/* SPDX-License-Identifier: GPL-3.0-or-later
 * Host test for swapping the LZ4 library (src/third_party/lz4): blocks written by one version are read by another, in both
 * directions, with the calls the app makes (LZ4_compress_fast_extState, LZ4_compress_HC_extStateHC, LZ4_decompress_safe).
 * The APR-EMU on the console pins its own LZ4 decoder (1.10.0), so a pack made by the app's compressor must stay readable by it.
 *
 *   test enc <file>   writes records of compressed blocks (the data is made from a seed, not stored)
 *   test dec <file>   reads them, decodes each and compares with the same data made again; prints the compressed size sum */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lz4.h"
#include "lz4hc.h"

static const char *words[] = { "texture", "mesh", "shader", "audio", "level", "player", "sprite", "the", "of", "pak", "ps5", "frame", "data" };

static void
make_data(uint8_t *b, size_t n, int kind, uint32_t seed) {
  uint32_t x = seed * 2654435761u + 12345u;
  size_t i = 0;
  while(i < n) {
    x = x * 1664525u + 1013904223u;
    switch(kind) {
    case 0: b[i++] = 0; break;                                                       /* zeros */
    case 1: b[i++] = (uint8_t)(x >> 24); break;                                      /* random */
    case 2: {                                                                         /* text-like */
      const char *w = words[(x >> 16) % (sizeof(words) / sizeof(words[0]))];
      for(; *w && i < n; w++) b[i++] = (uint8_t)*w;
      if(i < n) b[i++] = ' ';
      break;
    }
    default: {                                                                        /* mixed: runs, copies and noise */
      size_t run = 1 + ((x >> 8) & 255);
      int mode = (int)((x >> 4) & 3);
      for(size_t k = 0; k < run && i < n; k++) {
        if(mode == 0) b[i++] = (uint8_t)(x >> 24);
        else if(mode == 1) b[i++] = (uint8_t)(i * 7);
        else if(mode == 2 && i > 300) { b[i] = b[i - 257]; i++; }
        else b[i++] = 0x55;
      }
    }
    }
  }
}

static const size_t sizes[] = { 1, 12, 13, 15, 16, 17, 64, 255, 256, 1000, 4096, 65535, 65536, 262144, 1048576, 3145735, 4194304 };
#define NSIZE (sizeof(sizes) / sizeof(sizes[0]))
/* mode 0: fast with acceleration lvl, mode 1: HC with level lvl */
static const int modes[][2] = { {0,1},{0,2},{0,8},{1,3},{1,4},{1,5},{1,6},{1,7},{1,8},{1,9},{1,10},{1,11},{1,12} };
#define NMODE (sizeof(modes) / sizeof(modes[0]))

int
main(int argc, char **argv) {
  if(argc < 3) return 2;
  int enc = !strcmp(argv[1], "enc");
  FILE *f = fopen(argv[2], enc ? "wb" : "rb");
  if(!f) { perror("open"); return 2; }
  void *fs = malloc((size_t)LZ4_sizeofState());
  void *hs = malloc((size_t)LZ4_sizeofStateHC());
  uint8_t *raw = malloc(4194304 + 64), *chk = malloc(4194304 + 64), *cmp = malloc((size_t)LZ4_compressBound(4194304) + 64);
  unsigned long long total_c = 0, total_r = 0;
  unsigned recs = 0, bad = 0;
  if(enc) {
    for(int kind = 0; kind < 4; kind++)
      for(size_t s = 0; s < NSIZE; s++)
        for(size_t m = 0; m < NMODE; m++) {
          make_data(raw, sizes[s], kind, (uint32_t)(s * 31 + 7));
          int c = modes[m][0] == 0 ? LZ4_compress_fast_extState(fs, (const char *)raw, (char *)cmp, (int)sizes[s], LZ4_compressBound((int)sizes[s]), modes[m][1])
                                   : LZ4_compress_HC_extStateHC(hs, (const char *)raw, (char *)cmp, (int)sizes[s], LZ4_compressBound((int)sizes[s]), modes[m][1]);
          if(c <= 0) { fprintf(stderr, "compress failed kind %d size %zu mode %zu\n", kind, sizes[s], m); return 1; }
          uint32_t h[4] = { (uint32_t)kind, (uint32_t)s, (uint32_t)sizes[s], (uint32_t)c };
          fwrite(h, sizeof(h), 1, f);
          fwrite(cmp, 1, (size_t)c, f);
          total_c += (unsigned long long)c; total_r += sizes[s]; recs++;
        }
  } else {
    uint32_t h[4];
    while(fread(h, sizeof(h), 1, f) == 1) {
      if(fread(cmp, 1, h[3], f) != h[3]) { fprintf(stderr, "short file\n"); return 1; }
      make_data(chk, h[2], (int)h[0], (uint32_t)(h[1] * 31 + 7));
      int d = LZ4_decompress_safe((const char *)cmp, (char *)raw, (int)h[3], (int)h[2]);
      if(d != (int)h[2] || memcmp(raw, chk, h[2])) { bad++; fprintf(stderr, "MISMATCH kind %u size %u\n", h[0], h[2]); }
      total_c += h[3]; total_r += h[2]; recs++;
    }
  }
  printf("%s: %u blocks, raw %llu, compressed %llu, bad %u\n", enc ? "enc" : "dec", recs, total_r, total_c, bad);
  fclose(f);
  return bad ? 1 : 0;
}
