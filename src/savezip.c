/* PS5 Cooling & System Center - Pro: ZIP files for carrying a save to another console (savezip.h). */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "savezip.h"
#include "third_party/libdeflate/libdeflate.h"

#define SZ_BUF        (1u << 20)
#define SZ_LIMIT      0xFFFFFFF0ull                 /* one byte below what 32 bits hold, with room for the directory */
#define SZ_CD_MAX     (16u << 20)

#define SIG_LOCAL     0x04034b50u
#define SIG_CENTRAL   0x02014b50u
#define SIG_END       0x06054b50u

static void
put16(uint8_t *p, uint16_t v) {
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
}

static void
put32(uint8_t *p, uint32_t v) {
  put16(p, (uint16_t)v);
  put16(p + 2, (uint16_t)(v >> 16));
}

static uint16_t
get16(const uint8_t *p) {
  return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t
get32(const uint8_t *p) {
  return (uint32_t)get16(p) | ((uint32_t)get16(p + 2) << 16);
}

int
savezip_name_ok(const char *name) {
  if(!name || !name[0]) return 0;
  const size_t n = strlen(name);
  if(n > SZ_MAX_NAME || name[0] == '/') return 0;
  if(n >= 2 && name[1] == ':') return 0;                                  /* a drive letter */
  for(size_t i = 0; i < n; i++) {
    const unsigned char c = (unsigned char)name[i];
    if(c < 0x20 || c == 0x7f || c == '\\') return 0;
  }
  const char *p = name;
  while(*p) {                                                             /* no "." or ".." part, no empty part */
    const char *e = strchr(p, '/');
    const size_t l = e ? (size_t)(e - p) : strlen(p);
    if(l == 0 && e && e[1]) return 0;                                    /* "a//b" */
    if((l == 1 && p[0] == '.') || (l == 2 && p[0] == '.' && p[1] == '.')) return 0;
    if(!e) break;
    p = e + 1;
  }
  return 1;
}

static void
dostime(time_t t, uint16_t *time_out, uint16_t *date_out) {
  struct tm tmv;
  time_t tt = t > 0 ? t : 0;
  if(!gmtime_r(&tt, &tmv) || tmv.tm_year < 80) {                          /* before 1980: the earliest the format can say */
    *time_out = 0;
    *date_out = (uint16_t)((0 << 9) | (1 << 5) | 1);
    return;
  }
  *time_out = (uint16_t)((tmv.tm_hour << 11) | (tmv.tm_min << 5) | (tmv.tm_sec / 2));
  *date_out = (uint16_t)(((tmv.tm_year - 80) << 9) | ((tmv.tm_mon + 1) << 5) | tmv.tm_mday);
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
read_full(int fd, uint8_t *buf, size_t n, off_t at) {
  size_t got = 0;
  while(got < n) {
    ssize_t r = pread(fd, buf + got, n - got, at + (off_t)got);
    if(r < 0 && errno == EINTR) continue;
    if(r <= 0) return -1;
    got += (size_t)r;
  }
  return 0;
}

/* ------------------------------------------------------------------ writer */

typedef struct {
  char    *name;
  uint32_t crc, size, lho, attr;
  uint16_t time, date;
} wentry_t;

struct szw {
  int       fd;
  char     *path;
  uint64_t  off;
  wentry_t *e;
  unsigned  n, cap;
  uint8_t  *buf;
};

szw_t *
szw_create(const char *path) {
  szw_t *z = calloc(1, sizeof(*z));
  if(!z) return NULL;
  z->fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
  if(z->fd < 0) { free(z); return NULL; }
  z->path = strdup(path);
  z->buf = malloc(SZ_BUF);
  if(!z->path || !z->buf) {
    close(z->fd);
    unlink(path);
    free(z->path);
    free(z->buf);
    free(z);
    errno = ENOMEM;
    return NULL;
  }
  return z;
}

static void
wfree(szw_t *z) {
  for(unsigned i = 0; i < z->n; i++) free(z->e[i].name);
  free(z->e);
  free(z->buf);
  free(z->path);
  free(z);
}

void
szw_abort(szw_t *z) {
  if(!z) return;
  close(z->fd);
  unlink(z->path);
  wfree(z);
}

uint64_t szw_size(const szw_t *z) { return z ? z->off : 0; }
unsigned szw_count(const szw_t *z) { return z ? z->n : 0; }

/* The local header and the entry in the list; the data follows (the caller writes it). is_dir: the name gets its closing slash. */
static int
begin_entry(szw_t *z, const char *name, int is_dir, uint32_t crc, uint64_t size, time_t mtime, unsigned mode) {
  if(!savezip_name_ok(name)) return SZ_E_NAME;
  char full[SZ_MAX_NAME + 2];
  snprintf(full, sizeof(full), "%s%s", name, is_dir ? "/" : "");
  const size_t nl = strlen(full);
  if(z->n >= SZ_MAX_ENTRIES || size >= SZ_LIMIT || z->off + 30 + nl + size >= SZ_LIMIT) return SZ_E_TOO_BIG;
  if(z->n == z->cap) {
    unsigned nc = z->cap ? z->cap * 2 : 64;
    wentry_t *ne = realloc(z->e, nc * sizeof(*ne));
    if(!ne) return SZ_E_NOMEM;
    z->e = ne;
    z->cap = nc;
  }
  wentry_t *e = &z->e[z->n];
  memset(e, 0, sizeof(*e));
  e->name = strdup(full);
  if(!e->name) return SZ_E_NOMEM;
  e->crc = crc;
  e->size = (uint32_t)size;
  e->lho = (uint32_t)z->off;
  e->attr = ((uint32_t)(mode & 07777) << 16) | (uint32_t)(is_dir ? 0x10 : 0) | (uint32_t)(is_dir ? (040000u << 16) : (0100000u << 16));
  dostime(mtime, &e->time, &e->date);
  uint8_t h[30];
  memset(h, 0, sizeof(h));
  put32(h, SIG_LOCAL);
  put16(h + 4, 10);                                                       /* version needed: stored */
  put16(h + 6, 0x0800);                                                   /* UTF-8 names */
  put16(h + 8, 0);                                                        /* method: stored */
  put16(h + 10, e->time);
  put16(h + 12, e->date);
  put32(h + 14, crc);
  put32(h + 18, (uint32_t)size);
  put32(h + 22, (uint32_t)size);
  put16(h + 26, (uint16_t)nl);
  if(write_all(z->fd, h, sizeof(h)) != 0 || write_all(z->fd, full, nl) != 0) { free(e->name); e->name = NULL; return SZ_E_IO; }
  z->off += 30 + nl;
  z->n++;
  return SZ_OK;
}

int
szw_add_dir(szw_t *z, const char *name, time_t mtime, unsigned mode) {
  return begin_entry(z, name, 1, 0, 0, mtime, mode);
}

int
szw_add_mem(szw_t *z, const char *name, const void *data, size_t len, time_t mtime, unsigned mode) {
  const uint32_t crc = libdeflate_crc32(0, data, len);
  int rc = begin_entry(z, name, 0, crc, len, mtime, mode);
  if(rc != SZ_OK) return rc;
  if(write_all(z->fd, data, len) != 0) return SZ_E_IO;
  z->off += len;
  return SZ_OK;
}

int
szw_add_file(szw_t *z, const char *name, const char *src, time_t mtime, unsigned mode, sz_tick_fn tick, void *ctx) {
  int fd = open(src, O_RDONLY);
  if(fd < 0) return SZ_E_IO;
  struct stat st;
  if(fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) { close(fd); return SZ_E_IO; }
  const uint64_t size = (uint64_t)st.st_size;
  uint32_t crc = 0;
  uint64_t done = 0;
  int rc = SZ_OK;
  while(done < size) {                                                    /* first look: the checksum */
    const size_t want = (size - done) < SZ_BUF ? (size_t)(size - done) : SZ_BUF;
    ssize_t r = read(fd, z->buf, want);
    if(r < 0 && errno == EINTR) continue;
    if(r <= 0) { rc = r == 0 ? SZ_E_CHANGED : SZ_E_IO; break; }
    crc = libdeflate_crc32(crc, z->buf, (size_t)r);
    done += (uint64_t)r;
  }
  if(rc == SZ_OK && lseek(fd, 0, SEEK_SET) != 0) rc = SZ_E_IO;
  if(rc == SZ_OK) rc = begin_entry(z, name, 0, crc, size, mtime, mode);
  if(rc != SZ_OK) { close(fd); return rc; }
  uint32_t crc2 = 0;
  done = 0;
  while(done < size) {                                                    /* second look: the data, with the checksum again */
    const size_t want = (size - done) < SZ_BUF ? (size_t)(size - done) : SZ_BUF;
    ssize_t r = read(fd, z->buf, want);
    if(r < 0 && errno == EINTR) continue;
    if(r <= 0) { rc = r == 0 ? SZ_E_CHANGED : SZ_E_IO; break; }
    crc2 = libdeflate_crc32(crc2, z->buf, (size_t)r);
    if(write_all(z->fd, z->buf, (size_t)r) != 0) { rc = SZ_E_IO; break; }
    done += (uint64_t)r;
    z->off += (uint64_t)r;
    if(tick && tick(ctx, (uint64_t)r) != 0) { rc = SZ_E_STOP; break; }
  }
  if(rc == SZ_OK) {                                                       /* it must not have grown or changed between the two looks */
    uint8_t probe;
    if(crc2 != crc || read(fd, &probe, 1) == 1) rc = SZ_E_CHANGED;
  }
  close(fd);
  return rc;
}

int
szw_finish(szw_t *z) {
  if(!z) return SZ_E_IO;
  const uint64_t cd_off = z->off;
  int rc = SZ_OK;
  for(unsigned i = 0; i < z->n && rc == SZ_OK; i++) {
    const wentry_t *e = &z->e[i];
    const size_t nl = strlen(e->name);
    uint8_t h[46];
    memset(h, 0, sizeof(h));
    put32(h, SIG_CENTRAL);
    put16(h + 4, (uint16_t)((3 << 8) | 20));                              /* made by: unix, 2.0 */
    put16(h + 6, 10);
    put16(h + 8, 0x0800);
    put16(h + 10, 0);
    put16(h + 12, e->time);
    put16(h + 14, e->date);
    put32(h + 16, e->crc);
    put32(h + 20, e->size);
    put32(h + 24, e->size);
    put16(h + 28, (uint16_t)nl);
    put32(h + 38, e->attr);
    put32(h + 42, e->lho);
    if(write_all(z->fd, h, sizeof(h)) != 0 || write_all(z->fd, e->name, nl) != 0) rc = SZ_E_IO;
    z->off += sizeof(h) + nl;
    if(z->off >= SZ_LIMIT) rc = SZ_E_TOO_BIG;
  }
  if(rc == SZ_OK) {
    uint8_t t[22];
    memset(t, 0, sizeof(t));
    put32(t, SIG_END);
    put16(t + 8, (uint16_t)z->n);
    put16(t + 10, (uint16_t)z->n);
    put32(t + 12, (uint32_t)(z->off - cd_off));
    put32(t + 16, (uint32_t)cd_off);
    if(write_all(z->fd, t, sizeof(t)) != 0) rc = SZ_E_IO;
  }
  if(rc == SZ_OK && fsync(z->fd) != 0) rc = SZ_E_IO;
  if(close(z->fd) != 0 && rc == SZ_OK) rc = SZ_E_IO;
  if(rc != SZ_OK) unlink(z->path);
  wfree(z);
  return rc;
}

/* ------------------------------------------------------------------ reader */

struct szr {
  int          fd;
  uint64_t     size;
  szr_entry_t *e;
  unsigned     n;
};

void
szr_close(szr_t *z) {
  if(!z) return;
  if(z->fd >= 0) close(z->fd);
  free(z->e);
  free(z);
}

unsigned szr_count(const szr_t *z) { return z ? z->n : 0; }
const szr_entry_t *szr_at(const szr_t *z, unsigned i) { return z && i < z->n ? &z->e[i] : NULL; }

szr_t *
szr_open(const char *path, int *err) {
  int dummy;
  if(!err) err = &dummy;
  *err = SZ_OK;
  szr_t *z = calloc(1, sizeof(*z));
  if(!z) { *err = SZ_E_NOMEM; return NULL; }
  z->fd = open(path, O_RDONLY);
  if(z->fd < 0) { *err = SZ_E_IO; free(z); return NULL; }
  struct stat st;
  if(fstat(z->fd, &st) != 0 || !S_ISREG(st.st_mode)) { *err = SZ_E_IO; szr_close(z); return NULL; }
  z->size = (uint64_t)st.st_size;
  if(z->size < 22) { *err = SZ_E_BAD; szr_close(z); return NULL; }
  if(z->size >= (1ull << 32)) { *err = SZ_E_TOO_BIG; szr_close(z); return NULL; }
  /* the end record: in the last 22 + 65535 bytes */
  const size_t tail_len = z->size < 22 + 65535 ? (size_t)z->size : 22 + 65535;
  uint8_t *tail = malloc(tail_len);
  if(!tail) { *err = SZ_E_NOMEM; szr_close(z); return NULL; }
  if(read_full(z->fd, tail, tail_len, (off_t)(z->size - tail_len)) != 0) { free(tail); *err = SZ_E_IO; szr_close(z); return NULL; }
  long at = -1;
  for(long i = (long)tail_len - 22; i >= 0; i--) {
    if(get32(tail + i) == SIG_END && (size_t)i + 22 + get16(tail + i + 20) == tail_len) { at = i; break; }
  }
  if(at < 0) { free(tail); *err = SZ_E_BAD; szr_close(z); return NULL; }
  const unsigned count = get16(tail + at + 10);
  const uint32_t cd_size = get32(tail + at + 12), cd_off = get32(tail + at + 16);
  const int disk_ok = get16(tail + at + 4) == 0 && get16(tail + at + 6) == 0 && get16(tail + at + 8) == count;
  free(tail);
  if(!disk_ok || count == 0xFFFF || cd_size == 0xFFFFFFFFu || cd_off == 0xFFFFFFFFu) { *err = SZ_E_BAD; szr_close(z); return NULL; }   /* ZIP64 / split */
  if(count > SZ_MAX_ENTRIES || cd_size > SZ_CD_MAX || (uint64_t)cd_off + cd_size > z->size) { *err = SZ_E_BAD; szr_close(z); return NULL; }
  uint8_t *cd = malloc(cd_size ? cd_size : 1);
  z->e = calloc(count ? count : 1, sizeof(*z->e));
  if(!cd || !z->e) { free(cd); *err = SZ_E_NOMEM; szr_close(z); return NULL; }
  if(read_full(z->fd, cd, cd_size, (off_t)cd_off) != 0) { free(cd); *err = SZ_E_IO; szr_close(z); return NULL; }
  size_t p = 0;
  for(unsigned i = 0; i < count; i++) {
    if(p + 46 > cd_size || get32(cd + p) != SIG_CENTRAL) { free(cd); *err = SZ_E_BAD; szr_close(z); return NULL; }
    const uint16_t flags = get16(cd + p + 8), method = get16(cd + p + 10);
    const uint16_t nl = get16(cd + p + 28), xl = get16(cd + p + 30), cl = get16(cd + p + 32);
    szr_entry_t *e = &z->e[i];
    if(p + 46 + nl + xl + cl > cd_size || nl > SZ_MAX_NAME) { free(cd); *err = SZ_E_BAD; szr_close(z); return NULL; }
    if((flags & 1) || (method != 0 && method != 8)) { free(cd); *err = SZ_E_BAD; szr_close(z); return NULL; }   /* encrypted, or a method this reader does not know */
    memcpy(e->name, cd + p + 46, nl);
    e->name[nl] = '\0';
    e->method = method;
    e->crc = get32(cd + p + 16);
    e->csize = get32(cd + p + 20);
    e->usize = get32(cd + p + 24);
    e->attr = get32(cd + p + 38);
    e->lho = get32(cd + p + 42);
    if(e->csize == 0xFFFFFFFFu || e->usize == 0xFFFFFFFFu || e->lho == 0xFFFFFFFFu) { free(cd); *err = SZ_E_BAD; szr_close(z); return NULL; }
    if(nl && e->name[nl - 1] == '/') { e->is_dir = 1; e->name[nl - 1] = '\0'; }
    if(strlen(e->name) != nl - (e->is_dir ? 1u : 0u) || !savezip_name_ok(e->name)) { free(cd); *err = SZ_E_NAME; szr_close(z); return NULL; }
    z->n++;
    p += 46u + nl + xl + cl;
  }
  free(cd);
  return z;
}

/* Where the data of entry e begins (after the local header, whose own name and extra lengths count). */
static int
data_start(const szr_t *z, const szr_entry_t *e, uint64_t *start) {
  uint8_t h[30];
  if((uint64_t)e->lho + 30 > z->size || read_full(z->fd, h, sizeof(h), (off_t)e->lho) != 0) return SZ_E_BAD;
  if(get32(h) != SIG_LOCAL) return SZ_E_BAD;
  const uint64_t s = (uint64_t)e->lho + 30 + get16(h + 26) + get16(h + 28);
  if(s > z->size || e->csize > z->size - s) return SZ_E_BAD;
  *start = s;
  return SZ_OK;
}

int
szr_read(szr_t *z, unsigned i, uint8_t **out, size_t *len, size_t max_len) {
  *out = NULL;
  *len = 0;
  const szr_entry_t *e = szr_at(z, i);
  if(!e || e->is_dir) return SZ_E_BAD;
  if(e->usize > max_len) return SZ_E_TOO_BIG;
  /* the packed size is read into memory whole: a hostile entry with a tiny usize and a huge csize must not make it; deflate can make data a little larger, never much */
  if(e->method == 0 ? e->csize != e->usize : e->csize > e->usize + e->usize / 8 + 1024) return SZ_E_BAD;
  uint64_t start;
  int rc = data_start(z, e, &start);
  if(rc != SZ_OK) return rc;
  uint8_t *raw = malloc(e->csize ? e->csize : 1);
  uint8_t *res = malloc(e->usize ? e->usize : 1);
  if(!raw || !res) { free(raw); free(res); return SZ_E_NOMEM; }
  if(read_full(z->fd, raw, e->csize, (off_t)start) != 0) { free(raw); free(res); return SZ_E_IO; }
  if(e->method == 0) {
    if(e->csize != e->usize) { free(raw); free(res); return SZ_E_BAD; }
    memcpy(res, raw, e->usize);
  } else {
    struct libdeflate_decompressor *d = libdeflate_alloc_decompressor();
    if(!d) { free(raw); free(res); return SZ_E_NOMEM; }
    size_t actual = 0;
    const enum libdeflate_result r = libdeflate_deflate_decompress(d, raw, e->csize, res, e->usize, &actual);
    libdeflate_free_decompressor(d);
    if(r != LIBDEFLATE_SUCCESS || actual != e->usize) { free(raw); free(res); return SZ_E_BAD; }
  }
  free(raw);
  if(libdeflate_crc32(0, res, e->usize) != e->crc) { free(res); return SZ_E_CRC; }
  *out = res;
  *len = (size_t)e->usize;
  return SZ_OK;
}

int
szr_extract(szr_t *z, unsigned i, int fd, size_t max_mem, sz_tick_fn tick, void *ctx) {
  const szr_entry_t *e = szr_at(z, i);
  if(!e || e->is_dir) return SZ_E_BAD;
  if(e->method != 0) {
    uint8_t *data;
    size_t len;
    int rc = szr_read(z, i, &data, &len, max_mem);
    if(rc != SZ_OK) return rc;
    rc = write_all(fd, data, len) == 0 ? SZ_OK : SZ_E_IO;
    free(data);
    if(rc == SZ_OK && tick && tick(ctx, len) != 0) rc = SZ_E_STOP;
    return rc;
  }
  uint64_t start;
  int rc = data_start(z, e, &start);
  if(rc != SZ_OK) return rc;
  if(e->csize != e->usize) return SZ_E_BAD;
  uint8_t *buf = malloc(SZ_BUF);
  if(!buf) return SZ_E_NOMEM;
  uint32_t crc = 0;
  uint64_t done = 0;
  while(done < e->usize) {
    const size_t want = (e->usize - done) < SZ_BUF ? (size_t)(e->usize - done) : SZ_BUF;
    if(read_full(z->fd, buf, want, (off_t)(start + done)) != 0) { rc = SZ_E_IO; break; }
    crc = libdeflate_crc32(crc, buf, want);
    if(write_all(fd, buf, want) != 0) { rc = SZ_E_IO; break; }
    done += want;
    if(tick && tick(ctx, want) != 0) { rc = SZ_E_STOP; break; }
  }
  free(buf);
  if(rc == SZ_OK && crc != e->crc) rc = SZ_E_CRC;
  return rc;
}
