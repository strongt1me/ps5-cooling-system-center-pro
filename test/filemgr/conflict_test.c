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

  printf("%d checks passed, %d failed\n", g_ok, g_fail);
  return g_fail ? 1 : 0;
}
