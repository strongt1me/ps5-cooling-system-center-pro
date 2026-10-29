/* PS5 Cooling & System Center - Pro: a USB drive ejected safely, through ShadowMountPlus (29.10.2026).
 *
 * ShadowMountPlus 1.7beta10 can eject a USB drive (POST /api/v1/storage/eject, capability storage_eject). It checks
 * its own mounts, images and the running game, but knows nothing of this app's jobs: an eject in the middle of a copy,
 * a conversion or a backup would cut it off. Here the app asks it only when none of its own jobs runs and no folder of
 * the drive is mounted elsewhere (PS4Mount games are nullfs mounts of their ps4/ folder; cutting those off leaves the
 * tiles of running games without their data). The eject from ShadowMountPlus's own page cannot be held back: its checks
 * look only at its own registry (sm_api_service.c usb_eject_block_reason / check_usb_eject_mount).
 *
 * One eject at a time; a thread follows ShadowMountPlus's storage job until the drive is gone or the job failed. */
#include "ps5tm.h"
#include "third_party/cJSON.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/param.h>
#include <unistd.h>

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static struct {
  int      running;
  char     mount[16];
  char     state[12];           /* "" never, running, done, failed */
  char     message[240];
  uint64_t at_ms;
} g_job;

/* /mnt/usb0../mnt/usb7 and /mnt/ext0: what ShadowMountPlus calls ejectable (the M.2 drive /mnt/ext1 is not). */
int
ps5tm_usbeject_path_ok(const char *mount) {
  if(!mount) return 0;
  if(!strcmp(mount, "/mnt/ext0")) return 1;
  return !strncmp(mount, "/mnt/usb", 8) && mount[8] >= '0' && mount[8] <= '7' && !mount[9];
}

/* The app's own jobs, any of them: most read or write USB drives, and which drive a job uses is not always known
   beforehand (a copy from the SSD to usb1, a backup, the file manager). 1 with the reason. */
static int
app_job_running(char *why, size_t n) {
  static const struct { int (*busy)(void); const char *what; } jobs[] = {
    { ps5tm_gamecopy_busy,    "Es läuft eine Kopie oder ein Dump" },
    { ps5tm_gameconvert_busy, "Es läuft eine Konvertierung" },
    { ps5tm_gamemove_busy,    "ShadowMountPlus verschiebt oder entpackt gerade ein Spiel für die App" },
    { ps5tm_gamedelete_busy,  "Es wird ein Spiel oder eine Sicherung gelöscht" },
    { ps5tm_filemgr_busy,     "Der Dateimanager arbeitet (Kopieren, Verschieben oder Löschen)" },
    { ps5tm_saves_busy,       "Es läuft ein Vorgang mit den Spielständen" },
    { ps5tm_pkginst_busy,     "Es wird ein Paket installiert" },
    { ps5tm_pkgsplit_busy,    "Es wird ein Paket geteilt" },
    { ps5tm_pkgscan_busy,     "Die Pakete werden gerade gesucht" },
    { ps5tm_ampr_busy,        "Es wird ein AMPR-Index gebaut" },
    { ps5tm_imgjob_busy,      "Es wird ein Abbild bearbeitet" },
    { ps5tm_disc_busy,        "Es wird ein Abbild der Disc angelegt" },
  };
  for(size_t i = 0; i < sizeof(jobs) / sizeof(jobs[0]); i++)
    if(jobs[i].busy()) {
      snprintf(why, n, "%s. Solange ein Auftrag der App läuft, wirft die App kein Laufwerk aus.", jobs[i].what);
      return 1;
    }
  return 0;
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
             "nehmen; erst mit „Nicht mehr von USB spielen“ herausnehmen oder die Konsole neu starten.", ps4, first);
    return 1;
  }
  if(other) {
    snprintf(why, n, "Ein Ordner dieses Laufwerks ist an anderer Stelle eingehängt (%s). Erst das Spiel oder Programm "
             "dahinter beenden.", first);
    return 1;
  }
  return 0;
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
    if(ok) snprintf(msg, sizeof(msg), "%s ist ausgeworfen und kann abgezogen werden.", f->label);
    else snprintf(msg, sizeof(msg), "%s wurde nicht ausgeworfen: %s", f->label,
                  why ? why : cJSON_IsString(re) && re->valuestring[0] ? re->valuestring : "ShadowMountPlus lehnte ab.");
    cJSON_Delete(s);
    PS5TM_INFO(ok ? "usb_ejected" : "usb_eject_refused", "%s", msg);
    ps5tm_notify_plain("%s", msg);
    finish(ok ? "done" : "failed", msg);
    free(f);
    return NULL;
  }
  snprintf(msg, sizeof(msg), "Vom Auswerfen von %s kam keine Rückmeldung. Unter System nachsehen, ob es noch da ist.", f->label);
  PS5TM_WARN("usb_eject_lost", "%s", msg);
  finish("failed", msg);
  free(f);
  return NULL;
}

/* Starts the eject of mount. 202 started, otherwise the HTTP status of the refusal; msg says why or what happens. */
int
ps5tm_usbeject_start(const char *mount, char *msg, size_t n) {
  if(!ps5tm_usbeject_path_ok(mount)) {
    snprintf(msg, n, "Auswerfen geht nur bei USB-Laufwerken.");
    return 400;
  }
  if(!ps5tm_smp_can("storage_eject")) {
    snprintf(msg, n, "Auswerfen braucht ShadowMountPlus ab 1.7beta10; es läuft nicht oder ist älter.");
    return 409;
  }
  char label[24];
  if(!strcmp(mount, "/mnt/ext0")) snprintf(label, sizeof(label), "Erweiterter Speicher");
  else snprintf(label, sizeof(label), "USB-Speicher %d", mount[8] - '0' + 1);

  pthread_mutex_lock(&g_lock);
  int busy = g_job.running;
  if(!busy) {
    g_job.running = 1;
    snprintf(g_job.mount, sizeof(g_job.mount), "%s", mount);
    snprintf(g_job.state, sizeof(g_job.state), "running");
    snprintf(g_job.message, sizeof(g_job.message), "%s wird ausgeworfen …", label);
    g_job.at_ms = ps5tm_now_ms();
  }
  pthread_mutex_unlock(&g_lock);
  if(busy) {
    snprintf(msg, n, "Es wird schon ein Laufwerk ausgeworfen.");
    return 409;
  }
  char why[240];
  if(app_job_running(why, sizeof(why)) || drive_in_use(mount, why, sizeof(why))) {
    snprintf(msg, n, "%s wurde nicht ausgeworfen: %s", label, why);
    PS5TM_INFO("usb_eject_held", "%s", msg);
    finish("failed", msg);
    return 409;
  }

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
  cJSON *body = cJSON_CreateObject();
  cJSON_AddStringToObject(body, "path", mount);
  cJSON_AddStringToObject(body, "source", source);
  cJSON_AddStringToObject(body, "mount_id", mount_id);
  cJSON *ans = ps5tm_smp_call("/storage/eject", body, 8000, err, sizeof(err));
  cJSON_Delete(body);
  if(!ans) {
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
    snprintf(msg, n, "%s: Auswerfen angestoßen, die App kann es aber nicht verfolgen.", label);
    finish("failed", msg);
    return 202;
  }
  pthread_attr_destroy(&at);
  PS5TM_INFO("usb_eject_started", "%s wird über ShadowMountPlus ausgeworfen.", label);
  snprintf(msg, n, "%s wird ausgeworfen …", label);
  return 202;
}

/* {"state":"","running":…,"mount":…,"message":…,"at_ms":…}; state "" = never asked since the start of the app. */
cJSON *
ps5tm_usbeject_json(void) {
  cJSON *o = cJSON_CreateObject();
  pthread_mutex_lock(&g_lock);
  cJSON_AddBoolToObject(o, "ok", 1);
  cJSON_AddBoolToObject(o, "available", 0);
  cJSON_AddStringToObject(o, "state", g_job.state);
  cJSON_AddBoolToObject(o, "running", g_job.running);
  cJSON_AddStringToObject(o, "mount", g_job.mount);
  cJSON_AddStringToObject(o, "message", g_job.message);
  cJSON_AddNumberToObject(o, "at_ms", (double)g_job.at_ms);
  pthread_mutex_unlock(&g_lock);
  cJSON_ReplaceItemInObject(o, "available", cJSON_CreateBool(ps5tm_smp_can("storage_eject")));
  return o;
}
