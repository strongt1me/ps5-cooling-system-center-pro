/* A copy that ignores SIGTERM is ended by SIGKILL. */
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../../src/instlock.h"

int
main(void) {
  char path[] = "/tmp/instlock_test2_XXXXXX";
  int fd = mkstemp(path);
  if(fd < 0) return 2;
  close(fd);
  int pipefd[2];
  if(pipe(pipefd) != 0) return 2;
  pid_t a = fork();
  if(a == 0) {
    signal(SIGTERM, SIG_IGN);
    int s = 0;
    char c = ps5tm_instlock_acquire(path, 100, 100, &s) == 1 ? 'k' : 'x';
    (void)!write(pipefd[1], &c, 1);
    for(;;) pause();
  }
  char c = 0;
  (void)!read(pipefd[0], &c, 1);
  int stopped = 0;
  int r = ps5tm_instlock_acquire(path, 300, 2000, &stopped);
  int st = 0;
  waitpid(a, &st, 0);
  int ok = c == 'k' && r == 1 && stopped == a && WIFSIGNALED(st) && WTERMSIG(st) == SIGKILL;
  unlink(path);
  printf("%s\n", ok ? "stubborn: ok" : "stubborn: FAIL");
  return ok ? 0 : 1;
}
