/* Host test of src/indexlimit.c: will the lookup tables of a zstd pack fit into AMPR EMU NG (the 48 MiB of v0.4.0). */
#include <stdio.h>
#include <string.h>

#include "indexlimit.h"

static int g_fail, g_n;
#define CHECK(c) do { g_n++; if(!(c)) { g_fail++; printf("FAIL line %d: %s\n", __LINE__, #c); } } while(0)

#define MIB (1ull << 20)

static void
put32(uint8_t *p, uint32_t v) { for(int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i)); }
static void
put64(uint8_t *p, uint64_t v) { for(int i = 0; i < 8; i++) p[i] = (uint8_t)(v >> (8 * i)); }

static void
header(uint8_t *h, uint32_t volumes, uint64_t files, uint64_t chunks) {
  memset(h, 0, 128);
  memcpy(h, "AMPRZPK1", 8);
  put32(h + 8, 1);
  put32(h + 12, 128);
  put32(h + 20, volumes);
  put64(h + 40, files);
  put64(h + 48, chunks);
}

static ps5tm_zstd_need_t
need(uint64_t files, uint64_t chunks, uint64_t volumes, uint64_t ids, uint64_t index_bytes, uint64_t ampr_bytes) {
  ps5tm_zstd_index_t in = { files, chunks, volumes, ids, index_bytes, ampr_bytes };
  ps5tm_zstd_need_t out;
  ps5tm_zstd_need(&in, &out);
  return out;
}

int
main(void) {
  uint8_t h[128];
  ps5tm_zstd_index_t ix;

  /* the header: the three numbers at their places */
  header(h, 7, 123456, 654321);
  CHECK(ps5tm_zstd_header(h, 128, &ix) == 0 && ix.volumes == 7 && ix.files == 123456 && ix.chunks == 654321);
  header(h, 0xFFFFFFFFu, 0x123456789ull, 0x2000000000ull);                 /* more than 32 bits */
  CHECK(ps5tm_zstd_header(h, 128, &ix) == 0 && ix.volumes == 0xFFFFFFFFu && ix.files == 0x123456789ull && ix.chunks == 0x2000000000ull);
  header(h, 1, 1, 1);
  h[0] = 'X';
  CHECK(ps5tm_zstd_header(h, 128, &ix) != 0);                              /* not our magic */
  header(h, 1, 1, 1);
  put32(h + 8, 2);
  CHECK(ps5tm_zstd_header(h, 128, &ix) != 0);                              /* another version */
  header(h, 1, 1, 1);
  put32(h + 12, 64);
  CHECK(ps5tm_zstd_header(h, 128, &ix) != 0);                              /* another header size */
  header(h, 1, 1, 1);
  CHECK(ps5tm_zstd_header(h, 127, &ix) != 0 && ps5tm_zstd_header(NULL, 128, &ix) != 0);

  /* a small game: read whole, only the derived tables count */
  ps5tm_zstd_need_t r = need(100000, 200000, 10, 120000, 10 * MIB, 4 * MIB);
  CHECK(r.derived == 16ull * 120000 + 4ull * 100000);
  CHECK(r.fast == 8ull * (2 * 100000 + 200000 + 10));
  CHECK(!r.paged && r.need == r.derived && !r.over);
  CHECK(r.room == 60 * MIB);

  /* a big one: paged, fast and derived tables together; 1.2 million files stay under 48 MiB ... */
  r = need(1200000, 600000, 20, 1200000, 110 * MIB, 1 * MIB);
  CHECK(r.paged);
  CHECK(r.need == 8ull * (2 * 1200000 + 600000 + 20) + 16ull * 1200000 + 4ull * 1200000);
  CHECK(r.need < PS5TM_NG_LOOKUP_MAX && !r.over);
  /* ... and 1.3 million are over: the case of Final Fantasy XVI (49 MiB against 48) */
  r = need(1300000, 600000, 20, 1300000, 120 * MIB, 10 * MIB);
  CHECK(r.paged && r.need > PS5TM_NG_LOOKUP_MAX && r.over);

  /* the page cache keeps 16 MiB: a big ampr_emu.index takes room the tables would need */
  r = need(1200000, 600000, 20, 1200000, 110 * MIB, 1 * MIB);             /* 63 MiB left: 45.8 + 16 fits */
  CHECK(!r.over);
  r = need(1200000, 600000, 20, 1200000, 110 * MIB, 10 * MIB);            /* 54 MiB left: 45.8 + 16 does not, though 45.8 is under 48 */
  CHECK(r.paged && r.need < PS5TM_NG_LOOKUP_MAX && r.over);

  /* without ampr_emu.index the ids are estimated as one per file */
  r = need(1000, 2000, 1, 0, MIB, 0);
  CHECK(r.derived == 16ull * 1000 + 4ull * 1000 && !r.paged && !r.over);

  /* fewer ids than files (a pack of files the AMPRIDX3 does not list): the order table follows the smaller number */
  r = need(1000, 2000, 1, 400, MIB, 0);
  CHECK(r.derived == 16ull * 400 + 4ull * 400);

  /* absurd numbers: no wrap-around into a small number */
  r = need(UINT64_MAX, UINT64_MAX, UINT64_MAX, UINT64_MAX, UINT64_MAX, UINT64_MAX);
  CHECK(r.over && r.paged);
  r = need(0x4000000000000000ull, 5, 5, 5, UINT64_MAX / 2, 0);
  CHECK(r.over && r.paged);

  /* an empty pack never trips it */
  r = need(0, 0, 0, 0, 128, 0);
  CHECK(!r.over && !r.paged && r.need == 0);

  /* the headroom: how far from refusing; negative when it refuses */
  r = need(1200000, 600000, 20, 1200000, 110 * MIB, 1 * MIB);            /* paged: 45.8 MiB of 48, 63 - 16 = 47 MiB left after the page cache */
  CHECK(r.headroom == (int64_t)(47 * MIB) - (int64_t)r.need);
  r = need(1000, 2000, 1, 1000, MIB, 0);                                  /* read whole: lots left */
  CHECK(!r.paged && r.headroom > 40 * (int64_t)MIB);

  /* the three states: two MiB either side of the limit are "tight" */
  { ps5tm_need_t n;
    memset(&n, 0, sizeof(n));
    n.headroom = 3 * (int64_t)MIB;  CHECK(!strcmp(ps5tm_need_state(&n), "ok"));
    n.headroom = 2 * (int64_t)MIB;  CHECK(!strcmp(ps5tm_need_state(&n), "tight"));
    n.headroom = 0;                 CHECK(!strcmp(ps5tm_need_state(&n), "tight"));
    n.headroom = -2 * (int64_t)MIB; CHECK(!strcmp(ps5tm_need_state(&n), "tight"));
    n.headroom = -3 * (int64_t)MIB; CHECK(!strcmp(ps5tm_need_state(&n), "over")); }

  /* the LZ4 header: files at 40, chunks at 48 (u64), packs at 56 (u32) */
  { uint8_t b[128];
    uint64_t f = 0, c = 0, p = 0;
    memset(b, 0, sizeof(b));
    memcpy(b, "AMPRPAK4", 8);
    put32(b + 8, 4);
    put64(b + 40, 223180);
    put64(b + 48, 1513210);
    put32(b + 56, 18);
    CHECK(ps5tm_lz4_header(b, 128, &f, &c, &p) == 0 && f == 223180 && c == 1513210 && p == 18);
    put32(b + 8, 3);
    CHECK(ps5tm_lz4_header(b, 128, &f, &c, &p) != 0);                    /* not version 4 */
    put32(b + 8, 4);
    b[0] = 'Z';
    CHECK(ps5tm_lz4_header(b, 128, &f, &c, &p) != 0 && ps5tm_lz4_header(b, 64, &f, &c, &p) != 0); }

  /* the LZ4 case of ampr-emu-ng issue #61, Demon's Souls, as its reporter's metrics give it: ampr_emu.index 34,373,632 B, 223,180 files,
     1,513,210 chunks, 18 packs; 52 B per file + 12 B per chunk + 32 B per pack = 28.39 MiB of tables, 31.2 MiB of room left: refused */
  { ps5tm_need_t n;
    ps5tm_lz4_need(223180, 1513210, 18, 42 * MIB, 34373632, &n);
    CHECK(n.need == 223180ull * 52 + 1513210ull * 12 + 18ull * 32);
    CHECK(n.paged && n.over && !strcmp(ps5tm_need_state(&n), "over"));
    /* the same game without the big ampr_emu.index would be read whole and fit: the room ampr_emu.index takes is what decides */
    ps5tm_lz4_need(223180, 1513210, 18, 42 * MIB, 0, &n);
    CHECK(!n.paged && !n.over);
    /* a small LZ4 pack is read whole */
    ps5tm_lz4_need(5000, 20000, 2, 2 * MIB, 3 * MIB, &n);
    CHECK(!n.paged && !n.over && n.need == 5000ull * 4); }

  printf("%d checks, %d failed\n", g_n, g_fail);
  return g_fail ? 1 : 0;
}
