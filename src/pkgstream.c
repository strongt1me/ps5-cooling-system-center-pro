/* The server the system's installer reads a package from (05.10.2026).
 *
 * The console's install library does not take a package from a file. It is given an address and fetches the
 * bytes itself, with HTTP range requests, the way it would from a server on the network. For a package that lies on
 * a stick (or in parts on several sticks and discs) this module is that server: on the loopback address only, for
 * the length of one installation, with one package behind one file name (and the icon behind another) and nothing
 * else — no listing, no second file, no other address, no way to name a path.
 *
 * What it keeps track of is what the installer was sent. The installation is only over when the system says so
 * AND every byte of the package has gone out at least once: a title can be reported "playable" long before the last
 * chunk has been fetched, and a server that went away then would leave the rest of it undone. The bytes that went
 * out are kept as a list of intervals (the installer asks for big pieces, mostly in order, sometimes several at a
 * time), merged as they touch; the list has a bound, and a client that outgrows it is no longer judged: "complete"
 * is then never claimed (a count of bytes sent cannot tell a byte sent twice from one not sent), and the caller
 * has to go by what the system itself says it has received.
 *
 * HTTP/1.1 as far as the installer needs it: GET and HEAD, one range ("bytes=a-b", "a-", "-n"; several ranges are
 * refused), 200, 206, 416, 404, 405, 400, 431, 503, keep-alive. A request body is not accepted. Eight connections at
 * most, each with its own half-megabyte buffer: the console's memory is not for spending. */

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "pkginst.h"
#include "pkginst_ipc.h"

#define PS_MAX_CONNS    16
#define PS_CHUNK        (512u * 1024u)
#define PS_REQ_MAX      (16u * 1024u)
#ifndef PS_IDLE_MS                     /* the tests shorten both */
#define PS_IDLE_MS      60000u         /* a connection nothing arrives on for this long is closed */
#endif
#ifndef PS_STALL_MS
#define PS_STALL_MS     30000u         /* a client that takes nothing for this long is dropped */
#endif
#ifndef PS_HEAD_MS
#define PS_HEAD_MS      10000u         /* a request head that takes longer than this from its first byte is not a client's */
#endif
#ifndef PS_STOP_WAIT_MS
#define PS_STOP_WAIT_MS 10000          /* stop() waits this long for connections to end before it gives up on them */
#endif
#define PS_MAX_REQ      1000           /* requests on one connection; then the client reconnects */
#define PS_LOG_REQ      6              /* requests of one session that are written to the log: the first ones tell the story */
#define PS_LOG_ODD      6              /* ... and refused or odd ones */
#define PS_COV_MAX      1024
#define PS_MAX_SLICES   512
#define PS_PREFIX       "/stream/install/"

static struct {
  pthread_mutex_t lock;
  int             running, leaked;
  int             stop;
  int             listen_fd;
  pthread_t       accept_th;
  ps5tm_pkgslice_t *slices;
  unsigned        nslices;
  uint64_t        total;
  char            name[160], icon_name[160];
  unsigned char  *icon;
  size_t          icon_n;
  unsigned        conns, peak_conns;
  int             cfd[PS_MAX_CONNS];             /* the connections' sockets, -1 for a free place: stop() shuts them down */
  uint64_t        served, requests, read_errors, last_io_ms;
  uint64_t        cov[2 * PS_COV_MAX];
  unsigned        ncov;
  int             cov_overflow;
  unsigned        logged_req, logged_odd;
} g = { .lock = PTHREAD_MUTEX_INITIALIZER };

/* The log of one session is short on purpose (a package takes hours and the console asks thousands of times), but
   the first requests and every odd one are what a person debugging a failed installation has to go by. */
static int
log_slot(unsigned *counter, unsigned cap) {
  pthread_mutex_lock(&g.lock);
  int ok = *counter < cap;
  if(ok) (*counter)++;
  pthread_mutex_unlock(&g.lock);
  return ok;
}

#define LOG_REQ(...) do { if(log_slot(&g.logged_req, PS_LOG_REQ)) PS5TM_INFO("pkg_stream_request", __VA_ARGS__); } while(0)
#define LOG_ODD(...) do { if(log_slot(&g.logged_odd, PS_LOG_ODD)) PS5TM_WARN("pkg_stream_odd", __VA_ARGS__); } while(0)

/* ------------------------------------------------------------------ what was sent */

static int
stopping(void) { return __atomic_load_n(&g.stop, __ATOMIC_ACQUIRE); }

/* [a, b) joins the sorted list of disjoint intervals; those it touches are merged. Locked. */
static void
cov_add(uint64_t a, uint64_t b) {
  if(a >= b) return;
  unsigned i = 0;
  while(i < g.ncov && g.cov[2 * i + 1] < a) i++;
  unsigned j = i;
  uint64_t na = a, nb = b;
  while(j < g.ncov && g.cov[2 * j] <= b) {
    if(g.cov[2 * j] < na) na = g.cov[2 * j];
    if(g.cov[2 * j + 1] > nb) nb = g.cov[2 * j + 1];
    j++;
  }
  if(j == i) {                                           /* touches none: a new interval at i */
    if(g.ncov >= PS_COV_MAX) { g.cov_overflow = 1; return; }
    memmove(&g.cov[2 * (i + 1)], &g.cov[2 * i], (size_t)(g.ncov - i) * 2 * sizeof(uint64_t));
    g.cov[2 * i] = na;
    g.cov[2 * i + 1] = nb;
    g.ncov++;
    return;
  }
  g.cov[2 * i] = na;                                     /* merged into the first one it touched */
  g.cov[2 * i + 1] = nb;
  if(j - i > 1) {
    memmove(&g.cov[2 * (i + 1)], &g.cov[2 * j], (size_t)(g.ncov - j) * 2 * sizeof(uint64_t));
    g.ncov -= j - i - 1;
  }
}

static uint64_t
covered_locked(void) {
  uint64_t sum = 0;
  for(unsigned i = 0; i < g.ncov; i++) sum += g.cov[2 * i + 1] - g.cov[2 * i];
  return sum;
}

void
ps5tm_pkgstream_stats(ps5tm_pkgstream_stats_t *st) {
  memset(st, 0, sizeof(*st));
  pthread_mutex_lock(&g.lock);
  st->running = g.running;
  st->total = g.total;
  st->served = g.served;
  st->covered = covered_locked();
  st->coverage_lost = g.cov_overflow;
  st->complete = g.running && g.total > 0 && !g.cov_overflow && st->covered >= g.total;
  if(g.cov_overflow && st->covered < g.served) st->covered = g.served < g.total ? g.served : g.total;
  st->requests = g.requests;
  st->read_errors = g.read_errors;
  st->conns = g.conns;
  st->peak_conns = g.peak_conns;
  st->idle_ms = g.last_io_ms ? ps5tm_mono_ms() - g.last_io_ms : 0;
  pthread_mutex_unlock(&g.lock);
}

/* ------------------------------------------------------------------ reading the package */

/* The slice that holds logical offset off (the slices are sorted and have no gaps), or UINT32_MAX. */
static unsigned
find_slice(uint64_t off) {
  unsigned lo = 0, hi = g.nslices;
  while(lo < hi) {
    unsigned mid = lo + (hi - lo) / 2;
    const ps5tm_pkgslice_t *s = &g.slices[mid];
    if(off < s->logical) hi = mid;
    else if(off >= s->logical + s->size) lo = mid + 1;
    else return mid;
  }
  return UINT32_MAX;
}

typedef struct {
  int      fd;
  unsigned idx;
  int      err;                          /* why the last read failed: errno; 0: the file is shorter than measured; -1: it is not the file the search saw */
  unsigned bad_idx;                      /* ... and in which slice */
} rdr_t;

/* Exactly len bytes from logical offset off, across slices. 0, or -1. */
static int
pkg_read(rdr_t *r, uint64_t off, unsigned char *buf, size_t len) {
  while(len) {
    unsigned k = find_slice(off);
    if(k == UINT32_MAX) return -1;
    const ps5tm_pkgslice_t *s = &g.slices[k];
    uint64_t in = off - s->logical, avail = s->size - in;
    size_t n = len < avail ? len : (size_t)avail;
    if(r->fd < 0 || r->idx != k) {
      if(r->fd >= 0) close(r->fd);
      r->idx = k;
      /* A link or a pipe put in the file's place since the search is not the package: no following, and no waiting
         for a writer on a pipe. */
      r->fd = open(s->path, O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
      struct stat st;
      int changed = 0;
      if(r->fd >= 0 && (fstat(r->fd, &st) != 0 || !S_ISREG(st.st_mode))) { close(r->fd); r->fd = -1; errno = EPERM; }
      else if(r->fd >= 0 && s->file_size && ((uint64_t)st.st_size != s->file_size || (int64_t)st.st_mtime != s->mtime)) { close(r->fd); r->fd = -1; changed = 1; }
      if(r->fd < 0) { r->err = changed ? -1 : errno; r->bad_idx = k; return -1; }
    }
    ssize_t got = pread(r->fd, buf, n, (off_t)(s->file_off + in));
    if(got < 0 && errno == EINTR) continue;
    if(got <= 0) { r->err = got < 0 ? errno : 0; r->bad_idx = k; return -1; }       /* short: the file is not what was measured */
    buf += got;
    off += (uint64_t)got;
    len -= (size_t)got;
  }
  return 0;
}

/* ------------------------------------------------------------------ one connection */

static int
send_all(int fd, const void *buf, size_t len) {
  const unsigned char *p = buf;
  uint64_t stalled_since = ps5tm_mono_ms();
  while(len) {
    if(stopping()) return -1;
    ssize_t n = send(fd, p, len, MSG_NOSIGNAL);
    if(n > 0) {
      p += n;
      len -= (size_t)n;
      stalled_since = ps5tm_mono_ms();
      continue;
    }
    if(n < 0 && errno == EINTR) continue;
    if(n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {                 /* SO_SNDTIMEO ran out: look at the clock */
      if(ps5tm_mono_ms() - stalled_since > PS_STALL_MS) return -1;
      continue;
    }
    return -1;
  }
  return 0;
}

typedef struct {
  char method[8];
  char target[256];
  int  http11;                           /* HTTP/1.1 (1) or 1.0 (0) */
  int  close_it;                         /* Connection: close */
  int  keep_it;                          /* Connection: keep-alive */
  int  has_range;
  char range[160];
  int  body;                             /* a Content-Length above 0, or any Transfer-Encoding */
} req_t;

static const char *
skip_ws(const char *s) { while(*s == ' ' || *s == '\t') s++; return s; }

/* hdr: the request head, the blank line cut off, NUL-terminated. 0 well-formed, -1 not. */
static int
parse_request(char *hdr, req_t *r) {
  memset(r, 0, sizeof(*r));
  char *eol = strpbrk(hdr, "\r\n");
  if(!eol) eol = hdr + strlen(hdr);
  char first[512];
  size_t fl = (size_t)(eol - hdr);
  if(fl == 0 || fl >= sizeof(first)) return -1;
  memcpy(first, hdr, fl);
  first[fl] = 0;
  char *sp1 = strchr(first, ' ');
  if(!sp1) return -1;
  *sp1++ = 0;
  char *sp2 = strchr(sp1, ' ');
  if(!sp2) return -1;
  *sp2++ = 0;
  if(strlen(first) >= sizeof(r->method) || strlen(sp1) >= sizeof(r->target) || !first[0] || !sp1[0]) return -1;
  snprintf(r->method, sizeof(r->method), "%s", first);
  snprintf(r->target, sizeof(r->target), "%s", sp1);
  if(!strcmp(sp2, "HTTP/1.1")) r->http11 = 1;
  else if(!strcmp(sp2, "HTTP/1.0")) r->http11 = 0;
  else return -1;

  for(char *line = eol; *line;) {
    while(*line == '\r' || *line == '\n') line++;
    if(!*line) break;
    char *e = strpbrk(line, "\r\n");
    size_t ll = e ? (size_t)(e - line) : strlen(line);
    char *colon = memchr(line, ':', ll);
    if(colon) {
      size_t nl = (size_t)(colon - line);
      const char *v = colon + 1;
      char val[256];
      size_t vl = ll - nl - 1;
      if(vl >= sizeof(val)) vl = sizeof(val) - 1;
      memcpy(val, v, vl);
      val[vl] = 0;
      const char *vv = skip_ws(val);
      if(nl == 5 && !strncasecmp(line, "Range", 5)) { r->has_range = 1; snprintf(r->range, sizeof(r->range), "%s", vv); }
      else if(nl == 10 && !strncasecmp(line, "Connection", 10)) {
        if(strcasestr(vv, "close")) r->close_it = 1;
        if(strcasestr(vv, "keep-alive")) r->keep_it = 1;
      } else if(nl == 14 && !strncasecmp(line, "Content-Length", 14)) {
        if(strtoull(vv, NULL, 10) != 0) r->body = 1;
      } else if(nl == 17 && !strncasecmp(line, "Transfer-Encoding", 17)) {
        r->body = 1;
      }
    }
    line += ll;
  }
  return 0;
}

/* "bytes=a-b", "a-", "-n" against a package of total bytes. 0 with [*a, *b] (inclusive); -1 not satisfiable or not
   a range we serve (several ranges, another unit, garbage). */
static int
parse_range(const char *v, uint64_t total, uint64_t *a, uint64_t *b) {
  if(total == 0) return -1;
  if(strncasecmp(v, "bytes=", 6) != 0) return -1;
  v = skip_ws(v + 6);
  if(strchr(v, ',')) return -1;
  uint64_t x, y;
  char *end;
  if(*v == '-') {                                                     /* the last n bytes */
    if(v[1] < '0' || v[1] > '9') return -1;
    errno = 0;
    uint64_t n = strtoull(v + 1, &end, 10);
    if(errno || *skip_ws(end)) return -1;
    if(n == 0) return -1;
    if(n > total) n = total;
    *a = total - n;
    *b = total - 1;
    return 0;
  }
  if(*v < '0' || *v > '9') return -1;
  errno = 0;
  x = strtoull(v, &end, 10);
  if(errno || *end != '-') return -1;
  end++;
  if(*skip_ws(end) == 0) {
    y = total - 1;
  } else {
    if(*end < '0' || *end > '9') return -1;
    errno = 0;
    y = strtoull(end, &end, 10);
    if(errno || *skip_ws(end)) return -1;
  }
  if(x >= total || y < x) return -1;
  if(y >= total) y = total - 1;
  *a = x;
  *b = y;
  return 0;
}

static void
http_date(char *out, size_t n) {
  time_t t = time(NULL);
  struct tm tm;
  gmtime_r(&t, &tm);
  static const char *const wd[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
  static const char *const mo[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
  snprintf(out, n, "%s, %02d %s %04d %02d:%02d:%02d GMT", wd[tm.tm_wday], tm.tm_mday, mo[tm.tm_mon], tm.tm_year + 1900,
           tm.tm_hour, tm.tm_min, tm.tm_sec);
}

static int
send_simple(int fd, int code, const char *text, const char *extra, int keep) {
  char date[48], head[640], body[96];
  http_date(date, sizeof(date));
  int bl = snprintf(body, sizeof(body), "%s\n", text);
  int hl = snprintf(head, sizeof(head),
                    "HTTP/1.1 %d %s\r\nDate: %s\r\nContent-Type: text/plain\r\nContent-Length: %d\r\n%sConnection: %s\r\n\r\n",
                    code, text, date, bl, extra ? extra : "", keep ? "keep-alive" : "close");
  if(send_all(fd, head, (size_t)hl) != 0) return -1;
  return send_all(fd, body, (size_t)bl);
}

/* path is exactly PS_PREFIX followed by name. */
static int
under_prefix(const char *path, const char *name) {
  return strncmp(path, PS_PREFIX, sizeof(PS_PREFIX) - 1) == 0 && !strcmp(path + sizeof(PS_PREFIX) - 1, name);
}

/* Returns 1 to keep the connection, 0 to close it. */
static int
serve_one(int fd, const req_t *r, rdr_t *rd, unsigned char **buf) {
  int keep = r->http11 ? !r->close_it : r->keep_it;
  int head_only = !strcmp(r->method, "HEAD");
  if(r->body) { LOG_ODD("Server: eine Anfrage mit Inhalt (%.8s) wird abgewiesen, 400.", r->method); send_simple(fd, 400, "Bad Request", NULL, 0); return 0; }
  if(strcmp(r->method, "GET") != 0 && !head_only) {
    LOG_ODD("Server: Methode %.8s wird abgewiesen, 405.", r->method);
    send_simple(fd, 405, "Method Not Allowed", "Allow: GET, HEAD\r\n", keep);
    return keep;
  }

  char path[sizeof(r->target)];
  snprintf(path, sizeof(path), "%s", r->target);
  char *q = strpbrk(path, "?#");
  if(q) *q = 0;
  int is_pkg = under_prefix(path, g.name);
  int is_icon = !is_pkg && g.icon && g.icon_name[0] && under_prefix(path, g.icon_name);
  if(!is_pkg && !is_icon) {
    LOG_ODD("Server: nach %.80s wurde gefragt, es gibt nur das Paket (und sein Bild): 404.", path);
    send_simple(fd, 404, "Not Found", NULL, keep);
    return keep;
  }

  uint64_t total = is_pkg ? g.total : g.icon_n;
  uint64_t a = 0, b = total ? total - 1 : 0;
  int partial = 0;
  if(r->has_range && total) {
    if(parse_range(r->range, total, &a, &b) != 0) {
      LOG_ODD("Server: der Bereich \"%.60s\" ist bei %llu Bytes nicht erfüllbar: 416.", r->range, (unsigned long long)total);
      char extra[96];
      snprintf(extra, sizeof(extra), "Content-Range: bytes */%llu\r\n", (unsigned long long)total);
      send_simple(fd, 416, "Range Not Satisfiable", extra, keep);
      return keep;
    }
    partial = 1;
  }
  uint64_t n = total ? b - a + 1 : 0;

  char date[48], head[768];
  http_date(date, sizeof(date));
  int hl;
  if(partial)
    hl = snprintf(head, sizeof(head),
                  "HTTP/1.1 206 Partial Content\r\nDate: %s\r\nLast-Modified: Thu, 01 Jan 2026 00:00:00 GMT\r\n"
                  "Content-Type: %s\r\nAccept-Ranges: bytes\r\nContent-Range: bytes %llu-%llu/%llu\r\n"
                  "Content-Length: %llu\r\nContent-Disposition: attachment; filename=\"%s\"\r\nConnection: %s\r\n\r\n",
                  date, is_pkg ? "application/octet-stream" : "image/png", (unsigned long long)a, (unsigned long long)b,
                  (unsigned long long)total, (unsigned long long)n, is_pkg ? g.name : g.icon_name,
                  keep ? "keep-alive" : "close");
  else
    hl = snprintf(head, sizeof(head),
                  "HTTP/1.1 200 OK\r\nDate: %s\r\nLast-Modified: Thu, 01 Jan 2026 00:00:00 GMT\r\n"
                  "Content-Type: %s\r\nAccept-Ranges: bytes\r\nContent-Length: %llu\r\n"
                  "Content-Disposition: attachment; filename=\"%s\"\r\nConnection: %s\r\n\r\n",
                  date, is_pkg ? "application/octet-stream" : "image/png", (unsigned long long)n,
                  is_pkg ? g.name : g.icon_name, keep ? "keep-alive" : "close");
  if(send_all(fd, head, (size_t)hl) != 0) return 0;

  pthread_mutex_lock(&g.lock);
  g.requests++;
  g.last_io_ms = ps5tm_mono_ms();
  pthread_mutex_unlock(&g.lock);
  LOG_REQ("Server: %s %s %s %llu Bytes: %d.", r->method, is_pkg ? "Paket" : "Bild",
          partial ? "Bereich" : "ganz,", (unsigned long long)n, partial ? 206 : 200);
  if(head_only || n == 0) return keep;

  if(is_icon) {
    if(send_all(fd, g.icon + a, (size_t)n) != 0) return 0;
    return keep;
  }
  if(!*buf) {
    *buf = malloc(PS_CHUNK);
    if(!*buf) return 0;
  }
  while(n) {
    size_t c = n < PS_CHUNK ? (size_t)n : PS_CHUNK;
    if(pkg_read(rd, a, *buf, c) != 0) {
      pthread_mutex_lock(&g.lock);
      g.read_errors++;
      pthread_mutex_unlock(&g.lock);
      const char *why = rd->err > 0 ? strerror(rd->err) : rd->err < 0 ? "die Datei hat sich seit der Suche verändert" : "die Datei ist kürzer als bei der Suche";
      const char *file = g.slices && rd->bad_idx < g.nslices ? strrchr(g.slices[rd->bad_idx].path, '/') : NULL;
      PS5TM_WARN("pkg_stream_read_error", "Server: Lesefehler bei Byte %llu in Teil %u (%.60s): %.60s.", (unsigned long long)a,
                 rd->bad_idx + 1, file ? file + 1 : "?", why);
      return 0;                                           /* the length was promised: the only honest thing is to drop it */
    }
    if(send_all(fd, *buf, c) != 0) return 0;
    pthread_mutex_lock(&g.lock);
    cov_add(a, a + c);
    g.served += c;
    g.last_io_ms = ps5tm_mono_ms();
    pthread_mutex_unlock(&g.lock);
    a += c;
    n -= c;
  }
  return keep;
}

/* Reads one request head into buf (a NUL after it). Bytes of a request that follows stay behind in buf, counted in
   *have. 1 with the length of the head (the blank line not included) in *head and the blank line's length in *skip;
   0 on a clean end (closed, idle, stopping); -1 when the head does not fit. */
static int
read_head(int fd, char *buf, size_t cap, size_t *have, size_t *head, size_t *skip) {
  uint64_t t0 = ps5tm_mono_ms();
  uint64_t t_first = *have ? t0 : 0;                        /* when the head began (bytes of it were already here, or the first came) */
  for(;;) {
    buf[*have] = 0;
    char *end = strstr(buf, "\r\n\r\n");
    size_t gap = 4;
    char *end2 = strstr(buf, "\n\n");
    if(end2 && (!end || end2 < end)) { end = end2; gap = 2; }
    if(end) {
      *head = (size_t)(end - buf);
      *skip = gap;
      return 1;
    }
    if(*have >= cap - 1) return -1;
    if(stopping()) return 0;
    /* Checked on every pass, not only when the read timed out: a client that sends a byte every half second never
       lets it time out, and eight of them would hold every slot for good. */
    uint64_t now = ps5tm_mono_ms();
    if(now - t0 > PS_IDLE_MS) return 0;
    if(t_first && now - t_first > PS_HEAD_MS) return 0;
    ssize_t n = recv(fd, buf + *have, cap - 1 - *have, 0);
    if(n > 0) { if(!t_first) t_first = ps5tm_mono_ms(); *have += (size_t)n; continue; }
    if(n == 0) return 0;
    if(errno == EINTR) continue;
    if(errno == EAGAIN || errno == EWOULDBLOCK) continue;                      /* SO_RCVTIMEO ran out: the clock decides */
    return 0;
  }
}

/* The socket of a connection is in g.cfd from the moment its thread runs until just before it closes it, both under the
   lock, so that stop() never shuts down a descriptor somebody else has been given since. */
static void
conn_register(int fd) {
  pthread_mutex_lock(&g.lock);
  for(unsigned i = 0; i < PS_MAX_CONNS; i++)
    if(g.cfd[i] < 0) { g.cfd[i] = fd; break; }
  pthread_mutex_unlock(&g.lock);
}

static void
conn_unregister(int fd) {
  pthread_mutex_lock(&g.lock);
  for(unsigned i = 0; i < PS_MAX_CONNS; i++)
    if(g.cfd[i] == fd) g.cfd[i] = -1;
  pthread_mutex_unlock(&g.lock);
}

static void *
conn_main(void *arg) {
  int fd = (int)(intptr_t)arg;
  conn_register(fd);
  struct timeval tv = { 2, 0 };
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#ifdef SO_NOSIGPIPE
  int one = 1;
  setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
  char *rbuf = malloc(PS_REQ_MAX);
  unsigned char *body = NULL;
  rdr_t rd = { -1, 0, 0, 0 };
  size_t have = 0;
  for(int count = 0; rbuf && count < PS_MAX_REQ && !stopping(); count++) {
    size_t head = 0, skip = 0;
    int got = read_head(fd, rbuf, PS_REQ_MAX, &have, &head, &skip);
    if(got < 0) { send_simple(fd, 431, "Request Header Fields Too Large", NULL, 0); break; }
    if(got == 0) break;
    char *copy = malloc(head + 1);
    if(!copy) break;
    memcpy(copy, rbuf, head);
    copy[head] = 0;
    req_t r;
    int keep = 0;
    if(parse_request(copy, &r) != 0) send_simple(fd, 400, "Bad Request", NULL, 0);
    else keep = serve_one(fd, &r, &rd, &body);
    free(copy);
    size_t used = head + skip;
    if(used < have) { memmove(rbuf, rbuf + used, have - used); have -= used; } else have = 0;
    if(!keep) break;
  }
  free(rbuf);
  free(body);
  if(rd.fd >= 0) close(rd.fd);
  conn_unregister(fd);
  shutdown(fd, SHUT_RDWR);
  close(fd);
  pthread_mutex_lock(&g.lock);
  g.conns--;
  pthread_mutex_unlock(&g.lock);
  return NULL;
}

static void *
accept_main(void *arg) {
  (void)arg;
  while(!stopping()) {
    struct pollfd p = { g.listen_fd, POLLIN, 0 };
    int r = poll(&p, 1, 200);
    if(r <= 0) continue;
    struct sockaddr_in sa;
    socklen_t sl = sizeof(sa);
    int c = accept(g.listen_fd, (struct sockaddr *)&sa, &sl);
    if(c < 0) continue;
    if(sa.sin_addr.s_addr != htonl(INADDR_LOOPBACK)) { close(c); continue; }        /* bound to loopback; belt and braces */
    pthread_mutex_lock(&g.lock);
    int full = g.conns >= PS_MAX_CONNS;
    if(!full) { g.conns++; if(g.conns > g.peak_conns) g.peak_conns = g.conns; }
    pthread_mutex_unlock(&g.lock);
    if(full) {
      LOG_ODD("Server: alle %d Verbindungen sind belegt, eine weitere wird abgewiesen (503).", PS_MAX_CONNS);
      send_simple(c, 503, "Service Unavailable", NULL, 0);
      close(c);
      continue;
    }
    pthread_t th;
    pthread_attr_t at;
    pthread_attr_init(&at);
    pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
    pthread_attr_setstacksize(&at, 256 * 1024);
    int rc = pthread_create(&th, &at, conn_main, (void *)(intptr_t)c);
    pthread_attr_destroy(&at);
    if(rc != 0) {
      close(c);
      pthread_mutex_lock(&g.lock);
      g.conns--;
      pthread_mutex_unlock(&g.lock);
    }
  }
  return NULL;
}

/* ------------------------------------------------------------------ start and stop */

int
ps5tm_pkgstream_start(const ps5tm_pkgstream_cfg_t *cfg, char *err, size_t err_len) {
  err[0] = 0;
  if(!cfg || !cfg->slices || cfg->nslices == 0 || cfg->nslices > PS_MAX_SLICES || !cfg->name || !cfg->name[0] ||
     strlen(cfg->name) >= sizeof(g.name) || strchr(cfg->name, '/') || strchr(cfg->name, '"') ||
     (cfg->icon && (!cfg->icon_name || !cfg->icon_name[0] || strlen(cfg->icon_name) >= sizeof(g.icon_name) ||
                    strchr(cfg->icon_name, '/') || strchr(cfg->icon_name, '"')))) {
    snprintf(err, err_len, "Die Angaben zum Bereitstellen des Pakets sind ungültig.");
    return -1;
  }
  uint64_t at = 0;
  for(unsigned i = 0; i < cfg->nslices; i++) {                         /* in order, no gap, no overlap, no empty piece */
    if(cfg->slices[i].logical != at || cfg->slices[i].size == 0 || cfg->slices[i].size > UINT64_MAX - at) {
      snprintf(err, err_len, "Die Teile des Pakets passen nicht lückenlos aneinander.");
      return -1;
    }
    at += cfg->slices[i].size;
  }
  if(at != cfg->total || at == 0) {
    snprintf(err, err_len, "Die Teile des Pakets ergeben nicht die erwartete Länge.");
    return -1;
  }

  pthread_mutex_lock(&g.lock);
  if(g.leaked && g.conns == 0) {                                          /* the stuck connection has ended since: its slices can go */
    free(g.slices); g.slices = NULL;
    free(g.icon); g.icon = NULL;
    g.nslices = 0;
    g.leaked = 0;
    PS5TM_INFO("pkg_stream_recovered", "Server: die hängende Verbindung der letzten Installation ist zu Ende, es geht wieder.");
  }
  if(g.running || g.leaked) {
    int was_leaked = g.leaked;
    pthread_mutex_unlock(&g.lock);
    if(was_leaked) snprintf(err, err_len, "Eine Verbindung der letzten Installation hängt noch (vielleicht wurde ein Laufwerk abgezogen). Bitte die App neu starten, oder abwarten, bis das Laufwerk antwortet.");
    else snprintf(err, err_len, "Der Server für die Installation läuft schon.");
    return -1;
  }
  g.running = 1;
  pthread_mutex_unlock(&g.lock);

  int port = cfg->port ? cfg->port : PKGI_STREAM_PORT;
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  struct sockaddr_in sa;
  memset(&sa, 0, sizeof(sa));
  sa.sin_family = AF_INET;
  sa.sin_port = htons((uint16_t)port);
  sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  int one = 1;
  if(fd >= 0) setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  if(fd < 0 || bind(fd, (struct sockaddr *)&sa, sizeof(sa)) != 0 || listen(fd, 16) != 0) {
    int e = errno;
    if(fd >= 0) close(fd);
    pthread_mutex_lock(&g.lock);
    g.running = 0;
    pthread_mutex_unlock(&g.lock);
    if(e == EADDRINUSE) snprintf(err, err_len, "Der Anschluss %d ist belegt (läuft noch ein anderes Installationswerkzeug?).", port);
    else snprintf(err, err_len, "Der Server für die Installation ließ sich nicht starten (%s).", strerror(e));
    return -1;
  }

  ps5tm_pkgslice_t *sl = malloc(sizeof(*sl) * cfg->nslices);
  unsigned char *icon = cfg->icon && cfg->icon_n ? malloc(cfg->icon_n) : NULL;
  if(!sl || (cfg->icon && cfg->icon_n && !icon)) {
    free(sl);
    free(icon);
    close(fd);
    pthread_mutex_lock(&g.lock);
    g.running = 0;
    pthread_mutex_unlock(&g.lock);
    snprintf(err, err_len, "Zu wenig Speicher.");
    return -1;
  }
  memcpy(sl, cfg->slices, sizeof(*sl) * cfg->nslices);
  if(icon) memcpy(icon, cfg->icon, cfg->icon_n);

  pthread_mutex_lock(&g.lock);
  g.slices = sl;
  g.nslices = cfg->nslices;
  g.total = cfg->total;
  snprintf(g.name, sizeof(g.name), "%s", cfg->name);
  snprintf(g.icon_name, sizeof(g.icon_name), "%s", icon ? cfg->icon_name : "");
  g.icon = icon;
  g.icon_n = icon ? cfg->icon_n : 0;
  g.conns = g.peak_conns = 0;
  g.served = g.requests = g.read_errors = g.last_io_ms = 0;
  g.ncov = 0;
  g.cov_overflow = 0;
  g.logged_req = g.logged_odd = 0;
  for(unsigned i = 0; i < PS_MAX_CONNS; i++) g.cfd[i] = -1;
  g.listen_fd = fd;
  __atomic_store_n(&g.stop, 0, __ATOMIC_RELEASE);
  pthread_mutex_unlock(&g.lock);

  if(pthread_create(&g.accept_th, NULL, accept_main, NULL) != 0) {
    close(fd);
    pthread_mutex_lock(&g.lock);
    free(g.slices); g.slices = NULL;
    free(g.icon); g.icon = NULL;
    g.nslices = 0;
    g.running = 0;
    pthread_mutex_unlock(&g.lock);
    snprintf(err, err_len, "Der Server für die Installation ließ sich nicht starten.");
    return -1;
  }
  PS5TM_INFO("pkg_stream_up", "Server läuft auf 127.0.0.1:%d: %llu Bytes in %u Teilen%s.", port, (unsigned long long)cfg->total,
             cfg->nslices, icon ? ", mit Bild" : "");
  return 0;
}

void
ps5tm_pkgstream_stop(void) {
  pthread_mutex_lock(&g.lock);
  int was = g.running;
  pthread_mutex_unlock(&g.lock);
  if(!was) return;
  __atomic_store_n(&g.stop, 1, __ATOMIC_RELEASE);
  pthread_join(g.accept_th, NULL);
  close(g.listen_fd);
  /* A connection that waits for its client's next request (or for the client to take what it was sent) would notice the
     stop only when its timeout runs out. Shutting its socket down ends the wait at once. */
  pthread_mutex_lock(&g.lock);
  for(unsigned i = 0; i < PS_MAX_CONNS; i++)
    if(g.cfd[i] >= 0) shutdown(g.cfd[i], SHUT_RDWR);
  pthread_mutex_unlock(&g.lock);
  /* The connections are detached and look at the stop flag between two sends; a send that is stuck ends at its
     timeout. Their buffers refer to the slices, so those stay until the last one is gone. */
  int leaked = 0;
  const int rounds = (int)(PS_STOP_WAIT_MS / 10);
  for(int i = 0; i < rounds; i++) {
    pthread_mutex_lock(&g.lock);
    unsigned c = g.conns;
    pthread_mutex_unlock(&g.lock);
    if(c == 0) break;
    if(i == rounds - 1) leaked = 1;
    usleep(10000);
  }
  pthread_mutex_lock(&g.lock);
  uint64_t covered = covered_locked();
  PS5TM_INFO("pkg_stream_down", "Server beendet: %llu Anfragen, %llu Bytes geliefert, %llu von %llu abgedeckt%s, höchstens %u Verbindungen, %llu Lesefehler.",
             (unsigned long long)g.requests, (unsigned long long)g.served, (unsigned long long)(g.cov_overflow ? g.served : covered),
             (unsigned long long)g.total, g.cov_overflow ? " (Liste übergelaufen)" : "", g.peak_conns, (unsigned long long)g.read_errors);
  if(leaked) {
    PS5TM_WARN("pkg_stream_leaked", "Server: %u Verbindung(en) hängen noch (ein Lesen von einem Laufwerk, das nicht antwortet?). Ein neuer Server startet erst, wenn sie zu Ende sind.", g.conns);
    g.leaked = 1;                                                       /* never free what a thread may still read */
  } else {
    free(g.slices); g.slices = NULL;
    free(g.icon); g.icon = NULL;
    g.nslices = 0;
  }
  g.running = 0;
  pthread_mutex_unlock(&g.lock);
}
