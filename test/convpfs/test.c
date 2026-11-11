/* Host test of src/conv_pfs.c (.ffpfsc images): writing, the full and the quick check, damage.
 * Usage: test <workdir>   (built with -DQUICK_BLOCKS=64 so that a few hundred blocks are more than the quick check samples) */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "conv_pfs.h"

#ifndef QUICK_BLOCKS
#define QUICK_BLOCKS 2048u
#endif
#define BS 0x10000u
#define BASE (6ull * BS)

static int g_fail, g_checks;
#define CHECK(c) do { g_checks++; if(!(c)) { g_fail++; printf("FAIL line %d: %s\n", __LINE__, #c); } } while(0)

/* The payload: block i is, by i % 8: 0-2 text-like (compresses), 3 random 4 KiB pieces at 0, 28 and 56 KiB and zeros everywhere else
   (compresses well; a pre-filter that judged by those three pieces took it for incompressible, which is why there is none),
   4-5 random (does not), 6 random in front and zeros behind, 7 zeros. 520 blocks, the last one 1000 bytes short. */
#define NBLOCKS 520u
#define RAW_SIZE ((uint64_t)NBLOCKS * BS - 1000)

static void
fill_block(uint8_t *p, unsigned idx) {
  static uint64_t seed;
  seed = 1469598103934665603ull ^ idx;
  unsigned kind = idx % 8;
  for(unsigned i = 0; i < BS; i++) {
    uint8_t v;
    if(kind <= 2) v = (uint8_t)("the quick brown fox jumps over the lazy dog "[(i + idx) % 44]);
    else if(kind == 3) {
      int in_piece = (i < 4096) || (i >= 0x7000 && i < 0x7000 + 4096) || (i >= 0xE000 && i < 0xE000 + 4096);
      if(in_piece) { seed = seed * 6364136223846793005ull + 1442695040888963407ull; v = (uint8_t)(seed >> 33); } else v = 0;
    }
    else if(kind <= 5) { seed = seed * 6364136223846793005ull + 1442695040888963407ull; v = (uint8_t)(seed >> 33); }
    else if(kind == 6) { if(i < BS / 2) { seed = seed * 6364136223846793005ull + 1442695040888963407ull; v = (uint8_t)(seed >> 33); } else v = 0; }
    else v = 0;
    p[i] = v;
  }
}

typedef struct { uint64_t pos; uint8_t blk[BS]; unsigned have_idx; } src_t;

static ssize_t
src_read(void *ctx, void *buf, size_t len) {
  src_t *s = ctx;
  uint8_t *out = buf;
  size_t n = 0;
  static uint8_t tmp[BS];
  while(n < len && s->pos < RAW_SIZE) {
    unsigned idx = (unsigned)(s->pos / BS), off = (unsigned)(s->pos % BS);
    fill_block(tmp, idx);
    size_t take = BS - off;
    if(take > len - n) take = len - n;
    if(take > RAW_SIZE - s->pos) take = (size_t)(RAW_SIZE - s->pos);
    memcpy(out + n, tmp + off, take);
    n += take;
    s->pos += take;
  }
  return (ssize_t)n;
}

static uint64_t g_progress;
static void progress(void *ctx, uint64_t done) { (void)ctx; g_progress = done; }

static int
write_image(const char *path, uint64_t *size, uint32_t *crc) {
  int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0666);
  if(fd < 0) return -1;
  src_t *s = calloc(1, sizeof(*s));
  pfsc_opts_t o = { 6, 3, NULL, progress, NULL, crc };
  char err[300] = "";
  int rc = pfs_write_single(fd, "image.exfat", RAW_SIZE, src_read, s, &o, size, err, sizeof(err));
  if(rc) printf("write failed: %s\n", err);
  free(s);
  close(fd);
  return rc;
}

static int
verify(const char *path, int quick, uint32_t crc, uint8_t sha[32], char *err, size_t en) {
  int fd = open(path, O_RDONLY);
  if(fd < 0) return -1;
  pfsc_check_t c = { crc, NULL, progress, NULL, 0, quick };
  int rc = pfs_verify_full(fd, &c, sha, err, en);
  close(fd);
  return rc;
}

static uint64_t
get64(const uint8_t *p) { uint64_t v = 0; for(int i = 7; i >= 0; i--) v = v << 8 | p[i]; return v; }

/* where block i lies in the file and how long it is */
static void
block_at(int fd, unsigned i, uint64_t *off, uint64_t *len) {
  uint8_t e[16];
  if(pread(fd, e, 16, BASE + 0x400 + (uint64_t)i * 8) != 16) { *off = *len = 0; return; }
  *off = BASE + get64(e);
  *len = get64(e + 8) - get64(e);
}

static void
flip(int fd, uint64_t at) {
  uint8_t b;
  if(pread(fd, &b, 1, (off_t)at) != 1) return;
  b ^= 0x5A;
  if(pwrite(fd, &b, 1, (off_t)at) != 1) return;
}

int
main(int argc, char **argv) {
  if(argc < 2) { fprintf(stderr, "usage: test <workdir>\n"); return 2; }
  char a[512];
  mkdir(argv[1], 0777);
  snprintf(a, sizeof(a), "%s/image.ffpfsc", argv[1]);

  uint64_t size = 0;
  uint32_t crc = 0;
  CHECK(write_image(a, &size, &crc) == 0);

  /* which blocks were stored raw and which packed: the random ones (4, 5 of every 8) raw, every other kind packed, in particular the
     one with random pieces at 0, 28 and 56 KiB and zeros in between (3 of every 8) */
  {
    int fd0 = open(a, O_RDONLY);
    CHECK(fd0 >= 0);
    unsigned raw_ok = 0, packed_ok = 0, bad = 0;
    for(unsigned i = 0; i < NBLOCKS; i++) {
      uint64_t o2, l2;
      block_at(fd0, i, &o2, &l2);
      unsigned k = i % 8;
      if(i == NBLOCKS - 1) continue;                                /* the last one is short: no rule */
      if(k == 4 || k == 5) { if(l2 == BS) raw_ok++; else bad++; }
      else { if(l2 < BS) packed_ok++; else bad++; }
    }
    printf("blocks: %u raw as expected, %u packed as expected, %u not\n", raw_ok, packed_ok, bad);
    CHECK(bad == 0);
    CHECK(raw_ok == 2 * (NBLOCKS / 8));
    if(fd0 >= 0) close(fd0);
  }

  char err[300] = "";
  uint8_t sha[32];
  memset(sha, 0, sizeof(sha));
  CHECK(verify(a, 0, crc, sha, err, sizeof(err)) == 0);
  int nz = 0;
  for(int i = 0; i < 32; i++) nz |= sha[i];
  CHECK(nz);                                           /* the full check gives the SHA-256 */
  uint8_t sha2[32];
  memset(sha2, 0, sizeof(sha2));
  CHECK(verify(a, 1, crc, sha2, err, sizeof(err)) == 0);
  int nz2 = 0;
  for(int i = 0; i < 32; i++) nz2 |= sha2[i];
  CHECK(!nz2);                                         /* the quick check does not touch it */

  /* damage: a compressed block that the quick check samples, one it does not, a raw block, a place that must be empty */
  int fd = open(a, O_RDWR);
  CHECK(fd >= 0);
  const uint64_t ns = NBLOCKS < QUICK_BLOCKS ? NBLOCKS : QUICK_BLOCKS;
  unsigned sampled = 0, unsampled = 0;
  for(unsigned i = 0; i < NBLOCKS; i++) {
    int is_s = 0;
    for(uint64_t k = 0; k < ns; k++) if((ns == 1 ? 0 : k * (NBLOCKS - 1) / (ns - 1)) == i) { is_s = 1; break; }
    uint64_t off, len;
    block_at(fd, i, &off, &len);
    if(len == BS || i % 8 > 3) continue;               /* a compressed one: a text block */
    if(is_s && !sampled) sampled = i + 1;
    if(!is_s && !unsampled) unsampled = i + 1;
  }
  CHECK(sampled && unsampled);
  printf("damage: block %u is sampled, block %u is not (the quick check samples %llu of %u)\n", sampled - 1, unsampled - 1,
         (unsigned long long)ns, NBLOCKS);

  uint64_t off, len;
  block_at(fd, sampled - 1, &off, &len);
  flip(fd, off + len / 2);
  CHECK(verify(a, 0, crc, sha, err, sizeof(err)) != 0);
  CHECK(verify(a, 1, crc, sha2, err, sizeof(err)) != 0);
  flip(fd, off + len / 2);
  CHECK(verify(a, 1, crc, sha2, err, sizeof(err)) == 0);

  block_at(fd, unsampled - 1, &off, &len);
  flip(fd, off + len / 2);
  CHECK(verify(a, 0, crc, sha, err, sizeof(err)) != 0);           /* the full check always finds it */
  CHECK(verify(a, 1, crc, sha2, err, sizeof(err)) == 0);          /* the quick one is a sample: it misses it, by design */
  flip(fd, off + len / 2);

  block_at(fd, 4, &off, &len);                                    /* block 4 is random, stored raw */
  CHECK(len == BS);
  flip(fd, off + 100);
  CHECK(verify(a, 0, crc, sha, err, sizeof(err)) != 0);           /* the CRC of the payload */
  flip(fd, off + 100);

  flip(fd, 4ull * BS + 7);                                        /* block 4 of the PFS, reserved, must be empty */
  CHECK(verify(a, 0, crc, sha, err, sizeof(err)) != 0);
  CHECK(verify(a, 1, crc, sha2, err, sizeof(err)) != 0);
  flip(fd, 4ull * BS + 7);

  flip(fd, BASE + 0x400 + 8 * 10 + 1);                            /* a block table entry in the middle */
  CHECK(verify(a, 0, crc, sha, err, sizeof(err)) != 0);
  CHECK(verify(a, 1, crc, sha2, err, sizeof(err)) != 0);
  flip(fd, BASE + 0x400 + 8 * 10 + 1);
  CHECK(verify(a, 0, crc, sha, err, sizeof(err)) == 0);           /* and all as it was */
  CHECK(verify(a, 1, crc, sha2, err, sizeof(err)) == 0);
  close(fd);

  printf("%d checks, %d failed\n", g_checks, g_fail);
  return g_fail ? 1 : 0;
}
