/* The few settings of ShadowMountPlus's config.ini that this app reads and writes (06.11.2026, wish of the user: when
 * AMPR EMU NG is built into a game, the app asks whether to switch off ShadowMountPlus's automatic fakelib update and, on
 * "yes", does it).
 *
 * Two formats exist. ShadowMountPlus up to 1.7beta11 has a flat file with the key  update_emulators=1|0  and
 * emulators_path=<folder>. The format "v2" on its main branch has sections: [emulators] update_enabled and path. Both are
 * read; a write follows the format the file has (a flat file stays flat). Everything else in the file (comments, SMB
 * connections with their passwords, the line endings) is left byte for byte as it was.
 *
 * Plain C without the rest of the app, so that the host test (test/smpcfg/run.sh) compiles it alone. */
#ifndef PS5TM_SMPCFG_H
#define PS5TM_SMPCFG_H

#include <stddef.h>

typedef struct {
  int  update_emulators;       /* 1 = ShadowMountPlus replaces game fakelib files from its emulator folder (its default) */
  int  v2;                     /* the file has sections ([config] or [emulators]) */
  char emulators_path[200];    /* "" = not set (the caller's default applies) */
} ps5tm_smpcfg_t;

/* Reads the settings out of the text of a config.ini; defaults (on, flat, no path) for what is not there. */
void ps5tm_smpcfg_parse(const char *text, size_t len, ps5tm_smpcfg_t *c);

/* A copy of the text with update_emulators (update_enabled in v2) set to on or off, malloc'd, NULL when out of memory.
 * With on and no such line there is nothing to change (on is the default): the copy is the same text. */
char *ps5tm_smpcfg_with_update(const char *text, size_t len, int on, size_t *out_len);

/* Changes it in the file (made atomically: a temporary file next to it, then rename; a first change keeps the old file
 * as <path>.ps5cc-bak). 0 on success; otherwise an errno value and the reason in why. A missing file is made when its
 * folder exists. */
int ps5tm_smpcfg_set_update(const char *path, int on, char *why, size_t why_len);

#endif
