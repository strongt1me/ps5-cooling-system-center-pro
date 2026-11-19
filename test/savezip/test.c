/* Host test of src/savezip.c: the ZIP a save travels in (write, read back, refuse the unsafe and the broken). */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "savezip.h"
#include "third_party/libdeflate/libdeflate.h"

static int g_fail, g_n;
#define CHECK(c) do { g_n++; if(!(c)) { g_fail++; printf("FAIL line %d: %s\n", __LINE__, #c); } } while(0)

static char g_dir[512];

static void
path(char *out, size_t n, const char *name) {
  snprintf(out, n, "%s/%s", g_dir, name);
}

static void
put_file(const char *name, const void *data, size_t len) {
  char p[700];
  path(p, sizeof(p), name);
  int fd = open(p, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if(fd < 0 || write(fd, data, len) != (ssize_t)len) { printf("setup failed: %s\n", p); exit(2); }
  close(fd);
}

static uint8_t *
slurp(const char *name, size_t *len) {
  char p[700];
  path(p, sizeof(p), name);
  FILE *f = fopen(p, "rb");
  if(!f) return NULL;
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  uint8_t *b = malloc((size_t)n + 1);
  if(fread(b, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(b); return NULL; }
  fclose(f);
  *len = (size_t)n;
  return b;
}

static int
find(szr_t *z, const char *name) {
  for(unsigned i = 0; i < szr_count(z); i++)
    if(!strcmp(szr_at(z, i)->name, name)) return (int)i;
  return -1;
}

static void put16(uint8_t *p, unsigned v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { put16(p, v & 0xffff); put16(p + 2, v >> 16); }

/* A ZIP by hand: one entry with the given name, method, payload (as stored in the file), sizes and checksum. */
static size_t
hand_zip(uint8_t *out, const char *name, unsigned method, const uint8_t *payload, size_t csize, size_t usize, uint32_t crc) {
  size_t p = 0, nl = strlen(name);
  memset(out, 0, 400 + csize);
  put32(out + p, 0x04034b50); put16(out + p + 4, 20); put16(out + p + 8, method); put32(out + p + 14, crc);
  put32(out + p + 18, (uint32_t)csize); put32(out + p + 22, (uint32_t)usize); put16(out + p + 26, (unsigned)nl);
  p += 30;
  memcpy(out + p, name, nl);
  p += nl;
  memcpy(out + p, payload, csize);
  p += csize;
  const size_t cd = p;
  put32(out + p, 0x02014b50); put16(out + p + 4, 20); put16(out + p + 6, 20); put16(out + p + 10, method); put32(out + p + 16, crc);
  put32(out + p + 20, (uint32_t)csize); put32(out + p + 24, (uint32_t)usize); put16(out + p + 28, (unsigned)nl); put32(out + p + 42, 0);
  p += 46;
  memcpy(out + p, name, nl);
  p += nl;
  put32(out + p, 0x06054b50); put16(out + p + 8, 1); put16(out + p + 10, 1); put32(out + p + 12, (uint32_t)(p - cd - 0)); put32(out + p + 16, (uint32_t)cd);
  put32(out + p + 12, (uint32_t)(46 + nl));
  return p + 22;
}

static int g_tick_bytes, g_tick_append, g_tick_stop;
static char g_tick_file[700];
static int
tick(void *ctx, uint64_t bytes) {
  (void)ctx;
  g_tick_bytes += (int)bytes;
  if(g_tick_append) {
    FILE *f = fopen(g_tick_file, "ab");
    fputs("more", f);
    fclose(f);
  }
  return g_tick_stop;
}

int
main(int argc, char **argv) {
  snprintf(g_dir, sizeof(g_dir), "%s", argc > 1 ? argv[1] : "/tmp/ps5tm-savezip");
  char cmd[1200];
  snprintf(cmd, sizeof(cmd), "rm -rf '%s' && mkdir -p '%s'", g_dir, g_dir);
  if(system(cmd) != 0) return 2;

  /* ---- names */
  CHECK(savezip_name_ok("sce_sys/param.sfo") && savezip_name_ok("a") && savezip_name_ok("dir/sub/file.dat") && savezip_name_ok("Ü ber/Spiel.dat"));
  CHECK(!savezip_name_ok("") && !savezip_name_ok(NULL) && !savezip_name_ok("/etc/passwd") && !savezip_name_ok("../x") && !savezip_name_ok("a/../b") &&
        !savezip_name_ok("a/./b") && !savezip_name_ok("a//b") && !savezip_name_ok("a\\b") && !savezip_name_ok("C:\\x") && !savezip_name_ok("C:/x") &&
        !savezip_name_ok("a\nb") && !savezip_name_ok("a\x7f") && !savezip_name_ok(".."));
  char longname[600];
  memset(longname, 'a', sizeof(longname));
  longname[SZ_MAX_NAME + 1] = '\0';
  CHECK(!savezip_name_ok(longname));
  longname[SZ_MAX_NAME] = '\0';
  CHECK(savezip_name_ok(longname));

  /* ---- write */
  uint8_t big[300000];
  for(size_t i = 0; i < sizeof(big); i++) big[i] = (uint8_t)((i * 2654435761u) >> 13);
  put_file("save.bin", big, sizeof(big));
  put_file("empty.dat", "", 0);
  char zp[700], src[700];
  path(zp, sizeof(zp), "out.zip");
  szw_t *w = szw_create(zp);
  CHECK(w != NULL);
  CHECK(szw_create(zp) == NULL && errno == EEXIST);                 /* never over a file that is there */
  CHECK(szw_add_dir(w, "sce_sys", 1700000000, 0755) == 0);
  CHECK(szw_add_mem(w, "sce_sys/param.sfo", "PSFdata", 7, 1700000000, 0644) == 0);
  path(src, sizeof(src), "save.bin");
  g_tick_bytes = 0; g_tick_append = 0; g_tick_stop = 0;
  CHECK(szw_add_file(w, "slot/save.bin", src, 1700000100, 0600, tick, NULL) == 0 && g_tick_bytes == (int)sizeof(big));
  path(src, sizeof(src), "empty.dat");
  CHECK(szw_add_file(w, "leer.dat", src, 0, 0644, NULL, NULL) == 0);
  CHECK(szw_add_mem(w, "chives.json", "{}", 2, 1700000000, 0644) == 0);
  CHECK(szw_add_mem(w, "../boese", "x", 1, 0, 0644) == SZ_E_NAME);
  CHECK(szw_add_dir(w, "/abs", 0, 0755) == SZ_E_NAME);
  CHECK(szw_count(w) == 5 && szw_size(w) > sizeof(big));
  CHECK(szw_finish(w) == 0);

  /* ---- read it back */
  int err = 0;
  szr_t *z = szr_open(zp, &err);
  CHECK(z && err == 0 && szr_count(z) == 5);
  int i_sfo = find(z, "sce_sys/param.sfo"), i_bin = find(z, "slot/save.bin"), i_dir = find(z, "sce_sys"), i_leer = find(z, "leer.dat");
  CHECK(i_sfo >= 0 && i_bin >= 0 && i_dir >= 0 && i_leer >= 0);
  CHECK(szr_at(z, (unsigned)i_dir)->is_dir && !szr_at(z, (unsigned)i_bin)->is_dir);
  CHECK(((szr_at(z, (unsigned)i_bin)->attr >> 16) & 0777) == 0600 && ((szr_at(z, (unsigned)i_dir)->attr >> 16) & 0777) == 0755);
  CHECK(szr_at(z, (unsigned)i_bin)->usize == sizeof(big) && szr_at(z, (unsigned)i_bin)->method == 0);
  uint8_t *d;
  size_t dl;
  CHECK(szr_read(z, (unsigned)i_sfo, &d, &dl, 1000) == 0 && dl == 7 && !memcmp(d, "PSFdata", 7));
  free(d);
  CHECK(szr_read(z, (unsigned)i_bin, &d, &dl, 1000) == SZ_E_TOO_BIG);
  CHECK(szr_read(z, (unsigned)i_bin, &d, &dl, 1u << 20) == 0 && dl == sizeof(big) && !memcmp(d, big, dl));
  free(d);
  CHECK(szr_read(z, (unsigned)i_leer, &d, &dl, 10) == 0 && dl == 0);
  free(d);
  CHECK(szr_read(z, (unsigned)i_dir, &d, &dl, 10) == SZ_E_BAD);      /* a folder has no data */
  char op[700];
  path(op, sizeof(op), "extracted.bin");
  int fd = open(op, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  g_tick_bytes = 0;
  CHECK(szr_extract(z, (unsigned)i_bin, fd, 1u << 20, tick, NULL) == 0);
  close(fd);
  size_t xl;
  uint8_t *x = slurp("extracted.bin", &xl);
  CHECK(x && xl == sizeof(big) && !memcmp(x, big, xl) && g_tick_bytes == (int)sizeof(big));
  free(x);
  g_tick_stop = 1;
  fd = open(op, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  CHECK(szr_extract(z, (unsigned)i_bin, fd, 1u << 20, tick, NULL) == SZ_E_STOP);
  close(fd);
  g_tick_stop = 0;
  szr_close(z);

  /* ---- a stopped writer leaves nothing */
  path(zp, sizeof(zp), "stop.zip");
  w = szw_create(zp);
  path(src, sizeof(src), "save.bin");
  g_tick_stop = 1;
  CHECK(szw_add_file(w, "x", src, 0, 0644, tick, NULL) == SZ_E_STOP);
  szw_abort(w);
  CHECK(access(zp, F_OK) != 0);
  g_tick_stop = 0;

  /* ---- a file that grows while it is read */
  path(zp, sizeof(zp), "grow.zip");
  w = szw_create(zp);
  path(g_tick_file, sizeof(g_tick_file), "save.bin");
  g_tick_append = 1;
  CHECK(szw_add_file(w, "grow", g_tick_file, 0, 0644, tick, NULL) == SZ_E_CHANGED);
  g_tick_append = 0;
  szw_abort(w);
  w = szw_create(zp);
  CHECK(szw_add_file(w, "fehlt", "/nicht/da", 0, 0644, NULL, NULL) == SZ_E_IO);
  CHECK(szw_add_file(w, "ordner", g_dir, 0, 0644, NULL, NULL) == SZ_E_IO);   /* a folder is no file */
  szw_abort(w);

  /* ---- a deflated entry (as other tools write them) */
  uint8_t text[2000];
  for(size_t i = 0; i < sizeof(text); i++) text[i] = (uint8_t)('a' + (i / 50) % 7);
  struct libdeflate_compressor *c = libdeflate_alloc_compressor(6);
  uint8_t comp[3000];
  size_t cl = libdeflate_deflate_compress(c, text, sizeof(text), comp, sizeof(comp));
  libdeflate_free_compressor(c);
  CHECK(cl > 0 && cl < sizeof(text));
  const uint32_t tcrc = libdeflate_crc32(0, text, sizeof(text));
  uint8_t hz[4000];
  size_t hl = hand_zip(hz, "spiel/deflate.txt", 8, comp, cl, sizeof(text), tcrc);
  put_file("hand.zip", hz, hl);
  path(zp, sizeof(zp), "hand.zip");
  z = szr_open(zp, &err);
  CHECK(z && szr_count(z) == 1 && szr_at(z, 0)->method == 8);
  CHECK(z && szr_read(z, 0, &d, &dl, 5000) == 0 && dl == sizeof(text) && !memcmp(d, text, dl));
  free(d);
  if(z) {
    fd = open(op, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    CHECK(szr_extract(z, 0, fd, 5000, NULL, NULL) == 0);
    close(fd);
    x = slurp("extracted.bin", &xl);
    CHECK(x && xl == sizeof(text) && !memcmp(x, text, xl));
    free(x);
    fd = open(op, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    CHECK(szr_extract(z, 0, fd, 100, NULL, NULL) == SZ_E_TOO_BIG);      /* the memory limit */
    close(fd);
  }
  szr_close(z);

  /* ---- broken and unsafe archives */
  hl = hand_zip(hz, "../evil", 0, (const uint8_t *)"abc", 3, 3, libdeflate_crc32(0, "abc", 3));
  put_file("slip.zip", hz, hl);
  path(zp, sizeof(zp), "slip.zip");
  CHECK(szr_open(zp, &err) == NULL && err == SZ_E_NAME);
  hl = hand_zip(hz, "/abs/x", 0, (const uint8_t *)"abc", 3, 3, 0);
  put_file("abs.zip", hz, hl);
  path(zp, sizeof(zp), "abs.zip");
  CHECK(szr_open(zp, &err) == NULL && err == SZ_E_NAME);
  hl = hand_zip(hz, "a\\b", 0, (const uint8_t *)"abc", 3, 3, 0);
  put_file("bs.zip", hz, hl);
  path(zp, sizeof(zp), "bs.zip");
  CHECK(szr_open(zp, &err) == NULL && err == SZ_E_NAME);
  hl = hand_zip(hz, "ok.txt", 0, (const uint8_t *)"abc", 3, 3, 0x12345678);                 /* a wrong checksum */
  put_file("crc.zip", hz, hl);
  path(zp, sizeof(zp), "crc.zip");
  z = szr_open(zp, &err);
  CHECK(z != NULL);
  CHECK(z && szr_read(z, 0, &d, &dl, 100) == SZ_E_CRC);
  szr_close(z);
  hl = hand_zip(hz, "m.txt", 12, (const uint8_t *)"abc", 3, 3, 0);                            /* bzip2: not taken */
  put_file("method.zip", hz, hl);
  path(zp, sizeof(zp), "method.zip");
  CHECK(szr_open(zp, &err) == NULL && err == SZ_E_BAD);
  hl = hand_zip(hz, "ok.txt", 0, (const uint8_t *)"abc", 3, 3, libdeflate_crc32(0, "abc", 3));
  put_file("trunc.zip", hz, hl - 5);                                                         /* the end record cut */
  path(zp, sizeof(zp), "trunc.zip");
  CHECK(szr_open(zp, &err) == NULL && err == SZ_E_BAD);
  put_file("nozip.zip", "this is not a zip file at all, just text, and long enough to look", 60);
  path(zp, sizeof(zp), "nozip.zip");
  CHECK(szr_open(zp, &err) == NULL && err == SZ_E_BAD);
  put_file("tiny.zip", "PK", 2);
  path(zp, sizeof(zp), "tiny.zip");
  CHECK(szr_open(zp, &err) == NULL && err == SZ_E_BAD);
  path(zp, sizeof(zp), "gibtsnicht.zip");
  CHECK(szr_open(zp, &err) == NULL && err == SZ_E_IO);
  /* a local header that points past the file */
  hl = hand_zip(hz, "ok.txt", 0, (const uint8_t *)"abc", 3, 3, libdeflate_crc32(0, "abc", 3));
  {
    const size_t cd = 30 + strlen("ok.txt") + 3;
    put32(hz + cd + 42, 5000);                                                               /* local header offset */
  }
  put_file("lho.zip", hz, hl);
  path(zp, sizeof(zp), "lho.zip");
  z = szr_open(zp, &err);
  CHECK(z && szr_read(z, 0, &d, &dl, 100) == SZ_E_BAD);
  szr_close(z);
  /* ZIP64 marker: refused */
  hl = hand_zip(hz, "ok.txt", 0, (const uint8_t *)"abc", 3, 3, libdeflate_crc32(0, "abc", 3));
  put32(hz + hl - 22 + 16, 0xFFFFFFFFu);
  put_file("z64.zip", hz, hl);
  path(zp, sizeof(zp), "z64.zip");
  CHECK(szr_open(zp, &err) == NULL && err == SZ_E_BAD);

  printf("%d checks, %d failed\n", g_n, g_fail);
  snprintf(cmd, sizeof(cmd), "rm -rf '%s'", g_dir);
  if(system(cmd) != 0) return 2;
  return g_fail ? 1 : 0;
}
