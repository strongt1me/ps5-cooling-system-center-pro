/* Home-screen tile: puts the launcher on the console's home screen.
 *
 * The tile holds no program. It is a title the system lists in its media row
 * whose param.json carries a deeplinkUri, so that selecting it opens this
 * app's web interface in the console's browser. There are two ways to get one
 * onto a console, and this file knows both.
 *
 * 1. As a folder of files. No second payload, no package.
 *
 *    The few files a launcher tile consists of — param.json, icon0.png, the
 *    pictures — are embedded in this ELF (tile_files.h). They are written to
 *    /user/app/<id>/sce_sys, and the system is asked to register that folder:
 *    sceAppInstUtilAppInstallTitleDir(), or, where a firmware does not export
 *    it, the batch call sceAppInstUtilAppInstallAll(). That is how the other
 *    launchers on a console (Payload Manager, BFpilot, ShadowMount+'s own
 *    tile) got there, and how John Törnblom's ftpsrv installer does it from a
 *    single ELF: the same calls on the same layout. It is some 1.7 MB instead
 *    of an 8.2 MB package, which is the only reason this fits into the app.
 *
 * 2. As a package, the way up to 1.46.0 and still here as a fallback.
 *
 *    sceAppInstUtilAppInstallPkg() takes a .pkg path. Embedded in the app the
 *    package left no heap for the web server, so it lives in the separate
 *    installer payload, or on disk where the app finds it.
 *
 * The ABI shapes of the package call are the ones Elf Arsenal's installer
 * uses; they must match byte-for-byte or sceAppInstUtilAppInstallPkg corrupts
 * its arguments.
 */

/* No module is ever loaded at run time.
 *
 * sceKernelLoadStartModule() takes the runtime linker's lock, and on FW 12.00
 * it does not always come back. That lock is process-wide, so a single stuck
 * call froze every other thread the moment it needed a symbol resolved — the
 * web server answered exactly one request and then went silent. Moving the
 * call to a background thread did not help, because the lock is shared.
 *
 * Only libraries the ELF is linked against are used, and only through dlsym.
 * A symbol that is not there leaves a null pointer and the feature reports
 * nothing, which is a far better failure than a dead dashboard. */

#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "dynsym.h"
#include "ps5tm.h"
#include "tile_files.h"
#include "tile_pkg.h"

/* Host tests point every path at a scratch folder. */
#ifndef PS5TM_TILE_ROOT
#define PS5TM_TILE_ROOT ""
#endif

#define PS5TM_TITLE_ID   "PSCC69690"
#define PS5TM_APP_ROOT   PS5TM_TILE_ROOT "/user/app"
#define PS5TM_APP_DIR    PS5TM_APP_ROOT "/" PS5TM_TITLE_ID
#define PS5TM_SCE_SYS    PS5TM_APP_DIR "/sce_sys"
#define PS5TM_STAGED_PKG PS5TM_DATA_DIR "/tile.pkg"

/* What the system is given to register: the folder that holds <id>/sce_sys.
   The real one, whatever a test does to the paths above. */
#define PS5TM_INSTALL_DIR "/user/app/"

/* Two small notes in our own folder, both about the folder way.
 *
 * tile-staging exists from the moment the files are written until the system
 * has taken them. While it does, the tile does not count as installed even
 * though /user/app/<id> is there — the files of a registration that failed
 * must not pass for a finished tile, or it would never be tried again.
 *
 * tile-auto says what became of the one automatic attempt at start-up:
 * "ok" (the tile was put there; if it is gone, someone removed it, so it is
 * not put back by itself) or "fail <version>" (that version tried and did not
 * get there, or never came back from the system call; a newer version tries
 * once more). It is written BEFORE the call, as "fail", so a call that never
 * returns, or a console that is switched off in the middle of it, cannot
 * repeat itself at every start. */
#define PS5TM_STAGING_MARK PS5TM_DATA_DIR "/tile-staging"
#define PS5TM_AUTO_NOTE    PS5TM_DATA_DIR "/tile-auto"

/* The system's answer for "this title is already registered": fine. */
#define INST_ALREADY 0x80990002u

/* Where we look for the launcher package, in priority order. The first entry
   is where the built-in copy is unpacked to. */
static const char *k_pkg_paths[] = {
  PS5TM_STAGED_PKG,
  "/data/PS5_Cooling_Center.pkg",
  "/mnt/usb0/PS5_Cooling_Center.pkg",
  "/mnt/usb1/PS5_Cooling_Center.pkg",
};

#define CONTENTID_SIZE 0x30

typedef struct {
  char content_id[CONTENTID_SIZE];
  int  content_type;
  int  content_platform;
} pkg_info_t;

/* Looked up, not linked — the rule stated at the top of this file applies to
   these as much as to anything else. They used to be plain externs, which is a
   strong reference: if the console's real .sprx does not export one of them,
   rtld gives up before main() and the payload dies without a word. The library
   stays on the link line so it is resident for dlsym to search. */
typedef int (*fn_inst_init_t)(void);
typedef int (*fn_inst_pkg_t)(const char *path, pkg_info_t *pkg_info);
typedef int (*fn_inst_dir_t)(const char *title_id, const char *dir,
                             void *reserved);
typedef int (*fn_inst_all_t)(void *reserved);

static fn_inst_init_t p_inst_init = NULL;
static fn_inst_pkg_t  p_inst_pkg  = NULL;
static fn_inst_dir_t  p_inst_dir  = NULL;
static fn_inst_all_t  p_inst_all  = NULL;
static int            g_resolved  = 0;

static void
resolve_appinst(void) {
  if(g_resolved) return;
  g_resolved = 1;

  const char *AIU = "libSceAppInstUtil.sprx";

  p_inst_init = (fn_inst_init_t)ps5tm_dynsym(AIU, "sceAppInstUtilInitialize");
  p_inst_pkg  = (fn_inst_pkg_t) ps5tm_dynsym(AIU, "sceAppInstUtilAppInstallPkg");
  p_inst_dir  = (fn_inst_dir_t) ps5tm_dynsym(AIU, "sceAppInstUtilAppInstallTitleDir");
  p_inst_all  = (fn_inst_all_t) ps5tm_dynsym(AIU, "sceAppInstUtilAppInstallAll");

  if(!p_inst_init || (!p_inst_dir && !p_inst_all && !p_inst_pkg))
    PS5TM_WARN("tile_symbols_missing",
               "Installationsdienst nicht ansprechbar (Initialize %s, "
               "Titelordner %s, Sammelinstallation %s, Paket %s) — die Kachel "
               "lässt sich aus der App heraus nicht installieren; nutze den "
               "Installer-Payload.",
               p_inst_init ? "da" : "fehlt", p_inst_dir ? "da" : "fehlt",
               p_inst_all ? "da" : "fehlt", p_inst_pkg ? "da" : "fehlt");
  else
    PS5TM_INFO("tile_symbols",
               "Installationsdienst gefunden (Initialize %s, Titelordner %s, "
               "Sammelinstallation %s, Paket %s).",
               p_inst_init ? "ja" : "nein", p_inst_dir ? "ja" : "nein",
               p_inst_all ? "ja" : "nein", p_inst_pkg ? "ja" : "nein");
}

#ifdef PS5TM_HOST_TEST
/* A test stands in for the system's installer. */
void
ps5tm_tile_test_hooks(fn_inst_init_t init, fn_inst_dir_t dir, fn_inst_all_t all,
                      fn_inst_pkg_t pkg) {
  p_inst_init = init;
  p_inst_dir  = dir;
  p_inst_all  = all;
  p_inst_pkg  = pkg;
  g_resolved  = 1;
}
#endif


const char *
ps5tm_tile_state_name(ps5tm_tile_state_t state) {
  switch(state) {
    case PS5TM_TILE_NOT_INSTALLED:       return "not_installed";
    case PS5TM_TILE_INSTALLING:          return "installing";
    case PS5TM_TILE_INSTALLED:           return "installed";
    case PS5TM_TILE_ADAPTER_UNAVAILABLE: return "adapter_unavailable";
    case PS5TM_TILE_ERROR:               return "error";
    default:                             return "unknown";
  }
}


/* Writes the copy built into this ELF out to disk, because the installer
   service takes a path, not a buffer. Returns that path, or NULL when this
   build carries no package. */
static const char *
unpack_embedded(void) {
  if(ps5tm_tile_pkg_len == 0) return NULL;

  struct stat st;
  if(stat(PS5TM_STAGED_PKG, &st) == 0 &&
     (unsigned long)st.st_size == ps5tm_tile_pkg_len)
    return PS5TM_STAGED_PKG;               /* already unpacked, same size */

  mkdir(PS5TM_DATA_DIR, 0755);

  /* Write under a temporary name and rename, so an interrupted run cannot
     leave a truncated package for the installer to choke on. */
  const char *tmp = PS5TM_STAGED_PKG ".part";
  FILE *f = fopen(tmp, "wb");
  if(!f) {
    PS5TM_WARN("tile_unpack_failed", "%s ist nicht beschreibbar.", tmp);
    return NULL;
  }

  size_t written = fwrite(ps5tm_tile_pkg, 1, ps5tm_tile_pkg_len, f);
  int    flushed = (fflush(f) == 0);
  fclose(f);

  if(written != ps5tm_tile_pkg_len || !flushed) {
    PS5TM_WARN("tile_unpack_short",
               "Kachel-Paket unvollständig geschrieben (%zu von %zu Bytes) — "
               "ist die Konsole voll?", written, (size_t)ps5tm_tile_pkg_len);
    unlink(tmp);
    return NULL;
  }

  unlink(PS5TM_STAGED_PKG);
  if(rename(tmp, PS5TM_STAGED_PKG) != 0) {
    unlink(tmp);
    return NULL;
  }

  PS5TM_INFO("tile_unpacked", "Mitgeliefertes Kachel-Paket bereitgelegt (%zu KiB).",
             (size_t)(ps5tm_tile_pkg_len / 1024));
  return PS5TM_STAGED_PKG;
}


/* What a PS5 package starts with: 7F 'F' 'I' 'H'. The tile package built here
   does (checked on the file in pkg/out); a PS4 package starts 7F 'C' 'N' 'T'. */
static const unsigned char k_pkg_magic[4] = { 0x7f, 'F', 'I', 'H' };

/* Nothing smaller than this can be a package: the header alone is larger. */
#define PKG_MIN_BYTES 4096

/* Whether the file at `path` can be a PS5 package at all: a regular file of a
 * plausible size that starts with the package magic. Before this, any file
 * with at least one byte was handed to the installer service — a package cut
 * off by an FTP transfer, a page of HTML saved under the right name — and what
 * came back was an error code nobody could read, or nothing but a tile that
 * never appeared. 0 when it passes; otherwise `why` says what is wrong, in a
 * form fit to put after "unbrauchbar". */
static int
pkg_check(const char *path, char *why, size_t why_len) {
  struct stat st;
  if(stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
    snprintf(why, why_len, "keine gewöhnliche Datei");
    return -1;
  }
  if(st.st_size < PKG_MIN_BYTES) {
    snprintf(why, why_len, "%lld Bytes sind zu wenig für ein Paket",
             (long long)st.st_size);
    return -1;
  }

  unsigned char head[sizeof(k_pkg_magic)];
  FILE *f = fopen(path, "rb");
  size_t got = f ? fread(head, 1, sizeof(head), f) : 0;
  if(f) fclose(f);
  if(got != sizeof(head)) {
    snprintf(why, why_len, "nicht lesbar");
    return -1;
  }
  if(memcmp(head, k_pkg_magic, sizeof(k_pkg_magic)) != 0) {
    snprintf(why, why_len, "kein PS5-Paket (die Kennung 7F 46 49 48 fehlt)");
    return -1;
  }
  return 0;
}


/* The package to install: the built-in copy when there is one, else the first
 * of the usual places that holds something usable. A file that is there but
 * fails pkg_check() is passed over — the next place may hold a good one — and
 * if nothing passes, `why` (when given) names the first that was refused, so
 * the caller can say "unbrauchbar" instead of "nicht gefunden". It is empty
 * when there was nothing at all. */
static const char *
find_pkg(char *why, size_t why_len) {
  if(why && why_len) why[0] = '\0';

  char bad[160], first_bad[192] = "";
  const char *embedded = unpack_embedded();
  if(embedded && pkg_check(embedded, bad, sizeof(bad)) == 0) return embedded;
  if(embedded) snprintf(first_bad, sizeof(first_bad), "%s: %s", embedded, bad);

  for(unsigned i = 0; i < sizeof(k_pkg_paths) / sizeof(k_pkg_paths[0]); i++) {
    struct stat st;
    if(stat(k_pkg_paths[i], &st) != 0 || !S_ISREG(st.st_mode)) continue;
    if(pkg_check(k_pkg_paths[i], bad, sizeof(bad)) == 0) return k_pkg_paths[i];
    if(!first_bad[0])
      snprintf(first_bad, sizeof(first_bad), "%s: %s", k_pkg_paths[i], bad);
  }

  if(why && why_len) snprintf(why, why_len, "%s", first_bad);
  return NULL;
}


/* Where an installed title leaves traces.
 *
 * /user/app/<ID> alone was not enough: on the test console a tile was plainly
 * visible in the home screen while that directory did not exist, so the app
 * announced "no tile" and offered to install one that was already there. The
 * metadata directory is written for every title installed from a package
 * whatever storage its data ended up on, which makes it the better witness.
 * A tile registered from a folder has no metadata directory at all (the other
 * launchers on the test console have none), so there /user/app/<ID> is the
 * witness again — which is why the staging note below must be able to veto it. */
static const char *k_tile_marks[] = {
  PS5TM_TILE_ROOT "/system_data/priv/appmeta/" PS5TM_TITLE_ID,
  PS5TM_APP_DIR,
  PS5TM_TILE_ROOT "/user/appmeta/" PS5TM_TITLE_ID,
  PS5TM_TILE_ROOT "/mnt/ext1/user/app/" PS5TM_TITLE_ID,
};

static int
tile_installed(void) {
  struct stat st;
  /* Files written, registration not (yet) accepted: not a tile. */
  if(stat(PS5TM_STAGING_MARK, &st) == 0) return 0;

  for(unsigned i = 0; i < sizeof(k_tile_marks) / sizeof(k_tile_marks[0]); i++)
    if(stat(k_tile_marks[i], &st) == 0 && S_ISDIR(st.st_mode)) return 1;
  return 0;
}


/* sceAppInstUtilInitialize() can block forever when the IPMI service is not
   up yet, so it runs on its own thread with a deadline (same guard the
   reference DPI payload uses). */
static volatile int g_init_done = 0;
static volatile int g_init_rc   = -1;
static int          g_init_started = 0;

static void *
init_thread(void *arg) {
  (void)arg;
  g_init_rc   = p_inst_init();
  g_init_done = 1;
  return NULL;
}

/* Distinct from any Sony error code, so the caller can say something useful
   instead of printing a number nobody can look up. */
#define TILE_NO_SYMBOLS (-0xBADD)

static int
timed_init(void) {
  if(g_init_done) return g_init_rc;

  resolve_appinst();
  if(!p_inst_init || (!p_inst_dir && !p_inst_all && !p_inst_pkg))
    return TILE_NO_SYMBOLS;

  if(!g_init_started) {
    /* Linking does not guarantee the module is resident. etaHEN's package
       write-up is explicit that it must be started before Initialize(). */

    pthread_t tid;
    if(pthread_create(&tid, NULL, init_thread, NULL) != 0) return -1;
    /* Only once a thread exists. Set before the call, a failed pthread_create
       (the console is short of memory) left this at 1 with nobody to answer:
       every later attempt waited out the full ten seconds and timed out, until
       the app was restarted. */
    g_init_started = 1;
    pthread_detach(tid);
  }

  for(int i = 0; i < 100; i++) {          /* up to 10 s */
    if(g_init_done) return g_init_rc;
    struct timespec ts = { 0, 100000000 };
    nanosleep(&ts, NULL);
  }
  return -0xDEAD;                          /* timed out */
}


/* --------------------------------------------------- the tile as a folder */

static int
rc_accepted(int rc) {
  return rc == 0 || (unsigned)rc == INST_ALREADY;
}

/* Writes `len` bytes to `path` through a temporary name: a run that is cut off
   leaves nothing the system could take for a finished file. */
static int
write_file(const char *path, const unsigned char *data, unsigned long len) {
  char tmp[512];
  if(snprintf(tmp, sizeof(tmp), "%s.part", path) >= (int)sizeof(tmp)) {
    errno = ENAMETOOLONG;
    return -1;
  }
  int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if(fd < 0) return -1;

  unsigned long off = 0;
  int saved = 0;
  while(off < len) {
    ssize_t n = write(fd, data + off, len - off);
    if(n < 0) {
      if(errno == EINTR) continue;
      saved = errno;
      break;
    }
    if(n == 0) { saved = EIO; break; }
    off += (unsigned long)n;
  }
  if(saved == 0 && fsync(fd) != 0) saved = errno;
  if(close(fd) != 0 && saved == 0) saved = errno;
  if(saved == 0 && rename(tmp, path) != 0) saved = errno;
  if(saved != 0) {
    unlink(tmp);
    errno = saved;
    return -1;
  }
  return 0;
}

static int
make_dir(const char *path) {
  if(mkdir(path, 0755) == 0) return 0;
  if(errno != EEXIST) return -1;
  struct stat st;
  return stat(path, &st) == 0 && S_ISDIR(st.st_mode) ? 0 : -1;
}

/* A note is a short text file under our own folder. */
static int
note_write(const char *path, const char *text) {
  mkdir(PS5TM_DATA_DIR, 0755);
  return write_file(path, (const unsigned char *)text, (unsigned long)strlen(text));
}

/* Takes away what stage_tile() wrote, and nothing else: a folder that holds
   anything of someone else's stays. */
static void
unstage_tile(void) {
  for(unsigned i = 0; i < ps5tm_tile_file_count; i++) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", PS5TM_SCE_SYS, ps5tm_tile_files[i].name);
    unlink(path);
    strncat(path, ".part", sizeof(path) - strlen(path) - 1);
    unlink(path);
  }
  rmdir(PS5TM_SCE_SYS);
  rmdir(PS5TM_APP_DIR);
  unlink(PS5TM_STAGING_MARK);
}

/* Writes the embedded files to /user/app/<id>/sce_sys. The staging note goes
   first. If a file cannot be written, what was written is taken away again: a
   half-made folder is the one thing that must not stay behind. A registration
   that fails later leaves the finished files where they are instead — they are
   whole, the next try writes them over, and the system may even take them by
   itself at its next batch install. */
static int
stage_tile(char *why, size_t why_len) {
  if(note_write(PS5TM_STAGING_MARK, PS5TM_TITLE_ID "\n") != 0) {
    snprintf(why, why_len, "%s: %s", PS5TM_STAGING_MARK, strerror(errno));
    return -1;
  }
  if(make_dir(PS5TM_APP_ROOT) != 0 || make_dir(PS5TM_APP_DIR) != 0 ||
     make_dir(PS5TM_SCE_SYS) != 0) {
    snprintf(why, why_len, "Ordner unter %s lässt sich nicht anlegen: %s",
             PS5TM_APP_ROOT, strerror(errno));
    unstage_tile();
    return -1;
  }
  for(unsigned i = 0; i < ps5tm_tile_file_count; i++) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", PS5TM_SCE_SYS, ps5tm_tile_files[i].name);
    if(write_file(path, ps5tm_tile_files[i].data, ps5tm_tile_files[i].len) != 0) {
      snprintf(why, why_len, "%s: %s", path, strerror(errno));
      unstage_tile();
      return -1;
    }
  }
  return 0;
}

/* Distinct from any Sony code: the batch call was not made, see below. */
#define TILE_BATCH_REFUSED (-0xBA7C)

/* The batch call registers every title that waits under /user/app, not only
 * ours. On 03.10.2026 it registered Arkanoid along with the tile: the console's
 * mounter (ShadowMountPlus) had staged that game the evening before and failed
 * to register it — 1029 times "retry limit reached" in its log — and the
 * message stopped exactly with our call. So the batch call is made only when
 * nothing else is waiting: every other folder there that holds the file a
 * registration needs (sce_sys/param.json) must be a title the app database
 * already lists. Empty leftovers of removed titles cannot be registered and do
 * not count; the test console had five of them. When the database cannot be
 * read, nothing is known and the call is not made either. 1 when the call
 * would take only the tile, else 0 with `why` saying what stands in the way. */
#define BATCH_MAX_IDS 2048

static int
batch_reach_ok(char *why, size_t why_len) {
  static char ids[BATCH_MAX_IDS][12];
  int n = ps5tm_library_installed_ids(ids, BATCH_MAX_IDS);
  if(n < 0) {
    snprintf(why, why_len, "die App-Datenbank der Konsole ließ sich nicht lesen");
    return 0;
  }
  if(n > BATCH_MAX_IDS) {
    snprintf(why, why_len, "die App-Datenbank nennt mehr als %d Titel",
             BATCH_MAX_IDS);
    return 0;
  }

  static const char *const roots[] = {
    PS5TM_APP_ROOT,
    PS5TM_TILE_ROOT "/mnt/ext0/user/app",
    PS5TM_TILE_ROOT "/mnt/ext1/user/app",
  };
  for(unsigned r = 0; r < sizeof(roots) / sizeof(roots[0]); r++) {
    DIR *d = opendir(roots[r]);
    if(!d) continue;
    struct dirent *e;
    while((e = readdir(d))) {
      const char *name = e->d_name;
      if(strlen(name) != 9 || !strcmp(name, PS5TM_TITLE_ID)) continue;

      char param[512];
      struct stat st;
      snprintf(param, sizeof(param), "%s/%s/sce_sys/param.json", roots[r], name);
      if(stat(param, &st) != 0 || !S_ISREG(st.st_mode)) continue;

      int listed = 0;
      for(int i = 0; i < n && !listed; i++) listed = !strcmp(ids[i], name);
      if(!listed) {
        snprintf(why, why_len, "%s wartet unter %s ebenfalls auf eine "
                 "Anmeldung", name, roots[r]);
        closedir(d);
        return 0;
      }
    }
    closedir(d);
  }
  return 1;
}

/* Asks the system to register what stage_tile() wrote. 0 when it took it
   (also when it already had), else the system's error code, or
   TILE_BATCH_REFUSED with `why` filled in. The per-title call first; a firmware
   that does not export it, or one that refuses it, gets the batch call — but
   only when batch_reach_ok() says it would take nothing but the tile. */
static int
register_tile(const char **how, char *why, size_t why_len) {
  int rc = TILE_NO_SYMBOLS;

  if(p_inst_dir) {
    rc = p_inst_dir(PS5TM_TITLE_ID, PS5TM_INSTALL_DIR, NULL);
    PS5TM_INFO("tile_register_dir",
               "Anmeldung des Titelordners %s: 0x%08X.", PS5TM_TITLE_ID,
               (unsigned)rc);
    if(rc_accepted(rc)) { *how = "Titelordner"; return 0; }
  }
  if(p_inst_all) {
    if(!batch_reach_ok(why, why_len)) {
      PS5TM_WARN("tile_batch_refused",
                 "Sammelinstallation nicht aufgerufen: %s.", why);
      return TILE_BATCH_REFUSED;
    }
    int rc_all = p_inst_all(NULL);
    PS5TM_INFO("tile_register_all", "Sammelinstallation: 0x%08X.", (unsigned)rc_all);
    if(rc_accepted(rc_all)) { *how = "Sammelinstallation"; return 0; }
    if(rc == TILE_NO_SYMBOLS) rc = rc_all;
  }
  return rc;
}

/* The state of the one automatic attempt, see the top of the file. */
static void
auto_note(int ok) {
  char text[64];
  if(ok) snprintf(text, sizeof(text), "ok\n");
  else   snprintf(text, sizeof(text), "fail %s\n", PS5TM_VERSION);
  if(note_write(PS5TM_AUTO_NOTE, text) != 0)
    PS5TM_WARN("tile_note_failed", "%s ließ sich nicht schreiben: %s",
               PS5TM_AUTO_NOTE, strerror(errno));
}

static int
auto_allowed(void) {
  FILE *f = fopen(PS5TM_AUTO_NOTE, "r");
  if(!f) return 1;
  char line[64] = "";
  if(!fgets(line, sizeof(line), f)) line[0] = '\0';
  fclose(f);

  if(!strncmp(line, "ok", 2)) return 0;
  if(!strncmp(line, "fail ", 5)) {
    char *end = strchr(line, '\n');
    if(end) *end = '\0';
    return strcmp(line + 5, PS5TM_VERSION) != 0;
  }
  return 1;
}

/* The folder way, from "nothing there" to "the system has it". Returns
   PS5TM_TILE_INSTALLING when the system took the tile, otherwise a state and a
   message that says why. */
static ps5tm_tile_state_t
ensure_folder(char *msg, size_t msg_len) {
  int init_rc = timed_init();
  if(init_rc != 0) {
    if(init_rc == TILE_NO_SYMBOLS)
      snprintf(msg, msg_len,
               "Der Installationsdienst ist auf dieser Firmware nicht "
               "ansprechbar.");
    else if(init_rc == -0xDEAD)
      snprintf(msg, msg_len,
               "AppInstUtil antwortet nicht (Zeitüberschreitung). "
               "Ist der Jailbreak vollständig geladen?");
    else
      snprintf(msg, msg_len,
               "AppInstUtil konnte nicht initialisiert werden (0x%08X).",
               (unsigned)init_rc);
    PS5TM_ERROR("tile_provider_pending", "%s", msg);
    return PS5TM_TILE_ADAPTER_UNAVAILABLE;
  }

  /* timed_init() has resolved the calls by now. A firmware that offers only
     the package call has nothing to register a folder with: say so before any
     file is written. */
  if(!p_inst_dir && !p_inst_all) {
    snprintf(msg, msg_len,
             "Diese Firmware bietet die Anmeldung eines Kachel-Ordners nicht "
             "an.");
    PS5TM_WARN("tile_folder_unavailable", "%s", msg);
    return PS5TM_TILE_ADAPTER_UNAVAILABLE;
  }

  char why[256];

  /* Only the batch call (FW 12.00): find out before writing anything whether
     it would take nothing but the tile. */
  if(!p_inst_dir && !batch_reach_ok(why, sizeof(why))) {
    snprintf(msg, msg_len,
             "Die Kachel wurde nicht angelegt: Auf dieser Firmware ginge das "
             "nur über die Sammelanmeldung, und die hätte mehr erfasst — %s.",
             why);
    PS5TM_WARN("tile_batch_refused", "%s", msg);
    return PS5TM_TILE_ADAPTER_UNAVAILABLE;
  }

  if(stage_tile(why, sizeof(why)) != 0) {
    snprintf(msg, msg_len, "Die Kachel-Dateien ließen sich nicht ablegen (%s).", why);
    PS5TM_ERROR("tile_stage_failed", "%s", msg);
    return PS5TM_TILE_ERROR;
  }
  PS5TM_INFO("tile_staged", "Kachel-Dateien nach %s geschrieben (%u Dateien).",
             PS5TM_SCE_SYS, ps5tm_tile_file_count);

  const char *how = "";
  int rc = register_tile(&how, why, sizeof(why));
  if(rc == TILE_BATCH_REFUSED) {
    snprintf(msg, msg_len,
             "Die Kachel wurde nicht angemeldet: Die Sammelanmeldung hätte "
             "mehr erfasst — %s.", why);
    PS5TM_ERROR("tile_register_failed", "%s", msg);
    return PS5TM_TILE_ERROR;
  }
  if(rc != 0) {
    snprintf(msg, msg_len,
             "Das System hat die Kachel nicht angenommen (0x%08X).",
             (unsigned)rc);
    PS5TM_ERROR("tile_register_failed", "%s", msg);
    return PS5TM_TILE_ERROR;
  }

  unlink(PS5TM_STAGING_MARK);
  snprintf(msg, msg_len,
           "Kachel angemeldet (%s) – sie erscheint gleich im Startmenü unter "
           "Medien.", how);
  PS5TM_INFO("tile_registered", "Kachel angemeldet über %s.", how);
  return PS5TM_TILE_INSTALLING;
}


ps5tm_tile_state_t
ps5tm_tile_query(char *msg, size_t msg_len) {
  if(tile_installed()) {
    snprintf(msg, msg_len,
             "Kachel ist installiert (%s).", PS5TM_TITLE_ID);
    return PS5TM_TILE_INSTALLED;
  }

  if(ps5tm_tile_file_count > 0) {
    snprintf(msg, msg_len,
             "Kachel noch nicht installiert. Sie steckt im Programm; „Kachel "
             "installieren“ legt sie an.");
    return PS5TM_TILE_NOT_INSTALLED;
  }

  char why[192];
  const char *pkg = find_pkg(why, sizeof(why));
  if(pkg) {
    snprintf(msg, msg_len,
             "Kachel nicht installiert – Paket gefunden: %s", pkg);
    return PS5TM_TILE_NOT_INSTALLED;
  }

  if(why[0]) {
    snprintf(msg, msg_len,
             "Kachel-Paket unbrauchbar (%s). Lege ein vollständiges Paket "
             "unter %s/tile.pkg ab, dann erneut versuchen.", why,
             PS5TM_DATA_DIR);
    return PS5TM_TILE_ADAPTER_UNAVAILABLE;
  }

  snprintf(msg, msg_len,
           "Kein Kachel-Paket gefunden. Lege PS5_Temperature_Manager.pkg "
           "unter %s/tile.pkg ab, dann erneut versuchen.", PS5TM_DATA_DIR);
  return PS5TM_TILE_ADAPTER_UNAVAILABLE;
}


ps5tm_tile_state_t
ps5tm_tile_ensure(char *msg, size_t msg_len) {
  if(tile_installed()) {
    snprintf(msg, msg_len, "Kachel ist bereits installiert.");
    return PS5TM_TILE_INSTALLED;
  }

  /* The folder first. It needs nothing but this ELF. */
  char folder_msg[256] = "";
  ps5tm_tile_state_t folder_state = PS5TM_TILE_UNKNOWN;
  if(ps5tm_tile_file_count > 0) {
    folder_state = ensure_folder(msg, msg_len);
    if(folder_state == PS5TM_TILE_INSTALLING) return folder_state;
    snprintf(folder_msg, sizeof(folder_msg), "%s", msg);
  }

  /* Then a package, if one is lying around. */
  char why[192];
  const char *pkg = find_pkg(why, sizeof(why));
  if(!pkg) {
    if(folder_state != PS5TM_TILE_UNKNOWN) {
      /* The cause is the folder way's, not a missing package. */
      snprintf(msg, msg_len,
               "%s Alternativ ein Kachel-Paket unter %s/tile.pkg ablegen "
               "oder cooling-center-launcher-installer.elf senden.",
               folder_msg, PS5TM_DATA_DIR);
      return folder_state;
    }
    if(why[0]) {
      snprintf(msg, msg_len,
               "Das Kachel-Paket ist unbrauchbar (%s). Entweder "
               "cooling-center-launcher-installer.elf senden — der bringt ein "
               "vollständiges Paket mit — oder es neu unter %s/tile.pkg "
               "ablegen.", why, PS5TM_DATA_DIR);
      PS5TM_WARN("tile_package_unusable", "Kachel-Paket unbrauchbar: %s", why);
    } else {
      snprintf(msg, msg_len,
               "Kein Kachel-Paket gefunden. Entweder "
               "cooling-center-launcher-installer.elf senden — der bringt das "
               "Paket mit — oder es unter %s/tile.pkg ablegen.",
               PS5TM_DATA_DIR);
      PS5TM_WARN("tile_adapter_unavailable", "Kein Kachel-Paket gefunden.");
    }
    return PS5TM_TILE_ADAPTER_UNAVAILABLE;
  }

  /* The folder way wrote its files and could not get them registered. The
     package installs into the same /user/app/<id>, so they go first. */
  struct stat staged;
  if(folder_state != PS5TM_TILE_UNKNOWN && stat(PS5TM_STAGING_MARK, &staged) == 0) {
    unstage_tile();
    PS5TM_INFO("tile_unstaged",
               "Abgelegte Kachel-Dateien entfernt, das Paket übernimmt.");
  }

  int init_rc = timed_init();
  if(init_rc != 0 || !p_inst_pkg) {
    if(init_rc == TILE_NO_SYMBOLS || !p_inst_pkg)
      snprintf(msg, msg_len,
               "Der Installationsdienst ist auf dieser Firmware nicht "
               "ansprechbar. Nutze cooling-center-launcher-installer.elf, "
               "um die Kachel zu installieren.");
    else if(init_rc == -0xDEAD)
      snprintf(msg, msg_len,
               "AppInstUtil antwortet nicht (Zeitüberschreitung). "
               "Ist der Jailbreak vollständig geladen?");
    else
      snprintf(msg, msg_len,
               "AppInstUtil konnte nicht initialisiert werden (0x%08X).",
               (unsigned)init_rc);
    PS5TM_ERROR("tile_provider_pending", "%s", msg);
    return PS5TM_TILE_ADAPTER_UNAVAILABLE;
  }

  /* Once more, right before the call: initialising can take seconds, and a
     package uploaded or replaced in the meantime is not the one found above. */
  if(pkg_check(pkg, why, sizeof(why)) != 0) {
    snprintf(msg, msg_len,
             "Das Kachel-Paket ist unbrauchbar (%s) — es wurde nicht an den "
             "Installationsdienst übergeben.", why);
    PS5TM_ERROR("tile_package_unusable", "%s", msg);
    return PS5TM_TILE_ERROR;
  }

  pkg_info_t info;
  memset(&info, 0, sizeof(info));

  int rc = p_inst_pkg(pkg, &info);
  if(rc != 0) {
    snprintf(msg, msg_len,
             "Installation abgelehnt (0x%08X). Paket: %s", (unsigned)rc, pkg);
    PS5TM_ERROR("tile_install_failed", "%s", msg);
    return PS5TM_TILE_ERROR;
  }

  snprintf(msg, msg_len,
           "Installation angestoßen – die Kachel erscheint gleich im "
           "Startmenü.");
  PS5TM_INFO("tile_install_queued", "Kachel-Installation gestartet (%s).", pkg);
  return PS5TM_TILE_INSTALLING;
}


/* Set by the web request, acted on by the background thread. */
static volatile int g_install_requested = 0;

void
ps5tm_tile_request_install(void) {
  g_install_requested = 1;
  PS5TM_INFO("tile_install_requested",
             "Installation der Startmenü-Kachel angefordert.");
}


/* Called once a second from the probe thread — the only place allowed to take
   the ShellCore identity and wait on the installer service. */
void
ps5tm_tile_service(void) {
  if(!g_install_requested) return;
  g_install_requested = 0;

  if(tile_installed()) {
    PS5TM_INFO("tile_present", "Kachel ist bereits vorhanden.");
    return;
  }

  if(ps5tm_tile_file_count == 0) {
    char why[192];
    const char *pkg = find_pkg(why, sizeof(why));
    if(!pkg) {
      if(why[0])
        PS5TM_WARN("tile_package_unusable",
                   "Kachel-Paket unbrauchbar: %s. Lade ein vollständiges Paket "
                   "über die Systemseite hoch, dann erneut versuchen.", why);
      else
        PS5TM_WARN("tile_no_package",
                   "Kein Paket vorhanden. Lade die .pkg-Datei über die "
                   "Systemseite hoch, dann erneut versuchen.");
      return;
    }
    PS5TM_INFO("tile_package", "Paket gefunden: %s", pkg);
  }

  /* The process holds the ShellCore identity from here until the restore at
   * the end — through the whole wait for the tile to appear, as it did when
   * this path was tuned on the console.
   *
   * /dev/icc_fan wants the fan identity (platform.c: without a matching authid
   * the open is rejected), so for up to forty-odd seconds the fan worker's
   * writes and readbacks fail. That is tolerable: the controller keeps the last
   * threshold, fan.c puts up with a run of refused readbacks before it gives
   * the watchdog up for a while, and it picks both up again by itself once the
   * identity is back. Handing the identity back right after the install call
   * would shorten the window, and the job does run in the installer service's
   * process rather than ours — but that has not been tried on the console, and
   * this is a one-time action where "works" matters more than a quiet fan.
   *
   * The Sony lock covers the calls themselves, so that no other thread's Sony
   * call — a profile request, say — runs under the wrong identity or into
   * Initialize. That includes up to ten seconds for sceAppInstUtilInitialize()
   * (timed_init; normally it answers at once, and after the first success the
   * call is skipped). The install call itself is not bounded: should it never
   * return, the lock would never be released. Making it abandonable needs a
   * thread for the call, as timed_init has for Initialize; left alone here
   * because it would change which thread talks to the service. The fan worker
   * only try-locks (telemetry.c), so none of this can stall it. */
  ps5tm_sony_api_lock();
  if(ps5tm_platform_escalate_as(PS5TM_AUTHID_SHELLCORE) != 0) {
    ps5tm_platform_escalate();
    ps5tm_sony_api_unlock();
    PS5TM_ERROR("tile_authid_failed",
                "Installations-Identität konnte nicht gesetzt werden.");
    return;
  }
  PS5TM_INFO("tile_authid", "Identität auf ShellCore umgestellt.");

  auto_note(0);                 /* "fail" until the call has come back */
  char msg[256];
  ps5tm_tile_state_t st = ps5tm_tile_ensure(msg, sizeof(msg));
  ps5tm_sony_api_unlock();
  PS5TM_INFO("tile_result", "%s (%s)", msg, ps5tm_tile_state_name(st));

  if(st == PS5TM_TILE_INSTALLING) {
    int appeared = 0;
    for(int i = 0; i < 30; i++) {
      sleep(1);
      if(tile_installed()) {
        PS5TM_INFO("tile_installed",
                   "Kachel installiert — im Startmenü unter Medien.");
        ps5tm_notify("Startmenü-Kachel installiert.\nZu finden unter Medien.");
        auto_note(1);
        appeared = 1;
        break;
      }
    }
    /* The call was accepted and nothing appeared: the service may still be at
       it, or may have rejected the package after taking it. Without this line
       the log ended at "Installation angestoßen" and the person pressing the
       button was left guessing. */
    if(!appeared)
      PS5TM_WARN("tile_install_timeout",
                 "Die Kachel ist nach 30 s noch nicht erschienen. Die "
                 "Installation läuft womöglich noch, oder das System hat das "
                 "Paket nach der Annahme abgelehnt — im Startmenü unter Medien "
                 "nachsehen, sonst erneut versuchen.");
  }

  ps5tm_sony_api_lock();
  if(ps5tm_platform_escalate() != 0)
    PS5TM_WARN("tile_authid_restore_failed",
               "Rücksetzen der Rechte fehlgeschlagen — die Lüftersteuerung "
               "kann gestört sein.");
  else
    PS5TM_INFO("tile_authid_back", "Identität zurückgestellt.");
  ps5tm_sony_api_unlock();
}


/* Runs once at start-up, before the fan controller claims its own identity.
 *
 * Does nothing at all when the tile is already there, which is the normal
 * case from the second run onwards — no unpacking, no service call, no delay.
 *
 * When it is not, the tile is put there once, by itself: that is what makes
 * sending this one ELF enough. Once — see tile-auto at the top of the file —
 * because a person who deletes the tile from the home screen did that on
 * purpose, and a tile that comes back at every start would be a nuisance.
 * "Kachel installieren" on the system page puts it back whenever it is asked. */
void
ps5tm_tile_bootstrap(void) {
  if(tile_installed()) {
    PS5TM_INFO("tile_present",
               "Startmenü-Kachel ist vorhanden (%s) – nichts zu tun.",
               PS5TM_TITLE_ID);
    return;
  }

  if(ps5tm_tile_file_count == 0 && ps5tm_tile_pkg_len == 0) {
    PS5TM_INFO("tile_absent",
               "Keine Kachel installiert und in dieser Fassung weder Dateien "
               "noch Paket eingebaut – Installation über die Systemseite "
               "möglich.");
    return;
  }

  if(!auto_allowed()) {
    PS5TM_INFO("tile_auto_skipped",
               "Die Startmenü-Kachel fehlt. Sie wird nicht von allein neu "
               "angelegt, weil sie schon einmal da war oder der Versuch dieser "
               "Version nicht gelang — „Kachel installieren“ auf der "
               "Systemseite holt sie zurück.");
    return;
  }

  /* Installing needs ShellCore's identity; the fan needs a different one.
     Take the installer's now and hand it back before anything touches the
     fan, so a failure here cannot leave the process wrongly identified. */
  ps5tm_sony_api_lock();
  if(ps5tm_platform_escalate_as(PS5TM_AUTHID_SHELLCORE) != 0) {
    ps5tm_sony_api_unlock();
    PS5TM_WARN("tile_authid_failed",
               "Konnte die Installations-Identität nicht setzen – Kachel "
               "wird übersprungen.");
    ps5tm_platform_escalate();
    return;
  }

  auto_note(0);                 /* "fail" until the call has come back */
  char msg[256];
  ps5tm_tile_state_t state = ps5tm_tile_ensure(msg, sizeof(msg));
  ps5tm_sony_api_unlock();

  if(state == PS5TM_TILE_INSTALLING) {
    /* Wait for the tile to appear, but not for long: the fan is unregulated
       until this returns, and that matters more than a home-screen icon. */
    for(int i = 0; i < 20; i++) {
      sleep(1);
      if(tile_installed()) {
        PS5TM_INFO("tile_installed", "Kachel wurde installiert.");
        ps5tm_notify("Startmenü-Kachel installiert.\nZu finden unter Medien.");
        auto_note(1);
        break;
      }
    }
    if(!tile_installed())
      PS5TM_INFO("tile_install_pending",
                 "Kachel-Installation läuft im Hintergrund weiter.");
  } else if(state != PS5TM_TILE_INSTALLED) {
    PS5TM_WARN("tile_install_skipped", "%s", msg);
  }

  /* Back to the identity everything after this point depends on. */
  ps5tm_sony_api_lock();
  if(ps5tm_platform_escalate() != 0)
    PS5TM_WARN("tile_authid_restore_failed",
               "Rücksetzen der Rechte nach der Kachel-Installation "
               "fehlgeschlagen – die Lüftersteuerung kann gestört sein.");
  ps5tm_sony_api_unlock();
}
