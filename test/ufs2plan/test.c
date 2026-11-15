/* Host test of the .ffpkg planning and writing of src/conv_ufs2.c (audit 14-conv C-01..C-04):
 *  - a folder exFAT refuses (names that differ in case only, a ':') is planned and written by the UFS2 writer on its own;
 *  - ufs2_plan_info(): image size = what the writer produces, the files and data bytes, what was left out (a link, desktop.ini,
 *    thumbs.db, ._*) counted with the first name;
 *  - the written file has no hole (SEEK_HOLE finds the end only), checks out (ufs2_verify_tree), and the progress is
 *    monotonic and ends at the image size (the total the caller shows it against);
 *  - a cancel before the start and one in the middle of the zero-fill stop it with ECANCELED.
 * With -DWRITE_ONLY it only writes the deterministic tree to argv[2] (used to compare the image byte for byte with the one an
 * older conv_ufs2.c wrote: the format must not change). */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "conv_ufs2.h"
#ifndef WRITE_ONLY
#include "conv_exfat.h"
#endif

static int g_fail, g_checks;
#define CHECK(c, ...) do { g_checks++; if(!(c)) { g_fail++; printf("FAIL line %d: %s: ", __LINE__, #c); printf(__VA_ARGS__); printf("\n"); } } while(0)

/* the clock and the process id go into the superblock; the byte comparison needs them fixed */
time_t time(time_t *t) { if(t) *t = 1700000000; return 1700000000; }
pid_t  getpid(void) { return 4242; }

static void
put_file(const char *dir, const char *name, size_t len, unsigned seed) {
  char p[1024];
  snprintf(p, sizeof(p), "%s/%s", dir, name);
  FILE *f = fopen(p, "wb");
  if(!f) { perror(p); exit(2); }
  uint32_t x = seed;
  for(size_t i = 0; i < len; i++) { x = x * 1664525u + 1013904223u; fputc((int)(x >> 24), f); }
  fclose(f);
}

static void
build_tree(const char *root, int hostile) {
  char p[1024], cmd[2048];
  snprintf(cmd, sizeof(cmd), "rm -rf '%s' && mkdir -p '%s/sce_sys' '%s/data/deep'", root, root, root);
  if(system(cmd) != 0) exit(2);
  snprintf(p, sizeof(p), "%s", root);
  put_file(p, "eboot.bin", 1, 1);
  put_file(p, "empty.dat", 0, 2);
  put_file(p, "big.pak", (3u << 20) + 12345, 3);
  snprintf(p, sizeof(p), "%s/sce_sys", root);
  put_file(p, "param.json", 700, 4);
  snprintf(p, sizeof(p), "%s/data", root);
  put_file(p, "a.bin", 65536, 5);
  put_file(p, "b.bin", 65537, 6);
  snprintf(p, sizeof(p), "%s/data/deep", root);
  put_file(p, "c.bin", 100000, 7);
  if(hostile) {
    snprintf(p, sizeof(p), "%s", root);
    put_file(p, "Readme.txt", 10, 8);               /* the same name in exFAT's eyes */
    put_file(p, "README.TXT", 11, 9);
    put_file(p, "what: is this?", 12, 10);           /* characters exFAT forbids */
    put_file(p, "desktop.ini", 5, 11);
    put_file(p, "._data", 5, 12);
    snprintf(p, sizeof(p), "%s/data", root);
    put_file(p, "thumbs.db", 5, 13);
    snprintf(cmd, sizeof(cmd), "ln -s eboot.bin '%s/link_to_eboot'", root);
    if(system(cmd) != 0) exit(2);
  }
}

#ifndef WRITE_ONLY
static uint64_t g_last, g_calls;
static int      g_backwards;
static void
prog(void *ctx, uint64_t done) {
  (void)ctx;
  if(done < g_last) g_backwards++;
  g_last = done;
  g_calls++;
}

static int g_cancel_flag;
static uint64_t g_cancel_at;
static void
prog_cancel(void *ctx, uint64_t done) {
  (void)ctx;
  if(done >= g_cancel_at) __atomic_store_n(&g_cancel_flag, 1, __ATOMIC_RELEASE);
}
#endif

int
main(int argc, char **argv) {
  if(argc < 2) return 2;
  char tree[1024], img[1024], err[512] = "";
  snprintf(tree, sizeof(tree), "%s/tree", argv[1]);
#ifdef WRITE_ONLY
  build_tree(tree, 1);
  if(argc < 3) return 2;
  int fd = open(argv[2], O_RDWR | O_CREAT | O_TRUNC, 0666);
  uint64_t sz = 0;
  int rc = ufs2_write_tree(fd, tree, NULL, NULL, NULL, &sz, NULL, err, sizeof(err));
  if(rc) { printf("write failed: %s\n", err); return 1; }
  close(fd);
  printf("%llu\n", (unsigned long long)sz);
  return 0;
#else
  snprintf(img, sizeof(img), "%s/out.ffpkg", argv[1]);
  build_tree(tree, 1);

  /* C-01: exFAT cannot plan this folder, UFS2 can */
  exfat_image_t *x = exfat_image_plan(tree, 1, err, sizeof(err));
  CHECK(x == NULL, "exFAT should refuse the case clash / forbidden characters");
  if(x) exfat_image_free(x);

  ufs2_plan_info_t pi;
  memset(&pi, 0xAA, sizeof(pi));
  CHECK(ufs2_plan_info(tree, NULL, &pi, err, sizeof(err)) == 0, "plan: %s", err);
  /* in the image: eboot.bin 1, empty.dat 0, big.pak, param.json, a, b, c, Readme.txt 10, README.TXT 11, "what: is this?" 12 */
  uint64_t data = 1 + 0 + ((3u << 20) + 12345) + 700 + 65536 + 65537 + 100000 + 10 + 11 + 12;
  CHECK(pi.data_bytes == data, "data_bytes %llu, expected %llu", (unsigned long long)pi.data_bytes, (unsigned long long)data);
  CHECK(pi.files == 10, "files %llu", (unsigned long long)pi.files);
  CHECK(pi.skipped_special == 1, "special %u", pi.skipped_special);       /* the link */
  CHECK(pi.skipped_system == 3, "system %u", pi.skipped_system);          /* desktop.ini, ._data, thumbs.db */
  CHECK(pi.skipped_example[0] != 0, "example name");
  CHECK(pi.image_size > 0 && pi.image_size % 65536 == 0, "image size %llu", (unsigned long long)pi.image_size);
  uint64_t only = 0;
  CHECK(ufs2_plan_size(tree, &only, err, sizeof(err)) == 0 && only == pi.image_size, "plan_size equals plan_info");

  /* C-02, C-04: written through, progress against the image size */
  int fd = open(img, O_RDWR | O_CREAT | O_TRUNC, 0666);
  uint64_t wsize = 0;
  ufs2_sums_t *sums = NULL;
  g_last = g_calls = 0; g_backwards = 0;
  CHECK(ufs2_write_tree(fd, tree, NULL, prog, NULL, &wsize, &sums, err, sizeof(err)) == 0, "write: %s", err);
  CHECK(wsize == pi.image_size, "written size %llu vs planned %llu", (unsigned long long)wsize, (unsigned long long)pi.image_size);
  CHECK(g_backwards == 0, "progress went backwards %d times", g_backwards);
  CHECK(g_last == pi.image_size, "progress ends at %llu, image %llu", (unsigned long long)g_last, (unsigned long long)pi.image_size);
  CHECK(g_calls >= 10, "progress calls %llu", (unsigned long long)g_calls);
  struct stat st;
  CHECK(fstat(fd, &st) == 0 && (uint64_t)st.st_size == wsize, "file size");
  off_t hole = lseek(fd, 0, SEEK_HOLE);
  CHECK(hole == (off_t)wsize, "first hole at %lld, file ends at %llu (no hole before the end)", (long long)hole, (unsigned long long)wsize);
  CHECK(ufs2_verify_tree(fd, sums, NULL, NULL, NULL, err, sizeof(err)) == 0, "verify: %s", err);
  CHECK(ufs2_sums_bytes(sums) == data, "sums bytes %llu", (unsigned long long)ufs2_sums_bytes(sums));
  ufs2_sums_free(sums);
  close(fd);

  /* a plain tree: nothing skipped */
  build_tree(tree, 0);
  CHECK(ufs2_plan_info(tree, NULL, &pi, err, sizeof(err)) == 0, "plan plain: %s", err);
  CHECK(pi.skipped_special == 0 && pi.skipped_system == 0 && pi.skipped_example[0] == 0, "nothing skipped");

  /* cancel: before the start, and in the middle of the zero-fill */
  int stop = 1;
  errno = 0;
  CHECK(ufs2_plan_info(tree, &stop, &pi, err, sizeof(err)) == -1 && errno == ECANCELED, "plan with cancel set");
  fd = open(img, O_RDWR | O_CREAT | O_TRUNC, 0666);
  errno = 0;
  CHECK(ufs2_write_tree(fd, tree, &stop, NULL, NULL, &wsize, NULL, err, sizeof(err)) == -1 && errno == ECANCELED, "write with cancel set");
  close(fd);
  CHECK(ufs2_plan_info(tree, NULL, &pi, err, sizeof(err)) == 0, "plan again");
  g_cancel_flag = 0;
  g_cancel_at = pi.image_size - pi.image_size / 8;          /* only the zero-fill gets this far */
  fd = open(img, O_RDWR | O_CREAT | O_TRUNC, 0666);
  errno = 0;
  CHECK(ufs2_write_tree(fd, tree, &g_cancel_flag, prog_cancel, NULL, &wsize, NULL, err, sizeof(err)) == -1 && errno == ECANCELED,
        "cancel in the zero-fill: %s", err);
  close(fd);

  fprintf(stderr, "ufs2plan: %d checks, %d failed\n", g_checks, g_fail);
  return g_fail ? 1 : 0;
#endif
}
