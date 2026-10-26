/* SPDX-License-Identifier: GPL-3.0-or-later
 * Decrypting the SELF/SPRX files of a dump, and re-signing them as FSELF (25.10.2026). See dump_self.c and dump_fself.c. */

#ifndef PS5TM_DUMP_SELF_H
#define PS5TM_DUMP_SELF_H

#include <stddef.h>
#include <stdint.h>

#define DJ_SELF_UNSUPPORTED (-2)

typedef struct {
  unsigned total;         /* files with an extension that can hold a SELF */
  unsigned decrypted;     /* written in plain form */
  unsigned not_self;      /* not a SELF: the copy stays as it is */
  unsigned failed;        /* could not be decrypted: the copy stays as it is */
  unsigned fself;         /* re-signed as FSELF */
  uint64_t bytes;         /* bytes written for decrypted files */
  int      critical;      /* the kernel pointer could not be put back: the console must be restarted */
  char     why[220];      /* the reason, when the return value is negative; else the last hint */
} dj_self_res_t;

/* rel: the file under way (NULL: unchanged); done of total counts the files looked at; line: a line for the dump's log (or NULL) */
typedef void (*dj_self_cb_t)(void *ctx, const char *rel, unsigned done, unsigned total, const char *line);

/* 1 when the firmware is one the table of the self pager knows (no kernel access is made to find out) */
int ps5tm_dump_self_supported(void);

/* Counts the files below src_root that can hold a SELF; *largest gets the size of the biggest, *bytes the sum. 0, or 1 when cancelled. */
int ps5tm_dump_self_count(const char *src_root, volatile int *cancel, unsigned *count, uint64_t *largest, uint64_t *bytes);

/* Walks src_root (the folder the console mounts for the title) and writes the plain version of every SELF found into the same
   place below dst_root, through a temporary name, replacing the copy there only when the file is whole. With fself the plain ELF
   is kept below dst_root/decrypted/ and the file in place becomes an FSELF; with keep_plain (the dumper does that for fself and
   for the SDK backport) the plain file is kept there too, while the file in place stays a plain ELF. Only ever called from the
   dump's own thread.
   Returns 0 done (res says how many), 1 cancelled, -1 failed (res->why), DJ_SELF_UNSUPPORTED firmware not in the table. */
int ps5tm_dump_self_run(const char *src_root, const char *dst_root, const char *temp_suffix, int fself, int keep_plain,
                        volatile int *cancel, dj_self_cb_t cb, void *ctx, dj_self_res_t *res);

/* elf2fself of the dumper: the plain ELF at elf_path becomes an FSELF at fself_path. 0, or -1 with the reason in why. */
int ps5tm_dump_elf2fself(const char *elf_path, const char *fself_path, char *why, size_t why_len);

#endif
