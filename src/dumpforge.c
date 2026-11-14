/* Dump Forge (quer3q, GPL-3.0, https://github.com/quer3q/ps5-dump-forge) as the engine of the format "FPKG (Debug-.pkg)" (14.11.2026,
 * wish of the user: the format next to exFAT, ffpkg and ffpfsc in the window of the conversions, "Dump Forge als Motor").
 *
 * Writing a debug FPKG is about 18 000 lines of Rust in Dump Forge (container, PFS image, signature, SHA3 digests, PlayGo, Kraken
 * compression), so it is not ported: the app starts Dump Forge's PS5 program (the release file ps5-dump-forge-<version>-ps5.elf), gives it the
 * job over its interface on 127.0.0.1:8095 (POST /api/start_job, GET /api/jobs, POST /api/cancel_job, POST /api/quit) and shows the
 * progress like that of every other conversion (gameconvert.c). Nothing of Dump Forge's source is in this app.
 *
 * The program is not part of the app (it is 18 MB): it lies in the folder /data/PS5-Cooling-Center/dumpforge. The file can be put there by
 * hand (any name ps5-dump-forge-<version>-ps5.elf or ps5-dump-forge.elf; the newest version counts) or loaded on request from the
 * releases of the project (HTTPS, SHA256SUMS of the release checked). Nothing goes online unless the person presses the button.
 * The two tiles bring a copy in their own folder dumpforge/ (tools/make_starter.sh, tools/fetch-dumpforge.sh): the app looks there as well
 * (/data/homebrew/FAKE86921/dumpforge and /data/homebrew/FAKE86922/dumpforge), so whoever uses a tile has to fetch nothing. Of all places
 * the newest version counts; on the same version the folder of the app comes first. */
#include <dirent.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "lhttp.h"
#include "ps5tm.h"
#include "sha256.h"
#include "third_party/cJSON.h"
#include "titleid.h"
#include "upd_http.h"

#define DF_PORT      8095
#define DF_DIR       PS5TM_DATA_DIR "/dumpforge"
#define DF_TILE_DIR  "/data/homebrew/%s/dumpforge"   /* the folder of a tile: %s is its title id */
/* Dump Forge makes a home-screen tile of its own (PDFG00001, tile.rs) at every start; the app wants none (user, 14.11.2026): before a start
   the place /user/app/PDFG00001 is kept by an empty file, which Dump Forge leaves alone ("is not a folder"), and a tile made earlier goes
   (its record, tile-PDFG00001.owner, says it is Dump Forge's; the older place of the record is /data/ps5-dump-forge). tilekeep.c. */
#define DF_TILE_ID    "PDFG00001"
#define DF_ELF_MAX   (40u * 1024 * 1024)
#define DF_ELF_MIN   (1024u * 1024)
#define DF_STEM      "ps5-dump-forge"

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static int             g_fetching;
static uint64_t        g_started_ms;      /* the last start: a second one within 20 s waits for that one */

/* ------------------------------------------------------------------ the file */

/* The version of a name "ps5-dump-forge-<version>-ps5.elf" ("0.5.5"), "" for "ps5-dump-forge.elf"; -1 when it is neither. */
static int
elf_name_version(const char *name, char *ver, size_t n) {
  ver[0] = 0;
  if(!strcmp(name, DF_STEM ".elf")) return 0;
  size_t stem = strlen(DF_STEM "-");
  size_t l = strlen(name);
  static const char tail[] = "-ps5.elf";
  if(l <= stem + sizeof tail - 1 || strncmp(name, DF_STEM "-", stem) || strcmp(name + l - (sizeof tail - 1), tail)) return -1;
  size_t vl = l - stem - (sizeof tail - 1);
  if(vl == 0 || vl >= n) return -1;
  for(size_t i = 0; i < vl; i++) {
    char c = name[stem + i];
    if(!((c >= '0' && c <= '9') || c == '.')) return -1;
  }
  memcpy(ver, name + stem, vl);
  ver[vl] = 0;
  return 1;
}

/* 1 when version a is newer than b ("0.5.5" against "0.5.4"; "" is the oldest). */
static int
ver_newer(const char *a, const char *b) {
  while(*a || *b) {
    long x = strtol(a, (char **)&a, 10), y = strtol(b, (char **)&b, 10);
    if(x != y) return x > y;
    if(*a == '.') a++;
    if(*b == '.') b++;
  }
  return 0;
}

static int
elf_file_ok(const char *path, size_t *size) {
  struct stat st;
  if(stat(path, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < (off_t)DF_ELF_MIN || st.st_size > (off_t)DF_ELF_MAX) return 0;
  FILE *f = fopen(path, "rb");
  if(!f) return 0;
  unsigned char m[4];
  int ok = fread(m, 1, 4, f) == 4 && !memcmp(m, "\x7f" "ELF", 4);
  fclose(f);
  if(ok && size) *size = (size_t)st.st_size;
  return ok;
}

/* The best program in one folder: its path and version into path/ver when it is newer than what is there (found: 1 when path/ver hold one). */
static int
scan_dir(const char *dir, char *path, size_t pn, char *ver, size_t vn, int found) {
  DIR *d = opendir(dir);
  if(!d) return found;
  struct dirent *e;
  while((e = readdir(d))) {
    char v[40];
    if(elf_name_version(e->d_name, v, sizeof v) < 0) continue;
    char p[300];
    if(snprintf(p, sizeof p, "%s/%s", dir, e->d_name) >= (int)sizeof p || !elf_file_ok(p, NULL)) continue;
    if(!found || ver_newer(v, ver)) {
      found = 1;
      snprintf(ver, vn, "%s", v);
      snprintf(path, pn, "%s", p);
    }
  }
  closedir(d);
  return found;
}

/* The program: its path and version; 1 when there is one (the newest of the files that are ELFs of the right size, in the folder of the app and
   in the folders of the two tiles; the folder of the app wins on the same version). */
static int
find_elf(char *path, size_t pn, char *ver, size_t vn) {
  char best[64] = "";
  int found = scan_dir(DF_DIR, path, pn, best, sizeof best, 0);
  static const char *const tiles[] = { PS5TM_TV_TITLE_ID, PS5TM_BROWSER_TITLE_ID };
  for(size_t i = 0; i < sizeof tiles / sizeof tiles[0]; i++) {
    char dir[160];
    snprintf(dir, sizeof dir, DF_TILE_DIR, tiles[i]);
    found = scan_dir(dir, path, pn, best, sizeof best, found);
  }
  if(found) snprintf(ver, vn, "%s", best);
  return found;
}

/* ------------------------------------------------------------------ state */

/* Dump Forge answers on its port: GET /api/session names it. Its version goes to ver when asked for. */
static int
running_version(char *ver, size_t vn) {
  unsigned char *body = NULL;
  size_t n = 0;
  int st = ps5tm_lhttp(DF_PORT, "GET", "/api/session", NULL, NULL, 0, 1500, &body, &n);
  int ok = 0;
  if(st == 200 && body) {
    cJSON *j = cJSON_Parse((char *)body);
    const cJSON *app = j ? cJSON_GetObjectItem(j, "app") : NULL, *v = j ? cJSON_GetObjectItem(j, "version") : NULL;
    ok = cJSON_IsString(app) && !strcmp(app->valuestring, "ps5-dump-forge");
    if(ok && ver && vn) snprintf(ver, vn, "%s", cJSON_IsString(v) ? v->valuestring : "");
    cJSON_Delete(j);
  }
  free(body);
  return ok;
}

int
ps5tm_dumpforge_running(void) {
  return running_version(NULL, 0);
}

/* 1 when the program is there (path and version out). */
int
ps5tm_dumpforge_installed(char *ver, size_t vn) {
  char path[300];
  return find_elf(path, sizeof path, ver, vn);
}

struct cJSON *
ps5tm_dumpforge_state(void) {
  cJSON *o = cJSON_CreateObject();
  cJSON_AddBoolToObject(o, "ok", 1);
  char ver[40] = "", rv[40] = "", path[300] = "";
  int have = find_elf(path, sizeof path, ver, sizeof ver);
  cJSON_AddBoolToObject(o, "installed", have);
  if(have) cJSON_AddStringToObject(o, "version", ver);
  cJSON_AddStringToObject(o, "folder", DF_DIR);
  int run = running_version(rv, sizeof rv);
  cJSON_AddBoolToObject(o, "running", run);
  if(run) cJSON_AddStringToObject(o, "running_version", rv);
  cJSON_AddStringToObject(o, "project", "https://github.com/quer3q/ps5-dump-forge");
  pthread_mutex_lock(&g_lock);
  cJSON_AddBoolToObject(o, "fetching", g_fetching);
  pthread_mutex_unlock(&g_lock);
  return o;
}

/* ------------------------------------------------------------------ start and stop */

int
ps5tm_dumpforge_start(char *msg, size_t n) {
  if(ps5tm_dumpforge_running()) { snprintf(msg, n, "Dump Forge läuft schon."); return 0; }
  pthread_mutex_lock(&g_lock);
  uint64_t now = ps5tm_mono_ms();
  int again = g_started_ms && now - g_started_ms < 20000;
  if(!again) g_started_ms = now;
  pthread_mutex_unlock(&g_lock);
  if(!again) {
    char path[300], ver[40];
    if(!find_elf(path, sizeof path, ver, sizeof ver)) {
      pthread_mutex_lock(&g_lock); g_started_ms = 0; pthread_mutex_unlock(&g_lock);
      snprintf(msg, n, "Dump Forge ist nicht da. Die Datei ps5-dump-forge-VERSION-ps5.elf gehört in den Ordner %s (oder „Dump Forge laden“ drücken).", DF_DIR);
      return -1;
    }
    static const char *const tile_owner[] = { "/data/dumpforge/tile-" DF_TILE_ID ".owner", "/data/ps5-dump-forge/tile-" DF_TILE_ID ".owner" };
    ps5tm_tile_keep_away(DF_TILE_ID, "Dump Forge", tile_owner, 2);
    size_t len = 0;
    unsigned char *elf = NULL;
    if(elf_file_ok(path, &len)) {
      FILE *f = fopen(path, "rb");
      elf = f ? malloc(len) : NULL;
      if(f && elf && fread(elf, 1, len, f) != len) { free(elf); elf = NULL; }
      if(f) fclose(f);
    }
    if(!elf) {
      pthread_mutex_lock(&g_lock); g_started_ms = 0; pthread_mutex_unlock(&g_lock);
      snprintf(msg, n, "Die Datei von Dump Forge ließ sich nicht lesen.");
      return -1;
    }
    const char *why = NULL;
    int rc = ps5tm_loader_inject_mem(elf, (unsigned long)len, &why);
    free(elf);
    if(rc != 0) {
      pthread_mutex_lock(&g_lock); g_started_ms = 0; pthread_mutex_unlock(&g_lock);
      snprintf(msg, n, "%s", why ? why : "Dump Forge ließ sich nicht senden.");
      return -1;
    }
    PS5TM_INFO("dumpforge_start", "Dump Forge %s an den Payload-Lader gesendet.", ver[0] ? ver : "?");
  }
  for(int i = 0; i < 80; i++) {                    /* it takes a few seconds to bind its port */
    if(ps5tm_dumpforge_running()) { snprintf(msg, n, "Dump Forge läuft."); return 0; }
    usleep(500 * 1000);
  }
  snprintf(msg, n, "Dump Forge hat sich nicht gemeldet.");
  return -1;
}

void
ps5tm_dumpforge_stop(void) {
  if(!ps5tm_dumpforge_running()) return;
  ps5tm_lhttp(DF_PORT, "POST", "/api/quit", "application/json", "{}", 2, 5000, NULL, NULL);
  for(int i = 0; i < 60 && ps5tm_dumpforge_running(); i++) usleep(500 * 1000);    /* it cancels its jobs and cleans up first */
  pthread_mutex_lock(&g_lock);
  g_started_ms = 0;
  pthread_mutex_unlock(&g_lock);
  PS5TM_INFO("dumpforge_stop", "Dump Forge beendet.");
}

/* ------------------------------------------------------------------ the jobs */

/* A job: source (a folder or an image) into the debug FPKG `output`; level "fast", "balanced", "smallest" or "stored". 0 and the job's number,
   else -1 with a sentence. */
int
ps5tm_dumpforge_job_start(const char *source, const char *output, const char *level, int full_verify, int *job, char *err, size_t n) {
  *job = 0;
  cJSON *req = cJSON_CreateObject();
  cJSON_AddStringToObject(req, "source", source);
  cJSON_AddStringToObject(req, "format", "pkg");
  cJSON_AddStringToObject(req, "output", output);
  cJSON_AddNullToObject(req, "compression_threads");
  cJSON_AddNullToObject(req, "inner");
  cJSON_AddBoolToObject(req, "remove_backport", 0);
  cJSON_AddBoolToObject(req, "full_verify", full_verify ? 1 : 0);
  cJSON_AddStringToObject(req, "kraken_level", level && *level ? level : "fast");
  cJSON *body = cJSON_CreateObject();
  cJSON_AddItemToObject(body, "request", req);
  char *text = cJSON_PrintUnformatted(body);
  cJSON_Delete(body);
  if(!text) { snprintf(err, n, "Zu wenig Speicher."); return -1; }
  unsigned char *reply = NULL;
  size_t rn = 0;
  int st = ps5tm_lhttp(DF_PORT, "POST", "/api/start_job", "application/json", text, strlen(text), 30000, &reply, &rn);
  free(text);
  int rc = -1;
  if(st == 200 && reply) {
    cJSON *j = cJSON_Parse((char *)reply);
    if(cJSON_IsNumber(j)) { *job = (int)j->valuedouble; rc = 0; }
    cJSON_Delete(j);
  }
  if(rc != 0) {
    /* Dump Forge's own reason: {"error":"…"} */
    cJSON *j = reply ? cJSON_Parse((char *)reply) : NULL;
    const cJSON *e = j ? cJSON_GetObjectItem(j, "error") : NULL;
    snprintf(err, n, "Dump Forge hat den Auftrag abgelehnt: %s", cJSON_IsString(e) ? e->valuestring : st < 0 ? "keine Antwort" : "ohne Angabe");
    cJSON_Delete(j);
  }
  free(reply);
  return rc;
}

/* The state of job `job` from GET /api/jobs. state: 0 running, 1 done, 2 failed; -1 when the job (or the program) cannot be found. */
int
ps5tm_dumpforge_job_poll(int job, ps5tm_dumpforge_job_t *out) {
  memset(out, 0, sizeof *out);
  out->state = -1;
  unsigned char *body = NULL;
  size_t n = 0;
  int st = ps5tm_lhttp(DF_PORT, "GET", "/api/jobs", NULL, NULL, 0, 6000, &body, &n);
  if(st != 200 || !body) { free(body); return -1; }
  cJSON *root = cJSON_Parse((char *)body);
  free(body);
  const cJSON *jobs = root ? cJSON_GetObjectItem(root, "jobs") : NULL, *x;
  cJSON_ArrayForEach(x, jobs) {
    const cJSON *id = cJSON_GetObjectItem(x, "id");
    if(!cJSON_IsNumber(id) || (int)id->valuedouble != job) continue;
    out->state = 0;
    const cJSON *pr = cJSON_GetObjectItem(x, "progress"), *stages = cJSON_GetObjectItem(x, "stages");
    if(cJSON_IsObject(pr)) {
      const cJSON *s = cJSON_GetObjectItem(pr, "stage"), *d = cJSON_GetObjectItem(pr, "done"), *t = cJSON_GetObjectItem(pr, "total");
      if(cJSON_IsString(s)) snprintf(out->stage, sizeof out->stage, "%s", s->valuestring);
      if(cJSON_IsNumber(d)) out->done = (uint64_t)d->valuedouble;
      if(cJSON_IsNumber(t)) out->total = (uint64_t)t->valuedouble;
    }
    if(cJSON_IsArray(stages)) {
      out->stage_count = cJSON_GetArraySize(stages);
      int i = 0;
      const cJSON *sg;
      cJSON_ArrayForEach(sg, stages) {
        if(cJSON_IsString(sg) && !strcmp(sg->valuestring, out->stage)) out->stage_index = i;
        i++;
      }
    }
    const cJSON *log = cJSON_GetObjectItem(x, "log");
    if(cJSON_IsArray(log) && cJSON_GetArraySize(log) > 0) {
      const cJSON *last = cJSON_GetArrayItem(log, cJSON_GetArraySize(log) - 1);
      if(cJSON_IsString(last)) snprintf(out->log, sizeof out->log, "%s", last->valuestring);
    }
    const cJSON *done = cJSON_GetObjectItem(x, "done");
    if(cJSON_IsObject(done)) {
      const cJSON *res = cJSON_GetObjectItem(done, "result"), *ok = res ? cJSON_GetObjectItem(res, "Ok") : NULL, *er = res ? cJSON_GetObjectItem(res, "Err") : NULL;
      if(cJSON_IsObject(ok)) {
        out->state = 1;
        const cJSON *b = cJSON_GetObjectItem(ok, "bytes"), *o = cJSON_GetObjectItem(ok, "output"), *fl = cJSON_GetObjectItem(ok, "files");
        if(cJSON_IsNumber(b)) out->bytes = (uint64_t)b->valuedouble;
        if(cJSON_IsNumber(fl)) out->files = (unsigned)fl->valuedouble;
        if(cJSON_IsString(o)) snprintf(out->output, sizeof out->output, "%s", o->valuestring);
      } else {
        out->state = 2;
        snprintf(out->error, sizeof out->error, "%s", cJSON_IsString(er) ? er->valuestring : "ohne Angabe");
      }
    }
    cJSON_Delete(root);
    return out->state;
  }
  cJSON_Delete(root);
  return -1;
}

void
ps5tm_dumpforge_job_cancel(int job) {
  char b[48];
  snprintf(b, sizeof b, "{\"id\":%d}", job);
  ps5tm_lhttp(DF_PORT, "POST", "/api/cancel_job", "application/json", b, strlen(b), 10000, NULL, NULL);
}

/* ------------------------------------------------------------------ loading a release */

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
  r.total_ms = 8 * 60 * 1000;
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

static void
sha_hex(const unsigned char *d, size_t n, char out[65]) {
  ps5tm_sha256_t s;
  uint8_t dg[32];
  ps5tm_sha256_init(&s);
  ps5tm_sha256_update(&s, d, n);
  ps5tm_sha256_final(&s, dg);
  ps5tm_sha256_hex(dg, out);
}

/* The newest release's program from GitHub into the folder. 200 (also "already there"), else an HTTP-like status with a sentence. */
int
ps5tm_dumpforge_fetch(char *msg, size_t n) {
  pthread_mutex_lock(&g_lock);
  if(g_fetching) { pthread_mutex_unlock(&g_lock); snprintf(msg, n, "Dump Forge wird gerade schon geladen."); return 409; }
  g_fetching = 1;
  pthread_mutex_unlock(&g_lock);
  char why[300] = "";
  char *rel = NULL, *sums = NULL, *elf = NULL;
  size_t rel_n = 0, sums_n = 0, elf_n = 0;
  int rc = 502;
  cJSON *j = NULL;
  if(fetch_url("https://api.github.com/repos/quer3q/ps5-dump-forge/releases/latest", 512 * 1024, &rel, &rel_n, why, sizeof why, "Die Release-Liste von Dump Forge")) goto done;
  j = cJSON_Parse(rel);
  const cJSON *tag = j ? cJSON_GetObjectItem(j, "tag_name") : NULL, *assets = j ? cJSON_GetObjectItem(j, "assets") : NULL, *a;
  if(!cJSON_IsString(tag) || strlen(tag->valuestring) > 30) { snprintf(why, sizeof why, "Die Antwort von GitHub war nicht lesbar."); goto done; }
  char ver[40];
  snprintf(ver, sizeof ver, "%s", tag->valuestring[0] == 'v' ? tag->valuestring + 1 : tag->valuestring);
  for(const char *c = ver; *c; c++)
    if(!((*c >= '0' && *c <= '9') || *c == '.')) { snprintf(why, sizeof why, "Der Versionsname der Veröffentlichung ist ungewöhnlich."); goto done; }
  char name[96];
  snprintf(name, sizeof name, DF_STEM "-%s-ps5.elf", ver);
  char have_ver[40] = "", have_path[300];
  if(find_elf(have_path, sizeof have_path, have_ver, sizeof have_ver) && !ver_newer(ver, have_ver)) {
    snprintf(msg, n, "Die neueste Fassung (%s) ist schon da.", ver);
    rc = 200;
    goto done;
  }
  char elf_url[400] = "", sums_url[400] = "";
  cJSON_ArrayForEach(a, assets) {
    const cJSON *nm = cJSON_GetObjectItem(a, "name"), *u = cJSON_GetObjectItem(a, "browser_download_url");
    if(!cJSON_IsString(nm) || !cJSON_IsString(u) || strlen(u->valuestring) >= 400) continue;
    if(!strcmp(nm->valuestring, name)) snprintf(elf_url, sizeof elf_url, "%s", u->valuestring);
    else if(!strcmp(nm->valuestring, "SHA256SUMS")) snprintf(sums_url, sizeof sums_url, "%s", u->valuestring);
  }
  if(!elf_url[0] || !sums_url[0]) { snprintf(why, sizeof why, "Die Veröffentlichung %s hat nicht die erwarteten Dateien.", ver); goto done; }
  if(fetch_url(sums_url, 8192, &sums, &sums_n, why, sizeof why, "SHA256SUMS")) goto done;
  char want[65] = "";
  for(const char *p = sums; *p;) {
    const char *e = strchr(p, '\n');
    size_t l = e ? (size_t)(e - p) : strlen(p);
    const char *hit = strstr(p, name);
    if(l >= 66 && hit && hit < p + l) {
      int ok = 1;
      for(int i = 0; i < 64; i++) if(!((p[i] >= '0' && p[i] <= '9') || (p[i] >= 'a' && p[i] <= 'f'))) ok = 0;
      if(ok) { memcpy(want, p, 64); want[64] = 0; }
    }
    if(!e) break;
    p = e + 1;
  }
  if(!want[0]) { snprintf(why, sizeof why, "In SHA256SUMS steht keine Prüfsumme für %s.", name); goto done; }
  if(fetch_url(elf_url, DF_ELF_MAX, &elf, &elf_n, why, sizeof why, name)) goto done;
  char got[65];
  sha_hex((const unsigned char *)elf, elf_n, got);
  if(strcmp(got, want)) { snprintf(why, sizeof why, "Die Prüfsumme von %s passt nicht zu SHA256SUMS. Die Datei wurde verworfen.", name); goto done; }
  if(elf_n < DF_ELF_MIN || memcmp(elf, "\x7f" "ELF", 4)) { snprintf(why, sizeof why, "%s ist keine ELF-Datei.", name); goto done; }
  if(mkdir(DF_DIR, 0777) != 0 && errno != EEXIST) { snprintf(why, sizeof why, "Der Ordner %s ließ sich nicht anlegen: %s", DF_DIR, strerror(errno)); goto done; }
  char path[300];
  snprintf(path, sizeof path, DF_DIR "/%s", name);
  int e = ps5tm_put_lib_file(path, (const unsigned char *)elf, elf_n);
  if(e) { snprintf(why, sizeof why, "Die Datei ließ sich nicht ablegen: %s", strerror(e)); goto done; }
  PS5TM_INFO("dumpforge_fetched", "Dump Forge %s geladen (%zu Bytes, SHA-256 geprüft).", ver, elf_n);
  snprintf(msg, n, "Dump Forge %s ist geladen.", ver);
  rc = 200;
done:
  if(why[0]) {
    PS5TM_WARN("dumpforge_fetch", "%s", why);
    snprintf(msg, n, "%s", why);
  }
  cJSON_Delete(j);
  free(rel); free(sums); free(elf);
  pthread_mutex_lock(&g_lock);
  g_fetching = 0;
  pthread_mutex_unlock(&g_lock);
  return rc;
}
