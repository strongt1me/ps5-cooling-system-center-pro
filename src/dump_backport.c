/* SPDX-License-Identifier: GPL-3.0-or-later
 * Lowering the SDK version of the files in a dump (25.10.2026).
 *
 * Taken over from ps5-app-dumper 2.10 (Copyright (C) 2025 EchoStretch, Copyright (C) 2026 slopmaster33; GPL-3.0-or-later;
 * ps4_backport.c and ps5_backport.c): the two tables of SDK values, which fields of the ELF parameter block and of param.sfo /
 * param.json are lowered, and the rule "only when the value is higher than the target". Rewritten for this app: nothing is
 * mapped into memory (the self pager of dump_self.c is the only place that maps a file), every offset and size read from a file is
 * checked against the file before it is used, links are never followed, and the target is only ever a folder of the dump.
 *
 * What it does, as the dumper does:
 *   - walks the dump folder (not the folder "decrypted", which keeps the plain copies as they were) and lowers the SDK fields
 *     of the plain ELF files (.bin .elf .prx .sprx for a PS4 title, plus .self for a PS5 title). A file that is an FSELF is left
 *     alone: the dumper skips it as well, so with "re-sign as FSELF" only the parameter file is lowered;
 *   - then lowers the SDK values in sce_sys/param.sfo (PS4: PUBTOOLINFO sdk_ver and SYSTEM_VER) or in sce_sys/param.json
 *     (PS5: requiredSystemSoftwareVersion and sdkVersion).
 * A file is only ever changed in the dump; the console's own copy of a game is never opened for writing here. An ELF is patched
 * in place, four bytes at a time, and read back (copying a file of several hundred MB for four bytes would only wear the drive);
 * a parameter file is small and replaces its old self through a temporary name. */

#include "dump_backport.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define BP_DEPTH        24
#define BP_ENTRIES_MAX  400000u
#define BP_SMALL_MAX    (1u << 20)
#define BP_PHDR         0x38u

#define ELF_MAGIC       "\x7F" "ELF"
#define PS4_FSELF_MAGIC "\x4F\x15\x3D\x1D"
#define PS5_FSELF_MAGIC "\x54\x14\xF5\xEE"

#define PT_SCE_PROCPARAM    0x61000001u
#define PT_SCE_MODULE_PARAM 0x61000002u
#define SCE_PROCESS_PARAM_MAGIC 0x4942524Fu
#define SCE_MODULE_PARAM_MAGIC  0x3C13F4BFu

static const uint32_t k_ps4_levels[DJ_BP_PS4_LEVELS] = {
  0x05050001, 0x06008001, 0x07008001, 0x09008001, 0x10008001, 0x11008001,
};

static const struct { uint32_t ps5, ps4; } k_ps5_levels[DJ_BP_PS5_LEVELS] = {
  { 0x01000050, 0x07590001 }, { 0x02000009, 0x08050001 }, { 0x03000027, 0x08540001 }, { 0x04000031, 0x09040001 },
  { 0x05000033, 0x09590001 }, { 0x06000038, 0x10090001 }, { 0x07000038, 0x10590001 }, { 0x08000041, 0x11090001 },
  { 0x09000040, 0x11590001 }, { 0x10000040, 0x12090001 },
};

uint32_t
ps5tm_dump_bp_ps4_target(int level) {
  return level >= 1 && level <= DJ_BP_PS4_LEVELS ? k_ps4_levels[level - 1] : 0;
}

void
ps5tm_dump_bp_ps5_target(int level, uint32_t *ps5_sdk, uint32_t *ps4_sdk) {
  int ok = level >= 1 && level <= DJ_BP_PS5_LEVELS;
  if(ps5_sdk) *ps5_sdk = ok ? k_ps5_levels[level - 1].ps5 : 0;
  if(ps4_sdk) *ps4_sdk = ok ? k_ps5_levels[level - 1].ps4 : 0;
}

/* ---------------------------------------------------------------- small helpers */

static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint64_t le64(const uint8_t *p) { return (uint64_t)le32(p) | ((uint64_t)le32(p + 4) << 32); }
static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }

static int
rd(int fd, void *buf, size_t n, uint64_t off) {
  uint8_t *b = buf;
  while(n) {
    ssize_t r = pread(fd, b, n, (off_t)off);
    if(r < 0 && errno == EINTR) continue;
    if(r <= 0) return -1;
    b += r;
    off += (uint64_t)r;
    n -= (size_t)r;
  }
  return 0;
}

static int
wr(int fd, const void *buf, size_t n, uint64_t off) {
  const uint8_t *b = buf;
  while(n) {
    ssize_t r = pwrite(fd, b, n, (off_t)off);
    if(r < 0 && errno == EINTR) continue;
    if(r <= 0) return -1;
    b += r;
    off += (uint64_t)r;
    n -= (size_t)r;
  }
  return 0;
}

/* a folder of the console's own installation or mounts: never written to, whatever the caller says */
static int
protected_root(const char *root) {
  static const char *const k_no[] = { "/user/app", "/mnt/sandbox", "/app0", "/system", "/preinst", "/mnt/disc", "/dev" };
  for(size_t i = 0; i < sizeof(k_no) / sizeof(k_no[0]); i++) {
    size_t n = strlen(k_no[i]);
    if(!strncmp(root, k_no[i], n) && (root[n] == 0 || root[n] == '/')) return 1;
  }
  return 0;
}

/* ---------------------------------------------------------------- the executables */

enum { EL_UNCHANGED = 0, EL_PATCHED = 1, EL_SIGNED = 2, EL_NOT_ELF = 3, EL_ERROR = -1 };

typedef struct {
  const char *rel;
  char        line[2][200];
  int         nlines;
} el_say_t;

static void
el_note(el_say_t *s, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

static void
el_note(el_say_t *s, const char *fmt, ...) {
  if(s->nlines >= 2) return;
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(s->line[s->nlines], sizeof(s->line[0]), fmt, ap);
  va_end(ap);
  s->nlines++;
}

/* Writes four bytes and reads them back. 0 when the file holds the new value. */
static int
patch32(int fd, uint64_t off, uint32_t val) {
  uint8_t b[4], c[4];
  put32(b, val);
  if(wr(fd, b, 4, off) != 0 || rd(fd, c, 4, off) != 0) return -1;
  return memcmp(b, c, 4) ? -1 : 0;
}

/* One ELF. ps4 = 1: the PS4 parameter block (the PS4 SDK value at +0x10, magic at +8), as ps4_backport.c; ps4 = 0: the PS5
   layout of ps5_backport.c, whose block may start 8 bytes later, with a PS5 value at +0xC and a PS4 value at +8. */
static int
patch_elf(const char *path, int ps4, uint32_t t5, uint32_t t4, el_say_t *say) {
  int fd = open(path, O_RDWR | O_NOFOLLOW);
  if(fd < 0) return EL_ERROR;
  struct stat st;
  if(fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 0x40) {
    close(fd);
    return EL_NOT_ELF;
  }
  const uint64_t size = (uint64_t)st.st_size;
  uint8_t h[0x40];
  if(rd(fd, h, sizeof(h), 0) != 0) {
    close(fd);
    return EL_ERROR;
  }
  if(!memcmp(h, PS4_FSELF_MAGIC, 4) || (!ps4 && !memcmp(h, PS5_FSELF_MAGIC, 4))) {
    close(fd);
    return EL_SIGNED;
  }
  if(memcmp(h, ELF_MAGIC, 4)) {
    close(fd);
    return EL_NOT_ELF;
  }
  const uint64_t phoff = le64(h + 0x20);
  const uint16_t phnum = le16(h + 0x38);
  if(phoff > size || (uint64_t)phnum * BP_PHDR > size - phoff) {
    close(fd);
    return EL_UNCHANGED;
  }
  int patched = 0, err = 0;
  for(uint16_t i = 0; i < phnum && !err; i++) {
    uint8_t ph[16];
    if(rd(fd, ph, sizeof(ph), phoff + (uint64_t)i * BP_PHDR) != 0) {
      err = 1;
      break;
    }
    const uint32_t type = le32(ph);
    const uint64_t poff = le64(ph + 8);
    if(type != PT_SCE_PROCPARAM && type != PT_SCE_MODULE_PARAM) continue;
    if(ps4) {
      if(poff > size || size - poff < 0x30) continue;
      uint8_t p[0x14];
      if(rd(fd, p, sizeof(p), poff) != 0) {
        err = 1;
        break;
      }
      const uint32_t magic = le32(p + 8);
      if(magic != SCE_PROCESS_PARAM_MAGIC && magic != SCE_MODULE_PARAM_MAGIC) continue;
      const uint32_t old = le32(p + 0x10);
      if(old > t4 && old != 0) {
        if(patch32(fd, poff + 0x10, t4) != 0) {
          err = 1;
          break;
        }
        el_note(say, "PS4-SDK 0x%08X -> 0x%08X", old, t4);
        patched = 1;
      }
    } else {
      if(poff > size || size - poff < 0x18) continue;
      const uint32_t want = type == PT_SCE_PROCPARAM ? SCE_PROCESS_PARAM_MAGIC : SCE_MODULE_PARAM_MAGIC;
      uint8_t m[4];
      uint64_t base = poff;
      if(rd(fd, m, 4, base) != 0) {
        err = 1;
        break;
      }
      if(le32(m) != want) {
        base += 8;
        if(rd(fd, m, 4, base) != 0) {
          err = 1;
          break;
        }
        if(le32(m) != want) continue;
      }
      if(size - base < 0x10) continue;
      uint8_t f[8];
      if(rd(fd, f, 8, base + 8) != 0) {
        err = 1;
        break;
      }
      const uint32_t old4 = le32(f);                 /* at +8: the PS4 SDK value */
      const uint32_t old5 = le32(f + 4);             /* at +0xC: the PS5 SDK value */
      if(old5 > t5) {
        if(patch32(fd, base + 0xC, t5) != 0) {
          err = 1;
          break;
        }
        el_note(say, "PS5-SDK 0x%08X -> 0x%08X", old5, t5);
        patched = 1;
      }
      if(old4 > t4) {
        if(patch32(fd, base + 8, t4) != 0) {
          err = 1;
          break;
        }
        el_note(say, "PS4-SDK 0x%08X -> 0x%08X", old4, t4);
        patched = 1;
      }
    }
  }
  if(patched && fsync(fd) != 0) err = 1;
  close(fd);
  if(err) return EL_ERROR;
  return patched ? EL_PATCHED : EL_UNCHANGED;
}

/* ---------------------------------------------------------------- the parameter files */

static char *
read_small(const char *path, size_t *len) {
  int fd = open(path, O_RDONLY | O_NOFOLLOW);
  if(fd < 0) return NULL;
  struct stat st;
  if(fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size <= 0 || st.st_size > (off_t)BP_SMALL_MAX) {
    close(fd);
    return NULL;
  }
  char *b = malloc((size_t)st.st_size + 1);
  if(!b || rd(fd, b, (size_t)st.st_size, 0) != 0) {
    free(b);
    close(fd);
    return NULL;
  }
  close(fd);
  b[st.st_size] = 0;
  *len = (size_t)st.st_size;
  return b;
}

/* writes data through path.bp and renames it over path; 0 on success */
static int
replace_small(const char *path, const void *data, size_t len) {
  char tmp[PATH_MAX + 8];
  if(snprintf(tmp, sizeof(tmp), "%s.bp", path) >= (int)sizeof(tmp)) return -1;
  int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0644);
  if(fd < 0) return -1;
  int rc = wr(fd, data, len, 0);
  if(rc == 0 && fsync(fd) != 0) rc = -1;
  if(close(fd) != 0) rc = -1;
  if(rc != 0 || rename(tmp, path) != 0) {
    unlink(tmp);
    return -1;
  }
  return 0;
}

/* param.sfo of a PS4 title: PUBTOOLINFO "sdk_ver=XXXXXXXX" and SYSTEM_VER. 1 patched, 0 unchanged, -1 error. */
static int
patch_param_sfo(const char *path, uint32_t t4, el_say_t *say) {
  size_t len = 0;
  uint8_t *b = (uint8_t *)read_small(path, &len);
  if(!b) return -1;
  int patched = 0;
  if(len < 0x14 || memcmp(b, "\0PSF", 4)) {
    free(b);
    return 0;
  }
  const uint64_t kt = le32(b + 8), dt = le32(b + 0xC);
  const uint32_t n = le32(b + 0x10);
  if(n > (len - 0x14) / 0x10) {
    free(b);
    return 0;
  }
  for(uint32_t i = 0; i < n; i++) {
    const uint8_t *e = b + 0x14 + (size_t)i * 0x10;
    const uint16_t koff = le16(e), fmt = le16(e + 2);
    const uint32_t dlen = le32(e + 4), doff = le32(e + 0xC);
    if(kt + koff >= len || dt + doff + dlen > len) continue;
    char key[64] = { 0 };
    for(size_t k = 0; k < sizeof(key) - 1 && kt + koff + k < len && b[kt + koff + k]; k++) key[k] = (char)b[kt + koff + k];
    uint8_t *data = b + dt + doff;
    if(!strcmp(key, "PUBTOOLINFO") && fmt == 0x204) {
      char tool[256] = { 0 };
      size_t cl = dlen < sizeof(tool) - 1 ? dlen : sizeof(tool) - 1;
      memcpy(tool, data, cl);
      char *pos = strstr(tool, "sdk_ver=");
      if(!pos || (size_t)(pos - tool) + 16 > cl) continue;
      pos += 8;
      int hex = 1;
      for(int k = 0; k < 8; k++)
        if(!isxdigit((unsigned char)pos[k])) hex = 0;
      if(!hex) continue;
      char eight[9];
      memcpy(eight, pos, 8);
      eight[8] = 0;
      const uint32_t old = (uint32_t)strtoul(eight, NULL, 16);
      if(old > t4 && old != 0) {
        char nw[9];
        snprintf(nw, sizeof(nw), "%08X", t4);
        memcpy(data + (pos - tool), nw, 8);
        patched = 1;
        el_note(say, "param.sfo PUBTOOLINFO sdk_ver 0x%08X -> 0x%08X", old, t4);
      }
    } else if(!strcmp(key, "SYSTEM_VER") && fmt == 0x404 && dlen == 4) {
      const uint32_t old = le32(data);
      if(old > t4 && old != 0) {
        put32(data, t4);
        patched = 1;
        el_note(say, "param.sfo SYSTEM_VER 0x%08X -> 0x%08X", old, t4);
      }
    }
  }
  int rc = 0;
  if(patched && replace_small(path, b, len) != 0) rc = -1;
  free(b);
  return rc ? -1 : patched;
}

/* param.json of a PS5 title: requiredSystemSoftwareVersion and sdkVersion become "0xMM00000000000000". 1, 0 or -1. */
static int
patch_param_json(const char *path, uint32_t t5, el_say_t *say, char *why, size_t why_len) {
  size_t len = 0;
  char *b = read_small(path, &len);
  if(!b) return -1;
  if(len < 100) {
    free(b);
    return 0;
  }
  const uint8_t tmaj = (uint8_t)((t5 >> 24) & 0xFF);
  char major_hex[3];
  snprintf(major_hex, sizeof(major_hex), "%02X", tmaj);
  char value[24];
  snprintf(value, sizeof(value), "\"0x%s00000000000000\"", major_hex);       /* 20 characters with both quotes */
  int patched = 0;
  static const char *const k_keys[] = { "requiredSystemSoftwareVersion", "sdkVersion" };
  char *const end = b + len;
  for(int i = 0; i < 2; i++) {
    char search[64];
    snprintf(search, sizeof(search), "\"%s\"", k_keys[i]);
    char *pos = b;
    while((pos = strstr(pos, search)) != NULL) {
      pos += strlen(search);
      while(pos < end && (*pos == ' ' || *pos == '\t' || *pos == '\n' || *pos == '\r' || *pos == ':')) pos++;
      if(pos >= end || *pos != '"') {
        pos++;
        continue;
      }
      char *qs = pos;
      pos++;
      char *vs = pos;
      while(pos < end && *pos != '"') pos++;
      if(pos >= end) break;
      const size_t vlen = (size_t)(pos - vs);
      char cur[3] = { 0 };
      if(vlen > 4) memcpy(cur, vs + 2, 2);
      const uint8_t cmaj = (uint8_t)strtoul(cur, NULL, 16);
      if(cmaj > tmaj) {
        if(vlen != 18) {
          snprintf(why, why_len, "param.json: %s hat einen unerwarteten Wert und bleibt", k_keys[i]);
          el_note(say, "param.json %s hat einen unerwarteten Wert (Länge %zu) und bleibt", k_keys[i], vlen);
        } else {
          char was[24] = { 0 };
          memcpy(was, vs, vlen);
          memcpy(qs, value, 20);
          patched = 1;
          el_note(say, "param.json %s \"%s\" -> %s", k_keys[i], was, value);
        }
      }
      break;
    }
  }
  int rc = 0;
  if(patched && replace_small(path, b, len) != 0) rc = -1;
  free(b);
  return rc ? -1 : patched;
}

/* ---------------------------------------------------------------- the walk */

typedef struct {
  int           ps4;
  uint32_t      t5, t4;
  volatile int *cancel;
  dj_bp_cb_t    cb;
  void         *cbctx;
  dj_bp_res_t  *res;
  unsigned      done, total, entries;
  int           count_only, stop;     /* stop: 1 cancelled, -1 refused */
  char         *p;                    /* PATH_MAX, the path being walked */
  size_t        rootlen;
} bp_ctx_t;

static void
say_line(bp_ctx_t *c, const char *rel, const char *fmt, ...) __attribute__((format(printf, 3, 4)));

static void
say_line(bp_ctx_t *c, const char *rel, const char *fmt, ...) {
  if(!c->cb) return;
  char line[480];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(line, sizeof(line), fmt, ap);
  va_end(ap);
  c->cb(c->cbctx, rel, c->done, c->total, line);
}

static int
ext_ok(const char *name, int ps4) {
  const char *e = strrchr(name, '.');
  if(!e) return 0;
  if(!strcmp(e, ".bin") || !strcmp(e, ".elf") || !strcmp(e, ".prx") || !strcmp(e, ".sprx")) return 1;
  return !ps4 && !strcmp(e, ".self");
}

static void
one_file(bp_ctx_t *c) {
  const char *rel = c->p + c->rootlen + 1;
  if(c->cb) c->cb(c->cbctx, rel, c->done, c->total, NULL);
  el_say_t say = { .rel = rel };
  int rc = patch_elf(c->p, c->ps4, c->t5, c->t4, &say);
  dj_bp_res_t *r = c->res;
  switch(rc) {
  case EL_PATCHED:
    r->patched++;
    say_line(c, NULL, "Backport: %s: %s%s%s", rel, say.line[0], say.nlines > 1 ? ", " : "", say.nlines > 1 ? say.line[1] : "");
    break;
  case EL_SIGNED:
    r->signed_skip++;
    break;
  case EL_ERROR:
    r->failed++;
    snprintf(r->why, sizeof(r->why), "%s: ließ sich nicht lesen oder schreiben", rel);
    say_line(c, NULL, "Backport: %s ließ sich nicht lesen oder schreiben (%s)", rel, strerror(errno));
    break;
  default:
    r->unchanged++;
    break;
  }
}

static void
walk(bp_ctx_t *c, size_t plen, int depth) {
  if(depth > BP_DEPTH) return;
  DIR *d = opendir(c->p);
  if(!d) return;
  struct dirent *ent;
  while(!c->stop && (ent = readdir(d)) != NULL) {
    const char *name = ent->d_name;
    if(!strcmp(name, ".") || !strcmp(name, "..")) continue;
    if(c->cancel && *c->cancel) {
      c->stop = 1;
      break;
    }
    if(++c->entries > BP_ENTRIES_MAX) {
      c->stop = -1;
      snprintf(c->res->why, sizeof(c->res->why), "zu viele Einträge im Dump");
      break;
    }
    size_t nl = strlen(name);
    if(plen + 1 + nl >= PATH_MAX) continue;
    c->p[plen] = '/';
    memcpy(c->p + plen + 1, name, nl + 1);
    struct stat st;
    if(lstat(c->p, &st) == 0) {
      if(S_ISDIR(st.st_mode)) {
        if(strcmp(name, "decrypted")) walk(c, plen + 1 + nl, depth + 1);
      } else if(S_ISREG(st.st_mode) && ext_ok(name, c->ps4)) {
        if(c->count_only) {
          c->total++;
        } else {
          c->done++;
          one_file(c);
        }
      }
    }
    c->p[plen] = 0;
  }
  closedir(d);
}

int
ps5tm_dump_backport_run(const char *dst_root, int is_ps4, int level, volatile int *cancel, dj_bp_cb_t cb, void *ctx,
                        dj_bp_res_t *res) {
  memset(res, 0, sizeof(*res));
  if(!dst_root || !dst_root[0] || strlen(dst_root) >= PATH_MAX - 2 || dst_root[0] != '/') {
    snprintf(res->why, sizeof(res->why), "Der Pfad ist ungültig.");
    return -1;
  }
  if(protected_root(dst_root)) {
    snprintf(res->why, sizeof(res->why), "Die App schreibt nie in die Spiele-Installation der Konsole.");
    return -1;
  }
  struct stat st;
  if(lstat(dst_root, &st) != 0 || !S_ISDIR(st.st_mode)) {
    snprintf(res->why, sizeof(res->why), "Der Dump-Ordner fehlt.");
    return -1;
  }
  bp_ctx_t c;
  memset(&c, 0, sizeof(c));
  c.ps4 = is_ps4 ? 1 : 0;
  if(c.ps4) {
    if(level < 1) level = 1;
    if(level > DJ_BP_PS4_LEVELS) level = DJ_BP_PS4_LEVELS;
    c.t4 = ps5tm_dump_bp_ps4_target(level);
  } else {
    if(level < 1) level = 1;
    if(level > DJ_BP_PS5_LEVELS) level = DJ_BP_PS5_LEVELS;
    ps5tm_dump_bp_ps5_target(level, &c.t5, &c.t4);
  }
  c.cancel = cancel;
  c.cb = cb;
  c.cbctx = ctx;
  c.res = res;
  c.p = malloc(PATH_MAX);
  if(!c.p) {
    snprintf(res->why, sizeof(res->why), "Kein Speicher.");
    return -1;
  }
  snprintf(c.p, PATH_MAX, "%s", dst_root);
  size_t pl = strlen(c.p);
  while(pl > 1 && c.p[pl - 1] == '/') c.p[--pl] = 0;
  c.rootlen = pl;
  c.count_only = 1;
  walk(&c, pl, 0);
  res->total = c.total;
  c.count_only = 0;
  c.entries = 0;
  c.p[pl] = 0;
  if(!c.stop) walk(&c, pl, 0);
  int rc = 0;
  if(c.stop == 1) rc = 1;
  else if(c.stop == -1) rc = -1;

  if(rc == 0) {
    char path[PATH_MAX];
    el_say_t say = { .rel = "param" };
    int pr = 0;
    if(c.ps4) {
      if(snprintf(path, sizeof(path), "%s/sce_sys/param.sfo", c.p) < (int)sizeof(path)) pr = patch_param_sfo(path, c.t4, &say);
    } else {
      if(snprintf(path, sizeof(path), "%s/sce_sys/param.json", c.p) < (int)sizeof(path))
        pr = patch_param_json(path, c.t5, &say, res->why, sizeof(res->why));
    }
    if(pr == 1) {
      res->param_patched = 1;
      for(int i = 0; i < say.nlines; i++) say_line(&c, NULL, "Backport: %s", say.line[i]);
    } else if(pr < 0 && access(path, F_OK) == 0) {
      res->failed++;
      snprintf(res->why, sizeof(res->why), "%s ließ sich nicht umschreiben", c.ps4 ? "param.sfo" : "param.json");
      say_line(&c, NULL, "Backport: %s ließ sich nicht umschreiben", c.ps4 ? "param.sfo" : "param.json");
    } else if(pr < 0) {
      say_line(&c, NULL, "Backport: %s fehlt, übersprungen", c.ps4 ? "sce_sys/param.sfo" : "sce_sys/param.json");
    }
  }
  free(c.p);
  return rc;
}
