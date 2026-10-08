/* Reading what a PS4 or PS5 package says about itself (04.10.2026).
 *
 * A package on a stick is a "CNT" container (PS4, and PS5 packages that keep it at the start)
 * or a "FIH" wrapper that points at one (PS5). Inside, a table lists entries: the names,
 * param.sfo (PS4) or param.json (PS5), icon0.png, ... This module reads the table and those few
 * entries - a few kilobytes of a file that can be tens of gigabytes - and nothing else.
 * Everything in the file is a number somebody else wrote: every offset and size is checked
 * against the size of the file before it is used, and the sizes it will allocate for are capped.
 *
 * A package split into parts (PS5MPKG1: a 4096-byte header, the icon in the first part, then a
 * slice of the package) is described by that header alone: it carries the title, the content id,
 * the type, the version and where the icon lies, so only the header of a part is read and no
 * other part has to be present.
 *
 * Format knowledge: the public description of the package container (psdevwiki) and the open-source
 * PS5 PKG Manager 1.4.1 by itsPLK (GPL-3.0), named in the README. The code is our own. */

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "pkginst.h"
#include "ps5tm.h"
#include "third_party/cJSON.h"

#define PKG_MAX_ENTRIES 2048
#define PKG_MAX_PARAM   (256u * 1024)       /* param.json / param.sfo larger than this are not read */
#define PKG_MAX_ICON    (10u << 20)
#define PKG_TABLE_MAX   0x200000u           /* the entry table lies within the first 2 MB of the container */

/* The PS5MPKG1 header (little endian), 4096 bytes */
#define MP_HEADER_SIZE   4096
#define MP_OFF_VERSION   8
#define MP_OFF_PART      12
#define MP_OFF_PARTS     16
#define MP_OFF_COMPRESS  20
#define MP_OFF_CHUNK     24
#define MP_OFF_DATA_SIZE 32
#define MP_OFF_TOTAL     40
#define MP_OFF_FILE      56                 /* char[256] */
#define MP_OFF_TITLE_ID  312                /* char[32]  */
#define MP_OFF_NAME      344                /* char[256] */
#define MP_OFF_CONTENT   600                /* char[64]  */
#define MP_OFF_UUID      664                /* uint8[16] */
#define MP_OFF_ICON_OFF  680
#define MP_OFF_ICON_SIZE 684
#define MP_OFF_APP_VER   688                /* char[32]  */
#define MP_OFF_KIND      720                /* char[16]  */
#define MP_OFF_PART_OFF  736
#define MP_OFF_DATA_OFF  744
#define MP_MAX_PARTS     256

static uint32_t be32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint64_t le64(const uint8_t *p) { return (uint64_t)le32(p) | ((uint64_t)le32(p + 4) << 32); }

/* A string out of a fixed field in a file: at most n bytes, cut at the first NUL or control character.
   out has room for n + 1. */
static void
field_text(char *out, const uint8_t *src, size_t n) {
  size_t i = 0;
  for(; i < n && src[i] >= 32 && src[i] != 127; i++) out[i] = (char)src[i];
  out[i] = 0;
}

/* Makes a string that came out of a file or a file name valid UTF-8 (a stray byte becomes '?'), so that it can
   be put into JSON and read back by a browser. */
static void
utf8_clean(char *s) {
  unsigned char *p = (unsigned char *)s;
  while(*p) {
    if(*p < 0x80) { if(*p < 32 || *p == 127) *p = '?'; p++; continue; }
    int len = (*p >= 0xC2 && *p <= 0xDF) ? 2 : (*p >= 0xE0 && *p <= 0xEF) ? 3 : (*p >= 0xF0 && *p <= 0xF4) ? 4 : 0;
    int ok = len > 0;
    for(int i = 1; ok && i < len; i++) if((p[i] & 0xC0) != 0x80) ok = 0;    /* the string's NUL fails this test: no read past it */
    if(ok) p += len; else { *p = '?'; p++; }
  }
}

#define SET(dst, src) snprintf((dst), sizeof(dst), "%s", (src))

/* Does the text `needle` occur in the n bytes at hay? (memmem is not in this system's C library: the program
   would not link.) */
static int
has_text(const char *hay, size_t n, const char *needle) {
  size_t m = strlen(needle);
  if(m == 0 || n < m) return 0;
  for(size_t i = 0; i + m <= n; i++)
    if(hay[i] == needle[0] && !memcmp(hay + i, needle, m)) return 1;
  return 0;
}

/* A string that is a title id of this console's packages: letters and digits, 4 to 12 of them. */
static int
looks_like_title_id(const char *s) {
  size_t n = strlen(s);
  if(n < 4 || n > 12) return 0;
  for(size_t i = 0; i < n; i++) if(!isalnum((unsigned char)s[i])) return 0;
  return 1;
}

/* ------------------------------------------------------------------ names of parts */

/* "Game.pkg.part2", "Game.part2", "Game.pkg.PART2", "Game.part02", "Game.part2.pkg", "part_2.pkg.part":
   is this file name one of a split package's parts, and which? 1 and *part, or 0. The last ".part<digits>"
   of the name counts ("my.part1.pkg.part2" is part 2). */
int
ps5tm_pkg_part_name(const char *file, unsigned *part) {
  if(!file || !file[0] || file[0] == '.') return 0;
  const char *slash = strrchr(file, '/');
  const char *name = slash ? slash + 1 : file;
  if(!name[0]) return 0;
  if(!strncasecmp(name, "part_", 5)) {
    char *end = NULL;
    unsigned long n = strtoul(name + 5, &end, 10);
    if(n > 0 && n <= MP_MAX_PARTS && end && !strcasecmp(end, ".pkg.part")) { if(part) *part = (unsigned)n; return 1; }
  }
  const char *last = NULL;
  for(const char *c = name; (c = strcasestr(c, ".part")) != NULL; c += 5)
    if(c[5] >= '0' && c[5] <= '9') last = c;
  if(!last) return 0;
  char *end = NULL;
  unsigned long n = strtoul(last + 5, &end, 10);
  if(n == 0 || n > MP_MAX_PARTS) return 0;
  if(end && *end && strcasecmp(end, ".pkg")) return 0;
  if(part) *part = (unsigned)n;
  return 1;
}

/* ------------------------------------------------------------------ the titles in param.json / param.sfo */

typedef struct {
  char lang[24];
  char title[160];
} loc_t;

typedef struct {
  char   title_id[24], category[8], version[24], default_lang[24];
  loc_t  loc[32];
  size_t nloc;
  char   global_title[160];
} meta_t;

/* "01.002.003" -> "v1.03", "01.05.000" -> "v1.05", else "v1.2.3" (the same reading of the version as
   the PS5 PKG Manager, so that the two tools agree in what they show). */
static void
version_text(const char *ver, char *out, size_t n) {
  int maj = 0, min = 0, patch = 0;
  if(sscanf(ver, "%d.%d.%d", &maj, &min, &patch) == 3) {
    if(min == 0 && patch > 0) snprintf(out, n, "v%d.%02d", maj, patch);
    else if(patch == 0)       snprintf(out, n, "v%d.%02d", maj, min);
    else                      snprintf(out, n, "v%d.%d.%d", maj, min, patch);
  } else if(ver[0] != 'v' && ver[0] != 'V') {
    snprintf(out, n, "v%.20s", ver);
  } else {
    snprintf(out, n, "%.22s", ver);
  }
}

static void
parse_param_json(const char *buf, size_t len, meta_t *m) {
  cJSON *root = cJSON_ParseWithLength(buf, len);
  if(!cJSON_IsObject(root)) { cJSON_Delete(root); return; }
  const cJSON *it;
  if(cJSON_IsString(it = cJSON_GetObjectItemCaseSensitive(root, "titleId"))) SET(m->title_id, it->valuestring);
  if(cJSON_IsString(it = cJSON_GetObjectItemCaseSensitive(root, "category"))) SET(m->category, it->valuestring);
  const char *vk[] = { "contentVersion", "appVersion", "version" };
  for(int i = 0; i < 3 && !m->version[0]; i++)
    if(cJSON_IsString(it = cJSON_GetObjectItemCaseSensitive(root, vk[i])) && it->valuestring[0]) version_text(it->valuestring, m->version, sizeof(m->version));
  const char *gk[] = { "titleName", "title" };
  for(int i = 0; i < 2 && !m->global_title[0]; i++)
    if(cJSON_IsString(it = cJSON_GetObjectItemCaseSensitive(root, gk[i]))) SET(m->global_title, it->valuestring);
  const cJSON *lp = cJSON_GetObjectItemCaseSensitive(root, "localizedParameters");
  if(cJSON_IsObject(lp)) {
    if(cJSON_IsString(it = cJSON_GetObjectItemCaseSensitive(lp, "defaultLanguage"))) SET(m->default_lang, it->valuestring);
    const cJSON *l;
    cJSON_ArrayForEach(l, lp) {
      if(m->nloc >= sizeof(m->loc) / sizeof(m->loc[0])) break;
      if(!cJSON_IsObject(l) || !l->string) continue;
      const cJSON *tn = cJSON_GetObjectItemCaseSensitive(l, "titleName");
      if(!cJSON_IsString(tn) || !tn->valuestring[0]) continue;
      SET(m->loc[m->nloc].lang, l->string);
      SET(m->loc[m->nloc].title, tn->valuestring);
      m->nloc++;
    }
  }
  cJSON_Delete(root);
}

static const char *const k_sfo_lang[30] = {
  "ja-JP", "en-US", "fr-FR", "es-ES", "de-DE", "it-IT", "nl-NL", "pt-PT",
  "ru-RU", "ko-KR", "zh-Hant", "zh-Hans", "fi-FI", "sv-SE", "da-DK", "no-NO",
  "pl-PL", "pt-BR", "en-GB", "tr-TR", "es-419", "ar-AE", "fr-CA", "cs-CZ",
  "hu-HU", "el-GR", "ro-RO", "th-TH", "vi-VN", "id-ID"
};

/* The text of an SFO value: its bytes, without the trailing NULs. */
static void
sfo_value(const uint8_t *d, uint32_t len, char *out, size_t n) {
  size_t k = len < n - 1 ? len : n - 1;
  while(k > 0 && d[k - 1] == 0) k--;
  memcpy(out, d, k);
  out[k] = 0;
  for(size_t i = 0; i < k; i++) if((unsigned char)out[i] < 32) { out[i] = 0; break; }
}

static void
parse_param_sfo(const uint8_t *sfo, size_t len, meta_t *m, char *title) {
  if(len < 20 || memcmp(sfo, "\x00PSF", 4)) return;
  uint32_t kt = le32(sfo + 8), dt = le32(sfo + 12), count = le32(sfo + 16);
  if(kt >= len || dt >= len || count > 1024) return;
  char app_ver[24] = "", version[24] = "";
  for(uint32_t i = 0; i < count; i++) {
    if((uint64_t)20 + (uint64_t)(i + 1) * 16 > len) break;
    const uint8_t *e = sfo + 20 + (size_t)i * 16;
    uint16_t koff = le16(e);
    uint32_t dlen = le32(e + 4), doff = le32(e + 12);
    if((uint64_t)kt + koff >= len) continue;
    const char *key = (const char *)(sfo + kt + koff);
    if(!memchr(key, 0, len - (kt + koff))) continue;
    if((uint64_t)dt + doff + dlen > len) continue;
    const uint8_t *d = sfo + dt + doff;
    if(!strcmp(key, "TITLE")) {
      if(!title[0]) sfo_value(d, dlen, title, 160);
    } else if(!strcmp(key, "TITLE_ID")) {
      if(!m->title_id[0]) sfo_value(d, dlen, m->title_id, sizeof(m->title_id));
    } else if(!strncmp(key, "TITLE_", 6) && key[6] && m->nloc < sizeof(m->loc) / sizeof(m->loc[0])) {
      int numeric = 1;
      for(const char *s = key + 6; *s; s++) if(*s < '0' || *s > '9') { numeric = 0; break; }
      int idx = numeric ? atoi(key + 6) : -1;
      if(idx >= 0 && idx < 30) {
        SET(m->loc[m->nloc].lang, k_sfo_lang[idx]);
        sfo_value(d, dlen, m->loc[m->nloc].title, sizeof(m->loc[m->nloc].title));
        if(m->loc[m->nloc].title[0]) m->nloc++;
      }
    } else if(!strcmp(key, "CATEGORY")) {
      if(!m->category[0]) sfo_value(d, dlen, m->category, sizeof(m->category));
    } else if(!strcmp(key, "APP_VER")) {
      if(!app_ver[0]) sfo_value(d, dlen, app_ver, sizeof(app_ver));
    } else if(!strcmp(key, "VERSION")) {
      if(!version[0]) sfo_value(d, dlen, version, sizeof(version));
    }
  }
  const char *best = app_ver[0] ? app_ver : version;
  if(best[0] && !m->version[0]) {
    if(best[0] != 'v' && best[0] != 'V') snprintf(m->version, sizeof(m->version), "v%.20s", best);
    else                                 snprintf(m->version, sizeof(m->version), "%.22s", best);
  }
}

/* The title to show: German, else the package's default language, else English, else the first. */
static void
pick_name(const meta_t *m, const char *sfo_title, char *out, size_t n) {
  static const char *const want_de[] = { "de-DE", "de" };
  for(size_t k = 0; k < 2; k++)
    for(size_t i = 0; i < m->nloc; i++)
      if(!strcmp(m->loc[i].lang, want_de[k])) { snprintf(out, n, "%s", m->loc[i].title); return; }
  if(m->default_lang[0])
    for(size_t i = 0; i < m->nloc; i++)
      if(!strcmp(m->loc[i].lang, m->default_lang)) { snprintf(out, n, "%s", m->loc[i].title); return; }
  if(m->global_title[0]) { snprintf(out, n, "%s", m->global_title); return; }
  if(sfo_title[0]) { snprintf(out, n, "%s", sfo_title); return; }
  for(size_t i = 0; i < m->nloc; i++)
    if(!strncmp(m->loc[i].lang, "en", 2)) { snprintf(out, n, "%s", m->loc[i].title); return; }
  if(m->nloc) snprintf(out, n, "%s", m->loc[0].title);
}

/* ------------------------------------------------------------------ the container */

static int
pread_all(int fd, void *buf, size_t n, uint64_t off) {
  uint8_t *p = buf;
  size_t got = 0;
  while(got < n) {
    ssize_t r = pread(fd, p + got, n - got, (off_t)(off + got));
    if(r < 0 && errno == EINTR) continue;
    if(r <= 0) return -1;
    got += (size_t)r;
  }
  return 0;
}

/* Where the container is read from: a file, or the slices of a split package laid end to end. Every read is checked
   against the size first, so a number in the container that points past the end fails here. */
typedef struct {
  int      (*rd)(void *ctx, void *buf, size_t n, uint64_t off);
  void     *ctx;
  uint64_t size;
} pkg_src_t;

static int
src_read(const pkg_src_t *s, void *buf, size_t n, uint64_t off) {
  if(off > s->size || (uint64_t)n > s->size - off) return -1;
  return s->rd(s->ctx, buf, n, off);
}

static int
file_rd(void *ctx, void *buf, size_t n, uint64_t off) { return pread_all(*(int *)ctx, buf, n, off); }

static void
kind_text(char *out, size_t n, const char *k) { snprintf(out, n, "%s", k); }

/* 5 for a PS5 title id (PPSA...), 4 for a PS4 one (CUSA...), else the hint, else 0. */
static int
platform_of(const char *title_id, int hint) {
  if(!strncmp(title_id, "PPSA", 4)) return 5;
  if(!strncmp(title_id, "CUSA", 4)) return 4;
  return hint;
}

static void
clean_all(ps5tm_pkg_t *p) {
  utf8_clean(p->file);
  utf8_clean(p->orig_file);
  utf8_clean(p->title_id);
  utf8_clean(p->content_id);
  utf8_clean(p->name);
  utf8_clean(p->version);
  utf8_clean(p->category);
}

static int
parse_multipart(int fd, ps5tm_pkg_t *out) {
  uint8_t h[MP_HEADER_SIZE];
  if(pread_all(fd, h, sizeof(h), 0) != 0) return -1;
  uint32_t part = le32(h + MP_OFF_PART), parts = le32(h + MP_OFF_PARTS), comp = le32(h + MP_OFF_COMPRESS);
  if(le32(h + MP_OFF_VERSION) != 1 || parts == 0 || parts > MP_MAX_PARTS || part == 0 || part > parts) return -1;
  if(comp != 0 && comp != 1) return -1;
  if(le32(h + MP_OFF_CHUNK) > (64u << 20)) return -1;
  out->part = part;
  out->parts = parts;
  memcpy(out->uuid, h + MP_OFF_UUID, 16);
  field_text(out->orig_file, h + MP_OFF_FILE, 255);
  field_text(out->title_id, h + MP_OFF_TITLE_ID, 23);
  field_text(out->content_id, h + MP_OFF_CONTENT, 63);
  field_text(out->version, h + MP_OFF_APP_VER, 23);
  char kind[16];
  field_text(kind, h + MP_OFF_KIND, 15);
  kind_text(out->kind, sizeof(out->kind), !strcmp(kind, "update") ? "update" : !strcmp(kind, "dlc") ? "dlc" : "base");
  char name[160];
  field_text(name, h + MP_OFF_NAME, 159);
  SET(out->name, name[0] ? name : out->title_id[0] ? out->title_id : "Unbekanntes Paket");
  if(!out->title_id[0]) SET(out->title_id, "UNKNOWN");
  uint64_t total = le64(h + MP_OFF_TOTAL);
  out->total = total > 0 ? total : out->size;
  out->raw = comp == 0;
  out->data_off = le32(h + MP_OFF_DATA_OFF);
  out->data_size = le64(h + MP_OFF_DATA_SIZE);
  out->part_off = le64(h + MP_OFF_PART_OFF);
  uint32_t ioff = le32(h + MP_OFF_ICON_OFF), isz = le32(h + MP_OFF_ICON_SIZE);
  if(ioff > 0 && isz > 0 && isz < PKG_MAX_ICON && (uint64_t)ioff + isz <= out->size) { out->icon_off = ioff; out->icon_size = isz; }
  out->plat = platform_of(out->title_id, 0);
  clean_all(out);
  return 0;
}

/* The container of a package: the header the first 0x200 bytes of it make (already read into hdr), the entry table,
   param.json / param.sfo and the icon. src is the package from its first byte to its last. */
static int
parse_container(const pkg_src_t *src, const uint8_t *hdr, size_t hr, ps5tm_pkg_t *out) {
  uint64_t cnt = 0;
  int found = 0;
  if(!memcmp(hdr, "\x7f" "CNT", 4)) {
    found = 1;
  } else if(!memcmp(hdr, "\x7f" "FIH", 4)) {
    uint64_t cand = le64(hdr + 0x58);
    uint8_t magic[4];
    if(cand > 0 && cand < out->size && src_read(src, magic, 4, cand) == 0 && !memcmp(magic, "\x7f" "CNT", 4)) { cnt = cand; found = 1; }
    for(size_t off = 0x10; !found && off + 8 <= hr; off += 8) {
      cand = le64(hdr + off);
      if(cand >= 0x10000 && cand < out->size && (cand % 0x1000) == 0 && src_read(src, magic, 4, cand) == 0 && !memcmp(magic, "\x7f" "CNT", 4)) { cnt = cand; found = 1; }
    }
  }
  if(!found) return -1;

  uint8_t ch[0x80];
  if(src_read(src, ch, sizeof(ch), cnt) != 0 || memcmp(ch, "\x7f" "CNT", 4)) return -1;
  uint32_t type_magic = be32(ch + 4);
  field_text(out->content_id, ch + 0x40, 48);
  uint32_t count = be32(ch + 0x10), toff = be32(ch + 0x18);
  if(count == 0 || count > PKG_MAX_ENTRIES || toff > PKG_TABLE_MAX) return -1;
  size_t tsize = (size_t)count * 32;
  if(cnt + toff + tsize > out->size) return -1;
  uint8_t *tab = malloc(tsize);
  if(!tab || src_read(src, tab, tsize, cnt + toff) != 0) { free(tab); return -1; }

  char *names = NULL;
  uint32_t nsz = 0;
  for(uint32_t i = 0; i < count; i++) {
    const uint8_t *e = tab + (size_t)i * 32;
    if(be32(e) != 0x0200) continue;
    uint32_t noff = be32(e + 16);
    nsz = be32(e + 20);
    if(nsz > 0 && nsz < 65536 && cnt + noff + nsz <= out->size && (names = malloc((size_t)nsz + 1)) != NULL) {
      if(src_read(src, names, nsz, cnt + noff) == 0) names[nsz] = 0; else { free(names); names = NULL; }
    }
    break;
  }

  meta_t m;
  memset(&m, 0, sizeof(m));
  char sfo_title[160] = "";
  int has_playgo = 0, has_delta = 0, has_base_meta = 0, sfo_category = 0, has_json = 0;
  for(uint32_t i = 0; i < count; i++) {
    const uint8_t *e = tab + (size_t)i * 32;
    uint32_t type = be32(e), fn = be32(e + 4), doff = be32(e + 16), dsz = be32(e + 20);
    const char *name = (names && fn < nsz) ? names + fn : "";
    int in_file = dsz > 0 && cnt + doff + dsz <= out->size;
    if(type == 0x1008 || !strcmp(name, "app/playgo-chunk.dat")) has_playgo = 1;
    if(type == 0x0407 || type == 0x0408 || !strcmp(name, "target-deltainfo.dat") || !strcmp(name, "origin-deltainfo.dat")) has_delta = 1;

    if((type == 0x2000 || !strcmp(name, "param.json")) && in_file && dsz < PKG_MAX_PARAM) {
      char *b = malloc((size_t)dsz + 1);
      if(b && src_read(src, b, dsz, cnt + doff) == 0) {
        b[dsz] = 0;
        if(has_text(b, dsz, "\"applicationDrmType\"") || has_text(b, dsz, "\"applicationCategoryType\"") || has_text(b, dsz, "\"contentBadgeType\"")) has_base_meta = 1;
        parse_param_json(b, dsz, &m);
        has_json = 1;
      }
      free(b);
    }
    if((type == 0x1000 || !strcmp(name, "param.sfo")) && in_file && dsz < PKG_MAX_PARAM) {
      uint8_t *b = malloc(dsz);
      if(b && src_read(src, b, dsz, cnt + doff) == 0) {
        meta_t s;
        memset(&s, 0, sizeof(s));
        char stitle[160] = "";
        parse_param_sfo(b, dsz, &s, stitle);
        if(s.category[0] && !m.category[0]) { sfo_category = 1; SET(m.category, s.category); }
        if(!m.title_id[0] && s.title_id[0]) SET(m.title_id, s.title_id);
        if(!sfo_title[0] && stitle[0]) SET(sfo_title, stitle);
        if(!m.version[0] && s.version[0]) SET(m.version, s.version);
        for(size_t k = 0; k < s.nloc && m.nloc < sizeof(m.loc) / sizeof(m.loc[0]); k++) m.loc[m.nloc++] = s.loc[k];
        if(!m.default_lang[0] && s.nloc) SET(m.default_lang, "en-US");
      }
      free(b);
    }
    if((type == 0x1200 || !strcmp(name, "icon0.png")) && in_file && dsz < PKG_MAX_ICON) {
      out->icon_off = cnt + doff;
      out->icon_size = dsz;
    }
  }
  free(tab);
  free(names);

  int delta_type = ((type_magic & 0xFF) == 0x1E || (type_magic & 0xFF000000) == 0x41000000);
  const char *c = m.category;
  const char *kind;
  if(has_playgo || has_delta || delta_type || !strncmp(c, "gp", 2)) kind = "update";
  else if(!strncmp(c, "ac", 2) || !strncmp(c, "al", 2) || !strcmp(c, "addcont")) kind = "dlc";
  else if(sfo_category && (!strncmp(c, "gd", 2) || !strncmp(c, "bd", 2) || !strncmp(c, "gc", 2) || !strncmp(c, "wt", 2))) kind = "base";
  else if((type_magic & 0xFF) == 1 && !has_base_meta) kind = "dlc";        /* PS5 DLC; a PS4 base package can also have type 1: its category decided above */
  else kind = "base";
  kind_text(out->kind, sizeof(out->kind), kind);

  SET(out->title_id, m.title_id);
  SET(out->category, m.category);
  SET(out->version, m.version);
  if(!out->title_id[0] && out->content_id[0]) {                           /* "EP0000-PPSA01234_00-..." */
    const char *dash = strchr(out->content_id, '-');
    const char *us = dash ? strchr(dash + 1, '_') : NULL;
    if(us && (size_t)(us - (dash + 1)) < sizeof(out->title_id)) {
      snprintf(out->title_id, sizeof(out->title_id), "%.*s", (int)(us - (dash + 1)), dash + 1);
      if(!looks_like_title_id(out->title_id)) out->title_id[0] = 0;
    }
  }
  pick_name(&m, sfo_title, out->name, sizeof(out->name));
  if(!out->name[0]) SET(out->name, out->title_id[0] ? out->title_id : "Unbekanntes Paket");
  out->plat = platform_of(out->title_id, has_json ? 5 : 4);
  clean_all(out);
  out->valid = 1;
  return 0;
}


int
ps5tm_pkg_parse(const char *path, ps5tm_pkg_t *out) {
  if(!path || !out) return -1;
  memset(out, 0, sizeof(*out));
  SET(out->path, path);
  const char *slash = strrchr(path, '/');
  SET(out->file, slash ? slash + 1 : path);

  int fd = open(path, O_RDONLY);
  if(fd < 0) return -1;
  struct stat st;
  if(fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 0x80) { close(fd); return -1; }
  out->size = out->total = (uint64_t)st.st_size;
  out->mtime = (int64_t)st.st_mtime;

  uint8_t hdr[0x200];
  ssize_t hr = pread(fd, hdr, sizeof(hdr), 0);
  if(hr < 0x80) { close(fd); return -1; }

  if(hr >= 8 && !memcmp(hdr, "PS5MPKG1", 8)) {
    int rc = (st.st_size >= MP_HEADER_SIZE) ? parse_multipart(fd, out) : -1;
    close(fd);
    if(rc == 0) out->valid = 1;
    return rc;
  }

  pkg_src_t src = { file_rd, &fd, (uint64_t)st.st_size };
  int rc = parse_container(&src, hdr, (size_t)hr, out);
  close(fd);
  return rc;
}

/* The slices of a split package as one stream (the same pieces the server hands the installer). */
typedef struct {
  const ps5tm_pkgslice_t *sl;
  unsigned                n;
  int                     fd;
  unsigned                idx;
} slice_ctx_t;

static int
slice_rd(void *vctx, void *buf, size_t n, uint64_t off) {
  slice_ctx_t *c = vctx;
  uint8_t *p = buf;
  while(n) {
    unsigned k = UINT_MAX;
    for(unsigned i = 0; i < c->n; i++)
      if(off >= c->sl[i].logical && off - c->sl[i].logical < c->sl[i].size) { k = i; break; }
    if(k == UINT_MAX) return -1;
    const ps5tm_pkgslice_t *s = &c->sl[k];
    uint64_t in = off - s->logical, avail = s->size - in;
    size_t m = (uint64_t)n < avail ? n : (size_t)avail;
    if(c->fd < 0 || c->idx != k) {
      if(c->fd >= 0) close(c->fd);
      c->idx = k;
      c->fd = open(s->path, O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
      struct stat st;
      if(c->fd >= 0 && (fstat(c->fd, &st) != 0 || !S_ISREG(st.st_mode))) { close(c->fd); c->fd = -1; }
      if(c->fd < 0) return -1;
    }
    if(pread_all(c->fd, p, m, s->file_off + in) != 0) return -1;
    p += m;
    off += m;
    n -= m;
  }
  return 0;
}

/* What the package inside a set of parts says about itself - not what the header of the first part claims. 0, or -1
   when it cannot be read as a package. Only name, ids, kind, version and platform are filled in. */
int
ps5tm_pkg_parse_slices(const ps5tm_pkgslice_t *sl, unsigned n, uint64_t total, ps5tm_pkg_t *out) {
  if(!sl || n == 0 || !out || total < 0x80) return -1;
  memset(out, 0, sizeof(*out));
  slice_ctx_t c = { sl, n, -1, 0 };
  pkg_src_t src = { slice_rd, &c, total };
  uint8_t hdr[0x200];
  size_t hr = total < sizeof(hdr) ? (size_t)total : sizeof(hdr);
  int rc = src_read(&src, hdr, hr, 0);
  if(rc == 0) {
    out->size = out->total = total;
    rc = parse_container(&src, hdr, hr, out);
  }
  if(c.fd >= 0) close(c.fd);
  return rc;
}

/* The icon of a package: the bytes at the place the container named, only if they are a PNG. 0 and a malloc'd
   buffer, or -1. */
int
ps5tm_pkg_icon(const ps5tm_pkg_t *p, uint8_t **data, size_t *n) {
  if(!p || !data || !n || p->icon_size == 0 || p->icon_size >= PKG_MAX_ICON) return -1;
  int fd = open(p->path, O_RDONLY);
  if(fd < 0) return -1;
  struct stat st;
  if(fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || p->icon_off + p->icon_size > (uint64_t)st.st_size) { close(fd); return -1; }
  uint8_t *b = malloc(p->icon_size);
  int rc = (b && pread_all(fd, b, p->icon_size, p->icon_off) == 0 && p->icon_size >= 8 && !memcmp(b, "\x89PNG\r\n\x1a\n", 8)) ? 0 : -1;
  close(fd);
  if(rc != 0) { free(b); return -1; }
  *data = b;
  *n = p->icon_size;
  return 0;
}

/* A package read through a function instead of a file: the one the browser uploads while it is being installed
   (pkglive.c). rd reads n bytes at off (0, or -1), the whole package is total bytes. A package in parts is not
   read this way. Only name, ids, kind, version, platform and where the icon lies are filled in. 0, or -1. */
int
ps5tm_pkg_parse_reader(int (*rd)(void *ctx, void *buf, size_t n, uint64_t off), void *ctx, uint64_t total,
                       const char *file, ps5tm_pkg_t *out) {
  if(!rd || !out || total < 0x80) return -1;
  memset(out, 0, sizeof(*out));
  SET(out->file, file ? file : "");
  out->size = out->total = total;
  uint8_t hdr[0x200];
  size_t hr = total < sizeof(hdr) ? (size_t)total : sizeof(hdr);
  pkg_src_t src = { rd, ctx, total };
  if(src_read(&src, hdr, hr, 0) != 0) return -1;
  if(hr >= 8 && !memcmp(hdr, "PS5MPKG1", 8)) return -1;
  return parse_container(&src, hdr, hr, out);
}

/* The icon of such a package: the bytes at the place the container named, only if they are a PNG. */
int
ps5tm_pkg_icon_reader(const ps5tm_pkg_t *p, int (*rd)(void *ctx, void *buf, size_t n, uint64_t off), void *ctx,
                      uint8_t **data, size_t *n) {
  if(!p || !rd || !data || !n || p->icon_size == 0 || p->icon_size >= PKG_MAX_ICON) return -1;
  if(p->icon_off > p->total || (uint64_t)p->icon_size > p->total - p->icon_off) return -1;
  uint8_t *b = malloc(p->icon_size);
  int rc = (b && rd(ctx, b, p->icon_size, p->icon_off) == 0 && p->icon_size >= 8 && !memcmp(b, "\x89PNG\r\n\x1a\n", 8)) ? 0 : -1;
  if(rc != 0) { free(b); return -1; }
  *data = b;
  *n = p->icon_size;
  return 0;
}
