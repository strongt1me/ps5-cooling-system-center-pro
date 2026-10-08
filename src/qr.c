/* A QR code for the address of the web interface (08.10.2026), so that a phone gets in by pointing its camera at the
 * screen (System page).
 *
 * Adapted from qr.c of webhb 0.4.1 (https://github.com/slopmaster33/webhb, Copyright (C) 2026 slopmaster33, GPL-3.0-or-later):
 * byte mode, error correction level M, versions 1 to 6 (up to 106 bytes), mask 0, written after ISO 18004. Changes: the
 * name of the function and its header, nothing in the encoding. The test (a decoder reading the pictures back) is this
 * project's own.
 *
 * This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either version 3, or (at your option) any later version. */

#include <stdio.h>
#include <string.h>

#include "ps5tm.h"

#define QR_MAX_VERSION 6
#define QR_MAX_SIZE    (17 + 4 * QR_MAX_VERSION)

/* level M: total codewords, error-correction codewords per block, blocks */
static const struct { int total, ec, blocks; } g_ver[QR_MAX_VERSION + 1] = {
  { 0, 0, 0 }, { 26, 10, 1 }, { 44, 16, 1 }, { 70, 26, 1 }, { 100, 18, 2 }, { 134, 24, 2 }, { 172, 16, 4 },
};
/* alignment pattern centres, versions 2 to 6 */
static const int g_align[QR_MAX_VERSION + 1] = { 0, 0, 18, 22, 26, 30, 34 };

/* GF(256) with the QR polynomial 0x11d */
static unsigned char g_exp[512], g_log[256];
static void
gf_init(void) {
  if(g_exp[1]) return;
  unsigned v = 1;
  for(int i = 0; i < 255; i++) {
    g_exp[i] = (unsigned char)v;
    g_log[v] = (unsigned char)i;
    v <<= 1;
    if(v & 0x100) v ^= 0x11d;
  }
  for(int i = 255; i < 512; i++) g_exp[i] = g_exp[i - 255];
}

static unsigned char
gf_mul(unsigned char a, unsigned char b) {
  return (a && b) ? g_exp[g_log[a] + g_log[b]] : 0;
}

/* Reed-Solomon parity of data[n] with ec symbols into out[ec]. */
static void
rs_encode(const unsigned char *data, int n, int ec, unsigned char *out) {
  unsigned char gen[64] = { 1 };
  for(int i = 0; i < ec; i++) {                 /* (x - a^0)(x - a^1)... */
    for(int j = i + 1; j > 0; j--) gen[j] = gen[j - 1] ^ gf_mul(gen[j], g_exp[i]);
    gen[0] = gf_mul(gen[0], g_exp[i]);
  }
  memset(out, 0, (size_t)ec);
  for(int i = 0; i < n; i++) {
    unsigned char f = data[i] ^ out[0];
    memmove(out, out + 1, (size_t)ec - 1);
    out[ec - 1] = 0;
    if(f) for(int j = 0; j < ec; j++) out[j] ^= gf_mul(gen[ec - 1 - j], f);
  }
}

typedef struct {
  int size;
  unsigned char m[QR_MAX_SIZE][QR_MAX_SIZE];    /* bit 0: dark, bit 1: function pattern */
} qr_t;

static void
set(qr_t *q, int r, int c, int dark) { q->m[r][c] = (unsigned char)(2 | (dark ? 1 : 0)); }

static void
finder(qr_t *q, int r0, int c0) {
  for(int r = -1; r <= 7; r++)
    for(int c = -1; c <= 7; c++) {
      int rr = r0 + r, cc = c0 + c;
      if(rr < 0 || cc < 0 || rr >= q->size || cc >= q->size) continue;
      int on = (r >= 0 && r <= 6 && c >= 0 && c <= 6) &&
               (r == 0 || r == 6 || c == 0 || c == 6 || (r >= 2 && r <= 4 && c >= 2 && c <= 4));
      set(q, rr, cc, on);
    }
}

static void
patterns(qr_t *q, int version) {
  finder(q, 0, 0);
  finder(q, 0, q->size - 7);
  finder(q, q->size - 7, 0);
  for(int i = 8; i < q->size - 8; i++) { set(q, 6, i, i % 2 == 0); set(q, i, 6, i % 2 == 0); }
  if(version >= 2) {
    int a = g_align[version];
    for(int r = -2; r <= 2; r++)
      for(int c = -2; c <= 2; c++)
        set(q, a + r, a + c, r == -2 || r == 2 || c == -2 || c == 2 || (r == 0 && c == 0));
  }
  /* format information areas, filled in later; and the dark module */
  for(int i = 0; i < 8; i++) { set(q, 8, i < 6 ? i : i + 1, 0); set(q, i < 6 ? i : i + 1, 8, 0); }
  set(q, 8, 8, 0);
  for(int i = 0; i < 8; i++) { set(q, 8, q->size - 1 - i, 0); set(q, q->size - 1 - i, 8, 0); }
  set(q, q->size - 8, 8, 1);
}

static void
format_bits(qr_t *q, int mask) {
  unsigned bits = (0u << 3) | (unsigned)mask;       /* level M = 00 */
  unsigned rem = bits << 10;
  for(int i = 14; i >= 10; i--) if(rem & (1u << i)) rem ^= 0x537u << (i - 10);
  unsigned f = ((bits << 10) | rem) ^ 0x5412u;

  for(int i = 0; i < 15; i++) {
    int b = (f >> i) & 1;
    /* down the column next to the top-left finder, then along its row */
    if(i < 6)       set(q, i, 8, b);
    else if(i < 8)  set(q, i + 1, 8, b);
    else if(i == 8) set(q, 8, 7, b);
    else            set(q, 8, 14 - i, b);
    /* the second copy: along the row below the top-right finder, then up the column beside the bottom-left one */
    if(i < 8) set(q, 8, q->size - 1 - i, b);
    else      set(q, q->size - 15 + i, 8, b);
  }
}

/* The text (up to 106 bytes) as an SVG picture of its QR code into out. 0, or -1 when it is too long or out is too small. */
int
ps5tm_qr_svg(const char *text, char *out, size_t out_size) {
  gf_init();
  size_t len = strlen(text);
  int version = 0;
  for(int v = 1; v <= QR_MAX_VERSION; v++) {
    int data_cw = g_ver[v].total - g_ver[v].ec * g_ver[v].blocks;
    if((size_t)data_cw >= len + 2) { version = v; break; }       /* mode + count nibbles = 2 bytes with terminator */
  }
  if(!version) return -1;

  const int total = g_ver[version].total, ec = g_ver[version].ec, blocks = g_ver[version].blocks;
  const int data_cw = total - ec * blocks, per_block = data_cw / blocks;

  /* the bit stream: mode 0100, 8-bit count, the bytes, terminator, padding */
  unsigned char data[256] = { 0 };
  int bit = 0;
#define PUT(val, n) for(int k_ = (n) - 1; k_ >= 0; k_--, bit++) if(((val) >> k_) & 1) data[bit / 8] |= (unsigned char)(0x80 >> (bit % 8))
  PUT(4u, 4);
  PUT((unsigned)len, 8);
  for(size_t i = 0; i < len; i++) PUT((unsigned)(unsigned char)text[i], 8);
  if(bit + 4 <= data_cw * 8) bit += 4;
  bit = (bit + 7) / 8 * 8;
  for(int i = bit / 8, k = 0; i < data_cw; i++, k++) data[i] = k % 2 ? 0x11 : 0xec;
#undef PUT

  /* error correction per block, then interleave */
  unsigned char ecc[4][32], final[256];
  for(int b = 0; b < blocks; b++) rs_encode(data + b * per_block, per_block, ec, ecc[b]);
  int n = 0;
  for(int i = 0; i < per_block; i++) for(int b = 0; b < blocks; b++) final[n++] = data[b * per_block + i];
  for(int i = 0; i < ec; i++)        for(int b = 0; b < blocks; b++) final[n++] = ecc[b][i];

  /* the matrix */
  qr_t q;
  memset(&q, 0, sizeof(q));
  q.size = 17 + 4 * version;
  patterns(&q, version);

  /* data bits in the zigzag, mask 0 applied on the way */
  int r = q.size - 1, dir = -1, i = 0;
  for(int c = q.size - 1; c > 0; c -= 2) {
    if(c == 6) c--;
    for(;;) {
      for(int k = 0; k < 2; k++) {
        int cc = c - k;
        if(q.m[r][cc] & 2) continue;
        int b = i < total * 8 ? (final[i / 8] >> (7 - i % 8)) & 1 : 0;
        i++;
        if((r + cc) % 2 == 0) b ^= 1;
        q.m[r][cc] = (unsigned char)b;
      }
      r += dir;
      if(r < 0 || r >= q.size) { r -= dir; dir = -dir; break; }
    }
  }
  format_bits(&q, 0);

  /* as SVG: one path, four modules of quiet zone */
  const int quiet = 4, n_ = q.size + 2 * quiet;
  size_t o = 0;
#define EMIT(...) do { int w_ = snprintf(out + o, out_size > o ? out_size - o : 0, __VA_ARGS__); if(w_ < 0) return -1; o += (size_t)w_; } while(0)
  EMIT("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 %d %d\" shape-rendering=\"crispEdges\">"
       "<rect width=\"%d\" height=\"%d\" fill=\"#fff\"/><path fill=\"#000\" d=\"", n_, n_, n_, n_);
  for(int rr = 0; rr < q.size; rr++)
    for(int cc = 0; cc < q.size; cc++) {
      if(!(q.m[rr][cc] & 1)) continue;
      int w = 1;                                   /* a run of dark modules is one rectangle */
      while(cc + w < q.size && (q.m[rr][cc + w] & 1)) w++;
      EMIT("M%d %dh%dv1h-%dz", cc + quiet, rr + quiet, w, w);
      cc += w - 1;
    }
  EMIT("\"/></svg>");
#undef EMIT
  return o < out_size ? 0 : -1;
}
