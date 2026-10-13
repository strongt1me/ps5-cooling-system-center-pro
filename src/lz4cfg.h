/* The packing profile of the LZ4 asset packs (12.10.2026): the rules that say which files of a game go into packs, with
 * which block size and compression, and which stay loose. Either built in ("automatic") or read from a TOML file in the
 * format of drakmor's ampr_pack.py (ampr_emu, GPL-3.0, tools/ampr_pack.py load_config): [pack], [groups.NAME], [[rule]],
 * [runtime]. Only that subset of TOML is understood (strings, numbers, booleans, arrays of strings, tables, arrays of
 * tables, comments, multi-line arrays); anything else is an error with the line number. */
#ifndef PS5TM_LZ4CFG_H
#define PS5TM_LZ4CFG_H

#include <stddef.h>
#include <stdint.h>

enum { LZC_LOOSE = 0, LZC_COMPRESS, LZC_STORE };
enum { LZC_LAY_AUTO = 0, LZC_LAY_RANDOM, LZC_LAY_MIXED, LZC_LAY_STREAMING };
enum { LZC_ASSIGN_BALANCED = 0, LZC_ASSIGN_HASH, LZC_ASSIGN_ROUND_ROBIN };

#define LZC_MAX_GROUPS 256

typedef struct {
  char     name[40];
  unsigned pack_count;
  int      assignment;
  uint64_t max_pack_size;           /* 0: unlimited */
  int      stripe_large;
  uint64_t stripe_threshold;
  unsigned stripe_group_blocks;
  uint64_t io_page_size;            /* 0: the profile's */
} lzc_group_t;

typedef struct {
  int       action;
  char    **inc;  unsigned ninc;
  char    **exc;  unsigned nexc;
  unsigned  block_shift;            /* 14..20 */
  unsigned  group;                  /* index into groups */
  int       fast;                   /* 1: LZ4 fast, 0: LZ4 HC */
  int       level;                  /* HC level 1..12 */
  int       accel;                  /* fast acceleration */
  uint64_t  min_sav_bytes;
  double    min_sav_ratio;
  uint64_t  ion_bytes;              /* io-neutral thresholds */
  double    ion_ratio;
  int       layout;                 /* LZC_LAY_* */
  int       streaming, hot, force_pack;
} lzc_rule_t;

typedef struct {
  int       automatic;              /* the built-in profile: paths are judged by code, not by globs */
  int       has_runtime;
  uint64_t  rt_decoded, rt_physical;
  unsigned  rt_workers, rt_reserve;
  char      index_name[128];
  char      pack_pattern[128];
  int       default_action;
  unsigned  default_block_shift;
  uint64_t  io_page_size, payload_alignment, chunk_alignment;
  unsigned  workers;
  int       fast, level, accel;
  uint64_t  min_sav_bytes;
  double    min_sav_ratio;
  uint64_t  ion_bytes;
  double    ion_ratio;
  int       dedupe, dedupe_group_scope, dedupe_streaming;
  int       preserve_mtime, validate_index, self_contained;
  char    **required_packed; unsigned nreq;
  int       auto_loose_large, auto_loose_hot;
  uint64_t  al_min_size, al_sample_bytes;
  unsigned  al_sample_blocks;
  double    al_min_ratio, al_max_raw;
  lzc_group_t *groups;  unsigned ngroups;
  lzc_rule_t  *rules;   unsigned nrules;
} lzc_cfg_t;

/* Loads a TOML profile. include_from / exclude_from lists are read from beside the file (never outside its folder).
   0, or -1 with the reason (and line number) in err. */
int  lzc_load(const char *toml_path, lzc_cfg_t *c, char *err, size_t n);
/* The built-in profile: level 1..12 (1-5 LZ4 fast, 6-12 LZ4 HC), the block size shift 14..20, lanes 1..8. */
void lzc_automatic(lzc_cfg_t *c, int level, unsigned block_shift, unsigned lanes);
void lzc_free(lzc_cfg_t *c);

/* fnmatch of Python (fnmatch.fnmatchcase): * also matches "/", ? one character, [seq] and [!seq]. */
int  lzc_glob(const char *pattern, const char *s);
/* Is this path one that must never be packed (programs, system folders, the pack files themselves)? */
int  lzc_protected(const char *rel);
/* The rule of a path: the last matching rule wins; the default rule when none does. *def is filled for that case.
   The automatic profile judges by code. */
const lzc_rule_t *lzc_select(const lzc_cfg_t *c, const char *rel, lzc_rule_t *def);
/* The rule for a path no rule matches (action = the profile's default, loose in the automatic one). */
void lzc_default_rule(const lzc_cfg_t *c, lzc_rule_t *def);

/* sizes like "64KiB", "8GiB", "128MiB" or plain numbers; -1 on error */
int  lzc_parse_size(const char *s, uint64_t *out);

#endif
