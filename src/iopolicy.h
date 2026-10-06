/* Reading and writing at the same time, or one after the other — see iopolicy.c. */
#ifndef PS5TM_IOPOLICY_H
#define PS5TM_IOPOLICY_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

/* 1 when src and dst (a file, or a folder that need not exist yet) lie on two
   different physical drives, so reading one and writing the other may
   overlap; 0 when they share a drive or that is not certain. why says what
   decided it, for the log. */
int ps5tm_io_parallel(const char *src, const char *dst, char *why, size_t why_len);

/* Reads ahead on a thread of its own: rd(ctx) is called into nbuf buffers of
   chunk bytes while the caller is busy with the last ones. */
typedef ssize_t (*ps5tm_read_fn)(void *ctx, void *buf, size_t len);
typedef struct ps5tm_prefetch ps5tm_prefetch_t;

/* NULL when there is no memory or thread for it — the caller then reads
   directly, which is merely slower. */
ps5tm_prefetch_t *ps5tm_prefetch_start(ps5tm_read_fn rd, void *ctx, size_t chunk,
                                       unsigned nbuf);
/* In the shape of ps5tm_read_fn: the next up to len bytes in order, 0 at the
   end, -1 where rd failed, with the errno it left. */
ssize_t ps5tm_prefetch_read(void *pf, void *buf, size_t len);
/* Stops the thread once its current read returns, and frees everything. */
void    ps5tm_prefetch_stop(ps5tm_prefetch_t *pf);

#endif
