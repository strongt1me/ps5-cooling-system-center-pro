/* ampr-emu-ng in the choice of APR-EMU versions (03.11.2026, wish of the user): quer3q's fork of drakmor's ampr_emu
 * (https://github.com/quer3q/ampr-emu-ng, GPL-3.0). It reads the same LZ4 packs and zstd ones, and its builds carry the mark
 * "ampr-emu-ng <version>" instead of "[AMPR_EMU]"; amprlib.c recognises them by that and shows them as "NG <version>".
 *
 * Two versions of it are offered at any time (wish of the user, 13.11.2026: the two newest; 15.11.2026: a newer one comes on top by a button, the
 * oldest drops out). The app carries two (third_party/ampr_emu/libSceAmpr.sprx-<version>-ng and -ng-debug, packed into the payload by
 * tools/gen_emu_blob.py: 0.4.1 and 0.3.5, the second one by choice of the user, because 0.4.0 had known errors). A newer one is fetched on the
 * person's request (POST /api/v1/ampr-emu/fetch-ng): the app loads libSceAmpr.sprx of the newest STABLE release of the project (releases/latest/download:
 * github.com's own redirect, a beta pre-release is never taken), reads the version from its mark "ampr-emu-ng <version>", and when that is newer than
 * every version offered, loads libSceAmpr-debug.sprx and the release's SHA256SUMS too. A file only gets its name (libSceAmpr.sprx-<version>-ng, -ng-debug, in the folder
 * AMPR_EMU, the names amprlib.c reads) after its SHA-256 matched the line of SHA256SUMS and it carries the SELF header and the mark
 * "ampr-emu-ng <version>". Then the oldest version drops out of the list (amprlib.c, list_versions; the files of it that the app put into the folder
 * itself are deleted, ps5tm_amprlib_ng_prune).
 *
 * The network: the app's own HTTPS client (upd_http.c, GitHub's roots only), https only, every hop on github.com, api.github.com or
 * *.githubusercontent.com. Nothing goes online unless the person presses the button. */
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "ps5tm.h"
#include "sha256.h"
#include "third_party/cJSON.h"
#include "upd_http.h"

#define NG_VERSION  "0.4.1"      /* the newest version the app carries: the name for the zstd repair when the folder holds nothing newer */
/* "latest" of github.com is the newest STABLE release (a pre-release never is) and redirects to its files: no use of the API, whose 60 requests an hour
   for one address ran out in tests (HTTP 403) */
#define NG_LATEST   "https://github.com/quer3q/ampr-emu-ng/releases/latest/download/"
#define NG_DIR      PS5TM_DATA_DIR "/AMPR_EMU"
#define NG_MAX      (4u * 1024 * 1024)

const char *
ps5tm_amprng_name(void) {
  static __thread char name[96];
  if(!ps5tm_amprlib_ng_newest(name, sizeof(name), NULL, 0)) snprintf(name, sizeof(name), "libSceAmpr.sprx-" NG_VERSION "-ng");
  return name;
}

static pthread_mutex_t g_ng_lock = PTHREAD_MUTEX_INITIALIZER;
static int             g_ng_busy;

static int
hop_ok(const char *host, int https, void *ctx) {
  (void)ctx;
  if(!https) return 0;
  if(!strcasecmp(host, "github.com") || !strcasecmp(host, "api.github.com")) return 1;
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

/* "0.4.1" against "0.3.5": -1, 0, 1 */
static int
ver_cmp(const char *a, const char *b) {
  while(*a || *b) {
    unsigned long x = strtoul(a, (char **)&a, 10), y = strtoul(b, (char **)&b, 10);
    if(x != y) return x < y ? -1 : 1;
    if(*a == '.') a++;
    if(*b == '.') b++;
  }
  return 0;
}

void
ps5tm_amprng_json(struct cJSON *o) {
  cJSON *ng = cJSON_AddObjectToObject(o, "ng_release");
  char ver[24] = NG_VERSION;
  ps5tm_amprlib_ng_newest(NULL, 0, ver, sizeof(ver));
  cJSON_AddStringToObject(ng, "version", ver);                /* the newest version offered */
  cJSON_AddStringToObject(ng, "project", "https://github.com/quer3q/ampr-emu-ng");
  cJSON_AddNumberToObject(ng, "missing", 0);                  /* kept for the pages: whether something newer exists is found out by the button */
  pthread_mutex_lock(&g_ng_lock);
  cJSON_AddBoolToObject(ng, "busy", g_ng_busy);
  pthread_mutex_unlock(&g_ng_lock);
}

static int
fetch_url(const char *url, size_t max, char **buf, size_t *len, char *why, size_t why_len, const char *what) {
  upd_req_t r;
  memset(&r, 0, sizeof r);
  r.url = url;
  r.trust = UPD_TRUST_WEB;
  r.max_redirects = 5;
  r.hop_ok = hop_ok;
  r.connect_ms = 15000;
  r.idle_ms = 30000;
  r.total_ms = 50 * 1000;                       /* three loads one after the other stay below the 180 s the pages wait */
  r.range_from = -1;
  r.range_to = -1;
  upd_res_t res;
  *buf = NULL;
  *len = 0;
  if(upd_http_fetch(&r, max, buf, len, &res) != 0) {
    if(!res.status && (!strcmp(res.step, "dns") || !strcmp(res.step, "connect")))
      snprintf(why, why_len, "Keine Internetverbindung: GitHub ist nicht erreichbar. Bitte die Netzwerkverbindung der PS5 prüfen.");
    else if(res.status == 403 || res.status == 429) snprintf(why, why_len, "%s ließ sich nicht laden: GitHub begrenzt gerade die Anfragen von dieser Adresse (HTTP %d). Bitte später noch einmal.", what, res.status);
    else snprintf(why, why_len, "%s ließ sich nicht laden (%s%s%d).", what, res.step[0] ? res.step : "http", res.status ? " " : "", res.status);
    free(*buf);
    *buf = NULL;
    return -1;
  }
  return 0;
}

/* The line of SHA256SUMS for `name`: its 64 hex digits into out. 1 when found. */
static int
sums_find(const char *sums, const char *name, char out[65]) {
  size_t nl = strlen(name);
  for(const char *p = sums; *p;) {
    const char *e = strchr(p, '\n');
    size_t l = e ? (size_t)(e - p) : strlen(p);
    if(l >= 64 + 2 + nl) {
      int hex = 1;
      for(int i = 0; i < 64; i++) {
        char c = p[i];
        if(!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) hex = 0;
      }
      const char *fn = p + 64;
      while(fn < p + l && (*fn == ' ' || *fn == '*')) fn++;
      size_t fl = (size_t)(p + l - fn);
      while(fl && (fn[fl - 1] == '\r' || fn[fl - 1] == ' ')) fl--;
      if(hex && fl == nl && !strncmp(fn, name, nl)) { memcpy(out, p, 64); out[64] = 0; return 1; }
    }
    if(!e) break;
    p = e + 1;
  }
  return 0;
}

/* The version out of the mark "ampr-emu-ng <version>" of a build: digits and dots. 1 when found. */
static int
ng_version_of(const unsigned char *d, size_t n, char *out, size_t on) {
  static const char mark[] = "ampr-emu-ng ";
  const size_t ml = sizeof mark - 1;
  for(size_t i = 0; i + ml + 1 <= n; i++) {
    if(d[i] != 'a' || memcmp(d + i, mark, ml)) continue;
    size_t k = 0;
    while(i + ml + k < n && ((d[i + ml + k] >= '0' && d[i + ml + k] <= '9') || d[i + ml + k] == '.') && k + 1 < on) { out[k] = (char)d[i + ml + k]; k++; }
    /* the mark of a stable build ends right after the version (a zero byte): "0.5.0-beta1" or the like is not taken */
    if(i + ml + k < n && d[i + ml + k] != 0) continue;
    while(k && out[k - 1] == '.') k--;
    out[k] = 0;
    if(k) return 1;
  }
  return 0;
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

  char message[300] = "";
  unsigned put = 0, pruned = 0;
  int rc = 0;
  char *sums = NULL, *bufs[2] = { NULL, NULL };
  size_t sums_n = 0, lens[2] = { 0, 0 };
  char ver[24] = "";
  do {
    static const char *const assets_wanted[2] = { "libSceAmpr.sprx", "libSceAmpr-debug.sprx" };
    char url[200];
    /* the plain build first: its mark names the version, and when nothing newer is there nothing else is loaded */
    snprintf(url, sizeof(url), NG_LATEST "%s", assets_wanted[0]);
    if(fetch_url(url, NG_MAX, &bufs[0], &lens[0], why, why_len, assets_wanted[0])) { rc = -1; break; }
    const unsigned char *d0 = (const unsigned char *)bufs[0];
    if(lens[0] < 0x100 || memcmp(d0, "\x4f\x15\x3d\x1d", 4) || !ng_version_of(d0, lens[0], ver, sizeof(ver))) {
      snprintf(why, why_len, "%s ist keine ampr-emu-ng-Datei.", assets_wanted[0]);
      rc = -1;
      break;
    }
    char have[24] = "";
    ps5tm_amprlib_ng_newest(NULL, 0, have, sizeof(have));
#ifdef PS5TM_NG_TEST_HAVE
    snprintf(have, sizeof(have), "%s", PS5TM_NG_TEST_HAVE);     /* a test build pretends that this is the newest version it knows */
#endif
    if(have[0] && ver_cmp(ver, have) <= 0) {
      snprintf(message, sizeof(message), "Die neueste Fassung von AMPR EMU NG (%s) ist schon da.", have);
      break;
    }
    snprintf(url, sizeof(url), NG_LATEST "SHA256SUMS");
    if(fetch_url(url, 16 * 1024, &sums, &sums_n, why, why_len, "SHA256SUMS")) { rc = -1; break; }
    snprintf(url, sizeof(url), NG_LATEST "%s", assets_wanted[1]);
    if(fetch_url(url, NG_MAX, &bufs[1], &lens[1], why, why_len, assets_wanted[1])) { rc = -1; break; }
    for(int i = 0; i < 2; i++) {
      char want[65], hex[65];
      const unsigned char *d = (const unsigned char *)bufs[i];
      if(!sums_find(sums, assets_wanted[i], want)) {
        snprintf(why, why_len, "In SHA256SUMS steht keine Prüfsumme für %s.", assets_wanted[i]);
        rc = -1;
        break;
      }
      sha_hex(d, lens[i], hex);
      if(strcmp(hex, want)) {
        snprintf(why, why_len, "%s: die Prüfsumme passt nicht zu SHA256SUMS der Veröffentlichung %s (ist inzwischen eine neuere erschienen? Bitte noch einmal). Die Datei wurde verworfen.", assets_wanted[i], ver);
        rc = -1;
        break;
      }
      char dv[24] = "";
      if(lens[i] < 0x100 || memcmp(d, "\x4f\x15\x3d\x1d", 4) || !ng_version_of(d, lens[i], dv, sizeof(dv)) || strcmp(dv, ver)) {
        snprintf(why, why_len, "%s ist keine ampr-emu-ng-Datei der Fassung %s.", assets_wanted[i], ver);
        rc = -1;
        break;
      }
    }
    if(rc) break;
    /* both files checked: only now they get their names (nothing half there when the second one is bad) */
    for(int i = 0; i < 2; i++) {
      char name[96], path[300];
      snprintf(name, sizeof(name), "libSceAmpr.sprx-%s-ng%s", ver, i ? "-debug" : "");
      snprintf(path, sizeof(path), NG_DIR "/%s", name);
      int e = ps5tm_put_lib_file(path, (const unsigned char *)bufs[i], lens[i]);
      if(e) {
        snprintf(why, why_len, "%s ließ sich nicht in den Ordner AMPR_EMU legen: %s", name, strerror(e));
        if(i) {                                   /* nothing half there: the first file is taken back, so that the next press loads both again */
          char first[300];
          snprintf(first, sizeof(first), NG_DIR "/libSceAmpr.sprx-%s-ng", ver);
          unlink(first);
          put = 0;
        }
        rc = -1;
        break;
      }
      ps5tm_amprlib_ng_note(name);
      put++;
    }
    if(rc) break;
    pruned = ps5tm_amprlib_ng_prune();
    snprintf(message, sizeof(message), "AMPR EMU NG %s ist geladen und steht jetzt ganz oben. Die älteste Fassung entfällt.", ver);
  } while(0);
  free(sums);
  free(bufs[0]);
  free(bufs[1]);
  pthread_mutex_lock(&g_ng_lock);
  g_ng_busy = 0;
  pthread_mutex_unlock(&g_ng_lock);
  if(rc) {
    PS5TM_WARN("ampr_ng_fetch", "ampr-emu-ng: %s", why);
    *http = 502;
    return NULL;
  }
  if(put) PS5TM_INFO("ampr_ng_fetched", "ampr-emu-ng %s geladen: %u Datei(en) im Ordner AMPR_EMU, %u ältere entfernt.", ver, put, pruned);
  cJSON *o = ps5tm_amprlib_versions("ampr");
  cJSON_AddNumberToObject(o, "installed", put);
  cJSON_AddNumberToObject(o, "pruned", pruned);
  cJSON_AddStringToObject(o, "message", message);
  return o;
}
