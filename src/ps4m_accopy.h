/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 gasb07 (PS4Mount 0.7.0 accopy.c/.h), adapted for PS5 Cooling & System Center - Pro: functions
 * carry the p4m_ prefix, like the rest of src/ps4m_*.c (see third_party/ps4mount/ORIGIN.md); otherwise unchanged.
 *
 * Add-ons copied to internal storage by the system, in copy mode: the state files kept for them, the free space and
 * copy progress rules and the decision taken for each add-on. */
#ifndef PS5TM_PS4M_ACCOPY_H
#define PS5TM_PS4M_ACCOPY_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>

/* <dir>/<content id>.accopy: the add-on was installed by PS4Mount (holds the pkg path).
 * Only add-ons with this file are ever uninstalled by PS4Mount. */
int  p4m_accopy_mark(const char *dir, const char *cid, const char *pkg_path);
int  p4m_accopy_marked(const char *dir, const char *cid);
void p4m_accopy_unmark(const char *dir, const char *cid);

/* <dir>/<content id>.acfail: one line per failed install, the reason. */
#define P4M_ACFAIL_MAX 3
int  p4m_acfail_add(const char *dir, const char *cid, const char *reason); /* new count, -1 on error */
int  p4m_acfail_get(const char *dir, const char *cid, char *reason, size_t cap); /* count, last reason */
void p4m_acfail_clear(const char *dir, const char *cid);

/* Free space on internal storage must exceed the pkg size plus 1 GiB. */
int  p4m_accopy_space_ok(uint64_t free_bytes, uint64_t pkg_size);

/* Copy progress, checked every few seconds against the size of the copied pkg. */
#define P4M_ACW_TIMEOUT_S 1800
#define P4M_ACW_STALL_S   120
enum { P4M_ACW_WAIT, P4M_ACW_DONE, P4M_ACW_TIMEOUT, P4M_ACW_STALL };
typedef struct {
  uint64_t last;
  time_t   start, grew;
} p4m_acwait_t;
void p4m_acwait_start(p4m_acwait_t *w, time_t now);
int  p4m_acwait_step(p4m_acwait_t *w, uint64_t cur, uint64_t want, time_t now);

/* What is known of one add-on. files: /user/addcont/<TID>/<LABEL>/ac.pkg exists. store:
 * files there that PS4Mount did not install. playgo: /user/playgo/addcont/<CID> exists.
 * busy: being installed now. rereg: the game was registered again after the copy (the
 * system may have removed it then). */
typedef struct {
  int on, marked, files, store, playgo, fails, busy, rereg;
} p4m_acin_t;
enum { P4M_AC_NONE, P4M_AC_INSTALL, P4M_AC_UNINSTALL, P4M_AC_TURN_OFF, P4M_AC_REINSTALL, P4M_AC_PROBLEM, P4M_AC_STORE };
int  p4m_accopy_decide(const p4m_acin_t *in);

#endif
