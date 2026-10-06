/* PFS image with one PFSC-compressed file (.ffpfsc) — see conv_pfs.c. */
#ifndef PS5TM_CONV_PFS_H
#define PS5TM_CONV_PFS_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

/* Delivers the next up to len bytes of the payload in order: the count, 0 at
   the end, -1 on error with errno set. */
typedef ssize_t (*conv_read_fn)(void *ctx, void *buf, size_t len);

typedef struct {
  int       level;            /* libdeflate level, 1-12; 6 is its default  */
  int       workers;          /* compression threads, 0 = in the caller    */
  const int *cancel;          /* stops the build when it becomes non-zero  */
  void    (*progress)(void *ctx, uint64_t raw_done);
  void     *progress_ctx;
  uint32_t *raw_crc;          /* out, may be NULL: CRC-32 of the payload   */
} pfsc_opts_t;

/* Writes an unsigned PS5 PFS image holding one file, inner_name (ASCII),
   whose raw_size bytes come from rd and are stored PFSC-compressed. fd is an
   empty file open for reading and writing. 0 on success with the image size
   in *image_size; -1 with a reason in err, errno ECANCELED when cancelled. */
int pfs_write_single(int fd, const char *inner_name, uint64_t raw_size,
                     conv_read_fn rd, void *rd_ctx, const pfsc_opts_t *o,
                     uint64_t *image_size, char *err, size_t err_len);

typedef struct {
  uint32_t   raw_crc;         /* what pfs_write_single() reported            */
  const int *cancel;
  void     (*progress)(void *ctx, uint64_t done);   /* bytes of the image read */
  void      *progress_ctx;
} pfsc_check_t;

/* Reads a finished image back from the drive, all of it and in order: header,
   inodes, PFSC header, the whole block table (offsets rise, no block longer
   than a logical block, none outside the stream), the areas that must be
   zero — and every block, unpacked, with the CRC-32 of the whole payload
   compared against the one taken while writing. Every byte of the file also
   goes into sha (SHA-256, may be NULL) for the checksum file. 0 when it all
   holds together; -1 with the reason in err, errno ECANCELED on a cancel. */
int pfs_verify_full(int fd, const pfsc_check_t *c, uint8_t sha[32],
                    char *err, size_t err_len);

/* Whether the compression threads got the idle scheduling class: 1 yes,
   0 refused, -1 no thread has tried yet. */
int pfs_workers_idle(void);

#endif
