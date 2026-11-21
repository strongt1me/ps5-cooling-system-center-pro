/* PS5 Cooling & System Center - Pro: the drive gate (21.11.2026).
 *
 * Why. The console ejects a USB drive with unmount(path, MNT_FORCE): its service (SceAutoMounter, the method ShadowMountPlus and this app call) checks
 * only that the device is known and that a file system is mounted there; it asks nobody whether a process still has a file, a folder or a request under
 * way on the drive (firmware analysis of 21.11.2026, the same code in every ShellCore from 3.20 to 12.70). Whatever the app still reads from the drive at
 * that moment is cut off under its feet, and on the console of one user that ended in a kernel panic, again and again, with an eject started from the file
 * manager. Until now the app only REFUSED new reads once an eject was claimed (ps5tm_usbeject_settling); it never WAITED for the reads that had begun
 * before. That is what this gate adds, and it is the same idea as with_read() in filemgr.c, for every other reader of the app.
 *
 * How. A reader that touches a removable drive brackets that with ps5tm_drive_enter(path) .. ps5tm_drive_leave(token). enter counts first and asks
 * second (ps5tm_usbeject_settling), so that either the eject sees the reader (it is counted) or the reader sees the eject (it backs out): whichever
 * came second notices the first. The eject claims the drive, then ps5tm_drive_wait_idle() waits until the count of that drive (and of the readers that walk
 * every drive: ps5tm_drive_enter_any) is zero. A reader that is refused does not read; it uses what it knew before.
 *
 * Only short stretches belong inside the brackets (one directory, one stat): a walk down a whole tree is bracketed per directory or checks
 * ps5tm_usbeject_active() itself, otherwise the eject would wait for the whole walk. */
#include "ps5tm.h"

#include <errno.h>
#include <pthread.h>
#include <string.h>
#include <time.h>

#define SLOTS     9            /* usb0..usb7, ext0 */
#define TOKEN_ANY (SLOTS + 1)

static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_cv = PTHREAD_COND_INITIALIZER;
static int             g_n[SLOTS];
static int             g_any;

/* usb0..usb7 -> 0..7, ext0 -> 8, for the drive `path` lies on (or is); -1 for anything else (the internal storage, /user, /data ...). */
int
ps5tm_drive_slot(const char *path) {
  if(!path) return -1;
  if(!strncmp(path, "/mnt/ext0", 9) && (path[9] == '/' || path[9] == 0)) return 8;
  if(!strncmp(path, "/mnt/usb", 8) && path[8] >= '0' && path[8] <= '7' && (path[9] == '/' || path[9] == 0)) return path[8] - '0';
  return -1;
}

static void
back_out(int *counter) {
  pthread_mutex_lock(&g_mu);
  (*counter)--;
  pthread_cond_broadcast(&g_cv);
  pthread_mutex_unlock(&g_mu);
}

/* A reader of `path` goes in. -1: not a removable drive, nothing is counted, go ahead (leave(-1) is a no-op); 0: the drive is being ejected or has
   just been, do not touch it; >0: counted, go ahead, and call ps5tm_drive_leave() with this token when done. */
int
ps5tm_drive_enter(const char *path) {
  int s = ps5tm_drive_slot(path);
  if(s < 0) return -1;
  pthread_mutex_lock(&g_mu);
  g_n[s]++;
  pthread_mutex_unlock(&g_mu);
  if(ps5tm_usbeject_settling(path)) {
    back_out(&g_n[s]);
    return 0;
  }
  return s + 1;
}

/* A reader that may look at ANY drive (a library walk, the search for duplicate images): refused while any drive is being ejected or settles. */
int
ps5tm_drive_enter_any(void) {
  pthread_mutex_lock(&g_mu);
  g_any++;
  pthread_mutex_unlock(&g_mu);
  if(ps5tm_usbeject_active()) {
    back_out(&g_any);
    return 0;
  }
  return TOKEN_ANY;
}

void
ps5tm_drive_leave(int token) {
  if(token <= 0) return;
  pthread_mutex_lock(&g_mu);
  if(token == TOKEN_ANY) { if(g_any > 0) g_any--; }
  else if(token <= SLOTS) { if(g_n[token - 1] > 0) g_n[token - 1]--; }
  pthread_cond_broadcast(&g_cv);
  pthread_mutex_unlock(&g_mu);
}

/* The eject of `mount` is claimed (ps5tm_usbeject_settling is 1 for it): waits until no reader of that drive and no all-drive reader is inside the
   gate. 0: idle; -1: still busy after `ms`. Not a drive: 0. */
int
ps5tm_drive_wait_idle(const char *mount, int ms) {
  int s = ps5tm_drive_slot(mount);
  if(s < 0) return 0;
  struct timespec until;
  clock_gettime(CLOCK_REALTIME, &until);
  until.tv_sec += ms / 1000;
  until.tv_nsec += (long)(ms % 1000) * 1000000L;
  if(until.tv_nsec >= 1000000000L) { until.tv_sec++; until.tv_nsec -= 1000000000L; }
  int rc = 0;
  pthread_mutex_lock(&g_mu);
  while(g_n[s] > 0 || g_any > 0) {
    if(pthread_cond_timedwait(&g_cv, &g_mu, &until) == ETIMEDOUT) {
      if(g_n[s] > 0 || g_any > 0) rc = -1;
      break;
    }
  }
  pthread_mutex_unlock(&g_mu);
  return rc;
}

/* For the diagnostic line before an eject: readers inside the gate now, per drive (SLOTS entries) and all-drive. */
void
ps5tm_drive_counts(int *per_slot, int *any) {
  pthread_mutex_lock(&g_mu);
  if(per_slot) for(int i = 0; i < SLOTS; i++) per_slot[i] = g_n[i];
  if(any) *any = g_any;
  pthread_mutex_unlock(&g_mu);
}
