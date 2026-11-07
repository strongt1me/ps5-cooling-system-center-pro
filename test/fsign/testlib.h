/* SPDX-License-Identifier: GPL-3.0-or-later
 * Helpers of the host tests of src/fsign.c: a small ELF as ps5-dump-forge's test builds it, files, folders, an AMPR index. */
#ifndef FSIGN_TESTLIB_H
#define FSIGN_TESTLIB_H

#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int g_fail, g_checks;
#define CHECK(c) do { g_checks++; if(!(c)) { g_fail++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while(0)

static void p16(uint8_t *f, size_t at, uint16_t v) { f[at] = (uint8_t)v; f[at + 1] = (uint8_t)(v >> 8); }
static void p32(uint8_t *f, size_t at, uint32_t v) { for(int i = 0; i < 4; i++) f[at + i] = (uint8_t)(v >> (8 * i)); }
static void p64(uint8_t *f, size_t at, uint64_t v) { for(int i = 0; i < 8; i++) f[at + i] = (uint8_t)(v >> (8 * i)); }
static uint64_t g64(const uint8_t *f, size_t at) { uint64_t v = 0; for(int i = 7; i >= 0; i--) v = (v << 8) | f[at + i]; return v; }

#define ELF_LEN 0x58C0u

/* An eboot-like ELF as the test of ps5-dump-forge builds it: a PT_LOAD of 0x4321 bytes, a process param, a RELRO, a PT_DYNAMIC, a
   PT_SCE_VERSION, section headers (not real) left at the end. */
static uint8_t *
make_elf(uint16_t kind) {
  uint8_t *f = malloc(ELF_LEN);
  for(uint32_t i = 0; i < ELF_LEN; i++) f[i] = (uint8_t)(i * 7 + 3);
  static const uint8_t ident[16] = { 0x7F, 'E', 'L', 'F', 2, 1, 1, 9, 0, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0x55 };
  memcpy(f, ident, 16);
  p16(f, 0x10, kind);
  p16(f, 0x12, 0x3E);
  p32(f, 0x14, 1);
  p64(f, 0x18, 0x1234);
  p64(f, 0x20, 0x40);
  p64(f, 0x28, 0x5800);
  p32(f, 0x30, 0);
  p16(f, 0x34, 0x40); p16(f, 0x36, 0x38); p16(f, 0x38, 5); p16(f, 0x3A, 0x40); p16(f, 0x3C, 3); p16(f, 0x3E, 2);
  const struct { uint32_t t; uint64_t off, size; } ph[5] = {
    { 1, 0x1000, 0x4321 }, { 0x61000001u, 0x1200, 0x40 }, { 0x61000010u, 0x5400, 0x123 }, { 2, 0x5600, 0x80 }, { 0x6FFFFF01u, 0x5700, 0x25 } };
  for(int i = 0; i < 5; i++) {
    size_t at = 0x40 + (size_t)i * 0x38;
    p32(f, at, ph[i].t);
    p32(f, at + 4, 4);
    p64(f, at + 8, ph[i].off);
    p64(f, at + 0x10, 0x100000 + ph[i].off);
    p64(f, at + 0x18, 0x100000 + ph[i].off);
    p64(f, at + 0x20, ph[i].size);
    p64(f, at + 0x28, ph[i].size + 0x100);
    p64(f, at + 0x30, 0x4000);
  }
  p32(f, 0x1200, 0x4942524Fu);
  p32(f, 0x120C, 0x04500031u);
  return f;
}

static void
put_file(const char *path, const void *data, size_t n) {
  FILE *fp = fopen(path, "wb");
  if(!fp) { fprintf(stderr, "cannot write %s\n", path); exit(2); }
  if(n && fwrite(data, 1, n, fp) != n) { fprintf(stderr, "short write %s\n", path); exit(2); }
  fclose(fp);
}

static uint8_t *
get_file(const char *path, size_t *n) {
  FILE *fp = fopen(path, "rb");
  if(!fp) { *n = 0; return NULL; }
  fseek(fp, 0, SEEK_END);
  long l = ftell(fp);
  fseek(fp, 0, SEEK_SET);
  uint8_t *b = malloc(l ? (size_t)l : 1);
  *n = fread(b, 1, (size_t)l, fp);
  fclose(fp);
  return b;
}

static void
mk(const char *path) {
  char t[1024];
  snprintf(t, sizeof(t), "%s", path);
  for(char *p = t + 1; *p; p++) if(*p == '/') { *p = 0; mkdir(t, 0777); *p = '/'; }
  mkdir(t, 0777);
}

static int
exists(const char *p) {
  struct stat st;
  return lstat(p, &st) == 0;
}

static uint64_t
fsize(const char *p) {
  struct stat st;
  return lstat(p, &st) == 0 ? (uint64_t)st.st_size : 0;
}

/* the index of ampr_emu as the app builds it (amprindex.c): "AMPRIDX3", 24-byte records, a blob of paths; no hash table needed here */
static void
make_index(const char *path, const char *const *paths, const uint64_t *sizes, unsigned n) {
  size_t blob = 0;
  for(unsigned i = 0; i < n; i++) blob += strlen(paths[i]) + 1;
  size_t total = 48 + (size_t)n * 24 + blob + 64;
  uint8_t *b = calloc(1, total);
  memcpy(b, "AMPRIDX3", 8);
  p32(b, 8, 3);
  p32(b, 12, 24);
  p64(b, 16, n);
  p64(b, 24, blob);
  size_t off = 0;
  for(unsigned i = 0; i < n; i++) {
    uint8_t *r = b + 48 + (size_t)i * 24;
    p32(r, 0, (uint32_t)off);
    p32(r, 4, (uint32_t)strlen(paths[i]));
    p64(r, 8, sizes[i]);
    p64(r, 16, 1700000000u + i);
    memcpy(b + 48 + (size_t)n * 24 + off, paths[i], strlen(paths[i]) + 1);
    off += strlen(paths[i]) + 1;
  }
  put_file(path, b, total);
  free(b);
}

static void
rm_rf(const char *path) {
  DIR *d = opendir(path);
  if(d) {
    struct dirent *e;
    while((e = readdir(d))) {
      if(!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
      char c[1024];
      snprintf(c, sizeof(c), "%s/%s", path, e->d_name);
      struct stat st;
      if(lstat(c, &st) == 0 && S_ISDIR(st.st_mode)) rm_rf(c);
      else unlink(c);
    }
    closedir(d);
  }
  rmdir(path);
}


#endif
