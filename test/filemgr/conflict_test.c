/* Host test of the file manager's answer to names that are taken in the target (04.11.2026): refused with the list
 * first, then replace, keep both and skip. It runs the real src/filemgr.c on a folder of its own (FM_ROOT) with
 * real files; see run.sh. */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include "ps5tm.h"

/* ---- what filemgr.c asks of the rest of the app */
int ps5tm_ampr_busy(void) { return 0; }
int ps5tm_disc_busy(void) { return 0; }
int ps5tm_usbeject_busy(void) { return 0; }
int ps5tm_usbeject_blocked(void) { return 0; }
int ps5tm_usbeject_path_ok(const char *mount) { return mount && !strncmp(mount, "/mnt/usb", 8); }
/* the drive that is being ejected (usbeject.c): this stub follows its rule - the mount itself or a path below it */
static const char *g_eject_mount;
int ps5tm_usbeject_covers(const char *path) {
  if(!g_eject_mount || !path) return 0;
  size_t n = strlen(g_eject_mount);
  return !strncmp(path, g_eject_mount, n) && (path[n] == '/' || path[n] == 0);
}
/* the same drive for the 30 s after an eject, too (usbeject.c); here the eject is over when the test clears it, so it is the same rule */
int ps5tm_usbeject_settling(const char *path) { return ps5tm_usbeject_covers(path); }
/* the titles of games (gamestate.c): the stub takes a param file's whole text as the title, and knows one installed game */
int ps5tm_title_from_param(const char *path, char *name, size_t name_len) {
  name[0] = 0;
  FILE *f = fopen(path, "r");
  if(!f) return -1;
  size_t got = fread(name, 1, name_len - 1, f);
  fclose(f);
  name[got] = 0;
  return name[0] ? 0 : -1;
}
int ps5tm_title_installed(const char *title_id, char *name, size_t name_len) {
  snprintf(name, name_len, "%s", strcmp(title_id, "PPSA06240") ? "" : "Iron Tide");
  return name[0] ? 0 : -1;
}
/* the drive gate (drivegate.c): the places count their statvfs through it; here: refused for the drive that is being ejected, counted in g_gate_in otherwise */
static int g_gate_in, g_gate_out;
int ps5tm_drive_enter(const char *path) {
  if(ps5tm_usbeject_covers(path)) return 0;
  g_gate_in++;
  return strncmp(path, "/mnt/usb", 8) ? -1 : 1;
}
void ps5tm_drive_leave(int token) { if(token > 0) g_gate_out++; }
/* filemgr.c calls this while a read is entered (-DFM_TEST_HOOK_READ, see run.sh): what the readers' count says at that moment */
static int g_probe_n, g_probe_reading;
int ps5tm_filemgr_reading(void);
void fm_read_probe(void) { g_probe_n++; g_probe_reading = ps5tm_filemgr_reading(); }
int ps5tm_jobs_busy_except(int self, int flags, char *why, size_t n) { (void)self; (void)flags; (void)why; (void)n; return 0; }
int ps5tm_gameconvert_busy(void) { return 0; }
int ps5tm_gamecopy_busy(void) { return 0; }
int ps5tm_gamedelete_busy(void) { return 0; }
int ps5tm_gamemove_busy(void) { return 0; }
int ps5tm_imgjob_busy(void) { return 0; }
int ps5tm_pkginst_busy(void) { return 0; }
int ps5tm_pkgsplit_busy(void) { return 0; }
int ps5tm_saves_busy(void) { return 0; }
unsigned ps5tm_pkg_drives(ps5tm_pkgdrive_t *out, unsigned max) { (void)out; (void)max; return 0; }
void ps5tm_powerguard_hold(void) {}
void ps5tm_powerguard_release(void) {}
void ps5tm_hist_add(const char *k, const char *t, const char *d, const char *r, const char *e, uint64_t b, uint64_t ms) {
  (void)k; (void)t; (void)d; (void)r; (void)e; (void)b; (void)ms;
}
void ps5tm_log(const char *level, const char *code, const char *fmt, ...) { (void)level; (void)code; (void)fmt; }
uint64_t ps5tm_mono_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}
int ps5tm_thread_create(pthread_t *t, const pthread_attr_t *a, void *(*f)(void *), void *x) { return pthread_create(t, a, f, x); }

static int g_status;
static char g_body[16384];
void ps5tm_http_send_json(int fd, int status, const char *json) { (void)fd; g_status = status; snprintf(g_body, sizeof(g_body), "%s", json); }
/* downloads write their body through http.c's rule (at least 64 KiB in every 30 s); here: plain writes */
int ps5tm_http_write_all(int fd, const void *buf, size_t len) {
  for(const char *p = buf; len;) {
    ssize_t n = write(fd, p, len);
    if(n <= 0) return -1;
    p += n;
    len -= (size_t)n;
  }
  return 0;
}
void ps5tm_http_send_error(int fd, int status, const char *code, const char *message) {
  (void)fd;
  g_status = status;
  snprintf(g_body, sizeof(g_body), "{\"ok\":false,\"code\":\"%s\",\"message\":\"%s\"}", code, message);
}

int ps5tm_filemgr_api(int fd, const ps5tm_request_t *req);

/* ---- helpers */
static int g_fail, g_ok;
#define CHECK(c, ...) do { if(c) g_ok++; else { g_fail++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while(0)

static const char *ROOT = FM_ROOT;

/* filemgr.c calls this in every 1-MB step of a transfer (-DFM_TEST_HOOK, see run.sh): a test that needs a slow copy sets g_slow_ms. */
static int g_slow_ms;
void fm_slow(void) { if(g_slow_ms > 0) usleep((useconds_t)g_slow_ms * 1000); }

static void
put(const char *rel, const char *text) {
  char p[512];
  snprintf(p, sizeof(p), "%s%s", ROOT, rel);
  char dir[512];
  snprintf(dir, sizeof(dir), "%s", p);
  char *s = strrchr(dir, '/');
  *s = 0;
  char cmd[1100];
  snprintf(cmd, sizeof(cmd), "mkdir -p '%s'", dir);
  if(system(cmd) != 0) exit(2);
  FILE *f = fopen(p, "w");
  if(!f) exit(2);
  fputs(text, f);
  fclose(f);
}

static int
get(const char *rel, char *out, size_t n) {
  char p[512];
  snprintf(p, sizeof(p), "%s%s", ROOT, rel);
  FILE *f = fopen(p, "r");
  if(!f) return 0;
  size_t k = fread(out, 1, n - 1, f);
  out[k] = 0;
  fclose(f);
  return 1;
}

static int
has(const char *rel) {
  char p[512];
  struct stat st;
  snprintf(p, sizeof(p), "%s%s", ROOT, rel);
  return lstat(p, &st) == 0;
}

static int
is(const char *rel, const char *text) {
  char buf[256];
  return get(rel, buf, sizeof(buf)) && !strcmp(buf, text);
}

static void
call(const char *method, const char *path, const char *body) {
  ps5tm_request_t req;
  memset(&req, 0, sizeof(req));
  snprintf(req.method, sizeof(req.method), "%s", method);
  snprintf(req.path, sizeof(req.path), "%s", path);
  req.body = (char *)body;
  req.body_len = body ? strlen(body) : 0;
  g_status = 0;
  g_body[0] = 0;
  ps5tm_filemgr_api(0, &req);
}

static void
callq(const char *path, const char *query) {
  ps5tm_request_t req;
  memset(&req, 0, sizeof(req));
  snprintf(req.method, sizeof(req.method), "GET");
  snprintf(req.path, sizeof(req.path), "%s", path);
  snprintf(req.query, sizeof(req.query), "%s", query);
  g_status = 0;
  g_body[0] = 0;
  ps5tm_filemgr_api(0, &req);
}

/* A copy or move; returns the HTTP status, and waits for the job. */
static int
transfer(const char *what, const char *src, const char *dest, const char *conflict) {
  char body[1024];
  if(conflict) snprintf(body, sizeof(body), "{\"paths\":[\"%s\"],\"dest\":\"%s\",\"conflict\":\"%s\"}", src, dest, conflict);
  else snprintf(body, sizeof(body), "{\"paths\":[\"%s\"],\"dest\":\"%s\"}", src, dest);
  call("POST", what, body);
  int st = g_status;
  if(st == 200)
    for(int i = 0; i < 500 && ps5tm_filemgr_busy(); i++) usleep(10000);
  return st;
}

static void
fresh(void) {
  char cmd[600];
  snprintf(cmd, sizeof(cmd), "rm -rf '%s' && mkdir -p '%s/data'", ROOT, ROOT);
  if(system(cmd) != 0) exit(2);
  put("/data/src/f.txt", "NEW");
  put("/data/src/d/x.txt", "NEWX");
  put("/data/src/d/y.txt", "NEWY");
  put("/data/dst/f.txt", "OLD");
  put("/data/dst/d/x.txt", "OLDX");
  put("/data/dst/d/z.txt", "OLDZ");
}

/* A raw upload (src/filemgr.c:receive_upload, called directly by http.c, not through ps5tm_filemgr_api): `prefix_len`
   of `body` is handed over as already read (as it would arrive together with the headers in one recv()), the rest
   is written to a socketpair for receive_upload's own reads to pick up — the same split real uploads have. */
static int
upload_once(const char *query, const char *body, size_t body_len, size_t prefix_len) {
  int sv[2];
  if(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) exit(2);
  size_t rest = body_len - prefix_len;
  if(rest && write(sv[1], body + prefix_len, rest) != (ssize_t)rest) exit(2);
  close(sv[1]);
  g_status = 0;
  g_body[0] = 0;
  ps5tm_filemgr_receive_upload(sv[0], query, body, prefix_len, body_len);
  close(sv[0]);
  return g_status;
}

int
main(void) {
  /* 1. nothing chosen: refused, with the list, nothing touched */
  fresh();
  CHECK(transfer("/api/v1/files/copy", "/data/src/f.txt", "/data/dst", NULL) == 409, "no choice: 409, got %d", g_status);
  CHECK(strstr(g_body, "\"conflicts\":[\"f.txt\"]") != NULL, "list of names: %s", g_body);
  CHECK(strstr(g_body, "\"code\":\"exists\"") != NULL, "code exists: %s", g_body);
  CHECK(is("/data/dst/f.txt", "OLD"), "target unchanged");

  /* 2. replace a file */
  CHECK(transfer("/api/v1/files/copy", "/data/src/f.txt", "/data/dst", "replace") == 200, "replace file: %d %s", g_status, g_body);
  CHECK(is("/data/dst/f.txt", "NEW"), "file replaced");
  CHECK(is("/data/src/f.txt", "NEW"), "copy keeps the original");
  CHECK(!has("/data/dst/f.txt.fm-tmp"), "no temporary file left");

  /* 2b. replace, but the copy fails (the source cannot be read): the old file stays. The clean-up after a failed item took the
     name in the target for its own and deleted it, though the copy goes to a temporary file and the old one is still there (review
     24.11.2026). Not as root: root reads a file without read permission. */
  if(geteuid() != 0) {
    fresh();
    char p[700];
    snprintf(p, sizeof(p), "%s/data/src/f.txt", ROOT);
    chmod(p, 0);
    int st = transfer("/api/v1/files/copy", "/data/src/f.txt", "/data/dst", "replace");
    call("GET", "/api/v1/files/job", NULL);
    CHECK(is("/data/dst/f.txt", "OLD"), "failed replace keeps the old file (HTTP %d, job %s)", st, g_body);
    CHECK(!has("/data/dst/f.txt.fm-tmp"), "failed replace: no temporary file left");
    chmod(p, 0644);
  }

  /* 3. keep both: f (2).txt, then f (3).txt */
  fresh();
  CHECK(transfer("/api/v1/files/copy", "/data/src/f.txt", "/data/dst", "keep") == 200, "keep: %d %s", g_status, g_body);
  CHECK(is("/data/dst/f.txt", "OLD") && is("/data/dst/f (2).txt", "NEW"), "both kept");
  CHECK(transfer("/api/v1/files/copy", "/data/src/f.txt", "/data/dst", "keep") == 200, "keep again");
  CHECK(is("/data/dst/f (3).txt", "NEW"), "third name");

  /* 4. a folder over a folder is merged, its files replaced, the rest stays */
  fresh();
  CHECK(transfer("/api/v1/files/copy", "/data/src/d", "/data/dst", "replace") == 200, "merge: %d %s", g_status, g_body);
  CHECK(is("/data/dst/d/x.txt", "NEWX") && is("/data/dst/d/y.txt", "NEWY") && is("/data/dst/d/z.txt", "OLDZ"), "merged");

  /* 5. skip leaves the target alone, and the job says so */
  fresh();
  CHECK(transfer("/api/v1/files/copy", "/data/src/f.txt", "/data/dst", "skip") == 200, "skip: %d %s", g_status, g_body);
  CHECK(is("/data/dst/f.txt", "OLD"), "skip: unchanged");
  call("GET", "/api/v1/files/job", NULL);
  CHECK(strstr(g_body, "unverändert") != NULL && strstr(g_body, "\"state\":\"done\"") != NULL, "skip note: %s", g_body);

  /* 6. move with replace: the file is put over the old one, the original is gone */
  fresh();
  CHECK(transfer("/api/v1/files/move", "/data/src/f.txt", "/data/dst", "replace") == 200, "move replace: %d %s", g_status, g_body);
  CHECK(is("/data/dst/f.txt", "NEW") && !has("/data/src/f.txt"), "moved over");

  /* 7. move of a folder onto a folder: merged, then the source is cleaned */
  fresh();
  CHECK(transfer("/api/v1/files/move", "/data/src/d", "/data/dst", "replace") == 200, "move merge: %d %s", g_status, g_body);
  CHECK(is("/data/dst/d/x.txt", "NEWX") && is("/data/dst/d/z.txt", "OLDZ") && !has("/data/src/d"), "folder moved and merged");

  /* 8. a file over a folder (or the reverse) is refused for replace, nothing changes */
  fresh();
  put("/data/src/n", "FILE");
  put("/data/dst/n/inner.txt", "IN");
  CHECK(transfer("/api/v1/files/copy", "/data/src/n", "/data/dst", "replace") == 409, "type clash: %d", g_status);
  CHECK(is("/data/dst/n/inner.txt", "IN"), "folder untouched");
  CHECK(transfer("/api/v1/files/copy", "/data/src/n", "/data/dst", "keep") == 200, "keep both over type clash");
  CHECK(is("/data/dst/n (2)", "FILE"), "kept under a new name");

  /* 9. into its own folder: replace leaves the file alone, keep makes a copy */
  fresh();
  CHECK(transfer("/api/v1/files/copy", "/data/src/f.txt", "/data/src", "replace") == 200, "same folder replace: %d %s", g_status, g_body);
  CHECK(is("/data/src/f.txt", "NEW"), "file not damaged by copying onto itself");
  CHECK(transfer("/api/v1/files/copy", "/data/src/f.txt", "/data/src", "keep") == 200, "same folder keep");
  CHECK(is("/data/src/f (2).txt", "NEW"), "a copy beside it");

  /* 10. a folder under keep: "d (2)"; a name without extension; a dot file */
  fresh();
  CHECK(transfer("/api/v1/files/copy", "/data/src/d", "/data/dst", "keep") == 200, "keep folder");
  CHECK(is("/data/dst/d (2)/y.txt", "NEWY") && is("/data/dst/d/x.txt", "OLDX"), "folder kept beside the old");
  put("/data/src/.hid", "H");
  put("/data/dst/.hid", "OLDH");
  CHECK(transfer("/api/v1/files/copy", "/data/src/.hid", "/data/dst", "keep") == 200, "keep dot file");
  CHECK(is("/data/dst/.hid (2)", "H"), "dot file: the whole name is the stem");

  /* 11. an unknown choice is refused */
  CHECK(transfer("/api/v1/files/copy", "/data/src/f.txt", "/data/dst", "overwrite") == 400, "unknown choice: %d", g_status);

  /* 12. nothing taken: the choice makes no difference */
  fresh();
  CHECK(transfer("/api/v1/files/copy", "/data/src/f.txt", "/data/src/d", "replace") == 200, "no clash with a choice");
  CHECK(is("/data/src/d/f.txt", "NEW"), "plain copy");

  /* 13. upload: free name, no conflict choice needed; body split between "already read" and "read from the fd" */
  fresh();
  CHECK(upload_once("path=%2Fdata%2Fsrc&name=u.txt&conflict=", "HELLOBODY", 9, 3) == 200, "plain upload: %d %s", g_status, g_body);
  CHECK(is("/data/src/u.txt", "HELLOBODY"), "plain upload content");

  /* 14. upload to a taken name, no choice: refused, old content kept, nothing left behind */
  CHECK(upload_once("path=%2Fdata%2Fsrc&name=f.txt&conflict=", "REPLACED", 8, 0) == 409, "upload no choice: %d", g_status);
  CHECK(strstr(g_body, "\"code\":\"exists\"") != NULL, "upload no choice code: %s", g_body);
  CHECK(is("/data/src/f.txt", "NEW"), "upload no choice: target unchanged");

  /* 15. upload with conflict=replace: overwrites */
  CHECK(upload_once("path=%2Fdata%2Fsrc&name=f.txt&conflict=replace", "REPLACED", 8, 0) == 200, "upload replace: %d %s", g_status, g_body);
  CHECK(is("/data/src/f.txt", "REPLACED"), "upload replace: content swapped");

  /* 16. upload with conflict=keep: saved beside it under a free name, extension kept */
  CHECK(upload_once("path=%2Fdata%2Fsrc&name=f.txt&conflict=keep", "AGAIN", 5, 5) == 200, "upload keep: %d %s", g_status, g_body);
  CHECK(strstr(g_body, "\"path\":\"/data/src/f (2).txt\"") != NULL, "upload keep reports the new name: %s", g_body);
  CHECK(is("/data/src/f.txt", "REPLACED") && is("/data/src/f (2).txt", "AGAIN"), "upload keep: both present");

  /* 17. upload with conflict=skip: nothing written, nothing left behind, the original stays */
  CHECK(upload_once("path=%2Fdata%2Fsrc&name=f.txt&conflict=skip", "IGNORED", 7, 0) == 200, "upload skip: %d %s", g_status, g_body);
  CHECK(strstr(g_body, "\"skipped\":true") != NULL, "upload skip reports skipped: %s", g_body);
  CHECK(is("/data/src/f.txt", "REPLACED"), "upload skip: target unchanged");
  CHECK(!has("/data/src/f.txt.ps5cc-hochladen"), "upload skip: no leftover temp file");

  /* 18. speed and time left: a copy of 80 MB at about 25 MB/s reports a speed in the right range, and a time left that
        does not grow; nothing of it once the job is over */
  fresh();
  if(system("dd if=/dev/zero of='" FM_ROOT "/data/src/big.bin' bs=1M count=80 2>/dev/null") != 0) exit(2);
  g_slow_ms = 40;
  call("POST", "/api/v1/files/copy", "{\"paths\":[\"/data/src/big.bin\"],\"dest\":\"/data/dst\"}");
  CHECK(g_status == 200, "slow copy started: %d %s", g_status, g_body);
  long long rate = 0, eta = 0, eta_first = 0;
  int seen = 0, from_bad = 0;
  for(int i = 0; i < 80 && ps5tm_filemgr_busy(); i++) {
    usleep(100000);
    call("GET", "/api/v1/files/job", NULL);
    char *r = strstr(g_body, "\"rate_bps\":"), *e = strstr(g_body, "\"eta_s\":");
    if(r && e && atoll(r + 11) > 0) {
      seen++;
      if(!strstr(g_body, "\"from\":\"/data/src\"") || !strstr(g_body, "\"items\":1,")) from_bad = 1;
      rate = atoll(r + 11);
      eta = atoll(e + 8);
      if(!eta_first) eta_first = eta;
    }
  }
  g_slow_ms = 0;
  CHECK(seen > 0 && !from_bad, "the job names its source folder and its entries (%d answers)", seen);
  CHECK(rate > 8 * 1048576LL && rate < 80 * 1048576LL, "speed in a sane range: %lld B/s", rate);
  CHECK(eta_first > 0 && eta_first <= 4, "time left at the first sample: %lld s", eta_first);
  CHECK(eta <= eta_first, "time left does not grow: %lld after %lld", eta, eta_first);
  call("GET", "/api/v1/files/job", NULL);
  CHECK(strstr(g_body, "\"rate_bps\":0,") != NULL && strstr(g_body, "\"eta_s\":0,") != NULL, "no speed once done: %s", g_body);

  /* 19. reads of a drive (21.11.2026): a folder list, view, size and download are counted while they run, and refused while the drive they read is
        being ejected. The eject button of the file manager used to start with the page standing in a folder of the drive and a size count or a
        download still reading from it; ShadowMountPlus then cut the drive off under it. */
  fresh();
  g_eject_mount = NULL;
  g_probe_n = 0;
  CHECK(!ps5tm_filemgr_reading(), "nothing is reading at the start");
  callq("/api/v1/files/list", "path=%2Fdata%2Fsrc");
  CHECK(g_status == 200 && strstr(g_body, "f.txt") != NULL, "list without an eject: %d %s", g_status, g_body);
  CHECK(g_probe_n == 1 && g_probe_reading == 1, "the list counts as a read while it runs (%d, %d)", g_probe_n, g_probe_reading);
  CHECK(!ps5tm_filemgr_reading(), "the count is back to 0 after the list");
  callq("/api/v1/files/size", "path=%2Fdata%2Fsrc");
  CHECK(g_status == 200 && g_probe_n == 2 && g_probe_reading == 1 && !ps5tm_filemgr_reading(), "a size counts and ends: %d %s", g_status, g_body);
  callq("/api/v1/files/list", "path=%2Fdata%2Fnope");               /* a folder that is not there: the handler's own refusal, the count still ends */
  CHECK(g_status >= 400 && g_status != 409 && !ps5tm_filemgr_reading(), "a refused list does not leave the count up: %d", g_status);
  callq("/api/v1/files/list", "path=nonsense");                     /* a bad path goes on to the handler */
  CHECK(g_status == 400 && !ps5tm_filemgr_reading(), "a bad path: 400 and no count left: %d", g_status);

  /* the drive is being ejected: its list, view, size and download are refused (409) and leave nothing counted */
  g_eject_mount = "/data/src";
  g_probe_n = 0;
  static const char *const reads[] = { "/api/v1/files/list", "/api/v1/files/size", "/api/v1/files/view", "/api/v1/files/download" };
  for(size_t i = 0; i < sizeof(reads) / sizeof(reads[0]); i++) {
    callq(reads[i], "path=%2Fdata%2Fsrc");
    CHECK(g_status == 409 && strstr(g_body, "\"code\":\"drive_ejecting\"") != NULL, "%s on the drive: 409 drive_ejecting, got %d %s", reads[i], g_status, g_body);
    callq(reads[i], "path=%2Fdata%2Fsrc%2Fd");
    CHECK(g_status == 409, "%s below the drive: 409, got %d", reads[i], g_status);
    CHECK(!ps5tm_filemgr_reading(), "%s refused: no count left", reads[i]);
  }
  CHECK(g_probe_n == 0, "a refused read never reaches its handler (%d)", g_probe_n);
  /* another folder is still readable, also one whose name only starts like the drive's */
  callq("/api/v1/files/list", "path=%2Fdata%2Fdst");
  CHECK(g_status == 200, "another folder is read as before: %d %s", g_status, g_body);
  put("/data/srcx/a.txt", "A");
  callq("/api/v1/files/list", "path=%2Fdata%2Fsrcx");
  CHECK(g_status == 200, "a name that only starts like the drive is no part of it: %d %s", g_status, g_body);
  /* the places do not fail while an eject runs (there is no drive list in this test, so only the call is made) */
  call("GET", "/api/v1/files/places", NULL);
  CHECK(g_status == 200, "places while an eject runs: %d", g_status);
  /* the eject is over: the drive reads again */
  g_eject_mount = NULL;
  callq("/api/v1/files/list", "path=%2Fdata%2Fsrc");
  CHECK(g_status == 200, "after the eject the list works again: %d", g_status);
  CHECK(!ps5tm_filemgr_reading(), "no count is left at the end");

  /* the search (25.11.2026): a part of the name, upper and lower case the same, * and ?, below the folder, links not followed */
  {
    char cmd[900];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s/data/s' && mkdir -p '%s/data/s/a/b/c' '%s/data/s/Spiele/PPSA04933' && ln -s '%s/data/s/a' '%s/data/s/link'",
             ROOT, ROOT, ROOT, ROOT, ROOT);
    if(system(cmd) != 0) exit(2);
    put("/data/s/notiz.txt", "1");
    put("/data/s/a/NOTIZEN.TXT", "2");
    put("/data/s/a/b/c/tief-notiz.md", "3");
    put("/data/s/Spiele/PPSA04933/param.json", "4");
    put("/data/s/Spiele/spiel.pkg", "5");
    put("/data/s/Spiele/spiel.pkg.bak", "6");
    put("/data/s/Äpfel-Übersicht.txt", "7");
    put("/data/s/Spiele/PPSA04933/sce_sys/param.json", "Lantern Pass");   /* a dump: its own param.json names the game */
    put("/data/s/Spiele/PPSA06240.exfat", "8");                           /* an image: only the installed game names it */
    put("/data/s/Spiele/PPSA012345.txt", "9");                            /* ten digits: no title ID */
    struct { const char *q; int hits; const char *must; } cases[] = {
      { "notiz", 3, "/data/s/a/b/c/tief-notiz.md" },          /* part of the name, all levels, case ignored; the link to a is not followed */
      { "*.pkg", 1, "/data/s/Spiele/spiel.pkg" },              /* a wildcard matches the whole name: not spiel.pkg.bak */
      { "PPSA?????", 1, "/data/s/Spiele/PPSA04933" },          /* a folder is a hit too */
      { "äpfel", 1, "/data/s/Äpfel-Übersicht.txt" },           /* umlauts without case */
      { "lantern", 1, "/data/s/Spiele/PPSA04933" },            /* found by its game (25.11.2026) */
      { "IRON", 1, "/data/s/Spiele/PPSA06240.exfat" },         /* an image by the installed game's title */
      { "gibtesnicht", 0, NULL },
    };
    for(size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
      char body[300];
      snprintf(body, sizeof(body), "{\"path\":\"/data/s\",\"q\":\"%s\"}", cases[i].q);
      call("POST", "/api/v1/files/search", body);
      CHECK(g_status == 200, "search %s starts: %d %s", cases[i].q, g_status, g_body);
      for(int k = 0; k < 300; k++) {
        call("GET", "/api/v1/files/search", NULL);
        if(!strstr(g_body, "\"state\":\"running\"")) break;
        usleep(10000);
      }
      CHECK(strstr(g_body, "\"state\":\"done\"") != NULL, "search %s ends done: %s", cases[i].q, g_body);
      int n = 0;
      for(const char *s = g_body; (s = strstr(s, "\"path\":\"/data/s/")) != NULL; s++) n++;
      CHECK(n == cases[i].hits, "search %s: %d hits, want %d: %s", cases[i].q, n, cases[i].hits, g_body);
      if(cases[i].must) {
        char want[300];
        snprintf(want, sizeof(want), "\"path\":\"%s\"", cases[i].must);
        CHECK(strstr(g_body, want) != NULL, "search %s finds %s: %s", cases[i].q, cases[i].must, g_body);
      }
      CHECK(!ps5tm_filemgr_reading(), "search %s: no reader count left", cases[i].q);
    }
    /* the titles in the folder list and in the hits */
    callq("/api/v1/files/list", "path=%2Fdata%2Fs%2FSpiele");
    CHECK(g_status == 200 && strstr(g_body, "\"name\":\"PPSA04933\",\"type\":\"dir\"") != NULL, "list of the games: %s", g_body);
    CHECK(strstr(g_body, "\"title\":\"Lantern Pass\"") != NULL, "a dump folder names its game: %s", g_body);
    CHECK(strstr(g_body, "\"title\":\"Iron Tide\"") != NULL, "an image names the installed game: %s", g_body);
    { const char *x = strstr(g_body, "PPSA012345.txt"); const char *e = x ? strchr(x, '}') : NULL;
      CHECK(x && e && !memmem(x, (size_t)(e - x), "title", 5), "no title ID, no title: %s", g_body); }
    call("POST", "/api/v1/files/search", "{\"path\":\"/data/s\",\"q\":\"PPSA?????\"}");
    for(int k = 0; k < 300; k++) { call("GET", "/api/v1/files/search", NULL); if(!strstr(g_body, "\"state\":\"running\"")) break; usleep(10000); }
    CHECK(strstr(g_body, "\"title\":\"Lantern Pass\"") != NULL, "a hit names its game: %s", g_body);
    call("POST", "/api/v1/files/search", "{\"path\":\"/\",\"q\":\"a\"}");
    CHECK(g_status == 400, "the whole system is not searched: %d", g_status);
    call("POST", "/api/v1/files/search", "{\"path\":\"/data/s\",\"q\":\"**\"}");
    CHECK(g_status == 400, "a term of stars only is refused: %d", g_status);
    call("POST", "/api/v1/files/search", "{\"path\":\"/data/s\",\"q\":\"\"}");
    CHECK(g_status == 400, "an empty term is refused: %d", g_status);
    call("POST", "/api/v1/files/search/cancel", NULL);
    CHECK(g_status == 409, "cancel with nothing running: 409, got %d", g_status);
    /* the drive is being ejected: no search starts there */
    g_eject_mount = "/data/s";
    call("POST", "/api/v1/files/search", "{\"path\":\"/data/s\",\"q\":\"notiz\"}");
    CHECK(g_status == 409, "no search on a drive being ejected: %d", g_status);
    g_eject_mount = NULL;
  }

  printf("%d checks passed, %d failed\n", g_ok, g_fail);
  return g_fail ? 1 : 0;
}
