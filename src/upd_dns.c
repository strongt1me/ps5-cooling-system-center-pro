/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Knutwurst (PatchDL 0.3.0, src/patchdl_dns.c), adapted for PS5 Cooling & System Center - Pro
 *
 * Taken over from PatchDL with its logic unchanged (the order of the servers, the timeouts, the sinkhole test, the cache
 * and the saved addresses). Changed: the prefix upd_, DNS over HTTPS goes through the app's own HTTP client (upd_http.c,
 * BearSSL) instead of libcurl, the saved addresses live in the app's data folder. See upd_dns.h. */

#include "upd_dns.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "ps5tm.h"
#include "upd_http.h"

#define UPD_DNS_DIR       PS5TM_DATA_DIR "/updates"
#define UPD_DNS_SAVE_FILE UPD_DNS_DIR "/dns_cache.txt"

/* Worst case with everything blocked: 4 x 1.2 s UDP, 2 x 2 s TCP, 3 x 4 s DoH; about 21 s, once per host, then the failure
   is cached. */
#define UDP_TIMEOUT_MS   1200
#define TCP_TIMEOUT_MS   2000
#define DOH_TIMEOUT_MS   4000
#define POSITIVE_TTL_S   600     /* CDN edges rotate; ask again after 10 min */
#define NEGATIVE_TTL_S   20      /* don't run the whole chain for every piece */
/* upd_dns_forget leaves an answer alone while it is this fresh: many workers stalling one after another must not refresh it
   over and over. An answer that took long to get (blocked plain DNS) is kept longer. */
#define FORGET_MIN_AGE_S       15
#define FORGET_MIN_AGE_SLOW_S  120
#define SLOW_LOOKUP_MS         3000
#define CACHE_SLOTS      32
#define MAX_SAVED        32

static const char *const k_plain[] = { "1.1.1.1", "8.8.8.8", "9.9.9.9", "1.0.0.1", NULL };
static const char *const k_tcp[]   = { "1.1.1.1", "8.8.8.8", NULL };
static const char *const *g_plain = k_plain, *const *g_tcp = k_tcp;
static int g_port = 53, g_doh = 1;

/* DoH endpoints by name, pinned to an address so no DNS is needed to reach them. The certificate is checked against the
   name (the public roots of upd_ca.h). HTTP/1.1 only. */
static const struct { const char *name, *ip; } DOH_SERVERS[] = {
  { "cloudflare-dns.com", "1.1.1.1" },
  { "dns.google",         "8.8.8.8" },
  { "cloudflare-dns.com", "1.0.0.1" },
  { NULL, NULL },
};

#ifdef PS5TM_HOST_TEST
void
upd_dns_test_servers(const char *const *udp, const char *const *tcp, int port, int doh) {
  g_plain = udp ? udp : k_plain;
  g_tcp = tcp ? tcp : k_tcp;
  g_port = port > 0 ? port : 53;
  g_doh = doh;
}
#endif

/* ---------- wire format ------------------------------------------------- */

enum {
  ANS_OK = 0,
  ANS_MALFORMED = -1,
  ANS_NXDOMAIN = -2,
  ANS_RCODE = -6,      /* REFUSED, SERVFAIL ... */
  ANS_NO_A = -3,
  ANS_SINKHOLE = -4,   /* only private/loopback/zero addresses */
  ANS_TRUNCATED = -5,
};

static long long
now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static uint16_t
next_query_id(void) {
  static unsigned counter;
  uint16_t r = 0;
  if(upd_random(&r, sizeof(r)) == 0 && r) return (uint16_t)(r + __atomic_fetch_add(&counter, 1, __ATOMIC_RELAXED));
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (uint16_t)((unsigned)tv.tv_usec * 2654435761u + __atomic_fetch_add(&counter, 1, __ATOMIC_RELAXED) * 40503u + (unsigned)getpid());
}

/* Query for host's A record. Returns the length, or -1. */
static int
build_query(const char *host, uint16_t id, uint8_t *buf, size_t sz) {
  size_t pos = 12;
  const char *p = host;
  if(sz < 12 + 2 + 4) return -1;
  memset(buf, 0, 12);
  buf[0] = (uint8_t)(id >> 8); buf[1] = (uint8_t)id;
  buf[2] = 0x01;                 /* RD */
  buf[5] = 0x01;                 /* QDCOUNT = 1 */
  while(*p) {
    const char *dot = strchr(p, '.');
    size_t len = dot ? (size_t)(dot - p) : strlen(p);
    if(!len || len > 63 || pos + 1 + len + 1 + 4 > sz) return -1;
    buf[pos++] = (uint8_t)len;
    memcpy(buf + pos, p, len);
    pos += len;
    p += len;
    if(*p == '.') p++;
  }
  buf[pos++] = 0;
  buf[pos++] = 0; buf[pos++] = 1;   /* QTYPE A */
  buf[pos++] = 0; buf[pos++] = 1;   /* QCLASS IN */
  return (int)pos;
}

/* Past a (possibly compressed) name; 0 on success. */
static int
skip_name(const uint8_t *r, size_t n, size_t *pos) {
  while(*pos < n) {
    uint8_t len = r[*pos];
    if(!len) { (*pos)++; return 0; }
    if((len & 0xC0) == 0xC0) { *pos += 2; return *pos <= n ? 0 : -1; }
    if(len & 0xC0) return -1;
    *pos += 1 + (size_t)len;
  }
  return -1;
}

/* Addresses a filtering resolver answers with instead of the real one. */
static int
is_sinkhole(const uint8_t a[4]) {
  return a[0] == 0 || a[0] == 127 || a[0] == 10 || a[0] >= 224 ||
         (a[0] == 172 && (a[1] & 0xF0) == 16) ||
         (a[0] == 192 && a[1] == 168) ||
         (a[0] == 169 && a[1] == 254) ||
         (a[0] == 100 && (a[1] & 0xC0) == 64);
}

/* q/qlen: the query sent; the reply must repeat its question. */
static int
parse_answer(const uint8_t *r, size_t n, uint16_t id, const uint8_t *q, size_t qlen,
             upd_dns_answer_t *out, char *sinkhole_ip, size_t sinkhole_sz) {
  size_t pos = 12;
  uint16_t qd, an;
  out->n = 0;
  if(n < 12) return ANS_MALFORMED;
  if(((r[0] << 8) | r[1]) != id || !(r[2] & 0x80)) return ANS_MALFORMED;
  if(n < qlen) return ANS_MALFORMED;
  for(size_t i = 12; i < qlen; i++)   /* some resolvers change the case */
    if(tolower(r[i]) != tolower(q[i])) return ANS_MALFORMED;
  if(r[2] & 0x02) return ANS_TRUNCATED;
  if((r[3] & 0x0F) == 3) return ANS_NXDOMAIN;
  if(r[3] & 0x0F) return ANS_RCODE;
  qd = (uint16_t)((r[4] << 8) | r[5]);
  an = (uint16_t)((r[6] << 8) | r[7]);
  for(int k = 0; k < qd; k++) {
    if(skip_name(r, n, &pos) || pos + 4 > n) return ANS_MALFORMED;
    pos += 4;
  }
  for(int a = 0; a < an; a++) {
    uint16_t type, rdlen;
    if(skip_name(r, n, &pos) || pos + 10 > n) return ANS_MALFORMED;
    type  = (uint16_t)((r[pos] << 8) | r[pos + 1]);
    rdlen = (uint16_t)((r[pos + 8] << 8) | r[pos + 9]);
    pos += 10;
    if(pos + rdlen > n) return ANS_MALFORMED;
    if(type == 1 && rdlen == 4) {
      if(is_sinkhole(r + pos)) {
        if(sinkhole_ip && !sinkhole_ip[0])
          snprintf(sinkhole_ip, sinkhole_sz, "%u.%u.%u.%u", r[pos], r[pos + 1], r[pos + 2], r[pos + 3]);
      } else if(out->n < UPD_DNS_MAX_IPS) {
        snprintf(out->ip[out->n++], sizeof out->ip[0], "%u.%u.%u.%u", r[pos], r[pos + 1], r[pos + 2], r[pos + 3]);
      }
    }
    pos += rdlen;   /* CNAMEs and the like */
  }
  if(out->n) return ANS_OK;
  return sinkhole_ip && sinkhole_ip[0] ? ANS_SINKHOLE : ANS_NO_A;
}

static const char *
answer_text(int rc) {
  switch(rc) {
  case ANS_NXDOMAIN:  return "no such name";
  case ANS_RCODE:     return "refused";
  case ANS_NO_A:      return "no address";
  case ANS_TRUNCATED: return "truncated";
  default:            return "bad reply";
  }
}

/* ---------- transports -------------------------------------------------- */

static void
trail_add(char *trail, size_t sz, const char *fmt, const char *a, const char *b) {
  size_t len = strlen(trail);
  if(len + 3 >= sz) return;
  if(len) { memcpy(trail + len, "; ", 3); len += 2; }
  snprintf(trail + len, sz - len, fmt, a, b);
}

/* Shared tail of every transport: parse, note the outcome, 0 on a usable answer. */
static int
finish(const uint8_t *resp, size_t n, uint16_t id, const uint8_t *q, size_t qlen,
       upd_dns_answer_t *out, const char *label, char *trail, size_t trail_sz, int *truncated) {
  char sink[16] = "";
  int rc = parse_answer(resp, n, id, q, qlen, out, sink, sizeof sink);
  if(truncated) *truncated = (rc == ANS_TRUNCATED);
  if(rc == ANS_OK) return 0;
  if(rc == ANS_SINKHOLE) trail_add(trail, trail_sz, "%s: blocked (%s)", label, sink);
  else trail_add(trail, trail_sz, "%s: %s", label, answer_text(rc));
  return -1;
}

static void
server_addr(struct sockaddr_in *sa, const char *server) {
  memset(sa, 0, sizeof *sa);
  sa->sin_family = AF_INET;
  sa->sin_port = htons((uint16_t)g_port);
  inet_pton(AF_INET, server, &sa->sin_addr);
}

static int
query_udp(const char *server, const char *host, upd_dns_answer_t *out, char *trail, size_t trail_sz, int *truncated) {
  uint8_t q[300], resp[1500];
  struct sockaddr_in sa;
  uint16_t id = next_query_id();
  char label[32];
  int qlen, fd;
  ssize_t n = -1;

  snprintf(label, sizeof label, "udp %s", server);
  if((qlen = build_query(host, id, q, sizeof q)) < 0) return -1;
  if((fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
    trail_add(trail, trail_sz, "%s: socket %s", label, strerror(errno));
    return -1;
  }
  server_addr(&sa, server);
  /* Connected socket: replies from anyone but this server are dropped. */
  if(connect(fd, (struct sockaddr *)&sa, sizeof sa) || send(fd, q, (size_t)qlen, 0) != qlen) {
    trail_add(trail, trail_sz, "%s: %s", label, strerror(errno));
    close(fd);
    return -1;
  }
  /* Skip stray datagrams (an older query's late answer) until ours or the deadline. */
  errno = EAGAIN;
  for(long long deadline = now_ms() + UDP_TIMEOUT_MS, left; (left = deadline - now_ms()) > 0;) {
    struct pollfd pfd = { fd, POLLIN, 0 };
    if(poll(&pfd, 1, (int)left) != 1) { errno = EAGAIN; break; }
    n = recv(fd, resp, sizeof resp, 0);
    if(n < 0) break;
    if(n >= 2 && ((resp[0] << 8) | resp[1]) == id) {
      close(fd);
      if(!finish(resp, (size_t)n, id, q, (size_t)qlen, out, label, trail, trail_sz, truncated)) {
        snprintf(out->source, sizeof out->source, "%s", label);
        return 0;
      }
      return -1;
    }
  }
  trail_add(trail, trail_sz, "%s: %s", label, errno == EAGAIN || errno == EWOULDBLOCK ? "no answer" : strerror(errno));
  close(fd);
  return -1;
}

/* Send or receive exactly len bytes on a non-blocking socket before the deadline. A server trickling bytes can't stretch
   the exchange. */
static int
io_until(int fd, uint8_t *buf, size_t len, int sending, long long deadline) {
  size_t done = 0;
  while(done < len) {
    struct pollfd pfd = { fd, (short)(sending ? POLLOUT : POLLIN), 0 };
    long long left = deadline - now_ms();
    ssize_t k;
    if(left <= 0 || poll(&pfd, 1, (int)left) != 1) return -1;
    k = sending ? send(fd, buf + done, len - done, 0) : recv(fd, buf + done, len - done, 0);
    if(k < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) continue;
    if(k <= 0) return -1;
    done += (size_t)k;
  }
  return 0;
}

/* One DNS exchange over TCP, connect included, within TCP_TIMEOUT_MS. */
static int
query_tcp(const char *server, const char *host, upd_dns_answer_t *out, char *trail, size_t trail_sz) {
  uint8_t q[302], resp[4096], lenb[2];
  struct sockaddr_in sa;
  long long deadline = now_ms() + TCP_TIMEOUT_MS;
  uint16_t id = next_query_id();
  char label[32];
  int qlen, fd, err = 0;
  socklen_t elen = sizeof err;
  size_t rlen;

  snprintf(label, sizeof label, "tcp %s", server);
  if((qlen = build_query(host, id, q + 2, sizeof q - 2)) < 0) return -1;
  q[0] = (uint8_t)(qlen >> 8); q[1] = (uint8_t)qlen;
  if((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    trail_add(trail, trail_sz, "%s: socket %s", label, strerror(errno));
    return -1;
  }
  server_addr(&sa, server);
  fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
  if(connect(fd, (struct sockaddr *)&sa, sizeof sa) && errno != EINPROGRESS) {
    trail_add(trail, trail_sz, "%s: %s", label, strerror(errno));
    close(fd);
    return -1;
  }
  {
    struct pollfd pfd = { fd, POLLOUT, 0 };
    if(poll(&pfd, 1, TCP_TIMEOUT_MS) != 1 || getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &elen) || err) {
      trail_add(trail, trail_sz, "%s: %s", label, err ? strerror(err) : "no connection");
      close(fd);
      return -1;
    }
  }
  if(io_until(fd, q, (size_t)qlen + 2, 1, deadline) || io_until(fd, lenb, 2, 0, deadline) ||
     (rlen = (size_t)((lenb[0] << 8) | lenb[1])) > sizeof resp || io_until(fd, resp, rlen, 0, deadline)) {
    trail_add(trail, trail_sz, "%s: %s", label, "no answer");
    close(fd);
    return -1;
  }
  close(fd);
  if(finish(resp, rlen, id, q + 2, (size_t)qlen, out, label, trail, trail_sz, NULL)) return -1;
  snprintf(out->source, sizeof out->source, "%s", label);
  return 0;
}

static int
query_doh(const char *name, const char *ip, const char *host, upd_dns_answer_t *out, char *trail, size_t trail_sz) {
  uint8_t q[300];
  char url[96], label[48];
  uint16_t id = next_query_id();
  int qlen, rc = -1;

  snprintf(label, sizeof label, "doh %s", name);
  if((qlen = build_query(host, id, q, sizeof q)) < 0) return -1;
  snprintf(url, sizeof url, "https://%s/dns-query", name);
  upd_req_t r;
  memset(&r, 0, sizeof r);
  r.method = "POST";
  r.url = url;
  r.connect_ip = ip;
  r.content_type = "application/dns-message";
  r.extra_header = "Accept: application/dns-message";
  r.body = q;
  r.body_len = (size_t)qlen;
  r.range_from = -1;
  r.trust = UPD_TRUST_PUBLIC;
  r.max_redirects = 0;                             /* a resolver that redirects is not followed */
  r.connect_ms = 2000;
  r.idle_ms = DOH_TIMEOUT_MS;
  r.total_ms = DOH_TIMEOUT_MS;
  char *body = NULL;
  size_t blen = 0;
  upd_res_t res;
  if(upd_http_fetch(&r, 8192, &body, &blen, &res) != 0) {
    char code[24];
    if(res.status) snprintf(code, sizeof code, "http %d", res.status);
    else snprintf(code, sizeof code, "%s", !strcmp(res.step, "tls") ? "certificate rejected" : res.step);
    trail_add(trail, trail_sz, "%s: %s", label, code);
  } else if(!finish((const uint8_t *)body, blen, id, q, (size_t)qlen, out, label, trail, trail_sz, NULL)) {
    snprintf(out->source, sizeof out->source, "%s", label);
    rc = 0;
  }
  free(body);
  return rc;
}

/* ---------- last-known-good addresses on disk --------------------------- */

static pthread_mutex_t g_save_mtx = PTHREAD_MUTEX_INITIALIZER;

/* One line per host: "host ip,ip,...". */
static int
saved_get(const char *host, upd_dns_answer_t *out) {
  char line[512];
  FILE *f;
  int found = -1;
  pthread_mutex_lock(&g_save_mtx);
  if((f = fopen(UPD_DNS_SAVE_FILE, "r"))) {
    while(found && fgets(line, sizeof line, f)) {
      char *sp = strchr(line, ' ');
      if(!sp) continue;
      *sp = '\0';
      if(strcmp(line, host)) continue;
      out->n = 0;
      for(char *save = NULL, *tok = strtok_r(sp + 1, ",\r\n", &save); tok && out->n < UPD_DNS_MAX_IPS; tok = strtok_r(NULL, ",\r\n", &save)) {
        struct in_addr a;
        if(inet_pton(AF_INET, tok, &a) == 1 && !is_sinkhole((uint8_t *)&a))
          snprintf(out->ip[out->n++], sizeof out->ip[0], "%s", tok);
      }
      if(out->n) found = 0;
    }
    fclose(f);
  }
  pthread_mutex_unlock(&g_save_mtx);
  return found;
}

static void
saved_put(const char *host, const upd_dns_answer_t *a) {
  char (*lines)[512], line[512], tmp[sizeof UPD_DNS_SAVE_FILE + 8];
  int n = 0, fd;
  FILE *f;
  if(!(lines = calloc(MAX_SAVED, sizeof *lines))) return;   /* 16 KB: heap, not stack */
  pthread_mutex_lock(&g_save_mtx);
  mkdir(PS5TM_DATA_DIR, 0777);
  mkdir(UPD_DNS_DIR, 0777);
  /* This host first, then the others it already knew. */
  snprintf(lines[n], sizeof lines[0], "%s ", host);
  for(int i = 0; i < a->n; i++) {
    size_t len = strlen(lines[n]);
    snprintf(lines[n] + len, sizeof lines[0] - len, "%s%s", i ? "," : "", a->ip[i]);
  }
  n++;
  if((f = fopen(UPD_DNS_SAVE_FILE, "r"))) {
    size_t hl = strlen(host);
    while(n < MAX_SAVED && fgets(line, sizeof line, f)) {
      line[strcspn(line, "\r\n")] = '\0';
      if(!line[0] || (!strncmp(line, host, hl) && line[hl] == ' ')) continue;
      snprintf(lines[n++], sizeof lines[0], "%s", line);
    }
    fclose(f);
  }
  snprintf(tmp, sizeof tmp, "%s.tmp", UPD_DNS_SAVE_FILE);
  fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, 0600);
  if(fd >= 0 && (f = fdopen(fd, "w"))) {
    for(int i = 0; i < n; i++) fprintf(f, "%s\n", lines[i]);
    if(!fclose(f)) rename(tmp, UPD_DNS_SAVE_FILE);
    else unlink(tmp);
  } else if(fd >= 0) {
    close(fd);
    unlink(tmp);
  }
  pthread_mutex_unlock(&g_save_mtx);
  free(lines);
}

/* ---------- the chain --------------------------------------------------- */

static int
resolve_fresh(const char *host, upd_dns_answer_t *out) {
  char *trail = out->trail;
  size_t tsz = sizeof out->trail;
  int truncated = 0;
  trail[0] = '\0';
  for(int i = 0; g_plain[i]; i++) {
    truncated = 0;
    if(!query_udp(g_plain[i], host, out, trail, tsz, &truncated)) return 0;
    if(truncated && !query_tcp(g_plain[i], host, out, trail, tsz)) return 0;
  }
  for(int i = 0; g_tcp[i]; i++)
    if(!query_tcp(g_tcp[i], host, out, trail, tsz)) return 0;
  for(int i = 0; g_doh && DOH_SERVERS[i].name; i++)
    if(!query_doh(DOH_SERVERS[i].name, DOH_SERVERS[i].ip, host, out, trail, tsz)) return 0;
  if(!saved_get(host, out)) {
    snprintf(out->source, sizeof out->source, "saved");
    trail_add(trail, tsz, "%s%s", "using saved addresses", "");
    return 0;
  }
  out->n = 0;
  out->source[0] = '\0';
  return -1;
}

/* ---------- cache ------------------------------------------------------- */

typedef struct {
  char             host[256];
  upd_dns_answer_t ans;
  int              ok;
  int              inflight;
  time_t           expires;
  time_t           used;
  time_t           fetched;
  int              slow;     /* the lookup took > SLOW_LOOKUP_MS */
} cache_slot_t;

static pthread_mutex_t g_mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_cv  = PTHREAD_COND_INITIALIZER;
static cache_slot_t    g_cache[CACHE_SLOTS];

static time_t
now_s(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec;
}

static cache_slot_t *
slot_find(const char *host) {
  for(int i = 0; i < CACHE_SLOTS; i++)
    if(g_cache[i].host[0] && !strcmp(g_cache[i].host, host)) return &g_cache[i];
  return NULL;
}

/* A free slot, else the least recently used one nobody is resolving. */
static cache_slot_t *
slot_claim(const char *host) {
  cache_slot_t *pick = NULL;
  for(int i = 0; i < CACHE_SLOTS; i++) {
    cache_slot_t *s = &g_cache[i];
    if(s->inflight) continue;
    if(!s->host[0]) { pick = s; break; }
    if(!pick || s->used < pick->used) pick = s;
  }
  if(pick) {
    memset(pick, 0, sizeof *pick);
    snprintf(pick->host, sizeof pick->host, "%s", host);
  }
  return pick;
}

int
upd_dns_lookup(const char *host, upd_dns_answer_t *out) {
  upd_dns_answer_t fresh;
  cache_slot_t *s;
  int rc;

  memset(out, 0, sizeof *out);
  if(!host || !host[0] || strlen(host) >= sizeof g_cache[0].host) return -1;

  pthread_mutex_lock(&g_mtx);
  /* One lookup per host at a time: the download workers all want the same CDN host at once, and the others wait for its
     answer. While an answer that merely aged out is being renewed, they keep using it instead: with plain DNS blocked a
     renewal takes seconds. An answer dropped after a failure (expires == 0) is not reused. */
  for(;;) {
    s = slot_find(host);
    if(!s || !s->inflight) break;
    if(s->ok && s->expires) {
      s->used = now_s();
      *out = s->ans;
      pthread_mutex_unlock(&g_mtx);
      return 0;
    }
    pthread_cond_wait(&g_cv, &g_mtx);
  }
  if(s && s->expires > now_s()) {
    s->used = now_s();
    *out = s->ans;
    rc = s->ok ? 0 : -1;
    pthread_mutex_unlock(&g_mtx);
    return rc;
  }
  if(!s) s = slot_claim(host);
  if(s) s->inflight = 1;
  pthread_mutex_unlock(&g_mtx);

  memset(&fresh, 0, sizeof fresh);
  long long t0 = now_ms();
  rc = resolve_fresh(host, &fresh);
  long long took = now_ms() - t0;
  if(!rc && strcmp(fresh.source, "saved")) saved_put(host, &fresh);

  pthread_mutex_lock(&g_mtx);
  if(s) {
    s->ans      = fresh;
    s->ok       = !rc;
    s->expires  = now_s() + (rc ? NEGATIVE_TTL_S : POSITIVE_TTL_S);
    s->used     = now_s();
    s->fetched  = now_s();
    s->slow     = took > SLOW_LOOKUP_MS;
    s->inflight = 0;
    pthread_cond_broadcast(&g_cv);
  }
  pthread_mutex_unlock(&g_mtx);
  *out = fresh;
  return rc;
}

void
upd_dns_forget(const char *host) {
  cache_slot_t *s;
  pthread_mutex_lock(&g_mtx);
  if((s = slot_find(host)) && !s->inflight && s->ok && now_s() - s->fetched >= (s->slow ? FORGET_MIN_AGE_SLOW_S : FORGET_MIN_AGE_S))
    s->expires = 0;
  pthread_mutex_unlock(&g_mtx);
}

void
upd_dns_refresh(const char *host) {
  cache_slot_t *s;
  pthread_mutex_lock(&g_mtx);
  if((s = slot_find(host)) && !s->inflight) s->expires = 0;
  pthread_mutex_unlock(&g_mtx);
}

unsigned
upd_dns_rotation(void) {
  static unsigned turn;
  return __atomic_fetch_add(&turn, 1, __ATOMIC_RELAXED);
}
