/* Host test of the image walker in src/imgread.c (review of 23.11.2026): the whole tree of an exFAT and an .ffpkg (UFS2) image
 * the app's own writers made, then the exFAT image with a folder marked contiguous (NoFatChain, as other tools write them: such a
 * folder counted as unreadable), and with a folder entry bent into a loop and into a cross-link. A folder met a second time is
 * reported damaged once and not walked again; before, the loop ran 40 levels deep and the walk failed. */
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "conv_exfat.h"
#include "conv_ufs2.h"
#include "imgread.h"

static int g_fails;
#define CHECK(c, ...)                                                                                                          \
  do {                                                                                                                         \
    if(!(c)) {                                                                                                                 \
      g_fails++;                                                                                                               \
      printf("FAIL %s:%d: ", __FILE__, __LINE__);                                                                              \
      printf(__VA_ARGS__);                                                                                                     \
      printf("\n");                                                                                                            \
    }                                                                                                                          \
  } while(0)

static void
put(const char *path, const char *text) {
  int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if(fd < 0 || write(fd, text, strlen(text)) != (ssize_t)strlen(text)) { perror(path); exit(2); }
  close(fd);
}

static void
dir(const char *path) {
  if(mkdir(path, 0755) != 0 && errno != EEXIST) { perror(path); exit(2); }
}

typedef struct {
  int plain, dirs, damaged;  /* entries reported normally (folders among them), and reported again as damaged */
  char damaged_path[IMGR_PATH_MAX];
} count_t;

static int
count_cb(const imgr_entry_t *e, void *ctx) {
  count_t *c = ctx;
  if(e->damaged) {
    c->damaged++;
    snprintf(c->damaged_path, sizeof(c->damaged_path), "%s", e->path);
    return 0;
  }
  c->plain++;
  if(e->is_dir) c->dirs++;
  return 0;
}

static int
walk(const char *image, count_t *c) {
  char err[256];
  memset(c, 0, sizeof(*c));
  imgr_t *r = imgr_open(image, err, sizeof(err));
  if(!r) { printf("cannot open %s: %s\n", image, err); return -2; }
  int rc = imgr_walk(r, count_cb, c);
  imgr_close(r);
  return rc;
}

static uint8_t *
slurp(const char *path, size_t *len) {
  struct stat st;
  int fd = open(path, O_RDONLY);
  if(fd < 0 || fstat(fd, &st) != 0) { perror(path); exit(2); }
  uint8_t *b = malloc((size_t)st.st_size);
  if(!b || read(fd, b, (size_t)st.st_size) != st.st_size) { perror(path); exit(2); }
  close(fd);
  *len = (size_t)st.st_size;
  return b;
}

static void
spit(const char *path, const uint8_t *b, size_t len) {
  int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if(fd < 0 || write(fd, b, len) != (ssize_t)len) { perror(path); exit(2); }
  close(fd);
}

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static void wr32(uint8_t *p, uint32_t v) { for(int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i)); }

/* The stream extension (0xC0) of the folder entry named name (ASCII, at most 15 characters): its offset in the image, or 0. */
static size_t
stream_of(const uint8_t *img, size_t len, const char *name) {
  const size_t nl = strlen(name);
  for(size_t i = 0; i + 96 <= len; i += 32) {
    if(img[i] != 0x85 || img[i + 32] != 0xC0 || img[i + 64] != 0xC1 || img[i + 32 + 3] != nl) continue;
    int same = 1;
    for(size_t k = 0; k < nl && same; k++) same = img[i + 64 + 2 + 2 * k] == (uint8_t)name[k] && img[i + 64 + 3 + 2 * k] == 0;
    if(same) return i + 32;
  }
  return 0;
}

int
main(int argc, char **argv) {
  if(argc < 2) { fprintf(stderr, "usage: test <work folder>\n"); return 2; }
  const char *w = argv[1];
  char src[512], p[600], exf[600], bad[600], ufs[600];
  dir(w);
  snprintf(src, sizeof(src), "%s/src", w);
  dir(src);
  snprintf(p, sizeof(p), "%s/a", src); dir(p);
  snprintf(p, sizeof(p), "%s/a/b", src); dir(p);
  snprintf(p, sizeof(p), "%s/a/b/one.txt", src); put(p, "one");
  snprintf(p, sizeof(p), "%s/c", src); dir(p);
  snprintf(p, sizeof(p), "%s/c/two.txt", src); put(p, "two two");
  snprintf(p, sizeof(p), "%s/loop", src); dir(p);
  snprintf(p, sizeof(p), "%s/loop/three.txt", src); put(p, "three three three");
  /* 7 entries: a, a/b, a/b/one.txt, c, c/two.txt, loop, loop/three.txt; 4 of them folders */

  /* exFAT, as the app writes it */
  char err[256];
  snprintf(exf, sizeof(exf), "%s/t.exfat", w);
  exfat_image_t *im = exfat_image_plan(src, 0x12345678u, err, sizeof(err));
  if(!im) { printf("exfat plan: %s\n", err); return 2; }
  int fd = open(exf, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  uint8_t chunk[65536];
  ssize_t n;
  while((n = exfat_image_read(im, chunk, sizeof(chunk))) > 0)
    if(write(fd, chunk, (size_t)n) != n) { perror(exf); return 2; }
  close(fd);
  exfat_image_free(im);
  if(n < 0) { printf("exfat write failed\n"); return 2; }

  count_t c;
  int rc = walk(exf, &c);
  CHECK(rc == 0 && c.plain == 7 && c.dirs == 4 && c.damaged == 0, "exfat whole tree: rc %d, %d entries, %d folders, %d damaged", rc,
        c.plain, c.dirs, c.damaged);

  size_t len;
  uint8_t *img = slurp(exf, &len);
  const uint32_t root = rd32(img + 96);
  const size_t s_loop = stream_of(img, len, "loop"), s_a = stream_of(img, len, "a"), s_c = stream_of(img, len, "c");
  CHECK(s_loop && s_a && s_c, "folder entries not found (loop %zu, a %zu, c %zu)", s_loop, s_a, s_c);

  /* a contiguous folder (NoFatChain), as other tools write them: "a" is one cluster, so its data is the same; read by its length */
  uint8_t *b = malloc(len);
  memcpy(b, img, len);
  b[s_a + 1] |= 0x02;
  snprintf(bad, sizeof(bad), "%s/contig.exfat", w);
  spit(bad, b, len);
  rc = walk(bad, &c);
  CHECK(rc == 0 && c.plain == 7 && c.dirs == 4 && c.damaged == 0, "contiguous folder: rc %d, %d entries, %d folders, %d damaged (%s)", rc,
        c.plain, c.dirs, c.damaged, c.damaged_path);

  /* a loop: "loop" names the root's cluster */
  snprintf(bad, sizeof(bad), "%s/loop.exfat", w);
  memcpy(b, img, len);
  wr32(b + s_loop + 20, root);
  spit(bad, b, len);
  rc = walk(bad, &c);
  CHECK(rc == 0, "loop: the walk failed (rc %d) instead of skipping the folder met again", rc);
  CHECK(c.damaged == 1 && !strcmp(c.damaged_path, "/loop"), "loop: %d damaged (%s), expected /loop once", c.damaged, c.damaged_path);
  CHECK(c.plain == 6, "loop: %d entries, expected 6 (loop/three.txt is no longer reached)", c.plain);

  /* a cross-link: "c" names the data of "a" (first cluster, flags and length) */
  memcpy(b, img, len);
  memcpy(b + s_c + 1, img + s_a + 1, 1);
  memcpy(b + s_c + 20, img + s_a + 20, 12);
  spit(bad, b, len);
  rc = walk(bad, &c);
  CHECK(rc == 0, "cross-link: the walk failed (rc %d)", rc);
  CHECK(c.damaged == 1 && (!strcmp(c.damaged_path, "/a") || !strcmp(c.damaged_path, "/c")),
        "cross-link: %d damaged (%s), expected /a or /c once", c.damaged, c.damaged_path);
  CHECK(c.plain == 6, "cross-link: %d entries, expected 6", c.plain);
  free(b);
  free(img);

  /* .ffpkg (UFS2), as the app writes it: no folder of it has a hole, so the stricter folder read takes it whole */
  snprintf(ufs, sizeof(ufs), "%s/t.ffpkg", w);
  fd = open(ufs, O_RDWR | O_CREAT | O_TRUNC, 0644);
  uint64_t size = 0;
  ufs2_sums_t *sums = NULL;
  if(fd < 0 || ufs2_write_tree(fd, src, NULL, NULL, NULL, &size, &sums, err, sizeof(err)) != 0) { printf("ufs2 write: %s\n", err); return 2; }
  close(fd);
  ufs2_sums_free(sums);
  rc = walk(ufs, &c);
  CHECK(rc == 0 && c.plain == 7 && c.dirs == 4 && c.damaged == 0, "ffpkg whole tree: rc %d, %d entries, %d folders, %d damaged", rc,
        c.plain, c.dirs, c.damaged);

  if(g_fails) { printf("%d check(s) failed\n", g_fails); return 1; }
  printf("ok: imgwalk (exFAT tree, contiguous folder, loop, cross-link, ffpkg tree)\n");
  return 0;
}
