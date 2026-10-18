#ifndef PS5TM_TITLEID_H
#define PS5TM_TITLEID_H

#include <string.h>

/* The starter tile (starter/, PPSA86921) is a title of its own for the few seconds it runs: it hands the app over to the
 * loader and ends. Taken for a game, it showed up under "Läuft gerade" at every start from the tile, and its seconds at the
 * idle fan speed would have been learned as the console's minimum speed in a game. It is not a game. */
#define PS5TM_STARTER_TITLE_ID "PPSA86921"

/* A real game's title id: PPSA or CUSA followed by five digits. Restricted on purpose - "four capitals plus five digits" also
 * matches Sony's own applications (NPXS40000 and friends), which would turn the home screen into a "running game".
 * ShadowMountPlus applies the same rule. */
static inline int
ps5tm_is_game_title_id(const char *s) {
  if(!s || strlen(s) != 9) return 0;
  if(strncmp(s, "PPSA", 4) && strncmp(s, "CUSA", 4)) return 0;
  for(int i = 4; i < 9; i++)
    if(s[i] < '0' || s[i] > '9') return 0;
  return 1;
}

/* A title the "running game" logic may report: a game id, but not this app's own starter. */
static inline int
ps5tm_counts_as_game(const char *s) {
  return ps5tm_is_game_title_id(s) && strcmp(s, PS5TM_STARTER_TITLE_ID) != 0;
}

#endif
