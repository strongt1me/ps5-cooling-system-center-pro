/* PS4Mount inside the app (src/ps4m_core.c; GPL-3.0-or-later, gasb07 - see third_party/ps4mount/). */
#ifndef PS5TM_PS4M_H
#define PS5TM_PS4M_H

#include <stddef.h>

/* At the app's start: runs the module when it was switched on (config key enabled=1 in /data/ps4mount/config.txt). */
void  ps4m_boot(void);
/* Switches the scan thread on or off and keeps the choice. 0 = done, -1 = refused (msg says why: another PS4Mount runs). */
int   ps4m_set_enabled(int on, char *msg, size_t n);
/* {"ok":true,"enabled":…,"running":…,"version":…,"games":[…],"config":{…}}, malloc'd. */
char *ps4m_state_json(void);
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

#endif
