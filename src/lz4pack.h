/* LZ4 asset packs of a game (12.10.2026): packs the data files of a game folder into seekable LZ4 packs that the APR-EMU
 * (libSceAmpr.sprx, the "pack" builds) reads while the game runs, checks them, unpacks them and removes what they made
 * unnecessary. The format is drakmor's AMPRPAK4 (manifest "ampr_assets.index") with AMPRDAT3 data volumes
 * ("ampr_assets-NNN.pak") and the offline CRC sidecar AMPRCRC1 ("ampr_assets.index.crc"); this is a C port of the packer
 * of ampr_emu (https://github.com/drakmor/ampr_emu, docs/ASSET_PACKS.md, tools/ampr_pack.py + ampr_pack_format.py,
 * GPL-3.0, read on 12.10.2026) on LZ4 1.10.0 (BSD-2-Clause), the version the runtime pins.
 *
 * Every function returns 0, or -1 with the reason in err (German, for the page), or ECANCELED when the cancel flag was
 * raised (err then says "Abgebrochen."). Nothing here starts threads of the caller's kind: pack() runs its own pool of
 * compression threads and returns when they are gone. The functions need a big stack (the callers run them on a thread
 * of 768 KiB). */
#ifndef PS5TM_LZ4PACK_H
#define PS5TM_LZ4PACK_H

#include <stddef.h>
#include <stdint.h>

#include "lz4cfg.h"

#define LZP_INDEX_NAME   "ampr_assets.index"
#define LZP_AMPR_INDEX   "ampr_emu.index"
#define LZP_PART_SUFFIX  ".ps5cc-teil"

/* Everything written into a game folder is 0777, set with fchmod / chmod so that the umask cannot narrow it: a game whose
   eboot.bin or modules lack the execute bit crashes at the start (SIGBUS, CE-108255-1; found by LZ4 Compression 1.4.3 on
   007 First Light, whose files came as 0666, read on 15.10.2026; the same as the 0644 APR-EMU file of Arkanoid, amprlib.c). */
#define LZP_FILE_MODE    0777

/* What the job shows while it works. All callbacks may be NULL. */
typedef struct {
  void (*phase)(void *ctx, const char *text);
  void (*total)(void *ctx, uint64_t bytes);              /* the bytes this phase will work through */
  void (*work)(void *ctx, uint64_t bytes);               /* bytes done since the last call */
  void (*files)(void *ctx, unsigned done, unsigned total);
  void (*current)(void *ctx, const char *path);
  const int *cancel;                                     /* nonzero: stop */
  void *ctx;
} lzp_ui_t;

typedef struct {
  uint64_t files_total, files_packed, files_loose, files_auto_loose;
  uint64_t chunks, chunks_lz4, chunks_raw, chunks_shared;
  uint64_t logical_bytes, stored_bytes, loose_bytes, padding_bytes;
  unsigned packs;
} lzp_stats_t;

/* What a profile would do with the game, without packing anything. */
typedef struct {
  uint64_t files_total, pack_files, pack_bytes, loose_files, loose_bytes;
  uint64_t chunks;
  unsigned block_shift, lanes;
} lzp_plan_t;

/* The pack set of a folder, from its manifest (light: the file and pack records only). */
typedef struct {
  int      packed;                 /* a manifest of a pack set is there and readable */
  uint64_t files_packed, files_loose, chunks, logical_bytes, stored_bytes, volumes;
  char     build_id[33];
  int      has_crc, has_runtime;
  char     why[200];               /* packed == 0 but something is there: why it does not read */
} lzp_info_t;

/* Reads root/ampr_emu.index and applies the profile. cfg may be automatic or loaded. */
int lzp_plan(const char *root, lzc_cfg_t *cfg, lzp_plan_t *plan, const lzp_ui_t *ui, char *err, size_t n);

/* Packs the files of root into outdir (root itself for packing in place; outdir may be another folder): the volumes,
   the manifest, the CRC sidecar and, when the profile has [runtime], the runtime sidecar. Everything is written under
   temporary names and published at the end, the manifest last. The sources stay. workers: compression threads. */
int lzp_pack(const char *root, const char *outdir, lzc_cfg_t *cfg, lzp_stats_t *st, const lzp_ui_t *ui, char *err, size_t n);

/* The pack set in dir: 1 and filled, or 0. */
int lzp_info(const char *dir, lzp_info_t *info);

/* Reads the whole pack set in dir: manifest, every volume header, every chunk decoded and its CRC checked against the
   sidecar. With srcroot, every packed file is also compared byte for byte with the file of that name under srcroot. */
int lzp_verify(const char *dir, const char *srcroot, lzp_stats_t *st, const lzp_ui_t *ui, char *err, size_t n);

/* Writes every packed file of the set in dir to destroot (each as <name>.ps5cc-teil first, then renamed), CRC-checked. */
int lzp_unpack(const char *dir, const char *destroot, lzp_stats_t *st, const lzp_ui_t *ui, char *err, size_t n);

/* Removes from root the files the set in dir holds packed (never programs, system folders or the packs themselves; each
   must be a regular file of the size the manifest says). With empty_dirs the folders that become empty go too. The
   caller verified the set against root just before. */
int lzp_remove_sources(const char *dir, const char *root, int empty_dirs, lzp_stats_t *st, const lzp_ui_t *ui, char *err, size_t n);

/* The paths (relative to the game, as the AMPR index spells them) of the files the set in dir holds packed, sorted by strcmp,
   for bsearch. 0, or -1 with err. Free with lzp_list_free. */
typedef struct { char **rel; size_t n; } lzp_list_t;
int  lzp_list(const char *dir, lzp_list_t *out, char *err, size_t n);
void lzp_list_free(lzp_list_t *l);
int  lzp_list_has(const lzp_list_t *l, const char *rel);

/* Repairs of the check (after LZ4 Compression 1.4.3, read on 15.10.2026). The files the PS5 must find on disk
   (lzc_keep_on_disk) that a set made before that rule, or by another tool, holds only packed: *missing counts those not on
   disk, and with repair each is written next to the packs from them (CRC-checked, 0777), *restored counts those. The manifest
   stays as it is, so the game still reads its packed copy. 0, or -1 with err. */
int  lzp_restore_disk_files(const char *dir, int repair, unsigned *missing, unsigned *restored, char *err, size_t n);

/* Every folder and regular file under root (root itself too) made 0777 (LZP_FILE_MODE); links stay. *fixed counts what was
   changed, *failed what could not be (a drive that keeps no modes, a read-only mount). */
void lzp_fix_modes(const char *root, unsigned *fixed, unsigned *failed);

/* Deletes the pack set of dir: the volumes, the manifest, the CRC and runtime sidecars. 0, or -1 with err. */
int lzp_remove_set(const char *dir, char *err, size_t n);

#endif
