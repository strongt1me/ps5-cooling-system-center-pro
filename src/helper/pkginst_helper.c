/* The install helper (see pkginst_ipc.h): a program of its own, started through the payload loader. It connects back
 * to the app on the loopback address, initialises the system's install library (AppInstUtil) in its own process,
 * hands it the address the package can be read from, answers the app's questions about how far the installation is,
 * and leaves when the app is done with it or gone.
 *
 * The installation itself is not in this process. The call into the library only enters the request with the system's
 * installer, which fetches the package from the address and unpacks it on its own account; the first console test showed
 * that (the process that made the call died right after it, and the system went on reading for seconds). So the helper
 * is two things for the app: the one that makes the call, and an interpreter for "how far is it" (the system is asked
 * by content id, so any helper can do that, not only the one that made the call). The app starts a new one when this
 * one is gone.
 *
 * What the helper therefore never does: end the library's session. sceAppInstUtilTerminate was called here right after
 * the call returned in the first version, with the installation only just started, and the process crashed a moment
 * later (a jump to address 0, in a thread that was not ours, reported by the kernel). Nothing here needs it: when the
 * process ends, the system drops what it kept for it. Neither does the helper wait for its own threads or return
 * through main: it leaves with _exit, from wherever it is.
 *
 * A call that fails uses the process up all the same: what the library keeps in a process after a failure (a slot it
 * has not given back, a half-open session) is not something to build on. When the app goes away, or its end of the
 * socket does, the helper goes too, also while a call into the system is stuck.
 *
 * Nothing here knows the app's servers or its data; the one file it writes is a log, line by line, so that a helper
 * that died before it could say anything has left a reason (the loader's own output is gone with its connection).
 *
 * Built for the console as src/helper/pkginst_helper.c alone (make: gen/pkginst_helper.elf), which the app embeds. The
 * tests build it for the host with PKGI_HELPER_TEST and a stand-in for the system library.
 *
 * The structures below are the system library's: AppInstUtil is not documented, and these are the layouts the
 * console's own tools pass to it (as the ps5-pkg-manager of itsPLK, GPL-3.0, whose use of the library was read for
 * this; nothing of its code is here). */

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "pkginst_ipc.h"

#ifndef PKGI_CONNECT_TRIES
#define PKGI_CONNECT_TRIES 200                /* a tenth of a second apart; a helper whose app has gone gives up after 20 s */
#endif
#ifndef PKGI_BUILD
#define PKGI_BUILD 0                          /* the build date, as the app's own: set by the makefile */
#endif

/* ------------------------------------------------------------ the system library's layouts */

typedef struct {
  const char *uri, *ex_uri, *playgo_scenario_id, *content_id, *content_name, *icon_url;
} pkg_metadata_t;

typedef struct {
  char content_id[48];
  int  type;
  int  platform;
} pkg_info_t;

typedef struct {
  char          languages[30][8];
  char          playgo_scenario_ids[64][3];
  char          content_ids[64][48];
  unsigned char unknown[6480];
} playgo_info_t;

typedef struct {
  int32_t error_code;
  int32_t version;
  char    description[512];
  char    type[9];
} install_error_t;

typedef struct {
  char            status[16];
  char            src_type[8];
  uint32_t        remain_time;
  uint64_t        downloaded_size;
  uint64_t        initial_chunk_size;
  uint64_t        total_size;
  uint32_t        promote_progress;
  install_error_t error_info;
  int32_t         local_copy_percent;
  bool            is_copy_only;
} install_status_t;

/* What the library writes into the status is more than the layout above (known from the PKG Manager): on the console
   (05.10.2026) the call overwrote the helper's own variables next to it on the stack, the number of its connection
   among them, which then pointed at the loader's socket, and every helper lost the app at its first or second question.
   The PKG Manager never noticed, because its status sits inside a larger answer. The call gets a buffer of its own,
   16 KiB and away from everything else; what lies behind the known layout is filled with a mark first, so the log can
   say how far the library really writes. */
#define STATUS_ROOM 16384
static union { install_status_t st; unsigned char raw[STATUS_ROOM]; } g_status __attribute__((aligned(64)));

extern int sceAppInstUtilInitialize(void);
extern int sceAppInstUtilInstallByPackage(const pkg_metadata_t *, pkg_info_t *, playgo_info_t *);
extern int sceAppInstUtilGetInstallStatus(const char *content_id, install_status_t *);
extern int sceAppInstUtilAppUnInstall(const char *title_id);
extern int sceAppInstUtilAppUnInstallPat(const char *title_id);
extern int sceAppInstUtilAppUnInstallAddcont(const char *title_id);

/* Four capital letters and five digits, nothing else: what reaches the system's uninstall is a title id. */
static int
title_id_ok(const char *s) {
  if(!memchr(s, 0, 16) || strlen(s) != 9) return 0;
  for(int i = 0; i < 4; i++) if(s[i] < 'A' || s[i] > 'Z') return 0;
  for(int i = 4; i < 9; i++) if(s[i] < '0' || s[i] > '9') return 0;
  return 1;
}

/* ------------------------------------------------------------ the token */

/* In the image's data, not in read-only data and not a constant the compiler may fold: the app finds the mark in
   the image it is about to send and writes the token over the dots (pkginst_ipc.h). */
static volatile char g_token_slot[] = PKGI_TOKEN_MARK "................";

/* ------------------------------------------------------------ small things */

static int64_t
now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* Every line begins with the time of day and the seconds since the helper began: the app copies these lines into its
   own log after a failure, where they are read long after they were written. */
static void
plog(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void
plog(const char *fmt, ...) {
  static int64_t t0;
  if(!t0) t0 = now_ms();
  char buf[512];
  time_t wall = time(NULL);
  struct tm tm;
  localtime_r(&wall, &tm);
  int64_t since = now_ms() - t0;
  int head = snprintf(buf, sizeof(buf), "[%02d:%02d:%02d +%lld.%03lld] ", tm.tm_hour, tm.tm_min, tm.tm_sec, (long long)(since / 1000), (long long)(since % 1000));
  if(head < 0 || (size_t)head >= sizeof(buf)) return;
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(buf + head, sizeof(buf) - (size_t)head, fmt, ap);
  va_end(ap);
  if(n <= 0) return;
  size_t len = (size_t)head + (size_t)n < sizeof(buf) ? (size_t)head + (size_t)n : sizeof(buf) - 1;
  int fd = open(PKGI_LOG_PATH, O_WRONLY | O_CREAT | O_APPEND | O_NOFOLLOW, 0666);
  if(fd >= 0) { (void)!write(fd, buf, len); close(fd); }
}

/* Why the last transfer failed, for the log: a helper that leaves has to be able to say what it saw. */
static struct { const char *where; int rc, revents, err; long n; } g_xf;

static int
xfail(const char *where, int rc, int revents, long n) {
  g_xf.where = where;
  g_xf.rc = rc;
  g_xf.revents = revents;
  g_xf.err = errno;
  g_xf.n = n;
  return rc;
}

/* All of buf, or an error. sending: 1 send, 0 receive. Waits at most timeout_ms in all. */
static int
transfer(int fd, void *buf, size_t size, int sending, int timeout_ms) {
  int64_t deadline = now_ms() + timeout_ms;
  unsigned char *p = buf;
  while(size) {
    int64_t left = deadline - now_ms();
    if(left <= 0) return xfail("the time ran out", PKGI_E_TIMEOUT, 0, 0);
    struct pollfd pf = { fd, sending ? POLLOUT : POLLIN, 0 };
    int r = poll(&pf, 1, left > 100 ? 100 : (int)left);
    if(r < 0 && errno == EINTR) continue;
    if(r < 0) return xfail("poll failed", PKGI_E_DISCONNECTED, 0, r);
    if(r == 0) continue;
    if(!(pf.revents & pf.events)) return xfail("hang-up or error with nothing left to read", PKGI_E_DISCONNECTED, pf.revents, 0);
    ssize_t n = sending ? send(fd, p, size, MSG_DONTWAIT | MSG_NOSIGNAL) : recv(fd, p, size, MSG_DONTWAIT);
    if(n < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
    if(n <= 0) return xfail(n == 0 ? "the other end closed the connection" : (sending ? "send failed" : "recv failed"), PKGI_E_DISCONNECTED, pf.revents, (long)n);
    p += n;
    size -= (size_t)n;
  }
  return 0;
}

/* Leaves, at once, from wherever this is: no thread is waited for, nothing is closed in order, no way back through
   main. See the top of the file. */
static void leave_with(int code, const char *fmt, ...) __attribute__((format(printf, 2, 3), noreturn));
static void
leave_with(int code, const char *fmt, ...) {
  char why[200];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(why, sizeof(why), fmt, ap);
  va_end(ap);
  plog("serve: leaving (%s)\n", why);
  _exit(code);
}

/* The descriptors this process has open, and the state of the one to the app, for the log. A library that closes or
   replaces a descriptor it does not own would show here. */
static void
fd_report(const char *when, int ipc) {
  char list[240];
  size_t l = 0;
  for(int f = 0; f < 64 && l + 6 < sizeof(list); f++)
    if(fcntl(f, F_GETFD) >= 0) l += (size_t)snprintf(list + l, sizeof(list) - l, "%d ", f);
  errno = 0;
  int fl = fcntl(ipc, F_GETFL);
  int e1 = errno;
  int so = -1;
  socklen_t sl = sizeof(so);
  if(getsockopt(ipc, SOL_SOCKET, SO_ERROR, &so, &sl) != 0) so = -errno;
  struct sockaddr_in pa;
  socklen_t pl = sizeof(pa);
  int port = getpeername(ipc, (struct sockaddr *)&pa, &pl) == 0 ? (int)ntohs(pa.sin_port) : -errno;
  plog("fds %s: open: %s| app connection fd %d: flags %d (errno %d), socket error %d, peer port %d\n", when, list, ipc, fl, e1, so, port);
}

/* The connection to the app on a high descriptor number, 100 and up (05.10.2026): on the console every helper lost it at
   its second status call, and the library closing or replacing a small number it takes for its own (here: 7, the one the
   connection had) is the likeliest reason. The library's own descriptors stay small; this one is out of their way. */
static int
fd_high(int fd, int min) {
  int h = fcntl(fd, F_DUPFD, min);
  if(h < 0) return fd;
  close(fd);
  return h;
}

/* Without a parent the helper has no business. Peek only: the protocol loop is the one that reads. */
typedef struct { int fd; } watch_t;

static void *
watch_parent(void *arg) {
  watch_t *w = arg;
#ifdef POLLRDHUP
  const short gone = POLLHUP | POLLNVAL | POLLRDHUP;                 /* the peer's end is closed, even with a question left unread */
  const short want = POLLIN | POLLRDHUP;
#else
  const short gone = POLLHUP | POLLNVAL;
  const short want = POLLIN;
#endif
  for(;;) {
    struct pollfd pf = { w->fd, want, 0 };
    int r = poll(&pf, 1, 100);
    if(r > 0) {
      char c;
      ssize_t n = recv(w->fd, &c, 1, MSG_PEEK | MSG_DONTWAIT);
      if(n == 0 || (pf.revents & gone)) { plog("watch: the app's end of the connection is gone, leaving\n"); _exit(0); }
      if(n < 0 && errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) { plog("watch: the connection failed (errno %d), leaving\n", errno); _exit(0); }
      usleep(10000);
    }
  }
  return NULL;
}

static void
fill_token(char out[PKGI_TOKEN_LEN + 1]) {
#ifdef PKGI_HELPER_TEST
  /* The test starts this very program, not the patched image the app made: the token comes from the environment. */
  const char *t = getenv("PKGI_TEST_TOKEN");
  if(t && strlen(t) == PKGI_TOKEN_LEN) { memcpy(out, t, PKGI_TOKEN_LEN); out[PKGI_TOKEN_LEN] = 0; return; }
#endif
  const size_t mark = sizeof(PKGI_TOKEN_MARK) - 1;
  for(size_t i = 0; i < PKGI_TOKEN_LEN; i++) out[i] = g_token_slot[mark + i];
  out[PKGI_TOKEN_LEN] = 0;
}

/* ------------------------------------------------------------ the protocol */

static void serve(int fd) __attribute__((noreturn));
static void
serve(int fd) {
  signal(SIGPIPE, SIG_IGN);
  static watch_t w;
  w.fd = fd;
  pthread_t th;
  if(pthread_create(&th, NULL, watch_parent, &w) != 0) leave_with(1, "no watch thread (errno %d)", errno);

  int64_t t0 = now_ms();
  fd_report("at the start", fd);
  errno = 0;
  plog("serve: sceAppInstUtilInitialize\n");
  int init = sceAppInstUtilInitialize();
  int init_errno = errno;
  plog("serve: sceAppInstUtilInitialize -> 0x%08X (errno %d)\n", (unsigned)init, init_errno);
  fd_report("after the library's start", fd);

  /* What the system may still refer to after a call returns (the strings in the metadata, the answers it was given a
     place for) stays as it is, for as long as the process lives. */
  static pkgi_response_t rsp;
  static pkgi_request_t held;
  static pkg_metadata_t meta;
  static pkg_info_t info;
  static playgo_info_t playgo;

  memset(&rsp, 0, sizeof(rsp));
  rsp.magic = PKGI_MAGIC; rsp.version = PKGI_VERSION; rsp.op = PKGI_OP_READY;
  rsp.result = init; rsp.pid = (int32_t)getpid(); rsp.native_errno = init_errno;
  rsp.native_ms = (uint32_t)(now_ms() - t0);
  fill_token(rsp.token);
  snprintf(rsp.build, sizeof(rsp.build), "%llu", (unsigned long long)PKGI_BUILD);
  if(transfer(fd, &rsp, sizeof(rsp), 1, 10000) != 0) leave_with(1, "READY not sent: %s", g_xf.where);
  if(init != 0) leave_with(1, "the library did not start (0x%08X)", (unsigned)init);

  int used = 0, installed = 0;
  unsigned statuses = 0;
  for(;;) {
    static pkgi_request_t req;
    if(transfer(fd, &req, sizeof(req), 0, 120000) != 0) {
      fd_report("when leaving", fd);
      leave_with(0, "no question from the app: %s (rc %d, events 0x%X, errno %d, n %ld)", g_xf.where, g_xf.rc, g_xf.revents, g_xf.err, g_xf.n);
    }
    if(req.magic != PKGI_MAGIC || req.version != PKGI_VERSION ||
       !memchr(req.uri, 0, sizeof(req.uri)) || !memchr(req.name, 0, sizeof(req.name)) ||
       !memchr(req.icon_url, 0, sizeof(req.icon_url)) || !memchr(req.content_id, 0, sizeof(req.content_id)))
      leave_with(1, "a question that is not the app's");
    if(req.op != PKGI_OP_STATUS || statuses < 3 || statuses % 120 == 0) plog("serve: question %u, operation %u\n", (unsigned)req.seq, (unsigned)req.op);
    if(req.op == PKGI_OP_STATUS) statuses++;

    memset(&rsp, 0, sizeof(rsp));
    rsp.magic = PKGI_MAGIC; rsp.version = PKGI_VERSION; rsp.op = req.op; rsp.seq = req.seq;
    rsp.pid = (int32_t)getpid();
    rsp.result = PKGI_E_UNAVAILABLE;
    int64_t c0 = now_ms();
    errno = 0;
    if(req.op == PKGI_OP_INSTALL && !used) {
      used = 1;                                        /* a failed call uses the process up as well */
      held = req;
      meta = (pkg_metadata_t){ held.uri, "", "", "", held.name, held.icon_url };
      plog("serve: sceAppInstUtilInstallByPackage uri=%.200s name=%.80s\n", held.uri, held.name);
      fd_report("before the call", fd);
      rsp.result = sceAppInstUtilInstallByPackage(&meta, &info, &playgo);
      info.content_id[sizeof(info.content_id) - 1] = 0;
      snprintf(rsp.content_id, sizeof(rsp.content_id), "%s", info.content_id);
      rsp.type = info.type; rsp.platform = info.platform;
      installed = rsp.result == 0;
      plog("serve: sceAppInstUtilInstallByPackage -> 0x%08X content_id=%s (errno %d)\n", (unsigned)rsp.result, info.content_id, errno);
      fd_report("after the call", fd);
    } else if(req.op == PKGI_OP_STATUS && (installed || req.content_id[0])) {
      /* By the content id the app names, so that a helper that did not make the call can ask as well. */
      const char *cid = req.content_id[0] ? req.content_id : info.content_id;
      memset(g_status.raw, 0, sizeof(install_status_t));
      memset(g_status.raw + sizeof(install_status_t), 0xA5, STATUS_ROOM - sizeof(install_status_t));
      char when[32];
      if(statuses <= 3) { snprintf(when, sizeof(when), "before status %u", statuses); fd_report(when, fd); }
      errno = 0;
      rsp.result = sceAppInstUtilGetInstallStatus(cid, &g_status.st);
      if(statuses <= 3) {
        int e = errno;
        size_t end = STATUS_ROOM;
        while(end > sizeof(install_status_t) && g_status.raw[end - 1] == 0xA5) end--;
        plog("serve: status call %u wrote up to byte %zu (the known layout has %zu)\n", statuses, end, sizeof(install_status_t));
        snprintf(when, sizeof(when), "after status %u", statuses);
        fd_report(when, fd);
        errno = e;
      }
      install_status_t st = g_status.st;
      st.status[sizeof(st.status) - 1] = 0;
      st.src_type[sizeof(st.src_type) - 1] = 0;
      st.error_info.description[sizeof(st.error_info.description) - 1] = 0;
      st.error_info.type[sizeof(st.error_info.type) - 1] = 0;
      memcpy(rsp.status, st.status, sizeof(rsp.status));
      memcpy(rsp.src_type, st.src_type, sizeof(rsp.src_type));
      rsp.remain_time = st.remain_time; rsp.promote_progress = st.promote_progress;
      rsp.downloaded = st.downloaded_size; rsp.total = st.total_size; rsp.initial_chunk = st.initial_chunk_size;
      rsp.error_code = st.error_info.error_code; rsp.error_version = st.error_info.version;
      memcpy(rsp.error_type, st.error_info.type, sizeof(rsp.error_type));
      memcpy(rsp.error_desc, st.error_info.description, sizeof(rsp.error_desc));
      rsp.local_copy_percent = st.local_copy_percent; rsp.copy_only = st.is_copy_only ? 1 : 0;
    } else if(req.op == PKGI_OP_UNINSTALL && !used) {
      /* The game first, then what belongs to it (as the PKG Manager does for leftovers): the system removes the
         title's updates and add-ons only when asked. Saved games are not part of any of these. */
      used = 1;
      if(!title_id_ok(req.title_id)) {
        rsp.result = PKGI_E_BADREPLY;
        plog("serve: uninstall refused: not a title id\n");
      } else {
        plog("serve: sceAppInstUtilAppUnInstall %s\n", req.title_id);
        rsp.result = sceAppInstUtilAppUnInstall(req.title_id);
        plog("serve: sceAppInstUtilAppUnInstall -> 0x%08X (errno %d)\n", (unsigned)rsp.result, errno);
        if(rsp.result == 0) {
          rsp.result_pat = sceAppInstUtilAppUnInstallPat(req.title_id);
          rsp.result_addcont = sceAppInstUtilAppUnInstallAddcont(req.title_id);
          plog("serve: updates -> 0x%08X, add-ons -> 0x%08X\n", (unsigned)rsp.result_pat, (unsigned)rsp.result_addcont);
        }
      }
    } else if(req.op == PKGI_OP_CLOSE) {
      rsp.result = 0;
      (void)transfer(fd, &rsp, sizeof(rsp), 1, 2000);
      leave_with(0, "the app asked for it");
    }
    rsp.native_errno = errno;
    rsp.native_ms = (uint32_t)(now_ms() - c0);
    if(transfer(fd, &rsp, sizeof(rsp), 1, 10000) != 0) {
      fd_report("when leaving", fd);
      leave_with(0, "the answer was not taken: %s (rc %d, events 0x%X, errno %d, n %ld)", g_xf.where, g_xf.rc, g_xf.revents, g_xf.err, g_xf.n);
    }
    if(req.op == PKGI_OP_INSTALL) plog("serve: the answer to the call is sent\n");
  }
}

#ifndef PKGI_HELPER_TEST
extern int sceNetInit(void);
extern int sceNetCtlInit(void);
extern int sceUserServiceInitialize(int *priority);
#endif

int
main(void) {
  signal(SIGPIPE, SIG_IGN);
#ifdef SYS_thr_set_name
  /* The console's own tools that start such a helper give it a name of its own; the loader's default, "payload.elf",
     is the name of every payload on the console. (On this platform the call names the process.) */
  syscall(SYS_thr_set_name, -1, "ps5cc-inst.elf");
#endif
  mkdir("/data/PS5-Cooling-Center", 0777);
  plog("main: pid %d\n", (int)getpid());

#ifndef PKGI_HELPER_TEST
  /* What the library's own start-up expects of a process before it installs from an address. */
  int prio = 256;
  int r1 = sceNetCtlInit();
  int r2 = sceUserServiceInitialize(&prio);
  int r3 = sceNetInit();
  plog("main: sceNetCtlInit 0x%08X, sceUserServiceInitialize 0x%08X, sceNetInit 0x%08X\n", (unsigned)r1, (unsigned)r2, (unsigned)r3);
#endif

  struct sockaddr_in sa;
  memset(&sa, 0, sizeof(sa));
  sa.sin_family = AF_INET;
  sa.sin_port = htons(PKGI_IPC_PORT);
  sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  int sock = -1, last_errno = 0;
  for(int i = 0; i < PKGI_CONNECT_TRIES && sock < 0; i++) {   /* the app is listening already; a moment of grace anyway */
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if(s >= 0) {
      if(connect(s, (struct sockaddr *)&sa, sizeof(sa)) == 0) { sock = s; break; }
      last_errno = errno;
      close(s);
    } else {
      last_errno = errno;
    }
    usleep(100000);
  }
  if(sock < 0) {
    plog("main: no connection to the app on port %d (errno %d)\n", PKGI_IPC_PORT, last_errno);
    _exit(2);
  }
  int kept = fd_high(sock, 100);
  plog("main: connection to the app on fd %d, kept on fd %d\n", sock, kept);
  serve(kept);
}
