/* SPDX-License-Identifier: GPL-3.0-or-later
 * The dump queue of the Disc tab (25.10.2026).
 *
 * The order of work and its rules come from ps5-app-dumper 2.10 (Copyright (C) 2025 EchoStretch, Copyright (C) 2026 slopmaster33;
 * GPL-3.0-or-later; source/dump_queue.c, routes_dumper.c), rewritten for this app: up to 64 titles are dumped one after the other.
 * Each one is started (the game running before it is closed), given the load time to finish loading, and dumped with the
 * dump job of dump_job.c. A disc game whose disc is not in the drive holds the queue for as long as it takes, with a reminder on
 * the console every two minutes. A title that does not come up, does not fit or fails is marked, and the queue goes on.
 *
 * The list is kept here, on the console: the page may be closed meanwhile. It is not kept across a restart of the app; a restart
 * in the middle of a run is told once ("interrupted"). Nothing here touches a path the browser named: the titles are ids that the
 * game list knows, the target is the one of the dump settings. */

#include <ctype.h>
#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "ps5tm.h"
#include "third_party/cJSON.h"

#define DQ_MAX        64
#define DQ_SETTLE_MIN 5
#define DQ_SETTLE_MAX 600
#define DQ_SETTLE_DEF 30
#define DQ_DIR        PS5TM_DATA_DIR "/dump"
#define DQ_CONF       DQ_DIR "/queue.json"
#define DQ_MARK       DQ_DIR "/queue-run"

#ifndef DQ_MOUNT_TIMEOUT                  /* how long a started title may take to show up under pfsmnt */
#define DQ_MOUNT_TIMEOUT 120
#endif
#ifndef DQ_DISC_SETTLE                    /* a disc just put in is seen before the console is done with it */
#define DQ_DISC_SETTLE 8
#endif
#ifndef DQ_DISC_REMINDER                  /* how often the console is reminded that the queue wants a disc */
#define DQ_DISC_REMINDER 120
#endif
#define DQ_LAUNCH_TRIES 3
#define DQ_LAUNCH_GAP   5

enum { Q_PENDING, Q_WAITING_DISC, Q_LAUNCHING, Q_SETTLING, Q_DUMPING, Q_DONE, Q_FAILED, Q_SKIPPED };
static const char *const k_state[] = { "pending", "waiting_disc", "launching", "settling", "dumping", "done", "failed", "skipped" };

typedef struct {
  char            id[12];
  char            name[128];
  int             disc;                 /* a disc game: the queue holds until its disc is in the drive */
  ps5tm_dumpopt_t opt;                  /* opt.set: dumped with settings of its own */
  int             state;
  char            msg[192];
} dq_item_t;

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static dq_item_t       g_item[DQ_MAX];
static int             g_n;
static int             g_active;
static int             g_current = -1;
static int             g_settle = DQ_SETTLE_DEF;
static int             g_replace;
static int             g_wait_left;       /* seconds left in the wait that is going on */
static int             g_interrupted;     /* told once: a run was cut by a restart of the app */
static int             g_boot_checked;
static uint64_t        g_started_ms, g_finished_ms;
static volatile int    g_stop, g_skip;
static pthread_t       g_worker;
static int             g_worker_valid;

#ifndef DQ_SLEEP_MS
#define DQ_SLEEP_MS(ms) usleep((useconds_t)(ms) * 1000u)
#endif

/* ---------------------------------------------------------------- small helpers */

static int
tid_ok(const char *s) {
  if(!s || strlen(s) != 9 || (strncmp(s, "PPSA", 4) && strncmp(s, "CUSA", 4))) return 0;
  for(int i = 4; i < 9; i++)
    if(!isdigit((unsigned char)s[i])) return 0;
  return 1;
}

static void
set_item(int i, int state, const char *fmt, ...) __attribute__((format(printf, 3, 4)));

static void
set_item(int i, int state, const char *fmt, ...) {
  char msg[sizeof(g_item[0].msg)] = "";
  if(fmt) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
  }
  pthread_mutex_lock(&g_lock);
  g_item[i].state = state;
  memcpy(g_item[i].msg, msg, sizeof(msg));
  g_wait_left = 0;
  pthread_mutex_unlock(&g_lock);
}

static void
set_wait(int seconds) {
  pthread_mutex_lock(&g_lock);
  g_wait_left = seconds;
  pthread_mutex_unlock(&g_lock);
}

static int
clampi(int v, int lo, int hi) {
  return v < lo ? lo : v > hi ? hi : v;
}

static void
write_conf(void) {
  mkdir(PS5TM_DATA_DIR, 0777);
  mkdir(DQ_DIR, 0777);
  char b[96];
  int len = snprintf(b, sizeof(b), "{\"delay\":%d,\"replace\":%s}\n", g_settle, g_replace ? "true" : "false");
  char tmp[sizeof(DQ_CONF) + 8];
  snprintf(tmp, sizeof(tmp), "%s.tmp", DQ_CONF);
  FILE *f = fopen(tmp, "w");
  if(!f) return;
  int bad = fwrite(b, 1, (size_t)len, f) != (size_t)len;
  bad |= fflush(f) != 0;
  bad |= fclose(f) != 0;
  if(bad || rename(tmp, DQ_CONF) != 0) unlink(tmp);
}

/* the two settings of the queue and the sign of an interrupted run, read once */
static void
boot_locked(void) {
  if(g_boot_checked) return;
  g_boot_checked = 1;
  FILE *f = fopen(DQ_CONF, "r");
  if(f) {
    char b[256];
    size_t n = fread(b, 1, sizeof(b) - 1, f);
    fclose(f);
    b[n] = 0;
    cJSON *j = cJSON_Parse(b);
    const cJSON *v;
    if(j) {
      if(cJSON_IsNumber(v = cJSON_GetObjectItem(j, "delay"))) g_settle = clampi((int)v->valuedouble, DQ_SETTLE_MIN, DQ_SETTLE_MAX);
      if(cJSON_IsBool(v = cJSON_GetObjectItem(j, "replace"))) g_replace = cJSON_IsTrue(v);
      cJSON_Delete(j);
    }
  }
  struct stat st;
  if(stat(DQ_MARK, &st) == 0) {                /* a run was under way when the app ended */
    g_interrupted = 1;
    unlink(DQ_MARK);
  }
}

/* ---------------------------------------------------------------- waiting */

#define WAIT_STOPPED (-1)
#define WAIT_SKIPPED (-2)

/* Waits up to `seconds` for cond to hold (no cond: simply waits). 1 cond held, 0 the time ran out, WAIT_STOPPED, WAIT_SKIPPED. */
static int
wait_until(int seconds, int (*cond)(const char *), const char *arg) {
  for(int left = seconds; left > 0; left--) {
    if(g_stop) return WAIT_STOPPED;
    if(g_skip) return WAIT_SKIPPED;
    if(cond && cond(arg)) return 1;
    set_wait(left);
    DQ_SLEEP_MS(1000);
  }
  set_wait(0);
  if(g_stop) return WAIT_STOPPED;
  if(g_skip) return WAIT_SKIPPED;
  return (cond && cond(arg)) ? 1 : 0;
}

static int
cond_mounted(const char *id) {
  return ps5tm_dump_mounted(id);
}

static int
cond_disc(const char *id) {
  return ps5tm_title_on_disc(id);
}

/* Holds until the title's disc is in the drive. Swapping a disc needs a person, who may be away for hours: no time limit, only a
   stop or a skip ends it early. 0 once the disc is there (and given a moment), else the WAIT_* code. */
static int
wait_for_disc(int i, const char *id, const char *name) {
  if(cond_disc(id)) return 0;
  set_item(i, Q_WAITING_DISC, "Bitte die Disc dieses Spiels einlegen.");
  PS5TM_INFO("dumpq_disc_wait", "Dump-Warteschlange: wartet auf die Disc von %s.", id);
  for(unsigned waited = 0;; waited++) {
    if(g_stop) return WAIT_STOPPED;
    if(g_skip) return WAIT_SKIPPED;
    if(cond_disc(id)) break;
    if(waited % DQ_DISC_REMINDER == 0) ps5tm_notify_plain("Dump-Warteschlange: Disc einlegen: %s", name);
    DQ_SLEEP_MS(1000);
  }
  PS5TM_INFO("dumpq_disc_found", "Dump-Warteschlange: Disc von %s gefunden.", id);
  set_item(i, Q_WAITING_DISC, "Disc gefunden, die Konsole bekommt einen Moment.");
  int rc = wait_until(DQ_DISC_SETTLE, NULL, NULL);
  return rc < 0 ? rc : 0;
}

/* Brings the title up: 0 once it is mounted and had its load time, 1 when it failed (told on the item), else the WAIT_* code. */
static int
bring_up(int i, const char *id, int settle) {
  char err[192] = "";
  set_item(i, Q_LAUNCHING, "Das Spiel wird gestartet.");
  for(int attempt = 1;; attempt++) {
    if(ps5tm_library_switch(id, err, sizeof(err)) == 0) break;
    PS5TM_WARN("dumpq_launch", "Dump-Warteschlange: Start von %s gescheitert (Versuch %d): %s", id, attempt, err);
    if(attempt >= DQ_LAUNCH_TRIES) {
      set_item(i, Q_FAILED, "%s", err);
      return 1;
    }
    int gap = wait_until(DQ_LAUNCH_GAP, NULL, NULL);
    if(gap < 0) return gap;
  }
  int rc = wait_until(DQ_MOUNT_TIMEOUT, cond_mounted, id);
  if(rc < 0) return rc;
  if(rc == 0) {
    set_item(i, Q_FAILED, "Das Spiel kam nicht innerhalb von %d Sekunden hoch.", DQ_MOUNT_TIMEOUT);
    return 1;
  }
  set_item(i, Q_SETTLING, "Das Spiel wird geladen, die Ladezeit läuft.");
  rc = wait_until(settle, NULL, NULL);
  if(rc < 0) return rc;
  if(!ps5tm_dump_mounted(id)) {
    set_item(i, Q_FAILED, "Das Spiel wurde wieder beendet, bevor der Dump begann.");
    return 1;
  }
  return 0;
}

/* ---------------------------------------------------------------- one title */

/* -1 when the queue was stopped, else 0. */
static int
process_item(int i) {
  dq_item_t it;
  int settle, replace, total;
  pthread_mutex_lock(&g_lock);
  it = g_item[i];
  settle = g_settle;
  replace = g_replace;
  total = g_n;
  pthread_mutex_unlock(&g_lock);

  if(it.opt.set) PS5TM_INFO("dumpq_item", "Dump-Warteschlange: %d von %d: %s (%s), mit eigenen Einstellungen.", i + 1, total, it.name, it.id);
  else PS5TM_INFO("dumpq_item", "Dump-Warteschlange: %d von %d: %s (%s).", i + 1, total, it.name, it.id);

  /* a title with a finished dump is passed over: starting a game only to learn that costs minutes; one cut short is cleared
     away by the dump itself */
  if(!replace && ps5tm_dump_present(it.id) == 1) {
    set_item(i, Q_SKIPPED, "Schon gedumpt, übersprungen.");
    return 0;
  }

  ps5tm_title_marks_t m;
  ps5tm_title_marks(it.id, &m);
  if(m.install_pct >= 0 && m.install_pct < 100 && m.install_pending) {
    set_item(i, Q_FAILED, "Das Spiel wird gerade noch installiert; ein Dump hätte Lücken.");
    return 0;
  }

  if(!ps5tm_dump_mounted(it.id)) {
    ps5tm_libdel_t li;
    if(ps5tm_library_delete_info(it.id, &li) == 0 && li.smp && strcmp(li.format, "pkg")) {
      set_item(i, Q_FAILED, "Läuft über ShadowMountPlus aus einem Ordner oder Abbild: Es gibt keinen Einhängepunkt zum Dumpen.");
      return 0;
    }
    int rc = 0;
    if(it.disc) rc = wait_for_disc(i, it.id, it.name);
    if(rc == 0) rc = bring_up(i, it.id, settle);
    if(rc == WAIT_SKIPPED) {
      PS5TM_INFO("dumpq_skip", "Dump-Warteschlange: %s auf Wunsch übersprungen.", it.id);
      set_item(i, Q_SKIPPED, "Übersprungen.");
      return 0;
    }
    if(rc != 0) return rc < 0 ? -1 : 0;
  }
  if(g_stop) return -1;

  char msg[320] = "";
  int http = ps5tm_dump_start_item(it.id, replace, it.opt.set ? &it.opt : NULL, msg, sizeof(msg));
  if(http == 409 && !replace && ps5tm_dump_present(it.id) == 1) {
    set_item(i, Q_SKIPPED, "Schon gedumpt, übersprungen.");
    return 0;
  }
  if(http != 200) {
    set_item(i, Q_FAILED, "%s", msg[0] ? msg : "Der Dump ließ sich nicht starten.");
    return 0;
  }
  set_item(i, Q_DUMPING, NULL);

  /* the stop of the queue aborts the dump itself; a stop that came just before the dump existed is caught here */
  if(g_stop) ps5tm_dump_abort();
  while(ps5tm_dump_busy()) DQ_SLEEP_MS(500);

  char state[16] = "", why[320] = "";
  ps5tm_dump_last(state, sizeof(state), why, sizeof(why));
  if(!strcmp(state, "done")) {
    set_item(i, Q_DONE, "Dump fertig.");
    return 0;
  }
  if(!strcmp(state, "cancelled")) {
    if(g_skip && !g_stop) {
      PS5TM_INFO("dumpq_dump_stopped", "Dump-Warteschlange: der Dump von %s wurde gestoppt, weiter mit dem nächsten.", it.id);
      set_item(i, Q_SKIPPED, "Dump gestoppt, die Dateien im Ziel sind unvollständig.");
      return 0;
    }
    return -1;
  }
  set_item(i, Q_FAILED, "%s", why[0] ? why : "Der Dump ist gescheitert.");
  return 0;
}

static void *
worker(void *arg) {
  (void)arg;
  int stopped = 0, count;
  pthread_mutex_lock(&g_lock);
  count = g_n;
  pthread_mutex_unlock(&g_lock);

  for(int i = 0; i < count && !stopped; i++) {
    pthread_mutex_lock(&g_lock);
    g_current = i;
    pthread_mutex_unlock(&g_lock);
    g_skip = 0;                                  /* a skip is meant for one title only */
    if(g_stop || process_item(i) < 0) stopped = 1;
  }

  int done = 0;
  pthread_mutex_lock(&g_lock);
  for(int i = 0; i < g_n; i++) {
    dq_item_t *it = &g_item[i];
    if(it->state == Q_DONE) { done++; continue; }
    if(it->state == Q_FAILED || it->state == Q_SKIPPED) continue;
    int reached = it->state != Q_PENDING;        /* cut short by the stop, either midway or before its turn */
    it->state = reached ? Q_FAILED : Q_SKIPPED;
    snprintf(it->msg, sizeof(it->msg), "%s", reached ? "Von dir gestoppt." : "Die Warteschlange wurde vor diesem Titel gestoppt.");
  }
  g_active = 0;
  g_current = -1;
  g_wait_left = 0;
  g_finished_ms = ps5tm_mono_ms();
  int n = g_n;
  pthread_mutex_unlock(&g_lock);

  unlink(DQ_MARK);
  if(stopped) {
    PS5TM_INFO("dumpq_end", "Dump-Warteschlange gestoppt: %d von %d gedumpt.", done, n);
    ps5tm_notify("Dump-Warteschlange gestoppt: %d von %d gedumpt.", done, n);
  } else {
    PS5TM_INFO("dumpq_end", "Dump-Warteschlange fertig: %d von %d gedumpt.", done, n);
    ps5tm_notify("Dump-Warteschlange fertig: %d von %d gedumpt.", done, n);
  }
  ps5tm_powerguard_release();
  return NULL;
}

/* ---------------------------------------------------------------- interface */

int
ps5tm_dumpq_active(void) {
  pthread_mutex_lock(&g_lock);
  int a = g_active;
  pthread_mutex_unlock(&g_lock);
  return a;
}

static int
find_locked(const char *id) {
  for(int i = 0; i < g_n; i++)
    if(!strcmp(g_item[i].id, id)) return i;
  return -1;
}

/* the options of a body ({decrypt, fself, backport, ps4_backport_level, ps5_backport_level, ps4_split}), pulled into their ranges */
static void
read_opt(const cJSON *o, ps5tm_dumpopt_t *opt) {
  const cJSON *v;
  memset(opt, 0, sizeof(*opt));
  ps5tm_dump_settings(opt, NULL, 0);             /* what is not named stays as the global settings have it */
  if(cJSON_IsBool(v = cJSON_GetObjectItem(o, "decrypt"))) opt->decrypt = cJSON_IsTrue(v);
  if(cJSON_IsBool(v = cJSON_GetObjectItem(o, "fself"))) opt->fself = cJSON_IsTrue(v);
  if(cJSON_IsBool(v = cJSON_GetObjectItem(o, "backport"))) opt->backport = cJSON_IsTrue(v);
  if(cJSON_IsNumber(v = cJSON_GetObjectItem(o, "ps4_backport_level"))) opt->bp_ps4 = clampi((int)v->valuedouble, 1, 6);
  if(cJSON_IsNumber(v = cJSON_GetObjectItem(o, "ps5_backport_level"))) opt->bp_ps5 = clampi((int)v->valuedouble, 1, 10);
  if(cJSON_IsNumber(v = cJSON_GetObjectItem(o, "ps4_split"))) opt->ps4_split = clampi((int)v->valuedouble, 0, 3);
  if(!opt->decrypt) opt->fself = opt->backport = 0;
  opt->set = 1;
}

static int
queue_start(const cJSON *b, char *msg, size_t n) {
  char target[40];
  ps5tm_dumpopt_t g;
  ps5tm_dump_settings(&g, target, sizeof(target));
  if(!target[0]) { snprintf(msg, n, "Bitte zuerst bei den Einstellungen ein Ziel wählen."); return 400; }
  if(ps5tm_dump_busy()) { snprintf(msg, n, "Es läuft schon ein Dump."); return 409; }
  if(ps5tm_filejobs_busy_for_dump(msg, n)) return 409;

  pthread_mutex_lock(&g_lock);
  if(g_active) { pthread_mutex_unlock(&g_lock); snprintf(msg, n, "Die Warteschlange läuft schon."); return 409; }
  if(g_n == 0) { pthread_mutex_unlock(&g_lock); snprintf(msg, n, "Die Warteschlange ist leer."); return 400; }
  char first[12];
  snprintf(first, sizeof(first), "%s", g_item[0].id);
  pthread_mutex_unlock(&g_lock);

  /* a game that runs now and is not the first of the list is closed by the start of the next title: that needs the person's yes */
  char run[16] = "";
  if(ps5tm_procmgr_game_title(0, run, sizeof(run)) == 0 && run[0] && strcmp(run, first)) {
    const cJSON *c = b ? cJSON_GetObjectItem(b, "confirm_close") : NULL;
    if(!cJSON_IsTrue(c)) {
      snprintf(msg, n, "Das laufende Spiel (%s) wird beendet, sobald der erste Titel startet. Zum Bestätigen noch einmal starten.", run);
      return 409;
    }
  }

  pthread_mutex_lock(&g_lock);
  if(g_active) { pthread_mutex_unlock(&g_lock); snprintf(msg, n, "Die Warteschlange läuft schon."); return 409; }
  if(g_worker_valid) {                           /* a finished worker is joined before the next one starts */
    pthread_mutex_unlock(&g_lock);
    pthread_join(g_worker, NULL);
    pthread_mutex_lock(&g_lock);
    g_worker_valid = 0;
  }
  for(int i = 0; i < g_n; i++) { g_item[i].state = Q_PENDING; g_item[i].msg[0] = 0; }   /* a list that was run is run again from the top */
  g_interrupted = 0;
  g_current = -1;
  g_active = 1;
  g_stop = 0;
  g_skip = 0;
  g_started_ms = ps5tm_mono_ms();
  g_finished_ms = 0;
  int count = g_n, settle = g_settle, replace = g_replace;
  pthread_mutex_unlock(&g_lock);

  ps5tm_powerguard_hold();
  mkdir(PS5TM_DATA_DIR, 0777);
  mkdir(DQ_DIR, 0777);
  FILE *mk = fopen(DQ_MARK, "w");
  if(mk) fclose(mk);

  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, 256 * 1024);
  int rc = pthread_create(&g_worker, &attr, worker, NULL);
  pthread_attr_destroy(&attr);
  if(rc != 0) {
    pthread_mutex_lock(&g_lock);
    g_active = 0;
    pthread_mutex_unlock(&g_lock);
    ps5tm_powerguard_release();
    unlink(DQ_MARK);
    snprintf(msg, n, "Die Warteschlange ließ sich nicht starten.");
    return 500;
  }
  pthread_mutex_lock(&g_lock);
  g_worker_valid = 1;
  pthread_mutex_unlock(&g_lock);
  if(replace) PS5TM_INFO("dumpq_start", "Dump-Warteschlange gestartet: %d Titel, %d s Ladezeit, vorhandene Dumps werden ersetzt.", count, settle);
  else PS5TM_INFO("dumpq_start", "Dump-Warteschlange gestartet: %d Titel, %d s Ladezeit, vorhandene Dumps werden übersprungen.", count, settle);
  snprintf(msg, n, "Die Warteschlange läuft.");
  return 200;
}

int
ps5tm_dumpq_action(const cJSON *b, char *msg, size_t n) {
  const cJSON *ja = cJSON_GetObjectItem(b, "action");
  const char *act = cJSON_IsString(ja) ? ja->valuestring : "";
  const cJSON *jid = cJSON_GetObjectItem(b, "id");
  const char *id = cJSON_IsString(jid) ? jid->valuestring : "";

  pthread_mutex_lock(&g_lock);
  boot_locked();
  pthread_mutex_unlock(&g_lock);

  if(!strcmp(act, "queue_start")) return queue_start(b, msg, n);

  if(!strcmp(act, "queue_stop")) {
    if(!ps5tm_dumpq_active()) { snprintf(msg, n, "Die Warteschlange läuft nicht."); return 409; }
    g_stop = 1;
    if(ps5tm_dump_busy()) ps5tm_dump_abort();
    PS5TM_INFO("dumpq_stop", "Dump-Warteschlange: Stopp angefordert.");
    snprintf(msg, n, "Die Warteschlange wird gestoppt. Ein laufender Dump bleibt unfertig im Ziel.");
    return 200;
  }

  if(!strcmp(act, "queue_skip")) {
    pthread_mutex_lock(&g_lock);
    int waiting = 0, dumping = 0;
    if(g_active && g_current >= 0) {
      int st = g_item[g_current].state;
      waiting = st == Q_WAITING_DISC || st == Q_LAUNCHING || st == Q_SETTLING;
      dumping = st == Q_DUMPING;
    }
    pthread_mutex_unlock(&g_lock);
    if(!waiting && !dumping) { snprintf(msg, n, "Die Warteschlange arbeitet gerade an keinem Titel."); return 409; }
    g_skip = 1;
    if(dumping) ps5tm_dump_abort();              /* a dump does not look at g_skip: it ends like any other, and g_skip tells the worker to go on */
    snprintf(msg, n, dumping ? "Der Dump wird gestoppt, dann geht es mit dem nächsten Titel weiter." : "Der Titel wird übersprungen.");
    return 200;
  }

  /* everything below changes the list: only while no run is under way */
  if(ps5tm_dumpq_active()) { snprintf(msg, n, "Die Warteschlange läuft, die Liste lässt sich jetzt nicht ändern."); return 409; }

  if(!strcmp(act, "queue_settings")) {
    const cJSON *v;
    pthread_mutex_lock(&g_lock);
    if(cJSON_IsNumber(v = cJSON_GetObjectItem(b, "delay"))) g_settle = clampi((int)v->valuedouble, DQ_SETTLE_MIN, DQ_SETTLE_MAX);
    if(cJSON_IsBool(v = cJSON_GetObjectItem(b, "replace"))) g_replace = cJSON_IsTrue(v);
    pthread_mutex_unlock(&g_lock);
    write_conf();
    snprintf(msg, n, "Gespeichert.");
    return 200;
  }

  if(!strcmp(act, "queue_clear")) {
    pthread_mutex_lock(&g_lock);
    g_n = 0;
    g_current = -1;
    g_interrupted = 0;
    memset(g_item, 0, sizeof(g_item));
    pthread_mutex_unlock(&g_lock);
    snprintf(msg, n, "Die Liste ist leer.");
    return 200;
  }

  if(!tid_ok(id)) { snprintf(msg, n, "Das ist keine Titelkennung eines PS5- oder PS4-Spiels."); return 400; }

  if(!strcmp(act, "queue_add")) {
    char name[128], link[16];
    if(ps5tm_library_launch_info(id, name, sizeof(name), link, sizeof(link)) != 0) {
      snprintf(msg, n, "Dieses Spiel steht nicht in der Spieleliste.");
      return 404;
    }
    ps5tm_title_marks_t m;
    ps5tm_title_marks(id, &m);
    const cJSON *jd = cJSON_GetObjectItem(b, "disc");
    int disc = cJSON_IsBool(jd) ? cJSON_IsTrue(jd) : m.disc != 0;
    pthread_mutex_lock(&g_lock);
    if(g_n > 0 && g_item[0].state >= Q_DONE) {   /* the list of a finished run is replaced by the new one */
      g_n = 0;
      memset(g_item, 0, sizeof(g_item));
    }
    if(find_locked(id) >= 0) { pthread_mutex_unlock(&g_lock); snprintf(msg, n, "Dieses Spiel steht schon in der Warteschlange."); return 409; }
    if(g_n >= DQ_MAX) { pthread_mutex_unlock(&g_lock); snprintf(msg, n, "Eine Warteschlange fasst höchstens %d Titel.", DQ_MAX); return 409; }
    dq_item_t *it = &g_item[g_n++];
    memset(it, 0, sizeof(*it));
    snprintf(it->id, sizeof(it->id), "%s", id);
    snprintf(it->name, sizeof(it->name), "%s", name[0] ? name : id);
    it->disc = disc;
    g_interrupted = 0;
    pthread_mutex_unlock(&g_lock);
    if(disc) ps5tm_title_remember_disc(id);       /* what the person knows about a title is worth keeping */
    snprintf(msg, n, "In die Warteschlange gelegt.");
    return 200;
  }

  pthread_mutex_lock(&g_lock);
  int at = find_locked(id);
  if(at < 0) { pthread_mutex_unlock(&g_lock); snprintf(msg, n, "Dieses Spiel steht nicht in der Warteschlange."); return 404; }

  if(!strcmp(act, "queue_remove")) {
    for(int i = at; i + 1 < g_n; i++) g_item[i] = g_item[i + 1];
    memset(&g_item[--g_n], 0, sizeof(g_item[0]));
    pthread_mutex_unlock(&g_lock);
    snprintf(msg, n, "Aus der Warteschlange genommen.");
    return 200;
  }

  if(!strcmp(act, "queue_set")) {
    const cJSON *v, *jo = cJSON_GetObjectItem(b, "options");
    int remember = 0;
    if(cJSON_IsBool(v = cJSON_GetObjectItem(b, "disc"))) {
      g_item[at].disc = cJSON_IsTrue(v);
      remember = g_item[at].disc;
    }
    if(cJSON_IsTrue(cJSON_GetObjectItem(b, "reset"))) memset(&g_item[at].opt, 0, sizeof(g_item[at].opt));
    else if(cJSON_IsObject(jo)) read_opt(jo, &g_item[at].opt);
    pthread_mutex_unlock(&g_lock);
    if(remember) ps5tm_title_remember_disc(id);
    snprintf(msg, n, "Gespeichert.");
    return 200;
  }

  pthread_mutex_unlock(&g_lock);
  snprintf(msg, n, "Diese Aktion kennt die App nicht.");
  return 400;
}

cJSON *
ps5tm_dumpq_json(void) {
  pthread_mutex_lock(&g_lock);
  boot_locked();
  pthread_mutex_unlock(&g_lock);

  ps5tm_dumpopt_t gl;
  ps5tm_dump_settings(&gl, NULL, 0);

  cJSON *o = cJSON_CreateObject();
  pthread_mutex_lock(&g_lock);
  dq_item_t *items = malloc(sizeof(g_item[0]) * (size_t)(g_n ? g_n : 1));
  int n = g_n;
  if(items) memcpy(items, g_item, sizeof(g_item[0]) * (size_t)n);
  int active = g_active, cur = g_current, wait = g_wait_left, settle = g_settle, replace = g_replace, intr = g_interrupted;
  pthread_mutex_unlock(&g_lock);

  cJSON_AddBoolToObject(o, "active", active);
  cJSON_AddNumberToObject(o, "current", cur);
  cJSON_AddNumberToObject(o, "count", n);
  cJSON_AddNumberToObject(o, "delay", settle);
  cJSON_AddBoolToObject(o, "replace", replace);
  cJSON_AddNumberToObject(o, "wait", wait);
  cJSON_AddBoolToObject(o, "interrupted", intr);
  cJSON_AddNumberToObject(o, "max", DQ_MAX);
  cJSON *arr = cJSON_AddArrayToObject(o, "items");
  for(int i = 0; items && i < n; i++) {
    const dq_item_t *it = &items[i];
    const ps5tm_dumpopt_t *e = it->opt.set ? &it->opt : &gl;
    ps5tm_title_marks_t m;
    ps5tm_title_marks(it->id, &m);
    cJSON *e1 = cJSON_CreateObject();
    cJSON_AddStringToObject(e1, "id", it->id);
    cJSON_AddStringToObject(e1, "name", it->name);
    cJSON_AddBoolToObject(e1, "disc", it->disc);
    cJSON_AddBoolToObject(e1, "disc_in", m.disc == 2);
    cJSON_AddBoolToObject(e1, "custom", it->opt.set);
    cJSON_AddBoolToObject(e1, "dumped", m.dump == 1);
    cJSON_AddStringToObject(e1, "state", k_state[it->state]);
    cJSON_AddStringToObject(e1, "message", it->msg);
    cJSON *op = cJSON_AddObjectToObject(e1, "options");
    cJSON_AddBoolToObject(op, "decrypt", e->decrypt);
    cJSON_AddBoolToObject(op, "fself", e->fself);
    cJSON_AddBoolToObject(op, "backport", e->backport);
    cJSON_AddNumberToObject(op, "ps4_backport_level", e->bp_ps4);
    cJSON_AddNumberToObject(op, "ps5_backport_level", e->bp_ps5);
    cJSON_AddNumberToObject(op, "ps4_split", e->ps4_split);
    cJSON_AddItemToArray(arr, e1);
  }
  free(items);
  return o;
}
