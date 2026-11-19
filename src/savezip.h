/* PS5 Cooling & System Center - Pro: ZIP files for carrying a save to another console (savezip.h, 19.11.2026).
 *
 * A save leaves the console as a ZIP of plain files (the format every tool opens) and comes in again from one. Written here: entries stored without
 * compression (a save is small, and the sizes and the checksum are then known before the data is written - the local headers carry them, as the tools that
 * read these files need), UTF-8 names, folders as entries of their own, the unix mode in the attributes. Read here: stored and deflated entries, the sizes
 * taken from the central directory, every checksum verified, every name checked (no absolute path, no "..", no backslash, no control character).
 *
 * No ZIP64: a file, an entry or the whole archive of 4 GiB or more is refused with SZ_E_TOO_BIG (a save of that size does not exist).
 *
 * Plain C on file descriptors, so a host test can try it (test/savezip). */
#ifndef PS5TM_SAVEZIP_H
#define PS5TM_SAVEZIP_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

enum {
  SZ_OK = 0,
  SZ_E_IO = -1,            /* a read, a write or an open failed (errno is set) */
  SZ_E_TOO_BIG = -2,       /* beyond what the format without ZIP64 can say, or beyond the limit given */
  SZ_E_BAD = -3,           /* not a ZIP, a broken one, or one this reader does not take (ZIP64, encrypted, an unknown method) */
  SZ_E_NAME = -4,          /* an entry name that is not safe */
  SZ_E_CRC = -5,           /* the data does not match its checksum */
  SZ_E_CHANGED = -6,       /* a file changed while it was being read */
  SZ_E_NOMEM = -7,
  SZ_E_STOP = -8           /* the caller's tick asked to stop */
};

#define SZ_MAX_ENTRIES 60000u
#define SZ_MAX_NAME    500u

/* 1 when name is a safe relative path inside an archive (also used for the names this app writes). */
int savezip_name_ok(const char *name);

/* ------------------------------------------------------------------ writer */

typedef struct szw szw_t;

/* tick(ctx, bytes) is called with the bytes just processed; returning non-zero stops the work (SZ_E_STOP). May be NULL. */
typedef int (*sz_tick_fn)(void *ctx, uint64_t bytes);

/* Creates path (never overwrites: an existing file is an error). NULL on failure (errno set). */
szw_t *szw_create(const char *path);
int szw_add_dir(szw_t *z, const char *name, time_t mtime, unsigned mode);
int szw_add_mem(szw_t *z, const char *name, const void *data, size_t len, time_t mtime, unsigned mode);
/* Copies the file src into the archive: read twice (checksum, then data); a file that changed in between is SZ_E_CHANGED. */
int szw_add_file(szw_t *z, const char *name, const char *src, time_t mtime, unsigned mode, sz_tick_fn tick, void *ctx);
uint64_t szw_size(const szw_t *z);               /* bytes written so far */
unsigned szw_count(const szw_t *z);              /* entries so far */
int szw_finish(szw_t *z);                        /* writes the directory, syncs, closes and frees: SZ_OK or an error (the file is then removed) */
void szw_abort(szw_t *z);                        /* closes, frees and removes the file */

/* ------------------------------------------------------------------ reader */

typedef struct {
  char     name[SZ_MAX_NAME + 1];
  uint16_t method;                               /* 0 stored, 8 deflate */
  uint32_t crc;
  uint64_t csize, usize;
  uint32_t lho;                                  /* offset of the local header */
  uint32_t attr;                                 /* external attributes (the unix mode is attr >> 16) */
  int      is_dir;
} szr_entry_t;

typedef struct szr szr_t;

/* Opens path and reads the directory. NULL on failure; *err (when not NULL) gets SZ_E_*. */
szr_t *szr_open(const char *path, int *err);
unsigned szr_count(const szr_t *z);
const szr_entry_t *szr_at(const szr_t *z, unsigned i);
/* The whole entry in a new buffer (freed by the caller), checksum verified; usize above max_len is SZ_E_TOO_BIG. */
int szr_read(szr_t *z, unsigned i, uint8_t **out, size_t *len, size_t max_len);
/* The entry written to fd (stored: in pieces; deflated: through memory up to max_mem bytes), checksum verified. */
int szr_extract(szr_t *z, unsigned i, int fd, size_t max_mem, sz_tick_fn tick, void *ctx);
void szr_close(szr_t *z);

#endif
