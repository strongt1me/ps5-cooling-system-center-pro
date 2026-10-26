/* PS4Mount inside the app (src/ps4m_core.c; GPL-3.0-or-later, gasb07 - see third_party/ps4mount/). */
#ifndef PS5TM_PS4M_H
#define PS5TM_PS4M_H

#include <stddef.h>

/* At the app's start: runs the module when it was switched on (config key enabled=1 in /data/ps4mount/config.txt). */
void  ps4m_boot(void);
/* Switches the scan thread on or off and keeps the choice. 0 = done, -1 = refused (msg says why). While the stand-alone
   PS4Mount (ps4mount.elf) runs, switching on is done too, but the module is not loaded a second time: it waits and takes over
   once the stand-alone one has ended (msg says so). */
int   ps4m_set_enabled(int on, char *msg, size_t n);
/* {"ok":true,"enabled":…,"running":…,["standalone":"<why the module waits>",]"version":…,"games":[…],"config":{…}}, malloc'd. */
char *ps4m_state_json(void);
/* 1 while the module of the app runs. */
int   ps4m_running(void);
/* 1, with the reason in why, when the stand-alone PS4Mount runs: its web panel answers on port 8770, it holds the lock on 9032,
   or a process is named like it (since 15.10.2026; may take up to 0.6 s). */
int   ps4m_standalone_active(char *why, size_t n);
/* "scan"; "add" / "remove" with a CUSA title id; "restore" with one or more title ids (comma-separated): their pkgs go back to
   where they came from (since 0.5.0, done by the worker, which closes the open game first); "restore_all"; "restored_forget":
   pkgs moved back are picked up again. 0 = done or started. */
int   ps4m_do_action(const char *action, const char *tid, char *msg, size_t n);
/* Settings as the form string of the original panel (batch=0|1&batch_size=N&dlc=0|1&restore_batch=N&paths=...). */
int   ps4m_do_config(const char *form, char *msg, size_t n);
/* icon0.png of a game (malloc'd); 0 = found. */
int   ps4m_icon_png(const char *tid, unsigned char **data, size_t *len);
/* End of the log, malloc'd. */
char *ps4m_log_tail(size_t max);
/* 1 when tid is one of PS4Mount's games: /user/app/<tid> is a mount of our own hold folder, never the title's own
   internally installed copy. The normal game-update path (updates.c) must not run against it — see ps4m_core.c. */
int   ps4m_title_managed(const char *tid);

/* The updates and add-ons of one PS4Mount game (0.7.0): {"title_id","name","dlc_limit","updates":[…],"dlcs":[…]},
   malloc'd. NULL with a sentence in msg when the title is not found. */
char *ps4m_game_json(const char *tid, char *msg, size_t cap);
/* Picks which update file (as ps4m_game_json names it under "updates"."file") becomes patch.pkg. 0 = taken over. */
int   ps4m_choose_update(const char *tid, const char *rel, char *msg, size_t cap);
/* Picks which add-ons stay on: a comma-separated list of content ids ("" = all off). 0 = taken over. */
int   ps4m_choose_dlc(const char *tid, const char *on, char *msg, size_t cap);
/* Starts a PS4Mount game for the signed-in user. 1 with the open game's name in msg when another one must be closed
   first (ask, then call again with force); 0 = starting; -1 refused (msg says why). */
int   ps4m_launch(const char *tid, int force, char *msg, size_t cap);

/* Copies a downloaded, verified update package (updates.c) to the root of the drive tid's PS4Mount folder is on and
   starts a rescan: the next pass sorts it in exactly like one dropped there by hand (adopt_dir_pkgs), including the
   version check that keeps an older file from demoting a newer one already in use. The normal AppInstUtil install
   writes nowhere PS4Mount reads from, so this is what updates.c calls instead for a PS4Mount game. 0 = placed,
   -1 with a sentence in msg. */
int   ps4m_place_update(const char *tid, const char *pkg_path, char *msg, size_t cap);

#endif
