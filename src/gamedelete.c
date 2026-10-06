/* Deleting a game, cleanly (06.10.2026). The user's words: a delete function "für alle Spiele (saubere
 * Deinstallation bzw. sauberes Löschen)", and "bevor ein Spiel gelöscht oder deinstalliert wird, muss zuerst
 * bestätigt werden".
 *
 * Three kinds of thing can be deleted:
 *   installed  any other title on the system's list (a package, a fake package, one without app.pkg, one mounted by a
 *              link without ShadowMountPlus): the system's own uninstall, the way the console's menu does it, made by
 *              the install helper (ps5tm_pkginst_uninstall): the game, then its updates and add-ons. Then the app
 *              waits until the title has left the list and its folder is gone. Data a link points at stays (keeps).
 *   smp        a game ShadowMountPlus mounts from an image or a folder on a drive: SMP takes it off first (its own
 *              uninstall, which unmounts it; alone it would mount the file again at its next scan), then the image or
 *              the folder is deleted, and the checksum file next to it if there is one.
 *   backup     a copy or conversion this app made (PS5-Sicherung/Spiele on a drive): the file or the folder, and its
 *              checksum file.
 * Saved games are never touched: they are not part of any of these.
 *
 * Confirmation. A deletion starts only with the token its plan handed out: one use, ten minutes, for exactly that game
 * or backup. The page shows the plan, has the person tick that it is final and click twice, and sends the token back.
 * A running game is never deleted, and nothing is while another job of the app runs.
 *
 * Deleting files never follows a link, never leaves the drive it started on, and removes only below a drive's mount
 * point or /data (never the mount point itself, never the app's own folder). */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "checkfile.h"
#include "ioerr.h"
#include "ps5tm.h"
#include "third_party/cJSON.h"

#ifndef DL_WAIT_GONE_S
#define DL_WAIT_GONE_S    180          /* how long the system may take to take a title off after it said yes */
#endif
#ifndef DL_TOKEN_MS
#define DL_TOKEN_MS       600000       /* a plan's token is good for ten minutes */
#endif
#define DL_MAX_DEPTH      64
#define DL_SIZE_FILES_MAX 400000       /* a folder's size is counted up to this many entries */
#define DL_BACKUPS_MAX    200
/* A big file is shortened in steps of this much before it is removed, so the bar moves while a single image of many
   gigabytes is freed (one unlink would jump from 0 to 100 %). */
#ifndef DL_TRUNC_STEP
#define DL_TRUNC_STEP     (512ll << 20)
#endif
#define BACKUP_DIR        "PS5-Sicherung/Spiele"
/* Where the console's drives hang: "" on the console; a host test sets a folder of its own, and nothing outside it is
   then looked at or deleted. */
#ifndef DL_ROOT
#define DL_ROOT           ""
#endif
#define APP_DATA_DIR      DL_ROOT "/data/PS5-Cooling-Center"

enum { DL_IDLE, DL_RUNNING, DL_DONE, DL_FAILED };
static const char *const k_state[] = { "idle", "deleting", "done", "failed" };

typedef struct {
  int      state;
  char     kind[12];
  char     id[320];
  char     name[160];
  char     phase[96];
  char     error[640];
  char     note[400];
  uint64_t files, bytes;
  uint64_t started_ms, finished_ms;
  /* progress: of total bytes, done are gone; that maps onto pct_lo..pct_hi of the bar (the steps before and after
     have the rest). total 0: nothing to count, the bar stands at pct_lo. */
  uint64_t total, done;
  int      pct_lo, pct_hi;
} dl_job_t;

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static dl_job_t        g_job;
static struct {
  char     token[33];
  char     kind[12];
  char     id[320];
  uint64_t until_ms;
} g_tok;

typedef struct {
  char kind[12];
  char id[320];
} dl_args_t;


/* ------------------------------------------------------------------ small things */

static int
title_id_ok(const char *s) {
  if(!s || strlen(s) != 9) return 0;
  for(int i = 0; i < 4; i++) if(s[i] < 'A' || s[i] > 'Z') return 0;
  for(int i = 4; i < 9; i++) if(s[i] < '0' || s[i] > '9') return 0;
  return 1;
}

static int
ends_with(const char *s, const char *tail) {
  size_t a = strlen(s), b = strlen(tail);
  return a >= b && !strcmp(s + a - b, tail);
}

static void
job_phase(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void
job_phase(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  pthread_mutex_lock(&g_lock);
  vsnprintf(g_job.phase, sizeof(g_job.phase), fmt, ap);
  pthread_mutex_unlock(&g_lock);
  va_end(ap);
}

/* The next part of the bar: pct_lo..pct_hi for total bytes, none done yet. */
static void
job_stage(int lo, int hi, uint64_t total) {
  pthread_mutex_lock(&g_lock);
  g_job.pct_lo = lo;
  g_job.pct_hi = hi;
  g_job.total = total;
  g_job.done = 0;
  pthread_mutex_unlock(&g_lock);
}

#ifdef DL_TEST_HOOK
void DL_TEST_HOOK(uint64_t done, uint64_t total);    /* host test only: sees every step of the bar */
#endif

static void
job_done_bytes(uint64_t done) {
  pthread_mutex_lock(&g_lock);
  g_job.done = done;
#ifdef DL_TEST_HOOK
  DL_TEST_HOOK(done, g_job.total);
#endif
  pthread_mutex_unlock(&g_lock);
}

static void
job_end(int state, const char *error, const char *note) {
  pthread_mutex_lock(&g_lock);
  g_job.state = state;
  snprintf(g_job.error, sizeof(g_job.error), "%s", error ? error : "");
  snprintf(g_job.note, sizeof(g_job.note), "%s", note ? note : "");
  g_job.finished_ms = ps5tm_mono_ms();
  pthread_mutex_unlock(&g_lock);
}

static void
make_token(char out[33]) {
  unsigned char r[16];
  int fd = open("/dev/urandom", O_RDONLY);
  ssize_t got = fd >= 0 ? read(fd, r, sizeof(r)) : -1;
  if(fd >= 0) close(fd);
  if(got != (ssize_t)sizeof(r)) {
    uint64_t t = ps5tm_mono_ms() * 6364136223846793005ull + (uint64_t)getpid();
    for(size_t i = 0; i < sizeof(r); i++) { t = t * 6364136223846793005ull + 1442695040888963407ull; r[i] = (unsigned char)(t >> 33); }
  }
  static const char hex[] = "0123456789abcdef";
  for(size_t i = 0; i < sizeof(r); i++) { out[2 * i] = hex[r[i] >> 4]; out[2 * i + 1] = hex[r[i] & 15]; }
  out[32] = 0;
}

/* A game that is running, by the kernel where it can tell, by the cached reading otherwise (a paused game counts). */
static int
game_running(char *id, size_t n) {
  id[0] = 0;
  if(ps5tm_procmgr_appinfo_ready()) {
    char t[16] = {0};
    if(ps5tm_procmgr_game_title(0, t, sizeof(t)) == 0 && t[0]) { snprintf(id, n, "%s", t); return 1; }
  }
  ps5tm_gamestate_t gs;
  ps5tm_gamestate_get(&gs);
  if(gs.title_id[0]) { snprintf(id, n, "%s", gs.title_id); return 1; }
  return 0;
}

static int
other_job_active(char *err, size_t err_len) {
  if(ps5tm_gamecopy_busy() || ps5tm_gameconvert_busy() || ps5tm_gamemove_busy() || ps5tm_saves_busy() ||
     ps5tm_pkgsplit_busy() || ps5tm_pkginst_busy()) {
    snprintf(err, err_len, "Es läuft gerade ein anderer Vorgang (Kopieren, Konvertieren, Verschieben, Spielstände, "
             "ein Paket teilen oder installieren). Gelöscht wird erst danach.");
    return 1;
  }
  return 0;
}


/* ------------------------------------------------------------------ where deleting is allowed */

/* Below which mount point a path lies: /mnt/usbN, /mnt/ext0, /mnt/ext1 or /data; NULL for anything else. The path
   must have a name below that point, must be absolute and plain (no "." or ".." parts, no doubled slashes), and must
   not be the app's own folder or lie in it. */
static const char *
deletable_root(const char *p) {
  static const char *const fixed[] = { DL_ROOT "/mnt/ext0", DL_ROOT "/mnt/ext1", DL_ROOT "/data" };
  if(!p || p[0] != '/' || strlen(p) >= 300 || strstr(p, "//") || strstr(p, "/./") || strstr(p, "/../") ||
     ends_with(p, "/.") || ends_with(p, "/..") || ends_with(p, "/"))
    return NULL;
  if(!strncmp(p, APP_DATA_DIR, strlen(APP_DATA_DIR)) && (p[strlen(APP_DATA_DIR)] == 0 || p[strlen(APP_DATA_DIR)] == '/'))
    return NULL;
  for(size_t i = 0; i < sizeof(fixed) / sizeof(fixed[0]); i++) {
    size_t l = strlen(fixed[i]);
    if(!strncmp(p, fixed[i], l) && p[l] == '/' && p[l + 1]) return fixed[i];
  }
  const size_t ul = strlen(DL_ROOT "/mnt/usb");
  if(!strncmp(p, DL_ROOT "/mnt/usb", ul) && p[ul] >= '0' && p[ul] <= '9') {
    size_t l = ul + 1;
    while(p[l] >= '0' && p[l] <= '9') l++;
    if(p[l] == '/' && p[l + 1]) return DL_ROOT "/mnt/usb";
  }
  return NULL;
}

typedef struct {
  dev_t    dev;
  uint64_t files, bytes;
  int      err;
  int      count_only;
  uint64_t seen;
  int      live;            /* removing: tell the job how far it is (bytes gone) */
} walk_t;

/* A big regular file, shortened from its end in steps, each step reported; it is removed afterwards by the caller.
   Only a file with a single name (a second name elsewhere would lose its content too) that is still the one looked
   at. A step that fails just ends the shortening: the unlink that follows frees the rest. */
static void
shrink_big(const char *path, const struct stat *st, walk_t *w) {
  if(st->st_nlink != 1 || st->st_size <= (off_t)DL_TRUNC_STEP) return;
  int fd = open(path, O_WRONLY | O_NOFOLLOW);
  if(fd < 0) return;
  struct stat fs;
  if(fstat(fd, &fs) == 0 && fs.st_dev == st->st_dev && fs.st_ino == st->st_ino && S_ISREG(fs.st_mode)) {
    off_t left = fs.st_size;
    while(left > (off_t)DL_TRUNC_STEP) {
      left -= (off_t)DL_TRUNC_STEP;
      if(ftruncate(fd, left) != 0) break;
      if(w->live) job_done_bytes(w->bytes + (uint64_t)(fs.st_size - left));
    }
  }
  close(fd);
}

/* Counts (count_only) or removes path and all below it: no link is followed (a link is removed as a link), nothing on
   another drive is entered, and the depth is bounded. */
static void
walk(const char *path, int depth, walk_t *w) {
  if(w->err) return;
  if(depth > DL_MAX_DEPTH) { w->err = ELOOP; return; }
  struct stat st;
  if(lstat(path, &st) != 0) { w->err = errno; return; }
  if(w->count_only && ++w->seen > DL_SIZE_FILES_MAX) { w->err = E2BIG; return; }
  if(S_ISDIR(st.st_mode)) {
    if(st.st_dev != w->dev) { w->err = EXDEV; return; }
    DIR *d = opendir(path);
    if(!d) { w->err = errno; return; }
    struct dirent *e;
    char child[1024];
    while(!w->err && (e = readdir(d)) != NULL) {
      if(!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
      if(snprintf(child, sizeof(child), "%s/%s", path, e->d_name) >= (int)sizeof(child)) { w->err = ENAMETOOLONG; break; }
      walk(child, depth + 1, w);
    }
    closedir(d);
    if(!w->err && !w->count_only && rmdir(path) != 0) w->err = errno;
    return;
  }
  if(!w->count_only && S_ISREG(st.st_mode)) shrink_big(path, &st, w);
  if(!w->count_only && unlink(path) != 0) { w->err = errno; return; }
  w->files++;
  if(S_ISREG(st.st_mode)) w->bytes += (uint64_t)st.st_size;
  if(w->live) job_done_bytes(w->bytes);
}

/* The size of a file or a folder; -1 when it could not be counted (too many entries, unreadable). */
static int64_t
size_of(const char *path) {
  struct stat st;
  if(lstat(path, &st) != 0) return -1;
  if(!S_ISDIR(st.st_mode)) return (int64_t)st.st_size;
  walk_t w = { st.st_dev, 0, 0, 0, 1, 0, 0 };
  walk(path, 0, &w);
  return w.err ? -1 : (int64_t)w.bytes;
}

/* Deletes path (a file or a folder) and, next to it, its checksum file, moving the bar from lo to hi as the bytes go.
   0, or an errno with the reason in why. */
static int
delete_path(const char *path, int lo, int hi, uint64_t *files, uint64_t *bytes, char *why, size_t why_len) {
  if(!deletable_root(path)) { snprintf(why, why_len, "Dieser Ort ist zum Löschen nicht erlaubt: %s", path); return EPERM; }
  struct stat st;
  if(lstat(path, &st) != 0) { snprintf(why, why_len, "%s ist nicht (mehr) da: %s", path, ps5tm_io_strerror(errno)); return errno ? errno : ENOENT; }
  if(S_ISLNK(st.st_mode) || !(S_ISDIR(st.st_mode) || S_ISREG(st.st_mode))) {
    snprintf(why, why_len, "%s ist weder Datei noch Ordner (eine Verknüpfung wird nicht gelöscht).", path);
    return EPERM;
  }
  int64_t total = size_of(path);                        /* -1 (not countable): the bar stays at lo until the end */
  job_stage(lo, hi, total > 0 ? (uint64_t)total : 0);
  walk_t w = { st.st_dev, 0, 0, 0, 0, 0, 1 };
  walk(path, 0, &w);
  *files += w.files;
  *bytes += w.bytes;
  if(w.err) {
    snprintf(why, why_len, "Beim Löschen von %s: %s (%llu Dateien waren schon gelöscht).", path,
             w.err == EXDEV ? "Ein Teil liegt auf einem anderen Laufwerk und wurde nicht angefasst" : ps5tm_io_strerror(w.err),
             (unsigned long long)w.files);
    return w.err;
  }
  char sums[1100];
  struct stat ss;
  if(snprintf(sums, sizeof(sums), "%s" PS5TM_SUMS_EXT, path) < (int)sizeof(sums) && lstat(sums, &ss) == 0 && S_ISREG(ss.st_mode)) {
    if(unlink(sums) == 0) (*files)++;
  }
  return 0;
}


/* ------------------------------------------------------------------ the app's backups */

typedef struct {
  char    path[320];
  char    name[200];
  char    drive[40];
  char    type[12];
  int64_t size;
  int64_t mtime;
  int     unfinished;
  int     sums;
} backup_t;

/* Every backup in PS5-Sicherung/Spiele on /data and on every drive the console has now. */
static int
list_backups(backup_t *out, int max) {
  ps5tm_sysinfo_t info;
  ps5tm_sysinfo_get(&info);
  char bases[PS5TM_MAX_VOLUMES + 1][48];
  char labels[PS5TM_MAX_VOLUMES + 1][40];
  int nb = 0;
  snprintf(bases[nb], sizeof(bases[nb]), DL_ROOT "/data");
  snprintf(labels[nb++], sizeof(labels[0]), "Interne SSD");
  for(unsigned i = 0; i < info.volume_count && nb < PS5TM_MAX_VOLUMES + 1; i++) {
    if(!strcmp(info.volumes[i].path, DL_ROOT "/user") || !strcmp(info.volumes[i].path, DL_ROOT "/data")) continue;
    snprintf(bases[nb], sizeof(bases[nb]), "%s", info.volumes[i].path);
    snprintf(labels[nb++], sizeof(labels[0]), "%s", info.volumes[i].label[0] ? info.volumes[i].label : info.volumes[i].path);
  }
  int n = 0;
  for(int b = 0; b < nb && n < max; b++) {
    char dir[160];
    snprintf(dir, sizeof(dir), "%s/" BACKUP_DIR, bases[b]);
    DIR *d = opendir(dir);
    if(!d) continue;
    struct dirent *e;
    while(n < max && (e = readdir(d)) != NULL) {
      const char *nm = e->d_name;
      if(nm[0] == '.' || ends_with(nm, PS5TM_SUMS_EXT)) continue;
      backup_t *k = &out[n];
      memset(k, 0, sizeof(*k));
      if(snprintf(k->path, sizeof(k->path), "%s/%s", dir, nm) >= (int)sizeof(k->path)) continue;
      struct stat st;
      if(lstat(k->path, &st) != 0 || S_ISLNK(st.st_mode) || !(S_ISDIR(st.st_mode) || S_ISREG(st.st_mode))) continue;
      snprintf(k->name, sizeof(k->name), "%s", nm);
      snprintf(k->drive, sizeof(k->drive), "%s", labels[b]);
      k->mtime = (int64_t)st.st_mtime;
      if(S_ISDIR(st.st_mode)) {
        snprintf(k->type, sizeof(k->type), "Ordner");
        char mark[400];
        struct stat ms;
        snprintf(mark, sizeof(mark), "%s/.ps5cc-kopie-unfertig", k->path);
        k->unfinished = lstat(mark, &ms) == 0;
      } else {
        const char *dot = strrchr(nm, '.');
        snprintf(k->type, sizeof(k->type), "%s", dot && strlen(dot) < sizeof(k->type) ? dot + 1 : "Datei");
        k->unfinished = strstr(nm, ".ps5cc-") != NULL;
      }
      k->size = size_of(k->path);
      char sums[400];
      struct stat ss;
      snprintf(sums, sizeof(sums), "%s" PS5TM_SUMS_EXT, k->path);
      k->sums = lstat(sums, &ss) == 0 && S_ISREG(ss.st_mode);
      n++;
    }
    closedir(d);
  }
  return n;
}

cJSON *
ps5tm_gamedelete_backups_json(void) {
  backup_t *v = calloc(DL_BACKUPS_MAX, sizeof(*v));
  cJSON *root = cJSON_CreateObject();
  cJSON_AddBoolToObject(root, "ok", v != NULL);
  cJSON *arr = cJSON_AddArrayToObject(root, "backups");
  if(!v) return root;
  int n = list_backups(v, DL_BACKUPS_MAX);
  for(int i = 0; i < n; i++) {
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "path", v[i].path);
    cJSON_AddStringToObject(o, "name", v[i].name);
    cJSON_AddStringToObject(o, "drive", v[i].drive);
    cJSON_AddStringToObject(o, "type", v[i].type);
    cJSON_AddNumberToObject(o, "size_bytes", (double)v[i].size);
    cJSON_AddNumberToObject(o, "modified", (double)v[i].mtime);
    cJSON_AddBoolToObject(o, "unfinished", v[i].unfinished);
    cJSON_AddBoolToObject(o, "sums", v[i].sums);
    cJSON_AddItemToArray(arr, o);
  }
  free(v);
  return root;
}

/* The backup with this path, from a fresh listing: the path is never taken from the request alone. */
static int
find_backup(const char *path, backup_t *out) {
  backup_t *v = calloc(DL_BACKUPS_MAX, sizeof(*v));
  if(!v) return -1;
  int n = list_backups(v, DL_BACKUPS_MAX), rc = -1;
  for(int i = 0; i < n; i++)
    if(!strcmp(v[i].path, path)) { *out = v[i]; rc = 0; break; }
  free(v);
  return rc;
}


/* ------------------------------------------------------------------ the plan */

typedef struct {
  char    kind[12];         /* "installed", "smp", "backup" */
  char    name[160];
  char    path[320];
  char    format[12];
  char    version[16];
  int64_t size;
  char    why[400];         /* why it cannot be deleted; empty when it can */
  char    keeps[320];       /* data that stays where it is (a title mounted by a link without ShadowMountPlus) */
} plan_t;

/* What deleting kind/id would do. 0 when there is such a thing (why says if it cannot be deleted now), 404 when
   there is not, 400 for a request that names nothing. */
static int
make_plan(const char *kind, const char *id, plan_t *p) {
  memset(p, 0, sizeof(*p));
  p->size = -1;
  if(!kind || !id) return 400;
  if(!strcmp(kind, "game")) {
    if(!title_id_ok(id)) return 400;
    ps5tm_libdel_t li;
    if(ps5tm_library_delete_info(id, &li) != 0) return 404;
    snprintf(p->name, sizeof(p->name), "%s", li.name);
    snprintf(p->format, sizeof(p->format), "%s", li.format);
    snprintf(p->version, sizeof(p->version), "%s", li.version);
    p->size = li.size;
    if(li.smp) {
      snprintf(p->kind, sizeof(p->kind), "smp");
      ps5tm_smp_game_t s;
      if(ps5tm_smp_find(id, &s) != 0) {
        snprintf(p->why, sizeof(p->why), "ShadowMountPlus antwortet nicht oder kennt das Spiel gerade nicht. Bitte ShadowMountPlus prüfen.");
        return 0;
      }
      snprintf(p->path, sizeof(p->path), "%s", s.path);
      if(!ps5tm_smp_can("uninstall_game")) {
        snprintf(p->why, sizeof(p->why), "Diese Fassung von ShadowMountPlus kann Spiele nicht abmelden. Ohne Abmelden würde es die Datei wieder einbinden.");
        return 0;
      }
      if(!s.available) {
        snprintf(p->why, sizeof(p->why), "Die Spieldaten sind gerade nicht erreichbar (Laufwerk nicht angeschlossen?).");
        return 0;
      }
      if(!deletable_root(s.path)) {
        snprintf(p->why, sizeof(p->why), "Die Spieldaten liegen an einem Ort, an dem die App nichts löscht: %.200s", s.path);
        return 0;
      }
      int64_t sz = size_of(s.path);
      if(sz >= 0) p->size = sz;
    } else {
      /* Everything else on the list the system uninstalls itself, as its own menu "Löschen" does: an installed
         package, a title without app.pkg (06.10.2026: Styx, a PS4 title with no file the app recognised), and one
         mounted by a link without ShadowMountPlus. In that last case the system takes the title off and its own
         folder, but the data the link points at stays where it is: the app does not guess which files belong to
         it, and says so. */
      snprintf(p->kind, sizeof(p->kind), "installed");
      if(!strcmp(li.format, "pkg") || !li.format[0]) {
        snprintf(p->path, sizeof(p->path), "%s", li.path);
      } else if(li.path[0]) {
        snprintf(p->keeps, sizeof(p->keeps), "%s", li.path);
      }
    }
    char run[16];
    if(game_running(run, sizeof(run)) && !strcmp(run, id)) {
      snprintf(p->why, sizeof(p->why), "Das Spiel läuft gerade. Bitte erst beenden.");
      return 0;
    }
  } else if(!strcmp(kind, "backup")) {
    backup_t b;
    if(!deletable_root(id) || find_backup(id, &b) != 0) return 404;
    snprintf(p->kind, sizeof(p->kind), "backup");
    snprintf(p->name, sizeof(p->name), "%s", b.name);
    snprintf(p->path, sizeof(p->path), "%s", b.path);
    snprintf(p->format, sizeof(p->format), "%s", b.type);
    p->size = b.size;
  } else {
    return 400;
  }
  if(ps5tm_gamedelete_busy()) snprintf(p->why, sizeof(p->why), "Es wird gerade schon etwas gelöscht.");
  else other_job_active(p->why, sizeof(p->why));
  return 0;
}

cJSON *
ps5tm_gamedelete_plan(const char *kind, const char *id, int *http) {
  plan_t p;
  *http = make_plan(kind, id, &p);
  if(*http != 0) return NULL;
  *http = 200;
  cJSON *o = cJSON_CreateObject();
  cJSON_AddBoolToObject(o, "ok", 1);
  cJSON_AddStringToObject(o, "kind", p.kind);
  cJSON_AddStringToObject(o, "id", id);
  cJSON_AddStringToObject(o, "name", p.name);
  cJSON_AddStringToObject(o, "path", p.path);
  cJSON_AddStringToObject(o, "format", p.format);
  cJSON_AddStringToObject(o, "version", p.version);
  cJSON_AddNumberToObject(o, "size_bytes", (double)p.size);
  cJSON_AddBoolToObject(o, "saves_kept", 1);
  cJSON *items = cJSON_AddArrayToObject(o, "items");
  if(!strcmp(p.kind, "installed")) {
    cJSON_AddItemToArray(items, cJSON_CreateString("das Spiel, deinstalliert über die Konsole (wie im Menü „Löschen“)"));
    cJSON_AddItemToArray(items, cJSON_CreateString("seine Updates und Zusatzinhalte"));
  } else if(!strcmp(p.kind, "smp")) {
    cJSON_AddItemToArray(items, cJSON_CreateString("der Eintrag bei ShadowMountPlus (das Spiel wird ausgehängt und abgemeldet)"));
    char t[400];
    snprintf(t, sizeof(t), "die Spieldaten: %s", p.path);
    cJSON_AddItemToArray(items, cJSON_CreateString(t));
    cJSON_AddItemToArray(items, cJSON_CreateString("eine Prüfsummen-Datei daneben, falls vorhanden"));
  } else if(!strcmp(p.kind, "backup")) {
    char t[400];
    snprintf(t, sizeof(t), "die Sicherung: %s", p.path);
    cJSON_AddItemToArray(items, cJSON_CreateString(t));
    cJSON_AddItemToArray(items, cJSON_CreateString("ihre Prüfsummen-Datei, falls vorhanden"));
  }
  if(p.keeps[0]) cJSON_AddStringToObject(o, "keeps", p.keeps);
  int can = p.why[0] == 0;
  cJSON_AddBoolToObject(o, "can_delete", can);
  if(!can) cJSON_AddStringToObject(o, "why", p.why);
  else {
    pthread_mutex_lock(&g_lock);
    make_token(g_tok.token);
    snprintf(g_tok.kind, sizeof(g_tok.kind), "%s", kind);
    snprintf(g_tok.id, sizeof(g_tok.id), "%s", id);
    g_tok.until_ms = ps5tm_mono_ms() + DL_TOKEN_MS;
    cJSON_AddStringToObject(o, "confirm", g_tok.token);
    pthread_mutex_unlock(&g_lock);
  }
  return o;
}


/* ------------------------------------------------------------------ the job */

/* Gone from the system: no folder in any of its app places, and not on the list any more. */
static int
title_gone(const char *tid) {
  static const char *const roots[] = { DL_ROOT "/user/app", DL_ROOT "/mnt/ext0/user/app", DL_ROOT "/mnt/ext1/user/app" };
  char p[96];
  struct stat st;
  for(size_t i = 0; i < sizeof(roots) / sizeof(roots[0]); i++) {
    snprintf(p, sizeof(p), "%s/%s", roots[i], tid);
    if(lstat(p, &st) == 0) return 0;
  }
  ps5tm_library_forget();
  ps5tm_libdel_t li;
  return ps5tm_library_delete_info(tid, &li) != 0;
}

/* How much of the title is still on the system: its folders for the game, its updates and its add-ons, on the internal
   SSD and the extended storages. -1 when a folder could not be counted (the system may be removing it right now). */
static int64_t
title_bytes(const char *tid) {
  static const char *const roots[] = {
    DL_ROOT "/user/app", DL_ROOT "/user/patch", DL_ROOT "/user/addcont",
    DL_ROOT "/mnt/ext0/user/app", DL_ROOT "/mnt/ext0/user/patch", DL_ROOT "/mnt/ext0/user/addcont",
    DL_ROOT "/mnt/ext1/user/app", DL_ROOT "/mnt/ext1/user/patch", DL_ROOT "/mnt/ext1/user/addcont" };
  int64_t sum = 0;
  char p[96];
  struct stat st;
  for(size_t i = 0; i < sizeof(roots) / sizeof(roots[0]); i++) {
    snprintf(p, sizeof(p), "%s/%s", roots[i], tid);
    if(lstat(p, &st) != 0) continue;
    int64_t s = size_of(p);
    if(s < 0) return -1;
    sum += s;
  }
  return sum;
}

/* While the system uninstalls (the call blocks, the cleanup goes on after it), once a second: how much is gone. */
typedef struct {
  char     id[16];
  uint64_t initial;
  int      stop;
} sampler_t;

static void *
sampler_thread(void *arg) {
  sampler_t *s = arg;
  for(;;) {
    for(int i = 0; i < 10; i++) {
      if(__atomic_load_n(&s->stop, __ATOMIC_ACQUIRE)) return NULL;
      usleep(100 * 1000);
    }
    int64_t left = title_bytes(s->id);
    if(left >= 0 && (uint64_t)left <= s->initial) job_done_bytes(s->initial - (uint64_t)left);
  }
}

static void
run_installed(const dl_args_t *a) {
  job_phase("Die Konsole deinstalliert das Spiel");
  /* the bar: 5 % to start, then as the title's folders shrink up to 99 %; when they cannot be counted, 5 % until the
     system said yes, 60 % while it cleans up */
  int64_t initial = title_bytes(a->id);
  sampler_t smp = { "", initial > 0 ? (uint64_t)initial : 0, 0 };
  snprintf(smp.id, sizeof(smp.id), "%s", a->id);
  job_stage(5, 99, smp.initial);
  pthread_t sth;
  int sampling = smp.initial > 0 && pthread_create(&sth, NULL, sampler_thread, &smp) == 0;
  int pat = 0, add = 0;
  char why[600];
  int rc = ps5tm_pkginst_uninstall(a->id, &pat, &add, why, sizeof(why));
  if(rc == 0 && !sampling) job_stage(60, 60, 0);
  if(rc != 0) {
    if(sampling) { __atomic_store_n(&smp.stop, 1, __ATOMIC_RELEASE); pthread_join(sth, NULL); }
    char e[640];
    snprintf(e, sizeof(e), "Die Konsole hat das Deinstallieren abgelehnt: %s", why[0] ? why : "ohne Angabe.");
    PS5TM_WARN("game_delete_failed", "Löschen von %s: %.150s", a->id, e);
    job_end(DL_FAILED, e, NULL);
    return;
  }
  job_phase("Die Konsole räumt auf");
  uint64_t t0 = ps5tm_mono_ms();
  int gone = 0;
  while(!(gone = title_gone(a->id)) && ps5tm_mono_ms() - t0 < (uint64_t)DL_WAIT_GONE_S * 1000)
    usleep(2000 * 1000);
  if(sampling) { __atomic_store_n(&smp.stop, 1, __ATOMIC_RELEASE); pthread_join(sth, NULL); }
  char note[400] = "";
  if(!gone) snprintf(note, sizeof(note), "Die Konsole hat das Deinstallieren angenommen, ist aber noch nicht fertig. Bitte gleich in der Spieleliste nachsehen.");
  ps5tm_smp_forget();
  ps5tm_library_forget();
  PS5TM_INFO("game_delete_done", "%s über die Konsole deinstalliert%s (Updates 0x%08X, Zusatzinhalte 0x%08X).",
             a->id, gone ? "" : ", Aufräumen noch nicht fertig", (unsigned)pat, (unsigned)add);
  job_end(DL_DONE, NULL, note);
}

static void
run_smp(const dl_args_t *a) {
  ps5tm_smp_game_t s;
  if(ps5tm_smp_find(a->id, &s) != 0 || !deletable_root(s.path)) {
    job_end(DL_FAILED, "ShadowMountPlus kennt das Spiel nicht mehr oder die Spieldaten liegen an einem unerlaubten Ort. Es wurde nichts gelöscht.", NULL);
    return;
  }
  job_phase("ShadowMountPlus meldet das Spiel ab");
  job_stage(3, 3, 0);
  cJSON *body = cJSON_CreateObject();
  cJSON_AddStringToObject(body, "title_id", a->id);
  char err[300] = "";
  cJSON *r = ps5tm_smp_call("/games/uninstall", body, 30000, err, sizeof(err));
  cJSON_Delete(body);
  if(!r) {
    char e[640];
    snprintf(e, sizeof(e), "ShadowMountPlus hat das Abmelden abgelehnt (%s). Es wurde nichts gelöscht.", err[0] ? err : "ohne Angabe");
    PS5TM_WARN("game_delete_failed", "Löschen von %s: %.150s", a->id, e);
    job_end(DL_FAILED, e, NULL);
    return;
  }
  cJSON_Delete(r);
  ps5tm_smp_forget();
  ps5tm_library_forget();
  job_phase("Die Spieldaten werden gelöscht");
  uint64_t files = 0, bytes = 0;
  char why[640] = "";
  int rc = delete_path(s.path, 10, 99, &files, &bytes, why, sizeof(why));
  pthread_mutex_lock(&g_lock);
  g_job.files = files;
  g_job.bytes = bytes;
  pthread_mutex_unlock(&g_lock);
  if(rc != 0) {
    char e[640];
    snprintf(e, sizeof(e), "ShadowMountPlus hat das Spiel abgemeldet, aber die Spieldaten ließen sich nicht ganz löschen. %s", why);
    PS5TM_WARN("game_delete_failed", "Löschen von %s: %.150s", a->id, e);
    job_end(DL_FAILED, e, NULL);
    return;
  }
  PS5TM_INFO("game_delete_done", "%s abgemeldet und gelöscht: %s (%llu Dateien, %llu MB).", a->id, s.path,
             (unsigned long long)files, (unsigned long long)(bytes >> 20));
  job_end(DL_DONE, NULL, NULL);
}

static void
run_backup(const dl_args_t *a) {
  backup_t b;
  if(find_backup(a->id, &b) != 0) {
    job_end(DL_FAILED, "Die Sicherung ist nicht mehr da. Es wurde nichts gelöscht.", NULL);
    return;
  }
  job_phase("Die Sicherung wird gelöscht");
  uint64_t files = 0, bytes = 0;
  char why[640] = "";
  int rc = delete_path(b.path, 2, 99, &files, &bytes, why, sizeof(why));
  pthread_mutex_lock(&g_lock);
  g_job.files = files;
  g_job.bytes = bytes;
  pthread_mutex_unlock(&g_lock);
  if(rc != 0) {
    PS5TM_WARN("game_delete_failed", "Löschen der Sicherung %s: %.150s", b.path, why);
    job_end(DL_FAILED, why, NULL);
    return;
  }
  PS5TM_INFO("game_delete_done", "Sicherung gelöscht: %s (%llu Dateien, %llu MB).", b.path,
             (unsigned long long)files, (unsigned long long)(bytes >> 20));
  job_end(DL_DONE, NULL, NULL);
}

static void *
delete_thread(void *arg) {
  dl_args_t *a = arg;
  ps5tm_powerguard_hold();
  pthread_mutex_lock(&g_lock);
  char kind[12];
  snprintf(kind, sizeof(kind), "%s", g_job.kind);
  pthread_mutex_unlock(&g_lock);
  if(!strcmp(kind, "installed")) run_installed(a);
  else if(!strcmp(kind, "smp")) run_smp(a);
  else run_backup(a);
  ps5tm_powerguard_release();
  free(a);
  return NULL;
}

int
ps5tm_gamedelete_start(const char *kind, const char *id, const char *token, char *err, size_t err_len) {
  err[0] = 0;
  if(!kind || !id || !token) { snprintf(err, err_len, "Es fehlt, was gelöscht werden soll, oder die Bestätigung."); return 400; }
  /* the token first: without the confirmation of exactly this plan, nothing is even looked at */
  pthread_mutex_lock(&g_lock);
  int tok_ok = g_tok.token[0] && strlen(token) == 32 && !strcmp(token, g_tok.token) && !strcmp(kind, g_tok.kind) &&
               !strcmp(id, g_tok.id) && ps5tm_mono_ms() < g_tok.until_ms;
  if(tok_ok) memset(&g_tok, 0, sizeof(g_tok));                     /* one use */
  pthread_mutex_unlock(&g_lock);
  if(!tok_ok) { snprintf(err, err_len, "Die Bestätigung fehlt oder ist abgelaufen. Bitte das Löschen noch einmal öffnen und bestätigen."); return 403; }

  plan_t p;
  int http = make_plan(kind, id, &p);
  if(http != 0) { snprintf(err, err_len, http == 404 ? "Das gibt es nicht (mehr)." : "Ungültige Angabe."); return http; }
  if(p.why[0]) { snprintf(err, err_len, "%s", p.why); return 409; }

  dl_args_t *a = calloc(1, sizeof(*a));
  if(!a) { snprintf(err, err_len, "Kein Speicher."); return 500; }
  snprintf(a->kind, sizeof(a->kind), "%s", p.kind);
  snprintf(a->id, sizeof(a->id), "%s", id);

  pthread_mutex_lock(&g_lock);
  if(g_job.state == DL_RUNNING) {
    pthread_mutex_unlock(&g_lock);
    free(a);
    snprintf(err, err_len, "Es wird gerade schon etwas gelöscht.");
    return 409;
  }
  memset(&g_job, 0, sizeof(g_job));
  g_job.state = DL_RUNNING;
  snprintf(g_job.kind, sizeof(g_job.kind), "%s", p.kind);
  snprintf(g_job.id, sizeof(g_job.id), "%s", id);
  snprintf(g_job.name, sizeof(g_job.name), "%s", p.name);
  snprintf(g_job.phase, sizeof(g_job.phase), "Wird vorbereitet");
  g_job.started_ms = ps5tm_mono_ms();
  pthread_mutex_unlock(&g_lock);

  /* the others may have started in the same moment: they look at this job as well, so one of the two stands down */
  if(other_job_active(err, err_len)) {
    pthread_mutex_lock(&g_lock);
    g_job.state = DL_IDLE;
    pthread_mutex_unlock(&g_lock);
    free(a);
    return 409;
  }

  PS5TM_INFO("game_delete_start", "Löschen beginnt: %.40s „%.80s“ (%s).", id, p.name, p.kind);
  pthread_t th;
  pthread_attr_t at;
  pthread_attr_init(&at);
  pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
  int rc = pthread_create(&th, &at, delete_thread, a);
  pthread_attr_destroy(&at);
  if(rc != 0) {
    free(a);
    job_end(DL_FAILED, "Das Löschen ließ sich nicht starten.", NULL);
    snprintf(err, err_len, "Das Löschen ließ sich nicht starten.");
    return 500;
  }
  return 200;
}

int
ps5tm_gamedelete_busy(void) {
  pthread_mutex_lock(&g_lock);
  int b = g_job.state == DL_RUNNING;
  pthread_mutex_unlock(&g_lock);
  return b;
}

cJSON *
ps5tm_gamedelete_status(void) {
  pthread_mutex_lock(&g_lock);
  dl_job_t j = g_job;
  pthread_mutex_unlock(&g_lock);
  uint64_t now = ps5tm_mono_ms();
  cJSON *o = cJSON_CreateObject();
  cJSON_AddBoolToObject(o, "ok", 1);
  cJSON_AddStringToObject(o, "state", k_state[j.state]);
  cJSON_AddBoolToObject(o, "active", j.state == DL_RUNNING);
  cJSON_AddStringToObject(o, "kind", j.kind);
  cJSON_AddStringToObject(o, "id", j.id);
  cJSON_AddStringToObject(o, "name", j.name);
  cJSON_AddStringToObject(o, "phase", j.phase);
  cJSON_AddStringToObject(o, "error", j.error);
  cJSON_AddStringToObject(o, "note", j.note);
  cJSON_AddNumberToObject(o, "files", (double)j.files);
  cJSON_AddNumberToObject(o, "bytes", (double)j.bytes);
  double pct = 0;
  if(j.state == DL_DONE) pct = 100;
  else if(j.state != DL_IDLE) {
    uint64_t d = j.done < j.total ? j.done : j.total;
    pct = j.pct_lo + (j.total ? (double)(j.pct_hi - j.pct_lo) * (double)d / (double)j.total : 0);
  }
  cJSON_AddNumberToObject(o, "percent", (double)(int)pct);
  cJSON_AddNumberToObject(o, "bytes_total", (double)j.total);
  cJSON_AddNumberToObject(o, "bytes_done", (double)(j.done < j.total ? j.done : j.total));
  cJSON_AddNumberToObject(o, "elapsed_s", j.started_ms ? (double)(((j.finished_ms ? j.finished_ms : now) - j.started_ms) / 1000) : 0);
  cJSON_AddNumberToObject(o, "finished_ago_s", j.finished_ms ? (double)((now - j.finished_ms) / 1000) : -1);
  return o;
}
