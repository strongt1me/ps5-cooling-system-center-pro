/* Live view of the kernel log, for the page's "Klog" tab (03.10.2026).
 *
 * The kernel keeps its messages in a ring that sysctl kern.msgbuf hands out
 * whole, oldest line first — the source the game detection, the microphone
 * button and the clock table read already. A viewer wants what is new since
 * it last asked, and the ring has no counter to say where that starts. So each
 * read is compared with the one before: the last few lines of that one (their
 * hashes, KLOG_ANCHOR of them) are looked for from the end of this one, and
 * what follows them is new. Not found means the ring turned over in between:
 * the lines that fell out are gone, and a marker line says so.
 *
 * Not the source klogsrv uses. That is /dev/klog, a stream the kernel gives
 * away as it is read, and whoever tails port 3232 should not lose lines to
 * this page. kern.msgbuf is a copy; nobody is deprived of anything.
 *
 * Reads happen when a page asks, at most every KLOG_MIN_READ_MS however many
 * are asking. Nothing runs, and nothing is read, while nobody is watching.
 *
 * Lines are kept as the kernel wrote them, cut at PS5TM_KLOG_LINE bytes. The
 * oldest line of a read is dropped (the ring's edge may have cut it), and so is
 * the text after the last newline (the kernel is still writing that line; it
 * comes with the next read). Blank lines, which a ring that has not wrapped
 * yet shows as NULs, are not lines. */

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ps5tm.h"

#define KLOG_RING         3000     /* lines kept; older ones are overwritten  */
#define KLOG_ANCHOR       6        /* lines that tell where the last read ended */
#define KLOG_MIN_READ_MS  500

#define KLOG_GAP_NOTE \
  "— Lücke: Zeilen zwischen zwei Abfragen sind verloren gegangen —"

typedef struct {
  uint64_t seq;
  uint64_t t_ms;
  uint16_t len;
  uint8_t  marker;
  char     text[PS5TM_KLOG_LINE];
} entry_t;

typedef struct {
  uint32_t off;
  uint32_t len;
  uint64_t hash;
} span_t;

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static entry_t        *g_ring;                  /* KLOG_RING entries, on first use */
static uint64_t        g_seq;                   /* newest number handed out        */
static uint64_t        g_read_ms;               /* when the buffer was last read   */
static uint64_t        g_anchor[KLOG_ANCHOR];   /* the last lines of the last read */
static unsigned        g_anchor_n;


static uint64_t
fnv1a(const char *p, size_t n) {
  uint64_t h = 1469598103934665603ull;
  for(size_t i = 0; i < n; i++) {
    h ^= (unsigned char)p[i];
    h *= 1099511628211ull;
  }
  return h;
}


static void
push(const char *text, size_t len, int marker, uint64_t t_ms) {
  entry_t *e = &g_ring[(g_seq + 1) % KLOG_RING];
  /* Cut at a character boundary, and say so. */
  if(len > PS5TM_KLOG_LINE - 1) {
    size_t cut = PS5TM_KLOG_LINE - 4;           /* room for the ellipsis */
    while(cut > 0 && ((unsigned char)text[cut] & 0xC0) == 0x80) cut--;
    memcpy(e->text, text, cut);
    memcpy(e->text + cut, "\xE2\x80\xA6", 3);   /* … */
    e->len = (uint16_t)(cut + 3);
  } else {
    memcpy(e->text, text, len);
    e->len = (uint16_t)len;
  }
  e->text[e->len] = 0;
  e->marker = (uint8_t)marker;
  e->t_ms   = t_ms;
  e->seq    = ++g_seq;
}


/* One read of the buffer: find what is new in it and append that. */
static void
ingest(const char *buf, size_t len) {
  /* Only up to the last newline: after it is a line still being written. */
  size_t usable = len;
  while(usable > 0 && buf[usable - 1] != '\n') usable--;
  if(usable == 0) return;

  span_t *sp  = NULL;
  size_t  n   = 0;
  size_t  cap = 0;
  int     first = 1;

  for(size_t i = 0; i < usable; ) {
    const char *nl = memchr(buf + i, '\n', usable - i);
    size_t      l  = (size_t)(nl - (buf + i));
    size_t      st = i;
    i += l + 1;
    if(first) { first = 0; continue; }          /* the ring's edge may cut it */
    if(l > 0 && buf[st + l - 1] == '\r') l--;
    if(l == 0) continue;                        /* blank: not a line */
    if(n == cap) {
      size_t nc = cap ? cap * 2 : 4096;
      span_t *g = realloc(sp, nc * sizeof(*sp));
      if(!g) { free(sp); return; }
      sp = g; cap = nc;
    }
    sp[n].off  = (uint32_t)st;
    sp[n].len  = (uint32_t)l;
    sp[n].hash = fnv1a(buf + st, l);
    n++;
  }
  if(n == 0) { free(sp); return; }

  /* Where the new lines start: after the anchor, and never more than the copy
     holds. Lines the copy cannot hold, like an anchor that is not there, are
     lines lost. */
  size_t from = n > KLOG_RING ? n - KLOG_RING : 0;
  int lost = 0;
  if(g_anchor_n > 0) {
    int found = 0;
    for(size_t e = n; e >= g_anchor_n; e--) {   /* newest match first */
      size_t s = e - g_anchor_n;
      unsigned j = 0;
      while(j < g_anchor_n && sp[s + j].hash == g_anchor[j]) j++;
      if(j == g_anchor_n) {
        if(e > from) from = e; else lost = 1;
        found = 1;
        break;
      }
    }
    if(!found) lost = 1;
  }

  uint64_t now = ps5tm_now_ms();
  if(lost && g_seq > 0) push(KLOG_GAP_NOTE, strlen(KLOG_GAP_NOTE), 1, now);
  for(size_t k = from; k < n; k++) push(buf + sp[k].off, sp[k].len, 0, now);

  /* The next read looks for the end of this one. */
  unsigned keep = n < KLOG_ANCHOR ? (unsigned)n : KLOG_ANCHOR;
  for(unsigned j = 0; j < keep; j++) g_anchor[j] = sp[n - keep + j].hash;
  g_anchor_n = keep;
  free(sp);
}


/* Caller holds the lock. */
static void
refresh_locked(void) {
  uint64_t now = ps5tm_mono_ms();
  if(g_read_ms && now - g_read_ms < KLOG_MIN_READ_MS) return;
  g_read_ms = now;

  size_t len = 0;
  char  *buf = ps5tm_msgbuf_read(&len);
  if(!buf) return;
  ingest(buf, len);
  free(buf);
}


unsigned
ps5tm_klog_fetch(uint64_t after, unsigned max, ps5tm_klog_line_t *out,
                 uint64_t *newest, int *more, int *lost) {
  *newest = 0;
  *more   = 0;
  *lost   = 0;
  if(!out || max == 0) return 0;

  pthread_mutex_lock(&g_lock);
  if(!g_ring) g_ring = calloc(KLOG_RING, sizeof(*g_ring));
  if(!g_ring) {
    pthread_mutex_unlock(&g_lock);
    return 0;
  }
  refresh_locked();

  uint64_t last   = g_seq;
  uint64_t oldest = last > KLOG_RING ? last - KLOG_RING + 1 : 1;
  uint64_t first;

  if(after > last) {
    /* A cursor from before this app started: the numbers begin again. */
    after = 0;
    *lost = 1;
  }
  if(after == 0) {
    first = last >= max ? last - max + 1 : 1;
  } else {
    first = after + 1;
    if(first < oldest) *lost = 1;
  }
  if(first < oldest) first = oldest;

  unsigned n = 0;
  uint64_t s = first;
  for(; s <= last && n < max; s++, n++) {
    const entry_t *e = &g_ring[s % KLOG_RING];
    out[n].seq    = e->seq;
    out[n].t_ms   = e->t_ms;
    out[n].marker = e->marker;
    memcpy(out[n].text, e->text, (size_t)e->len + 1);
  }
  *newest = last;
  *more   = s <= last;
  pthread_mutex_unlock(&g_lock);
  return n;
}
