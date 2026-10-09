/* Installing several packages one after the other (09.10.2026).
 *
 * The person puts packages the search found into a list and starts it with a click; a thread then hands them to the
 * installation job (pkginstall.c) one at a time. Nothing is decided in advance: each package is asked the very questions
 * an installation of its own is asked, when its turn comes. That is what makes the order work: a game that has been
 * installed by the entry before it is "installed" for the update behind it, and an update the console already has
 * (or one that is older than what it has) is skipped, with the reason, not installed again.
 *
 *   - a package the installation refuses for good (installed already, no game for the update, files changed, ...)
 *     is skipped, the reason stays in its line, and the queue goes on;
 *   - a refusal that is only "not now" (a game runs, a copy is under way) halts the queue with that reason, and the entry
 *     keeps its place; "continue" asks again;
 *   - a package that fails halts the queue, so that a cable that has come loose or a drive that is full does not take
 *     every other package down with it one after the other; "continue" goes on with the next, "retry" puts the failed
 *     one back;
 *   - "stop" of the running package stops that installation (as it does outside the queue) and halts the queue.
 *
 * Like the installation, the queue never deletes, uninstalls or overwrites anything and never starts by itself. It lives
 * in memory: an app that restarts starts with an empty list. The packages come from the last search; a package that
 * has left it by the time its turn comes is skipped with the installation's own words. */

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "ps5tm.h"
#include "third_party/cJSON.h"

#define PQ_MAX 64

enum { PQ_WAIT, PQ_RUN, PQ_DONE, PQ_FAIL, PQ_SKIP, PQ_CANCEL };
static const char *const k_state[] = { "waiting", "running", "done", "failed", "skipped", "cancelled" };

typedef struct {
  char     id[17];
  char     name[160], title_id[24], version[24], kind[8];
  int      plat, has_icon;
  int64_t  mtime;
  uint64_t size;
  int      state;
  char     msg[512];
  int      verified;
} pq_item_t;

#ifndef PQ_POLL_MS
#define PQ_POLL_MS 300                   /* how often the runner looks whether the job has ended */
#endif

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static struct {
  pq_item_t it[PQ_MAX];
  unsigned  n;
  int       running;                     /* the runner thread is alive (also between two packages) */
  int       pause_req;                   /* stop after the package that runs */
  int       cancel_req;                  /* the running package is to be stopped */
  int       halted;                      /* the queue has stopped, and why: */
  char      halt_kind[12];               /* "failed", "stopped" or "wait" */
  char      halt_reason[400];
  unsigned  job_seq;                     /* of the installation job the queue started last */
  unsigned  done_n, skip_n, fail_n;      /* of the run that is under way, for the log */
} g_q;

static int
find_locked(const char *id) {
  for(unsigned i = 0; i < g_q.n; i++)
    if(!strcmp(g_q.it[i].id, id)) return (int)i;
  return -1;
}

static void
remove_at_locked(unsigned i) {
  if(i >= g_q.n) return;
  memmove(&g_q.it[i], &g_q.it[i + 1], sizeof(g_q.it[0]) * (g_q.n - i - 1));
  g_q.n--;
}

static int
valid_id(const char *id) {
  if(!id || strlen(id) != 16) return 0;
  for(const char *c = id; *c; c++)
    if(!((*c >= '0' && *c <= '9') || (*c >= 'a' && *c <= 'f'))) return 0;
  return 1;
}

static int
waiting_locked(void) {
  int w = 0;
  for(unsigned i = 0; i < g_q.n; i++) w += g_q.it[i].state == PQ_WAIT;
  return w;
}

/* ------------------------------------------------------------------ the list */

int
ps5tm_pkgqueue_add(const char *const *ids, unsigned n, unsigned *added, unsigned *ignored, char *err, size_t err_len) {
  unsigned ad = 0, ig = 0, unknown = 0;
  err[0] = 0;
  if(n == 0) {
    snprintf(err, err_len, "Es wurde kein Paket genannt.");
    if(added) *added = 0;
    if(ignored) *ignored = 0;
    return 400;
  }
  int full = 0;
  ps5tm_pkg_t *p = calloc(1, sizeof(*p));
  if(!p) { snprintf(err, err_len, "Zu wenig Speicher."); return 503; }
  for(unsigned k = 0; k < n; k++) {
    const char *id = ids[k];
    if(!valid_id(id) || ps5tm_pkgscan_find(id, p) != 0) { unknown++; continue; }
    pthread_mutex_lock(&g_lock);
    int at = find_locked(id);
    if(at >= 0 && (g_q.it[at].state == PQ_WAIT || g_q.it[at].state == PQ_RUN)) {
      ig++;                                                       /* it is in the list and has not been dealt with */
      pthread_mutex_unlock(&g_lock);
      continue;
    }
    if(at >= 0) remove_at_locked((unsigned)at);                   /* a finished entry of an earlier round: put at the end again */
    if(g_q.n >= PQ_MAX) {
      full = 1;
      ig++;
      pthread_mutex_unlock(&g_lock);
      continue;
    }
    pq_item_t *it = &g_q.it[g_q.n++];
    memset(it, 0, sizeof(*it));
    snprintf(it->id, sizeof(it->id), "%s", id);
    snprintf(it->name, sizeof(it->name), "%s", p->name);
    snprintf(it->title_id, sizeof(it->title_id), "%s", p->title_id);
    snprintf(it->version, sizeof(it->version), "%s", p->version);
    snprintf(it->kind, sizeof(it->kind), "%s", p->kind);
    it->plat = p->plat;
    it->has_icon = p->icon_size > 0;
    it->mtime = p->mtime;
    it->size = p->parts ? p->total : p->size;
    it->state = PQ_WAIT;
    ad++;
    pthread_mutex_unlock(&g_lock);
  }
  free(p);
  if(added) *added = ad;
  if(ignored) *ignored = ig;
  if(ad == 0 && ig == 0 && unknown) {
    snprintf(err, err_len, "Dieses Paket kennt die letzte Suche nicht (mehr). Bitte noch einmal suchen.");
    return 404;
  }
  if(full) snprintf(err, err_len, "Die Warteschlange ist voll (höchstens %d Pakete).", PQ_MAX);
  else if(unknown) snprintf(err, err_len, "%u Paket(e) kennt die letzte Suche nicht (mehr).", unknown);
  return 200;
}

int
ps5tm_pkgqueue_remove(const char *id) {
  if(!valid_id(id)) return 404;
  pthread_mutex_lock(&g_lock);
  int at = find_locked(id);
  int rc = 404;
  if(at >= 0) {
    if(g_q.it[at].state == PQ_RUN) rc = 409;
    else { remove_at_locked((unsigned)at); rc = 200; }
  }
  pthread_mutex_unlock(&g_lock);
  return rc;
}

/* Failed, stopped and skipped entries wait again (one by id, or all). The queue stays halted until "continue". */
int
ps5tm_pkgqueue_retry(const char *id) {
  int n = 0;
  pthread_mutex_lock(&g_lock);
  for(unsigned i = 0; i < g_q.n; i++) {
    pq_item_t *it = &g_q.it[i];
    if(id && strcmp(it->id, id)) continue;
    if(it->state != PQ_FAIL && it->state != PQ_CANCEL && it->state != PQ_SKIP) continue;
    it->state = PQ_WAIT;
    it->msg[0] = 0;
    it->verified = 0;
    n++;
  }
  pthread_mutex_unlock(&g_lock);
  return n;
}

void
ps5tm_pkgqueue_clear(int all) {
  pthread_mutex_lock(&g_lock);
  unsigned i = 0;
  while(i < g_q.n) {
    int s = g_q.it[i].state;
    int drop = all ? s != PQ_RUN : (s == PQ_DONE || s == PQ_SKIP || s == PQ_CANCEL);
    if(drop) remove_at_locked(i); else i++;
  }
  if(!g_q.running && waiting_locked() == 0) {                       /* nothing left to go on with: the halt is over */
    g_q.halted = 0;
    g_q.halt_kind[0] = 0;
    g_q.halt_reason[0] = 0;
  }
  pthread_mutex_unlock(&g_lock);
}

/* ------------------------------------------------------------------ the runner */

/* Entries are found by their id, never by their place: the person may remove other entries while one runs. */
static void
finish_item(const char *id, int state, const char *msg, int verified) {
  pthread_mutex_lock(&g_lock);
  int at = find_locked(id);
  if(at >= 0) {
    pq_item_t *it = &g_q.it[at];
    it->state = state;
    snprintf(it->msg, sizeof(it->msg), "%s", msg ? msg : "");
    it->verified = verified;
  }
  pthread_mutex_unlock(&g_lock);
}

static void
halt(const char *kind, const char *reason) {
  pthread_mutex_lock(&g_lock);
  g_q.halted = 1;
  snprintf(g_q.halt_kind, sizeof(g_q.halt_kind), "%s", kind);
  snprintf(g_q.halt_reason, sizeof(g_q.halt_reason), "%s", reason ? reason : "");
  pthread_mutex_unlock(&g_lock);
}

static void *
runner_main(void *arg) {
  (void)arg;
  for(;;) {
    pthread_mutex_lock(&g_lock);
    int idx = -1;
    if(!g_q.halted && !g_q.pause_req)
      for(unsigned i = 0; i < g_q.n; i++)
        if(g_q.it[i].state == PQ_WAIT) { idx = (int)i; break; }
    if(idx < 0) {
      unsigned d = g_q.done_n, s = g_q.skip_n, f = g_q.fail_n;
      int paused = g_q.pause_req || g_q.halted;
      g_q.running = 0;
      g_q.pause_req = 0;
      g_q.cancel_req = 0;
      pthread_mutex_unlock(&g_lock);
      /* two whole sentences, not one with a word put in: the log is translated sentence by sentence */
      if(paused)
        PS5TM_INFO("pkg_queue_end", "Pakete: Die Warteschlange ist angehalten: %u installiert, %u übersprungen, %u fehlgeschlagen.", d, s, f);
      else
        PS5TM_INFO("pkg_queue_end", "Pakete: Die Warteschlange ist zu Ende: %u installiert, %u übersprungen, %u fehlgeschlagen.", d, s, f);
      return NULL;
    }
    pq_item_t *it = &g_q.it[idx];
    it->state = PQ_RUN;
    it->msg[0] = 0;
    char id[17], name[160];
    snprintf(id, sizeof(id), "%s", it->id);
    snprintf(name, sizeof(name), "%s", it->name);
    pthread_mutex_unlock(&g_lock);

    char err[600];
    int transient = 0;
    unsigned seq = 0;
    int st = ps5tm_pkginst_start_q(id, err, sizeof(err), &transient, &seq);
    if(st != 200) {
      if(transient) {
        /* "not now": the entry keeps its place, the queue halts with the reason */
        finish_item(id, PQ_WAIT, "", 0);
        halt("wait", err);
        PS5TM_WARN("pkg_queue_wait", "Pakete: Die Warteschlange wartet bei „%.60s“: %.200s", name, err);
      } else {
        finish_item(id, PQ_SKIP, err, 0);
        pthread_mutex_lock(&g_lock);
        g_q.skip_n++;
        pthread_mutex_unlock(&g_lock);
        PS5TM_INFO("pkg_queue_skip", "Pakete: „%.60s“ wird übersprungen: %.200s", name, err);
      }
      continue;
    }
    pthread_mutex_lock(&g_lock);
    g_q.job_seq = seq;
    pthread_mutex_unlock(&g_lock);

    /* wait for the installation; it has its own limits for a stall, so there is none here */
    for(;;) {
      pthread_mutex_lock(&g_lock);
      int cr = g_q.cancel_req;
      pthread_mutex_unlock(&g_lock);
      if(cr) ps5tm_pkginst_cancel();                 /* idempotent; also covers a stop that came before the job was active */
      if(!ps5tm_pkginst_busy()) break;
      usleep(PQ_POLL_MS * 1000);
    }
    ps5tm_pkginst_result_t *r = calloc(1, sizeof(*r));
    if(!r) {
      finish_item(id, PQ_FAIL, "Zu wenig Speicher, um das Ergebnis zu lesen.", 0);
      halt("failed", "");
      continue;
    }
    ps5tm_pkginst_result(r);
    if(r->seq != seq || r->state == 0) {
      finish_item(id, PQ_FAIL, "Das Ergebnis der Installation ließ sich nicht feststellen.", 0);
      halt("failed", "");
      PS5TM_WARN("pkg_queue_lost", "Pakete: Das Ergebnis der Installation von „%.60s“ ließ sich nicht feststellen.", name);
    } else if(r->state == 1) {
      const char *m = "";
      if(!r->verified)
        m = r->blind
          ? "Die App hat das Paket vollständig geliefert, die Konsole ließ sich aber nicht nach dem Ergebnis fragen. Bitte in der Spielebibliothek nachsehen."
          : "Die Konsole meldet das Paket als fertig. Dass es in ihrer Liste steht, ließ sich nicht bestätigen. Bitte in der Spielebibliothek nachsehen.";
      finish_item(id, PQ_DONE, m, r->verified);
      pthread_mutex_lock(&g_lock);
      g_q.done_n++;
      pthread_mutex_unlock(&g_lock);
      if(r->verified)
        PS5TM_INFO("pkg_queue_done", "Pakete: „%.60s“ ist installiert.", name);
      else
        PS5TM_INFO("pkg_queue_done", "Pakete: „%.60s“ ist installiert (nicht bestätigt).", name);
    } else if(r->state == 2) {
      finish_item(id, PQ_FAIL, r->error[0] ? r->error : "Die Installation ist fehlgeschlagen.", 0);
      halt("failed", "");
      pthread_mutex_lock(&g_lock);
      g_q.fail_n++;
      pthread_mutex_unlock(&g_lock);
      PS5TM_WARN("pkg_queue_failed", "Pakete: „%.60s“ ließ sich nicht installieren: %.200s", name, r->error);
    } else {
      finish_item(id, PQ_CANCEL, "Abgebrochen.", 0);
      halt("stopped", "");
      PS5TM_INFO("pkg_queue_stopped", "Pakete: Die Installation von „%.60s“ wurde abgebrochen, die Warteschlange hält an.", name);
    }
    free(r);
    pthread_mutex_lock(&g_lock);
    g_q.cancel_req = 0;
    pthread_mutex_unlock(&g_lock);
  }
}

int
ps5tm_pkgqueue_start(char *err, size_t err_len) {
  err[0] = 0;
  pthread_mutex_lock(&g_lock);
  if(g_q.running) {
    pthread_mutex_unlock(&g_lock);
    snprintf(err, err_len, "Die Warteschlange läuft schon.");
    return 409;
  }
  if(waiting_locked() == 0) {
    pthread_mutex_unlock(&g_lock);
    snprintf(err, err_len, "Es wartet kein Paket in der Warteschlange.");
    return 409;
  }
  if(ps5tm_pkginst_busy()) {
    pthread_mutex_unlock(&g_lock);
    snprintf(err, err_len, "Es läuft gerade eine Installation. Die Warteschlange fängt an, wenn sie fertig ist: bitte dann noch einmal starten.");
    return 409;
  }
  g_q.running = 1;
  g_q.pause_req = 0;
  g_q.cancel_req = 0;
  g_q.halted = 0;
  g_q.halt_kind[0] = 0;
  g_q.halt_reason[0] = 0;
  g_q.done_n = g_q.skip_n = g_q.fail_n = 0;
  unsigned w = (unsigned)waiting_locked();
  pthread_mutex_unlock(&g_lock);

  pthread_t th;
  pthread_attr_t at;
  pthread_attr_init(&at);
  pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
  pthread_attr_setstacksize(&at, 128 * 1024);
  int rc = pthread_create(&th, &at, runner_main, NULL);
  pthread_attr_destroy(&at);
  if(rc != 0) {
    pthread_mutex_lock(&g_lock);
    g_q.running = 0;
    pthread_mutex_unlock(&g_lock);
    snprintf(err, err_len, "Die Warteschlange ließ sich nicht starten.");
    return 409;
  }
  PS5TM_INFO("pkg_queue_start", "Pakete: Die Warteschlange beginnt mit %u Paket(en).", w);
  return 200;
}

void
ps5tm_pkgqueue_pause(void) {
  pthread_mutex_lock(&g_lock);
  if(g_q.running) g_q.pause_req = 1;
  pthread_mutex_unlock(&g_lock);
}

int
ps5tm_pkgqueue_cancel(void) {
  int ran = 0;
  pthread_mutex_lock(&g_lock);
  for(unsigned i = 0; i < g_q.n; i++) if(g_q.it[i].state == PQ_RUN) ran = 1;
  if(ran) {
    g_q.cancel_req = 1;
    g_q.halted = 1;
    snprintf(g_q.halt_kind, sizeof(g_q.halt_kind), "stopped");
    g_q.halt_reason[0] = 0;
  }
  pthread_mutex_unlock(&g_lock);
  if(ran) ps5tm_pkginst_cancel();
  return ran;
}

/* ------------------------------------------------------------------ the state */

cJSON *
ps5tm_pkgqueue_json(void) {
  cJSON *o = cJSON_CreateObject();
  if(!o) return NULL;
  pthread_mutex_lock(&g_lock);
  unsigned cnt[6] = { 0 };
  for(unsigned i = 0; i < g_q.n; i++) cnt[g_q.it[i].state]++;
  cJSON_AddBoolToObject(o, "ok", 1);
  cJSON_AddBoolToObject(o, "running", g_q.running);
  cJSON_AddBoolToObject(o, "pause_requested", g_q.running && g_q.pause_req);
  cJSON_AddBoolToObject(o, "cancelling", g_q.cancel_req);
  cJSON_AddBoolToObject(o, "halted", g_q.halted);
  cJSON_AddStringToObject(o, "halt_kind", g_q.halted ? g_q.halt_kind : "");
  cJSON_AddStringToObject(o, "halt_reason", g_q.halted ? g_q.halt_reason : "");
  cJSON_AddNumberToObject(o, "job_seq", g_q.job_seq);
  cJSON_AddNumberToObject(o, "max", PQ_MAX);
  cJSON *c = cJSON_AddObjectToObject(o, "counts");
  cJSON_AddNumberToObject(c, "total", g_q.n);
  for(int s = 0; s < 6; s++) cJSON_AddNumberToObject(c, k_state[s], cnt[s]);
  cJSON *arr = cJSON_AddArrayToObject(o, "items");
  for(unsigned i = 0; i < g_q.n; i++) {
    const pq_item_t *it = &g_q.it[i];
    cJSON *e = cJSON_CreateObject();
    if(!e) continue;
    cJSON_AddStringToObject(e, "id", it->id);
    cJSON_AddStringToObject(e, "name", it->name);
    cJSON_AddStringToObject(e, "title_id", it->title_id);
    cJSON_AddStringToObject(e, "version", it->version);
    cJSON_AddStringToObject(e, "kind", it->kind);
    cJSON_AddNumberToObject(e, "plat", it->plat);
    cJSON_AddNumberToObject(e, "size", (double)it->size);
    cJSON_AddNumberToObject(e, "mtime", (double)it->mtime);
    cJSON_AddBoolToObject(e, "has_icon", it->has_icon);
    cJSON_AddStringToObject(e, "state", k_state[it->state]);
    cJSON_AddStringToObject(e, "message", it->msg);
    cJSON_AddBoolToObject(e, "verified", it->verified);
    cJSON_AddItemToArray(arr, e);
  }
  pthread_mutex_unlock(&g_lock);
  return o;
}
