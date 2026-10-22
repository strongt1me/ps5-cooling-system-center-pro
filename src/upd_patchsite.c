/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Knutwurst (PatchDL 0.3.4), adapted for PS5 Cooling & System Center - Pro. See upd_patchsite.h. */

#include "upd_patchsite.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "upd_http.h"
#include "upd_sony.h"

#define PS_BUF_MAX (512 * 1024)

static int
is_ps4(const char *tid) { return !strncmp(tid, "CUSA", 4); }

static const char *
site_host(const char *tid) { return is_ps4(tid) ? "orbispatches.com" : "prosperopatches.com"; }

/* "10.20.00.06" -> 0x10200006: hex digits read as decimal, like the system's own version numbers */
static uint32_t
fw_str_to_bin(const char *s) {
  unsigned a = 0, b = 0, c = 0, d = 0;
  sscanf(s, "%x.%x.%x.%x", &a, &b, &c, &d);
  return (a << 24) | (b << 16) | (c << 8) | d;
}

static void
fw_bin_to_text(uint32_t bin, char *out, size_t n) {
  snprintf(out, n, "%x.%02x", (bin >> 24) & 0xff, (bin >> 16) & 0xff);
}

/* The key on the title page: data-key="<hex>" on Prospero, 'key': '<hex>' on Orbis. */
static int
extract_key(const char *html, char *out, size_t n) {
  const char *p = strstr(html, "data-key=\"");
  size_t len = 0;
  if(p) {
    p += 10;
    while(p[len] && p[len] != '"' && isxdigit((unsigned char)p[len]) && len < n - 1) len++;
    if(!len || p[len] != '"') return -1;
  } else {
    if(!(p = strstr(html, "'key': '"))) return -1;
    p += 8;
    while(isxdigit((unsigned char)p[len]) && len < n - 1) len++;
    if(!len || p[len] != '\'') return -1;
  }
  memcpy(out, p, len);
  out[len] = 0;
  return 0;
}

/* The string value of "key" inside one entry: crude, but the answer is predictable. */
static void
json_str(const char *json, const char *key, char *out, size_t n) {
  char needle[40];
  out[0] = 0;
  snprintf(needle, sizeof needle, "\"%s\"", key);
  const char *p = strstr(json, needle);
  if(!p) return;
  p += strlen(needle);
  while(*p == ' ' || *p == ':') p++;
  if(*p != '"') return;
  p++;
  size_t len = 0;
  while(p[len] && p[len] != '"' && len < n - 1) len++;
  memcpy(out, p, len);
  out[len] = 0;
}

/* Text of the sites is foreign: only plain characters are kept (it goes to the page and into the log). */
static void
plain(char *s) {
  for(; *s; s++)
    if(!isalnum((unsigned char)*s) && *s != '.' && *s != ' ' && *s != '-') *s = '?';
}

static int
fetch(const char *method, const char *url, const char *body, char **out, char *why, size_t wn) {
  upd_req_t r;
  memset(&r, 0, sizeof r);
  r.method = method;
  r.url = url;
  r.range_from = -1;
  r.trust = UPD_TRUST_PUBLIC;
  r.max_redirects = 2;
  r.connect_ms = 10000;
  r.idle_ms = 15000;
  r.total_ms = 40000;
  if(body) {                                     /* JSON in the body, form type on the header: how the site's own page calls it */
    r.content_type = "application/x-www-form-urlencoded; charset=UTF-8";
    r.body = body;
    r.body_len = strlen(body);
  }
  size_t len = 0;
  upd_res_t res;
  if(upd_http_fetch(&r, PS_BUF_MAX, out, &len, &res) != 0) {
    if(res.status) snprintf(why, wn, "http %d", res.status);
    else snprintf(why, wn, "%s", res.step[0] ? res.step : "network");
    return -1;
  }
  return 0;
}

int
upd_patchsite_versions(const char *tid, upd_pver_t *out, int max, int *count, char *why, size_t wn) {
  *count = 0;
  why[0] = 0;
  if(!tid || strlen(tid) != 9 || !out || max <= 0) { snprintf(why, wn, "bad_title"); return -1; }
  int ps4 = is_ps4(tid);
  char url[96], key[128], body[200];
  char *html = NULL, *js = NULL;

  snprintf(url, sizeof url, "https://%s/%s", site_host(tid), tid);
  if(fetch("GET", url, NULL, &html, why, wn) != 0) return -1;
  int kr = extract_key(html, key, sizeof key);
  free(html);
  if(kr != 0) { snprintf(why, wn, "no_key"); return -1; }

  snprintf(body, sizeof body, "{\"titleid\":\"%s\",\"key\":\"%s\"}", tid, key);
  snprintf(url, sizeof url, "https://%s/api/internal/loadpatches", site_host(tid));
  if(fetch("POST", url, body, &js, why, wn) != 0) return -1;
  const char *ok = strstr(js, "\"success\"");
  if(ok) { ok += 9; while(*ok == ' ' || *ok == ':') ok++; }
  if(!ok || strncmp(ok, "true", 4)) { free(js); snprintf(why, wn, "no_list"); return -1; }

  int n = 0;
  const char *cur, *next;
  for(cur = strstr(js, "\"is_latest\""); cur && n < max; cur = next) {
    char entry[2048], fw[32];
    next = strstr(cur + 1, "\"is_latest\"");
    size_t len = next ? (size_t)(next - cur) : strlen(cur);
    if(len >= sizeof entry) len = sizeof entry - 1;
    memcpy(entry, cur, len);
    entry[len] = 0;
    upd_pver_t *v = &out[n];
    memset(v, 0, sizeof *v);
    json_str(entry, "content_ver", v->version, sizeof v->version);
    if(!v->version[0]) json_str(entry, "version", v->version, sizeof v->version);
    json_str(entry, "filesize", v->size, sizeof v->size);
    json_str(entry, "required_firmware", fw, sizeof fw);
    plain(v->version);
    plain(v->size);
    plain(fw);
    if(!v->version[0]) continue;
    if(!ps4) {
      /* "07.01.01.00": without a readable requirement the version counts as needing a newer firmware than any console
         has, so it is never offered on a guess */
      if(strchr(fw, '.') && strchr(strchr(fw, '.') + 1, '.')) {
        v->fw_bin = fw_str_to_bin(fw);
        fw_bin_to_text(v->fw_bin, v->required_fw, sizeof v->required_fw);
      } else {
        v->fw_bin = 0xFFFFFFFFu;
        snprintf(v->required_fw, sizeof v->required_fw, "?");
      }
    } else {
      snprintf(v->required_fw, sizeof v->required_fw, "%s", fw);
    }
    n++;
  }
  free(js);
  *count = n;
  return 0;
}

const char *
upd_patchsite_link(const char *tid, const char *in, char *canon, size_t sz) {
  static const char *const ends[] = { "_sc.pkg", ".crc", ".json", NULL };
  const char *p, *rest, *base, *stem_end = NULL;
  size_t n = in ? strlen(in) : 0;
  int ps4_piece = 0;
  char tag[16], host[128];

  if(!tid || strlen(tid) != 9 || n < 12 || n > 600) return "url_invalid";
  for(p = in; *p; p++)
    if(!isalnum((unsigned char)*p) && !strchr("-._~:/", *p)) return "url_invalid";
  if(strstr(in, "..")) return "url_invalid";
  if(!strncmp(in, "https://", 8)) rest = in + 8;
  else if(!strncmp(in, "http://", 7)) rest = in + 7;
  else return "url_not_allowed";
  const char *slash = strchr(rest, '/');
  if(!slash || slash == rest || (size_t)(slash - rest) >= sizeof host) return "url_not_allowed";
  memcpy(host, rest, (size_t)(slash - rest));
  host[slash - rest] = 0;
  if(strchr(host, ':') || !upd_sony_host_ok(host)) return "url_not_allowed";

  for(int i = 0; ends[i]; i++) {
    size_t el = strlen(ends[i]);
    if(n > el && !strcmp(in + n - el, ends[i])) { stem_end = in + n - el; break; }
  }
  /* PS4: the pieces sit in the manifest's folder on gs2 as "<name>_0.pkg" ... or "<name>.pkg" */
  if(!stem_end && !strcasecmp(host, "gs2.ww.prod.dl.playstation.net") && n > 5 && !strcmp(in + n - 4, ".pkg")) {
    const char *d = in + n - 4;
    while(d > in && isdigit((unsigned char)d[-1])) d--;
    stem_end = (d < in + n - 4 && d > in && d[-1] == '_') ? d - 1 : in + n - 4;
    ps4_piece = 1;
  }
  if(!stem_end) return "not_an_sc_link";
  base = strrchr(in, '/') + 1;
  if(stem_end <= base) return "not_an_sc_link";

  /* the folder ".../<TID>_00/..." and the name "<publisher>-<TID>_00-..." must both name this game */
  snprintf(tag, sizeof tag, "/%.9s_", tid);
  if(!strstr(in, tag)) return "url_title_mismatch";
  tag[0] = '-';
  if(!strstr(base, tag)) return "url_title_mismatch";

  /* the manifest is built anew from the checked parts; PS4 manifests exist over plain http on gs2 only */
  int k = snprintf(canon, sz, "%s%.*s.json", ps4_piece ? "http://" : "https://", (int)(stem_end - rest), rest);
  if(k < 0 || (size_t)k >= sz) return "url_too_long";
  return NULL;
}
