/* PS5 Cooling & System Center - Pro: a USB drive ejected safely, through ShadowMountPlus (29.10.2026).
 *
 * ShadowMountPlus 1.7beta10 can eject a USB drive (POST /api/v1/storage/eject, capability storage_eject). It checks
 * its own mounts, images and the running game, but knows nothing of this app's jobs: an eject in the middle of a copy,
 * a conversion or a backup would cut it off. Here the app asks it only when none of its own jobs runs and no folder of
 * the drive is mounted elsewhere (a PS4Mount game that is played from the drive is asked of the module itself: its mounts only
 * link to the pkgs; cutting the drive off leaves the tile of a running game without its data). The eject from ShadowMountPlus's own
 * page cannot be held back: its checks
 * look only at its own registry (sm_api_service.c usb_eject_block_reason / check_usb_eject_mount).
 *
 * One eject at a time; a thread follows ShadowMountPlus's storage job until the drive is gone or the job failed. */
#include "ps4m.h"
#include "ps5tm.h"
#include "titleid.h"
#include "third_party/cJSON.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/mount.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <unistd.h>

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static struct {
  int      running;
  char     mount[16];
  char     state[12];           /* "" never, running, done, failed */
  char     message[640];        /* the longest refusals (a program holding files, Kraken Mount's images) name a path: whole, or the translation finds no key */
  uint64_t at_ms;
} g_job;

/* 1 while a drive is being ejected, from the first claim to the end (audit 11.11.2026, F-06): the jobs of the app do not start in that
   window (they list this next to the other busy functions), as the eject does not start beside them. */
int
ps5tm_usbeject_busy(void) {
  pthread_mutex_lock(&g_lock);
  int b = g_job.running;
  pthread_mutex_unlock(&g_lock);
  return b;
}

/* 1 while a drive is being ejected and `path` is that drive or lies below it. The file manager asks it before it reads anything (a list, a view,
   a size, a download): ShadowMountPlus cuts the drive off while the eject runs, and a read still under way on it was the one gap between the button
   of the file manager and the one under System, which read nothing from the drive (21.11.2026). */
int
ps5tm_usbeject_covers(const char *path) {
  if(!path) return 0;
  pthread_mutex_lock(&g_lock);
  int hit = 0;
  if(g_job.running && g_job.mount[0]) {
    size_t ml = strlen(g_job.mount);
    hit = !strncmp(path, g_job.mount, ml) && (path[ml] == '/' || path[ml] == 0);
  }
  pthread_mutex_unlock(&g_lock);
  return hit;
}

/* A drive whose eject ShadowMountPlus took: for this long after it, the app's scanners keep away from it (see ps5tm.h). Kept per drive (usb0..usb7, ext0),
   so that the eject of a second drive does not end the quiet time of the first (F-356), and on the monotonic clock, which a change of the time of day does
   not move (F-357). */
#define SETTLE_MS 30000
#define SETTLE_SLOTS 9
static uint64_t g_settle_until[SETTLE_SLOTS];     /* 0: not settling; else ps5tm_mono_ms() until which it is */

/* usb0..usb7 -> 0..7, ext0 -> 8, the drive `path` lies on (or is); -1 for anything else. */
static int
slot_of(const char *path) {
  return ps5tm_drive_slot(path);                       /* drivegate.c: the one place that names the drives */
}

int
ps5tm_usbeject_settling(const char *path) {
  if(!path) return 0;
  pthread_mutex_lock(&g_lock);
  int hit = 0;
  if(g_job.running && g_job.mount[0]) {
    size_t ml = strlen(g_job.mount);
    hit = !strncmp(path, g_job.mount, ml) && (path[ml] == '/' || path[ml] == 0);
  }
  int s = slot_of(path);
  if(!hit && s >= 0 && g_settle_until[s] && ps5tm_mono_ms() < g_settle_until[s]) hit = 1;
  pthread_mutex_unlock(&g_lock);
  return hit;
}

/* Any drive: the PS4Mount worker puts off what it does on the drives (a restore, an update or add-on choice, adding a game again) while this is 1. */
int
ps5tm_usbeject_active(void) {
  pthread_mutex_lock(&g_lock);
  int a = g_job.running;
  uint64_t now = ps5tm_mono_ms();
  for(int i = 0; i < SETTLE_SLOTS && !a; i++) a = g_settle_until[i] && now < g_settle_until[i];
  pthread_mutex_unlock(&g_lock);
  return a;
}

/* ShadowMountPlus took the eject of `mount` (or may have): the drive is left alone for SETTLE_MS. Not for a refusal that came before anything was cut off
   (a game runs from it, it is in use): there the drive never went away, and it would only vanish from the lists for nothing. */
static void
settle(const char *mount) {
  int s = slot_of(mount);
  if(s < 0) return;
  pthread_mutex_lock(&g_lock);
  g_settle_until[s] = ps5tm_mono_ms() + SETTLE_MS;
  pthread_mutex_unlock(&g_lock);
}

/* The marker of an eject that did not come back (ejectmark.c, 21.11.2026): the eject stays off after a crash until the person allows it again. */
void
ps5tm_usbeject_init(void) { ps5tm_ejectmark_init(); }

int
ps5tm_usbeject_blocked(void) { return ps5tm_ejectmark_blocked(); }

int
ps5tm_usbeject_rearm(void) { return ps5tm_ejectmark_rearm(); }

void
ps5tm_usbeject_shutdown(void) { ps5tm_ejectmark_shutdown(); }

/* /mnt/usb0../mnt/usb7 and /mnt/ext0: what ShadowMountPlus calls ejectable (the M.2 drive /mnt/ext1 is not). Nothing is offered while the eject is blocked. */
int
ps5tm_usbeject_path_ok(const char *mount) {
  if(!mount) return 0;
  if(ps5tm_usbeject_blocked()) return 0;
  if(!strcmp(mount, "/mnt/ext0")) return 1;
  return !strncmp(mount, "/mnt/usb", 8) && mount[8] >= '0' && mount[8] <= '7' && !mount[9];
}

/* The app's own jobs, any of them: most read or write USB drives, and which drive a job uses is not always known
   beforehand (a copy from the SSD to usb1, a backup, the file manager). 1 with what runs. Also asked before a game is
   mounted or unmounted (smpremount.c). */
int
ps5tm_app_job_running(char *why, size_t n) {
  return ps5tm_jobs_busy_except(PS5TM_JOB_USBEJECT, PS5TM_JOBS_READERS, why, n);
}

/* The same, with an eject that runs counted too: for the questions asked from outside the eject (/api/v1/jobs/active - the tile replaces the app only when
   it says no -, mounting over ShadowMountPlus, the hold guard). ps5tm_app_job_running leaves the eject out because the eject asks it itself (review of
   21.11.2026, F3). */
int
ps5tm_any_job_running(char *why, size_t n) {
  return ps5tm_jobs_busy_except(-1, PS5TM_JOBS_READERS, why, n);
}

/* A folder of the drive mounted somewhere else (PS4Mount's games, a ShadowMountPlus title): 1 with the reason. */
static int
drive_in_use(const char *mount, char *why, size_t n) {
  int count = getfsstat(NULL, 0, MNT_NOWAIT);
  if(count <= 0) return 0;
  struct statfs *list = malloc(sizeof(*list) * (size_t)(count + 16));
  if(!list) return 0;
  count = getfsstat(list, (long)(sizeof(*list) * (size_t)(count + 16)), MNT_NOWAIT);
  size_t ml = strlen(mount);
  int ps4 = 0, other = 0;
  char first[64] = "";
  for(int i = 0; i < count; i++) {
    const char *from = list[i].f_mntfromname, *on = list[i].f_mntonname;
    /* The built-in PS4Mount: a game is a nullfs mount of its own hold folder in the system data, which only links to the pkgs, so the
       mount table never names the drive. The module knows which drive a game's pkgs are on, and whether it is played (ps4m_core.c). */
    if(!strncmp(on, "/user/app/CUSA", 14) && strlen(on) == 19 && !strncmp(from, "/system_data/ps4mount/", 22)) {
      /* -1, not to be known: held back. Once the module's lock was not to be had, the others are not asked again (1.5 s each). */
      static __thread int unknown;
      if(i == 0) unknown = 0;
      int r = unknown ? -1 : ps4m_game_running_from(on + 10, mount);
      if(r < 0) unknown = 1;
      if(r != 0 && !ps4++) snprintf(first, sizeof(first), "%s", on + 10);
      continue;
    }
    if(strncmp(from, mount, ml) || from[ml] != '/' || !strcmp(on, mount)) continue;
    if(!strncmp(on, "/user/app/CUSA", 14) && strstr(from, "/ps4/")) {
      if(!ps4++) snprintf(first, sizeof(first), "%s", on + 10);
    } else if(!other++ && !first[0]) {
      snprintf(first, sizeof(first), "%s", on);
    }
  }
  free(list);
  if(ps4) {
    snprintf(why, n, "PS4Mount spielt %d PS4-Spiel(e) von diesem Laufwerk, zum Beispiel %s. Auswerfen würde ihnen die Daten "
             "nehmen; erst mit „Kachel entfernen“ herausnehmen oder die Konsole neu starten.", ps4, first);
    return 1;
  }
  if(other) {
    snprintf(why, n, "Ein Ordner dieses Laufwerks ist an anderer Stelle eingehängt (%s). Erst das Spiel oder Programm "
             "dahinter beenden.", first);
    return 1;
  }
  return 0;
}

/* A game of Kraken Mount (KrakenMount) that runs, also paused, from this drive (audit F-254). drive_in_use sees the nullfs of the .pkg's folder in the game's
   sandbox, but not a .pkg that lies in the drive's root (that mount names the drive itself, as the nullfs of the whole drive does in every sandbox). Asked of
   the kernel (the eboot.bin of that title), not of the game state; -1, not to be known: held back, as with PS4Mount. */
static int
kraken_game_on(const char *mount) {
  ps5tm_kraken_game_t *kg = calloc(PS5TM_KRAKEN_MAX, sizeof(*kg));     /* ~80 KB: not on the stack */
  if(!kg) return 0;
  int k = ps5tm_kraken_games(kg, PS5TM_KRAKEN_MAX), hit = 0;
  size_t ml = strlen(mount);
  for(int i = 0; i < k && !hit; i++) {
    const char *src = kg[i].image[0] ? kg[i].image : kg[i].path;      /* an image game (0.4.2): its image file is on the drive, its path in Kraken Mount's mount */
    if(strncmp(src, mount, ml) || src[ml] != '/') continue;
    /* an id that the kernel way never judges (not PPSA/CUSA, not the app's tile) would count as "not to be known" for ever: skipped (review 18.11.) */
    if(!ps5tm_is_game_title_id(kg[i].title_id) && !ps5tm_is_own_tile(kg[i].title_id)) continue;
    hit = ps5tm_procmgr_title_running(kg[i].title_id) != 0;
  }
  free(kg);
  return hit;
}

/* Kraken Mount 0.4.2 mounts game images (.exfat, .ffpkg, .ffpfs, .ffpfsc) itself: attached to /dev/lvdN and mounted read-only on /mnt/krakenmount/<name>,
   with /data/krakenmount/images/<name> = "<unit>\n<image path>" (its src/image.rs). Such a mount names no path of the drive (drive_in_use does not
   see it), it outlives a stop of Kraken Mount (a later copy takes it over), and ShadowMountPlus's eject releases only its own images: the console would cut the
   drive off under it. Kraken Mount's own unmount of the drive releases them first. How many of its image mounts are of an image on this drive, the first
   one in `first`. A mount without a readable record is not counted: which drive it is on is not to be known (27.11.2026). */
#define KR_IMAGE_BASE  "/mnt/krakenmount/"
#ifndef KR_IMAGE_STATE                            /* the host test (test/usbeject) points it at a folder of its own */
#define KR_IMAGE_STATE "/data/krakenmount/images/"
#endif
static int
kraken_images_on(const char *mount, char *first, size_t n) {
  int count = getfsstat(NULL, 0, MNT_NOWAIT);
  if(count <= 0) return 0;
  struct statfs *list = malloc(sizeof(*list) * (size_t)(count + 16));
  if(!list) return 0;
  count = getfsstat(list, (long)(sizeof(*list) * (size_t)(count + 16)), MNT_NOWAIT);
  const size_t ml = strlen(mount), bl = strlen(KR_IMAGE_BASE);
  int hits = 0;
  for(int i = 0; i < count; i++) {
    const char *on = list[i].f_mntonname, *name = on + bl;
    if(strncmp(on, KR_IMAGE_BASE, bl) || !name[0] || strchr(name, '/') || !strcmp(name, ".") || !strcmp(name, "..")) continue;
    char rec[sizeof(KR_IMAGE_STATE) + sizeof(list[0].f_mntonname)];
    if(snprintf(rec, sizeof(rec), KR_IMAGE_STATE "%s", name) >= (int)sizeof(rec)) continue;
    int fd = open(rec, O_RDONLY | O_NONBLOCK);
    if(fd < 0) continue;
    char buf[1100];
    ssize_t got = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if(got <= 0) continue;
    buf[got] = 0;
    char *img = strchr(buf, '\n');                   /* line 1: the unit; line 2: the image file */
    if(!img) continue;
    img++;
    img[strcspn(img, "\r\n")] = 0;
    if(strncmp(img, mount, ml) || img[ml] != '/') continue;
    if(!hits++) snprintf(first, n, "%s", img);
  }
  free(list);
  return hits;
}

/* ShadowMountPlus's refusal codes (docs/api.md of 1.7beta10) in words. */
static const char *
reason_text(const char *r) {
  static const struct { const char *code, *text; } k[] = {
    { "game_active",           "Ein laufendes Spiel nutzt dieses Laufwerk." },
    { "runtime_prepared",      "Ein Spiel wird gerade gestartet oder beendet." },
    { "runtime_busy",          "Ein Spiel wird gerade gestartet oder beendet." },
    { "scanner_busy",          "ShadowMountPlus sucht gerade nach Spielen; gleich noch einmal versuchen." },
    { "usb_mount_busy",        "Ein Spiel oder Programm nutzt einen Ordner dieses Laufwerks." },
    { "usb_partition_busy",    "Eine andere Partition dieses Laufwerks ist noch eingehängt." },
    { "usb_image_busy",        "Ein Abbild auf diesem Laufwerk ist eingehängt." },
    { "usb_disk_busy",         "Das Laufwerk wird noch benutzt." },
    { "usb_mount_changed",     "Das Laufwerk hat sich geändert oder wurde abgezogen." },
    { "usb_eject_unconfirmed", "Die Konsole hat das Auswerfen angenommen, das Laufwerk ist aber noch da. Angeschlossen lassen und später nachsehen." },
    { "usb_eject_failed",      "Die Konsole hat das Auswerfen abgelehnt." },
  };
  for(size_t i = 0; i < sizeof(k) / sizeof(k[0]); i++)
    if(r && !strcmp(r, k[i].code)) return k[i].text;
  return NULL;
}

/* The refusal codes of ShadowMountPlus that come from its checks before it touches the drive: nothing was cut off then. The two others
   (usb_eject_failed, usb_eject_unconfirmed) are after the detach was tried, the drive may be gone or half gone. */
static int
refused_before_detach(const char *r) {
  static const char *const k[] = { "game_active", "runtime_prepared", "runtime_busy", "scanner_busy", "usb_mount_busy", "usb_partition_busy",
                                   "usb_image_busy", "usb_disk_busy", "usb_mount_changed" };
  for(size_t i = 0; r && i < sizeof(k) / sizeof(k[0]); i++)
    if(!strcmp(r, k[i])) return 1;
  return 0;
}

static void
finish(const char *state, const char *msg) {
  pthread_mutex_lock(&g_lock);
  g_job.running = 0;
  snprintf(g_job.state, sizeof(g_job.state), "%s", state);
  snprintf(g_job.message, sizeof(g_job.message), "%s", msg);
  g_job.at_ms = ps5tm_now_ms();
  pthread_mutex_unlock(&g_lock);
}

typedef struct { double job_id; char mount[16], label[24]; } follow_t;

static void *
follow(void *arg) {
  follow_t *f = arg;
  char msg[240];
  for(int misses = 0, rounds = 0; misses < 20 && rounds < 120; rounds++) {
    usleep(1000 * 1000);
    char err[160];
    cJSON *s = ps5tm_smp_call("/games/storage/status", NULL, 4000, err, sizeof(err));
    if(!s) { misses++; continue; }
    misses = 0;
    const cJSON *id = cJSON_GetObjectItem(s, "job_id");
    if(f->job_id > 0 && cJSON_IsNumber(id) && id->valuedouble != f->job_id) {
      cJSON_Delete(s);
      break;
    }
    if(cJSON_IsTrue(cJSON_GetObjectItem(s, "active"))) { cJSON_Delete(s); continue; }
    const cJSON *rs = cJSON_GetObjectItem(s, "result_status");
    const cJSON *rr = cJSON_GetObjectItem(s, "result_error_reason");
    const cJSON *re = cJSON_GetObjectItem(s, "result_error");
    int ok = !cJSON_IsNumber(rs) || rs->valuedouble == 0;
    const char *why = cJSON_IsString(rr) ? reason_text(rr->valuestring) : NULL;
    /* ejecting only detaches the drive by software: left plugged in, it announces itself again after a few seconds and the console mounts it anew
       (kernel log of the Pro, 21.11.2026: da1 detached, attached, mounted at /mnt/usb0), so the message says it */
    if(ok) snprintf(msg, sizeof(msg), "%s ist ausgeworfen und kann abgezogen werden. Manche Laufwerke meldet die Konsole nach einigen Sekunden von selbst wieder an, andere erst nach erneutem Einstecken.", f->label);
    else snprintf(msg, sizeof(msg), "%s wurde nicht ausgeworfen: %s", f->label,
                  why ? why : cJSON_IsString(re) && re->valuestring[0] ? re->valuestring : "ShadowMountPlus lehnte ab.");
    const int before = !ok && cJSON_IsString(rr) && refused_before_detach(rr->valuestring);
    cJSON_Delete(s);
    if(!before) settle(f->mount);
    if(before) ps5tm_ejectmark_clear_now();                    /* nothing was cut off: no crash can be blamed on this one */
    else ps5tm_ejectmark_clear_later();                        /* cut off (or may be): the marker stays through the re-announce window of the drive */
    finish(ok ? "done" : "failed", msg);                    /* first: a notification that hangs must not keep the eject claimed (F11) */
    PS5TM_INFO(ok ? "usb_ejected" : "usb_eject_refused", "%s", msg);
    ps5tm_notify_plain("%s", msg);
    free(f);
    return NULL;
  }
  snprintf(msg, sizeof(msg), "Vom Auswerfen von %s kam keine Rückmeldung. Unter System nachsehen, ob es noch da ist.", f->label);
  PS5TM_WARN("usb_eject_lost", "%s", msg);
  settle(f->mount);
  ps5tm_ejectmark_clear_later();
  finish("failed", msg);
  free(f);
  return NULL;
}

/* Does this process, which has a file open on the drive, stop an eject? The console cuts the drive off with a forced unmount and asks nobody (drivegate.c), so
   a process that does not know about it loses what it has open, and whatever that costs the kernel it costs on this console. Not counted: ShadowMountPlus
   (it closes its own watchers right before, sm_scanner_suspend_usb_for_eject), the console's own services (Sce*, NPXS*, mini-syscore: the console's eject takes
   care of them itself), kstuff and the payload loader (kstuff keeps a watcher on /mnt/usbN for good, on every console that runs the fan control).
   Counted: this app itself (a handler or a job that did not close something) and every other payload, FTP server, file manager or tool.
   (Written without having seen the list of a console: GET /api/v1/storage/holders shows it, and which of the names blocks is easily changed here.) */
static int
holder_blocks(const ps5tm_holder_t *h, int *self) {
  *self = h->pid == (int)getpid();
  if(*self) return 1;
  static const char *const ok[] = { "shadowmount", "kstuff", "elfldr", "Sce", "SCE", "NPXS", "mini-syscore", "SceShell", "ps5-payload", "ps5tm", "ps5cc" };
  for(size_t i = 0; i < sizeof(ok) / sizeof(ok[0]); i++)
    if(!strncasecmp(h->name, ok[i], strlen(ok[i]))) return 0;
  return 1;
}

/* One line (and a few more) in the log, written to the file at once: what the state of the app and of the mount table was when the drive was cut off.
   After a crash this is what is left (the app log is on the internal storage and is fsync'ed here). */
static void
snapshot(const char *mount, int nholders, const ps5tm_holder_t *h) {
  int per[9], any = 0;
  ps5tm_drive_counts(per, &any);
  int slot = ps5tm_drive_slot(mount);
  char line[400];
  int o = snprintf(line, sizeof(line), "Vor dem Auswerfen von %s: Leser im Tor %d (alle Laufwerke %d); Dateimanager liest %d; Auftrag der App %d; Prozesse mit offenen Dateien: ",
                   mount, slot >= 0 ? per[slot] : 0, any, ps5tm_filemgr_reading(), ps5tm_app_job_running(NULL, 0));
  if(nholders < 0) o += snprintf(line + o, sizeof(line) - (size_t)o, "nicht ermittelbar");
  else if(nholders == 0) o += snprintf(line + o, sizeof(line) - (size_t)o, "keine");
  for(int i = 0; i < nholders && o < (int)sizeof(line) - 80; i++)
    o += snprintf(line + o, sizeof(line) - (size_t)o, "%s%s(%d)x%d", i ? ", " : "", h[i].name, h[i].pid, h[i].count);
  PS5TM_INFO("usb_eject_state", "%s", line);
  int count = getfsstat(NULL, 0, MNT_NOWAIT);
  if(count > 0) {
    struct statfs *list = malloc(sizeof(*list) * (size_t)(count + 16));
    if(list) {
      count = getfsstat(list, (long)(sizeof(*list) * (size_t)(count + 16)), MNT_NOWAIT);
      size_t ml = strlen(mount);
      int shown = 0;
      for(int i = 0; i < count && shown < 8; i++) {
        const char *from = list[i].f_mntfromname, *on = list[i].f_mntonname;
        if(strncmp(on, mount, ml) && strncmp(from, mount, ml) && strncmp(from, "/dev/da", 7)) continue;
        PS5TM_INFO("usb_eject_state", "Einhängung: %s auf %s (%s)", from, on, list[i].f_fstypename);
        shown++;
      }
      free(list);
    }
  }
  ps5tm_log_file_flush();                           /* to the file now: the console may be gone a moment later */
}

/* Starts the eject of mount. 202 started, otherwise the HTTP status of the refusal; msg says why or what happens. */
int
ps5tm_usbeject_start(const char *mount, char *msg, size_t n) {
  if(ps5tm_usbeject_blocked()) {
    snprintf(msg, n, "Auswerfen ist gesperrt: Das letzte Auswerfen eines USB-Laufwerks ist nicht zurückgekommen, die App oder die Konsole ist dabei abgestürzt. "
             "Auf der Seite Dateien oder System lässt es sich wieder erlauben.");
    return 409;
  }
  /* the form of the path, not whether it is blocked (asked above) */
  if(!(mount && (!strcmp(mount, "/mnt/ext0") || (!strncmp(mount, "/mnt/usb", 8) && mount[8] >= '0' && mount[8] <= '7' && !mount[9])))) {
    snprintf(msg, n, "Auswerfen geht nur bei USB-Laufwerken.");
    return 400;
  }
  /* from what is known: ShadowMountPlus answers a fresh ask with a stat() of every game's source, and the eject is the one moment the drives are best left alone */
  if(!ps5tm_smp_can_cached("storage_eject") && !ps5tm_smp_can("storage_eject")) {
    snprintf(msg, n, "Auswerfen braucht ShadowMountPlus ab 1.7beta10; es läuft nicht oder ist älter.");
    return 409;
  }
  char label[24];
  if(!strcmp(mount, "/mnt/ext0")) snprintf(label, sizeof(label), "Erweiterter Speicher");
  else snprintf(label, sizeof(label), "usb%d", mount[8] - '0');   /* as the console names it */

  pthread_mutex_lock(&g_lock);
  int busy = g_job.running;
  if(!busy) {
    g_job.running = 1;
    snprintf(g_job.mount, sizeof(g_job.mount), "%s", mount);
    snprintf(g_job.state, sizeof(g_job.state), "running");
    /* "not yet" in the text itself: pulled while this shows, the drive is pulled mid-unmount (the eject took 2 to 7 s on
       the test consoles, 21.11.2026); the message that it may be pulled comes only after the console has unmounted it */
    snprintf(g_job.message, sizeof(g_job.message), "%s wird ausgeworfen … Bitte noch nicht abziehen, bis „ausgeworfen“ erscheint.", label);
    g_job.at_ms = ps5tm_now_ms();
  }
  pthread_mutex_unlock(&g_lock);
  if(!busy) ps5tm_filemgr_search_stop(mount);     /* a search of the file manager on this drive ends first (it is a reader; 25.11.2026) */
  if(busy) {
    snprintf(msg, n, "Es wird schon ein Laufwerk ausgeworfen.");
    return 409;
  }
  char why[240];
  if(ps5tm_app_job_running(why, sizeof(why))) {
    snprintf(msg, n, "%s wurde nicht ausgeworfen: %s. Solange ein Auftrag der App läuft, wirft die App kein Laufwerk aus.",
             label, why);
    PS5TM_INFO("usb_eject_held", "%s", msg);
    finish("failed", msg);
    return 409;
  }
  if(drive_in_use(mount, why, sizeof(why))) {
    snprintf(msg, n, "%s wurde nicht ausgeworfen: %s", label, why);
    PS5TM_INFO("usb_eject_held", "%s", msg);
    finish("failed", msg);
    return 409;
  }
  if(kraken_game_on(mount)) {
    snprintf(msg, n, "%s wurde nicht ausgeworfen: %s", label, "Ein laufendes Spiel nutzt dieses Laufwerk.");
    PS5TM_INFO("usb_eject_held", "%s", msg);
    finish("failed", msg);
    return 409;
  }
  {
    char img[160] = "";
    const int kn = kraken_images_on(mount, img, sizeof(img));
    if(kn > 0) {
      snprintf(msg, n, "%s wurde nicht ausgeworfen: Kraken Mount hat %d Abbild(er) dieses Laufwerks eingehängt, zum Beispiel %s. Das Auswerfen über "
               "ShadowMountPlus gibt sie nicht frei. Erst in Kraken Mount aushängen: Seite KrakenMount, „Seite von Kraken Mount“, beim Laufwerk „Aushängen“ "
               "(läuft Kraken Mount nicht, vorher einschalten).", label, kn, img);
      PS5TM_INFO("usb_eject_held", "%s", msg);
      finish("failed", msg);
      return 409;
    }
  }

  /* The scanners of the app (the PS4Mount worker above all: every 10 s a walk down every drive, up to eight levels) look at no drive that is being
     ejected from now on, as the claim above is made. One that began before it is waited for: the worker holds its lock for the whole of a pass.
     Cutting a drive off under a walk that is still reading it is what the console does not forgive (reports of 21.11.2026: system storage to
     repair after an eject from the file manager, now and then, not every time - the timing of the walk). */
  if(ps4m_wait_drive_free(mount, 10000) != 0) {
    snprintf(msg, n, "%s wurde nicht ausgeworfen: PS4Mount arbeitet gerade (Spiele anmelden, Zusatzinhalte kopieren oder zurücklegen). Gleich noch einmal versuchen.", label);
    PS5TM_INFO("usb_eject_held", "%s", msg);
    finish("failed", msg);
    return 409;
  }

  /* The drive gate (drivegate.c, 21.11.2026): every reader of the app that was on the drive before the claim is WAITED for (the new ones are turned away, as
     they were before). Until now only new reads were refused; one that had begun was cut off with the drive. */
  if(ps5tm_drive_wait_idle(mount, 4000) != 0) {
    snprintf(msg, n, "%s wurde nicht ausgeworfen: Die App liest noch vom Laufwerk. Gleich noch einmal versuchen.", label);
    PS5TM_INFO("usb_eject_held", "%s", msg);
    finish("failed", msg);
    return 409;
  }

  /* Who else has a file open: the console's forced unmount takes it away from them (firmware analysis of 21.11.2026). Written to the log whatever the answer. */
  ps5tm_holder_t holders[16];
  int nh = ps5tm_drive_holders(mount, holders, 16);
  for(int i = 0; i < nh; i++) {
    int self = 0;
    int blocks = holder_blocks(&holders[i], &self);
    PS5TM_INFO("usb_eject_holder", "%s: %s (PID %d) hat %d Datei(en) offen, zum Beispiel %s%s", mount, holders[i].name, holders[i].pid, holders[i].count,
               holders[i].sample, blocks ? " – hält das Auswerfen zurück" : "");
    if(blocks) {
      if(self) snprintf(msg, n, "%s wurde nicht ausgeworfen: Die App selbst hat noch eine Datei auf dem Laufwerk offen (%s). Gleich noch einmal versuchen.", label, holders[i].sample);
      else snprintf(msg, n, "%s wurde nicht ausgeworfen: Das Programm „%s“ hat noch %d Datei(en) auf dem Laufwerk offen (zum Beispiel %s). Erst dort schließen oder unter Payloads beenden, "
                    "dann noch einmal. Die Konsole trennt das Laufwerk mit Gewalt und fragt nicht nach.", label, holders[i].name, holders[i].count, holders[i].sample);
      PS5TM_INFO("usb_eject_held", "%s", msg);
      finish("failed", msg);
      return 409;
    }
  }
  if(nh < 0) PS5TM_INFO("usb_eject_holders_unknown", "%s: Welche Programme Dateien auf dem Laufwerk offen haben, ließ sich nicht ermitteln; es wird trotzdem ausgeworfen.", mount);

  /* Everything the app wrote is on the drive before it is cut off (prosperomgr and PS5Upload do the same; ShadowMountPlus does not). Then the gate once more: a
     reader that came in while the flush ran is waited for, too. */
  sync();
  usleep(300 * 1000);
  if(ps5tm_drive_wait_idle(mount, 2000) != 0) {
    snprintf(msg, n, "%s wurde nicht ausgeworfen: Die App liest noch vom Laufwerk. Gleich noch einmal versuchen.", label);
    PS5TM_INFO("usb_eject_held", "%s", msg);
    finish("failed", msg);
    return 409;
  }
  snapshot(mount, nh, holders);

  /* the drive as ShadowMountPlus lists it: its source and mount_id go back unchanged */
  char err[160], source[96] = "", mount_id[24] = "";
  cJSON *list = ps5tm_smp_call("/storage", NULL, 6000, err, sizeof(err));
  const cJSON *m = NULL, *mounts = list ? cJSON_GetObjectItem(list, "mounts") : NULL;
  cJSON_ArrayForEach(m, mounts) {
    const cJSON *mp = cJSON_GetObjectItem(m, "mount_point");
    if(!cJSON_IsString(mp) || strcmp(mp->valuestring, mount)) continue;
    const cJSON *src = cJSON_GetObjectItem(m, "source"), *id = cJSON_GetObjectItem(m, "mount_id");
    if(cJSON_IsTrue(cJSON_GetObjectItem(m, "ejectable")) && cJSON_IsString(src) && cJSON_IsString(id)) {
      snprintf(source, sizeof(source), "%s", src->valuestring);
      snprintf(mount_id, sizeof(mount_id), "%s", id->valuestring);
    }
    break;
  }
  int answered = list != NULL;
  cJSON_Delete(list);
  if(!source[0]) {
    snprintf(msg, n, "%s wurde nicht ausgeworfen: %s", label,
             answered ? "ShadowMountPlus kennt das Laufwerk nicht als auswerfbar." : err);
    finish("failed", msg);
    return 409;
  }
  ps5tm_ejectmark_write(mount);                                 /* from here the drive may be cut off: if the console dies, this stays (see above) */
  PS5TM_INFO("usb_eject_ask", "%s: ShadowMountPlus wird gebeten, das Laufwerk auszuwerfen.", mount);
  ps5tm_log_file_flush();
  cJSON *body = cJSON_CreateObject();
  cJSON_AddStringToObject(body, "path", mount);
  cJSON_AddStringToObject(body, "source", source);
  cJSON_AddStringToObject(body, "mount_id", mount_id);
  cJSON *ans = ps5tm_smp_call("/storage/eject", body, 8000, err, sizeof(err));
  cJSON_Delete(body);
  if(!ans) {
    /* no answer in time: ShadowMountPlus may have taken it and be cutting the drive off now (a refusal it answers is one that came before anything was cut: smp.c) */
    if(!strncmp(err, "ShadowMountPlus antwortet nicht", 31)) { settle(mount); ps5tm_ejectmark_clear_later(); }
    else ps5tm_ejectmark_clear_now();                           /* it was not even asked: nothing was cut off */
    snprintf(msg, n, "%s wurde nicht ausgeworfen: %s", label, err);
    finish("failed", msg);
    return 409;
  }
  const cJSON *id = cJSON_GetObjectItem(ans, "job_id");
  follow_t *f = calloc(1, sizeof(*f));
  if(f) {
    f->job_id = cJSON_IsNumber(id) ? id->valuedouble : 0;
    snprintf(f->mount, sizeof(f->mount), "%s", mount);
    snprintf(f->label, sizeof(f->label), "%s", label);
  }
  cJSON_Delete(ans);
  pthread_t t;
  pthread_attr_t at;
  pthread_attr_init(&at);
  pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
  if(!f || pthread_create(&t, &at, follow, f) != 0) {
    free(f);
    pthread_attr_destroy(&at);
    settle(mount);                                    /* ShadowMountPlus took it: nobody follows, the drive is still left alone */
    ps5tm_ejectmark_clear_later();
    snprintf(msg, n, "%s: Auswerfen angestoßen, die App kann es aber nicht verfolgen.", label);
    finish("failed", msg);
    return 202;
  }
  pthread_attr_destroy(&at);
  PS5TM_INFO("usb_eject_started", "%s wird über ShadowMountPlus ausgeworfen.", label);
  snprintf(msg, n, "%s wird ausgeworfen … Bitte noch nicht abziehen, bis „ausgeworfen“ erscheint.", label);
  return 202;
}

/* {"state":"","running":…,"mount":…,"message":…,"at_ms":…}; state "" = never asked since the start of the app. */
cJSON *
ps5tm_usbeject_json(void) {
  cJSON *o = cJSON_CreateObject();
  const int blocked = ps5tm_ejectmark_blocked();            /* asked before g_lock: ejectmark.c has its own lock */
  pthread_mutex_lock(&g_lock);
  cJSON_AddBoolToObject(o, "ok", 1);
  cJSON_AddBoolToObject(o, "available", 0);
  cJSON_AddBoolToObject(o, "blocked", blocked);            /* the last eject did not come back: off until it is allowed again (POST {"action":"rearm"}) */
  cJSON_AddStringToObject(o, "state", g_job.state);
  cJSON_AddBoolToObject(o, "running", g_job.running);
  cJSON_AddStringToObject(o, "mount", g_job.mount);
  cJSON_AddStringToObject(o, "message", g_job.message);
  cJSON_AddNumberToObject(o, "at_ms", (double)g_job.at_ms);
  pthread_mutex_unlock(&g_lock);
  /* from what is known while a drive is being cut off: the page asks every 1.5 s then, and a fresh ask of ShadowMountPlus is a stat() of every game's source */
  int av = !ps5tm_usbeject_blocked() && (ps5tm_smp_can_cached("storage_eject") || (!ps5tm_usbeject_active() && ps5tm_smp_can("storage_eject")));
  cJSON_ReplaceItemInObject(o, "available", cJSON_CreateBool(av));
  return o;
}

/* GET /api/v1/storage/holders?mount=/mnt/usb0: read-only; who has a file open on the drive, and whether that would hold an eject back. For a person who wants to
   know why an eject was refused, and for finding out what runs on a console (21.11.2026). */
cJSON *
ps5tm_usbeject_holders_json(const char *mount) {
  cJSON *o = cJSON_CreateObject();
  /* exactly the mount point of a drive (/mnt/usb0 .. /mnt/usb7, /mnt/ext0: nine characters), no path below it */
  if(!mount || strlen(mount) != 9 || ps5tm_drive_slot(mount) < 0) {
    cJSON_AddBoolToObject(o, "ok", 0);
    cJSON_AddStringToObject(o, "error", "mount muss /mnt/usb0 bis /mnt/usb7 oder /mnt/ext0 sein");
    return o;
  }
  cJSON_AddBoolToObject(o, "ok", 1);
  cJSON_AddStringToObject(o, "mount", mount);
  ps5tm_holder_t h[32];
  int nh = ps5tm_drive_holders(mount, h, 32);
  cJSON_AddBoolToObject(o, "known", nh >= 0);
  {
    char diag[320];
    ps5tm_drive_holders_diag(diag, sizeof(diag));
    cJSON_AddStringToObject(o, "diag", diag);
  }
  cJSON *arr = cJSON_AddArrayToObject(o, "holders");
  for(int i = 0; i < nh; i++) {
    int self = 0;
    cJSON *e = cJSON_CreateObject();
    cJSON_AddNumberToObject(e, "pid", h[i].pid);
    cJSON_AddStringToObject(e, "name", h[i].name);
    cJSON_AddNumberToObject(e, "files", h[i].count);
    cJSON_AddStringToObject(e, "sample", h[i].sample);
    cJSON_AddBoolToObject(e, "blocks", holder_blocks(&h[i], &self));
    cJSON_AddBoolToObject(e, "self", self);
    cJSON_AddItemToArray(arr, e);
  }
  int per[9], any = 0;
  ps5tm_drive_counts(per, &any);
  int s = ps5tm_drive_slot(mount);
  cJSON *gate = cJSON_AddObjectToObject(o, "gate");
  cJSON_AddNumberToObject(gate, "readers", s >= 0 ? per[s] : 0);
  cJSON_AddNumberToObject(gate, "readers_any", any);
  cJSON_AddNumberToObject(gate, "file_manager_reads", ps5tm_filemgr_reading());
  return o;
}
