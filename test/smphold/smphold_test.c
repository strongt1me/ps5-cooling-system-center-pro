/* Host test of src/smphold.c: a game the app mounted through ShadowMountPlus and nobody started is unmounted again after two minutes. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "ps5tm.h"
#include "smphold.h"

static int g_fail, g_n;
#define CHECK(c) do { g_n++; if(!(c)) { g_fail++; printf("FAIL line %d: %s\n", __LINE__, #c); } } while(0)

/* ---- stubs of what smphold.c links against (the real calls are replaced by the test environment) */
void ps5tm_log(const char *level, const char *code, const char *fmt, ...) { (void)level; (void)code; (void)fmt; }
void ps5tm_notify_plain(const char *fmt, ...) { (void)fmt; }
uint64_t ps5tm_mono_ms(void) { return 0; }
#undef pthread_create        /* ps5tm.h routes it through ps5tm_thread_create; the stub calls the real one */
int ps5tm_thread_create(pthread_t *t, const pthread_attr_t *a, void *(*f)(void *), void *x) { return pthread_create(t, a, f, x); }
int ps5tm_smp_find(const char *id, ps5tm_smp_game_t *out) { (void)id; (void)out; return -1; }
int ps5tm_smp_available(char *v, size_t n) { (void)v; (void)n; return 0; }
int ps5tm_gamestate_measured(void) { return 1; }
void ps5tm_gamestate_get(ps5tm_gamestate_t *o) { memset(o, 0, sizeof(*o)); }
int ps5tm_app_job_running(char *why, size_t n) { (void)why; (void)n; return 0; }
int ps5tm_smpremount(const char *id, const char *a, char *msg, size_t n) { (void)id; (void)a; (void)msg; (void)n; return 500; }

/* ---- the test world */
static uint64_t g_now;
static int g_up, g_game, g_job;
static int g_mounted;            /* what ShadowMountPlus says about every title */
static int g_known;
static int g_unmount_calls, g_unmount_http;
static char g_unmounted[16];
static int g_notices;

static int t_find(const char *id, int *mounted) { (void)id; if(!g_known) return -1; *mounted = g_mounted; return 0; }
static int t_up(void) { return g_up; }
static int t_game(void) { return g_game; }
static int t_job(void) { return g_job; }
static int t_unmount(const char *id, char *msg, size_t n) {
  g_unmount_calls++;
  snprintf(g_unmounted, sizeof(g_unmounted), "%s", id);
  snprintf(msg, n, "test");
  if(g_unmount_http == 200) ps5tm_smphold_note(id, 0);       /* the real unmount goes through ps5tm_smp_call, which does this */
  return g_unmount_http;
}
static uint64_t t_now(void) { return g_now; }
static void t_notice(const char *id) { (void)id; g_notices++; }

static void
world(void) {
  ps5tm_smphold_env_t e = { t_find, t_up, t_game, t_job, t_unmount, t_now, t_notice };
  ps5tm_smphold_test_env(&e);
  ps5tm_smphold_test_reset();
  g_now = 1000000; g_up = 1; g_game = 0; g_job = 0; g_mounted = 1; g_known = 1;
  g_unmount_calls = 0; g_unmount_http = 200; g_unmounted[0] = 0; g_notices = 0;
}

static int
file_has(const char *id) {
  FILE *f = fopen(PS5TM_DATA_DIR "/smp-mounted.txt", "r");
  if(!f) return 0;
  char line[64];
  int yes = 0;
  while(fgets(line, sizeof(line), f)) {
    line[strcspn(line, "\r\n")] = 0;
    if(!strcmp(line, id)) yes = 1;
  }
  fclose(f);
  return yes;
}

static int
file_exists(void) {
  struct stat st;
  return stat(PS5TM_DATA_DIR "/smp-mounted.txt", &st) == 0;
}

int
main(void) {
  mkdir(PS5TM_DATA_DIR, 0755);
  unlink(PS5TM_DATA_DIR "/smp-mounted.txt");

  /* written down, and in the file */
  world();
  ps5tm_smphold_note("PPSA02664", 1);
  CHECK(ps5tm_smphold_count() == 1);
  CHECK(file_has("PPSA02664"));
  ps5tm_smphold_note("PPSA02664", 1);                        /* the same title twice: one entry */
  CHECK(ps5tm_smphold_count() == 1);

  /* before the two minutes: nothing */
  g_now += PS5TM_SMPHOLD_GRACE_MS - 1;
  CHECK(ps5tm_smphold_step() == 0 && g_unmount_calls == 0);

  /* after them: unmounted, written off, the person told */
  g_now += 1;
  CHECK(ps5tm_smphold_step() == 1);
  CHECK(g_unmount_calls == 1 && !strcmp(g_unmounted, "PPSA02664"));
  CHECK(ps5tm_smphold_count() == 0 && !file_exists());
  CHECK(g_notices == 1);
  CHECK(ps5tm_smphold_step() == 0 && g_unmount_calls == 1);  /* nothing left */

  /* the person unmounts it: written off at once */
  world();
  ps5tm_smphold_note("CUSA00775", 1);
  ps5tm_smphold_note("CUSA00775", 0);
  CHECK(ps5tm_smphold_count() == 0 && !file_exists());

  /* a game plays: the two minutes wait and start again afterwards */
  world();
  ps5tm_smphold_note("PPSA02664", 1);
  g_game = 1;
  g_now += 10 * PS5TM_SMPHOLD_GRACE_MS;
  CHECK(ps5tm_smphold_step() == 0 && g_unmount_calls == 0);
  g_game = 0;
  CHECK(ps5tm_smphold_step() == 0 && g_unmount_calls == 0);  /* the clock started again at the last look */
  g_now += PS5TM_SMPHOLD_GRACE_MS - 1;
  CHECK(ps5tm_smphold_step() == 0 && g_unmount_calls == 0);
  g_now += 1;
  CHECK(ps5tm_smphold_step() == 1 && g_unmount_calls == 1);

  /* one of the app's own jobs: the same */
  world();
  ps5tm_smphold_note("PPSA02664", 1);
  g_job = 1;
  g_now += 10 * PS5TM_SMPHOLD_GRACE_MS;
  CHECK(ps5tm_smphold_step() == 0 && g_unmount_calls == 0);
  g_job = 0;
  CHECK(ps5tm_smphold_step() == 0 && g_unmount_calls == 0);
  g_now += PS5TM_SMPHOLD_GRACE_MS;
  CHECK(ps5tm_smphold_step() == 1);

  /* released by ShadowMountPlus meanwhile (the game started and ended, or it restarted): dropped without an unmount */
  world();
  ps5tm_smphold_note("PPSA02664", 1);
  g_mounted = 0;
  g_now += 10 * PS5TM_SMPHOLD_GRACE_MS;
  CHECK(ps5tm_smphold_step() == 0 && g_unmount_calls == 0);
  CHECK(ps5tm_smphold_count() == 0 && !file_exists());

  /* ShadowMountPlus does not know the title any more: dropped too */
  world();
  ps5tm_smphold_note("PPSA02664", 1);
  g_known = 0;
  g_now += 10 * PS5TM_SMPHOLD_GRACE_MS;
  CHECK(ps5tm_smphold_step() == 0 && ps5tm_smphold_count() == 0);

  /* ShadowMountPlus does not answer: nothing is known, nothing is done, nothing forgotten */
  world();
  ps5tm_smphold_note("PPSA02664", 1);
  g_up = 0;
  g_now += 10 * PS5TM_SMPHOLD_GRACE_MS;
  CHECK(ps5tm_smphold_step() == 0 && g_unmount_calls == 0 && ps5tm_smphold_count() == 1);
  g_up = 1;
  CHECK(ps5tm_smphold_step() == 1 && g_unmount_calls == 1);

  /* a refusal: the next try comes after 30 s, not at the next look, and the title stays written down */
  world();
  ps5tm_smphold_note("PPSA02664", 1);
  g_unmount_http = 409;
  g_now += PS5TM_SMPHOLD_GRACE_MS;
  CHECK(ps5tm_smphold_step() == 0 && g_unmount_calls == 1 && ps5tm_smphold_count() == 1);
  g_now += 10000;
  CHECK(ps5tm_smphold_step() == 0 && g_unmount_calls == 1);
  g_now += 20000;
  g_unmount_http = 200;
  CHECK(ps5tm_smphold_step() == 1 && g_unmount_calls == 2 && ps5tm_smphold_count() == 0);

  /* what is not the app's to write down: its own tiles, a wrong id */
  world();
  ps5tm_smphold_note("FAKE86921", 1);
  ps5tm_smphold_note("FAKE86922", 1);
  ps5tm_smphold_note("", 1);
  ps5tm_smphold_note("PPSA0266", 1);
  ps5tm_smphold_note("ppsa02664", 1);
  ps5tm_smphold_note("PPSA02664/", 1);
  CHECK(ps5tm_smphold_count() == 0 && !file_exists());
  ps5tm_smphold_note("FAKE42007", 1);                        /* a homebrew ShadowMountPlus manages: yes */
  CHECK(ps5tm_smphold_count() == 1);

  /* room for eight, not more */
  world();
  for(int i = 0; i < 12; i++) {
    char id[16];
    snprintf(id, sizeof(id), "PPSA%05d", 100 + i);
    ps5tm_smphold_note(id, 1);
  }
  CHECK(ps5tm_smphold_count() == 8);

  /* a restart of the app: the file brings it back, wrong lines are skipped, the clock starts at the start */
  world();
  { FILE *f = fopen(PS5TM_DATA_DIR "/smp-mounted.txt", "w");
    fputs("PPSA02664\nFAKE86921\nxx\nCUSA00775\nPPSA02664\n", f);
    fclose(f); }
  ps5tm_smphold_start();
  CHECK(ps5tm_smphold_count() == 2);
  g_now += PS5TM_SMPHOLD_GRACE_MS - 1;
  CHECK(ps5tm_smphold_step() == 0);
  g_now += 1;
  CHECK(ps5tm_smphold_step() == 2 && g_unmount_calls == 2 && ps5tm_smphold_count() == 0);

  unlink(PS5TM_DATA_DIR "/smp-mounted.txt");
  printf("%d checks, %d failed\n", g_n, g_fail);
  return g_fail ? 1 : 0;
}
