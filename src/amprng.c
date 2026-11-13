/* ampr-emu-ng in the choice of APR-EMU versions (03.11.2026, wish of the user): quer3q's fork of drakmor's ampr_emu
 * (https://github.com/quer3q/ampr-emu-ng, GPL-3.0). It reads the same LZ4 packs and zstd ones, and its builds carry the mark
 * "ampr-emu-ng <version>" instead of "[AMPR_EMU]"; amprlib.c recognises them by that and shows them as "NG <version>".
 *
 * The two newest releases are carried by the app (wish of the user, 13.11.2026: always the two newest to choose from): 0.4.0 and 0.3.5,
 * third_party/ampr_emu/libSceAmpr.sprx-<version>-ng and -ng-debug of each, packed into the payload by tools/gen_emu_blob.py. A newer
 * release replaces the oldest of the two (the old files are taken out of third_party/ampr_emu). Between the two a file the table
 * below names counts as there when the app carries it. A newer release is fetched on request
 * (POST /api/v1/ampr-emu/fetch-ng): the two files of the release named below come from GitHub into the folder
 * AMPR_EMU, under the names amprlib.c reads (libSceAmpr.sprx-<version>-ng, -ng-debug). A file only gets its name after its
 * SHA-256 matched the one written here (from the release's SHA256SUMS, checked on 10.11.2026) and it carries the SELF header
 * and the mark. A newer release goes into the table below by hand (tools/upstream.json watches the project); until then the
 * person can add its files with "Datei hinzufügen" (the page names them).
 *
 * The network: the app's own HTTPS client (upd_http.c, GitHub's roots only), https only, every hop on github.com or
 * *.githubusercontent.com. Nothing goes online unless the person presses the button. */
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "ps5tm.h"
#include "sha256.h"
#include "third_party/cJSON.h"
#include "upd_http.h"

#define NG_VERSION  "0.4.0"      /* the newest release: the table below and the default of the zstd repair (ps5tm_amprng_name) */
#define NG_BASE     "https://github.com/quer3q/ampr-emu-ng/releases/download/v" NG_VERSION "/"
#define NG_DIR      PS5TM_DATA_DIR "/AMPR_EMU"
#define NG_MAX      (4u * 1024 * 1024)

typedef struct {
  const char *asset;           /* the name in the release */
  const char *name;            /* the name in the folder AMPR_EMU */
  const char *sha256;
  unsigned    size;
} ng_file_t;

static const ng_file_t NG_FILES[] = {
  { "libSceAmpr.sprx", "libSceAmpr.sprx-" NG_VERSION "-ng", "8320eb65c16565f42031b9d88b15a447039f0fd7e5694faf5f1cb84cafdba218", 775696 },
  { "libSceAmpr-debug.sprx", "libSceAmpr.sprx-" NG_VERSION "-ng-debug", "d27f7c58a92da9807366db5d15423ef19257ad06df3b78ea0d0fbaea3ce664f6", 899072 },
};
#define NG_COUNT (sizeof(NG_FILES) / sizeof(NG_FILES[0]))

const char *
ps5tm_amprng_name(void) {
  return "libSceAmpr.sprx-" NG_VERSION "-ng";
}

static pthread_mutex_t g_ng_lock = PTHREAD_MUTEX_INITIALIZER;
static int             g_ng_busy;

static int
hop_ok(const char *host, int https, void *ctx) {
  (void)ctx;
  if(!https) return 0;
  if(!strcasecmp(host, "github.com")) return 1;
  size_t n = strlen(host);
  static const char tail[] = ".githubusercontent.com";
  return n > sizeof tail - 1 && !strcasecmp(host + n - (sizeof tail - 1), tail);
}

static void
sha_hex(const unsigned char *d, size_t n, char out[65]) {
  ps5tm_sha256_t s;
  uint8_t dg[32];
  ps5tm_sha256_init(&s);
  ps5tm_sha256_update(&s, d, n);
  ps5tm_sha256_final(&s, dg);
  ps5tm_sha256_hex(dg, out);
}

static int
has_text(const unsigned char *d, size_t n, const char *t) {
  size_t m = strlen(t);
  for(size_t i = 0; i + m <= n; i++)
    if(d[i] == (unsigned char)t[0] && !memcmp(d + i, t, m)) return 1;
  return 0;
}

/* 1 when the app carries this very file (name and SHA-256): it goes into the folder with the other built-in versions */
static int
carried(const ng_file_t *f) {
  for(unsigned i = 0; i < ps5tm_emu_blob_count; i++)
    if(!strcmp(ps5tm_emu_blobs[i].name, f->name) && !strcmp(ps5tm_emu_blobs[i].sha256, f->sha256)) return 1;
  return 0;
}

/* 1 when the file is there: the app carries it, or the folder already holds this very file */
static int
have_file(const ng_file_t *f) {
  if(carried(f)) return 1;
  char path[300];
  snprintf(path, sizeof(path), NG_DIR "/%s", f->name);
  FILE *fp = fopen(path, "rb");
  if(!fp) return 0;
  unsigned char *buf = malloc(NG_MAX);
  size_t n = buf ? fread(buf, 1, NG_MAX, fp) : 0;
  fclose(fp);
  int same = 0;
  if(buf && n == f->size) {
    char hex[65];
    sha_hex(buf, n, hex);
    same = !strcmp(hex, f->sha256);
  }
  free(buf);
  return same;
}

void
ps5tm_amprng_json(struct cJSON *o) {
  cJSON *ng = cJSON_AddObjectToObject(o, "ng_release");
  cJSON_AddStringToObject(ng, "version", NG_VERSION);
  cJSON_AddStringToObject(ng, "project", "https://github.com/quer3q/ampr-emu-ng");
  int missing = 0;
  cJSON *files = cJSON_AddArrayToObject(ng, "files");
  for(unsigned i = 0; i < NG_COUNT; i++) {
    int have = have_file(&NG_FILES[i]);
    missing += !have;
    cJSON *e = cJSON_CreateObject();
    cJSON_AddStringToObject(e, "name", NG_FILES[i].name);
    cJSON_AddStringToObject(e, "asset", NG_FILES[i].asset);
    cJSON_AddNumberToObject(e, "size", NG_FILES[i].size);
    cJSON_AddBoolToObject(e, "stored", have);
    cJSON_AddItemToArray(files, e);
  }
  cJSON_AddNumberToObject(ng, "missing", missing);
  pthread_mutex_lock(&g_ng_lock);
  cJSON_AddBoolToObject(ng, "busy", g_ng_busy);
  pthread_mutex_unlock(&g_ng_lock);
}

/* One file into the folder: download into memory, check, write under its name. 0 or -1 with why. */
static int
fetch_one(const ng_file_t *f, char *why, size_t why_len) {
  char url[256];
  snprintf(url, sizeof(url), NG_BASE "%s", f->asset);
  upd_req_t r;
  memset(&r, 0, sizeof r);
  r.url = url;
  r.trust = UPD_TRUST_WEB;
  r.max_redirects = 5;
  r.hop_ok = hop_ok;
  r.connect_ms = 15000;
  r.idle_ms = 30000;
  r.total_ms = 3 * 60 * 1000;
  r.range_from = -1;
  r.range_to = -1;
  upd_res_t res;
  char *buf = NULL;
  size_t len = 0;
  if(upd_http_fetch(&r, NG_MAX, &buf, &len, &res) != 0) {
    snprintf(why, why_len, "%s ließ sich nicht laden (%s%s%d).", f->asset, res.step[0] ? res.step : "http", res.status ? " " : "", res.status);
    free(buf);
    return -1;
  }
  const unsigned char *d = (const unsigned char *)buf;
  char hex[65];
  sha_hex(d, len, hex);
  int rc = 0;
  if(strcmp(hex, f->sha256)) {
    snprintf(why, why_len, "%s: die Prüfsumme passt nicht zur Veröffentlichung %s. Die Datei wurde verworfen.", f->asset, NG_VERSION);
    rc = -1;
  } else if(len < 0x100 || memcmp(d, "\x4f\x15\x3d\x1d", 4) || !has_text(d, len, "ampr-emu-ng ")) {
    snprintf(why, why_len, "%s ist keine ampr-emu-ng-Datei.", f->asset);
    rc = -1;
  } else {
    char path[300];
    snprintf(path, sizeof(path), NG_DIR "/%s", f->name);
    int e = ps5tm_put_lib_file(path, d, len);
    if(e) { snprintf(why, why_len, "%s ließ sich nicht in den Ordner AMPR_EMU legen: %s", f->name, strerror(e)); rc = -1; }
  }
  free(buf);
  return rc;
}

struct cJSON *
ps5tm_amprng_fetch(char *why, size_t why_len, int *http) {
  if(why && why_len) why[0] = 0;
  *http = 200;
  pthread_mutex_lock(&g_ng_lock);
  if(g_ng_busy) {
    pthread_mutex_unlock(&g_ng_lock);
    snprintf(why, why_len, "ampr-emu-ng wird gerade schon geladen.");
    *http = 409;
    return NULL;
  }
  g_ng_busy = 1;
  pthread_mutex_unlock(&g_ng_lock);
  cJSON *versions = ps5tm_amprlib_versions("ampr");          /* also makes the folder AMPR_EMU */
  cJSON_Delete(versions);
  unsigned put = 0;
  int rc = 0;
  for(unsigned i = 0; i < NG_COUNT && !rc; i++) {
    if(have_file(&NG_FILES[i])) continue;
    rc = fetch_one(&NG_FILES[i], why, why_len);
    if(!rc) put++;
  }
  pthread_mutex_lock(&g_ng_lock);
  g_ng_busy = 0;
  pthread_mutex_unlock(&g_ng_lock);
  if(rc) {
    PS5TM_WARN("ampr_ng_fetch", "ampr-emu-ng %s: %s", NG_VERSION, why);
    *http = 502;
    return NULL;
  }
  if(put) PS5TM_INFO("ampr_ng_fetched", "ampr-emu-ng %s geladen: %u Datei(en) im Ordner AMPR_EMU.", NG_VERSION, put);
  cJSON *o = ps5tm_amprlib_versions("ampr");
  cJSON_AddNumberToObject(o, "installed", put);
  return o;
}
