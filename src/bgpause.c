/* PS5 Cooling & System Center - Pro: the game that waits in the background is paused before a tile of the app is tapped (17.11.2026).
 *
 * What happens when the person goes to the home screen from a game and taps a tile of the app (browser or full screen), measured on
 * the console with a PS4 game from PS4Mount and an FPKG game from Kraken Mount, FW 12.00:
 *   1. launchApp(<tile>) fails in the shell: checkExistingApp 0x80940011, because the game, a big application, still RUNS (it is only
 *      in the background, not paused). ShadowMountPlus has just mounted the tile ("launch mount ready").
 *   2. The shell pauses the game itself (suspendApp, 65 ms) and starts the tile again. In between ShadowMountPlus took the failed
 *      start for a lost one and released the tile's mount ("runtime release start"); its search is busy for a moment, the second
 *      "launch mount" is refused with EBUSY ("Device busy", status=16), the start fails a second time and the console shows its
 *      launch error dialog. Whether that happens is a matter of milliseconds: it did on 11 of 14 attempts.
 * ShadowMountPlus avoids the whole thing for games it manages itself (its "game switch" path), not for any other game.
 *
 * With the game paused beforehand, step 1 passes: the log of the console showed no checkExistingApp, no release, no EBUSY, and the
 * tile came up at once (measured 17.11.2026 on the Pro: PS button, the game paused 1 s later, tile tapped 2.5 s after the button).
 * So the app does what the shell does 65 ms after the tap - only a moment earlier, when the home screen comes up. The game is the
 * one the shell would pause anyway the moment anything is started; it is resumed the usual way (from its tile, or through the app).
 *
 * Rules (all of them keep the game safe, none is a guess):
 *  - only while the home screen itself is in front (the shell's last scene change went to Render.NPXS40002), never behind an overlay
 *    such as the control centre;
 *  - only a real game (PPSA, CUSA), never one of the app's own tiles or another homebrew (which may keep a service running), and only
 *    a title the games page lists as a game: a media app (YouTube and the like) has a PPSA id too and is never touched (audit 17.11.2026);
 *  - only while ShadowMountPlus answers (without it the tiles do not start through it, nothing to avoid);
 *  - once per stay in the background: when the game is in front again, or another one is resident, the next one counts;
 *  - the setting "Spiel im Hintergrund pausieren" (bg_pause, default on) can switch it off.
 * The call into the console's LNC service is made in a helper process of its own (src/helper/bgpause_helper.c): an LNC call from
 * inside the app once killed it within seconds whenever a game ran (gamestate.c). The helper pauses only the very application id the
 * app names, and only when that is the big application the service reports as running. */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "bgpause.h"
#include "ps5tm.h"
#include "titleid.h"

#ifndef PS5TM_DATA_DIR
#define PS5TM_DATA_DIR "/data/PS5-Cooling-Center"
#endif
#define BGP_REQ PS5TM_DATA_DIR "/bgpause.req"
#define BGP_RES PS5TM_DATA_DIR "/bgpause.res"

#define BGP_ANSWER_WAIT_MS 10000u           /* the helper writes its answer within a second; after this it is called lost (it ends itself after 5 s, counted from its own start) */
#define BGP_GAME_AGE_MS    2500u            /* a reading of the game state older than this says nothing about the home screen */

extern const unsigned char ps5tm_bgpause_helper[];
extern const unsigned long ps5tm_bgpause_helper_len;       /* the zlib stream of the helper (tools/gen_blob.py) */
extern const unsigned long ps5tm_bgpause_helper_raw_len;


int
ps5tm_bgpause_decide(ps5tm_bgpause_state_t *st, const ps5tm_bgpause_in_t *in, uint64_t now_ms) {
  const int game = in->measured && in->title_id[0] && ps5tm_counts_as_game(in->title_id) && in->app_id != 0;

  /* The stay in the background is over when the game is in front again or none is resident any more: the next one counts again. */
  if(!in->measured || !in->title_id[0] || in->foreground) st->done_app = 0;
  if(game && st->done_app != 0 && st->done_app != in->app_id) st->done_app = 0;      /* another game */

  const int candidate = in->enabled && game && !in->foreground && in->home_scene && in->smp_up && in->is_game;
  if(!candidate) { st->seen = 0; st->since_ms = 0; return 0; }
  if(st->done_app == in->app_id) return 0;                                           /* paused already in this stay */

  if(st->seen == 0) st->since_ms = now_ms;
  st->seen++;
  if(st->seen >= PS5TM_BGPAUSE_LOOKS) {
    st->done_app = in->app_id;
    st->seen = 0;
    st->since_ms = 0;
    return 1;
  }
  return 0;
}


static ps5tm_bgpause_state_t g_state;
static int g_first_look_done;                /* the first look after the start of the app has been taken */
static uint64_t g_started_ms;                /* when the last helper was started, 0 = none waiting for its answer */
static char     g_started_title[16];
static unsigned g_started_app;
static unsigned g_again;                     /* "back in front" answers in this stay: the pause is tried again after the first three only (a log line of the shell that the helper and the app read differently must not make it a loop) */
static unsigned g_lost_in_row;               /* helpers in a row that never answered (the console's service hangs): after BGP_LOST_MAX none is started */
static char     g_ig_title[16];              /* the last title asked about, and the answer of the games page: 1 game, 0 app, -1 not known */
static int      g_ig_val = -1;
static uint64_t g_ig_ms;                     /* when the answer was kept: a "no" is kept for a minute only (a game just mounted is not in the list yet) */
static uint64_t g_ig_try_ms;                 /* the last time the list was asked for while the game was in front */
static char     g_ig_asked[16];              /* the title the list was read afresh for: once per title (an app in front for hours must not make the list be read again and again) */
#define BGP_NO_KEEP_MS 60000u
#define BGP_LOST_MAX 2u

void
ps5tm_bgpause_test_reset(void) {
  memset(&g_state, 0, sizeof(g_state));
  g_started_ms = 0;
  g_started_title[0] = 0;
  g_started_app = 0;
  g_first_look_done = 0;
  g_lost_in_row = 0;
  g_again = 0;
  g_ig_title[0] = 0;
  g_ig_val = -1;
  g_ig_ms = 0;
  g_ig_try_ms = 0;
  g_ig_asked[0] = 0;
}


/* Is the title a game (1) or an app (0)? The games page's list answers. A "yes" is kept for the title; a "no" for a minute (a game that was just
   mounted or installed is not in the list yet). may_wait: the question may wait for the list to be read (only while the game is in front - the
   answer is wanted at the home screen, where nothing may wait); otherwise the list as it is. -1 while it is not known (nothing is paused then). */
static int
title_cached(const char *title_id, uint64_t now) {
  if(!title_id || !title_id[0] || strcmp(g_ig_title, title_id) || g_ig_val < 0) return -1;
  if(g_ig_val == 1 || now - g_ig_ms < BGP_NO_KEEP_MS) return g_ig_val;
  return -1;
}

/* A "no" is kept only when the list was read for the question (may_wait): the list as it is can be old, and a game that was mounted after its last read is
   in it only later - a "no" from it kept for a minute and asked again from the same old list would keep that game "an app" for good (audit 17.11.2026). */
static int
title_is_game(const char *title_id, int may_wait, uint64_t now) {
  if(!title_id || !title_id[0]) return -1;
  int c = title_cached(title_id, now);
  if(c == 1) return 1;
  /* A kept "no" is overruled by a "yes" from the list as it is (a page has read it again since: the game is in it now). */
  if(c == 0) {
    if(ps5tm_library_is_game_known(title_id) != 1) return 0;
    snprintf(g_ig_title, sizeof(g_ig_title), "%s", title_id); g_ig_val = 1; g_ig_ms = now;
    return 1;
  }
  int v = may_wait ? ps5tm_library_is_game(title_id) : ps5tm_library_is_game_known(title_id);
  if(v == 1 || (v == 0 && may_wait)) { snprintf(g_ig_title, sizeof(g_ig_title), "%s", title_id); g_ig_val = v; g_ig_ms = now; }
  return v;
}


/* The probe thread's loop looks at the game twice a second only while this says yes: a game is resident (and counts) and the setting
   is on. Otherwise the game state stays at one reading in four seconds, as before. */
int
ps5tm_bgpause_wants_fast(void) {
  ps5tm_config_lock();
  unsigned on = g_config.bg_pause;
  ps5tm_config_unlock();
  if(!on || !ps5tm_gamestate_measured()) return 0;
  ps5tm_gamestate_t gs;
  ps5tm_gamestate_get(&gs);
  if(!gs.title_id[0] || !ps5tm_counts_as_game(gs.title_id)) return 0;
  if(title_cached(gs.title_id, ps5tm_mono_ms()) == 0) return 0;          /* an app (the list was read for the question): never touched, no quick reading for it */
  /* The last answer only (audit 17.11.2026, F-180): asking would send ShadowMountPlus two requests every ten seconds for as long as a
     game runs, and the probe thread waits for the answer. The real question comes in bgpause_tick, only at the home screen. */
  return ps5tm_smp_last_known_up();             /* without ShadowMountPlus nothing is to be avoided: no quick reading */
}


/* The helper's answer, one line: "<what> rc=<n> big=0x<id> want=0x<id>". Read once, then the file is gone. */
static void
collect_answer(uint64_t now) {
  if(g_started_ms == 0) return;
  char line[160] = "";
  int fd = open(BGP_RES, O_RDONLY);
  if(fd >= 0) {
    ssize_t n = read(fd, line, sizeof(line) - 1);
    close(fd);
    if(n < 0) n = 0;
    line[n] = 0;
    /* Gone only after a read that got something: the helper creates the file and writes in two steps, and an empty read between the two
       must not take the answer's place away (audit 17.11.2026). */
    if(n > 0) (void)unlink(BGP_RES);
  }
  if(line[0]) {
    size_t n = strlen(line);
    while(n && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = 0;
    if(!strncmp(line, "schon-pausiert ", 15))
      PS5TM_INFO("bg_pause_skip", "Das Spiel %s war schon pausiert (Anwendung 0x%x): Es war nichts zu tun. Antwort: %s", g_started_title, g_started_app, line);
    else if(!strncmp(line, "wieder-vorne ", 13) || !strncmp(line, "anderes-spiel ", 14)) {
      PS5TM_INFO("bg_pause_skip", "Das Spiel %s wurde nicht pausiert (Anwendung 0x%x): Es war schon wieder vorn oder ein anderes Spiel lief. Das ist so gewollt. Antwort: %s",
                 g_started_title, g_started_app, line);
      /* The home screen was not in front any more at the last moment (a menu over it, or the person went straight back): the stay in the background is not
         over - the next look at the home screen decides again. */
      if(!strncmp(line, "wieder-vorne ", 13) && ++g_again <= 3) g_state.done_app = 0;
    }
    else if(!strncmp(line, "pausiert ", 9))
      PS5TM_INFO("bg_pause", "Das Spiel %s wurde im Hintergrund pausiert (Anwendung 0x%x), damit die Kachel der App ohne Fehlstart startet. Antwort: %s",
                 g_started_title, g_started_app, line);
    else
      PS5TM_WARN("bg_pause_failed", "Das Spiel %s wurde nicht pausiert (Anwendung 0x%x). Antwort des Hilfsprogramms: %s",
                 g_started_title, g_started_app, line);
    g_started_ms = 0;
    g_lost_in_row = 0;
  } else if(now - g_started_ms > BGP_ANSWER_WAIT_MS) {
    PS5TM_WARN("bg_pause_failed", "Das Spiel %s wurde nicht pausiert (Anwendung 0x%x): Das Hilfsprogramm hat nicht geantwortet.",
               g_started_title, g_started_app);
    g_started_ms = 0;
    /* A service that hangs hangs the helper too (it ends itself after seconds, but it never answered): after two in a row nothing more is
       started in this run of the app, and the person is told. */
    if(++g_lost_in_row == BGP_LOST_MAX)
      PS5TM_WARN("bg_pause_off", "Das Hilfsprogramm hat zweimal nicht geantwortet: Das Pausieren im Hintergrund ruht, bis die App neu startet.");
  }
}


/* Writes the request (the application id, one number), unpacks the helper and hands it to the local loader. 0 when it was sent. */
static int
start_helper(unsigned app_id) {
#ifdef PS5TM_HOST_TEST
  (void)app_id;
  return -1;
#else
  char req[40];
  int n = snprintf(req, sizeof(req), "0x%x\n", app_id);
  int fd = open(BGP_REQ, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if(fd < 0) {
    PS5TM_WARN("bg_pause_failed", "Das Spiel konnte nicht pausiert werden: Die Anfrage an das Hilfsprogramm ließ sich nicht schreiben (%s).", strerror(errno));
    return -1;
  }
  ssize_t w = write(fd, req, (size_t)n);
  close(fd);
  if(w != n) {
    PS5TM_WARN("bg_pause_failed", "Das Spiel konnte nicht pausiert werden: Die Anfrage an das Hilfsprogramm wurde nicht ganz geschrieben (%s).", strerror(errno));
    (void)unlink(BGP_REQ);
    return -1;
  }
  (void)unlink(BGP_RES);
  size_t len = 0;
  unsigned char *elf = ps5tm_blob_inflate(ps5tm_bgpause_helper, ps5tm_bgpause_helper_len, ps5tm_bgpause_helper_raw_len, &len);
  if(!elf) {
    PS5TM_WARN("bg_pause_failed", "Das Spiel konnte nicht pausiert werden: Das Hilfsprogramm ließ sich nicht entpacken.");
    (void)unlink(BGP_REQ);
    return -1;
  }
  const char *why = NULL;
  int rc = ps5tm_loader_inject_mem(elf, len, &why);
  free(elf);
  if(rc != 0) {
    (void)unlink(BGP_REQ);
    PS5TM_WARN("bg_pause_failed", "Das Spiel konnte nicht pausiert werden: %s", why ? why : "Das Hilfsprogramm ließ sich nicht starten.");
    return -1;
  }
  return 0;
#endif
}


void
ps5tm_bgpause_tick(void) {
  const uint64_t now = ps5tm_mono_ms();
  collect_answer(now);

  ps5tm_config_lock();
  unsigned on = g_config.bg_pause;
  ps5tm_config_unlock();

  ps5tm_bgpause_in_t in;
  memset(&in, 0, sizeof(in));
  in.enabled = on != 0;
  in.measured = ps5tm_gamestate_measured() && ps5tm_gamestate_age_ms() <= BGP_GAME_AGE_MS;
  ps5tm_gamestate_t gs;
  ps5tm_gamestate_get(&gs);
  if(in.measured) {
    snprintf(in.title_id, sizeof(in.title_id), "%s", gs.title_id);
    in.app_id = gs.game_app_id;
    in.foreground = gs.foreground;
    in.home_scene = gs.home_scene;
  }
  /* Only asked when the rest already says "pause" (the answer comes from a service that may be slow). */
  in.smp_up = (in.enabled && in.title_id[0] && !in.foreground && in.home_scene) ? ps5tm_smp_available(NULL, 0) : 0;
  /* The answer of the games page for the title of the game that is in front (a minute apart at the most, and only while the pause is on and the title could
     be a game at all: an app in front for hours does not make the list be read again and again): it is wanted at the home screen, where the
     list is only read as it is - a wait for its read would hold the probe thread right when a tile is tapped. */
  if(in.measured && in.foreground) g_again = 0;                         /* a new stay in the background starts when the game has been in front */
  if(in.enabled && in.measured && in.title_id[0] && in.foreground && ps5tm_counts_as_game(in.title_id) && now - g_ig_try_ms >= 60000u &&
     strcmp(g_ig_asked, in.title_id) != 0) {
    g_ig_try_ms = now;
    /* The list is read afresh for this question, once per title (a game that was mounted after the last read would be a "no" from an old list; a "yes" or a
       "no" from a fresh read stands - a later "yes" of the list as it is overrules a "no"). */
    ps5tm_library_forget();
    if(title_is_game(in.title_id, 1, now) >= 0) snprintf(g_ig_asked, sizeof(g_ig_asked), "%s", in.title_id);
  }

  /* The game's application id as the kernel names it for the game's process (procmgr.c, sceKernelGetAppInfo): the id the service calls
     use. The helper checks it against the service's own answer before it pauses anything. */
  unsigned app_id = in.app_id;

  /* The first look after the start of the app (a new ELF, a restart): a game that waits at the home screen then went there before the app
     knew; it is long paused by the console or soon will be. Nothing is asked about it - the next stay in the background counts. */
  if(!g_first_look_done && in.measured) {
    g_first_look_done = 1;
    if(in.title_id[0] && !in.foreground && in.home_scene && app_id) { g_state.done_app = app_id; return; }
  }
  if(g_lost_in_row >= BGP_LOST_MAX) return;                                          /* the service hangs: no more helpers in this run */
  /* A game, not an app such as YouTube (17.11.2026): looked at only when everything else says "pause" (after the first look above). */
  in.is_game = in.smp_up ? title_is_game(in.title_id, 0, now) == 1 : 0;
  if(!ps5tm_bgpause_decide(&g_state, &in, now)) return;

  if(g_started_ms != 0 && now - g_started_ms < BGP_ANSWER_WAIT_MS) {                 /* one helper at a time: the next look tries again */
    g_state.done_app = 0;
    return;
  }
  snprintf(g_started_title, sizeof(g_started_title), "%s", in.title_id);
  g_started_app = app_id;
  if(start_helper(app_id) == 0) g_started_ms = now;
}
