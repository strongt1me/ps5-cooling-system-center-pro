/* Optimization by recording (13.10.2026): a profile for the LZ4 asset packs of one game, made from how the game really reads
 * its files.
 *
 *   start     the debug APR-EMU (0.4.2.1 test-debug-pack) is put into <game>/fakelib2, with the other libraries of fakelib
 *             beside it: ShadowMountPlus mounts fakelib2 instead of fakelib and never replaces what stands in it. The game
 *             then writes /app0/ampr_commands.bin (journal AMPRCMD1) while it is played.
 *   collect   the journal and the ampr_emu.index it belongs to are copied to <data>/traces/<TITLE_ID>/<n>/ (the emulator
 *             empties the journal at every launch).
 *   generate  all collected sessions are merged and turned into <data>/LZ4_Profile/<TITLE_ID>-mitschnitt.toml: per file read
 *             the block size, the layout (random, mixed, streaming), the hot flag and the action (media are stored), and
 *             the [runtime] cache and worker settings from a cache simulation. Files the game never read stay loose.
 *   stop      fakelib2 goes away again (only the one this module made: it carries a marker).
 *
 * This is a port of drakmor's tools/ampr_pack_profile.py (generate, exact pattern mode) and of the AMPRCMD1 decoder of
 * tools/parse_ampr_command_log.py (GPL-3.0). The structure of the port and its constants follow the trace module of
 * exFAT2AMPR by V-10, which ran the same steps on a console (https://github.com/exFAT2AMPR/exfat2ampr, GPL-3.0-or-later
 * with additional terms, "exFAT2AMPR - Copyright (C) 2026 V-10", "Made by V-10"); like that port, this one has a single
 * group and no index-size pass. The rules written are those of the Python tool (action, block_size, layout, hot, streaming)
 * so the profile also works with its packer. */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "ioerr.h"
#include "ps5tm.h"
#include "third_party/cJSON.h"

#define KIB 1024ull
#define MIB (1024ull * 1024)
#define NBS 7
static const uint32_t BS[NBS] = { 16 * 1024, 32 * 1024, 64 * 1024, 128 * 1024, 256 * 1024, 512 * 1024, 1024 * 1024 };
enum { L_MIXED = 0, L_RANDOM = 1, L_STREAMING = 2 };
enum { A_COMPRESS = 0, A_STORE = 1, A_LOOSE = 2 };

#define TR_DEBUG_EMU   "libSceAmpr.sprx-0.4.2.1-test-debug-pack"
#define TR_MARKER      ".ps5cc-trace"
#define TR_MARKER_TEXT "ps5cc-trace\n"
#define TR_DIR         PS5TM_DATA_DIR "/traces"
#define TR_PROFILE_DIR PS5TM_DATA_DIR "/LZ4_Profile"

/* ------------------------------------------------------------------------------------------------ the model */

typedef struct { uint64_t off; uint32_t len; uint16_t trace; } ev_t;

typedef struct {
  char    *rel;                     /* path relative to /app0 */
  uint64_t size;                    /* the largest over the sessions */
  ev_t    *ev;
  size_t   n, cap;
  uint64_t requested;
  double   p50, p90, tiny4, small16, small64, large256;
  double   exact, near, random, repeat64;
  double   score[NBS];
  int      action, layout, block_idx;
  int      hot, recommended;
  uint32_t ordinal;
} file_t;

typedef struct { uint32_t file; uint64_t off; uint32_t len; } gev_t;      /* the reads in the order they came */

typedef struct {
  file_t   *f;
  size_t    n, cap;
  uint32_t *tab;                    /* rel -> 1-based file index (open addressing) */
  size_t    tcap;
  gev_t    *g;
  size_t    gn, gcap;
  uint64_t  records, decode_errors, dropped, gaps, oom;
  int       sim_oom;
  int       traces;
} model_t;

static uint64_t
hstr(const char *s) {
  uint64_t h = 1469598103934665603ull;
  for(; *s; s++) { h ^= (uint8_t)*s; h *= 1099511628211ull; }
  return h;
}

static int
grow(void **p, size_t *cap, size_t need, size_t elem) {
  if(need <= *cap) return 0;
  size_t nc = *cap ? *cap * 2 : 256;
  while(nc < need) nc *= 2;
  void *np = realloc(*p, nc * elem);
  if(!np) return -1;
  *p = np;
  *cap = nc;
  return 0;
}

static int
rehash(model_t *m, size_t cap) {
  uint32_t *t = calloc(cap, sizeof(*t));
  if(!t) return -1;
  for(size_t i = 0; i < m->n; i++) {
    size_t pos = hstr(m->f[i].rel) & (cap - 1);
    while(t[pos]) pos = (pos + 1) & (cap - 1);
    t[pos] = (uint32_t)(i + 1);
  }
  free(m->tab);
  m->tab = t;
  m->tcap = cap;
  return 0;
}

static file_t *
file_get(model_t *m, const char *rel, uint64_t size) {
  if(!m->tab && rehash(m, 4096)) return NULL;
  size_t pos = hstr(rel) & (m->tcap - 1);
  while(m->tab[pos]) {
    file_t *f = &m->f[m->tab[pos] - 1];
    if(!strcmp(f->rel, rel)) { if(size > f->size) f->size = size; return f; }
    pos = (pos + 1) & (m->tcap - 1);
  }
  if((m->n + 1) * 2 > m->tcap) { if(rehash(m, m->tcap * 2)) return NULL; return file_get(m, rel, size); }
  if(grow((void **)&m->f, &m->cap, m->n + 1, sizeof(*m->f))) return NULL;
  file_t *f = &m->f[m->n];
  memset(f, 0, sizeof(*f));
  f->rel = strdup(rel);
  if(!f->rel) return NULL;
  f->size = size;
  f->ordinal = (uint32_t)m->n;
  m->tab[pos] = (uint32_t)(m->n + 1);
  m->n++;
  return f;
}

static void
model_free(model_t *m) {
  for(size_t i = 0; i < m->n; i++) { free(m->f[i].rel); free(m->f[i].ev); }
  free(m->f);
  free(m->tab);
  free(m->g);
  memset(m, 0, sizeof(*m));
}

/* ------------------------------------------------------------------------------------------------ the index */

typedef struct { char **path; uint64_t *size; uint64_t n; } idx_t;

static void
idx_free(idx_t *x) {
  for(uint64_t i = 0; i < x->n; i++) free(x->path[i]);
  free(x->path);
  free(x->size);
  memset(x, 0, sizeof(*x));
}

static int
read_full(int fd, void *buf, size_t n, size_t *got) {
  size_t have = 0;
  while(have < n) {
    ssize_t r = read(fd, (char *)buf + have, n - have);
    if(r < 0 && errno == EINTR) continue;
    if(r < 0) { *got = have; return errno ? errno : EIO; }
    if(r == 0) break;
    have += (size_t)r;
  }
  *got = have;
  return 0;
}

/* AMPRIDX3: 48 bytes of header (magic, version 3, entry size 24, entry count, path bytes), then 24-byte entries
   (path offset, path length, size, mtime), then the paths. */
static int
idx_load(const char *path, idx_t *x) {
  memset(x, 0, sizeof(*x));
  int fd = open(path, O_RDONLY);
  if(fd < 0) return -1;
  struct stat st;
  if(fstat(fd, &st) != 0 || st.st_size < 48 || (uint64_t)st.st_size > (512ull << 20)) { close(fd); return -1; }
  size_t len = (size_t)st.st_size, got = 0;
  uint8_t *buf = malloc(len);
  if(!buf || read_full(fd, buf, len, &got) != 0 || got != len) { free(buf); close(fd); return -1; }
  close(fd);
  uint32_t version, esize;
  uint64_t count, pbytes;
  memcpy(&version, buf + 8, 4);
  memcpy(&esize, buf + 12, 4);
  memcpy(&count, buf + 16, 8);
  memcpy(&pbytes, buf + 24, 8);
  if(memcmp(buf, "AMPRIDX3", 8) || version != 3 || esize != 24 || count > len / 24 || pbytes > len || 48 + count * 24 + pbytes > len) { free(buf); return -1; }
  x->path = calloc((size_t)count + 1, sizeof(*x->path));
  x->size = calloc((size_t)count + 1, sizeof(*x->size));
  if(!x->path || !x->size) { free(buf); idx_free(x); return -1; }
  const uint8_t *blob = buf + 48 + count * 24;
  for(uint64_t i = 0; i < count; i++) {
    uint32_t at, ln;
    uint64_t sz;
    memcpy(&at, buf + 48 + i * 24, 4);
    memcpy(&ln, buf + 48 + i * 24 + 4, 4);
    memcpy(&sz, buf + 48 + i * 24 + 8, 8);
    if((uint64_t)at + ln >= pbytes) continue;
    const char *p = (const char *)blob + at;
    size_t pl = ln;
    if(pl > 6 && !strncasecmp(p, "/app0/", 6)) { p += 6; pl -= 6; }
    else if(pl > 0 && p[0] == '/') { p++; pl--; }
    x->path[i] = strndup(p, pl);
    x->size[i] = sz;
  }
  x->n = count;
  free(buf);
  return 0;
}

/* ------------------------------------------------------------------------------------------------ AMPRCMD1 */

typedef struct __attribute__((packed)) {
  char     magic[8];
  uint16_t version, header_bytes;
  uint32_t record_bytes;
  uint64_t sequence, utc_ns, monotonic_ns, cookie, source_address, payload_hash;
  uint32_t payload_bytes, source_capacity, command_count, priority, domain, submit_mode, submit_type, flags;
} cmd_hdr;
_Static_assert(sizeof(cmd_hdr) == 96, "AMPRCMD1 header");

typedef struct { uint32_t prio; uint32_t file_id; uint64_t next; int used; } gs_state;

static void
emit(model_t *m, const idx_t *x, int trace, uint32_t file_id, uint64_t off, uint64_t len) {
  if(file_id == 0 || file_id > x->n || !x->path[file_id - 1]) { m->dropped++; return; }
  uint64_t size = x->size[file_id - 1];
  if(len == 0 || off >= size) { m->dropped++; return; }
  if(off + len > size) len = size - off;
  file_t *f = file_get(m, x->path[file_id - 1], size);
  if(!f || grow((void **)&f->ev, &f->cap, f->n + 1, sizeof(*f->ev))) { m->oom++; return; }
  f->ev[f->n].off = off;
  f->ev[f->n].len = (uint32_t)(len > 0xFFFFFFFFu ? 0xFFFFFFFFu : len);
  f->ev[f->n].trace = (uint16_t)trace;
  f->n++;
  if(grow((void **)&m->g, &m->gcap, m->gn + 1, sizeof(*m->g))) { m->oom++; return; }
  m->g[m->gn].file = f->ordinal;
  m->g[m->gn].off = off;
  m->g[m->gn].len = f->ev[f->n - 1].len;
  m->gn++;
}

static gs_state *
gs_find(gs_state *st, int *nst, uint32_t prio, int create) {
  for(int i = 0; i < *nst; i++)
    if(st[i].used && st[i].prio == prio) return &st[i];
  if(!create) return NULL;
  for(int i = 0; i < *nst; i++)
    if(!st[i].used) { st[i].prio = prio; st[i].used = 1; return &st[i]; }
  if(*nst >= 64) return NULL;
  st[*nst].prio = prio;
  st[*nst].used = 1;
  return &st[(*nst)++];
}

/* the length in dwords of a command, from its first dword (the table of parse_ampr_command_log.py) */
static int
cmd_dwords(uint32_t w0) {
  uint32_t b = w0 & 0xFF, b12 = w0 & 0xFFF;
  if((w0 & 0xFFFF000F) == 0x5452000F) return w0 == 0x5452300F ? 1 : (int)((w0 >> 8) & 0xF) + 1;
  if(b12 == 1032 || b12 == 1144) return 5;
  if(b12 == 0x22A) return 3;
  if(b12 == 557) return 3;
  if(b12 == 813) return 4;
  switch(b12) {                                    /* AMM */
    case 0x221: case 0x222: case 0x228: return 3;
    case 0x321: case 0x325: case 0x323: case 0x324: case 0x327: case 0x326: return 4;
    case 0x425: case 0x423: case 0x424: case 0x426: return 5;
    default: break;
  }
  if(w0 == 46 || w0 == 47) return 1;
  if(b == 1 || b == 2) return (int)((w0 >> 8) & 0xF) + 1;
  if(b == 5 || b == 117) return (int)((w0 >> 8) & 3) + 1;
  if(b == 6 || b == 118) return (int)((w0 >> 8) & 0xF) + 1;
  if(b == 40 || b == 43) return (int)((w0 >> 8) & 7) + 1;
  if(b == 41) return (int)((w0 >> 8) & 3) + 1;
  return (int)((w0 >> 8) & 0xF) + 1;
}

static int
load_trace(model_t *m, const char *journal, const char *index, int trace) {
  idx_t x;
  if(idx_load(index, &x) != 0) { PS5TM_WARN("trace_index", "Der Index %s ließ sich nicht lesen.", index); return -1; }
  int fd = open(journal, O_RDONLY);
  if(fd < 0) { idx_free(&x); return -1; }
  gs_state st[64];
  int nst = 0;
  memset(st, 0, sizeof(st));
  uint32_t *payload = NULL;
  size_t pcap = 0;
  uint64_t expect = 1;
  for(;;) {
    cmd_hdr h;
    size_t got = 0;
    if(read_full(fd, &h, sizeof(h), &got) != 0 || got != sizeof(h)) break;
    if(memcmp(h.magic, "AMPRCMD1", 8) || h.version != 1 || h.header_bytes < 96 || h.record_bytes != (uint32_t)h.header_bytes + h.payload_bytes) {
      PS5TM_WARN("trace_corrupt", "Der Mitschnitt %s hat einen beschädigten Eintrag.", journal);
      break;
    }
    if(h.header_bytes > 96 && lseek(fd, h.header_bytes - 96, SEEK_CUR) < 0) break;
    if(h.payload_bytes > (64u << 20)) break;
    if(grow((void **)&payload, &pcap, h.payload_bytes / 4 + 1, 4)) break;
    if(read_full(fd, payload, h.payload_bytes, &got) != 0 || got != h.payload_bytes) break;      /* cut off: stop */
    if(h.sequence != expect) m->gaps++;
    expect = h.sequence + 1;
    m->records++;
    if(h.domain != 1) continue;                    /* APR only */
    size_t nw = h.payload_bytes / 4, i = 0;
    uint32_t cmds = 0;
    while(i < nw) {
      const uint32_t *w = payload + i;
      uint32_t w0 = w[0];
      int dw = cmd_dwords(w0);
      if(dw < 1 || i + (size_t)dw > nw) { m->decode_errors++; break; }
      uint32_t b = w0 & 0xFF, b12 = w0 & 0xFFF;
      if(b == 40 && dw >= 5 && dw <= 6) {                          /* AprReadFile */
        uint64_t len = (uint64_t)w[1] + 1, off = ((w0 >> 12) & 0x3FFFF) | (w[4] & 0xFFFC0000u);
        if(dw == 6) off |= (uint64_t)(w[5] & 0xFF) << 32;
        uint32_t fid = w[2] & 0x7FFFFFFF;
        gs_state *s = gs_find(st, &nst, h.priority, 1);
        if(s) { s->file_id = fid; s->next = off + len; }
        emit(m, &x, trace, fid, off, len);
      } else if(b == 41 && dw >= 2 && dw <= 3) {                   /* AprReadGather */
        uint64_t len = (uint64_t)w[1] + 1, off = (w0 >> 12) & 0x3FFFF;
        if(dw == 3) off |= (uint64_t)(w[2] & 0x3FFFFF) << 18;
        gs_state *s = gs_find(st, &nst, h.priority, 0);
        if(s) { emit(m, &x, trace, s->file_id, off, len); s->next = off + len; }
      } else if(b12 == 0x22A) {                                    /* AprReadScatter */
        uint64_t len = (uint64_t)w[1] + 1;
        gs_state *s = gs_find(st, &nst, h.priority, 0);
        if(s) { emit(m, &x, trace, s->file_id, s->next, len); s->next += len; }
      } else if(b == 43 && dw >= 4 && dw <= 5) {                   /* AprReadGatherScatter */
        uint64_t len = (uint64_t)w[1] + 1, off = ((w0 >> 12) & 0x3FFFF) | (w[3] & 0xFFFC0000u);
        if(dw == 5) off |= (uint64_t)(w[4] & 0xFF) << 32;
        gs_state *s = gs_find(st, &nst, h.priority, 0);
        if(s) { emit(m, &x, trace, s->file_id, off, len); s->next = off + len; }
      } else if(w0 == 47) {                                        /* AprResetGatherScatterState */
        gs_state *s = gs_find(st, &nst, h.priority, 0);
        if(s) s->used = 0;
      }
      i += (size_t)dw;
      cmds++;
    }
    if(h.command_count && cmds != h.command_count) m->decode_errors++;
  }
  free(payload);
  close(fd);
  idx_free(&x);
  m->traces++;
  return 0;
}

/* ------------------------------------------------------------------------------------------------ the metrics */

static int cmp_u32(const void *a, const void *b) { uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b; return x < y ? -1 : x > y; }
static int cmp_u64(const void *a, const void *b) { uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b; return x < y ? -1 : x > y; }

static double
pct_u32(const uint32_t *v, size_t n, double p) {
  if(!n) return 0;
  if(n == 1) return v[0];
  double pos = (double)(n - 1) * p / 100.0;
  size_t lo = (size_t)floor(pos), hi = (size_t)ceil(pos);
  if(lo == hi) return v[lo];
  return v[lo] * (1.0 - (pos - (double)lo)) + v[hi] * (pos - (double)lo);
}

static double
pct_u64(const uint64_t *v, size_t n, double p) {
  if(!n) return 0;
  if(n == 1) return (double)v[0];
  double pos = (double)(n - 1) * p / 100.0;
  size_t lo = (size_t)floor(pos), hi = (size_t)ceil(pos);
  if(lo == hi) return (double)v[lo];
  return (double)v[lo] * (1.0 - (pos - (double)lo)) + (double)v[hi] * (pos - (double)lo);
}

typedef struct { uint64_t a, b; } span_t;
static int cmp_span(const void *x, const void *y) { const span_t *p = x, *q = y; return p->a < q->a ? -1 : p->a > q->a; }

static uint64_t
union_len(span_t *s, size_t n) {
  if(!n) return 0;
  qsort(s, n, sizeof(*s), cmp_span);
  uint64_t tot = 0, a = s[0].a, b = s[0].b;
  for(size_t i = 1; i < n; i++) {
    if(s[i].a <= b) { if(s[i].b > b) b = s[i].b; }
    else { tot += b - a; a = s[i].a; b = s[i].b; }
  }
  return tot + (b - a);
}

static int
classify(const file_t *f) {
  size_t n = f->n;
  double seq = f->exact + f->near;
  if(f->p50 <= 256 && n >= 64 && (f->repeat64 >= 0.10 || seq >= 0.70)) return L_RANDOM;
  if(f->p50 >= 64 * 1024 && seq >= 0.76 && f->random <= 0.20 && f->repeat64 <= 0.18) return L_STREAMING;
  if(f->p90 >= 256 * 1024 && f->large256 >= 0.65 && seq >= 0.86 && f->repeat64 <= 0.12) return L_STREAMING;
  if(f->tiny4 >= 0.50 || f->small16 >= 0.70 || (f->small64 >= 0.82 && f->random >= 0.30) || (f->repeat64 >= 0.28 && f->p50 <= 64 * 1024))
    return L_RANDOM;
  return L_MIXED;
}

/* how well a block size suits the reads of a file under a layout: lower is better */
static double
block_score(const file_t *f, uint32_t B, int layout, double *repeat_out) {
  span_t *sp = malloc((f->n ? f->n : 1) * sizeof(*sp));
  if(!sp) return 1e9;
  uint64_t touched = 0, touches = 0;
  for(size_t i = 0; i < f->n; i++) {
    uint64_t first = f->ev[i].off / B, lastx = (f->ev[i].off + f->ev[i].len + B - 1) / B;
    uint64_t start = first * B, end = lastx * B < f->size ? lastx * B : f->size;
    touched += end > start ? end - start : 0;
    touches += lastx - first;
    sp[i].a = first;
    sp[i].b = lastx;
  }
  uint64_t unique = union_len(sp, f->n);
  free(sp);
  double amp = f->requested ? (double)touched / (double)f->requested : 1.0;
  double avg = f->n ? (double)touches / (double)f->n : 0.0;
  double repeat = touches ? fmax(0.0, 1.0 - (double)unique / (double)touches) : 0.0;
  if(repeat_out) *repeat_out = repeat;
  double meta = ceil((double)f->size / B) * 12.0;
  double la = log2(fmax(1.0, amp)), lo = log2(fmax(1.0, avg)), sl = log2((double)B / 65536.0), s;
  if(layout == L_RANDOM) {
    s = 3.8 * la + 0.12 * lo;
    if(B > 65536) s += 0.55 * log2((double)B / 65536.0);
    if(B < 16384) s += 10.0;
  } else if(layout == L_STREAMING) {
    s = 0.65 * la + 0.95 * lo;
    if(B < 131072) s += 0.35 * log2(131072.0 / B);
    if(B > 524288) s += 0.15 * log2((double)B / 524288.0);
  } else {
    s = 2.15 * la + 0.38 * lo + fabs(sl) * 0.06;
    if(B > 131072) s += 0.28 * log2((double)B / 131072.0);
    if(B > 65536) s += f->random * 0.42 * log2((double)B / 65536.0);
    if(B < 32768) s += 0.20 * log2(32768.0 / B);
  }
  return s + meta / fmax((double)f->size, 1.0) * 3.0;
}

static void
file_metrics(file_t *f) {
  size_t n = f->n;
  uint32_t *lens = malloc((n ? n : 1) * sizeof(*lens));
  if(!lens) return;
  uint64_t req = 0, t4 = 0, s16 = 0, s64 = 0, l256 = 0;
  for(size_t i = 0; i < n; i++) {
    uint32_t L = f->ev[i].len;
    lens[i] = L;
    req += L;
    if(L <= 4096) t4 += L;
    if(L <= 16384) s16 += L;
    if(L <= 65536) s64 += L;
    if(L >= 262144) l256 += L;
  }
  qsort(lens, n, sizeof(*lens), cmp_u32);
  f->p50 = pct_u32(lens, n, 50);
  f->p90 = pct_u32(lens, n, 90);
  free(lens);
  f->requested = req;
  f->tiny4 = req ? (double)t4 / req : 0;
  f->small16 = req ? (double)s16 / req : 0;
  f->small64 = req ? (double)s64 / req : 0;
  f->large256 = req ? (double)l256 / req : 0;
  /* the step from one read of this file to the next within the same session */
  uint64_t tb = 0, ex = 0, nr = 0, rnd = 0;
  for(size_t i = 1; i < n; i++) {
    const ev_t *p = &f->ev[i - 1], *c = &f->ev[i];
    if(p->trace != c->trace) continue;
    uint64_t pend = p->off + p->len, w = c->len;
    tb += w;
    if(c->off == pend) ex += w;
    else if(c->off >= pend && c->off - pend <= (uint64_t)fmax(65536.0, fmin((double)p->len, 524288.0))) nr += w;
    else if(c->off >= p->off && c->off < pend) { /* overlap */ }
    else rnd += w;                                                  /* backwards or far */
  }
  f->exact = tb ? (double)ex / tb : 0;
  f->near = tb ? (double)nr / tb : 0;
  f->random = tb ? (double)rnd / tb : 0;
  double rep = 0;
  (void)block_score(f, 65536, L_MIXED, &rep);
  f->repeat64 = rep;
}

/* what is already known about a path: programs and system folders stay loose, media are stored, the rest is packed */
static const char *const STORE_EXT[] = { ".bik", ".bk2", ".mp4", ".m4v", ".webm", ".m2v", ".mpg", ".mpeg", ".avi", ".wem", ".opus",
                                         ".ogg", ".mp3", ".aac", ".flac", NULL };
static int
known_action(const char *rel) {
  const char *base = strrchr(rel, '/');
  base = base ? base + 1 : rel;
  const char *dot = strrchr(base, '.');
  if(!strcasecmp(base, "eboot.bin") || !strncasecmp(base, "ampr_emu.index", 14) || !strncasecmp(base, "ampr_assets.index", 17) ||
     !strcasecmp(base, "param.sfo") || !strcasecmp(base, "nptitle.dat"))
    return A_LOOSE;
  if(!strncasecmp(rel, "sce_", 4) || !strncasecmp(rel, "fakelib", 7)) return A_LOOSE;
  if(dot && (!strcasecmp(dot, ".prx") || !strcasecmp(dot, ".sprx") || !strcasecmp(dot, ".self") || !strcasecmp(dot, ".elf"))) return A_LOOSE;
  for(int i = 0; dot && STORE_EXT[i]; i++)
    if(!strcasecmp(dot, STORE_EXT[i])) return A_STORE;
  return A_COMPRESS;
}

static void
recommend(model_t *m) {
  size_t k = 0;
  uint32_t *rc = malloc((m->n ? m->n : 1) * sizeof(*rc));
  uint64_t *rq = malloc((m->n ? m->n : 1) * sizeof(*rq));
  if(!rc || !rq) { free(rc); free(rq); return; }
  for(size_t i = 0; i < m->n; i++) { rc[k] = (uint32_t)m->f[i].n; rq[k] = m->f[i].requested; k++; }
  qsort(rc, k, sizeof(*rc), cmp_u32);
  qsort(rq, k, sizeof(*rq), cmp_u64);
  double read_hot = fmax(32.0, floor(pct_u32(rc, k, 90)));
  double byte_hot = fmax(16.0 * MIB, floor(pct_u64(rq, k, 90)));
  free(rc);
  free(rq);
  for(size_t i = 0; i < m->n; i++) {
    file_t *f = &m->f[i];
    if(f->n < 1 || f->requested < 1) continue;
    int layout = classify(f);
    for(int b = 0; b < NBS; b++) f->score[b] = block_score(f, BS[b], layout, NULL);
    int action = known_action(f->rel);
    if(action == A_STORE) layout = L_STREAMING;
    int hot = layout != L_STREAMING && ((f->n >= 16 && f->repeat64 >= 0.22) || (double)f->n >= read_hot ||
                                        (f->n >= 8 && (double)f->requested >= byte_hot));
    int best = -1;
    for(int b = 0; b < NBS; b++) {
      uint32_t B = BS[b];
      if(layout == L_RANDOM && B > 128 * 1024) continue;
      if(layout == L_MIXED && (B < 32 * 1024 || B > 256 * 1024)) continue;
      if(layout == L_STREAMING && B < 64 * 1024) continue;
      if(best < 0) { best = b; continue; }
      double d1 = f->score[b], d0 = f->score[best];
      double t1 = fabs(log2((double)B / 65536.0)), t0 = fabs(log2((double)BS[best] / 65536.0));
      if(d1 < d0 || (d1 == d0 && (t1 < t0 || (t1 == t0 && B < BS[best])))) best = b;
    }
    if(f->p50 <= 256 && f->n >= 64) { best = 0; layout = L_RANDOM; hot = 1; }       /* files of tiny index reads */
    f->action = action;
    f->layout = layout;
    f->block_idx = best < 0 ? 2 : best;
    f->hot = hot;
    f->recommended = 1;
  }
}

/* ------------------------------------------------------------------------------------------------ the cache simulation */

/* simulate_cache() of ampr_pack_profile.py: a byte-weighted LRU, a block is admitted at its second touch (hot blocks at once);
   up to 5 million block touches exactly, otherwise 64 windows spread over the reads, each starting with an empty cache. */
static uint64_t
mix64(uint64_t x) {
  x ^= x >> 33; x *= 0xff51afd7ed558ccdull; x ^= x >> 33; x *= 0xc4ceb9fe1a85ec53ull;
  return x ^ (x >> 33);
}

/* u64 -> u32 map with deletion; keys are stored + 1 (0 = empty, all ones = deleted) */
typedef struct { uint64_t *k; uint32_t *v; size_t cap, used, live; } hmap_t;
#define HM_EMPTY 0ull
#define HM_TOMB UINT64_MAX

static void hm_free(hmap_t *h) { free(h->k); free(h->v); memset(h, 0, sizeof(*h)); }
static int
hm_init(hmap_t *h, size_t cap) {
  memset(h, 0, sizeof(*h));
  h->k = calloc(cap, sizeof(*h->k));
  h->v = malloc(cap * sizeof(*h->v));
  if(!h->k || !h->v) { hm_free(h); return -1; }
  h->cap = cap;
  return 0;
}
static void hm_clear(hmap_t *h) { memset(h->k, 0, h->cap * sizeof(*h->k)); h->used = h->live = 0; }

static size_t
hm_slot(const hmap_t *h, uint64_t key, int *found) {
  uint64_t s = key + 1;
  size_t pos = mix64(s) & (h->cap - 1), tomb = SIZE_MAX;
  for(;;) {                                         /* ends: hm_reserve keeps used below cap */
    uint64_t c = h->k[pos];
    if(c == HM_EMPTY) { *found = 0; return tomb != SIZE_MAX ? tomb : pos; }
    if(c == HM_TOMB) { if(tomb == SIZE_MAX) tomb = pos; }
    else if(c == s) { *found = 1; return pos; }
    pos = (pos + 1) & (h->cap - 1);
  }
}

static int
hm_reserve(hmap_t *h) {
  if((h->used + 1) * 4 <= h->cap * 3) return 0;
  size_t nc = h->cap;
  while((h->live + 1) * 2 > nc) nc *= 2;
  hmap_t n;
  if(hm_init(&n, nc) != 0) return -1;
  for(size_t i = 0; i < h->cap; i++) {
    uint64_t c = h->k[i];
    if(c == HM_EMPTY || c == HM_TOMB) continue;
    int f;
    size_t s = hm_slot(&n, c - 1, &f);
    n.k[s] = c;
    n.v[s] = h->v[i];
    n.used++;
    n.live++;
  }
  hm_free(h);
  *h = n;
  return 0;
}

static int64_t
hm_get(const hmap_t *h, uint64_t key) {
  int f;
  size_t s = hm_slot(h, key, &f);
  return f ? (int64_t)h->v[s] : -1;
}

static int
hm_put(hmap_t *h, uint64_t key, uint32_t val) {
  if(hm_reserve(h) != 0) return -1;
  int f;
  size_t s = hm_slot(h, key, &f);
  if(!f) {
    if(h->k[s] == HM_EMPTY) h->used++;
    h->k[s] = key + 1;
    h->live++;
  }
  h->v[s] = val;
  return 0;
}

static void
hm_del(hmap_t *h, uint64_t key) {
  int f;
  size_t s = hm_slot(h, key, &f);
  if(f) { h->k[s] = HM_TOMB; h->live--; }
}

#define SEEN_MAX 1000000u          /* bounds the set of "seen once" blocks on the console (about 24 MiB) */
typedef struct {
  uint64_t *key;
  uint32_t *size;
  int32_t  *prev, *next;
  size_t    cap, hw;
  int32_t   head, tail, freel;
  uint64_t  bytes, limit;
  hmap_t    map, seen;
  int       oom;
} lru_t;

static int
lru_init(lru_t *c, uint64_t limit) {
  memset(c, 0, sizeof(*c));
  c->limit = limit;
  c->head = c->tail = c->freel = -1;
  if(hm_init(&c->map, 4096) || hm_init(&c->seen, 4096)) { hm_free(&c->map); return -1; }
  return 0;
}

static void lru_free(lru_t *c) { free(c->key); free(c->size); free(c->prev); free(c->next); hm_free(&c->map); hm_free(&c->seen); }

static void
lru_reset(lru_t *c) {
  c->hw = 0;
  c->bytes = 0;
  c->head = c->tail = c->freel = -1;
  hm_clear(&c->map);
  hm_clear(&c->seen);
}

static void
lru_unlink(lru_t *c, int32_t i) {
  if(c->prev[i] >= 0) c->next[c->prev[i]] = c->next[i]; else c->head = c->next[i];
  if(c->next[i] >= 0) c->prev[c->next[i]] = c->prev[i]; else c->tail = c->prev[i];
}

static void
lru_front(lru_t *c, int32_t i) {
  c->prev[i] = -1;
  c->next[i] = c->head;
  if(c->head >= 0) c->prev[c->head] = i;
  c->head = i;
  if(c->tail < 0) c->tail = i;
}

static int32_t
lru_node(lru_t *c) {
  if(c->freel >= 0) { int32_t i = c->freel; c->freel = c->next[i]; return i; }
  if(c->hw == c->cap) {
    size_t nc = c->cap ? c->cap * 2 : 4096;
    if(nc > INT32_MAX) return -1;
    uint64_t *k = realloc(c->key, nc * sizeof(*k));
    if(k) c->key = k;
    uint32_t *s = realloc(c->size, nc * sizeof(*s));
    if(s) c->size = s;
    int32_t *p = realloc(c->prev, nc * sizeof(*p));
    if(p) c->prev = p;
    int32_t *n = realloc(c->next, nc * sizeof(*n));
    if(n) c->next = n;
    if(!k || !s || !p || !n) return -1;
    c->cap = nc;
  }
  return (int32_t)c->hw++;
}

/* the bytes of this touch that came from the cache */
static uint64_t
lru_touch(lru_t *c, uint64_t key, uint32_t raw, uint32_t overlap, int hot) {
  int64_t i = hm_get(&c->map, key);
  if(i >= 0) { lru_unlink(c, (int32_t)i); lru_front(c, (int32_t)i); return overlap; }
  int admit = hot || hm_get(&c->seen, key) >= 0;
  if(!admit) {
    if(c->seen.live >= SEEN_MAX) hm_clear(&c->seen);
    if(hm_put(&c->seen, key, 0) != 0) c->oom = 1;
    return 0;
  }
  hm_del(&c->seen, key);
  if(raw > c->limit) return 0;
  while(c->bytes + raw > c->limit && c->tail >= 0) {
    int32_t v = c->tail;
    lru_unlink(c, v);
    c->bytes -= c->size[v];
    hm_del(&c->map, c->key[v]);
    c->next[v] = c->freel;
    c->freel = v;
  }
  if(c->bytes + raw > c->limit) return 0;
  int32_t n = lru_node(c);
  if(n < 0 || hm_put(&c->map, key, (uint32_t)n) != 0) {
    if(n >= 0) { c->next[n] = c->freel; c->freel = n; }
    c->oom = 1;
    return 0;
  }
  c->key[n] = key;
  c->size[n] = raw;
  lru_front(c, n);
  c->bytes += raw;
  return 0;
}

#define SIM_MAX_TOUCHES 5000000ull
#define SIM_MAX_WINDOWS 64
static const uint64_t CACHE_CAND[] = { 32 * MIB, 64 * MIB, 128 * MIB, 256 * MIB, 512 * MIB };

static int sim_eligible(const file_t *f) { return f->recommended && f->action != A_LOOSE && f->layout != L_STREAMING; }

static uint64_t
choose_decoded(model_t *m, double *best_hit) {
  uint64_t total = 0;
  for(size_t i = 0; i < m->gn; i++) {
    const file_t *f = &m->f[m->g[i].file];
    if(!sim_eligible(f)) continue;
    uint32_t B = BS[f->block_idx];
    total += (m->g[i].off + m->g[i].len - 1) / B - m->g[i].off / B + 1;
  }
  *best_hit = 0;
  if(!total) return 64 * MIB;
  uint64_t ws[SIM_MAX_WINDOWS], we[SIM_MAX_WINDOWS];
  int nw = 0;
  if(total <= SIM_MAX_TOUCHES) { ws[0] = 0; we[0] = total; nw = 1; }
  else {
    uint64_t wc = SIM_MAX_TOUCHES / 4096;
    if(wc > SIM_MAX_WINDOWS) wc = SIM_MAX_WINDOWS;
    if(wc < 1) wc = 1;
    uint64_t budget = SIM_MAX_TOUCHES / wc;
    for(uint64_t w = 0; w < wc; w++) {
      uint64_t s = total * w / wc, e = total * (w + 1) / wc, size = e - s;
      if(!size) continue;
      uint64_t b = budget < size ? budget : size;
      ws[nw] = s + (size - b) / 2;
      we[nw] = ws[nw] + b;
      nw++;
    }
  }
  double hits[5];
  int oom = 0;
  for(int c = 0; c < 5; c++) {
    lru_t L;
    hits[c] = 0;
    if(lru_init(&L, CACHE_CAND[c]) != 0) { oom = 1; continue; }
    uint64_t hit = 0, req = 0, t = 0;
    int w = 0, started = 0;
    for(size_t i = 0; i < m->gn && w < nw; i++) {
      const file_t *f = &m->f[m->g[i].file];
      if(!sim_eligible(f)) continue;
      uint32_t B = BS[f->block_idx];
      uint64_t off = m->g[i].off, end = off + m->g[i].len;
      for(uint64_t b = off / B; b * B < end; b++, t++) {
        while(w < nw && t >= we[w]) { w++; started = 0; }
        if(w >= nw) break;
        if(t < ws[w]) continue;
        if(!started) { lru_reset(&L); started = 1; }
        uint64_t bs = b * B, be = bs + B < f->size ? bs + B : f->size;
        uint64_t os = off > bs ? off : bs, oe = end < be ? end : be;
        uint32_t overlap = (uint32_t)(oe > os ? oe - os : 0);
        req += overlap;
        hit += lru_touch(&L, ((uint64_t)f->ordinal << 40) | b, (uint32_t)(be - bs), overlap, f->hot);
      }
    }
    oom |= L.oom;
    lru_free(&L);
    hits[c] = req ? (double)hit / (double)req : 0;
  }
  if(oom) { m->sim_oom = 1; PS5TM_WARN("trace_sim_oom", "Die Zwischenspeicher-Simulation hatte zu wenig Speicher: die Größe ist ungefähr."); }
  double best = 0;
  for(int c = 0; c < 5; c++) if(hits[c] > best) best = hits[c];
  *best_hit = best;
  if(best < 0.08) return 64 * MIB;
  if(best < 0.15) return 128 * MIB;
  if(best < 0.25) return 256 * MIB;
  double prev = -1;
  for(int c = 0; c < 5; c++) {
    if(CACHE_CAND[c] < 64 * MIB) { prev = hits[c]; continue; }
    double marginal = prev >= 0 ? hits[c] - prev : hits[c];
    if(best - hits[c] <= 0.03 || (hits[c] >= 0.30 && marginal < 0.05)) return CACHE_CAND[c];
    prev = hits[c];
  }
  return CACHE_CAND[4];
}

static void
choose_runtime(model_t *m, uint64_t *decoded, uint64_t *physical, uint32_t *workers, uint32_t *reserve, double *hit) {
  *decoded = choose_decoded(m, hit);
  /* the physical page cache: the pressure of small random reuse, weighted by the bytes requested */
  double wsum = 0, wr = 0, ws = 0, wrand = 0, req_total = 0;
  for(size_t i = 0; i < m->n; i++) {
    file_t *f = &m->f[i];
    if(!f->recommended || f->action == A_LOOSE || f->layout == L_STREAMING) continue;
    double w = (double)f->requested;
    wsum += w;
    wr += w * f->repeat64;
    ws += w * f->small64;
    wrand += w * f->random;
    req_total += w;
  }
  if(wsum <= 0) *physical = 8 * MIB;
  else {
    double pressure = wr / wsum + 0.5 * (ws / wsum) * (wrand / wsum);
    if(req_total >= 16.0 * 1024 * MIB && pressure >= 0.20) *physical = 64 * MIB;
    else if(req_total >= 2.0 * 1024 * MIB || pressure >= 0.10) *physical = 32 * MIB;
    else *physical = 16 * MIB;
  }
  /* workers: how often two reads in a row are of different files */
  uint64_t n = 0, switches = 0, small = 0, tiny = 0, bulk = 0;
  int64_t prev = -1;
  uint64_t *bytes = calloc(m->n ? m->n : 1, sizeof(*bytes));
  uint64_t tot = 0, files = 0;
  for(size_t i = 0; i < m->gn; i++) {
    file_t *f = &m->f[m->g[i].file];
    if(!f->recommended || f->action == A_LOOSE) continue;
    if(prev >= 0 && prev != (int64_t)m->g[i].file) switches++;
    prev = (int64_t)m->g[i].file;
    n++;
    uint32_t L = m->g[i].len;
    if(L <= 65536) small++;
    if(L <= 16384) tiny++;
    if(L >= 262144) bulk++;
    if(bytes) { if(!bytes[m->g[i].file]) files++; bytes[m->g[i].file] += L; }
    tot += L;
  }
  uint64_t dom = 0;
  for(size_t i = 0; bytes && i < m->n; i++) if(bytes[i] > dom) dom = bytes[i];
  free(bytes);
  if(!n) *workers = 2;
  else {
    double sw = n > 1 ? (double)switches / (double)(n - 1) : 0, dshare = tot ? (double)dom / (double)tot : 0;
    if(files >= 1000 && sw >= 0.70) *workers = 8;
    else if(files >= 128 && sw >= 0.50) *workers = 6;
    else if(dshare >= 0.85 && files <= 8) *workers = 2;
    else *workers = 4;
  }
  if(*workers <= 1 || !n) *reserve = 0;
  else {
    double s = (double)small / n, t = (double)tiny / n, b = (double)bulk / n;
    *reserve = (s >= 0.45 || t >= 0.20 || (s >= 0.25 && b >= 0.20)) ? 1 : 0;
  }
}

/* ------------------------------------------------------------------------------------------------ the profile */

static void
toml_str(FILE *f, const char *s) {
  fputc('"', f);
  for(; *s; s++) {
    if(*s == '"' || *s == '\\') fputc('\\', f);
    if((unsigned char)*s < 0x20) continue;
    fputc(*s, f);
  }
  fputc('"', f);
}

typedef struct { int action, layout, block_idx, hot; const char *rel; uint64_t requested; } rec_t;
#define TOML_PATH_BUDGET (3u << 20)           /* the loader reads a profile of at most 4 MiB */

static int
cmp_req_desc(const void *a, const void *b) {
  const rec_t *x = a, *y = b;
  return x->requested < y->requested ? 1 : x->requested > y->requested ? -1 : strcmp(x->rel, y->rel);
}

static int
cmp_rec(const void *a, const void *b) {
  const rec_t *x = a, *y = b;
  if(x->action != y->action) return x->action - y->action;
  if(x->layout != y->layout) return x->layout - y->layout;
  if(x->block_idx != y->block_idx) return x->block_idx - y->block_idx;
  if(x->hot != y->hot) return x->hot - y->hot;
  return strcmp(x->rel, y->rel);
}

static const char *const STANDARD_LOOSE[] = { "sce_module/**", "system/**", "mods/**", "save/**", "**/*.prx", "**/*.sprx", "eboot.bin",
  "ampr_emu.index", "ampr_assets.index", "ampr_assets.index.crc", "ampr_assets.index.runtime", "ampr_assets-*.pak", NULL };

static int
write_toml(model_t *m, const char *out, const char *title_id, uint64_t dec, uint64_t phys, uint32_t workers, uint32_t reserve,
           size_t *nrules, size_t *nfiles, size_t *dropped_out) {
  rec_t *r = malloc((m->n ? m->n : 1) * sizeof(*r));
  if(!r) return -1;
  size_t k = 0;
  for(size_t i = 0; i < m->n; i++) {
    file_t *f = &m->f[i];
    if(!f->recommended || f->action == A_LOOSE) continue;
    if(!strchr(f->rel, '/') || strpbrk(f->rel, "\"\\]#*?[") || strlen(f->rel) > 400) continue;     /* a path that would read as a pattern */
    r[k].action = f->action;
    r[k].layout = f->layout;
    r[k].block_idx = f->block_idx;
    r[k].hot = f->hot;
    r[k].rel = f->rel;
    r[k].requested = f->requested;
    k++;
  }
  size_t path_bytes = 0, dropped = 0;
  for(size_t i = 0; i < k; i++) path_bytes += strlen(r[i].rel) + 6;
  if(path_bytes > TOML_PATH_BUDGET) {              /* leave out the files read least */
    qsort(r, k, sizeof(*r), cmp_req_desc);
    size_t keep = 0, b = 0;
    while(keep < k && b + strlen(r[keep].rel) + 6 <= TOML_PATH_BUDGET) b += strlen(r[keep++].rel) + 6;
    dropped = k - keep;
    k = keep;
  }
  qsort(r, k, sizeof(*r), cmp_rec);
  char tmp[PATH_MAX + 16];
  snprintf(tmp, sizeof(tmp), "%s.ps5cc-teil", out);
  FILE *f = fopen(tmp, "w");
  if(!f) { free(r); return -1; }
  fprintf(f, "# Erzeugt von PS5 Cooling & System Center - Pro aus %d Mitschnitt(en) von %s, %zu beobachtete Dateien.\n", m->traces, title_id, k);
  fprintf(f, "# Aus den Lesezugriffen, die die Debug-APR-EMU aufgezeichnet hat (Port von ampr_pack_profile.py, drakmor/ampr_emu).\n");
  fprintf(f, "# WICHTIG: Ein Mitschnitt zeigt nur, was beim Spielen gelesen wurde. Nicht beobachtete Dateien bleiben lose.\n");
  fprintf(f, "# Wer mehr packen will (andere Level, Sprachen, DLC), ergänzt Dateien oder Ordner mit einer eigenen Regel.\n\n");
  fprintf(f, "[pack]\ndefault_action = \"loose\"\ndefault_block_size = \"64KiB\"\n\n");
  fprintf(f, "[runtime]\ndecoded_cache_bytes = %llu\nphysical_cache_bytes = %llu\nworkers = %u\nlatency_reserve_workers = %u\n",
          (unsigned long long)dec, (unsigned long long)phys, workers, reserve);
  static const char *const LAY[] = { "mixed", "random", "streaming" };
  static const char *const ACT[] = { "compress", "store", "loose" };
  size_t rules = 0;
  for(size_t i = 0; i < k;) {
    size_t j = i, bytes = 0;
    while(j < k && j - i < 256 && bytes < 48 * 1024 && r[j].action == r[i].action && r[j].layout == r[i].layout &&
          r[j].block_idx == r[i].block_idx && r[j].hot == r[i].hot)
      bytes += strlen(r[j++].rel) + 6;
    fprintf(f, "\n[[rule]]\naction = \"%s\"\n", ACT[r[i].action]);
    if(j - i == 1) { fputs("include = [", f); toml_str(f, r[i].rel); fputs("]\n", f); }
    else {
      fputs("include = [\n", f);
      for(size_t x = i; x < j; x++) { fputs("  ", f); toml_str(f, r[x].rel); fputs(",\n", f); }
      fputs("]\n", f);
    }
    if(BS[r[i].block_idx] % (1024 * 1024) == 0) fprintf(f, "block_size = \"%uMiB\"\n", BS[r[i].block_idx] / (1024 * 1024));   /* the literal of the Python tool */
    else fprintf(f, "block_size = \"%uKiB\"\n", BS[r[i].block_idx] / 1024);
    fprintf(f, "layout = \"%s\"\n", LAY[r[i].layout]);
    if(r[i].hot) {
      fputs("hot = true\n", f);
      if(BS[r[i].block_idx] <= 32 * 1024)
        fputs("min_savings_bytes = 16\nmin_savings_ratio = 0.0025\nio_neutral_min_savings_bytes = \"2KiB\"\nio_neutral_min_savings_ratio = 0.125\n", f);
    }
    if(r[i].layout == L_STREAMING) fputs("streaming = true\n", f);
    rules++;
    i = j;
  }
  fputs("\n# Sicherheitsausnahmen. Die letzte passende Regel gewinnt.\n[[rule]]\naction = \"loose\"\ninclude = [\n", f);
  for(int i = 0; STANDARD_LOOSE[i]; i++) fprintf(f, "  \"%s\",\n", STANDARD_LOOSE[i]);
  fputs("]\n", f);
  free(r);
  int rc = fflush(f) == 0 && fsync(fileno(f)) == 0 ? 0 : -1;
  if(fclose(f) != 0) rc = -1;
  if(rc == 0 && rename(tmp, out) != 0) rc = -1;
  if(rc != 0) unlink(tmp);
  *nrules = rules;
  *nfiles = k;
  *dropped_out = dropped;
  return rc;
}

/* The core, for the console and for the host test: (journal, index) pairs in, a TOML profile out. 0 or -1; summary says what. */
int
ps5tm_trace_build_profile(const char *const *journals, const char *const *indexes, int pairs, const char *out_toml, const char *title_id,
                          char *summary, size_t cap, int *out_files, int *out_traces) {
  model_t m;
  memset(&m, 0, sizeof(m));
  for(int i = 0; i < pairs; i++) load_trace(&m, journals[i], indexes[i], i);
  if(m.gn == 0) {
    snprintf(summary, cap, "In %d Mitschnitt(en) (%llu Einträge) steht kein gelesenes Stück einer Datei. War die Debug-APR-EMU aktiv und lief das Spiel?",
             pairs, (unsigned long long)m.records);
    model_free(&m);
    return -1;
  }
  for(size_t i = 0; i < m.n; i++) file_metrics(&m.f[i]);
  recommend(&m);
  uint64_t dec, phys;
  uint32_t workers, reserve;
  double hit;
  choose_runtime(&m, &dec, &phys, &workers, &reserve, &hit);
  size_t rules = 0, files = 0, dropped = 0;
  int rc = write_toml(&m, out_toml, title_id, dec, phys, workers, reserve, &rules, &files, &dropped);
  size_t cnt[3] = { 0, 0, 0 }, hot = 0;
  for(size_t i = 0; i < m.n; i++)
    if(m.f[i].recommended && m.f[i].action != A_LOOSE) { cnt[m.f[i].layout]++; hot += (size_t)m.f[i].hot; }
  snprintf(summary, cap, "%d Sitzung(en), %llu Lesezugriffe, %zu Dateien (%zu gemischt, %zu zufällig, %zu streamend, %zu heiß), Zwischenspeicher %llu + %llu MiB, %u Arbeiter%s%s",
           m.traces, (unsigned long long)m.gn, files, cnt[0], cnt[1], cnt[2], hot, (unsigned long long)(dec / MIB), (unsigned long long)(phys / MIB), workers,
           dropped ? ". Die am wenigsten gelesenen Dateien blieben draußen, damit das Profil klein bleibt" : "",
           m.decode_errors || m.oom || m.sim_oom ? ". Einige Einträge ließen sich nicht lesen oder der Speicher reichte nicht ganz" : "");
  if(out_files) *out_files = (int)files;
  if(out_traces) *out_traces = m.traces;
  model_free(&m);
  return rc;
}

/* ------------------------------------------------------------------------------------------------ files and folders */

static int
is_dir(const char *p) {
  struct stat st;
  return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

static int
is_file(const char *p) {
  struct stat st;
  return stat(p, &st) == 0 && S_ISREG(st.st_mode);
}

static int
mkdir_p(const char *path) {
  char tmp[PATH_MAX];
  snprintf(tmp, sizeof(tmp), "%s", path);
  for(char *p = tmp + 1; *p; p++) {
    if(*p != '/') continue;
    *p = 0;
    if(mkdir(tmp, 0777) != 0 && errno != EEXIST) return errno ? errno : EIO;
    *p = '/';
  }
  if(mkdir(tmp, 0777) != 0 && errno != EEXIST) return errno ? errno : EIO;
  return 0;
}

/* a file of fakelib2 or a trace is flat: remove the files of a folder, then the folder (never deeper) */
static void
rm_flat(const char *dir) {
  DIR *d = opendir(dir);
  if(d) {
    struct dirent *e;
    while((e = readdir(d)) != NULL) {
      if(!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
      char p[PATH_MAX];
      if(snprintf(p, sizeof(p), "%s/%s", dir, e->d_name) >= (int)sizeof(p)) continue;
      struct stat st;
      if(lstat(p, &st) == 0 && S_ISREG(st.st_mode)) unlink(p);
    }
    closedir(d);
  }
  rmdir(dir);
}

/* the whole file copied: a temporary name, fsync, a check of the size, rename */
static int
copy_file(const char *src, const char *dst, mode_t mode) {
  int in = open(src, O_RDONLY);
  if(in < 0) return errno ? errno : EIO;
  char tmp[PATH_MAX + 16];
  snprintf(tmp, sizeof(tmp), "%s.ps5cc-teil", dst);
  int out = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, mode);
  if(out < 0) { int e = errno ? errno : EIO; close(in); return e; }
  uint8_t *buf = malloc(1 << 20);
  if(!buf) { close(in); close(out); unlink(tmp); return ENOMEM; }
  int rc = 0;
  uint64_t total = 0;
  for(;;) {
    ssize_t r = read(in, buf, 1 << 20);
    if(r < 0 && errno == EINTR) continue;
    if(r < 0) { rc = errno ? errno : EIO; break; }
    if(r == 0) break;
    size_t off = 0;
    while(off < (size_t)r) {
      ssize_t w = write(out, buf + off, (size_t)r - off);
      if(w < 0 && errno == EINTR) continue;
      if(w <= 0) { rc = w < 0 && errno ? errno : EIO; break; }
      off += (size_t)w;
    }
    if(rc) break;
    total += (uint64_t)r;
  }
  free(buf);
  close(in);
  if(!rc && fsync(out) != 0) rc = errno ? errno : EIO;
  if(close(out) != 0 && !rc) rc = errno ? errno : EIO;
  struct stat st;
  if(!rc && (stat(tmp, &st) != 0 || (uint64_t)st.st_size != total)) rc = EIO;
  if(!rc && rename(tmp, dst) != 0) rc = errno ? errno : EIO;
  if(rc) unlink(tmp);
  return rc;
}

static int
write_small(const char *path, const char *text) {
  return ps5tm_put_lib_file(path, (const unsigned char *)text, strlen(text));
}

/* ------------------------------------------------------------------------------------------------ the console side */

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static struct {
  int      busy;                    /* a collect or generate is running */
  char     id[16];                  /* the game of the last action */
  char     phase[80];
  char     msg[500];
  int      msg_err;
} g_tr;

static void
traces_dir(const char *id, char *out, size_t cap) {
  snprintf(out, cap, "%s/%s", TR_DIR, id);
}

static int
marker_present(const char *game) {
  char p[PATH_MAX];
  snprintf(p, sizeof(p), "%s/fakelib2/" TR_MARKER, game);
  return is_file(p);
}

/* the names of the collected sessions (folders with a journal and an index), sorted */
static int
session_names(const char *id, char names[][16], int max) {
  char dir[PATH_MAX];
  traces_dir(id, dir, sizeof(dir));
  DIR *d = opendir(dir);
  if(!d) return 0;
  int n = 0;
  struct dirent *e;
  while((e = readdir(d)) != NULL && n < max) {
    if(e->d_name[0] == '.' || strlen(e->d_name) >= 16) continue;
    char j[PATH_MAX + 64], x[PATH_MAX + 64];
    snprintf(j, sizeof(j), "%s/%s/ampr_commands.bin", dir, e->d_name);
    snprintf(x, sizeof(x), "%s/%s/ampr_emu.index", dir, e->d_name);
    if(is_file(j) && is_file(x)) snprintf(names[n++], 16, "%s", e->d_name);
  }
  closedir(d);
  for(int i = 1; i < n; i++)                                   /* a few: a plain insertion sort */
    for(int k = i; k > 0 && strcmp(names[k - 1], names[k]) > 0; k--) {
      char t[16];
      memcpy(t, names[k], 16); memcpy(names[k], names[k - 1], 16); memcpy(names[k - 1], t, 16);
    }
  return n;
}

/* a session is told by its first record: the emulator empties the journal at every launch */
static int
journal_session(const char *journal, char *out, size_t cap) {
  int fd = open(journal, O_RDONLY);
  if(fd < 0) return -1;
  cmd_hdr h;
  size_t got = 0;
  int ok = read_full(fd, &h, sizeof(h), &got) == 0 && got == sizeof(h) && !memcmp(h.magic, "AMPRCMD1", 8);
  close(fd);
  if(!ok) return -1;
  snprintf(out, cap, "%llx-%llx-%llx", (unsigned long long)h.utc_ns, (unsigned long long)h.monotonic_ns, (unsigned long long)h.sequence);
  return 0;
}

static void
profile_path(const char *id, char *out, size_t cap) {
  snprintf(out, cap, "%s/%s-mitschnitt.toml", TR_PROFILE_DIR, id);
}

cJSON *
ps5tm_trace_info(const char *id, int *http) {
  *http = 200;
  ps5tm_libdel_t li;
  if(!id || !id[0] || ps5tm_library_delete_info(id, &li) != 0) { *http = 404; return NULL; }
  cJSON *o = cJSON_CreateObject();
  cJSON_AddBoolToObject(o, "ok", 1);
  cJSON_AddStringToObject(o, "id", id);
  cJSON_AddStringToObject(o, "name", li.name);
  const char *why = NULL;
  char buf[300];
  if(strcmp(li.format, "folder") || li.path[0] != '/') why = "Nur für Spiele, die als Ordner vorliegen. Ein Abbild lässt sich vorher mit „In einen Ordner entpacken“ in einen Ordner bringen.";
  else if(ps5tm_lz4_packed_dir(li.path)) why = "Das Spiel liegt in LZ4-Paketen. Zuerst „LZ4 entpacken“.";
  else if(ps5tm_lz4_ampr_import(li.path) == 0) why = "Dieses Spiel lädt libSceAmpr nicht selbst: Ein Mitschnitt bliebe leer.";
  else {
    char ip[PATH_MAX];
    snprintf(ip, sizeof(ip), "%s/ampr_emu.index", li.path);
    if(!is_file(ip)) why = "Der AMPR-Index des Spiels fehlt. Oben bei „AMPR-Index“ zuerst bauen: Der Mitschnitt gehört zu genau diesem Index.";
  }
  cJSON_AddBoolToObject(o, "can", why == NULL);
  if(why) cJSON_AddStringToObject(o, "why", why);
  if(why && strcmp(li.format, "folder")) return o;
  int enabled = marker_present(li.path);
  cJSON_AddBoolToObject(o, "enabled", enabled);
  char names[64][16];
  int ns = session_names(id, names, 64);
  cJSON_AddNumberToObject(o, "sessions", ns);
  /* a journal that is not yet collected: its first record is not that of a collected session */
  int fresh = 0;
  char journal[PATH_MAX];
  snprintf(journal, sizeof(journal), "%s/ampr_commands.bin", li.path);
  struct stat st;
  if(enabled && stat(journal, &st) == 0 && st.st_size >= 96) {
    char sess[80], tdir[PATH_MAX], lastp[PATH_MAX + 8];
    traces_dir(id, tdir, sizeof(tdir));
    snprintf(lastp, sizeof(lastp), "%s/last", tdir);
    fresh = 1;
    if(journal_session(journal, sess, sizeof(sess)) == 0) {
      FILE *lf = fopen(lastp, "r");
      if(lf) {
        char osess[80] = "";
        long long osize = -1;
        if(fscanf(lf, "%79s %lld", osess, &osize) == 2 && !strcmp(osess, sess) && osize == (long long)st.st_size) fresh = 0;
        fclose(lf);
      }
    }
  }
  cJSON_AddBoolToObject(o, "journal_new", fresh);
  cJSON_AddBoolToObject(o, "game_running", ps5tm_procmgr_title_running(id) == 1);
  char pp[PATH_MAX];
  profile_path(id, pp, sizeof(pp));
  if(is_file(pp)) { snprintf(buf, sizeof(buf), "%s-mitschnitt.toml", id); cJSON_AddStringToObject(o, "profile", buf); }
  pthread_mutex_lock(&g_lock);
  cJSON_AddBoolToObject(o, "busy", g_tr.busy && !strcmp(g_tr.id, id));
  if(g_tr.busy && !strcmp(g_tr.id, id)) cJSON_AddStringToObject(o, "phase", g_tr.phase);
  if(!g_tr.busy && g_tr.msg[0] && !strcmp(g_tr.id, id)) {
    cJSON_AddStringToObject(o, "msg", g_tr.msg);
    cJSON_AddBoolToObject(o, "msg_err", g_tr.msg_err);
  }
  pthread_mutex_unlock(&g_lock);
  return o;
}

static void
set_result(const char *id, int err, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
static void
set_result(const char *id, int err, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  pthread_mutex_lock(&g_lock);
  snprintf(g_tr.id, sizeof(g_tr.id), "%s", id);
  vsnprintf(g_tr.msg, sizeof(g_tr.msg), fmt, ap);
  g_tr.msg_err = err;
  g_tr.busy = 0;
  pthread_mutex_unlock(&g_lock);
  va_end(ap);
}

typedef struct { char id[16]; char game[PATH_MAX]; int generate; } tr_job_t;

static void
run_collect(const char *id, const char *game) {
  char journal[PATH_MAX], index[PATH_MAX], dir[PATH_MAX];
  snprintf(journal, sizeof(journal), "%s/ampr_commands.bin", game);
  snprintf(index, sizeof(index), "%s/ampr_emu.index", game);
  struct stat st;
  if(stat(journal, &st) != 0 || st.st_size < 96) { set_result(id, 1, "Es gibt noch keinen Mitschnitt: Starte das Spiel mit eingeschaltetem Mitschnitt, spiele ein Stück und beende es."); return; }
  if(!is_file(index)) { set_result(id, 1, "Neben dem Mitschnitt fehlt die Datei ampr_emu.index."); return; }
  if(time(NULL) - st.st_mtime < 5) { set_result(id, 1, "Das Spiel schreibt noch am Mitschnitt: Beende es zuerst."); return; }
  char session[80];
  if(journal_session(journal, session, sizeof(session)) != 0) { set_result(id, 1, "Die Datei ist kein Mitschnitt (AMPRCMD1)."); return; }
  traces_dir(id, dir, sizeof(dir));
  if(mkdir_p(dir) != 0) { set_result(id, 1, "Der Ordner %s lässt sich nicht anlegen.", dir); return; }
  /* "last" = "<session> <size> <folder>": the same session collected again (copied while the game still ran) replaces its folder */
  char lastp[PATH_MAX + 8], osess[80] = "", oname[16] = "";
  long long osize = -1;
  snprintf(lastp, sizeof(lastp), "%s/last", dir);
  FILE *lf = fopen(lastp, "r");
  if(lf) {
    if(fscanf(lf, "%79s %lld %15s", osess, &osize, oname) != 3) osess[0] = 0;
    fclose(lf);
  }
  int same = osess[0] && !strcmp(osess, session) && !strchr(oname, '/') && oname[0] && oname[0] != '.';
  if(same && osize == (long long)st.st_size) { set_result(id, 0, "Diese Sitzung ist schon eingesammelt."); return; }
  char names[64][16], name[16], sub[PATH_MAX + 32], dst[PATH_MAX + 64];
  if(same) snprintf(name, sizeof(name), "%s", oname);
  else {
    int ns = session_names(id, names, 64);
    for(int n = ns + 1;; n++) {
      snprintf(name, sizeof(name), "%03d", n);
      snprintf(sub, sizeof(sub), "%s/%s", dir, name);
      if(!is_dir(sub)) break;
    }
  }
  snprintf(sub, sizeof(sub), "%s/%s", dir, name);
  if(mkdir_p(sub) != 0) { set_result(id, 1, "Der Ordner %s lässt sich nicht anlegen.", sub); return; }
  snprintf(dst, sizeof(dst), "%s/ampr_commands.bin", sub);
  int rc = copy_file(journal, dst, 0666);
  if(!rc) { snprintf(dst, sizeof(dst), "%s/ampr_emu.index", sub); rc = copy_file(index, dst, 0666); }
  if(rc) {
    set_result(id, 1, "Das Kopieren ging schief: %s", ps5tm_io_strerror(rc));
    snprintf(dst, sizeof(dst), "%s/ampr_commands.bin", sub); unlink(dst);
    snprintf(dst, sizeof(dst), "%s/ampr_emu.index", sub); unlink(dst);
    rmdir(sub);
    return;
  }
  char stamp[160];
  snprintf(stamp, sizeof(stamp), "%s %lld %s\n", session, (long long)st.st_size, name);
  ps5tm_put_lib_file(lastp, (const unsigned char *)stamp, strlen(stamp));
  if(same) PS5TM_INFO("trace_collect", "Mitschnitt von %s aktualisiert: %s (%.1f MB).", id, sub, (double)st.st_size / 1e6);
  else PS5TM_INFO("trace_collect", "Mitschnitt von %s eingesammelt: %s (%.1f MB).", id, sub, (double)st.st_size / 1e6);
  set_result(id, 0, "Sitzung %s eingesammelt (%.1f MB).", name, (double)st.st_size / 1e6);
}

static void
run_generate(const char *id) {
  char names[64][16];
  int ns = session_names(id, names, 64);
  if(ns == 0) { set_result(id, 1, "Es ist noch keine Sitzung eingesammelt."); return; }
  char dir[PATH_MAX];
  traces_dir(id, dir, sizeof(dir));
  char (*jp)[PATH_MAX + 64] = malloc((size_t)ns * sizeof(*jp));
  char (*ip)[PATH_MAX + 64] = malloc((size_t)ns * sizeof(*ip));
  const char **jv = malloc((size_t)ns * sizeof(*jv));
  const char **iv = malloc((size_t)ns * sizeof(*iv));
  if(!jp || !ip || !jv || !iv) { free(jp); free(ip); free(jv); free(iv); set_result(id, 1, "Kein Speicher."); return; }
  for(int i = 0; i < ns; i++) {
    snprintf(jp[i], sizeof(jp[i]), "%s/%s/ampr_commands.bin", dir, names[i]);
    snprintf(ip[i], sizeof(ip[i]), "%s/%s/ampr_emu.index", dir, names[i]);
    jv[i] = jp[i];
    iv[i] = ip[i];
  }
  mkdir_p(TR_PROFILE_DIR);
  char out[PATH_MAX], summary[400];
  profile_path(id, out, sizeof(out));
  int nfiles = 0, ntraces = 0;
  int rc = ps5tm_trace_build_profile(jv, iv, ns, out, id, summary, sizeof(summary), &nfiles, &ntraces);
  free(jp); free(ip); free(jv); free(iv);
  if(rc != 0) { set_result(id, 1, "%s", summary); return; }
  PS5TM_INFO("trace_profile", "Profil für %s: %s.", id, summary);
  set_result(id, 0, "Profil %s-mitschnitt.toml erzeugt (%d Dateien aus %d Sitzung(en)). Beim Packen unter „Eigenes Profil“ wählen.", id, nfiles, ntraces);
}

static void *
tr_thread(void *arg) {
  tr_job_t *j = arg;
  if(j->generate) run_generate(j->id); else run_collect(j->id, j->game);
  free(j);
  return NULL;
}

/* "start", "stop" at once; "collect", "generate" in a thread (the journal can be big). The HTTP status; msg says what happened. */
int
ps5tm_trace_action(const char *id, const char *action, char *msg, size_t n) {
  msg[0] = 0;
  ps5tm_libdel_t li;
  if(!id || !id[0] || ps5tm_library_delete_info(id, &li) != 0) { snprintf(msg, n, "Das Spiel ist nicht in der Liste."); return 404; }
  if(strcmp(li.format, "folder") || li.path[0] != '/') { snprintf(msg, n, "Nur für Spiele, die als Ordner vorliegen."); return 409; }
  if(!action) action = "";
  pthread_mutex_lock(&g_lock);
  int busy = g_tr.busy;
  pthread_mutex_unlock(&g_lock);
  if(busy) { snprintf(msg, n, "Es läuft schon ein Vorgang des Mitschnitts."); return 409; }
  char dir[PATH_MAX], dst[PATH_MAX + 32];
  snprintf(dir, sizeof(dir), "%s/fakelib2", li.path);
  if(!strcmp(action, "start")) {
    if(ps5tm_procmgr_title_running(id) == 1) { snprintf(msg, n, "Das Spiel läuft gerade. Bitte zuerst beenden."); return 409; }
    if(ps5tm_lz4_packed_dir(li.path)) { snprintf(msg, n, "Das Spiel liegt in LZ4-Paketen. Zuerst „LZ4 entpacken“."); return 409; }
    if(ps5tm_lz4_ampr_import(li.path) == 0) { snprintf(msg, n, "Dieses Spiel lädt libSceAmpr nicht selbst: Ein Mitschnitt bliebe leer."); return 409; }
    snprintf(dst, sizeof(dst), "%s/ampr_emu.index", li.path);
    if(!is_file(dst)) { snprintf(msg, n, "Der AMPR-Index des Spiels fehlt. Zuerst bauen."); return 409; }
    if(is_dir(dir) && !marker_present(li.path)) { snprintf(msg, n, "Das Spiel hat schon ein eigenes fakelib2 (Backport): Ein Mitschnitt ist damit nicht möglich."); return 409; }
    const ps5tm_emu_blob_t *b = NULL;
    for(unsigned i = 0; i < ps5tm_emu_blob_count; i++)
      if(!strcmp(ps5tm_emu_blobs[i].name, TR_DEBUG_EMU)) b = &ps5tm_emu_blobs[i];
    if(!b) { snprintf(msg, n, "Die Debug-APR-EMU fehlt in dieser App."); return 500; }
    uint8_t *data = NULL;
    size_t len = 0;
    if(ps5tm_blob_unpack(b, &data, &len) != 0) { snprintf(msg, n, "Die Debug-APR-EMU ließ sich nicht entpacken."); return 500; }
    int rc = mkdir_p(dir);
    if(rc) { free(data); snprintf(msg, n, "Der Ordner fakelib2 lässt sich nicht anlegen: %s", ps5tm_io_strerror(rc)); return 500; }
    /* ShadowMountPlus mounts fakelib2 instead of fakelib: the other libraries of the game go along */
    char src_dir[PATH_MAX];
    snprintf(src_dir, sizeof(src_dir), "%s/fakelib", li.path);
    DIR *d = opendir(src_dir);
    if(d) {
      struct dirent *e;
      while((e = readdir(d)) != NULL) {
        if(e->d_name[0] == '.' || !strcasecmp(e->d_name, "libSceAmpr.sprx")) continue;
        char s[PATH_MAX];
        if(snprintf(s, sizeof(s), "%s/%s", src_dir, e->d_name) >= (int)sizeof(s)) continue;
        if(snprintf(dst, sizeof(dst), "%s/%s", dir, e->d_name) >= (int)sizeof(dst)) continue;
        if(is_file(s) && copy_file(s, dst, 0755) != 0) PS5TM_WARN("trace_copy", "%s ließ sich nicht nach fakelib2 kopieren.", s);
      }
      closedir(d);
    }
    snprintf(dst, sizeof(dst), "%s/libSceAmpr.sprx", dir);
    rc = ps5tm_put_lib_file(dst, data, len);
    free(data);
    if(!rc) { snprintf(dst, sizeof(dst), "%s/" TR_MARKER, dir); rc = write_small(dst, TR_MARKER_TEXT); }
    if(rc) { rm_flat(dir); snprintf(msg, n, "Die Debug-APR-EMU ließ sich nicht schreiben: %s", ps5tm_io_strerror(rc)); return 500; }
    PS5TM_INFO("trace_start", "Mitschnitt von %s eingeschaltet (Debug-APR-EMU in fakelib2).", id);
    snprintf(msg, n, "Mitschnitt gestartet: Die Debug-APR-EMU liegt in fakelib2. Starte das Spiel und spiele ein Stück.");
    set_result(id, 0, "%s", msg);
    return 200;
  }
  if(!strcmp(action, "stop")) {
    if(ps5tm_procmgr_title_running(id) == 1) { snprintf(msg, n, "Das Spiel läuft gerade. Bitte zuerst beenden."); return 409; }
    if(!is_dir(dir)) { snprintf(msg, n, "Der Mitschnitt ist nicht eingeschaltet."); return 409; }
    if(!marker_present(li.path)) { snprintf(msg, n, "Dieses fakelib2 stammt nicht von der App und bleibt unberührt."); return 409; }
    rm_flat(dir);
    PS5TM_INFO("trace_stop", "Mitschnitt von %s ausgeschaltet.", id);
    snprintf(msg, n, "Mitschnitt beendet: fakelib2 ist entfernt.");
    set_result(id, 0, "%s", msg);
    return 200;
  }
  if(!strcmp(action, "collect") || !strcmp(action, "generate")) {
    int gen = !strcmp(action, "generate");
    tr_job_t *j = calloc(1, sizeof(*j));
    if(!j) { snprintf(msg, n, "Kein Speicher."); return 500; }
    snprintf(j->id, sizeof(j->id), "%s", id);
    snprintf(j->game, sizeof(j->game), "%s", li.path);
    j->generate = gen;
    pthread_mutex_lock(&g_lock);
    g_tr.busy = 1;
    g_tr.msg[0] = 0;
    snprintf(g_tr.id, sizeof(g_tr.id), "%s", id);
    snprintf(g_tr.phase, sizeof(g_tr.phase), "%s", gen ? "Das Profil wird berechnet" : "Die Sitzung wird eingesammelt");
    pthread_mutex_unlock(&g_lock);
    pthread_t th;
    pthread_attr_t at;
    pthread_attr_init(&at);
    pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
    pthread_attr_setstacksize(&at, 512 * 1024);
    int prc = pthread_create(&th, &at, tr_thread, j);
    pthread_attr_destroy(&at);
    if(prc != 0) {
      free(j);
      set_result(id, 1, "Der Vorgang ließ sich nicht starten.");
      snprintf(msg, n, "Der Vorgang ließ sich nicht starten.");
      return 500;
    }
    snprintf(msg, n, "%s.", gen ? "Das Profil wird berechnet" : "Die Sitzung wird eingesammelt");
    return 200;
  }
  snprintf(msg, n, "Unbekannte Aktion.");
  return 400;
}
