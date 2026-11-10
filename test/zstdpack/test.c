/* Host test of src/zstdpack.c: pack, check, list, unpack and remove a made-up game folder; damage must be found.
 * Usage: test <workdir> [keep]   (the work folder is made fresh; with "keep" the packed game stays for another reader) */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "zstdpack.h"

/* the two list helpers of lz4pack.c (not linked here), the same code */
static int cmp_str(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }
void lzp_list_free(lzp_list_t *l) { for(size_t i = 0; i < l->n; i++) free(l->rel[i]); free(l->rel); memset(l, 0, sizeof(*l)); }
int lzp_list_has(const lzp_list_t *l, const char *rel) { return l->n && bsearch(&rel, l->rel, l->n, sizeof(char *), cmp_str) != NULL; }

static int g_fail, g_checks;
#define CHECK(c) do { g_checks++; if(!(c)) { g_fail++; printf("FAIL line %d: %s\n", __LINE__, #c); } } while(0)

static uint64_t g_seed = 88172645463325252ull;
static uint32_t rnd(void) { g_seed ^= g_seed << 13; g_seed ^= g_seed >> 7; g_seed ^= g_seed << 17; return (uint32_t)(g_seed >> 11); }

static void
mkdirs(const char *path) {
  char b[PATH_MAX];
  snprintf(b, sizeof(b), "%s", path);
  for(char *p = b + 1; *p; p++) if(*p == '/') { *p = 0; mkdir(b, 0777); *p = '/'; }
  mkdir(b, 0777);
}

/* kind 0: text-like (compresses well), 1: random (does not), 2: half and half */
static void
make_file(const char *root, const char *rel, size_t size, int kind) {
  char p[PATH_MAX];
  snprintf(p, sizeof(p), "%s/%s", root, rel);
  char d[PATH_MAX];
  snprintf(d, sizeof(d), "%s", p);
  char *sl = strrchr(d, '/');
  if(sl) { *sl = 0; mkdirs(d); }
  FILE *f = fopen(p, "wb");
  if(!f) { printf("cannot create %s\n", p); exit(2); }
  for(size_t i = 0; i < size; i++) {
    uint8_t b;
    int k = kind == 2 ? (int)((i >> 17) & 1) : kind;
    if(k == 0) b = (uint8_t)("the quick brown fox jumps over the lazy dog "[(i + (i >> 9)) % 44]);
    else b = (uint8_t)rnd();
    fputc(b, f);
  }
  fclose(f);
}

static int
same_file(const char *a, const char *b) {
  FILE *x = fopen(a, "rb"), *y = fopen(b, "rb");
  int same = x && y;
  while(same) {
    int c = fgetc(x), d = fgetc(y);
    if(c != d) same = 0;
    if(c == EOF) break;
  }
  if(x) fclose(x);
  if(y) fclose(y);
  return same;
}

static void
flip_byte(const char *path, long at) {
  FILE *f = fopen(path, "r+b");
  if(!f) return;
  fseek(f, at, SEEK_SET);
  int c = fgetc(f);
  fseek(f, at, SEEK_SET);
  fputc(c ^ 0x5A, f);
  fclose(f);
}

static int
exists(const char *p) { struct stat st; return lstat(p, &st) == 0; }

int
main(int argc, char **argv) {
  if(argc < 2) { puts("usage: test <workdir> [keep]"); return 2; }
  int keep = argc > 2 && !strcmp(argv[2], "keep");
  char root[PATH_MAX], out[PATH_MAX], cmd[PATH_MAX * 2];
  snprintf(root, sizeof(root), "%s/game", argv[1]);
  snprintf(cmd, sizeof(cmd), "rm -rf '%s'", argv[1]);
  if(system(cmd)) return 2;
  mkdirs(root);

  make_file(root, "eboot.bin", 5000, 1);
  make_file(root, "sce_sys/param.json", 900, 0);
  make_file(root, "sce_module/libfoo.prx", 3000, 1);
  make_file(root, "fakelib/libSceAmpr.sprx", 4000, 1);
  make_file(root, "empty.dat", 0, 0);
  make_file(root, "data/text.txt", 1000, 0);
  make_file(root, "data/big.bin", 3 * 1024 * 1024 + 1234, 2);
  make_file(root, "data/rand.dat", 600 * 1024, 1);
  make_file(root, "data/exact.bin", 512 * 1024, 0);            /* exactly two chunks */
  make_file(root, "data/sub/\xc3\xbc\xc3\xa4.bin", 70000, 0);  /* UTF-8 name */
  make_file(root, "cache_ps5/playgo.pgm", 2000, 0);            /* a file the PS5 looks for on disk: stays loose */
  make_file(root, "movie.mp4", 300000, 1);

  /* what stays loose */
  CHECK(zsp_stays_loose("eboot.bin", 10));
  CHECK(zsp_stays_loose("a/b/EBOOT.BIN", 10));
  CHECK(zsp_stays_loose("lib/x.sprx", 10));
  CHECK(zsp_stays_loose("sce_sys/a/b.png", 10));
  CHECK(zsp_stays_loose("data/dlc_emu.ini", 10));                 /* settings stay editable (Dump Forge 0.5.5) */
  CHECK(zsp_stays_loose("Config/GAME.INI", 10));
  CHECK(!zsp_stays_loose("data/x.ini.bin", 10));                  /* only the extension counts */
  CHECK(zsp_stays_loose("fakelib2/x", 10));
  CHECK(zsp_stays_loose("ampr_zstd.index", 10));
  CHECK(zsp_stays_loose("ampr_zstd-000.pak", 10));
  CHECK(zsp_stays_loose("ampr_emu.index", 10));
  CHECK(zsp_stays_loose("data/x.bin", 0));
  CHECK(!zsp_stays_loose("data/x.bin", 1));
  CHECK(!zsp_stays_loose("movie.mp4", 5));

  lzp_plan_t pl;
  char err[400] = "";
  CHECK(zsp_plan(root, &pl, NULL, err, sizeof(err)) == 0);
  CHECK(pl.files_total == 12);
  CHECK(pl.pack_files == 6);                                    /* text, big, rand, exact, sub, movie */
  CHECK(pl.loose_files == 6);
  CHECK(pl.chunks == 1 + 13 + 3 + 2 + 1 + 2);                   /* big: 3 MiB + 1234 = 13 chunks of 256 KiB */
  printf("plan: %llu files, %llu to pack (%llu bytes, %llu chunks)\n", (unsigned long long)pl.files_total,
         (unsigned long long)pl.pack_files, (unsigned long long)pl.pack_bytes, (unsigned long long)pl.chunks);

  /* pack in place at level 5, and at 19 into another folder */
  lzp_stats_t st;
  CHECK(!zsp_packed_dir(root));
  CHECK(zsp_pack(root, root, 5, 3, &st, NULL, err, sizeof(err)) == 0);
  if(g_fail) printf("pack error: %s\n", err);
  CHECK(zsp_packed_dir(root));
  CHECK(st.files_packed == 6);
  CHECK(st.chunks == pl.chunks);
  CHECK(st.chunks_lz4 + st.chunks_raw == st.chunks);
  CHECK(st.chunks_raw >= 3 + 1);                                /* random data stays raw */
  CHECK(st.stored_bytes < st.logical_bytes);
  printf("pack: %llu chunks (%llu zstd, %llu raw), %llu -> %llu bytes, %u volume(s)\n", (unsigned long long)st.chunks,
         (unsigned long long)st.chunks_lz4, (unsigned long long)st.chunks_raw, (unsigned long long)st.logical_bytes,
         (unsigned long long)st.stored_bytes, st.packs);
  CHECK(zsp_pack(root, root, 5, 3, &st, NULL, err, sizeof(err)) != 0);       /* again: refused */
  snprintf(out, sizeof(out), "%s/copy", argv[1]);
  mkdirs(out);
  CHECK(zsp_pack(root, out, 19, 2, &st, NULL, err, sizeof(err)) == 0);
  CHECK(zsp_packed_dir(out));

  lzp_info_t inf;
  int level = 0;
  CHECK(zsp_info(root, &inf, &level) == 1);
  CHECK(inf.packed && inf.files_packed == 6 && inf.volumes == 1 && level == 5 && strlen(inf.build_id) == 32);
  CHECK(inf.logical_bytes == pl.pack_bytes);
  CHECK(zsp_info(out, &inf, &level) == 1 && level == 19);

  /* verify: alone and against the originals */
  lzp_stats_t vs;
  CHECK(zsp_verify(root, NULL, &vs, NULL, err, sizeof(err)) == 0);
  CHECK(vs.files_packed == 6 && vs.logical_bytes == pl.pack_bytes);
  CHECK(zsp_verify(root, root, &vs, NULL, err, sizeof(err)) == 0);
  CHECK(zsp_verify(out, root, &vs, NULL, err, sizeof(err)) == 0);

  /* the list */
  lzp_list_t l;
  CHECK(zsp_list(root, &l, err, sizeof(err)) == 0 && l.n == 6);
  CHECK(lzp_list_has(&l, "data/big.bin") && lzp_list_has(&l, "movie.mp4") && lzp_list_has(&l, "data/sub/\xc3\xbc\xc3\xa4.bin"));
  CHECK(!lzp_list_has(&l, "eboot.bin") && !lzp_list_has(&l, "empty.dat"));
  lzp_list_free(&l);

  /* unpack into a fresh folder and compare */
  char dst[PATH_MAX];
  snprintf(dst, sizeof(dst), "%s/unpacked", argv[1]);
  mkdirs(dst);
  CHECK(zsp_unpack(root, dst, &vs, NULL, err, sizeof(err)) == 0);
  CHECK(vs.files_packed == 6);
  static const char *const names[] = { "data/text.txt", "data/big.bin", "data/rand.dat", "data/exact.bin", "data/sub/\xc3\xbc\xc3\xa4.bin", "movie.mp4" };
  for(unsigned i = 0; i < 6; i++) {
    char a[PATH_MAX], b[PATH_MAX];
    snprintf(a, sizeof(a), "%s/%s", root, names[i]);
    snprintf(b, sizeof(b), "%s/%s", dst, names[i]);
    CHECK(same_file(a, b));
  }
  {
    char b[PATH_MAX];
    snprintf(b, sizeof(b), "%s/eboot.bin", dst);
    CHECK(!exists(b));                                          /* loose files are not in the packs */
  }

  if(keep) {                                                    /* a packed game for another reader (the originals stay) */
    printf("%d checks, %d failed (kept: %s)\n", g_checks, g_fail, argv[1]);
    return g_fail ? 1 : 0;
  }

  /* damage is found: a changed byte in a volume, in the index, a short volume, a changed original */
  char vol[PATH_MAX], idx[PATH_MAX];
  snprintf(vol, sizeof(vol), "%s/ampr_zstd-000.pak", out);
  snprintf(idx, sizeof(idx), "%s/ampr_zstd.index", out);
  flip_byte(vol, 5000);
  CHECK(zsp_verify(out, NULL, &vs, NULL, err, sizeof(err)) != 0);
  printf("damaged volume: %s\n", err);
  flip_byte(vol, 5000);
  CHECK(zsp_verify(out, NULL, &vs, NULL, err, sizeof(err)) == 0);
  flip_byte(idx, 300);
  CHECK(zsp_verify(out, NULL, &vs, NULL, err, sizeof(err)) != 0);
  printf("damaged index: %s\n", err);
  flip_byte(idx, 300);
  CHECK(zsp_verify(out, NULL, &vs, NULL, err, sizeof(err)) == 0);
  flip_byte(idx, 20);                                           /* the volume count */
  CHECK(zsp_info(out, &inf, &level) == 0 && inf.why[0]);
  flip_byte(idx, 20);
  CHECK(zsp_info(out, &inf, &level) == 1);
  {
    char big[PATH_MAX];
    snprintf(big, sizeof(big), "%s/data/exact.bin", root);
    flip_byte(big, 1000);
    CHECK(zsp_verify(out, root, &vs, NULL, err, sizeof(err)) != 0);
    printf("changed original: %s\n", err);
    flip_byte(big, 1000);
    CHECK(zsp_verify(out, root, &vs, NULL, err, sizeof(err)) == 0);
    if(truncate(vol, 100000) != 0) puts("truncate failed");
    CHECK(zsp_verify(out, NULL, &vs, NULL, err, sizeof(err)) != 0);
    printf("short volume: %s\n", err);
  }

  /* cancel */
  int stop = 1;
  lzp_ui_t ui;
  memset(&ui, 0, sizeof(ui));
  ui.cancel = &stop;
  snprintf(out, sizeof(out), "%s/cancelled", argv[1]);
  mkdirs(out);
  CHECK(zsp_pack(root, out, 3, 2, &st, &ui, err, sizeof(err)) == ECANCELED);
  CHECK(!zsp_packed_dir(out));
  {
    char t[PATH_MAX];
    snprintf(t, sizeof(t), "%s/ampr_zstd-000.pak.ps5cc-teil", out);
    CHECK(!exists(t));                                          /* nothing left behind */
  }

  /* remove the originals, then the set */
  lzp_stats_t rs;
  CHECK(zsp_verify(root, root, &vs, NULL, err, sizeof(err)) == 0);
  CHECK(zsp_remove_sources(root, root, 1, &rs, NULL, err, sizeof(err)) == 0);
  CHECK(rs.files_packed == 6);
  {
    char a[PATH_MAX];
    snprintf(a, sizeof(a), "%s/data/big.bin", root);
    CHECK(!exists(a));
    snprintf(a, sizeof(a), "%s/data/sub", root);
    CHECK(!exists(a));                                          /* the empty folder went too */
    snprintf(a, sizeof(a), "%s/eboot.bin", root);
    CHECK(exists(a));                                           /* programs stay */
    snprintf(a, sizeof(a), "%s/cache_ps5/playgo.pgm", root);
    CHECK(exists(a));
  }
  CHECK(zsp_verify(root, NULL, &vs, NULL, err, sizeof(err)) == 0);          /* the set alone still reads */
  if(!keep) {
    CHECK(zsp_remove_set(root, err, sizeof(err)) == 0);
    CHECK(!zsp_packed_dir(root));
    char a[PATH_MAX];
    snprintf(a, sizeof(a), "%s/ampr_zstd-000.pak", root);
    CHECK(!exists(a));
  }
  printf("%d checks, %d failed\n", g_checks, g_fail);
  return g_fail ? 1 : 0;
}
