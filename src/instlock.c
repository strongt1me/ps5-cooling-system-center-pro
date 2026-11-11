/* PS5 Cooling & System Center - Pro - one running copy of the app, found without HTTP. See instlock.h. */

#include "instlock.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>

static int g_lock_fd = -1;       /* kept open for the life of the process: closing it would drop the lock */

/* 1 = the write lock is ours now. 0 = a lock of another process is in the way; *holder = its pid (0 when unknown). -1 = cannot lock. */
static int
try_lock(int fd, int *holder) {
  struct flock fl;
  memset(&fl, 0, sizeof(fl));
  fl.l_type = F_WRLCK;
  fl.l_whence = SEEK_SET;
  if(fcntl(fd, F_SETLK, &fl) == 0) return 1;
  if(errno != EAGAIN && errno != EACCES) return -1;
  memset(&fl, 0, sizeof(fl));
  fl.l_type = F_WRLCK;
  fl.l_whence = SEEK_SET;
  *holder = (fcntl(fd, F_GETLK, &fl) == 0 && fl.l_type != F_UNLCK) ? (int)fl.l_pid : 0;
  return 0;
}

/* Tries for up to `ms` milliseconds. Same results as try_lock(). */
static int
lock_within(int fd, int ms, int *holder) {
  for(int waited = 0;; waited += 50) {
    int r = try_lock(fd, holder);
    if(r != 0 || waited >= ms) return r;
    usleep(50 * 1000);
  }
}

int
ps5tm_instlock_acquire(const char *path, int term_wait_ms, int kill_wait_ms, int *stopped_pid) {
  if(stopped_pid) *stopped_pid = 0;
  if(g_lock_fd >= 0) return 1;                   /* already ours */
  int fd = open(path, O_RDWR | O_CREAT, 0644);
  if(fd < 0) return -1;
  (void)fcntl(fd, F_SETFD, FD_CLOEXEC);

  int holder = 0;
  int r = try_lock(fd, &holder);
  if(r == 0) {
    if(holder <= 1 || holder == (int)getpid()) {
      r = lock_within(fd, 500, &holder);         /* the pid was not readable for a moment */
    }
  }
  if(r == 0 && holder > 1 && holder != (int)getpid()) {
    int pid = holder;
    if(kill(pid, SIGTERM) == 0) {
      r = lock_within(fd, term_wait_ms, &holder);
      if(r == 0 && kill(pid, SIGKILL) == 0) r = lock_within(fd, kill_wait_ms, &holder);
    } else {
      r = lock_within(fd, 500, &holder);         /* it ended by itself in the meantime */
    }
    if(r == 1 && stopped_pid) *stopped_pid = pid;
  }
  if(r == 1) {
    g_lock_fd = fd;
    return 1;
  }
  close(fd);
  return r < 0 ? -1 : 0;
}
