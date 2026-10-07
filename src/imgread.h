/* A read-only reader for the images ShadowMountPlus mounts (07.10.2026): a few files out of an image without mounting it.
 *
 * The app only ever needs two small things from an image game — eboot.bin (its first bytes and a program header) and the
 * names in the folder fakelib — and ShadowMountPlus 1.7 keeps images unmounted until a game starts. Asking it to mount
 * each image for a moment works (library.c, the first version of the image probe) but is a visit to its mount machinery;
 * reading the container directly touches nothing but the image file itself, at any time, game running or not.
 *
 * Formats (found by what the first blocks say, not by the file name):
 *   .exfat   an exFAT file system
 *   .ffpfsc  a PFS container (version 2) holding one PFSC-compressed file, which is an exFAT image
 *   .ffpkg   a UFS2 file system
 * Anything else: imgr_open() answers NULL and the caller keeps its other way.
 *
 * Paths are those of the game's tree, "/eboot.bin", "/fakelib". Names are compared without regard to case. */
#ifndef PS5TM_IMGREAD_H
#define PS5TM_IMGREAD_H

#include <stddef.h>
#include <stdint.h>

typedef struct imgr imgr_t;

/* NULL when the file is not readable or not an image of a known kind; err (may be NULL) gets the reason. */
imgr_t *imgr_open(const char *image_path, char *err, size_t err_len);
void    imgr_close(imgr_t *r);
/* "exfat", "ffpfsc" or "ffpkg". */
const char *imgr_kind(const imgr_t *r);

/* Reads up to n bytes of the file at path from offset off. Returns the bytes read (0 at the end), -1 when there is no
   such file or on a read error. */
long    imgr_pread(imgr_t *r, const char *path, uint64_t off, void *buf, size_t n);
/* Calls cb for every entry of the folder at path (not "." or ".."); cb returns non-zero to stop. 0, or -1 when the
   folder is not there. */
int     imgr_list(imgr_t *r, const char *path, int (*cb)(const char *name, int is_dir, void *ctx), void *ctx);

#endif
