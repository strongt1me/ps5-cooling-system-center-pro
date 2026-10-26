/* SPDX-License-Identifier: GPL-3.0-or-later
 * The sce_sys files that lie inside an installed app.pkg or ac.pkg (23.10.2026). See dump_unpkg.c. */

#ifndef PS5TM_DUMP_UNPKG_H
#define PS5TM_DUMP_UNPKG_H

#include <stdint.h>

/* current: the file under way (NULL: unchanged); line: a line for the dump's log (NULL: none) */
typedef void (*dj_unpkg_cb_t)(void *ctx, const char *current, const char *line);

typedef struct {
  unsigned extracted;     /* files written */
  unsigned existing;      /* files that were there already and stay */
  unsigned skipped;       /* entries left out: no usable name, outside the file, too big */
  uint64_t bytes;
  int      deferred[2];   /* param.json and param.sfo were written under defer_suffix */
  char     why[200];      /* the reason, when the return value is negative */
} dj_unpkg_res_t;

/* Writes the files of the package's inner container into sce_dir, none that exists already.
   defer_suffix (or NULL): param.json and param.sfo go to "<name><suffix>" instead; the caller renames them.
   Returns 0 done, 1 cancelled, -1 failed (res->why says why; what was written stays). */
int ps5tm_dump_unpkg(const char *pkg, const char *sce_dir, const char *defer_suffix, volatile int *cancel,
                     dj_unpkg_cb_t cb, void *ctx, dj_unpkg_res_t *res);

/* The same for the package of a PS4 game (app.pkg / patch.pkg: the container starts at the top of the file, names may carry a
   leading "sce_sys/", files over 100 MB are left out), as unpkg_ps4() of the dumper does. */
int ps5tm_dump_unpkg_ps4(const char *pkg, const char *sce_dir, const char *defer_suffix, volatile int *cancel,
                         dj_unpkg_cb_t cb, void *ctx, dj_unpkg_res_t *res);

#endif
