/* SPDX-License-Identifier: GPL-3.0-or-later
 * Host test of the way a conversion uses src/fsign.c: the signed files go into a folder that is laid over the game as a second source
 * of the image writers (conv_exfat.c, conv_ufs2.c), the way gameconvert.c does it. The images are read back and searched for the
 * bytes: the SELF must be in them and the plain ELF must not. Run by test/fsign/run.sh.
 *
 *   e2e <workdir> */

#include <sys/types.h>

#include "conv_exfat.h"
#include "conv_ufs2.h"
#include "fsign.h"
#include "testlib.h"

static void *
find_bytes(const uint8_t *hay, size_t hn, const uint8_t *needle, size_t nn) {
  return memmem(hay, hn, needle, nn);
}

static size_t
count_bytes(const uint8_t *hay, size_t hn, const uint8_t *needle, size_t nn) {
  size_t c = 0;
  const uint8_t *p = hay;
  size_t left = hn;
  while(left >= nn && (p = memmem(p, left, needle, nn))) {
    c++;
    p++;
    left = hn - (size_t)(p - hay);
  }
  return c;
}

int
main(int argc, char **argv) {
  if(argc < 2) { fprintf(stderr, "usage: e2e <workdir>\n"); return 2; }
  const char *W = argv[1];
  char G[1024], S[1024], p[1024], err[400] = "";
  rm_rf(W);
  mk(W);
  snprintf(G, sizeof(G), "%s/game", W);
  snprintf(S, sizeof(S), "%s/game.ffpfsc" FSIGN_STAGE_SUFFIX, W);
  uint8_t *elf = make_elf(0xFE10);
  mk(G);
  snprintf(p, sizeof(p), "%s/sce_sys", G); mk(p);
  snprintf(p, sizeof(p), "%s/data", G); mk(p);
  snprintf(p, sizeof(p), "%s/eboot.bin", G); put_file(p, elf, ELF_LEN);
  snprintf(p, sizeof(p), "%s/sce_sys/param.json", G); put_file(p, "{\"titleId\":\"PPSA00000\"}", 23);
  uint8_t data[3000];
  for(unsigned i = 0; i < sizeof(data); i++) data[i] = (uint8_t)(i * 31 + 7);
  snprintf(p, sizeof(p), "%s/data/a.dat", G); put_file(p, data, sizeof(data));
  const char *paths[] = { "/app0/eboot.bin", "/app0/data/a.dat" };
  const uint64_t sizes[] = { ELF_LEN, sizeof(data) };
  snprintf(p, sizeof(p), "%s/ampr_emu.index", G);
  make_index(p, paths, sizes, 2);

  fsign_list_t l;
  CHECK(ps5tm_fsign_scan(G, NULL, &l) == 0 && l.n == 1);
  unsigned fx = 0;
  CHECK(ps5tm_fsign_stage(G, S, &l, NULL, NULL, NULL, &fx, err, sizeof(err)) == 0 && fx == 1);

  /* the signed file and the corrected index, as they are in the stage */
  size_t sn, in_, xn, x0n;
  snprintf(p, sizeof(p), "%s/eboot.bin", S);
  uint8_t *self = get_file(p, &sn);
  snprintf(p, sizeof(p), "%s/ampr_emu.index", S);
  uint8_t *idx = get_file(p, &xn);
  snprintf(p, sizeof(p), "%s/ampr_emu.index", G);
  uint8_t *idx0 = get_file(p, &x0n);
  CHECK(self && sn == 18917 && idx && idx0 && xn == x0n && memcmp(idx, idx0, xn) != 0);
  (void)in_;

  /* exFAT: game, then the stage over it */
  const char *roots[2] = { G, S };
  exfat_image_t *img = exfat_image_plan_roots(roots, 2, 0x1234567u, err, sizeof(err));
  CHECK(img != NULL);
  if(img) {
    uint64_t total = exfat_image_size(img);
    uint8_t *buf = malloc((size_t)total);
    uint64_t got = 0;
    for(;;) {
      ssize_t r = exfat_image_read(img, buf + got, (size_t)(total - got));
      if(r <= 0) break;
      got += (uint64_t)r;
    }
    CHECK(got == total);
    CHECK(count_bytes(buf, (size_t)got, self, sn) == 1);                    /* the SELF is in the image once ... */
    CHECK(find_bytes(buf, (size_t)got, elf, ELF_LEN) == NULL);               /* ... and the plain ELF is not */
    CHECK(count_bytes(buf, (size_t)got, data, sizeof(data)) == 1);           /* a file of the game itself is there */
    CHECK(count_bytes(buf, (size_t)got, idx, xn) == 1 && find_bytes(buf, (size_t)got, idx0, x0n) == NULL);   /* the corrected index only */
    free(buf);
    exfat_image_free(img);
  }
  /* without the stage the plain ELF is what the image would hold: the test sees the difference */
  img = exfat_image_plan(G, 0x1234567u, err, sizeof(err));
  CHECK(img != NULL);
  if(img) {
    uint64_t total = exfat_image_size(img);
    uint8_t *buf = malloc((size_t)total);
    uint64_t got = 0;
    for(;;) {
      ssize_t r = exfat_image_read(img, buf + got, (size_t)(total - got));
      if(r <= 0) break;
      got += (uint64_t)r;
    }
    CHECK(count_bytes(buf, (size_t)got, elf, ELF_LEN) == 1 && find_bytes(buf, (size_t)got, self, sn) == NULL);
    free(buf);
    exfat_image_free(img);
  }

  /* UFS2 (.ffpkg) */
  uint64_t psize = 0, wsize = 0;
  CHECK(ufs2_plan_size_roots(roots, 2, &psize, err, sizeof(err)) == 0);
  snprintf(p, sizeof(p), "%s/out.ffpkg", W);
  int fd = open(p, O_RDWR | O_CREAT | O_TRUNC, 0666);
  ufs2_sums_t *sums = NULL;
  CHECK(fd >= 0 && ufs2_write_tree_roots(fd, roots, 2, NULL, NULL, NULL, &wsize, &sums, err, sizeof(err)) == 0 && wsize == psize);
  if(fd >= 0) {
    uint8_t *buf = malloc((size_t)wsize);
    CHECK(pread(fd, buf, (size_t)wsize, 0) == (ssize_t)wsize);
    CHECK(count_bytes(buf, (size_t)wsize, self, sn) == 1);
    CHECK(find_bytes(buf, (size_t)wsize, elf, ELF_LEN) == NULL);
    CHECK(count_bytes(buf, (size_t)wsize, data, sizeof(data)) == 1);
    free(buf);
    close(fd);
  }
  ufs2_sums_free(sums);

  ps5tm_fsign_rmtree(S);
  CHECK(fsize(S) == 0);
  ps5tm_fsign_free(&l);
  free(elf); free(self); free(idx); free(idx0);
  fprintf(stderr, "e2e: %d checks, %d failed\n", g_checks, g_fail);
  return g_fail ? 1 : 0;
}
