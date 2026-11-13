/* PS5 Cooling & System Center - Pro - the queue of conversions (13.11.2026).
 *
 * Several games one after the other: each entry holds what the window of converting asked for (game, format, drive, mode, the way of
 * checking, the name template); a thread of its own starts them through ps5tm_gameconvert_start() one at a time and waits for each to end.
 * What is new beside the single start is only the waiting: an entry whose start is refused because another job runs (a copy, a move, the
 * file manager) waits and tries again; every other refusal marks that entry as failed and the queue goes on with the next.
 *
 * Cancelling the running conversion (its own button) marks that entry as cancelled and the queue goes on; "stop" only keeps the queue
 * from starting more, the running one ends as it would. The queue is in memory: it is gone when the app ends (a conversion takes hours;
 * what it ended with stays in the history, jobhist.c). The list holds 24 entries.
 *
 * Nothing here deletes anything: removing an entry takes it off the list, and a result stays where it was written. */

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "ps5tm.h"
#include "third_party/cJSON.h"

#define CQ_MAX 24

enum { CQ_WAIT, CQ_RUN, CQ_DONE, CQ_FAIL, CQ_CANCEL };
static const char *const k_state[] = { "waiting", "running", "done", "failed", "cancelled" };

typedef struct {
  int      qid;
  int      state;
  char     id[12], name[128], op[8], mount[40], mode[12], name_tpl[200], target[400];
  int      ampr, quick;
  char     msg[300];
  uint64_t out_bytes, started_ms, finished_ms;
} cq_item_t;

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static cq_item_t       g_it[CQ_MAX];
static int             g_n;
static int             g_next_qid = 1;
static int             g_run;              /* the queue starts entries */
static int             g_thread;           /* the runner thread exists */
static int             g_running_qid;      /* the entry that is started or running, else 0 */

static void
set_msg(cq_item_t *it, const char *m) {
  snprintf(it->msg, sizeof(it->msg), "%s", m ? m : "");
}

static cq_item_t *
find_locked(int qid) {
  for(int i = 0; i < g_n; i++) if(g_it[i].qid == qid) return &g_it[i];
  return NULL;
}

static void
remove_at_locked(int i) {
  memmove(&g_it[i], &g_it[i + 1], (size_t)(g_n - i - 1) * sizeof(g_it[0]));
  g_n--;
}

/* Waits until the job this entry started has ended and notes how. Returns 0. */
static void
wait_for_job(int qid) {
  for(;;) {
    sleep(1);
    cJSON *st = ps5tm_gameconvert_status();
    if(!st) continue;
    const cJSON *act = cJSON_GetObjectItem(st, "active");
    const cJSON *state = cJSON_GetObjectItem(st, "state");
    const cJSON *q = cJSON_GetObjectItem(st, "queue_id");
    int active = cJSON_IsTrue(act);
    int mine = cJSON_IsNumber(q) && (int)q->valuedouble == qid;
    if(!active) {
      pthread_mutex_lock(&g_lock);
      cq_item_t *it = find_locked(qid);
      if(it) {
        const char *s = cJSON_IsString(state) ? state->valuestring : "";
        const cJSON *err = cJSON_GetObjectItem(st, "error");
        const cJSON *ob = cJSON_GetObjectItem(st, "output_bytes");
        if(mine && !strcmp(s, "done")) { it->state = CQ_DONE; if(cJSON_IsNumber(ob)) it->out_bytes = (uint64_t)ob->valuedouble; }
        else if(mine && !strcmp(s, "cancelled")) { it->state = CQ_CANCEL; set_msg(it, "Abgebrochen."); }
        else if(mine) { it->state = CQ_FAIL; set_msg(it, cJSON_IsString(err) ? err->valuestring : "Das Konvertieren ist gescheitert."); }
        else { it->state = CQ_FAIL; set_msg(it, "Der Auftrag ist nicht mehr zu sehen."); }
        it->finished_ms = ps5tm_mono_ms();
      }
      pthread_mutex_unlock(&g_lock);
      cJSON_Delete(st);
      return;
    }
    cJSON_Delete(st);
  }
}

static void *
runner(void *arg) {
  (void)arg;
  int done_n = 0, tried = 0;
  for(;;) {
    cq_item_t it;
    int idx = -1;
    pthread_mutex_lock(&g_lock);
    if(g_run) for(int i = 0; i < g_n; i++) if(g_it[i].state == CQ_WAIT) { idx = i; break; }
    if(idx < 0) {                                     /* nothing to start: the thread ends (decided under the lock the adders look at) */
      g_thread = 0;
      g_running_qid = 0;
      pthread_mutex_unlock(&g_lock);
      break;
    }
    g_it[idx].state = CQ_RUN;
    g_it[idx].started_ms = ps5tm_mono_ms();
    g_it[idx].msg[0] = 0;
    g_running_qid = g_it[idx].qid;
    it = g_it[idx];
    pthread_mutex_unlock(&g_lock);

    tried++;
    char err[640] = "";
    int rc, requeued = 0;
    for(;;) {
      rc = ps5tm_gameconvert_start(it.id, it.op, it.ampr, it.mount, it.mode, it.quick, it.name_tpl, it.qid, err, sizeof(err));
      if(rc == 200) break;
      /* Refused because another job runs (the sentences of gameconvert.c: other_job_active and the claim of the one place): wait. The words
         are read from the refusal itself, so that nothing here asks ShadowMountPlus over its port while the entry waits. */
      int busy = rc == 409 && (strstr(err, "laufen nicht gleichzeitig") || strstr(err, "Es läuft schon eine Konvertierung"));
      if(!busy) break;
      /* another job runs: wait, unless the queue was stopped or the entry removed meanwhile */
      pthread_mutex_lock(&g_lock);
      cq_item_t *me = find_locked(it.qid);
      int keep = g_run && me;
      if(me && !keep) me->state = CQ_WAIT;            /* stopped while waiting: back to waiting */
      if(me && !keep) requeued = 1;
      pthread_mutex_unlock(&g_lock);
      if(!keep) break;
      sleep(10);
    }
    if(requeued) continue;
    if(rc != 200) {
      pthread_mutex_lock(&g_lock);
      cq_item_t *me = find_locked(it.qid);
      if(me) { me->state = CQ_FAIL; set_msg(me, err[0] ? err : "Der Start ist gescheitert."); me->finished_ms = ps5tm_mono_ms(); }
      pthread_mutex_unlock(&g_lock);
      PS5TM_WARN("convq_item_failed", "Konvertier-Warteschlange: %s (%s) nicht gestartet: %s", it.name, it.id, err);
      continue;
    }
    PS5TM_INFO("convq_item_started", "Konvertier-Warteschlange: %s (%s) gestartet.", it.name, it.id);
    wait_for_job(it.qid);
    pthread_mutex_lock(&g_lock);
    cq_item_t *me = find_locked(it.qid);
    if(me && me->state == CQ_DONE) done_n++;
    pthread_mutex_unlock(&g_lock);
  }
  if(tried) {
    ps5tm_notify("Konvertier-Warteschlange zu Ende: %d von %d fertig.", done_n, tried);
    PS5TM_INFO("convq_end", "Konvertier-Warteschlange zu Ende: %d von %d fertig.", done_n, tried);
  }
  return NULL;
}

/* Starts the runner when the queue is to run and it is not already there. The caller holds g_lock. */
static void
ensure_runner_locked(void) {
  if(!g_run || g_thread) return;
  int any = 0;
  for(int i = 0; i < g_n; i++) if(g_it[i].state == CQ_WAIT) any = 1;
  if(!any) return;
  pthread_attr_t at;
  pthread_attr_init(&at);
  pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
  pthread_attr_setstacksize(&at, 512 * 1024);
  pthread_t th;
  if(pthread_create(&th, &at, runner, NULL) == 0) g_thread = 1;
  pthread_attr_destroy(&at);
}

cJSON *
ps5tm_convqueue_status(void) {
  pthread_mutex_lock(&g_lock);
  cJSON *root = cJSON_CreateObject();
  cJSON_AddBoolToObject(root, "ok", 1);
  cJSON_AddBoolToObject(root, "running", g_run);
  cJSON_AddNumberToObject(root, "running_id", g_running_qid);
  cJSON *arr = cJSON_AddArrayToObject(root, "items");
  int w = 0, d = 0, f = 0;
  uint64_t now = ps5tm_mono_ms();
  for(int i = 0; i < g_n; i++) {
    const cq_item_t *it = &g_it[i];
    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "qid", it->qid);
    cJSON_AddStringToObject(o, "id", it->id);
    cJSON_AddStringToObject(o, "name", it->name);
    cJSON_AddStringToObject(o, "op", it->op);
    cJSON_AddStringToObject(o, "mode", it->mode);
    cJSON_AddStringToObject(o, "mount", it->mount);
    cJSON_AddStringToObject(o, "target", it->target);
    cJSON_AddStringToObject(o, "state", k_state[it->state]);
    if(it->msg[0]) cJSON_AddStringToObject(o, "message", it->msg);
    if(it->out_bytes) cJSON_AddNumberToObject(o, "output_bytes", (double)it->out_bytes);
    if(it->started_ms) {
      uint64_t end = it->finished_ms ? it->finished_ms : now;
      cJSON_AddNumberToObject(o, "elapsed_s", (double)((end - it->started_ms) / 1000));
    }
    cJSON_AddItemToArray(arr, o);
    if(it->state == CQ_WAIT || it->state == CQ_RUN) w++;
    else if(it->state == CQ_DONE) d++;
    else f++;
  }
  cJSON_AddNumberToObject(root, "open", w);
  cJSON_AddNumberToObject(root, "done", d);
  cJSON_AddNumberToObject(root, "failed", f);
  pthread_mutex_unlock(&g_lock);
  return root;
}

static const char *
str_of(const cJSON *b, const char *k) {
  const cJSON *j = b ? cJSON_GetObjectItem(b, k) : NULL;
  return cJSON_IsString(j) ? j->valuestring : "";
}

/* action: "add" (id, op, target, mode, verify, ampr_index, name), "remove" (qid), "retry" (qid), "start", "stop", "clear" (takes the
   finished entries off the list). Returns an HTTP status; the reason in err when it is not 200. */
int
ps5tm_convqueue_action(const cJSON *body, char *err, size_t err_len) {
  const char *act = str_of(body, "action");
  const cJSON *jq = body ? cJSON_GetObjectItem(body, "qid") : NULL;
  int qid = cJSON_IsNumber(jq) ? (int)jq->valuedouble : 0;

  if(!strcmp(act, "add")) {
    const char *id = str_of(body, "id"), *op = str_of(body, "op"), *mount = str_of(body, "target"), *mode = str_of(body, "mode");
    const char *tpl = str_of(body, "name");
    int quick = !strcmp(str_of(body, "verify"), "quick");
    int ampr = cJSON_IsTrue(cJSON_GetObjectItem(body, "ampr_index"));
    cJSON *chk = ps5tm_gameconvert_check(id, op, mount, mode, tpl, err, err_len);
    if(!chk) return 409;
    const cJSON *cq = cJSON_GetObjectItem(chk, "can_queue");
    const cJSON *tg = cJSON_GetObjectItem(chk, "target");
    if(!cJSON_IsTrue(cq) || !cJSON_IsString(tg) || !tg->valuestring[0]) {
      const cJSON *cs = cJSON_GetObjectItem(chk, "checks");
      const cJSON *c;
      snprintf(err, err_len, "Das lässt sich so nicht einreihen.");
      cJSON_ArrayForEach(c, cs) {
        const cJSON *lv = cJSON_GetObjectItem(c, "level"), *tx = cJSON_GetObjectItem(c, "text");
        if(cJSON_IsString(lv) && !strcmp(lv->valuestring, "error") && cJSON_IsString(tx)) { snprintf(err, err_len, "%s", tx->valuestring); break; }
      }
      cJSON_Delete(chk);
      return 409;
    }
    char name[128], source[256];
    if(ps5tm_library_copy_info(id, name, sizeof(name), source, sizeof(source)) != 0) {
      cJSON_Delete(chk);
      snprintf(err, err_len, "Dieses Spiel lässt sich nicht konvertieren.");
      return 404;
    }
    int rc = 200;
    pthread_mutex_lock(&g_lock);
    int open = 0, dup = 0;
    for(int i = 0; i < g_n; i++) {
      if(g_it[i].state == CQ_WAIT || g_it[i].state == CQ_RUN) {
        open++;
        if(!strcmp(g_it[i].target, tg->valuestring)) dup = 1;
      }
    }
    if(dup) { snprintf(err, err_len, "Dieses Abbild steht schon in der Warteschlange."); rc = 409; }
    else if(g_n >= CQ_MAX) {                                    /* make room: the finished ones go first */
      for(int i = 0; i < g_n;) { if(g_it[i].state >= CQ_DONE) remove_at_locked(i); else i++; }
      if(g_n >= CQ_MAX) { snprintf(err, err_len, "Die Warteschlange ist voll (%d Einträge).", CQ_MAX); rc = 409; }
    }
    (void)open;
    if(rc == 200) {
      cq_item_t *it = &g_it[g_n++];
      memset(it, 0, sizeof(*it));
      it->qid = g_next_qid++;
      it->state = CQ_WAIT;
      snprintf(it->id, sizeof(it->id), "%s", id);
      snprintf(it->name, sizeof(it->name), "%s", name);
      snprintf(it->op, sizeof(it->op), "%s", op);
      snprintf(it->mount, sizeof(it->mount), "%s", mount);
      snprintf(it->mode, sizeof(it->mode), "%s", mode);
      snprintf(it->name_tpl, sizeof(it->name_tpl), "%s", tpl);
      snprintf(it->target, sizeof(it->target), "%s", tg->valuestring);
      it->ampr = ampr;
      it->quick = quick;
      g_run = 1;                                                /* adding starts the queue */
      ensure_runner_locked();
      PS5TM_INFO("convq_add", "Konvertier-Warteschlange: %s (%s) nach %s eingereiht.", name, id, tg->valuestring);
    }
    pthread_mutex_unlock(&g_lock);
    cJSON_Delete(chk);
    return rc;
  }

  int rc = 200;
  pthread_mutex_lock(&g_lock);
  if(!strcmp(act, "start")) {
    g_run = 1;
    ensure_runner_locked();
  } else if(!strcmp(act, "stop")) {
    g_run = 0;
  } else if(!strcmp(act, "clear")) {
    for(int i = 0; i < g_n;) { if(g_it[i].state >= CQ_DONE) remove_at_locked(i); else i++; }
  } else if(!strcmp(act, "remove") || !strcmp(act, "retry")) {
    cq_item_t *it = find_locked(qid);
    if(!it) { snprintf(err, err_len, "Diesen Eintrag gibt es nicht mehr."); rc = 404; }
    else if(it->state == CQ_RUN) { snprintf(err, err_len, "Dieser Auftrag läuft gerade. Brich ihn ab, dann geht es weiter."); rc = 409; }
    else if(!strcmp(act, "remove")) remove_at_locked((int)(it - g_it));
    else if(it->state == CQ_WAIT) { /* already waiting */ }
    else {
      it->state = CQ_WAIT;
      it->msg[0] = 0;
      it->out_bytes = 0;
      it->started_ms = it->finished_ms = 0;
      g_run = 1;
      ensure_runner_locked();
    }
  } else {
    snprintf(err, err_len, "Unbekannte Aktion.");
    rc = 400;
  }
  pthread_mutex_unlock(&g_lock);
  return rc;
}
