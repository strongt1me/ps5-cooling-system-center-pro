/* SPDX-License-Identifier: GPL-3.0-or-later
 * Unpacking the PFS image of a PS4 game for the dump (24.10.2026). See dump_ps4.c. */

#ifndef PS5TM_DUMP_PS4_H
#define PS5TM_DUMP_PS4_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
  uint64_t bytes, largest;
  unsigned files, dirs, max_rel;
} dj_pfs_stat_t;

/* delta: bytes written since the last call; rel: the file under way (NULL: unchanged) */
typedef void (*dj_pfs_progress_cb)(void *ctx, uint64_t delta, const char *rel);

/* A file is whole: dst is where it lies, rel its name below the image's root, crc its CRC-32 (when want_crc was set).
   Returns 0, or an errno-like code that stops the extraction (written to *rc_out of the result as the reason). */
typedef int (*dj_pfs_file_cb)(void *ctx, const char *dst, const char *rel, uint64_t size, uint32_t crc);

/* Walks the image without writing: the sizes of what extract would write. 0 done, 1 cancelled, -1 failed (why says why). */
int ps5tm_dump_pfs_measure(const char *img, volatile int *cancel, dj_pfs_stat_t *st, char *why, size_t wn);

/* Writes the files of the image below out_dir (existing files are replaced, links are never followed).
   defer_suffix (or NULL): sce_sys/param.json and sce_sys/param.sfo go to "<name><suffix>" instead; the caller renames them.
   Returns 0 done, 1 cancelled, -1 failed (why says why; what was written stays), -2 file_cb refused (*cb_rc has its code). */
int ps5tm_dump_pfs_extract(const char *img, const char *out_dir, const char *defer_suffix, volatile int *cancel,
                           dj_pfs_progress_cb pcb, dj_pfs_file_cb fcb, void *ctx, int want_crc, int *cb_rc, char *why,
                           size_t wn);

/* The NPWR trophy id that npbind.dat names. 0 found (out filled), -1 none. */
int ps5tm_dump_npwr(const char *npbind, char *out, size_t n);

#endif
