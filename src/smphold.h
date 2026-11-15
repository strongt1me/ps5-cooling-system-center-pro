/* PS5 Cooling & System Center - Pro: a game the app mounted through ShadowMountPlus and nobody started (smphold.c, 14.11.2026). */
#ifndef PS5TM_SMPHOLD_H
#define PS5TM_SMPHOLD_H

#include <stddef.h>
#include <stdint.h>

/* So long after the app mounted it, a game that is not started is unmounted again (14.11.2026: first two minutes; the person found
   that long, nothing starts meanwhile, and shortened it to this together with the two ways below, which release it at once). */
#define PS5TM_SMPHOLD_GRACE_MS (30u * 1000u)

/* The world the watcher looks at; smphold.c has the real one, a test brings its own. */
typedef struct {
  int      (*find)(const char *id, int *mounted);      /* 0 and *mounted when ShadowMountPlus knows the title */
  int      (*smp_up)(void);                             /* ShadowMountPlus answers */
  int      (*game_running)(void);                       /* a game plays, or it is not known (yet, any more: an old reading) */
  int      (*job_running)(void);                        /* one of the app's own jobs works */
  int      (*unmount)(const char *id, char *msg, size_t n); /* an HTTP status, 200 done */
  uint64_t (*now_ms)(void);
  /* The person is told what happened; `other` is the title whose start was refused, "" after the grace. NULL: the notification
     on the console. */
  void     (*notice)(const char *held_id, const char *other);
  /* Did the console refuse to start a title since the last call (kernel log line "LaunchFlowError.<id>")? 1 and its id; with `reset`
     it forgets what was before and says 0. NULL: nothing is watched. Called only by the watcher thread. */
  int      (*launch_failed)(int reset, char *id, size_t n);
  /* Is this very title the one that runs (a fresh reading)? Then the person started it: it is written off, never unmounted. NULL: never. */
  int      (*title_running)(const char *id);
  /* The boot of the console (seconds, kern.boottime); the file of held games is written under it and not read under another boot. 0 or NULL:
     not known, the file is read as it is. */
  uint64_t (*boot_id)(void);
} ps5tm_smphold_env_t;

/* Called for every /games/mount (mounted 1) and /games/unmount (0) the app sends (smp.c, ps5tm_smp_call). */
void ps5tm_smphold_note(const char *title_id, int mounted);
/* At the start of the app: reads what the last run wrote down, starts the watcher. */
void ps5tm_smphold_start(void);
/* One look at everything written down; how many games it unmounted. The thread calls it every 3 s. */
int  ps5tm_smphold_step(void);
/* One look at the console's refusals to start a title: if another title than a held one was refused, the held games are unmounted
   at once (they were what refused it) and the person is told to start again. How many it unmounted. The thread calls it every second. */
int  ps5tm_smphold_check_launch(void);
/* Before the app starts a title itself: every game it holds except that one is unmounted, so the start cannot be refused. How many. */
int  ps5tm_smphold_release_for_start(const char *title_id);
/* From now on only refusals that come later count (the watcher thread calls it when the app starts to hold a game). */
void ps5tm_smphold_mark(void);
/* How many games are written down. */
int  ps5tm_smphold_count(void);
/* Is this title one the app mounted and nobody started yet? */
int  ps5tm_smphold_is_held(const char *title_id);

/* Host test only. NULL gives the real world back. */
void ps5tm_smphold_test_env(const ps5tm_smphold_env_t *env);
void ps5tm_smphold_test_reset(void);
void ps5tm_smphold_test_load(void);                 /* reads the file of held games (what the start of the app does) without starting the thread */

#endif
