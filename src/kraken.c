/* FPKGMount: Kraken Mount (quer3q, GPL-3.0, https://github.com/quer3q/kraken-mount) as a game mounter next to PS4Mount (13.11.2026, wish of
 * the user: "(F)PKG games mounted like PS4Mount, but with the kraken payload").
 *
 * Kraken Mount is a program of its own (Rust, with a reader for debug FPKGs and its Kraken block decoder), so it is not ported: the
 * app carries the released ELF (third_party/kraken_mount, packed by tools/gen_blob.py), starts it through the loader on
 * 127.0.0.1:9021 and runs the page on top of its interface on 127.0.0.1:8097 (docs/DESIGN of the project: GET /api/state, POST
 * /api/rescan, /api/rebuild/<TID>, /api/rebuild-all, /api/delete/<TID>, /api/quit, GET /icon/<TID>). Nothing of its source is in
 * this app; the app never changes its settings (they stay in its own page on port 8097). The tile of its own that the program makes at
 * every start is kept away (keep_tile_away): there is only the page in the app, as with PS4Mount.
 *
 * A newer release comes only when the person presses the button: the release list of the project (api.github.com), the ELF and
 * SHA256SUMS of the release over HTTPS (the app's own client, GitHub's roots only); the file is kept only when its SHA-256
 * matches SHA256SUMS of the same release and it is an ELF that carries the name "krakenmount". The next start then uses it
 * instead of the embedded one. */
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#include "lhttp.h"
#include "ps5tm.h"
#include "sha256.h"
#include "third_party/cJSON.h"
#include "upd_http.h"

#define KR_PORT              8097
#define KR_EMBEDDED_VERSION  "0.0.1-beta"
#define KR_PROJECT           "https://github.com/quer3q/kraken-mount"
#define KR_DIR               PS5TM_DATA_DIR "/kraken"
#define PS5TM_KRAKEN_DATA    "/data/krakenmount"
#define KR_ELF               KR_DIR "/kraken-mount.elf"
#define KR_VER               KR_DIR "/version.txt"
#define KR_ELF_MAX           (16u * 1024 * 1024)

extern const unsigned char ps5tm_kraken[];
extern const unsigned long ps5tm_kraken_len;          /* the zlib stream of the ELF (tools/gen_blob.py) */
extern const unsigned long ps5tm_kraken_raw_len;

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static int             g_busy;                        /* a download runs */
static uint64_t        g_started_ms;                  /* the last start: a second one within 15 s is not sent */

/* ------------------------------------------------------------------ loopback client */

/* One request to Kraken Mount on 127.0.0.1:8097 (lhttp.c): the HTTP status, or -1 when nothing answers. */
static int
kr_http(const char *method, const char *path, const char *body, size_t body_len, int timeout_ms, unsigned char **out, size_t *out_len) {
  return ps5tm_lhttp(KR_PORT, method, path, NULL, body, body_len, timeout_ms, out, out_len);
}

/* ------------------------------------------------------------------ files */

static int
read_file(const char *path, unsigned char **out, size_t *len, size_t max) {
  *out = NULL;
  *len = 0;
  FILE *f = fopen(path, "rb");
  if(!f) return -1;
  unsigned char *b = malloc(max + 1);
  size_t n = b ? fread(b, 1, max + 1, f) : 0;
  fclose(f);
  if(!b || n == 0 || n > max) { free(b); return -1; }
  *out = b;
  *len = n;
  return 0;
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

static int
elf_ok(const unsigned char *d, size_t n) {
  return n > 0x1000 && !memcmp(d, "\x7f" "ELF", 4) && has_text(d, n, "krakenmount");
}

/* The version of the downloaded copy ("" when there is none, or it is damaged). */
static void
fetched_version(char *v, size_t n) {
  v[0] = 0;
  struct stat st;
  if(stat(KR_ELF, &st) != 0 || st.st_size < 0x1000) return;
  FILE *f = fopen(KR_VER, "r");
  if(!f) return;
  if(fgets(v, (int)n, f)) {
    size_t l = strlen(v);
    while(l && (v[l - 1] == '\n' || v[l - 1] == '\r' || v[l - 1] == ' ')) v[--l] = 0;
  }
  fclose(f);
  for(const char *c = v; *c; c++)
    if(!((*c >= '0' && *c <= '9') || (*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || *c == '.' || *c == '-' || *c == '_')) { v[0] = 0; return; }
}

/* ------------------------------------------------------------------ state */

static int
valid_tid(const char *t) {
  if(!t || strlen(t) != 9) return 0;
  for(int i = 0; i < 4; i++) if(t[i] < 'A' || t[i] > 'Z') return 0;
  for(int i = 4; i < 9; i++) if(t[i] < '0' || t[i] > '9') return 0;
  return 1;
}

struct cJSON *
ps5tm_kraken_state(void) {
  cJSON *o = cJSON_CreateObject();
  cJSON_AddBoolToObject(o, "ok", 1);
  cJSON_AddStringToObject(o, "embedded_version", KR_EMBEDDED_VERSION);
  cJSON_AddStringToObject(o, "project", KR_PROJECT);
  char fv[48];
  fetched_version(fv, sizeof fv);
  if(fv[0]) cJSON_AddStringToObject(o, "fetched_version", fv);
  cJSON_AddStringToObject(o, "start_version", fv[0] ? fv : KR_EMBEDDED_VERSION);
  pthread_mutex_lock(&g_lock);
  cJSON_AddBoolToObject(o, "fetching", g_busy);
  pthread_mutex_unlock(&g_lock);
  unsigned char *body = NULL;
  size_t n = 0;
  int st = kr_http("GET", "/api/state", NULL, 0, 2000, &body, &n);
  cJSON *k = (st == 200 && body) ? cJSON_Parse((char *)body) : NULL;
  free(body);
  if(cJSON_IsObject(k)) {
    cJSON_AddBoolToObject(o, "running", 1);
    cJSON_AddItemToObject(o, "kraken", k);
    /* its settings: the page names the folders it searches (scanpath; empty = its own defaults) */
    st = kr_http("GET", "/api/settings", NULL, 0, 2000, &body, &n);
    cJSON *set = (st == 200 && body) ? cJSON_Parse((char *)body) : NULL;
    free(body);
    if(cJSON_IsObject(set)) cJSON_AddItemToObject(o, "settings", set);
    else cJSON_Delete(set);
  } else {
    cJSON_Delete(k);
    cJSON_AddBoolToObject(o, "running", 0);
    pthread_mutex_lock(&g_lock);
    int starting = g_started_ms && ps5tm_mono_ms() - g_started_ms < 20000;
    pthread_mutex_unlock(&g_lock);
    cJSON_AddBoolToObject(o, "starting", starting);
  }
  return o;
}

int
ps5tm_kraken_icon(const char *id, unsigned char **png, size_t *len) {
  *png = NULL;
  *len = 0;
  if(!valid_tid(id)) return -1;
  char path[40];
  snprintf(path, sizeof path, "/icon/%s", id);
  unsigned char *b = NULL;
  size_t n = 0;
  int st = kr_http("GET", path, NULL, 0, 4000, &b, &n);
  if(st != 200 || !b || n < 8 || memcmp(b, "\x89PNG", 4)) { free(b); return -1; }
  *png = b;
  *len = n;
  return 0;
}

/* ------------------------------------------------------------------ no tile of its own */

/* Kraken Mount puts a home-screen tile of its own (KRKN00001, for its web page) at every start. Here the page FPKGMount is the
   way in, as the page "PS4 PKG von USB" is for PS4Mount, so there is to be no tile (user, 13.11.2026). The program leaves the title's
   folder alone when /user/app/KRKN00001 exists and its own marker (/data/krakenmount/tile.ok) does not (register.rs ensure_tile),
   so before every start: a tile it made earlier goes (the console's own uninstall, then the marker), and a plain empty file takes the
   place of the folder. */
#define KR_TILE_ID      "KRKN00001"
#define KR_TILE_MARKER  "/data/krakenmount/tile.ok"

static void
keep_tile_away(void) {
  static const char *const markers[] = { KR_TILE_MARKER };
  ps5tm_tile_keep_away(KR_TILE_ID, "Kraken Mount", markers, 1);
}

/* ------------------------------------------------------------------ search folders */

/* Kraken Mount searches /data/homebrew, /data/etaHEN/games and the roots of the drives by itself, where ShadowMountPlus searches too:
   every game folder there would be registered twice. The app therefore gives it folders of its own, FPKG-Games on the internal storage
   and on each drive (user, 13.11.2026: "getrennte Ordner, beide laufen"), and writes them into its config when there is none yet.
   An existing config is never touched (the page of Kraken Mount on port 8097 changes it). A drive that is not there costs nothing. */
#define KR_CONFIG      PS5TM_KRAKEN_DATA "/config.toml"
#define KR_FPKG_FOLDER "FPKG-Games"

static int make_dir(const char *p);

static void
seed_config(void) {
  struct stat st;
  if(stat(KR_CONFIG, &st) == 0) return;
  make_dir(PS5TM_KRAKEN_DATA);
  make_dir("/data/" KR_FPKG_FOLDER);
  char buf[900];
  size_t o = (size_t)snprintf(buf, sizeof buf, "# search folders set by the app (page FPKGMount); ShadowMountPlus does not search them\nscanpath = [\"/data/" KR_FPKG_FOLDER "\"");
  for(int i = 0; i < 8 && o < sizeof buf; i++) o += (size_t)snprintf(buf + o, sizeof buf - o, ", \"/mnt/usb%d/" KR_FPKG_FOLDER "\"", i);
  for(int i = 0; i < 2 && o < sizeof buf; i++) o += (size_t)snprintf(buf + o, sizeof buf - o, ", \"/mnt/ext%d/" KR_FPKG_FOLDER "\"", i);
  if(o < sizeof buf) o += (size_t)snprintf(buf + o, sizeof buf - o, "]\n");
  if(o >= sizeof buf) return;
  FILE *f = fopen(KR_CONFIG, "w");
  if(!f) { PS5TM_WARN("kraken_config", "%s ließ sich nicht anlegen: %s", KR_CONFIG, strerror(errno)); return; }
  fputs(buf, f);
  fclose(f);
  PS5TM_INFO("kraken_config", "Suchordner für Kraken Mount angelegt: /data/%s und %s auf den Laufwerken.", KR_FPKG_FOLDER, KR_FPKG_FOLDER);
}

/* ------------------------------------------------------------------ actions */

static int
start_kraken(char *msg, size_t n) {
  if(kr_http("GET", "/api/state", NULL, 0, 1500, NULL, NULL) == 200) { snprintf(msg, n, "Kraken Mount läuft schon."); return 409; }
  pthread_mutex_lock(&g_lock);
  uint64_t now = ps5tm_mono_ms();
  int again = g_started_ms && now - g_started_ms < 15000;
  if(!again) g_started_ms = now;
  pthread_mutex_unlock(&g_lock);
  if(again) { snprintf(msg, n, "Kraken Mount wird gerade gestartet. Das dauert einige Sekunden."); return 409; }
  keep_tile_away();
  seed_config();
  unsigned char *elf = NULL;
  size_t elf_len = 0;
  char fv[48];
  fetched_version(fv, sizeof fv);
  const char *which = KR_EMBEDDED_VERSION;
  if(fv[0] && read_file(KR_ELF, &elf, &elf_len, KR_ELF_MAX) == 0 && elf_ok(elf, elf_len)) which = fv;
  else { free(elf); elf = NULL; }
  if(!elf) {
    elf = ps5tm_blob_inflate(ps5tm_kraken, ps5tm_kraken_len, ps5tm_kraken_raw_len, &elf_len);
    if(!elf) {
      pthread_mutex_lock(&g_lock); g_started_ms = 0; pthread_mutex_unlock(&g_lock);
      snprintf(msg, n, "Kraken Mount ließ sich nicht entpacken (zu wenig Speicher).");
      return 500;
    }
  }
  const char *why = NULL;
  int rc = ps5tm_loader_inject_mem(elf, (unsigned long)elf_len, &why);
  free(elf);
  if(rc != 0) {
    pthread_mutex_lock(&g_lock); g_started_ms = 0; pthread_mutex_unlock(&g_lock);
    snprintf(msg, n, "%s", why ? why : "Kraken Mount ließ sich nicht senden.");
    return 502;
  }
  PS5TM_INFO("kraken_start", "Kraken Mount %s an den Payload-Lader gesendet.", which);
  snprintf(msg, n, "Kraken Mount %s wird gestartet. Das dauert einige Sekunden.", which);
  return 200;
}

static int
stop_kraken(char *msg, size_t n) {
  int st = kr_http("POST", "/api/quit", "shutdown", 8, 5000, NULL, NULL);
  if(st == 204 || st == 200) { snprintf(msg, n, "Kraken Mount wird beendet."); PS5TM_INFO("kraken_stop", "Kraken Mount beendet (Seite FPKGMount)."); return 200; }
  if(st == 409) { snprintf(msg, n, "Ein Spiel läuft gerade. Kraken Mount lässt sich erst nach dem Spiel beenden."); return 409; }
  if(st < 0) { snprintf(msg, n, "Kraken Mount läuft nicht."); return 409; }
  snprintf(msg, n, "Kraken Mount hat das Beenden abgelehnt (%d).", st);
  return 502;
}

/* Is there a game with this title id and this path in Kraken Mount's list? Then the delete may go on. */
static int
game_known(const char *id, const char *path) {
  unsigned char *body = NULL;
  size_t bn = 0;
  int st = kr_http("GET", "/api/state", NULL, 0, 3000, &body, &bn);
  cJSON *k = (st == 200 && body) ? cJSON_Parse((char *)body) : NULL;
  free(body);
  int found = 0;
  const cJSON *games = k ? cJSON_GetObjectItem(k, "games") : NULL, *g;
  cJSON_ArrayForEach(g, games) {
    const cJSON *t = cJSON_GetObjectItem(g, "title_id"), *p = cJSON_GetObjectItem(g, "path");
    if(cJSON_IsString(t) && cJSON_IsString(p) && !strcmp(t->valuestring, id) && !strcmp(p->valuestring, path)) { found = 1; break; }
  }
  cJSON_Delete(k);
  return found;
}

static int
simple_post(const char *path, const char *body, const char *ok_msg, char *msg, size_t n) {
  unsigned char *reply = NULL;
  size_t rn = 0;
  int st = kr_http("POST", path, body, body ? strlen(body) : 0, 20000, &reply, &rn);
  int rc;
  if(st == 204 || st == 200) { snprintf(msg, n, "%s", ok_msg); rc = 200; }
  else if(st < 0) { snprintf(msg, n, "Kraken Mount läuft nicht."); rc = 409; }
  else {
    /* Kraken Mount's own reason, when it gave one (a short line of text) */
    char why[160] = "";
    if(reply && rn && rn < sizeof why) {
      snprintf(why, sizeof why, "%s", (char *)reply);
      for(char *c = why; *c; c++) if((unsigned char)*c < 0x20) *c = ' ';
    }
    if(st == 404) snprintf(msg, n, "Kraken Mount kennt dieses Spiel nicht (mehr).");
    else if(st == 409) snprintf(msg, n, "Kraken Mount kann das gerade nicht: Es arbeitet oder ein Spiel läuft.%s%s", why[0] ? " " : "", why);
    else snprintf(msg, n, "Kraken Mount hat das abgelehnt (%d)%s%s.", st, why[0] ? ": " : "", why);
    rc = st == 404 ? 404 : st == 409 ? 409 : 502;
  }
  free(reply);
  return rc;
}

/* ------------------------------------------------------------------ download of a newer release */

static int
hop_ok(const char *host, int https, void *ctx) {
  (void)ctx;
  if(!https) return 0;
  if(!strcasecmp(host, "github.com") || !strcasecmp(host, "api.github.com")) return 1;
  size_t n = strlen(host);
  static const char tail[] = ".githubusercontent.com";
  return n > sizeof tail - 1 && !strcasecmp(host + n - (sizeof tail - 1), tail);
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
  r.total_ms = 3 * 60 * 1000;
  r.range_from = -1;
  r.range_to = -1;
  upd_res_t res;
  if(upd_http_fetch(&r, max, buf, len, &res) != 0) {
    snprintf(why, why_len, "%s ließ sich nicht laden (%s%s%d).", what, res.step[0] ? res.step : "http", res.status ? " " : "", res.status);
    free(*buf);
    *buf = NULL;
    return -1;
  }
  return 0;
}

static int
make_dir(const char *p) {
  return mkdir(p, 0777) == 0 || errno == EEXIST ? 0 : -1;
}

static int
fetch_release(char *msg, size_t n) {
  char why[300] = "";
  char *rel = NULL, *sums = NULL, *elf = NULL;
  size_t rel_n = 0, sums_n = 0, elf_n = 0;
  int rc = 502;
  cJSON *j = NULL;
  if(fetch_url("https://api.github.com/repos/quer3q/kraken-mount/releases/latest", 512 * 1024, &rel, &rel_n, why, sizeof why, "Die Release-Liste von Kraken Mount")) goto done;
  j = cJSON_Parse(rel);
  const cJSON *tag = j ? cJSON_GetObjectItem(j, "tag_name") : NULL, *assets = j ? cJSON_GetObjectItem(j, "assets") : NULL, *a;
  if(!cJSON_IsString(tag) || strlen(tag->valuestring) > 40) { snprintf(why, sizeof why, "Die Antwort von GitHub war nicht lesbar."); goto done; }
  char ver[48];
  snprintf(ver, sizeof ver, "%s", tag->valuestring[0] == 'v' ? tag->valuestring + 1 : tag->valuestring);
  for(const char *c = ver; *c; c++)
    if(!((*c >= '0' && *c <= '9') || (*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || *c == '.' || *c == '-' || *c == '_')) { snprintf(why, sizeof why, "Der Versionsname der Veröffentlichung ist ungewöhnlich."); goto done; }
  char fv[48];
  fetched_version(fv, sizeof fv);
  if(!strcmp(ver, fv) || (!fv[0] && !strcmp(ver, KR_EMBEDDED_VERSION))) {
    snprintf(msg, n, "Die neueste Fassung (%s) ist schon da.", ver);
    rc = 200;
    goto done;
  }
  char elf_url[400] = "", sums_url[400] = "";
  cJSON_ArrayForEach(a, assets) {
    const cJSON *nm = cJSON_GetObjectItem(a, "name"), *u = cJSON_GetObjectItem(a, "browser_download_url");
    if(!cJSON_IsString(nm) || !cJSON_IsString(u) || strlen(u->valuestring) >= 400) continue;
    if(!strcmp(nm->valuestring, "kraken-mount.elf")) snprintf(elf_url, sizeof elf_url, "%s", u->valuestring);
    else if(!strcmp(nm->valuestring, "SHA256SUMS")) snprintf(sums_url, sizeof sums_url, "%s", u->valuestring);
  }
  if(!elf_url[0] || !sums_url[0]) { snprintf(why, sizeof why, "Die Veröffentlichung %s hat nicht die erwarteten Dateien.", ver); goto done; }
  if(fetch_url(sums_url, 4096, &sums, &sums_n, why, sizeof why, "SHA256SUMS")) goto done;
  char want[65] = "";
  for(const char *p = sums; *p;) {
    const char *e = strchr(p, '\n');
    size_t l = e ? (size_t)(e - p) : strlen(p);
    if(l >= 66 && strstr(p, "kraken-mount.elf") && strstr(p, "kraken-mount.elf") < p + l) {
      int ok = 1;
      for(int i = 0; i < 64; i++) if(!((p[i] >= '0' && p[i] <= '9') || (p[i] >= 'a' && p[i] <= 'f'))) ok = 0;
      if(ok) { memcpy(want, p, 64); want[64] = 0; }
    }
    if(!e) break;
    p = e + 1;
  }
  if(!want[0]) { snprintf(why, sizeof why, "In SHA256SUMS steht keine Prüfsumme für kraken-mount.elf."); goto done; }
  if(fetch_url(elf_url, KR_ELF_MAX, &elf, &elf_n, why, sizeof why, "kraken-mount.elf")) goto done;
  char got[65];
  sha_hex((const unsigned char *)elf, elf_n, got);
  if(strcmp(got, want)) { snprintf(why, sizeof why, "Die Prüfsumme von kraken-mount.elf passt nicht zu SHA256SUMS. Die Datei wurde verworfen."); goto done; }
  if(!elf_ok((const unsigned char *)elf, elf_n)) { snprintf(why, sizeof why, "kraken-mount.elf ist keine Datei von Kraken Mount."); goto done; }
  if(make_dir(KR_DIR) != 0) { snprintf(why, sizeof why, "Der Ordner %s ließ sich nicht anlegen: %s", KR_DIR, strerror(errno)); goto done; }
  int e = ps5tm_put_lib_file(KR_ELF, (const unsigned char *)elf, elf_n);
  if(e) { snprintf(why, sizeof why, "Die Datei ließ sich nicht ablegen: %s", strerror(e)); goto done; }
  FILE *vf = fopen(KR_VER, "w");
  if(!vf || fprintf(vf, "%s\n", ver) < 0) { if(vf) fclose(vf); remove(KR_ELF); snprintf(why, sizeof why, "Die Versionsdatei ließ sich nicht schreiben."); goto done; }
  fclose(vf);
  PS5TM_INFO("kraken_fetched", "Kraken Mount %s geladen (%zu Bytes, SHA-256 geprüft).", ver, elf_n);
  snprintf(msg, n, "Kraken Mount %s ist geladen. Der nächste Start nimmt diese Fassung.", ver);
  rc = 200;
done:
  if(rc != 200 || why[0]) {
    if(why[0]) {
      PS5TM_WARN("kraken_fetch", "%s", why);
      snprintf(msg, n, "%s", why);
    }
  }
  cJSON_Delete(j);
  free(rel); free(sums); free(elf);
  return rc;
}

/* ------------------------------------------------------------------ entry */

int
ps5tm_kraken_action(const struct cJSON *b, char *msg, size_t n) {
  msg[0] = 0;
  const cJSON *ja = cJSON_GetObjectItem(b, "action"), *jid = cJSON_GetObjectItem(b, "id"), *jp = cJSON_GetObjectItem(b, "path");
  if(!cJSON_IsString(ja)) { snprintf(msg, n, "Es fehlt die Aktion."); return 400; }
  const char *a = ja->valuestring;
  const char *id = cJSON_IsString(jid) ? jid->valuestring : "";
  if(!strcmp(a, "start")) return start_kraken(msg, n);
  if(!strcmp(a, "stop")) return stop_kraken(msg, n);
  if(!strcmp(a, "rescan")) return simple_post("/api/rescan", NULL, "Kraken Mount sucht neu.", msg, n);
  if(!strcmp(a, "rebuild_all")) return simple_post("/api/rebuild-all", NULL, "Kraken Mount baut alle Spiele neu auf.", msg, n);
  if(!strcmp(a, "rebuild")) {
    if(!valid_tid(id)) { snprintf(msg, n, "Die Titel-ID ist ungültig."); return 400; }
    char path[40];
    snprintf(path, sizeof path, "/api/rebuild/%s", id);
    return simple_post(path, NULL, "Das Spiel wird neu aufgebaut.", msg, n);
  }
  if(!strcmp(a, "delete")) {
    if(!valid_tid(id) || !cJSON_IsString(jp) || !jp->valuestring[0] || strlen(jp->valuestring) > 1000) { snprintf(msg, n, "Titel-ID oder Pfad fehlen."); return 400; }
    if(!game_known(id, jp->valuestring)) { snprintf(msg, n, "Dieses Spiel steht nicht (mehr) in der Liste von Kraken Mount. Bitte „Aktualisieren“ drücken."); return 409; }
    char path[40];
    snprintf(path, sizeof path, "/api/delete/%s", id);
    int rc = simple_post(path, jp->valuestring, "Das Spiel ist gelöscht.", msg, n);
    if(rc == 200) PS5TM_INFO("kraken_delete", "Spiel %s über Kraken Mount gelöscht: %s", id, jp->valuestring);
    return rc;
  }
  if(!strcmp(a, "fetch")) {
    pthread_mutex_lock(&g_lock);
    if(g_busy) { pthread_mutex_unlock(&g_lock); snprintf(msg, n, "Kraken Mount wird gerade schon geladen."); return 409; }
    g_busy = 1;
    pthread_mutex_unlock(&g_lock);
    int rc = fetch_release(msg, n);
    pthread_mutex_lock(&g_lock);
    g_busy = 0;
    pthread_mutex_unlock(&g_lock);
    return rc;
  }
  snprintf(msg, n, "Diese Aktion kennt die App nicht.");
  return 400;
}
