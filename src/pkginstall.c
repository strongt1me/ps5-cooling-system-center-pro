/* Installing a package from a stick, a disc or the console's storage (05.10.2026).
 *
 * The console installs a package with a call into its system library (AppInstUtil): it is given an address, fetches
 * the bytes itself and unpacks them. This job does what the call needs around it:
 *
 *   - serves the package on the loopback address (pkgstream.c), a split package as the one stream it is;
 *   - starts the install helper (src/helper/pkginst_helper.c, embedded in this program) through the payload loader,
 *     one process for one installation, and talks to it over the loopback address (pkginst_ipc.h);
 *   - asks it how far the system is, once a second, and shows that;
 *   - calls the installation done when the system says so AND every byte of the package has been sent (a title can be
 *     "playable" long before the last chunk is fetched), and then looks the result up in the app database;
 *   - on a failure that comes and goes (the system is not ready, a slot it has not given back) tries again with a new
 *     helper and a new address, as the console's own tools do.
 *
 * What the installation job never does: delete anything, uninstall anything, start a game, touch a package (it is
 * only read), or run without a click. (Uninstalling is a call of its own, ps5tm_pkginst_uninstall, made only by the
 * delete job in gamedelete.c after the person confirmed it.) A stop kills the helper and the server; what the system has already put on its disk stays for it to
 * deal with.
 *
 * One job at a time, and not alongside a copy, a conversion, a move or a backup of saved games, or the splitting of
 * a package: they all want the same drives. The format knowledge of how the system library is called is that of the
 * PS5 PKG Manager by itsPLK (GPL-3.0, named in the README), read for this; the code is our own. */

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "pkginst.h"
#include "pkginst_ipc.h"
#include "ps5tm.h"
#include "third_party/cJSON.h"

#ifndef PS5TM_LOADER_PORT
#define PS5TM_LOADER_PORT 9021
#endif

/* The helper's image, put into this program at build time (tools/gen_blob.py over the helper's ELF). */
extern const unsigned char ps5tm_pkginst_helper[];
extern const unsigned long ps5tm_pkginst_helper_len;

#define PI_MAX_SLICES   512
#define PI_SPACE_SLACK  (1ull << 30)         /* what the console wants free besides the package */
#define PI_ATTEMPTS     3

/* The waits; the tests shorten them. */
#ifndef PI_ACCEPT_MS
#define PI_ACCEPT_MS    25000                /* from the image sent to the helper's connection */
#endif
#ifndef PI_READY_MS
#define PI_READY_MS     30000                /* the helper initialises the system library */
#endif
#ifndef PI_INSTALL_MS
#define PI_INSTALL_MS   120000               /* the call that starts an installation */
#endif
#ifndef PI_UNINSTALL_MS
#define PI_UNINSTALL_MS 120000               /* the system's uninstall of a title (it may take its time) */
#endif
#ifndef PI_STATUS_MS
#define PI_STATUS_MS    10000
#endif
#ifndef PI_STALL_NOTE_S
#define PI_STALL_NOTE_S 180                  /* no progress for this long: a note */
#endif
#ifndef PI_STALL_FAIL_S
#define PI_STALL_FAIL_S 1200                 /* ... and this long: it has failed */
#endif
#ifndef PI_VERIFY_S
#define PI_VERIFY_S     90                   /* how long the app database is waited for after the system is done */
#endif
#ifndef PI_STATUS_MISSES
#define PI_STATUS_MISSES 3                   /* questions about the progress that may stay unanswered, one after the other */
#endif
#ifndef PI_NONE_GRACE_S
#define PI_NONE_GRACE_S 15                   /* a status "none" this soon after the call: the system has not registered it yet */
#endif
#define PI_LOG_STATUS_S 30                   /* the status goes into the log when it changes, and at least this often */

/* The helper that made the call can die while the system goes on (the first console test: it crashed right after the call). */
#ifndef PI_RESTARTS_MAX
#define PI_RESTARTS_MAX 6                    /* helpers started after the first one, over one installation */
#endif
#ifndef PI_RESTART_WAIT_MS
#define PI_RESTART_WAIT_MS 2000              /* before the next try to start one, times the tries so far */
#endif
#ifndef PI_BLIND_VERIFY_S
#define PI_BLIND_VERIFY_S 600                /* with nobody to ask: how long the list is watched after the last byte went out */
#endif
#ifndef PI_LOADER_GONE_MS
#define PI_LOADER_GONE_MS 3000               /* the loader's connection has closed and the helper has not reported: wait this long for it */
#endif

enum { J_IDLE, J_PREPARE, J_START, J_RUN, J_FINISH, J_DONE, J_FAILED, J_CANCELLED };
static const char *const k_state[] = { "idle", "preparing", "starting", "installing", "finishing", "done", "failed", "cancelled" };

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static struct {
  int      state;
  char     name[160], file[256], title_id[24], content_id[64], version[24], kind[8];
  int      plat;
  char     phase[96];
  uint64_t total, downloaded, served;
  unsigned remain_s, promote;
  int      copy_pct;
  char     sys_status[17];
  char     error[512], note[400];
  int      error_code;                           /* what the system library returned, 0 when it was not that */
  unsigned attempt;
  unsigned restarts;                             /* helpers started after the first one (it had gone, or hung) */
  int      blind;                                /* nobody can ask the system how far it is: the numbers are the server's */
  int      started;                              /* the system has accepted the package: from here on it may have put something on the console */
  int      verified;                             /* done: the result was found in the console's list */
  uint64_t started_ms, finished_ms;              /* the clock that only runs forward: a step of the wall clock must not show */
} g_job;
static int g_cancel;
static unsigned g_seq;

static uint64_t
mono_ms(void) { return ps5tm_mono_ms(); }

/* What the system library returned, or 0 for the app's own failure codes (PKGI_E_*) and the -1 of "no code". */
static int
sys_code(int c) { return (c <= -1 && c >= -2100) ? 0 : c; }

static int
active_locked(void) { return g_job.state >= J_PREPARE && g_job.state <= J_FINISH; }

int
ps5tm_pkginst_busy(void) {
  pthread_mutex_lock(&g_lock);
  int a = active_locked();
  pthread_mutex_unlock(&g_lock);
  return a;
}

static int
cancelled(void) { return __atomic_load_n(&g_cancel, __ATOMIC_ACQUIRE); }

void
ps5tm_pkginst_cancel(void) {
  /* The flag is set while the lock is held that says the job is active: a start resets it under the same lock, so a
     cancel that was meant for the job that has just ended can never land on the one that begins next. */
  pthread_mutex_lock(&g_lock);
  if(active_locked()) __atomic_store_n(&g_cancel, 1, __ATOMIC_RELEASE);
  pthread_mutex_unlock(&g_lock);
}

static void
job_phase(int state, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void
job_phase(int state, const char *fmt, ...) {
  pthread_mutex_lock(&g_lock);
  g_job.state = state;
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(g_job.phase, sizeof(g_job.phase), fmt, ap);
  va_end(ap);
  pthread_mutex_unlock(&g_lock);
}

static void
job_note(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void
job_note(const char *fmt, ...) {
  pthread_mutex_lock(&g_lock);
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(g_job.note, sizeof(g_job.note), fmt, ap);
  va_end(ap);
  pthread_mutex_unlock(&g_lock);
}

static void
job_end(int state) {
  pthread_mutex_lock(&g_lock);
  g_job.state = state;
  g_job.finished_ms = mono_ms();
  pthread_mutex_unlock(&g_lock);
}

/* Records why the job has failed. The state itself changes only at the very end (job_end), when the helper and the server
   are gone: a job that shows "failed" while its server still holds the port would have the next start fail with "port
   taken". */
static void
job_fail(int code, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void
job_fail(int code, const char *fmt, ...) {
  pthread_mutex_lock(&g_lock);
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(g_job.error, sizeof(g_job.error), fmt, ap);
  va_end(ap);
  g_job.error_code = sys_code(code);                   /* only what the console said is shown as its code */
  pthread_mutex_unlock(&g_lock);
}

/* ------------------------------------------------------------------ what the system says */

static int
is_transient(int code) {
  uint32_t c = (uint32_t)code;
  return c == 0x80B2116Fu || c == 0x80B2100Du || c == 0x80B2100Eu;     /* slot, not ready, timeout */
}

/* The system's error in words a person can use; the number stays in the text for whoever has to look it up. */
static void
describe_native(int code, char *out, size_t n) {
  uint32_t c = (uint32_t)code;
  const char *t;
  switch(c) {
  case 0x80A30001u: t = "Die Konsole meldet einen unbekannten Fehler bei der Installation"; break;
  case 0x80A30002u: t = "Auf der Konsole ist nicht genug Speicherplatz frei"; break;
  case 0x80A30003u: t = "Die Konsole lehnt die Angaben zum Paket ab"; break;
  case 0x80B21121u: t = "Die Konsole hat keine Netzwerkverbindung. Sie nimmt das Paket nur über das Netzwerk an, auch wenn es von der App kommt: bitte mit LAN oder WLAN verbinden (Internet ist nicht nötig)"; break;
  case 0x80B21164u: t = "Die Inhalts-ID des Pakets ist der Konsole unbekannt"; break;
  case 0x80B21167u: t = "Die Inhalts-ID des Pakets passt nicht zum installierten Spiel"; break;
  case 0x80B2116Au: t = "Dafür muss das Spiel schon vollständig installiert sein"; break;
  case 0x80B2116Eu: t = "Die Version des Pakets passt nicht zum installierten Stand"; break;
  case 0x80B21170u: t = "Dieses Update-Paket ist fehlerhaft oder passt nicht zum installierten Spiel"; break;
  case 0x80B2116Fu: t = "Die Konsole ist noch mit einer anderen Installation beschäftigt"; break;
  case 0x80B2100Du: t = "Die Installationsbibliothek der Konsole war nicht bereit"; break;
  case 0x80B2100Eu: t = "Die Konsole hat zu lange für die Installation gebraucht"; break;
  default:          t = NULL; break;
  }
  if(t) snprintf(out, n, "%s (Fehler 0x%08X).", t, c);
  else  snprintf(out, n, "Die Konsole meldet den Fehler 0x%08X.", c);
}

/* What a person can do about an error that did not go away by itself (shown after the last of the tries). */
static const char *
advice_for(int code) {
  switch((uint32_t)code) {
  case 0x80B2116Fu: return " Bitte warten, bis die Download-Liste der Konsole leer ist, oder die Konsole neu starten.";
  case 0x80B2100Du: return " Bitte die Konsole neu starten, wenn es wiederholt vorkommt.";
  default:          return "";
  }
}

static void
describe_local(int code, char *out, size_t n) {
  switch(code) {
  case PKGI_E_UNAVAILABLE:  snprintf(out, n, "Der Installationshelfer ließ sich nicht starten oder hat nicht geantwortet."); break;
  case PKGI_E_DISCONNECTED: snprintf(out, n, "Der Installationshelfer hat sich beendet, bevor die Installation fertig war (Einzelheiten stehen im Protokoll)."); break;
  case PKGI_E_CANCELED:     snprintf(out, n, "Abgebrochen."); break;
  case PKGI_E_TIMEOUT:      snprintf(out, n, "Der Installationshelfer hat nicht rechtzeitig geantwortet."); break;
  default:                  snprintf(out, n, "Der Installationshelfer hat unverständlich geantwortet."); break;
  }
}

/* "12,3 GB" for a number of bytes, for messages and the log. */
static void
gb_text(uint64_t b, char *out, size_t n) {
  snprintf(out, n, "%.1f GB", (double)b / 1073741824.0);
}

/* ------------------------------------------------------------------ versions */

/* A version the way a package prints it ("v1.03") from one the app database or a param.json holds
   ("01.000.003", "01.03", "v1.03"): the same reading as pkgparse.c, so that two of them can be compared. */
static void
norm_version(const char *ver, char *out, size_t n) {
  int maj = 0, min = 0, patch = 0;
  if(sscanf(ver, "%d.%d.%d", &maj, &min, &patch) == 3) {
    if(min == 0 && patch > 0) snprintf(out, n, "v%d.%02d", maj, patch);
    else if(patch == 0)       snprintf(out, n, "v%d.%02d", maj, min);
    else                      snprintf(out, n, "v%d.%d.%d", maj, min, patch);
  } else if(ver[0] != 'v' && ver[0] != 'V') {
    snprintf(out, n, "v%.20s", ver);
  } else {
    snprintf(out, n, "%.22s", ver);
  }
}

/* The numbers in a version text, left to right. */
static unsigned
ver_numbers(const char *s, unsigned long *v, unsigned max) {
  unsigned n = 0;
  while(*s && n < max) {
    while(*s && !isdigit((unsigned char)*s)) s++;
    if(!*s) break;
    char *end;
    v[n++] = strtoul(s, &end, 10);
    s = end;
  }
  return n;
}

/* <0, 0, >0 like strcmp; a missing number counts as 0. */
static int
ver_cmp(const char *a, const char *b) {
  unsigned long x[6] = { 0 }, y[6] = { 0 };
  unsigned nx = ver_numbers(a, x, 6), ny = ver_numbers(b, y, 6);
  (void)nx; (void)ny;
  for(unsigned i = 0; i < 6; i++) {
    if(x[i] != y[i]) return x[i] < y[i] ? -1 : 1;
  }
  return 0;
}

/* The version a param.json at path names, or "". */
static void
param_json_version(const char *path, char *out, size_t n) {
  out[0] = 0;
  int fd = open(path, O_RDONLY | O_NONBLOCK);
  if(fd < 0) return;
  /* On the heap: this runs on a request thread, whose stack is the system's default, and the parser needs depth for a
     deeply nested file. */
  const size_t cap = 65536;
  char *buf = malloc(cap);
  if(!buf) { close(fd); return; }
  ssize_t r = read(fd, buf, cap - 1);
  close(fd);
  if(r <= 2) { free(buf); return; }
  buf[r] = 0;
  cJSON *j = cJSON_ParseWithLength(buf, (size_t)r);
  free(buf);
  if(!j) return;
  static const char *const keys[] = { "contentVersion", "appVersion", "version" };
  for(unsigned i = 0; i < 3 && !out[0]; i++) {
    const cJSON *it = cJSON_GetObjectItemCaseSensitive(j, keys[i]);
    if(cJSON_IsString(it) && it->valuestring[0]) snprintf(out, n, "%s", it->valuestring);
  }
  cJSON_Delete(j);
}

/* What is installed of a title: 1 with its newest version (normalised, "" when none can be told), 0 when the console
   does not list it, -1 when that cannot be told. The app database has the base; the patch folders and the
   metadata folders have what an update has put there. */
static int
installed_state(const char *title_id, char *ver, size_t ver_len) {
  ver[0] = 0;
  for(const char *c = title_id; *c; c++)                       /* the id goes into paths */
    if(!isalnum((unsigned char)*c) && *c != '_' && *c != '-') return -1;
  char raw[48] = "";
  int listed = ps5tm_library_title_info(title_id, raw, sizeof(raw));
  if(listed < 0) return -1;
  if(listed == 0) return 0;
  char best[48];
  norm_version(raw, best, sizeof(best));
  if(!raw[0]) best[0] = 0;
  static const char *const where[] = {
    "/user/patch/%s/sce_sys/param.json", "/user/patch/%s/param.json", "/user/patch0/%s/sce_sys/param.json",
    "/system_data/priv/appmeta/%s/param.json", "/system_data/priv/appmeta/%s/patch/param.json",
    "/user/appmeta/%s/param.json", "/user/appmeta/%s/patch/param.json"
  };
  for(unsigned i = 0; i < sizeof(where) / sizeof(where[0]); i++) {
    char path[160], cand[48], norm[48];
    snprintf(path, sizeof(path), where[i], title_id);
    param_json_version(path, cand, sizeof(cand));
    if(!cand[0]) continue;
    norm_version(cand, norm, sizeof(norm));
    if(!best[0] || ver_cmp(norm, best) > 0) snprintf(best, sizeof(best), "%s", norm);
  }
  snprintf(ver, ver_len, "%s", best);
  return 1;
}

/* A DLC: listed in the app database, or its folder is there. */
static int
dlc_installed(const char *title_id, const char *content_id) {
  int l = ps5tm_library_content_listed(content_id);
  if(l == 1) return 1;
  for(const char *c = title_id; *c; c++) if(!isalnum((unsigned char)*c) && *c != '_' && *c != '-') return l;
  for(const char *c = content_id; *c; c++) if(!isalnum((unsigned char)*c) && *c != '_' && *c != '-') return l;
  char path[240];
  snprintf(path, sizeof(path), "/user/addcont/%s/%s", title_id, content_id);
  struct stat st;
  if(stat(path, &st) == 0 && S_ISDIR(st.st_mode)) return 1;
  return l < 0 ? -1 : 0;
}

/* Has the package's content arrived? 1 yes, 0 not yet, -1 cannot be told. */
static int
content_present(const ps5tm_pkg_t *p) {
  if(!strcmp(p->kind, "dlc")) return dlc_installed(p->title_id, p->content_id);
  char ver[48];
  int st = installed_state(p->title_id, ver, sizeof(ver));
  if(st <= 0) return st;
  if(!strcmp(p->kind, "update")) {
    if(!p->version[0] || !ver[0]) return -1;               /* no version on one side: "newer" cannot be told, and a guess is no proof */
    char want[48];
    norm_version(p->version, want, sizeof(want));
    return ver_cmp(ver, want) >= 0;
  }
  return 1;
}

/* ------------------------------------------------------------------ what installing would do */

typedef struct {
  ps5tm_pkg_t      pkg;
  ps5tm_pkgslice_t slices[PI_MAX_SLICES];
  unsigned         nslices;
  uint64_t         total;
  char             installed_ver[48];
  int              installed;                    /* 1 listed, 0 not, -1 unknown */
  int              content_there;                /* the package's own content is installed already: 1, 0, -1 */
  uint64_t         free_bytes;
  int              free_known;
  char             block[512];                   /* why it cannot be installed; "" when it can */
  int              http;                         /* the status a refused start answers with */
  char             warn[512];
} eval_t;

static int
other_job_active(void) {
  return ps5tm_gamecopy_busy() || ps5tm_gameconvert_busy() || ps5tm_gamemove_busy() || ps5tm_saves_busy() ||
         ps5tm_pkgsplit_busy() || ps5tm_gamedelete_busy();
}

/* One more sentence for the warning of the plan. */
static void
add_warn(eval_t *e, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void
add_warn(eval_t *e, const char *fmt, ...) {
  size_t l = strlen(e->warn);
  if(l + 2 >= sizeof(e->warn)) return;
  if(l) e->warn[l++] = ' ';
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(e->warn + l, sizeof(e->warn) - l, fmt, ap);
  va_end(ap);
}

static void
evaluate(const char *id, eval_t *e) {
  memset(e, 0, sizeof(*e));
  e->installed = -1;
  e->content_there = -1;
  e->http = 200;
  if(ps5tm_pkgscan_find(id, &e->pkg) != 0) {
    snprintf(e->block, sizeof(e->block), "Dieses Paket kennt die letzte Suche nicht (mehr). Bitte noch einmal suchen.");
    e->http = 404;
    return;
  }
  const ps5tm_pkg_t *p = &e->pkg;
  char why[300];
  if(ps5tm_pkgscan_slices(id, e->slices, PI_MAX_SLICES, &e->nslices, &e->total, why, sizeof(why)) != 0) {
    snprintf(e->block, sizeof(e->block), "%s", why);
    e->http = 409;
    return;
  }
  /* each file is the one the search read: as long, written at the same time, and not a link put in its place */
  for(unsigned i = 0; i < e->nslices; i++) {
    const ps5tm_pkgslice_t *s = &e->slices[i];
    struct stat st;
    if(!strncmp(s->path, PKGLIVE_PREFIX, strlen(PKGLIVE_PREFIX))) continue;       /* from the browser: no file to look at */
    if(lstat(s->path, &st) != 0 || !S_ISREG(st.st_mode) || (uint64_t)st.st_size < s->file_off + s->size ||
       (s->file_size && ((uint64_t)st.st_size != s->file_size || (int64_t)st.st_mtime != s->mtime))) {
      snprintf(e->block, sizeof(e->block), "Die Paketdatei hat sich seit der Suche verändert oder ist nicht mehr da. Bitte noch einmal suchen.");
      e->http = 409;
      return;
    }
  }
  /* The parts of a split package say what is in them in a header of their own; the installation is of what is in them.
     The two must agree: the rules below (installed already? running? an update?) are for the package that will be
     installed, not for what a header claims. */
  if(p->parts) {
    ps5tm_pkg_t real;
    if(ps5tm_pkg_parse_slices(e->slices, e->nslices, e->total, &real) != 0) {
      snprintf(e->block, sizeof(e->block), "Der Inhalt der Teile lässt sich nicht als Paket lesen. Die Teile sind beschädigt oder gehören nicht zu einem Paket.");
      e->http = 409;
      return;
    }
    if(strcmp(real.title_id, p->title_id) || (real.content_id[0] && strcmp(real.content_id, p->content_id))) {
      snprintf(e->block, sizeof(e->block), "Die Teile sind falsch beschriftet: Ihr Kopf nennt %.24s (%.60s), das Paket darin aber %.24s (%.60s). Es wird nichts installiert.",
               p->title_id, p->content_id, real.title_id[0] ? real.title_id : "keine Titel-ID", real.content_id[0] ? real.content_id : "keine Inhalts-ID");
      e->http = 409;
      return;
    }
    if(strcmp(real.kind, p->kind))
      add_warn(e, "Der Kopf der Teile nennt die Art „%s“, das Paket darin ist aber „%s“. Es gilt, was im Paket steht.", p->kind, real.kind);
    snprintf(e->pkg.kind, sizeof(e->pkg.kind), "%s", real.kind);
    if(real.version[0]) snprintf(e->pkg.version, sizeof(e->pkg.version), "%s", real.version);
    if(real.plat) e->pkg.plat = real.plat;
  }
  if(p->plat != 4 && p->plat != 5) {
    snprintf(e->block, sizeof(e->block), "Die Plattform des Pakets (PS4 oder PS5) ist unbekannt.");
    e->http = 409;
    return;
  }
  if(!p->title_id[0] || !strcmp(p->title_id, "UNKNOWN") || !p->content_id[0]) {
    snprintf(e->block, sizeof(e->block), "Dem Paket fehlen Titel-ID oder Inhalts-ID.");
    e->http = 409;
    return;
  }
  if(strcmp(p->kind, "base") && strcmp(p->kind, "update") && strcmp(p->kind, "dlc")) {
    snprintf(e->block, sizeof(e->block), "Die Art des Pakets ist unbekannt.");
    e->http = 409;
    return;
  }

  ps5tm_gamestate_t gs;
  ps5tm_gamestate_get(&gs);
  if(gs.title_id[0] && !strcmp(gs.title_id, p->title_id) && strcmp(p->kind, "dlc")) {
    snprintf(e->block, sizeof(e->block), "Das Spiel läuft gerade. Bitte erst beenden.");
    e->http = 409;
    return;
  }
  if(other_job_active() || ps5tm_pkginst_busy()) {
    snprintf(e->block, sizeof(e->block), "Es läuft gerade ein Kopieren, Konvertieren, Verschieben, Sichern oder Teilen, oder schon eine Installation. Das läuft nicht gleichzeitig.");
    e->http = 409;
    return;
  }

  if(!strcmp(p->kind, "dlc")) {
    e->content_there = content_present(p);
    if(e->content_there == 1) {
      snprintf(e->block, sizeof(e->block), "Dieser Zusatzinhalt ist schon installiert.");
      e->http = 409;
      return;
    }
    if(e->content_there < 0) add_warn(e, "Ob dieser Zusatzinhalt schon installiert ist, ließ sich nicht feststellen.");
    e->installed = installed_state(p->title_id, e->installed_ver, sizeof(e->installed_ver));
  } else {
    e->installed = installed_state(p->title_id, e->installed_ver, sizeof(e->installed_ver));
    if(!strcmp(p->kind, "base") && e->installed == 1) {
      if(e->installed_ver[0])
        snprintf(e->block, sizeof(e->block), "Dieses Spiel ist schon installiert (%s). Die App überschreibt nichts; zum Neuinstallieren erst an der Konsole löschen.", e->installed_ver);
      else
        snprintf(e->block, sizeof(e->block), "Die Konsole kennt dieses Spiel schon (Version unbekannt), vielleicht von einem abgebrochenen Versuch. Bitte in der Spielebibliothek der Konsole nachsehen. Die App überschreibt nichts; zum Neuinstallieren erst dort löschen.");
      e->http = 409;
      return;
    }
    if(!strcmp(p->kind, "base") && e->installed < 0) {
      snprintf(e->block, sizeof(e->block), "Die App kann nicht prüfen, ob dieses Spiel schon installiert ist (die Liste der Konsole ließ sich nicht lesen). Aus Vorsicht wird nichts installiert: Die App überschreibt nichts.");
      e->http = 409;
      return;
    }
    if(!strcmp(p->kind, "update")) {
      if(e->installed == 0) {
        snprintf(e->block, sizeof(e->block), "Das Spiel ist nicht installiert. Ein Update braucht das installierte Spiel.");
        e->http = 409;
        return;
      }
      if(e->installed == 1 && e->installed_ver[0] && p->version[0]) {
        char want[48];
        norm_version(p->version, want, sizeof(want));
        if(ver_cmp(e->installed_ver, want) >= 0) {
          snprintf(e->block, sizeof(e->block), "Dieses Update (%s) oder ein neueres ist schon installiert (%s).", p->version, e->installed_ver);
          e->http = 409;
          return;
        }
      }
      if(e->installed < 0)
        add_warn(e, "Ob das Spiel schon installiert ist und welche Version es hat, ließ sich nicht feststellen.");
      else if(e->installed == 1 && (!e->installed_ver[0] || !p->version[0]))
        add_warn(e, "Die installierte Version oder die des Updates ist unbekannt; ob das Update neuer ist, ließ sich nicht prüfen. Auch nach der Installation lässt sich das Ergebnis dann nicht bestätigen.");
    }
  }

  ps5tm_pkgdrive_t dr[PS5TM_MAX_VOLUMES];
  unsigned nd = ps5tm_pkg_drives(dr, PS5TM_MAX_VOLUMES);
  for(unsigned i = 0; i < nd; i++)
    if(dr[i].internal) { e->free_bytes = dr[i].free_bytes; e->free_known = 1; break; }
  if(!e->free_known)
    add_warn(e, "Wie viel Platz auf der Konsole frei ist, ließ sich nicht feststellen.");
  else if(e->free_bytes < e->total + PI_SPACE_SLACK)
    add_warn(e, "Auf dem internen Speicher sind %.1f GB frei, das Paket ist %.1f GB groß. Je nach Einstellung installiert die Konsole auf der M.2-Erweiterung, sonst meldet sie bei Platzmangel selbst einen Fehler.",
             (double)e->free_bytes / 1073741824.0, (double)e->total / 1073741824.0);
}

cJSON *
ps5tm_pkginst_plan_json(const char *id, int *http) {
  eval_t *e = calloc(1, sizeof(*e));
  if(!e) { *http = 503; return NULL; }
  evaluate(id, e);
  const ps5tm_pkg_t *p = &e->pkg;
  cJSON *o = cJSON_CreateObject();
  if(!o) { free(e); *http = 503; return NULL; }
  *http = e->http == 404 ? 404 : 200;
  cJSON_AddBoolToObject(o, "ok", 1);
  cJSON_AddBoolToObject(o, "can_install", e->block[0] == 0);
  cJSON_AddStringToObject(o, "blocked", e->block);
  cJSON_AddStringToObject(o, "warning", e->warn);
  if(e->http != 404) {
    cJSON_AddStringToObject(o, "id", id);
    cJSON_AddStringToObject(o, "name", p->name);
    cJSON_AddStringToObject(o, "file", p->parts ? (p->orig_file[0] ? p->orig_file : p->file) : p->file);
    cJSON_AddStringToObject(o, "title_id", p->title_id);
    cJSON_AddStringToObject(o, "content_id", p->content_id);
    cJSON_AddStringToObject(o, "version", p->version);
    cJSON_AddStringToObject(o, "kind", p->kind);
    cJSON_AddNumberToObject(o, "plat", p->plat);
    cJSON_AddNumberToObject(o, "size", (double)e->total);
    cJSON_AddNumberToObject(o, "parts", e->nslices > 1 ? e->nslices : 0);
    cJSON *in = cJSON_AddObjectToObject(o, "installed");
    cJSON_AddNumberToObject(in, "state", e->installed);           /* 1 listed, 0 not, -1 unknown */
    cJSON_AddStringToObject(in, "version", e->installed_ver);
    cJSON *sp = cJSON_AddObjectToObject(o, "space");
    cJSON_AddBoolToObject(sp, "known", e->free_known);
    cJSON_AddNumberToObject(sp, "free", (double)e->free_bytes);
    cJSON_AddNumberToObject(sp, "needed", (double)e->total);
  }
  free(e);
  return o;
}

/* ------------------------------------------------------------------ the helper */

typedef struct {
  int      fd;                    /* the loopback connection to it */
  int      loader_fd;             /* the loader's side: its output, and the process lives as long as it is open */
  int32_t  pid;
  uint32_t seq;
  char     token[PKGI_TOKEN_LEN + 1];
  /* At most one question is under way at a time: the next is sent when the last one's answer has been read. A helper
     that is busy in a call of the system library then never has a question waiting unread behind it, which is what
     would keep it from seeing that the app has gone (and what a CLOSE sent into a stuck helper would be). */
  int           pending;
  uint32_t      pending_seq, pending_op;
  unsigned char rbuf[sizeof(pkgi_response_t)];   /* the answer so far: a wait that runs out in the middle of one keeps it */
  size_t        rgot;
  char     out[400];              /* what the loader printed on its connection (the helper itself prints nothing there) */
  size_t   out_n;
} helper_t;

static int64_t
now_ms(void) { return (int64_t)ps5tm_mono_ms(); }

/* Why the last exchange with the helper failed, for the log. On the console (05.10.2026) every helper lost its connection
   at its second question about the progress, and the helper saw the app close while the app saw the helper close: only
   the two reports side by side tell which descriptor went. Only the job's thread talks to a helper. */
static struct { const char *where; int revents, err; long n; } g_ipcf;

static int
ipc_fail(const char *where, int revents, long n, int err) {
  g_ipcf.where = where;
  g_ipcf.revents = revents;
  g_ipcf.n = n;
  g_ipcf.err = err;
  return PKGI_E_DISCONNECTED;
}

/* What a descriptor is now, for the log: still a socket (and to which port), something else, or nothing at all. */
static void
fd_describe(int fd, char *out, size_t n) {
  struct stat st;
  if(fd < 0) { snprintf(out, n, "keine Verbindung"); return; }
  if(fstat(fd, &st) != 0) { snprintf(out, n, "fd %d ist geschlossen (errno %d)", fd, errno); return; }
  if(!S_ISSOCK(st.st_mode)) { snprintf(out, n, "fd %d ist kein Socket mehr", fd); return; }
  struct sockaddr_in pa;
  socklen_t pl = sizeof(pa);
  int so = 0;
  socklen_t sl = sizeof(so);
  if(getsockopt(fd, SOL_SOCKET, SO_ERROR, &so, &sl) != 0) so = -errno;
  if(getpeername(fd, (struct sockaddr *)&pa, &pl) != 0) snprintf(out, n, "fd %d: Socket ohne Gegenstelle (errno %d, Fehler %d)", fd, errno, so);
  else snprintf(out, n, "fd %d: Socket zu Port %u (Fehler %d)", fd, (unsigned)ntohs(pa.sin_port), so);
}

/* The connection to the helper on a high descriptor number, 200 and up: a library that closes a small number it takes for
   its own (the likeliest reason for the losses on the console) does not find it there. Where the system refuses, the
   descriptor stays as it is. */
static int
fd_high(int fd, int min) {
  int h = fcntl(fd, F_DUPFD, min);
  if(h < 0) return fd;
  close(fd);
  return h;
}

/* All of buf in or out, or an error code. Looks at the cancel flag every tenth of a second. */
static int
ipc_transfer(int fd, void *buf, size_t size, int sending, int timeout_ms, int honour_cancel) {
  int64_t deadline = now_ms() + timeout_ms;
  unsigned char *p = buf;
  while(size) {
    if(honour_cancel && cancelled()) return PKGI_E_CANCELED;
    int64_t left = deadline - now_ms();
    if(left <= 0) return PKGI_E_TIMEOUT;
    struct pollfd pf = { fd, sending ? POLLOUT : POLLIN, 0 };
    int r = poll(&pf, 1, left > 100 ? 100 : (int)left);
    if(r < 0 && errno == EINTR) continue;
    if(r < 0) return ipc_fail("poll gescheitert", 0, r, errno);
    if(r == 0) continue;
    if(!(pf.revents & pf.events)) return ipc_fail("aufgelegt oder Fehler, nichts zu lesen", pf.revents, 0, 0);
    ssize_t n = sending ? send(fd, p, size, MSG_DONTWAIT | MSG_NOSIGNAL) : recv(fd, p, size, MSG_DONTWAIT);
    if(n < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
    if(n <= 0) return ipc_fail(n == 0 ? "die Gegenstelle hat geschlossen" : sending ? "Senden gescheitert" : "Empfangen gescheitert",
                               pf.revents, (long)n, n < 0 ? errno : 0);
    p += n;
    size -= (size_t)n;
  }
  return 0;
}

/* Reads `want` bytes into buf, of which *got are there already; a wait that runs out keeps what has come. */
static int
ipc_recv(int fd, unsigned char *buf, size_t *got, size_t want, int timeout_ms, int honour_cancel) {
  int64_t deadline = now_ms() + timeout_ms;
  while(*got < want) {
    if(honour_cancel && cancelled()) return PKGI_E_CANCELED;
    int64_t left = deadline - now_ms();
    if(left <= 0) return PKGI_E_TIMEOUT;
    struct pollfd pf = { fd, POLLIN, 0 };
    int r = poll(&pf, 1, left > 100 ? 100 : (int)left);
    if(r < 0 && errno == EINTR) continue;
    if(r < 0) return ipc_fail("poll gescheitert", 0, r, errno);
    if(r == 0) continue;
    if(!(pf.revents & POLLIN)) return ipc_fail("aufgelegt oder Fehler, nichts zu lesen", pf.revents, 0, 0);
    ssize_t n = recv(fd, buf + *got, want - *got, MSG_DONTWAIT);
    if(n < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
    if(n <= 0) return ipc_fail(n == 0 ? "die Gegenstelle hat geschlossen" : "Empfangen gescheitert", pf.revents, (long)n, n < 0 ? errno : 0);
    *got += (size_t)n;
  }
  return 0;
}

/* The question goes out. Only one may be under way: see helper_t. */
static int
helper_send(helper_t *h, pkgi_request_t *req, int honour_cancel) {
  if(h->pending) return PKGI_E_BADREPLY;
  memset(&g_ipcf, 0, sizeof(g_ipcf));
  req->magic = PKGI_MAGIC;
  req->version = PKGI_VERSION;
  req->seq = ++h->seq;
  int rc = ipc_transfer(h->fd, req, sizeof(*req), 1, 10000, honour_cancel);
  if(rc != 0) return rc;
  h->pending = 1;
  h->pending_seq = req->seq;
  h->pending_op = req->op;
  h->rgot = 0;
  return 0;
}

/* The answer to the question that is under way. PKGI_E_TIMEOUT leaves the question pending: call again to go on waiting
   for the same answer. The answer's header must be the one asked for; an answer to an earlier question (one that was
   given up on) is read and dropped. */
static int
helper_recv(helper_t *h, pkgi_response_t *rsp, int timeout_ms, int honour_cancel) {
  memset(rsp, 0, sizeof(*rsp));
  if(!h->pending) return PKGI_E_BADREPLY;
  int64_t deadline = now_ms() + timeout_ms;
  for(unsigned skipped = 0;; skipped++) {
    int64_t left = deadline - now_ms();
    int rc = ipc_recv(h->fd, h->rbuf, &h->rgot, sizeof(h->rbuf), left > 0 ? (int)left : 1, honour_cancel);
    if(rc != 0) return rc;
    h->rgot = 0;
    memcpy(rsp, h->rbuf, sizeof(*rsp));
    rsp->token[sizeof(rsp->token) - 1] = 0;                              /* whatever it sent, it is a string now */
    rsp->build[sizeof(rsp->build) - 1] = 0;
    rsp->content_id[sizeof(rsp->content_id) - 1] = 0;
    rsp->status[sizeof(rsp->status) - 1] = 0;
    rsp->src_type[sizeof(rsp->src_type) - 1] = 0;
    rsp->error_type[sizeof(rsp->error_type) - 1] = 0;
    rsp->error_desc[sizeof(rsp->error_desc) - 1] = 0;
    if(rsp->magic != PKGI_MAGIC || rsp->version != PKGI_VERSION || rsp->pid != h->pid) { h->pending = 0; return PKGI_E_BADREPLY; }
    if(rsp->seq == h->pending_seq) break;
    if((int32_t)(rsp->seq - h->pending_seq) < 0 && skipped < 16) continue;
    h->pending = 0;
    return PKGI_E_BADREPLY;
  }
  h->pending = 0;
  if(rsp->op != h->pending_op) return PKGI_E_BADREPLY;
  return 0;
}

/* One question, one answer. */
static int
helper_ask(helper_t *h, pkgi_request_t *req, pkgi_response_t *rsp, int timeout_ms, int honour_cancel) {
  memset(rsp, 0, sizeof(*rsp));
  int rc = helper_send(h, req, honour_cancel);
  if(rc != 0) return rc;
  return helper_recv(h, rsp, timeout_ms, honour_cancel);
}

static void
make_token(char out[PKGI_TOKEN_LEN + 1]) {
  unsigned char r[PKGI_TOKEN_LEN / 2];
  int fd = open("/dev/urandom", O_RDONLY);
  ssize_t got = fd >= 0 ? read(fd, r, sizeof(r)) : -1;
  if(fd >= 0) close(fd);
  if(got != (ssize_t)sizeof(r)) {                         /* no randomness device: the clock will have to do */
    uint64_t t = ps5tm_mono_ms() * 6364136223846793005ull + (uint64_t)getpid();
    for(size_t i = 0; i < sizeof(r); i++) { t = t * 6364136223846793005ull + 1442695040888963407ull; r[i] = (unsigned char)(t >> 33); }
  }
  static const char hex[] = "0123456789abcdef";
  for(size_t i = 0; i < sizeof(r); i++) { out[2 * i] = hex[r[i] >> 4]; out[2 * i + 1] = hex[r[i] & 15]; }
  out[PKGI_TOKEN_LEN] = 0;
}

/* A copy of the helper's image with the token written into its place. NULL (and a reason) when the image has no
   such place exactly once. */
static unsigned char *
image_with_token(const char *token, size_t *len, char *why, size_t why_len) {
  size_t n = (size_t)ps5tm_pkginst_helper_len;
  if(n < 64) { snprintf(why, why_len, "Der Installationshelfer ist in dieser Fassung der App nicht enthalten."); return NULL; }
  unsigned char *img = malloc(n);
  if(!img) { snprintf(why, why_len, "Zu wenig Speicher."); return NULL; }
  memcpy(img, ps5tm_pkginst_helper, n);
  const char *mark = PKGI_TOKEN_MARK;
  size_t ml = strlen(mark), at = (size_t)-1;
  unsigned count = 0;
  for(size_t i = 0; i + ml + PKGI_TOKEN_LEN <= n; i++) {
    if(img[i] == (unsigned char)mark[0] && !memcmp(img + i, mark, ml)) { at = i; count++; }
  }
  if(count != 1) {
    free(img);
    snprintf(why, why_len, "Der Installationshelfer in dieser App ist beschädigt (Kennung %u-mal gefunden).", count);
    return NULL;
  }
  memcpy(img + at + ml, token, PKGI_TOKEN_LEN);
  *len = n;
  return img;
}

static int
connect_loopback(int port, int timeout_ms) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if(fd < 0) return -1;
  int fl = fcntl(fd, F_GETFL, 0);
  fcntl(fd, F_SETFL, fl | O_NONBLOCK);
  struct sockaddr_in sa;
  memset(&sa, 0, sizeof(sa));
  sa.sin_family = AF_INET;
  sa.sin_port = htons((uint16_t)port);
  sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if(connect(fd, (struct sockaddr *)&sa, sizeof(sa)) != 0 && errno != EINPROGRESS) { close(fd); return -1; }
  struct pollfd pf = { fd, POLLOUT, 0 };
  int soerr = 0;
  socklen_t sl = sizeof(soerr);
  if(poll(&pf, 1, timeout_ms) != 1 || getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &sl) != 0 || soerr != 0) { close(fd); return -1; }
  return fd;
}

/* The image to the payload loader. The connection stays open: the program the loader starts has this socket for its
   output, and a loader that sees it go away may take the program with it. Returns the socket, or -1. */
static int
loader_send(const unsigned char *elf, size_t len, char *why, size_t why_len) {
  int fd = connect_loopback(PS5TM_LOADER_PORT, 3000);
  if(fd < 0) { snprintf(why, why_len, "Der Payload-Lader (Port %d) antwortet nicht. Läuft elfldr?", PS5TM_LOADER_PORT); return -1; }
  size_t off = 0;
  int64_t deadline = now_ms() + 20000;
  while(off < len) {
    if(cancelled()) { close(fd); snprintf(why, why_len, "Abgebrochen."); return -1; }       /* a loader that does not read must not hold a cancel for 20 s */
    ssize_t w = send(fd, elf + off, len - off, MSG_NOSIGNAL);
    if(w > 0) { off += (size_t)w; continue; }
    if(w < 0 && (errno == EAGAIN || errno == EINTR)) {
      struct pollfd pf = { fd, POLLOUT, 0 };
      int64_t left = deadline - now_ms();
      if(left <= 0 || poll(&pf, 1, left > 100 ? 100 : (int)left) < 0) break;
      continue;
    }
    break;
  }
  if(off != len) { close(fd); snprintf(why, why_len, "Das Senden des Installationshelfers an den Payload-Lader ist abgebrochen oder zu langsam (der Lader nimmt es nicht an)."); return -1; }
  shutdown(fd, SHUT_WR);                                    /* the end of the stream is the end of the file */
  return fd;
}

/* Reads what the loader says on its connection (it must be read, or the loader's writes would block), and keeps the
   first few hundred bytes: when the helper never reports, that is often the only word of what went wrong. */
static void
drain_loader(helper_t *h) {
  if(h->loader_fd < 0) return;
  char buf[512];
  for(;;) {
    ssize_t n = recv(h->loader_fd, buf, sizeof(buf), MSG_DONTWAIT);
    if(n > 0) {
      size_t room = sizeof(h->out) - 1 - h->out_n;
      size_t take = (size_t)n < room ? (size_t)n : room;
      memcpy(h->out + h->out_n, buf, take);
      h->out_n += take;
      h->out[h->out_n] = 0;
      continue;
    }
    if(n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) { close(h->loader_fd); h->loader_fd = -1; }
    return;
  }
}

/* The helper writes its own log file (pkginst-helper.log). After a failure its lines go into the app's log, where a
   person looking for the reason will find them next to the app's own; the end of the file is what matters. */
static void
dump_helper_log(void) {
  int fd = open(PKGI_LOG_PATH, O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
  if(fd < 0) { PS5TM_INFO("pkg_helper_log", "Helfer: keine Protokolldatei (%s), er hat nichts geschrieben.", strerror(errno)); return; }
  char buf[3072];
  off_t end = lseek(fd, 0, SEEK_END);
  off_t from = end > (off_t)(sizeof(buf) - 1) ? end - (off_t)(sizeof(buf) - 1) : 0;
  ssize_t n = pread(fd, buf, sizeof(buf) - 1, from);
  close(fd);
  if(n <= 0) { PS5TM_INFO("pkg_helper_log", "Helfer: die Protokolldatei ist leer, er hat nichts geschrieben."); return; }
  buf[n] = 0;
  char *lines[14];
  unsigned nl = 0;
  char *save = NULL;
  for(char *ln = strtok_r(buf, "\r\n", &save); ln; ln = strtok_r(NULL, "\r\n", &save)) {
    if(nl == sizeof(lines) / sizeof(lines[0])) { memmove(lines, lines + 1, (nl - 1) * sizeof(lines[0])); nl--; }
    lines[nl++] = ln;
  }
  for(unsigned i = 0; i < nl; i++) PS5TM_INFO("pkg_helper_log", "Helfer: %.170s", lines[i]);
}

static void
helper_kill(helper_t *h) {
  if(h->pid > 0) {
    kill((pid_t)h->pid, SIGKILL);
    h->pid = 0;
  }
}

/* Lets the helper go: its end of the socket ends, which it takes as the end, and a helper that is stuck in a call is killed.
   There is no polite goodbye (it used to ask the library to end its session, and that is what crashed the first helper a
   moment after the call: see pkginst_helper.c). Closes everything. */
static void
helper_close(helper_t *h) {
  if(h->fd >= 0) {
    shutdown(h->fd, SHUT_RDWR);
    close(h->fd);
    h->fd = -1;
  }
  if(h->pid > 0) {
    int gone = 0;
    for(int i = 0; i < 100; i++) {                          /* up to a second: the helper ends when its end of the socket does */
      if(kill((pid_t)h->pid, 0) != 0 && errno == ESRCH) { gone = 1; break; }
      usleep(10000);
    }
    if(!gone) helper_kill(h);
    h->pid = 0;
  }
  if(h->loader_fd >= 0) { close(h->loader_fd); h->loader_fd = -1; }
}

#define PI_PENDING_MAX 8

/* A connection to the helper's port that has not said yet who it is. */
typedef struct {
  int           fd;
  size_t        got;
  unsigned char buf[sizeof(pkgi_response_t)];
} hs_conn_t;

static void
hs_close_all(hs_conn_t *pend) {
  for(unsigned i = 0; i < PI_PENDING_MAX; i++)
    if(pend[i].fd >= 0) { close(pend[i].fd); pend[i].fd = -1; }
}

/* Starts a helper and takes its READY. 0, or a PKGI_E_ code with the reason in why. keep_log: the helper's log is not
   emptied first (a helper that takes over from another adds to what the first one wrote). */
static int
helper_start(helper_t *h, char *why, size_t why_len, int keep_log) {
  memset(h, 0, sizeof(*h));
  h->fd = h->loader_fd = -1;
  why[0] = 0;
  make_token(h->token);
  size_t len = 0;
  unsigned char *img = image_with_token(h->token, &len, why, why_len);
  if(!img) return PKGI_E_UNAVAILABLE;

  /* The helper's log starts empty (it is the app's own file, emptied, not removed), so that what is in it afterwards is
     what this try did. */
  int lg = open(PKGI_LOG_PATH, O_WRONLY | O_CREAT | (keep_log ? 0 : O_TRUNC) | O_NOFOLLOW, 0666);
  if(lg >= 0) close(lg);

  int lfd = socket(AF_INET, SOCK_STREAM, 0);
  struct sockaddr_in sa;
  memset(&sa, 0, sizeof(sa));
  sa.sin_family = AF_INET;
  sa.sin_port = htons(PKGI_IPC_PORT);
  sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  int one = 1;
  if(lfd >= 0) setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  if(lfd < 0 || bind(lfd, (struct sockaddr *)&sa, sizeof(sa)) != 0 || listen(lfd, 8) != 0) {
    int e = errno;
    if(lfd >= 0) close(lfd);
    free(img);
    snprintf(why, why_len, "Der Anschluss %d für den Installationshelfer ist belegt oder nicht nutzbar (%s).", PKGI_IPC_PORT, strerror(e));
    return PKGI_E_UNAVAILABLE;
  }
  h->loader_fd = loader_send(img, len, why, why_len);
  free(img);
  if(h->loader_fd < 0) { close(lfd); return cancelled() ? PKGI_E_CANCELED : PKGI_E_UNAVAILABLE; }
  int fl = fcntl(h->loader_fd, F_GETFL, 0);
  fcntl(h->loader_fd, F_SETFL, fl | O_NONBLOCK);

  hs_conn_t *pend = calloc(PI_PENDING_MAX, sizeof(*pend));
  if(!pend) { close(lfd); helper_close(h); snprintf(why, why_len, "Zu wenig Speicher."); return PKGI_E_UNAVAILABLE; }
  for(unsigned i = 0; i < PI_PENDING_MAX; i++) pend[i].fd = -1;

  /* Whoever connects to the port is asked for the token it was given, and every connection is listened to at the same
     time: the helper reports only after the system library has started, which takes its time, and a program that
     connects and says nothing must not stand in front of it. A helper left over from an earlier try (started after
     its job had been stopped) or any other program is turned away; the wait goes on for the right one. */
  int64_t t0 = now_ms(), loader_gone_at = 0;
  const int64_t overall = t0 + PI_ACCEPT_MS + PI_READY_MS;
  int ready = 0, turned_away = 0, answered = 0, strays = 0;
  pkgi_response_t rsp;
  memset(&rsp, 0, sizeof(rsp));
  while(!ready && now_ms() < overall) {
    if(cancelled()) { hs_close_all(pend); free(pend); close(lfd); helper_close(h); return PKGI_E_CANCELED; }
    struct pollfd pf[2 + PI_PENDING_MAX];
    int slot_of[2 + PI_PENDING_MAX];
    unsigned np = 0;
    pf[np] = (struct pollfd){ lfd, POLLIN, 0 }; slot_of[np++] = -1;
    if(h->loader_fd >= 0) { pf[np] = (struct pollfd){ h->loader_fd, POLLIN, 0 }; slot_of[np++] = -1; }
    for(unsigned i = 0; i < PI_PENDING_MAX; i++)
      if(pend[i].fd >= 0) { pf[np] = (struct pollfd){ pend[i].fd, POLLIN, 0 }; slot_of[np++] = (int)i; }
    int r = poll(pf, np, 200);
    if(r < 0 && errno != EINTR) break;
    if(r > 0 && (pf[0].revents & POLLIN)) {
      struct sockaddr_in peer;
      socklen_t pl = sizeof(peer);
      int c = accept(lfd, (struct sockaddr *)&peer, &pl);
      if(c >= 0 && peer.sin_addr.s_addr != htonl(INADDR_LOOPBACK)) { close(c); c = -1; }
      if(c >= 0) {
        unsigned i = 0;
        while(i < PI_PENDING_MAX && pend[i].fd >= 0) i++;
        if(i == PI_PENDING_MAX) { close(c); strays++; }                     /* more than eight at once: not the helper's way */
        else { pend[i].fd = c; pend[i].got = 0; }
      }
    }
    if(r > 0) {
      for(unsigned k = 1; k < np && !ready; k++) {
        int i = slot_of[k];
        if(i < 0 || !(pf[k].revents & (POLLIN | POLLHUP | POLLERR))) continue;
        hs_conn_t *c = &pend[i];
        ssize_t n = recv(c->fd, c->buf + c->got, sizeof(c->buf) - c->got, MSG_DONTWAIT);
        if(n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) continue;
        if(n <= 0) { close(c->fd); c->fd = -1; strays++; continue; }          /* it left without saying who it was */
        c->got += (size_t)n;
        if(c->got < sizeof(c->buf)) continue;                                /* the rest comes */
        memcpy(&rsp, c->buf, sizeof(rsp));
        rsp.token[PKGI_TOKEN_LEN] = 0;
        rsp.build[sizeof(rsp.build) - 1] = 0;
        if(rsp.magic == PKGI_MAGIC && rsp.version == PKGI_VERSION && rsp.op == PKGI_OP_READY && rsp.pid > 0) {
          answered = 1;
          if(!strcmp(rsp.token, h->token)) { h->fd = fd_high(c->fd, 200); c->fd = -1; ready = 1; }
          else { close(c->fd); c->fd = -1; turned_away = 1; }
        } else {
          close(c->fd);                                                      /* it said something that is nothing of ours */
          c->fd = -1;
          strays++;
        }
      }
    }
    if(!ready && h->loader_fd >= 0) {
      drain_loader(h);
      if(h->loader_fd < 0 && !loader_gone_at) loader_gone_at = now_ms();      /* the helper ended before it connected */
    }
    if(!ready && loader_gone_at && now_ms() - loader_gone_at > PI_LOADER_GONE_MS) break;
  }
  hs_close_all(pend);
  free(pend);
  close(lfd);
  if(!ready) {
    drain_loader(h);
    if(h->out_n) PS5TM_WARN("pkg_loader_output", "Lader: %.170s", h->out);
    if(strays || turned_away) PS5TM_WARN("pkg_helper_strays", "Am Anschluss des Installationshelfers: %d Verbindung(en) ohne Meldung oder mit fremdem Inhalt, %d mit falscher Kennung.", strays, turned_away);
    dump_helper_log();
    helper_close(h);
    if(turned_away) snprintf(why, why_len, "Der Installationshelfer hat sich mit der falschen Kennung gemeldet.");
    else if(answered) snprintf(why, why_len, "Der Installationshelfer hat sich nicht richtig gemeldet.");
    else snprintf(why, why_len, "Der Installationshelfer hat sich nicht gemeldet: Entweder wurde er nicht gestartet, oder die Installationsbibliothek der Konsole hängt schon beim Start. Einzelheiten stehen in %s.", "pkginst-helper.log");
    return turned_away ? PKGI_E_BADREPLY : PKGI_E_UNAVAILABLE;
  }
  h->pid = rsp.pid;
  PS5TM_INFO("pkg_install_helper", "Installationshelfer bereit (PID %d, Aufbau %.20s, Start der Bibliothek %u ms, Ergebnis 0x%08X, Verbindung fd %d).",
             (int)rsp.pid, rsp.build, (unsigned)rsp.native_ms, (unsigned)rsp.result, h->fd);
  if(rsp.result != 0) {
    char d[400];
    describe_native(rsp.result, d, sizeof(d));
    dump_helper_log();
    helper_close(h);
    snprintf(why, why_len, "Die Installationsbibliothek der Konsole ließ sich nicht starten: %s", d);
    return rsp.result;
  }
  return 0;
}

/* ------------------------------------------------------------------ taking a title off (gamedelete.c) */

/* The system's own uninstall of a title, the way the console's menu does it, made by a helper of its own: the same
   isolation as an installation (whatever the library keeps or hangs on stays in that process). The game first, then
   its updates and add-ons; saved games are not touched by any of these calls. Synchronous, for the delete job, which
   never runs beside an installation. 0 when the system took it; else the system's code or a PKGI_E_ one, and why. */
int
ps5tm_pkginst_uninstall(const char *title_id, int *res_pat, int *res_addcont, char *why, size_t why_len) {
  *res_pat = *res_addcont = 0;
  why[0] = 0;
  pthread_mutex_lock(&g_lock);
  int busy = active_locked();
  if(!busy) __atomic_store_n(&g_cancel, 0, __ATOMIC_RELEASE);   /* a cancel of an earlier installation is not this one's */
  pthread_mutex_unlock(&g_lock);
  if(busy) { snprintf(why, why_len, "Es wird gerade ein Paket installiert."); return PKGI_E_UNAVAILABLE; }

  helper_t h;
  int rc = helper_start(&h, why, why_len, 0);
  if(rc != 0) return rc;                                       /* helper_start has let it go already */
  pkgi_request_t req;
  pkgi_response_t rsp;
  memset(&req, 0, sizeof(req));
  req.op = PKGI_OP_UNINSTALL;
  snprintf(req.title_id, sizeof(req.title_id), "%s", title_id);
  rc = helper_ask(&h, &req, &rsp, PI_UNINSTALL_MS, 0);
  if(rc == 0) {
    rc = rsp.result;
    *res_pat = rsp.result_pat;
    *res_addcont = rsp.result_addcont;
    PS5TM_INFO("pkg_uninstall_called", "Deinstallieren von %s: Ergebnis 0x%08X nach %u ms (Updates 0x%08X, Zusatzinhalte 0x%08X).",
               title_id, (unsigned)rc, (unsigned)rsp.native_ms, (unsigned)rsp.result_pat, (unsigned)rsp.result_addcont);
    if(rc != 0) describe_native(rc, why, why_len);
  } else {
    describe_local(rc, why, why_len);
    dump_helper_log();
  }
  drain_loader(&h);
  helper_close(&h);
  return rc;
}

/* ------------------------------------------------------------------ the job */

typedef struct {
  ps5tm_pkg_t      pkg;
  ps5tm_pkgslice_t slices[PI_MAX_SLICES];
  unsigned         nslices;
  uint64_t         total;
} args_t;

static void
names_for(char *pkg_name, size_t pn, char *icon_name, size_t in, uint64_t *ts) {
  *ts = (uint64_t)time(NULL);
  unsigned seq = ++g_seq;
  snprintf(pkg_name, pn, "package-%llu-%u.pkg", (unsigned long long)*ts, seq);
  snprintf(icon_name, in, "icon-%llu-%u.png", (unsigned long long)*ts, seq);
}

static void
sleep_ms_cancellable(int ms) {
  for(int i = 0; i < ms / 50 && !cancelled(); i++) usleep(50000);
}

static void *
install_main(void *arg) {
  args_t *a = arg;
  const ps5tm_pkg_t *p = &a->pkg;
  ps5tm_powerguard_hold();
  {
    char gt[24];
    gb_text(a->total, gt, sizeof(gt));
    PS5TM_INFO("pkg_install_start", "Pakete: Installation von „%.60s“ (%s, %s, %u Teile, %s) beginnt.", p->name, p->title_id, p->kind, a->nslices, gt);
  }

  /* A package from the browser (pkglive.c): the session is read by this installation from here on, and it is over
     when this job is. */
  const char *live_id = !strncmp(p->path, PKGLIVE_PREFIX, strlen(PKGLIVE_PREFIX)) ? p->path + strlen(PKGLIVE_PREFIX) : NULL;
  char live_buf[24] = "";
  if(live_id) {
    snprintf(live_buf, sizeof(live_buf), "%s", live_id);
    live_id = live_buf;
    ps5tm_pkglive_installing(live_id, 1);
  }

  uint8_t *icon = NULL;
  size_t icon_n = 0;
  if(p->icon_size > 0 && (live_id ? ps5tm_pkglive_icon(p, &icon, &icon_n) : ps5tm_pkg_icon(p, &icon, &icon_n)) != 0) {
    icon = NULL;
    icon_n = 0;
    PS5TM_INFO("pkg_install_icon", "Pakete: das Bild des Pakets ließ sich nicht lesen; die Konsole zeigt dann keins.");
  }

  helper_t h;
  memset(&h, 0, sizeof(h));
  h.fd = h.loader_fd = -1;
  int stream_up = 0, result = J_FAILED;
  char err[512] = "";
  /* the last thing the console said about an error, for the log if the installation fails */
  int      last_native = 0;
  char     last_err_type[10] = "", last_err_desc[200] = "";
  uint64_t began_ms = mono_ms();
  ps5tm_pkgstream_stats_t fin;
  memset(&fin, 0, sizeof(fin));

  for(unsigned attempt = 1; attempt <= PI_ATTEMPTS; attempt++) {
    pthread_mutex_lock(&g_lock);
    g_job.attempt = attempt;
    pthread_mutex_unlock(&g_lock);
    if(cancelled()) { result = J_CANCELLED; break; }

    char pkg_name[96], icon_name[96];
    uint64_t ts;
    names_for(pkg_name, sizeof(pkg_name), icon_name, sizeof(icon_name), &ts);
    ps5tm_pkgstream_cfg_t cfg = { a->slices, a->nslices, a->total, pkg_name, icon, icon_n, icon_name, PKGI_STREAM_PORT };
    if(stream_up) { ps5tm_pkgstream_stop(); stream_up = 0; }
    job_phase(J_START, attempt == 1 ? "Der Server für die Konsole wird gestartet" : "Neuer Versuch (%u von %u): der Server wird gestartet", attempt, PI_ATTEMPTS);
    if(ps5tm_pkgstream_start(&cfg, err, sizeof(err)) != 0) { job_fail(0, "%s", err); result = J_FAILED; goto out; }
    stream_up = 1;

    job_phase(J_START, "Der Installationshelfer wird gestartet");
    char why[400];
    int rc = helper_start(&h, why, sizeof(why), 0);
    if(rc == PKGI_E_CANCELED || cancelled()) { result = J_CANCELLED; break; }
    if(rc != 0) {
      if(is_transient(rc) && attempt < PI_ATTEMPTS) {
        PS5TM_WARN("pkg_install_retry", "Pakete: Versuch %u von %d beim Start des Helfers gescheitert (0x%08X), neuer Versuch.", attempt, PI_ATTEMPTS, (unsigned)rc);
        job_note("%s Neuer Versuch …", why);
        sleep_ms_cancellable(attempt == 1 ? 2000 : 5000);
        continue;
      }
      job_fail(rc, "%s%s", why, is_transient(rc) ? advice_for(rc) : "");
      result = J_FAILED;
      goto out;
    }

    job_phase(J_START, "Die Konsole liest das Paket");
    char uri[300], icon_url[300];
    snprintf(uri, sizeof(uri), "http://127.0.0.1:%d/stream/install/%s", PKGI_STREAM_PORT, pkg_name);
    snprintf(icon_url, sizeof(icon_url), "http://127.0.0.1:%d/stream/install/%s", PKGI_STREAM_PORT, icon_name);
    pkgi_request_t req;
    pkgi_response_t rsp;
    memset(&req, 0, sizeof(req));
    req.op = PKGI_OP_INSTALL;
    snprintf(req.uri, sizeof(req.uri), "%s", uri);
    snprintf(req.name, sizeof(req.name), "%s", p->name);
    snprintf(req.icon_url, sizeof(req.icon_url), "%s", icon ? icon_url : "");
    rc = helper_ask(&h, &req, &rsp, PI_INSTALL_MS, 1);
    if(rc == PKGI_E_CANCELED || cancelled()) { result = J_CANCELLED; break; }
    if(rc != 0) {
      if(rc == PKGI_E_TIMEOUT) snprintf(why, sizeof(why), "Die Konsole hat das Paket nicht innerhalb von %d Sekunden angenommen. Einzelheiten stehen in %s.", PI_INSTALL_MS / 1000, "pkginst-helper.log");
      else describe_local(rc, why, sizeof(why));
      job_fail(rc, "%s", why);
      result = J_FAILED;
      goto out;
    }
    PS5TM_INFO("pkg_install_called", "Installationsaufruf: Ergebnis 0x%08X nach %u ms, Inhalts-ID %.50s, Art %d, Plattform %d.",
               (unsigned)rsp.result, (unsigned)rsp.native_ms, rsp.content_id, (int)rsp.type, (int)rsp.platform);
    if(rsp.result != 0) {
      describe_native(rsp.result, why, sizeof(why));
      if(is_transient(rsp.result) && attempt < PI_ATTEMPTS) {
        PS5TM_WARN("pkg_install_retry", "Pakete: Versuch %u von %d: die Konsole lehnt den Aufruf ab (0x%08X), neuer Versuch.", attempt, PI_ATTEMPTS, (unsigned)rsp.result);
        job_note("%s Neuer Versuch …", why);
        helper_close(&h);
        sleep_ms_cancellable(attempt == 1 ? 2000 : 5000);
        continue;
      }
      last_native = rsp.result;
      job_fail(rsp.result, "%s%s", why, advice_for(rsp.result));
      result = J_FAILED;
      goto out;
    }

    /* The system has the package and reads it now, and it does so on its own account: the helper that made the call is
       needed only to ask how far it is, and it can be replaced. (The first console test: the helper crashed right after
       the call, the system went on reading, and the app, which tore its server down on that, ended the installation.) */
    pthread_mutex_lock(&g_lock);
    g_job.started = 1;
    pthread_mutex_unlock(&g_lock);
    char cid[sizeof(rsp.content_id)];
    snprintf(cid, sizeof(cid), "%s", rsp.content_id[0] ? rsp.content_id : p->content_id);
    const uint64_t called_at = mono_ms();
    job_note("");
    job_phase(J_RUN, "Die Konsole installiert");
    uint64_t last_move = called_at;
    /* what counts as the console getting on: bytes it says it has, bytes the server has sent for the first time, and
       the steps it takes after the last byte (it unpacks and copies, which can take long for a big title). Bytes sent a
       second time are not progress: a console that reads the same piece over and over is not getting on. */
    uint64_t key_down = 0, key_cov = 0;
    unsigned key_promote = 0;
    int      key_copy = 0;
    char     logged_status[17] = "";
    uint64_t logged_ms = 0, fail_since = 0;
    unsigned misses = 0;
    int      attached = 1;                           /* a helper answers the questions */
    unsigned restarts = 0;                           /* helpers started after the first one, and tries to */
    uint64_t next_restart_ms = 0;
    int      blind_done = 0;                         /* all bytes went out, and nobody could ask the system: the list decides */
    for(;;) {
      if(cancelled()) { result = J_CANCELLED; goto out; }
      int answered = 0;                              /* the system said how far it is, this round */
      if(attached) {
        drain_loader(&h);
        /* One question at a time: after a miss the same one is waited for again, a new one is not put behind it. */
        rc = 0;
        if(!h.pending) {
          memset(&req, 0, sizeof(req));
          req.op = PKGI_OP_STATUS;
          snprintf(req.content_id, sizeof(req.content_id), "%s", cid);
          rc = helper_send(&h, &req, 1);
        }
        if(rc == 0) rc = helper_recv(&h, &rsp, PI_STATUS_MS, 1);
        if(rc == PKGI_E_CANCELED) { result = J_CANCELLED; goto out; }
        if(rc == PKGI_E_TIMEOUT && ++misses <= PI_STATUS_MISSES) {
          if(misses == 1) PS5TM_WARN("pkg_install_slow", "Pakete: die Konsole antwortet seit %d s nicht auf die Frage nach dem Fortschritt; die App wartet.", PI_STATUS_MS / 1000);
          job_note("Die Konsole antwortet gerade nur langsam. Die App wartet.");
          continue;
        }
        if(rc != 0) {
          /* The helper has gone (it crashed, or it was killed) or it hangs in the system library for good. The
             installation of the system does not depend on it: it is replaced, and the server stays up. */
          char how[200];
          if(rc == PKGI_E_TIMEOUT) snprintf(how, sizeof(how), "er antwortet seit %d Sekunden nicht", (int)(PI_STATUS_MS / 1000) * (int)(PI_STATUS_MISSES + 1));
          else describe_local(rc, how, sizeof(how));
          PS5TM_WARN("pkg_install_helper_lost", "Pakete: Der Installationshelfer ist weg (%.150s). Die Installation der Konsole läuft weiter; ein neuer Helfer wird gestartet.", how);
          if(rc == PKGI_E_DISCONNECTED) {
            char fdd[96];
            fd_describe(h.fd, fdd, sizeof(fdd));
            PS5TM_INFO("pkg_install_ipc", "Verbindung zum Helfer: %s (revents 0x%X, n %ld, errno %d); %s.",
                       g_ipcf.where ? g_ipcf.where : "-", g_ipcf.revents, g_ipcf.n, g_ipcf.err, fdd);
          }
          dump_helper_log();
          helper_close(&h);
          attached = 0;
          misses = 0;
          next_restart_ms = 0;
          if(restarts >= PI_RESTARTS_MAX)
            PS5TM_WARN("pkg_install_blind", "Pakete: Kein Installationshelfer mehr. Die App liefert das Paket weiter und achtet nur noch auf den Server und auf die Liste der Konsole.");
        } else {
          misses = 0;
          answered = 1;
        }
      }
      if(!attached && restarts < PI_RESTARTS_MAX && mono_ms() >= next_restart_ms) {
        restarts++;
        pthread_mutex_lock(&g_lock);
        g_job.restarts = restarts;
        pthread_mutex_unlock(&g_lock);
        job_note("Der Installationshelfer wird neu gestartet. Die Installation der Konsole läuft weiter.");
        rc = helper_start(&h, why, sizeof(why), 1);
        if(rc == PKGI_E_CANCELED || cancelled()) { result = J_CANCELLED; goto out; }
        if(rc == 0) {
          attached = 1;
          misses = 0;
          PS5TM_INFO("pkg_install_helper_restarted", "Pakete: Ein neuer Installationshelfer fragt jetzt nach dem Fortschritt (Versuch %u von %d).", restarts, PI_RESTARTS_MAX);
          continue;                                  /* and asks at once */
        }
        PS5TM_WARN("pkg_install_helper_restart_failed", "Pakete: Der neue Installationshelfer ließ sich nicht starten (Versuch %u von %d): %.200s", restarts, PI_RESTARTS_MAX, why);
        next_restart_ms = mono_ms() + (uint64_t)PI_RESTART_WAIT_MS * restarts;
        if(restarts >= PI_RESTARTS_MAX)
          PS5TM_WARN("pkg_install_blind", "Pakete: Kein Installationshelfer mehr. Die App liefert das Paket weiter und achtet nur noch auf den Server und auf die Liste der Konsole.");
      }
      ps5tm_pkgstream_stats_t st;
      ps5tm_pkgstream_stats(&st);
      /* A file that could not be read is the reason when the system then reports a failure (it saw the connection
         dropped), so this comes first. */
      if(st.read_errors) {
        if(live_id) {
          char lw[260];
          ps5tm_pkglive_why(lw, sizeof(lw));
          job_fail(0, "Das Paket kam nicht mehr vom PC an. %s", lw[0] ? lw : "Die Übertragung wurde unterbrochen.");
        } else {
          job_fail(0, "Eine Paketdatei ließ sich nicht mehr lesen (sie hat sich verändert, oder das Laufwerk wurde entfernt).");
        }
        result = J_FAILED;
        goto out;
      }

      const uint64_t nowm = mono_ms();
      if(!answered) {
        /* Nobody can ask the system. What is known is what the server has sent for the first time, and that is what the
           console has read. */
        uint64_t down = st.covered > a->total ? a->total : st.covered;
        pthread_mutex_lock(&g_lock);
        if(down > g_job.downloaded) g_job.downloaded = down;
        g_job.served = st.served;
        g_job.blind = 1;
        pthread_mutex_unlock(&g_lock);
        if(down != key_down || st.covered != key_cov) { key_down = down; key_cov = st.covered; last_move = nowm; }
        if(st.complete) { blind_done = 1; break; }
        int64_t quiet_s = (int64_t)((nowm - last_move) / 1000);
        if(quiet_s >= PI_STALL_FAIL_S) {
          char g1[24], g2[24];
          gb_text(down, g1, sizeof(g1));
          gb_text(a->total, g2, sizeof(g2));
          snprintf(why, sizeof(why), "Die Konsole holt seit %d Minuten keine Daten mehr (%s von %s). Die Installation wurde beendet.", (int)(quiet_s / 60), g1, g2);
          job_fail(0, "%s", why);
          result = J_FAILED;
          goto out;
        }
        if(quiet_s >= PI_STALL_NOTE_S) job_note("Seit %d Minuten tut sich nichts. Die Konsole ist vielleicht beschäftigt oder das Laufwerk langsam.", (int)(quiet_s / 60));
        else job_note("Der Installationshelfer ist nicht mehr da, und die Konsole lässt sich gerade nicht nach dem Fortschritt fragen. Die App liefert das Paket weiter; die Anzeige folgt dem, was die Konsole geholt hat.");
        sleep_ms_cancellable(1000);
        continue;
      }
      pthread_mutex_lock(&g_lock);
      g_job.blind = 0;
      pthread_mutex_unlock(&g_lock);

      /* "none" right after the call: the system has not entered the installation in its list yet. */
      const int none_early = rsp.result == 0 && !strcmp(rsp.status, "none") && rsp.error_code == 0 && nowm - called_at < (uint64_t)PI_NONE_GRACE_S * 1000;
      if(rsp.result == 0 && !none_early && (rsp.error_code != 0 || !strcmp(rsp.status, "error") || !strcmp(rsp.status, "none"))) {
        int code = rsp.error_code ? rsp.error_code : -1;
        last_native = code;
        snprintf(last_err_type, sizeof(last_err_type), "%.9s", rsp.error_type);
        snprintf(last_err_desc, sizeof(last_err_desc), "%.199s", rsp.error_desc);
        PS5TM_WARN("pkg_install_native_error", "Die Konsole meldet Fehler 0x%08X (Version %d, Art „%.9s“, Status „%.16s“): %.110s",
                   (unsigned)rsp.error_code, (int)rsp.error_version, rsp.error_type, rsp.status, rsp.error_desc);
        if(is_transient(code) && attempt < PI_ATTEMPTS) {
          describe_native(code, why, sizeof(why));
          PS5TM_WARN("pkg_install_retry", "Pakete: Versuch %u von %d: die Konsole meldet 0x%08X, neuer Versuch.", attempt, PI_ATTEMPTS, (unsigned)code);
          job_note("%s Neuer Versuch …", why);
          helper_close(&h);
          sleep_ms_cancellable(attempt == 1 ? 2000 : 5000);
          goto next_attempt;
        }
        if(code == -1) snprintf(why, sizeof(why), "Die Konsole hat die Installation abgebrochen (Status „%s“).", rsp.status);
        else describe_native(code, why, sizeof(why));
        if(rsp.error_desc[0] && code != -1) {
          size_t wl = strlen(why);
          snprintf(why + wl, sizeof(why) - wl, " Meldung der Konsole: %.140s", rsp.error_desc);
        }
        job_fail(code, "%s%s", why, advice_for(code));
        result = J_FAILED;
        goto out;
      }
      if(none_early) {
        sleep_ms_cancellable(1000);
        continue;
      }
      if(rsp.result != 0) {
        /* a status call that failed: the system has not registered the installation yet, or has forgotten it */
        if(!fail_since) fail_since = nowm;
        if(nowm - fail_since > 60000) {
          describe_native(rsp.result, why, sizeof(why));
          last_native = rsp.result;
          job_fail(rsp.result, "%s", why);
          result = J_FAILED;
          goto out;
        }
        sleep_ms_cancellable(1000);
        continue;
      }
      fail_since = 0;

      uint64_t down = rsp.downloaded;
      if(down > st.covered) down = st.covered;               /* what the console says it has cannot be more than was sent */
      if(down > a->total) down = a->total;
      unsigned remain = rsp.remain_time;
      if(remain == 0xFFFFFFFFu || remain > 30u * 86400u) remain = 0;       /* "not known" is not thirty days */
      pthread_mutex_lock(&g_lock);
      g_job.downloaded = down > g_job.downloaded ? down : g_job.downloaded;
      g_job.served = st.served;
      g_job.remain_s = remain;
      g_job.promote = rsp.promote_progress;
      g_job.copy_pct = rsp.local_copy_percent;
      snprintf(g_job.sys_status, sizeof(g_job.sys_status), "%s", rsp.status);
      pthread_mutex_unlock(&g_lock);

      if(down != key_down || st.covered != key_cov || rsp.promote_progress != key_promote || rsp.local_copy_percent != key_copy) {
        key_down = down;
        key_cov = st.covered;
        key_promote = rsp.promote_progress;
        key_copy = rsp.local_copy_percent;
        last_move = nowm;
      }
      if(strcmp(rsp.status, logged_status) != 0 || nowm - logged_ms >= (uint64_t)PI_LOG_STATUS_S * 1000) {
        char g1[24], g2[24];
        gb_text(down, g1, sizeof(g1));
        gb_text(a->total, g2, sizeof(g2));
        snprintf(logged_status, sizeof(logged_status), "%s", rsp.status);
        logged_ms = nowm;
        PS5TM_INFO("pkg_install_status", "Konsole meldet „%.16s“: %s von %s geholt, %u %% verarbeitet, noch %u s; Server: %llu Anfragen, %u Verbindungen.",
                   rsp.status, g1, g2, (unsigned)rsp.promote_progress, remain, (unsigned long long)st.requests, st.conns);
      }

      /* Done is the system saying so, backed by the bytes: "completed" with every byte sent (or the console's own count
         at the total); "playable" only with the console's own count at the total, because a title can be playable long
         before the end. The server's count alone does not decide: whoever else read the file would complete it. */
      const int sys_completed = !strcmp(rsp.status, "completed"), sys_playable = !strcmp(rsp.status, "playable");
      const int sys_dl_total = a->total > 0 && down >= a->total;
      const int all_sent = st.complete;
      /* The console counts a title without the package's head and signature: LEGO Batman on 05.10.2026 was 34 064 826 368
         bytes for the console in a file of 34 136 974 352, and it stayed at "playable" with its own count at its own
         total, never "completed". So "playable" counts as well when every byte went out and the console has the whole of
         what it counts itself. */
      const int sys_own_total = rsp.total > 0 && rsp.downloaded >= rsp.total;
      if((all_sent || sys_dl_total) && !(sys_completed || sys_playable)) job_phase(J_FINISH, "Die Konsole schließt die Installation ab");
      else if(!(all_sent || sys_dl_total)) job_phase(J_RUN, "Die Konsole installiert");
      if((sys_completed && (all_sent || sys_dl_total || st.coverage_lost)) ||
         (sys_playable && (sys_dl_total || (all_sent && sys_own_total)))) break;

      int64_t quiet_s = (int64_t)((nowm - last_move) / 1000);
      if(quiet_s >= PI_STALL_FAIL_S) {
        char g1[24], g2[24];
        gb_text(down, g1, sizeof(g1));
        gb_text(a->total, g2, sizeof(g2));
        if(all_sent || sys_dl_total)
          snprintf(why, sizeof(why), "Alles wurde übertragen, aber die Konsole meldet die Installation seit %d Minuten nicht als fertig (Status „%s“, %u %% verarbeitet). Die Installation wurde beendet.",
                   (int)(quiet_s / 60), rsp.status, (unsigned)rsp.promote_progress);
        else
          snprintf(why, sizeof(why), "Die Konsole holt seit %d Minuten keine Daten mehr (%s von %s). Die Installation wurde beendet.", (int)(quiet_s / 60), g1, g2);
        job_fail(0, "%s", why);
        result = J_FAILED;
        goto out;
      }
      if(quiet_s >= PI_STALL_NOTE_S) job_note("Seit %d Minuten tut sich nichts. Die Konsole ist vielleicht beschäftigt oder das Laufwerk langsam.", (int)(quiet_s / 60));
      else job_note("");
      sleep_ms_cancellable(1000);
    }

    /* The system says it is done and has all the bytes (or all the bytes went out and nobody could ask the system): the
       result is looked for in the app database. With nobody to ask, the database is all there is to go by, so it is
       watched for longer, the system needs its time after the last byte; and what it shows then is not taken for the
       system's word. */
    job_phase(J_FINISH, "Das Ergebnis wird geprüft");
    const int verify_s = blind_done ? PI_BLIND_VERIFY_S : PI_VERIFY_S;
    int present = -1;
    for(int i = 0; i < (verify_s + 2) / 3 && !cancelled(); i++) {
      present = content_present(p);
      if(present == 1) break;
      sleep_ms_cancellable(3000);
    }
    const int cut_short = cancelled() && present != 1;
    pthread_mutex_lock(&g_lock);
    g_job.downloaded = a->total;
    g_job.verified = present == 1 && !blind_done;
    g_job.blind = blind_done;
    pthread_mutex_unlock(&g_lock);
    if(blind_done) {
      job_note("Die App hat das ganze Paket an die Konsole geliefert. Die Konsole ließ sich dabei nicht nach dem Fortschritt fragen%s. Bitte an der Konsole nachsehen, ob die Installation fertig ist.%s",
               present == 1 ? ", und der Titel steht in ihrer Liste" : (present == 0 ? ", und der Titel steht noch nicht in ihrer Liste" : ""), cut_short ? " Die Prüfung wurde abgebrochen." : "");
    } else if(present == 1) {
      job_note("");
    } else if(present == 0) {
      if(!strcmp(p->kind, "update"))
        job_note("Die Konsole meldet die Installation als fertig, aber die neue Version (%.20s) steht noch nicht in ihrer Liste. Das kann einen Moment dauern; sonst bitte an der Konsole nachsehen.%s", p->version, cut_short ? " Die Prüfung wurde abgebrochen." : "");
      else
        job_note("Die Konsole meldet die Installation als fertig, aber der Titel steht noch nicht in ihrer Liste. Das kann einen Moment dauern; sonst bitte an der Konsole nachsehen.%s", cut_short ? " Die Prüfung wurde abgebrochen." : "");
    } else {
      job_note("Die Konsole meldet die Installation als fertig. Ob das Ergebnis in ihrer Liste steht, ließ sich nicht prüfen; bitte an der Konsole nachsehen.%s", cut_short ? " Die Prüfung wurde abgebrochen." : "");
    }
    result = J_DONE;
    goto out;
next_attempt:;
  }

out:
  /* The helper and the server go first; only then the job says how it ended, so that "failed", "done" and "cancelled"
     always mean that the port is free and the next installation can start. The numbers of the server are taken before
     it goes. */
  ps5tm_pkgstream_stats(&fin);
  if(h.fd >= 0 || h.pid > 0 || h.loader_fd >= 0) { drain_loader(&h); helper_close(&h); }
  if(stream_up) ps5tm_pkgstream_stop();
  if(live_id) ps5tm_pkglive_end(live_id);                  /* the browser's upload has nothing left to wait for */
  free(icon);
  ps5tm_powerguard_release();                              /* nothing is left to keep the console awake for */
  const unsigned took_s = (unsigned)((mono_ms() - began_ms) / 1000);
  if(result == J_FAILED) {
    pthread_mutex_lock(&g_lock);
    int said = g_job.error[0] != 0;
    pthread_mutex_unlock(&g_lock);
    if(!said) job_fail(0, "Die Installation ist nicht gelungen.");
    /* The line a person debugging this on a console reads first, and the one under it with the numbers. */
    char reason[512], phase[96], sstat[17];
    int code;
    unsigned att;
    uint64_t dl, tot;
    pthread_mutex_lock(&g_lock);
    snprintf(reason, sizeof(reason), "%s", g_job.error);
    snprintf(phase, sizeof(phase), "%s", g_job.phase);
    snprintf(sstat, sizeof(sstat), "%s", g_job.sys_status);
    code = g_job.error_code;
    att = g_job.attempt;
    dl = g_job.downloaded;
    tot = g_job.total;
    pthread_mutex_unlock(&g_lock);
    PS5TM_WARN("pkg_install_failed", "Pakete: Installieren von „%.40s“ fehlgeschlagen: %.120s", p->name, reason);
    PS5TM_WARN("pkg_install_detail", "Phase „%.40s“, Versuch %u von %d, Code 0x%08X, Status „%.16s“, %llu von %llu MB, Server: %llu Anfragen, %llu MB geliefert, %llu Lesefehler, nach %u s.",
               phase, att, PI_ATTEMPTS, (unsigned)(code ? code : last_native), sstat, (unsigned long long)(dl >> 20), (unsigned long long)(tot >> 20),
               (unsigned long long)fin.requests, (unsigned long long)(fin.served >> 20), (unsigned long long)fin.read_errors, took_s);
    if(last_err_desc[0]) PS5TM_WARN("pkg_install_native_error", "Letzte Meldung der Konsole: Art „%.9s“, %.150s", last_err_type, last_err_desc);
    if(h.out_n) PS5TM_WARN("pkg_loader_output", "Lader: %.170s", h.out);
    dump_helper_log();
    job_end(J_FAILED);
  }
  if(result == J_DONE) {
    pthread_mutex_lock(&g_lock);
    int verified = g_job.verified;
    snprintf(g_job.phase, sizeof(g_job.phase), "Fertig");      /* not "Das Ergebnis wird geprüft" any more */
    pthread_mutex_unlock(&g_lock);
    job_end(J_DONE);
    if(verified) PS5TM_INFO("pkg_install_done", "Pakete: „%.60s“ ist installiert (nach %u s, %llu Anfragen).", p->name, took_s, (unsigned long long)fin.requests);
    else PS5TM_WARN("pkg_install_unverified", "Pakete: „%.60s“: die Konsole meldet die Installation als fertig, in ihrer Liste ist das Ergebnis nicht bestätigt (nach %u s).", p->name, took_s);
    ps5tm_library_forget();
  } else if(result == J_CANCELLED) {
    job_note("Abgebrochen. Was die Konsole schon angelegt hat, bleibt unter Umständen liegen; die App räumt das nicht selbst auf.");
    job_end(J_CANCELLED);
    PS5TM_INFO("pkg_install_cancelled", "Pakete: Installation von „%.60s“ abgebrochen (nach %u s).", p->name, took_s);
    ps5tm_library_forget();
  }
  free(a);
  return NULL;
}

/* ------------------------------------------------------------------ the start */

/* The status of an HTTP answer: 200 started; 404 no such package; 409 cannot be done now. */
int
ps5tm_pkginst_start(const char *id, char *err, size_t err_len) {
  err[0] = 0;
  args_t *a = calloc(1, sizeof(*a));
  eval_t *e = calloc(1, sizeof(*e));
  if(!a || !e) { free(a); free(e); snprintf(err, err_len, "Zu wenig Speicher."); return 409; }
  evaluate(id, e);
  if(e->block[0]) {
    snprintf(err, err_len, "%s", e->block);
    int http = e->http == 200 ? 409 : e->http;
    free(a);
    free(e);
    return http;
  }
  a->pkg = e->pkg;
  memcpy(a->slices, e->slices, sizeof(e->slices[0]) * e->nslices);
  a->nslices = e->nslices;
  a->total = e->total;
  free(e);

  pthread_mutex_lock(&g_lock);
  if(active_locked()) {
    pthread_mutex_unlock(&g_lock);
    free(a);
    snprintf(err, err_len, "Es läuft schon eine Installation.");
    return 409;
  }
  memset(&g_job, 0, sizeof(g_job));
  g_job.state = J_PREPARE;
  snprintf(g_job.phase, sizeof(g_job.phase), "Vorbereiten");
  snprintf(g_job.name, sizeof(g_job.name), "%s", a->pkg.name);
  snprintf(g_job.file, sizeof(g_job.file), "%s", a->pkg.parts ? (a->pkg.orig_file[0] ? a->pkg.orig_file : a->pkg.file) : a->pkg.file);
  snprintf(g_job.title_id, sizeof(g_job.title_id), "%s", a->pkg.title_id);
  snprintf(g_job.content_id, sizeof(g_job.content_id), "%s", a->pkg.content_id);
  snprintf(g_job.version, sizeof(g_job.version), "%s", a->pkg.version);
  snprintf(g_job.kind, sizeof(g_job.kind), "%s", a->pkg.kind);
  g_job.plat = a->pkg.plat;
  g_job.total = a->total;
  g_job.started_ms = mono_ms();
  __atomic_store_n(&g_cancel, 0, __ATOMIC_RELEASE);
  pthread_mutex_unlock(&g_lock);

  /* The question in evaluate() and the claim above are not one step: a copy, a conversion or a split may have started in
     between. It asks for this installation before it claims its own (and finds this one now), so of two that start at
     the very same moment at least one gives way. No lock of this file is held while asking the others. */
  if(other_job_active()) {
    pthread_mutex_lock(&g_lock);
    memset(&g_job, 0, sizeof(g_job));                    /* idle again */
    pthread_mutex_unlock(&g_lock);
    free(a);
    snprintf(err, err_len, "Es läuft gerade ein Kopieren, Konvertieren, Verschieben, Sichern oder Teilen. Das läuft nicht gleichzeitig mit einer Installation.");
    return 409;
  }

  pthread_t th;
  pthread_attr_t at;
  pthread_attr_init(&at);
  pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
  pthread_attr_setstacksize(&at, 256 * 1024);
  int rc = pthread_create(&th, &at, install_main, a);
  pthread_attr_destroy(&at);
  if(rc != 0) {
    pthread_mutex_lock(&g_lock);
    g_job.state = J_IDLE;
    pthread_mutex_unlock(&g_lock);
    free(a);
    snprintf(err, err_len, "Der Vorgang ließ sich nicht starten.");
    return 409;
  }
  return 200;
}

cJSON *
ps5tm_pkginst_job_json(void) {
  cJSON *o = cJSON_CreateObject();
  if(!o) return NULL;
  pthread_mutex_lock(&g_lock);
  int act = active_locked();
  uint64_t now = mono_ms();
  cJSON_AddBoolToObject(o, "ok", 1);
  cJSON_AddStringToObject(o, "state", k_state[g_job.state]);
  cJSON_AddBoolToObject(o, "active", act);
  cJSON_AddBoolToObject(o, "can_cancel", act && !cancelled());
  cJSON_AddBoolToObject(o, "cancelling", act && cancelled());                  /* the stop has been asked for and is under way */
  cJSON_AddNumberToObject(o, "helper_restarts", g_job.restarts);               /* helpers started after the first one */
  cJSON_AddBoolToObject(o, "blind", g_job.blind);                             /* nobody can ask the system: the numbers are the server's */
  cJSON_AddBoolToObject(o, "started", g_job.started);                         /* the system has the package: what it has put on the console may stay */
  cJSON_AddBoolToObject(o, "verified", g_job.state == J_DONE && g_job.verified);   /* done, and the result was found in the console's list */
  cJSON_AddStringToObject(o, "phase", g_job.phase);
  cJSON_AddStringToObject(o, "name", g_job.name);
  cJSON_AddStringToObject(o, "file", g_job.file);
  cJSON_AddStringToObject(o, "title_id", g_job.title_id);
  cJSON_AddStringToObject(o, "content_id", g_job.content_id);
  cJSON_AddStringToObject(o, "version", g_job.version);
  cJSON_AddStringToObject(o, "kind", g_job.kind);
  cJSON_AddNumberToObject(o, "plat", g_job.plat);
  cJSON_AddNumberToObject(o, "bytes_total", (double)g_job.total);
  cJSON_AddNumberToObject(o, "bytes_done", (double)g_job.downloaded);
  cJSON_AddNumberToObject(o, "bytes_sent", (double)g_job.served);
  cJSON_AddNumberToObject(o, "percent", g_job.total ? (g_job.state == J_DONE ? 100 : (double)(g_job.downloaded * 100 / g_job.total)) : 0);
  cJSON_AddNumberToObject(o, "remain_s", g_job.remain_s);
  cJSON_AddNumberToObject(o, "promote_percent", g_job.promote);
  cJSON_AddStringToObject(o, "system_status", g_job.sys_status);
  cJSON_AddNumberToObject(o, "attempt", g_job.attempt);
  cJSON_AddStringToObject(o, "error", g_job.error);
  char code[16] = "";
  if(g_job.error_code) snprintf(code, sizeof(code), "0x%08X", (unsigned)g_job.error_code);
  cJSON_AddStringToObject(o, "error_code", code);
  cJSON_AddStringToObject(o, "note", g_job.note);
  uint64_t until = act ? now : g_job.finished_ms;
  cJSON_AddNumberToObject(o, "elapsed_s", g_job.started_ms && until > g_job.started_ms ? (double)((until - g_job.started_ms) / 1000) : 0);
  cJSON_AddNumberToObject(o, "finished_ago_s", g_job.finished_ms ? (now > g_job.finished_ms ? (double)((now - g_job.finished_ms) / 1000) : 0) : -1);
  pthread_mutex_unlock(&g_lock);
  return o;
}
