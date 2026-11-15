/* PS5 Cooling & System Center - Pro: will the lookup tables of a zstd or LZ4 pack fit into AMPR EMU NG (14.11.2026).
 *
 * ampr-emu-ng v0.4.0 holds everything about its indexes in 64 MiB. A pack index is read whole when the file and the tables derived from it fit
 * what is left (mount_index_source: len + derived <= index_room); then only the derived tables count. Else it is read in pages and gets a fast
 * table too; fast + derived must fit what is left after the page cache of 16 MiB (INDEX_ARENA_MIN, taken when the first paged file registers)
 * and stay under 48 MiB (INDEX_LOOKUP_MAX). If they do not, the pack is refused and the game does not find a single packed file. Seen with Final
 * Fantasy XVI (49 MiB) and Demon's Souls (62 MiB). Usually the left-over room binds first, not the 48 MiB: ampr_emu.index is read whole and takes
 * about 150 B per file of the 64 MiB (32.8 MiB for Demon's Souls' 223,180 files).
 *
 * Checked with the maintainers of ampr-emu-ng on 14.11.2026 against the source of HEAD aa0bf91: the formulas, the header places and the decision. What
 * the app cannot know is estimated: that ampr_emu.index is read whole, that nothing else has taken lookup tables before the pack is mounted, and, for a game
 * without ampr_emu.index (the runtime then builds one for every file of the folder), the id count. Two MiB either side of the limit are "tight". */
#include "indexlimit.h"

#include <string.h>

static uint32_t
le32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t
le64(const uint8_t *p) {
  return (uint64_t)le32(p) | ((uint64_t)le32(p + 4) << 32);
}

int
ps5tm_zstd_header(const uint8_t *h, size_t n, ps5tm_zstd_index_t *out) {
  if(!h || n < 128 || memcmp(h, "AMPRZPK1", 8) != 0) return -1;
  if(le32(h + 8) != 1 || le32(h + 12) != 128) return -1;
  memset(out, 0, sizeof(*out));
  out->volumes = le32(h + 20);
  out->files = le64(h + 40);
  out->chunks = le64(h + 48);
  return 0;
}

int
ps5tm_lz4_header(const uint8_t *h, size_t n, uint64_t *files, uint64_t *chunks, uint64_t *packs) {
  if(!h || n < 128 || memcmp(h, "AMPRPAK4", 8) != 0 || le32(h + 8) != 4) return -1;
  *files = le64(h + 40);
  *chunks = le64(h + 48);
  *packs = le32(h + 56);
  return 0;
}

/* a * b without wrapping: a number far beyond any real game stands for "far too much" */
static uint64_t
mul(uint64_t a, uint64_t b) {
  if(a && b > UINT64_MAX / a) return UINT64_MAX;
  return a * b;
}

static uint64_t
add(uint64_t a, uint64_t b) {
  return a > UINT64_MAX - b ? UINT64_MAX : a + b;
}

static int64_t
sat(uint64_t v) {
  return v > (uint64_t)INT64_MAX ? INT64_MAX : (int64_t)v;
}

/* a - b as a signed number */
static int64_t
sub(uint64_t a, uint64_t b) {
  return a >= b ? sat(a - b) : -sat(b - a);
}

static int64_t
lo(int64_t a, int64_t b) {
  return a < b ? a : b;
}

/* The common decision. fast and derived are what the format needs; index_bytes the size of the pack index; ampr_bytes what ampr_emu.index takes. */
static void
decide(uint64_t fast, uint64_t derived, uint64_t index_bytes, uint64_t ampr_bytes, ps5tm_need_t *out) {
  memset(out, 0, sizeof(*out));
  out->fast = fast;
  out->derived = derived;
  /* the index memory ampr_emu.index leaves: it is read whole while it fits (then its bytes sit there), else it takes tables of its own */
  uint64_t taken = ampr_bytes < PS5TM_NG_INDEX_MEMORY ? ampr_bytes : PS5TM_NG_INDEX_MEMORY;
  out->room = PS5TM_NG_INDEX_MEMORY - taken;
  out->paged = add(index_bytes, derived) > out->room;
  if(!out->paged) {
    /* read whole: the file and its derived tables, no fast table and no page cache */
    out->need = derived;
    out->headroom = lo(sub(out->room, add(index_bytes, derived)), sub(PS5TM_NG_LOOKUP_MAX, derived));
  } else {
    out->need = add(fast, derived);
    /* a paged index also needs its page cache: at least 16 MiB must stay (INDEX_ARENA_MIN) */
    uint64_t after_arena = out->room > PS5TM_NG_ARENA_MIN ? out->room - PS5TM_NG_ARENA_MIN : 0;
    out->headroom = lo(sub(PS5TM_NG_LOOKUP_MAX, out->need), sub(after_arena, out->need));
  }
  out->over = out->headroom < 0;
}

void
ps5tm_zstd_need(const ps5tm_zstd_index_t *in, ps5tm_zstd_need_t *out) {
  uint64_t ids = in->ids ? in->ids : in->files;                    /* before the AMPRIDX3 is published NG estimates one id per file */
  uint64_t fast = mul(add(add(mul(in->files, 2), in->chunks), in->volumes), 8);
  uint64_t order_n = in->files < ids ? in->files : ids;
  uint64_t derived = add(mul(ids, 16), mul(order_n, 4));
  decide(fast, derived, in->index_bytes, in->ampr_bytes, out);
}

void
ps5tm_lz4_need(uint64_t files, uint64_t chunks, uint64_t packs, uint64_t index_bytes, uint64_t ampr_bytes, ps5tm_need_t *out) {
  uint64_t fast = add(add(mul(files, 48), mul(chunks, 12)), mul(packs, 32));
  decide(fast, mul(files, 4), index_bytes, ampr_bytes, out);
}

/* ---- ampr-emu-ng v0.4.1 (15.11.2026) ----
 *
 * Read from the source of the tag v0.4.1: crates/ampr-sys/src/rt/index_view.rs (layout, first_dir_cap), crates/ampr-format/src/view.rs (sizes,
 * slot_count, pack_columns, Dir = 32 B), rt/index_memory.rs (INDEX_MEMORY_MAX 80 MiB, page_round), rt/pack/zstd.rs (table_bytes: 8 B per chunk and per
 * volume, 2 x 64 KiB windows), rt/pack/manifest.rs (the AMPRPAK4 block: file up to the string section + spans + pack names, the mismatch bitmap).
 * The view is exact from the file count and the directory capacity; of the numbers below these are ESTIMATED:
 *  - the directory count: NG starts at files / 8 + 1024 directory records of 32 B and doubles on overflow (a retry first gives the failed block back);
 *    the app does not know the directories, so it takes the first capacity. A game with fewer than 8 files per directory on average costs a few MiB more;
 *  - the page size of the ledger (16 KiB on the PS5, SCE_KERNEL_PAGE_SIZE: blocks are rounded up to it);
 *  - the AMPRPAK4 pack names: 32 B per pack plus the 16 B span (the paths of the files are not kept, only the names of the packs);
 *  - the view files: the AMPRIDX3 count when the game has an ampr_emu.index, else one per packed file (without it NG builds an index from a walk and
 *    serves no packed file at all);
 *  - other users of the index memory beside the mount (the 2 MiB decode-on-access slots of AMPR_ZSTD_MMAP, the save icon): not counted, the 2 MiB
 *    margin of ps5tm_need_state covers them.
 * Not modelled: the hard caps of the runtime (2,000,000 files, 16,000,000 chunks, 1,024 packs) and a game with 2^31 files or more (reported as over).
 * The peak during the mount is view + dir_of/lz4_hash (given back at the end of the mount) + the pack tables + the 128 KiB of windows; what stays
 * afterwards is the view and the tables (LZ4: plus the mismatch bitmap of one bit per view file). */

static uint64_t
page_up(uint64_t v) {
  return add(v, PS5TM_NG041_PAGE - 1) & ~(PS5TM_NG041_PAGE - 1);
}

/* view.rs slot_count: the power of two at or above 1.5 x n (n odd: one more), at least 2 */
static uint64_t
slot_count(uint64_t n) {
  uint64_t want = n * 3 / 2 + (n & 1), p = 2;
  while(p < want) p <<= 1;
  return p;
}

/* index_view.rs layout(): bytes of the resident block and of the transient one for `n` files (n < 2^31) and the first directory capacity.
   Resident: hash, size, mtime 8 B each; path_at 4 B; next 4 B (zstd and LZ4); chunk 4 B (zstd only); file slots 4 B x slot_count(n); the directory
   records 32 B x dir_cap and their slots 4 B x slot_count(dir_cap); 256 collision entries of 4 B. Transient: dir_of 4 B per file, LZ4 also lz4_hash 8 B. */
static void
view_bytes_041(uint64_t n, int lz4, uint64_t *resident, uint64_t *scratch) {
  uint64_t dirs = n / 8 + 1024;
  uint64_t r = mul(n, 24 + 4 + 4 + (lz4 ? 0 : 4));
  r = add(r, mul(slot_count(n), 4));
  r = add(r, mul(dirs, 32));
  r = add(r, mul(slot_count(dirs), 4));
  r = add(r, 256 * 4);
  *resident = page_up(r);
  *scratch = page_up(mul(n, lz4 ? 12 : 4));
}

/* The common end: view of `n` files, pack tables kept, a transient part (windows) beside the view's scratch block. */
static void
decide_041(uint64_t n, int lz4, uint64_t tables, ps5tm_need_t *out) {
  uint64_t resident, scratch;
  memset(out, 0, sizeof(*out));
  out->room = PS5TM_NG041_INDEX_MEMORY;
  if(n >= (1ull << 31)) {
    out->fast = tables;
    out->derived = UINT64_MAX;
    out->need = UINT64_MAX;
    out->headroom = -INT64_MAX;
    out->over = 1;
    return;
  }
  view_bytes_041(n, lz4, &resident, &scratch);
  out->fast = tables;
  out->derived = resident;
  out->need = add(tables, resident);
  out->headroom = sub(out->room, add(add(out->need, scratch), PS5TM_NG041_WINDOWS));
  out->over = out->headroom < 0;
}

void
ps5tm_zstd_need_041(const ps5tm_zstd_index_t *in, ps5tm_need_t *out) {
  uint64_t n = in->ids ? in->ids : in->files;
  /* zstd.rs table_bytes: 8 B per chunk and per volume, one block */
  uint64_t tables = page_up(mul(add(in->chunks, in->volumes), 8));
  decide_041(n, 0, tables, out);
}

void
ps5tm_lz4_need_041(uint64_t files, uint64_t chunks, uint64_t packs, uint64_t ids, ps5tm_need_t *out) {
  uint64_t n = ids ? ids : files;
  /* manifest.rs: the 128 B header, 48 B per file, 12 B per chunk, 32 B per pack, then the spans (16 B per pack) and the pack names (about 32 B each) */
  uint64_t block = add(add(add(128, mul(files, 48)), mul(chunks, 12)), mul(packs, 32 + 16 + 32));
  /* join_lz4: one bit per view file */
  uint64_t bits = page_up(add(n, 1) / 8 + 1);
  decide_041(n, 1, add(page_up(block), bits), out);
}

const char *
ps5tm_need_state(const ps5tm_need_t *n) {
  if(n->headroom < -(int64_t)PS5TM_NG_MARGIN) return "over";
  if(n->headroom <= (int64_t)PS5TM_NG_MARGIN) return "tight";
  return "ok";
}
