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

const char *
ps5tm_need_state(const ps5tm_need_t *n) {
  if(n->headroom < -(int64_t)PS5TM_NG_MARGIN) return "over";
  if(n->headroom <= (int64_t)PS5TM_NG_MARGIN) return "tight";
  return "ok";
}
