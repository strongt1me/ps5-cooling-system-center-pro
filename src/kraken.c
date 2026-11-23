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
#include <sys/sysctl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include "krakenboot.h"
#include "lhttp.h"
#include "ps5tm.h"
#include "sha256.h"
#include "third_party/cJSON.h"
#include "upd_http.h"

#define KR_PORT              8097
#define KR_EMBEDDED_VERSION  "0.3.1"
#define KR_PROJECT           "https://github.com/quer3q/kraken-mount"
#define KR_DIR               PS5TM_DATA_DIR "/kraken"
#define PS5TM_KRAKEN_DATA    "/data/krakenmount"
#define KR_ELF               KR_DIR "/kraken-mount.elf"
#define KR_VER               KR_DIR "/version.txt"
#define KR_BAD               KR_DIR "/bad.txt"        /* a downloaded version that did not answer after its start: not used again */
#define KR_STATE             KR_DIR "/zustand.txt"    /* "an" or "aus": what the person chose in the page; without the file Kraken Mount is off (since 21.11.2026) */
#define KR_OFF               KR_DIR "/aus.txt"        /* the marker of the rule until 21.11.2026 ("switched off"): no longer read, removed when the state is written */
#define KR_PENDING           KR_DIR "/start-pending.txt"  /* written right before the app sends Kraken Mount with itself ("<version> <uptime of the console in seconds>"), gone when it has answered or the app has lived on for a minute */
#define KR_BOOT_DELAY_SEC    5                        /* the app starts Kraken Mount this long after its own start (krakenboot.h) */
#define KR_ELF_MAX           (16u * 1024 * 1024)

extern const unsigned char ps5tm_kraken[];
extern const unsigned long ps5tm_kraken_len;          /* the zlib stream of the ELF (tools/gen_blob.py) */
extern const unsigned long ps5tm_kraken_raw_len;

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static int             g_busy;                        /* a download runs */
static uint64_t        g_started_ms;                  /* the last start: a second one within 15 s is not sent */
static int             g_boot_pending;                /* the app has not yet had its first look at Kraken Mount (the start with the app): the page says "starts" */
static uint64_t        g_quit_ms;                     /* the last "quit" it accepted: it answers for a few more seconds, the page says "stopping" (17.11.2026) */
static int             g_boot_sending;                /* the start with the app is at the loader just now: a "stop" in these seconds cannot take it back (audit 17.11.2026) */
static uint64_t        g_boot_sent_ms;                /* when the start with the app was handed to the loader (0 = none): Kraken Mount may not be listening yet for a few seconds */
static int             g_pending_active;              /* KR_PENDING was written and has not been taken back yet */
#define KR_QUIT_WINDOW_MS    20000u                   /* so long after the quit an answer still counts as "on the way out" */

/* ------------------------------------------------------------------ loopback client */

/* One request to Kraken Mount on 127.0.0.1:8097 (lhttp.c): the HTTP status, or -1 when nothing answers. */
static int
kr_http(const char *method, const char *path, const char *body, size_t body_len, int timeout_ms, unsigned char **out, size_t *out_len) {
  return ps5tm_lhttp(KR_PORT, method, path, NULL, body, body_len, timeout_ms, out, out_len);
}

/* ------------------------------------------------------------------ files */

static int make_dir(const char *p);

/* Kraken Mount is off unless the person switched it on in the page (21.11.2026, wish of the user; from 17.11. it was on unless switched off). The choice is
   kept in a file either way, "an" or "aus", as PS4Mount keeps its enabled= in config.txt: a console without the file has it off. */
static int
kr_is_on(void) {
  char line[16] = "";
  FILE *f = fopen(KR_STATE, "r");
  if(!f) return 0;
  if(!fgets(line, sizeof line, f)) line[0] = 0;
  fclose(f);
  return ps5tm_krakenboot_parse_state(line);
}

/* The choice is written whole under another name and put in place, so that a half-written file never turns a "switched off" into something else. */
static void
kr_set_on(int on) {
  make_dir(KR_DIR);
  FILE *f = fopen(KR_STATE ".tmp", "w");
  int ok = f && fputs(on ? "an\n" : "aus\n", f) >= 0;
  if(f && fclose(f) != 0) ok = 0;
  if(ok && rename(KR_STATE ".tmp", KR_STATE) == 0) {
    unlink(KR_OFF);                                        /* the marker of the old rule: the state file says it now */
    return;
  }
  PS5TM_WARN("kraken_state_marker", "Die Einstellung für Kraken Mount (%s) ließ sich nicht merken (%s): %s", on ? "an" : "aus", KR_STATE, strerror(errno));
  unlink(KR_STATE ".tmp");
}

/* Seconds since the console started (the monotonic clock counts from the boot; it does not move when the clock is set, as the boot time of the system does). */
static unsigned long long
kr_uptime_s(void) {
  return (unsigned long long)(ps5tm_mono_ms() / 1000u);
}

/* The boot time of the system (kern.boottime, seconds since 1970); 0 when it cannot be read. */
static long long
kr_boot_time_s(void) {
  struct timeval bt;
  size_t sz = sizeof(bt);
  if(sysctlbyname("kern.boottime", &bt, &sz, NULL, 0) != 0) return 0;
  return (long long)bt.tv_sec;
}

/* Protection against a start loop (audit 17.11.2026): a Kraken Mount that takes the console down would be sent again at every start of the app, 5 seconds
   after it. Right before the app sends it with itself, the version and the uptime of the console go into KR_PENDING; it is removed when Kraken Mount has
   answered, or when the app has lived on for a minute (a console that went down does not do that). Found at the next start of the app with a SMALLER uptime
   than the one written, the console went down in between (the app starts at about the same time after every boot, a minute or so, and the marker is written
   five seconds after that): a downloaded version then goes to bad.txt (the embedded one takes its place), the embedded one switches the start with the app off -
   the person can switch it on again in the page. */
static void
pending_write(const char *version) {
  make_dir(KR_DIR);
  FILE *f = fopen(KR_PENDING ".tmp", "w");
  int ok = f && fprintf(f, "%s %llu %lld\n", version, kr_uptime_s(), kr_boot_time_s()) > 0;
  /* On the disk before the send: the marker is for the case that the console goes down seconds later, and a file that is only in the cache would be gone. */
  if(f && (fflush(f) != 0 || fsync(fileno(f)) != 0)) ok = 0;
  if(f && fclose(f) != 0) ok = 0;
  if(ok && rename(KR_PENDING ".tmp", KR_PENDING) == 0) {
    __atomic_store_n(&g_pending_active, 1, __ATOMIC_RELAXED);
  } else {
    PS5TM_WARN("kraken_pending", "Die Markierung für den Start von Kraken Mount mit der App ließ sich nicht schreiben (%s): Der Start läuft ohne den Schutz vor einer Startschleife.", strerror(errno));
    unlink(KR_PENDING ".tmp");
  }
}

static void
pending_clear(void) {
  if(__atomic_exchange_n(&g_pending_active, 0, __ATOMIC_RELAXED)) unlink(KR_PENDING);
}

static void
pending_check(void) {
  FILE *f = fopen(KR_PENDING, "r");
  if(!f) return;
  char ver[48] = "";
  unsigned long long bid = 0;
  long long btime = 0;
  int got = fscanf(f, "%47s %llu %lld", ver, &bid, &btime);         /* a marker of an earlier build has no boot time: then the uptime alone says */
  fclose(f);
  unlink(KR_PENDING);
  if(!ps5tm_krakenboot_went_down(got >= 2, bid, kr_uptime_s(), got >= 3 ? btime : 0, kr_boot_time_s())) return;   /* the same boot: the app was only replaced, nothing happened to the console */
  if(strcmp(ver, KR_EMBEDDED_VERSION) != 0) {
    FILE *bf = fopen(KR_BAD ".tmp", "w");
    int wrote = bf && fprintf(bf, "%s\n", ver) >= 0;
    if(bf && fclose(bf) != 0) wrote = 0;
    if(wrote && rename(KR_BAD ".tmp", KR_BAD) == 0) {
      PS5TM_WARN("kraken_start_lost", "Die Konsole ging aus, kurz nachdem die App Kraken Mount %s mit sich gestartet hatte: Diese Fassung wird nicht mehr genommen, die eingebettete startet statt ihrer.", ver);
    } else {
      PS5TM_WARN("kraken_start_lost", "Die Konsole ging aus, kurz nachdem die App Kraken Mount %s mit sich gestartet hatte, und die Fassung ließ sich nicht als unbrauchbar merken (%s).", ver, strerror(errno));
      unlink(KR_BAD ".tmp");
    }
  } else {
    kr_set_on(0);
    if(!kr_is_on())
      PS5TM_WARN("kraken_start_lost", "Die Konsole ging aus, kurz nachdem die App Kraken Mount %s mit sich gestartet hatte: Die App startet es nicht mehr von selbst (Seite FPKGMount, „Einschalten“ schaltet es wieder ein).", ver);
  }
}

static int
read_file(const char *path, unsigned char **out, size_t *len, size_t max) {
  *out = NULL;
  *len = 0;
  FILE *f = fopen(path, "rb");
  if(!f) return -1;
  /* the size of the file, not the limit: a start used to take 16 MiB for a few MiB of program */
  struct stat st;
  size_t want = max;
  if(fstat(fileno(f), &st) == 0 && st.st_size > 0 && (uint64_t)st.st_size < max) want = (size_t)st.st_size;
  unsigned char *b = malloc(want + 1);
  size_t n = b ? fread(b, 1, want + 1, f) : 0;
  fclose(f);
  if(n > want) { free(b); return -1; }                     /* it grew while it was read, or is over the limit */
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

/* Kraken Mount v0.2.0 and later wait at start until no process named "shadowmountplus.elf" runs (and mount nothing before). The app runs it next to
   ShadowMountPlus on purpose, so the last byte of that 19-byte name constant is changed in a downloaded copy, as tools/patch_kraken.py does for the
   embedded one ("shadowmountplus.elf" -> "shadowmountplus.elX": no process has that name). Same length, nothing else changes. Number of places changed. */
static int
patch_smp_wait(unsigned char *d, size_t n) {
  static const char name[] = "shadowmountplus.elf";
  const size_t l = sizeof name - 1;
  int changed = 0;
  for(size_t i = 0; i + l <= n; i++)
    if(d[i] == 's' && !memcmp(d + i, name, l)) { d[i + l - 1] = 'X'; changed++; i += l - 1; }
  return changed;
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
  /* A version that was marked as not working is as good as not there: the embedded copy starts instead (audit F-68: no way back before). */
  char bad[48] = "";
  FILE *bf = fopen(KR_BAD, "r");
  if(bf) {
    if(fgets(bad, sizeof bad, bf)) {
      size_t l = strlen(bad);
      while(l && (bad[l - 1] == '\n' || bad[l - 1] == '\r' || bad[l - 1] == ' ')) bad[--l] = 0;
    }
    fclose(bf);
    if(bad[0] && !strcmp(bad, v)) v[0] = 0;
  }
  /* Not newer than the embedded copy (a version loaded before an app update): the embedded one starts (audit F-251). */
  if(v[0] && !ps5tm_krakenboot_take_fetched(v, KR_EMBEDDED_VERSION)) v[0] = 0;
}

/* After the start of a downloaded version: it has a minute to answer. If it does not, the version is written to KR_BAD and the next start
   takes the embedded copy (a release that does not run must not leave the page without a working Kraken Mount for good). */
static void *
watch_fetched_start(void *arg) {
  char *ver = arg;
  for(int i = 0; i < 60; i++) {
    sleep(1);
    int st = kr_http("GET", "/api/state", NULL, 0, 1500, NULL, NULL);
    /* "listens but answers late" (a scan, a drive that hangs) is alive: only a program that never listens is bad */
    if(st == 200 || st == LHTTP_NO_ANSWER || st == LHTTP_TOO_BIG) { free(ver); return NULL; }
  }
  FILE *bf = fopen(KR_BAD ".tmp", "w");
  int ok = bf && fprintf(bf, "%s\n", ver) >= 0;
  if(bf && fclose(bf) != 0) ok = 0;
  if(ok && rename(KR_BAD ".tmp", KR_BAD) == 0)
    PS5TM_WARN("kraken_bad_version", "Kraken Mount %s hat nach dem Start nicht geantwortet: die nächsten Starts nehmen die eingebettete Fassung.", ver);
  else unlink(KR_BAD ".tmp");
  free(ver);
  return NULL;
}

/* ------------------------------------------------------------------ state */

static int
valid_tid(const char *t) {
  if(!t || strlen(t) != 9) return 0;
  for(int i = 0; i < 4; i++) if(t[i] < 'A' || t[i] > 'Z') return 0;
  for(int i = 4; i < 9; i++) if(t[i] < '0' || t[i] > '9') return 0;
  return 1;
}

static int store_games(const cJSON *k, uint64_t now);

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
  cJSON_AddBoolToObject(o, "autostart", kr_is_on());                   /* the app starts Kraken Mount with itself only when it was switched on in the page (off from the start since 21.11.2026) */
  pthread_mutex_lock(&g_lock);
  cJSON_AddBoolToObject(o, "fetching", g_busy);
  pthread_mutex_unlock(&g_lock);
  unsigned char *body = NULL;
  size_t n = 0;
  int st = kr_http("GET", "/api/state", NULL, 0, 2000, &body, &n);
  cJSON *k = (st == 200 && body) ? cJSON_Parse((char *)body) : NULL;
  free(body);
  if(cJSON_IsObject(k)) {
    store_games(k, ps5tm_mono_ms());                     /* the games of this very answer: the conflict list below and the library need no second ask */
    pending_clear();                                     /* it answers: the start with the app went through */
    cJSON_AddBoolToObject(o, "running", 1);
    /* a Kraken Mount that runs on from before an update is the old version until it is switched off and on again (the app never stops it by itself: a game may
       run from it): the page says so */
    /* It was told to quit and still answers: it is on its way out (the page used to show the switch "on" again for those seconds, 17.11.2026) */
    pthread_mutex_lock(&g_lock);
    const int stopping = g_quit_ms && ps5tm_mono_ms() - g_quit_ms < KR_QUIT_WINDOW_MS;
    pthread_mutex_unlock(&g_lock);
    cJSON_AddBoolToObject(o, "stopping", stopping);
    const cJSON *kv = cJSON_GetObjectItem(k, "version");
    cJSON_AddBoolToObject(o, "restart_needed", cJSON_IsString(kv) && ps5tm_krakenboot_restart_needed(kv->valuestring, fv[0] ? fv : KR_EMBEDDED_VERSION));
    cJSON_AddItemToObject(o, "kraken", k);
    /* its settings: the page names the folders it searches (scanpath; empty = its own defaults) */
    st = kr_http("GET", "/api/settings", NULL, 0, 2000, &body, &n);
    cJSON *set = (st == 200 && body) ? cJSON_Parse((char *)body) : NULL;
    free(body);
    if(cJSON_IsObject(set)) cJSON_AddItemToObject(o, "settings", set);
    else cJSON_Delete(set);
    /* titles ShadowMountPlus or PS4Mount manages too (mounters.c): the page warns about them */
    cJSON_AddItemToObject(o, "conflicts", ps5tm_mounters_conflicts());
  } else {
    cJSON_Delete(k);
    cJSON_AddBoolToObject(o, "running", 0);
    cJSON_AddBoolToObject(o, "stopping", 0);
    pthread_mutex_lock(&g_lock);
    if(st == -1) g_quit_ms = 0;                            /* gone (nothing listens): nothing to wait for any more. A late answer is no reason: it is busy on its way out (audit 17.11.2026) */
    /* "starting" also while the app waits to start it itself (the first seconds after the app's start): the page used to say "off" with a button to start it
       until Kraken Mount was sent, which looked as if it were not on by default (17.11.2026) */
    int starting = (g_started_ms && ps5tm_mono_ms() - g_started_ms < 20000) || g_boot_pending;
    pthread_mutex_unlock(&g_lock);
    cJSON_AddBoolToObject(o, "starting", starting);
  }
  return o;
}

/* Does Kraken Mount search this folder? Its settings (GET /api/settings) name the folders under scanpath; empty means its own defaults
   (/data/homebrew, /data/etaHEN/games, <drive>/homebrew), which are not the app's FPKG-Games. 1 yes; 0 it runs and does not; -1 it does not
   answer. The convert check asks it: a debug FPKG put where Kraken Mount does not search is never registered. */
int
ps5tm_kraken_searches(const char *folder) {
  unsigned char *body = NULL;
  size_t n = 0;
  int st = kr_http("GET", "/api/settings", NULL, 0, 2000, &body, &n);
  cJSON *set = (st == 200 && body) ? cJSON_Parse((char *)body) : NULL;
  free(body);
  if(!cJSON_IsObject(set)) { cJSON_Delete(set); return -1; }
  char want[256];
  snprintf(want, sizeof want, "%s", folder);
  for(size_t l = strlen(want); l > 1 && want[l - 1] == '/'; l--) want[l - 1] = 0;
  int found = 0;
  const cJSON *sp = cJSON_GetObjectItem(set, "scanpath"), *p;
  cJSON_ArrayForEach(p, sp) {
    if(!cJSON_IsString(p)) continue;
    char have[256];
    snprintf(have, sizeof have, "%s", p->valuestring);
    for(size_t l = strlen(have); l > 1 && have[l - 1] == '/'; l--) have[l - 1] = 0;
    if(!strcmp(have, want)) { found = 1; break; }
  }
  cJSON_Delete(set);
  return found;
}

/* Kraken Mount's games as a small copy, for mounters.c. One ask of its /api/state every 10 s at most, however often the library or the
   page asks; while it does not answer (switched off), nothing is listed and the next ask waits 5 s. */
static ps5tm_kraken_game_t g_games[PS5TM_KRAKEN_MAX];
static char                g_cache_dir[160] = "/data/krakenmount/caches";   /* cache.path of the last state that named one */
static int                 g_games_n;
static uint64_t            g_games_ms;
static int                 g_games_flying;             /* one ask at a time: the others wait for its answer (audit 17.11.2026) */
static pthread_cond_t      g_games_cv = PTHREAD_COND_INITIALIZER;

/* The games out of a parsed /api/state into the small copy (the page's own ask hands it over, so the library does not ask Kraken Mount again
   a moment later). 1 when stored. */
static int
store_games(const cJSON *k, uint64_t now) {
  const cJSON *games = k ? cJSON_GetObjectItem(k, "games") : NULL, *g;
  ps5tm_kraken_game_t *tmp = calloc(PS5TM_KRAKEN_MAX, sizeof(*tmp));     /* 120 KB: not on a thread's stack */
  if(!tmp) return 0;
  int n = 0;
  cJSON_ArrayForEach(g, games) {
    if(n >= PS5TM_KRAKEN_MAX) {
      static int warned;                               /* once: the page asks every 5 s */
      if(!warned) {
        warned = 1;
        PS5TM_WARN("kraken_games", "Kraken Mount meldet mehr als %d Spiele; die weiteren fehlen in der Warnliste für doppelt eingehängte Titel.", PS5TM_KRAKEN_MAX);
      }
      break;
    }
    const cJSON *t = cJSON_GetObjectItem(g, "title_id"), *nm = cJSON_GetObjectItem(g, "title_name"),
                *p = cJSON_GetObjectItem(g, "path"), *kd = cJSON_GetObjectItem(g, "kind"), *bp = cJSON_GetObjectItem(g, "backport");
    if(!cJSON_IsString(t) || !valid_tid(t->valuestring)) continue;
    ps5tm_kraken_game_t *e = &tmp[n++];
    memset(e, 0, sizeof(*e));
    snprintf(e->title_id, sizeof(e->title_id), "%s", t->valuestring);
    snprintf(e->name, sizeof(e->name), "%s", cJSON_IsString(nm) ? nm->valuestring : "");
    snprintf(e->path, sizeof(e->path), "%s", cJSON_IsString(p) ? p->valuestring : "");
    snprintf(e->kind, sizeof(e->kind), "%s", cJSON_IsString(kd) && !strcmp(kd->valuestring, "pkg") ? "pkg" : "folder");
    snprintf(e->backport, sizeof(e->backport), "%s", cJSON_IsString(bp) ? bp->valuestring : "");   /* null: none applied */
  }
  /* where the caches lie: an absolute path without "..", else the last one stays */
  const cJSON *cache = k ? cJSON_GetObjectItem(k, "cache") : NULL, *cp = cache ? cJSON_GetObjectItem(cache, "path") : NULL;
  char cdir[sizeof(g_cache_dir)] = "";
  if(cp && cJSON_IsString(cp) && cp->valuestring[0] == '/' && !strstr(cp->valuestring, "..") && strlen(cp->valuestring) < sizeof(cdir))
    snprintf(cdir, sizeof(cdir), "%s", cp->valuestring);
  for(size_t l = strlen(cdir); l > 1 && cdir[l - 1] == '/'; l--) cdir[l - 1] = 0;
  pthread_mutex_lock(&g_lock);
  if(cdir[0]) memcpy(g_cache_dir, cdir, sizeof(cdir));
  memcpy(g_games, tmp, sizeof(g_games[0]) * (size_t)n);
  g_games_n = n;
  g_games_ms = now;
  pthread_mutex_unlock(&g_lock);
  free(tmp);
  return 1;
}

int
ps5tm_kraken_games(ps5tm_kraken_game_t *out, int max) {
  pthread_mutex_lock(&g_lock);
  for(;;) {
    uint64_t now = ps5tm_mono_ms();
    int fresh = g_games_ms && now - g_games_ms < (g_games_n > 0 ? 10000u : 5000u);
    if(fresh) break;
    if(g_games_flying) { pthread_cond_wait(&g_games_cv, &g_lock); continue; }       /* somebody asks already: wait for the answer */
    g_games_flying = 1;
    pthread_mutex_unlock(&g_lock);
    unsigned char *body = NULL;
    size_t bn = 0;
    int st = kr_http("GET", "/api/state", NULL, 0, 1500, &body, &bn);
    cJSON *k = (st == 200 && body) ? cJSON_Parse((char *)body) : NULL;
    free(body);
    if(st == 200 || st == LHTTP_NO_ANSWER || st == LHTTP_TOO_BIG) pending_clear();      /* it is there: the start with the app went through */
    if(st == LHTTP_NO_ANSWER || st == LHTTP_TOO_BIG) {
      /* It listens and is busy (a scan, a rebuild): what was known stays - the mark "FPKGMount" and the warning for titles two mounters want
         must not flicker away for seconds (audit 17.11.2026). The next ask waits for the usual time. */
      pthread_mutex_lock(&g_lock);
      g_games_ms = ps5tm_mono_ms();
      pthread_mutex_unlock(&g_lock);
    } else {
      store_games(k, now);                             /* without a parsed state (switched off): the list is empty, the next ask waits 5 s */
    }
    cJSON_Delete(k);
    pthread_mutex_lock(&g_lock);
    g_games_flying = 0;
    pthread_cond_broadcast(&g_games_cv);
    break;
  }
  int n = g_games_n < max ? g_games_n : max;
  memcpy(out, g_games, sizeof(out[0]) * (size_t)n);
  pthread_mutex_unlock(&g_lock);
  return n;
}

void
ps5tm_kraken_cache_dir(char *out, size_t n) {
  pthread_mutex_lock(&g_lock);
  snprintf(out, n, "%s", g_cache_dir);
  pthread_mutex_unlock(&g_lock);
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
  if(stat(KR_CONFIG, &st) == 0 && st.st_size > 0) return; /* an empty one is the rest of an interrupted write: written anew */
  make_dir(PS5TM_KRAKEN_DATA);
  make_dir("/data/" KR_FPKG_FOLDER);
  char buf[900];
  size_t o = (size_t)snprintf(buf, sizeof buf, "# search folders set by the app (page FPKGMount); ShadowMountPlus does not search them\nscanpath = [\"/data/" KR_FPKG_FOLDER "\"");
  for(int i = 0; i < 8 && o < sizeof buf; i++) o += (size_t)snprintf(buf + o, sizeof buf - o, ", \"/mnt/usb%d/" KR_FPKG_FOLDER "\"", i);
  for(int i = 0; i < 2 && o < sizeof buf; i++) o += (size_t)snprintf(buf + o, sizeof buf - o, ", \"/mnt/ext%d/" KR_FPKG_FOLDER "\"", i);
  if(o < sizeof buf) o += (size_t)snprintf(buf + o, sizeof buf - o, "]\n");
  if(o >= sizeof buf) return;
  /* Written to a file of its own and renamed, so that a full disk or a power cut never leaves half a config behind (audit 15.11.2026). */
  const char *tmp = KR_CONFIG ".tmp";
  FILE *f = fopen(tmp, "w");
  if(!f) { PS5TM_WARN("kraken_config", "%s ließ sich nicht anlegen: %s", KR_CONFIG, strerror(errno)); return; }
  int ok = fputs(buf, f) >= 0;
  ok = (fflush(f) == 0) && ok;
  if(ok) fsync(fileno(f));
  ok = (fclose(f) == 0) && ok;
  if(!ok || rename(tmp, KR_CONFIG) != 0) {
    PS5TM_WARN("kraken_config", "%s ließ sich nicht schreiben: %s", KR_CONFIG, strerror(errno));
    unlink(tmp);
    return;
  }
  PS5TM_INFO("kraken_config", "Suchordner für Kraken Mount angelegt: /data/%s und %s auf den Laufwerken.", KR_FPKG_FOLDER, KR_FPKG_FOLDER);
}

/* ------------------------------------------------------------------ actions */

/* by_person: the button "Einschalten" (it also takes the "off" marker away: wanted on, the next start of the app starts it, too); 0: the start with the app,
   which respects the marker up to the moment before it sends (audit 17.11.2026: it used to take the marker away itself and so overrode a "Ausschalten" given in
   the first seconds). */
static int
start_kraken(char *msg, size_t n, int by_person) {
  if(by_person) kr_set_on(1);
  int alive = kr_http("GET", "/api/state", NULL, 0, 1500, NULL, NULL);
  if(alive == 200) {
    pthread_mutex_lock(&g_lock);
    const int stopping = g_quit_ms && ps5tm_mono_ms() - g_quit_ms < KR_QUIT_WINDOW_MS;
    pthread_mutex_unlock(&g_lock);
    if(stopping) { snprintf(msg, n, "Kraken Mount wird gerade beendet. Bitte in einigen Sekunden wieder einschalten."); return 409; }
    snprintf(msg, n, "Kraken Mount läuft schon.");
    return 409;
  }
  /* It listens but does not answer: it is busy (a scan, a drive that hangs), not gone. A second copy would only send the first one away. */
  if(alive == LHTTP_NO_ANSWER) { snprintf(msg, n, "Kraken Mount antwortet gerade nicht (es arbeitet vermutlich). Bitte gleich noch einmal versuchen."); return 409; }
  pthread_mutex_lock(&g_lock);
  uint64_t now = ps5tm_mono_ms();
  int again = g_started_ms && now - g_started_ms < 15000;
  if(!again) { g_started_ms = now; g_quit_ms = 0; }
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
  if(!by_person) {
    /* The last look at the "off" marker, under the lock the "stop" of the page takes it under: from here on a "stop" is told that the start is under way. */
    pthread_mutex_lock(&g_lock);
    if(!kr_is_on()) {
      g_started_ms = 0;
      pthread_mutex_unlock(&g_lock);
      free(elf);
      snprintf(msg, n, "Kraken Mount wurde ausgeschaltet, bevor die App es gesendet hat.");
      return 409;
    }
    g_boot_sending = 1;
    pthread_mutex_unlock(&g_lock);
    pending_write(which);
  }
  const char *why = NULL;
  int rc = ps5tm_loader_inject_mem(elf, (unsigned long)elf_len, &why);
  free(elf);
  if(!by_person) { pthread_mutex_lock(&g_lock); g_boot_sending = 0; if(rc == 0) g_boot_sent_ms = ps5tm_mono_ms(); pthread_mutex_unlock(&g_lock); }
  if(rc != 0) {
    pthread_mutex_lock(&g_lock); g_started_ms = 0; pthread_mutex_unlock(&g_lock);
    if(!by_person) pending_clear();
    snprintf(msg, n, "%s", why ? why : "Kraken Mount ließ sich nicht senden.");
    return 502;
  }
  PS5TM_INFO("kraken_start", "Kraken Mount %s an den Payload-Lader gesendet.", which);
  if(which == fv) {
    char *ver = strdup(fv);
    pthread_t th;
    pthread_attr_t at;
    pthread_attr_init(&at);
    pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
    pthread_attr_setstacksize(&at, 128 * 1024);
    if(!ver || pthread_create(&th, &at, watch_fetched_start, ver) != 0) free(ver);
    pthread_attr_destroy(&at);
  }
  snprintf(msg, n, "Kraken Mount %s wird gestartet. Das dauert einige Sekunden.", which);
  return 200;
}

static int
stop_kraken(char *msg, size_t n) {
  int st = kr_http("POST", "/api/quit", "shutdown", 8, 5000, NULL, NULL);
  if(st == 204 || st == 200) {
    kr_set_on(0);                                          /* switched off in the page: the app does not start it again with itself */
    pthread_mutex_lock(&g_lock);
    g_quit_ms = ps5tm_mono_ms();
    pthread_mutex_unlock(&g_lock);
    snprintf(msg, n, "Kraken Mount wird beendet.");
    PS5TM_INFO("kraken_stop", "Kraken Mount beendet (Seite FPKGMount).");
    return 200;
  }
  if(st == 409) { snprintf(msg, n, "Ein Spiel läuft gerade. Kraken Mount lässt sich erst nach dem Spiel beenden."); return 409; }
  if(st == LHTTP_NO_ANSWER) { snprintf(msg, n, "Kraken Mount antwortet gerade nicht (es arbeitet vermutlich). Bitte gleich noch einmal versuchen."); return 409; }
  if(st < 0) {
    pthread_mutex_lock(&g_lock);
    kr_set_on(0);
    int pending = g_boot_pending;                          /* switched off while the app was still waiting to start it: that is what the person asked for */
    /* ... but the start is at the loader right now, or was sent a few seconds ago and Kraken Mount is not listening yet (the same 20 s the page says "starts" for) */
    int sending = g_boot_sending || (g_boot_sent_ms && ps5tm_mono_ms() - g_boot_sent_ms < 20000);
    g_boot_pending = 0;
    pthread_mutex_unlock(&g_lock);
    if(sending) {
      snprintf(msg, n, "Kraken Mount wird gerade mit der App gesendet und startet noch. Ab dem nächsten Start der App bleibt es aus; zum Beenden den Schalter in einigen Sekunden noch einmal ausschalten.");
      return 409;
    }
    if(pending) { snprintf(msg, n, "Kraken Mount wird nicht mit der App gestartet."); return 200; }
    snprintf(msg, n, "Kraken Mount läuft nicht.");
    return 409;
  }
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
  /* A delete of a big game folder takes longer than the time: Kraken Mount is not gone, it is working. */
  else if(st == LHTTP_NO_ANSWER) { snprintf(msg, n, "Keine Antwort in der Frist: Kraken Mount arbeitet vermutlich noch. Bitte später in der Liste nachsehen."); rc = 409; }
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
    const char *step = res.step[0] ? res.step : "http";
    if(res.status) snprintf(why, why_len, "%s ließ sich nicht laden (%s %d).", what, step, res.status);
    else snprintf(why, why_len, "%s ließ sich nicht laden (%s).", what, step);
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
  if(!ps5tm_krakenboot_take_fetched(ver, KR_EMBEDDED_VERSION)) {            /* not newer than what the app brings: nothing to load (audit F-251) */
    snprintf(msg, n, "Die neueste Fassung (%s) ist nicht neuer als die eingebettete (%s). Die eingebettete wird genommen.", ver, KR_EMBEDDED_VERSION);
    rc = 200;
    goto done;
  }
  {
    /* the version that was marked as not working is not loaded again and again (the marker stays until a different one comes) */
    char badv[48] = "";
    FILE *bf = fopen(KR_BAD, "r");
    if(bf) {
      if(fgets(badv, sizeof badv, bf)) {
        size_t l = strlen(badv);
        while(l && (badv[l - 1] == '\n' || badv[l - 1] == '\r' || badv[l - 1] == ' ')) badv[--l] = 0;
      }
      fclose(bf);
    }
    if(badv[0] && !strcmp(badv, ver)) {
      snprintf(msg, n, "Die neueste Fassung (%s) hat nach dem Start nicht geantwortet und wird nicht noch einmal geladen. Die eingebettete Fassung wird genommen.", ver);
      rc = 200;
      goto done;
    }
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
  int smp_places = patch_smp_wait((unsigned char *)elf, elf_n);
  if(smp_places) PS5TM_INFO("kraken_patch", "Kraken Mount %s: Prozessname von ShadowMountPlus an %d Stelle(n) geändert (kein Warten auf ShadowMountPlus).", ver, smp_places);
  int e = ps5tm_put_lib_file(KR_ELF, (const unsigned char *)elf, elf_n);
  if(e) { snprintf(why, sizeof why, "Die Datei ließ sich nicht ablegen: %s", strerror(e)); goto done; }
  unlink(KR_BAD);                                  /* a newer release gets its chance */
  /* the version file is written to a file of its own and renamed (a start at the same moment never reads half of it) */
  FILE *vf = fopen(KR_VER ".tmp", "w");
  int vok = vf && fprintf(vf, "%s\n", ver) >= 0;
  if(vf && fclose(vf) != 0) vok = 0;
  if(!vok || rename(KR_VER ".tmp", KR_VER) != 0) { unlink(KR_VER ".tmp"); remove(KR_ELF); snprintf(why, sizeof why, "Die Versionsdatei ließ sich nicht schreiben."); goto done; }
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
  if(!strcmp(a, "start")) return start_kraken(msg, n, 1);
  if(!strcmp(a, "stop")) return stop_kraken(msg, n);
  if(!strcmp(a, "rescan")) return simple_post("/api/rescan", NULL, "Kraken Mount sucht neu.", msg, n);
  if(!strcmp(a, "rebuild_all")) return simple_post("/api/rebuild-all", NULL, "Kraken Mount baut alle Spiele neu auf.", msg, n);
  if(!strcmp(a, "rebuild")) {
    if(!valid_tid(id)) { snprintf(msg, n, "Die Titel-ID ist ungültig."); return 400; }
    char path[40];
    snprintf(path, sizeof path, "/api/rebuild/%s", id);
    return simple_post(path, NULL, "Das Spiel wird neu aufgebaut.", msg, n);
  }
  /* the PlayGo stub of one game (Kraken Mount 0.3.1, POST /api/playgo/<TID>, body "on" or "off"); an older Kraken Mount has no such route (its games carry no
     "playgo_off": the pages show no switch then) */
  if(!strcmp(a, "playgo")) {
    const cJSON *joff = cJSON_GetObjectItem(b, "off");
    if(!valid_tid(id) || !cJSON_IsBool(joff)) { snprintf(msg, n, "Titel-ID oder Wert fehlen."); return 400; }
    char path[48];
    snprintf(path, sizeof path, "/api/playgo/%s", id);
    const int off = cJSON_IsTrue(joff);
    return simple_post(path, off ? "off" : "on", off ? "Der PlayGo-Stub ist für dieses Spiel ausgeschaltet." : "Der PlayGo-Stub ist für dieses Spiel eingeschaltet.", msg, n);
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

/* ------------------------------------------------------------------ start with the app */

/* Kraken Mount runs on when the app is replaced or restarted, and is gone after a restart of the console: the app starts it by itself (17.11.2026, wish of the
   user: switched on by default) unless the person switched it off in the page. One look after 5 s, then at most three more every 45 s when the loader does not
   take it (nothing is sent while it is there, busy or not: krakenboot.h). The page's "Einschalten" and "Ausschalten" write and remove the marker. */
static void
boot_pending_done(void) {
  pthread_mutex_lock(&g_lock);
  g_boot_pending = 0;
  pthread_mutex_unlock(&g_lock);
}

/* After the start with the app: the marker goes when Kraken Mount has answered (even late: busy is alive) or, at the latest, a minute after the send - the app has
   lived on, so the console did not go down because of it. A console that does go down in these seconds never gets here, and the marker stays. */
static void
pending_confirm(void) {
  for(int i = 0; i < 60; i++) {
    sleep(1);
    int st = kr_http("GET", "/api/state", NULL, 0, 1500, NULL, NULL);
    if(st == 200 || st == LHTTP_NO_ANSWER || st == LHTTP_TOO_BIG) break;
  }
  pending_clear();
}

static void *
boot_thread(void *arg) {
  (void)arg;
  for(int tries = 0; tries < 4; tries++) {
    sleep(tries == 0 ? KR_BOOT_DELAY_SEC : 45);
    int alive = kr_http("GET", "/api/state", NULL, 0, 1500, NULL, NULL);
    if(!ps5tm_krakenboot_should_start(!kr_is_on(), alive, LHTTP_NO_ANSWER)) {
      boot_pending_done();
      if(tries == 0 && alive == 200) PS5TM_INFO("kraken_boot", "Kraken Mount läuft schon: die App startet es nicht noch einmal.");
      else if(!kr_is_on()) PS5TM_INFO("kraken_boot", "Kraken Mount ist nicht eingeschaltet (Vorgabe, oder in der Seite ausgeschaltet): die App startet es nicht.");
      return NULL;
    }
    char msg[200];
    int rc = start_kraken(msg, sizeof msg, 0);             /* sets the time of the start: the page goes on saying "starts" from there */
    boot_pending_done();
    PS5TM_INFO("kraken_boot", "Kraken Mount mit der App gestartet (Versuch %d): %d, %s", tries + 1, rc, msg);
    if(rc == 200) { pending_confirm(); return NULL; }
  }
  PS5TM_WARN("kraken_boot_failed", "Kraken Mount ließ sich mit der App nicht starten: auf der Seite FPKGMount „Einschalten“ drücken.");
  return NULL;
}

void
ps5tm_kraken_boot(void) {
  /* main() calls this in its bind loop, which runs again after rest mode or a new network: once per start of the app. A second run took away the
     marker of a start still under way (the guard against a start loop) and could send a second copy (review of 23.11.2026). */
  static int booted;
  if(booted) return;
  booted = 1;
  pending_check();                                         /* a start with the app that the console did not survive: no second one (before the marker is looked at) */
  if(kr_is_on()) {
    pthread_mutex_lock(&g_lock);
    g_boot_pending = 1;
    pthread_mutex_unlock(&g_lock);
  }
  pthread_t th;
  pthread_attr_t at;
  pthread_attr_init(&at);
  pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
  pthread_attr_setstacksize(&at, 128 * 1024);
  if(pthread_create(&th, &at, boot_thread, NULL) != 0) {
    boot_pending_done();
    PS5TM_WARN("kraken_boot_thread", "Kraken Mount lässt sich nicht mit der App starten: der Faden ließ sich nicht anlegen.");
  }
  pthread_attr_destroy(&at);
}
