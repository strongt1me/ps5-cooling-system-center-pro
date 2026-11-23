/* Host test of NTFS sticks in the file manager (23.11.2026): the real src/filemgr.c (with fm_ntfs.inc), src/ntfsvol.c and the driver of
 * PS5-NTFS-BROWSER (src/third_party/ntfsb) on NTFS images made by mkntfs; the console's side is a folder of its own (FM_ROOT). It asks
 * the file manager what the pages ask: places, listing, a new folder, uploads with taken names, download and view, copy and move in both
 * directions, delete with its plan, the refusals (rename, edit, replace), eject, and a drive that cannot be written. run.sh then reads
 * the image with ntfs-3g's own tools (ntfsls, ntfscat, ntfsresize --check), which must find exactly what this test wrote. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include "ntfsvol.h"
#include "ps5tm.h"

/* ---- what filemgr.c asks of the rest of the app */
int ps5tm_ampr_busy(void) { return 0; }
int ps5tm_disc_busy(void) { return 0; }
int ps5tm_usbeject_busy(void) { return 0; }
int ps5tm_usbeject_blocked(void) { return 0; }
int ps5tm_usbeject_path_ok(const char *mount) { return mount && !strncmp(mount, "/mnt/usb", 8); }
int ps5tm_usbeject_covers(const char *path) { (void)path; return 0; }
int ps5tm_usbeject_settling(const char *path) { (void)path; return 0; }
int ps5tm_drive_enter(const char *path) { (void)path; return -1; }
void ps5tm_drive_leave(int token) { (void)token; }
int ps5tm_jobs_busy_except(int self, int flags, char *why, size_t n) { (void)self; (void)flags; (void)why; (void)n; return 0; }
unsigned ps5tm_pkg_drives(ps5tm_pkgdrive_t *out, unsigned max) { (void)out; (void)max; return 0; }
void ps5tm_powerguard_hold(void) {}
void ps5tm_powerguard_release(void) {}
void ps5tm_hist_add(const char *k, const char *t, const char *d, const char *r, const char *e, uint64_t b, uint64_t ms) {
  (void)k; (void)t; (void)d; (void)r; (void)e; (void)b; (void)ms;
}
static int g_verbose;
void ps5tm_log(const char *level, const char *code, const char *fmt, ...) {
  if(!g_verbose) return;
  va_list ap;
  va_start(ap, fmt);
  printf("  [%s %s] ", level, code);
  vprintf(fmt, ap);
  printf("\n");
  va_end(ap);
}
uint64_t ps5tm_mono_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}
int ps5tm_thread_create(pthread_t *t, const pthread_attr_t *a, void *(*f)(void *), void *x) { return pthread_create(t, a, f, x); }

static int g_status;
static char g_body[65536];
void ps5tm_http_send_json(int fd, int status, const char *json) { (void)fd; g_status = status; snprintf(g_body, sizeof(g_body), "%s", json); }
void ps5tm_http_send_error(int fd, int status, const char *code, const char *message) {
  (void)fd;
  g_status = status;
  snprintf(g_body, sizeof(g_body), "{\"ok\":false,\"code\":\"%s\",\"message\":\"%s\"}", code, message);
}

int ps5tm_filemgr_api(int fd, const ps5tm_request_t *req);

/* ---- helpers */
static int g_fail, g_ok;
#define CHECK(c, ...) do { if(c) g_ok++; else { g_fail++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n   answer %d: %.400s\n", g_status, g_body); } } while(0)

static const char *ROOT = FM_ROOT;

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

/* A GET whose answer goes straight to the socket (download, view): what came out, in out. */
static size_t
fetch(const char *path, const char *query, char *out, size_t n) {
  int sv[2];
  if(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) exit(2);
  ps5tm_request_t req;
  memset(&req, 0, sizeof(req));
  snprintf(req.method, sizeof(req.method), "GET");
  snprintf(req.path, sizeof(req.path), "%s", path);
  snprintf(req.query, sizeof(req.query), "%s", query);
  g_status = 0;
  g_body[0] = 0;
  ps5tm_filemgr_api(sv[1], &req);
  close(sv[1]);
  size_t got = 0;
  ssize_t r;
  while(got + 1 < n && (r = read(sv[0], out + got, n - 1 - got)) > 0) got += (size_t)r;
  out[got] = 0;
  close(sv[0]);
  return got;
}

static int
upload(const char *query, const char *body) {
  size_t len = strlen(body);
  g_status = 0;
  g_body[0] = 0;
  ps5tm_filemgr_receive_upload(-1, query, body, len, len);   /* all of the body came with the headers */
  return g_status;
}

static void
wait_job(void) {
  for(int i = 0; i < 3000 && ps5tm_filemgr_busy(); i++) usleep(10000);
  callq("/api/v1/files/job", "");
}

static int
transfer(const char *what, const char *src, const char *dest, const char *conflict) {
  char body[1024];
  if(conflict) snprintf(body, sizeof(body), "{\"paths\":[\"%s\"],\"dest\":\"%s\",\"conflict\":\"%s\"}", src, dest, conflict);
  else snprintf(body, sizeof(body), "{\"paths\":[\"%s\"],\"dest\":\"%s\"}", src, dest);
  call("POST", what, body);
  int st = g_status;
  if(st == 200) wait_job();
  return st;
}

/* delete with its plan, as the page does it: the plan's token goes back with the same list */
static int
delete_path(const char *p) {
  char body[512];
  snprintf(body, sizeof(body), "{\"paths\":[\"%s\"]}", p);
  call("POST", "/api/v1/files/delete/plan", body);
  if(g_status != 200) return g_status;
  const char *c = strstr(g_body, "\"confirm\":\"");
  if(!c) return -1;
  char tok[40];
  snprintf(tok, sizeof(tok), "%.32s", c + 11);
  snprintf(body, sizeof(body), "{\"paths\":[\"%s\"],\"confirm\":\"%s\"}", p, tok);
  call("POST", "/api/v1/files/delete", body);
  int st = g_status;
  if(st == 200) wait_job();
  return st;
}

static void
put_file(const char *rel, const char *data, size_t len) {
  char p[512], cmd[700];
  snprintf(p, sizeof(p), "%s%s", ROOT, rel);
  snprintf(cmd, sizeof(cmd), "mkdir -p \"$(dirname '%s')\"", p);
  if(system(cmd) != 0) exit(2);
  FILE *f = fopen(p, "wb");
  if(!f || fwrite(data, 1, len, f) != len) exit(2);
  fclose(f);
}

static char *
read_file(const char *rel, size_t *len) {
  char p[512];
  snprintf(p, sizeof(p), "%s%s", ROOT, rel);
  FILE *f = fopen(p, "rb");
  if(!f) return NULL;
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  char *b = malloc((size_t)n + 1);
  if(!b || fread(b, 1, (size_t)n, f) != (size_t)n) exit(2);
  fclose(f);
  b[n] = 0;
  *len = (size_t)n;
  return b;
}

static int
exists(const char *rel) {
  char p[512];
  struct stat st;
  snprintf(p, sizeof(p), "%s%s", ROOT, rel);
  return lstat(p, &st) == 0;
}

static int has(const char *s) { return strstr(g_body, s) != NULL; }

int
main(int argc, char **argv) {
  if(argc < 4) { fprintf(stderr, "usage: test <writable image> <read-only image> <disk with an MBR partition>\n"); return 2; }
  g_verbose = getenv("NTFS_TEST_VERBOSE") != NULL;
  char cmd[700];
  snprintf(cmd, sizeof(cmd), "rm -rf '%s' && mkdir -p '%s/data'", ROOT, ROOT);
  if(system(cmd) != 0) return 2;
  ps5tm_ntfs_add_source(argv[1]);
  ps5tm_ntfs_add_source(argv[2]);
  ps5tm_ntfs_add_source(argv[3]);

  /* a big file over several 1-MB steps of a copy, and one with a name NTFS cannot hold */
  size_t big_len = 3 * 1024 * 1024 + 12345;
  char *big = malloc(big_len);
  for(size_t i = 0; i < big_len; i++) big[i] = (char)(i * 131 + (i >> 9));
  put_file("/data/src/big.bin", big, big_len);
  put_file("/data/src/sub/klein.txt", "klein", 5);
  put_file("/data/src/weird:name?.txt", "w", 1);

  /* places */
  callq("/api/v1/files/places", "");
  CHECK(g_status == 200 && has("\"path\":\"/ntfs0\"") && has("ntfs0 (NTFS · PS5TEST)") && has("\"fs\":\"ntfs\""), "places: ntfs0");
  CHECK(has("\"path\":\"/ntfs1\""), "places: ntfs1 (the read-only image)");
  CHECK(has("ntfs2 (NTFS · MITMBR)"), "places: ntfs2, found through the MBR partition table of a whole disk");

  /* the partitioned disk, as Windows formats a stick: writable like the others */
  call("POST", "/api/v1/files/mkdir", "{\"path\":\"/ntfs2\",\"name\":\"Partition\"}");
  CHECK(g_status == 200, "mkdir on the partition");
  CHECK(upload("path=%2Fntfs2%2FPartition&name=p.txt", "auf der Partition") == 200, "upload onto the partition");

  /* listing the empty root */
  callq("/api/v1/files/list", "path=%2Fntfs0");
  CHECK(g_status == 200 && has("\"fs\":\"ntfs\"") && has("\"renamable\":false") && has("\"writable\":true") && has("\"parent\":\"\""),
        "list of the root");

  /* a new folder, taken, and one with a name NTFS cannot hold */
  call("POST", "/api/v1/files/mkdir", "{\"path\":\"/ntfs0\",\"name\":\"Spiele\"}");
  CHECK(g_status == 200 && has("\"path\":\"/ntfs0/Spiele\""), "mkdir");
  call("POST", "/api/v1/files/mkdir", "{\"path\":\"/ntfs0\",\"name\":\"SPIELE\"}");
  CHECK(g_status == 409, "mkdir of a taken name (NTFS ignores case)");
  call("POST", "/api/v1/files/mkdir", "{\"path\":\"/ntfs0\",\"name\":\"a:b\"}");
  CHECK(g_status == 200 && has("\"path\":\"/ntfs0/a_b\""), "mkdir with an adjusted name");

  /* uploads and taken names */
  CHECK(upload("path=%2Fntfs0%2FSpiele&name=notiz.txt", "Hallo NTFS") == 200, "upload");
  CHECK(upload("path=%2Fntfs0%2FSpiele&name=notiz.txt", "zweite") == 409 && has("\"code\":\"exists\""), "upload of a taken name asks");
  CHECK(upload("path=%2Fntfs0%2FSpiele&name=notiz.txt&conflict=replace", "zweite") == 409 && has("no_replace"), "upload: no replacing on NTFS");
  CHECK(upload("path=%2Fntfs0%2FSpiele&name=notiz.txt&conflict=keep", "zweite") == 200 && has("notiz (2).txt"), "upload: keep both");
  CHECK(upload("path=%2Fntfs0%2FSpiele&name=notiz.txt&conflict=skip", "dritte") == 200 && has("\"skipped\":true"), "upload: skip");

  /* download, view, size */
  char out[8192];
  fetch("/api/v1/files/download", "path=%2Fntfs0%2FSpiele%2Fnotiz.txt", out, sizeof(out));
  CHECK(strstr(out, "Content-Length: 10\r\n") && strstr(out, "\r\n\r\nHallo NTFS"), "download: %.200s", out);
  fetch("/api/v1/files/view", "path=%2Fntfs0%2FSpiele%2Fnotiz%20(2).txt", out, sizeof(out));
  CHECK(strstr(out, "text/plain") && strstr(out, "\r\n\r\nzweite"), "view: %.200s", out);
  callq("/api/v1/files/size", "path=%2Fntfs0%2FSpiele");
  CHECK(g_status == 200 && has("\"files\":2") && has("\"bytes\":16"), "size");

  /* copy console -> NTFS: names adjusted, bytes the same (read back over NTFS below) */
  CHECK(transfer("/api/v1/files/copy", "/data/src", "/ntfs0/Spiele", NULL) == 200, "copy to NTFS starts");
  CHECK(has("\"state\":\"done\""), "copy to NTFS done");
  callq("/api/v1/files/list", "path=%2Fntfs0%2FSpiele%2Fsrc");
  CHECK(has("\"name\":\"big.bin\"") && has("\"name\":\"sub\"") && has("\"name\":\"weird_name_.txt\""), "copied tree on NTFS");

  /* copy NTFS -> console: the same bytes come back */
  CHECK(transfer("/api/v1/files/copy", "/ntfs0/Spiele/src", "/data/back", NULL) == 403, "no copy into a folder that is not there");
  call("POST", "/api/v1/files/mkdir", "{\"path\":\"/data\",\"name\":\"back\"}");
  CHECK(transfer("/api/v1/files/copy", "/ntfs0/Spiele/src", "/data/back", NULL) == 200 && has("\"state\":\"done\""), "copy from NTFS");
  size_t l = 0;
  char *b = read_file("/data/back/src/big.bin", &l);
  CHECK(b && l == big_len && !memcmp(b, big, big_len), "big.bin read back from NTFS is the same (%zu bytes)", l);
  free(b);
  b = read_file("/data/back/src/sub/klein.txt", &l);
  CHECK(b && l == 5 && !memcmp(b, "klein", 5), "klein.txt read back");
  free(b);

  /* taken names when copying onto NTFS */
  CHECK(transfer("/api/v1/files/copy", "/data/src", "/ntfs0/Spiele", NULL) == 409 && has("\"code\":\"exists\""), "copy of a taken name asks");
  CHECK(transfer("/api/v1/files/copy", "/data/src", "/ntfs0/Spiele", "replace") == 409 && has("ersetzt die App nichts"), "no replace on NTFS");
  CHECK(transfer("/api/v1/files/copy", "/data/src", "/ntfs0/Spiele", "keep") == 200 && has("\"state\":\"done\""), "keep both");
  callq("/api/v1/files/list", "path=%2Fntfs0%2FSpiele");
  CHECK(has("\"name\":\"src (2)\""), "src (2) made");
  CHECK(transfer("/api/v1/files/copy", "/data/src", "/ntfs0/Spiele", "skip") == 200 && has("blieben unverändert"), "skip");

  /* move console -> NTFS -> console */
  put_file("/data/mv/m.txt", "move me", 7);
  CHECK(transfer("/api/v1/files/move", "/data/mv", "/ntfs0", NULL) == 200 && has("\"state\":\"done\""), "move to NTFS");
  CHECK(!exists("/data/mv"), "the original went after the move");
  callq("/api/v1/files/list", "path=%2Fntfs0%2Fmv");
  CHECK(g_status == 200 && has("\"name\":\"m.txt\""), "moved folder on NTFS");
  CHECK(transfer("/api/v1/files/move", "/ntfs0/mv", "/data", NULL) == 200 && has("\"state\":\"done\""), "move from NTFS");
  b = read_file("/data/mv/m.txt", &l);
  CHECK(b && l == 7 && !memcmp(b, "move me", 7), "m.txt back on the console");
  free(b);
  callq("/api/v1/files/list", "path=%2Fntfs0%2Fmv");
  CHECK(g_status == 404, "the NTFS original went after the move");

  /* refusals */
  call("POST", "/api/v1/files/rename", "{\"path\":\"/ntfs0/Spiele/notiz.txt\",\"name\":\"x.txt\"}");
  CHECK(g_status == 403 && has("ntfs_no_rename"), "no rename on NTFS");
  {
    ps5tm_request_t req;
    memset(&req, 0, sizeof(req));
    snprintf(req.method, sizeof(req.method), "POST");
    snprintf(req.path, sizeof(req.path), "/api/v1/files/write");
    snprintf(req.query, sizeof(req.query), "path=%%2Fntfs0%%2FSpiele%%2Fnotiz.txt");
    req.body = (char *)"neu";
    req.body_len = 3;
    ps5tm_filemgr_api(0, &req);
    CHECK(g_status == 403 && has("ntfs_no_change"), "no editing on NTFS");
  }
  call("POST", "/api/v1/files/delete/plan", "{\"paths\":[\"/ntfs0\"]}");
  CHECK(g_status == 403, "the drive itself is not deleted");

  /* delete with its plan */
  CHECK(delete_path("/ntfs0/Spiele/src (2)") == 200 && has("\"state\":\"done\""), "delete a folder on NTFS");
  callq("/api/v1/files/list", "path=%2Fntfs0%2FSpiele");
  CHECK(!has("src (2)") && has("\"name\":\"src\""), "deleted, the rest stays");
  CHECK(delete_path("/ntfs0/a_b") == 200, "delete an empty folder");

  /* the read-only drive: the first write finds it cannot be opened for writing, and says so afterwards (skipped as root) */
  if(geteuid() != 0) {
    call("POST", "/api/v1/files/mkdir", "{\"path\":\"/ntfs1\",\"name\":\"neu\"}");
    CHECK(g_status >= 400 && g_status != 404, "mkdir on a drive that cannot be written is refused");
    callq("/api/v1/files/list", "path=%2Fntfs1");
    CHECK(g_status == 200 && has("\"writable\":false") && has("nicht zum Schreiben"), "the drive says why it is read-only");
    CHECK(transfer("/api/v1/files/copy", "/data/src/sub", "/ntfs1", NULL) == 403, "no copy onto it");
  }

  /* eject: gone from the list and the places, not taken up again */
  call("POST", "/api/v1/files/eject", "{\"path\":\"/ntfs0\"}");
  CHECK(g_status == 200 && has("kann jetzt abgezogen werden"), "eject");
  callq("/api/v1/files/list", "path=%2Fntfs0");
  CHECK(g_status == 404, "ejected drive is gone");
  ps5tm_ntfs_scan(1);
  callq("/api/v1/files/places", "");
  CHECK(!has("\"path\":\"/ntfs0\""), "ejected drive is not taken up again");
  call("POST", "/api/v1/files/eject", "{\"path\":\"/data\"}");
  CHECK(g_status == 400, "eject refuses what is no NTFS drive");

  free(big);
  ps5tm_ntfs_shutdown();
  printf("%d checks, %d failed\n", g_ok + g_fail, g_fail);
  return g_fail ? 1 : 0;
}
