/* Checking, unpacking and repairing the images of games (11.10.2026): the three jobs behind "Abbild prüfen", "In einen
 * Ordner entpacken" and "Reparieren" in the window "APR & LZ4" of a game card.
 *
 *   check    Reads the image without mounting it, through imgread.c. An .ffpfsc is first read as a whole container
 *            (conv_pfs.c pfs_verify_full: header, inodes, the whole block table, the areas that must be empty, every block
 *            unpacked — each block carries an Adler-32 that the unpacking checks); then the files inside are looked at:
 *            every chain of every file must lie where the file system says. An .exfat or .ffpkg has no checksums, so every
 *            byte of every file is read (a drive that has gone bad shows there). Is a block damaged, the files are read in
 *            full to name the ones it hits.
 *   unpack   Writes every file of the image to a folder on a drive of the person's choice — into "<folder>.ps5cc-teil"
 *            first, renamed when everything is written — and reads each file back from the drive to compare its CRC-32 with
 *            the one taken while reading it from the image, like the copy of a game does (gamecopy.c).
 *   repair   Only what is safe: an .ffpfsc whose size is not the one its own header declares, with the data whole and the
 *            extra bytes (if any) all zero, is given that size. A damaged block cannot be made again from the image alone.
 *
 * One job at a time, and none beside another job of the app (copy, conversion, move, delete, saved data, packages, file
 * manager, AMPR index): the drive and the CPUs are shared. Everything that touches the image runs on a thread of its
 * own with a big stack: a connection thread's is too small for the readers (the app died once on the first .ffpfsc read
 * on 11.10.2026). The result of the last check of each image is kept in memory for the window; every job also ends with
 * one line in the history (jobhist.c). */
#include <ctype.h>
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
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#ifndef PS5TM_HOST_TEST
#include <sys/mount.h>
#endif

#include "checkfile.h"
#include "conv_pfs.h"
#include "imgread.h"
#include "ioerr.h"
#include "lz4pack.h"
#include "ps5tm.h"
#include "third_party/cJSON.h"
#include "third_party/libdeflate/libdeflate.h"

#define IJ_BUF_BYTES    (1u << 20)
#define IJ_RESERVE      (256ull * 1024 * 1024)     /* never fill a drive up */
#define IJ_FAT32_MAX    0xFFFFFFFFull              /* 4 GiB less one byte */
#define IJ_PART_SUFFIX  ".ps5cc-teil"
#define IJ_BAD_MAX      32                          /* damaged files named in the result */
#define IJ_LAST_MAX     6                           /* images whose last check is kept */
#define IJ_PLAN_TTL_MS  120000
#define IJ_STACK        (768 * 1024)

enum { IJ_IDLE = 0, IJ_RUNNING, IJ_DONE, IJ_FAILED, IJ_CANCELLED };

typedef struct {
  char     path[256];
  uint64_t off;
} bad_t;

typedef struct {
  char     id[16];
  int64_t  when;                 /* unix time of the check */
  int      clean;
  unsigned files, bad_files, bad_dirs;
  uint64_t bytes;
  char     container[320];       /* what the container check said, "" when fine */
  char     note[200];
  unsigned bad_n;
  bad_t    bad[IJ_BAD_MAX];
} last_t;

typedef struct {
  int      state;
  char     kind[10];             /* "check", "unpack", "repair", "lz4pack", "lz4copy", "lz4verify", "lz4unpack" */
  char     id[16], name[128], image[256], format[8];
  char     phase[160], current[256], error[640], dest[320], note[640], container[320];
  uint64_t work_total, work_done, work_window_bytes, window_ms;
  double   rate;
  unsigned files_total, files_done, bad_files, bad_dirs;
  uint64_t bytes_files;          /* the bytes of all files of the image */
  uint64_t started_ms, finished_ms;
  int      clean;                /* check: nothing wrong found */
  unsigned bad_n;
  bad_t    bad[IJ_BAD_MAX];
  uint64_t fixed_from, fixed_to;
  uint64_t lz4_logical, lz4_stored;     /* the LZ4 jobs: bytes of the packed files, bytes they take in the packs */
  unsigned lz4_loose, lz4_packed;       /* files left loose / packed */
  int      lz4_apr_fixed;               /* lz4verify: the APR-EMU that reads packs was put back */
  unsigned lz4_restored, lz4_modes;     /* lz4verify: files written back to the disk from the packs, modes set to 0777 */
} job_t;

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static job_t           g_job;
static int             g_cancel;          /* __atomic */
static last_t          g_last[IJ_LAST_MAX];
static unsigned        g_last_next;

static int
active_locked(void) {
  return g_job.state == IJ_RUNNING;
}

int
ps5tm_imgjob_busy(void) {
  pthread_mutex_lock(&g_lock);
  int b = active_locked();
  pthread_mutex_unlock(&g_lock);
  return b;
}

/* ---------------------------------------------------------------- job bookkeeping */

static void
set_phase(const char *p) {
  pthread_mutex_lock(&g_lock);
  snprintf(g_job.phase, sizeof(g_job.phase), "%s", p);
  g_job.current[0] = 0;
  pthread_mutex_unlock(&g_lock);
}

static void
set_current(const char *p) {
  pthread_mutex_lock(&g_lock);
  snprintf(g_job.current, sizeof(g_job.current), "%s", p);
  pthread_mutex_unlock(&g_lock);
}

static void
set_total(uint64_t total) {
  pthread_mutex_lock(&g_lock);
  g_job.work_total = total;
  pthread_mutex_unlock(&g_lock);
}

static void
work(uint64_t n) {
  uint64_t now = ps5tm_mono_ms();
  pthread_mutex_lock(&g_lock);
  g_job.work_done += n;
  if(!g_job.window_ms) {
    g_job.window_ms = now;
    g_job.work_window_bytes = g_job.work_done;
  } else if(now - g_job.window_ms >= 1000) {
    double inst = (double)(g_job.work_done - g_job.work_window_bytes) * 1000.0 / (double)(now - g_job.window_ms);
    g_job.rate = g_job.rate > 0 ? g_job.rate * 0.7 + inst * 0.3 : inst;
    g_job.window_ms = now;
    g_job.work_window_bytes = g_job.work_done;
  }
  pthread_mutex_unlock(&g_lock);
}

/* A reader reports how far it has come in one file or one pass; this turns it into the bytes since the last report. */
typedef struct { uint64_t last; } acc_t;

static void
acc_cb(void *ctx, uint64_t done) {
  acc_t *a = ctx;
  if(done > a->last) work(done - a->last);
  a->last = done;
}

static int
cancelled(void) {
  return __atomic_load_n(&g_cancel, __ATOMIC_ACQUIRE) != 0;
}

static void
finish(int state, const char *error) {
  pthread_mutex_lock(&g_lock);
  g_job.state = state;
  snprintf(g_job.error, sizeof(g_job.error), "%s", error ? error : "");
  g_job.finished_ms = ps5tm_mono_ms();
  char kind[16], name[128], detail[160], err[640];
  snprintf(kind, sizeof(kind), "img_%s", g_job.kind);
  snprintf(name, sizeof(name), "%s", g_job.name);
  snprintf(err, sizeof(err), "%s", g_job.error);
  const char *result = state == IJ_DONE ? "done" : state == IJ_CANCELLED ? "cancelled" : "failed";
  detail[0] = 0;
  if(!strcmp(g_job.kind, "check")) {
    if(state == IJ_DONE && g_job.clean) snprintf(detail, sizeof(detail), "in Ordnung · %u Dateien", g_job.files_total);
    else if(state == IJ_DONE) {
      result = "failed";
      snprintf(detail, sizeof(detail), "%u Dateien beschädigt", g_job.bad_files);
      if(!err[0]) snprintf(err, sizeof(err), "%s", g_job.container[0] ? g_job.container : "Im Abbild wurden Fehler gefunden.");
    }
  } else if(!strcmp(g_job.kind, "unpack")) {
    snprintf(detail, sizeof(detail), "%u Dateien nach %s", g_job.files_done, g_job.dest);
  } else if(!strncmp(g_job.kind, "lz4", 3)) {
    if(state == IJ_DONE && !strcmp(g_job.kind, "lz4verify")) {
      if(g_job.lz4_apr_fixed || g_job.lz4_restored || g_job.lz4_modes) snprintf(detail, sizeof(detail), "%u Dateien in Ordnung · repariert", g_job.lz4_packed);
      else snprintf(detail, sizeof(detail), "%u Dateien in Ordnung", g_job.lz4_packed);
    }
    else if(state == IJ_DONE && !strcmp(g_job.kind, "lz4unpack")) snprintf(detail, sizeof(detail), "%u Dateien entpackt", g_job.lz4_packed);
    else if(state == IJ_DONE) snprintf(detail, sizeof(detail), "%u Dateien, %.1f → %.1f GB", g_job.lz4_packed, (double)g_job.lz4_logical / 1e9, (double)g_job.lz4_stored / 1e9);
  } else if(state == IJ_DONE) {
    snprintf(detail, sizeof(detail), "%s", g_job.fixed_from == g_job.fixed_to ? "nichts zu reparieren" : "Größe angepasst");
  }
  uint64_t bytes = g_job.bytes_files, dur = g_job.finished_ms - g_job.started_ms;
  pthread_mutex_unlock(&g_lock);
  if(state == IJ_FAILED)
    PS5TM_WARN("img_job_failed", "%s von %.60s gescheitert: %.150s", kind, name, err[0] ? err : "ohne Angabe");
  ps5tm_hist_add(kind, name, detail, result, err, bytes, dur);
}

/* ---------------------------------------------------------------- a thread with a big stack for short readers */

typedef struct { void *(*fn)(void *); void *arg; } big_t;

static void *
big_main(void *p) {
  big_t *b = p;
  b->fn(b->arg);
  return NULL;
}

/* Runs fn(arg) on a thread with IJ_STACK bytes of stack and waits for it. 0, or -1 when no thread could be made. */
static int
run_big(void *(*fn)(void *), void *arg) {
  big_t b = { fn, arg };
  pthread_t th;
  pthread_attr_t at;
  pthread_attr_init(&at);
  pthread_attr_setstacksize(&at, IJ_STACK);
  int rc = pthread_create(&th, &at, big_main, &b);
  pthread_attr_destroy(&at);
  if(rc != 0) return -1;
  pthread_join(th, NULL);
  return 0;
}

/* ---------------------------------------------------------------- what an image is */

typedef struct {
  char     path[256];
  char     kind[8];               /* "exfat", "ffpfsc", "ffpkg", "" when the app cannot read it */
  char     why[200];
  uint64_t size;
  int64_t  mtime;
  /* the outer size of an .ffpfsc */
  int      outer;                  /* -2 not an .ffpfsc, -1 broken, 0 fine, 1 fixable */
  uint64_t declared, actual;
  char     outer_why[200];
} imginfo_t;

static void *
info_thread(void *arg) {
  imginfo_t *x = arg;
  struct stat st;
  x->kind[0] = 0;
  x->outer = -2;
  if(stat(x->path, &st) != 0 || !S_ISREG(st.st_mode)) {
    snprintf(x->why, sizeof(x->why), "Die Abbilddatei ist nicht lesbar.");
    return NULL;
  }
  x->size = (uint64_t)st.st_size;
  x->mtime = (int64_t)st.st_mtime;
  char err[160] = "";
  imgr_t *r = imgr_open(x->path, err, sizeof(err));
  if(!r) {
    /* an .ffpfsc whose nested file system the app does not know still has its container */
    int fd = open(x->path, O_RDONLY);
    if(fd >= 0) {
      char e2[160];
      uint64_t d, a;
      int oc = pfs_outer_check(fd, &d, &a, e2, sizeof(e2));
      if(oc >= 0) {
        snprintf(x->kind, sizeof(x->kind), "ffpfsc");
        x->outer = oc; x->declared = d; x->actual = a;
        snprintf(x->why, sizeof(x->why), "Das Dateisystem im Abbild kennt die App nicht; geprüft werden kann nur der Container.");
      } else snprintf(x->why, sizeof(x->why), "%s", err[0] ? err : "Das Abbild hat kein bekanntes Format.");
      close(fd);
    } else snprintf(x->why, sizeof(x->why), "%s", err[0] ? err : "Das Abbild hat kein bekanntes Format.");
    return NULL;
  }
  snprintf(x->kind, sizeof(x->kind), "%s", imgr_kind(r));
  imgr_close(r);
  if(!strcmp(x->kind, "ffpfsc")) {
    int fd = open(x->path, O_RDONLY);
    if(fd >= 0) {
      x->outer = pfs_outer_check(fd, &x->declared, &x->actual, x->outer_why, sizeof(x->outer_why));
      close(fd);
    } else {
      x->outer = -1;
      snprintf(x->outer_why, sizeof(x->outer_why), "Die Abbilddatei ist nicht lesbar.");
    }
  }
  return NULL;
}

static int
read_info(const ps5tm_libdel_t *li, imginfo_t *x) {
  memset(x, 0, sizeof(*x));
  snprintf(x->path, sizeof(x->path), "%s", li->path);
  return run_big(info_thread, x);
}

/* ---------------------------------------------------------------- the last result of a check */

static void
store_last(void) {
  pthread_mutex_lock(&g_lock);
  const char *id = g_job.id;
  last_t *l = NULL;
  for(unsigned i = 0; i < IJ_LAST_MAX; i++)
    if(!strcmp(g_last[i].id, id)) { l = &g_last[i]; break; }
  if(!l) { l = &g_last[g_last_next++ % IJ_LAST_MAX]; }
  memset(l, 0, sizeof(*l));
  snprintf(l->id, sizeof(l->id), "%s", id);
  l->when = (int64_t)time(NULL);
  l->clean = g_job.clean;
  l->files = g_job.files_total;
  l->bad_files = g_job.bad_files;
  l->bad_dirs = g_job.bad_dirs;
  l->bytes = g_job.bytes_files;
  snprintf(l->container, sizeof(l->container), "%s", g_job.container);
  snprintf(l->note, sizeof(l->note), "%s", g_job.note);
  l->bad_n = g_job.bad_n;
  memcpy(l->bad, g_job.bad, sizeof(l->bad));
  pthread_mutex_unlock(&g_lock);
}

static cJSON *
last_json(const char *id) {
  cJSON *o = NULL;
  pthread_mutex_lock(&g_lock);
  for(unsigned i = 0; i < IJ_LAST_MAX; i++) {
    const last_t *l = &g_last[i];
    if(strcmp(l->id, id)) continue;
    o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "when", (double)l->when);
    cJSON_AddBoolToObject(o, "clean", l->clean);
    cJSON_AddNumberToObject(o, "files", l->files);
    cJSON_AddNumberToObject(o, "bad_files", l->bad_files);
    cJSON_AddNumberToObject(o, "bad_dirs", l->bad_dirs);
    cJSON_AddNumberToObject(o, "bytes", (double)l->bytes);
    if(l->container[0]) cJSON_AddStringToObject(o, "container", l->container);
    if(l->note[0]) cJSON_AddStringToObject(o, "note", l->note);
    cJSON *a = cJSON_AddArrayToObject(o, "bad");
    for(unsigned k = 0; k < l->bad_n; k++) {
      cJSON *b = cJSON_CreateObject();
      cJSON_AddStringToObject(b, "path", l->bad[k].path);
      cJSON_AddNumberToObject(b, "offset", (double)l->bad[k].off);
      cJSON_AddItemToArray(a, b);
    }
    break;
  }
  pthread_mutex_unlock(&g_lock);
  return o;
}

/* ---------------------------------------------------------------- check */

typedef struct {
  imgr_t  *r;
  int      deep;
  unsigned files, dirs;
  uint64_t bytes, largest;
  size_t   max_rel;
  int      damaged_dirs;
} count_t;

static int
count_cb(const imgr_entry_t *e, void *ctx) {
  count_t *c = ctx;
  size_t pl = strlen(e->path);
  if(pl > c->max_rel) c->max_rel = pl;
  if(e->is_dir) {
    if(e->damaged) c->damaged_dirs++;
    else c->dirs++;
    return cancelled();
  }
  c->files++;
  c->bytes += e->size;
  if(e->size > c->largest) c->largest = e->size;
  return cancelled();
}

typedef struct {
  imgr_t *r;
  int     deep;
  int     stop;
} chk_t;

static void
add_bad(const char *path, uint64_t off) {
  pthread_mutex_lock(&g_lock);
  if(g_job.bad_n < IJ_BAD_MAX) {
    snprintf(g_job.bad[g_job.bad_n].path, sizeof(g_job.bad[g_job.bad_n].path), "%s", path);
    g_job.bad[g_job.bad_n].off = off;
    g_job.bad_n++;
  }
  pthread_mutex_unlock(&g_lock);
}

static int
check_cb(const imgr_entry_t *e, void *ctx) {
  chk_t *c = ctx;
  if(cancelled()) { c->stop = 1; return 1; }
  if(e->is_dir) {
    if(e->damaged) {
      pthread_mutex_lock(&g_lock);
      g_job.bad_dirs++;
      pthread_mutex_unlock(&g_lock);
      add_bad(e->path, 0);
    }
    return 0;
  }
  set_current(e->path);
  uint64_t off = 0;
  acc_t acc = { 0 };
  int rc = imgr_verify_entry(c->r, e, c->deep, &off, &g_cancel, c->deep ? acc_cb : NULL, &acc);
  int en = errno;
  if(rc != 0) {
    if(en == ECANCELED && cancelled()) { c->stop = 1; return 1; }
    pthread_mutex_lock(&g_lock);
    g_job.bad_files++;
    pthread_mutex_unlock(&g_lock);
    add_bad(e->path, off);
    if(c->deep && e->size > acc.last) work(e->size - acc.last);        /* the rest of this file is not read */
  } else if(!c->deep) {
    /* nothing was read: the chain lies where it should */
  }
  pthread_mutex_lock(&g_lock);
  g_job.files_done++;
  pthread_mutex_unlock(&g_lock);
  return 0;
}

static void
container_progress(void *ctx, uint64_t done) {
  acc_t *a = ctx;
  acc_cb(a, done);
}

static void *
check_run(void *arg) {
  (void)arg;
  ps5tm_powerguard_hold();
  char img[256], fmt[8];
  pthread_mutex_lock(&g_lock);
  snprintf(img, sizeof(img), "%s", g_job.image);
  snprintf(fmt, sizeof(fmt), "%s", g_job.format);
  pthread_mutex_unlock(&g_lock);
  struct stat st;
  if(stat(img, &st) != 0 || !S_ISREG(st.st_mode)) { finish(IJ_FAILED, "Die Abbilddatei ist nicht lesbar."); goto out; }
  uint64_t image_size = (uint64_t)st.st_size;
  int is_pfsc = !strcmp(fmt, "ffpfsc");
  int container_bad = 0;
  char cerr[320] = "";

  /* 1  the container of an .ffpfsc, all of it */
  if(is_pfsc) {
    set_phase("Der Container wird gelesen");
    set_total(image_size);
    int fd = open(img, O_RDONLY);
    if(fd < 0) { finish(IJ_FAILED, "Die Abbilddatei ist nicht lesbar."); goto out; }
    acc_t acc = { 0 };
    pfsc_check_t pc;
    memset(&pc, 0, sizeof(pc));
    pc.skip_crc = 1;
    pc.cancel = &g_cancel;
    pc.progress = container_progress;
    pc.progress_ctx = &acc;
    int rc = pfs_verify_full(fd, &pc, NULL, cerr, sizeof(cerr));
    int en = errno;
    close(fd);
    if(rc != 0) {
      if(en == ECANCELED || cancelled()) { finish(IJ_CANCELLED, "Abgebrochen."); goto out; }
      container_bad = 1;
      pthread_mutex_lock(&g_lock);
      snprintf(g_job.container, sizeof(g_job.container), "%s", cerr);
      pthread_mutex_unlock(&g_lock);
    }
  }

  /* 2  the file system inside */
  set_phase("Das Abbild wird geöffnet");
  char err[160] = "";
  imgr_t *r = imgr_open(img, err, sizeof(err));
  if(!r) {
    if(is_pfsc && !container_bad) {
      pthread_mutex_lock(&g_lock);
      snprintf(g_job.note, sizeof(g_job.note), "Das Dateisystem im Abbild kennt die App nicht: geprüft ist nur der Container.");
      g_job.clean = 1;
      pthread_mutex_unlock(&g_lock);
      store_last();
      finish(IJ_DONE, NULL);
    } else {
      finish(IJ_FAILED, container_bad ? cerr : (err[0] ? err : "Das Abbild hat kein bekanntes Format."));
    }
    goto out;
  }
  set_phase("Die Dateien werden gezählt");
  count_t cnt;
  memset(&cnt, 0, sizeof(cnt));
  cnt.r = r;
  int wr = imgr_walk(r, count_cb, &cnt);
  if(cancelled()) { imgr_close(r); finish(IJ_CANCELLED, "Abgebrochen."); goto out; }
  if(wr < 0) {
    imgr_close(r);
    finish(IJ_FAILED, container_bad ? cerr : "Die Ordner im Abbild lassen sich nicht lesen.");
    goto out;
  }
  int deep = !is_pfsc || container_bad;
  pthread_mutex_lock(&g_lock);
  g_job.files_total = cnt.files;
  g_job.bytes_files = cnt.bytes;
  g_job.files_done = 0;
  g_job.bad_dirs = (unsigned)cnt.damaged_dirs;
  if(cnt.damaged_dirs) g_job.bad_n = 0;
  g_job.work_total = (is_pfsc ? image_size : 0) + (deep ? cnt.bytes : 0);
  if(g_job.work_total == 0) g_job.work_total = 1;
  g_job.phase[0] = 0;
  pthread_mutex_unlock(&g_lock);
  set_phase(deep ? "Alle Dateien werden gelesen" : "Die Dateien werden geprüft");
  chk_t ck = { r, deep, 0 };
  uint64_t skipped = 0;
  int cw = imgr_walk2(r, check_cb, &ck, &skipped);
  imgr_close(r);
  if(ck.stop || cancelled()) { finish(IJ_CANCELLED, "Abgebrochen."); goto out; }
  if(cw < 0) {                                   /* the walk did not get through: no "in Ordnung" for half the image */
    finish(IJ_FAILED, "Die Prüfung brach mitten im Abbild ab: Die Ordner ließen sich nicht mehr lesen (Laufwerk getrennt?) "
                      "oder das Abbild ist beschädigt.");
    goto out;
  }
  pthread_mutex_lock(&g_lock);
  g_job.clean = !container_bad && g_job.bad_files == 0 && g_job.bad_dirs == 0 && skipped == 0;
  if(skipped)
    snprintf(g_job.note, sizeof(g_job.note), "%llu Einträge im Abbild haben einen ungültigen Namen und wurden nicht geprüft.",
             (unsigned long long)skipped);
  pthread_mutex_unlock(&g_lock);
  store_last();
  PS5TM_INFO("img_check_done", "Abbild geprüft: %s.", g_job.clean ? "in Ordnung" : "Fehler gefunden");
  finish(IJ_DONE, NULL);
out:
  ps5tm_powerguard_release();
  return NULL;
}

/* ---------------------------------------------------------------- unpack */

typedef struct {
  imgr_t  *r;
  char    *tmp;                   /* the folder written into, no trailing slash */
  size_t   tmp_len;
  char    *dst;                   /* the path of the entry being written */
  uint8_t *buf;
  int      rc;                    /* 0, or the errno that stopped the job (SRC_SIDE or'ed on for the image) */
  char     why[300];
  unsigned made_dirs;
} unp_t;

#define IJ_SRC 0x10000

static void
unp_fail(unp_t *u, int code, const char *what, const char *path) {
  if(u->rc) return;
  u->rc = code ? code : EIO;
  int src = u->rc & IJ_SRC, e = u->rc & ~IJ_SRC;
  if(e == ECANCELED) { snprintf(u->why, sizeof(u->why), "Abgebrochen."); return; }
  if(src) snprintf(u->why, sizeof(u->why), "%s im Abbild lässt sich nicht lesen: %s", path, "das Abbild ist beschädigt oder das Laufwerk hat einen Lesefehler.");
  else if(e == ENOSPC) snprintf(u->why, sizeof(u->why), "Auf dem Ziel ist kein Platz mehr (%s).", path);
  else if(e == EEXIST) snprintf(u->why, sizeof(u->why), "%s gibt es schon: im Abbild unterscheiden sich Namen nur in Groß-/Kleinschreibung, das Ziel kann das nicht.", path);
  else if(e == ENAMETOOLONG) snprintf(u->why, sizeof(u->why), "Ein Name ist für das Ziel zu lang: %s", path);
  else if(e == 1000) snprintf(u->why, sizeof(u->why), "Beim Zurücklesen weicht %s auf dem Ziel von dem ab, was aus dem Abbild gelesen wurde. Ist das Ziel in Ordnung?", path);
  else snprintf(u->why, sizeof(u->why), "%s: %s", what, ps5tm_io_strerror(e));
}

static int
unp_cb(const imgr_entry_t *e, void *ctx) {
  unp_t *u = ctx;
  if(u->rc) return 1;
  if(cancelled()) { unp_fail(u, ECANCELED, "", ""); return 1; }
  size_t pl = strlen(e->path);
  if(u->tmp_len + pl >= PATH_MAX) { unp_fail(u, ENAMETOOLONG, "", e->path); return 1; }
  memcpy(u->dst, u->tmp, u->tmp_len);
  memcpy(u->dst + u->tmp_len, e->path, pl + 1);
  if(e->is_dir) {
    if(e->damaged) { unp_fail(u, EIO | IJ_SRC, "", e->path); return 1; }
    if(mkdir(u->dst, 0777) == 0) (void)chmod(u->dst, LZP_FILE_MODE);
    else if(errno != EEXIST) { unp_fail(u, errno, "Ordner anlegen", e->path); return 1; }
    u->made_dirs++;
    return 0;
  }
  set_current(e->path);
  int out = open(u->dst, O_WRONLY | O_CREAT | O_EXCL, LZP_FILE_MODE);         /* a game folder: 0777 (lz4pack.h) */
  if(out < 0) { unp_fail(u, errno, "Datei anlegen", e->path); return 1; }
  (void)fchmod(out, LZP_FILE_MODE);
  uint32_t crc = 0;
  uint64_t off = 0;
  int rc = 0;
  while(off < e->size) {
    if(cancelled()) { rc = ECANCELED; break; }
    size_t n = e->size - off < IJ_BUF_BYTES ? (size_t)(e->size - off) : IJ_BUF_BYTES;
    long got = imgr_read_entry(u->r, e, off, u->buf, n);
    if(got != (long)n) { rc = EIO | IJ_SRC; break; }
    crc = libdeflate_crc32(crc, u->buf, n);
    for(size_t done = 0; done < n;) {
      ssize_t w = write(out, u->buf + done, n - done);
      if(w < 0) { if(errno == EINTR) continue; rc = errno ? errno : EIO; break; }
      if(w == 0) { rc = EIO; break; }
      done += (size_t)w;
    }
    if(rc) break;
    off += n;
    work(n);
#ifdef IJ_TEST_DELAY_US
    usleep(IJ_TEST_DELAY_US);                       /* the host test slows the job down to cancel it in the middle */
#endif
  }
  if(!rc && fsync(out) != 0) rc = errno ? errno : EIO;
  if(close(out) != 0 && !rc) rc = errno ? errno : EIO;
  if(!rc && e->size > 0) {
    /* read back from the drive, not from memory */
    int in = open(u->dst, O_RDONLY);
    if(in < 0) rc = errno ? errno : EIO;
    else {
      ps5tm_drop_cache(in);
      uint32_t got = 0;
      char why[96];
      acc_t acc = { 0 };
      int dr = ps5tm_digest_fd(in, e->size, &got, NULL, &g_cancel, acc_cb, &acc, why, sizeof(why));
      int en = errno;
      close(in);
      if(dr) rc = en == ECANCELED ? ECANCELED : (en ? en : EIO);
      else if(got != crc) rc = 1000;
    }
  }
  if(rc) { unlink(u->dst); unp_fail(u, rc, "Datei schreiben", e->path); return 1; }
  pthread_mutex_lock(&g_lock);
  g_job.files_done++;
  pthread_mutex_unlock(&g_lock);
  return 0;
}

/* Removes a folder this job made — never anything else: the caller only passes the temporary folder it created, or one
   that carries this app's suffix. Looked at with lstat, a link is unlinked and never entered. */
static void
rm_tree(const char *path, int depth) {
  if(depth > 60) return;
  DIR *d = opendir(path);
  if(d) {
    struct dirent *de;
    while((de = readdir(d)) != NULL) {
      if(!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
      size_t n = strlen(path) + strlen(de->d_name) + 2;
      char *child = malloc(n);
      if(!child) continue;
      snprintf(child, n, "%s/%s", path, de->d_name);
      struct stat st;
      if(lstat(child, &st) == 0 && S_ISDIR(st.st_mode)) rm_tree(child, depth + 1);
      else unlink(child);
      free(child);
    }
    closedir(d);
  }
  rmdir(path);
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
  return 0;
}

static int
drive_free(const char *base, uint64_t *avail, char *fstype, size_t fstype_len) {
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

static int
folder_name_ok(const char *s) {
  return s && s[0] && strcmp(s, ".") && strcmp(s, "..") && !strchr(s, '/') && strlen(s) < 200;
}

/* "/mnt/usb0/homebrew/Name" for a drive mount and an image file: the name of the image without its extension. */
static int
dest_for(const char *mount, const char *image, const char *id, char *out, size_t out_len, char *base_out, size_t base_len,
         char *label, size_t label_len) {
  ps5tm_sysinfo_t info;
  ps5tm_sysinfo_get(&info);
  for(unsigned i = 0; i < info.volume_count; i++) {
    if(strcmp(info.volumes[i].path, mount)) continue;
    char base[64];
    snprintf(base, sizeof(base), "%s", !strcmp(mount, "/user") ? "/data" : mount);
    if(base_out) snprintf(base_out, base_len, "%s", base);
    if(label) snprintf(label, label_len, "%s", info.volumes[i].label);
    const char *f = strrchr(image, '/');
    char stem[200];
    snprintf(stem, sizeof(stem), "%s", f ? f + 1 : image);
    char *dot = strrchr(stem, '.');
    if(dot && dot != stem) *dot = 0;
    if(!folder_name_ok(stem)) snprintf(stem, sizeof(stem), "%s", id);
    int n = snprintf(out, out_len, "%s/homebrew/%s", base, stem);
    return (n > 0 && (size_t)n < out_len) ? 0 : -1;
  }
  return -1;
}

static void *
unpack_run(void *arg) {
  char *mount = arg;
  ps5tm_powerguard_hold();
  char img[256], id[16];
  pthread_mutex_lock(&g_lock);
  snprintf(img, sizeof(img), "%s", g_job.image);
  snprintf(id, sizeof(id), "%s", g_job.id);
  pthread_mutex_unlock(&g_lock);
  unp_t u;
  memset(&u, 0, sizeof(u));
  char dest[320], base[64], label[40];
  if(dest_for(mount, img, id, dest, sizeof(dest), base, sizeof(base), label, sizeof(label)) != 0) {
    finish(IJ_FAILED, "Dieses Laufwerk gibt es nicht (mehr).");
    goto out;
  }
  pthread_mutex_lock(&g_lock);
  snprintf(g_job.dest, sizeof(g_job.dest), "%s", dest);
  pthread_mutex_unlock(&g_lock);
  char err[160] = "";
  set_phase("Das Abbild wird geöffnet");
  imgr_t *r = imgr_open(img, err, sizeof(err));
  if(!r) { finish(IJ_FAILED, err[0] ? err : "Das Abbild hat kein bekanntes Format."); goto out; }

  set_phase("Die Dateien werden gezählt");
  count_t cnt;
  memset(&cnt, 0, sizeof(cnt));
  cnt.r = r;
  int wr = imgr_walk(r, count_cb, &cnt);
  if(cancelled()) { imgr_close(r); finish(IJ_CANCELLED, "Abgebrochen."); goto out; }
  if(wr < 0 || cnt.damaged_dirs) {
    imgr_close(r);
    finish(IJ_FAILED, "Das Abbild ist beschädigt: ein Ordner darin lässt sich nicht lesen. Zuerst „Abbild prüfen“.");
    goto out;
  }
  pthread_mutex_lock(&g_lock);
  g_job.files_total = cnt.files;
  g_job.bytes_files = cnt.bytes;
  g_job.work_total = cnt.bytes * 2 ? cnt.bytes * 2 : 1;          /* written, then read back */
  pthread_mutex_unlock(&g_lock);

  /* room, name, FAT32 */
  char fstype[16];
  uint64_t avail = 0;
  struct stat st;
  char tmp[340];
  snprintf(tmp, sizeof(tmp), "%s" IJ_PART_SUFFIX, dest);
  if(lstat(dest, &st) == 0) {
    imgr_close(r);
    finish(IJ_FAILED, "Den Zielordner gibt es schon. Bitte ihn zuerst umbenennen oder löschen.");
    goto out;
  }
  if(lstat(tmp, &st) == 0 && S_ISDIR(st.st_mode)) rm_tree(tmp, 0);                /* an unfinished unpack of this app */
  if(drive_free(base, &avail, fstype, sizeof(fstype)) != 0) {
    imgr_close(r);
    finish(IJ_FAILED, "Das Ziellaufwerk lässt sich nicht lesen.");
    goto out;
  }
  if(avail < cnt.bytes + IJ_RESERVE) {
    imgr_close(r);
    char m[200];
    snprintf(m, sizeof(m), "Auf dem Ziel ist nicht genug Platz: nötig sind %.1f GB, frei sind %.1f GB.",
             (double)(cnt.bytes + IJ_RESERVE) / 1e9, (double)avail / 1e9);
    finish(IJ_FAILED, m);
    goto out;
  }
  if(!strcmp(fstype, "msdosfs") && cnt.largest > IJ_FAT32_MAX) {
    imgr_close(r);
    finish(IJ_FAILED, "Das Ziel ist FAT32: Dateien über 4 GB passen nicht darauf.");
    goto out;
  }
  if(strlen(tmp) + cnt.max_rel + 1 >= PATH_MAX) {
    imgr_close(r);
    finish(IJ_FAILED, "Ein Pfad im Abbild wäre auf dem Ziel zu lang.");
    goto out;
  }
  int mk = mkdir_p(tmp);
  if(mk == 0 && mkdir(tmp, 0777) != 0) mk = errno ? errno : EIO;
  if(mk == 0) (void)chmod(tmp, LZP_FILE_MODE);
  if(mk != 0) {
    imgr_close(r);
    char m[300];
    snprintf(m, sizeof(m), "Der Zielordner lässt sich nicht anlegen: %s", ps5tm_io_strerror(mk));
    finish(IJ_FAILED, m);
    goto out;
  }

  u.r = r;
  u.tmp = tmp;
  u.tmp_len = strlen(tmp);
  u.dst = malloc(PATH_MAX + 1);
  u.buf = malloc(IJ_BUF_BYTES);
  if(!u.dst || !u.buf) {
    free(u.dst); free(u.buf);
    imgr_close(r);
    rm_tree(tmp, 0);
    finish(IJ_FAILED, "Kein Speicher.");
    goto out;
  }
  pthread_mutex_lock(&g_lock);
  g_job.work_done = 0;
  g_job.files_done = 0;
  pthread_mutex_unlock(&g_lock);
  set_phase("Die Dateien werden geschrieben");
  uint64_t skipped = 0;
  int uw = imgr_walk2(r, unp_cb, &u, &skipped);
  imgr_close(r);
  free(u.dst);
  free(u.buf);
  if(u.rc) {
    rm_tree(tmp, 0);                                   /* created above with mkdir: ours */
    finish((u.rc & ~IJ_SRC) == ECANCELED ? IJ_CANCELLED : IJ_FAILED, u.why);
    goto out;
  }
  pthread_mutex_lock(&g_lock);
  unsigned written = g_job.files_done;
  pthread_mutex_unlock(&g_lock);
  if(uw != 0 || skipped || written != cnt.files) {   /* a folder half written is no game: never renamed into place */
    rm_tree(tmp, 0);
    char m[300];
    if(skipped)
      snprintf(m, sizeof(m), "%llu Einträge im Abbild haben einen ungültigen Namen; ohne sie wäre das Spiel unvollständig. Nichts wurde behalten.",
               (unsigned long long)skipped);
    else
      snprintf(m, sizeof(m), "Das Entpacken brach mitten im Abbild ab (%u von %u Dateien): Die Ordner ließen sich nicht mehr lesen "
               "(Laufwerk getrennt?). Nichts wurde behalten.", written, cnt.files);
    finish(IJ_FAILED, m);
    goto out;
  }
  set_phase("Der Ordner wird fertiggestellt");
  if(rename(tmp, dest) != 0) {
    int e = errno ? errno : EIO;
    rm_tree(tmp, 0);
    char m[300];
    snprintf(m, sizeof(m), "Der fertige Ordner ließ sich nicht umbenennen: %s", ps5tm_io_strerror(e));
    finish(IJ_FAILED, m);
    goto out;
  }
  PS5TM_INFO("img_unpack_done", "Abbild von %s entpackt: %u Dateien nach %s.", g_job.name, g_job.files_done, dest);
  finish(IJ_DONE, NULL);
out:
  free(mount);
  ps5tm_powerguard_release();
  return NULL;
}

/* ---------------------------------------------------------------- repair */

static void *
repair_run(void *arg) {
  (void)arg;
  ps5tm_powerguard_hold();
  char img[256];
  pthread_mutex_lock(&g_lock);
  snprintf(img, sizeof(img), "%s", g_job.image);
  pthread_mutex_unlock(&g_lock);
  set_phase("Die Größe des Abbilds wird geprüft");
  int fd = open(img, O_RDWR);
  if(fd < 0) {
    char m[300];
    snprintf(m, sizeof(m), "Die Abbilddatei lässt sich nicht zum Schreiben öffnen: %s", ps5tm_io_strerror(errno));
    finish(IJ_FAILED, m);
    goto out;
  }
  char err[300] = "";
  uint64_t declared = 0, actual = 0;
  int oc = pfs_outer_check(fd, &declared, &actual, err, sizeof(err));
  pthread_mutex_lock(&g_lock);
  g_job.fixed_from = actual;
  g_job.fixed_to = declared;
  pthread_mutex_unlock(&g_lock);
  if(oc < 0) { close(fd); finish(IJ_FAILED, err); goto out; }
  if(oc == 0) {
    close(fd);
    pthread_mutex_lock(&g_lock);
    g_job.fixed_to = g_job.fixed_from;
    snprintf(g_job.note, sizeof(g_job.note), "Die Größe stimmt schon: nichts zu reparieren.");
    pthread_mutex_unlock(&g_lock);
    finish(IJ_DONE, NULL);
    goto out;
  }
  set_phase("Die Größe wird angepasst");
  if(ftruncate(fd, (off_t)declared) != 0 || fsync(fd) != 0) {
    int e = errno ? errno : EIO;
    close(fd);
    char m[300];
    snprintf(m, sizeof(m), "Die Größe ließ sich nicht ändern: %s", ps5tm_io_strerror(e));
    finish(IJ_FAILED, m);
    goto out;
  }
  close(fd);
  /* and proof: the container is read once more, all of it */
  set_phase("Der Container wird noch einmal geprüft");
  set_total(declared ? declared : 1);
  int rd = open(img, O_RDONLY);
  if(rd < 0) { finish(IJ_FAILED, "Die Abbilddatei ist nach der Reparatur nicht lesbar."); goto out; }
  acc_t acc = { 0 };
  pfsc_check_t pc;
  memset(&pc, 0, sizeof(pc));
  pc.skip_crc = 1;
  pc.cancel = &g_cancel;
  pc.progress = container_progress;
  pc.progress_ctx = &acc;
  char verr[320] = "";
  int vr = pfs_verify_full(rd, &pc, NULL, verr, sizeof(verr));
  int ven = errno;
  close(rd);
  if(vr != 0) {
    if(ven == ECANCELED) { finish(IJ_CANCELLED, "Abgebrochen. Die Größe ist schon angepasst."); goto out; }
    char m[400];
    snprintf(m, sizeof(m), "Die Größe ist angepasst, doch die Prüfung findet noch etwas: %s", verr);
    finish(IJ_FAILED, m);
    goto out;
  }
  PS5TM_INFO("img_repair_done", "Abbild von %s repariert: Größe von %llu auf %llu Bytes.", g_job.name,
             (unsigned long long)actual, (unsigned long long)declared);
  finish(IJ_DONE, NULL);
out:
  ps5tm_powerguard_release();
  return NULL;
}

/* ---------------------------------------------------------------- the page's side */

cJSON *
ps5tm_imgjob_info(const char *id, int *http) {
  *http = 200;
  ps5tm_libdel_t li;
  if(!id || ps5tm_library_delete_info(id, &li) != 0) { *http = 404; return NULL; }
  cJSON *o = cJSON_CreateObject();
  cJSON_AddBoolToObject(o, "ok", 1);
  cJSON_AddStringToObject(o, "id", id);
  cJSON_AddStringToObject(o, "name", li.name);
  const char *why = NULL;
  imginfo_t *x = calloc(1, sizeof(*x));
  int is_image = strcmp(li.format, "folder") && strcmp(li.format, "pkg") && li.path[0] == '/';
  if(!is_image) {
    why = !strcmp(li.format, "pkg") ? "Installierte Spiele (PKG) haben kein Abbild."
        : !strcmp(li.format, "folder") ? "Dieses Spiel liegt als Ordner vor, nicht als Abbild."
        : "Wo die Spieldaten liegen, ist nicht bekannt.";
    cJSON_AddStringToObject(o, "format", li.format);
  } else if(!x || read_info(&li, x) != 0) {
    why = "Das Abbild lässt sich gerade nicht lesen.";
    cJSON_AddStringToObject(o, "format", li.format);
  } else {
    cJSON_AddStringToObject(o, "format", x->kind[0] ? x->kind : li.format);
    cJSON_AddNumberToObject(o, "image_bytes", (double)x->size);
    if(!x->kind[0]) why = x->why;
  }
  int can_check = is_image && x && x->kind[0];
  int can_unpack = can_check && !x->why[0];                        /* not an .ffpfsc whose file system the app cannot read */
  cJSON_AddBoolToObject(o, "can_check", can_check);
  cJSON_AddBoolToObject(o, "can_unpack", can_unpack);
  if(why) cJSON_AddStringToObject(o, "why", why);
  else if(can_check && x->why[0]) cJSON_AddStringToObject(o, "note", x->why);
  cJSON *rp = cJSON_AddObjectToObject(o, "repair");
  if(x && x->outer == 0) { cJSON_AddStringToObject(rp, "state", "fine"); }
  else if(x && x->outer == 1) {
    cJSON_AddStringToObject(rp, "state", "fixable");
    cJSON_AddNumberToObject(rp, "declared", (double)x->declared);
    cJSON_AddNumberToObject(rp, "actual", (double)x->actual);
  } else if(x && x->outer == -1) {
    cJSON_AddStringToObject(rp, "state", "broken");
    cJSON_AddStringToObject(rp, "why", x->outer_why);
  } else cJSON_AddStringToObject(rp, "state", "na");
  cJSON *last = last_json(id);
  if(last) cJSON_AddItemToObject(o, "last_check", last);
  free(x);
  return o;
}

/* what an unpack would write, counted once per image and kept for a while */
typedef struct {
  char     path[256];
  uint64_t size;
  int64_t  mtime;
  uint64_t at_ms;
  int      valid;
  unsigned files;
  uint64_t bytes, largest;
  size_t   max_rel;
  int      damaged;
  int      err;
} plan_cache_t;

static plan_cache_t g_plan;

static void *
plan_thread(void *arg) {
  plan_cache_t *p = arg;
  imgr_t *r = imgr_open(p->path, NULL, 0);
  if(!r) { p->err = 1; return NULL; }
  count_t c;
  memset(&c, 0, sizeof(c));
  c.r = r;
  int wr = imgr_walk(r, count_cb, &c);
  imgr_close(r);
  p->files = c.files; p->bytes = c.bytes; p->largest = c.largest; p->max_rel = c.max_rel; p->damaged = c.damaged_dirs;
  p->err = wr < 0;
  return NULL;
}

cJSON *
ps5tm_imgjob_plan(const char *id, char *err, size_t err_len) {
  ps5tm_libdel_t li;
  if(!id || ps5tm_library_delete_info(id, &li) != 0) { snprintf(err, err_len, "Das Spiel ist nicht in der Liste."); return NULL; }
  struct stat st;
  if(!strcmp(li.format, "folder") || !strcmp(li.format, "pkg") || li.path[0] != '/' || stat(li.path, &st) != 0 || !S_ISREG(st.st_mode)) {
    snprintf(err, err_len, "Dieses Spiel liegt nicht als Abbild vor.");
    return NULL;
  }
  plan_cache_t *p = calloc(1, sizeof(*p));
  if(!p) { snprintf(err, err_len, "Kein Speicher."); return NULL; }
  uint64_t now = ps5tm_mono_ms();
  pthread_mutex_lock(&g_lock);
  int hit = g_plan.valid && !strcmp(g_plan.path, li.path) && g_plan.size == (uint64_t)st.st_size && g_plan.mtime == (int64_t)st.st_mtime &&
            now - g_plan.at_ms < IJ_PLAN_TTL_MS;
  if(hit) *p = g_plan;
  pthread_mutex_unlock(&g_lock);
  if(!hit) {
    snprintf(p->path, sizeof(p->path), "%s", li.path);
    p->size = (uint64_t)st.st_size;
    p->mtime = (int64_t)st.st_mtime;
    if(run_big(plan_thread, p) != 0 || p->err) {
      free(p);
      snprintf(err, err_len, "Das Abbild lässt sich nicht lesen.");
      return NULL;
    }
    p->valid = 1;
    p->at_ms = now;
    pthread_mutex_lock(&g_lock);
    g_plan = *p;
    pthread_mutex_unlock(&g_lock);
  }
  cJSON *root = cJSON_CreateObject();
  cJSON_AddBoolToObject(root, "ok", 1);
  cJSON_AddStringToObject(root, "id", id);
  cJSON_AddStringToObject(root, "name", li.name);
  cJSON_AddStringToObject(root, "image", li.path);
  cJSON_AddNumberToObject(root, "size_bytes", (double)p->bytes);
  cJSON_AddNumberToObject(root, "files", p->files);
  cJSON_AddNumberToObject(root, "largest_bytes", (double)p->largest);
  cJSON_AddBoolToObject(root, "damaged", p->damaged != 0);
  cJSON_AddBoolToObject(root, "busy", ps5tm_imgjob_busy());
  ps5tm_sysinfo_t info;
  ps5tm_sysinfo_get(&info);
  uint64_t need = p->bytes + IJ_RESERVE;
  cJSON *arr = cJSON_AddArrayToObject(root, "targets");
  for(unsigned i = 0; i < info.volume_count; i++) {
    const char *mount = info.volumes[i].path;
    char dest[320], base[64], label[40];
    if(dest_for(mount, li.path, id, dest, sizeof(dest), base, sizeof(base), label, sizeof(label)) != 0) continue;
    uint64_t avail = 0;
    char fstype[16];
    if(drive_free(base, &avail, fstype, sizeof(fstype)) != 0) continue;
    struct stat ds;
    cJSON *d = cJSON_CreateObject();
    cJSON_AddStringToObject(d, "mount", mount);
    cJSON_AddStringToObject(d, "label", label);
    cJSON_AddStringToObject(d, "base", base);
    cJSON_AddStringToObject(d, "fs", fstype);
    cJSON_AddStringToObject(d, "path", dest);
    cJSON_AddNumberToObject(d, "free_bytes", (double)avail);
    cJSON_AddBoolToObject(d, "exists", lstat(dest, &ds) == 0);
    cJSON_AddBoolToObject(d, "enough_space", avail >= need);
    cJSON_AddBoolToObject(d, "fat32_too_big", !strcmp(fstype, "msdosfs") && p->largest > IJ_FAT32_MAX);
    cJSON_AddBoolToObject(d, "path_too_long", strlen(dest) + sizeof(IJ_PART_SUFFIX) + p->max_rel + 1 >= PATH_MAX);
    cJSON_AddBoolToObject(d, "source_here", !strncmp(li.path, base, strlen(base)) && li.path[strlen(base)] == '/');
    cJSON_AddItemToArray(arr, d);
  }
  free(p);
  return root;
}

static int
other_job(char *err, size_t n) {
  if(ps5tm_gamecopy_busy() || ps5tm_gameconvert_busy() || ps5tm_gamemove_busy() || ps5tm_saves_busy() ||
     ps5tm_pkgsplit_busy() || ps5tm_pkginst_busy() || ps5tm_gamedelete_busy() || ps5tm_filemgr_busy() || ps5tm_ampr_busy()) {
    snprintf(err, n, "Es läuft gerade ein anderer Vorgang der App. Bitte danach noch einmal.");
    return 1;
  }
  return 0;
}

int
ps5tm_imgjob_start(const char *kind, const char *id, const char *mount, char *err, size_t n) {
  int is_check = kind && !strcmp(kind, "check"), is_unpack = kind && !strcmp(kind, "unpack"), is_repair = kind && !strcmp(kind, "repair");
  if(!is_check && !is_unpack && !is_repair) { snprintf(err, n, "Unbekannter Vorgang."); return 400; }
  ps5tm_libdel_t li;
  if(!id || ps5tm_library_delete_info(id, &li) != 0) { snprintf(err, n, "Das Spiel ist nicht in der Liste."); return 404; }
  if(!strcmp(li.format, "folder") || !strcmp(li.format, "pkg") || li.path[0] != '/') {
    snprintf(err, n, "Dieses Spiel liegt nicht als Abbild vor.");
    return 409;
  }
  if(is_unpack && (!mount || !mount[0])) { snprintf(err, n, "Es fehlt das Ziellaufwerk."); return 400; }
  if(ps5tm_procmgr_title_running(id) == 1) { snprintf(err, n, "Das Spiel läuft gerade. Bitte zuerst beenden."); return 409; }
  if(is_repair) {
    ps5tm_smp_game_t s;
    if(ps5tm_smp_find(id, &s) == 0 && s.mounted) {
      snprintf(err, n, "ShadowMountPlus hat das Abbild gerade eingehängt. Zum Reparieren muss es frei sein.");
      return 409;
    }
  }
  imginfo_t *x = calloc(1, sizeof(*x));
  if(!x) { snprintf(err, n, "Kein Speicher."); return 500; }
  if(read_info(&li, x) != 0) { free(x); snprintf(err, n, "Das Abbild lässt sich gerade nicht lesen."); return 500; }
  if(!x->kind[0]) { snprintf(err, n, "%s", x->why[0] ? x->why : "Das Abbild hat kein bekanntes Format."); free(x); return 409; }
  if(is_unpack && x->why[0]) { snprintf(err, n, "%s", x->why); free(x); return 409; }
  if(is_repair && strcmp(x->kind, "ffpfsc")) {
    free(x);
    snprintf(err, n, "Repariert wird nur ein komprimiertes Abbild (ffpfsc).");
    return 409;
  }
  char kind_name[8], image[256], name[128];
  snprintf(kind_name, sizeof(kind_name), "%s", x->kind);
  snprintf(image, sizeof(image), "%s", li.path);
  snprintf(name, sizeof(name), "%s", li.name);
  free(x);
  pthread_mutex_lock(&g_lock);
  if(active_locked()) { pthread_mutex_unlock(&g_lock); snprintf(err, n, "Es läuft schon ein Vorgang an einem Abbild."); return 409; }
  memset(&g_job, 0, sizeof(g_job));
  g_job.state = IJ_RUNNING;
  snprintf(g_job.kind, sizeof(g_job.kind), "%s", kind);
  snprintf(g_job.id, sizeof(g_job.id), "%s", id);
  snprintf(g_job.name, sizeof(g_job.name), "%s", name);
  snprintf(g_job.image, sizeof(g_job.image), "%s", image);
  snprintf(g_job.format, sizeof(g_job.format), "%s", kind_name);
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

  char *arg = is_unpack ? strdup(mount) : NULL;
  if(is_unpack && !arg) { finish(IJ_FAILED, "Kein Speicher."); snprintf(err, n, "Kein Speicher."); return 500; }
  pthread_t th;
  pthread_attr_t at;
  pthread_attr_init(&at);
  pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
  pthread_attr_setstacksize(&at, IJ_STACK);
  int rc = pthread_create(&th, &at, is_check ? check_run : is_unpack ? unpack_run : repair_run, arg);
  pthread_attr_destroy(&at);
  if(rc != 0) {
    free(arg);
    finish(IJ_FAILED, "Der Vorgang ließ sich nicht starten.");
    snprintf(err, n, "Der Vorgang ließ sich nicht starten.");
    return 500;
  }
  PS5TM_INFO("img_job_start", "Abbild-Vorgang gestartet: %s von %s (%s).", kind, name, id);
  return 200;
}

cJSON *
ps5tm_imgjob_status(void) {
  cJSON *o = cJSON_CreateObject();
  pthread_mutex_lock(&g_lock);
  static const char *const names[] = { "idle", "running", "done", "failed", "cancelled" };
  cJSON_AddBoolToObject(o, "ok", 1);
  cJSON_AddStringToObject(o, "state", names[g_job.state]);
  cJSON_AddBoolToObject(o, "active", g_job.state == IJ_RUNNING);
  cJSON_AddStringToObject(o, "kind", g_job.kind);
  cJSON_AddStringToObject(o, "id", g_job.id);
  cJSON_AddStringToObject(o, "name", g_job.name);
  cJSON_AddStringToObject(o, "format", g_job.format);
  cJSON_AddStringToObject(o, "phase", g_job.phase);
  cJSON_AddStringToObject(o, "current", g_job.current);
  if(g_job.error[0]) cJSON_AddStringToObject(o, "error", g_job.error);
  if(g_job.note[0]) cJSON_AddStringToObject(o, "note", g_job.note);
  if(g_job.container[0]) cJSON_AddStringToObject(o, "container", g_job.container);
  if(g_job.dest[0]) cJSON_AddStringToObject(o, "dest", g_job.dest);
  cJSON_AddNumberToObject(o, "work_total", (double)g_job.work_total);
  cJSON_AddNumberToObject(o, "work_done", (double)g_job.work_done);
  cJSON_AddNumberToObject(o, "files_total", g_job.files_total);
  cJSON_AddNumberToObject(o, "files_done", g_job.files_done);
  cJSON_AddNumberToObject(o, "bad_files", g_job.bad_files);
  cJSON_AddNumberToObject(o, "bad_dirs", g_job.bad_dirs);
  cJSON_AddNumberToObject(o, "bytes_files", (double)g_job.bytes_files);
  cJSON_AddBoolToObject(o, "clean", g_job.clean);
  cJSON_AddNumberToObject(o, "fixed_from", (double)g_job.fixed_from);
  cJSON_AddNumberToObject(o, "fixed_to", (double)g_job.fixed_to);
  if(!strncmp(g_job.kind, "lz4", 3)) {
    cJSON_AddNumberToObject(o, "lz4_logical", (double)g_job.lz4_logical);
    cJSON_AddNumberToObject(o, "lz4_stored", (double)g_job.lz4_stored);
    cJSON_AddNumberToObject(o, "lz4_loose", g_job.lz4_loose);
    cJSON_AddNumberToObject(o, "lz4_packed", g_job.lz4_packed);
    if(!strcmp(g_job.kind, "lz4verify")) {
      cJSON_AddBoolToObject(o, "lz4_apr_fixed", g_job.lz4_apr_fixed);
      cJSON_AddNumberToObject(o, "lz4_restored", g_job.lz4_restored);
      cJSON_AddNumberToObject(o, "lz4_modes", g_job.lz4_modes);
    }
  }
  cJSON *a = cJSON_AddArrayToObject(o, "bad");
  for(unsigned k = 0; k < g_job.bad_n; k++) {
    cJSON *b = cJSON_CreateObject();
    cJSON_AddStringToObject(b, "path", g_job.bad[k].path);
    cJSON_AddNumberToObject(b, "offset", (double)g_job.bad[k].off);
    cJSON_AddItemToArray(a, b);
  }
  uint64_t end = g_job.state == IJ_RUNNING ? ps5tm_mono_ms() : g_job.finished_ms;
  double elapsed = g_job.started_ms ? (double)(end - g_job.started_ms) / 1000.0 : 0;
  cJSON_AddNumberToObject(o, "elapsed_s", elapsed);
  double rate = g_job.rate;
  if(g_job.state == IJ_RUNNING && rate > 1 && g_job.work_total > g_job.work_done) {
    cJSON_AddNumberToObject(o, "bytes_per_s", rate);
    cJSON_AddNumberToObject(o, "eta_s", (double)(g_job.work_total - g_job.work_done) / rate);
  }
  pthread_mutex_unlock(&g_lock);
  return o;
}

int
ps5tm_imgjob_cancel(void) {
  pthread_mutex_lock(&g_lock);
  int act = active_locked();
  pthread_mutex_unlock(&g_lock);
  if(!act) return 0;
  __atomic_store_n(&g_cancel, 1, __ATOMIC_RELEASE);
  return 1;
}

#include "imgjob_lz4.inc"
