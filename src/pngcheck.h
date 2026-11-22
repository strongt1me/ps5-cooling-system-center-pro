/* PS5 Cooling & System Center - Pro: is a PNG of a game's sce_sys whole? (pngcheck.h, 22.11.2026). No state, so a host test can try it (test/pngcheck).
 *
 * Dumps can carry damaged artwork: fpkg-cli 0.6.8 (a front end of LibProsperoPkg, read for its notes only, no licence) met a pic2.png of 532 bytes of
 * random data next to a good pic2.dds. The app copies a game's files as they are, so the check before converting names such a file (gameconvert.c).
 * The file is read through a callback (a folder: pread on a descriptor; an image: imgread.c), in pieces of 64 KiB, never whole.
 *
 * Whole means: the 8-byte signature, IHDR as the first chunk with 13 bytes, every chunk with a length the file has room for, a type of four letters and
 * a CRC-32 over type and data that matches, up to IEND. Bytes after IEND are allowed (fpkg-cli refuses them; a viewer does not mind them, and a warning
 * that is wrong is worse than none). A file larger than PNGCHECK_MAX_BYTES is not judged. */
#ifndef PS5TM_PNGCHECK_H
#define PS5TM_PNGCHECK_H

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define PNGCHECK_MAX_BYTES  (64ull << 20)      /* the home-screen pictures are a few MB; anything larger is left unjudged */
#define PNGCHECK_MAX_CHUNKS (1u << 20)

/* Reads up to n bytes from offset off: the bytes read (0 at the end of the file), -1 on a read error. */
typedef long (*pngcheck_read_fn)(void *ctx, uint64_t off, void *buf, size_t n);

/* A window over a reader whose every call is costly (imgread.c looks the path up again on each one): pngcheck reads chunk by chunk, so a
   picture of a million empty chunks would mean millions of calls (review of 22.11.2026). Through the window it is one call per cap bytes.
   buf holds cap bytes; at and len start at 0. Reads must go forward, as pngcheck's do. */
typedef struct {
  pngcheck_read_fn rd;
  void            *ctx;
  uint8_t         *buf;
  size_t           cap;
  uint64_t         at;     /* file offset of buf[0] */
  size_t           len;    /* bytes held */
  unsigned         misses; /* refills that failed (start at 0) */
} pngcheck_window_t;

/* A refill reads cap bytes and fails as a whole when any part of them cannot be read; the bytes asked for may still read. That is tried
   this often, then the picture gets no verdict: an unreadable spot must not bring back one costly call per chunk (review of 22.11.2026). */
#define PNGCHECK_WINDOW_MISSES 64u

static inline long
pngcheck_window_read(void *ctx, uint64_t off, void *buf, size_t n) {
  pngcheck_window_t *w = (pngcheck_window_t *)ctx;
  if(off < w->at || off >= w->at + w->len) {
    const long got = w->rd(w->ctx, off, w->buf, w->cap);
    if(got < 0) {
      w->len = 0;
      if(++w->misses > PNGCHECK_WINDOW_MISSES) return -1;
      return w->rd(w->ctx, off, buf, n);
    }
    w->at = off;
    w->len = (size_t)got;
    if(got == 0) return 0;                                     /* the end of the file */
  }
  size_t have = (size_t)(w->at + w->len - off);
  if(have > n) have = n;
  memcpy(buf, w->buf + (off - w->at), have);
  return (long)have;
}

/* Reads exactly n bytes: 0, 1 when the file ends first, -1 on a read error. */
static inline int
pngcheck_read_all(pngcheck_read_fn rd, void *ctx, uint64_t off, uint8_t *buf, size_t n) {
  size_t got = 0;
  while(got < n) {
    long r = rd(ctx, off + got, buf + got, n - got);
    if(r < 0) return -1;
    if(r == 0) return 1;
    got += (size_t)r;
  }
  return 0;
}

static inline uint32_t
pngcheck_be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | (uint32_t)p[3]; }

static inline uint32_t
pngcheck_crc(const uint32_t *table, uint32_t crc, const uint8_t *p, size_t n) {
  for(size_t i = 0; i < n; i++) crc = table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
  return crc;
}

#define PNGCHECK_PIECE 65536u                  /* read in pieces of this size, from the heap: a connection thread's stack is small */

/* The walk itself; table and buf come from ps5tm_pngcheck. */
static inline int
pngcheck_walk(pngcheck_read_fn rd, void *ctx, const uint32_t *table, uint8_t *buf) {
  static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
  uint8_t head[8];
  int r = pngcheck_read_all(rd, ctx, 0, head, sizeof(head));
  if(r) return r;                                              /* shorter than a signature: damaged; a read error: no verdict */
  if(memcmp(head, sig, sizeof(sig))) return 1;
  uint64_t off = 8;
  for(unsigned n = 0; n < PNGCHECK_MAX_CHUNKS; n++) {
    uint8_t ch[8];
    if((r = pngcheck_read_all(rd, ctx, off, ch, sizeof(ch))) != 0) return r;      /* ends without IEND */
    const uint32_t len = pngcheck_be32(ch);
    if(len > 0x7FFFFFFFu) return 1;                            /* the format's limit */
    for(int i = 4; i < 8; i++) if(!((ch[i] >= 'A' && ch[i] <= 'Z') || (ch[i] >= 'a' && ch[i] <= 'z'))) return 1;
    if(n == 0 && (memcmp(ch + 4, "IHDR", 4) || len != 13)) return 1;
    if(off + 12 + (uint64_t)len > PNGCHECK_MAX_BYTES) {          /* too large to judge, unless the file ends before this chunk would: */
      uint8_t last;                                              /* a damaged length in a small file is damage (review of 22.11.2026) */
      const long rl = rd(ctx, off + 11 + (uint64_t)len, &last, 1);
      return rl == 0 ? 1 : -1;
    }
    uint32_t crc = pngcheck_crc(table, 0xFFFFFFFFu, ch + 4, 4);
    uint64_t at = off + 8, left = len;
    while(left) {
      const size_t take = left < PNGCHECK_PIECE ? (size_t)left : PNGCHECK_PIECE;
      if((r = pngcheck_read_all(rd, ctx, at, buf, take)) != 0) return r;          /* the chunk runs past the end of the file */
      crc = pngcheck_crc(table, crc, buf, take);
      at += take;
      left -= take;
    }
    uint8_t stored[4];
    if((r = pngcheck_read_all(rd, ctx, at, stored, sizeof(stored))) != 0) return r;
    if((crc ^ 0xFFFFFFFFu) != pngcheck_be32(stored)) return 1;
    if(!memcmp(ch + 4, "IEND", 4)) return len == 0 ? 0 : 1;
    off = at + 4;
  }
  return 1;                                                    /* more chunks than any picture has */
}

/* 0: a whole PNG; 1: damaged; -1: it could not be read (or is too large to judge, or no memory): no verdict. */
static inline int
ps5tm_pngcheck(pngcheck_read_fn rd, void *ctx) {
  uint32_t *table = malloc(256 * sizeof(uint32_t) + PNGCHECK_PIECE);   /* CRC-32 (ISO 3309, as PNG uses it) and the read buffer, no shared state */
  if(!table) return -1;
  for(uint32_t i = 0; i < 256; i++) {
    uint32_t c = i;
    for(int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
    table[i] = c;
  }
  const int r = pngcheck_walk(rd, ctx, table, (uint8_t *)(table + 256));
  free(table);
  return r;
}

#endif
