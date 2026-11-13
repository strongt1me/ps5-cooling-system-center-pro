/* Converting a game on the console (1.46.0).
 *
 *   exfat    a game folder into an uncompressed exFAT image (.exfat)
 *   ffpfsc   a game folder, or an .exfat/.ffpkg/.ffpfs image, into a
 *            compressed PFS container (.ffpfsc); a folder goes in as an exFAT
 *            image, built and compressed in one pass with no temporary file
 *
 * Both are what MkPFS (PSBrew/MkPFS, GPL-3.0) does on a PC, ported to C in
 * conv_exfat.c and conv_pfs.c; "folder into exFAT into .ffpfsc" is the
 * layout MkPFS and ShadowMountPlus recommend for compressed games. Unpacking
 * the other way is ShadowMountPlus's own job (gamemove.c).
 *
 * The destinations are those of copying (gamecopy.c): the homebrew folder of
 * a drive, where ShadowMountPlus finds the result — next to the original,
 * which the page warns about — or PS5-Sicherung/Spiele, where it never looks.
 * The result is written under a name no scanner knows, read back and checked,
 * and only then renamed; nothing is overwritten, and the original is only
 * read. That unfinished file has a suffix of its own, apart from a copy's, and
 * this app's own leftover of an earlier try is deleted before the room is
 * counted, so what it held is free again.
 *
 * Checking (03.10.2026, checkfile.c): after writing, the whole result is read
 * back from the drive. For .exfat and .ffpfsc a CRC-32 taken of the data
 * while it was written must come out the same (an .ffpfsc is also unpacked
 * block by block on the way). The UFS2 writer lays its blocks down in no
 * order, so an .ffpkg has one CRC-32 per file instead: its structure and every
 * file's blocks are checked against those, and the file is read through once
 * more. Every byte of the last read of each also goes into a SHA-256, left next
 * to the result as "<name>.sha256" in the format of sha256sum.
 *
 * While the job runs the console is kept from going into rest mode on its own
 * (powerguard.c), and between two different drives reading the source and
 * writing the result overlap (iopolicy.c). Both are ideas from PS5 Game
 * Compressor; none of its code is used.
 *
 * One conversion at a time, and none beside a copy (gamecopy.c) or a
 * ShadowMountPlus move or unpack: they would share the drives' room, and a
 * move deletes what a conversion reads. */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <unistd.h>
#ifndef PS5TM_HOST_TEST
#include <sys/mount.h>
#endif

#include "checkfile.h"
#include "conv_exfat.h"
#include "cvname.h"
#include "conv_pfs.h"
#include "conv_ufs2.h"
#include "cvsample.h"
#include "dump_image.h"
#include "fsign.h"
#include "ioerr.h"
#include "imgread.h"
#include "iopolicy.h"
#include "ps5tm.h"
#include "third_party/cJSON.h"
#include "third_party/libdeflate/libdeflate.h"

#define SPACE_RESERVE    (256ull * 1024 * 1024)
#define FAT32_MAX_FILE   0xFFFFFFFFull
#define BACKUP_DIR       "PS5-Sicherung/Spiele"
/* Not the copy's ".ps5cc-teil": a copy of an image and a conversion can end up
   with the same target name, and must never be able to share — or delete — a
   temporary file. */
#define PART_SUFFIX      ".ps5cc-konv-teil"
/* libdeflate's default level; at 6 it matches the ratio MkPFS gets from zlib
   at 7 (Instant Sports Plus, 4.36 GB: 2.08 GB with zlib 7). */
#define DEFLATE_LEVEL    6
/* Four of the five CPUs all payloads share, in the idle class (conv_pfs.c):
   they only get what nobody else wants. Three zlib threads managed 80 MB/s
   on 29.09.2026, with the SSD as the source. */
#define WORKERS          4
/* The source is read in 4 MiB pieces (1 MiB until 10.11.2026: a drive, USB above all, gives more with bigger reads). */
#define IO_CHUNK         (4u << 20)
/* Read-ahead between two drives: three pieces, twelve megabytes in flight. */
#define PREFETCH_BUFS    3
/* The quick check of an exFAT image (10.11.2026): the pieces it keeps a CRC-32 of while writing and reads back afterwards, the first
   and the last few and a few hundred in between: cvsample.h. */

enum { CV_IDLE, CV_SCANNING, CV_WRITING, CV_VERIFYING, CV_DONE, CV_FAILED,
       CV_CANCELLED };
static const char *const k_state[] = { "idle", "scanning", "writing",
                                       "verifying", "done", "failed",
                                       "cancelled" };

typedef struct {
  int      state;
  char     title_id[12];
  char     name[128];
  char     op[8];
  char     mode[12];
  char     source[256];
  char     target[320];
  char     drive[40];
  uint64_t total_bytes;       /* raw payload: the exFAT image or the file */
  uint64_t done_bytes;
  uint64_t output_bytes;
  uint64_t check_total;       /* the result, read back: its size ...      */
  uint64_t check_done;        /* ... and how much of it has been read     */
  unsigned files;
  char     error[640];        /* room for PS5TM_IO_TEXT_MAX (ioerr.h) after a label and a path */
  char     sums[320];         /* the checksum file, once written          */
  int      quick;             /* the check afterwards is the quick one (structure and a sample; no checksum file) */
  char     note[300];         /* something that did not stop the job      */
  uint64_t started_ms, finished_ms;
  uint64_t window_ms, window_bytes;
  double   rate;
  uint64_t cwindow_ms, cwindow_bytes;
  double   crate;             /* the same, for reading back               */
  int      qid;               /* the entry of the queue (convqueue.c) this job runs for, else 0 */
  /* when each step began (monotonic ms; 0 = not yet): the step line of the page shows how long each took (13.11.2026) */
  uint64_t t_write_ms, t_verify_ms, t_rename_ms;
} cv_job_t;

typedef struct {
  char title_id[12];
  char op[8];
  char source[256];
  char base[64];
  char target[320];
  char inner[40];
  int  folder;
  int  ampr;                  /* build ampr_emu.index in the folder first (amprindex.c) */
  int  quick;                 /* check the result the quick way (structure and a sample), no checksum file */
  int  nroots;                /* a PS4 dump: the folders laid over each other (base, update, overlay), else 0 */
  char roots[3][336];                 /* a target path (320) and the ending of the folder of signed files */
  char overlay[256];          /* the folder of our own files among them, removed when the job is over */
} cv_args_t;

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static cv_job_t        g_job;
static int             g_cancel;          /* read by the build loops */


/* ------------------------------------------------------------------ helpers */

static int
active_locked(void) {
  return g_job.state == CV_SCANNING || g_job.state == CV_WRITING ||
         g_job.state == CV_VERIFYING;
}

static void cv_fail(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* The job's timing (the speed window, the time it has run) is all differences,
   so it runs on the monotonic clock: the wall clock steps when the console
   syncs its time, and a difference across a step is garbage. */
static struct { int qid, state; char error[300]; uint64_t bytes; } g_qres;   /* how the last job of a queue entry ended; the next start overwrites g_job */

static void
cv_state(int s) {
  pthread_mutex_lock(&g_lock);
  g_job.state = s;
  if(s >= CV_DONE) {
    g_job.finished_ms = ps5tm_mono_ms();
    if(g_job.qid) {
      g_qres.qid = g_job.qid;
      g_qres.state = s;
      snprintf(g_qres.error, sizeof(g_qres.error), "%s", g_job.error);
      g_qres.bytes = g_job.output_bytes;
    }
  }
  pthread_mutex_unlock(&g_lock);
}

/* 1 and the way it ended ("done", "failed" or "cancelled"), when the last job that ran for queue entry qid has ended; else 0. */
int
ps5tm_gameconvert_queue_result(int qid, char *state, size_t state_len, char *error, size_t error_len, uint64_t *bytes) {
  int have = 0;
  pthread_mutex_lock(&g_lock);
  if(qid && g_qres.qid == qid) {
    have = 1;
    snprintf(state, state_len, "%s", k_state[g_qres.state]);
    snprintf(error, error_len, "%s", g_qres.error);
    if(bytes) *bytes = g_qres.bytes;
  }
  pthread_mutex_unlock(&g_lock);
  return have;
}

static void
cv_fail(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  pthread_mutex_lock(&g_lock);
  vsnprintf(g_job.error, sizeof(g_job.error), fmt, ap);
  char tid[12], why[200];
  snprintf(tid, sizeof(tid), "%s", g_job.title_id);
  snprintf(why, sizeof(why), "%s", g_job.error);
  pthread_mutex_unlock(&g_lock);
  va_end(ap);
  PS5TM_WARN("convert_failed", "Konvertieren von %s gescheitert: %s", tid, why);   /* also the checks before (review 15.10.2026) */
  cv_state(CV_FAILED);
}

static void
cv_progress(void *ctx, uint64_t done) {
  (void)ctx;
  uint64_t now = ps5tm_mono_ms();
  pthread_mutex_lock(&g_lock);
  g_job.done_bytes = done;
  if(!g_job.window_ms) {
    g_job.window_ms    = now;
    g_job.window_bytes = done;
  } else if(now - g_job.window_ms >= 1000) {
    double inst = (double)(done - g_job.window_bytes) * 1000.0 /
                  (double)(now - g_job.window_ms);
    g_job.rate = g_job.rate > 0 ? g_job.rate * 0.7 + inst * 0.3 : inst;
    g_job.window_ms    = now;
    g_job.window_bytes = done;
  }
  pthread_mutex_unlock(&g_lock);
}

/* The same for reading the result back: the speed of that read is its own. */
static void
cv_check_progress(void *ctx, uint64_t done) {
  (void)ctx;
  uint64_t now = ps5tm_mono_ms();
  pthread_mutex_lock(&g_lock);
  g_job.check_done = done;
  if(!g_job.cwindow_ms) {
    g_job.cwindow_ms    = now;
    g_job.cwindow_bytes = done;
  } else if(now - g_job.cwindow_ms >= 1000) {
    double inst = (double)(done - g_job.cwindow_bytes) * 1000.0 /
                  (double)(now - g_job.cwindow_ms);
    g_job.crate = g_job.crate > 0 ? g_job.crate * 0.7 + inst * 0.3 : inst;
    g_job.cwindow_ms    = now;
    g_job.cwindow_bytes = done;
  }
  pthread_mutex_unlock(&g_lock);
}

/* The same for a second read that follows a first one: ctx points to the bytes
   the first read counted. */
static void
cv_check_progress_after(void *ctx, uint64_t done) {
  cv_check_progress(NULL, *(const uint64_t *)ctx + done);
}

/* Starts the reading-back phase of size bytes. */
static void
cv_check_begin(uint64_t size) {
  pthread_mutex_lock(&g_lock);
  g_job.state         = CV_VERIFYING;
  g_job.t_verify_ms   = ps5tm_mono_ms();
  g_job.check_total   = size;
  g_job.check_done    = 0;
  g_job.cwindow_ms    = 0;
  g_job.crate         = 0;
  pthread_mutex_unlock(&g_lock);
}

static int
is_dir(const char *p) {
  struct stat st;
  return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

static const char *
ext_of(const char *path) {
  const char *sl = strrchr(path, '/');
  const char *dot = strrchr(path, '.');
  return dot && (!sl || dot > sl) ? dot : "";
}

/* Only the drives a person plugs in: a PS4 title from the console's own storage panics the console (the converter this
   follows says so), and a mount of the system is no place for one either. */
static int
image_drive_ok(const char *mount) {
  return mount && (!strncmp(mount, "/mnt/usb", 8) || !strncmp(mount, "/mnt/ext", 8)) && !strstr(mount, "..");
}

/* A PS4 game folder: sce_sys/param.sfo and eboot.bin, no param.json. ShadowMountPlus mounts images of these since
   1.7beta8 (param.sfo at the image root, any of its image formats). They are converted the way a PS4 dump is
   (dump_image.c): a param.json built from the param.sfo is laid over the folder, so older versions mount them too. */
static int
ps4_folder(const char *source) {
  char p[PATH_MAX];
  struct stat st;
  if(!is_dir(source)) return 0;
  if(snprintf(p, sizeof(p), "%s/sce_sys/param.json", source) >= (int)sizeof(p) || stat(p, &st) == 0) return 0;
  snprintf(p, sizeof(p), "%s/sce_sys/param.sfo", source);
  if(stat(p, &st) != 0 || !S_ISREG(st.st_mode)) return 0;
  snprintf(p, sizeof(p), "%s/eboot.bin", source);
  return stat(p, &st) == 0 && S_ISREG(st.st_mode);
}

/* What a source can become; "" when nothing. ffpkg (UFS2) is direct-from-
   folder only, like MkPFS's own "folder into exFAT" step — repacking an
   existing image into ffpkg isn't a thing ShadowMountPlus expects.
   A folder only counts when it is a game's own folder — sce_sys/param.json is what ShadowMountPlus itself looks
   for (see gamecopy.c), or a PS4 game's sce_sys/param.sfo (ps4_folder); a folder without either has nothing to
   convert into. */
static void
ops_for(const char *source, int *can_exfat, int *can_ffpfsc, int *can_ffpkg) {
  *can_exfat = *can_ffpfsc = *can_ffpkg = 0;
  if(is_dir(source)) {
    char param[PATH_MAX];
    struct stat st;
    if((snprintf(param, sizeof(param), "%s/sce_sys/param.json", source) < (int)sizeof(param) &&
        stat(param, &st) == 0 && S_ISREG(st.st_mode)) || ps4_folder(source))
      *can_exfat = *can_ffpfsc = *can_ffpkg = 1;
    return;
  }
  const char *e = ext_of(source);
  if(!strcasecmp(e, ".exfat") || !strcasecmp(e, ".ffpkg") || !strcasecmp(e, ".ffpfs"))
    *can_ffpfsc = 1;
}

/* The name without its folder and, for a file, without its extension. */
static void
stem_of(const char *source, int folder, char *out, size_t out_len) {
  const char *b = strrchr(source, '/');
  b = b ? b + 1 : source;
  snprintf(out, out_len, "%s", b);
  if(!folder) {
    char *dot = strrchr(out, '.');
    if(dot && dot != out) *dot = 0;
  }
}

static int
drive_base(const char *mount, char *base, size_t base_len, char *label, size_t label_len) {
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
dest_dir(const char *base, const char *mode, char *out, size_t out_len) {
  const char *sub = !strcmp(mode, "homebrew") ? "homebrew"
                  : !strcmp(mode, "backup")   ? BACKUP_DIR : NULL;
  if(!sub) return -1;
  int n = snprintf(out, out_len, "%s/%s", base, sub);
  return (n > 0 && (size_t)n < out_len) ? 0 : -1;
}

static int
drive_space(const char *base, uint64_t *avail, char *fs, size_t fs_len) {
  struct statvfs sv;
  if(statvfs(base, &sv) != 0) return -1;
  uint64_t fr = sv.f_frsize ? sv.f_frsize : sv.f_bsize;
  *avail = (uint64_t)sv.f_bavail * fr;
  fs[0] = 0;
#ifndef PS5TM_HOST_TEST
  struct statfs sf;
  if(statfs(base, &sf) == 0) snprintf(fs, fs_len, "%s", sf.f_fstypename);
#endif
  return 0;
}

static int
mkdir_p(const char *path) {
  char tmp[PATH_MAX];
  snprintf(tmp, sizeof(tmp), "%s", path);
  for(char *p = tmp + 1; *p; p++) {
    if(*p != '/') continue;
    *p = 0;
    if(mkdir(tmp, 0777) != 0 && errno != EEXIST) return -1;
    *p = '/';
  }
  return (mkdir(tmp, 0777) == 0 || errno == EEXIST) ? 0 : -1;
}

/* 0 free, 1 taken, 2 free but for this app's unfinished file beside it. */
static int
target_state(const char *target) {
  struct stat st;
  if(lstat(target, &st) == 0) return 1;
  char part[PATH_MAX];
  snprintf(part, sizeof(part), "%s" PART_SUFFIX, target);
  return lstat(part, &st) == 0 ? 2 : 0;
}

/* The room this app's own unfinished file beside target gives back once it is
   deleted (target_state() said 2); 0 when there is none. */
static uint64_t
leftover_bytes(const char *target) {
  char part[PATH_MAX];
  struct stat st;
  snprintf(part, sizeof(part), "%s" PART_SUFFIX, target);
  return lstat(part, &st) == 0 && S_ISREG(st.st_mode) ? (uint64_t)st.st_size : 0;
}

static uint32_t
serial_for(const char *title_id) {
  uint32_t h = 2166136261u;                     /* FNV-1a */
  for(const char *p = title_id; *p; p++) h = (h ^ (uint8_t)*p) * 16777619u;
  return h | 1;
}

/* A plain file read in order, for packing an existing image. */
typedef struct { int fd; } file_src_t;

static ssize_t
file_read(void *ctx, void *buf, size_t len) {
  file_src_t *f = ctx;
  for(;;) {
    ssize_t r = read(f->fd, buf, len);
    if(r >= 0 || errno != EINTR) return r;
  }
}

static ssize_t
exfat_read(void *ctx, void *buf, size_t len) {
  return exfat_image_read((exfat_image_t *)ctx, buf, len);
}



/* ------------------------------------------- did the source change while the job ran? (13.11.2026) */

/* A fingerprint of the files of the source, taken before anything is written and again before the result is put in place: the
   count, the bytes, and one 64-bit hash of (path, size, time) per file. A game that was changed meanwhile (a file replaced, added or
   deleted) would give an image of neither state, and the check afterwards reads back what was written, not what should have been.
   The hashes are kept (sorted) for up to STAMP_KEEP files, so that the second look can name the first file that differs. */
#define STAMP_KEEP        400000u
#define STAMP_MAX_ENTRIES 4000000u

typedef struct {
  uint64_t *h;
  size_t    n, cap;
  uint64_t  total, bytes, sum;
  int       kept_all;                    /* every hash is in h (not more than STAMP_KEEP files) */
  int       usable;                      /* the walk was complete: not more than STAMP_MAX_ENTRIES files, no depth over 32 */
  unsigned  changed;                     /* the second look: files whose hash is not among the first ones */
  char      first[256];                  /* the first of them */
} cv_stamp_t;

static uint64_t
stamp_hash(const char *rel, uint64_t size, int64_t mtime) {
  uint64_t h = 1469598103934665603ull;     /* FNV-1a */
  for(const char *p = rel; *p; p++) h = (h ^ (uint8_t)*p) * 1099511628211ull;
  for(int i = 0; i < 8; i++) h = (h ^ ((size >> (8 * i)) & 0xff)) * 1099511628211ull;
  for(int i = 0; i < 8; i++) h = (h ^ (((uint64_t)mtime >> (8 * i)) & 0xff)) * 1099511628211ull;
  return h;
}

static int
stamp_cmp(const void *a, const void *b) {
  uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
  return x < y ? -1 : x > y;
}

static void
stamp_add(cv_stamp_t *st, const cv_stamp_t *ref, const char *rel, uint64_t size, int64_t mtime) {
  uint64_t h = stamp_hash(rel, size, mtime);
  st->total++;
  st->bytes += size;
  st->sum += h;
  if(!ref && st->kept_all) {                 /* the second look (ref) only compares: it keeps no hashes */
    if(st->n >= STAMP_KEEP) st->kept_all = 0;
    else {
      if(st->n == st->cap) {
        size_t nc = st->cap ? st->cap * 2 : 4096;
        uint64_t *nh = realloc(st->h, nc * sizeof(*nh));
        if(!nh) st->kept_all = 0;
        else { st->h = nh; st->cap = nc; }
      }
      if(st->kept_all) st->h[st->n++] = h;
    }
  }
  if(ref && ref->kept_all && !bsearch(&h, ref->h, ref->n, sizeof(*ref->h), stamp_cmp)) {
    if(!st->changed++) snprintf(st->first, sizeof(st->first), "%s", rel);
  }
}

static void
stamp_walk(cv_stamp_t *st, const cv_stamp_t *ref, char *path, size_t len, size_t root_len, int depth) {
  if(depth > 32) { st->usable = 0; return; }
  DIR *d = opendir(path);
  if(!d) return;
  struct dirent *e;
  while((e = readdir(d))) {
    if(e->d_name[0] == '.' && (!e->d_name[1] || (e->d_name[1] == '.' && !e->d_name[2]))) continue;
    if(st->total >= STAMP_MAX_ENTRIES) { st->usable = 0; break; }
    size_t nl = strlen(e->d_name);
    if(len + 1 + nl >= PATH_MAX) { st->usable = 0; continue; }
    path[len] = '/';
    memcpy(path + len + 1, e->d_name, nl + 1);
    struct stat sb;
    if(lstat(path, &sb) == 0) {
      if(S_ISDIR(sb.st_mode)) stamp_walk(st, ref, path, len + 1 + nl, root_len, depth + 1);
      else if(S_ISREG(sb.st_mode)) stamp_add(st, ref, path + root_len + 1, (uint64_t)sb.st_size, (int64_t)sb.st_mtime);
    }
    path[len] = 0;
  }
  closedir(d);
}

/* Takes the fingerprint of source (a folder or one image file). ref: the first one, when this is the second look. */
static void
stamp_take(cv_stamp_t *st, const cv_stamp_t *ref, const char *source, int folder) {
  memset(st, 0, sizeof(*st));
  st->kept_all = 1;
  st->usable = 1;
  if(folder) {
    char path[PATH_MAX];
    size_t len = strlen(source);
    if(len >= sizeof(path)) { st->usable = 0; return; }
    memcpy(path, source, len + 1);
    while(len > 1 && path[len - 1] == '/') path[--len] = 0;
    stamp_walk(st, ref, path, len, len, 0);
  } else {
    struct stat sb;
    const char *b = strrchr(source, '/');
    if(stat(source, &sb) == 0 && S_ISREG(sb.st_mode)) stamp_add(st, ref, b ? b + 1 : source, (uint64_t)sb.st_size, (int64_t)sb.st_mtime);
    else st->usable = 0;
  }
  if(!ref && st->kept_all && st->n > 1) qsort(st->h, st->n, sizeof(*st->h), stamp_cmp);
}

static void
stamp_free(cv_stamp_t *st) {
  free(st->h);
  memset(st, 0, sizeof(*st));
}

/* 0 when source is as it was; else -1 with the sentence in err. A source that could not be fingerprinted (too many files) is not checked. */
static int
stamp_verify(const cv_stamp_t *first, const char *source, int folder, char *err, size_t err_len) {
  if(!first->usable) return 0;
  cv_stamp_t now;
  stamp_take(&now, first, source, folder);
  int same = !now.usable || (now.total == first->total && now.bytes == first->bytes && now.sum == first->sum);
  if(!same) {
    if(now.changed > 1 && now.first[0])
      snprintf(err, err_len, "Die Quelle hat sich während des Konvertierens geändert: %s und andere Dateien. Das Ergebnis wurde nicht übernommen.", now.first);
    else if(now.changed && now.first[0])
      snprintf(err, err_len, "Die Quelle hat sich während des Konvertierens geändert: %s. Das Ergebnis wurde nicht übernommen.", now.first);
    else
      snprintf(err, err_len, "Die Quelle hat sich während des Konvertierens geändert (Dateien sind gelöscht worden, oder Größe und Zeit stimmen "
                             "nicht mehr). Das Ergebnis wurde nicht übernommen.");
  }
  stamp_free(&now);
  return same ? 0 : -1;
}

/* --------------------------------------------------------------- the worker */

static void *cv_run(void *arg);

/* The UFS2 writer over the one folder of a game, or over a PS4 dump's folders laid over each other (dump_image.c). */
static int
cv_ufs2_plan(const cv_args_t *a, uint64_t *size, char *err, size_t err_len) {
  if(a->nroots > 0) {
    const char *rt[3] = { a->roots[0], a->roots[1], a->roots[2] };
    return ufs2_plan_size_roots(rt, (unsigned)a->nroots, size, err, err_len);
  }
  return ufs2_plan_size(a->source, size, err, err_len);
}

static int
cv_ufs2_write(const cv_args_t *a, int fd, const int *cancel, void (*progress)(void *, uint64_t), uint64_t *size,
              ufs2_sums_t **sums, char *err, size_t err_len) {
  if(a->nroots > 0) {
    const char *rt[3] = { a->roots[0], a->roots[1], a->roots[2] };
    return ufs2_write_tree_roots(fd, rt, (unsigned)a->nroots, cancel, progress, NULL, size, sums, err, err_len);
  }
  return ufs2_write_tree(fd, a->source, cancel, progress, NULL, size, sums, err, err_len);
}

/* ------------------------------------------------ the quick check of an exFAT image (10.11.2026) */

/* Reads the pieces kept while writing back from the drive and compares each with its CRC-32. 0, or -1 with the reason in
   err (errno ECANCELED on a cancel). The work shown is the pieces read. */
static int
cv_check_samples(int fd, const cv_sample_t *s, unsigned n, const int *cancel, char *err, size_t err_len) {
  uint64_t total = 0, done = 0;
  uint32_t maxlen = 1;
  for(unsigned i = 0; i < n; i++) {
    total += s[i].len;
    if(s[i].len > maxlen) maxlen = s[i].len;
  }
  uint8_t *buf = malloc(maxlen);
  if(!buf) { snprintf(err, err_len, "Kein Speicher."); return -1; }
  cv_check_begin(total);
  int rc = 0;
  for(unsigned i = 0; i < n && !rc; i++) {
    if(__atomic_load_n(cancel, __ATOMIC_ACQUIRE)) {
      snprintf(err, err_len, "Abgebrochen.");
      errno = ECANCELED;
      rc = -1;
      break;
    }
    size_t got = 0;
    while(got < s[i].len) {
      ssize_t r = pread(fd, buf + got, s[i].len - got, (off_t)(s[i].pos + got));
      if(r < 0 && errno == EINTR) continue;
      if(r <= 0) {
        snprintf(err, err_len, "Lesefehler beim Prüfen: %s", ps5tm_io_strerror(r < 0 ? errno : EIO));
        rc = -1;
        break;
      }
      got += (size_t)r;
    }
    if(rc) break;
    uint32_t crc = libdeflate_crc32(0, buf, s[i].len);
    if(crc != s[i].crc) {
      snprintf(err, err_len, "Die Datei auf dem Laufwerk weicht von dem ab, was geschrieben wurde (Stelle %llu MB, "
               "Prüfsumme %08X statt %08X). Ist das Ziel defekt?",
               (unsigned long long)(s[i].pos >> 20), (unsigned)crc, (unsigned)s[i].crc);
      rc = -1;
      break;
    }
    done += s[i].len;
    cv_check_progress(NULL, done);
  }
  free(buf);
  return rc;
}

/* The console stays awake for as long as the job runs, whichever way it ends
   (powerguard.c). */
static void *
cv_thread(void *arg) {
  char overlay[256];
  snprintf(overlay, sizeof(overlay), "%s", ((cv_args_t *)arg)->overlay);
  ps5tm_powerguard_hold();
  void *r = cv_run(arg);
  ps5tm_powerguard_release();
  if(overlay[0]) dimg_remove_overlay(overlay);
  /* one line in the history (jobhist.c) */
  pthread_mutex_lock(&g_lock);
  int st = g_job.state;
  char name[128], detail[96], error[640];
  snprintf(name, sizeof(name), "%s", g_job.name);
  snprintf(detail, sizeof(detail), "zu %s%s%s", g_job.op, g_job.drive[0] ? " · " : "", g_job.drive);
  snprintf(error, sizeof(error), "%s", g_job.error);
  uint64_t bytes = g_job.output_bytes ? g_job.output_bytes : g_job.done_bytes;
  uint64_t dur = g_job.finished_ms > g_job.started_ms ? g_job.finished_ms - g_job.started_ms : 0;
  pthread_mutex_unlock(&g_lock);
  if(st == CV_DONE || st == CV_FAILED || st == CV_CANCELLED)
    ps5tm_hist_add("convert", name, detail, st == CV_DONE ? "done" : st == CV_FAILED ? "failed" : "cancelled", error, bytes, dur);
  return r;
}

static void *
cv_run(void *arg) {
  cv_args_t a = *(cv_args_t *)arg;
  free(arg);

  char part[PATH_MAX], err[640] = "";
  snprintf(part, sizeof(part), "%s" PART_SUFFIX, a.target);

  exfat_image_t    *img = NULL;
  file_src_t        fsrc = { -1 };
  uint64_t          raw = 0;
  unsigned          files = 1;
  int               out = -1;
  ps5tm_prefetch_t *pf = NULL;
  char              sign_dir[PATH_MAX] = "";      /* the folder of signed executables laid over the game (fsign.c), removed in the end */
  unsigned          signed_n = 0, refused_n = 0;
  cv_stamp_t        stamp;                        /* the fingerprint of the source before anything is written */
  memset(&stamp, 0, sizeof(stamp));

  /* The AMPR index first, if asked for: it goes into the folder and with it into the image, at the place APR-EMU
     looks for it (/app0/ampr_emu.index). */
  if(a.folder && a.ampr) {
    unsigned ai_files = 0, ai_coll = 0;
    int arc = ps5tm_ampr_build_dir(a.source, &ai_files, &ai_coll, &g_cancel, err, sizeof(err));
    if(arc == ECANCELED) { cv_state(CV_CANCELLED); return NULL; }
    if(arc) { cv_fail("Der AMPR-Index ließ sich nicht bauen: %s", err[0] ? err : strerror(arc)); return NULL; }
    PS5TM_INFO("ampr_index_built", "AMPR-Index für %s vor dem Konvertieren gebaut: %u Dateien.", a.title_id, ai_files);
    err[0] = 0;
  }
  /* The source as it is now (after the AMPR index, which is part of it): compared once more before the result is put in place. */
  stamp_take(&stamp, NULL, a.source, a.folder);
  /* Unsigned PS5 executables (fsign.c): the console's loader refuses a plain ELF, so the image would not start the game. They are
     written as fake-signed SELFs into a small folder next to the result, which goes over the game as a second source (the way a
     PS4 dump gets its param.json); the game folder itself is only read. A PS4 folder has none of these. */
  if(a.folder && a.nroots == 0) {
    fsign_list_t sl;
    if(ps5tm_fsign_scan(a.source, &g_cancel, &sl) == 1) {
      cv_state(CV_CANCELLED);
      PS5TM_INFO("game_convert_cancelled", "Konvertieren von %s abgebrochen, bevor etwas geschrieben wurde.", a.title_id);
      goto cleanup;
    }
    refused_n = sl.refused;
    if(sl.refused)
      PS5TM_WARN("game_convert_sign_refused", "Konvertieren von %s: %u Programmdateien lassen sich nicht signieren und bleiben, wie sie sind (z. B. %.120s: %.100s).",
                 a.title_id, sl.refused, sl.refused_rel, sl.refused_why);
    if(sl.n) {
      char sdir[PATH_MAX], sw[400] = "";
      uint64_t avail0 = 0;
      char fs0[16];
      snprintf(sdir, sizeof(sdir), "%s", a.target);
      char *ssl = strrchr(sdir, '/');
      if(ssl) *ssl = 0;
      snprintf(sign_dir, sizeof(sign_dir), "%s" FSIGN_STAGE_SUFFIX, a.target);
      ps5tm_fsign_rmtree(sign_dir);                                /* this app's own leftover of an earlier try */
      if(mkdir_p(sdir) != 0) { cv_fail("Der Zielordner ließ sich nicht anlegen."); ps5tm_fsign_free(&sl); sign_dir[0] = 0; goto cleanup; }
      if(drive_space(a.base, &avail0, fs0, sizeof(fs0)) != 0 || avail0 < sl.self_bytes + SPACE_RESERVE) {
        cv_fail("Auf dem Ziel ist nicht genug Platz, um die Programmdateien zu signieren.");
        ps5tm_fsign_free(&sl);
        sign_dir[0] = 0;
        goto cleanup;
      }
      unsigned idx = 0;
      int sr = ps5tm_fsign_stage(a.source, sign_dir, &sl, &g_cancel, NULL, NULL, &idx, sw, sizeof(sw));
      if(sr == 1) {
        cv_state(CV_CANCELLED);
        PS5TM_INFO("game_convert_cancelled", "Konvertieren von %s abgebrochen, bevor etwas geschrieben wurde.", a.title_id);
        ps5tm_fsign_free(&sl);
        goto cleanup;
      }
      if(sr != 0) {
        cv_fail("Die Programmdateien ließen sich nicht signieren: %s", sw[0] ? sw : "ohne Angabe");
        PS5TM_WARN("game_convert_sign_failed", "Konvertieren von %s: Signieren gescheitert: %s", a.title_id, sw);
        ps5tm_fsign_free(&sl);
        goto cleanup;
      }
      signed_n = sl.n;
      PS5TM_INFO("game_convert_signed", "Konvertieren von %s: %u unsignierte Programmdateien als FSELF in das Abbild geschrieben%s.",
                 a.title_id, sl.n, idx ? " (AMPR-Index angepasst)" : "");
      ps5tm_fsign_free(&sl);
      snprintf(a.roots[0], sizeof(a.roots[0]), "%s", a.source);
      snprintf(a.roots[1], sizeof(a.roots[1]), "%s", sign_dir);
      a.nroots = 2;
    }
  }
  if(a.folder) {
    if(a.nroots > 0) {
      const char *rt[3] = { a.roots[0], a.roots[1], a.roots[2] };
      img = exfat_image_plan_roots(rt, (unsigned)a.nroots, serial_for(a.title_id), err, sizeof(err));
    } else {
      img = exfat_image_plan(a.source, serial_for(a.title_id), err, sizeof(err));
    }
    if(!img) { cv_fail("%s", err); goto cleanup; }
    raw   = exfat_image_size(img);
    files = exfat_image_files(img);
  } else {
    struct stat st;
    fsrc.fd = open(a.source, O_RDONLY);
    if(fsrc.fd < 0 || fstat(fsrc.fd, &st) != 0 || !S_ISREG(st.st_mode)) {
      cv_fail("Die Abbild-Datei ist nicht lesbar.");
      goto cleanup;
    }
    raw = (uint64_t)st.st_size;
  }

  /* An .ffpkg is as big as its own image, which is not the exFAT image's size:
     a big file's pointer blocks are counted generously, and every file is
     rounded up to whole blocks. The room is asked for that size. */
  uint64_t need_bytes = raw;
  if(!strcmp(a.op, "ffpkg") && a.folder) {
    uint64_t ffpkg_size = 0;
    if(cv_ufs2_plan(&a, &ffpkg_size, err, sizeof(err)) != 0) { cv_fail("%s", err); goto cleanup; }
    need_bytes = ffpkg_size;
  }

  /* Planning cannot be interrupted (it is another file's code). A cancel that
     came meanwhile is honoured here, before anything is touched — not only
     once the output has been created and a leftover deleted for nothing. */
  if(__atomic_load_n(&g_cancel, __ATOMIC_ACQUIRE)) {
    cv_state(CV_CANCELLED);
    PS5TM_INFO("game_convert_cancelled", "Konvertieren von %s abgebrochen, "
               "bevor etwas geschrieben wurde.", a.title_id);
    goto cleanup;
  }

  /* This app's own leftover from an earlier try goes FIRST, so that the room
     it held is free when the room is measured next. Only what carries this
     job's suffix is ever deleted. */
  int ts = target_state(a.target);
  if(ts == 1) { cv_fail("Am Ziel gibt es %s schon.", a.target); goto cleanup; }
  if(ts == 2) unlink(part);                     /* this app's own leftover */

  uint64_t avail = 0;
  char fs[16];
  if(drive_space(a.base, &avail, fs, sizeof(fs)) != 0) {
    cv_fail("Das Ziel %s ist nicht erreichbar.", a.base);
    goto cleanup;
  }
  /* The compressed size is only known at the end: room for the raw size is
     asked for, as MkPFS and the Game Compressor do. */
  if(avail < need_bytes + SPACE_RESERVE) {
    cv_fail("Auf dem Ziel ist nicht genug Platz.");
    goto cleanup;
  }
  if(!strcmp(fs, "msdosfs") && need_bytes > FAT32_MAX_FILE) {
    cv_fail("Das Ziel ist mit FAT32 formatiert und nimmt keine Datei über 4 GB auf.");
    goto cleanup;
  }

  char dir[PATH_MAX];
  snprintf(dir, sizeof(dir), "%s", a.target);
  char *sl = strrchr(dir, '/');
  if(sl) *sl = 0;
  if(mkdir_p(dir) != 0) { cv_fail("Der Zielordner ließ sich nicht anlegen."); goto cleanup; }
  if(__atomic_load_n(&g_cancel, __ATOMIC_ACQUIRE)) {   /* no output exists yet */
    cv_state(CV_CANCELLED);
    PS5TM_INFO("game_convert_cancelled", "Konvertieren von %s abgebrochen, "
               "bevor etwas geschrieben wurde.", a.title_id);
    goto cleanup;
  }
  out = open(part, O_RDWR | O_CREAT | O_EXCL, 0666);
  if(out < 0) { cv_fail("Die Zieldatei ließ sich nicht anlegen: %s", ps5tm_io_strerror(errno)); goto cleanup; }

  pthread_mutex_lock(&g_lock);
  g_job.total_bytes = raw;
  g_job.files       = files;
  g_job.state       = CV_WRITING;
  g_job.t_write_ms  = ps5tm_mono_ms();
  if(signed_n && refused_n)
    snprintf(g_job.note, sizeof(g_job.note), "%u Programmdateien waren nicht signiert und stehen im Abbild als FSELF; %u ließen sich nicht signieren und blieben, wie sie sind.", signed_n, refused_n);
  else if(signed_n)
    snprintf(g_job.note, sizeof(g_job.note), "%u Programmdateien waren nicht signiert (einfache ELF-Dateien, die die Konsole nicht startet) und stehen im Abbild als FSELF.", signed_n);
  else if(refused_n)
    snprintf(g_job.note, sizeof(g_job.note), "%u Programmdateien sind nicht signiert und ließen sich nicht signieren; sie blieben, wie sie sind.", refused_n);
  pthread_mutex_unlock(&g_lock);

  uint64_t size = 0;
  int rc;
  uint32_t wcrc = 0;                  /* CRC-32 of the payload as it was written */
  uint8_t  sha[32];                   /* SHA-256 of the finished file            */
  int      have_sha = 0;

  /* Reading the source and writing the result overlap only between two
     different drives (iopolicy.c); on one drive, or when that is not certain,
     they follow each other as before. The UFS2 writer reads inside its own
     loop and keeps that. */
  conv_read_fn rd   = a.folder ? exfat_read : file_read;
  void        *rctx = a.folder ? (void *)img : (void *)&fsrc;
  char         why_io[96] = "";
  if(strcmp(a.op, "ffpkg") &&
     ps5tm_io_parallel(a.source, a.base, why_io, sizeof(why_io))) {
    pf = ps5tm_prefetch_start(rd, rctx, IO_CHUNK, PREFETCH_BUFS);
    if(pf) { rd = ps5tm_prefetch_read; rctx = pf; }
  }
  if(why_io[0])
    PS5TM_INFO("game_convert_io", "Konvertieren von %s: %s%s.", a.title_id, why_io,
               strcmp(a.op, "ffpkg") && !pf ? " (kein Platz für den Lesepuffer, nacheinander)" : "");

  if(!strcmp(a.op, "exfat")) {
    uint8_t *buf = malloc(IO_CHUNK);
    rc = buf ? 0 : -1;
    if(!buf) snprintf(err, sizeof(err), "Kein Speicher.");
    uint64_t pos = 0;
    /* The quick check keeps a CRC-32 of some pieces while they go down: the first and the last few, and one about every
       "stride" bytes between (a few hundred altogether); afterwards only those are read back. */
    cv_sampler_t sm;
    cv_sampler_init(&sm, raw, IO_CHUNK);
    while(!rc) {
      if(__atomic_load_n(&g_cancel, __ATOMIC_ACQUIRE)) {
        rc = -1;
        errno = ECANCELED;
        snprintf(err, sizeof(err), "Abgebrochen.");
        break;
      }
      ssize_t n = rd(rctx, buf, IO_CHUNK);
      if(n < 0) {
        snprintf(err, sizeof(err), "Lesefehler bei %s: %s", exfat_image_where(img),
                 ps5tm_io_strerror(errno));
        rc = -1;
        break;
      }
      if(n == 0) break;
      wcrc = libdeflate_crc32(wcrc, buf, (size_t)n);
      if(a.quick) cv_sampler_add(&sm, pos, (uint32_t)n, libdeflate_crc32(0, buf, (size_t)n));
      const uint8_t *p = buf;
      size_t left = (size_t)n;
      while(left) {
        ssize_t w = pwrite(out, p, left, (off_t)pos);
        if(w < 0 && errno == EINTR) continue;
        if(w <= 0) {
          snprintf(err, sizeof(err), "Schreibfehler: %s", ps5tm_io_strerror(errno ? errno : EIO));
          rc = -1;
          break;
        }
        p += w; left -= (size_t)w; pos += (uint64_t)w;
      }
      cv_progress(NULL, pos);
    }
    free(buf);
    if(!rc && (pos != raw || fsync(out) != 0)) {
      snprintf(err, sizeof(err), "Das Abbild ist unvollständig.");
      rc = -1;
    }
    size = pos;
    if(!rc && a.quick) {
      /* The quick level: the size of the file, then the pieces kept while writing, read back and compared. The image is not
         read through, so there is no SHA-256 and no checksum file. The last few pieces join the ones taken on the way. */
      cv_sampler_finish(&sm);
      struct stat fst;
      if(fstat(out, &fst) != 0 || (uint64_t)fst.st_size != size) {
        snprintf(err, sizeof(err), "Das Abbild ist unvollständig.");
        rc = -1;
      } else {
        ps5tm_drop_cache(out);
        rc = cv_check_samples(out, sm.smp, sm.nsmp, &g_cancel, err, sizeof(err));
      }
    } else if(!rc) {
      /* Read back from the drive: every byte, against the CRC-32 taken while
         it was written. */
      cv_check_begin(size);
      uint32_t rcrc = 0;
      ps5tm_drop_cache(out);
      rc = ps5tm_digest_fd(out, size, &rcrc, sha, &g_cancel, cv_check_progress, NULL,
                           err, sizeof(err));
      if(!rc && rcrc != wcrc) {
        snprintf(err, sizeof(err), "Die Datei auf dem Laufwerk weicht von dem ab, was "
                 "geschrieben wurde (Prüfsumme %08X statt %08X). Ist das Ziel defekt?",
                 (unsigned)rcrc, (unsigned)wcrc);
        rc = -1;
      }
      have_sha = !rc;
    }
  } else if(!strcmp(a.op, "ffpkg")) {
    ufs2_sums_t *usums = NULL;
    rc = cv_ufs2_write(&a, out, &g_cancel, cv_progress, &size, &usums, err, sizeof(err));
    if(!rc) {
      /* The blocks of this writer go down in no order, so the image as a whole
         has no CRC to compare. What it has is one per file, taken while the
         file was read: first the structure and every file's blocks are checked
         against those, then the whole file is read through once more for the
         checksum file's SHA-256. The work shown is the two reads together. */
      uint64_t data = ufs2_sums_bytes(usums);
      cv_check_begin(a.quick ? data : data + size);
      ps5tm_drop_cache(out);
      rc = ufs2_verify_tree(out, usums, &g_cancel, cv_check_progress, NULL, err, sizeof(err));
      if(!rc && !a.quick) {
        ps5tm_drop_cache(out);
        rc = ps5tm_digest_fd(out, size, NULL, sha, &g_cancel, cv_check_progress_after, &data,
                             err, sizeof(err));
      }
      /* The quick level stops after the first read (structure and every file's blocks): no second read for the SHA-256,
         so no checksum file. */
      have_sha = !rc && !a.quick;
    }
    ufs2_sums_free(usums);
  } else {
    pfsc_opts_t o = { DEFLATE_LEVEL, WORKERS, &g_cancel, cv_progress, NULL, &wcrc };
    char inner[48];
    snprintf(inner, sizeof(inner), "%s", a.inner);
    rc = pfs_write_single(out, inner, raw, rd, rctx, &o, &size, err, sizeof(err));
    if(!rc) {
      cv_check_begin(size);
      /* quick: structure and a sample of the blocks, the image is not read through (no payload CRC, no SHA-256) */
      pfsc_check_t c = { wcrc, &g_cancel, cv_check_progress, NULL, 0, a.quick };
      rc = pfs_verify_full(out, &c, a.quick ? NULL : sha, err, sizeof(err));
      have_sha = !rc && !a.quick;
    }
  }
  /* The reading-ahead thread stops before what it reads from is freed. */
  ps5tm_prefetch_stop(pf);
  pf = NULL;
  /* A cancel that came during a step that cannot be interrupted (the last
     sync, the renaming) is honoured here, before the result is put in place:
     the person was told the conversion is being cancelled. */
  int stop = __atomic_load_n(&g_cancel, __ATOMIC_ACQUIRE);
  if(!rc && stop) {
    rc = -1;
    snprintf(err, sizeof(err), "Abgebrochen.");
  }
  int cancelled = rc && stop;
  close(out);
  out = -1;

  if(!rc) {                                     /* the last step: the source is looked at once more, then the result gets its name */
    pthread_mutex_lock(&g_lock);
    g_job.t_rename_ms = ps5tm_mono_ms();
    pthread_mutex_unlock(&g_lock);
    if(stamp_verify(&stamp, a.source, a.folder, err, sizeof(err)) != 0) {
      PS5TM_WARN("game_convert_source_changed", "Konvertieren von %s: %s", a.title_id, err);
      rc = -1;
    }
  }
  if(!rc) {
    struct stat st;
    if(lstat(a.target, &st) == 0) {             /* appeared meanwhile */
      snprintf(err, sizeof(err), "Am Ziel gibt es %s schon.", a.target);
      rc = -1;
    } else if(rename(part, a.target) != 0) {
      snprintf(err, sizeof(err), "Umbenennen gescheitert: %s", ps5tm_io_strerror(errno));
      rc = -1;
    }
  }
  if(rc) {
    unlink(part);                               /* ours, created with O_EXCL */
    if(cancelled) {
      cv_state(CV_CANCELLED);
      PS5TM_INFO("game_convert_cancelled", "Konvertieren von %s abgebrochen.", a.title_id);
    } else {
      cv_fail("%s", err[0] ? err : "Das Konvertieren ist gescheitert.");
      PS5TM_WARN("game_convert_failed", "Konvertieren von %s gescheitert: %s",
                 a.title_id, err);
    }
    goto cleanup;
  }

  /* The checksum file, next to the result. The result is in place and checked
     by now; a failure to write this file does not undo it, and is only said. */
  char sums_path[PATH_MAX + 16], sums_err[400] = "";
  int  sums_ok = 0;
  if(have_sha && snprintf(sums_path, sizeof(sums_path), "%s" PS5TM_SUMS_EXT, a.target)
                 < (int)sizeof(sums_path)) {
    ps5tm_sums_t *sm = ps5tm_sums_open(sums_path, sums_err, sizeof(sums_err));
    if(sm) {
      const char *name = strrchr(a.target, '/');
      name = name ? name + 1 : a.target;
      if(ps5tm_sums_add(sm, sha, name) == 0) {
        sums_ok = ps5tm_sums_close(sm, sums_err, sizeof(sums_err)) == 0;
      } else {
        ps5tm_sums_abort(sm);
        snprintf(sums_err, sizeof(sums_err), "Schreibfehler.");
      }
    }
  }
  if(have_sha && !sums_ok)
    PS5TM_WARN("game_convert_sums_failed", "Prüfsummen-Datei zu %s nicht geschrieben: %s",
               a.target, sums_err[0] ? sums_err : "Pfad zu lang.");

  pthread_mutex_lock(&g_lock);
  g_job.output_bytes = size;
  if(sums_ok) snprintf(g_job.sums, sizeof(g_job.sums), "%s", sums_path);
  else if(a.quick) {                  /* the quick level reads no SHA-256, so there is no checksum file; said once */
    size_t nl = strlen(g_job.note);
    snprintf(g_job.note + nl, sizeof(g_job.note) - nl, "%s%s", nl ? " · " : "",
             strcmp(a.op, "ffpkg") ? "Schnell geprüft: Aufbau und Stichprobe, daher keine Prüfsummen-Datei."
                                   : "Schnell geprüft: Aufbau und alle Dateien in einem Durchgang, daher keine Prüfsummen-Datei.");
  } else if(have_sha) {
    size_t nl = strlen(g_job.note);
    snprintf(g_job.note + nl, sizeof(g_job.note) - nl, "%sDie Prüfsummen-Datei ließ sich nicht "
             "schreiben. Das Ergebnis selbst ist geprüft und in Ordnung.", nl ? " · " : "");
  }
  uint64_t secs = (ps5tm_mono_ms() - g_job.started_ms) / 1000;
  pthread_mutex_unlock(&g_lock);
  cv_state(CV_DONE);
  ps5tm_smp_forget();
  ps5tm_library_forget();
  int idle = pfs_workers_idle();
  PS5TM_INFO("game_convert_done", "%s nach %s konvertiert und geprüft: %llu MB aus %llu MB in "
             "%llu s (%llu MB/s)%s%s.", a.title_id, a.target, (unsigned long long)(size >> 20),
             (unsigned long long)(raw >> 20), (unsigned long long)secs,
             (unsigned long long)(secs ? (raw >> 20) / secs : 0),
             strcmp(a.op, "ffpfsc") ? ""
             : idle == 1 ? ", Rechen-Threads in der Leerlaufklasse"
             : idle == 0 ? ", Leerlaufklasse abgelehnt — normale Priorität" : "",
             sums_ok ? ", Prüfsummen-Datei angelegt" : "");

cleanup:
  stamp_free(&stamp);
  ps5tm_prefetch_stop(pf);
  if(out >= 0) { close(out); unlink(part); }
  if(fsrc.fd >= 0) close(fsrc.fd);
  exfat_image_free(img);
  if(sign_dir[0]) ps5tm_fsign_rmtree(sign_dir);
  return NULL;
}


/* ------------------------------------------------------- compression and streaming games (21.10.2026) */

/* A compressed image (ffpfsc) is unpacked by the console again and again while the game reads. Games with a great deal of data
   or with texture streaming can stutter then. Nothing here can know a game's engine; it looks for what such games have in
   common and the page asks before it packs one: a lot of data altogether, big streaming containers (Unreal's .ucas/.utoc/.pak,
   Anvil's .forge, id Tech's .streamdb/.resources, Frostbite's .cas, Rockstar's .rpf) or a folder named for streaming. */
#define STREAM_DATA_MIN     (40ull << 30)
#define STREAM_CONTAINER_MIN (4ull << 30)
#define STREAM_DIR_MIN      (2ull << 30)
#define STREAM_MAX_ENTRIES  3000000u

typedef struct {
  uint64_t data, container, sdir;
  char     exts[4][10];
  int      n_exts;
  char     dir_name[64];
  unsigned entries;
  unsigned max_name, max_path;          /* the longest file name and the longest path inside the source, in bytes */
} stream_scan_t;

static void
stream_note(stream_scan_t *x, const char *path, uint64_t size) {
  x->data += size;
  const char *sl = strrchr(path, '/');
  const char *name = sl ? sl + 1 : path;
  if(strlen(name) > x->max_name) x->max_name = (unsigned)strlen(name);
  if(strlen(path) > x->max_path) x->max_path = (unsigned)strlen(path);
  const char *dot = strrchr(name, '.');
  static const char *const cont[] = { ".ucas", ".utoc", ".pak", ".forge", ".streamdb", ".resources", ".cas", ".rpf", NULL };
  if(dot) {
    for(int i = 0; cont[i]; i++) {
      if(strcasecmp(dot, cont[i]) != 0) continue;
      x->container += size;
      int have = 0;
      for(int k = 0; k < x->n_exts; k++) if(!strcasecmp(x->exts[k], cont[i])) have = 1;
      if(!have && x->n_exts < 4) snprintf(x->exts[x->n_exts++], sizeof x->exts[0], "%s", cont[i]);
      break;
    }
  }
  /* a folder whose name says streaming: StreamingAssets, Streaming, streamed ... */
  for(const char *c = path; *c;) {
    while(*c == '/') c++;
    const char *e = strchr(c, '/');
    if(!e) break;                                       /* the last part is the file */
    if(!strncasecmp(c, "stream", 6)) {
      x->sdir += size;
      if(!x->dir_name[0]) snprintf(x->dir_name, sizeof x->dir_name, "%.*s", (int)(e - c), c);
      break;
    }
    c = e;
  }
}

static void
stream_walk_dir(stream_scan_t *x, const char *dir, const char *rel, int depth) {
  if(depth > 24) return;
  DIR *d = opendir(dir);
  if(!d) return;
  struct dirent *e;
  while((e = readdir(d)) && x->entries < STREAM_MAX_ENTRIES) {
    if(e->d_name[0] == '.' && (!e->d_name[1] || (e->d_name[1] == '.' && !e->d_name[2]))) continue;
    char full[PATH_MAX], r[PATH_MAX];
    struct stat st;
    if(snprintf(full, sizeof full, "%s/%s", dir, e->d_name) >= (int)sizeof full) continue;
    if(snprintf(r, sizeof r, "%s/%s", rel, e->d_name) >= (int)sizeof r) continue;
    if(lstat(full, &st) != 0) continue;
    x->entries++;
    if(S_ISDIR(st.st_mode)) stream_walk_dir(x, full, r, depth + 1);
    else if(S_ISREG(st.st_mode)) stream_note(x, r, (uint64_t)st.st_size);
  }
  closedir(d);
}

static int
stream_img_cb(const imgr_entry_t *e, void *ctx) {
  stream_scan_t *x = ctx;
  if(!e->is_dir) stream_note(x, e->path, e->size);
  return ++x->entries >= STREAM_MAX_ENTRIES;
}

typedef struct { const char *source; int folder; stream_scan_t res; } stream_job_t;

/* On a thread of its own with a large stack: an image goes through imgread.c, whose readers do not fit a connection thread's. */
static void *
stream_thread(void *arg) {
  stream_job_t *j = arg;
  if(j->folder) stream_walk_dir(&j->res, j->source, "", 0);
  else {
    char err[160];
    imgr_t *r = imgr_open(j->source, err, sizeof err);
    if(r) { imgr_walk(r, stream_img_cb, &j->res); imgr_close(r); }
  }
  return NULL;
}

/* {risky, data_bytes, big_data, container_bytes, containers:[".pak"...], stream_dir, stream_dir_bytes}; never fails: when the
   source cannot be read the answer is "not risky" (the page then does not ask). */
static cJSON *
stream_risk_json(const char *source, int folder, uint64_t raw_bytes) {
  stream_job_t *j = calloc(1, sizeof *j);
  cJSON *o = cJSON_CreateObject();
  if(!j) return o;
  j->source = source;
  j->folder = folder;
  pthread_t th;
  pthread_attr_t at;
  pthread_attr_init(&at);
  pthread_attr_setstacksize(&at, 1u << 20);
  if(pthread_create(&th, &at, stream_thread, j) == 0) pthread_join(th, NULL);
  pthread_attr_destroy(&at);
  stream_scan_t *x = &j->res;
  uint64_t data = x->data ? x->data : raw_bytes;
  int big = data >= STREAM_DATA_MIN, cont = x->container >= STREAM_CONTAINER_MIN, sdir = x->sdir >= STREAM_DIR_MIN;
  cJSON_AddBoolToObject(o, "risky", big || cont || sdir);
  cJSON_AddNumberToObject(o, "data_bytes", (double)data);
  cJSON_AddBoolToObject(o, "big_data", big);
  cJSON_AddNumberToObject(o, "container_bytes", cont ? (double)x->container : 0);
  cJSON *ex = cJSON_AddArrayToObject(o, "containers");
  if(cont) for(int k = 0; k < x->n_exts; k++) cJSON_AddItemToArray(ex, cJSON_CreateString(x->exts[k]));
  cJSON_AddStringToObject(o, "stream_dir", sdir ? x->dir_name : "");
  cJSON_AddNumberToObject(o, "stream_dir_bytes", sdir ? (double)x->sdir : 0);
  cJSON_AddNumberToObject(o, "max_name", x->max_name);
  cJSON_AddNumberToObject(o, "max_path", x->max_path);
  free(j);
  return o;
}

/* --------------------------------------------------------------- interface */

/* What the last plan worked out (the walk over the source is the costly part): the check before the start (ps5tm_gameconvert_check) and
   the queue use it instead of walking again. It is for one title at a time and goes stale after PLAN_KEEP_MS. */
#define PLAN_KEEP_MS (30ull * 60 * 1000)
typedef struct {
  int      valid;
  char     title_id[12], name[128], source[256], version[40];
  int      folder, ps4;
  uint64_t raw, ffpkg_bytes, files;
  char     ffpkg_why[700];
  unsigned max_name, max_path;
  uint64_t at_ms;
} plan_cache_t;
static pthread_mutex_t g_pc_lock = PTHREAD_MUTEX_INITIALIZER;
#define PLAN_SLOTS 4
static plan_cache_t    g_pc[PLAN_SLOTS];             /* the last few titles: two windows or the queue asking about two games do not push each other out */

static void
game_version(const char *title_id, char *out, size_t n) {
  ps5tm_libdel_t d;
  out[0] = 0;
  if(ps5tm_library_delete_info(title_id, &d) == 0) snprintf(out, n, "%s", d.version);
}

cJSON *
ps5tm_gameconvert_plan(const char *title_id, char *err, size_t err_len) {
  char name[128], source[256];
  if(ps5tm_library_copy_info(title_id, name, sizeof(name), source, sizeof(source)) != 0) {
    snprintf(err, err_len, "Dieses Spiel lässt sich nicht konvertieren.");
    return NULL;
  }
  int can_exfat, can_ffpfsc, can_ffpkg;
  ops_for(source, &can_exfat, &can_ffpfsc, &can_ffpkg);
  int folder = is_dir(source);
  int ps4 = folder && ps4_folder(source);
  if(ps4) {
    dimg_info_t pi;
    if(dimg_check(source, NULL, &pi, err, err_len) != 0) return NULL;
    if(strcmp(pi.title_id, title_id) != 0) {
      snprintf(err, err_len, "Die param.sfo im Ordner nennt %s, nicht %s.", pi.title_id, title_id);
      return NULL;
    }
  }

  uint64_t raw = 0;
  unsigned files = 1;
  if(folder) {
    exfat_image_t *img = exfat_image_plan(source, serial_for(title_id), err, err_len);
    if(!img) return NULL;
    raw   = exfat_image_size(img);
    files = exfat_image_files(img);
    exfat_image_free(img);
    if(ps4) raw += 1ull << 20;                         /* room for the param.json laid over it (dump_image.c) */
  } else {
    struct stat st;
    if(stat(source, &st) != 0) {
      snprintf(err, err_len, "Die Spieldaten sind nicht erreichbar.");
      return NULL;
    }
    raw = (uint64_t)st.st_size;
  }

  /* What an .ffpkg of this folder takes: its own image size, not the exFAT
     image's. 0 when that cannot be worked out here, with the reason in
     ffpkg_why (a folder with too many entries, a file too big): the page says
     so before anything is chosen, and the conversion refuses with the same
     words. */
  uint64_t ffpkg_bytes = 0;
  char ffpkg_why[700] = "";
  if(folder && can_ffpkg) {
    if(ufs2_plan_size(source, &ffpkg_bytes, ffpkg_why, sizeof(ffpkg_why)) != 0) ffpkg_bytes = 0;
    else { ffpkg_why[0] = 0; if(ps4) ffpkg_bytes += 1ull << 20; }
  }

  char stem[200];
  stem_of(source, folder, stem, sizeof(stem));
  ps5tm_gamestate_t gs;
  ps5tm_gamestate_get(&gs);
  pthread_mutex_lock(&g_lock);
  int busy = active_locked();
  pthread_mutex_unlock(&g_lock);

  cJSON *root = cJSON_CreateObject();
  cJSON_AddBoolToObject  (root, "ok", 1);
  cJSON_AddStringToObject(root, "title_id", title_id);
  cJSON_AddStringToObject(root, "name", name);
  cJSON_AddStringToObject(root, "source", source);
  cJSON_AddStringToObject(root, "kind", folder ? "folder" : "file");
  /* A PS4 folder: only USB/M.2 targets are listed (a PS4 title from the console's own storage panics it), no AMPR
     index (a PS5 thing), and a param.json from the param.sfo goes into the image. */
  cJSON_AddBoolToObject  (root, "ps4", ps4);
  cJSON_AddStringToObject(root, "stem", stem);
  char version[40];
  game_version(title_id, version, sizeof(version));
  cJSON_AddStringToObject(root, "version", version);
  cJSON_AddNumberToObject(root, "raw_bytes", (double)raw);
  cJSON *srisk = stream_risk_json(source, folder, raw);
  cJSON_AddItemToObject  (root, "stream_risk", srisk);
  {
    const cJSON *mn = cJSON_GetObjectItem(srisk, "max_name"), *mp = cJSON_GetObjectItem(srisk, "max_path");
    pthread_mutex_lock(&g_pc_lock);
    int slot = 0;                                    /* this title's slot, else a free one, else the oldest */
    for(int i = 0; i < PLAN_SLOTS; i++) {
      if(g_pc[i].valid && !strcmp(g_pc[i].title_id, title_id)) { slot = i; break; }
      if(!g_pc[i].valid) { slot = i; break; }
      if(g_pc[i].at_ms < g_pc[slot].at_ms) slot = i;
    }
    plan_cache_t *pcs = &g_pc[slot];
    memset(pcs, 0, sizeof(*pcs));
    pcs->valid = 1;
    snprintf(pcs->title_id, sizeof(pcs->title_id), "%s", title_id);
    snprintf(pcs->name, sizeof(pcs->name), "%s", name);
    snprintf(pcs->source, sizeof(pcs->source), "%s", source);
    snprintf(pcs->version, sizeof(pcs->version), "%s", version);
    pcs->folder = folder;
    pcs->ps4 = ps4;
    pcs->raw = raw;
    pcs->ffpkg_bytes = ffpkg_bytes;
    pcs->files = files;
    snprintf(pcs->ffpkg_why, sizeof(pcs->ffpkg_why), "%s", ffpkg_why);
    pcs->max_name = cJSON_IsNumber(mn) ? (unsigned)mn->valuedouble : 0;
    pcs->max_path = cJSON_IsNumber(mp) ? (unsigned)mp->valuedouble : 0;
    pcs->at_ms = ps5tm_mono_ms();
    pthread_mutex_unlock(&g_pc_lock);
  }
  /* A PS5 folder with plain ELF executables: the image gets them as FSELF (fsign.c), the page says so before the start. */
  cJSON_AddItemToObject  (root, "unsigned_exec", ps5tm_fsign_info(folder && !ps4 ? source : NULL));
  cJSON_AddNumberToObject(root, "ffpkg_bytes", (double)ffpkg_bytes);
  if(ffpkg_why[0]) cJSON_AddStringToObject(root, "ffpkg_error", ffpkg_why);
  cJSON_AddNumberToObject(root, "files", files);
  cJSON *ops = cJSON_AddArrayToObject(root, "ops");
  if(can_ffpfsc) cJSON_AddItemToArray(ops, cJSON_CreateString("ffpfsc"));
  if(can_ffpkg)  cJSON_AddItemToArray(ops, cJSON_CreateString("ffpkg"));
  if(can_exfat)  cJSON_AddItemToArray(ops, cJSON_CreateString("exfat"));
  cJSON_AddBoolToObject  (root, "running", gs.title_id[0] && !strcmp(gs.title_id, title_id));
  cJSON_AddBoolToObject  (root, "busy", busy);
  /* A copy runs: no conversion starts beside it (the start says so too). */
  cJSON_AddBoolToObject  (root, "other_busy", ps5tm_gamecopy_busy());

  ps5tm_sysinfo_t info;
  ps5tm_sysinfo_get(&info);
  uint64_t need = raw + SPACE_RESERVE;
  uint64_t need_ffpkg = ffpkg_bytes ? ffpkg_bytes + SPACE_RESERVE : need;
  cJSON *arr = cJSON_AddArrayToObject(root, "targets");
  for(unsigned i = 0; i < info.volume_count; i++) {
    char base[64], label[40], fs[16];
    uint64_t avail = 0;
    if((ps4 && !image_drive_ok(info.volumes[i].path)) ||
       drive_base(info.volumes[i].path, base, sizeof(base), label, sizeof(label)) != 0 ||
       drive_space(base, &avail, fs, sizeof(fs)) != 0)
      continue;
    cJSON *d = cJSON_CreateObject();
    cJSON_AddStringToObject(d, "mount", info.volumes[i].path);
    cJSON_AddStringToObject(d, "label", label);
    cJSON_AddStringToObject(d, "base", base);
    cJSON_AddStringToObject(d, "fs", fs);
    cJSON_AddNumberToObject(d, "free_bytes", (double)avail);
    size_t bl = strlen(base);
    cJSON_AddBoolToObject  (d, "source_here", !strncmp(source, base, bl) && source[bl] == '/');
    cJSON_AddBoolToObject  (d, "fat32_too_big", !strcmp(fs, "msdosfs") && raw > FAT32_MAX_FILE);
    /* An .ffpkg is its own size, which is bigger than the exFAT image's (every file rounded up to whole blocks):
       a stick it would not fit on as one file is named before the start, as the job would refuse it. */
    cJSON_AddBoolToObject  (d, "fat32_too_big_ffpkg",
                            !strcmp(fs, "msdosfs") && (ffpkg_bytes ? ffpkg_bytes : raw) > FAT32_MAX_FILE);
    static const char *const modes[] = { "homebrew", "backup" };
    static const char *const exts[]  = { ".ffpfsc", ".ffpkg", ".exfat" };
    static const char *const keys[]  = { "ffpfsc", "ffpkg", "exfat" };
    const int offered[3] = { can_ffpfsc, can_ffpkg, can_exfat };
    /* What is promised here is what the job will find: it deletes this app's
       own leftover first, so the room that leftover holds counts as free for
       THAT target (enough_space inside the target). The drive-wide
       enough_space is true when at least one offered target fits. */
    int fits_any = avail >= need;
    for(int m = 0; m < 2; m++) {
      char dir[128];
      if(dest_dir(base, modes[m], dir, sizeof(dir)) != 0) continue;
      cJSON *o = cJSON_AddObjectToObject(d, modes[m]);
      cJSON_AddStringToObject(o, "dir", dir);
      for(int e = 0; e < 3; e++) {
        char path[400];
        snprintf(path, sizeof(path), "%s/%s%s", dir, stem, exts[e]);
        int      ts   = target_state(path);
        uint64_t back = ts == 2 ? leftover_bytes(path) : 0;
        int      fits = avail + back >= (e == 1 ? need_ffpkg : need);
        if(fits && offered[e]) fits_any = 1;
        cJSON *t = cJSON_AddObjectToObject(o, keys[e]);
        cJSON_AddStringToObject(t, "path", path);
        cJSON_AddBoolToObject  (t, "exists", ts == 1);
        cJSON_AddBoolToObject  (t, "unfinished", ts == 2);
        cJSON_AddNumberToObject(t, "unfinished_bytes", (double)back);
        cJSON_AddBoolToObject  (t, "enough_space", fits);
      }
    }
    cJSON_AddBoolToObject  (d, "enough_space", fits_any);
    cJSON_AddItemToArray(arr, d);
  }
  return root;
}


/* ------------------------------------------------------ the check before the start (13.11.2026) */

static void
chk_add(cJSON *arr, const char *id, const char *level, const char *hint, const char *fmt, ...) __attribute__((format(printf, 5, 6)));

static void
chk_add(cJSON *arr, const char *id, const char *level, const char *hint, const char *fmt, ...) {
  char text[400];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(text, sizeof(text), fmt, ap);
  va_end(ap);
  cJSON *o = cJSON_CreateObject();
  cJSON_AddStringToObject(o, "id", id);
  cJSON_AddStringToObject(o, "level", level);
  cJSON_AddStringToObject(o, "text", text);
  if(hint && hint[0]) cJSON_AddStringToObject(o, "hint", hint);
  cJSON_AddItemToArray(arr, o);
}

static const char *
size_text(uint64_t b, char *buf, size_t n) {
  if(b >= (1ull << 30)) snprintf(buf, n, "%.1f GB", (double)b / (double)(1ull << 30));
  else snprintf(buf, n, "%llu MB", (unsigned long long)(b >> 20));
  return buf;
}

/* The ending of the result for op. */
static const char *
op_ext(const char *op) {
  return !strcmp(op, "exfat") ? ".exfat" : !strcmp(op, "ffpkg") ? ".ffpkg" : ".ffpfsc";
}

/* The stem of the result name from the template tpl for this game: 0, or -1 with the reason in err. bytes: its length with the ending. */
static int
result_stem(const char *tpl, const char *op, const char *title_id, const char *name, const char *version, const char *source, int folder,
            char *stem, size_t stem_len, size_t *bytes, char *err, size_t err_len) {
  char quelle[200];
  stem_of(source, folder, quelle, sizeof(quelle));
  ps5tm_cvname_fields_t f = { name, title_id, version, quelle };
  return ps5tm_cvname_expand(tpl, &f, strlen(op_ext(op)), stem, stem_len, bytes, err, err_len);
}

cJSON *
ps5tm_gameconvert_check(const char *title_id, const char *op, const char *mount, const char *mode, const char *name_tpl, char *err,
                        size_t err_len) {
  if(!title_id || !op || !mount || !mode) { snprintf(err, err_len, "Angaben fehlen."); return NULL; }
  plan_cache_t pc;
  for(int tries = 0; tries < 2; tries++) {
    pthread_mutex_lock(&g_pc_lock);
    pc.valid = 0;
    for(int i = 0; i < PLAN_SLOTS; i++) if(g_pc[i].valid && !strcmp(g_pc[i].title_id, title_id)) { pc = g_pc[i]; break; }
    pthread_mutex_unlock(&g_pc_lock);
    if(pc.valid && !strcmp(pc.title_id, title_id) && ps5tm_mono_ms() - pc.at_ms < PLAN_KEEP_MS) break;
    pc.valid = 0;
    if(tries == 0) {                                    /* no plan of this game at hand: work it out (the same walk the plan does) */
      cJSON *pj = ps5tm_gameconvert_plan(title_id, err, err_len);
      if(!pj) return NULL;
      cJSON_Delete(pj);
    }
  }
  if(!pc.valid) { snprintf(err, err_len, "Das Spiel ließ sich nicht vermessen."); return NULL; }

  int can_exfat, can_ffpfsc, can_ffpkg;
  ops_for(pc.source, &can_exfat, &can_ffpfsc, &can_ffpkg);
  if(!(!strcmp(op, "exfat") && can_exfat) && !(!strcmp(op, "ffpfsc") && can_ffpfsc) && !(!strcmp(op, "ffpkg") && can_ffpkg)) {
    snprintf(err, err_len, "Diese Umwandlung gibt es für dieses Spiel nicht.");
    return NULL;
  }

  cJSON *root = cJSON_CreateObject();
  cJSON *arr = cJSON_AddArrayToObject(root, "checks");
  char b1[32], b2[32];
  int errors = 0;

  /* the source */
  if(pc.folder && !strcmp(op, "exfat")) chk_add(arr, "source", "ok", NULL, "Quelle lesbar: %llu Dateien, als exFAT-Abbild %s",
                                                (unsigned long long)pc.files, size_text(pc.raw, b1, sizeof(b1)));
  else if(pc.folder) chk_add(arr, "source", "ok", NULL, "Quelle lesbar: %llu Dateien", (unsigned long long)pc.files);
  else chk_add(arr, "source", "ok", NULL, "Abbild-Datei lesbar: %s", size_text(pc.raw, b1, sizeof(b1)));
  if(!strcmp(op, "ffpkg") && pc.ffpkg_why[0]) {
    chk_add(arr, "ffpkg", "error", pc.ffpkg_why, "Ein ffpkg-Abbild ist bei diesem Spiel nicht möglich.");
    errors++;
  }
  if(pc.max_name > 240 || pc.max_path > 800)
    chk_add(arr, "names", "warn", "exFAT erlaubt 255 Zeichen je Name und diese App 1024 Bytes je Pfad.",
            "Ein Dateiname (%u Bytes) oder ein Pfad (%u Bytes) im Spiel kommt nah an die Grenze.", pc.max_name, pc.max_path);

  /* the game must not run */
  ps5tm_gamestate_t gs;
  ps5tm_gamestate_get(&gs);
  if(gs.title_id[0] && !strcmp(gs.title_id, title_id)) {
    chk_add(arr, "running", "error", "Beende das Spiel, dann geht es weiter.", "Das Spiel läuft gerade.");
    errors++;
  }

  /* a PS4 title only goes to a plugged-in drive */
  if(pc.ps4 && !image_drive_ok(mount)) {
    chk_add(arr, "ps4", "error", "Ein PS4-Titel aus dem Speicher der Konsole bringt sie zum Absturz.",
            "Das Abbild eines PS4-Spiels darf nur auf ein USB- oder M.2-Laufwerk.");
    errors++;
  }

  /* the target: drive, name, room, file system */
  char base[64], label[40], dir[128];
  char stem[256] = "", eerr[200] = "";
  size_t name_bytes = 0;
  int have_target = 0;
  cJSON_AddStringToObject(root, "target", "");
  if(drive_base(mount, base, sizeof(base), label, sizeof(label)) != 0 || dest_dir(base, mode, dir, sizeof(dir)) != 0) {
    chk_add(arr, "drive", "error", NULL, "Dieses Ziel gibt es nicht.");
    errors++;
  } else if(result_stem(name_tpl, op, title_id, pc.name, pc.version, pc.source, pc.folder, stem, sizeof(stem), &name_bytes, eerr, sizeof(eerr)) != 0) {
    chk_add(arr, "name", "error", NULL, "%s", eerr);
    errors++;
  } else {
    char target[400];
    snprintf(target, sizeof(target), "%s/%s%s", dir, stem, op_ext(op));
    cJSON_ReplaceItemInObject(root, "target", cJSON_CreateString(target));
    have_target = 1;
    char fname[300];
    snprintf(fname, sizeof(fname), "%s%s", stem, op_ext(op));
    chk_add(arr, "name", "ok", NULL, "Name der Ausgabe: %s (%zu von %d Bytes)", fname, name_bytes, PS5TM_CVNAME_MAX - PS5TM_CVNAME_SUFFIX);

    uint64_t avail = 0;
    char fs[16];
    int ts = target_state(target);
    uint64_t back = ts == 2 ? leftover_bytes(target) : 0;
    uint64_t need = !strcmp(op, "ffpkg") && pc.ffpkg_bytes ? pc.ffpkg_bytes : pc.raw;
    if(ts == 1) {
      chk_add(arr, "exists", "error", "Ändere den Namen oder wähle einen anderen Ordner.", "Am Ziel gibt es %s schon.", fname);
      errors++;
    } else if(ts == 2) {
      chk_add(arr, "leftover", "ok", NULL, "Dort liegt eine unfertige Datei dieser App. Sie wird ersetzt (%s werden frei).", size_text(back, b1, sizeof(b1)));
    }
    if(drive_space(base, &avail, fs, sizeof(fs)) != 0) {
      chk_add(arr, "drive", "error", NULL, "Das Ziel %s ist nicht erreichbar.", label);
      errors++;
    } else {
      if(avail + back < need + SPACE_RESERVE) {
        chk_add(arr, "space", "error", "Wähle ein anderes Laufwerk oder schaffe Platz.",
                "Auf %s ist nicht genug Platz: %s frei, gebraucht werden bis zu %s.", label, size_text(avail + back, b1, sizeof(b1)),
                size_text(need + SPACE_RESERVE, b2, sizeof(b2)));
        errors++;
      } else {
        chk_add(arr, "space", "ok", NULL, "Platz auf %s: %s frei, gebraucht höchstens %s", label, size_text(avail + back, b1, sizeof(b1)),
                size_text(need, b2, sizeof(b2)));
      }
      if(!strcmp(fs, "msdosfs") && need > FAT32_MAX_FILE) {
        chk_add(arr, "fat32", "error", "Wähle ein Laufwerk mit exFAT.", "Das Ziel ist mit FAT32 formatiert und nimmt keine Datei über 4 GB auf. Das Abbild wird %s groß.",
                size_text(need, b1, sizeof(b1)));
        errors++;
      }
    }
  }

  /* another job: the start has to wait, the queue can take it */
  int busy = 0;
  char why[160] = "";
  if(ps5tm_gameconvert_busy()) { busy = 1; snprintf(why, sizeof(why), "Es läuft gerade eine Konvertierung"); }
  else if(ps5tm_jobs_busy_except(PS5TM_JOB_GAMECONVERT, PS5TM_JOBS_NO_SMP, why, sizeof(why))) busy = 1;   /* no question to ShadowMountPlus at every key typed */
  if(busy) chk_add(arr, "busy", "warn", "In die Warteschlange gestellt, beginnt der Auftrag danach von selbst.", "%s. Der Start geht erst danach.", why);

  cJSON_AddBoolToObject  (root, "ok", 1);
  cJSON_AddBoolToObject  (root, "can_queue", errors == 0 && have_target);
  cJSON_AddBoolToObject  (root, "can_start", errors == 0 && have_target && !busy);
  cJSON_AddStringToObject(root, "stem", have_target ? stem : "");
  cJSON_AddNumberToObject(root, "name_bytes", (double)name_bytes);
  cJSON_AddNumberToObject(root, "name_limit", PS5TM_CVNAME_MAX - PS5TM_CVNAME_SUFFIX);
  return root;
}

/* The unsigned PS5 executables of a game folder, for the windows of converting and of packing into a new folder. */
cJSON *
ps5tm_fsign_info(const char *folder) {
  fsign_list_t l;
  memset(&l, 0, sizeof(l));
  if(folder && folder[0] == '/') ps5tm_fsign_scan(folder, NULL, &l);
  cJSON *o = cJSON_CreateObject();
  cJSON_AddNumberToObject(o, "count", l.n);
  cJSON_AddNumberToObject(o, "refused", l.refused);
  cJSON_AddBoolToObject  (o, "capped", l.capped);
  cJSON_AddNumberToObject(o, "bytes", (double)l.elf_bytes);
  cJSON *a = cJSON_AddArrayToObject(o, "files");
  for(unsigned i = 0; i < l.n && i < 8; i++) cJSON_AddItemToArray(a, cJSON_CreateString(l.it[i].rel));
  ps5tm_fsign_free(&l);
  return o;
}

/* 1 while a conversion is under way. For the other file jobs' starts: only one
   of copy, conversion and ShadowMountPlus move/unpack runs at a time. */
int
ps5tm_gameconvert_busy(void) {
  pthread_mutex_lock(&g_lock);
  int busy = active_locked();
  pthread_mutex_unlock(&g_lock);
  return busy;
}

/* The reason in err when a copy or a ShadowMountPlus move/unpack is running; 0
   when none is. No lock of this file is held while asking: the others take
   theirs, and two jobs asking each other with their own lock held would wait
   on each other forever. */
static int
other_job_active(int ask_smp, char *err, size_t err_len) {
  char w[160];
  if(ps5tm_jobs_busy_except(PS5TM_JOB_GAMECONVERT, ask_smp ? 0 : PS5TM_JOBS_NO_SMP, w, sizeof(w))) {
    snprintf(err, err_len, "%s. Kopieren, Konvertieren und Verschieben laufen nicht gleichzeitig.", w);
    return 1;
  }
  return 0;
}

/* Claims the one place for a file job and starts the worker. Takes over a; frees it on every refusal. qid: the queue entry it runs for, or 0. */
static int
cv_launch(cv_args_t *a, const char *title_id, const char *name, const char *source, const char *op, const char *mode,
          const char *label, int qid, char *err, size_t err_len) {
  /* One file job at a time. Asked before this one is claimed, so a refusal
     leaves the last job's result on the page untouched. */
  if(other_job_active(1, err, err_len)) {
    free(a);
    return 409;
  }

  pthread_mutex_lock(&g_lock);
  if(active_locked()) {
    pthread_mutex_unlock(&g_lock);
    free(a);
    snprintf(err, err_len, "Es läuft schon eine Konvertierung.");
    return 409;
  }
  memset(&g_job, 0, sizeof(g_job));
  g_job.state = CV_SCANNING;
  snprintf(g_job.title_id, sizeof(g_job.title_id), "%s", title_id);
  snprintf(g_job.name, sizeof(g_job.name), "%s", name);
  snprintf(g_job.op, sizeof(g_job.op), "%s", op);
  snprintf(g_job.mode, sizeof(g_job.mode), "%s", mode);
  snprintf(g_job.source, sizeof(g_job.source), "%s", source);
  snprintf(g_job.target, sizeof(g_job.target), "%s", a->target);
  snprintf(g_job.drive, sizeof(g_job.drive), "%s", label);
  g_job.quick = a->quick;
  g_job.qid = qid;
  if(qid && g_qres.qid == qid) g_qres.qid = 0;       /* an entry tried again: its earlier result is not this job's */
  g_job.started_ms = ps5tm_mono_ms();
  __atomic_store_n(&g_cancel, 0, __ATOMIC_RELEASE);
  pthread_mutex_unlock(&g_lock);

  /* A copy that claimed ITS place in the same instant: each of the two sees
     the other now, and both stand down — neither runs, which is safe, and the
     person starts again. (The check above and the claim are not one step; this
     closes the gap.) */
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
  /* the AMPR index walk (amprindex.c) recurses with ~4 KB per folder level: never the small default stack */
  pthread_attr_setstacksize(&attr, 1024 * 1024);
  pthread_t th;
  int rc = pthread_create(&th, &attr, cv_thread, a);
  pthread_attr_destroy(&attr);
  if(rc != 0) {
    free(a);
    cv_fail("Das Konvertieren ließ sich nicht starten.");
    snprintf(err, err_len, "Das Konvertieren ließ sich nicht starten.");
    return 500;
  }
  PS5TM_INFO("game_convert_started", "Konvertieren von %s (%s) nach %s gestartet.",
             title_id, source, g_job.target);
  return 200;
}

int
ps5tm_gameconvert_start(const char *title_id, const char *op, int ampr_index, const char *mount,
                        const char *mode, int quick, const char *name_tpl, int qid, char *err, size_t err_len) {
  char name[128], source[256];
  if(!title_id || !op || !mount || !mode ||
     ps5tm_library_copy_info(title_id, name, sizeof(name), source, sizeof(source)) != 0) {
    snprintf(err, err_len, "Dieses Spiel lässt sich nicht konvertieren.");
    return 404;
  }
  int can_exfat, can_ffpfsc, can_ffpkg;
  ops_for(source, &can_exfat, &can_ffpfsc, &can_ffpkg);
  int want_exfat = !strcmp(op, "exfat"), want_ffpfsc = !strcmp(op, "ffpfsc"),
      want_ffpkg = !strcmp(op, "ffpkg");
  if(!(want_exfat && can_exfat) && !(want_ffpfsc && can_ffpfsc) && !(want_ffpkg && can_ffpkg)) {
    snprintf(err, err_len, "Diese Umwandlung gibt es für dieses Spiel nicht.");
    return 400;
  }

  int ps4 = ps4_folder(source);
  dimg_info_t pi;
  if(ps4) {
    if(!image_drive_ok(mount)) {
      snprintf(err, err_len, "Das Abbild eines PS4-Spiels darf nur auf ein USB- oder M.2-Laufwerk, nie in den "
               "Speicher der Konsole: Ein PS4-Titel von dort bringt die Konsole zum Absturz.");
      return 400;
    }
    if(dimg_check(source, NULL, &pi, err, err_len) != 0) return 409;
    if(strcmp(pi.title_id, title_id) != 0) {
      snprintf(err, err_len, "Die param.sfo im Ordner nennt %s, nicht %s.", pi.title_id, title_id);
      return 409;
    }
  }

  cv_args_t *a = calloc(1, sizeof(*a));
  if(!a) { snprintf(err, err_len, "Kein Speicher."); return 500; }
  char label[40], dir[128], stem[200];
  a->folder = is_dir(source);
  a->ampr = a->folder && ampr_index && !ps4;
  a->quick = quick ? 1 : 0;
  {
    char version[40], eerr[200] = "";
    game_version(title_id, version, sizeof(version));
    if(result_stem(name_tpl, op, title_id, name, version, source, a->folder, stem, sizeof(stem), NULL, eerr, sizeof(eerr)) != 0) {
      free(a);
      snprintf(err, err_len, "%s", eerr);
      return 400;
    }
  }
  if(drive_base(mount, a->base, sizeof(a->base), label, sizeof(label)) != 0 ||
     dest_dir(a->base, mode, dir, sizeof(dir)) != 0 ||
     snprintf(a->target, sizeof(a->target), "%s/%s%s", dir, stem,
              want_exfat ? ".exfat" : want_ffpkg ? ".ffpkg" : ".ffpfsc") >= (int)sizeof(a->target)) {
    free(a);
    snprintf(err, err_len, "Dieses Ziel gibt es nicht.");
    return 400;
  }
  if(target_state(a->target) == 1) {
    snprintf(err, err_len, "Am Ziel gibt es %s schon.", a->target);
    free(a);
    return 409;
  }
  ps5tm_gamestate_t gs;
  ps5tm_gamestate_get(&gs);
  if(gs.title_id[0] && !strcmp(gs.title_id, title_id)) {
    free(a);
    snprintf(err, err_len, "Das Spiel läuft gerade. Bitte erst beenden.");
    return 409;
  }

  snprintf(a->title_id, sizeof(a->title_id), "%s", title_id);
  snprintf(a->op, sizeof(a->op), "%s", op);
  snprintf(a->source, sizeof(a->source), "%s", source);
  /* The one file inside the container, named after the title id as MkPFS
     does; the extension says what it is. */
  if(a->folder) snprintf(a->inner, sizeof(a->inner), "%s.exfat", title_id);
  else {
    char e[12];
    snprintf(e, sizeof(e), "%s", ext_of(source));
    for(char *p = e; *p; p++) if(*p >= 'A' && *p <= 'Z') *p = (char)(*p + 32);
    snprintf(a->inner, sizeof(a->inner), "%s%s", title_id, e);
  }
  if(!ps4) return cv_launch(a, title_id, name, source, op, mode, label, qid, err, err_len);

  /* A PS4 folder: the folder with our param.json (and a repaired npbind.dat, if needed) laid over it, as for a dump.
     The overlay is written only once this job may start, and the worker removes it when it is over.
     One start at a time: the overlay folder is named after the title, so two starts of the same title at once (a double
     click, the page and the fullscreen app) would both write it, and the one that is then refused would delete it under
     the one that runs. The first start has claimed the place when cv_launch returns; the second then sees it busy. */
  static pthread_mutex_t ps4_start_lock = PTHREAD_MUTEX_INITIALIZER;
  pthread_mutex_lock(&ps4_start_lock);
  int rc, busy, repaired = 0;
  char overlay[256];
  if(other_job_active(1, err, err_len)) { free(a); rc = 409; goto ps4_done; }
  pthread_mutex_lock(&g_lock);
  busy = active_locked();
  pthread_mutex_unlock(&g_lock);
  if(busy) { snprintf(err, err_len, "Es läuft schon eine Konvertierung."); free(a); rc = 409; goto ps4_done; }
  snprintf(a->overlay, sizeof(a->overlay), "%s/dump-image/%s", PS5TM_DATA_DIR, title_id);
  if(dimg_write_overlay(source, NULL, a->overlay, &pi, &repaired, err, err_len) != 0) {
    dimg_remove_overlay(a->overlay);
    free(a);
    rc = 500;
    goto ps4_done;
  }
  snprintf(a->roots[0], sizeof(a->roots[0]), "%s", source);
  snprintf(a->roots[1], sizeof(a->roots[1]), "%s", a->overlay);
  a->nroots = 2;
  PS5TM_INFO("ps4_image", "PS4-Ordner %s: param.json aus der param.sfo gebaut%s.", title_id,
             repaired ? ", npbind.dat-Prüfsumme korrigiert" : "");
  snprintf(overlay, sizeof(overlay), "%s", a->overlay);
  rc = cv_launch(a, title_id, name, source, op, mode, label, qid, err, err_len);
  if(rc != 200) dimg_remove_overlay(overlay);        /* cv_launch freed a; the worker would have removed it otherwise */
ps4_done:
  pthread_mutex_unlock(&ps4_start_lock);
  return rc;
}


cJSON *
ps5tm_gameconvert_status(void) {
  pthread_mutex_lock(&g_lock);
  cv_job_t j = g_job;
  pthread_mutex_unlock(&g_lock);

  cJSON *root = cJSON_CreateObject();
  cJSON_AddBoolToObject  (root, "ok", 1);
  cJSON_AddStringToObject(root, "state", k_state[j.state]);
  cJSON_AddBoolToObject  (root, "active", j.state >= CV_SCANNING && j.state <= CV_VERIFYING);
  if(j.state == CV_IDLE) return root;
  cJSON_AddStringToObject(root, "title_id", j.title_id);
  cJSON_AddStringToObject(root, "name", j.name);
  cJSON_AddStringToObject(root, "op", j.op);
  cJSON_AddStringToObject(root, "verify", j.quick ? "quick" : "full");
  cJSON_AddStringToObject(root, "mode", j.mode);
  cJSON_AddStringToObject(root, "source", j.source);
  cJSON_AddStringToObject(root, "target", j.target);
  cJSON_AddStringToObject(root, "drive", j.drive);
  cJSON_AddNumberToObject(root, "total_bytes", (double)j.total_bytes);
  cJSON_AddNumberToObject(root, "done_bytes", (double)j.done_bytes);
  cJSON_AddNumberToObject(root, "files", j.files);
  if(j.output_bytes) cJSON_AddNumberToObject(root, "output_bytes", (double)j.output_bytes);
  if(j.error[0]) cJSON_AddStringToObject(root, "error", j.error);
  /* The result read back: how big it is and how far the reading has come. */
  if(j.check_total) {
    cJSON_AddNumberToObject(root, "check_total_bytes", (double)j.check_total);
    cJSON_AddNumberToObject(root, "check_done_bytes", (double)j.check_done);
  }
  if(j.sums[0]) cJSON_AddStringToObject(root, "sums", j.sums);
  if(j.note[0]) cJSON_AddStringToObject(root, "note", j.note);
  uint64_t end = j.finished_ms ? j.finished_ms : ps5tm_mono_ms();
  cJSON_AddNumberToObject(root, "elapsed_s", (double)((end - j.started_ms) / 1000));
  if(j.qid) cJSON_AddNumberToObject(root, "queue_id", j.qid);
  /* The steps with the time each took (13.11.2026): "done", "now" (running), "stopped" (the job ended in it) or "todo". */
  {
    static const char *const k_stage[4] = { "scan", "write", "verify", "rename" };
    uint64_t at[4] = { j.started_ms, j.t_write_ms, j.t_verify_ms, j.t_rename_ms };
    cJSON *sa = cJSON_AddArrayToObject(root, "stages");
    for(int i = 0; i < 4; i++) {
      uint64_t to = 0;                                    /* when the step ended: the start of the next one that began, else the job's end */
      for(int k = i + 1; k < 4 && !to; k++) if(at[k]) to = at[k];
      int begun = at[i] != 0, later = to != 0;
      const char *st = !begun ? "todo" : later ? "done" : j.finished_ms ? (j.state == CV_DONE ? "done" : "stopped") : "now";
      if(!to) to = end;
      cJSON *o = cJSON_CreateObject();
      cJSON_AddStringToObject(o, "id", k_stage[i]);
      cJSON_AddStringToObject(o, "state", st);
      cJSON_AddNumberToObject(o, "ms", begun && to > at[i] ? (double)(to - at[i]) : 0);
      cJSON_AddItemToArray(sa, o);
    }
  }
  if(j.state == CV_WRITING && j.rate > 0) {
    cJSON_AddNumberToObject(root, "bytes_per_s", j.rate);
    if(j.total_bytes > j.done_bytes)
      cJSON_AddNumberToObject(root, "eta_s", (double)(j.total_bytes - j.done_bytes) / j.rate);
  } else if(j.state == CV_VERIFYING && j.crate > 0) {
    cJSON_AddNumberToObject(root, "bytes_per_s", j.crate);
    if(j.check_total > j.check_done)
      cJSON_AddNumberToObject(root, "eta_s", (double)(j.check_total - j.check_done) / j.crate);
  }
  return root;
}

int
ps5tm_gameconvert_cancel(void) {
  /* set under the lock that saw the job: a cancel arriving as one job ends must not hit the next */
  pthread_mutex_lock(&g_lock);
  int active = active_locked();
  if(active) __atomic_store_n(&g_cancel, 1, __ATOMIC_RELEASE);
  pthread_mutex_unlock(&g_lock);
  return active ? 0 : -1;
}
