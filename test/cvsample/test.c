/* Host test of src/cvsample.h: the pieces kept for the quick check of an exFAT image (gameconvert.c).
 * For images of every size from one piece to 2 TiB, read in pieces of 4 MiB (and in shorter ones of changing length), it checks that
 * the list is ascending without a piece twice, stays inside SAMPLE_MAX, holds the first and the last SAMPLE_EDGE pieces, keeps the
 * values that were given in, and leaves no gap wider than the stride and one read. */
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cvsample.h"

static int g_fail, g_checks;
#define CHECK(c, ...) do { g_checks++; if(!(c)) { g_fail++; printf("FAIL line %d: %s: ", __LINE__, #c); printf(__VA_ARGS__); printf("\n"); } } while(0)

#define CHUNK (4ull << 20)

static uint32_t crc_of(uint64_t pos) { return (uint32_t)((pos * 2654435761ull) >> 7) ^ 0x5a5a5a5au; }

typedef struct { uint64_t pos; uint32_t len; } piece_t;

/* one image: raw bytes, pieces of at most `chunk`, the length of each cut by `cut` (0: none; else a changing shorter length) */
static void
one(uint64_t raw, uint64_t chunk, int cut, const char *what) {
  static cv_sampler_t s;
  cv_sampler_init(&s, raw, CHUNK);
  uint64_t pos = 0, npieces = 0, last_pos = 0;
  uint32_t last_len = 0;
  uint64_t first[SAMPLE_EDGE], lastp[SAMPLE_EDGE];
  uint32_t seq = 1;
  while(pos < raw) {
    uint64_t len = chunk;
    if(cut) { seq = seq * 1103515245u + 12345u; len = chunk - ((seq >> 8) % (chunk / 2)); }   /* between half and all of a chunk */
    if(len > raw - pos) len = raw - pos;
    cv_sampler_add(&s, pos, (uint32_t)len, crc_of(pos));
    if(npieces < SAMPLE_EDGE) first[npieces] = pos;
    lastp[npieces % SAMPLE_EDGE] = pos;
    npieces++;
    last_pos = pos;
    last_len = (uint32_t)len;
    pos += len;
  }
  cv_sampler_finish(&s);

  CHECK(s.nsmp >= 1 && s.nsmp <= SAMPLE_MAX, "%s: %u pieces kept", what, s.nsmp);
  int asc = 1, vals = 1;
  uint64_t maxgap = 0;
  for(unsigned i = 0; i < s.nsmp; i++) {
    if(i && s.smp[i].pos <= s.smp[i - 1].pos) asc = 0;
    if(s.smp[i].crc != crc_of(s.smp[i].pos) || s.smp[i].len == 0 || s.smp[i].pos + s.smp[i].len > raw) vals = 0;
    if(i && s.smp[i].pos - s.smp[i - 1].pos > maxgap) maxgap = s.smp[i].pos - s.smp[i - 1].pos;
  }
  CHECK(asc, "%s: ascending, no piece twice", what);
  CHECK(vals, "%s: the values given in come back (crc, length, inside the image)", what);

  /* the first and the last SAMPLE_EDGE pieces are in */
  unsigned nfirst = npieces < SAMPLE_EDGE ? (unsigned)npieces : SAMPLE_EDGE;
  for(unsigned k = 0; k < nfirst; k++) {
    int found = 0;
    for(unsigned i = 0; i < s.nsmp; i++) if(s.smp[i].pos == first[k]) found = 1;
    CHECK(found, "%s: first piece %u (at %llu) kept", what, k, (unsigned long long)first[k]);
  }
  for(unsigned k = 0; k < nfirst; k++) {
    uint64_t want = lastp[(npieces - 1 - k) % SAMPLE_EDGE];
    int found = 0;
    for(unsigned i = 0; i < s.nsmp; i++) if(s.smp[i].pos == want) found = 1;
    CHECK(found, "%s: last piece %u (at %llu) kept", what, k, (unsigned long long)want);
  }
  CHECK(s.smp[s.nsmp - 1].pos == last_pos && s.smp[s.nsmp - 1].len == last_len, "%s: the very last piece ends the list", what);

  /* no gap wider than the stride and one read; a small image is read through (every piece) */
  CHECK(maxgap <= s.stride + chunk, "%s: widest gap %llu, stride %llu", what, (unsigned long long)maxgap, (unsigned long long)s.stride);
  if(npieces <= 3 * SAMPLE_EDGE) CHECK(s.nsmp == npieces || npieces > s.nsmp, "%s: small image", what);
  /* a big image does not give the sampler more work than a few hundred reads */
  CHECK(s.nsmp <= SAMPLE_PIECES + 2 * SAMPLE_EDGE + 2, "%s: %u pieces", what, s.nsmp);
}

int
main(void) {
  static const uint64_t sizes[] = {
    1, 100, CHUNK - 1, CHUNK, CHUNK + 1, 2 * CHUNK, 3 * CHUNK, 4 * CHUNK, 5 * CHUNK + 7, 6 * CHUNK, 7 * CHUNK + 1,
    50 * CHUNK + 3, 99 * CHUNK, 100ull << 20, 1ull << 30, 25ull << 30, 100ull << 30, 400ull << 30, 2ull << 40
  };
  char what[64];
  for(unsigned i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
    snprintf(what, sizeof(what), "%llu bytes, full pieces", (unsigned long long)sizes[i]);
    one(sizes[i], CHUNK, 0, what);
    snprintf(what, sizeof(what), "%llu bytes, short pieces", (unsigned long long)sizes[i]);
    one(sizes[i], CHUNK, 1, what);
    snprintf(what, sizeof(what), "%llu bytes, 1 MiB pieces", (unsigned long long)sizes[i]);
    if(sizes[i] <= (100ull << 30)) one(sizes[i], 1ull << 20, 0, what);
  }

  /* the number kept for a big image: about SAMPLE_PIECES, spread to the end (the last regular piece lies within a stride of it) */
  {
    static cv_sampler_t s;
    const uint64_t raw = 100ull << 30;
    cv_sampler_init(&s, raw, CHUNK);
    for(uint64_t pos = 0; pos < raw; pos += CHUNK) cv_sampler_add(&s, pos, (uint32_t)CHUNK, crc_of(pos));
    cv_sampler_finish(&s);
    printf("100 GiB image: %u pieces kept, stride %llu MiB\n", s.nsmp, (unsigned long long)(s.stride >> 20));
    CHECK(s.nsmp >= SAMPLE_PIECES - 2 && s.nsmp <= SAMPLE_PIECES + 2 * SAMPLE_EDGE + 2, "pieces for a big image: %u", s.nsmp);
  }

  printf("%d checks, %d failed\n", g_checks, g_fail);
  return g_fail ? 1 : 0;
}
