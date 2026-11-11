/* SHA-256 (FIPS 180-4).
 *
 * For the checksum files this app writes next to a backup (checkfile.c): the
 * format of sha256sum, so a PC can check a backup with the tools it already
 * has. Written for this app, portable C; built at -O2 like libdeflate (see
 * the Makefile), since checking a backup means hashing every byte of it.
 *
 * Since 11.11.2026 there is a second path for x86-64: the SHA extensions
 * (SHA-NI, in AMD's Zen since 2017 and in Intel's CPUs since Ice Lake). The
 * plain C below runs at about 125 MB/s on the console, and checking a
 * converted game hashes all of it: a third of the time of a conversion to the
 * internal SSD (measured 11.11.2026). The CPU says whether it has the
 * instructions (CPUID), and before the first use the hardware path hashes
 * "abc" and has to give the known answer, else the plain C stays. The hardware
 * functions carry the target attribute, so no compiler flag is needed and the
 * rest of the program stays free of them. Both paths give the same bytes
 * (test/sha256). */

#include <string.h>

#include "sha256.h"

#if defined(__x86_64__) && (defined(__clang__) || defined(__GNUC__)) && !defined(PS5TM_NO_SHANI)
#include <cpuid.h>
#include <immintrin.h>
#define PS5TM_HAVE_SHANI 1
#endif

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
compress_c(uint32_t h[8], const uint8_t *p) {
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


#ifdef PS5TM_HAVE_SHANI
/* nblocks 64-byte blocks through the SHA extensions. The state is kept as ABEF and CDGH, the order the instructions want;
   the message is added to the round constants four words at a time and the schedule is made with sha256msg1/msg2. */
__attribute__((target("sha,sse4.1,ssse3")))
static void
compress_shani(uint32_t state[8], const uint8_t *data, size_t nblocks) {
  const __m128i mask = _mm_set_epi64x(0x0c0d0e0f08090a0bULL, 0x0405060700010203ULL);
  __m128i tmp = _mm_loadu_si128((const __m128i *)&state[0]);
  __m128i s1  = _mm_loadu_si128((const __m128i *)&state[4]);
  tmp = _mm_shuffle_epi32(tmp, 0xB1);              /* CDAB */
  s1  = _mm_shuffle_epi32(s1, 0x1B);               /* EFGH */
  __m128i s0 = _mm_alignr_epi8(tmp, s1, 8);        /* ABEF */
  s1 = _mm_blend_epi16(s1, tmp, 0xF0);             /* CDGH */

#define QROUND(i, w)                                                                                 \
  do {                                                                                               \
    __m128i m = _mm_add_epi32((w), _mm_loadu_si128((const __m128i *)&K[4 * (i)]));                   \
    s1 = _mm_sha256rnds2_epu32(s1, s0, m);                                                           \
    m = _mm_shuffle_epi32(m, 0x0E);                                                                  \
    s0 = _mm_sha256rnds2_epu32(s0, s1, m);                                                           \
  } while(0)
/* the next four words of the schedule, into a: a = W[t-16..], b = W[t-12..], c = W[t-8..], d = W[t-4..] */
#define SCHED(a, b, c, d) \
  (a) = _mm_sha256msg2_epu32(_mm_add_epi32(_mm_sha256msg1_epu32((a), (b)), _mm_alignr_epi8((d), (c), 4)), (d))

  while(nblocks--) {
    const __m128i s0_save = s0, s1_save = s1;
    __m128i w0 = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i *)(data + 0)), mask);
    __m128i w1 = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i *)(data + 16)), mask);
    __m128i w2 = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i *)(data + 32)), mask);
    __m128i w3 = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i *)(data + 48)), mask);
    QROUND(0, w0); QROUND(1, w1); QROUND(2, w2); QROUND(3, w3);
    SCHED(w0, w1, w2, w3); QROUND(4, w0);
    SCHED(w1, w2, w3, w0); QROUND(5, w1);
    SCHED(w2, w3, w0, w1); QROUND(6, w2);
    SCHED(w3, w0, w1, w2); QROUND(7, w3);
    SCHED(w0, w1, w2, w3); QROUND(8, w0);
    SCHED(w1, w2, w3, w0); QROUND(9, w1);
    SCHED(w2, w3, w0, w1); QROUND(10, w2);
    SCHED(w3, w0, w1, w2); QROUND(11, w3);
    SCHED(w0, w1, w2, w3); QROUND(12, w0);
    SCHED(w1, w2, w3, w0); QROUND(13, w1);
    SCHED(w2, w3, w0, w1); QROUND(14, w2);
    SCHED(w3, w0, w1, w2); QROUND(15, w3);
    s0 = _mm_add_epi32(s0, s0_save);
    s1 = _mm_add_epi32(s1, s1_save);
    data += 64;
  }
#undef QROUND
#undef SCHED

  tmp = _mm_shuffle_epi32(s0, 0x1B);               /* FEBA */
  s1  = _mm_shuffle_epi32(s1, 0xB1);               /* DCHG */
  s0  = _mm_blend_epi16(tmp, s1, 0xF0);            /* DCBA */
  s1  = _mm_alignr_epi8(s1, tmp, 8);               /* HGFE */
  _mm_storeu_si128((__m128i *)&state[0], s0);
  _mm_storeu_si128((__m128i *)&state[4], s1);
}

/* The CPU has SSSE3, SSE4.1 and the SHA extensions (CPUID leaf 7, EBX bit 29). */
static int
cpu_has_shani(void) {
  unsigned a, b, c, d;
  if(!__get_cpuid(0, &a, &b, &c, &d) || a < 7) return 0;
  if(!__get_cpuid(1, &a, &b, &c, &d)) return 0;
  const int ssse3 = (c >> 9) & 1, sse41 = (c >> 19) & 1;
  if(!__get_cpuid_count(7, 0, &a, &b, &c, &d)) return 0;
  return ssse3 && sse41 && ((b >> 29) & 1);
}
#endif

/* The path in use: -1 not decided yet, 0 plain C, 1 the SHA extensions. Decided on first use (every thread gets the same
   answer, so a second thread deciding at the same moment does no harm). */
static int g_path = -1;

/* The known answer for "abc", to be given by the hardware path before it is trusted. */
static int
hw_selftest(void) {
#ifdef PS5TM_HAVE_SHANI
  static const uint32_t iv[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
  static const uint32_t want[8] = { 0xba7816bf, 0x8f01cfea, 0x414140de, 0x5dae2223, 0xb00361a3, 0x96177a9c, 0xb410ff61, 0xf20015ad };
  uint8_t blk[64];
  uint32_t h[8];
  memset(blk, 0, sizeof(blk));
  memcpy(blk, "abc", 3);
  blk[3]  = 0x80;
  blk[63] = 24;                                     /* the length in bits */
  memcpy(h, iv, sizeof(iv));
  compress_shani(h, blk, 1);
  return memcmp(h, want, sizeof(h)) == 0;
#else
  return 0;
#endif
}

static int
decide_path(void) {
  int path = 0;
#ifdef PS5TM_HAVE_SHANI
  if(cpu_has_shani() && hw_selftest()) path = 1;
#endif
  __atomic_store_n(&g_path, path, __ATOMIC_RELAXED);
  return path;
}

int
ps5tm_sha256_hw(void) {
  int p = __atomic_load_n(&g_path, __ATOMIC_RELAXED);
  return p >= 0 ? p : decide_path();
}

void
ps5tm_sha256_force(int mode) {
  if(mode < 0) { __atomic_store_n(&g_path, -1, __ATOMIC_RELAXED); return; }
  int hw = mode > 0 ? decide_path() : 0;
  __atomic_store_n(&g_path, hw, __ATOMIC_RELAXED);
}

static void
compress_blocks(uint32_t h[8], const uint8_t *p, size_t n) {
#ifdef PS5TM_HAVE_SHANI
  if(ps5tm_sha256_hw()) { compress_shani(h, p, n); return; }
#endif
  for(; n; n--, p += 64) compress_c(h, p);
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
    compress_blocks(s->h, s->buf, 1);
    s->fill = 0;
  }
  if(len >= 64) {
    size_t n = len / 64;
    compress_blocks(s->h, p, n);
    p += n * 64;
    len -= n * 64;
  }
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
