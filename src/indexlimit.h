/* PS5 Cooling & System Center - Pro: will the lookup tables of a zstd or LZ4 pack fit into AMPR EMU NG (indexlimit.c, 14.11.2026). */
#ifndef PS5TM_INDEXLIMIT_H
#define PS5TM_INDEXLIMIT_H

#include <stddef.h>
#include <stdint.h>

/* What ampr-emu-ng v0.4.0 sets aside (rt/index_memory.rs): 64 MiB for everything about its indexes (INDEX_MEMORY_MAX), of that at most 48 MiB
   for lookup tables (INDEX_LOOKUP_MAX) and at least 16 MiB for the page cache of a paged index (INDEX_ARENA_MIN, taken when the first paged file
   registers). A pack whose tables do not fit is not served at all: "ampr-emu-ng: index tables need 49 MiB, over the 48 MiB beside the page
   cache; packed files not served", and the game finds none of its packed files (ampr-emu-ng issues #67, #61, #53). The numbers belong to
   v0.4.0; a later version may move them (the runtime's own start message is the binding source). */
#define PS5TM_NG_INDEX_MEMORY (64ull << 20)
#define PS5TM_NG_LOOKUP_MAX   (48ull << 20)
#define PS5TM_NG_ARENA_MIN    (16ull << 20)
/* ids, page rounding and other index memory are estimates: a result this close to the limit is "tight", not a verdict */
#define PS5TM_NG_MARGIN       (2ull << 20)

/* The numbers of an ampr_zstd.index (AMPRZPK1, 128-byte header: volume_count at 20, file_count at 40, chunk_count at 48) and of the
   game's other index. */
typedef struct {
  uint64_t files;        /* packed files in ampr_zstd.index */
  uint64_t chunks;
  uint64_t volumes;
  uint64_t ids;          /* AMPRIDX3 records (ampr_emu.index: every file of the game); one per zstd file when the game has none */
  uint64_t index_bytes;  /* size of ampr_zstd.index */
  uint64_t ampr_bytes;   /* size of ampr_emu.index, read whole into the same index memory when it is not too big; 0 when there is none */
} ps5tm_zstd_index_t;

typedef struct {
  uint64_t fast;         /* fast table of a paged index */
  uint64_t derived;      /* the tables built from the index */
  uint64_t need;         /* what the tables take: derived when the index is read whole, fast + derived when it is paged */
  uint64_t room;         /* index memory left once ampr_emu.index is in */
  int64_t  headroom;     /* how much more the tables could take before NG refuses the pack; negative: it refuses */
  int      paged;        /* the index file does not fit with its derived tables: it is read in pages */
  int      over;         /* headroom below zero */
} ps5tm_need_t;

typedef ps5tm_need_t ps5tm_zstd_need_t;

/* 0 when the 128 bytes are the header of an ampr_zstd.index (version 1); the counts are filled in. */
int  ps5tm_zstd_header(const uint8_t *h, size_t n, ps5tm_zstd_index_t *out);
/* The same decision as ampr-emu-ng (rt/pack/zstd.rs lookup_bytes: fast 8 B x (2 x files + chunks + volumes), derived 16 B per id and 4 B per
   packed file; rt/pack/manifest.rs mount_index_source: read whole when the file and its derived tables fit what is left). An estimate in two
   places (what else sits in the index memory when the pack is mounted), exact in the rest. */
void ps5tm_zstd_need(const ps5tm_zstd_index_t *in, ps5tm_zstd_need_t *out);

/* An ampr_assets.index (AMPRPAK4, 128-byte header: file_count u64 at 40, chunk_count u64 at 48, pack_count u32 at 56): 0 when the bytes are one. */
int  ps5tm_lz4_header(const uint8_t *h, size_t n, uint64_t *files, uint64_t *chunks, uint64_t *packs);
/* LZ4 (rt/pack/manifest.rs lz4_lookup_bytes): fast 48 B per file, 12 B per chunk, 32 B per pack; derived 4 B per file. ampr_bytes as above. */
void ps5tm_lz4_need(uint64_t files, uint64_t chunks, uint64_t packs, uint64_t index_bytes, uint64_t ampr_bytes, ps5tm_need_t *out);

/* ---- ampr-emu-ng v0.4.1 and v0.5.0 (the two versions the app ships; the formulas hold for both, see the note below; 0.4.0 above is no longer shipped) ----
   v0.4.1 has no 48 MiB lookup limit and no paged reading any more (rt/index_memory.rs, rt/index_view.rs, docs/zstd-format.md of v0.4.1): the
   index memory ("Index ledger") is 80 MiB for everything about the indexes. The mount reads ampr_zstd.index (or ampr_assets.index) once, front to
   back, through two 64 KiB windows, builds a compiled view of every file of ampr_emu.index (columns hash, size, mtime, path_at, next, chunk, two
   hash slot arrays, directory records) and keeps the pack tables beside it. Nothing of ampr_emu.index itself stays in the memory. When the view and
   the tables do not fit what is left of the 80 MiB, "ampr-emu-ng: index needs N MiB, over the 80 MiB cap; packed files not served". The result has
   the same type as above: fast = the pack tables kept after the mount, derived = the compiled view, need = fast + derived (what stays), room =
   80 MiB, headroom = room - need - what only the mount takes for a moment (block of dir_of/lz4_hash, the 128 KiB of windows), paged = 0. */
#define PS5TM_NG041_INDEX_MEMORY (80ull << 20)
#define PS5TM_NG041_PAGE         (16ull << 10)   /* every block of the ledger is rounded up to this (SCE_KERNEL_PAGE_SIZE) */
#define PS5TM_NG041_WINDOWS      (128ull << 10)  /* the two 64 KiB read windows of a mount */

/* zstd: in->files, chunks, volumes from the header; in->ids = AMPRIDX3 records (view files), 0 = one per zstd file; index_bytes and ampr_bytes
   are not used by 0.4.1. */
void ps5tm_zstd_need_041(const ps5tm_zstd_index_t *in, ps5tm_need_t *out);
/* LZ4 (AMPRPAK4): files, chunks, packs from the header; ids = AMPRIDX3 records (view files), 0 = files. */
void ps5tm_lz4_need_041(uint64_t files, uint64_t chunks, uint64_t packs, uint64_t ids, ps5tm_need_t *out);

/* "ok", "tight" (within PS5TM_NG_MARGIN of the limit, either side) or "over". */
const char *ps5tm_need_state(const ps5tm_need_t *n);

#endif
