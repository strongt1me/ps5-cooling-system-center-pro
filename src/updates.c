/* Game updates (19.10.2026, simplified 21.10.2026): the newest official update of every PS5 and PS4 game on the
 * console, from Sony's servers, downloaded and installed by the app. After PatchDL 0.3.0 by Knutwurst
 * (GPL-3.0-or-later); its rules are kept, the code around them is the app's own (PatchDL runs as a payload with its
 * own web server, libcurl and OpenSSL).
 *
 * What PatchDL does, and so does this:
 *   - only games installed from disc or the PlayStation Store: their app.pkg is signed by Sony ("\x7FFIH" byte 5 bit 0x80,
 *     "\x7FCNT" byte 4 bit 1); ShadowMountPlus games, fake packages and homebrew are listed, never updated
 *   - the update list is the game's own version.xml (VERSION_FILE_URI in the app database for PS5, the HMAC path for PS4);
 *     only versions the firmware runs are offered. Direct from Sony only: when Sony's newest needs a newer firmware,
 *     the title just says so (no update that runs here) — no link of any kind is pasted in or fetched
 *   - the package is assembled from the pieces of Sony's JSON manifest, each piece checked against its SHA-256 (SHA-1 for
 *     PS4) when the check is on; up to 16 connections; progress kept after every finished part, so a pause, a crash or a
 *     power cut costs only the parts under way; a package already in the folder is checked piece by piece and only the
 *     pieces that do not match are fetched again
 *   - "update all" queues every game with an update; each one is installed as soon as it is complete (by the app's own
 *     installer, pkginstall.c, the console's installer reads it from the app); after the install the game's version in the
 *     app database is compared with the update's, and only then is the package deleted (switchable)
 *   - the network: Sony's hosts only (one root certificate), the app's own resolver (upd_dns.c), never the console's DNS
 *
 * The switch is off until the person turns it on; no request leaves the console before that, and none without a click
 * ("Nach Updates suchen", a download). Folder: PS5TM_DATA_DIR/updates (settings.json, <TITLE ID>/). */

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <time.h>
#include <unistd.h>

#include "pkginst.h"
#include "ps5tm.h"
#include "third_party/cJSON.h"
#include "upd_http.h"
#include "upd_sony.h"

#define UPD_DIR          PS5TM_DATA_DIR "/updates"
#define UPD_SETTINGS     UPD_DIR "/settings.json"
#define UPD_MAX_TITLES   256
#ifndef UPD_SEG
#define UPD_SEG          (64LL << 20)        /* a download part: pieces are split into these for parallel connections */
#endif
#ifndef UPD_APP_ROOT
#define UPD_APP_ROOT     "/user/app"         /* where an installed game's app.pkg lies */
#endif
#define UPD_MAX_CONNS    16
#define UPD_INSTALL_MAX_MS (6ull * 3600 * 1000)
#define UPD_RESERVE      (1LL << 30)         /* left free on the drive */
#define UPD_PKG_NAME     "update.pkg"
#define UPD_JOB_NAME     "job.json"
#define UPD_SEG_TRIES    6

enum { SIGN_NONE = 0, SIGN_RETAIL, SIGN_FAKE, SIGN_UNREAD };
enum { SRC_OFFICIAL = 0, SRC_SMP, SRC_FAKE, SRC_UNKNOWN };
enum { ST_IDLE = 0, ST_QUEUED, ST_DOWNLOADING, ST_PAUSED, ST_VERIFYING, ST_READY, ST_INSTALLING, ST_INSTALLED, ST_FAILED };

static const char *const k_state[] = { "idle", "queued", "downloading", "paused", "verifying", "ready", "installing", "installed", "failed" };

typedef struct {
  char     tid[12], name[128], cid[40], ver[16], ver_uri[256];
  int      plat;                     /* 5 or 4 */
  int      sign, source;
  /* the check */
  int      checked;                  /* 0 never, 1 done, -1 failed */
  char     check_why[64];
  char     compat_ver[16], latest_ver[16], latest_fw[16], patch_url[512];
  /* the download */
  int      state;
  char     err[240];
  char     job_ver[16], job_url[512];
  int64_t  total, have;              /* bytes of the package, bytes on disk */
  int      in_queue;
  unsigned qorder;
  int      deleting;                 /* its folder is being deleted: nothing may start meanwhile */
} title_t;

typedef struct {
  int enabled, conns, delete_after;
} settings_t;

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static title_t        *g_t;                 /* UPD_MAX_TITLES */
static int             g_n;
static uint64_t        g_titles_ms;
static settings_t      g_set = { 0, 8, 1 };
static int             g_set_loaded;
static int             g_checking, g_check_done, g_check_total;
static int             g_worker_up;
static unsigned        g_qseq;
static uint32_t        g_fw;
/* the job that runs (one at a time) */
static char            g_job_tid[12];
static volatile int    g_abort;              /* pause or cancel of the running job */
static int             g_cancel_req;         /* the abort is a cancel: the files go */
static volatile int64_t g_slot[UPD_MAX_CONNS];
static int64_t         g_job_base;           /* bytes done before the parts under way */
static uint64_t        g_job_t0, g_rate_ms;
static int64_t         g_rate_bytes;
static double          g_rate;               /* bytes/s, smoothed */
static char            g_phase[96];

/* ---------------------------------------------------------------- small helpers */

static int
tid_ok(const char *t) {
  if(!t || strlen(t) != 9 || (strncmp(t, "PPSA", 4) && strncmp(t, "CUSA", 4))) return 0;
  for(int i = 4; i < 9; i++)
    if(!isdigit((unsigned char)t[i])) return 0;
  return 1;
}

static void
title_dir(const char *tid, char *out, size_t n) { snprintf(out, n, UPD_DIR "/%s", tid); }

static void
title_file(const char *tid, const char *name, char *out, size_t n) { snprintf(out, n, UPD_DIR "/%s/%s", tid, name); }

static int
write_atomic(const char *path, const char *data, size_t n) {
  char tmp[PATH_MAX];
  snprintf(tmp, sizeof tmp, "%s.tmp", path);
  int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0666);
  if(fd < 0) return -1;
  size_t off = 0;
  while(off < n) {
    ssize_t w = write(fd, data + off, n - off);
    if(w <= 0) { if(w < 0 && errno == EINTR) continue; close(fd); unlink(tmp); return -1; }
    off += (size_t)w;
  }
  if(fsync(fd) != 0 || close(fd) != 0) { unlink(tmp); return -1; }
  if(rename(tmp, path) != 0) { unlink(tmp); return -1; }
  return 0;
}

static pthread_mutex_t g_save_mx = PTHREAD_MUTEX_INITIALIZER;   /* job.json: one writer at a time */

static char *
read_small(const char *path, size_t max) {
  int fd = open(path, O_RDONLY | O_NOFOLLOW);
  if(fd < 0) return NULL;
  struct stat st;
  if(fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || (size_t)st.st_size > max) { close(fd); return NULL; }
  char *b = malloc((size_t)st.st_size + 1);
  if(!b) { close(fd); return NULL; }
  ssize_t r = read(fd, b, (size_t)st.st_size);
  close(fd);
  if(r != st.st_size) { free(b); return NULL; }
  b[r] = 0;
  return b;
}

static void
rm_dir_files(const char *dir) {
  DIR *d = opendir(dir);
  if(!d) return;
  struct dirent *e;
  char p[PATH_MAX];
  while((e = readdir(d))) {
    if(!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
    snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
    unlink(p);
  }
  closedir(d);
  rmdir(dir);
}

static int64_t
free_bytes(const char *path) {
  struct statvfs sv;
  if(statvfs(path, &sv) != 0) return -1;
  return (int64_t)sv.f_bavail * (int64_t)(sv.f_frsize ? sv.f_frsize : sv.f_bsize);
}

static title_t *
find_locked(const char *tid) {
  for(int i = 0; i < g_n; i++)
    if(!strcmp(g_t[i].tid, tid)) return &g_t[i];
  return NULL;
}

/* version strings compare as zero-padded text: "01.003.000" > "01.002.010" */
static int ver_gt(const char *a, const char *b) { return a[0] && strcmp(a, b ? b : "") > 0; }

/* ---------------------------------------------------------------- settings and links */

static void
settings_load_locked(void) {
  if(g_set_loaded) return;
  g_set_loaded = 1;
  char *b = read_small(UPD_SETTINGS, 4096);
  cJSON *j = b ? cJSON_Parse(b) : NULL;
  free(b);
  if(!j) return;
  const cJSON *v;
  if(cJSON_IsBool(v = cJSON_GetObjectItem(j, "enabled"))) g_set.enabled = cJSON_IsTrue(v);
  if(cJSON_IsNumber(v = cJSON_GetObjectItem(j, "connections")) && v->valuedouble >= 1 && v->valuedouble <= UPD_MAX_CONNS) g_set.conns = (int)v->valuedouble;
  if(cJSON_IsBool(v = cJSON_GetObjectItem(j, "delete_after"))) g_set.delete_after = cJSON_IsTrue(v);
  cJSON_Delete(j);
}

static void
settings_save_locked(void) {
  char b[256];
  int n = snprintf(b, sizeof b, "{\"enabled\":%s,\"connections\":%d,\"delete_after\":%s}\n", g_set.enabled ? "true" : "false",
                   g_set.conns, g_set.delete_after ? "true" : "false");
  mkdir(PS5TM_DATA_DIR, 0777);
  mkdir(UPD_DIR, 0777);
  if(write_atomic(UPD_SETTINGS, b, (size_t)n) != 0) PS5TM_WARN("upd_settings", "Spiel-Updates: Die Einstellungen ließen sich nicht speichern.");
}

/* ---------------------------------------------------------------- the games */

/* Sony-signed or not, from the first bytes of the installed app.pkg (PatchDL's test, the same Itemzflow uses). */
static int
file_sign(const char *p) {
  int fd = open(p, O_RDONLY);
  if(fd < 0) return errno == ENOENT ? SIGN_NONE : SIGN_UNREAD;
  unsigned char h[8];
  ssize_t r = read(fd, h, sizeof h);
  close(fd);
  if(r != (ssize_t)sizeof h) return SIGN_UNREAD;
  if(!memcmp(h, "\x7f" "FIH", 4)) return (h[5] & 0x80) ? SIGN_RETAIL : SIGN_FAKE;
  if(!memcmp(h, "\x7f" "CNT", 4)) return (h[4] & 0x01) ? SIGN_RETAIL : SIGN_FAKE;
  return SIGN_UNREAD;
}

static int
pkg_sign(const char *tid) {
  char p[96];
  snprintf(p, sizeof p, UPD_APP_ROOT "/%s/app.pkg", tid);
  return file_sign(p);
}

/* The state a title's folder says it is in, read when the list is built: a finished package, a part of one. */
static void
disk_state_locked(title_t *t) {
  char jp[PATH_MAX], pp[PATH_MAX];
  title_file(t->tid, UPD_JOB_NAME, jp, sizeof jp);
  title_file(t->tid, UPD_PKG_NAME, pp, sizeof pp);
  if(!strcmp(g_job_tid, t->tid)) return;                     /* the running job keeps its own state */
  if(t->state == ST_INSTALLING || t->state == ST_QUEUED) return;
  char *b = read_small(jp, 1 << 20);
  cJSON *j = b ? cJSON_Parse(b) : NULL;
  free(b);
  struct stat st;
  int has_pkg = stat(pp, &st) == 0 && S_ISREG(st.st_mode);
  if(!j) {
    if(t->state != ST_INSTALLED && t->state != ST_FAILED) t->state = ST_IDLE;
    t->total = t->have = 0;
    return;
  }
  const cJSON *v;
  t->total = cJSON_IsNumber(v = cJSON_GetObjectItem(j, "total")) ? (int64_t)v->valuedouble : 0;
  if(cJSON_IsString(v = cJSON_GetObjectItem(j, "version"))) snprintf(t->job_ver, sizeof t->job_ver, "%s", v->valuestring);
  if(cJSON_IsString(v = cJSON_GetObjectItem(j, "url"))) snprintf(t->job_url, sizeof t->job_url, "%s", v->valuestring);
  int complete = cJSON_IsTrue(cJSON_GetObjectItem(j, "complete"));
  t->have = cJSON_IsNumber(v = cJSON_GetObjectItem(j, "have")) ? (int64_t)v->valuedouble : 0;
  cJSON_Delete(j);
  if(complete && has_pkg && st.st_size == t->total) { if(t->state != ST_INSTALLED) t->state = ST_READY; t->have = t->total; }
  else if(has_pkg) { if(t->state != ST_FAILED) t->state = ST_PAUSED; }
  else t->state = ST_IDLE;
}

/* Builds the list again from the app database, keeping what was learned about each title. */
static void
titles_refresh(int force) {
  pthread_mutex_lock(&g_lock);
  uint64_t now = ps5tm_mono_ms();
  if(!force && g_t && g_titles_ms && now - g_titles_ms < 20000) { pthread_mutex_unlock(&g_lock); return; }
  g_titles_ms = now;
  pthread_mutex_unlock(&g_lock);

  ps5tm_libupd_t *rows = calloc(UPD_MAX_TITLES, sizeof *rows);
  title_t *nt = calloc(UPD_MAX_TITLES, sizeof *nt);
  if(!rows || !nt) { free(rows); free(nt); return; }
  int n = ps5tm_library_update_titles(rows, UPD_MAX_TITLES);
  if(n < 0) { free(rows); free(nt); return; }
  int k = 0;
  for(int i = 0; i < n && k < UPD_MAX_TITLES; i++) {
    if(ps5tm_library_is_game(rows[i].title_id) == 0) continue;   /* apps such as YouTube and homebrew tools are no games to update */
    title_t *t = &nt[k++];
    snprintf(t->tid, sizeof t->tid, "%s", rows[i].title_id);
    snprintf(t->name, sizeof t->name, "%s", rows[i].name);
    snprintf(t->cid, sizeof t->cid, "%s", rows[i].content_id);
    snprintf(t->ver, sizeof t->ver, "%s", rows[i].version);
    snprintf(t->ver_uri, sizeof t->ver_uri, "%s", rows[i].ver_uri);
    t->plat = !strncmp(t->tid, "CUSA", 4) ? 4 : 5;
    t->sign = pkg_sign(t->tid);
    t->source = t->sign == SIGN_RETAIL ? SRC_OFFICIAL : t->sign == SIGN_FAKE ? SRC_FAKE : t->sign == SIGN_NONE ? SRC_SMP : SRC_UNKNOWN;
    if(t->source == SRC_SMP || t->source == SRC_OFFICIAL) {   /* the game's own param.json first (no unpacking), app.db is the fallback */
      ps5tm_libdel_t li;
      char real_ver[16], real_uri[256];
      if(ps5tm_library_delete_info(t->tid, &li) == 0 && li.path[0] &&
         ps5tm_library_param_info(li.path, li.format, real_ver, sizeof real_ver, real_uri, sizeof real_uri) == 0) {
        if(real_uri[0]) snprintf(t->ver_uri, sizeof t->ver_uri, "%s", real_uri);
        /* never lower than app.db: an update installed through the console is in app.db, not in the image's base param.json */
        if(t->source == SRC_SMP && real_ver[0] && ver_gt(real_ver, t->ver)) snprintf(t->ver, sizeof t->ver, "%s", real_ver);
      }
    }
  }
  free(rows);
  pthread_mutex_lock(&g_lock);
  for(int i = 0; i < k; i++) {                             /* carry over what the old list knew */
    title_t *o = find_locked(nt[i].tid);
    if(!o) continue;
    title_t keep = nt[i];
    nt[i] = *o;
    snprintf(nt[i].name, sizeof nt[i].name, "%s", keep.name);
    snprintf(nt[i].cid, sizeof nt[i].cid, "%s", keep.cid);
    snprintf(nt[i].ver_uri, sizeof nt[i].ver_uri, "%s", keep.ver_uri);
    nt[i].sign = keep.sign;
    nt[i].source = keep.source;
    if(strcmp(nt[i].ver, keep.ver)) nt[i].checked = 0;   /* the game changed: check it again */
    snprintf(nt[i].ver, sizeof nt[i].ver, "%s", keep.ver);
  }
  free(g_t);
  g_t = nt;
  g_n = k;
  for(int i = 0; i < g_n; i++) disk_state_locked(&g_t[i]);
  pthread_mutex_unlock(&g_lock);
}

/* The version and URL this title would download now, straight from Sony: "" when there is none newer than the
   installed version that runs on this firmware. */
static void
target_locked(const title_t *t, char *ver, size_t vn, char *url, size_t un) {
  ver[0] = url[0] = 0;
  if(t->checked == 1 && t->patch_url[0] && ver_gt(t->compat_ver, t->ver)) {
    snprintf(ver, vn, "%s", t->compat_ver);
    snprintf(url, un, "%s", t->patch_url);
  }
}

static const char *
status_locked(const title_t *t) {
  /* SMP games can receive updates the same way as official ones (the PS5 update mechanism only checks the title ID,
     not how the base game was installed). Only fake PKGs and unknown sources are excluded. */
  if(t->source == SRC_FAKE) return "not_official_fake";
  if(t->source == SRC_UNKNOWN) return "not_official_unknown";
  if(t->state != ST_IDLE) return k_state[t->state];
  char v[16], u[512];
  target_locked(t, v, sizeof v, u, sizeof u);
  if(v[0]) return "update";
  if(t->checked == 0) return "unchecked";
  if(t->checked < 0) return "check_failed";
  if(ver_gt(t->latest_ver, t->ver)) return "needs_newer_fw";
  return "current";
}

/* ---------------------------------------------------------------- checking for updates */

static void *
check_thread(void *arg) {
  (void)arg;
  titles_refresh(1);
  pthread_mutex_lock(&g_lock);
  int n = g_n;
  char (*ids)[12] = calloc((size_t)(n ? n : 1), 12);
  int k = 0;
  for(int i = 0; ids && i < n; i++)
    if(g_t[i].source == SRC_OFFICIAL || g_t[i].source == SRC_SMP) snprintf(ids[k++], 12, "%s", g_t[i].tid);
  g_check_total = k;
  g_check_done = 0;
  uint32_t fw = g_fw;
  pthread_mutex_unlock(&g_lock);

  for(int i = 0; i < k; i++) {
    char url[512] = "", tid[12];
    snprintf(tid, sizeof tid, "%s", ids[i]);
    pthread_mutex_lock(&g_lock);
    title_t *t = find_locked(tid);
    if(t) snprintf(url, sizeof url, "%s", t->ver_uri);
    int enabled = g_set.enabled;
    pthread_mutex_unlock(&g_lock);
    if(!enabled) break;
    if(!strncmp(tid, "CUSA", 4)) upd_sony_ps4_url(tid, url, sizeof url);
    upd_verinfo_t vi;
    char why[64] = "";
    int rc = url[0] ? upd_sony_verxml(url, fw, &vi, why, sizeof why) : (snprintf(why, sizeof why, "no_update_list"), -1);
    if(rc != 0 && !strncmp(why, "http_status 404", 15)) { memset(&vi, 0, sizeof vi); rc = 0; why[0] = 0; }   /* no update was ever published */
    pthread_mutex_lock(&g_lock);
    if((t = find_locked(tid))) {
      t->checked = rc == 0 ? 1 : -1;
      snprintf(t->check_why, sizeof t->check_why, "%s", why);
      if(rc == 0) {
        snprintf(t->compat_ver, sizeof t->compat_ver, "%s", vi.compatible_version);
        snprintf(t->latest_ver, sizeof t->latest_ver, "%s", vi.latest_version);
        snprintf(t->latest_fw, sizeof t->latest_fw, "%s", vi.latest_required_fw);
        snprintf(t->patch_url, sizeof t->patch_url, "%s", vi.compatible_url);
      }
    }
    g_check_done = i + 1;
    pthread_mutex_unlock(&g_lock);
  }
  free(ids);
  pthread_mutex_lock(&g_lock);
  g_checking = 0;
  pthread_mutex_unlock(&g_lock);
  PS5TM_INFO("upd_check", "Spiel-Updates: %d Spiele geprüft.", k);
  return NULL;
}

/* ---------------------------------------------------------------- the download */

typedef struct {
  int      piece;
  int64_t  off;           /* in the package */
  int64_t  len;
  int      done, busy, tries;
} seg_t;

typedef struct {
  upd_manifest_t mf;
  seg_t        *seg;
  int           nseg;
  unsigned char *piece_ok;        /* verified (or no hash to check) */
  int          *piece_busy;       /* being hashed */
  int           fd;
  char          tid[12];
  int           failed;           /* a part gave up: the job stops */
  char          why[200];
  pthread_mutex_t mx;
} job_t;

static void
job_save(job_t *j, const char *ver, const char *url, int complete) {
  cJSON *o = cJSON_CreateObject();
  cJSON_AddStringToObject(o, "url", url);
  cJSON_AddStringToObject(o, "version", ver);
  cJSON_AddNumberToObject(o, "total", (double)j->mf.total);
  cJSON_AddNumberToObject(o, "pieces", j->mf.count);
  cJSON_AddNumberToObject(o, "seg", (double)UPD_SEG);
  int64_t have = 0;
  char *bm = calloc((size_t)j->nseg + 1, 1);
  pthread_mutex_lock(&j->mx);
  for(int i = 0; i < j->nseg; i++) {
    if(bm) bm[i] = j->seg[i].done ? '1' : '0';
    if(j->seg[i].done) have += j->seg[i].len;
  }
  char *pv = calloc((size_t)j->mf.count + 1, 1);
  for(int i = 0; pv && i < j->mf.count; i++) pv[i] = j->piece_ok[i] ? '1' : '0';
  pthread_mutex_unlock(&j->mx);
  cJSON_AddStringToObject(o, "done", bm ? bm : "");
  cJSON_AddStringToObject(o, "verified", pv ? pv : "");
  cJSON_AddNumberToObject(o, "have", (double)have);
  cJSON_AddBoolToObject(o, "complete", complete);
  free(bm);
  free(pv);
  char *s = cJSON_PrintUnformatted(o);
  cJSON_Delete(o);
  if(!s) return;
  char p[PATH_MAX];
  title_file(j->tid, UPD_JOB_NAME, p, sizeof p);
  pthread_mutex_lock(&g_save_mx);
  if(write_atomic(p, s, strlen(s)) != 0) PS5TM_WARN("upd_job_save", "Spiel-Updates: %s, job.json ließ sich nicht schreiben: %s", j->tid, strerror(errno));
  pthread_mutex_unlock(&g_save_mx);
  free(s);
}

static int
piece_complete_locked(job_t *j, int piece) {
  for(int i = 0; i < j->nseg; i++)
    if(j->seg[i].piece == piece && !j->seg[i].done) return 0;
  return 1;
}

/* Checks a piece whose parts are all on disk. 0 good (or nothing to check), -1 it does not match: its parts go again. */
static int
verify_piece(job_t *j, int p) {
  const upd_piece_t *pc = &j->mf.pieces[p];
  char hex[80];
  pthread_mutex_lock(&g_lock);
  snprintf(g_phase, sizeof g_phase, "Stück %d von %d wird geprüft", p + 1, j->mf.count);
  pthread_mutex_unlock(&g_lock);
  int rc = upd_hash_fd(j->fd, pc->offset, pc->size, pc->hash, hex, &g_abort);
  if(rc == -2) return 1;                            /* paused meanwhile: neither good nor bad */
  if(rc != 0) return -1;
  return strcasecmp(hex, pc->hash) ? -1 : 0;
}

/* The state of one worker (slot is its counter of bytes under way). */
typedef struct { job_t *j; int slot; } wk_t;

static void *
worker_main(void *arg) {
  wk_t *w = arg;
  job_t *j = w->j;
  for(;;) {
    if(g_abort) break;
    pthread_mutex_lock(&j->mx);
    if(j->failed) { pthread_mutex_unlock(&j->mx); break; }
    int pick = -1;
    for(int i = 0; i < j->nseg; i++)
      if(!j->seg[i].done && !j->seg[i].busy) { pick = i; break; }
    if(pick < 0) { pthread_mutex_unlock(&j->mx); break; }
    seg_t *s = &j->seg[pick];
    s->busy = 1;
    const upd_piece_t *pc = &j->mf.pieces[s->piece];
    char url[800];
    snprintf(url, sizeof url, "%s", pc->url);
    int64_t from = s->off - pc->offset, len = s->len, off = s->off;
    pthread_mutex_unlock(&j->mx);

    char why[64];
    g_slot[w->slot] = 0;
    int rc = upd_sony_fetch_range(url, j->fd, off, from, len, &g_slot[w->slot], &g_abort, why, sizeof why);
    pthread_mutex_lock(&j->mx);
    s->busy = 0;
    if(rc == 0) {
      s->done = 1;
      __atomic_add_fetch(&g_job_base, len, __ATOMIC_RELAXED);
      g_slot[w->slot] = 0;
      int whole = piece_complete_locked(j, s->piece) && !j->piece_busy[s->piece];
      if(whole) j->piece_busy[s->piece] = 1;
      int p = s->piece;
      pthread_mutex_unlock(&j->mx);
      if(whole) {
        int v = verify_piece(j, p);
        pthread_mutex_lock(&j->mx);
        j->piece_busy[p] = 0;
        if(v == 0) j->piece_ok[p] = 1;
        else if(v < 0) {                                  /* the piece is wrong: fetch it again, a few times at most */
          int tries = 0;
          for(int i = 0; i < j->nseg; i++)
            if(j->seg[i].piece == p) {
              j->seg[i].done = 0;
              if(++j->seg[i].tries > tries) tries = j->seg[i].tries;
              __atomic_sub_fetch(&g_job_base, j->seg[i].len, __ATOMIC_RELAXED);
            }
          if(tries >= 3) {
            j->failed = 1;
            snprintf(j->why, sizeof j->why, "Stück %d stimmt nach drei Versuchen nicht mit der offiziellen Prüfsumme überein.", p + 1);
          }
          PS5TM_WARN("upd_hash", "Spiel-Updates: %s, Stück %d passt nicht zur Prüfsumme, es wird neu geladen.", j->tid, p + 1);
        }
        pthread_mutex_unlock(&j->mx);
        pthread_mutex_lock(&g_lock);
        g_phase[0] = 0;
        pthread_mutex_unlock(&g_lock);
      }
      pthread_mutex_lock(&g_lock);
      char ver[16] = "", u[512] = "";
      const title_t *jt = find_locked(j->tid);
      if(jt) { snprintf(ver, sizeof ver, "%s", jt->job_ver); snprintf(u, sizeof u, "%s", jt->job_url); }
      pthread_mutex_unlock(&g_lock);
      job_save(j, ver, u, 0);
      continue;
    }
    g_slot[w->slot] = 0;
    if(g_abort) { pthread_mutex_unlock(&j->mx); break; }
    s->tries++;
    if(s->tries >= UPD_SEG_TRIES) {
      j->failed = 1;
      snprintf(j->why, sizeof j->why, "Ein Teil des Downloads scheiterte %d-mal (%s).", s->tries, why);
      pthread_mutex_unlock(&j->mx);
      break;
    }
    int t = s->tries;
    pthread_mutex_unlock(&j->mx);
    PS5TM_INFO("upd_retry", "Spiel-Updates: %s, ein Teil wird wiederholt (%s, Versuch %d).", j->tid, why, t + 1);
    for(int ms = 0; ms < 2000 * t && !g_abort; ms += 200) usleep(200 * 1000);   /* back off: 2, 4, 6 ... s */
  }
  return NULL;
}

/* Downloads (or resumes) the update of tid. 0 complete and checked, 1 paused/cancelled, -1 failed (why). */
static int
download(const char *tid, const char *ver, const char *url, int conns, char *why, size_t wn) {
  job_t *j = calloc(1, sizeof *j);
  if(!j) { snprintf(why, wn, "Kein Speicher."); return -1; }
  pthread_mutex_init(&j->mx, NULL);
  snprintf(j->tid, sizeof j->tid, "%s", tid);
  j->fd = -1;
  int rc = -1;
  char mwhy[64];
  pthread_mutex_lock(&g_lock);
  snprintf(g_phase, sizeof g_phase, "Die Paketliste wird abgerufen");
  pthread_mutex_unlock(&g_lock);
  if(upd_sony_manifest(url, &j->mf, mwhy, sizeof mwhy) != 0) {
    snprintf(why, wn, "Die offizielle Paketliste ließ sich nicht laden (%s).", mwhy);
    goto out;
  }
  /* every piece is checked (pieces may come over plain http): one without a checksum makes the list unusable */
  for(int p = 0; p < j->mf.count; p++)
    if(!j->mf.pieces[p].hash[0]) { snprintf(why, wn, "Die Paketliste nennt für Stück %d keine Prüfsumme.", p + 1); goto out; }
  char dir[PATH_MAX], pkg[PATH_MAX], jp[PATH_MAX];
  title_dir(tid, dir, sizeof dir);
  title_file(tid, UPD_PKG_NAME, pkg, sizeof pkg);
  title_file(tid, UPD_JOB_NAME, jp, sizeof jp);
  mkdir(PS5TM_DATA_DIR, 0777);
  mkdir(UPD_DIR, 0777);
  mkdir(dir, 0777);

  /* the parts */
  int nseg = 0;
  for(int p = 0; p < j->mf.count; p++) nseg += (int)((j->mf.pieces[p].size + UPD_SEG - 1) / UPD_SEG);
  j->seg = calloc((size_t)nseg, sizeof *j->seg);
  j->piece_ok = calloc((size_t)j->mf.count, 1);
  j->piece_busy = calloc((size_t)j->mf.count, sizeof(int));
  if(!j->seg || !j->piece_ok || !j->piece_busy) { snprintf(why, wn, "Kein Speicher."); goto out; }
  for(int p = 0; p < j->mf.count; p++)
    for(int64_t o = 0; o < j->mf.pieces[p].size; o += UPD_SEG) {
      seg_t *s = &j->seg[j->nseg++];
      s->piece = p;
      s->off = j->mf.pieces[p].offset + o;
      s->len = j->mf.pieces[p].size - o < UPD_SEG ? j->mf.pieces[p].size - o : UPD_SEG;
    }

  /* what is on disk already: a job of this very package resumes, a package of the same size is checked piece by piece */
  struct stat st;
  int have_file = stat(pkg, &st) == 0 && S_ISREG(st.st_mode);
  char *jb = read_small(jp, 1 << 20);
  cJSON *jj = jb ? cJSON_Parse(jb) : NULL;
  free(jb);
  int resume = 0, reuse = 0;
  if(have_file && jj) {
    const cJSON *u = cJSON_GetObjectItem(jj, "url"), *t = cJSON_GetObjectItem(jj, "total"), *c = cJSON_GetObjectItem(jj, "pieces"),
                *sg = cJSON_GetObjectItem(jj, "seg"), *d = cJSON_GetObjectItem(jj, "done");
    resume = cJSON_IsString(u) && !strcmp(u->valuestring, url) && cJSON_IsNumber(t) && (int64_t)t->valuedouble == j->mf.total &&
             cJSON_IsNumber(c) && (int)c->valuedouble == j->mf.count && cJSON_IsNumber(sg) && (int64_t)sg->valuedouble == UPD_SEG &&
             cJSON_IsString(d) && (int)strlen(d->valuestring) == j->nseg && st.st_size == j->mf.total;
    if(resume)
      for(int i = 0; i < j->nseg; i++) j->seg[i].done = d->valuestring[i] == '1';
  }
  cJSON_Delete(jj);
  if(!resume && have_file && st.st_size == j->mf.total) reuse = 1;           /* a package already there: check it */
  j->fd = open(pkg, O_RDWR | O_CREAT | O_NOFOLLOW, 0666);
  if(j->fd < 0) { snprintf(why, wn, "Die Paketdatei ließ sich nicht anlegen: %s", strerror(errno)); goto out; }
  if(!resume && !reuse) {
    int64_t fb = free_bytes(dir);
    if(fb >= 0 && fb < j->mf.total + UPD_RESERVE) {
      snprintf(why, wn, "Zu wenig Platz: nötig sind %.1f GB, frei sind %.1f GB.", (double)(j->mf.total + UPD_RESERVE) / 1e9, (double)fb / 1e9);
      goto out;
    }
    if(ftruncate(j->fd, 0) != 0 || ftruncate(j->fd, (off_t)j->mf.total) != 0) { snprintf(why, wn, "Die Paketdatei ließ sich nicht anlegen: %s", strerror(errno)); goto out; }
  }
  /* a resumed or reused package: whole pieces are hashed first (a pause can come in between any time) */
  if(resume || reuse) {
    for(int p = 0; p < j->mf.count && !g_abort; p++) {
      int whole = 1;
      for(int i = 0; i < j->nseg; i++)
        if(j->seg[i].piece == p && !j->seg[i].done && !reuse) whole = 0;
      if(!whole) continue;
      char hex[80];
      pthread_mutex_lock(&g_lock);
      snprintf(g_phase, sizeof g_phase, "Vorhandenes Stück %d von %d wird geprüft", p + 1, j->mf.count);
      pthread_mutex_unlock(&g_lock);
      int hr = upd_hash_fd(j->fd, j->mf.pieces[p].offset, j->mf.pieces[p].size, j->mf.pieces[p].hash, hex, &g_abort);
      int good = hr == 0 && !strcasecmp(hex, j->mf.pieces[p].hash);
      for(int i = 0; i < j->nseg; i++)
        if(j->seg[i].piece == p) j->seg[i].done = good;
      j->piece_ok[p] = good;
    }
    if(g_abort) { rc = 1; goto out; }
  }
  for(int i = 0; i < j->nseg; i++)
    if(j->seg[i].done) g_job_base += j->seg[i].len;
  pthread_mutex_lock(&g_lock);
  g_phase[0] = 0;
  title_t *t = find_locked(tid);
  if(t) { t->total = j->mf.total; snprintf(t->job_ver, sizeof t->job_ver, "%s", ver); snprintf(t->job_url, sizeof t->job_url, "%s", url); }
  pthread_mutex_unlock(&g_lock);
  job_save(j, ver, url, 0);

  /* the connections */
  if(conns < 1) conns = 1;
  if(conns > UPD_MAX_CONNS) conns = UPD_MAX_CONNS;
  pthread_t th[UPD_MAX_CONNS];
  wk_t wk[UPD_MAX_CONNS];
  int started = 0;
  for(int i = 0; i < conns; i++) {
    wk[i].j = j;
    wk[i].slot = i;
    pthread_attr_t at;
    pthread_attr_init(&at);
    pthread_attr_setstacksize(&at, 256 * 1024);
    if(pthread_create(&th[started], &at, worker_main, &wk[i]) == 0) started++;
    pthread_attr_destroy(&at);
  }
  if(!started) worker_main(&wk[0]);
  for(int i = 0; i < started; i++) pthread_join(th[i], NULL);
  for(int i = 0; i < UPD_MAX_CONNS; i++) g_slot[i] = 0;
  job_save(j, ver, url, 0);
  if(g_abort) { rc = 1; goto out; }
  if(j->failed) { snprintf(why, wn, "%s", j->why); goto out; }
  /* every piece checked (a piece whose parts all came while another worker hashed may still be open) */
  for(int p = 0; p < j->mf.count; p++) {
    if(j->piece_ok[p]) continue;
    int v = verify_piece(j, p);
    if(v > 0) { rc = 1; goto out; }
    if(v < 0) { snprintf(why, wn, "Stück %d stimmt nicht mit der offiziellen Prüfsumme überein.", p + 1); goto out; }
    j->piece_ok[p] = 1;
  }
  if(fsync(j->fd) != 0) { snprintf(why, wn, "Die Paketdatei ließ sich nicht sichern: %s", strerror(errno)); goto out; }
  job_save(j, ver, url, 1);
  rc = 0;
out:
  if(j->fd >= 0) close(j->fd);
  upd_manifest_free(&j->mf);
  free(j->seg);
  free(j->piece_ok);
  free(j->piece_busy);
  pthread_mutex_destroy(&j->mx);
  free(j);
  pthread_mutex_lock(&g_lock);
  g_phase[0] = 0;
  pthread_mutex_unlock(&g_lock);
  return rc;
}

/* ---------------------------------------------------------------- installing, then checking it took */

static int
install(const char *tid, const char *ver, char *why, size_t wn) {
  char id[16];
  snprintf(id, sizeof id, "upd-%s", tid);
  int transient = 0;
  unsigned seq = 0;
  char err[400] = "";
  int http = 0;
  /* the installer may be busy with another package: wait for it (at most 30 minutes) */
  for(int i = 0; i < 1800; i++) {
    http = ps5tm_pkginst_start_q(id, err, sizeof err, &transient, &seq);
    if(http == 200 || !transient || g_abort) break;
    pthread_mutex_lock(&g_lock);
    snprintf(g_phase, sizeof g_phase, "Wartet auf die Installation: %.70s", err);
    pthread_mutex_unlock(&g_lock);
    sleep(1);
  }
  if(http != 200) { snprintf(why, wn, "Die Installation begann nicht: %s", err[0] ? err : "ohne Angabe"); return -1; }
  pthread_mutex_lock(&g_lock);
  snprintf(g_phase, sizeof g_phase, "Die Konsole installiert das Update");
  pthread_mutex_unlock(&g_lock);
  ps5tm_pkginst_result_t r;
  uint64_t t0 = ps5tm_mono_ms();
  int lost = 0;
  for(;;) {
    int k = ps5tm_pkginst_result_of(seq, &r);
    if(k == 1) break;
    /* the result is gone (far too many installs since), or it takes far too long: the game's version decides below */
    if(k < 0 || ps5tm_mono_ms() - t0 > UPD_INSTALL_MAX_MS) { lost = 1; break; }
    sleep(1);
  }
  if(!lost && r.state != 1) { snprintf(why, wn, "Die Installation ist gescheitert: %s", r.error[0] ? r.error : "ohne Angabe"); return -1; }
  /* the game must report the new version before the package goes (PatchDL: app.db can be updated while the content was
     never applied; then deleting the package loses it) */
  char now[24] = "";
  for(int i = 0; i < 30; i++) {
    if(ps5tm_library_title_info(tid, now, sizeof now) == 1 && !strcmp(now, ver)) return 0;
    sleep(2);
  }
  snprintf(why, wn, "Installiert, aber das Spiel meldet noch Version %s statt %s. Das Paket bleibt erhalten.", now[0] ? now : "?", ver);
  return 1;
}

/* ---------------------------------------------------------------- the queue */

static title_t *
next_queued_locked(void) {
  title_t *best = NULL;
  for(int i = 0; i < g_n; i++)
    if(g_t[i].in_queue && !g_t[i].deleting && (!best || g_t[i].qorder < best->qorder)) best = &g_t[i];
  return best;
}

static void *
queue_thread(void *arg) {
  (void)arg;
  for(;;) {
    pthread_mutex_lock(&g_lock);
    title_t *t = next_queued_locked();
    if(!t || !g_set.enabled) { g_worker_up = 0; g_job_tid[0] = 0; pthread_mutex_unlock(&g_lock); return NULL; }
    t->in_queue = 0;
    char tid[12], ver[16], url[512];
    snprintf(tid, sizeof tid, "%s", t->tid);
    if(t->state == ST_READY) { snprintf(ver, sizeof ver, "%s", t->job_ver); snprintf(url, sizeof url, "%s", t->job_url); }
    else target_locked(t, ver, sizeof ver, url, sizeof url);
    if((!ver[0] || !url[0]) && t->job_ver[0] && t->job_url[0]) {   /* the URL is checked against the host rules on use */
      snprintf(ver, sizeof ver, "%s", t->job_ver);
      snprintf(url, sizeof url, "%s", t->job_url);
    }
    if(!ver[0] || !url[0]) { t->state = ST_IDLE; pthread_mutex_unlock(&g_lock); continue; }
    int ready = t->state == ST_READY;
    t->state = ready ? ST_INSTALLING : ST_DOWNLOADING;
    t->err[0] = 0;
    snprintf(t->job_ver, sizeof t->job_ver, "%s", ver);
    snprintf(t->job_url, sizeof t->job_url, "%s", url);
    snprintf(g_job_tid, sizeof g_job_tid, "%s", tid);
    g_abort = 0;
    g_cancel_req = 0;
    g_job_base = 0;
    g_job_t0 = ps5tm_mono_ms();
    g_rate = 0;
    g_rate_ms = 0;
    int conns = g_set.conns, del = g_set.delete_after;
    pthread_mutex_unlock(&g_lock);

    char why[300] = "";
    int rc = ready ? 0 : download(tid, ver, url, conns, why, sizeof why);
    if(rc == 0 && !ready) {
      pthread_mutex_lock(&g_lock);
      if((t = find_locked(tid))) t->state = ST_INSTALLING;
      pthread_mutex_unlock(&g_lock);
      PS5TM_INFO("upd_downloaded", "Spiel-Updates: %s %s ist geladen und geprüft.", tid, ver);
    }
    int irc = 0;
    if(rc == 0 && !g_abort) irc = install(tid, ver, why, sizeof why);
    pthread_mutex_lock(&g_lock);
    int cancelled = g_cancel_req;
    if((t = find_locked(tid))) {
      if(rc == 1 || (rc == 0 && g_abort)) t->state = cancelled ? ST_IDLE : ST_PAUSED;
      else if(rc < 0) { t->state = ST_FAILED; snprintf(t->err, sizeof t->err, "%s", why); }
      else if(irc < 0) { t->state = ST_READY; snprintf(t->err, sizeof t->err, "%s", why); }
      else {
        t->state = ST_INSTALLED;
        snprintf(t->err, sizeof t->err, "%s", irc > 0 ? why : "");
        if(irc == 0) { snprintf(t->ver, sizeof t->ver, "%s", ver); t->checked = 0; }
      }
    }
    g_job_tid[0] = 0;
    pthread_mutex_unlock(&g_lock);
    char dir[PATH_MAX];
    title_dir(tid, dir, sizeof dir);
    /* the installer may still read the file (its wait above can end before it does): never delete under it */
    if(cancelled || (rc == 0 && irc == 0 && del))
      for(int i = 0; i < 600 && ps5tm_pkginst_busy(); i++) sleep(1);
    if(cancelled) rm_dir_files(dir);
    else if(rc == 0 && irc == 0 && del) rm_dir_files(dir);
    if(rc < 0) PS5TM_WARN("upd_failed", "Spiel-Updates: %s %s: %s", tid, ver, why);
    if(rc == 0 && irc == 0) {
      PS5TM_INFO("upd_installed", "Spiel-Updates: %s ist jetzt auf Version %s.", tid, ver);
      ps5tm_notify("Update installiert: %s %s", tid, ver);
    }
    ps5tm_library_forget();
  }
}

static void
kick_worker_locked(void) {
  if(g_worker_up) return;
  pthread_t th;
  pthread_attr_t at;
  pthread_attr_init(&at);
  pthread_attr_setstacksize(&at, 512 * 1024);
  pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
  if(pthread_create(&th, &at, queue_thread, NULL) == 0) g_worker_up = 1;
  pthread_attr_destroy(&at);
}

/* ---------------------------------------------------------------- the API */

static void
init_locked(void) {
  settings_load_locked();
  if(!g_fw) g_fw = ps5tm_platform_firmware();
}

static double
job_rate_locked(int64_t done) {
  uint64_t now = ps5tm_mono_ms();
  if(!g_rate_ms) { g_rate_ms = now; g_rate_bytes = done; return 0; }
  if(now - g_rate_ms >= 2000) {
    double r = (double)(done - g_rate_bytes) * 1000.0 / (double)(now - g_rate_ms);
    if(r < 0) r = 0;
    g_rate = g_rate > 0 ? g_rate * 0.6 + r * 0.4 : r;
    g_rate_ms = now;
    g_rate_bytes = done;
  }
  return g_rate;
}

cJSON *
ps5tm_updates_json(void) {
  titles_refresh(0);
  pthread_mutex_lock(&g_lock);
  init_locked();
  cJSON *o = cJSON_CreateObject();
  cJSON_AddBoolToObject(o, "ok", 1);
  cJSON_AddBoolToObject(o, "enabled", g_set.enabled);
  cJSON *s = cJSON_AddObjectToObject(o, "settings");
  cJSON_AddNumberToObject(s, "connections", g_set.conns);
  cJSON_AddBoolToObject(s, "delete_after", g_set.delete_after);
  char fw[16];
  upd_fw_text(g_fw, fw, sizeof fw);
  cJSON_AddStringToObject(o, "firmware", fw);
  cJSON_AddBoolToObject(o, "checking", g_checking);
  cJSON_AddNumberToObject(o, "check_done", g_check_done);
  cJSON_AddNumberToObject(o, "check_total", g_check_total);
  int64_t free_b = free_bytes(PS5TM_DATA_DIR);
  cJSON_AddNumberToObject(o, "free_bytes", (double)free_b);
  if(g_job_tid[0]) {
    cJSON *j = cJSON_AddObjectToObject(o, "job");
    title_t *t = find_locked(g_job_tid);
    int64_t done = __atomic_load_n(&g_job_base, __ATOMIC_RELAXED);
    for(int i = 0; i < UPD_MAX_CONNS; i++) done += g_slot[i];
    if(t && done > t->total && t->total) done = t->total;
    cJSON_AddStringToObject(j, "title_id", g_job_tid);
    cJSON_AddStringToObject(j, "name", t ? t->name : "");
    cJSON_AddStringToObject(j, "version", t ? t->job_ver : "");
    cJSON_AddStringToObject(j, "state", t ? k_state[t->state] : "");
    cJSON_AddNumberToObject(j, "total", t ? (double)t->total : 0);
    cJSON_AddNumberToObject(j, "done", (double)done);
    double rate = t && t->state == ST_DOWNLOADING ? job_rate_locked(done) : 0;
    cJSON_AddNumberToObject(j, "bytes_per_s", rate);
    cJSON_AddNumberToObject(j, "eta_s", rate > 1 && t && t->total > done ? (double)(t->total - done) / rate : -1);
    cJSON_AddStringToObject(j, "phase", g_phase);
    int queued = 0;
    for(int i = 0; i < g_n; i++) queued += g_t[i].in_queue;
    cJSON_AddNumberToObject(j, "queued", queued);
  }
  cJSON *arr = cJSON_AddArrayToObject(o, "titles");
  for(int i = 0; i < g_n; i++) {
    title_t *t = &g_t[i];
    cJSON *e = cJSON_CreateObject();
    cJSON_AddStringToObject(e, "title_id", t->tid);
    cJSON_AddStringToObject(e, "name", t->name);
    cJSON_AddNumberToObject(e, "plat", t->plat);
    cJSON_AddStringToObject(e, "version", t->ver);
    cJSON_AddStringToObject(e, "status", status_locked(t));
    cJSON_AddBoolToObject(e, "via_smp", t->source == SRC_SMP);
    char tv[16], tu[512];
    target_locked(t, tv, sizeof tv, tu, sizeof tu);
    cJSON_AddStringToObject(e, "target", t->state != ST_IDLE && t->job_ver[0] ? t->job_ver : tv);
    cJSON_AddStringToObject(e, "latest", t->latest_ver);
    cJSON_AddStringToObject(e, "latest_fw", t->latest_fw);
    cJSON_AddStringToObject(e, "check_why", t->check_why);
    cJSON_AddBoolToObject(e, "queued", t->in_queue);
    cJSON_AddNumberToObject(e, "total", (double)t->total);
    cJSON_AddNumberToObject(e, "have", (double)t->have);
    if(t->err[0]) cJSON_AddStringToObject(e, "error", t->err);
    cJSON_AddItemToArray(arr, e);
  }
  pthread_mutex_unlock(&g_lock);
  return o;
}

/* The packages in the folder (for "Paket löschen" and the list in the settings). */
cJSON *
ps5tm_updates_packages_json(void) {
  cJSON *o = cJSON_CreateObject();
  cJSON_AddBoolToObject(o, "ok", 1);
  cJSON *arr = cJSON_AddArrayToObject(o, "packages");
  DIR *d = opendir(UPD_DIR);
  struct dirent *e;
  while(d && (e = readdir(d))) {
    if(!tid_ok(e->d_name)) continue;
    char pp[PATH_MAX], jp[PATH_MAX];
    title_file(e->d_name, UPD_PKG_NAME, pp, sizeof pp);
    title_file(e->d_name, UPD_JOB_NAME, jp, sizeof jp);
    struct stat st;
    if(stat(pp, &st) != 0) continue;
    char *b = read_small(jp, 1 << 20);
    cJSON *j = b ? cJSON_Parse(b) : NULL;
    free(b);
    cJSON *x = cJSON_CreateObject();
    cJSON_AddStringToObject(x, "title_id", e->d_name);
    const cJSON *v = j ? cJSON_GetObjectItem(j, "version") : NULL;
    cJSON_AddStringToObject(x, "version", cJSON_IsString(v) ? v->valuestring : "");
    cJSON_AddNumberToObject(x, "size", (double)st.st_size);
    cJSON_AddBoolToObject(x, "complete", j && cJSON_IsTrue(cJSON_GetObjectItem(j, "complete")));
    pthread_mutex_lock(&g_lock);
    title_t *t = find_locked(e->d_name);
    cJSON_AddStringToObject(x, "name", t ? t->name : e->d_name);
    cJSON_AddBoolToObject(x, "busy", !strcmp(g_job_tid, e->d_name));
    pthread_mutex_unlock(&g_lock);
    cJSON_Delete(j);
    cJSON_AddItemToArray(arr, x);
  }
  if(d) closedir(d);
  return o;
}

/* Actions. 200 done, else an HTTP status with the reason in msg. */
int
ps5tm_updates_action(const cJSON *b, char *msg, size_t mn) {
  msg[0] = 0;
  const cJSON *ja = cJSON_GetObjectItem(b, "action"), *jid = cJSON_GetObjectItem(b, "id");
  const char *a = cJSON_IsString(ja) ? ja->valuestring : "";
  const char *tid = cJSON_IsString(jid) ? jid->valuestring : "";
  titles_refresh(0);
  pthread_mutex_lock(&g_lock);
  init_locked();
  int rc = 200;
  if(!strcmp(a, "enable") || !strcmp(a, "disable")) {
    g_set.enabled = a[0] == 'e';
    settings_save_locked();
    if(!g_set.enabled) {
      title_t *jt = g_job_tid[0] ? find_locked(g_job_tid) : NULL;
      if(g_job_tid[0] && (!jt || jt->state != ST_INSTALLING)) g_abort = 1;   /* switched off: the running download pauses */
      for(int i = 0; i < g_n; i++)
        if(g_t[i].in_queue) { g_t[i].in_queue = 0; g_t[i].state = ST_IDLE; disk_state_locked(&g_t[i]); }
    }
    snprintf(msg, mn, "%s", g_set.enabled ? "Spiel-Updates sind eingeschaltet." : "Spiel-Updates sind ausgeschaltet.");
    goto out;
  }
  if(!strcmp(a, "settings")) {
    const cJSON *v;
    if(cJSON_IsNumber(v = cJSON_GetObjectItem(b, "connections"))) {
      if(v->valuedouble < 1 || v->valuedouble > UPD_MAX_CONNS) { snprintf(msg, mn, "Verbindungen: 1 bis %d.", UPD_MAX_CONNS); rc = 400; goto out; }
      g_set.conns = (int)v->valuedouble;
    }
    if(cJSON_IsBool(v = cJSON_GetObjectItem(b, "delete_after"))) g_set.delete_after = cJSON_IsTrue(v);
    settings_save_locked();
    snprintf(msg, mn, "Gespeichert.");
    goto out;
  }
  if(!g_set.enabled) { snprintf(msg, mn, "Spiel-Updates sind ausgeschaltet."); rc = 409; goto out; }
  if(!strcmp(a, "check")) {
    if(g_checking) { snprintf(msg, mn, "Die Prüfung läuft schon."); goto out; }
    pthread_t th;
    pthread_attr_t at;
    pthread_attr_init(&at);
    pthread_attr_setstacksize(&at, 512 * 1024);
    pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
    g_checking = 1;
    g_check_done = 0;
    g_check_total = 0;
    if(pthread_create(&th, &at, check_thread, NULL) != 0) { g_checking = 0; snprintf(msg, mn, "Die Prüfung ließ sich nicht starten."); rc = 500; }
    else snprintf(msg, mn, "Die Spiele werden bei den Update-Servern geprüft.");
    pthread_attr_destroy(&at);
    goto out;
  }
  if(!strcmp(a, "update_all")) {
    int k = 0;
    for(int i = 0; i < g_n; i++) {
      title_t *t = &g_t[i];
      char v[16], u[512];
      target_locked(t, v, sizeof v, u, sizeof u);
      if((t->source != SRC_OFFICIAL && t->source != SRC_SMP) || t->in_queue || t->deleting || !strcmp(g_job_tid, t->tid)) continue;
      if((v[0] && (t->state == ST_IDLE || t->state == ST_FAILED || t->state == ST_PAUSED)) || t->state == ST_READY) {
        t->in_queue = 1;
        t->qorder = ++g_qseq;
        if(t->state != ST_READY) t->state = ST_QUEUED;
        k++;
      }
    }
    if(k) kick_worker_locked();
    snprintf(msg, mn, k ? "%d Update(s) in der Warteschlange." : "Kein Spiel hat ein Update, das sich laden lässt.", k);
    goto out;
  }
  title_t *t = tid_ok(tid) ? find_locked(tid) : NULL;
  if(!t) { snprintf(msg, mn, "Dieses Spiel ist nicht (mehr) in der Liste."); rc = 404; goto out; }
  if(t->deleting) { snprintf(msg, mn, "Das Update dieses Spiels wird gerade gelöscht."); rc = 409; goto out; }
  if(!strcmp(a, "download") || !strcmp(a, "resume") || !strcmp(a, "install")) {
    if(t->source != SRC_OFFICIAL && t->source != SRC_SMP) { snprintf(msg, mn, "Nur für Spiele von Disc, aus dem PlayStation Store oder über ShadowMountPlus."); rc = 403; goto out; }
    if(t->in_queue || !strcmp(g_job_tid, t->tid)) { snprintf(msg, mn, "Dieses Update steht schon in der Warteschlange."); goto out; }
    char v[16], u[512];
    target_locked(t, v, sizeof v, u, sizeof u);
    if(t->state != ST_READY && t->state != ST_PAUSED && !v[0]) { snprintf(msg, mn, "Für dieses Spiel gibt es kein Update, das hier läuft."); rc = 409; goto out; }
    if(t->state == ST_PAUSED && t->job_url[0] && !v[0]) { /* a paused job whose target is no longer offered: resume it as it was */ }
    t->in_queue = 1;
    t->qorder = ++g_qseq;
    if(t->state != ST_READY) t->state = ST_QUEUED;
    kick_worker_locked();
    snprintf(msg, mn, "In der Warteschlange.");
    goto out;
  }
  if(!strcmp(a, "pause")) {
    if(!strcmp(g_job_tid, t->tid)) {
      if(t->state == ST_INSTALLING) { snprintf(msg, mn, "Während die Konsole installiert, lässt sich nichts anhalten."); rc = 409; goto out; }
      g_abort = 1;
      snprintf(msg, mn, "Wird angehalten.");
    }
    else if(t->in_queue) { t->in_queue = 0; t->state = ST_IDLE; disk_state_locked(t); snprintf(msg, mn, "Aus der Warteschlange genommen."); }
    goto out;
  }
  if(!strcmp(a, "cancel") || !strcmp(a, "delete_pkg")) {
    if(!strcmp(g_job_tid, t->tid)) {
      if(t->state == ST_INSTALLING) { snprintf(msg, mn, "Während die Konsole installiert, lässt sich nichts löschen."); rc = 409; goto out; }
      g_abort = 1;
      g_cancel_req = 1;
      snprintf(msg, mn, "Wird abgebrochen, die Teile werden gelöscht.");
      goto out;
    }
    t->in_queue = 0;
    t->state = ST_IDLE;
    t->total = t->have = 0;
    t->err[0] = 0;
    char dir[PATH_MAX], tidc[12];
    title_dir(t->tid, dir, sizeof dir);
    snprintf(tidc, sizeof tidc, "%s", t->tid);
    t->deleting = 1;
    pthread_mutex_unlock(&g_lock);
    rm_dir_files(dir);
    pthread_mutex_lock(&g_lock);
    if((t = find_locked(tidc))) t->deleting = 0;
    snprintf(msg, mn, "Gelöscht.");
    goto out;
  }
  snprintf(msg, mn, "Unbekannte Aktion.");
  rc = 400;
out:
  pthread_mutex_unlock(&g_lock);
  return rc;
}

/* ---------------------------------------------------------------- the installer's view of a finished package */

static int
pkg_path_for(const char *id, char *path, size_t n, char *tid, size_t tn) {
  if(!id || strncmp(id, "upd-", 4) || !tid_ok(id + 4)) return -1;
  snprintf(tid, tn, "%s", id + 4);
  title_file(id + 4, UPD_PKG_NAME, path, n);
  pthread_mutex_lock(&g_lock);
  title_t *t = find_locked(id + 4);
  /* only while the update queue itself installs this title: then nobody can delete the file under the installer */
  int ok = t && (t->source == SRC_OFFICIAL || t->source == SRC_SMP) && t->state == ST_INSTALLING && !strcmp(g_job_tid, t->tid);
  pthread_mutex_unlock(&g_lock);
  return ok ? 0 : -1;
}

int
ps5tm_updates_pkg_find(const char *id, ps5tm_pkg_t *out) {
  char path[PATH_MAX], tid[12];
  if(pkg_path_for(id, path, sizeof path, tid, sizeof tid) != 0) return -1;
  if(ps5tm_pkg_parse(path, out) != 0 || !out->valid) return -1;
  /* the package must be an officially signed update of this very game (what came over the network is never trusted alone) */
  if(file_sign(path) != SIGN_RETAIL) return -1;
  if(strcmp(out->title_id, tid) || strcmp(out->kind, "update")) return -1;
  return 0;
}

int
ps5tm_updates_pkg_slices(const char *id, ps5tm_pkgslice_t *out, unsigned max, unsigned *n, uint64_t *total, char *err, size_t err_len) {
  char path[PATH_MAX], tid[12];
  if(max < 1 || pkg_path_for(id, path, sizeof path, tid, sizeof tid) != 0) { snprintf(err, err_len, "Dieses Update ist nicht (mehr) bereit."); return -1; }
  struct stat st;
  if(lstat(path, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size <= 0) { snprintf(err, err_len, "Die Paketdatei des Updates fehlt."); return -1; }
  memset(out, 0, sizeof *out);
  snprintf(out[0].path, sizeof out[0].path, "%s", path);
  out[0].file_off = 0;
  out[0].size = (uint64_t)st.st_size;
  out[0].logical = 0;
  out[0].file_size = (uint64_t)st.st_size;
  out[0].mtime = (int64_t)st.st_mtime;
  *n = 1;
  *total = (uint64_t)st.st_size;
  return 0;
}

/* A job the app was running when it ended is shown as paused (its parts are on disk); nothing starts by itself. */
void
ps5tm_updates_boot(void) {
  pthread_mutex_lock(&g_lock);
  init_locked();
  pthread_mutex_unlock(&g_lock);
}

int
ps5tm_updates_busy(void) {
  pthread_mutex_lock(&g_lock);
  int b = g_job_tid[0] != 0;
  pthread_mutex_unlock(&g_lock);
  return b;
}
