/* Keeping the console awake while a long copy or conversion runs
 * (03.10.2026).
 *
 * Copying a big game takes an hour and more, mostly with nobody touching the
 * controller — long enough for the console's own timer to send it into rest
 * mode, which stops the drives under the copy. While a job holds the guard,
 * the timer is reset every ten seconds, the way PS5 Game Compressor's power
 * guard does it (idea only; its code has no licence). Nothing else is
 * touched: the screen may still dim, and the person can still choose rest
 * mode themselves — the job then fails like any other interruption and
 * removes what it wrote.
 *
 * Tried first with a throwaway payload under this app's identity (FW 12.00,
 * 03.10.2026): sceShellCoreUtilResetAutoPowerDownTimer and
 * sceSystemServicePowerTick both answered 0 and nothing went wrong. The
 * remaining time could not be read back (GetAutoPowerDownRemainingSeconds
 * answered 0 and wrote nothing; automatic rest mode may be switched off on
 * that console), so the effect itself is not measured.
 *
 * The calls go through the Sony lock, but only when it is free: whoever holds
 * it may be inside a service call that takes long, and a missed tick is
 * caught up a second later. */

#include <pthread.h>
#include <unistd.h>

#include "ps5tm.h"

#define TICK_MS 10000

#ifdef PS5TM_HOST_TEST
int g_powerguard_ticks;          /* for the host tests */
static int sceShellCoreUtilResetAutoPowerDownTimer(void) { g_powerguard_ticks++; return 0; }
static int sceSystemServicePowerTick(void) { return 0; }
#else
int sceShellCoreUtilResetAutoPowerDownTimer(void);
int sceSystemServicePowerTick(void);
#endif

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_once_t  g_once = PTHREAD_ONCE_INIT;
static int             g_holders;
static int             g_thread_ok;

static void *
guard_thread(void *arg) {
  (void)arg;
  uint64_t next = 0;
  int      was = 0, logged = 0;
  for(;;) {
    pthread_mutex_lock(&g_lock);
    int held = g_holders > 0;
    pthread_mutex_unlock(&g_lock);

    uint64_t now = ps5tm_mono_ms();
    if(held && !was) { next = now; logged = 0; }
    if(!held && was)
      PS5TM_INFO("power_guard_off", "Kein langer Vorgang mehr: Der automatische "
                 "Ruhemodus ist wieder frei.");
    was = held;

    if(held && now >= next && ps5tm_sony_api_trylock()) {
      int r1 = sceShellCoreUtilResetAutoPowerDownTimer();
      int r2 = sceSystemServicePowerTick();
      ps5tm_sony_api_unlock();
      if(!logged) {
        PS5TM_INFO("power_guard_on", "Langer Vorgang: Die Uhr für den automatischen "
                   "Ruhemodus wird alle %d s zurückgesetzt (0x%08X, 0x%08X).",
                   TICK_MS / 1000, (unsigned)r1, (unsigned)r2);
        logged = 1;
      }
      next = now + TICK_MS;
    }
    sleep(1);
  }
  return NULL;
}

static void
guard_start(void) {
  pthread_t t;
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
  g_thread_ok = pthread_create(&t, &attr, guard_thread, NULL) == 0;
  pthread_attr_destroy(&attr);
  if(!g_thread_ok)
    PS5TM_WARN("power_guard_unavailable", "Kein Thread für die Ruhemodus-Sperre: "
               "Lange Vorgänge laufen ohne sie.");
}

void
ps5tm_powerguard_hold(void) {
  pthread_once(&g_once, guard_start);
  pthread_mutex_lock(&g_lock);
  g_holders++;
  pthread_mutex_unlock(&g_lock);
}

void
ps5tm_powerguard_release(void) {
  pthread_mutex_lock(&g_lock);
  if(g_holders > 0) g_holders--;
  pthread_mutex_unlock(&g_lock);
}
