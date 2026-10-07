/* The file manager (06.10.2026). The user chose all of it: looking and downloading, uploading and new folders,
 * copying, moving and renaming, and deleting; and where it may change things: "Laufwerke + /data" — the USB sticks
 * and discs, the M.2 drives and /data. Everything else can only be looked at.
 *
 * What may be changed (write_root): a path strictly below /mnt/usbN, /mnt/ext0, /mnt/ext1 or /data, never the mount
 * point itself; in the app's own folder only below its "payloads" folder (that is where the Payloads page looks), so
 * the settings and logs cannot be deleted from here. A path to be changed must be plain (absolute, no "." or ".."
 * parts, no doubled slashes) and no part of it may be a link (no_links): a link in /data pointing at /system must not
 * make /system writable. Copying and deleting never follow a link and never enter another drive.
 *
 * Looking is allowed everywhere: listing a folder, downloading a regular file (a device file is refused, so /dev
 * cannot be read endlessly).
 *
 * Copying, moving and deleting run as one job at a time on a thread of their own, with a bar (bytes done of total),
 * the current file and Abbrechen. Nothing is ever overwritten: a name that is taken stops the job before it begins.
 * A copy that fails or is cancelled removes what it had written of the item it was at (that item's target is the
 * job's own: it did not exist before). Moving within one drive is a rename; across drives it is a copy, and the
 * source is deleted only when its copy is complete. Deleting needs the token of its plan, which the page gets after
 * showing what goes (one use, ten minutes, exactly that list), like deleting a game (gamedelete.c).
 *
 * Paths in the API are the console's; FM_ROOT puts them below a folder of a host test's own. */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "ioerr.h"
#include "ps5tm.h"
#include "third_party/cJSON.h"

#ifndef FM_ROOT
#define FM_ROOT          ""
#endif
#ifndef FM_TOKEN_MS
#define FM_TOKEN_MS      600000          /* a delete plan's token is good for ten minutes */
#endif
#ifndef FM_TRUNC_STEP
#define FM_TRUNC_STEP    (512ll << 20)   /* a big file is shortened in such steps before it is removed (the bar moves) */
#endif
#define FM_PATH          1024
#define FM_NAME          256
#define FM_LIST_MAX      3000            /* entries shown of one folder */
#define FM_SEL_MAX       100             /* items in one copy, move or delete */
#define FM_DEPTH         64
#define FM_COUNT_MAX     1000000         /* entries counted for one job */
#define FM_BUF           (1u << 20)
#define FM_SPACE_SPARE   (4ull << 20)    /* left free on the target beyond what is copied */
#define APP_DIR          "/data/PS5-Cooling-Center"
#define APP_PAYLOADS     APP_DIR "/payloads"
#define UPLOAD_EXT       ".ps5cc-hochladen"

enum { FM_IDLE, FM_COUNTING, FM_COPYING, FM_DELETING, FM_DONE, FM_FAILED, FM_CANCELLED };
static const char *const k_state[] = { "idle", "counting", "copying", "deleting", "done", "failed", "cancelled" };

typedef struct {
  int      state;
  char     kind[8];                       /* "copy", "move", "delete" */
  char     dest[FM_PATH];
  char     current[FM_PATH];
  char     error[800];
  char     note[400];
  uint64_t bytes_total, bytes_done, files_total, files_done;
  unsigned items, items_done, skipped;
  uint64_t started_ms, finished_ms;
} fm_job_t;

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_upload_lock = PTHREAD_MUTEX_INITIALIZER;
static fm_job_t        g_job;
static int             g_cancel;

static struct {
  char     token[33];
  char    *list;                          /* the plan's paths, one per line */
  uint64_t until_ms;
} g_tok;

typedef struct {
  char     kind[8];
  char     dest[FM_PATH];
  unsigned n;
  char   (*src)[FM_PATH];
} fm_args_t;


/* ------------------------------------------------------------------ paths */

static int
ends_with(const char *s, const char *tail) {
  size_t a = strlen(s), b = strlen(tail);
  return a >= b && !strcmp(s + a - b, tail);
}

/* Absolute, plain, short enough, no control characters. "/" itself is plain. */
static int
path_plain(const char *p) {
  if(!p || p[0] != '/' || strlen(p) >= FM_PATH - 64) return 0;
  if(!strcmp(p, "/")) return 1;
  if(strstr(p, "//") || strstr(p, "/./") || strstr(p, "/../") || ends_with(p, "/.") || ends_with(p, "/..") ||
     ends_with(p, "/"))
    return 0;
  for(const unsigned char *c = (const unsigned char *)p; *c; c++)
    if(*c < 0x20 || *c == 0x7f) return 0;
  return 1;
}

/* A name for a new file or folder: one part, not "." or "..", no slash, no control character. */
static int
name_ok(const char *n) {
  if(!n || !n[0] || strlen(n) >= FM_NAME || !strcmp(n, ".") || !strcmp(n, "..")) return 0;
  for(const unsigned char *c = (const unsigned char *)n; *c; c++)
    if(*c < 0x20 || *c == 0x7f || *c == '/' || *c == '\\') return 0;
  return 1;
}

/* Where the console's path p lies on this system (FM_ROOT before it). */
static int
real_path(const char *p, char *out, size_t n) {
  int w = snprintf(out, n, "%s%s", FM_ROOT, p);
  return w > 0 && (size_t)w < n ? 0 : -1;
}

static int
join(const char *dir, const char *name, char *out, size_t n) {
  int w = snprintf(out, n, "%s/%s", strcmp(dir, "/") ? dir : "", name);
  return w > 0 && (size_t)w < n ? 0 : -1;
}

static const char *
base_name(const char *p) {
  const char *s = strrchr(p, '/');
  return s ? s + 1 : p;
}

static void
dir_name(const char *p, char *out, size_t n) {
  snprintf(out, n, "%s", p);
  char *s = strrchr(out, '/');
  if(s == out) out[1] = 0;
  else if(s) *s = 0;
}

/* The mount point below which p may be changed, or NULL. */
static const char *
write_root(const char *p) {
  static const char *const fixed[] = { "/mnt/ext0", "/mnt/ext1", "/data" };
  if(!path_plain(p) || !strcmp(p, "/")) return NULL;
  size_t al = strlen(APP_DIR);
  if(!strncmp(p, APP_DIR, al) && (p[al] == 0 || p[al] == '/')) {
    size_t pl = strlen(APP_PAYLOADS);
    if(!(strncmp(p, APP_PAYLOADS, pl) == 0 && p[pl] == '/' && p[pl + 1])) return NULL;
  }
  for(size_t i = 0; i < sizeof(fixed) / sizeof(fixed[0]); i++) {
    size_t l = strlen(fixed[i]);
    if(!strncmp(p, fixed[i], l) && p[l] == '/' && p[l + 1]) return fixed[i];
  }
  const size_t ul = strlen("/mnt/usb");
  if(!strncmp(p, "/mnt/usb", ul) && p[ul] >= '0' && p[ul] <= '9') {
    size_t l = ul + 1;
    while(p[l] >= '0' && p[l] <= '9') l++;
    if(p[l] == '/' && p[l + 1]) return "/mnt/usb";
  }
  return NULL;
}

/* No part of p from the mount point on is a link (the last part may be missing: a target yet to be made). The
   folders on the way must exist and be folders. */
static int
no_links(const char *p) {
  const char *root = write_root(p);
  if(!root) return 0;
  char part[FM_PATH], real[FM_PATH + 64];
  size_t start = strlen(root);
  if(!strcmp(root, "/mnt/usb")) while(p[start] >= '0' && p[start] <= '9') start++;
  for(size_t i = start; ; i++) {
    if(p[i] != '/' && p[i] != 0) continue;
    memcpy(part, p, i);
    part[i] = 0;
    if(real_path(part, real, sizeof(real)) != 0) return 0;
    struct stat st;
    if(lstat(real, &st) != 0) return p[i] == 0 && errno == ENOENT;   /* only the last part may be missing */
    if(S_ISLNK(st.st_mode)) return 0;
    if(p[i] != 0 && !S_ISDIR(st.st_mode)) return 0;
    if(p[i] == 0) return 1;
  }
}

/* Is p a drive hung in below its parent (another file system than the folder it is in)? Deleting into it would
   empty that drive. */
static int
mounted_here(const char *p) {
  char real[FM_PATH + 64], parent[FM_PATH], rp[FM_PATH + 64];
  struct stat a, b;
  dir_name(p, parent, sizeof(parent));
  if(real_path(p, real, sizeof(real)) != 0 || real_path(parent, rp, sizeof(rp)) != 0) return 1;
  if(lstat(real, &a) != 0) return 0;                  /* not there (yet): nothing hangs there */
  if(lstat(rp, &b) != 0) return 1;
  return a.st_dev != b.st_dev;
}

/* May p be changed (created, renamed, deleted)? */
static int
writable(const char *p) {
  return write_root(p) && no_links(p) && !mounted_here(p);
}

/* May something be made inside the folder dir? A name below it must be writable; the name used to ask is one nobody
   has (a link of that name would only make the answer a cautious no). */
static int
dir_writable(const char *dir) {
  char probe[FM_PATH];
  return join(dir, ".ps5cc-schreibprobe", probe, sizeof(probe)) == 0 && write_root(probe) && no_links(probe);
}

static int
space_free(const char *dir, uint64_t *avail) {
  char real[FM_PATH + 64];
  struct statvfs sv;
  if(real_path(dir, real, sizeof(real)) != 0 || statvfs(real, &sv) != 0) return -1;
  *avail = (uint64_t)sv.f_bavail * (sv.f_frsize ? sv.f_frsize : sv.f_bsize);
  return 0;
}

/* The words for a failed write, with the case people meet: a file over 4 GB on a FAT32 stick. */
static const char *
io_words(int e) {
  if(e == EFBIG) return "Die Datei ist zu groß für dieses Laufwerk (FAT32 nimmt höchstens 4 GB je Datei; exFAT kann mehr).";
  if(e == ENOSPC) return "Auf dem Laufwerk ist kein Platz mehr.";
  if(e == EROFS) return "Das Laufwerk ist schreibgeschützt.";
  if(e == E2BIG) return "Zu viele Dateien für einen Vorgang (höchstens eine Million).";
  if(e == ELOOP) return "Zu tief verschachtelt.";
  if(e == ENAMETOOLONG) return "Ein Pfad darin ist zu lang.";
  return ps5tm_io_strerror(e);
}


/* ------------------------------------------------------------------ http helpers */

static void
send_obj(int fd, int status, cJSON *o) {
  char *s = o ? cJSON_PrintUnformatted(o) : NULL;
  cJSON_Delete(o);
  if(!s) { ps5tm_http_send_error(fd, 500, "alloc_failed", "Kein Speicher."); return; }
  ps5tm_http_send_json(fd, status, s);
  free(s);
}

static int
hexval(int c) {
  if(c >= '0' && c <= '9') return c - '0';
  if(c >= 'a' && c <= 'f') return c - 'a' + 10;
  if(c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

/* key=value among the query parameters, percent-decoded (encodeURIComponent). -1 when missing, broken, too long
   or holding a NUL. */
static int
query_value(const char *query, const char *key, char *out, size_t n) {
  size_t kl = strlen(key);
  out[0] = 0;
  for(const char *p = query; p && *p; ) {
    if(!strncmp(p, key, kl) && p[kl] == '=') {
      const char *v = p + kl + 1;
      size_t o = 0;
      for(; *v && *v != '&'; v++) {
        int c = (unsigned char)*v;
        if(c == '%') {
          int h = hexval(v[1]), l = h >= 0 ? hexval(v[2]) : -1;
          if(h < 0 || l < 0) return -1;
          c = h * 16 + l;
          v += 2;
          if(c == 0) return -1;
        }
        if(o + 1 >= n) return -1;
        out[o++] = (char)c;
      }
      out[o] = 0;
      return 0;
    }
    p = strchr(p, '&');
    if(p) p++;
  }
  return -1;
}

static const char *
jstr(const cJSON *o, const char *k) {
  const cJSON *v = cJSON_GetObjectItem(o, k);
  return cJSON_IsString(v) ? v->valuestring : NULL;
}


/* ------------------------------------------------------------------ places and listing */

static void
add_place(cJSON *arr, const char *label, const char *path, int writable_here) {
  cJSON *o = cJSON_CreateObject();
  cJSON_AddStringToObject(o, "label", label);
  cJSON_AddStringToObject(o, "path", path);
  uint64_t fr = 0;
  cJSON_AddNumberToObject(o, "free_bytes", space_free(path, &fr) == 0 ? (double)fr : -1);
  cJSON_AddBoolToObject(o, "writable", writable_here);
  cJSON_AddItemToArray(arr, o);
}

static cJSON *
places_json(void) {
  cJSON *o = cJSON_CreateObject();
  cJSON_AddBoolToObject(o, "ok", 1);
  cJSON *arr = cJSON_AddArrayToObject(o, "places");
  ps5tm_pkgdrive_t dr[PS5TM_MAX_VOLUMES];
  unsigned nd = ps5tm_pkg_drives(dr, PS5TM_MAX_VOLUMES);
  int have_data = 0;
  for(unsigned i = 0; i < nd; i++) {
    if(dr[i].internal) { add_place(arr, "Interne SSD (/data)", "/data", 1); have_data = 1; continue; }
    add_place(arr, dr[i].label[0] ? dr[i].label : dr[i].mount, dr[i].mount, 1);
  }
  if(!have_data) add_place(arr, "Interne SSD (/data)", "/data", 1);
  add_place(arr, "Ganzes System (nur ansehen)", "/", 0);
  return o;
}

typedef struct {
  char     name[FM_NAME];
  char     type;                          /* 'd', 'f', 'l', 'o' */
  uint64_t size;
  int64_t  mtime;
} entry_t;

static int
entry_cmp(const void *a, const void *b) {
  const entry_t *x = a, *y = b;
  int dx = x->type == 'd', dy = y->type == 'd';
  if(dx != dy) return dy - dx;                       /* folders first */
  int c = strcasecmp(x->name, y->name);
  return c ? c : strcmp(x->name, y->name);
}

static void
handle_list(int fd, const ps5tm_request_t *req) {
  char p[FM_PATH], real[FM_PATH + 64];
  if(query_value(req->query, "path", p, sizeof(p)) != 0 || !path_plain(p) || real_path(p, real, sizeof(real)) != 0) {
    ps5tm_http_send_error(fd, 400, "bad_path", "Ungültiger Pfad.");
    return;
  }
  DIR *d = opendir(real);
  if(!d) {
    int e = errno;
    char msg[400];
    snprintf(msg, sizeof(msg), "Der Ordner lässt sich nicht öffnen: %s", ps5tm_io_strerror(e));
    ps5tm_http_send_error(fd, e == ENOENT || e == ENOTDIR ? 404 : 403, "list_failed", msg);
    return;
  }
  entry_t *v = malloc(sizeof(entry_t) * FM_LIST_MAX);
  if(!v) { closedir(d); ps5tm_http_send_error(fd, 500, "alloc_failed", "Kein Speicher."); return; }
  unsigned n = 0, total = 0;
  struct dirent *e;
  char child[FM_PATH + FM_NAME + 64];
  while((e = readdir(d)) != NULL) {
    if(!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
    total++;
    if(n >= FM_LIST_MAX || strlen(e->d_name) >= FM_NAME) continue;
    entry_t *x = &v[n];
    snprintf(x->name, sizeof(x->name), "%s", e->d_name);
    snprintf(child, sizeof(child), "%s/%s", real, e->d_name);
    struct stat st;
    if(lstat(child, &st) != 0) { x->type = 'o'; x->size = 0; x->mtime = 0; n++; continue; }
    x->type = S_ISDIR(st.st_mode) ? 'd' : S_ISREG(st.st_mode) ? 'f' : S_ISLNK(st.st_mode) ? 'l' : 'o';
    x->size = S_ISREG(st.st_mode) ? (uint64_t)st.st_size : 0;
    x->mtime = (int64_t)st.st_mtime;
    if(x->type == 'l') {                             /* a link to a folder opens like one; it is marked as a link */
      struct stat ts;
      if(stat(child, &ts) == 0 && S_ISDIR(ts.st_mode)) x->type = 'L';
    }
    n++;
  }
  closedir(d);
  qsort(v, n, sizeof(entry_t), entry_cmp);

  cJSON *o = cJSON_CreateObject();
  cJSON_AddBoolToObject(o, "ok", 1);
  cJSON_AddStringToObject(o, "path", p);
  char parent[FM_PATH];
  dir_name(p, parent, sizeof(parent));
  cJSON_AddStringToObject(o, "parent", strcmp(p, "/") ? parent : "");
  int here = dir_writable(p);
  cJSON_AddBoolToObject(o, "writable", here);
  uint64_t fr = 0;
  cJSON_AddNumberToObject(o, "free_bytes", space_free(p, &fr) == 0 ? (double)fr : -1);
  cJSON_AddNumberToObject(o, "total", total);
  cJSON_AddBoolToObject(o, "truncated", total > n);
  cJSON *arr = cJSON_AddArrayToObject(o, "entries");
  char vp[FM_PATH + FM_NAME];
  for(unsigned i = 0; i < n; i++) {
    cJSON *x = cJSON_CreateObject();
    cJSON_AddStringToObject(x, "name", v[i].name);
    const char *t = v[i].type == 'd' ? "dir" : v[i].type == 'f' ? "file" : v[i].type == 'L' ? "dirlink" :
                    v[i].type == 'l' ? "link" : "other";
    cJSON_AddStringToObject(x, "type", t);
    cJSON_AddNumberToObject(x, "size", (double)v[i].size);
    cJSON_AddNumberToObject(x, "mtime", (double)v[i].mtime);
    /* may this entry be renamed, moved away or deleted? (a link only as a link, which no_links refuses: no) */
    int w = here && snprintf(vp, sizeof(vp), "%s/%s", strcmp(p, "/") ? p : "", v[i].name) < (int)sizeof(vp) &&
            v[i].type != 'l' && v[i].type != 'L' && writable(vp);
    cJSON_AddBoolToObject(x, "writable", w);
    cJSON_AddItemToArray(arr, x);
  }
  free(v);
  send_obj(fd, 200, o);
}


/* ------------------------------------------------------------------ download */

static void
handle_download(int fd, const ps5tm_request_t *req) {
  char p[FM_PATH], real[FM_PATH + 64];
  if(query_value(req->query, "path", p, sizeof(p)) != 0 || !path_plain(p) || !strcmp(p, "/") ||
     real_path(p, real, sizeof(real)) != 0) {
    ps5tm_http_send_error(fd, 400, "bad_path", "Ungültiger Pfad.");
    return;
  }
  int in = open(real, O_RDONLY | O_NONBLOCK);       /* a FIFO must not hold the thread until someone writes */
  struct stat st;
  if(in < 0 || fstat(in, &st) != 0 || !S_ISREG(st.st_mode)) {
    if(in >= 0) close(in);
    ps5tm_http_send_error(fd, 404, "not_a_file", "Das ist keine Datei, die sich herunterladen lässt.");
    return;
  }
  /* the name twice: plain ASCII for old clients (others become "_"), and UTF-8 percent-encoded (RFC 5987) */
  const char *nm = base_name(p);
  char ascii[FM_NAME], enc[FM_NAME * 3 + 1];
  size_t a = 0, e = 0;
  for(const unsigned char *c = (const unsigned char *)nm; *c && a + 1 < sizeof(ascii); c++) {
    if(*c >= 0x80 && *c < 0xc0) continue;          /* one "_" per character, not per byte of its UTF-8 */
    ascii[a++] = (*c >= 0x20 && *c < 0x7f && *c != '"' && *c != '\\' && *c != ';' && *c != '%') ? (char)*c : '_';
  }
  ascii[a] = 0;
  static const char hex[] = "0123456789ABCDEF";
  for(const unsigned char *c = (const unsigned char *)nm; *c && e + 4 < sizeof(enc); c++) {
    if((*c >= 'A' && *c <= 'Z') || (*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || strchr("-._~", *c))
      enc[e++] = (char)*c;
    else { enc[e++] = '%'; enc[e++] = hex[*c >> 4]; enc[e++] = hex[*c & 15]; }
  }
  enc[e] = 0;
  char head[FM_NAME * 5 + 400];
  int n = snprintf(head, sizeof(head),
                   "HTTP/1.1 200 OK\r\n"
                   "Content-Type: application/octet-stream\r\n"
                   "Content-Length: %lld\r\n"
                   "Content-Disposition: attachment; filename=\"%s\"; filename*=UTF-8''%s\r\n"
                   "Cache-Control: no-store\r\n"
                   "X-Content-Type-Options: nosniff\r\n"
                   "Connection: close\r\n\r\n",
                   (long long)st.st_size, ascii, enc);
  if(n < 0 || n >= (int)sizeof(head)) { close(in); ps5tm_http_send_error(fd, 500, "name_too_long", "Name zu lang."); return; }
  /* a long download: the browser may read slowly, so a send may wait a while */
  struct timeval tv = { 60, 0 };
  setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
  char *buf = malloc(256 * 1024);
  if(!buf) { close(in); ps5tm_http_send_error(fd, 500, "alloc_failed", "Kein Speicher."); return; }
  ssize_t w = write(fd, head, (size_t)n);
  off_t left = st.st_size;
  while(w == n && left > 0) {
    ssize_t r = read(in, buf, 256 * 1024);
    if(r <= 0) break;
    ssize_t off = 0;
    while(off < r) {
      ssize_t k = write(fd, buf + off, (size_t)(r - off));
      if(k < 0 && errno == EINTR) continue;
      if(k <= 0) { left = -1; break; }
      off += k;
    }
    if(left < 0) break;
    left -= r;
  }
  free(buf);
  close(in);
}


/* ------------------------------------------------------------------ mkdir, rename */

static cJSON *
parse_body(const ps5tm_request_t *req) {
  return req->body ? cJSON_Parse(req->body) : NULL;
}

static void
handle_mkdir(int fd, const ps5tm_request_t *req) {
  cJSON *b = parse_body(req);
  const char *dir = jstr(b, "path"), *name = jstr(b, "name");
  char t[FM_PATH], real[FM_PATH + 64];
  if(!dir || !name || !path_plain(dir) || !name_ok(name) || join(dir, name, t, sizeof(t)) != 0) {
    cJSON_Delete(b);
    ps5tm_http_send_error(fd, 400, "bad_name", "Ungültiger Ordner oder Name.");
    return;
  }
  if(!dir_writable(dir) || !writable(t)) {
    cJSON_Delete(b);
    ps5tm_http_send_error(fd, 403, "read_only", "Hier darf nichts angelegt werden (nur auf den Laufwerken und in /data).");
    return;
  }
  cJSON_Delete(b);
  if(real_path(t, real, sizeof(real)) != 0 || mkdir(real, 0777) != 0) {
    int e = errno;
    char msg[500];
    snprintf(msg, sizeof(msg), e == EEXIST ? "Diesen Namen gibt es hier schon." : "Der Ordner ließ sich nicht anlegen: %s", io_words(e));
    ps5tm_http_send_error(fd, e == EEXIST ? 409 : 500, "mkdir_failed", msg);
    return;
  }
  PS5TM_INFO("files_mkdir", "Dateien: Ordner angelegt: %.150s", t);
  cJSON *o = cJSON_CreateObject();
  cJSON_AddBoolToObject(o, "ok", 1);
  cJSON_AddStringToObject(o, "path", t);
  send_obj(fd, 200, o);
}

static void
handle_rename(int fd, const ps5tm_request_t *req) {
  cJSON *b = parse_body(req);
  const char *p = jstr(b, "path"), *name = jstr(b, "name");
  char dir[FM_PATH], t[FM_PATH], rs[FM_PATH + 64], rt[FM_PATH + 64];
  if(!p || !name || !path_plain(p) || !name_ok(name)) {
    cJSON_Delete(b);
    ps5tm_http_send_error(fd, 400, "bad_name", "Ungültiger Pfad oder Name.");
    return;
  }
  dir_name(p, dir, sizeof(dir));
  int ok = join(dir, name, t, sizeof(t)) == 0;
  char src[FM_PATH], nm[FM_NAME];
  snprintf(src, sizeof(src), "%s", p);
  snprintf(nm, sizeof(nm), "%s", name);
  name = nm;                                        /* the body goes now; what is still needed is copied */
  cJSON_Delete(b);
  if(!ok || !writable(src) || !writable(t)) {
    ps5tm_http_send_error(fd, 403, "read_only", "Das lässt sich hier nicht umbenennen (nur auf den Laufwerken und in /data, keine Verknüpfungen).");
    return;
  }
  struct stat st;
  if(real_path(src, rs, sizeof(rs)) != 0 || real_path(t, rt, sizeof(rt)) != 0 || lstat(rs, &st) != 0) {
    ps5tm_http_send_error(fd, 404, "not_found", "Das gibt es nicht (mehr).");
    return;
  }
  if(!strcmp(rs, rt)) { ps5tm_http_send_json(fd, 200, "{\"ok\":true}"); return; }
  struct stat ts;
  /* a name that only differs in case on a drive that ignores case (exFAT) is the same file: that one may go ahead */
  if(lstat(rt, &ts) == 0 && !(ts.st_ino == st.st_ino && ts.st_dev == st.st_dev)) {
    ps5tm_http_send_error(fd, 409, "exists", "Diesen Namen gibt es hier schon.");
    return;
  }
  if(rename(rs, rt) != 0) {
    char msg[500];
    snprintf(msg, sizeof(msg), "Umbenennen ging nicht: %s", io_words(errno));
    ps5tm_http_send_error(fd, 500, "rename_failed", msg);
    return;
  }
  PS5TM_INFO("files_rename", "Dateien: umbenannt: %.90s -> %.60s", src, name);
  cJSON *o = cJSON_CreateObject();
  cJSON_AddBoolToObject(o, "ok", 1);
  cJSON_AddStringToObject(o, "path", t);
  send_obj(fd, 200, o);
}


/* ------------------------------------------------------------------ upload */

static int
read_some(int fd, char *buf, size_t n) {
  for(;;) {
    ssize_t r = read(fd, buf, n);
    if(r < 0 && errno == EINTR) continue;
    return (int)r;
  }
}

static void
receive_upload(int fd, const char *query, const char *prefix, size_t prefix_len, size_t total) {
  char dir[FM_PATH], name[FM_NAME * 3], t[FM_PATH], real[FM_PATH + 64], tmp[FM_PATH + 96];
  if(query_value(query, "path", dir, sizeof(dir)) != 0 || query_value(query, "name", name, sizeof(name)) != 0 ||
     !path_plain(dir) || !name_ok(name) || join(dir, name, t, sizeof(t)) != 0 || real_path(t, real, sizeof(real)) != 0) {
    ps5tm_http_send_error(fd, 400, "bad_name", "Ungültiger Ordner oder Dateiname.");
    return;
  }
  if(!dir_writable(dir) || !writable(t)) {
    ps5tm_http_send_error(fd, 403, "read_only", "Hierhin darf nichts geladen werden (nur auf die Laufwerke und nach /data).");
    return;
  }
  struct stat st;
  if(lstat(real, &st) == 0) { ps5tm_http_send_error(fd, 409, "exists", "Eine Datei dieses Namens gibt es hier schon."); return; }
  uint64_t fr = 0;
  if(space_free(dir, &fr) == 0 && fr < (uint64_t)total + FM_SPACE_SPARE) {
    ps5tm_http_send_error(fd, 409, "no_space", "Auf dem Laufwerk ist dafür nicht genug Platz.");
    return;
  }
  snprintf(tmp, sizeof(tmp), "%s" UPLOAD_EXT, real);
  int out = open(tmp, O_WRONLY | O_CREAT | O_EXCL, 0666);
  if(out < 0) {
    char msg[500];
    snprintf(msg, sizeof(msg), "Die Datei ließ sich nicht anlegen: %s", io_words(errno));
    ps5tm_http_send_error(fd, 500, "create_failed", msg);
    return;
  }
  ps5tm_powerguard_hold();
  char *buf = malloc(FM_BUF);
  size_t got = 0;
  int werr = buf ? 0 : ENOMEM;
  size_t first = prefix_len < total ? prefix_len : total;
  if(!werr && first) {
    if(write(out, prefix, first) != (ssize_t)first) werr = errno ? errno : EIO;
    got = first;
  }
  while(!werr && got < total) {
    size_t want = total - got < FM_BUF ? total - got : FM_BUF;
    int r = read_some(fd, buf, want);
    if(r <= 0) break;
    ssize_t off = 0;
    while(off < r) {
      ssize_t k = write(out, buf + off, (size_t)(r - off));
      if(k < 0 && errno == EINTR) continue;
      if(k <= 0) { werr = errno ? errno : EIO; break; }
      off += k;
    }
    got += (size_t)r;
  }
  free(buf);
  if(close(out) != 0 && !werr) werr = errno ? errno : EIO;
  ps5tm_powerguard_release();
  if(werr || got != total) {
    unlink(tmp);
    char msg[500];
    if(werr) snprintf(msg, sizeof(msg), "Schreiben ging nicht: %s", io_words(werr));
    else snprintf(msg, sizeof(msg), "Die Übertragung brach nach %zu von %zu Bytes ab; nichts wurde gespeichert.", got, total);
    ps5tm_http_send_error(fd, 500, "upload_incomplete", msg);
    return;
  }
  /* the name may have been taken while the bytes came: then the upload stays beside it under its own name */
  if(lstat(real, &st) == 0 || rename(tmp, real) != 0) {
    unlink(tmp);
    ps5tm_http_send_error(fd, 409, "exists", "Eine Datei dieses Namens ist inzwischen entstanden; nichts wurde überschrieben.");
    return;
  }
  PS5TM_INFO("files_upload", "Dateien: hochgeladen: %.140s (%zu KB).", t, total / 1024);
  cJSON *o = cJSON_CreateObject();
  cJSON_AddBoolToObject(o, "ok", 1);
  cJSON_AddStringToObject(o, "path", t);
  cJSON_AddNumberToObject(o, "bytes", (double)total);
  send_obj(fd, 200, o);
}

void
ps5tm_filemgr_receive_upload(int fd, const char *query, const char *prefix, size_t prefix_len, size_t total) {
  if(pthread_mutex_trylock(&g_upload_lock) != 0) {
    ps5tm_http_send_error(fd, 409, "upload_busy", "Es wird gerade schon eine Datei hochgeladen.");
    return;
  }
  receive_upload(fd, query, prefix, prefix_len, total);
  pthread_mutex_unlock(&g_upload_lock);
}


/* ------------------------------------------------------------------ the job: small things */

static int
cancelled(void) {
  return __atomic_load_n(&g_cancel, __ATOMIC_ACQUIRE);
}

static void
job_current(const char *v) {
  pthread_mutex_lock(&g_lock);
  snprintf(g_job.current, sizeof(g_job.current), "%s", v);
  pthread_mutex_unlock(&g_lock);
}

#ifdef FM_TEST_HOOK
void FM_TEST_HOOK(void);                          /* host test only: may slow the job down to cancel it midway */
#endif

static void
job_add(uint64_t bytes, uint64_t files) {
#ifdef FM_TEST_HOOK
  FM_TEST_HOOK();
#endif
  pthread_mutex_lock(&g_lock);
  g_job.bytes_done += bytes;
  g_job.files_done += files;
  pthread_mutex_unlock(&g_lock);
}

static void
job_state(int s) {
  pthread_mutex_lock(&g_lock);
  g_job.state = s;
  pthread_mutex_unlock(&g_lock);
}

static int
other_job_active(char *err, size_t n) {
  if(ps5tm_gamecopy_busy() || ps5tm_gameconvert_busy() || ps5tm_gamemove_busy() || ps5tm_saves_busy() ||
     ps5tm_pkgsplit_busy() || ps5tm_pkginst_busy() || ps5tm_gamedelete_busy()) {
    snprintf(err, n, "Es läuft gerade ein anderer Vorgang der App (Kopieren, Konvertieren, Verschieben, Spielstände, "
             "Paket teilen oder installieren, Löschen). Bitte danach noch einmal.");
    return 1;
  }
  return 0;
}

typedef struct {
  uint64_t files, bytes, seen;
  int      err;
  uint64_t dirs;                      /* folders below (and the start itself, when it is one) */
  uint64_t deadline_ms;               /* 0: none; else counting stops there, partial = 1 (the numbers are a lower bound) */
  int      partial;
} count_t;

/* Files and bytes below real (not following links, not entering another drive). */
static void
count_tree(const char *real, dev_t dev, int depth, count_t *c) {
  if(c->err) return;
  if(depth > FM_DEPTH) { c->err = ELOOP; return; }
  if(++c->seen > FM_COUNT_MAX) { c->err = E2BIG; return; }
  if(c->deadline_ms && (c->seen & 255) == 0 && ps5tm_mono_ms() > c->deadline_ms) { c->partial = 1; c->err = ETIMEDOUT; return; }
  struct stat st;
  if(lstat(real, &st) != 0) { c->err = errno; return; }
  if(S_ISREG(st.st_mode)) { c->files++; c->bytes += (uint64_t)st.st_size; return; }
  if(!S_ISDIR(st.st_mode) || st.st_dev != dev) { c->files++; return; }
  c->dirs++;
  DIR *d = opendir(real);
  if(!d) { c->err = errno; return; }
  struct dirent *e;
  char child[FM_PATH + 64];
  while(!c->err && (e = readdir(d)) != NULL) {
    if(!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
    if(snprintf(child, sizeof(child), "%s/%s", real, e->d_name) >= (int)sizeof(child)) { c->err = ENAMETOOLONG; break; }
    count_tree(child, dev, depth + 1, c);
  }
  closedir(d);
}


/* ------------------------------------------------------------------ view and size (07.10.2026) */

/* Looking, like the download, is allowed everywhere. A picture is sent as itself (types by extension only: never
   SVG, which can carry script), a text as plain text, cut at max bytes. Everything is "inline" and sealed with a
   header that lets the browser do nothing with it but show it. A file that is no text (a NUL byte or too many
   control characters in its first 8 KB) is refused with 415, so a binary never lands in the page as garbage. */
#define FM_VIEW_TEXT_DEFAULT  (256u * 1024)
#define FM_VIEW_TEXT_MAX      (1024u * 1024)
#define FM_VIEW_IMAGE_MAX     (16ull << 20)

static const char *
image_type(const char *name) {
  const char *dot = strrchr(name, '.');
  if(!dot) return NULL;
  static const struct { const char *ext, *type; } t[] = {
    { ".png", "image/png" }, { ".jpg", "image/jpeg" }, { ".jpeg", "image/jpeg" }, { ".gif", "image/gif" },
    { ".webp", "image/webp" }, { ".bmp", "image/bmp" }, { ".ico", "image/x-icon" },
  };
  for(size_t i = 0; i < sizeof(t) / sizeof(t[0]); i++)
    if(!strcasecmp(dot, t[i].ext)) return t[i].type;
  return NULL;
}

static int
looks_like_text(const unsigned char *b, size_t n) {
  size_t ctl = 0;
  for(size_t i = 0; i < n; i++) {
    if(b[i] == 0) return 0;
    if(b[i] < 0x20 && b[i] != '\n' && b[i] != '\r' && b[i] != '\t' && b[i] != '\f' && b[i] != 0x1b) ctl++;
  }
  return n == 0 || ctl * 10 < n;
}

static void
handle_view(int fd, const ps5tm_request_t *req) {
  char p[FM_PATH], real[FM_PATH + 64], num[24];
  if(query_value(req->query, "path", p, sizeof(p)) != 0 || !path_plain(p) || !strcmp(p, "/") ||
     real_path(p, real, sizeof(real)) != 0) {
    ps5tm_http_send_error(fd, 400, "bad_path", "Ungültiger Pfad.");
    return;
  }
  size_t max = FM_VIEW_TEXT_DEFAULT;
  if(query_value(req->query, "max", num, sizeof(num)) == 0 && num[0]) {
    char *end = NULL;
    unsigned long v = strtoul(num, &end, 10);
    if(!end || *end || v == 0) { ps5tm_http_send_error(fd, 400, "bad_max", "Ungültige Länge."); return; }
    max = v > FM_VIEW_TEXT_MAX ? FM_VIEW_TEXT_MAX : (size_t)v;
  }
  int in = open(real, O_RDONLY | O_NONBLOCK);
  struct stat st;
  if(in < 0 || fstat(in, &st) != 0 || !S_ISREG(st.st_mode)) {
    if(in >= 0) close(in);
    ps5tm_http_send_error(fd, 404, "not_a_file", "Das ist keine Datei, die sich ansehen lässt.");
    return;
  }
  const char *img = image_type(base_name(p));
  size_t send_n;
  if(img) {
    if((uint64_t)st.st_size > FM_VIEW_IMAGE_MAX) {
      close(in);
      ps5tm_http_send_error(fd, 413, "too_big", "Das Bild ist zu groß für die Vorschau (über 16 MB).");
      return;
    }
    send_n = (size_t)st.st_size;
  } else {
    unsigned char probe[8192];
    ssize_t k = read(in, probe, sizeof(probe));
    if(k < 0 || !looks_like_text(probe, (size_t)k)) {
      close(in);
      ps5tm_http_send_error(fd, 415, "not_text", "Das ist keine Textdatei; sie lässt sich nur herunterladen.");
      return;
    }
    if(lseek(in, 0, SEEK_SET) < 0) { close(in); ps5tm_http_send_error(fd, 500, "seek_failed", "Lesen ging nicht."); return; }
    send_n = (uint64_t)st.st_size > max ? max : (size_t)st.st_size;
  }
  char head[640];
  int n = snprintf(head, sizeof(head),
                   "HTTP/1.1 200 OK\r\n"
                   "Content-Type: %s\r\n"
                   "Content-Length: %zu\r\n"
                   "Content-Disposition: inline\r\n"
                   "Cache-Control: no-store\r\n"
                   "X-Content-Type-Options: nosniff\r\n"
                   "Content-Security-Policy: default-src 'none'; img-src 'self'; style-src 'unsafe-inline'; sandbox\r\n"
                   "X-Fm-Size: %lld\r\n"
                   "X-Fm-Truncated: %d\r\n"
                   "Connection: close\r\n\r\n",
                   img ? img : "text/plain; charset=utf-8", send_n, (long long)st.st_size, send_n < (size_t)st.st_size);
  if(n < 0 || n >= (int)sizeof(head)) { close(in); ps5tm_http_send_error(fd, 500, "head_failed", "Antwort zu lang."); return; }
  struct timeval tv = { 30, 0 };
  setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
  char *buf = malloc(128 * 1024);
  if(!buf) { close(in); ps5tm_http_send_error(fd, 500, "alloc_failed", "Kein Speicher."); return; }
  int ok = write(fd, head, (size_t)n) == n;
  size_t left = send_n;
  while(ok && left > 0) {
    ssize_t r = read(in, buf, left < 128 * 1024 ? left : 128 * 1024);
    if(r <= 0) break;
    ssize_t off = 0;
    while(off < r) {
      ssize_t w = write(fd, buf + off, (size_t)(r - off));
      if(w < 0 && errno == EINTR) continue;
      if(w <= 0) { ok = 0; break; }
      off += w;
    }
    left -= (size_t)r;
  }
  free(buf);
  close(in);
}

/* The size of a file or a folder with everything below it, counted for at most FM_SIZE_BUDGET_MS: a folder too big
   for that comes back with partial=true and the numbers counted so far (a lower bound). Links are not followed and
   another drive below is not entered (as when copying and deleting). */
#ifndef FM_SIZE_BUDGET_MS
#define FM_SIZE_BUDGET_MS 8000
#endif

static void
handle_size(int fd, const ps5tm_request_t *req) {
  char p[FM_PATH], real[FM_PATH + 64];
  if(query_value(req->query, "path", p, sizeof(p)) != 0 || !path_plain(p) || !strcmp(p, "/") ||
     real_path(p, real, sizeof(real)) != 0) {
    ps5tm_http_send_error(fd, 400, "bad_path", "Ungültiger Pfad.");
    return;
  }
  struct stat st;
  if(lstat(real, &st) != 0) { ps5tm_http_send_error(fd, 404, "not_found", "Das gibt es nicht (mehr)."); return; }
  count_t c = { 0 };
  c.deadline_ms = ps5tm_mono_ms() + FM_SIZE_BUDGET_MS;
  count_tree(real, st.st_dev, 0, &c);
  if(c.err && !c.partial) {
    char msg[300];
    snprintf(msg, sizeof(msg), "Die Größe ließ sich nicht ermitteln: %s", io_words(c.err));
    ps5tm_http_send_error(fd, c.err == E2BIG ? 413 : 500, "size_failed", msg);
    return;
  }
  cJSON *o = cJSON_CreateObject();
  cJSON_AddBoolToObject(o, "ok", 1);
  cJSON_AddStringToObject(o, "path", p);
  cJSON_AddNumberToObject(o, "bytes", (double)c.bytes);
  cJSON_AddNumberToObject(o, "files", (double)c.files);
  cJSON_AddNumberToObject(o, "folders", (double)(c.dirs > 0 ? c.dirs - 1 : 0));   /* without the start itself */
  cJSON_AddBoolToObject(o, "partial", c.partial);
  send_obj(fd, 200, o);
}


/* ------------------------------------------------------------------ deleting */

/* A big file, shortened from its end in steps (each counted), before it is removed: the bar moves while a single
   image of many gigabytes is freed. Only a file with one name, still the one looked at. Returns the bytes cut off
   (and counted); the rest counts when the file goes. */
static uint64_t
shrink_big(const char *real, const struct stat *st) {
  if(st->st_nlink != 1 || st->st_size <= (off_t)FM_TRUNC_STEP) return 0;
  int fd = open(real, O_WRONLY | O_NOFOLLOW | O_NONBLOCK);
  if(fd < 0) return 0;
  uint64_t cut = 0;
  struct stat fs;
  if(fstat(fd, &fs) == 0 && fs.st_dev == st->st_dev && fs.st_ino == st->st_ino && S_ISREG(fs.st_mode)) {
    off_t left = fs.st_size;
    while(left > (off_t)FM_TRUNC_STEP && !cancelled()) {
      left -= (off_t)FM_TRUNC_STEP;
      if(ftruncate(fd, left) != 0) break;
      cut += (uint64_t)FM_TRUNC_STEP;
      job_add((uint64_t)FM_TRUNC_STEP, 0);
    }
  }
  close(fd);
  return cut;
}

/* Removes real and all below it; vpath is what the page shows. 0 or an errno. live: this is the job's own deleting
   (counted, and Abbrechen stops it); not live: tidying up after a copy, which must finish even after Abbrechen. */
static int
delete_tree(const char *real, const char *vpath, dev_t dev, int depth, int live) {
  if(live && cancelled()) return ECANCELED;
  if(depth > FM_DEPTH) return ELOOP;
  struct stat st;
  if(lstat(real, &st) != 0) return errno;
  if(S_ISDIR(st.st_mode)) {
    if(st.st_dev != dev) return EXDEV;
    DIR *d = opendir(real);
    if(!d) return errno;
    struct dirent *e;
    char child[FM_PATH + 64], vchild[FM_PATH + 64];
    int rc = 0;
    while(!rc && (e = readdir(d)) != NULL) {
      if(!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
      if(snprintf(child, sizeof(child), "%s/%s", real, e->d_name) >= (int)sizeof(child) ||
         snprintf(vchild, sizeof(vchild), "%s/%s", vpath, e->d_name) >= (int)sizeof(vchild)) { rc = ENAMETOOLONG; break; }
      rc = delete_tree(child, vchild, dev, depth + 1, live);
    }
    closedir(d);
    if(rc) return rc;
    return rmdir(real) == 0 ? 0 : errno;
  }
  if(live) job_current(vpath);
  uint64_t cut = 0;
  if(live && S_ISREG(st.st_mode)) {
    cut = shrink_big(real, &st);
    if(cancelled()) return ECANCELED;               /* the file stays, shortened: it was being deleted anyway */
  }
  if(unlink(real) != 0) return errno;
  if(live) job_add(S_ISREG(st.st_mode) && (uint64_t)st.st_size > cut ? (uint64_t)st.st_size - cut : 0, 1);
  return 0;
}


/* ------------------------------------------------------------------ copying */

static char *g_buf;                               /* the job's buffer, FM_BUF */

static int
copy_file(const char *rs, const char *rt, const char *vs) {
  job_current(vs);
  int in = open(rs, O_RDONLY | O_NONBLOCK);
  if(in < 0) return errno;
  int out = open(rt, O_WRONLY | O_CREAT | O_EXCL, 0666);
  if(out < 0) { int e = errno; close(in); return e; }
  int rc = 0;
  for(;;) {
    if(cancelled()) { rc = ECANCELED; break; }
    ssize_t r = read(in, g_buf, FM_BUF);
    if(r < 0 && errno == EINTR) continue;
    if(r < 0) { rc = errno; break; }
    if(r == 0) break;
    ssize_t off = 0;
    while(off < r) {
      ssize_t k = write(out, g_buf + off, (size_t)(r - off));
      if(k < 0 && errno == EINTR) continue;
      if(k <= 0) { rc = errno ? errno : EIO; break; }
      off += k;
    }
    if(rc) break;
    job_add((uint64_t)r, 0);
  }
  if(close(out) != 0 && !rc) rc = errno ? errno : EIO;
  close(in);
  if(!rc) job_add(0, 1);
  return rc;
}

static int
copy_tree(const char *rs, const char *rt, const char *vs, dev_t dev, int depth) {
  if(cancelled()) return ECANCELED;
  if(depth > FM_DEPTH) return ELOOP;
  struct stat st;
  if(lstat(rs, &st) != 0) return errno;
  if(S_ISREG(st.st_mode)) return copy_file(rs, rt, vs);
  if(!S_ISDIR(st.st_mode) || st.st_dev != dev) {     /* links, devices, other drives inside: left out, and said so */
    pthread_mutex_lock(&g_lock);
    g_job.skipped++;
    g_job.files_done++;
    pthread_mutex_unlock(&g_lock);
    return 0;
  }
  if(mkdir(rt, 0777) != 0) return errno;
  DIR *d = opendir(rs);
  if(!d) return errno;
  struct dirent *e;
  char cs[FM_PATH + 64], ct[FM_PATH + 64], cv[FM_PATH + 64];
  int rc = 0;
  while(!rc && (e = readdir(d)) != NULL) {
    if(!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
    if(snprintf(cs, sizeof(cs), "%s/%s", rs, e->d_name) >= (int)sizeof(cs) ||
       snprintf(ct, sizeof(ct), "%s/%s", rt, e->d_name) >= (int)sizeof(ct) ||
       snprintf(cv, sizeof(cv), "%s/%s", vs, e->d_name) >= (int)sizeof(cv)) { rc = ENAMETOOLONG; break; }
    rc = copy_tree(cs, ct, cv, dev, depth + 1);
  }
  closedir(d);
  return rc;
}


/* ------------------------------------------------------------------ the job */

static void
job_finish(int state, const char *error, const char *note) {
  pthread_mutex_lock(&g_lock);
  g_job.state = state;
  snprintf(g_job.error, sizeof(g_job.error), "%s", error ? error : "");
  snprintf(g_job.note, sizeof(g_job.note), "%s", note ? note : "");
  g_job.current[0] = 0;
  g_job.finished_ms = ps5tm_mono_ms();
  pthread_mutex_unlock(&g_lock);
}

static void
run_delete(fm_args_t *a) {
  job_state(FM_DELETING);
  char real[FM_PATH + 64], err[800] = "";
  for(unsigned i = 0; i < a->n; i++) {
    struct stat st;
    if(real_path(a->src[i], real, sizeof(real)) != 0 || lstat(real, &st) != 0) continue;   /* gone already */
    int rc = delete_tree(real, a->src[i], st.st_dev, 0, 1);
    if(rc) {
      if(rc == ECANCELED) {
        job_finish(FM_CANCELLED, NULL, "Abgebrochen. Was bis dahin gelöscht war, ist weg; der Rest ist noch da.");
        PS5TM_INFO("files_delete_cancelled", "Dateien: Löschen abgebrochen bei %.120s", a->src[i]);
        return;
      }
      snprintf(err, sizeof(err), "Beim Löschen von %.300s: %s", a->src[i],
               rc == EXDEV ? "Ein Teil liegt auf einem anderen Laufwerk und wurde nicht angefasst." : io_words(rc));
      PS5TM_WARN("files_delete_failed", "Dateien: %.170s", err);
      job_finish(FM_FAILED, err, NULL);
      return;
    }
    pthread_mutex_lock(&g_lock);
    g_job.items_done++;
    pthread_mutex_unlock(&g_lock);
  }
  pthread_mutex_lock(&g_lock);
  uint64_t files = g_job.files_done, bytes = g_job.bytes_done;
  pthread_mutex_unlock(&g_lock);
  PS5TM_INFO("files_delete_done", "Dateien: %u Einträge gelöscht (%llu Dateien, %llu MB).", a->n,
             (unsigned long long)files, (unsigned long long)(bytes >> 20));
  job_finish(FM_DONE, NULL, NULL);
}

static void
run_copy(fm_args_t *a, int move) {
  job_state(FM_COPYING);
  char rd[FM_PATH + 64], rs[FM_PATH + 64], rt[FM_PATH + 64], t[FM_PATH], err[800] = "";
  struct stat ds;
  if(real_path(a->dest, rd, sizeof(rd)) != 0 || stat(rd, &ds) != 0) {
    job_finish(FM_FAILED, "Der Zielordner ist nicht mehr da.", NULL);
    return;
  }
  for(unsigned i = 0; i < a->n; i++) {
    struct stat ss;
    if(real_path(a->src[i], rs, sizeof(rs)) != 0 || join(a->dest, base_name(a->src[i]), t, sizeof(t)) != 0 ||
       real_path(t, rt, sizeof(rt)) != 0 || lstat(rs, &ss) != 0) {
      snprintf(err, sizeof(err), "%.300s ist nicht mehr da.", a->src[i]);
      job_finish(FM_FAILED, err, NULL);
      return;
    }
    struct stat ts;
    if(lstat(rt, &ts) == 0) {
      snprintf(err, sizeof(err), "Im Ziel gibt es inzwischen schon „%.200s“. Nichts wurde überschrieben.", base_name(t));
      job_finish(FM_FAILED, err, NULL);
      return;
    }
    int rc;
    if(move && ss.st_dev == ds.st_dev) {
      job_current(a->src[i]);
      rc = rename(rs, rt) == 0 ? 0 : errno;
      if(!rc) {
        count_t c = { 0 };
        count_tree(rt, ss.st_dev, 0, &c);
        job_add(c.bytes, c.files);
      }
    } else {
      rc = copy_tree(rs, rt, a->src[i], ss.st_dev, 0);
      if(rc) {
        struct stat ps;                           /* what this item left behind is its own: it goes again */
        if(lstat(rt, &ps) == 0) delete_tree(rt, t, ps.st_dev, 0, 0);
      } else if(move) {
        int drc = delete_tree(rs, a->src[i], ss.st_dev, 0, 0);
        if(drc) {
          snprintf(err, sizeof(err), "%.250s wurde kopiert, aber das Original ließ sich nicht ganz löschen: %s. Es liegt "
                   "jetzt an beiden Orten (das Original vielleicht nur noch zum Teil).", a->src[i], io_words(drc));
          PS5TM_WARN("files_move_failed", "Dateien: %.170s", err);
          job_finish(FM_FAILED, err, NULL);
          return;
        }
      }
    }
    if(rc) {
      if(rc == ECANCELED) {
        char note[400];
        snprintf(note, sizeof(note), "Abgebrochen. %u von %u Einträgen sind fertig; der angefangene wurde im Ziel wieder entfernt.",
                 i, a->n);
        job_finish(FM_CANCELLED, NULL, note);
        PS5TM_INFO("files_copy_cancelled", "Dateien: %s abgebrochen bei %.120s", move ? "Verschieben" : "Kopieren", a->src[i]);
        return;
      }
      snprintf(err, sizeof(err), "Bei %.300s: %s%s", a->src[i], io_words(rc),
               i ? " Die Einträge davor sind fertig." : "");
      PS5TM_WARN("files_copy_failed", "Dateien: %.170s", err);
      job_finish(FM_FAILED, err, NULL);
      return;
    }
    pthread_mutex_lock(&g_lock);
    g_job.items_done++;
    pthread_mutex_unlock(&g_lock);
  }
  char note[400] = "";
  pthread_mutex_lock(&g_lock);
  unsigned skipped = g_job.skipped;
  uint64_t done = g_job.bytes_done;
  pthread_mutex_unlock(&g_lock);
  if(skipped)
    snprintf(note, sizeof(note), "%u Einträge wurden ausgelassen (Verknüpfungen, Gerätedateien oder ein anderes Laufwerk darin).", skipped);
  PS5TM_INFO("files_copy_done", "Dateien: %u Einträge %s nach %.100s (%llu MB).", a->n, move ? "verschoben" : "kopiert",
             a->dest, (unsigned long long)(done >> 20));
  job_finish(FM_DONE, NULL, note);
}

static void *
job_thread(void *arg) {
  fm_args_t *a = arg;
  ps5tm_powerguard_hold();
  if(!strcmp(a->kind, "delete")) run_delete(a);
  else run_copy(a, !strcmp(a->kind, "move"));
  ps5tm_powerguard_release();
  free(g_buf);
  g_buf = NULL;
  free(a->src);
  free(a);
  return NULL;
}

/* The paths of a body's "paths": plain, distinct, at most FM_SEL_MAX; none inside another one of them. */
static int
read_paths(const cJSON *b, fm_args_t *a, char *err, size_t n) {
  const cJSON *arr = cJSON_GetObjectItem(b, "paths");
  int cnt = cJSON_IsArray(arr) ? cJSON_GetArraySize(arr) : 0;
  if(cnt <= 0) { snprintf(err, n, "Es ist nichts ausgewählt."); return 400; }
  if(cnt > FM_SEL_MAX) { snprintf(err, n, "Höchstens %d Einträge auf einmal.", FM_SEL_MAX); return 400; }
  a->src = calloc((size_t)cnt, FM_PATH);
  if(!a->src) { snprintf(err, n, "Kein Speicher."); return 500; }
  for(int i = 0; i < cnt; i++) {
    const cJSON *it = cJSON_GetArrayItem(arr, i);
    if(!cJSON_IsString(it) || !path_plain(it->valuestring) || !strcmp(it->valuestring, "/")) {
      snprintf(err, n, "Ein ausgewählter Pfad ist ungültig.");
      return 400;
    }
    snprintf(a->src[i], FM_PATH, "%s", it->valuestring);
    for(int k = 0; k < i; k++) {
      size_t l = strlen(a->src[k]), m = strlen(a->src[i]);
      if(!strcmp(a->src[k], a->src[i]) || (m > l && !strncmp(a->src[i], a->src[k], l) && a->src[i][l] == '/') ||
         (l > m && !strncmp(a->src[k], a->src[i], m) && a->src[k][m] == '/')) {
        snprintf(err, n, "Ein Eintrag ist doppelt oder liegt in einem anderen ausgewählten.");
        return 400;
      }
    }
  }
  a->n = (unsigned)cnt;
  return 0;
}

/* Starts the job a describes (taken over, freed by the job). 200, or an error with the reason. */
static int
start_job(fm_args_t *a, uint64_t bytes_total, uint64_t files_total, char *err, size_t n) {
  /* the claim and the check are one step: two requests in the same moment cannot both start */
  pthread_mutex_lock(&g_lock);
  int busy = g_job.state == FM_COUNTING || g_job.state == FM_COPYING || g_job.state == FM_DELETING;
  fm_job_t before = g_job;
  if(!busy) g_job.state = FM_COUNTING;
  pthread_mutex_unlock(&g_lock);
  if(busy) { snprintf(err, n, "Der Dateimanager ist gerade schon beschäftigt."); return 409; }
  int other = other_job_active(err, n);
  char *buf = !other && strcmp(a->kind, "delete") ? malloc(FM_BUF) : NULL;
  if(other || (strcmp(a->kind, "delete") && !buf)) {
    if(!other) snprintf(err, n, "Kein Speicher.");
    pthread_mutex_lock(&g_lock);
    g_job = before;                                 /* the last job's result stays on show */
    pthread_mutex_unlock(&g_lock);
    return other ? 409 : 500;
  }
  g_buf = buf;
  pthread_mutex_lock(&g_lock);
  memset(&g_job, 0, sizeof(g_job));
  g_job.state = FM_COUNTING;
  snprintf(g_job.kind, sizeof(g_job.kind), "%s", a->kind);
  snprintf(g_job.dest, sizeof(g_job.dest), "%s", a->dest);
  g_job.bytes_total = bytes_total;
  g_job.files_total = files_total;
  g_job.items = a->n;
  g_job.started_ms = ps5tm_mono_ms();
  __atomic_store_n(&g_cancel, 0, __ATOMIC_RELEASE);
  pthread_mutex_unlock(&g_lock);
  PS5TM_INFO("files_job_start", "Dateien: %s beginnt: %u Einträge, %llu MB%s%.100s.", a->kind, a->n,
             (unsigned long long)(bytes_total >> 20), a->dest[0] ? " nach " : "", a->dest);
  pthread_t th;
  pthread_attr_t at;
  pthread_attr_init(&at);
  pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
  int rc = pthread_create(&th, &at, job_thread, a);
  pthread_attr_destroy(&at);
  if(rc != 0) {
    free(g_buf);
    g_buf = NULL;
    job_finish(FM_FAILED, "Der Vorgang ließ sich nicht starten.", NULL);
    snprintf(err, n, "Der Vorgang ließ sich nicht starten.");
    return 500;
  }
  return 200;
}

/* POST copy/move {paths, dest} */
static void
handle_copy(int fd, const ps5tm_request_t *req, int move) {
  cJSON *b = parse_body(req);
  fm_args_t *a = calloc(1, sizeof(*a));
  char err[800] = "";
  int http = a ? 0 : 500;
  if(!a) snprintf(err, sizeof(err), "Kein Speicher.");
  const char *dest = b ? jstr(b, "dest") : NULL;
  if(!http) {
    snprintf(a->kind, sizeof(a->kind), "%s", move ? "move" : "copy");
    if(!dest || !path_plain(dest)) { http = 400; snprintf(err, sizeof(err), "Ungültiger Zielordner."); }
    else { snprintf(a->dest, sizeof(a->dest), "%s", dest); http = read_paths(b, a, err, sizeof(err)); }
  }
  cJSON_Delete(b);
  char rd[FM_PATH + 64], rs[FM_PATH + 64], t[FM_PATH], rt[FM_PATH + 64];
  struct stat ds;
  if(!http && (!dir_writable(a->dest) || real_path(a->dest, rd, sizeof(rd)) != 0 || stat(rd, &ds) != 0 || !S_ISDIR(ds.st_mode))) {
    http = 403;
    snprintf(err, sizeof(err), "In diesen Ordner darf nichts %s werden (nur auf die Laufwerke und nach /data).", move ? "verschoben" : "kopiert");
  }
  uint64_t bytes = 0, files = 0;
  cJSON *conflicts = cJSON_CreateArray();
  for(unsigned i = 0; !http && i < a->n; i++) {
    struct stat ss;
    size_t sl = strlen(a->src[i]);
    if(real_path(a->src[i], rs, sizeof(rs)) != 0 || lstat(rs, &ss) != 0) {
      http = 404; snprintf(err, sizeof(err), "%.300s gibt es nicht (mehr).", a->src[i]); break;
    }
    if(S_ISLNK(ss.st_mode)) { http = 400; snprintf(err, sizeof(err), "%.300s ist eine Verknüpfung; die wird nicht kopiert.", a->src[i]); break; }
    if(move && !writable(a->src[i])) {
      http = 403; snprintf(err, sizeof(err), "%.300s darf nicht verschoben werden (nur von den Laufwerken und aus /data).", a->src[i]); break;
    }
    if(!strncmp(a->dest, a->src[i], sl) && (a->dest[sl] == 0 || a->dest[sl] == '/')) {
      http = 400; snprintf(err, sizeof(err), "Ein Ordner kann nicht in sich selbst %s werden.", move ? "verschoben" : "kopiert"); break;
    }
    if(join(a->dest, base_name(a->src[i]), t, sizeof(t)) != 0 || !writable(t) || real_path(t, rt, sizeof(rt)) != 0) {
      http = 403; snprintf(err, sizeof(err), "Dorthin darf „%.200s“ nicht.", base_name(a->src[i])); break;
    }
    struct stat ts;
    if(lstat(rt, &ts) == 0) { cJSON_AddItemToArray(conflicts, cJSON_CreateString(base_name(a->src[i]))); continue; }
    if(!move || ss.st_dev != ds.st_dev) {
      count_t c = { 0 };
      count_tree(rs, ss.st_dev, 0, &c);
      if(c.err) { http = 500; snprintf(err, sizeof(err), "%.300s ließ sich nicht durchzählen: %s", a->src[i], io_words(c.err)); break; }
      bytes += c.bytes;
      files += c.files;
    } else {
      count_t c = { 0 };
      count_tree(rs, ss.st_dev, 0, &c);           /* only for the bar; a rename takes no room */
      files += c.files;
      bytes += c.err ? 0 : c.bytes;
    }
  }
  if(!http && cJSON_GetArraySize(conflicts) > 0) {
    http = 409;
    snprintf(err, sizeof(err), "Im Ziel gibt es schon Einträge mit diesen Namen; nichts wurde überschrieben.");
  }
  uint64_t fr = 0;
  int needs_room = 0;
  for(unsigned i = 0; !http && i < a->n; i++) {
    struct stat ss;
    if(real_path(a->src[i], rs, sizeof(rs)) == 0 && lstat(rs, &ss) == 0 && (!move || ss.st_dev != ds.st_dev)) needs_room = 1;
  }
  if(!http && needs_room && space_free(a->dest, &fr) == 0 && fr < bytes + FM_SPACE_SPARE) {
    http = 409;
    snprintf(err, sizeof(err), "Im Ziel ist nicht genug Platz: gebraucht %llu MB, frei %llu MB.",
             (unsigned long long)(bytes >> 20), (unsigned long long)(fr >> 20));
  }
  if(!http) http = start_job(a, bytes, files, err, sizeof(err));
  if(http != 200) {
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "ok", 0);
    cJSON_AddStringToObject(o, "code", http == 409 && cJSON_GetArraySize(conflicts) ? "exists" : "refused");
    cJSON_AddStringToObject(o, "message", err);
    cJSON_AddItemToObject(o, "conflicts", conflicts);
    if(a) { free(a->src); free(a); }
    send_obj(fd, http ? http : 500, o);
    return;
  }
  cJSON_Delete(conflicts);
  ps5tm_http_send_json(fd, 200, "{\"ok\":true}");
}

static void
make_token(char out[33]) {
  unsigned char r[16];
  int fd = open("/dev/urandom", O_RDONLY);
  ssize_t got = fd >= 0 ? read(fd, r, sizeof(r)) : -1;
  if(fd >= 0) close(fd);
  if(got != (ssize_t)sizeof(r)) {
    uint64_t t = ps5tm_mono_ms() * 6364136223846793005ull + (uint64_t)getpid() + (uint64_t)(uintptr_t)out;
    for(size_t i = 0; i < sizeof(r); i++) { t = t * 6364136223846793005ull + 1442695040888963407ull; r[i] = (unsigned char)(t >> 33); }
  }
  static const char hex[] = "0123456789abcdef";
  for(size_t i = 0; i < sizeof(r); i++) { out[2 * i] = hex[r[i] >> 4]; out[2 * i + 1] = hex[r[i] & 15]; }
  out[32] = 0;
}

static char *
joined(const fm_args_t *a) {
  size_t len = 1;
  for(unsigned i = 0; i < a->n; i++) len += strlen(a->src[i]) + 1;
  char *s = malloc(len), *p = s;
  if(!s) return NULL;
  for(unsigned i = 0; i < a->n; i++) p += sprintf(p, "%s\n", a->src[i]);
  *p = 0;
  return s;
}

/* POST delete/plan {paths}: what goes, and the token to start it with */
static void
handle_delete_plan(int fd, const ps5tm_request_t *req) {
  cJSON *b = parse_body(req);
  fm_args_t a;
  memset(&a, 0, sizeof(a));
  char err[800] = "";
  int http = read_paths(b, &a, err, sizeof(err));
  cJSON_Delete(b);
  uint64_t bytes = 0, files = 0, dirs = 0;
  char rs[FM_PATH + 64];
  for(unsigned i = 0; !http && i < a.n; i++) {
    struct stat st;
    if(!writable(a.src[i])) {
      http = 403;
      snprintf(err, sizeof(err), "%.300s darf nicht gelöscht werden (nur auf den Laufwerken und in /data, keine Verknüpfungen).", a.src[i]);
      break;
    }
    if(real_path(a.src[i], rs, sizeof(rs)) != 0 || lstat(rs, &st) != 0) {
      http = 404; snprintf(err, sizeof(err), "%.300s gibt es nicht (mehr).", a.src[i]); break;
    }
    if(S_ISDIR(st.st_mode)) dirs++;
    count_t c = { 0 };
    count_tree(rs, st.st_dev, 0, &c);
    files += c.files;
    bytes += c.bytes;
    if(c.err == E2BIG) { http = 400; snprintf(err, sizeof(err), "%.300s enthält zu viele Dateien für einen Vorgang.", a.src[i]); break; }
  }
  if(http) {
    free(a.src);
    ps5tm_http_send_error(fd, http, "refused", err);
    return;
  }
  char *list = joined(&a);
  cJSON *o = cJSON_CreateObject();
  cJSON_AddBoolToObject(o, "ok", 1);
  cJSON_AddNumberToObject(o, "items", a.n);
  cJSON_AddNumberToObject(o, "folders", (double)dirs);
  cJSON_AddNumberToObject(o, "files", (double)files);
  cJSON_AddNumberToObject(o, "bytes", (double)bytes);
  cJSON *arr = cJSON_AddArrayToObject(o, "paths");
  for(unsigned i = 0; i < a.n; i++) cJSON_AddItemToArray(arr, cJSON_CreateString(a.src[i]));
  pthread_mutex_lock(&g_lock);
  free(g_tok.list);
  g_tok.list = list;
  make_token(g_tok.token);
  g_tok.until_ms = ps5tm_mono_ms() + FM_TOKEN_MS;
  if(list) cJSON_AddStringToObject(o, "confirm", g_tok.token);
  pthread_mutex_unlock(&g_lock);
  free(a.src);
  send_obj(fd, 200, o);
}

/* POST delete {paths, confirm} */
static void
handle_delete(int fd, const ps5tm_request_t *req) {
  cJSON *b = parse_body(req);
  fm_args_t *a = calloc(1, sizeof(*a));
  char err[800] = "";
  int http = a ? read_paths(b, a, err, sizeof(err)) : 500;
  const char *tok = b ? jstr(b, "confirm") : NULL;
  char token[40] = "";
  if(tok) snprintf(token, sizeof(token), "%s", tok);
  cJSON_Delete(b);
  if(!http) {
    snprintf(a->kind, sizeof(a->kind), "delete");
    char *list = joined(a);
    pthread_mutex_lock(&g_lock);
    int ok = list && g_tok.list && g_tok.token[0] && strlen(token) == 32 && !strcmp(token, g_tok.token) &&
             !strcmp(list, g_tok.list) && ps5tm_mono_ms() < g_tok.until_ms;
    if(ok) { free(g_tok.list); memset(&g_tok, 0, sizeof(g_tok)); }               /* one use */
    pthread_mutex_unlock(&g_lock);
    free(list);
    if(!ok) { http = 403; snprintf(err, sizeof(err), "Die Bestätigung fehlt oder ist abgelaufen. Bitte das Löschen noch einmal öffnen und bestätigen."); }
  }
  uint64_t bytes = 0, files = 0;
  char rs[FM_PATH + 64];
  for(unsigned i = 0; !http && i < a->n; i++) {
    struct stat st;
    if(!writable(a->src[i])) { http = 403; snprintf(err, sizeof(err), "%.300s darf nicht gelöscht werden.", a->src[i]); break; }
    if(real_path(a->src[i], rs, sizeof(rs)) != 0 || lstat(rs, &st) != 0) continue;
    count_t c = { 0 };
    count_tree(rs, st.st_dev, 0, &c);
    bytes += c.bytes;
    files += c.files;
  }
  if(!http) http = start_job(a, bytes, files, err, sizeof(err));     /* 200: the job owns a now */
  if(http != 200) {
    if(a) { free(a->src); free(a); }
    ps5tm_http_send_error(fd, http, "refused", err);
    return;
  }
  ps5tm_http_send_json(fd, 200, "{\"ok\":true}");
}

static cJSON *
job_json(void) {
  pthread_mutex_lock(&g_lock);
  fm_job_t j = g_job;
  pthread_mutex_unlock(&g_lock);
  uint64_t now = ps5tm_mono_ms();
  int active = j.state == FM_COUNTING || j.state == FM_COPYING || j.state == FM_DELETING;
  cJSON *o = cJSON_CreateObject();
  cJSON_AddBoolToObject(o, "ok", 1);
  cJSON_AddStringToObject(o, "state", k_state[j.state]);
  cJSON_AddBoolToObject(o, "active", active);
  cJSON_AddStringToObject(o, "kind", j.kind);
  cJSON_AddStringToObject(o, "dest", j.dest);
  cJSON_AddStringToObject(o, "current", j.current);
  cJSON_AddStringToObject(o, "error", j.error);
  cJSON_AddStringToObject(o, "note", j.note);
  uint64_t done = j.bytes_done < j.bytes_total ? j.bytes_done : j.bytes_total;
  double pct = j.state == FM_DONE ? 100 : j.bytes_total ? 100.0 * (double)done / (double)j.bytes_total :
               j.files_total ? 100.0 * (double)(j.files_done < j.files_total ? j.files_done : j.files_total) / (double)j.files_total : 0;
  if(j.state != FM_DONE && pct > 99) pct = 99;
  cJSON_AddNumberToObject(o, "percent", (double)(int)pct);
  cJSON_AddNumberToObject(o, "bytes_total", (double)j.bytes_total);
  cJSON_AddNumberToObject(o, "bytes_done", (double)done);
  cJSON_AddNumberToObject(o, "files_total", (double)j.files_total);
  cJSON_AddNumberToObject(o, "files_done", (double)j.files_done);
  cJSON_AddNumberToObject(o, "items", j.items);
  cJSON_AddNumberToObject(o, "items_done", j.items_done);
  cJSON_AddNumberToObject(o, "skipped", j.skipped);
  cJSON_AddNumberToObject(o, "elapsed_s", j.started_ms ? (double)(((j.finished_ms ? j.finished_ms : now) - j.started_ms) / 1000) : 0);
  cJSON_AddNumberToObject(o, "finished_ago_s", j.finished_ms ? (double)((now - j.finished_ms) / 1000) : -1);
  return o;
}

int
ps5tm_filemgr_busy(void) {
  pthread_mutex_lock(&g_lock);
  int b = g_job.state == FM_COUNTING || g_job.state == FM_COPYING || g_job.state == FM_DELETING;
  pthread_mutex_unlock(&g_lock);
  return b;
}


/* ------------------------------------------------------------------ the API */

int
ps5tm_filemgr_api(int fd, const ps5tm_request_t *req) {
  const char *p = req->path;
  if(strncmp(p, "/api/v1/files/", 14) && strcmp(p, "/api/v1/files")) return 0;
  int get = !strcmp(req->method, "GET"), post = !strcmp(req->method, "POST");
  struct { const char *path; int post; } routes[] = {
    { "/api/v1/files/places", 0 }, { "/api/v1/files/list", 0 }, { "/api/v1/files/download", 0 },
    { "/api/v1/files/job", 0 }, { "/api/v1/files/mkdir", 1 }, { "/api/v1/files/rename", 1 },
    { "/api/v1/files/copy", 1 }, { "/api/v1/files/move", 1 }, { "/api/v1/files/delete/plan", 1 },
    { "/api/v1/files/delete", 1 }, { "/api/v1/files/job/cancel", 1 },
    { "/api/v1/files/view", 0 }, { "/api/v1/files/size", 0 },
  };
  for(size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
    if(strcmp(p, routes[i].path)) continue;
    if(routes[i].post ? !post : !get) {
      ps5tm_http_send_error(fd, 405, "method_not_allowed", routes[i].post ? "Nur POST erlaubt." : "Nur GET erlaubt.");
      return 1;
    }
    switch(i) {
      case 0: send_obj(fd, 200, places_json()); break;
      case 1: handle_list(fd, req); break;
      case 2: handle_download(fd, req); break;
      case 3: send_obj(fd, 200, job_json()); break;
      case 4: handle_mkdir(fd, req); break;
      case 5: handle_rename(fd, req); break;
      case 6: handle_copy(fd, req, 0); break;
      case 7: handle_copy(fd, req, 1); break;
      case 8: handle_delete_plan(fd, req); break;
      case 9: handle_delete(fd, req); break;
      case 10:
        if(!ps5tm_filemgr_busy()) { ps5tm_http_send_error(fd, 409, "not_running", "Es läuft gerade nichts."); break; }
        __atomic_store_n(&g_cancel, 1, __ATOMIC_RELEASE);
        ps5tm_http_send_json(fd, 200, "{\"ok\":true}");
        break;
      case 11: handle_view(fd, req); break;
      case 12: handle_size(fd, req); break;
    }
    return 1;
  }
  ps5tm_http_send_error(fd, 404, "not_found", "Unbekannte Anfrage.");
  return 1;
}
