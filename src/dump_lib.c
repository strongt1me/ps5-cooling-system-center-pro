/* SPDX-License-Identifier: GPL-3.0-or-later
 * Marks on the games list and the search for existing dumps (22.10.2026).
 *
 * How a game's install state, its disc and its dumps are told comes from ps5-app-dumper 2.10
 * (Copyright (C) 2025 EchoStretch, Copyright (C) 2026 slopmaster33; GPL-3.0-or-later; app_scan.c, dump_store.c,
 * dump_library.c), rewritten for this app. Read only: nothing here writes, moves or deletes.
 *
 * Marks (ps5tm_title_marks): per title, from a handful of cheap file checks —
 *   - a disc game: /mnt/disc/app/<id> is there (disc in the drive), or it was seen there before (disc_titles.txt, like the
 *     dumper's own list), or the console kept /system_data/priv/appmeta/<id>/disc_info.dat (the dumper's author checked
 *     on FW 12.00: only titles installed from a disc have it) or the old /system_data/playgo/<id>/bdcopy.pbm.
 *   - the install state: <root>/<id>/app.pbm, one bit per 64 KiB block (read again only when its size or time changed),
 *     and the shell's PlayGo status in <root>/<id>/app.xml (which chunks this console wants). Below 100 % can be for
 *     good — the console fetches only the chunks it wants. "Still arriving" means the bitmap moved in the last ten
 *     minutes and the PlayGo status does not say that every wanted chunk is here.
 *   - the DLC installed for it: /user/addcont/<id>/<LABEL>/ac.pkg.
 *   - whether a dump exists: from the last search below.
 *
 * The search (ps5tm_dumplib_*) runs in the background on the sticks, the M.2 drives and the console's own storage
 * (below /data only the folder "homebrew"), a few folders deep. It lists only the dumps that were made from the Disc tab:
 * a folder named <TITLE>[-app0|-patch0][_Title] or a DLC folder as pfsmnt names it, with "<folder>.dump-info.json" next to
 * it that says "origin": "disc-tab". Every other folder — a game's own folder, a dump of another tool, an older dump of this
 * app — is not listed and not counted. Links are never followed. */

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "ps5tm.h"
#include "third_party/cJSON.h"

#define DL_MAX          128              /* dumps kept from one search */
#define DL_SCAN_DEPTH   3                /* folders below a drive's root that are looked into */
#define DL_SCAN_DIRS    500              /* folders opened per drive */
#define DL_INFO_MAX     8192
#define DL_MEASURE_CAP  300000           /* entries counted per dump when measuring */
#define DL_MEASURE_MS   180000           /* time one search may spend measuring */
#define DL_INSTALL_QUIET_S (10 * 60)

#define INFO_SUFFIX ".dump-info.json"

/* ------------------------------------------------------------------ small helpers */

static int
is_title_id(const char *s) {
  if(!s || strlen(s) != 9 || (strncmp(s, "PPSA", 4) && strncmp(s, "CUSA", 4))) return 0;
  for(int i = 4; i < 9; i++)
    if(!isdigit((unsigned char)s[i])) return 0;
  return 1;
}

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

static uint64_t
wall_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  return (uint64_t)ts.tv_sec * 1000 + (uint64_t)(ts.tv_nsec / 1000000);
}

/* ------------------------------------------------------------------ install state */

static const char *const k_app_roots[] = { "/user/app", "/mnt/ext0/user/app", "/mnt/ext1/user/app", NULL };
static const char *const k_addcont_roots[] = { "/user/addcont", "/mnt/ext0/user/addcont", "/mnt/ext1/user/addcont", NULL };

/* app.pbm: "pdbm", ..., 0x22 number of 64 KiB blocks (little endian), 0x100 one bit per block, highest bit of a byte
   first, set once the block is here, then a 32-byte digest. A file that does not hold what its header promises tells
   nothing (-1). 100 only when every bit is set. */
static int
pbm_percent(const char *path) {
  FILE *f = fopen(path, "rb");
  if(!f) return -1;
  unsigned char head[256];
  int pct = -1;
  if(fread(head, 1, sizeof(head), f) == sizeof(head) && !memcmp(head, "pdbm", 4)) {
    uint32_t blocks = (uint32_t)head[0x22] | ((uint32_t)head[0x23] << 8) | ((uint32_t)head[0x24] << 16) | ((uint32_t)head[0x25] << 24);
    uint64_t set = 0, seen = 0;
    unsigned char *buf = malloc(8192);
    size_t n;
    while(buf && blocks && seen < blocks && (n = fread(buf, 1, 8192, f)) > 0) {
      for(size_t i = 0; i < n && seen < blocks; i++)
        for(int bit = 0; bit < 8 && seen < blocks; bit++, seen++)
          if(buf[i] & (0x80u >> bit)) set++;
    }
    free(buf);
    if(blocks && seen == blocks) {
      pct = (int)(set * 100 / blocks);
      if(pct == 100 && set != blocks) pct = 99;
    }
  }
  fclose(f);
  return pct;
}

static struct { char id[12]; time_t mtime; off_t size; int pct; } g_pbm_cache[64];
static pthread_mutex_t g_pbm_mx = PTHREAD_MUTEX_INITIALIZER;

/* -1 when there is no bitmap here; *mtime tells when it moved last. */
static int
installed_percent_at(const char *root, const char *id, time_t *mtime) {
  char path[320];
  snprintf(path, sizeof(path), "%s/%s/app.pbm", root, id);
  struct stat st;
  if(stat(path, &st) != 0 || !S_ISREG(st.st_mode)) return -1;
  *mtime = st.st_mtime;
  pthread_mutex_lock(&g_pbm_mx);
  int slot = -1, spare = -1;
  for(int i = 0; i < 64; i++) {
    if(!strcmp(g_pbm_cache[i].id, id)) { slot = i; break; }
    if(spare < 0 && !g_pbm_cache[i].id[0]) spare = i;
  }
  if(slot >= 0 && g_pbm_cache[slot].mtime == st.st_mtime && g_pbm_cache[slot].size == st.st_size) {
    int pct = g_pbm_cache[slot].pct;
    pthread_mutex_unlock(&g_pbm_mx);
    return pct;
  }
  pthread_mutex_unlock(&g_pbm_mx);
  int pct = pbm_percent(path);
  pthread_mutex_lock(&g_pbm_mx);
  if(slot < 0) slot = spare >= 0 ? spare : (int)((unsigned)st.st_mtime % 64);
  snprintf(g_pbm_cache[slot].id, sizeof(g_pbm_cache[slot].id), "%s", id);
  g_pbm_cache[slot].mtime = st.st_mtime;
  g_pbm_cache[slot].size = st.st_size;
  g_pbm_cache[slot].pct = pct;
  pthread_mutex_unlock(&g_pbm_mx);
  return pct;
}

/* app.xml, "playgo-status": one <chunk> per chunk with locus="3" when it is here and req_locus="3" when this console
   wants it; chunk_count says how many there are. Which chunks are wanted can be read here; whether they arrived is not
   always kept up to date by the shell, so "here" is only used to say "settled", never to deny. */
static int
playgo_at(const char *root, const char *id, int *chunks, int *wanted, int *here) {
  char path[320];
  snprintf(path, sizeof(path), "%s/%s/app.xml", root, id);
  FILE *f = fopen(path, "r");
  if(!f) return -1;
  char *xml = calloc(1, 65536);
  if(!xml) { fclose(f); return -1; }
  size_t n = fread(xml, 1, 65535, f);
  fclose(f);
  xml[n] = 0;
  if(!strstr(xml, "playgo-status")) { free(xml); return -1; }
  const char *cc = strstr(xml, "chunk_count=\"");
  int declared = cc ? atoi(cc + 13) : 0;
  int c = 0, w = 0, h = 0;
  for(const char *p = strstr(xml, "<chunk "); p; p = strstr(p + 1, "<chunk ")) {
    const char *end = strchr(p, '>');
    if(!end) break;
    const char *l = strstr(p, "locus=\"");
    const char *r = strstr(p, "req_locus=\"");
    while(l && l < end && l > p && l[-1] == '_') l = strstr(l + 1, "locus=\"");   /* "locus" also matches inside "req_locus" */
    int locus = (l && l < end) ? atoi(l + 7) : 0;
    int want = (r && r < end) ? atoi(r + 11) : -1;
    c++;
    if(want >= 0) { w++; if(locus == want) h++; }
  }
  free(xml);
  if(declared > c) c = declared;
  if(!c) return -1;
  *chunks = c; *wanted = w; *here = h;
  return 0;
}

/* ------------------------------------------------------------------ the search results */

typedef struct {
  char     mount[40];                    /* what the page calls the drive: "/mnt/usb0", "/user" */
  char     dir[128];                     /* the folder that holds it, relative to the drive's files; "" = root */
  char     folder[64];
  char     title_id[12];
  char     title[128];
  char     state[16];                    /* "done", "running", "failed", "aborted"; "" without an info file */
  int      kind;                         /* 0 game, 1 patch, 2 DLC */
  int      drive;
  uint64_t bytes;                        /* the info file's */
  int64_t  measured;                     /* -1 not measured */
  int      decrypt, fself, backport, split;   /* -1 not known */
  int64_t  started, finished;
  int      origin_ok;                    /* the info file says it was made from the Disc tab */
} dump_t;

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static dump_t   *g_list;                 /* replaced whole at the end of a search */
static unsigned  g_n;
static struct { char mount[40], label[40]; unsigned count; } g_drv[PS5TM_MAX_VOLUMES];
static unsigned  g_ndrv;
static struct {
  int      scanning, ever, truncated, measure;
  uint64_t finished_ms;
} g_st;
static int g_auto_tried;

static int
suffix_char(char c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.' ||
         c == '(' || c == ')' || c == '&' || c == '+' || c == '!' || c == '\'';
}

/* <region>-<TITLEID>_00-<LABEL>-ac: a DLC, as pfsmnt names it */
static int
is_dlc_folder(const char *name) {
  size_t len = name ? strlen(name) : 0;
  if(len < 22 || len > 63) return 0;
  if(strcmp(name + len - 3, "-ac")) return 0;
  if(name[6] != '-' || (strncmp(name + 7, "PPSA", 4) && strncmp(name + 7, "CUSA", 4))) return 0;
  if(strncmp(name + 16, "_00-", 4)) return 0;
  for(size_t i = 0; i < len; i++)
    if(!isalnum((unsigned char)name[i]) && name[i] != '-' && name[i] != '_') return 0;
  return 1;
}

/* The kind of a dump folder from its name: 0 game ("PPSA01234", "PPSA01234-app0"), 1 patch ("CUSA01234-patch0"),
   2 DLC; each game or patch name may carry "_Title" behind it. -1 for anything else. */
static int
folder_kind(const char *name) {
  size_t len = name ? strlen(name) : 0;
  if(len >= 64) return -1;
  if(is_dlc_folder(name)) return 2;
  if(len < 9 || (strncmp(name, "PPSA", 4) && strncmp(name, "CUSA", 4))) return -1;
  for(int i = 4; i < 9; i++)
    if(!isdigit((unsigned char)name[i])) return -1;
  const char *rest = name + 9;
  size_t base = 9;
  int kind = 0;
  if(!strncmp(rest, "-app0", 5)) base += 5;
  else if(!strncmp(rest, "-patch0", 7)) { base += 7; kind = 1; }
  const char *tail = name + base;
  if(tail[0] && tail[0] != '_') return -1;
  for(const char *p = tail; *p; p++)
    if(!suffix_char(*p)) return -1;
  return kind;
}

/* A title id is not enough of a name: save data, screenshots and other tools file things under it too. A dump is a
   folder with the dumper's info file next to it, or with a game's insides: sce_sys, an eboot.bin. */
static int
looks_like_dump(const char *dest, const char *folder) {
  char p[640];
  snprintf(p, sizeof(p), "%s/%s" INFO_SUFFIX, dest, folder);
  if(is_file(p)) return 1;
  snprintf(p, sizeof(p), "%s/%s/sce_sys", dest, folder);
  if(is_dir(p)) return 1;
  snprintf(p, sizeof(p), "%s/%s/eboot.bin", dest, folder);
  return is_file(p);
}

typedef struct {
  dump_t        *out;
  unsigned       n, dirs;
  int            truncated;
  const char    *mount, *base;
  int            drive;
  ps5tm_libupd_t *lib;
  int            nlib;
} ctx_t;

/* Reads the fields the dumper writes (flat, small). Missing file or key: the field keeps what it was. */
static void
read_info(const char *path, dump_t *d) {
  FILE *f = fopen(path, "r");
  if(!f) return;
  char *buf = malloc(DL_INFO_MAX + 1);
  if(!buf) { fclose(f); return; }
  size_t n = fread(buf, 1, DL_INFO_MAX, f);
  fclose(f);
  buf[n] = 0;
  cJSON *j = cJSON_Parse(buf);
  free(buf);
  if(!j) return;
  const cJSON *v;
  if(cJSON_IsString(v = cJSON_GetObjectItem(j, "state"))) snprintf(d->state, sizeof(d->state), "%s", v->valuestring);
  if(cJSON_IsString(v = cJSON_GetObjectItem(j, "title"))) snprintf(d->title, sizeof(d->title), "%s", v->valuestring);
  if(cJSON_IsString(v = cJSON_GetObjectItem(j, "origin")) && !strcmp(v->valuestring, "disc-tab")) d->origin_ok = 1;
  if(cJSON_IsNumber(v = cJSON_GetObjectItem(j, "bytes")) && v->valuedouble > 0) d->bytes = (uint64_t)v->valuedouble;
  if(cJSON_IsNumber(v = cJSON_GetObjectItem(j, "started"))) d->started = (int64_t)v->valuedouble;
  if(cJSON_IsNumber(v = cJSON_GetObjectItem(j, "finished"))) d->finished = (int64_t)v->valuedouble;
  const cJSON *s = cJSON_GetObjectItem(j, "settings");
  if(cJSON_IsObject(s)) {
    if(cJSON_IsNumber(v = cJSON_GetObjectItem(s, "decrypt"))) d->decrypt = (int)v->valuedouble;
    if(cJSON_IsNumber(v = cJSON_GetObjectItem(s, "fself"))) d->fself = (int)v->valuedouble;
    if(cJSON_IsNumber(v = cJSON_GetObjectItem(s, "backport"))) d->backport = (int)v->valuedouble;
    if(cJSON_IsNumber(v = cJSON_GetObjectItem(s, "split"))) d->split = (int)v->valuedouble;
  }
  cJSON_Delete(j);
}

static int
skip_dir(const char *name) {
  return name[0] == '.' || name[0] == '$' || !strcmp(name, "System Volume Information") || !strcmp(name, "logs") ||
         !strcmp(name, "SAVEDATA");
}

static void
add_entry(ctx_t *c, const char *rel, const char *dest, const char *name, int kind) {
  if(c->n >= DL_MAX) { c->truncated = 1; return; }
  dump_t *d = &c->out[c->n++];
  memset(d, 0, sizeof(*d));
  d->measured = -1;
  d->decrypt = d->fself = d->backport = d->split = -1;
  d->kind = kind;
  d->drive = c->drive;
  snprintf(d->mount, sizeof(d->mount), "%s", c->mount);
  snprintf(d->dir, sizeof(d->dir), "%s", rel);
  snprintf(d->folder, sizeof(d->folder), "%s", name);
  snprintf(d->title_id, sizeof(d->title_id), "%.9s", kind == 2 ? name + 7 : name);
  char info[640];
  snprintf(info, sizeof(info), "%s/%s" INFO_SUFFIX, dest, name);
  read_info(info, d);
  if(!d->origin_ok) { c->n--; return; }
  for(int i = 0; i < c->nlib; i++) {
    if(strcmp(c->lib[i].title_id, d->title_id) || !c->lib[i].name[0]) continue;
    /* the installed title's name wins over what the info file kept (it was written in whatever language came first);
       a DLC keeps its label behind the game's name */
    if(kind == 2) {
      char label[96];
      snprintf(label, sizeof(label), "%s", d->title[0] ? d->title : name);
      snprintf(d->title, sizeof(d->title), "%.60s - %.60s", c->lib[i].name, label);
    } else {
      snprintf(d->title, sizeof(d->title), "%s", c->lib[i].name);
    }
    break;
  }
}

static void
walk(ctx_t *c, const char *rel, int depth) {
  if(c->dirs++ >= DL_SCAN_DIRS) { c->truncated = 1; return; }
  if(c->n >= DL_MAX) { c->truncated = 1; return; }
  char path[512];
  if(rel[0]) snprintf(path, sizeof(path), "%s/%s", c->base, rel);
  else snprintf(path, sizeof(path), "%s", c->base);
  DIR *d = opendir(path);
  if(!d) return;
  struct dirent *e;
  while((e = readdir(d)) != NULL && c->n < DL_MAX) {
    if(!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
    if(e->d_type == DT_LNK) continue;
    char full[768];
    if(snprintf(full, sizeof(full), "%s/%s", path, e->d_name) >= (int)sizeof(full)) continue;
    if(e->d_type != DT_DIR) {
      if(e->d_type != DT_UNKNOWN) continue;
      struct stat st;
      if(lstat(full, &st) != 0 || !S_ISDIR(st.st_mode)) continue;
    }
    char child[256];
    if(rel[0]) { if(snprintf(child, sizeof(child), "%s/%s", rel, e->d_name) >= (int)sizeof(child)) continue; }
    else snprintf(child, sizeof(child), "%s", e->d_name);
    int kind = folder_kind(e->d_name);
    if(kind >= 0) {
      /* a dump is never looked into: it is somebody's game, not a place for more */
      if(strlen(rel) < sizeof(((dump_t *)0)->dir) && looks_like_dump(path, e->d_name))
        add_entry(c, rel, path, e->d_name, kind);
    } else if(depth < DL_SCAN_DEPTH && !skip_dir(e->d_name)) {
      walk(c, child, depth + 1);
    }
  }
  closedir(d);
}

/* Bytes of a dump folder: files counted, links not followed, at most DL_MEASURE_CAP entries. -1 when it ran out. */
static int64_t
measure_tree(const char *path, unsigned *budget, uint64_t deadline_ms, int depth) {
  if(depth > 32 || *budget == 0 || wall_ms() > deadline_ms) return -1;
  DIR *d = opendir(path);
  if(!d) return 0;
  int64_t sum = 0;
  struct dirent *e;
  while((e = readdir(d)) != NULL) {
    if(!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
    if(*budget == 0 || wall_ms() > deadline_ms) { sum = -1; break; }
    (*budget)--;
    char full[1024];
    if(snprintf(full, sizeof(full), "%s/%s", path, e->d_name) >= (int)sizeof(full)) continue;
    struct stat st;
    if(lstat(full, &st) != 0) continue;
    if(S_ISREG(st.st_mode)) sum += st.st_size;
    else if(S_ISDIR(st.st_mode)) {
      int64_t sub = measure_tree(full, budget, deadline_ms, depth + 1);
      if(sub < 0) { sum = -1; break; }
      sum += sub;
    }
  }
  closedir(d);
  return sum;
}

static void *
scan_main(void *arg) {
  int measure = arg != NULL;
  ps5tm_pkgdrive_t *dr = calloc(PS5TM_MAX_VOLUMES, sizeof(*dr));
  dump_t *nl = calloc(DL_MAX, sizeof(*nl));
  ps5tm_libupd_t *lib = calloc(256, sizeof(*lib));
  unsigned nd = dr ? ps5tm_pkg_drives(dr, PS5TM_MAX_VOLUMES) : 0;
  int nlib = lib ? ps5tm_library_update_titles(lib, 256) : -1;
  if(nlib < 0) nlib = 0;
  ctx_t c = { nl, 0, 0, 0, NULL, NULL, 0, lib, nlib };
  unsigned counts[PS5TM_MAX_VOLUMES] = { 0 };
  for(unsigned i = 0; nl && dr && i < nd; i++) {
    const char *m = dr[i].mount;
    if(!dr[i].internal && strncmp(m, "/mnt/usb", 8) && strncmp(m, "/mnt/ext", 8)) continue;   /* the shell's own mounts are no place for dumps */
    c.mount = dr[i].mount;
    c.base = dr[i].base;
    c.drive = (int)i;
    c.dirs = 0;
    unsigned before = c.n;
    if(dr[i].internal) walk(&c, "homebrew", 1);                     /* the console's storage is not crawled from the top */
    else walk(&c, "", 0);
    counts[i] = c.n - before;
  }
  if(nl && measure) {
    uint64_t deadline = wall_ms() + DL_MEASURE_MS;
    for(unsigned k = 0; k < c.n; k++) {
      dump_t *d = &nl[k];
      if(!strcmp(d->state, "running")) continue;                    /* it is being written right now */
      const char *base = d->mount;
      for(unsigned i = 0; i < nd; i++) if(!strcmp(dr[i].mount, d->mount)) { base = dr[i].base; break; }
      char p[640];
      if(d->dir[0]) snprintf(p, sizeof(p), "%s/%s/%s", base, d->dir, d->folder);
      else snprintf(p, sizeof(p), "%s/%s", base, d->folder);
      unsigned budget = DL_MEASURE_CAP;
      d->measured = measure_tree(p, &budget, deadline, 0);
    }
  }
  pthread_mutex_lock(&g_lock);
  dump_t *old = g_list;
  g_list = nl;
  g_n = nl ? c.n : 0;
  g_ndrv = 0;
  for(unsigned i = 0; dr && i < nd; i++) {
    const char *m = dr[i].mount;
    if(!dr[i].internal && strncmp(m, "/mnt/usb", 8) && strncmp(m, "/mnt/ext", 8)) continue;
    snprintf(g_drv[g_ndrv].mount, sizeof(g_drv[0].mount), "%s", dr[i].mount);
    snprintf(g_drv[g_ndrv].label, sizeof(g_drv[0].label), "%s", dr[i].label);
    g_drv[g_ndrv].count = counts[i];
    g_ndrv++;
  }
  g_st.truncated = c.truncated;
  g_st.scanning = 0;
  g_st.ever = 1;
  g_st.finished_ms = wall_ms();
  pthread_mutex_unlock(&g_lock);
  free(old);
  free(dr);
  free(lib);
  return NULL;
}

/* 1 started, 0 one is under way already, -1 could not start */
int
ps5tm_dumplib_start(int measure) {
  pthread_mutex_lock(&g_lock);
  if(g_st.scanning) { pthread_mutex_unlock(&g_lock); return 0; }
  g_st.scanning = 1;
  g_st.measure = measure != 0;
  pthread_mutex_unlock(&g_lock);
  pthread_t th;
  pthread_attr_t at;
  pthread_attr_init(&at);
  pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
  pthread_attr_setstacksize(&at, 256 * 1024);
  int rc = pthread_create(&th, &at, scan_main, measure ? (void *)1 : NULL);
  pthread_attr_destroy(&at);
  if(rc != 0) {
    pthread_mutex_lock(&g_lock);
    g_st.scanning = 0;
    pthread_mutex_unlock(&g_lock);
    return -1;
  }
  return 1;
}

cJSON *
ps5tm_dumplib_json(void) {
  cJSON *root = cJSON_CreateObject();
  if(!root) return NULL;
  pthread_mutex_lock(&g_lock);
  cJSON_AddBoolToObject(root, "ok", 1);
  cJSON_AddBoolToObject(root, "scanning", g_st.scanning);
  cJSON_AddBoolToObject(root, "ever", g_st.ever);
  cJSON_AddBoolToObject(root, "measured", g_st.measure);
  cJSON_AddBoolToObject(root, "truncated", g_st.truncated);
  cJSON_AddNumberToObject(root, "scanned_at", g_st.ever ? (double)(g_st.finished_ms / 1000) : 0);
  cJSON_AddNumberToObject(root, "now", (double)(wall_ms() / 1000));
  cJSON *drives = cJSON_AddArrayToObject(root, "drives");
  for(unsigned i = 0; i < g_ndrv; i++) {
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "mount", g_drv[i].mount);
    cJSON_AddStringToObject(o, "label", g_drv[i].label);
    cJSON_AddNumberToObject(o, "count", g_drv[i].count);
    cJSON_AddItemToArray(drives, o);
  }
  cJSON *list = cJSON_AddArrayToObject(root, "dumps");
  for(unsigned i = 0; i < g_n; i++) {
    const dump_t *d = &g_list[i];
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "mount", d->mount);
    cJSON_AddStringToObject(o, "dir", d->dir);
    cJSON_AddStringToObject(o, "folder", d->folder);
    cJSON_AddStringToObject(o, "title_id", d->title_id);
    cJSON_AddStringToObject(o, "title", d->title);
    cJSON_AddStringToObject(o, "state", d->state);
    cJSON_AddStringToObject(o, "kind", d->kind == 2 ? "dlc" : d->kind == 1 ? "patch" : "game");
    cJSON_AddNumberToObject(o, "bytes", (double)d->bytes);
    if(d->measured >= 0) cJSON_AddNumberToObject(o, "measured", (double)d->measured);
    if(d->decrypt >= 0) cJSON_AddBoolToObject(o, "decrypt", d->decrypt != 0);
    if(d->fself >= 0) cJSON_AddBoolToObject(o, "fself", d->fself != 0);
    if(d->backport >= 0) cJSON_AddBoolToObject(o, "backport", d->backport != 0);
    if(d->started > 0) cJSON_AddNumberToObject(o, "started", (double)d->started);
    if(d->finished > 0) cJSON_AddNumberToObject(o, "finished", (double)d->finished);
    cJSON_AddItemToArray(list, o);
  }
  pthread_mutex_unlock(&g_lock);
  return root;
}

/* The folders of a PS4 dump for the image builder. The list entry is named by the three fields the page got from the list; it must
   still be in the list (the page cannot name a path), be a finished dump of a PS4 game, and its other half — the update beside the
   game, or the game under the update — is looked for on the same drive. */
int
ps5tm_dumplib_source(const char *mount, const char *dir, const char *folder, ps5tm_dumpsrc_t *out, char *err, size_t err_len) {
  memset(out, 0, sizeof(*out));
  if(!mount || !dir || !folder || !mount[0] || !folder[0]) { snprintf(err, err_len, "Dieser Dump ist nicht angegeben."); return 400; }
  dump_t base_e, patch_e, sel;
  memset(&base_e, 0, sizeof(base_e));
  memset(&patch_e, 0, sizeof(patch_e));
  memset(&sel, 0, sizeof(sel));
  int have_base = 0, have_patch = 0, found = 0;
  pthread_mutex_lock(&g_lock);
  for(unsigned i = 0; i < g_n; i++) {
    const dump_t *d = &g_list[i];
    if(!strcmp(d->mount, mount) && !strcmp(d->dir, dir) && !strcmp(d->folder, folder)) { sel = *d; found = 1; break; }
  }
  if(found) {
    for(unsigned i = 0; i < g_n; i++) {
      const dump_t *d = &g_list[i];
      if(d->kind == 2 || strcmp(d->title_id, sel.title_id) || strcmp(d->mount, sel.mount)) continue;
      if(d->state[0] && strcmp(d->state, "done")) continue;                           /* only finished dumps */
      int same_dir = !strcmp(d->dir, sel.dir);
      if(d->kind == 0 && (!have_base || (same_dir && strcmp(base_e.dir, sel.dir)))) { base_e = *d; have_base = 1; }
      if(d->kind == 1 && (!have_patch || (same_dir && strcmp(patch_e.dir, sel.dir)))) { patch_e = *d; have_patch = 1; }
    }
  }
  pthread_mutex_unlock(&g_lock);
  if(!found) { snprintf(err, err_len, "Diesen Dump kennt die Suche nicht (mehr). Bitte in der Liste der Dumps im Disc-Reiter auf „Aktualisieren“ drücken."); return 404; }
  if(sel.kind == 2) { snprintf(err, err_len, "Ein DLC-Ordner lässt sich nicht für sich als Abbild bauen."); return 400; }
  if(strncmp(sel.title_id, "CUSA", 4)) { snprintf(err, err_len, "Das ist kein PS4-Dump. PS5-Dumps lassen sich mit ShadowMountPlus direkt aus dem Ordner starten."); return 400; }
  if(sel.state[0] && strcmp(sel.state, "done")) { snprintf(err, err_len, "Dieser Dump ist nicht fertig."); return 409; }
  if(!have_base) { snprintf(err, err_len, "Zu diesem Update fehlt der Dump des Spiels selbst."); return 409; }

  ps5tm_pkgdrive_t *dr = calloc(PS5TM_MAX_VOLUMES, sizeof(*dr));
  if(!dr) { snprintf(err, err_len, "Kein Speicher."); return 500; }
  unsigned nd = ps5tm_pkg_drives(dr, PS5TM_MAX_VOLUMES);
  const char *root = NULL;
  for(unsigned i = 0; i < nd; i++) if(!strcmp(dr[i].mount, sel.mount)) { root = dr[i].base; break; }
  if(!root) { free(dr); snprintf(err, err_len, "Das Laufwerk des Dumps ist nicht mehr da."); return 409; }
  int bad = 0;
  const dump_t *parts[2] = { &base_e, have_patch ? &patch_e : NULL };
  char *dst[2] = { out->base, out->patch };
  for(int k = 0; k < 2 && !bad; k++) {
    if(!parts[k]) continue;
    int n = parts[k]->dir[0] ? snprintf(dst[k], 512, "%s/%s/%s", root, parts[k]->dir, parts[k]->folder)
                             : snprintf(dst[k], 512, "%s/%s", root, parts[k]->folder);
    if(n < 0 || n >= 512) bad = 1;
  }
  free(dr);
  if(bad) { snprintf(err, err_len, "Der Pfad des Dumps ist zu lang."); return 400; }
  snprintf(out->title_id, sizeof(out->title_id), "%s", sel.title_id);
  snprintf(out->title, sizeof(out->title), "%s", base_e.title[0] ? base_e.title : sel.title);
  return 200;
}

/* The folder that holds a dump of the list (dest, "<drive>[/dir]") and its state; the three strings are those of a list entry. */
int
ps5tm_dumplib_locate(const char *mount, const char *dir, const char *folder, char *dest, size_t dest_len, char *state,
                     size_t state_len, char *err, size_t err_len) {
  if(!mount || !dir || !folder || !mount[0] || !folder[0]) { snprintf(err, err_len, "Dieser Dump ist nicht angegeben."); return 400; }
  int found = 0;
  char st[16] = "";
  pthread_mutex_lock(&g_lock);
  for(unsigned i = 0; i < g_n; i++) {
    const dump_t *d = &g_list[i];
    if(!strcmp(d->mount, mount) && !strcmp(d->dir, dir) && !strcmp(d->folder, folder)) {
      found = 1;
      snprintf(st, sizeof(st), "%s", d->state);
      break;
    }
  }
  pthread_mutex_unlock(&g_lock);
  if(!found) { snprintf(err, err_len, "Diesen Dump kennt die Liste nicht (mehr). Bitte auf „Aktualisieren“ drücken."); return 404; }
  ps5tm_pkgdrive_t *dr = calloc(PS5TM_MAX_VOLUMES, sizeof(*dr));
  if(!dr) { snprintf(err, err_len, "Kein Speicher."); return 500; }
  unsigned nd = ps5tm_pkg_drives(dr, PS5TM_MAX_VOLUMES);
  const char *root = NULL;
  for(unsigned i = 0; i < nd; i++) if(!strcmp(dr[i].mount, mount)) { root = dr[i].base; break; }
  int rc = 200;
  if(!root) {
    snprintf(err, err_len, "Das Laufwerk des Dumps ist nicht mehr da.");
    rc = 409;
  } else {
    int n = dir[0] ? snprintf(dest, dest_len, "%s/%s", root, dir) : snprintf(dest, dest_len, "%s", root);
    if(n < 0 || (size_t)n >= dest_len) { snprintf(err, err_len, "Der Pfad des Dumps ist zu lang."); rc = 400; }
    else snprintf(state, state_len, "%s", st);
  }
  free(dr);
  return rc;
}

/* 0 nothing known, 1 a finished dump (a folder without an info file counts as finished, as in the dumper),
   2 only dumps that were cut short. DLC folders do not count. */
static int
dump_state_of(const char *title_id) {
  int found = 0;
  pthread_mutex_lock(&g_lock);
  for(unsigned i = 0; i < g_n; i++) {
    const dump_t *d = &g_list[i];
    if(d->kind == 2 || strcmp(d->title_id, title_id)) continue;
    if(!d->state[0] || !strcmp(d->state, "done")) { found = 1; break; }
    found = 2;
  }
  pthread_mutex_unlock(&g_lock);
  return found;
}

/* ------------------------------------------------------------------ disc games seen so far */

/* The dumper keeps a list of the disc games it has seen (disc_titles.txt), so that a disc game whose disc is out is still told from a
   package. Here it is /data/PS5-Cooling-Center/dump/disc_titles.txt, one title id per line, rewritten whole when one is added. */
#define DL_DISC_MAX  128
#define DL_DISC_FILE PS5TM_DATA_DIR "/dump/disc_titles.txt"

static pthread_mutex_t g_disc_mu = PTHREAD_MUTEX_INITIALIZER;
static char            g_disc_known[DL_DISC_MAX][12];
static unsigned        g_disc_n;
static int             g_disc_read;

static int
disc_known_locked(const char *tid) {
  for(unsigned i = 0; i < g_disc_n; i++)
    if(!strcmp(g_disc_known[i], tid)) return 1;
  return 0;
}

static void
disc_load_locked(void) {
  if(g_disc_read) return;
  FILE *f = fopen(DL_DISC_FILE, "r");
  if(!f) return;                          /* tried again at the next look until the file was read once */
  g_disc_read = 1;
  char line[64];
  while(g_disc_n < DL_DISC_MAX && fgets(line, sizeof(line), f)) {
    line[strcspn(line, "\r\n")] = 0;
    if(is_title_id(line) && !disc_known_locked(line)) snprintf(g_disc_known[g_disc_n++], sizeof(g_disc_known[0]), "%s", line);
  }
  fclose(f);
}

void
ps5tm_title_remember_disc(const char *title_id) {
  if(!is_title_id(title_id)) return;
  pthread_mutex_lock(&g_disc_mu);
  disc_load_locked();
  if(!disc_known_locked(title_id) && g_disc_n < DL_DISC_MAX) {
    snprintf(g_disc_known[g_disc_n++], sizeof(g_disc_known[0]), "%s", title_id);
    mkdir(PS5TM_DATA_DIR, 0777);
    mkdir(PS5TM_DATA_DIR "/dump", 0777);
    char tmp[sizeof(DL_DISC_FILE) + 8];
    snprintf(tmp, sizeof(tmp), "%s.tmp", DL_DISC_FILE);
    FILE *f = fopen(tmp, "w");
    if(f) {
      for(unsigned i = 0; i < g_disc_n; i++) fprintf(f, "%s\n", g_disc_known[i]);
      int bad = fflush(f) != 0;
      bad |= fclose(f) != 0;
      if(bad || rename(tmp, DL_DISC_FILE) != 0) unlink(tmp);
    }
  }
  pthread_mutex_unlock(&g_disc_mu);
}

static int
disc_remembered(const char *title_id) {
  pthread_mutex_lock(&g_disc_mu);
  disc_load_locked();
  int known = disc_known_locked(title_id);
  pthread_mutex_unlock(&g_disc_mu);
  return known;
}

/* The title's disc is in the drive: its folder is below /mnt/disc/app. Seen once, a disc game is remembered. */
int
ps5tm_title_on_disc(const char *title_id) {
  if(!is_title_id(title_id)) return 0;
  char p[64];
  snprintf(p, sizeof(p), "/mnt/disc/app/%s", title_id);
  if(!is_dir(p)) return 0;
  ps5tm_title_remember_disc(title_id);
  return 1;
}

/* ------------------------------------------------------------------ the marks */

void
ps5tm_title_marks(const char *title_id, ps5tm_title_marks_t *m) {
  memset(m, 0, sizeof(*m));
  m->install_pct = -1;
  if(!is_title_id(title_id)) return;

  char p[256];
  if(ps5tm_title_on_disc(title_id)) m->disc = 2;
  else if(disc_remembered(title_id)) m->disc = 1;
  else {
    snprintf(p, sizeof(p), "/system_data/priv/appmeta/%s/disc_info.dat", title_id);
    if(is_file(p)) m->disc = 1;
    else {
      snprintf(p, sizeof(p), "/system_data/playgo/%s/bdcopy.pbm", title_id);
      if(is_file(p)) m->disc = 1;
    }
  }

  time_t mt = 0;
  const char *root = NULL;
  for(int r = 0; k_app_roots[r]; r++) {
    int pct = installed_percent_at(k_app_roots[r], title_id, &mt);
    if(pct >= 0) { m->install_pct = pct; root = k_app_roots[r]; break; }
  }
  if(root && m->install_pct < 100) {
    int c = 0, w = 0, h = 0;
    if(playgo_at(root, title_id, &c, &w, &h) == 0) { m->pg_chunks = c; m->pg_wanted = w; m->pg_here = h; }
    int settled = m->pg_wanted && m->pg_here >= m->pg_wanted;
    m->install_pending = !settled && (time(NULL) - mt) < DL_INSTALL_QUIET_S;
  }

  for(int r = 0; k_addcont_roots[r]; r++) {
    char dir[160];
    snprintf(dir, sizeof(dir), "%s/%s", k_addcont_roots[r], title_id);
    DIR *d = opendir(dir);
    if(!d) continue;
    struct dirent *e;
    while((e = readdir(d)) != NULL && m->dlc_count < PS5TM_MARK_DLC) {
      if(strlen(e->d_name) != 16) continue;
      int ok = 1;
      for(const char *s = e->d_name; *s; s++)
        if(!((*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9'))) ok = 0;
      if(!ok) continue;
      int seen = 0;
      for(unsigned i = 0; i < m->dlc_count; i++) if(!strcmp(m->dlc[i], e->d_name)) seen = 1;
      if(seen) continue;
      char pkg[320];
      struct stat st;
      snprintf(pkg, sizeof(pkg), "%s/%s/ac.pkg", dir, e->d_name);
      if(stat(pkg, &st) != 0) continue;
      snprintf(m->dlc[m->dlc_count], sizeof(m->dlc[0]), "%s", e->d_name);
      m->dlc_bytes[m->dlc_count++] = (uint64_t)st.st_size;
    }
    closedir(d);
  }

  m->dump = dump_state_of(title_id);

  /* the first look at the games list starts one search, so the marks have something to go by; later searches only on
     the person's click or when the Dumps tab is opened */
  pthread_mutex_lock(&g_lock);
  int first = !g_st.ever && !g_st.scanning && !g_auto_tried;
  if(first) g_auto_tried = 1;
  pthread_mutex_unlock(&g_lock);
  if(first) ps5tm_dumplib_start(0);
}
