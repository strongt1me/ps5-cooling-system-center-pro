/* LZ4 asset packs of a game: the packer, the checker, the unpacker. See lz4pack.h.
 *
 * Format (drakmor's ampr_pack_format.py, little endian):
 *   manifest "AMPRPAK4": header 128 bytes, then the file records (48 bytes each, one per AMPRIDX3 file id, packed or loose),
 *     the chunk records (12 bytes: 48-bit offset + 16-bit pack id, 20-bit stored size - 1 + codec + flags), the pack records
 *     (32 bytes) and the string table (paths and volume names, each with a NUL). The raw size of a chunk is not stored: it is
 *     the block size, the last chunk of a file the rest.
 *   volume "AMPRDAT3": header 64 bytes, padding to the payload alignment, the payload (independent raw-LZ4 or RAW blocks),
 *     padding to a multiple of the I/O page size.
 *   sidecar "AMPRCRC1": header 48 bytes and one CRC-32 of the decoded bytes per chunk, for offline checks only.
 * Placement: random/mixed layouts keep every chunk of at most one I/O page inside one page (and start larger ones page-
 * aligned); streaming extents start page-aligned and stay dense. The runtime refuses a set that breaks these rules, so the
 * writer and the checker both enforce them. */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <sched.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#ifndef PS5TM_HOST_TEST
#include <sys/rtprio.h>
#include <sys/syscall.h>
#endif

#include "lz4pack.h"
#include "sha256.h"
#include "third_party/libdeflate/libdeflate.h"
#include "third_party/lz4/lz4.h"
#include "third_party/lz4/lz4hc.h"

#define INDEX_HEADER_SIZE  128u
#define DATA_HEADER_SIZE   64u
#define CRC_HEADER_SIZE    48u
#define FILE_REC           48u
#define CHUNK_REC          12u
#define PACK_REC           32u
#define ENDIAN_MARKER      0x01020304u
#define MAX_FILES          2000000u
#define MAX_CHUNKS         16000000u
#define MAX_PACKS          1024u
#define OFFSET_MASK        ((1ull << 48) - 1)
#define MAX_BLOCK          (1u << 20)
#define MIN_BLOCK_SHIFT    14u
#define MAX_BLOCK_SHIFT    20u

#define FF_PACKED          (1u << 0)
#define FF_STORE_ONLY      (1u << 1)
#define FF_STREAMING       (1u << 2)
#define FF_HOT             (1u << 3)
#define FF_RANDOM          (1u << 4)
#define FF_KNOWN           (FF_PACKED | FF_STORE_ONLY | FF_STREAMING | FF_HOT | FF_RANDOM)
#define CF_SHARED          (1u << 0)
#define CF_STREAMING       (1u << 1)
#define CF_PAGE_CONTAINED  (1u << 2)
#define CF_PAGE_ALIGNED    (1u << 3)
#define CF_KNOWN           (CF_SHARED | CF_STREAMING | CF_PAGE_CONTAINED | CF_PAGE_ALIGNED)
#define CODEC_RAW          0u
#define CODEC_LZ4          1u
#define PF_STRIPED         (1u << 0)
#define PF_IO_PAGE_LAYOUT  (1u << 1)
#define PF_KNOWN           (PF_STRIPED | PF_IO_PAGE_LAYOUT)

static const uint8_t k_index_magic[8] = { 'A', 'M', 'P', 'R', 'P', 'A', 'K', '4' };
static const uint8_t k_data_magic[8]  = { 'A', 'M', 'P', 'R', 'D', 'A', 'T', '3' };
static const uint8_t k_crc_magic[8]   = { 'A', 'M', 'P', 'R', 'C', 'R', 'C', '1' };

/* ---------------------------------------------------------------- small helpers */

static void
say(char *err, size_t n, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
static void
say(char *err, size_t n, const char *fmt, ...) {
  if(!err || !n) return;
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(err, n, fmt, ap);
  va_end(ap);
}

static void p32(uint8_t *b, size_t o, uint32_t v) { for(int i = 0; i < 4; i++) b[o + i] = (uint8_t)(v >> (8 * i)); }
static void p64(uint8_t *b, size_t o, uint64_t v) { for(int i = 0; i < 8; i++) b[o + i] = (uint8_t)(v >> (8 * i)); }
static uint32_t g32(const uint8_t *b, size_t o) { uint32_t v = 0; for(int i = 3; i >= 0; i--) v = v << 8 | b[o + i]; return v; }
static uint64_t g64(const uint8_t *b, size_t o) { uint64_t v = 0; for(int i = 7; i >= 0; i--) v = v << 8 | b[o + i]; return v; }

static uint64_t align_up(uint64_t v, uint64_t a) { return (v + a - 1) & ~(a - 1); }
static uint64_t align_down(uint64_t v, uint64_t a) { return v & ~(a - 1); }
static uint32_t crc32_of(const void *d, size_t n) { return libdeflate_crc32(0, d, n); }

static int
cancelled(const lzp_ui_t *ui) {
  return ui && ui->cancel && __atomic_load_n(ui->cancel, __ATOMIC_ACQUIRE);
}

static void ui_phase(const lzp_ui_t *ui, const char *t) { if(ui && ui->phase) ui->phase(ui->ctx, t); }
static void ui_total(const lzp_ui_t *ui, uint64_t b) { if(ui && ui->total) ui->total(ui->ctx, b); }
static void ui_work(const lzp_ui_t *ui, uint64_t b) { if(ui && ui->work && b) ui->work(ui->ctx, b); }
static void ui_files(const lzp_ui_t *ui, unsigned d, unsigned t) { if(ui && ui->files) ui->files(ui->ctx, d, t); }
static void ui_current(const lzp_ui_t *ui, const char *p) { if(ui && ui->current) ui->current(ui->ctx, p); }

/* FNV-1a 64 of the path with ASCII A-Z folded and "\" as "/", 0 becoming 1: the key AMPRIDX3 and the manifest use */
static uint64_t
path_hash(const char *abs_path) {
  uint64_t h = 0xCBF29CE484222325ull;
  for(const unsigned char *p = (const unsigned char *)abs_path; *p; p++) {
    unsigned c = *p == '\\' ? '/' : *p;
    if(c >= 'A' && c <= 'Z') c += 0x20;
    h ^= c;
    h *= 0x100000001B3ull;
  }
  return h ? h : 1;
}

/* a relative path of the game: no leading slash, no empty, "." or ".." component, no backslash */
static int
safe_rel(const char *rel) {
  if(!rel[0] || rel[0] == '/' || strchr(rel, '\\')) return 0;
  const char *p = rel;
  while(*p) {
    const char *e = strchr(p, '/');
    size_t l = e ? (size_t)(e - p) : strlen(p);
    if(l == 0 || (l == 1 && p[0] == '.') || (l == 2 && p[0] == '.' && p[1] == '.')) return 0;
    if(!e) break;
    p = e + 1;
  }
  return 1;
}

static int
mkdir_p(const char *path) {
  char buf[PATH_MAX];
  snprintf(buf, sizeof(buf), "%s", path);
  for(char *p = buf + 1; *p; p++) {
    if(*p != '/') continue;
    *p = 0;
    if(mkdir(buf, 0777) == 0) (void)chmod(buf, 0777);
    else if(errno != EEXIST) return errno ? errno : EIO;
    *p = '/';
  }
  if(mkdir(buf, 0777) == 0) (void)chmod(buf, 0777);
  else if(errno != EEXIST) return errno ? errno : EIO;
  return 0;
}

static int
read_all(int fd, void *buf, size_t n, uint64_t off) {
  uint8_t *p = buf;
  while(n) {
    ssize_t r = pread(fd, p, n, (off_t)off);
    if(r < 0 && errno == EINTR) continue;
    if(r <= 0) return -1;
    p += r;
    off += (uint64_t)r;
    n -= (size_t)r;
  }
  return 0;
}

static int
write_all(int fd, const void *buf, size_t n) {
  const uint8_t *p = buf;
  while(n) {
    ssize_t w = write(fd, p, n);
    if(w < 0 && errno == EINTR) continue;
    if(w <= 0) return -1;
    p += w;
    n -= (size_t)w;
  }
  return 0;
}

static int
file_exists(const char *p) {
  struct stat st;
  return lstat(p, &st) == 0;
}

/* ---------------------------------------------------------------- the AMPR index (AMPRIDX3) */

typedef struct {
  char    *path;                 /* "/app0/..." */
  uint64_t size;
  int64_t  mtime;
} ent_t;

static void
ents_free(ent_t *e, size_t n) {
  (void)n;
  free(e);
}

/* all paths live in one blob that stays allocated: ent_t.path points into it */
typedef struct {
  ent_t  *e;
  size_t  n;
  uint8_t *blob;
} ents_t;

static void
ents_release(ents_t *x) {
  ents_free(x->e, x->n);
  free(x->blob);
  memset(x, 0, sizeof(*x));
}

static int
read_ampridx3(const char *file, ents_t *out, char *err, size_t en) {
  memset(out, 0, sizeof(*out));
  int fd = open(file, O_RDONLY);
  if(fd < 0) { say(err, en, "Der AMPR-Index (ampr_emu.index) fehlt: Bitte zuerst bauen."); return -1; }
  struct stat st;
  if(fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 48 || st.st_size > (off_t)(1u << 30)) { close(fd); say(err, en, "Der AMPR-Index ist ungültig."); return -1; }
  uint8_t *b = malloc((size_t)st.st_size);
  if(!b) { close(fd); say(err, en, "Kein Speicher."); return -1; }
  if(read_all(fd, b, (size_t)st.st_size, 0) != 0) { close(fd); free(b); say(err, en, "Der AMPR-Index ließ sich nicht lesen."); return -1; }
  close(fd);
  size_t len = (size_t)st.st_size;
  if(memcmp(b, "AMPRIDX3", 8) || g32(b, 8) != 3 || g32(b, 12) != 24) { free(b); say(err, en, "Der AMPR-Index hat ein unbekanntes Format."); return -1; }
  uint64_t count = g64(b, 16), path_bytes = g64(b, 24);
  if(count > MAX_FILES || 48 + count * 24 + path_bytes > len) { free(b); say(err, en, "Der AMPR-Index ist abgeschnitten."); return -1; }
  ent_t *e = calloc(count ? count : 1, sizeof(ent_t));
  if(!e) { free(b); say(err, en, "Kein Speicher."); return -1; }
  size_t blob = 48 + (size_t)count * 24;
  for(uint64_t i = 0; i < count; i++) {
    size_t r = 48 + (size_t)i * 24;
    uint32_t at = g32(b, r), pl = g32(b, r + 4);
    if((uint64_t)at + pl >= path_bytes || b[blob + at + pl] != 0 || pl < 6 || memcmp(b + blob + at, "/app0/", 6)) {
      free(e); free(b);
      say(err, en, "Der AMPR-Index enthält einen ungültigen Pfad (Nr. %llu).", (unsigned long long)i);
      return -1;
    }
    e[i].path = (char *)b + blob + at;
    e[i].size = g64(b, r + 8);
    e[i].mtime = (int64_t)g64(b, r + 16);
  }
  out->e = e;
  out->n = (size_t)count;
  out->blob = b;
  return 0;
}

/* ---------------------------------------------------------------- the manifest in memory */

typedef struct {
  uint64_t path_hash, logical;
  int64_t  mtime;
  uint32_t first, count, path_off, path_len, flags;
  uint8_t  shift, pclass;
  uint16_t reserved;
} frec_t;

typedef struct {
  uint64_t payload_bytes, file_size;
  uint32_t name_off, name_len, flags, io_page;
} prec_t;

typedef struct {
  uint8_t  build[16];
  uint32_t flags;
  frec_t  *f;   size_t nf;
  uint64_t *loc;  uint32_t *desc;  size_t nc;     /* the chunk records: location (offset | pack << 48), descriptor */
  prec_t  *p;   size_t np;
  char    *str; size_t nstr;
} man_t;

static void
man_free(man_t *m) {
  free(m->f); free(m->loc); free(m->desc); free(m->p); free(m->str);
  memset(m, 0, sizeof(*m));
}

static uint64_t c_off(const man_t *m, size_t i) { return m->loc[i] & OFFSET_MASK; }
static unsigned c_pack(const man_t *m, size_t i) { return (unsigned)(m->loc[i] >> 48); }
static uint32_t c_stored(const man_t *m, size_t i) { return (m->desc[i] & 0xFFFFFu) + 1; }
static unsigned c_codec(const man_t *m, size_t i) { return (m->desc[i] >> 20) & 3u; }
static unsigned c_flags(const man_t *m, size_t i) { return (m->desc[i] >> 22) & 0xFFu; }

/* the raw size of the local-th chunk of a packed file */
static uint32_t
raw_of(const frec_t *f, uint32_t local) {
  uint64_t bs = 1ull << f->shift, begin = (uint64_t)local * bs;
  uint64_t rest = f->logical > begin ? f->logical - begin : 0;
  return (uint32_t)(rest < bs ? rest : bs);
}

static const char *
man_string(const man_t *m, uint32_t off, uint32_t len) {
  if((uint64_t)off + len >= m->nstr || m->str[off + len] != 0) return NULL;
  return m->str + off;
}

/* serializes the manifest; the caller owns the result */
static uint8_t *
man_serialize(const man_t *m, size_t *out_len) {
  size_t files_off = INDEX_HEADER_SIZE, chunks_off = files_off + m->nf * FILE_REC, packs_off = chunks_off + m->nc * CHUNK_REC;
  size_t strings_off = packs_off + m->np * PACK_REC, total = strings_off + m->nstr;
  uint8_t *b = calloc(1, total);
  if(!b) return NULL;
  for(size_t i = 0; i < m->nf; i++) {
    const frec_t *f = &m->f[i];
    uint8_t *r = b + files_off + i * FILE_REC;
    p64(r, 0, f->path_hash); p64(r, 8, f->logical); p64(r, 16, (uint64_t)f->mtime);
    p32(r, 24, f->first); p32(r, 28, f->count); p32(r, 32, f->path_off); p32(r, 36, f->path_len); p32(r, 40, f->flags);
    r[44] = f->shift; r[45] = f->pclass; r[46] = (uint8_t)f->reserved; r[47] = (uint8_t)(f->reserved >> 8);
  }
  for(size_t i = 0; i < m->nc; i++) {
    uint8_t *r = b + chunks_off + i * CHUNK_REC;
    p64(r, 0, m->loc[i]);
    p32(r, 8, m->desc[i]);
  }
  for(size_t i = 0; i < m->np; i++) {
    const prec_t *p = &m->p[i];
    uint8_t *r = b + packs_off + i * PACK_REC;
    p64(r, 0, p->payload_bytes); p64(r, 8, p->file_size);
    p32(r, 16, p->name_off); p32(r, 20, p->name_len); p32(r, 24, p->flags); p32(r, 28, p->io_page);
  }
  if(m->nstr) memcpy(b + strings_off, m->str, m->nstr);
  uint32_t payload_crc = crc32_of(b + INDEX_HEADER_SIZE, total - INDEX_HEADER_SIZE);
  memcpy(b, k_index_magic, 8);
  p32(b, 8, 4); p32(b, 12, INDEX_HEADER_SIZE); p32(b, 16, m->flags); p32(b, 20, ENDIAN_MARKER);
  memcpy(b + 24, m->build, 16);
  p64(b, 40, m->nf); p64(b, 48, m->nc);
  p32(b, 56, (uint32_t)m->np); p32(b, 60, FILE_REC); p32(b, 64, CHUNK_REC); p32(b, 68, PACK_REC);
  p64(b, 72, files_off); p64(b, 80, chunks_off); p64(b, 88, packs_off); p64(b, 96, strings_off); p64(b, 104, m->nstr);
  p32(b, 112, payload_crc); p32(b, 116, 0); p64(b, 120, 0);
  p32(b, 116, crc32_of(b, INDEX_HEADER_SIZE));
  *out_len = total;
  return b;
}

static int
page_ok(uint32_t io) {
  return io >= 4096 && io <= (1u << 20) && !(io & (io - 1)) && !(io % 64);
}

/* Reads and validates a manifest (the checks of ampr_pack_format.load_manifest/validate_manifest that matter for
   reading it back safely). heavy == 0 skips the per-chunk placement checks. */
static int
man_load(const char *path, man_t *m, int heavy, char *err, size_t en) {
  memset(m, 0, sizeof(*m));
  int fd = open(path, O_RDONLY);
  if(fd < 0) { say(err, en, "Das Paketverzeichnis (%s) fehlt.", LZP_INDEX_NAME); return -1; }
  struct stat st;
  if(fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < (off_t)INDEX_HEADER_SIZE || st.st_size > (off_t)(2000ll << 20)) { close(fd); say(err, en, "Das Paketverzeichnis ist ungültig."); return -1; }
  size_t len = (size_t)st.st_size;
  uint8_t *b = malloc(len);
  if(!b) { close(fd); say(err, en, "Kein Speicher."); return -1; }
  if(read_all(fd, b, len, 0) != 0) { close(fd); free(b); say(err, en, "Das Paketverzeichnis ließ sich nicht lesen."); return -1; }
  close(fd);
#define BADM(msg) do { free(b); man_free(m); say(err, en, "Das Paketverzeichnis ist ungültig: %s.", msg); return -1; } while(0)
  if(memcmp(b, k_index_magic, 8) || g32(b, 8) != 4) BADM("kein AMPRPAK4");
  if(g32(b, 12) != INDEX_HEADER_SIZE || g32(b, 20) != ENDIAN_MARKER || g32(b, 16) != 0 || g64(b, 120) != 0) BADM("Kopf");
  if(g32(b, 60) != FILE_REC || g32(b, 64) != CHUNK_REC || g32(b, 68) != PACK_REC) BADM("Satzgrößen");
  uint32_t header_crc = g32(b, 116);
  uint8_t hz[INDEX_HEADER_SIZE];
  memcpy(hz, b, INDEX_HEADER_SIZE);
  p32(hz, 116, 0);
  if(crc32_of(hz, INDEX_HEADER_SIZE) != header_crc) BADM("Kopf-Prüfsumme");
  if(crc32_of(b + INDEX_HEADER_SIZE, len - INDEX_HEADER_SIZE) != g32(b, 112)) BADM("Prüfsumme der Daten");
  uint64_t nf = g64(b, 40), nc = g64(b, 48);
  uint32_t np = g32(b, 56);
  if(nf > MAX_FILES || nc > MAX_CHUNKS || np > MAX_PACKS) BADM("zu viele Einträge");
  uint64_t files_off = INDEX_HEADER_SIZE, chunks_off = files_off + nf * FILE_REC, packs_off = chunks_off + nc * CHUNK_REC;
  uint64_t strings_off = packs_off + (uint64_t)np * PACK_REC;
  if(g64(b, 72) != files_off || g64(b, 80) != chunks_off || g64(b, 88) != packs_off || g64(b, 96) != strings_off || strings_off + g64(b, 104) != len) BADM("Aufbau");
  memcpy(m->build, b + 24, 16);
  m->nf = (size_t)nf; m->nc = (size_t)nc; m->np = np; m->nstr = (size_t)g64(b, 104);
  m->f = calloc(nf ? nf : 1, sizeof(frec_t));
  m->loc = calloc(nc ? nc : 1, sizeof(uint64_t));
  m->desc = calloc(nc ? nc : 1, sizeof(uint32_t));
  m->p = calloc(np ? np : 1, sizeof(prec_t));
  m->str = malloc(m->nstr + 1);
  if(!m->f || !m->loc || !m->desc || !m->p || !m->str) BADM("kein Speicher");
  memcpy(m->str, b + strings_off, m->nstr);
  m->str[m->nstr] = 0;
  for(size_t i = 0; i < m->nf; i++) {
    const uint8_t *r = b + files_off + i * FILE_REC;
    frec_t *f = &m->f[i];
    f->path_hash = g64(r, 0); f->logical = g64(r, 8); f->mtime = (int64_t)g64(r, 16);
    f->first = g32(r, 24); f->count = g32(r, 28); f->path_off = g32(r, 32); f->path_len = g32(r, 36); f->flags = g32(r, 40);
    f->shift = r[44]; f->pclass = r[45]; f->reserved = (uint16_t)(r[46] | r[47] << 8);
  }
  for(size_t i = 0; i < m->nc; i++) {
    const uint8_t *r = b + chunks_off + i * CHUNK_REC;
    m->loc[i] = g64(r, 0);
    m->desc[i] = g32(r, 8);
    if(m->desc[i] & ~((0xFFFFFu) | (3u << 20) | (0xFFu << 22))) BADM("reservierte Bits eines Blocks");
  }
  for(size_t i = 0; i < m->np; i++) {
    const uint8_t *r = b + packs_off + i * PACK_REC;
    prec_t *p = &m->p[i];
    p->payload_bytes = g64(r, 0); p->file_size = g64(r, 8);
    p->name_off = g32(r, 16); p->name_len = g32(r, 20); p->flags = g32(r, 24); p->io_page = g32(r, 28);
  }
  free(b);
#undef BADM
#define BAD2(msg) do { man_free(m); say(err, en, "Das Paketverzeichnis ist ungültig: %s.", msg); return -1; } while(0)
  for(size_t i = 0; i < m->np; i++) {
    const prec_t *p = &m->p[i];
    const char *name = man_string(m, p->name_off, p->name_len);
    if(!name || !safe_rel(name) || strchr(name, '/')) BAD2("Name einer Paketdatei");
    if((p->flags & ~PF_KNOWN) || !(p->flags & PF_IO_PAGE_LAYOUT) || !page_ok(p->io_page)) BAD2("Paketdatei");
    if(p->file_size < DATA_HEADER_SIZE || p->payload_bytes > p->file_size) BAD2("Größe einer Paketdatei");
    uint64_t poff = p->file_size - p->payload_bytes;
    if(poff < DATA_HEADER_SIZE || poff % p->io_page || p->file_size % p->io_page) BAD2("Nutzdatenbereich einer Paketdatei");
  }
  for(size_t id = 0; id < m->nf; id++) {
    const frec_t *f = &m->f[id];
    const char *path = man_string(m, f->path_off, f->path_len);
    if(!path || strncmp(path, "/app0/", 6) || !safe_rel(path + 6)) BAD2("Pfad einer Datei");
    if(f->reserved || (f->flags & ~FF_KNOWN)) BAD2("Kennzeichen einer Datei");
    if(path_hash(path) != f->path_hash) BAD2("Pfad-Hash");
    if(!(f->flags & FF_PACKED)) {
      if(f->first || f->count || f->shift || f->pclass || f->flags) BAD2("lose Datei mit Blöcken");
      continue;
    }
    if(f->shift < MIN_BLOCK_SHIFT || f->shift > MAX_BLOCK_SHIFT) BAD2("Blockgröße");
    if((f->flags & FF_STREAMING) && (f->flags & FF_RANDOM)) BAD2("Streaming und Zufallszugriff");
    if((uint64_t)f->first + f->count > m->nc) BAD2("Blockbereich");
    uint64_t bs = 1ull << f->shift, total = 0;
    for(uint32_t k = 0; k < f->count; k++) {
      size_t ci = f->first + k;
      uint32_t raw = raw_of(f, k), stored = c_stored(m, ci);
      if(raw == 0 || raw > bs || stored == 0 || stored > bs) BAD2("Blockgrößen");
      if(k + 1 != f->count && raw != bs) BAD2("kurzer Block mitten in der Datei");
      unsigned codec = c_codec(m, ci);
      if(codec > CODEC_LZ4) BAD2("unbekannter Block-Codec");
      if(codec == CODEC_RAW && stored != raw) BAD2("RAW-Blockgröße");
      unsigned pk = c_pack(m, ci);
      if(pk >= m->np) BAD2("Paketnummer");
      unsigned fl = c_flags(m, ci);
      if(fl & ~CF_KNOWN) BAD2("Blockkennzeichen");
      if(((f->flags & FF_STREAMING) != 0) != ((fl & CF_STREAMING) != 0)) BAD2("Streaming-Kennzeichen");
      if((f->flags & FF_STORE_ONLY) && codec != CODEC_RAW) BAD2("komprimierter Block in einer unkomprimierten Datei");
      const prec_t *p = &m->p[pk];
      uint64_t poff = p->file_size - p->payload_bytes, off = c_off(m, ci);
      if(off < poff || off + stored > p->file_size || off % 64) BAD2("Lage eines Blocks");
      if(heavy) {
        uint64_t page = p->io_page, pb = align_down(off, page), pe = align_up(off + stored, page);
        int contained = (fl & CF_PAGE_CONTAINED) != 0, aligned = (fl & CF_PAGE_ALIGNED) != 0;
        if(contained && (stored > page || pb != align_down(off + stored - 1, page))) BAD2("Block über eine Seitengrenze");
        if(aligned && off % page) BAD2("Block nicht seitenbündig");
        int safe = contained || aligned;
        if(stored <= page && aligned && !contained) BAD2("kleiner seitenbündiger Block ohne Kennzeichen");
        if((f->flags & FF_RANDOM) && !safe) BAD2("Zufallszugriff ohne seitensicheren Block");
        if(!(f->flags & FF_STREAMING) && !safe) BAD2("Block nicht seitensicher");
        if(pb < poff || pe > p->file_size) BAD2("Seitenbereich außerhalb");
      }
      total += raw;
    }
    if(total != f->logical) BAD2("Dateigröße");
  }
#undef BAD2
  return 0;
}

/* ---------------------------------------------------------------- the CRC sidecar */

static int
crc_load(const char *path, const man_t *m, uint32_t **out, char *err, size_t en) {
  *out = NULL;
  int fd = open(path, O_RDONLY);
  if(fd < 0) return 1;                 /* missing: not an error for the callers that can do without it */
  struct stat st;
  if(fstat(fd, &st) != 0 || st.st_size < (off_t)CRC_HEADER_SIZE || (uint64_t)st.st_size != CRC_HEADER_SIZE + (uint64_t)m->nc * 4) {
    close(fd);
    say(err, en, "Die Prüfsummen-Datei (.crc) passt nicht zu den Paketen.");
    return -1;
  }
  size_t len = (size_t)st.st_size;
  uint8_t *b = malloc(len);
  if(!b || read_all(fd, b, len, 0) != 0) { close(fd); free(b); say(err, en, "Die Prüfsummen-Datei ließ sich nicht lesen."); return -1; }
  close(fd);
  uint8_t hz[CRC_HEADER_SIZE];
  memcpy(hz, b, CRC_HEADER_SIZE);
  p32(hz, 44, 0);
  if(memcmp(b, k_crc_magic, 8) || g32(b, 8) != 1 || g32(b, 12) != CRC_HEADER_SIZE || memcmp(b + 16, m->build, 16) || g64(b, 32) != m->nc ||
     crc32_of(hz, CRC_HEADER_SIZE) != g32(b, 44) || crc32_of(b + CRC_HEADER_SIZE, len - CRC_HEADER_SIZE) != g32(b, 40)) {
    free(b);
    say(err, en, "Die Prüfsummen-Datei (.crc) gehört nicht zu diesen Paketen oder ist beschädigt.");
    return -1;
  }
  uint32_t *v = malloc((m->nc ? m->nc : 1) * sizeof(uint32_t));
  if(!v) { free(b); say(err, en, "Kein Speicher."); return -1; }
  for(size_t i = 0; i < m->nc; i++) v[i] = g32(b, CRC_HEADER_SIZE + i * 4);
  free(b);
  *out = v;
  return 0;
}

/* ---------------------------------------------------------------- opening the volumes of a set */

typedef struct {
  int       *fd;
  unsigned   n;
  uint64_t  *begin, *end;        /* payload range of each */
} vols_t;

static void
vols_close(vols_t *v) {
  for(unsigned i = 0; i < v->n; i++) if(v->fd && v->fd[i] >= 0) close(v->fd[i]);
  free(v->fd); free(v->begin); free(v->end);
  memset(v, 0, sizeof(*v));
}

static int
vols_open(const char *dir, const man_t *m, vols_t *v, char *err, size_t en) {
  memset(v, 0, sizeof(*v));
  v->n = (unsigned)m->np;
  v->fd = malloc((m->np ? m->np : 1) * sizeof(int));
  v->begin = calloc(m->np ? m->np : 1, sizeof(uint64_t));
  v->end = calloc(m->np ? m->np : 1, sizeof(uint64_t));
  if(!v->fd || !v->begin || !v->end) { vols_close(v); say(err, en, "Kein Speicher."); return -1; }
  for(unsigned i = 0; i < v->n; i++) v->fd[i] = -1;
  for(unsigned i = 0; i < v->n; i++) {
    const prec_t *p = &m->p[i];
    const char *name = man_string(m, p->name_off, p->name_len);
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    int fd = open(path, O_RDONLY);
    if(fd < 0) { vols_close(v); say(err, en, "Die Paketdatei %s fehlt.", name); return -1; }
    v->fd[i] = fd;
    struct stat st;
    uint8_t h[DATA_HEADER_SIZE];
    if(fstat(fd, &st) != 0 || (uint64_t)st.st_size != p->file_size) { vols_close(v); say(err, en, "Die Paketdatei %s hat nicht die richtige Größe.", name); return -1; }
    if(read_all(fd, h, sizeof(h), 0) != 0) { vols_close(v); say(err, en, "Die Paketdatei %s ließ sich nicht lesen.", name); return -1; }
    uint8_t hz[DATA_HEADER_SIZE];
    memcpy(hz, h, sizeof(hz));
    p32(hz, 56, 0);
    if(memcmp(h, k_data_magic, 8) || g32(h, 8) != 3 || g32(h, 12) != DATA_HEADER_SIZE || g32(h, 60) != 0 || (g32(h, 20) & ~PF_KNOWN) ||
       crc32_of(hz, sizeof(hz)) != g32(h, 56)) { vols_close(v); say(err, en, "Der Kopf der Paketdatei %s ist ungültig.", name); return -1; }
    if(g32(h, 16) != i || memcmp(h + 24, m->build, 16) || g32(h, 20) != p->flags) { vols_close(v); say(err, en, "Die Paketdatei %s gehört nicht zu diesem Verzeichnis (andere Fassung).", name); return -1; }
    uint64_t poff = g64(h, 40), pbytes = g64(h, 48);
    if(pbytes != p->payload_bytes || poff + pbytes != p->file_size) { vols_close(v); say(err, en, "Die Nutzdaten der Paketdatei %s passen nicht zum Verzeichnis.", name); return -1; }
    v->begin[i] = poff;
    v->end[i] = poff + pbytes;
  }
  return 0;
}

/* One chunk: read, decoded, size- and CRC-checked into raw (MAX_BLOCK bytes). stored is scratch of MAX_BLOCK bytes. */
static int
chunk_read(const man_t *m, const vols_t *v, const uint32_t *crcs, size_t ci, uint32_t raw_size, uint8_t *stored, uint8_t *raw, char *err, size_t en) {
  unsigned pk = c_pack(m, ci);
  uint64_t off = c_off(m, ci);
  uint32_t ss = c_stored(m, ci);
  if(pk >= v->n || off < v->begin[pk] || off + ss > v->end[pk]) { say(err, en, "Ein Block liegt außerhalb seiner Paketdatei."); return -1; }
  if(read_all(v->fd[pk], stored, ss, off) != 0) { say(err, en, "Ein Block ist abgeschnitten (Paketdatei zu kurz)."); return -1; }
  if(c_codec(m, ci) == CODEC_RAW) {
    if(ss != raw_size) { say(err, en, "Ein RAW-Block hat die falsche Größe."); return -1; }
    memcpy(raw, stored, ss);
  } else {
    int r = LZ4_decompress_safe((const char *)stored, (char *)raw, (int)ss, (int)raw_size);
    if(r < 0 || (uint32_t)r != raw_size) { say(err, en, "Ein LZ4-Block ließ sich nicht entpacken."); return -1; }
  }
  if(crcs && crc32_of(raw, raw_size) != crcs[ci]) { say(err, en, "Die Prüfsumme eines Blocks stimmt nicht."); return -1; }
  return 0;
}

#include "lz4pack_write.inc"
#include "lz4pack_read.inc"
