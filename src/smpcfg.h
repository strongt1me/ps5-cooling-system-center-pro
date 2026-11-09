/* The few settings of ShadowMountPlus's config.ini that this app reads and writes (06.11.2026, wish of the user: when
 * AMPR EMU NG is built into a game, the app asks whether to switch off ShadowMountPlus's automatic fakelib update and, on
 * "yes", does it).
 *
 * Two formats exist. ShadowMountPlus up to 1.7beta11 has a flat file with the key  update_emulators=1|0  and
 * emulators_path=<folder>. From 1.7beta12 (08.11.2026) the file is "v2": it starts with [config] version = 2 and has
 * sections, here [emulators] update_enabled and path; ShadowMountPlus converts the old files at its first start (the old
 * ones go to config-v1-backup) and ignores the old flat names in a v2 file, so does this reader. Values are read as its
 * v2 reader does: "quoted" (with \\ and \" as escapes) or up to a # or ; that starts a comment; a section header may have a
 * comment behind the ]. Both formats are read; a write follows the format the file has (a flat file stays flat) and
 * changes only the value: the blanks around it, a comment behind it, everything else in the file (comments, SMB connections
 * with their passwords, the line endings) stays byte for byte as it was. Checked against the reader of 1.7beta12 itself.
 *
 * The version the person says they run (the app's setting smp_version, 09.11.2026): the file decides how a line is written
 * as long as the file says which format it is in; the named version decides only for a file that says nothing (missing,
 * empty, comments only), and it is the reason for a hint when the file and the named version do not fit (a v2 file
 * ShadowMountPlus has not made yet because it has not been started since its update, or the reverse).
 *
 * Plain C without the rest of the app, so that the host test (test/smpcfg/run.sh) compiles it alone. */
#ifndef PS5TM_SMPCFG_H
#define PS5TM_SMPCFG_H

#include <stddef.h>

/* The version of ShadowMountPlus the person named (the setting smp_version: "" or "auto", "old", "v2"). */
#define PS5TM_SMP_AUTO 0       /* not named: the file tells */
#define PS5TM_SMP_OLD  1       /* up to 1.7beta11 (flat file) */
#define PS5TM_SMP_V2   2       /* from 1.7beta12 (sections) */

/* What the text of a config.ini is in. */
#define PS5TM_SMPFMT_NONE 0    /* nothing says (missing, empty, only comments) */
#define PS5TM_SMPFMT_FLAT 1    /* keys without sections: up to 1.7beta11 */
#define PS5TM_SMPFMT_V2   2    /* [config] or [emulators] is there: from 1.7beta12 */

typedef struct {
  int  update_emulators;       /* 1 = ShadowMountPlus replaces game fakelib files from its emulator folder (its default) */
  int  v2;                     /* the file has sections ([config] or [emulators]) */
  char emulators_path[200];    /* "" = not set (the caller's default applies) */
} ps5tm_smpcfg_t;

/* Reads the settings out of the text of a config.ini; defaults (on, flat, no path) for what is not there. */
void ps5tm_smpcfg_parse(const char *text, size_t len, ps5tm_smpcfg_t *c);

/* PS5TM_SMPFMT_* of the text. */
int ps5tm_smpcfg_format(const char *text, size_t len);

/* PS5TM_SMP_* of the setting text ("old", "v2", anything else is auto). */
int ps5tm_smpcfg_mode_of(const char *setting);

/* The name of the line that holds the setting: "update_emulators" or "update_enabled" - by the format of the file, for a
 * file that says nothing by the named version. Never NULL. */
const char *ps5tm_smpcfg_key(int format, int mode);

/* A German sentence when the named version and the format of the file do not fit, else "" (also when no version is named
 * or the file says nothing). Never NULL. */
const char *ps5tm_smpcfg_mismatch(int format, int mode);

/* A copy of the text with update_emulators (update_enabled in v2) set to on or off, malloc'd, NULL when out of memory.
 * With on and no such line there is nothing to change (on is the default): the copy is the same text. A text that says
 * nothing gets the line of the named version (a v2 one with its [config] version = 2). */
char *ps5tm_smpcfg_with_update(const char *text, size_t len, int on, int mode, size_t *out_len);

/* Changes it in the file (made atomically: a temporary file next to it, then rename; a first change keeps the old file
 * as <path>.ps5cc-bak). 0 on success; otherwise an errno value and the reason in why. A missing file is made when its
 * folder exists. */
int ps5tm_smpcfg_set_update(const char *path, int on, int mode, char *why, size_t why_len);

#endif
