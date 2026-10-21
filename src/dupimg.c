/* The same game in more than one image (18.10.2026, after "Set aside" of exFAT2AMPR 1.95 by V-10, GPL-3.0; own code).
 *
 * ShadowMountPlus keeps one source per title id. Two images of one game on the drives make it take one, then the other,
 * scan after scan (it says "duplicate titleId" and asks to keep one source; the test console once moved Assassin's Creed
 * Mirage back and forth this way). Deleting the second copy is final; setting it aside is not: the image is renamed to
 * <name>.off, which ShadowMountPlus does not take for an image, and renamed back to undo it.
 *
 * Where the app looks: the top level of every drive and the folders one below it, and the homebrew and etaHEN game folders
 * (the places ShadowMountPlus and the dump tools use). Which game an image holds is read from it (sce_sys/param.json, or
 * param.sfo of a PS4 game) with the image reader, without mounting anything; an image the reader does not know is
 * taken by the title id in its name. What was read is kept by path, size and time, so a second look costs only the
 * listing. The folder ShadowMountPlus uses for the game counts as a source too, but only images are ever renamed. */

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "imgread.h"
#include "ioerr.h"
#include "ps5tm.h"
#include "third_party/cJSON.h"

#define DI_MAX       512                 /* images looked at in one search */
#define DI_PARAM_MAX (256u * 1024)
#define DI_STACK     (768 * 1024)        /* the image reader likes a big stack, as in imgjob.c */
#define DI_OFF       ".off"

#ifdef PS5TM_HOST_TEST
static const char *const k_roots[] = { "/tmp/ps5tm-dup/usb0", "/tmp/ps5tm-dup/usb1", "/tmp/ps5tm-dup/homebrew", NULL };
#else
static const char *const k_roots[] = {
  "/mnt/usb0", "/mnt/usb1", "/mnt/usb2", "/mnt/usb3", "/mnt/usb4", "/mnt/usb5", "/mnt/usb6", "/mnt/usb7",
  "/mnt/ext0", "/mnt/ext1", "/data/homebrew", "/data/etaHEN/games", NULL
};
#endif

typedef struct {
  char     path[512];
  char     tid[12];
  char     name[128];
  uint64_t size;
  int64_t  mtime;
  int      off;                          /* set aside (<name>.off) */
} di_img_t;

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static di_img_t       *g_cache;          /* what earlier searches read, by path, size and time */
static int             g_ncache;

/* ".exfat", ".ffpkg", ".ffpfs", ".ffpfsc", each also with ".off" after it. 1 image, 2 an image set aside, 0 else. */
static int
image_name(const char *name) {
  static const char *const ext[] = { ".exfat", ".ffpkg", ".ffpfs", ".ffpfsc", NULL };
  size_t n = strlen(name);
  int off = 0;
  if(n > 4 && !strcasecmp(name + n - 4, DI_OFF)) { off = 1; n -= 4; }
  for(int i = 0; ext[i]; i++) {
    size_t e = strlen(ext[i]);
    if(n > e && !strncasecmp(name + n - e, ext[i], e)) return off ? 2 : 1;
  }
  return 0;
}

static int
title_id_at(const char *s) {
  if(strncmp(s, "PPSA", 4) && strncmp(s, "CUSA", 4)) return 0;
  for(int i = 4; i < 9; i++)
    if(!isdigit((unsigned char)s[i])) return 0;
  return !isdigit((unsigned char)s[9]);
}

/* the first PPSA12345 / CUSA12345 in a file name */
static void
tid_from_name(const char *name, char *out, size_t n) {
  out[0] = 0;
  for(const char *p = name; *p; p++)
    if(title_id_at(p)) { snprintf(out, n, "%.9s", p); return; }
}

/* TITLE_ID and TITLE out of a param.sfo */
static void
from_sfo(const uint8_t *b, size_t len, char *tid, size_t tn, char *name, size_t nn) {
  if(len < 20 || memcmp(b, "\0PSF", 4)) return;
  uint32_t koff = b[8] | b[9] << 8 | b[10] << 16 | (uint32_t)b[11] << 24;
  uint32_t doff = b[12] | b[13] << 8 | b[14] << 16 | (uint32_t)b[15] << 24;
  uint32_t cnt = b[16] | b[17] << 8 | b[18] << 16 | (uint32_t)b[19] << 24;
  if(cnt > 512 || 20 + (uint64_t)cnt * 16 > len || koff >= len || doff >= len) return;
  for(uint32_t i = 0; i < cnt; i++) {
    const uint8_t *e = b + 20 + i * 16;
    uint32_t k = e[0] | e[1] << 8, dl = e[4] | e[5] << 8 | e[6] << 16 | (uint32_t)e[7] << 24;
    uint32_t dpos = e[12] | e[13] << 8 | e[14] << 16 | (uint32_t)e[15] << 24;
    if((uint64_t)koff + k >= len || (uint64_t)doff + dpos + dl > len || dl == 0) continue;
    const char *key = (const char *)b + koff + k;
    if(!memchr(key, 0, len - koff - k)) continue;
    const char *val = (const char *)b + doff + dpos;
    size_t vl = strnlen(val, dl);
    if(!strcmp(key, "TITLE_ID")) snprintf(tid, tn, "%.*s", (int)(vl < 9 ? vl : 9), val);
    else if(!strcmp(key, "TITLE")) snprintf(name, nn, "%.*s", (int)(vl < nn - 1 ? vl : nn - 1), val);
  }
}

/* titleId and the name in the default language out of a param.json */
static void
from_json(const char *b, size_t len, char *tid, size_t tn, char *name, size_t nn) {
  cJSON *j = cJSON_ParseWithLength(b, len);
  if(!j) return;
  const cJSON *t = cJSON_GetObjectItem(j, "titleId");
  if(cJSON_IsString(t)) snprintf(tid, tn, "%.9s", t->valuestring);
  const cJSON *lp = cJSON_GetObjectItem(j, "localizedParameters");
  const cJSON *dl = lp ? cJSON_GetObjectItem(lp, "defaultLanguage") : NULL;
  const cJSON *loc = cJSON_IsString(dl) ? cJSON_GetObjectItem(lp, dl->valuestring) : NULL;
  const cJSON *tn2 = loc ? cJSON_GetObjectItem(loc, "titleName") : NULL;
  if(cJSON_IsString(tn2)) snprintf(name, nn, "%s", tn2->valuestring);
  cJSON_Delete(j);
}

/* which game an image holds: read from it, else from its name */
static void
identify(di_img_t *m) {
  m->tid[0] = 0;
  m->name[0] = 0;
  if(!m->off) {
    imgr_t *r = imgr_open(m->path, NULL, 0);
    if(r) {
      uint8_t *b = malloc(DI_PARAM_MAX);
      if(b) {
        long n = imgr_pread(r, "/sce_sys/param.json", 0, b, DI_PARAM_MAX);
        if(n > 0) from_json((const char *)b, (size_t)n, m->tid, sizeof(m->tid), m->name, sizeof(m->name));
        if(!m->tid[0] && (n = imgr_pread(r, "/sce_sys/param.sfo", 0, b, DI_PARAM_MAX)) > 0)
          from_sfo(b, (size_t)n, m->tid, sizeof(m->tid), m->name, sizeof(m->name));
        free(b);
      }
      imgr_close(r);
    }
  }
  if(!title_id_at(m->tid)) {                     /* an .ffpfs, a set-aside one or a damaged one: the name says it, if anything */
    const char *slash = strrchr(m->path, '/');
    tid_from_name(slash ? slash + 1 : m->path, m->tid, sizeof(m->tid));
  }
}

typedef struct {
  di_img_t *v;
  int       n;
} di_list_t;

static void
add_file(di_list_t *l, const char *path, const char *name) {
  int kind = image_name(name);
  if(!kind || l->n >= DI_MAX) return;
  struct stat st;
  if(lstat(path, &st) != 0 || !S_ISREG(st.st_mode) || strlen(path) >= sizeof(l->v[0].path)) return;
  di_img_t *m = &l->v[l->n];
  memset(m, 0, sizeof(*m));
  snprintf(m->path, sizeof(m->path), "%s", path);
  m->size = (uint64_t)st.st_size;
  m->mtime = (int64_t)st.st_mtime;
  m->off = kind == 2;
  pthread_mutex_lock(&g_lock);
  int hit = 0;
  for(int i = 0; i < g_ncache && !hit; i++)
    if(!strcmp(g_cache[i].path, m->path) && g_cache[i].size == m->size && g_cache[i].mtime == m->mtime) { *m = g_cache[i]; hit = 1; }
  pthread_mutex_unlock(&g_lock);
  if(!hit) identify(m);
  l->n++;
}

/* the files of dir; with depth 1 also those of the folders in it */
static void
scan_dir(di_list_t *l, const char *dir, int depth) {
  DIR *d = opendir(dir);
  if(!d) return;
  struct dirent *e;
  char p[512];
  while((e = readdir(d)) != NULL && l->n < DI_MAX) {
    if(e->d_name[0] == '.') continue;
    if(snprintf(p, sizeof(p), "%s/%s", dir, e->d_name) >= (int)sizeof(p)) continue;
    struct stat st;
    if(lstat(p, &st) != 0) continue;
    if(S_ISREG(st.st_mode)) add_file(l, p, e->d_name);
    else if(S_ISDIR(st.st_mode) && depth > 0 && strcasecmp(e->d_name, "ps4") && strcasecmp(e->d_name, "backports")) scan_dir(l, p, depth - 1);
  }
  closedir(d);
}

static void *
search_thread(void *arg) {
  di_list_t *l = arg;
  for(int r = 0; k_roots[r]; r++) {
    struct stat st;
    if(stat(k_roots[r], &st) != 0 || !S_ISDIR(st.st_mode)) continue;
    scan_dir(l, k_roots[r], 1);
  }
  pthread_mutex_lock(&g_lock);                   /* this search is the cache of the next one */
  free(g_cache);
  g_cache = malloc((size_t)(l->n ? l->n : 1) * sizeof(*g_cache));
  g_ncache = 0;
  if(g_cache) { memcpy(g_cache, l->v, (size_t)l->n * sizeof(*g_cache)); g_ncache = l->n; }
  pthread_mutex_unlock(&g_lock);
  return NULL;
}

static int
search(di_list_t *l) {
  l->v = calloc(DI_MAX, sizeof(*l->v));
  l->n = 0;
  if(!l->v) return -1;
  pthread_t th;
  pthread_attr_t at;
  pthread_attr_init(&at);
  pthread_attr_setstacksize(&at, DI_STACK);
  int rc = pthread_create(&th, &at, search_thread, l);
  pthread_attr_destroy(&at);
  if(rc != 0) { free(l->v); l->v = NULL; return -1; }
  pthread_join(th, NULL);
  return 0;
}

static void
add_entry(cJSON *arr, const di_img_t *m, const ps5tm_smp_game_t *sg) {
  cJSON *o = cJSON_CreateObject();
  cJSON_AddStringToObject(o, "path", m->path);
  cJSON_AddNumberToObject(o, "size", (double)m->size);
  cJSON_AddBoolToObject(o, "set_aside", m->off);
  int used = sg && !strcmp(sg->path, m->path);
  cJSON_AddBoolToObject(o, "smp_uses", used);
  cJSON_AddBoolToObject(o, "mounted", used && sg->mounted);
  cJSON_AddItemToArray(arr, o);
}

/* {"ok":true,"groups":[{title_id, name, sources:[{path, size, set_aside, smp_uses, mounted, folder?}]}], "aside":[...]}:
   groups are the titles with two or more sources that ShadowMountPlus would take; "aside" every image set aside. */
cJSON *
ps5tm_dupimg_list(void) {
  di_list_t l;
  if(search(&l) != 0) return NULL;
  cJSON *o = cJSON_CreateObject();
  cJSON_AddBoolToObject(o, "ok", 1);
  cJSON_AddNumberToObject(o, "images", l.n);
  cJSON *groups = cJSON_AddArrayToObject(o, "groups");
  cJSON *aside = cJSON_AddArrayToObject(o, "aside");
  char *done = calloc((size_t)(l.n ? l.n : 1), 1);
  for(int i = 0; done && i < l.n; i++) {
    if(done[i] || !l.v[i].tid[0]) continue;
    const char *tid = l.v[i].tid;
    ps5tm_smp_game_t sg;
    int known = ps5tm_smp_find(tid, &sg) == 0;
    int live = 0, folder = known && !strcmp(sg.source_type, "folder");
    for(int k = i; k < l.n; k++)
      if(!strcmp(l.v[k].tid, tid) && !l.v[k].off) live++;
    if(live + folder >= 2) {
      cJSON *g = cJSON_CreateObject();
      cJSON_AddStringToObject(g, "title_id", tid);
      const char *nm = "";
      for(int k = i; k < l.n && !nm[0]; k++)
        if(!strcmp(l.v[k].tid, tid)) nm = l.v[k].name;
      cJSON_AddStringToObject(g, "name", nm);
      cJSON *src = cJSON_AddArrayToObject(g, "sources");
      for(int k = i; k < l.n; k++)
        if(!strcmp(l.v[k].tid, tid) && !l.v[k].off) add_entry(src, &l.v[k], known ? &sg : NULL);
      if(folder) {
        cJSON *f = cJSON_CreateObject();
        cJSON_AddStringToObject(f, "path", sg.path);
        cJSON_AddBoolToObject(f, "folder", 1);
        cJSON_AddBoolToObject(f, "smp_uses", 1);
        cJSON_AddBoolToObject(f, "mounted", sg.mounted);
        cJSON_AddItemToArray(src, f);
      }
      cJSON_AddItemToArray(groups, g);
    }
    for(int k = i; k < l.n; k++)
      if(!strcmp(l.v[k].tid, tid)) done[k] = 1;
  }
  for(int i = 0; i < l.n; i++)
    if(l.v[i].off) {
      cJSON *a = cJSON_CreateObject();
      cJSON_AddStringToObject(a, "path", l.v[i].path);
      cJSON_AddStringToObject(a, "title_id", l.v[i].tid);
      cJSON_AddNumberToObject(a, "size", (double)l.v[i].size);
      cJSON_AddItemToArray(aside, a);
    }
  free(done);
  free(l.v);
  return o;
}

static int
in_roots(const char *path) {
  for(int r = 0; k_roots[r]; r++) {
    size_t n = strlen(k_roots[r]);
    if(!strncmp(path, k_roots[r], n) && path[n] == '/') return 1;
  }
  return 0;
}

/* Renames an image to <name>.off (undo 0) or back (undo 1). 200, or an HTTP status with the reason in err. */
int
ps5tm_dupimg_set_aside(const char *path, int undo, char *err, size_t n) {
  if(!path || path[0] != '/' || ps5tm_path_has_dotdot(path) || !in_roots(path) || strlen(path) >= 500) {
    snprintf(err, n, "Diese Datei lässt sich hier nicht beiseitelegen.");
    return 400;
  }
  const char *slash = strrchr(path, '/');
  int kind = image_name(slash + 1);
  if(kind != (undo ? 2 : 1)) { snprintf(err, n, undo ? "Das ist kein beiseitegelegtes Abbild." : "Das ist kein Abbild eines Spiels."); return 400; }
  struct stat st;
  if(lstat(path, &st) != 0 || !S_ISREG(st.st_mode)) { snprintf(err, n, "Die Datei gibt es nicht mehr."); return 404; }
  char to[512];
  if(undo) snprintf(to, sizeof(to), "%.*s", (int)(strlen(path) - 4), path);
  else snprintf(to, sizeof(to), "%s" DI_OFF, path);
  if(lstat(to, &st) == 0) { snprintf(err, n, "Es gibt schon eine Datei mit dem Namen %s.", strrchr(to, '/') + 1); return 409; }
  if(!undo) {
    di_img_t m;                                  /* the title from the last search (no image is read on this thread), else from the name */
    memset(&m, 0, sizeof(m));
    pthread_mutex_lock(&g_lock);
    for(int i = 0; i < g_ncache; i++)
      if(!strcmp(g_cache[i].path, path)) { snprintf(m.tid, sizeof(m.tid), "%s", g_cache[i].tid); break; }
    pthread_mutex_unlock(&g_lock);
    if(!m.tid[0]) tid_from_name(slash + 1, m.tid, sizeof(m.tid));
    ps5tm_smp_game_t sg;
    if(m.tid[0] && ps5tm_procmgr_title_running(m.tid) == 1) { snprintf(err, n, "Das Spiel läuft gerade. Bitte zuerst beenden."); return 409; }
    if(m.tid[0] && ps5tm_smp_find(m.tid, &sg) == 0 && !strcmp(sg.path, path) && sg.mounted) {
      snprintf(err, n, "ShadowMountPlus hat dieses Abbild gerade eingehängt. Erst wenn das Spiel nicht mehr läuft und es wieder "
               "ausgehängt ist, lässt es sich beiseitelegen.");
      return 409;
    }
  }
  if(rename(path, to) != 0) { snprintf(err, n, "Das Umbenennen ging nicht: %s", ps5tm_io_strerror(errno)); return 500; }
  PS5TM_INFO(undo ? "dupimg_back" : "dupimg_aside", "%s: %s → %s", undo ? "Abbild zurückgeholt" : "Abbild beiseitegelegt", path, to);
  ps5tm_smp_forget();
  ps5tm_library_forget();
  snprintf(err, n, "%s", undo ? "Das Abbild heißt wieder wie vorher; ShadowMountPlus findet es bei seiner nächsten Suche."
                              : "Das Abbild endet jetzt auf .off; ShadowMountPlus nimmt es nicht mehr.");
  return 200;
}
