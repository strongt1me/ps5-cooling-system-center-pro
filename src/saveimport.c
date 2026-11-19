/* PS5 Cooling & System Center - Pro: a save from a ZIP of plain files put in on this console (saveimport.h). */

#include <ctype.h>
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
#include "saveimport.h"
#include "savedb.h"
#include "savemnt.h"
#include "savesfo.h"
#include "savezip.h"
#include "third_party/libdeflate/libdeflate.h"

#define SI_MAX_FILES   5000u
#define SI_MIN_FREE    (64ull << 20)
#define SI_MAX_SFO     0x8000u
#define SI_MEM_MAX     (64u << 20)                /* a deflated file is inflated through memory up to this size */
#define SI_BUF         (1u << 20)

typedef struct {
  const savexport_hooks_t *h;
  char *err;
  size_t err_len;
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

static int
tick(void *ctx, uint64_t bytes) {
  run_t *r = ctx;
  work(r, bytes);
  return stopped(r);
}

/* A slot is the name after "sdimg_": letters, digits, "_", "-", "."; never starting with a dot, and never one of the console's own copies. */
static int
slot_ok(const char *s) {
  const size_t n = s ? strlen(s) : 0;
  if(n < 1 || n > 64 || s[0] == '.' || !strncmp(s, "sce_bu_", 7)) return 0;
  for(size_t i = 0; i < n; i++) {
    const char c = s[i];
    if(!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' || c == '-' || c == '.')) return 0;
  }
  return 1;
}

static int
title_ok(const char *s) {
  if(!s || strlen(s) != 9 || strncmp(s, "PPSA", 4)) return 0;
  for(int i = 4; i < 9; i++) if(s[i] < '0' || s[i] > '9') return 0;
  return 1;
}

/* ------------------------------------------------------------------ the ZIP */

/* The last part of an entry name, and the folder part with its slash ("" at the root). */
static const char *
base_of(const char *name, size_t *dir_len) {
  const char *s = strrchr(name, '/');
  *dir_len = s ? (size_t)(s - name) + 1 : 0;
  return s ? s + 1 : name;
}

static int
ieq(const char *a, const char *b) {
  while(*a && *b && tolower((unsigned char)*a) == tolower((unsigned char)*b)) { a++; b++; }
  return !*a && !*b;
}

/* The other tool writes a few entries beside the files of a save of newer firmware (the raw image, its key, a size): they are no files of the save, and the key of an
   image has no business in a ZIP at all - never written into a new image, never counted. */
static int
is_foreign_sidecar(const char *rel) {
  return !strcmp(rel, "PS5IMAGE") || !strcmp(rel, "PS5KEY") || !strcmp(rel, "PS5PARAM_SIZE");
}

static uint64_t
le_num(const uint8_t *v, size_t n) {
  uint64_t x = 0;
  for(size_t i = n > 8 ? 8 : n; i > 0; i--) x = (x << 8) | v[i - 1];
  return x;
}

static void
why_set(saveimport_info_t *info, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void
why_set(saveimport_info_t *info, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(info->why, sizeof(info->why), fmt, ap);
  va_end(ap);
  info->importable = 0;
}

static int
cusa_ok(const char *s) {
  if(!s || strlen(s) != 9 || strncmp(s, "CUSA", 4)) return 0;
  for(int i = 4; i < 9; i++) if(s[i] < '0' || s[i] > '9') return 0;
  return 1;
}

/* A ZIP without a sidecar can still be a PS4 save: its param.sfo says so by the title. 1 when it was taken as one (importable or not: info says), 0 when it is not. */
static int
inspect_ps4(szr_t *z, saveimport_info_t *info, chives_t *geo, uint8_t **sfo_out, size_t *sfo_len_out) {
  const unsigned n = szr_count(z);
  int sfo_i = -1;
  unsigned found = 0;
  for(unsigned i = 0; i < n; i++) {
    const szr_entry_t *e = szr_at(z, i);
    const size_t l = strlen(e->name);
    if(e->is_dir || l < 17 || strcmp(e->name + l - 17, "sce_sys/param.sfo")) continue;
    if(l == 17 || e->name[l - 18] == '/') { sfo_i = (int)i; found++; }
  }
  if(found != 1) return 0;
  uint8_t *sfo = NULL;
  size_t sl = 0;
  if(szr_read(z, (unsigned)sfo_i, &sfo, &sl, SI_MAX_SFO) != SZ_OK || !savesfo_valid(sfo, sl)) { free(sfo); return 0; }
  char tid[24] = "", slot[120] = "";
  savesfo_get_str(sfo, sl, "TITLE_ID", tid, sizeof(tid));
  if(!cusa_ok(tid)) { free(sfo); return 0; }
  info->ps4 = 1;
  const size_t pl = strlen(szr_at(z, (unsigned)sfo_i)->name) - 17;
  if(pl >= sizeof(info->prefix)) { free(sfo); why_set(info, "Der Ordnername in der ZIP-Datei ist zu lang."); return 1; }
  memcpy(info->prefix, szr_at(z, (unsigned)sfo_i)->name, pl);
  info->prefix[pl] = 0;
  for(unsigned i = 0; i < n; i++) {
    const szr_entry_t *e = szr_at(z, i);
    if(strncmp(e->name, info->prefix, pl)) { free(sfo); why_set(info, "Die ZIP-Datei enthält Dateien außerhalb des Spielstand-Ordners (%.100s).", e->name); return 1; }
    if(e->is_dir) { info->dirs++; continue; }
    if(chives_is_sidecar(e->name + pl) || is_foreign_sidecar(e->name + pl)) continue;
    info->files++;
    info->bytes += e->usize;
  }
  if(info->files == 0 || info->files > SI_MAX_FILES) { free(sfo); why_set(info, info->files ? "Die ZIP-Datei enthält mehr als %u Dateien." : "Die ZIP-Datei enthält keine Dateien des Spielstands.", SI_MAX_FILES); return 1; }
  savesfo_get_str(sfo, sl, "SAVEDATA_DIRECTORY", slot, sizeof(slot));
  if(!slot_ok(slot)) { free(sfo); why_set(info, "Der Name des Spielstands im param.sfo (SAVEDATA_DIRECTORY) ist ungültig."); return 1; }
  /* the size the new image needs: what the files take, a quarter more, 4 MB for the file system, at least 32 MB, in steps of 32 KiB */
  uint64_t sz = info->bytes + info->bytes / 4 + (4ull << 20);
  if(sz < (32ull << 20)) sz = 32ull << 20;
  sz = (sz + 32767) / 32768 * 32768;
  info->block_size = 32768;
  info->image_size = sz;
  info->block_count = sz / 32768;
  geo->version = 1;
  geo->block_size = 32768;
  geo->block_count = info->block_count;
  geo->image_size = sz;
  snprintf(info->title_id, sizeof(info->title_id), "%.15s", tid);
  snprintf(info->slot, sizeof(info->slot), "%.99s", slot);
  savesfo_get_str(sfo, sl, "MAINTITLE", info->main_title, sizeof(info->main_title));
  savesfo_get_str(sfo, sl, "SUBTITLE", info->sub_title, sizeof(info->sub_title));
  savesfo_get_str(sfo, sl, "DETAIL", info->detail, sizeof(info->detail));
  uint32_t lp = 0;
  if(savesfo_get_u32(sfo, sl, "SAVEDATA_LIST_PARAM", &lp) == 0) info->user_param = lp;
  uint8_t aid[8];
  if(savesfo_get_raw(sfo, sl, "ACCOUNT_ID", aid, sizeof(aid)) == 8) info->src_account = le_num(aid, 8);
  *sfo_out = sfo;
  *sfo_len_out = sl;
  info->importable = 1;
  return 1;
}

/* The sidecar and the param.sfo of the ZIP, found and read; fills info. szr stays open for the caller. */
static int
inspect_open(const char *zip, szr_t **zp, saveimport_info_t *info, chives_t *geo, uint8_t **sfo_out, size_t *sfo_len_out, char *err, size_t err_len) {
  memset(info, 0, sizeof(*info));
  chives_init(geo);
  *zp = NULL;
  *sfo_out = NULL;
  *sfo_len_out = 0;
  int zerr = 0;
  szr_t *z = szr_open(zip, &zerr);
  if(!z) {
    snprintf(err, err_len, zerr == SZ_E_IO ? "Die ZIP-Datei ließ sich nicht öffnen." : zerr == SZ_E_NAME ? "Die ZIP-Datei enthält einen unsicheren Dateinamen." :
             "Das ist keine lesbare ZIP-Datei (oder eine, die diese App nicht liest: verschlüsselt oder ZIP64).");
    return -1;
  }
  *zp = z;
  const unsigned n = szr_count(z);

  /* the sidecar: exactly one, and its folder is the save's folder */
  int side = -1;
  unsigned sides = 0;
  for(unsigned i = 0; i < n; i++) {
    const szr_entry_t *e = szr_at(z, i);
    size_t dl;
    const char *b = base_of(e->name, &dl);
    if(!e->is_dir && ieq(b, "chives.json")) { side = (int)i; sides++; }
  }
  if(sides == 0 && inspect_ps4(z, info, geo, sfo_out, sfo_len_out)) return 0;
  if(sides == 0) { why_set(info, "In dieser ZIP-Datei fehlt chives.json: Ohne die Beschreibung des Abbilds (Größe, Aufbau, Besitzer) lässt sich der Spielstand nicht einspielen. Die ZIP-Datei muss von dieser App oder von Garlic SaveMgr stammen."); return 0; }
  if(sides > 1) { why_set(info, "Diese ZIP-Datei enthält mehrere Spielstände (mehrere chives.json). Bitte einen einzelnen Spielstand als ZIP-Datei verwenden."); return 0; }
  {
    size_t dl;
    (void)base_of(szr_at(z, (unsigned)side)->name, &dl);
    if(dl >= sizeof(info->prefix)) { why_set(info, "Der Ordnername in der ZIP-Datei ist zu lang."); return 0; }
    memcpy(info->prefix, szr_at(z, (unsigned)side)->name, dl);
    info->prefix[dl] = 0;
  }
  const size_t pl = strlen(info->prefix);

  uint8_t *js = NULL;
  size_t jl = 0;
  if(szr_read(z, (unsigned)side, &js, &jl, CHIVES_MAX_JSON) != SZ_OK) { why_set(info, "Die chives.json der ZIP-Datei ist nicht lesbar."); return 0; }
  const int pr = chives_parse((const char *)js, jl, geo);
  free(js);
  if(pr != 0) { why_set(info, "Die chives.json der ZIP-Datei ist keine gültige Beschreibung eines PS5-Spielstands."); return 0; }

  info->image_size = geo->image_size;
  info->block_count = geo->block_count;
  info->block_size = geo->block_size;
  /* the layout this app can make exactly */
  if(!geo->has_fs_ext || geo->fs_ext != 0 || geo->backup_header_count != 1 || geo->mode_raw != 0x000d || geo->image_size != geo->block_count * (uint64_t)geo->block_size) {
    why_set(info, "Dieser Spielstand hat das Abbildformat einer neueren Firmware (Überschreibbereich fs_ext=%u, mode=%u): Dieses Format kann die App noch nicht anlegen. "
            "Es wurde nichts verändert.", (unsigned)geo->fs_ext, (unsigned)geo->mode_raw);
    return 0;
  }

  /* every entry lies in the save's folder; count what is there */
  for(unsigned i = 0; i < n; i++) {
    const szr_entry_t *e = szr_at(z, i);
    if(strncmp(e->name, info->prefix, pl)) { why_set(info, "Die ZIP-Datei enthält Dateien außerhalb des Spielstand-Ordners (%.100s).", e->name); return 0; }
    if(e->is_dir) { info->dirs++; continue; }
    if((int)i == side || is_foreign_sidecar(e->name + pl)) continue;
    info->files++;
    info->bytes += e->usize;
  }
  if(info->files == 0) { why_set(info, "Die ZIP-Datei enthält keine Dateien des Spielstands."); return 0; }
  if(info->files > SI_MAX_FILES) { why_set(info, "Die ZIP-Datei enthält mehr als %u Dateien.", SI_MAX_FILES); return 0; }
  if(info->bytes > geo->image_size) { why_set(info, "Die Dateien sind größer als das Abbild, das die chives.json beschreibt."); return 0; }

  /* the real param.sfo */
  int sfo_i = -1;
  for(unsigned i = 0; i < n; i++) {
    const szr_entry_t *e = szr_at(z, i);
    if(!e->is_dir && strlen(e->name) == pl + 17 && !strcmp(e->name + pl, "sce_sys/param.sfo")) sfo_i = (int)i;
  }
  if(sfo_i < 0) { why_set(info, "In der ZIP-Datei fehlt sce_sys/param.sfo."); return 0; }
  uint8_t *sfo = NULL;
  size_t sl = 0;
  if(szr_read(z, (unsigned)sfo_i, &sfo, &sl, SI_MAX_SFO) != SZ_OK || !savesfo_valid(sfo, sl)) {
    free(sfo);
    why_set(info, "Das param.sfo der ZIP-Datei ist leer oder nicht lesbar. Bei Spielständen neuerer Firmware liegt es nur im Abbildkopf, den die App nicht erzeugen kann.");
    return 0;
  }
  char fmt[16] = "", tid[24] = "", slot[120] = "";
  savesfo_get_str(sfo, sl, "FORMAT", fmt, sizeof(fmt));
  savesfo_get_str(sfo, sl, "TITLE_ID", tid, sizeof(tid));
  savesfo_get_str(sfo, sl, "SAVEDATA_DIRECTORY", slot, sizeof(slot));
  uint8_t b8[8];
  const int nb = savesfo_get_raw(sfo, sl, "SAVEDATA_BLOCKS", b8, sizeof(b8));
  uint8_t s8[8];
  const int ns = savesfo_get_raw(sfo, sl, "SYSTEM_BLOCKS", s8, sizeof(s8));
  if(strcmp(fmt, "ppr")) { free(sfo); why_set(info, "Das ist kein PS5-Spielstand im Format ppr (FORMAT=%.12s).", fmt); return 0; }
  if(!title_ok(tid)) { free(sfo); why_set(info, "Die Titel-Kennung im param.sfo (%.20s) ist keine PS5-Kennung.", tid); return 0; }
  if(!slot_ok(slot)) { free(sfo); why_set(info, "Der Name des Spielstands im param.sfo (SAVEDATA_DIRECTORY) ist ungültig."); return 0; }
  if(nb < 4 || le_num(b8, (size_t)nb) != geo->block_count || ns < 4 || le_num(s8, (size_t)ns) != 0) {
    free(sfo);
    why_set(info, "Die Blockzahlen im param.sfo passen nicht zur chives.json: Dieser Spielstand hat ein Abbildformat, das die App nicht anlegen kann.");
    return 0;
  }
  snprintf(info->title_id, sizeof(info->title_id), "%.15s", tid);
  snprintf(info->slot, sizeof(info->slot), "%.99s", slot);
  savesfo_get_str(sfo, sl, "MAINTITLE", info->main_title, sizeof(info->main_title));
  savesfo_get_str(sfo, sl, "SUBTITLE", info->sub_title, sizeof(info->sub_title));
  savesfo_get_str(sfo, sl, "DETAIL", info->detail, sizeof(info->detail));
  uint32_t lp = 0;
  if(savesfo_get_u32(sfo, sl, "SAVEDATA_LIST_PARAM", &lp) == 0) info->user_param = lp;
  uint8_t aid[8];
  if(savesfo_get_raw(sfo, sl, "ACCOUNT_ID", aid, sizeof(aid)) == 8) info->src_account = le_num(aid, 8);
  {
    uint8_t params[1024];
    uint32_t u = 0;
    if(savesfo_get_u32(sfo, sl, "USER_ID", &u) == 0) info->src_user = u;
    else if(savesfo_get_raw(sfo, sl, "PARAMS", params, sizeof(params)) >= 8) info->src_user = (uint32_t)le_num(params + 4, 4);
  }
  if(strcmp(geo->title_id, "") && strcmp(geo->title_id, tid)) { free(sfo); why_set(info, "Die Titel-Kennung der chives.json (%.12s) weicht vom param.sfo ab (%.12s).", geo->title_id, tid); return 0; }
  *sfo_out = sfo;
  *sfo_len_out = sl;
  info->importable = 1;
  return 0;
}

int
saveimport_inspect(const char *zip, saveimport_info_t *info, char *err, size_t err_len) {
  if(err_len) err[0] = 0;
  szr_t *z = NULL;
  chives_t geo;
  uint8_t *sfo = NULL;
  size_t sl = 0;
  const int rc = inspect_open(zip, &z, info, &geo, &sfo, &sl, err, err_len);
  free(sfo);
  chives_free(&geo);
  if(z) szr_close(z);
  return rc;
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

/* mkdir -p; a folder that is made gets the mode of the folder it is made in (the console's own convention), the ones that exist stay. */
static int
mkdir_like_parent(const char *path) {
  char tmp[PATH_MAX];
  if(snprintf(tmp, sizeof(tmp), "%s", path) >= (int)sizeof(tmp)) return ENAMETOOLONG;
  for(char *p = tmp + 1;; p++) {
    if(*p != '/' && *p != 0) continue;
    const char save = *p;
    *p = 0;
    struct stat st;
    if(stat(tmp, &st) != 0) {
      char parent[PATH_MAX];
      snprintf(parent, sizeof(parent), "%s", tmp);
      char *sl = strrchr(parent, '/');
      mode_t mode = 0755;
      struct stat ps;
      if(sl && sl != parent) { *sl = 0; if(stat(parent, &ps) == 0) mode = ps.st_mode & 0777; }
      if(mkdir(tmp, mode) != 0 && errno != EEXIST) return errno ? errno : EIO;
      (void)chmod(tmp, mode);
    }
    *p = save;
    if(!save) break;
  }
  return 0;
}

/* Copies src to dst + suffix (never over an existing file of that name), flushed. 0 or errno. */
static int
copy_new(const char *src, const char *dst, mode_t mode) {
  int in = open(src, O_RDONLY);
  if(in < 0) return errno ? errno : EIO;
  unlink(dst);
  int out = open(dst, O_WRONLY | O_CREAT | O_EXCL, 0600);
  if(out < 0) { int e = errno ? errno : EIO; close(in); return e; }
  uint8_t *buf = malloc(SI_BUF);
  if(!buf) { close(in); close(out); unlink(dst); return ENOMEM; }
  int rc = 0;
  for(;;) {
    ssize_t n = read(in, buf, SI_BUF);
    if(n < 0 && errno == EINTR) continue;
    if(n < 0) { rc = errno ? errno : EIO; break; }
    if(n == 0) break;
    if(write_all(out, buf, (size_t)n) != 0) { rc = errno ? errno : EIO; break; }
  }
  free(buf);
  close(in);
  if(rc == 0 && fsync(out) != 0) rc = errno ? errno : EIO;
  if(close(out) != 0 && rc == 0) rc = errno ? errno : EIO;
  if(rc == 0) (void)chmod(dst, mode);
  if(rc != 0) unlink(dst);
  return rc;
}

static int
free_space(const char *dir, uint64_t *avail) {
  struct statvfs sv;
  if(statvfs(dir, &sv) != 0) return -1;
  *avail = (uint64_t)sv.f_bavail * (uint64_t)sv.f_frsize;
  return 0;
}

/* ------------------------------------------------------------------ the run */

typedef struct {
  const chives_t *geo;
  szr_t *z;
  const saveimport_info_t *info;
  const uint8_t *sfo;                                     /* the patched param.sfo */
  size_t sfo_len;
  char mnt[PATH_MAX];
  size_t pl;
  run_t *r;
  int chmod_failed;
  int ps4;                                                /* no sidecar: the modes come from the ZIP entries */
} fill_t;

static void
own_apply(fill_t *f, const char *rel) {
  for(unsigned i = 0; i < f->geo->nown; i++) {
    const chives_own_t *o = &f->geo->own[i];
    if(strcmp(o->path, rel)) continue;
    char p[PATH_MAX];
    if(snprintf(p, sizeof(p), "%s/%s", f->mnt, rel) >= (int)sizeof(p)) return;
    if(chmod(p, o->mode) != 0) f->chmod_failed++;
    if(chown(p, (uid_t)o->uid, (gid_t)o->gid) != 0) { /* the owner of the source is 0:0 as a rule; where the console refuses, the file keeps the one it got */ }
    return;
  }
}

static int
fill(fill_t *f, uint64_t *done_files) {
  run_t *r = f->r;
  const unsigned n = szr_count(f->z);
  for(unsigned i = 0; i < n; i++) {
    const szr_entry_t *e = szr_at(f->z, i);
    if(e->is_dir || stopped(r)) {
      if(stopped(r)) return ECANCELED;
      continue;
    }
    const char *rel = e->name + f->pl;
    if(chives_is_sidecar(rel) || is_foreign_sidecar(rel)) continue;
    char p[PATH_MAX];
    if(snprintf(p, sizeof(p), "%s/%s", f->mnt, rel) >= (int)sizeof(p)) { fail(r, "Ein Pfad ist zu lang."); return -1; }
    {
      char dir[PATH_MAX];
      snprintf(dir, sizeof(dir), "%s", p);
      char *sl = strrchr(dir, '/');
      if(sl) { *sl = 0; if(strlen(dir) > strlen(f->mnt)) { const int me = mkdir_like_parent(dir); if(me) { fail(r, "Ein Ordner des Spielstands ließ sich nicht anlegen (%s): %s", rel, ps5tm_io_strerror(me)); return -1; } } }
    }
    current(r, rel);
    const int fd = open(p, O_WRONLY | O_CREAT | O_EXCL, 0644);
    if(fd < 0) { fail(r, "Eine Datei ließ sich im neuen Abbild nicht anlegen (%s): %s", rel, ps5tm_io_strerror(errno ? errno : EIO)); return -1; }
    int rc;
    if(!strcmp(rel, "sce_sys/param.sfo")) {
      rc = write_all(fd, f->sfo, f->sfo_len) == 0 ? SZ_OK : SZ_E_IO;
      if(rc == SZ_OK) work(r, f->sfo_len);
    } else {
      rc = szr_extract(f->z, i, fd, SI_MEM_MAX, tick, r);
    }
    const int wrote_errno = errno;
    if(rc == SZ_OK && fsync(fd) != 0) rc = SZ_E_IO;
    if(close(fd) != 0 && rc == SZ_OK) rc = SZ_E_IO;
    if(rc == SZ_E_STOP) return ECANCELED;
    if(rc == SZ_E_CRC) { fail(r, "Eine Datei in der ZIP-Datei ist beschädigt (%s): Die Prüfsumme stimmt nicht.", rel); return -1; }
    if(rc == SZ_E_TOO_BIG) { fail(r, "Eine Datei in der ZIP-Datei ist zu groß (%s).", rel); return -1; }
    if(rc != SZ_OK) { fail(r, "Eine Datei ließ sich nicht in das neue Abbild schreiben (%s): %s", rel, ps5tm_io_strerror(wrote_errno ? wrote_errno : EIO)); return -1; }
    if(f->ps4) {
      const unsigned mode = (e->attr >> 16) & 07777;
      if(mode && chmod(p, mode) != 0) f->chmod_failed++;
    }
    (*done_files)++;
  }
  /* folders the sidecar names that no file brought, then modes and owners of everything it lists */
  for(unsigned i = 0; i < f->geo->nown; i++) {
    const chives_own_t *o = &f->geo->own[i];
    char p[PATH_MAX];
    struct stat st;
    if(snprintf(p, sizeof(p), "%s/%s", f->mnt, o->path) >= (int)sizeof(p)) continue;
    if(stat(p, &st) != 0) {
      int in_zip = 0;
      for(unsigned k = 0; k < n && !in_zip; k++)
        if(!szr_at(f->z, k)->is_dir && !strcmp(szr_at(f->z, k)->name + f->pl, o->path)) in_zip = 1;
      if(!in_zip) (void)mkdir_like_parent(p);               /* an empty folder of the source */
    }
    own_apply(f, o->path);
  }
  return 0;
}

/* Reads every file of the ZIP back from the mounted image and compares size and checksum. 0, ECANCELED, or -1 with the reason. */
static int
verify_files(fill_t *f) {
  run_t *r = f->r;
  const unsigned n = szr_count(f->z);
  uint8_t *buf = malloc(SI_BUF);
  if(!buf) return fail(r, "Kein Speicher.");
  int rc = 0;
  for(unsigned i = 0; i < n && rc == 0; i++) {
    const szr_entry_t *e = szr_at(f->z, i);
    if(e->is_dir) continue;
    const char *rel = e->name + f->pl;
    if(chives_is_sidecar(rel) || is_foreign_sidecar(rel)) continue;
    if(stopped(r)) { rc = ECANCELED; break; }
    char p[PATH_MAX];
    if(snprintf(p, sizeof(p), "%s/%s", f->mnt, rel) >= (int)sizeof(p)) { rc = fail(r, "Ein Pfad ist zu lang."); break; }
    current(r, rel);
    const int fd = open(p, O_RDONLY);
    if(fd < 0) { rc = fail(r, "Beim Prüfen fehlt eine Datei im neuen Abbild (%s).", rel); break; }
    uint32_t crc = 0;
    uint64_t total = 0;
    for(;;) {
      ssize_t k = read(fd, buf, SI_BUF);
      if(k < 0 && errno == EINTR) continue;
      if(k <= 0) { if(k < 0) rc = fail(r, "Beim Prüfen ließ sich eine Datei nicht lesen (%s): %s", rel, ps5tm_io_strerror(errno ? errno : EIO)); break; }
      crc = libdeflate_crc32(crc, buf, (size_t)k);
      total += (uint64_t)k;
      work(r, (uint64_t)k);
    }
    close(fd);
    if(rc) break;
    if(!strcmp(rel, "sce_sys/param.sfo")) {
      if(total != f->sfo_len || crc != libdeflate_crc32(0, f->sfo, f->sfo_len)) rc = fail(r, "Beim Prüfen weicht das param.sfo im neuen Abbild vom geschriebenen ab.");
    } else if(total != e->usize || crc != e->crc) {
      rc = fail(r, "Beim Prüfen weicht eine Datei im neuen Abbild von der ZIP-Datei ab (%s).", rel);
    }
  }
  free(buf);
  return rc;
}

static const char *
base_name(const char *p) {
  const char *s = strrchr(p, '/');
  return s ? s + 1 : p;
}

/* The owner of a PS4 save is more than ACCOUNT_ID and the user id: the 1024-byte PARAMS record carries the user's id and a 32-byte value that is the same in all saves of
   that user. It cannot be made up here, so it is taken from a save of the same user that is on this console: the same title and slot first (the whole record), then
   another slot of that title, then any other title (only the user's id and that value, bytes 4 to 40: the rest of the record stays the incoming save's own). Each candidate
   is a COPY of the image, mounted and read and let go again; the first whose param.sfo shows the account and the user id this import is for is the one. Never fails the
   import for want of a donor: the note says how far the owner could be made right. 0, ECANCELED, or -1 (a donor that would not let go: nothing was changed). */
typedef struct { char title[16]; char slot[100]; int rank; uint64_t size; } donor_t;

#define SI_DONOR_MAX (256ull << 20)                       /* a donor is copied: a larger image is not taken (the small ones do as well) */

static int
cmp_donor(const void *x, const void *y) {
  const donor_t *a = x, *b = y;
  if(a->rank != b->rank) return a->rank - b->rank;
  return a->size < b->size ? -1 : a->size > b->size ? 1 : 0;
}

static int
ps4_owner(const saveimport_args_t *a, const saveimport_info_t *info, uint8_t *sfo, size_t sfo_len, run_t *r, char *note, size_t note_len) {
  char root[PATH_MAX];
  note[0] = 0;
  if(snprintf(root, sizeof(root), "%s/%s/savedata", a->home_dir, a->uid) >= (int)sizeof(root)) return 0;
  donor_t cand[48];
  unsigned nc = 0;
  DIR *d = opendir(root);
  while(d) {
    struct dirent *te = readdir(d);
    if(!te) break;
    if(!cusa_ok(te->d_name)) continue;
    char tdir[PATH_MAX];
    if(snprintf(tdir, sizeof(tdir), "%s/%s", root, te->d_name) >= (int)sizeof(tdir)) continue;
    DIR *sd = opendir(tdir);
    while(sd && nc < sizeof(cand) / sizeof(cand[0])) {
      struct dirent *se = readdir(sd);
      if(!se) break;
      if(strncmp(se->d_name, "sdimg_", 6) || !slot_ok(se->d_name + 6)) continue;
      char bin[PATH_MAX], img[PATH_MAX];
      struct stat st, ist;
      if(snprintf(bin, sizeof(bin), "%s/%s.bin", tdir, se->d_name + 6) >= (int)sizeof(bin) || stat(bin, &st) != 0) continue;
      if(snprintf(img, sizeof(img), "%s/%s", tdir, se->d_name) >= (int)sizeof(img) || stat(img, &ist) != 0 || !S_ISREG(ist.st_mode) || (uint64_t)ist.st_size > SI_DONOR_MAX) continue;
      cand[nc].size = (uint64_t)ist.st_size;
      snprintf(cand[nc].title, sizeof(cand[nc].title), "%.15s", te->d_name);
      snprintf(cand[nc].slot, sizeof(cand[nc].slot), "%.99s", se->d_name + 6);
      cand[nc].rank = strcmp(te->d_name, info->title_id) ? 2 : strcmp(se->d_name + 6, info->slot) ? 1 : 0;
      nc++;
    }
    if(sd) closedir(sd);
  }
  if(d) closedir(d);
  qsort(cand, nc, sizeof(cand[0]), cmp_donor);                    /* the best rank first, the smallest image first within it */

  uint8_t params[1024];
  int got = -1;
  unsigned tries = 0;
  char ddir[PATH_MAX], dmnt[PATH_MAX];
  if(snprintf(ddir, sizeof(ddir), "%s/donor", a->work_dir) >= (int)sizeof(ddir) || snprintf(dmnt, sizeof(dmnt), "%s/donor_mnt", a->work_dir) >= (int)sizeof(dmnt)) return 0;
  for(int rank = 0; rank <= 2 && got < 0 && tries < 8; rank++) {
    for(unsigned i = 0; i < nc && got < 0 && tries < 8; i++) {
      if(cand[i].rank != rank) continue;
      if(stopped(r)) return ECANCELED;
      tries++;
      char src[PATH_MAX], bin[PATH_MAX], dimg[PATH_MAX];
      uint8_t sealed[SAVEMNT_SEALED_BYTES];
      if(snprintf(src, sizeof(src), "%s/%s/sdimg_%s", root, cand[i].title, cand[i].slot) >= (int)sizeof(src) ||
         snprintf(bin, sizeof(bin), "%s/%s/%s.bin", root, cand[i].title, cand[i].slot) >= (int)sizeof(bin) ||
         snprintf(dimg, sizeof(dimg), "%s/sdimg_%s", ddir, cand[i].slot) >= (int)sizeof(dimg)) continue;
      const int bfd = open(bin, O_RDONLY);
      const ssize_t bn = bfd >= 0 ? read(bfd, sealed, sizeof(sealed)) : -1;
      if(bfd >= 0) close(bfd);
      if(bn != (ssize_t)sizeof(sealed)) continue;
      mkdir(ddir, 0755);
      if(copy_new(src, dimg, 0755) != 0) { unlink(dimg); continue; }
      mkdir(dmnt, 0755);
      savemnt_t dm;
      char e2[300];
      int valid = 0;
      if(savemnt_open_sealed(dimg, sealed, dmnt, &dm, e2, sizeof(e2)) == 0) {
        char sp[PATH_MAX];
        uint8_t buf[SI_MAX_SFO];
        if(snprintf(sp, sizeof(sp), "%s/sce_sys/param.sfo", dmnt) < (int)sizeof(sp)) {
          const int fd = open(sp, O_RDONLY);
          const ssize_t n = fd >= 0 ? read(fd, buf, sizeof(buf)) : -1;
          if(fd >= 0) close(fd);
          uint8_t aid[8], pr[1024];
          if(n > 0 && savesfo_valid(buf, (size_t)n) && savesfo_get_raw(buf, (size_t)n, "ACCOUNT_ID", aid, 8) == 8 && le_num(aid, 8) == a->account_id &&
             savesfo_get_raw(buf, (size_t)n, "PARAMS", pr, sizeof(pr)) == 1024 && (uint32_t)le_num(pr + 4, 4) == a->user_id) {
            int nz = 0;
            for(int k = 8; k < 40; k++) nz |= pr[k];
            if(nz) { memcpy(params, pr, sizeof(params)); valid = 1; }
          }
        }
        if(savemnt_close(&dm, e2, sizeof(e2)) != 0) { fail(r, "%s Es wurde nichts verändert; bitte die Konsole neu starten, bevor es noch einmal versucht wird.", e2); return -1; }
      }
      rmdir(dmnt);
      unlink(dimg);
      if(valid) got = rank;
    }
  }
  rmdir(ddir);
  if(got == 0) {
    savesfo_set_raw(sfo, sfo_len, "PARAMS", 0, params, 1024);
  } else if(got > 0) {
    savesfo_set_raw(sfo, sfo_len, "PARAMS", 4, params + 4, 36);
    snprintf(note, note_len, "Die Besitzerangaben stammen aus einem anderen Spielstand dieses Benutzers und sind nicht vollständig geprüft: Meldet das Spiel beschädigte Daten, liegt es vermutlich daran.");
  } else {
    snprintf(note, note_len, "Von diesem Benutzer war kein Spielstand zu finden, dessen Besitzerangaben sich übernehmen ließen. Der Spielstand ist eingespielt; das Spiel meldet vielleicht beschädigte Daten.");
  }
  savesfo_patch_owner(sfo, sfo_len, a->account_id, 1, a->user_id);              /* the account and the user id once more, over whatever the record brought */
  return 0;
}

int
saveimport_run(const saveimport_args_t *a, const saveimport_hooks_t *h, saveimport_result_t *res, char *err, size_t err_len) {
  run_t R = { &h->base, err, err_len };
  run_t *r = &R;
  memset(res, 0, sizeof(*res));
  if(err_len) err[0] = 0;

  saveimport_info_t info;
  chives_t geo;
  szr_t *z = NULL;
  uint8_t *sfo = NULL;
  size_t sfo_len = 0;
  int rc = -1, mounted = 0, stuck = 0, have_img = 0, committed = 0, ps4 = 0;
  char img[PATH_MAX] = "", mnt[PATH_MAX] = "", icon_tmp[PATH_MAX] = "", bin_tmp[PATH_MAX] = "";
  uint8_t sealed[SAVEMNT_SEALED_BYTES];
  char owner_note[400];
  memset(sealed, 0, sizeof(sealed));
  owner_note[0] = 0;
  chives_init(&geo);
  memset(&info, 0, sizeof(info));

  phase(r, "ZIP-Datei prüfen", 0);
  if(inspect_open(a->zip, &z, &info, &geo, &sfo, &sfo_len, err, err_len) != 0) goto done;
  if(!info.importable) { fail(r, "%s", info.why); goto done; }
  if(strlen(a->uid) != 8) { fail(r, "Der Benutzer ist ungültig."); goto done; }
  ps4 = info.ps4;

  snprintf(res->title_id, sizeof(res->title_id), "%s", info.title_id);
  snprintf(res->slot, sizeof(res->slot), "%s", info.slot);
  {
    char sdimg[160];
    snprintf(sdimg, sizeof(sdimg), "sdimg_%s", info.slot);
    if(snprintf(img, sizeof(img), "%s/%s", a->work_dir, sdimg) >= (int)sizeof(img) || snprintf(mnt, sizeof(mnt), "%s/mnt", a->work_dir) >= (int)sizeof(mnt) ||
       snprintf(icon_tmp, sizeof(icon_tmp), "%s/icon0.png", a->work_dir) >= (int)sizeof(icon_tmp) ||
       snprintf(bin_tmp, sizeof(bin_tmp), "%s/%s.bin", a->work_dir, info.slot) >= (int)sizeof(bin_tmp)) { fail(r, "Ein Pfad ist zu lang."); goto done; }
  }

  /* the owner in the param.sfo: the account and the user of this console */
  {
    uint8_t *own_sfo = malloc(sfo_len);
    if(!own_sfo) { fail(r, "Kein Speicher."); goto done; }
    memcpy(own_sfo, sfo, sfo_len);
    free(sfo);
    sfo = own_sfo;
    if(savesfo_patch_owner(sfo, sfo_len, a->account_id, 1, a->user_id) != 0) { fail(r, "Im param.sfo lassen sich das Konto und der Benutzer nicht eintragen (ACCOUNT_ID fehlt oder ist zu kurz)."); goto done; }
  }

  /* room, folders, leftovers */
  {
    int e = mkdir_like_parent(a->work_dir);
    if(e) { fail(r, "Der Arbeitsordner ließ sich nicht anlegen: %s", ps5tm_io_strerror(e)); goto done; }
    uint64_t avail = 0;
    if(free_space(a->work_dir, &avail) != 0) { fail(r, "Der freie Platz ließ sich nicht ermitteln."); goto done; }
    const uint64_t need = 2 * info.image_size + SI_MIN_FREE;
    if(avail < need) {
      fail(r, "Auf der internen Platte ist zu wenig Platz: gebraucht werden etwa %llu MB, frei sind %llu MB.", (unsigned long long)(need >> 20), (unsigned long long)(avail >> 20));
      goto done;
    }
    if(rmdir(mnt) != 0 && errno != ENOENT) {
      char e2[300];
      if(savemnt_umount_path(mnt, e2, sizeof(e2)) != 0) { fail(r, "Von einem früheren Versuch ist noch ein Spielstand eingehängt, und die Konsole lässt ihn nicht los. %s Die Konsole neu starten, dann geht es wieder.", e2); goto done; }
      if(rmdir(mnt) != 0 && errno != ENOENT) { fail(r, "Im Arbeitsordner liegt noch ein Rest eines früheren Versuchs (%s). Die Konsole neu starten, dann geht es wieder.", ps5tm_io_strerror(errno)); goto done; }
    }
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
    unlink(icon_tmp);
    unlink(bin_tmp);
    {
      char dm[PATH_MAX], dd[PATH_MAX];                       /* what a cut-off earlier run may have left of a PS4 donor */
      if(snprintf(dm, sizeof(dm), "%s/donor_mnt", a->work_dir) < (int)sizeof(dm) && snprintf(dd, sizeof(dd), "%s/donor", a->work_dir) < (int)sizeof(dd)) {
        if(rmdir(dm) != 0 && errno != ENOENT) {
          char e3[300];
          if(savemnt_umount_path(dm, e3, sizeof(e3)) == 0) rmdir(dm);
        }
        DIR *dq = opendir(dd);
        if(dq) {
          struct dirent *de;
          while((de = readdir(dq)) != NULL) {
            char p[PATH_MAX];
            if(strncmp(de->d_name, "sdimg_", 6) == 0 && snprintf(p, sizeof(p), "%s/%s", dd, de->d_name) < (int)sizeof(p)) unlink(p);
          }
          closedir(dq);
          rmdir(dd);
        }
      }
    }
  }
  if(stopped(r)) { rc = ECANCELED; goto done; }

  /* a PS4 save: the owner in the param.sfo is made right from a save of the same user (copies of images, mounted one by one) */
  if(ps4) {
    phase(r, "Besitzer prüfen", 0);
    current(r, "");
    const int orc = ps4_owner(a, &info, sfo, sfo_len, r, owner_note, sizeof(owner_note));
    if(orc == ECANCELED) { rc = ECANCELED; goto done; }
    if(orc != 0) { stuck = 1; goto done; }
    if(stopped(r)) { rc = ECANCELED; goto done; }
  }

  /* 2. the new image */
  phase(r, "Abbild anlegen", 0);
  current(r, base_name(img));
  {
    char e2[400];
    if((ps4 ? savemnt_create_ps4(img, info.image_size, sealed, e2, sizeof(e2)) : savemnt_create(img, info.image_size, info.block_size, e2, sizeof(e2))) != 0) { fail(r, "%s", e2); goto done; }
    have_img = 1;
    if(ps4) {
      /* the sealed key of the new image goes into <slot>.bin beside it; the check is only that the formatter made a PS4 image of the size asked for */
      uint8_t head4[0x40];
      struct stat st4;
      int fd4 = open(img, O_RDONLY);
      const ssize_t hn4 = fd4 >= 0 ? pread(fd4, head4, sizeof(head4), 0) : -1;
      const int s4 = fd4 >= 0 && fstat(fd4, &st4) == 0;
      if(fd4 >= 0) close(fd4);
      int bfd = open(bin_tmp, O_WRONLY | O_CREAT | O_EXCL, 0644);
      int bok = bfd >= 0 && write_all(bfd, sealed, sizeof(sealed)) == 0 && fsync(bfd) == 0;
      if(bfd >= 0 && close(bfd) != 0) bok = 0;
      if(hn4 < 0x40 || !s4 || savemnt_header_kind(head4, (size_t)hn4) != 1 || (uint64_t)st4.st_size != info.image_size || !bok) {
        fail(r, "Das neue Abbild hat nicht den Aufbau, den die ZIP-Datei beschreibt (die Konsole hat es anders angelegt). Es wurde nichts verändert.");
        goto done;
      }
    } else {
    uint8_t head[0x370];
    int fd = open(img, O_RDONLY);
    ssize_t hn = fd >= 0 ? pread(fd, head, sizeof(head), 0) : -1;
    struct stat st;
    const int ok_stat = fd >= 0 && fstat(fd, &st) == 0;
    if(fd >= 0) close(fd);
    chives_t made;
    if(hn < 0x40 || !ok_stat || chives_from_header(head, (size_t)hn, (uint64_t)st.st_size, &made) != 0 || made.block_size != geo.block_size || made.block_count != geo.block_count ||
       (made.has_fs_ext && made.fs_ext != 0)) {
      fail(r, "Das neue Abbild hat nicht den Aufbau, den die ZIP-Datei beschreibt (die Konsole hat es anders angelegt). Es wurde nichts verändert.");
      goto done;
    }
    }
  }
  if(stopped(r)) { rc = ECANCELED; goto done; }

  /* 3. mounted: the files in */
  savemnt_t mt;
  memset(&mt, 0, sizeof(mt));
  fill_t f;
  memset(&f, 0, sizeof(f));
  f.geo = &geo;
  f.z = z;
  f.info = &info;
  f.sfo = sfo;
  f.sfo_len = sfo_len;
  f.pl = strlen(info.prefix);
  f.r = r;
  f.ps4 = ps4;
  snprintf(f.mnt, sizeof(f.mnt), "%s", mnt);
  phase(r, "Spielstand einhängen", 0);
  current(r, "");
  {
    char e2[400];
    if(mkdir(mnt, 0755) != 0 && errno != EEXIST) { fail(r, "Der Einhängeordner ließ sich nicht anlegen: %s", ps5tm_io_strerror(errno)); goto done; }
    if((ps4 ? savemnt_open_sealed(img, sealed, mnt, &mt, e2, sizeof(e2)) : savemnt_open(img, mnt, &mt, e2, sizeof(e2))) != 0) { fail(r, "%s", e2); goto done; }
    mounted = 1;
  }
  uint64_t free_blocks = 0;
  {
    phase(r, "Dateien schreiben", info.bytes);
    uint64_t files_done = 0;
    const int fr = fill(&f, &files_done);
    if(fr == ECANCELED) { rc = ECANCELED; goto done; }
    if(fr != 0) goto done;
    struct statvfs sv;
    if(statvfs(mnt, &sv) == 0 && sv.f_frsize && sv.f_blocks && (uint64_t)sv.f_blocks * (uint64_t)sv.f_frsize <= info.image_size * 2)
      free_blocks = (uint64_t)sv.f_bfree * (uint64_t)sv.f_frsize / info.block_size;
    else
      free_blocks = info.image_size > info.bytes ? (info.image_size - info.bytes) / info.block_size : 0;
    if(free_blocks > info.block_count) free_blocks = info.block_count;
    sync();
  }
  phase(r, "Spielstand aushängen", 0);
  current(r, "");
  {
    char e2[400];
    const int ce = savemnt_close(&mt, e2, sizeof(e2));
    mounted = 0;
    if(ce != 0) { stuck = 1; fail(r, "%s Auf der Konsole wurde nichts verändert; bitte die Konsole neu starten, bevor es noch einmal versucht wird.", e2); goto done; }
  }
  rmdir(mnt);
  if(stopped(r)) { rc = ECANCELED; goto done; }

  /* 4. mounted a second time: every file read back from the new image and compared with the ZIP */
  phase(r, "Abbild prüfen", info.bytes);
  {
    char e2[400];
    if(mkdir(mnt, 0755) != 0 && errno != EEXIST) { fail(r, "Der Einhängeordner ließ sich nicht anlegen: %s", ps5tm_io_strerror(errno)); goto done; }
    if((ps4 ? savemnt_open_sealed(img, sealed, mnt, &mt, e2, sizeof(e2)) : savemnt_open(img, mnt, &mt, e2, sizeof(e2))) != 0) { fail(r, "Beim Prüfen ließ sich das neue Abbild nicht einhängen. %s", e2); goto done; }
    mounted = 1;
    const int vr = verify_files(&f);
    if(vr == ECANCELED) { rc = ECANCELED; goto done; }
    if(vr != 0) goto done;
    const int ce = savemnt_close(&mt, e2, sizeof(e2));
    mounted = 0;
    if(ce != 0) { stuck = 1; fail(r, "%s Auf der Konsole wurde nichts verändert; bitte die Konsole neu starten, bevor es noch einmal versucht wird.", e2); goto done; }
  }
  rmdir(mnt);

  /* the picture of the save, from the ZIP (its own file: the one in the image is the same) */
  int have_icon = 0;
  {
    char want[320];
    snprintf(want, sizeof(want), "%ssce_sys/icon0.png", info.prefix);
    for(unsigned i = 0; i < szr_count(z); i++) {
      if(szr_at(z, i)->is_dir || strcmp(szr_at(z, i)->name, want)) continue;
      const int fd = open(icon_tmp, O_WRONLY | O_CREAT | O_EXCL, 0644);
      if(fd >= 0) {
        have_icon = szr_extract(z, i, fd, SI_MEM_MAX, NULL, NULL) == SZ_OK && fsync(fd) == 0;
        close(fd);
        if(!have_icon) unlink(icon_tmp);
      }
      break;
    }
  }
  if(stopped(r)) { rc = ECANCELED; goto done; }

  /* the database must be one this app can write before anything of the console is touched: its folder is there (never made here), and a database that is there opens */
  {
    char dbp0[PATH_MAX], dbdir[PATH_MAX], e2[300];
    struct stat st;
    if(snprintf(dbp0, sizeof(dbp0), "%s/%s/%s/db/user/savedata.db", a->sys_dir, ps4 ? "savedata" : "savedata_prospero", a->uid) >= (int)sizeof(dbp0) ||
       snprintf(dbdir, sizeof(dbdir), "%s/%s/%s/db/user", a->sys_dir, ps4 ? "savedata" : "savedata_prospero", a->uid) >= (int)sizeof(dbdir)) { fail(r, "Ein Pfad ist zu lang."); goto done; }
    if(stat(dbdir, &st) != 0 || !S_ISDIR(st.st_mode)) {
      fail(r, "Für diesen Benutzer gibt es an der Konsole keinen Ordner für die Spielstand-Datenbank. Es wurde nichts verändert.");
      goto done;
    }
    if(stat(dbp0, &st) == 0 && savedb_has_slot(dbp0, info.title_id, info.slot, e2, sizeof(e2)) < 0) {
      fail(r, "Die Spielstand-Datenbank dieses Benutzers lässt sich nicht lesen. %s Es wurde nichts verändert.", e2);
      goto done;
    }
  }

  /* 5. the console is changed from here on */
  if(h->no_return) h->no_return(h->base.ctx);
  committed = 1;
  phase(r, "Spielstand installieren", 0);
  current(r, "");
  char dir[PATH_MAX], fin[PATH_MAX], neu[PATH_MAX], alt[PATH_MAX];
  char idir[PATH_MAX], ifin[PATH_MAX], ineu[PATH_MAX], ialt[PATH_MAX];
  char dbp[PATH_MAX], dbb[PATH_MAX];
  char bfin[PATH_MAX], bneu[PATH_MAX], balt[PATH_MAX];
  int image_in = 0, replaced = 0, icon_in = 0, icon_replaced = 0, db_existed = 0, bin_in = 0, bin_replaced = 0;
  const char *adir = ps4 ? "savedata" : "savedata_prospero";
  const char *mdir = ps4 ? "savedata_meta" : "savedata_prospero_meta";
  if(snprintf(dir, sizeof(dir), "%s/%s/%s/%s", a->home_dir, a->uid, adir, info.title_id) >= (int)sizeof(dir) ||
     snprintf(fin, sizeof(fin), "%s/sdimg_%s", dir, info.slot) >= (int)sizeof(fin) ||
     snprintf(neu, sizeof(neu), "%s.ps5cc-neu", fin) >= (int)sizeof(neu) || snprintf(alt, sizeof(alt), "%s.ps5cc-alt", fin) >= (int)sizeof(alt) ||
     snprintf(idir, sizeof(idir), "%s/%s/%s/user/%s", a->home_dir, a->uid, mdir, info.title_id) >= (int)sizeof(idir) ||
     snprintf(bfin, sizeof(bfin), "%s/%s.bin", dir, info.slot) >= (int)sizeof(bfin) ||
     snprintf(bneu, sizeof(bneu), "%s.ps5cc-neu", bfin) >= (int)sizeof(bneu) || snprintf(balt, sizeof(balt), "%s.ps5cc-alt", bfin) >= (int)sizeof(balt) ||
     snprintf(ifin, sizeof(ifin), "%s/%s_icon0.png", idir, info.slot) >= (int)sizeof(ifin) ||
     snprintf(ineu, sizeof(ineu), "%s.ps5cc-neu", ifin) >= (int)sizeof(ineu) || snprintf(ialt, sizeof(ialt), "%s.ps5cc-alt", ifin) >= (int)sizeof(ialt) ||
     snprintf(dbp, sizeof(dbp), "%s/%s/%s/db/user/savedata.db", a->sys_dir, adir, a->uid) >= (int)sizeof(dbp)) {
    fail(r, "Ein Pfad ist zu lang.");
    goto done;
  }
  {
    struct stat st;
    int e = mkdir_like_parent(dir);
    if(e) { fail(r, "Der Ordner des Spielstands ließ sich nicht anlegen: %s", ps5tm_io_strerror(e)); goto done; }
    /* a cut-off earlier installation: the old files go back, the unfinished ones go */
    if(lstat(alt, &st) == 0) (void)rename(alt, fin);
    unlink(neu);
    if(lstat(ialt, &st) == 0) (void)rename(ialt, ifin);
    unlink(ineu);
    if(ps4) {
      if(lstat(balt, &st) == 0) (void)rename(balt, bfin);
      unlink(bneu);
    }
    replaced = lstat(fin, &st) == 0;

    e = copy_new(img, neu, 0644);
    if(e) { fail(r, "Das Abbild ließ sich nicht an den Platz des Spielstands kopieren: %s", ps5tm_io_strerror(e)); goto done; }
    if(ps4) {
      e = copy_new(bin_tmp, bneu, 0644);
      if(e) { unlink(neu); fail(r, "Die Schlüsseldatei ließ sich nicht an den Platz des Spielstands kopieren: %s", ps5tm_io_strerror(e)); goto done; }
      bin_replaced = lstat(bfin, &st) == 0;
    }
    /* the renames of the image and of its key file one after the other, so that the time in which they do not belong together is as short as it can be */
    if(replaced && rename(fin, alt) != 0) { const int re = errno ? errno : EIO; unlink(neu); if(ps4) unlink(bneu); fail(r, "Der bisherige Spielstand ließ sich nicht beiseite legen: %s", ps5tm_io_strerror(re)); goto done; }
    if(ps4 && bin_replaced && rename(bfin, balt) != 0) {
      const int re = errno ? errno : EIO;
      if(replaced) (void)rename(alt, fin);
      unlink(neu);
      unlink(bneu);
      fail(r, "Die bisherige Schlüsseldatei ließ sich nicht beiseite legen: %s", ps5tm_io_strerror(re));
      goto done;
    }
    if(rename(neu, fin) != 0) {
      const int re = errno ? errno : EIO;
      if(replaced) (void)rename(alt, fin);
      if(ps4 && bin_replaced) (void)rename(balt, bfin);
      unlink(neu);
      if(ps4) unlink(bneu);
      fail(r, "Der neue Spielstand ließ sich nicht an seinen Platz legen: %s", ps5tm_io_strerror(re));
      goto done;
    }
    image_in = 1;
    if(ps4) {
      if(rename(bneu, bfin) != 0) {
        const int re = errno ? errno : EIO;
        if(replaced) (void)rename(alt, fin); else unlink(fin);
        if(bin_replaced) (void)rename(balt, bfin);
        unlink(bneu);
        image_in = 0;
        fail(r, "Die Schlüsseldatei ließ sich nicht an ihren Platz legen: %s", ps5tm_io_strerror(re));
        goto done;
      }
      bin_in = 1;
    }

    if(have_icon) {
      if(mkdir_like_parent(idir) == 0 && copy_new(icon_tmp, ineu, 0644) == 0) {
        icon_replaced = lstat(ifin, &st) == 0;
        if(icon_replaced && rename(ifin, ialt) != 0) { unlink(ineu); }
        else if(rename(ineu, ifin) != 0) { if(icon_replaced) (void)rename(ialt, ifin); unlink(ineu); }
        else icon_in = 1;
      }
      if(!icon_in) snprintf(res->note, sizeof(res->note), "Das Bild des Spielstands ließ sich nicht ablegen; der Spielstand selbst ist eingespielt.");
    }
  }

  /* the row in the database, after a copy of it */
  {
    char stamp[24] = "00000000-000000";
    time_t now = time(NULL);
    struct tm tmv;
    if(gmtime_r(&now, &tmv)) strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &tmv);
    struct stat st;
    db_existed = stat(dbp, &st) == 0;
    char e2[400];
    int dbr = 0;
    dbb[0] = 0;
    if(db_existed && a->db_backup_dir && a->db_backup_dir[0]) {
      if(mkdir_like_parent(a->db_backup_dir) != 0 || snprintf(dbb, sizeof(dbb), "%s/savedata-%s-%s.db", a->db_backup_dir, a->uid, stamp) >= (int)sizeof(dbb)) dbr = -1;
      else if(savedb_backup(dbp, dbb, e2, sizeof(e2)) != 0) dbr = -1;
      if(dbr != 0) snprintf(e2, sizeof(e2), "Die Spielstand-Datenbank ließ sich nicht sichern.");
    } else if(db_existed) {
      dbr = -1;
      snprintf(e2, sizeof(e2), "Es gibt keinen Platz für die Sicherung der Spielstand-Datenbank.");
    }
    if(dbr == 0) {
      savedb_row_t row;
      memset(&row, 0, sizeof(row));
      snprintf(row.title_id, sizeof(row.title_id), "%s", info.title_id);
      snprintf(row.dir_name, sizeof(row.dir_name), "%s", info.slot);
      snprintf(row.main_title, sizeof(row.main_title), "%s", info.main_title);
      snprintf(row.sub_title, sizeof(row.sub_title), "%s", info.sub_title);
      snprintf(row.detail, sizeof(row.detail), "%s", info.detail);
      strftime(row.mtime, sizeof(row.mtime), "%Y-%m-%dT%H:%M:%S.00Z", &tmv);
      row.account_id = a->account_id;
      row.user_id = a->user_id;
      row.user_param = info.user_param;
      row.blocks = info.block_count;
      row.free_blocks = free_blocks;
      row.size_kib = info.image_size / 1024;
      row.system_blocks = 0;
      int inserted = 0;
      dbr = savedb_upsert(dbp, &row, &inserted, e2, sizeof(e2));
      if(dbr == 0) dbr = savedb_check(dbp, e2, sizeof(e2));
      res->db_inserted = inserted;
    }
    if(dbr != 0) {
      /* everything goes back as it was */
      if(db_existed && dbb[0]) {
        char dneu[PATH_MAX];
        if(snprintf(dneu, sizeof(dneu), "%s.ps5cc-neu", dbp) < (int)sizeof(dneu) && copy_new(dbb, dneu, 0644) == 0) (void)rename(dneu, dbp);
      } else if(!db_existed) {
        unlink(dbp);
      }
      if(icon_in) { if(icon_replaced) (void)rename(ialt, ifin); else unlink(ifin); }
      if(bin_in) { if(bin_replaced) (void)rename(balt, bfin); else unlink(bfin); }
      if(image_in) { if(replaced) (void)rename(alt, fin); else unlink(fin); }
      fail(r, "%s Der Spielstand wurde nicht eingespielt; alles ist wieder so wie vorher.", e2);
      goto done;
    }
  }
  /* done: the old files that were kept for the way back may go */
  if(replaced) unlink(alt);
  if(icon_replaced) unlink(ialt);
  if(bin_in && bin_replaced) unlink(balt);
  snprintf(res->image, sizeof(res->image), "%.319s", fin);
  snprintf(res->db_backup, sizeof(res->db_backup), "%.319s", dbb);
  res->replaced = replaced;
  res->files = info.files;
  res->bytes = info.bytes;
  if(f.chmod_failed && !res->note[0]) snprintf(res->note, sizeof(res->note), "Bei %d Dateien ließen sich die Zugriffsrechte der Quelle nicht übernehmen.", f.chmod_failed);
  if(owner_note[0]) {
    const size_t on = strlen(res->note);
    snprintf(res->note + on, sizeof(res->note) - on, "%s%.350s", on ? " " : "", owner_note);
  }
  rc = 0;

done:
  if(mounted) {
    char e2[300];
    if(savemnt_close(&mt, e2, sizeof(e2)) != 0) {
      stuck = 1;
      if(rc == ECANCELED) rc = -1;
      if(!err[0]) snprintf(err, err_len, "%s Bitte die Konsole neu starten, bevor es noch einmal versucht wird.", e2);
    }
    mounted = 0;
  }
  if(!stuck) {
    if(have_img) unlink(img);
    if(mnt[0]) rmdir(mnt);
  }
  if(icon_tmp[0]) unlink(icon_tmp);
  if(bin_tmp[0]) unlink(bin_tmp);
  if(z) szr_close(z);
  free(sfo);
  chives_free(&geo);
  (void)committed;
  if(rc != 0 && rc != ECANCELED && err_len && !err[0]) snprintf(err, err_len, "Der Spielstand ließ sich nicht einspielen.");
  return rc;
}
