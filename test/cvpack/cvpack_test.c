/* Host test of src/cvpack.c: the request's choice to pack a game into LZ4 or zstd packs while an image is written (19.11.2026). */
#include <stdio.h>
#include <string.h>

#include "cvpack.h"
#include "third_party/cJSON.h"

static int fails, checks;
#define CHECK(c, ...) do { checks++; if(!(c)) { fails++; printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while(0)

/* the result of the body text: the status, kind, level, runtime */
static int
parse(const char *text, ps5tm_cvpack_t *p, char *err, size_t n) {
  cJSON *j = text ? cJSON_Parse(text) : NULL;
  err[0] = 0;
  int rc = ps5tm_cvpack_parse(j, p, err, n);
  cJSON_Delete(j);
  return rc;
}

int
main(void) {
  ps5tm_cvpack_t p;
  char err[200];

  /* no body, no field, "none", an empty text, null: no packing */
  CHECK(ps5tm_cvpack_parse(NULL, &p, err, sizeof(err)) == 0 && p.kind == 0 && p.level == 0 && p.runtime == 0, "no body");
  CHECK(parse("{}", &p, err, sizeof(err)) == 0 && p.kind == 0, "no field");
  CHECK(parse("{\"pack\":\"none\"}", &p, err, sizeof(err)) == 0 && p.kind == 0, "none");
  CHECK(parse("{\"pack\":\"\"}", &p, err, sizeof(err)) == 0 && p.kind == 0, "empty");
  CHECK(parse("{\"pack\":null}", &p, err, sizeof(err)) == 0 && p.kind == 0, "null");
  CHECK(parse("{\"pack\":\"none\",\"pack_level\":99,\"pack_runtime\":\"x\"}", &p, err, sizeof(err)) == 0 && p.kind == 0, "none reads nothing else");

  /* LZ4: the page's middle choice is 9, the runtime AMPR EMU NG */
  CHECK(parse("{\"pack\":\"lz4\"}", &p, err, sizeof(err)) == 0 && p.kind == 1 && p.level == 9 && p.runtime == 0, "lz4 defaults");
  CHECK(parse("{\"pack\":\"lz4\",\"pack_level\":3}", &p, err, sizeof(err)) == 0 && p.level == 3, "lz4 3");
  CHECK(parse("{\"pack\":\"lz4\",\"pack_level\":12}", &p, err, sizeof(err)) == 0 && p.level == 12, "lz4 12");
  CHECK(parse("{\"pack\":\"lz4\",\"pack_level\":0}", &p, err, sizeof(err)) == 0 && p.kind == 1 && p.level == 0, "lz4 0 is the fast level, not the default");
  CHECK(parse("{\"pack\":\"lz4\",\"pack_level\":1}", &p, err, sizeof(err)) == 400 && p.kind == 0 && err[0], "lz4 1 does not exist");
  CHECK(parse("{\"pack\":\"lz4\",\"pack_level\":2}", &p, err, sizeof(err)) == 400, "lz4 2 does not exist");
  CHECK(parse("{\"pack\":\"lz4\",\"pack_level\":13}", &p, err, sizeof(err)) == 400, "lz4 13 does not exist");
  CHECK(parse("{\"pack\":\"lz4\",\"pack_level\":-3}", &p, err, sizeof(err)) == 400, "negative");
  CHECK(parse("{\"pack\":\"lz4\",\"pack_level\":9.5}", &p, err, sizeof(err)) == 400, "fraction");
  CHECK(parse("{\"pack\":\"lz4\",\"pack_level\":\"9\"}", &p, err, sizeof(err)) == 400, "text");
  CHECK(parse("{\"pack\":\"lz4\",\"pack_level\":1e300}", &p, err, sizeof(err)) == 400 && p.kind == 0, "huge");
  CHECK(parse("{\"pack\":\"lz4\",\"pack_runtime\":\"ng\"}", &p, err, sizeof(err)) == 0 && p.runtime == 0, "ng");
  /* Dump Forge 0.6.0 packs LZ4 with AMPR EMU NG only: the old choice is refused with the reason (26.11.2026) */
  CHECK(parse("{\"pack\":\"lz4\",\"pack_runtime\":\"ampr_emu\"}", &p, err, sizeof(err)) == 400 && p.kind == 0 && p.runtime == 0 && strstr(err, "0.6.0"),
        "ampr_emu with LZ4 is refused");
  CHECK(parse("{\"pack\":\"lz4\",\"pack_runtime\":\"other\"}", &p, err, sizeof(err)) == 400 && p.kind == 0, "unknown runtime");
  CHECK(parse("{\"pack\":\"lz4\",\"pack_runtime\":3}", &p, err, sizeof(err)) == 400, "runtime as number");
  CHECK(parse("{\"pack\":\"lz4\",\"pack_runtime\":null,\"pack_level\":null}", &p, err, sizeof(err)) == 0 && p.level == 9 && p.runtime == 0, "null fields are absent");

  /* zstd: default 5, 1..22, only AMPR EMU NG */
  CHECK(parse("{\"pack\":\"zstd\"}", &p, err, sizeof(err)) == 0 && p.kind == 2 && p.level == 5 && p.runtime == 0, "zstd defaults");
  CHECK(parse("{\"pack\":\"zstd\",\"pack_level\":1}", &p, err, sizeof(err)) == 0 && p.level == 1, "zstd 1");
  CHECK(parse("{\"pack\":\"zstd\",\"pack_level\":19}", &p, err, sizeof(err)) == 0 && p.level == 19, "zstd 19");
  CHECK(parse("{\"pack\":\"zstd\",\"pack_level\":22}", &p, err, sizeof(err)) == 0 && p.level == 22, "zstd 22");
  CHECK(parse("{\"pack\":\"zstd\",\"pack_level\":0}", &p, err, sizeof(err)) == 400, "zstd 0 does not exist");
  CHECK(parse("{\"pack\":\"zstd\",\"pack_level\":23}", &p, err, sizeof(err)) == 400, "zstd 23 does not exist");
  CHECK(parse("{\"pack\":\"zstd\",\"pack_runtime\":\"ampr_emu\"}", &p, err, sizeof(err)) == 400 && p.kind == 0 && strstr(err, "zstd"), "zstd needs AMPR EMU NG");
  CHECK(parse("{\"pack\":\"zstd\",\"pack_runtime\":\"ng\"}", &p, err, sizeof(err)) == 0 && p.kind == 2, "zstd with ng");

  /* other kinds */
  CHECK(parse("{\"pack\":\"gzip\"}", &p, err, sizeof(err)) == 400 && p.kind == 0 && err[0], "unknown kind");
  CHECK(parse("{\"pack\":\"LZ4\"}", &p, err, sizeof(err)) == 400, "case matters: the pages send lower case");
  CHECK(parse("{\"pack\":true}", &p, err, sizeof(err)) == 400, "bool");
  CHECK(parse("{\"pack\":5}", &p, err, sizeof(err)) == 400, "number");

  /* a short error buffer is cut, not overrun (the address sanitizer watches) */
  char tiny[8];
  CHECK(parse("{\"pack\":\"gzip\"}", &p, tiny, sizeof(tiny)) == 400, "tiny buffer");

  printf("%d checks\n", checks);
  printf(fails ? "%d FAILED\n" : "all passed\n", fails);
  return fails != 0;
}
