/* SPDX-License-Identifier: GPL-3.0-or-later
 * A PS4 dump as an image ShadowMountPlus can mount (23.10.2026).
 *
 * What it takes, and what this file does of it, follows PS4FFPFSC 0.2.9 (GPL-3.0-or-later; ps4ffpsc/dump_source.py,
 * sfo.py, npbind.py, pipeline.py), which does the same on a PC: check that the dump has an eboot.bin and a param.sfo with a
 * matching title id; lay the update over the base (done by the image planner of conv_exfat.c, nothing is copied); give the
 * image the sce_sys/param.json ShadowMountPlus reads, built from the param.sfo; put right a wrong SHA-1 footer in the dump's
 * npbind.dat. The two files that are new to the dump go into a small folder of their own (the "overlay") that the planner
 * reads as the last of its source folders, so the dump itself is never touched.
 *
 * Everything that reads a file of the dump checks every number in it against the file's size first. */

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "dump_image.h"

#define SFO_MAX      (256u * 1024)
#define NPBIND_MAX   (1024u * 1024)

/* sfo.py SFO_TITLE_LOCALES: TITLE_00 ... TITLE_29 */
static const char *const k_locales[30] = {
  "ja-JP", "en-US", "fr-FR", "es-ES", "de-DE", "it-IT", "nl-NL", "pt-PT", "ru-RU", "ko-KR",
  "zh-Hant", "zh-Hans", "fi-FI", "sv-SE", "da-DK", "no-NO", "pl-PL", "pt-BR", "en-GB", "tr-TR",
  "es-419", "ar-AE", "fr-CA", "cs-CZ", "hu-HU", "el-GR", "ro-RO", "th-TH", "vi-VN", "id-ID",
};

/* ------------------------------------------------------------------ small helpers */

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

static int
utf8_valid(const uint8_t *s, size_t n) {
  size_t i = 0;
  while(i < n) {
    uint8_t c = s[i];
    if(c < 0x80) { i++; continue; }
    size_t need;
    uint32_t cp;
    if((c & 0xE0) == 0xC0)      { need = 1; cp = c & 0x1F; }
    else if((c & 0xF0) == 0xE0) { need = 2; cp = c & 0x0F; }
    else if((c & 0xF8) == 0xF0) { need = 3; cp = c & 0x07; }
    else return 0;
    if(i + need >= n) return 0;
    for(size_t k = 1; k <= need; k++) {
      if((s[i + k] & 0xC0) != 0x80) return 0;
      cp = (cp << 6) | (s[i + k] & 0x3F);
    }
    if((need == 1 && cp < 0x80) || (need == 2 && cp < 0x800) || (need == 3 && cp < 0x10000) ||
       cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
      return 0;
    i += need + 1;
  }
  return 1;
}

/* The bytes of a regular file (not a link) up to max; NULL with err set otherwise. */
static uint8_t *
read_file(const char *path, size_t max, size_t *len, char *err, size_t err_len) {
  struct stat st;
  if(lstat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
    snprintf(err, err_len, "%s fehlt oder ist keine Datei.", path);
    return NULL;
  }
  if((uint64_t)st.st_size > max) {
    snprintf(err, err_len, "%s ist ungewöhnlich groß.", path);
    return NULL;
  }
  FILE *f = fopen(path, "rb");
  if(!f) { snprintf(err, err_len, "%s ließ sich nicht öffnen: %s", path, strerror(errno)); return NULL; }
  uint8_t *b = malloc((size_t)st.st_size + 1);
  if(!b) { fclose(f); snprintf(err, err_len, "Kein Speicher."); return NULL; }
  size_t got = fread(b, 1, (size_t)st.st_size, f);
  fclose(f);
  if(got != (size_t)st.st_size) { free(b); snprintf(err, err_len, "%s ließ sich nicht ganz lesen.", path); return NULL; }
  b[got] = 0;
  *len = got;
  return b;
}

static int
is_file(const char *p) {
  struct stat st;
  return lstat(p, &st) == 0 && S_ISREG(st.st_mode);
}

static int
is_dir(const char *p) {
  struct stat st;
  return lstat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

static int
mkdir_p(const char *path) {
  char tmp[1024];
  if(snprintf(tmp, sizeof(tmp), "%s", path) >= (int)sizeof(tmp)) return -1;
  for(char *p = tmp + 1; *p; p++) {
    if(*p != '/') continue;
    *p = 0;
    if(mkdir(tmp, 0777) != 0 && errno != EEXIST) return -1;
    *p = '/';
  }
  return (mkdir(tmp, 0777) == 0 || errno == EEXIST) ? 0 : -1;
}

/* Replaces path with data through a temporary file next to it. */
static int
write_atomic(const char *path, const void *data, size_t len) {
  char tmp[1100];
  if(snprintf(tmp, sizeof(tmp), "%s.tmp", path) >= (int)sizeof(tmp)) return -1;
  FILE *f = fopen(tmp, "wb");
  if(!f) return -1;
  int ok = fwrite(data, 1, len, f) == len;
  ok = (fflush(f) == 0) && ok;
  ok = (fclose(f) == 0) && ok;
  if(!ok || rename(tmp, path) != 0) { unlink(tmp); return -1; }
  return 0;
}

/* ------------------------------------------------------------------ SFO */

typedef struct { const uint8_t *d; size_t n; uint32_t keys, data, count; } sfo_t;

/* The header and every entry are checked here, so that what comes later cannot read outside the file (sfo.py parse_sfo_bytes). */
static int
sfo_open(sfo_t *s, const uint8_t *d, size_t n) {
  if(!d || n < 20 || n > SFO_MAX || memcmp(d, "\0PSF", 4) != 0) return -1;
  uint32_t ver = rd32(d + 4);
  if(ver != 0x100 && ver != 0x101) return -1;
  s->d = d;
  s->n = n;
  s->keys = rd32(d + 8);
  s->data = rd32(d + 12);
  s->count = rd32(d + 16);
  if(s->count > 4096) return -1;
  uint64_t index_end = 20 + (uint64_t)s->count * 16;
  if(!(index_end <= s->keys && s->keys <= s->data && s->data <= n)) return -1;
  for(uint32_t i = 0; i < s->count; i++) {
    const uint8_t *e = d + 20 + (size_t)i * 16;
    uint32_t koff = rd16(e), len = rd32(e + 4), maxlen = rd32(e + 8), doff = rd32(e + 12);
    uint16_t fmt = rd16(e + 2);
    uint64_t kstart = (uint64_t)s->keys + koff, vstart = (uint64_t)s->data + doff;
    if(kstart >= s->data || vstart > n || vstart + len > n) return -1;
    if(!memchr(d + kstart, 0, s->data - kstart)) return -1;
    if(fmt == 0x0204) {
      if(!len || d[vstart + len - 1] != 0 || !utf8_valid(d + vstart, len - 1)) return -1;
    } else if(fmt == 0x0404) {
      if(len != 4) return -1;
    } else if(fmt != 0x0004) {
      return -1;
    }
    if(maxlen < len) return -1;
  }
  return 0;
}

/* The entry of key: 0 found with its format and value, -1 not there. */
static int
sfo_find(const sfo_t *s, const char *key, uint16_t *fmt, const uint8_t **val, uint32_t *len) {
  for(uint32_t i = 0; i < s->count; i++) {
    const uint8_t *e = s->d + 20 + (size_t)i * 16;
    const char *k = (const char *)s->d + s->keys + rd16(e);
    if(strcmp(k, key) != 0) continue;
    *fmt = rd16(e + 2);
    *len = rd32(e + 4);
    *val = s->d + s->data + rd32(e + 12);
    return 0;
  }
  return -1;
}

static void
strip_copy(char *out, size_t out_len, const uint8_t *v, size_t n) {
  size_t a = 0;
  size_t z = 0;
  while(z < n && v[z]) z++;
  while(a < z && isspace(v[a])) a++;
  while(z > a && isspace(v[z - 1])) z--;
  size_t l = z - a;
  if(l >= out_len) {                 /* never cut a character in two */
    l = out_len - 1;
    while(l > 0 && (v[a + l] & 0xC0) == 0x80) l--;
  }
  memcpy(out, v + a, l);
  out[l] = 0;
}

static int
sfo_get_text(const sfo_t *s, const char *key, char *out, size_t out_len) {
  uint16_t fmt;
  const uint8_t *v;
  uint32_t len;
  out[0] = 0;
  if(sfo_find(s, key, &fmt, &v, &len) != 0 || fmt != 0x0204) return -1;
  strip_copy(out, out_len, v, len - 1);
  return 0;
}

static int
sfo_get_int(const sfo_t *s, const char *key, int32_t *out) {
  uint16_t fmt;
  const uint8_t *v;
  uint32_t len;
  if(sfo_find(s, key, &fmt, &v, &len) != 0 || fmt != 0x0404) return -1;
  *out = (int32_t)rd32(v);
  return 0;
}

int
dimg_sfo_text(const uint8_t *sfo, size_t sfo_len, const char *key, char *out, size_t out_len) {
  sfo_t s;
  if(out_len) out[0] = 0;
  if(sfo_open(&s, sfo, sfo_len) != 0 || !out_len) return -1;
  return sfo_get_text(&s, key, out, out_len);
}

static void
title_of(const sfo_t *s, char *out, size_t out_len) {
  char t[256];
  if(sfo_get_text(s, "TITLE", t, sizeof(t)) == 0 && t[0]) { snprintf(out, out_len, "%s", t); return; }
  if(sfo_get_text(s, "TITLE_01", t, sizeof(t)) == 0 && t[0]) { snprintf(out, out_len, "%s", t); return; }
  for(int i = 0; i < 30; i++) {
    char key[16];
    if(i == 1) continue;
    snprintf(key, sizeof(key), "TITLE_%02d", i);
    if(sfo_get_text(s, key, t, sizeof(t)) == 0 && t[0]) { snprintf(out, out_len, "%s", t); return; }
  }
  if(sfo_get_text(s, "CONTENT_ID", t, sizeof(t)) == 0 && t[0]) { snprintf(out, out_len, "%s", t); return; }
  if(sfo_get_text(s, "TITLE_ID", t, sizeof(t)) == 0 && t[0]) { snprintf(out, out_len, "%s", t); return; }
  snprintf(out, out_len, "Unknown Game");
}

void
dimg_sfo_title(const uint8_t *sfo, size_t sfo_len, char *out, size_t out_len) {
  sfo_t s;
  if(!out_len) return;
  if(sfo_open(&s, sfo, sfo_len) != 0) { out[0] = 0; return; }
  title_of(&s, out, out_len);
}

/* ------------------------------------------------------------------ param.json (sfo.py build_param_json) */

typedef struct { char *p; size_t len, cap; int oom; } sb_t;

static void
sb_add(sb_t *b, const char *s, size_t n) {
  if(b->oom) return;
  if(b->len + n + 1 > b->cap) {
    size_t c = b->cap ? b->cap : 1024;
    while(c < b->len + n + 1) c *= 2;
    char *q = realloc(b->p, c);
    if(!q) { b->oom = 1; return; }
    b->p = q;
    b->cap = c;
  }
  memcpy(b->p + b->len, s, n);
  b->len += n;
  b->p[b->len] = 0;
}

static void sb_str(sb_t *b, const char *s) { sb_add(b, s, strlen(s)); }

/* A JSON string the way Python's json.dumps(ensure_ascii=False) writes it. */
static void
sb_json(sb_t *b, const char *s) {
  sb_add(b, "\"", 1);
  for(const unsigned char *p = (const unsigned char *)s; *p; p++) {
    switch(*p) {
    case '"':  sb_add(b, "\\\"", 2); break;
    case '\\': sb_add(b, "\\\\", 2); break;
    case '\n': sb_add(b, "\\n", 2); break;
    case '\r': sb_add(b, "\\r", 2); break;
    case '\t': sb_add(b, "\\t", 2); break;
    case '\b': sb_add(b, "\\b", 2); break;
    case '\f': sb_add(b, "\\f", 2); break;
    default:
      if(*p < 0x20) { char u[8]; snprintf(u, sizeof(u), "\\u%04x", *p); sb_add(b, u, 6); }
      else sb_add(b, (const char *)p, 1);
    }
  }
  sb_add(b, "\"", 1);
}

char *
dimg_param_json(const uint8_t *sfo, size_t sfo_len, const char *title_id, const char *title, size_t *len) {
  sfo_t s;
  if(sfo_open(&s, sfo, sfo_len) != 0 || !title_id || !title || !title[0]) return NULL;

  /* localizedParameters: each TITLE_xx with text, en-US always (TITLE_01 when it has text, else the chosen title) */
  char names[30][256];
  int has[30] = { 0 };
  for(int i = 0; i < 30; i++) {
    char key[16];
    snprintf(key, sizeof(key), "TITLE_%02d", i);
    if(sfo_get_text(&s, key, names[i], sizeof(names[i])) == 0 && names[i][0]) has[i] = 1;
  }
  if(!has[1]) { snprintf(names[1], sizeof(names[1]), "%s", title); has[1] = 1; }

  const char *keys[31];
  int idx[31];
  unsigned nk = 0;
  keys[nk] = "defaultLanguage";
  idx[nk++] = -1;
  for(int i = 0; i < 30; i++) if(has[i]) { keys[nk] = k_locales[i]; idx[nk++] = i; }
  /* sort_keys=True: by key, as bytes */
  for(unsigned a = 1; a < nk; a++) {
    const char *k = keys[a];
    int x = idx[a];
    unsigned b = a;
    while(b > 0 && strcmp(keys[b - 1], k) > 0) { keys[b] = keys[b - 1]; idx[b] = idx[b - 1]; b--; }
    keys[b] = k;
    idx[b] = x;
  }

  sb_t b = { 0 };
  sb_str(&b, "{\n  \"localizedParameters\": {\n");
  for(unsigned i = 0; i < nk; i++) {
    sb_str(&b, "    ");
    sb_json(&b, keys[i]);
    sb_str(&b, ": ");
    if(idx[i] < 0) {
      sb_json(&b, "en-US");
    } else {
      sb_str(&b, "{\n      \"titleName\": ");
      sb_json(&b, names[idx[i]]);
      sb_str(&b, "\n    }");
    }
    sb_str(&b, i + 1 < nk ? ",\n" : "\n");
  }
  sb_str(&b, "  },\n  \"titleId\": ");
  sb_json(&b, title_id);
  sb_str(&b, ",\n  \"titleName\": ");
  sb_json(&b, title);
  for(int i = 1; i <= 4; i++) {
    char key[32];
    int32_t v;
    snprintf(key, sizeof(key), "USER_DEFINED_PARAM_%d", i);
    if(sfo_get_int(&s, key, &v) == 0 && v != 0) {
      char line[64];
      snprintf(line, sizeof(line), ",\n  \"userDefinedParam%d\": %d", i, (int)v);
      sb_str(&b, line);
    }
  }
  sb_str(&b, "\n}\n");
  if(b.oom) { free(b.p); return NULL; }
  *len = b.len;
  return b.p;
}

/* ------------------------------------------------------------------ npbind.dat (npbind.py) */

typedef struct { uint32_t h[5]; uint64_t total; uint8_t buf[64]; size_t fill; } sha1_t;

static uint32_t rol(uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }

static void
sha1_block(sha1_t *c, const uint8_t *p) {
  uint32_t w[80];
  for(int i = 0; i < 16; i++) w[i] = (uint32_t)p[i * 4] << 24 | (uint32_t)p[i * 4 + 1] << 16 | (uint32_t)p[i * 4 + 2] << 8 | p[i * 4 + 3];
  for(int i = 16; i < 80; i++) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
  uint32_t a = c->h[0], b = c->h[1], cc = c->h[2], d = c->h[3], e = c->h[4];
  for(int i = 0; i < 80; i++) {
    uint32_t f, k;
    if(i < 20)      { f = (b & cc) | (~b & d); k = 0x5A827999u; }
    else if(i < 40) { f = b ^ cc ^ d;          k = 0x6ED9EBA1u; }
    else if(i < 60) { f = (b & cc) | (b & d) | (cc & d); k = 0x8F1BBCDCu; }
    else            { f = b ^ cc ^ d;          k = 0xCA62C1D6u; }
    uint32_t t = rol(a, 5) + f + e + k + w[i];
    e = d; d = cc; cc = rol(b, 30); b = a; a = t;
  }
  c->h[0] += a; c->h[1] += b; c->h[2] += cc; c->h[3] += d; c->h[4] += e;
}

static void
sha1(const uint8_t *data, size_t n, uint8_t out[20]) {
  sha1_t c = { { 0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u }, 0, { 0 }, 0 };
  c.total = n;
  while(n >= 64) { sha1_block(&c, data); data += 64; n -= 64; }
  memcpy(c.buf, data, n);
  c.fill = n;
  c.buf[c.fill++] = 0x80;
  if(c.fill > 56) { memset(c.buf + c.fill, 0, 64 - c.fill); sha1_block(&c, c.buf); c.fill = 0; }
  memset(c.buf + c.fill, 0, 56 - c.fill);
  uint64_t bits = c.total * 8;
  for(int i = 0; i < 8; i++) c.buf[56 + i] = (uint8_t)(bits >> (56 - 8 * i));
  sha1_block(&c, c.buf);
  for(int i = 0; i < 5; i++) { out[i * 4] = (uint8_t)(c.h[i] >> 24); out[i * 4 + 1] = (uint8_t)(c.h[i] >> 16); out[i * 4 + 2] = (uint8_t)(c.h[i] >> 8); out[i * 4 + 3] = (uint8_t)c.h[i]; }
}

int
dimg_npbind_fix(uint8_t *data, size_t len) {
  const size_t head = 0x80, entry = 0x180, digest = 20;
  if(!data || len < head + digest) return -1;
  uint32_t magic = (uint32_t)data[0] << 24 | (uint32_t)data[1] << 16 | (uint32_t)data[2] << 8 | data[3];
  uint32_t version = (uint32_t)data[4] << 24 | (uint32_t)data[5] << 16 | (uint32_t)data[6] << 8 | data[7];
  uint64_t declared = 0, esize = 0, count = 0;
  for(int i = 0; i < 8; i++) { declared = declared << 8 | data[8 + i]; esize = esize << 8 | data[16 + i]; count = count << 8 | data[24 + i]; }
  if(magic != 0xD294A018u || version != 1 || declared != len || esize != entry) return -1;
  if(count == 0 || count > len / entry || head + count * entry + digest != len) return -1;
  uint8_t sum[20];
  sha1(data, len - digest, sum);
  if(!memcmp(sum, data + len - digest, digest)) return 0;
  memcpy(data + len - digest, sum, digest);
  return 1;
}

/* ------------------------------------------------------------------ the dump */

static int
tid_ok(const char *t) {
  if(!t || strlen(t) != 9 || strncmp(t, "CUSA", 4)) return 0;
  for(int i = 4; i < 9; i++) if(!isdigit((unsigned char)t[i])) return 0;
  return 1;
}

/* The param.sfo that ends up in the image: the update's when it has one. The path written to out. */
static void
final_file(const char *base, const char *patch, const char *rel, char *out, size_t out_len) {
  if(patch && patch[0]) {
    snprintf(out, out_len, "%s/%s", patch, rel);
    if(is_file(out)) return;
  }
  snprintf(out, out_len, "%s/%s", base, rel);
}

int
dimg_check(const char *base, const char *patch, dimg_info_t *info, char *err, size_t err_len) {
  char p[1100];
  memset(info, 0, sizeof(*info));
  if(!base || !is_dir(base)) { snprintf(err, err_len, "Der Dump-Ordner ist nicht mehr da."); return -1; }
  if(patch && patch[0] && !is_dir(patch)) { snprintf(err, err_len, "Der Ordner des Updates ist nicht mehr da."); return -1; }
  snprintf(p, sizeof(p), "%s/eboot.bin", base);
  if(!is_file(p)) { snprintf(err, err_len, "Im Dump fehlt die eboot.bin."); return -1; }

  char base_tid[16] = "";
  snprintf(p, sizeof(p), "%s/sce_sys/param.sfo", base);
  size_t n = 0;
  uint8_t *b = read_file(p, SFO_MAX, &n, err, err_len);
  if(!b) {
    snprintf(err, err_len, "Im Dump fehlt sce_sys/param.sfo. Ein Dump ohne diese Datei lässt sich nicht einbinden — den "
             "Dump nach der Korrektur dieser App noch einmal machen.");
    return -1;
  }
  sfo_t s;
  if(sfo_open(&s, b, n) != 0) { free(b); snprintf(err, err_len, "Die param.sfo des Dumps ist beschädigt."); return -1; }
  sfo_get_text(&s, "TITLE_ID", base_tid, sizeof(base_tid));
  free(b);
  if(!tid_ok(base_tid)) {
    snprintf(err, err_len, "Das ist kein PS4-Dump: Die param.sfo nennt die Titel-ID „%s“.", base_tid);
    return -1;
  }

  final_file(base, patch, "sce_sys/param.sfo", p, sizeof(p));
  b = read_file(p, SFO_MAX, &n, err, err_len);
  if(!b) return -1;
  if(sfo_open(&s, b, n) != 0) { free(b); snprintf(err, err_len, "Die param.sfo des Updates ist beschädigt."); return -1; }
  char tid[16] = "";
  sfo_get_text(&s, "TITLE_ID", tid, sizeof(tid));
  if(strcmp(tid, base_tid) != 0) {
    free(b);
    snprintf(err, err_len, "Dump und Update gehören nicht zusammen (%s und %s).", base_tid, tid);
    return -1;
  }
  sfo_get_text(&s, "CONTENT_ID", info->content_id, sizeof(info->content_id));
  if(info->content_id[0] && !strstr(info->content_id, tid)) {
    free(b);
    snprintf(err, err_len, "Die Inhalts-ID %s passt nicht zur Titel-ID %s.", info->content_id, tid);
    return -1;
  }
  title_of(&s, info->title, sizeof(info->title));
  if(sfo_get_text(&s, "APP_VER", info->app_ver, sizeof(info->app_ver)) != 0 || !info->app_ver[0])
    snprintf(info->app_ver, sizeof(info->app_ver), "01.00");
  snprintf(info->title_id, sizeof(info->title_id), "%.9s", tid);
  info->have_patch = patch && patch[0];
  free(b);
  return 0;
}

int
dimg_write_overlay(const char *base, const char *patch, const char *overlay_dir, const dimg_info_t *info,
                   int *npbind_repaired, char *err, size_t err_len) {
  char p[1100], dst[1100];
  size_t n = 0;
  *npbind_repaired = 0;
  final_file(base, patch, "sce_sys/param.sfo", p, sizeof(p));
  uint8_t *sfo = read_file(p, SFO_MAX, &n, err, err_len);
  if(!sfo) return -1;
  size_t jl = 0;
  char *json = dimg_param_json(sfo, n, info->title_id, info->title, &jl);
  free(sfo);
  if(!json) { snprintf(err, err_len, "Die param.json ließ sich nicht aus der param.sfo bauen."); return -1; }

  if(snprintf(dst, sizeof(dst), "%s/sce_sys", overlay_dir) >= (int)sizeof(dst) || mkdir_p(dst) != 0) {
    free(json);
    snprintf(err, err_len, "Der Ordner für die zusätzlichen Dateien ließ sich nicht anlegen.");
    return -1;
  }
  snprintf(dst, sizeof(dst), "%s/sce_sys/param.json", overlay_dir);
  int wrc = write_atomic(dst, json, jl);
  free(json);
  if(wrc != 0) { snprintf(err, err_len, "Die param.json ließ sich nicht schreiben."); return -1; }

  snprintf(dst, sizeof(dst), "%s/sce_sys/npbind.dat", overlay_dir);
  unlink(dst);
  final_file(base, patch, "sce_sys/npbind.dat", p, sizeof(p));
  if(is_file(p)) {
    char e2[200];
    uint8_t *nb = read_file(p, NPBIND_MAX, &n, e2, sizeof(e2));
    if(nb) {
      if(dimg_npbind_fix(nb, n) == 1) {
        if(write_atomic(dst, nb, n) != 0) {
          free(nb);
          snprintf(err, err_len, "Die korrigierte npbind.dat ließ sich nicht schreiben.");
          return -1;
        }
        *npbind_repaired = 1;
      }
      free(nb);
    }
  }
  return 0;
}

void
dimg_remove_overlay(const char *overlay_dir) {
  char p[1100];
  snprintf(p, sizeof(p), "%s/sce_sys/param.json", overlay_dir);
  unlink(p);
  snprintf(p, sizeof(p), "%s/sce_sys/npbind.dat", overlay_dir);
  unlink(p);
  snprintf(p, sizeof(p), "%s/sce_sys", overlay_dir);
  rmdir(p);
  rmdir(overlay_dir);
}

void
dimg_stem(const char *title, const char *title_id, char *out, size_t out_len) {
  char t[128];
  size_t o = 0;
  int space = 1;                                   /* no leading blank, none doubled */
  for(const unsigned char *p = (const unsigned char *)title; *p && o < 100; p++) {
    if(o >= 90 && (*p < 0x80 || *p >= 0xC0)) break;     /* stop between characters, never inside one */
    if(*p == ' ' || *p == '	') {
      if(!space) t[o++] = ' ';
      space = 1;
      continue;
    }
    if(!(*p >= 0x80 || isalnum(*p) || strchr("._-()[]&+'!,#@", *p))) continue;
    t[o++] = (char)*p;
    space = 0;
  }
  while(o > 0 && (t[o - 1] == ' ' || t[o - 1] == '.')) o--;
  t[o] = 0;
  if(t[0]) snprintf(out, out_len, "%s [%s]", t, title_id);
  else snprintf(out, out_len, "%s", title_id);
}
