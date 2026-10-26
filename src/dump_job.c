/* SPDX-License-Identifier: GPL-3.0-or-later
 * Dumping a running PS5 game, and the DLC mounted with it (23.10.2026).
 *
 * What is dumped, where it comes from and what goes with it follows ps5-app-dumper 2.10 (Copyright (C) 2025 EchoStretch,
 * Copyright (C) 2026 slopmaster33; GPL-3.0-or-later; ps5_dumper.c, dump_job.c, dump_store.c, app_scan.c), rewritten for this
 * app. Not taken over: the queue (a later round). Decrypting the SELF/SPRX files (dump_self.c) and the SDK-version backport
 * (dump_backport.c) come after the copy, as in the dumper. A PS4 title is
 * dumped the dumper's way (24.10.2026, dump_ps4.c): the PFS image of the running title (<TITLE>-app0-nest/pfs_image.dat, and
 * -patch0-nest for the update) is unpacked into <TITLE>-app0 and <TITLE>-patch0 (or one folder, see ps4_split), plus the two
 * metadata files, the files of the installed package, and the trophy file.
 * The files inside the installed app.pkg of the game and the ac.pkg of each DLC that the console does not mount (the rest of
 * sce_sys) come out of the packages (dump_unpkg.c), where the dumper takes them as well; only what is missing is added.
 *
 * The source is the folder the console mounts for the running title: /mnt/sandbox/pfsmnt/<TITLE>-app0 (the files in it are
 * readable as they are; the game has to run for the mount to exist). Besides it come
 *   - the title's metadata, /user/appmeta/<TITLE> and /system_data/priv/appmeta/<TITLE>, into sce_sys of the dump, and the
 *     trophy and UDS configuration files that the npbind.dat of the title names;
 *   - the DLC that bring files of their own, mounted next to the game as <region>-<TITLE>_00-<LABEL>-ac, each as a
 *     folder of its own beside the game's, with the metadata of the DLC in its sce_sys.
 * ShadowMountPlus recognises a game folder by sce_sys/param.json. That file and param.sfo are therefore copied last under a
 * temporary name and renamed into place at the very end, so a half-finished dump is never taken for a game.
 *
 * Next to each folder lies "<folder>.dump-info.json" (the format of ps5-app-dumper, so that both can read each other's
 * dumps): written as "running" before the first file, and "done", "failed" or "aborted" at the end. An older dump of the title
 * in the way is removed first when it is unfinished; a finished one only when the person has said so (overwrite).
 *
 * The copy uses what the game copy uses: the console is kept awake (powerguard.c), a big file is read ahead while the last piece
 * is written (iopolicy.c), the words for a drive that left come from ioerr.h, and the history (jobhist.c) gets a line.
 * Optionally every file is read back from the drive and its CRC-32 compared (verify); the SHA-256 sums then go into
 * "<folder>.sha256" next to the dump. One job at a time, on a single thread of its own, and none beside another file job:
 * ps5tm_gamecopy_busy() answers 1 while a dump runs, which every other file job asks before it starts. */

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
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#ifndef PS5TM_HOST_TEST
#include <sys/mount.h>
#endif

#include "checkfile.h"
#include "dump_ps4.h"
#include "dump_backport.h"
#include "dump_self.h"
#include "dump_unpkg.h"
#include "ioerr.h"
#include "iopolicy.h"
#include "ps5tm.h"
#include "third_party/cJSON.h"
#include "third_party/libdeflate/libdeflate.h"

#ifndef DJ_SANDBOX
#define DJ_SANDBOX      "/mnt/sandbox/pfsmnt"
#endif
#define DJ_DIR          PS5TM_DATA_DIR "/dump"
#define DJ_SETTINGS     DJ_DIR "/settings.json"
#define DJ_LOGDIR       DJ_DIR "/logs"
#define DJ_SUBDIR_DEF   "homebrew/ps5-app-dumper/dumps"
#define DJ_RESERVE      (10ull << 30)       /* what a dump to the console's own storage leaves free */
#define DJ_BUF          (1u << 20)
#define DJ_PREFETCH_MIN (8ull << 20)
#define DJ_PREFETCH_BUFS 4
#define DJ_DEPTH        24
#define DJ_DLC_MAX      16
#define DJ_NAME_MAX     96
#define DJ_FAT32_MAX    0xFFFFFFFFull
#define DJ_PART         ".ps5cc-teil"
#define DJ_INFO_SUFFIX  ".dump-info.json"
#define DJ_LOGS_KEPT    20
#define DJ_TITLE_SUFFIX 40
#define SRC_SIDE        0x10000             /* or'ed onto an errno that the source caused */
#define E_SOURCE_CHANGED 1000
#define E_VERIFY_FAILED  1001

enum { DJ_IDLE, DJ_MEASURING, DJ_COPYING, DJ_FINISHING, DJ_DONE, DJ_FAILED, DJ_CANCELLED };
static const char *const k_state[] = { "idle", "measuring", "copying", "finishing", "done", "failed", "cancelled" };

/* The files that tell ShadowMountPlus a folder is a game: copied last. */
static const char *const k_defer_app[]  = { "sce_sys/param.json", "sce_sys/param.sfo" };
static const char *const k_defer_meta[] = { "param.json", "param.sfo" };

typedef struct {
  char target[40];            /* mount path of the chosen drive; "" = none chosen yet */
  char subdir[96];
  int  folder_titles, dump_dlc, verify;
  int  ps4_split;             /* 0 one folder, 1 app, 2 patch, 3 both apart */
  int  decrypt, fself;        /* write plain executables (dump_self.c); re-sign them as FSELF */
  int  backport, bp_ps4, bp_ps5;  /* lower the SDK version in the dump (dump_backport.c); the levels 1..6 and 1..10 */
} dj_cfg_t;

typedef struct {
  int      state;
  char     title_id[12];
  char     name[128];
  char     kind[8];           /* "game" or "dlc" */
  char     dest[320];         /* the folder the dump folders go into */
  char     folder[DJ_NAME_MAX];
  char     drive[40];
  char     stage[64];
  uint64_t total_bytes, done_bytes, checked_bytes;
  unsigned files_total, files_done;
  unsigned dlc_total, dlc_done;
  unsigned dec_total, dec_done, dec_failed;   /* executables looked at for decrypting */
  unsigned bp_total, bp_done, bp_patched;     /* executables looked at for the SDK backport */
  char     current[192];
  char     error[640];
  char     note[240];
  char     log[200];
  uint64_t started_ms, finished_ms, window_ms, window_bytes;
  double   rate;
  int      verify;
} dj_job_t;

typedef struct {
  char     title_id[12];
  char     name[128];
  char     base[64];
  char     label[40];
  char     dest[320];
  int      console;           /* the drive is the console's own storage */
  int      overwrite, dlc_only;
  dj_cfg_t cfg;
  char     origin[16];        /* "disc-tab": the Disc tab lists only the dumps it made itself (dump_lib.c) */
} dj_args_t;

typedef struct {
  uint64_t bytes, largest;
  unsigned files, dirs, skipped, max_rel;
  int      err;
} tree_t;

typedef struct {
  ps5tm_sums_t *sums;
  const char   *prefix;       /* the dump folder's name, first part of a name in the checksum file */
  const char   *inner;        /* what lies between it and the copied tree's own paths: "" or "sce_sys/" */
  int           parallel, verify, replace;
  const char *const *defer;
  size_t        ndefer;
} cp_ctx_t;

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static dj_job_t        g_job;
static dj_cfg_t        g_cfg = { "", DJ_SUBDIR_DEF, 1, 1, 0, 3, 0, 0, 0, 4, 1 };
static int             g_cfg_loaded;
static int             g_info_bp, g_info_bp4, g_info_bp5;   /* the backport ran, with these levels */
static int             g_info_dec, g_info_fself;  /* what the running dump really did, for its info files (the worker's own) */
static int             g_cancel;
static FILE           *g_log;                    /* the worker's own; nobody else touches it */

/* ---------------------------------------------------------------- small helpers */

static int
is_ppsa(const char *s) {
  if(!s || strlen(s) != 9 || strncmp(s, "PPSA", 4)) return 0;
  for(int i = 4; i < 9; i++)
    if(!isdigit((unsigned char)s[i])) return 0;
  return 1;
}

static int
is_cusa(const char *s) {
  if(!s || strlen(s) != 9 || strncmp(s, "CUSA", 4)) return 0;
  for(int i = 4; i < 9; i++)
    if(!isdigit((unsigned char)s[i])) return 0;
  return 1;
}

static int
is_tid(const char *s) {
  return is_ppsa(s) || is_cusa(s);
}

static int
is_dir(const char *p) {
  struct stat st;
  return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

static int
lis_reg(const char *p, uint64_t *size) {
  struct stat st;
  if(lstat(p, &st) != 0 || !S_ISREG(st.st_mode)) return 0;
  if(size) *size = (uint64_t)st.st_size;
  return 1;
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

static int
write_atomic(const char *path, const char *data, size_t n) {
  char tmp[PATH_MAX];
  snprintf(tmp, sizeof(tmp), "%s.tmp", path);
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
dlog(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

static void
dlog(const char *fmt, ...) {
  if(!g_log) return;
  time_t t = time(NULL);
  struct tm tmv;
  char ts[16] = "--:--:--";
  if(localtime_r(&t, &tmv)) strftime(ts, sizeof(ts), "%H:%M:%S", &tmv);
  fprintf(g_log, "[%s] ", ts);
  va_list ap;
  va_start(ap, fmt);
  vfprintf(g_log, fmt, ap);
  va_end(ap);
  fputc('\n', g_log);
  fflush(g_log);
}

static const char *
errno_text(int code) {
  int src = code & SRC_SIDE;
  int e   = code & ~SRC_SIDE;
  switch(e) {
    case E_SOURCE_CHANGED: return "Die Quelle hat sich während des Dumpens geändert.";
    case E_VERIFY_FAILED:  return "Beim Zurücklesen weicht die Datei auf dem Ziel von dem ab, was gelesen wurde. Ist das Ziel defekt oder der Stecker locker?";
    case ENOSPC:           return "Das Ziel ist voll.";
    case EFBIG:            return "Eine Datei ist zu groß für das Dateisystem des Ziels (FAT32: höchstens 4 GB je Datei).";
    case EIO:
    case ENXIO:
    case ENODEV:           return ps5tm_io_strerror(e);
    case EROFS:            return "Das Ziel lässt sich nicht beschreiben.";
    case EACCES:
    case EPERM:            return src ? "Eine Datei des Spiels lässt sich nicht lesen." : "Das Ziel lässt sich nicht beschreiben.";
    case ENAMETOOLONG:     return "Ein Dateipfad ist zu lang.";
    case EEXIST:           return "Dort liegt schon etwas mit diesem Namen.";
    case ENOENT:           return src ? "Eine Datei oder ein Ordner des Spiels ist verschwunden."
                                      : "Der Zielordner ist verschwunden. Ist der Datenträger noch angeschlossen?";
    case ELOOP:            return "Der Ordner ist zu tief verschachtelt.";
    default:               return strerror(e);
  }
}

static int
cancelled(void) {
  return __atomic_load_n(&g_cancel, __ATOMIC_ACQUIRE);
}

/* ---------------------------------------------------------------- names */

static int
suffix_char(char c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.' ||
         c == '(' || c == ')' || c == '&' || c == '+' || c == '!' || c == '\'';
}

/* The game's title for the end of a folder name ("_ASTROs_PLAYROOM"): spaces become '_', what a file system or a tool may trip
   over is left out, at most 40 characters, cut at the end of the last whole word when that keeps more than half. */
static void
title_suffix(const char *title, char *out, size_t out_size) {
  char buf[DJ_TITLE_SUFFIX + 1];
  size_t n = 0;
  const char *p = title ? title : "";
  for(; *p && n < DJ_TITLE_SUFFIX; p++) {
    char c = (*p == ' ') ? '_' : *p;
    if(!suffix_char(c)) continue;
    if(c == '_' && (n == 0 || buf[n - 1] == '_')) continue;
    buf[n++] = c;
  }
  if(*p && *p != ' ' && n == DJ_TITLE_SUFFIX) {
    size_t w = n;
    while(w && buf[w - 1] != '_') w--;
    if(w > DJ_TITLE_SUFFIX / 2) n = w;
  }
  while(n && (buf[n - 1] == '_' || buf[n - 1] == '.')) n--;
  buf[n] = 0;
  if(out_size) snprintf(out, out_size, n ? "_%s" : "%s", buf);
}

/* "<TITLE>-app0" with or without "_Title" behind it. A PS4 title also has "<TITLE>-patch0" and, undivided, "<TITLE>" alone. */
static int
folder_is_game_of(const char *name, const char *tid) {
  size_t tl = strlen(tid);
  if(strlen(name) < tl || strncmp(name, tid, tl)) return 0;
  const char *t = name + tl;
  if(!strncmp(t, "-app0", 5)) t += 5;
  else if(is_cusa(tid) && !strncmp(t, "-patch0", 7)) t += 7;
  else if(!is_cusa(tid)) return 0;
  if(*t && *t != '_') return 0;
  for(; *t; t++)
    if(!suffix_char(*t)) return 0;
  return 1;
}

/* <region>-<TITLE>_00-<LABEL>-ac: a DLC, as pfsmnt names it. */
static int
dlc_name_fits(const char *name, const char *mark) {
  size_t len = strlen(name);
  if(len < 20 || len >= DJ_NAME_MAX || !strstr(name, mark)) return 0;
  if(strcmp(name + len - 3, "-ac")) return 0;
  for(size_t i = 0; i < len; i++)
    if(!isalnum((unsigned char)name[i]) && name[i] != '-' && name[i] != '_') return 0;
  return 1;
}

static int
find_dlc(const char *tid, char names[DJ_DLC_MAX][DJ_NAME_MAX]) {
  int n = 0;
  char mark[24];
  snprintf(mark, sizeof(mark), "-%s_00-", tid);
  DIR *d = opendir(DJ_SANDBOX);
  if(d) {
    struct dirent *e;
    while((e = readdir(d)) != NULL && n < DJ_DLC_MAX) {
      if(!dlc_name_fits(e->d_name, mark)) continue;
      int seen = 0;
      for(int i = 0; i < n; i++) if(!strcmp(names[i], e->d_name)) seen = 1;
      if(!seen) snprintf(names[n++], DJ_NAME_MAX, "%s", e->d_name);
    }
    closedir(d);
  }
#ifndef PS5TM_HOST_TEST
  /* a console that shows the mounts but not the listing still has the mount table */
  struct statfs *mnt = NULL;
  int cnt = getmntinfo(&mnt, MNT_NOWAIT);
  const size_t plen = strlen(DJ_SANDBOX "/");
  for(int i = 0; i < cnt && n < DJ_DLC_MAX; i++) {
    const char *on = mnt[i].f_mntonname;
    if(strncmp(on, DJ_SANDBOX "/", plen) || strchr(on + plen, '/') || !dlc_name_fits(on + plen, mark)) continue;
    int seen = 0;
    for(int k = 0; k < n; k++) if(!strcmp(names[k], on + plen)) seen = 1;
    if(!seen) snprintf(names[n++], DJ_NAME_MAX, "%s", on + plen);
  }
#endif
  return n;
}

int
ps5tm_dump_mounted(const char *title_id) {
  if(!is_tid(title_id)) return 0;
  char p[96];
  snprintf(p, sizeof(p), DJ_SANDBOX "/%s-app0", title_id);
  return is_dir(p);
}

/* a backport level pulled into 1..max, as the dumper's settings route does */
static int
clamp_level(int v, int max) {
  return v < 1 ? 1 : v > max ? max : v;
}

/* 1 for a folder name of the settings that is safe to build a path from: one to three plain segments. */
static int
subdir_ok(const char *s) {
  size_t len = s ? strlen(s) : 0;
  if(len == 0 || len >= 96 || s[0] == '/' || s[len - 1] == '/') return 0;
  int segs = 1, seglen = 0;
  for(size_t i = 0; i < len; i++) {
    unsigned char c = (unsigned char)s[i];
    if(c == '/') {
      if(seglen == 0) return 0;
      segs++;
      seglen = 0;
      continue;
    }
    if(!(isalnum(c) || c == '.' || c == '_' || c == '-' || c == ' ')) return 0;
    seglen++;
    if((seglen == 1 && c == '.' && (i + 1 == len || s[i + 1] == '/')) ||
       (seglen == 2 && c == '.' && s[i - 1] == '.' && (i + 1 == len || s[i + 1] == '/')))
      return 0;
  }
  return seglen > 0 && segs <= 3;
}

/* ---------------------------------------------------------------- settings */

static void
cfg_load_locked(void) {
  if(g_cfg_loaded) return;
  g_cfg_loaded = 1;
  char *b = read_small(DJ_SETTINGS, 4096);
  cJSON *j = b ? cJSON_Parse(b) : NULL;
  free(b);
  if(!j) return;
  const cJSON *v;
  if(cJSON_IsString(v = cJSON_GetObjectItem(j, "target")) && strlen(v->valuestring) < sizeof(g_cfg.target))
    snprintf(g_cfg.target, sizeof(g_cfg.target), "%s", v->valuestring);
  if(cJSON_IsString(v = cJSON_GetObjectItem(j, "subdir")) && subdir_ok(v->valuestring))
    snprintf(g_cfg.subdir, sizeof(g_cfg.subdir), "%s", v->valuestring);
  if(cJSON_IsBool(v = cJSON_GetObjectItem(j, "folder_titles"))) g_cfg.folder_titles = cJSON_IsTrue(v);
  if(cJSON_IsBool(v = cJSON_GetObjectItem(j, "dump_dlc"))) g_cfg.dump_dlc = cJSON_IsTrue(v);
  if(cJSON_IsBool(v = cJSON_GetObjectItem(j, "verify"))) g_cfg.verify = cJSON_IsTrue(v);
  if(cJSON_IsNumber(v = cJSON_GetObjectItem(j, "ps4_split")) && v->valuedouble >= 0 && v->valuedouble <= 3) g_cfg.ps4_split = (int)v->valuedouble;
  if(cJSON_IsBool(v = cJSON_GetObjectItem(j, "decrypt"))) g_cfg.decrypt = cJSON_IsTrue(v);
  if(cJSON_IsBool(v = cJSON_GetObjectItem(j, "fself"))) g_cfg.fself = cJSON_IsTrue(v);
  if(cJSON_IsBool(v = cJSON_GetObjectItem(j, "backport"))) g_cfg.backport = cJSON_IsTrue(v);
  if(cJSON_IsNumber(v = cJSON_GetObjectItem(j, "bp_ps4"))) g_cfg.bp_ps4 = clamp_level((int)v->valuedouble, DJ_BP_PS4_LEVELS);
  if(cJSON_IsNumber(v = cJSON_GetObjectItem(j, "bp_ps5"))) g_cfg.bp_ps5 = clamp_level((int)v->valuedouble, DJ_BP_PS5_LEVELS);
  if(!g_cfg.decrypt) g_cfg.fself = g_cfg.backport = 0;
  cJSON_Delete(j);
}

static dj_cfg_t
cfg_get(void) {
  pthread_mutex_lock(&g_lock);
  cfg_load_locked();
  dj_cfg_t c = g_cfg;
  pthread_mutex_unlock(&g_lock);
  return c;
}

static void
cfg_save(const dj_cfg_t *c) {
  cJSON *o = cJSON_CreateObject();
  cJSON_AddStringToObject(o, "target", c->target);
  cJSON_AddStringToObject(o, "subdir", c->subdir);
  cJSON_AddBoolToObject(o, "folder_titles", c->folder_titles);
  cJSON_AddBoolToObject(o, "dump_dlc", c->dump_dlc);
  cJSON_AddBoolToObject(o, "verify", c->verify);
  cJSON_AddNumberToObject(o, "ps4_split", c->ps4_split);
  cJSON_AddBoolToObject(o, "decrypt", c->decrypt);
  cJSON_AddBoolToObject(o, "fself", c->fself);
  cJSON_AddBoolToObject(o, "backport", c->backport);
  cJSON_AddNumberToObject(o, "bp_ps4", c->bp_ps4);
  cJSON_AddNumberToObject(o, "bp_ps5", c->bp_ps5);
  char *s = cJSON_PrintUnformatted(o);
  cJSON_Delete(o);
  if(!s) return;
  mkdir(PS5TM_DATA_DIR, 0777);
  mkdir(DJ_DIR, 0777);
  if(write_atomic(DJ_SETTINGS, s, strlen(s)) != 0)
    PS5TM_WARN("dump_settings", "Dump: Die Einstellungen ließen sich nicht speichern.");
  free(s);
}

/* ---------------------------------------------------------------- drives */

/* base: where the dump folder hangs off — /data for the internal SSD, else the drive's own mount point. The drive must be
   one the console has right now. */
static int
drive_base(const char *mount, char *base, size_t base_len, char *label, size_t label_len) {
  ps5tm_sysinfo_t *info = malloc(sizeof(*info));
  if(!info) return -1;
  ps5tm_sysinfo_get(info);
  int rc = -1;
  for(unsigned i = 0; i < info->volume_count; i++) {
    if(strcmp(info->volumes[i].path, mount)) continue;
    snprintf(base, base_len, "%s", !strcmp(mount, "/user") ? "/data" : mount);
    snprintf(label, label_len, "%s", info->volumes[i].label);
    rc = 0;
    break;
  }
  free(info);
  return rc;
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

/* ---------------------------------------------------------------- info files, existing dumps, removing */

static void
json_escape(const char *in, char *out, size_t out_size) {
  size_t o = 0;
  for(const unsigned char *p = (const unsigned char *)(in ? in : ""); *p && o + 2 < out_size; p++) {
    if(*p == '"' || *p == '\\') out[o++] = '\\';
    out[o++] = (*p < 0x20) ? ' ' : (char)*p;
  }
  out[o] = 0;
}

static void
info_path(const char *dest, const char *folder, char *out, size_t n) {
  snprintf(out, n, "%s/%s" DJ_INFO_SUFFIX, dest, folder);
}

static void
write_info(const char *dest, const char *folder, const char *tid, const char *title, const char *kind, const char *state,
           uint64_t bytes, time_t started, time_t finished, const char *origin) {
  char path[640], t[300], o[16];
  info_path(dest, folder, path, sizeof(path));
  json_escape(title, t, sizeof(t));
  snprintf(o, sizeof(o), "%s", origin && origin[0] ? origin : "disc-tab");
  FILE *f = fopen(path, "w");
  if(!f) return;
  fprintf(f,
          "{\n"
          "  \"tool\": \"ps5-cooling-center\",\n"
          "  \"origin\": \"%s\",\n"
          "  \"version\": \"" PS5TM_VERSION "\",\n"
          "  \"kind\": \"%s\",\n"
          "  \"titleId\": \"%s\",\n"
          "  \"title\": \"%s\",\n"
          "  \"folder\": \"%s\",\n"
          "  \"state\": \"%s\",\n"
          "  \"started\": %lld,\n"
          "  \"finished\": %lld,\n"
          "  \"bytes\": %llu,\n"
          "  \"settings\": { \"decrypt\": %d, \"fself\": %d, \"backport\": %d, \"ps4BackportLevel\": %d, "
          "\"ps5BackportLevel\": %d, \"split\": 0 }\n"
          "}\n",
          o, kind, tid, t, folder, state, (long long)started, (long long)finished, (unsigned long long)bytes,
          !strcmp(kind, "game") ? g_info_dec : 0, !strcmp(kind, "game") ? g_info_fself : 0,
          !strcmp(kind, "game") ? g_info_bp : 0, !strcmp(kind, "game") && g_info_bp ? g_info_bp4 : 0,
          !strcmp(kind, "game") && g_info_bp ? g_info_bp5 : 0);
  fflush(f);
  fsync(fileno(f));
  fclose(f);
}

/* the "state" of a folder's info file; "" when there is none (an older dump, or something else) */
static void
info_state(const char *dest, const char *folder, char *out, size_t n) {
  out[0] = 0;
  char path[640];
  info_path(dest, folder, path, sizeof(path));
  char *b = read_small(path, 8192);
  cJSON *j = b ? cJSON_Parse(b) : NULL;
  free(b);
  if(!j) return;
  const cJSON *v = cJSON_GetObjectItem(j, "state");
  if(cJSON_IsString(v)) snprintf(out, n, "%s", v->valuestring);
  cJSON_Delete(j);
}

/* The folders of this title's game dump below dest. worst: 0 none, 1 finished (or without an info file), 2 unfinished. */
static int
find_existing(const char *dest, const char *tid, char names[4][DJ_NAME_MAX], int *worst) {
  int n = 0;
  *worst = 0;
  DIR *d = opendir(dest);
  if(!d) return 0;
  struct dirent *e;
  while((e = readdir(d)) != NULL && n < 4) {
    if(!folder_is_game_of(e->d_name, tid) || strlen(e->d_name) >= DJ_NAME_MAX) continue;
    char full[PATH_MAX];
    snprintf(full, sizeof(full), "%s/%s", dest, e->d_name);
    if(!is_dir(full)) continue;
    snprintf(names[n++], DJ_NAME_MAX, "%s", e->d_name);
    char st[16];
    info_state(dest, e->d_name, st, sizeof(st));
    if(st[0] && strcmp(st, "done")) *worst = 2;
    else if(*worst == 0) *worst = 1;
  }
  closedir(d);
  return n;
}

/* Depth first, never following a link (everything is looked at with lstat; a link is unlinked, not entered). */
static void
remove_tree(char *path, size_t len, int depth) {
  if(depth > DJ_DEPTH + 2) return;
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

/* Removes one dump folder of ours (named for the title), its info file and its checksum file. */
static void
remove_dump_folder(const char *dest, const char *folder) {
  char path[PATH_MAX];
  if(snprintf(path, sizeof(path), "%s/%s", dest, folder) >= (int)sizeof(path)) return;
  remove_tree(path, strlen(path), 0);
  info_path(dest, folder, path, sizeof(path));
  unlink(path);
  snprintf(path, sizeof(path), "%s/%s" PS5TM_SUMS_EXT, dest, folder);
  unlink(path);
}

/* ---------------------------------------------------------------- measuring */

static int
is_deferred(const char *rel, const cp_ctx_t *cx) {
  for(size_t i = 0; cx && i < cx->ndefer; i++)
    if(!strcmp(rel, cx->defer[i])) return 1;
  return 0;
}

static void
tree_scan(char *path, size_t len, size_t root, int depth, tree_t *t, const cp_ctx_t *cx) {
  if(t->err) return;
  if(depth > DJ_DEPTH) { t->err = ELOOP; return; }
  DIR *d = opendir(path);
  if(!d) { t->err = SRC_SIDE | (errno ? errno : EIO); return; }
  struct dirent *e;
  int rd_errno = 0;
  while(!t->err) {
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
      tree_scan(path, len + 1 + n, root, depth + 1, t, cx);
    } else if(S_ISREG(st.st_mode)) {
      if(!is_deferred(path + root + 1, cx)) {
        t->files++;
        t->bytes += (uint64_t)st.st_size;
        if((uint64_t)st.st_size > t->largest) t->largest = (uint64_t)st.st_size;
      }
    } else {
      t->skipped++;
    }
    path[len] = 0;
  }
  closedir(d);
  if(!t->err && rd_errno) t->err = SRC_SIDE | rd_errno;
}

static tree_t
measure(const char *root, const cp_ctx_t *cx) {
  tree_t t;
  memset(&t, 0, sizeof(t));
  char *path = malloc(PATH_MAX);
  if(!path) { t.err = ENOMEM; return t; }
  snprintf(path, PATH_MAX, "%s", root);
  tree_scan(path, strlen(path), strlen(path), 0, &t, cx);
  free(path);
  return t;
}

static void
tree_add(tree_t *sum, const tree_t *t) {
  sum->bytes += t->bytes;
  sum->files += t->files;
  sum->dirs += t->dirs;
  sum->skipped += t->skipped;
  if(t->largest > sum->largest) sum->largest = t->largest;
  if(t->max_rel > sum->max_rel) sum->max_rel = t->max_rel;
}

/* ---------------------------------------------------------------- progress */

static void
job_work(uint64_t n, int read_back) {
  uint64_t now = ps5tm_mono_ms();
  pthread_mutex_lock(&g_lock);
  if(read_back) g_job.checked_bytes += n;
  else          g_job.done_bytes += n;
  uint64_t work = g_job.done_bytes + g_job.checked_bytes;
  if(!g_job.window_ms) {
    g_job.window_ms = now;
    g_job.window_bytes = work;
  } else if(now - g_job.window_ms >= 1000) {
    double inst = (double)(work - g_job.window_bytes) * 1000.0 / (double)(now - g_job.window_ms);
    g_job.rate = g_job.rate > 0 ? g_job.rate * 0.7 + inst * 0.3 : inst;
    g_job.window_ms = now;
    g_job.window_bytes = work;
  }
  pthread_mutex_unlock(&g_lock);
}

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
job_state(int state, const char *stage) {
  pthread_mutex_lock(&g_lock);
  g_job.state = state;
  if(stage) snprintf(g_job.stage, sizeof(g_job.stage), "%s", stage);
  if(state >= DJ_DONE) g_job.finished_ms = ps5tm_mono_ms();
  pthread_mutex_unlock(&g_lock);
}

static void
job_add_total(uint64_t bytes, unsigned files) {
  pthread_mutex_lock(&g_lock);
  g_job.total_bytes += bytes;
  g_job.files_total += files;
  pthread_mutex_unlock(&g_lock);
}

static void
job_note(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

static void
job_note(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  pthread_mutex_lock(&g_lock);
  size_t used = strlen(g_job.note);
  if(used) { snprintf(g_job.note + used, sizeof(g_job.note) - used, " "); used = strlen(g_job.note); }
  vsnprintf(g_job.note + used, sizeof(g_job.note) - used, fmt, ap);
  pthread_mutex_unlock(&g_lock);
  va_end(ap);
}

static void
job_fail(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

static void
job_fail(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  pthread_mutex_lock(&g_lock);
  vsnprintf(g_job.error, sizeof(g_job.error), fmt, ap);
  char tid[12], why[200];
  snprintf(tid, sizeof(tid), "%s", g_job.title_id);
  snprintf(why, sizeof(why), "%s", g_job.error);
  pthread_mutex_unlock(&g_lock);
  va_end(ap);
  dlog("FEHLER: %s", why);
  PS5TM_WARN("dump_failed", "Dump von %s gescheitert: %s", tid, why);
  job_state(DJ_FAILED, "failed");
}

/* ---------------------------------------------------------------- copying */

typedef struct { int fd; } fd_src_t;

static ssize_t
fd_read(void *ctx, void *buf, size_t len) {
  fd_src_t *f = ctx;
  for(;;) {
    ssize_t r = read(f->fd, buf, len);
    if(r >= 0 || errno != EINTR) return r;
  }
}

static int
verify_written(const char *dst, uint64_t size, uint32_t want_crc, const cp_ctx_t *cx, const char *name) {
  int fd = open(dst, O_RDONLY);
  if(fd < 0) return errno ? errno : EIO;
  ps5tm_drop_cache(fd);
  uint32_t got = 0;
  uint8_t sha[32];
  char why[96];
  check_acc_t acc = { 0 };
  int rc = ps5tm_digest_fd(fd, size, &got, sha, &g_cancel, job_checked_cb, &acc, why, sizeof(why));
  int e = errno;
  close(fd);
  if(rc) return e == ECANCELED ? ECANCELED : (e ? e : EIO);
  if(got != want_crc) return E_VERIFY_FAILED;
  if(cx->sums && name) (void)ps5tm_sums_add(cx->sums, sha, name);
  return 0;
}

static int
copy_file(const char *src, const char *dst, char *buf, const cp_ctx_t *cx, const char *name) {
  int in = open(src, O_RDONLY);
  if(in < 0) return SRC_SIDE | (errno ? errno : EIO);
  struct stat st;
  uint64_t hint = fstat(in, &st) == 0 ? (uint64_t)st.st_size : 0;
  /* 0777 whatever the umask says: a game whose eboot.bin or modules lack the execute bit crashes at the start. */
  int out = open(dst, O_WRONLY | O_CREAT | (cx->replace ? O_TRUNC : O_EXCL), 0777);
  if(out < 0) {
    int e = errno ? errno : EIO;
    close(in);
    return e;
  }
  (void)fchmod(out, 0777);
  fd_src_t fs = { in };
  ps5tm_prefetch_t *pf = NULL;
  if(cx->parallel && hint >= DJ_PREFETCH_MIN) pf = ps5tm_prefetch_start(fd_read, &fs, DJ_BUF, DJ_PREFETCH_BUFS);
  int rc = 0;
  uint32_t crc = 0;
  uint64_t total = 0;
  for(;;) {
    if(cancelled()) { rc = ECANCELED; break; }
    ssize_t n = pf ? ps5tm_prefetch_read(pf, buf, DJ_BUF) : read(in, buf, DJ_BUF);
    if(n < 0) {
      if(!pf && errno == EINTR) continue;
      rc = SRC_SIDE | (errno ? errno : EIO);
      break;
    }
    if(n == 0) break;
    if(cx->verify) crc = libdeflate_crc32(crc, buf, (size_t)n);
    for(ssize_t off = 0; off < n;) {
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
    job_work((uint64_t)n, 0);
  }
  ps5tm_prefetch_stop(pf);
  if(!rc && fsync(out) != 0) rc = errno ? errno : EIO;
  if(close(out) != 0 && !rc) rc = errno ? errno : EIO;
  close(in);
  if(!rc && cx->verify) rc = verify_written(dst, total, crc, cx, name);
  if(rc && !cx->replace) unlink(dst);        /* created above with O_EXCL: ours */
  return rc;
}

/* Copies what lies below src into dst, which exists. root is the length of the source folder's own path. On an error, src
   still names the entry that failed. */
static int
copy_tree(char *src, size_t slen, char *dst, size_t dlen, size_t root, int depth, char *buf, const cp_ctx_t *cx) {
  if(depth > DJ_DEPTH) return ELOOP;
  DIR *d = opendir(src);
  if(!d) return SRC_SIDE | (errno ? errno : EIO);
  int rc = 0, rd_errno = 0;
  struct dirent *e;
  while(!rc) {
    errno = 0;
    e = readdir(d);
    if(!e) { rd_errno = errno; break; }
    if(!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
    if(cancelled()) { rc = ECANCELED; break; }
    size_t n = strlen(e->d_name);
    if(slen + 1 + n >= PATH_MAX || dlen + 1 + n >= PATH_MAX) { rc = ENAMETOOLONG; break; }
    src[slen] = '/';
    memcpy(src + slen + 1, e->d_name, n + 1);
    dst[dlen] = '/';
    memcpy(dst + dlen + 1, e->d_name, n + 1);
    struct stat st;
    if(lstat(src, &st) != 0) {
      rc = SRC_SIDE | (errno ? errno : EIO);
    } else if(S_ISDIR(st.st_mode)) {
      if(mkdir(dst, 0777) != 0 && !(cx->replace && errno == EEXIST)) rc = errno ? errno : EIO;
      else {
        (void)chmod(dst, 0777);
        rc = copy_tree(src, slen + 1 + n, dst, dlen + 1 + n, root, depth + 1, buf, cx);
      }
    } else if(S_ISREG(st.st_mode) && !is_deferred(src + root + 1, cx)) {
      char name[PATH_MAX + 256];
      snprintf(name, sizeof(name), "%s/%s%s", cx->prefix, cx->inner, src + root + 1);
      job_current(src + root + 1);
      rc = copy_file(src, dst, buf, cx, name);
      if(!rc) job_file_done();
    }
    /* Links and special files are no part of a dump; left out. */
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

/* One tree: measured elsewhere, copied here. src and dst are scratch buffers of PATH_MAX. */
static int
copy_folder(const char *from, const char *to, char *src, char *dst, char *buf, const cp_ctx_t *cx) {
  snprintf(src, PATH_MAX, "%s", from);
  snprintf(dst, PATH_MAX, "%s", to);
  return copy_tree(src, strlen(src), dst, strlen(dst), strlen(src), 0, buf, cx);
}

/* One single file that was not part of the measurement (the trophy files, the files held back): its size joins the totals first. */
static int
copy_extra(const char *src, const char *dst, char *buf, const cp_ctx_t *cx, const char *name, uint64_t *bytes_out) {
  uint64_t size = 0;
  if(!lis_reg(src, &size)) return ENOENT | SRC_SIDE;
  job_add_total(size, 1);
  if(bytes_out) *bytes_out += size;
  job_current(name);
  int rc = copy_file(src, dst, buf, cx, name);
  if(!rc) job_file_done();
  return rc;
}

/* What a phase (the game, one DLC) was measured at, against what it has read. A file that grew is no loss; one that vanished or
   shrank is. */
typedef struct { unsigned f0, ftot; uint64_t b0, btot; } phase_t;

static void
phase_start(phase_t *p, unsigned ftot, uint64_t btot) {
  pthread_mutex_lock(&g_lock);
  p->f0 = g_job.files_done;
  p->b0 = g_job.done_bytes;
  pthread_mutex_unlock(&g_lock);
  p->ftot = ftot;
  p->btot = btot;
}

static int
phase_complete(const phase_t *p, unsigned *fd, uint64_t *bd) {
  pthread_mutex_lock(&g_lock);
  *fd = g_job.files_done - p->f0;
  *bd = g_job.done_bytes - p->b0;
  pthread_mutex_unlock(&g_lock);
  return *fd >= p->ftot && *bd >= p->btot;
}

/* ---------------------------------------------------------------- the worker */

static void
log_open(const char *tid) {
  mkdir(PS5TM_DATA_DIR, 0777);
  mkdir(DJ_DIR, 0777);
  mkdir(DJ_LOGDIR, 0777);
  /* keep the last few logs only: the oldest go */
  for(int round = 0; round < 64; round++) {
    DIR *d = opendir(DJ_LOGDIR);
    if(!d) break;
    int n = 0;
    char oldest[128] = "";
    struct dirent *e;
    while((e = readdir(d)) != NULL) {
      size_t l = strlen(e->d_name);
      if(l < 5 || strcmp(e->d_name + l - 4, ".log") || l >= sizeof(oldest)) continue;
      n++;
      if(!oldest[0] || strcmp(e->d_name, oldest) < 0) snprintf(oldest, sizeof(oldest), "%s", e->d_name);
    }
    closedir(d);
    if(n < DJ_LOGS_KEPT) break;
    char p[PATH_MAX];
    snprintf(p, sizeof(p), DJ_LOGDIR "/%s", oldest);
    unlink(p);
  }
  time_t t = time(NULL);
  struct tm tmv;
  char ts[32] = "unbekannt";
  if(localtime_r(&t, &tmv)) strftime(ts, sizeof(ts), "%Y%m%d-%H%M%S", &tmv);
  char path[PATH_MAX];
  snprintf(path, sizeof(path), DJ_LOGDIR "/%s-%s.log", tid, ts);
  g_log = fopen(path, "w");
  pthread_mutex_lock(&g_lock);
  snprintf(g_job.log, sizeof(g_job.log), "%s", g_log ? path : "");
  pthread_mutex_unlock(&g_lock);
}

static void
log_close(void) {
  if(g_log) fclose(g_log);
  g_log = NULL;
}

static void
hist_note(void) {
  pthread_mutex_lock(&g_lock);
  int st = g_job.state;
  char name[128], detail[96], error[640];
  snprintf(name, sizeof(name), "%s", g_job.name);
  snprintf(detail, sizeof(detail), "nach %s", g_job.drive[0] ? g_job.drive : g_job.dest);
  snprintf(error, sizeof(error), "%s", g_job.error);
  uint64_t bytes = g_job.done_bytes;
  uint64_t dur = g_job.finished_ms > g_job.started_ms ? g_job.finished_ms - g_job.started_ms : 0;
  pthread_mutex_unlock(&g_lock);
  if(st != DJ_DONE && st != DJ_FAILED && st != DJ_CANCELLED) return;
  ps5tm_hist_add("dump", name, detail, st == DJ_DONE ? "done" : st == DJ_FAILED ? "failed" : "cancelled", error, bytes, dur);
}

/* Copies the metadata folder of a title into sce_sys of the dump: the files that come from there replace the ones pfsmnt had. */
static int
copy_meta(const char *meta, const char *sce_sys, char *src, char *dst, char *buf, const cp_ctx_t *cx) {
  if(!is_dir(meta)) return 0;
  mkdir(sce_sys, 0777);
  return copy_folder(meta, sce_sys, src, dst, buf, cx);
}

typedef struct {
  char     folder[DJ_NAME_MAX];
  tree_t   app, meta;
  char     meta_path[160];
} dlc_plan_t;

static void
fail_rc(int rc, const char *where) {
  if(rc == E_SOURCE_CHANGED) job_fail("%s", errno_text(rc));
  else job_fail("Bei %s: %s", where[0] ? where : "?", errno_text(rc));
}

static void
unpkg_cb(void *ctx, const char *cur, const char *line) {
  (void)ctx;
  if(cur) job_current(cur);
  if(line) dlog("%s", line);
}

/* A package file, also when the path is a link: a game that PS4Mount holds keeps app.pkg as a symlink to the pkg on the drive
   (the unpacker opens it and checks the file itself). The paths here are fixed system folders, never the browser's. */
static int
pkg_file(const char *p) {
  struct stat st;
  return stat(p, &st) == 0 && S_ISREG(st.st_mode);
}

/* app.pkg of the game (dlc = 0) or ac.pkg of one DLC: on the console's own drive, on an extended-storage drive or in PS4Mount's folder */
static int
find_pkg(int dlc, const char *tid, const char *label, int label_len, char *out, size_t n) {
  static const char *const roots[] = { "/user", "/mnt/ext1/user", "/mnt/ext0/user", "/system_data/ps4mount", NULL };
  for(int i = 0; roots[i]; i++) {
    int k = dlc ? snprintf(out, n, "%s/addcont/%s/%.*s/ac.pkg", roots[i], tid, label_len, label)
                : snprintf(out, n, "%s/app/%s/app.pkg", roots[i], tid);
    if(k > 0 && (size_t)k < n && pkg_file(out)) return 1;
  }
  out[0] = 0;
  return 0;
}

/* A failure here is a hint only: the dump is whole without the package's files. The state is not touched, only the stage text. */
static int
run_unpkg(const char *pkg, const char *sce_dir, const char *defer, const char *what, int deferred[2], int ps4) {
  char before[64];
  pthread_mutex_lock(&g_lock);
  snprintf(before, sizeof(before), "%s", g_job.stage);
  snprintf(g_job.stage, sizeof(g_job.stage), "Paketdaten entpacken");
  pthread_mutex_unlock(&g_lock);
  dlog("Entpacke %s", pkg);
  dj_unpkg_res_t *ur = calloc(1, sizeof(*ur));
  int r = -1;
  if(!ur) {
    dlog("Hinweis: Kein Speicher zum Entpacken von %s.", what);
  } else {
    r = ps4 ? ps5tm_dump_unpkg_ps4(pkg, sce_dir, defer, &g_cancel, unpkg_cb, NULL, ur)
            : ps5tm_dump_unpkg(pkg, sce_dir, defer, &g_cancel, unpkg_cb, NULL, ur);
    if(r == 1) {
      dlog("Beim Entpacken abgebrochen.");
    } else if(r < 0) {
      dlog("Hinweis: %s ließ sich nicht entpacken: %s", what, ur->why);
      job_note("%s ließ sich nicht entpacken. Der Dump bleibt gültig.", what);
    } else {
      dlog("Aus %s: %u Dateien geschrieben (%llu Bytes), %u waren schon da, %u übersprungen.", what, ur->extracted,
           (unsigned long long)ur->bytes, ur->existing, ur->skipped);
    }
    if(deferred) {                               /* the caller learns which held-back files came from the package */
      deferred[0] = ur->deferred[0];
      deferred[1] = ur->deferred[1];
    }
  }
  free(ur);
  pthread_mutex_lock(&g_lock);
  snprintf(g_job.stage, sizeof(g_job.stage), "%s", before);
  pthread_mutex_unlock(&g_lock);
  return r;
}

/* ---------------------------------------------------------------- decrypting */

typedef struct { unsigned base; } dec_cb_t;

static void
self_cb(void *ctx, const char *rel, unsigned done, unsigned total, const char *line) {
  const dec_cb_t *d = ctx;
  (void)total;
  if(rel) job_current(rel);
  pthread_mutex_lock(&g_lock);
  g_job.dec_done = d->base + done;
  pthread_mutex_unlock(&g_lock);
  if(line) dlog("%s", line);
}

/* After the copy, as in the dumper: the executables of the mounted folder are written in plain form over their copies in the dump.
   Never a reason to fail the dump: what cannot be decrypted stays as the console shows it, and the job says so. */
static int
run_decrypt(const dj_args_t *a, const char *src_root, const char *dst_root, const char *what) {
  if(!a->cfg.decrypt || cancelled()) return 1;
  if(!is_dir(src_root)) {
    dlog("Hinweis: Zum Entschlüsseln gibt es keinen eingehängten Ordner (%s).", src_root);
    return 1;
  }
  if(!ps5tm_dump_self_supported()) {
    dlog("Entschlüsseln wird für diese Firmware nicht unterstützt; der Dump bleibt, wie die Konsole ihn zeigt.");
    job_note("Entschlüsseln wird für diese Firmware nicht unterstützt. Der Dump bleibt gültig, aber verschlüsselt.");
    return 1;
  }
  unsigned n = 0;
  uint64_t largest = 0, sum = 0;
  if(ps5tm_dump_self_count(src_root, &g_cancel, &n, &largest, &sum) == 1) return 1;
  if(n == 0) {
    dlog("Zum Entschlüsseln gibt es in %s keine Dateien.", what);
    return 0;
  }
  uint64_t avail = 0;
  char fstype[16];
  if(drive_space(a->base, &avail, fstype, sizeof(fstype)) == 0) {
    uint64_t need = (a->cfg.fself ? sum + 2 * largest : a->cfg.backport ? sum + largest : largest) + (a->console ? DJ_RESERVE : 0) + (64ull << 20);
    if(avail < need) {
      dlog("Zu wenig Platz zum Entschlüsseln: gebraucht %.2f GB, frei %.2f GB.", (double)need / 1073741824.0, (double)avail / 1073741824.0);
      job_note("Zu wenig Platz zum Entschlüsseln. Der Dump bleibt gültig, aber verschlüsselt.");
      return 1;
    }
  }
  dec_cb_t cb;
  pthread_mutex_lock(&g_lock);
  cb.base = g_job.dec_done;
  g_job.dec_total += n;
  snprintf(g_job.stage, sizeof(g_job.stage), "Entschlüsseln");
  g_job.state = DJ_FINISHING;
  pthread_mutex_unlock(&g_lock);
  dlog("Entschlüssele %u Dateien aus %s (%s)%s", n, src_root, what, a->cfg.fself ? ", danach als FSELF" : "");
  dj_self_res_t *res = calloc(1, sizeof(*res));
  if(!res) {
    job_note("Kein Speicher zum Entschlüsseln. Der Dump bleibt gültig, aber verschlüsselt.");
    return 1;
  }
  int rc = ps5tm_dump_self_run(src_root, dst_root, DJ_PART, a->cfg.fself, a->cfg.backport, &g_cancel, self_cb, &cb, res);
  if(rc == 1) {
    dlog("Beim Entschlüsseln abgebrochen.");
  } else if(rc == DJ_SELF_UNSUPPORTED) {
    dlog("Entschlüsseln wird für diese Firmware nicht unterstützt.");
    job_note("Entschlüsseln wird für diese Firmware nicht unterstützt. Der Dump bleibt gültig, aber verschlüsselt.");
  } else if(rc != 0) {
    dlog("Entschlüsseln gescheitert: %s", res->why);
    job_note("Entschlüsseln gescheitert (%s). Der Dump bleibt gültig.", res->why);
  }
  if(res->critical) job_note("Wichtig: Der Kernel-Zeiger ließ sich nicht zurückstellen. Bitte die Konsole neu starten.");
  if(rc == 0 || rc == 1) {
    dlog("Entschlüsselt: %u Dateien (%llu MB)%s; keine SELF-Dateien: %u; nicht entschlüsselt: %u.", res->decrypted,
         (unsigned long long)(res->bytes >> 20), res->fself ? ", davon FSELF" : "", res->not_self, res->failed);
    if(res->failed) job_note("%u Dateien ließen sich nicht entschlüsseln und bleiben, wie die Konsole sie zeigt.", res->failed);
  }
  if(res->decrypted > 0) g_info_dec = 1;
  if(res->fself > 0) g_info_fself = 1;
  pthread_mutex_lock(&g_lock);
  g_job.dec_failed += res->failed;
  pthread_mutex_unlock(&g_lock);
  free(res);
  return rc == 0 ? 0 : 1;                        /* the backport only follows a decrypting that ran to its end */
}

/* ---------------------------------------------------------------- the SDK backport */

typedef struct { unsigned base; } bp_cb_t;

static void
bp_cb(void *ctx, const char *rel, unsigned done, unsigned total, const char *line) {
  const bp_cb_t *d = ctx;
  (void)total;
  if(rel) job_current(rel);
  pthread_mutex_lock(&g_lock);
  g_job.bp_done = d->base + done;
  pthread_mutex_unlock(&g_lock);
  if(line) dlog("%s", line);
}

/* After the decrypting, as in the dumper: the SDK version of the plain executables in the dump folder and of its parameter file
   is lowered to the chosen level. It only ever touches the dump folder, and a file that does not fit is a note in the log, never
   a reason to fail the dump. */
static void
run_backport(const dj_args_t *a, const char *dst_root, int ps4) {
  if(!a->cfg.backport || cancelled()) return;
  const int level = ps4 ? a->cfg.bp_ps4 : a->cfg.bp_ps5;
  dj_bp_res_t *res = calloc(1, sizeof(*res));
  if(!res) {
    job_note("Kein Speicher für den Backport. Der Dump bleibt gültig.");
    return;
  }
  bp_cb_t cb;
  pthread_mutex_lock(&g_lock);
  cb.base = g_job.bp_done;
  snprintf(g_job.stage, sizeof(g_job.stage), "Backport");
  g_job.state = DJ_FINISHING;
  pthread_mutex_unlock(&g_lock);
  if(ps4) {
    dlog("SDK-Backport: Ziel Stufe %d (PS4-SDK 0x%08X) in %s", level, ps5tm_dump_bp_ps4_target(level), dst_root);
  } else {
    uint32_t t5 = 0, t4 = 0;
    ps5tm_dump_bp_ps5_target(level, &t5, &t4);
    dlog("SDK-Backport: Ziel Stufe %d (PS5-SDK 0x%08X, PS4-SDK 0x%08X) in %s", level, t5, t4, dst_root);
  }
  int rc = ps5tm_dump_backport_run(dst_root, ps4, level, &g_cancel, bp_cb, &cb, res);
  pthread_mutex_lock(&g_lock);
  g_job.bp_total += res->total;
  g_job.bp_patched += res->patched + (unsigned)res->param_patched;
  pthread_mutex_unlock(&g_lock);
  if(rc == 1) {
    dlog("Beim Backport abgebrochen.");
  } else if(rc != 0) {
    dlog("Backport übersprungen: %s", res->why);
    job_note("Der SDK-Backport ließ sich nicht ausführen (%s). Der Dump bleibt gültig.", res->why);
  } else {
    dlog("Backport: %u Dateien heruntergepatcht, %u schon passend, %u als FSELF unverändert, %u Fehler%s.", res->patched, res->unchanged,
         res->signed_skip, res->failed, res->param_patched ? (ps4 ? ", param.sfo angepasst" : ", param.json angepasst") : "");
    if(res->signed_skip) job_note("%u Programmdateien sind FSELF-signiert und wurden vom Backport nicht angefasst.", res->signed_skip);
    if(res->failed) job_note("Beim Backport gab es %u Fehler. Der Dump bleibt gültig.", res->failed);
    g_info_bp = 1;
    g_info_bp4 = a->cfg.bp_ps4;
    g_info_bp5 = a->cfg.bp_ps5;
  }
  free(res);
}

/* ---------------------------------------------------------------- a PS4 title */

typedef struct {
  const char   *kind;                 /* "app" or "patch" */
  char          folder[DJ_NAME_MAX];  /* the dump folder's name */
  char          dpath[PATH_MAX];
  char          img[160], mnt[128];
  int           use_img;              /* 1: the console's PFS image of the title, 0: the folder it mounts */
  int           present;
  dj_pfs_stat_t pfs;
  tree_t        tree;
} ps4_part_t;

typedef struct {
  int        n;
  int        single;                  /* split 0: both go into one folder */
  ps4_part_t part[2];
} ps4_plan_t;

typedef struct {
  const cp_ctx_t *cx;
  const char     *folder;
} ps4_cb_t;

static void
ps4_progress(void *ctx, uint64_t delta, const char *rel) {
  (void)ctx;
  if(rel) job_current(rel);
  if(delta) job_work(delta, 0);
}

/* A file of the image is whole: read back and counted. */
static int
ps4_file_done(void *ctx, const char *dst, const char *rel, uint64_t size, uint32_t crc) {
  ps4_cb_t *c = ctx;
  int rc = 0;
  if(c->cx->verify) {
    char name[PATH_MAX + 256];
    snprintf(name, sizeof(name), "%s/%s", c->folder, rel);
    rc = verify_written(dst, size, crc, c->cx, name);
  }
  if(!rc) job_file_done();
  return rc;
}

/* The installed package of the title: app.pkg or patch.pkg on the console's drive, on an extended-storage drive or in PS4Mount's folder. */
static int
find_pkg4(int patch, const char *tid, char *out, size_t n) {
  static const char *const roots[] = { "/user", "/mnt/ext1/user", "/mnt/ext0/user", "/system_data/ps4mount", NULL };
  for(int i = 0; roots[i]; i++) {
    int k = patch ? snprintf(out, n, "%s/patch/%s/patch.pkg", roots[i], tid) : snprintf(out, n, "%s/app/%s/app.pkg", roots[i], tid);
    if(k > 0 && (size_t)k < n && pkg_file(out)) return 1;
  }
  out[0] = 0;
  return 0;
}

/* What of the title there is to dump and where it goes. 0, or -1 with job_fail said. */
static int
ps4_plan(const dj_args_t *a, const char *suffix, ps4_plan_t *pl, const cp_ctx_t *mcx, int split) {
  int want_app = split == 0 || (split & 1), want_patch = split == 0 || (split & 2);
  pl->single = split == 0;
  struct { const char *kind; const char *mnt_sfx; int want; } k[2] = { { "app", "app0", want_app }, { "patch", "patch0", want_patch } };
  for(int i = 0; i < 2; i++) {
    if(!k[i].want) continue;
    ps4_part_t *pt = &pl->part[pl->n];
    memset(pt, 0, sizeof(*pt));
    pt->kind = k[i].kind;
    snprintf(pt->img, sizeof(pt->img), DJ_SANDBOX "/%s-%s-nest/pfs_image.dat", a->title_id, k[i].mnt_sfx);
    snprintf(pt->mnt, sizeof(pt->mnt), DJ_SANDBOX "/%s-%s", a->title_id, k[i].mnt_sfx);
    if(pl->single) snprintf(pt->folder, sizeof(pt->folder), "%s%s", a->title_id, suffix);
    else snprintf(pt->folder, sizeof(pt->folder), "%s-%s%s", a->title_id, k[i].mnt_sfx, suffix);
    snprintf(pt->dpath, sizeof(pt->dpath), "%s/%s", a->dest, pt->folder);
    if(lis_reg(pt->img, NULL)) {
      pt->use_img = 1;
      pt->present = 1;
    } else if(is_dir(pt->mnt)) {
      pt->present = 1;
    }
    if(!pt->present) {
      if(i == 0) {
        job_fail("Zu diesem Spiel fehlt das Abbild der Spieldaten (%s) und der Einhängepunkt (%s). Läuft das Spiel wirklich?", pt->img, pt->mnt);
        return -1;
      }
      if(split == 2) {
        job_fail("Zu diesem Spiel gibt es keinen eingehängten Patch.");
        return -1;
      }
      dlog("Kein Patch zu %s eingehängt; es gibt nur den App-Teil.", a->title_id);
      continue;
    }
    if(pt->use_img) {
      char why[200] = "";
      int r = ps5tm_dump_pfs_measure(pt->img, &g_cancel, &pt->pfs, why, sizeof(why));
      if(r == 1) return 0;
      if(r != 0) {
        job_fail("Das Abbild der Spieldaten ließ sich nicht lesen: %s", why);
        return -1;
      }
      dlog("Abbild %s: %u Dateien, %.2f GB", pt->img, pt->pfs.files, (double)pt->pfs.bytes / 1073741824.0);
    } else {
      pt->tree = measure(pt->mnt, mcx);
      if(pt->tree.err) {
        job_fail("Die Spieldaten ließen sich nicht lesen: %s", errno_text(pt->tree.err));
        return -1;
      }
      dlog("Kein Abbild; der eingehängte Ordner %s wird kopiert: %u Dateien, %.2f GB", pt->mnt, pt->tree.files, (double)pt->tree.bytes / 1073741824.0);
    }
    pl->n++;
  }
  return 0;
}

/* The metadata files and the trophy file that the dumper adds to every part, and the package's own files. A missing one is a hint. */
static void
ps4_extras(const dj_args_t *a, const ps4_part_t *pt, char *buf, const cp_ctx_t *cx, int *rc, char *where, size_t wn) {
  char sce[PATH_MAX], src[PATH_MAX], dst[PATH_MAX];
  snprintf(sce, sizeof(sce), "%s/sce_sys", pt->dpath);
  mkdir(sce, 0777);
  static const char *const files[] = { "nptitle.dat", "npbind.dat" };
  for(int i = 0; i < 2 && !*rc && !cancelled(); i++) {
    snprintf(src, sizeof(src), "/system_data/priv/appmeta/%s/%s", a->title_id, files[i]);
    snprintf(dst, sizeof(dst), "%s/%s", sce, files[i]);
    struct stat st;
    if(lstat(dst, &st) == 0) continue;                  /* the image brought it */
    if(!lis_reg(src, NULL)) {
      dlog("Hinweis: %s nicht gefunden", src);
      continue;
    }
    char nm[PATH_MAX + 64];
    snprintf(nm, sizeof(nm), "%s/sce_sys/%s", pt->folder, files[i]);
    *rc = copy_extra(src, dst, buf, cx, nm, NULL);
    if(*rc) snprintf(where, wn, "%s", src);
  }
  if(*rc || cancelled()) return;

  char pkg[PATH_MAX];
  int patch = !strcmp(pt->kind, "patch");
  if(find_pkg4(patch, a->title_id, pkg, sizeof(pkg))) run_unpkg(pkg, sce, DJ_PART, patch ? "patch.pkg" : "app.pkg", NULL, 1);
  else dlog("Zu %s wurde kein %s-Paket gefunden; sce_sys kommt vom Abbild und aus den Metadaten.", a->title_id, pt->kind);
  if(cancelled()) return;

  char npwr[32];
  snprintf(src, sizeof(src), "%s/npbind.dat", sce);
  if(ps5tm_dump_npwr(src, npwr, sizeof(npwr)) == 0) {
    char tsrc[PATH_MAX], tdir[PATH_MAX], tdst[PATH_MAX];
    snprintf(tsrc, sizeof(tsrc), "/user/trophy/conf/%s/TROPHY.TRP", npwr);
    snprintf(tdir, sizeof(tdir), "%s/trophy", sce);
    snprintf(tdst, sizeof(tdst), "%s/trophy00.trp", tdir);
    struct stat st;
    if(!lis_reg(tsrc, NULL)) {
      dlog("Trophäendatei nicht gefunden: %s", tsrc);
    } else if(lstat(tdst, &st) != 0) {
      mkdir(tdir, 0777);
      char nm[PATH_MAX + 64];
      snprintf(nm, sizeof(nm), "%s/sce_sys/trophy/trophy00.trp", pt->folder);
      *rc = copy_extra(tsrc, tdst, buf, cx, nm, NULL);
      if(*rc) snprintf(where, wn, "%s", tsrc);
    }
  } else {
    dlog("Aus npbind.dat ließ sich keine NPWR-Kennung lesen.");
  }
}

/* param.sfo / param.json were held back under a temporary name: now they take their places. */
static int
ps4_finish_folder(const char *dpath) {
  static const char *const names[] = { "sce_sys/param.sfo", "sce_sys/param.json" };
  for(int i = 0; i < 2; i++) {
    char held[PATH_MAX], fin[PATH_MAX];
    if(snprintf(held, sizeof(held), "%s/%s" DJ_PART, dpath, names[i]) >= (int)sizeof(held)) continue;
    snprintf(fin, sizeof(fin), "%s/%s", dpath, names[i]);
    struct stat st;
    if(lstat(held, &st) != 0) continue;
    if(rename(held, fin) != 0) return errno ? errno : EIO;
  }
  return 0;
}

static void *
dump_run(void *arg) {
  dj_args_t a = *(dj_args_t *)arg;
  free(arg);
  ps5tm_powerguard_hold();
  log_open(a.title_id);
  dlog("Dump von %s (%s) nach %s, Version " PS5TM_VERSION, a.title_id, a.name, a.dest);
  dlog("Einstellungen: Spieltitel im Namen=%d, DLC=%d, Zurücklesen=%d, nur DLC=%d, ersetzen=%d, entschlüsseln=%d, FSELF=%d, Backport=%d (PS4 Stufe %d, PS5 Stufe %d)",
       a.cfg.folder_titles, a.cfg.dump_dlc, a.cfg.verify, a.dlc_only, a.overwrite, a.cfg.decrypt, a.cfg.fself, a.cfg.backport,
       a.cfg.bp_ps4, a.cfg.bp_ps5);

  const int is4 = is_cusa(a.title_id);
  g_info_dec = g_info_fself = g_info_bp = 0;
  ps4_plan_t *pl4 = NULL;
  char *buf = malloc(DJ_BUF);
  char *src = malloc(PATH_MAX), *dst = malloc(PATH_MAX);
  dlc_plan_t *dp = calloc(DJ_DLC_MAX, sizeof(*dp));
  if(!buf || !src || !dst || !dp) {
    job_fail("Kein Speicher für den Kopierpuffer.");
    goto out;
  }

  int rc = mkdir_p(a.dest);
  if(rc) {
    job_fail("Der Zielordner ließ sich nicht anlegen: %s", errno_text(rc));
    goto out;
  }

  char app0[96], u_meta[96], s_meta[128];
  snprintf(app0, sizeof(app0), DJ_SANDBOX "/%s-app0", a.title_id);
  snprintf(u_meta, sizeof(u_meta), "/user/appmeta/%s", a.title_id);
  snprintf(s_meta, sizeof(s_meta), "/system_data/priv/appmeta/%s", a.title_id);
  if(!a.dlc_only && !is_dir(app0)) {
    job_fail("Das Spiel ist nicht mehr eingehängt (%s). Es muss laufen, während es gedumpt wird.", app0);
    goto out;
  }

  /* the folder names */
  char suffix[64] = "", gfolder[DJ_NAME_MAX];
  if(a.cfg.folder_titles) title_suffix(a.name, suffix, sizeof(suffix));
  snprintf(gfolder, sizeof(gfolder), "%s-app0%s", a.title_id, suffix);

  /* an earlier dump of this title in the way */
  if(!a.dlc_only) {
    char old[4][DJ_NAME_MAX];
    int worst = 0, n = find_existing(a.dest, a.title_id, old, &worst);
    if(n && worst == 1 && !a.overwrite) {
      job_fail("Am Ziel liegt schon ein fertiger Dump dieses Spiels (%s).", old[0]);
      goto out;
    }
    for(int i = 0; i < n; i++) {
      job_state(DJ_MEASURING, "Alten Dump entfernen");
      dlog("Entferne den vorhandenen Dump %s/%s (%s)", a.dest, old[i], worst == 2 ? "unfertig" : "fertig, ersetzen");
      remove_dump_folder(a.dest, old[i]);
    }
  }

  /* what there is to copy */
  job_state(DJ_MEASURING, "Größe messen");
  cp_ctx_t app_cx = { NULL, gfolder, "", 0, a.cfg.verify, 0, k_defer_app, 2 };
  cp_ctx_t meta_cx = { NULL, gfolder, "sce_sys/", 0, a.cfg.verify, 1, k_defer_meta, 2 };
  cp_ctx_t dlc_cx = { NULL, "", "", 0, a.cfg.verify, 0, NULL, 0 };
  tree_t t_app, t_um, t_sm;
  memset(&t_app, 0, sizeof(t_app));
  memset(&t_um, 0, sizeof(t_um));
  memset(&t_sm, 0, sizeof(t_sm));
  tree_t all;
  memset(&all, 0, sizeof(all));
  if(!a.dlc_only && is4) {
    pl4 = calloc(1, sizeof(*pl4));
    if(!pl4) {
      job_fail("Kein Speicher.");
      goto out;
    }
    if(ps4_plan(&a, suffix, pl4, &dlc_cx, a.cfg.ps4_split) != 0) goto out;
    if(cancelled()) {
      job_state(DJ_CANCELLED, "cancelled");
      goto out;
    }
    for(int i = 0; i < pl4->n; i++) {
      const ps4_part_t *pt = &pl4->part[i];
      if(pt->use_img) {
        t_app.bytes += pt->pfs.bytes;
        t_app.files += pt->pfs.files;
        t_app.dirs += pt->pfs.dirs;
        if(pt->pfs.largest > t_app.largest) t_app.largest = pt->pfs.largest;
        if(pt->pfs.max_rel > t_app.max_rel) t_app.max_rel = pt->pfs.max_rel;
      } else {
        tree_add(&t_app, &pt->tree);
      }
    }
    tree_add(&all, &t_app);
    snprintf(gfolder, sizeof(gfolder), "%s", pl4->part[0].folder);
    if(strlen(a.dest) + 1 + strlen(gfolder) + 1 + t_app.max_rel >= PATH_MAX) {
      job_fail("Ein Dateipfad ist am Ziel zu lang: Er käme auf %zu Zeichen, erlaubt sind %d.",
               strlen(a.dest) + 1 + strlen(gfolder) + 1 + t_app.max_rel, PATH_MAX - 1);
      goto out;
    }
  } else if(!a.dlc_only) {
    t_app = measure(app0, &app_cx);
    if(t_app.err) {
      job_fail("Die Spieldaten ließen sich nicht lesen: %s", errno_text(t_app.err));
      goto out;
    }
    if(is_dir(u_meta)) t_um = measure(u_meta, &meta_cx);
    if(is_dir(s_meta)) t_sm = measure(s_meta, &meta_cx);
    if(t_um.err || t_sm.err) {
      dlog("Hinweis: die Metadaten ließen sich nicht ganz messen (%s)", errno_text(t_um.err ? t_um.err : t_sm.err));
      memset(t_um.err ? &t_um : &t_sm, 0, sizeof(tree_t));
    }
    tree_add(&all, &t_app);
    tree_add(&all, &t_um);
    tree_add(&all, &t_sm);
    if(strlen(a.dest) + 1 + strlen(gfolder) + 1 + t_app.max_rel >= PATH_MAX) {
      job_fail("Ein Dateipfad ist am Ziel zu lang: Er käme auf %zu Zeichen, erlaubt sind %d.",
               strlen(a.dest) + 1 + strlen(gfolder) + 1 + t_app.max_rel, PATH_MAX - 1);
      goto out;
    }
  }

  /* the DLC mounted with the title */
  char names[DJ_DLC_MAX][DJ_NAME_MAX];
  int ndlc = (a.cfg.dump_dlc || a.dlc_only) ? find_dlc(a.title_id, names) : 0;
  int ntodo = 0;
  for(int i = 0; i < ndlc && !cancelled(); i++) {
    char st[16];
    info_state(a.dest, names[i], st, sizeof(st));
    char dpath[PATH_MAX];
    snprintf(dpath, sizeof(dpath), "%s/%s", a.dest, names[i]);
    if(is_dir(dpath) && (!st[0] || !strcmp(st, "done"))) {
      dlog("DLC %s liegt schon als fertiger Dump da, sie bleibt, wie sie ist", names[i]);
      job_note("DLC %s war schon gedumpt.", names[i]);
      continue;
    }
    dlc_plan_t *p = &dp[ntodo];
    snprintf(p->folder, sizeof(p->folder), "%s", names[i]);
    snprintf(src, PATH_MAX, DJ_SANDBOX "/%s", names[i]);
    p->app = measure(src, NULL);
    if(p->app.err) {
      job_fail("Die DLC %s ließ sich nicht lesen: %s", names[i], errno_text(p->app.err));
      goto out;
    }
    const char *label = strstr(names[i], "_00-");
    label = label ? label + 4 : names[i];
    int ll = (int)strlen(label) - 3;
    if(ll > 0) {
      snprintf(p->meta_path, sizeof(p->meta_path), "/user/appmeta/addcont/%s/%.*s", a.title_id, ll, label);
      if(is_dir(p->meta_path)) p->meta = measure(p->meta_path, NULL);
      else p->meta_path[0] = 0;
    }
    tree_add(&all, &p->app);
    tree_add(&all, &p->meta);
    ntodo++;
  }
  if(a.dlc_only && !ntodo && !cancelled()) {
    if(ndlc) job_note("Alle DLC dieses Spiels sind schon gedumpt.");
    else {
      job_fail("Zu diesem Spiel sind keine DLC eingehängt. Sie erscheinen unter %s, solange das Spiel läuft.", DJ_SANDBOX);
      goto out;
    }
  }
  if(cancelled()) {
    job_state(DJ_CANCELLED, "cancelled");
    goto out;
  }

  /* room: found out now and not hours in */
  uint64_t avail = 0;
  char fstype[16];
  if(drive_space(a.base, &avail, fstype, sizeof(fstype)) != 0) {
    job_fail("Das Ziel %s ist nicht erreichbar.", a.base);
    goto out;
  }
  if(!strcmp(fstype, "msdosfs") && all.largest > DJ_FAT32_MAX) {
    job_fail("Das Ziel ist mit FAT32 formatiert und nimmt keine Datei über 4 GB auf. Dieses Spiel hat größere.");
    goto out;
  }
  uint64_t reserve = a.console ? DJ_RESERVE : 0;
  if(all.bytes + reserve > avail) {
    if(a.console)
      job_fail("Zu wenig Platz auf der Konsole: Der Dump braucht %.1f GB, und %d GB müssen frei bleiben; es sind nur %.1f GB frei.",
               (double)all.bytes / 1073741824.0, (int)(DJ_RESERVE >> 30), (double)avail / 1073741824.0);
    else
      job_fail("Zu wenig Platz: Der Dump braucht %.1f GB, auf dem Laufwerk sind %.1f GB frei.", (double)all.bytes / 1073741824.0,
               (double)avail / 1073741824.0);
    goto out;
  }
  dlog("Gemessen: %u Dateien, %.2f GB; frei: %.2f GB%s", all.files, (double)all.bytes / 1073741824.0, (double)avail / 1073741824.0,
       a.console ? " (10 GB bleiben frei)" : "");

  pthread_mutex_lock(&g_lock);
  g_job.total_bytes = all.bytes;
  g_job.files_total = all.files;
  g_job.dlc_total = (unsigned)ntodo;
  snprintf(g_job.kind, sizeof(g_job.kind), "%s", a.dlc_only ? "dlc" : "game");
  snprintf(g_job.folder, sizeof(g_job.folder), "%s", a.dlc_only && ntodo ? dp[0].folder : gfolder);
  g_job.state = DJ_COPYING;
  snprintf(g_job.stage, sizeof(g_job.stage), "Kopieren");
  pthread_mutex_unlock(&g_lock);

  char sums_path[PATH_MAX + 16], serr[300] = "";
  ps5tm_sums_t *sums = NULL;
  const int will_decrypt = a.cfg.decrypt && !a.dlc_only && ps5tm_dump_self_supported();
  if(will_decrypt && a.cfg.verify)
    job_note("Mit Entschlüsseln gibt es keine Prüfsummen-Datei, weil die Dateien nach dem Kopieren ersetzt werden. Das Zurücklesen prüft die Kopie.");
  if(a.cfg.verify && !a.dlc_only && !will_decrypt && snprintf(sums_path, sizeof(sums_path), "%s/%s" PS5TM_SUMS_EXT, a.dest, gfolder) < (int)sizeof(sums_path)) {
    sums = ps5tm_sums_open(sums_path, serr, sizeof(serr));
    if(!sums) job_note("Ohne Prüfsummen-Datei: %s", serr[0] ? serr : "der Pfad ist zu lang.");
  }
  char why_io[96] = "";
  int par = ps5tm_io_parallel(DJ_SANDBOX, a.dest, why_io, sizeof(why_io));
  app_cx.sums = sums; app_cx.parallel = par;
  meta_cx.sums = sums; meta_cx.parallel = par;
  dlc_cx.parallel = par;
  dlog("Lesen und Schreiben: %s", why_io);

  time_t t_start = time(NULL);
  char dgame[PATH_MAX];
  snprintf(dgame, sizeof(dgame), "%s/%s", a.dest, gfolder);
  uint64_t game_bytes = 0;
  char where[192] = "";
  int game_done = 0;
  char held[2][PATH_MAX];                   /* the files held back, under their temporary names */
  int have[2] = { 0, 0 };

  if(!a.dlc_only && is4) {
    const int nfold = pl4->single ? 1 : pl4->n;
    char ps4why[220] = "";
    for(int i = 0; i < nfold; i++) write_info(a.dest, pl4->part[i].folder, a.title_id, a.name, "game", "running", 0, t_start, 0, a.origin);
    rc = 0;
    for(int i = 0; i < pl4->n && !rc && !cancelled(); i++) {
      ps4_part_t *pt = &pl4->part[i];
      job_state(DJ_COPYING, pt->use_img ? "Abbild entpacken" : "Kopieren");
      if(mkdir(pt->dpath, 0777) != 0 && errno != EEXIST) {
        rc = errno ? errno : EIO;
        snprintf(where, sizeof(where), "%s", pt->dpath);
        break;
      }
      (void)chmod(pt->dpath, 0777);
      cp_ctx_t pcx = { sums, pt->folder, "", par, a.cfg.verify, (pl4->single && i > 0) ? 1 : 0, NULL, 0 };
      if(pt->use_img) {
        ps4_cb_t cb = { &pcx, pt->folder };
        int cbrc = 0;
        dlog("Entpacke %s -> %s", pt->img, pt->dpath);
        int r = ps5tm_dump_pfs_extract(pt->img, pt->dpath, DJ_PART, &g_cancel, ps4_progress, ps4_file_done, &cb, a.cfg.verify, &cbrc,
                                       ps4why, sizeof(ps4why));
        if(r == 1) {
          rc = ECANCELED;
        } else if(r == -2) {
          rc = cbrc ? cbrc : EIO;
          snprintf(where, sizeof(where), "%s", pt->img);
          ps4why[0] = 0;
        } else if(r != 0) {
          rc = EIO;
        }
      } else {
        dlog("Kopiere %s -> %s", pt->mnt, pt->dpath);
        rc = copy_folder(pt->mnt, pt->dpath, src, dst, buf, &pcx);
        if(rc) snprintf(where, sizeof(where), "%s", strlen(src) > strlen(pt->mnt) ? src + strlen(pt->mnt) + 1 : "");
      }
      if(!rc && !cancelled()) ps4_extras(&a, pt, buf, &pcx, &rc, where, sizeof(where));
      if(!rc && !cancelled() && a.cfg.decrypt && run_decrypt(&a, pt->mnt, pt->dpath, pt->kind) == 0) run_backport(&a, pt->dpath, 1);
    }
    if(!rc && cancelled()) rc = ECANCELED;
    if(!rc) {
      job_state(DJ_FINISHING, "Abschließen");
      for(int i = 0; i < nfold && !rc; i++) {
        int fr = ps4_finish_folder(pl4->part[i].dpath);
        if(fr) {
          rc = fr;
          snprintf(where, sizeof(where), "sce_sys/param.sfo");
        }
      }
    }
    pthread_mutex_lock(&g_lock);
    game_bytes = g_job.done_bytes;
    pthread_mutex_unlock(&g_lock);
    if(rc == ECANCELED) {
      for(int i = 0; i < nfold; i++) write_info(a.dest, pl4->part[i].folder, a.title_id, a.name, "game", "aborted", game_bytes, t_start, time(NULL), a.origin);
      job_state(DJ_CANCELLED, "cancelled");
      dlog("Abgebrochen. Was bis dahin geschrieben wurde, bleibt unfertig liegen.");
      goto out_sums;
    }
    if(rc) {
      if(ps4why[0]) job_fail("%s", ps4why);
      else fail_rc(rc, where);
      for(int i = 0; i < nfold; i++) write_info(a.dest, pl4->part[i].folder, a.title_id, a.name, "game", "failed", game_bytes, t_start, time(NULL), a.origin);
      goto out_sums;
    }
    for(int i = 0; i < nfold; i++) write_info(a.dest, pl4->part[i].folder, a.title_id, a.name, "game", "done", game_bytes, t_start, time(NULL), a.origin);
    game_done = 1;
    dlog("Spiel fertig: %.2f GB", (double)game_bytes / 1073741824.0);
  } else if(!a.dlc_only) {
    write_info(a.dest, gfolder, a.title_id, a.name, "game", "running", 0, t_start, 0, a.origin);
    if(mkdir(dgame, 0777) != 0 && errno != EEXIST) {
      rc = errno ? errno : EIO;
      job_fail("Der Dump-Ordner ließ sich nicht anlegen: %s", errno_text(rc));
      write_info(a.dest, gfolder, a.title_id, a.name, "game", "failed", 0, t_start, time(NULL), a.origin);
      goto out_sums;
    }
    (void)chmod(dgame, 0777);
    phase_t ph;
    phase_start(&ph, t_app.files + t_um.files + t_sm.files, t_app.bytes + t_um.bytes + t_sm.bytes);

    dlog("Kopiere %s -> %s", app0, dgame);
    rc = copy_folder(app0, dgame, src, dst, buf, &app_cx);
    if(rc) snprintf(where, sizeof(where), "%s", strlen(src) > strlen(app0) ? src + strlen(app0) + 1 : "");
    char sce[PATH_MAX];
    snprintf(sce, sizeof(sce), "%s/sce_sys", dgame);
    if(!rc) {
      dlog("Kopiere die Metadaten (%s, %s)", u_meta, s_meta);
      job_current("sce_sys");
      rc = copy_meta(u_meta, sce, src, dst, buf, &meta_cx);
      if(rc) snprintf(where, sizeof(where), "Metadaten (%s)", u_meta);
    }
    if(!rc) {
      rc = copy_meta(s_meta, sce, src, dst, buf, &meta_cx);
      if(rc) snprintf(where, sizeof(where), "Metadaten (%s)", s_meta);
    }

    /* trophy and UDS files, found through npbind.dat */
    if(!rc && !cancelled()) {
      char tr[PATH_MAX], ud[PATH_MAX];
      snprintf(tr, sizeof(tr), "%s/trophy2", sce);
      snprintf(ud, sizeof(ud), "%s/uds", sce);
      mkdir(tr, 0777);
      mkdir(ud, 0777);
      char np1[160], np2[160], npwr[32] = "";
      snprintf(np1, sizeof(np1), "/system_data/priv/appmeta/%s/trophy2/npbind.dat", a.title_id);
      snprintf(np2, sizeof(np2), "/system_data/priv/appmeta/%s/uds/npbind.dat", a.title_id);
      const char *np = lis_reg(np1, NULL) ? np1 : lis_reg(np2, NULL) ? np2 : NULL;
      if(np) {
        unsigned char raw[4096];
        FILE *f = fopen(np, "rb");
        size_t n = f ? fread(raw, 1, sizeof(raw), f) : 0;
        if(f) fclose(f);
        for(size_t i = 0; n >= 9 && i + 9 <= n; i++) {
          if(raw[i] == 'N' && raw[i + 1] == 'P' && raw[i + 2] == 'W' && raw[i + 3] == 'R') {
            size_t j = 0;
            for(; j < sizeof(npwr) - 1 && i + j < n && isprint(raw[i + j]); j++) npwr[j] = (char)raw[i + j];
            npwr[j] = 0;
            break;
          }
        }
      }
      if(npwr[0] && strspn(npwr, "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") == strlen(npwr)) {
        char s1[160], d1[PATH_MAX];
        snprintf(s1, sizeof(s1), "/user/trophy2/nobackup/conf/%s/TROPHY.UCP", npwr);
        snprintf(d1, sizeof(d1), "%s/trophy00.ucp", tr);
        if(lis_reg(s1, NULL)) {
          uint64_t eb = 0;
          rc = copy_extra(s1, d1, buf, &meta_cx, "sce_sys/trophy2/trophy00.ucp", &eb);
          ph.ftot++; ph.btot += eb;
          if(rc) snprintf(where, sizeof(where), "%s", s1);
        } else dlog("Trophäendatei nicht gefunden: %s", s1);
        snprintf(s1, sizeof(s1), "/user/np_uds/nobackup/conf/%s/uds.ucp", npwr);
        snprintf(d1, sizeof(d1), "%s/uds00.ucp", ud);
        if(!rc && lis_reg(s1, NULL)) {
          uint64_t eb = 0;
          rc = copy_extra(s1, d1, buf, &meta_cx, "sce_sys/uds/uds00.ucp", &eb);
          ph.ftot++; ph.btot += eb;
          if(rc) snprintf(where, sizeof(where), "%s", s1);
        } else if(!rc) dlog("UDS-Datei nicht gefunden: %s", s1);
      } else {
        dlog(np ? "Aus npbind.dat ließ sich keine NPWR-Kennung lesen." : "npbind.dat nicht gefunden.");
      }
    }

    /* param.json and param.sfo last, under a temporary name: the last source that has them wins (as in the dumper, where the
       metadata replaces what pfsmnt had) */
    if(!rc && !cancelled()) {
      job_state(DJ_FINISHING, "Abschließen");
      for(int i = 0; i < 2 && !rc; i++) {
        char cand[3][PATH_MAX];
        snprintf(cand[0], PATH_MAX, "%s/%s", s_meta, k_defer_meta[i]);
        snprintf(cand[1], PATH_MAX, "%s/%s", u_meta, k_defer_meta[i]);
        snprintf(cand[2], PATH_MAX, "%s/%s", app0, k_defer_app[i]);
        const char *use = NULL;
        for(int c = 0; c < 3 && !use; c++)
          if(lis_reg(cand[c], NULL)) use = cand[c];
        if(!use) continue;
        snprintf(held[i], PATH_MAX, "%s/%s" DJ_PART, dgame, k_defer_app[i]);
        snprintf(where, sizeof(where), "%s", k_defer_app[i]);
        char nm[PATH_MAX + 16];
        snprintf(nm, sizeof(nm), "%s/%s", gfolder, k_defer_app[i]);
        uint64_t eb = 0;
        rc = copy_extra(use, held[i], buf, &app_cx, nm, &eb);
        ph.ftot++; ph.btot += eb;
        if(!rc) have[i] = 1;
      }
    }

    /* what the package holds and the console did not mount: only added, and param.json / param.sfo held back like the others */
    if(!rc && !cancelled()) {
      char pkg[PATH_MAX];
      if(find_pkg(0, a.title_id, NULL, 0, pkg, sizeof(pkg))) {
        int from_pkg[2] = { 0, 0 };
        run_unpkg(pkg, sce, DJ_PART, "app.pkg", from_pkg, 0);
        for(int i = 0; i < 2; i++) {
          if(have[i] || !from_pkg[i]) continue;
          snprintf(held[i], PATH_MAX, "%s/%s" DJ_PART, dgame, k_defer_app[i]);
          have[i] = 1;
        }
      } else {
        dlog("Zu %s wurde kein app.pkg gefunden; sce_sys kommt nur von der Konsole und aus den Metadaten.", a.title_id);
      }
    }

    unsigned fd2 = 0;
    uint64_t bd2 = 0;
    if(!rc && cancelled()) rc = ECANCELED;
    if(!rc && !phase_complete(&ph, &fd2, &bd2)) rc = E_SOURCE_CHANGED;
    if(!rc) {
      for(int i = 0; i < 2 && !rc; i++) {
        if(!have[i]) continue;
        char fin[PATH_MAX];
        snprintf(fin, sizeof(fin), "%s/%s", dgame, k_defer_app[i]);
        snprintf(where, sizeof(where), "%s", k_defer_app[i]);
        if(rename(held[i], fin) != 0) rc = errno ? errno : EIO;
      }
    }
    for(int i = 0; i < 2; i++)
      if(have[i] && rc) unlink(held[i]);
    if(!rc && !cancelled() && a.cfg.decrypt && run_decrypt(&a, app0, dgame, "Spiel") == 0) run_backport(&a, dgame, 0);
    if(!rc && cancelled()) rc = ECANCELED;
    pthread_mutex_lock(&g_lock);
    game_bytes = g_job.done_bytes;
    pthread_mutex_unlock(&g_lock);

    if(rc == ECANCELED) {
      write_info(a.dest, gfolder, a.title_id, a.name, "game", "aborted", game_bytes, t_start, time(NULL), a.origin);
      job_state(DJ_CANCELLED, "cancelled");
      dlog("Abgebrochen. Was bis dahin geschrieben wurde, bleibt unfertig liegen.");
      goto out_sums;
    }
    if(rc == E_SOURCE_CHANGED) {
      job_fail("%s Gelesen wurden %u von %u Dateien und %llu von %llu MB.", errno_text(rc), fd2, ph.ftot,
               (unsigned long long)(bd2 >> 20), (unsigned long long)(ph.btot >> 20));
      write_info(a.dest, gfolder, a.title_id, a.name, "game", "failed", game_bytes, t_start, time(NULL), a.origin);
      goto out_sums;
    }
    if(rc) {
      fail_rc(rc, where);
      write_info(a.dest, gfolder, a.title_id, a.name, "game", "failed", game_bytes, t_start, time(NULL), a.origin);
      goto out_sums;
    }
    write_info(a.dest, gfolder, a.title_id, a.name, "game", "done", game_bytes, t_start, time(NULL), a.origin);
    game_done = 1;
    dlog("Spiel fertig: %.2f GB", (double)game_bytes / 1073741824.0);
  }

  /* the DLC: each as a folder of its own beside the game's */
  for(int k = 0; k < ntodo && !cancelled(); k++) {
    dlc_plan_t *p = &dp[k];
    job_state(DJ_COPYING, "DLC kopieren");
    char ddlc[PATH_MAX], sdlc[PATH_MAX];
    snprintf(ddlc, sizeof(ddlc), "%s/%s", a.dest, p->folder);
    snprintf(sdlc, sizeof(sdlc), DJ_SANDBOX "/%s", p->folder);
    job_current(p->folder);
    if(is_dir(ddlc)) {                     /* an unfinished earlier try (finished ones were left out above) */
      dlog("Entferne den unfertigen DLC-Dump %s", ddlc);
      remove_dump_folder(a.dest, p->folder);
    }
    write_info(a.dest, p->folder, a.title_id, p->folder, "dlc", "running", 0, time(NULL), 0, a.origin);
    if(mkdir(ddlc, 0777) != 0 && errno != EEXIST) {
      rc = errno ? errno : EIO;
      job_fail("Der DLC-Ordner ließ sich nicht anlegen: %s", errno_text(rc));
      write_info(a.dest, p->folder, a.title_id, p->folder, "dlc", "failed", 0, t_start, time(NULL), a.origin);
      goto out_sums;
    }
    (void)chmod(ddlc, 0777);
    phase_t ph;
    phase_start(&ph, p->app.files + p->meta.files, p->app.bytes + p->meta.bytes);
    dlc_cx.prefix = p->folder;
    dlog("Kopiere DLC %s -> %s", sdlc, ddlc);
    uint64_t b0 = 0;
    pthread_mutex_lock(&g_lock);
    b0 = g_job.done_bytes;
    pthread_mutex_unlock(&g_lock);
    rc = copy_folder(sdlc, ddlc, src, dst, buf, &dlc_cx);
    if(rc) snprintf(where, sizeof(where), "%s", p->folder);
    if(!rc && p->meta_path[0]) {
      char sce[PATH_MAX];
      snprintf(sce, sizeof(sce), "%s/sce_sys", ddlc);
      cp_ctx_t mcx = dlc_cx;
      mcx.replace = 1;
      rc = copy_meta(p->meta_path, sce, src, dst, buf, &mcx);
      if(rc) snprintf(where, sizeof(where), "Metadaten der DLC (%s)", p->meta_path);
    }
    if(!rc && !cancelled() && !is4) {
      const char *lab = strstr(p->folder, "_00-");
      lab = lab ? lab + 4 : p->folder;
      int ll = (int)strlen(lab) - 3;
      char pkg[PATH_MAX], sce[PATH_MAX];
      if(ll > 0 && find_pkg(1, a.title_id, lab, ll, pkg, sizeof(pkg)) && snprintf(sce, sizeof(sce), "%s/sce_sys", ddlc) < (int)sizeof(sce))
        run_unpkg(pkg, sce, NULL, "ac.pkg", NULL, 0);
      else
        dlog("Zur DLC %s wurde kein ac.pkg gefunden.", p->folder);
    }
    unsigned fd2 = 0;
    uint64_t bd2 = 0;
    if(!rc && cancelled()) rc = ECANCELED;
    if(!rc && !phase_complete(&ph, &fd2, &bd2)) rc = E_SOURCE_CHANGED;
    uint64_t now_b = 0;
    pthread_mutex_lock(&g_lock);
    now_b = g_job.done_bytes;
    pthread_mutex_unlock(&g_lock);
    if(rc == ECANCELED) {
      write_info(a.dest, p->folder, a.title_id, p->folder, "dlc", "aborted", now_b - b0, t_start, time(NULL), a.origin);
      job_state(DJ_CANCELLED, "cancelled");
      dlog("Abgebrochen bei der DLC %s", p->folder);
      goto out_sums;
    }
    if(rc) {
      if(rc == E_SOURCE_CHANGED) job_fail("DLC %s: %s", p->folder, errno_text(rc));
      else fail_rc(rc, where);
      write_info(a.dest, p->folder, a.title_id, p->folder, "dlc", "failed", now_b - b0, t_start, time(NULL), a.origin);
      goto out_sums;
    }
    write_info(a.dest, p->folder, a.title_id, p->folder, "dlc", "done", now_b - b0, t_start, time(NULL), a.origin);
    pthread_mutex_lock(&g_lock);
    g_job.dlc_done++;
    pthread_mutex_unlock(&g_lock);
    dlog("DLC fertig: %s", p->folder);
  }
  if(cancelled()) {
    job_state(DJ_CANCELLED, "cancelled");
    dlog("Abgebrochen.");
    goto out_sums;
  }

  /* the checksum file gets its name only for a dump that is whole */
  if(sums) {
    if(ps5tm_sums_close(sums, serr, sizeof(serr)) != 0)
      job_note("Die Prüfsummen-Datei ließ sich nicht schreiben. Der Dump selbst ist zurückgelesen und in Ordnung.");
    sums = NULL;
  }
  job_state(DJ_DONE, "done");
  {
    pthread_mutex_lock(&g_lock);
    uint64_t done = g_job.done_bytes, secs = (ps5tm_mono_ms() - g_job.started_ms) / 1000;
    pthread_mutex_unlock(&g_lock);
    dlog("Fertig: %.2f GB in %llu s", (double)done / 1073741824.0, (unsigned long long)secs);
    PS5TM_INFO("dump_done", "Dump von %s nach %s fertig: %llu MB in %llu s%s.", a.title_id, a.dest, (unsigned long long)(done >> 20),
               (unsigned long long)secs, game_done ? "" : " (nur DLC)");
    ps5tm_notify("Dump fertig: %s", a.name);
  }

out_sums:
  if(sums) ps5tm_sums_abort(sums);
out:
  free(buf);
  free(src);
  free(dst);
  free(dp);
  free(pl4);
  pthread_mutex_lock(&g_lock);
  int st = g_job.state;
  pthread_mutex_unlock(&g_lock);
  if(st < DJ_DONE) {                       /* a path above left without saying how it ended */
    job_state(DJ_FAILED, "failed");
  }
  if(st == DJ_CANCELLED) {
    PS5TM_INFO("dump_cancelled", "Dump von %s abgebrochen; was geschrieben wurde, bleibt unfertig im Ziel.", a.title_id);
  }
  hist_note();
  log_close();
  ps5tm_powerguard_release();
  ps5tm_library_forget();
  ps5tm_dumplib_start(0);                  /* the new dump counts in the list and as GEDUMPT */
  return NULL;
}

/* ---------------------------------------------------------------- interface */

int
ps5tm_dump_busy(void) {
  pthread_mutex_lock(&g_lock);
  int busy = g_job.state >= DJ_MEASURING && g_job.state <= DJ_FINISHING;
  pthread_mutex_unlock(&g_lock);
  return busy;
}

static int
dump_start(const char *tid, const char *target_in, int overwrite, int dlc_only, const ps5tm_dumpopt_t *opt,
           char *err, size_t en) {
  if(!is_tid(tid)) {
    snprintf(err, en, "Das ist keine Titelkennung eines PS5- oder PS4-Spiels.");
    return 400;
  }
  dj_cfg_t cfg = cfg_get();
  if(opt && opt->set) {                          /* a queued title with settings of its own; the rest stays the global settings */
    cfg.decrypt = opt->decrypt != 0;
    cfg.fself = cfg.decrypt && opt->fself;
    cfg.backport = cfg.decrypt && opt->backport;
    cfg.bp_ps4 = clamp_level(opt->bp_ps4, DJ_BP_PS4_LEVELS);
    cfg.bp_ps5 = clamp_level(opt->bp_ps5, DJ_BP_PS5_LEVELS);
    cfg.ps4_split = opt->ps4_split < 0 ? 0 : opt->ps4_split > 3 ? 3 : opt->ps4_split;
  }
  const char *mount = target_in && target_in[0] ? target_in : cfg.target;
  if(!mount[0]) {
    snprintf(err, en, "Bitte zuerst ein Ziel wählen.");
    return 400;
  }
  if(!subdir_ok(cfg.subdir)) {
    snprintf(err, en, "Der Ordner für die Dumps ist ungültig.");
    return 400;
  }
  dj_args_t *a = calloc(1, sizeof(*a));
  if(!a) { snprintf(err, en, "Kein Speicher."); return 500; }
  if(drive_base(mount, a->base, sizeof(a->base), a->label, sizeof(a->label)) != 0) {
    free(a);
    snprintf(err, en, "Dieses Ziel gibt es gerade nicht. Ist der Datenträger angeschlossen?");
    return 400;
  }
  if(snprintf(a->dest, sizeof(a->dest), "%s/%s", a->base, cfg.subdir) >= (int)sizeof(a->dest)) {
    free(a);
    snprintf(err, en, "Der Zielpfad ist zu lang.");
    return 400;
  }
  a->console = !strcmp(a->base, "/data");
  if(!ps5tm_dump_mounted(tid) && !dlc_only) {
    ps5tm_libdel_t li;
    int smp = ps5tm_library_delete_info(tid, &li) == 0 && li.smp && strcmp(li.format, "pkg");
    free(a);
    if(smp)
      snprintf(err, en, "Dieses Spiel läuft über ShadowMountPlus aus einem Ordner oder Abbild – es gibt keinen Einhängepunkt zum Dumpen. Mit „Kopieren“ lässt es sich sichern.");
    else
      snprintf(err, en, "Das Spiel muss laufen. Bitte zuerst auf „Starten“ drücken und warten, bis es geladen ist.");
    return 409;
  }
  char dl[DJ_DLC_MAX][DJ_NAME_MAX];
  if(dlc_only && find_dlc(tid, dl) == 0) {
    free(a);
    snprintf(err, en, "Zu diesem Spiel sind keine DLC eingehängt. Das Spiel muss laufen, damit sie erscheinen.");
    return 409;
  }
  if(!dlc_only && !overwrite) {
    char old[4][DJ_NAME_MAX];
    int worst = 0;
    if(find_existing(a->dest, tid, old, &worst) && worst == 1) {
      free(a);
      snprintf(err, en, "Am Ziel liegt schon ein fertiger Dump dieses Spiels. Er wird nur nach einer Bestätigung ersetzt.");
      return 409;
    }
  }

  snprintf(a->title_id, sizeof(a->title_id), "%s", tid);
  snprintf(a->origin, sizeof(a->origin), "disc-tab");
  ps5tm_gamestate_t gs;
  ps5tm_gamestate_get(&gs);
  char link[16], nm[128] = "";
  if(gs.title_id[0] && !strcmp(gs.title_id, tid) && gs.title_name[0]) snprintf(nm, sizeof(nm), "%s", gs.title_name);
  else (void)ps5tm_library_launch_info(tid, nm, sizeof(nm), link, sizeof(link));
  snprintf(a->name, sizeof(a->name), "%s", nm[0] ? nm : tid);
  a->overwrite = overwrite;
  a->dlc_only = dlc_only;
  a->cfg = cfg;

  if(ps5tm_filejobs_busy_for_dump(err, en)) { free(a); return 409; }

  pthread_mutex_lock(&g_lock);
  if(g_job.state >= DJ_MEASURING && g_job.state <= DJ_FINISHING) {
    pthread_mutex_unlock(&g_lock);
    free(a);
    snprintf(err, en, "Es läuft schon ein Dump.");
    return 409;
  }
  memset(&g_job, 0, sizeof(g_job));
  g_job.state = DJ_MEASURING;
  snprintf(g_job.stage, sizeof(g_job.stage), "Vorbereiten");
  snprintf(g_job.title_id, sizeof(g_job.title_id), "%s", tid);
  snprintf(g_job.name, sizeof(g_job.name), "%s", a->name);
  snprintf(g_job.kind, sizeof(g_job.kind), "%s", dlc_only ? "dlc" : "game");
  snprintf(g_job.dest, sizeof(g_job.dest), "%s", a->dest);
  snprintf(g_job.drive, sizeof(g_job.drive), "%s", a->label);
  g_job.verify = cfg.verify;
  g_job.started_ms = ps5tm_mono_ms();
  __atomic_store_n(&g_cancel, 0, __ATOMIC_RELEASE);
  pthread_mutex_unlock(&g_lock);

  /* a file job that claimed its place in the same instant: both stand down, the person starts again */
  if(ps5tm_filejobs_busy_for_dump(err, en)) {
    pthread_mutex_lock(&g_lock);
    memset(&g_job, 0, sizeof(g_job));
    pthread_mutex_unlock(&g_lock);
    free(a);
    return 409;
  }

  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
  pthread_attr_setstacksize(&attr, 1024 * 1024);
  pthread_t th;
  int rc = pthread_create(&th, &attr, dump_run, a);
  pthread_attr_destroy(&attr);
  if(rc != 0) {
    free(a);
    job_fail("Der Dump ließ sich nicht starten.");
    snprintf(err, en, "Der Dump ließ sich nicht starten.");
    return 500;
  }
  PS5TM_INFO("dump_started", "Dump von %s gestartet (nach %s%s).", tid, mount, dlc_only ? ", nur DLC" : "");
  snprintf(err, en, dlc_only ? "Der DLC-Dump läuft." : "Der Dump läuft.");
  return 200;
}

/* ---------------------------------------------------------------- what the queue (dump_queue.c) needs */

int
ps5tm_dump_start_item(const char *tid, int overwrite, const ps5tm_dumpopt_t *opt, char *msg, size_t n) {
  return dump_start(tid, "", overwrite, 0, opt, msg, n);
}

void
ps5tm_dump_abort(void) {
  __atomic_store_n(&g_cancel, 1, __ATOMIC_RELEASE);
}

/* How the last dump ended: its state name and a sentence (the error, else the note). 1 when there is one that has ended. */
int
ps5tm_dump_last(char *state, size_t sn, char *msg, size_t mn) {
  pthread_mutex_lock(&g_lock);
  dj_job_t j = g_job;
  pthread_mutex_unlock(&g_lock);
  snprintf(state, sn, "%s", k_state[j.state]);
  snprintf(msg, mn, "%s", j.error[0] ? j.error : j.note);
  return j.state >= DJ_DONE;
}

/* A dump of this title at the chosen drive: 0 none, 1 a finished one, 2 only one that was cut short. */
int
ps5tm_dump_present(const char *tid) {
  dj_cfg_t cfg = cfg_get();
  char base[64], label[40], dest[320];
  if(!is_tid(tid) || !cfg.target[0] || !subdir_ok(cfg.subdir) || drive_base(cfg.target, base, sizeof(base), label, sizeof(label)) != 0) return 0;
  if(snprintf(dest, sizeof(dest), "%s/%s", base, cfg.subdir) >= (int)sizeof(dest)) return 0;
  char old[4][DJ_NAME_MAX];
  int worst = 0;
  if(!find_existing(dest, tid, old, &worst)) return 0;
  return worst == 2 ? 2 : 1;
}

/* The global settings, so the queue can tell which title carries settings of its own; and the chosen target. */
void
ps5tm_dump_settings(ps5tm_dumpopt_t *o, char *target, size_t tn) {
  dj_cfg_t cfg = cfg_get();
  o->set = 1;
  o->decrypt = cfg.decrypt;
  o->fself = cfg.fself;
  o->backport = cfg.backport;
  o->bp_ps4 = cfg.bp_ps4;
  o->bp_ps5 = cfg.bp_ps5;
  o->ps4_split = cfg.ps4_split;
  if(target) snprintf(target, tn, "%s", cfg.target);
}

int
ps5tm_dump_action(const cJSON *b, char *msg, size_t n) {
  const cJSON *ja = cJSON_GetObjectItem(b, "action");
  const char *act = cJSON_IsString(ja) ? ja->valuestring : "";
  const cJSON *jid = cJSON_GetObjectItem(b, "id");
  const char *id = cJSON_IsString(jid) ? jid->valuestring : "";
  const cJSON *jt = cJSON_GetObjectItem(b, "target");
  const char *target = cJSON_IsString(jt) ? jt->valuestring : "";
  const cJSON *jo = cJSON_GetObjectItem(b, "overwrite");

  if(!strncmp(act, "queue_", 6)) return ps5tm_dumpq_action(b, msg, n);
  if(!strcmp(act, "start")) return dump_start(id, target, cJSON_IsTrue(jo), 0, NULL, msg, n);
  if(!strcmp(act, "dlc_only")) return dump_start(id, target, 0, 1, NULL, msg, n);
  if(!strcmp(act, "cancel")) {
    if(!ps5tm_dump_busy()) { snprintf(msg, n, "Es läuft kein Dump."); return 409; }
    __atomic_store_n(&g_cancel, 1, __ATOMIC_RELEASE);
    snprintf(msg, n, "Der Dump wird gestoppt. Was bis dahin geschrieben wurde, bleibt unfertig im Ziel.");
    return 200;
  }
  if(!strcmp(act, "settings")) {
    dj_cfg_t c = cfg_get();
    const cJSON *v;
    if(cJSON_IsString(v = cJSON_GetObjectItem(b, "target"))) {
      if(v->valuestring[0]) {
        char base[64], label[40];
        if(strlen(v->valuestring) >= sizeof(c.target) || drive_base(v->valuestring, base, sizeof(base), label, sizeof(label)) != 0) {
          snprintf(msg, n, "Dieses Ziel gibt es gerade nicht.");
          return 400;
        }
      }
      snprintf(c.target, sizeof(c.target), "%s", v->valuestring);
    }
    if(cJSON_IsString(v = cJSON_GetObjectItem(b, "subdir"))) {
      if(!subdir_ok(v->valuestring)) {
        snprintf(msg, n, "Der Ordner darf nur aus Buchstaben, Ziffern, Punkt, Strich, Unterstrich und Leerzeichen bestehen, höchstens drei Ebenen tief.");
        return 400;
      }
      snprintf(c.subdir, sizeof(c.subdir), "%s", v->valuestring);
    }
    if(cJSON_IsBool(v = cJSON_GetObjectItem(b, "folder_titles"))) c.folder_titles = cJSON_IsTrue(v);
    if(cJSON_IsBool(v = cJSON_GetObjectItem(b, "dump_dlc"))) c.dump_dlc = cJSON_IsTrue(v);
    if(cJSON_IsBool(v = cJSON_GetObjectItem(b, "verify"))) c.verify = cJSON_IsTrue(v);
    if(cJSON_IsNumber(v = cJSON_GetObjectItem(b, "ps4_split"))) {
      if(v->valuedouble < 0 || v->valuedouble > 3) {
        snprintf(msg, n, "Die PS4-Aufteilung ist ungültig.");
        return 400;
      }
      c.ps4_split = (int)v->valuedouble;
    }
    if(cJSON_IsBool(v = cJSON_GetObjectItem(b, "decrypt"))) c.decrypt = cJSON_IsTrue(v);
    if(cJSON_IsBool(v = cJSON_GetObjectItem(b, "fself"))) c.fself = cJSON_IsTrue(v);
    if(cJSON_IsBool(v = cJSON_GetObjectItem(b, "backport"))) c.backport = cJSON_IsTrue(v);
    if(cJSON_IsNumber(v = cJSON_GetObjectItem(b, "ps4_backport_level"))) c.bp_ps4 = clamp_level((int)v->valuedouble, DJ_BP_PS4_LEVELS);
    if(cJSON_IsNumber(v = cJSON_GetObjectItem(b, "ps5_backport_level"))) c.bp_ps5 = clamp_level((int)v->valuedouble, DJ_BP_PS5_LEVELS);
    if(!c.decrypt) c.fself = c.backport = 0;
    pthread_mutex_lock(&g_lock);
    cfg_load_locked();
    g_cfg = c;
    pthread_mutex_unlock(&g_lock);
    cfg_save(&c);
    snprintf(msg, n, "Gespeichert.");
    return 200;
  }
  snprintf(msg, n, "Diese Aktion kennt die App nicht.");
  return 400;
}

/* Deletes one dump of the list in the Disc tab: its folder, info file and checksum file. The page names it by the three
   fields of its list entry; what is not in the list is never touched. */
int
ps5tm_dump_delete(const char *mount, const char *dir, const char *folder, char *msg, size_t n) {
  if(ps5tm_dump_busy()) { snprintf(msg, n, "Es läuft gerade ein Dump."); return 409; }
  if(ps5tm_filejobs_busy_for_dump(msg, n)) return 409;
  char dest[PATH_MAX], state[16];
  int http = ps5tm_dumplib_locate(mount, dir, folder, dest, sizeof(dest), state, sizeof(state), msg, n);
  if(http != 200) return http;
  if(!strcmp(state, "running")) { snprintf(msg, n, "Dieser Dump wird gerade geschrieben."); return 409; }
  uint64_t t0 = ps5tm_mono_ms();
  remove_dump_folder(dest, folder);
  PS5TM_INFO("dump_deleted", "Dump gelöscht: %s.", folder);
  ps5tm_hist_add("delete", folder, "Dump", "done", "", 0, ps5tm_mono_ms() - t0);
  (void)ps5tm_dumplib_start(0);
  snprintf(msg, n, "Der Dump ist gelöscht.");
  return 200;
}

cJSON *
ps5tm_dump_json(const char *title_id) {
  dj_cfg_t cfg = cfg_get();
  cJSON *o = cJSON_CreateObject();
  cJSON_AddBoolToObject(o, "ok", 1);
  cJSON *s = cJSON_AddObjectToObject(o, "settings");
  cJSON_AddStringToObject(s, "target", cfg.target);
  cJSON_AddStringToObject(s, "subdir", cfg.subdir);
  cJSON_AddBoolToObject(s, "folder_titles", cfg.folder_titles);
  cJSON_AddBoolToObject(s, "dump_dlc", cfg.dump_dlc);
  cJSON_AddBoolToObject(s, "verify", cfg.verify);
  cJSON_AddNumberToObject(s, "ps4_split", cfg.ps4_split);
  cJSON_AddBoolToObject(s, "decrypt", cfg.decrypt);
  cJSON_AddBoolToObject(s, "fself", cfg.fself);
  cJSON_AddBoolToObject(s, "backport", cfg.backport);
  cJSON_AddNumberToObject(s, "ps4_backport_level", cfg.bp_ps4);
  cJSON_AddNumberToObject(s, "ps5_backport_level", cfg.bp_ps5);
  cJSON_AddBoolToObject(s, "decrypt_supported", ps5tm_dump_self_supported());

  ps5tm_sysinfo_t *info = malloc(sizeof(*info));
  cJSON *arr = cJSON_AddArrayToObject(o, "targets");
  if(info) {
    ps5tm_sysinfo_get(info);
    for(unsigned i = 0; i < info->volume_count; i++) {
      const char *mount = info->volumes[i].path;
      char base[64], label[40];
      if(drive_base(mount, base, sizeof(base), label, sizeof(label)) != 0) continue;
      uint64_t avail = 0;
      char fstype[16];
      if(drive_space(base, &avail, fstype, sizeof(fstype)) != 0) continue;
      cJSON *d = cJSON_CreateObject();
      cJSON_AddStringToObject(d, "mount", mount);
      cJSON_AddStringToObject(d, "label", label);
      cJSON_AddStringToObject(d, "fs", fstype);
      cJSON_AddNumberToObject(d, "free_bytes", (double)avail);
      cJSON_AddBoolToObject(d, "console", !strcmp(base, "/data"));
      cJSON_AddItemToArray(arr, d);
    }
    free(info);
  }

  if(title_id && is_tid(title_id)) {
    cJSON *t = cJSON_AddObjectToObject(o, "title");
    cJSON_AddStringToObject(t, "title_id", title_id);
    cJSON_AddBoolToObject(t, "mounted", ps5tm_dump_mounted(title_id));
    char dl[DJ_DLC_MAX][DJ_NAME_MAX];
    int nd = find_dlc(title_id, dl);
    cJSON *da = cJSON_AddArrayToObject(t, "dlc");
    for(int i = 0; i < nd; i++) cJSON_AddItemToArray(da, cJSON_CreateString(dl[i]));
    const char *exists = "";
    char base[64], label[40];
    if(cfg.target[0] && subdir_ok(cfg.subdir) && drive_base(cfg.target, base, sizeof(base), label, sizeof(label)) == 0) {
      char dest[320];
      snprintf(dest, sizeof(dest), "%s/%s", base, cfg.subdir);
      char old[4][DJ_NAME_MAX];
      int worst = 0;
      if(find_existing(dest, title_id, old, &worst)) exists = worst == 2 ? "unfinished" : "done";
    }
    cJSON_AddStringToObject(t, "exists", exists);
  }

  cJSON_AddItemToObject(o, "queue", ps5tm_dumpq_json());
  pthread_mutex_lock(&g_lock);
  dj_job_t j = g_job;
  pthread_mutex_unlock(&g_lock);
  cJSON *jo = cJSON_AddObjectToObject(o, "job");
  cJSON_AddStringToObject(jo, "state", k_state[j.state]);
  cJSON_AddBoolToObject(jo, "active", j.state >= DJ_MEASURING && j.state <= DJ_FINISHING);
  if(j.state != DJ_IDLE) {
    cJSON_AddStringToObject(jo, "title_id", j.title_id);
    cJSON_AddNumberToObject(jo, "started", (double)j.started_ms);
    cJSON_AddStringToObject(jo, "name", j.name);
    cJSON_AddStringToObject(jo, "kind", j.kind);
    cJSON_AddStringToObject(jo, "dest", j.dest);
    cJSON_AddStringToObject(jo, "folder", j.folder);
    cJSON_AddStringToObject(jo, "drive", j.drive);
    cJSON_AddStringToObject(jo, "stage", j.stage);
    cJSON_AddNumberToObject(jo, "total_bytes", (double)j.total_bytes);
    cJSON_AddNumberToObject(jo, "done_bytes", (double)j.done_bytes);
    cJSON_AddNumberToObject(jo, "checked_bytes", (double)j.checked_bytes);
    cJSON_AddBoolToObject(jo, "verify", j.verify);
    cJSON_AddNumberToObject(jo, "files_total", j.files_total);
    cJSON_AddNumberToObject(jo, "files_done", j.files_done);
    cJSON_AddNumberToObject(jo, "dlc_total", j.dlc_total);
    cJSON_AddNumberToObject(jo, "dlc_done", j.dlc_done);
    cJSON_AddNumberToObject(jo, "dec_total", j.dec_total);
    cJSON_AddNumberToObject(jo, "dec_done", j.dec_done);
    cJSON_AddNumberToObject(jo, "dec_failed", j.dec_failed);
    cJSON_AddNumberToObject(jo, "bp_total", j.bp_total);
    cJSON_AddNumberToObject(jo, "bp_done", j.bp_done);
    cJSON_AddNumberToObject(jo, "bp_patched", j.bp_patched);
    cJSON_AddStringToObject(jo, "current", j.current);
    cJSON_AddNumberToObject(jo, "rate", j.rate);
    uint64_t end = j.finished_ms ? j.finished_ms : ps5tm_mono_ms();
    cJSON_AddNumberToObject(jo, "elapsed_s", (double)(end > j.started_ms ? (end - j.started_ms) / 1000 : 0));
    double work_total = (double)j.total_bytes * (j.verify ? 2.0 : 1.0);
    double work = (double)(j.done_bytes + j.checked_bytes);
    cJSON_AddNumberToObject(jo, "eta_s", j.rate > 1 && work_total > work && j.state == DJ_COPYING ? (work_total - work) / j.rate : -1);
    if(j.error[0]) cJSON_AddStringToObject(jo, "error", j.error);
    if(j.note[0]) cJSON_AddStringToObject(jo, "note", j.note);
    if(j.log[0]) cJSON_AddStringToObject(jo, "log", j.log);
  }
  return o;
}
