#ifndef PS5TM_TITLEID_H
#define PS5TM_TITLEID_H

#include <string.h>

/* The app's two tiles (tools/make_starter.sh): the full-screen app (tvapp/) and the browser tile (starter/browser/). Each is a
 * title of its own while it runs. Up to 1.74.0 they were PPSA86921/PPSA86922, and ShadowMountPlus took a PPSA title in front for
 * a running game: it refused every unmount, delete and move while the tile was open ("game_active", console test 28.10.2026,
 * sm_game_lifecycle.c treats only PPSA/CUSA as ordinary games). FAKE ids are accepted by every ShadowMountPlus version (1.7beta2
 * allowed PPSA, CUSA and FAKE) and never block it. The old ids are still recognised in case an old tile is left installed. */
#define PS5TM_TV_TITLE_ID      "FAKE86921"
#define PS5TM_BROWSER_TITLE_ID "FAKE86922"

static inline int
ps5tm_is_own_tile(const char *s) {
  return s && (!strcmp(s, PS5TM_TV_TITLE_ID) || !strcmp(s, PS5TM_BROWSER_TITLE_ID) ||
               !strcmp(s, "PPSA86921") || !strcmp(s, "PPSA86922"));
}

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

/* A title the "running game" logic may report: a game id, but not one of this app's own tiles. A tile is no game: taken for
 * one, it showed up under "Läuft gerade" at every start from the tile, and its seconds at the idle fan speed would have been
 * learned as the console's minimum speed in a game. */
static inline int
ps5tm_counts_as_game(const char *s) {
  return ps5tm_is_game_title_id(s) && !ps5tm_is_own_tile(s);
}

#endif
