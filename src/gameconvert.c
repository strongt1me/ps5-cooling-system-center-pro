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
#include "conv_pfs.h"
#include "conv_ufs2.h"
#include "ioerr.h"
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
#define IO_CHUNK         (1u << 20)
/* Read-ahead between two drives: four megabytes in flight. */
#define PREFETCH_BUFS    4

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
  char     note[200];         /* something that did not stop the job      */
  uint64_t started_ms, finished_ms;
  uint64_t window_ms, window_bytes;
  double   rate;
  uint64_t cwindow_ms, cwindow_bytes;
  double   crate;             /* the same, for reading back               */
} cv_job_t;

typedef struct {
  char title_id[12];
  char op[8];
  char source[256];
  char base[64];
  char target[320];
  char inner[40];
  int  folder;
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
static void
cv_state(int s) {
  pthread_mutex_lock(&g_lock);
  g_job.state = s;
  if(s >= CV_DONE) g_job.finished_ms = ps5tm_mono_ms();
  pthread_mutex_unlock(&g_lock);
}

static void
cv_fail(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  pthread_mutex_lock(&g_lock);
  vsnprintf(g_job.error, sizeof(g_job.error), fmt, ap);
  pthread_mutex_unlock(&g_lock);
  va_end(ap);
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

/* What a source can become; "" when nothing. ffpkg (UFS2) is direct-from-
   folder only, like MkPFS's own "folder into exFAT" step — repacking an
   existing image into ffpkg isn't a thing ShadowMountPlus expects. */
static void
ops_for(const char *source, int *can_exfat, int *can_ffpfsc, int *can_ffpkg) {
  *can_exfat = *can_ffpfsc = *can_ffpkg = 0;
  if(is_dir(source)) { *can_exfat = *can_ffpfsc = *can_ffpkg = 1; return; }
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


/* --------------------------------------------------------------- the worker */

static void *cv_run(void *arg);

/* The console stays awake for as long as the job runs, whichever way it ends
   (powerguard.c). */
static void *
cv_thread(void *arg) {
  ps5tm_powerguard_hold();
  void *r = cv_run(arg);
  ps5tm_powerguard_release();
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

  if(a.folder) {
    img = exfat_image_plan(a.source, serial_for(a.title_id), err, sizeof(err));
    if(!img) { cv_fail("%s", err); return NULL; }
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
    if(ufs2_plan_size(a.source, &ffpkg_size, err, sizeof(err)) != 0) { cv_fail("%s", err); goto cleanup; }
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
    if(!rc) {
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
    rc = ufs2_write_tree(out, a.source, &g_cancel, cv_progress, NULL, &size, &usums,
                         err, sizeof(err));
    if(!rc) {
      /* The blocks of this writer go down in no order, so the image as a whole
         has no CRC to compare. What it has is one per file, taken while the
         file was read: first the structure and every file's blocks are checked
         against those, then the whole file is read through once more for the
         checksum file's SHA-256. The work shown is the two reads together. */
      uint64_t data = ufs2_sums_bytes(usums);
      cv_check_begin(data + size);
      ps5tm_drop_cache(out);
      rc = ufs2_verify_tree(out, usums, &g_cancel, cv_check_progress, NULL, err, sizeof(err));
      if(!rc) {
        ps5tm_drop_cache(out);
        rc = ps5tm_digest_fd(out, size, NULL, sha, &g_cancel, cv_check_progress_after, &data,
                             err, sizeof(err));
      }
      have_sha = !rc;
    }
    ufs2_sums_free(usums);
  } else {
    pfsc_opts_t o = { DEFLATE_LEVEL, WORKERS, &g_cancel, cv_progress, NULL, &wcrc };
    char inner[48];
    snprintf(inner, sizeof(inner), "%s", a.inner);
    rc = pfs_write_single(out, inner, raw, rd, rctx, &o, &size, err, sizeof(err));
    if(!rc) {
      cv_check_begin(size);
      pfsc_check_t c = { wcrc, &g_cancel, cv_check_progress, NULL };
      rc = pfs_verify_full(out, &c, sha, err, sizeof(err));
      have_sha = !rc;
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
  else if(have_sha)
    snprintf(g_job.note, sizeof(g_job.note), "Die Prüfsummen-Datei ließ sich nicht "
             "schreiben. Das Ergebnis selbst ist geprüft und in Ordnung.");
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
  ps5tm_prefetch_stop(pf);
  if(out >= 0) { close(out); unlink(part); }
  if(fsrc.fd >= 0) close(fsrc.fd);
  exfat_image_free(img);
  return NULL;
}


/* --------------------------------------------------------------- interface */

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

  uint64_t raw = 0;
  unsigned files = 1;
  if(folder) {
    exfat_image_t *img = exfat_image_plan(source, serial_for(title_id), err, err_len);
    if(!img) return NULL;
    raw   = exfat_image_size(img);
    files = exfat_image_files(img);
    exfat_image_free(img);
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
    else ffpkg_why[0] = 0;
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
  cJSON_AddStringToObject(root, "stem", stem);
  cJSON_AddNumberToObject(root, "raw_bytes", (double)raw);
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
    if(drive_base(info.volumes[i].path, base, sizeof(base), label, sizeof(label)) != 0 ||
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
  if(ps5tm_gamecopy_busy()) {
    snprintf(err, err_len, "Es läuft gerade eine Kopie. Kopieren, Konvertieren "
             "und Verschieben laufen nicht gleichzeitig.");
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
ps5tm_gameconvert_start(const char *title_id, const char *op, const char *mount,
                        const char *mode, char *err, size_t err_len) {
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

  cv_args_t *a = calloc(1, sizeof(*a));
  if(!a) { snprintf(err, err_len, "Kein Speicher."); return 500; }
  char label[40], dir[128], stem[200];
  a->folder = is_dir(source);
  stem_of(source, a->folder, stem, sizeof(stem));
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
  pthread_mutex_lock(&g_lock);
  int active = active_locked();
  pthread_mutex_unlock(&g_lock);
  if(!active) return -1;
  __atomic_store_n(&g_cancel, 1, __ATOMIC_RELEASE);
  return 0;
}
