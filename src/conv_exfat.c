/* exFAT image writer, forward only.
 *
 * A C port of MkPFS's exfat_writer.py (PSBrew/MkPFS, GPL-3.0,
 * https://github.com/PSBrew/MkPFS, commit 78eda0a). Same layout, same
 * choices, so an image from here and one from MkPFS are built alike:
 *
 *   - the whole layout is computed before the first byte goes out (every
 *     size is known once the tree is scanned), then the image is emitted
 *     strictly in offset order: boot region and its backup, the FAT, then the
 *     cluster heap — allocation bitmap, up-case table, root directory, and
 *     every directory and file contiguously in pre-order. Nothing is written
 *     out of order, so the stream can go straight into a compressor
 *   - 64 KiB clusters: ShadowMountPlus wants them for its fast path
 *   - one FAT, which still carries an explicit chain for every run (the
 *     widely compatible form); every heap cluster is allocated
 *   - fixed timestamps, OS metadata (.DS_Store, Thumbs.db, ...) left out
 *
 * Two departures, both about size rather than layout. The FAT and the
 * bitmap are generated as they stream instead of being built in memory —
 * for a 100 GB game the FAT alone is 6 MB, which a payload's heap would
 * notice. And names are converted properly from UTF-8 and folded through
 * the real up-case table, where MkPFS assumes ASCII. */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "conv_exfat.h"
#include "ioerr.h"
#include "conv_exfat_upcase.inc"

#define SECTOR          512u
#define SECTOR_SHIFT    9
#define CLUSTER         (64u * 1024)
#define SPC             (CLUSTER / SECTOR)            /* 128 */
#define SPC_SHIFT       7
#define FAT_OFFSET      128u                          /* sectors, as newfs_exfat */
#define FAT_EOC         0xFFFFFFFFu
#define FAT_MEDIA       0xFFFFFFF8u
#define FIRST_CLUSTER   2u
#define NAME_PER_ENTRY  15
#define FIXED_TIME      ((uint32_t)(((2024 - 1980) << 25) | (1 << 21) | (1 << 16)))
#define MAX_DEPTH       32
#define BOOT_BYTES      (12u * SECTOR)

typedef struct {
  uint32_t first_child;     /* directories: children are contiguous */
  uint32_t nchildren;
  uint32_t first_cluster;
  uint32_t cluster_count;
  uint64_t size;            /* files */
  uint32_t path_off;        /* files: absolute path in the string pool */
  uint32_t name_off;        /* UTF-16 name in the name pool */
  uint8_t  name_len;        /* UTF-16 code units, 1..255 */
  uint8_t  is_dir;
} xnode_t;

enum { SEG_BOOT, SEG_BOOT2, SEG_PAD1, SEG_FAT, SEG_PAD2, SEG_BITMAP,
       SEG_UPCASE, SEG_ROOT, SEG_TREE, SEG_END };

struct exfat_image {
  xnode_t  *nodes;
  uint32_t  nnodes, cap_nodes;
  char     *paths;
  uint32_t  paths_len, paths_cap;
  uint16_t *names;
  uint32_t  names_len, names_cap;
  uint16_t *upmap;          /* 65536 entries, built when the image is planned */

  uint32_t  bitmap_clusters, upcase_clusters, cluster_count, root_cluster;
  uint32_t  fat_sectors, heap_offset;
  uint64_t  volume_sectors;
  uint32_t  serial;
  unsigned  files;
  uint64_t  payload;        /* bytes of file data */

  uint32_t *runs;           /* cluster runs in cluster order, for the FAT */
  uint32_t  nruns, cap_runs;
  uint32_t *emit;           /* pre-order nodes with clusters, root excluded */
  uint32_t  nemit, cap_emit;

  /* streaming state */
  int       seg;
  uint64_t  seg_pos, seg_len;
  uint8_t   boot[BOOT_BYTES];
  uint32_t  fat_run, fat_left, fat_cluster;   /* FAT cursor */
  uint64_t  fat_entry;                        /* index of the entry in cur */
  uint32_t  fat_cur;
  uint8_t  *dirbuf;         /* entries of the directory being emitted */
  size_t    dirbuf_len;
  uint32_t  item;           /* index into emit */
  uint32_t  item_node;
  int       fd;
  char      where[256];
  int       error;
};


/* ------------------------------------------------------------------ helpers */

static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { put16(p, (uint16_t)v); put16(p + 2, (uint16_t)(v >> 16)); }
static void put64(uint8_t *p, uint64_t v) { put32(p, (uint32_t)v); put32(p + 4, (uint32_t)(v >> 32)); }

static uint64_t ceil_div(uint64_t a, uint64_t b) { return (a + b - 1) / b; }
static uint64_t align_up(uint64_t v, uint64_t a) { return ceil_div(v, a) * a; }

static int
grow(void **p, uint32_t *cap, uint32_t need, size_t elem) {
  if(need <= *cap) return 0;
  uint32_t n = *cap ? *cap : 256;
  while(n < need) {
    if(n > 0x7FFFFFFFu / 2) return -1;
    n *= 2;
  }
  void *q = realloc(*p, (size_t)n * elem);
  if(!q) return -1;
  *p = q;
  *cap = n;
  return 0;
}

/* OS-generated metadata never goes into an image (MkPFS utils.IGNORED_NAMES,
   plus AppleDouble "._*" forks). */
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

/* UTF-8 to UTF-16; -1 for invalid UTF-8, a name too long for exFAT (255
   units), or a character exFAT forbids in names. */
static int
utf8_to_utf16(const char *s, uint16_t *out, unsigned max) {
  unsigned n = 0;
  const unsigned char *p = (const unsigned char *)s;
  while(*p) {
    uint32_t cp;
    if(p[0] < 0x80)                              { cp = p[0]; p += 1; }
    else if((p[0] & 0xE0) == 0xC0 && (p[1] & 0xC0) == 0x80) {
      cp = ((uint32_t)(p[0] & 0x1F) << 6) | (p[1] & 0x3F); p += 2;
      if(cp < 0x80) return -1;
    } else if((p[0] & 0xF0) == 0xE0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) {
      cp = ((uint32_t)(p[0] & 0x0F) << 12) | ((uint32_t)(p[1] & 0x3F) << 6) | (p[2] & 0x3F);
      p += 3;
      if(cp < 0x800 || (cp >= 0xD800 && cp <= 0xDFFF)) return -1;
    } else if((p[0] & 0xF8) == 0xF0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80 &&
              (p[3] & 0xC0) == 0x80) {
      cp = ((uint32_t)(p[0] & 0x07) << 18) | ((uint32_t)(p[1] & 0x3F) << 12) |
           ((uint32_t)(p[2] & 0x3F) << 6) | (p[3] & 0x3F);
      p += 4;
      if(cp < 0x10000 || cp > 0x10FFFF) return -1;
    } else {
      return -1;
    }
    if(cp < 0x20 || (cp < 0x80 && strchr("\"*/:<>?\\|", (int)cp))) return -1;
    if(cp >= 0x10000) {
      if(n + 2 > max) return -1;
      cp -= 0x10000;
      out[n++] = (uint16_t)(0xD800 | (cp >> 10));
      out[n++] = (uint16_t)(0xDC00 | (cp & 0x3FF));
    } else {
      if(n + 1 > max) return -1;
      out[n++] = (uint16_t)cp;
    }
  }
  return (int)n;
}

/* The up-case table unpacked: runs of identity mappings are stored as
   0xFFFF followed by their length. */
static int
build_upmap(exfat_image_t *im) {
  if(im->upmap) return 0;
  im->upmap = malloc(65536 * sizeof(uint16_t));
  if(!im->upmap) return -1;
  uint32_t cp = 0;
  for(size_t i = 0; i + 1 < EXFAT_UPCASE_SIZE && cp < 65536; i += 2) {
    uint16_t v = (uint16_t)(k_exfat_upcase[i] | (k_exfat_upcase[i + 1] << 8));
    if(v == 0xFFFF && i + 3 < EXFAT_UPCASE_SIZE) {
      i += 2;
      uint16_t run = (uint16_t)(k_exfat_upcase[i] | (k_exfat_upcase[i + 1] << 8));
      for(uint32_t k = 0; k < run && cp < 65536; k++, cp++) im->upmap[cp] = (uint16_t)cp;
    } else {
      im->upmap[cp] = v;
      cp++;
    }
  }
  for(; cp < 65536; cp++) im->upmap[cp] = (uint16_t)cp;
  return 0;
}

/* One UTF-16 unit as exFAT compares names: up-cased through the table. The
   table exists from the moment the image is planned (exfat_image_plan builds
   it first): building it here, on the first non-ASCII name, meant that a
   failing malloc was swallowed and a hash computed without the table was
   written — names the volume could then not find. */
static uint16_t
fold_unit(const exfat_image_t *im, uint16_t c) {
  if(c >= 'a' && c <= 'z') return (uint16_t)(c - 32);
  return c >= 0x80 ? im->upmap[c] : c;
}

static uint16_t
name_hash(const exfat_image_t *im, const uint16_t *u, unsigned n) {
  uint32_t h = 0;       /* 16 bits wide; unsigned and masked, so nothing here overflows as a signed int */
  for(unsigned i = 0; i < n; i++) {
    uint16_t c = fold_unit(im, u[i]);
    h = (((h << 15) | (h >> 1)) + (c & 0xFFu)) & 0xFFFFu;
    h = (((h << 15) | (h >> 1)) + (c >> 8)) & 0xFFFFu;
  }
  return (uint16_t)h;
}


/* ----------------------------------------------------------------- scanning */

typedef struct {
  char    *name;
  int      is_dir;
  uint64_t size;
} scan_ent_t;

static int
ent_cmp(const void *a, const void *b) {
  return strcasecmp(((const scan_ent_t *)a)->name, ((const scan_ent_t *)b)->name);
}

static int
add_name(exfat_image_t *im, xnode_t *nd, const char *name, char *err, size_t err_len) {
  uint16_t u[255];
  int n = utf8_to_utf16(name, u, 255);
  if(n <= 0) {
    snprintf(err, err_len, "Der Name „%s“ passt nicht in ein exFAT-Abbild.", name);
    return -1;
  }
  if(grow((void **)&im->names, &im->names_cap, im->names_len + (uint32_t)n,
          sizeof(uint16_t)) != 0)
    return -1;
  memcpy(im->names + im->names_len, u, (size_t)n * sizeof(uint16_t));
  nd->name_off = im->names_len;
  nd->name_len = (uint8_t)n;
  im->names_len += (uint32_t)n;
  return 0;
}

static int
add_path(exfat_image_t *im, xnode_t *nd, const char *path) {
  uint32_t n = (uint32_t)strlen(path) + 1;
  if(grow((void **)&im->paths, &im->paths_cap, im->paths_len + n, 1) != 0) return -1;
  memcpy(im->paths + im->paths_len, path, n);
  nd->path_off = im->paths_len;
  im->paths_len += n;
  return 0;
}

/* One directory's names, up-cased as exFAT compares them. */
typedef struct {
  const uint16_t *k;
  unsigned        len;
  uint32_t        idx;      /* position among the directory's entries */
} clash_key_t;

static int
clash_cmp(const void *a, const void *b) {
  const clash_key_t *x = a, *y = b;
  unsigned m = x->len < y->len ? x->len : y->len;
  for(unsigned i = 0; i < m; i++)
    if(x->k[i] != y->k[i]) return x->k[i] < y->k[i] ? -1 : 1;
  return x->len < y->len ? -1 : x->len > y->len;
}

/* exFAT finds a name case-insensitively, through the up-case table. Two
   entries of one directory whose names are equal that way (Readme.txt and
   README.TXT, École and école: both fine on the console's case-sensitive file
   systems) make a volume where one name leads to one file and the other
   file cannot be reached; other systems call it damaged. They are refused
   here, while the image is planned, and both are named. -1 with err set for
   a clash, -1 with err empty when memory ran out (the caller words that). */
static int
check_name_clashes(const exfat_image_t *im, uint32_t first, const scan_ent_t *ents, uint32_t n,
                   const char *dirpath, char *err, size_t err_len) {
  if(n < 2) return 0;
  size_t units = 0;
  for(uint32_t i = 0; i < n; i++) units += im->nodes[first + i].name_len;
  clash_key_t *keys = malloc((size_t)n * sizeof(*keys));
  uint16_t *pool = malloc(units * sizeof(uint16_t));
  if(!keys || !pool) { free(keys); free(pool); return -1; }
  size_t at = 0;
  for(uint32_t i = 0; i < n; i++) {
    const xnode_t *nd = &im->nodes[first + i];
    for(unsigned k = 0; k < nd->name_len; k++) pool[at + k] = fold_unit(im, im->names[nd->name_off + k]);
    keys[i].k = pool + at; keys[i].len = nd->name_len; keys[i].idx = i;
    at += nd->name_len;
  }
  qsort(keys, n, sizeof(*keys), clash_cmp);
  int rc = 0;
  for(uint32_t i = 1; i < n && !rc; i++)
    if(clash_cmp(&keys[i - 1], &keys[i]) == 0) {
      snprintf(err, err_len, "Diese beiden Namen sind in exFAT gleich: „%s“ und „%s“ (Ordner %s).",
               ents[keys[i - 1].idx].name, ents[keys[i].idx].name, dirpath);
      rc = -1;
    }
  free(keys);
  free(pool);
  return rc;
}

static int
scan_dir(exfat_image_t *im, uint32_t dir, char *path, size_t len, int depth,
         char *err, size_t err_len) {
  if(depth > MAX_DEPTH) {
    snprintf(err, err_len, "Der Ordner ist zu tief verschachtelt.");
    return -1;
  }
  DIR *d = opendir(path);
  if(!d) {
    snprintf(err, err_len, "%s ist nicht lesbar: %s", path, ps5tm_io_strerror(errno));
    return -1;
  }
  scan_ent_t *ents = NULL;
  uint32_t n = 0, cap = 0;
  int rc = 0, rd_errno = 0;
  struct dirent *e;
  for(;;) {
    /* readdir() returns NULL at the end and on an error alike; only errno tells them apart. A listing that broke off
       half way must not pass for a folder that ends there: the image would lack the rest and every check would pass. */
    errno = 0;
    e = readdir(d);
    if(!e) { rd_errno = errno; break; }
    if(!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..") || ignored_name(e->d_name))
      continue;
    size_t nl = strlen(e->d_name);
    if(len + 1 + nl >= 1024) { snprintf(err, err_len, "Ein Pfad ist zu lang."); rc = -1; break; }
    path[len] = '/';
    memcpy(path + len + 1, e->d_name, nl + 1);
    struct stat st;
    int ok = lstat(path, &st) == 0;
    path[len] = 0;
    if(!ok) { snprintf(err, err_len, "%s: %s", e->d_name, ps5tm_io_strerror(errno)); rc = -1; break; }
    if(!S_ISDIR(st.st_mode) && !S_ISREG(st.st_mode)) continue;   /* links, devices */
    if(grow((void **)&ents, &cap, n + 1, sizeof(*ents)) != 0 ||
       !(ents[n].name = strdup(e->d_name))) { rc = -1; break; }
    ents[n].is_dir = S_ISDIR(st.st_mode);
    ents[n].size   = S_ISREG(st.st_mode) ? (uint64_t)st.st_size : 0;
    n++;
  }
  closedir(d);
  if(!rc && rd_errno) {
    snprintf(err, err_len, "Der Ordner %s ließ sich nicht vollständig lesen: %s", path, ps5tm_io_strerror(rd_errno));
    rc = -1;
  }

  if(!rc && n) qsort(ents, n, sizeof(*ents), ent_cmp);

  uint32_t first = im->nnodes;
  if(!rc && grow((void **)&im->nodes, &im->cap_nodes, im->nnodes + n, sizeof(xnode_t)) != 0)
    rc = -1;
  for(uint32_t i = 0; !rc && i < n; i++) {
    xnode_t *nd = &im->nodes[im->nnodes];
    memset(nd, 0, sizeof(*nd));
    nd->is_dir = (uint8_t)ents[i].is_dir;
    nd->size   = ents[i].size;
    if(add_name(im, nd, ents[i].name, err, err_len) != 0) { rc = -1; break; }
    if(!nd->is_dir) {
      size_t nl = strlen(ents[i].name);
      path[len] = '/';
      memcpy(path + len + 1, ents[i].name, nl + 1);
      int prc = add_path(im, &im->nodes[im->nnodes], path);
      path[len] = 0;
      if(prc != 0) { rc = -1; break; }
      im->files++;
      im->payload += ents[i].size;
    }
    im->nnodes++;
  }
  if(!rc) rc = check_name_clashes(im, first, ents, n, path, err, err_len);
  if(!rc) {
    im->nodes[dir].first_child = first;
    im->nodes[dir].nchildren   = n;
  }
  for(uint32_t i = 0; !rc && i < n; i++) {
    if(!ents[i].is_dir) continue;
    size_t nl = strlen(ents[i].name);
    path[len] = '/';
    memcpy(path + len + 1, ents[i].name, nl + 1);
    rc = scan_dir(im, first + i, path, len + 1 + nl, depth + 1, err, err_len);
    path[len] = 0;
  }
  for(uint32_t i = 0; i < n; i++) free(ents[i].name);
  free(ents);
  if(rc && !err[0]) snprintf(err, err_len, "Kein Speicher beim Einlesen des Ordners.");
  return rc;
}


/* ------------------------------------------------------------------- layout */

static uint32_t
dir_entries(const exfat_image_t *im, uint32_t idx, int is_root) {
  const xnode_t *nd = &im->nodes[idx];
  uint32_t count = is_root ? 3 : 0;   /* root: volume label, bitmap, up-case */
  for(uint32_t i = 0; i < nd->nchildren; i++) {
    const xnode_t *c = &im->nodes[nd->first_child + i];
    count += 2 + (uint32_t)ceil_div(c->name_len, NAME_PER_ENTRY);
  }
  return count;
}

static uint64_t
node_clusters(const exfat_image_t *im, uint32_t idx, int is_root) {
  const xnode_t *nd = &im->nodes[idx];
  if(nd->is_dir) {
    uint64_t c = ceil_div((uint64_t)dir_entries(im, idx, is_root) * 32, CLUSTER);
    return c ? c : 1;
  }
  return ceil_div(nd->size, CLUSTER);
}

static uint64_t
sum_clusters(const exfat_image_t *im, uint32_t dir) {
  uint64_t total = 0;
  const xnode_t *nd = &im->nodes[dir];
  for(uint32_t i = 0; i < nd->nchildren; i++) {
    uint32_t c = nd->first_child + i;
    total += node_clusters(im, c, 0);
    if(im->nodes[c].is_dir) total += sum_clusters(im, c);
  }
  return total;
}

static int
push_run(exfat_image_t *im, uint32_t count) {
  if(grow((void **)&im->runs, &im->cap_runs, im->nruns + 1, sizeof(uint32_t)) != 0) return -1;
  im->runs[im->nruns++] = count;
  return 0;
}

/* Pre-order, exactly the order in which the tree is emitted. */
static int
assign(exfat_image_t *im, uint32_t dir, uint32_t *next) {
  uint32_t first = im->nodes[dir].first_child, n = im->nodes[dir].nchildren;
  for(uint32_t i = 0; i < n; i++) {
    uint32_t c = first + i;
    xnode_t *nd = &im->nodes[c];
    nd->cluster_count = (uint32_t)node_clusters(im, c, 0);
    if(nd->cluster_count && !(!nd->is_dir && nd->size == 0)) {
      nd->first_cluster = *next;
      *next += nd->cluster_count;
      if(push_run(im, nd->cluster_count) != 0 ||
         grow((void **)&im->emit, &im->cap_emit, im->nemit + 1, sizeof(uint32_t)) != 0)
        return -1;
      im->emit[im->nemit++] = c;
    }
    if(nd->is_dir && assign(im, c, next) != 0) return -1;
  }
  return 0;
}

static void
build_boot(exfat_image_t *im) {
  uint8_t *r = im->boot;
  memset(r, 0, BOOT_BYTES);
  memcpy(r, "\xeb\x76\x90" "EXFAT   ", 11);
  put64(r + 64, 0);                             /* PartitionOffset */
  put64(r + 72, im->volume_sectors);
  put32(r + 80, FAT_OFFSET);
  put32(r + 84, im->fat_sectors);
  put32(r + 88, im->heap_offset);
  put32(r + 92, im->cluster_count);
  put32(r + 96, im->root_cluster);
  put32(r + 100, im->serial);
  put16(r + 104, 0x0100);                       /* revision 1.00 */
  put16(r + 106, 0);                            /* VolumeFlags */
  r[108] = SECTOR_SHIFT;
  r[109] = SPC_SHIFT;
  r[110] = 1;                                   /* NumberOfFats */
  r[111] = 0x80;                                /* DriveSelect */
  r[112] = 0xFF;                                /* PercentInUse: not available */
  put16(r + 510, 0xAA55);
  for(int s = 1; s <= 8; s++)                   /* extended boot sectors */
    put32(r + s * SECTOR + SECTOR - 4, 0xAA550000u);
  uint32_t cs = 0;
  for(uint32_t i = 0; i < 11 * SECTOR; i++) {
    if(i == 106 || i == 107 || i == 112) continue;
    cs = ((cs << 31) | (cs >> 1)) + r[i];
  }
  for(uint32_t i = 0; i < SECTOR / 4; i++) put32(r + 11 * SECTOR + i * 4, cs);
}

exfat_image_t *
exfat_image_plan(const char *source_root, uint32_t serial, char *err, size_t err_len) {
  err[0] = 0;
  exfat_image_t *im = calloc(1, sizeof(*im));
  if(!im) { snprintf(err, err_len, "Kein Speicher."); return NULL; }
  im->fd     = -1;
  im->serial = serial;

  /* Up front, so that running out of memory is an error of the plan and not a
     silently different hash in the middle of the stream. */
  if(build_upmap(im) != 0) goto nomem;
  if(grow((void **)&im->nodes, &im->cap_nodes, 1, sizeof(xnode_t)) != 0) goto nomem;
  memset(&im->nodes[0], 0, sizeof(xnode_t));
  im->nodes[0].is_dir = 1;
  im->nnodes = 1;

  char path[1024];
  if(strlen(source_root) >= sizeof(path)) {     /* a cut-off path could be another folder */
    snprintf(err, err_len, "Ein Pfad ist zu lang.");
    goto fail;
  }
  snprintf(path, sizeof(path), "%s", source_root);
  size_t len = strlen(path);
  while(len > 1 && path[len - 1] == '/') path[--len] = 0;
  if(scan_dir(im, 0, path, len, 0, err, err_len) != 0) goto fail;

  im->upcase_clusters = (uint32_t)ceil_div(EXFAT_UPCASE_SIZE, CLUSTER);
  uint64_t content = node_clusters(im, 0, 1) + sum_clusters(im, 0) + im->upcase_clusters;

  /* The bitmap has to cover itself as well as everything else. */
  uint64_t bitmap = 1, total;
  for(;;) {
    total = bitmap + content;
    uint64_t need = ceil_div(ceil_div(total, 8), CLUSTER);
    if(need == bitmap) break;
    bitmap = need;
  }
  if(total > 0xFFFFFFF5ull) {
    snprintf(err, err_len, "Das Spiel ist zu groß für ein exFAT-Abbild.");
    goto fail;
  }
  im->bitmap_clusters = (uint32_t)bitmap;
  im->cluster_count   = (uint32_t)total;

  uint32_t next = FIRST_CLUSTER + im->bitmap_clusters + im->upcase_clusters;
  im->root_cluster           = next;
  im->nodes[0].first_cluster = next;
  im->nodes[0].cluster_count = (uint32_t)node_clusters(im, 0, 1);
  next += im->nodes[0].cluster_count;
  if(push_run(im, im->bitmap_clusters) != 0 || push_run(im, im->upcase_clusters) != 0 ||
     push_run(im, im->nodes[0].cluster_count) != 0 || assign(im, 0, &next) != 0)
    goto nomem;

  uint64_t fat_entries = (uint64_t)im->cluster_count + 2;
  im->fat_sectors    = (uint32_t)align_up(ceil_div(fat_entries * 4, SECTOR), SPC);
  im->heap_offset    = (uint32_t)align_up(FAT_OFFSET + im->fat_sectors, SPC);
  im->volume_sectors = (uint64_t)im->heap_offset + (uint64_t)im->cluster_count * SPC;
  build_boot(im);

  im->seg     = SEG_BOOT;
  im->seg_pos = 0;
  im->seg_len = BOOT_BYTES;
  return im;

nomem:
  snprintf(err, err_len, "Kein Speicher für das exFAT-Layout.");
fail:
  exfat_image_free(im);
  return NULL;
}

uint64_t exfat_image_size(const exfat_image_t *im)    { return im->volume_sectors * SECTOR; }
unsigned exfat_image_files(const exfat_image_t *im)   { return im->files; }
uint64_t exfat_image_payload(const exfat_image_t *im) { return im->payload; }
const char *exfat_image_where(const exfat_image_t *im) { return im->where; }

void
exfat_image_free(exfat_image_t *im) {
  if(!im) return;
  if(im->fd >= 0) close(im->fd);
  free(im->nodes);
  free(im->paths);
  free(im->names);
  free(im->upmap);
  free(im->runs);
  free(im->emit);
  free(im->dirbuf);
  free(im);
}


/* ---------------------------------------------------------------- streaming */

static size_t
entry_set(exfat_image_t *im, const xnode_t *c, uint8_t *out) {
  unsigned ne  = (unsigned)ceil_div(c->name_len, NAME_PER_ENTRY);
  size_t   len = 32u * (2 + ne);
  memset(out, 0, len);
  uint8_t *fe = out, *se = out + 32;
  const uint16_t *u = im->names + c->name_off;

  fe[0] = 0x85;
  fe[1] = (uint8_t)(1 + ne);
  put16(fe + 4, c->is_dir ? 0x10 : 0x20);
  put32(fe + 8,  FIXED_TIME);
  put32(fe + 12, FIXED_TIME);
  put32(fe + 16, FIXED_TIME);

  uint64_t dlen = c->is_dir ? (uint64_t)c->cluster_count * CLUSTER : c->size;
  int alloc = c->first_cluster >= FIRST_CLUSTER;
  se[0] = 0xC0;
  se[1] = alloc ? 0x01 : 0x00;                 /* AllocationPossible */
  se[3] = c->name_len;
  put16(se + 4, name_hash(im, u, c->name_len));
  put64(se + 8, dlen);
  put32(se + 0x14, alloc ? c->first_cluster : 0);
  put64(se + 0x18, dlen);

  for(unsigned i = 0; i < ne; i++) {
    uint8_t *e = out + 64 + 32 * i;
    e[0] = 0xC1;
    for(unsigned k = 0; k < NAME_PER_ENTRY; k++) {
      unsigned idx = i * NAME_PER_ENTRY + k;
      if(idx >= c->name_len) break;
      put16(e + 2 + 2 * k, u[idx]);
    }
  }
  uint32_t cs = 0;      /* 16 bits wide, unsigned and masked: no signed overflow */
  for(size_t i = 0; i < len; i++) {
    if(i == 2 || i == 3) continue;
    cs = (((cs << 15) | (cs >> 1)) + out[i]) & 0xFFFFu;
  }
  put16(fe + 2, (uint16_t)cs);
  return len;
}

static int
build_dir(exfat_image_t *im, uint32_t idx, int is_root) {
  const xnode_t *nd = &im->nodes[idx];
  size_t len = (size_t)dir_entries(im, idx, is_root) * 32;
  free(im->dirbuf);
  im->dirbuf = calloc(1, len ? len : 1);
  if(!im->dirbuf) return -1;
  uint8_t *o = im->dirbuf;
  if(is_root) {
    o[0] = 0x83;                                /* volume label, empty */
    o += 32;
    o[0] = 0x81;                                /* allocation bitmap */
    put32(o + 0x14, FIRST_CLUSTER);
    put64(o + 0x18, ceil_div(im->cluster_count, 8));
    o += 32;
    o[0] = 0x82;                                /* up-case table */
    put32(o + 0x04, EXFAT_UPCASE_CHECKSUM);
    put32(o + 0x14, FIRST_CLUSTER + im->bitmap_clusters);
    put64(o + 0x18, EXFAT_UPCASE_SIZE);
    o += 32;
  }
  for(uint32_t i = 0; i < nd->nchildren; i++)
    o += entry_set(im, &im->nodes[nd->first_child + i], o);
  im->dirbuf_len = len;
  return 0;
}

/* The FAT entry for the next index, sequentially: every heap cluster is
   allocated, in runs, so an entry points at its successor except at the end
   of a run. */
static uint32_t
fat_next(exfat_image_t *im) {
  uint64_t e = im->fat_entry++;
  if(e == 0) return FAT_MEDIA;
  if(e == 1) return FAT_EOC;
  if(e >= (uint64_t)im->cluster_count + 2) return 0;
  while(im->fat_left == 0 && im->fat_run < im->nruns)
    im->fat_left = im->runs[im->fat_run++];
  if(im->fat_left == 0) return 0;
  im->fat_left--;
  return im->fat_left ? (uint32_t)e + 1 : FAT_EOC;
}

static int
next_segment(exfat_image_t *im) {
  if(im->fd >= 0) { close(im->fd); im->fd = -1; }
  im->seg_pos = 0;
  switch(im->seg) {
    case SEG_BOOT:   im->seg = SEG_BOOT2;  im->seg_len = BOOT_BYTES; break;
    case SEG_BOOT2:  im->seg = SEG_PAD1;   im->seg_len = (uint64_t)(FAT_OFFSET - 24) * SECTOR; break;
    case SEG_PAD1:
      im->seg = SEG_FAT;
      im->seg_len = (uint64_t)im->fat_sectors * SECTOR;
      im->fat_entry = 0;
      im->fat_run = 0;
      im->fat_left = 0;
      break;
    case SEG_FAT:
      im->seg = SEG_PAD2;
      im->seg_len = (uint64_t)(im->heap_offset - (FAT_OFFSET + im->fat_sectors)) * SECTOR;
      break;
    case SEG_PAD2:   im->seg = SEG_BITMAP; im->seg_len = (uint64_t)im->bitmap_clusters * CLUSTER; break;
    case SEG_BITMAP: im->seg = SEG_UPCASE; im->seg_len = (uint64_t)im->upcase_clusters * CLUSTER; break;
    case SEG_UPCASE:
      if(build_dir(im, 0, 1) != 0) { errno = ENOMEM; return -1; }
      im->seg = SEG_ROOT;
      im->seg_len = (uint64_t)im->nodes[0].cluster_count * CLUSTER;
      break;
    case SEG_ROOT:
      im->item = 0;
      /* fall through */
    case SEG_TREE: {
      if(im->seg == SEG_TREE) im->item++;
      im->seg = SEG_TREE;
      if(im->item >= im->nemit) { im->seg = SEG_END; im->seg_len = 0; break; }
      im->item_node = im->emit[im->item];
      const xnode_t *nd = &im->nodes[im->item_node];
      im->seg_len = (uint64_t)nd->cluster_count * CLUSTER;
      if(nd->is_dir) {
        if(build_dir(im, im->item_node, 0) != 0) { errno = ENOMEM; return -1; }
      } else {
        /* The file is opened by its full path. where is only what the error
           message shows and is cut at 255 bytes: opening that copy meant that
           every file with a longer path failed with "no such file", late,
           when the stream got to it. The cut must not split a UTF-8 character. */
        const char *file = im->paths + nd->path_off;
        snprintf(im->where, sizeof(im->where), "%s", file);
        for(size_t w = strlen(im->where); w && file[w] && ((unsigned char)file[w] & 0xC0) == 0x80; )
          im->where[--w] = 0;
        im->fd = open(file, O_RDONLY);
        if(im->fd < 0) return -1;
      }
      break;
    }
    default:
      im->seg = SEG_END;
      im->seg_len = 0;
  }
  return 0;
}

static void
dir_bytes(const exfat_image_t *im, uint8_t *p, size_t n) {
  for(size_t i = 0; i < n; i++) {
    uint64_t b = im->seg_pos + i;
    p[i] = b < im->dirbuf_len ? im->dirbuf[b] : 0;
  }
}

ssize_t
exfat_image_read(exfat_image_t *im, void *out, size_t len) {
  uint8_t *buf = out;
  size_t   got = 0;
  if(im->error) { errno = im->error; return -1; }
  while(got < len && im->seg != SEG_END) {
    if(im->seg_pos >= im->seg_len) {
      if(next_segment(im) != 0) { im->error = errno ? errno : EIO; return -1; }
      continue;
    }
    size_t n = len - got;
    if(n > im->seg_len - im->seg_pos) n = (size_t)(im->seg_len - im->seg_pos);
    uint8_t *p = buf + got;

    switch(im->seg) {
      case SEG_BOOT:
      case SEG_BOOT2:
        memcpy(p, im->boot + im->seg_pos, n);
        break;
      case SEG_FAT:
        for(size_t i = 0; i < n; i++) {
          uint64_t o = im->seg_pos + i;
          if((o & 3) == 0) im->fat_cur = fat_next(im);
          p[i] = (uint8_t)(im->fat_cur >> (8 * (o & 3)));
        }
        break;
      case SEG_BITMAP: {
        uint64_t full = im->cluster_count / 8;
        unsigned rest = im->cluster_count % 8;
        for(size_t i = 0; i < n; i++) {
          uint64_t b = im->seg_pos + i;
          p[i] = b < full ? 0xFF : (b == full && rest) ? (uint8_t)((1u << rest) - 1) : 0;
        }
        break;
      }
      case SEG_UPCASE:
        for(size_t i = 0; i < n; i++) {
          uint64_t b = im->seg_pos + i;
          p[i] = b < EXFAT_UPCASE_SIZE ? k_exfat_upcase[b] : 0;
        }
        break;
      case SEG_ROOT:
        dir_bytes(im, p, n);
        break;
      case SEG_TREE: {
        const xnode_t *nd = &im->nodes[im->item_node];
        if(nd->is_dir) {
          dir_bytes(im, p, n);
        } else if(im->seg_pos < nd->size) {
          size_t want = n;
          if(want > nd->size - im->seg_pos) want = (size_t)(nd->size - im->seg_pos);
          ssize_t r = read(im->fd, p, want);
          if(r < 0) {
            if(errno == EINTR) continue;
            im->error = errno ? errno : EIO;
            return -1;
          }
          if(r == 0) {                          /* the file shrank meanwhile */
            im->error = EIO;
            errno = im->error;
            return -1;
          }
          n = (size_t)r;
        } else {
          memset(p, 0, n);
        }
        break;
      }
      default:                                  /* padding */
        memset(p, 0, n);
    }
    im->seg_pos += n;
    got += n;
  }
  return (ssize_t)got;
}
