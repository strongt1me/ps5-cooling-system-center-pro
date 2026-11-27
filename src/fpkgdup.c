/* PS5 Cooling & System Center - Pro: a game that Kraken Mount and ShadowMountPlus both manage is unmounted at ShadowMountPlus after it ended (15.11.2026).
 *
 * Kraken Mount (KrakenMount) and ShadowMountPlus register the same title id and mount the same /system_ex/app/<id> (mounters.c names such titles).
 * Wish of the user: when a game that lies at both is started and later ended, it should be unmounted (the "Aushängen" button of its card). Only
 * this case is touched: a game that lies at one mounter alone, and every tile, stay as they are. The unmount is ShadowMountPlus's own
 * (POST /games/unmount, ps5tm_smpremount): the app never unmounts a thing itself. Kraken Mount keeps what it has, it never gets a command.
 *
 * The watcher notes the title that plays (game state, a fresh reading); when the state says "kein Spiel" and a title was seen playing, that title
 * has ended: if it is on the list of mounters.c and ShadowMountPlus holds it mounted, it is unmounted there and the person is told. */
#include "ps5tm.h"
#include "titleid.h"
#include "third_party/cJSON.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define FD_STATE_AGE_MS 15000u   /* a reading older than this says nothing */

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static int g_up;

static int
in_conflict(const char *id) {
  cJSON *arr = ps5tm_mounters_conflicts();
  int hit = 0;
  cJSON *x;
  cJSON_ArrayForEach(x, arr) {
    const cJSON *t = cJSON_GetObjectItemCaseSensitive(x, "title_id");
    const cJSON *o = cJSON_GetObjectItemCaseSensitive(x, "other");
    const cJSON *by = o ? cJSON_GetObjectItemCaseSensitive(o, "by") : NULL;
    if(cJSON_IsString(t) && !strcmp(t->valuestring, id) && cJSON_IsString(by) && !strcmp(by->valuestring, "smp")) hit = 1;
  }
  cJSON_Delete(arr);
  return hit;
}

/* The title that was playing has ended: unmount it at ShadowMountPlus when it lies there too. 1 when it did. */
static int
ended(const char *id) {
  if(!ps5tm_is_game_title_id(id) || ps5tm_is_own_tile(id)) return 0;
  ps5tm_smp_game_t g;
  if(ps5tm_smp_find(id, &g) != 0 || !strcmp(g.source_type, "pkg") || !g.mounted) return 0;
  if(!in_conflict(id)) return 0;
  if(ps5tm_procmgr_title_running(id) == 1) return 0;   /* started again in the meantime: left mounted; its next end is seen anew (audit F-258) */
  char msg[240] = "";
  int st = ps5tm_smpremount(id, "unmount", msg, sizeof(msg));
  if(st != 200) {
    PS5TM_WARN("fpkg_dup_unmount_failed", "%s lag bei Kraken Mount und bei ShadowMountPlus; das Aushängen nach dem Beenden ging nicht: %s", id, msg);
    return 0;
  }
  char name[128];
  char link[96];
  if(ps5tm_library_launch_info(id, name, sizeof(name), link, sizeof(link)) != 0) snprintf(name, sizeof(name), "%s", id);
  PS5TM_INFO("fpkg_dup_unmounted", "%s (%s) lag bei Kraken Mount und bei ShadowMountPlus: nach dem Beenden bei ShadowMountPlus ausgehängt.", name, id);
  ps5tm_notify_plain("„%s“ ist beendet und bei ShadowMountPlus ausgehängt: Es lag auch bei Kraken Mount.", name);
  return 1;
}

/* One look at the game state; `playing` is the title seen playing last ("" none). When the state says "kein Spiel" and a title was playing, that
   title has ended. Not static: the host test calls it. */
void
ps5tm_fpkgdup_step(char playing[12]) {
  if(!ps5tm_gamestate_measured() || ps5tm_gamestate_age_ms() > FD_STATE_AGE_MS) return;
  ps5tm_gamestate_t gs;
  ps5tm_gamestate_get(&gs);
  if(gs.title_id[0]) {                           /* a title plays: remember which */
    snprintf(playing, 12, "%s", gs.title_id);
    return;
  }
  if(!gs.state || strcmp(gs.state, "kein Spiel") != 0 || !playing[0]) return;   /* not told, or nothing played before */
  char id[12];
  snprintf(id, sizeof(id), "%s", playing);
  playing[0] = 0;
  pthread_mutex_lock(&g_lock);
  ended(id);
  pthread_mutex_unlock(&g_lock);
}

static void *
dup_thread(void *arg) {
  (void)arg;
  char playing[12] = "";
  for(;;) {
    sleep(3);
    ps5tm_fpkgdup_step(playing);
  }
  return NULL;
}

void
ps5tm_fpkgdup_start(void) {
  if(g_up) return;
  pthread_t th;
  pthread_attr_t at;
  pthread_attr_init(&at);
  pthread_attr_setstacksize(&at, 512 * 1024);
  pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
  if(pthread_create(&th, &at, dup_thread, NULL) == 0) g_up = 1;
  else PS5TM_WARN("fpkg_dup_thread", "Der Wächter für Spiele, die bei zwei Einhängern liegen, ließ sich nicht starten.");
  pthread_attr_destroy(&at);
}
