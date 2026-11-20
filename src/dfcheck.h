/* PS5 Cooling & System Center - Pro: is a Dump Forge file of a folder whole? (dfcheck.h, 21.11.2026). No state, so a host test can try it (test/dfcheck).
 *
 * A file in /data/PS5-Cooling-Center/dumpforge or in the folder dumpforge/ of a tile is put there by hand or by an older tile; before the app hands it to the
 * loader in place of the built-in program it must be the whole file. Two checks, by what is known about it:
 *   - ps5tm_df_sha_matches: the SHA-256 of the file is the one that is known for its version (the built-in version: the program is in the ELF with its sum);
 *   - ps5tm_df_elf_complete: for a version the app knows no sum of (a newer one), at least that the file is not cut off: an FTP copy that broke off has the
 *     size (over 1 MiB) and the ELF mark, but the tables its headers point at lie behind its end. */
#ifndef PS5TM_DFCHECK_H
#define PS5TM_DFCHECK_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "sha256.h"

/* 1 when the file at `path` has the 64 lower-case hex digits `want` as SHA-256 (read in pieces, never all of it in memory), 0 when it has another sum,
   -1 when it cannot be read to its end (a read error is not a verdict on the file: the caller asks again next time). */
static inline int
ps5tm_df_sha_matches(const char *path, const char *want) {
  FILE *f = fopen(path, "rb");
  if(!f) return -1;
  unsigned char *buf = malloc(64 * 1024);                /* not on the stack of the thread (a server thread has a small one) */
  if(!buf) { fclose(f); return -1; }
  ps5tm_sha256_t s;
  ps5tm_sha256_init(&s);
  int bad = 0;
  for(;;) {
    size_t n = fread(buf, 1, 64 * 1024, f);
    if(n) ps5tm_sha256_update(&s, buf, n);
    if(n < 64 * 1024) { bad = ferror(f); break; }
  }
  fclose(f);
  free(buf);
  if(bad) return -1;
  uint8_t dg[32];
  char hex[65];
  ps5tm_sha256_final(&s, dg);
  ps5tm_sha256_hex(dg, hex);
  return strcmp(hex, want) == 0;
}

static inline uint64_t
ps5tm_df_le(const unsigned char *p, int n) {
  uint64_t v = 0;
  for(int i = n - 1; i >= 0; i--) v = v << 8 | p[i];
  return v;
}

/* 1 when the file is a 64-bit little-endian x86-64 ELF whose program headers, section headers and segments all lie inside its `size` bytes (the end of
   the last of them is not behind the end of the file), 0 when not or when it cannot be read. A file whose headers say 0 headers or no section table is
   judged by what it has. */
static inline int
ps5tm_df_elf_complete(const char *path, uint64_t size) {
  FILE *f = fopen(path, "rb");
  if(!f) return 0;
  unsigned char h[64];
  if(fread(h, 1, sizeof h, f) != sizeof h || memcmp(h, "\x7f" "ELF", 4) || h[4] != 2 || h[5] != 1 || ps5tm_df_le(h + 18, 2) != 62) { fclose(f); return 0; }
  const uint64_t phoff = ps5tm_df_le(h + 32, 8), shoff = ps5tm_df_le(h + 40, 8);
  const uint64_t phent = ps5tm_df_le(h + 54, 2), phnum = ps5tm_df_le(h + 56, 2), shent = ps5tm_df_le(h + 58, 2), shnum = ps5tm_df_le(h + 60, 2);
  if(phnum > 256 || (phnum && phent < 56) || phoff > size || shoff > size) { fclose(f); return 0; }
  uint64_t end = sizeof h;
  if(phnum && phoff + phnum * phent > end) end = phoff + phnum * phent;          /* all the terms are small: 16 + 16 bits at most before the sum */
  if(shoff && shnum && shoff + shnum * shent > end) end = shoff + shnum * shent;
  int ok = end <= size;
  for(uint64_t i = 0; ok && i < phnum; i++) {
    unsigned char p[56];
    if(fseeko(f, (off_t)(phoff + i * phent), SEEK_SET) != 0 || fread(p, 1, sizeof p, f) != sizeof p) { ok = 0; break; }
    const uint64_t off = ps5tm_df_le(p + 8, 8), fsz = ps5tm_df_le(p + 32, 8);
    if(off > size || fsz > size || off + fsz > size) ok = 0;
  }
  fclose(f);
  return ok;
}

#endif
