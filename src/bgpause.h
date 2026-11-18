/* PS5 Cooling & System Center - Pro: the game that waits in the background is paused before a tile of the app is tapped (bgpause.c, 17.11.2026). */
#ifndef PS5TM_BGPAUSE_H
#define PS5TM_BGPAUSE_H

#include <stdint.h>

/* The probe thread asks twice a second while a game is resident. The first look that finds the home screen itself in front (not an
   overlay: gamestate.c reads the scene) is enough: the first measurement on the Pro (17.11.2026) had the pause arrive 1.5 to 2 s after
   the PS button with two looks, and the quickest tap of the person came 2 s after it. */
#define PS5TM_BGPAUSE_LOOKS  1u       /* looks in a row that find it */

/* What the decision looks at; the runtime fills it from the game state, the test makes it up. */
typedef struct {
  int      enabled;       /* the setting bg_pause */
  int      measured;      /* a fresh reading of the game state exists */
  char     title_id[16];  /* the resident game, "" when none */
  unsigned app_id;        /* its application id (0 = not known) */
  int      foreground;    /* the game owns the controller */
  int      home_scene;    /* the shell's last scene change went to the home screen */
  int      smp_up;        /* ShadowMountPlus answers (only then do the app's tiles start through it) */
  int      is_game;       /* the title is a game the games page lists, not an app such as YouTube (17.11.2026: apps are never touched) */
} ps5tm_bgpause_in_t;

typedef struct {
  uint64_t since_ms;      /* when the first of the looks in a row was taken */
  unsigned seen;          /* how many looks in a row */
  unsigned done_app;      /* the application id paused in this stay in the background (0 = none yet) */
} ps5tm_bgpause_state_t;

/* 1: pause that game now (once per stay in the background: the id is written down until the game is in front again or another one
   is resident). The state starts zeroed. */
int ps5tm_bgpause_decide(ps5tm_bgpause_state_t *st, const ps5tm_bgpause_in_t *in, uint64_t now_ms);

/* Host test only. */
void ps5tm_bgpause_test_reset(void);

#endif
