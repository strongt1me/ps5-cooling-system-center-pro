/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Knutwurst (PatchDL 0.3.0), adapted for PS5 Cooling & System Center - Pro
 *
 * Taken over from PatchDL (src/patchdl_resolve.c, src/patchdl_verxml.c, the manifest and piece parts of
 * src/patchdl_net.c): the host list, the PS4 update key and URL, the reading of version.xml (content_ver / version,
 * system_ver, manifest_url before delta_url, the title ids), the manifest checks (in order, contiguous, size caps) and the
 * resumed piece (206 at the right offset, else start over). Changed: the prefix upd_, HTTP through upd_http.c (BearSSL),
 * hashes with the app's SHA-256 and BearSSL's SHA-1 / HMAC instead of OpenSSL, a piece is fetched as a byte range so that
 * several connections share one piece (PatchDL hands out whole pieces). See upd_sony.h. */

#include "upd_sony.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include <bearssl.h>

#include "sha256.h"
#include "upd_http.h"

/* Manifest sanity caps - reject anything bigger than a real patch (the largest seen: about 70 GB in 18 pieces). */
#define MAX_PIECES        4096
#define MAX_PIECE_BYTES   (8LL * 1024 * 1024 * 1024)
#define MAX_TOTAL_BYTES   (200LL * 1024 * 1024 * 1024)
#define BUF_MAX_VERXML    (4u * 1024 * 1024)
#define BUF_MAX_MANIFEST  (16u * 1024 * 1024)

static const char *const ALLOWED_HOSTS[] = {
  "sgst.prod.dl.playstation.net",
  "gst.prod.dl.playstation.net",
  "gs2.ww.prod.dl.playstation.net",
  "gs-sec.ww.np.dl.playstation.net",   /* PS4 update lists */
  NULL,
};

/* PS4 package manifests are published on gs2 over plain http only (https there is a CloudFront edge without Sony's
   certificate). The console's own installer checks the package signature, so a forged manifest can't get an unsigned
   package installed; every piece is checked against the manifest's hash besides. Everything else stays https-only. */
#define PLAIN_HTTP_HOST "gs2.ww.prod.dl.playstation.net"

int
upd_sony_host_ok(const char *host) {
  size_t hlen = strlen(host);
  for(int i = 0; ALLOWED_HOSTS[i]; i++) {
    if(!strcasecmp(host, ALLOWED_HOSTS[i])) return 1;
    size_t alen = strlen(ALLOWED_HOSTS[i]);
    if(hlen > alen + 1 && host[hlen - alen - 1] == '.' && !strcasecmp(host + hlen - alen, ALLOWED_HOSTS[i])) return 1;
  }
  return 0;
}

int
upd_sony_hop_ok(const char *host, int https, void *ctx) {
  (void)ctx;
  return upd_sony_host_ok(host) && (https || !strcasecmp(host, PLAIN_HTTP_HOST));
}

int
upd_sony_piece_hop_ok(const char *host, int https, void *ctx) {
  (void)ctx;
  (void)https;
  return upd_sony_host_ok(host);
}

/* ---------------------------------------------------------------- PS4 update list URL */

/* PS4 titles: Sony's update list lives at a path derived from the title id with HMAC-SHA256 under a key that is part of
   the PS4 system software and widely published (it's how every PS4 update checker finds patches). */
static const unsigned char PS4_UPDATE_KEY[32] = {
  0xAD, 0x62, 0xE3, 0x7F, 0x90, 0x5E, 0x06, 0xBC, 0x19, 0x59, 0x31, 0x42,
  0x28, 0x1C, 0x11, 0x2C, 0xEC, 0x0E, 0x7E, 0xC3, 0xE9, 0x7E, 0xFD, 0xCA,
  0xEF, 0xCD, 0xBA, 0xAF, 0xA6, 0x37, 0x8D, 0x84,
};

int
upd_sony_ps4_url(const char *title_id, char *url, size_t n) {
  if(!title_id || strlen(title_id) != 9 || strncmp(title_id, "CUSA", 4)) return -1;
  for(int i = 4; i < 9; i++)
    if(!isdigit((unsigned char)title_id[i])) return -1;
  char msg[24];
  int ml = snprintf(msg, sizeof msg, "np_%s", title_id);
  br_hmac_key_context kc;
  br_hmac_context hc;
  unsigned char mac[32];
  br_hmac_key_init(&kc, &br_sha256_vtable, PS4_UPDATE_KEY, sizeof PS4_UPDATE_KEY);
  br_hmac_init(&hc, &kc, 0);
  br_hmac_update(&hc, msg, (size_t)ml);
  br_hmac_out(&hc, mac);
  char hex[65];
  for(int i = 0; i < 32; i++) snprintf(hex + 2 * i, 3, "%02x", mac[i]);
  int k = snprintf(url, n, "https://gs-sec.ww.np.dl.playstation.net/plo/np/%s/%s/%s-ver.xml", title_id, hex, title_id);
  return k > 0 && (size_t)k < n ? 0 : -1;
}

/* ---------------------------------------------------------------- version.xml */

void
upd_fw_text(uint32_t bin, char *out, size_t n) {
  /* packed firmware: hex digits read as decimal (0x10200006 -> "10.20") */
  snprintf(out, n, "%x.%02x", (bin >> 24) & 0xff, (bin >> 16) & 0xff);
}

static uint32_t
parse_hex(const char *s) {
  unsigned v = 0;
  if(s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) sscanf(s + 2, "%x", &v);
  else sscanf(s, "%u", &v);
  return v;
}

/* The value of attr inside one tag [tag_start, tag_end). 0, or -1 (also when it does not end on its quote). */
static int
attr_val(const char *tag_start, const char *tag_end, const char *attr, char *out, size_t out_sz) {
  char needle[64];
  if((size_t)snprintf(needle, sizeof needle, " %s=\"", attr) >= sizeof needle) return -1;
  const char *p = tag_start;
  while(p < tag_end) {
    p = strstr(p, needle);
    if(!p || p >= tag_end) return -1;
    p += strlen(needle);
    size_t len;
    for(len = 0; p + len < tag_end && p[len] != '"' && len < out_sz - 1; len++)
      ;
    if(p + len >= tag_end || p[len] != '"') return -1;
    memcpy(out, p, len);
    out[len] = '\0';
    return 0;
  }
  return -1;
}

/* version strings are zero-padded ("01.041.000"): strcmp orders them */
static int ver_gt(const char *a, const char *b) { return strcmp(a, b) > 0; }

/* the first [A-Z]{4}[0-9]{5} in s */
static void
extract_title_id(const char *s, char *out, size_t sz) {
  out[0] = '\0';
  if(sz < 10 || !s) return;
  size_t len = strlen(s);
  if(len < 9) return;
  for(size_t i = 0; i <= len - 9; i++) {
    const char *p = s + i;
    int ok = 1;
    for(int k = 0; k < 4 && ok; k++) if(p[k] < 'A' || p[k] > 'Z') ok = 0;
    for(int k = 4; k < 9 && ok; k++) if(p[k] < '0' || p[k] > '9') ok = 0;
    if(ok) { memcpy(out, p, 9); out[9] = '\0'; return; }
  }
}

void
upd_sony_parse_verxml(const char *xml, uint32_t fw_bin, upd_verinfo_t *out) {
  memset(out, 0, sizeof *out);
  if(!xml) return;
  char root_title[16] = "";
  const char *rp = strstr(xml, "<title_patch");
  if(rp) {
    const char *te = strchr(rp, '>');
    char np[64] = "";
    if(te && !attr_val(rp, te + 1, "nptitleid", np, sizeof np)) extract_title_id(np, root_title, sizeof root_title);
  }
  const char *p = xml;
  while((p = strstr(p, "<package "))) {
    const char *tag_end = strchr(p, '>');
    if(!tag_end) break;
    tag_end++;
    char ver[16] = "", sver[16] = "", durl[512] = "", murl[512] = "";
    /* PS5 version.xml uses content_ver; PS4 uses version */
    int is_ps4 = attr_val(p, tag_end, "content_ver", ver, sizeof ver) != 0;
    if(is_ps4) attr_val(p, tag_end, "version", ver, sizeof ver);
    attr_val(p, tag_end, "system_ver", sver, sizeof sver);
    attr_val(p, tag_end, "delta_url", durl, sizeof durl);
    attr_val(p, tag_end, "manifest_url", murl, sizeof murl);
    if(ver[0] && (sver[0] || is_ps4)) {
      /* a PS4 patch's system_ver is a PS4 firmware: the PS5 runs PS4 games regardless */
      uint32_t pkg_sver = is_ps4 ? 0 : parse_hex(sver);
      if(!out->latest_version[0] || ver_gt(ver, out->latest_version)) {
        snprintf(out->latest_version, sizeof out->latest_version, "%s", ver);
        if(is_ps4) out->latest_required_fw[0] = '\0';
        else upd_fw_text(pkg_sver, out->latest_required_fw, sizeof out->latest_required_fw);
      }
      /* PS5 updates expose a small delta_url (DP.pkg) plus a manifest_url with the real split package: the manifest is
         preferred (feeding the DP bootstrap can make the system fetch the patch under the storage title id) */
      if(pkg_sver <= fw_bin && (!out->compatible_version[0] || ver_gt(ver, out->compatible_version))) {
        snprintf(out->compatible_version, sizeof out->compatible_version, "%s", ver);
        snprintf(out->compatible_url, sizeof out->compatible_url, "%s", murl[0] ? murl : durl);
        if(durl[0]) snprintf(out->delta_url, sizeof out->delta_url, "%s", durl);
        extract_title_id(durl, out->compatible_storage_title, sizeof out->compatible_storage_title);
        if(root_title[0]) snprintf(out->compatible_title, sizeof out->compatible_title, "%s", root_title);
        else {
          extract_title_id(murl, out->compatible_title, sizeof out->compatible_title);
          if(!out->compatible_title[0]) extract_title_id(durl, out->compatible_title, sizeof out->compatible_title);
        }
      }
    }
    p = tag_end;
  }
}

static void
std_req(upd_req_t *r, const char *url) {
  memset(r, 0, sizeof *r);
  r->url = url;
  r->range_from = -1;
  r->trust = UPD_TRUST_SONY;
  r->hop_ok = upd_sony_hop_ok;
  r->max_redirects = 3;
  r->connect_ms = 10000;
  r->idle_ms = 15000;
  r->total_ms = 30000;
}

int
upd_sony_verxml(const char *url, uint32_t fw_bin, upd_verinfo_t *out, char *why, size_t wn) {
  memset(out, 0, sizeof *out);
  if(why && wn) why[0] = 0;
  char *buf = NULL;
  size_t len = 0;
  upd_req_t r;
  upd_res_t res;
  std_req(&r, url);
  if(upd_http_fetch(&r, BUF_MAX_VERXML, &buf, &len, &res) != 0) {
    if(why) snprintf(why, wn, "%s%s%d", res.step[0] ? res.step : "http_status", res.status ? " " : "", res.status);
    return -1;
  }
  upd_sony_parse_verxml(buf, fw_bin, out);
  free(buf);
  if(!out->latest_version[0] && why) snprintf(why, wn, "no_versions");
  return out->latest_version[0] ? 0 : -1;
}

/* ---------------------------------------------------------------- the manifest */

static const char *
strstr_bounded(const char *p, const char *needle, const char *limit) {
  const char *hit = strstr(p, needle);
  if(!hit || (limit && hit >= limit)) return NULL;
  return hit;
}

/* "key": "value" from p, bounded by limit (NULL: to the end). Decodes \\ \" \/ \n \r \t \b \f; other escapes keep their
   character. */
static int
json_string_after(const char *p, const char *key, char *out, size_t out_sz, const char *limit) {
  char needle[48];
  size_t n = 0;
  if(!p || !out || out_sz == 0) return -1;
  out[0] = '\0';
  snprintf(needle, sizeof needle, "\"%s\"", key);
  const char *q = strstr_bounded(p, needle, limit);
  if(!q) return -1;
  q += strlen(needle);
  while((!limit || q < limit) && (*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n')) q++;
  if(limit && q >= limit) return -1;
  if(*q++ != ':') return -1;
  while((!limit || q < limit) && (*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n')) q++;
  if(limit && q >= limit) return -1;
  if(*q++ != '"') return -1;
  while((!limit || q < limit) && *q && *q != '"' && n + 1 < out_sz) {
    if(*q == '\\' && q[1] && (!limit || q + 1 < limit)) {
      q++;
      switch(*q) {
      case 'n': out[n++] = '\n'; break;
      case 'r': out[n++] = '\r'; break;
      case 't': out[n++] = '\t'; break;
      case 'b': out[n++] = '\b'; break;
      case 'f': out[n++] = '\f'; break;
      default:  out[n++] = *q;   break;
      }
      q++;
    } else out[n++] = *q++;
  }
  out[n] = '\0';
  return n ? 0 : -1;
}

static int
json_u64_after(const char *p, const char *key, unsigned long long *out, const char *limit) {
  char needle[48];
  if(!p || !out) return -1;
  snprintf(needle, sizeof needle, "\"%s\"", key);
  const char *q = strstr_bounded(p, needle, limit);
  if(!q) return -1;
  q += strlen(needle);
  while((!limit || q < limit) && (*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n')) q++;
  if(limit && q >= limit) return -1;
  if(*q++ != ':') return -1;
  while((!limit || q < limit) && (*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n')) q++;
  if(limit && q >= limit) return -1;
  if(*q < '0' || *q > '9') return -1;
  *out = strtoull(q, NULL, 10);
  return 0;
}

void
upd_manifest_free(upd_manifest_t *m) {
  if(!m || !m->pieces) return;
  for(int i = 0; i < m->count; i++) free(m->pieces[i].url);
  free(m->pieces);
  m->pieces = NULL;
  m->count = 0;
}

static int
hex_ok(const char *h) {
  size_t n = strlen(h);
  if(n != 40 && n != 64) return 0;
  for(size_t i = 0; i < n; i++)
    if(!isxdigit((unsigned char)h[i])) return 0;
  return 1;
}

int
upd_sony_parse_manifest(const char *json, upd_manifest_t *out) {
  memset(out, 0, sizeof *out);
  if(!json) return -1;
  const char *pieces = strstr(json, "\"pieces\"");
  if(!pieces || !(pieces = strchr(pieces, '['))) return -1;
  /* bound the scan to the pieces array: a later "url" key (playgoChunkCrcUrl) must not become a piece */
  const char *pieces_end = strchr(pieces, ']');
  int cap = 0, n = 0;
  int64_t running = 0;
  for(const char *p = pieces; (p = strstr(p, "\"url\"")) && (!pieces_end || p < pieces_end); p += 5) cap++;
  if(cap <= 0 || cap > MAX_PIECES) return -1;
  out->pieces = calloc((size_t)cap, sizeof(upd_piece_t));
  if(!out->pieces) return -1;
  const char *p = pieces;
  while((p = strstr(p, "\"url\"")) && (!pieces_end || p < pieces_end) && n < cap) {
    char url[768] = "";
    unsigned long long sz = 0, off = 0;
    const char *obj_end = strchr(p, '}');
    const char *limit = (obj_end && (!pieces_end || obj_end < pieces_end)) ? obj_end : pieces_end;
    if(json_string_after(p, "url", url, sizeof url, limit)) break;
    json_u64_after(p, "fileSize", &sz, limit);
    if(json_u64_after(p, "fileOffset", &off, limit) != 0) off = (unsigned long long)running;   /* no offset: contiguous */
    /* in order, contiguous, non-empty, each under the per-piece cap */
    if((int64_t)off != running || sz == 0 || sz > (unsigned long long)MAX_PIECE_BYTES || (unsigned long long)running + sz > (unsigned long long)MAX_TOTAL_BYTES) {
      upd_manifest_free(out);
      return -1;
    }
    out->pieces[n].url = strdup(url);
    out->pieces[n].offset = (int64_t)off;
    out->pieces[n].size = (int64_t)sz;
    json_string_after(p, "hashValue", out->pieces[n].hash, sizeof out->pieces[n].hash, limit);
    if(out->pieces[n].hash[0] && !hex_ok(out->pieces[n].hash)) out->pieces[n].hash[0] = 0;
    if(!out->pieces[n].url) { upd_manifest_free(out); return -1; }
    running += (int64_t)sz;
    n++;
    out->count = n;
    p = obj_end ? obj_end + 1 : p + 5;
  }
  json_string_after(json, "playgoChunkCrcUrl", out->crc_url, sizeof out->crc_url, NULL);
  if(n == 0) { upd_manifest_free(out); return -1; }
  out->total = running;
  return 0;
}

int
upd_sony_manifest(const char *url, upd_manifest_t *out, char *why, size_t wn) {
  static const unsigned pause_s[] = { 2, 5 };
  memset(out, 0, sizeof *out);
  if(why && wn) why[0] = 0;
  char *buf = NULL;
  size_t len = 0;
  upd_res_t res;
  int got = -1;
  /* a request that failed on the network is tried twice more after a pause, each time at another CDN address */
  for(int attempt = 0; attempt < 3; attempt++) {
    if(attempt) {
      if(strcmp(res.step, "connect") && strcmp(res.step, "timeout") && strcmp(res.step, "network")) break;
      sleep(pause_s[attempt - 1]);
    }
    upd_req_t r;
    std_req(&r, url);
    r.total_ms = 60000;
    if((got = upd_http_fetch(&r, BUF_MAX_MANIFEST, &buf, &len, &res)) == 0) break;
  }
  if(got != 0) {
    if(why) snprintf(why, wn, "%s%s%d", res.step[0] ? res.step : "http_status", res.status ? " " : "", res.status);
    return -1;
  }
  int rc = upd_sony_parse_manifest(buf, out);
  free(buf);
  if(rc != 0 && why) snprintf(why, wn, "bad_manifest");
  return rc;
}

/* ---------------------------------------------------------------- one byte range of a piece */

/* What came in is collected and written in blocks of 2 MiB instead of one pwrite per network read (a few KiB): the
   idea of PatchDL 0.3.1, which went from 8 to 117 MB/s on a gigabit line with it (19.10.2026); 2 MiB as in 0.3.3. */
#define SINK_BUF (2u << 20)

typedef struct {
  int               fd;
  int64_t           file_off;
  int64_t           want_start;
  int64_t           len;
  int64_t           written;        /* on disk */
  int64_t           got;            /* received (on disk + in buf) */
  int               bad_range;
  int               write_failed;
  volatile int64_t *done;
  unsigned char    *buf;
  size_t            buf_n;
} sink_t;

static int
sink_flush(sink_t *s) {
  size_t off = 0;
  while(off < s->buf_n) {
    ssize_t w = pwrite(s->fd, s->buf + off, s->buf_n - off, (off_t)(s->file_off + s->written));
    if(w <= 0) { if(w < 0 && errno == EINTR) continue; s->write_failed = 1; return -1; }
    off += (size_t)w;
    s->written += w;
  }
  s->buf_n = 0;
  return 0;
}

static int
range_head(void *ctx, const upd_res_t *res) {
  sink_t *s = ctx;
  /* the server must answer the range asked for; a 200 with the whole piece would be the wrong bytes at this offset unless
     the range starts at 0 */
  if(res->status == 206) {
    if(res->range_start != s->want_start) { s->bad_range = 1; return 1; }
  } else if(res->status == 200) {
    if(s->want_start != 0) { s->bad_range = 1; return 1; }
  }
  return 0;
}

static int
range_body(void *ctx, const void *data, size_t n) {
  sink_t *s = ctx;
  if(s->got + (int64_t)n > s->len) n = (size_t)(s->len - s->got);   /* a 200 for the whole piece: the rest is not ours */
  if(n == 0) return 1;
  const unsigned char *p = data;
  while(n) {
    size_t take = SINK_BUF - s->buf_n;
    if(take > n) take = n;
    memcpy(s->buf + s->buf_n, p, take);
    s->buf_n += take;
    s->got += (int64_t)take;
    p += take;
    n -= take;
    if((s->buf_n == SINK_BUF || s->got >= s->len) && sink_flush(s) != 0) return 1;
  }
  if(s->done) *s->done = s->got;
  return s->got >= s->len;          /* stop once the range is complete (the server may send more) */
}

int
upd_sony_fetch_range(const char *url, int fd, int64_t file_off, int64_t from, int64_t len,
                     volatile int64_t *done, volatile int *abort, char *why, size_t wn) {
  if(why && wn) why[0] = 0;
  if(len <= 0 || from < 0) return -1;
  sink_t s;
  memset(&s, 0, sizeof s);
  s.fd = fd;
  s.file_off = file_off;
  s.want_start = from;
  s.len = len;
  s.done = done;
  s.buf = malloc(SINK_BUF);
  if(!s.buf) { if(why) snprintf(why, wn, "no_memory"); return -1; }
  if(done) *done = 0;
  upd_req_t r;
  memset(&r, 0, sizeof r);
  r.url = url;
  r.range_from = from;
  r.range_to = from + len - 1;
  r.trust = UPD_TRUST_SONY;
  r.hop_ok = upd_sony_piece_hop_ok;
  r.max_redirects = 5;
  r.connect_ms = 20000;
  r.idle_ms = 30000;                /* PatchDL: less than 1 KiB/s for 30 s is a stall */
  r.on_head = range_head;
  r.on_body = range_body;
  r.ctx = &s;
  r.abort = abort;
  upd_res_t res;
  int rc = upd_http(&r, &res);
  free(s.buf);
  if(s.bad_range) { if(why) snprintf(why, wn, "bad_range"); return -3; }
  if(s.write_failed) { if(why) snprintf(why, wn, "write"); return -1; }
  if(s.written == len && (rc == 0 || !strcmp(res.step, "aborted")) && (res.status == 200 || res.status == 206)) {
    if(fdatasync(fd) != 0) { if(why) snprintf(why, wn, "write"); return -1; }
    return 0;
  }
  if(res.status == 416) { if(why) snprintf(why, wn, "bad_range"); return -3; }
  if(why) snprintf(why, wn, "%s%s%d", res.step[0] ? res.step : "short", res.status ? " " : "", res.status);
  return -1;
}

int
upd_hash_fd(int fd, int64_t off, int64_t size, const char *like_hex, char *out, volatile int *abort) {
  out[0] = 0;
  if(fd < 0 || size < 0) return -1;
  int sha1 = like_hex && strlen(like_hex) == 40;
  ps5tm_sha256_t s256;
  br_sha1_context s1;
  if(sha1) br_sha1_init(&s1); else ps5tm_sha256_init(&s256);
  const size_t CHUNK = 1u << 20;
  unsigned char *buf = malloc(CHUNK);
  if(!buf) return -1;
  while(size > 0) {
    if(abort && *abort) { free(buf); return -2; }
    size_t want = size > (int64_t)CHUNK ? CHUNK : (size_t)size;
    ssize_t got = pread(fd, buf, want, (off_t)off);
    if(got <= 0) { if(got < 0 && errno == EINTR) continue; free(buf); return -1; }
    if(sha1) br_sha1_update(&s1, buf, (size_t)got); else ps5tm_sha256_update(&s256, buf, (size_t)got);
    off += got;
    size -= got;
  }
  free(buf);
  unsigned char dg[32];
  if(sha1) {
    br_sha1_out(&s1, dg);
    for(int i = 0; i < 20; i++) snprintf(out + 2 * i, 3, "%02x", dg[i]);
  } else {
    ps5tm_sha256_final(&s256, dg);
    ps5tm_sha256_hex(dg, out);
  }
  return 0;
}
