/* Host test of src/fpkgdup.c: a game that lies at Kraken Mount and ShadowMountPlus is unmounted at ShadowMountPlus after it ended;
   nothing else is touched. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ps5tm.h"
#include "third_party/cJSON.h"

static int g_fail, g_n;
#define CHECK(c) do { g_n++; if(!(c)) { g_fail++; printf("FAIL line %d: %s\n", __LINE__, #c); } } while(0)

void ps5tm_fpkgdup_step(char playing[12]);

/* ---- stubs of what fpkgdup.c links against */
static char g_title[12], g_state[24] = "kein Spiel";
static int g_measured = 1;
static uint64_t g_age;
static int g_conflict, g_mounted = 1, g_unmounts, g_notices, g_warns, g_pkg;
static char g_conflict_id[12] = "PPSA02664";

void ps5tm_log(const char *level, const char *code, const char *fmt, ...) {
  (void)level; (void)fmt;
  if(code && !strcmp(code, "fpkg_dup_unmount_failed")) g_warns++;
}
void ps5tm_notify_plain(const char *fmt, ...) { (void)fmt; g_notices++; }
#undef pthread_create
int ps5tm_thread_create(pthread_t *t, const pthread_attr_t *a, void *(*f)(void *), void *x) { return pthread_create(t, a, f, x); }
int ps5tm_gamestate_measured(void) { return g_measured; }
uint64_t ps5tm_gamestate_age_ms(void) { return g_age; }
void ps5tm_gamestate_get(ps5tm_gamestate_t *o) {
  memset(o, 0, sizeof(*o));
  snprintf(o->title_id, sizeof(o->title_id), "%s", g_title);
  o->state = g_state;
}
cJSON *ps5tm_mounters_conflicts(void) {
  cJSON *arr = cJSON_CreateArray();
  if(g_conflict) {
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "title_id", g_conflict_id);
    cJSON *w = cJSON_AddObjectToObject(o, "other");
    cJSON_AddStringToObject(w, "by", "smp");
    cJSON_AddItemToArray(arr, o);
  }
  return arr;
}
int ps5tm_smp_find(const char *id, ps5tm_smp_game_t *out) {
  memset(out, 0, sizeof(*out));
  snprintf(out->title_id, sizeof(out->title_id), "%s", id);
  snprintf(out->source_type, sizeof(out->source_type), "%s", g_pkg ? "pkg" : "image");
  out->mounted = g_mounted;
  return 0;
}
int ps5tm_smpremount(const char *id, const char *a, char *msg, size_t n) {
  (void)id; (void)msg; (void)n;
  CHECK(!strcmp(a, "unmount"));
  g_unmounts++;
  return 200;
}
int ps5tm_library_launch_info(const char *id, char *name, size_t nn, char *link, size_t ln) {
  (void)link; (void)ln;
  snprintf(name, nn, "Spiel %s", id);
  return 0;
}

static void reset(void) {
  g_title[0] = 0;
  snprintf(g_state, sizeof(g_state), "kein Spiel");
  g_measured = 1; g_age = 0; g_conflict = 1; g_mounted = 1; g_pkg = 0;
  g_unmounts = g_notices = g_warns = 0;
  snprintf(g_conflict_id, sizeof(g_conflict_id), "PPSA02664");
}
static void play(char *playing, const char *id) {
  snprintf(g_title, sizeof(g_title), "%s", id);
  snprintf(g_state, sizeof(g_state), "%s", "läuft");
  ps5tm_fpkgdup_step(playing);
}
static void quit_game(char *playing) {
  g_title[0] = 0;
  snprintf(g_state, sizeof(g_state), "kein Spiel");
  ps5tm_fpkgdup_step(playing);
}

int main(void) {
  char playing[12] = "";

  /* nothing played before: nothing happens */
  reset(); ps5tm_fpkgdup_step(playing);
  CHECK(g_unmounts == 0 && !playing[0]);

  /* a game at both mounters ends: unmounted once, the person is told */
  reset(); playing[0] = 0;
  play(playing, "PPSA02664"); CHECK(!strcmp(playing, "PPSA02664") && g_unmounts == 0);
  quit_game(playing);
  CHECK(g_unmounts == 1 && g_notices == 1 && !playing[0]);
  quit_game(playing); CHECK(g_unmounts == 1);                       /* not a second time */

  /* no conflict: nothing */
  reset(); playing[0] = 0; g_conflict = 0;
  play(playing, "PPSA02664"); quit_game(playing);
  CHECK(g_unmounts == 0);

  /* a conflict with another title: nothing */
  reset(); playing[0] = 0; snprintf(g_conflict_id, sizeof(g_conflict_id), "PPSA00001");
  play(playing, "PPSA02664"); quit_game(playing);
  CHECK(g_unmounts == 0);

  /* ShadowMountPlus does not hold it mounted: nothing to unmount */
  reset(); playing[0] = 0; g_mounted = 0;
  play(playing, "PPSA02664"); quit_game(playing);
  CHECK(g_unmounts == 0);

  /* an installed package at ShadowMountPlus is not its mount */
  reset(); playing[0] = 0; g_pkg = 1;
  play(playing, "PPSA02664"); quit_game(playing);
  CHECK(g_unmounts == 0);

  /* the app's own tile is never touched */
  reset(); playing[0] = 0; snprintf(g_conflict_id, sizeof(g_conflict_id), "FAKE86921");
  play(playing, "FAKE86921"); quit_game(playing);
  CHECK(g_unmounts == 0);

  /* a state that cannot be told, or an old reading, is no end */
  reset(); playing[0] = 0;
  play(playing, "PPSA02664");
  g_title[0] = 0; snprintf(g_state, sizeof(g_state), "nicht ermittelbar");
  ps5tm_fpkgdup_step(playing);
  CHECK(g_unmounts == 0 && !strcmp(playing, "PPSA02664"));
  snprintf(g_state, sizeof(g_state), "kein Spiel"); g_age = 60000;
  ps5tm_fpkgdup_step(playing);
  CHECK(g_unmounts == 0 && !strcmp(playing, "PPSA02664"));
  g_age = 0; g_measured = 0;
  ps5tm_fpkgdup_step(playing);
  CHECK(g_unmounts == 0);
  g_measured = 1;
  ps5tm_fpkgdup_step(playing);
  CHECK(g_unmounts == 1);

  /* still playing: nothing */
  reset(); playing[0] = 0;
  play(playing, "PPSA02664"); play(playing, "PPSA02664");
  CHECK(g_unmounts == 0);

  printf("%d Prüfungen, %d Fehler\n", g_n, g_fail);
  return g_fail ? 1 : 0;
}
