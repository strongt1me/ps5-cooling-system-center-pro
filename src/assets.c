/* The pages of the web interface, as the app serves them (moved out of api.c on 07.10.2026 so a host test can run it
 * without the rest of the app).
 *
 * Text assets are stored gzipped (tools/gen_assets.py). A browser that accepts gzip gets them as they are, with
 * Content-Encoding; one that does not gets them unpacked here with libdeflate (the gzip header is the plain ten
 * bytes the generator writes, the 8-byte trailer is not read: the length is known and checked). Pictures and small
 * files are stored as they are. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ps5tm.h"
#include "third_party/libdeflate/libdeflate.h"

/* What the page may do, said to the browser along with the page itself. The
 * page has no inline script and talks to its own origin only, so the policy
 * costs it nothing — checked by running every page and dialog of it against
 * exactly this header: no violations. What it buys: a script smuggled in
 * through a title's name or a log line is refused by the browser even if some
 * place forgot to escape it, and no other page can frame this one. Styles stay
 * open to inline because the page sets widths and colours through the style
 * attribute. Pictures may also come from github.com: the Credits page loads that site's small icon once to
 * learn whether the browser has internet (only then its links are clickable). */
#define PAGE_HEADERS \
  "Content-Security-Policy: default-src 'self'; script-src 'self'; " \
  "style-src 'self' 'unsafe-inline'; img-src 'self' data: blob: https://github.com; " \
  "connect-src 'self'; object-src 'none'; base-uri 'none'; " \
  "form-action 'none'; frame-ancestors 'none'\r\n" \
  "Referrer-Policy: no-referrer\r\n"

#define VARY_HEADER  "Vary: Accept-Encoding\r\n"
#define GZIP_HEADERS "Content-Encoding: gzip\r\n" VARY_HEADER

/* Every page file gets an ETag, a hash of its stored bytes (FNV-1a, worked out on its first request): the browser keeps
   the file ("no-cache" = keep, but ask) and gets a short 304 while the app has not changed. Before 19.10.2026 every
   load fetched everything again (no-store), the language dictionary alone 0.4–0.5 MB. */
#define ETAG_MAX 512
static uint64_t g_etag[ETAG_MAX];

static uint64_t
etag_of(unsigned i, const ps5tm_asset_t *a) {
  if(i >= ETAG_MAX) return 0;
  uint64_t h = __atomic_load_n(&g_etag[i], __ATOMIC_RELAXED);
  if(h) return h;
  h = 1469598103934665603ull;
  for(size_t k = 0; k < a->len; k++) { h ^= a->data[k]; h *= 1099511628211ull; }
  h ^= (uint64_t)a->len;
  if(!h) h = 1;
  __atomic_store_n(&g_etag[i], h, __ATOMIC_RELAXED);
  return h;
}

int
ps5tm_asset_serve(int fd, const ps5tm_request_t *req) {
  const char *path = req->path;
  if(!strcmp(path, "/")) path = "/index.html";

  for(unsigned i = 0; i < ps5tm_assets_count; i++) {
    const ps5tm_asset_t *a = &ps5tm_assets[i];
    if(strcmp(a->path, path) != 0) continue;
    int page = !strncmp(a->ctype, "text/html", 9);
    char extra[640], etag[40];
    uint64_t h = etag_of(i, a);
    snprintf(etag, sizeof(etag), "\"%016llx%c\"", (unsigned long long)h, req->gzip_ok ? 'g' : 'p');
    if(h && req->if_none_match[0] && strstr(req->if_none_match, etag)) {
      snprintf(extra, sizeof(extra), "ETag: %s\r\n%s", etag, a->gz ? VARY_HEADER : "");
      ps5tm_http_send_cc(fd, 304, "Not Modified", a->ctype, "", 0, extra, "no-cache");
      return 1;
    }
    if(!a->gz) {
      snprintf(extra, sizeof(extra), "ETag: %s\r\n%s", etag, page ? PAGE_HEADERS : "");
      ps5tm_http_send_cc(fd, 200, "OK", a->ctype, a->data, a->len, extra, "no-cache");
      return 1;
    }
    if(req->gzip_ok) {
      snprintf(extra, sizeof(extra), "ETag: %s\r\n%s", etag, page ? PAGE_HEADERS GZIP_HEADERS : GZIP_HEADERS);
      ps5tm_http_send_cc(fd, 200, "OK", a->ctype, a->data, a->len, extra, "no-cache");
      return 1;
    }
    unsigned char *raw = malloc(a->raw_len ? a->raw_len : 1);
    struct libdeflate_decompressor *dc = raw ? libdeflate_alloc_decompressor() : NULL;
    size_t got = 0;
    int ok = dc && a->len > 18 && a->data[0] == 0x1f && a->data[1] == 0x8b && a->data[2] == 8 && a->data[3] == 0 &&
             libdeflate_deflate_decompress(dc, a->data + 10, a->len - 18, raw, a->raw_len, &got) == LIBDEFLATE_SUCCESS &&
             got == a->raw_len;
    if(dc) libdeflate_free_decompressor(dc);
    if(!ok) {
      free(raw);
      ps5tm_http_send_error(fd, 500, "asset_unpack_failed", "Die Seite ließ sich nicht entpacken.");
      return 1;
    }
    snprintf(extra, sizeof(extra), "ETag: %s\r\n%s", etag, page ? PAGE_HEADERS VARY_HEADER : VARY_HEADER);
    ps5tm_http_send_cc(fd, 200, "OK", a->ctype, raw, a->raw_len, extra, "no-cache");
    free(raw);
    return 1;
  }
  return 0;
}
