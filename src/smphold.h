/* PS5 Cooling & System Center - Pro: a game the app mounted through ShadowMountPlus and nobody started (smphold.c, 14.11.2026). */
#ifndef PS5TM_SMPHOLD_H
#define PS5TM_SMPHOLD_H

#include <stddef.h>
#include <stdint.h>

/* So long after the app mounted it, a game that is not started is unmounted again. */
#define PS5TM_SMPHOLD_GRACE_MS (120u * 1000u)

/* The world the watcher looks at; smphold.c has the real one, a test brings its own. */
typedef struct {
  int      (*find)(const char *id, int *mounted);      /* 0 and *mounted when ShadowMountPlus knows the title */
  int      (*smp_up)(void);                             /* ShadowMountPlus answers */
  int      (*game_running)(void);                       /* a game plays, or it is not known yet */
  int      (*job_running)(void);                        /* one of the app's own jobs works */
  int      (*unmount)(const char *id, char *msg, size_t n); /* an HTTP status, 200 done */
  uint64_t (*now_ms)(void);
  void     (*notice)(const char *id);                   /* NULL: the notification on the console */
} ps5tm_smphold_env_t;

/* Called for every /games/mount (mounted 1) and /games/unmount (0) the app sends (smp.c, ps5tm_smp_call). */
void ps5tm_smphold_note(const char *title_id, int mounted);
/* At the start of the app: reads what the last run wrote down, starts the watcher. */
void ps5tm_smphold_start(void);
/* One look at everything written down; how many games it unmounted. The thread calls it every 10 s. */
int  ps5tm_smphold_step(void);
/* How many games are written down. */
int  ps5tm_smphold_count(void);

/* Host test only. */
void ps5tm_smphold_test_env(const ps5tm_smphold_env_t *env);
void ps5tm_smphold_test_reset(void);

#endif
