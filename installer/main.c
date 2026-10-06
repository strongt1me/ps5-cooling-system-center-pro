/* PS5 Cooling & System Center - Pro — standalone tile installer.
 *
 * Sole job: get the home-screen tile onto the console and say what happened.
 * It carries the PKG inside itself, so nothing has to be copied to a USB stick
 * or fetched over the network first. If the embedded copy is missing (a build
 * made before the PKG existed), it falls back to the usual on-disk locations.
 *
 * Why this is a separate payload rather than a button in the app: installing a
 * package needs ShellCore's identity (authid 0x3800000000000010), while the
 * fan controller needs a different one (0x4801000000000013) to open
 * /dev/icc_fan. A process gets one identity, so the two jobs cannot share it.
 * Running the installer once and the app afterwards keeps both correct.
 */

#include <dlfcn.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <ps5/kernel.h>
#include <ps5/klog.h>

#include "dynsym.h"
#include "tile_pkg.h"

#define TITLE_ID    "PSCC69690"
#define CONTENT_ID  "IV9999-PSCC69690_00-PS5COOLINGCNTR01"
#define APP_DIR     "/user/app/" TITLE_ID
/* No spaces: this path gets typed into FTP clients and shells. */
#define DATA_DIR    "/data/PS5-Cooling-Center"
#define STAGED_PKG  DATA_DIR "/tile.pkg"

/* ShellCore's identity. The fan controller's 0x4801000000000013 is accepted
   by /dev/icc_fan but rejected by the installer service. */
#define SHELLCORE_AUTHID 0x3800000000000010ULL

/* Where a manually-placed package may sit, in the order we look. */
static const char *k_fallback_paths[] = {
  STAGED_PKG,
  "/data/PS5_Cooling_Center.pkg",
  "/mnt/usb0/PS5_Cooling_Center.pkg",
  "/mnt/usb1/PS5_Cooling_Center.pkg",
  "/data/PS5 Cooling Center/tile.pkg",   /* pre-1.3 location */
};

#define CONTENTID_SIZE 0x30

typedef struct {
  char content_id[CONTENTID_SIZE];
  int  content_type;
  int  content_platform;
} pkg_info_t;

extern int sceKernelSendNotificationRequest(int, void *, size_t, int);
extern int sceKernelLoadStartModule(const char *, size_t, const void *,
                                    unsigned, void *, int *);

/* Resolved at run time, never linked.
 *
 * The SDK's stub library exports symbols the console's real .sprx does not
 * always provide, and a missing one makes the dynamic loader give up before
 * main() runs: no output, no toast, nothing at all — which is exactly what a
 * payload that "does nothing when sent" looks like. dlsym cannot fail that
 * way; at worst a pointer stays null and we say so. */
typedef int (*fn_inst_init_t)(void);
typedef int (*fn_inst_pkg_t)(const char *path, pkg_info_t *info);

static fn_inst_init_t p_inst_init = NULL;
static fn_inst_pkg_t  p_inst_pkg  = NULL;


/* Installer diagnostics to both the sender terminal and klogsrv.
   Keeps failures visible even when a toast cannot be shown yet. */
static void
diagf(const char *fmt, ...) {
  char line[512];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(line, sizeof(line), fmt, ap);
  va_end(ap);

  printf("%s\n", line);
  fflush(stdout);
  klog_printf("[installer] %s\n", line);
}

/* Must run after the module has been started, or there is nothing to find.
   Both names are reported either way: "which symbol is missing" is the first
   thing worth knowing when an install fails on an untested firmware. */
static int
resolve_appinst(int mod) {
  /* The module handle from sceKernelLoadStartModule() is passed straight in.
     Looking the module up again by name was pointless: it is already loaded
     and we already know which one it is. Name-based lookup failed anyway —
     Sony's libraries export by NID, which ps5tm_dynsym_in() handles last. */
  const char *AIU = "libSceAppInstUtil.sprx";
  unsigned    h   = (mod > 0) ? (unsigned)mod : 0u;

  p_inst_init = (fn_inst_init_t)
      ps5tm_dynsym_in(h, AIU, "sceAppInstUtilInitialize");
  const char *how_init = ps5tm_dynsym_route_name();

  p_inst_pkg = (fn_inst_pkg_t)
      ps5tm_dynsym_in(h, AIU, "sceAppInstUtilAppInstallPkg");
  const char *how_pkg = ps5tm_dynsym_route_name();

    diagf("Symbole: Initialize %s (%s), AppInstallPkg %s (%s)",
      p_inst_init ? "gefunden" : "FEHLT", how_init,
      p_inst_pkg  ? "gefunden" : "FEHLT", how_pkg);

  return (p_inst_init && p_inst_pkg) ? 0 : -1;
}


/* ------------------------------------------------------------------ output */

/* Everything the user sees goes to both places: stdout reaches whoever sent
   the payload over the network, the toast reaches whoever is at the TV. */
static void
say(const char *fmt, ...) {
  char line[512];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(line, sizeof(line), fmt, ap);
  va_end(ap);

  diagf("%s", line);

  struct { char unused[45]; char message[3075]; } req;
  memset(&req, 0, sizeof(req));
  snprintf(req.message, sizeof(req.message), "PS5 Cooling & System Center - Pro\n%s", line);
  sceKernelSendNotificationRequest(0, &req, sizeof(req), 0);
}


/* -------------------------------------------------------------- privileges */

static int
escalate(void) {
  pid_t pid = getpid();
  if(pid <= 0) return -1;

  if(!kernel_get_proc(pid)) {
    diagf("Kein Kernel-Zugriff — läuft der Payload über etaHEN/elfldr?");
    return -1;
  }

  int rc = 0;
  if(kernel_set_ucred_uid  (pid, 0) != 0) rc = -1;
  if(kernel_set_ucred_ruid (pid, 0) != 0) rc = -1;
  if(kernel_set_ucred_svuid(pid, 0) != 0) rc = -1;
  if(kernel_set_ucred_rgid (pid, 0) != 0) rc = -1;
  if(kernel_set_ucred_svgid(pid, 0) != 0) rc = -1;

  intptr_t rootvnode = kernel_get_root_vnode();
  if(rootvnode) {
    if(kernel_set_proc_rootdir(pid, rootvnode) != 0) rc = -1;
    if(kernel_set_proc_jaildir(pid, rootvnode) != 0) rc = -1;
  }

  if(kernel_set_ucred_authid(pid, SHELLCORE_AUTHID) != 0) rc = -1;

  uint8_t caps[16];
  memset(caps, 0xff, sizeof(caps));
  if(kernel_set_ucred_caps(pid, caps) != 0) rc = -1;

  uint8_t attrs[32];
  memset(attrs, 0, sizeof(attrs));
  attrs[0] = 0x80;
  if(kernel_set_ucred_attrs(pid, attrs) != 0) rc = -1;

  return rc;
}


/* ----------------------------------------------------------- the package */

static int
file_ok(const char *path) {
  struct stat st;
  return stat(path, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0;
}

/* Writes the built-in copy to disk. Returns the path, or NULL if this build
   has no package embedded. */
static const char *
unpack_embedded(void) {
  if(ps5tm_tile_pkg_len == 0) return NULL;

  mkdir("/data", 0755);
  mkdir(DATA_DIR, 0755);

  /* Write to a temporary name and rename, so an interrupted run can never
     leave a half-written package that the installer would then reject. */
  const char *tmp = STAGED_PKG ".part";
  FILE *f = fopen(tmp, "wb");
  if(!f) {
    diagf("Konnte %s nicht anlegen.", tmp);
    return NULL;
  }

  size_t written = fwrite(ps5tm_tile_pkg, 1, ps5tm_tile_pkg_len, f);
  int    flushed = (fflush(f) == 0);
  fclose(f);

  if(written != ps5tm_tile_pkg_len || !flushed) {
    diagf("Paket unvollständig geschrieben (%zu von %zu Bytes).",
          written, (size_t)ps5tm_tile_pkg_len);
    unlink(tmp);
    return NULL;
  }

  unlink(STAGED_PKG);
  if(rename(tmp, STAGED_PKG) != 0) {
    diagf("Konnte %s nicht an seinen Platz verschieben.", tmp);
    unlink(tmp);
    return NULL;
  }

  diagf("Eingebautes Paket entpackt: %s (%zu KiB)",
        STAGED_PKG, (size_t)(ps5tm_tile_pkg_len / 1024));
  return STAGED_PKG;
}

static const char *
find_pkg(void) {
  const char *embedded = unpack_embedded();
  if(embedded) return embedded;

  for(unsigned i = 0; i < sizeof(k_fallback_paths) / sizeof(k_fallback_paths[0]);
      i++) {
    if(file_ok(k_fallback_paths[i])) {
      diagf("Paket gefunden: %s", k_fallback_paths[i]);
      return k_fallback_paths[i];
    }
  }
  return NULL;
}


/* ------------------------------------------------------------- installing */

/* sceAppInstUtilInitialize() blocks indefinitely when the IPMI service is not
   up yet, so it runs on its own thread against a deadline. */
static volatile int g_init_done = 0;
static volatile int g_init_rc   = -1;

static void *
init_thread(void *arg) {
  (void)arg;
  g_init_rc   = p_inst_init ? p_inst_init() : -1;
  g_init_done = 1;
  return NULL;
}

static int
timed_init(void) {
  pthread_t tid;
  if(pthread_create(&tid, NULL, init_thread, NULL) != 0) return -1;
  pthread_detach(tid);

  for(int i = 0; i < 150; i++) {                 /* up to 15 s */
    if(g_init_done) return g_init_rc;
    struct timespec ts = { 0, 100000000 };
    nanosleep(&ts, NULL);
  }
  return -0xDEAD;
}

/* Where an installed title leaves traces.
 *
 * /user/app/<ID> alone was not enough: a tile was plainly visible in the home
 * screen while that directory did not exist, and the app therefore reported
 * "no tile" and offered to install one that was already there. The metadata
 * directory is written for every installed title regardless of where its data
 * ended up, which makes it the more reliable witness. */
static const char *k_tile_marks[] = {
  "/system_data/priv/appmeta/" TITLE_ID,
  APP_DIR,
  "/user/appmeta/" TITLE_ID,
  "/mnt/ext1/user/app/" TITLE_ID,
};

static const char *
tile_installed(void) {
  for(unsigned i = 0; i < sizeof(k_tile_marks) / sizeof(k_tile_marks[0]); i++) {
    struct stat st;
    if(stat(k_tile_marks[i], &st) == 0 && S_ISDIR(st.st_mode))
      return k_tile_marks[i];
  }
  return NULL;
}


int
main(void) {
  /* Unbuffered from the first line: when this payload goes wrong it does so
     early, and a buffered banner that never reaches the sender looks exactly
     like a payload that never started. */
  setvbuf(stdout, NULL, _IONBF, 0);

  diagf("");
  diagf("PS5 Cooling & System Center - Pro — Kachel-Installer");
  diagf("==============================================");

  /* Announced before anything can fail, so a silent run and a failed run can
     be told apart from the couch. */
  say("Installer gestartet.");

  const char *mark = tile_installed();
  if(mark) {
    diagf("Kachel gefunden unter %s", mark);
    say("Kachel ist bereits installiert.");
    return 0;
  }
  diagf("Keine Kachel gefunden, Installation beginnt.");

  if(escalate() != 0) {
    say("Rechte konnten nicht erweitert werden — Jailbreak neu laden.");
    return 1;
  }
    diagf("Rechte erweitert (authid 0x%016llx).",
      (unsigned long long)SHELLCORE_AUTHID);

  /* Linking alone does not guarantee the module is resident; etaHEN's PKG
     write-up is explicit that it must be started before Initialize(). */
  int mod_res = 0;
  int mod = sceKernelLoadStartModule(
      "/system/common/lib/libSceAppInstUtil.sprx", 0, NULL, 0, NULL, &mod_res);
  diagf("libSceAppInstUtil: handle 0x%08x, res 0x%08x", mod, mod_res);

  if(resolve_appinst(mod) != 0) {
    say("Installationsdienst nicht ansprechbar — Symbole fehlen.");
    return 1;
  }

  const char *pkg = find_pkg();
  if(!pkg) {
    say("Kein Kachel-Paket gefunden. PKG unter " STAGED_PKG " ablegen.");
    return 1;
  }

  int init_rc = timed_init();
  if(init_rc != 0) {
    if(init_rc == -0xDEAD)
      say("Installationsdienst antwortet nicht. Konsole neu starten.");
    else
      say("Installationsdienst nicht bereit (0x%08X).", (unsigned)init_rc);
    return 1;
  }
  diagf("Installationsdienst bereit.");

  pkg_info_t info;
  memset(&info, 0, sizeof(info));

  int rc = p_inst_pkg(pkg, &info);
  if(rc != 0) {
    say("Installation abgelehnt (0x%08X).", (unsigned)rc);
    diagf("Paket: %s", pkg);
    return 1;
  }

  diagf("Installation angestoßen, warte auf die Kachel …");

  /* The call returns as soon as the job is queued, so the only honest way to
     report success is to watch for the title to appear. */
  for(int i = 0; i < 60; i++) {                  /* up to 60 s */
    const char *now = tile_installed();
    if(now) {
      diagf("Erschienen unter %s", now);
      say("Kachel installiert — im Startmenü unter Medien.");
      diagf("Web-UI nach dem Start der App: http://<PS5-IP>:8086/");
      return 0;
    }
    sleep(1);
  }

  say("Installation läuft noch. Startmenü in einer Minute prüfen.");
  return 0;
}
