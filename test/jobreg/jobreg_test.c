/* Host test of src/jobreg.c: the one list of jobs that do not run beside each other. Needs clang/gcc on Linux/WSL. */
#include <stdio.h>
#include <string.h>

#include "ps5tm.h"

static int g_busy[PS5TM_JOB_COUNT];
static int g_asked_move;
#define STUB(fn, id) int fn(void) { return g_busy[id]; }
STUB(ps5tm_saves_busy, PS5TM_JOB_SAVES)
STUB(ps5tm_pkgsplit_busy, PS5TM_JOB_PKGSPLIT)
STUB(ps5tm_pkginst_busy, PS5TM_JOB_PKGINST)
STUB(ps5tm_pkgscan_busy, PS5TM_JOB_PKGSCAN)
STUB(ps5tm_gamedelete_busy, PS5TM_JOB_GAMEDELETE)
STUB(ps5tm_filemgr_busy, PS5TM_JOB_FILEMGR)
STUB(ps5tm_ampr_busy, PS5TM_JOB_AMPR)
STUB(ps5tm_imgjob_busy, PS5TM_JOB_IMGJOB)
STUB(ps5tm_disc_busy, PS5TM_JOB_DISC)
STUB(ps5tm_usbeject_busy, PS5TM_JOB_USBEJECT)
STUB(ps5tm_gameconvert_busy, PS5TM_JOB_GAMECONVERT)
STUB(ps5tm_gamecopy_busy, PS5TM_JOB_GAMECOPY)
static int g_move_local;
int ps5tm_gamemove_busy_local(void) { return g_move_local; }
int ps5tm_gamemove_busy(void) { g_asked_move++; return g_move_local || g_busy[PS5TM_JOB_GAMEMOVE]; }

static int g_fail, g_n;
#define CHECK(c) do { g_n++; if(!(c)) { g_fail++; printf("FAIL line %d: %s\n", __LINE__, #c); } } while(0)

int
main(void) {
  char why[200];
  CHECK(!ps5tm_jobs_busy_except(-1, PS5TM_JOBS_READERS, why, sizeof(why)));
  for(int j = 0; j < PS5TM_JOB_COUNT; j++) {
    memset(g_busy, 0, sizeof(g_busy));
    g_busy[j] = 1;
    why[0] = 0;
    int reads = j == PS5TM_JOB_PKGSCAN;
    /* a job beside which nothing else may start (the package search only reads: it stops an eject, no start) */
    CHECK(ps5tm_jobs_busy_except(-1, PS5TM_JOBS_READERS, why, sizeof(why)) == 1 && why[0]);
    CHECK(ps5tm_jobs_busy_except(-1, 0, NULL, 0) == (reads ? 0 : 1));
    /* a module never counts itself */
    CHECK(ps5tm_jobs_busy_except(j, PS5TM_JOBS_READERS, why, sizeof(why)) == 0);
    /* and every other module sees it */
    for(int k = 0; k < PS5TM_JOB_COUNT; k++) {
      if(k == j) continue;
      CHECK(ps5tm_jobs_busy_except(k, PS5TM_JOBS_READERS, why, sizeof(why)) == 1);
    }
  }
  /* no call to ShadowMountPlus when asked not to */
  memset(g_busy, 0, sizeof(g_busy));
  g_asked_move = 0;
  CHECK(!ps5tm_jobs_busy_except(-1, PS5TM_JOBS_NO_SMP, NULL, 0) && g_asked_move == 0);
  CHECK(!ps5tm_jobs_busy_except(-1, 0, NULL, 0) && g_asked_move == 1);
  /* a move the app itself holds (being started, waiting for the tile) counts even when ShadowMountPlus is not asked */
  g_asked_move = 0;
  g_move_local = 1;
  CHECK(ps5tm_jobs_busy_except(-1, PS5TM_JOBS_NO_SMP, why, sizeof(why)) == 1 && g_asked_move == 0 && why[0]);
  CHECK(ps5tm_jobs_busy_except(PS5TM_JOB_GAMEMOVE, PS5TM_JOBS_NO_SMP, NULL, 0) == 0); /* but not for the move itself */
  g_move_local = 0;
  /* the move is asked last: a job found earlier ends the asking */
  g_asked_move = 0;
  g_busy[PS5TM_JOB_FILEMGR] = 1;
  CHECK(ps5tm_jobs_busy_except(-1, 0, NULL, 0) == 1 && g_asked_move == 0);
  printf("%d checks, %d failed\n", g_n, g_fail);
  return g_fail ? 1 : 0;
}
