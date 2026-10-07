/* Minimal HTTP/1.1 server (one detached thread per connection).
 *
 * Deliberately dependency-free: the payload SDK ships no libmicrohttpd, and
 * the API surface here is small enough that a hand-rolled parser is safer
 * than pulling in a web stack.
 */

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#include "ps5tm.h"

#define MAX_HEADER_BYTES 16384
#define MAX_BODY_BYTES   65536

/* One thread and 16 KiB per connection, on a console whose heap is small: a
   cap keeps a flood of connections from starving the fan controller that
   shares the process. A page load keeps six or so open at once. */
#define MAX_CONNECTIONS  24

/* Wall-clock budgets for receiving a request. SO_RCVTIMEO alone only drops a
   client that goes completely silent; one that sends a byte every few seconds
   used to hold its thread for as long as it liked. */
#define HEADER_BUDGET_MS 15000
#define BODY_BUDGET_MS   20000

static atomic_int g_connections;


static int
write_all(int fd, const void *buf, size_t len) {
  const char *p = buf;
  while(len) {
    ssize_t n = write(fd, p, len);
    if(n > 0) { p += n; len -= (size_t)n; continue; }
    if(n < 0 && errno == EINTR) continue;
    return -1;
  }
  return 0;
}


void
ps5tm_http_send(int fd, int status, const char *status_text,
                const char *ctype, const void *body, size_t len,
                const char *extra_headers) {
  /* Room for the page's security headers on top of the usual five lines. When
     it does not fit, nothing is sent at all, which is why this is not tight. */
  char head[1024];
  int n = snprintf(head, sizeof(head),
                   "HTTP/1.1 %d %s\r\n"
                   "Content-Type: %s\r\n"
                   "Content-Length: %zu\r\n"
                   "Cache-Control: no-store\r\n"
                   "Connection: close\r\n"
                   "X-Content-Type-Options: nosniff\r\n"
                   "%s\r\n",
                   status, status_text, ctype, len,
                   extra_headers ? extra_headers : "");
  if(n < 0 || n >= (int)sizeof(head)) return;

  if(write_all(fd, head, (size_t)n) != 0) return;
  if(len) write_all(fd, body, len);
}


/* Covers are up to a few hundred kilobytes and a page loads dozens of them,
   so the file goes out in chunks rather than through one buffer, and the
   browser may keep it: the URL carries the database's timestamp and changes
   whenever the cover does. */
int
ps5tm_http_send_file(int fd, const char *path, const char *ctype,
                     size_t max_bytes, unsigned max_age_s) {
  int in = open(path, O_RDONLY);
  if(in < 0) return -1;
  struct stat st;
  if(fstat(in, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size <= 0 ||
     (uint64_t)st.st_size > max_bytes) {
    close(in);
    return -1;
  }

  char head[256];
  int n = snprintf(head, sizeof(head),
                   "HTTP/1.1 200 OK\r\n"
                   "Content-Type: %s\r\n"
                   "Content-Length: %lld\r\n"
                   "Cache-Control: private, max-age=%u\r\n"
                   "Connection: close\r\n\r\n",
                   ctype, (long long)st.st_size, max_age_s);
  if(n < 0 || n >= (int)sizeof(head)) { close(in); return -1; }

  /* From here on the answer is under way; a client that hangs up mid-file
     is not an error to report. */
  if(write_all(fd, head, (size_t)n) == 0) {
    char   buf[8192];
    off_t  left = st.st_size;
    while(left > 0) {
      ssize_t r = read(in, buf, sizeof(buf));
      if(r <= 0 || write_all(fd, buf, (size_t)r) != 0) break;
      left -= r;
    }
  }
  close(in);
  return 0;
}


/* Bytes that are in memory already (an icon read out of a package), which the browser may keep for max_age_s. */
void
ps5tm_http_send_cached(int fd, const char *ctype, const void *body, size_t len, unsigned max_age_s) {
  char head[256];
  int n = snprintf(head, sizeof(head),
                   "HTTP/1.1 200 OK\r\n"
                   "Content-Type: %s\r\n"
                   "Content-Length: %zu\r\n"
                   "Cache-Control: private, max-age=%u\r\n"
                   "X-Content-Type-Options: nosniff\r\n"
                   "Connection: close\r\n\r\n",
                   ctype, len, max_age_s);
  if(n < 0 || n >= (int)sizeof(head)) return;
  if(write_all(fd, head, (size_t)n) == 0 && len) write_all(fd, body, len);
}


/* A file as a download. Logs can be tens of megabytes, so it goes out in
   chunks like a cover; the name is the caller's and must hold no quote or
   control character (it is put between quotes in the header). */
int
ps5tm_http_send_download(int fd, const char *path, const char *ctype,
                         const char *filename, uint64_t max_bytes) {
  for(const char *p = filename; *p; p++)
    if(*p == '"' || *p == '\\' || (unsigned char)*p < 0x20) return -1;
  int in = open(path, O_RDONLY);
  if(in < 0) return -1;
  struct stat st;
  if(fstat(in, &st) != 0 || !S_ISREG(st.st_mode) || (uint64_t)st.st_size > max_bytes) {
    close(in);
    return -1;
  }

  char head[512];
  int n = snprintf(head, sizeof(head),
                   "HTTP/1.1 200 OK\r\n"
                   "Content-Type: %s\r\n"
                   "Content-Length: %lld\r\n"
                   "Content-Disposition: attachment; filename=\"%s\"\r\n"
                   "Cache-Control: no-store\r\n"
                   "X-Content-Type-Options: nosniff\r\n"
                   "Connection: close\r\n\r\n",
                   ctype, (long long)st.st_size, filename);
  if(n < 0 || n >= (int)sizeof(head)) { close(in); return -1; }

  if(write_all(fd, head, (size_t)n) == 0) {
    char   buf[8192];
    off_t  left = st.st_size;
    while(left > 0) {
      ssize_t r = read(in, buf, sizeof(buf));
      if(r <= 0 || write_all(fd, buf, (size_t)r) != 0) break;
      left -= r;
    }
  }
  close(in);
  return 0;
}


void
ps5tm_http_send_json(int fd, int status, const char *json) {
  const char *text = status == 200 ? "OK"
                   : status == 202 ? "Accepted"
                   : status == 400 ? "Bad Request"
                   : status == 403 ? "Forbidden"
                   : status == 404 ? "Not Found"
                   : status == 405 ? "Method Not Allowed"
                   : status == 409 ? "Conflict"
                   : status == 503 ? "Service Unavailable"
                   : "Internal Server Error";
  ps5tm_http_send(fd, status, text, "application/json",
                  json, strlen(json), NULL);
}


/* Escapes the few characters that can appear in our own messages. */
static void
json_escape(const char *in, char *out, size_t out_len) {
  size_t o = 0;
  for(size_t i = 0; in[i] && o + 7 < out_len; i++) {
    unsigned char c = (unsigned char)in[i];
    if(c == '"' || c == '\\') { out[o++] = '\\'; out[o++] = (char)c; }
    else if(c == '\n')        { out[o++] = '\\'; out[o++] = 'n';     }
    else if(c == '\r')        { out[o++] = '\\'; out[o++] = 'r';     }
    else if(c == '\t')        { out[o++] = '\\'; out[o++] = 't';     }
    else if(c < 0x20)         { o += (size_t)snprintf(out + o, out_len - o,
                                                      "\\u%04x", c); }
    else                      { out[o++] = (char)c; }
  }
  out[o] = 0;
}


void
ps5tm_http_send_error(int fd, int status, const char *code,
                      const char *message) {
  char esc_code[128], esc_msg[512], body[768];
  json_escape(code ? code : "error", esc_code, sizeof(esc_code));
  json_escape(message ? message : "", esc_msg, sizeof(esc_msg));
  snprintf(body, sizeof(body),
           "{\"ok\":false,\"code\":\"%s\",\"message\":\"%s\"}",
           esc_code, esc_msg);
  ps5tm_http_send_json(fd, status, body);
}


/* Case-insensitive header lookup within the raw header block. */
static const char *
find_header(const char *headers, const char *name) {
  size_t nlen = strlen(name);
  for(const char *p = headers; *p; ) {
    /* Terminating the header block overwrites the last line's CRLF, so the
       final header ends at the NUL rather than at "\r\n". Missing that case
       hid whichever header the client happened to send last. */
    const char *eol      = strstr(p, "\r\n");
    size_t      line_len = eol ? (size_t)(eol - p) : strlen(p);

    if(line_len > nlen && strncasecmp(p, name, nlen) == 0 && p[nlen] == ':') {
      const char *v = p + nlen + 1;
      while(*v == ' ' || *v == '\t') v++;
      return v;
    }

    if(!eol) break;
    p = eol + 2;
  }
  return NULL;
}


/* Copies one header's value, trimmed, into `out`. 0 = header absent,
   -1 = present but too long for `out` (treated as hostile), else its length. */
static int
header_value(const char *headers, const char *name, char *out, size_t out_len) {
  out[0] = 0;
  const char *v = find_header(headers, name);
  if(!v) return 0;
  size_t n = 0;
  while(v[n] && v[n] != '\r' && v[n] != '\n') n++;
  while(n && (v[n - 1] == ' ' || v[n - 1] == '\t')) n--;
  if(n >= out_len) return -1;
  memcpy(out, v, n);
  out[n] = 0;
  return (int)n;
}


/* ------------------------------------------------------- cross-site guard
 *
 * The API has no login, and some of it is destructive: power off, moving a
 * game with "delete the source", killing payloads, rewriting the settings. A
 * web page open in the owner's browser can send a POST to the console's LAN
 * address without any permission — a "simple" cross-origin request needs no
 * preflight, and the body is parsed as JSON whatever its Content-Type says.
 * Two checks close that:
 *
 *   Host    DNS rebinding makes such a page re-resolve its own name to the
 *           console, so the Host header carries that name. Only an IP address,
 *           localhost, a bare local name or a well-known LAN suffix passes.
 *   Origin  Browsers add it to every cross-site POST. When it is there it has
 *           to name this very server. Tools like curl send none and pass.
 */
static int
host_allowed(const char *host) {
  char name[160];

  if(host[0] == '[') {                              /* [IPv6] or [IPv6]:port */
    const char *end = strchr(host, ']');
    if(!end || (size_t)(end - host) >= sizeof(name)) return 0;
    memcpy(name, host + 1, (size_t)(end - host - 1));
    name[end - host - 1] = 0;
    struct in6_addr a6;
    return inet_pton(AF_INET6, name, &a6) == 1;
  }

  const char *colon = strrchr(host, ':');
  size_t      len   = colon ? (size_t)(colon - host) : strlen(host);
  if(len == 0 || len >= sizeof(name)) return 0;
  memcpy(name, host, len);
  name[len] = 0;

  struct in_addr a4;
  if(inet_pton(AF_INET, name, &a4) == 1) return 1;
  if(!strchr(name, '.')) return 1;                  /* localhost, ps5-konsole */

  static const char *const lan[] = {
    ".local", ".lan", ".home", ".home.arpa", ".internal", ".localdomain",
    ".intranet", ".fritz.box",
  };
  for(size_t i = 0; i < sizeof(lan) / sizeof(lan[0]); i++) {
    size_t sl = strlen(lan[i]);
    if(len > sl && strcasecmp(name + len - sl, lan[i]) == 0) return 1;
  }
  return 0;
}


static int
origin_allowed(const char *origin, const char *host) {
  const char *p;
  if(!strncasecmp(origin, "http://", 7))       p = origin + 7;
  else if(!strncasecmp(origin, "https://", 8)) p = origin + 8;
  else return 0;                                    /* "null", file://, ... */
  size_t n = strcspn(p, "/");
  return host[0] && strlen(host) == n && strncasecmp(p, host, n) == 0;
}


/* Letters, digits and a few separators; everything else becomes '?'. Log lines
   also land in the kernel message buffer, which the game-state and button
   parsers read for markers such as "[…]" and "Name(0x…)" — and the text here
   is the stranger's own. A page that names its path "/MicMuteKeyPressed"
   must not get to put a button press into that buffer. */
static void
plain_text(const char *in, char *out, size_t out_len) {
  size_t o = 0;
  for(; in && *in && o + 1 < out_len; in++) {
    unsigned char c = (unsigned char)*in;
    int plain = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
                (c >= 'a' && c <= 'z') ||
                c == '.' || c == '-' || c == '_' || c == '/' || c == ':';
    out[o++] = plain ? (char)c : '?';
  }
  out[o] = 0;
}

/* Said at most every half minute: a hostile page must not be able to fill the
   log with its own refusals. */
static void
log_refused(const char *what, const char *method, const char *path,
            const char *value) {
  static uint64_t last_ms;
  uint64_t now  = ps5tm_mono_ms();
  uint64_t last = __atomic_load_n(&last_ms, __ATOMIC_RELAXED);
  if(last && now - last < 30000) return;
  __atomic_store_n(&last_ms, now, __ATOMIC_RELAXED);

  char m[12], p[64], v[64];
  plain_text(method, m, sizeof(m));
  plain_text(path,   p, sizeof(p));
  plain_text(value,  v, sizeof(v));
  PS5TM_WARN("http_refused",
             "Anfrage abgelehnt (%s): %s %s von %s. Eine fremde "
             "Webseite im Browser darf die Konsole nicht steuern.",
             what, m, p, v);
}


/* Sets the receive timeout to what is left of a budget, never less than a
   fifth of a second. */
static void
rcv_timeout_until(int fd, uint64_t deadline_ms) {
  uint64_t now  = ps5tm_mono_ms();
  long     left = deadline_ms > now ? (long)(deadline_ms - now) : 0;
  if(left < 200) left = 200;
  struct timeval tv = { left / 1000, (left % 1000) * 1000 };
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
}


static void *
serve_connection(void *arg) {
  int fd = (int)(intptr_t)arg;

  /* A wedged client must not tie up a thread forever. */
  struct timeval tv = { 10, 0 };
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

  char  *raw = malloc(MAX_HEADER_BYTES + 1);
  ps5tm_request_t req;
  memset(&req, 0, sizeof(req));

  struct sockaddr_in peer;
  socklen_t          peer_len = sizeof(peer);
  if(getpeername(fd, (struct sockaddr *)&peer, &peer_len) == 0 &&
     peer.sin_family == AF_INET)
    inet_ntop(AF_INET, &peer.sin_addr, req.peer, sizeof(req.peer));

  /* Out of memory used to close the connection without a word, which looks
     from the outside exactly like a crashed server and is miserable to
     diagnose. Say so instead — the reply below needs no allocation. */
  if(!raw) {
    static const char oom[] =
        "HTTP/1.1 503 Service Unavailable\r\n"
        "Content-Type: application/json; charset=utf-8\r\n"
        "Content-Length: 96\r\n"
        "Connection: close\r\n\r\n"
        "{\"ok\":false,\"code\":\"out_of_memory\","
        "\"message\":\"Zu wenig Arbeitsspeicher im Payload.\"}";
    (void)!write(fd, oom, sizeof(oom) - 1);
    PS5TM_ERROR("http_out_of_memory",
                "Für eine Anfrage konnten %d Bytes nicht belegt werden. "
                "Läuft der Payload mehrfach, oder ist das eingebettete "
                "Kachel-Paket zu groß?", MAX_HEADER_BYTES + 1);
    close(fd);
    return NULL;
  }

  /* Read until the end of the header block, within a fixed wall-clock budget
     rather than per read. */
  size_t   got = 0;
  char    *header_end = NULL;
  uint64_t started = ps5tm_mono_ms();
  while(got < MAX_HEADER_BYTES && ps5tm_mono_ms() - started < HEADER_BUDGET_MS) {
    rcv_timeout_until(fd, started + HEADER_BUDGET_MS);
    ssize_t n = read(fd, raw + got, MAX_HEADER_BYTES - got);
    if(n <= 0) break;
    got += (size_t)n;
    raw[got] = 0;
    if((header_end = strstr(raw, "\r\n\r\n")) != NULL) break;
  }

  if(!header_end) {
    ps5tm_http_send_error(fd, 400, "bad_request", "Ungültige Anfrage.");
    free(raw);
    close(fd);
    return NULL;
  }
  *header_end = 0;
  const char *headers    = strstr(raw, "\r\n");
  headers = headers ? headers + 2 : "";
  const char *body_start = header_end + 4;
  size_t      body_have  = got - (size_t)(body_start - raw);

  /* Request line: METHOD SP TARGET SP VERSION */
  char target[sizeof(req.path) + sizeof(req.query)];
  _Static_assert(sizeof(target) == 3584, "the request line's width below is written out for this size");
  if(sscanf(raw, "%7s %3583s", req.method, target) != 2) {
    ps5tm_http_send_error(fd, 400, "bad_request", "Ungültige Statuszeile.");
    free(raw);
    close(fd);
    return NULL;
  }

  char *qmark = strchr(target, '?');
  if(qmark) {
    *qmark = 0;
    snprintf(req.query, sizeof(req.query), "%s", qmark + 1);
  }
  snprintf(req.path, sizeof(req.path), "%.*s",
           (int)sizeof(req.path) - 1, target);

  /* Cross-site guard, before any handler or upload path sees the request. */
  {
    char host[160], origin[200];
    int  hl = header_value(headers, "Host",   host,   sizeof(host));
    int  ol = header_value(headers, "Origin", origin, sizeof(origin));
    int  reads = !strcmp(req.method, "GET") || !strcmp(req.method, "HEAD");
    const char *refused = NULL;
    if(hl < 0 || ol < 0)                      refused = "Kopfzeile zu lang";
    else if(hl > 0 && !host_allowed(host))    refused = "Host";
    else if(!reads && ol > 0 && !origin_allowed(origin, host))
                                              refused = "Origin";
    if(refused) {
      log_refused(refused, req.method, req.path, refused[0] == 'O' ? origin : host);
      ps5tm_http_send_error(fd, 403, "forbidden_origin",
          "Abgelehnt: Die Weboberfläche ist nur direkt über die IP-Adresse "
          "der Konsole zu bedienen, nicht von einer fremden Webseite aus.");
      free(raw);
      close(fd);
      return NULL;
    }
  }

  {
    char enc[160];
    req.gzip_ok = 0;
    if(header_value(headers, "Accept-Encoding", enc, sizeof(enc)) > 0)
      for(const char *c = enc; *c; c++)
        if(!strncasecmp(c, "gzip", 4)) { req.gzip_ok = 1; break; }
  }

  const char *cl = find_header(headers, "Content-Length");
  size_t want = cl ? (size_t)strtoul(cl, NULL, 10) : 0;

  /* The tile package is ten megabytes and must never be held in memory — that
     is what broke the app when it was embedded. It streams straight to disk
     instead, and is the only path allowed to exceed the body limit. */
  if(!strcmp(req.path, "/api/v1/tile/upload")) {
    /* The header budget is spent; from here on a sender may be silent for ten
       seconds at a time, and the handler keeps the clock on the whole. */
    rcv_timeout_until(fd, ps5tm_mono_ms() + 10000);
    ps5tm_api_receive_upload(fd, body_start, body_have, want);
    free(raw);
    close(fd);
    return NULL;
  }

  /* The avatar textures are a few hundred kilobytes together — under the body
     limit, but they arrive as raw bytes rather than JSON, so they take the
     same straight-to-disk path instead of being decoded into a request. */
  if(!strcmp(req.path, "/api/v1/profile/avatar/file")) {
    rcv_timeout_until(fd, ps5tm_mono_ms() + 10000);
    ps5tm_api_receive_avatar_file(fd, req.query, body_start, body_have, want);
    free(raw);
    close(fd);
    return NULL;
  }

  /* The text of a saved kernel log: a few megabytes of lines, which the page has
     and the console does not, so it comes as a raw body and goes to disk the same way. */
  if(!strcmp(req.path, "/api/v1/klog/save") && !strcmp(req.method, "POST")) {
    rcv_timeout_until(fd, ps5tm_mono_ms() + 10000);
    ps5tm_api_receive_klog_save(fd, req.query, body_start, body_have, want);
    free(raw);
    close(fd);
    return NULL;
  }

  /* A file from the file manager: any size, straight to disk (filemgr.c). */
  if(!strcmp(req.path, "/api/v1/files/upload") && !strcmp(req.method, "POST")) {
    rcv_timeout_until(fd, ps5tm_mono_ms() + 10000);
    ps5tm_filemgr_receive_upload(fd, req.query, body_start, body_have, want);
    free(raw);
    close(fd);
    return NULL;
  }

  if(want > MAX_BODY_BYTES) {
    ps5tm_http_send_error(fd, 400, "payload_too_large",
                          "Anfrage ist zu groß.");
    free(raw);
    close(fd);
    return NULL;
  }

  if(want) {
    req.body = malloc(want + 1);
    if(!req.body) {
      ps5tm_http_send_error(fd, 500, "alloc_failed", "Kein Speicher.");
      free(raw);
      close(fd);
      return NULL;
    }
    size_t   have = body_have < want ? body_have : want;
    uint64_t body_deadline = ps5tm_mono_ms() + BODY_BUDGET_MS;
    memcpy(req.body, body_start, have);
    while(have < want && ps5tm_mono_ms() < body_deadline) {
      rcv_timeout_until(fd, body_deadline);
      ssize_t n = read(fd, req.body + have, want - have);
      if(n <= 0) break;
      have += (size_t)n;
    }
    req.body[have] = 0;
    req.body_len   = have;
  }

  ps5tm_api_handle(fd, &req);

  free(req.body);
  free(raw);
  close(fd);
  return NULL;
}


static void *
connection_thread(void *arg) {
  serve_connection(arg);
  atomic_fetch_sub(&g_connections, 1);
  return NULL;
}


int
ps5tm_http_bind(unsigned port) {
  int srv = socket(AF_INET, SOCK_STREAM, 0);
  if(srv < 0) {
    PS5TM_ERROR("http_socket_failed",
                "Socket konnte nicht erstellt werden (errno %d).", errno);
    return -1;
  }

  int one = 1;
  setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port   = htons((uint16_t)port);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);

  /* The address comes from the settings, which the web page itself writes: a
     typo there must never leave the page unreachable for good. inet_addr()
     read garbage as 255.255.255.255, bind() then failed on every retry, and
     the saved setting made it fail again after each restart — with the page
     that could have corrected it being the thing that was down. */
  char bind_addr[sizeof(g_config.bind_address)];
  ps5tm_config_lock();
  snprintf(bind_addr, sizeof(bind_addr), "%s", g_config.bind_address);
  ps5tm_config_unlock();

  int wildcard = 1;
  if(bind_addr[0] && strcmp(bind_addr, "0.0.0.0")) {
    struct in_addr want;
    if(inet_pton(AF_INET, bind_addr, &want) == 1) {
      addr.sin_addr = want;
      wildcard = 0;
    } else {
      PS5TM_WARN("http_bind_address_invalid",
                 "Die Bind-Adresse \"%.40s\" ist keine IPv4-Adresse; die "
                 "Weboberfläche lauscht stattdessen auf allen Adressen.",
                 bind_addr);
    }
  }

  int brc = bind(srv, (struct sockaddr *)&addr, sizeof(addr));
  if(brc != 0 && !wildcard && errno == EADDRNOTAVAIL) {
    PS5TM_WARN("http_bind_address_unavailable",
               "Die Konsole hat die Adresse %.40s nicht; die Weboberfläche "
               "lauscht stattdessen auf allen Adressen.", bind_addr);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    brc = bind(srv, (struct sockaddr *)&addr, sizeof(addr));
  }

  if(brc != 0) {
    int eno = errno;
    close(srv);
    /* EADDRINUSE is reported separately so the caller can pick another port
       instead of retrying the same busy one forever. SO_REUSEADDR above only
       covers sockets in TIME_WAIT, not a live listener. */
    if(eno == EADDRINUSE) return -2;
    PS5TM_ERROR("http_bind_failed",
                "Die Weboberfläche konnte Port %u nicht belegen "
                "(Fehler %d: %s).", port, eno, strerror(eno));
    return -1;
  }

  if(listen(srv, 16) != 0) {
    int eno = errno;
    close(srv);
    PS5TM_ERROR("http_listen_failed",
                "listen() auf Port %u fehlgeschlagen (errno %d).", port, eno);
    return -1;
  }

  return srv;
}


int
ps5tm_http_port_free(unsigned port) {
  int s = socket(AF_INET, SOCK_STREAM, 0);
  if(s < 0) return 1;                          /* cannot tell; do not stand in the way */

  int one = 1;
  setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family      = AF_INET;
  addr.sin_port        = htons((uint16_t)port);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  int free_now = (bind(s, (struct sockaddr *)&addr, sizeof(addr)) == 0);
  close(s);
  return free_now;
}


int
ps5tm_http_run(int srv, unsigned port) {
  PS5TM_INFO("http_started",
             "Weboberfläche gestartet "
             "(Port %u).", port);

  for(;;) {
    /* A one-second accept timeout keeps the loop responsive to a port
       change made through the settings page. */
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(srv, &rfds);
    struct timeval tv = { 1, 0 };

    int rc = select(srv + 1, &rfds, NULL, NULL, &tv);

    ps5tm_config_lock();
    unsigned desired = g_config.http_port;
    ps5tm_config_unlock();
    ps5tm_config_tick();           /* a timed probe session may be due */
    if(desired != port) {
      close(srv);
      PS5TM_INFO("http_rebind",
                 "Port geändert: %u → %u, Server wird neu gebunden.",
                 port, desired);
      return 0;
    }

    /* A dead listener does not block — select() returns -1 immediately, every
     * time. `rc <= 0 → continue` then stops being a one-second idle wait and
     * becomes a loop with nothing in it: one core pinned at 100 % inside a
     * program whose entire purpose is to keep the console cool, and the web
     * UI silent while the fan controller carries on regulating.
     *
     * The socket can die under us — the console suspends and resumes, the
     * network drops, the address changes. Nothing here can repair that, so
     * the listener is handed back instead: returning 0 lands in the bind loop
     * in main.c, which is already built to open a fresh one and to wait
     * patiently if it cannot. EINTR is the one case that means nothing at
     * all. */
    if(rc < 0) {
      if(errno == EINTR) continue;
      int eno = errno;
      close(srv);
      PS5TM_WARN("http_listener_lost",
                 "Die Weboberfläche hat ihren Zugang verloren (Fehler %d: %s) "
                 "— das passiert typischerweise nach dem Ruhezustand oder "
                 "einem Netzwerkwechsel. Sie wird auf Port %u neu geöffnet; "
                 "die Lüftersteuerung lief die ganze Zeit weiter.",
                 eno, strerror(eno), port);
      return 0;
    }

    if(rc == 0) continue;                      /* the one-second idle tick */
    if(!FD_ISSET(srv, &rfds)) continue;

    int cl_fd = accept(srv, NULL, NULL);
    if(cl_fd < 0) {
      /* A single connection going away between select() and accept() is
         routine and says nothing about the listener. Anything else means the
         listener itself is gone, and gets the same treatment as above. */
      if(errno == EINTR || errno == ECONNABORTED ||
         errno == EAGAIN || errno == EWOULDBLOCK) continue;
      int eno = errno;
      close(srv);
      PS5TM_WARN("http_accept_failed",
                 "Die Weboberfläche kann keine Verbindungen mehr annehmen "
                 "(Fehler %d: %s). Sie wird auf Port %u neu geöffnet.",
                 eno, strerror(eno), port);
      return 0;
    }

    int nodelay = 1;
    setsockopt(cl_fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));

    /* Over the cap: say so and hang up, rather than add another thread. */
    if(atomic_load(&g_connections) >= MAX_CONNECTIONS) {
      struct timeval sndtv = { 1, 0 };
      setsockopt(cl_fd, SOL_SOCKET, SO_SNDTIMEO, &sndtv, sizeof(sndtv));
      ps5tm_http_send_error(cl_fd, 503, "too_many_connections",
                            "Zu viele gleichzeitige Verbindungen.");
      close(cl_fd);
      continue;
    }
    atomic_fetch_add(&g_connections, 1);

    pthread_t tid;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    int trc = pthread_create(&tid, &attr, connection_thread,
                             (void *)(intptr_t)cl_fd);
    if(trc != 0) {
      atomic_fetch_sub(&g_connections, 1);
      /* The other way a request can die without a trace: no thread, so
         nobody ever reads it. From the outside this is indistinguishable
         from a crashed server, so it gets said out loud. */
      static const char busy[] =
          "HTTP/1.1 503 Service Unavailable\r\n"
          "Content-Type: application/json; charset=utf-8\r\n"
          "Content-Length: 88\r\n"
          "Connection: close\r\n\r\n"
          "{\"ok\":false,\"code\":\"no_thread\","
          "\"message\":\"Kein Arbeitsthread verfügbar.\"}";
      (void)!write(cl_fd, busy, sizeof(busy) - 1);
      PS5TM_ERROR("http_thread_failed",
                  "Eine Anfrage konnte nicht bearbeitet werden, die Konsole "
                  "gab keinen Arbeitsplatz mehr her (Fehler %d). Läuft diese "
                  "App womöglich mehrfach?", trc);
      close(cl_fd);
    }
    pthread_attr_destroy(&attr);
  }
}
