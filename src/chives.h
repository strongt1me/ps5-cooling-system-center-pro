/* PS5 Cooling & System Center - Pro: the sidecar of an exported save, chives.json (chives.h, 19.11.2026).
 *
 * A save that has been read out into plain files cannot say how its image was built: how big it was, how its file system was laid out, who owned which file.
 * Putting it back as an image the console accepts needs exactly that, so the ZIP carries a small JSON next to the files:
 *
 *   { "schema_version": 1, "platform": "PS5", "title_id": "...", "save_directory": "...", "encrypted_name": "sdimg_...",
 *     "original_image_size_bytes": N,
 *     "pfs": { "version": 2, "block_size_bytes": 32768|65536, "block_count": N, "mode_raw": N, "backup_header_count": N, "fs_ext": N },
 *     "ownership": [ { "path": "...", "uid": N, "gid": N, "mode": N }, ... ] }
 *
 * The same names and meaning as the tool the community already uses for this (so a ZIP made by either can be read by the other); read and written here
 * without any of its code. Everything read from a ZIP is taken as hostile: sizes, counts, names and numbers are checked, nothing is ever trusted into a buffer.
 *
 * Pure functions, so a host test can try them (test/chives). */
#ifndef PS5TM_CHIVES_H
#define PS5TM_CHIVES_H

#include <stddef.h>
#include <stdint.h>

#define CHIVES_MAX_JSON       262144u                    /* a larger sidecar is refused */
#define CHIVES_MAX_OWN_READ   2048u                      /* ownership entries accepted when reading */
#define CHIVES_MAX_OWN_WRITE  256u                       /* ... and written (the other tool refuses more: then none are written) */
#define CHIVES_MAX_IMAGE      (UINT64_C(64) << 30)
#define CHIVES_PATH_MAX       256u

typedef struct {
  char     path[CHIVES_PATH_MAX];
  uint32_t uid, gid;
  uint16_t mode;                                         /* permission bits only (07777) */
} chives_own_t;

typedef struct {
  uint64_t version;                                      /* 2 for a PS5 save */
  uint64_t block_count;
  uint64_t image_size;                                   /* what the image file measured */
  uint32_t block_size, backup_header_count, fs_ext;
  uint16_t mode_raw;
  int      has_fs_ext;
  char     title_id[16];
  char     save_dir[96];
  char     image_name[128];                              /* the file name of the image on the console (sdimg_...) */
  chives_own_t *own;                                     /* owned by the struct: chives_free */
  unsigned nown;
  unsigned own_cap;
} chives_t;

void chives_init(chives_t *g);
void chives_free(chives_t *g);

/* Adds one ownership entry (path is copied; refused when unsafe or too long). 0 or -1. */
int chives_own_add(chives_t *g, const char *path, uint32_t uid, uint32_t gid, unsigned mode);

/* The geometry of a PS5 image from its first bytes (at least 0x40, 0x36c to get fs_ext) and its size on disk. 0 when it is a sound PS5 image, else -1. */
int chives_from_header(const uint8_t *hdr, size_t len, uint64_t file_size, chives_t *g);

/* 1 when the numbers describe an image that can be made again. */
int chives_valid(const chives_t *g);

/* 1 when name is a safe relative path (no absolute, no "..", no backslash or colon, no empty part). */
int chives_path_safe(const char *name);
/* 1 when a ZIP entry name is the sidecar itself ("chives.json", any case, an optional "./" in front). */
int chives_is_sidecar(const char *name);

/* The JSON text: its length (without a terminator beyond len), or -1 when it does not fit cap. Ownership is left out when there are more than
   CHIVES_MAX_OWN_WRITE entries. */
int chives_write(const chives_t *g, char *out, size_t cap);

/* Reads a sidecar. 0 on success (g is then filled and must be freed with chives_free), -1 when it is not a sound sidecar of a PS5 save. */
int chives_parse(const char *json, size_t len, chives_t *g);

#endif
