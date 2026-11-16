/* Host test of src/smphold.c: a game the app mounted through ShadowMountPlus and nobody started is unmounted again: after the grace, at once
   when the console refuses to start another title, and before the app starts one itself. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "ps5tm.h"
#include "smphold.h"
#include "third_party/cJSON.h"

static int g_fail, g_n;
#define CHECK(c) do { g_n++; if(!(c)) { g_fail++; printf("FAIL line %d: %s\n", __LINE__, #c); } } while(0)

/* ---- stubs of what smphold.c links against (the real calls are replaced by the test environment) */
static int g_warns_failed;                                  /* log lines "the unmount was refused" */
void ps5tm_log(const char *level, const char *code, const char *fmt, ...) {
  (void)level; (void)fmt;
  if(code && !strcmp(code, "smp_hold_failed")) g_warns_failed++;
}
void ps5tm_notify_plain(const char *fmt, ...) { (void)fmt; }
uint64_t ps5tm_mono_ms(void) { return 0; }
#undef pthread_create        /* ps5tm.h routes it through ps5tm_thread_create; the stub calls the real one */
int ps5tm_thread_create(pthread_t *t, const pthread_attr_t *a, void *(*f)(void *), void *x) { return pthread_create(t, a, f, x); }
static int g_smp_available;
int ps5tm_smp_find(const char *id, ps5tm_smp_game_t *out) {
  if(!g_smp_available) return -1;
  memset(out, 0, sizeof(*out));
  snprintf(out->title_id, sizeof(out->title_id), "%s", id);
  out->mounted = 1;
  return 0;
}
int ps5tm_smp_available(char *v, size_t n) { (void)v; (void)n; return g_smp_available; }
int ps5tm_gamestate_measured(void) { return 1; }
uint64_t ps5tm_gamestate_age_ms(void) { return 0; }
void ps5tm_gamestate_get(ps5tm_gamestate_t *o) { memset(o, 0, sizeof(*o)); }
int ps5tm_app_job_running(char *why, size_t n) { (void)why; (void)n; return 0; }
static int g_real_unmounts;
int ps5tm_smpremount(const char *id, const char *a, char *msg, size_t n) {
  (void)a; (void)msg; (void)n;
  if(!g_smp_available) return 500;
  g_real_unmounts++;
  ps5tm_smphold_note(id, 0);                                /* what ps5tm_smp_call does for a real unmount */
  return 200;
}
int ps5tm_library_launch_info(const char *id, char *name, size_t nn, char *link, size_t ln) { (void)id; (void)name; (void)nn; (void)link; (void)ln; return -1; }
/* A kernel log the test fills; the lines are what the test console wrote (14.11.2026) when a title was refused. */
static ps5tm_klog_line_t g_kl[64];
static unsigned g_kl_n;
static int g_kl_fail;                                       /* the kernel log cannot be read */
static void kl_add(const char *text) {
  ps5tm_klog_line_t *l = &g_kl[g_kl_n];
  memset(l, 0, sizeof(*l));
  l->seq = 6250 + (uint64_t)g_kl_n + 1;
  snprintf(l->text, sizeof(l->text), "%s", text);
  g_kl_n++;
}
unsigned ps5tm_klog_fetch(uint64_t after, unsigned max, ps5tm_klog_line_t *out, uint64_t *newest, int *more, int *lost) {
  unsigned got = 0, first = 0;
  if(g_kl_fail) {
    if(newest) *newest = 0;
    if(more) *more = 0;
    if(lost) *lost = 0;
    return 0;
  }
  if(newest) *newest = g_kl_n ? g_kl[g_kl_n - 1].seq : 6250;
  if(more) *more = 0;
  if(lost) *lost = 0;
  if(after == 0 && g_kl_n > max) first = g_kl_n - max;      /* "the latest" */
  for(unsigned i = first; i < g_kl_n && got < max; i++)
    if(after == 0 || g_kl[i].seq > after) out[got++] = g_kl[i];
  if(more && after != 0) { unsigned left = 0; for(unsigned i = 0; i < g_kl_n; i++) if(g_kl[i].seq > after) left++; *more = left > got; }
  return got;
}

/* ---- the test world */
static uint64_t g_now;
static int g_up, g_game, g_job;
static int g_mounted;            /* what ShadowMountPlus says about every title */
static int g_known;
static int g_unmount_calls, g_unmount_http;
static char g_unmounted[16];
static int g_notices;
static char g_notice_held[16], g_notice_other[16];
static char g_failed_next[16];          /* the title the console "refuses" at the next look; "" none */
static int g_resets;
static char g_run_title[16];            /* the title that runs (a fresh reading); "" none */
static uint64_t g_boot;                 /* the boot of the console; 0 not known */
static int g_find_kills_up;             /* ShadowMountPlus falls silent while its list is asked */

static int t_find(const char *id, int *mounted) {
  (void)id;
  if(g_find_kills_up) g_up = 0;
  if(!g_known) return -1;
  *mounted = g_mounted;
  return 0;
}
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
static void t_notice(const char *id, const char *other) {
  g_notices++;
  snprintf(g_notice_held, sizeof(g_notice_held), "%s", id);
  snprintf(g_notice_other, sizeof(g_notice_other), "%s", other);
}
static int t_failed(int reset, char *id, size_t n) {
  if(reset) { g_resets++; g_failed_next[0] = 0; return 0; }
  if(!g_failed_next[0]) return 0;
  snprintf(id, n, "%s", g_failed_next);
  g_failed_next[0] = 0;
  return 1;
}

static int t_title_running(const char *id) { return g_run_title[0] && !strcmp(g_run_title, id); }
/* the start of a title (the own tile that was refused is started again, 16.11.2026) */
static int g_start_calls, g_start_rc;
static char g_start_id[16];
static int t_start(const char *id) { g_start_calls++; snprintf(g_start_id, sizeof(g_start_id), "%s", id); return g_start_rc; }
int ps5tm_library_start(const char *id) { (void)id; return -1; }
cJSON *ps5tm_smp_call(const char *route, const cJSON *body, int timeout_ms, char *err, size_t err_len) { (void)route; (void)body; (void)timeout_ms; (void)err; (void)err_len; return NULL; }
void ps5tm_config_smp_version(char *out, size_t n) { snprintf(out, n, "v2"); }
static uint64_t t_boot(void) { return g_boot; }

static void
world(void) {
  ps5tm_smphold_env_t e = { t_find, t_up, t_game, t_job, t_unmount, t_now, t_notice, t_failed, t_title_running, t_boot, t_start };
  ps5tm_smphold_test_env(&e);
  ps5tm_smphold_test_reset();
  ps5tm_smphold_tile_test_reset();
  g_start_calls = 0; g_start_rc = 0; g_start_id[0] = 0;
  g_now = 1000000; g_up = 1; g_game = 0; g_job = 0; g_mounted = 1; g_known = 1;
  g_unmount_calls = 0; g_unmount_http = 200; g_unmounted[0] = 0; g_notices = 0;
  g_notice_held[0] = g_notice_other[0] = 0; g_failed_next[0] = 0; g_resets = 0;
  g_run_title[0] = 0; g_boot = 0; g_find_kills_up = 0; g_warns_failed = 0;
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

  /* a refusal: the next try comes after 10 s, not at the next look, and the title stays written down */
  world();
  ps5tm_smphold_note("PPSA02664", 1);
  g_unmount_http = 409;
  g_now += PS5TM_SMPHOLD_GRACE_MS;
  CHECK(ps5tm_smphold_step() == 0 && g_unmount_calls == 1 && ps5tm_smphold_count() == 1);
  g_now += 5000;
  CHECK(ps5tm_smphold_step() == 0 && g_unmount_calls == 1);
  g_now += 5000;
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


  /* the grace is short, and the notice after it names no refused start */
  CHECK(PS5TM_SMPHOLD_GRACE_MS == 30000u);
  world();
  ps5tm_smphold_note("PPSA02664", 1);
  g_now += PS5TM_SMPHOLD_GRACE_MS;
  CHECK(ps5tm_smphold_step() == 1 && g_notices == 1 && !strcmp(g_notice_held, "PPSA02664") && g_notice_other[0] == 0);

  /* is it held? */
  world();
  CHECK(!ps5tm_smphold_is_held("PPSA02664") && !ps5tm_smphold_is_held(NULL));
  ps5tm_smphold_note("PPSA02664", 1);
  CHECK(ps5tm_smphold_is_held("PPSA02664") && !ps5tm_smphold_is_held("PPSA02433"));
  ps5tm_smphold_note("PPSA02664", 0);
  CHECK(!ps5tm_smphold_is_held("PPSA02664"));

  /* the console refuses to start another title: released at once, no grace, the person told which start to repeat */
  world();
  ps5tm_smphold_note("PPSA02664", 1);
  CHECK(ps5tm_smphold_check_launch() == 0 && g_unmount_calls == 0);                       /* no refusal: nothing */
  snprintf(g_failed_next, sizeof(g_failed_next), "FAKE86922");                            /* its own tile, the case of 14.11. */
  CHECK(ps5tm_smphold_check_launch() == 1);
  CHECK(g_unmount_calls == 1 && !strcmp(g_unmounted, "PPSA02664") && ps5tm_smphold_count() == 0 && !file_exists());
  CHECK(g_notices == 1 && !strcmp(g_notice_held, "PPSA02664") && !strcmp(g_notice_other, "FAKE86922"));
  CHECK(ps5tm_smphold_check_launch() == 0 && g_unmount_calls == 1);                       /* nothing held any more */

  /* a game plays and a refusal comes: released all the same (it is a real block) */
  world();
  ps5tm_smphold_note("PPSA02664", 1);
  g_game = 1;
  snprintf(g_failed_next, sizeof(g_failed_next), "PPSA02433");
  CHECK(ps5tm_smphold_check_launch() == 1);

  /* the held game's own start failed: that is not what it blocked */
  world();
  ps5tm_smphold_note("PPSA02664", 1);
  snprintf(g_failed_next, sizeof(g_failed_next), "PPSA02664");
  CHECK(ps5tm_smphold_check_launch() == 0 && g_unmount_calls == 0 && ps5tm_smphold_count() == 1);

  /* two held, the refused one is one of them: only the other goes */
  world();
  ps5tm_smphold_note("PPSA02664", 1);
  ps5tm_smphold_note("CUSA00775", 1);
  snprintf(g_failed_next, sizeof(g_failed_next), "PPSA02664");
  CHECK(ps5tm_smphold_check_launch() == 1 && g_unmount_calls == 1 && !strcmp(g_unmounted, "CUSA00775") && ps5tm_smphold_is_held("PPSA02664"));

  /* ShadowMountPlus does not answer: nothing is done, the game stays written down */
  world();
  ps5tm_smphold_note("PPSA02664", 1);
  g_up = 0;
  snprintf(g_failed_next, sizeof(g_failed_next), "PPSA02433");
  CHECK(ps5tm_smphold_check_launch() == 0 && g_unmount_calls == 0 && ps5tm_smphold_count() == 1);

  /* the unmount is refused: it stays written down, no notice, the grace and its retries go on */
  world();
  ps5tm_smphold_note("PPSA02664", 1);
  g_unmount_http = 409;
  snprintf(g_failed_next, sizeof(g_failed_next), "PPSA02433");
  CHECK(ps5tm_smphold_check_launch() == 0 && g_unmount_calls == 1 && ps5tm_smphold_count() == 1 && g_notices == 0);

  /* an environment without the kernel-log reading watches nothing */
  { ps5tm_smphold_env_t e = { t_find, t_up, t_game, t_job, t_unmount, t_now, t_notice, NULL, NULL, NULL };
    ps5tm_smphold_test_env(&e);
    ps5tm_smphold_test_reset();
    g_unmount_calls = 0;
    ps5tm_smphold_note("PPSA02664", 1);
    CHECK(ps5tm_smphold_check_launch() == 0 && g_unmount_calls == 0 && ps5tm_smphold_count() == 1); }

  /* the app starts a title itself: everything else it holds goes first, the title itself stays */
  world();
  ps5tm_smphold_note("PPSA02664", 1);
  CHECK(ps5tm_smphold_release_for_start("PPSA02664") == 0 && g_unmount_calls == 0 && ps5tm_smphold_is_held("PPSA02664"));
  CHECK(ps5tm_smphold_release_for_start("PPSA02433") == 1 && g_unmount_calls == 1 && !strcmp(g_unmounted, "PPSA02664"));
  CHECK(ps5tm_smphold_count() == 0 && g_notices == 0);                                   /* the person started it: no notice */
  CHECK(ps5tm_smphold_release_for_start("PPSA02433") == 0 && g_unmount_calls == 1);       /* nothing held: cheap, nothing asked */
  world();
  ps5tm_smphold_note("PPSA02664", 1);
  ps5tm_smphold_note("CUSA00775", 1);
  CHECK(ps5tm_smphold_release_for_start("CUSA00775") == 1 && ps5tm_smphold_is_held("CUSA00775") && !ps5tm_smphold_is_held("PPSA02664"));
  world();
  ps5tm_smphold_note("PPSA02664", 1);
  g_up = 0;
  CHECK(ps5tm_smphold_release_for_start("PPSA02433") == 0 && ps5tm_smphold_count() == 1);   /* not answering: it does not hold the start back */
  world();
  ps5tm_smphold_note("PPSA02664", 1);
  g_unmount_http = 409;
  CHECK(ps5tm_smphold_release_for_start("PPSA02433") == 0 && ps5tm_smphold_count() == 1);   /* refused: stays, the start goes on regardless */


  /* ---- audit of 15.11.2026 */

  /* the held title itself runs: the person started it, so it is written off and never unmounted (F-52) */
  world();
  ps5tm_smphold_note("PPSA02664", 1);
  snprintf(g_run_title, sizeof(g_run_title), "PPSA02664");
  g_game = 1;
  g_now += 10 * PS5TM_SMPHOLD_GRACE_MS;
  CHECK(ps5tm_smphold_step() == 0 && g_unmount_calls == 0 && ps5tm_smphold_count() == 0 && !file_exists());
  g_game = 0;
  g_now += 10 * PS5TM_SMPHOLD_GRACE_MS;
  CHECK(ps5tm_smphold_step() == 0 && g_unmount_calls == 0);                              /* and after its end nothing comes to take it out */

  /* a start is refused while the held title itself plays: it is not what blocked, it stays untouched */
  world();
  ps5tm_smphold_note("PPSA02664", 1);
  snprintf(g_run_title, sizeof(g_run_title), "PPSA02664");
  snprintf(g_failed_next, sizeof(g_failed_next), "PPSA02433");
  CHECK(ps5tm_smphold_check_launch() == 0 && g_unmount_calls == 0 && ps5tm_smphold_count() == 0);
  world();
  ps5tm_smphold_note("PPSA02664", 1);
  snprintf(g_run_title, sizeof(g_run_title), "PPSA02664");
  CHECK(ps5tm_smphold_release_for_start("PPSA02433") == 0 && g_unmount_calls == 0 && ps5tm_smphold_count() == 0);

  /* the app starts the held title: its clock starts again (the game state needs seconds to show the start) */
  world();
  ps5tm_smphold_note("PPSA02664", 1);
  g_now += PS5TM_SMPHOLD_GRACE_MS - 1000;
  CHECK(ps5tm_smphold_release_for_start("PPSA02664") == 0 && ps5tm_smphold_is_held("PPSA02664"));
  g_now += 2000;                                                                         /* the half minute since the mount is over */
  CHECK(ps5tm_smphold_step() == 0 && g_unmount_calls == 0);
  g_now += PS5TM_SMPHOLD_GRACE_MS;
  CHECK(ps5tm_smphold_step() == 1 && g_unmount_calls == 1);

  /* ShadowMountPlus falls silent between the look and the question: the game stays written down (F-54) */
  world();
  ps5tm_smphold_note("PPSA02664", 1);
  g_find_kills_up = 1;
  g_known = 0;
  g_now += 10 * PS5TM_SMPHOLD_GRACE_MS;
  CHECK(ps5tm_smphold_step() == 0 && ps5tm_smphold_count() == 1 && g_unmount_calls == 0);
  g_find_kills_up = 0;
  g_up = 1;                                                                              /* it answers again and does not know the title: written off */
  CHECK(ps5tm_smphold_step() == 0 && ps5tm_smphold_count() == 0);

  /* ShadowMountPlus silent for ten minutes: it was stopped or restarted, nothing is held any more */
  world();
  ps5tm_smphold_note("PPSA02664", 1);
  g_up = 0;
  CHECK(ps5tm_smphold_step() == 0 && ps5tm_smphold_count() == 1);
  g_now += 10 * 60 * 1000 - 1;
  CHECK(ps5tm_smphold_step() == 0 && ps5tm_smphold_count() == 1);
  g_now += 1;
  CHECK(ps5tm_smphold_step() == 0 && ps5tm_smphold_count() == 0 && !file_exists());

  /* a refused unmount is asked again 10 s, 20 s, 40 s, 80 s later, and only the first refusal is logged (F-55) */
  world();
  ps5tm_smphold_note("PPSA02664", 1);
  g_unmount_http = 409;
  g_now += PS5TM_SMPHOLD_GRACE_MS;
  CHECK(ps5tm_smphold_step() == 0 && g_unmount_calls == 1 && g_warns_failed == 1);
  g_now += 9999;
  CHECK(ps5tm_smphold_step() == 0 && g_unmount_calls == 1);
  g_now += 1;
  CHECK(ps5tm_smphold_step() == 0 && g_unmount_calls == 2 && g_warns_failed == 1);       /* the second refusal is not logged again */
  g_now += 19999;
  CHECK(ps5tm_smphold_step() == 0 && g_unmount_calls == 2);
  g_now += 1;
  CHECK(ps5tm_smphold_step() == 0 && g_unmount_calls == 3);
  g_now += 39999;
  CHECK(ps5tm_smphold_step() == 0 && g_unmount_calls == 3);
  g_now += 1;
  g_unmount_http = 200;
  CHECK(ps5tm_smphold_step() == 1 && g_unmount_calls == 4 && ps5tm_smphold_count() == 0);
  world();                                                                               /* mounted afresh: the waiting starts over */
  ps5tm_smphold_note("PPSA02664", 1);
  g_unmount_http = 409;
  g_now += PS5TM_SMPHOLD_GRACE_MS;
  ps5tm_smphold_step();
  g_now += 1000;
  ps5tm_smphold_note("PPSA02664", 1);
  g_now += PS5TM_SMPHOLD_GRACE_MS;
  CHECK(ps5tm_smphold_step() == 0 && g_unmount_calls == 2 && g_warns_failed == 2);       /* a new first refusal, logged */

  /* the ninth game is not forgotten silently: eight are held, the log says so (the count stays) (F-59) */
  world();
  for(int i = 0; i < 9; i++) {
    char id[16];
    snprintf(id, sizeof(id), "PPSA%05d", 200 + i);
    ps5tm_smphold_note(id, 1);
  }
  CHECK(ps5tm_smphold_count() == 8);

  /* the file is written under the boot of the console, and not read under another boot (F-51) */
  world();
  g_boot = 1234;
  ps5tm_smphold_note("PPSA02664", 1);
  CHECK(file_has("boot=1234") && file_has("PPSA02664"));
  world();
  g_boot = 1000;
  { FILE *f = fopen(PS5TM_DATA_DIR "/smp-mounted.txt", "w"); fputs("boot=2000\nPPSA02664\n", f); fclose(f); }
  ps5tm_smphold_test_load();
  CHECK(ps5tm_smphold_count() == 0 && !file_exists());                                   /* another boot: nothing the app mounted is left */
  world();
  g_boot = 1000;
  { FILE *f = fopen(PS5TM_DATA_DIR "/smp-mounted.txt", "w"); fputs("boot=1003\nPPSA02664\nCUSA00775\n", f); fclose(f); }
  ps5tm_smphold_test_load();
  CHECK(ps5tm_smphold_count() == 2);                                                     /* the same boot (a second or two of rounding) */
  world();
  g_boot = 1000;
  { FILE *f = fopen(PS5TM_DATA_DIR "/smp-mounted.txt", "w"); fputs("PPSA02664\n", f); fclose(f); }
  ps5tm_smphold_test_load();
  CHECK(ps5tm_smphold_count() == 1);                                                     /* a file of an older version: read as it is */
  world();
  g_boot = 0;
  { FILE *f = fopen(PS5TM_DATA_DIR "/smp-mounted.txt", "w"); fputs("boot=2000\nPPSA02664\n", f); fclose(f); }
  ps5tm_smphold_test_load();
  CHECK(ps5tm_smphold_count() == 1);                                                     /* the boot is not known now: no reason to throw it away */
  ps5tm_smphold_test_reset();
  unlink(PS5TM_DATA_DIR "/smp-mounted.txt");

  /* a second release of the same title at the same moment does nothing more: the entry is gone, and the clock of a re-mounted one has not run out */
  world();
  ps5tm_smphold_note("PPSA02664", 1);
  g_now += PS5TM_SMPHOLD_GRACE_MS;
  CHECK(ps5tm_smphold_step() == 1 && g_unmount_calls == 1);
  CHECK(ps5tm_smphold_release_for_start("PPSA02433") == 0 && g_unmount_calls == 1);

  /* The real watcher world (not the test one) against the lines the test console wrote: the first start of PPSA99996 was refused while
     PPSA99995 was mounted through the app. */
  ps5tm_smphold_test_env(NULL);
  ps5tm_smphold_test_reset();
  g_smp_available = 1; g_real_unmounts = 0; g_kl_n = 0;
  kl_add("<118>[ps5tm.elf] [INFO] smp_mounted: PPSA99995 ueber ShadowMountPlus eingehaengt.");
  ps5tm_smphold_note("PPSA99995", 1);
  ps5tm_smphold_mark();                                                         /* what the thread does when the app starts to hold */
  CHECK(ps5tm_smphold_check_launch() == 0 && g_real_unmounts == 0);              /* nothing happened yet */
  kl_add("[SceShellCore] Libc Heap Status: free 27%, in-use 4464.1 KB, trend +2.3 KB/min, peak 5459.6 KB, when 26108 [sec]");
  kl_add("[SceLncService] launchApp(PPSA99996)");
  kl_add("[SceLncService] W:\\Build\\J03718123\\vsh\\shell\\shell_core\\lnc_service\\lnc_app_info.cpp(184) query: LNC_CHECK::0x80940033 (m_is_pkg value is false)");
  CHECK(ps5tm_smphold_check_launch() == 0 && g_real_unmounts == 0);              /* the request alone is not a refusal */
  /* a line of the app's own, with the words in it, is no refusal either */
  kl_add("<118>[ps5tm.elf] [INFO] library_launch_offered: LaunchFlowError.PPSA00001 als Spielname");
  kl_add("ThemeOverrideType: InheritProperties from supposedParent(Type:Sce.PlayStation.PUI.UI3.Scene, Name:LaunchFlowError.PPSA99996) to (Type:x, Name:Noname)");
  CHECK(ps5tm_smphold_check_launch() == 0 && g_real_unmounts == 0 && ps5tm_smphold_is_held("PPSA99995"));
  kl_add("[SceShellUI] I/PSM.UI : ** Unload enqueue: LaunchFlowError.PPSA99996 : Scene");
  CHECK(ps5tm_smphold_check_launch() == 1 && g_real_unmounts == 1 && !ps5tm_smphold_is_held("PPSA99995"));
  CHECK(ps5tm_smphold_check_launch() == 0 && g_real_unmounts == 1);              /* the same line is not read twice */

  /* a refusal from before the app held anything does not count */
  g_real_unmounts = 0;
  ps5tm_smphold_note("PPSA99995", 1);
  kl_add("[SceShellUI] I/PSM.UI : ** Unload enqueue: LaunchFlowError.PPSA99996 : Scene");
  ps5tm_smphold_mark();
  CHECK(ps5tm_smphold_check_launch() == 0 && g_real_unmounts == 0 && ps5tm_smphold_is_held("PPSA99995"));
  kl_add("[SceShellUI] I/PSM.UI : SceneQ : Loaded[0.00msec] : LaunchFlowError.FAKE86922 : Scene");   /* a tile refused: the case of 14.11. */
  CHECK(ps5tm_smphold_check_launch() == 1 && g_real_unmounts == 1);

  /* the log could not be read when the mark was made: the next look makes it again, and a refusal from before it does not count (F-56) */
  ps5tm_smphold_test_env(NULL);
  ps5tm_smphold_test_reset();
  g_real_unmounts = 0; g_kl_n = 0;
  kl_add("[SceShellUI] I/PSM.UI : ** Unload enqueue: LaunchFlowError.PPSA99996 : Scene");
  ps5tm_smphold_note("PPSA99995", 1);
  g_kl_fail = 1;
  ps5tm_smphold_mark();
  g_kl_fail = 0;
  CHECK(ps5tm_smphold_check_launch() == 0 && g_real_unmounts == 0 && ps5tm_smphold_is_held("PPSA99995"));
  kl_add("[SceShellUI] I/PSM.UI : ** Unload enqueue: LaunchFlowError.PPSA99997 : Scene");
  CHECK(ps5tm_smphold_check_launch() == 1 && g_real_unmounts == 1);


  /* ---- an own tile refused while a game waits: started again after two seconds, at most three tries ---- */
  world(); g_game = 0;
  g_failed_next[0] = 0;
  ps5tm_smphold_tile_tick();                                          /* no game: nothing read, nothing done */
  CHECK(g_start_calls == 0 && g_resets == 0);

  world(); g_game = 1;
  ps5tm_smphold_tile_tick();                                          /* a game: the log is looked at, from now on */
  CHECK(g_resets == 1);
  snprintf(g_failed_next, sizeof(g_failed_next), "FAKE86922");
  ps5tm_smphold_tile_tick();                                          /* refused: not at once */
  CHECK(g_start_calls == 0);
  g_now += 1000; ps5tm_smphold_tile_tick();
  CHECK(g_start_calls == 0);
  g_now += 1100; ps5tm_smphold_tile_tick();                           /* after two seconds */
  CHECK(g_start_calls == 1 && !strcmp(g_start_id, "FAKE86922"));
  g_now += 5000; ps5tm_smphold_tile_tick();                           /* accepted: not again */
  CHECK(g_start_calls == 1);

  /* another title that was refused: the app does not start it */
  world(); g_game = 1;
  ps5tm_smphold_tile_tick();
  snprintf(g_failed_next, sizeof(g_failed_next), "PPSA01325");
  ps5tm_smphold_tile_tick();
  g_now += 3000; ps5tm_smphold_tile_tick();
  CHECK(g_start_calls == 0);

  /* the console refuses the start again: three tries four seconds apart, then it is left */
  world(); g_game = 1; g_start_rc = -1;
  ps5tm_smphold_tile_tick();
  snprintf(g_failed_next, sizeof(g_failed_next), "FAKE86921");
  ps5tm_smphold_tile_tick();
  g_now += 2100; ps5tm_smphold_tile_tick(); CHECK(g_start_calls == 1);
  g_now += 4100; ps5tm_smphold_tile_tick(); CHECK(g_start_calls == 2);
  g_now += 4100; ps5tm_smphold_tile_tick(); CHECK(g_start_calls == 3);
  g_now += 4100; ps5tm_smphold_tile_tick(); CHECK(g_start_calls == 3);   /* no fourth */

  /* the tile is up before the second is over: nothing is started */
  world(); g_game = 1;
  ps5tm_smphold_tile_tick();
  snprintf(g_failed_next, sizeof(g_failed_next), "FAKE86922");
  ps5tm_smphold_tile_tick();
  snprintf(g_run_title, sizeof(g_run_title), "FAKE86922");
  g_now += 2100; ps5tm_smphold_tile_tick();
  CHECK(g_start_calls == 0);

  /* a console that refuses for good is not pestered: three starts in a minute */
  world(); g_game = 1;
  ps5tm_smphold_tile_tick();
  for(int i = 0; i < 6; i++) {
    snprintf(g_failed_next, sizeof(g_failed_next), "FAKE86922");
    ps5tm_smphold_tile_tick();
    g_now += 2100; ps5tm_smphold_tile_tick();
    g_now += 1000;
  }
  CHECK(g_start_calls == 3);

  /* with games held, the one reader of the log is the check for held games: it hands the refusal on */
  world(); g_game = 1;
  ps5tm_smphold_note("PPSA99994", 1);
  snprintf(g_failed_next, sizeof(g_failed_next), "FAKE86921");
  ps5tm_smphold_check_launch();
  g_now += 2100; ps5tm_smphold_tile_tick();
  CHECK(g_start_calls == 1 && !strcmp(g_start_id, "FAKE86921"));

  g_smp_available = 0;
  ps5tm_smphold_test_env(NULL);
  ps5tm_smphold_test_reset();

  unlink(PS5TM_DATA_DIR "/smp-mounted.txt");
  printf("%d checks, %d failed\n", g_n, g_fail);
  return g_fail ? 1 : 0;
}
