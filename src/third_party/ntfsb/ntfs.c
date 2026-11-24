#include "ntfs.h"

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "endian_util.h"
#include "ntfs_priv.h"
#include "utf.h"

const uint16_t I30[4] = {'$', 'I', '3', '0'};

/* ---- Small helpers ------------------------------------------------------- */

int64_t nt2unix(uint64_t t) {
  /* 100ns intervals since 1601-01-01 -> seconds since 1970-01-01 */
  return (int64_t)(t / 10000000ULL) - 11644473600LL;
}

void attr_free(attr_t *a) {
  free(a->data);
  free(a->runs);
  free(a->cu_buf);
  memset(a, 0, sizeof *a);
}

int run_push(attr_t *a, int64_t vcn, int64_t lcn, uint64_t len) {
  /* Merge with the previous run when contiguous; keeps lists short. */
  if (a->nruns) {
    run_t *p = &a->runs[a->nruns - 1];
    if (p->vcn + (int64_t)p->len == vcn &&
        ((p->lcn < 0 && lcn < 0) ||
         (p->lcn >= 0 && lcn >= 0 && p->lcn + (int64_t)p->len == lcn))) {
      p->len += len;
      return 0;
    }
  }
  if (a->nruns == a->cap) {
    size_t nc = a->cap ? a->cap * 2 : 16;
    run_t *r = realloc(a->runs, nc * sizeof *r);
    if (!r) return -ENOMEM;
    a->runs = r;
    a->cap = nc;
  }
  a->runs[a->nruns++] = (run_t){vcn, lcn, len};
  return 0;
}

/* Decode a mapping-pairs array (the run list of a non-resident attribute). */
int decode_runs(attr_t *a, const uint8_t *p, const uint8_t *end,
                       int64_t vcn) {
  int64_t lcn = 0;
  const uint64_t lim = (uint64_t)INT64_MAX >> 21;       /* clusters of at most 2 MiB: vcn/lcn * cluster stays below 2^63 */
  if (vcn < 0 || (uint64_t)vcn > lim) return -EIO;
  while (p < end && *p) {
    int lb = *p & 0x0F, ob = *p >> 4;
    p++;
    if (!lb || lb > 8 || ob > 8 || p + lb + ob > end) return -EIO;
    uint64_t len = 0;
    for (int i = 0; i < lb; i++) len |= (uint64_t)p[i] << (8 * i);
    p += lb;
    int64_t delta = 0;
    if (ob) {
      uint64_t u = 0;
      for (int i = 0; i < ob; i++) u |= (uint64_t)p[i] << (8 * i);
      if (ob < 8 && (p[ob - 1] & 0x80)) u |= ~0ULL << (8 * ob); /* sign extend */
      delta = (int64_t)u;
      p += ob;
      if (__builtin_add_overflow(lcn, delta, &lcn) || lcn < 0) return -EIO;
    }
    if (!len || len > lim - (uint64_t)vcn || (ob && (uint64_t)lcn > lim)) return -EIO;
    int rc = run_push(a, vcn, ob ? lcn : -1, len);
    if (rc) return rc;
    vcn += (int64_t)len;
  }
  return 0;
}

/* Apply the update sequence array ("fixups") of a multi-sector record. */
int apply_fixups(uint8_t *buf, uint32_t size) {
  uint16_t ofs = le16(buf + 4), cnt = le16(buf + 6);
  if (cnt < 2 || (uint32_t)(cnt - 1) * 512 != size || ofs + cnt * 2u > size ||
      (ofs & 1))
    return -EIO;
  uint16_t usn = le16(buf + ofs);
  for (uint16_t i = 1; i < cnt; i++) {
    uint8_t *tail = buf + i * 512 - 2;
    if (le16(tail) != usn) return -EIO; /* torn write */
    memcpy(tail, buf + ofs + i * 2, 2);
  }
  return 0;
}

/* ---- LZNT1 --------------------------------------------------------------- */

ssize_t ntfs_lznt1_decompress(const uint8_t *src, size_t srclen, uint8_t *dst,
                              size_t dstlen) {
  size_t sp = 0, dp = 0;
  while (sp + 2 <= srclen && dp < dstlen) {
    uint16_t hdr = le16(src + sp);
    if (!hdr) break;
    size_t chunk_len = (size_t)(hdr & 0x0FFF) + 1;
    size_t cs = sp + 2, ce = cs + chunk_len;
    if (ce > srclen) return -1;
    size_t chunk_start = dp;
    size_t chunk_end = dp + 4096 > dstlen ? dstlen : dp + 4096;

    if (!(hdr & 0x8000)) {
      /* Stored (uncompressed) chunk. */
      size_t n = chunk_len;
      if (n > chunk_end - dp) n = chunk_end - dp;
      memcpy(dst + dp, src + cs, n);
      dp += n;
    } else {
      size_t p = cs;
      while (p < ce && dp < chunk_end) {
        uint8_t tag = src[p++];
        for (int bit = 0; bit < 8 && p < ce && dp < chunk_end; bit++) {
          if (!(tag & (1 << bit))) {
            dst[dp++] = src[p++];
            continue;
          }
          if (p + 2 > ce) return -1;
          uint16_t tok = le16(src + p);
          p += 2;
          size_t pos = dp - chunk_start;
          if (!pos) return -1; /* a back-reference needs earlier output */
          int lg = 0;
          for (size_t i = pos - 1; i >= 0x10; i >>= 1) lg++;
          size_t back = (size_t)(tok >> (12 - lg)) + 1;
          size_t len = (size_t)(tok & (0x0FFF >> lg)) + 3;
          if (back > pos) return -1;
          for (size_t i = 0; i < len && dp < chunk_end; i++, dp++)
            dst[dp] = dst[dp - back];
        }
      }
    }
    /* A chunk that decompresses to less than 4 KiB is padded with zeros. */
    if (dp < chunk_end && ce + 2 <= srclen && le16(src + ce)) {
      memset(dst + dp, 0, chunk_end - dp);
      dp = chunk_end;
    }
    sp = ce;
  }
  return (ssize_t)dp;
}

/* ---- Attribute data access ----------------------------------------------- */

const run_t *find_run(const attr_t *a, int64_t vcn) {
  size_t lo = 0, hi = a->nruns;
  while (lo < hi) {
    size_t mid = (lo + hi) / 2;
    const run_t *r = &a->runs[mid];
    if (vcn < r->vcn) hi = mid;
    else if (vcn >= r->vcn + (int64_t)r->len) lo = mid + 1;
    else return r;
  }
  return NULL;
}

/* Read uncompressed clusters [off, off+len) of a non-resident attribute,
 * ignoring sizes. Unmapped or sparse ranges read as zeros. */
static int read_raw(ntfs_vol_t *v, const attr_t *a, uint64_t off, uint8_t *buf,
                    size_t len) {
  uint32_t cs = v->cluster;
  while (len) {
    int64_t vcn = (int64_t)(off / cs);
    const run_t *r = find_run(a, vcn);
    uint64_t run_end = r ? (uint64_t)(r->vcn + (int64_t)r->len) * cs
                         : (off / cs + 1) * cs;
    size_t n = run_end - off < len ? (size_t)(run_end - off) : len;
    if (!n) return -EIO;
    if (!r || r->lcn < 0) {
      memset(buf, 0, n);
    } else {
      uint64_t phys = (uint64_t)r->lcn * cs + (off - (uint64_t)r->vcn * cs);
      if (bdev_read(v->dev, phys, buf, n) != 0) return -EIO;
    }
    buf += n; off += n; len -= n;
  }
  return 0;
}

/* Load one compression unit into a->cu_buf. */
static int load_cu(ntfs_vol_t *v, attr_t *a, int64_t idx) {
  if (a->cu_buf && a->cu_idx == idx) return 0;
  uint32_t cs = v->cluster;
  size_t cu_bytes = (size_t)a->cu_clusters * cs;
  if (!a->cu_buf && !(a->cu_buf = malloc(cu_bytes))) return -ENOMEM;
  a->cu_idx = -1;

  int64_t vcn0 = idx * a->cu_clusters;
  uint32_t allocated = 0;
  for (uint32_t i = 0; i < a->cu_clusters;) {
    const run_t *r = find_run(a, vcn0 + i);
    if (!r) break;
    int64_t avail = r->vcn + (int64_t)r->len - (vcn0 + i);
    uint32_t take = avail < (int64_t)(a->cu_clusters - i) ? (uint32_t)avail
                                                          : a->cu_clusters - i;
    if (r->lcn < 0) break; /* the rest of a compressed unit is sparse */
    allocated += take;
    i += take;
  }

  uint64_t off = (uint64_t)vcn0 * cs;
  if (allocated == 0) {
    memset(a->cu_buf, 0, cu_bytes);
  } else if (allocated == a->cu_clusters) {
    /* Data that did not compress well is stored as-is. */
    if (read_raw(v, a, off, a->cu_buf, cu_bytes)) return -EIO;
  } else {
    size_t clen = (size_t)allocated * cs;
    uint8_t *tmp = malloc(clen);
    if (!tmp) return -ENOMEM;
    if (read_raw(v, a, off, tmp, clen)) { free(tmp); return -EIO; }
    memset(a->cu_buf, 0, cu_bytes);
    ssize_t n = ntfs_lznt1_decompress(tmp, clen, a->cu_buf, cu_bytes);
    free(tmp);
    if (n < 0) return -EIO;
  }
  a->cu_idx = idx;
  return 0;
}

/* Read from an attribute's value. Returns bytes read (short at EOF). */
ssize_t attr_pread(ntfs_vol_t *v, attr_t *a, uint64_t off, void *out,
                          size_t len) {
  uint8_t *buf = out;
  if (off >= a->size) return 0;
  if (len > a->size - off) len = (size_t)(a->size - off);

  if (a->resident) {
    size_t have = off < a->data_len ? a->data_len - (size_t)off : 0;
    size_t n = len < have ? len : have;
    memcpy(buf, a->data + off, n);
    memset(buf + n, 0, len - n);
    return (ssize_t)len;
  }

  /* Everything past the initialised size reads as zero. */
  size_t real = len;
  if (off + len > a->init) real = off >= a->init ? 0 : (size_t)(a->init - off);
  memset(buf + real, 0, len - real);

  if (!a->cu_clusters) {
    if (real && read_raw(v, a, off, buf, real)) return -EIO;
    return (ssize_t)len;
  }

  uint64_t cu_bytes = (uint64_t)a->cu_clusters * v->cluster;
  size_t done = 0;
  while (done < real) {
    uint64_t pos = off + done;
    int64_t idx = (int64_t)(pos / cu_bytes);
    int rc = load_cu(v, a, idx);
    if (rc) return rc;
    size_t in = (size_t)(pos % cu_bytes);
    size_t n = (size_t)cu_bytes - in;
    if (n > real - done) n = real - done;
    memcpy(buf + done, a->cu_buf + in, n);
    done += n;
  }
  return (ssize_t)len;
}

/* ---- MFT records --------------------------------------------------------- */

/* A record must agree with itself before anything trusts it (app, 23.11.2026; the walk over the attributes 24.11.2026 after upstream
   PR #17, the checks ntfs-3g makes before using a record): its sizes match the record size, the attributes start behind the fixup
   array and end with the end marker inside the used part, and every attribute's name, resident value and run list lie inside the
   attribute. The writer moves and resizes attributes in place from these fields (rec_insert with a length past `used` moved about
   4 GB), and the readers take values and run lists from them. */
static int record_ok(const ntfs_vol_t *v, const uint8_t *buf) {
  uint32_t used = le32(buf + 0x18), cap = le32(buf + 0x1C), ao = le16(buf + 0x14);
  if (cap != v->rec_size || used > cap || ao < 0x18 || (ao & 7) || ao + 8 > used) return 0;
  if (ao < le16(buf + 4) + 2u * le16(buf + 6)) return 0;
  for (uint32_t p = ao;;) {
    if ((uint64_t)p + 4 > used) return 0;                     /* no end marker */
    if (le32(buf + p) == AT_END) return 1;
    if ((uint64_t)p + 8 > used) return 0;
    const uint8_t *at = buf + p;
    uint32_t len = le32(at + 4);
    if (len < 24 || (len & 7) || len > used - p) return 0;
    if (at[9] && le16(at + 10) + 2u * at[9] > len) return 0;
    if (!at[8]) {
      if ((uint64_t)le16(at + 20) + le32(at + 16) > len) return 0;
    } else if (len < 0x40 || le16(at + 32) >= len) {
      return 0;
    }
    p += len;
  }
}

int read_record_any(ntfs_vol_t *v, uint64_t inode, uint8_t *buf) {
  uint64_t off = inode * v->rec_size;
  if (off >= v->mft.size) return -ENOENT;
  ssize_t n = attr_pread(v, &v->mft, off, buf, v->rec_size);
  if (n != (ssize_t)v->rec_size) return -EIO;
  if (memcmp(buf, "FILE", 4) != 0) return -ENOENT;
  if (apply_fixups(buf, v->rec_size)) return -EIO;
  return record_ok(v, buf) ? 0 : -EIO;
}

int read_record(ntfs_vol_t *v, uint64_t inode, uint8_t *buf) {
  uint64_t off = inode * v->rec_size;
  if (off >= v->mft.size) return -ENOENT;
  ssize_t n = attr_pread(v, &v->mft, off, buf, v->rec_size);
  if (n != (ssize_t)v->rec_size) return -EIO;
  if (memcmp(buf, "FILE", 4) != 0) return -ENOENT;
  if (apply_fixups(buf, v->rec_size)) return -EIO;
  if (!record_ok(v, buf)) return -EIO;
  if (!(le16(buf + 0x16) & MFT_IN_USE)) return -ENOENT;
  return 0;
}

/* Iterate attributes in a record. *pos starts at 0. */
const uint8_t *next_attr(const uint8_t *rec, uint32_t rec_size,
                                uint32_t *pos) {
  uint32_t p = *pos ? *pos : le16(rec + 0x14);
  uint32_t used = le32(rec + 0x18);
  if (used > rec_size) used = rec_size;
  if (p + 8 > used) return NULL;
  const uint8_t *at = rec + p;
  uint32_t type = le32(at), len = le32(at + 4);
  if (type == AT_END || len < 24 || len > used - p || (len & 7)) return NULL;
  *pos = p + len;
  return at;
}

int attr_name_eq(const uint8_t *at, const uint16_t *name, int nlen) {
  if (at[9] != nlen) return 0;
  if (le16(at + 10) + (uint32_t)nlen * 2 > le32(at + 4)) return 0;
  const uint8_t *n = at + le16(at + 10);
  for (int i = 0; i < nlen; i++)
    if (le16(n + i * 2) != name[i]) return 0;
  return 1;
}

/* Merge one attribute record (a segment for non-resident ones) into a. */
int absorb(attr_t *a, const uint8_t *at, uint32_t at_len, int first) {
  if (!at[8]) {
    uint32_t vlen = le32(at + 16);
    uint16_t voff = le16(at + 20);
    if (voff > at_len || vlen > at_len - voff) return -EIO;
    a->resident = 1;
    a->data = malloc(vlen ? vlen : 1);
    if (!a->data) return -ENOMEM;
    memcpy(a->data, at + voff, vlen);
    a->data_len = vlen;
    a->size = a->init = a->alloc = vlen;
    a->flags = le16(at + 12);
    return 0;
  }
  if (at_len < 64) return -EIO;
  int64_t lowest = (int64_t)le64(at + 16);
  uint16_t mp = le16(at + 32);
  if (mp >= at_len) return -EIO;
  if (first || lowest == 0) {
    a->flags = le16(at + 12);
    a->alloc = le64(at + 40);
    a->size = le64(at + 48);
    a->init = le64(at + 56);
    uint8_t cu = at[34];
    if ((a->flags & ATTR_COMPRESSED) && cu) {
      if (cu > 4) return -EIO;
      a->cu_clusters = 1u << cu;
    }
    if (a->init > a->size) a->init = a->size;
  }
  return decode_runs(a, at + mp, at + at_len, lowest);
}

/* Find an attribute of an inode, following $ATTRIBUTE_LIST if present.
 * name may be NULL (unnamed). */
int attr_find(ntfs_vol_t *v, uint64_t inode, uint32_t type,
                     const uint16_t *name, int nlen, attr_t *out) {
  memset(out, 0, sizeof *out);
  out->cu_idx = -1;
  uint8_t *rec = malloc(v->rec_size);
  if (!rec) return -ENOMEM;
  int rc = read_record(v, inode, rec);
  if (rc) goto done;

  /* Without an attribute list, the base record holds every attribute.
   * With one, the list says which records hold each piece. */
  const uint8_t *at, *list = NULL, *match = NULL;
  uint32_t pos = 0;
  while ((at = next_attr(rec, v->rec_size, &pos))) {
    uint32_t t = le32(at);
    if (t == AT_ATTRIBUTE_LIST && !list) list = at;
    if (t == type && !match && attr_name_eq(at, name, nlen)) match = at;
  }
  if (!list) {
    rc = match ? absorb(out, match, le32(match + 4), 1) : -ENOENT;
    goto done;
  }

  /* Read the attribute list itself (resident or not). */
  attr_t la;
  memset(&la, 0, sizeof la);
  la.cu_idx = -1;
  rc = absorb(&la, list, le32(list + 4), 1);
  if (rc) { attr_free(&la); goto done; }
  if (la.size > 16 * 1024 * 1024) { attr_free(&la); rc = -EIO; goto done; }
  uint8_t *lbuf = malloc(la.size ? (size_t)la.size : 1);
  if (!lbuf) { attr_free(&la); rc = -ENOMEM; goto done; }
  ssize_t ln = attr_pread(v, &la, 0, lbuf, (size_t)la.size);
  attr_free(&la);
  if (ln < 0) { free(lbuf); rc = (int)ln; goto done; }

  uint8_t *ext = malloc(v->rec_size);
  if (!ext) { free(lbuf); rc = -ENOMEM; goto done; }
  int found = 0;
  /* The segments of a non-resident attribute follow each other: each starts where the runs read so far end (the list is sorted by
     lowest VCN, as Windows and ntfs-3g write it). Without this a list naming the same segment again and again appended its runs again
     and again: hundreds of MB from a few MB on the stick, already while mounting ($MFT). (app, 24.11.2026, after upstream PR #21; the
     end is taken from the runs, not from the segment's highest VCN, which is -1 for an empty attribute and would wrap to 0.) */
  uint64_t next_vcn = 0;
  rc = 0;
  for (size_t p = 0; p + 26 <= (size_t)ln && !rc;) {
    const uint8_t *e = lbuf + p;
    uint16_t elen = le16(e + 4);
    if (elen < 26 || p + elen > (size_t)ln) break;
    p += elen;
    if (le32(e) != type || e[6] != nlen) continue;
    if (e[7] + nlen * 2u > elen) continue;
    int match = 1;
    for (int i = 0; i < nlen && match; i++)
      if (le16(e + e[7] + i * 2) != name[i]) match = 0;
    if (!match) continue;

    uint64_t lowest = le64(e + 8), ref = MREF(le64(e + 16));
    uint16_t inst = le16(e + 24);
    const uint8_t *src = rec;
    if (ref != inode) {
      if (read_record(v, ref, ext)) { rc = -EIO; break; }
      src = ext;
    }
    uint32_t p2 = 0;
    const uint8_t *seg;
    while ((seg = next_attr(src, v->rec_size, &p2))) {
      if (le32(seg) == type && le16(seg + 14) == inst &&
          attr_name_eq(seg, name, nlen)) {
        if (seg[8] && le64(seg + 16) != lowest) continue;
        if (seg[8] && lowest != next_vcn) { rc = -EIO; break; }
        const int later = found;
        rc = absorb(out, seg, le32(seg + 4), !found);
        if (!rc && seg[8]) {
          const run_t *r = out->nruns ? &out->runs[out->nruns - 1] : NULL;
          uint64_t end = r ? (uint64_t)r->vcn + r->len : 0;
          /* a segment after the first maps clusters in a valid list; one that adds none could be named again and again, each time
             a record read under the lock (upstream PR #21, commit 862fda8) */
          if (later && end <= next_vcn) { rc = -EIO; break; }
          next_vcn = end;
        }
        found = 1;
        break;
      }
    }
    if (found && out->resident) break;
  }
  free(ext);
  free(lbuf);
  if (!rc && !found) rc = -ENOENT;

done:
  if (rc) attr_free(out);
  free(rec);
  return rc;
}

/* ---- Directory indexes --------------------------------------------------- */

typedef struct idx_ctx {
  ntfs_vol_t *v;
  attr_t      alloc;
  int         have_alloc;
  uint32_t    block_size;
  uint32_t    vcn_unit;
  uint8_t    *blocks[MAX_DEPTH];
  unsigned    visited, max_visits;
  uint64_t   *seen;                 /* (app) the blocks visited, an open-addressing set of vcn + 1 */
  size_t      seen_n, seen_cap;
} idx_ctx_t;

/* (app, 23.11.2026) A B-tree reaches each block once: a block reached again is a damaged (or made-up) index. Without this, a few blocks
 * that point at each other many times kept a listing busy for minutes. 1 new, 0 seen before, -ENOMEM. */
static int seen_add(idx_ctx_t *ix, uint64_t vcn) {
  uint64_t key = vcn + 1;
  if ((ix->seen_n + 1) * 2 > ix->seen_cap) {
    size_t nc = ix->seen_cap ? ix->seen_cap * 2 : 64;
    uint64_t *ns = calloc(nc, sizeof *ns);
    if (!ns) return -ENOMEM;
    for (size_t i = 0; i < ix->seen_cap; i++) {
      uint64_t k = ix->seen[i];
      if (!k) continue;
      size_t h = (size_t)((k * 0x9E3779B97F4A7C15ull) & (nc - 1));
      while (ns[h]) h = (h + 1) & (nc - 1);
      ns[h] = k;
    }
    free(ix->seen);
    ix->seen = ns;
    ix->seen_cap = nc;
  }
  size_t h = (size_t)((key * 0x9E3779B97F4A7C15ull) & (ix->seen_cap - 1));
  while (ix->seen[h]) {
    if (ix->seen[h] == key) return 0;
    h = (h + 1) & (ix->seen_cap - 1);
  }
  ix->seen[h] = key;
  ix->seen_n++;
  return 1;
}

/* Callback: return 1 to stop the walk, 0 to continue, <0 on error. */

static int walk_node(idx_ctx_t *ix, const uint8_t *p, const uint8_t *end,
                     int depth, idx_cb cb, void *ctx);

static int walk_block(idx_ctx_t *ix, uint64_t vcn, int depth, idx_cb cb,
                      void *ctx) {
  if (!ix->have_alloc || depth >= MAX_DEPTH || ++ix->visited > ix->max_visits)
    return -EIO;
  int fresh = seen_add(ix, vcn);
  if (fresh <= 0) return fresh < 0 ? fresh : -EIO;
  if (!ix->blocks[depth] && !(ix->blocks[depth] = malloc(ix->block_size)))
    return -ENOMEM;
  uint8_t *b = ix->blocks[depth];
  ssize_t n = attr_pread(ix->v, &ix->alloc, vcn * ix->vcn_unit, b, ix->block_size);
  if (n != (ssize_t)ix->block_size) return -EIO;
  if (memcmp(b, "INDX", 4) || apply_fixups(b, ix->block_size)) return -EIO;
  const uint8_t *h = b + 0x18;
  uint32_t eo = le32(h), il = le32(h + 4);
  if (0x18 + (uint64_t)il > ix->block_size || eo >= il) return -EIO;
  return walk_node(ix, h + eo, h + il, depth + 1, cb, ctx);
}

/* In-order walk of the index entries in one node. */
static int walk_node(idx_ctx_t *ix, const uint8_t *p, const uint8_t *end,
                     int depth, idx_cb cb, void *ctx) {
  while (p + 16 <= end) {
    uint16_t len = le16(p + 8), klen = le16(p + 10), flags = le16(p + 12);
    if (len < 16 || p + len > end) return -EIO;
    if (flags & IE_NODE) {
      if (len < 24) return -EIO;
      int rc = walk_block(ix, le64(p + len - 8), depth, cb, ctx);
      if (rc) return rc;
    }
    if (flags & IE_LAST) return 0;
    if (klen >= 0x42 && 16u + klen <= len) {
      int rc = cb(ctx, le64(p), p + 16, klen);
      if (rc) return rc;
    }
    p += len;
  }
  return 0;
}

/* The index of a folder opened for a walk or a search: its root (resident) and, if it has one, its allocation. 0, or an error with
 * nothing left to free. */
static int ix_open(ntfs_vol_t *v, uint64_t inode, idx_ctx_t *ix, attr_t *root) {
  int rc = attr_find(v, inode, AT_INDEX_ROOT, I30, 4, root);
  if (rc) return rc == -ENOENT ? -ENOTDIR : rc;
  if (!root->resident || root->data_len < 32) { attr_free(root); return -EIO; }

  memset(ix, 0, sizeof *ix);
  ix->v = v;
  ix->block_size = le32(root->data + 8);
  ix->vcn_unit = v->cluster <= ix->block_size ? v->cluster : 512;
  if (attr_find(v, inode, AT_INDEX_ALLOCATION, I30, 4, &ix->alloc) == 0) {
    ix->have_alloc = 1;
    if (ix->block_size < 512 || ix->block_size > 65536) rc = -EIO;
    uint64_t mapped = 0;
    for (size_t i = 0; i < ix->alloc.nruns; i++)
      if (ix->alloc.runs[i].lcn >= 0) mapped += ix->alloc.runs[i].len;
    uint64_t blocks = ix->alloc.size / (ix->block_size ? ix->block_size : 1);
    uint64_t mblocks = mapped * v->cluster / (ix->block_size ? ix->block_size : 1);
    if (blocks > mblocks) blocks = mblocks;
    ix->max_visits = (unsigned)(blocks > (1u << 20) ? (1u << 20) : blocks) + 1;
  }
  if (rc) {
    if (ix->have_alloc) attr_free(&ix->alloc);
    attr_free(root);
  }
  return rc;
}

static void ix_close(idx_ctx_t *ix, attr_t *root) {
  for (int i = 0; i < MAX_DEPTH; i++) free(ix->blocks[i]);
  free(ix->seen);
  if (ix->have_alloc) attr_free(&ix->alloc);
  attr_free(root);
}

int index_walk(ntfs_vol_t *v, uint64_t inode, idx_cb cb, void *ctx) {
  attr_t root;
  idx_ctx_t ix;
  int rc = ix_open(v, inode, &ix, &root);
  if (rc) return rc;
  const uint8_t *h = root.data + 16;
  uint32_t eo = le32(h), il = le32(h + 4);
  if (16 + (uint64_t)il > root.data_len || eo >= il) rc = -EIO;
  else rc = walk_node(&ix, h + eo, h + il, 0, cb, ctx);
  ix_close(&ix, &root);
  return rc < 0 ? rc : 0;
}

/* ---- Finding one name: down the sorted tree --------------------------------
 * (app, 24.11.2026) The entries of a folder's index are sorted by the upper-case name ($UpCase), shorter first on a tie, as Windows
 * writes them and index_insert keeps them. A name is found by going down one path: in each node the first entry that does not sort
 * before it is either the name or the one whose sub-node holds it. Before, every lookup walked all entries: copying or deleting n files
 * in a folder of n entries read the folder n times (a folder of 100 000 files took hours). */

typedef struct find_one {
  const uint16_t *name;
  int             nlen;
  uint64_t        ref;
  int             exact;
} find_one_t;

static int name_cmp(const ntfs_vol_t *v, const uint16_t *a, int al, const uint8_t *b, int bl) {
  int n = al < bl ? al : bl;
  for (int i = 0; i < n; i++) {
    uint16_t x = up(v, a[i]), y = up(v, le16(b + i * 2));
    if (x != y) return x < y ? -1 : 1;
  }
  return al == bl ? 0 : al < bl ? -1 : 1;
}

static int find_node(idx_ctx_t *ix, const uint8_t *p, const uint8_t *end, int depth, find_one_t *f);

static int find_block(idx_ctx_t *ix, uint64_t vcn, int depth, find_one_t *f) {
  if (!ix->have_alloc || depth >= MAX_DEPTH || ++ix->visited > ix->max_visits) return -EIO;
  int fresh = seen_add(ix, vcn);
  if (fresh <= 0) return fresh < 0 ? fresh : -EIO;
  if (!ix->blocks[depth] && !(ix->blocks[depth] = malloc(ix->block_size))) return -ENOMEM;
  uint8_t *b = ix->blocks[depth];
  if (attr_pread(ix->v, &ix->alloc, vcn * ix->vcn_unit, b, ix->block_size) != (ssize_t)ix->block_size) return -EIO;
  if (memcmp(b, "INDX", 4) || apply_fixups(b, ix->block_size)) return -EIO;
  const uint8_t *h = b + 0x18;
  uint32_t eo = le32(h), il = le32(h + 4);
  if (0x18 + (uint64_t)il > ix->block_size || eo >= il) return -EIO;
  return find_node(ix, h + eo, h + il, depth + 1, f);
}

/* 1 found (f->ref, f->exact), 0 not there, <0 error */
/* (audit 24.11.2026) 1 if the entries of one leaf (from p to its end entry) are in the order the descent relies on. */
static int leaf_sorted(const ntfs_vol_t *v, const uint8_t *p, const uint8_t *end) {
  const uint8_t *prev = NULL;
  while (p + 16 <= end) {
    uint16_t len = le16(p + 8), klen = le16(p + 10), flags = le16(p + 12);
    if (len < 16 || p + len > end) return 0;
    if (flags & IE_LAST) return 1;
    if (klen < 0x42 || 16u + klen > len || 0x42u + p[16 + 0x40] * 2u > klen) return 0;
    if (prev) {
      const uint8_t *a = prev + 16 + 0x42, *b = p + 16 + 0x42;
      int al = prev[16 + 0x40], bl = p[16 + 0x40], n = al < bl ? al : bl, c = 0;
      for (int i = 0; i < n && !c; i++) {
        uint16_t x = up(v, le16(a + i * 2)), y = up(v, le16(b + i * 2));
        c = x == y ? 0 : x < y ? -1 : 1;
      }
      if (!c && al != bl) c = al < bl ? -1 : 1;
      if (c > 0) return 0;
    }
    prev = p;
    p += len;
  }
  return 0;
}

static int find_node(idx_ctx_t *ix, const uint8_t *p, const uint8_t *end, int depth, find_one_t *f) {
  const uint8_t *first = p;
  while (p + 16 <= end) {
    uint16_t len = le16(p + 8), klen = le16(p + 10), flags = le16(p + 12);
    if (len < 16 || p + len > end) return -EIO;
    if ((flags & IE_NODE) && len < 24) return -EIO;
    int c = -1;                                     /* the end entry sorts after every name */
    if (!(flags & IE_LAST)) {
      if (klen < 0x42 || 16u + klen > len) return -EIO;
      const uint8_t *fn = p + 16;
      if (0x42u + fn[0x40] * 2u > klen) return -EIO;
      c = name_cmp(ix->v, f->name, f->nlen, fn + 0x42, fn[0x40]);
      if (c == 0) {
        f->ref = le64(p);
        f->exact = 1;
        for (int i = 0; i < f->nlen; i++)
          if (le16(fn + 0x42 + i * 2) != f->name[i]) f->exact = 0;
        return 1;
      }
    }
    if (c < 0) {
      if (flags & IE_NODE) return find_block(ix, le64(p + len - 8), depth, f);
      /* a miss in a leaf counts only if the leaf is sorted: in a damaged index (or one a foreign tool wrote) the name may be there, and
       * the "name taken" check of a create would let a second one be made. -EIO: the caller walks every entry, as before. */
      return leaf_sorted(ix->v, first, end) ? 0 : -EIO;
    }
    p += len;
  }
  return -EIO;                                      /* a node without its end entry */
}

int index_find(ntfs_vol_t *v, uint64_t dir, const uint16_t *name, int nlen, uint64_t *ref, int *exact) {
  attr_t root;
  idx_ctx_t ix;
  int rc = ix_open(v, dir, &ix, &root);
  if (rc) return rc;
  find_one_t f = {name, nlen, 0, 0};
  const uint8_t *h = root.data + 16;
  uint32_t eo = le32(h), il = le32(h + 4);
  if (16 + (uint64_t)il > root.data_len || eo >= il) rc = -EIO;
  else rc = find_node(&ix, h + eo, h + il, 0, &f);
  ix_close(&ix, &root);
  if (rc < 0) return rc;
  if (!rc) return -ENOENT;
  *ref = f.ref;
  if (exact) *exact = f.exact;
  return 0;
}

/* ---- Stat ---------------------------------------------------------------- */

int stat_locked(ntfs_vol_t *v, uint64_t inode, ntfs_stat_t *st) {
  uint8_t *rec = malloc(v->rec_size);
  if (!rec) return -ENOMEM;
  int rc = read_record(v, inode, rec);
  if (rc) { free(rec); return rc; }
  if (le64(rec + 0x20)) { free(rec); return -ENOENT; } /* extension record */

  memset(st, 0, sizeof *st);
  st->inode = inode;
  st->nlink = le16(rec + 0x12);
  st->is_dir = (le16(rec + 0x16) & MFT_DIRECTORY) != 0;

  const uint8_t *at;
  uint32_t pos = 0;
  while ((at = next_attr(rec, v->rec_size, &pos))) {
    if (le32(at) == AT_STANDARD_INFORMATION && !at[8]) {
      const uint8_t *si = at + le16(at + 20);
      if (le32(at + 16) >= 36 && si + 36 <= rec + v->rec_size) {
        st->crtime = nt2unix(le64(si));
        st->mtime = nt2unix(le64(si + 8));
        st->atime = nt2unix(le64(si + 24));
        st->attrs = le32(si + 32);
      }
    }
  }
  free(rec);
  if (st->is_dir) {
    st->attrs |= NTFS_FA_DIRECTORY;
    return 0;
  }

  attr_t d;
  if (attr_find(v, inode, AT_DATA, NULL, 0, &d) == 0) {
    st->size = d.size;
    st->alloc = d.resident ? 0 : d.alloc;
    attr_free(&d);
  }
  return 0;
}

int ntfs_stat(ntfs_vol_t *v, uint64_t inode, ntfs_stat_t *st) {
  pthread_mutex_lock(&v->lock);
  int rc = stat_locked(v, inode, st);
  pthread_mutex_unlock(&v->lock);
  return rc;
}

/* ---- Readdir ------------------------------------------------------------- */

typedef struct dent {
  uint64_t ref;
  uint8_t  fn[66]; /* $FILE_NAME header, used as a fallback for stat */
  char    *name;
} dent_t;

typedef struct dlist {
  dent_t *e;
  size_t  n, cap;
  int     is_root;
} dlist_t;

static int collect_cb(void *ctx, uint64_t ref, const uint8_t *fn, uint16_t fnlen) {
  dlist_t *l = ctx;
  ref = MREF(ref);
  uint8_t nchars = fn[0x40], ns = fn[0x41];
  if (ns == NS_DOS) return 0;
  if (0x42u + nchars * 2u > fnlen) return 0;
  if (l->is_root && ref < FIRST_USER_INODE) return 0;
  if (l->n == l->cap) {
    size_t nc = l->cap ? l->cap * 2 : 64;
    dent_t *e = realloc(l->e, nc * sizeof *e);
    if (!e) return -ENOMEM;
    l->e = e;
    l->cap = nc;
  }
  dent_t *d = &l->e[l->n];
  d->ref = ref;
  memcpy(d->fn, fn, sizeof d->fn);
  d->name = utf16le_to_utf8(fn + 0x42, nchars);
  if (!d->name) return -ENOMEM;
  l->n++;
  return 0;
}

int ntfs_readdir(ntfs_vol_t *v, uint64_t dir, ntfs_dir_cb cb, void *ctx) {
  dlist_t l = {0};
  l.is_root = dir == NTFS_ROOT_INODE;
  pthread_mutex_lock(&v->lock);
  int rc = index_walk(v, dir, collect_cb, &l);
  if (!rc) {
    for (size_t i = 0; i < l.n; i++) {
      ntfs_stat_t st;
      if (stat_locked(v, l.e[i].ref, &st) != 0) {
        /* Fall back to the (possibly stale) copy kept in the index. */
        const uint8_t *fn = l.e[i].fn;
        memset(&st, 0, sizeof st);
        st.inode = l.e[i].ref;
        st.crtime = nt2unix(le64(fn + 0x08));
        st.mtime = nt2unix(le64(fn + 0x10));
        st.atime = nt2unix(le64(fn + 0x20));
        st.alloc = le64(fn + 0x28);
        st.size = le64(fn + 0x30);
        st.attrs = le32(fn + 0x38);
        st.is_dir = (st.attrs & 0x10000000) != 0; /* DUP_FILE_NAME_INDEX_PRESENT */
        if (st.is_dir) st.attrs |= NTFS_FA_DIRECTORY;
        st.nlink = 1;
      }
      if (cb(ctx, l.e[i].name, &st)) break;
    }
  }
  pthread_mutex_unlock(&v->lock);
  for (size_t i = 0; i < l.n; i++) free(l.e[i].name);
  free(l.e);
  return rc;
}

/* ---- Lookup -------------------------------------------------------------- */

typedef struct find_ctx {
  ntfs_vol_t     *v;
  const uint16_t *name;
  size_t          len;
  uint64_t        ref;
} find_ctx_t;

uint16_t up(const ntfs_vol_t *v, uint16_t c) {
  if (c < v->upcase_len) return v->upcase[c];
  if (!v->upcase_len && c >= 'a' && c <= 'z') return (uint16_t)(c - 32);
  return c;
}

static int find_cb(void *ctx, uint64_t ref, const uint8_t *fn, uint16_t fnlen) {
  find_ctx_t *f = ctx;
  ref = MREF(ref);
  uint8_t n = fn[0x40];
  if (n != f->len || 0x42u + n * 2u > fnlen) return 0;
  /* Exact match wins straight away. */
  int exact = 1, ci = 1;
  for (size_t i = 0; i < n; i++) {
    uint16_t c = le16(fn + 0x42 + i * 2);
    if (c != f->name[i]) exact = 0;
    if (up(f->v, c) != up(f->v, f->name[i])) { ci = 0; break; }
  }
  if (!ci) return 0;
  f->ref = ref;
  return exact ? 1 : 0; /* keep looking for an exact-case match */
}

int lookup_locked(ntfs_vol_t *v, const char *path, uint64_t *inode) {
  uint64_t cur = NTFS_ROOT_INODE;
  const char *p = path;
  while (*p) {
    while (*p == '/') p++;
    if (!*p) break;
    const char *e = strchr(p, '/');
    size_t clen = e ? (size_t)(e - p) : strlen(p);
    if ((clen == 1 && p[0] == '.')) { p += clen; continue; }
    if (clen == 2 && p[0] == '.' && p[1] == '.') return -EINVAL;

    char comp[1024];
    if (clen >= sizeof comp) return -ENAMETOOLONG;
    memcpy(comp, p, clen);
    comp[clen] = 0;
    uint16_t name[256];
    int nlen = utf8_to_utf16(comp, name, 256);
    if (nlen <= 0) return -ENOENT;

    uint64_t ref = 0;
    int exact = 0, rc = index_find(v, cur, name, nlen, &ref, &exact);
    if (rc == -ENOENT || rc == -ENOTDIR) return rc;
    if (rc == 0 && exact) {
      cur = MREF(ref);
    } else {
      /* (app) only another case was found (a name that differs in case only may sit beside it), or the index is damaged: every
       * entry, as before */
      find_ctx_t f = {v, name, (size_t)nlen, (uint64_t)-1};
      rc = index_walk(v, cur, find_cb, &f);
      if (rc < 0) return rc;
      if (f.ref == (uint64_t)-1) return -ENOENT;
      cur = f.ref;
    }
    p += clen;
  }
  *inode = cur;
  return 0;
}

int ntfs_lookup(ntfs_vol_t *v, const char *path, uint64_t *inode) {
  pthread_mutex_lock(&v->lock);
  int rc = lookup_locked(v, path, inode);
  pthread_mutex_unlock(&v->lock);
  return rc;
}

/* ---- Files --------------------------------------------------------------- */

int ntfs_open(ntfs_vol_t *v, uint64_t inode, ntfs_file_t **out) {
  ntfs_file_t *f = calloc(1, sizeof *f);
  if (!f) return -ENOMEM;
  f->v = v;
  pthread_mutex_lock(&v->lock);
  int rc = attr_find(v, inode, AT_DATA, NULL, 0, &f->a);
  if (rc == -ENOENT) {
    /* A directory, or a file without data. */
    ntfs_stat_t st;
    if (stat_locked(v, inode, &st) == 0) rc = st.is_dir ? -EISDIR : 0;
    if (!rc) { memset(&f->a, 0, sizeof f->a); f->a.resident = 1; }
  }
  pthread_mutex_unlock(&v->lock);
  if (!rc && (f->a.flags & ATTR_ENCRYPTED)) rc = -EACCES;
  if (rc) {
    attr_free(&f->a);
    free(f);
    return rc;
  }
  *out = f;
  return 0;
}

ssize_t ntfs_pread(ntfs_file_t *f, void *buf, size_t len, uint64_t off) {
  pthread_mutex_lock(&f->v->lock);
  ssize_t n = attr_pread(f->v, &f->a, off, buf, len);
  pthread_mutex_unlock(&f->v->lock);
  return n;
}

uint64_t ntfs_file_size(const ntfs_file_t *f) { return f->a.size; }

void ntfs_close(ntfs_file_t *f) {
  if (!f) return;
  attr_free(&f->a);
  free(f);
}

/* ---- Mount --------------------------------------------------------------- */

#define FAIL(...) do { snprintf(err, errlen, __VA_ARGS__); goto fail; } while (0)

int ntfs_mount(bdev_t *b, ntfs_vol_t **out, char *err, size_t errlen) {
  uint8_t bs[512];
  ntfs_vol_t *v = NULL;
  uint8_t *rec = NULL;

  if (bdev_read(b, 0, bs, sizeof bs)) { snprintf(err, errlen, "read error"); return -EIO; }
  if (memcmp(bs + 3, "NTFS    ", 8) || bs[510] != 0x55 || bs[511] != 0xAA) {
    snprintf(err, errlen, "not an NTFS volume");
    return -EINVAL;
  }

  v = calloc(1, sizeof *v);
  if (!v) { snprintf(err, errlen, "out of memory"); return -ENOMEM; }
  pthread_mutex_init(&v->lock, NULL);
  v->dev = b;
  v->mft.cu_idx = -1;

  uint32_t bps = le16(bs + 0x0B);
  uint8_t spc_raw = bs[0x0D];
  /* (app, 23.11.2026) clusters of at most 2 MiB: from a shift of 13 on the product below wrapped to 0 in 32 bits (division by zero) */
  if (spc_raw > 0x80 && 256 - spc_raw > 12) FAIL("bad boot sector geometry");
  uint32_t spc = spc_raw <= 0x80 ? spc_raw : 1u << (256 - spc_raw);
  if (bps < 256 || bps > 4096 || (bps & (bps - 1)) || !spc || (spc & (spc - 1)))
    FAIL("bad boot sector geometry");
  if ((uint64_t)bps * spc > 2u * 1024 * 1024) FAIL("unsupported cluster size");
  v->cluster = bps * spc;
  if (!v->cluster) FAIL("bad boot sector geometry");

  int8_t cpr = (int8_t)bs[0x40];
  if (cpr < -31) FAIL("bad MFT record size");
  v->rec_size = cpr > 0 ? (uint32_t)cpr * v->cluster : 1u << -cpr;
  if (v->rec_size < 1024 || v->rec_size > 65536 || (v->rec_size & 511))
    FAIL("bad MFT record size");

  uint64_t total_sectors = le64(bs + 0x28);
  v->total_clusters = total_sectors / spc;
  v->serial = le64(bs + 0x48);
  uint64_t mft_lcn = le64(bs + 0x30);
  /* (app, 23.11.2026) not (much) larger than the device: a made-up size let the free-space count run for years. A few MB more than
   * the device is still read (some USB bridges report a little less than the partition table says), but never written: clusters past
   * the device's end would be handed out and fail. */
  {
    uint64_t dev = bdev_size(b) / v->cluster, slack = dev / 100 + (64u << 20) / v->cluster;
    if (v->total_clusters > dev + slack) FAIL("volume larger than the drive");
    v->short_dev = v->total_clusters > dev;
  }
  if (mft_lcn >= v->total_clusters) FAIL("MFT location out of range");
  v->mft_lcn = mft_lcn;
  v->mftmirr_lcn = le64(bs + 0x38);
  v->sector = bps;
  int8_t cpi = (int8_t)bs[0x44];
  v->idx_size = cpi > 0 ? (uint32_t)cpi * v->cluster : cpi >= -31 ? 1u << -cpi : 0;
  if (v->idx_size < 512 || v->idx_size > 65536) v->idx_size = 4096;

  /* Bootstrap: read $MFT's own record straight from disk. */
  rec = malloc(v->rec_size);
  if (!rec) FAIL("out of memory");
  if (bdev_read(b, mft_lcn * v->cluster, rec, v->rec_size))
    FAIL("cannot read MFT");
  if (memcmp(rec, "FILE", 4) || apply_fixups(rec, v->rec_size))
    FAIL("MFT record 0 is damaged (try chkdsk on Windows)");

  const uint8_t *at;
  uint32_t pos = 0;
  int have = 0, has_list = 0;
  while ((at = next_attr(rec, v->rec_size, &pos))) {
    uint32_t t = le32(at);
    if (t == AT_ATTRIBUTE_LIST) has_list = v->has_list_mft = 1;
    if (t == AT_DATA && at[9] == 0 && at[8] && !have) {
      if (absorb(&v->mft, at, le32(at + 4), 1)) FAIL("bad $MFT run list");
      have = 1;
    }
  }
  if (!have) FAIL("$MFT has no data attribute");
  if (has_list) {
    /* $MFT is spread over several records; load the complete run list
     * using the partial one we already have. */
    attr_t full;
    if (attr_find(v, INODE_MFT, AT_DATA, NULL, 0, &full) == 0) {
      attr_free(&v->mft);
      v->mft = full;
    }
  }
  free(rec);
  rec = NULL;

  ntfs_stat_t st;
  if (stat_locked(v, NTFS_ROOT_INODE, &st) || !st.is_dir)
    FAIL("root directory is missing or damaged");

  /* $UpCase: used for case-insensitive name lookup. */
  attr_t uc;
  if (attr_find(v, INODE_UPCASE, AT_DATA, NULL, 0, &uc) == 0) {
    if (uc.size >= 256 && uc.size <= 131072) {
      v->upcase = malloc((size_t)uc.size);
      if (v->upcase && attr_pread(v, &uc, 0, v->upcase, (size_t)uc.size) ==
                           (ssize_t)uc.size) {
        v->upcase_len = (uint32_t)(uc.size / 2);
        for (uint32_t i = 0; i < v->upcase_len; i++)
          v->upcase[i] = le16(&v->upcase[i]);
      } else {
        free(v->upcase);
        v->upcase = NULL;
      }
    }
    attr_free(&uc);
  }

  /* Volume label and version. */
  attr_t a;
  if (attr_find(v, INODE_VOLUME, AT_VOLUME_NAME, NULL, 0, &a) == 0) {
    if (a.resident && a.data_len) {
      char *s = utf16le_to_utf8(a.data, a.data_len / 2);
      if (s) {
        snprintf(v->label, sizeof v->label, "%s", s);
        free(s);
      }
    }
    attr_free(&a);
  }
  if (attr_find(v, INODE_VOLUME, AT_VOLUME_INFORMATION, NULL, 0, &a) == 0) {
    if (a.resident && a.data_len >= 12) {
      v->ver_major = a.data[8];
      v->ver_minor = a.data[9];
      v->dirty = (le16(a.data + 10) & 0x0001) != 0;
    }
    attr_free(&a);
  }

  *out = v;
  return 0;

fail:
  free(rec);
  if (v) {
    attr_free(&v->mft);
    free(v->upcase);
    pthread_mutex_destroy(&v->lock);
    free(v);
  }
  return -EINVAL;
}

void ntfs_umount(ntfs_vol_t *v) {
  if (!v) return;
  if (v->rw_ready) bdev_flush(v->dev);
  ntfs_unmark_for_umount(v);                      /* (app) the dirty flag of the writer cleared */
  attr_free(&v->cbmp);
  bdev_close(v->dev);
  attr_free(&v->mft);
  free(v->upcase);
  pthread_mutex_destroy(&v->lock);
  free(v);
}

void ntfs_get_info(ntfs_vol_t *v, ntfs_info_t *info) {
  memset(info, 0, sizeof *info);
  snprintf(info->label, sizeof info->label, "%s", v->label);
  info->serial = v->serial;
  info->total_bytes = v->total_clusters * v->cluster;
  info->cluster_size = v->cluster;
  info->mft_record_size = v->rec_size;
  info->ver_major = v->ver_major;
  info->ver_minor = v->ver_minor;
  info->dirty = v->dirty;
}

int ntfs_free_bytes(ntfs_vol_t *v, uint64_t *out) {
  pthread_mutex_lock(&v->lock);
  if (v->free_known) {
    *out = v->free_clusters * v->cluster;
    pthread_mutex_unlock(&v->lock);
    return 0;
  }
  attr_t bm;
  int rc = attr_find(v, INODE_BITMAP, AT_DATA, NULL, 0, &bm);
  pthread_mutex_unlock(&v->lock);
  if (rc) return rc;

  /* $Bitmap can be over 100 MB on big drives: take the lock per chunk so
   * browsing stays responsive while this runs. */
  enum { CHUNK = 1024 * 1024 };
  uint8_t *buf = malloc(CHUNK);
  uint64_t bits = v->total_clusters, used = 0;
  uint64_t bytes = (bits + 7) / 8;
  if (!buf) rc = -ENOMEM;
  for (uint64_t off = 0; !rc && off < bytes; off += CHUNK) {
    size_t n = bytes - off < CHUNK ? (size_t)(bytes - off) : CHUNK;
    pthread_mutex_lock(&v->lock);
    ssize_t got = attr_pread(v, &bm, off, buf, n);
    pthread_mutex_unlock(&v->lock);
    if (got != (ssize_t)n) { rc = -EIO; break; }
    for (size_t i = 0; i < n; i++) {
      uint8_t byte = buf[i];
      uint64_t bitpos = (off + i) * 8;
      if (bitpos + 8 > bits) byte &= (uint8_t)((1u << (bits - bitpos)) - 1);
      used += (uint64_t)__builtin_popcount(byte);
    }
  }
  free(buf);
  attr_free(&bm);
  if (rc) return rc;
  pthread_mutex_lock(&v->lock);
  v->free_clusters = bits - used;
  v->free_known = 1;
  *out = v->free_clusters * v->cluster;
  pthread_mutex_unlock(&v->lock);
  return 0;
}
