/* Host test of src/bgpause.c: when the game that waits in the background is paused (and when it is not). */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "bgpause.h"
#include "ps5tm.h"

static int g_fail, g_n;
#define CHECK(c) do { g_n++; if(!(c)) { g_fail++; printf("FAIL line %d: %s\n", __LINE__, #c); } } while(0)

/* ---- stubs of what bgpause.c links against */
ps5tm_config_t g_config;
void ps5tm_config_lock(void) {}
void ps5tm_config_unlock(void) {}
static uint64_t g_now;
uint64_t ps5tm_mono_ms(void) { return g_now; }
static int g_logged_warn;
void ps5tm_log(const char *level, const char *code, const char *fmt, ...) {
  (void)fmt; (void)code;
  if(!strcmp(level, "WARN")) g_logged_warn++;
}
static ps5tm_gamestate_t g_gs;
static int g_measured = 1;
static uint64_t g_age;
void ps5tm_gamestate_get(ps5tm_gamestate_t *o) { *o = g_gs; }
int ps5tm_gamestate_measured(void) { return g_measured; }
uint64_t ps5tm_gamestate_age_ms(void) { return g_age; }
static int g_smp = 1;
int ps5tm_smp_available(char *v, size_t n) { (void)v; (void)n; return g_smp; }
unsigned char *ps5tm_blob_inflate(const unsigned char *z, unsigned long a, unsigned long b, size_t *n) { (void)z; (void)a; (void)b; (void)n; return NULL; }
int ps5tm_loader_inject_mem(const unsigned char *d, unsigned long l, const char **why) { (void)d; (void)l; if(why) *why = NULL; return -1; }
const unsigned char ps5tm_bgpause_helper[1] = {0};
const unsigned long ps5tm_bgpause_helper_len = 1;
const unsigned long ps5tm_bgpause_helper_raw_len = 1;

static ps5tm_bgpause_in_t
game(const char *title, unsigned app, int fg, int home) {
  ps5tm_bgpause_in_t in;
  memset(&in, 0, sizeof(in));
  in.enabled = 1; in.measured = 1; in.smp_up = 1;
  snprintf(in.title_id, sizeof(in.title_id), "%s", title);
  in.app_id = app; in.foreground = fg; in.home_scene = home;
  return in;
}

static void
decide_tests(void) {
  ps5tm_bgpause_state_t st;
  memset(&st, 0, sizeof(st));
  ps5tm_bgpause_in_t in = game("CUSA45555", 0xa018, 1, 0);

  /* in front: nothing */
  CHECK(ps5tm_bgpause_decide(&st, &in, 0) == 0);
  /* the home screen comes up: the first look pauses */
  in = game("CUSA45555", 0xa018, 0, 1);
  CHECK(ps5tm_bgpause_decide(&st, &in, 1000) == 1);
  /* once per stay in the background */
  CHECK(ps5tm_bgpause_decide(&st, &in, 2000) == 0);
  CHECK(ps5tm_bgpause_decide(&st, &in, 2500) == 0);
  CHECK(ps5tm_bgpause_decide(&st, &in, 9000) == 0);
  /* the tile is tapped (a splash scene, not the home screen): still the same stay, nothing again */
  ps5tm_bgpause_in_t tap = game("CUSA45555", 0xa018, 0, 0);
  CHECK(ps5tm_bgpause_decide(&st, &tap, 9500) == 0);
  CHECK(ps5tm_bgpause_decide(&st, &in, 10000) == 0);          /* back at the home screen after the tile: not a second pause */
  CHECK(ps5tm_bgpause_decide(&st, &in, 10500) == 0);
  /* the game is in front again (resumed), later again at the home screen: a new stay */
  in = game("CUSA45555", 0xa018, 1, 0);
  CHECK(ps5tm_bgpause_decide(&st, &in, 20000) == 0);
  in = game("CUSA45555", 0xa018, 0, 1);
  CHECK(ps5tm_bgpause_decide(&st, &in, 21000) == 1);
}

static void
guard_tests(void) {
  ps5tm_bgpause_state_t st;
  /* an overlay (home_scene 0) never pauses, however long it stays; the home screen after it does, once */
  memset(&st, 0, sizeof(st));
  ps5tm_bgpause_in_t in = game("PPSA02985", 0x8018, 0, 1);
  ps5tm_bgpause_in_t ov = game("PPSA02985", 0x8018, 0, 0);
  CHECK(ps5tm_bgpause_decide(&st, &ov, 1000) == 0);
  CHECK(ps5tm_bgpause_decide(&st, &ov, 1500) == 0);
  CHECK(ps5tm_bgpause_decide(&st, &ov, 9000) == 0);
  CHECK(ps5tm_bgpause_decide(&st, &in, 9500) == 1);
  CHECK(ps5tm_bgpause_decide(&st, &in, 10000) == 0);

  /* never: setting off, no ShadowMountPlus, not measured, no app id, no game */
  memset(&st, 0, sizeof(st));
  in = game("PPSA02985", 0x8018, 0, 1); in.enabled = 0;
  CHECK(ps5tm_bgpause_decide(&st, &in, 1000) == 0 && ps5tm_bgpause_decide(&st, &in, 2000) == 0);
  in = game("PPSA02985", 0x8018, 0, 1); in.smp_up = 0;
  CHECK(ps5tm_bgpause_decide(&st, &in, 3000) == 0 && ps5tm_bgpause_decide(&st, &in, 4000) == 0);
  in = game("PPSA02985", 0x8018, 0, 1); in.measured = 0;
  CHECK(ps5tm_bgpause_decide(&st, &in, 5000) == 0 && ps5tm_bgpause_decide(&st, &in, 6000) == 0);
  in = game("PPSA02985", 0, 0, 1);
  CHECK(ps5tm_bgpause_decide(&st, &in, 7000) == 0 && ps5tm_bgpause_decide(&st, &in, 8000) == 0);
  in = game("", 0x8018, 0, 1);
  CHECK(ps5tm_bgpause_decide(&st, &in, 9000) == 0 && ps5tm_bgpause_decide(&st, &in, 10000) == 0);

  /* never the app's own tiles, nor another homebrew */
  const char *no[] = { "FAKE86921", "FAKE86922", "PPSA86921", "FAKE42007", "NPXS40002", "FAKE77031" };
  for(unsigned i = 0; i < sizeof(no) / sizeof(no[0]); i++) {
    memset(&st, 0, sizeof(st));
    in = game(no[i], 0x8018, 0, 1);
    CHECK(ps5tm_bgpause_decide(&st, &in, 1000) == 0 && ps5tm_bgpause_decide(&st, &in, 2000) == 0);
  }

  /* another game becomes the resident one: it counts on its own */
  memset(&st, 0, sizeof(st));
  in = game("PPSA02985", 0x8018, 0, 1);
  CHECK(ps5tm_bgpause_decide(&st, &in, 1000) == 1 && ps5tm_bgpause_decide(&st, &in, 1500) == 0);
  in = game("CUSA00775", 0xc018, 0, 1);
  CHECK(ps5tm_bgpause_decide(&st, &in, 3000) == 1 && ps5tm_bgpause_decide(&st, &in, 3500) == 0);

  /* the game ends (none resident): the next stay counts again, also for the same id */
  in = game("", 0, 0, 1);
  CHECK(ps5tm_bgpause_decide(&st, &in, 5000) == 0);
  in = game("CUSA00775", 0xc018, 0, 1);
  CHECK(ps5tm_bgpause_decide(&st, &in, 6000) == 1 && ps5tm_bgpause_decide(&st, &in, 6500) == 0);
}

static void
runtime_tests(void) {
  ps5tm_bgpause_test_reset();
  memset(&g_gs, 0, sizeof(g_gs));
  g_config.bg_pause = 1;

  /* wants_fast: a counted game is resident and the setting is on */
  snprintf(g_gs.title_id, sizeof(g_gs.title_id), "CUSA45555");
  CHECK(ps5tm_bgpause_wants_fast() == 1);
  g_config.bg_pause = 0;
  CHECK(ps5tm_bgpause_wants_fast() == 0);
  g_config.bg_pause = 1;
  snprintf(g_gs.title_id, sizeof(g_gs.title_id), "FAKE86922");
  CHECK(ps5tm_bgpause_wants_fast() == 0);
  g_gs.title_id[0] = 0;
  CHECK(ps5tm_bgpause_wants_fast() == 0);
  snprintf(g_gs.title_id, sizeof(g_gs.title_id), "CUSA45555");
  g_measured = 0;
  CHECK(ps5tm_bgpause_wants_fast() == 0);
  g_measured = 1;

  /* the tick: the helper cannot be started on the host - the tick must go through without a crash */
  g_gs.game_app_id = 0xa018; g_gs.foreground = 0; g_gs.home_scene = 1;
  g_now = 1000; ps5tm_bgpause_tick();
  g_now = 1500; ps5tm_bgpause_tick();
  g_now = 2000; ps5tm_bgpause_tick();
  CHECK(g_logged_warn == 0);
}

int
main(void) {
  decide_tests();
  guard_tests();
  runtime_tests();
  printf("%d checks, %d failed\n", g_n, g_fail);
  return g_fail ? 1 : 0;
}
