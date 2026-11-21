/* Host test of src/ejectmark.c: the marker of an eject that did not come back (21.11.2026). A "restart of the app" is a new process of this very program
   (mode argument), because the state of the module lives in the process. Needs gcc/clang and pthreads on Linux/WSL. */
#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "ps5tm.h"

#undef pthread_create
int ps5tm_thread_create(pthread_t *thread, const pthread_attr_t *attr, void *(*start)(void *), void *arg) { return pthread_create(thread, attr, start, arg); }
void ps5tm_log(const char *level, const char *code, const char *fmt, ...) { (void)level; (void)code; (void)fmt; }
uint64_t ps5tm_now_ms(void) { return 1700000000000ull; }

#ifndef EJECTMARK_PATH
#error "the test needs -DEJECTMARK_PATH"
#endif
#define MARK EJECTMARK_PATH
#define LATER_MS EJECTMARK_LATER_MS

static int g_fail, g_n;
#define CHECK(c) do { g_n++; if(!(c)) { g_fail++; printf("FAIL line %d: %s\n", __LINE__, #c); } } while(0)

static int exists(void) { return access(MARK, F_OK) == 0; }

/* a restart of the app: exit code 0 = not blocked after init, 1 = blocked, and what it does after (mode) */
static int
child(const char *mode) {
  ps5tm_ejectmark_init();
  int blocked = ps5tm_ejectmark_blocked();
  if(!strcmp(mode, "shutdown")) ps5tm_ejectmark_shutdown();
  if(!strcmp(mode, "rearm")) { int was = ps5tm_ejectmark_rearm(); if(was != blocked) return 9; if(ps5tm_ejectmark_blocked()) return 8; if(ps5tm_ejectmark_rearm()) return 7; }
  return blocked ? 1 : 0;
}

static int
restart(const char *self, const char *mode) {
  char cmd[600];
  snprintf(cmd, sizeof(cmd), "'%s' %s", self, mode);
  int rc = system(cmd);
  return rc == -1 ? -1 : (rc >> 8) & 0xff;
}

int
main(int argc, char **argv) {
  if(argc > 1) return child(argv[1]);
  const char *self = argv[0];
  unlink(MARK);

  /* no marker: a start is not blocked */
  CHECK(restart(self, "plain") == 0);
  CHECK(!ps5tm_ejectmark_blocked());

  /* written just before the console is asked: it is on the disk, with the drive in it */
  ps5tm_ejectmark_write("/mnt/usb0");
  CHECK(exists());
  {
    char line[100] = "";
    FILE *f = fopen(MARK, "r");
    if(f) { if(!fgets(line, sizeof(line), f)) line[0] = 0; fclose(f); }
    CHECK(strncmp(line, "/mnt/usb0 ", 10) == 0);
  }
  CHECK(!ps5tm_ejectmark_blocked());                          /* this run is not blocked by its own marker */

  /* the console died (the process never ran a planned end): the next start is blocked, and stays so over another start */
  CHECK(restart(self, "plain") == 1);
  CHECK(restart(self, "plain") == 1);
  CHECK(exists());
  /* a planned end of a BLOCKED run keeps the block (a restart of the app does not lift it) */
  CHECK(restart(self, "shutdown") == 1);
  CHECK(exists());
  CHECK(restart(self, "plain") == 1);
  /* the person allows it again: the marker goes, the next start is clean; a second "allow" finds nothing to lift */
  CHECK(restart(self, "rearm") == 1);
  CHECK(!exists());
  CHECK(restart(self, "plain") == 0);
  ps5tm_ejectmark_clear_now();                                /* this process's own write of usb0 (in a real crash the process would be gone) */

  /* a planned end of a clean run removes the marker (nothing crashed) */
  ps5tm_ejectmark_write("/mnt/usb1");
  CHECK(exists());
  ps5tm_ejectmark_shutdown();
  CHECK(!exists());
  CHECK(restart(self, "plain") == 0);

  /* clear_now: nothing was cut off */
  ps5tm_ejectmark_write("/mnt/usb2");
  ps5tm_ejectmark_clear_now();
  CHECK(!exists());

  /* clear_later: still there inside the window, gone after it */
  ps5tm_ejectmark_write("/mnt/usb3");
  ps5tm_ejectmark_clear_later();
  usleep((useconds_t)(LATER_MS / 3) * 1000);
  CHECK(exists());                                            /* a crash in the window leaves it */
  usleep((useconds_t)(LATER_MS * 2) * 1000);
  CHECK(!exists());

  /* a newer eject's marker is not removed by the thread of an older one */
  ps5tm_ejectmark_write("/mnt/usb4");
  ps5tm_ejectmark_clear_later();
  usleep((useconds_t)(LATER_MS / 3) * 1000);
  ps5tm_ejectmark_write("/mnt/usb5");                          /* the next eject began before the first window was over */
  usleep((useconds_t)(LATER_MS * 2) * 1000);
  CHECK(exists());
  ps5tm_ejectmark_clear_later();                              /* and its own thread removes it */
  usleep((useconds_t)(LATER_MS * 2) * 1000);
  CHECK(!exists());

  /* an eject refused before anything was cut off (clear_now) while another one is inside its window: the other one's marker stays (review F4) */
  ps5tm_ejectmark_write("/mnt/usb7");
  ps5tm_ejectmark_clear_later();
  ps5tm_ejectmark_write("/mnt/usb1");
  ps5tm_ejectmark_clear_now();
  CHECK(exists());                                            /* usb7 may still crash the console: the marker must stay */
  usleep((useconds_t)(LATER_MS * 2) * 1000);
  CHECK(!exists());

  /* the folder of the marker is gone (no storage): nothing crashes, nothing blocks */
  ps5tm_ejectmark_write("/mnt/usb6");
  ps5tm_ejectmark_clear_now();
  CHECK(!exists());

  printf("ejectmark: %d checks, %d failed\n", g_n, g_fail);
  unlink(MARK);
  return g_fail ? 1 : 0;
}
