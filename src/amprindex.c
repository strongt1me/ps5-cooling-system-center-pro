/* The AMPR index of a game (11.10.2026): /app0/ampr_emu.index, the list of all files of a game that APR-EMU uses to
 * find them by name, without regard to case.
 *
 * The format and the way it is built are a C port of build_ampr_index.py from drakmor's ampr_emu
 * (https://github.com/drakmor/ampr_emu, tools/build_ampr_index.py, GPL-3.0, read on 11.10.2026):
 *
 *   header   "AMPRIDX3", version 3, record size 24, count, path blob size, hash table offset, slot size 16, slots
 *            (little endian: 8s I I Q Q Q I I = 48 bytes)
 *   records  sorted by the key of the path: path offset (u32), path length (u32), size (u64), mtime in seconds (s64)
 *   blob     the paths "/app0/...", each with a NUL
 *   padding  up to a multiple of 16
 *   slots    an open-addressed hash table, a power of two >= 2 * count: FNV-1a 64 of the key (0 becomes 1) (u64),
 *            index of the record + 1 (u32), flags (u32, 1 = another path has the same hash)
 *
 * The key of a path is its UTF-8 bytes with "\" as "/" and only ASCII A-Z folded to a-z. Left out: the index itself
 * (and its .tmp and .bak), ampr_commands.bin and apr_emu.log at the top, paths with a tab or a line break. Two paths
 * that differ only in case: the first one (sorted folders, top-down) is kept, as the script does with
 * --allow-case-collisions; the page says how many.
 *
 * Where: a dump folder is written in place. An image is mounted read-write through ShadowMountPlus for the job
 * (the user allowed that use of its interface on 11.10.2026) and released again, unless it was mounted before.
 * A compressed .ffpfsc cannot be written; its index is made when the folder is converted (gameconvert.c asks
 * ps5tm_ampr_build_dir before packing). The old index stays as ampr_emu.index.bak. */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "imgread.h"
#include "ioerr.h"
#include "ps5tm.h"
#include "third_party/cJSON.h"

#define AI_NAME        "ampr_emu.index"
#define AI_MAX_FILES   1000000u
#define AI_MAX_DEPTH   64
#define AI_PATH        1024

typedef struct {
  char    *path;                       /* "/app0/..." */
  char    *key;                        /* the folded form, for sorting and hashing */
  uint64_t size;
  int64_t  mtime;
  size_t   seq;                        /* visiting order: of two equal keys the first one stays */
} ai_row_t;

typedef struct {
  ai_row_t *rows;
  size_t    n, cap;
  size_t    collisions;
  size_t    skipped;
  int       err;
  char      why[256];
  const int *cancel;
} ai_walk_t;

/* ------------------------------------------------------------------ building */

static char *
make_key(const char *path) {
  size_t n = strlen(path);
  char *k = malloc(n + 1);
  if(!k) return NULL;
  for(size_t i = 0; i < n; i++) {
    unsigned char c = (unsigned char)path[i];
    if(c == '\\') c = '/';
    if(c >= 'A' && c <= 'Z') c = (unsigned char)(c + 0x20);
    k[i] = (char)c;
  }
  k[n] = 0;
  return k;
}

static uint64_t
fnv1a64(const char *key) {
  uint64_t h = 1469598103934665603ull;
  for(const unsigned char *p = (const unsigned char *)key; *p; p++) {
    h ^= *p;
    h *= 1099511628211ull;
  }
  return h ? h : 1;
}

static int
cmp_key(const void *a, const void *b) {
  const ai_row_t *x = a, *y = b;
  int r = strcmp(x->key, y->key);                    /* bytes, unsigned, like Python's bytes */
  return r ? r : (x->seq < y->seq ? -1 : x->seq > y->seq);
}

static int
cmp_name(const void *a, const void *b) {
  char *ka = make_key(*(char *const *)a), *kb = make_key(*(char *const *)b);
  int r = (ka && kb) ? strcmp(ka, kb) : 0;
  free(ka);
  free(kb);
  return r;
}

/* The names of one folder, sorted by key (like the script's os.walk with sorted lists). */
static char **
list_sorted(const char *dir, size_t *count) {
  DIR *d = opendir(dir);
  if(!d) return NULL;
  size_t n = 0, cap = 64;
  char **v = malloc(cap * sizeof(*v));
  struct dirent *e;
  while(v && (errno = 0, e = readdir(d)) != NULL) {
    if(!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
    if(n == cap) {
      char **nv = realloc(v, (cap *= 2) * sizeof(*v));
      if(!nv) { for(size_t i = 0; i < n; i++) free(v[i]); free(v); v = NULL; break; }
      v = nv;
    }
    if(!(v[n] = strdup(e->d_name))) break;
    n++;
  }
  int rerr = errno;
  closedir(d);
  if(!v) return NULL;
  if(rerr) { for(size_t i = 0; i < n; i++) free(v[i]); free(v); errno = rerr; return NULL; }
  qsort(v, n, sizeof(*v), cmp_name);
  *count = n;
  return v;
}

static void free_list(char **v, size_t n) { for(size_t i = 0; i < n; i++) free(v[i]); free(v); }

static void
walk(ai_walk_t *w, const char *root, const char *rel, int depth) {
  if(w->err) return;
  if(w->cancel && __atomic_load_n(w->cancel, __ATOMIC_ACQUIRE)) { w->err = ECANCELED; return; }
  if(depth > AI_MAX_DEPTH) { w->err = ELOOP; snprintf(w->why, sizeof(w->why), "Zu tief verschachtelt: %.180s", rel); return; }
  char dir[AI_PATH];
  if(snprintf(dir, sizeof(dir), "%s%s", root, rel) >= (int)sizeof(dir)) { w->err = ENAMETOOLONG; return; }
  size_t n = 0;
  char **names = list_sorted(dir, &n);
  if(!names) { w->err = errno ? errno : EIO; snprintf(w->why, sizeof(w->why), "Ordner nicht lesbar: %.180s", rel[0] ? rel : "/"); return; }
  /* files of this folder first, then its folders: the order os.walk visits them in */
  for(int pass = 0; pass < 2 && !w->err; pass++) {
    for(size_t i = 0; i < n && !w->err; i++) {
      char sub[AI_PATH], full[AI_PATH];
      if(snprintf(sub, sizeof(sub), "%s/%s", rel, names[i]) >= (int)sizeof(sub) ||
         snprintf(full, sizeof(full), "%s%s", root, sub) >= (int)sizeof(full)) { w->err = ENAMETOOLONG; break; }
      struct stat ls;
      if(lstat(full, &ls) != 0) {
        /* gone meanwhile is fine; any other error would leave the file out of the index, and APR-EMU would miss it while the
           game runs (review 15.10.2026) */
        if(errno == ENOENT) continue;
        w->err = errno ? errno : EIO;
        snprintf(w->why, sizeof(w->why), "Eintrag nicht lesbar (%.60s): %.150s", ps5tm_io_strerror(w->err), sub);
        break;
      }
      if(pass == 1) {
        if(S_ISDIR(ls.st_mode)) walk(w, root, sub, depth + 1);
        continue;
      }
      if(S_ISDIR(ls.st_mode)) continue;
      struct stat st;
      if(stat(full, &st) != 0) {
        if(errno == ENOENT || S_ISLNK(ls.st_mode)) continue;   /* a link into nothing, as os.walk + os.stat skip it */
        w->err = errno ? errno : EIO;
        snprintf(w->why, sizeof(w->why), "Datei nicht lesbar (%.60s): %.150s", ps5tm_io_strerror(w->err), sub);
        break;
      }
      if(!S_ISREG(st.st_mode)) continue;
      char app0[AI_PATH + 8];
      snprintf(app0, sizeof(app0), "/app0%s", sub);
      /* the index itself and the two files APR-EMU writes at the top */
      if(depth == 0) {
        char *k = make_key(names[i]);
        int skip = k && (!strcmp(k, AI_NAME) || !strcmp(k, AI_NAME ".tmp") || !strcmp(k, AI_NAME ".bak") ||
                         !strcmp(k, "ampr_commands.bin") || !strcmp(k, "apr_emu.log"));
        free(k);
        if(skip) continue;
      }
      if(strpbrk(app0, "\t\r\n")) { w->skipped++; continue; }
      if(w->n >= AI_MAX_FILES) { w->err = EFBIG; snprintf(w->why, sizeof(w->why), "Mehr als %u Dateien.", AI_MAX_FILES); break; }
      if(w->n == w->cap) {
        size_t nc = w->cap ? w->cap * 2 : 4096;
        ai_row_t *nr = realloc(w->rows, nc * sizeof(*nr));
        if(!nr) { w->err = ENOMEM; break; }
        w->rows = nr;
        w->cap = nc;
      }
      ai_row_t *r = &w->rows[w->n];
      r->path = strdup(app0);
      r->key = make_key(app0);
      if(!r->path || !r->key) { free(r->path); free(r->key); w->err = ENOMEM; break; }
      r->size = (uint64_t)st.st_size;
      r->mtime = (int64_t)st.st_mtime;
      r->seq = w->n;
      w->n++;
    }
  }
  free_list(names, n);
}

static void
free_rows(ai_walk_t *w) {
  for(size_t i = 0; i < w->n; i++) { free(w->rows[i].path); free(w->rows[i].key); }
  free(w->rows);
  w->rows = NULL;
  w->n = w->cap = 0;
}

static void put32(uint8_t *p, uint32_t v) { for(int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i)); }
static void put64(uint8_t *p, uint64_t v) { for(int i = 0; i < 8; i++) p[i] = (uint8_t)(v >> (8 * i)); }

static int
write_all(int fd, const void *buf, size_t n) {
  const uint8_t *p = buf;
  while(n) {
    ssize_t k = write(fd, p, n);
    if(k < 0 && errno == EINTR) continue;
    if(k <= 0) return errno ? errno : EIO;
    p += k;
    n -= (size_t)k;
  }
  return 0;
}

/* Builds the index of the tree at root into root/ampr_emu.index. 0, the number of files in *files; else an errno and
   the reason in why. The old index is kept as ampr_emu.index.bak. */
int
ps5tm_ampr_build_dir(const char *root, unsigned *files, unsigned *collisions, const int *cancel, char *why, size_t why_len) {
  if(why && why_len) why[0] = 0;
  ai_walk_t w;
  memset(&w, 0, sizeof(w));
  w.cancel = cancel;
  walk(&w, root, "", 0);
  if(w.err) {
    if(why && why_len) snprintf(why, why_len, "%s", w.why[0] ? w.why : strerror(w.err));
    int e = w.err;
    free_rows(&w);
    return e;
  }
  if(w.n == 0) {                       /* drakmor's script refuses an empty tree too (no table without entries) */
    free_rows(&w);
    if(why && why_len) snprintf(why, why_len, "Im Spielordner liegen keine Dateien.");
    return ENOENT;
  }
  qsort(w.rows, w.n, sizeof(*w.rows), cmp_key);
  /* paths that differ only in case: after sorting they are neighbours, the first visited one first (seq) – it stays */
  size_t out = 0;
  for(size_t i = 0; i < w.n; i++) {
    if(out && !strcmp(w.rows[out - 1].key, w.rows[i].key)) {
      w.collisions++;
      free(w.rows[i].path);
      free(w.rows[i].key);
      continue;
    }
    w.rows[out++] = w.rows[i];
  }
  w.n = out;

  /* the blob and the records */
  uint64_t blob = 0;
  for(size_t i = 0; i < w.n; i++) blob += strlen(w.rows[i].path) + 1;
  uint32_t slots = 2;
  while((uint64_t)slots < (uint64_t)w.n * 2) slots <<= 1;
  uint64_t path_end = 48 + (uint64_t)w.n * 24 + blob;
  uint64_t hash_off = (path_end + 15) & ~(uint64_t)15;
  uint64_t total = hash_off + (uint64_t)slots * 16;
  if(w.n > 0xFFFFFFFEu || blob > 0xFFFFFFFFull || total > 512ull * 1024 * 1024) {
    free_rows(&w);
    if(why && why_len) snprintf(why, why_len, "Das Verzeichnis würde zu groß.");
    return EFBIG;
  }
  uint8_t *img = calloc(1, (size_t)total);
  if(!img) { free_rows(&w); return ENOMEM; }
  memcpy(img, "AMPRIDX3", 8);
  put32(img + 8, 3);
  put32(img + 12, 24);
  put64(img + 16, w.n);
  put64(img + 24, blob);
  put64(img + 32, hash_off);
  put32(img + 40, 16);
  put32(img + 44, slots);
  uint64_t off = 0;
  uint8_t *rec = img + 48, *bp = img + 48 + (uint64_t)w.n * 24;
  for(size_t i = 0; i < w.n; i++) {
    size_t l = strlen(w.rows[i].path);
    put32(rec + i * 24, (uint32_t)off);
    put32(rec + i * 24 + 4, (uint32_t)l);
    put64(rec + i * 24 + 8, w.rows[i].size);
    put64(rec + i * 24 + 16, (uint64_t)w.rows[i].mtime);
    memcpy(bp + off, w.rows[i].path, l + 1);
    off += l + 1;
  }
  /* the hash table, filled in sorted order like the script */
  uint8_t *tab = img + hash_off;
  uint32_t mask = slots - 1;
  for(size_t i = 0; i < w.n; i++) {
    uint64_t h = fnv1a64(w.rows[i].key);
    uint32_t pos = (uint32_t)(h & mask);
    int dup = 0;
    for(;;) {
      uint8_t *s = tab + (uint64_t)pos * 16;
      uint32_t used = (uint32_t)s[8] | (uint32_t)s[9] << 8 | (uint32_t)s[10] << 16 | (uint32_t)s[11] << 24;
      if(!used) break;
      uint64_t sh = 0;
      for(int b = 7; b >= 0; b--) sh = (sh << 8) | s[b];
      if(sh == h) { s[12] |= 1; dup = 1; }
      pos = (pos + 1) & mask;
    }
    uint8_t *s = tab + (uint64_t)pos * 16;
    put64(s, h);
    put32(s + 8, (uint32_t)(i + 1));
    put32(s + 12, dup ? 1 : 0);
  }
  unsigned nfiles = (unsigned)w.n, ncoll = (unsigned)w.collisions;
  free_rows(&w);

  /* written beside it, on the disk, the old one kept as .bak, then put in place */
  char tmp[AI_PATH], dst[AI_PATH], bak[AI_PATH];
  snprintf(tmp, sizeof(tmp), "%s/%s.tmp", root, AI_NAME);
  snprintf(dst, sizeof(dst), "%s/%s", root, AI_NAME);
  snprintf(bak, sizeof(bak), "%s/%s.bak", root, AI_NAME);
  int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0777);         /* game files are 0777 (see LZP_FILE_MODE in lz4pack.h) */
  if(fd < 0) {
    int e = errno;
    free(img);
    if(why && why_len) snprintf(why, why_len, "Die Datei ließ sich nicht anlegen: %s", strerror(e));
    return e;
  }
  (void)fchmod(fd, 0777);
  int rc = write_all(fd, img, (size_t)total);
  if(!rc && fsync(fd) != 0) rc = errno ? errno : EIO;
  if(close(fd) != 0 && !rc) rc = errno ? errno : EIO;
  free(img);
  if(rc) {
    unlink(tmp);
    if(why && why_len) snprintf(why, why_len, "Schreiben fehlgeschlagen: %s", strerror(rc));
    return rc;
  }
  struct stat st;
  if(stat(dst, &st) == 0) {
    unlink(bak);
    if(rename(dst, bak) != 0) {
      rc = errno;
      unlink(tmp);
      if(why && why_len) snprintf(why, why_len, "Das alte Verzeichnis ließ sich nicht beiseitelegen: %s", strerror(rc));
      return rc;
    }
  }
  if(rename(tmp, dst) != 0) {
    rc = errno;
    rename(bak, dst);                                          /* the old one back */
    unlink(tmp);
    if(why && why_len) snprintf(why, why_len, "Das Verzeichnis ließ sich nicht einsetzen: %s", strerror(rc));
    return rc;
  }
  if(files) *files = nfiles;
  if(collisions) *collisions = ncoll;
  return 0;
}

/* ------------------------------------------------------------------ reading what is there */

/* 0 and the count when the bytes start like an index. */
static int
header_count(const uint8_t *h, size_t n, uint64_t *count) {
  if(n < 48 || memcmp(h, "AMPRIDX3", 8)) return -1;
  uint64_t c = 0;
  for(int b = 7; b >= 0; b--) c = (c << 8) | h[16 + b];
  *count = c;
  return 0;
}

/* ------------------------------------------------------------------ the job */

enum { AI_IDLE, AI_RUNNING, AI_DONE, AI_FAILED };

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static struct {
  int      state;
  char     kind[8];                    /* "index" or "emu" (amprlib.c: the APR-EMU or PlayGo file of the game) */
  char     lib[8];                     /* emu: "ampr" or "playgo" */
  char     op[10];                     /* emu: apply, restore, remove */
  char     store[96];                  /* emu apply: the version's file name */
  char     id[16];
  char     name[128];
  char     phase[160];
  char     error[400];
  char     note[400];
  unsigned files, collisions;
  uint64_t started_ms, finished_ms;
} g_job;
static int g_cancel;

int
ps5tm_ampr_busy(void) {
  pthread_mutex_lock(&g_lock);
  int b = g_job.state == AI_RUNNING;
  pthread_mutex_unlock(&g_lock);
  return b;
}

static void
phase(const char *p) {
  pthread_mutex_lock(&g_lock);
  snprintf(g_job.phase, sizeof(g_job.phase), "%s", p);
  pthread_mutex_unlock(&g_lock);
}

static void
finish(int state, const char *error) {
  pthread_mutex_lock(&g_lock);
  g_job.state = state;
  snprintf(g_job.error, sizeof(g_job.error), "%s", error ? error : "");
  g_job.finished_ms = ps5tm_mono_ms();
  char name[128], detail[160];
  snprintf(name, sizeof(name), "%s", g_job.name);
  int emu = !strcmp(g_job.kind, "emu");
  if(emu) {
    const char *sp = strstr(g_job.store, ".sprx-");
    const char *lab = sp ? sp + 6 : g_job.store;
    if(!strcmp(g_job.op, "apply")) snprintf(detail, sizeof(detail), "%s eingebaut", lab);
    else if(!strcmp(g_job.op, "restore")) snprintf(detail, sizeof(detail), "Original wiederhergestellt");
    else snprintf(detail, sizeof(detail), "entfernt");
  } else snprintf(detail, sizeof(detail), "%u Dateien", g_job.files);
  uint64_t dur = g_job.finished_ms - g_job.started_ms;
  pthread_mutex_unlock(&g_lock);
  ps5tm_hist_add(emu ? (strcmp(g_job.lib, "playgo") ? "ampr_emu" : "playgo") : "ampr_index", name, state == AI_DONE ? detail : NULL, state == AI_DONE ? "done" : "failed", error, 0, dur);
}

static int
is_image_format(const char *f) {
  return !strcmp(f, "exfat") || !strcmp(f, "ffpkg") || !strcmp(f, "ffpfs") || !strcmp(f, "image");
}

typedef struct { char id[16]; int emu; char lib[8]; char op[10]; char store[96]; } ai_args_t;

static int
smp_mount(const char *id, const char *route, int rw, char *err, size_t n) {
  cJSON *b = cJSON_CreateObject();
  cJSON_AddStringToObject(b, "title_id", id);
  if(rw) cJSON_AddStringToObject(b, "mode", "rw");
  cJSON *a = ps5tm_smp_call(route, b, 90000, err, n);
  cJSON_Delete(b);
  if(!a) return -1;
  cJSON_Delete(a);
  return 0;
}

/* What is done in the root of a game: the index is built there, or the APR-EMU file is changed there. */
typedef struct {
  const ai_args_t *a;
  unsigned         files, coll;
  char             why[400];
  char             note[400];
} ai_ctx_t;

static int
root_job(const char *root, void *vctx) {
  ai_ctx_t *c = vctx;
  if(c->a->emu) {
    int pg = !strcmp(c->a->lib, "playgo");
    phase(!strcmp(c->a->op, "apply") ? (pg ? "Die PlayGo-Datei wird eingebaut" : "Die APR-EMU-Datei wird eingebaut")
                                     : (pg ? "Die PlayGo-Datei wird zurückgesetzt" : "Die APR-EMU-Datei wird zurückgesetzt"));
    return ps5tm_amprlib_run(c->a->lib, c->a->op, c->a->id, root, c->a->store, c->note, sizeof(c->note), c->why, sizeof(c->why));
  }
  phase("Die Dateien des Spiels werden gelesen");
  return ps5tm_ampr_build_dir(root, &c->files, &c->coll, &g_cancel, c->why, sizeof(c->why));
}

static void *
ai_thread(void *arg) {
  ai_args_t *a = arg;
  ps5tm_powerguard_hold();
  ps5tm_libdel_t li;
  ai_ctx_t ctx;
  memset(&ctx, 0, sizeof(ctx));
  ctx.a = a;
  if(ps5tm_library_delete_info(a->id, &li) != 0) { finish(AI_FAILED, "Das Spiel ist nicht mehr in der Liste."); goto out; }
  int rc;
  char place[10] = "game", pwhy[200] = "";
  if(a->emu && ps5tm_amprlib_place(a->lib, a->id, place, sizeof(place), pwhy, sizeof(pwhy)) != 0) { finish(AI_FAILED, "Das Spiel ist nicht mehr in der Liste."); goto out; }
  if(a->emu && !strcmp(place, "backport")) {
    /* a compressed .ffpfsc or an existing backport folder: nothing is mounted, the game stays untouched */
    rc = root_job(NULL, &ctx);
  } else if(!strcmp(li.format, "folder")) {
    rc = root_job(li.path, &ctx);
  } else {
    ps5tm_smp_game_t s;
    if(ps5tm_smp_find(a->id, &s) != 0) { finish(AI_FAILED, "ShadowMountPlus kennt das Spiel gerade nicht."); goto out; }
    int was_mounted = s.mounted;
    char err[300] = "";
    phase("ShadowMountPlus hängt das Abbild zum Schreiben ein");
    if(smp_mount(a->id, "/games/mount", 1, err, sizeof(err)) != 0) {
      char e[400];
      snprintf(e, sizeof(e), "ShadowMountPlus hat das Abbild nicht zum Schreiben eingehängt (%s).", err[0] ? err : "ohne Angabe");
      finish(AI_FAILED, e);
      goto out;
    }
    /* where it is mounted: up to 15 s for the mount point */
    char where[256] = "";
    for(int i = 0; i < 60; i++) {
      ps5tm_smp_forget();
      struct stat st;
      if(ps5tm_smp_find(a->id, &s) == 0 && s.runtime_path[0] == '/' && strcmp(s.runtime_path, s.path) &&
         stat(s.runtime_path, &st) == 0 && S_ISDIR(st.st_mode)) {
        snprintf(where, sizeof(where), "%s", s.runtime_path);
        break;
      }
      usleep(250 * 1000);
    }
    rc = -1;
    if(!where[0]) snprintf(ctx.why, sizeof(ctx.why), "Das eingehängte Abbild war nicht zu finden.");
    else {
      rc = root_job(where, &ctx);
      if(rc == EROFS || rc == EACCES || rc == EPERM) {
        if(a->emu) snprintf(ctx.why, sizeof(ctx.why), !strcmp(a->lib, "playgo") ? "ShadowMountPlus hat das Abbild nur zum Lesen eingehängt. Die PlayGo-Datei lässt sich so nicht ändern."
                                                                                  : "ShadowMountPlus hat das Abbild nur zum Lesen eingehängt. Die APR-EMU-Datei lässt sich so nicht ändern.");
        else snprintf(ctx.why, sizeof(ctx.why), "ShadowMountPlus hat das Abbild nur zum Lesen eingehängt. Der Index entsteht dann beim Konvertieren aus dem Ordner.");
      }
    }
    if(!was_mounted) {
      phase("Das Abbild wird wieder freigegeben");
      if(smp_mount(a->id, "/games/unmount", 0, err, sizeof(err)) != 0)
        PS5TM_WARN("ampr_unmount_failed", "Das Abbild von %s ließ sich nicht wieder freigeben: %s", a->id, err);
    }
    ps5tm_smp_forget();
  }
  if(rc) { finish(AI_FAILED, ctx.why[0] ? ctx.why : (a->emu ? (!strcmp(a->lib, "playgo") ? "Die PlayGo-Datei ließ sich nicht ändern." : "Die APR-EMU-Datei ließ sich nicht ändern.") : "Das Verzeichnis ließ sich nicht bauen.")); goto out; }
  pthread_mutex_lock(&g_lock);
  g_job.files = ctx.files;
  g_job.collisions = ctx.coll;
  snprintf(g_job.note, sizeof(g_job.note), "%s", ctx.note);
  pthread_mutex_unlock(&g_lock);
  ps5tm_library_forget();
  if(a->emu) PS5TM_INFO("ampr_emu_changed", "%s-Datei von %s geändert (%s %s).", !strcmp(a->lib, "playgo") ? "PlayGo" : "APR-EMU", a->id, a->op, a->store);
  else PS5TM_INFO("ampr_index_built", "AMPR-Index für %s gebaut: %u Dateien%s.", a->id, ctx.files, ctx.coll ? " (Namen, die sich nur in Groß-/Kleinschreibung unterscheiden, einmal genommen)" : "");
  finish(AI_DONE, NULL);
out:
  ps5tm_powerguard_release();
  free(a);
  return NULL;
}

/* The index header inside an image. The reading goes through imgread.c (it needs more stack than a connection thread
 * has), so it runs in a short thread of its own with a bigger stack, like the probe in library.c does. */
typedef struct { char path[256]; uint8_t h[48]; long got; } ai_img_t;

static void *
ai_img_thread(void *arg) {
  ai_img_t *x = arg;
  x->got = -1;
  imgr_t *r = imgr_open(x->path, NULL, 0);
  if(r) {
    x->got = imgr_pread(r, "/" AI_NAME, 0, x->h, sizeof(x->h));
    imgr_close(r);
  }
  return NULL;
}

/* 0 and *count when the image holds a valid index. */
static int
ai_img_header(const char *path, uint64_t *count) {
  ai_img_t *x = calloc(1, sizeof(*x));
  if(!x) return -1;
  snprintf(x->path, sizeof(x->path), "%s", path);
  x->got = -1;
  pthread_t th;
  pthread_attr_t at;
  pthread_attr_init(&at);
  pthread_attr_setstacksize(&at, 512 * 1024);
  int rc = -1;
  if(pthread_create(&th, &at, ai_img_thread, x) == 0) {
    pthread_join(th, NULL);
    rc = x->got == (long)sizeof(x->h) && header_count(x->h, sizeof(x->h), count) == 0 ? 0 : -1;
  }
  pthread_attr_destroy(&at);
  free(x);
  return rc;
}

/* What the page shows for a game: where its data lies, whether it has an index, and whether one can be built. */
cJSON *
ps5tm_ampr_info(const char *id, int *http) {
  *http = 200;
  ps5tm_libdel_t li;
  if(!id || ps5tm_library_delete_info(id, &li) != 0) { *http = 404; return NULL; }
  cJSON *o = cJSON_CreateObject();
  cJSON_AddBoolToObject(o, "ok", 1);
  cJSON_AddStringToObject(o, "id", id);
  cJSON_AddStringToObject(o, "name", li.name);
  cJSON_AddStringToObject(o, "format", li.format);
  uint8_t h[48];
  uint64_t count = 0;
  int have = 0;
  long mtime = 0;
  const char *why = NULL;
  int can = 0;
  if(!strcmp(li.format, "folder")) {
    char p[AI_PATH];
    snprintf(p, sizeof(p), "%s/%s", li.path, AI_NAME);
    int fd = open(p, O_RDONLY | O_NONBLOCK);
    if(fd >= 0) {
      struct stat st;
      if(fstat(fd, &st) == 0) mtime = (long)st.st_mtime;
      if(pread(fd, h, sizeof(h), 0) == (ssize_t)sizeof(h) && header_count(h, sizeof(h), &count) == 0) have = 1;
      close(fd);
    }
    can = 1;
    if(ps5tm_lz4_packed_dir(li.path)) {
      can = 0;
      why = "Gesperrt, solange das Spiel in LZ4-Paketen liegt: Die Pakete gehören zu genau diesem Index. Zuerst „LZ4 entpacken“.";
    }
  } else if(is_image_format(li.format) || !strcmp(li.format, "ffpfsc")) {
    if(ai_img_header(li.path, &count) == 0) have = 1;
    if(!strcmp(li.format, "ffpfsc")) why = "Ein komprimiertes Abbild (ffpfsc) lässt sich nicht beschreiben. Der Index entsteht beim Konvertieren aus dem Ordner.";
    else if(!li.smp) why = "Nur für Abbilder, die ShadowMountPlus verwaltet.";
    else if(!ps5tm_smp_available(NULL, 0)) why = "ShadowMountPlus antwortet gerade nicht.";
    else can = 1;
  } else {
    why = !strcmp(li.format, "pkg") ? "Installierte Spiele (PKG) haben keinen Spielordner, in den der Index käme."
                                     : "Wo die Spieldaten liegen, ist nicht bekannt.";
  }
  cJSON_AddBoolToObject(o, "has_index", have);
  if(have) cJSON_AddNumberToObject(o, "files", (double)count);
  if(have && mtime) cJSON_AddNumberToObject(o, "mtime", (double)mtime);
  cJSON_AddBoolToObject(o, "can_build", can);
  if(why) cJSON_AddStringToObject(o, "why", why);
  return o;
}

static int
other_job(char *err, size_t n) {
  if(ps5tm_gamecopy_busy() || ps5tm_gameconvert_busy() || ps5tm_gamemove_busy() || ps5tm_saves_busy() ||
     ps5tm_pkgsplit_busy() || ps5tm_pkginst_busy() || ps5tm_gamedelete_busy() || ps5tm_filemgr_busy() || ps5tm_imgjob_busy()) {
    snprintf(err, n, "Es läuft gerade ein anderer Vorgang der App. Bitte danach noch einmal.");
    return 1;
  }
  return 0;
}

static int ai_start(const char *kind, const char *lib, const char *op, const char *store, const char *id, char *err, size_t n);

int
ps5tm_ampr_start(const char *id, char *err, size_t n) {
  return ai_start("index", "", "", "", id, err, n);
}

/* The APR-EMU or PlayGo file of a game (amprlib.c; lib "ampr" or "playgo"): op "apply" (store = the version's file name),
   "restore" or "remove". */
int
ps5tm_ampr_emu_start(const char *lib, const char *op, const char *store, const char *id, char *err, size_t n) {
  if(!lib || !lib[0]) lib = "ampr";
  if(strcmp(lib, "ampr") && strcmp(lib, "playgo")) { snprintf(err, n, "Unbekannte Bibliothek."); return 400; }
  return ai_start("emu", lib, op, store, id, err, n);
}

static int
ai_start(const char *kind, const char *lib, const char *op, const char *store, const char *id, char *err, size_t n) {
  int emu = !strcmp(kind, "emu");
  char name[128];
  if(emu) {
    if(strcmp(op, "apply") && strcmp(op, "restore") && strcmp(op, "remove")) { snprintf(err, n, "Unbekannter Vorgang."); return 400; }
    if(!strcmp(op, "apply") && (!store || !store[0] || strchr(store, '/') || strlen(store) >= sizeof(((ai_args_t *)0)->store))) {
      snprintf(err, n, "Es fehlt die Version, die eingebaut werden soll.");
      return 400;
    }
    ps5tm_libdel_t li;
    if(!id || ps5tm_library_delete_info(id, &li) != 0) { snprintf(err, n, "Das Spiel ist nicht in der Liste."); return 404; }
    char place[10] = "", why[240] = "";
    if(ps5tm_amprlib_place(lib, id, place, sizeof(place), why, sizeof(why)) != 0) { snprintf(err, n, "Das Spiel ist nicht in der Liste."); return 404; }
    if(!place[0] || why[0]) { snprintf(err, n, "%s", why[0] ? why : "Für dieses Spiel geht das nicht."); return 409; }
    snprintf(name, sizeof(name), "%s", li.name);
  } else {
    int http = 200;
    cJSON *info = ps5tm_ampr_info(id, &http);
    if(!info) { snprintf(err, n, "Das Spiel ist nicht in der Liste."); return 404; }
    int can = cJSON_IsTrue(cJSON_GetObjectItem(info, "can_build"));
    const cJSON *why = cJSON_GetObjectItem(info, "why");
    if(!can) { snprintf(err, n, "%s", cJSON_IsString(why) ? why->valuestring : "Für dieses Spiel geht das nicht."); cJSON_Delete(info); return 409; }
    snprintf(name, sizeof(name), "%s", cJSON_GetObjectItem(info, "name")->valuestring);
    cJSON_Delete(info);
  }
  if(ps5tm_procmgr_title_running(id) == 1) { snprintf(err, n, "Das Spiel läuft gerade. Bitte zuerst beenden."); return 409; }
  pthread_mutex_lock(&g_lock);
  if(g_job.state == AI_RUNNING) { pthread_mutex_unlock(&g_lock); snprintf(err, n, "Es wird gerade schon ein Index gebaut."); return 409; }
  memset(&g_job, 0, sizeof(g_job));
  g_job.state = AI_RUNNING;
  snprintf(g_job.kind, sizeof(g_job.kind), "%s", kind);
  snprintf(g_job.lib, sizeof(g_job.lib), "%s", lib);
  snprintf(g_job.op, sizeof(g_job.op), "%s", op);
  snprintf(g_job.store, sizeof(g_job.store), "%s", store ? store : "");
  snprintf(g_job.id, sizeof(g_job.id), "%s", id);
  snprintf(g_job.name, sizeof(g_job.name), "%s", name);
  snprintf(g_job.phase, sizeof(g_job.phase), "Wird vorbereitet");
  g_job.started_ms = ps5tm_mono_ms();
  __atomic_store_n(&g_cancel, 0, __ATOMIC_RELEASE);
  pthread_mutex_unlock(&g_lock);
  /* claim first, then ask the others (they do the same): of two starts at the same moment one gives way (review 19.10.2026) */
  if(other_job(err, n)) {
    pthread_mutex_lock(&g_lock);
    memset(&g_job, 0, sizeof(g_job));
    pthread_mutex_unlock(&g_lock);
    return 409;
  }
  ai_args_t *a = calloc(1, sizeof(*a));
  if(!a) { finish(AI_FAILED, "Kein Speicher."); snprintf(err, n, "Kein Speicher."); return 500; }
  snprintf(a->id, sizeof(a->id), "%s", id);
  a->emu = emu;
  snprintf(a->lib, sizeof(a->lib), "%s", lib);
  snprintf(a->op, sizeof(a->op), "%s", op);
  snprintf(a->store, sizeof(a->store), "%s", store ? store : "");
  pthread_t th;
  pthread_attr_t at;
  pthread_attr_init(&at);
  pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
  pthread_attr_setstacksize(&at, 768 * 1024);
  int rc = pthread_create(&th, &at, ai_thread, a);
  pthread_attr_destroy(&at);
  if(rc != 0) { free(a); finish(AI_FAILED, "Der Vorgang ließ sich nicht starten."); snprintf(err, n, "Der Vorgang ließ sich nicht starten."); return 500; }
  if(emu) PS5TM_INFO("ampr_emu_start", "%s-Datei: %s von %s (%s).", !strcmp(lib, "playgo") ? "PlayGo" : "APR-EMU", op, name, id);
  else PS5TM_INFO("ampr_index_start", "AMPR-Index wird gebaut: %s (%s).", name, id);
  return 200;
}

cJSON *
ps5tm_ampr_status(void) {
  cJSON *o = cJSON_CreateObject();
  pthread_mutex_lock(&g_lock);
  static const char *const names[] = { "idle", "running", "done", "failed" };
  cJSON_AddBoolToObject(o, "ok", 1);
  cJSON_AddStringToObject(o, "state", names[g_job.state]);
  cJSON_AddStringToObject(o, "kind", g_job.kind);
  cJSON_AddStringToObject(o, "lib", g_job.lib);
  cJSON_AddStringToObject(o, "op", g_job.op);
  if(g_job.note[0]) cJSON_AddStringToObject(o, "note", g_job.note);
  cJSON_AddBoolToObject(o, "active", g_job.state == AI_RUNNING);
  cJSON_AddStringToObject(o, "id", g_job.id);
  cJSON_AddStringToObject(o, "name", g_job.name);
  cJSON_AddStringToObject(o, "phase", g_job.phase);
  if(g_job.error[0]) cJSON_AddStringToObject(o, "error", g_job.error);
  cJSON_AddNumberToObject(o, "files", g_job.files);
  cJSON_AddNumberToObject(o, "collisions", g_job.collisions);
  uint64_t end = g_job.state == AI_RUNNING ? ps5tm_mono_ms() : g_job.finished_ms;
  if(g_job.started_ms) cJSON_AddNumberToObject(o, "elapsed_s", (double)((end - g_job.started_ms) / 1000));
  pthread_mutex_unlock(&g_lock);
  return o;
}
