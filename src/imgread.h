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
   folder is not there. cb may be NULL: the call then only says whether the folder is there. */
int     imgr_list(imgr_t *r, const char *path, int (*cb)(const char *name, int is_dir, void *ctx), void *ctx);

/* The whole tree of an image, for checking and unpacking (11.10.2026). Names are UTF-8. */
#define IMGR_PATH_MAX 1024
typedef struct {
  char     path[IMGR_PATH_MAX];  /* "/data/level1.pak": from the root, parents come before what they hold */
  int      is_dir;
  int      damaged;              /* a folder whose entries could not be read: it is reported, then skipped */
  uint64_t size;                 /* bytes of a file, 0 for a folder */
  uint64_t h1;                   /* the reader's own handle (first cluster, inode), for the calls below */
  uint32_t h2;
} imgr_entry_t;

/* Calls cb for every file and folder, depth first, parents first (cb gets the entry for the length of the call only);
   cb returns non-zero to stop. 0 when the walk is complete, 1 when cb stopped it, -1 when the root cannot be read or
   the tree is nested too deeply. A folder that cannot be read is reported with damaged set and the walk goes on. */
int     imgr_walk(imgr_t *r, int (*cb)(const imgr_entry_t *e, void *ctx), void *ctx);
/* Reads up to n bytes of a file from off (like imgr_pread, without looking the path up again). */
long    imgr_read_entry(imgr_t *r, const imgr_entry_t *e, uint64_t off, void *buf, size_t n);
/* Whether the file's data lies where the file system says: every cluster or block of it is inside the image and in a
   chain that is long enough. deep also reads every byte (what a drive that has gone bad shows). 0 when it holds,
   -1 with *bad_off the first offset that does not, or the size when the chain is too short. *cancel (may be NULL) stops
   a deep read: -1 with errno ECANCELED. progress (may be NULL) gets the bytes read so far of this file. */
int     imgr_verify_entry(imgr_t *r, const imgr_entry_t *e, int deep, uint64_t *bad_off, const int *cancel,
                          void (*progress)(void *ctx, uint64_t done), void *ctx);

#endif
