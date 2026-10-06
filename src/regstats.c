/* Values read from the system registry. Currently exactly one: the console's
 * own name.
 *
 * ‼ Read only. Nothing here ever writes. The registry holds the console's
 * identity and account state, and a wrong write is not something that can be
 * undone from a web page.
 *
 * Sources, both local and both necessary:
 *   - ps5-payload-dev/regdump (regmgr.h) names 1187 keys with their type and
 *     size. Type 0 = integer, 1 = string, 2 = binary.
 *   - ps5-payload-dev/offact (offact.c:20-22) links -lSceRegMgr and reads a
 *     64-bit value, which is where the signatures come from rather than guesses.
 *
 * ── The drive statistics are NOT here, and that is a finding, not an omission.
 *
 * This file was written to expose SYSTEM_HDD_WRITE_STATS — lifetime bytes
 * read and written, split system/game and internal/external. It would have
 * been the one real wear figure a monitoring app can show. Measured on
 * 01.08.2026, FW 12.00:
 *
 *   8 aggregate counters      read rc=0, all zero
 *   3 per-device slots        read rc=0, empty name, zero bytes
 *
 * Eleven keys, every read reporting success, every value empty. That alone
 * would not prove much — it is also what a broken read looks like — so three
 * controls were read the same way, and they answered:
 *
 *   SYSTEM_language     rc=0, 4
 *   USER_01_16_user_name(1) rc=0, "JBuser2"  (matches sceUserServiceGetUserName)
 *   SYSTEM_nickname     rc=0, "PS5-615"
 *
 * So the mechanism is sound and this firmware simply does not maintain those
 * counters. Same behaviour as the fields removed in 1.9.5 (boot cause, product
 * shape, RAM/VRAM): the call works, the value is not there. Showing eight
 * zeros would be worse than showing nothing.
 *
 * ⚠ One thing did turn out to be our own fault, and it is the reason the
 * console name looked broken at first: SYSTEM_nickname is declared 65 bytes
 * and the first attempt passed a 32-byte buffer. A short buffer is refused.
 * Pass the size regmgr.h declares, not the size of your struct field.
 */

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ps5tm.h"

#ifndef PS5TM_HOST_TEST
/* Weakly linked, like every other Sony export here: a missing symbol must
   leave a null pointer, not kill the payload before main(). */
extern int sceRegMgrGetInt(long key, int *out) __attribute__((weak));
extern int sceRegMgrGetStr(int key, char *out, size_t size) __attribute__((weak));
#endif

#define REG_NICKNAME      33882112    /* SYSTEM_nickname, type 1, size 65 */
#define REG_NICKNAME_SIZE 65
#define REG_LANGUAGE      33685504    /* SYSTEM_language, type 0, size 4 */
#define REG_TIME_ZONE     83951616    /* DATE_time_zone, type 0, size 4 */
#define REG_TZ_OFFSET     84410368    /* DATE_timezone_offset, type 0, size 4 */
#define REG_REGION_CODE   58723328    /* SECURITY_PARENTAL_game_age_limit_region */
#define REG_REGION_SIZE   3

static ps5tm_regstats_t g_stats;
static pthread_mutex_t  g_lock = PTHREAD_MUTEX_INITIALIZER;


void
ps5tm_regstats_refresh(void) {
#ifdef PS5TM_HOST_TEST
  return;
#else
  ps5tm_regstats_t s;
  memset(&s, 0, sizeof(s));

  int rc = -1;
  if(sceRegMgrGetStr || sceRegMgrGetInt) {
    ps5tm_sony_api_lock();
    if(sceRegMgrGetStr)
      rc = sceRegMgrGetStr(REG_NICKNAME, s.nickname, REG_NICKNAME_SIZE);

    if(sceRegMgrGetInt) {
      int v = 0;
      if(sceRegMgrGetInt(REG_LANGUAGE, &v) == 0) {
        s.language_valid = 1;
        s.language_code = v;
      }
      if(sceRegMgrGetInt(REG_TIME_ZONE, &v) == 0) {
        s.time_zone_valid = 1;
        s.time_zone_code = v;
      }
      if(sceRegMgrGetInt(REG_TZ_OFFSET, &v) == 0) {
        s.timezone_offset_valid = 1;
        s.timezone_offset_min = v;
      }
    }

    if(sceRegMgrGetStr) {
      char region[REG_REGION_SIZE + 1];
      memset(region, 0, sizeof(region));
      if(sceRegMgrGetStr(REG_REGION_CODE, region, REG_REGION_SIZE) == 0 &&
         region[0]) {
        s.region_valid = 1;
        snprintf(s.region_code, sizeof(s.region_code), "%s", region);
      }
    }
    ps5tm_sony_api_unlock();
  }
  if(rc != 0) s.nickname[0] = '\0';
  s.nickname[sizeof(s.nickname) - 1] = '\0';
  s.valid = (s.nickname[0] != '\0');

  pthread_mutex_lock(&g_lock);
  g_stats = s;
  pthread_mutex_unlock(&g_lock);

  static int logged = 0;
  if(!logged) {
    logged = 1;
    if(!sceRegMgrGetStr && !sceRegMgrGetInt)
      PS5TM_INFO("regstats_unavailable",
                 "Registry-Zugriff nicht verfügbar — der Konsolenname bleibt "
                 "leer.");
    else if(s.valid)
      PS5TM_INFO("regstats_ready", "Konsolenname aus der Registry: \"%s\".",
                 s.nickname);
    else
      PS5TM_INFO("regstats_empty",
                 "Registry antwortet (rc=%d), führt aber keinen Konsolennamen.",
                 rc);
  }
#endif
}


void
ps5tm_regstats_get(ps5tm_regstats_t *out) {
  if(!out) return;
  pthread_mutex_lock(&g_lock);
  *out = g_stats;
  pthread_mutex_unlock(&g_lock);
}
