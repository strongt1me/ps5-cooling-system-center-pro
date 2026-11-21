/* Host test of src/drivegate.c: the gate an eject waits on (21.11.2026). Needs gcc/clang and pthreads on Linux/WSL. */
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "ps5tm.h"

/* ps5tm.h routes pthread_create through the app's wrapper (a stack size for every thread); here the plain call */
#undef pthread_create
int ps5tm_thread_create(pthread_t *thread, const pthread_attr_t *attr, void *(*start)(void *), void *arg) { return pthread_create(thread, attr, start, arg); }
#define pthread_create(t, a, f, x) ps5tm_thread_create((t), (a), (f), (x))

/* ---- what drivegate.c asks of usbeject.c: the drive that is being ejected, or settles */
static atomic_int g_settling_usb0;      /* /mnt/usb0 is being ejected */
static atomic_int g_active_any;         /* any drive is */
int ps5tm_usbeject_settling(const char *path) {
  return atomic_load(&g_settling_usb0) && path && !strncmp(path, "/mnt/usb0", 9) && (path[9] == '/' || path[9] == 0);
}
int ps5tm_usbeject_active(void) { return atomic_load(&g_active_any) || atomic_load(&g_settling_usb0); }

static int g_fail, g_n;
#define CHECK(c) do { g_n++; if(!(c)) { g_fail++; printf("FAIL line %d: %s\n", __LINE__, #c); } } while(0)

static void ms(int n) { usleep((useconds_t)n * 1000); }

/* a reader that holds the gate for a while, then lets go */
typedef struct { const char *path; int hold_ms; atomic_int *in; } holder_t;
static void *
hold(void *arg) {
  holder_t *h = arg;
  int tok = ps5tm_drive_enter(h->path);
  if(tok == 0) return NULL;
  atomic_fetch_add(h->in, 1);
  ms(h->hold_ms);
  atomic_fetch_sub(h->in, 1);
  ps5tm_drive_leave(tok);
  return NULL;
}

/* the stress: readers in and out of /mnt/usb0 all the time; "eject" claims, waits idle, and nobody may be inside when the wait says idle */
static atomic_int g_stop, g_inside, g_ok_enters;
static void *
reader(void *arg) {
  (void)arg;
  while(!atomic_load(&g_stop)) {
    int tok = ps5tm_drive_enter("/mnt/usb0/folder");
    if(tok == 0) { usleep(50); continue; }
    atomic_fetch_add(&g_inside, 1);
    atomic_fetch_add(&g_ok_enters, 1);
    usleep(200);
    atomic_fetch_sub(&g_inside, 1);
    ps5tm_drive_leave(tok);
  }
  return NULL;
}

int
main(void) {
  int per[9], any;

  /* not a removable drive: nothing is counted, nothing is refused */
  CHECK(ps5tm_drive_slot("/data") == -1 && ps5tm_drive_slot(NULL) == -1 && ps5tm_drive_slot("/mnt/ext1") == -1 && ps5tm_drive_slot("/mnt/usb8") == -1);
  CHECK(ps5tm_drive_slot("/mnt/usb0") == 0 && ps5tm_drive_slot("/mnt/usb7/x/y") == 7 && ps5tm_drive_slot("/mnt/ext0") == 8 && ps5tm_drive_slot("/mnt/usb01") == -1);
  CHECK(ps5tm_drive_enter("/data/homebrew") == -1);
  ps5tm_drive_leave(-1);
  ps5tm_drive_leave(0);
  ps5tm_drive_counts(per, &any);
  for(int i = 0; i < 9; i++) CHECK(per[i] == 0);
  CHECK(any == 0);

  /* enter counts, leave releases */
  int t = ps5tm_drive_enter("/mnt/usb3/games");
  CHECK(t == 4);
  ps5tm_drive_counts(per, &any);
  CHECK(per[3] == 1 && per[0] == 0);
  CHECK(ps5tm_drive_wait_idle("/mnt/usb3", 50) == -1);        /* busy: times out */
  CHECK(ps5tm_drive_wait_idle("/mnt/usb0", 50) == 0);         /* another drive: idle */
  ps5tm_drive_leave(t);
  ps5tm_drive_counts(per, &any);
  CHECK(per[3] == 0);
  CHECK(ps5tm_drive_wait_idle("/mnt/usb3", 50) == 0);
  CHECK(ps5tm_drive_wait_idle("/data", 50) == 0);              /* not a drive: nothing to wait for */

  /* refused while the drive is being ejected: not counted */
  atomic_store(&g_settling_usb0, 1);
  CHECK(ps5tm_drive_enter("/mnt/usb0") == 0);
  CHECK(ps5tm_drive_enter("/mnt/usb0/a/b") == 0);
  CHECK(ps5tm_drive_enter("/mnt/usb1") == 2);                  /* another drive goes on */
  ps5tm_drive_leave(2);
  ps5tm_drive_counts(per, &any);
  CHECK(per[0] == 0 && per[1] == 0);
  /* a reader of all drives: refused while any drive is being ejected */
  CHECK(ps5tm_drive_enter_any() == 0);
  ps5tm_drive_counts(per, &any);
  CHECK(any == 0);
  atomic_store(&g_settling_usb0, 0);
  int ta = ps5tm_drive_enter_any();
  CHECK(ta > 0);
  ps5tm_drive_counts(per, &any);
  CHECK(any == 1);
  CHECK(ps5tm_drive_wait_idle("/mnt/usb5", 50) == -1);         /* an all-drive reader holds every drive */
  ps5tm_drive_leave(ta);
  CHECK(ps5tm_drive_wait_idle("/mnt/usb5", 50) == 0);

  /* the wait ends as soon as the reader lets go (not only at the end of the time) */
  atomic_int in = 0;
  holder_t h = { "/mnt/usb2/x", 150, &in };
  pthread_t th;
  pthread_create(&th, NULL, hold, &h);
  ms(30);
  CHECK(atomic_load(&in) == 1);
  struct timespec a, b;
  clock_gettime(CLOCK_MONOTONIC, &a);
  CHECK(ps5tm_drive_wait_idle("/mnt/usb2", 2000) == 0);
  clock_gettime(CLOCK_MONOTONIC, &b);
  long took = (b.tv_sec - a.tv_sec) * 1000 + (b.tv_nsec - a.tv_nsec) / 1000000;
  CHECK(took >= 80 && took < 1000);
  CHECK(atomic_load(&in) == 0);
  pthread_join(th, NULL);

  /* a reader that still holds it when the time is over: -1 */
  holder_t h2 = { "/mnt/usb2/x", 400, &in };
  pthread_create(&th, NULL, hold, &h2);
  ms(30);
  CHECK(ps5tm_drive_wait_idle("/mnt/usb2", 100) == -1);
  pthread_join(th, NULL);
  CHECK(ps5tm_drive_wait_idle("/mnt/usb2", 100) == 0);

  /* the race the gate exists for: readers all the time; once the drive is claimed and wait_idle says idle, nobody is inside and nobody gets in */
  pthread_t rd[6];
  for(int i = 0; i < 6; i++) pthread_create(&rd[i], NULL, reader, NULL);
  int bad = 0;
  for(int round = 0; round < 40; round++) {
    ms(3);
    atomic_store(&g_settling_usb0, 1);                          /* the claim */
    if(ps5tm_drive_wait_idle("/mnt/usb0", 3000) != 0) bad++;
    if(atomic_load(&g_inside) != 0) bad++;                       /* idle means: nobody inside */
    int before = atomic_load(&g_ok_enters);
    ms(5);
    if(atomic_load(&g_ok_enters) != before) bad++;               /* and nobody comes in while claimed */
    if(atomic_load(&g_inside) != 0) bad++;
    atomic_store(&g_settling_usb0, 0);                          /* the drive is back */
  }
  atomic_store(&g_stop, 1);
  for(int i = 0; i < 6; i++) pthread_join(rd[i], NULL);
  CHECK(bad == 0);
  ps5tm_drive_counts(per, &any);
  CHECK(per[0] == 0 && any == 0);
  CHECK(atomic_load(&g_ok_enters) > 100);                         /* the readers did get in between the claims */

  printf("drivegate: %d checks, %d failed\n", g_n, g_fail);
  return g_fail ? 1 : 0;
}
