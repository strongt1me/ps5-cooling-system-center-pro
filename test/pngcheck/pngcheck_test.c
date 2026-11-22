/* Host test of src/pngcheck.h (22.11.2026): a whole PNG passes, a damaged one is named, an unreadable one gets no verdict.
 * Every further argument is a real PNG file (the run passes the pictures of the repository): each must pass. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pngcheck.h"

static int fails;
#define CHECK(c, ...) do { if(!(c)) { fails++; printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while(0)

typedef struct { const uint8_t *p; size_t n; int fail_at; } mem_t;     /* fail_at >= 0: a read that reaches this offset fails */

static long
mem_read(void *ctx, uint64_t off, void *buf, size_t n) {
  const mem_t *m = ctx;
  if(m->fail_at >= 0 && off + n > (uint64_t)m->fail_at) return -1;
  if(off >= m->n) return 0;
  size_t take = m->n - (size_t)off < n ? m->n - (size_t)off : n;
  if(take > 1000) take = 1000;                                   /* short reads, as a file system may give them */
  memcpy(buf, m->p + off, take);
  return (long)take;
}

/* A reader that counts its calls (the costly ones) and, like imgr_pread, gives as much as asked up to the end of the file. */
typedef struct { mem_t m; unsigned long calls; } counted_t;

static long
counted_read(void *ctx, uint64_t off, void *buf, size_t n) {
  counted_t *c = ctx;
  c->calls++;
  if(c->m.fail_at >= 0 && off + n > (uint64_t)c->m.fail_at) return -1;
  if(off >= c->m.n) return 0;
  const size_t take = c->m.n - (size_t)off < n ? c->m.n - (size_t)off : n;
  memcpy(buf, c->m.p + off, take);
  return (long)take;
}

static int
check_mem(const uint8_t *p, size_t n) {
  mem_t m = { p, n, -1 };
  return ps5tm_pngcheck(mem_read, &m);
}

/* A reference CRC-32 without a table, bit by bit: not the code under test. */
static uint32_t
ref_crc(const uint8_t *p, size_t n) {
  uint32_t c = 0xFFFFFFFFu;
  for(size_t i = 0; i < n; i++) {
    c ^= p[i];
    for(int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
  }
  return c ^ 0xFFFFFFFFu;
}

static size_t
put_chunk(uint8_t *out, const char *type, const uint8_t *data, uint32_t len) {
  out[0] = (uint8_t)(len >> 24); out[1] = (uint8_t)(len >> 16); out[2] = (uint8_t)(len >> 8); out[3] = (uint8_t)len;
  memcpy(out + 4, type, 4);
  if(len) memcpy(out + 8, data, len);
  uint32_t c = ref_crc(out + 4, 4 + (size_t)len);
  out[8 + len] = (uint8_t)(c >> 24); out[9 + len] = (uint8_t)(c >> 16); out[10 + len] = (uint8_t)(c >> 8); out[11 + len] = (uint8_t)c;
  return 12 + (size_t)len;
}

static long
file_read(void *ctx, uint64_t off, void *buf, size_t n) {
  FILE *f = ctx;
  if(fseek(f, (long)off, SEEK_SET) != 0) return -1;
  size_t r = fread(buf, 1, n, f);
  return ferror(f) ? -1 : (long)r;
}

int
main(int argc, char **argv) {
  /* the well-known 1x1 PNG of a real encoder (67 bytes) */
  static const uint8_t one[67] = {
    0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
    0x08, 0x06, 0x00, 0x00, 0x00, 0x1F, 0x15, 0xC4, 0x89, 0x00, 0x00, 0x00, 0x0A, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0x00, 0x01, 0x00, 0x00,
    0x05, 0x00, 0x01, 0x0D, 0x0A, 0x2D, 0xB4, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82 };
  CHECK(check_mem(one, sizeof(one)) == 0, "a whole 1x1 PNG");

  uint8_t buf[200];
  memcpy(buf, one, sizeof(one));
  memset(buf + sizeof(one), 0xAB, 40);
  CHECK(check_mem(buf, sizeof(one) + 40) == 0, "bytes after IEND are allowed");

  for(size_t cut = 0; cut < sizeof(one); cut++)
    if(check_mem(one, cut) != 1) { CHECK(0, "cut to %zu bytes must be damaged", cut); break; }

  memcpy(buf, one, sizeof(one)); buf[1] = 'Q';
  CHECK(check_mem(buf, sizeof(one)) == 1, "a wrong signature");
  memcpy(buf, one, sizeof(one)); buf[44] ^= 0x01;                 /* a byte of the IDAT data */
  CHECK(check_mem(buf, sizeof(one)) == 1, "a changed data byte (CRC)");
  memcpy(buf, one, sizeof(one)); buf[32] ^= 0x80;                 /* the stored CRC of IHDR */
  CHECK(check_mem(buf, sizeof(one)) == 1, "a changed CRC");
  memcpy(buf, one, sizeof(one)); buf[11] = 14;                    /* IHDR with 14 bytes */
  CHECK(check_mem(buf, sizeof(one)) == 1, "IHDR of the wrong length");
  memcpy(buf, one, sizeof(one)); buf[8] = 0x80;                   /* a length over the format's limit */
  CHECK(check_mem(buf, sizeof(one)) == 1, "a length over 2^31-1");
  memcpy(buf, one, sizeof(one)); buf[38] = '1';                   /* a type that is not four letters */
  CHECK(check_mem(buf, sizeof(one)) == 1, "a chunk type with a digit");
  memcpy(buf, one, sizeof(one)); buf[33] = 0x10;                  /* IDAT length 0x1000000A: far past 64 MiB, in a 67-byte file */
  CHECK(check_mem(buf, sizeof(one)) == 1, "a damaged length in a small file is damage, not 'too large to judge'");

  /* random data, as the 532-byte pic2.png of fpkg-cli's notes; and the same behind a good signature */
  uint8_t rnd[532];
  srand(7);
  for(size_t i = 0; i < sizeof(rnd); i++) rnd[i] = (uint8_t)rand();
  CHECK(check_mem(rnd, sizeof(rnd)) == 1, "random bytes");
  memcpy(rnd, one, 8);
  CHECK(check_mem(rnd, sizeof(rnd)) == 1, "random bytes behind a PNG signature");
  CHECK(check_mem(rnd, 0) == 1, "an empty file");

  /* a larger picture: IHDR, an IDAT of 200 000 bytes (read in several pieces), a tEXt, IEND */
  const size_t big = 8 + 25 + (12 + 200000) + (12 + 10) + 12;
  uint8_t *p = malloc(big);
  uint8_t *data = malloc(200000);
  if(!p || !data) { printf("no memory\n"); return 2; }
  for(size_t i = 0; i < 200000; i++) data[i] = (uint8_t)(i * 31 + 7);
  size_t o = 8;
  memcpy(p, one, 8);
  o += put_chunk(p + o, "IHDR", one + 16, 13);
  o += put_chunk(p + o, "IDAT", data, 200000);
  o += put_chunk(p + o, "tEXt", (const uint8_t *)"Comment\0ok", 10);
  o += put_chunk(p + o, "IEND", NULL, 0);
  CHECK(o == big, "built %zu of %zu bytes", o, big);
  CHECK(check_mem(p, big) == 0, "a whole larger PNG");
  p[8 + 25 + 8 + 150000] ^= 0x10;
  CHECK(check_mem(p, big) == 1, "a changed byte deep inside a large IDAT");
  p[8 + 25 + 8 + 150000] ^= 0x10;
  CHECK(check_mem(p, big - 12) == 1, "IEND missing");
  {
    mem_t m = { p, big, 100000 };
    CHECK(ps5tm_pngcheck(mem_read, &m) == -1, "a read error: no verdict");
  }
  free(data);
  free(p);

  /* the window (review of 22.11.2026): 100 000 empty chunks behind IHDR, as a crafted picture in an image could be; every call of the
     slow reader (imgread looks the path up each time) is counted */
  {
    const size_t nchunks = 100000, sz = 8 + 25 + nchunks * 12 + 12;
    uint8_t *q = malloc(sz);
    uint8_t *win = malloc(65536);
    if(!q || !win) { printf("no memory\n"); return 2; }
    size_t o2 = 8;
    memcpy(q, one, 8);
    o2 += put_chunk(q + o2, "IHDR", one + 16, 13);
    for(size_t i = 0; i < nchunks; i++) o2 += put_chunk(q + o2, "tEXt", NULL, 0);
    o2 += put_chunk(q + o2, "IEND", NULL, 0);
    CHECK(o2 == sz, "built %zu of %zu bytes", o2, sz);
    counted_t c = { { q, sz, -1 }, 0 };
    CHECK(ps5tm_pngcheck(counted_read, &c) == 0, "100 000 empty chunks read directly");
    const unsigned long direct = c.calls;
    c.calls = 0;
    pngcheck_window_t w = { counted_read, &c, win, 65536, 0, 0, 0 };
    CHECK(ps5tm_pngcheck(pngcheck_window_read, &w) == 0, "100 000 empty chunks through the window");
    CHECK(c.calls < 64, "through the window: %lu calls of the slow reader (directly: %lu)", c.calls, direct);
    CHECK(direct > 200000, "directly: %lu calls (the case the window is for)", direct);
    q[sz / 2] ^= 0x01;                                           /* a damaged byte in the middle */
    c.calls = 0;
    pngcheck_window_t w2 = { counted_read, &c, win, 65536, 0, 0, 0 };
    CHECK(ps5tm_pngcheck(pngcheck_window_read, &w2) == 1, "damage found through the window");
    q[sz / 2] ^= 0x01;
    c.m.fail_at = (int)(sz / 3);
    pngcheck_window_t w3 = { counted_read, &c, win, 65536, 0, 0, 0 };
    CHECK(ps5tm_pngcheck(pngcheck_window_read, &w3) == -1, "a read error through the window: no verdict");
    c.calls = 0;
    pngcheck_window_t w3b = { counted_read, &c, win, 65536, 0, 0, 0 };
    CHECK(ps5tm_pngcheck(pngcheck_window_read, &w3b) == -1 && c.calls <= 2 * (PNGCHECK_WINDOW_MISSES + 1) + 40,
          "an unreadable spot costs a bounded number of calls (%lu)", c.calls);
    /* damage before an unreadable spot is still found through the window: the refill fails, the bytes asked for read */
    {
      uint8_t d[67];
      memcpy(d, one, sizeof(one));
      d[44] ^= 0x01;                                             /* a changed IDAT byte: its CRC no longer fits */
      counted_t cd = { { d, sizeof(d), 64 }, 0 };                /* the bytes from 64 on cannot be read */
      pngcheck_window_t wd = { counted_read, &cd, win, 65536, 0, 0, 0 };
      CHECK(ps5tm_pngcheck(pngcheck_window_read, &wd) == 1, "damage found although a refill fails");
    }
    free(win);
    free(q);
  }

  /* the real pictures given on the command line */
  int real = 0;
  for(int i = 1; i < argc; i++) {
    FILE *f = fopen(argv[i], "rb");
    if(!f) { CHECK(0, "cannot open %s", argv[i]); continue; }
    const int r = ps5tm_pngcheck(file_read, f);
    fclose(f);
    CHECK(r == 0, "real picture %s: %d", argv[i], r);
    real++;
  }

  printf("pngcheck: %d checks of real pictures, %s\n", real, fails ? "FAILED" : "all passed");
  return fails ? 1 : 0;
}
