/* PS5 Cooling & System Center - Pro: titles two mounters want at once (14.11.2026).
 *
 * Kraken Mount (FPKGMount), ShadowMountPlus and PS4Mount each register the games they find with the console and mount them: all of them
 * write /user/app/<title id> and mount /system_ex/app/<title id>. A title that lies with two of them is fought over. Seen on the test
 * console with Alex Kidd: a .pkg for Kraken Mount and an image for ShadowMountPlus. Kraken Mount took ShadowMountPlus's mount away
 * ("not ours, or stale; unmounting it"), got "Device busy" while the title was running, and its tile differed from the other one (Kraken
 * Mount registers the param.json as it is, ShadowMountPlus rewrites it).
 *
 * The app cannot settle that for the person (it would have to move or delete their files), so it says it: the game list and the page of
 * FPKGMount name every title that Kraken Mount lists and that another mounter manages too. Where the title lies at the other mounter is
 * taken from that one's own list: for ShadowMountPlus a folder or an image (an installed package is something else), for PS4Mount the
 * mount table. Nothing here changes anything. */
#include "ps5tm.h"
#include "ps4m.h"
#include "third_party/cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

cJSON *
ps5tm_mounters_conflicts(void) {
  cJSON *arr = cJSON_CreateArray();
  ps5tm_kraken_game_t *kg = calloc(PS5TM_KRAKEN_MAX, sizeof(*kg));          /* 120 KB: not on a thread's stack */
  if(!kg) return arr;
  int n = ps5tm_kraken_games(kg, PS5TM_KRAKEN_MAX);
  for(int i = 0; i < n; i++) {
    const char *by = NULL;
    char path[256] = "";
    ps5tm_smp_game_t sg;
    if(ps5tm_smp_find(kg[i].title_id, &sg) == 0 && strcmp(sg.source_type, "pkg") != 0) {
      by = "smp";
      snprintf(path, sizeof(path), "%s", sg.path);
    } else if(ps4m_title_managed(kg[i].title_id)) {
      by = "ps4mount";
    }
    if(!by) continue;
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "title_id", kg[i].title_id);
    cJSON_AddStringToObject(o, "name", kg[i].name[0] ? kg[i].name : kg[i].title_id);
    cJSON *k = cJSON_AddObjectToObject(o, "kraken");
    cJSON_AddStringToObject(k, "path", kg[i].path);
    cJSON_AddStringToObject(k, "kind", kg[i].kind);
    cJSON *w = cJSON_AddObjectToObject(o, "other");
    cJSON_AddStringToObject(w, "by", by);
    cJSON_AddStringToObject(w, "path", path);
    cJSON_AddItemToArray(arr, o);
  }
  free(kg);
  return arr;
}
