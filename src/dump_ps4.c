/* SPDX-License-Identifier: GPL-3.0-or-later
 * Unpacking the PFS image of a PS4 game (24.10.2026).
 *
 * Follows unpfs() of ps5-app-dumper 2.10 (Copyright (C) 2025 EchoStretch, Copyright (C) 2026 slopmaster33; GPL-3.0-or-later;
 * source/pfs.c) and the PS4 part of ps4_dumper.c (idea of the PS4 layout: ps4-app-dumper by Alazif, Scene-Collective), rewritten
 * for this app. What the dumper does, and no more:
 *   - the console shows a running PS4 title's file system as an image, <TITLE>-app0-nest/pfs_image.dat (and -patch0-nest for the
 *     update), which the dumper reads as a plain, unencrypted PFS: header, a table of 168 byte inodes from block 1 on, directories
 *     as lists of entries (inode, type 2 file / 3 folder, name), a file's data lying in one run from its first block on;
 *   - the root of the image is the super-root; its folders (the "uroot") are written into the dump folder.
 * What differs, on purpose: every number from the image is checked (block size, inode and block numbers, entry sizes, the place
 * and length of a file against the image's size, overflow); names are single plain components (no "/", no "." or "..");
 * a directory that does not end, or nests too deep, stops the walk; the files are written in pieces with a stop between
 * them, are never opened through a link, and report their CRC-32 so that the caller can read them back; sce_sys/param.sfo and
 * param.json can be held back under a temporary name. Nothing is decrypted: what the image holds is what is written. */

#include "dump_ps4.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "third_party/libdeflate/libdeflate.h"

#define PFS_HDR        80u
#define PFS_INODE      168u
#define BUF_SIZE       (1u << 20)
#define MAX_DEPTH      24
#define MAX_ENTRIES    2000000u
#define MIN_BLOCK      4096u
#define MAX_BLOCK      (1u << 20)
#define MAX_NAME       255u

typedef struct {
  int       fd;
  uint64_t  fsize;
  uint32_t  blocksz;
  uint64_t  ndinode, ndinodeblock;
  uint32_t  per_block;
  uint32_t  super_ino;
  int       dry, want_crc;
  const char *defer;
  size_t    root;                 /* length of out_dir */
  volatile int *cancel;
  dj_pfs_progress_cb pcb;
  dj_pfs_file_cb fcb;
  void     *ctx;
  uint8_t  *buf;
  char     *path;
  dj_pfs_stat_t *st;
  unsigned  entries;
  int       cb_rc;
  char      why[200];
} pfs_t;

static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint64_t le64(const uint8_t *p) { return (uint64_t)le32(p) | ((uint64_t)le32(p + 4) << 32); }

static int
pread_full(int fd, void *buf, size_t len, uint64_t off) {
  uint8_t *p = buf;
  while(len) {
    ssize_t r = pread(fd, p, len, (off_t)off);
    if(r < 0 && errno == EINTR) continue;
    if(r <= 0) return -1;
    p += r;
    len -= (size_t)r;
    off += (uint64_t)r;
  }
  return 0;
}

static int
bad(pfs_t *p, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

static int
bad(pfs_t *p, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(p->why, sizeof(p->why), fmt, ap);
  va_end(ap);
  return -1;
}

static int
stopped(const pfs_t *p) {
  return p->cancel && *p->cancel;
}

/* one plain name component */
static int
name_ok(const char *n, size_t len) {
  if(len == 0 || len > MAX_NAME) return 0;
  if(len == 1 && n[0] == '.') return 0;
  if(len == 2 && n[0] == '.' && n[1] == '.') return 0;
  for(size_t i = 0; i < len; i++) {
    unsigned char c = (unsigned char)n[i];
    if(c < 0x20 || c == 0x7F || c == '/' || c == '\\') return 0;
  }
  return 1;
}

static int
read_inode(pfs_t *p, uint64_t ino, uint8_t out[PFS_INODE]) {
  if(ino >= p->ndinode) return -1;
  uint64_t blk = ino / p->per_block;
  if(blk >= p->ndinodeblock) return -1;
  uint64_t off = (uint64_t)p->blocksz * (1 + blk) + (uint64_t)PFS_INODE * (ino % p->per_block);
  if(off + PFS_INODE > p->fsize) return -1;
  return pread_full(p->fd, out, PFS_INODE, off);
}

/* ---------------------------------------------------------------- one file */

/* 0 done, 1 cancelled, -1 failed, -2 the callback refused */
static int
write_file(pfs_t *p, const char *dst, const char *rel, uint64_t start, uint64_t size) {
  if(size && (start > p->fsize || size > p->fsize - start)) return bad(p, "%s liegt außerhalb des Abbilds.", rel);
  struct stat st;
  if(lstat(dst, &st) == 0 && !S_ISREG(st.st_mode)) return bad(p, "%s: Am Ziel liegt schon ein Ordner oder ein Link.", rel);
  int out = open(dst, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0777);
  if(out < 0) return bad(p, "%s ließ sich nicht anlegen (%s).", rel, strerror(errno));
  (void)fchmod(out, 0777);
  if(p->pcb) p->pcb(p->ctx, 0, rel);
  uint64_t done = 0;
  uint32_t crc = 0;
  int rc = 0;
  while(done < size) {
    if(stopped(p)) { rc = 1; break; }
    size_t n = size - done < BUF_SIZE ? (size_t)(size - done) : BUF_SIZE;
    if(pread_full(p->fd, p->buf, n, start + done) != 0) { rc = bad(p, "%s ließ sich im Abbild nicht lesen.", rel); break; }
    if(p->want_crc) crc = libdeflate_crc32(crc, p->buf, n);
    size_t off = 0;
    while(off < n) {
      ssize_t w = write(out, p->buf + off, n - off);
      if(w < 0 && errno == EINTR) continue;
      if(w <= 0) { rc = bad(p, "%s ließ sich nicht schreiben (%s).", rel, strerror(errno ? errno : EIO)); break; }
      off += (size_t)w;
    }
    if(rc) break;
    done += n;
    if(p->pcb) p->pcb(p->ctx, n, NULL);
  }
  int saved = errno;
  if(rc == 0 && fsync(out) != 0) { saved = errno; rc = bad(p, "%s ließ sich nicht sichern (%s).", rel, strerror(saved ? saved : EIO)); }
  if(close(out) != 0 && rc == 0) { saved = errno; rc = bad(p, "%s ließ sich nicht abschließen (%s).", rel, strerror(saved ? saved : EIO)); }
  if(rc) {
    unlink(dst);
    return rc;
  }
  if(p->fcb) {
    int r = p->fcb(p->ctx, dst, rel, size, crc);
    if(r) { p->cb_rc = r; return -2; }
  }
  return 0;
}

/* ---------------------------------------------------------------- the walk */

/* path holds the folder being walked, plen its length. 0 done, 1 cancelled, <0 failed. */
static int
walk_dir(pfs_t *p, uint32_t ino, int level, size_t plen) {
  if(level > MAX_DEPTH) return bad(p, "Die Ordner im Abbild sind zu tief verschachtelt.");
  uint8_t in[PFS_INODE];
  if(read_inode(p, ino, in) != 0) return bad(p, "Ein Verzeichnis (Inode %u) ließ sich im Abbild nicht lesen.", ino);
  uint32_t blocks = le32(in + 96), db0 = le32(in + 100);
  if(blocks == 0) return 0;
  if(blocks > 65536) return bad(p, "Ein Verzeichnis im Abbild ist unplausibel groß (%u Blöcke).", blocks);

  for(uint32_t b = 0; b < blocks; b++) {
    uint64_t bpos = (uint64_t)p->blocksz * ((uint64_t)db0 + b);
    if(bpos > p->fsize || p->blocksz > p->fsize - bpos) return bad(p, "Ein Verzeichnis liegt außerhalb des Abbilds.");
    uint64_t pos = bpos, bend = bpos + p->blocksz;
    while(pos + 16 <= bend) {
      if(stopped(p)) return 1;
      uint8_t eh[16];
      if(pread_full(p->fd, eh, sizeof(eh), pos) != 0) return bad(p, "Ein Verzeichnis ließ sich nicht lesen.");
      uint32_t eino = le32(eh), type = le32(eh + 4), namelen = le32(eh + 8), entsize = le32(eh + 12);
      if(type == 0) break;                                   /* the list of this block ends */
      if(namelen > MAX_NAME || entsize < 16 + namelen || entsize > bend - pos)
        return bad(p, "Ein Verzeichniseintrag im Abbild ist beschädigt.");
      if(++p->entries > MAX_ENTRIES) return bad(p, "Das Abbild hat unplausibel viele Einträge.");

      if(level == 0) {                                       /* the super-root: its folders hold the game, names do not count */
        if(type == 3) {
          int r = walk_dir(p, eino, level + 1, plen);
          if(r) return r;
        }
        pos += entsize;
        continue;
      }
      if(type == 2 || type == 3) {
        char name[MAX_NAME + 1];
        if(pread_full(p->fd, name, namelen, pos + 16) != 0) return bad(p, "Ein Dateiname ließ sich nicht lesen.");
        name[namelen] = 0;
        if(!name_ok(name, namelen)) return bad(p, "Das Abbild enthält einen unbrauchbaren Dateinamen.");
        if(plen + 1 + namelen >= PATH_MAX) return bad(p, "Ein Dateipfad im Abbild ist zu lang.");
        p->path[plen] = '/';
        memcpy(p->path + plen + 1, name, namelen + 1);
        size_t nlen = plen + 1 + namelen;
        unsigned rel_len = (unsigned)(nlen - p->root);
        if(rel_len > p->st->max_rel) p->st->max_rel = rel_len;
        int r = 0;
        if(type == 3) {
          p->st->dirs++;
          if(!p->dry) {
            struct stat st;
            if(lstat(p->path, &st) == 0) {
              if(!S_ISDIR(st.st_mode)) r = bad(p, "%s: Am Ziel liegt schon eine Datei oder ein Link.", p->path + p->root + 1);
            } else if(mkdir(p->path, 0777) != 0) {
              r = bad(p, "%s ließ sich nicht anlegen (%s).", p->path + p->root + 1, strerror(errno ? errno : EIO));
            } else {
              (void)chmod(p->path, 0777);
            }
          }
          if(!r) r = walk_dir(p, eino, level + 1, nlen);
        } else {
          uint8_t fi[PFS_INODE];
          if(read_inode(p, eino, fi) != 0) {
            r = bad(p, "%s: Der Inode ließ sich nicht lesen.", p->path + p->root + 1);
          } else {
            uint64_t size = le64(fi + 8), start = (uint64_t)p->blocksz * le32(fi + 100);
            if(p->dry) {
              p->st->bytes += size;
              p->st->files++;
              if(size > p->st->largest) p->st->largest = size;
            } else {
              const char *rel = p->path + p->root + 1;
              char held[PATH_MAX];
              const char *dst = p->path;
              if(p->defer && (!strcmp(rel, "sce_sys/param.sfo") || !strcmp(rel, "sce_sys/param.json"))) {
                if(snprintf(held, sizeof(held), "%s%s", p->path, p->defer) >= (int)sizeof(held)) {
                  r = bad(p, "Ein Dateipfad im Abbild ist zu lang.");
                } else {
                  dst = held;
                }
              }
              if(!r) r = write_file(p, dst, rel, start, size);
            }
          }
        }
        p->path[plen] = 0;
        if(r) return r;
      }
      pos += entsize;
    }
  }
  return 0;
}

static int
run(const char *img, const char *out_dir, int dry, const char *defer, volatile int *cancel, dj_pfs_progress_cb pcb,
    dj_pfs_file_cb fcb, void *ctx, int want_crc, dj_pfs_stat_t *st, int *cb_rc, char *why, size_t wn) {
  pfs_t *p = calloc(1, sizeof(*p));
  int ret = -1, r;
  size_t ol;
  uint64_t super;
  if(!p) {
    snprintf(why, wn, "Kein Speicher.");
    return -1;
  }
  p->fd = open(img, O_RDONLY);
  if(p->fd < 0) {
    snprintf(why, wn, "Das Abbild ließ sich nicht öffnen (%s).", strerror(errno));
    free(p);
    return -1;
  }
  struct stat fst;
  uint8_t hdr[PFS_HDR];
  if(fstat(p->fd, &fst) != 0 || !S_ISREG(fst.st_mode) || fst.st_size < (off_t)(2 * MIN_BLOCK) || pread_full(p->fd, hdr, sizeof(hdr), 0) != 0) {
    snprintf(why, wn, "Das Abbild ist keine lesbare Datei.");
    goto out;
  }
  p->fsize = (uint64_t)fst.st_size;
  p->blocksz = le32(hdr + 32);
  p->ndinode = le64(hdr + 48);
  p->ndinodeblock = le64(hdr + 64);
  super = le64(hdr + 72);
  if(p->blocksz < MIN_BLOCK || p->blocksz > MAX_BLOCK || (p->blocksz & (p->blocksz - 1)) || p->blocksz < PFS_INODE) {
    snprintf(why, wn, "Das Abbild ist kein PFS-Abbild (Blockgröße %u).", p->blocksz);
    goto out;
  }
  p->per_block = p->blocksz / PFS_INODE;
  if(p->ndinode == 0 || p->ndinode > (1ull << 24) || p->ndinodeblock == 0 || p->ndinodeblock > p->ndinode ||
     p->ndinode > p->ndinodeblock * p->per_block || super >= p->ndinode) {
    snprintf(why, wn, "Der Kopf des Abbilds ist unplausibel (Inodes %llu).", (unsigned long long)p->ndinode);
    goto out;
  }
  p->super_ino = (uint32_t)super;
  p->dry = dry;
  p->want_crc = want_crc;
  p->defer = defer;
  p->cancel = cancel;
  p->pcb = pcb;
  p->fcb = fcb;
  p->ctx = ctx;
  p->st = st;
  p->path = malloc(PATH_MAX);
  p->buf = dry ? NULL : malloc(BUF_SIZE);
  if(!p->path || (!dry && !p->buf)) {
    snprintf(why, wn, "Kein Speicher.");
    goto out;
  }
  ol = strlen(out_dir);
  if(ol == 0 || ol >= PATH_MAX - 2) {
    snprintf(why, wn, "Der Zielpfad ist zu lang.");
    goto out;
  }
  memcpy(p->path, out_dir, ol + 1);
  p->root = ol;
  if(!dry && mkdir(out_dir, 0777) != 0 && errno != EEXIST) {
    snprintf(why, wn, "Der Zielordner ließ sich nicht anlegen (%s).", strerror(errno));
    goto out;
  }
  r = walk_dir(p, p->super_ino, 0, ol);
  if(r == 0) ret = 0;
  else if(r == 1) ret = 1;
  else {
    if(cb_rc) *cb_rc = p->cb_rc;
    snprintf(why, wn, "%s", p->why[0] ? p->why : "Das Abbild ließ sich nicht lesen.");
    ret = r == -2 ? -2 : -1;
  }

out:
  close(p->fd);
  free(p->path);
  free(p->buf);
  free(p);
  return ret;
}

int
ps5tm_dump_pfs_measure(const char *img, volatile int *cancel, dj_pfs_stat_t *st, char *why, size_t wn) {
  memset(st, 0, sizeof(*st));
  why[0] = 0;
  return run(img, "/", 1, NULL, cancel, NULL, NULL, NULL, 0, st, NULL, why, wn);
}

int
ps5tm_dump_pfs_extract(const char *img, const char *out_dir, const char *defer_suffix, volatile int *cancel, dj_pfs_progress_cb pcb,
                       dj_pfs_file_cb fcb, void *ctx, int want_crc, int *cb_rc, char *why, size_t wn) {
  dj_pfs_stat_t st;
  memset(&st, 0, sizeof(st));
  why[0] = 0;
  if(cb_rc) *cb_rc = 0;
  return run(img, out_dir, 0, defer_suffix, cancel, pcb, fcb, ctx, want_crc, &st, cb_rc, why, wn);
}

/* ---------------------------------------------------------------- the trophy id */

int
ps5tm_dump_npwr(const char *npbind, char *out, size_t n) {
  if(!npbind || !out || n < 10) return -1;
  uint8_t raw[4096];
  int fd = open(npbind, O_RDONLY | O_NOFOLLOW);
  if(fd < 0) return -1;
  ssize_t got = read(fd, raw, sizeof(raw));
  close(fd);
  if(got < 9) return -1;
  for(size_t i = 0; i + 9 <= (size_t)got; i++) {
    if(raw[i] != 'N' || raw[i + 1] != 'P' || raw[i + 2] != 'W' || raw[i + 3] != 'R') continue;
    size_t j = 0;
    for(; j < n - 1 && i + j < (size_t)got && isprint(raw[i + j]); j++) out[j] = (char)raw[i + j];
    out[j] = 0;
    if(j >= 9 && strspn(out, "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") == j) return 0;
    return -1;
  }
  return -1;
}
