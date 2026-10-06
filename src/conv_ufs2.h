/* UFS2 image writer (.ffpkg) — see conv_ufs2.c. */
#ifndef PS5TM_CONV_UFS2_H
#define PS5TM_CONV_UFS2_H

#include <stddef.h>
#include <stdint.h>

/* The largest single file this version can place: 12 direct blocks, one
   single-indirect block (8192 pointers) and one double-indirect block (8192
   single-indirect ones) at the fixed 64 KiB block size, 4 398 584 168 448 bytes
   (4 TiB and a little). Without the double-indirect block it would be
   537 657 344 bytes (512.75 MiB). Exposed so callers can reject or warn before
   starting the build. */
#define UFS2_MAX_FILE_BYTES ((uint64_t)(12 + 8192 + 8192 * 8192) * 65536)

/* The CRC-32 of every file's data, taken while the writer read it, by inode
   number: what the finished image is compared with. */
typedef struct ufs2_sums ufs2_sums_t;

/* Scans source_root and writes a UFS2 image of its tree (64 KiB blocks and
   fragments, as ShadowMountPlus recommends) into fd (an empty file open for
   reading and writing). Direct blocks plus a single- and a double-indirect
   block per file, no triple-indirect one (conv_ufs2.c has the rationale): a
   file over UFS2_MAX_FILE_BYTES, or a directory with more entries than its 12
   direct blocks hold, is refused while the tree is sized — with a reason that
   names it, before anything is written to fd; so is a tree of almost nothing
   but empty files, whose inode tables would be bigger than its data. 0 on
   success with the image size in *image_size; -1 with a reason in err, errno
   ECANCELED when *cancel stopped it. With sums not NULL, a successful write
   hands over the CRC-32s of the files' data (ufs2_sums_free() them). */
int ufs2_write_tree(int fd, const char *source_root, const int *cancel,
                    void (*progress)(void *ctx, uint64_t done_bytes), void *progress_ctx,
                    uint64_t *image_size, ufs2_sums_t **sums, char *err, size_t err_len);

/* The size of the image ufs2_write_tree() would write for source_root, with
   the same refusals (a file or folder that does not fit, too many entries),
   from the folders and the sizes of the files alone, without reading any of
   them. For asking whether a drive has the room before anything is written.
   0 with the size in *image_size; -1 with a reason in err. */
int ufs2_plan_size(const char *source_root, uint64_t *image_size, char *err, size_t err_len);

/* How many bytes of file data ufs2_verify_tree() reads when it has sums. */
uint64_t ufs2_sums_bytes(const ufs2_sums_t *sums);
void     ufs2_sums_free(ufs2_sums_t *sums);

/* Reads the finished image back, read-only: the superblock; that the groups
   cover the image and every group header (magic, number) is in place with
   its room before the inode table; then every directory and file reachable
   from the root. A directory: a size within its 12 blocks, pointers inside
   the image, records that tile each block. A file: type, size, exactly the
   block pointers that size needs (direct and indirect, inside the image) and
   the block count. With sums, also what the data blocks hold: every file's
   blocks are read and their CRC-32 must be the one taken while writing
   (progress gets the bytes of file data read so far; *cancel stops it, errno
   ECANCELED). Without sums that is not looked at; the bitmaps and the free
   counters never are. 0 when it holds together. */
int ufs2_verify_tree(int fd, const ufs2_sums_t *sums, const int *cancel,
                     void (*progress)(void *ctx, uint64_t done_bytes), void *progress_ctx,
                     char *err, size_t err_len);

#endif
