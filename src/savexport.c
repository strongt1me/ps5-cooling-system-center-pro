/* PS5 Cooling & System Center - Pro: a save of this console, read out into a ZIP (savexport.h). */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <time.h>
#include <unistd.h>

#include "chives.h"
#include "ioerr.h"
#include "savemnt.h"
#include "savesfo.h"
#include "savexport.h"
#include "savezip.h"

#define SX_BUF      (1u << 20)
#define SX_MAX_ENT  5000u
#define SX_REL_MAX  200u
#define SX_DEPTH    8
#define SX_MIN_FREE (64ull << 20)
#define SX_ZIP_MAX  (3500ull << 20)                    /* a ZIP without ZIP64 stops at 4 GiB: stay well below */

typedef struct {
  char     rel[SX_REL_MAX];
  uint64_t size;
  uint32_t uid, gid;
  uint16_t mode;
  time_t   mtime;
  int      dir;
} ent_t;

typedef struct {
  ent_t   *v;
  unsigned n, cap;
  uint64_t bytes;
  unsigned skipped;
} list_t;

typedef struct {
  const savexport_hooks_t *h;
  char    *err;
  size_t   err_len;
} run_t;

static int
fail(run_t *r, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static int
fail(run_t *r, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(r->err, r->err_len, fmt, ap);
  va_end(ap);
  return -1;
}

static int
stopped(const run_t *r) {
  return r->h && r->h->stop && r->h->stop(r->h->ctx);
}

static void
phase(const run_t *r, const char *text, uint64_t total) {
  if(r->h && r->h->phase) r->h->phase(r->h->ctx, text, total);
}

static void
work(const run_t *r, uint64_t n) {
  if(r->h && r->h->work) r->h->work(r->h->ctx, n);
}

static void
current(const run_t *r, const char *rel) {
  if(r->h && r->h->current) r->h->current(r->h->ctx, rel);
}

/* ------------------------------------------------------------------ files and folders */

static int
write_all(int fd, const void *buf, size_t len) {
  const uint8_t *p = buf;
  while(len) {
    ssize_t w = write(fd, p, len);
    if(w < 0 && errno == EINTR) continue;
    if(w <= 0) { if(w == 0) errno = EIO; return -1; }
    p += w;
    len -= (size_t)w;
  }
  return 0;
}

/* A folder this engine made, below a path it made: depth first, a link is unlinked and never entered. */
static void
rm_tree(char *path, size_t len, int depth) {
  if(depth > SX_DEPTH + 4) return;
  for(int pass = 0; pass < 3; pass++) {
    DIR *d = opendir(path);
    if(!d) break;
    struct dirent *e;
    while((e = readdir(d)) != NULL) {
      if(!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
      const size_t n = strlen(e->d_name);
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
remove_tree(const char *dir) {
  char p[PATH_MAX];
  if(snprintf(p, sizeof(p), "%s", dir) >= (int)sizeof(p)) return;
  rm_tree(p, strlen(p), 0);
}

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

/* Copies src to a NEW file dst (never overwrites). 0, or errno. The progress counts the bytes. */
static int
copy_file(run_t *r, const char *src, const char *dst, uint64_t expect, int count_work, uint64_t *copied) {
  int in = open(src, O_RDONLY);
  if(in < 0) return errno ? errno : EIO;
  int out = open(dst, O_WRONLY | O_CREAT | O_EXCL, 0600);
  if(out < 0) { int e = errno ? errno : EIO; close(in); return e; }
  uint8_t *buf = malloc(SX_BUF);
  if(!buf) { close(in); close(out); unlink(dst); return ENOMEM; }
  uint64_t done = 0;
  int rc = 0;
  for(;;) {
    ssize_t n = read(in, buf, SX_BUF);
    if(n < 0 && errno == EINTR) continue;
    if(n < 0) { rc = errno ? errno : EIO; break; }
    if(n == 0) break;
    if(write_all(out, buf, (size_t)n) != 0) { rc = errno ? errno : EIO; break; }
    done += (uint64_t)n;
    if(count_work) work(r, (uint64_t)n);
    if(stopped(r)) { rc = ECANCELED; break; }
  }
  free(buf);
  close(in);
  if(rc == 0 && fsync(out) != 0) rc = errno ? errno : EIO;
  if(close(out) != 0 && rc == 0) rc = errno ? errno : EIO;
  if(rc == 0 && expect != (uint64_t)-1 && done != expect) rc = EAGAIN;                  /* the size changed under the copy */
  if(rc != 0) unlink(dst);
  if(copied) *copied = done;
  return rc;
}

/* ------------------------------------------------------------------ the tree of the mount */

static int
list_add(list_t *l, const char *rel, const struct stat *st, int dir) {
  if(l->n >= SX_MAX_ENT) return E2BIG;
  if(l->n == l->cap) {
    const unsigned cap = l->cap ? l->cap * 2 : 128;
    ent_t *nv = realloc(l->v, (size_t)cap * sizeof(*nv));
    if(!nv) return ENOMEM;
    l->v = nv;
    l->cap = cap;
  }
  ent_t *e = &l->v[l->n++];
  memset(e, 0, sizeof(*e));
  snprintf(e->rel, sizeof(e->rel), "%s", rel);
  e->size = dir ? 0 : (uint64_t)st->st_size;
  e->uid = (uint32_t)st->st_uid;
  e->gid = (uint32_t)st->st_gid;
  e->mode = (uint16_t)(st->st_mode & 07777);
  e->mtime = st->st_mtime;
  e->dir = dir;
  if(!dir) l->bytes += e->size;
  return 0;
}

/* Walks root/rel. Returns 0, or errno; bad is set to the name that is not carried. */
static int
walk(const char *root, const char *rel, int depth, list_t *l, char *bad, size_t bad_len) {
  if(depth > SX_DEPTH) { snprintf(bad, bad_len, "%s", rel); return ENAMETOOLONG; }
  char dir[PATH_MAX];
  if(snprintf(dir, sizeof(dir), rel[0] ? "%s/%s" : "%s", root, rel) >= (int)sizeof(dir)) return ENAMETOOLONG;
  DIR *d = opendir(dir);
  if(!d) { snprintf(bad, bad_len, "%s", rel[0] ? rel : "/"); return errno ? errno : EIO; }
  int rc = 0;
  struct dirent *e;
  while(rc == 0 && (e = readdir(d)) != NULL) {
    if(!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
    char child[SX_REL_MAX + 8];
    if(snprintf(child, sizeof(child), rel[0] ? "%s/%s" : "%s%s", rel, e->d_name) >= (int)SX_REL_MAX) {
      snprintf(bad, bad_len, "%.180s...", child);
      rc = ENAMETOOLONG;
      break;
    }
    char full[PATH_MAX];
    if(snprintf(full, sizeof(full), "%s/%s", root, child) >= (int)sizeof(full)) { rc = ENAMETOOLONG; break; }
    struct stat st;
    if(lstat(full, &st) != 0) { snprintf(bad, bad_len, "%s", child); rc = errno ? errno : EIO; break; }
    if(!S_ISREG(st.st_mode) && !S_ISDIR(st.st_mode)) { l->skipped++; continue; }
    if(!chives_path_safe(child) || !savezip_name_ok(child)) { snprintf(bad, bad_len, "%s", child); rc = EINVAL; break; }
    if(S_ISREG(st.st_mode) && (uint64_t)st.st_size > SX_ZIP_MAX) { snprintf(bad, bad_len, "%s", child); rc = EFBIG; break; }
    rc = list_add(l, child, &st, S_ISDIR(st.st_mode));
    if(rc != 0) { snprintf(bad, bad_len, "%s", child); break; }
    if(S_ISDIR(st.st_mode)) rc = walk(root, child, depth + 1, l, bad, bad_len);
  }
  closedir(d);
  return rc;
}

static int
cmp_ent(const void *a, const void *b) {
  return strcmp(((const ent_t *)a)->rel, ((const ent_t *)b)->rel);
}

/* ------------------------------------------------------------------ the run */

static int
free_space(const char *dir, uint64_t *avail) {
  struct statvfs sv;
  if(statvfs(dir, &sv) != 0) return -1;
  *avail = (uint64_t)sv.f_bavail * (uint64_t)sv.f_frsize;
  return 0;
}

static int
progress_tick(void *ctx, uint64_t bytes) {
  run_t *r = ctx;
  work(r, bytes);
  return stopped(r);
}

static const char *
base_name(const char *p) {
  const char *s = strrchr(p, '/');
  return s ? s + 1 : p;
}

int
savexport_run(const savexport_args_t *a, const savexport_hooks_t *h, savexport_result_t *res, char *err, size_t err_len) {
  run_t R = { h, err, err_len };
  run_t *r = &R;
  memset(res, 0, sizeof(*res));
  if(err_len) err[0] = 0;

  /* The copy keeps the name the console gave the image (sdimg_<slot>), in a folder everyone may enter and with mode 0755: the way the console's own
     tools hand an image to the mount. Whether the mount cares is not known; nothing is gained by being different. */
  const char *imgname = base_name(a->image);
  if(!imgname[0] || strlen(imgname) > 100 || !chives_path_safe(imgname)) return fail(r, "Die Abbild-Datei des Spielstands gibt es nicht (mehr).");
  char img[PATH_MAX], mnt[PATH_MAX], stage[PATH_MAX], zip_tmp[PATH_MAX], zip_fin[PATH_MAX];
  if(snprintf(img, sizeof(img), "%s/%s", a->work_dir, imgname) >= (int)sizeof(img) ||
     snprintf(mnt, sizeof(mnt), "%s/mnt", a->work_dir) >= (int)sizeof(mnt) ||
     snprintf(stage, sizeof(stage), "%s/stage", a->work_dir) >= (int)sizeof(stage) ||
     snprintf(zip_fin, sizeof(zip_fin), "%s/%s", a->out_dir, a->zip_name) >= (int)sizeof(zip_fin) ||
     snprintf(zip_tmp, sizeof(zip_tmp), "%s.partial", zip_fin) >= (int)sizeof(zip_tmp))
    return fail(r, "Ein Pfad ist zu lang.");
  if(strchr(a->zip_name, '/') || !a->zip_name[0]) return fail(r, "Der Name der Datei ist ungültig.");
  if(strlen(zip_fin) >= sizeof(res->zip)) return fail(r, "Ein Pfad ist zu lang.");

  list_t list;
  memset(&list, 0, sizeof(list));
  chives_t geo;
  chives_init(&geo);
  uint8_t *sfo = NULL;
  uint32_t sfo_len = 0;
  char pdiag[480];
  pdiag[0] = 0;
  int rc = -1, mounted = 0, stuck = 0, have_img = 0, have_zip = 0, ps4 = 0;
  uint8_t sealed[SAVEMNT_SEALED_BYTES];                          /* PS4: the key from the companion file <slot>.bin */
  memset(sealed, 0, sizeof(sealed));
  szw_t *z = NULL;
  savemnt_t mt;
  memset(&mt, 0, sizeof(mt));

  /* what is to be read: a PS5 image of a sound size */
  struct stat st0;
  if(stat(a->image, &st0) != 0 || !S_ISREG(st0.st_mode)) return fail(r, "Die Abbild-Datei des Spielstands gibt es nicht (mehr).");
  {
    uint8_t head[0x370];
    int fd = open(a->image, O_RDONLY);
    ssize_t n = fd >= 0 ? pread(fd, head, sizeof(head), 0) : -1;
    const int e = errno;
    if(fd >= 0) close(fd);
    if(n < 0x40) return fail(r, "Die Abbild-Datei ließ sich nicht lesen (%s).", ps5tm_io_strerror(n < 0 ? e : EIO));
    if(savemnt_header_kind(head, (size_t)n) == 1) ps4 = 1;                    /* a PS4 save: no geometry of its own to carry, the key is in <slot>.bin */
    else if(chives_from_header(head, (size_t)n, (uint64_t)st0.st_size, &geo) != 0)
      return fail(r, "Das ist kein Spielstand, den sich diese App übertragen lässt (nur PS4- und PS5-Spielstände).");
  }
  if(ps4) {
    /* <dir>/sdimg_<slot> -> <dir>/<slot>.bin, 96 bytes */
    char bin[PATH_MAX];
    if(strncmp(imgname, "sdimg_", 6) || snprintf(bin, sizeof(bin), "%.*s%s.bin", (int)(strlen(a->image) - strlen(imgname)), a->image, imgname + 6) >= (int)sizeof(bin))
      return fail(r, "Der Name der Abbild-Datei ist ungültig.");
    int bfd = open(bin, O_RDONLY);
    ssize_t bn = bfd >= 0 ? read(bfd, sealed, sizeof(sealed)) : -1;
    const int be = errno;
    if(bfd >= 0) close(bfd);
    if(bn != (ssize_t)sizeof(sealed)) return fail(r, "Zum Spielstand gehört die Schlüsseldatei (%s.bin) nicht oder sie ist nicht lesbar (%s).", imgname + 6, ps5tm_io_strerror(bn < 0 ? be : EIO));
  }

  /* room: the copy of the image, then the files out of it, then the ZIP (the copy is gone before the ZIP is written, so the larger of the two counts) */
  {
    int e = mkdir_p(a->work_dir, 0755);
    if(e == 0) e = mkdir_p(a->out_dir, 0755);
    if(e != 0) return fail(r, "Der Arbeitsordner ließ sich nicht anlegen: %s", ps5tm_io_strerror(e));
    uint64_t avail = 0;
    if(free_space(a->work_dir, &avail) != 0) return fail(r, "Der freie Platz ließ sich nicht ermitteln.");
    const uint64_t need = 2 * (uint64_t)st0.st_size + SX_MIN_FREE;
    if(avail < need)
      return fail(r, "Auf der internen Platte ist zu wenig Platz: gebraucht werden etwa %llu MB, frei sind %llu MB.",
                  (unsigned long long)(need >> 20), (unsigned long long)(avail >> 20));
  }

  /* leftovers of a run that was cut off: a mount first (the console keeps it), then the files. Never delete below a mount. */
  {
    /* An empty folder goes with rmdir; one that cannot be removed is still mounted (EBUSY) or holds something: the console is told to let go, once.
       Nothing below it is ever deleted by this program - a mount that is still there would take the save's files with it. */
    if(rmdir(mnt) != 0 && errno != ENOENT) {
      char e2[300];
      if(savemnt_umount_path(mnt, e2, sizeof(e2)) != 0)
        return fail(r, "Von einem früheren Versuch ist noch ein Spielstand eingehängt, und die Konsole lässt ihn nicht los. %s Die Konsole neu starten, dann geht es wieder.", e2);
      if(rmdir(mnt) != 0 && errno != ENOENT)
        return fail(r, "Im Arbeitsordner liegt noch ein Rest eines früheren Versuchs (%s). Die Konsole neu starten, dann geht es wieder.", ps5tm_io_strerror(errno));
    }
    /* image copies of an earlier run (any slot): plain files named sdimg_..., nothing else */
    DIR *wd = opendir(a->work_dir);
    if(wd) {
      struct dirent *we;
      while((we = readdir(wd)) != NULL) {
        char p[PATH_MAX];
        struct stat sw;
        if(strncmp(we->d_name, "sdimg_", 6) || snprintf(p, sizeof(p), "%s/%s", a->work_dir, we->d_name) >= (int)sizeof(p)) continue;
        if(lstat(p, &sw) == 0 && S_ISREG(sw.st_mode)) unlink(p);
      }
      closedir(wd);
    }
    unlink(img);
    remove_tree(stage);
    unlink(zip_tmp);
  }

  /* 1. the copy of the image */
  phase(r, "Abbild kopieren", (uint64_t)st0.st_size);
  current(r, base_name(a->image));
  {
    uint64_t copied = 0;
    int e = copy_file(r, a->image, img, (uint64_t)st0.st_size, 1, &copied);
    if(e == ECANCELED) { rc = ECANCELED; goto done; }
    if(e == EAGAIN) { fail(r, "Der Spielstand hat sich beim Kopieren geändert. Läuft ein Spiel? Es noch einmal versuchen, wenn keines läuft."); goto done; }
    if(e != 0) { fail(r, "Das Abbild ließ sich nicht kopieren: %s", ps5tm_io_strerror(e)); goto done; }
    have_img = 1;
    (void)chmod(img, 0755);
    struct stat st1;
    if(stat(a->image, &st1) != 0 || st1.st_size != st0.st_size || st1.st_mtime != st0.st_mtime) {
      fail(r, "Der Spielstand hat sich beim Kopieren geändert. Läuft ein Spiel? Es noch einmal versuchen, wenn keines läuft.");
      goto done;
    }
  }
  if(stopped(r)) { rc = ECANCELED; goto done; }

  /* the real param.sfo from the image header, read BEFORE the mount (the console's own tools do it in this order: the mount and this read do not get in each other's way).
     Whatever the reason it cannot be had - an image without such a blob, a console that does not answer - the file system's own param.sfo is tried later; the answer of
     the console is kept for the message that says why that did not work either. */
  {
    char e2[480];
    sfo = malloc(SAVEMNT_PARAM_BYTES);
    if(!sfo) { fail(r, "Kein Speicher."); goto done; }
    const int pr = ps4 ? -1 : savemnt_param_read(img, 0, sfo, SAVEMNT_PARAM_BYTES, &sfo_len, e2, sizeof(e2));   /* a PS4 save keeps its param.sfo in the file system */
    if(pr == 0 && savesfo_valid(sfo, sfo_len)) {
      res->sfo_from_header = 1;
    } else {
      sfo_len = 0;
      if(pr == -2) snprintf(pdiag, sizeof(pdiag), "%s", e2);
      else if(pr == 0) snprintf(pdiag, sizeof(pdiag), "Die Metadaten (param.sfo) im Kopf des Abbilds sind nicht lesbar.");
    }
  }
  if(stopped(r)) { rc = ECANCELED; goto done; }

  /* 2. mounted: the files out */
  phase(r, "Spielstand einhängen", 0);
  current(r, "");
  {
    char e2[400];
    if(mkdir(mnt, 0755) != 0 && errno != EEXIST) { fail(r, "Der Einhängeordner ließ sich nicht anlegen: %s", ps5tm_io_strerror(errno)); goto done; }
    if((ps4 ? savemnt_open_sealed(img, sealed, mnt, &mt, e2, sizeof(e2)) : savemnt_open(img, mnt, &mt, e2, sizeof(e2))) != 0) { fail(r, "%s", e2); goto done; }
    mounted = 1;
  }
  {
    char bad[SX_REL_MAX + 8] = "";
    int e = walk(mnt, "", 0, &list, bad, sizeof(bad));
    if(e == E2BIG) fail(r, "Der Spielstand hat mehr als %u Dateien und Ordner: so viele überträgt diese App nicht.", SX_MAX_ENT);
    else if(e == EINVAL) fail(r, "Ein Name im Spielstand lässt sich nicht in eine ZIP-Datei übernehmen: %s", bad);
    else if(e == EFBIG) fail(r, "Eine Datei im Spielstand ist zu groß für eine ZIP-Datei (%s).", bad);
    else if(e != 0) fail(r, "Der Spielstand ließ sich nicht durchsehen (%s): %s", bad, ps5tm_io_strerror(e));
    if(e != 0) goto done;
    if(list.bytes > SX_ZIP_MAX) { fail(r, "Der Spielstand ist mit %llu MB zu groß für eine ZIP-Datei.", (unsigned long long)(list.bytes >> 20)); goto done; }
    qsort(list.v, list.n, sizeof(*list.v), cmp_ent);
  }
  {
    uint64_t avail = 0;
    if(free_space(a->work_dir, &avail) == 0 && avail < 2 * list.bytes + SX_MIN_FREE) {
      fail(r, "Auf der internen Platte ist zu wenig Platz für die Dateien des Spielstands (gebraucht werden etwa %llu MB, frei sind %llu MB).",
           (unsigned long long)((2 * list.bytes + SX_MIN_FREE) >> 20), (unsigned long long)(avail >> 20));
      goto done;
    }
  }
  if(mkdir_p(stage, 0700) != 0) { fail(r, "Der Zwischenordner ließ sich nicht anlegen."); goto done; }
  phase(r, "Spielstand lesen", list.bytes);
  for(unsigned i = 0; i < list.n; i++) {
    const ent_t *e = &list.v[i];
    char src[PATH_MAX], dst[PATH_MAX];
    if(snprintf(src, sizeof(src), "%s/%s", mnt, e->rel) >= (int)sizeof(src) || snprintf(dst, sizeof(dst), "%s/%s", stage, e->rel) >= (int)sizeof(dst)) {
      fail(r, "Ein Pfad ist zu lang."); goto done;
    }
    if(e->dir) {
      if(mkdir(dst, 0700) != 0 && errno != EEXIST) { fail(r, "Ein Ordner des Spielstands ließ sich nicht anlegen (%s): %s", e->rel, ps5tm_io_strerror(errno)); goto done; }
      continue;
    }
    current(r, e->rel);
    uint64_t copied = 0;
    int ce = copy_file(r, src, dst, e->size, 1, &copied);
    if(ce == ECANCELED) { rc = ECANCELED; goto done; }
    if(ce == EAGAIN) { fail(r, "Eine Datei des Spielstands hat sich beim Lesen geändert (%s).", e->rel); goto done; }
    if(ce != 0) { fail(r, "Eine Datei des Spielstands ließ sich nicht lesen (%s): %s", e->rel, ps5tm_io_strerror(ce)); goto done; }
  }

  /* 3. let go */
  phase(r, "Spielstand aushängen", 0);
  current(r, "");
  {
    char e2[400];
    const int ce = savemnt_close(&mt, e2, sizeof(e2));
    mounted = 0;
    if(ce != 0) {
      stuck = 1;                                                 /* the image stays where it is, mounted: deleting it now would pull it out from under the console */
      fail(r, "%s Der Spielstand wurde nicht verändert; bitte die Konsole neu starten, bevor es noch einmal versucht wird.", e2);
      goto done;
    }
  }
  rmdir(mnt);
  unlink(img);
  have_img = 0;
  if(stopped(r)) { rc = ECANCELED; goto done; }

  /* the real param.sfo goes in the place of the block of zeros that the file system holds */
  if(sfo_len) {
    char dir[PATH_MAX], path[PATH_MAX];
    if(snprintf(dir, sizeof(dir), "%s/sce_sys", stage) >= (int)sizeof(dir) || snprintf(path, sizeof(path), "%s/sce_sys/param.sfo", stage) >= (int)sizeof(path)) {
      fail(r, "Ein Pfad ist zu lang.");
      goto done;
    }
    if(mkdir(dir, 0700) != 0 && errno != EEXIST) { fail(r, "Der Ordner sce_sys ließ sich nicht anlegen."); goto done; }
    unlink(path);
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if(fd < 0 || write_all(fd, sfo, sfo_len) != 0) {
      const int e = errno ? errno : EIO;
      if(fd >= 0) close(fd);
      fail(r, "Die Metadaten ließen sich nicht schreiben: %s", ps5tm_io_strerror(e));
      goto done;
    }
    close(fd);
    int found = -1;
    for(unsigned i = 0; i < list.n; i++) if(!strcmp(list.v[i].rel, "sce_sys/param.sfo")) found = (int)i;
    if(found >= 0) {
      list.bytes = list.bytes - list.v[found].size + sfo_len;
      list.v[found].size = sfo_len;
    } else {
      struct stat fake;
      memset(&fake, 0, sizeof(fake));
      fake.st_size = sfo_len;
      fake.st_mode = S_IFREG | 0644;
      fake.st_mtime = time(NULL);
      int has_dir = 0;
      for(unsigned i = 0; i < list.n; i++) if(!strcmp(list.v[i].rel, "sce_sys")) has_dir = 1;
      if(!has_dir) {
        struct stat fd2 = fake;
        fd2.st_mode = S_IFDIR | 0755;
        if(list_add(&list, "sce_sys", &fd2, 1) != 0) { fail(r, "Zu viele Dateien."); goto done; }
      }
      if(list_add(&list, "sce_sys/param.sfo", &fake, 0) != 0) { fail(r, "Zu viele Dateien."); goto done; }
      qsort(list.v, list.n, sizeof(*list.v), cmp_ent);
    }
  } else {
    /* no blob in the header: the file system's own param.sfo is the real one - it must be there and sound */
    char path[PATH_MAX];
    if(snprintf(path, sizeof(path), "%s/sce_sys/param.sfo", stage) >= (int)sizeof(path)) { fail(r, "Ein Pfad ist zu lang."); goto done; }
    uint8_t buf[0x8000];
    int fd = open(path, O_RDONLY);
    ssize_t n = fd >= 0 ? read(fd, buf, sizeof(buf)) : -1;
    if(fd >= 0) close(fd);
    if(n <= 0 || !savesfo_valid(buf, (size_t)n)) {
      if(pdiag[0]) fail(r, "Dem Spielstand fehlen die Metadaten (param.sfo): ohne sie lässt er sich später nicht einspielen. Antwort der Konsole: %s", pdiag);
      else fail(r, "Dem Spielstand fehlen die Metadaten (param.sfo): ohne sie lässt er sich später nicht einspielen.");
      goto done;
    }
    sfo_len = (uint32_t)n;
    memcpy(sfo, buf, sfo_len);
    if(pdiag[0]) snprintf(res->note, sizeof(res->note), "Die Metadaten stammen aus dem Spielstand selbst: Die Konsole gab die aus dem Kopf des Abbilds nicht heraus (%.300s).", pdiag);
  }
  {
    /* The ZIP travels (to another console, to a friend): the account of the user is not given away in it. ACCOUNT_ID goes in as zero; the putting-in writes the account of the
       user it is for in any case. The rest of the owner details (PARAMS: the user id and the value all saves of a user share) stays, because the putting-in needs it when the
       other console has no save of that user to take it from. */
    char path[PATH_MAX];
    uint8_t *pb = malloc(0x8000);
    if(pb && snprintf(path, sizeof(path), "%s/sce_sys/param.sfo", stage) < (int)sizeof(path)) {
      const int fd = open(path, O_RDWR);
      const ssize_t n = fd >= 0 ? read(fd, pb, 0x8000) : -1;
      if(n > 0 && savesfo_valid(pb, (size_t)n) && savesfo_patch_owner(pb, (size_t)n, 0, 0, 0) == 0 && lseek(fd, 0, SEEK_SET) == 0 && write_all(fd, pb, (size_t)n) == 0 && fsync(fd) == 0) {
        if(savesfo_valid(sfo, sfo_len)) (void)savesfo_patch_owner(sfo, sfo_len, 0, 0, 0);
      }
      if(fd >= 0) close(fd);
    }
    free(pb);
  }
  if(savesfo_valid(sfo, sfo_len)) {
    savesfo_get_str(sfo, sfo_len, "TITLE_ID", res->title_id, sizeof(res->title_id));
    savesfo_get_str(sfo, sfo_len, "SAVEDATA_DIRECTORY", res->save_dir, sizeof(res->save_dir));
  }
  if(!res->title_id[0]) snprintf(res->title_id, sizeof(res->title_id), "%s", a->title_id);
  if(!res->save_dir[0]) snprintf(res->save_dir, sizeof(res->save_dir), "%s", a->slot);

  /* the sidecar */
  snprintf(geo.title_id, sizeof(geo.title_id), "%s", res->title_id);
  snprintf(geo.save_dir, sizeof(geo.save_dir), "%s", res->save_dir);
  snprintf(geo.image_name, sizeof(geo.image_name), "%s", base_name(a->image));
  for(unsigned i = 0; i < list.n; i++)
    if(chives_own_add(&geo, list.v[i].rel, list.v[i].uid, list.v[i].gid, list.v[i].mode) != 0) break;          /* more than are kept: the rest is left out */
  /* (a PS4 save travels as its files alone: the other tool writes no sidecar for it, and one that says PS4 would make it refuse the ZIP) */
  char *json = ps4 ? NULL : malloc(CHIVES_MAX_JSON);
  if(!ps4 && !json) { fail(r, "Kein Speicher."); goto done; }
  const int jn = ps4 ? 0 : chives_write(&geo, json, CHIVES_MAX_JSON);
  if(jn < 0) { free(json); fail(r, "Die Beschreibung des Abbilds (chives.json) ließ sich nicht erzeugen."); goto done; }

  /* 4. the ZIP */
  phase(r, "ZIP-Datei schreiben", list.bytes);
  z = szw_create(zip_tmp);
  if(!z) { const int e = errno ? errno : EIO; free(json); fail(r, "Die ZIP-Datei ließ sich nicht anlegen: %s", ps5tm_io_strerror(e)); goto done; }
  have_zip = 1;
  int zrc = SZ_OK;
  for(unsigned i = 0; i < list.n && zrc == SZ_OK; i++) {
    const ent_t *e = &list.v[i];
    if(e->dir) continue;                                         /* folders are made again from the paths of the files (and the ownership list) */
    char src[PATH_MAX];
    if(snprintf(src, sizeof(src), "%s/%s", stage, e->rel) >= (int)sizeof(src)) { zrc = SZ_E_IO; errno = ENAMETOOLONG; break; }
    current(r, e->rel);
    zrc = szw_add_file(z, e->rel, src, e->mtime, e->mode ? e->mode : 0644, progress_tick, r);
    if(zrc == SZ_OK) { res->files++; res->bytes += e->size; }
  }
  if(zrc == SZ_OK && !ps4) zrc = szw_add_mem(z, "chives.json", json, (size_t)jn, time(NULL), 0644);
  free(json);
  if(zrc != SZ_OK) {
    szw_abort(z);
    z = NULL;
    have_zip = 0;
    if(zrc == SZ_E_STOP) { rc = ECANCELED; goto done; }
    if(zrc == SZ_E_TOO_BIG) fail(r, "Der Spielstand ist zu groß für eine ZIP-Datei.");
    else if(zrc == SZ_E_CHANGED) fail(r, "Eine Datei hat sich beim Verpacken geändert.");
    else fail(r, "Die ZIP-Datei ließ sich nicht schreiben: %s", ps5tm_io_strerror(errno ? errno : EIO));
    goto done;
  }
  zrc = szw_finish(z);
  z = NULL;
  if(zrc != SZ_OK) { have_zip = 0; fail(r, "Die ZIP-Datei ließ sich nicht abschließen: %s", ps5tm_io_strerror(errno ? errno : EIO)); goto done; }
  if(rename(zip_tmp, zip_fin) != 0) {
    const int e = errno ? errno : EIO;
    unlink(zip_tmp);
    have_zip = 0;
    fail(r, "Die ZIP-Datei ließ sich nicht an ihren Platz legen: %s", ps5tm_io_strerror(e));
    goto done;
  }
  have_zip = 0;                                                  /* it is whole now: it stays */
  {
    /* the name in the folder is made lasting too: a stick pulled out right after "done" must have the file under its name, not as ".partial" or not at all */
    char dd[PATH_MAX];
    snprintf(dd, sizeof(dd), "%s", zip_fin);
    char *sl = strrchr(dd, '/');
    if(sl && sl != dd) {
      *sl = 0;
      const int dfd = open(dd, O_RDONLY);
      if(dfd >= 0) { (void)fsync(dfd); close(dfd); }
    }
  }
  snprintf(res->zip, sizeof(res->zip), "%.*s", (int)sizeof(res->zip) - 1, zip_fin);
  res->skipped = list.skipped;
  rc = 0;

done:
  if(z) szw_abort(z);
  if(mounted) {
    char e2[300];
    if(savemnt_close(&mt, e2, sizeof(e2)) != 0) {
      stuck = 1;
      if(rc == ECANCELED) rc = -1;                               /* a mount that will not let go is worth more than the stop */
      if(!err[0]) snprintf(err, err_len, "%s Bitte die Konsole neu starten, bevor es noch einmal versucht wird.", e2);
    }
    mounted = 0;
  }
  if(have_zip) unlink(zip_tmp);
  if(!stuck) {
    if(have_img) unlink(img);
    rmdir(mnt);
  }
  remove_tree(stage);
  free(list.v);
  free(sfo);
  chives_free(&geo);
  if(rc != 0 && rc != ECANCELED && err_len && !err[0]) snprintf(err, err_len, "Der Spielstand ließ sich nicht auslesen.");
  return rc;
}
