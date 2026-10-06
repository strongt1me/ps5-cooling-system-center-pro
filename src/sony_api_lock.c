/* Shared serialization for fragile Sony API calls.
 *
 * Some service calls are safe alone but become flaky under cross-thread
 * overlap (probe thread + request thread). A single recursive lock keeps these
 * sections deterministic without forbidding nested helper calls.
 */

#include <pthread.h>

#include "ps5tm.h"

static pthread_mutex_t g_lock;
static pthread_once_t  g_once = PTHREAD_ONCE_INIT;

static void
sony_lock_init(void) {
  pthread_mutexattr_t attr;
  pthread_mutexattr_init(&attr);
  pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
  pthread_mutex_init(&g_lock, &attr);
  pthread_mutexattr_destroy(&attr);
}

void
ps5tm_sony_api_lock(void) {
  pthread_once(&g_once, sony_lock_init);
  pthread_mutex_lock(&g_lock);
}

/* The same lock without waiting for it: 1 when it was taken (and must be
   released with ps5tm_sony_api_unlock()), 0 when another thread holds it.

   The fan worker uses only this one. Whoever holds the lock is inside a Sony
   service, and the notes in probe.c and tile.c record several of those that
   did not come back for a long time; a thread that queued behind such a call
   would stop regulating the fan for as long as it lasted. */
int
ps5tm_sony_api_trylock(void) {
  pthread_once(&g_once, sony_lock_init);
  return pthread_mutex_trylock(&g_lock) == 0;
}


void
ps5tm_sony_api_unlock(void) {
  pthread_mutex_unlock(&g_lock);
}
