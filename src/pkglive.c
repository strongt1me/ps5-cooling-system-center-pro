/* A package straight from the PC (07.10.2026).
 *
 * The browser on a PC cuts a .pkg file into pieces of one megabyte and sends them, while the console installs the
 * package: nothing is written to a drive first, so the installation needs the room of the installed game only, not
 * that and the file besides. The console's installer reads the package from the app's own server (pkgstream.c) like
 * any other package; this module is what stands behind that server when the bytes come from the browser.
 *
 *     browser --PUT piece--> pkglive.c: ring of 64 pieces in memory --read--> pkgstream.c --HTTP ranges--> installer
 *
 * The installer does not read in order and does not read once: it reads the first 64 KiB several times, then two
 * long connections take adjacent 16 MiB ranges, and a second attempt starts at the beginning again. So the browser
 * does not push the file in one go. It asks (GET state, and the answer to every piece it sent) what is wanted:
 *   - first the pieces a reader is waiting for ("demands"),
 *   - then, while an installation reads, the next pieces after each reader's position, as far as the ring has room.
 * The first piece stays in the ring for good (the header is read again and again); a piece that was read to its end is
 * the first to be replaced; one that was not read yet is replaced only for a demand.
 *
 * A reader that has to wait gives up when the browser has not been heard from for 40 seconds, or when the piece it
 * waits for does not come for two minutes: the installation then fails with a clear message instead of hanging.
 * A session nobody talks to (and no installation reads) expires after three minutes. One session at a time.
 *
 * The first piece also tells what the package is: a thread reads the container through the same ring (asking the
 * browser for the pieces it needs) and fills in name, ids, kind and platform, which the plan and the installation need.
 *
 * Memory: the ring is allocated when the session starts and freed when it ends, and never while a piece is being
 * received into it (refs). */

#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

#include "pkginst.h"
#include "ps5tm.h"
#include "third_party/cJSON.h"

#define LV_SEG          (1u << 20)
#ifndef LV_SLOTS
#define LV_SLOTS        64u
#endif
#ifndef LV_CONTACT_MS
#define LV_CONTACT_MS   40000u         /* a waiting reader gives up when the browser was silent this long */
#endif
#ifndef LV_WAIT_MS
#define LV_WAIT_MS      120000u        /* ... or when one piece does not come for this long */
#endif
#ifndef LV_IDLE_MS
#define LV_IDLE_MS      180000u        /* nobody talks to the session and no installation reads it: it ends */
#endif
#ifndef LV_PLAN_MS
#define LV_PLAN_MS      (30u * 60u * 1000u)   /* a session whose installation was not started within this long ends */
#endif
#define LV_MIN_TOTAL    0x10000ull
#define LV_MAX_TOTAL    (2ull << 40)
#define LV_DEM_MAX      32
#define LV_RDR_MAX      4
#define LV_SEND_MAX     8
#define LV_AHEAD        12             /* pieces looked ahead of a reader */
#define LV_RDR_LIVE_MS  10000u         /* a reader that has not read for this long is not looked ahead of */

enum { S_HEAD = 1, S_PARSING, S_READY, S_FAILED };
enum { F_FILL = 1, F_DONE = 2, F_PIN = 4 };

static struct {
  pthread_mutex_t lock;
  pthread_cond_t  cv;
  int             active, free_pending, installing, state;
  int             refs;                                 /* pieces being received into the ring */
  unsigned long   gen;
  unsigned long   ver;                                  /* counts what could change the browser's list of wishes */
  unsigned        seq;
  char            id[24], name[160], why[240], parse_err[200];
  uint64_t        total, last_contact, bytes_in, began_ms;
  uint32_t        nseg;
  unsigned        nslots, resident;
  unsigned char  *ring;
  int16_t        *slot_of;                              /* per piece: slot, -1 not there, -2 being received */
  int32_t        *seg_of;                               /* per slot: piece, -1 free */
  uint64_t       *last_use;
  uint8_t        *flags;
  struct { uint32_t seg; uint64_t ms; } dem[LV_DEM_MAX], rdr[LV_RDR_MAX];
  unsigned        ndem;
  ps5tm_pkg_t     pkg;
} L = { .lock = PTHREAD_MUTEX_INITIALIZER, .cv = PTHREAD_COND_INITIALIZER };

static uint64_t
now_ms(void) { return ps5tm_mono_ms(); }

/* Something changed that the browser (waiting in GET state) and the readers (waiting for a piece) care about. */
static void
bump_locked(void) {
  L.ver++;
  pthread_cond_broadcast(&L.cv);
}

static void
free_all_locked(void) {
  free(L.ring); free(L.slot_of); free(L.seg_of); free(L.last_use); free(L.flags);
  L.ring = NULL; L.slot_of = NULL; L.seg_of = NULL; L.last_use = NULL; L.flags = NULL;
  L.free_pending = 0;
  L.resident = 0;
  L.ndem = 0;
}

/* The session is over (the memory goes as soon as nothing writes into it). */
static void
end_locked(const char *why) {
  if(!L.active) return;
  L.active = 0;
  L.installing = 0;
  if(why && why[0] && !L.why[0]) snprintf(L.why, sizeof(L.why), "%s", why);
  bump_locked();
  if(L.refs == 0) free_all_locked();
  else L.free_pending = 1;
}

static void
expire_locked(void) {
  if(L.active && !L.installing && now_ms() - L.last_contact > LV_IDLE_MS)
    end_locked("Die Übertragung vom PC wurde nicht fortgesetzt und ist beendet worden.");
  else if(L.active && !L.installing && now_ms() - L.began_ms > LV_PLAN_MS)       /* a page left open with the plan on it must not hold 64 MB for ever */
    end_locked("Die Installation wurde nicht gestartet; die Übertragung vom PC ist nach einer halben Stunde beendet worden.");
}

static int
other_busy(void) {
  return ps5tm_gamecopy_busy() || ps5tm_gameconvert_busy() || ps5tm_gamemove_busy() || ps5tm_saves_busy() ||
         ps5tm_pkgsplit_busy() || ps5tm_gamedelete_busy() || ps5tm_pkginst_busy();
}

/* ------------------------------------------------------------------ demands and readers */

static void
dem_add_locked(uint32_t s) {
  for(unsigned i = 0; i < L.ndem; i++)
    if(L.dem[i].seg == s) return;
  if(L.ndem == LV_DEM_MAX) {                            /* the oldest goes */
    memmove(&L.dem[0], &L.dem[1], sizeof(L.dem[0]) * (LV_DEM_MAX - 1));
    L.ndem--;
  }
  L.dem[L.ndem].seg = s;
  L.dem[L.ndem].ms = now_ms();
  L.ndem++;
  bump_locked();
}

static void
dem_del_locked(uint32_t s) {
  for(unsigned i = 0; i < L.ndem; i++)
    if(L.dem[i].seg == s) {
      memmove(&L.dem[i], &L.dem[i + 1], sizeof(L.dem[0]) * (L.ndem - i - 1));
      L.ndem--;
      return;
    }
}

static void
rdr_note_locked(uint32_t s) {
  uint64_t now = now_ms();
  unsigned pick = 0;
  for(unsigned i = 0; i < LV_RDR_MAX; i++) {
    if(L.rdr[i].ms && (L.rdr[i].seg + LV_AHEAD >= s && s + LV_AHEAD >= L.rdr[i].seg)) { pick = i; goto set; }
    if(L.rdr[i].ms < L.rdr[pick].ms) pick = i;
  }
set:
  if(!L.rdr[pick].ms || L.rdr[pick].seg != s) bump_locked();       /* a reader moved on: other pieces are wanted now */
  L.rdr[pick].seg = s;
  L.rdr[pick].ms = now;
}

/* A slot for a piece: a free one; else one whose piece was read to its end (the least recently used); else, for a
   piece somebody waits for, any piece that is not the first. -1: none. */
static int
pick_slot_locked(int demanded) {
  int best = -1;
  for(unsigned i = 0; i < L.nslots; i++)
    if(L.seg_of[i] < 0) return (int)i;
  for(unsigned i = 0; i < L.nslots; i++)
    if((L.flags[i] & F_DONE) && !(L.flags[i] & (F_PIN | F_FILL)) && (best < 0 || L.last_use[i] < L.last_use[best])) best = (int)i;
  if(best < 0 && demanded)
    for(unsigned i = 0; i < L.nslots; i++)
      if(!(L.flags[i] & (F_PIN | F_FILL)) && (best < 0 || L.last_use[i] < L.last_use[best])) best = (int)i;
  if(best >= 0) {
    L.slot_of[L.seg_of[best]] = -1;
    L.seg_of[best] = -1;
    L.flags[best] = 0;
    if(L.resident) L.resident--;
  }
  return best;
}

static unsigned
room_locked(void) {
  unsigned r = 0;
  for(unsigned i = 0; i < L.nslots; i++)
    if(L.seg_of[i] < 0 || ((L.flags[i] & F_DONE) && !(L.flags[i] & (F_PIN | F_FILL)))) r++;
  return r;
}

/* What the browser should send now, most important first. */
static unsigned
send_list_locked(uint32_t *out, unsigned max) {
  unsigned n = 0;
  if(L.state == S_HEAD) {
    if(L.slot_of[0] == -1) out[n++] = 0;
    return n;
  }
  for(unsigned i = 0; i < L.ndem && n < max; i++)
    if(L.slot_of[L.dem[i].seg] == -1) out[n++] = L.dem[i].seg;
  if(!L.installing) return n;
  uint64_t now = now_ms();
  unsigned room = room_locked();
  for(unsigned k = 1; k <= LV_AHEAD && n < max && room; k++)
    for(unsigned r = 0; r < LV_RDR_MAX && n < max && room; r++) {
      if(!L.rdr[r].ms || now - L.rdr[r].ms > LV_RDR_LIVE_MS) continue;
      uint32_t s = L.rdr[r].seg + k;
      if(s >= L.nseg || L.slot_of[s] != -1) continue;
      int dup = 0;
      for(unsigned i = 0; i < n; i++) if(out[i] == s) dup = 1;
      if(dup) continue;
      out[n++] = s;
      room--;
    }
  return n;
}

static const char *
state_word_locked(void) {
  if(!L.active) return "ended";
  switch(L.state) {
    case S_HEAD:    return "head";
    case S_PARSING: return "parsing";
    case S_READY:   return "ready";
    default:        return "failed";
  }
}

static cJSON *
state_json_locked(void) {
  cJSON *o = cJSON_CreateObject();
  if(!o) return NULL;
  cJSON_AddBoolToObject(o, "ok", 1);
  cJSON_AddStringToObject(o, "id", L.id);
  cJSON_AddStringToObject(o, "state", state_word_locked());
  cJSON_AddStringToObject(o, "name", L.name);
  cJSON_AddNumberToObject(o, "size", (double)L.total);
  cJSON_AddNumberToObject(o, "seg_bytes", LV_SEG);
  cJSON_AddNumberToObject(o, "slots", L.nslots);
  cJSON_AddNumberToObject(o, "bytes_in", (double)L.bytes_in);
  cJSON_AddNumberToObject(o, "resident", L.resident);
  cJSON_AddBoolToObject(o, "installing", L.installing);
  cJSON_AddNumberToObject(o, "ver", (double)L.ver);
  cJSON_AddStringToObject(o, "error", L.active ? (L.state == S_FAILED ? L.parse_err : "") : L.why);
  cJSON *send = cJSON_AddArrayToObject(o, "send");
  if(L.active) {
    uint32_t list[LV_SEND_MAX];
    unsigned n = send_list_locked(list, LV_SEND_MAX);
    for(unsigned i = 0; i < n; i++) cJSON_AddItemToArray(send, cJSON_CreateNumber(list[i]));
  }
  if(L.active && L.state == S_READY) {
    cJSON_AddStringToObject(o, "title_id", L.pkg.title_id);
    cJSON_AddStringToObject(o, "content_id", L.pkg.content_id);
    cJSON_AddStringToObject(o, "title", L.pkg.name);
    cJSON_AddStringToObject(o, "version", L.pkg.version);
    cJSON_AddStringToObject(o, "kind", L.pkg.kind);
    cJSON_AddNumberToObject(o, "plat", L.pkg.plat);
  }
  return o;
}

/* ------------------------------------------------------------------ reading (for the installer and for the parser) */

int
ps5tm_pkglive_read(const char *id, uint64_t off, void *vbuf, size_t n) {
  unsigned char *buf = vbuf;
  pthread_mutex_lock(&L.lock);
  while(n) {
    if(!L.active || !id || strcmp(id, L.id) || off >= L.total) { pthread_mutex_unlock(&L.lock); return -1; }
    uint32_t s = (uint32_t)(off / LV_SEG);
    int slot = L.slot_of[s];
    if(slot >= 0) {
      uint64_t seg_start = (uint64_t)s * LV_SEG;
      uint64_t seg_len = L.total - seg_start < LV_SEG ? L.total - seg_start : LV_SEG;
      uint64_t in = off - seg_start;
      size_t take = (uint64_t)n < seg_len - in ? n : (size_t)(seg_len - in);
      memcpy(buf, L.ring + (size_t)slot * LV_SEG + in, take);
      L.last_use[slot] = now_ms();
      if(in + take == seg_len && !(L.flags[slot] & F_DONE)) { L.flags[slot] |= F_DONE; bump_locked(); }       /* its slot can be used again */
      rdr_note_locked(s);
      buf += take;
      off += take;
      n -= take;
      continue;
    }
    /* the piece is not there: ask for it, and wait */
    uint64_t began = now_ms();
    dem_add_locked(s);
    rdr_note_locked(s);
    while(L.active && L.slot_of[s] < 0) {
      uint64_t now = now_ms();
      if(now - L.last_contact > LV_CONTACT_MS) {
        char w[200];
        snprintf(w, sizeof(w), "Der Browser hat sich seit %u Sekunden nicht mehr gemeldet. Die Übertragung vom PC ist abgebrochen; die Seite muss offen bleiben, bis die Installation fertig ist.",
                 (unsigned)((now - L.last_contact) / 1000));
        end_locked(w);
        break;
      }
      if(now - began > LV_WAIT_MS) {
        end_locked("Ein Stück des Pakets kam vom PC nicht rechtzeitig an. Die Übertragung ist abgebrochen.");
        break;
      }
      struct timespec ts;
      clock_gettime(CLOCK_REALTIME, &ts);
      ts.tv_nsec += 250 * 1000000L;
      if(ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
      pthread_cond_timedwait(&L.cv, &L.lock, &ts);
    }
    if(!L.active) { pthread_mutex_unlock(&L.lock); return -1; }
  }
  pthread_mutex_unlock(&L.lock);
  return 0;
}

static int
rd_cb(void *ctx, void *buf, size_t n, uint64_t off) {
  return ps5tm_pkglive_read((const char *)ctx, off, buf, n);
}

/* ------------------------------------------------------------------ the session */

typedef struct { unsigned long gen; char id[24]; } thr_arg_t;

static void *
parse_thread(void *vp) {
  thr_arg_t *a = vp;
  ps5tm_pkg_t *p = calloc(1, sizeof(*p));
  char name[160];
  uint64_t total;
  pthread_mutex_lock(&L.lock);
  snprintf(name, sizeof(name), "%s", L.name);
  total = L.total;
  pthread_mutex_unlock(&L.lock);
  int rc = p ? ps5tm_pkg_parse_reader(rd_cb, a->id, total, name, p) : -1;
  pthread_mutex_lock(&L.lock);
  if(L.active && L.gen == a->gen) {
    if(rc == 0 && p->valid) {
      L.pkg = *p;
      snprintf(L.pkg.path, sizeof(L.pkg.path), "%s%s", PKGLIVE_PREFIX, L.id);
      snprintf(L.pkg.file, sizeof(L.pkg.file), "%s", L.name);
      if(!L.pkg.name[0]) snprintf(L.pkg.name, sizeof(L.pkg.name), "%s", L.name);
      L.state = S_READY;
    } else {
      snprintf(L.parse_err, sizeof(L.parse_err), "Die Datei ließ sich nicht als PS4- oder PS5-Paket lesen. Ist es ein .pkg für die Konsole (und nicht in Teile geteilt)?");
      L.state = S_FAILED;
    }
    bump_locked();
  }
  pthread_mutex_unlock(&L.lock);
  free(p);
  free(a);
  return NULL;
}

static void *
janitor_thread(void *vp) {
  thr_arg_t *a = vp;
  for(;;) {
    for(int i = 0; i < 20; i++) usleep(250 * 1000);
    pthread_mutex_lock(&L.lock);
    if(!L.active || L.gen != a->gen) { pthread_mutex_unlock(&L.lock); break; }
    expire_locked();
    pthread_mutex_unlock(&L.lock);
  }
  free(a);
  return NULL;
}

static void
spawn(void *(*fn)(void *), unsigned long gen, const char *id, size_t stack) {
  thr_arg_t *a = calloc(1, sizeof(*a));
  if(!a) return;
  a->gen = gen;
  snprintf(a->id, sizeof(a->id), "%s", id);
  pthread_attr_t at;
  pthread_attr_init(&at);
  pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
  pthread_attr_setstacksize(&at, stack);
  pthread_t th;
  if(pthread_create(&th, &at, fn, a) != 0) free(a);
  pthread_attr_destroy(&at);
}

/* Well-formed UTF-8 (the name goes into JSON the page reads). */
static int
utf8_ok(const unsigned char *s, size_t n) {
  for(size_t i = 0; i < n;) {
    unsigned char c = s[i];
    size_t l = c < 0x80 ? 1 : (c >= 0xC2 && c <= 0xDF) ? 2 : (c >= 0xE0 && c <= 0xEF) ? 3 : (c >= 0xF0 && c <= 0xF4) ? 4 : 0;
    if(!l || i + l > n) return 0;
    for(size_t k = 1; k < l; k++) if((s[i + k] & 0xC0) != 0x80) return 0;
    if(c == 0xE0 && s[i + 1] < 0xA0) return 0;
    if(c == 0xED && s[i + 1] > 0x9F) return 0;
    if(c == 0xF0 && s[i + 1] < 0x90) return 0;
    if(c == 0xF4 && s[i + 1] > 0x8F) return 0;
    i += l;
  }
  return 1;
}

static int
name_ok(const char *s) {
  size_t n = strlen(s);
  if(n < 5 || n >= sizeof(L.name)) return 0;
  for(size_t i = 0; i < n; i++)
    if((unsigned char)s[i] < 0x20 || s[i] == '/' || s[i] == '\\') return 0;
  return utf8_ok((const unsigned char *)s, n) && strcasecmp(s + n - 4, ".pkg") == 0;
}

cJSON *
ps5tm_pkglive_init(const char *name, uint64_t size, int *http, char *err, size_t err_len) {
  *http = 200;
  err[0] = 0;
  if(!name || !name_ok(name)) { *http = 400; snprintf(err, err_len, "Der Dateiname ist ungültig (eine .pkg-Datei wird erwartet)."); return NULL; }
  if(size < LV_MIN_TOTAL || size > LV_MAX_TOTAL) { *http = 400; snprintf(err, err_len, "Die Größe der Datei ist unplausibel für ein Paket."); return NULL; }
  if(other_busy()) {
    *http = 409;
    snprintf(err, err_len, "Es läuft gerade ein Kopieren, Konvertieren, Verschieben, Sichern, Löschen, Teilen oder eine Installation. Das läuft nicht gleichzeitig.");
    return NULL;
  }
  pthread_mutex_lock(&L.lock);
  expire_locked();
  if(L.active || L.refs > 0 || L.ring) {
    pthread_mutex_unlock(&L.lock);
    *http = 409;
    snprintf(err, err_len, "Es wird schon ein Paket vom PC übertragen (oder die letzte Übertragung wird noch beendet).");
    return NULL;
  }
  uint32_t nseg = (uint32_t)((size + LV_SEG - 1) / LV_SEG);
  unsigned slots = LV_SLOTS;
  unsigned char *ring = NULL;
  while(slots >= 8 && !(ring = malloc((size_t)slots * LV_SEG))) slots /= 2;
  int16_t *slot_of = malloc(sizeof(int16_t) * nseg);
  int32_t *seg_of = ring ? malloc(sizeof(int32_t) * slots) : NULL;
  uint64_t *last_use = ring ? calloc(slots, sizeof(uint64_t)) : NULL;
  uint8_t *flags = ring ? calloc(slots, 1) : NULL;
  if(!ring || !slot_of || !seg_of || !last_use || !flags) {
    free(ring); free(slot_of); free(seg_of); free(last_use); free(flags);
    pthread_mutex_unlock(&L.lock);
    *http = 503;
    snprintf(err, err_len, "Zu wenig Arbeitsspeicher für die Übertragung.");
    return NULL;
  }
  for(uint32_t i = 0; i < nseg; i++) slot_of[i] = -1;
  for(unsigned i = 0; i < slots; i++) seg_of[i] = -1;
  L.ring = ring; L.slot_of = slot_of; L.seg_of = seg_of; L.last_use = last_use; L.flags = flags;
  L.nslots = slots;
  L.nseg = nseg;
  L.total = size;
  L.active = 1;
  L.installing = 0;
  L.state = S_HEAD;
  L.free_pending = 0;
  L.resident = 0;
  L.ndem = 0;
  L.bytes_in = 0;
  L.why[0] = 0;
  L.parse_err[0] = 0;
  memset(&L.rdr, 0, sizeof(L.rdr));
  memset(&L.pkg, 0, sizeof(L.pkg));
  L.gen++;
  snprintf(L.id, sizeof(L.id), "live-%u", ++L.seq);
  snprintf(L.name, sizeof(L.name), "%s", name);
  L.last_contact = L.began_ms = now_ms();
  unsigned long gen = L.gen;
  char id[24];
  snprintf(id, sizeof(id), "%s", L.id);
  cJSON *o = state_json_locked();
  pthread_mutex_unlock(&L.lock);
  spawn(janitor_thread, gen, id, 64 * 1024);
  PS5TM_INFO("pkglive_init", "Paket vom PC: Übertragung von „%.80s“ (%llu MB) beginnt, Speicher %u MB.", name, (unsigned long long)(size >> 20), slots);
  return o;
}

/* The state. With since >= 0 and wait_ms > 0 it is a long poll: it answers when something changed since the version
   the browser last saw (or after wait_ms). The page lives on these answers and on the end of its own uploads, not on
   timers, so a page in a background tab, whose timers the browser slows down, keeps the transfer going. */
cJSON *
ps5tm_pkglive_state(const char *id, long since, unsigned wait_ms, int *http) {
  *http = 200;
  pthread_mutex_lock(&L.lock);
  if(!id || strcmp(id, L.id) || !L.id[0]) { pthread_mutex_unlock(&L.lock); *http = 404; return NULL; }
  if(L.active) { L.last_contact = now_ms(); expire_locked(); }
  if(L.active && since >= 0 && wait_ms) {
    uint64_t until = now_ms() + (wait_ms > 5000 ? 5000 : wait_ms);
    while(L.active && (long)L.ver == since && now_ms() < until) {
      L.last_contact = now_ms();
      struct timespec ts;
      clock_gettime(CLOCK_REALTIME, &ts);
      ts.tv_nsec += 200 * 1000000L;
      if(ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
      pthread_cond_timedwait(&L.cv, &L.lock, &ts);
    }
    if((long)L.ver != since && L.active) {                   /* not more than about 25 answers a second */
      pthread_mutex_unlock(&L.lock);
      usleep(40 * 1000);
      pthread_mutex_lock(&L.lock);
    }
    if(L.active) L.last_contact = now_ms();
  }
  cJSON *o = state_json_locked();
  pthread_mutex_unlock(&L.lock);
  return o;
}

int
ps5tm_pkglive_cancel(const char *id) {
  pthread_mutex_lock(&L.lock);
  int ok = id && !strcmp(id, L.id) && L.id[0];
  if(ok) end_locked("Die Übertragung wurde abgebrochen.");
  pthread_mutex_unlock(&L.lock);
  return ok ? 0 : -1;
}

void
ps5tm_pkglive_installing(const char *id, int on) {
  pthread_mutex_lock(&L.lock);
  if(id && !strcmp(id, L.id)) { L.installing = on && L.active; L.last_contact = now_ms(); bump_locked(); }
  pthread_mutex_unlock(&L.lock);
}

void
ps5tm_pkglive_end(const char *id) {
  pthread_mutex_lock(&L.lock);
  if(id && !strcmp(id, L.id)) end_locked("Die Installation ist beendet; die Übertragung ist zu Ende.");
  pthread_mutex_unlock(&L.lock);
}

const char *
ps5tm_pkglive_why(char *out, size_t n) {
  pthread_mutex_lock(&L.lock);
  snprintf(out, n, "%s", L.why);
  pthread_mutex_unlock(&L.lock);
  return out;
}

int
ps5tm_pkglive_find(const char *id, ps5tm_pkg_t *out) {
  int rc = -1;
  pthread_mutex_lock(&L.lock);
  if(L.active && id && !strcmp(id, L.id) && L.state == S_READY) { *out = L.pkg; rc = 0; }
  pthread_mutex_unlock(&L.lock);
  return rc;
}

int
ps5tm_pkglive_slices(const char *id, ps5tm_pkgslice_t *out, unsigned max, unsigned *n, uint64_t *total, char *err, size_t err_len) {
  *n = 0;
  *total = 0;
  err[0] = 0;
  int rc = -1;
  pthread_mutex_lock(&L.lock);
  if(!L.active || !id || strcmp(id, L.id)) snprintf(err, err_len, "Die Übertragung vom PC ist beendet. Bitte die Datei noch einmal wählen.");
  else if(L.state != S_READY) snprintf(err, err_len, "Das Paket wird noch gelesen.");
  else if(max < 1) snprintf(err, err_len, "Zu wenig Platz in der Liste.");
  else {
    memset(&out[0], 0, sizeof(out[0]));
    snprintf(out[0].path, sizeof(out[0].path), "%s%s", PKGLIVE_PREFIX, L.id);
    out[0].size = L.total;
    *n = 1;
    *total = L.total;
    rc = 0;
  }
  pthread_mutex_unlock(&L.lock);
  return rc;
}

int
ps5tm_pkglive_icon(const ps5tm_pkg_t *p, uint8_t **data, size_t *n) {
  if(!p || strncmp(p->path, PKGLIVE_PREFIX, strlen(PKGLIVE_PREFIX))) return -1;
  return ps5tm_pkg_icon_reader(p, rd_cb, (void *)(p->path + strlen(PKGLIVE_PREFIX)), data, n);
}

/* ------------------------------------------------------------------ receiving a piece (http.c) */

/* Reserves a slot for piece n and hands out its memory. 0: *buf is the place to write (call commit afterwards), or
   *dup is set (the piece is there or on its way: nothing to write). Else an HTTP status in *http. */
static int
put_begin(const char *id, uint32_t n, size_t len, unsigned char **buf, int *slot_out, int *dup, int *http, char *err, size_t err_len) {
  *buf = NULL;
  *dup = 0;
  *http = 200;
  pthread_mutex_lock(&L.lock);
  if(!L.id[0] || !id || strcmp(id, L.id)) { *http = 404; snprintf(err, err_len, "Diese Übertragung gibt es nicht."); goto fail; }
  if(!L.active) { *http = 410; snprintf(err, err_len, "%s", L.why[0] ? L.why : "Die Übertragung ist beendet."); goto fail; }
  L.last_contact = now_ms();
  if(n >= L.nseg) { *http = 400; snprintf(err, err_len, "Dieses Stück gibt es nicht."); goto fail; }
  uint64_t start = (uint64_t)n * LV_SEG;
  uint64_t want = L.total - start < LV_SEG ? L.total - start : LV_SEG;
  if(len != want) { *http = 400; snprintf(err, err_len, "Das Stück hat nicht die erwartete Länge."); goto fail; }
  if(L.slot_of[n] != -1) { *dup = 1; pthread_mutex_unlock(&L.lock); return 0; }
  int demanded = n == 0;
  for(unsigned i = 0; i < L.ndem; i++) if(L.dem[i].seg == n) demanded = 1;
  int slot = pick_slot_locked(demanded);
  if(slot < 0) { *http = 503; snprintf(err, err_len, "Der Speicher der Konsole ist gerade voll; bitte kurz warten."); goto fail; }
  L.seg_of[slot] = (int32_t)n;
  L.flags[slot] = F_FILL;
  L.slot_of[n] = -2;
  L.refs++;
  *slot_out = slot;
  *buf = L.ring + (size_t)slot * LV_SEG;
  pthread_mutex_unlock(&L.lock);
  return 0;
fail:
  pthread_mutex_unlock(&L.lock);
  return -1;
}

static void
put_commit(unsigned long gen, uint32_t n, int slot, size_t len, int ok) {
  pthread_mutex_lock(&L.lock);
  if(L.active && L.gen == gen && L.ring) {
    if(ok) {
      L.slot_of[n] = (int16_t)slot;
      L.flags[slot] = n == 0 ? F_PIN : 0;
      L.last_use[slot] = now_ms();
      L.resident++;
      L.bytes_in += len;
      dem_del_locked(n);
      if(n == 0 && L.state == S_HEAD) {
        L.state = S_PARSING;
        char id[24];
        snprintf(id, sizeof(id), "%s", L.id);
        spawn(parse_thread, L.gen, id, 512 * 1024);
      }
    } else {
      L.slot_of[n] = -1;
      L.seg_of[slot] = -1;
      L.flags[slot] = 0;
    }
    bump_locked();
  }
  if(--L.refs == 0 && L.free_pending) free_all_locked();
  pthread_mutex_unlock(&L.lock);
}

static void
query_value(const char *q, const char *key, char *out, size_t n) {
  size_t kl = strlen(key);
  out[0] = 0;
  for(const char *p = q; p && *p;) {
    if(!strncmp(p, key, kl) && p[kl] == '=') {
      size_t l = strcspn(p + kl + 1, "&");
      if(l < n) { memcpy(out, p + kl + 1, l); out[l] = 0; }
      return;
    }
    p = strchr(p, '&');
    if(p) p++;
  }
}

/* Reads up to want bytes from fd into dst (or throws them away when dst is NULL). Returns the bytes read; short means
   the sender stopped. */
static size_t
read_body(int fd, unsigned char *dst, size_t want) {
  size_t got = 0;
  uint64_t deadline = now_ms() + 120000;
  unsigned char sink[4096];
  while(got < want && now_ms() < deadline) {
    struct pollfd pf = { fd, POLLIN, 0 };
    int r = poll(&pf, 1, 10000);
    if(r < 0 && errno == EINTR) continue;
    if(r <= 0) break;
    size_t chunk = want - got;
    ssize_t k = read(fd, dst ? dst + got : sink, dst ? chunk : (chunk < sizeof(sink) ? chunk : sizeof(sink)));
    if(k < 0 && errno == EINTR) continue;
    if(k <= 0) break;
    got += (size_t)k;
  }
  return got;
}

static void
send_obj(int fd, cJSON *o) {
  if(!o) { ps5tm_http_send_error(fd, 500, "alloc_failed", "Kein Speicher."); return; }
  char *txt = cJSON_PrintUnformatted(o);
  cJSON_Delete(o);
  if(!txt) { ps5tm_http_send_error(fd, 500, "alloc_failed", "Kein Speicher."); return; }
  ps5tm_http_send(fd, 200, "OK", "application/json", txt, strlen(txt), NULL);
  free(txt);
}

void
ps5tm_pkglive_receive(int fd, const char *query, const char *prefix, size_t prefix_len, size_t total) {
  char id[24], nstr[16], err[240];
  query_value(query, "id", id, sizeof(id));
  query_value(query, "n", nstr, sizeof(nstr));
  char *end = NULL;
  unsigned long n = nstr[0] ? strtoul(nstr, &end, 10) : 0;
  if(!nstr[0] || (end && *end) || n > 0x7FFFFFFFul || total == 0 || total > LV_SEG) {
    ps5tm_http_send_error(fd, 400, "bad_piece", "Das Stück ist falsch angegeben.");
    return;
  }
  if(prefix_len > total) prefix_len = total;
  unsigned char *buf = NULL;
  int slot = -1, dup = 0, http = 200;
  pthread_mutex_lock(&L.lock);
  unsigned long gen = L.gen;
  pthread_mutex_unlock(&L.lock);
  if(put_begin(id, (uint32_t)n, total, &buf, &slot, &dup, &http, err, sizeof(err)) != 0) {
    read_body(fd, NULL, total - prefix_len);                    /* the browser is still sending: let it finish */
    ps5tm_http_send_error(fd, http, http == 503 ? "busy" : "live_refused", err);
    return;
  }
  if(dup) {
    read_body(fd, NULL, total - prefix_len);
    int h;
    send_obj(fd, ps5tm_pkglive_state(id, -1, 0, &h));
    return;
  }
  memcpy(buf, prefix, prefix_len);
  size_t got = prefix_len + (prefix_len < total ? read_body(fd, buf + prefix_len, total - prefix_len) : 0);
  put_commit(gen, (uint32_t)n, slot, total, got == total);
  if(got != total) {
    ps5tm_http_send_error(fd, 408, "piece_incomplete", "Das Stück kam nicht vollständig an.");
    return;
  }
  int h;
  send_obj(fd, ps5tm_pkglive_state(id, -1, 0, &h));
}
