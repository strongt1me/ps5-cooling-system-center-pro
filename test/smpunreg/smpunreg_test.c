/* Host test of src/smpunreg.h: the order of the steps that take a title off ShadowMountPlus (18.11.2026). */
#include <stdio.h>
#include <string.h>

#include "smpunreg.h"

static int g_fail, g_n;
#define CHECK(c) do { g_n++; if(!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); g_fail++; } } while(0)

typedef struct {
  int  mounted;                 /* the state ShadowMountPlus shows now */
  int  unmount_ok;              /* what the unmount answers */
  int  unmount_unmounts;        /* an unmount that works makes the title "not mounted" */
  int  busy_first;              /* the first N uninstalls answer "busy" */
  const char *other_refusal;    /* else this refusal, every time */
  int  unmount_calls, uninstall_calls, mounted_calls;
  int  pauses[16], npause;
} sim_t;

static int s_mounted(void *c) { sim_t *s = c; s->mounted_calls++; return s->mounted; }
static int s_unmount(void *c, char *err, size_t n) {
  sim_t *s = c;
  s->unmount_calls++;
  if(!s->unmount_ok) { snprintf(err, n, "unmount refused"); return -1; }
  if(s->unmount_unmounts) s->mounted = 0;
  return 0;
}
static int s_uninstall(void *c, char *err, size_t n) {
  sim_t *s = c;
  s->uninstall_calls++;
  if(s->other_refusal) { snprintf(err, n, "%s", s->other_refusal); return -1; }
  if(s->uninstall_calls <= s->busy_first) { snprintf(err, n, "ShadowMountPlus: title runtime could not be released; its mount or sandbox is still busy"); return -1; }
  return 0;
}
static void s_pause(void *c, int sec) { sim_t *s = c; if(s->npause < 16) s->pauses[s->npause++] = sec; }

static int run(sim_t *s, char *err, size_t n, int *unmounts) {
  ps5tm_unreg_ops_t o = { s_mounted, s_unmount, s_uninstall, s_pause, s };
  return ps5tm_unreg_run(&o, PS5TM_UNREG_TRIES, unmounts, err, n);
}

int
main(void) {
  char err[300];
  int um;

  /* the words */
  CHECK(ps5tm_unreg_busy("ShadowMountPlus: title runtime could not be released; its mount or sandbox is still busy"));
  CHECK(ps5tm_unreg_busy("Device BUSY"));
  CHECK(ps5tm_unreg_busy("could not be released"));
  CHECK(!ps5tm_unreg_busy("game is active"));
  CHECK(!ps5tm_unreg_busy("title not found"));
  CHECK(!ps5tm_unreg_busy(""));
  CHECK(!ps5tm_unreg_busy(NULL));
  CHECK(ps5tm_unreg_has("abc busy", "busy") && !ps5tm_unreg_has("abc bus", "busy") && !ps5tm_unreg_has("abc", ""));

  /* not mounted, accepted at once: no unmount, no pause */
  { sim_t s = { 0 }; CHECK(run(&s, err, sizeof(err), &um) == 0 && um == 0 && s.unmount_calls == 0 && s.uninstall_calls == 1 && s.npause == 0); }

  /* mounted: unmounted first, one pause for the mount point, then accepted */
  { sim_t s = { .mounted = 1, .unmount_ok = 1, .unmount_unmounts = 1 };
    CHECK(run(&s, err, sizeof(err), &um) == 0 && um == 1 && s.unmount_calls == 1 && s.uninstall_calls == 1 && s.npause == 1 && s.pauses[0] == 1); }

  /* the unmount is refused (the title runs, or ShadowMountPlus says no): the uninstall is still asked */
  { sim_t s = { .mounted = 1, .unmount_ok = 0 };
    CHECK(run(&s, err, sizeof(err), &um) == 0 && s.unmount_calls == 1 && s.uninstall_calls == 1); }

  /* busy twice, then accepted: three tries in all, a pause of 3 s between them */
  { sim_t s = { .busy_first = 2 };
    CHECK(run(&s, err, sizeof(err), &um) == 0 && s.uninstall_calls == 3 && s.npause == 2 && s.pauses[0] == 3 && s.pauses[1] == 3); }

  /* busy every time: three tries, then it gives up and says what ShadowMountPlus said last */
  { sim_t s = { .busy_first = 99 };
    CHECK(run(&s, err, sizeof(err), &um) == -1 && s.uninstall_calls == 3 && strstr(err, "still busy") != NULL && s.npause == 2); }

  /* mounted and busy: the unmount is tried again each round as long as the title shows as mounted */
  { sim_t s = { .mounted = 1, .unmount_ok = 1, .unmount_unmounts = 0, .busy_first = 99 };
    CHECK(run(&s, err, sizeof(err), &um) == -1 && s.unmount_calls == 3 && um == 3 && s.uninstall_calls == 3); }

  /* once unmounted, it is not unmounted again in the next round */
  { sim_t s = { .mounted = 1, .unmount_ok = 1, .unmount_unmounts = 1, .busy_first = 1 };
    CHECK(run(&s, err, sizeof(err), &um) == 0 && s.unmount_calls == 1 && s.uninstall_calls == 2); }

  /* another refusal is an answer: no second try, no pause */
  { sim_t s = { .other_refusal = "game is active" };
    CHECK(run(&s, err, sizeof(err), &um) == -1 && s.uninstall_calls == 1 && s.npause == 0 && !strcmp(err, "game is active")); }

  printf("%d Prüfungen, %d Fehler\n", g_n, g_fail);
  return g_fail ? 1 : 0;
}
