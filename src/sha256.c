/* SHA-256 (FIPS 180-4).
 *
 * For the checksum files this app writes next to a backup (checkfile.c): the
 * format of sha256sum, so a PC can check a backup with the tools it already
 * has. Written for this app, portable C; built at -O2 like libdeflate (see
 * the Makefile), since checking a backup means hashing every byte of it. */

#include <string.h>

#include "sha256.h"

static const uint32_t K[64] = {
  0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
  0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
  0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
  0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
  0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
  0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
  0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
  0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

#define ROTR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static void
compress(uint32_t h[8], const uint8_t *p) {
  uint32_t w[64];
  for(int i = 0; i < 16; i++)
    w[i] = (uint32_t)p[i * 4] << 24 | (uint32_t)p[i * 4 + 1] << 16 |
           (uint32_t)p[i * 4 + 2] << 8 | (uint32_t)p[i * 4 + 3];
  for(int i = 16; i < 64; i++) {
    uint32_t s0 = ROTR(w[i - 15], 7) ^ ROTR(w[i - 15], 18) ^ (w[i - 15] >> 3);
    uint32_t s1 = ROTR(w[i - 2], 17) ^ ROTR(w[i - 2], 19) ^ (w[i - 2] >> 10);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }
  uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
  uint32_t e = h[4], f = h[5], g = h[6], k = h[7];
  for(int i = 0; i < 64; i++) {
    uint32_t t1 = k + (ROTR(e, 6) ^ ROTR(e, 11) ^ ROTR(e, 25)) +
                  ((e & f) ^ (~e & g)) + K[i] + w[i];
    uint32_t t2 = (ROTR(a, 2) ^ ROTR(a, 13) ^ ROTR(a, 22)) +
                  ((a & b) ^ (a & c) ^ (b & c));
    k = g; g = f; f = e; e = d + t1;
    d = c; c = b; b = a; a = t1 + t2;
  }
  h[0] += a; h[1] += b; h[2] += c; h[3] += d;
  h[4] += e; h[5] += f; h[6] += g; h[7] += k;
}

void
ps5tm_sha256_init(ps5tm_sha256_t *s) {
  static const uint32_t iv[8] = {
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
    0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
  };
  memcpy(s->h, iv, sizeof(iv));
  s->bytes = 0;
  s->fill  = 0;
}

void
ps5tm_sha256_update(ps5tm_sha256_t *s, const void *data, size_t len) {
  const uint8_t *p = data;
  s->bytes += len;
  if(s->fill) {
    size_t n = 64 - s->fill;
    if(n > len) n = len;
    memcpy(s->buf + s->fill, p, n);
    s->fill += (unsigned)n;
    p += n;
    len -= n;
    if(s->fill < 64) return;
    compress(s->h, s->buf);
    s->fill = 0;
  }
  for(; len >= 64; p += 64, len -= 64) compress(s->h, p);
  if(len) {
    memcpy(s->buf, p, len);
    s->fill = (unsigned)len;
  }
}

void
ps5tm_sha256_final(ps5tm_sha256_t *s, uint8_t out[32]) {
  uint64_t bits = s->bytes * 8;
  uint8_t pad[72];
  size_t  n = (s->fill < 56 ? 56 : 120) - s->fill;
  memset(pad, 0, sizeof(pad));
  pad[0] = 0x80;
  for(int i = 0; i < 8; i++) pad[n + (size_t)i] = (uint8_t)(bits >> (56 - 8 * i));
  ps5tm_sha256_update(s, pad, n + 8);
  for(int i = 0; i < 8; i++) {
    out[i * 4]     = (uint8_t)(s->h[i] >> 24);
    out[i * 4 + 1] = (uint8_t)(s->h[i] >> 16);
    out[i * 4 + 2] = (uint8_t)(s->h[i] >> 8);
    out[i * 4 + 3] = (uint8_t)s->h[i];
  }
}

void
ps5tm_sha256_hex(const uint8_t digest[32], char out[65]) {
  static const char hex[] = "0123456789abcdef";
  for(int i = 0; i < 32; i++) {
    out[i * 2]     = hex[digest[i] >> 4];
    out[i * 2 + 1] = hex[digest[i] & 15];
  }
  out[64] = 0;
}
