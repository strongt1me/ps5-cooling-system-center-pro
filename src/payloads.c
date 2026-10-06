/* Payload files: the folder /data/PS5-Cooling-Center/payloads and the ELF
 * files on USB drives, and starting one of them.
 *
 * Starting a payload on this console means what a person does from a PC:
 * connect to the ELF loader (elfldr, port 9021), write the file, and close the
 * sending side — the loader takes the end of the stream as the end of the
 * file, runs it and answers with whatever the payload prints. That is all
 * start() does, from the console itself, so a payload can be started without
 * a PC and without a network connection to the console.
 *
 * Where files are looked for, and nowhere else:
 *   - /data/PS5-Cooling-Center/payloads   (made when the app starts, if it is
 *                                          not there; an existing one is left
 *                                          alone)
 *   - the root of every USB drive, and the folder "payloads" in that root
 *
 * The page never names a path. It names a place (internal, or a USB mount
 * point from this module's own list of mounts), a folder (the root, or the
 * folder "payloads") and a plain file name, and every one of the three is
 * checked again here before anything is opened. The file itself must be a
 * regular file (never followed through a link), end in ".elf", and begin with
 * the header of a 64-bit x86-64 ELF — a file that does not is listed, marked,
 * and refused: the loader answers a stray file with nothing a person can read.
 *
 * Starting, copying and deleting take one lock, so two clicks (or two tabs)
 * cannot interleave their work. Listing reads USB drives, which can hang on a
 * bad stick: one scan at a time, answers kept for a few seconds, and a request
 * that finds a scan running gets the last answer instead of starting a second
 * scan behind the first. */

#include <dirent.h>
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
#include <sys/statvfs.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#include "ps5tm.h"
#include "third_party/cJSON.h"

#define PL_DIR_NAME   "payloads"
#define PL_DIR        PS5TM_DATA_DIR "/" PL_DIR_NAME

/* The ELF loader. Overridable like PS5TM_DATA_DIR, so the host test can stand a
   fake loader up on another port. */
#ifndef PS5TM_LOADER_PORT
#define PS5TM_LOADER_PORT 9021
#endif

#define PL_NAME_MAX    200                 /* bytes of a file name, with the NUL     */
#define PL_PATH_MAX    512
#define PL_MAX_FILES   64                  /* per folder; more is a mess, not a list */
#define PL_MIN_SIZE    64u                 /* an ELF header                           */
#define PL_MAX_SIZE    (128ull << 20)      /* the largest payload worth sending       */
#define PL_CACHE_MS    3000
/* Two starts of one file closer together than this are a double click. */
#define PL_START_GAP_MS 1500

#define PL_CONNECT_MS  3000
#define PL_SEND_MS     60000
#define PL_REPLY_MS    1500                /* how long to listen for the payload's first words */
#define PL_COPY_MS     120000
#define PL_CHUNK       (64u * 1024)
#define PL_FREE_MARGIN (16ull << 20)       /* kept free on /data after a copy        */

typedef struct {
  char        name[PL_NAME_MAX];
  char        dir[PL_NAME_MAX];            /* "" = the root of the drive             */
  uint64_t    size;
  int64_t     mtime;
  int         valid;
  const char *why;                         /* when not valid: the reason, in German  */
} pl_file_t;

static pthread_mutex_t g_op_lock    = PTHREAD_MUTEX_INITIALIZER;   /* start, copy, delete */
static pthread_mutex_t g_scan_lock  = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_cache_lock = PTHREAD_MUTEX_INITIALIZER;
static char     *g_cache_json;
static uint64_t  g_cache_ms;
static uint64_t  g_last_start_ms;
static char      g_last_start_name[PL_NAME_MAX];


/* ------------------------------------------------------------- results */

static int
fail(ps5tm_payload_result_t *r, int status, const char *code, const char *msg) {
  snprintf(r->code, sizeof(r->code), "%s", code);
  snprintf(r->msg,  sizeof(r->msg),  "%s", msg);
  return status;
}

static void
cache_drop(void) {
  pthread_mutex_lock(&g_cache_lock);
  g_cache_ms = 0;
  pthread_mutex_unlock(&g_cache_lock);
}


/* --------------------------------------------------------------- names */

/* UTF-8 that a page and a JSON string can carry: no stray continuation bytes,
   no overlong forms, no surrogates, no control characters. */
static int
utf8_seq(const unsigned char *p) {            /* length of the sequence at p, 0 = invalid */
  if(*p < 0x80) return 1;
  int n;
  unsigned cp;
  if(*p >= 0xC2 && *p <= 0xDF)      { n = 1; cp = *p & 0x1F; }
  else if(*p >= 0xE0 && *p <= 0xEF) { n = 2; cp = *p & 0x0F; }
  else if(*p >= 0xF0 && *p <= 0xF4) { n = 3; cp = *p & 0x07; }
  else return 0;
  for(int i = 1; i <= n; i++) {
    if((p[i] & 0xC0) != 0x80) return 0;           /* also stops at the NUL */
    cp = (cp << 6) | (p[i] & 0x3F);
  }
  if(n == 2 && (cp < 0x800 || (cp >= 0xD800 && cp <= 0xDFFF))) return 0;
  if(n == 3 && (cp < 0x10000 || cp > 0x10FFFF)) return 0;
  return n + 1;
}

static int
utf8_ok(const char *s) {
  const unsigned char *p = (const unsigned char *)s;
  while(*p) {
    if(*p < 0x20 || *p == 0x7F) return 0;
    int n = utf8_seq(p);
    if(!n) return 0;
    p += n;
  }
  return 1;
}

static int
has_elf_ext(const char *name) {
  size_t n = strlen(name);
  return n > 4 && !strcasecmp(name + n - 4, ".elf");
}

/* A name this module will handle: one path component, no hidden files (the
   "._name.elf" shadows macOS leaves on exFAT drives would otherwise be
   listed), valid UTF-8, ending in ".elf". */
static int
name_ok(const char *name) {
  size_t n = name ? strlen(name) : 0;
  if(n == 0 || n >= PL_NAME_MAX) return 0;
  if(name[0] == '.') return 0;
  if(strchr(name, '/') || strchr(name, '\\')) return 0;
  if(!utf8_ok(name)) return 0;
  return has_elf_ext(name);
}

/* The folder on a drive: the root, or "payloads" however the drive spells it
   (an exFAT stick formatted on a PC often says "Payloads"). */
static int
dir_ok(const char *dir) {
  return dir && (dir[0] == 0 || !strcasecmp(dir, PL_DIR_NAME));
}

/* Text another program printed, made fit for a JSON string and the page:
   valid UTF-8 stays, every other byte becomes '?', control characters other
   than the line break go. */
static void
sanitize(char *dst, size_t dst_len, const unsigned char *src, size_t n) {
  size_t o = 0, i = 0;
  while(i < n && o + 5 < dst_len) {
    unsigned char c = src[i];
    if(c == '\n' || c == '\t') { dst[o++] = (char)c; i++; continue; }
    if(c < 0x20 || c == 0x7F)  { i++; continue; }
    if(c < 0x80)               { dst[o++] = (char)c; i++; continue; }
    unsigned char tmp[5] = { c, 0, 0, 0, 0 };
    int len = (c >= 0xF0) ? 4 : (c >= 0xE0) ? 3 : 2;
    if(i + (size_t)len <= n) memcpy(tmp, src + i, (size_t)len);
    int ok = (i + (size_t)len <= n) ? utf8_seq(tmp) : 0;
    if(ok && ok == len) { memcpy(dst + o, tmp, (size_t)len); o += (size_t)len; i += (size_t)len; }
    else                { dst[o++] = '?'; i++; }
  }
  dst[o] = 0;
}


/* A file name as it goes into a log line. The log is mirrored into the kernel
   message buffer, and other parts of this app read that buffer for markers
   such as "Battery level : " and "[onPSButtonPressed] value=" — without skipping
   this app's own lines. A stick holding a file named "Battery level : 5.elf"
   must not be able to write one of them. So only letters, digits, space and
   ._-+ stay (and UTF-8 text, for names in other scripts); the rest is '?'. */
static const char *
log_name(const char *name, char *out, size_t out_len) {
  size_t o = 0;
  for(const unsigned char *p = (const unsigned char *)name; *p && o + 1 < out_len; p++) {
    int keep = (*p >= '0' && *p <= '9') || (*p >= 'A' && *p <= 'Z') ||
               (*p >= 'a' && *p <= 'z') || *p == ' ' || *p == '.' || *p == '_' ||
               *p == '-' || *p == '+' || *p >= 0x80;
    out[o++] = keep ? (char)*p : '?';
  }
  out[o] = 0;
  return out;
}


/* -------------------------------------------------------------- the ELF */

/* The first 64 bytes of a file. NULL when it is an ELF a loader on this
   console can run: ELF64, little endian, x86-64, an executable or a PIE;
   otherwise the reason, in German. */
static const char *
check_header(int fd, uint64_t size) {
  if(size < PL_MIN_SIZE)  return "zu klein für ein ELF";
  if(size > PL_MAX_SIZE)  return "zu groß (über 128 MB)";
  unsigned char h[64];
  ssize_t n = pread(fd, h, sizeof(h), 0);
  if(n != (ssize_t)sizeof(h)) return "nicht lesbar";
  if(memcmp(h, "\x7f" "ELF", 4) != 0) return "kein ELF (falscher Anfang)";
  if(h[4] != 2 || h[5] != 1)         return "kein 64-Bit-ELF für die PS5";
  unsigned type    = h[16] | (unsigned)(h[17] << 8);
  unsigned machine = h[18] | (unsigned)(h[19] << 8);
  if(machine != 62)                  return "für einen anderen Prozessor gebaut";
  if(type != 2 && type != 3)         return "kein ausführbares ELF";
  return NULL;
}


/* -------------------------------------------------------------- listing */

static int
cmp_file(const void *a, const void *b) {
  const pl_file_t *x = a, *y = b;
  int c = strcasecmp(x->name, y->name);
  return c ? c : strcmp(x->name, y->name);
}

/* The ELF files directly in dir_path (not below it). `dir_label` is what the
   page sends back to name this folder. Returns how many it put into out. */
static unsigned
scan_dir(const char *dir_path, const char *dir_label, pl_file_t *out, unsigned room) {
  DIR *d = opendir(dir_path);
  if(!d) return 0;
  unsigned n = 0;
  struct dirent *de;
  while(n < room && (de = readdir(d)) != NULL) {
    if(!name_ok(de->d_name)) continue;
    char path[PL_PATH_MAX];
    if(snprintf(path, sizeof(path), "%s/%s", dir_path, de->d_name) >= (int)sizeof(path)) continue;
    struct stat st;
    if(lstat(path, &st) != 0 || !S_ISREG(st.st_mode)) continue;   /* never through a link */

    pl_file_t *f = &out[n];
    memset(f, 0, sizeof(*f));
    snprintf(f->name, sizeof(f->name), "%s", de->d_name);
    snprintf(f->dir,  sizeof(f->dir),  "%s", dir_label);
    f->size  = (uint64_t)st.st_size;
    f->mtime = (int64_t)st.st_mtime;

    int fd = open(path, O_RDONLY | O_NOFOLLOW);
    if(fd < 0) {
      f->why = "nicht lesbar";
    } else {
      f->why = check_header(fd, f->size);
      close(fd);
    }
    f->valid = (f->why == NULL);
    n++;
  }
  closedir(d);
  qsort(out, n, sizeof(*out), cmp_file);
  return n;
}

/* Mount points of USB drives, with the label the page shows. */
typedef struct { char mount[40]; char label[40]; } pl_usb_t;

static unsigned
usb_mounts(pl_usb_t *out, unsigned room) {
  unsigned n = 0;
#ifdef PS5TM_HOST_TEST
  /* No USB on a PC: the test names scratch folders instead, "path[,path…]". */
  const char *env = getenv("PS5TM_TEST_USB");
  if(env && *env) {
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", env);
    char *save = NULL;
    for(char *tok = strtok_r(tmp, ",", &save); tok && n < room;
        tok = strtok_r(NULL, ",", &save)) {
      snprintf(out[n].mount, sizeof(out[n].mount), "%s", tok);
      snprintf(out[n].label, sizeof(out[n].label), "USB-Speicher %u", n + 1);
      n++;
    }
  }
#else
  ps5tm_sysinfo_t info;
  ps5tm_sysinfo_get(&info);
  for(unsigned i = 0; i < info.volume_count && n < room; i++) {
    if(strncmp(info.volumes[i].path, "/mnt/usb", 8) != 0) continue;
    snprintf(out[n].mount, sizeof(out[n].mount), "%s", info.volumes[i].path);
    snprintf(out[n].label, sizeof(out[n].label), "%s", info.volumes[i].label);
    n++;
  }
#endif
  return n;
}

static int
usb_known(const char *mount) {
  pl_usb_t usb[8];
  unsigned n = usb_mounts(usb, 8);
  for(unsigned i = 0; i < n; i++)
    if(!strcmp(usb[i].mount, mount)) return 1;
  return 0;
}

static void
add_file(cJSON *arr, const pl_file_t *f) {
  cJSON *e = cJSON_CreateObject();
  cJSON_AddStringToObject(e, "name",  f->name);
  cJSON_AddStringToObject(e, "dir",   f->dir);
  cJSON_AddNumberToObject(e, "size",  (double)f->size);
  cJSON_AddNumberToObject(e, "mtime", (double)f->mtime);
  cJSON_AddBoolToObject  (e, "valid", f->valid);
  if(f->why) cJSON_AddStringToObject(e, "why", f->why);
  cJSON_AddItemToArray(arr, e);
}

/* The folder named "payloads" in the root of a drive, as the drive spells it.
   "" when there is none. */
static void
usb_payload_dir(const char *mount, char *out, size_t out_len) {
  out[0] = 0;
  DIR *d = opendir(mount);
  if(!d) return;
  struct dirent *de;
  while((de = readdir(d)) != NULL) {
    if(strcasecmp(de->d_name, PL_DIR_NAME) != 0) continue;
    char path[PL_PATH_MAX];
    struct stat st;
    if(snprintf(path, sizeof(path), "%s/%s", mount, de->d_name) < (int)sizeof(path) &&
       lstat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
      snprintf(out, out_len, "%s", de->d_name);
      break;
    }
  }
  closedir(d);
}

/* Creates the payload folder when it is not there. An existing folder is left
   exactly as it is — no mkdir, no chmod, no message. A name taken by something
   that is not a folder is reported and left alone too. Called when the app
   starts, and before a copy lands in it. */
void
ps5tm_payloads_init(void) {
  struct stat st;
  if(stat(PL_DIR, &st) == 0) {
    if(!S_ISDIR(st.st_mode)) {
      static int told = 0;
      if(!__atomic_exchange_n(&told, 1, __ATOMIC_RELAXED))
        PS5TM_WARN("payload_dir_blocked",
                   "%s ist kein Ordner, sondern eine Datei — die Seite „Payloads“ "
                   "findet deshalb keine eigenen Payloads. Die Datei umbenennen "
                   "oder löschen, dann legt die App den Ordner beim nächsten "
                   "Start an.", PL_DIR);
    }
    return;
  }
  if(errno != ENOENT) return;                 /* unreadable: not ours to guess at */

  mkdir(PS5TM_DATA_DIR, 0755);                /* the parent; a no-op when it exists */
  if(mkdir(PL_DIR, 0777) == 0) {
    chmod(PL_DIR, 0777);                      /* the umask must not narrow it: FTP writes here */
    PS5TM_INFO("payload_dir_created",
               "Ordner für eigene Payloads angelegt: %s", PL_DIR);
  } else if(errno != EEXIST) {
    PS5TM_WARN("payload_dir_failed",
               "Der Ordner %s ließ sich nicht anlegen (Fehler %d: %s).",
               PL_DIR, errno, strerror(errno));
  }
}

/* The JSON answer of GET /api/v1/payload-files, built from scratch. Caller
   frees. NULL: out of memory. g_scan_lock must be held (the file table below
   is shared). */
static char *
build_listing(void) {
  static pl_file_t files[PL_MAX_FILES];
  pl_usb_t usb[8];
  unsigned usb_n = usb_mounts(usb, 8);

  cJSON *root = cJSON_CreateObject();
  cJSON_AddBoolToObject(root, "ok", 1);
  cJSON_AddStringToObject(root, "dir", PL_DIR);

  struct stat st;
  int exists = (stat(PL_DIR, &st) == 0 && S_ISDIR(st.st_mode));
  cJSON_AddBoolToObject(root, "dir_exists", exists);

  cJSON *internal = cJSON_AddArrayToObject(root, "internal");
  if(exists) {
    unsigned n = scan_dir(PL_DIR, "", files, PL_MAX_FILES);
    for(unsigned i = 0; i < n; i++) add_file(internal, &files[i]);
  }

  cJSON *drives = cJSON_AddArrayToObject(root, "usb");
  for(unsigned u = 0; u < usb_n; u++) {
    cJSON *dj = cJSON_CreateObject();
    cJSON_AddStringToObject(dj, "mount", usb[u].mount);
    cJSON_AddStringToObject(dj, "label", usb[u].label);
    cJSON *items = cJSON_AddArrayToObject(dj, "items");

    /* The root, and the folder "payloads" in it. */
    unsigned n = scan_dir(usb[u].mount, "", files, PL_MAX_FILES);
    for(unsigned i = 0; i < n; i++) add_file(items, &files[i]);

    char sub[PL_NAME_MAX];
    usb_payload_dir(usb[u].mount, sub, sizeof(sub));
    if(sub[0]) {
      char path[PL_PATH_MAX];
      if(snprintf(path, sizeof(path), "%s/%s", usb[u].mount, sub) < (int)sizeof(path)) {
        n = scan_dir(path, sub, files, PL_MAX_FILES);
        for(unsigned i = 0; i < n; i++) add_file(items, &files[i]);
      }
    }
    cJSON_AddItemToArray(drives, dj);
  }

  cJSON_AddNumberToObject(root, "usb_count", usb_n);
  char *txt = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  return txt;
}

char *
ps5tm_payload_list(void) {
  uint64_t now = ps5tm_mono_ms();

  pthread_mutex_lock(&g_cache_lock);
  if(g_cache_json && g_cache_ms && now - g_cache_ms < PL_CACHE_MS) {
    char *copy = strdup(g_cache_json);
    pthread_mutex_unlock(&g_cache_lock);
    return copy;
  }
  pthread_mutex_unlock(&g_cache_lock);

  if(pthread_mutex_trylock(&g_scan_lock) != 0) {
    /* A scan is running — or stuck on a drive that does not answer. The last
       answer, however old, beats a second thread behind the first. */
    pthread_mutex_lock(&g_cache_lock);
    char *copy = g_cache_json ? strdup(g_cache_json) : NULL;
    pthread_mutex_unlock(&g_cache_lock);
    return copy;
  }
  char *txt = build_listing();
  pthread_mutex_unlock(&g_scan_lock);
  if(!txt) return NULL;

  pthread_mutex_lock(&g_cache_lock);
  free(g_cache_json);
  g_cache_json = strdup(txt);
  g_cache_ms   = ps5tm_mono_ms();
  pthread_mutex_unlock(&g_cache_lock);
  return txt;
}


/* ----------------------------------------------------------- resolution */

/* The full path a request names, after checking every part of it. 0 when it
   is fine; otherwise an HTTP status, with the reason in r. */
static int
resolve(const char *source, const char *mount, const char *dir, const char *name,
        char *path, size_t path_len, ps5tm_payload_result_t *r) {
  if(!name_ok(name))
    return fail(r, 400, "invalid_name",
                "Der Dateiname ist ungültig (erlaubt: ein Name auf .elf, ohne Pfad).");
  if(!source || (strcmp(source, "internal") && strcmp(source, "usb")))
    return fail(r, 400, "invalid_source", "Ort fehlt oder ist unbekannt.");

  if(!strcmp(source, "internal")) {
    if(snprintf(path, path_len, "%s/%s", PL_DIR, name) >= (int)path_len)
      return fail(r, 400, "invalid_name", "Der Dateiname ist zu lang.");
    return 0;
  }

  if(!mount || !usb_known(mount))
    return fail(r, 404, "usb_not_found",
                "Dieser USB-Speicher ist nicht (mehr) angeschlossen.");
  if(!dir_ok(dir))
    return fail(r, 400, "invalid_dir",
                "Gesucht wird nur im Hauptverzeichnis und im Ordner „payloads“.");
  int n = dir[0] ? snprintf(path, path_len, "%s/%s/%s", mount, dir, name)
                 : snprintf(path, path_len, "%s/%s", mount, name);
  if(n >= (int)path_len)
    return fail(r, 400, "invalid_name", "Der Dateiname ist zu lang.");
  return 0;
}

/* Opens the file for reading and checks it is a payload. fd >= 0 and 0 on
   success; otherwise -1 and an HTTP status in *status. */
static int
open_payload(const char *path, int *status, uint64_t *size, ps5tm_payload_result_t *r) {
  struct stat st;
  if(lstat(path, &st) != 0) {
    *status = fail(r, 404, "file_not_found", "Die Datei gibt es nicht (mehr).");
    return -1;
  }
  if(!S_ISREG(st.st_mode)) {
    *status = fail(r, 400, "not_a_file", "Das ist keine gewöhnliche Datei.");
    return -1;
  }
  int fd = open(path, O_RDONLY | O_NOFOLLOW);
  if(fd < 0) {
    *status = fail(r, 400, "unreadable", "Die Datei lässt sich nicht öffnen.");
    return -1;
  }
  struct stat fst;
  if(fstat(fd, &fst) != 0 || !S_ISREG(fst.st_mode)) {
    close(fd);
    *status = fail(r, 400, "not_a_file", "Das ist keine gewöhnliche Datei.");
    return -1;
  }
  const char *why = check_header(fd, (uint64_t)fst.st_size);
  if(why) {
    close(fd);
    char msg[160];
    snprintf(msg, sizeof(msg), "Das ist kein startbares Payload: %s.", why);
    *status = fail(r, 400, "not_a_payload", msg);
    return -1;
  }
  *size = (uint64_t)fst.st_size;
  return fd;
}


/* ------------------------------------------------------------ the loader */

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

static int
loader_connect(uint64_t deadline) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if(fd < 0) return -1;
  int fl = fcntl(fd, F_GETFL, 0);
  fcntl(fd, F_SETFL, fl | O_NONBLOCK);
#ifdef SO_NOSIGPIPE
  int one = 1;
  setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
  struct sockaddr_in sa;
  memset(&sa, 0, sizeof(sa));
  sa.sin_family      = AF_INET;
  sa.sin_port        = htons(PS5TM_LOADER_PORT);
  sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if(connect(fd, (struct sockaddr *)&sa, sizeof(sa)) != 0 && errno != EINPROGRESS) {
    close(fd);
    return -1;
  }
  int soerr = 0;
  socklen_t sl = sizeof(soerr);
  if(wait_fd(fd, POLLOUT, deadline) != 1 ||
     getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &sl) != 0 || soerr != 0) {
    close(fd);
    return -1;
  }
  return fd;
}

/* All of buf, or the deadline. 0 on success. */
static int
send_all(int fd, const char *buf, size_t len, uint64_t deadline) {
  size_t off = 0;
  while(off < len) {
    ssize_t w = send(fd, buf + off, len - off, 0);
    if(w > 0) { off += (size_t)w; continue; }
    if(w < 0 && (errno == EAGAIN || errno == EINTR) && wait_fd(fd, POLLOUT, deadline) == 1)
      continue;
    return -1;
  }
  return 0;
}

int
ps5tm_payload_start(const char *source, const char *mount, const char *dir,
                    const char *name, ps5tm_payload_result_t *r) {
  memset(r, 0, sizeof(*r));
  char path[PL_PATH_MAX];
  int st = resolve(source, mount, dir, name, path, sizeof(path), r);
  if(st) return st;

  if(pthread_mutex_trylock(&g_op_lock) != 0)
    return fail(r, 409, "busy",
                "Es läuft gerade ein anderer Start, ein Kopiervorgang oder ein Löschen.");

  uint64_t size = 0;
  int fd = open_payload(path, &st, &size, r);
  if(fd < 0) { pthread_mutex_unlock(&g_op_lock); return st; }

  uint64_t now = ps5tm_mono_ms();
  if(g_last_start_ms && now - g_last_start_ms < PL_START_GAP_MS &&
     !strcmp(g_last_start_name, name)) {
    close(fd);
    pthread_mutex_unlock(&g_op_lock);
    return fail(r, 409, "just_started",
                "Dieses Payload wurde gerade eben schon gestartet.");
  }

  char *buf = malloc(PL_CHUNK);
  if(!buf) {
    close(fd);
    pthread_mutex_unlock(&g_op_lock);
    return fail(r, 503, "no_memory", "Zu wenig Speicher.");
  }

  uint64_t deadline = now + PL_SEND_MS;
  int sock = loader_connect(now + PL_CONNECT_MS);
  if(sock < 0) {
    free(buf);
    close(fd);
    pthread_mutex_unlock(&g_op_lock);
    char shown[PL_NAME_MAX];
    PS5TM_WARN("payload_loader_down",
               "Der Payload-Lader auf Port %d antwortet nicht — „%s“ wurde nicht gestartet.",
               PS5TM_LOADER_PORT, log_name(name, shown, sizeof(shown)));
    return fail(r, 503, "loader_unavailable",
                "Der Payload-Lader (Port 9021) antwortet nicht. Läuft elfldr?");
  }

  uint64_t sent = 0;
  int bad = 0;
  for(;;) {
    ssize_t n = read(fd, buf, PL_CHUNK);
    if(n == 0) break;
    if(n < 0) { if(errno == EINTR) continue; bad = 1; break; }
    if(send_all(sock, buf, (size_t)n, deadline) != 0) { bad = 2; break; }
    sent += (uint64_t)n;
  }
  free(buf);
  close(fd);
  if(!bad && sent != size) bad = 3;             /* the file changed while it was read */
  if(bad) {
    close(sock);
    pthread_mutex_unlock(&g_op_lock);
    char shown[PL_NAME_MAX];
    PS5TM_WARN("payload_send_failed",
               "„%s“ ließ sich nicht an den Lader senden (Grund %d, %llu von %llu Bytes).",
               log_name(name, shown, sizeof(shown)), bad,
               (unsigned long long)sent, (unsigned long long)size);
    return fail(r, 503, "loader_failed",
                bad == 3 ? "Die Datei hat sich beim Senden verändert."
                         : "Das Senden an den Payload-Lader ist abgebrochen.");
  }

  /* The loader takes the end of the stream as the end of the file. Then it
     starts the program and answers with what the program prints; the first
     words are enough to show a person that it began. */
  shutdown(sock, SHUT_WR);
  unsigned char raw[1024];
  size_t got = 0;
  uint64_t reply_end = ps5tm_mono_ms() + PL_REPLY_MS;
  while(got < sizeof(raw)) {
    if(wait_fd(sock, POLLIN, reply_end) != 1) break;
    ssize_t n = recv(sock, raw + got, sizeof(raw) - got, 0);
    if(n <= 0) break;
    got += (size_t)n;
  }
  close(sock);
  sanitize(r->reply, sizeof(r->reply), raw, got);
  r->bytes = sent;

  g_last_start_ms = ps5tm_mono_ms();
  snprintf(g_last_start_name, sizeof(g_last_start_name), "%s", name);
  pthread_mutex_unlock(&g_op_lock);

  char shown[PL_NAME_MAX];
  PS5TM_INFO("payload_started",
             "Payload „%s“ an den Lader gesendet (%llu Bytes, von %s).",
             log_name(name, shown, sizeof(shown)), (unsigned long long)sent,
             strcmp(source, "usb") ? "intern" : "USB");
  return 200;
}


/* ------------------------------------------------------ copy and delete */

int
ps5tm_payload_copy(const char *mount, const char *dir, const char *name,
                   ps5tm_payload_result_t *r) {
  memset(r, 0, sizeof(*r));
  char src[PL_PATH_MAX];
  int st = resolve("usb", mount, dir, name, src, sizeof(src), r);
  if(st) return st;

  if(pthread_mutex_trylock(&g_op_lock) != 0)
    return fail(r, 409, "busy",
                "Es läuft gerade ein anderer Start, ein Kopiervorgang oder ein Löschen.");

  uint64_t size = 0;
  int in = open_payload(src, &st, &size, r);
  if(in < 0) { pthread_mutex_unlock(&g_op_lock); return st; }

  ps5tm_payloads_init();                        /* the folder may have been deleted since the start */
  char dst[PL_PATH_MAX], tmp[PL_PATH_MAX];
  if(snprintf(dst, sizeof(dst), "%s/%s", PL_DIR, name) >= (int)sizeof(dst) ||
     snprintf(tmp, sizeof(tmp), "%s/.%s.part", PL_DIR, name) >= (int)sizeof(tmp)) {
    close(in);
    pthread_mutex_unlock(&g_op_lock);
    return fail(r, 400, "invalid_name", "Der Dateiname ist zu lang.");
  }

  struct stat est;
  if(lstat(dst, &est) == 0) {
    close(in);
    pthread_mutex_unlock(&g_op_lock);
    return fail(r, 409, "exists",
                "Im internen Speicher liegt schon eine Datei mit diesem Namen. "
                "Zuerst dort löschen, wenn sie ersetzt werden soll.");
  }

  struct statvfs sv;
  if(statvfs(PL_DIR, &sv) == 0) {
    uint64_t freeb = (uint64_t)sv.f_bavail * (sv.f_frsize ? sv.f_frsize : sv.f_bsize);
    if(freeb < size + PL_FREE_MARGIN) {
      close(in);
      pthread_mutex_unlock(&g_op_lock);
      return fail(r, 409, "no_space", "Im internen Speicher ist nicht genug Platz frei.");
    }
  }

  unlink(tmp);                                  /* a leftover of an earlier try */
  int out = open(tmp, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0755);
  if(out < 0) {
    close(in);
    pthread_mutex_unlock(&g_op_lock);
    return fail(r, 503, "write_failed", "Die Zieldatei ließ sich nicht anlegen.");
  }

  char *buf = malloc(PL_CHUNK);
  uint64_t deadline = ps5tm_mono_ms() + PL_COPY_MS;
  uint64_t copied = 0;
  int bad = (buf == NULL);
  while(!bad) {
    ssize_t n = read(in, buf, PL_CHUNK);
    if(n == 0) break;
    if(n < 0) { if(errno == EINTR) continue; bad = 1; break; }
    ssize_t off = 0;
    while(off < n) {
      ssize_t w = write(out, buf + off, (size_t)(n - off));
      if(w > 0) { off += w; continue; }
      if(w < 0 && errno == EINTR) continue;
      bad = 2;
      break;
    }
    copied += (uint64_t)n;
    if(!bad && ps5tm_mono_ms() > deadline) bad = 4;
  }
  free(buf);
  close(in);
  if(!bad && copied != size) bad = 3;           /* the stick lost it, or the file changed */
  if(!bad && fsync(out) != 0) bad = 2;
  if(close(out) != 0 && !bad) bad = 2;
  if(!bad && rename(tmp, dst) != 0) bad = 2;
  if(bad) {
    unlink(tmp);
    pthread_mutex_unlock(&g_op_lock);
    char shown[PL_NAME_MAX];
    PS5TM_WARN("payload_copy_failed",
               "„%s“ ließ sich nicht in den internen Speicher kopieren (Grund %d, %llu von %llu Bytes).",
               log_name(name, shown, sizeof(shown)), bad,
               (unsigned long long)copied, (unsigned long long)size);
    return fail(r, 503, "copy_failed",
                bad == 4 ? "Das Kopieren dauerte zu lange und wurde abgebrochen."
                         : "Das Kopieren ist fehlgeschlagen. Die Datei wurde nicht übernommen.");
  }

  r->bytes = copied;
  pthread_mutex_unlock(&g_op_lock);
  cache_drop();
  char shown[PL_NAME_MAX];
  PS5TM_INFO("payload_copied",
             "Payload „%s“ in den internen Speicher kopiert (%llu Bytes).",
             log_name(name, shown, sizeof(shown)), (unsigned long long)copied);
  return 200;
}

int
ps5tm_payload_delete(const char *name, ps5tm_payload_result_t *r) {
  memset(r, 0, sizeof(*r));
  char path[PL_PATH_MAX];
  int st = resolve("internal", NULL, NULL, name, path, sizeof(path), r);
  if(st) return st;

  if(pthread_mutex_trylock(&g_op_lock) != 0)
    return fail(r, 409, "busy",
                "Es läuft gerade ein anderer Start, ein Kopiervorgang oder ein Löschen.");

  struct stat sb;
  if(lstat(path, &sb) != 0) {
    pthread_mutex_unlock(&g_op_lock);
    return fail(r, 404, "file_not_found", "Die Datei gibt es nicht (mehr).");
  }
  if(!S_ISREG(sb.st_mode)) {
    pthread_mutex_unlock(&g_op_lock);
    return fail(r, 400, "not_a_file", "Das ist keine gewöhnliche Datei.");
  }
  if(unlink(path) != 0) {
    pthread_mutex_unlock(&g_op_lock);
    return fail(r, 503, "delete_failed", "Die Datei ließ sich nicht löschen.");
  }
  pthread_mutex_unlock(&g_op_lock);
  cache_drop();
  char shown[PL_NAME_MAX];
  PS5TM_INFO("payload_deleted", "Payload „%s“ aus dem internen Speicher gelöscht.",
             log_name(name, shown, sizeof(shown)));
  return 200;
}
