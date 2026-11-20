/* Host test of src/dfcheck.h: is a Dump Forge file of a folder whole? (21.11.2026). The checksum of a file read in pieces, and the check that a file is an
   x86-64 ELF that is not cut off. Optional: DF_REAL=<the released ps5-dump-forge-<version>-ps5.elf> also tries the real file and copies cut off at several places. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "dfcheck.h"

static int fails, checks;
#define CHECK(c, ...) do { checks++; if(!(c)) { fails++; printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while(0)

static char work[400];

static void
put(unsigned char *p, int n, uint64_t v) {
  for(int i = 0; i < n; i++) { p[i] = (unsigned char)(v & 0xff); v >>= 8; }
}

/* A file: the bytes b[0..n) under work/name; its path. */
static const char *
write_file(const char *name, const unsigned char *b, size_t n) {
  static char path[460];
  snprintf(path, sizeof path, "%s/%s", work, name);
  FILE *f = fopen(path, "wb");
  if(!f || (n && fwrite(b, 1, n, f) != n)) { printf("cannot write %s\n", path); exit(2); }
  fclose(f);
  return path;
}

/* A small x86-64 ELF: header, one program header (a segment of `seg` bytes at 0x200), `ph2` more program headers all alike (the same segment) and a table of
   `shnum` section headers behind the segment (shoff 0 when shnum is 0). Returns the size in `size`; the buffer is malloc'd. */
static unsigned char *
make_elf(size_t seg, int ph2, int shnum, size_t *size) {
  const size_t phoff = 64, phn = 1 + (size_t)ph2, segoff = 0x200;
  const size_t shoff = shnum ? segoff + seg : 0, total = segoff + seg + (size_t)shnum * 64;
  unsigned char *b = calloc(1, total ? total : 1);
  memcpy(b, "\x7f" "ELF", 4);
  b[4] = 2; b[5] = 1; b[6] = 1;
  put(b + 16, 2, 3);                  /* ET_DYN */
  put(b + 18, 2, 62);                 /* x86-64 */
  put(b + 20, 4, 1);
  put(b + 32, 8, phoff);
  put(b + 40, 8, shoff);
  put(b + 52, 2, 64);
  put(b + 54, 2, 56);
  put(b + 56, 2, phn);
  put(b + 58, 2, 64);
  put(b + 60, 2, (uint64_t)shnum);
  for(size_t i = 0; i < phn; i++) {
    unsigned char *p = b + phoff + i * 56;
    put(p, 4, 1);                     /* PT_LOAD */
    put(p + 8, 8, segoff);
    put(p + 32, 8, seg);
    put(p + 40, 8, seg);
  }
  *size = total;
  return b;
}

static int
complete(const char *name, const unsigned char *b, size_t n) {
  return ps5tm_df_elf_complete(write_file(name, b, n), n);
}

int
main(int argc, char **argv) {
  snprintf(work, sizeof work, "%s", argc > 1 ? argv[1] : "/tmp/ps5tm-dfcheck");
  mkdir(work, 0777);

  /* ------------------------------------------------------------ SHA-256 of a file */
  {
    const unsigned char abc[] = "abc";
    const char *p = write_file("abc.bin", abc, 3);
    CHECK(ps5tm_df_sha_matches(p, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") == 1, "SHA-256 of abc");
    CHECK(ps5tm_df_sha_matches(p, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ae") == 0, "a sum that differs in its last digit");
    CHECK(ps5tm_df_sha_matches(p, "") == 0, "an empty sum matches nothing");
    CHECK(ps5tm_df_sha_matches("/nonexistent/dir/nothing.elf", "00") == -1, "a file that does not exist: -1, no verdict");
    /* more than one piece (64 KiB): 3 pieces and a bit, against the sum made in one go by the same code */
    const size_t big = 3 * 64 * 1024 + 4321;
    unsigned char *b = malloc(big);
    uint32_t x = 12345;
    for(size_t i = 0; i < big; i++) { x = x * 1664525u + 1013904223u; b[i] = (unsigned char)(x >> 24); }
    ps5tm_sha256_t s;
    uint8_t dg[32];
    char hex[65];
    ps5tm_sha256_init(&s);
    ps5tm_sha256_update(&s, b, big);
    ps5tm_sha256_final(&s, dg);
    ps5tm_sha256_hex(dg, hex);
    p = write_file("big.bin", b, big);
    CHECK(ps5tm_df_sha_matches(p, hex) == 1, "a file of several pieces has the sum of its bytes");
    b[big - 1] ^= 1;
    p = write_file("big1.bin", b, big);
    CHECK(ps5tm_df_sha_matches(p, hex) == 0, "one bit flipped at the end changes the verdict");
    p = write_file("big2.bin", b, big - 64 * 1024);
    CHECK(ps5tm_df_sha_matches(p, hex) == 0, "a file cut by one piece");
    /* exactly one piece, and exactly two (the loop ends on a short read, and a full last piece needs one more) */
    for(int pieces = 1; pieces <= 2; pieces++) {
      size_t n = (size_t)pieces * 64 * 1024;
      ps5tm_sha256_init(&s);
      ps5tm_sha256_update(&s, b, n);
      ps5tm_sha256_final(&s, dg);
      ps5tm_sha256_hex(dg, hex);
      p = write_file("piece.bin", b, n);
      CHECK(ps5tm_df_sha_matches(p, hex) == 1, "a file of exactly %d piece(s)", pieces);
    }
    free(b);
    p = write_file("empty.bin", (const unsigned char *)"", 0);
    CHECK(ps5tm_df_sha_matches(p, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855") == 1, "the sum of an empty file");
  }

  /* ------------------------------------------------------------ the ELF is whole */
  {
    size_t n;
    unsigned char *b = make_elf(0x4000, 0, 3, &n);
    CHECK(complete("ok.elf", b, n) == 1, "a whole ELF");
    CHECK(complete("cut1.elf", b, n - 1) == 0, "cut off by one byte (the end of the section table)");
    CHECK(complete("cut2.elf", b, n - 3 * 64) == 0, "section table gone (the segment is there)");
    CHECK(complete("cut3.elf", b, 0x200 + 0x4000 - 1) == 0, "cut inside the segment");
    CHECK(complete("cut4.elf", b, 0x200) == 0, "only headers");
    CHECK(complete("cut5.elf", b, 100) == 0, "cut inside the program headers");
    CHECK(complete("cut6.elf", b, 63) == 0, "shorter than the ELF header");
    CHECK(complete("cut7.elf", b, 0) == 0, "an empty file");
    CHECK(ps5tm_df_elf_complete("/nonexistent/dir/nothing.elf", 1 << 20) == 0, "a file that does not exist");
    CHECK(ps5tm_df_elf_complete(write_file("ok2.elf", b, n), n - 1) == 0, "the size asked for is smaller than what the headers say");
    free(b);

    b = make_elf(0x4000, 0, 0, &n);
    CHECK(complete("nosec.elf", b, n) == 1, "no section table (shoff 0): judged by the segments");
    CHECK(complete("nosec_cut.elf", b, n - 1) == 0, "no section table, a segment cut off");
    free(b);

    b = make_elf(0x4000, 3, 2, &n);
    CHECK(complete("many.elf", b, n) == 1, "four program headers");
    free(b);

    /* not what the app can use */
    b = make_elf(0x4000, 0, 2, &n);
    b[18] = 3;                                         /* x86 */
    CHECK(complete("x86.elf", b, n) == 0, "another machine");
    b[18] = 62; b[4] = 1;
    CHECK(complete("elf32.elf", b, n) == 0, "32-bit class");
    b[4] = 2; b[5] = 2;
    CHECK(complete("be.elf", b, n) == 0, "big-endian");
    b[5] = 1; b[0] = 'X';
    CHECK(complete("nomark.elf", b, n) == 0, "no ELF mark");
    b[0] = 0x7f;
    CHECK(complete("again.elf", b, n) == 1, "the same bytes put right are whole again");
    free(b);

    /* headers that lie (a file made on purpose): none may run past the file or loop long */
    b = make_elf(0x4000, 0, 2, &n);
    put(b + 56, 2, 5000);
    CHECK(complete("phnum.elf", b, n) == 0, "more program headers than any program has");
    put(b + 56, 2, 1);
    put(b + 54, 2, 8);
    CHECK(complete("phent.elf", b, n) == 0, "a program header size under 56");
    put(b + 54, 2, 56);
    put(b + 32, 8, 0xfffffffffffff000ull);
    CHECK(complete("phoff.elf", b, n) == 0, "program headers far behind the file");
    put(b + 32, 8, 64);
    put(b + 40, 8, 0xffffffffffffffffull);
    CHECK(complete("shoff.elf", b, n) == 0, "section table far behind the file");
    put(b + 40, 8, 0x200 + 0x4000);
    CHECK(complete("fine.elf", b, n) == 1, "put right: whole");
    put(b + 64 + 8, 8, 0xfffffffffffffff0ull);          /* p_offset that wraps when the size is added */
    put(b + 64 + 32, 8, 0x20);
    CHECK(complete("wrap.elf", b, n) == 0, "p_offset + p_filesz that wraps round 2^64");
    put(b + 64 + 8, 8, 0x200);
    put(b + 64 + 32, 8, 0xffffffffffffffffull);
    CHECK(complete("huge.elf", b, n) == 0, "p_filesz of 2^64 - 1");
    free(b);

    /* a file with the mark and a size, nothing else */
    unsigned char *g = calloc(1, 2 << 20);
    memcpy(g, "\x7f" "ELF", 4);
    CHECK(complete("mark.elf", g, 2 << 20) == 0, "the mark and zeros: not an ELF of the app");
    free(g);
  }

  /* ------------------------------------------------------------ the real file, when given */
  const char *real = getenv("DF_REAL");
  if(real && *real) {
    FILE *f = fopen(real, "rb");
    if(!f) { printf("DF_REAL %s cannot be read\n", real); return 2; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    rewind(f);
    unsigned char *b = malloc((size_t)n);
    if(fread(b, 1, (size_t)n, f) != (size_t)n) { printf("DF_REAL: short read\n"); return 2; }
    fclose(f);
    CHECK(complete("real.elf", b, (size_t)n) == 1, "the released file is whole");
    const long cuts[] = { 1, 64, 1000, 1 << 20, n / 2, n - 1472, n - 64, n - 1 };
    for(size_t i = 0; i < sizeof cuts / sizeof cuts[0]; i++) {
      char name[40];
      snprintf(name, sizeof name, "realcut%zu.elf", i);
      CHECK(complete(name, b, (size_t)cuts[i]) == 0, "the released file cut to %ld of %ld bytes", cuts[i], n);
    }
    free(b);
    printf("(real file tried: %ld bytes)\n", n);
  }

  printf("%d checks, %d failed\n", checks, fails);
  return fails ? 1 : 0;
}
