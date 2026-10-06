/* Background collection of everything that talks to a system service.
 *
 * A web request must never call into Sony's services. They can block for a
 * long time or forever — sceKernelLoadStartModule() and the LNC/IPMI calls
 * behind sceSystemServiceGetAppIdOfRunningBigApp() both did on FW 12.00 —
 * and a request thread that blocks inside module loading takes the runtime
 * linker's lock with it. When that happened the whole server wedged after a
 * single request: the page had loaded once, then nothing answered again.
 *
 * So the rule is now structural rather than a matter of care: one background
 * thread does all of it on a timer, the request path only ever copies the
 * last result. A service that hangs costs one thread and stale values in one
 * card — not the dashboard.
 */

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "ps5tm.h"

#define GAME_INTERVAL_SEC 4
#define SYS_INTERVAL_SEC  5
#define PAD_INTERVAL_SEC  15
/* Lifetime byte counters move slowly; asking once a minute is generous. */
#define REG_INTERVAL_SEC  60

static pthread_t g_thread;
static int       g_started = 0;


/* Which of the newer probes are allowed to run.
 *
 * On the test console the web server stopped answering entirely as soon as
 * these were introduced, and the failure was not in the request path: some
 * call in here blocks while holding a process-wide lock, so every other
 * thread stops the moment it allocates or resolves a symbol. Which call it is
 * has not been established, so they are off by default and each can be turned
 * on separately from the settings page to find out.
 *
 * Off means one empty card. On, in the wrong combination, meant no dashboard
 * at all — so off is the honest default until it is known. */
static int
probe_allowed(unsigned bit) {
  ps5tm_config_lock();
  unsigned mask = g_config.probe_mask;
  ps5tm_config_unlock();
  return (mask & bit) != 0;
}


static void *
probe_worker(void *arg) {
  (void)arg;

  unsigned tick = 0;
  for(;;) {
    if(tick % GAME_INTERVAL_SEC == 0 && probe_allowed(PS5TM_PROBE_GAME))
      ps5tm_gamestate_refresh();
    if(tick % SYS_INTERVAL_SEC == 0)
      ps5tm_sysinfo_refresh();            /* sensors and storage only */
    if(tick % PAD_INTERVAL_SEC == 0 && probe_allowed(PS5TM_PROBE_PAD))
      ps5tm_pad_refresh();

     /* Executed here on purpose: pad APIs may block, and this thread is the
       sandbox for such calls so thermal control and web requests stay alive. */
     ps5tm_pad_lightbar_service();

    /* Ungated, like the sensors: sceRegMgr is a plain library read, not one of
       the shell services this thread exists to keep away from the request
       path. It runs here anyway because "plain" is an assumption until it has
       run a few thousand times, and here a hang costs one stale card. */
    if(tick % REG_INTERVAL_SEC == 0)
      ps5tm_regstats_refresh();

    /* Clock table, out of the kernel log. Ungated for the same reason as the
       sensors: it is a sysctl read, not a shell service. Every 15 s because
       the table only changes when the power mode does. */
    if(tick % 15 == 0)
      ps5tm_clocks_refresh();

    /* Never gated: it only runs when the user asked for it. */
    ps5tm_tile_service();

    /* Every second, so the notification follows a double press of the
       microphone button closely. It is a sysctl read of the kernel log, like
       the clock table, and gated by its own setting inside, not by
       probe_mask: it calls no shell service. */
    ps5tm_micbutton_poll();

    tick++;
    sleep(1);
  }
  return NULL;
}


void
ps5tm_probe_start(void) {
  if(g_started) return;
  g_started = 1;

  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

  if(pthread_create(&g_thread, &attr, probe_worker, NULL) != 0) {
    PS5TM_WARN("probe_thread_failed",
               "Hintergrundabfrage konnte nicht gestartet werden — System- "
               "und Spielangaben bleiben leer. Die Lüftersteuerung läuft "
               "davon unabhängig weiter.");
    g_started = 0;
  }
  pthread_attr_destroy(&attr);
}
