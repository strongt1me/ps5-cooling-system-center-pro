/* Payload store (19.10.2026): payloads to download, the way itsPLK's Payload Manager (ps5-payload-manager 0.6.3,
 * GPL-3.0) offers them, from the very same list: the catalog itsPLK keeps at
 * https://itsplk.github.io/ps5-payloads-mirror/payloads_v2.json. A workflow there picks the newest release of each
 * payload every day, copies the file into one GitHub release of the mirror and writes name, version, category,
 * description, file name, URL and SHA-256 into that list. Only the URL is used here; nothing of the mirror is part of
 * this app.
 *
 * What the app does with it:
 *   - The list is fetched when the person asks for it ("Liste laden"), and later, once one is there, when the payloads
 *     page is opened and the list is older than three hours (the Payload Manager's interval). Nothing else goes online.
 *   - A payload is downloaded into the app's payload folder (/data/PS5-Cooling-Center/payloads) under the file name the
 *     list gives, through a hidden temporary file; it only gets its name after its SHA-256 matched the list's.
 *     installed.json beside the list remembers which file came from where in which version, so an update replaces the
 *     old file and "delete" only ever removes what the store put there.
 *   - A file of the same name that the store did not put there is not overwritten without a second click.
 *   - Starting goes the usual way (payloads.c: the loader on port 9021).
 *
 * The network: the app's own HTTPS client (upd_http.c, BearSSL, TLS 1.2), the app's own name lookup (upd_dns.c), the
 * roots of GitHub's certificates only (upd_ca.h, UPD_WEB_*), https only, and every hop on GitHub's hosts: the list on
 * itsplk.github.io, the files on github.com, which sends them on to release-assets.githubusercontent.com. */

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <strings.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <time.h>
#include <unistd.h>

#include "ps5tm.h"
#include "sha256.h"
#include "third_party/cJSON.h"
#include "upd_http.h"

#define PS_CATALOG_URL   "https://itsplk.github.io/ps5-payloads-mirror/payloads_v2.json"
#define PS_SOURCE_NAME   "itsPLK ps5-payloads-mirror"
#ifndef PS_DIR
#define PS_DIR           PS5TM_DATA_DIR "/payload-store"
#endif
#ifndef PS_PAY_DIR
#define PS_PAY_DIR       PS5TM_DATA_DIR "/payloads"
#endif
#define PS_CATALOG       PS_DIR "/catalog.json"
#define PS_INDEX         PS_DIR "/installed.json"
#define PS_TTL_S         (3 * 3600)
#define PS_MAX_CATALOG   (2u << 20)
#define PS_MAX_FILE      (128ll << 20)            /* payloads.c sends at most this much */
#define PS_MAX_ITEMS     200
#define PS_FREE_MARGIN   (16ll << 20)

typedef struct {
  char id[64], name[80], filename[96], url[400], version[48], category[48], description[480];
  char last_update[16], source[200], checksum[65];
} ps_item_t;

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static ps_item_t      *g_items;
static int             g_n = -1;           /* -1: no list read yet (not even the saved one) */
static int64_t         g_cat_at;           /* when the list was fetched (wall clock, s) */
static char            g_cat_err[200];
static int             g_fetching;

enum { PJ_IDLE = 0, PJ_RUNNING, PJ_DONE, PJ_FAILED, PJ_CANCELLED };
static struct {
  int     state;
  char    id[64], name[80], error[240];
  int64_t done, total;
} g_job;
static volatile int g_abort;

/* ---------------------------------------------------------------- small helpers */

static char *
read_file(const char *path, size_t max, size_t *len) {
  int fd = open(path, O_RDONLY | O_NOFOLLOW);
  if(fd < 0) return NULL;
  struct stat st;
  if(fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || (size_t)st.st_size > max) { close(fd); return NULL; }
  char *b = malloc((size_t)st.st_size + 1);
  if(!b) { close(fd); return NULL; }
  ssize_t r = read(fd, b, (size_t)st.st_size);
  close(fd);
  if(r != st.st_size) { free(b); return NULL; }
  b[r] = 0;
  if(len) *len = (size_t)r;
  return b;
}

static int
write_atomic(const char *path, const char *data, size_t n) {
  char tmp[512];
  snprintf(tmp, sizeof tmp, "%s.tmp", path);
  int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0666);
  if(fd < 0) return -1;
  size_t off = 0;
  while(off < n) {
    ssize_t w = write(fd, data + off, n - off);
    if(w <= 0) { if(w < 0 && errno == EINTR) continue; close(fd); unlink(tmp); return -1; }
    off += (size_t)w;
  }
  if(fsync(fd) != 0 || close(fd) != 0) { unlink(tmp); return -1; }
  if(rename(tmp, path) != 0) { unlink(tmp); return -1; }
  return 0;
}

/* GitHub's hosts, https only: the list (GitHub Pages), the release page and where it sends the file */
static int
hop_ok(const char *host, int https, void *ctx) {
  (void)ctx;
  if(!https) return 0;
  if(!strcasecmp(host, "itsplk.github.io") || !strcasecmp(host, "github.com")) return 1;
  size_t n = strlen(host);
  static const char tail[] = ".githubusercontent.com";
  return n > sizeof tail - 1 && !strcasecmp(host + n - (sizeof tail - 1), tail);
}

static int
url_ok(const char *u) {
  char host[256], path[512];
  int https = 0, port = 0;
  if(strlen(u) >= sizeof(((ps_item_t *)0)->url)) return 0;
  if(upd_url_split(u, host, sizeof host, &https, &port, path, sizeof path) != 0) return 0;
  return https && port == 443 && hop_ok(host, 1, NULL);
}

/* a plain file name: letters, digits, . _ + - (no path, no leading dot), ending in .elf or .bin */
static int
filename_ok(const char *f) {
  size_t n = strlen(f);
  if(n < 5 || n >= sizeof(((ps_item_t *)0)->filename) || f[0] == '.') return 0;
  for(size_t i = 0; i < n; i++) {
    unsigned char c = (unsigned char)f[i];
    if(!(isalnum(c) || c == '.' || c == '_' || c == '+' || c == '-')) return 0;
  }
  if(strstr(f, "..")) return 0;
  return !strcasecmp(f + n - 4, ".elf") || !strcasecmp(f + n - 4, ".bin");
}

static int
hex64_ok(const char *h) {
  if(strlen(h) != 64) return 0;
  for(int i = 0; i < 64; i++)
    if(!isxdigit((unsigned char)h[i])) return 0;
  return 1;
}

/* the Payload Manager's id: the name in lower case, runs of anything else as one "-" */
static void
slug(const char *name, char *out, size_t n) {
  size_t k = 0;
  int dash = 0;
  for(const char *p = name; *p && k + 1 < n; p++) {
    unsigned char c = (unsigned char)*p;
    if(isalnum(c)) { out[k++] = (char)tolower(c); dash = 0; }
    else if(k && !dash) { out[k++] = '-'; dash = 1; }
  }
  while(k && out[k - 1] == '-') k--;
  out[k] = 0;
}

static void
copy_field(const cJSON *o, const char *key, char *out, size_t n) {
  const cJSON *v = cJSON_GetObjectItem(o, key);
  snprintf(out, n, "%s", cJSON_IsString(v) ? v->valuestring : "");
  for(char *p = out; *p; p++)
    if((unsigned char)*p < 0x20) *p = ' ';
}

/* Version order as in the Payload Manager (frontend/src/utils/payloadStatus.js): a leading "v" dropped, runs of digits
   compared as numbers and runs of letters without case; where one ends, a further letter run counts lower ("1.7" is
   newer than "1.7beta3"), a further number higher. */
static int
ver_cmp(const char *a, const char *b) {
  while(*a == ' ') a++;
  while(*b == ' ') b++;
  if(*a == 'v' || *a == 'V') a++;
  if(*b == 'v' || *b == 'V') b++;
  for(;;) {
    while(*a && !isalnum((unsigned char)*a)) a++;
    while(*b && !isalnum((unsigned char)*b)) b++;
    if(!*a || !*b) {
      if(!*a && !*b) return 0;
      const char *rest = *a ? a : b;
      int sign = *a ? 1 : -1;                          /* a has more */
      return isdigit((unsigned char)*rest) ? sign : -sign;
    }
    if(isdigit((unsigned char)*a) && isdigit((unsigned char)*b)) {
      while(*a == '0') a++;
      while(*b == '0') b++;
      size_t la = 0, lb = 0;
      while(isdigit((unsigned char)a[la])) la++;
      while(isdigit((unsigned char)b[lb])) lb++;
      if(la != lb) return la > lb ? 1 : -1;
      int c = strncmp(a, b, la);
      if(c) return c > 0 ? 1 : -1;
      a += la;
      b += lb;
    } else if(isalpha((unsigned char)*a) && isalpha((unsigned char)*b)) {
      while(isalpha((unsigned char)*a) && isalpha((unsigned char)*b)) {
        int ca = tolower((unsigned char)*a), cb = tolower((unsigned char)*b);
        if(ca != cb) return ca > cb ? 1 : -1;
        a++;
        b++;
      }
      if(isalpha((unsigned char)*a)) return 1;
      if(isalpha((unsigned char)*b)) return -1;
    } else {
      return isdigit((unsigned char)*a) ? 1 : -1;      /* a number beats a letter at the same place */
    }
  }
}

/* ---------------------------------------------------------------- the list */

/* Parses payloads_v2.json (or the bare array of payloads.json). Entries that do not pass every check are left out. */
static int
parse_catalog(const char *json, ps_item_t **out, int *count) {
  cJSON *root = cJSON_Parse(json);
  if(!root) return -1;
  const cJSON *arr = cJSON_IsArray(root) ? root : cJSON_GetObjectItem(root, "payloads");
  if(!cJSON_IsArray(arr)) { cJSON_Delete(root); return -1; }
  ps_item_t *items = calloc(PS_MAX_ITEMS, sizeof(ps_item_t));
  if(!items) { cJSON_Delete(root); return -1; }
  int n = 0;
  const cJSON *e;
  cJSON_ArrayForEach(e, arr) {
    if(n >= PS_MAX_ITEMS) break;
    if(!cJSON_IsObject(e)) continue;
    ps_item_t *it = &items[n];
    memset(it, 0, sizeof *it);
    copy_field(e, "name", it->name, sizeof it->name);
    copy_field(e, "filename", it->filename, sizeof it->filename);
    copy_field(e, "url", it->url, sizeof it->url);
    copy_field(e, "version", it->version, sizeof it->version);
    copy_field(e, "category", it->category, sizeof it->category);
    copy_field(e, "description", it->description, sizeof it->description);
    copy_field(e, "last_update", it->last_update, sizeof it->last_update);
    copy_field(e, "source", it->source, sizeof it->source);
    copy_field(e, "checksum", it->checksum, sizeof it->checksum);
    for(char *p = it->checksum; *p; p++) *p = (char)tolower((unsigned char)*p);
    const cJSON *idv = cJSON_GetObjectItem(e, "id");
    if(cJSON_IsString(idv) && idv->valuestring[0]) slug(idv->valuestring, it->id, sizeof it->id);
    else slug(it->name, it->id, sizeof it->id);
    if(!it->id[0] || !it->name[0] || !filename_ok(it->filename) || !url_ok(it->url) || !hex64_ok(it->checksum)) continue;
    if(strncmp(it->source, "https://", 8)) it->source[0] = 0;
    int dup = 0;
    for(int i = 0; i < n; i++)
      if(!strcmp(items[i].id, it->id)) dup = 1;
    if(dup) continue;
    n++;
  }
  cJSON_Delete(root);
  *out = items;
  *count = n;
  return 0;
}

static void
load_saved_locked(void) {
  if(g_n >= 0) return;
  g_n = 0;
  size_t len = 0;
  char *b = read_file(PS_CATALOG, PS_MAX_CATALOG, &len);
  if(!b) return;
  ps_item_t *items = NULL;
  int n = 0;
  if(parse_catalog(b, &items, &n) == 0) {
    free(g_items);
    g_items = items;
    g_n = n;
    struct stat st;
    if(stat(PS_CATALOG, &st) == 0) g_cat_at = (int64_t)st.st_mtime;
  }
  free(b);
}

/* Fetches the list (outside the lock). 0, or -1 with the reason in g_cat_err. */
static int
fetch_catalog(void) {
  pthread_mutex_lock(&g_lock);
  if(g_fetching) { pthread_mutex_unlock(&g_lock); return 0; }
  g_fetching = 1;
  pthread_mutex_unlock(&g_lock);

  upd_req_t r;
  memset(&r, 0, sizeof r);
  r.url = PS_CATALOG_URL;
  r.trust = UPD_TRUST_WEB;
  r.max_redirects = 3;
  r.hop_ok = hop_ok;
  r.connect_ms = 10000;
  r.idle_ms = 20000;
  r.total_ms = 45000;
  r.range_from = -1;
  r.range_to = -1;
  r.extra_header = "Accept: application/json";
  upd_res_t res;
  char *buf = NULL;
  size_t len = 0;
  int rc = upd_http_fetch(&r, PS_MAX_CATALOG, &buf, &len, &res);
  ps_item_t *items = NULL;
  int n = 0;
  char err[200] = "";
  if(rc != 0) snprintf(err, sizeof err, "Die Liste ließ sich nicht laden (%s%s%d).", res.step[0] ? res.step : "http", res.status ? " " : "", res.status);
  else if(parse_catalog(buf, &items, &n) != 0) snprintf(err, sizeof err, "Die Liste ist kein gültiges JSON.");
  else if(n == 0) snprintf(err, sizeof err, "Die Liste enthält keinen brauchbaren Eintrag.");
  if(!err[0]) {
    mkdir(PS5TM_DATA_DIR, 0777);
    mkdir(PS_DIR, 0777);
    if(write_atomic(PS_CATALOG, buf, len) != 0)
      PS5TM_WARN("paystore_save", "Payload-Liste: ließ sich nicht speichern (%s).", strerror(errno));
  }
  free(buf);

  pthread_mutex_lock(&g_lock);
  g_fetching = 0;
  if(err[0]) {
    snprintf(g_cat_err, sizeof g_cat_err, "%s", err);
    free(items);
    pthread_mutex_unlock(&g_lock);
    PS5TM_WARN("paystore_fetch", "Payload-Liste: %s", err);
    return -1;
  }
  free(g_items);
  g_items = items;
  g_n = n;
  g_cat_at = ps5tm_now_ms() / 1000;
  g_cat_err[0] = 0;
  pthread_mutex_unlock(&g_lock);
  PS5TM_INFO("paystore_fetched", "Payload-Liste geladen: %d Payloads.", n);
  return 0;
}

/* ---------------------------------------------------------------- what is installed */

static cJSON *
index_load(void) {
  char *b = read_file(PS_INDEX, 1 << 20, NULL);
  cJSON *j = b ? cJSON_Parse(b) : NULL;
  free(b);
  if(!cJSON_IsObject(j)) { cJSON_Delete(j); j = cJSON_CreateObject(); }
  return j;
}

static int
index_save(cJSON *j) {
  char *s = cJSON_PrintUnformatted(j);
  if(!s) return -1;
  mkdir(PS5TM_DATA_DIR, 0777);
  mkdir(PS_DIR, 0777);
  int rc = write_atomic(PS_INDEX, s, strlen(s));
  free(s);
  return rc;
}

static const char *
idx_str(const cJSON *e, const char *k) {
  const cJSON *v = e ? cJSON_GetObjectItem(e, k) : NULL;
  return cJSON_IsString(v) ? v->valuestring : "";
}

static int
file_exists(const char *name) {
  char p[512];
  struct stat st;
  snprintf(p, sizeof p, PS_PAY_DIR "/%s", name);
  return lstat(p, &st) == 0 && S_ISREG(st.st_mode);
}

static int
file_sha256(const char *path, char hex[65]) {
  int fd = open(path, O_RDONLY | O_NOFOLLOW);
  if(fd < 0) return -1;
  ps5tm_sha256_t s;
  ps5tm_sha256_init(&s);
  unsigned char *buf = malloc(1 << 16);
  if(!buf) { close(fd); return -1; }
  ssize_t r;
  while((r = read(fd, buf, 1 << 16)) > 0) ps5tm_sha256_update(&s, buf, (size_t)r);
  free(buf);
  close(fd);
  if(r < 0) return -1;
  uint8_t d[32];
  ps5tm_sha256_final(&s, d);
  ps5tm_sha256_hex(d, hex);
  return 0;
}

/* ---------------------------------------------------------------- the download */

typedef struct {
  int            fd;
  int64_t        got;
  ps5tm_sha256_t sha;
  int            too_big, write_failed, bad_status;
} dl_t;

static int
dl_head(void *ctx, const upd_res_t *res) {
  dl_t *d = ctx;
  if(res->status != 200) { d->bad_status = 1; return 1; }
  if(res->content_length > PS_MAX_FILE) { d->too_big = 1; return 1; }
  pthread_mutex_lock(&g_lock);
  g_job.total = res->content_length > 0 ? res->content_length : 0;
  pthread_mutex_unlock(&g_lock);
  return 0;
}

static int
dl_body(void *ctx, const void *data, size_t n) {
  dl_t *d = ctx;
  if(d->got + (int64_t)n > PS_MAX_FILE) { d->too_big = 1; return 1; }
  const unsigned char *p = data;
  size_t left = n;
  while(left) {
    ssize_t w = write(d->fd, p, left);
    if(w <= 0) { if(w < 0 && errno == EINTR) continue; d->write_failed = 1; return 1; }
    p += w;
    left -= (size_t)w;
  }
  ps5tm_sha256_update(&d->sha, data, n);
  d->got += (int64_t)n;
  pthread_mutex_lock(&g_lock);
  g_job.done = d->got;
  pthread_mutex_unlock(&g_lock);
  return 0;
}

static void
job_end(int state, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

static void
job_end(int state, const char *fmt, ...) {
  pthread_mutex_lock(&g_lock);
  g_job.state = state;
  g_job.error[0] = 0;
  if(fmt) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_job.error, sizeof g_job.error, fmt, ap);
    va_end(ap);
  }
  pthread_mutex_unlock(&g_lock);
}

static void *
install_thread(void *arg) {
  ps_item_t *it = arg;
  char tmp[512], dst[512];
  snprintf(tmp, sizeof tmp, PS_PAY_DIR "/.store-%s.part", it->id);
  snprintf(dst, sizeof dst, PS_PAY_DIR "/%s", it->filename);
  mkdir(PS5TM_DATA_DIR, 0777);
  mkdir(PS_PAY_DIR, 0777);

  dl_t d;
  memset(&d, 0, sizeof d);
  ps5tm_sha256_init(&d.sha);
  d.fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0666);
  if(d.fd < 0) { job_end(PJ_FAILED, "Die Datei ließ sich nicht anlegen: %s", strerror(errno)); free(it); return NULL; }

  upd_req_t r;
  memset(&r, 0, sizeof r);
  r.url = it->url;
  r.trust = UPD_TRUST_WEB;
  r.max_redirects = 5;
  r.hop_ok = hop_ok;
  r.connect_ms = 15000;
  r.idle_ms = 30000;
  r.total_ms = 10 * 60 * 1000;
  r.range_from = -1;
  r.range_to = -1;
  r.on_head = dl_head;
  r.on_body = dl_body;
  r.ctx = &d;
  r.abort = &g_abort;
  upd_res_t res;
  int rc = upd_http(&r, &res);
  int ok_fs = fsync(d.fd) == 0;
  close(d.fd);

  char hex[65] = "";
  if(rc == 0 && !d.bad_status && !d.too_big && !d.write_failed) {
    uint8_t dg[32];
    ps5tm_sha256_final(&d.sha, dg);
    ps5tm_sha256_hex(dg, hex);
  }
  const char *fail = NULL;
  char why[160];
  if(g_abort) fail = "";
  else if(d.too_big) fail = "Die Datei ist größer als 128 MB.";
  else if(d.write_failed || !ok_fs) fail = "Die Datei ließ sich nicht schreiben.";
  else if(rc != 0 || d.bad_status) {
    snprintf(why, sizeof why, "Der Download scheiterte (%s%s%d).", res.step[0] ? res.step : "http", res.status ? " " : "", res.status);
    fail = why;
  } else if(strcmp(hex, it->checksum)) fail = "Die Prüfsumme der Datei passt nicht zur Liste. Die Datei wurde verworfen.";
  if(!fail && !strcasecmp(it->filename + strlen(it->filename) - 4, ".elf")) {
    unsigned char h[4] = { 0 };
    int fd = open(tmp, O_RDONLY);
    if(fd < 0 || read(fd, h, 4) != 4 || memcmp(h, "\x7f" "ELF", 4)) fail = "Die Datei ist kein ELF.";
    if(fd >= 0) close(fd);
  }
  if(fail) {
    unlink(tmp);
    if(g_abort) { job_end(PJ_CANCELLED, "Abgebrochen."); PS5TM_INFO("paystore_cancelled", "Payload-Download abgebrochen: %s.", it->name); }
    else { job_end(PJ_FAILED, "%s", fail); PS5TM_WARN("paystore_failed", "Payload-Download %s: %s", it->name, fail); }
    free(it);
    return NULL;
  }

  /* the file gets its name; an older file of this payload under another name goes (only one the store put there) */
  pthread_mutex_lock(&g_lock);
  cJSON *idx = index_load();
  const cJSON *old = cJSON_GetObjectItem(idx, it->id);
  char oldname[96];
  snprintf(oldname, sizeof oldname, "%s", idx_str(old, "filename"));
  if(rename(tmp, dst) != 0) {
    int e = errno;
    pthread_mutex_unlock(&g_lock);
    unlink(tmp);
    cJSON_Delete(idx);
    job_end(PJ_FAILED, "Die Datei ließ sich nicht ablegen: %s", strerror(e));
    free(it);
    return NULL;
  }
  if(oldname[0] && strcmp(oldname, it->filename) && filename_ok(oldname)) {
    char op[512];
    snprintf(op, sizeof op, PS_PAY_DIR "/%s", oldname);
    unlink(op);
  }
  cJSON_DeleteItemFromObject(idx, it->id);
  cJSON *e = cJSON_AddObjectToObject(idx, it->id);
  cJSON_AddStringToObject(e, "name", it->name);
  cJSON_AddStringToObject(e, "filename", it->filename);
  cJSON_AddStringToObject(e, "version", it->version);
  cJSON_AddStringToObject(e, "checksum", it->checksum);
  cJSON_AddStringToObject(e, "last_update", it->last_update);
  cJSON_AddStringToObject(e, "url", it->url);
  cJSON_AddNumberToObject(e, "installed_at", (double)(ps5tm_now_ms() / 1000));
  int saved = index_save(idx) == 0;
  cJSON_Delete(idx);
  g_job.state = PJ_DONE;
  g_job.done = g_job.total = d.got;
  snprintf(g_job.error, sizeof g_job.error, "%s", saved ? "" : "Geladen, aber die Liste der geladenen Payloads ließ sich nicht speichern.");
  pthread_mutex_unlock(&g_lock);
  PS5TM_INFO("paystore_installed", "Payload geladen: %s %s (%s).", it->name, it->version, it->filename);
  free(it);
  return NULL;
}

/* ---------------------------------------------------------------- the API */

static const ps_item_t *
find_locked(const char *id) {
  for(int i = 0; i < g_n; i++)
    if(!strcmp(g_items[i].id, id)) return &g_items[i];
  return NULL;
}

cJSON *
ps5tm_paystore_json(int refresh) {
  pthread_mutex_lock(&g_lock);
  load_saved_locked();
  int64_t now = ps5tm_now_ms() / 1000;
  int stale = g_n <= 0 || now - g_cat_at >= PS_TTL_S || now < g_cat_at;
  /* refresh 1: only when a list was loaded once before and is old by now; 2: always (the button) */
  int want = refresh == 2 || (refresh == 1 && g_n > 0 && stale);
  pthread_mutex_unlock(&g_lock);
  if(want) fetch_catalog();

  cJSON *o = cJSON_CreateObject();
  cJSON *idx = index_load();
  pthread_mutex_lock(&g_lock);
  cJSON_AddBoolToObject(o, "ok", 1);
  cJSON_AddStringToObject(o, "source", PS_SOURCE_NAME);
  cJSON_AddStringToObject(o, "source_url", PS_CATALOG_URL);
  cJSON_AddNumberToObject(o, "fetched_at", (double)g_cat_at);
  cJSON_AddNumberToObject(o, "age_s", g_cat_at ? (double)(now - g_cat_at) : -1);
  cJSON_AddBoolToObject(o, "loaded", g_n > 0);
  if(g_cat_err[0]) cJSON_AddStringToObject(o, "error", g_cat_err);
  cJSON *arr = cJSON_AddArrayToObject(o, "items");
  for(int i = 0; i < g_n; i++) {
    const ps_item_t *it = &g_items[i];
    cJSON *x = cJSON_CreateObject();
    cJSON_AddStringToObject(x, "id", it->id);
    cJSON_AddStringToObject(x, "name", it->name);
    cJSON_AddStringToObject(x, "filename", it->filename);
    cJSON_AddStringToObject(x, "version", it->version);
    cJSON_AddStringToObject(x, "category", it->category);
    cJSON_AddStringToObject(x, "description", it->description);
    cJSON_AddStringToObject(x, "last_update", it->last_update);
    cJSON_AddStringToObject(x, "source", it->source);
    const cJSON *e = cJSON_GetObjectItem(idx, it->id);
    const char *fn = idx_str(e, "filename");
    if(e && fn[0] && file_exists(fn)) {
      cJSON *ins = cJSON_AddObjectToObject(x, "installed");
      cJSON_AddStringToObject(ins, "version", idx_str(e, "version"));
      cJSON_AddStringToObject(ins, "filename", fn);
      int c = ver_cmp(it->version, idx_str(e, "version"));
      int upd = c > 0 || (c == 0 && strcmp(it->checksum, idx_str(e, "checksum")) && strcmp(it->last_update, idx_str(e, "last_update")) >= 0);
      cJSON_AddBoolToObject(x, "update", upd);
    } else {
      cJSON_AddBoolToObject(x, "update", 0);
      cJSON_AddBoolToObject(x, "name_taken", file_exists(it->filename));
    }
    cJSON_AddItemToArray(arr, x);
  }
  cJSON *j = cJSON_AddObjectToObject(o, "job");
  static const char *const st[] = { "idle", "running", "done", "failed", "cancelled" };
  cJSON_AddStringToObject(j, "state", st[g_job.state]);
  cJSON_AddStringToObject(j, "id", g_job.id);
  cJSON_AddStringToObject(j, "name", g_job.name);
  cJSON_AddNumberToObject(j, "done", (double)g_job.done);
  cJSON_AddNumberToObject(j, "total", (double)g_job.total);
  if(g_job.error[0]) cJSON_AddStringToObject(j, "error", g_job.error);
  cJSON_AddBoolToObject(o, "fetching", g_fetching);
  pthread_mutex_unlock(&g_lock);
  cJSON_Delete(idx);
  return o;
}

int
ps5tm_paystore_busy(void) {
  pthread_mutex_lock(&g_lock);
  int b = g_job.state == PJ_RUNNING;
  pthread_mutex_unlock(&g_lock);
  return b;
}

/* POST {action: refresh | install | delete | cancel | start, id, force}. HTTP status; msg for the person. */
int
ps5tm_paystore_action(const cJSON *b, char *msg, size_t mn) {
  msg[0] = 0;
  const cJSON *ja = cJSON_GetObjectItem(b, "action"), *jid = cJSON_GetObjectItem(b, "id");
  const char *a = cJSON_IsString(ja) ? ja->valuestring : "";
  const char *id = cJSON_IsString(jid) ? jid->valuestring : "";
  int force = cJSON_IsTrue(cJSON_GetObjectItem(b, "force"));

  if(!strcmp(a, "refresh")) {
    if(fetch_catalog() != 0) {
      pthread_mutex_lock(&g_lock);
      snprintf(msg, mn, "%s", g_cat_err);
      pthread_mutex_unlock(&g_lock);
      return 502;
    }
    pthread_mutex_lock(&g_lock);
    snprintf(msg, mn, "Liste geladen: %d Payloads.", g_n);
    pthread_mutex_unlock(&g_lock);
    return 200;
  }
  if(!strcmp(a, "cancel")) {
    pthread_mutex_lock(&g_lock);
    int run = g_job.state == PJ_RUNNING;
    pthread_mutex_unlock(&g_lock);
    if(run) g_abort = 1;
    snprintf(msg, mn, "%s", run ? "Wird abgebrochen." : "Es läuft kein Download.");
    return 200;
  }

  pthread_mutex_lock(&g_lock);
  load_saved_locked();
  const ps_item_t *f = find_locked(id);
  ps_item_t it;
  if(f) it = *f;
  pthread_mutex_unlock(&g_lock);

  if(!strcmp(a, "start") || !strcmp(a, "delete")) {
    /* by the installed file, which may come from an older list */
    cJSON *idx = index_load();
    const cJSON *e = cJSON_GetObjectItem(idx, id);
    char fn[96];
    snprintf(fn, sizeof fn, "%s", idx_str(e, "filename"));
    if(!e || !filename_ok(fn) || !file_exists(fn)) {
      cJSON_Delete(idx);
      snprintf(msg, mn, "Dieses Payload ist nicht geladen.");
      return 404;
    }
    if(!strcmp(a, "start")) {
      cJSON_Delete(idx);
      ps5tm_payload_result_t r;
      int st = ps5tm_payload_start("internal", "", "", fn, &r);
      snprintf(msg, mn, "%s", r.msg[0] ? r.msg : st == 200 ? "Gestartet." : "Nicht gestartet.");
      return st;
    }
    pthread_mutex_lock(&g_lock);
    int busy = g_job.state == PJ_RUNNING && !strcmp(g_job.id, id);
    pthread_mutex_unlock(&g_lock);
    if(busy) { cJSON_Delete(idx); snprintf(msg, mn, "Dieses Payload wird gerade geladen."); return 409; }
    char p[512];
    snprintf(p, sizeof p, PS_PAY_DIR "/%s", fn);
    if(unlink(p) != 0 && errno != ENOENT) {
      cJSON_Delete(idx);
      snprintf(msg, mn, "Die Datei ließ sich nicht löschen: %s", strerror(errno));
      return 500;
    }
    cJSON_DeleteItemFromObject(idx, id);
    index_save(idx);
    cJSON_Delete(idx);
    PS5TM_INFO("paystore_deleted", "Payload gelöscht: %s.", fn);
    snprintf(msg, mn, "Gelöscht.");
    return 200;
  }

  if(!strcmp(a, "install")) {
    if(!f) { snprintf(msg, mn, "Dieses Payload steht nicht in der Liste."); return 404; }
    /* a file of this name the store did not put there is replaced only on a second click */
    cJSON *idx = index_load();
    const cJSON *e = cJSON_GetObjectItem(idx, it.id);
    int ours = e && !strcmp(idx_str(e, "filename"), it.filename);
    cJSON_Delete(idx);
    if(!ours && file_exists(it.filename) && !force) {
      char p[512], hex[65] = "";
      snprintf(p, sizeof p, PS_PAY_DIR "/%s", it.filename);
      if(file_sha256(p, hex) != 0 || strcmp(hex, it.checksum)) {
        snprintf(msg, mn, "Im Payload-Ordner liegt schon eine andere Datei namens %s. Noch einmal klicken ersetzt sie.", it.filename);
        return 409;
      }
    }
    struct statvfs sv;
    if(statvfs(PS5TM_DATA_DIR, &sv) == 0 && (int64_t)sv.f_bavail * (int64_t)(sv.f_frsize ? sv.f_frsize : sv.f_bsize) < PS_MAX_FILE / 4 + PS_FREE_MARGIN) {
      snprintf(msg, mn, "Auf der Konsole ist zu wenig Platz frei.");
      return 507;
    }
    pthread_mutex_lock(&g_lock);
    if(g_job.state == PJ_RUNNING) { pthread_mutex_unlock(&g_lock); snprintf(msg, mn, "Es läuft schon ein Download."); return 409; }
    memset(&g_job, 0, sizeof g_job);
    g_job.state = PJ_RUNNING;
    snprintf(g_job.id, sizeof g_job.id, "%s", it.id);
    snprintf(g_job.name, sizeof g_job.name, "%s", it.name);
    g_abort = 0;
    pthread_mutex_unlock(&g_lock);
    ps_item_t *arg = malloc(sizeof *arg);
    if(!arg) { job_end(PJ_FAILED, "Kein Speicher."); snprintf(msg, mn, "Kein Speicher."); return 500; }
    *arg = it;
    pthread_t th;
    pthread_attr_t at;
    pthread_attr_init(&at);
    pthread_attr_setstacksize(&at, 256 * 1024);
    pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
    int rc = pthread_create(&th, &at, install_thread, arg);
    pthread_attr_destroy(&at);
    if(rc != 0) { free(arg); job_end(PJ_FAILED, "Der Download ließ sich nicht starten."); snprintf(msg, mn, "Der Download ließ sich nicht starten."); return 500; }
    snprintf(msg, mn, "%s %s wird geladen.", it.name, it.version);
    return 200;
  }
  snprintf(msg, mn, "Unbekannte Aktion.");
  return 400;
}
