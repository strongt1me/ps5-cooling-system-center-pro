/* PFS image with one PFSC-compressed file — the .ffpfsc container.
 *
 * A C port of MkPFS's single-file streaming builder (PSBrew/MkPFS, GPL-3.0,
 * https://github.com/PSBrew/MkPFS, commit 78eda0a: pfs.py,
 * build_pfs_stream_from_exfat / build_pfs_stream_single_file and the PFSC
 * encoder behind them). Same layout, same values:
 *
 *   block 0   header: version 2 (PS5), magic 20130315, case-insensitive,
 *             64 KiB blocks, four inodes, the inode-block signature record
 *   block 1   inode table, four unsigned 0xA8-byte inodes: super root,
 *             flat_path_table, uroot, the file
 *   block 2   super root directory: flat_path_table, uroot
 *   block 3   flat path table, one entry for "/<inner name>"
 *   block 4   reserved, empty (no collision resolver needed)
 *   block 5   uroot directory: ".", "..", the file
 *   block 6+  the file: a PFSC stream — 0x30-byte header, a table of block
 *             offsets at 0x400, then each 64 KiB logical block either as a
 *             zlib stream (when that is smaller) or raw
 *
 * The console's PFS driver decompresses PFSC natively; ShadowMountPlus mounts
 * the container and then the inner image (usually exFAT) inside it. Checked
 * on 29.09.2026 against MkPFS itself: it verifies these images without an
 * error, and unpacks them to the exact source files.
 *
 * Differences from MkPFS, none of them in the layout:
 *
 *   - the zlib streams come from libdeflate (MIT), which compresses a whole
 *     64 KiB block two to three times faster than zlib at the same ratio;
 *     the first build used zlib 1.3.1 and managed 80 MB/s on the console
 *   - the compression threads ask for the idle scheduling class, so the fan
 *     control and the web server always come first on the five CPUs all
 *     payloads share. A test payload with fewer rights was refused it
 *     (29.09.2026); refused threads run one fewer, keeping a CPU free, and
 *     yield after every block
 *   - input is read and output written in 1 MiB pieces, not per block
 *   - the block table is written in batches while the blocks stream, not
 *     held in memory (it is 12 MB for a 100 GB image)
 *
 * And since 03.10.2026, ideas from PS5 Game Compressor (no licence, so none
 * of its code — the ideas only):
 *
 *   - a block is stored compressed only when that saves at least 5 %, as the
 *     Game Compressor does: the console unpacks every compressed block a game
 *     reads, and for 1 % saved that is work for nothing while playing. Raw
 *     blocks are part of the format, any reader takes them
 *   - every byte of the image is written, zeros included: the PFSC header
 *     area right away, the padding at the end explicitly. The file never has
 *     a hole that the drive's file system has to fill with zeros — the Game
 *     Compressor traced failures on USB drives to exactly that (its 1.0.3)
 *   - a CRC-32 of the payload is kept while writing, and pfs_verify_full()
 *     unpacks every block afterwards and compares, instead of a sample */

#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#ifndef PS5TM_HOST_TEST
#include <sys/rtprio.h>
#include <sys/syscall.h>
#endif

#include "checkfile.h"
#include "conv_pfs.h"
#include "ioerr.h"
#include "sha256.h"
#include "third_party/libdeflate/libdeflate.h"

#define BS                 0x10000u      /* PFS block size                */
#define LBS                0x10000u      /* PFSC logical block size       */
#define PFS_VERSION_PS5    2
#define PFS_MAGIC          20130315
#define MODE_CASE_INSENS   0x8
#define INODE_SIZE         0xA8
#define INODE_COUNT        4
#define MODE_DIR           0x4000
#define MODE_FILE          0x8000
#define MODE_RX            0x16D         /* r-x for owner, group, other   */
#define FLAG_COMPRESSED    0x1
#define FLAG_READONLY      0x10
#define FLAG_INTERNAL      0x20000
#define DT_FILE            2
#define DT_DIR             3
#define DT_DOT             4
#define DT_DOTDOT          5
#define PFSC_MAGIC         0x43534650    /* "PFSC" */
#define PFSC_VERSION       6
#define PFSC_TABLE_OFF     0x400u
#define PFSC_FIRST_DATA    0x10000u
#define FIRST_FILE_BLOCK   6u
#define TABLE_BATCH        8192          /* offsets per table write       */
#define MAX_SLOTS          24
#define IO_BYTES           (1u << 20)    /* read and write in 1 MiB       */
/* The longest a compressed block may be: it must save at least 5 % of the
   logical block (3277 of 65536 bytes), else it is stored raw. */
#define MAX_COMPRESSED     (LBS - (LBS * 5 + 99) / 100)

typedef struct {
  uint16_t mode, nlink;
  uint32_t flags;
  int64_t  size, size_compressed;
  uint32_t blocks;
  int32_t  db[12], ib[5];
} inode_t;

static int g_idle_state = -1;            /* -1 unknown, 0 refused, 1 set  */


/* ------------------------------------------------------------------ helpers */

static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { put16(p, (uint16_t)v); put16(p + 2, (uint16_t)(v >> 16)); }
static void put64(uint8_t *p, uint64_t v) { put32(p, (uint32_t)v); put32(p + 4, (uint32_t)(v >> 32)); }
static uint32_t get32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint64_t get64(const uint8_t *p) { return (uint64_t)get32(p) | (uint64_t)get32(p + 4) << 32; }

static uint64_t ceil_div(uint64_t a, uint64_t b) { return (a + b - 1) / b; }

static int
pwrite_all(int fd, const void *buf, size_t len, uint64_t off) {
  const uint8_t *p = buf;
  while(len) {
    ssize_t w = pwrite(fd, p, len, (off_t)off);
    if(w < 0) { if(errno == EINTR) continue; return -1; }
    if(w == 0) { errno = EIO; return -1; }
    p += w; len -= (size_t)w; off += (uint64_t)w;
  }
  return 0;
}

static int
pread_all(int fd, void *buf, size_t len, uint64_t off) {
  uint8_t *p = buf;
  while(len) {
    ssize_t r = pread(fd, p, len, (off_t)off);
    if(r < 0) { if(errno == EINTR) continue; return -1; }
    if(r == 0) { errno = EIO; return -1; }
    p += r; len -= (size_t)r; off += (uint64_t)r;
  }
  return 0;
}

static void
inode_put(uint8_t *o, const inode_t *in, int64_t now) {
  memset(o, 0, INODE_SIZE);
  put16(o + 0x00, in->mode);
  put16(o + 0x02, in->nlink);
  put32(o + 0x04, in->flags);
  put64(o + 0x08, (uint64_t)in->size);
  put64(o + 0x10, (uint64_t)in->size_compressed);
  for(int i = 0; i < 4; i++) put64(o + 0x18 + 8 * i, (uint64_t)now);
  /* 0x38 four nanosecond fields, 0x48 uid, gid, 0x50 two unknown words: 0 */
  put32(o + 0x60, in->blocks);
  for(int i = 0; i < 12; i++) put32(o + 0x64 + 4 * i, (uint32_t)in->db[i]);
  for(int i = 0; i < 5; i++)  put32(o + 0x94 + 4 * i, (uint32_t)in->ib[i]);
}

static size_t
dirent_put(uint8_t *o, uint32_t ino, uint32_t type, const char *name) {
  size_t nl = strlen(name);
  size_t es = (nl + 17 + 7) & ~(size_t)7;
  memset(o, 0, es);
  put32(o + 0, ino);
  put32(o + 4, type);
  put32(o + 8, (uint32_t)nl);
  put32(o + 12, (uint32_t)es);
  memcpy(o + 16, name, nl);
  return es;
}

/* flat_path_table hash, case-insensitive as the image is. */
static uint32_t
fpt_hash(const char *s) {
  uint32_t h = 0;
  for(; *s; s++) {
    unsigned char c = (unsigned char)*s;
    if(c >= 'a' && c <= 'z') c = (unsigned char)(c - 32);
    h = c + 31u * h;
  }
  return h;
}

static void
header_block(uint8_t *h, uint64_t final_ndblock, int64_t now) {
  memset(h, 0, BS);
  put64(h + 0x00, PFS_VERSION_PS5);
  put64(h + 0x08, PFS_MAGIC);
  h[0x1A] = 1;
  put16(h + 0x1C, MODE_CASE_INSENS);
  put32(h + 0x20, BS);
  put64(h + 0x28, 1);                           /* nblock          */
  put64(h + 0x30, INODE_COUNT);
  put64(h + 0x38, final_ndblock);
  put64(h + 0x40, 1);                           /* inode blocks    */
  /* The inode-block signature record (a DinodeS64), unsigned form: zeroed
     signatures, db[0] pointing at the inode table in block 1. */
  uint8_t *s = h + 0x50;
  put16(s + 0x02, 1);                           /* nlink           */
  put32(s + 0x04, FLAG_READONLY);
  put64(s + 0x08, BS);
  put64(s + 0x10, BS);
  for(int i = 0; i < 4; i++) put64(s + 0x18 + 8 * i, (uint64_t)now);
  put32(s + 0x60, 1);                           /* blocks          */
  put64(s + 0x68 + 32, 1);                      /* db[0] = block 1 */
  put32(h + 0x368, 1);                          /* unsigned, clear */
}

static uint64_t
pfsc_header_size(uint64_t block_count) {
  uint64_t table = (block_count + 1) * 8;
  uint64_t cap   = PFSC_FIRST_DATA - PFSC_TABLE_OFF;
  uint64_t extra = table > cap ? ceil_div(table - cap, LBS) : 0;
  return PFSC_FIRST_DATA + extra * LBS;
}

/* The idle class: a thread in it only gets a CPU nobody else wants. Checked
   by looking it up afterwards, since the call may be refused; a thread left
   at normal priority yields after every block instead. */
static int
set_idle_priority(void) {
  int ok = 0;
#if !defined(PS5TM_HOST_TEST) && defined(SYS_rtprio_thread)
  struct rtprio rtp = { RTP_PRIO_IDLE, 0 };
  int set = (int)syscall(SYS_rtprio_thread, RTP_SET, 0, &rtp);
  struct rtprio got = { 0, 0 };
  ok = set == 0 && syscall(SYS_rtprio_thread, RTP_LOOKUP, 0, &got) == 0 &&
       got.type == RTP_PRIO_IDLE;
#endif
  __atomic_store_n(&g_idle_state, ok ? 1 : 0, __ATOMIC_RELEASE);
  return ok;
}

int
pfs_workers_idle(void) {
  return __atomic_load_n(&g_idle_state, __ATOMIC_ACQUIRE);
}


/* --------------------------------------------------------- compression pool */

enum { SLOT_FREE, SLOT_READY, SLOT_BUSY, SLOT_DONE, SLOT_FAILED };

typedef struct {
  uint8_t  raw[LBS];
  uint8_t  out[LBS];        /* a zlib block counts only when shorter */
  size_t   out_len;         /* 0: store raw                           */
  uint32_t raw_len;
  uint64_t seq;
  int      state;
} slot_t;

typedef struct {
  pthread_mutex_t mu;
  pthread_cond_t  work, done;
  slot_t         *slots;
  unsigned        nslots;
  int             level;
  int             stop;
  /* Threads the idle class refused keep one CPU free between them: all
     payloads share five, and 16 busy threads once froze even the console's
     own interface (26.09.2026). */
  int             normal_quota;
  int             normal_used;
} pool_t;

static void
compress_slot(struct libdeflate_compressor *c, slot_t *s) {
  /* Room for MAX_COMPRESSED bytes only: whatever does not fit is stored raw.
     MkPFS's own limit is LBS - 1 (a compressed block must be strictly
     smaller than the logical block, or the reader takes it for raw bytes);
     this one is 5 % lower, see the top of the file. libdeflate gives up as
     soon as the output passes it, so an incompressible block costs less. */
  s->out_len = libdeflate_zlib_compress(c, s->raw, LBS, s->out, MAX_COMPRESSED);
}

static void *
worker(void *arg) {
  pool_t *p = arg;
  int idle = set_idle_priority();

  pthread_mutex_lock(&p->mu);
  if(!idle && p->normal_used >= p->normal_quota) {
    pthread_mutex_unlock(&p->mu);             /* one CPU stays free */
    return NULL;
  }
  if(!idle) p->normal_used++;
  pthread_mutex_unlock(&p->mu);

  struct libdeflate_compressor *c = libdeflate_alloc_compressor(p->level);
  pthread_mutex_lock(&p->mu);
  for(;;) {
    slot_t *s = NULL;
    while(!p->stop) {
      for(unsigned i = 0; i < p->nslots; i++) {
        slot_t *x = &p->slots[i];
        if(x->state == SLOT_READY && (!s || x->seq < s->seq)) s = x;
      }
      if(s) break;
      pthread_cond_wait(&p->work, &p->mu);
    }
    if(!s) break;
    s->state = SLOT_BUSY;
    pthread_mutex_unlock(&p->mu);
    if(c) compress_slot(c, s);
    if(!idle) sched_yield();
    pthread_mutex_lock(&p->mu);
    s->state = c ? SLOT_DONE : SLOT_FAILED;
    pthread_cond_broadcast(&p->done);
  }
  pthread_mutex_unlock(&p->mu);
  if(c) libdeflate_free_compressor(c);
  return NULL;
}

/* The source, read ahead in 1 MiB pieces and handed out in blocks. */
typedef struct {
  conv_read_fn rd;
  void        *ctx;
  uint8_t     *buf;
  size_t       pos, len;
  uint32_t     crc;           /* of every byte handed out, in order */
} stage_t;

static int
stage_read(stage_t *st, uint8_t *dst, size_t want) {
  while(want) {
    if(st->pos == st->len) {
      ssize_t r = st->rd(st->ctx, st->buf, IO_BYTES);
      if(r < 0) return -1;
      if(r == 0) { errno = EIO; return -1; }
      st->pos = 0;
      st->len = (size_t)r;
    }
    size_t n = st->len - st->pos;
    if(n > want) n = want;
    memcpy(dst, st->buf + st->pos, n);
    /* What goes into the image, not what was read: a source that delivers
       more than raw_size must not count the rest. */
    st->crc = libdeflate_crc32(st->crc, st->buf + st->pos, n);
    st->pos += n;
    dst += n;
    want -= n;
  }
  return 0;
}

/* len zero bytes at off, from a zeroed buffer of IO_BYTES. */
static int
write_zeros(int fd, const uint8_t *zero, uint64_t off, uint64_t len) {
  while(len) {
    size_t n = len < IO_BYTES ? (size_t)len : IO_BYTES;
    if(pwrite_all(fd, zero, n, off) != 0) return -1;
    off += n;
    len -= n;
  }
  return 0;
}

/* The output, collected into 1 MiB pieces. */
typedef struct {
  int      fd;
  uint8_t *buf;
  size_t   len;
  uint64_t at;              /* file offset of buf[0] */
} sink_t;

static int
sink_flush(sink_t *k) {
  if(!k->len) return 0;
  if(pwrite_all(k->fd, k->buf, k->len, k->at) != 0) return -1;
  k->at += k->len;
  k->len = 0;
  return 0;
}

static int
sink_put(sink_t *k, const uint8_t *p, size_t n) {
  if(k->len + n > IO_BYTES && sink_flush(k) != 0) return -1;
  memcpy(k->buf + k->len, p, n);
  k->len += n;
  return 0;
}


/* -------------------------------------------------------------------- build */

int
pfs_write_single(int fd, const char *inner_name, uint64_t raw_size,
                 conv_read_fn rd, void *rd_ctx, const pfsc_opts_t *o,
                 uint64_t *image_size, char *err, size_t err_len) {
  size_t nl = inner_name ? strlen(inner_name) : 0;
  if(!nl || nl > 255) { snprintf(err, err_len, "Ungültiger innerer Name."); return -1; }
  for(size_t i = 0; i < nl; i++)
    if((unsigned char)inner_name[i] < 0x20 || (unsigned char)inner_name[i] > 0x7E ||
       inner_name[i] == '/') {
      snprintf(err, err_len, "Der innere Name muss aus ASCII-Zeichen bestehen.");
      return -1;
    }
  if(!raw_size) { snprintf(err, err_len, "Leere Quelle."); return -1; }

  int64_t now = (int64_t)time(NULL);
  inode_t in[INODE_COUNT];
  memset(in, 0, sizeof(in));
  /* super root, flat_path_table, uroot, file — block pointers as MkPFS
     lays them out: 2, 3, (4 reserved), 5, 6. */
  in[0] = (inode_t){ MODE_DIR | MODE_RX,  1, FLAG_INTERNAL | FLAG_READONLY, BS, BS, 1,
                     { 2 }, { 0 } };
  in[1] = (inode_t){ MODE_FILE | MODE_RX, 1, FLAG_INTERNAL | FLAG_READONLY, 8, 8, 1,
                     { 3, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 }, { 0 } };
  in[2] = (inode_t){ MODE_DIR | MODE_RX,  3, FLAG_READONLY, BS, BS, 1,
                     { 5, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 }, { 0 } };
  in[3] = (inode_t){ MODE_FILE | MODE_RX, 1, FLAG_READONLY, 0, 0, 1,
                     { (int32_t)FIRST_FILE_BLOCK, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
                     { 0 } };

  char fpt_path[260];
  snprintf(fpt_path, sizeof(fpt_path), "/%s", inner_name);

  int       rc = -1;
  pool_t    pool;
  pthread_t th[MAX_SLOTS];
  int       nthreads = 0, pool_live = 0;
  uint64_t *table = NULL;
  struct libdeflate_compressor *inline_c = NULL;
  stage_t   st   = { rd, rd_ctx, NULL, 0, 0, 0 };
  sink_t    sink = { fd, NULL, 0, 0 };
  memset(&pool, 0, sizeof(pool));
  uint8_t *blk = calloc(1, BS);
  st.buf   = malloc(IO_BYTES);
  sink.buf = malloc(IO_BYTES);
  if(!blk || !st.buf || !sink.buf) goto nomem;

  /* Blocks 0-5: provisional header and inodes, directories, flat path table. */
  header_block(blk, 0, now);
  if(pwrite_all(fd, blk, BS, 0) != 0) goto io;
  memset(blk, 0, BS);
  for(int i = 0; i < INODE_COUNT; i++) inode_put(blk + i * INODE_SIZE, &in[i], now);
  if(pwrite_all(fd, blk, BS, 1ull * BS) != 0) goto io;
  memset(blk, 0, BS);
  size_t off = dirent_put(blk, 1, DT_FILE, "flat_path_table");
  dirent_put(blk + off, 2, DT_DIR, "uroot");
  if(pwrite_all(fd, blk, BS, 2ull * BS) != 0) goto io;
  memset(blk, 0, BS);
  put32(blk, fpt_hash(fpt_path));
  put32(blk + 4, 3);
  if(pwrite_all(fd, blk, BS, 3ull * BS) != 0) goto io;
  memset(blk, 0, BS);
  if(pwrite_all(fd, blk, BS, 4ull * BS) != 0) goto io;
  off  = dirent_put(blk, 2, DT_DOT, ".");
  off += dirent_put(blk + off, 2, DT_DOTDOT, "..");
  dirent_put(blk + off, 3, DT_FILE, inner_name);
  if(pwrite_all(fd, blk, BS, 5ull * BS) != 0) goto io;

  /* The PFSC stream. Its header area — the 0x30-byte header, the block table,
     the padding up to the first block — is filled in at the end and in
     batches; it is written as zeros first, so that no part of it is ever a
     hole (see the top of the file). The read buffer is not in use yet. */
  uint64_t base        = (uint64_t)FIRST_FILE_BLOCK * BS;
  uint64_t block_count = ceil_div(raw_size, LBS);
  uint64_t header_size = pfsc_header_size(block_count);
  memset(st.buf, 0, IO_BYTES);
  if(write_zeros(fd, st.buf, base, header_size) != 0) goto io;
  uint64_t cur         = header_size;          /* offset of the next block */
  sink.at = base + header_size;
  table = malloc(TABLE_BATCH * sizeof(uint64_t));
  if(!table) goto nomem;
  uint64_t tbl_first = 0;                      /* table index of table[0]  */
  unsigned tbl_n     = 0;
  table[tbl_n++] = header_size;

  pool.level        = o->level;
  pool.normal_quota = o->workers > 1 ? o->workers - 1 : 1;
  pool.nslots = (unsigned)(o->workers > 0 ? o->workers * 4 : 1);
  if(pool.nslots > MAX_SLOTS) pool.nslots = MAX_SLOTS;
  pool.slots  = calloc(pool.nslots, sizeof(slot_t));
  if(!pool.slots) goto nomem;
  pthread_mutex_init(&pool.mu, NULL);
  pthread_cond_init(&pool.work, NULL);
  pthread_cond_init(&pool.done, NULL);
  pool_live = 1;
  for(int i = 0; i < o->workers && i < MAX_SLOTS; i++) {
    if(pthread_create(&th[nthreads], NULL, worker, &pool) != 0) break;
    nthreads++;
  }
  if(!nthreads && !(inline_c = libdeflate_alloc_compressor(o->level))) goto nomem;

  uint64_t next_fill = 0, next_write = 0, raw_done = 0;
  while(next_write < block_count) {
    while(next_fill < block_count && next_fill - next_write < pool.nslots) {
      /* Stored from another thread, with an atomic store: loaded atomically. */
      if(o->cancel && __atomic_load_n(o->cancel, __ATOMIC_ACQUIRE)) { errno = ECANCELED; goto io; }
      slot_t  *s    = &pool.slots[next_fill % pool.nslots];
      uint64_t left = raw_size - next_fill * LBS;
      uint32_t want = left < LBS ? (uint32_t)left : LBS;
      if(stage_read(&st, s->raw, want) != 0) {
        snprintf(err, err_len, "Lesefehler in der Quelle: %s", ps5tm_io_strerror(errno));
        goto fail;
      }
      if(want < LBS) memset(s->raw + want, 0, LBS - want);
      s->raw_len = want;
      s->seq     = next_fill;
      if(nthreads) {
        pthread_mutex_lock(&pool.mu);
        s->state = SLOT_READY;
        pthread_cond_signal(&pool.work);
        pthread_mutex_unlock(&pool.mu);
      } else {
        compress_slot(inline_c, s);
        s->state = SLOT_DONE;
      }
      next_fill++;
    }

    slot_t *s = &pool.slots[next_write % pool.nslots];
    if(nthreads) {
      pthread_mutex_lock(&pool.mu);
      while(s->state != SLOT_DONE && s->state != SLOT_FAILED)
        pthread_cond_wait(&pool.done, &pool.mu);
      pthread_mutex_unlock(&pool.mu);
    }
    if(s->state == SLOT_FAILED) {
      snprintf(err, err_len, "Die Kompression ist gescheitert (kein Speicher).");
      goto fail;
    }
    const uint8_t *data = s->out_len ? s->out : s->raw;
    size_t         dlen = s->out_len ? s->out_len : LBS;
    if(sink_put(&sink, data, dlen) != 0) goto io;
    cur += dlen;
    table[tbl_n++] = cur;
    if(tbl_n == TABLE_BATCH) {
      if(pwrite_all(fd, table, (size_t)tbl_n * 8,
                    base + PFSC_TABLE_OFF + tbl_first * 8) != 0) goto io;
      tbl_first += tbl_n;
      tbl_n = 0;
    }
    raw_done += s->raw_len;
    if(o->progress) o->progress(o->progress_ctx, raw_done);
    pthread_mutex_lock(&pool.mu);
    s->state = SLOT_FREE;
    pthread_mutex_unlock(&pool.mu);
    next_write++;
  }
  if(sink_flush(&sink) != 0) goto io;
  if(tbl_n && pwrite_all(fd, table, (size_t)tbl_n * 8,
                         base + PFSC_TABLE_OFF + tbl_first * 8) != 0) goto io;

  /* PFSC header, then the final inodes and header block. */
  memset(blk, 0, 0x30);
  put32(blk + 0x00, PFSC_MAGIC);
  put32(blk + 0x04, 0);
  put32(blk + 0x08, PFSC_VERSION);
  put32(blk + 0x0C, LBS);
  put64(blk + 0x10, LBS);
  put64(blk + 0x18, PFSC_TABLE_OFF);
  put64(blk + 0x20, header_size);
  put64(blk + 0x28, block_count * LBS);
  if(pwrite_all(fd, blk, 0x30, base) != 0) goto io;

  uint64_t stored = cur;
  in[3].blocks          = (uint32_t)ceil_div(stored, BS);
  in[3].size            = (int64_t)stored;
  in[3].flags           = FLAG_READONLY | FLAG_COMPRESSED;
  in[3].size_compressed = (int64_t)raw_size;
  uint64_t final_ndblock = FIRST_FILE_BLOCK + in[3].blocks;
  if(final_ndblock > 0x7FFFFFFFull) { snprintf(err, err_len, "Das Abbild wird zu groß."); goto fail; }

  /* The rest of the last block, written as zeros instead of left to
     ftruncate (less than one block). */
  memset(blk, 0, BS);
  if(final_ndblock * BS > base + stored &&
     pwrite_all(fd, blk, (size_t)(final_ndblock * BS - (base + stored)), base + stored) != 0)
    goto io;

  header_block(blk, final_ndblock, now);
  if(pwrite_all(fd, blk, BS, 0) != 0) goto io;
  memset(blk, 0, BS);
  for(int i = 0; i < INODE_COUNT; i++) inode_put(blk + i * INODE_SIZE, &in[i], now);
  if(pwrite_all(fd, blk, BS, 1ull * BS) != 0) goto io;
  if(ftruncate(fd, (off_t)(final_ndblock * BS)) != 0 || fsync(fd) != 0) goto io;

  if(image_size) *image_size = final_ndblock * BS;
  if(o->raw_crc) *o->raw_crc = st.crc;
  rc = 0;
  goto out;

nomem:
  snprintf(err, err_len, "Kein Speicher für die Kompression.");
  goto fail;
io:
  if(errno == ECANCELED) snprintf(err, err_len, "Abgebrochen.");
  else snprintf(err, err_len, "Schreibfehler: %s", ps5tm_io_strerror(errno));
fail:
  rc = -1;
out:
  if(pool_live) {
    int saved = errno;
    pthread_mutex_lock(&pool.mu);
    pool.stop = 1;
    pthread_cond_broadcast(&pool.work);
    pthread_mutex_unlock(&pool.mu);
    for(int i = 0; i < nthreads; i++) pthread_join(th[i], NULL);
    pthread_cond_destroy(&pool.work);
    pthread_cond_destroy(&pool.done);
    pthread_mutex_destroy(&pool.mu);
    errno = saved;
  }
  if(inline_c) libdeflate_free_compressor(inline_c);
  free(pool.slots);
  free(table);
  free(sink.buf);
  free(st.buf);
  free(blk);
  return rc;
}


/* ------------------------------------------------------------------- verify */

/* Every byte of [a, b) that falls inside the piece p = [off, off + n) is 0. */
static int
zero_within(const uint8_t *p, uint64_t off, size_t n, uint64_t a, uint64_t b) {
  uint64_t s = a > off ? a : off, e = b < off + n ? b : off + n;
  for(uint64_t i = s; i < e; i++)
    if(p[i - off]) return 0;
  return 1;
}

/* The blocks as the sequential read meets them. */
typedef struct {
  int       fd;
  uint64_t  base, block_count, raw_size;
  uint8_t  *tb;               /* TABLE_BATCH entries, from entry tb_first */
  uint64_t  tb_first, tb_n;
  uint64_t  next;             /* the block being put together              */
  uint64_t  b_start, b_end;   /* its place in the file                     */
  size_t    have;             /* how much of it is in bbuf                 */
  uint8_t  *bbuf, *raw;
  struct libdeflate_decompressor *d;
  uint32_t  crc;
} vblocks_t;

static int
table_at(vblocks_t *v, uint64_t i, uint64_t *val) {
  if(i < v->tb_first || i >= v->tb_first + v->tb_n) {
    uint64_t entries = v->block_count + 1;
    uint64_t n = entries - i < TABLE_BATCH ? entries - i : TABLE_BATCH;
    if(pread_all(v->fd, v->tb, (size_t)n * 8, v->base + PFSC_TABLE_OFF + i * 8) != 0)
      return -1;
    v->tb_first = i;
    v->tb_n = n;
  }
  *val = get64(v->tb + (i - v->tb_first) * 8);
  return 0;
}

/* Where block next lies; the table was checked before, so it rises. */
static int
block_bounds(vblocks_t *v) {
  uint64_t a, b;
  if(table_at(v, v->next, &a) != 0 || table_at(v, v->next + 1, &b) != 0) return -1;
  v->b_start = v->base + a;
  v->b_end   = v->base + b;
  v->have    = 0;
  return 0;
}

/* Feeds the part of the piece [off, off + n) that holds block data; every
   block completed is unpacked and goes into the CRC. 0, or -1 with the block
   that failed in v->next. */
static int
feed_blocks(vblocks_t *v, const uint8_t *p, uint64_t off, size_t n) {
  uint64_t end = off + n;
  while(v->next < v->block_count && v->b_start < end) {
    uint64_t from = v->b_start + v->have;
    if(from < off) return -1;                    /* cannot happen: in order */
    uint64_t upto = v->b_end < end ? v->b_end : end;
    if(upto > from) {
      memcpy(v->bbuf + v->have, p + (from - off), (size_t)(upto - from));
      v->have += (size_t)(upto - from);
    }
    if(v->b_start + v->have < v->b_end) return 0; /* continues in the next piece */

    size_t len = (size_t)(v->b_end - v->b_start);
    const uint8_t *raw = v->bbuf;
    if(len != LBS) {                             /* stored raw at exactly LBS */
      size_t got = 0;
      if(libdeflate_zlib_decompress(v->d, v->bbuf, len, v->raw, LBS, &got) !=
             LIBDEFLATE_SUCCESS || got != LBS)
        return -1;
      raw = v->raw;
    }
    uint64_t left = v->raw_size - v->next * LBS;
    v->crc = libdeflate_crc32(v->crc, raw, left < LBS ? (size_t)left : LBS);
    v->next++;
    if(v->next < v->block_count && block_bounds(v) != 0) return -1;
  }
  return 0;
}

int
pfs_verify_full(int fd, const pfsc_check_t *c, uint8_t sha[32], char *err, size_t err_len) {
  uint8_t *h = malloc(BS), *buf = malloc(IO_BYTES);
  vblocks_t v;
  memset(&v, 0, sizeof(v));
  v.fd   = fd;
  v.tb   = malloc((size_t)TABLE_BATCH * 8);
  v.bbuf = malloc(LBS);
  v.raw  = malloc(LBS);
  v.d    = libdeflate_alloc_decompressor();
  int rc = -1;
  struct stat st;
  if(!h || !buf || !v.tb || !v.bbuf || !v.raw || !v.d) { snprintf(err, err_len, "Kein Speicher."); goto out; }
  /* From the drive, not from what the system still holds in memory. */
  ps5tm_drop_cache(fd);
  if(fstat(fd, &st) != 0 || pread_all(fd, h, BS, 0) != 0) {
    snprintf(err, err_len, "Das Abbild ist nicht lesbar.");
    goto out;
  }
  if(get64(h) != PFS_VERSION_PS5 || get64(h + 8) != PFS_MAGIC || get32(h + 0x20) != BS ||
     get64(h + 0x30) != INODE_COUNT || get64(h + 0x38) * BS != (uint64_t)st.st_size) {
    snprintf(err, err_len, "Der PFS-Kopf stimmt nicht.");
    goto out;
  }
  if(pread_all(fd, h, BS, BS) != 0) { snprintf(err, err_len, "Inodes nicht lesbar."); goto out; }
  const uint8_t *fi = h + 3 * INODE_SIZE;
  uint64_t stored = get64(fi + 0x08), raw_size = get64(fi + 0x10);
  if(!(get32(fi + 0x04) & FLAG_COMPRESSED) || get32(fi + 0x64) != FIRST_FILE_BLOCK || !raw_size ||
     (FIRST_FILE_BLOCK + ceil_div(stored, BS)) * BS != (uint64_t)st.st_size) {
    snprintf(err, err_len, "Die Datei-Inode stimmt nicht.");
    goto out;
  }
  uint64_t base = (uint64_t)FIRST_FILE_BLOCK * BS;
  uint64_t block_count = ceil_div(raw_size, LBS);
  uint64_t header_size = pfsc_header_size(block_count);
  if(pread_all(fd, h, 0x30, base) != 0 || get32(h) != PFSC_MAGIC ||
     get32(h + 8) != PFSC_VERSION || get32(h + 0x0C) != LBS ||
     get64(h + 0x18) != PFSC_TABLE_OFF || get64(h + 0x20) != header_size ||
     get64(h + 0x28) != block_count * LBS) {
    snprintf(err, err_len, "Der PFSC-Kopf stimmt nicht.");
    goto out;
  }
  v.base = base;
  v.block_count = block_count;
  v.raw_size = raw_size;

  /* The whole block table first, not only its two ends: it is written in
     batches while the blocks stream, so a wrong entry in the middle would
     leave both ends right. Offsets must rise, no block may be longer than a
     logical block, none may leave the stored stream, and the last one ends
     exactly where the stream does. (v.tb serves as the buffer; the reads
     below start from an empty window and reload it.) */
  uint64_t entries = block_count + 1, idx = 0, prev = 0;
  while(idx < entries) {
    size_t n = entries - idx < TABLE_BATCH ? (size_t)(entries - idx) : TABLE_BATCH;
    if(pread_all(fd, v.tb, n * 8, base + PFSC_TABLE_OFF + idx * 8) != 0) {
      snprintf(err, err_len, "Die Blocktabelle ist nicht lesbar.");
      goto out;
    }
    for(size_t k = 0; k < n; k++) {
      uint64_t cur = get64(v.tb + k * 8);
      int fine = idx + k == 0 ? cur == header_size : cur > prev && cur - prev <= LBS && cur <= stored;
      if(!fine) {
        snprintf(err, err_len, "Die Blocktabelle stimmt nicht (Eintrag %llu).", (unsigned long long)(idx + k));
        goto out;
      }
      prev = cur;
    }
    idx += n;
  }
  if(prev != stored) {
    snprintf(err, err_len, "Die Blocktabelle endet nicht dort, wo der Datenstrom endet.");
    goto out;
  }

  /* Then the file once, in order, from the drive: every byte goes into the
     SHA-256, the places that must be empty are looked at, and every block is
     unpacked and counted into the CRC-32 of the payload. */
  ps5tm_sha256_t sh;
  ps5tm_sha256_init(&sh);
  uint64_t fsize = (uint64_t)st.st_size, pos = 0;
  if(block_bounds(&v) != 0) { snprintf(err, err_len, "Die Blocktabelle ist nicht lesbar."); goto out; }
  while(pos < fsize) {
    if(c && c->cancel && __atomic_load_n(c->cancel, __ATOMIC_ACQUIRE)) {
      snprintf(err, err_len, "Abgebrochen.");
      errno = ECANCELED;
      goto out;
    }
    size_t n = fsize - pos < IO_BYTES ? (size_t)(fsize - pos) : IO_BYTES;
    if(pread_all(fd, buf, n, pos) != 0) {
      snprintf(err, err_len, "Lesefehler beim Prüfen: %s", ps5tm_io_strerror(errno));
      goto out;
    }
    ps5tm_sha256_update(&sh, buf, n);
    if(!zero_within(buf, pos, n, 4ull * BS, 5ull * BS) ||                              /* block 4, reserved */
       !zero_within(buf, pos, n, base + 0x30, base + PFSC_TABLE_OFF) ||                /* behind the header */
       !zero_within(buf, pos, n, base + PFSC_TABLE_OFF + entries * 8, base + header_size) ||  /* behind the table */
       !zero_within(buf, pos, n, base + stored, fsize)) {                              /* the end */
      snprintf(err, err_len, "Ein Bereich des Abbilds, der leer sein muss, enthält Daten.");
      goto out;
    }
    if(feed_blocks(&v, buf, pos, n) != 0) {
      snprintf(err, err_len, "Block %llu lässt sich nicht entpacken.", (unsigned long long)v.next);
      goto out;
    }
    pos += n;
    if(c && c->progress) c->progress(c->progress_ctx, pos);
    sched_yield();                  /* the fan and the web server come first */
  }
  if(v.next != block_count) {
    snprintf(err, err_len, "Dem Abbild fehlen Blöcke (%llu von %llu).",
             (unsigned long long)v.next, (unsigned long long)block_count);
    goto out;
  }
  if(c && v.crc != c->raw_crc) {
    snprintf(err, err_len, "Die Daten im Abbild stimmen nicht mit denen der Quelle überein "
             "(Prüfsumme %08X statt %08X).", (unsigned)v.crc, (unsigned)c->raw_crc);
    goto out;
  }
  if(sha) ps5tm_sha256_final(&sh, sha);
  rc = 0;
out:
  if(v.d) libdeflate_free_decompressor(v.d);
  free(h);
  free(buf);
  free(v.tb);
  free(v.bbuf);
  free(v.raw);
  return rc;
}
