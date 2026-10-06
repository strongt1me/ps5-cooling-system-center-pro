/* Copying a game to another drive (1.46.0).
 *
 * What qualifies is where a title's data really lies (library.c,
 * detect_storage): a folder, or the image file ShadowMountPlus mounts it
 * from — .exfat, .ffpkg, .ffpfs, .ffpfsc. An installed game lives in the
 * system's encrypted packages and is the console's own business.
 *
 * Two destinations per drive, and the person chooses (29.09.2026):
 *
 *   homebrew  <drive>/homebrew/<folder>. One of ShadowMountPlus's scan roots,
 *             so the copy is found and mounted like the original. The same
 *             title id then lives in two places, which ShadowMountPlus
 *             advises against ("keep only one source per TITLE_ID"); the
 *             page says so before anything is copied.
 *   backup    <drive>/PS5-Sicherung/Spiele/<folder>. ShadowMountPlus also
 *             scans the roots of USB and M.2 drives, but never deeper than
 *             two levels (scan_depth=2 at most, per its README), and this is
 *             three. On the internal SSD /data is no scan root at all.
 *
 * <drive> is /data for the internal SSD — /data/homebrew is where
 * ShadowMountPlus looks there — else the drive's own mount point.
 *
 * ShadowMountPlus recognises a game folder by sce_sys/param.json. That file,
 * and param.sfo for PS4 folders, is copied last under a temporary name and
 * renamed into place at the very end, so a half-copied game is never seen.
 * An image file is written under its name plus ".ps5cc-teil", an extension
 * no scanner knows, and renamed once complete.
 * Nothing is overwritten: the destination must not exist, and every file is
 * created with O_EXCL. A failed or cancelled copy removes what it created
 * and nothing else; a folder carrying this app's unfinished-copy mark, or a
 * ".ps5cc-teil" file, left by a copy the console's power cut short, may be
 * replaced, and is deleted before the room is counted, so what it held is
 * free again. A folder with the mark AND sce_sys/param.json in place is not
 * such a leftover (those files come last): it is left alone. The original is
 * only ever read.
 *
 * A copy only ends "done" once it has read every file and byte the plan
 * counted. The original may change meanwhile (a game started from it, a
 * ShadowMountPlus move), and a copy that missed files must say so instead of
 * passing for whole — the page invites deleting the original afterwards.
 *
 * And once it has read the copy back (03.10.2026, checkfile.c): each file is
 * read again from the drive right after it was written, and its CRC-32 must
 * equal the one taken of the bytes read from the original. A file that does
 * not fails the whole copy, which removes what it wrote. The SHA-256 of every
 * file, taken on that second read, goes into "<target>.sha256" next to the
 * copy, in the format of sha256sum: on a PC, "sha256sum -c <name>.sha256" in
 * the folder that holds both checks the backup at any later time.
 *
 * While the copy runs the console is kept from going into rest mode on its
 * own (powerguard.c), and between two different drives a big file is read
 * ahead while the last piece is written (iopolicy.c). Both are ideas from PS5
 * Game Compressor; none of its code is used.
 *
 * One copy at a time, on a single thread of its own. It waits on the disks
 * almost all the time, and one thread leaves the other four of the five
 * CPUs all payloads share to the fan and the web server. A conversion
 * (gameconvert.c) or a ShadowMountPlus move or unpack does not run beside it:
 * they would share the drives' room, and a move deletes what a copy reads. */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <unistd.h>
#ifndef PS5TM_HOST_TEST
#include <sys/mount.h>
#endif

#include "checkfile.h"
#include "ioerr.h"
#include "iopolicy.h"
#include "ps5tm.h"
#include "third_party/cJSON.h"
#include "third_party/libdeflate/libdeflate.h"

#define COPY_BUF_BYTES   (1u << 20)
/* Reading ahead pays for a big file; a thread per small one would cost more
   than it saves. */
#define PREFETCH_MIN     (8ull << 20)
#define PREFETCH_BUFS    4
#define SPACE_RESERVE    (256ull * 1024 * 1024)   /* never fill a drive up  */
#define FAT32_MAX_FILE   0xFFFFFFFFull            /* 4 GiB less one byte    */
#define BACKUP_DIR       "PS5-Sicherung/Spiele"
#define UNFINISHED_MARK  ".ps5cc-kopie-unfertig"
#define PART_SUFFIX      ".ps5cc-teil"
#define MAX_DEPTH        24
#define PLAN_TTL_MS      60000
#define MOVE_TTL_MS      600000    /* gamemove.c: plan -> start, see below   */

/* What errno_text() is given besides plain errno values. */
#define E_SOURCE_CHANGED 1000      /* the copy found less than the plan counted */
#define E_MARK_STUCK     1001      /* the unfinished-copy mark would not go     */
#define E_VERIFY_FAILED  1002      /* a file read back differs from the one read */
#define SRC_SIDE         0x10000   /* or'ed onto an errno the ORIGINAL caused
                                      (a read, a lookup), not the target       */

/* What ShadowMountPlus looks for; copied last. Relative to the game folder. */
static const char *const k_deferred[] = { "sce_sys/param.json", "sce_sys/param.sfo" };

enum { JOB_IDLE, JOB_SCANNING, JOB_COPYING, JOB_FINISHING,
       JOB_DONE, JOB_FAILED, JOB_CANCELLED };
static const char *const k_state[] = { "idle", "scanning", "copying",
                                       "finishing", "done", "failed",
                                       "cancelled" };

typedef struct {
  uint64_t bytes;
  uint64_t largest;
  unsigned files;
  unsigned dirs;
  unsigned skipped;           /* links and special files: no part of a dump */
  unsigned max_rel;           /* longest path below the folder, in bytes: the
                                 destination's own length check needs it    */
  int      err;               /* errno of the first failure, 0 = none; a
                                 failure of the original carries SRC_SIDE   */
} tree_t;

typedef struct {
  int      state;
  char     title_id[12];
  char     name[128];
  char     mode[12];
  char     source[256];
  char     target[320];
  char     drive[40];
  uint64_t total_bytes;
  uint64_t done_bytes;        /* copied                                   */
  uint64_t checked_bytes;     /* of those, read back from the target      */
  unsigned files_total;
  unsigned files_done;
  unsigned skipped;
  char     current[192];
  char     error[640];        /* room for PS5TM_IO_TEXT_MAX (ioerr.h) after a label and a path */
  char     sums[320];         /* the checksum file, once written          */
  char     note[200];         /* something that did not stop the job      */
  uint64_t started_ms;
  uint64_t finished_ms;
  uint64_t window_ms;
  uint64_t window_bytes;
  double   rate;              /* bytes per second, smoothed */
} job_t;

typedef struct {
  char title_id[12];
  char source[256];
  char base[64];
  char target[320];
  char mode[12];
} job_args_t;

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static job_t           g_job;
static int             g_cancel;               /* __atomic */

/* The size of the last folder planned, so opening the dialog twice does not
   walk a USB drive twice. Under g_lock. */
static struct {
  char     source[256];
  uint64_t at_ms;
  tree_t   tree;
} g_plan;


/* ------------------------------------------------------------ small helpers */

static int
job_active_locked(void) {
  return g_job.state == JOB_SCANNING || g_job.state == JOB_COPYING ||
         g_job.state == JOB_FINISHING;
}

static int
folder_name_ok(const char *s) {
  return s && s[0] && strcmp(s, ".") && strcmp(s, "..") && !strchr(s, '/');
}

/* The words for a failure. Which side it came from matters: "the target cannot
   be written" for an original that cannot be read sends the person looking at
   the wrong drive. */
static const char *
errno_text(int code) {
  int src = code & SRC_SIDE;
  int e   = code & ~SRC_SIDE;
  switch(e) {
    case E_SOURCE_CHANGED: return "Die Quelle hat sich während des Kopierens "
                                  "geändert.";
    case E_MARK_STUCK: return "Die Markierung der unfertigen Kopie ließ sich "
                              "nicht entfernen.";
    case E_VERIFY_FAILED: return "Beim Zurücklesen weicht die Datei auf dem "
                                 "Ziel von dem ab, was gelesen wurde. Ist das "
                                 "Ziel defekt oder der Stecker locker?";
    case ENOSPC:       return "Das Ziel ist voll.";
    case EFBIG:        return "Eine Datei ist zu groß für das Dateisystem des "
                              "Ziels (FAT32: höchstens 4 GB je Datei).";
    case EIO:
    case ENXIO:
    case ENODEV:       return ps5tm_io_strerror(e);
    case EROFS:        return "Das Ziel lässt sich nicht beschreiben.";
    case EACCES:
    case EPERM:        return src ? "Eine Datei des Spiels lässt sich nicht "
                                    "lesen."
                                  : "Das Ziel lässt sich nicht beschreiben.";
    case ENAMETOOLONG: return "Ein Dateipfad ist zu lang.";
    case EEXIST:       return "Dort liegt schon etwas mit diesem Namen.";
    case ENOENT:       return src ? "Eine Datei oder ein Ordner des Spiels "
                                    "ist verschwunden."
                                  : "Der Zielordner ist verschwunden. Ist der "
                                    "Datenträger noch angeschlossen?";
    case ELOOP:        return "Der Ordner ist zu tief verschachtelt.";
    default:           return strerror(e);
  }
}

/* Walks below path (a directory), extending and restoring it in place. root is
   the length of the game folder's own path: what lies beyond it is the path an
   entry gets below the destination too, which max_rel records. */
static void
tree_scan(char *path, size_t len, size_t root, int depth, tree_t *t) {
  if(t->err) return;
  if(depth > MAX_DEPTH) { t->err = ELOOP; return; }
  DIR *d = opendir(path);
  if(!d) { t->err = SRC_SIDE | (errno ? errno : EIO); return; }
  struct dirent *e;
  int rd_errno = 0;
  while(!t->err) {
    /* readdir() returns NULL at the end and on an error alike; only errno tells them apart. A listing that broke off
       half way must not pass for a folder that ends there: the copy would lack the rest and still check out. */
    errno = 0;
    e = readdir(d);
    if(!e) { rd_errno = errno; break; }
    if(!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
    size_t n = strlen(e->d_name);
    if(len + 1 + n >= PATH_MAX) { t->err = ENAMETOOLONG; break; }
    if(len + 1 + n - root > t->max_rel) t->max_rel = (unsigned)(len + 1 + n - root);
    path[len] = '/';
    memcpy(path + len + 1, e->d_name, n + 1);
    struct stat st;
    if(lstat(path, &st) != 0) {
      t->err = SRC_SIDE | (errno ? errno : EIO);
    } else if(S_ISDIR(st.st_mode)) {
      t->dirs++;
      tree_scan(path, len + 1 + n, root, depth + 1, t);
    } else if(S_ISREG(st.st_mode)) {
      t->files++;
      t->bytes += (uint64_t)st.st_size;
      if((uint64_t)st.st_size > t->largest) t->largest = (uint64_t)st.st_size;
    } else {
      t->skipped++;
    }
    path[len] = 0;
  }
  closedir(d);
  if(!t->err && rd_errno) t->err = SRC_SIDE | rd_errno;
}

/* A folder is walked; an image file is one file. */
static tree_t
measure(const char *source) {
  tree_t t;
  memset(&t, 0, sizeof(t));
  struct stat st;
  if(lstat(source, &st) != 0) {
    t.err = SRC_SIDE | (errno ? errno : EIO);
  } else if(S_ISREG(st.st_mode)) {
    t.files   = 1;
    t.bytes   = (uint64_t)st.st_size;
    t.largest = (uint64_t)st.st_size;
  } else if(S_ISDIR(st.st_mode)) {
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s", source);
    size_t len = strlen(path);
    tree_scan(path, len, len, 0, &t);
  } else {
    t.err = SRC_SIDE | EINVAL;
  }
  return t;
}

static int
is_file(const char *path) {
  struct stat st;
  return lstat(path, &st) == 0 && S_ISREG(st.st_mode);
}

/* measure(), except that a folder measured within ttl_ms is not walked again:
   opening a dialog twice, or starting a move right after its plan, must not
   cost a USB drive a second walk. Only the plan side uses this — the copy
   itself measures afresh, because the folder may have changed since. */
static tree_t
measure_cached(const char *source, uint64_t ttl_ms) {
  tree_t t;
  pthread_mutex_lock(&g_lock);
  if(!strcmp(g_plan.source, source) &&
     ps5tm_mono_ms() - g_plan.at_ms < ttl_ms) {
    t = g_plan.tree;
    pthread_mutex_unlock(&g_lock);
    return t;
  }
  pthread_mutex_unlock(&g_lock);

  t = measure(source);
  if(!t.err) {
    pthread_mutex_lock(&g_lock);
    snprintf(g_plan.source, sizeof(g_plan.source), "%s", source);
    g_plan.at_ms = ps5tm_mono_ms();
    g_plan.tree  = t;
    pthread_mutex_unlock(&g_lock);
  }
  return t;
}

/* For gamemove.c's plan and its re-check at the start. 0 on success. The move
   dialog can stay open for minutes before "start" is pressed, and the size of
   a game folder does not move that fast (ShadowMountPlus checks again itself),
   so the start may use what the plan measured up to MOVE_TTL_MS ago instead of
   walking a USB drive again with the page waiting. */
int
ps5tm_gamecopy_measure(const char *path, uint64_t *bytes, uint64_t *largest) {
  tree_t t = measure_cached(path, MOVE_TTL_MS);
  if(t.err) return -1;
  if(bytes)   *bytes   = t.bytes;
  if(largest) *largest = t.largest;
  return 0;
}

/* Where a destination hangs off: /data for the internal SSD, where
   ShadowMountPlus looks, else the drive's own mount point. The drive must be
   one the console has right now. */
static int
drive_base(const char *mount, char *base, size_t base_len,
           char *label, size_t label_len) {
  ps5tm_sysinfo_t info;
  ps5tm_sysinfo_get(&info);
  for(unsigned i = 0; i < info.volume_count; i++) {
    if(strcmp(info.volumes[i].path, mount)) continue;
    snprintf(base, base_len, "%s", !strcmp(mount, "/user") ? "/data" : mount);
    snprintf(label, label_len, "%s", info.volumes[i].label);
    return 0;
  }
  return -1;
}

static int
dest_path(const char *base, const char *mode, const char *folder,
          char *out, size_t out_len) {
  const char *sub = !strcmp(mode, "homebrew") ? "homebrew"
                  : !strcmp(mode, "backup")   ? BACKUP_DIR : NULL;
  if(!sub) return -1;
  int n = snprintf(out, out_len, "%s/%s/%s", base, sub, folder);
  return (n > 0 && (size_t)n < out_len) ? 0 : -1;
}

static int
drive_space(const char *base, uint64_t *avail, char *fstype, size_t fstype_len) {
  struct statvfs sv;
  if(statvfs(base, &sv) != 0) return -1;
  uint64_t fr = sv.f_frsize ? sv.f_frsize : sv.f_bsize;
  *avail = (uint64_t)sv.f_bavail * fr;
  fstype[0] = 0;
#ifndef PS5TM_HOST_TEST
  struct statfs sf;
  if(statfs(base, &sf) == 0) snprintf(fstype, fstype_len, "%s", sf.f_fstypename);
#endif
  return 0;
}

/* 0 free, 1 taken, 2 a folder with this app's unfinished-copy mark, 3 free
   but for this app's unfinished single-file copy beside it.
   The mark alone is not enough for 2: param.json and param.sfo are the LAST
   files a copy puts in place, after it has removed the mark, so a folder that
   has one of them under its final name is a finished game that merely still
   wears the mark (the mark could not be removed). That is somebody's game,
   and never a leftover to wipe. */
static int
dest_state(const char *target) {
  struct stat st;
  char aux[PATH_MAX];
  if(lstat(target, &st) == 0) {
    if(!S_ISDIR(st.st_mode)) return 1;
    snprintf(aux, sizeof(aux), "%s/" UNFINISHED_MARK, target);
    if(lstat(aux, &st) != 0) return 1;
    for(size_t i = 0; i < sizeof(k_deferred) / sizeof(k_deferred[0]); i++) {
      snprintf(aux, sizeof(aux), "%s/%s", target, k_deferred[i]);
      if(lstat(aux, &st) == 0) return 1;
    }
    return 2;
  }
  snprintf(aux, sizeof(aux), "%s" PART_SUFFIX, target);
  return lstat(aux, &st) == 0 ? 3 : 0;
}

/* The room this app's own leftover at target gives back once it is deleted
   (ds is dest_state()'s answer); 0 for anything that is not ours to delete. */
static uint64_t
leftover_bytes(const char *target, int ds) {
  if(ds == 2) {
    tree_t t = measure(target);
    return t.err ? 0 : t.bytes;
  }
  if(ds == 3) {
    char part[PATH_MAX];
    struct stat st;
    snprintf(part, sizeof(part), "%s" PART_SUFFIX, target);
    if(lstat(part, &st) == 0 && S_ISREG(st.st_mode)) return (uint64_t)st.st_size;
  }
  return 0;
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

/* Removes a folder this job made, and nothing else: it is only ever called
   on the destination created here (mkdir fails if it existed) or on one
   carrying this app's unfinished-copy mark. Depth first, never following a
   link — everything is looked at with lstat, and a link is unlinked, not
   entered. Deleting while reading a directory can skip entries, hence the
   passes. */
static void
remove_tree(char *path, size_t len, int depth) {
  if(depth > MAX_DEPTH + 2) return;
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
        if(S_ISDIR(st.st_mode)) remove_tree(path, len + 1 + n, depth + 1);
        else unlink(path);
      }
      path[len] = 0;
    }
    closedir(d);
    if(rmdir(path) == 0) return;
  }
}

static void
remove_own(const char *target) {
  char path[PATH_MAX];
  snprintf(path, sizeof(path), "%s", target);
  remove_tree(path, strlen(path), 0);
}


/* ---------------------------------------------------------------- progress */

/* All of this job's timing is differences (the speed window, the time it has
   run), so it runs on the monotonic clock: the wall clock steps when the
   console syncs its time, and the difference across a step is garbage. */
/* The work of a copy is two passes over every byte: written, then read back.
   Both count towards the speed, the percentage and the time left; done_bytes
   alone says how much has been copied. */
static void
job_work(uint64_t n, int read_back) {
  uint64_t now = ps5tm_mono_ms();
  pthread_mutex_lock(&g_lock);
  if(read_back) g_job.checked_bytes += n;
  else          g_job.done_bytes    += n;
  uint64_t work = g_job.done_bytes + g_job.checked_bytes;
  if(!g_job.window_ms) {
    g_job.window_ms    = now;
    g_job.window_bytes = work;
  } else if(now - g_job.window_ms >= 1000) {
    double inst = (double)(work - g_job.window_bytes) * 1000.0 /
                  (double)(now - g_job.window_ms);
    g_job.rate = g_job.rate > 0 ? g_job.rate * 0.7 + inst * 0.3 : inst;
    g_job.window_ms    = now;
    g_job.window_bytes = work;
  }
  pthread_mutex_unlock(&g_lock);
}

static void
job_progress(uint64_t n) {
  job_work(n, 0);
}

/* The read-back reports how far it has come in ONE file; this turns that into
   the bytes since the last report. */
typedef struct { uint64_t last; } check_acc_t;

static void
job_checked_cb(void *ctx, uint64_t done) {
  check_acc_t *a = ctx;
  job_work(done - a->last, 1);
  a->last = done;
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

static void
job_state(int state) {
  pthread_mutex_lock(&g_lock);
  g_job.state = state;
  if(state >= JOB_DONE) g_job.finished_ms = ps5tm_mono_ms();
  pthread_mutex_unlock(&g_lock);
}

/* Whether the copy has read everything the plan counted: every file, and at
   least every byte (a file that grew is no loss; one that vanished or shrank
   is). Under g_lock, because the worker updates the counters as it goes. */
static int
plan_complete(unsigned *files_done, unsigned *files_total,
              uint64_t *done_bytes, uint64_t *total_bytes) {
  pthread_mutex_lock(&g_lock);
  *files_done  = g_job.files_done;
  *files_total = g_job.files_total;
  *done_bytes  = g_job.done_bytes;
  *total_bytes = g_job.total_bytes;
  pthread_mutex_unlock(&g_lock);
  return *files_done >= *files_total && *done_bytes >= *total_bytes;
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
  job_state(JOB_FAILED);
}

static int
cancelled(void) {
  return __atomic_load_n(&g_cancel, __ATOMIC_ACQUIRE);
}

/* The cancel came before this job had created or deleted anything, so there
   is nothing to remove. Ends the job; the caller returns what this returns. */
static void *
cancelled_early(const job_args_t *a, char *buf) {
  free(buf);
  job_state(JOB_CANCELLED);
  PS5TM_INFO("game_copy_cancelled", "Kopie von %s abgebrochen, bevor etwas "
             "geschrieben wurde.", a->title_id);
  return NULL;
}


/* -------------------------------------------------------------- the copying */

/* What the files of one copy share: the checksum file they report into (NULL
   when it could not be made), what its names start with, and whether source
   and target are two different drives. */
typedef struct {
  ps5tm_sums_t *sums;
  const char   *prefix;
  int           parallel;
} cp_ctx_t;

typedef struct { int fd; } fd_src_t;

static ssize_t
fd_read(void *ctx, void *buf, size_t len) {
  fd_src_t *f = ctx;
  for(;;) {
    ssize_t r = read(f->fd, buf, len);
    if(r >= 0 || errno != EINTR) return r;
  }
}

/* The file just written, read back from the drive: its CRC-32 must equal the
   one taken of what was read from the original, and its SHA-256 goes into the
   checksum file under `name`. A read that fails is the target's fault, a
   different CRC is E_VERIFY_FAILED. */
static int
verify_written(const char *dst, uint64_t size, uint32_t want_crc,
               const cp_ctx_t *cx, const char *name) {
  int fd = open(dst, O_RDONLY);
  if(fd < 0) return errno ? errno : EIO;
  ps5tm_drop_cache(fd);
  uint32_t got = 0;
  uint8_t  sha[32];
  char     why[96];
  check_acc_t acc = { 0 };
  int rc = ps5tm_digest_fd(fd, size, &got, sha, &g_cancel, job_checked_cb, &acc, why, sizeof(why));
  int e  = errno;
  close(fd);
  if(rc) return e == ECANCELED ? ECANCELED : (e ? e : EIO);
  if(got != want_crc) return E_VERIFY_FAILED;
  if(cx->sums && name && ps5tm_sums_add(cx->sums, sha, name) != 0) {
    /* The line could not be written; the close of the checksum file reports
       that, and the copy itself is good. */
  }
  return 0;
}

static int
copy_file(const char *src, const char *dst, char *buf, const cp_ctx_t *cx,
          const char *name) {
  int in = open(src, O_RDONLY);
  if(in < 0) return SRC_SIDE | (errno ? errno : EIO);
  struct stat st;
  uint64_t hint = fstat(in, &st) == 0 ? (uint64_t)st.st_size : 0;
  int out = open(dst, O_WRONLY | O_CREAT | O_EXCL, 0666);
  if(out < 0) {
    int e = errno ? errno : EIO;
    close(in);
    return e;
  }
  /* Between two drives a big file is read ahead on a thread of its own while
     the last piece is written; with no room for that thread it is read as
     before. */
  fd_src_t          fs = { in };
  ps5tm_prefetch_t *pf = NULL;
  if(cx->parallel && hint >= PREFETCH_MIN)
    pf = ps5tm_prefetch_start(fd_read, &fs, COPY_BUF_BYTES, PREFETCH_BUFS);
  int rc = 0;
  uint32_t crc = 0;
  uint64_t total = 0;
  for(;;) {
    if(cancelled()) { rc = ECANCELED; break; }
    ssize_t n = pf ? ps5tm_prefetch_read(pf, buf, COPY_BUF_BYTES)
                   : read(in, buf, COPY_BUF_BYTES);
    if(n < 0) {
      if(!pf && errno == EINTR) continue;
      rc = SRC_SIDE | (errno ? errno : EIO);
      break;
    }
    if(n == 0) break;
    crc = libdeflate_crc32(crc, buf, (size_t)n);
    for(ssize_t off = 0; off < n; ) {
      ssize_t w = write(out, buf + off, (size_t)(n - off));
      if(w < 0) {
        if(errno == EINTR) continue;
        rc = errno ? errno : EIO;
        break;
      }
      if(w == 0) { rc = EIO; break; }
      off += w;
    }
    if(rc) break;
    total += (uint64_t)n;
    job_progress((uint64_t)n);
  }
  ps5tm_prefetch_stop(pf);             /* before the file it reads is closed */
  /* On the disk before the next file, not somewhere in a cache: a copy that
     says it is finished must survive the drive being pulled. */
  if(!rc && fsync(out) != 0) rc = errno ? errno : EIO;
  if(close(out) != 0 && !rc) rc = errno ? errno : EIO;
  close(in);
  if(!rc) rc = verify_written(dst, total, crc, cx, name);
  if(rc) unlink(dst);                  /* created above with O_EXCL: ours */
  return rc;
}

static int
is_deferred(const char *rel) {
  for(size_t i = 0; i < sizeof(k_deferred) / sizeof(k_deferred[0]); i++)
    if(!strcmp(rel, k_deferred[i])) return 1;
  return 0;
}

/* "<prefix>/<rel>" as the checksum file spells it: the folder's name and the
   path inside it, so that the file checks from the folder that holds both. */
static void
sums_name(char *out, size_t out_len, const cp_ctx_t *cx, const char *rel) {
  snprintf(out, out_len, "%s/%s", cx->prefix, rel);
}

/* Copies what lies below src into dst, which exists. `root` is the length of
   the game folder's own path, so src + root + 1 is the path inside it. On an
   error, src still names the entry that failed. */
static int
copy_tree(char *src, size_t slen, char *dst, size_t dlen, size_t root,
          int depth, char *buf, const cp_ctx_t *cx) {
  if(depth > MAX_DEPTH) return ELOOP;
  DIR *d = opendir(src);
  if(!d) return SRC_SIDE | (errno ? errno : EIO);
  int rc = 0, rd_errno = 0;
  struct dirent *e;
  while(!rc) {
    errno = 0;                                   /* see tree_scan: an error ends the listing like the end does */
    e = readdir(d);
    if(!e) { rd_errno = errno; break; }
    if(!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
    if(cancelled()) { rc = ECANCELED; break; }
    size_t n = strlen(e->d_name);
    if(slen + 1 + n >= PATH_MAX || dlen + 1 + n >= PATH_MAX) {
      rc = ENAMETOOLONG;
      break;
    }
    src[slen] = '/';
    memcpy(src + slen + 1, e->d_name, n + 1);
    dst[dlen] = '/';
    memcpy(dst + dlen + 1, e->d_name, n + 1);

    struct stat st;
    if(lstat(src, &st) != 0) {
      rc = SRC_SIDE | (errno ? errno : EIO);
    } else if(S_ISDIR(st.st_mode)) {
      if(mkdir(dst, 0777) != 0) rc = errno ? errno : EIO;
      else rc = copy_tree(src, slen + 1 + n, dst, dlen + 1 + n, root,
                          depth + 1, buf, cx);
    } else if(S_ISREG(st.st_mode) && !is_deferred(src + root + 1)) {
      char name[PATH_MAX + 256];
      sums_name(name, sizeof(name), cx, src + root + 1);
      job_current(src + root + 1);
      rc = copy_file(src, dst, buf, cx, name);
      if(!rc) job_file_done();
    }
    /* Links and special files are no part of a game dump; left out. */

    if(!rc) {
      src[slen] = 0;
      dst[dlen] = 0;
    }
  }
  closedir(d);
  if(!rc && rd_errno) {
    rc = SRC_SIDE | rd_errno;
    src[slen] = 0;
    dst[dlen] = 0;
  }
  return rc;
}

static void *copy_run(void *arg);

/* The console stays awake for as long as the copy runs, whichever way it ends
   (powerguard.c). */
static void *
copy_thread(void *arg) {
  ps5tm_powerguard_hold();
  void *r = copy_run(arg);
  ps5tm_powerguard_release();
  return r;
}

static void *
copy_run(void *arg) {
  job_args_t a = *(job_args_t *)arg;
  free(arg);

  char *buf = malloc(COPY_BUF_BYTES);
  if(!buf) {
    job_fail("Kein Speicher für den Kopierpuffer.");
    return NULL;
  }

  /* Measured again here, not taken from the dialog: the folder may have
     changed since, and the space check has to hold for what is copied. */
  tree_t t = measure(a.source);
  if(t.err) {
    job_fail("Die Spieldaten ließen sich nicht lesen: %s", errno_text(t.err));
    free(buf);
    return NULL;
  }
  /* The longest path inside the game, below THIS destination: it has to fit
     before anything is written, not turn up half way through the copy. */
  if(strlen(a.target) + 1 + t.max_rel >= PATH_MAX) {
    job_fail("Ein Dateipfad ist am Ziel zu lang: Er käme auf %zu Zeichen, "
             "erlaubt sind %d.", strlen(a.target) + 1 + t.max_rel,
             PATH_MAX - 1);
    free(buf);
    return NULL;
  }
  /* A cancel that came while measuring has touched nothing, and must not
     begin by deleting an old leftover either. */
  if(cancelled()) return cancelled_early(&a, buf);

  int  single = is_file(a.source);
  char part[PATH_MAX];
  snprintf(part, sizeof(part), "%s" PART_SUFFIX, a.target);

  /* This app's own leftover from an earlier try goes FIRST, so that the room
     it held is free when the room is measured next. Only what carries the
     mark or the suffix is ever deleted (dest_state). */
  int ds = dest_state(a.target);
  if(ds == 1) {
    job_fail("Am Ziel gibt es %s schon.", a.target);
    free(buf);
    return NULL;
  }
  if(ds == 2) {
    PS5TM_INFO("game_copy_replace", "Unfertige Kopie in %s wird ersetzt.", a.target);
    remove_own(a.target);
  }
  if(ds == 3) unlink(part);             /* this app's own unfinished file */

  uint64_t avail = 0;
  char fstype[16];
  if(drive_space(a.base, &avail, fstype, sizeof(fstype)) != 0) {
    job_fail("Das Ziel %s ist nicht erreichbar.", a.base);
    free(buf);
    return NULL;
  }
  if(!strcmp(fstype, "msdosfs") && t.largest > FAT32_MAX_FILE) {
    job_fail("Das Ziel ist mit FAT32 formatiert und nimmt keine Datei über "
             "4 GB auf. Dieses Spiel hat größere.");
    free(buf);
    return NULL;
  }
  if(avail < t.bytes + SPACE_RESERVE) {
    job_fail("Auf dem Ziel ist nicht genug Platz.");
    free(buf);
    return NULL;
  }
  if(cancelled()) return cancelled_early(&a, buf);   /* nothing created yet */

  char parent[PATH_MAX];
  snprintf(parent, sizeof(parent), "%s", a.target);
  char *slash = strrchr(parent, '/');
  if(slash) *slash = 0;
  int rc = mkdir_p(parent);
  if(!rc && !single && mkdir(a.target, 0777) != 0) rc = errno ? errno : EIO;
  if(rc) {
    job_fail("Der Zielordner ließ sich nicht anlegen: %s", errno_text(rc));
    free(buf);
    return NULL;
  }

  pthread_mutex_lock(&g_lock);
  g_job.total_bytes = t.bytes;
  g_job.files_total = t.files;
  g_job.skipped     = t.skipped;
  g_job.state       = JOB_COPYING;
  pthread_mutex_unlock(&g_lock);

  /* The checksum file, "<target>.sha256", next to the copy; it gets its name
     only when the copy is done. The copy does not depend on it: if it cannot
     be made, the copy is still read back and checked, and the page says so. */
  const char *tname = strrchr(a.target, '/');
  tname = tname ? tname + 1 : a.target;
  char sums_path[PATH_MAX + 16], serr[400] = "";
  ps5tm_sums_t *sums = NULL;
  if(snprintf(sums_path, sizeof(sums_path), "%s" PS5TM_SUMS_EXT, a.target)
     < (int)sizeof(sums_path))
    sums = ps5tm_sums_open(sums_path, serr, sizeof(serr));
  if(!sums) {
    pthread_mutex_lock(&g_lock);
    snprintf(g_job.note, sizeof(g_job.note), "Ohne Prüfsummen-Datei: %s",
             serr[0] ? serr : "der Pfad ist zu lang.");
    pthread_mutex_unlock(&g_lock);
  }
  char why_io[96] = "";
  cp_ctx_t cx = { sums, tname, ps5tm_io_parallel(a.source, a.base, why_io, sizeof(why_io)) };
  PS5TM_INFO("game_copy_io", "Kopie von %s: %s.", a.title_id, why_io);

  char where[192] = "";
  /* What the plan counted against what the copy read, filled in by
     plan_complete() just before the copy would be declared whole. */
  unsigned fdone = 0, ftotal = 0;
  uint64_t bdone = 0, btotal = 0;
  int      mark_errno = 0;
  if(single) {
    /* An image: one file under a name no scanner knows, renamed once whole.
       rename() would replace a file that appeared meanwhile, hence the look
       first. */
    const char *name = strrchr(a.source, '/');
    name = name ? name + 1 : a.source;
    snprintf(where, sizeof(where), "%s", name);
    job_current(name);
    rc = copy_file(a.source, part, buf, &cx, tname);
    if(!rc) {
      job_file_done();
      if(!plan_complete(&fdone, &ftotal, &bdone, &btotal)) {
        rc = E_SOURCE_CHANGED;          /* an image that shrank meanwhile */
      } else {
        struct stat st;
        if(lstat(a.target, &st) == 0)          rc = EEXIST;
        else if(rename(part, a.target) != 0)   rc = errno ? errno : EIO;
      }
      if(rc) unlink(part);              /* made by copy_file with O_EXCL: ours */
    }
  } else {
    char mark[PATH_MAX];
    snprintf(mark, sizeof(mark), "%s/" UNFINISHED_MARK, a.target);
    int mfd = open(mark, O_WRONLY | O_CREAT | O_EXCL, 0666);
    if(mfd >= 0) close(mfd);

    char src[PATH_MAX], dst[PATH_MAX];
    snprintf(src, sizeof(src), "%s", a.source);
    snprintf(dst, sizeof(dst), "%s", a.target);
    size_t root = strlen(src);
    rc = copy_tree(src, root, dst, strlen(dst), root, 0, buf, &cx);

    /* On an error src still names what failed (copy_tree leaves it there). */
    snprintf(where, sizeof(where), "%s", strlen(src) > root ? src + root + 1 : "");

    /* The files ShadowMountPlus looks for, last and under a temporary name. */
    char tmp[PATH_MAX], fin[PATH_MAX];
    int  have[2] = { 0, 0 };
    if(!rc) {
      job_state(JOB_FINISHING);
      for(int i = 0; i < 2 && !rc; i++) {
        snprintf(src, sizeof(src), "%s/%s", a.source, k_deferred[i]);
        struct stat st;
        if(lstat(src, &st) != 0 || !S_ISREG(st.st_mode)) continue;
        snprintf(tmp, sizeof(tmp), "%s/%s" PART_SUFFIX, a.target, k_deferred[i]);
        snprintf(where, sizeof(where), "%s", k_deferred[i]);
        job_current(k_deferred[i]);
        char name[PATH_MAX + 256];
        sums_name(name, sizeof(name), &cx, k_deferred[i]);
        rc = copy_file(src, tmp, buf, &cx, name);
        if(!rc) { have[i] = 1; job_file_done(); }
      }
    }
    /* Every file and byte the plan counted must have come across. The tree is
       read live, so a file that vanished, shrank or was never listed (a
       directory read while something deletes from it skips entries without an
       error) would otherwise end as "done" — and the page then invites the
       person to delete the original. */
    if(!rc && !plan_complete(&fdone, &ftotal, &bdone, &btotal))
      rc = E_SOURCE_CHANGED;

    /* The mark goes before the renames: a folder that has lost it but not
       yet gained param.json is merely incomplete, whereas a complete copy
       still wearing the mark would look like a leftover. If it will not go
       (ENOENT is fine: it was never made), the copy is not declared done and
       param.json is not put in place — without it the folder is no game, and
       it is plainly this app's own leftover. */
    if(!rc) {
      if(unlink(mark) != 0 && errno != ENOENT) {
        mark_errno = errno ? errno : EIO;
        rc = E_MARK_STUCK;
        snprintf(where, sizeof(where), "%s", UNFINISHED_MARK);
      }
      for(int i = 0; i < 2 && !rc; i++) {
        if(!have[i]) continue;
        snprintf(tmp, sizeof(tmp), "%s/%s" PART_SUFFIX, a.target, k_deferred[i]);
        snprintf(fin, sizeof(fin), "%s/%s", a.target, k_deferred[i]);
        snprintf(where, sizeof(where), "%s", k_deferred[i]);
        if(rename(tmp, fin) != 0) rc = errno ? errno : EIO;
      }
    }
  }
  free(buf);

  /* The checksum file gets its name only for a copy that is whole and read
     back; for any other it is dropped. Its trouble does not touch the copy. */
  int sums_ok = 0;
  if(sums) {
    if(!rc) {
      sums_ok = ps5tm_sums_close(sums, serr, sizeof(serr)) == 0;
      pthread_mutex_lock(&g_lock);
      if(sums_ok) snprintf(g_job.sums, sizeof(g_job.sums), "%s", sums_path);
      else snprintf(g_job.note, sizeof(g_job.note), "Die Prüfsummen-Datei ließ sich nicht "
                    "schreiben. Die Kopie selbst ist zurückgelesen und in Ordnung.");
      pthread_mutex_unlock(&g_lock);
      if(!sums_ok)
        PS5TM_WARN("game_copy_sums_failed", "Prüfsummen-Datei zu %s nicht geschrieben: %s",
                   a.target, serr);
    } else {
      ps5tm_sums_abort(sums);
    }
  }

  pthread_mutex_lock(&g_lock);
  uint64_t done = g_job.done_bytes;
  uint64_t secs = (ps5tm_mono_ms() - g_job.started_ms) / 1000;
  pthread_mutex_unlock(&g_lock);

  if(!rc) {
    job_state(JOB_DONE);
    ps5tm_smp_forget();
    ps5tm_library_forget();
    PS5TM_INFO("game_copy_done", "%s nach %s kopiert und zurückgelesen: %llu MB in %llu s%s.",
               a.title_id, a.target, (unsigned long long)(done >> 20),
               (unsigned long long)secs, sums_ok ? ", Prüfsummen-Datei angelegt" : "");
    return NULL;
  }

  if(!single) remove_own(a.target);     /* the single file is gone already */
  if(rc == ECANCELED) {
    job_state(JOB_CANCELLED);
    PS5TM_INFO("game_copy_cancelled", "Kopie von %s abgebrochen, %s entfernt.",
               a.title_id, a.target);
  } else if(rc == E_SOURCE_CHANGED) {
    job_fail("%s Gelesen wurden %u von %u Dateien und %llu von %llu MB.",
             errno_text(rc), fdone, ftotal, (unsigned long long)(bdone >> 20),
             (unsigned long long)(btotal >> 20));
    PS5TM_WARN("game_copy_failed", "Kopie von %s nach %s verworfen: Die Quelle "
               "hat sich geändert (%u von %u Dateien, %llu von %llu Bytes).",
               a.title_id, a.target, fdone, ftotal,
               (unsigned long long)bdone, (unsigned long long)btotal);
  } else if(rc == E_MARK_STUCK) {
    job_fail("%s %s", errno_text(rc), errno_text(mark_errno));
    PS5TM_WARN("game_copy_failed", "Kopie von %s nach %s verworfen: Die "
               "Markierung ließ sich nicht entfernen: %s", a.title_id,
               a.target, errno_text(mark_errno));
  } else {
    job_fail("Bei %s: %s", where[0] ? where : a.target, errno_text(rc));
    PS5TM_WARN("game_copy_failed", "Kopie von %s nach %s gescheitert bei %s: %s",
               a.title_id, a.target, where[0] ? where : "-", errno_text(rc));
  }
  return NULL;
}


/* --------------------------------------------------------------- interface */

cJSON *
ps5tm_gamecopy_plan(const char *title_id, char *err, size_t err_len) {
  char name[128], source[256];
  if(ps5tm_library_copy_info(title_id, name, sizeof(name),
                             source, sizeof(source)) != 0 ||
     !strncmp(source, "/mnt/shadowmnt", 14)) {
    snprintf(err, err_len, "Dieses Spiel lässt sich nicht kopieren: Es ist "
             "normal installiert, oder die Datei, aus der ShadowMountPlus es "
             "einbindet, ist nicht bekannt.");
    return NULL;
  }
  const char *folder = strrchr(source, '/');
  folder = folder ? folder + 1 : "";
  if(!folder_name_ok(folder)) {
    snprintf(err, err_len, "Die Spieldaten haben keinen brauchbaren Namen.");
    return NULL;
  }

  int busy = ps5tm_gamecopy_busy();
  tree_t t = measure_cached(source, PLAN_TTL_MS);
  if(t.err) {
    snprintf(err, err_len, "Die Spieldaten sind nicht lesbar: %s",
             errno_text(t.err));
    return NULL;
  }

  ps5tm_gamestate_t gs;
  ps5tm_gamestate_get(&gs);

  cJSON *root = cJSON_CreateObject();
  cJSON_AddBoolToObject  (root, "ok", 1);
  cJSON_AddStringToObject(root, "title_id", title_id);
  cJSON_AddStringToObject(root, "name", name);
  cJSON_AddStringToObject(root, "source", source);
  cJSON_AddStringToObject(root, "folder", folder);
  cJSON_AddStringToObject(root, "kind", is_file(source) ? "file" : "folder");
  cJSON_AddNumberToObject(root, "size_bytes", (double)t.bytes);
  cJSON_AddNumberToObject(root, "files", t.files);
  cJSON_AddNumberToObject(root, "largest_bytes", (double)t.largest);
  cJSON_AddNumberToObject(root, "skipped", t.skipped);
  cJSON_AddBoolToObject  (root, "running",
                          gs.title_id[0] && !strcmp(gs.title_id, title_id));
  cJSON_AddBoolToObject  (root, "busy", busy);
  /* A conversion runs: no copy starts beside it (the start says so too). */
  cJSON_AddBoolToObject  (root, "other_busy", ps5tm_gameconvert_busy());

  ps5tm_sysinfo_t info;
  ps5tm_sysinfo_get(&info);
  uint64_t need = t.bytes + SPACE_RESERVE;
  cJSON *arr = cJSON_AddArrayToObject(root, "targets");
  for(unsigned i = 0; i < info.volume_count; i++) {
    const char *mount = info.volumes[i].path;
    char base[64], label[40];
    if(drive_base(mount, base, sizeof(base), label, sizeof(label)) != 0) continue;
    uint64_t avail = 0;
    char fstype[16];
    if(drive_space(base, &avail, fstype, sizeof(fstype)) != 0) continue;

    cJSON *d = cJSON_CreateObject();
    cJSON_AddStringToObject(d, "mount", mount);
    cJSON_AddStringToObject(d, "label", label);
    cJSON_AddStringToObject(d, "base", base);
    cJSON_AddStringToObject(d, "fs", fstype);
    cJSON_AddNumberToObject(d, "free_bytes", (double)avail);
    size_t bl = strlen(base);
    cJSON_AddBoolToObject  (d, "source_here",
                            !strncmp(source, base, bl) && source[bl] == '/');
    cJSON_AddBoolToObject  (d, "fat32_too_big",
                            !strcmp(fstype, "msdosfs") && t.largest > FAT32_MAX_FILE);
    /* What is promised here is what the copy will find: it deletes this
       app's own leftover first, so the room that leftover holds counts as free
       for THAT destination (enough_space inside the destination). The
       drive-wide enough_space is true when at least one of the two fits. */
    int fits_any = avail >= need;
    static const char *const modes[] = { "homebrew", "backup" };
    for(int m = 0; m < 2; m++) {
      char path[320];
      if(dest_path(base, modes[m], folder, path, sizeof(path)) != 0) continue;
      int      ds   = dest_state(path);
      uint64_t back = leftover_bytes(path, ds);
      int      fits = avail + back >= need;
      if(fits) fits_any = 1;
      cJSON *o = cJSON_AddObjectToObject(d, modes[m]);
      cJSON_AddStringToObject(o, "path", path);
      cJSON_AddBoolToObject  (o, "exists", ds == 1);
      cJSON_AddBoolToObject  (o, "unfinished", ds == 2 || ds == 3);
      cJSON_AddNumberToObject(o, "unfinished_bytes", (double)back);
      cJSON_AddBoolToObject  (o, "enough_space", fits);
      cJSON_AddBoolToObject  (o, "path_too_long",
                              strlen(path) + 1 + t.max_rel >= PATH_MAX);
    }
    cJSON_AddBoolToObject  (d, "enough_space", fits_any);
    cJSON_AddItemToArray(arr, d);
  }
  return root;
}

/* 1 while a copy is under way. For the other file jobs' starts: only one of
   copy, conversion and ShadowMountPlus move/unpack runs at a time. */
int
ps5tm_gamecopy_busy(void) {
  pthread_mutex_lock(&g_lock);
  int busy = job_active_locked();
  pthread_mutex_unlock(&g_lock);
  return busy;
}

/* The reason in err when another file job — a conversion, or a
   ShadowMountPlus move/unpack — is running; 0 when none is. No lock of this
   file is held while asking (the others take theirs: two jobs asking each
   other with their own lock held would wait on each other forever). */
static int
other_job_active(int ask_smp, char *err, size_t err_len) {
  if(ps5tm_saves_busy()) {
    snprintf(err, err_len, "Es läuft gerade ein Vorgang mit den Spielständen. Kopieren, Konvertieren "
             "und Verschieben laufen nicht gleichzeitig damit.");
    return 1;
  }
  if(ps5tm_pkgsplit_busy()) {
    snprintf(err, err_len, "Es wird gerade ein Paket geteilt. Kopieren, Konvertieren "
             "und Verschieben laufen nicht gleichzeitig damit.");
    return 1;
  }
  if(ps5tm_gamedelete_busy()) {
    snprintf(err, err_len, "Es wird gerade ein Spiel oder eine Sicherung gelöscht. Kopieren, Konvertieren "
             "und Verschieben laufen nicht gleichzeitig damit.");
    return 1;
  }
  if(ps5tm_pkginst_busy()) {
    snprintf(err, err_len, "Es wird gerade ein Paket installiert. Kopieren, Konvertieren "
             "und Verschieben laufen nicht gleichzeitig damit.");
    return 1;
  }
  if(ps5tm_gameconvert_busy()) {
    snprintf(err, err_len, "Es läuft gerade eine Konvertierung. Kopieren, "
             "Konvertieren und Verschieben laufen nicht gleichzeitig.");
    return 1;
  }
  if(ask_smp && ps5tm_gamemove_busy()) {
    snprintf(err, err_len, "ShadowMountPlus verschiebt oder entpackt gerade "
             "ein Spiel. Kopieren, Konvertieren und Verschieben laufen nicht "
             "gleichzeitig.");
    return 1;
  }
  return 0;
}

int
ps5tm_gamecopy_start(const char *title_id, const char *mount, const char *mode,
                     char *err, size_t err_len) {
  char name[128], source[256];
  if(!title_id || !mount || !mode ||
     ps5tm_library_copy_info(title_id, name, sizeof(name),
                             source, sizeof(source)) != 0 ||
     !strncmp(source, "/mnt/shadowmnt", 14)) {
    snprintf(err, err_len, "Dieses Spiel lässt sich nicht kopieren.");
    return 404;
  }
  const char *folder = strrchr(source, '/');
  folder = folder ? folder + 1 : "";
  if(!folder_name_ok(folder)) {
    snprintf(err, err_len, "Der Spielordner hat keinen brauchbaren Namen.");
    return 400;
  }

  job_args_t *a = calloc(1, sizeof(*a));
  if(!a) { snprintf(err, err_len, "Kein Speicher."); return 500; }
  char label[40];
  if(drive_base(mount, a->base, sizeof(a->base), label, sizeof(label)) != 0 ||
     dest_path(a->base, mode, folder, a->target, sizeof(a->target)) != 0) {
    free(a);
    snprintf(err, err_len, "Dieses Ziel gibt es nicht.");
    return 400;
  }
  /* Belt and braces: the paths are built here, but a copy into itself would
     never end. */
  size_t sl = strlen(source);
  if(!strncmp(a->target, source, sl) &&
     (a->target[sl] == '/' || a->target[sl] == 0)) {
    free(a);
    snprintf(err, err_len, "Das Ziel liegt im Spielordner selbst.");
    return 400;
  }
  if(dest_state(a->target) == 1) {
    snprintf(err, err_len, "Am Ziel gibt es %s schon.", a->target);
    free(a);
    return 409;
  }

  ps5tm_gamestate_t gs;
  ps5tm_gamestate_get(&gs);
  if(gs.title_id[0] && !strcmp(gs.title_id, title_id)) {
    free(a);
    snprintf(err, err_len, "Das Spiel läuft gerade. Bitte erst beenden: Es "
             "schreibt womöglich in seinen Ordner.");
    return 409;
  }

  snprintf(a->title_id, sizeof(a->title_id), "%s", title_id);
  snprintf(a->source, sizeof(a->source), "%s", source);
  snprintf(a->mode, sizeof(a->mode), "%s", mode);

  /* The plan measured this folder a moment ago (the page asks for it before
     it offers "start"): a path that cannot fit below THIS destination is
     refused right here, as an error on the button, not as a failed job. The
     copy checks again with a measurement of its own either way. */
  pthread_mutex_lock(&g_lock);
  int      planned = !strcmp(g_plan.source, source) &&
                     ps5tm_mono_ms() - g_plan.at_ms < PLAN_TTL_MS;
  unsigned max_rel = planned ? g_plan.tree.max_rel : 0;
  pthread_mutex_unlock(&g_lock);
  if(planned && strlen(a->target) + 1 + max_rel >= PATH_MAX) {
    snprintf(err, err_len, "Ein Dateipfad ist am Ziel zu lang: Er käme auf %zu "
             "Zeichen, erlaubt sind %d.", strlen(a->target) + 1 + max_rel,
             PATH_MAX - 1);
    free(a);
    return 409;
  }

  /* One file job at a time. Asked before this one is claimed, so a refusal
     leaves the last job's result on the page untouched. */
  if(other_job_active(1, err, err_len)) {
    free(a);
    return 409;
  }

  pthread_mutex_lock(&g_lock);
  if(job_active_locked()) {
    pthread_mutex_unlock(&g_lock);
    free(a);
    snprintf(err, err_len, "Es läuft schon eine Kopie.");
    return 409;
  }
  memset(&g_job, 0, sizeof(g_job));
  g_job.state = JOB_SCANNING;
  snprintf(g_job.title_id, sizeof(g_job.title_id), "%s", title_id);
  snprintf(g_job.name, sizeof(g_job.name), "%s", name);
  snprintf(g_job.mode, sizeof(g_job.mode), "%s", mode);
  snprintf(g_job.source, sizeof(g_job.source), "%s", source);
  snprintf(g_job.target, sizeof(g_job.target), "%s", a->target);
  snprintf(g_job.drive, sizeof(g_job.drive), "%s", label);
  g_job.started_ms = ps5tm_mono_ms();
  __atomic_store_n(&g_cancel, 0, __ATOMIC_RELEASE);
  pthread_mutex_unlock(&g_lock);

  /* A conversion that claimed ITS place in the same instant: each of the two
     sees the other now, and both stand down — neither runs, which is safe, and
     the person starts again. (The check above and the claim are not one step;
     this closes the gap.) */
  if(other_job_active(0, err, err_len)) {
    pthread_mutex_lock(&g_lock);
    memset(&g_job, 0, sizeof(g_job));              /* idle again */
    pthread_mutex_unlock(&g_lock);
    free(a);
    return 409;
  }

  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
  pthread_t th;
  int rc = pthread_create(&th, &attr, copy_thread, a);
  pthread_attr_destroy(&attr);
  if(rc != 0) {
    free(a);
    job_fail("Der Kopiervorgang ließ sich nicht starten.");
    snprintf(err, err_len, "Der Kopiervorgang ließ sich nicht starten.");
    return 500;
  }
  PS5TM_INFO("game_copy_started", "Kopie von %s (%s) nach %s gestartet.",
             title_id, source, g_job.target);
  return 200;
}

cJSON *
ps5tm_gamecopy_status(void) {
  pthread_mutex_lock(&g_lock);
  job_t j = g_job;
  pthread_mutex_unlock(&g_lock);

  cJSON *root = cJSON_CreateObject();
  cJSON_AddBoolToObject  (root, "ok", 1);
  cJSON_AddStringToObject(root, "state", k_state[j.state]);
  cJSON_AddBoolToObject  (root, "active", j.state >= JOB_SCANNING &&
                                          j.state <= JOB_FINISHING);
  if(j.state == JOB_IDLE) return root;

  cJSON_AddStringToObject(root, "title_id", j.title_id);
  cJSON_AddStringToObject(root, "name", j.name);
  cJSON_AddStringToObject(root, "mode", j.mode);
  cJSON_AddStringToObject(root, "source", j.source);
  cJSON_AddStringToObject(root, "target", j.target);
  cJSON_AddStringToObject(root, "drive", j.drive);
  cJSON_AddNumberToObject(root, "total_bytes", (double)j.total_bytes);
  cJSON_AddNumberToObject(root, "done_bytes", (double)j.done_bytes);
  cJSON_AddNumberToObject(root, "checked_bytes", (double)j.checked_bytes);
  cJSON_AddNumberToObject(root, "files_total", j.files_total);
  cJSON_AddNumberToObject(root, "files_done", j.files_done);
  cJSON_AddNumberToObject(root, "skipped", j.skipped);
  if(j.current[0]) cJSON_AddStringToObject(root, "current", j.current);
  if(j.error[0])   cJSON_AddStringToObject(root, "error", j.error);
  if(j.sums[0])    cJSON_AddStringToObject(root, "sums", j.sums);
  if(j.note[0])    cJSON_AddStringToObject(root, "note", j.note);
  uint64_t end = j.finished_ms ? j.finished_ms : ps5tm_mono_ms();
  cJSON_AddNumberToObject(root, "elapsed_s", (double)((end - j.started_ms) / 1000));
  /* Speed and time left count both passes — written, then read back — which
     is why the work is twice the size. */
  if(j.state == JOB_COPYING && j.rate > 0) {
    cJSON_AddNumberToObject(root, "bytes_per_s", j.rate);
    double left = 2.0 * (double)j.total_bytes - (double)(j.done_bytes + j.checked_bytes);
    if(left > 0) cJSON_AddNumberToObject(root, "eta_s", left / j.rate);
  }
  return root;
}

int
ps5tm_gamecopy_cancel(void) {
  pthread_mutex_lock(&g_lock);
  int active = job_active_locked();
  pthread_mutex_unlock(&g_lock);
  if(!active) return -1;
  __atomic_store_n(&g_cancel, 1, __ATOMIC_RELEASE);
  return 0;
}
