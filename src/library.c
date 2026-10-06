/* The games on the home screen, the way the home screen itself lists them.
 *
 * The shell draws its tiles from the system's app database,
 * /system_data/priv/mms/app.db, read here with sqlite_ro.c. Two tables carry
 * everything the games page shows (measured 28.09.2026, FW 12.00, PS5 Pro):
 *
 *   tbl_iconinfo_<user>  one row per tile of that user — visible or not, the
 *                        row it sits in (dispLocation), last played, play
 *                        time in seconds, lastAccessIndex (higher = more
 *                        recent, the home screen's order). <user> is the user
 *                        id in decimal, ten digits: 0x12345678 = 0305419896.
 *   tbl_contentinfo      one row per installed title — content id, the name
 *                        as the home screen shows it (already localised),
 *                        size, install time, the cover's path (icon0Info),
 *                        platform, and AppInfoJson with what param.json or
 *                        param.sfo say: version, required system software.
 *
 * dispLocation 138 is the games row. On the test console 28 visible tiles sat
 * there: PS5 and PS4 games, and two homebrew titles filed under games. 188 is
 * the media row (YouTube, this app's own tile, most homebrew), 146/148/154
 * are the shell's own tiles (Store, game library, media gallery).
 *
 * Titles mounted from another drive (ShadowMount and the like) leave a
 * mount.lnk in /user/app/<id>/ naming where their data really lives; the
 * size the database gives for them is only the local part.
 *
 * Times in the database are UTC ("2026-09-27 23:04:23.714"): a session that
 * ended at 03:22 local (CEST) is stored as 01:22. They go out as ISO 8601 with
 * a Z, and the browser shows local time.
 *
 * For a PS5 title mounted from a folder, the folder itself says what was done
 * to the dump (measured 28.09.2026 on three of them):
 *
 *   fakelib/libSceAmpr.sprx    AMPR emulation; ampr_emu.index ("AMPRIDX3")
 *                              appears beside the game once it has run
 *   fakelib/libScePlayGo.sprx  PlayGo emulation; writes playlgo.log
 *   fakelib/<other>.sprx       system libraries carried for a lower firmware
 *   eboot.bin                  a backport patches the SDK version in its
 *                              process parameters down: Arkanoid's param.json
 *                              says SDK 5.00, its eboot.bin 4.00. Fishing
 *                              (4.00/4.00) and AC Shadows (10.00/10.00) are
 *                              untouched.
 *
 * A title ShadowMount keeps in a disc image is only readable while the image
 * is mounted under /mnt/shadowmnt; until then the answer is "unknown".
 *
 * With "Covers & Metadaten speichern" on (config.library_cache) the slow parts of all
 * this survive a restart: libcache.c keeps each title's folder size and backport check
 * in /data/PS5-Cooling-Center/covers_and_more/<title>/meta.json and a copy of its cover
 * as icon0.png. cache_seed() puts the metas back into the two in-memory caches below
 * before the first scan (where they are judged by the same keys as anything else in them),
 * cache_save() writes back what changed after each scan, and the cover route serves the
 * copy. A title that appears in the list is stored by the scan that finds it. */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "ps5tm.h"
#include "sqlite_ro.h"
#include "third_party/cJSON.h"

#define APPDB_PATH       "/system_data/priv/mms/app.db"
#define APPDB_JOURNAL    APPDB_PATH "-journal"
#define APPDB_MAX_BYTES  (32u * 1024 * 1024)
#define LIBRARY_TTL_MS   15000
#define SIZE_TTL_MS      (10u * 60 * 1000)   /* folder sizes: see real_size() */
#define SIZE_SEED_TTL_MS (6u * 60 * 60 * 1000) /* ... one read from the cache folder counts this long */
#define LIBRARY_MAX      256
#define ROW_GAMES        138
#define MOD_LIB_NAMES    6

enum { MODS_NONE = 0, MODS_CHECKED, MODS_UNREACHABLE };

typedef struct {
  int      state;                /* MODS_*                                  */
  int      backport;
  int      ampr;                 /* fakelib/libSceAmpr.sprx                 */
  int      playgo;               /* fakelib/libScePlayGo.sprx               */
  int      ampr_index;           /* ampr_emu.index: the emulation has run   */
  int      playgo_log;           /* playlgo.log: likewise                   */
  int      extra_libs;           /* other libraries in fakelib              */
  uint32_t eboot_sdk;            /* from eboot.bin, 0 = not readable        */
  int      nlibs;
  int      more_libs;
  char     libs[MOD_LIB_NAMES][32];
} lib_mods_t;

typedef struct {
  char     title_id[12];
  char     content_id[40];
  char     name[128];
  char     version[16];
  char     system_ver[8];       /* "12.60" — PS4 titles: PS4 system software */
  int      platform;            /* 0 PS5, 1 PS4, -1 unknown */
  int64_t  size;                /* bytes as the system counts them, -1 */
  char     installed[24];       /* ISO 8601, UTC */
  char     last_played[24];
  int64_t  played_s;            /* this user's play time, -1 unknown */
  int64_t  order;               /* lastAccessIndex */
  int      disp;                /* contentinfo's row, for the fallback */
  int      picked;
  char     cover[160];          /* checked path, or empty */
  char     cover_ts[16];        /* the database's ?ts=, for the browser cache */
  char     source[128];         /* mount.lnk target, or empty */
  char     deeplink[96];        /* the tile's own link: psgm:play?id=... */
  uint64_t param_sdk;           /* param.json sdkVersion, PS5 titles */
  lib_mods_t mods;
  /* Where the data really lies, and in what form — see detect_storage(). */
  char     real_path[256];      /* folder, image file or installed app dir */
  char     format[8];           /* folder exfat ffpkg ffpfs ffpfsc image pkg */
  int      smp;                 /* ShadowMountPlus manages it */
  int      smp_mounted;
  int      smp_available;
  /* Asked of ShadowMountPlus while the list is built, not while the answer is:
     the call can wait on its socket, and the answer is made under g_lock. */
  int      smp_can_move;
  int      smp_can_unpack;
} lib_game_t;

/* The folder checks read the USB drive; their result is kept per title and
   only redone when eboot.bin or fakelib changes. */
typedef struct {
  char       title_id[12];
  char       source[128];
  int64_t    eboot_mtime, eboot_size, fakelib_mtime;
  lib_mods_t mods;
} mods_cache_t;

/* A folder's size, kept for SIZE_TTL_MS: summing it means a stat() for every
   file in it, which on a game of 100 000 files on a USB drive is not something
   to repeat every few seconds. */
typedef struct {
  char     path[256];
  int64_t  size;
  uint64_t ms;                   /* mono clock, when it was measured (or would have been) */
  uint32_t ttl_ms;               /* how long it counts: SIZE_TTL_MS, or SIZE_SEED_TTL_MS for a seed */
  int64_t  wall_s;               /* the same moment as a date, for the cache folder        */
} size_cache_t;

/* Two locks, in this order. g_refresh_lock lets one thread at a time read the
   database and the drives — slow, and it can block on a drive that has gone
   away — and guards the two caches below, which only refresh() touches.
   g_lock guards what the answers are made from (g_games and the fields next to
   it) and is only ever held briefly, so that a launch, a cover or a copy
   request never waits for a drive. */
static pthread_mutex_t g_refresh_lock = PTHREAD_MUTEX_INITIALIZER;
static mods_cache_t    g_mods_cache[LIBRARY_MAX];
static int             g_mods_cached;
static size_cache_t    g_size_cache[LIBRARY_MAX];
static int             g_sizes_cached;

static lib_game_t      g_games[LIBRARY_MAX];
static int             g_count;
static int             g_truncated;       /* more rows than LIBRARY_MAX */
static int             g_state;           /* 0 never read, 1 ok, -1 failed */
static char            g_error[160];
static int             g_user_matched;    /* the foreground user's own table */
static int             g_from_tiles;      /* 0 = contentinfo fallback */
static uint64_t        g_read_ms;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;


static int
is_title_id(const char *s) {
  for(int i = 0; i < 4; i++) if(s[i] < 'A' || s[i] > 'Z') return 0;
  for(int i = 4; i < 9; i++) if(s[i] < '0' || s[i] > '9') return 0;
  return s[9] == 0;
}

/* "2026-09-27 23:04:23.714" -> "2026-09-27T23:04:23Z"; anything else -> "". */
static void
iso_time(const char *in, char *out, size_t out_len) {
  static const char pat[] = "dddd-dd-dd dd:dd:dd";
  out[0] = 0;
  if(out_len < 21) return;
  for(size_t i = 0; i < sizeof(pat) - 1; i++) {
    if(pat[i] == 'd' ? (in[i] < '0' || in[i] > '9') : in[i] != pat[i]) return;
  }
  memcpy(out, in, 19);
  out[10] = 'T';
  out[19] = 'Z';
  out[20] = 0;
}

/* Only a PNG inside the title's own metadata directories. The path comes out
   of the database, and the cover route must not become a way to read any
   file on the console. */
static int
cover_ok(const char *path, const char *tid) {
  static const char *const roots[] = {
    "/user/app/", "/user/appmeta/", "/system_data/priv/appmeta/",
    "/mnt/ext0/user/app/", "/mnt/ext0/user/appmeta/",
    "/mnt/ext1/user/app/", "/mnt/ext1/user/appmeta/",
  };
  size_t n = strlen(path);
  if(n < 20 || ps5tm_path_has_dotdot(path) ||
     strcasecmp(path + n - 4, ".png") != 0)
    return 0;
  for(size_t i = 0; i < sizeof(roots) / sizeof(roots[0]); i++) {
    size_t rl = strlen(roots[i]);
    if(!strncmp(path, roots[i], rl) && !strncmp(path + rl, tid, 9) &&
       path[rl + 9] == '/')
      return 1;
  }
  return 0;
}

/* The version word counts in BCD: 0x1260000000000000 is 12.60 on a PS5
   title, 0x05508000 is 5.50 on a PS4 one. */
static void
system_version(const cJSON *j, int platform, char *out, size_t out_len) {
  out[0] = 0;
  const cJSON *v = cJSON_GetObjectItem(j, platform == 1 ? "SYSTEM_VER"
                                                         : "SYSTEM_VER_PPR");
  uint64_t w = 0;
  if(cJSON_IsNumber(v) && v->valuedouble > 0 && v->valuedouble < 1.8e19)
    w = (uint64_t)v->valuedouble;
  else if(cJSON_IsString(v))
    w = strtoull(v->valuestring, NULL, 10);
  if(!w) return;
  unsigned major = platform == 1 ? (unsigned)(w >> 24) & 0xFF
                                 : (unsigned)(w >> 56) & 0xFF;
  unsigned minor = platform == 1 ? (unsigned)(w >> 16) & 0xFF
                                 : (unsigned)(w >> 48) & 0xFF;
  if(major) snprintf(out, out_len, "%x.%02x", major, minor);
}

static void
read_mount_source(const char *tid, char *out, size_t out_len) {
  char path[64];
  snprintf(path, sizeof(path), "/user/app/%s/mount.lnk", tid);
  out[0] = 0;
  int fd = open(path, O_RDONLY);
  if(fd < 0) return;
  char buf[160];
  ssize_t n = read(fd, buf, sizeof(buf) - 1);
  close(fd);
  if(n <= 1 || buf[0] != '/') return;
  size_t len = (size_t)n;
  while(len && (buf[len - 1] == '\n' || buf[len - 1] == '\r' || !buf[len - 1]))
    len--;
  for(size_t i = 0; i < len; i++)
    if((unsigned char)buf[i] < 0x20) return;     /* not a path */
  if(len >= out_len) return;                      /* cut, it would be wrong */
  memcpy(out, buf, len);
  out[len] = 0;
  if(ps5tm_path_has_dotdot(out)) out[0] = 0;
}


static uint32_t
rd16(const unsigned char *p) {
  return (uint32_t)p[0] | (uint32_t)p[1] << 8;
}

static uint32_t
rd32(const unsigned char *p) {
  return rd16(p) | rd16(p + 2) << 16;
}

static uint64_t
rd64(const unsigned char *p) {
  return (uint64_t)rd32(p) | (uint64_t)rd32(p + 4) << 32;
}

/* The SDK version eboot.bin was built for, or patched to. A fake-signed SELF
 * (magic 4F 15 3D 1D, or 54 14 F5 EE) stores its segments in the clear:
 *
 *   0x18       u16 entry count n; entries of 32 bytes from 0x20 — props,
 *              offset, file size, memory size. Each program segment has a
 *              digest entry (props bit 0x10000) and a data entry; the ELF
 *              program header it belongs to is props >> 20.
 *   0x20+32n   the ELF header, then its program headers
 *
 * PT_SCE_PROCPARAM (0x61000001) lies inside a LOAD segment, and its file
 * offset maps through that segment's data entry. The block starts with its
 * size, "ORBI" and an entry count; the u32 at +0x14 is the SDK version,
 * 0x04000033 = 4.00, 0x10000040 = 10.00.
 *
 * Two small reads, never the whole file — AC Shadows' eboot.bin is 212 MB. */
static uint32_t
eboot_sdk(const char *root) {
  char path[192];
  snprintf(path, sizeof(path), "%s/eboot.bin", root);
  int fd = open(path, O_RDONLY);
  if(fd < 0) return 0;

  unsigned char h[4096];
  uint32_t sdk = 0;
  ssize_t  n   = read(fd, h, sizeof(h));
  if(n < 0x100 || (memcmp(h, "\x4f\x15\x3d\x1d", 4) != 0 &&
                   memcmp(h, "\x54\x14\xf5\xee", 4) != 0))
    goto out;

  uint32_t cnt = rd16(h + 0x18);
  size_t   elf = 0x20 + (size_t)cnt * 0x20;
  if(cnt == 0 || cnt > 64 || elf + 64 > (size_t)n ||
     memcmp(h + elf, "\x7f" "ELF", 4) != 0)
    goto out;
  uint64_t phoff = rd64(h + elf + 32);
  uint32_t phent = rd16(h + elf + 54), phnum = rd16(h + elf + 56);
  if(phent != 56 || phnum == 0 || phnum > 64 || phoff > (uint64_t)n ||
     elf + phoff + (uint64_t)phnum * 56 > (uint64_t)n)
    goto out;
  const unsigned char *ph = h + elf + phoff;

  uint64_t pp = UINT64_MAX;
  for(uint32_t i = 0; i < phnum; i++)
    if(rd32(ph + i * 56) == 0x61000001) { pp = rd64(ph + i * 56 + 8); break; }
  if(pp == UINT64_MAX) goto out;

  for(uint32_t i = 0; i < phnum; i++) {
    const unsigned char *p = ph + i * 56;
    uint64_t off = rd64(p + 8), fsz = rd64(p + 32);
    if(rd32(p) != 1 || pp < off || pp - off + 0x18 > fsz) continue;
    for(uint32_t e = 0; e < cnt; e++) {
      const unsigned char *en = h + 0x20 + e * 0x20;
      uint64_t props = rd64(en);
      if(((props >> 20) & 0xFFFF) != i || (props & 0x10000)) continue;
      if(props & 0xA) goto out;                  /* encrypted or compressed */
      uint64_t eoff = rd64(en + 8), efsz = rd64(en + 16), rel = pp - off;
      unsigned char b[0x18];
      if(rel + sizeof(b) <= efsz &&
         lseek(fd, (off_t)(eoff + rel), SEEK_SET) == (off_t)(eoff + rel) &&
         read(fd, b, sizeof(b)) == (ssize_t)sizeof(b) &&
         memcmp(b + 8, "ORBI", 4) == 0)
        sdk = rd32(b + 0x14);
      goto out;
    }
    goto out;
  }
out:
  close(fd);
  return sdk;
}

static void
scan_fakelib(const char *root, lib_mods_t *m) {
  char path[192];
  snprintf(path, sizeof(path), "%s/fakelib", root);
  DIR *d = opendir(path);
  if(!d) return;
  struct dirent *e;
  while((e = readdir(d)) != NULL) {
    const char *nm  = e->d_name;
    size_t      len = strlen(nm);
    if(nm[0] == '.') continue;
    if(!(len > 5 && !strcasecmp(nm + len - 5, ".sprx")) &&
       !(len > 4 && !strcasecmp(nm + len - 4, ".prx")))
      continue;
    if(!strcasecmp(nm, "libSceAmpr.sprx"))        m->ampr = 1;
    else if(!strcasecmp(nm, "libScePlayGo.sprx")) m->playgo = 1;
    else                                          m->extra_libs++;
    if(m->nlibs < MOD_LIB_NAMES)
      snprintf(m->libs[m->nlibs++], sizeof(m->libs[0]), "%s", nm);
    else
      m->more_libs++;
  }
  closedir(d);
}

static int64_t
stat_mtime(const char *path, int64_t *size) {
  struct stat st;
  if(stat(path, &st) != 0) return -1;
  if(size) *size = (int64_t)st.st_size;
  return (int64_t)st.st_mtime;
}

/* Called from refresh(), with g_refresh_lock held. */
static void
detect_mods(lib_game_t *g) {
  lib_mods_t *m = &g->mods;
  memset(m, 0, sizeof(*m));
  if(g->platform != 0 || !g->source[0]) return;           /* MODS_NONE */

  struct stat st;
  if(stat(g->source, &st) != 0 || !S_ISDIR(st.st_mode)) {
    m->state = MODS_UNREACHABLE;
    return;
  }

  char    path[192];
  int64_t eb_size = -1;
  snprintf(path, sizeof(path), "%s/eboot.bin", g->source);
  int64_t eb_mtime = stat_mtime(path, &eb_size);
  snprintf(path, sizeof(path), "%s/fakelib", g->source);
  int64_t fl_mtime = stat_mtime(path, NULL);

  mods_cache_t *c = NULL;
  for(int i = 0; i < g_mods_cached; i++)
    if(!strcmp(g_mods_cache[i].title_id, g->title_id)) { c = &g_mods_cache[i]; break; }

  if(c && !strcmp(c->source, g->source) && c->eboot_mtime == eb_mtime &&
     c->eboot_size == eb_size && c->fakelib_mtime == fl_mtime) {
    *m = c->mods;
  } else {
    m->state     = MODS_CHECKED;
    scan_fakelib(g->source, m);
    m->eboot_sdk = eboot_sdk(g->source);
    if(!c && g_mods_cached < LIBRARY_MAX) c = &g_mods_cache[g_mods_cached++];
    if(c) {
      snprintf(c->title_id, sizeof(c->title_id), "%s", g->title_id);
      snprintf(c->source, sizeof(c->source), "%s", g->source);
      c->eboot_mtime   = eb_mtime;
      c->eboot_size    = eb_size;
      c->fakelib_mtime = fl_mtime;
      c->mods          = *m;
    }
  }

  /* These appear once the game has run; two stats, never cached. */
  snprintf(path, sizeof(path), "%s/ampr_emu.index", g->source);
  m->ampr_index = stat(path, &st) == 0;
  snprintf(path, sizeof(path), "%s/playlgo.log", g->source);
  m->playgo_log = stat(path, &st) == 0;

  uint32_t eb = (m->eboot_sdk >> 16) & 0xFFFF;              /* 0x0400 = 4.00 */
  uint32_t pj = (uint32_t)(g->param_sdk >> 48) & 0xFFFF;    /* 0x0500 = 5.00 */
  m->backport = m->extra_libs > 0 || (eb && pj && eb < pj);
}


/* Real, current size on disk. The database's own "size" is only the local
   part for a title kept elsewhere (see the file header) — a placeholder set
   once at registration, not the live size. One stat() for a single-file
   image; summed recursively for a folder, the same judgement gamecopy.c and
   gameconvert.c already make before moving or converting one.

   -1 whenever the answer would be a guess: a read error half way (a drive
   pulled during the walk), a tree deeper than any game, a folder that is gone.
   A partial sum is worse than none — a 280 GB game would show as 12. */
static int64_t
dir_size(const char *path, int depth) {
  if(depth > 32) return -1;
  DIR *d = opendir(path);
  if(!d) return -1;
  int64_t total = 0;
  struct dirent *e;
  char child[600];
  for(;;) {
    errno = 0;
    e = readdir(d);
    if(!e) {
      if(errno != 0) total = -1;               /* an error, not the end */
      break;
    }
    if(!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
    if(snprintf(child, sizeof(child), "%s/%s", path, e->d_name) >= (int)sizeof(child)) continue;
    struct stat st;
    if(lstat(child, &st) != 0) {
      if(errno == ENOENT) continue;            /* gone since readdir: fine */
      total = -1;
      break;
    }
    if(S_ISDIR(st.st_mode)) {
      int64_t sub = dir_size(child, depth + 1);
      if(sub < 0) { total = -1; break; }
      total += sub;
    } else if(S_ISREG(st.st_mode)) {
      total += (int64_t)st.st_size;
    }
  }
  closedir(d);
  return total;
}

/* A single file (an image) is one stat(). A folder is walked, at most once per
   SIZE_TTL_MS; when the walk fails — the drive is gone — the last size known
   still beats the database's, which is only the local part. Called from
   refresh(), with g_refresh_lock held. */
static int64_t
real_size(const char *path, int is_folder) {
  if(!is_folder) {
    struct stat st;
    return stat(path, &st) == 0 ? (int64_t)st.st_size : -1;
  }

  size_cache_t *c = NULL, *oldest = NULL;
  for(int i = 0; i < g_sizes_cached; i++) {
    if(!strcmp(g_size_cache[i].path, path)) { c = &g_size_cache[i]; break; }
    if(!oldest || g_size_cache[i].ms < oldest->ms) oldest = &g_size_cache[i];
  }
  uint64_t now = ps5tm_mono_ms();
  if(c && now - c->ms < c->ttl_ms) return c->size;

  int64_t sz = dir_size(path, 0);
  if(sz < 0) return c ? c->size : -1;

  if(!c) {
    if(g_sizes_cached < LIBRARY_MAX) c = &g_size_cache[g_sizes_cached++];
    else c = oldest;
  }
  if(c) {
    snprintf(c->path, sizeof(c->path), "%s", path);
    c->size   = sz;
    c->ms     = now;
    c->ttl_ms = SIZE_TTL_MS;
    c->wall_s = (int64_t)(ps5tm_now_ms() / 1000);
  }
  return sz;
}

/* Where a title's data really lies, and in what form.
 *
 * ShadowMountPlus knows best where it has one (smp.c): the real image file
 * or folder, and the image's filesystem. On the test console (29.09.2026)
 * the extensions and its image types matched one to one — .exfat exfatfs,
 * .ffpkg ufs, .ffpfs pfs, .ffpfsc pfsc — so the extension decides and the
 * image type fills in for an unusual name. Without it: a mount.lnk into
 * /mnt/shadowmnt is some image, one anywhere else a folder, and no mount.lnk
 * but an app.pkg is an installed package. Called from refresh(). */
static void
detect_storage(lib_game_t *g) {
  const char *fmt = "";
  ps5tm_smp_game_t s;
  if(ps5tm_smp_find(g->title_id, &s) == 0) {
    g->smp           = 1;
    g->smp_mounted   = s.mounted;
    g->smp_available = s.available;
    g->smp_can_move   = s.available && ps5tm_smp_can("move_game_source");
    g->smp_can_unpack = s.available && ps5tm_smp_can("unpack_game_image");
    snprintf(g->real_path, sizeof(g->real_path), "%s", s.path);
    const char *ext = strrchr(s.path, '.');
    const char *sl  = strrchr(s.path, '/');
    if(ext && sl && ext < sl) ext = NULL;
    if(!strcmp(s.source_type, "folder"))                fmt = "folder";
    else if(ext && !strcasecmp(ext, ".exfat"))          fmt = "exfat";
    else if(ext && !strcasecmp(ext, ".ffpkg"))          fmt = "ffpkg";
    else if(ext && !strcasecmp(ext, ".ffpfs"))          fmt = "ffpfs";
    else if(ext && !strcasecmp(ext, ".ffpfsc"))         fmt = "ffpfsc";
    else if(!strcmp(s.image_type, "exfatfs"))           fmt = "exfat";
    else if(!strcmp(s.image_type, "ufs"))               fmt = "ffpkg";
    else if(!strcmp(s.image_type, "pfs"))               fmt = "ffpfs";
    else if(!strcmp(s.image_type, "pfsc"))              fmt = "ffpfsc";
    else                                                fmt = "image";
  } else if(g->source[0] && strncmp(g->source, "/mnt/shadowmnt", 14)) {
    snprintf(g->real_path, sizeof(g->real_path), "%s", g->source);
    fmt = "folder";
  } else if(g->source[0]) {
    fmt = "image";                          /* mounted from a file unknown */
  } else {
    static const char *const roots[] = {
      "/user/app/", "/mnt/ext0/user/app/", "/mnt/ext1/user/app/"
    };
    for(size_t i = 0; i < sizeof(roots) / sizeof(roots[0]); i++) {
      char path[80];
      struct stat st;
      snprintf(path, sizeof(path), "%s%s/app.pkg", roots[i], g->title_id);
      if(stat(path, &st) != 0) continue;
      snprintf(g->real_path, sizeof(g->real_path), "%s%s", roots[i], g->title_id);
      fmt = "pkg";
      break;
    }
  }
  snprintf(g->format, sizeof(g->format), "%s", fmt);

  if(g->real_path[0] == '/' && strcmp(fmt, "pkg")) {
    int64_t sz = real_size(g->real_path, !strcmp(fmt, "folder"));
    if(sz >= 0) g->size = sz;
  }
}


/* ---- the cache folder (libcache.c): seeded before the first scan, written after each ---- */

static mods_cache_t *
mods_find(const char *tid) {
  for(int i = 0; i < g_mods_cached; i++)
    if(!strcmp(g_mods_cache[i].title_id, tid)) return &g_mods_cache[i];
  return NULL;
}

static size_cache_t *
sizes_find(const char *path) {
  for(int i = 0; i < g_sizes_cached; i++)
    if(!strcmp(g_size_cache[i].path, path)) return &g_size_cache[i];
  return NULL;
}

static int g_cache_seeded;                  /* refresh() only, under g_refresh_lock */

/* What the metas of an earlier run knew goes into the memory caches — once, and only the
   slow parts: the mods answer (judged afterwards against the keys of eboot.bin and fakelib
   like any other entry) and the size of a folder (good for SIZE_SEED_TTL_MS from the day it
   was measured; an older one is kept as a fallback for a drive that is gone, and measured
   again). Called from refresh(). */
static void
cache_seed(void) {
  if(g_cache_seeded) return;
  g_cache_seeded = 1;
  ps5tm_libmeta_t *v = calloc(LIBRARY_MAX, sizeof(*v));
  if(!v) return;
  unsigned n = ps5tm_libcache_load(v, LIBRARY_MAX);
  int64_t  now_s = (int64_t)(ps5tm_now_ms() / 1000);
  uint64_t mono  = ps5tm_mono_ms();
  for(unsigned i = 0; i < n; i++) {
    const ps5tm_libmeta_t *m = &v[i];
    if(m->mods_checked && m->source[0] && !mods_find(m->title_id) && g_mods_cached < LIBRARY_MAX) {
      mods_cache_t *c = &g_mods_cache[g_mods_cached++];
      memset(c, 0, sizeof(*c));
      snprintf(c->title_id, sizeof(c->title_id), "%s", m->title_id);
      snprintf(c->source, sizeof(c->source), "%s", m->source);
      c->eboot_mtime   = m->eboot_mtime;
      c->eboot_size    = m->eboot_size;
      c->fakelib_mtime = m->fakelib_mtime;
      lib_mods_t *md = &c->mods;
      md->state      = MODS_CHECKED;
      md->backport   = m->backport;
      md->ampr       = m->ampr;
      md->playgo     = m->playgo;
      md->extra_libs = m->extra_libs;
      md->eboot_sdk  = m->eboot_sdk;
      md->more_libs  = m->more_libs;
      md->nlibs      = m->nlibs < MOD_LIB_NAMES ? m->nlibs : MOD_LIB_NAMES;
      for(int k = 0; k < md->nlibs; k++) snprintf(md->libs[k], sizeof(md->libs[k]), "%s", m->libs[k]);
    }
    if(!strcmp(m->format, "folder") && m->size_path[0] == '/' && m->size >= 0 && !sizes_find(m->size_path) &&
       g_sizes_cached < LIBRARY_MAX) {
      size_cache_t *c = &g_size_cache[g_sizes_cached++];
      int64_t age_s = now_s - m->size_at;
      uint64_t age_ms = age_s > 0 ? (uint64_t)age_s * 1000 : 0;
      int fresh = m->size_at > 0 && age_s >= 0 && age_ms < SIZE_SEED_TTL_MS;
      memset(c, 0, sizeof(*c));
      snprintf(c->path, sizeof(c->path), "%s", m->size_path);
      c->size   = m->size;
      c->wall_s = m->size_at;
      /* What is left of its life, counted from now: the monotonic clock may have run for less time than
         the size is old (a console that has just started), so it cannot be counted back from. An older
         size counts no more at all; it is what a drive that is gone leaves to show. */
      c->ms     = mono;
      c->ttl_ms = fresh ? (uint32_t)(SIZE_SEED_TTL_MS - age_ms) : 0;
    }
  }
  free(v);
}

/* The cover as the database names it, else where the system keeps covers. 0 and the path, or -1. */
static int
cover_source(const char *db_path, const char *tid, char *path, size_t path_len) {
  struct stat st;
  if(db_path[0] && stat(db_path, &st) == 0) {
    snprintf(path, path_len, "%s", db_path);
    return 0;
  }
  static const char *const fallback[] = {
    "/user/appmeta/%s/icon0.png",
    "/user/app/%s/sce_sys/icon0.png",
  };
  for(size_t i = 0; i < sizeof(fallback) / sizeof(fallback[0]); i++) {
    snprintf(path, path_len, fallback[i], tid);
    if(stat(path, &st) == 0) return 0;
  }
  path[0] = 0;
  return -1;
}

/* After a scan: the finished list goes to the cache folder (only titles whose record changed
   are written), and every cover gets its copy. Called from refresh(), g_lock not held. */
static void
cache_save(const lib_game_t *list, int n) {
  if(n <= 0) return;
  ps5tm_libmeta_t *v = calloc((size_t)n, sizeof(*v));
  if(!v) return;
  for(int i = 0; i < n; i++) {
    const lib_game_t *g = &list[i];
    ps5tm_libmeta_t  *m = &v[i];
    snprintf(m->title_id, sizeof(m->title_id), "%s", g->title_id);
    snprintf(m->content_id, sizeof(m->content_id), "%s", g->content_id);
    snprintf(m->name, sizeof(m->name), "%s", g->name);
    snprintf(m->version, sizeof(m->version), "%s", g->version);
    snprintf(m->source, sizeof(m->source), "%s", g->source);
    snprintf(m->format, sizeof(m->format), "%s", g->format);
    snprintf(m->real_path, sizeof(m->real_path), "%s", g->real_path);
    m->platform = g->platform;
    m->size     = -1;
    const mods_cache_t *mc = g->mods.state == MODS_CHECKED ? mods_find(g->title_id) : NULL;
    if(mc) {
      const lib_mods_t *md = &g->mods;
      m->mods_checked  = 1;
      m->backport      = md->backport;
      m->ampr          = md->ampr;
      m->playgo        = md->playgo;
      m->extra_libs    = md->extra_libs;
      m->eboot_sdk     = md->eboot_sdk;
      m->more_libs     = md->more_libs;
      m->nlibs         = md->nlibs;
      for(int k = 0; k < md->nlibs && k < MOD_LIB_NAMES; k++) snprintf(m->libs[k], sizeof(m->libs[k]), "%s", md->libs[k]);
      m->eboot_mtime   = mc->eboot_mtime;
      m->eboot_size    = mc->eboot_size;
      m->fakelib_mtime = mc->fakelib_mtime;
    }
    if(!strcmp(g->format, "folder") && g->real_path[0] == '/') {
      const size_cache_t *sc = sizes_find(g->real_path);
      if(sc && sc->size >= 0) {
        snprintf(m->size_path, sizeof(m->size_path), "%s", g->real_path);
        m->size    = sc->size;
        m->size_at = sc->wall_s;
      }
    }
  }
  ps5tm_libcache_save(v, (unsigned)n);
  free(v);

  for(int i = 0; i < n; i++) {
    char src[200];
    if(cover_source(list[i].cover, list[i].title_id, src, sizeof(src)) == 0)
      ps5tm_libcache_cover_put(list[i].title_id, list[i].cover_ts, src);
  }
}


/* The whole file in one read. The shell rewrites it in place (rollback
   journal, journal_mode delete), so a read can catch it halfway. Two guards:
   no read while a journal says a write is under way — except on the last
   try, in case a crash left one behind — and the header's change counter
   must be the same before and after. */
static unsigned char *
read_appdb(size_t *size_out, char *err, size_t err_len) {
  for(int attempt = 0; attempt < 5; attempt++) {
    if(attempt) usleep(120 * 1000);
    struct stat js;
    if(attempt < 4 && stat(APPDB_JOURNAL, &js) == 0 && js.st_size > 0) {
      snprintf(err, err_len, "Die Konsole schreibt gerade in ihre App-Datenbank.");
      continue;
    }

    int fd = open(APPDB_PATH, O_RDONLY);
    if(fd < 0) {
      snprintf(err, err_len, "Die App-Datenbank der Konsole ist nicht lesbar.");
      return NULL;
    }
    struct stat st;
    if(fstat(fd, &st) != 0 || st.st_size < 512 ||
       (uint64_t)st.st_size > APPDB_MAX_BYTES) {
      close(fd);
      snprintf(err, err_len, "Die App-Datenbank hat eine unerwartete Größe.");
      return NULL;
    }
    size_t n = (size_t)st.st_size;
    unsigned char *buf = malloc(n);
    if(!buf) {
      close(fd);
      snprintf(err, err_len, "Kein Speicher für die App-Datenbank.");
      return NULL;
    }
    size_t got = 0;
    while(got < n) {
      ssize_t r = read(fd, buf + got, n - got);
      if(r <= 0) break;
      got += (size_t)r;
    }
    unsigned char again[4];
    int same = got == n && lseek(fd, 24, SEEK_SET) == 24 &&
               read(fd, again, 4) == 4 && memcmp(again, buf + 24, 4) == 0;
    close(fd);
    /* The counter in the header changes with the first page of a write, so a
       writer that began after the first look and is still busy with the later
       pages passes the comparison above. Its journal gives it away. */
    if(same && attempt < 4 && stat(APPDB_JOURNAL, &js) == 0 && js.st_size > 0)
      same = 0;
    if(same) { *size_out = n; return buf; }
    free(buf);
    snprintf(err, err_len, "Die App-Datenbank änderte sich beim Lesen.");
  }
  return NULL;
}


typedef struct {
  lib_game_t *list;
  int         count;
  int c_tid, c_cid, c_name, c_size, c_inst, c_icon, c_plat, c_disp, c_json;
  int c_link;
  int truncated;                  /* the table has more rows than fit */
} content_ctx_t;

/* What a tile press opens. Games carry "psgm:play?id=<their id>"; a homebrew
   tile may point at a local web page instead (the game compressor:
   "http://127.0.0.1:5910/"). Anything else is not passed on — the link ends
   up in a notification button. */
static void
tile_link(const char *in, const char *tid, char *out, size_t out_len) {
  char want[32];
  snprintf(want, sizeof(want), "psgm:play?id=%s", tid);
  out[0] = 0;
  if(!strcmp(in, want) ||
     ((!strncmp(in, "http://127.0.0.1:", 17) || !strncmp(in, "http://localhost:", 17))
      && !strpbrk(in, "\"\\ <>")))
    snprintf(out, out_len, "%s", in);
  else if(!strncmp(tid, "PPSA", 4) || !strncmp(tid, "CUSA", 4))
    snprintf(out, out_len, "%s", want);
}

static int
content_row(void *ctx, const sqlro_table_t *t, const sqlro_row_t *r) {
  content_ctx_t *c = ctx;
  (void)t;
  if(c->count >= LIBRARY_MAX) { c->truncated = 1; return 1; }

  lib_game_t *g = &c->list[c->count];
  memset(g, 0, sizeof(*g));
  if(sqlro_text(r, c->c_tid, g->title_id, sizeof(g->title_id)) != 0 ||
     !is_title_id(g->title_id))
    return 0;
  sqlro_text(r, c->c_cid, g->content_id, sizeof(g->content_id));
  sqlro_text(r, c->c_name, g->name, sizeof(g->name));

  int64_t v;
  g->size     = sqlro_int(r, c->c_size, &v) == 0 && v >= 0 ? v : -1;
  g->platform = sqlro_int(r, c->c_plat, &v) == 0 && (v == 0 || v == 1)
                  ? (int)v : -1;
  g->disp     = sqlro_int(r, c->c_disp, &v) == 0 ? (int)v : -1;
  g->played_s = -1;

  char tmp[200];
  if(sqlro_text(r, c->c_inst, tmp, sizeof(tmp)) == 0)
    iso_time(tmp, g->installed, sizeof(g->installed));

  tmp[0] = 0;
  sqlro_text(r, c->c_link, tmp, sizeof(tmp));
  tile_link(tmp, g->title_id, g->deeplink, sizeof(g->deeplink));

  /* "/user/app/PPSA20396/sce_sys/icon0.png?ts=1790453939" */
  if(sqlro_text(r, c->c_icon, tmp, sizeof(tmp)) == 0) {
    char *q = strchr(tmp, '?');
    if(q) {
      *q = 0;
      /* Digits only: it goes into the cover's URL. */
      size_t k = 0;
      if(!strncmp(q + 1, "ts=", 3))
        for(const char *d = q + 4;
            *d >= '0' && *d <= '9' && k + 1 < sizeof(g->cover_ts); d++)
          g->cover_ts[k++] = *d;
      g->cover_ts[k] = 0;
    }
    if(cover_ok(tmp, g->title_id))
      snprintf(g->cover, sizeof(g->cover), "%s", tmp);
  }

  /* AppInfoJson: the title's param.json or param.sfo as the system keeps it. */
  if(c->c_json >= 0 && c->c_json < r->ncols &&
     r->kind[c->c_json] == SQLRO_TEXT && r->len[c->c_json] > 2) {
    cJSON *j = cJSON_ParseWithLength((const char *)r->data[c->c_json],
                                     r->len[c->c_json]);
    if(j) {
      if(g->platform < 0) {
        const cJSON *pl = cJSON_GetObjectItem(j, "_ps_platform");
        if(cJSON_IsNumber(pl) && (pl->valueint == 0 || pl->valueint == 1))
          g->platform = pl->valueint;
      }
      const cJSON *ver = cJSON_GetObjectItem(j, "CONTENT_VERSION");
      if(!cJSON_IsString(ver)) ver = cJSON_GetObjectItem(j, "APP_VER");
      if(cJSON_IsString(ver))
        snprintf(g->version, sizeof(g->version), "%s", ver->valuestring);
      system_version(j, g->platform, g->system_ver, sizeof(g->system_ver));
      /* "0x0500000000000000" — the SDK the title was built with, which a
         backport leaves in param.json while it patches eboot.bin. */
      const cJSON *sdk = cJSON_GetObjectItem(j, "SDK_VERSION");
      if(cJSON_IsString(sdk))
        g->param_sdk = strtoull(sdk->valuestring, NULL, 0);
      else if(cJSON_IsNumber(sdk) && sdk->valuedouble > 0 &&
              sdk->valuedouble < 1.8e19)
        g->param_sdk = (uint64_t)sdk->valuedouble;
      cJSON_Delete(j);
    }
  }
  if(g->platform < 0) {
    if(!strncmp(g->title_id, "PPSA", 4)) g->platform = 0;
    else if(!strncmp(g->title_id, "CUSA", 4)) g->platform = 1;
  }

  c->count++;
  return 0;
}


typedef struct {
  lib_game_t *list;
  int        *count;
  int        *truncated;
  int         seen;
  int c_tid, c_name, c_cid, c_vis, c_disp, c_played, c_time, c_order;
} tile_ctx_t;

static int
tile_row(void *ctx, const sqlro_table_t *t, const sqlro_row_t *r) {
  tile_ctx_t *c = ctx;
  (void)t;
  int64_t vis = 0, disp = 0, v;
  if(sqlro_int(r, c->c_vis, &vis) != 0 || vis != 1) return 0;
  if(sqlro_int(r, c->c_disp, &disp) != 0 || disp != ROW_GAMES) return 0;

  char tid[12];
  if(sqlro_text(r, c->c_tid, tid, sizeof(tid)) != 0 || !is_title_id(tid))
    return 0;
  c->seen++;

  lib_game_t *g = NULL;
  for(int i = 0; i < *c->count; i++)
    if(!strcmp(c->list[i].title_id, tid)) { g = &c->list[i]; break; }
  if(!g) {
    /* A tile without an installed-content row: keep what the tile knows. When
       there is no room left this one is skipped, not the rest of the scan —
       the tiles behind it may well be on the list already. */
    if(*c->count >= LIBRARY_MAX) { *c->truncated = 1; return 0; }
    g = &c->list[(*c->count)++];
    memset(g, 0, sizeof(*g));
    snprintf(g->title_id, sizeof(g->title_id), "%s", tid);
    sqlro_text(r, c->c_cid, g->content_id, sizeof(g->content_id));
    g->size     = -1;
    g->platform = !strncmp(tid, "PPSA", 4) ? 0 : !strncmp(tid, "CUSA", 4) ? 1 : -1;
    tile_link("", tid, g->deeplink, sizeof(g->deeplink));
  }
  if(!g->name[0]) sqlro_text(r, c->c_name, g->name, sizeof(g->name));

  char tmp[40];
  if(sqlro_text(r, c->c_played, tmp, sizeof(tmp)) == 0)
    iso_time(tmp, g->last_played, sizeof(g->last_played));
  g->played_s = sqlro_int(r, c->c_time, &v) == 0 && v >= 0 ? v : -1;
  g->order    = sqlro_int(r, c->c_order, &v) == 0 ? v : 0;
  g->picked   = 1;
  return 0;
}


static int
by_recent(const void *a, const void *b) {
  const lib_game_t *x = a, *y = b;
  if(x->order != y->order) return x->order < y->order ? 1 : -1;
  return strcmp(x->title_id, y->title_id);
}

/* Reads the database and replaces the cache. Called with g_refresh_lock held
   and g_lock NOT held: everything slow happens before g_lock is taken, once,
   to publish the finished list. */
static void
refresh(void) {
  char err[160] = "";
  size_t size = 0;
  unsigned char *file = read_appdb(&size, err, sizeof(err));
  lib_game_t    *list = calloc(LIBRARY_MAX, sizeof(lib_game_t));
  sqlro_table_t *tab  = malloc(sizeof(sqlro_table_t));
  sqlro_db_t     db;
  int count = 0, user_matched = 0, from_tiles = 0, ok = 0, truncated = 0;
  int cache_on = ps5tm_libcache_enabled();
  if(cache_on) cache_seed();

  if(!file || !list || !tab) {
    if(!err[0]) snprintf(err, sizeof(err), "Kein Speicher für die Spieleliste.");
    goto done;
  }
  if(sqlro_open_mem(&db, file, size) != 0) {
    snprintf(err, sizeof(err), "Die App-Datenbank hat ein unbekanntes Format.");
    goto done;
  }

  if(sqlro_find_table(&db, "tbl_contentinfo", tab) != 0) {
    snprintf(err, sizeof(err), "In der App-Datenbank fehlt tbl_contentinfo.");
    goto done;
  }
  content_ctx_t cc = {
    list, 0,
    sqlro_col(tab, "titleId"), sqlro_col(tab, "contentId"),
    sqlro_col(tab, "titleName"), sqlro_col(tab, "size"),
    sqlro_col(tab, "installTime"), sqlro_col(tab, "icon0Info"),
    sqlro_col(tab, "platform"), sqlro_col(tab, "dispLocation"),
    sqlro_col(tab, "AppInfoJson"), sqlro_col(tab, "pprDeeplinkUri"),
    0,
  };
  if(cc.c_tid < 0 || sqlro_scan(&db, tab, content_row, &cc) < 0) {
    snprintf(err, sizeof(err), "tbl_contentinfo ließ sich nicht lesen.");
    goto done;
  }
  count     = cc.count;
  truncated = cc.truncated;

  /* The foreground user's tiles; failing that, any user's. */
  char name[40] = "";
  int uid = -1;
  if(ps5tm_user_service_active_user(&uid) == 0 && uid > 0) {
    snprintf(name, sizeof(name), "tbl_iconinfo_%010u", (unsigned)uid);
    user_matched = sqlro_find_table(&db, name, tab) == 0;
  }
  int have_tiles = user_matched;
  if(!have_tiles &&
     sqlro_find_table_prefix(&db, "tbl_iconinfo_", name, sizeof(name)) == 0)
    have_tiles = sqlro_find_table(&db, name, tab) == 0;

  if(have_tiles) {
    tile_ctx_t tc = {
      list, &count, &truncated, 0,
      sqlro_col(tab, "titleId"), sqlro_col(tab, "titleName"),
      sqlro_col(tab, "contentId"), sqlro_col(tab, "visible"),
      sqlro_col(tab, "dispLocation"), sqlro_col(tab, "lastPlayedDate"),
      sqlro_col(tab, "playedTimeOnConsole"), sqlro_col(tab, "lastAccessIndex"),
    };
    if(tc.c_tid >= 0 && tc.c_vis >= 0 && tc.c_disp >= 0 &&
       sqlro_scan(&db, tab, tile_row, &tc) >= 0)
      from_tiles = 1;
  }
  if(!from_tiles) {
    /* No tile table to go by: the games row as the content table files it. */
    for(int i = 0; i < count; i++) list[i].picked = list[i].disp == ROW_GAMES;
  }

  int n = 0;
  for(int i = 0; i < count; i++) {
    if(!list[i].picked) continue;
    if(n != i) list[n] = list[i];
    read_mount_source(list[n].title_id, list[n].source, sizeof(list[n].source));
    detect_mods(&list[n]);
    detect_storage(&list[n]);
    n++;
  }
  qsort(list, (size_t)n, sizeof(lib_game_t), by_recent);
  count = n;
  ok = 1;

done:
  pthread_mutex_lock(&g_lock);
  if(ok) {
    memcpy(g_games, list, (size_t)count * sizeof(lib_game_t));
    g_count        = count;
    g_truncated    = truncated;
    g_state        = 1;
    g_error[0]     = 0;
    g_user_matched = user_matched;
    g_from_tiles   = from_tiles;
  } else {
    /* A failed read keeps the last good list; only a first read fails loud. */
    if(g_state != 1) g_state = -1;
    snprintf(g_error, sizeof(g_error), "%s", err);
  }
  g_read_ms = ps5tm_mono_ms();
  pthread_mutex_unlock(&g_lock);
  if(ok && cache_on) cache_save(list, count);

  /* Said once per spell, not at every read: the page asks every few seconds,
     and a log that repeats itself pushes out everything else. refresh() runs
     one at a time, so these two need no lock of their own. */
  static char last_err[160];
  static int  warned_truncated;
  if(!ok) {
    if(strcmp(last_err, err)) {
      snprintf(last_err, sizeof(last_err), "%s", err);
      PS5TM_WARN("library_read_failed", "Spieleliste: %s", err);
    }
  } else {
    last_err[0] = 0;
  }
  if(ok && truncated && !warned_truncated)
    PS5TM_WARN("library_truncated",
               "Die App-Datenbank nennt mehr als %d Titel; die übrigen "
               "erscheinen nicht in der Spieleliste.", LIBRARY_MAX);
  if(ok) warned_truncated = truncated;
  free(tab);
  free(list);
  free(file);
}

/* Brings the list up to date when it is older than its TTL — or reads it for
   the first time — without holding g_lock while it does. One thread reads at a
   time; a caller that finds a read already under way takes the list as it is
   rather than queue behind a drive, except for the very first read, which has
   nothing to fall back on and is waited for. */
static void
ensure_fresh(int ttl_applies) {
  pthread_mutex_lock(&g_lock);
  int first = (g_state == 0);
  int stale = first || (ttl_applies && ps5tm_mono_ms() - g_read_ms >= LIBRARY_TTL_MS);
  pthread_mutex_unlock(&g_lock);
  if(!stale) return;

  if(first) pthread_mutex_lock(&g_refresh_lock);
  else if(pthread_mutex_trylock(&g_refresh_lock) != 0) return;

  /* The thread ahead of us may just have finished the very read we came for. */
  pthread_mutex_lock(&g_lock);
  int need = (g_state == 0) ||
             (ttl_applies && ps5tm_mono_ms() - g_read_ms >= LIBRARY_TTL_MS);
  pthread_mutex_unlock(&g_lock);
  if(need) refresh();
  pthread_mutex_unlock(&g_refresh_lock);
}

void
ps5tm_library_forget(void) {
  pthread_mutex_lock(&g_lock);
  g_read_ms = 0;
  pthread_mutex_unlock(&g_lock);
}


/* Every title id the app database lists as installed (tbl_contentinfo), read
   afresh: the games list's cache is not consulted, and the games row is not
   the limit — a tile in the media row counts as much as a game. */
typedef struct {
  char (*ids)[12];
  int    max, count, c_tid;
} ids_ctx_t;

static int
ids_row(void *ctx, const sqlro_table_t *t, const sqlro_row_t *r) {
  ids_ctx_t *c = ctx;
  (void)t;
  char tid[12];
  if(sqlro_text(r, c->c_tid, tid, sizeof(tid)) != 0 || !tid[0]) return 0;
  if(c->count < c->max) snprintf(c->ids[c->count], sizeof(c->ids[0]), "%s", tid);
  c->count++;
  return 0;
}

int
ps5tm_library_installed_ids(char (*ids)[12], int max) {
  char err[160] = "";
  size_t size = 0;
  unsigned char *file = read_appdb(&size, err, sizeof(err));
  sqlro_table_t *tab  = malloc(sizeof(sqlro_table_t));
  sqlro_db_t     db;
  int rc = -1;

  if(file && tab && sqlro_open_mem(&db, file, size) == 0 &&
     sqlro_find_table(&db, "tbl_contentinfo", tab) == 0) {
    ids_ctx_t c = { ids, max, 0, sqlro_col(tab, "titleId") };
    if(c.c_tid >= 0 && sqlro_scan(&db, tab, ids_row, &c) >= 0) rc = c.count;
  }
  if(rc < 0)
    PS5TM_WARN("appdb_ids_failed", "App-Datenbank nicht auswertbar: %s",
               err[0] ? err : "tbl_contentinfo fehlt oder ist unlesbar");
  free(tab);
  free(file);
  return rc;
}


/* A lookup that could not read the database: said in the log, but only once a minute (an installation asks several
   times, and so does the page). */
static void
lookup_failed(const char *what, const char *why) {
  static uint64_t last_ms;
  uint64_t now = ps5tm_mono_ms();
  uint64_t last = __atomic_load_n(&last_ms, __ATOMIC_RELAXED);
  if(last && now - last < 60000) return;
  __atomic_store_n(&last_ms, now, __ATOMIC_RELAXED);
  PS5TM_WARN("appdb_lookup_failed", "App-Datenbank: %s ließ sich nicht abfragen (%.100s).", what, why && why[0] ? why : "Datei oder Tabelle nicht lesbar");
}

/* One title of the app database, for checking an installation: 1 when it is listed (ver gets the version its
   AppInfoJson names, "" when it names none), 0 when it is not, -1 when the database cannot be read. */
typedef struct {
  const char *tid;
  char       *ver;
  size_t      ver_len;
  int         found, c_tid, c_json;
} tinfo_ctx_t;

static int
tinfo_row(void *ctx, const sqlro_table_t *t, const sqlro_row_t *r) {
  tinfo_ctx_t *c = ctx;
  (void)t;
  char tid[24];
  if(sqlro_text(r, c->c_tid, tid, sizeof(tid)) != 0 || strcmp(tid, c->tid) != 0) return 0;
  c->found = 1;
  if(c->c_json >= 0 && c->c_json < r->ncols && r->kind[c->c_json] == SQLRO_TEXT && r->len[c->c_json] > 2) {
    cJSON *j = cJSON_ParseWithLength((const char *)r->data[c->c_json], r->len[c->c_json]);
    if(j) {
      const cJSON *ver = cJSON_GetObjectItem(j, "CONTENT_VERSION");
      if(!cJSON_IsString(ver)) ver = cJSON_GetObjectItem(j, "APP_VER");
      if(cJSON_IsString(ver) && c->ver && c->ver_len) snprintf(c->ver, c->ver_len, "%s", ver->valuestring);
      cJSON_Delete(j);
    }
  }
  return 0;
}

int
ps5tm_library_title_info(const char *title_id, char *ver, size_t ver_len) {
  if(ver && ver_len) ver[0] = 0;
  if(!title_id || !title_id[0]) return -1;
  char err[160] = "";
  size_t size = 0;
  unsigned char *file = read_appdb(&size, err, sizeof(err));
  sqlro_table_t *tab  = malloc(sizeof(sqlro_table_t));
  sqlro_db_t     db;
  int rc = -1;
  if(file && tab && sqlro_open_mem(&db, file, size) == 0 && sqlro_find_table(&db, "tbl_contentinfo", tab) == 0) {
    tinfo_ctx_t c = { title_id, ver, ver_len, 0, sqlro_col(tab, "titleId"), sqlro_col(tab, "AppInfoJson") };
    if(c.c_tid >= 0 && sqlro_scan(&db, tab, tinfo_row, &c) >= 0) rc = c.found ? 1 : 0;
  }
  free(tab);
  free(file);
  if(rc < 0) lookup_failed("die Version eines Titels", err);
  return rc;
}

/* Is a content id (a DLC's, for one) listed in the app database? 1, 0, or -1 when the database cannot be read. */
typedef struct { const char *cid; int found, c_cid; } cinfo_ctx_t;

static int
cinfo_row(void *ctx, const sqlro_table_t *t, const sqlro_row_t *r) {
  cinfo_ctx_t *c = ctx;
  (void)t;
  char cid[64];
  if(sqlro_text(r, c->c_cid, cid, sizeof(cid)) == 0 && !strcmp(cid, c->cid)) { c->found = 1; return 1; }
  return 0;
}

int
ps5tm_library_content_listed(const char *content_id) {
  if(!content_id || !content_id[0]) return -1;
  char err[160] = "";
  size_t size = 0;
  unsigned char *file = read_appdb(&size, err, sizeof(err));
  sqlro_table_t *tab  = malloc(sizeof(sqlro_table_t));
  sqlro_db_t     db;
  int rc = -1;
  if(file && tab && sqlro_open_mem(&db, file, size) == 0 && sqlro_find_table(&db, "tbl_contentinfo", tab) == 0) {
    cinfo_ctx_t c = { content_id, 0, sqlro_col(tab, "contentId") };
    if(c.c_cid >= 0 && sqlro_scan(&db, tab, cinfo_row, &c) >= 0) rc = c.found;
  }
  free(tab);
  free(file);
  if(rc < 0) lookup_failed("ein Zusatzinhalt", err);
  return rc;
}


static const char *
platform_name(int p) {
  return p == 0 ? "PS5" : p == 1 ? "PS4" : "";
}

struct cJSON *
ps5tm_library_json(void) {
  ensure_fresh(1);
  pthread_mutex_lock(&g_lock);

  cJSON *root = cJSON_CreateObject();
  cJSON_AddBoolToObject(root, "ok", g_state == 1);
  if(g_state != 1) {
    cJSON_AddStringToObject(root, "code", "library_unavailable");
    cJSON_AddStringToObject(root, "message", g_error);
    pthread_mutex_unlock(&g_lock);
    return root;
  }
  cJSON_AddStringToObject(root, "source", "app.db");
  cJSON_AddBoolToObject  (root, "from_home_screen", g_from_tiles);
  cJSON_AddBoolToObject  (root, "user_matched", g_user_matched);
  cJSON_AddNumberToObject(root, "age_s",
                          (double)((ps5tm_mono_ms() - g_read_ms) / 1000));
  if(g_error[0]) cJSON_AddStringToObject(root, "last_error", g_error);
  cJSON_AddNumberToObject(root, "count", g_count);
  if(g_truncated) cJSON_AddBoolToObject(root, "truncated", 1);
  /* For the page to compare each title's required system software with. */
  char fw[32];
  ps5tm_platform_firmware_text(ps5tm_platform_firmware(), fw, sizeof(fw));
  cJSON_AddStringToObject(root, "console_fw", fw);

  cJSON *arr = cJSON_AddArrayToObject(root, "games");
  for(int i = 0; i < g_count; i++) {
    const lib_game_t *g = &g_games[i];
    cJSON *e = cJSON_CreateObject();
    cJSON_AddStringToObject(e, "title_id", g->title_id);
    cJSON_AddStringToObject(e, "name", g->name[0] ? g->name : g->title_id);
    if(g->content_id[0]) cJSON_AddStringToObject(e, "content_id", g->content_id);
    if(g->platform >= 0) cJSON_AddStringToObject(e, "platform", platform_name(g->platform));
    if(g->version[0])    cJSON_AddStringToObject(e, "version", g->version);
    if(g->system_ver[0]) cJSON_AddStringToObject(e, "system_ver", g->system_ver);
    if(g->size >= 0)     cJSON_AddNumberToObject(e, "size_bytes", (double)g->size);
    if(g->installed[0])  cJSON_AddStringToObject(e, "installed", g->installed);
    if(g->last_played[0]) cJSON_AddStringToObject(e, "last_played", g->last_played);
    if(g->played_s >= 0) cJSON_AddNumberToObject(e, "played_s", (double)g->played_s);
    if(g->source[0])     cJSON_AddStringToObject(e, "source", g->source);
    char v[8];
    if(g->param_sdk) {
      snprintf(v, sizeof(v), "%x.%02x", (unsigned)(g->param_sdk >> 56) & 0xFF,
               (unsigned)(g->param_sdk >> 48) & 0xFF);
      cJSON_AddStringToObject(e, "sdk", v);
    }
    if(g->mods.state == MODS_UNREACHABLE) {
      cJSON *m = cJSON_AddObjectToObject(e, "mods");
      cJSON_AddStringToObject(m, "state", "unreachable");
      cJSON_AddStringToObject(m, "reason",
          !strncmp(g->source, "/mnt/shadowmnt/", 15)
            ? "Das Abbild ist gerade nicht eingehängt."
            : "Der Spielordner ist gerade nicht erreichbar.");
    } else if(g->mods.state == MODS_CHECKED) {
      const lib_mods_t *md = &g->mods;
      cJSON *m = cJSON_AddObjectToObject(e, "mods");
      cJSON_AddStringToObject(m, "state", "checked");
      cJSON_AddBoolToObject  (m, "backport", md->backport);
      cJSON_AddBoolToObject  (m, "ampr_emu", md->ampr);
      cJSON_AddBoolToObject  (m, "playgo", md->playgo);
      cJSON_AddBoolToObject  (m, "ampr_index", md->ampr_index);
      cJSON_AddBoolToObject  (m, "playgo_log", md->playgo_log);
      cJSON_AddNumberToObject(m, "backport_libs", md->extra_libs);
      if(md->eboot_sdk) {
        snprintf(v, sizeof(v), "%x.%02x", (unsigned)(md->eboot_sdk >> 24) & 0xFF,
                 (unsigned)(md->eboot_sdk >> 16) & 0xFF);
        cJSON_AddStringToObject(m, "eboot_sdk", v);
      }
      cJSON *libs = cJSON_AddArrayToObject(m, "fakelib");
      for(int k = 0; k < md->nlibs; k++)
        cJSON_AddItemToArray(libs, cJSON_CreateString(md->libs[k]));
      if(md->more_libs) cJSON_AddNumberToObject(m, "fakelib_more", md->more_libs);
    }
    cJSON_AddBoolToObject(e, "can_launch", g->deeplink[0] != 0);
    if(g->deeplink[0]) cJSON_AddStringToObject(e, "launch_url", g->deeplink);

    /* Where it lies and in what form (detect_storage), and what may be done
       with it: a folder or a known image file the app copies itself
       (gamecopy.c); moving and unpacking only through ShadowMountPlus
       (gamemove.c), for titles it manages. */
    if(g->format[0])    cJSON_AddStringToObject(e, "format", g->format);
    if(g->real_path[0]) cJSON_AddStringToObject(e, "path", g->real_path);
    int is_file = strcmp(g->format, "folder") && strcmp(g->format, "pkg") &&
                  strcmp(g->format, "image") && g->format[0];
    cJSON_AddBoolToObject(e, "smp", g->smp);
    if(g->smp) cJSON_AddBoolToObject(e, "mounted", g->smp_mounted);
    cJSON_AddBoolToObject(e, "can_copy", g->real_path[0] == '/' &&
                          (!strcmp(g->format, "folder") || is_file));
    cJSON_AddBoolToObject(e, "can_move", g->smp_can_move);
    cJSON_AddBoolToObject(e, "can_unpack", g->smp_can_unpack && is_file);
    /* Converting is the app's own (gameconvert.c): a folder becomes .exfat
       or .ffpfsc, an uncompressed image .ffpfsc. */
    cJSON_AddBoolToObject(e, "can_convert", g->real_path[0] == '/' &&
                          (!strcmp(g->format, "folder") || !strcmp(g->format, "exfat") ||
                           !strcmp(g->format, "ffpkg") || !strcmp(g->format, "ffpfs")));
    char url[64];
    snprintf(url, sizeof(url), "/api/v1/library/cover?id=%s%s%s", g->title_id,
             g->cover_ts[0] ? "&ts=" : "", g->cover_ts);
    cJSON_AddStringToObject(e, "cover", url);
    cJSON_AddItemToArray(arr, e);
  }
  pthread_mutex_unlock(&g_lock);
  return root;
}


/* Starting a title directly. Measured on 29.09.2026, FW 12.00, with two
 * throwaway test payloads under this app's own identity (authid
 * 0x4801000000000013), touching no process but their own:
 *
 *   sceLncUtilLaunchApp(id, NULL, &param)       accepted, app id 0x6018 —
 *                                               but the game stayed behind
 *                                               the home screen
 *   sceSystemServiceLaunchApp(id, NULL, &ctx)   accepted, app id 0x8018, and
 *                                               in front. The Homebrew
 *                                               Launcher's route (websrv,
 *                                               src/ps5/sys.c), same block
 *   either one without a parameter block        0x80940005, "param value is
 *                                               false"
 *   the title already running                   0x80940010
 *
 * ps5upload's notes say the launcher takes calls from ShellUI alone on these
 * firmwares; on this console that did not hold. The Homebrew Launcher also
 * closes a running game first — this never does. The same title already
 * running is brought forward through its tile link (ps5tm_library_hand_over
 * below); a different one gets the notification with a start button
 * (notify.c), and the tile link behind it leaves the decision to the shell
 * and the person at the console. */
typedef struct {
  uint32_t structsize;
  uint32_t user_id;
  uint32_t app_opt;
  uint64_t crash_report;
  uint32_t check_flag;
} launch_ctx_t;

int sceSystemServiceLaunchApp(const char *title_id, char **argv,
                              launch_ctx_t *ctx) __attribute__((weak));

int
ps5tm_library_start(const char *title_id) {
  if(!title_id || !is_title_id(title_id)) return -1;
#ifdef PS5TM_HOST_TEST
  return -1;
#else
  if(!sceSystemServiceLaunchApp) return -1;
  int uid = -1;
  if(ps5tm_user_service_active_user(&uid) != 0 || uid <= 0) return -1;

  launch_ctx_t ctx;
  memset(&ctx, 0, sizeof(ctx));
  ctx.user_id = (uint32_t)uid;
  ps5tm_sony_api_lock();
  int rc = sceSystemServiceLaunchApp(title_id, NULL, &ctx);
  ps5tm_sony_api_unlock();
  return rc;                                   /* >= 0: the new app id */
#endif
}


/* Closing the console's own browser after a start from it.
 *
 * Started from the PS5's browser, a game comes up behind it: the shell does
 * not take the screen from the page in front (measured 29.09.2026). And the
 * browser cannot be closed the way a game is. It is not an application of
 * its own but part of SceShellUI — process SceNKWebProcess, title NPXS40087,
 * spawned by the shell when the tile opens its link — so closing it like an
 * app would close the system's user interface.
 *
 * The shell is asked instead, through its own helper library, the way
 * ps5-unified-autoloader closes the browser (src/app_killer.c):
 *
 *   1. "pshomeui:navigateToHome?bootCondition=psButton": home, as if the PS
 *      button had been pressed; the shell closes the browser on the way
 *   2. the tile's own link, psgm:play?id=<title>: the shell brings the game
 *      that is already running to the front
 *
 * Measured 29.09.2026 with a throwaway payload under this app's identity:
 * Tetris started behind the browser, the controller focus stayed with the
 * shell (0x7); 0.5 s after step 1 the browser process was gone; after step 2
 * the focus was Tetris' own (0x2018). Both calls returned 0. Step 2 waits for
 * the browser to go, in the order that was measured.
 *
 * The browser goes as soon as the start has been accepted — the user asked
 * for no wait (29.09.2026; the test had waited 2 s). The short delay only
 * lets the page's answer leave first. The launch itself takes about 0.8 s,
 * so the browser is gone roughly a second and a half after "Starten".
 *
 * libSceShellUIUtil.sprx is linked, not loaded at run time — see the Makefile,
 * and tile.c on sceKernelLoadStartModule. The calls run on a thread of their
 * own: the shell may take its time, and the request must not wait for it. */
typedef struct {
  unsigned int size;
  uint32_t     user_id;
} shellui_uri_param_t;

int sceShellUIUtilInitialize(void) __attribute__((weak));
int sceShellUIUtilLaunchByUri(const char *uri, shellui_uri_param_t *param)
    __attribute__((weak));

#define HOME_URI           "pshomeui:navigateToHome?bootCondition=psButton"
#define BROWSER_PROCESS    "SceNKWebProcess"
#define HANDOVER_DELAY_MS  200
#define BROWSER_CLOSE_MS   3000

#ifndef PS5TM_HOST_TEST
typedef struct {
  char     title_id[12];
  uint32_t user_id;
  int      close_browser;
} handover_t;

static int g_handover_busy;             /* one hand-over at a time         */
static int g_shellui_ready;             /* handover_thread only, see above */

static void *
handover_thread(void *arg) {
  handover_t h = *(handover_t *)arg;
  free(arg);

  usleep(HANDOVER_DELAY_MS * 1000);

  if(!g_shellui_ready) {
    int rc = sceShellUIUtilInitialize();
    if(rc != 0)
      PS5TM_WARN("shellui_init", "sceShellUIUtilInitialize ergab 0x%08X.",
                 (unsigned)rc);
    g_shellui_ready = 1;
  }

  shellui_uri_param_t prm = { sizeof(prm), h.user_id };
  int home = 0, closed_ms = -1;
  if(h.close_browser) {
    home = sceShellUIUtilLaunchByUri(HOME_URI, &prm);
    for(int t = 0; t <= BROWSER_CLOSE_MS; t += 100) {
      if(ps5tm_procmgr_pid_by_name(BROWSER_PROCESS) == -1) { closed_ms = t; break; }
      usleep(100 * 1000);
    }
  }

  char link[32];
  snprintf(link, sizeof(link), "psgm:play?id=%s", h.title_id);
  int front = sceShellUIUtilLaunchByUri(link, &prm);

  if(h.close_browser && closed_ms >= 0)
    PS5TM_INFO("library_handover", "Browser der Konsole nach %d ms geschlossen "
               "(0x%08X), %s nach vorn geholt (0x%08X).", closed_ms,
               (unsigned)home, h.title_id, (unsigned)front);
  else if(h.close_browser)
    PS5TM_WARN("library_handover", "Browser der Konsole nach %d ms noch offen "
               "(0x%08X), %s nach vorn geholt (0x%08X).", BROWSER_CLOSE_MS,
               (unsigned)home, h.title_id, (unsigned)front);
  else
    PS5TM_INFO("library_handover", "%s nach vorn geholt (0x%08X).",
               h.title_id, (unsigned)front);

  __atomic_store_n(&g_handover_busy, 0, __ATOMIC_RELEASE);
  return NULL;
}
#endif

int
ps5tm_library_hand_over(const char *title_id, int close_browser) {
  if(!title_id || !is_title_id(title_id)) return -1;
#ifdef PS5TM_HOST_TEST
  (void)close_browser;
  return -1;
#else
  if(!sceShellUIUtilInitialize || !sceShellUIUtilLaunchByUri) return -1;
  int uid = -1;
  if(ps5tm_user_service_active_user(&uid) != 0 || uid <= 0) return -1;

  if(__atomic_exchange_n(&g_handover_busy, 1, __ATOMIC_ACQ_REL)) return -1;

  handover_t *h = calloc(1, sizeof(*h));
  if(!h) {
    __atomic_store_n(&g_handover_busy, 0, __ATOMIC_RELEASE);
    return -1;
  }
  snprintf(h->title_id, sizeof(h->title_id), "%s", title_id);
  h->user_id       = (uint32_t)uid;
  h->close_browser = close_browser;

  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
  pthread_t th;
  int rc = pthread_create(&th, &attr, handover_thread, h);
  pthread_attr_destroy(&attr);
  if(rc != 0) {
    free(h);
    __atomic_store_n(&g_handover_busy, 0, __ATOMIC_RELEASE);
    return -1;
  }
  return 0;
#endif
}


/* For the start button: the name and the tile's own link, for a title on the
   list only — the link ends up in a notification button on the console. */
int
ps5tm_library_launch_info(const char *title_id, char *name, size_t name_len,
                          char *link, size_t link_len) {
  if(!title_id || !is_title_id(title_id) || !name || !link) return -1;
  ensure_fresh(0);
  pthread_mutex_lock(&g_lock);
  int rc = -1;
  for(int i = 0; i < g_count; i++) {
    const lib_game_t *g = &g_games[i];
    if(strcmp(g->title_id, title_id)) continue;
    if(g->deeplink[0]) {
      snprintf(name, name_len, "%s", g->name[0] ? g->name : g->title_id);
      snprintf(link, link_len, "%s", g->deeplink);
      rc = 0;
    }
    break;
  }
  pthread_mutex_unlock(&g_lock);
  return rc;
}


/* For copying: the name and the folder the title runs from, for a title on
   the list only. The folder never comes from the request. */
int
ps5tm_library_copy_info(const char *title_id, char *name, size_t name_len,
                        char *source, size_t source_len) {
  if(!title_id || !is_title_id(title_id) || !name || !source) return -1;
  ensure_fresh(0);
  pthread_mutex_lock(&g_lock);
  int rc = -1;
  for(int i = 0; i < g_count; i++) {
    const lib_game_t *g = &g_games[i];
    if(strcmp(g->title_id, title_id)) continue;
    if(g->real_path[0] == '/' && strcmp(g->format, "pkg") &&
       strcmp(g->format, "image")) {
      snprintf(name, name_len, "%s", g->name[0] ? g->name : g->title_id);
      snprintf(source, source_len, "%s", g->real_path);
      rc = 0;
    }
    break;
  }
  pthread_mutex_unlock(&g_lock);
  return rc;
}


/* For deleting (gamedelete.c): what the title is and where it lies, for a title on the list only. format as on
   the list ("pkg" for what the system installed), smp when ShadowMountPlus mounts it (then path is its image or
   folder), size -1 when not known. */
int
ps5tm_library_delete_info(const char *title_id, ps5tm_libdel_t *out) {
  if(!title_id || !is_title_id(title_id) || !out) return -1;
  memset(out, 0, sizeof(*out));
  ensure_fresh(0);
  pthread_mutex_lock(&g_lock);
  int rc = -1;
  for(int i = 0; i < g_count; i++) {
    const lib_game_t *g = &g_games[i];
    if(strcmp(g->title_id, title_id)) continue;
    snprintf(out->name, sizeof(out->name), "%s", g->name[0] ? g->name : g->title_id);
    snprintf(out->format, sizeof(out->format), "%s", g->format);
    snprintf(out->path, sizeof(out->path), "%s", g->real_path);
    snprintf(out->version, sizeof(out->version), "%s", g->version);
    out->smp = g->smp;
    out->size = g->size;
    rc = 0;
    break;
  }
  pthread_mutex_unlock(&g_lock);
  return rc;
}


/* The cover as the database names it, else where the system keeps covers — or, with the cache
   on, the copy libcache.c made of it, which is there when the system's file is not.
   Only titles on the list: the route serves nothing else. */
int
ps5tm_library_cover(const char *title_id, char *path, size_t path_len) {
  if(!title_id || !is_title_id(title_id) || !path || path_len < 64) return -1;
  path[0] = 0;

  ensure_fresh(0);
  char db_path[sizeof(g_games[0].cover)] = "", ts[sizeof(g_games[0].cover_ts)] = "";
  pthread_mutex_lock(&g_lock);
  int found = 0;
  for(int i = 0; i < g_count; i++) {
    if(strcmp(g_games[i].title_id, title_id)) continue;
    found = 1;
    snprintf(db_path, sizeof(db_path), "%s", g_games[i].cover);
    snprintf(ts, sizeof(ts), "%s", g_games[i].cover_ts);
    break;
  }
  pthread_mutex_unlock(&g_lock);
  if(!found) return -1;

  int cache_on = ps5tm_libcache_enabled();
  if(cache_on && ps5tm_libcache_cover_get(title_id, ts, path, path_len) == 0) return 0;
  path[0] = 0;

  int rc = cover_source(db_path, title_id, path, path_len);
  if(rc == 0 && cache_on) ps5tm_libcache_cover_put(title_id, ts, path);
  return rc;
}
