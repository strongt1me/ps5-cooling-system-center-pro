/* Checking what a copy or conversion wrote, and the checksum files left next
   to a backup — see checkfile.c. */
#ifndef PS5TM_CHECKFILE_H
#define PS5TM_CHECKFILE_H

#include <stddef.h>
#include <stdint.h>

#define PS5TM_SUMS_EXT ".sha256"

/* Asks the system to forget the cached pages of fd, so that reading it back
   comes from the drive and not from memory. Best effort: nothing happens
   where the system does not offer it. */
void ps5tm_drop_cache(int fd);

/* Reads fd from offset 0 to size in order: CRC-32 into *crc and SHA-256 into
   sha, either may be NULL. progress gets the bytes read in this call so far
   (not a running total over several files). 0 on success; -1 with a reason in
   err (errno ECANCELED when *cancel became non-zero). */
int ps5tm_digest_fd(int fd, uint64_t size, uint32_t *crc, uint8_t sha[32],
                    const int *cancel, void (*progress)(void *ctx, uint64_t done),
                    void *progress_ctx, char *err, size_t err_len);

/* A checksum file in the format of sha256sum ("<hex>  <name>"), so a PC can
   check the backup with the tools it already has: sha256sum -c, or 7-Zip's
   hash tools. It is written as the lines come in, to "<path>.ps5cc-teil",
   not kept in memory (a game folder has a hundred thousand files), and given
   its real name only by ps5tm_sums_close(): it is never seen half written. An
   older file of that name is replaced then — it can only belong to a backup of
   the same name that is no longer there, since nothing is copied over an
   existing one. */
typedef struct ps5tm_sums ps5tm_sums_t;

/* NULL with a reason in err. */
ps5tm_sums_t *ps5tm_sums_open(const char *path, char *err, size_t err_len);
/* name as it should read in the file (relative to the folder the file lies
   in); characters sha256sum escapes are escaped. 0, or -1 when a write failed
   (also reported by the close). */
int  ps5tm_sums_add(ps5tm_sums_t *s, const uint8_t sha[32], const char *name);
/* Flushes, syncs, closes and renames into place; frees s. 0, or -1 with a
   reason in err (the temporary file is removed then). */
int  ps5tm_sums_close(ps5tm_sums_t *s, char *err, size_t err_len);
/* Drops the temporary file; frees s. */
void ps5tm_sums_abort(ps5tm_sums_t *s);

#endif
