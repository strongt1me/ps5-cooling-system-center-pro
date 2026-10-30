/* PS5 Cooling & System Center - Pro: one game mounted, unmounted or mounted afresh through ShadowMountPlus (30.10.2026).
 *
 * ShadowMountPlus 1.7beta10 mounts and unmounts a managed game on request (POST /api/v1/games/mount and
 * /games/unmount, capabilities mount_game and unmount_game). From a game's details the user can take it out and put it
 * back: it helps when a game hangs or, after copying, moving or deleting, should be found anew. "remount" is both in
 * one. Never while that game runs (its files would go from under it) and never while one of the app's own jobs runs
 * (a copy or conversion may read the very image); nothing is asked twice, a refusal says why. */
#include "ps5tm.h"
#include "third_party/cJSON.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int
smp_post(const char *route, const char *title_id, char *err, size_t n) {
  cJSON *b = cJSON_CreateObject();
  cJSON_AddStringToObject(b, "title_id", title_id);
  cJSON *r = ps5tm_smp_call(route, b, 30000, err, n);
  cJSON_Delete(b);
  if(!r) return -1;
  cJSON_Delete(r);
  return 0;
}

int
ps5tm_smpremount(const char *title_id, const char *action, char *msg, size_t n) {
  const int mount = !strcmp(action, "mount"), unmount = !strcmp(action, "unmount"), remount = !strcmp(action, "remount");
  if(!title_id || !title_id[0] || (!mount && !unmount && !remount)) {
    snprintf(msg, n, "Unbekannter Auftrag.");
    return 400;
  }
  if(!ps5tm_smp_available(NULL, 0) || !ps5tm_smp_can("mount_game") || !ps5tm_smp_can("unmount_game")) {
    snprintf(msg, n, "Aus- und Einhängen braucht ShadowMountPlus (ab 1.7beta10); es läuft nicht oder kann das nicht.");
    return 409;
  }
  ps5tm_smp_game_t g;
  if(ps5tm_smp_find(title_id, &g) != 0 || !strcmp(g.source_type, "pkg")) {
    snprintf(msg, n, "%s wird nicht von ShadowMountPlus eingehängt (installiertes Paket oder nicht in seiner Liste).", title_id);
    return 409;
  }
  if(!g.available && !unmount) {
    snprintf(msg, n, "Die Quelle von %s ist gerade nicht erreichbar (Laufwerk abgezogen?). Einhängen geht erst, wenn sie wieder da ist.",
             title_id);
    return 409;
  }
  ps5tm_gamestate_t gs;
  ps5tm_gamestate_get(&gs);
  if(!strcmp(gs.title_id, title_id)) {
    snprintf(msg, n, "%s läuft gerade. Erst das Spiel beenden, dann aus- oder einhängen.", title_id);
    return 409;
  }
  char why[200];
  if(ps5tm_app_job_running(why, sizeof(why))) {
    snprintf(msg, n, "%s. Solange ein Auftrag der App läuft, hängt die App kein Spiel aus oder ein.", why);
    return 409;
  }

  char err[200] = "";
  if((unmount || remount) && g.mounted) {
    if(smp_post("/games/unmount", title_id, err, sizeof(err)) != 0) {
      ps5tm_smp_forget();
      snprintf(msg, n, "ShadowMountPlus hat %s nicht ausgehängt: %s", title_id, err);
      PS5TM_WARN("smp_unmount_failed", "%s", msg);
      return 502;
    }
    PS5TM_INFO("smp_unmounted", "%s über ShadowMountPlus ausgehängt.", title_id);
  }
  if(mount || remount) {
    if(remount && g.mounted) sleep(1); /* the mount point is released a moment after the answer */
    if(smp_post("/games/mount", title_id, err, sizeof(err)) != 0) {
      ps5tm_smp_forget();
      ps5tm_library_forget();
      snprintf(msg, n, "%sShadowMountPlus hat %s nicht eingehängt: %s",
               remount && g.mounted ? "Ausgehängt, aber " : "", title_id, err);
      PS5TM_WARN("smp_mount_failed", "%s", msg);
      return 502;
    }
    PS5TM_INFO("smp_mounted", "%s über ShadowMountPlus %s.", title_id, remount ? "neu eingehängt" : "eingehängt");
  }
  ps5tm_smp_forget();
  ps5tm_library_forget(); /* the next list shows the new state */
  snprintf(msg, n, "%s", unmount ? (g.mounted ? "Ausgehängt." : "War schon ausgehängt.")
                     : remount ? "Neu eingehängt."
                               : "Eingehängt.");
  return 200;
}
