/* HTTP/1.1 over BearSSL or plain TCP for the game updates (19.10.2026). See upd_http.h.
 *
 * What PatchDL 0.3.0 (Knutwurst, GPL-3.0-or-later) asks of libcurl, done here by hand: the host resolved by the app's own
 * resolver and each of its addresses tried in turn (CURLOPT_RESOLVE), TLS pinned to a set of roots (CAINFO_BLOB), SHA-1
 * signed leaves of Sony's CDN accepted under the pinned root (SECLEVEL=0; BearSSL checks SHA-1 signatures as a matter of
 * course), redirects followed with each hop checked, a connect timeout, a stall timeout instead of a total one for the big
 * pieces (LOW_SPEED_TIME), an abort flag (XFERINFOFUNCTION), Range requests and the Content-Range they answer with.
 * Addresses that refuse or stall are dropped from the resolver's cache, as PatchDL's note_curl_result does. */

#include "upd_http.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#ifndef PS5TM_HOST_TEST
#include <sys/sysctl.h>
#endif
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <bearssl.h>

#include "upd_ca.h"
#include "upd_dns.h"

#define UPD_UA           "Mozilla/5.0 (PS5 Cooling & System Center; game updates after PatchDL)"
#define HEAD_MAX         16384
#define IO_SLICE_MS      250

static long long
now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* ---------------------------------------------------------------- randomness, test hooks */

int
upd_random(void *buf, size_t n) {
#ifdef PS5TM_HOST_TEST
  FILE *f = fopen("/dev/urandom", "rb");
  if(!f) return -1;
  size_t k = fread(buf, 1, n, f);
  fclose(f);
  return k == n ? 0 : -1;
#else
  /* kern.arandom hands out at most 256 bytes per call */
  unsigned char *p = buf;
  while(n) {
    int mib[2] = { CTL_KERN, KERN_ARND };
    size_t want = n > 256 ? 256 : n, got = want;
    if(sysctl(mib, 2, p, &got, NULL, 0) != 0 || got == 0) return -1;
    p += got;
    n -= got;
  }
  return 0;
#endif
}

#ifdef PS5TM_HOST_TEST
static const br_x509_trust_anchor *g_test_ta;
static size_t g_test_ta_n;
static int g_test_https_port, g_test_http_port;
void upd_http_test_anchors(const br_x509_trust_anchor *ta, size_t n) { g_test_ta = ta; g_test_ta_n = n; }
void upd_http_test_redirect_all(int https_port, int http_port) { g_test_https_port = https_port; g_test_http_port = http_port; }
#endif

/* ---------------------------------------------------------------- URLs */

int
upd_url_split(const char *url, char *host, size_t hn, int *https, int *port, char *path, size_t pn) {
  const char *p;
  if(!url) return -1;
  if(!strncasecmp(url, "https://", 8)) { *https = 1; *port = 443; p = url + 8; }
  else if(!strncasecmp(url, "http://", 7)) { *https = 0; *port = 80; p = url + 7; }
  else return -1;
  size_t hl = strcspn(p, ":/?#");
  if(hl == 0 || hl >= hn) return -1;
  for(size_t i = 0; i < hl; i++)
    if(!isalnum((unsigned char)p[i]) && p[i] != '.' && p[i] != '-') return -1;
  memcpy(host, p, hl);
  host[hl] = 0;
  p += hl;
  if(*p == ':') {
    char *end;
    long v = strtol(p + 1, &end, 10);
    if(end == p + 1 || v <= 0 || v > 65535) return -1;
    *port = (int)v;
    p = end;
  }
  if(*p == '#') p = "";
  const char *q = *p ? p : "/";
  if(*q != '/' && *q != '?') return -1;
  size_t ql = strcspn(q, "#");
  if(ql + 2 >= pn) return -1;
  if(*q == '?') { path[0] = '/'; memcpy(path + 1, q, ql); path[ql + 1] = 0; }
  else { memcpy(path, q, ql); path[ql] = 0; }
  for(const char *c = path; *c; c++)
    if((unsigned char)*c <= ' ' || *c == 0x7f) return -1;      /* no header injection through the path */
  return 0;
}

/* ---------------------------------------------------------------- the connection */

typedef struct {
  int            fd;
  int            tls;
  int            idle_ms;
  long long      deadline;      /* 0: none */
  volatile int  *abort;
  int            timed_out, aborted, eof;
  int            cut;           /* TLS ended by a plain TCP close, without close_notify */
  br_ssl_client_context  *cc;
  br_x509_minimal_context *xc;
  br_sslio_context        ioc;
  unsigned char          *iobuf;
} conn_t;

/* Waits until fd is ready for events, in slices so that the abort flag and the deadline are seen. 1 ready, 0 timeout or
   abort (flags set), -1 error. */
static int
wait_fd(conn_t *c, short events, int ms) {
  long long until = now_ms() + ms;
  for(;;) {
    if(c->abort && *c->abort) { c->aborted = 1; return 0; }
    long long now = now_ms();
    long long left = until - now;
    if(c->deadline && c->deadline - now < left) left = c->deadline - now;
    if(left <= 0) { c->timed_out = 1; return 0; }
    struct pollfd pfd = { c->fd, events, 0 };
    int r = poll(&pfd, 1, (int)(left < IO_SLICE_MS ? left : IO_SLICE_MS));
    if(r > 0) return 1;
    if(r < 0 && errno != EINTR) return -1;
  }
}

static int
raw_read(conn_t *c, unsigned char *buf, size_t len) {
  for(;;) {
    int w = wait_fd(c, POLLIN, c->idle_ms);
    if(w <= 0) return -1;
    ssize_t k = recv(c->fd, buf, len, 0);
    if(k > 0) return (int)k;
    if(k == 0) { c->eof = 1; return -1; }
    if(errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) return -1;
  }
}

static int
raw_write(conn_t *c, const unsigned char *buf, size_t len) {
  for(;;) {
    int w = wait_fd(c, POLLOUT, c->idle_ms);
    if(w <= 0) return -1;
    ssize_t k = send(c->fd, buf, len, MSG_NOSIGNAL);
    if(k > 0) return (int)k;
    if(k < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) return -1;
  }
}

static int sock_read_cb(void *ctx, unsigned char *buf, size_t len) { return raw_read(ctx, buf, len); }
static int sock_write_cb(void *ctx, const unsigned char *buf, size_t len) { return raw_write(ctx, buf, len); }

static int
conn_open_tcp(conn_t *c, const char *ip, int port, int connect_ms) {
  struct sockaddr_in sa;
  memset(&sa, 0, sizeof sa);
  sa.sin_family = AF_INET;
  sa.sin_port = htons((uint16_t)port);
  if(inet_pton(AF_INET, ip, &sa.sin_addr) != 1) return -1;
  c->fd = socket(AF_INET, SOCK_STREAM, 0);
  if(c->fd < 0) return -1;
  int one = 1;
  setsockopt(c->fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
#ifdef SO_NOSIGPIPE
  setsockopt(c->fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
  /* a large receive window: a single connection to a CDN far away is limited by it, not by the line (PatchDL 0.3.1) */
  int rcvbuf = 2 * 1024 * 1024;
  setsockopt(c->fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof rcvbuf);
  fcntl(c->fd, F_SETFL, fcntl(c->fd, F_GETFL, 0) | O_NONBLOCK);
  if(connect(c->fd, (struct sockaddr *)&sa, sizeof sa) != 0 && errno != EINPROGRESS) { close(c->fd); c->fd = -1; return -1; }
  int w = wait_fd(c, POLLOUT, connect_ms);
  int err = 0;
  socklen_t el = sizeof err;
  if(w != 1 || getsockopt(c->fd, SOL_SOCKET, SO_ERROR, &err, &el) != 0 || err) { close(c->fd); c->fd = -1; return -1; }
  return 0;
}

static int
conn_start_tls(conn_t *c, const char *host, upd_trust_t trust) {
  c->cc = calloc(1, sizeof(*c->cc));
  c->xc = calloc(1, sizeof(*c->xc));
  c->iobuf = malloc(BR_SSL_BUFSIZE_BIDI);
  if(!c->cc || !c->xc || !c->iobuf) return -1;
  const br_x509_trust_anchor *ta = trust == UPD_TRUST_SONY ? UPD_SONY_TAs : trust == UPD_TRUST_WEB ? UPD_WEB_TAs : UPD_PUB_TAs;
  size_t tan = trust == UPD_TRUST_SONY ? UPD_SONY_TAs_NUM : trust == UPD_TRUST_WEB ? UPD_WEB_TAs_NUM : UPD_PUB_TAs_NUM;
#ifdef PS5TM_HOST_TEST
  if(g_test_ta) { ta = g_test_ta; tan = g_test_ta_n; }
#endif
  br_ssl_client_init_full(c->cc, c->xc, ta, tan);
  br_ssl_engine_set_versions(&c->cc->eng, BR_TLS12, BR_TLS12);
  br_ssl_engine_set_buffer(&c->cc->eng, c->iobuf, BR_SSL_BUFSIZE_BIDI, 1);
  unsigned char seed[32];
  if(upd_random(seed, sizeof seed) != 0) return -1;
  br_ssl_engine_inject_entropy(&c->cc->eng, seed, sizeof seed);
  if(!br_ssl_client_reset(c->cc, host, 0)) return -1;
  br_sslio_init(&c->ioc, &c->cc->eng, sock_read_cb, c, sock_write_cb, c);
  if(br_sslio_flush(&c->ioc) != 0) return -1;        /* the handshake goes out here; a failure shows in the engine's error */
  c->tls = 1;
  return 0;
}

static void
conn_close(conn_t *c) {
  /* no close_notify: every request ends with Connection: close, and br_sslio_close spins when the peer has already gone */
  if(c->fd >= 0) close(c->fd);
  c->fd = -1;
  free(c->cc);
  free(c->xc);
  free(c->iobuf);
  c->cc = NULL; c->xc = NULL; c->iobuf = NULL;
  c->tls = 0;
}

static int
conn_write_all(conn_t *c, const void *data, size_t n) {
  if(c->tls) return br_sslio_write_all(&c->ioc, data, n) == 0 && br_sslio_flush(&c->ioc) == 0 ? 0 : -1;
  const unsigned char *p = data;
  while(n) {
    int k = raw_write(c, p, n);
    if(k <= 0) return -1;
    p += k;
    n -= (size_t)k;
  }
  return 0;
}

/* >0 bytes, 0 at the end of the stream, -1 on an error (flags tell timeout or abort) */
static int
conn_read(conn_t *c, void *buf, size_t n) {
  if(c->tls) {
    int k = br_sslio_read(&c->ioc, buf, n);
    if(k > 0) return k;
    /* the peer closed: cleanly (close_notify) or by dropping TCP after the response, which servers often do */
    if(br_ssl_engine_last_error(&c->cc->eng) == BR_ERR_OK) return 0;
    if(c->eof) { c->cut = 1; return 0; }
    return -1;
  }
  int k = raw_read(c, buf, n);
  if(k > 0) return k;
  return c->eof ? 0 : -1;
}

/* ---------------------------------------------------------------- the response */

typedef struct {
  conn_t   *c;
  char      head[HEAD_MAX + 1];
  size_t    hlen;
  char     *extra;          /* body bytes read together with the head */
  size_t    extra_len, extra_off;
  int       chunked;
  int64_t   clen;
  char      location[1024];
} resp_t;

static int
resp_fill(resp_t *r, void *buf, size_t n) {
  if(r->extra_off < r->extra_len) {
    size_t k = r->extra_len - r->extra_off;
    if(k > n) k = n;
    memcpy(buf, r->extra + r->extra_off, k);
    r->extra_off += k;
    return (int)k;
  }
  return conn_read(r->c, buf, n);
}

static const char *
hdr_find(const char *head, const char *name) {
  size_t nl = strlen(name);
  for(const char *p = strstr(head, "\r\n"); p && p[2]; p = strstr(p + 2, "\r\n")) {
    const char *l = p + 2;
    if(!strncasecmp(l, name, nl) && l[nl] == ':') {
      l += nl + 1;
      while(*l == ' ' || *l == '\t') l++;
      return l;
    }
  }
  return NULL;
}

static void
hdr_copy(const char *v, char *out, size_t n) {
  size_t k = v ? strcspn(v, "\r\n") : 0;
  if(k >= n) k = n - 1;
  if(v) memcpy(out, v, k);
  out[k] = 0;
}

/* Reads the status line and the headers. 0, or -1. */
static int
resp_head(resp_t *r, upd_res_t *res) {
  r->hlen = 0;
  for(;;) {
    if(r->hlen >= HEAD_MAX) { snprintf(res->step, sizeof res->step, "bad_reply"); return -1; }
    int k = conn_read(r->c, r->head + r->hlen, HEAD_MAX - r->hlen);
    if(k <= 0) {
      snprintf(res->step, sizeof res->step, "%s", r->c->aborted ? "aborted" : r->c->timed_out ? "timeout" : k == 0 ? "bad_reply" : r->c->tls && !r->hlen ? "tls" : "network");
      return -1;
    }
    r->hlen += (size_t)k;
    r->head[r->hlen] = 0;
    char *end = strstr(r->head, "\r\n\r\n");
    if(end) {
      size_t hl = (size_t)(end - r->head) + 4;
      r->extra_len = r->hlen - hl;
      r->extra = malloc(r->extra_len + 1);
      if(!r->extra) return -1;
      memcpy(r->extra, r->head + hl, r->extra_len);
      r->extra_off = 0;
      r->head[hl] = 0;
      r->hlen = hl;
      break;
    }
  }
  int major = 0, minor = 0, status = 0;
  if(sscanf(r->head, "HTTP/%d.%d %d", &major, &minor, &status) != 3 || status < 100 || status > 999) {
    snprintf(res->step, sizeof res->step, "bad_reply");
    return -1;
  }
  res->status = status;
  const char *v;
  r->clen = -1;
  if((v = hdr_find(r->head, "Content-Length"))) {
    char *e;
    long long x = strtoll(v, &e, 10);
    if(e != v && x >= 0) r->clen = x;
  }
  r->chunked = (v = hdr_find(r->head, "Transfer-Encoding")) && !strncasecmp(v, "chunked", 7);
  res->content_length = r->chunked ? -1 : r->clen;
  res->range_start = res->range_total = -1;
  if((v = hdr_find(r->head, "Content-Range"))) {
    long long a = -1, b = -1, t = -1;
    if(sscanf(v, "bytes %lld-%lld/%lld", &a, &b, &t) >= 2) { res->range_start = a; res->range_total = t; }
  }
  hdr_copy(hdr_find(r->head, "Location"), r->location, sizeof r->location);
  return 0;
}

/* Hands the body to on_body. 0 when it was read to its end, -1 otherwise (step set). */
static int
resp_body(resp_t *r, const upd_req_t *req, upd_res_t *res, int deliver) {
  unsigned char *buf = malloc(65536);
  if(!buf) { snprintf(res->step, sizeof res->step, "network"); return -1; }
  int rc = -1;
  int64_t left = r->clen;
  for(;;) {
    int64_t chunk = -1;
    if(r->chunked) {
      char line[64];
      size_t ln = 0;
      for(;;) {                                     /* the size line */
        unsigned char ch;
        int k = resp_fill(r, &ch, 1);
        if(k <= 0) goto io_fail;
        if(ch == '\n') break;
        if(ln + 1 < sizeof line) line[ln++] = (char)ch;
      }
      line[ln] = 0;
      char *e;
      long long x = strtoll(line, &e, 16);
      if(e == line || x < 0) { snprintf(res->step, sizeof res->step, "bad_reply"); goto out; }
      if(x == 0) { rc = 0; goto out; }              /* trailers are not read: the connection closes anyway */
      chunk = x;
    } else {
      chunk = left;                                 /* -1: until the end of the stream */
    }
    while(chunk != 0) {
      size_t want = 65536;
      if(chunk > 0 && (uint64_t)chunk < want) want = (size_t)chunk;
      int k = resp_fill(r, buf, want);
      if(k == 0 && chunk < 0 && !r->chunked) {      /* body to the end of the stream: over TLS only a clean close ends it */
        if(r->c->tls && r->c->cut) { snprintf(res->step, sizeof res->step, "cut_short"); goto out; }
        rc = 0;
        goto out;
      }
      if(k <= 0) goto io_fail;
      res->body_bytes += (uint64_t)k;
      if(deliver && req->on_body && req->on_body(req->ctx, buf, (size_t)k) != 0) { snprintf(res->step, sizeof res->step, "aborted"); goto out; }
      if(chunk > 0) chunk -= k;
    }
    if(!r->chunked) { rc = 0; goto out; }
    unsigned char crlf[2];                          /* the CRLF after a chunk */
    for(int i = 0; i < 2; i++)
      if(resp_fill(r, &crlf[i], 1) <= 0) goto io_fail;
  }
io_fail:
  snprintf(res->step, sizeof res->step, "%s", r->c->aborted ? "aborted" : r->c->timed_out ? "timeout" : "network");
out:
  free(buf);
  return rc;
}

/* ---------------------------------------------------------------- one request, with its redirects */

static int
same_host_https(const char *host, int https, void *ctx) {
  return https && !strcasecmp(host, (const char *)ctx);
}

int
upd_http(const upd_req_t *req, upd_res_t *res) {
  memset(res, 0, sizeof *res);
  res->content_length = res->range_start = res->range_total = -1;
  char *url = malloc(2048), *path = malloc(2048), host[256], first_host[256] = "";
  if(!url || !path) { free(url); free(path); snprintf(res->step, sizeof res->step, "network"); return -1; }
  snprintf(url, 2048, "%s", req->url ? req->url : "");
  const char *method = req->method ? req->method : "GET";
  int rc = -1, hops = 0;
  long long deadline = req->total_ms > 0 ? now_ms() + req->total_ms : 0;

  for(;;) {
    int https = 0, port = 0;
    if(upd_url_split(url, host, sizeof host, &https, &port, path, 2048) != 0) { snprintf(res->step, sizeof res->step, "bad_url"); break; }
    if(!first_host[0]) {
      snprintf(first_host, sizeof first_host, "%s", host);
      if(req->hop_ok && !req->hop_ok(host, https, req->hop_ctx)) { snprintf(res->step, sizeof res->step, "host"); break; }
    }
    snprintf(res->final_host, sizeof res->final_host, "%s", host);

    /* where to connect */
    upd_dns_answer_t ans;
    memset(&ans, 0, sizeof ans);
    int test_route = 0;
#ifdef PS5TM_HOST_TEST
    test_route = (https && g_test_https_port) || (!https && g_test_http_port);
#endif
    if(req->connect_ip) { ans.n = 1; snprintf(ans.ip[0], sizeof ans.ip[0], "%s", req->connect_ip); }
    else if(!test_route && (upd_dns_lookup(host, &ans) != 0 || ans.n == 0)) { snprintf(res->step, sizeof res->step, "dns"); break; }
    int cport = port;
#ifdef PS5TM_HOST_TEST
    if(https && g_test_https_port) { cport = g_test_https_port; ans.n = 1; snprintf(ans.ip[0], sizeof ans.ip[0], "127.0.0.1"); }
    if(!https && g_test_http_port) { cport = g_test_http_port; ans.n = 1; snprintf(ans.ip[0], sizeof ans.ip[0], "127.0.0.1"); }
#endif

    conn_t c;
    memset(&c, 0, sizeof c);
    c.fd = -1;
    c.idle_ms = req->idle_ms > 0 ? req->idle_ms : 30000;
    c.deadline = deadline;
    c.abort = req->abort;
    unsigned first = ans.n ? upd_dns_rotation() % (unsigned)ans.n : 0;
    int connected = 0;
    for(int i = 0; i < ans.n && !connected; i++) {
      const char *ip = ans.ip[(first + (unsigned)i) % (unsigned)ans.n];
      if(conn_open_tcp(&c, ip, cport, req->connect_ms > 0 ? req->connect_ms : 10000) == 0) {
        connected = 1;
        snprintf(res->ip, sizeof res->ip, "%s", ip);
      }
      if(c.aborted) break;
    }
    if(!connected) {
      snprintf(res->step, sizeof res->step, "%s", c.aborted ? "aborted" : "connect");
      if(!req->connect_ip) upd_dns_forget(host);
      conn_close(&c);
      break;
    }
    if(https && conn_start_tls(&c, host, req->trust) != 0) {
      snprintf(res->step, sizeof res->step, "%s", c.aborted ? "aborted" : c.timed_out ? "timeout" : "tls");
      conn_close(&c);
      break;
    }

    /* the request */
    char *rq = malloc(4096 + 2048);
    if(!rq) { conn_close(&c); snprintf(res->step, sizeof res->step, "network"); break; }
    int n = snprintf(rq, 4096 + 2048, "%s %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: " UPD_UA "\r\nAccept: */*\r\nAccept-Encoding: identity\r\nConnection: close\r\n",
                     method, path, host);
    if(req->range_from >= 0) {
      if(req->range_to >= req->range_from) n += snprintf(rq + n, 4096 + 2048 - (size_t)n, "Range: bytes=%lld-%lld\r\n", (long long)req->range_from, (long long)req->range_to);
      else n += snprintf(rq + n, 4096 + 2048 - (size_t)n, "Range: bytes=%lld-\r\n", (long long)req->range_from);
    }
    if(!strcmp(method, "POST"))
      n += snprintf(rq + n, 4096 + 2048 - (size_t)n, "Content-Type: %s\r\nContent-Length: %zu\r\n",
                    req->content_type ? req->content_type : "application/octet-stream", req->body_len);
    if(req->extra_header && !strpbrk(req->extra_header, "\r\n"))
      n += snprintf(rq + n, 4096 + 2048 - (size_t)n, "%s\r\n", req->extra_header);
    n += snprintf(rq + n, 4096 + 2048 - (size_t)n, "\r\n");
    int wrc = conn_write_all(&c, rq, (size_t)n);
    if(wrc == 0 && !strcmp(method, "POST") && req->body_len) wrc = conn_write_all(&c, req->body, req->body_len);
    free(rq);
    if(wrc != 0) {
      snprintf(res->step, sizeof res->step, "%s", c.aborted ? "aborted" : c.timed_out ? "timeout" : c.tls && br_ssl_engine_last_error(&c.cc->eng) != BR_ERR_OK && br_ssl_engine_last_error(&c.cc->eng) != BR_ERR_IO ? "tls" : "network");
      if(!req->connect_ip && (c.timed_out || !strcmp(res->step, "network"))) upd_dns_forget(host);
      conn_close(&c);
      break;
    }

    resp_t *r = calloc(1, sizeof(*r));
    if(!r) { conn_close(&c); snprintf(res->step, sizeof res->step, "network"); break; }
    r->c = &c;
    if(resp_head(r, res) != 0) {
      if(!strcmp(res->step, "tls") && c.cc && br_ssl_engine_last_error(&c.cc->eng) == BR_ERR_IO) snprintf(res->step, sizeof res->step, "network");
      if(!req->connect_ip && (c.timed_out || !strcmp(res->step, "network"))) upd_dns_forget(host);
      free(r->extra); free(r); conn_close(&c);
      break;
    }

    /* a redirect */
    if((res->status == 301 || res->status == 302 || res->status == 303 || res->status == 307 || res->status == 308) && r->location[0]) {
      char next[2048];
      if(!strncasecmp(r->location, "http://", 7) || !strncasecmp(r->location, "https://", 8)) snprintf(next, sizeof next, "%s", r->location);
      else if(r->location[0] == '/' && r->location[1] != '/') snprintf(next, sizeof next, "%s://%s:%d%s", https ? "https" : "http", host, port, r->location);
      else next[0] = 0;
      free(r->extra); free(r); conn_close(&c);
      char nh[256], np[16];
      int nhttps = 0, nport = 0;
      (void)np;
      if(!next[0] || hops >= req->max_redirects || upd_url_split(next, nh, sizeof nh, &nhttps, &nport, path, 2048) != 0) { snprintf(res->step, sizeof res->step, "redirect"); break; }
      int ok = req->hop_ok ? req->hop_ok(nh, nhttps, req->hop_ctx) : same_host_https(nh, nhttps, first_host);
      if(!ok) { snprintf(res->step, sizeof res->step, "redirect"); break; }
      snprintf(url, 2048, "%s", next);
      if(res->status == 303) method = "GET";
      hops++;
      res->status = 0;
      continue;
    }

    if(req->on_head && req->on_head(req->ctx, res) != 0) {
      snprintf(res->step, sizeof res->step, "aborted");
      free(r->extra); free(r); conn_close(&c);
      break;
    }
    if(res->status >= 400) {
      snprintf(res->step, sizeof res->step, "http_status");
      free(r->extra); free(r); conn_close(&c);
      rc = 0;
      break;
    }
    int no_body = res->status == 204 || res->status == 304 || (res->status >= 100 && res->status < 200);
    int brc = no_body ? 0 : resp_body(r, req, res, 1);
    if(brc != 0 && !req->connect_ip && (c.timed_out || !strcmp(res->step, "network"))) upd_dns_forget(host);
    free(r->extra); free(r); conn_close(&c);
    rc = brc;
    break;
  }
  free(url);
  free(path);
  return rc;
}

/* ---------------------------------------------------------------- into a buffer */

typedef struct { char *buf; size_t len, max; int over; } membuf_t;

static int
mem_body(void *ctx, const void *data, size_t n) {
  membuf_t *m = ctx;
  if(n > m->max - m->len) { m->over = 1; return 1; }
  char *nb = realloc(m->buf, m->len + n + 1);
  if(!nb) return 1;
  m->buf = nb;
  memcpy(m->buf + m->len, data, n);
  m->len += n;
  m->buf[m->len] = 0;
  return 0;
}

int
upd_http_fetch(const upd_req_t *req, size_t max, char **out, size_t *len, upd_res_t *res) {
  *out = NULL;
  *len = 0;
  membuf_t m = { NULL, 0, max, 0 };
  upd_req_t r = *req;
  r.on_body = mem_body;
  r.ctx = &m;
  int rc = upd_http(&r, res);
  if(m.over) snprintf(res->step, sizeof res->step, "too_big");
  if(rc != 0 || res->status < 200 || res->status >= 300 || m.over) { free(m.buf); return -1; }
  if(!m.buf) { m.buf = malloc(1); if(!m.buf) return -1; m.buf[0] = 0; }
  *out = m.buf;
  *len = m.len;
  return 0;
}
