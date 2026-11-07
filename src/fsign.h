/* SPDX-License-Identifier: GPL-3.0-or-later
 * Unsigned PS5 executables of a game, written as fake-signed SELFs (07.11.2026). See fsign.c.
 *
 * The console's loader refuses a plain ELF, so a game whose executables are plain ELF files never starts. Converting a game
 * into an image and packing it into a new folder write such files as fake-signed SELFs (what ps5-dump-forge 0.5.4 does);
 * the original folder is only read. */

#ifndef PS5TM_FSIGN_H
#define PS5TM_FSIGN_H

#include <stddef.h>
#include <stdint.h>

#define FSIGN_MAX_ITEMS   4096u                    /* as many executables are looked at (a game has a few dozen) */
#define FSIGN_STAGE_SUFFIX ".ps5cc-sign-teil"      /* a folder with this ending is ours, and the only kind fsign_rmtree removes */

typedef struct {
  char    *rel;                                    /* below the game folder, '/' between the names */
  uint64_t elf_size;
  uint64_t self_size;                              /* as the fake-signed SELF will be */
} fsign_item_t;

typedef struct {
  fsign_item_t *it;                                /* sorted by rel */
  unsigned      n, cap;
  uint64_t      elf_bytes, self_bytes;
  unsigned      refused;                           /* unsigned executables that cannot be signed (odd headers): left as they are */
  char          refused_rel[160];                  /* the first of them ... */
  char          refused_why[120];                  /* ... and why */
  int           capped;                            /* more than FSIGN_MAX_ITEMS executables: the rest was not looked at */
} fsign_list_t;

/* Bytes done in a step, for a progress bar. */
typedef void (*fsign_prog_fn)(void *ctx, uint64_t bytes);

/* An executable by its name (the extensions .bin (only eboot*), .elf, .self, .prx, .sprx), 1 or 0. */
int ps5tm_fsign_name_ok(const char *rel);

/* 1 when the first bytes (at least 0x12) are a PS5 ELF that is not signed (ELF magic, e_type 0xFE10 or 0xFE18). */
int ps5tm_fsign_head_unsigned(const uint8_t *head, size_t n);

/* Walks root (links are not followed) and lists the unsigned PS5 executables that can be signed, sorted. Unreadable folders and
   files are passed over. 0, or 1 when cancelled. */
int ps5tm_fsign_scan(const char *root, const int *cancel, fsign_list_t *out);
void ps5tm_fsign_free(fsign_list_t *l);

/* The item for rel (exactly as the scan wrote it), or NULL. */
const fsign_item_t *ps5tm_fsign_find(const fsign_list_t *l, const char *rel);

/* Writes the fake-signed SELF of the ELF at src to dst (made new, mode 0777), then reads dst back against src. prog gets the bytes
   of each of the two passes. 0, 1 when cancelled (dst removed), -1 with the reason in err (dst removed). *out_size: the size. */
int ps5tm_fsign_write(const char *src, const char *dst, const int *cancel, fsign_prog_fn prog, void *ctx, uint64_t *out_size,
                      char *err, size_t en);

/* The AMPR index at index_path with the records of the listed files set to their SELF sizes (only where the record holds the ELF
   size), written to out_path (through a temporary name; out_path may be index_path). *resized: how many records changed; with
   none, nothing is written. 0, or -1 (err); an index that is not one is no error: *resized stays 0. */
int ps5tm_fsign_fix_index(const char *index_path, const char *out_path, const fsign_list_t *l, unsigned *resized, char *err, size_t en);

/* Writes the signed files of l (from root) and, if root has an AMPR index that lists them, its corrected copy below stage, at the
   same relative paths: a folder to lay over root as a second source. 0, 1 cancelled, -1 failed (err). */
int ps5tm_fsign_stage(const char *root, const char *stage, const fsign_list_t *l, const int *cancel, fsign_prog_fn prog, void *ctx,
                      unsigned *index_resized, char *err, size_t en);

/* Replaces the listed executables of root by their fake-signed SELFs, in place (zstd packs, which always work in the game's folder):
   each one is written beside the original under a temporary name (original + FSIGN_TMP_SUFFIX), read back against it, and only then
   renamed over it; the mode and the times of the original are kept. Done in the order of l, and stopped at the first that fails: the
   ones done stay signed, the others stay as they were. Then the AMPR index is corrected for the files done. *done: how many. 0, 1 when
   cancelled, -1 failed (err). */
#define FSIGN_TMP_SUFFIX ".ps5cc-sign-tmp"
int ps5tm_fsign_replace(const char *root, const fsign_list_t *l, const int *cancel, fsign_prog_fn prog, void *ctx, unsigned *done,
                        unsigned *index_resized, char *err, size_t en);

/* Removes a folder whose name ends in FSIGN_STAGE_SUFFIX and everything in it (links are removed, never followed). */
void ps5tm_fsign_rmtree(const char *stage);

#endif
