/* ShadowMountPlus's own interface, where it has one.
 *
 * Version 1.7beta2 on the test console (29.09.2026) answers on port 10101:
 * POST /api/v1/<route> with a JSON body, and every answer carries
 * "status": 0 on success. Used here:
 *
 *   /version              capabilities: list_games, move_game_source,
 *                         unpack_game_image, storage_job_status, ...
 *   /games                every title it manages — "path" is the real image
 *                         file or folder, "runtime_path" where an image is
 *                         mounted (/mnt/shadowmnt/...), "source_type" folder
 *                         or image, "image_type" exfatfs, ufs (.ffpkg), pfs
 *                         (.ffpfs) or pfsc (.ffpfsc), "mounted"
 *   /games/move           {title_id, destination_dir}: a storage job
 *   /games/unpack         {title_id, destination_dir, delete_source}: an
 *                         image unpacked into a folder, as a storage job
 *   /games/storage/status the one job it runs at a time
 *   /games/storage/cancel {job_id}
 *
 * The app only asks. Moving and unpacking are done by ShadowMountPlus itself,
 * so that its own bookkeeping — which title lives where, what is mounted —
 * stays right; a move behind its back would leave a registration pointing at
 * nothing for minutes. Without ShadowMountPlus none of this is offered.
 *
 * A plain local HTTP/1.1 client: one connection per call, Connection: close,
 * a body of either Content-Length or chunked encoding, short timeouts. */

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "ps5tm.h"
#include "third_party/cJSON.h"

#define SMP_PORT        10101
#define SMP_MAX_BODY    (2u * 1024 * 1024)
#define SMP_GAMES_TTL   10000
#define SMP_DOWN_TTL    30000          /* not asked again before this */

static pthread_mutex_t   g_lock = PTHREAD_MUTEX_INITIALIZER;
static ps5tm_smp_game_t *g_games;
static int               g_count = -1; /* -1: not reachable at last ask */
static uint64_t          g_games_ms;
static char              g_version[32];
static char              g_caps[512];  /* ",cap1,cap2," */


/* Waits for fd until events or the deadline; 1 ready, 0 timeout, -1 error. */
static int
wait_fd(int fd, short events, uint64_t deadline) {
  for(;;) {
    uint64_t now = ps5tm_mono_ms();
    if(now >= deadline) return 0;
    struct pollfd p = { fd, events, 0 };
    int r = poll(&p, 1, (int)(deadline - now));
    if(r > 0) return (p.revents & (events | POLLHUP)) ? 1 : -1;
    if(r == 0) return 0;
    if(errno != EINTR) return -1;
  }
}

/* The body of a chunked answer, decoded in place. Returns its length, or -1. */
static long
dechunk(char *body, size_t len) {
  size_t in = 0, out = 0;
  for(;;) {
    char *end = NULL;
    unsigned long n = strtoul(body + in, &end, 16);
    if(!end || end == body + in) return -1;
    char *crlf = strstr(end, "\r\n");
    if(!crlf) return -1;
    in = (size_t)(crlf - body) + 2;
    if(n == 0) return (long)out;
    /* Compared as len - in, never in + n: a size like fffffffffffffffe wraps
       the sum and sails past the check into a memmove of a negative length. */
    if(in > len || n > len - in) return -1;
    memmove(body + out, body + in, n);
    out += n;
    in  += n + 2;                           /* the chunk's own CRLF */
    if(in > len) return -1;
  }
}

/* POSTs body to /api/v1<route> and returns the parsed answer, or NULL. */
static cJSON *
post(const char *route, const char *body, int timeout_ms) {
  uint64_t deadline = ps5tm_mono_ms() + (uint64_t)timeout_ms;
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if(fd < 0) return NULL;
  int fl = fcntl(fd, F_GETFL, 0);
  fcntl(fd, F_SETFL, fl | O_NONBLOCK);

  struct sockaddr_in sa;
  memset(&sa, 0, sizeof(sa));
  sa.sin_family      = AF_INET;
  sa.sin_port        = htons(SMP_PORT);
  sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if(connect(fd, (struct sockaddr *)&sa, sizeof(sa)) != 0 && errno != EINPROGRESS) {
    close(fd);
    return NULL;
  }
  int soerr = 0;
  socklen_t sl = sizeof(soerr);
  if(wait_fd(fd, POLLOUT, deadline) != 1 ||
     getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &sl) != 0 || soerr != 0) {
    close(fd);
    return NULL;
  }

  size_t blen = body ? strlen(body) : 0;
  char   head[256];
  int hn = snprintf(head, sizeof(head),
                    "POST /api/v1%s HTTP/1.1\r\nHost: 127.0.0.1:%d\r\n"
                    "Content-Type: application/json\r\nContent-Length: %zu\r\n"
                    "Connection: close\r\n\r\n", route, SMP_PORT, blen);
  if(hn <= 0 || (size_t)hn >= sizeof(head)) { close(fd); return NULL; }

  const char *parts[2] = { head, body ? body : "" };
  size_t      lens[2]  = { (size_t)hn, blen };
  for(int i = 0; i < 2; i++) {
    size_t off = 0;
    while(off < lens[i]) {
      ssize_t w = send(fd, parts[i] + off, lens[i] - off, 0);
      if(w > 0) { off += (size_t)w; continue; }
      if(w < 0 && (errno == EAGAIN || errno == EINTR) &&
         wait_fd(fd, POLLOUT, deadline) == 1)
        continue;
      close(fd);
      return NULL;
    }
  }

  size_t cap = 16384, got = 0;
  char  *buf = malloc(cap + 1);
  if(!buf) { close(fd); return NULL; }
  for(;;) {
    if(got == cap) {
      if(cap >= SMP_MAX_BODY) break;
      char *nb = realloc(buf, cap * 2 + 1);
      if(!nb) break;
      buf = nb;
      cap *= 2;
    }
    ssize_t r = recv(fd, buf + got, cap - got, 0);
    if(r > 0) { got += (size_t)r; continue; }
    if(r == 0) break;
    if((errno == EAGAIN || errno == EINTR) && wait_fd(fd, POLLIN, deadline) == 1)
      continue;
    break;
  }
  close(fd);
  buf[got] = 0;

  cJSON *json = NULL;
  char  *sep  = strstr(buf, "\r\n\r\n");
  if(sep && !strncmp(buf, "HTTP/1.", 7)) {
    *sep = 0;
    char  *payload = sep + 4;
    size_t plen    = got - (size_t)(payload - buf);
    int chunked = 0;
    for(char *h = strstr(buf, "\r\n"); h; h = strstr(h + 2, "\r\n")) {
      if(!strncasecmp(h + 2, "Transfer-Encoding:", 18) && strstr(h + 2, "chunked"))
        chunked = 1;
    }
    long n = chunked ? dechunk(payload, plen) : (long)plen;
    if(n >= 0) json = cJSON_ParseWithLength(payload, (size_t)n);
  }
  free(buf);
  return json;
}

static int
status_ok(const cJSON *j) {
  const cJSON *s = j ? cJSON_GetObjectItem(j, "status") : NULL;
  return cJSON_IsNumber(s) && s->valueint == 0;
}

static void
copy_str(const cJSON *j, const char *key, char *out, size_t out_len) {
  const cJSON *v = cJSON_GetObjectItem(j, key);
  snprintf(out, out_len, "%s", cJSON_IsString(v) ? v->valuestring : "");
}

/* The list and the version, at most every SMP_GAMES_TTL; after a failed ask
   not again for SMP_DOWN_TTL, so a console without ShadowMountPlus pays one
   refused connection now and then. Called with g_lock held. */
static void
refresh_locked(void) {
  uint64_t now = ps5tm_mono_ms();
  uint64_t ttl = g_count < 0 ? SMP_DOWN_TTL : SMP_GAMES_TTL;
  if(g_games_ms && now - g_games_ms < ttl) return;
  g_games_ms = now;

  cJSON *ver = post("/version", "{}", 1500);
  if(!status_ok(ver)) {
    cJSON_Delete(ver);
    g_count      = -1;
    g_version[0] = 0;
    g_caps[0]    = 0;
    return;
  }
  copy_str(ver, "shadowmount_version", g_version, sizeof(g_version));
  snprintf(g_caps, sizeof(g_caps), ",");
  const cJSON *caps = cJSON_GetObjectItem(ver, "capabilities");
  const cJSON *c;
  cJSON_ArrayForEach(c, caps) {
    if(!cJSON_IsString(c)) continue;
    size_t used = strlen(g_caps);
    snprintf(g_caps + used, sizeof(g_caps) - used, "%s,", c->valuestring);
  }
  cJSON_Delete(ver);

  cJSON *list = post("/games", "{\"include_size\":false}", 4000);
  const cJSON *games = status_ok(list) ? cJSON_GetObjectItem(list, "games") : NULL;
  if(!cJSON_IsArray(games)) {
    cJSON_Delete(list);
    g_count = -1;
    return;
  }
  int n = cJSON_GetArraySize(games);
  if(n > PS5TM_SMP_MAX_GAMES) n = PS5TM_SMP_MAX_GAMES;
  if(!g_games) g_games = calloc(PS5TM_SMP_MAX_GAMES, sizeof(ps5tm_smp_game_t));
  if(!g_games) { cJSON_Delete(list); g_count = -1; return; }

  int k = 0;
  const cJSON *g;
  cJSON_ArrayForEach(g, games) {
    if(k >= n) break;
    ps5tm_smp_game_t *e = &g_games[k];
    memset(e, 0, sizeof(*e));
    copy_str(g, "title_id", e->title_id, sizeof(e->title_id));
    copy_str(g, "path", e->path, sizeof(e->path));
    copy_str(g, "runtime_path", e->runtime_path, sizeof(e->runtime_path));
    copy_str(g, "source_type", e->source_type, sizeof(e->source_type));
    copy_str(g, "image_type", e->image_type, sizeof(e->image_type));
    e->mounted   = cJSON_IsTrue(cJSON_GetObjectItem(g, "mounted"));
    e->available = cJSON_IsTrue(cJSON_GetObjectItem(g, "source_available"));
    /* Only absolute paths without a way up: they are shown, copied from and
       handed back to ShadowMountPlus. */
    if(!e->title_id[0] || e->path[0] != '/' || ps5tm_path_has_dotdot(e->path))
      continue;
    k++;
  }
  g_count = k;
  cJSON_Delete(list);
}

int
ps5tm_smp_find(const char *title_id, ps5tm_smp_game_t *out) {
  if(!title_id || !out) return -1;
  pthread_mutex_lock(&g_lock);
  refresh_locked();
  int rc = -1;
  for(int i = 0; i < g_count; i++) {
    if(strcmp(g_games[i].title_id, title_id)) continue;
    *out = g_games[i];
    rc = 0;
    break;
  }
  pthread_mutex_unlock(&g_lock);
  return rc;
}

int
ps5tm_smp_available(char *version, size_t version_len) {
  pthread_mutex_lock(&g_lock);
  refresh_locked();
  int up = g_count >= 0;
  if(version) snprintf(version, version_len, "%s", up ? g_version : "");
  pthread_mutex_unlock(&g_lock);
  return up;
}

int
ps5tm_smp_can(const char *capability) {
  char want[64];
  snprintf(want, sizeof(want), ",%s,", capability);
  pthread_mutex_lock(&g_lock);
  refresh_locked();
  int yes = g_count >= 0 && strstr(g_caps, want) != NULL;
  pthread_mutex_unlock(&g_lock);
  return yes;
}

/* The list is stale the moment a job moves something. */
void
ps5tm_smp_forget(void) {
  pthread_mutex_lock(&g_lock);
  g_games_ms = 0;
  pthread_mutex_unlock(&g_lock);
}

cJSON *
ps5tm_smp_call(const char *route, const cJSON *body, int timeout_ms,
               char *err, size_t err_len) {
  char *txt = body ? cJSON_PrintUnformatted(body) : NULL;
  cJSON *ans = post(route, txt ? txt : "{}", timeout_ms);
  free(txt);
  if(!ans) {
    snprintf(err, err_len, "ShadowMountPlus antwortet nicht.");
    return NULL;
  }
  if(!status_ok(ans)) {
    const cJSON *e = cJSON_GetObjectItem(ans, "error");
    snprintf(err, err_len, "ShadowMountPlus: %s",
             cJSON_IsString(e) && e->valuestring[0] ? e->valuestring
                                                    : "Auftrag abgelehnt.");
    cJSON_Delete(ans);
    return NULL;
  }
  return ans;
}
