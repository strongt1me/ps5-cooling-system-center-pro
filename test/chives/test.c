/* Host test of src/chives.c: the sidecar of an exported save (geometry from an image header, written, read back, refused when it lies). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chives.h"

static int g_fail, g_n;
#define CHECK(c) do { g_n++; if(!(c)) { g_fail++; printf("FAIL line %d: %s\n", __LINE__, #c); } } while(0)

static void
put(uint8_t *p, uint64_t v, int n) {
  for(int i = 0; i < n; i++) p[i] = (uint8_t)(v >> (8 * i));
}

static void
header(uint8_t *h, uint64_t version, unsigned bs, uint64_t blocks, unsigned fs_ext) {
  memset(h, 0, 0x370);
  put(h, version, 8);
  put(h + 8, 20130315, 8);
  put(h + 0x1c, 0x1234, 2);
  put(h + 0x20, bs, 4);
  put(h + 0x24, 3, 4);
  put(h + 0x38, blocks, 8);
  put(h + 0x368, fs_ext, 4);
}

int
main(void) {
  uint8_t h[0x370];
  chives_t g;

  /* geometry from a header */
  header(h, 2, 32768, 1000, 0);
  CHECK(chives_from_header(h, sizeof(h), 32768ull * 1000, &g) == 0);
  CHECK(g.version == 2 && g.block_size == 32768 && g.block_count == 1000 && g.mode_raw == 0x1234 && g.backup_header_count == 3);
  CHECK(g.image_size == 32768ull * 1000 && g.has_fs_ext && g.fs_ext == 0);
  header(h, 2, 65536, 500, 7);
  CHECK(chives_from_header(h, sizeof(h), 65536ull * 500, &g) == 0 && g.fs_ext == 7);
  header(h, 2, 32768, 500, 7);
  CHECK(chives_from_header(h, sizeof(h), 32768ull * 500, &g) != 0);              /* fs_ext only with 64 KiB blocks */
  header(h, 2, 65536, 500, 500);
  CHECK(chives_from_header(h, sizeof(h), 65536ull * 500, &g) != 0);              /* fs_ext beyond the blocks */
  header(h, 1, 32768, 1000, 0);
  CHECK(chives_from_header(h, sizeof(h), 1 << 20, &g) != 0);                     /* PS4 */
  header(h, 2, 4096, 1000, 0);
  CHECK(chives_from_header(h, sizeof(h), 1 << 20, &g) != 0);                     /* a block size no save has */
  header(h, 2, 32768, 0, 0);
  CHECK(chives_from_header(h, sizeof(h), 1 << 20, &g) != 0);                     /* no blocks */
  header(h, 2, 32768, 1ull << 40, 0);
  CHECK(chives_from_header(h, sizeof(h), 1 << 20, &g) != 0);                     /* more than any image */
  header(h, 2, 32768, 1000, 0);
  put(h + 8, 5, 8);
  CHECK(chives_from_header(h, sizeof(h), 1 << 20, &g) != 0);                     /* not a PFS */
  header(h, 2, 32768, 1000, 0);
  CHECK(chives_from_header(h, 0x20, 1 << 20, &g) != 0);                          /* too short */
  CHECK(chives_from_header(h, 0x40, 32768ull * 1000, &g) == 0 && !g.has_fs_ext); /* no room for fs_ext */
  CHECK(chives_from_header(h, sizeof(h), 0x10, &g) != 0);                        /* an image of 16 bytes */

  /* paths */
  CHECK(chives_path_safe("sce_sys/param.sfo") && chives_path_safe("a") && chives_path_safe("a/b/c.dat"));
  CHECK(!chives_path_safe("") && !chives_path_safe("/a") && !chives_path_safe("../a") && !chives_path_safe("a/../b") && !chives_path_safe("a//b"));
  CHECK(!chives_path_safe("a/./b") && !chives_path_safe("a\\b") && !chives_path_safe("C:x") && !chives_path_safe("a/") && !chives_path_safe("a\nb"));
  CHECK(chives_is_sidecar("chives.json") && chives_is_sidecar("./CHIVES.JSON") && chives_is_sidecar("././Chives.Json"));
  CHECK(!chives_is_sidecar("chives.json.bak") && !chives_is_sidecar("x/chives.json") && !chives_is_sidecar("chives"));

  /* write, read back */
  header(h, 2, 65536, 800, 9);
  chives_t w;
  CHECK(chives_from_header(h, sizeof(h), 65536ull * 800 + 4096, &w) == 0);
  snprintf(w.title_id, sizeof(w.title_id), "PPSA01234");
  snprintf(w.save_dir, sizeof(w.save_dir), "SAVE0");
  snprintf(w.image_name, sizeof(w.image_name), "sdimg_SAVE0");
  CHECK(chives_own_add(&w, "sce_sys", 1, 2, 0755) == 0);
  CHECK(chives_own_add(&w, "sce_sys/param.sfo", 10, 20, 0640) == 0);
  CHECK(chives_own_add(&w, "data \"q\".bin", 11, 21, 0600) == 0 && chives_own_add(&w, "a\\b", 0, 0, 0600) != 0);
  CHECK(chives_own_add(&w, "../evil", 0, 0, 0600) != 0 && chives_own_add(&w, "ok", 0, 0, 010000) != 0);
  CHECK(w.nown == 3);
  char json[8192];
  int n = chives_write(&w, json, sizeof(json));
  CHECK(n > 100 && (size_t)n == strlen(json));
  chives_t r;
  CHECK(chives_parse(json, (size_t)n, &r) == 0);
  CHECK(r.version == 2 && r.block_size == 65536 && r.block_count == 800 && r.mode_raw == 0x1234 && r.backup_header_count == 3 && r.fs_ext == 9);
  CHECK(r.image_size == 65536ull * 800 + 4096);
  CHECK(!strcmp(r.title_id, "PPSA01234") && !strcmp(r.save_dir, "SAVE0") && !strcmp(r.image_name, "sdimg_SAVE0"));
  CHECK(r.nown == 3 && !strcmp(r.own[0].path, "sce_sys") && r.own[0].uid == 1 && r.own[0].gid == 2 && r.own[0].mode == 0755);
  CHECK(!strcmp(r.own[1].path, "sce_sys/param.sfo") && r.own[1].uid == 10 && r.own[1].mode == 0640);
  CHECK(!strcmp(r.own[2].path, "data \"q\".bin") && r.own[2].gid == 21);               /* escaped and read back */
  chives_free(&r);
  CHECK(chives_write(&w, json, 50) == -1);                                                /* does not fit */
  CHECK(chives_write(&w, json, (size_t)n) == -1);                                         /* does not fit by the terminator */

  /* more entries than the other tool takes: the list is left out, the geometry stays */
  for(unsigned i = 0; i < 300; i++) {
    char p[40];
    snprintf(p, sizeof(p), "f/%u.dat", i);
    CHECK(chives_own_add(&w, p, 1, 1, 0600) == 0);
  }
  char *big = malloc(262144);
  n = chives_write(&w, big, 262144);
  CHECK(n > 0 && !strstr(big, "ownership"));
  CHECK(chives_parse(big, (size_t)n, &r) == 0 && r.nown == 0 && r.block_count == 800);
  chives_free(&r);
  free(big);
  chives_free(&w);

  /* a sidecar written by the other tool: more keys, no fs_ext, no ownership */
  const char *other =
    "{\"schema_version\":1,\"platform\":\"PS5\",\"title_id\":\"PPSA00001\",\"save_directory\":\"SAVEDATA\",\"save_format\":\"x\","
    "\"encrypted_name\":\"sdimg_SAVEDATA\",\"original_image_size_bytes\":4194304,"
    "\"pfs\":{\"version\":2,\"block_size_bytes\":32768,\"block_count\":128,\"mode_raw\":5,\"backup_header_count\":1},"
    "\"sfo\":{\"savedata_blocks\":3,\"system_blocks\":4},\"crypto\":{\"a\":[1,2,{\"b\":null}]}}";
  CHECK(chives_parse(other, strlen(other), &r) == 0 && r.block_count == 128 && !r.has_fs_ext && r.nown == 0);
  CHECK(!strcmp(r.title_id, "PPSA00001"));
  chives_free(&r);

  /* a sidecar that lies */
  const char *bad[] = {
    "", "{", "[]", "null", "{}",
    "{\"schema_version\":2,\"platform\":\"PS5\",\"original_image_size_bytes\":4194304,\"pfs\":{\"version\":2,\"block_size_bytes\":32768,\"block_count\":128}}",
    "{\"schema_version\":1,\"platform\":\"PS4\",\"original_image_size_bytes\":4194304,\"pfs\":{\"version\":2,\"block_size_bytes\":32768,\"block_count\":128}}",
    "{\"schema_version\":1,\"platform\":\"PS5\",\"original_image_size_bytes\":4194304,\"pfs\":{\"version\":1,\"block_size_bytes\":32768,\"block_count\":128}}",
    "{\"schema_version\":1,\"platform\":\"PS5\",\"original_image_size_bytes\":4194304,\"pfs\":{\"version\":2,\"block_size_bytes\":1000,\"block_count\":128}}",
    "{\"schema_version\":1,\"platform\":\"PS5\",\"original_image_size_bytes\":4194304,\"pfs\":{\"version\":2,\"block_size_bytes\":32768,\"block_count\":-5}}",
    "{\"schema_version\":1,\"platform\":\"PS5\",\"original_image_size_bytes\":4194304,\"pfs\":{\"version\":2,\"block_size_bytes\":32768,\"block_count\":1.5}}",
    "{\"schema_version\":1,\"platform\":\"PS5\",\"original_image_size_bytes\":4194304,\"pfs\":{\"version\":2,\"block_size_bytes\":32768,\"block_count\":1e30}}",
    "{\"schema_version\":1,\"platform\":\"PS5\",\"original_image_size_bytes\":4194304,\"pfs\":{\"version\":2,\"block_size_bytes\":32768}}",
    "{\"schema_version\":1,\"platform\":\"PS5\",\"original_image_size_bytes\":4194304}",
    "{\"schema_version\":1,\"platform\":\"PS5\",\"original_image_size_bytes\":\"big\",\"pfs\":{\"version\":2,\"block_size_bytes\":32768,\"block_count\":128}}",
    "{\"schema_version\":1,\"platform\":\"PS5\",\"encrypted_name\":\"../x\",\"original_image_size_bytes\":4194304,\"pfs\":{\"version\":2,\"block_size_bytes\":32768,\"block_count\":128}}",
    "{\"schema_version\":1,\"platform\":\"PS5\",\"encrypted_name\":\"a/b\",\"original_image_size_bytes\":4194304,\"pfs\":{\"version\":2,\"block_size_bytes\":32768,\"block_count\":128}}",
    "{\"schema_version\":1,\"platform\":\"PS5\",\"original_image_size_bytes\":4194304,\"pfs\":{\"version\":2,\"block_size_bytes\":32768,\"block_count\":128},\"ownership\":{}}",
    "{\"schema_version\":1,\"platform\":\"PS5\",\"original_image_size_bytes\":4194304,\"pfs\":{\"version\":2,\"block_size_bytes\":32768,\"block_count\":128},\"ownership\":[{\"path\":\"../a\",\"uid\":0,\"gid\":0,\"mode\":420}]}",
    "{\"schema_version\":1,\"platform\":\"PS5\",\"original_image_size_bytes\":4194304,\"pfs\":{\"version\":2,\"block_size_bytes\":32768,\"block_count\":128},\"ownership\":[{\"path\":\"a\",\"uid\":0,\"gid\":0,\"mode\":99999}]}",
    "{\"schema_version\":1,\"platform\":\"PS5\",\"original_image_size_bytes\":4194304,\"pfs\":{\"version\":2,\"block_size_bytes\":32768,\"block_count\":128},\"ownership\":[{\"path\":\"a\",\"uid\":0,\"gid\":0}]}",
    "{\"schema_version\":1,\"platform\":\"PS5\",\"original_image_size_bytes\":4194304,\"pfs\":{\"version\":2,\"block_size_bytes\":32768,\"block_count\":128},\"ownership\":[{\"path\":\"a\",\"uid\":0,\"gid\":0,\"mode\":1},{\"path\":\"a\",\"uid\":0,\"gid\":0,\"mode\":2}]}",
    "{\"schema_version\":1,\"platform\":\"PS5\",\"original_image_size_bytes\":4194304,\"pfs\":{\"version\":2,\"block_size_bytes\":32768,\"block_count\":128},\"ownership\":[{\"path\":\"a\",\"uid\":4294967296,\"gid\":0,\"mode\":1}]}",
  };
  for(unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
    chives_t x;
    int rc = chives_parse(bad[i], strlen(bad[i]), &x);
    if(rc != 0) CHECK(x.own == NULL && x.nown == 0);
    else { printf("  accepted: %u\n", i); chives_free(&x); }
    CHECK(rc != 0);
  }
  CHECK(chives_parse(NULL, 5, &r) != 0 && chives_parse("{}", 0, &r) != 0);
  CHECK(chives_parse(other, CHIVES_MAX_JSON + 1, &r) != 0);                               /* above the limit: refused before it is read */

  /* many entries read (up to the limit) */
  {
    size_t cap = 400000;
    char *js = malloc(cap);
    int pos = snprintf(js, cap, "{\"schema_version\":1,\"platform\":\"PS5\",\"original_image_size_bytes\":4194304,"
                                "\"pfs\":{\"version\":2,\"block_size_bytes\":32768,\"block_count\":128},\"ownership\":[");
    for(unsigned i = 0; i < 2048; i++)
      pos += snprintf(js + pos, cap - (size_t)pos, "%s{\"path\":\"d/%u\",\"uid\":1,\"gid\":2,\"mode\":420}", i ? "," : "", i);
    pos += snprintf(js + pos, cap - (size_t)pos, "]}");
    CHECK((size_t)pos < CHIVES_MAX_JSON);
    CHECK(chives_parse(js, (size_t)pos, &r) == 0 && r.nown == 2048);
    chives_free(&r);
    pos -= 2;
    pos += snprintf(js + pos, cap - (size_t)pos, ",{\"path\":\"extra\",\"uid\":1,\"gid\":2,\"mode\":420}]}");
    CHECK(chives_parse(js, (size_t)pos, &r) != 0);                                        /* one more than the limit */
    free(js);
  }

  printf("%d checks, %d failed\n", g_n, g_fail);
  return g_fail ? 1 : 0;
}
