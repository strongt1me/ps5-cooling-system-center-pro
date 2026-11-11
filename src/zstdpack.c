/* zstd asset packs of a game: the packer, the checker, the unpacker. See zstdpack.h for the format and the rules. */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <sched.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#ifndef PS5TM_HOST_TEST
#include <sys/rtprio.h>
#include <sys/syscall.h>
#endif

#include "zstdpack.h"
#include "third_party/libdeflate/libdeflate.h"
#define ZSTD_STATIC_LINKING_ONLY                         /* ZSTD_getFrameHeader: the check reads the frame parameters */
#include "third_party/zstd/lib/zstd.h"

#define INDEX_HEADER   128u
#define VOLUME_HEADER  64u
#define FILE_REC       32u
#define CHUNK_SHIFT    18u
#define CHUNK_SIZE     (1u << CHUNK_SHIFT)
#define VOLUME_LIMIT   ((1ull << 32) - (1ull << 16))
#define MAX_VOLUMES    4096u
#define MAX_FILES      2000000u
#define INDEX_MAX      (512ull << 20)
#define IO_BUF         (1u << 20)

static const uint8_t k_index_magic[8]  = { 'A', 'M', 'P', 'R', 'Z', 'P', 'K', '1' };
static const uint8_t k_volume_magic[8] = { 'A', 'M', 'P', 'R', 'Z', 'D', 'T', '1' };

/* ---------------------------------------------------------------- small helpers */

static void
say(char *err, size_t n, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
static void
say(char *err, size_t n, const char *fmt, ...) {
  if(!err || !n) return;
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(err, n, fmt, ap);
  va_end(ap);
}

static void p16(uint8_t *b, size_t o, uint32_t v) { b[o] = (uint8_t)v; b[o + 1] = (uint8_t)(v >> 8); }
static void p32(uint8_t *b, size_t o, uint32_t v) { for(int i = 0; i < 4; i++) b[o + i] = (uint8_t)(v >> (8 * i)); }
static void p64(uint8_t *b, size_t o, uint64_t v) { for(int i = 0; i < 8; i++) b[o + i] = (uint8_t)(v >> (8 * i)); }
static uint32_t g16(const uint8_t *b, size_t o) { return (uint32_t)b[o] | (uint32_t)b[o + 1] << 8; }
static uint32_t g32(const uint8_t *b, size_t o) { uint32_t v = 0; for(int i = 3; i >= 0; i--) v = v << 8 | b[o + i]; return v; }
static uint64_t g64(const uint8_t *b, size_t o) { uint64_t v = 0; for(int i = 7; i >= 0; i--) v = v << 8 | b[o + i]; return v; }
static uint32_t crc32_of(const void *d, size_t n) { return libdeflate_crc32(0, d, n); }

static int
cancelled(const lzp_ui_t *ui) {
  return ui && ui->cancel && __atomic_load_n(ui->cancel, __ATOMIC_ACQUIRE);
}

static void ui_phase(const lzp_ui_t *ui, const char *t) { if(ui && ui->phase) ui->phase(ui->ctx, t); }
static void ui_total(const lzp_ui_t *ui, uint64_t b) { if(ui && ui->total) ui->total(ui->ctx, b); }
static void ui_work(const lzp_ui_t *ui, uint64_t b) { if(ui && ui->work && b) ui->work(ui->ctx, b); }
static void ui_files(const lzp_ui_t *ui, unsigned d, unsigned t) { if(ui && ui->files) ui->files(ui->ctx, d, t); }
static void ui_current(const lzp_ui_t *ui, const char *p) { if(ui && ui->current) ui->current(ui->ctx, p); }

/* FNV-1a 64 over the bytes of the path, case kept (the format's key; not the case-folding hash of AMPRIDX3) */
static uint64_t
path_hash(const uint8_t *p, size_t n) {
  uint64_t h = 0xCBF29CE484222325ull;
  for(size_t i = 0; i < n; i++) { h ^= p[i]; h *= 0x100000001B3ull; }
  return h;
}

/* the format's rules for a path: not empty, no leading "/", no "\" or "." / ".." / empty component, at most 65535 bytes */
static int
path_ok(const char *rel, size_t len) {
  if(!len || len > 65535 || rel[0] == '/') return 0;
  const char *s = rel, *end = rel + len;
  while(s <= end) {
    const char *e = s;
    while(e < end && *e != '/') {
      if(*e == '\\' || *e == 0) return 0;
      e++;
    }
    size_t l = (size_t)(e - s);
    if(l == 0 || (l == 1 && s[0] == '.') || (l == 2 && s[0] == '.' && s[1] == '.')) return 0;
    if(e == end) break;
    s = e + 1;
  }
  return 1;
}

static int
read_all(int fd, void *buf, size_t n, uint64_t off) {
  uint8_t *p = buf;
  while(n) {
    ssize_t r = pread(fd, p, n, (off_t)off);
    if(r < 0) { if(errno == EINTR) continue; return -1; }
    if(r == 0) { errno = EIO; return -1; }
    p += r;
    off += (uint64_t)r;
    n -= (size_t)r;
  }
  return 0;
}

static int
write_all(int fd, const void *buf, size_t n) {
  const uint8_t *p = buf;
  while(n) {
    ssize_t w = write(fd, p, n);
    if(w < 0) { if(errno == EINTR) continue; return -1; }
    p += w;
    n -= (size_t)w;
  }
  return 0;
}

static int
file_exists(const char *p) {
  struct stat st;
  return lstat(p, &st) == 0;
}

static int
mkdir_p(const char *path) {
  char buf[PATH_MAX];
  snprintf(buf, sizeof(buf), "%s", path);
  for(char *p = buf + 1; *p; p++) {
    if(*p != '/') continue;
    *p = 0;
    if(mkdir(buf, 0777) == 0) (void)chmod(buf, 0777);
    else if(errno != EEXIST) return errno ? errno : EIO;
    *p = '/';
  }
  if(mkdir(buf, 0777) == 0) (void)chmod(buf, 0777);
  else if(errno != EEXIST) return errno ? errno : EIO;
  return 0;
}

static void
random16(uint8_t out[16]) {
  int fd = open("/dev/urandom", O_RDONLY);
  if(fd >= 0) {
    ssize_t r = read(fd, out, 16);
    close(fd);
    if(r == 16) return;
  }
  struct timespec ts;                                    /* no random device: time and addresses are enough for an id */
  clock_gettime(CLOCK_REALTIME, &ts);
  uint64_t a = (uint64_t)ts.tv_nsec * 0x9E3779B97F4A7C15ull ^ (uint64_t)ts.tv_sec ^ (uint64_t)(uintptr_t)out;
  uint64_t b = (a ^ (a >> 31)) * 0xD6E8FEB86659FD93ull ^ (uint64_t)getpid();
  p64(out, 0, a);
  p64(out, 8, b);
}

/* ---------------------------------------------------------------- what stays loose */

static int
ends_ci(const char *s, const char *suffix) {
  size_t a = strlen(s), b = strlen(suffix);
  return a >= b && !strcasecmp(s + a - b, suffix);
}

static int
volume_name(const char *name) {                          /* ampr_zstd-NNN.pak, at least three digits */
  size_t l = strlen(name);
  if(l < 10 + 3 + 4 || strncasecmp(name, "ampr_zstd-", 10) || strcasecmp(name + l - 4, ".pak")) return 0;
  for(size_t i = 10; i < l - 4; i++) if(name[i] < '0' || name[i] > '9') return 0;
  return 1;
}

int
zsp_stays_loose(const char *rel, uint64_t size) {
  if(size == 0) return 1;
  const char *slash = strrchr(rel, '/');
  const char *name = slash ? slash + 1 : rel;
  if(!strcasecmp(name, "eboot.bin") || ends_ci(rel, ".elf") || ends_ci(rel, ".self") || ends_ci(rel, ".prx") || ends_ci(rel, ".sprx")) return 1;
  /* every .ini stays editable (dlc_emu.ini and other settings a person changes by hand), as in Dump Forge 0.5.5 (10.11.2026) */
  if(lzc_is_ini(rel)) return 1;
  const char *first_end = strchr(rel, '/');
  if(first_end) {                                        /* a folder at the root of the game */
    size_t fl = (size_t)(first_end - rel);
    if((fl == 7 && !strncasecmp(rel, "sce_sys", 7)) || (fl == 10 && !strncasecmp(rel, "sce_module", 10)) ||
       (fl >= 7 && !strncasecmp(rel, "fakelib", 7))) return 1;
  } else {                                               /* a file at the root: the packs, the indexes, the runtime's own files */
    if(!strcasecmp(rel, ZSP_INDEX_NAME) || volume_name(rel) || !strcasecmp(rel, "ampr_emu.index") ||
       !strncasecmp(rel, "ampr_assets.index", 17) || (!strncasecmp(rel, "ampr_assets-", 12) && ends_ci(rel, ".pak")) ||
       !strcasecmp(rel, "ampr_pack.toml") || !strcasecmp(rel, "ampr_commands.bin") || !strcasecmp(rel, "ampr_emu.log") ||
       !strcasecmp(rel, "apr_emu.log")) return 1;
  }
  return lzc_keep_on_disk(rel, size);
}

int
zsp_packed_dir(const char *dir) {
  char p[PATH_MAX];
  struct stat st;
  snprintf(p, sizeof(p), "%s/" ZSP_INDEX_NAME, dir);
  return stat(p, &st) == 0 && S_ISREG(st.st_mode);
}

/* ---------------------------------------------------------------- the files of a game */

typedef struct {
  char    *rel;
  uint64_t size, hash;
  uint32_t len;
} zent_t;

typedef struct {
  zent_t *e;
  size_t  n, cap;
} zlist_t;

static void
zlist_free(zlist_t *l) {
  for(size_t i = 0; i < l->n; i++) free(l->e[i].rel);
  free(l->e);
  memset(l, 0, sizeof(*l));
}

static int
zlist_push(zlist_t *l, const char *rel, uint64_t size) {
  if(l->n == l->cap) {
    size_t nc = l->cap ? l->cap * 2 : 1024;
    zent_t *ne = realloc(l->e, nc * sizeof(zent_t));
    if(!ne) return -1;
    l->e = ne;
    l->cap = nc;
  }
  size_t len = strlen(rel);
  char *d = malloc(len + 1);
  if(!d) return -1;
  memcpy(d, rel, len + 1);
  l->e[l->n++] = (zent_t){ d, size, path_hash((const uint8_t *)rel, len), (uint32_t)len };
  return 0;
}

static int
cmp_ent(const void *a, const void *b) {
  const zent_t *x = a, *y = b;
  if(x->hash != y->hash) return x->hash < y->hash ? -1 : 1;
  return strcmp(x->rel, y->rel);
}

typedef struct {
  zlist_t       *pack;                                   /* the files that go into the packs (may be NULL: only counted) */
  lzp_plan_t    *plan;
  const lzp_ui_t *ui;
  size_t         rootlen;
  char          *err;
  size_t         en;
} walk_t;

static int
walk_dir(walk_t *w, char *path, size_t len, int depth) {
  if(depth > 64) { say(w->err, w->en, "Der Spielordner ist zu tief verschachtelt."); return -1; }
  DIR *d = opendir(path);
  if(!d) { say(w->err, w->en, "Ein Ordner im Spiel ließ sich nicht lesen (%s).", strerror(errno)); return -1; }
  int rc = 0;
  for(;;) {
    errno = 0;
    struct dirent *e = readdir(d);
    if(!e) {
      if(errno) { say(w->err, w->en, "Ein Ordner im Spiel ließ sich nicht ganz lesen (%s).", strerror(errno)); rc = -1; }
      break;
    }
    if(!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
    size_t n = strlen(e->d_name);
    if(len + 1 + n >= PATH_MAX - 8) { say(w->err, w->en, "Ein Pfad im Spiel ist zu lang: %s", e->d_name); rc = -1; break; }
    path[len] = '/';
    memcpy(path + len + 1, e->d_name, n + 1);
    struct stat st;
    if(lstat(path, &st) != 0) {
      say(w->err, w->en, "Eine Datei im Spiel ließ sich nicht ansehen (%s): %s", strerror(errno), path + w->rootlen + 1);
      rc = -1;
    } else if(S_ISDIR(st.st_mode)) {
      rc = walk_dir(w, path, len + 1 + n, depth + 1);
    } else if(S_ISREG(st.st_mode)) {
      const char *rel = path + w->rootlen + 1;
      uint64_t size = (uint64_t)st.st_size;
      w->plan->files_total++;
      if(zsp_stays_loose(rel, size) || !path_ok(rel, strlen(rel))) {
        w->plan->loose_files++;
        w->plan->loose_bytes += size;
      } else {
        w->plan->pack_files++;
        w->plan->pack_bytes += size;
        w->plan->chunks += (size + CHUNK_SIZE - 1) >> CHUNK_SHIFT;
        if(w->pack && zlist_push(w->pack, rel, size) != 0) { say(w->err, w->en, "Kein Speicher."); rc = -1; }
      }
    } else {                                             /* a link or a device: not packed, and the job's own check said so before */
      w->plan->loose_files++;
    }
    path[len] = 0;
    if(rc != 0) break;
    if(cancelled(w->ui)) { say(w->err, w->en, "Abgebrochen."); rc = ECANCELED; break; }
  }
  closedir(d);
  return rc;
}

static int
scan_game(const char *root, zlist_t *pack, lzp_plan_t *plan, const lzp_ui_t *ui, char *err, size_t en) {
  memset(plan, 0, sizeof(*plan));
  plan->block_shift = CHUNK_SHIFT;
  char *path = malloc(PATH_MAX);
  if(!path) { say(err, en, "Kein Speicher."); return -1; }
  snprintf(path, PATH_MAX, "%s", root);
  size_t rl = strlen(path);
  while(rl > 1 && path[rl - 1] == '/') path[--rl] = 0;
  walk_t w = { pack, plan, ui, rl, err, en };
  int rc = walk_dir(&w, path, rl, 0);
  free(path);
  return rc;
}

int
zsp_plan(const char *root, lzp_plan_t *plan, const lzp_ui_t *ui, char *err, size_t n) {
  return scan_game(root, NULL, plan, ui, err, n);
}

/* ---------------------------------------------------------------- the compression pool */

enum { SL_FREE = 0, SL_READY, SL_BUSY, SL_DONE };

typedef struct {
  uint8_t *raw, *out;
  uint32_t raw_len, out_len, item, chunk;
  uint8_t  codec;                                        /* 0 raw, 1 zstd */
  uint64_t seq;
  int      last, state;
} zslot_t;

typedef struct {
  pthread_mutex_t mu;
  pthread_cond_t  work, done;
  zslot_t        *slots;
  unsigned        nslots;
  int             stop;
  int             normal_quota, normal_used;
  size_t          out_cap;
} zpool_t;

typedef struct {
  zpool_t  *pool;
  ZSTD_CCtx *cctx;
} zworker_t;

static int
set_idle_priority(void) {
  int ok = 0;
#if !defined(PS5TM_HOST_TEST) && defined(SYS_rtprio_thread)
  struct rtprio rtp = { RTP_PRIO_IDLE, 0 };
  int set = (int)syscall(SYS_rtprio_thread, RTP_SET, 0, &rtp);
  struct rtprio got = { 0, 0 };
  ok = set == 0 && syscall(SYS_rtprio_thread, RTP_LOOKUP, 0, &got) == 0 && got.type == RTP_PRIO_IDLE;
#endif
  return ok;
}

static void *
pool_worker(void *arg) {
  zworker_t *wk = arg;
  zpool_t *p = wk->pool;
  int idle = set_idle_priority();
  pthread_mutex_lock(&p->mu);
  if(!idle && p->normal_used >= p->normal_quota) { pthread_mutex_unlock(&p->mu); return NULL; }     /* one CPU stays free */
  if(!idle) p->normal_used++;
  for(;;) {
    zslot_t *s = NULL;
    while(!p->stop) {
      for(unsigned i = 0; i < p->nslots; i++) {
        zslot_t *x = &p->slots[i];
        if(x->state == SL_READY && (!s || x->seq < s->seq)) s = x;
      }
      if(s) break;
      pthread_cond_wait(&p->work, &p->mu);
    }
    if(!s) break;
    s->state = SL_BUSY;
    pthread_mutex_unlock(&p->mu);
    size_t r = ZSTD_compress2(wk->cctx, s->out, p->out_cap, s->raw, s->raw_len);
    if(!ZSTD_isError(r) && r < s->raw_len) { s->codec = 1; s->out_len = (uint32_t)r; }     /* zstd only when it is strictly smaller */
    else { s->codec = 0; s->out_len = s->raw_len; }
    if(!idle) sched_yield();
    pthread_mutex_lock(&p->mu);
    s->state = SL_DONE;
    pthread_cond_broadcast(&p->done);
  }
  pthread_mutex_unlock(&p->mu);
  return NULL;
}

/* ---------------------------------------------------------------- zsp_pack */

typedef struct {
  FILE    *f;
  unsigned n;                                            /* volumes started */
  uint64_t pos;                                          /* where the next chunk goes in the open volume */
  uint64_t *size;                                        /* of each volume when it was closed */
  char     dir[PATH_MAX];
} zvols_t;

static void
vol_tmp_name(const zvols_t *v, unsigned i, char *out, size_t n) {
  snprintf(out, n, "%s/" ZSP_VOLUME_FMT LZP_PART_SUFFIX, v->dir, i);
}

static void
vol_final_name(const char *dir, unsigned i, char *out, size_t n) {
  snprintf(out, n, "%s/" ZSP_VOLUME_FMT, dir, i);
}

static int
vol_close(zvols_t *v) {
  if(!v->f) return 0;
  int rc = 0;
  if(fflush(v->f) != 0 || fsync(fileno(v->f)) != 0) rc = -1;
  if(fclose(v->f) != 0) rc = -1;
  v->f = NULL;
  v->size[v->n - 1] = v->pos;
  return rc;
}

static int
vol_open_next(zvols_t *v, char *err, size_t en) {
  if(v->n >= MAX_VOLUMES) { say(err, en, "Das Spiel brauchte mehr als %u Paketdateien.", MAX_VOLUMES); return -1; }
  char p[PATH_MAX + 64];
  vol_tmp_name(v, v->n, p, sizeof(p));
  int fd = open(p, O_WRONLY | O_CREAT | O_TRUNC, LZP_FILE_MODE);
  if(fd < 0) { say(err, en, "Eine Paketdatei ließ sich nicht anlegen (%s).", strerror(errno)); return -1; }
  (void)fchmod(fd, LZP_FILE_MODE);
  v->f = fdopen(fd, "wb");
  if(!v->f) { close(fd); say(err, en, "Kein Speicher."); return -1; }
  setvbuf(v->f, NULL, _IOFBF, IO_BUF);
  uint8_t zero[VOLUME_HEADER] = { 0 };
  if(fwrite(zero, 1, sizeof(zero), v->f) != sizeof(zero)) {
    say(err, en, "Schreiben in eine Paketdatei schlug fehl (%s).", strerror(errno));
    fclose(v->f);                                      /* not counted in v->n yet, so remove_tmp_set would never see it */
    v->f = NULL;
    unlink(p);
    return -1;
  }
  v->pos = VOLUME_HEADER;
  v->n++;
  return 0;
}

typedef struct {
  uint8_t *b;
  size_t   n, cap;
} zbuf_t;

static int
zbuf_add(zbuf_t *z, const void *d, size_t n) {
  if(z->n + n > z->cap) {
    size_t nc = z->cap ? z->cap : 4096;
    while(nc < z->n + n) nc *= 2;
    uint8_t *nb = realloc(z->b, nc);
    if(!nb) return -1;
    z->b = nb;
    z->cap = nc;
  }
  if(n) memcpy(z->b + z->n, d, n);
  z->n += n;
  return 0;
}

static void
remove_tmp_set(const char *dir, unsigned nvol) {
  char p[PATH_MAX + 64];
  for(unsigned i = 0; i < nvol; i++) {
    snprintf(p, sizeof(p), "%s/" ZSP_VOLUME_FMT LZP_PART_SUFFIX, dir, i);
    unlink(p);
  }
  snprintf(p, sizeof(p), "%s/" ZSP_INDEX_NAME LZP_PART_SUFFIX, dir);
  unlink(p);
}

int
zsp_pack(const char *root, const char *outdir, int level, unsigned workers, lzp_stats_t *st, const lzp_ui_t *ui, char *err, size_t en) {
  lzp_stats_t zero_stats;
  if(!st) st = &zero_stats;
  memset(st, 0, sizeof(*st));
  if(level < ZSP_LEVEL_MIN || level > ZSP_LEVEL_MAX) level = ZSP_LEVEL_DEFAULT;
  char final_index[PATH_MAX + 32];
  snprintf(final_index, sizeof(final_index), "%s/" ZSP_INDEX_NAME, outdir);
  if(file_exists(final_index)) { say(err, en, "Hier gibt es schon zstd-Pakete."); return -1; }

  zlist_t list;
  memset(&list, 0, sizeof(list));
  lzp_plan_t plan;
  zpool_t pool;
  memset(&pool, 0, sizeof(pool));
  zvols_t vols;
  memset(&vols, 0, sizeof(vols));
  zbuf_t files_tab = { 0 }, chunks_tab = { 0 }, strings = { 0 };
  zworker_t *wk = NULL;
  pthread_t *th = NULL;
  unsigned nth = 0, nwk = 0, renamed = 0;
  int rc = -1, pool_up = 0, fd = -1, err_state = 0;
  struct stat src0;
  uint8_t *idx = NULL;

  ui_phase(ui, "Die Dateien werden ausgewählt");
  rc = scan_game(root, &list, &plan, ui, err, en);
  if(rc != 0) goto out;
  rc = -1;
  if(list.n == 0) { say(err, en, "Es gäbe nichts zu packen."); goto out; }
  if(list.n > MAX_FILES) { say(err, en, "Das Spiel hat zu viele Dateien für die Pakete (mehr als %u).", MAX_FILES); goto out; }
  qsort(list.e, list.n, sizeof(zent_t), cmp_ent);
  mkdir_p(outdir);
  snprintf(vols.dir, sizeof(vols.dir), "%s", outdir);
  vols.size = calloc(MAX_VOLUMES, sizeof(uint64_t));
  if(!vols.size) { say(err, en, "Kein Speicher."); goto out; }

  st->files_total = plan.files_total;
  st->files_loose = plan.loose_files;
  st->loose_bytes = plan.loose_bytes;
  ui_total(ui, plan.pack_bytes);
  ui_files(ui, 0, (unsigned)list.n);
  ui_phase(ui, "Die Dateien werden gepackt");

  /* the pool: every worker has a context of its own */
  if(!workers) workers = 3;
  if(workers > 16) workers = 16;
  pool.nslots = workers * 3 + 2;
  pool.out_cap = ZSTD_compressBound(CHUNK_SIZE);
  pool.slots = calloc(pool.nslots, sizeof(zslot_t));
  if(!pool.slots) { say(err, en, "Kein Speicher."); goto out; }
  for(unsigned i = 0; i < pool.nslots; i++) {
    pool.slots[i].raw = malloc(CHUNK_SIZE);
    pool.slots[i].out = malloc(pool.out_cap);
    if(!pool.slots[i].raw || !pool.slots[i].out) { say(err, en, "Kein Speicher."); goto out; }
  }
  pthread_mutex_init(&pool.mu, NULL);
  pthread_cond_init(&pool.work, NULL);
  pthread_cond_init(&pool.done, NULL);
  pool.normal_quota = workers > 1 ? (int)workers - 1 : 1;
  wk = calloc(workers, sizeof(zworker_t));
  th = calloc(workers, sizeof(pthread_t));
  if(!wk || !th) { say(err, en, "Kein Speicher."); goto out; }
  pool_up = 1;
  for(unsigned i = 0; i < workers; i++) {
    ZSTD_CCtx *c = ZSTD_createCCtx();
    if(!c) { say(err, en, "Kein Speicher für die Kompression."); goto out; }
    wk[nwk].pool = &pool;
    wk[nwk].cctx = c;
    nwk++;
    if(ZSTD_isError(ZSTD_CCtx_setParameter(c, ZSTD_c_compressionLevel, level)) ||
       ZSTD_isError(ZSTD_CCtx_setParameter(c, ZSTD_c_windowLog, (int)CHUNK_SHIFT)) ||
       ZSTD_isError(ZSTD_CCtx_setParameter(c, ZSTD_c_checksumFlag, 1)) ||
       ZSTD_isError(ZSTD_CCtx_setParameter(c, ZSTD_c_contentSizeFlag, 1)) ||
       ZSTD_isError(ZSTD_CCtx_setParameter(c, ZSTD_c_dictIDFlag, 0)) ||
       ZSTD_isError(ZSTD_CCtx_setParameter(c, ZSTD_c_nbWorkers, 0))) {
      say(err, en, "Die Kompression ließ sich nicht einstellen.");
      goto out;
    }
  }
  for(unsigned i = 0; i < nwk; i++) {
    pthread_attr_t at;
    pthread_attr_init(&at);
    pthread_attr_setstacksize(&at, 512 * 1024);
    if(pthread_create(&th[nth], &at, pool_worker, &wk[i]) == 0) nth++;
    pthread_attr_destroy(&at);
  }
  if(nth == 0) { say(err, en, "Die Rechenfäden ließen sich nicht starten."); goto out; }

  if(vol_open_next(&vols, err, en) != 0) goto out;

  /* read the chunks in order into free slots, write the finished ones in order */
  size_t cur_item = 0;
  uint64_t cur_off = 0;
  uint32_t cur_chunk = 0;
  uint64_t seq_next = 0, seq_write = 0, total_logical = 0;
  unsigned files_done = 0;
  int reading = 0;
  uint8_t rec[FILE_REC];
  for(;;) {
    for(;;) {                                            /* fill slots */
      if(err_state) break;
      if(!reading) {
        if(cur_item >= list.n) break;
        const zent_t *it = &list.e[cur_item];
        char abs[2 * PATH_MAX + 8];
        if(snprintf(abs, sizeof(abs), "%s/%s", root, it->rel) >= (int)sizeof(abs)) { say(err, en, "Ein Pfad ist zu lang: %s", it->rel); err_state = 1; break; }
        fd = open(abs, O_RDONLY);
        if(fd < 0 || fstat(fd, &src0) != 0) { say(err, en, "Eine Datei ließ sich nicht öffnen: %s", it->rel); err_state = 1; break; }
        if((uint64_t)src0.st_size != it->size) { say(err, en, "Die Datei hat sich während des Packens verändert: %s", it->rel); err_state = 1; break; }
        cur_off = 0;
        cur_chunk = 0;
        reading = 1;
        ui_current(ui, it->rel);
      }
      zslot_t *s = NULL;
      pthread_mutex_lock(&pool.mu);
      for(unsigned i = 0; i < pool.nslots; i++) if(pool.slots[i].state == SL_FREE) { s = &pool.slots[i]; break; }
      pthread_mutex_unlock(&pool.mu);
      if(!s) break;
      const zent_t *it = &list.e[cur_item];
      uint32_t len = (uint32_t)(it->size - cur_off < CHUNK_SIZE ? it->size - cur_off : CHUNK_SIZE);
      if(read_all(fd, s->raw, len, cur_off) != 0) { say(err, en, "Eine Datei ist beim Packen kürzer geworden: %s", it->rel); err_state = 1; break; }
      s->raw_len = len;
      s->item = (uint32_t)cur_item;
      s->chunk = cur_chunk;
      s->seq = seq_next++;
      cur_off += len;
      cur_chunk++;
      s->last = cur_off >= it->size;
      if(s->last) {
        struct stat st2;
        if(fstat(fd, &st2) != 0 || st2.st_size != src0.st_size || st2.st_mtime != src0.st_mtime) {
          say(err, en, "Die Datei hat sich während des Packens verändert: %s", it->rel);
          err_state = 1;
          break;
        }
        close(fd);
        fd = -1;
        reading = 0;
        cur_item++;
      }
      pthread_mutex_lock(&pool.mu);
      s->state = SL_READY;
      pthread_cond_signal(&pool.work);
      pthread_mutex_unlock(&pool.mu);
    }
    if(err_state) break;
    if(cancelled(ui)) { say(err, en, "Abgebrochen."); rc = ECANCELED; goto out; }
    if(seq_write == seq_next) {                          /* nothing in flight: done when nothing is left to read */
      if(!reading && cur_item >= list.n) break;
      continue;
    }
    zslot_t *s = NULL;                                   /* the next finished one in order */
    pthread_mutex_lock(&pool.mu);
    for(;;) {
      for(unsigned i = 0; i < pool.nslots; i++)
        if(pool.slots[i].seq == seq_write && pool.slots[i].state == SL_DONE) { s = &pool.slots[i]; break; }
      if(s) break;
      struct timespec ts;
      clock_gettime(CLOCK_REALTIME, &ts);
      ts.tv_nsec += 200 * 1000 * 1000;
      if(ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
      pthread_cond_timedwait(&pool.done, &pool.mu, &ts);
      if(cancelled(ui)) break;
    }
    pthread_mutex_unlock(&pool.mu);
    if(!s) { say(err, en, "Abgebrochen."); rc = ECANCELED; goto out; }
    seq_write++;

    const zent_t *it = &list.e[s->item];
    if(s->chunk == 0) {                                  /* the file's record, as its first chunk arrives */
      uint64_t path_off = strings.n;
      if(zbuf_add(&strings, it->rel, it->len) != 0) { say(err, en, "Kein Speicher."); goto out; }
      p64(rec, 0, it->hash);
      p64(rec, 8, it->size);
      p32(rec, 16, (uint32_t)(chunks_tab.n / 8));
      p32(rec, 20, (uint32_t)path_off);
      p16(rec, 24, it->len);
      p16(rec, 26, 0);
      p32(rec, 28, 0);
      if(zbuf_add(&files_tab, rec, sizeof(rec)) != 0) { say(err, en, "Kein Speicher."); goto out; }
    }
    uint32_t stored_len = s->codec ? s->out_len : s->raw_len;
    const uint8_t *stored = s->codec ? s->out : s->raw;
    if(vols.pos + stored_len > VOLUME_LIMIT) {           /* a chunk that does not fit starts the next volume */
      if(vol_close(&vols) != 0) { say(err, en, "Schreiben in eine Paketdatei schlug fehl (%s).", strerror(errno)); goto out; }
      if(vol_open_next(&vols, err, en) != 0) goto out;
    }
    if(fwrite(stored, 1, stored_len, vols.f) != stored_len) { say(err, en, "Schreiben in eine Paketdatei schlug fehl (%s).", strerror(errno)); goto out; }
    uint8_t w8[8];
    p64(w8, 0, vols.pos | ((uint64_t)(vols.n - 1) << 32) | ((uint64_t)(stored_len - 1) << 44) | ((uint64_t)s->codec << 62));
    if(zbuf_add(&chunks_tab, w8, 8) != 0) { say(err, en, "Kein Speicher."); goto out; }
    vols.pos += stored_len;
    st->chunks++;
    if(s->codec) st->chunks_lz4++; else st->chunks_raw++;
    st->stored_bytes += stored_len;
    st->logical_bytes += s->raw_len;
    total_logical += s->raw_len;
    ui_work(ui, s->raw_len);
    if(s->last) { st->files_packed++; ui_files(ui, ++files_done, (unsigned)list.n); }
    pthread_mutex_lock(&pool.mu);
    s->state = SL_FREE;
    pthread_mutex_unlock(&pool.mu);
  }
  if(err_state) goto out;
  if(vol_close(&vols) != 0) { say(err, en, "Schreiben in eine Paketdatei schlug fehl (%s).", strerror(errno)); goto out; }
  st->packs = vols.n;

  /* the headers of the volumes, now that the sizes are known */
  uint8_t arc[16];
  random16(arc);
  for(unsigned i = 0; i < vols.n; i++) {
    char p[PATH_MAX + 64];
    vol_tmp_name(&vols, i, p, sizeof(p));
    int vfd = open(p, O_WRONLY);
    uint8_t h[VOLUME_HEADER] = { 0 };
    memcpy(h, k_volume_magic, 8);
    p32(h, 8, 1);
    p32(h, 12, VOLUME_HEADER);
    p32(h, 16, i);
    p32(h, 20, vols.n);
    memcpy(h + 24, arc, 16);
    p64(h, 40, vols.size[i] - VOLUME_HEADER);
    p32(h, 60, crc32_of(h, VOLUME_HEADER));              /* the CRC field is still zero while it is computed */
    if(vfd < 0 || pwrite(vfd, h, sizeof(h), 0) != (ssize_t)sizeof(h) || fsync(vfd) != 0) {
      say(err, en, "Der Kopf einer Paketdatei ließ sich nicht schreiben (%s).", strerror(errno));
      if(vfd >= 0) close(vfd);
      goto out;
    }
    close(vfd);
  }

  /* the index */
  size_t nv = vols.n;
  uint64_t files_off = INDEX_HEADER, chunks_off = files_off + files_tab.n, volumes_off = chunks_off + chunks_tab.n;
  uint64_t strings_off = volumes_off + 8 * nv;
  size_t total = (size_t)strings_off + strings.n;
  idx = calloc(1, total);
  if(!idx) { say(err, en, "Kein Speicher."); goto out; }
  memcpy(idx + files_off, files_tab.b, files_tab.n);
  memcpy(idx + chunks_off, chunks_tab.b, chunks_tab.n);
  for(size_t i = 0; i < nv; i++) p64(idx, volumes_off + 8 * i, vols.size[i]);
  if(strings.n) memcpy(idx + strings_off, strings.b, strings.n);
  memcpy(idx, k_index_magic, 8);
  p32(idx, 8, 1);
  p32(idx, 12, INDEX_HEADER);
  idx[16] = (uint8_t)CHUNK_SHIFT;
  idx[17] = (uint8_t)level;
  p32(idx, 20, (uint32_t)nv);
  memcpy(idx + 24, arc, 16);
  p64(idx, 40, list.n);
  p64(idx, 48, chunks_tab.n / 8);
  p64(idx, 56, files_off);
  p64(idx, 64, chunks_off);
  p64(idx, 72, volumes_off);
  p64(idx, 80, strings_off);
  p64(idx, 88, strings.n);
  p64(idx, 96, total_logical);
  p32(idx, 112, crc32_of(idx + INDEX_HEADER, total - INDEX_HEADER));
  {                                                      /* the header's own CRC is taken with bytes 112..120 (both CRCs) zeroed */
    uint8_t h[INDEX_HEADER];
    memcpy(h, idx, sizeof(h));
    memset(h + 112, 0, 8);
    p32(idx, 116, crc32_of(h, sizeof(h)));
  }
  char tmp_index[PATH_MAX + 48];
  snprintf(tmp_index, sizeof(tmp_index), "%s" LZP_PART_SUFFIX, final_index);
  int ifd = open(tmp_index, O_WRONLY | O_CREAT | O_TRUNC, LZP_FILE_MODE);
  if(ifd < 0) { say(err, en, "Der Index ließ sich nicht anlegen (%s).", strerror(errno)); goto out; }
  (void)fchmod(ifd, LZP_FILE_MODE);
  if(write_all(ifd, idx, total) != 0 || fsync(ifd) != 0) {
    say(err, en, "Der Index ließ sich nicht schreiben (%s).", strerror(errno));
    close(ifd);
    goto out;
  }
  if(close(ifd) != 0) { say(err, en, "Der Index ließ sich nicht schreiben (%s).", strerror(errno)); goto out; }

  /* published: the volumes first, the index last */
  ui_phase(ui, "Die Pakete werden fertiggestellt");
  for(unsigned i = 0; i < vols.n; i++) {
    char a[PATH_MAX + 64], b[PATH_MAX + 64];
    vol_tmp_name(&vols, i, a, sizeof(a));
    vol_final_name(outdir, i, b, sizeof(b));
    if(rename(a, b) != 0) { say(err, en, "Eine Paketdatei ließ sich nicht umbenennen (%s).", strerror(errno)); goto out; }
    renamed++;
  }
  if(rename(tmp_index, final_index) != 0) { say(err, en, "Der Index ließ sich nicht umbenennen (%s).", strerror(errno)); goto out; }
  rc = 0;

out:
  if(pool_up) {
    pthread_mutex_lock(&pool.mu);
    pool.stop = 1;
    pthread_cond_broadcast(&pool.work);
    pthread_mutex_unlock(&pool.mu);
    for(unsigned i = 0; i < nth; i++) pthread_join(th[i], NULL);
    pthread_mutex_destroy(&pool.mu);
    pthread_cond_destroy(&pool.work);
    pthread_cond_destroy(&pool.done);
  }
  for(unsigned i = 0; i < nwk; i++) ZSTD_freeCCtx(wk[i].cctx);
  if(pool.slots) for(unsigned i = 0; i < pool.nslots; i++) { free(pool.slots[i].raw); free(pool.slots[i].out); }
  if(rc != 0) {                                          /* a failed run leaves nothing behind */
    if(vols.f) fclose(vols.f);
    char p[PATH_MAX + 64];
    for(unsigned i = 0; i < renamed; i++) { vol_final_name(outdir, i, p, sizeof(p)); unlink(p); }
    remove_tmp_set(outdir, vols.n);
  }
  if(fd >= 0) close(fd);
  free(pool.slots);
  free(wk);
  free(th);
  free(vols.size);
  free(files_tab.b);
  free(chunks_tab.b);
  free(strings.b);
  free(idx);
  zlist_free(&list);
  return rc;
}

/* ---------------------------------------------------------------- reading a pack set */

typedef struct {
  uint8_t        *buf;
  size_t          len;
  uint64_t        nf, nc, logical, strings_len;
  unsigned        nv;
  int             level;
  uint8_t         arc[16];
  const uint8_t  *files, *chunks, *vols, *strings;
} zidx_t;

static void zidx_free(zidx_t *x) { free(x->buf); memset(x, 0, sizeof(*x)); }

/* the header of the index: its checks without the payload (the light read of zsp_info) */
static int
header_check(const uint8_t *h, uint64_t file_len, zidx_t *x, char *why, size_t wn) {
  if(memcmp(h, k_index_magic, 8)) { say(why, wn, "Das ist kein Index von zstd-Paketen."); return -1; }
  if(g32(h, 8) != 1 || g32(h, 12) != INDEX_HEADER) { say(why, wn, "Der Index hat eine unbekannte Fassung."); return -1; }
  if(h[16] != CHUNK_SHIFT) { say(why, wn, "Der Index teilt in Stücke von %u KiB (nötig sind 256 KiB). Bitte neu packen.", h[16] < 31 ? 1u << (h[16] - 10) : 0u); return -1; }
  if(h[17] < ZSP_LEVEL_MIN || h[17] > ZSP_LEVEL_MAX) { say(why, wn, "Der Index nennt eine ungültige zstd-Stufe."); return -1; }
  uint8_t t[INDEX_HEADER];
  memcpy(t, h, sizeof(t));
  memset(t + 112, 0, 8);
  if(crc32_of(t, sizeof(t)) != g32(h, 116)) { say(why, wn, "Der Kopf des Index ist beschädigt."); return -1; }
  if(g16(h, 18) || g64(h, 104) || g64(h, 120)) { say(why, wn, "Der Index hat unbekannte Felder."); return -1; }
  uint64_t nf = g64(h, 40), nc = g64(h, 48), fo = g64(h, 56), co = g64(h, 64), vo = g64(h, 72), so = g64(h, 80), sl = g64(h, 88);
  unsigned nv = g32(h, 20);
  if(nf == 0 || nf > MAX_FILES || nv == 0 || nv > MAX_VOLUMES || nc > 0xFFFFFFFFull) { say(why, wn, "Der Index hat unmögliche Zahlen."); return -1; }
  if(fo != INDEX_HEADER || co != fo + FILE_REC * nf || vo != co + 8 * nc || so != vo + 8ull * nv || so + sl != file_len) {
    say(why, wn, "Die Teile des Index passen nicht zu seiner Länge.");
    return -1;
  }
  x->nf = nf;
  x->nc = nc;
  x->nv = nv;
  x->level = h[17];
  x->logical = g64(h, 96);
  x->strings_len = sl;
  memcpy(x->arc, h + 24, 16);
  return 0;
}

static int
zidx_load(const char *dir, zidx_t *x, char *err, size_t en) {
  memset(x, 0, sizeof(*x));
  char p[PATH_MAX + 32];
  snprintf(p, sizeof(p), "%s/" ZSP_INDEX_NAME, dir);
  int fd = open(p, O_RDONLY);
  struct stat st;
  if(fd < 0 || fstat(fd, &st) != 0) { if(fd >= 0) close(fd); say(err, en, "Der Index ließ sich nicht öffnen (%s).", strerror(errno)); return -1; }
  if(st.st_size < (off_t)INDEX_HEADER || (uint64_t)st.st_size > INDEX_MAX) { close(fd); say(err, en, "Der Index hat eine unmögliche Größe."); return -1; }
  x->len = (size_t)st.st_size;
  x->buf = malloc(x->len);
  if(!x->buf) { close(fd); say(err, en, "Kein Speicher."); return -1; }
  int rr = read_all(fd, x->buf, x->len, 0);
  close(fd);
  if(rr != 0) { say(err, en, "Der Index ließ sich nicht lesen (%s).", strerror(errno)); zidx_free(x); return -1; }
  uint8_t *b = x->buf;
  size_t len = x->len;
  if(header_check(b, len, x, err, en) != 0) { zidx_free(x); return -1; }
  if(crc32_of(b + INDEX_HEADER, len - INDEX_HEADER) != g32(b, 112)) { say(err, en, "Der Index ist beschädigt (Prüfsumme)."); zidx_free(x); return -1; }
  x->files = b + INDEX_HEADER;
  x->chunks = x->files + FILE_REC * x->nf;
  x->vols = x->chunks + 8 * x->nc;
  x->strings = x->vols + 8ull * x->nv;
  return 0;
}

/* the sizes the index calls a file's own chunks: a file of size has ceil(size / 256 KiB) chunks */
static uint64_t chunks_of(uint64_t size) { return (size + CHUNK_SIZE - 1) >> CHUNK_SHIFT; }

/* the records of the index against each other and the strings: sorted, hashes right, chunk numbers consecutive */
static int
records_check(const zidx_t *x, char *err, size_t en) {
  uint64_t next_chunk = 0, logical = 0, prev_hash = 0;
  const uint8_t *prev_path = NULL;
  size_t prev_len = 0;
  for(uint64_t i = 0; i < x->nf; i++) {
    const uint8_t *r = x->files + FILE_REC * i;
    uint64_t hash = g64(r, 0), size = g64(r, 8), first = g32(r, 16), poff = g32(r, 20), plen = g16(r, 24);
    if(size == 0 || first != next_chunk || (g16(r, 26) & ~1u) || g32(r, 28)) { say(err, en, "Der Index ist unstimmig (Datei %llu).", (unsigned long long)i); return -1; }
    if(poff + plen > x->strings_len) { say(err, en, "Der Index ist unstimmig (Pfad außerhalb)."); return -1; }
    const uint8_t *path = x->strings + poff;
    if(!path_ok((const char *)path, plen) || path_hash(path, plen) != hash) { say(err, en, "Der Index ist unstimmig (Pfad oder Hash der Datei %llu).", (unsigned long long)i); return -1; }
    if(prev_path) {
      int c = hash < prev_hash ? -1 : hash > prev_hash ? 1 : 0;
      if(!c) {
        size_t m = plen < prev_len ? plen : prev_len;
        c = memcmp(path, prev_path, m);
        if(!c) c = plen < prev_len ? -1 : plen > prev_len ? 1 : 0;
      }
      if(c <= 0) { say(err, en, "Der Index ist nicht sortiert."); return -1; }
    }
    prev_hash = hash;
    prev_path = path;
    prev_len = plen;
    next_chunk += chunks_of(size);
    logical += size;
  }
  if(next_chunk != x->nc || logical != x->logical) { say(err, en, "Der Index ist unstimmig (Summen)."); return -1; }
  return 0;
}

typedef struct {
  int       fd;
  unsigned  id;
  uint64_t  size;
} zvol_t;

static void
zvol_close(zvol_t *v) {
  if(v->fd >= 0) close(v->fd);
  v->fd = -1;
}

static int
zvol_open(const char *dir, const zidx_t *x, unsigned id, zvol_t *v, char *err, size_t en) {
  zvol_close(v);
  char p[PATH_MAX + 64], vn[48];
  vol_final_name(dir, id, p, sizeof(p));
  snprintf(vn, sizeof(vn), ZSP_VOLUME_FMT, id);                 /* the name in the messages, whole, so that each message can be translated */
  v->fd = open(p, O_RDONLY);
  struct stat st;
  if(v->fd < 0 || fstat(v->fd, &st) != 0) { say(err, en, "Eine Paketdatei fehlt oder ließ sich nicht öffnen: %s", vn); zvol_close(v); return -1; }
  uint64_t want = g64(x->vols, 8ull * id);
  uint8_t h[VOLUME_HEADER];
  if((uint64_t)st.st_size != want || want < VOLUME_HEADER) { say(err, en, "Die Paketdatei %s hat nicht die Größe, die der Index nennt.", vn); zvol_close(v); return -1; }
  if(read_all(v->fd, h, sizeof(h), 0) != 0) { say(err, en, "Der Kopf von %s ließ sich nicht lesen.", vn); zvol_close(v); return -1; }
  uint8_t t[VOLUME_HEADER];
  memcpy(t, h, sizeof(t));
  memset(t + 60, 0, 4);
  if(memcmp(h, k_volume_magic, 8) || g32(h, 8) != 1 || g32(h, 12) != VOLUME_HEADER || g32(h, 16) != id || g32(h, 20) != x->nv ||
     memcmp(h + 24, x->arc, 16) || g64(h, 40) != want - VOLUME_HEADER || crc32_of(t, sizeof(t)) != g32(h, 60)) {
    say(err, en, "Der Kopf von %s passt nicht zum Index.", vn);
    zvol_close(v);
    return -1;
  }
  v->id = id;
  v->size = want;
  return 0;
}

/* the callbacks of a walk over the decoded files: begin (a file starts), data (a decoded chunk at an offset of the file), end */
typedef struct {
  int (*begin)(void *ctx, size_t fi, const char *rel, uint64_t size, char *err, size_t en);
  int (*data)(void *ctx, uint64_t off, const uint8_t *d, uint32_t n, char *err, size_t en);
  int (*end)(void *ctx, char *err, size_t en);
  void *ctx;
} zwalk_cb_t;

/* Decodes every chunk of the set in file order, checks each against the format and hands it on. */
static int
zsp_walk(const char *dir, const zidx_t *x, const zwalk_cb_t *cb, lzp_stats_t *st, const lzp_ui_t *ui, char *err, size_t en) {
  int rc = -1;
  zvol_t vol = { -1, 0, 0 };
  uint8_t *stored = malloc(CHUNK_SIZE), *raw = malloc(CHUNK_SIZE);
  ZSTD_DCtx *dctx = ZSTD_createDCtx();
  if(!stored || !raw || !dctx) { say(err, en, "Kein Speicher."); goto out; }
  /* every volume must be there with the size and the header the index says */
  for(unsigned v = 0; v < x->nv; v++) {
    if(zvol_open(dir, x, v, &vol, err, en) != 0) goto out;
    zvol_close(&vol);
  }
  vol.fd = -1;
  ui_total(ui, x->logical);
  ui_files(ui, 0, (unsigned)x->nf);
  int have_vol = 0;
  uint64_t chunk_no = 0;
  for(uint64_t fi = 0; fi < x->nf; fi++) {
    const uint8_t *r = x->files + FILE_REC * fi;
    uint64_t size = g64(r, 8);
    uint64_t poff = g32(r, 20), plen = g16(r, 24);
    char rel[PATH_MAX];
    if(plen >= sizeof(rel)) { say(err, en, "Ein Pfad im Index ist zu lang."); goto out; }
    memcpy(rel, x->strings + poff, plen);
    rel[plen] = 0;
    ui_current(ui, rel);
    if(cb && cb->begin && cb->begin(cb->ctx, (size_t)fi, rel, size, err, en) != 0) goto out;
    for(uint64_t off = 0; off < size; off += CHUNK_SIZE, chunk_no++) {
      uint32_t want = (uint32_t)(size - off < CHUNK_SIZE ? size - off : CHUNK_SIZE);
      uint64_t w = g64(x->chunks, 8 * chunk_no);
      uint64_t coff = w & 0xFFFFFFFFull;
      unsigned cvol = (unsigned)((w >> 32) & 0xFFF);
      uint32_t clen = (uint32_t)(((w >> 44) & 0x3FFFF) + 1);
      unsigned codec = (unsigned)(w >> 62);
      if(cvol >= x->nv || codec > 1 || clen > want) { say(err, en, "Ein Block im Index ist ungültig: %s", rel); goto out; }
      if(!have_vol || vol.id != cvol) {
        if(zvol_open(dir, x, cvol, &vol, err, en) != 0) goto out;
        have_vol = 1;
      }
      if(coff < VOLUME_HEADER || coff + clen > vol.size) { say(err, en, "Ein Block liegt außerhalb seiner Paketdatei: %s", rel); goto out; }
      if(read_all(vol.fd, stored, clen, coff) != 0) { say(err, en, "Eine Paketdatei ließ sich nicht lesen (%s): %s", strerror(errno), rel); goto out; }
      const uint8_t *data = stored;
      if(codec == 0) {
        if(clen != want) { say(err, en, "Ein roher Block hat die falsche Länge: %s", rel); goto out; }
        st->chunks_raw++;
      } else {
        ZSTD_frameHeader fh;
        if(clen >= want || ZSTD_getFrameHeader(&fh, stored, clen) != 0 || fh.frameType != ZSTD_frame || fh.frameContentSize != want ||
           !fh.checksumFlag || fh.dictID != 0 || fh.windowSize > CHUNK_SIZE) {
          say(err, en, "Ein zstd-Block entspricht nicht dem Format: %s", rel);
          goto out;
        }
        size_t got = ZSTD_decompressDCtx(dctx, raw, CHUNK_SIZE, stored, clen);
        if(ZSTD_isError(got) || got != want) { say(err, en, "Ein zstd-Block ist beschädigt: %s", rel); goto out; }
        data = raw;
        st->chunks_lz4++;
      }
      if(cb && cb->data && cb->data(cb->ctx, off, data, want, err, en) != 0) goto out;
      st->chunks++;
      st->stored_bytes += clen;
      st->logical_bytes += want;
      ui_work(ui, want);
      if(cancelled(ui)) { say(err, en, "Abgebrochen."); rc = ECANCELED; goto out; }
    }
    if(cb && cb->end && cb->end(cb->ctx, err, en) != 0) goto out;
    st->files_packed++;
    ui_files(ui, (unsigned)(fi + 1), (unsigned)x->nf);
  }
  st->packs = x->nv;
  rc = 0;
out:
  zvol_close(&vol);
  free(stored);
  free(raw);
  if(dctx) ZSTD_freeDCtx(dctx);
  return rc;
}

int
zsp_info(const char *dir, lzp_info_t *info, int *level) {
  memset(info, 0, sizeof(*info));
  char p[PATH_MAX + 32];
  snprintf(p, sizeof(p), "%s/" ZSP_INDEX_NAME, dir);
  struct stat st;
  if(stat(p, &st) != 0 || !S_ISREG(st.st_mode)) return 0;
  int fd = open(p, O_RDONLY);
  if(fd < 0 || st.st_size < (off_t)INDEX_HEADER) { if(fd >= 0) close(fd); say(info->why, sizeof(info->why), "Der Index der zstd-Pakete ließ sich nicht lesen."); return 0; }
  uint8_t h[INDEX_HEADER];
  zidx_t x;
  memset(&x, 0, sizeof(x));
  int ok = read_all(fd, h, sizeof(h), 0) == 0 && header_check(h, (uint64_t)st.st_size, &x, info->why, sizeof(info->why)) == 0;
  uint64_t stored = 0;
  if(ok) {
    uint8_t vt[8];
    uint64_t vo = g64(h, 72);
    for(unsigned i = 0; ok && i < x.nv; i++) {
      if(read_all(fd, vt, 8, vo + 8ull * i) != 0) { ok = 0; say(info->why, sizeof(info->why), "Der Index ließ sich nicht lesen."); break; }
      uint64_t s = g64(vt, 0);
      stored += s > VOLUME_HEADER ? s - VOLUME_HEADER : 0;
    }
  }
  close(fd);
  if(!ok) { if(!info->why[0]) say(info->why, sizeof(info->why), "Der Index der zstd-Pakete ließ sich nicht lesen."); return 0; }
  info->packed = 1;
  info->files_packed = x.nf;
  info->chunks = x.nc;
  info->logical_bytes = x.logical;
  info->stored_bytes = stored;
  info->volumes = x.nv;
  for(int i = 0; i < 16; i++) snprintf(info->build_id + 2 * i, 3, "%02x", x.arc[i]);
  if(level) *level = x.level;
  return 1;
}

/* ---------------------------------------------------------------- verify */

typedef struct {
  const char *srcroot;
  int         sfd;
  uint64_t    ssize;
  char        rel[PATH_MAX];
  uint8_t    *cmp;
} vctx_t;

static int
v_begin(void *c, size_t fi, const char *rel, uint64_t size, char *err, size_t en) {
  (void)fi;
  vctx_t *v = c;
  if(!v->srcroot) return 0;
  char p[2 * PATH_MAX + 8];
  if(snprintf(p, sizeof(p), "%s/%s", v->srcroot, rel) >= (int)sizeof(p)) { say(err, en, "Ein Pfad ist zu lang: %s", rel); return -1; }
  v->sfd = open(p, O_RDONLY);
  struct stat st;
  if(v->sfd < 0 || fstat(v->sfd, &st) != 0) { say(err, en, "Die Datei fehlt oder ließ sich nicht lesen: %s", rel); return -1; }
  if((uint64_t)st.st_size != size) { say(err, en, "Die Datei hat nicht die Größe, die das Paket nennt: %s", rel); return -1; }
  snprintf(v->rel, sizeof(v->rel), "%s", rel);
  return 0;
}

static int
v_data(void *c, uint64_t off, const uint8_t *d, uint32_t n, char *err, size_t en) {
  vctx_t *v = c;
  if(!v->srcroot) return 0;
  if(read_all(v->sfd, v->cmp, n, off) != 0) { say(err, en, "Die Datei ließ sich nicht lesen: %s", v->rel); return -1; }
  if(memcmp(v->cmp, d, n) != 0) { say(err, en, "Die Datei weicht vom Paket ab: %s", v->rel); return -1; }
  return 0;
}

static int
v_end(void *c, char *err, size_t en) {
  (void)err; (void)en;
  vctx_t *v = c;
  if(v->sfd >= 0) { close(v->sfd); v->sfd = -1; }
  return 0;
}

int
zsp_verify(const char *dir, const char *srcroot, lzp_stats_t *st, const lzp_ui_t *ui, char *err, size_t en) {
  lzp_stats_t zero_stats;
  if(!st) st = &zero_stats;
  memset(st, 0, sizeof(*st));
  zidx_t x;
  ui_phase(ui, "Der Index wird gelesen");
  if(zidx_load(dir, &x, err, en) != 0) return -1;
  if(records_check(&x, err, en) != 0) { zidx_free(&x); return -1; }
  vctx_t v = { srcroot, -1, 0, "", NULL };
  v.cmp = srcroot ? malloc(CHUNK_SIZE) : NULL;
  if(srcroot && !v.cmp) { zidx_free(&x); say(err, en, "Kein Speicher."); return -1; }
  zwalk_cb_t cb = { v_begin, v_data, v_end, &v };
  ui_phase(ui, srcroot ? "Die Pakete werden gelesen und mit den Originalen verglichen" : "Die Pakete werden gelesen und geprüft");
  int rc = zsp_walk(dir, &x, &cb, st, ui, err, en);
  if(v.sfd >= 0) close(v.sfd);
  free(v.cmp);
  zidx_free(&x);
  return rc;
}

/* ---------------------------------------------------------------- unpack */

typedef struct {
  const char *dest;
  int         fd;
  char        tmp[2 * PATH_MAX + 16], fin[2 * PATH_MAX + 8];
} uctx_t;

static int
u_begin(void *c, size_t fi, const char *rel, uint64_t size, char *err, size_t en) {
  (void)fi; (void)size;
  uctx_t *u = c;
  if(snprintf(u->fin, sizeof(u->fin), "%s/%s", u->dest, rel) >= (int)sizeof(u->fin) ||
     snprintf(u->tmp, sizeof(u->tmp), "%s" LZP_PART_SUFFIX, u->fin) >= (int)sizeof(u->tmp)) {
    say(err, en, "Ein Pfad ist zu lang: %s", rel);
    return -1;
  }
  char dirp[sizeof(u->fin)];
  snprintf(dirp, sizeof(dirp), "%s", u->fin);
  char *sl = strrchr(dirp, '/');
  if(sl && sl != dirp) { *sl = 0; if(mkdir_p(dirp) != 0) { say(err, en, "Ein Ordner ließ sich nicht anlegen: %s", rel); return -1; } }
  u->fd = open(u->tmp, O_WRONLY | O_CREAT | O_TRUNC, LZP_FILE_MODE);
  if(u->fd < 0) { say(err, en, "Eine Datei ließ sich nicht anlegen (%s): %s", strerror(errno), rel); return -1; }
  (void)fchmod(u->fd, LZP_FILE_MODE);
  return 0;
}

static int
u_data(void *c, uint64_t off, const uint8_t *d, uint32_t n, char *err, size_t en) {
  (void)off;
  uctx_t *u = c;
  if(write_all(u->fd, d, n) != 0) { say(err, en, "Schreiben schlug fehl (%s): %s", strerror(errno), u->fin); return -1; }
  return 0;
}

static int
u_end(void *c, char *err, size_t en) {
  uctx_t *u = c;
  int r = fsync(u->fd) == 0 ? 0 : -1;
  if(close(u->fd) != 0) r = -1;
  u->fd = -1;
  if(r != 0 || rename(u->tmp, u->fin) != 0) { say(err, en, "Eine Datei ließ sich nicht fertig schreiben (%s): %s", strerror(errno), u->fin); unlink(u->tmp); return -1; }
  return 0;
}

int
zsp_unpack(const char *dir, const char *destroot, lzp_stats_t *st, const lzp_ui_t *ui, char *err, size_t en) {
  lzp_stats_t zero_stats;
  if(!st) st = &zero_stats;
  memset(st, 0, sizeof(*st));
  zidx_t x;
  ui_phase(ui, "Der Index wird gelesen");
  if(zidx_load(dir, &x, err, en) != 0) return -1;
  if(records_check(&x, err, en) != 0) { zidx_free(&x); return -1; }
  uctx_t u;
  memset(&u, 0, sizeof(u));
  u.dest = destroot;
  u.fd = -1;
  zwalk_cb_t cb = { u_begin, u_data, u_end, &u };
  ui_phase(ui, "Die Dateien werden aus den Paketen geschrieben");
  int rc = zsp_walk(dir, &x, &cb, st, ui, err, en);
  if(u.fd >= 0) { close(u.fd); unlink(u.tmp); }
  zidx_free(&x);
  return rc;
}

/* ---------------------------------------------------------------- list, remove */

static int
cmp_str(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

int
zsp_list(const char *dir, lzp_list_t *out, char *err, size_t en) {
  out->rel = NULL;
  out->n = 0;
  zidx_t x;
  if(zidx_load(dir, &x, err, en) != 0) return -1;
  char **a = calloc(x.nf ? x.nf : 1, sizeof(char *));
  if(!a) { zidx_free(&x); say(err, en, "Kein Speicher."); return -1; }
  size_t n = 0;
  for(uint64_t i = 0; i < x.nf; i++) {
    const uint8_t *r = x.files + FILE_REC * i;
    uint64_t poff = g32(r, 20), plen = g16(r, 24);
    if(poff + plen > x.strings_len) continue;
    char *s = malloc(plen + 1);
    if(!s) { for(size_t k = 0; k < n; k++) free(a[k]); free(a); zidx_free(&x); say(err, en, "Kein Speicher."); return -1; }
    memcpy(s, x.strings + poff, plen);
    s[plen] = 0;
    a[n++] = s;
  }
  qsort(a, n, sizeof(char *), cmp_str);
  out->rel = a;
  out->n = n;
  zidx_free(&x);
  return 0;
}

int
zsp_remove_sources(const char *dir, const char *root, int empty_dirs, lzp_stats_t *st, const lzp_ui_t *ui, char *err, size_t en) {
  lzp_stats_t zero_stats;
  if(!st) st = &zero_stats;
  memset(st, 0, sizeof(*st));
  zidx_t x;
  if(zidx_load(dir, &x, err, en) != 0) return -1;
  /* the paths are checked like everywhere else the index is read (no "..", no leading "/"): this one deletes files */
  if(records_check(&x, err, en) != 0) { zidx_free(&x); return -1; }
  int rc = -1;
  ui_phase(ui, "Die Originale werden entfernt");
  ui_files(ui, 0, (unsigned)x.nf);
  for(uint64_t i = 0; i < x.nf; i++) {
    const uint8_t *r = x.files + FILE_REC * i;
    uint64_t size = g64(r, 8), poff = g32(r, 20), plen = g16(r, 24);
    char rel[PATH_MAX];
    if(poff + plen > x.strings_len || plen >= sizeof(rel)) { say(err, en, "Der Index ist unstimmig."); goto out; }
    memcpy(rel, x.strings + poff, plen);
    rel[plen] = 0;
    if(zsp_stays_loose(rel, size)) continue;             /* programs, system folders, the packs: never removed */
    char p[2 * PATH_MAX + 8];
    if(snprintf(p, sizeof(p), "%s/%s", root, rel) >= (int)sizeof(p)) { say(err, en, "Ein Pfad ist zu lang: %s", rel); goto out; }
    struct stat s;
    if(lstat(p, &s) != 0) {
      if(errno == ENOENT) continue;                      /* already gone */
      say(err, en, "Eine Datei ließ sich nicht ansehen (%s): %s", strerror(errno), rel);
      goto out;
    }
    if(!S_ISREG(s.st_mode) || (uint64_t)s.st_size != size) { say(err, en, "Die Datei ist keine reguläre Datei der Größe aus dem Paket, sie bleibt: %s", rel); goto out; }
    if(unlink(p) != 0) { say(err, en, "Eine Datei ließ sich nicht entfernen (%s): %s", strerror(errno), rel); goto out; }
    st->files_packed++;
    st->logical_bytes += size;
    if(empty_dirs) {
      char *sl;
      while((sl = strrchr(p, '/')) && (size_t)(sl - p) > strlen(root)) {
        *sl = 0;
        if(rmdir(p) != 0) break;                         /* not empty: stays */
      }
    }
    if((i & 63) == 0) {
      ui_files(ui, (unsigned)i, (unsigned)x.nf);
      if(cancelled(ui)) { say(err, en, "Abgebrochen."); rc = ECANCELED; goto out; }
    }
  }
  rc = 0;
out:
  zidx_free(&x);
  return rc;
}

int
zsp_remove_set(const char *dir, char *err, size_t en) {
  DIR *d = opendir(dir);
  if(!d) { say(err, en, "Der Spielordner ließ sich nicht lesen (%s).", strerror(errno)); return -1; }
  int rc = 0;
  struct dirent *e;
  while((e = readdir(d))) {
    char base[NAME_MAX + 1];
    snprintf(base, sizeof(base), "%s", e->d_name);
    size_t l = strlen(base), pl = strlen(LZP_PART_SUFFIX);
    if(l > pl && !strcmp(base + l - pl, LZP_PART_SUFFIX)) base[l - pl] = 0;      /* what a failed run left under temporary names */
    if(strcasecmp(base, ZSP_INDEX_NAME) && !volume_name(base)) continue;
    char p[PATH_MAX + NAME_MAX + 2];
    snprintf(p, sizeof(p), "%s/%s", dir, e->d_name);
    if(unlink(p) != 0 && errno != ENOENT) { say(err, en, "Eine Paketdatei ließ sich nicht entfernen (%s): %s", strerror(errno), e->d_name); rc = -1; }
  }
  closedir(d);
  return rc;
}
