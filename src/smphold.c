/* PS5 Cooling & System Center - Pro: a game the app mounted through ShadowMountPlus and nobody started (14.11.2026).
 *
 * ShadowMountPlus 1.7beta12 has one place for a "prepared" runtime mount (claim_prepared_title in sm_shellcore_service.c). A
 * game mounted by POST /games/mount keeps it until it is started, unmounted, or ShadowMountPlus restarts; every other launch,
 * a game or a tile, is refused meanwhile with EBUSY ("launch mount unavailable ... Device busy", on the console the launch
 * error dialog). Reproduced on 14.11.2026: after the "Einhängen" button of the app, both tiles would not start; "Aushängen"
 * and they did. The first time it was left standing for 4.5 hours, after the rollback of an FPKG test.
 *
 * So the app does not leave such a mount standing. Every game it mounts through ShadowMountPlus (the buttons of the game card,
 * the probe that reads the fakelibs, the AMPR job: all go through ps5tm_smp_call) is written down here, in memory and in a small
 * file (so that a restart of the app, for instance a new ELF, does not forget it). A game that is still mounted and not started
 * two minutes after the app mounted it is unmounted again, with a notice on the console and a line in the log. The two minutes
 * only run while no game plays and none of the app's own jobs runs. Nothing the app did not mount is touched: another way of
 * mounting (ShadowMountPlus's own pages, or its persistent image mounts) is the person's own business. */
#include "ps5tm.h"
#include "smphold.h"
#include "titleid.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define HOLD_MAX      8
#define HOLD_PATH     PS5TM_DATA_DIR "/smp-mounted.txt"
#define HOLD_TMP      PS5TM_DATA_DIR "/smp-mounted.txt.part"
#define HOLD_RETRY_MS 30000u       /* after a refusal, the next try comes this much later */

typedef struct {
  char     id[12];
  uint64_t since_ms;               /* the clock only runs while nothing keeps it back */
} hold_t;

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static hold_t          g_hold[HOLD_MAX];
static int             g_n;
static int             g_up;

/* The real world; a test swaps it (ps5tm_smphold_test_env). */
static ps5tm_smphold_env_t g_env;

static int
env_find(const char *id, int *mounted) {
  ps5tm_smp_game_t g;
  if(ps5tm_smp_find(id, &g) != 0) return -1;
  *mounted = g.mounted;
  return 0;
}

static int
env_smp_up(void) {
  return ps5tm_smp_available(NULL, 0);
}

static int
env_game_running(void) {
  if(!ps5tm_gamestate_measured()) return 1;            /* not known yet: as if one ran, the clock waits */
  ps5tm_gamestate_t gs;
  ps5tm_gamestate_get(&gs);
  return gs.title_id[0] != 0;
}

static int
env_job_running(void) {
  char why[200];
  return ps5tm_app_job_running(why, sizeof(why));
}

static int
env_unmount(const char *id, char *msg, size_t n) {
  return ps5tm_smpremount(id, "unmount", msg, n);
}

static void
env_default(void) {
  g_env.find = env_find;
  g_env.smp_up = env_smp_up;
  g_env.game_running = env_game_running;
  g_env.job_running = env_job_running;
  g_env.unmount = env_unmount;
  g_env.now_ms = ps5tm_mono_ms;
  g_env.notice = NULL;
}

/* An id as ShadowMountPlus manages them: PPSA, CUSA, or a homebrew's FAKE and the like (nine letters and digits). */
static int
id_ok(const char *id) {
  if(!id || strlen(id) != 9) return 0;
  for(int i = 0; i < 9; i++)
    if(!((id[i] >= 'A' && id[i] <= 'Z') || (id[i] >= '0' && id[i] <= '9'))) return 0;
  return !ps5tm_is_own_tile(id);
}

static void
save_locked(void) {
  char buf[HOLD_MAX * 16 + 1];
  size_t k = 0;
  for(int i = 0; i < g_n; i++) k += (size_t)snprintf(buf + k, sizeof(buf) - k, "%s\n", g_hold[i].id);
  if(g_n == 0) {
    unlink(HOLD_PATH);
    return;
  }
  int fd = open(HOLD_TMP, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if(fd < 0) return;
  ssize_t w = write(fd, buf, k);
  int ok = w == (ssize_t)k;
  if(close(fd) != 0) ok = 0;
  if(ok) rename(HOLD_TMP, HOLD_PATH);
  else unlink(HOLD_TMP);
}

static int
index_of_locked(const char *id) {
  for(int i = 0; i < g_n; i++)
    if(!strcmp(g_hold[i].id, id)) return i;
  return -1;
}

static void
drop_locked(int i) {
  g_hold[i] = g_hold[--g_n];
}

void
ps5tm_smphold_note(const char *id, int mounted) {
  if(!id_ok(id)) return;
  if(!g_env.now_ms) env_default();
  pthread_mutex_lock(&g_lock);
  int i = index_of_locked(id);
  int changed = 0;
  if(mounted) {
    if(i < 0 && g_n < HOLD_MAX) {
      memcpy(g_hold[g_n].id, id, 9);                   /* id_ok: nine letters and digits */
      g_hold[g_n].id[9] = 0;
      i = g_n++;
      changed = 1;
    }
    if(i >= 0) g_hold[i].since_ms = g_env.now_ms();
  } else if(i >= 0) {
    drop_locked(i);
    changed = 1;
  }
  if(changed) save_locked();
  pthread_mutex_unlock(&g_lock);
}

/* What the app wrote down before a restart: the clock starts again at the start. */
static void
load(void) {
  FILE *f = fopen(HOLD_PATH, "r");
  if(!f) return;
  char line[32];
  uint64_t now = g_env.now_ms();
  pthread_mutex_lock(&g_lock);
  while(fgets(line, sizeof(line), f) && g_n < HOLD_MAX) {
    line[strcspn(line, "\r\n")] = 0;
    if(!id_ok(line) || index_of_locked(line) >= 0) continue;
    memcpy(g_hold[g_n].id, line, 9);                   /* id_ok: nine letters and digits */
    g_hold[g_n].id[9] = 0;
    g_hold[g_n].since_ms = now;
    g_n++;
  }
  pthread_mutex_unlock(&g_lock);
  fclose(f);
}

int
ps5tm_smphold_count(void) {
  pthread_mutex_lock(&g_lock);
  int n = g_n;
  pthread_mutex_unlock(&g_lock);
  return n;
}

/* One look at everything written down. Returns how many games it unmounted. */
int
ps5tm_smphold_step(void) {
  if(!g_env.now_ms) env_default();
  hold_t cur[HOLD_MAX];
  int n;
  pthread_mutex_lock(&g_lock);
  n = g_n;
  memcpy(cur, g_hold, sizeof(g_hold[0]) * (size_t)n);
  pthread_mutex_unlock(&g_lock);
  if(n == 0) return 0;
  if(!g_env.smp_up()) return 0;                        /* ShadowMountPlus not answering: nothing is known, nothing is done */

  /* While a game plays (an independent title mounted then does not take the place), or one of the app's jobs works with
     games, the two minutes wait: they start again afterwards. */
  int paused = g_env.game_running() || g_env.job_running();
  uint64_t now = g_env.now_ms();
  int done = 0;
  for(int i = 0; i < n; i++) {
    int mounted = 0;
    if(g_env.find(cur[i].id, &mounted) != 0 || !mounted) {   /* released meanwhile: started and ended, or ShadowMountPlus restarted */
      pthread_mutex_lock(&g_lock);
      int k = index_of_locked(cur[i].id);
      if(k >= 0) { drop_locked(k); save_locked(); }
      pthread_mutex_unlock(&g_lock);
      continue;
    }
    if(paused) {
      pthread_mutex_lock(&g_lock);
      int k = index_of_locked(cur[i].id);
      if(k >= 0) g_hold[k].since_ms = now;
      pthread_mutex_unlock(&g_lock);
      continue;
    }
    if(now - cur[i].since_ms < PS5TM_SMPHOLD_GRACE_MS) continue;

    char msg[300] = "";
    int http = g_env.unmount(cur[i].id, msg, sizeof(msg));
    if(http == 200) {
      /* ps5tm_smp_call has already written it off (the unmount went through it); this is for a test with its own unmount */
      pthread_mutex_lock(&g_lock);
      int k = index_of_locked(cur[i].id);
      if(k >= 0) { drop_locked(k); save_locked(); }
      pthread_mutex_unlock(&g_lock);
      PS5TM_INFO("smp_hold_released",
                 "%s war eingehängt, aber nicht gestartet. Die App hat es nach 2 Minuten ausgehängt, damit andere Spiele und Kacheln starten.",
                 cur[i].id);
      if(g_env.notice) g_env.notice(cur[i].id);
      else ps5tm_notify_plain("%s war eingehängt, aber nicht gestartet: Die App hat es ausgehängt, damit andere Spiele und Kacheln starten.", cur[i].id);
      done++;
    } else {
      PS5TM_WARN("smp_hold_failed", "%s ist eingehängt, aber nicht gestartet, und ließ sich nicht aushängen: %s", cur[i].id, msg);
      pthread_mutex_lock(&g_lock);
      int k = index_of_locked(cur[i].id);
      if(k >= 0) g_hold[k].since_ms = now - (PS5TM_SMPHOLD_GRACE_MS - HOLD_RETRY_MS);   /* the next try in HOLD_RETRY_MS */
      pthread_mutex_unlock(&g_lock);
    }
  }
  return done;
}

void
ps5tm_smphold_test_env(const ps5tm_smphold_env_t *env) {
  g_env = *env;
}

void
ps5tm_smphold_test_reset(void) {
  pthread_mutex_lock(&g_lock);
  g_n = 0;
  pthread_mutex_unlock(&g_lock);
}

static void *
hold_thread(void *arg) {
  (void)arg;
  for(;;) {
    sleep(10);
    ps5tm_smphold_step();
  }
  return NULL;
}

void
ps5tm_smphold_start(void) {
  if(g_up) return;
  if(!g_env.now_ms) env_default();
  load();
  pthread_t th;
  pthread_attr_t at;
  pthread_attr_init(&at);
  pthread_attr_setstacksize(&at, 512 * 1024);
  pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
  if(pthread_create(&th, &at, hold_thread, NULL) == 0) g_up = 1;
  else PS5TM_WARN("smp_hold_thread", "Der Wächter für eingehängte, nicht gestartete Spiele ließ sich nicht starten.");
  pthread_attr_destroy(&at);
}
