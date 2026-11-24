/* Block device access with sector alignment and a small LRU read cache.
 *
 * Raw disk devices on FreeBSD (and therefore the PS5) only accept reads whose
 * offset and length are multiples of the sector size. Everything above this
 * layer can read at any byte offset and length. */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <pthread.h>

typedef struct bdev bdev_t;

/* Open a device node or an image file read-only. Returns NULL on error. */
bdev_t *bdev_open(const char *path);

/* A view of part of another device (a partition). Holds a reference to the
 * parent; closing the view drops it. */
bdev_t *bdev_slice(bdev_t *parent, uint64_t offset, uint64_t size);

void bdev_close(bdev_t *b);

/* Read len bytes at off. Returns 0 on success, -1 on error or past the end. */
int bdev_read(bdev_t *b, uint64_t off, void *buf, size_t len);

/* Reopen the underlying device read-write. Returns 0 or -errno. */
int bdev_enable_write(bdev_t *b);
int bdev_is_writable(const bdev_t *b);

/* Write len bytes at off (any alignment; partial sectors are
 * read-modify-written). Returns 0 on success, -1 on error. */
int bdev_write(bdev_t *b, uint64_t off, const void *buf, size_t len);
/* (app) As bdev_write, but on the device before it returns (file data); bdev_write keeps small writes in the cache until the next
 * bdev_flush. */
int bdev_write_through(bdev_t *b, uint64_t off, const void *buf, size_t len);

/* Push written data to the device. */
int bdev_flush(bdev_t *b);
/* (app) The cached writes to the device, in order, without fsync (an ordering point between two steps). */
int bdev_writeout(bdev_t *b);
/* (app) Ask the device to empty its own write cache (on eject). */
void bdev_flush_cache(bdev_t *b);

uint64_t    bdev_size(const bdev_t *b);
uint32_t    bdev_sector_size(const bdev_t *b);
const char *bdev_path(const bdev_t *b);
