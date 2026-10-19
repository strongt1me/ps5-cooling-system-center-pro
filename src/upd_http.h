/* HTTP/1.1 over TLS (BearSSL) or plain TCP for the game updates (19.10.2026, after PatchDL 0.3.0 by Knutwurst,
 * GPL-3.0-or-later; PatchDL uses libcurl and OpenSSL, this is the app's own client over BearSSL).
 *
 * One connection per request. The host name is looked up with the app's own resolver (upd_dns.c), never with the
 * console's DNS; a request may name the address instead (the DNS-over-HTTPS servers). TLS checks the server against one
 * of two sets of roots (upd_ca.h): Sony's for Sony's download servers, the public set for the resolvers and the patch
 * history sites. Redirects are followed by hand, each hop checked by the caller's rule. */
#ifndef PS5TM_UPD_HTTP_H
#define PS5TM_UPD_HTTP_H

#include <stddef.h>
#include <stdint.h>

/* SONY: the download servers; PUBLIC: DNS over HTTPS and the patch history sites; WEB: GitHub (the payload store) */
typedef enum { UPD_TRUST_SONY = 0, UPD_TRUST_PUBLIC = 1, UPD_TRUST_WEB = 2 } upd_trust_t;

typedef struct {
  int      status;              /* of the last response, 0 when none came */
  int64_t  content_length;      /* -1 unknown */
  int64_t  range_start;         /* from Content-Range, -1 none */
  int64_t  range_total;         /* the whole size from Content-Range, -1 none */
  uint64_t body_bytes;          /* body bytes handed to on_body */
  char     step[24];            /* "" ok, else dns, connect, tls, timeout, network, redirect, http_status, too_big, aborted, bad_reply, host */
  char     final_host[256];     /* the host of the last hop */
  char     ip[16];              /* the address that answered */
} upd_res_t;

typedef struct {
  const char  *method;          /* "GET" (default) or "POST" */
  const char  *url;             /* http:// or https:// */
  const char  *connect_ip;      /* NULL: look the host up; else this IPv4 address (TLS still checks the name) */
  const char  *content_type;    /* POST */
  const void  *body;
  size_t       body_len;
  const char  *extra_header;    /* one more "Name: value" line, or NULL */
  int64_t      range_from;      /* >= 0: ask for bytes range_from..range_to (range_to < 0: to the end) */
  int64_t      range_to;
  upd_trust_t  trust;
  int          max_redirects;   /* 0: none followed */
  /* each hop's host and scheme (https 1/0); 0 to refuse it. NULL: every hop must be https on the same host */
  int        (*hop_ok)(const char *host, int https, void *hop_ctx);
  void        *hop_ctx;
  int          connect_ms;      /* default 10000 */
  int          idle_ms;         /* no byte for this long: timeout (default 30000) */
  int          total_ms;        /* 0: no limit */
  /* the status line and headers of the final response; non-zero stops (step "aborted"). May be NULL. */
  int        (*on_head)(void *ctx, const upd_res_t *res);
  /* body bytes; non-zero stops (step "aborted"). NULL: the body is read and dropped. */
  int        (*on_body)(void *ctx, const void *data, size_t n);
  void        *ctx;
  volatile int *abort;          /* non-zero at any time: stop (step "aborted") */
} upd_req_t;

/* 0 when a final response came (any status; res->step is "http_status" for 4xx/5xx), -1 otherwise with res->step. */
int  upd_http(const upd_req_t *req, upd_res_t *res);

/* GET (or POST with body) into a malloc'd, NUL-terminated buffer of at most max bytes. 0 only for a 2xx answer. */
int  upd_http_fetch(const upd_req_t *req, size_t max, char **out, size_t *len, upd_res_t *res);

/* "https://host:port/path?x" -> host (no port), https flag, port, path ("/" when none). 0, or -1 when it is not one. */
int  upd_url_split(const char *url, char *host, size_t hn, int *https, int *port, char *path, size_t pn);

/* Random bytes for TLS (kern.arandom on the console). 0, or -1. */
int  upd_random(void *buf, size_t n);

#ifdef PS5TM_HOST_TEST
#include <bearssl.h>
/* host tests: trust these anchors instead of a set (NULL: back to the real sets) */
void upd_http_test_anchors(const br_x509_trust_anchor *ta, size_t n);
/* host tests: send every https/http request to 127.0.0.1:port (TLS name stays the URL's host) */
void upd_http_test_redirect_all(int https_port, int http_port);
#endif

#endif
