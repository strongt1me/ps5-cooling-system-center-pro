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

  /* ---- ampr-emu-ng v0.4.1: 80 MiB of index memory, no 48 MiB limit, no paging ---- */
  { ps5tm_need_t n;
    ps5tm_zstd_index_t z;

    /* exact bytes of a small zstd game: 1000 files, 2000 chunks, 1 volume (16 KiB pages) */
    z = (ps5tm_zstd_index_t){ 1000, 2000, 1, 1000, 8 * MIB, 3 * MIB };
    ps5tm_zstd_need_041(&z, &n);
    CHECK(n.derived == 98304 && n.fast == 16384 && n.need == 114688);      /* view 89,408 B -> 6 pages; tables 16,008 B -> 1 page */
    CHECK(n.room == 80 * MIB && !n.paged && !n.over);
    CHECK(n.headroom == (int64_t)(80 * MIB) - (int64_t)(114688 + 16384 + 128 * 1024));   /* + dir_of block + windows */
    CHECK(!strcmp(ps5tm_need_state(&n), "ok"));

    /* ids = 0: one view file per zstd file; the size of ampr_emu.index does not matter any more */
    z.ids = 0; z.ampr_bytes = 70 * MIB;
    ps5tm_zstd_need_041(&z, &n);
    CHECK(n.need == 114688 && n.room == 80 * MIB);

    /* the case the maintainers measured: 242,409 files, about 5.5 chunks per file, tables 10.22 MiB: 21.74 MiB for the view and the tables */
    z = (ps5tm_zstd_index_t){ 242409, 1338000, 1500, 242409, 0, 0 };
    ps5tm_zstd_need_041(&z, &n);
    CHECK(n.need > 21 * MIB && n.need < 22 * MIB);
    CHECK(n.fast > 10 * MIB + 150 * 1024 && n.fast < 10 * MIB + 350 * 1024);    /* 10.22 MiB of chunk and volume tables */
    CHECK(n.derived / 242409 >= 49 && n.derived / 242409 <= 51);                /* about 50 B per file */
    CHECK(!n.over && !strcmp(ps5tm_need_state(&n), "ok"));

    /* per file, view alone: about 50 B for zstd */
    z = (ps5tm_zstd_index_t){ 600000, 0, 0, 600000, 0, 0 };
    ps5tm_zstd_need_041(&z, &n);
    CHECK(n.derived / 600000 >= 47 && n.derived / 600000 <= 55);

    /* the limit: files with 5.5 chunks each at about 90 B per file in all */
    z = (ps5tm_zstd_index_t){ 800000, 4400000, 40, 800000, 0, 0 };
    ps5tm_zstd_need_041(&z, &n);
    CHECK(!n.over && n.headroom > 0);
    z = (ps5tm_zstd_index_t){ 1000000, 5500000, 40, 1000000, 0, 0 };           /* about 100 MiB */
    ps5tm_zstd_need_041(&z, &n);
    CHECK(n.over && !strcmp(ps5tm_need_state(&n), "over") && n.need > 80 * MIB);
    /* a game of 1.3 million files at 600,000 chunks (49 MiB for v0.4.0) fits here */
    z = (ps5tm_zstd_index_t){ 1300000, 600000, 20, 1300000, 120 * MIB, 10 * MIB };
    ps5tm_zstd_need_041(&z, &n);
    CHECK(!n.over);

    /* edge: the largest chunk count of a 300,000-file game that is not refused; the verdict flips where the headroom crosses zero */
    { uint64_t lo = 0, hi = 40000000;
      while(lo < hi) {
        uint64_t mid = (lo + hi + 1) / 2;
        z = (ps5tm_zstd_index_t){ 300000, mid, 10, 300000, 0, 0 };
        ps5tm_zstd_need_041(&z, &n);
        if(!n.over) lo = mid; else hi = mid - 1;
      }
      z = (ps5tm_zstd_index_t){ 300000, lo, 10, 300000, 0, 0 };
      ps5tm_zstd_need_041(&z, &n);
      CHECK(!n.over && n.headroom >= 0 && n.headroom < 16 * 1024);          /* what is left is below one page of tables (2048 chunks) */
      z.chunks = lo + 2048;
      ps5tm_zstd_need_041(&z, &n);
      CHECK(n.over);
      CHECK(lo > 8000000 && lo < 9000000); }                                  /* about 8.5 million chunks fit beside 300,000 files */

    /* the three states round the limit: 2 MiB either side */
    { int saw_ok = 0, saw_tight = 0, saw_over = 0;
      for(uint64_t chunks = 8000000; chunks < 10000000; chunks += 50000) {
        z = (ps5tm_zstd_index_t){ 300000, chunks, 10, 300000, 0, 0 };
        ps5tm_zstd_need_041(&z, &n);
        const char *st = ps5tm_need_state(&n);
        if(!strcmp(st, "ok")) saw_ok = 1; else if(!strcmp(st, "tight")) saw_tight = 1; else saw_over = 1;
        CHECK(n.over == (n.headroom < 0));
      }
      CHECK(saw_ok && saw_tight && saw_over); }

    /* absurd numbers never wrap into a small one */
    z = (ps5tm_zstd_index_t){ UINT64_MAX, UINT64_MAX, UINT64_MAX, UINT64_MAX, UINT64_MAX, UINT64_MAX };
    ps5tm_zstd_need_041(&z, &n);
    CHECK(n.over && n.headroom < 0);
    z = (ps5tm_zstd_index_t){ 1000, UINT64_MAX / 4, 5, 1000, 0, 0 };          /* the chunk table alone is huge */
    ps5tm_zstd_need_041(&z, &n);
    CHECK(n.over && n.headroom < 0);
    z = (ps5tm_zstd_index_t){ 1ull << 31, 10, 1, 1ull << 31, 0, 0 };          /* 2^31 files: view::sizes refuses */
    ps5tm_zstd_need_041(&z, &n);
    CHECK(n.over && !strcmp(ps5tm_need_state(&n), "over"));
    z = (ps5tm_zstd_index_t){ 0, 0, 0, 0, 0, 0 };                              /* an empty pack: only fixed parts */
    ps5tm_zstd_need_041(&z, &n);
    CHECK(!n.over && n.need < MIB);

    /* LZ4 (AMPRPAK4): exact bytes of 1000 files, 2000 chunks, 2 packs */
    ps5tm_lz4_need_041(1000, 2000, 2, 1000, &n);
    CHECK(n.derived == 98304);                                                 /* view 85,408 B -> 6 pages (no chunk column) */
    CHECK(n.fast == 81920 + 16384);                                            /* block 72,288 B -> 5 pages, mismatch bits 1 page */
    CHECK(n.headroom == (int64_t)(80 * MIB) - (int64_t)(n.need + 16384 + 128 * 1024));
    CHECK(!n.paged && !n.over && n.room == 80 * MIB);
    ps5tm_lz4_need_041(1000, 2000, 2, 0, &n);                                  /* ids 0: one view file per pack file */
    CHECK(n.derived == 98304);

    /* LZ4 of ampr-emu-ng issue #61, Demon's Souls: 223,180 files, 1,513,210 chunks, 18 packs; about 27.5 MiB for the pack block (128 B header,
       48 B per file, 12 B per chunk, 32 B per pack and the names), plus the view of about 46 B per file */
    ps5tm_lz4_need_041(223180, 1513210, 18, 223180, &n);
    CHECK(n.fast > 27 * MIB && n.fast < 29 * MIB);
    CHECK(n.derived / 223180 >= 44 && n.derived / 223180 <= 48);
    CHECK(n.need > 36 * MIB && n.need < 40 * MIB);
    CHECK(!n.over && !strcmp(ps5tm_need_state(&n), "ok"));                     /* v0.4.0 refused it; v0.4.1 has 80 MiB */
    /* LZ4 has no chunk column in the view: smaller than zstd's for the same files */
    { ps5tm_need_t nz;
      z = (ps5tm_zstd_index_t){ 223180, 0, 0, 223180, 0, 0 };
      ps5tm_zstd_need_041(&z, &nz);
      ps5tm_lz4_need_041(223180, 0, 0, 223180, &n);
      CHECK(n.derived < nz.derived); }

    /* LZ4: far over at the runtime caps, and no wrap-around */
    ps5tm_lz4_need_041(2000000, 12000000, 100, 2000000, &n);
    CHECK(n.over);
    ps5tm_lz4_need_041(UINT64_MAX, UINT64_MAX, UINT64_MAX, UINT64_MAX, &n);
    CHECK(n.over && n.headroom < 0);
    ps5tm_lz4_need_041(1ull << 31, 1, 1, 1ull << 31, &n);
    CHECK(n.over);

    /* the v0.4.0 functions are unchanged beside the new ones: the same game, both models */
    { ps5tm_need_t old;
      ps5tm_lz4_need(223180, 1513210, 18, 42 * MIB, 34373632, &old);
      CHECK(old.over && old.room == 64 * MIB - 34373632);                      /* 0.4.0: refused */
      ps5tm_lz4_need_041(223180, 1513210, 18, 223180, &n);
      CHECK(!n.over); }                                                        /* 0.4.1: passes */
  }

  printf("%d checks, %d failed\n", g_n, g_fail);
  return g_fail ? 1 : 0;
}
