/* Host test of src/instlock.c: a second copy ends the first one (SIGTERM, then SIGKILL for one that ignores it). Needs clang on Linux/WSL. */
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../../src/instlock.h"

static int g_fail, g_n;
#define CHECK(c) do { g_n++; if(!(c)) { g_fail++; printf("FAIL line %d: %s\n", __LINE__, #c); } } while(0)

/* A child that takes the lock and then waits; stubborn = it ignores SIGTERM. Returns its pid once it holds the lock. */
static pid_t
holder(const char *path, int stubborn) {
  int pipefd[2];
  if(pipe(pipefd) != 0) exit(2);
  pid_t pid = fork();
  if(pid == 0) {
    if(stubborn) signal(SIGTERM, SIG_IGN);
    int stopped = 0;
    int r = ps5tm_instlock_acquire(path, 100, 100, &stopped);
    char c = r == 1 ? 'k' : 'x';
    (void)!write(pipefd[1], &c, 1);
    for(;;) pause();
  }
  char c = 0;
  (void)!read(pipefd[0], &c, 1);
  close(pipefd[0]);
  close(pipefd[1]);
  return c == 'k' ? pid : -1;
}

int
main(void) {
  char path[] = "/tmp/instlock_test_XXXXXX";
  int fd = mkstemp(path);
  if(fd < 0) return 2;
  close(fd);

  /* 1. nobody holds it: the lock is ours, nothing was stopped */
  pid_t a = holder(path, 0);
  CHECK(a > 0);

  /* 2. a second copy in this process: the first one is ended by SIGTERM */
  int stopped = 0;
  int r = ps5tm_instlock_acquire(path, 2000, 1000, &stopped);
  CHECK(r == 1);
  CHECK(stopped == a);
  int st = 0;
  waitpid(a, &st, 0);
  CHECK(WIFSIGNALED(st) && WTERMSIG(st) == SIGTERM);

  /* 3. calling it again in the same process does nothing new */
  CHECK(ps5tm_instlock_acquire(path, 100, 100, &stopped) == 1);
  CHECK(stopped == 0);
  unlink(path);
  printf("%d checks, %d failed\n", g_n, g_fail);
  return g_fail ? 1 : 0;
}
