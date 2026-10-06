/* UFS2 image writer — the .ffpkg container.
 *
 * Writes a UFS2 file system image straight from a folder, in the layout
 * ShadowMountPlus recommends for a .ffpkg (its README and mkufs2.sh) and that
 * exFAT Image Builder asks UFS2Tool for:
 *
 *   newfs -O 2 -b 65536 -f 65536 -m 0 -S 512 -i 262144 -D <folder> <image>
 *
 * i.e. UFS2, blocks and fragments of 64 KiB (a block of the file system is
 * exactly one 64 KiB request of the loader's image mount), 256-byte inodes,
 * one inode for every 256 KiB of data, 0% minfree, newfs' default "time"
 * optimization and classic BSD directory entries. A C port of SvenGDK/UFS2Tool's
 * (BSD-2-Clause, github.com/SvenGDK/UFS2Tool) own newfs -D path, from its C#
 * source, not the code itself — nothing from UFS2Tool ships here or is
 * redistributed, and nothing of exFAT Image Builder was used but what its call
 * says about the layout.
 *
 * Scope: the 12 direct blocks of an inode, its single-indirect block and its
 * double-indirect block, i.e. files up to UFS2_MAX_FILE_BYTES (4 TiB less a
 * little, far more than any game's biggest file). The triple-indirect block is
 * not written. A directory has its 12 direct blocks and nothing else (about
 * 24,000 entries with 20-character names). Whatever does not fit (a bigger
 * file, a directory with more entries than 12 blocks hold) is refused while the
 * tree is being sized, before a single byte is written.
 *
 * History: the first version (30.09.2026, the user's choice: "small and
 * medium games first") followed UFS2Tool's "PS5 Quick Create" call (makefs
 * -S 4096, 32 KiB blocks, 4 KiB fragments) and stopped at the single-indirect
 * block, i.e. ~128 MiB a file; a game's 210 MB asset file ran into that on
 * 04.10.2026 and the double-indirect block was added. On 05.10.2026 the layout
 * became the 64 KiB one above, after reading how exFAT Image Builder and
 * ShadowMountPlus make and expect a .ffpkg.
 *
 * Validated against a freshly built UFS2Tool, whole image and not only the
 * superblock: for random trees (1 byte to 620 MB files, up to 151 files in 12
 * folders), thousands of tiny files in one folder and every size at the limits
 * of the pointer trees, the image is byte for byte the one UFS2Tool's newfs -D
 * makes at the same size, except for the time stamps, fsid and the random
 * generation numbers it gives unused inodes. That comparison found four things
 * that were wrong in the image from the start and are fixed: fs_fsbtodb and
 * fs_old_nspf (FreeBSD's newfs lays the file system out in 512-byte units
 * whatever -S says), the recovery block before the superblock, and the cluster
 * map, cg_frsum and cg_initediblk of the group headers. Two older finds:
 * di_blocks counts fixed 512-byte units regardless of the sector size, and the
 * block pointers start at inode offset 0x70. The size is the one difference:
 * this writer plans it itself (make_fs_size: the blocks of the tree, the free
 * room mkufs2.sh leaves, the metadata) and never ends in a last group too short
 * for its own header and inode table, as UFS2Tool sometimes does. An image of
 * the writer's own size is checked with fsck_ufs and an independent checker
 * instead of a byte comparison. */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "conv_ufs2.h"
#include "ioerr.h"
#include "third_party/libdeflate/libdeflate.h"

#define UFS2_MAGIC          0x19540119u
#define CG_MAGIC            0x090255u
#define SUPERBLOCK_OFFSET   65536ull
#define SUPERBLOCK_SIZE     8192u
#define MAXMNTLEN           468
#define DEFAULT_INODES_PER_GROUP 2048
#define UFS2_INODE_SIZE     256u
#define ROOT_INODE          2u
#define UFS2_DIRBLKSIZ           512u
#define DIR_HDR_SIZE        8u
#define NDIRECT             12
#define NINDIRECT           3
#define FS_44INODEFMT       2
#define FS_DYNAMICPOSTBLFMT (-1)
#define FS_FLAGS_UPDATED    0x80u
#define FS_OPTTIME          0
#define FS_OPTSPACE         1
#define CG_HEADER_BASE_SIZE 168
#define CSUM_STRUCT_SIZE    16u
#define IFDIR               0x4000u
#define IFREG               0x8000u
#define PERM                0555u        /* r-xr-xr-x */
#define DT_DIR              4
#define DT_REG              8
#define MAXBSIZE            65536
#define MAXCONTIG_SUMMARY   16
#define MAX_DEPTH           32
#define MAX_SUBDIRS         32765u       /* a directory's link count is 2 + its subfolders, and the kernel stops at 32767 */

/* The layout ShadowMountPlus recommends for a .ffpkg (its README and mkufs2.sh,
   and what exFAT Image Builder asks UFS2Tool for): newfs -O 2 -b 65536 -f 65536
   -m 0 -i 262144 — blocks and fragments of 64 KiB, so a block of the file system
   is exactly one 64 KiB request of the loader's image mount. */
#define BLOCK_SIZE          65536u
#define FRAG_SIZE           65536u
#define SECTOR_SIZE         4096u        /* what the buffers of a group header are rounded to */
#define DEV_BSIZE           512u         /* di_blocks unit — always 512, see file header */
#define MIN_FREE_PERCENT    0
#define OPTIMIZATION_TIME   FS_OPTTIME   /* newfs' default; -m 0 does not change it in UFS2Tool */
#define FRAGS_PER_BLOCK     ((int64_t)(BLOCK_SIZE / FRAG_SIZE))   /* 1     */
#define PTRS_PER_BLOCK      ((int64_t)(BLOCK_SIZE / 8))            /* 8192  */
#define BYTES_PER_INODE     262144u      /* newfs -i: 4 blocks of data for each inode, for a normal game */
#define SPARE_MIN_BYTES     (64ull << 20)     /* free room left after the tree, as mkufs2.sh does: */
#define SPARE_MAX_BYTES     (512ull << 20)    /* about 0.5 %, between 64 and 512 MiB              */

typedef struct {
  int32_t sblkno, cblkno, iblkno, dblkno;
  int64_t time_;
  int64_t total_blocks, total_data_blocks;
  int32_t num_cg, bsize, fsize, frag, minfree;
  int32_t bmask, fmask, bshift, fshift, maxcontig, maxbpg, fragshift;
  int32_t sectorsize, optim;
  uint32_t fsid0, fsid1;
  int64_t csaddr;
  int32_t cssize, cgsize, ipg, fpg;
  int64_t dirs, free_blocks, free_inodes, free_frags;
  int32_t flags, avgfilesize, avgfpdir, maxsymlinklen;
  int64_t qbmask, qfmask, maxfilesize, sblockloc;
  int32_t maxbsize, contigsumsize;
  int64_t providersize, metaspace;
} ufs2_sb_t;

typedef struct {
  uint16_t mode;
  int16_t  nlink;
  int64_t  size, blocks512;
  int64_t  atime, mtime, ctime, btime;
  int32_t  gen;
  int64_t  direct[NDIRECT];
  int64_t  indirect[NINDIRECT];
  uint32_t dirdepth;
} ufs2_inode_fields_t;

/* blocks: exactly what the writer will ask alloc_block() for (file data and
   indirect blocks, directory blocks) — the size of the image is planned from it. */
typedef struct { uint64_t raw_size, total_entries, blocks; } dirsize_acc_t;

typedef struct { char *name; int is_dir; } scan_ent_t;

typedef struct { uint32_t inode; uint8_t type; char name[256]; } dirent_src_t;
typedef struct { dirent_src_t *items; unsigned n, cap; } dirent_list_t;

typedef struct {
  int64_t  fpb, fpg, dsf;
  int32_t  num_cg;
  int64_t  total_frags;
  int32_t  cur_cg;
  int64_t  next_frag_in_cg;
  int64_t *high_water;              /* num_cg entries */
} alloc_t;

struct ufs2_sums {
  uint32_t *crc;                    /* by inode number, 0 for what is no file */
  uint32_t  n;
  uint64_t  data_bytes;             /* the sum of the files' sizes            */
  uint64_t  tree_hash;              /* the sum of a hash of every entry (folder, inode, type, name), in any order */
};

typedef struct {
  int       fd;
  alloc_t  *alloc;
  int32_t   ipg, iblkno, num_cg;
  int64_t   frags_per_group, timestamp;
  const int *cancel;
  void    (*progress)(void *ctx, uint64_t done_bytes);
  void     *progress_ctx;
  uint64_t  done_bytes;
  ufs2_sums_t *sums;                /* NULL: no CRCs are kept */
  uint32_t  next_inode;
  int32_t  *dirs_per_cg;             /* num_cg entries */
  int64_t  *tail_free;
  uint32_t  tail_free_n, tail_free_cap;
  unsigned  file_count, dir_count;
  int       cancelled;
  char     *err;
  size_t    err_len;
} populate_ctx_t;


/* ------------------------------------------------------------------ helpers */

static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { put16(p, (uint16_t)v); put16(p + 2, (uint16_t)(v >> 16)); }
static void put64(uint8_t *p, uint64_t v) { put32(p, (uint32_t)v); put32(p + 4, (uint32_t)(v >> 32)); }
static uint32_t get32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint64_t get64(const uint8_t *p) { return (uint64_t)get32(p) | (uint64_t)get32(p + 4) << 32; }

static uint64_t ceil_div(uint64_t a, uint64_t b) { return (a + b - 1) / b; }

/* A hash of one directory entry — the folder it is in, the inode it names, its type and its name. Both the writer and
   the check add these up over all entries, and the two sums must be equal: a lost, doubled, swapped or renamed entry
   changes it, which the checksums of the file contents alone (kept by inode) cannot tell. */
static uint64_t
entry_hash(uint32_t parent, uint32_t inode, uint8_t type, const uint8_t *name, size_t namlen) {
  uint64_t h = 1469598103934665603ull;
  uint8_t head[9];
  head[0] = (uint8_t)parent; head[1] = (uint8_t)(parent >> 8); head[2] = (uint8_t)(parent >> 16); head[3] = (uint8_t)(parent >> 24);
  head[4] = (uint8_t)inode;  head[5] = (uint8_t)(inode >> 8);  head[6] = (uint8_t)(inode >> 16);  head[7] = (uint8_t)(inode >> 24);
  head[8] = type;
  for(size_t i = 0; i < sizeof(head); i++) h = (h ^ head[i]) * 1099511628211ull;
  for(size_t i = 0; i < namlen; i++) h = (h ^ name[i]) * 1099511628211ull;
  h ^= h >> 33; h *= 0xff51afd7ed558ccdull; h ^= h >> 33;            /* so that a plain sum of them is no weaker than one */
  return h;
}
static uint64_t align_up(uint64_t v, uint64_t a) { return ceil_div(v, a) * a; }
static int32_t  ilog2_u32(uint32_t v) { int32_t r = 0; while(v > 1) { v >>= 1; r++; } return r; }

/* How many indirect blocks a file of nb data blocks has besides them: none up
   to the 12 direct ones; then the single-indirect block; past the 8192 blocks
   that one holds, the double-indirect block and one single-indirect block for
   every 8192 more. nb must be within UFS2_MAX_FILE_BYTES. */
static uint64_t
indirect_blocks_for(uint64_t nb) {
  if(nb <= NDIRECT) return 0;
  uint64_t rest = nb - NDIRECT;
  if(rest <= (uint64_t)PTRS_PER_BLOCK) return 1;
  return 2 + ceil_div(rest - (uint64_t)PTRS_PER_BLOCK, (uint64_t)PTRS_PER_BLOCK);
}

static int
pwrite_all(int fd, const void *buf, size_t len, uint64_t off) {
  const uint8_t *p = buf;
  while(len) {
    ssize_t w = pwrite(fd, p, len, (off_t)off);
    if(w < 0) { if(errno == EINTR) continue; return -1; }
    if(w == 0) { errno = EIO; return -1; }
    p += w; len -= (size_t)w; off += (uint64_t)w;
  }
  return 0;
}

static int
pread_all(int fd, void *buf, size_t len, uint64_t off) {
  uint8_t *p = buf;
  while(len) {
    ssize_t r = pread(fd, p, len, (off_t)off);
    if(r < 0) { if(errno == EINTR) continue; return -1; }
    if(r == 0) { errno = EIO; return -1; }
    p += r; len -= (size_t)r; off += (uint64_t)r;
  }
  return 0;
}

static ssize_t
read_some(int fd, void *buf, size_t len) {
  uint8_t *p = buf; size_t got = 0;
  while(got < len) {
    ssize_t r = read(fd, p + got, len - got);
    if(r < 0) { if(errno == EINTR) continue; return -1; }
    if(r == 0) break;
    got += (size_t)r;
  }
  return (ssize_t)got;
}

/* The flag is stored from another thread (an atomic store in gameconvert.c),
   so it is loaded atomically here as well. */
static int
is_cancelled(const int *flag) {
  return flag && __atomic_load_n(flag, __ATOMIC_ACQUIRE);
}

/* OS-generated metadata never goes into an image — same list as
   conv_exfat.c's ignored_name(), kept in step with it. */
static int
ignored_name(const char *n) {
  static const char *const names[] = {
    ".ds_store", ".spotlight-v100", ".trashes", ".fseventsd", ".temporaryitems",
    ".documentrevisions-v100", ".apdisk", "__macosx", ".volumeicon.icns",
    "thumbs.db", "ehthumbs.db", "desktop.ini", "$recycle.bin",
    "system volume information",
  };
  if(n[0] == '.' && n[1] == '_') return 1;
  for(size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++)
    if(!strcasecmp(n, names[i])) return 1;
  return 0;
}

static int
ent_cmp(const void *a, const void *b) {
  return strcasecmp(((const scan_ent_t *)a)->name, ((const scan_ent_t *)b)->name);
}

static void
free_scan_ents(scan_ent_t *ents, unsigned n) {
  for(unsigned i = 0; i < n; i++) free(ents[i].name);
  free(ents);
}

/* Lists path's own children (no recursion), filtered and case-insensitively
   sorted — same idiom as conv_exfat.c's scan_dir. UFS2 itself doesn't care
   about directory-entry order (confirmed from UFS2Tool's own writer, which
   uses raw unsorted OS enumeration); sorting here only makes a conversion's
   output reproducible. */
static int
scan_and_sort_dir(const char *path, scan_ent_t **out_ents, unsigned *out_n,
                  char *err, size_t err_len) {
  DIR *d = opendir(path);
  if(!d) { snprintf(err, err_len, "%s ist nicht lesbar: %s", path, ps5tm_io_strerror(errno)); return -1; }
  scan_ent_t *ents = NULL; unsigned n = 0, cap = 0;
  int rc = 0, rd_errno = 0;
  struct dirent *e;
  char child[PATH_MAX];
  for(;;) {
    /* readdir() returns NULL both at the end and on an error, and only errno tells them apart. A listing that broke
       off half way (a stick that fails in the middle of a folder) must not look like a folder that ends there: the
       image would lack the rest and pass every check, because the checks only know what was read. */
    errno = 0;
    e = readdir(d);
    if(!e) { rd_errno = errno; break; }
    if(!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..") || ignored_name(e->d_name)) continue;
    if(strlen(e->d_name) > 255) {
      snprintf(err, err_len, "Der Name \"%s\" ist zu lang.", e->d_name); rc = -1; break;
    }
    if(snprintf(child, sizeof(child), "%s/%s", path, e->d_name) >= (int)sizeof(child)) {
      snprintf(err, err_len, "Ein Pfad ist zu lang."); rc = -1; break;
    }
    struct stat st;
    if(lstat(child, &st) != 0) { snprintf(err, err_len, "%s: %s", child, ps5tm_io_strerror(errno)); rc = -1; break; }
    if(!S_ISDIR(st.st_mode) && !S_ISREG(st.st_mode)) continue;    /* links, devices */
    if(n >= cap) {
      unsigned ncap = cap ? cap * 2 : 64;
      scan_ent_t *p = realloc(ents, ncap * sizeof(scan_ent_t));
      if(!p) { snprintf(err, err_len, "Kein Speicher."); rc = -1; break; }
      ents = p; cap = ncap;
    }
    ents[n].name = strdup(e->d_name);
    ents[n].is_dir = S_ISDIR(st.st_mode);
    if(!ents[n].name) { snprintf(err, err_len, "Kein Speicher."); rc = -1; break; }
    n++;
  }
  closedir(d);
  if(!rc && rd_errno) {
    snprintf(err, err_len, "Der Ordner %s ließ sich nicht vollständig lesen: %s", path, ps5tm_io_strerror(rd_errno));
    rc = -1;
  }
  if(rc) { free_scan_ents(ents, n); return -1; }
  if(n) qsort(ents, n, sizeof(*ents), ent_cmp);
  *out_ents = ents; *out_n = n;
  return 0;
}


/* --------------------------------------------------------------- sizing pass */

static unsigned
dirsiz(unsigned namelen) { return (DIR_HDR_SIZE + namelen + 1 + 3) & ~3u; }

static unsigned
add_dir_ent(unsigned curdirsize, unsigned namelen) {
  unsigned ent = dirsiz(namelen);
  unsigned aligned = (unsigned)align_up(curdirsize, UFS2_DIRBLKSIZ);
  if(ent + curdirsize > aligned) curdirsize = aligned;
  return curdirsize + ent;
}

static int
calc_dir_sizes_recurse(const char *path, dirsize_acc_t *acc, int depth,
                       char *err, size_t err_len) {
  if(depth > MAX_DEPTH) { snprintf(err, err_len, "Der Ordner ist zu tief verschachtelt."); return -1; }
  scan_ent_t *ents = NULL; unsigned n = 0;
  if(scan_and_sort_dir(path, &ents, &n, err, err_len) != 0) return -1;

  unsigned subdirs = 0;
  for(unsigned i = 0; i < n; i++) if(ents[i].is_dir) subdirs++;
  if(subdirs > MAX_SUBDIRS) {
    snprintf(err, err_len, "Zu viele Unterordner in einem Ordner für ein ffpkg-Abbild (%u, höchstens %u): %s",
             subdirs, MAX_SUBDIRS, path);
    free_scan_ents(ents, n);
    return -1;
  }

  unsigned dsize = add_dir_ent(0, 1);
  dsize = add_dir_ent(dsize, 2);
  for(unsigned i = 0; i < n; i++) dsize = add_dir_ent(dsize, (unsigned)strlen(ents[i].name));
  /* A directory gets its 12 direct blocks and no indirect one. A bigger one
     used to be written all the same, with the blocks past the twelfth
     allocated but unreferenced: every entry in them was lost, and the check
     afterwards passed. Refuse it here, naming the folder; the reason comes
     first, since the message may be cut. */
  uint64_t dblocks = ceil_div(dsize, BLOCK_SIZE);
  if(dblocks > NDIRECT) {
    snprintf(err, err_len, "Zu viele Einträge in einem Ordner für ein ffpkg-Abbild "
             "(%u Bytes Verzeichnisdaten, höchstens %u): %s", dsize, (unsigned)NDIRECT * BLOCK_SIZE, path);
    free_scan_ents(ents, n);
    return -1;
  }
  acc->blocks += dblocks;

  int rc = 0;
  char child[PATH_MAX];
  for(unsigned i = 0; !rc && i < n; i++) {
    if(snprintf(child, sizeof(child), "%s/%s", path, ents[i].name) >= (int)sizeof(child)) {
      snprintf(err, err_len, "Ein Pfad ist zu lang."); rc = -1; break;
    }
    acc->total_entries++;
    if(ents[i].is_dir) {
      rc = calc_dir_sizes_recurse(child, acc, depth + 1, err, err_len);
    } else {
      struct stat st;
      if(stat(child, &st) != 0) { snprintf(err, err_len, "%s: %s", child, ps5tm_io_strerror(errno)); rc = -1; break; }
      uint64_t len = (uint64_t)st.st_size;
      /* The limit is checked here, with the whole tree still untouched: it
         used to be met only when the copy reached the file, after everything
         in front of it had been written. */
      if(len > UFS2_MAX_FILE_BYTES) {
        snprintf(err, err_len, "Eine Datei ist zu groß für ein ffpkg-Abbild (%llu Bytes, höchstens %llu): %s",
                 (unsigned long long)len, (unsigned long long)UFS2_MAX_FILE_BYTES, child);
        rc = -1; break;
      }
      acc->raw_size += len;
      if(len > 0) {
        uint64_t bl = ceil_div(len, BLOCK_SIZE);
        acc->blocks += bl + indirect_blocks_for(bl);      /* + its indirect blocks */
      }
    }
  }
  free_scan_ents(ents, n);
  return rc;
}

static void
cg_layout(int32_t inodes_per_group, int32_t *sblkno, int32_t *cblkno,
         int32_t *iblkno, int32_t *dblkno) {
  int32_t fpb = (int32_t)FRAGS_PER_BLOCK;
  *sblkno = (int32_t)align_up(ceil_div(SUPERBLOCK_OFFSET + SUPERBLOCK_SIZE, FRAG_SIZE), (uint64_t)fpb);
  *cblkno = *sblkno + (int32_t)align_up(ceil_div(SUPERBLOCK_SIZE, FRAG_SIZE), (uint64_t)fpb);
  *iblkno = *cblkno + fpb;
  int32_t inopb = BLOCK_SIZE / UFS2_INODE_SIZE;
  int32_t inodeblks = (int32_t)(align_up((uint64_t)inodes_per_group, (uint64_t)inopb) / (uint64_t)inopb) * fpb;
  *dblkno = *iblkno + inodeblks;
}

/* The header of one cylinder group — struct cg, inode map, fragment map,
   cluster summary and cluster map — rounded up to whole fragments. The layout
   reserves exactly one block for it (cg_layout: iblkno = cblkno + fpb); a
   longer header runs into the inode table behind it. */
static int64_t
cg_header_size(int32_t ipg, int64_t frags_per_group) {
  int32_t max_contig = (int32_t)MAXBSIZE / (int32_t)BLOCK_SIZE;
  if(max_contig < 1) max_contig = 1;
  int32_t contig_sum_size = max_contig < MAXCONTIG_SUMMARY ? max_contig : MAXCONTIG_SUMMARY;
  int64_t raw = CG_HEADER_BASE_SIZE + (ipg + 7) / 8 + (frags_per_group + 7) / 8 + 4;
  if(contig_sum_size > 0) raw += (int64_t)contig_sum_size * 4 + (frags_per_group / FRAGS_PER_BLOCK + 7) / 8;
  return (int64_t)align_up((uint64_t)raw, FRAG_SIZE);
}

/* How many whole blocks alloc_block() can hand out in an image of this
   geometry: the data areas of the groups, less the summary area and the
   root directory's first block at the start of group 0. */
static int64_t
alloc_capacity(uint64_t total_size, int64_t frags_per_group, int32_t num_cg, int64_t dblkno) {
  int64_t fpb = FRAGS_PER_BLOCK, total_frags = (int64_t)(total_size / FRAG_SIZE), cap = 0;
  int64_t cs_blk = (int64_t)(align_up((uint64_t)num_cg * CSUM_STRUCT_SIZE, BLOCK_SIZE) / FRAG_SIZE);
  for(int32_t cg = 0; cg < num_cg; cg++) {
    int64_t limit = cg == num_cg - 1 ? total_frags - (int64_t)cg * frags_per_group : frags_per_group;
    int64_t start = cg == 0 ? dblkno + cs_blk + fpb : dblkno;
    if(limit - start >= fpb) cap += (limit - start) / fpb;
  }
  return cap;
}

/* The groups of the image: how many inodes each has, how many fragments, how
   many groups there are. The rule is that of UFS2Tool's newfs for -i
   BYTES_PER_INODE (ComputeInodesPerGroup, then WriteFilesystem): a first count of
   groups from the default of 2048 inodes each gives the inodes a group gets, and
   a group is then as many fragments as it has inodes. */
typedef struct { int32_t ipg, num_cg; int64_t fpg; } ufs2_geom_t;

static ufs2_geom_t
geometry_for(uint64_t total_size) {
  const uint64_t inopb = BLOCK_SIZE / UFS2_INODE_SIZE;
  uint64_t total_frags = total_size / FRAG_SIZE;
  uint64_t ncg0 = ceil_div(total_frags, (uint64_t)DEFAULT_INODES_PER_GROUP * (uint64_t)FRAGS_PER_BLOCK);
  if(ncg0 < 1) ncg0 = 1;
  uint64_t ipg = align_up((total_size / BYTES_PER_INODE) / ncg0, inopb);
  if(ipg < inopb) ipg = inopb;
  ufs2_geom_t g;
  g.ipg = (int32_t)ipg;
  g.fpg = (int64_t)ipg * FRAGS_PER_BLOCK;
  g.num_cg = (int32_t)ceil_div(total_frags, (uint64_t)g.fpg);
  if(g.num_cg < 1) g.num_cg = 1;
  return g;
}

/* How big the image of a tree is, and with it its groups. blocks_needed is what
   the tree asks alloc_block() for (calc_dir_sizes_recurse), inodes_needed its
   entries and the three of a new file system. The image is that many blocks and
   some free room after them, as ShadowMountPlus' mkufs2.sh leaves (about 0.5 %,
   at least 64 MiB, at most 512 MiB), plus the groups' own metadata: the size is
   grown until what alloc_block() can really hand out covers all of it. 0, or -1
   with a reason in err when no such image exists. */
static int
make_fs_size(uint64_t blocks_needed, uint64_t inodes_needed,
             uint64_t *out_total_size, int32_t *out_ipg,
             int64_t *out_frags_per_group, int32_t *out_num_cg,
             char *err, size_t err_len) {
  const int64_t fpb = FRAGS_PER_BLOCK;
  uint64_t spare = blocks_needed * (uint64_t)BLOCK_SIZE / 200;
  if(spare < SPARE_MIN_BYTES) spare = SPARE_MIN_BYTES;
  if(spare > SPARE_MAX_BYTES) spare = SPARE_MAX_BYTES;
  uint64_t want_blocks = blocks_needed + ceil_div(spare, BLOCK_SIZE);
  /* A first guess: the blocks wanted and a share for the groups' own metadata (6 of
     every 512 blocks, a little over 1.2 %, is what a group of 512 blocks needs); the
     rounds below grow it by whatever is still missing. */
  uint64_t total_size = align_up((want_blocks + want_blocks / 80 + 16) * (uint64_t)BLOCK_SIZE, BLOCK_SIZE);
  /* A group has as many inodes as blocks, so a tree is short of inodes only when it
     has far more entries than blocks: nearly nothing but empty files. Growing the
     image for each of them would make one many times the size of its data. */
  const uint64_t limit = total_size + total_size / 4 + (1ull << 30);

  for(int round = 0;; round++) {
    ufs2_geom_t g = geometry_for(total_size);
    int32_t sblkno, cblkno, iblkno, dblkno;
    cg_layout(g.ipg, &sblkno, &cblkno, &iblkno, &dblkno);
    if(cg_header_size(g.ipg, g.fpg) > (int64_t)BLOCK_SIZE || g.fpg - dblkno < fpb) {
      snprintf(err, err_len, "Der Ordner hat zu viele Einträge für ein ffpkg-Abbild.");
      return -1;
    }
    int64_t total_frags = (int64_t)(total_size / FRAG_SIZE);
    int64_t last = total_frags - (int64_t)(g.num_cg - 1) * g.fpg;      /* the size of the last group */
    int64_t cap = alloc_capacity(total_size, g.fpg, g.num_cg, dblkno);
    uint64_t inodes = (uint64_t)g.num_cg * (uint64_t)g.ipg;
    if(last >= dblkno + fpb && cap >= (int64_t)want_blocks && inodes >= inodes_needed) {
      *out_total_size = total_size;
      *out_ipg = g.ipg;
      *out_frags_per_group = g.fpg;
      *out_num_cg = g.num_cg;
      return 0;
    }
    if(round >= 64) {
      snprintf(err, err_len, "Für diesen Ordner ließ sich kein ffpkg-Abbild planen.");
      return -1;
    }
    /* A last group that cannot hold a block of its own grows to one; otherwise the
       blocks that are missing and the metadata of one more group, or, for the
       inodes, the size at which the groups they fill exist: inodes come with whole
       groups (the last counts in full, however short), so a few more blocks
       change nothing until a new group starts. */
    uint64_t grow;
    if(g.num_cg > 1 && last < dblkno + fpb) {
      grow = (uint64_t)(dblkno + fpb - last);
    } else {
      uint64_t miss_blocks = cap < (int64_t)want_blocks ? want_blocks - (uint64_t)cap : 0;
      uint64_t grow_blocks = miss_blocks ? miss_blocks * (uint64_t)fpb + (uint64_t)dblkno : 0;
      uint64_t grow_inodes = 0;
      if(inodes < inodes_needed) {
        uint64_t groups = ceil_div(inodes_needed, (uint64_t)g.ipg);
        uint64_t least = (groups - 1) * (uint64_t)g.fpg + (uint64_t)(dblkno + fpb);
        grow_inodes = least > (uint64_t)total_frags ? least - (uint64_t)total_frags : 1;
      }
      grow = grow_blocks > grow_inodes ? grow_blocks : grow_inodes;
      if(grow_inodes > grow_blocks && total_size + grow * FRAG_SIZE > limit) {
        snprintf(err, err_len, "Der Ordner hat zu viele Einträge im Verhältnis zu seinen Daten (fast nur leere "
                 "Dateien) für ein ffpkg-Abbild.");
        return -1;
      }
    }
    total_size = align_up(total_size + grow * FRAG_SIZE, BLOCK_SIZE);
  }
}

/* ------------------------------------------------------------------ superblock */

static void
build_superblock(uint64_t total_size_bytes, int32_t ipg, int64_t frags_per_group,
                 int32_t num_cg, ufs2_sb_t *sb, int64_t *out_cs_frags, int64_t *out_cs_frags_blk) {
  int32_t fpb = (int32_t)FRAGS_PER_BLOCK;
  int64_t total_frags = (int64_t)(total_size_bytes / FRAG_SIZE);
  int32_t sblkno, cblkno, iblkno, dblkno;
  cg_layout(ipg, &sblkno, &cblkno, &iblkno, &dblkno);

  int32_t max_bsize = MAXBSIZE;
  int32_t max_contig = max_bsize / (int32_t)BLOCK_SIZE; if(max_contig < 1) max_contig = 1;
  int32_t contig_sum_size = max_contig < MAXCONTIG_SUMMARY ? max_contig : MAXCONTIG_SUMMARY;

  int32_t cg_size = (int32_t)cg_header_size(ipg, frags_per_group);   /* what make_fs_size capped */

  int64_t cs_size = (int64_t)align_up((uint64_t)num_cg * CSUM_STRUCT_SIZE, FRAG_SIZE);
  int64_t cs_frags = (int64_t)ceil_div((uint64_t)cs_size, FRAG_SIZE);
  int64_t cs_frags_blk = (int64_t)(align_up((uint64_t)cs_size, BLOCK_SIZE) / FRAG_SIZE);

  int64_t now = (int64_t)time(NULL);
  int32_t bmask_s = (int32_t)(~(uint32_t)(BLOCK_SIZE - 1));
  int32_t fmask_s = (int32_t)(~(uint32_t)(FRAG_SIZE - 1));
  int32_t max_symlink_len = (NDIRECT + NINDIRECT) * 8;
  int64_t nindir = BLOCK_SIZE / 8;
  int64_t max_file_size = (int64_t)BLOCK_SIZE * NDIRECT - 1;
  int64_t sizepb = BLOCK_SIZE;
  for(int i = 0; i < NINDIRECT; i++) { sizepb *= nindir; max_file_size += sizepb; }

  int64_t total_data_blocks = total_frags - sblkno - (int64_t)num_cg * (dblkno - sblkno) - cs_frags;

  memset(sb, 0, sizeof(*sb));
  sb->sblkno = sblkno; sb->cblkno = cblkno; sb->iblkno = iblkno; sb->dblkno = dblkno;
  sb->time_ = now;
  sb->total_blocks = total_frags; sb->total_data_blocks = total_data_blocks;
  sb->num_cg = num_cg; sb->bsize = (int32_t)BLOCK_SIZE; sb->fsize = (int32_t)FRAG_SIZE; sb->frag = fpb;
  sb->minfree = MIN_FREE_PERCENT;
  sb->bmask = bmask_s; sb->fmask = fmask_s;
  sb->bshift = ilog2_u32(BLOCK_SIZE); sb->fshift = ilog2_u32(FRAG_SIZE);
  sb->maxcontig = max_contig; sb->maxbpg = ipg; sb->fragshift = ilog2_u32((uint32_t)fpb);
  /* Not SECTOR_SIZE: FreeBSD's newfs sets the layout's sector size back to
     DEV_BSIZE before it lays anything out (the -S value is only the size the
     device is written in), so fs_fsbtodb and fs_old_nspf count 512-byte units
     — 7 and 128 for 64 KiB fragments, not 4 and 16. The kernel turns a
     fragment number into a disk address with fs_fsbtodb. */
  sb->sectorsize = (int32_t)DEV_BSIZE; sb->optim = OPTIMIZATION_TIME;
  sb->fsid0 = (uint32_t)now; sb->fsid1 = (uint32_t)((now >> 16) ^ getpid());
  sb->cssize = (int32_t)cs_size; sb->cgsize = cg_size; sb->ipg = ipg; sb->fpg = (int32_t)frags_per_group;
  sb->dirs = 1; sb->free_blocks = 0; sb->free_frags = 0;
  sb->free_inodes = (int64_t)num_cg * ipg - 3;
  sb->flags = 0; sb->avgfilesize = 16384; sb->avgfpdir = 64; sb->maxsymlinklen = max_symlink_len;
  /* di_blocks-style reinterpretation: NOT the signed mask, widened — not the
     unsigned mask. Confirmed against UFS2Tool's own output (65535 for both). */
  sb->qbmask = (int64_t)(~bmask_s); sb->qfmask = (int64_t)(~fmask_s);
  sb->maxfilesize = max_file_size;
  sb->sblockloc = SUPERBLOCK_OFFSET; sb->maxbsize = max_bsize; sb->contigsumsize = contig_sum_size;
  sb->csaddr = dblkno; sb->providersize = total_frags;
  sb->metaspace = (frags_per_group * MIN_FREE_PERCENT / 200 / fpb) * fpb;

  *out_cs_frags = cs_frags; *out_cs_frags_blk = cs_frags_blk;
}

static void
serialize_superblock(const ufs2_sb_t *sb, uint8_t *b) {
  put32(b + 0x008, (uint32_t)sb->sblkno);
  put32(b + 0x00C, (uint32_t)sb->cblkno);
  put32(b + 0x010, (uint32_t)sb->iblkno);
  put32(b + 0x014, (uint32_t)sb->dblkno);
  put32(b + 0x020, (uint32_t)sb->time_);
  put32(b + 0x024, (uint32_t)sb->total_blocks);
  put32(b + 0x028, (uint32_t)sb->total_data_blocks);
  put32(b + 0x02C, (uint32_t)sb->num_cg);
  put32(b + 0x030, (uint32_t)sb->bsize);
  put32(b + 0x034, (uint32_t)sb->fsize);
  put32(b + 0x038, (uint32_t)sb->frag);
  put32(b + 0x03C, (uint32_t)sb->minfree);
  put32(b + 0x048, (uint32_t)sb->bmask);
  put32(b + 0x04C, (uint32_t)sb->fmask);
  put32(b + 0x050, (uint32_t)sb->bshift);
  put32(b + 0x054, (uint32_t)sb->fshift);
  put32(b + 0x058, (uint32_t)sb->maxcontig);
  put32(b + 0x05C, (uint32_t)sb->maxbpg);
  put32(b + 0x060, (uint32_t)sb->fragshift);
  put32(b + 0x064, (uint32_t)ilog2_u32((uint32_t)(sb->fsize / sb->sectorsize)));
  put32(b + 0x068, SUPERBLOCK_SIZE);
  put32(b + 0x074, (uint32_t)(sb->bsize / 8));
  put32(b + 0x078, (uint32_t)(sb->bsize / (int32_t)UFS2_INODE_SIZE));
  put32(b + 0x07C, (uint32_t)(sb->fsize / sb->sectorsize));
  put32(b + 0x080, (uint32_t)sb->optim);
  put32(b + 0x090, sb->fsid0);
  put32(b + 0x094, sb->fsid1);
  put32(b + 0x098, (uint32_t)sb->csaddr);
  put32(b + 0x09C, (uint32_t)sb->cssize);
  put32(b + 0x0A0, (uint32_t)sb->cgsize);
  put32(b + 0x0B0, (uint32_t)sb->num_cg);
  put32(b + 0x0B4, 1);
  put32(b + 0x0B8, (uint32_t)sb->ipg);
  put32(b + 0x0BC, (uint32_t)sb->fpg);
  put32(b + 0x0C0, (uint32_t)sb->dirs);
  put32(b + 0x0C4, (uint32_t)sb->free_blocks);
  put32(b + 0x0C8, (uint32_t)sb->free_inodes);
  put32(b + 0x0CC, (uint32_t)sb->free_frags);
  b[0x0D1] = 1; b[0x0D3] = FS_FLAGS_UPDATED;
  b[0x0D4] = '/';
  put32(b + 0x35C, (uint32_t)sb->maxbsize);
  put64(b + 0x368, (uint64_t)sb->providersize);
  put64(b + 0x370, (uint64_t)sb->metaspace);
  put64(b + 0x3E0, (uint64_t)sb->sblockloc);
  put64(b + 0x3E8, (uint64_t)sb->sblockloc);
  put64(b + 0x3F0, (uint64_t)sb->dirs);
  put64(b + 0x3F8, (uint64_t)sb->free_blocks);
  put64(b + 0x400, (uint64_t)sb->free_inodes);
  put64(b + 0x408, (uint64_t)sb->free_frags);
  put64(b + 0x430, (uint64_t)sb->time_);
  put64(b + 0x438, (uint64_t)sb->total_blocks);
  put64(b + 0x440, (uint64_t)sb->total_data_blocks);
  put64(b + 0x448, (uint64_t)sb->csaddr);
  put32(b + 0x4AC, (uint32_t)sb->avgfilesize);
  put32(b + 0x4B0, (uint32_t)sb->avgfpdir);
  put64(b + 0x4B8, (uint64_t)sb->time_);
  put32(b + 0x520, (uint32_t)sb->flags);
  put32(b + 0x524, (uint32_t)sb->contigsumsize);
  put32(b + 0x528, (uint32_t)sb->maxsymlinklen);
  put32(b + 0x52C, FS_44INODEFMT);
  put64(b + 0x530, (uint64_t)sb->maxfilesize);
  put64(b + 0x538, (uint64_t)sb->qbmask);
  put64(b + 0x540, (uint64_t)sb->qfmask);
  put32(b + 0x54C, (uint32_t)FS_DYNAMICPOSTBLFMT);
  put32(b + 0x550, 1);
  put32(b + 0x55C, UFS2_MAGIC);
}

/* What a group's header holds about its free space besides the bitmaps and the
   four counters, worked out from a fragment bitmap (a set bit is a free
   fragment) the way UFS2Tool does when it has finished an image:
   - the cluster map: one set bit for every block that is wholly free,
   - the cluster summary: how many runs of free blocks there are of each length
     (the longest counted as contig_sum_size),
   - cg_frsum: how many runs of free fragments of each length 1..7 there are
     inside the blocks that are only partly free. */
typedef struct {
  int32_t  frsum[8];
  int32_t  cluster_sum[MAXCONTIG_SUMMARY + 1];      /* 1..contig_sum_size */
  uint8_t *cluster_map;                             /* NULL without clusters */
  size_t   cluster_map_bytes;
} cg_maps_t;

static int
frag_is_free(const uint8_t *map, int64_t map_bytes, int64_t f) {
  return f / 8 < map_bytes && ((map[f / 8] >> (f % 8)) & 1);
}

/* 0, or -1 when there is no memory. The caller frees maps->cluster_map. */
static int
compute_cg_maps(const uint8_t *frag_bitmap, int64_t frag_bitmap_bytes, int64_t usable_frags,
                int64_t nclusterblks, int32_t contig_sum_size, cg_maps_t *maps) {
  const int32_t fpb = (int32_t)FRAGS_PER_BLOCK;
  memset(maps, 0, sizeof(*maps));

  if(contig_sum_size > 0 && nclusterblks > 0) {
    maps->cluster_map_bytes = (size_t)((nclusterblks + 7) / 8);
    maps->cluster_map = calloc(maps->cluster_map_bytes, 1);
    if(!maps->cluster_map) return -1;
    for(int64_t blk = 0; blk < nclusterblks; blk++) {
      int all_free = 1;
      for(int32_t ff = 0; ff < fpb; ff++) {
        int64_t f = blk * fpb + ff;
        if(f >= usable_frags || !frag_is_free(frag_bitmap, frag_bitmap_bytes, f)) { all_free = 0; break; }
      }
      if(all_free) maps->cluster_map[blk / 8] |= (uint8_t)(1u << (blk % 8));
    }
    int64_t run = 0;
    for(int64_t blk = 0; blk < nclusterblks; blk++) {
      if((maps->cluster_map[blk / 8] >> (blk % 8)) & 1) run++;
      else if(run) { maps->cluster_sum[run < contig_sum_size ? run : contig_sum_size]++; run = 0; }
    }
    if(run) maps->cluster_sum[run < contig_sum_size ? run : contig_sum_size]++;
  }

  int64_t blocks = usable_frags / fpb;
  for(int64_t blk = 0; blk < blocks; blk++) {
    int all_free = 1;
    for(int32_t ff = 0; ff < fpb && all_free; ff++)
      if(!frag_is_free(frag_bitmap, frag_bitmap_bytes, blk * fpb + ff)) all_free = 0;
    if(all_free) continue;                      /* counted in the cluster map */
    int64_t run = 0;
    for(int32_t ff = 0; ff < fpb; ff++) {
      if(frag_is_free(frag_bitmap, frag_bitmap_bytes, blk * fpb + ff)) run++;
      else { if(run > 0 && run < fpb) maps->frsum[run]++; run = 0; }
    }
    if(run > 0 && run < fpb) maps->frsum[run]++;
  }
  int64_t run = 0;                              /* the last, shorter block of a group */
  for(int64_t f = blocks * fpb; f < usable_frags; f++) {
    if(frag_is_free(frag_bitmap, frag_bitmap_bytes, f)) run++;
    else { if(run > 0 && run < fpb) maps->frsum[run]++; run = 0; }
  }
  if(run > 0 && run < fpb) maps->frsum[run]++;
  return 0;
}

/* Cylinder group header: static layout fields, with the maps of an image whose
   data area is still empty. patch_cg_and_superblock() writes the real ones
   once the tree is in (the bitmaps, the counters, the cluster map and the
   summaries); what is written here fixes where each part sits. */
static void
serialize_cg_header(int32_t cg_index, int64_t total_frags_in_cg, int64_t free_data_frags,
                    int32_t ipg, int64_t timestamp, int64_t data_start_frag,
                    int32_t contig_sum_size, int64_t frags_per_group,
                    int64_t cs_frags, int64_t cs_frags_blk, int32_t sblkno,
                    uint8_t *buf, size_t buf_cap, size_t *out_written) {
  int32_t fpb = (int32_t)FRAGS_PER_BLOCK;
  int64_t inode_bitmap_bytes = (ipg + 7) / 8;
  int64_t frag_bitmap_bytes = (frags_per_group + 7) / 8;
  int32_t inode_bitmap_off = CG_HEADER_BASE_SIZE;
  int32_t frag_bitmap_off = (int32_t)(inode_bitmap_off + inode_bitmap_bytes);
  int32_t next_free_off = (int32_t)(frag_bitmap_off + frag_bitmap_bytes);

  int64_t nclusterblks = 0;
  int32_t clustersumoff = 0, clusteroff = 0;
  if(contig_sum_size > 0) {
    nclusterblks = total_frags_in_cg / fpb;
    int64_t raw_end = frag_bitmap_off + (frags_per_group + 7) / 8;
    clustersumoff = (int32_t)(align_up((uint64_t)raw_end, 4) - 4);
    clusteroff = (int32_t)(clustersumoff + (int64_t)(contig_sum_size + 1) * 4);
    int64_t blocks_for_cluster_bitmap = frags_per_group / fpb;
    next_free_off = (int32_t)(clusteroff + (blocks_for_cluster_bitmap + 7) / 8);
  }

  int64_t total_cg_size = (int64_t)align_up((uint64_t)next_free_off, FRAG_SIZE);
  int64_t min_size = (int64_t)align_up(FRAG_SIZE, SECTOR_SIZE);
  if(total_cg_size < min_size) total_cg_size = min_size;
  size_t buf_len = (size_t)align_up((uint64_t)total_cg_size, SECTOR_SIZE);
  if(buf_len > buf_cap) buf_len = buf_cap;
  memset(buf, 0, buf_len);

  int32_t used_inodes = cg_index == 0 ? 3 : 0;
  int32_t free_inodes = ipg - used_inodes;
  int64_t cs_summary_tail_free = cg_index == 0 ? (cs_frags_blk - cs_frags) : 0;

  int64_t free_blocks, free_frags;
  if(cg_index == 0) {
    free_blocks = free_data_frags / fpb;
    free_frags = cs_summary_tail_free + (free_data_frags % fpb);
  } else {
    int64_t total_free = free_data_frags + sblkno;
    free_blocks = total_free / fpb;
    free_frags = total_free % fpb;
  }
  int32_t dirs = cg_index == 0 ? 1 : 0;

  put32(buf + 0x04, CG_MAGIC);
  put32(buf + 0x08, (uint32_t)timestamp);
  put32(buf + 0x0C, (uint32_t)cg_index);
  put32(buf + 0x14, (uint32_t)total_frags_in_cg);
  put32(buf + 0x18, (uint32_t)dirs);
  put32(buf + 0x1C, (uint32_t)free_blocks);
  put32(buf + 0x20, (uint32_t)free_inodes);
  put32(buf + 0x24, (uint32_t)free_frags);

  put32(buf + 0x5C, (uint32_t)inode_bitmap_off);
  put32(buf + 0x60, (uint32_t)frag_bitmap_off);
  put32(buf + 0x64, (uint32_t)next_free_off);
  put32(buf + 0x68, (uint32_t)clustersumoff);
  put32(buf + 0x6C, (uint32_t)clusteroff);
  put32(buf + 0x70, (uint32_t)nclusterblks);
  put32(buf + 0x74, (uint32_t)ipg);
  int32_t inodes_per_blk = (int32_t)(BLOCK_SIZE / UFS2_INODE_SIZE);
  int32_t initediblk = ipg < 2 * inodes_per_blk ? ipg : 2 * inodes_per_blk;
  put32(buf + 0x78, (uint32_t)initediblk);
  put64(buf + 0x88, (uint64_t)timestamp);

  if(cg_index == 0 && inode_bitmap_bytes > 0) buf[inode_bitmap_off] = 0x07;

  uint8_t *frag_bitmap = buf + frag_bitmap_off;
  int64_t f;
  if(cg_index > 0) {
    int64_t lim = sblkno < total_frags_in_cg ? sblkno : total_frags_in_cg;
    for(f = 0; f < lim; f++) frag_bitmap[f / 8] |= (uint8_t)(1u << (f % 8));
  }
  int64_t first_free;
  if(cg_index == 0) {
    for(f = data_start_frag + cs_frags; f < data_start_frag + cs_frags_blk; f++)
      frag_bitmap[f / 8] |= (uint8_t)(1u << (f % 8));
    first_free = data_start_frag + cs_frags_blk + fpb;
  } else {
    first_free = data_start_frag;
  }
  for(f = first_free; f < total_frags_in_cg; f++) frag_bitmap[f / 8] |= (uint8_t)(1u << (f % 8));

  cg_maps_t maps;
  if(compute_cg_maps(frag_bitmap, frag_bitmap_bytes, total_frags_in_cg, nclusterblks, contig_sum_size, &maps) == 0) {
    for(int i = 0; i < 8; i++) put32(buf + 0x34 + i * 4, (uint32_t)maps.frsum[i]);
    if(maps.cluster_map) {
      memcpy(buf + clusteroff, maps.cluster_map, maps.cluster_map_bytes);
      for(int32_t i = 1; i <= contig_sum_size; i++)
        put32(buf + clustersumoff + i * 4, (uint32_t)maps.cluster_sum[i]);
    }
    free(maps.cluster_map);
  }
  *out_written = buf_len;
}


/* ----------------------------------------------------------------- inode / dir */

static void
write_inode(uint8_t *buf, const ufs2_inode_fields_t *in) {
  put16(buf + 0x00, in->mode);
  put16(buf + 0x02, (uint16_t)in->nlink);
  put32(buf + 0x0C, BLOCK_SIZE);
  put64(buf + 0x10, (uint64_t)in->size);
  put64(buf + 0x18, (uint64_t)in->blocks512);
  put64(buf + 0x20, (uint64_t)in->atime);
  put64(buf + 0x28, (uint64_t)in->mtime);
  put64(buf + 0x30, (uint64_t)in->ctime);
  put64(buf + 0x38, (uint64_t)in->btime);
  put32(buf + 0x50, (uint32_t)in->gen);
  for(int i = 0; i < NDIRECT; i++) put64(buf + 0x70 + i * 8, (uint64_t)in->direct[i]);
  for(int i = 0; i < NINDIRECT; i++) put64(buf + 0x70 + NDIRECT * 8 + i * 8, (uint64_t)in->indirect[i]);
  put32(buf + 0xF0, in->dirdepth);
}

static void
dirent_put(uint8_t *o, uint32_t inode, uint8_t type, const char *name, unsigned reclen) {
  memset(o, 0, reclen);
  put32(o + 0, inode);
  put16(o + 4, (uint16_t)reclen);
  o[6] = type;
  size_t nl = strlen(name);
  o[7] = (uint8_t)nl;
  memcpy(o + 8, name, nl);
}

/* Packs "." + ".." + children into BLOCK_SIZE-sized blocks, UFS2_DIRBLKSIZ-chunk
   aware (the last entry in a chunk is stretched to the chunk boundary) —
   ported from UFS2Tool's WriteDirBlocks/CalculateDirBlocksNeeded and
   validated byte-for-byte against its own output. out == NULL only counts
   the blocks needed (a dry run); otherwise out must be nblk * BLOCK_SIZE
   bytes (nblk from a prior dry run) and cap_blocks == nblk. Returns the
   block count, or 0 if it doesn't fit in cap_blocks. */
static unsigned
pack_dir_blocks(uint32_t self_inode, uint32_t parent_inode,
                const dirent_src_t *children, unsigned nc,
                uint8_t *out, unsigned cap_blocks) {
  unsigned n = nc + 2;
  dirent_src_t *ents = malloc((size_t)n * sizeof(dirent_src_t));
  if(!ents) return 0;
  ents[0].inode = self_inode;   ents[0].type = DT_DIR; snprintf(ents[0].name, sizeof(ents[0].name), ".");
  ents[1].inode = parent_inode; ents[1].type = DT_DIR; snprintf(ents[1].name, sizeof(ents[1].name), "..");
  if(nc) memcpy(ents + 2, children, (size_t)nc * sizeof(dirent_src_t));

  unsigned idx = 0, b = 0;
  for(;;) {
    if(out && b >= cap_blocks) { free(ents); return 0; }
    uint8_t *block = out ? out + (size_t)b * BLOCK_SIZE : NULL;
    unsigned blen = 0;
    while(idx < n) {
      const dirent_src_t *e = &ents[idx];
      unsigned namelen = (unsigned)strlen(e->name);
      unsigned reclen = dirsiz(namelen);
      unsigned min_size = DIR_HDR_SIZE + namelen + 1;
      unsigned pos_in_chunk = blen % UFS2_DIRBLKSIZ;
      unsigned remaining_in_chunk = UFS2_DIRBLKSIZ - pos_in_chunk;
      if(min_size > remaining_in_chunk) {
        if(blen + remaining_in_chunk >= BLOCK_SIZE) break;
        if(block) dirent_put(block + blen, 0, 0, "", remaining_in_chunk);
        blen += remaining_in_chunk;
      }
      if(blen + min_size > BLOCK_SIZE) break;
      int is_last_in_chunk = (idx == n - 1);
      if(!is_last_in_chunk) {
        unsigned next_namelen = (unsigned)strlen(ents[idx + 1].name);
        unsigned next_min = DIR_HDR_SIZE + next_namelen + 1;
        unsigned after_this = blen + reclen;
        unsigned next_pos = after_this % UFS2_DIRBLKSIZ;
        unsigned next_remaining = UFS2_DIRBLKSIZ - next_pos;
        is_last_in_chunk = (next_min > next_remaining && next_pos != 0) ||
                            (after_this + next_min > BLOCK_SIZE);
      }
      if(is_last_in_chunk) {
        pos_in_chunk = blen % UFS2_DIRBLKSIZ;
        reclen = UFS2_DIRBLKSIZ - pos_in_chunk;
      }
      if(block) dirent_put(block + blen, e->inode, e->type, e->name, reclen);
      blen += reclen;
      idx++;
      if(is_last_in_chunk) {
        if(idx < n && blen >= BLOCK_SIZE) break;
        if(idx >= n) break;
      }
    }
    unsigned pos = blen % UFS2_DIRBLKSIZ;
    if(pos != 0) {
      if(block) dirent_put(block + blen, 0, 0, "", UFS2_DIRBLKSIZ - pos);
      blen += UFS2_DIRBLKSIZ - pos;
    }
    while(blen + UFS2_DIRBLKSIZ <= BLOCK_SIZE) {
      if(block) dirent_put(block + blen, 0, 0, "", UFS2_DIRBLKSIZ);
      blen += UFS2_DIRBLKSIZ;
    }
    if(block && blen < BLOCK_SIZE) memset(block + blen, 0, BLOCK_SIZE - blen);
    b++;
    if(idx >= n) break;
  }
  free(ents);
  return idx >= n ? b : 0;
}


/* --------------------------------------------------------------- allocator */

static int64_t
alloc_limit(const alloc_t *a, int32_t cg) {
  if(cg == a->num_cg - 1) return a->total_frags - (int64_t)cg * a->fpg;
  return a->fpg;
}

/* Mirrors AllocateDataBlock's per-CG spill-over bookkeeping exactly. -1 when
   the image has no more room (should not happen: sizing includes slack). */
static int64_t
alloc_block(alloc_t *a) {
  int64_t limit = alloc_limit(a, a->cur_cg);
  if(a->next_frag_in_cg + a->fpb > limit) {
    a->cur_cg++;
    if(a->cur_cg >= a->num_cg) return -1;
    a->next_frag_in_cg = a->dsf;
    limit = alloc_limit(a, a->cur_cg);
    if(a->next_frag_in_cg + a->fpb > limit) return -1;
  }
  int64_t frag = a->next_frag_in_cg;
  int32_t cg = a->cur_cg;
  a->next_frag_in_cg += a->fpb;
  a->high_water[cg] = a->next_frag_in_cg;
  return (int64_t)cg * a->fpg + frag;
}

static int
append_entry(dirent_list_t *list, uint32_t inode, uint8_t type, const char *name,
            char *err, size_t err_len) {
  if(list->n >= list->cap) {
    unsigned ncap = list->cap ? list->cap * 2 : 16;
    dirent_src_t *p = realloc(list->items, (size_t)ncap * sizeof(dirent_src_t));
    if(!p) { snprintf(err, err_len, "Kein Speicher."); return -1; }
    list->items = p; list->cap = ncap;
  }
  dirent_src_t *e = &list->items[list->n++];
  e->inode = inode; e->type = type;
  snprintf(e->name, sizeof(e->name), "%s", name);
  return 0;
}

static void
free_dirent_list(dirent_list_t *list) { free(list->items); list->items = NULL; list->n = list->cap = 0; }

static void
track_tail_free(populate_ctx_t *pc, int64_t frag) {
  if(pc->tail_free_n >= pc->tail_free_cap) {
    uint32_t ncap = pc->tail_free_cap ? pc->tail_free_cap * 2 : 64;
    int64_t *p = realloc(pc->tail_free, (size_t)ncap * sizeof(int64_t));
    if(!p) return;                          /* best-effort: costs a little slack space at worst */
    pc->tail_free = p; pc->tail_free_cap = ncap;
  }
  pc->tail_free[pc->tail_free_n++] = frag;
}

static int
write_inode_at(populate_ctx_t *pc, uint32_t inode_num, const ufs2_inode_fields_t *fi) {
  uint8_t buf[UFS2_INODE_SIZE];
  memset(buf, 0, sizeof(buf));
  write_inode(buf, fi);
  int32_t cg = (int32_t)(inode_num / (uint32_t)pc->ipg);
  uint32_t idx = inode_num % (uint32_t)pc->ipg;
  /* The sizing pass counted the entries. A folder that has grown since then
     would get inodes behind the last group, outside the file system. */
  if(cg >= pc->num_cg) {
    snprintf(pc->err, pc->err_len, "Der Ordner hat sich während der Umwandlung verändert (mehr Einträge als geplant).");
    return -1;
  }
  uint64_t cg_start_byte = (uint64_t)cg * (uint64_t)pc->frags_per_group * FRAG_SIZE;
  uint64_t table_off = cg_start_byte + (uint64_t)pc->iblkno * FRAG_SIZE;
  if(pwrite_all(pc->fd, buf, sizeof(buf), table_off + (uint64_t)idx * UFS2_INODE_SIZE) != 0) {
    snprintf(pc->err, pc->err_len, "Schreibfehler: %s", ps5tm_io_strerror(errno));
    return -1;
  }
  return 0;
}


/* --------------------------------------------------------------- population */

/* How many bytes of a file of this size block number index carries: a whole
   block, except for the last one. */
static size_t
block_bytes(uint64_t size, uint64_t index) {
  uint64_t left = size - index * BLOCK_SIZE;
  return left < BLOCK_SIZE ? (size_t)left : BLOCK_SIZE;
}

/* The file ended before the size it had when its copy began. The block used
   to be padded with zeros and written as if nothing had happened — a
   silently wrong file, where the other writers fail with EIO. */
static void
short_read_error(populate_ctx_t *pc, const char *path) {
  snprintf(pc->err, pc->err_len, "Die Datei ist beim Lesen kürzer geworden: %s", path);
  errno = EIO;
}

/* One file on its way into the image: what the functions below share. */
typedef struct {
  populate_ctx_t *pc;
  const char *path;
  int         src;
  uint64_t    size;
  uint64_t    next;                 /* index of the next block of the file        */
  uint64_t    meta_blocks;          /* indirect blocks taken so far               */
  int64_t     last_frag;            /* the last data block written                */
  uint32_t    crc;                  /* of the data as it is read, in order        */
  uint8_t    *chunk;                /* one block of data                          */
  uint8_t    *ind;                  /* one block of pointers: an indirect block   */
  uint8_t    *dbl;                  /* and the double-indirect block              */
} file_wr_t;

/* Every this many blocks of a big file the progress is reported, so that a
   file of many gigabytes does not stand still on the screen. */
#define PROGRESS_EVERY_BLOCKS 32u

/* The next block of the file: takes the next free block of the image, fills it
   with the file's next bytes (zeros after the last of them) and writes it.
   Returns its address; -1 with the reason in pc->err. */
static int64_t
write_data_block(file_wr_t *fw) {
  populate_ctx_t *pc = fw->pc;
  if(is_cancelled(pc->cancel)) { pc->cancelled = 1; errno = ECANCELED; return -1; }
  int64_t frag = alloc_block(pc->alloc);
  if(frag < 0) { snprintf(pc->err, pc->err_len, "Das Abbild wird zu groß."); return -1; }
  size_t want = block_bytes(fw->size, fw->next);
  ssize_t r = read_some(fw->src, fw->chunk, want);
  if(r < 0) { snprintf(pc->err, pc->err_len, "%s: %s", fw->path, ps5tm_io_strerror(errno)); return -1; }
  if((size_t)r < want) { short_read_error(pc, fw->path); return -1; }
  fw->crc = libdeflate_crc32(fw->crc, fw->chunk, want);
  if(want < BLOCK_SIZE) memset(fw->chunk + want, 0, BLOCK_SIZE - want);
  if(pwrite_all(pc->fd, fw->chunk, BLOCK_SIZE, (uint64_t)frag * FRAG_SIZE) != 0) {
    snprintf(pc->err, pc->err_len, "Schreibfehler: %s", ps5tm_io_strerror(errno)); return -1;
  }
  fw->last_frag = frag;
  fw->next++;
  if(pc->progress && fw->next % PROGRESS_EVERY_BLOCKS == 0) {
    uint64_t done = fw->next * BLOCK_SIZE;
    pc->progress(pc->progress_ctx, pc->done_bytes + (done < fw->size ? done : fw->size));
  }
  return frag;
}

/* An indirect block and the next count blocks of the file (at most
   PTRS_PER_BLOCK) under it. The block itself is taken first, then its data
   blocks, as UFS2Tool does; it is written once they are. Returns its address;
   -1 with the reason in pc->err. */
static int64_t
write_indirect_block(file_wr_t *fw, uint64_t count) {
  populate_ctx_t *pc = fw->pc;
  int64_t self = alloc_block(pc->alloc);
  if(self < 0) { snprintf(pc->err, pc->err_len, "Das Abbild wird zu groß."); return -1; }
  fw->meta_blocks++;
  memset(fw->ind, 0, BLOCK_SIZE);
  for(uint64_t i = 0; i < count; i++) {
    int64_t frag = write_data_block(fw);
    if(frag < 0) return -1;
    put64(fw->ind + i * 8, (uint64_t)frag);
  }
  if(pwrite_all(pc->fd, fw->ind, BLOCK_SIZE, (uint64_t)self * FRAG_SIZE) != 0) {
    snprintf(pc->err, pc->err_len, "Schreibfehler: %s", ps5tm_io_strerror(errno)); return -1;
  }
  return self;
}

/* The double-indirect block and the next count blocks of the file under it
   (at most PTRS_PER_BLOCK squared): itself first, then for each of its
   entries a single-indirect block with its data. Returns its address; -1 with
   the reason in pc->err. */
static int64_t
write_double_indirect_block(file_wr_t *fw, uint64_t count) {
  populate_ctx_t *pc = fw->pc;
  int64_t self = alloc_block(pc->alloc);
  if(self < 0) { snprintf(pc->err, pc->err_len, "Das Abbild wird zu groß."); return -1; }
  fw->meta_blocks++;
  memset(fw->dbl, 0, BLOCK_SIZE);
  for(uint64_t i = 0; count > 0; i++) {
    if(i >= (uint64_t)PTRS_PER_BLOCK) {     /* the size was checked: the file has grown since */
      snprintf(pc->err, pc->err_len, "Die Datei ist beim Lesen größer geworden: %s", fw->path);
      return -1;
    }
    uint64_t n = count < (uint64_t)PTRS_PER_BLOCK ? count : (uint64_t)PTRS_PER_BLOCK;
    int64_t one = write_indirect_block(fw, n);
    if(one < 0) return -1;
    put64(fw->dbl + i * 8, (uint64_t)one);
    count -= n;
  }
  if(pwrite_all(pc->fd, fw->dbl, BLOCK_SIZE, (uint64_t)self * FRAG_SIZE) != 0) {
    snprintf(pc->err, pc->err_len, "Schreibfehler: %s", ps5tm_io_strerror(errno)); return -1;
  }
  return self;
}

static int
write_file_entry(populate_ctx_t *pc, const char *path, uint32_t this_inode) {
  struct stat st;
  if(stat(path, &st) != 0) { snprintf(pc->err, pc->err_len, "%s: %s", path, ps5tm_io_strerror(errno)); return -1; }
  uint64_t size = (uint64_t)st.st_size;
  if(size > UFS2_MAX_FILE_BYTES) {
    snprintf(pc->err, pc->err_len, "Eine Datei ist zu groß für ein ffpkg-Abbild (%llu Bytes, höchstens %llu): %s",
             (unsigned long long)size, (unsigned long long)UFS2_MAX_FILE_BYTES, path);
    return -1;
  }

  int64_t direct[NDIRECT] = {0}, single_frag = 0, double_frag = 0, last_frag = 0;
  unsigned ndirect_used = 0;
  uint64_t meta_blocks = 0;
  int rc = 0;
  uint32_t crc = 0;                  /* of the data as it is read, in order */

  if(size > 0) {
    int src = open(path, O_RDONLY);
    if(src < 0) { snprintf(pc->err, pc->err_len, "%s: %s", path, ps5tm_io_strerror(errno)); return -1; }
    uint64_t blocks_needed = ceil_div(size, BLOCK_SIZE);

    file_wr_t fw; memset(&fw, 0, sizeof(fw));
    fw.pc = pc; fw.path = path; fw.src = src; fw.size = size;
    fw.chunk = malloc(BLOCK_SIZE);
    if(blocks_needed > NDIRECT) fw.ind = malloc(BLOCK_SIZE);
    if(blocks_needed > (uint64_t)NDIRECT + (uint64_t)PTRS_PER_BLOCK) fw.dbl = malloc(BLOCK_SIZE);
    if(!fw.chunk || (blocks_needed > NDIRECT && !fw.ind) ||
       (blocks_needed > (uint64_t)NDIRECT + (uint64_t)PTRS_PER_BLOCK && !fw.dbl)) {
      snprintf(pc->err, pc->err_len, "Kein Speicher.");
      rc = -1;
    }

    /* The order in which the blocks are taken is UFS2Tool's: the direct
       blocks, the single-indirect block and its data, then the double-indirect
       block and, for each of its entries, a single-indirect block and its data. */
    unsigned direct_to_use = (unsigned)(blocks_needed < NDIRECT ? blocks_needed : NDIRECT);
    for(unsigned i = 0; !rc && i < direct_to_use; i++) {
      int64_t frag = write_data_block(&fw);
      if(frag < 0) { rc = -1; break; }
      direct[i] = frag; ndirect_used++;
    }
    if(!rc && fw.next < blocks_needed) {
      uint64_t n = blocks_needed - fw.next;
      if(n > (uint64_t)PTRS_PER_BLOCK) n = (uint64_t)PTRS_PER_BLOCK;
      single_frag = write_indirect_block(&fw, n);
      if(single_frag < 0) rc = -1;
    }
    if(!rc && fw.next < blocks_needed) {
      double_frag = write_double_indirect_block(&fw, blocks_needed - fw.next);
      if(double_frag < 0) rc = -1;
    }
    crc = fw.crc; last_frag = fw.last_frag; meta_blocks = fw.meta_blocks;
    free(fw.chunk); free(fw.ind); free(fw.dbl);
    close(src);
  }
  if(rc) return -1;

  if(size > 0 && last_frag && !single_frag && ndirect_used <= NDIRECT) {
    int64_t fpb = FRAGS_PER_BLOCK;
    uint64_t full_blocks = size / BLOCK_SIZE, tail_bytes = size % BLOCK_SIZE;
    uint64_t used_frags = full_blocks * (uint64_t)fpb;
    if(tail_bytes) used_frags += ceil_div(tail_bytes, FRAG_SIZE);
    uint64_t allocated_frags = (uint64_t)ndirect_used * (uint64_t)fpb;
    if(allocated_frags > used_frags) {
      unsigned used_in_last_block = (unsigned)(used_frags % (uint64_t)fpb);
      if(!used_in_last_block) used_in_last_block = (unsigned)fpb;
      for(unsigned tf = used_in_last_block; tf < (unsigned)fpb; tf++)
        track_tail_free(pc, last_frag + tf);
    }
  }

  uint64_t data_frags = 0, metadata_frags = 0;
  if(size > 0) {
    uint64_t blocks_needed = ceil_div(size, BLOCK_SIZE);
    if(blocks_needed > NDIRECT) {
      data_frags = blocks_needed * (uint64_t)FRAGS_PER_BLOCK;
    } else {
      uint64_t full_blocks = size / BLOCK_SIZE, tail_bytes = size % BLOCK_SIZE;
      data_frags = full_blocks * (uint64_t)FRAGS_PER_BLOCK;
      if(tail_bytes) data_frags += ceil_div(tail_bytes, FRAG_SIZE);
    }
    metadata_frags = meta_blocks * (uint64_t)FRAGS_PER_BLOCK;
  }

  ufs2_inode_fields_t fi; memset(&fi, 0, sizeof(fi));
  fi.mode = IFREG | PERM; fi.nlink = 1;
  fi.size = (int64_t)size;
  fi.blocks512 = (int64_t)((data_frags + metadata_frags) * (FRAG_SIZE / DEV_BSIZE));
  fi.atime = fi.mtime = fi.ctime = fi.btime = pc->timestamp;
  fi.gen = 1;
  for(unsigned i = 0; i < ndirect_used; i++) fi.direct[i] = direct[i];
  if(single_frag) fi.indirect[0] = single_frag;
  if(double_frag) fi.indirect[1] = double_frag;

  if(write_inode_at(pc, this_inode, &fi) != 0) return -1;

  if(pc->sums && this_inode < pc->sums->n) {
    pc->sums->crc[this_inode] = crc;
    pc->sums->data_bytes += size;
  }
  pc->done_bytes += size;
  if(pc->progress) pc->progress(pc->progress_ctx, pc->done_bytes);
  return 0;
}

static int
populate_directory(populate_ctx_t *pc, const char *path, uint32_t self_inode,
                   int depth, dirent_list_t *out_entries) {
  if(depth > MAX_DEPTH) { snprintf(pc->err, pc->err_len, "Der Ordner ist zu tief verschachtelt."); return -1; }
  scan_ent_t *ents = NULL; unsigned n = 0;
  if(scan_and_sort_dir(path, &ents, &n, pc->err, pc->err_len) != 0) return -1;

  int rc = 0;
  char child[PATH_MAX];
  for(unsigned i = 0; !rc && i < n; i++) {
    if(is_cancelled(pc->cancel)) { pc->cancelled = 1; errno = ECANCELED; rc = -1; break; }
    if(snprintf(child, sizeof(child), "%s/%s", path, ents[i].name) >= (int)sizeof(child)) {
      snprintf(pc->err, pc->err_len, "Ein Pfad ist zu lang."); rc = -1; break;
    }

    if(ents[i].is_dir) {
      uint32_t this_inode = pc->next_inode++;
      dirent_list_t sub; memset(&sub, 0, sizeof(sub));
      rc = populate_directory(pc, child, this_inode, depth + 1, &sub);
      unsigned nblk = 0; int64_t *frags = NULL; uint8_t *buf = NULL;
      if(!rc) {
        nblk = pack_dir_blocks(this_inode, self_inode, sub.items, sub.n, NULL, 0);
        if(!nblk) { snprintf(pc->err, pc->err_len, "%s: ließ sich nicht packen.", child); rc = -1; }
        else if(nblk > NDIRECT) {       /* it fitted when the sizing pass looked: the folder has grown */
          snprintf(pc->err, pc->err_len, "Der Ordner hat sich während der Umwandlung verändert "
                   "(zu viele Einträge): %s", child);
          rc = -1;
        }
      }
      if(!rc) {
        frags = malloc((size_t)nblk * sizeof(int64_t));
        buf = malloc((size_t)nblk * BLOCK_SIZE);
        if(!frags || !buf) { snprintf(pc->err, pc->err_len, "Kein Speicher."); rc = -1; }
      }
      for(unsigned k = 0; !rc && k < nblk; k++) {
        frags[k] = alloc_block(pc->alloc);
        if(frags[k] < 0) { snprintf(pc->err, pc->err_len, "Das Abbild wird zu groß."); rc = -1; }
      }
      /* The second call allocates again, and may fail where the dry run did
         not: a block that was never filled must not be written. */
      if(!rc && pack_dir_blocks(this_inode, self_inode, sub.items, sub.n, buf, nblk) != nblk) {
        snprintf(pc->err, pc->err_len, "%s: ließ sich nicht packen.", child); rc = -1;
      }
      for(unsigned k = 0; !rc && k < nblk; k++)
        if(pwrite_all(pc->fd, buf + (size_t)k * BLOCK_SIZE, BLOCK_SIZE, (uint64_t)frags[k] * FRAG_SIZE) != 0) {
          snprintf(pc->err, pc->err_len, "Schreibfehler: %s", ps5tm_io_strerror(errno)); rc = -1;
        }
      if(!rc) {
        unsigned subdirs = 0;
        for(unsigned k = 0; k < sub.n; k++) if(sub.items[k].type == DT_DIR) subdirs++;
        if(subdirs > MAX_SUBDIRS) {         /* the sizing pass refused this; the folder has grown since */
          snprintf(pc->err, pc->err_len, "Der Ordner hat sich während der Umwandlung verändert (zu viele Unterordner): %s", child);
          rc = -1;
        }
        ufs2_inode_fields_t fi; memset(&fi, 0, sizeof(fi));
        fi.mode = IFDIR | PERM; fi.nlink = (int16_t)(2 + subdirs);
        fi.size = (int64_t)nblk * BLOCK_SIZE;
        fi.blocks512 = (int64_t)nblk * (BLOCK_SIZE / DEV_BSIZE);
        fi.atime = fi.mtime = fi.ctime = fi.btime = pc->timestamp;
        fi.gen = 1; fi.dirdepth = (uint32_t)(depth + 1);
        for(unsigned k = 0; k < nblk && k < NDIRECT; k++) fi.direct[k] = frags[k];
        if(!rc) rc = write_inode_at(pc, this_inode, &fi);
      }
      free(frags); free(buf);
      if(!rc) {
        rc = append_entry(out_entries, this_inode, DT_DIR, ents[i].name, pc->err, pc->err_len);
        if(!rc) {
          if(pc->sums) pc->sums->tree_hash += entry_hash(self_inode, this_inode, DT_DIR, (const uint8_t *)ents[i].name, strlen(ents[i].name));
          pc->dir_count++;
          int32_t cg = (int32_t)(this_inode / (uint32_t)pc->ipg);
          if(cg < pc->num_cg) pc->dirs_per_cg[cg]++;
        }
      }
      free_dirent_list(&sub);
    } else {
      uint32_t this_inode = pc->next_inode++;
      rc = write_file_entry(pc, child, this_inode);
      if(!rc) rc = append_entry(out_entries, this_inode, DT_REG, ents[i].name, pc->err, pc->err_len);
      if(!rc && pc->sums) pc->sums->tree_hash += entry_hash(self_inode, this_inode, DT_REG, (const uint8_t *)ents[i].name, strlen(ents[i].name));
      if(!rc) pc->file_count++;
    }
  }
  free_scan_ents(ents, n);
  return rc;
}


/* ------------------------------------------------------------- final bitmap pass */

/* The parts of a group's header that tell what is free, besides the bitmaps and
   the four counters: cg_frsum, the cluster map and its summary, all from the
   final fragment bitmap; and cg_initediblk, which has to take in every inode in
   use (rounded up to whole inode blocks, as UFS2Tool does). hdr_offs holds the
   24 bytes of the header from cg_iusedoff on. */
static int
write_cg_maps(int fd, uint64_t cg_header_off, const uint8_t *hdr_offs,
              const uint8_t *frag_bitmap, int64_t frag_bitmap_bytes, int64_t usable,
              int32_t contig_sum_size, int64_t inodes_used, int32_t ipg,
              char *err, size_t err_len) {
  int32_t clustersumoff = (int32_t)get32(hdr_offs + 12), clusteroff = (int32_t)get32(hdr_offs + 16);
  int64_t nclusterblks = (int32_t)get32(hdr_offs + 20);
  cg_maps_t maps;
  if(compute_cg_maps(frag_bitmap, frag_bitmap_bytes, usable, nclusterblks, contig_sum_size, &maps) != 0) {
    snprintf(err, err_len, "Kein Speicher.");
    return -1;
  }
  int rc = 0;
  uint8_t fr[8 * 4];
  for(int i = 0; i < 8; i++) put32(fr + i * 4, (uint32_t)maps.frsum[i]);
  if(pwrite_all(fd, fr, sizeof(fr), cg_header_off + 0x34) != 0) rc = -1;
  if(!rc && maps.cluster_map) {
    uint8_t sums[MAXCONTIG_SUMMARY * 4];
    for(int32_t i = 1; i <= contig_sum_size; i++) put32(sums + (i - 1) * 4, (uint32_t)maps.cluster_sum[i]);
    if(pwrite_all(fd, maps.cluster_map, maps.cluster_map_bytes, cg_header_off + (uint64_t)clusteroff) != 0 ||
       pwrite_all(fd, sums, (size_t)contig_sum_size * 4, cg_header_off + (uint64_t)clustersumoff + 4) != 0)
      rc = -1;
  }
  if(!rc && inodes_used > 0) {
    int64_t init = (int64_t)align_up((uint64_t)inodes_used, BLOCK_SIZE / UFS2_INODE_SIZE);
    if(init > ipg) init = ipg;
    uint8_t b4[4];
    put32(b4, (uint32_t)init);
    if(pwrite_all(fd, b4, sizeof(b4), cg_header_off + 0x78) != 0) rc = -1;
  }
  free(maps.cluster_map);
  if(rc) snprintf(err, err_len, "Schreibfehler: %s", ps5tm_io_strerror(errno));
  return rc;
}

static int
patch_cg_and_superblock(int fd, ufs2_sb_t *sb, int32_t ipg, int64_t frags_per_group,
                        int64_t data_start_frag, const int32_t *dirs_per_cg,
                        const int64_t *high_water, const int64_t *tail_free, uint32_t tail_free_n,
                        int32_t num_cg, uint32_t next_inode, char *err, size_t err_len) {
  int64_t total_frags = sb->total_blocks;
  int32_t fpb = (int32_t)FRAGS_PER_BLOCK;
  int32_t sblkno = sb->sblkno;
  int64_t cs_frags = (int64_t)ceil_div((uint64_t)sb->cssize, FRAG_SIZE);
  int64_t cs_frags_blk = (int64_t)(align_up((uint64_t)sb->cssize, BLOCK_SIZE) / FRAG_SIZE);
  int64_t cs_summary_tail_free = cs_frags_blk - cs_frags;

  int64_t total_dirs = 0;
  for(int32_t i = 0; i < num_cg; i++) total_dirs += dirs_per_cg[i];
  int64_t total_free_inodes = 0, total_free_blocks = 0, total_free_frag_rem = 0;
  uint8_t *cs_summary = calloc((size_t)cs_frags_blk, FRAG_SIZE);
  if(!cs_summary) { snprintf(err, err_len, "Kein Speicher."); return -1; }

  int rc = 0;
  for(int32_t cg = 0; !rc && cg < num_cg; cg++) {
    int64_t cg_start_frag = (int64_t)cg * frags_per_group;
    uint64_t cg_start_byte = (uint64_t)cg_start_frag * FRAG_SIZE;
    int64_t usable = cg < num_cg - 1 ? frags_per_group : total_frags - cg_start_frag;

    int64_t first_inode_in_cg = (int64_t)cg * ipg;
    int64_t inodes_used = 0;
    if((int64_t)next_inode > first_inode_in_cg) {
      inodes_used = (int64_t)next_inode - first_inode_in_cg;
      if(inodes_used > ipg) inodes_used = ipg;
    }
    int64_t free_inodes = ipg - inodes_used;
    total_free_inodes += free_inodes;
    int64_t inode_bitmap_bytes = (ipg + 7) / 8;
    int64_t frag_bitmap_bytes = (frags_per_group + 7) / 8;
    uint8_t *inode_bitmap = calloc((size_t)inode_bitmap_bytes, 1);
    uint8_t *frag_bitmap = calloc((size_t)frag_bitmap_bytes, 1);
    if(!inode_bitmap || !frag_bitmap) {
      free(inode_bitmap); free(frag_bitmap); snprintf(err, err_len, "Kein Speicher."); rc = -1; break;
    }
    for(int64_t bit = 0; bit < inodes_used; bit++) inode_bitmap[bit / 8] |= (uint8_t)(1u << (bit % 8));

    int64_t used_data_frags_in_cg = high_water[cg] ? high_water[cg] - data_start_frag : 0;
    if(used_data_frags_in_cg < 0) used_data_frags_in_cg = 0;
    int64_t ff;
    if(cg > 0) {
      int64_t lim = sblkno < usable ? sblkno : usable;
      for(ff = 0; ff < lim; ff++) frag_bitmap[ff / 8] |= (uint8_t)(1u << (ff % 8));
    }
    int64_t first_free = data_start_frag + used_data_frags_in_cg;
    for(ff = first_free; ff < usable; ff++) frag_bitmap[ff / 8] |= (uint8_t)(1u << (ff % 8));
    if(cg == 0 && cs_summary_tail_free > 0) {
      int64_t lim2 = data_start_frag + cs_frags_blk < usable ? data_start_frag + cs_frags_blk : usable;
      for(ff = data_start_frag + cs_frags; ff < lim2; ff++) frag_bitmap[ff / 8] |= (uint8_t)(1u << (ff % 8));
    }
    for(uint32_t t = 0; t < tail_free_n; t++) {
      int64_t abs_frag = tail_free[t];
      if(abs_frag >= cg_start_frag && abs_frag < cg_start_frag + usable) {
        int64_t rel = abs_frag - cg_start_frag;
        frag_bitmap[rel / 8] |= (uint8_t)(1u << (rel % 8));
      }
    }

    int64_t total_blocks_in_cg = usable / fpb;
    int64_t free_blocks_in_cg = 0, free_frag_rem_in_cg = 0;
    for(int64_t blk = 0; blk < total_blocks_in_cg; blk++) {
      int64_t base = blk * fpb;
      int free_in_block = 0;
      for(int32_t k = 0; k < fpb; k++) {
        int64_t idx = base + k;
        if((frag_bitmap[idx / 8] >> (idx % 8)) & 1) free_in_block++;
      }
      if(free_in_block == fpb) free_blocks_in_cg++;
      else free_frag_rem_in_cg += free_in_block;
    }
    for(ff = total_blocks_in_cg * fpb; ff < usable; ff++)
      if((frag_bitmap[ff / 8] >> (ff % 8)) & 1) free_frag_rem_in_cg++;
    total_free_blocks += free_blocks_in_cg;
    total_free_frag_rem += free_frag_rem_in_cg;
    int32_t dirs_in_cg = dirs_per_cg[cg];

    uint64_t cg_header_off = cg_start_byte + (uint64_t)sb->cblkno * FRAG_SIZE;
    uint8_t off_buf[24];                    /* cg_iusedoff to cg_nclusterblks */
    if(!rc && pread_all(fd, off_buf, sizeof(off_buf), cg_header_off + 0x5C) != 0) {
      snprintf(err, err_len, "Lesefehler: %s", ps5tm_io_strerror(errno)); rc = -1;
    }
    if(!rc) {
      int32_t inode_bitmap_off = (int32_t)get32(off_buf), frag_bitmap_off = (int32_t)get32(off_buf + 4);
      if(pwrite_all(fd, inode_bitmap, (size_t)inode_bitmap_bytes, cg_header_off + inode_bitmap_off) != 0 ||
         pwrite_all(fd, frag_bitmap, (size_t)frag_bitmap_bytes, cg_header_off + frag_bitmap_off) != 0) {
        snprintf(err, err_len, "Schreibfehler: %s", ps5tm_io_strerror(errno)); rc = -1;
      }
    }
    if(!rc)
      rc = write_cg_maps(fd, cg_header_off, off_buf, frag_bitmap, frag_bitmap_bytes, usable,
                         sb->contigsumsize, inodes_used, ipg, err, err_len);
    if(!rc) {
      uint8_t sum4[16];
      put32(sum4 + 0, (uint32_t)dirs_in_cg);
      put32(sum4 + 4, (uint32_t)free_blocks_in_cg);
      put32(sum4 + 8, (uint32_t)free_inodes);
      put32(sum4 + 12, (uint32_t)free_frag_rem_in_cg);
      if(pwrite_all(fd, sum4, 16, cg_header_off + 0x18) != 0) {
        snprintf(err, err_len, "Schreibfehler: %s", ps5tm_io_strerror(errno)); rc = -1;
      }
    }
    if(!rc) {
      size_t cs_off = (size_t)cg * CSUM_STRUCT_SIZE;
      put32(cs_summary + cs_off + 0, (uint32_t)dirs_in_cg);
      put32(cs_summary + cs_off + 4, (uint32_t)free_blocks_in_cg);
      put32(cs_summary + cs_off + 8, (uint32_t)free_inodes);
      put32(cs_summary + cs_off + 12, (uint32_t)free_frag_rem_in_cg);
    }
    free(inode_bitmap); free(frag_bitmap);
  }

  if(!rc && pwrite_all(fd, cs_summary, (size_t)cs_frags_blk * FRAG_SIZE, (uint64_t)sb->dblkno * FRAG_SIZE) != 0) {
    snprintf(err, err_len, "Schreibfehler: %s", ps5tm_io_strerror(errno)); rc = -1;
  }
  free(cs_summary);
  if(rc) return -1;

  sb->dirs = total_dirs;
  sb->free_blocks = total_free_blocks;
  sb->free_inodes = total_free_inodes;
  sb->free_frags = total_free_frag_rem;
  uint8_t sb_bytes[SUPERBLOCK_SIZE];
  memset(sb_bytes, 0, sizeof(sb_bytes));
  serialize_superblock(sb, sb_bytes);
  if(pwrite_all(fd, sb_bytes, SUPERBLOCK_SIZE, SUPERBLOCK_OFFSET) != 0) {
    snprintf(err, err_len, "Schreibfehler: %s", ps5tm_io_strerror(errno)); return -1;
  }
  /* struct fsrecovery, in the last 20 bytes before the superblock: what fsck
     needs to rebuild a damaged superblock. FreeBSD's newfs writes it there. */
  uint8_t fsr[20];
  put32(fsr + 0, UFS2_MAGIC);
  put32(fsr + 4, (uint32_t)sb->fpg);
  put32(fsr + 8, (uint32_t)ilog2_u32((uint32_t)(sb->fsize / sb->sectorsize)));
  put32(fsr + 12, (uint32_t)sb->sblkno);
  put32(fsr + 16, (uint32_t)sb->num_cg);
  if(pwrite_all(fd, fsr, sizeof(fsr), SUPERBLOCK_OFFSET - sizeof(fsr)) != 0) {
    snprintf(err, err_len, "Schreibfehler: %s", ps5tm_io_strerror(errno)); return -1;
  }
  for(int32_t cg = 1; cg < num_cg; cg++) {
    int64_t cg_start_frag = (int64_t)cg * frags_per_group;
    uint64_t cg_start_byte = (uint64_t)cg_start_frag * FRAG_SIZE;
    int64_t usable = cg < num_cg - 1 ? frags_per_group : total_frags - cg_start_frag;
    uint64_t backup_off = cg_start_byte + (uint64_t)sblkno * FRAG_SIZE;
    if(backup_off + SUPERBLOCK_SIZE <= cg_start_byte + (uint64_t)usable * FRAG_SIZE &&
       pwrite_all(fd, sb_bytes, SUPERBLOCK_SIZE, backup_off) != 0) {
      snprintf(err, err_len, "Schreibfehler: %s", ps5tm_io_strerror(errno)); return -1;
    }
  }
  return 0;
}


/* -------------------------------------------------------------------- build */

uint64_t
ufs2_sums_bytes(const ufs2_sums_t *sums) {
  return sums ? sums->data_bytes : 0;
}

void
ufs2_sums_free(ufs2_sums_t *sums) {
  if(!sums) return;
  free(sums->crc);
  free(sums);
}

/* The sizing pass and the image that follows from it: how big, how many
   groups, how many inodes in each. Reads the folders and the sizes of the
   files, not the files. */
typedef struct {
  dirsize_acc_t acc;
  uint64_t      total_size;
  int32_t       ipg, num_cg;
  int64_t       frags_per_group;
} ufs2_plan_t;

static int
plan_image(const char *source_root, ufs2_plan_t *pl, char *err, size_t err_len) {
  struct stat rst;
  if(stat(source_root, &rst) != 0 || !S_ISDIR(rst.st_mode)) {
    snprintf(err, err_len, "Die Quelle ist kein Ordner.");
    return -1;
  }
  memset(pl, 0, sizeof(*pl));
  if(calc_dir_sizes_recurse(source_root, &pl->acc, 0, err, err_len) != 0) return -1;
  /* acc.blocks counts the root directory's first block, which sits at a fixed
     place and is not handed out by alloc_block(). */
  if(make_fs_size(pl->acc.blocks - 1, pl->acc.total_entries + 3, &pl->total_size,
                  &pl->ipg, &pl->frags_per_group, &pl->num_cg, err, err_len) != 0)
    return -1;
#ifdef PS5TM_HOST_TEST
  /* The tests give the image the size another writer chose, to compare the two
     byte for byte. */
  const char *forced = getenv("PS5TM_UFS2_FORCE_SIZE");
  if(forced && *forced) {
    uint64_t size = strtoull(forced, NULL, 10);
    ufs2_geom_t g = geometry_for(size);
    int32_t sblkno, cblkno, iblkno, dblkno;
    cg_layout(g.ipg, &sblkno, &cblkno, &iblkno, &dblkno);
    if(size % BLOCK_SIZE || alloc_capacity(size, g.fpg, g.num_cg, dblkno) < (int64_t)(pl->acc.blocks - 1) ||
       (uint64_t)g.num_cg * (uint64_t)g.ipg < pl->acc.total_entries + 3) {
      snprintf(err, err_len, "Die vorgegebene Abbildgröße reicht nicht.");
      return -1;
    }
    pl->total_size = size; pl->ipg = g.ipg; pl->frags_per_group = g.fpg; pl->num_cg = g.num_cg;
  }
#endif
  return 0;
}

int
ufs2_plan_size(const char *source_root, uint64_t *image_size, char *err, size_t err_len) {
  ufs2_plan_t plan;
  if(plan_image(source_root, &plan, err, err_len) != 0) return -1;
  *image_size = plan.total_size;
  return 0;
}

int
ufs2_write_tree(int fd, const char *source_root, const int *cancel,
                void (*progress)(void *ctx, uint64_t done_bytes), void *progress_ctx,
                uint64_t *image_size, ufs2_sums_t **sums_out, char *err, size_t err_len) {
  if(sums_out) *sums_out = NULL;

  ufs2_plan_t plan;
  if(plan_image(source_root, &plan, err, err_len) != 0) return -1;
  dirsize_acc_t acc = plan.acc;
  uint64_t total_size = plan.total_size;
  int32_t ipg = plan.ipg, num_cg = plan.num_cg;
  int64_t frags_per_group = plan.frags_per_group;

  ufs2_sb_t sb; int64_t cs_frags, cs_frags_blk;
  build_superblock(total_size, ipg, frags_per_group, num_cg, &sb, &cs_frags, &cs_frags_blk);

  /* make_fs_size() sees to both, so this cannot happen. An image whose group
     header runs into its inode table, or with fewer inodes than the tree has
     entries, is wrong without any error showing — never write one. */
  if((int64_t)sb.cgsize > (int64_t)(sb.iblkno - sb.cblkno) * FRAG_SIZE ||
     (int64_t)num_cg * ipg < (int64_t)(acc.total_entries + 3)) {
    snprintf(err, err_len, "Für diesen Ordner ließ sich kein ffpkg-Abbild planen.");
    return -1;
  }

  if(ftruncate(fd, (off_t)total_size) != 0) {
    snprintf(err, err_len, "Die Zieldatei ließ sich nicht auf die Abbildgröße bringen: %s", ps5tm_io_strerror(errno));
    return -1;
  }

  int64_t total_frags = (int64_t)(total_size / FRAG_SIZE);
  size_t cg_cap = (size_t)align_up((uint64_t)sb.cgsize, SECTOR_SIZE) + FRAG_SIZE;
  uint8_t *cgbuf = malloc(cg_cap);
  if(!cgbuf) { snprintf(err, err_len, "Kein Speicher."); return -1; }

  int rc = 0;
  for(int32_t cg = 0; !rc && cg < num_cg; cg++) {
    int64_t cg_start_frag = (int64_t)cg * frags_per_group;
    uint64_t cg_start_byte = (uint64_t)cg_start_frag * FRAG_SIZE;
    int64_t usable = cg < num_cg - 1 ? frags_per_group : total_frags - cg_start_frag;
    int64_t data_frags_in_cg = usable - sb.dblkno;
    if(cg == 0) data_frags_in_cg -= cs_frags_blk + FRAGS_PER_BLOCK;

    size_t written;
    serialize_cg_header(cg, usable, data_frags_in_cg, ipg, sb.time_, sb.dblkno,
                        sb.contigsumsize, frags_per_group, cs_frags, cs_frags_blk, sb.sblkno,
                        cgbuf, cg_cap, &written);
    if(written > (size_t)(sb.iblkno - sb.cblkno) * FRAG_SIZE) {   /* what is written, not only what was planned */
      snprintf(err, err_len, "Für diesen Ordner ließ sich kein ffpkg-Abbild planen."); rc = -1; break;
    }
    if(pwrite_all(fd, cgbuf, written, cg_start_byte + (uint64_t)sb.cblkno * FRAG_SIZE) != 0) {
      snprintf(err, err_len, "Schreibfehler: %s", ps5tm_io_strerror(errno)); rc = -1;
    }
  }
  free(cgbuf);
  if(rc) return -1;

  int64_t root_data_frag = sb.dblkno + cs_frags_blk;     /* CG0 starts at frag 0 */

  alloc_t alloc; memset(&alloc, 0, sizeof(alloc));
  alloc.fpb = FRAGS_PER_BLOCK; alloc.fpg = frags_per_group; alloc.dsf = sb.dblkno;
  alloc.num_cg = num_cg; alloc.total_frags = total_frags;
  alloc.high_water = calloc((size_t)num_cg, sizeof(int64_t));
  if(!alloc.high_water) { snprintf(err, err_len, "Kein Speicher."); return -1; }
  alloc.next_frag_in_cg = sb.dblkno + cs_frags_blk + FRAGS_PER_BLOCK;
  alloc.high_water[0] = alloc.next_frag_in_cg;

  int32_t *dirs_per_cg = calloc((size_t)num_cg, sizeof(int32_t));
  if(!dirs_per_cg) { free(alloc.high_water); snprintf(err, err_len, "Kein Speicher."); return -1; }
  dirs_per_cg[0] = 1;

  /* One CRC-32 per inode the tree can have: the root is 2, entries follow from
     3, and acc.total_entries counts them (make_fs_size() was given the same
     number plus three). */
  ufs2_sums_t *sums = NULL;
  if(sums_out) {
    sums = calloc(1, sizeof(*sums));
    if(sums) {
      sums->n   = (uint32_t)(acc.total_entries + 4);
      sums->crc = calloc(sums->n, sizeof(uint32_t));
    }
    if(!sums || !sums->crc) {
      ufs2_sums_free(sums);
      free(alloc.high_water); free(dirs_per_cg);
      snprintf(err, err_len, "Kein Speicher.");
      return -1;
    }
  }

  populate_ctx_t pc; memset(&pc, 0, sizeof(pc));
  pc.sums = sums;
  pc.fd = fd; pc.alloc = &alloc; pc.ipg = ipg; pc.iblkno = sb.iblkno; pc.num_cg = num_cg;
  pc.frags_per_group = frags_per_group; pc.timestamp = sb.time_;
  pc.cancel = cancel; pc.progress = progress; pc.progress_ctx = progress_ctx;
  pc.next_inode = 3; pc.dirs_per_cg = dirs_per_cg; pc.err = err; pc.err_len = err_len;

  dirent_list_t root_entries; memset(&root_entries, 0, sizeof(root_entries));
  rc = populate_directory(&pc, source_root, ROOT_INODE, 0, &root_entries);
  /* As many entries went in as the sizing pass counted, or the tree changed between the two reads: a file that came or
     went in a folder read late would otherwise end as an image without it, or as a failed check that names a
     checksum. */
  if(!rc && (uint64_t)pc.next_inode - 3 != acc.total_entries) {
    snprintf(err, err_len, "Der Ordner hat sich während der Umwandlung verändert (%llu statt %llu Einträge).",
             (unsigned long long)pc.next_inode - 3, (unsigned long long)acc.total_entries);
    rc = -1;
  }

  if(!rc) {
    unsigned nblk = pack_dir_blocks(ROOT_INODE, ROOT_INODE, root_entries.items, root_entries.n, NULL, 0);
    if(!nblk) { snprintf(err, err_len, "Das Wurzelverzeichnis ließ sich nicht packen."); rc = -1; }
    else if(nblk > NDIRECT) {         /* it fitted when the sizing pass looked */
      snprintf(err, err_len, "Der Ordner hat sich während der Umwandlung verändert (zu viele Einträge).");
      rc = -1;
    }
    int64_t *frags = NULL; uint8_t *buf = NULL;
    if(!rc) {
      frags = malloc((size_t)nblk * sizeof(int64_t));
      buf = malloc((size_t)nblk * BLOCK_SIZE);
      if(!frags || !buf) { snprintf(err, err_len, "Kein Speicher."); rc = -1; }
    }
    if(!rc) {
      frags[0] = root_data_frag;
      for(unsigned k = 1; k < nblk && !rc; k++) {
        frags[k] = alloc_block(&alloc);
        if(frags[k] < 0) { snprintf(err, err_len, "Das Abbild wird zu groß."); rc = -1; }
      }
    }
    if(!rc && pack_dir_blocks(ROOT_INODE, ROOT_INODE, root_entries.items, root_entries.n, buf, nblk) != nblk) {
      snprintf(err, err_len, "Das Wurzelverzeichnis ließ sich nicht packen.");
      rc = -1;
    }
    for(unsigned k = 0; !rc && k < nblk; k++)
      if(pwrite_all(fd, buf + (size_t)k * BLOCK_SIZE, BLOCK_SIZE, (uint64_t)frags[k] * FRAG_SIZE) != 0) {
        snprintf(err, err_len, "Schreibfehler: %s", ps5tm_io_strerror(errno)); rc = -1;
      }
    if(!rc) {
      unsigned subdirs = 0;
      for(unsigned k = 0; k < root_entries.n; k++) if(root_entries.items[k].type == DT_DIR) subdirs++;
      if(subdirs > MAX_SUBDIRS) {
        snprintf(err, err_len, "Der Ordner hat sich während der Umwandlung verändert (zu viele Unterordner).");
        rc = -1;
      }
      ufs2_inode_fields_t fi; memset(&fi, 0, sizeof(fi));
      fi.mode = IFDIR | PERM; fi.nlink = (int16_t)(2 + subdirs);
      fi.size = (int64_t)nblk * BLOCK_SIZE;
      fi.blocks512 = (int64_t)nblk * (BLOCK_SIZE / DEV_BSIZE);
      fi.atime = fi.mtime = fi.ctime = fi.btime = sb.time_;
      fi.gen = 1; fi.dirdepth = 0;
      for(unsigned k = 0; k < nblk && k < NDIRECT; k++) fi.direct[k] = frags[k];
      if(!rc) rc = write_inode_at(&pc, ROOT_INODE, &fi);
    }
    free(frags); free(buf);
  }
  free_dirent_list(&root_entries);

  if(!rc)
    rc = patch_cg_and_superblock(fd, &sb, ipg, frags_per_group, sb.dblkno, dirs_per_cg,
                                 alloc.high_water, pc.tail_free, pc.tail_free_n, num_cg,
                                 pc.next_inode, err, err_len);

  int cancelled = pc.cancelled;
  free(alloc.high_water);
  free(dirs_per_cg);
  free(pc.tail_free);

  if(!rc && fsync(fd) != 0) { snprintf(err, err_len, "Schreibfehler: %s", ps5tm_io_strerror(errno)); rc = -1; }
  if(rc) {
    ufs2_sums_free(sums);
    if(cancelled) { errno = ECANCELED; snprintf(err, err_len, "Abgebrochen."); }
    return -1;
  }
  if(image_size) *image_size = total_size;
  if(sums_out) *sums_out = sums;
  return 0;
}


/* ------------------------------------------------------------------- verify */

typedef struct {
  int      fd;
  int32_t  ipg, iblkno;
  uint32_t num_cg;
  int64_t  frags_per_group, total_frags;
  uint8_t *ind;                     /* one block: an indirect block of pointers */
  uint8_t *dbl;                     /* one block: a double-indirect block       */
  uint8_t *blk;                     /* one block of file data (with sums)       */
  const ufs2_sums_t *sums;          /* NULL: the structure only                 */
  const int *cancel;
  void   (*progress)(void *ctx, uint64_t done_bytes);
  void    *progress_ctx;
  uint64_t done_bytes;              /* file data read and compared so far       */
  uint64_t tree_hash;               /* the sum of entry_hash() over every entry found */
  char    *err;
  size_t   err_len;
} verify_ctx_t;

typedef struct {
  uint16_t mode;
  int64_t  size, blocks512;
  int64_t  direct[NDIRECT], indirect[NINDIRECT];
} verify_inode_t;

static int
verify_read_inode(verify_ctx_t *vc, uint32_t inode_num, verify_inode_t *in) {
  if(inode_num / (uint32_t)vc->ipg >= vc->num_cg) {
    snprintf(vc->err, vc->err_len, "Inode %u liegt außerhalb des Abbilds.", inode_num);
    return -1;
  }
  int32_t cg = (int32_t)(inode_num / (uint32_t)vc->ipg);
  uint32_t idx = inode_num % (uint32_t)vc->ipg;
  uint64_t cg_start_byte = (uint64_t)cg * (uint64_t)vc->frags_per_group * FRAG_SIZE;
  uint64_t table_off = cg_start_byte + (uint64_t)vc->iblkno * FRAG_SIZE;
  uint8_t buf[UFS2_INODE_SIZE];
  if(pread_all(vc->fd, buf, sizeof(buf), table_off + (uint64_t)idx * UFS2_INODE_SIZE) != 0) {
    snprintf(vc->err, vc->err_len, "Inode %u ist nicht lesbar.", inode_num);
    return -1;
  }
  in->mode = (uint16_t)(buf[0] | (buf[1] << 8));
  in->size = (int64_t)get64(buf + 0x10);
  in->blocks512 = (int64_t)get64(buf + 0x18);
  for(int i = 0; i < NDIRECT; i++) in->direct[i] = (int64_t)get64(buf + 0x70 + i * 8);
  for(int i = 0; i < NINDIRECT; i++) in->indirect[i] = (int64_t)get64(buf + 0x70 + NDIRECT * 8 + i * 8);
  return 0;
}

/* A block pointer in a finished image: a whole-block address with the whole
   block inside the image. */
static int
block_ok(const verify_ctx_t *vc, int64_t p) {
  return p > 0 && p % FRAGS_PER_BLOCK == 0 && p <= vc->total_frags - FRAGS_PER_BLOCK;
}

/* One data block of a file: its pointer must be a whole block inside the image.
   With sums the block is read as well, in the order of the file, and counted
   into a CRC-32 that must be, in the end, the one the writer took of the data
   it read from the original. */
static int
verify_data_block(verify_ctx_t *vc, uint32_t inum, uint64_t size, uint64_t index,
                  int64_t p, uint32_t *crc) {
  if(!block_ok(vc, p)) {
    snprintf(vc->err, vc->err_len, "Ungültiger Blockzeiger in Inode %u.", inum);
    return -1;
  }
  if(!vc->sums) return 0;
  if(vc->cancel && __atomic_load_n(vc->cancel, __ATOMIC_ACQUIRE)) {
    snprintf(vc->err, vc->err_len, "Abgebrochen.");
    errno = ECANCELED;
    return -1;
  }
  size_t want = block_bytes(size, index);
  if(pread_all(vc->fd, vc->blk, want, (uint64_t)p * FRAG_SIZE) != 0) {
    snprintf(vc->err, vc->err_len, "Ein Datenblock von Inode %u ist nicht lesbar: %s", inum, ps5tm_io_strerror(errno));
    return -1;
  }
  *crc = libdeflate_crc32(*crc, vc->blk, want);
  vc->done_bytes += want;
  if(vc->progress) vc->progress(vc->progress_ctx, vc->done_bytes);
  return 0;
}

/* An indirect block of a finished image, read into buf: its own address is a
   whole block inside the image, its first used pointers are too, and not one
   of the others is set. */
static int
verify_pointer_block(verify_ctx_t *vc, uint32_t inum, int64_t p, uint64_t used, uint8_t *buf) {
  if(!block_ok(vc, p) || pread_all(vc->fd, buf, BLOCK_SIZE, (uint64_t)p * FRAG_SIZE) != 0) {
    snprintf(vc->err, vc->err_len, "Ungültiger Indirektblock in Inode %u.", inum);
    return -1;
  }
  for(uint64_t i = 0; i < (uint64_t)PTRS_PER_BLOCK; i++) {
    int64_t q = (int64_t)get64(buf + i * 8);
    if(i < used ? !block_ok(vc, q) : q != 0) {
      snprintf(vc->err, vc->err_len, "Ungültiger Blockzeiger im Indirektblock von Inode %u.", inum);
      return -1;
    }
  }
  return 0;
}

/* A file's inode: its type, its size, pointers for exactly the blocks that
   size needs and none besides (the direct ones, then those under the
   single-indirect block, then those under the double-indirect block), and its
   block count; with sums, what the blocks hold as well. */
static int
verify_file(verify_ctx_t *vc, uint32_t inum, const verify_inode_t *in) {
  if((in->mode & 0xF000) != IFREG) {
    snprintf(vc->err, vc->err_len, "Inode %u sollte eine Datei sein.", inum); return -1;
  }
  if(in->size < 0 || (uint64_t)in->size > UFS2_MAX_FILE_BYTES) {
    snprintf(vc->err, vc->err_len, "Inode %u hat eine ungültige Größe (%lld Bytes).", inum, (long long)in->size);
    return -1;
  }
  const uint64_t ptrs = (uint64_t)PTRS_PER_BLOCK;
  uint64_t size = (uint64_t)in->size, nb = ceil_div(size, BLOCK_SIZE);
  uint64_t idx = 0;                 /* the block of the file the walk is at */
  uint32_t crc = 0;

  for(; idx < NDIRECT; idx++) {
    if(idx < nb) {
      if(verify_data_block(vc, inum, size, idx, in->direct[idx], &crc) != 0) return -1;
    } else if(in->direct[idx] != 0) {
      snprintf(vc->err, vc->err_len, "Ungültiger Blockzeiger in Inode %u.", inum); return -1;
    }
  }

  if(nb > NDIRECT) {
    uint64_t used = nb - NDIRECT < ptrs ? nb - NDIRECT : ptrs;
    if(verify_pointer_block(vc, inum, in->indirect[0], used, vc->ind) != 0) return -1;
    for(uint64_t k = 0; k < used; k++, idx++)
      if(verify_data_block(vc, inum, size, idx, (int64_t)get64(vc->ind + k * 8), &crc) != 0) return -1;
  } else if(in->indirect[0] != 0) {
    snprintf(vc->err, vc->err_len, "Inode %u hat einen Indirektblock, den seine Größe nicht braucht.", inum);
    return -1;
  }

  if(nb > NDIRECT + ptrs) {
    uint64_t rest = nb - NDIRECT - ptrs, singles = ceil_div(rest, ptrs);
    if(verify_pointer_block(vc, inum, in->indirect[1], singles, vc->dbl) != 0) return -1;
    for(uint64_t j = 0; j < singles; j++) {
      uint64_t used = rest - j * ptrs < ptrs ? rest - j * ptrs : ptrs;
      if(verify_pointer_block(vc, inum, (int64_t)get64(vc->dbl + j * 8), used, vc->ind) != 0) return -1;
      for(uint64_t k = 0; k < used; k++, idx++)
        if(verify_data_block(vc, inum, size, idx, (int64_t)get64(vc->ind + k * 8), &crc) != 0) return -1;
    }
  } else if(in->indirect[1] != 0) {
    snprintf(vc->err, vc->err_len, "Inode %u hat einen Indirektblock, den seine Größe nicht braucht.", inum);
    return -1;
  }

  /* The writer never makes a triple-indirect block, and a size within
     UFS2_MAX_FILE_BYTES never needs one. */
  if(in->indirect[2] != 0) {
    snprintf(vc->err, vc->err_len, "Inode %u hat einen Indirektblock, den seine Größe nicht braucht.", inum);
    return -1;
  }

  /* di_blocks is in 512-byte units: a file of more than 12 blocks holds whole
     blocks plus its indirect blocks, a smaller one only the fragments of its
     last block that it needs. */
  uint64_t frags = nb > NDIRECT ? (nb + indirect_blocks_for(nb)) * (uint64_t)FRAGS_PER_BLOCK
                                : size / BLOCK_SIZE * (uint64_t)FRAGS_PER_BLOCK + ceil_div(size % BLOCK_SIZE, FRAG_SIZE);
  if((uint64_t)in->blocks512 != frags * (FRAG_SIZE / DEV_BSIZE)) {
    snprintf(vc->err, vc->err_len, "Inode %u hat eine falsche Blockzahl.", inum); return -1;
  }

  if(vc->sums && (inum >= vc->sums->n || crc != vc->sums->crc[inum])) {
    snprintf(vc->err, vc->err_len, "Der Inhalt der Datei in Inode %u weicht von dem ab, was gelesen wurde "
             "(Prüfsumme %08X statt %08X).", inum, (unsigned)crc,
             inum < vc->sums->n ? (unsigned)vc->sums->crc[inum] : 0u);
    errno = EIO;
    return -1;
  }
  return 0;
}

static int
verify_dir(verify_ctx_t *vc, uint32_t dir_inode, uint32_t parent_inode, int depth) {
  if(depth > MAX_DEPTH) { snprintf(vc->err, vc->err_len, "Das Abbild ist zu tief verschachtelt."); return -1; }
  verify_inode_t in;
  if(verify_read_inode(vc, dir_inode, &in) != 0) return -1;
  if((in.mode & 0xF000) != IFDIR) {
    snprintf(vc->err, vc->err_len, "Inode %u sollte ein Verzeichnis sein.", dir_inode); return -1;
  }
  /* The writer gives a directory whole blocks and its 12 direct pointers
     only. A bigger size means entries in blocks nobody can reach — the check
     used to look at the first twelve and pass such a directory. */
  if(in.size <= 0 || in.size % BLOCK_SIZE || in.size > (int64_t)NDIRECT * BLOCK_SIZE) {
    snprintf(vc->err, vc->err_len, "Das Verzeichnis in Inode %u hat eine ungültige Größe (%lld Bytes).",
             dir_inode, (long long)in.size);
    return -1;
  }
  int nblk = (int)(in.size / BLOCK_SIZE);
  for(int i = 0; i < NDIRECT; i++)
    if(i < nblk ? !block_ok(vc, in.direct[i]) : in.direct[i] != 0) {
      snprintf(vc->err, vc->err_len, "Ungültiger Blockzeiger in Inode %u.", dir_inode); return -1;
    }
  if(in.blocks512 != (int64_t)nblk * (BLOCK_SIZE / DEV_BSIZE)) {
    snprintf(vc->err, vc->err_len, "Inode %u hat eine falsche Blockzahl.", dir_inode); return -1;
  }

  int rc = 0;
  uint8_t *block = malloc(BLOCK_SIZE);
  if(!block) { snprintf(vc->err, vc->err_len, "Kein Speicher."); return -1; }
  for(int i = 0; i < nblk && !rc; i++) {
    if(pread_all(vc->fd, block, BLOCK_SIZE, (uint64_t)in.direct[i] * FRAG_SIZE) != 0) {
      snprintf(vc->err, vc->err_len, "Verzeichnisblock in Inode %u ist nicht lesbar.", dir_inode);
      rc = -1; break;
    }
    int64_t pos = 0;
    while(pos + 8 <= (int64_t)BLOCK_SIZE && !rc) {
      uint32_t e_inode = get32(block + pos);
      uint32_t reclen = (uint32_t)(block[pos + 4] | (block[pos + 5] << 8));
      uint8_t ftype = block[pos + 6], namlen = block[pos + 7];
      /* The kernel's rules for an entry (ufs_dirbadentry): the record is a whole number of 4-byte words, long enough for
         its name and the NUL behind it, and it stays inside its 512-byte section — the records tile the block, and one
         that does not is damage (and would be read past its end). The name has neither '/' nor NUL in it. */
      uint32_t need = (DIR_HDR_SIZE + (uint32_t)namlen + 1 + 3) & ~3u;
      int bad = !reclen || (reclen & 3) || reclen < need || pos + (int64_t)reclen > (int64_t)BLOCK_SIZE ||
                (uint32_t)(pos % UFS2_DIRBLKSIZ) + reclen > UFS2_DIRBLKSIZ || ftype > 15;
      for(unsigned k = 0; !bad && k < namlen; k++) {
        uint8_t c = block[pos + DIR_HDR_SIZE + k];
        if(c == 0 || c == '/') bad = 1;
      }
      if(!bad && block[pos + DIR_HDR_SIZE + namlen] != 0) bad = 1;
      if(bad) {
        snprintf(vc->err, vc->err_len, "Ein Verzeichnisblock in Inode %u ist beschädigt.", dir_inode);
        rc = -1; break;
      }
      const uint8_t *name = block + pos + DIR_HDR_SIZE;
      int is_dot = namlen == 1 && name[0] == '.';
      int is_dotdot = namlen == 2 && name[0] == '.' && name[1] == '.';
      if(i == 0 && pos == 0) {                                      /* every folder starts with "." and ".." */
        if(!is_dot || e_inode != dir_inode || ftype != DT_DIR) {
          snprintf(vc->err, vc->err_len, "Dem Verzeichnis in Inode %u fehlt sein \".\".", dir_inode); rc = -1; break;
        }
      } else if(i == 0 && pos == (int64_t)dirsiz(1)) {
        if(!is_dotdot || e_inode != parent_inode || ftype != DT_DIR) {
          snprintf(vc->err, vc->err_len, "Dem Verzeichnis in Inode %u fehlt sein \"..\".", dir_inode); rc = -1; break;
        }
      } else if(e_inode && (is_dot || is_dotdot)) {
        snprintf(vc->err, vc->err_len, "Ein Verzeichnis in Inode %u hat einen Eintrag \".\" oder \"..\" an falscher Stelle.", dir_inode);
        rc = -1; break;
      } else if(e_inode) {
        vc->tree_hash += entry_hash(dir_inode, e_inode, ftype, name, namlen);
        verify_inode_t child;
        if(verify_read_inode(vc, e_inode, &child) != 0) { rc = -1; break; }
        if(ftype == DT_DIR) rc = verify_dir(vc, e_inode, dir_inode, depth + 1);
        else if(ftype == DT_REG) rc = verify_file(vc, e_inode, &child);
        else {
          snprintf(vc->err, vc->err_len, "Ein Eintrag in Inode %u hat einen unbekannten Typ.", dir_inode);
          rc = -1;
        }
      }
      pos += reclen;
    }
  }
  free(block);
  return rc;
}

/* The superblock as the writer makes it from the geometry in it: the same bytes, but for what depends on when the
   image was made (times, the id) and on what is in it (the counts of folders and free space). A field that is off —
   fs_fsbtodb was once, and showed nowhere until the console tried to read the image — is named by its offset. */
static int
verify_superblock(const uint8_t *sb_bytes, int64_t total_frags, int32_t ipg, int64_t fpg, uint32_t num_cg,
                  char *err, size_t err_len) {
  ufs2_sb_t want;
  int64_t cs_frags, cs_frags_blk;
  build_superblock((uint64_t)total_frags * FRAG_SIZE, ipg, fpg, (int32_t)num_cg, &want, &cs_frags, &cs_frags_blk);
  uint8_t exp[SUPERBLOCK_SIZE], got[SUPERBLOCK_SIZE];
  memset(exp, 0, sizeof(exp));
  serialize_superblock(&want, exp);
  memcpy(got, sb_bytes, sizeof(got));
  static const struct { unsigned off, len; } skip[] = {
    { 0x020, 4 },                       /* time */
    { 0x090, 8 },                       /* fsid */
    { 0x0C0, 16 },                      /* counts of folders, free blocks, free inodes, free fragments (32 bit) */
    { 0x3F0, 32 },                      /* the same in 64 bit */
    { 0x430, 8 },                       /* time */
    { 0x4B8, 8 },                       /* time */
  };
  for(unsigned i = 0; i < sizeof(skip) / sizeof(skip[0]); i++) {
    memset(exp + skip[i].off, 0, skip[i].len);
    memset(got + skip[i].off, 0, skip[i].len);
  }
  for(unsigned off = 0; off < SUPERBLOCK_SIZE; off++) {
    if(exp[off] != got[off]) {
      snprintf(err, err_len, "Der UFS2-Kopf stimmt nicht (Feld bei 0x%03X).", off & ~3u);
      return -1;
    }
  }
  return 0;
}

int
ufs2_verify_tree(int fd, const ufs2_sums_t *sums, const int *cancel,
                 void (*progress)(void *ctx, uint64_t done_bytes), void *progress_ctx,
                 char *err, size_t err_len) {
  uint8_t sb_bytes[SUPERBLOCK_SIZE];
  if(pread_all(fd, sb_bytes, SUPERBLOCK_SIZE, SUPERBLOCK_OFFSET) != 0) {
    snprintf(err, err_len, "Das Abbild ist nicht lesbar.");
    return -1;
  }
  if(get32(sb_bytes + 0x55C) != UFS2_MAGIC) { snprintf(err, err_len, "Der UFS2-Kopf stimmt nicht."); return -1; }
  if(get32(sb_bytes + 0x030) != BLOCK_SIZE || get32(sb_bytes + 0x034) != FRAG_SIZE) {
    snprintf(err, err_len, "Die Blockgrößen im Abbild stimmen nicht.");
    return -1;
  }

  verify_ctx_t vc; memset(&vc, 0, sizeof(vc));
  vc.fd = fd;
  vc.ipg = (int32_t)get32(sb_bytes + 0x0B8);
  vc.iblkno = (int32_t)get32(sb_bytes + 0x010);
  vc.frags_per_group = (int64_t)get32(sb_bytes + 0x0BC);
  vc.total_frags = (int64_t)get64(sb_bytes + 0x438);
  vc.num_cg = get32(sb_bytes + 0x02C);
  vc.err = err; vc.err_len = err_len;
  /* Sizes that no image of this writer has are refused before any arithmetic is done with them. */
  if(vc.ipg <= 0 || vc.ipg > (1 << 20) || vc.frags_per_group <= 0 || vc.frags_per_group > (1ll << 26) || vc.num_cg == 0 ||
     vc.total_frags <= 0 || vc.total_frags > (1ll << 40)) {
    snprintf(err, err_len, "Der UFS2-Kopf stimmt nicht."); return -1;
  }
  /* The groups cover the image exactly (the last one is the short one), and a group's header fits the block in front
     of its inode table: a longer one overwrote the first inodes (the root's among them) and used to show only as a
     broken root directory. */
  uint32_t cblkno = get32(sb_bytes + 0x00C), cgsize = get32(sb_bytes + 0x0A0);
  if((uint64_t)vc.num_cg != ceil_div((uint64_t)vc.total_frags, (uint64_t)vc.frags_per_group) ||
     cgsize == 0 || (uint32_t)vc.iblkno <= cblkno || cgsize > ((uint32_t)vc.iblkno - cblkno) * FRAG_SIZE) {
    snprintf(err, err_len, "Der UFS2-Kopf stimmt nicht (Aufteilung in Gruppen).");
    return -1;
  }
  if(verify_superblock(sb_bytes, vc.total_frags, vc.ipg, vc.frags_per_group, vc.num_cg, err, err_len) != 0) return -1;
  for(uint32_t cg = 0; cg < vc.num_cg; cg++) {
    uint8_t h[16];
    if(pread_all(fd, h, sizeof(h), ((uint64_t)cg * (uint64_t)vc.frags_per_group + cblkno) * FRAG_SIZE) != 0 ||
       get32(h + 4) != CG_MAGIC || get32(h + 0x0C) != cg) {
      snprintf(err, err_len, "Der Kopf der Gruppe %u stimmt nicht.", cg);
      return -1;
    }
  }

  vc.ind = malloc(BLOCK_SIZE);
  vc.dbl = malloc(BLOCK_SIZE);
  vc.blk = sums ? malloc(BLOCK_SIZE) : NULL;
  if(!vc.ind || !vc.dbl || (sums && !vc.blk)) {
    free(vc.ind); free(vc.dbl); free(vc.blk);
    snprintf(err, err_len, "Kein Speicher.");
    return -1;
  }
  vc.sums = sums; vc.cancel = cancel; vc.progress = progress; vc.progress_ctx = progress_ctx;
  int rc = verify_dir(&vc, ROOT_INODE, ROOT_INODE, 0);
  /* With the sums the whole tree is known: its entries (names, inodes, types) and the bytes of all the files must be
     the ones the writer made and read. */
  if(!rc && sums && vc.tree_hash != sums->tree_hash) {
    snprintf(err, err_len, "Die Ordner im Abbild enthalten nicht die Einträge, die geschrieben wurden (Namen, Zuordnung oder Zahl).");
    rc = -1;
  }
  if(!rc && sums && vc.done_bytes != sums->data_bytes) {
    snprintf(err, err_len, "Im Abbild wurden %llu Bytes Dateiinhalt gefunden, geschrieben wurden %llu.",
             (unsigned long long)vc.done_bytes, (unsigned long long)sums->data_bytes);
    rc = -1;
  }
  free(vc.ind);
  free(vc.dbl);
  free(vc.blk);
  return rc;
}
