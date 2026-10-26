/* SPDX-License-Identifier: GPL-3.0-or-later
 * Lowering the SDK version of the files in a dump (25.10.2026). See dump_backport.c. */

#ifndef PS5TM_DUMP_BACKPORT_H
#define PS5TM_DUMP_BACKPORT_H

#include <stdint.h>

#define DJ_BP_PS4_LEVELS 6
#define DJ_BP_PS5_LEVELS 10

typedef struct {
  unsigned total;          /* files that can hold the SDK fields (by extension) */
  unsigned patched;        /* SDK field lowered */
  unsigned unchanged;      /* looked at: already at or below the target, or no parameter block */
  unsigned signed_skip;    /* FSELF: left as they are, as the dumper does */
  unsigned failed;         /* could not be read or written */
  int      param_patched;  /* param.sfo (PS4) or param.json (PS5) rewritten */
  char     why[220];       /* the last hint */
} dj_bp_res_t;

/* rel: the file under way (NULL: unchanged); done of total counts the files looked at; line: a line for the dump's log (or NULL) */
typedef void (*dj_bp_cb_t)(void *ctx, const char *rel, unsigned done, unsigned total, const char *line);

/* The PS4 SDK value of a level 1..6 (0 when out of range). */
uint32_t ps5tm_dump_bp_ps4_target(int level);

/* The PS5 and PS4 SDK values of a level 1..10; 0 and 0 when out of range. */
void ps5tm_dump_bp_ps5_target(int level, uint32_t *ps5_sdk, uint32_t *ps4_sdk);

/* Lowers the SDK version of the plain executables below dst_root (a folder of the dump, never the console's own copy of a game)
   and of its parameter file (sce_sys/param.sfo for a PS4 title, sce_sys/param.json for a PS5 title). The folder "decrypted" is
   left alone. level: 1..6 for a PS4 title, 1..10 for a PS5 title (clamped). Files are patched in place, four bytes at a time and
   read back; the parameter files are rewritten through a temporary name. Returns 0 done, 1 cancelled, -1 refused (res->why). */
int ps5tm_dump_backport_run(const char *dst_root, int is_ps4, int level, volatile int *cancel, dj_bp_cb_t cb, void *ctx,
                            dj_bp_res_t *res);

#endif
