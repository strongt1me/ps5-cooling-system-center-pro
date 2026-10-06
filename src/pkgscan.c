/* Finding package files (04.10.2026).
 *
 * On every stick, disc and the console's own storage the root of the drive and the folder "pkg"
 * (with the folders below it, three levels down) are searched for package files: "*.pkg" and the parts of
 * a split package ("*.pkg.part2", ...). A search runs in the background, because a stick can be slow and a
 * big package is read at a few places only; the page asks for the list and for the state of the search.
 * A package that has been read before and has not changed (same path, size, time) is not read again.
 *
 * The parts of a split package are one entry: the parts that were found, the ones still missing (they may lie
 * on another stick or disc) and whether the set is complete.
 *
 * Nothing here writes. A package is addressed by an id (a hash of its path) that only means something for the
 * list of the last search: the page never hands a path to the console that it did not get from the list. */

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "pkginst.h"
#include "ps5tm.h"
#include "third_party/cJSON.h"

#define PK_MAX_PKGS  512                 /* packages kept from one search */
#define PK_MAX_DEPTH 3                   /* folders below "pkg" that are searched */
#define PK_MIN_SIZE  4096                /* a smaller file is no package */
#define PK_MAX_PARTS 256                 /* parts a split package can have */

typedef struct {
  ps5tm_pkg_t p;
  char        drive[40];                 /* the mount it was found on */
  unsigned    drive_idx;
  char        id[17];
} item_t;

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static item_t   *g_items;                /* the list of the last finished search; written by the search thread only, at its end */
static unsigned  g_n;
static ps5tm_pkgdrive_t g_drives[PS5TM_MAX_VOLUMES];     /* the drives that search looked at */
static unsigned  g_ndrives;

static struct {
  int      scanning;
  unsigned seen, parsed, failed;         /* files looked at, packages read, files that were no package */
  char     drive[64];
  uint64_t started_ms, finished_ms;      /* wall clock */
  int      ever;
} g_st;

/* ------------------------------------------------------------------ the drives */

unsigned
ps5tm_pkg_drives(ps5tm_pkgdrive_t *out, unsigned max) {
  unsigned n = 0;
#ifdef PS5TM_HOST_TEST
  const char *roots = getenv("PS5TM_PKG_ROOTS");        /* the tests name the folders that stand for drives: a:b:c */
  if(roots && roots[0]) {
    char buf[1024];
    snprintf(buf, sizeof(buf), "%s", roots);
    for(char *save = NULL, *t = strtok_r(buf, ":", &save); t && n < max; t = strtok_r(NULL, ":", &save)) {
      snprintf(out[n].mount, sizeof(out[n].mount), "%s", t);
      snprintf(out[n].base, sizeof(out[n].base), "%s", t);
      snprintf(out[n].label, sizeof(out[n].label), "Test %u", n);
      out[n].free_bytes = 50ull << 30;
      const char *in = getenv("PS5TM_PKG_INTERNAL");                /* one of them stands for the console's own storage */
      out[n].internal = in && !strcmp(in, t);
      n++;
    }
    return n;
  }
#endif
  ps5tm_sysinfo_t info;
  ps5tm_sysinfo_get(&info);
  for(unsigned i = 0; i < info.volume_count && n < max; i++) {
    const char *p = info.volumes[i].path;
    int internal = !strcmp(p, "/user");
    if(!internal && strncmp(p, "/mnt/", 5)) continue;
    snprintf(out[n].mount, sizeof(out[n].mount), "%s", p);
    snprintf(out[n].base, sizeof(out[n].base), "%s", internal ? "/data" : p);
    snprintf(out[n].label, sizeof(out[n].label), "%s", info.volumes[i].label);
    out[n].free_bytes = info.volumes[i].free_bytes;
    out[n].internal = internal;
    n++;
  }
  return n;
}

/* ------------------------------------------------------------------ the search */

static uint64_t
wall_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  return (uint64_t)ts.tv_sec * 1000 + (uint64_t)(ts.tv_nsec / 1000000);
}

static void
make_id(const char *path, char out[17]) {
  uint64_t h = 1469598103934665603ull;
  for(const unsigned char *c = (const unsigned char *)path; *c; c++) { h ^= *c; h *= 1099511628211ull; }
  snprintf(out, 17, "%016llx", (unsigned long long)h);
}

static int
has_pkg_ext(const char *name) {
  size_t n = strlen(name);
  return n > 4 && !strcasecmp(name + n - 4, ".pkg");
}

/* One file out of an earlier list, when path, size and time are the same. */
static const item_t *
cached(const item_t *old, unsigned nold, const char *path, uint64_t size, int64_t mtime) {
  for(unsigned i = 0; i < nold; i++)
    if(old[i].p.size == size && old[i].p.mtime == mtime && !strcmp(old[i].p.path, path)) return &old[i];
  return NULL;
}

typedef struct {
  item_t         *list;
  unsigned        n;
  const item_t   *old;
  unsigned        nold;
  unsigned        drive_idx;
  const char     *mount;
} ctx_t;

static void
look_at(ctx_t *c, const char *path, const char *name, const struct stat *st) {
  unsigned part;
  if(!has_pkg_ext(name) && !ps5tm_pkg_part_name(name, &part)) return;
  if(st->st_size < PK_MIN_SIZE) return;
  pthread_mutex_lock(&g_lock);
  g_st.seen++;
  pthread_mutex_unlock(&g_lock);
  if(c->n >= PK_MAX_PKGS) return;
  for(unsigned i = 0; i < c->n; i++) if(!strcmp(c->list[i].p.path, path)) return;       /* the same file reached twice */
  item_t *it = &c->list[c->n];
  const item_t *hit = cached(c->old, c->nold, path, (uint64_t)st->st_size, (int64_t)st->st_mtime);
  if(hit) {
    *it = *hit;
  } else {
    memset(it, 0, sizeof(*it));
    if(ps5tm_pkg_parse(path, &it->p) != 0) {
      pthread_mutex_lock(&g_lock);
      g_st.failed++;
      pthread_mutex_unlock(&g_lock);
      return;
    }
  }
  snprintf(it->drive, sizeof(it->drive), "%s", c->mount);
  it->drive_idx = c->drive_idx;
  make_id(path, it->id);
  c->n++;
  pthread_mutex_lock(&g_lock);
  g_st.parsed++;
  pthread_mutex_unlock(&g_lock);
}

/* files of one folder; below it, with `depth` levels left, the folders too */
static void
walk(ctx_t *c, const char *dir, int depth) {
  DIR *d = opendir(dir);
  if(!d) return;
  struct dirent *e;
  while((e = readdir(d)) != NULL) {
    if(e->d_name[0] == '.') continue;
    char path[PS5TM_PKG_PATH];
    if(snprintf(path, sizeof(path), "%s/%s", dir, e->d_name) >= (int)sizeof(path)) continue;
    struct stat st;
    if(lstat(path, &st) != 0) continue;                            /* links are not followed: no loops, no surprises */
    if(S_ISREG(st.st_mode)) look_at(c, path, e->d_name, &st);
    else if(S_ISDIR(st.st_mode) && depth > 0) walk(c, path, depth - 1);
  }
  closedir(d);
}

static int
cmp_item(const void *a, const void *b) {
  const item_t *x = a, *y = b;
  if(x->drive_idx != y->drive_idx) return x->drive_idx < y->drive_idx ? -1 : 1;
  int c = strcasecmp(x->p.name, y->p.name);
  if(c) return c;
  c = strcmp(x->p.kind, y->p.kind);
  if(c) return c;
  c = strcmp(x->p.version, y->p.version);
  if(c) return c;
  if(x->p.part != y->p.part) return x->p.part < y->p.part ? -1 : 1;
  return strcmp(x->p.path, y->p.path);
}

static void *
scan_main(void *arg) {
  (void)arg;
  ps5tm_pkgdrive_t dr[PS5TM_MAX_VOLUMES];
  unsigned nd = ps5tm_pkg_drives(dr, PS5TM_MAX_VOLUMES);
  item_t *nl = calloc(PK_MAX_PKGS, sizeof(*nl));
  ctx_t c = { nl, 0, NULL, 0, 0, "" };
  pthread_mutex_lock(&g_lock);
  c.old = g_items;                                   /* only this thread ever replaces it */
  c.nold = g_n;
  pthread_mutex_unlock(&g_lock);
  for(unsigned i = 0; nl && i < nd; i++) {
    pthread_mutex_lock(&g_lock);
    snprintf(g_st.drive, sizeof(g_st.drive), "%s", dr[i].label[0] ? dr[i].label : dr[i].mount);
    pthread_mutex_unlock(&g_lock);
    c.drive_idx = i;
    c.mount = dr[i].mount;
    char dir[PS5TM_PKG_PATH];
    if(!dr[i].internal) walk(&c, dr[i].base, 0);                   /* the root: its files only */
    /* the folder named pkg, whatever its capitals */
    DIR *d = opendir(dr[i].base);
    struct dirent *e;
    while(d && (e = readdir(d)) != NULL) {
      if(strcasecmp(e->d_name, "pkg")) continue;
      if(snprintf(dir, sizeof(dir), "%s/%s", dr[i].base, e->d_name) >= (int)sizeof(dir)) continue;
      struct stat st;
      if(lstat(dir, &st) == 0 && S_ISDIR(st.st_mode)) walk(&c, dir, PK_MAX_DEPTH);
    }
    if(d) closedir(d);
  }
  if(nl) qsort(nl, c.n, sizeof(*nl), cmp_item);
  pthread_mutex_lock(&g_lock);
  item_t *old = g_items;
  g_items = nl;
  g_n = nl ? c.n : 0;
  memcpy(g_drives, dr, sizeof(dr[0]) * nd);
  g_ndrives = nd;
  g_st.scanning = 0;
  g_st.drive[0] = 0;
  g_st.finished_ms = wall_ms();
  g_st.ever = 1;
  pthread_mutex_unlock(&g_lock);
  free(old);
  return NULL;
}

/* 1 started, 0 one is under way already, -1 could not start */
int
ps5tm_pkgscan_start(void) {
  pthread_mutex_lock(&g_lock);
  if(g_st.scanning) { pthread_mutex_unlock(&g_lock); return 0; }
  g_st.scanning = 1;
  g_st.seen = g_st.parsed = g_st.failed = 0;
  g_st.started_ms = wall_ms();
  pthread_mutex_unlock(&g_lock);
  pthread_t th;
  pthread_attr_t at;
  pthread_attr_init(&at);
  pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
  pthread_attr_setstacksize(&at, 256 * 1024);
  int rc = pthread_create(&th, &at, scan_main, NULL);
  pthread_attr_destroy(&at);
  if(rc != 0) {
    pthread_mutex_lock(&g_lock);
    g_st.scanning = 0;
    pthread_mutex_unlock(&g_lock);
    return -1;
  }
  return 1;
}

int
ps5tm_pkgscan_busy(void) {
  pthread_mutex_lock(&g_lock);
  int b = g_st.scanning;
  pthread_mutex_unlock(&g_lock);
  return b;
}

/* ------------------------------------------------------------------ the list */

/* The set a part belongs to: the other parts with the same uuid and the same number of parts. */
static int
same_set(const ps5tm_pkg_t *a, const ps5tm_pkg_t *b) {
  return a->parts > 0 && b->parts > 0 && !memcmp(a->uuid, b->uuid, 16) && a->parts == b->parts;
}

cJSON *
ps5tm_pkgscan_json(void) {
  cJSON *root = cJSON_CreateObject();
  if(!root) return NULL;
  pthread_mutex_lock(&g_lock);
  cJSON_AddBoolToObject(root, "ok", 1);
  cJSON_AddBoolToObject(root, "scanning", g_st.scanning);
  cJSON_AddBoolToObject(root, "ever", g_st.ever);
  cJSON_AddNumberToObject(root, "seen", g_st.seen);
  cJSON_AddNumberToObject(root, "parsed", g_st.parsed);
  cJSON_AddNumberToObject(root, "failed", g_st.failed);
  cJSON_AddStringToObject(root, "current", g_st.drive);
  cJSON_AddNumberToObject(root, "scanned_at", g_st.ever ? (double)(g_st.finished_ms / 1000) : 0);
  cJSON_AddNumberToObject(root, "now", (double)(wall_ms() / 1000));
  cJSON *drives = cJSON_AddArrayToObject(root, "drives");
  for(unsigned i = 0; i < g_ndrives; i++) {
    cJSON *d = cJSON_CreateObject();
    unsigned cnt = 0;
    for(unsigned k = 0; k < g_n; k++) if(g_items[k].drive_idx == i) cnt++;
    cJSON_AddStringToObject(d, "mount", g_drives[i].mount);
    cJSON_AddStringToObject(d, "label", g_drives[i].label);
    cJSON_AddNumberToObject(d, "free_bytes", (double)g_drives[i].free_bytes);
    cJSON_AddNumberToObject(d, "count", cnt);
    cJSON_AddItemToArray(drives, d);
  }
  cJSON *list = cJSON_AddArrayToObject(root, "packages");
  for(unsigned i = 0; i < g_n; i++) {
    const item_t *it = &g_items[i];
    const ps5tm_pkg_t *p = &it->p;
    unsigned have_mask[PK_MAX_PARTS / 32];
    unsigned found = 0;
    uint64_t bytes = 0;
    int leader = 1;
    if(p->parts > 0) {
      memset(have_mask, 0, sizeof(have_mask));
      for(unsigned k = 0; k < g_n; k++) {
        const ps5tm_pkg_t *q = &g_items[k].p;
        if(!same_set(p, q) || q->part == 0 || q->part > p->parts || q->part > PK_MAX_PARTS) continue;
        unsigned bit = q->part - 1;
        if(q->part == p->part && k < i) leader = 0;                  /* an earlier file is this very part: it stands for both */
        if(have_mask[bit / 32] & (1u << (bit % 32))) continue;       /* the same part twice */
        have_mask[bit / 32] |= 1u << (bit % 32);
        found++;
        bytes += q->size;
        if(q->part < p->part) leader = 0;
      }
      if(!leader) continue;                                         /* shown once: with its lowest part */
    }
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "id", it->id);
    cJSON_AddStringToObject(o, "path", p->path);
    cJSON_AddStringToObject(o, "file", p->parts ? p->orig_file[0] ? p->orig_file : p->file : p->file);
    cJSON_AddStringToObject(o, "drive", it->drive);
    cJSON_AddStringToObject(o, "title_id", p->title_id);
    cJSON_AddStringToObject(o, "content_id", p->content_id);
    cJSON_AddStringToObject(o, "name", p->name);
    cJSON_AddStringToObject(o, "version", p->version);
    cJSON_AddStringToObject(o, "kind", p->kind);
    cJSON_AddNumberToObject(o, "plat", p->plat);
    cJSON_AddNumberToObject(o, "size", (double)(p->parts ? bytes : p->size));
    cJSON_AddNumberToObject(o, "total", (double)p->total);
    cJSON_AddNumberToObject(o, "mtime", (double)p->mtime);
    cJSON_AddBoolToObject(o, "has_icon", p->icon_size > 0);
    if(p->parts > 0) {
      cJSON *pt = cJSON_AddObjectToObject(o, "parts");
      cJSON_AddNumberToObject(pt, "total", p->parts);
      cJSON_AddNumberToObject(pt, "found", found);
      cJSON *miss = cJSON_AddArrayToObject(pt, "missing");
      for(unsigned k = 1; k <= p->parts && k <= PK_MAX_PARTS; k++)
        if(!(have_mask[(k - 1) / 32] & (1u << ((k - 1) % 32)))) cJSON_AddItemToArray(miss, cJSON_CreateNumber(k));
      cJSON_AddBoolToObject(pt, "complete", found == p->parts);
    }
    cJSON_AddItemToArray(list, o);
  }
  pthread_mutex_unlock(&g_lock);
  return root;
}

/* The package the last search listed under this id (for a set of parts: its lowest part). 0, or -1. */
int
ps5tm_pkgscan_find(const char *id, ps5tm_pkg_t *out) {
  if(!id || strlen(id) != 16) return -1;
  int rc = -1;
  pthread_mutex_lock(&g_lock);
  for(unsigned i = 0; i < g_n; i++)
    if(!strcmp(g_items[i].id, id)) { *out = g_items[i].p; rc = 0; break; }
  pthread_mutex_unlock(&g_lock);
  return rc;
}

/* The pieces of the package under this id, in order, for reading it as one stream (the installer): one slice for a
   package in one file; for a split one a slice per part, taken from the parts the last search found. All parts must be
   there and stored as they are (none compressed), each slice must lie inside its file, and together they must be the
   whole package, without a gap. 0 with the number of slices and the length of the whole package, or -1 with the
   reason in err. */
int
ps5tm_pkgscan_slices(const char *id, ps5tm_pkgslice_t *out, unsigned max, unsigned *n, uint64_t *total,
                     char *err, size_t err_len) {
  err[0] = 0;
  *n = 0;
  *total = 0;
  if(!id || strlen(id) != 16) { snprintf(err, err_len, "Dieses Paket kennt die letzte Suche nicht."); return -1; }
  int rc = -1;
  pthread_mutex_lock(&g_lock);
  const item_t *base = NULL;
  for(unsigned i = 0; i < g_n; i++) if(!strcmp(g_items[i].id, id)) { base = &g_items[i]; break; }
  if(!base) {
    snprintf(err, err_len, "Dieses Paket kennt die letzte Suche nicht (mehr). Bitte noch einmal suchen.");
  } else if(base->p.parts == 0) {
    if(max < 1 || base->p.size == 0) {
      snprintf(err, err_len, "Das Paket ist leer.");
    } else {
      snprintf(out[0].path, sizeof(out[0].path), "%s", base->p.path);
      out[0].file_off = 0;
      out[0].size = base->p.size;
      out[0].logical = 0;
      out[0].file_size = base->p.size;
      out[0].mtime = base->p.mtime;
      *n = 1;
      *total = base->p.size;
      rc = 0;
    }
  } else if(base->p.parts > max || base->p.parts > PK_MAX_PARTS) {
    snprintf(err, err_len, "Das Paket hat zu viele Teile.");
  } else {
    uint64_t at = 0;
    unsigned have = 0;
    int bad = 0;
    for(unsigned part = 1; part <= base->p.parts && !bad; part++) {
      const item_t *it = NULL;
      for(unsigned k = 0; k < g_n; k++)
        if(same_set(&base->p, &g_items[k].p) && g_items[k].p.part == part) { it = &g_items[k]; break; }
      if(!it) { snprintf(err, err_len, "Es fehlen noch Teile des Pakets (zum Beispiel Teil %u von %u).", part, base->p.parts); bad = 1; break; }
      const ps5tm_pkg_t *q = &it->p;
      /* Of one part there may be several files (a copy on another drive). Copies of the same size and time are taken
         for the same file; two that differ are two different parts, and nobody can say which is meant. */
      for(unsigned k = 0; k < g_n; k++) {
        const ps5tm_pkg_t *o = &g_items[k].p;
        if(&g_items[k] != it && same_set(&base->p, o) && o->part == part &&
           (o->size != q->size || o->mtime != q->mtime || o->data_off != q->data_off || o->data_size != q->data_size || o->part_off != q->part_off)) {
          snprintf(err, err_len, "Von Teil %u gibt es zwei verschiedene Dateien. Bitte die überzählige entfernen.", part);
          bad = 1;
          break;
        }
      }
      if(bad) break;
      if(!q->raw) { snprintf(err, err_len, "Die Teile sind komprimiert gespeichert; so lässt sich das Paket nicht installieren."); bad = 1; break; }
      if(q->total != base->p.total) { snprintf(err, err_len, "Teil %u gehört zu einem Paket anderer Größe als die übrigen Teile.", part); bad = 1; break; }
      if(q->data_off < 4096 || q->data_size == 0 || q->data_off > q->size || q->data_size > q->size - q->data_off) {
        snprintf(err, err_len, "Teil %u des Pakets ist beschädigt (der Inhalt liegt nicht in der Datei).", part); bad = 1; break;
      }
      if(q->part_off != at) { snprintf(err, err_len, "Die Teile des Pakets passen nicht lückenlos aneinander (Teil %u).", part); bad = 1; break; }
      snprintf(out[have].path, sizeof(out[have].path), "%s", q->path);
      out[have].file_off = q->data_off;
      out[have].size = q->data_size;
      out[have].logical = at;
      out[have].file_size = q->size;
      out[have].mtime = q->mtime;
      at += q->data_size;
      have++;
    }
    if(!bad && at != base->p.total) {
      snprintf(err, err_len, "Die Teile ergeben %llu Bytes, das Paket sollte %llu haben.", (unsigned long long)at, (unsigned long long)base->p.total);
      bad = 1;
    }
    if(!bad) { *n = have; *total = at; rc = 0; }
  }
  pthread_mutex_unlock(&g_lock);
  return rc;
}
