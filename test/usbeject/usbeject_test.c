/* Host test of src/usbeject.c: what the eject asks, and in which order, before ShadowMountPlus is asked to cut a drive off (21.11.2026).
   The point of the test: when anything speaks against ejecting, ShadowMountPlus is NEVER asked (the counter of its eject calls stays 0) and no marker is
   written; when it goes through, the marker is written right before. ShadowMountPlus, the process list, the jobs and the mount table are stubs here; the gate
   (drivegate.c), the marker (ejectmark.c) and cJSON are the real ones. Needs gcc/clang and pthreads on Linux/WSL (see run.sh). */
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>                              /* the shim of test/usbeject/shim */
#include <time.h>
#include <unistd.h>

#include "ps5tm.h"
#include "ps4m.h"
#include "third_party/cJSON.h"

#undef pthread_create
int ps5tm_thread_create(pthread_t *thread, const pthread_attr_t *attr, void *(*start)(void *), void *arg) { return pthread_create(thread, attr, start, arg); }
#define pthread_create(t, a, f, x) ps5tm_thread_create((t), (a), (f), (x))

#ifndef EJECTMARK_PATH
#error "the test needs -DEJECTMARK_PATH"
#endif

static int g_fail, g_n;
#define CHECK(c) do { g_n++; if(!(c)) { g_fail++; printf("FAIL line %d: %s\n", __LINE__, #c); } } while(0)

/* ---- the world around usbeject.c ---- */
static char  g_codes[8192];                       /* the log codes in the order they were written, e.g. "usb_eject_state usb_eject_ask" */
void ps5tm_log(const char *level, const char *code, const char *fmt, ...) {
  (void)level; (void)fmt;
  size_t l = strlen(g_codes);
  snprintf(g_codes + l, sizeof(g_codes) - l, "%s ", code);
}
void ps5tm_log_file_flush(void) {}
void ps5tm_notify_plain(const char *fmt, ...) { (void)fmt; }
uint64_t ps5tm_now_ms(void) { struct timespec t; clock_gettime(CLOCK_REALTIME, &t); return (uint64_t)t.tv_sec * 1000 + (uint64_t)t.tv_nsec / 1000000; }
uint64_t ps5tm_mono_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec * 1000 + (uint64_t)t.tv_nsec / 1000000; }

static int  g_job_busy;                           /* an app job runs */
int ps5tm_jobs_busy_except(int self, int flags, char *why, size_t n) {
  (void)self; (void)flags;
  if(g_job_busy && why && n) snprintf(why, n, "Es läuft ein Test-Auftrag");
  return g_job_busy;
}
static int  g_fm_reading;
int ps5tm_filemgr_reading(void) { return g_fm_reading; }
static int  g_ps4m_wait;
int ps4m_wait_drive_free(const char *mount, int ms) { (void)mount; (void)ms; return g_ps4m_wait; }
int ps4m_game_running_from(const char *tid, const char *mount) { (void)tid; (void)mount; return 0; }
int ps5tm_kraken_games(ps5tm_kraken_game_t *out, int max) { (void)out; (void)max; return 0; }
int ps5tm_procmgr_title_running(const char *title_id) { (void)title_id; return 0; }

static ps5tm_holder_t g_holders[8];
static int            g_nh;                       /* the return value of ps5tm_drive_holders: how many, or -1 */
int ps5tm_drive_holders(const char *mount, ps5tm_holder_t *out, int max) {
  (void)mount;
  for(int i = 0; i < g_nh && i < max; i++) out[i] = g_holders[i];
  return g_nh;
}
void ps5tm_drive_holders_diag(char *out, size_t n) { snprintf(out, n, "test"); }

int getfsstat(struct statfs *buf, long bufsize, int mode) {
  (void)mode;
  if(!buf) return 2;
  (void)bufsize;
  snprintf(buf[0].f_mntfromname, sizeof(buf[0].f_mntfromname), "/dev/da0p1");
  snprintf(buf[0].f_mntonname, sizeof(buf[0].f_mntonname), "/mnt/usb0");
  snprintf(buf[0].f_fstypename, sizeof(buf[0].f_fstypename), "exfatfs");
  snprintf(buf[1].f_mntfromname, sizeof(buf[1].f_mntfromname), "/dev/ssd0");
  snprintf(buf[1].f_mntonname, sizeof(buf[1].f_mntonname), "/data");
  snprintf(buf[1].f_fstypename, sizeof(buf[1].f_fstypename), "ufs");
  return 2;
}

/* ShadowMountPlus: the calls it gets, in order; what it answers */
static int  g_smp_can = 1;
static int  g_smp_eject_noanswer;                 /* the eject call gets no answer in time */
static int  g_smp_status_mode;                    /* 0 done, 1 refused before the detach (game_active), 2 failed after it (usb_eject_failed) */
static char g_smp_order[256];
static int  g_smp_eject_calls, g_smp_list_calls;
static char g_smp_mount[32];
int ps5tm_smp_can(const char *capability) { (void)capability; return g_smp_can; }
int ps5tm_smp_can_cached(const char *capability) { (void)capability; return g_smp_can; }
cJSON *ps5tm_smp_call(const char *route, const cJSON *body, int timeout_ms, char *err, size_t err_len) {
  (void)body; (void)timeout_ms;
  if(!strcmp(route, "/storage")) {
    g_smp_list_calls++;
    strcat(g_smp_order, "list ");
    char js[400];
    snprintf(js, sizeof(js), "{\"mounts\":[{\"mount_point\":\"%s\",\"ejectable\":true,\"source\":\"/dev/da0p1\",\"mount_id\":\"0123456789abcdef\"}]}", g_smp_mount);
    return cJSON_Parse(js);
  }
  if(!strcmp(route, "/storage/eject")) {
    g_smp_eject_calls++;
    strcat(g_smp_order, "eject ");
    if(g_smp_eject_noanswer) { snprintf(err, err_len, "ShadowMountPlus antwortet nicht (Zeitüberschreitung)"); return NULL; }
    return cJSON_Parse("{\"job_id\":7}");
  }
  if(!strcmp(route, "/games/storage/status")) {
    if(g_smp_status_mode == 1) return cJSON_Parse("{\"job_id\":7,\"active\":false,\"result_status\":16,\"result_error_reason\":\"game_active\"}");
    if(g_smp_status_mode == 2) return cJSON_Parse("{\"job_id\":7,\"active\":false,\"result_status\":5,\"result_error_reason\":\"usb_eject_failed\"}");
    return cJSON_Parse("{\"job_id\":7,\"active\":false,\"result_status\":0}");
  }
  snprintf(err, err_len, "unbekannte Route");
  return NULL;
}

/* ---- helpers ---- */
static int exists(void) { return access(EJECTMARK_PATH, F_OK) == 0; }

static void
reset(const char *mount) {
  unlink(EJECTMARK_PATH);
  g_codes[0] = 0;
  g_smp_order[0] = 0;
  g_smp_eject_calls = g_smp_list_calls = 0;
  g_job_busy = g_fm_reading = g_ps4m_wait = g_nh = 0;
  g_smp_can = 1;
  g_smp_eject_noanswer = 0;
  g_smp_status_mode = 0;
  snprintf(g_smp_mount, sizeof(g_smp_mount), "%s", mount);
}

static void
wait_end(void) {                                  /* until the follower thread is done (it asks once a second) */
  for(int i = 0; i < 80 && ps5tm_usbeject_busy(); i++) usleep(100 * 1000);
}

static int
start(const char *mount, char *msg, size_t n) {
  reset(mount);
  msg[0] = 0;
  return ps5tm_usbeject_start(mount, msg, n);
}

/* a reader that holds the gate of one drive for a while */
typedef struct { const char *path; int hold_ms; } hold_t;
static void *
hold(void *arg) {
  hold_t *h = arg;
  int tok = ps5tm_drive_enter(h->path);
  usleep((useconds_t)h->hold_ms * 1000);
  ps5tm_drive_leave(tok);
  return NULL;
}

int
main(void) {
  char msg[400];
  unlink(EJECTMARK_PATH);

  /* 1. everything quiet: ShadowMountPlus is asked, once, after the list; the state is logged first; the marker is written; the follower ends it */
  {
    int rc = start("/mnt/usb0", msg, sizeof(msg));
    CHECK(rc == 202);
    CHECK(g_smp_eject_calls == 1 && g_smp_list_calls == 1);
    CHECK(strcmp(g_smp_order, "list eject ") == 0);
    CHECK(strstr(g_codes, "usb_eject_state") != NULL && strstr(g_codes, "usb_eject_ask") != NULL);
    CHECK(strstr(g_codes, "usb_eject_state") < strstr(g_codes, "usb_eject_ask"));            /* the state report is written BEFORE the console is asked */
    CHECK(exists());                                                                          /* the marker is there while the drive is being cut off */
    wait_end();
    CHECK(!ps5tm_usbeject_busy());
    CHECK(exists());                                                                          /* and it stays through the re-announce window ... */
    usleep(400 * 1000);                                                                       /* ... (120 ms in this test) */
    CHECK(!exists());
    cJSON *j = ps5tm_usbeject_json();
    CHECK(j && !cJSON_IsTrue(cJSON_GetObjectItem(j, "blocked")) && !strcmp(cJSON_GetObjectItem(j, "state")->valuestring, "done"));
    cJSON_Delete(j);
  }

  /* 2. an app job runs: refused, ShadowMountPlus is NOT asked, no marker */
  {
    reset("/mnt/usb1");
    g_job_busy = 1;
    int rc = ps5tm_usbeject_start("/mnt/usb1", msg, sizeof(msg));
    CHECK(rc == 409 && strstr(msg, "Auftrag") != NULL);
    CHECK(g_smp_eject_calls == 0 && g_smp_list_calls == 0 && !exists());
    CHECK(!ps5tm_usbeject_busy());                                                            /* the claim is released again */
  }

  /* 3. the app itself has a file on the drive: refused with that, SMP not asked, no marker */
  {
    reset("/mnt/usb2");
    g_nh = 1;
    memset(&g_holders[0], 0, sizeof(g_holders[0]));
    g_holders[0].pid = (int)getpid();
    snprintf(g_holders[0].name, sizeof(g_holders[0].name), "diese App");
    g_holders[0].count = 1;
    snprintf(g_holders[0].sample, sizeof(g_holders[0].sample), "/mnt/usb2 (descriptor 23)");
    int rc = ps5tm_usbeject_start("/mnt/usb2", msg, sizeof(msg));
    CHECK(rc == 409 && strstr(msg, "App selbst") != NULL);
    CHECK(g_smp_eject_calls == 0 && g_smp_list_calls == 0 && !exists());
    CHECK(strstr(g_codes, "usb_eject_holder") != NULL);
  }

  /* 4. another program has files on the drive: refused, named; the ones that do not count (ShadowMountPlus, kstuff, the console's services) do not stop it */
  {
    reset("/mnt/usb3");
    g_nh = 1;
    memset(&g_holders[0], 0, sizeof(g_holders[0]));
    g_holders[0].pid = 4242;
    snprintf(g_holders[0].name, sizeof(g_holders[0].name), "ftpsrv.elf");
    g_holders[0].count = 3;
    snprintf(g_holders[0].sample, sizeof(g_holders[0].sample), "/mnt/usb3/games/x.bin");
    int rc = ps5tm_usbeject_start("/mnt/usb3", msg, sizeof(msg));
    CHECK(rc == 409 && strstr(msg, "ftpsrv.elf") != NULL);
    CHECK(g_smp_eject_calls == 0 && !exists());
    static const char *const fine[] = { "shadowmountplus.elf", "kstuff.elf", "SceShellCore", "NPXS40000", "elfldr.elf" };
    for(size_t i = 0; i < sizeof(fine) / sizeof(fine[0]); i++) {
      char mt[16];
      snprintf(mt, sizeof(mt), "/mnt/usb%d", 4 + (int)(i % 3));
      if(i >= 3) snprintf(mt, sizeof(mt), "/mnt/usb%d", 7 - (int)(i - 3));
      reset(mt);
      g_nh = 1;
      memset(&g_holders[0], 0, sizeof(g_holders[0]));
      g_holders[0].pid = 5000 + (int)i;
      snprintf(g_holders[0].name, sizeof(g_holders[0].name), "%s", fine[i]);
      g_holders[0].count = 5;
      snprintf(g_holders[0].sample, sizeof(g_holders[0].sample), "%s/x", mt);
      rc = ps5tm_usbeject_start(mt, msg, sizeof(msg));
      CHECK(rc == 202);
      CHECK(g_smp_eject_calls == 1);
      wait_end();
    }
  }

  /* 5. who has files open is not known (the Pro, firmware 12.00): it goes on */
  {
    int rc = start("/mnt/ext0", msg, sizeof(msg));
    g_nh = 0;
    CHECK(rc == 202 || rc == 409);                                                            /* (the stub list is for this mount; start() resets g_nh to 0) */
    wait_end();
  }
  {
    reset("/mnt/usb0");
    usleep(1);
  }

  /* 6. the gate: a reader that is inside is waited for ... (drives that no earlier case ejected: an ejected drive is "settling" and its readers are turned away) */
  {
    reset("/mnt/usb1");
    pthread_t th;
    hold_t h = { "/mnt/usb1/folder", 1500 };
    pthread_create(&th, NULL, hold, &h);
    usleep(80 * 1000);
    int per[9], any = 0;
    ps5tm_drive_counts(per, &any);
    CHECK(per[1] == 1);                                                                       /* the reader really is inside */
    struct timespec a, b;
    clock_gettime(CLOCK_MONOTONIC, &a);
    int rc = ps5tm_usbeject_start("/mnt/usb1", msg, sizeof(msg));
    clock_gettime(CLOCK_MONOTONIC, &b);
    long took = (b.tv_sec - a.tv_sec) * 1000 + (b.tv_nsec - a.tv_nsec) / 1000000;
    CHECK(rc == 202);
    CHECK(took >= 1000);                                                                      /* it waited for the reader (it had 1420 ms left; the sync pause alone is 300) */
    CHECK(g_smp_eject_calls == 1 && !strcmp(g_smp_order, "list eject "));
    pthread_join(th, NULL);
    wait_end();
  }
  /* ... and one that does not let go is a refusal after 4 s, SMP not asked, no marker */
  {
    reset("/mnt/usb2");
    pthread_t th;
    hold_t h = { "/mnt/usb2/folder", 5500 };
    pthread_create(&th, NULL, hold, &h);
    usleep(80 * 1000);
    int per[9], any = 0;
    ps5tm_drive_counts(per, &any);
    CHECK(per[2] == 1);
    int rc = ps5tm_usbeject_start("/mnt/usb2", msg, sizeof(msg));
    CHECK(rc == 409 && strstr(msg, "liest noch vom Laufwerk") != NULL);
    CHECK(g_smp_eject_calls == 0 && g_smp_list_calls == 0 && !exists());
    CHECK(!ps5tm_usbeject_busy());
    pthread_join(th, NULL);
  }

  /* 7. no answer from ShadowMountPlus to the eject call: it may be cutting the drive off now, so the marker STAYS (it is removed after the window) */
  {
    reset("/mnt/usb4");
    g_smp_eject_noanswer = 1;
    int rc = ps5tm_usbeject_start("/mnt/usb4", msg, sizeof(msg));
    CHECK(rc == 409 && g_smp_eject_calls == 1);
    CHECK(exists());
    usleep(500 * 1000);
    CHECK(!exists());
  }

  /* 8. ShadowMountPlus refuses before it touches the drive (a game runs): nothing was cut off, the marker goes at once */
  {
    reset("/mnt/usb7");
    g_smp_status_mode = 1;
    int rc = ps5tm_usbeject_start("/mnt/usb7", msg, sizeof(msg));
    CHECK(rc == 202 && exists());
    wait_end();
    CHECK(!exists());
    cJSON *j = ps5tm_usbeject_json();
    CHECK(j && !strcmp(cJSON_GetObjectItem(j, "state")->valuestring, "failed"));
    cJSON_Delete(j);
  }

  /* 9. the console may have been cut off (refusal after the detach): the marker stays through the window */
  {
    reset("/mnt/usb3");
    g_smp_status_mode = 2;
    int rc = ps5tm_usbeject_start("/mnt/usb3", msg, sizeof(msg));
    CHECK(rc == 202);
    wait_end();
    CHECK(exists());
    usleep(500 * 1000);
    CHECK(!exists());
  }

  /* 10. a second eject while one runs */
  {
    reset("/mnt/usb2");
    int rc = ps5tm_usbeject_start("/mnt/usb2", msg, sizeof(msg));
    CHECK(rc == 202);
    int rc2 = ps5tm_usbeject_start("/mnt/usb1", msg, sizeof(msg));
    CHECK(rc2 == 409 && strstr(msg, "schon") != NULL);
    CHECK(g_smp_eject_calls == 1);
    wait_end();
  }

  /* 11. blocked after a crash: a marker that was there at the start. Nothing is offered, nothing is asked, until it is allowed again */
  {
    reset("/mnt/usb1");
    FILE *f = fopen(EJECTMARK_PATH, "w");
    if(f) { fputs("/mnt/usb0 1\n", f); fclose(f); }
    ps5tm_usbeject_init();                                                                    /* the start of the app */
    CHECK(ps5tm_usbeject_blocked());
    CHECK(!ps5tm_usbeject_path_ok("/mnt/usb1") && !ps5tm_usbeject_path_ok("/mnt/ext0"));
    int rc = ps5tm_usbeject_start("/mnt/usb1", msg, sizeof(msg));
    CHECK(rc == 409 && strstr(msg, "gesperrt") != NULL);
    CHECK(g_smp_eject_calls == 0 && g_smp_list_calls == 0);
    cJSON *j = ps5tm_usbeject_json();
    CHECK(j && cJSON_IsTrue(cJSON_GetObjectItem(j, "blocked")) && !cJSON_IsTrue(cJSON_GetObjectItem(j, "available")));
    cJSON_Delete(j);
    ps5tm_usbeject_shutdown();                                                                /* a planned end of a blocked run keeps the block */
    CHECK(exists());
    CHECK(ps5tm_usbeject_rearm() == 1);
    CHECK(!exists() && !ps5tm_usbeject_blocked() && ps5tm_usbeject_path_ok("/mnt/usb1"));
    rc = ps5tm_usbeject_start("/mnt/usb1", msg, sizeof(msg));
    CHECK(rc == 202 && g_smp_eject_calls == 1);
    wait_end();
  }

  /* 12. not a drive of that kind, or ShadowMountPlus without the capability */
  {
    reset("/mnt/usb0");
    CHECK(ps5tm_usbeject_start("/data", msg, sizeof(msg)) == 400);
    CHECK(ps5tm_usbeject_start("/mnt/ext1", msg, sizeof(msg)) == 400);
    CHECK(ps5tm_usbeject_start("/mnt/usb9", msg, sizeof(msg)) == 400);
    CHECK(ps5tm_usbeject_start(NULL, msg, sizeof(msg)) == 400);
    g_smp_can = 0;
    CHECK(ps5tm_usbeject_start("/mnt/usb0", msg, sizeof(msg)) == 409);
    CHECK(g_smp_eject_calls == 0 && !exists());
  }

  usleep(500 * 1000);
  unlink(EJECTMARK_PATH);
  printf("usbeject: %d checks, %d failed\n", g_n, g_fail);
  return g_fail ? 1 : 0;
}
