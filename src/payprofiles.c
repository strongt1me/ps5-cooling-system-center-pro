/* Payload profiles: named sequences of payload files, with pauses in between.
 *
 * A profile is a list of entries; an entry is the name of a file in the
 * internal payloads folder (/data/PS5-Cooling-Center/payloads) or a pause
 * "!<ms>". One profile can be the startup profile: the app runs it by itself
 * a little after it has started. Any profile can be run with one click, and a
 * running one can be stopped between two entries.
 *
 * The idea (profiles, pauses, startup profile) comes from ps5-payload-manager
 * by itsPLK (GPL-3.0); this is a rebuild for this app's own payload folder,
 * not a port of that program's code.
 *
 * Saved in PS5TM_DATA_DIR/payload-profiles.json:
 *   {"startup":"<id>","profiles":[{"id":"…","name":"…","items":["a.elf","!2000","b.elf"]}]}
 * The page sends the whole document; every part of it is checked here again
 * (counts, lengths, names that are one path component ending in ".elf").
 *
 * Boot-loop protection: while a profile runs, a marker file exists. If the app
 * starts and finds the marker, the previous run did not end (a payload in it
 * restarted the app, or the console crashed), so the startup profile is NOT
 * run again; the marker is removed and the reason is logged. */

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "ps5tm.h"
#include "third_party/cJSON.h"

#define PP_FILE        PS5TM_DATA_DIR "/payload-profiles.json"
#define PP_MARK        PS5TM_DATA_DIR "/payload-profile-run.txt"
#define PP_MAX_PROF    24
#define PP_MAX_ITEMS   64
#define PP_ID_MAX      33
#define PP_NAME_MAX    80
#define PP_ITEM_MAX    200
#define PP_DELAY_MAX   600000
#define PP_STARTUP_MS  25000u              /* after the app is up: the console is done with its own start */

typedef struct {
  char id[PP_ID_MAX];
  char name[PP_NAME_MAX * 4];              /* UTF-8 */
  int  n;
  char items[PP_MAX_ITEMS][PP_ITEM_MAX];
} pp_profile_t;

typedef struct {
  int          startup_set;
  char         startup[PP_ID_MAX];
  int          n;
  pp_profile_t p[PP_MAX_PROF];
} pp_set_t;

typedef struct {
  char item[PP_ITEM_MAX];
  int  ok;
  char msg[160];
} pp_step_t;

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;     /* the file and the status */
static pp_set_t        g_set;                                   /* kept in memory, loaded by init */
static int             g_loaded;

static struct {
  int       running;
  int       stop;
  char      id[PP_ID_MAX];
  char      name[PP_NAME_MAX * 4];
  int       step, total;
  int       from_startup;
  int       nres;
  pp_step_t res[PP_MAX_ITEMS];
} g_run;


/* ------------------------------------------------------------ checks */

static int
item_ok(const char *s) {
  size_t n = strlen(s);
  if(n == 0 || n >= PP_ITEM_MAX) return 0;
  if(s[0] == '!') {
    if(n > 7) return 0;
    for(size_t i = 1; i < n; i++) if(s[i] < '0' || s[i] > '9') return 0;
    long v = atol(s + 1);
    return v >= 1 && v <= PP_DELAY_MAX;
  }
  if(s[0] == '.' || strchr(s, '/') || strchr(s, '\\')) return 0;
  if(n < 5 || strcasecmp(s + n - 4, ".elf") != 0) return 0;
  for(size_t i = 0; i < n; i++) if((unsigned char)s[i] < 0x20) return 0;
  return 1;
}

static int
id_ok(const char *s) {
  size_t n = strlen(s);
  if(n == 0 || n >= PP_ID_MAX) return 0;
  for(size_t i = 0; i < n; i++) {
    char c = s[i];
    if(!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_')) return 0;
  }
  return 1;
}

static int
find_id(const pp_set_t *s, const char *id) {
  for(int i = 0; i < s->n; i++) if(!strcmp(s->p[i].id, id)) return i;
  return -1;
}

/* Parse and check a whole document into out; 0 = ok, else a German reason in err. */
static int
parse_doc(const char *json, pp_set_t *out, char *err, size_t en) {
  memset(out, 0, sizeof(*out));
  cJSON *root = cJSON_Parse(json);
  if(!cJSON_IsObject(root)) { cJSON_Delete(root); snprintf(err, en, "Das ist kein gültiges JSON."); return -1; }
  const cJSON *arr = cJSON_GetObjectItem(root, "profiles");
  const cJSON *st  = cJSON_GetObjectItem(root, "startup");
  if(!cJSON_IsArray(arr)) { cJSON_Delete(root); snprintf(err, en, "„profiles“ fehlt."); return -1; }
  if(cJSON_GetArraySize(arr) > PP_MAX_PROF) {
    cJSON_Delete(root);
    snprintf(err, en, "Es sind höchstens %d Profile möglich.", PP_MAX_PROF);
    return -1;
  }
  const cJSON *e;
  cJSON_ArrayForEach(e, arr) {
    const cJSON *id = cJSON_GetObjectItem(e, "id"), *nm = cJSON_GetObjectItem(e, "name"),
                *it = cJSON_GetObjectItem(e, "items");
    if(!cJSON_IsString(id) || !id_ok(id->valuestring) || find_id(out, id->valuestring) >= 0) {
      cJSON_Delete(root); snprintf(err, en, "Ein Profil hat keine gültige oder eine doppelte Kennung."); return -1;
    }
    if(!cJSON_IsString(nm) || !nm->valuestring[0] || strlen(nm->valuestring) >= sizeof(out->p[0].name) ||
       strlen(nm->valuestring) > PP_NAME_MAX * 3) {
      cJSON_Delete(root); snprintf(err, en, "Ein Profil braucht einen Namen (höchstens %d Zeichen).", PP_NAME_MAX); return -1;
    }
    for(const unsigned char *c = (const unsigned char *)nm->valuestring; *c; c++)
      if(*c < 0x20) { cJSON_Delete(root); snprintf(err, en, "Der Name enthält Steuerzeichen."); return -1; }
    if(!cJSON_IsArray(it) || cJSON_GetArraySize(it) > PP_MAX_ITEMS) {
      cJSON_Delete(root); snprintf(err, en, "Ein Profil hat höchstens %d Einträge.", PP_MAX_ITEMS); return -1;
    }
    pp_profile_t *p = &out->p[out->n];
    snprintf(p->id, sizeof(p->id), "%s", id->valuestring);
    snprintf(p->name, sizeof(p->name), "%s", nm->valuestring);
    const cJSON *x;
    cJSON_ArrayForEach(x, it) {
      if(!cJSON_IsString(x) || !item_ok(x->valuestring)) {
        cJSON_Delete(root);
        snprintf(err, en, "Ein Eintrag ist weder eine .elf-Datei noch eine Pause (1 bis %d ms).", PP_DELAY_MAX);
        return -1;
      }
      snprintf(p->items[p->n++], PP_ITEM_MAX, "%s", x->valuestring);
    }
    out->n++;
  }
  if(cJSON_IsString(st) && st->valuestring[0]) {
    if(find_id(out, st->valuestring) < 0) { cJSON_Delete(root); snprintf(err, en, "Das Startprofil gibt es nicht."); return -1; }
    out->startup_set = 1;
    snprintf(out->startup, sizeof(out->startup), "%s", st->valuestring);
  }
  cJSON_Delete(root);
  return 0;
}

static cJSON *
doc_json(const pp_set_t *s) {
  cJSON *root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "startup", s->startup_set ? s->startup : "");
  cJSON *arr = cJSON_AddArrayToObject(root, "profiles");
  for(int i = 0; i < s->n; i++) {
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "id", s->p[i].id);
    cJSON_AddStringToObject(o, "name", s->p[i].name);
    cJSON *it = cJSON_AddArrayToObject(o, "items");
    for(int k = 0; k < s->p[i].n; k++) cJSON_AddItemToArray(it, cJSON_CreateString(s->p[i].items[k]));
    cJSON_AddItemToArray(arr, o);
  }
  return root;
}

static int
write_file(const char *txt) {
  char tmp[sizeof(PP_FILE) + 8];
  snprintf(tmp, sizeof(tmp), "%s.tmp", PP_FILE);
  int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if(fd < 0) return errno;
  size_t n = strlen(txt), off = 0;
  int eno = 0;
  while(off < n) {
    ssize_t k = write(fd, txt + off, n - off);
    if(k < 0 && errno == EINTR) continue;
    if(k <= 0) { eno = errno ? errno : EIO; break; }
    off += (size_t)k;
  }
  if(close(fd) != 0 && !eno) eno = errno;
  if(!eno && rename(tmp, PP_FILE) != 0) eno = errno;
  if(eno) unlink(tmp);
  return eno;
}

static void
load_locked(void) {
  if(g_loaded) return;
  g_loaded = 1;
  memset(&g_set, 0, sizeof(g_set));
  FILE *f = fopen(PP_FILE, "rb");
  if(!f) return;
  char *buf = malloc(256 * 1024);
  if(!buf) { fclose(f); return; }
  size_t n = fread(buf, 1, 256 * 1024 - 1, f);
  fclose(f);
  buf[n] = 0;
  char err[200];
  pp_set_t *s = malloc(sizeof(*s));                 /* 300 KB: never on a thread's stack */
  if(s && parse_doc(buf, s, err, sizeof(err)) == 0) g_set = *s;
  else if(s) PS5TM_WARN("payprof_load", "Die Profile-Datei ist unlesbar und wird ignoriert: %s", err);
  free(s);
  free(buf);
}


/* ------------------------------------------------------------ the API */

/* GET: the document, the files of the internal folder come with the page's own list. */
char *
ps5tm_payprof_get_json(void) {
  pthread_mutex_lock(&g_lock);
  load_locked();
  cJSON *root = doc_json(&g_set);
  pthread_mutex_unlock(&g_lock);
  cJSON_AddBoolToObject(root, "ok", 1);
  char *txt = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  return txt;
}

/* POST: replace the whole document. 0 = stored (answer: the stored document, malloc'd, in *out). */
int
ps5tm_payprof_save(const char *json, char **out, char *err, size_t en) {
  pp_set_t *s = malloc(sizeof(*s));                 /* 300 KB: never on a thread's stack */
  if(!s) { snprintf(err, en, "Kein Speicher."); return 503; }
  if(parse_doc(json, s, err, en) != 0) { free(s); return 400; }
  cJSON *doc = doc_json(s);
  char *txt = cJSON_PrintUnformatted(doc);
  cJSON_Delete(doc);
  if(!txt) { free(s); snprintf(err, en, "Kein Speicher."); return 503; }
  pthread_mutex_lock(&g_lock);
  int eno = write_file(txt);
  if(!eno) { g_set = *s; g_loaded = 1; }
  pthread_mutex_unlock(&g_lock);
  free(s);
  free(txt);
  if(eno) { snprintf(err, en, "Speichern ging nicht: %s", strerror(eno)); return 500; }
  *out = ps5tm_payprof_get_json();
  return 200;
}


/* ------------------------------------------------------------ running */

static void
run_mark_set(const char *name) {
  FILE *f = fopen(PP_MARK, "w");
  if(!f) return;
  fprintf(f, "%s\n", name);
  fclose(f);
}

static void
res_add(const char *item, int ok, const char *msg) {
  pthread_mutex_lock(&g_lock);
  if(g_run.nres < PP_MAX_ITEMS) {
    pp_step_t *r = &g_run.res[g_run.nres++];
    snprintf(r->item, sizeof(r->item), "%s", item);
    r->ok = ok;
    snprintf(r->msg, sizeof(r->msg), "%s", msg);
  }
  pthread_mutex_unlock(&g_lock);
}

static int
stop_wanted(void) {
  pthread_mutex_lock(&g_lock);
  int s = g_run.stop;
  pthread_mutex_unlock(&g_lock);
  return s;
}

static void *
run_thread(void *arg) {
  pp_profile_t *p = (pp_profile_t *)arg;
  PS5TM_INFO("payprof_run", "Payload-Profil „%.60s“ wird ausgeführt (%d Einträge).", p->name, p->n);
  run_mark_set(p->name);
  int stopped = 0, bad = 0;
  for(int i = 0; i < p->n && !stopped; i++) {
    pthread_mutex_lock(&g_lock);
    g_run.step = i + 1;
    pthread_mutex_unlock(&g_lock);
    const char *it = p->items[i];
    if(it[0] == '!') {
      long ms = atol(it + 1);
      for(long t = 0; t < ms; t += 100) {
        if(stop_wanted()) { stopped = 1; break; }
        usleep(100 * 1000);
      }
      if(!stopped) res_add(it, 1, "Pause");
      continue;
    }
    if(stop_wanted()) { stopped = 1; break; }
    ps5tm_payload_result_t r;
    int st = ps5tm_payload_start("internal", "", "", it, &r);
    if(st == 200) { res_add(it, 1, "gesendet"); }
    else { bad++; res_add(it, 0, r.msg[0] ? r.msg : "nicht gestartet"); PS5TM_WARN("payprof_item", "Profil: %.100s nicht gestartet: %.150s", it, r.msg); }
    usleep(400 * 1000);                 /* the loader takes one file at a time */
  }
  remove(PP_MARK);
  PS5TM_INFO("payprof_done", "Payload-Profil „%.60s“ %s (%d Fehler).", p->name, stopped ? "angehalten" : "fertig", bad);
  pthread_mutex_lock(&g_lock);
  g_run.running = 0;
  pthread_mutex_unlock(&g_lock);
  free(p);
  return NULL;
}

/* 200 started; else status + German reason. */
int
ps5tm_payprof_run(const char *id, int from_startup, char *err, size_t en) {
  pthread_mutex_lock(&g_lock);
  load_locked();
  int i = id ? find_id(&g_set, id) : -1;
  if(i < 0) { pthread_mutex_unlock(&g_lock); snprintf(err, en, "Dieses Profil gibt es nicht."); return 404; }
  if(g_run.running) { pthread_mutex_unlock(&g_lock); snprintf(err, en, "Es läuft schon ein Profil."); return 409; }
  if(g_set.p[i].n == 0) { pthread_mutex_unlock(&g_lock); snprintf(err, en, "Das Profil ist leer."); return 400; }
  pp_profile_t *copy = malloc(sizeof(*copy));
  if(!copy) { pthread_mutex_unlock(&g_lock); snprintf(err, en, "Kein Speicher."); return 503; }
  *copy = g_set.p[i];
  memset(&g_run, 0, sizeof(g_run));
  g_run.running = 1;
  g_run.total = copy->n;
  g_run.from_startup = from_startup;
  snprintf(g_run.id, sizeof(g_run.id), "%s", copy->id);
  snprintf(g_run.name, sizeof(g_run.name), "%s", copy->name);
  pthread_mutex_unlock(&g_lock);

  pthread_t th;
  if(pthread_create(&th, NULL, run_thread, copy) != 0) {
    free(copy);
    pthread_mutex_lock(&g_lock);
    g_run.running = 0;
    pthread_mutex_unlock(&g_lock);
    snprintf(err, en, "Der Ablauf ließ sich nicht starten.");
    return 503;
  }
  pthread_detach(th);
  return 200;
}

void
ps5tm_payprof_stop(void) {
  pthread_mutex_lock(&g_lock);
  if(g_run.running) g_run.stop = 1;
  pthread_mutex_unlock(&g_lock);
}

char *
ps5tm_payprof_status_json(void) {
  cJSON *root = cJSON_CreateObject();
  cJSON_AddBoolToObject(root, "ok", 1);
  pthread_mutex_lock(&g_lock);
  cJSON_AddBoolToObject(root, "running", g_run.running);
  cJSON_AddStringToObject(root, "id", g_run.id);
  cJSON_AddStringToObject(root, "name", g_run.name);
  cJSON_AddNumberToObject(root, "step", g_run.step);
  cJSON_AddNumberToObject(root, "total", g_run.total);
  cJSON_AddBoolToObject(root, "startup", g_run.from_startup);
  cJSON *a = cJSON_AddArrayToObject(root, "results");
  for(int i = 0; i < g_run.nres; i++) {
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "item", g_run.res[i].item);
    cJSON_AddBoolToObject(o, "ok", g_run.res[i].ok);
    cJSON_AddStringToObject(o, "msg", g_run.res[i].msg);
    cJSON_AddItemToArray(a, o);
  }
  pthread_mutex_unlock(&g_lock);
  char *txt = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  return txt;
}


/* ------------------------------------------------------------ startup */

static void *
startup_thread(void *arg) {
  (void)arg;
  /* A marker left by a run that never ended: do not run it again (boot-loop protection). */
  struct stat st;
  if(stat(PP_MARK, &st) == 0) {
    remove(PP_MARK);
    PS5TM_WARN("payprof_loop", "Das Startprofil wird diesmal nicht ausgeführt: Der letzte Ablauf wurde nicht beendet "
               "(ein Payload darin hat die App oder die Konsole neu gestartet).");
    return NULL;
  }
  for(unsigned w = 0; w < PP_STARTUP_MS; w += 500) usleep(500 * 1000);
  char id[PP_ID_MAX] = "";
  pthread_mutex_lock(&g_lock);
  load_locked();
  if(g_set.startup_set) snprintf(id, sizeof(id), "%s", g_set.startup);
  pthread_mutex_unlock(&g_lock);
  if(!id[0]) return NULL;
  char err[160];
  int r = ps5tm_payprof_run(id, 1, err, sizeof(err));
  if(r != 200) PS5TM_WARN("payprof_startup", "Das Startprofil lief nicht an: %s", err);
  return NULL;
}

void
ps5tm_payprof_start(void) {
  pthread_t th;
  if(pthread_create(&th, NULL, startup_thread, NULL) == 0) pthread_detach(th);
}
