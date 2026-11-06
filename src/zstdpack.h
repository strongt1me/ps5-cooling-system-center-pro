/* zstd asset packs of a game (06.11.2026): the second format next to the LZ4 packs (lz4pack.h), read by AMPR EMU NG only
 * (ampr-emu-ng, quer3q, GPL-3.0; libSceAmpr.sprx of the NG series). The format is NG's "AMPRZPK1" index (ampr_zstd.index)
 * with "AMPRZDT1" volumes (ampr_zstd-NNN.pak), described in the project's docs/zstd-format.md
 * (https://github.com/quer3q/ampr-emu-ng/blob/main/docs/zstd-format.md, read on 06.11.2026); this is a C version of the packer on
 * zstd 1.5.7 (BSD-3-Clause, src/third_party/zstd). The runtime picks the format at load by whether /app0/ampr_zstd.index exists.
 *
 *   - every file of a game is cut into chunks of 256 KiB; every chunk is one zstd frame (content size and checksum in the
 *     frame, no dictionary, window at most 256 KiB), or raw when zstd does not make it smaller. The chunks lie one after another
 *     in the volumes, in the order of the file records, which are sorted by (FNV-1a 64 of the path, path bytes).
 *   - a volume is at most 4 GiB less 64 KiB; a file may continue in the next volume.
 *   - what stays loose: programs and modules (eboot.bin, .elf .self .prx .sprx), sce_sys, sce_module, fakelib*, the files of the
 *     packs and of the APR-EMU, empty files, and the few files the PS5 itself looks for on disk (lzc_keep_on_disk).
 *
 * The functions return 0, or -1 with the reason in err (German, for the page), or ECANCELED when the cancel flag was raised
 * (err then says "Abgebrochen."). They use the types of lz4pack.h (callbacks, statistics, plan, info, list), so the jobs of the app
 * treat both formats alike. pack() runs its own pool of compression threads and returns when they are gone; the functions need a
 * big stack (the callers run them on a thread of 768 KiB). */
#ifndef PS5TM_ZSTDPACK_H
#define PS5TM_ZSTDPACK_H

#include <stddef.h>
#include <stdint.h>

#include "lz4pack.h"

#define ZSP_INDEX_NAME   "ampr_zstd.index"
#define ZSP_VOLUME_FMT   "ampr_zstd-%03u.pak"
#define ZSP_LEVEL_MIN    1
#define ZSP_LEVEL_MAX    22
#define ZSP_LEVEL_DEFAULT 5

/* 1 when dir holds the index of a zstd pack set (a regular file ampr_zstd.index). */
int zsp_packed_dir(const char *dir);

/* True for a path (relative to the game, "/" separators) that never goes into a pack, and for an empty or system-looked-for file. */
int zsp_stays_loose(const char *rel, uint64_t size);

/* What packing would do with the game in root, without packing anything. */
int zsp_plan(const char *root, lzp_plan_t *plan, const lzp_ui_t *ui, char *err, size_t n);

/* Packs the files of root into outdir (root itself for packing in place; outdir may be another folder): the volumes and the
   index, everything under temporary names (.ps5cc-teil) and published at the end, the index last. The sources stay. level is
   1..22; workers is the number of compression threads (0: 3). */
int zsp_pack(const char *root, const char *outdir, int level, unsigned workers, lzp_stats_t *st, const lzp_ui_t *ui, char *err, size_t n);

/* The pack set in dir: 1 and filled (files_packed, chunks, logical_bytes, stored_bytes, volumes, build_id = the archive id; level
   in *level when not NULL), or 0 with info->why when something is there that does not read. */
int zsp_info(const char *dir, lzp_info_t *info, int *level);

/* Reads the whole set in dir: index, every volume header, every chunk decoded (the frame checks its own CRC); the frame
   parameters must be the ones the format asks for. With srcroot every packed file is also compared byte for byte with the file of
   that name under srcroot. */
int zsp_verify(const char *dir, const char *srcroot, lzp_stats_t *st, const lzp_ui_t *ui, char *err, size_t n);

/* Writes every packed file of the set in dir to destroot (each as <name>.ps5cc-teil first, then renamed). */
int zsp_unpack(const char *dir, const char *destroot, lzp_stats_t *st, const lzp_ui_t *ui, char *err, size_t n);

/* Removes from root the files the set in dir holds packed (never programs, system folders or the packs themselves; each must
   be a regular file of the size the index says). With empty_dirs the folders that become empty go too. The caller verified the
   set against root just before. */
int zsp_remove_sources(const char *dir, const char *root, int empty_dirs, lzp_stats_t *st, const lzp_ui_t *ui, char *err, size_t n);

/* The paths of the files the set holds packed, sorted by strcmp (lzp_list_has finds in it). Free with lzp_list_free. */
int zsp_list(const char *dir, lzp_list_t *out, char *err, size_t n);

/* Deletes the pack set of dir: the volumes and the index (and what a failed run left under temporary names). */
int zsp_remove_set(const char *dir, char *err, size_t n);

#endif
