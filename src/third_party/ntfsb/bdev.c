#include "bdev.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

#if defined(__FreeBSD__) || defined(__PROSPERO__)
#include <sys/disk.h>
#elif defined(__linux__)
#include <linux/fs.h>
#endif

#define CACHE_BLOCK   (64 * 1024)  /* bytes per cache block */
#define CACHE_BLOCKS  128          /* 8 MiB per physical device */
#define DIRECT_MIN    (256 * 1024) /* larger aligned reads skip the cache */

typedef struct cblock {
  uint64_t blkno;
  uint64_t stamp;
  int      valid;
  int      dirty;           /* (app) written in the cache, not yet on the device */
  int      bad;             /* (audit) the last try to write it out failed: it is not taken for another block while there is another */
  uint8_t  dsec[(CACHE_BLOCK / 512 + 7) / 8];   /* (app) which sectors of it (bit per device sector) */
  uint8_t *data;
} cblock_t;

struct bdev {
  char      path[300];
  int       fd;             /* -1 for slices */
  bdev_t   *parent;         /* non-NULL for slices */
  uint64_t  base;           /* offset inside parent */
  uint64_t  size;
  uint32_t  sector;
  int       refs;
  int       writable;

  /* cache, physical devices only */
  pthread_mutex_t lock;
  cblock_t  cache[CACHE_BLOCKS];
  uint64_t  clock;
};

static int cache_out(bdev_t *root);

static int read_full(int fd, uint64_t off, void *buf, size_t len) {
  uint8_t *p = buf;
  while (len) {
    ssize_t n = pread(fd, p, len, (off_t)off);
    if (n < 0) {
      if (errno == EINTR) continue;
      return -1;
    }
    if (n == 0) {
      /* End of an image file that is not a whole number of sectors. */
      memset(p, 0, len);
      return 0;
    }
    p += n; off += (uint64_t)n; len -= (size_t)n;
  }
  return 0;
}

bdev_t *bdev_open(const char *path) {
  int fd = open(path, O_RDONLY);
  if (fd < 0) return NULL;

  struct stat st;
  if (fstat(fd, &st) != 0) { close(fd); return NULL; }

  uint64_t size = 0;
  uint32_t sector = 512;

  if (S_ISREG(st.st_mode)) {
    size = (uint64_t)st.st_size;
  } else {
#if defined(__FreeBSD__) || defined(__PROSPERO__)
    off_t ms = 0; u_int ss = 0;
    if (ioctl(fd, DIOCGMEDIASIZE, &ms) == 0) size = (uint64_t)ms;
    if (ioctl(fd, DIOCGSECTORSIZE, &ss) == 0 && ss) sector = ss;
#elif defined(__linux__)
    unsigned long long ms = 0; int ss = 0;
    if (ioctl(fd, BLKGETSIZE64, &ms) == 0) size = ms;
    if (ioctl(fd, BLKSSZGET, &ss) == 0 && ss > 0) sector = (uint32_t)ss;
#endif
    if (!size) {
      off_t end = lseek(fd, 0, SEEK_END);
      if (end > 0) size = (uint64_t)end;
    }
  }
  if (!size || sector < 512 || sector > CACHE_BLOCK || (sector & (sector - 1))) {   /* (app) < 512: the dirty map counts 512-byte sectors */
    close(fd);
    return NULL;
  }

  bdev_t *b = calloc(1, sizeof *b);
  if (!b) { close(fd); return NULL; }
  strncpy(b->path, path, sizeof b->path - 1);
  b->fd = fd;
  b->size = size;
  b->sector = sector;
  b->refs = 1;
  pthread_mutex_init(&b->lock, NULL);
  return b;
}

bdev_t *bdev_slice(bdev_t *parent, uint64_t offset, uint64_t size) {
  if (offset > parent->size || size > parent->size - offset) return NULL;
  bdev_t *b = calloc(1, sizeof *b);
  if (!b) return NULL;
  bdev_t *root = parent->parent ? parent->parent : parent;
  snprintf(b->path, sizeof b->path, "%.250s@%llu", parent->path,
           (unsigned long long)offset);
  b->fd = -1;
  b->parent = root;
  b->base = offset + parent->base;
  b->size = size;
  b->sector = parent->sector;
  b->refs = 1;
  pthread_mutex_lock(&root->lock);
  root->refs++;
  pthread_mutex_unlock(&root->lock);
  return b;
}

void bdev_close(bdev_t *b) {
  if (!b) return;
  if (b->parent) {
    bdev_t *root = b->parent;
    free(b);
    bdev_close(root);
    return;
  }
  pthread_mutex_lock(&b->lock);
  int left = --b->refs;
  pthread_mutex_unlock(&b->lock);
  if (left > 0) return;
  if (b->writable) (void)cache_out(b);            /* (app) normally empty: the unmount flushed */
  for (int i = 0; i < CACHE_BLOCKS; i++) free(b->cache[i].data);
  close(b->fd);
  pthread_mutex_destroy(&b->lock);
  free(b);
}

static int write_full(int fd, uint64_t off, const void *buf, size_t len);

/* (app, 24.11.2026) The changed sectors of a dirty block to the device, one write per run of them; caller holds the lock. 0 or -1
 * (what is not written stays marked). */
static int block_out(bdev_t *b, cblock_t *c) {
  if (!c->valid || !c->dirty) return 0;
  uint64_t off = c->blkno * CACHE_BLOCK;
  size_t len = b->size - off < CACHE_BLOCK ? (size_t)(b->size - off) : CACHE_BLOCK;
  uint32_t ss = b->sector, ns = (uint32_t)((len + ss - 1) / ss);
  for (uint32_t s = 0; s < ns;) {
    if (!(c->dsec[s / 8] & (1u << (s % 8)))) { s++; continue; }
    uint32_t e = s;
    while (e < ns && (c->dsec[e / 8] & (1u << (e % 8)))) e++;
    size_t a = (size_t)s * ss, z = (size_t)e * ss < len ? (size_t)e * ss : len;
    if (write_full(b->fd, off + a, c->data + a, z - a)) { c->bad = 1; return -1; }
    for (uint32_t i = s; i < e; i++) c->dsec[i / 8] &= (uint8_t)~(1u << (i % 8));
    s = e;
  }
  c->dirty = 0;
  c->bad = 0;
  return 0;
}

/* (app) Every dirty block that overlaps [off, off + len) to the device; caller holds the lock. */
static int range_out(bdev_t *b, uint64_t off, uint64_t len) {
  for (int i = 0; i < CACHE_BLOCKS; i++) {
    cblock_t *c = &b->cache[i];
    if (!c->valid || !c->dirty) continue;
    uint64_t bs = c->blkno * CACHE_BLOCK;
    if (bs < off + len && off < bs + CACHE_BLOCK && block_out(b, c)) return -1;
  }
  return 0;
}

/* Fetch one cache block; caller holds the lock. */
static cblock_t *cache_get(bdev_t *b, uint64_t blkno) {
  cblock_t *victim = NULL;
  for (int round = 0; round < 2; round++) {       /* 2nd round: the 1st victim could not be written out and is marked bad now */
    victim = NULL;
    for (int i = 0; i < CACHE_BLOCKS; i++) {
      cblock_t *c = &b->cache[i];
      if (c->valid && c->blkno == blkno) {
        c->stamp = ++b->clock;
        return c;
      }
      /* (audit 24.11.2026) a block that could not be written out stays where it is: if it were taken as the oldest, every read of a block
       * that is not cached would fail for as long as the device refuses that write (a bad sector, a stick gone read-only) */
      if (c->valid && c->bad) continue;
      if (!victim || !c->valid || (victim->valid && c->stamp < victim->stamp)) victim = c;
    }
    if (!victim) return NULL;
    if (!victim->data && !(victim->data = malloc(CACHE_BLOCK))) return NULL;
    if (!block_out(b, victim)) break;             /* (app) what was written in it goes to the device first */
    victim = NULL;
  }
  if (!victim) return NULL;

  uint64_t off = blkno * CACHE_BLOCK;
  size_t len = CACHE_BLOCK;
  if (off + len > b->size) {
    /* Short final block: round up to a whole sector. */
    len = (size_t)(b->size - off);
    len = (len + b->sector - 1) & ~(size_t)(b->sector - 1);
    memset(victim->data, 0, CACHE_BLOCK);
  }
  victim->valid = 0;
  if (read_full(b->fd, off, victim->data, len) != 0) return NULL;
  victim->valid = 1;
  victim->blkno = blkno;
  victim->stamp = ++b->clock;
  return victim;
}

int bdev_read(bdev_t *b, uint64_t off, void *buf, size_t len) {
  if (off > b->size || len > b->size - off) return -1;
  if (!len) return 0;

  bdev_t *root = b;
  if (b->parent) {
    root = b->parent;
    off += b->base;
  }

  uint8_t *out = buf;
  uint32_t ss = root->sector;

  /* Large, aligned reads go straight to the device. (app) What the cache holds written for that range goes out first. */
  if (len >= DIRECT_MIN && !(off % ss) && !(len % ss)) {
    pthread_mutex_lock(&root->lock);
    int rc = range_out(root, off, len) ? -1 : read_full(root->fd, off, out, len);
    pthread_mutex_unlock(&root->lock);
    return rc;
  }

  pthread_mutex_lock(&root->lock);
  while (len) {
    uint64_t blkno = off / CACHE_BLOCK;
    size_t inblk = (size_t)(off % CACHE_BLOCK);
    size_t n = CACHE_BLOCK - inblk;
    if (n > len) n = len;
    cblock_t *c = cache_get(root, blkno);
    if (!c) {
      pthread_mutex_unlock(&root->lock);
      return -1;
    }
    memcpy(out, c->data + inblk, n);
    out += n; off += n; len -= n;
  }
  pthread_mutex_unlock(&root->lock);
  return 0;
}

static int write_full(int fd, uint64_t off, const void *buf, size_t len) {
  if (!len) return 0;
  const uint8_t *p = buf;
  while (len) {
    ssize_t n = pwrite(fd, p, len, (off_t)off);
    if (n < 0) {
      if (errno == EINTR) continue;
      return -1;
    }
    if (n == 0) return -1;
    p += n; off += (uint64_t)n; len -= (size_t)n;
  }
  return 0;
}

/* (app, 23.11.2026) Is fd (the node opened anew) still the disk old (the fd the drive was read through)? A stick swapped for another under
 * the same /dev/daN between two looks of the app was opened here and written with the first stick's cached tables (its files were lost).
 * On the console the old fd of an unplugged disk answers with an error; a disk that is there has the same size on both. An image file
 * (host tests) is the same file. */
static int same_disk(int old, int fd) {
  struct stat a, b;
  if (fstat(old, &a) != 0 || fstat(fd, &b) != 0) return 0;
  if (S_ISREG(a.st_mode) || S_ISREG(b.st_mode))
    return S_ISREG(a.st_mode) && S_ISREG(b.st_mode) && a.st_dev == b.st_dev && a.st_ino == b.st_ino;
  if (a.st_rdev != b.st_rdev) return 0;
#if defined(__FreeBSD__) || defined(__PROSPERO__)
  off_t ma = 0, mb = 0;
  if (ioctl(old, DIOCGMEDIASIZE, &ma) != 0 || ioctl(fd, DIOCGMEDIASIZE, &mb) != 0 || ma != mb) return 0;
#endif
  return 1;
}

int bdev_enable_write(bdev_t *b) {
  bdev_t *root = b->parent ? b->parent : b;
  pthread_mutex_lock(&root->lock);
  int rc = 0;
  if (!root->writable) {
    int fd = open(root->path, O_RDWR);
    if (fd < 0) {
      rc = -errno;
    } else {
      /* dup2 swaps the descriptor atomically, so reads running in other
       * threads keep working. */
      if (!same_disk(root->fd, fd)) rc = -ENODEV;
      else if (dup2(fd, root->fd) < 0) rc = -errno;
      else root->writable = 1;
      close(fd);
    }
  }
  pthread_mutex_unlock(&root->lock);
  return rc;
}

int bdev_is_writable(const bdev_t *b) {
  const bdev_t *root = b->parent ? b->parent : b;
  return root->writable;
}

/* Copy freshly written bytes into any cached blocks they overlap. */
static void cache_update(bdev_t *root, uint64_t off, const uint8_t *data, size_t len) {
  for (int i = 0; i < CACHE_BLOCKS; i++) {
    cblock_t *c = &root->cache[i];
    if (!c->valid) continue;
    uint64_t bs = c->blkno * CACHE_BLOCK, be = bs + CACHE_BLOCK;
    uint64_t s = off > bs ? off : bs, e = off + len < be ? off + len : be;
    if (s < e) memcpy(c->data + (s - bs), data + (s - off), (size_t)(e - s));
  }
}

int bdev_write(bdev_t *b, uint64_t off, const void *buf, size_t len) {
  if (off > b->size || len > b->size - off) return -1;
  if (!len) return 0;
  bdev_t *root = b;
  if (b->parent) {
    root = b->parent;
    off += b->base;
  }
  if (!root->writable) return -1;

  int rc = 0;
  uint32_t ss = root->sector;
  pthread_mutex_lock(&root->lock);
  if (!(off % ss) && !(len % ss) && len >= CACHE_BLOCK) {
    /* file data: straight to the device; a cached copy of the range (dirty or not) takes the new bytes */
    rc = write_full(root->fd, off, buf, len);
    if (!rc) cache_update(root, off, buf, len);
  } else {
    /* (app, 24.11.2026) small or unaligned (records, index blocks, bitmap bytes): into the cache, out at the next flush or when the
     * block is needed for another. Before, each became a read and a write of the edge sectors on the device: a cheap stick took one
     * to three seconds for every new file or folder. */
    const uint8_t *in = buf;
    while (len && !rc) {
      uint64_t blkno = off / CACHE_BLOCK;
      size_t inblk = (size_t)(off % CACHE_BLOCK);
      size_t n = CACHE_BLOCK - inblk < len ? CACHE_BLOCK - inblk : len;
      cblock_t *c = cache_get(root, blkno);
      if (!c) { rc = -1; break; }
      memcpy(c->data + inblk, in, n);
      for (size_t s = inblk / ss; s <= (inblk + n - 1) / ss; s++) c->dsec[s / 8] |= (uint8_t)(1u << (s % 8));
      c->dirty = 1;
      in += n; off += n; len -= n;
    }
  }
  pthread_mutex_unlock(&root->lock);
  return rc;
}

/* (app, 24.11.2026) For file data: to the device now, as every write went before. A cached copy of the range takes the new bytes;
 * dirty blocks under the edge sectors go out first, so that the read of the edges sees what the cache holds. */
int bdev_write_through(bdev_t *b, uint64_t off, const void *buf, size_t len) {
  if (off > b->size || len > b->size - off) return -1;
  if (!len) return 0;
  bdev_t *root = b;
  if (b->parent) {
    root = b->parent;
    off += b->base;
  }
  if (!root->writable) return -1;
  int rc = 0;
  uint32_t ss = root->sector;
  pthread_mutex_lock(&root->lock);
  if (!(off % ss) && !(len % ss)) {
    rc = write_full(root->fd, off, buf, len);
    if (!rc) cache_update(root, off, buf, len);
  } else {
    uint64_t a0 = off / ss * ss, a1 = (off + len + ss - 1) / ss * ss;
    size_t span = (size_t)(a1 - a0);
    uint8_t *tmp = malloc(span);
    if (!tmp) rc = -1;
    if (!rc) rc = range_out(root, a0, span);
    if (!rc) rc = read_full(root->fd, a0, tmp, span);
    if (!rc) {
      memcpy(tmp + (off - a0), buf, len);
      rc = write_full(root->fd, a0, tmp, span);
    }
    if (!rc) cache_update(root, a0, tmp, span);
    free(tmp);
  }
  pthread_mutex_unlock(&root->lock);
  return rc;
}

/* (app) Every dirty block to the device, in the order of the disk; caller holds the lock. 0 or -1 (the rest stays dirty). */
static int cache_out(bdev_t *root) {
  int rc = 0;
  for (;;) {
    cblock_t *next = NULL;
    for (int i = 0; i < CACHE_BLOCKS; i++) {
      cblock_t *c = &root->cache[i];
      if (c->valid && c->dirty && (!next || c->blkno < next->blkno)) next = c;
    }
    if (!next) break;
    if (block_out(root, next)) { rc = -1; break; }
  }
  return rc;
}

/* (app) What the cache holds to the device, in order, without asking the device to make it durable: an ordering point. */
int bdev_writeout(bdev_t *b) {
  bdev_t *root = b->parent ? b->parent : b;
  if (!root->writable) return 0;
  pthread_mutex_lock(&root->lock);
  int rc = cache_out(root);
  pthread_mutex_unlock(&root->lock);
  return rc;
}

int bdev_flush(bdev_t *b) {
  bdev_t *root = b->parent ? b->parent : b;
  if (!root->writable) return 0;
  pthread_mutex_lock(&root->lock);
  int rc = cache_out(root);                       /* (app) what the cache holds first */
  pthread_mutex_unlock(&root->lock);
  if (rc) return -1;
  return fsync(root->fd) ? -1 : 0;
}

/* (app, 23.11.2026) fsync on a raw disk does not empty the stick's own write cache: ask the disk to, once, when the drive is ejected
 * (not at every sync: some USB bridges are slow or stuck on the command). A stick that does not know it answers with an error, which
 * is no reason to call the eject failed. */
void bdev_flush_cache(bdev_t *b) {
  bdev_t *root = b->parent ? b->parent : b;
  if (!root->writable) return;
#if defined(__FreeBSD__) || defined(__PROSPERO__)
  (void)ioctl(root->fd, DIOCGFLUSH);
#endif
}

uint64_t bdev_size(const bdev_t *b) { return b->size; }
uint32_t bdev_sector_size(const bdev_t *b) { return b->sector; }
const char *bdev_path(const bdev_t *b) { return b->path; }
