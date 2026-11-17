/* Host test of src/tvname.h: the name of the connected display out of the 1024 byte block of sceVideoOutGetMonitorInfo (16./17.11.2026).
   The block of a real console (Samsung TV of 2019, measured on the .52) starts 4c 2d f1 0f ...: an EDID without its 8 byte header. */
#include "tvname.h"

#include <stdio.h>
#include <stdlib.h>

static int g_fail;

#define CHECK(cond, ...) do { if(!(cond)) { printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); g_fail++; } } while(0)

/* A block like the console's: maker "SAM" (0x4C2D), product 0x0FF1, a serial number, week 1, year 2019 (0x1D), then the four descriptors of the base block at
   `desc0` + 18 * i: a timing, the ranges, the name (when given) and a serial text. `desc0` is 46 without the header (EDID offset 54 - 8) and 54 with it. */
static void
make_block(unsigned char *b, int with_header, const char *name_or_null) {
  memset(b, 0, PS5TM_EDID_SIZE);
  size_t id = 0, desc0 = 46;
  if(with_header) {
    const unsigned char hdr[8] = { 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00 };
    memcpy(b, hdr, 8);
    id = 8;
    desc0 = 54;
  }
  b[id] = 0x4C; b[id + 1] = 0x2D;                 /* S=19, A=1, M=13: 19<<10 | 1<<5 | 13 = 0x4C2D */
  b[id + 2] = 0xF1; b[id + 3] = 0x0F;
  b[id + 4] = 0x00; b[id + 5] = 0x0E; b[id + 6] = 0x00; b[id + 7] = 0x01;           /* the serial number: never logged */
  b[id + 8] = 0x01; b[id + 9] = 0x1D;
  static const unsigned char tags[4] = { 0x00, 0xFD, 0xFC, 0xFF };
  for(int i = 0; i < 4; i++) {
    unsigned char *d = b + desc0 + 18 * i;
    d[3] = tags[i];
    memset(d + 5, 0x20, 13);
  }
  if(name_or_null) {
    unsigned char *d = b + desc0 + 36;            /* the third descriptor */
    size_t n = strlen(name_or_null);
    memcpy(d + 5, name_or_null, n > 13 ? 13 : n);
    if(n < 13) d[5 + n] = 0x0A;
  } else {
    b[desc0 + 36 + 3] = 0xFE;                     /* no name descriptor: a text one */
  }
}

static void
name_is(const unsigned char *b, size_t n, int want_rc, const char *want) {
  char out[64];
  int rc = ps5tm_tvname_from_block(b, n, out, sizeof(out));
  CHECK(rc == want_rc && !strcmp(out, want), "rc=%d name=\"%s\", wanted rc=%d \"%s\"", rc, out, want_rc, want);
}

int
main(void) {
  unsigned char *b = malloc(PS5TM_EDID_SIZE);
  if(!b) return 2;
  char mk[4], desc[64];

  /* the console's block: no header */
  make_block(b, 0, "UE55RU7179");
  name_is(b, PS5TM_EDID_SIZE, 1, "UE55RU7179");
  CHECK(!ps5tm_edid_header_ok(b, PS5TM_EDID_SIZE), "header found in a headerless block");
  CHECK(ps5tm_edid_id_at(b, PS5TM_EDID_SIZE) == 0, "id at %d", ps5tm_edid_id_at(b, PS5TM_EDID_SIZE));
  CHECK(ps5tm_edid_maker(b, PS5TM_EDID_SIZE, mk) == 1 && !strcmp(mk, "SAM"), "maker \"%s\"", mk);
  CHECK(ps5tm_edid_product(b, PS5TM_EDID_SIZE) == 0x0FF1u, "product 0x%x", ps5tm_edid_product(b, PS5TM_EDID_SIZE));
  CHECK(ps5tm_edid_year(b, PS5TM_EDID_SIZE) == 2019u, "year %u", ps5tm_edid_year(b, PS5TM_EDID_SIZE));
  ps5tm_edid_descriptors(b, PS5TM_EDID_SIZE, desc, sizeof(desc));
  CHECK(!strcmp(desc, "fd@64 fc@82 ff@100"), "descriptors \"%s\"", desc);

  /* the same with the EDID header (the usual layout) */
  make_block(b, 1, "SAMSUNG");
  name_is(b, PS5TM_EDID_SIZE, 1, "SAMSUNG");
  CHECK(ps5tm_edid_header_ok(b, PS5TM_EDID_SIZE), "header not found");
  CHECK(ps5tm_edid_id_at(b, PS5TM_EDID_SIZE) == 8, "id at %d", ps5tm_edid_id_at(b, PS5TM_EDID_SIZE));
  CHECK(ps5tm_edid_maker(b, PS5TM_EDID_SIZE, mk) == 1 && !strcmp(mk, "SAM"), "maker with header \"%s\"", mk);
  CHECK(ps5tm_edid_product(b, PS5TM_EDID_SIZE) == 0x0FF1u && ps5tm_edid_year(b, PS5TM_EDID_SIZE) == 2019u, "product/year with header");
  ps5tm_edid_descriptors(b, PS5TM_EDID_SIZE, desc, sizeof(desc));
  CHECK(!strcmp(desc, "fd@72 fc@90 ff@108"), "descriptors with header \"%s\"", desc);

  /* names: the whole 13 characters, trailing blanks, too short, no name descriptor */
  make_block(b, 0, "ABCDEFGHIJKLM");
  name_is(b, PS5TM_EDID_SIZE, 1, "ABCDEFGHIJKLM");
  make_block(b, 0, "TV     ");
  name_is(b, PS5TM_EDID_SIZE, 0, "");                          /* "TV" is only two characters */
  make_block(b, 0, "ABC");
  name_is(b, PS5TM_EDID_SIZE, 1, "ABC");
  make_block(b, 0, NULL);
  name_is(b, PS5TM_EDID_SIZE, 0, "");
  ps5tm_edid_descriptors(b, PS5TM_EDID_SIZE, desc, sizeof(desc));
  CHECK(!strcmp(desc, "fd@64 fe@82 ff@100"), "descriptors without a name \"%s\"", desc);

  /* a short descriptor is passed over, the next real name is taken */
  make_block(b, 0, "TV");
  memcpy(b + 300, "\x00\x00\x00\xfc\x00" "BRAVIA\x0a", 5 + 7);
  name_is(b, PS5TM_EDID_SIZE, 1, "BRAVIA");
  /* the descriptor is looked for in the first 512 bytes only */
  make_block(b, 0, NULL);
  memcpy(b + 700, "\x00\x00\x00\xfc\x00" "FAR AWAY\x0a", 5 + 9);
  name_is(b, PS5TM_EDID_SIZE, 0, "");
  /* cut by a non-printable character */
  make_block(b, 0, "LG T");
  b[46 + 36 + 5 + 4] = 0x01; b[46 + 36 + 5 + 5] = 'X';
  name_is(b, PS5TM_EDID_SIZE, 1, "LG T");
  /* cut by the end of the block */
  memset(b, 0, 64);
  memcpy(b + 20, "\x00\x00\x00\xfc\x00" "ABCDE", 5 + 5);
  name_is(b, 27, 0, "");                                       /* the descriptor is cut after "AB" */
  name_is(b, 30, 1, "ABCDE");

  /* a refused call leaves the block as it was: all zero; garbage is no maker */
  memset(b, 0, PS5TM_EDID_SIZE);
  name_is(b, PS5TM_EDID_SIZE, 0, "");
  CHECK(ps5tm_edid_id_at(b, PS5TM_EDID_SIZE) == -1 && ps5tm_edid_maker(b, PS5TM_EDID_SIZE, mk) == 0 && !mk[0], "zero block has a maker");
  CHECK(ps5tm_edid_product(b, PS5TM_EDID_SIZE) == 0 && ps5tm_edid_year(b, PS5TM_EDID_SIZE) == 0, "zero block has a product or year");
  memset(b, 0xFF, PS5TM_EDID_SIZE);
  CHECK(ps5tm_edid_id_at(b, PS5TM_EDID_SIZE) == -1, "0xFF block has a maker");
  name_is(b, PS5TM_EDID_SIZE, 0, "");
  name_is(NULL, 128, 0, "");
  name_is(b, 0, 0, "");
  ps5tm_edid_descriptors(b, PS5TM_EDID_SIZE, desc, sizeof(desc));
  CHECK(!desc[0], "descriptors in 0xFF block: \"%s\"", desc);

  /* the year must be plausible */
  make_block(b, 0, "SAMSUNG");
  b[9] = 0x00;                                                  /* 1990 */
  CHECK(ps5tm_edid_year(b, PS5TM_EDID_SIZE) == 0, "year 1990");

  /* the buffer limits */
  make_block(b, 0, "ABCDEFGHIJKLM");
  char small[5];
  CHECK(ps5tm_tvname_from_block(b, PS5TM_EDID_SIZE, small, sizeof(small)) == 1 && !strcmp(small, "ABCD"), "small buffer \"%s\"", small);
  char one[1] = { 'x' };
  CHECK(ps5tm_tvname_from_block(b, PS5TM_EDID_SIZE, one, sizeof(one)) == 0, "buffer of one byte");
  make_block(b, 0, "SAMSUNG");
  char tiny[6];
  ps5tm_edid_descriptors(b, PS5TM_EDID_SIZE, tiny, sizeof(tiny));
  CHECK(strlen(tiny) < sizeof(tiny), "descriptors overran a small buffer");
  ps5tm_edid_descriptors(b, PS5TM_EDID_SIZE, tiny, 0);

  /* hex for the log: the serial number is left out in both places */
  {
    unsigned char x[16];
    for(int i = 0; i < 16; i++) x[i] = (unsigned char)i;
    char h[64];
    ps5tm_tvname_hex(x, 16, h, sizeof(h));
    CHECK(!strcmp(h, "00 01 02 03 xx xx xx xx 08 09 0a 0b xx xx xx xx"), "hex \"%s\"", h);
    char h2[8];
    ps5tm_tvname_hex(x, 16, h2, sizeof(h2));
    CHECK(!strcmp(h2, "00 01"), "hex cut \"%s\"", h2);
    ps5tm_tvname_hex(x, 0, h, sizeof(h));
    CHECK(!h[0], "hex empty");
  }

  free(b);
  if(g_fail) { printf("%d failed\n", g_fail); return 1; }
  printf("tvname: all checks passed\n");
  return 0;
}
