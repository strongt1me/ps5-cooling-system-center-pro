/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Knutwurst (PatchDL 0.3.0), adapted for PS5 Cooling & System Center - Pro
 *
 * Taken over from PatchDL (src/patchdl_prospero.c; manifest_from_link and size_mismatch of src/patchdl_websrv.c) with the
 * same steps: the title page for its API key (data-key="..." on Prospero, 'key': '...' on Orbis), POST loadpatches with the
 * JSON body sent as form data (as the site and every client do), each entry bounded by "is_latest", firmware packed like
 * Sony's system_ver. Changed: HTTP through upd_http.c, and the sites' certificates are checked against the public roots
 * (PatchDL switches the check off for them). See upd_site.h. */

#include "upd_site.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "upd_http.h"

#define PROSPERO_BASE "https://prosperopatches.com"
#define ORBIS_BASE    "https://orbispatches.com"
#define BUF_MAX       (512u * 1024)

static uint32_t
fw_str_to_bin(const char *s) {
  unsigned a = 0, b = 0, c = 0, d = 0;
  sscanf(s, "%x.%x.%x.%x", &a, &b, &c, &d);
  return (a << 24) | (b << 16) | (c << 8) | d;
}

const char *
upd_site_base(const char *title_id) {
  return (title_id && !strncmp(title_id, "CUSA", 4)) ? ORBIS_BASE : PROSPERO_BASE;
}

/* the API key on the title page */
static int
extract_key(const char *html, char *out, size_t out_sz) {
  const char *p = strstr(html, "data-key=\"");
  size_t len = 0;
  if(p) {
    p += strlen("data-key=\"");
    while(p[len] && p[len] != '"' && len < out_sz - 1) len++;
    if(len && p[len] == '"') { memcpy(out, p, len); out[len] = 0; goto check; }
  }
  if(!(p = strstr(html, "'key': '"))) return -1;
  p += strlen("'key': '");
  len = 0;
  while(isxdigit((unsigned char)p[len]) && len < out_sz - 1) len++;
  if(!len || p[len] != '\'') return -1;
  memcpy(out, p, len);
  out[len] = 0;
check:
  for(size_t i = 0; out[i]; i++)                   /* it goes back into a JSON body */
    if(!isalnum((unsigned char)out[i])) return -1;
  return 0;
}

static const char *
json_str_val(const char *json, const char *key, char *out, size_t out_sz) {
  char needle[64];
  snprintf(needle, sizeof needle, "\"%s\"", key);
  const char *p = strstr(json, needle);
  if(!p) return NULL;
  p += strlen(needle);
  while(*p == ' ' || *p == ':') p++;
  if(*p != '"') return NULL;
  p++;
  size_t len = 0;
  while(p[len] && p[len] != '"' && len < out_sz - 1) len++;
  memcpy(out, p, len);
  out[len] = '\0';
  return p + len + 1;
}

static int
json_success(const char *json) {
  const char *p = strstr(json, "\"success\"");
  if(!p) return 0;
  p += 9;
  while(*p == ' ' || *p == ':') p++;
  return !strncmp(p, "true", 4);
}

static int
site_hop(const char *host, int https, void *ctx) {
  return https && !strcmp(host, (const char *)ctx);
}

int
upd_site_parse_versions(const char *title_id, const char *json, upd_site_version_t *out, int max) {
  int n = 0;
  if(!json_success(json)) return -1;
  for(const char *cur = strstr(json, "\"is_latest\""), *next; cur && n < max; cur = next) {
    char entry[2048], fw[32] = "";
    next = strstr(cur + 1, "\"is_latest\"");
    size_t len = next ? (size_t)(next - cur) : strlen(cur);
    if(len >= sizeof entry) len = sizeof entry - 1;
    memcpy(entry, cur, len);
    entry[len] = '\0';
    upd_site_version_t *v = &out[n];
    memset(v, 0, sizeof *v);
    if(!json_str_val(entry, "content_ver", v->version, sizeof v->version)) json_str_val(entry, "version", v->version, sizeof v->version);
    json_str_val(entry, "filesize", v->size, sizeof v->size);
    json_str_val(entry, "required_firmware", fw, sizeof fw);
    if(!v->version[0]) continue;
    for(char *c = v->version; *c; c++) if(!isdigit((unsigned char)*c) && *c != '.') *c = '?';
    for(char *c = v->size; *c; c++) if(!isalnum((unsigned char)*c) && *c != '.' && *c != ' ') *c = '?';
    if(strncmp(title_id, "CUSA", 4)) {
      /* PS5 "07.01.01.00", packed like Sony's system_ver. Without a readable requirement the version counts as needing a
         newer firmware than any console has: never offer a guess. */
      if(strchr(fw, '.') && strchr(strchr(fw, '.') + 1, '.')) {
        v->fw_bin = fw_str_to_bin(fw);
        snprintf(v->required_fw, sizeof v->required_fw, "%x.%02x", (v->fw_bin >> 24) & 0xff, (v->fw_bin >> 16) & 0xff);
      } else {
        v->fw_bin = 0xFFFFFFFFu;
        snprintf(v->required_fw, sizeof v->required_fw, "?");
      }
    } else {
      for(char *c = fw; *c; c++) if(!isdigit((unsigned char)*c) && *c != '.') *c = '?';
      snprintf(v->required_fw, sizeof v->required_fw, "%s", fw);   /* PS4 "3.00": never a limit on the PS5 */
      v->fw_bin = 0;
    }
    n++;
  }
  return n;
}

int
upd_site_versions(const char *title_id, upd_site_version_t *out, int max, int *count, char *why, size_t wn) {
  *count = 0;
  if(why && wn) why[0] = 0;
  if(!title_id || !out || max <= 0 || strlen(title_id) != 9) return -1;
  for(int i = 0; i < 9; i++)
    if(!isalnum((unsigned char)title_id[i])) return -1;
  const char *base = upd_site_base(title_id);
  const char *host = base + strlen("https://");
  char url[256], key[128], body[256];
  char *buf = NULL;
  size_t len = 0;
  upd_res_t res;
  upd_req_t r;
  /* step 1: the title page, for the API key */
  memset(&r, 0, sizeof r);
  snprintf(url, sizeof url, "%s/%s", base, title_id);
  r.url = url;
  r.range_from = -1;
  r.trust = UPD_TRUST_PUBLIC;
  r.hop_ok = site_hop;
  r.hop_ctx = (void *)host;
  r.max_redirects = 2;
  r.connect_ms = 8000;
  r.idle_ms = 15000;
  r.total_ms = 30000;
  if(upd_http_fetch(&r, BUF_MAX, &buf, &len, &res) != 0) { if(why) snprintf(why, wn, "page %s %d", res.step, res.status); return -1; }
  int krc = extract_key(buf, key, sizeof key);
  free(buf);
  buf = NULL;
  if(krc) { if(why) snprintf(why, wn, "no_key"); return -1; }
  /* step 2: POST loadpatches. JSON in the body, but sent as form data: that is how the website calls it */
  snprintf(body, sizeof body, "{\"titleid\":\"%s\",\"key\":\"%s\"}", title_id, key);
  snprintf(url, sizeof url, "%s/api/internal/loadpatches", base);
  r.url = url;
  r.method = "POST";
  r.content_type = "application/x-www-form-urlencoded; charset=UTF-8";
  r.body = body;
  r.body_len = strlen(body);
  r.max_redirects = 0;
  if(upd_http_fetch(&r, BUF_MAX, &buf, &len, &res) != 0) { if(why) snprintf(why, wn, "api %s %d", res.step, res.status); return -1; }
  int n = upd_site_parse_versions(title_id, buf, out, max);
  free(buf);
  if(n < 0) { if(why) snprintf(why, wn, "api_failed"); return -1; }
  *count = n;
  return 0;
}

int
upd_site_best(const char *title_id, uint32_t fw_bin, upd_site_version_t *best, char *why, size_t wn) {
  memset(best, 0, sizeof *best);
  upd_site_version_t *list = calloc(UPD_SITE_MAX_VERSIONS, sizeof *list);
  int n = 0, b = -1;
  if(!list) return -1;
  if(upd_site_versions(title_id, list, UPD_SITE_MAX_VERSIONS, &n, why, wn) != 0) { free(list); return -1; }
  for(int i = 0; i < n; i++)
    if(list[i].fw_bin <= fw_bin && (b < 0 || strcmp(list[i].version, list[b].version) > 0)) b = i;
  if(b >= 0) *best = list[b];
  free(list);
  return b >= 0 ? 1 : 0;
}

/* ---------------------------------------------------------------- a link copied off the site */

static int
cdn_host_ok(const char *rest) {
  static const char *const hosts[] = { "sgst.prod.dl.playstation.net/", "gst.prod.dl.playstation.net/", "gs2.ww.prod.dl.playstation.net/", NULL };
  for(int i = 0; hosts[i]; i++)
    if(!strncmp(rest, hosts[i], strlen(hosts[i]))) return 1;
  return 0;
}

const char *
upd_site_manifest_from_link(const char *tid, const char *in, char *canon, size_t sz) {
  static const char *const ends[] = { "_sc.pkg", ".crc", ".json", NULL };
  char tag[16];
  const char *rest, *base, *stem_end = NULL;
  size_t n = strlen(in);
  int ps4_piece = 0;
  /* the URL ends up in JSON and file names, and dot segments could walk the path into another title's folder */
  for(const char *p = in; *p; p++)
    if(!isalnum((unsigned char)*p) && !strchr("-._~:/", *p)) return "url_invalid";
  if(strstr(in, "..")) return "url_invalid";
  if(!strncmp(in, "https://", 8)) rest = in + 8;
  else if(!strncmp(in, "http://", 7)) rest = in + 7;
  else return "url_not_allowed";
  if(!cdn_host_ok(rest)) return "url_not_allowed";
  for(int i = 0; ends[i]; i++) {
    size_t el = strlen(ends[i]);
    if(n > el && !strcmp(in + n - el, ends[i])) { stem_end = in + n - el; break; }
  }
  /* PS4: Orbis Patches lists the pieces, which sit in the same gs2 folder as the manifest "<name>.json": "<name>_0.pkg",
     "_1.pkg", ... for a split package, or just "<name>.pkg" when it is a single piece */
  if(!stem_end && !strncmp(rest, "gs2.ww.prod.dl.playstation.net/", 31) && n > 5 && !strcmp(in + n - 4, ".pkg")) {
    const char *d = in + n - 4;
    while(d > in && isdigit((unsigned char)d[-1])) d--;
    stem_end = (d < in + n - 4 && d > in && d[-1] == '_') ? d - 1 : in + n - 4;
    ps4_piece = 1;
  }
  if(!stem_end) return "not_an_sc_link";
  base = strrchr(in, '/') + 1;
  if(stem_end <= base) return "not_an_sc_link";
  /* Sony keeps a title's files under ".../<TID>_00/..." and names them "<publisher>-<TID>_00-<label>...": both must name
     this title */
  snprintf(tag, sizeof tag, "/%.9s_", tid);
  if(!strstr(in, tag)) return "url_title_mismatch";
  tag[0] = '-';
  if(!strstr(base, tag)) return "url_title_mismatch";
  /* PS5 manifests over https with Sony's root; PS4 manifests only exist over plain http on gs2 */
  if(sizeof("https://") - 1 + (size_t)(stem_end - rest) + sizeof(".json") > sz) return "url_too_long";
  snprintf(canon, sz, "%s%.*s.json", ps4_piece ? "http://" : "https://", (int)(stem_end - rest), rest);
  return NULL;
}

int
upd_site_size_mismatch(const char *label, int64_t found) {
  double v, unit_bytes;
  char unit[8] = "";
  if(!label || sscanf(label, "%lf %7s", &v, unit) != 2) {
    /* "8.7GB" without a space */
    char num[16] = "";
    size_t k = 0;
    if(!label) return 0;
    while(label[k] && (isdigit((unsigned char)label[k]) || label[k] == '.') && k < sizeof num - 1) { num[k] = label[k]; k++; }
    num[k] = 0;
    if(!k || sscanf(num, "%lf", &v) != 1) return 0;
    snprintf(unit, sizeof unit, "%.7s", label + k);
  }
  if(!strcmp(unit, "TB")) unit_bytes = 1099511627776.0;
  else if(!strcmp(unit, "GB")) unit_bytes = 1073741824.0;
  else if(!strcmp(unit, "MB")) unit_bytes = 1048576.0;
  else return 0;
  double expect = v * unit_bytes;
  double diff = (double)found - expect;
  if(diff < 0) diff = -diff;
  double tol = expect * 0.02;
  if(tol < 0.15 * unit_bytes) tol = 0.15 * unit_bytes;
  return diff > tol;
}
