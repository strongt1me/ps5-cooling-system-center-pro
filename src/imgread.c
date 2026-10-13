/* A read-only reader for exFAT, PFS/PFSC (.ffpfsc) and UFS2 (.ffpkg) images — see imgread.h.
 *
 * Three layers: a byte stream (the image file itself, or the unpacked view of a PFSC file inside a PFS container), a file
 * system on top of it (exFAT or UFS2) and the small operations the app needs, pread of one file and the names of one folder.
 * Everything is bounds-checked against the sizes the headers claim: an image is data from a drive, possibly half written
 * or damaged, and the answer to anything unexpected is -1, never a wild read.
 *
 * The layouts are those this app writes itself (conv_exfat.c, conv_pfs.c, conv_ufs2.c) — and, since the reader takes the
 * sizes from the headers, those other tools make as well: UFS2 with any block and fragment size, exFAT with any sector and
 * cluster size. */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "imgread.h"
#include "third_party/libdeflate/libdeflate.h"

#define MAX_DIR_BYTES   (8u << 20)
#define MAX_PATH_PARTS  16
#define EXF_NAME_MAX    1024           /* 255 UTF-16 units: at most 4 bytes each (a pair of surrogates) */
#define MAX_WALK_DEPTH  40

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint64_t rd64(const uint8_t *p) { return (uint64_t)rd32(p) | (uint64_t)rd32(p + 4) << 32; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }


/* ------------------------------------------------------------------ byte streams */

typedef struct bs bs_t;
struct bs {
  /* 0 when n bytes were read, -1 otherwise (a read past the end counts as an error) */
  int      (*rd)(bs_t *s, uint64_t off, void *buf, size_t n);
  void     (*cl)(bs_t *s);
  uint64_t size;
};

typedef struct { bs_t b; int fd; } file_bs_t;

static int
file_rd(bs_t *s, uint64_t off, void *buf, size_t n) {
  file_bs_t *f = (file_bs_t *)s;
  if(off > s->size || n > s->size - off) return -1;
  uint8_t *p = buf;
  while(n) {
    ssize_t k = pread(f->fd, p, n, (off_t)off);
    if(k < 0 && errno == EINTR) continue;
    if(k <= 0) return -1;
    p += k; off += (uint64_t)k; n -= (size_t)k;
  }
  return 0;
}

static void
file_cl(bs_t *s) {
  file_bs_t *f = (file_bs_t *)s;
  close(f->fd);
  free(f);
}

static bs_t *
file_open(const char *path, char *err, size_t err_len) {
  int fd = open(path, O_RDONLY);
  struct stat st;
  if(fd < 0 || fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size <= 0) {
    if(fd >= 0) close(fd);
    snprintf(err, err_len, "Das Abbild ist nicht lesbar.");
    return NULL;
  }
  file_bs_t *f = calloc(1, sizeof(*f));
  if(!f) { close(fd); snprintf(err, err_len, "Kein Speicher."); return NULL; }
  f->fd = fd;
  f->b.rd = file_rd;
  f->b.cl = file_cl;
  f->b.size = (uint64_t)st.st_size;
  return &f->b;
}


/* The unpacked view of a PFSC file in a PFS container (the .ffpfsc layout of conv_pfs.c / MkPFS): block 0 the PFS
   header, block 1 the inode table with four 0xA8-byte inodes, the file's inode is number 3 and names its first block;
   there a 0x30-byte PFSC header, a table of block offsets at 0x400, then each 64 KiB logical block stored raw or as zlib. */
#define PFS_BS      0x10000u
#define PFS_INODE   0xA8u

typedef struct {
  bs_t      b;
  bs_t     *under;
  uint64_t  base, blocks, lbs;
  uint64_t  tb_first, tb_n;      /* the window of the offset table that is in tb */
  uint8_t  *tb, *comp, *raw;
  int64_t   cached;              /* the logical block in raw, -1 none */
  struct libdeflate_decompressor *d;
} pfsc_bs_t;

#define PFSC_TB_BATCH 2048u

static int
pfsc_off(pfsc_bs_t *p, uint64_t i, uint64_t *v) {
  if(i < p->tb_first || i >= p->tb_first + p->tb_n) {
    uint64_t entries = p->blocks + 1;
    uint64_t n = entries - i < PFSC_TB_BATCH ? entries - i : PFSC_TB_BATCH;
    if(p->under->rd(p->under, p->base + 0x400 + i * 8, p->tb, (size_t)n * 8) != 0) return -1;
    p->tb_first = i;
    p->tb_n = n;
  }
  *v = rd64(p->tb + (i - p->tb_first) * 8);
  return 0;
}

static int
pfsc_block(pfsc_bs_t *p, uint64_t i) {
  if((int64_t)i == p->cached) return 0;
  uint64_t a, b;
  if(i >= p->blocks || pfsc_off(p, i, &a) != 0 || pfsc_off(p, i + 1, &b) != 0) return -1;
  if(b < a || b - a > p->lbs) return -1;
  size_t len = (size_t)(b - a);
  if(len == 0) { memset(p->raw, 0, (size_t)p->lbs); p->cached = (int64_t)i; return 0; }
  if(p->under->rd(p->under, p->base + a, p->comp, len) != 0) return -1;
  if(len == p->lbs) {
    memcpy(p->raw, p->comp, len);
  } else {
    size_t got = 0;
    if(libdeflate_zlib_decompress(p->d, p->comp, len, p->raw, (size_t)p->lbs, &got) != LIBDEFLATE_SUCCESS) return -1;
    if(got < p->lbs) memset(p->raw + got, 0, (size_t)p->lbs - got);
  }
  p->cached = (int64_t)i;
  return 0;
}

static int
pfsc_rd(bs_t *s, uint64_t off, void *buf, size_t n) {
  pfsc_bs_t *p = (pfsc_bs_t *)s;
  if(off > s->size || n > s->size - off) return -1;
  uint8_t *out = buf;
  while(n) {
    uint64_t i = off / p->lbs, in = off % p->lbs;
    size_t take = (size_t)(p->lbs - in);
    if(take > n) take = n;
    if(pfsc_block(p, i) != 0) return -1;
    memcpy(out, p->raw + in, take);
    out += take; off += take; n -= take;
  }
  return 0;
}

static void
pfsc_cl(bs_t *s) {
  pfsc_bs_t *p = (pfsc_bs_t *)s;
  if(p->under) p->under->cl(p->under);
  if(p->d) libdeflate_free_decompressor(p->d);
  free(p->tb); free(p->comp); free(p->raw);
  free(p);
}

/* Takes over under. NULL (under is closed) when it is not a PFS container of this layout. */
static bs_t *
pfsc_open(bs_t *under) {
  uint8_t h[PFS_BS];
  pfsc_bs_t *p = NULL;
  if(under->rd(under, 0, h, 0x40) != 0 || rd64(h) != 2 || rd64(h + 8) != 20130315 || rd32(h + 0x20) != PFS_BS) goto bad;
  if(under->rd(under, PFS_BS, h, 4 * PFS_INODE) != 0) goto bad;
  const uint8_t *fi = h + 3 * PFS_INODE;
  uint64_t first_block = rd32(fi + 0x64);
  if(!(rd32(fi + 0x04) & 0x1) || first_block == 0 || first_block > (under->size / PFS_BS)) goto bad;     /* compressed flag */
  uint64_t base = first_block * PFS_BS;
  uint8_t ph[0x30];
  if(under->rd(under, base, ph, sizeof(ph)) != 0 || rd32(ph) != 0x43534650 || rd32(ph + 8) != 6) goto bad;
  uint64_t lbs = rd32(ph + 0x0C), raw_size = rd64(ph + 0x28), tbl_off = rd64(ph + 0x18);
  if(lbs < 4096 || lbs > (1u << 24) || tbl_off != 0x400 || raw_size == 0) goto bad;
  uint64_t blocks = (raw_size + lbs - 1) / lbs;
  if(blocks > (1ull << 32) || base + 0x400 + (blocks + 1) * 8 > under->size) goto bad;
  p = calloc(1, sizeof(*p));
  if(!p) goto bad;
  p->under = under;
  p->base = base; p->blocks = blocks; p->lbs = lbs;
  p->tb = malloc((size_t)PFSC_TB_BATCH * 8);
  p->comp = malloc((size_t)lbs);
  p->raw = malloc((size_t)lbs);
  p->d = libdeflate_alloc_decompressor();
  p->cached = -1;
  if(!p->tb || !p->comp || !p->raw || !p->d) goto bad;
  p->b.rd = pfsc_rd;
  p->b.cl = pfsc_cl;
  p->b.size = raw_size;
  return &p->b;
bad:
  if(p) { p->under = NULL; pfsc_cl(&p->b); }
  under->cl(under);
  return NULL;
}


/* ------------------------------------------------------------------ file systems */

typedef struct fsx fsx_t;
struct fsx {
  const char *kind;
  long (*pread)(fsx_t *f, const char *path, uint64_t off, void *buf, size_t n);
  int  (*list)(fsx_t *f, const char *path, int (*cb)(const char *, int, void *), void *ctx);
  void (*cl)(fsx_t *f);
  bs_t *b;
  /* the whole-tree side (imgr_walk and friends) */
  int  (*root)(fsx_t *f, imgr_entry_t *e);                               /* the root folder as an entry */
  int  (*children)(fsx_t *f, const imgr_entry_t *dir, int (*cb)(const char *name, const imgr_entry_t *e, void *ctx), void *ctx);
  long (*read_entry)(fsx_t *f, const imgr_entry_t *e, uint64_t off, void *buf, size_t n);
  int  (*chain_ok)(fsx_t *f, const imgr_entry_t *e, uint64_t *bad_off);   /* the data is where the file system says */
};

struct imgr {
  fsx_t *fs;
  const char *kind_name;           /* "exfat", "ffpfsc" or "ffpkg" */
};

static int
split_path(const char *path, char parts[][256], int max) {
  int n = 0;
  const char *p = path;
  while(*p) {
    while(*p == '/') p++;
    if(!*p) break;
    size_t len = strcspn(p, "/");
    if(len == 0 || len >= 256 || n >= max) return -1;
    memcpy(parts[n], p, len);
    parts[n][len] = 0;
    if(!strcmp(parts[n], "..")) return -1;
    n++;
    p += len;
  }
  return n;
}


/* ---- exFAT */

typedef struct {
  fsx_t    f;
  uint32_t sec_shift, clu_shift;
  uint64_t fat_off, heap_off;
  uint32_t clusters, root;
  uint8_t  fat_page[4096];
  uint64_t fat_page_base;
  int      fat_page_ok;
} exfat_t;

typedef struct { uint32_t first; uint64_t size; int nofat; int is_dir; } exf_node_t;

static int
exf_fat_next(exfat_t *x, uint32_t c, uint32_t *next) {
  uint64_t off = x->fat_off + (uint64_t)c * 4;
  uint64_t page = off & ~4095ull;
  if(!x->fat_page_ok || x->fat_page_base != page) {
    uint64_t avail = x->f.b->size - page;
    size_t n = avail < sizeof(x->fat_page) ? (size_t)avail : sizeof(x->fat_page);
    if(x->f.b->rd(x->f.b, page, x->fat_page, n) != 0) return -1;
    if(n < sizeof(x->fat_page)) memset(x->fat_page + n, 0, sizeof(x->fat_page) - n);
    x->fat_page_base = page;
    x->fat_page_ok = 1;
  }
  *next = rd32(x->fat_page + (off - page));
  return 0;
}

static int
exf_valid_cluster(const exfat_t *x, uint32_t c) {
  return c >= 2 && c < x->clusters + 2;
}

/* Reads [off, off+n) of a cluster chain (a file or a folder); size bounds it when known (nofat: contiguous). */
static int
exf_read(exfat_t *x, uint32_t first, int nofat, uint64_t off, void *buf, size_t n) {
  uint8_t *out = buf;
  uint64_t csize = 1ull << x->clu_shift;
  uint64_t idx64 = off >> x->clu_shift;
  /* a file cannot reach past the cluster count: a corrupt size (or a FAT chain in a circle) must not make the
     walk below run for billions of steps */
  if(idx64 >= x->clusters) return -1;
  uint32_t idx = (uint32_t)idx64, c = first;
  if(!exf_valid_cluster(x, first)) return -1;
  if(nofat) {
    c = first + idx;
  } else {
    for(uint32_t i = 0; i < idx; i++) {
      uint32_t nx;
      if(exf_fat_next(x, c, &nx) != 0 || !exf_valid_cluster(x, nx)) return -1;
      c = nx;
    }
  }
  uint64_t in = off & (csize - 1);
  while(n) {
    if(!exf_valid_cluster(x, c)) return -1;
    size_t take = (size_t)(csize - in);
    if(take > n) take = n;
    uint64_t pos = x->heap_off + ((uint64_t)(c - 2) << x->clu_shift) + in;
    if(x->f.b->rd(x->f.b, pos, out, take) != 0) return -1;
    out += take; n -= take; in = 0;
    if(n) {
      if(nofat) c++;
      else {
        uint32_t nx;
        if(exf_fat_next(x, c, &nx) != 0) return -1;
        c = nx;
      }
    }
  }
  return 0;
}

/* The whole folder as bytes; the root has no stream entry, so its chain is followed to the end. */
static uint8_t *
exf_load_dir(exfat_t *x, const exf_node_t *d, size_t *len) {
  uint64_t csize = 1ull << x->clu_shift;
  uint64_t size = d->size;
  uint8_t *buf;
  if(d->nofat || size) {
    if(size == 0 || size > MAX_DIR_BYTES) return NULL;
    buf = malloc((size_t)size);
    if(!buf || exf_read(x, d->first, d->nofat, 0, buf, (size_t)size) != 0) { free(buf); return NULL; }
    *len = (size_t)size;
    return buf;
  }
  uint32_t cnt = 0, c = d->first;
  while(exf_valid_cluster(x, c) && cnt * csize < MAX_DIR_BYTES) {
    uint32_t nx;
    cnt++;
    if(exf_fat_next(x, c, &nx) != 0) return NULL;
    c = nx;
  }
  if(cnt == 0) return NULL;
  buf = malloc((size_t)(cnt * csize));
  if(!buf || exf_read(x, d->first, 0, 0, buf, (size_t)(cnt * csize)) != 0) { free(buf); return NULL; }
  *len = (size_t)(cnt * csize);
  return buf;
}

/* One entry set at a time: calls cb(name, node) for each file/folder; non-zero stops. */
static int
exf_each(exfat_t *x, const exf_node_t *d, int (*cb)(const char *, const exf_node_t *, void *), void *ctx) {
  size_t len;
  uint8_t *buf = exf_load_dir(x, d, &len);
  if(!buf) return -1;
  for(size_t i = 0; i + 32 <= len; i += 32) {
    if(buf[i] == 0x00) break;
    if(buf[i] != 0x85) continue;
    unsigned secondary = buf[i + 1];
    if(secondary < 2 || i + 32 * (size_t)(secondary + 1) > len || buf[i + 32] != 0xC0) continue;
    const uint8_t *st = buf + i + 32;
    unsigned nlen = st[3];
    exf_node_t n;
    n.nofat  = (st[1] & 0x02) != 0;
    n.first  = rd32(st + 20);
    n.size   = rd64(st + 24);
    n.is_dir = (rd16(buf + i + 4) & 0x10) != 0;
    /* the name is UTF-16 in up to 255 units, handed on as UTF-8 (a pair of surrogates is one character; a lone one
       becomes '?') */
    char name[EXF_NAME_MAX];
    unsigned k = 0, o = 0;
    uint32_t hi = 0;
    for(unsigned s = 2; s <= secondary && k < nlen; s++) {
      const uint8_t *ne = buf + i + 32 * (size_t)s;
      if(ne[0] != 0xC1) break;
      for(unsigned q = 0; q < 15 && k < nlen; q++, k++) {
        uint32_t ch = rd16(ne + 2 + q * 2);
        if(hi) {
          if(ch >= 0xDC00 && ch <= 0xDFFF) ch = 0x10000 + ((hi - 0xD800) << 10) + (ch - 0xDC00);
          else name[o++] = '?';
          hi = 0;
        }
        if(ch >= 0xD800 && ch <= 0xDBFF) { hi = ch; continue; }
        if(ch >= 0xDC00 && ch <= 0xDFFF) ch = '?';
        if(ch < 0x80) name[o++] = (char)ch;
        else if(ch < 0x800) { name[o++] = (char)(0xC0 | ch >> 6); name[o++] = (char)(0x80 | (ch & 0x3F)); }
        else if(ch < 0x10000) { name[o++] = (char)(0xE0 | ch >> 12); name[o++] = (char)(0x80 | (ch >> 6 & 0x3F)); name[o++] = (char)(0x80 | (ch & 0x3F)); }
        else { name[o++] = (char)(0xF0 | ch >> 18); name[o++] = (char)(0x80 | (ch >> 12 & 0x3F)); name[o++] = (char)(0x80 | (ch >> 6 & 0x3F)); name[o++] = (char)(0x80 | (ch & 0x3F)); }
      }
    }
    if(hi) name[o++] = '?';
    name[o] = 0;
    int stop = cb(name, &n, ctx);
    if(stop) { free(buf); return 0; }
    i += 32 * (size_t)secondary;
  }
  free(buf);
  return 0;
}

typedef struct { const char *want; exf_node_t found; int hit; } exf_find_t;

static int
exf_find_cb(const char *name, const exf_node_t *n, void *ctx) {
  exf_find_t *f = ctx;
  if(strcasecmp(name, f->want) != 0) return 0;
  f->found = *n;
  f->hit = 1;
  return 1;
}

static int
exf_lookup(exfat_t *x, const char *path, exf_node_t *out) {
  char parts[MAX_PATH_PARTS][256];
  int n = split_path(path, parts, MAX_PATH_PARTS);
  if(n < 0) return -1;
  exf_node_t cur = { x->root, 0, 0, 1 };
  for(int i = 0; i < n; i++) {
    if(!cur.is_dir) return -1;
    exf_find_t f = { parts[i], {0}, 0 };
    if(exf_each(x, &cur, exf_find_cb, &f) != 0 || !f.hit) return -1;
    cur = f.found;
  }
  *out = cur;
  return 0;
}

static long
exf_pread(fsx_t *f, const char *path, uint64_t off, void *buf, size_t n) {
  exfat_t *x = (exfat_t *)f;
  exf_node_t nd;
  if(exf_lookup(x, path, &nd) != 0 || nd.is_dir) return -1;
  if(off >= nd.size) return 0;
  if(n > nd.size - off) n = (size_t)(nd.size - off);
  if(n == 0) return 0;
  return exf_read(x, nd.first, nd.nofat, off, buf, n) == 0 ? (long)n : -1;
}

typedef struct { int (*cb)(const char *, int, void *); void *ctx; } exf_list_t;

static int
exf_list_cb(const char *name, const exf_node_t *n, void *ctx) {
  exf_list_t *l = ctx;
  return l->cb ? l->cb(name, n->is_dir, l->ctx) : 0;                /* no callback: the call only asks whether the folder is there */
}

static int
exf_list(fsx_t *f, const char *path, int (*cb)(const char *, int, void *), void *ctx) {
  exfat_t *x = (exfat_t *)f;
  exf_node_t nd;
  if(exf_lookup(x, path, &nd) != 0 || !nd.is_dir) return -1;
  exf_list_t l = { cb, ctx };
  return exf_each(x, &nd, exf_list_cb, &l);
}

static void
exf_cl(fsx_t *f) {
  f->b->cl(f->b);
  free(f);
}

/* ---- the whole-tree side, exFAT */

static void
exf_entry_of(const exf_node_t *n, imgr_entry_t *e) {
  e->is_dir = n->is_dir;
  e->size = n->is_dir ? 0 : n->size;
  e->h1 = n->first;
  e->h2 = (n->nofat ? 1u : 0u) | (n->is_dir ? 2u : 0u);
}

static exf_node_t
exf_node_of(const imgr_entry_t *e) {
  exf_node_t n;
  n.first = (uint32_t)e->h1;
  n.nofat = (e->h2 & 1) != 0;
  n.is_dir = (e->h2 & 2) != 0;
  n.size = e->size;
  return n;
}

static int
exf_root(fsx_t *f, imgr_entry_t *e) {
  exfat_t *x = (exfat_t *)f;
  exf_node_t n = { x->root, 0, 0, 1 };
  memset(e, 0, sizeof(*e));
  exf_entry_of(&n, e);
  return 0;
}

typedef struct { int (*cb)(const char *, const imgr_entry_t *, void *); void *ctx; } exf_kids_t;

static int
exf_kids_cb(const char *name, const exf_node_t *n, void *ctx) {
  exf_kids_t *k = ctx;
  imgr_entry_t e;
  memset(&e, 0, sizeof(e));
  exf_entry_of(n, &e);
  return k->cb(name, &e, k->ctx);
}

static int
exf_children(fsx_t *f, const imgr_entry_t *dir, int (*cb)(const char *, const imgr_entry_t *, void *), void *ctx) {
  exf_node_t d = exf_node_of(dir);
  exf_kids_t k = { cb, ctx };
  return exf_each((exfat_t *)f, &d, exf_kids_cb, &k);
}

static long
exf_read_entry(fsx_t *f, const imgr_entry_t *e, uint64_t off, void *buf, size_t n) {
  if(e->is_dir) return -1;
  if(off >= e->size) return 0;
  if(n > e->size - off) n = (size_t)(e->size - off);
  if(n == 0) return 0;
  exf_node_t nd = exf_node_of(e);
  return exf_read((exfat_t *)f, nd.first, nd.nofat, off, buf, n) == 0 ? (long)n : -1;
}

/* The chain of a file has as many clusters as its size needs, all inside the heap, none twice in a row of the FAT
   walk (a circle would otherwise run for ever: the walk is bounded by the cluster count). */
static int
exf_chain_ok(fsx_t *f, const imgr_entry_t *e, uint64_t *bad_off) {
  exfat_t *x = (exfat_t *)f;
  uint64_t csize = 1ull << x->clu_shift;
  if(e->size == 0) return 0;
  uint64_t need = (e->size + csize - 1) >> x->clu_shift;
  *bad_off = 0;
  if(need > x->clusters) return -1;
  uint32_t c = (uint32_t)e->h1;
  if(!exf_valid_cluster(x, c)) return -1;
  if(e->h2 & 1) {                                          /* contiguous: no FAT */
    if((uint64_t)c + need > (uint64_t)x->clusters + 2) { *bad_off = ((uint64_t)x->clusters + 2 - c) * csize; return -1; }
    return 0;
  }
  for(uint64_t i = 0; i < need; i++) {
    if(!exf_valid_cluster(x, c)) { *bad_off = i * csize; return -1; }
    if(i + 1 < need) {
      uint32_t nx;
      if(exf_fat_next(x, c, &nx) != 0 || !exf_valid_cluster(x, nx)) { *bad_off = (i + 1) * csize; return -1; }
      c = nx;
    }
  }
  return 0;
}

/* Takes over b. NULL (b is closed) when it does not hold exFAT. */
static fsx_t *
exf_open(bs_t *b) {
  uint8_t h[512];
  if(b->rd(b, 0, h, sizeof(h)) != 0 || memcmp(h + 3, "EXFAT   ", 8) != 0) return NULL;
  exfat_t *x = calloc(1, sizeof(*x));
  if(!x) return NULL;
  x->sec_shift = h[108];
  x->clu_shift = h[109] + h[108];
  x->fat_off   = (uint64_t)rd32(h + 80) << x->sec_shift;
  x->heap_off  = (uint64_t)rd32(h + 88) << x->sec_shift;
  x->clusters  = rd32(h + 92);
  x->root      = rd32(h + 96);
  if(h[108] < 9 || h[108] > 12 || x->clu_shift > 25 || x->clusters == 0 || x->heap_off >= b->size ||
     x->fat_off >= b->size || x->root < 2 || x->root >= x->clusters + 2) { free(x); return NULL; }
  x->f.kind = "exfat";
  x->f.pread = exf_pread;
  x->f.list = exf_list;
  x->f.cl = exf_cl;
  x->f.root = exf_root;
  x->f.children = exf_children;
  x->f.read_entry = exf_read_entry;
  x->f.chain_ok = exf_chain_ok;
  x->f.b = b;
  return &x->f;
}


/* ---- UFS2 (read from the superblock, any block and fragment size) */

#define UFS2_MAGIC      0x19540119u
#define UFS_SB_OFF      65536ull
#define UFS_INODE_SIZE  256u

typedef struct {
  fsx_t    f;
  uint32_t bsize, fsize, ipg, iblkno;
  uint64_t fpg;
} ufs_t;

typedef struct { uint16_t mode; uint64_t size; int64_t direct[12], indirect[3]; } ufs_ino_t;

static int
ufs_inode(ufs_t *u, uint32_t ino, ufs_ino_t *out) {
  if(ino < 2) return -1;
  uint32_t cg = ino / u->ipg, idx = ino % u->ipg;
  uint64_t table = ((uint64_t)cg * u->fpg + u->iblkno) * u->fsize;
  uint64_t pos = table + (uint64_t)idx * UFS_INODE_SIZE;
  uint8_t b[UFS_INODE_SIZE];
  if(u->f.b->rd(u->f.b, pos, b, sizeof(b)) != 0) return -1;
  out->mode = rd16(b);
  out->size = rd64(b + 0x10);
  for(int i = 0; i < 12; i++) out->direct[i] = (int64_t)rd64(b + 0x70 + i * 8);
  for(int i = 0; i < 3; i++) out->indirect[i] = (int64_t)rd64(b + 0xD0 + i * 8);
  return 0;
}

/* The fragment address of logical block lbn; 0 is a hole, -1 an error (triple indirection is not read). */
static int64_t
ufs_bmap(ufs_t *u, const ufs_ino_t *in, uint64_t lbn) {
  uint64_t per = u->bsize / 8;
  if(lbn < 12) return in->direct[lbn];
  lbn -= 12;
  int64_t blk;
  uint64_t idx[2];
  int levels;
  if(lbn < per) { blk = in->indirect[0]; idx[0] = lbn; levels = 1; }
  else if(lbn - per < per * per) { lbn -= per; blk = in->indirect[1]; idx[0] = lbn / per; idx[1] = lbn % per; levels = 2; }
  else return -1;
  for(int l = 0; l < levels; l++) {
    if(blk == 0) return 0;
    uint8_t p[8];
    uint64_t pos = (uint64_t)blk * u->fsize + idx[l] * 8;
    if(u->f.b->rd(u->f.b, pos, p, 8) != 0) return -1;
    blk = (int64_t)rd64(p);
  }
  return blk;
}

static int
ufs_read(ufs_t *u, const ufs_ino_t *in, uint64_t off, void *buf, size_t n) {
  uint8_t *out = buf;
  while(n) {
    uint64_t lbn = off / u->bsize, within = off % u->bsize;
    size_t take = (size_t)(u->bsize - within);
    if(take > n) take = n;
    int64_t fr = ufs_bmap(u, in, lbn);
    if(fr < 0) return -1;
    if(fr == 0) memset(out, 0, take);
    else if(u->f.b->rd(u->f.b, (uint64_t)fr * u->fsize + within, out, take) != 0) return -1;
    out += take; off += take; n -= take;
  }
  return 0;
}

static int
ufs_each(ufs_t *u, const ufs_ino_t *dir, int (*cb)(const char *, uint32_t, int, void *), void *ctx) {
  if((dir->mode & 0xF000) != 0x4000 || dir->size == 0 || dir->size > MAX_DIR_BYTES) return -1;
  uint8_t *buf = malloc((size_t)dir->size);
  if(!buf || ufs_read(u, dir, 0, buf, (size_t)dir->size) != 0) { free(buf); return -1; }
  size_t pos = 0, len = (size_t)dir->size;
  while(pos + 8 <= len) {
    uint32_t ino = rd32(buf + pos);
    unsigned reclen = rd16(buf + pos + 4), type = buf[pos + 6], nlen = buf[pos + 7];
    if(reclen < 8 || reclen % 4 || pos + reclen > len) break;
    if(ino && nlen && 8u + nlen <= reclen) {
      char name[256];
      memcpy(name, buf + pos + 8, nlen);
      name[nlen] = 0;
      if(strcmp(name, ".") && strcmp(name, "..") && cb(name, ino, type == 4, ctx)) { free(buf); return 0; }
    }
    pos += reclen;
  }
  free(buf);
  return 0;
}

typedef struct { const char *want; uint32_t ino; int is_dir, hit; } ufs_find_t;

static int
ufs_find_cb(const char *name, uint32_t ino, int is_dir, void *ctx) {
  ufs_find_t *f = ctx;
  if(strcasecmp(name, f->want) != 0) return 0;
  f->ino = ino; f->is_dir = is_dir; f->hit = 1;
  return 1;
}

static int
ufs_lookup(ufs_t *u, const char *path, ufs_ino_t *out) {
  char parts[MAX_PATH_PARTS][256];
  int n = split_path(path, parts, MAX_PATH_PARTS);
  if(n < 0) return -1;
  ufs_ino_t cur;
  if(ufs_inode(u, 2, &cur) != 0) return -1;
  for(int i = 0; i < n; i++) {
    ufs_find_t f = { parts[i], 0, 0, 0 };
    if(ufs_each(u, &cur, ufs_find_cb, &f) != 0 || !f.hit || ufs_inode(u, f.ino, &cur) != 0) return -1;
  }
  *out = cur;
  return 0;
}

static long
ufs_pread(fsx_t *f, const char *path, uint64_t off, void *buf, size_t n) {
  ufs_t *u = (ufs_t *)f;
  ufs_ino_t in;
  if(ufs_lookup(u, path, &in) != 0 || (in.mode & 0xF000) != 0x8000) return -1;
  if(off >= in.size) return 0;
  if(n > in.size - off) n = (size_t)(in.size - off);
  if(n == 0) return 0;
  return ufs_read(u, &in, off, buf, n) == 0 ? (long)n : -1;
}

typedef struct { int (*cb)(const char *, int, void *); void *ctx; } ufs_list_t;

static int
ufs_list_cb(const char *name, uint32_t ino, int is_dir, void *ctx) {
  (void)ino;
  ufs_list_t *l = ctx;
  return l->cb ? l->cb(name, is_dir, l->ctx) : 0;
}

static int
ufs_list(fsx_t *f, const char *path, int (*cb)(const char *, int, void *), void *ctx) {
  ufs_t *u = (ufs_t *)f;
  ufs_ino_t in;
  if(ufs_lookup(u, path, &in) != 0) return -1;
  ufs_list_t l = { cb, ctx };
  return ufs_each(u, &in, ufs_list_cb, &l);
}

static void
ufs_cl(fsx_t *f) {
  f->b->cl(f->b);
  free(f);
}

/* ---- the whole-tree side, UFS2: h1 is the inode number */

static int
ufs_root(fsx_t *f, imgr_entry_t *e) {
  (void)f;
  memset(e, 0, sizeof(*e));
  e->is_dir = 1;
  e->h1 = 2;
  return 0;
}

typedef struct { ufs_t *u; int (*cb)(const char *, const imgr_entry_t *, void *); void *ctx; } ufs_kids_t;

static int
ufs_kids_cb(const char *name, uint32_t ino, int is_dir, void *ctx) {
  ufs_kids_t *k = ctx;
  imgr_entry_t e;
  memset(&e, 0, sizeof(e));
  e.h1 = ino;
  e.is_dir = is_dir;
  if(!is_dir) {
    ufs_ino_t in;
    if(ufs_inode(k->u, ino, &in) != 0 || (in.mode & 0xF000) != 0x8000) return 0;       /* a link or a device: not a file of the game */
    e.size = in.size;
  }
  return k->cb(name, &e, k->ctx);
}

static int
ufs_children(fsx_t *f, const imgr_entry_t *dir, int (*cb)(const char *, const imgr_entry_t *, void *), void *ctx) {
  ufs_t *u = (ufs_t *)f;
  ufs_ino_t in;
  if(ufs_inode(u, (uint32_t)dir->h1, &in) != 0) return -1;
  ufs_kids_t k = { u, cb, ctx };
  return ufs_each(u, &in, ufs_kids_cb, &k);
}

static long
ufs_read_entry(fsx_t *f, const imgr_entry_t *e, uint64_t off, void *buf, size_t n) {
  ufs_t *u = (ufs_t *)f;
  ufs_ino_t in;
  if(e->is_dir || ufs_inode(u, (uint32_t)e->h1, &in) != 0 || (in.mode & 0xF000) != 0x8000) return -1;
  if(off >= in.size) return 0;
  if(n > in.size - off) n = (size_t)(in.size - off);
  if(n == 0) return 0;
  return ufs_read(u, &in, off, buf, n) == 0 ? (long)n : -1;
}

/* every block of the file maps to somewhere inside the image */
static int
ufs_chain_ok(fsx_t *f, const imgr_entry_t *e, uint64_t *bad_off) {
  ufs_t *u = (ufs_t *)f;
  ufs_ino_t in;
  *bad_off = 0;
  if(ufs_inode(u, (uint32_t)e->h1, &in) != 0) return -1;
  uint64_t blocks = (in.size + u->bsize - 1) / u->bsize;
  for(uint64_t l = 0; l < blocks; l++) {
    int64_t fr = ufs_bmap(u, &in, l);
    if(fr < 0 || (fr > 0 && ((uint64_t)fr * u->fsize + u->fsize > u->f.b->size))) { *bad_off = l * u->bsize; return -1; }
  }
  return 0;
}

static fsx_t *
ufs_open(bs_t *b) {
  uint8_t sb[0x560];
  if(b->size < UFS_SB_OFF + sizeof(sb) || b->rd(b, UFS_SB_OFF, sb, sizeof(sb)) != 0 || rd32(sb + 0x55C) != UFS2_MAGIC) return NULL;
  uint32_t bsize = rd32(sb + 0x30), fsize = rd32(sb + 0x34), ipg = rd32(sb + 0xB8), fpg = rd32(sb + 0xBC), iblkno = rd32(sb + 0x10);
  if(bsize < 4096 || bsize > (1u << 20) || fsize < 512 || fsize > bsize || bsize % fsize || ipg == 0 || ipg > (1u << 22) ||
     fpg == 0 || iblkno == 0) return NULL;
  ufs_t *u = calloc(1, sizeof(*u));
  if(!u) return NULL;
  u->bsize = bsize; u->fsize = fsize; u->ipg = ipg; u->fpg = fpg; u->iblkno = iblkno;
  u->f.kind = "ffpkg";
  u->f.pread = ufs_pread;
  u->f.list = ufs_list;
  u->f.cl = ufs_cl;
  u->f.root = ufs_root;
  u->f.children = ufs_children;
  u->f.read_entry = ufs_read_entry;
  u->f.chain_ok = ufs_chain_ok;
  u->f.b = b;
  return &u->f;
}


/* ------------------------------------------------------------------ the public side */

imgr_t *
imgr_open(const char *image_path, char *err, size_t err_len) {
  char dummy[8];
  if(!err) { err = dummy; err_len = sizeof(dummy); }
  err[0] = 0;
  bs_t *b = file_open(image_path, err, err_len);
  if(!b) return NULL;
  imgr_t *r = calloc(1, sizeof(*r));
  if(!r) { b->cl(b); snprintf(err, err_len, "Kein Speicher."); return NULL; }
  uint8_t h[0x40];
  fsx_t *fs = NULL;
  const char *name = NULL;
  if(b->rd(b, 0, h, sizeof(h)) == 0 && rd64(h) == 2 && rd64(h + 8) == 20130315) {
    bs_t *inner = pfsc_open(b);                       /* takes over b */
    b = NULL;
    if(inner) {
      fs = exf_open(inner);
      if(!fs) fs = ufs_open(inner);
      if(!fs) inner->cl(inner);
      name = "ffpfsc";
    }
  } else {
    fs = exf_open(b);
    if(fs) name = "exfat";
    else { fs = ufs_open(b); if(fs) name = "ffpkg"; }
    if(!fs) b->cl(b);
    b = NULL;
  }
  if(!fs) {
    snprintf(err, err_len, "Das Abbild hat kein bekanntes Format.");
    free(r);
    return NULL;
  }
  r->fs = fs;
  r->kind_name = name;
  return r;
}

void
imgr_close(imgr_t *r) {
  if(!r) return;
  r->fs->cl(r->fs);
  free(r);
}

const char *
imgr_kind(const imgr_t *r) {
  return r ? r->kind_name : "";
}

long
imgr_pread(imgr_t *r, const char *path, uint64_t off, void *buf, size_t n) {
  return r ? r->fs->pread(r->fs, path, off, buf, n) : -1;
}

int
imgr_list(imgr_t *r, const char *path, int (*cb)(const char *, int, void *), void *ctx) {
  return r ? r->fs->list(r->fs, path, cb, ctx) : -1;
}

/* ---- the whole tree */

typedef struct {
  imgr_t *r;
  int (*cb)(const imgr_entry_t *, void *);
  void *ctx;
  char *path;                /* the path being built, IMGR_PATH_MAX bytes on the heap */
  size_t len;
  int depth, stopped, too_deep;
} walk_t;

static int walk_dir(walk_t *w, const imgr_entry_t *dir);

static int
walk_kid(const char *name, const imgr_entry_t *kid, void *ctx) {
  walk_t *w = ctx;
  size_t nl = strlen(name), keep = w->len;
  /* a name that is empty, a dot, has a slash in it or does not fit is skipped: it could never be a path of the game */
  if(nl == 0 || strchr(name, '/') || !strcmp(name, ".") || !strcmp(name, "..") || keep + 1 + nl >= IMGR_PATH_MAX) return 0;
  imgr_entry_t *e = malloc(sizeof(*e));
  if(!e) { w->stopped = 1; return 1; }
  *e = *kid;
  w->path[keep] = '/';
  memcpy(w->path + keep + 1, name, nl + 1);
  w->len = keep + 1 + nl;
  memcpy(e->path, w->path, w->len + 1);
  int stop = w->cb(e, w->ctx);
  if(!stop && kid->is_dir) {
    if(w->depth >= MAX_WALK_DEPTH) { w->too_deep = 1; stop = 1; }
    else {
      w->depth++;
      imgr_entry_t d = *kid;
      int rc = walk_dir(w, &d);
      w->depth--;
      if(rc != 0 && !w->stopped) {
        /* the folder could not be read: said once more, with damaged set, then on with the next */
        e->damaged = 1;
        stop = w->cb(e, w->ctx);
      }
      if(w->stopped) stop = 1;
    }
  }
  free(e);
  w->path[keep] = 0;
  w->len = keep;
  if(stop) w->stopped = 1;
  return stop;
}

static int
walk_dir(walk_t *w, const imgr_entry_t *dir) {
  return w->r->fs->children(w->r->fs, dir, walk_kid, w);
}

int
imgr_walk(imgr_t *r, int (*cb)(const imgr_entry_t *, void *), void *ctx) {
  if(!r || !r->fs->root || !r->fs->children) return -1;
  walk_t w;
  memset(&w, 0, sizeof(w));
  w.r = r; w.cb = cb; w.ctx = ctx;
  w.path = calloc(1, IMGR_PATH_MAX);
  if(!w.path) return -1;
  imgr_entry_t *root = malloc(sizeof(*root));
  if(!root) { free(w.path); return -1; }
  int rc = -1;
  if(r->fs->root(r->fs, root) == 0) {
    rc = walk_dir(&w, root);
    if(rc == 0) rc = w.too_deep ? -1 : w.stopped ? 1 : 0;
    else rc = -1;
  }
  free(root);
  free(w.path);
  return rc;
}

long
imgr_read_entry(imgr_t *r, const imgr_entry_t *e, uint64_t off, void *buf, size_t n) {
  return r && e ? r->fs->read_entry(r->fs, e, off, buf, n) : -1;
}

#define VERIFY_CHUNK (1u << 20)

int
imgr_verify_entry(imgr_t *r, const imgr_entry_t *e, int deep, uint64_t *bad_off, const int *cancel,
                  void (*progress)(void *, uint64_t), void *ctx) {
  uint64_t dummy;
  if(!bad_off) bad_off = &dummy;
  *bad_off = 0;
  if(!r || !e || e->is_dir) return -1;
  if(r->fs->chain_ok(r->fs, e, bad_off) != 0) return -1;
  if(!deep || e->size == 0) return 0;
  uint8_t *buf = malloc(VERIFY_CHUNK);
  if(!buf) { errno = ENOMEM; return -1; }
  uint64_t off = 0;
  int rc = 0;
  while(off < e->size) {
    if(cancel && __atomic_load_n(cancel, __ATOMIC_ACQUIRE)) { errno = ECANCELED; rc = -1; *bad_off = off; break; }
    size_t n = e->size - off < VERIFY_CHUNK ? (size_t)(e->size - off) : VERIFY_CHUNK;
    long got = r->fs->read_entry(r->fs, e, off, buf, n);
    if(got != (long)n) { *bad_off = off; errno = EIO; rc = -1; break; }
    off += n;
    if(progress) progress(ctx, off);
  }
  free(buf);
  return rc;
}
