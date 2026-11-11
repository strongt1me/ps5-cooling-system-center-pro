/* Host test of src/sha256.c: the plain C and the SHA extensions give the same bytes, and the right ones.
 * Usage: test <workdir>   (the work folder is made if it is not there; one file of 8 MiB is written into it) */
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "sha256.h"

static int g_fail, g_checks;
#define CHECK(c) do { g_checks++; if(!(c)) { g_fail++; printf("FAIL line %d: %s\n", __LINE__, #c); } } while(0)

static uint64_t g_seed = 88172645463325252ull;
static uint32_t rnd(void) { g_seed ^= g_seed << 13; g_seed ^= g_seed >> 7; g_seed ^= g_seed << 17; return (uint32_t)(g_seed >> 11); }

static void
hash(const void *d, size_t n, char hex[65]) {
  ps5tm_sha256_t s;
  uint8_t out[32];
  ps5tm_sha256_init(&s);
  ps5tm_sha256_update(&s, d, n);
  ps5tm_sha256_final(&s, out);
  ps5tm_sha256_hex(out, hex);
}

/* the same message, cut into pieces of random sizes (0 included) */
static void
hash_split(const uint8_t *d, size_t n, char hex[65]) {
  ps5tm_sha256_t s;
  uint8_t out[32];
  ps5tm_sha256_init(&s);
  for(size_t at = 0; at < n;) {
    size_t take = rnd() % 200;
    if(take > n - at) take = n - at;
    ps5tm_sha256_update(&s, d + at, take);
    at += take;
  }
  ps5tm_sha256_final(&s, out);
  ps5tm_sha256_hex(out, hex);
}

static double
now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (double)t.tv_sec + (double)t.tv_nsec / 1e9; }

static void
vectors(const char *label) {
  char h[65];
  hash("", 0, h);
  CHECK(!strcmp(h, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
  hash("abc", 3, h);
  CHECK(!strcmp(h, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
  const char *m = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
  hash(m, strlen(m), h);
  CHECK(!strcmp(h, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));
  const char *m2 = "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu";
  hash(m2, strlen(m2), h);
  CHECK(!strcmp(h, "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1"));
  /* one million times "a", in pieces of 1000 */
  ps5tm_sha256_t s;
  uint8_t out[32], blk[1000];
  memset(blk, 'a', sizeof(blk));
  ps5tm_sha256_init(&s);
  for(int i = 0; i < 1000; i++) ps5tm_sha256_update(&s, blk, sizeof(blk));
  ps5tm_sha256_final(&s, out);
  ps5tm_sha256_hex(out, h);
  CHECK(!strcmp(h, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));
  printf("vectors (%s): done\n", label);
}

int
main(int argc, char **argv) {
  if(argc < 2) { fprintf(stderr, "usage: test <workdir>\n"); return 2; }
  mkdir(argv[1], 0777);

  ps5tm_sha256_force(-1);
  int hw = ps5tm_sha256_hw();
  printf("this CPU: %s\n", hw ? "SHA extensions present, the hardware path is chosen" : "no SHA extensions: only the plain C can be tested");

  ps5tm_sha256_force(0);
  CHECK(ps5tm_sha256_hw() == 0);
  vectors("plain C");
  if(hw) {
    ps5tm_sha256_force(1);
    CHECK(ps5tm_sha256_hw() == 1);
    vectors("SHA extensions");
  }

  /* random messages: lengths 0 to 1500 (every position around the block and padding borders), cut at random */
  if(hw) {
    uint8_t *buf = malloc(4096);
    for(int round = 0; round < 3000; round++) {
      size_t n = round < 1600 ? (size_t)round % 1500 : rnd() % 1500;
      for(size_t i = 0; i < n; i++) buf[i] = (uint8_t)rnd();
      char a[65], b[65], c[65], d[65];
      ps5tm_sha256_force(0); hash(buf, n, a); hash_split(buf, n, c);
      ps5tm_sha256_force(1); hash(buf, n, b); hash_split(buf, n, d);
      if(strcmp(a, b) || strcmp(a, c) || strcmp(a, d)) { CHECK(0); printf("   length %zu\n", n); break; }
    }
    g_checks++;
    free(buf);
  }

  /* an independent implementation: the system's sha256sum over a file */
  {
    const size_t n = 8u << 20;
    uint8_t *buf = malloc(n);
    for(size_t i = 0; i < n; i++) buf[i] = (uint8_t)rnd();
    char path[600];
    snprintf(path, sizeof(path), "%s/random8m.bin", argv[1]);
    FILE *f = fopen(path, "wb");
    CHECK(f && fwrite(buf, 1, n, f) == n);
    if(f) fclose(f);
    char cmd[700], want[65] = "", got[65];
    snprintf(cmd, sizeof(cmd), "sha256sum '%s'", path);
    FILE *p = popen(cmd, "r");
    if(p) { if(fscanf(p, "%64s", want) != 1) want[0] = 0; pclose(p); }
    CHECK(strlen(want) == 64);
    for(int mode = 0; mode <= (hw ? 1 : 0); mode++) {
      ps5tm_sha256_force(mode);
      hash(buf, n, got);
      CHECK(!strcmp(got, want));
    }
    free(buf);
    unlink(path);
  }

  /* speed: 256 MiB in pieces of 1 MiB, once per path */
  {
    const size_t piece = 1u << 20, total = 256u;
    uint8_t *buf = malloc(piece);
    for(size_t i = 0; i < piece; i++) buf[i] = (uint8_t)rnd();
    char first[65] = "";
    for(int mode = 0; mode <= (hw ? 1 : 0); mode++) {
      ps5tm_sha256_force(mode);
      ps5tm_sha256_t s;
      uint8_t out[32];
      char hex[65];
      ps5tm_sha256_init(&s);
      double t0 = now();
      for(size_t i = 0; i < total; i++) ps5tm_sha256_update(&s, buf, piece);
      double t1 = now();
      ps5tm_sha256_final(&s, out);
      ps5tm_sha256_hex(out, hex);
      printf("speed %-14s: %7.1f MB/s\n", mode ? "SHA extensions" : "plain C", (double)total * 1.048576 / (t1 - t0));
      if(!mode) snprintf(first, sizeof(first), "%s", hex);
      else CHECK(!strcmp(first, hex));
    }
    free(buf);
  }

  printf("%d checks, %d failed\n", g_checks, g_fail);
  return g_fail ? 1 : 0;
}
