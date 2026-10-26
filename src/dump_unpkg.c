/* SPDX-License-Identifier: GPL-3.0-or-later
 * Taking the sce_sys files out of an installed app.pkg or ac.pkg (23.10.2026).
 *
 * Follows unpkg_ps5() of ps5-app-dumper 2.10 (Copyright (C) 2025 EchoStretch, Copyright (C) 2026 slopmaster33; GPL-3.0-or-later;
 * source/ps5_pkg.c), rewritten for this app. What it does, and no more:
 *   - the package holds a second, unencrypted container ("CNT") at the offset the json next to it gives for its
 *     "<CONTENT-ID>_sc.pkg" piece (app.pkg -> app.json, ac.pkg -> ac.json); without a json the end of the file is searched for
 *     the container's magic (7F 43 4E 54 83), as the dumper does;
 *   - the container's file table (big endian) lists the files; the name table (type 0x0200) names them, and a few types have
 *     names of their own when a name is missing: param.json, icon0.png, pic0.png ...;
 *   - each file is written below sce_sys of the dump. Encrypted or compressed content is not touched, and there is no
 *     decrypting here.
 * What differs from the dumper, on purpose: a file is only written when it is not there yet (the dump takes what the console
 * itself mounts, and the metadata folders first; the package fills what is missing, which is the dumper's result as well, since
 * its later copies replace the package's files); every name is checked (no "..", no absolute path, no characters a drive
 * refuses) and every offset and size against the file and against overflow; files are streamed in pieces instead of being read
 * whole; a file is never opened through a link; and the work stops when the dump is cancelled. */

#include "dump_unpkg.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define SCAN_BUF       (4u * 1024 * 1024)
#define SCAN_TAIL      (500ull * 1024 * 1024)
#define COPY_BUF       (256u * 1024)
#define JSON_MAX       (64u * 1024)
#define NAME_TABLE_MAX (64u * 1024)
#define ENTRIES_MAX    8192u
#define FILE_MAX       (512ull * 1024 * 1024)
#define PS4_FILE_MAX   (100u * 1024 * 1024)
#define ENTRY_SIZE     32u
#define HDR_SIZE       0x20u

static const char k_magic[5] = { 0x7F, 0x43, 0x4E, 0x54, (char)0x83 };

static uint16_t be16(const unsigned char *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static uint32_t be32(const unsigned char *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }

static int
pread_full(int fd, void *buf, size_t len, uint64_t off) {
  unsigned char *p = buf;
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

static const char *
type_name(uint32_t type, int ps4) {
  switch(type) {
    case 0x0400: return "license.dat";
    case 0x0401: return "license.info";
    case 0x0402: return "nptitle.dat";
    case 0x0403: return "npbind.dat";
    case 0x0404: return "selfinfo.dat";
    case 0x0406: return "imageinfo.dat";
    case 0x0407: return "target-deltainfo.dat";
    case 0x0408: return "origin-deltainfo.dat";
    case 0x0409: return "psreserved.dat";
    case 0x1000: return ps4 ? "param.sfo" : "param.json";
    case 0x1001: return "playgo-chunk.dat";
    case 0x1002: return "playgo-chunk.sha";
    case 0x1003: return "playgo-manifest.xml";
    case 0x1004: return "pronunciation.xml";
    case 0x1005: return "pronunciation.sig";
    case 0x1006: return "pic1.png";
    case 0x1007: return "pubtoolinfo.dat";
    case 0x1200: return "icon0.png";
    case 0x1220: return "pic0.png";
    case 0x1240: return "snd0.at9";
    case 0x1260: return "changeinfo/changeinfo.xml";
    case 0x1280: return "icon0.dds";
    case 0x12A0: return "pic0.dds";
    case 0x12C0: return "pic1.dds";
    default: return NULL;
  }
}

/* A PS4 package's names may carry a leading path: the dump wants the bare name below sce_sys (as the dumper's unpkg_ps4 does). */
static void
clean_entry_name(char *name) {
  if(!strncmp(name, "/sce_sys/", 9)) memmove(name, name + 9, strlen(name + 9) + 1);
  else if(!strncmp(name, "sce_sys/", 8)) memmove(name, name + 8, strlen(name + 8) + 1);
  if(!strncmp(name, "/mnt/usb0/", 10)) memmove(name, name + 10, strlen(name + 10) + 1);
  else if(!strncmp(name, "mnt/usb0/", 9)) memmove(name, name + 9, strlen(name + 9) + 1);
  if(name[0] == '/') memmove(name, name + 1, strlen(name));
}

/* A relative path of plain components that a drive of any kind takes. */
static int
name_ok(const char *n) {
  size_t len = strlen(n);
  if(len == 0 || len > 200 || n[0] == '/') return 0;
  size_t comp = 0;
  for(size_t i = 0; i <= len; i++) {
    unsigned char c = (unsigned char)n[i];
    if(c == 0 || c == '/') {
      if(comp == 0) return 0;
      if(comp <= 2 && n[i - comp] == '.' && (comp == 1 || n[i - 1] == '.')) return 0;
      comp = 0;
      continue;
    }
    if(c < 0x20 || c == 0x7F || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') return 0;
    comp++;
  }
  return 1;
}

static void
say(dj_unpkg_cb_t cb, void *ctx, const char *fmt, ...) __attribute__((format(printf, 3, 4)));

static void
say(dj_unpkg_cb_t cb, void *ctx, const char *fmt, ...) {
  if(!cb) return;
  char line[400];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(line, sizeof(line), fmt, ap);
  va_end(ap);
  cb(ctx, NULL, line);
}

static void
current(dj_unpkg_cb_t cb, void *ctx, const char *name) {
  if(cb) cb(ctx, name, NULL);
}

/* ---------------------------------------------------------------- where the container starts */

/* The piece "<...>_sc.pkg" of the package's json: its fileOffset. The keys are looked for inside that piece's braces only. */
static int
json_offset(const char *json_path, uint64_t *off) {
  int fd = open(json_path, O_RDONLY);
  if(fd < 0) return -1;
  char *buf = calloc(1, JSON_MAX + 1);
  if(!buf) { close(fd); return -1; }
  ssize_t r = read(fd, buf, JSON_MAX);
  close(fd);
  if(r <= 0) { free(buf); return -1; }
  buf[r] = 0;
  int rc = -1;
  char *sc = strstr(buf, "_sc.pkg");
  if(sc) {
    char *start = sc;
    while(start > buf && *start != '{') start--;
    char *end = strchr(sc, '}');
    if(end) *end = 0;
    char *k = strstr(start, "\"fileOffset\":");
    if(k) {
      char *e = NULL;
      unsigned long long v = strtoull(k + strlen("\"fileOffset\":"), &e, 10);
      if(e != k + strlen("\"fileOffset\":")) { *off = v; rc = 0; }
    }
  }
  free(buf);
  return rc;
}

static int
tail_scan(int fd, uint64_t fsize, volatile int *cancel, uint64_t *off) {
  uint8_t *buf = malloc(SCAN_BUF);
  if(!buf) return -1;
  uint64_t pos = fsize > SCAN_TAIL ? fsize - SCAN_TAIL : 0;
  int rc = -1;
  while(pos < fsize) {
    if(cancel && *cancel) break;
    size_t want = fsize - pos < SCAN_BUF ? (size_t)(fsize - pos) : SCAN_BUF;
    ssize_t r = pread(fd, buf, want, (off_t)pos);
    if(r < 0 && errno == EINTR) continue;
    if(r <= 0) break;
    for(size_t i = 0; i + sizeof(k_magic) <= (size_t)r; i++) {
      if(buf[i] == (uint8_t)k_magic[0] && memcmp(buf + i, k_magic, sizeof(k_magic)) == 0) {
        *off = pos + i;
        rc = 0;
        goto done;
      }
    }
    if((size_t)r <= sizeof(k_magic) - 1) break;
    pos += (uint64_t)r - (sizeof(k_magic) - 1);
  }
done:
  free(buf);
  return rc;
}

/* ---------------------------------------------------------------- writing one file */

/* Creates the folders of rel below base; every one that exists has to be a folder (no link). */
static int
make_dirs(const char *base, const char *rel) {
  char path[PATH_MAX];
  size_t b = strlen(base);
  if(b >= sizeof(path)) return -1;
  memcpy(path, base, b + 1);
  for(const char *p = rel; *p; p++) {
    if(*p != '/') continue;
    size_t n = (size_t)(p - rel);
    if(b + 1 + n >= sizeof(path)) return -1;
    path[b] = '/';
    memcpy(path + b + 1, rel, n);
    path[b + 1 + n] = 0;
    struct stat st;
    if(lstat(path, &st) == 0) {
      if(!S_ISDIR(st.st_mode)) return -1;
    } else if(mkdir(path, 0777) != 0 && errno != EEXIST) {
      return -1;
    }
  }
  return 0;
}

/* 0 written, 1 there already, -1 failed (errno), -2 cancelled */
static int
write_entry(int fd, uint64_t from, uint32_t size, const char *dst, uint8_t *buf, volatile int *cancel) {
  struct stat st;
  if(lstat(dst, &st) == 0) return 1;
  int out = open(dst, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0777);
  if(out < 0) return errno == EEXIST ? 1 : -1;
  uint32_t left = size;
  uint64_t at = from;
  int rc = 0;
  while(left) {
    if(cancel && *cancel) { rc = -2; break; }
    size_t n = left < COPY_BUF ? left : COPY_BUF;
    if(pread_full(fd, buf, n, at) != 0) { errno = EIO; rc = -1; break; }
    size_t off = 0;
    while(off < n) {
      ssize_t w = write(out, buf + off, n - off);
      if(w < 0 && errno == EINTR) continue;
      if(w <= 0) { rc = -1; break; }
      off += (size_t)w;
    }
    if(rc) break;
    left -= (uint32_t)n;
    at += n;
  }
  int saved = errno;
  if(rc == 0 && fsync(out) != 0) { saved = errno; rc = -1; }
  if(close(out) != 0 && rc == 0) { saved = errno; rc = -1; }
  if(rc) {
    unlink(dst);
    errno = saved;
  }
  return rc;
}

/* ---------------------------------------------------------------- the whole package */

static int
unpkg_core(const char *pkg, const char *sce_dir, const char *defer_suffix, volatile int *cancel, dj_unpkg_cb_t cb, void *ctx,
           dj_unpkg_res_t *res, int ps4) {
  memset(res, 0, sizeof(*res));
  int ret = -1;
  int fd = open(pkg, O_RDONLY);
  uint8_t *entries = NULL, *buf = NULL;
  char *names = NULL;
  size_t names_size = 0;
  if(fd < 0) { snprintf(res->why, sizeof(res->why), "Das Paket ließ sich nicht öffnen (%s).", strerror(errno)); return -1; }
  struct stat fst;
  if(fstat(fd, &fst) != 0 || !S_ISREG(fst.st_mode) || fst.st_size <= 0) {
    snprintf(res->why, sizeof(res->why), "Das Paket ist keine lesbare Datei.");
    goto out;
  }
  uint64_t fsize = (uint64_t)fst.st_size;

  uint64_t cnt = UINT64_MAX;
  size_t pl = strlen(pkg);
  char json[PATH_MAX];
  if(ps4) {                                            /* a PS4 package is the container itself */
    cnt = 0;
    if(fsize < HDR_SIZE) { snprintf(res->why, sizeof(res->why), "Das Paket ist zu klein."); goto out; }
  } else if(pl > 4 && pl + 2 < sizeof(json) && !strcmp(pkg + pl - 4, ".pkg")) {
    snprintf(json, sizeof(json), "%.*s.json", (int)(pl - 4), pkg);
    if(json_offset(json, &cnt) == 0) say(cb, ctx, "Paket-json: Container bei 0x%llX", (unsigned long long)cnt);
    else cnt = UINT64_MAX;
  }
  if(!ps4 && (cnt == UINT64_MAX || cnt > fsize || fsize - cnt < HDR_SIZE)) {
    cnt = UINT64_MAX;
    current(cb, ctx, "Paket durchsuchen");
    say(cb, ctx, "Ohne brauchbare json-Datei: Das Ende des Pakets wird nach dem Container durchsucht.");
    if(tail_scan(fd, fsize, cancel, &cnt) != 0) cnt = UINT64_MAX;
    if(cancel && *cancel) { ret = 1; goto out; }
  }
  if(cnt == UINT64_MAX) { snprintf(res->why, sizeof(res->why), "Der Container im Paket wurde nicht gefunden."); goto out; }

  unsigned char hdr[HDR_SIZE];
  if(pread_full(fd, hdr, sizeof(hdr), cnt) != 0) { snprintf(res->why, sizeof(res->why), "Der Kopf des Containers ließ sich nicht lesen."); goto out; }
  if(memcmp(hdr, k_magic, 4) != 0 && !(hdr[0] == 0x54 && hdr[1] == 0x4E && hdr[2] == 0x43 && hdr[3] == 0x7F)) {
    if(ps4) snprintf(res->why, sizeof(res->why), "Das ist kein PS4-Paket (Kennung fehlt).");
    else snprintf(res->why, sizeof(res->why), "Kein Container im PS5-Format an Stelle 0x%llX.", (unsigned long long)cnt);
    goto out;
  }
  unsigned n_entries = be16(hdr + 0x12);
  uint32_t table_off = be32(hdr + 0x18);
  if(n_entries == 0 || n_entries > ENTRIES_MAX) { snprintf(res->why, sizeof(res->why), "Die Dateitabelle hat %u Einträge.", n_entries); goto out; }
  uint64_t table_at = cnt + table_off;
  if(table_at > fsize || (uint64_t)n_entries * ENTRY_SIZE > fsize - table_at) {
    snprintf(res->why, sizeof(res->why), "Die Dateitabelle liegt außerhalb des Pakets.");
    goto out;
  }
  say(cb, ctx, "Container: %u Einträge, Tabelle bei +0x%X", n_entries, table_off);
  entries = malloc((size_t)n_entries * ENTRY_SIZE);
  buf = malloc(COPY_BUF);
  if(!entries || !buf) { snprintf(res->why, sizeof(res->why), "Kein Speicher."); goto out; }
  if(pread_full(fd, entries, (size_t)n_entries * ENTRY_SIZE, table_at) != 0) {
    snprintf(res->why, sizeof(res->why), "Die Dateitabelle ließ sich nicht lesen.");
    goto out;
  }

  /* the name table: each entry points at its own name by byte offset */
  for(unsigned i = 0; i < n_entries; i++) {
    const unsigned char *e = entries + (size_t)i * ENTRY_SIZE;
    if(be32(e) != 0x0200) continue;
    uint32_t off = be32(e + 16), sz = be32(e + 20);
    if(sz == 0 || sz > NAME_TABLE_MAX || cnt + off > fsize || sz > fsize - (cnt + off)) break;
    names = malloc((size_t)sz + 1);
    if(names && pread_full(fd, names, sz, cnt + off) == 0) {
      names[sz] = 0;
      names_size = sz;
    } else {
      free(names);
      names = NULL;
    }
    break;
  }

  if(mkdir(sce_dir, 0777) != 0 && errno != EEXIST) { snprintf(res->why, sizeof(res->why), "sce_sys ließ sich nicht anlegen (%s).", strerror(errno)); goto out; }

  for(unsigned i = 0; i < n_entries; i++) {
    if(cancel && *cancel) { ret = 1; goto out; }
    const unsigned char *e = entries + (size_t)i * ENTRY_SIZE;
    uint32_t type = be32(e), name_off = be32(e + 4), off = be32(e + 16), sz = be32(e + 20);
    if(sz == 0 || type == 0x0200) continue;
    const char *name = NULL;
    char cleaned[256];
    if(names && name_off < names_size) name = names + name_off;
    if(!name || !name[0]) name = type_name(type, ps4);
    if(!name || !name[0]) continue;
    if(ps4) {
      snprintf(cleaned, sizeof(cleaned), "%s", name);
      clean_entry_name(cleaned);
      if(!cleaned[0]) continue;
      name = cleaned;
      if(sz > PS4_FILE_MAX) {
        res->skipped++;
        say(cb, ctx, "%s übersprungen: zu groß (%u Bytes)", name, sz);
        continue;
      }
    }
    if(!name_ok(name)) {
      res->skipped++;
      say(cb, ctx, "Eintrag %u übersprungen: unbrauchbarer Name", i);
      continue;
    }
    if(cnt + off > fsize || sz > fsize - (cnt + off) || sz > FILE_MAX) {
      res->skipped++;
      say(cb, ctx, "%s übersprungen: liegt außerhalb des Pakets oder ist zu groß (%u Bytes)", name, sz);
      continue;
    }
    char dst[PATH_MAX];
    int held = -1;
    if(defer_suffix) {
      if(!strcmp(name, "param.json")) held = 0;
      else if(!strcmp(name, "param.sfo")) held = 1;
    }
    int k = held >= 0 ? snprintf(dst, sizeof(dst), "%s/%s%s", sce_dir, name, defer_suffix) : snprintf(dst, sizeof(dst), "%s/%s", sce_dir, name);
    if(k < 0 || (size_t)k >= sizeof(dst) || make_dirs(sce_dir, name) != 0) {
      res->skipped++;
      say(cb, ctx, "%s übersprungen: der Zielpfad ließ sich nicht anlegen", name);
      continue;
    }
    if(held >= 0) {                                    /* the final name counts as there as well */
      char fin[PATH_MAX];
      struct stat st;
      snprintf(fin, sizeof(fin), "%s/%s", sce_dir, name);
      if(lstat(fin, &st) == 0) { res->existing++; continue; }
    }
    current(cb, ctx, name);
    int r = write_entry(fd, cnt + off, sz, dst, buf, cancel);
    if(r == -2) { ret = 1; goto out; }
    if(r == 1) {
      res->existing++;
      continue;
    }
    if(r < 0) {
      snprintf(res->why, sizeof(res->why), "%s ließ sich nicht schreiben (%s).", name, strerror(errno));
      goto out;
    }
    res->extracted++;
    res->bytes += sz;
    if(held >= 0) res->deferred[held] = 1;
    say(cb, ctx, "Aus dem Paket: %s (%u Bytes)", name, sz);
  }
  ret = 0;

out:
  free(names);
  free(entries);
  free(buf);
  close(fd);
  return ret;
}

int
ps5tm_dump_unpkg(const char *pkg, const char *sce_dir, const char *defer_suffix, volatile int *cancel, dj_unpkg_cb_t cb, void *ctx,
                 dj_unpkg_res_t *res) {
  return unpkg_core(pkg, sce_dir, defer_suffix, cancel, cb, ctx, res, 0);
}

int
ps5tm_dump_unpkg_ps4(const char *pkg, const char *sce_dir, const char *defer_suffix, volatile int *cancel, dj_unpkg_cb_t cb,
                     void *ctx, dj_unpkg_res_t *res) {
  return unpkg_core(pkg, sce_dir, defer_suffix, cancel, cb, ctx, res, 1);
}
