/* SPDX-License-Identifier: GPL-3.0-or-later
 * Unsigned PS5 executables written as fake-signed SELFs, for converting a game and for packing it into a new folder (07.11.2026).
 *
 * Why: the console's loader refuses a plain ELF (e_type 0xFE10 / 0xFE18), so a game whose executables are such files never
 * starts. ps5-dump-forge 0.5.4 (quer3q, GPL-3.0, crates/ps5-dump-forge-core/src/fself.rs, read on 07.11.2026) writes them as
 * fake-signed SELFs in every conversion and pack job; this is the same, built afresh in C, and the original folder is only read.
 *
 * The file is byte for byte what `make_fself.py --ptype fake` of ampr-emu-ng writes (its defaults: program authority id
 * 0x3100000000000002, program type 1, versions 0, no authentication info): a header, a pair of entries for every copied segment
 * (PT_LOAD, PT_SCE_RELRO, PT_SCE_DYNLIBDATA, PT_SCE_COMMENT: first a meta entry of zero digests, then the segment's bytes), the ELF
 * header and the program headers, the extended info with the SHA-256 of the whole ELF, a zero NPDRM block, zero meta blocks, the
 * footer and the signature, then the segments, then the bytes of PT_SCE_VERSION. test/fsign/run.sh compares the result with the
 * script's.
 *
 * It is not the layout of dump_fself.c (the dump's "as FSELF", taken from the ps5-app-dumper): that one has the other magic and a
 * longer meta area, and it has not been tried on the console. This one is what the community's tools write and what the
 * conversion tool of quer3q has people's games started from.
 *
 * Which files: the extensions .bin (only a name that starts with "eboot"), .elf, .self, .prx and .sprx, of at least 0x40 bytes,
 * whose first bytes are an ELF with e_type 0xFE10 or 0xFE18 (fakelib/ too: the loader loads those as well). An executable with
 * headers that do not fit the script's rules is left as it is and counted (refused). */

#include "fsign.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#include "sha256.h"

#ifndef PATH_MAX
#define PATH_MAX 1024
#endif

#define FS_ET_EXEC_ASLR  0xFE10u
#define FS_ET_DYNAMIC    0xFE18u
#define FS_PT_LOAD       1u
#define FS_PT_DYNLIBDATA 0x61000000u
#define FS_PT_RELRO      0x61000010u
#define FS_PT_COMMENT    0x6FFFFF00u
#define FS_PT_VERSION    0x6FFFFF01u
#define FS_PHDR          0x38u
#define FS_BLOCK         0x4000ull                 /* one zero digest of 32 bytes per block of a segment */
#define FS_FIRST         0x4000u                   /* the first read of an executable: its header and, as built, its program headers */
#define FS_PAID          0x3100000000000002ull
#define FS_PTYPE_FAKE    1ull
#define FS_BUF           (1u << 20)
#define FS_MAX_DEPTH     64
#define FS_INDEX_NAME    "ampr_emu.index"
#define FS_INDEX_MAX     (64ull << 20)

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint64_t rd64(const uint8_t *p) { return (uint64_t)rd32(p) | (uint64_t)rd32(p + 4) << 32; }
static void wr16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void wr32(uint8_t *p, uint32_t v) { for(int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i)); }
static void wr64(uint8_t *p, uint64_t v) { for(int i = 0; i < 8; i++) p[i] = (uint8_t)(v >> (8 * i)); }

static int
cancelled(const int *c) {
  return c && __atomic_load_n(c, __ATOMIC_ACQUIRE);
}

static void __attribute__((format(printf, 3, 4)))
say(char *buf, size_t n, const char *fmt, ...) {
  if(!buf || !n) return;
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, n, fmt, ap);
  va_end(ap);
}

static int
pread_all(int fd, void *buf, size_t n, uint64_t off) {
  size_t got = 0;
  while(got < n) {
    ssize_t r = pread(fd, (char *)buf + got, n - got, (off_t)(off + got));
    if(r < 0) {
      if(errno == EINTR) continue;
      return -1;
    }
    if(r == 0) {
      errno = EIO;                                 /* shorter than it was: the file changed */
      return -1;
    }
    got += (size_t)r;
  }
  return 0;
}

static int
write_all(int fd, const void *buf, size_t n) {
  size_t put = 0;
  while(put < n) {
    ssize_t r = write(fd, (const char *)buf + put, n - put);
    if(r < 0) {
      if(errno == EINTR) continue;
      return -1;
    }
    if(r == 0) {
      errno = EIO;
      return -1;
    }
    put += (size_t)r;
  }
  return 0;
}

/* ------------------------------------------------------------------ which files */

int
ps5tm_fsign_name_ok(const char *rel) {
  const char *name = strrchr(rel, '/');
  name = name ? name + 1 : rel;
  const char *dot = strrchr(name, '.');
  if(!dot) return 0;
  const char *ext = dot + 1;
  if(!strcasecmp(ext, "bin")) return strlen(name) >= 5 && !strncasecmp(name, "eboot", 5);
  return !strcasecmp(ext, "elf") || !strcasecmp(ext, "self") || !strcasecmp(ext, "prx") || !strcasecmp(ext, "sprx");
}

int
ps5tm_fsign_head_unsigned(const uint8_t *h, size_t n) {
  if(n < 0x12 || h[0] != 0x7F || h[1] != 'E' || h[2] != 'L' || h[3] != 'F') return 0;
  uint16_t t = rd16(h + 0x10);
  return t == FS_ET_EXEC_ASLR || t == FS_ET_DYNAMIC;
}

/* ------------------------------------------------------------------ the plan of one SELF, from the headers only */

typedef struct {
  uint64_t at, from, n;                            /* where in the SELF, where in the ELF, how long */
} fs_copy_t;

typedef struct {
  uint64_t   elf_size, size;
  uint8_t   *head;                                 /* the bytes up to the first segment; the digest is put in before writing */
  size_t     head_len, digest_at;
  fs_copy_t *copy;
  unsigned   ncopy;
} fs_plan_t;

static void
fs_plan_free(fs_plan_t *p) {
  free(p->head);
  free(p->copy);
  memset(p, 0, sizeof(*p));
}

static int
copied_type(uint32_t t) {
  return t == FS_PT_LOAD || t == FS_PT_RELRO || t == FS_PT_DYNLIBDATA || t == FS_PT_COMMENT;
}

static uint64_t
align16(uint64_t x) {
  return (x + 15) & ~15ull;
}

/* 0, or -1 with the reason in why. */
static int
fs_plan(int fd, uint64_t elf_size, fs_plan_t *p, char *why, size_t wn) {
  memset(p, 0, sizeof(*p));
  p->elf_size = elf_size;
  int rc = -1;
  uint8_t *first = malloc(FS_FIRST), *table = NULL;
  unsigned *copied = NULL;
  if(!first) { say(why, wn, "kein Speicher"); return -1; }
  size_t flen = elf_size < FS_FIRST ? (size_t)elf_size : FS_FIRST;
  if(pread_all(fd, first, flen, 0) != 0) { say(why, wn, "nicht lesbar"); goto out; }
  if(flen < 0x40 || !ps5tm_fsign_head_unsigned(first, flen)) { say(why, wn, "kein vollständiger PS5-ELF-Kopf"); goto out; }
  /* make_fself's own refusals */
  if(first[4] != 2 || first[5] != 1) { say(why, wn, "kein 64-Bit-ELF in Little Endian"); goto out; }
  if(rd16(first + 0x12) != 0x3E || rd32(first + 0x14) != 1) { say(why, wn, "kein x86-64-ELF der Version 1"); goto out; }
  uint16_t shentsize = rd16(first + 0x3A);
  if(rd16(first + 0x36) != FS_PHDR || !(shentsize == 0 || shentsize == 0x40)) { say(why, wn, "unbekannte Größe der Kopfeinträge"); goto out; }
  uint64_t phoff = rd64(first + 0x20);
  uint64_t ehsize = rd16(first + 0x34);
  unsigned phnum = rd16(first + 0x38);
  uint64_t tbl = (uint64_t)phnum * FS_PHDR;
  if(phnum && phoff < 0x40) { say(why, wn, "ungewöhnliche Lage der Programmköpfe"); goto out; }
  table = malloc(tbl ? (size_t)tbl : 1);
  copied = calloc(phnum ? phnum : 1, sizeof(unsigned));
  if(!table || !copied) { say(why, wn, "kein Speicher"); goto out; }
  if(phnum) {
    if(phoff > elf_size || tbl > elf_size - phoff) { say(why, wn, "Programmköpfe hinter dem Dateiende"); goto out; }
    if(phoff + tbl <= flen) memcpy(table, first + phoff, (size_t)tbl);
    else if(pread_all(fd, table, (size_t)tbl, phoff) != 0) { say(why, wn, "Programmköpfe nicht lesbar"); goto out; }
  }
  unsigned ncopied = 0;
  int ver = -1;
  for(unsigned i = 0; i < phnum; i++) {
    uint32_t t = rd32(table + (size_t)i * FS_PHDR);
    if(copied_type(t)) copied[ncopied++] = i;
    if(t == FS_PT_VERSION) ver = (int)i;           /* the last one, as the script takes it */
  }
  for(unsigned k = 0; k <= ncopied; k++) {         /* every piece that is copied has to lie in the ELF */
    int i = k < ncopied ? (int)copied[k] : ver;
    if(i < 0) continue;
    uint64_t off = rd64(table + (size_t)i * FS_PHDR + 8), fsz = rd64(table + (size_t)i * FS_PHDR + 0x20);
    if(fsz > 0 && (off > elf_size || fsz > elf_size - off)) { say(why, wn, "Abschnitt %d reicht über das Dateiende hinaus", i); goto out; }
  }
  uint64_t n = (uint64_t)ncopied * 2;
  uint64_t span = phoff + tbl;
  if(span < ehsize) span = ehsize;
  if(span > 0xFFFF) { say(why, wn, "Köpfe zu groß für ein SELF"); goto out; }
  uint64_t elf_at = 0x20 + 0x20 * n;
  uint64_t elf_hs = align16(span);
  uint64_t header_size = elf_hs + elf_at + 0x40 + 0x30;
  uint64_t meta_size = 0x50 * n + 0x50 + 0x100;
  if(n > 0xFFFF || header_size > 0xFFFF || meta_size > 0xFFFF) { say(why, wn, "Köpfe zu groß für ein SELF"); goto out; }

  p->head_len = (size_t)(header_size + meta_size);
  p->head = calloc(1, p->head_len);
  p->copy = calloc(ncopied + 1, sizeof(fs_copy_t));
  if(!p->head || !p->copy) { say(why, wn, "kein Speicher"); goto out; }
  uint8_t *head = p->head;

  /* entries: a meta entry of zero digests, then the segment's bytes, each 16-aligned */
  uint64_t offset = header_size + meta_size;
  for(unsigned j = 0; j < ncopied; j++) {
    unsigned i = copied[j], k = 2 * j;
    uint64_t poff = rd64(table + (size_t)i * FS_PHDR + 8), fsz = rd64(table + (size_t)i * FS_PHDR + 0x20);
    uint64_t digests = (fsz / FS_BLOCK + (fsz % FS_BLOCK ? 1 : 0)) * 0x20;
    uint64_t meta = (1ull << 2) | (1ull << 16) | ((uint64_t)(k + 1) << 20);
    uint64_t data = (1ull << 2) | (1ull << 11) | (2ull << 12) | ((uint64_t)i << 20);
    uint8_t *e = head + 0x20 + 0x20 * (size_t)k;
    wr64(e, meta); wr64(e + 8, offset); wr64(e + 16, digests); wr64(e + 24, digests);
    offset = align16(offset + digests);
    e += 0x20;
    wr64(e, data); wr64(e + 8, offset); wr64(e + 16, fsz); wr64(e + 24, fsz);
    p->copy[p->ncopy].at = offset;
    p->copy[p->ncopy].from = poff;
    p->copy[p->ncopy].n = fsz;
    p->ncopy++;
    offset = align16(offset + fsz);
  }
  uint64_t file_size = offset;
  /* the common and the extended header */
  static const uint8_t magic[8] = { 0x4F, 0x15, 0x3D, 0x1D, 0x00, 0x01, 0x01, 0x12 };
  memcpy(head, magic, 8);
  wr32(head + 8, 0x101);
  wr16(head + 0xC, (uint16_t)header_size);
  wr16(head + 0xE, (uint16_t)meta_size);
  wr64(head + 0x10, file_size);
  wr16(head + 0x18, (uint16_t)n);
  wr16(head + 0x1A, 0x22);
  /* the ELF header as the script saves it (e_version in e_ident, the bytes after it zero, no section headers), each program header
     at its offset */
  uint8_t *eh = head + elf_at;
  memcpy(eh, first, 0x40);
  eh[6] = 1;
  memset(eh + 9, 0, 6);
  eh[0x3C] = eh[0x3D] = 0;
  for(unsigned i = 0; i < phnum; i++) memcpy(head + elf_at + phoff + (size_t)i * FS_PHDR, table + (size_t)i * FS_PHDR, FS_PHDR);
  /* the extended info (the digest comes later), the NPDRM block of type 3, the meta blocks, the footer, the signature */
  size_t info = (size_t)(elf_at + elf_hs);
  wr64(head + info, FS_PAID);
  wr64(head + info + 8, FS_PTYPE_FAKE);
  p->digest_at = info + 0x20;
  wr16(head + info + 0x40, 3);
  size_t footer = info + 0x70 + 0x50 * (size_t)n;
  wr32(head + footer + 48, 0x10000);

  p->size = file_size;
  if(ver >= 0) {
    uint64_t voff = rd64(table + (size_t)ver * FS_PHDR + 8), vsz = rd64(table + (size_t)ver * FS_PHDR + 0x20);
    p->copy[p->ncopy].at = file_size;
    p->copy[p->ncopy].from = voff;
    p->copy[p->ncopy].n = vsz;
    p->ncopy++;
    p->size = file_size + vsz;
  }
  rc = 0;
out:
  free(first);
  free(table);
  free(copied);
  if(rc != 0) fs_plan_free(p);
  return rc;
}

/* ------------------------------------------------------------------ the list of a game folder */

typedef struct {
  fsign_list_t *l;
  const int    *cancel;
  char         *path;
  size_t        rootlen;
  int           stop;
} fs_scan_t;

static int
item_cmp(const void *a, const void *b) {
  return strcmp(((const fsign_item_t *)a)->rel, ((const fsign_item_t *)b)->rel);
}

static void
scan_file(fs_scan_t *s, uint64_t size) {
  fsign_list_t *l = s->l;
  const char *rel = s->path + s->rootlen + 1;
  if(l->n >= FSIGN_MAX_ITEMS) { l->capped = 1; return; }
  int fd = open(s->path, O_RDONLY);
  if(fd < 0) return;
  uint8_t h[0x12];
  if(pread_all(fd, h, sizeof(h), 0) == 0 && ps5tm_fsign_head_unsigned(h, sizeof(h))) {
    fs_plan_t p;
    char why[120] = "";
    if(fs_plan(fd, size, &p, why, sizeof(why)) == 0) {
      if(l->n == l->cap) {
        unsigned nc = l->cap ? l->cap * 2 : 16;
        fsign_item_t *ni = realloc(l->it, nc * sizeof(*ni));
        if(ni) { l->it = ni; l->cap = nc; }
      }
      char *r = l->n < l->cap ? strdup(rel) : NULL;
      if(r) {
        l->it[l->n].rel = r;
        l->it[l->n].elf_size = size;
        l->it[l->n].self_size = p.size;
        l->n++;
        l->elf_bytes += size;
        l->self_bytes += p.size;
      }
      fs_plan_free(&p);
    } else {
      if(!l->refused++) {
        snprintf(l->refused_rel, sizeof(l->refused_rel), "%s", rel);
        snprintf(l->refused_why, sizeof(l->refused_why), "%s", why);
      }
    }
  }
  close(fd);
}

static void
scan_dir(fs_scan_t *s, size_t plen, int depth) {
  if(depth > FS_MAX_DEPTH) return;
  DIR *d = opendir(s->path);
  if(!d) return;
  struct dirent *e;
  while(!s->stop && (e = readdir(d))) {
    if(!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
    if(cancelled(s->cancel)) { s->stop = 1; break; }
    size_t n = strlen(e->d_name);
    if(plen + 1 + n >= PATH_MAX - 8) continue;
    s->path[plen] = '/';
    memcpy(s->path + plen + 1, e->d_name, n + 1);
    int cand = ps5tm_fsign_name_ok(e->d_name);
    int dir = 0, file = 0;
    uint64_t size = 0;
    if(e->d_type == DT_DIR) dir = 1;
    else if((e->d_type == DT_REG && cand) || e->d_type == DT_UNKNOWN) {   /* a data file or a link needs no look; links are never followed */
      struct stat st;
      if(lstat(s->path, &st) == 0) {
        if(S_ISDIR(st.st_mode)) dir = 1;
        else if(S_ISREG(st.st_mode) && cand && st.st_size >= 0x40) { file = 1; size = (uint64_t)st.st_size; }
      }
    }
    if(dir) scan_dir(s, plen + 1 + n, depth + 1);
    else if(file) scan_file(s, size);
    s->path[plen] = 0;
  }
  closedir(d);
}

int
ps5tm_fsign_scan(const char *root, const int *cancel, fsign_list_t *out) {
  memset(out, 0, sizeof(*out));
  fs_scan_t s;
  memset(&s, 0, sizeof(s));
  s.l = out;
  s.cancel = cancel;
  s.path = malloc(PATH_MAX);
  if(!s.path) return 0;
  size_t rl = strlen(root);
  while(rl > 1 && root[rl - 1] == '/') rl--;
  if(rl >= PATH_MAX - 16) { free(s.path); return 0; }
  memcpy(s.path, root, rl);
  s.path[rl] = 0;
  s.rootlen = rl;
  scan_dir(&s, rl, 0);
  free(s.path);
  if(s.stop) {
    ps5tm_fsign_free(out);
    return 1;
  }
  if(out->n > 1) qsort(out->it, out->n, sizeof(*out->it), item_cmp);
  return 0;
}

void
ps5tm_fsign_free(fsign_list_t *l) {
  for(unsigned i = 0; i < l->n; i++) free(l->it[i].rel);
  free(l->it);
  memset(l, 0, sizeof(*l));
}

const fsign_item_t *
ps5tm_fsign_find(const fsign_list_t *l, const char *rel) {
  if(!l || !l->n) return NULL;
  fsign_item_t key;
  memset(&key, 0, sizeof(key));
  key.rel = (char *)rel;
  return bsearch(&key, l->it, l->n, sizeof(*l->it), item_cmp);
}

/* ------------------------------------------------------------------ writing one file */

static int
zeros_write(int fd, uint64_t n, uint8_t *zero, const int *cancel) {
  while(n) {
    size_t c = n > FS_BUF ? FS_BUF : (size_t)n;
    if(write_all(fd, zero, c) != 0) return -1;
    n -= c;
    if(cancelled(cancel)) { errno = ECANCELED; return -1; }
  }
  return 0;
}

/* The expected bytes [off, off+n) of the written file are all zero. */
static int
zeros_check(int fd, uint64_t off, uint64_t n, uint8_t *buf, const int *cancel) {
  while(n) {
    size_t c = n > FS_BUF ? FS_BUF : (size_t)n;
    if(pread_all(fd, buf, c, off) != 0) return -1;
    for(size_t i = 0; i < c; i++) if(buf[i]) { errno = EILSEQ; return -1; }
    off += c;
    n -= c;
    if(cancelled(cancel)) { errno = ECANCELED; return -1; }
  }
  return 0;
}

static void
unlink_quiet(const char *p) {
  int e = errno;
  unlink(p);
  errno = e;
}

int
ps5tm_fsign_write(const char *src, const char *dst, const int *cancel, fsign_prog_fn prog, void *ctx, uint64_t *out_size,
                  char *err, size_t en) {
  int rc = -1, in = -1, out = -1, vf = -1, made = 0;
  fs_plan_t p;
  memset(&p, 0, sizeof(p));
  uint8_t *buf = NULL, *buf2 = NULL;
  char why[160] = "";
  struct stat st, vs;
  ps5tm_sha256_t sha;
  uint64_t pos = 0;

  in = open(src, O_RDONLY);
  if(in < 0 || fstat(in, &st) != 0 || !S_ISREG(st.st_mode)) { say(err, en, "Die Programmdatei ließ sich nicht öffnen."); goto out; }
  buf = malloc(FS_BUF);
  buf2 = malloc(FS_BUF);
  if(!buf || !buf2) { say(err, en, "Kein Speicher."); goto out; }
  if(fs_plan(in, (uint64_t)st.st_size, &p, why, sizeof(why)) != 0) { say(err, en, "Die Programmdatei lässt sich nicht signieren: %s.", why); goto out; }

  /* the SHA-256 of the whole ELF stands in front of its segments, so the ELF is read once before it is copied */
  ps5tm_sha256_init(&sha);
  for(uint64_t done = 0; done < p.elf_size; ) {
    size_t c = p.elf_size - done > FS_BUF ? FS_BUF : (size_t)(p.elf_size - done);
    if(pread_all(in, buf, c, done) != 0) { say(err, en, "Die Programmdatei ließ sich nicht lesen (sie hat sich verändert?)."); goto out; }
    ps5tm_sha256_update(&sha, buf, c);
    done += c;
    if(cancelled(cancel)) { rc = 1; goto out; }
  }
  ps5tm_sha256_final(&sha, p.head + p.digest_at);

  out = open(dst, O_WRONLY | O_CREAT | O_EXCL, 0777);
  if(out < 0) { say(err, en, "Die signierte Datei ließ sich nicht anlegen: %s", strerror(errno)); goto out; }
  made = 1;
  (void)fchmod(out, 0777);
  memset(buf2, 0, FS_BUF);                                      /* zeros for the gaps */
  if(write_all(out, p.head, p.head_len) != 0) { say(err, en, "Schreiben schlug fehl: %s", strerror(errno)); goto out; }
  pos = p.head_len;
  for(unsigned k = 0; k < p.ncopy; k++) {
    const fs_copy_t *c = &p.copy[k];
    if(zeros_write(out, c->at - pos, buf2, cancel) != 0) {
      if(errno == ECANCELED) { rc = 1; goto out; }
      say(err, en, "Schreiben schlug fehl: %s", strerror(errno));
      goto out;
    }
    for(uint64_t done = 0; done < c->n; ) {
      size_t m = c->n - done > FS_BUF ? FS_BUF : (size_t)(c->n - done);
      if(pread_all(in, buf, m, c->from + done) != 0) { say(err, en, "Die Programmdatei ließ sich nicht lesen (sie hat sich verändert?)."); goto out; }
      if(write_all(out, buf, m) != 0) { say(err, en, "Schreiben schlug fehl: %s", strerror(errno)); goto out; }
      done += m;
      if(prog) prog(ctx, m);
      if(cancelled(cancel)) { rc = 1; goto out; }
    }
    pos = c->at + c->n;
  }
  if(pos < p.size && zeros_write(out, p.size - pos, buf2, cancel) != 0) {
    if(errno == ECANCELED) { rc = 1; goto out; }
    say(err, en, "Schreiben schlug fehl: %s", strerror(errno));
    goto out;
  }
  {
    int werr = fsync(out) != 0 ? errno : 0;
    if(close(out) != 0 && !werr) werr = errno ? errno : EIO;
    out = -1;
    if(werr) { say(err, en, "Schreiben schlug fehl: %s", strerror(werr)); goto out; }
  }

  /* read back: the file against the plan and the ELF */
  vf = open(dst, O_RDONLY);
  if(vf < 0 || fstat(vf, &vs) != 0 || (uint64_t)vs.st_size != p.size) { say(err, en, "Die signierte Datei ist unvollständig."); goto out; }
  for(uint64_t done = 0; done < p.head_len; ) {
    size_t c = p.head_len - done > FS_BUF ? FS_BUF : (size_t)(p.head_len - done);
    if(pread_all(vf, buf, c, done) != 0 || memcmp(buf, p.head + done, c) != 0) { say(err, en, "Die signierte Datei weicht vom Plan ab (Kopf)."); goto out; }
    done += c;
  }
  pos = p.head_len;
  for(unsigned k = 0; k < p.ncopy; k++) {
    const fs_copy_t *c = &p.copy[k];
    if(zeros_check(vf, pos, c->at - pos, buf, cancel) != 0) {
      if(errno == ECANCELED) { rc = 1; goto out; }
      say(err, en, "Die signierte Datei weicht vom Plan ab (Lücke).");
      goto out;
    }
    for(uint64_t done = 0; done < c->n; ) {
      size_t m = c->n - done > FS_BUF ? FS_BUF : (size_t)(c->n - done);
      if(pread_all(vf, buf, m, c->at + done) != 0 || pread_all(in, buf2, m, c->from + done) != 0 || memcmp(buf, buf2, m) != 0) {
        say(err, en, "Die signierte Datei weicht vom Original ab.");
        goto out;
      }
      done += m;
      if(prog) prog(ctx, m);
      if(cancelled(cancel)) { rc = 1; goto out; }
    }
    pos = c->at + c->n;
  }
  if(pos < p.size && zeros_check(vf, pos, p.size - pos, buf, cancel) != 0) {
    if(errno == ECANCELED) { rc = 1; goto out; }
    say(err, en, "Die signierte Datei weicht vom Plan ab (Ende).");
    goto out;
  }
  if(out_size) *out_size = p.size;
  rc = 0;
out:
  if(in >= 0) close(in);
  if(out >= 0) close(out);
  if(vf >= 0) close(vf);
  if(rc != 0 && made) unlink_quiet(dst);
  fs_plan_free(&p);
  free(buf);
  free(buf2);
  return rc;
}

/* ------------------------------------------------------------------ the AMPR index */

static uint8_t
fold(uint8_t c) {
  if(c == '\\') return '/';
  return (c >= 'A' && c <= 'Z') ? (uint8_t)(c + 0x20) : c;
}

/* the folded form of (a, al) against the NUL-ended folded key b */
static int
key_cmp(const uint8_t *a, size_t al, const char *b) {
  size_t bl = strlen(b);
  size_t m = al < bl ? al : bl;
  for(size_t i = 0; i < m; i++) {
    uint8_t x = fold(a[i]), y = (uint8_t)b[i];
    if(x != y) return x < y ? -1 : 1;
  }
  return al < bl ? -1 : al > bl ? 1 : 0;
}

typedef struct { char *key; const fsign_item_t *it; } fs_key_t;

static int
fs_key_sort(const void *a, const void *b) {
  return strcmp(((const fs_key_t *)a)->key, ((const fs_key_t *)b)->key);
}

int
ps5tm_fsign_fix_index(const char *index_path, const char *out_path, const fsign_list_t *l, unsigned *resized, char *err, size_t en) {
  if(resized) *resized = 0;
  if(!l || !l->n) return 0;
  int fd = open(index_path, O_RDONLY);
  if(fd < 0) return 0;                                         /* no index: nothing to correct */
  struct stat st;
  if(fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 48 || (uint64_t)st.st_size > FS_INDEX_MAX) { close(fd); return 0; }
  size_t size = (size_t)st.st_size;
  uint8_t *b = malloc(size);
  fs_key_t *keys = calloc(l->n, sizeof(*keys));
  int rc = -1;
  unsigned nkeys = 0, changed = 0;
  char tmp[PATH_MAX + 8];
  if(!b || !keys) { say(err, en, "Kein Speicher."); goto out; }
  if(pread_all(fd, b, size, 0) != 0) { say(err, en, "Der AMPR-Index ließ sich nicht lesen."); goto out; }
  if(memcmp(b, "AMPRIDX3", 8) || rd32(b + 12) != 24) { rc = 0; goto out; }       /* not an index we know: left alone */
  uint64_t count = rd64(b + 16), blob = rd64(b + 24);
  if(count > (size - 48) / 24 || blob > size || 48 + count * 24 + blob > size) { rc = 0; goto out; }
  const uint8_t *bp = b + 48 + count * 24;
  for(unsigned i = 0; i < l->n; i++) {
    size_t kl = 6 + strlen(l->it[i].rel);
    char *k = malloc(kl + 1);
    if(!k) { say(err, en, "Kein Speicher."); goto out; }
    memcpy(k, "/app0/", 6);
    for(size_t j = 0; l->it[i].rel[j]; j++) k[6 + j] = (char)fold((uint8_t)l->it[i].rel[j]);
    k[kl] = 0;
    keys[nkeys].key = k;
    keys[nkeys].it = &l->it[i];
    nkeys++;
  }
  qsort(keys, nkeys, sizeof(*keys), fs_key_sort);
  for(uint64_t r = 0; r < count; r++) {
    uint8_t *rec = b + 48 + r * 24;
    uint32_t po = rd32(rec), pl = rd32(rec + 4);
    if((uint64_t)po + pl > blob) continue;
    size_t lo = 0, hi = nkeys;
    while(lo < hi) {                                           /* the runtime folds ASCII case */
      size_t mid = (lo + hi) / 2;
      int c = key_cmp(bp + po, pl, keys[mid].key);
      if(c == 0) {
        if(rd64(rec + 8) == keys[mid].it->elf_size) { wr64(rec + 8, keys[mid].it->self_size); changed++; }
        break;
      }
      if(c < 0) hi = mid; else lo = mid + 1;
    }
  }
  if(!changed) { rc = 0; goto out; }
  if(snprintf(tmp, sizeof(tmp), "%s.tmp", out_path) >= (int)sizeof(tmp)) { say(err, en, "Der Pfad ist zu lang."); goto out; }
  int o = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0777);
  if(o < 0) { say(err, en, "Der korrigierte AMPR-Index ließ sich nicht anlegen: %s", strerror(errno)); goto out; }
  (void)fchmod(o, 0777);
  int werr = write_all(o, b, size) != 0 ? errno : 0;
  if(!werr && fsync(o) != 0) werr = errno;
  if(close(o) != 0 && !werr) werr = errno ? errno : EIO;
  if(!werr && rename(tmp, out_path) != 0) werr = errno;
  if(werr) { unlink(tmp); say(err, en, "Der korrigierte AMPR-Index ließ sich nicht schreiben: %s", strerror(werr)); goto out; }
  if(resized) *resized = changed;
  rc = 0;
out:
  close(fd);
  if(keys) for(unsigned i = 0; i < nkeys; i++) free(keys[i].key);
  free(keys);
  free(b);
  return rc;
}

/* ------------------------------------------------------------------ a folder to lay over the game */

static int
mkdir_parents(char *path, size_t from) {                       /* the folders above the last name of path, from `from` on */
  for(size_t i = from; path[i]; i++) {
    if(path[i] != '/') continue;
    path[i] = 0;
    int r = mkdir(path, 0777);
    int e = errno;
    path[i] = '/';
    if(r != 0 && e != EEXIST) { errno = e; return -1; }
  }
  return 0;
}

int
ps5tm_fsign_stage(const char *root, const char *stage, const fsign_list_t *l, const int *cancel, fsign_prog_fn prog, void *ctx,
                  unsigned *index_resized, char *err, size_t en) {
  if(index_resized) *index_resized = 0;
  if(mkdir(stage, 0777) != 0 && errno != EEXIST) { say(err, en, "Der Ordner für die signierten Dateien ließ sich nicht anlegen: %s", strerror(errno)); return -1; }
  char *src = malloc(PATH_MAX), *dst = malloc(PATH_MAX);
  if(!src || !dst) { free(src); free(dst); say(err, en, "Kein Speicher."); return -1; }
  int rc = 0;
  size_t sl = strlen(stage);
  for(unsigned i = 0; i < l->n && !rc; i++) {
    if(snprintf(src, PATH_MAX, "%s/%s", root, l->it[i].rel) >= PATH_MAX || snprintf(dst, PATH_MAX, "%s/%s", stage, l->it[i].rel) >= PATH_MAX) {
      say(err, en, "Ein Pfad ist zu lang: %s", l->it[i].rel);
      rc = -1;
      break;
    }
    if(mkdir_parents(dst, sl + 1) != 0) { say(err, en, "Ein Ordner ließ sich nicht anlegen (%s): %s", strerror(errno), l->it[i].rel); rc = -1; break; }
    char why[300] = "";
    int w = ps5tm_fsign_write(src, dst, cancel, prog, ctx, NULL, why, sizeof(why));
    if(w == 1) rc = 1;
    else if(w != 0) { say(err, en, "%s: %s", l->it[i].rel, why[0] ? why : "nicht signiert."); rc = -1; }
  }
  if(!rc) {
    if(snprintf(src, PATH_MAX, "%s/" FS_INDEX_NAME, root) < PATH_MAX && snprintf(dst, PATH_MAX, "%s/" FS_INDEX_NAME, stage) < PATH_MAX) {
      char why[300] = "";
      if(ps5tm_fsign_fix_index(src, dst, l, index_resized, why, sizeof(why)) != 0) { say(err, en, "%s", why); rc = -1; }
    }
  }
  free(src);
  free(dst);
  return rc;
}

/* ------------------------------------------------------------------ in place */

int
ps5tm_fsign_replace(const char *root, const fsign_list_t *l, const int *cancel, fsign_prog_fn prog, void *ctx, unsigned *done,
                    unsigned *index_resized, char *err, size_t en) {
  unsigned n = 0;
  if(done) *done = 0;
  if(index_resized) *index_resized = 0;
  char *src = malloc(PATH_MAX), *tmp = malloc(PATH_MAX + 32);
  if(!src || !tmp) { free(src); free(tmp); say(err, en, "Kein Speicher."); return -1; }
  int rc = 0;
  for(unsigned i = 0; i < l->n && !rc; i++) {
    if(snprintf(src, PATH_MAX, "%s/%s", root, l->it[i].rel) >= PATH_MAX || snprintf(tmp, PATH_MAX + 32, "%s" FSIGN_TMP_SUFFIX, src) >= PATH_MAX + 32) {
      say(err, en, "Ein Pfad ist zu lang: %s", l->it[i].rel);
      rc = -1;
      break;
    }
    struct stat st;
    if(lstat(src, &st) != 0 || !S_ISREG(st.st_mode) || (uint64_t)st.st_size != l->it[i].elf_size) {
      say(err, en, "%s: Die Datei hat sich verändert.", l->it[i].rel);
      rc = -1;
      break;
    }
    unlink(tmp);                                                   /* a leftover of an earlier try */
    char why[300] = "";
    int w = ps5tm_fsign_write(src, tmp, cancel, prog, ctx, NULL, why, sizeof(why));
    if(w == 1) { rc = 1; break; }
    if(w != 0) { say(err, en, "%s: %s", l->it[i].rel, why[0] ? why : "nicht signiert."); rc = -1; break; }
    struct stat st2;
    if(lstat(src, &st2) != 0 || st2.st_size != st.st_size || st2.st_mtime != st.st_mtime) {      /* changed while it was signed */
      unlink(tmp);
      say(err, en, "%s: Die Datei hat sich verändert.", l->it[i].rel);
      rc = -1;
      break;
    }
    if(rename(tmp, src) != 0) {
      int e = errno;
      unlink(tmp);
      say(err, en, "%s: Umbenennen gescheitert: %s", l->it[i].rel, strerror(e));
      rc = -1;
      break;
    }
    struct timeval tv[2];
    tv[0].tv_sec = tv[1].tv_sec = st.st_mtime;
    tv[0].tv_usec = tv[1].tv_usec = 0;
    utimes(src, tv);
    n++;
  }
  if(done) *done = n;
  if(n) {                                                          /* the index lists the files done at their SELF sizes */
    fsign_list_t sub;
    memset(&sub, 0, sizeof(sub));
    sub.it = l->it;                                                /* the files done are the first n; sub is never freed */
    sub.n = n;
    char ip[PATH_MAX + 32], why[300] = "";
    if(snprintf(ip, sizeof(ip), "%s/" FS_INDEX_NAME, root) < (int)sizeof(ip) &&
       ps5tm_fsign_fix_index(ip, ip, &sub, index_resized, why, sizeof(why)) != 0 && !rc) {
      say(err, en, "%s", why);
      rc = -1;
    }
  }
  free(src);
  free(tmp);
  return rc;
}

/* ------------------------------------------------------------------ removing it again */

static void
rm_dir(char *path, size_t len, int depth) {
  DIR *d = opendir(path);
  if(!d) return;
  struct dirent *e;
  while((e = readdir(d))) {
    if(!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
    size_t n = strlen(e->d_name);
    if(len + 1 + n >= PATH_MAX - 8) continue;
    path[len] = '/';
    memcpy(path + len + 1, e->d_name, n + 1);
    struct stat st;
    if(lstat(path, &st) == 0) {
      if(S_ISDIR(st.st_mode) && depth < FS_MAX_DEPTH) { rm_dir(path, len + 1 + n, depth + 1); rmdir(path); }
      else if(!S_ISDIR(st.st_mode)) unlink(path);            /* a link is removed, never followed */
    }
    path[len] = 0;
  }
  closedir(d);
}

void
ps5tm_fsign_rmtree(const char *stage) {
  size_t sl = strlen(stage), xl = strlen(FSIGN_STAGE_SUFFIX);
  if(sl <= xl || sl >= PATH_MAX - 16 || strcmp(stage + sl - xl, FSIGN_STAGE_SUFFIX)) return;   /* only ever a folder that is ours */
  struct stat st;
  if(lstat(stage, &st) != 0 || !S_ISDIR(st.st_mode)) return;
  char *p = malloc(PATH_MAX);
  if(!p) return;
  memcpy(p, stage, sl + 1);
  rm_dir(p, sl, 0);
  free(p);
  rmdir(stage);
}
