/* Backing up the console's saved games, and putting them back (04.10.2026).
 *
 * What is copied is the saves exactly as the console keeps them — encrypted
 * images, nothing is decrypted, nothing leaves the console. They are bound to
 * this console and its user accounts, so a backup is insurance for the same
 * console, not something to carry to another one (the restore refuses that).
 *
 * ── Where a save lives (read off the console, 04.10.2026; one user = one
 *    8-digit hex folder below /user/home):
 *
 *   <user>/savedata/<CUSA…>/                 PS4 titles: sdimg_<slot> + <slot>.bin
 *   <user>/savedata_prospero/<PPSA…>/        PS5 titles: sdimg_<slot>, and
 *                                            sdimg_sce_bu_… (the console's own copies)
 *   <user>/savedata_meta/user/<CUSA…>/       what the save list shows: icons
 *   <user>/savedata_prospero_meta/user/<PPSA…>/   … and the .sfo files
 *   /system_data/savedata/<user>/db/user/savedata.db, game_setting.dat
 *                                            the console's save database
 *
 * A title's save is its data folder plus its meta folder. The database is
 * copied with a backup (as a record) and never written back: it describes every
 * title at once, and an old one would roll them all back.
 *
 * ── A backup
 *
 *   <drive>/PS5-Sicherung/Spielstaende/<date>/
 *       manifest.json          every file with its size and SHA-256
 *       pruefsummen.sha256     the same, in the format of sha256sum
 *       home/<user>/…          the files, below the same names they have on the console
 *       system/<user>/…
 *
 * It is made under the mark ".ps5cc-unfertig" inside the folder, which goes
 * once everything is in and written back and checked; a folder with the mark is
 * not a backup and is not listed. Nothing is overwritten (O_EXCL), and a failed
 * or cancelled backup removes what it made. Every file is read again from the
 * drive right after it was written, and its CRC-32 must equal the one taken of
 * what was read from the console (checkfile.c, as for game copies).
 *
 * <drive> is /data for the internal SSD, else the drive's own mount point.
 *
 * ── Putting a title back
 *
 * Writes into the console's own save folders, so it is careful in this order:
 *   1. no game may be running — not even paused: a resident game may hold the
 *      save open. Refused otherwise, here and again before the first file is
 *      replaced;
 *   2. the backup must be complete and every file of the title must still match
 *      its SHA-256 (a stick that has been in a drawer for a year);
 *   3. the user must exist on this console, and the backup must come from this
 *      console (model and serial tail);
 *   4. the title's CURRENT save is backed up first, as a backup of its own
 *      (kind "undo", listed like any other, and can itself be put back);
 *   5. EVERY new file goes to "<name>.ps5cc-neu" beside the old one first, is read
 *      back and compared with the SHA-256 of the backup, and takes the old file's
 *      mode and owner. Nothing is replaced yet: a failure, a stop or a pulled plug
 *      here changes nothing on the console (the free room is checked first, and
 *      leftovers of an earlier run are swept);
 *   6. after a last look at the game state, the new files replace the old ones by
 *      rename, one after the other, a matter of milliseconds: a file is old or new,
 *      never half. From the first rename on no stop is taken. Files that only the
 *      console has are left as they are.
 * The database is not touched. A power cut in the moment of the renames can leave
 * some files new and some old; every slot is a file of its own, and the undo
 * backup holds what was there.
 *
 * ── The job
 *
 * One at a time, on a thread of its own, holding the console awake
 * (powerguard.c). It does not run beside a game copy, conversion or move.
 *
 * Titles are never written into the log, only their ids: the log is copied into
 * the kernel's message buffer, which the game probe scans for markers, and a
 * title named "SetControllerFocus(0x7)" would take the pad from the running game.
 */

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <sched.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "checkfile.h"
#include "ioerr.h"
#include "ps5tm.h"
#include "third_party/cJSON.h"
#include "third_party/libdeflate/libdeflate.h"

#ifndef PS5TM_SAVE_HOME
#define PS5TM_SAVE_HOME "/user/home"
#endif
#ifndef PS5TM_SAVE_SYS
#define PS5TM_SAVE_SYS  "/system_data/savedata"
#endif

#define SV_DIR        "PS5-Sicherung/Spielstaende"
#define SV_MARK       ".ps5cc-unfertig"
#define SV_MANIFEST   "manifest.json"
#define SV_SUMS       "pruefsummen.sha256"
#define SV_NEW_SUFFIX ".ps5cc-neu"
#define SV_BUF        (1u << 20)
#define SV_RESERVE    (64ull << 20)           /* never fill a drive up           */
#define SV_MAX_FILES  5000
#define SV_MAX_TITLES 400
#define SV_MAX_USERS  16               /* a PS5 has sixteen accounts                  */
#define SV_MAX_DEPTH  6
#define SV_REL_MAX    240
#define SV_MANIFEST_MAX (8u << 20)
#define SV_MAX_BACKUPS 40               /* the page lists this many, the newest         */
#define SV_NAMES_MAX   1024             /* folders looked at to find them               */

/* The two kinds of save a console keeps. */
typedef struct { const char *data; const char *meta; const char *plat; } area_t;
static const area_t k_area[2] = {
  { "savedata",          "savedata_meta/user",          "PS4" },
  { "savedata_prospero", "savedata_prospero_meta/user", "PS5" },
};

enum { J_IDLE, J_SCAN, J_COPY, J_VERIFY, J_APPLY, J_DONE, J_FAILED, J_CANCELLED };
static const char *const k_state[] = { "idle", "scanning", "copying", "verifying",
                                       "applying", "done", "failed", "cancelled" };

typedef struct {
  int      state;
  char     kind[12];             /* backup, verify, restore                      */
  char     phase[48];            /* what it is doing, for the page               */
  uint64_t work_total, work_done;/* of the phase                                 */
  uint64_t bytes_total;
  unsigned files_total, files_done;
  char     current[160];
  char     error[640];        /* room for PS5TM_IO_TEXT_MAX (ioerr.h) after a label and a path */
  char     note[256];
  char     path[320];            /* the backup made, or checked                  */
  char     undo[320];            /* the backup of the state before a restore     */
  unsigned ok_files, bad_files;
  uint64_t started_ms, finished_ms;
} job_t;

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static job_t           g_job;
static int             g_cancel;                    /* __atomic */
static int             g_nocancel;                  /* __atomic: replacing files, see restore_main */

typedef struct { char rel[SV_REL_MAX]; uint64_t size; uint8_t sha[32]; uint32_t crc; } frec_t;
typedef struct { frec_t *v; size_t n, cap; uint64_t bytes; int err; unsigned skipped; } flist_t;
typedef struct { char uid[9]; char id[10]; int area; } tsel_t;


/* ---------------------------------------------------------------- helpers */

static int
uid_ok(const char *s) {
  if(!s || strlen(s) != 8) return 0;
  for(int i = 0; i < 8; i++)
    if(!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f'))) return 0;
  return 1;
}

/* A title id: four capitals and five digits, PPSA01650 or CUSA00775. */
static int
tid_ok(const char *s) {
  if(!s || strlen(s) != 9) return 0;
  for(int i = 0; i < 4; i++) if(s[i] < 'A' || s[i] > 'Z') return 0;
  for(int i = 4; i < 9; i++) if(s[i] < '0' || s[i] > '9') return 0;
  return 1;
}

static const char *
err_text(int e) {
  switch(e) {
    case ENOSPC:  return "Das Ziel ist voll.";
    case EROFS:   return "Das Ziel lässt sich nicht beschreiben.";
    case EIO:
    case ENXIO:
    case ENODEV:  return ps5tm_io_strerror(e);
    case EACCES:
    case EPERM:   return "Keine Berechtigung.";
    case EEXIST:  return "Dort liegt schon etwas mit diesem Namen.";
    case ENOENT:  return "Eine Datei oder ein Ordner ist verschwunden.";
    case ENAMETOOLONG: return "Ein Dateipfad ist zu lang.";
    case E2BIG:   return "Zu viele Dateien.";
    case ECANCELED: return "Abgebrochen.";
    default:      return strerror(e);
  }
}

static int
cancelled(void) {
  return !__atomic_load_n(&g_nocancel, __ATOMIC_ACQUIRE) &&
         __atomic_load_n(&g_cancel, __ATOMIC_ACQUIRE);
}

/* What ps5tm_digest_fd() is given to watch: nothing while a cancel is not taken. */
static const int *
cancel_flag(void) {
  return __atomic_load_n(&g_nocancel, __ATOMIC_ACQUIRE) ? NULL : &g_cancel;
}

static int
job_active_locked(void) {
  return g_job.state >= J_SCAN && g_job.state <= J_APPLY;
}

static void
job_state(int st) {
  pthread_mutex_lock(&g_lock);
  g_job.state = st;
  if(st >= J_DONE) g_job.finished_ms = ps5tm_mono_ms();
  pthread_mutex_unlock(&g_lock);
}

static void job_fail(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void
job_fail(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  pthread_mutex_lock(&g_lock);
  vsnprintf(g_job.error, sizeof(g_job.error), fmt, ap);
  pthread_mutex_unlock(&g_lock);
  va_end(ap);
  job_state(J_FAILED);
}

static void
job_phase(int st, const char *phase, uint64_t work_total) {
  pthread_mutex_lock(&g_lock);
  g_job.state      = st;
  snprintf(g_job.phase, sizeof(g_job.phase), "%s", phase);
  g_job.work_total = work_total;
  g_job.work_done  = 0;
  pthread_mutex_unlock(&g_lock);
}

static void
job_work(uint64_t n) {
  pthread_mutex_lock(&g_lock);
  g_job.work_done += n;
  pthread_mutex_unlock(&g_lock);
}

static void
job_current(const char *rel) {
  pthread_mutex_lock(&g_lock);
  snprintf(g_job.current, sizeof(g_job.current), "%s", rel);
  pthread_mutex_unlock(&g_lock);
}

static void
job_file_done(void) {
  pthread_mutex_lock(&g_lock);
  g_job.files_done++;
  pthread_mutex_unlock(&g_lock);
}

typedef struct { uint64_t last; } acc_t;

static void
progress_cb(void *ctx, uint64_t done) {
  acc_t *a = ctx;
  job_work(done - a->last);
  a->last = done;
}

static void
hex_of(const uint8_t sha[32], char out[65]) {
  static const char d[] = "0123456789abcdef";
  for(int i = 0; i < 32; i++) {
    out[2 * i]     = d[sha[i] >> 4];
    out[2 * i + 1] = d[sha[i] & 15];
  }
  out[64] = 0;
}

/* Folders that exist are left as they are; the ones made get `mode`. */
static int
mkdir_p(const char *path, mode_t mode) {
  char tmp[PATH_MAX];
  if(snprintf(tmp, sizeof(tmp), "%s", path) >= (int)sizeof(tmp)) return ENAMETOOLONG;
  for(char *p = tmp + 1; *p; p++) {
    if(*p != '/') continue;
    *p = 0;
    if(mkdir(tmp, mode) != 0 && errno != EEXIST) return errno ? errno : EIO;
    *p = '/';
  }
  if(mkdir(tmp, mode) != 0 && errno != EEXIST) return errno ? errno : EIO;
  return 0;
}

/* Removes a folder this job made — only ever called on a backup folder created
   here (mkdir failed if it existed). Depth first, links unlinked and never entered. */
static void
rm_tree(char *path, size_t len, int depth) {
  if(depth > SV_MAX_DEPTH + 4) return;
  for(int pass = 0; pass < 4; pass++) {
    DIR *d = opendir(path);
    if(!d) break;
    struct dirent *e;
    while((e = readdir(d)) != NULL) {
      if(!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
      size_t n = strlen(e->d_name);
      if(len + 1 + n >= PATH_MAX) continue;
      path[len] = '/';
      memcpy(path + len + 1, e->d_name, n + 1);
      struct stat st;
      if(lstat(path, &st) == 0) {
        if(S_ISDIR(st.st_mode)) rm_tree(path, len + 1 + n, depth + 1);
        else unlink(path);
      }
      path[len] = 0;
    }
    closedir(d);
    if(rmdir(path) == 0) return;
  }
}

static void
remove_own(const char *dir) {
  char path[PATH_MAX];
  if(snprintf(path, sizeof(path), "%s", dir) >= (int)sizeof(path)) return;
  rm_tree(path, strlen(path), 0);
}

/* The title's save folders on the console: data, then meta. */
static void
title_dirs(const char *uid, int area, const char *id, char *data, size_t dl,
           char *meta, size_t ml) {
  snprintf(data, dl, "%s/%s/%s/%s", PS5TM_SAVE_HOME, uid, k_area[area].data, id);
  snprintf(meta, ml, "%s/%s/%s/%s", PS5TM_SAVE_HOME, uid, k_area[area].meta, id);
}

static void
user_name(const char *uid, char *out, size_t n) {
  char p[PATH_MAX];
  snprintf(p, sizeof(p), "%s/%s/username.dat", PS5TM_SAVE_HOME, uid);
  out[0] = 0;
  FILE *f = fopen(p, "rb");
  if(f) {
    char raw[40] = {0};
    size_t got = fread(raw, 1, sizeof(raw) - 1, f);
    fclose(f);
    raw[got] = 0;
    ps5tm_copy_utf8(out, n, raw);
    for(char *c = out; *c; c++)
      if((unsigned char)*c < 0x20 || *c == 0x7f) *c = ' ';
  }
  if(!out[0]) snprintf(out, n, "%s", uid);
}

/* What this console is, as far as a backup needs to tell: a backup from another one
   cannot be read here, and the page says so before anyone tries. */
static void
console_id(char *out, size_t n) {
  ps5tm_sysinfo_t info;
  ps5tm_sysinfo_get(&info);
  snprintf(out, n, "%s %s", info.model, info.serial_masked);
}

/* A game that is running — not even a paused one may be there while saves are
   moved: it may hold its save open. The kernel decides where it can answer, the
   cached reading otherwise; either is enough to say yes. */
static int
game_running(char *id, size_t n) {
  id[0] = 0;
  if(ps5tm_procmgr_appinfo_ready()) {
    char t[16] = {0};
    if(ps5tm_procmgr_game_title(0, t, sizeof(t)) == 0 && t[0]) {
      snprintf(id, n, "%s", t);
      return 1;
    }
  }
  ps5tm_gamestate_t gs;
  ps5tm_gamestate_get(&gs);
  if(gs.title_id[0]) {
    snprintf(id, n, "%s", gs.title_id);
    return 1;
  }
  return 0;
}

static int
other_job_active(char *err, size_t err_len) {
  if(ps5tm_gamecopy_busy() || ps5tm_gameconvert_busy() || ps5tm_gamemove_busy() || ps5tm_pkgsplit_busy() || ps5tm_pkginst_busy() ||
     ps5tm_gamedelete_busy()) {
    snprintf(err, err_len, "Es läuft gerade eine Kopie, Konvertierung, ein Verschieben oder Löschen von Spielen, "
             "das Teilen oder Installieren eines Pakets. Das läuft nicht gleichzeitig mit den Spielständen.");
    return 1;
  }
  return 0;
}


/* --------------------------------------------------------- file lists */

static int
flist_add(flist_t *l, const char *rel, uint64_t size) {
  if(l->n >= SV_MAX_FILES) return E2BIG;
  if(strlen(rel) >= SV_REL_MAX) return ENAMETOOLONG;
  if(l->n == l->cap) {
    size_t cap = l->cap ? l->cap * 2 : 128;
    frec_t *v  = realloc(l->v, cap * sizeof(*v));
    if(!v) return ENOMEM;
    l->v = v;
    l->cap = cap;
  }
  frec_t *r = &l->v[l->n++];
  memset(r, 0, sizeof(*r));
  snprintf(r->rel, sizeof(r->rel), "%s", rel);
  r->size = size;
  l->bytes += size;
  return 0;
}

/* Walks abs (extended in place), adding the regular files under the names rel
   gets. Links and special files are no part of a save. */
static void
walk_dir(flist_t *l, char *abs, size_t alen, char *rel, size_t rlen, int depth) {
  if(l->err) return;
  if(depth > SV_MAX_DEPTH) { l->err = ELOOP; return; }
  DIR *d = opendir(abs);
  if(!d) { l->err = errno ? errno : EIO; return; }
  struct dirent *e;
  int rd_errno = 0;
  while(!l->err) {
    /* readdir() returns NULL at the end and on an error alike; only errno tells them apart. A listing that broke off
       half way must not pass for a folder that ends there: the backup would lack the rest and still check out. */
    errno = 0;
    e = readdir(d);
    if(!e) { rd_errno = errno; break; }
    if(!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
    size_t n = strlen(e->d_name);
    size_t sl = strlen(SV_NEW_SUFFIX);
    if(n > sl && !strcmp(e->d_name + n - sl, SV_NEW_SUFFIX)) continue;     /* a file of ours from a restore that was cut off */
    if(alen + 1 + n >= PATH_MAX || rlen + 1 + n >= SV_REL_MAX) { l->err = ENAMETOOLONG; break; }
    abs[alen] = '/';
    memcpy(abs + alen + 1, e->d_name, n + 1);
    rel[rlen] = '/';
    memcpy(rel + rlen + 1, e->d_name, n + 1);
    struct stat st;
    if(lstat(abs, &st) != 0) {
      l->err = errno ? errno : EIO;
    } else if(S_ISDIR(st.st_mode)) {
      walk_dir(l, abs, alen + 1 + n, rel, rlen + 1 + n, depth + 1);
    } else if(S_ISREG(st.st_mode)) {
      int rc = flist_add(l, rel, (uint64_t)st.st_size);
      if(rc) l->err = rc;
    } else {
      l->skipped++;
    }
    abs[alen] = 0;
    rel[rlen] = 0;
  }
  closedir(d);
  if(!l->err && rd_errno) l->err = rd_errno;
}

/* A folder that is not there adds nothing: a title may have no meta folder. */
static void
add_tree(flist_t *l, const char *abs_dir, const char *rel_dir) {
  char abs[PATH_MAX], rel[SV_REL_MAX];
  struct stat st;
  if(lstat(abs_dir, &st) != 0 || !S_ISDIR(st.st_mode)) return;
  if(snprintf(abs, sizeof(abs), "%s", abs_dir) >= (int)sizeof(abs) ||
     snprintf(rel, sizeof(rel), "%s", rel_dir) >= (int)sizeof(rel)) { l->err = ENAMETOOLONG; return; }
  walk_dir(l, abs, strlen(abs), rel, strlen(rel), 0);
}

static void
add_file(flist_t *l, const char *abs_file, const char *rel) {
  struct stat st;
  if(lstat(abs_file, &st) != 0 || !S_ISREG(st.st_mode)) return;
  int rc = flist_add(l, rel, (uint64_t)st.st_size);
  if(rc) l->err = rc;
}

/* Where a name inside a backup lies on the console, or -1 when it is not a name a
   backup of saves can hold. Nothing from a manifest is used before it has passed
   this: no "..", no empty parts, only the folders listed at the top of this file.
   uid and id say which user and title it belongs to (id empty for the database). */
static int
rel_check(const char *rel, char uid[9], char id[10], int *area, int *is_meta) {
  char tmp[SV_REL_MAX];
  if(!rel || !rel[0] || strlen(rel) >= sizeof(tmp) || strchr(rel, '\\')) return -1;
  snprintf(tmp, sizeof(tmp), "%s", rel);
  char *part[16];                   /* the walk goes six folders deep: at most twelve parts */
  int   n = 0;
  char *p = tmp;
  for(;;) {
    char *slash = strchr(p, '/');
    if(slash) *slash = 0;
    if(!*p || !strcmp(p, ".") || !strcmp(p, "..") || n >= 16) return -1;
    part[n++] = p;
    if(!slash) break;
    p = slash + 1;
  }
  uid[0] = id[0] = 0;
  if(area) *area = 0;
  if(is_meta) *is_meta = 0;

  if(n >= 5 && !strcmp(part[0], "home") && uid_ok(part[1])) {
    int a = -1, meta = 0, idx = 0;
    if(!strcmp(part[2], "savedata"))                     { a = 0; idx = 3; }
    else if(!strcmp(part[2], "savedata_prospero"))       { a = 1; idx = 3; }
    else if(n >= 6 && !strcmp(part[3], "user") && !strcmp(part[2], "savedata_meta"))          { a = 0; meta = 1; idx = 4; }
    else if(n >= 6 && !strcmp(part[3], "user") && !strcmp(part[2], "savedata_prospero_meta")) { a = 1; meta = 1; idx = 4; }
    /* a file below the title's folder, so at least one more part */
    if(a < 0 || n <= idx + 1 || !tid_ok(part[idx])) return -1;
    snprintf(uid, 9, "%s", part[1]);
    snprintf(id, 10, "%s", part[idx]);
    if(area) *area = a;
    if(is_meta) *is_meta = meta;
    return 0;
  }
  if(n == 5 && !strcmp(part[0], "system") && uid_ok(part[1]) && !strcmp(part[2], "db") &&
     !strcmp(part[3], "user") && !strcmp(part[4], "savedata.db")) {
    snprintf(uid, 9, "%s", part[1]);
    return 0;
  }
  if(n == 3 && !strcmp(part[0], "system") && uid_ok(part[1]) && !strcmp(part[2], "game_setting.dat")) {
    snprintf(uid, 9, "%s", part[1]);
    return 0;
  }
  return -1;
}

/* A size out of a manifest: a number that is a size a file can have. Not a negative or a huge one, which
   would make the cast to uint64_t undefined. 0, or -1. */
static int
json_size(const cJSON *n, uint64_t *out) {
  if(!cJSON_IsNumber(n)) return -1;
  double d = n->valuedouble;
  if(!(d >= 0.0 && d <= 9007199254740992.0)) return -1;        /* 2^53 */
  *out = (uint64_t)d;
  return 0;
}

/* The console's path of a name inside a backup (already passed rel_check). */
static void
console_path(const char *rel, char *out, size_t n) {
  if(!strncmp(rel, "home/", 5)) snprintf(out, n, "%s/%s", PS5TM_SAVE_HOME, rel + 5);
  else                          snprintf(out, n, "%s/%s", PS5TM_SAVE_SYS, rel + 7);   /* system/ */
}


/* -------------------------------------------------------------- one file */

#ifdef PS5TM_HOST_TEST
/* Called with the name of a file just written, before it is read back: the tests damage
   it there, since a drive that writes wrongly cannot be had on request. */
void (*ps5tm_saves_test_hook)(const char *dst);
/* Called once per file, with the name of the source, after its first read: a test changes the source
   there, since a game that writes while it is copied cannot be had on request. */
void (*ps5tm_saves_mid_hook)(const char *src);
#endif

/* Copies src to dst (a new file), reads dst back from the drive and compares:
   CRC-32 of what was read from src with that of what is on dst. The SHA-256 of the
   read-back lands in rec->sha. 0, or an errno (SRC_SIDE when the original is to
   blame is told apart by err). mode_from, when not NULL, names a file whose mode and
   owner the copy takes; else it is 0600 like the console's own save files. */
static int
copy_verified(const char *src, const char *dst, frec_t *rec, const char *mode_from,
              char *err, size_t err_len) {
  err[0] = 0;
  int in = open(src, O_RDONLY);
  if(in < 0) { snprintf(err, err_len, "Lesen von %s: %s", rec->rel, err_text(errno)); return errno ? errno : EIO; }
  struct stat st;
  if(fstat(in, &st) != 0 || !S_ISREG(st.st_mode)) {
    int e = errno ? errno : EINVAL;
    close(in);
    snprintf(err, err_len, "Lesen von %s: %s", rec->rel, err_text(e));
    return e;
  }
  int out = open(dst, O_WRONLY | O_CREAT | O_EXCL, 0600);
  if(out < 0) {
    int e = errno ? errno : EIO;
    close(in);
    snprintf(err, err_len, "Schreiben von %s: %s", rec->rel, err_text(e));
    return e;
  }

  uint8_t *buf = malloc(SV_BUF);
  if(!buf) { close(in); close(out); unlink(dst); snprintf(err, err_len, "Kein Speicher."); return ENOMEM; }

  uint32_t crc = 0;
  uint64_t got = 0;
  int      rc  = 0;
#ifdef PS5TM_HOST_TEST
  int      first_read = 1;
#endif
  for(;;) {
    if(cancelled()) { rc = ECANCELED; snprintf(err, err_len, "Abgebrochen."); break; }
    ssize_t r = read(in, buf, SV_BUF);
#ifdef PS5TM_HOST_TEST
    if(r > 0 && first_read && ps5tm_saves_mid_hook) { first_read = 0; ps5tm_saves_mid_hook(src); }
#endif
    if(r < 0 && errno == EINTR) continue;
    if(r < 0) { rc = errno ? errno : EIO; snprintf(err, err_len, "Lesen von %s: %s", rec->rel, err_text(rc)); break; }
    if(r == 0) break;
    crc = libdeflate_crc32(crc, buf, (size_t)r);
    for(ssize_t off = 0; off < r; ) {
      ssize_t w = write(out, buf + off, (size_t)(r - off));
      if(w < 0 && errno == EINTR) continue;
      if(w <= 0) { rc = w < 0 && errno ? errno : EIO; break; }
      off += w;
    }
    if(rc) { snprintf(err, err_len, "Schreiben von %s: %s", rec->rel, err_text(rc)); break; }
    got += (uint64_t)r;
    job_work((uint64_t)r);
    sched_yield();                          /* the fan and the web server come first */
  }
  free(buf);
  struct stat st2;
  int changed = fstat(in, &st2) != 0 || st2.st_size != st.st_size || st2.st_mtime != st.st_mtime;
  close(in);

  if(!rc && (got != (uint64_t)st.st_size || changed)) {
    rc = EIO;
    snprintf(err, err_len, "%s hat sich während des Kopierens geändert.", rec->rel);
  }
  if(!rc && fsync(out) != 0) { rc = errno ? errno : EIO; snprintf(err, err_len, "Schreiben von %s: %s", rec->rel, err_text(rc)); }
  if(close(out) != 0 && !rc) { rc = errno ? errno : EIO; snprintf(err, err_len, "Schreiben von %s: %s", rec->rel, err_text(rc)); }
  if(rc) { unlink(dst); return rc; }

#ifdef PS5TM_HOST_TEST
  if(ps5tm_saves_test_hook) ps5tm_saves_test_hook(dst);
#endif

  if(mode_from) {
    struct stat old;
    if(stat(mode_from, &old) == 0) {
      (void)chmod(dst, old.st_mode & 07777);
      (void)chown(dst, old.st_uid, old.st_gid);
    }
  }

  /* read it back from the drive */
  int fd = open(dst, O_RDONLY);
  if(fd < 0) { rc = errno ? errno : EIO; unlink(dst); snprintf(err, err_len, "Prüfen von %s: %s", rec->rel, err_text(rc)); return rc; }
  ps5tm_drop_cache(fd);
  uint32_t crc2 = 0;
  acc_t    acc = { 0 };
  char     derr[400];
  if(ps5tm_digest_fd(fd, got, &crc2, rec->sha, cancel_flag(), progress_cb, &acc, derr, sizeof(derr)) != 0) {
    rc = errno ? errno : EIO;
    snprintf(err, err_len, "%s", derr);
  } else if(crc2 != crc) {
    rc = EIO;
    snprintf(err, err_len, "Beim Zurücklesen weicht %s von dem ab, was gelesen wurde. "
             "Ist das Ziel defekt oder der Stecker locker?", rec->rel);
  }
  close(fd);
  if(rc) { unlink(dst); return rc; }
  rec->size = got;
  rec->crc  = crc;
  return 0;
}


/* ----------------------------------------------------- writing a backup */

typedef struct {
  char     base[64];          /* the drive's own place: /data or a mount point */
  char     kind[8];           /* "backup" or "undo"                            */
  char     dir[320];          /* the backup folder, once made                  */
  flist_t  files;
  tsel_t  *sel;
  unsigned nsel;
  char   (*users)[9];
  unsigned nusers;
  int      with_db;           /* the users' save databases go in (a backup, not an undo copy) */
} bk_t;

/* The name of a title as the page knows it: from the library, which only lists what is installed. A title that is
   not installed any more keeps its saved games and loses its name: so what the library has told is remembered
   (covers_and_more/title-names.json), and for the rest the console's own appmeta is asked (param.json of a PS5 title,
   param.sfo of a PS4 one), which often stays behind after an uninstall. "Installed" is still only what the library lists. */
#define SV_NAMES_FILE   PS5TM_DATA_DIR "/covers_and_more/title-names.json"
#define SV_MEMNAMES_MAX    600

static pthread_mutex_t g_names_lock = PTHREAD_MUTEX_INITIALIZER;
static cJSON          *g_names_mem;        /* id -> name, remembered; under g_names_lock */
static int             g_names_dirty;

static void
names_mem_load_locked(void) {
  if(g_names_mem) return;
  g_names_mem = cJSON_CreateObject();
  FILE *f = fopen(SV_NAMES_FILE, "r");
  if(!f) return;
  char *buf = malloc(128 * 1024);
  if(!buf) { fclose(f); return; }
  size_t n = fread(buf, 1, 128 * 1024 - 1, f);
  fclose(f);
  buf[n] = 0;
  cJSON *o = cJSON_Parse(buf);
  free(buf);
  cJSON *it;
  cJSON_ArrayForEach(it, o)
    if(it->string && cJSON_IsString(it) && it->valuestring[0] && cJSON_GetArraySize(g_names_mem) < SV_MEMNAMES_MAX)
      cJSON_AddStringToObject(g_names_mem, it->string, it->valuestring);
  cJSON_Delete(o);
}

static void
names_mem_save_locked(void) {
  if(!g_names_dirty || !g_names_mem) return;
  char *txt = cJSON_PrintUnformatted(g_names_mem);
  if(!txt) return;
  mkdir(PS5TM_DATA_DIR, 0755);
  mkdir(PS5TM_DATA_DIR "/covers_and_more", 0755);
  char tmp[sizeof(SV_NAMES_FILE) + 8];
  snprintf(tmp, sizeof(tmp), "%s.tmp", SV_NAMES_FILE);
  FILE *f = fopen(tmp, "w");
  if(f) {
    int ok = fputs(txt, f) >= 0;
    ok = (fclose(f) == 0) && ok;
    if(ok && rename(tmp, SV_NAMES_FILE) == 0) g_names_dirty = 0; else remove(tmp);
  }
  free(txt);
}

static void
names_mem_put_locked(const char *id, const char *name) {
  cJSON *old = cJSON_GetObjectItem(g_names_mem, id);
  if(cJSON_IsString(old) && !strcmp(old->valuestring, name)) return;
  if(!old && cJSON_GetArraySize(g_names_mem) >= SV_MEMNAMES_MAX) return;
  cJSON_DeleteItemFromObject(g_names_mem, id);
  cJSON_AddStringToObject(g_names_mem, id, name);
  g_names_dirty = 1;
}

/* One text value out of a param.sfo (PS4), e.g. CATEGORY. 0 when found. */
static int
sfo_value(const unsigned char *b, size_t n, const char *key, char *out, size_t out_len) {
  if(n < 20 || memcmp(b, "\0PSF", 4) != 0) return -1;
  uint32_t kt = (uint32_t)b[8] | (uint32_t)b[9] << 8 | (uint32_t)b[10] << 16 | (uint32_t)b[11] << 24;
  uint32_t dt = (uint32_t)b[12] | (uint32_t)b[13] << 8 | (uint32_t)b[14] << 16 | (uint32_t)b[15] << 24;
  uint32_t cnt = (uint32_t)b[16] | (uint32_t)b[17] << 8 | (uint32_t)b[18] << 16 | (uint32_t)b[19] << 24;
  size_t kl = strlen(key) + 1;
  if(cnt > 256) return -1;
  for(uint32_t i = 0; i < cnt; i++) {
    size_t e = 20 + (size_t)i * 16;
    if(e + 16 > n) return -1;
    uint32_t ko = (uint32_t)b[e] | (uint32_t)b[e + 1] << 8;
    uint32_t len = (uint32_t)b[e + 4] | (uint32_t)b[e + 5] << 8 | (uint32_t)b[e + 6] << 16 | (uint32_t)b[e + 7] << 24;
    uint32_t doff = (uint32_t)b[e + 12] | (uint32_t)b[e + 13] << 8 | (uint32_t)b[e + 14] << 16 | (uint32_t)b[e + 15] << 24;
    if((size_t)kt + ko + kl > n || memcmp(b + kt + ko, key, kl) != 0) continue;
    if((size_t)dt + doff + len > n || len == 0) return -1;
    size_t l = len < out_len ? len : out_len - 1;
    memcpy(out, b + dt + doff, l);
    out[l] = 0;
    out[strnlen(out, l)] = 0;
    return out[0] ? 0 : -1;
  }
  return -1;
}

/* Is the title a game or an app (YouTube, a browser, a system app)? PS5: applicationCategoryType in param.json is 0
   for a game (measured: Arkanoid 0, YouTube 65536). PS4: CATEGORY in param.sfo starts with "g" for a game. A title
   the console no longer has any description of is called a game, as it is by default. Remembered until the app ends. */
static const char *
title_kind(const char *id) {
  static char memo_id[64][10];
  static char memo_kind[64];                          /* 'g' or 'a' */
  static int  nmemo;
  static pthread_mutex_t lk = PTHREAD_MUTEX_INITIALIZER;
  pthread_mutex_lock(&lk);
  for(int i = 0; i < nmemo; i++)
    if(!strcmp(memo_id[i], id)) { const char *r = memo_kind[i] == 'a' ? "app" : "game"; pthread_mutex_unlock(&lk); return r; }
  pthread_mutex_unlock(&lk);

  char kind = 0;
  if(!strncmp(id, "NPXS", 4)) kind = 'a';                          /* PS4 system applications */
  static const char *const roots[] = { "/user/appmeta", "/system_data/priv/appmeta", "/mnt/ext0/user/appmeta", "/mnt/ext1/user/appmeta" };
  for(size_t r = 0; r < sizeof(roots) / sizeof(roots[0]) && !kind; r++) {
    for(int k = 0; k < 2 && !kind; k++) {
      char path[160];
      snprintf(path, sizeof(path), "%s/%s/%s", roots[r], id, k == 0 ? "param.json" : "param.sfo");
      FILE *f = fopen(path, "rb");
      if(!f) continue;
      unsigned char *buf = malloc(96 * 1024);
      size_t n = buf ? fread(buf, 1, 96 * 1024 - 1, f) : 0;
      fclose(f);
      if(buf && n) {
        buf[n] = 0;
        if(k == 0) {
          cJSON *j = cJSON_Parse((const char *)buf);
          cJSON *ct = j ? cJSON_GetObjectItem(j, "applicationCategoryType") : NULL;
          if(cJSON_IsNumber(ct)) kind = ct->valuedouble == 0 ? 'g' : 'a';
          cJSON_Delete(j);
        } else {
          char cat[16];
          if(sfo_value(buf, n, "CATEGORY", cat, sizeof(cat)) == 0) kind = cat[0] == 'g' ? 'g' : 'a';
        }
      }
      free(buf);
    }
  }
  if(!kind) kind = 'g';
  pthread_mutex_lock(&lk);
  if(nmemo < 64) { snprintf(memo_id[nmemo], sizeof(memo_id[0]), "%s", id); memo_kind[nmemo++] = kind; }
  pthread_mutex_unlock(&lk);
  return kind == 'a' ? "app" : "game";
}

/* TITLE out of a param.sfo (the PS4 key/value file). */
static int
sfo_title(const unsigned char *b, size_t n, char *out, size_t out_len) {
  if(n < 20 || memcmp(b, "\0PSF", 4) != 0) return -1;
  uint32_t kt = (uint32_t)b[8] | (uint32_t)b[9] << 8 | (uint32_t)b[10] << 16 | (uint32_t)b[11] << 24;
  uint32_t dt = (uint32_t)b[12] | (uint32_t)b[13] << 8 | (uint32_t)b[14] << 16 | (uint32_t)b[15] << 24;
  uint32_t cnt = (uint32_t)b[16] | (uint32_t)b[17] << 8 | (uint32_t)b[18] << 16 | (uint32_t)b[19] << 24;
  if(cnt > 256) return -1;
  for(uint32_t i = 0; i < cnt; i++) {
    size_t e = 20 + (size_t)i * 16;
    if(e + 16 > n) return -1;
    uint32_t ko = (uint32_t)b[e] | (uint32_t)b[e + 1] << 8;
    uint32_t len = (uint32_t)b[e + 4] | (uint32_t)b[e + 5] << 8 | (uint32_t)b[e + 6] << 16 | (uint32_t)b[e + 7] << 24;
    uint32_t doff = (uint32_t)b[e + 12] | (uint32_t)b[e + 13] << 8 | (uint32_t)b[e + 14] << 16 | (uint32_t)b[e + 15] << 24;
    if((size_t)kt + ko + 6 > n || memcmp(b + kt + ko, "TITLE", 6) != 0) continue;
    if((size_t)dt + doff + len > n || len == 0) return -1;
    size_t l = len;
    if(l >= out_len) l = out_len - 1;
    memcpy(out, b + dt + doff, l);
    out[l] = 0;
    out[strnlen(out, l)] = 0;
    return out[0] ? 0 : -1;
  }
  return -1;
}

static int
meta_title(const char *id, char *out, size_t out_len) {
  static const char *const roots[] = { "/user/appmeta", "/system_data/priv/appmeta", "/mnt/ext0/user/appmeta", "/mnt/ext1/user/appmeta" };
  for(size_t r = 0; r < sizeof(roots) / sizeof(roots[0]); r++) {
    for(int k = 0; k < 2; k++) {
      char path[160];
      snprintf(path, sizeof(path), "%s/%s/%s", roots[r], id, k == 0 ? "param.json" : "param.sfo");
      FILE *f = fopen(path, "rb");
      if(!f) continue;
      unsigned char *buf = malloc(96 * 1024);
      size_t n = buf ? fread(buf, 1, 96 * 1024 - 1, f) : 0;
      fclose(f);
      int ok = -1;
      if(buf && n) {
        buf[n] = 0;
        if(k == 0) {
          cJSON *j = cJSON_Parse((const char *)buf);
          cJSON *lp = j ? cJSON_GetObjectItem(j, "localizedParameters") : NULL;
          cJSON *dl = lp ? cJSON_GetObjectItem(lp, "defaultLanguage") : NULL;
          cJSON *lang = cJSON_IsString(dl) ? cJSON_GetObjectItem(lp, dl->valuestring) : NULL;
          cJSON *tn = lang ? cJSON_GetObjectItem(lang, "titleName") : NULL;
          if(!cJSON_IsString(tn) && lp) {
            cJSON *any;
            cJSON_ArrayForEach(any, lp) {
              cJSON *t2 = cJSON_IsObject(any) ? cJSON_GetObjectItem(any, "titleName") : NULL;
              if(cJSON_IsString(t2)) { tn = t2; break; }
            }
          }
          if(cJSON_IsString(tn) && tn->valuestring[0]) { snprintf(out, out_len, "%s", tn->valuestring); ok = 0; }
          cJSON_Delete(j);
        } else {
          ok = sfo_title(buf, n, out, out_len);
        }
      }
      free(buf);
      if(ok == 0) return 0;
    }
  }
  return -1;
}

static void
names_load(cJSON **map) {
  *map = cJSON_CreateObject();
  cJSON *lib = ps5tm_library_json();
  cJSON *games = lib ? cJSON_GetObjectItem(lib, "games") : NULL;
  cJSON *g;
  pthread_mutex_lock(&g_names_lock);
  names_mem_load_locked();
  cJSON_ArrayForEach(g, games) {
    cJSON *id = cJSON_GetObjectItem(g, "title_id");
    cJSON *nm = cJSON_GetObjectItem(g, "name");
    if(cJSON_IsString(id) && cJSON_IsString(nm) && *map && !cJSON_HasObjectItem(*map, id->valuestring)) {
      cJSON_AddStringToObject(*map, id->valuestring, nm->valuestring);
      if(nm->valuestring[0]) names_mem_put_locked(id->valuestring, nm->valuestring);
    }
  }
  names_mem_save_locked();
  pthread_mutex_unlock(&g_names_lock);
  cJSON_Delete(lib);
}

static const char *
name_of(cJSON *map, const char *id) {
  cJSON *it = map ? cJSON_GetObjectItem(map, id) : NULL;
  return cJSON_IsString(it) ? it->valuestring : "";
}

/* The name to show: the installed title's, else a remembered one, else what appmeta has. Empty when nothing knows it.
   The answer is copied into out. */
static void
name_shown(cJSON *map, const char *id, char *out, size_t out_len) {
  snprintf(out, out_len, "%s", name_of(map, id));
  if(out[0]) return;
  pthread_mutex_lock(&g_names_lock);
  names_mem_load_locked();
  cJSON *m = cJSON_GetObjectItem(g_names_mem, id);
  if(cJSON_IsString(m)) snprintf(out, out_len, "%s", m->valuestring);
  if(!out[0] && meta_title(id, out, out_len) == 0 && out[0]) {
    names_mem_put_locked(id, out);
    names_mem_save_locked();
  }
  pthread_mutex_unlock(&g_names_lock);
}

/* Builds the list of what goes in. */
static int
bk_collect(bk_t *b) {
  for(unsigned i = 0; i < b->nsel; i++) {
    char data[PATH_MAX], meta[PATH_MAX], rd[SV_REL_MAX], rm[SV_REL_MAX];
    const tsel_t *s = &b->sel[i];
    title_dirs(s->uid, s->area, s->id, data, sizeof(data), meta, sizeof(meta));
    snprintf(rd, sizeof(rd), "home/%s/%s/%s", s->uid, k_area[s->area].data, s->id);
    snprintf(rm, sizeof(rm), "home/%s/%s/%s", s->uid, k_area[s->area].meta, s->id);
    add_tree(&b->files, data, rd);
    add_tree(&b->files, meta, rm);
    if(b->files.err) return b->files.err;
  }
  for(unsigned u = 0; u < b->nusers && b->with_db; u++) {
    char abs[PATH_MAX], rel[SV_REL_MAX];
    snprintf(abs, sizeof(abs), "%s/%s/db/user/savedata.db", PS5TM_SAVE_SYS, b->users[u]);
    snprintf(rel, sizeof(rel), "system/%s/db/user/savedata.db", b->users[u]);
    add_file(&b->files, abs, rel);
    snprintf(abs, sizeof(abs), "%s/%s/game_setting.dat", PS5TM_SAVE_SYS, b->users[u]);
    snprintf(rel, sizeof(rel), "system/%s/game_setting.dat", b->users[u]);
    add_file(&b->files, abs, rel);
    if(b->files.err) return b->files.err;
  }
  return 0;
}

static void
stamp_name(char *out, size_t n) {
  time_t t = time(NULL);
  struct tm tmv;
  gmtime_r(&t, &tmv);
  snprintf(out, n, "%04d-%02d-%02d_%02d-%02d-%02d", tmv.tm_year + 1900, tmv.tm_mon + 1,
           tmv.tm_mday, tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
}

/* Writes a backup of b->sel (and the users' databases) below b->base. Returns 0
   with b->dir set, or the errno of what failed, the reason in err. The folder is
   removed on any failure. `tag` is part of the folder's name. */
static int
bk_write(bk_t *b, const char *tag, const char *phase, char *err, size_t err_len) {
  err[0] = 0;
  int rc = bk_collect(b);
  if(rc) { snprintf(err, err_len, "Die Spielstände ließen sich nicht auflisten: %s", err_text(rc)); return rc; }
  if(b->files.n == 0) { snprintf(err, err_len, "Es gibt nichts zu sichern."); return ENOENT; }

  /* room */
  struct statvfs sv;
  char root[PATH_MAX];
  snprintf(root, sizeof(root), "%s/%s", b->base, SV_DIR);
  rc = mkdir_p(root, 0755);
  if(rc) { snprintf(err, err_len, "Der Zielordner ließ sich nicht anlegen: %s", err_text(rc)); return rc; }
  if(statvfs(root, &sv) != 0) { snprintf(err, err_len, "Der Platz auf dem Ziel ließ sich nicht lesen: %s", err_text(errno)); return errno ? errno : EIO; }
  uint64_t fr = sv.f_frsize ? sv.f_frsize : sv.f_bsize;
  uint64_t avail = (uint64_t)sv.f_bavail * fr;
  if(avail < b->files.bytes + SV_RESERVE) {
    snprintf(err, err_len, "Auf dem Ziel ist nicht genug Platz: nötig %llu MB, frei %llu MB.",
             (unsigned long long)((b->files.bytes + SV_RESERVE) >> 20), (unsigned long long)(avail >> 20));
    return ENOSPC;
  }

  /* the folder, under the mark */
  char stamp[40];
  stamp_name(stamp, sizeof(stamp));
  for(int tries = 0; tries < 20; tries++) {
    if(tries) snprintf(b->dir, sizeof(b->dir), "%s/%s%s%s-%d", root, stamp, tag[0] ? "_" : "", tag, tries + 1);
    else      snprintf(b->dir, sizeof(b->dir), "%s/%s%s%s", root, stamp, tag[0] ? "_" : "", tag);
    if(mkdir(b->dir, 0700) == 0) break;
    if(errno != EEXIST || tries == 19) {
      rc = errno ? errno : EIO;
      snprintf(err, err_len, "Der Ordner der Sicherung ließ sich nicht anlegen: %s", err_text(rc));
      b->dir[0] = 0;
      return rc;
    }
  }
  char mark[PATH_MAX];
  snprintf(mark, sizeof(mark), "%s/" SV_MARK, b->dir);
  int mfd = open(mark, O_WRONLY | O_CREAT | O_EXCL, 0600);
  if(mfd >= 0) close(mfd);

  job_phase(J_COPY, phase, b->files.bytes * 2);
  pthread_mutex_lock(&g_lock);
  g_job.bytes_total = b->files.bytes;
  g_job.files_total = (unsigned)b->files.n;
  g_job.files_done  = 0;
  snprintf(g_job.path, sizeof(g_job.path), "%s", b->dir);
  pthread_mutex_unlock(&g_lock);

  for(size_t i = 0; i < b->files.n && !rc; i++) {
    frec_t *r = &b->files.v[i];
    char src[PATH_MAX], dst[PATH_MAX], dir[PATH_MAX];
    console_path(r->rel, src, sizeof(src));
    if(snprintf(dst, sizeof(dst), "%s/%s", b->dir, r->rel) >= (int)sizeof(dst)) { rc = ENAMETOOLONG; snprintf(err, err_len, "Ein Dateipfad ist zu lang."); break; }
    snprintf(dir, sizeof(dir), "%s", dst);
    char *slash = strrchr(dir, '/');
    if(slash) { *slash = 0; rc = mkdir_p(dir, 0700); }
    if(rc) { snprintf(err, err_len, "Ein Ordner der Sicherung ließ sich nicht anlegen: %s", err_text(rc)); break; }
    job_current(r->rel);
    rc = copy_verified(src, dst, r, NULL, err, err_len);
    if(!rc) job_file_done();
  }

  if(!rc) {
    /* a game that started while the files were copied may have written to them */
    char gid[16];
    if(game_running(gid, sizeof(gid))) {
      rc = EBUSY;
      snprintf(err, err_len, "Währenddessen wurde ein Spiel gestartet (%s). %s", gid,
               !strcmp(b->kind, "undo") ? "Es wurde nichts geändert." : "Es wurde keine Sicherung angelegt.");
    }
  }
  if(!rc) {
    /* the checksum file for a PC, then the manifest */
    char sums[PATH_MAX], serr[400];
    snprintf(sums, sizeof(sums), "%s/" SV_SUMS, b->dir);
    ps5tm_sums_t *s = ps5tm_sums_open(sums, serr, sizeof(serr));
    if(!s) { rc = EIO; snprintf(err, err_len, "%s", serr); }
    else {
      for(size_t i = 0; i < b->files.n; i++) ps5tm_sums_add(s, b->files.v[i].sha, b->files.v[i].rel);
      if(ps5tm_sums_close(s, serr, sizeof(serr)) != 0) { rc = EIO; snprintf(err, err_len, "%s", serr); }
      else (void)chmod(sums, 0600);              /* like everything else in the folder */
    }
  }
  if(!rc) {
    cJSON *names = NULL;
    names_load(&names);
    cJSON *root_j = cJSON_CreateObject();
    cJSON_AddNumberToObject(root_j, "format", 1);
    cJSON_AddStringToObject(root_j, "kind", b->kind);
    cJSON_AddNumberToObject(root_j, "created", (double)time(NULL));
    cJSON_AddStringToObject(root_j, "app", PS5TM_VERSION);
    char cid[128];
    console_id(cid, sizeof(cid));
    cJSON_AddStringToObject(root_j, "console", cid);
    cJSON *users = cJSON_AddArrayToObject(root_j, "users");
    for(unsigned u = 0; u < b->nusers; u++) {
      cJSON *uj = cJSON_CreateObject();
      char nm[64];
      user_name(b->users[u], nm, sizeof(nm));
      cJSON_AddStringToObject(uj, "uid", b->users[u]);
      cJSON_AddStringToObject(uj, "name", nm);
      cJSON_AddItemToArray(users, uj);
    }
    cJSON *titles = cJSON_AddArrayToObject(root_j, "titles");
    for(unsigned i = 0; i < b->nsel; i++) {
      const tsel_t *s = &b->sel[i];
      cJSON *tj = cJSON_CreateObject();
      cJSON_AddStringToObject(tj, "uid", s->uid);
      cJSON_AddStringToObject(tj, "id", s->id);
      cJSON_AddStringToObject(tj, "platform", k_area[s->area].plat);
      cJSON_AddStringToObject(tj, "name", name_of(names, s->id));
      char pre_d[SV_REL_MAX], pre_m[SV_REL_MAX];
      snprintf(pre_d, sizeof(pre_d), "home/%s/%s/%s/", s->uid, k_area[s->area].data, s->id);
      snprintf(pre_m, sizeof(pre_m), "home/%s/%s/%s/", s->uid, k_area[s->area].meta, s->id);
      cJSON *fl = cJSON_AddArrayToObject(tj, "files");
      uint64_t bytes = 0;
      for(size_t k = 0; k < b->files.n; k++) {
        const frec_t *r = &b->files.v[k];
        if(strncmp(r->rel, pre_d, strlen(pre_d)) && strncmp(r->rel, pre_m, strlen(pre_m))) continue;
        char hx[65];
        hex_of(r->sha, hx);
        cJSON *fj = cJSON_CreateObject();
        cJSON_AddStringToObject(fj, "p", r->rel);
        cJSON_AddNumberToObject(fj, "s", (double)r->size);
        cJSON_AddStringToObject(fj, "h", hx);
        cJSON_AddItemToArray(fl, fj);
        bytes += r->size;
      }
      cJSON_AddNumberToObject(tj, "bytes", (double)bytes);
      cJSON_AddItemToArray(titles, tj);
    }
    cJSON *sys = cJSON_AddArrayToObject(root_j, "system");
    for(size_t k = 0; k < b->files.n; k++) {
      const frec_t *r = &b->files.v[k];
      if(strncmp(r->rel, "system/", 7)) continue;
      char hx[65];
      hex_of(r->sha, hx);
      cJSON *fj = cJSON_CreateObject();
      cJSON_AddStringToObject(fj, "p", r->rel);
      cJSON_AddNumberToObject(fj, "s", (double)r->size);
      cJSON_AddStringToObject(fj, "h", hx);
      cJSON_AddItemToArray(sys, fj);
    }
    cJSON_AddNumberToObject(root_j, "files", (double)b->files.n);
    cJSON_AddNumberToObject(root_j, "bytes", (double)b->files.bytes);
    char *txt = cJSON_PrintUnformatted(root_j);
    cJSON_Delete(root_j);
    cJSON_Delete(names);
    if(!txt) { rc = ENOMEM; snprintf(err, err_len, "Kein Speicher."); }
    else {
      char mpath[PATH_MAX], tmp[PATH_MAX];
      snprintf(mpath, sizeof(mpath), "%s/" SV_MANIFEST, b->dir);
      snprintf(tmp, sizeof(tmp), "%s.ps5cc-teil", mpath);
      int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0600);
      size_t len = strlen(txt);
      int ok = fd >= 0;
      for(size_t off = 0; ok && off < len; ) {
        ssize_t w = write(fd, txt + off, len - off);
        if(w < 0 && errno == EINTR) continue;
        if(w <= 0) ok = 0; else off += (size_t)w;
      }
      if(ok && fsync(fd) != 0) ok = 0;
      int eno = errno;
      if(fd >= 0 && close(fd) != 0 && ok) { ok = 0; eno = errno; }
      if(ok && rename(tmp, mpath) != 0) { ok = 0; eno = errno; }
      if(!ok) { unlink(tmp); rc = eno ? eno : EIO; snprintf(err, err_len, "Die Liste der Sicherung ließ sich nicht schreiben: %s", err_text(rc)); }
      free(txt);
    }
  }
  if(!rc && unlink(mark) != 0) { rc = errno ? errno : EIO; snprintf(err, err_len, "Die Sicherung ließ sich nicht abschließen: %s", err_text(rc)); }

  if(rc) { remove_own(b->dir); b->dir[0] = 0; }
  return rc;
}


/* ------------------------------------------------------------- manifests */

static cJSON *
manifest_load(const char *dir) {
  char path[PATH_MAX];
  snprintf(path, sizeof(path), "%s/" SV_MANIFEST, dir);
  FILE *f = fopen(path, "rb");
  if(!f) return NULL;
  char *buf = malloc(SV_MANIFEST_MAX + 1);
  if(!buf) { fclose(f); return NULL; }
  size_t n = fread(buf, 1, SV_MANIFEST_MAX, f);
  int big = !feof(f);
  fclose(f);
  buf[n] = 0;
  cJSON *j = big ? NULL : cJSON_Parse(buf);
  free(buf);
  cJSON *fmt = j ? cJSON_GetObjectItem(j, "format") : NULL;
  if(!cJSON_IsNumber(fmt) || fmt->valueint != 1) { cJSON_Delete(j); return NULL; }
  return j;
}

static int
backup_complete(const char *dir) {
  char mark[PATH_MAX];
  struct stat st;
  snprintf(mark, sizeof(mark), "%s/" SV_MARK, dir);
  return lstat(mark, &st) != 0;
}

/* The drives a backup can go to. */
typedef struct { char mount[40]; char base[48]; char label[40]; uint64_t free_bytes; } drive_t;

static unsigned
drives_get(drive_t *out, unsigned max) {
  ps5tm_sysinfo_t info;
  ps5tm_sysinfo_get(&info);
  unsigned n = 0;
  for(unsigned i = 0; i < info.volume_count && n < max; i++) {
    const char *p = info.volumes[i].path;
#ifndef PS5TM_HOST_TEST
    if(strcmp(p, "/user") && strncmp(p, "/mnt/", 5)) continue;
#endif
    snprintf(out[n].mount, sizeof(out[n].mount), "%s", p);
    snprintf(out[n].base, sizeof(out[n].base), "%s", !strcmp(p, "/user") ? "/data" : p);
    snprintf(out[n].label, sizeof(out[n].label), "%s", info.volumes[i].label);
    out[n].free_bytes = info.volumes[i].free_bytes;
    n++;
  }
  return n;
}

/* A path the page names, accepted only when it is a backup folder of one of the
   drives the console has right now — compared name by name, never taken apart. */
static int
backup_path_resolve(const char *path, drive_t *drive_out, char *dir_out, size_t dir_len) {
  if(!path || !*path) return -1;
  drive_t dr[16];
  unsigned nd = drives_get(dr, 16);
  for(unsigned d = 0; d < nd; d++) {
    char root[PATH_MAX];
    snprintf(root, sizeof(root), "%s/%s", dr[d].base, SV_DIR);
    DIR *dp = opendir(root);
    if(!dp) continue;
    struct dirent *e;
    while((e = readdir(dp)) != NULL) {
      if(e->d_name[0] == '.') continue;
      char full[PATH_MAX];
      if(snprintf(full, sizeof(full), "%s/%s", root, e->d_name) >= (int)sizeof(full)) continue;
      if(strcmp(full, path)) continue;
      struct stat st;
      if(lstat(full, &st) != 0 || !S_ISDIR(st.st_mode)) continue;
      snprintf(dir_out, dir_len, "%s", full);
      if(drive_out) *drive_out = dr[d];
      closedir(dp);
      return 0;
    }
    closedir(dp);
  }
  return -1;
}

static int
hex_eq(const char *a, const char *b) {
  if(!a || !b || strlen(a) != 64 || strlen(b) != 64) return 0;
  for(int i = 0; i < 64; i++)
    if(tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) return 0;
  return 1;
}

/* Reads one file of a backup and compares its SHA-256 with the manifest's.
   0 when it matches; else a reason in err. */
static int
check_backup_file(const char *dir, const char *rel, uint64_t size, const char *want_hex,
                  char *err, size_t err_len) {
  char path[PATH_MAX];
  if(snprintf(path, sizeof(path), "%s/%s", dir, rel) >= (int)sizeof(path)) { snprintf(err, err_len, "Pfad zu lang: %s", rel); return -1; }
  int fd = open(path, O_RDONLY);
  if(fd < 0) { snprintf(err, err_len, "%s fehlt in der Sicherung.", rel); return -1; }
  struct stat st;
  if(fstat(fd, &st) != 0 || (uint64_t)st.st_size != size) {
    close(fd);
    snprintf(err, err_len, "%s hat nicht mehr die Größe, die die Sicherung nennt.", rel);
    return -1;
  }
  ps5tm_drop_cache(fd);
  uint8_t sha[32];
  acc_t   acc = { 0 };
  char    derr[400];
  int rc = ps5tm_digest_fd(fd, size, NULL, sha, cancel_flag(), progress_cb, &acc, derr, sizeof(derr));
  close(fd);
  if(rc != 0) { snprintf(err, err_len, "%s", derr); return -1; }
  char hx[65];
  hex_of(sha, hx);
  if(!hex_eq(hx, want_hex)) { snprintf(err, err_len, "%s weicht von der Prüfsumme der Sicherung ab.", rel); return -2; }
  return 0;
}


/* ----------------------------------------------------------- the jobs */

typedef struct {
  char     kind[12];
  char     base[64];                 /* backup: where to */
  char     backup[320];              /* verify, restore: which */
  char     uid[9];
  char     id[10];
  tsel_t  *sel;
  unsigned nsel;
  char   (*users)[9];
  unsigned nusers;
} job_args_t;

static void
args_free(job_args_t *a) {
  free(a->sel);
  free(a->users);
  free(a);
}

static void *backup_main(void *arg);
static void *verify_main(void *arg);
static void *restore_main(void *arg);
static void *delete_main(void *arg);

static int
job_launch(void *(*fn)(void *), job_args_t *a, const char *kind, char *err, size_t err_len) {
  pthread_mutex_lock(&g_lock);
  if(job_active_locked()) {
    pthread_mutex_unlock(&g_lock);
    snprintf(err, err_len, "Es läuft schon ein Vorgang mit den Spielständen.");
    args_free(a);
    return 409;
  }
  memset(&g_job, 0, sizeof(g_job));
  g_job.state = J_SCAN;
  snprintf(g_job.kind, sizeof(g_job.kind), "%s", kind);
  snprintf(g_job.phase, sizeof(g_job.phase), "Vorbereiten");
  g_job.started_ms = ps5tm_mono_ms();
  pthread_mutex_unlock(&g_lock);
  __atomic_store_n(&g_cancel, 0, __ATOMIC_RELEASE);
  __atomic_store_n(&g_nocancel, 0, __ATOMIC_RELEASE);

  /* The question before the call and this claim are not one step: a job of another kind (a split, an installation, a
     copy) may have started on the way. The others ask for this one before they claim their own, so of two that meet at
     least one gives way. No lock of this file is held while asking. */
  if(other_job_active(err, err_len)) {
    pthread_mutex_lock(&g_lock);
    memset(&g_job, 0, sizeof(g_job));                      /* idle again */
    pthread_mutex_unlock(&g_lock);
    args_free(a);
    return 409;
  }

  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
  pthread_t t;
  int rc = pthread_create(&t, &attr, fn, a);
  pthread_attr_destroy(&attr);
  if(rc != 0) {
    pthread_mutex_lock(&g_lock);
    g_job.state = J_IDLE;
    pthread_mutex_unlock(&g_lock);
    snprintf(err, err_len, "Der Vorgang ließ sich nicht starten.");
    args_free(a);
    return 503;
  }
  return 200;
}

/* ---- backup */

static void *
backup_main(void *arg) {
  job_args_t *a = arg;
  ps5tm_powerguard_hold();

  bk_t b;
  memset(&b, 0, sizeof(b));
  snprintf(b.base, sizeof(b.base), "%s", a->base);
  snprintf(b.kind, sizeof(b.kind), "backup");
  b.sel = a->sel; b.nsel = a->nsel;
  b.users = a->users; b.nusers = a->nusers;
  b.with_db = 1;

  PS5TM_INFO("saves_backup_start", "Spielstände: Sicherung von %u Titeln beginnt.", b.nsel);
  char err[640];
  int rc = bk_write(&b, "", "Spielstände kopieren und prüfen", err, sizeof(err));
  if(rc == 0) {
    pthread_mutex_lock(&g_lock);
    g_job.ok_files = (unsigned)b.files.n;
    snprintf(g_job.path, sizeof(g_job.path), "%s", b.dir);
    pthread_mutex_unlock(&g_lock);
    PS5TM_INFO("saves_backup_done", "Spielstände: Sicherung fertig, %zu Dateien (%llu MB) geschrieben, zurückgelesen und geprüft.",
               b.files.n, (unsigned long long)(b.files.bytes >> 20));
    job_state(J_DONE);
  } else if(rc == ECANCELED) {
    PS5TM_INFO("saves_backup_cancelled", "Spielstände: Sicherung abgebrochen, das Angefangene ist entfernt.");
    job_state(J_CANCELLED);
  } else {
    /* the page gets the reason, which names a file; the log gets none: it is copied into the kernel's
       message buffer, and a save file may be called anything, "SetControllerFocus(0x7)" among it */
    PS5TM_WARN("saves_backup_failed", "Spielstände: Sicherung fehlgeschlagen (Fehler %d).", rc);
    job_fail("%s", err);
  }
  free(b.files.v);
  ps5tm_powerguard_release();
  args_free(a);
  return NULL;
}

/* ---- verify */

static void *
verify_main(void *arg) {
  job_args_t *a = arg;
  ps5tm_powerguard_hold();
  char dir[PATH_MAX];
  drive_t dr;
  cJSON *m = NULL;
  if(backup_path_resolve(a->backup, &dr, dir, sizeof(dir)) != 0 || !backup_complete(dir) || !(m = manifest_load(dir))) {
    job_fail("Diese Sicherung ist nicht (mehr) da oder nicht fertig.");
    goto out;
  }
  /* what is to be read */
  flist_t fl = { 0 };
  cJSON *t, *f;
  cJSON_ArrayForEach(t, cJSON_GetObjectItem(m, "titles"))
    cJSON_ArrayForEach(f, cJSON_GetObjectItem(t, "files")) {
      cJSON *p = cJSON_GetObjectItem(f, "p"), *s = cJSON_GetObjectItem(f, "s");
      uint64_t fsz;
      if(cJSON_IsString(p) && json_size(s, &fsz) == 0 && flist_add(&fl, p->valuestring, fsz)) break;
    }
  cJSON_ArrayForEach(f, cJSON_GetObjectItem(m, "system")) {
    cJSON *p = cJSON_GetObjectItem(f, "p"), *s = cJSON_GetObjectItem(f, "s");
    uint64_t fsz;
    if(cJSON_IsString(p) && json_size(s, &fsz) == 0 && flist_add(&fl, p->valuestring, fsz)) break;
  }
  size_t listed = fl.n;
  cJSON *head_count = cJSON_GetObjectItem(m, "files");
  if(listed == 0) {
    free(fl.v);
    job_fail("Die Sicherung enthält keine Dateien.");
    goto out;
  }
  if(cJSON_IsNumber(head_count) && head_count->valuedouble != (double)listed) {
    free(fl.v);
    job_fail("Die Liste der Sicherung stimmt nicht: Sie nennt %zu Dateien, in ihrem Kopf stehen %.0f.", listed, head_count->valuedouble);
    goto out;
  }
  pthread_mutex_lock(&g_lock);
  g_job.files_total = (unsigned)listed;
  g_job.bytes_total = fl.bytes;
  snprintf(g_job.path, sizeof(g_job.path), "%s", dir);
  pthread_mutex_unlock(&g_lock);
  job_phase(J_VERIFY, "Sicherung prüfen", fl.bytes);
  free(fl.v);

  unsigned ok = 0, bad = 0;
  char first[640] = {0};
  cJSON_ArrayForEach(t, cJSON_GetObjectItem(m, "titles"))
    cJSON_ArrayForEach(f, cJSON_GetObjectItem(t, "files")) {
      if(cancelled()) break;
      cJSON *p = cJSON_GetObjectItem(f, "p"), *s = cJSON_GetObjectItem(f, "s"), *h = cJSON_GetObjectItem(f, "h");
      char u[9], id[10];
      uint64_t fsz = 0;
      if(!cJSON_IsString(p) || json_size(s, &fsz) != 0 || !cJSON_IsString(h) || rel_check(p->valuestring, u, id, NULL, NULL) != 0) {
        bad++;
        if(!first[0]) snprintf(first, sizeof(first), "Die Liste der Sicherung enthält einen unzulässigen Eintrag.");
        continue;
      }
      job_current(p->valuestring);
      char err[640];
      if(check_backup_file(dir, p->valuestring, fsz, h->valuestring, err, sizeof(err)) == 0) ok++;
      else { bad++; if(!first[0]) snprintf(first, sizeof(first), "%s", err); }
      job_file_done();
    }
  cJSON_ArrayForEach(f, cJSON_GetObjectItem(m, "system")) {
    if(cancelled()) break;
    cJSON *p = cJSON_GetObjectItem(f, "p"), *s = cJSON_GetObjectItem(f, "s"), *h = cJSON_GetObjectItem(f, "h");
    char u[9], id[10];
    uint64_t fsz = 0;
    if(!cJSON_IsString(p) || json_size(s, &fsz) != 0 || !cJSON_IsString(h) || rel_check(p->valuestring, u, id, NULL, NULL) != 0) {
      bad++;
      if(!first[0]) snprintf(first, sizeof(first), "Die Liste der Sicherung enthält einen unzulässigen Eintrag.");
      continue;
    }
    job_current(p->valuestring);
    char err[640];
    if(check_backup_file(dir, p->valuestring, fsz, h->valuestring, err, sizeof(err)) == 0) ok++;
    else { bad++; if(!first[0]) snprintf(first, sizeof(first), "%s", err); }
    job_file_done();
  }
  pthread_mutex_lock(&g_lock);
  g_job.ok_files  = ok;
  g_job.bad_files = bad;
  pthread_mutex_unlock(&g_lock);
  if(cancelled()) {
    job_state(J_CANCELLED);
  } else if(bad) {
    PS5TM_WARN("saves_verify_bad", "Spielstände: Prüfung einer Sicherung: %u von %u Dateien weichen ab.", bad, ok + bad);
    job_fail("%u von %u Dateien weichen ab oder fehlen. Erste: %s", bad, ok + bad, first);
  } else {
    PS5TM_INFO("saves_verify_ok", "Spielstände: Prüfung einer Sicherung: alle %u Dateien stimmen.", ok);
    job_state(J_DONE);
  }
out:
  cJSON_Delete(m);
  ps5tm_powerguard_release();
  args_free(a);
  return NULL;
}

/* ---- restore */

typedef struct { char rel[SV_REL_MAX]; uint64_t size; char hex[65]; } mfile_t;

#ifdef PS5TM_HOST_TEST
/* The tests look at the job where it changes character: "renaming" and "verifying". */
void (*ps5tm_saves_phase_hook)(const char *phase);
#define PHASE(p) do { if(ps5tm_saves_phase_hook) ps5tm_saves_phase_hook(p); } while(0)
#else
#define PHASE(p) ((void)0)
#endif

static int
cmp_mfile_rel(const void *x, const void *y) {
  return strcmp((*(const mfile_t *const *)x)->rel, (*(const mfile_t *const *)y)->rel);
}

/* The same name twice in one title's list: two staged files would share a temporary name. */
static int
has_duplicate(const mfile_t *f, size_t n) {
  if(n < 2) return 0;
  const mfile_t **idx = malloc(n * sizeof(*idx));
  if(!idx) return 1;                                  /* cannot tell: refuse */
  for(size_t i = 0; i < n; i++) idx[i] = &f[i];
  qsort(idx, n, sizeof(*idx), cmp_mfile_rel);
  int dup = 0;
  for(size_t i = 1; i < n && !dup; i++) dup = !strcmp(idx[i - 1]->rel, idx[i]->rel);
  free(idx);
  return dup;
}

/* At least one regular file below a title's folders in one area? An empty folder — what a restore that
   failed at its first file leaves behind — is no state to save. */
static int
title_has_files(const char *uid, int area, const char *id) {
  char data[PATH_MAX], meta[PATH_MAX], rd[SV_REL_MAX], rm[SV_REL_MAX];
  title_dirs(uid, area, id, data, sizeof(data), meta, sizeof(meta));
  snprintf(rd, sizeof(rd), "x");
  snprintf(rm, sizeof(rm), "x");
  flist_t l = { 0 };
  add_tree(&l, data, rd);
  if(l.n == 0) add_tree(&l, meta, rm);
  int has = l.n > 0 || l.err;                         /* a folder that cannot be listed is not "empty" */
  free(l.v);
  return has;
}

/* Free room on the volume the console's saves are on; UINT64_MAX when that cannot be asked. */
static uint64_t
console_free(void) {
#ifdef PS5TM_HOST_TEST
  const char *e = getenv("PS5TM_SV_FREE");
  if(e && *e) return strtoull(e, NULL, 10);
#endif
  struct statvfs sv;
  if(statvfs(PS5TM_SAVE_HOME, &sv) != 0) return UINT64_MAX;
  uint64_t fr = sv.f_frsize ? sv.f_frsize : sv.f_bsize;
  return (uint64_t)sv.f_bavail * fr;
}

/* Leftovers of ours below a folder, from a run that was stopped by a power cut. */
static void
sweep_tmp(char *path, size_t len, int depth) {
  if(depth > SV_MAX_DEPTH + 2) return;
  DIR *d = opendir(path);
  if(!d) return;
  size_t sl = strlen(SV_NEW_SUFFIX);
  struct dirent *e;
  while((e = readdir(d)) != NULL) {
    if(!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
    size_t n = strlen(e->d_name);
    if(len + 1 + n >= PATH_MAX) continue;
    path[len] = '/';
    memcpy(path + len + 1, e->d_name, n + 1);
    struct stat st;
    if(lstat(path, &st) == 0) {
      if(S_ISDIR(st.st_mode)) sweep_tmp(path, len + 1 + n, depth + 1);
      else if(S_ISREG(st.st_mode) && n > sl && !strcmp(e->d_name + n - sl, SV_NEW_SUFFIX)) unlink(path);
    }
    path[len] = 0;
  }
  closedir(d);
}

/* The files written next to the old ones, taken away again: the first `n` of the list. */
static void
unstage(const mfile_t *f, size_t n) {
  for(size_t i = 0; i < n; i++) {
    char dest[PATH_MAX], tmp[PATH_MAX];
    console_path(f[i].rel, dest, sizeof(dest));
    if(snprintf(tmp, sizeof(tmp), "%s" SV_NEW_SUFFIX, dest) < (int)sizeof(tmp)) unlink(tmp);
  }
}

/* Folders this job made (they were not there before) and nothing came to be in: taken away again, so
   that the title is not left with empty folders that look like a state. rmdir refuses a folder with content. */
static void
drop_new_dirs(const char *uid, const char *id, unsigned areas, const int had[2][2]) {
  for(int ar = 0; ar < 2; ar++) {
    if(!(areas & (1u << ar))) continue;
    char data[PATH_MAX], meta[PATH_MAX];
    title_dirs(uid, ar, id, data, sizeof(data), meta, sizeof(meta));
    if(!had[ar][0]) rmdir(data);
    if(!had[ar][1]) rmdir(meta);
  }
}

static void *
restore_main(void *arg) {
  job_args_t *a = arg;
  ps5tm_powerguard_hold();
  char dir[PATH_MAX], err[640] = {0};
  drive_t dr;
  cJSON *m = NULL;
  mfile_t *files = NULL;
  size_t nfiles = 0, cap = 0, staged = 0;
  bk_t undo;
  memset(&undo, 0, sizeof(undo));
  unsigned areas = 0;                    /* bit 0: savedata (PS4), bit 1: savedata_prospero (PS5)   */
  int had[2][2] = { { 0, 0 }, { 0, 0 } };/* [area][data, meta]: that folder was there before the job */

  if(backup_path_resolve(a->backup, &dr, dir, sizeof(dir)) != 0 || !backup_complete(dir) || !(m = manifest_load(dir))) {
    job_fail("Diese Sicherung ist nicht (mehr) da oder nicht fertig.");
    goto out;
  }

  /* 1: the title's files in the manifest — every entry for it, every name checked */
  {
    cJSON *t;
    int found = 0;
    cJSON_ArrayForEach(t, cJSON_GetObjectItem(m, "titles")) {
      cJSON *u = cJSON_GetObjectItem(t, "uid"), *id = cJSON_GetObjectItem(t, "id");
      if(!cJSON_IsString(u) || !cJSON_IsString(id) || strcmp(u->valuestring, a->uid) || strcmp(id->valuestring, a->id)) continue;
      found = 1;
      cJSON *f;
      cJSON_ArrayForEach(f, cJSON_GetObjectItem(t, "files")) {
        cJSON *p = cJSON_GetObjectItem(f, "p"), *sz = cJSON_GetObjectItem(f, "s"), *h = cJSON_GetObjectItem(f, "h");
        char fu[9], fid[10];
        int  fa = 0;
        uint64_t fsize = 0;
        if(!cJSON_IsString(p) || json_size(sz, &fsize) != 0 || !cJSON_IsString(h) || strlen(h->valuestring) != 64 ||
           rel_check(p->valuestring, fu, fid, &fa, NULL) != 0 || strcmp(fu, a->uid) || strcmp(fid, a->id) || fid[0] == 0) {
          job_fail("Die Liste der Sicherung enthält einen unzulässigen Eintrag. Es wurde nichts geändert.");
          goto out;
        }
        if(nfiles >= SV_MAX_FILES) {
          job_fail("Die Liste der Sicherung nennt für diesen Titel zu viele Dateien. Es wurde nichts geändert.");
          goto out;
        }
        if(nfiles == cap) {
          size_t nc = cap ? cap * 2 : 64;
          mfile_t *g = realloc(files, nc * sizeof(*g));
          if(!g) { job_fail("Kein Speicher."); goto out; }
          files = g;
          cap = nc;
        }
        areas |= 1u << fa;
        snprintf(files[nfiles].rel, sizeof(files[nfiles].rel), "%s", p->valuestring);
        files[nfiles].size = fsize;
        snprintf(files[nfiles].hex, sizeof(files[nfiles].hex), "%s", h->valuestring);
        nfiles++;
      }
    }
    if(!found || nfiles == 0) { job_fail("Dieser Titel ist in der Sicherung nicht enthalten."); goto out; }
    if(has_duplicate(files, nfiles)) {
      job_fail("Die Liste der Sicherung nennt eine Datei mehrfach. Es wurde nichts geändert.");
      goto out;
    }
  }

  uint64_t total = 0;
  for(size_t i = 0; i < nfiles; i++) total += files[i].size;
  pthread_mutex_lock(&g_lock);
  g_job.files_total = (unsigned)nfiles;
  g_job.bytes_total = total;
  snprintf(g_job.path, sizeof(g_job.path), "%s", dir);
  pthread_mutex_unlock(&g_lock);

  /* 2: the backup must still be what it was */
  job_phase(J_VERIFY, "Sicherung prüfen", total);
  for(size_t i = 0; i < nfiles; i++) {
    job_current(files[i].rel);
    if(cancelled()) { job_state(J_CANCELLED); goto out; }
    if(check_backup_file(dir, files[i].rel, files[i].size, files[i].hex, err, sizeof(err)) != 0) {
      if(cancelled()) { job_state(J_CANCELLED); goto out; }
      PS5TM_WARN("saves_restore_refused", "Spielstände: Zurückspielen von %s abgelehnt, die Sicherung ist beschädigt.", a->id);
      job_fail("Die Sicherung ist beschädigt, es wurde nichts geändert. %s", err);
      goto out;
    }
  }

  /* 3: the title's current state first, as a backup of its own — what there is of it (one area
        can be missing, or the folders can be empty) */
  {
    unsigned have_areas = 0;
    for(int ar = 0; ar < 2; ar++)
      if((areas & (1u << ar)) && title_has_files(a->uid, ar, a->id)) have_areas |= 1u << ar;
    if(have_areas) {
      undo.sel   = calloc(2, sizeof(*undo.sel));
      undo.users = calloc(1, sizeof(*undo.users));
      if(!undo.sel || !undo.users) { job_fail("Kein Speicher."); goto out; }
      for(int ar = 0; ar < 2; ar++) {
        if(!(have_areas & (1u << ar))) continue;
        snprintf(undo.sel[undo.nsel].uid, sizeof(undo.sel[undo.nsel].uid), "%s", a->uid);
        snprintf(undo.sel[undo.nsel].id, sizeof(undo.sel[undo.nsel].id), "%s", a->id);
        undo.sel[undo.nsel].area = ar;
        undo.nsel++;
      }
      snprintf(undo.users[0], 9, "%s", a->uid);
      undo.nusers  = 1;
      undo.with_db = 0;                          /* the database is not part of an undo copy */
      snprintf(undo.base, sizeof(undo.base), "%s", dr.base);
      snprintf(undo.kind, sizeof(undo.kind), "undo");
      char tag[40];
      snprintf(tag, sizeof(tag), "vor-Zurueckspielen_%s", a->id);
      int rc = bk_write(&undo, tag, "Jetzigen Stand sichern", err, sizeof(err));
      if(rc == ECANCELED) { job_state(J_CANCELLED); goto out; }
      if(rc != 0) {
        PS5TM_WARN("saves_restore_failed", "Spielstände: Zurückspielen von %s nicht begonnen, die Sicherung des jetzigen Stands schlug fehl.", a->id);
        job_fail("Der jetzige Stand ließ sich nicht sichern, es wurde nichts geändert. %s", err);
        goto out;
      }
      pthread_mutex_lock(&g_lock);
      snprintf(g_job.undo, sizeof(g_job.undo), "%s", undo.dir);
      snprintf(g_job.path, sizeof(g_job.path), "%s", dir);
      pthread_mutex_unlock(&g_lock);
    } else {
      pthread_mutex_lock(&g_lock);
      snprintf(g_job.note, sizeof(g_job.note), "Auf der Konsole gab es für diesen Titel keinen Stand, deshalb auch keine Sicherung davor.");
      pthread_mutex_unlock(&g_lock);
    }
  }

  PHASE("saved");

  /* 4: a game must not have started meanwhile */
  {
    char gid[16];
    if(game_running(gid, sizeof(gid))) {
      job_fail("Währenddessen wurde ein Spiel gestartet (%s). Es wurde nichts zurückgespielt; "
               "der jetzige Stand ist gesichert.", gid);
      goto out;
    }
  }

  /* 5: room on the console. All the new files are written next to the old ones before any is replaced,
        so for a while both exist. */
  {
    uint64_t avail = console_free();
    if(avail != UINT64_MAX && avail < total + SV_RESERVE) {
      job_fail("Auf der Konsole ist nicht genug Platz, um die Dateien neben die jetzigen zu schreiben: nötig %llu MB, frei %llu MB. "
               "Es wurde nichts geändert%s.", (unsigned long long)((total + SV_RESERVE) >> 20), (unsigned long long)(avail >> 20),
               undo.dir[0] ? "; der jetzige Stand ist gesichert" : "");
      goto out;
    }
  }

  /* 6: stage. Every new file is written next to its old one and checked against the manifest; nothing is
        replaced yet, so a stop or a failure here changes nothing on the console. */
  for(int ar = 0; ar < 2; ar++) {
    if(!(areas & (1u << ar))) continue;
    char data[PATH_MAX], meta[PATH_MAX];
    struct stat st;
    title_dirs(a->uid, ar, a->id, data, sizeof(data), meta, sizeof(meta));
    had[ar][0] = stat(data, &st) == 0 && S_ISDIR(st.st_mode);
    had[ar][1] = stat(meta, &st) == 0 && S_ISDIR(st.st_mode);
    sweep_tmp(data, strlen(data), 0);
    sweep_tmp(meta, strlen(meta), 0);
  }
  pthread_mutex_lock(&g_lock);
  g_job.files_done  = 0;
  g_job.files_total = (unsigned)nfiles;     /* the undo copy had its own numbers */
  g_job.bytes_total = total;
  snprintf(g_job.path, sizeof(g_job.path), "%s", dir);
  pthread_mutex_unlock(&g_lock);
  job_phase(J_APPLY, "Zurückspielen", total * 2);
  for(size_t i = 0; i < nfiles; i++) {
    char src[PATH_MAX], dest[PATH_MAX], tmp[PATH_MAX], parent[PATH_MAX];
    snprintf(src, sizeof(src), "%s/%s", dir, files[i].rel);
    console_path(files[i].rel, dest, sizeof(dest));
    if(snprintf(tmp, sizeof(tmp), "%s" SV_NEW_SUFFIX, dest) >= (int)sizeof(tmp)) {
      unstage(files, staged);
      drop_new_dirs(a->uid, a->id, areas, had);
      job_fail("Ein Dateipfad ist zu lang. Es wurde nichts geändert.");
      goto out;
    }
    if(cancelled()) {
      unstage(files, staged);
      drop_new_dirs(a->uid, a->id, areas, had);
      job_state(J_CANCELLED);
      goto out;
    }
    snprintf(parent, sizeof(parent), "%s", dest);
    char *slash = strrchr(parent, '/');
    if(slash) *slash = 0;
    int rc = mkdir_p(parent, 0700);
    if(rc) {
      unstage(files, staged);
      drop_new_dirs(a->uid, a->id, areas, had);
      job_fail("Ein Ordner der Konsole ließ sich nicht anlegen: %s Es wurde nichts geändert.", err_text(rc));
      goto out;
    }
    unlink(tmp);                                   /* a leftover of ours from a stopped run */
    job_current(files[i].rel);
    frec_t rec;
    memset(&rec, 0, sizeof(rec));
    snprintf(rec.rel, sizeof(rec.rel), "%s", files[i].rel);
    rc = copy_verified(src, tmp, &rec, dest, err, sizeof(err));
    if(rc == 0) {
      char hx[65];
      hex_of(rec.sha, hx);
      if(!hex_eq(hx, files[i].hex)) {
        rc = EIO;
        snprintf(err, sizeof(err), "%s weicht nach dem Kopieren von der Sicherung ab.", files[i].rel);
        unlink(tmp);
      }
    }
    if(rc != 0) {
      unstage(files, staged);                      /* the failed one removed itself */
      drop_new_dirs(a->uid, a->id, areas, had);
      if(rc == ECANCELED) {
        job_state(J_CANCELLED);
      } else {
        PS5TM_WARN("saves_restore_failed", "Spielstände: Zurückspielen von %s abgebrochen nach %zu von %zu Dateien, nichts ersetzt.", a->id, i, nfiles);
        job_fail("Das Zurückspielen wurde abgebrochen, bevor etwas ersetzt wurde: %s Auf der Konsole ist alles beim Alten.", err);
      }
      goto out;
    }
    staged = i + 1;
    job_file_done();
  }

  PHASE("staged");

  /* 6b: writing the files can take minutes from a stick, and a game may have been started meanwhile:
         the last look, before the point of no return */
  {
    char gid[16];
    if(game_running(gid, sizeof(gid))) {
      unstage(files, staged);
      drop_new_dirs(a->uid, a->id, areas, had);
      job_fail("Währenddessen wurde ein Spiel gestartet (%s). Es wurde nichts ersetzt; auf der Konsole ist alles beim Alten.", gid);
      goto out;
    }
  }

  /* 7: replace. From here no stop is taken: stopping in the middle would leave a mix, and the renames
        are a matter of milliseconds, the files being in place already. */
  __atomic_store_n(&g_nocancel, 1, __ATOMIC_RELEASE);
  PHASE("renaming");
  for(size_t i = 0; i < nfiles; i++) {
    char dest[PATH_MAX], tmp[PATH_MAX];
    console_path(files[i].rel, dest, sizeof(dest));
    snprintf(tmp, sizeof(tmp), "%s" SV_NEW_SUFFIX, dest);
    if(rename(tmp, dest) != 0) {
      int rc = errno ? errno : EIO;
      unstage(files + i, nfiles - i);
      if(i == 0) drop_new_dirs(a->uid, a->id, areas, had);       /* nothing was replaced: no empty folders either */
      PS5TM_WARN("saves_restore_failed", "Spielstände: Zurückspielen von %s unterbrochen nach %zu von %zu Dateien.", a->id, i, nfiles);
      job_fail("Das Zurückspielen wurde nach %zu von %zu Dateien unterbrochen: %s ließ sich nicht an seinen Platz setzen (%s). "
               "Der Stand davor liegt in der Sicherung vor dem Zurückspielen.", i, nfiles, files[i].rel, err_text(rc));
      goto out;
    }
  }

  /* 8: what is on the console now equals the backup */
  PHASE("verifying");
  job_phase(J_VERIFY, "Ergebnis prüfen", total);
  for(size_t i = 0; i < nfiles; i++) {
    char dest[PATH_MAX], fe[256];
    console_path(files[i].rel, dest, sizeof(dest));
    int fd = open(dest, O_RDONLY);
    uint8_t sha[32];
    acc_t acc = { 0 };
    char derr[400];
    int bad = fd < 0;
    if(!bad) {
      ps5tm_drop_cache(fd);
      bad = ps5tm_digest_fd(fd, files[i].size, NULL, sha, NULL, progress_cb, &acc, derr, sizeof(derr)) != 0;
      close(fd);
    }
    char hx[65] = {0};
    if(!bad) { hex_of(sha, hx); bad = !hex_eq(hx, files[i].hex); }
    if(bad) {
      snprintf(fe, sizeof(fe), "%s", files[i].rel);
      PS5TM_WARN("saves_restore_failed", "Spielstände: Zurückspielen von %s: eine Datei weicht nach dem Zurückspielen ab.", a->id);
      job_fail("Nach dem Zurückspielen weicht %s von der Sicherung ab. Der Stand davor liegt in der Sicherung vor dem Zurückspielen.", fe);
      goto out;
    }
  }
  pthread_mutex_lock(&g_lock);
  g_job.ok_files = (unsigned)nfiles;
  pthread_mutex_unlock(&g_lock);
  PS5TM_INFO("saves_restore_done", "Spielstände: Titel %s zurückgespielt, %zu Dateien geschrieben, zurückgelesen und geprüft.", a->id, nfiles);
  job_state(J_DONE);

out:
  free(files);
  free(undo.files.v);
  free(undo.sel);
  free(undo.users);
  cJSON_Delete(m);
  ps5tm_powerguard_release();
  args_free(a);
  return NULL;
}




/* ---- delete: a title's saved games for one user, with a copy of them first (the same "undo" copy a restore makes,
   so it can be put back), then the folders are removed. The console's own save database is not touched. */

static void *delete_main(void *arg);

static void *
delete_main(void *arg) {
  job_args_t *a = arg;
  ps5tm_powerguard_hold();
  char err[640] = {0};
  bk_t undo;
  memset(&undo, 0, sizeof(undo));
  int area = a->nsel ? a->sel[0].area : 0;
  char data[PATH_MAX], meta[PATH_MAX];
  title_dirs(a->uid, area, a->id, data, sizeof(data), meta, sizeof(meta));

  if(a->base[0]) {
    undo.sel   = calloc(1, sizeof(*undo.sel));
    undo.users = calloc(1, sizeof(*undo.users));
    if(!undo.sel || !undo.users) { job_fail("Kein Speicher."); goto out; }
    snprintf(undo.sel[0].uid, sizeof(undo.sel[0].uid), "%s", a->uid);
    snprintf(undo.sel[0].id, sizeof(undo.sel[0].id), "%s", a->id);
    undo.sel[0].area = area;
    undo.nsel = 1;
    snprintf(undo.users[0], 9, "%s", a->uid);
    undo.nusers  = 1;
    undo.with_db = 0;
    snprintf(undo.base, sizeof(undo.base), "%s", a->base);
    snprintf(undo.kind, sizeof(undo.kind), "undo");
    char tag[40];
    snprintf(tag, sizeof(tag), "vor-Loeschen_%s", a->id);
    int rc = bk_write(&undo, tag, "Stand vor dem Löschen sichern", err, sizeof(err));
    if(rc == ECANCELED) { job_state(J_CANCELLED); goto out; }
    if(rc != 0) {
      PS5TM_WARN("saves_delete_failed", "Spielstände: Löschen von %s nicht begonnen, die Sicherung davor schlug fehl.", a->id);
      job_fail("Der Stand ließ sich nicht sichern, deshalb wurde nichts gelöscht. %s", err);
      goto out;
    }
    pthread_mutex_lock(&g_lock);
    snprintf(g_job.undo, sizeof(g_job.undo), "%s", undo.dir);
    pthread_mutex_unlock(&g_lock);
  }

  {
    char gid[16];
    if(game_running(gid, sizeof(gid))) {
      job_fail("Währenddessen wurde ein Spiel gestartet (%s). Es wurde nichts gelöscht.", gid);
      goto out;
    }
  }

  __atomic_store_n(&g_nocancel, 1, __ATOMIC_RELEASE);
  job_phase(J_APPLY, "Löschen", 0);
  job_current(a->id);
  remove_own(data);
  remove_own(meta);
  struct stat st;
  if(stat(data, &st) == 0 || stat(meta, &st) == 0) {
    PS5TM_WARN("saves_delete_failed", "Spielstände: Löschen von %s unvollständig.", a->id);
    job_fail("Die Spielstände ließen sich nicht ganz löschen (Reste liegen noch auf der Konsole).%s",
             undo.dir[0] ? " Der Stand davor ist gesichert." : "");
    goto out;
  }
  PS5TM_INFO("saves_delete_done", "Spielstände: %s von Benutzer %s gelöscht%s.", a->id, a->uid,
             undo.dir[0] ? ", der Stand davor ist gesichert" : " (ohne Sicherung davor)");
  job_state(J_DONE);

out:
  free(undo.files.v);
  free(undo.sel);
  free(undo.users);
  ps5tm_powerguard_release();
  args_free(a);
  return NULL;
}


/* ------------------------------------------------------------ the starts */

/* Title ids and user ids a page names are looked up in what the console has: a
   name that is not there is refused, never joined into a path. */
static int
user_exists(const char *uid) {
  if(!uid_ok(uid)) return 0;
  char p[PATH_MAX];
  struct stat st;
  snprintf(p, sizeof(p), "%s/%s", PS5TM_SAVE_HOME, uid);
  return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

static int
drive_resolve(const char *mount, drive_t *out) {
  drive_t dr[16];
  unsigned n = drives_get(dr, 16);
  for(unsigned i = 0; i < n; i++)
    if(!strcmp(dr[i].mount, mount)) { *out = dr[i]; return 0; }
  return -1;
}

/* The area a title's save folder is in for this user, or -1. */
static int
title_find(const char *uid, const char *id) {
  for(int a = 0; a < 2; a++) {
    char data[PATH_MAX], meta[PATH_MAX];
    struct stat st;
    title_dirs(uid, a, id, data, sizeof(data), meta, sizeof(meta));
    if(stat(data, &st) == 0 && S_ISDIR(st.st_mode)) return a;
  }
  return -1;
}

/* All titles of one user, every area. Returns how many fit. */
static unsigned
titles_of(const char *uid, tsel_t *out, unsigned max) {
  unsigned n = 0;
  for(int a = 0; a < 2; a++) {
    char dir[PATH_MAX];
    snprintf(dir, sizeof(dir), "%s/%s/%s", PS5TM_SAVE_HOME, uid, k_area[a].data);
    DIR *d = opendir(dir);
    if(!d) continue;
    struct dirent *e;
    while((e = readdir(d)) != NULL && n < max) {
      if(!tid_ok(e->d_name)) continue;
      char full[PATH_MAX];
      struct stat st;
      snprintf(full, sizeof(full), "%s/%s", dir, e->d_name);
      if(stat(full, &st) != 0 || !S_ISDIR(st.st_mode)) continue;
      snprintf(out[n].uid, sizeof(out[n].uid), "%s", uid);
      snprintf(out[n].id, sizeof(out[n].id), "%s", e->d_name);
      out[n].area = a;
      n++;
    }
    closedir(d);
  }
  return n;
}

static unsigned
users_all(char (*out)[9], unsigned max) {
  unsigned n = 0;
  DIR *d = opendir(PS5TM_SAVE_HOME);
  if(!d) return 0;
  struct dirent *e;
  while((e = readdir(d)) != NULL && n < max) {
    if(!uid_ok(e->d_name)) continue;
    snprintf(out[n], 9, "%s", e->d_name);
    n++;
  }
  closedir(d);
  return n;
}

int
ps5tm_saves_backup_start(const char *target_mount, const char **users, unsigned nusers,
                         const char **titles, unsigned ntitles, char *err, size_t err_len) {
  char gid[16];
  if(game_running(gid, sizeof(gid))) {
    snprintf(err, err_len, "Es läuft ein Spiel (%s). Spielstände werden nur gesichert, wenn kein Spiel "
             "läuft, auch kein pausiertes: Sonst wäre die Sicherung vielleicht halb geschrieben.", gid);
    return 409;
  }
  if(other_job_active(err, err_len)) return 409;

  drive_t dr;
  if(!target_mount || drive_resolve(target_mount, &dr) != 0) {
    snprintf(err, err_len, "Dieses Ziel gibt es an der Konsole gerade nicht.");
    return 404;
  }

  job_args_t *a = calloc(1, sizeof(*a));
  if(!a) { snprintf(err, err_len, "Kein Speicher."); return 503; }
  snprintf(a->kind, sizeof(a->kind), "backup");
  snprintf(a->base, sizeof(a->base), "%s", dr.base);
  a->sel   = calloc(SV_MAX_TITLES, sizeof(*a->sel));
  a->users = calloc(SV_MAX_USERS, sizeof(*a->users));
  if(!a->sel || !a->users) { args_free(a); snprintf(err, err_len, "Kein Speicher."); return 503; }

  /* which users: the ones named, else all */
  if(nusers) {
    for(unsigned i = 0; i < nusers && a->nusers < SV_MAX_USERS; i++) {
      if(!user_exists(users[i])) { args_free(a); snprintf(err, err_len, "Diesen Benutzer gibt es an der Konsole nicht."); return 404; }
      int dup = 0;
      for(unsigned k = 0; k < a->nusers; k++) if(!strcmp(a->users[k], users[i])) dup = 1;
      if(!dup) snprintf(a->users[a->nusers++], 9, "%s", users[i]);
    }
  } else {
    a->nusers = users_all(a->users, SV_MAX_USERS);
  }

  /* which titles: the ones named ("<user>/<id>"), else all of the chosen users */
  if(ntitles) {
    for(unsigned i = 0; i < ntitles && a->nsel < SV_MAX_TITLES; i++) {
      char uid[9], id[10];
      const char *s = titles[i];
      if(!s || strlen(s) != 18 || s[8] != '/') { args_free(a); snprintf(err, err_len, "Ein Titel ist falsch angegeben."); return 400; }
      memcpy(uid, s, 8); uid[8] = 0;
      memcpy(id, s + 9, 9); id[9] = 0;
      if(!uid_ok(uid) || !tid_ok(id)) { args_free(a); snprintf(err, err_len, "Ein Titel ist falsch angegeben."); return 400; }
      int in_users = 0;
      for(unsigned k = 0; k < a->nusers; k++) if(!strcmp(a->users[k], uid)) in_users = 1;
      if(!in_users) continue;
      int area = title_find(uid, id);
      if(area < 0) { args_free(a); snprintf(err, err_len, "Diesen Titel gibt es auf der Konsole nicht."); return 404; }
      snprintf(a->sel[a->nsel].uid, sizeof(a->sel[a->nsel].uid), "%s", uid);
      snprintf(a->sel[a->nsel].id, sizeof(a->sel[a->nsel].id), "%s", id);
      a->sel[a->nsel].area = area;
      a->nsel++;
    }
  } else {
    for(unsigned u = 0; u < a->nusers && a->nsel < SV_MAX_TITLES; u++)
      a->nsel += titles_of(a->users[u], a->sel + a->nsel, SV_MAX_TITLES - a->nsel);
  }
  if(a->nsel == 0) { args_free(a); snprintf(err, err_len, "Es gibt nichts zu sichern."); return 400; }

  return job_launch(backup_main, a, "backup", err, err_len);
}

int
ps5tm_saves_verify_start(const char *path, char *err, size_t err_len) {
  char dir[PATH_MAX];
  if(backup_path_resolve(path, NULL, dir, sizeof(dir)) != 0) {
    snprintf(err, err_len, "Diese Sicherung gibt es nicht (mehr).");
    return 404;
  }
  if(other_job_active(err, err_len)) return 409;
  job_args_t *a = calloc(1, sizeof(*a));
  if(!a) { snprintf(err, err_len, "Kein Speicher."); return 503; }
  snprintf(a->backup, sizeof(a->backup), "%s", dir);
  return job_launch(verify_main, a, "verify", err, err_len);
}

int
ps5tm_saves_restore_start(const char *path, const char *uid, const char *title,
                          char *err, size_t err_len) {
  char dir[PATH_MAX];
  if(!uid_ok(uid) || !tid_ok(title)) { snprintf(err, err_len, "Benutzer oder Titel sind falsch angegeben."); return 400; }
  if(backup_path_resolve(path, NULL, dir, sizeof(dir)) != 0) {
    snprintf(err, err_len, "Diese Sicherung gibt es nicht (mehr).");
    return 404;
  }
  char gid[16];
  if(game_running(gid, sizeof(gid))) {
    snprintf(err, err_len, "Es läuft ein Spiel (%s). Spielstände werden nur zurückgespielt, wenn kein Spiel "
             "läuft, auch kein pausiertes: Es könnte seinen Spielstand offen halten. Beende das Spiel zuerst.", gid);
    return 409;
  }
  if(other_job_active(err, err_len)) return 409;
  if(!user_exists(uid)) {
    snprintf(err, err_len, "Diesen Benutzer gibt es an dieser Konsole nicht. Eine Sicherung lässt sich nur auf der "
             "Konsole zurückspielen, von der sie stammt.");
    return 404;
  }
  /* the backup must be complete and from this console */
  cJSON *m = NULL;
  if(!backup_complete(dir) || !(m = manifest_load(dir))) {
    cJSON_Delete(m);
    snprintf(err, err_len, "Diese Sicherung ist nicht fertig oder nicht lesbar.");
    return 409;
  }
  cJSON *cj = cJSON_GetObjectItem(m, "console");
  char mine[128];
  console_id(mine, sizeof(mine));
  int same = cJSON_IsString(cj) && !strcmp(cj->valuestring, mine);      /* no name: not provably ours */
  int holds = 0;
  cJSON *tt;
  cJSON_ArrayForEach(tt, cJSON_GetObjectItem(m, "titles")) {
    cJSON *tu = cJSON_GetObjectItem(tt, "uid"), *ti = cJSON_GetObjectItem(tt, "id");
    if(cJSON_IsString(tu) && cJSON_IsString(ti) && !strcmp(tu->valuestring, uid) && !strcmp(ti->valuestring, title)) holds = 1;
  }
  cJSON_Delete(m);
  if(!holds) {
    snprintf(err, err_len, "Dieser Titel ist in der Sicherung nicht enthalten.");
    return 404;
  }
  if(!same) {
    snprintf(err, err_len, "Diese Sicherung stammt von einer anderen Konsole. Die Spielstände sind an ihre "
             "Konsole gebunden und ließen sich hier nicht öffnen; es wird nichts zurückgespielt.");
    return 409;
  }

  job_args_t *a = calloc(1, sizeof(*a));
  if(!a) { snprintf(err, err_len, "Kein Speicher."); return 503; }
  snprintf(a->kind, sizeof(a->kind), "restore");
  snprintf(a->backup, sizeof(a->backup), "%s", dir);
  snprintf(a->uid, sizeof(a->uid), "%s", uid);
  snprintf(a->id, sizeof(a->id), "%s", title);
  return job_launch(restore_main, a, "restore", err, err_len);
}

int
ps5tm_saves_delete_start(const char *uid, const char *id, const char *target_mount, char *err, size_t err_len) {
  if(!uid_ok(uid) || !tid_ok(id)) { snprintf(err, err_len, "Benutzer oder Titel sind falsch angegeben."); return 400; }
  char gid[16];
  if(game_running(gid, sizeof(gid))) {
    snprintf(err, err_len, "Es läuft ein Spiel (%s). Spielstände werden nur gelöscht, wenn kein Spiel läuft, auch kein "
             "pausiertes: Es könnte seinen Spielstand offen halten. Beende das Spiel zuerst.", gid);
    return 409;
  }
  if(other_job_active(err, err_len)) return 409;
  if(!user_exists(uid)) { snprintf(err, err_len, "Diesen Benutzer gibt es an der Konsole nicht."); return 404; }
  int area = title_find(uid, id);
  if(area < 0) { snprintf(err, err_len, "Diesen Titel gibt es für diesen Benutzer nicht."); return 404; }
  char base[64] = "";
  if(target_mount && target_mount[0]) {
    drive_t dr;
    if(drive_resolve(target_mount, &dr) != 0) { snprintf(err, err_len, "Dieses Ziel gibt es an der Konsole gerade nicht."); return 404; }
    snprintf(base, sizeof(base), "%s", dr.base);
  }
  job_args_t *a = calloc(1, sizeof(*a));
  if(!a) { snprintf(err, err_len, "Kein Speicher."); return 503; }
  snprintf(a->kind, sizeof(a->kind), "delete");
  snprintf(a->base, sizeof(a->base), "%s", base);
  snprintf(a->uid, sizeof(a->uid), "%s", uid);
  snprintf(a->id, sizeof(a->id), "%s", id);
  a->sel = calloc(1, sizeof(*a->sel));
  if(!a->sel) { args_free(a); snprintf(err, err_len, "Kein Speicher."); return 503; }
  a->sel[0].area = area;
  a->nsel = 1;
  return job_launch(delete_main, a, "delete", err, err_len);
}

int
ps5tm_saves_busy(void) {
  pthread_mutex_lock(&g_lock);
  int active = job_active_locked();
  pthread_mutex_unlock(&g_lock);
  return active;
}

void
ps5tm_saves_cancel(void) {
  pthread_mutex_lock(&g_lock);
  int active = job_active_locked();
  pthread_mutex_unlock(&g_lock);
  if(active && !__atomic_load_n(&g_nocancel, __ATOMIC_ACQUIRE))
    __atomic_store_n(&g_cancel, 1, __ATOMIC_RELEASE);
}


/* ------------------------------------------------------------------ JSON */

cJSON *
ps5tm_saves_job_json(void) {
  job_t j;
  pthread_mutex_lock(&g_lock);
  j = g_job;
  pthread_mutex_unlock(&g_lock);

  cJSON *o = cJSON_CreateObject();
  cJSON_AddBoolToObject(o, "ok", 1);
  cJSON_AddStringToObject(o, "state", k_state[j.state]);
  int active = j.state >= J_SCAN && j.state <= J_APPLY;
  cJSON_AddBoolToObject(o, "active", active);
  /* From the replacing of the first file to the end, a cancel is not taken (restore_main):
     the page hides its button then instead of showing one that does nothing. */
  cJSON_AddBoolToObject(o, "can_cancel", active && !__atomic_load_n(&g_nocancel, __ATOMIC_ACQUIRE));
  cJSON_AddStringToObject(o, "kind", j.kind);
  cJSON_AddStringToObject(o, "phase", j.phase);
  double pct = j.work_total ? 100.0 * (double)j.work_done / (double)j.work_total : 0.0;
  if(pct > 100.0) pct = 100.0;
  if(j.state == J_DONE) pct = 100.0;
  cJSON_AddNumberToObject(o, "percent", (double)(int)pct);
  cJSON_AddNumberToObject(o, "files_done", j.files_done);
  cJSON_AddNumberToObject(o, "files_total", j.files_total);
  cJSON_AddNumberToObject(o, "bytes_total", (double)j.bytes_total);
  cJSON_AddStringToObject(o, "current", j.current);
  cJSON_AddStringToObject(o, "error", j.error);
  cJSON_AddStringToObject(o, "note", j.note);
  cJSON_AddStringToObject(o, "path", j.path);
  cJSON_AddStringToObject(o, "undo", j.undo);
  cJSON_AddNumberToObject(o, "ok_files", j.ok_files);
  cJSON_AddNumberToObject(o, "bad_files", j.bad_files);
  uint64_t now = ps5tm_mono_ms();
  uint64_t end = j.finished_ms ? j.finished_ms : now;
  cJSON_AddNumberToObject(o, "elapsed_s", j.started_ms ? (double)((end - j.started_ms) / 1000) : 0);
  /* -1: nothing has ended yet. The page shows a result for a quarter of an hour, not for ever. */
  cJSON_AddNumberToObject(o, "finished_ago_s", j.finished_ms ? (double)((now - j.finished_ms) / 1000) : -1);
  return o;
}

static void
measure_title(const char *uid, int area, const char *id, uint64_t *bytes, unsigned *files, int64_t *mtime) {
  char data[PATH_MAX], meta[PATH_MAX];
  title_dirs(uid, area, id, data, sizeof(data), meta, sizeof(meta));
  flist_t l = { 0 };
  char rel[SV_REL_MAX];
  snprintf(rel, sizeof(rel), "x");
  add_tree(&l, data, rel);
  add_tree(&l, meta, rel);
  *bytes = l.bytes;
  *files = (unsigned)l.n;
  *mtime = 0;
  /* newest change: stat the data files again — a list of the names alone has no times */
  char abs[PATH_MAX];
  DIR *d = opendir(data);
  if(d) {
    struct dirent *e;
    while((e = readdir(d)) != NULL) {
      if(e->d_name[0] == '.') continue;
      struct stat st;
      snprintf(abs, sizeof(abs), "%s/%s", data, e->d_name);
      if(stat(abs, &st) == 0 && S_ISREG(st.st_mode) && (int64_t)st.st_mtime > *mtime) *mtime = (int64_t)st.st_mtime;
    }
    closedir(d);
  }
  free(l.v);
}

/* Backup folders are named by their UTC stamp: the larger name is the newer one. */
typedef struct { char name[64]; unsigned d; } bname_t;

static int
cmp_bname_newest_first(const void *x, const void *y) {
  const bname_t *a = x, *b = y;
  int c = strcmp(b->name, a->name);
  return c ? c : (a->d < b->d ? -1 : a->d > b->d ? 1 : 0);
}

/* Newest first. */
static int
cmp_backups_by_created(const void *x, const void *y) {
  cJSON *ca = cJSON_GetObjectItem(*(cJSON *const *)x, "created");
  cJSON *cb = cJSON_GetObjectItem(*(cJSON *const *)y, "created");
  double a = cJSON_IsNumber(ca) ? ca->valuedouble : 0, b = cJSON_IsNumber(cb) ? cb->valuedouble : 0;
  return a < b ? 1 : a > b ? -1 : 0;
}

cJSON *
ps5tm_saves_json(void) {
  cJSON *root = cJSON_CreateObject();
  if(!root) return NULL;
  cJSON_AddBoolToObject(root, "ok", 1);
  cJSON_AddNumberToObject(root, "now", (double)time(NULL));

  char gid[16];
  int running = game_running(gid, sizeof(gid));
  cJSON_AddBoolToObject(root, "game_running", running);
  cJSON_AddStringToObject(root, "game_id", running ? gid : "");
  char mine[128];
  console_id(mine, sizeof(mine));
  cJSON_AddStringToObject(root, "console", mine);

  cJSON *names = NULL;
  names_load(&names);

  /* what the console has */
  cJSON *users = cJSON_AddArrayToObject(root, "users");
  char uids[SV_MAX_USERS][9];
  unsigned nu = users_all(uids, SV_MAX_USERS);
  for(unsigned u = 0; u < nu; u++) {
    cJSON *uj = cJSON_CreateObject();
    char nm[64];
    user_name(uids[u], nm, sizeof(nm));
    cJSON_AddStringToObject(uj, "uid", uids[u]);
    cJSON_AddStringToObject(uj, "name", nm);
    cJSON *tl = cJSON_AddArrayToObject(uj, "titles");
    tsel_t *sel = calloc(SV_MAX_TITLES, sizeof(*sel));
    unsigned nt = sel ? titles_of(uids[u], sel, SV_MAX_TITLES) : 0;
    uint64_t user_bytes = 0;
    for(unsigned i = 0; i < nt; i++) {
      cJSON *tj = cJSON_CreateObject();
      uint64_t bytes; unsigned files; int64_t mt;
      measure_title(sel[i].uid, sel[i].area, sel[i].id, &bytes, &files, &mt);
      const char *nmz = name_of(names, sel[i].id);
      char shown[160];
      name_shown(names, sel[i].id, shown, sizeof(shown));
      cJSON_AddStringToObject(tj, "id", sel[i].id);
      cJSON_AddStringToObject(tj, "name", shown);
      cJSON_AddBoolToObject(tj, "installed", nmz[0] != 0);
      cJSON_AddStringToObject(tj, "platform", k_area[sel[i].area].plat);
      cJSON_AddStringToObject(tj, "kind", title_kind(sel[i].id));
      cJSON_AddNumberToObject(tj, "bytes", (double)bytes);
      cJSON_AddNumberToObject(tj, "files", files);
      cJSON_AddNumberToObject(tj, "mtime", (double)mt);
      cJSON_AddItemToArray(tl, tj);
      user_bytes += bytes;
    }
    free(sel);
    cJSON_AddNumberToObject(uj, "bytes", (double)user_bytes);
    cJSON_AddItemToArray(users, uj);
  }

  /* where a backup can go, and what already is there: the newest SV_MAX_BACKUPS complete ones.
     The folders are named by their UTC stamp, so sorting the names sorts by time; the manifests, which
     can be megabytes, are read for those alone. Folders that are not complete (the mark is still in them:
     a power cut in the middle of a backup) are only counted. */
  cJSON *drives = cJSON_AddArrayToObject(root, "drives");
  drive_t dr[16];
  unsigned nd = drives_get(dr, 16);
  bname_t *bn = malloc(SV_NAMES_MAX * sizeof(*bn));
  unsigned nbn = 0, unfinished = 0;
  for(unsigned d = 0; d < nd; d++) {
    cJSON *dj = cJSON_CreateObject();
    cJSON_AddStringToObject(dj, "mount", dr[d].mount);
    cJSON_AddStringToObject(dj, "label", dr[d].label);
    cJSON_AddNumberToObject(dj, "free_bytes", (double)dr[d].free_bytes);
    cJSON_AddItemToArray(drives, dj);

    char rootdir[PATH_MAX];
    snprintf(rootdir, sizeof(rootdir), "%s/%s", dr[d].base, SV_DIR);
    DIR *dp = opendir(rootdir);
    if(!dp) continue;
    struct dirent *e;
    while(bn && (e = readdir(dp)) != NULL && nbn < SV_NAMES_MAX) {
      if(e->d_name[0] == '.' || strlen(e->d_name) >= sizeof(bn[0].name)) continue;
      snprintf(bn[nbn].name, sizeof(bn[nbn].name), "%s", e->d_name);
      bn[nbn].d = d;
      nbn++;
    }
    closedir(dp);
  }
  if(bn) qsort(bn, nbn, sizeof(*bn), cmp_bname_newest_first);
  cJSON *found[SV_MAX_BACKUPS];
  unsigned nfound = 0;
  for(unsigned i = 0; bn && i < nbn; i++) {
    char full[PATH_MAX];
    if(snprintf(full, sizeof(full), "%s/%s/%s", dr[bn[i].d].base, SV_DIR, bn[i].name) >= (int)sizeof(full)) continue;
    struct stat st;
    if(stat(full, &st) != 0 || !S_ISDIR(st.st_mode)) continue;
    if(!backup_complete(full)) { unfinished++; continue; }
    if(nfound >= SV_MAX_BACKUPS) continue;
    cJSON *m = manifest_load(full);
    if(!m) continue;
    const drive_t *dd = &dr[bn[i].d];
    cJSON *b = cJSON_CreateObject();
    cJSON *cr = cJSON_GetObjectItem(m, "created");
    cJSON_AddNumberToObject(b, "created", cJSON_IsNumber(cr) ? cr->valuedouble : 0);
    cJSON_AddStringToObject(b, "path", full);
    cJSON_AddStringToObject(b, "name", bn[i].name);
    cJSON_AddStringToObject(b, "mount", dd->mount);
    cJSON_AddStringToObject(b, "label", dd->label);
    cJSON *k = cJSON_GetObjectItem(m, "kind");
    cJSON_AddStringToObject(b, "kind", cJSON_IsString(k) ? k->valuestring : "backup");
    cJSON *by = cJSON_GetObjectItem(m, "bytes"), *fi = cJSON_GetObjectItem(m, "files");
    cJSON_AddNumberToObject(b, "bytes", cJSON_IsNumber(by) ? by->valuedouble : 0);
    cJSON_AddNumberToObject(b, "files", cJSON_IsNumber(fi) ? fi->valuedouble : 0);
    cJSON *cj = cJSON_GetObjectItem(m, "console");
    cJSON_AddBoolToObject(b, "this_console", cJSON_IsString(cj) && !strcmp(cj->valuestring, mine));
    cJSON *bu = cJSON_AddArrayToObject(b, "users");
    cJSON *x;
    cJSON_ArrayForEach(x, cJSON_GetObjectItem(m, "users")) {
      cJSON *uid = cJSON_GetObjectItem(x, "uid"), *nm = cJSON_GetObjectItem(x, "name");
      if(!cJSON_IsString(uid)) continue;
      cJSON *uj = cJSON_CreateObject();
      cJSON_AddStringToObject(uj, "uid", uid->valuestring);
      cJSON_AddStringToObject(uj, "name", cJSON_IsString(nm) ? nm->valuestring : uid->valuestring);
      cJSON_AddItemToArray(bu, uj);
    }
    cJSON *bt = cJSON_AddArrayToObject(b, "titles");
    cJSON_ArrayForEach(x, cJSON_GetObjectItem(m, "titles")) {
      cJSON *uid = cJSON_GetObjectItem(x, "uid"), *id = cJSON_GetObjectItem(x, "id");
      cJSON *nm = cJSON_GetObjectItem(x, "name"), *pl = cJSON_GetObjectItem(x, "platform");
      cJSON *tb = cJSON_GetObjectItem(x, "bytes");
      if(!cJSON_IsString(uid) || !cJSON_IsString(id) || !uid_ok(uid->valuestring) || !tid_ok(id->valuestring)) continue;
      cJSON *tj = cJSON_CreateObject();
      cJSON_AddStringToObject(tj, "uid", uid->valuestring);
      cJSON_AddStringToObject(tj, "id", id->valuestring);
      /* the name of the title as it is known now, else as it was when the backup was made */
      const char *now_name = name_of(names, id->valuestring);
      cJSON_AddStringToObject(tj, "name", now_name[0] ? now_name : cJSON_IsString(nm) ? nm->valuestring : "");
      cJSON_AddStringToObject(tj, "platform", cJSON_IsString(pl) ? pl->valuestring : "");
      cJSON_AddNumberToObject(tj, "bytes", cJSON_IsNumber(tb) ? tb->valuedouble : 0);
      cJSON_AddItemToArray(bt, tj);
    }
    cJSON_Delete(m);
    found[nfound++] = b;
  }
  free(bn);
  qsort(found, nfound, sizeof(found[0]), cmp_backups_by_created);
  cJSON *bl = cJSON_AddArrayToObject(root, "backups");
  for(unsigned i = 0; i < nfound; i++) cJSON_AddItemToArray(bl, found[i]);
  cJSON_AddNumberToObject(root, "unfinished", unfinished);

  cJSON_Delete(names);
  cJSON_AddItemToObject(root, "job", ps5tm_saves_job_json());
  return root;
}
