/* Host test of src/origafter.c: what the app does with the ORIGINAL of a game after a conversion, a copy or an unpack (18.11.2026).
   The jobs (copy, delete, ShadowMountPlus, the library, the drives) are stubs; the files are real, below a folder of the test's own (OA_ROOT). */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "ps5tm.h"
#include "third_party/cJSON.h"

#define ROOT OA_ROOT

static int g_fail, g_n;
#define CHECK(c) do { g_n++; if(!(c)) { g_fail++; printf("FAIL line %d: %s\n", __LINE__, #c); } } while(0)

/* ---------------------------------------------------------------- the stubs */
#undef pthread_create
int ps5tm_thread_create(pthread_t *t, const pthread_attr_t *a, void *(*f)(void *), void *x) { return pthread_create(t, a, f, x); }
static int g_log_warn;
void ps5tm_log(const char *level, const char *code, const char *fmt, ...) { (void)fmt; (void)code; if(!strcmp(level, "WARN")) g_log_warn++; }
uint64_t ps5tm_mono_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}
static char g_notes[8][300];
static int g_nnotes;
void ps5tm_notify_plain(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  if(g_nnotes < 8) vsnprintf(g_notes[g_nnotes++], sizeof(g_notes[0]), fmt, ap);
  va_end(ap);
}
void ps5tm_powerguard_hold(void) {}
void ps5tm_powerguard_release(void) {}

/* the drives */
void ps5tm_sysinfo_get(ps5tm_sysinfo_t *o) {
  memset(o, 0, sizeof(*o));
  static const struct { const char *path, *label; } v[] = { { "/user", "" }, { "/mnt/usb0", "Stick" }, { "/mnt/usb1", "Platte" }, { "/mnt/ext0", "M2" }, { "/mnt/weird", "x" } };
  for(unsigned i = 0; i < sizeof(v) / sizeof(v[0]); i++) {
    snprintf(o->volumes[i].path, sizeof(o->volumes[i].path), "%s", v[i].path);
    snprintf(o->volumes[i].label, sizeof(o->volumes[i].label), "%s", v[i].label);
    o->volumes[i].free_bytes = 50ull << 30;
  }
  o->volume_count = sizeof(v) / sizeof(v[0]);
}

/* the library */
static char g_src_path[320] = "";
static char g_src_name[64] = "Testspiel";
int ps5tm_library_copy_info(const char *id, char *name, size_t nn, char *source, size_t sn) {
  if(strcmp(id, "PPSA00001") || !g_src_path[0]) return -1;
  snprintf(name, nn, "%s", g_src_name);
  snprintf(source, sn, "%s", g_src_path);
  return 0;
}
int ps5tm_library_delete_info(const char *id, ps5tm_libdel_t *o) {
  memset(o, 0, sizeof(*o));
  if(strcmp(id, "PPSA00001") || !g_src_path[0]) return -1;
  snprintf(o->name, sizeof(o->name), "%s", g_src_name);
  snprintf(o->path, sizeof(o->path), "%s", g_src_path);
  o->size = 1ll << 20;
  return 0;
}
int ps5tm_smp_find(const char *id, ps5tm_smp_game_t *o) { (void)id; (void)o; return -1; }
int ps5tm_gamecopy_measure(const char *path, uint64_t *bytes, uint64_t *largest) { (void)path; *bytes = 1ull << 20; *largest = 1ull << 19; return 0; }

/* the jobs */
static int g_idle_polls;                                    /* "busy" this many times before it is idle */
int ps5tm_jobs_busy_except(int self, int flags, char *why, size_t n) {
  (void)self; (void)flags;
  if(g_idle_polls > 0) { g_idle_polls--; snprintf(why, n, "Es läuft etwas"); return 1; }
  return 0;
}
int ps5tm_gameconvert_busy(void) { return 0; }
int ps5tm_gamecopy_busy(void) { return 0; }
int ps5tm_gamecopy_cancel(void) { return 0; }

static int g_del_busy_polls, g_del_calls, g_del_http = 200, g_del_fail;
static char g_del_path[320], g_del_id[16];
static void rm_tree(const char *p) {                          /* the stub's "deleting": a file or an empty folder */
  char b[700];
  snprintf(b, sizeof(b), "%s%s", ROOT, p);
  if(unlink(b) != 0) (void)rmdir(b);
}
int ps5tm_gamedelete_original_ok(const char *id, const char *path, char *why, size_t n) {
  (void)id;
  struct stat st;
  char b[700];
  snprintf(b, sizeof(b), "%s%s", ROOT, path);
  if(lstat(b, &st) != 0) { snprintf(why, n, "Das Original ist nicht mehr da."); return -1; }
  why[0] = 0;
  return 0;
}
int ps5tm_gamedelete_original(const char *id, const char *name, const char *path, const struct stat *was, char *err, size_t n) {
  (void)name; (void)was;
  err[0] = 0;
  if(g_del_http != 200) { snprintf(err, n, "Das Spiel läuft gerade. Bitte erst beenden."); return g_del_http; }
  g_del_calls++;
  snprintf(g_del_path, sizeof(g_del_path), "%s", path);
  snprintf(g_del_id, sizeof(g_del_id), "%s", id);
  g_del_busy_polls = 2;
  if(!g_del_fail) rm_tree(path);
  return 200;
}
int ps5tm_gamedelete_busy(void) { return g_del_busy_polls-- > 0; }
cJSON *ps5tm_gamedelete_status(void) {
  cJSON *o = cJSON_CreateObject();
  cJSON_AddStringToObject(o, "state", g_del_fail ? "failed" : "done");
  cJSON_AddStringToObject(o, "error", g_del_fail ? "Beim Löschen: Zugriff verweigert." : "");
  return o;
}

static int g_cp_calls, g_cp_http = 200, g_cp_fail, g_cp_make, g_cp_skipped, g_cp_badsource;   /* g_cp_make: the stub creates the copy (a folder) as the real one would */
static char g_cp_dest[320], g_cp_mount[48], g_cp_target[340], g_cp_source[320];
int ps5tm_gamecopy_start_to(const char *id, const char *mount, const char *dest, const char *source, char *err, size_t n) {
  (void)id;
  err[0] = 0;
  if(g_cp_http != 200) { snprintf(err, n, "Kopieren, Konvertieren und Verschieben laufen nicht gleichzeitig."); return g_cp_http; }
  g_cp_calls++;
  snprintf(g_cp_source, sizeof(g_cp_source), "%s", source);
  snprintf(g_cp_dest, sizeof(g_cp_dest), "%s", dest);
  snprintf(g_cp_mount, sizeof(g_cp_mount), "%s", mount);
  snprintf(g_cp_target, sizeof(g_cp_target), "%s/%s", dest, "PPSA00001");
  if(g_cp_make && !g_cp_fail) {
    char b[700];
    snprintf(b, sizeof(b), "%s%s", ROOT, dest);
    char cmd[800];
    snprintf(cmd, sizeof(cmd), "mkdir -p '%s/PPSA00001'", b);
    if(system(cmd) != 0) return 500;
  }
  return 200;
}
cJSON *ps5tm_gamecopy_status(void) {
  cJSON *o = cJSON_CreateObject();
  cJSON_AddStringToObject(o, "state", g_cp_fail ? "failed" : "done");
  cJSON_AddBoolToObject(o, "active", 0);
  cJSON_AddStringToObject(o, "target", g_cp_target);
  cJSON_AddNumberToObject(o, "total_bytes", 1000);
  cJSON_AddNumberToObject(o, "done_bytes", 1000);
  cJSON_AddStringToObject(o, "source", g_cp_badsource ? "/mnt/usb0/homebrew/ANDERES" : g_cp_source);
  cJSON_AddNumberToObject(o, "skipped", g_cp_skipped);
  if(g_cp_fail) cJSON_AddStringToObject(o, "error", "Auf dem Ziel ist kein Platz mehr.");
  return o;
}

/* ---------------------------------------------------------------- the test */
static void mk(const char *rel, int dir) {
  char b[700];
  snprintf(b, sizeof(b), "%s%s", ROOT, rel);
  char cmd[800];
  snprintf(cmd, sizeof(cmd), dir ? "mkdir -p '%s'" : "mkdir -p \"$(dirname '%s')\" && : > '%s'", b, b, b);
  if(system(cmd) != 0) { printf("setup failed: %s\n", cmd); exit(2); }
}
static int exists(const char *rel) {
  char b[700];
  struct stat st;
  snprintf(b, sizeof(b), "%s%s", ROOT, rel);
  return lstat(b, &st) == 0;
}

/* the state of the newest item for "kind" (a copy of its text), waiting up to ~4 s for it to leave "waiting"/"running" */
static char g_msg[400], g_state[16], g_mode[8], g_target[340];
static int wait_end(void) {
  for(int i = 0; i < 400; i++) {
    cJSON *o = ps5tm_orig_status_json();
    const cJSON *items = cJSON_GetObjectItem(o, "items");
    const cJSON *it = cJSON_GetArrayItem(items, 0);
    if(it) {
      const cJSON *s = cJSON_GetObjectItem(it, "state");
      snprintf(g_state, sizeof(g_state), "%s", s->valuestring);
      snprintf(g_msg, sizeof(g_msg), "%s", cJSON_GetObjectItem(it, "message")->valuestring);
      snprintf(g_mode, sizeof(g_mode), "%s", cJSON_GetObjectItem(it, "mode")->valuestring);
      snprintf(g_target, sizeof(g_target), "%s", cJSON_GetObjectItem(it, "target")->valuestring);
      if(strcmp(g_state, "waiting") && strcmp(g_state, "running")) { cJSON_Delete(o); return 1; }
    }
    cJSON_Delete(o);
    usleep(20000);
  }
  return 0;
}
static int n_items(void) {
  cJSON *o = ps5tm_orig_status_json();
  int n = cJSON_GetArraySize(cJSON_GetObjectItem(o, "items"));
  cJSON_Delete(o);
  return n;
}
static void reset_stubs(void) {
  g_del_calls = 0; g_del_http = 200; g_del_fail = 0; g_del_path[0] = 0; g_del_busy_polls = 0;
  g_cp_calls = 0; g_cp_http = 200; g_cp_fail = 0; g_cp_make = 0; g_cp_dest[0] = 0; g_cp_skipped = 0; g_cp_badsource = 0;
  g_idle_polls = 0; g_nnotes = 0; g_log_warn = 0;
}

int
main(void) {
  char cmd[800];
  snprintf(cmd, sizeof(cmd), "rm -rf '%s' && mkdir -p '%s/mnt/usb0' '%s/mnt/usb1' '%s/mnt/ext0' '%s/data'", ROOT, ROOT, ROOT, ROOT, ROOT);
  if(system(cmd) != 0) return 2;
  ps5tm_orig_req_t r;
  char err[400];

  /* ---- keep: nothing at all */
  snprintf(g_src_path, sizeof(g_src_path), "/mnt/usb0/homebrew/PPSA00001");
  mk(g_src_path, 1);
  CHECK(ps5tm_orig_prepare("convert", "PPSA00001", "", "", &r, err, sizeof(err)) == 0 && r.mode == PS5TM_ORIG_KEEP);
  CHECK(ps5tm_orig_prepare("convert", "PPSA00001", "keep", "", &r, err, sizeof(err)) == 0 && r.mode == PS5TM_ORIG_KEEP);
  CHECK(ps5tm_orig_arm(&r, "convert") == 0);                /* a no-op */
  ps5tm_orig_commit(&r, "convert", 1);
  ps5tm_orig_job_done("convert", "PPSA00001", 1, NULL);
  CHECK(n_items() == 0);
  CHECK(ps5tm_orig_prepare("convert", "PPSA00001", "bogus", "", &r, err, sizeof(err)) == 400);
  CHECK(ps5tm_orig_prepare("convert", "PPSA99999", "delete", "", &r, err, sizeof(err)) == 404);

  /* ---- delete after a conversion that ended well */
  reset_stubs();
  mk("/mnt/usb1/PPSA00001.exfat", 0);
  CHECK(ps5tm_orig_prepare("convert", "PPSA00001", "delete", "", &r, err, sizeof(err)) == 0 && r.mode == PS5TM_ORIG_DELETE);
  CHECK(!strcmp(r.path, "/mnt/usb0/homebrew/PPSA00001") && !strcmp(r.name, "Testspiel"));
  CHECK(ps5tm_orig_arm(&r, "convert") == 0);
  CHECK(n_items() == 1);
  ps5tm_orig_commit(&r, "convert", 1);
  g_idle_polls = 2;                                          /* the app's other jobs still run for a moment */
  ps5tm_orig_job_done("convert", "PPSA00001", 1, "/mnt/usb1/PPSA00001.exfat");
  CHECK(wait_end());
  CHECK(!strcmp(g_state, "done") && !strcmp(g_mode, "delete"));
  CHECK(strstr(g_msg, "Das Original „Testspiel“ wurde gelöscht.") != NULL);
  CHECK(g_del_calls == 1 && !strcmp(g_del_path, "/mnt/usb0/homebrew/PPSA00001") && !strcmp(g_del_id, "PPSA00001"));
  CHECK(!exists("/mnt/usb0/homebrew/PPSA00001"));
  CHECK(exists("/mnt/usb1/PPSA00001.exfat"));
  CHECK(g_nnotes == 1 && strstr(g_notes[0], "wurde gelöscht") != NULL);

  /* ---- the job ends before the choice was made firm (a job that finishes at once) */
  reset_stubs();
  mk(g_src_path, 1);
  CHECK(ps5tm_orig_prepare("copy", "PPSA00001", "delete", "", &r, err, sizeof(err)) == 0);
  CHECK(ps5tm_orig_arm(&r, "copy") == 0);
  ps5tm_orig_job_done("copy", "PPSA00001", 1, "/mnt/usb1/PPSA00001.exfat");
  ps5tm_orig_commit(&r, "copy", 1);
  CHECK(wait_end() && !strcmp(g_state, "done") && g_del_calls == 1);

  /* ---- a job that did not end well leaves the original alone */
  reset_stubs();
  mk(g_src_path, 1);
  CHECK(ps5tm_orig_prepare("copy", "PPSA00001", "delete", "", &r, err, sizeof(err)) == 0);
  CHECK(ps5tm_orig_arm(&r, "copy") == 0);
  ps5tm_orig_commit(&r, "copy", 1);
  ps5tm_orig_job_done("copy", "PPSA00001", 0, NULL);
  CHECK(wait_end() && !strcmp(g_state, "skipped"));
  CHECK(strstr(g_msg, "blieb liegen, weil der Vorgang nicht erfolgreich war") != NULL);
  CHECK(g_del_calls == 0 && exists("/mnt/usb0/homebrew/PPSA00001"));

  /* ---- a job that was refused: the note is dropped */
  reset_stubs();
  int before = n_items();
  CHECK(ps5tm_orig_prepare("unpack", "PPSA00001", "delete", "", &r, err, sizeof(err)) == 0);
  CHECK(ps5tm_orig_arm(&r, "unpack") == 0);
  CHECK(n_items() == before + 1);
  ps5tm_orig_commit(&r, "unpack", 0);
  CHECK(n_items() == before);
  ps5tm_orig_job_done("unpack", "PPSA00001", 1, NULL);        /* nobody is waiting for it */
  usleep(100000);
  CHECK(g_del_calls == 0);

  /* ---- the result is not there: nothing goes */
  reset_stubs();
  CHECK(ps5tm_orig_prepare("unpack", "PPSA00001", "delete", "", &r, err, sizeof(err)) == 0);
  CHECK(ps5tm_orig_arm(&r, "unpack") == 0);
  ps5tm_orig_commit(&r, "unpack", 1);
  ps5tm_orig_job_done("unpack", "PPSA00001", 1, "/mnt/usb1/gibt-es-nicht");
  CHECK(wait_end() && !strcmp(g_state, "failed") && strstr(g_msg, "Das Ergebnis des Vorgangs wurde nicht gefunden") != NULL);
  CHECK(g_del_calls == 0 && exists("/mnt/usb0/homebrew/PPSA00001"));

  /* ---- the result lies in the original (or is it): nothing goes */
  reset_stubs();
  mk(g_src_path, 1);
  CHECK(ps5tm_orig_prepare("copy", "PPSA00001", "delete", "", &r, err, sizeof(err)) == 0);
  CHECK(ps5tm_orig_arm(&r, "copy") == 0);
  ps5tm_orig_commit(&r, "copy", 1);
  ps5tm_orig_job_done("copy", "PPSA00001", 1, "/mnt/usb0/homebrew/PPSA00001/unterordner");
  CHECK(wait_end() && !strcmp(g_state, "failed") && strstr(g_msg, "liegen ineinander") != NULL);
  CHECK(g_del_calls == 0 && exists("/mnt/usb0/homebrew/PPSA00001"));
  CHECK(ps5tm_orig_prepare("copy", "PPSA00001", "delete", "", &r, err, sizeof(err)) == 0);
  CHECK(ps5tm_orig_arm(&r, "copy") == 0);
  ps5tm_orig_commit(&r, "copy", 1);
  ps5tm_orig_job_done("copy", "PPSA00001", 1, "/mnt/usb0/homebrew/PPSA00001");
  CHECK(wait_end() && !strcmp(g_state, "failed") && g_del_calls == 0);

  /* ---- the delete is refused (the game runs) */
  reset_stubs();
  g_del_http = 409;
  CHECK(ps5tm_orig_prepare("convert", "PPSA00001", "delete", "", &r, err, sizeof(err)) == 0);
  CHECK(ps5tm_orig_arm(&r, "convert") == 0);
  ps5tm_orig_commit(&r, "convert", 1);
  ps5tm_orig_job_done("convert", "PPSA00001", 1, "/mnt/usb1/PPSA00001.exfat");
  CHECK(wait_end() && !strcmp(g_state, "failed") && strstr(g_msg, "blieb liegen: Das Spiel läuft gerade") != NULL);
  CHECK(exists("/mnt/usb0/homebrew/PPSA00001"));

  /* ---- the delete job itself fails */
  reset_stubs();
  g_del_fail = 1;
  CHECK(ps5tm_orig_prepare("convert", "PPSA00001", "delete", "", &r, err, sizeof(err)) == 0);
  CHECK(ps5tm_orig_arm(&r, "convert") == 0);
  ps5tm_orig_commit(&r, "convert", 1);
  ps5tm_orig_job_done("convert", "PPSA00001", 1, "/mnt/usb1/PPSA00001.exfat");
  CHECK(wait_end() && !strcmp(g_state, "failed") && strstr(g_msg, "Zugriff verweigert") != NULL && g_log_warn > 0);
  CHECK(exists("/mnt/usb0/homebrew/PPSA00001"));

  /* ---- moving: where it may go */
  reset_stubs();
  mk(g_src_path, 1);
  CHECK(ps5tm_orig_prepare("convert", "PPSA00001", "move", "/mnt/usb1/PS5-Sicherung/Spiele", &r, err, sizeof(err)) == 0 && r.mode == PS5TM_ORIG_MOVE);

  CHECK(!strcmp(r.dest, "/mnt/usb1/PS5-Sicherung/Spiele"));
  CHECK(ps5tm_orig_prepare("convert", "PPSA00001", "move", "/mnt/usb1//PS5-Sicherung/Spiele/", &r, err, sizeof(err)) == 0 && !strcmp(r.dest, "/mnt/usb1/PS5-Sicherung/Spiele"));
  CHECK(ps5tm_orig_prepare("convert", "PPSA00001", "move", "/mnt/usb1/homebrew", &r, err, sizeof(err)) == 409 && strstr(err, "ShadowMountPlus") != NULL);
  CHECK(ps5tm_orig_prepare("convert", "PPSA00001", "move", "/mnt/usb1/Spiele", &r, err, sizeof(err)) == 409 && strstr(err, "ShadowMountPlus") != NULL);   /* two levels: searched */
  CHECK(ps5tm_orig_prepare("convert", "PPSA00001", "move", "/mnt/usb1", &r, err, sizeof(err)) == 409);                                              /* the root of a drive */
  CHECK(ps5tm_orig_prepare("convert", "PPSA00001", "move", "/mnt/usb1/etaHEN/games", &r, err, sizeof(err)) == 409);
  CHECK(ps5tm_orig_prepare("convert", "PPSA00001", "move", "/data/PS5-Sicherung/Spiele", &r, err, sizeof(err)) == 0);                              /* the internal SSD */
  CHECK(ps5tm_orig_prepare("convert", "PPSA00001", "move", "/data/homebrew", &r, err, sizeof(err)) == 409);
  CHECK(ps5tm_orig_prepare("convert", "PPSA00001", "move", "/data/PS5-Cooling-Center/x", &r, err, sizeof(err)) == 409);
  CHECK(ps5tm_orig_prepare("convert", "PPSA00001", "move", "/mnt/usb0/homebrew/PPSA00001/innen", &r, err, sizeof(err)) == 409);                    /* inside the original */
  CHECK(ps5tm_orig_prepare("convert", "PPSA00001", "move", "/mnt/usb0/homebrew", &r, err, sizeof(err)) == 409);                                     /* where it lies already */
  CHECK(ps5tm_orig_prepare("convert", "PPSA00001", "move", "/mnt/nirgends/PS5-Sicherung/Spiele", &r, err, sizeof(err)) == 409);                     /* no drive */
  CHECK(ps5tm_orig_prepare("convert", "PPSA00001", "move", "/mnt/weird/PS5-Sicherung/Spiele", &r, err, sizeof(err)) == 409);                        /* a mount that is no drive of ours */
  CHECK(ps5tm_orig_prepare("convert", "PPSA00001", "move", "mnt/usb1/x", &r, err, sizeof(err)) == 409);                                             /* not absolute */
  CHECK(ps5tm_orig_prepare("convert", "PPSA00001", "move", "/mnt/usb1/a/../../usb0/x", &r, err, sizeof(err)) == 409);                               /* no ".." */
  CHECK(ps5tm_orig_prepare("convert", "PPSA00001", "move", "", &r, err, sizeof(err)) == 409);
  mk("/mnt/usb1/PS5-Sicherung/Spiele/PPSA00001", 1);
  CHECK(ps5tm_orig_prepare("convert", "PPSA00001", "move", "/mnt/usb1/PS5-Sicherung/Spiele", &r, err, sizeof(err)) == 409 && strstr(err, "schon") != NULL);   /* taken */
  CHECK(system("rm -rf '" ROOT "/mnt/usb1/PS5-Sicherung'") == 0);
  CHECK(system("ln -s /tmp '" ROOT "/mnt/usb1/PS5-Sicherung'") == 0);
  CHECK(ps5tm_orig_prepare("convert", "PPSA00001", "move", "/mnt/usb1/PS5-Sicherung/Spiele", &r, err, sizeof(err)) == 409 && strstr(err, "Verknüpfung") != NULL);   /* a link on the way */
  CHECK(system("rm -f '" ROOT "/mnt/usb1/PS5-Sicherung'") == 0);

  /* ---- moving: no room (an image of 8 TiB, sparse) */
  {
    char sp[800];
    snprintf(sp, sizeof(sp), "%s/mnt/usb0/gross.exfat", ROOT);
    int fd = open(sp, O_CREAT | O_WRONLY, 0644);
    CHECK(fd >= 0 && ftruncate(fd, (off_t)8 << 40) == 0);
    if(fd >= 0) close(fd);
    snprintf(g_src_path, sizeof(g_src_path), "/mnt/usb0/gross.exfat");
    CHECK(ps5tm_orig_prepare("convert", "PPSA00001", "move", "/mnt/usb1/PS5-Sicherung/Spiele", &r, err, sizeof(err)) == 409 && strstr(err, "nicht genug Platz") != NULL);
    unlink(sp);
    snprintf(g_src_path, sizeof(g_src_path), "/mnt/usb0/homebrew/PPSA00001");
  }

  /* ---- moving: copied, read back, then the original goes */
  reset_stubs();
  mk(g_src_path, 1);
  g_cp_make = 1;
  CHECK(ps5tm_orig_prepare("convert", "PPSA00001", "move", "/mnt/usb1/PS5-Sicherung/Spiele", &r, err, sizeof(err)) == 0);
  CHECK(ps5tm_orig_arm(&r, "convert") == 0);
  ps5tm_orig_commit(&r, "convert", 1);
  mk("/mnt/usb1/PPSA00001.exfat", 0);
  ps5tm_orig_job_done("convert", "PPSA00001", 1, "/mnt/usb1/PPSA00001.exfat");
  CHECK(wait_end());
  CHECK(!strcmp(g_state, "done") && !strcmp(g_mode, "move"));
  CHECK(strstr(g_msg, "liegt jetzt in /mnt/usb1/PS5-Sicherung/Spiele") != NULL);
  CHECK(!strcmp(g_target, "/mnt/usb1/PS5-Sicherung/Spiele/PPSA00001"));
  CHECK(g_cp_calls == 1 && !strcmp(g_cp_dest, "/mnt/usb1/PS5-Sicherung/Spiele") && !strcmp(g_cp_mount, "/mnt/usb1"));
  CHECK(g_del_calls == 1 && !strcmp(g_del_path, "/mnt/usb0/homebrew/PPSA00001"));
  CHECK(!exists("/mnt/usb0/homebrew/PPSA00001") && exists("/mnt/usb1/PS5-Sicherung/Spiele/PPSA00001"));

  /* ---- moving: the copy fails, the original stays */
  reset_stubs();
  CHECK(system("rm -rf '" ROOT "/mnt/usb1/PS5-Sicherung'") == 0);
  mk(g_src_path, 1);
  g_cp_fail = 1;
  CHECK(ps5tm_orig_prepare("convert", "PPSA00001", "move", "/mnt/usb1/PS5-Sicherung/Spiele", &r, err, sizeof(err)) == 0);
  CHECK(ps5tm_orig_arm(&r, "convert") == 0);
  ps5tm_orig_commit(&r, "convert", 1);
  ps5tm_orig_job_done("convert", "PPSA00001", 1, "/mnt/usb1/PPSA00001.exfat");
  CHECK(wait_end() && !strcmp(g_state, "failed") && strstr(g_msg, "kein Platz mehr") != NULL && strstr(g_msg, "blieb liegen") != NULL);
  CHECK(g_cp_calls == 1 && g_del_calls == 0 && exists("/mnt/usb0/homebrew/PPSA00001"));

  /* ---- moving: the copy said "done" but is not there */
  reset_stubs();
  CHECK(ps5tm_orig_prepare("convert", "PPSA00001", "move", "/mnt/usb1/PS5-Sicherung/Spiele", &r, err, sizeof(err)) == 0);
  CHECK(ps5tm_orig_arm(&r, "convert") == 0);
  ps5tm_orig_commit(&r, "convert", 1);
  ps5tm_orig_job_done("convert", "PPSA00001", 1, "/mnt/usb1/PPSA00001.exfat");
  CHECK(wait_end() && !strcmp(g_state, "failed") && strstr(g_msg, "Die Kopie wurde nicht gefunden") != NULL);
  CHECK(g_del_calls == 0 && exists("/mnt/usb0/homebrew/PPSA00001"));

  /* ---- the copy was made of another source than the original: stopped, nothing deleted */
  reset_stubs();
  CHECK(system("rm -rf '" ROOT "/mnt/usb1/PS5-Sicherung'") == 0);
  mk(g_src_path, 1);
  g_cp_make = 1;
  g_cp_badsource = 1;
  CHECK(ps5tm_orig_prepare("convert", "PPSA00001", "move", "/mnt/usb1/PS5-Sicherung/Spiele", &r, err, sizeof(err)) == 0);
  CHECK(ps5tm_orig_arm(&r, "convert") == 0);
  ps5tm_orig_commit(&r, "convert", 1);
  ps5tm_orig_job_done("convert", "PPSA00001", 1, "/mnt/usb1/PPSA00001.exfat");
  CHECK(wait_end() && !strcmp(g_state, "failed") && strstr(g_msg, "liegt inzwischen woanders") != NULL);
  CHECK(!strcmp(g_cp_source, "/mnt/usb0/homebrew/PPSA00001") && g_del_calls == 0 && exists("/mnt/usb0/homebrew/PPSA00001"));

  /* ---- the copy left out links or special files: it is not the whole original */
  reset_stubs();
  CHECK(system("rm -rf '" ROOT "/mnt/usb1/PS5-Sicherung'") == 0);
  g_cp_make = 1;
  g_cp_skipped = 2;
  CHECK(ps5tm_orig_prepare("convert", "PPSA00001", "move", "/mnt/usb1/PS5-Sicherung/Spiele", &r, err, sizeof(err)) == 0);
  CHECK(ps5tm_orig_arm(&r, "convert") == 0);
  ps5tm_orig_commit(&r, "convert", 1);
  ps5tm_orig_job_done("convert", "PPSA00001", 1, "/mnt/usb1/PPSA00001.exfat");
  CHECK(wait_end() && !strcmp(g_state, "failed") && strstr(g_msg, "Verknüpfungen oder Sonderdateien") != NULL);
  CHECK(g_del_calls == 0 && exists("/mnt/usb0/homebrew/PPSA00001"));

  /* ---- a result that was only spot-checked ("Schnell", ok 2): enough for a move, not for a delete */
  reset_stubs();
  CHECK(ps5tm_orig_prepare("convert", "PPSA00001", "delete", "", &r, err, sizeof(err)) == 0);
  CHECK(ps5tm_orig_arm(&r, "convert") == 0);
  ps5tm_orig_commit(&r, "convert", 1);
  ps5tm_orig_job_done("convert", "PPSA00001", 2, "/mnt/usb1/PPSA00001.exfat");
  CHECK(wait_end() && !strcmp(g_state, "skipped") && strstr(g_msg, "nur schnell geprüft") != NULL);
  CHECK(g_del_calls == 0 && exists("/mnt/usb0/homebrew/PPSA00001"));
  reset_stubs();
  CHECK(system("rm -rf '" ROOT "/mnt/usb1/PS5-Sicherung'") == 0);
  g_cp_make = 1;
  CHECK(ps5tm_orig_prepare("convert", "PPSA00001", "move", "/mnt/usb1/PS5-Sicherung/Spiele", &r, err, sizeof(err)) == 0);
  CHECK(ps5tm_orig_arm(&r, "convert") == 0);
  ps5tm_orig_commit(&r, "convert", 1);
  ps5tm_orig_job_done("convert", "PPSA00001", 2, "/mnt/usb1/PPSA00001.exfat");
  CHECK(wait_end() && !strcmp(g_state, "done") && g_del_calls == 1);
  mk(g_src_path, 1);

  /* ---- no result named: nothing goes */
  reset_stubs();
  CHECK(ps5tm_orig_prepare("copy", "PPSA00001", "delete", "", &r, err, sizeof(err)) == 0);
  CHECK(ps5tm_orig_arm(&r, "copy") == 0);
  ps5tm_orig_commit(&r, "copy", 1);
  ps5tm_orig_job_done("copy", "PPSA00001", 1, NULL);
  CHECK(wait_end() && !strcmp(g_state, "failed") && strstr(g_msg, "Das Ergebnis ist nicht bekannt") != NULL && g_del_calls == 0);

  /* ---- an unpacked folder must hold the game (sce_sys/param.json), whatever ShadowMountPlus said */
  reset_stubs();
  mk("/mnt/usb0/homebrew/PPSA00001-entpackt", 1);
  CHECK(ps5tm_orig_prepare("unpack", "PPSA00001", "delete", "", &r, err, sizeof(err)) == 0);
  CHECK(ps5tm_orig_arm(&r, "unpack") == 0);
  ps5tm_orig_commit(&r, "unpack", 1);
  ps5tm_orig_job_done("unpack", "PPSA00001", 1, "/mnt/usb0/homebrew/PPSA00001-entpackt");
  CHECK(wait_end() && !strcmp(g_state, "failed") && strstr(g_msg, "unvollständig") != NULL && g_del_calls == 0);
  mk("/mnt/usb0/homebrew/PPSA00001-entpackt/sce_sys/param.json", 0);
  CHECK(ps5tm_orig_prepare("unpack", "PPSA00001", "delete", "", &r, err, sizeof(err)) == 0);
  CHECK(ps5tm_orig_arm(&r, "unpack") == 0);
  ps5tm_orig_commit(&r, "unpack", 1);
  ps5tm_orig_job_done("unpack", "PPSA00001", 1, "/mnt/usb0/homebrew/PPSA00001-entpackt");
  CHECK(wait_end() && !strcmp(g_state, "done") && g_del_calls == 1);
  mk(g_src_path, 1);

  /* ---- a second choice for the same job is refused, not put in place of the first */
  reset_stubs();
  CHECK(system("rm -rf '" ROOT "/mnt/usb1/PS5-Sicherung'") == 0);
  CHECK(ps5tm_orig_prepare("convert", "PPSA00001", "delete", "", &r, err, sizeof(err)) == 0);
  CHECK(ps5tm_orig_arm(&r, "convert") == 0);
  ps5tm_orig_commit(&r, "convert", 1);
  ps5tm_orig_req_t r2;
  CHECK(ps5tm_orig_prepare("convert", "PPSA00001", "move", "/mnt/usb1/PS5-Sicherung/Spiele", &r2, err, sizeof(err)) == 0);
  CHECK(ps5tm_orig_arm(&r2, "convert") == -2);
  ps5tm_orig_job_done("convert", "PPSA00001", 1, "/mnt/usb1/PPSA00001.exfat");
  CHECK(wait_end() && !strcmp(g_state, "done") && !strcmp(g_mode, "delete") && g_del_calls == 1 && g_cp_calls == 0);
  mk(g_src_path, 1);

  /* ---- where ShadowMountPlus looks, whatever the case (exFAT and FAT drives do not tell them apart); the console's own folders */
  CHECK(ps5tm_orig_prepare("convert", "PPSA00001", "move", "/mnt/usb1/HomeBrew", &r, err, sizeof(err)) == 409 && strstr(err, "ShadowMountPlus") != NULL);
  CHECK(ps5tm_orig_prepare("convert", "PPSA00001", "move", "/mnt/usb1/ETAHEN/Games/x", &r, err, sizeof(err)) == 409);
  CHECK(ps5tm_orig_prepare("convert", "PPSA00001", "move", "/mnt/usb0/HOMEBREW/PPSA00001", &r, err, sizeof(err)) == 409);                           /* the original itself, in capitals */
  CHECK(ps5tm_orig_prepare("convert", "PPSA00001", "move", "/mnt/ext0/user/app/x", &r, err, sizeof(err)) == 409 && strstr(err, "Systemordner") != NULL);
  CHECK(ps5tm_orig_prepare("convert", "PPSA00001", "move", "/data/pkg/x", &r, err, sizeof(err)) == 409);
  CHECK(ps5tm_orig_prepare("convert", "PPSA00001", "move", "/mnt/ext0/PS5-Sicherung/Spiele", &r, err, sizeof(err)) == 0);                        /* the M.2 drive's own backup folder is fine */

  /* ---- the older field "delete_source" is the unpack's alone */
  {
    cJSON *b = cJSON_Parse("{\"delete_source\":true}");
    CHECK(ps5tm_orig_prepare_body("copy", "PPSA00001", b, &r, err, sizeof(err)) == 0 && r.mode == PS5TM_ORIG_KEEP);
    CHECK(ps5tm_orig_prepare_body("convert", "PPSA00001", b, &r, err, sizeof(err)) == 0 && r.mode == PS5TM_ORIG_KEEP);
    cJSON_Delete(b);
  }

  /* ---- the gate: nothing waits when no choice is open; a waiting one is awaited */
  reset_stubs();
  ps5tm_orig_gate(2);

  /* ---- the plan and the check */
  {
    int http = 0;
    cJSON *p = ps5tm_orig_plan_json("PPSA00001", &http);
    CHECK(p && http == 200);
    CHECK(cJSON_IsTrue(cJSON_GetObjectItem(p, "can_delete")) && cJSON_IsTrue(cJSON_GetObjectItem(p, "can_move")));
    CHECK(!strcmp(cJSON_GetObjectItem(cJSON_GetObjectItem(p, "source"), "kind")->valuestring, "folder"));
    const cJSON *dr = cJSON_GetObjectItem(p, "drives");
    CHECK(cJSON_GetArraySize(dr) == 4);                        /* /data, usb0, usb1, ext0 (not /mnt/weird) */
    CHECK(!strcmp(cJSON_GetObjectItem(cJSON_GetArrayItem(dr, 0), "mount")->valuestring, "/data"));
    CHECK(!strcmp(cJSON_GetObjectItem(cJSON_GetArrayItem(dr, 0), "kind")->valuestring, "internal"));
    CHECK(!strcmp(cJSON_GetObjectItem(cJSON_GetArrayItem(dr, 3), "kind")->valuestring, "m2"));
    CHECK(!strcmp(cJSON_GetObjectItem(cJSON_GetArrayItem(dr, 1), "default_dest")->valuestring, "/mnt/usb0/PS5-Sicherung/Spiele"));
    cJSON_Delete(p);
    CHECK(ps5tm_orig_plan_json("PPSA99999", &http) == NULL && http == 404);

    CHECK(system("rm -rf '" ROOT "/mnt/usb1/PS5-Sicherung'") == 0);
    cJSON *c = ps5tm_orig_check_json("PPSA00001", "/mnt/usb1/PS5-Sicherung/Spiele", &http);
    CHECK(c && !strcmp(cJSON_GetObjectItem(c, "level")->valuestring, "ok") && cJSON_IsTrue(cJSON_GetObjectItem(c, "can_move")));
    CHECK(!strcmp(cJSON_GetObjectItem(c, "target")->valuestring, "/mnt/usb1/PS5-Sicherung/Spiele/PPSA00001"));
    cJSON_Delete(c);
    c = ps5tm_orig_check_json("PPSA00001", "/mnt/usb1/homebrew", &http);
    CHECK(c && !strcmp(cJSON_GetObjectItem(c, "level")->valuestring, "error") && cJSON_IsTrue(cJSON_GetObjectItem(c, "smp_searches")));
    cJSON_Delete(c);
    c = ps5tm_orig_check_json("PPSA00001", "", &http);
    CHECK(c && !strcmp(cJSON_GetObjectItem(c, "level")->valuestring, "error"));
    cJSON_Delete(c);
  }

  /* ---- the older field of the unpack: delete_source = delete */
  {
    cJSON *b = cJSON_Parse("{\"id\":\"PPSA00001\",\"delete_source\":true}");
    CHECK(ps5tm_orig_prepare_body("unpack", "PPSA00001", b, &r, err, sizeof(err)) == 0 && r.mode == PS5TM_ORIG_DELETE);
    cJSON_Delete(b);
    b = cJSON_Parse("{\"orig_after\":\"move\",\"orig_dest\":\"/mnt/usb0/PS5-Sicherung/Spiele\"}");
    CHECK(ps5tm_orig_prepare_body("unpack", "PPSA00001", b, &r, err, sizeof(err)) == 0 && r.mode == PS5TM_ORIG_MOVE);
    cJSON_Delete(b);
    b = cJSON_Parse("{\"id\":\"PPSA00001\"}");
    CHECK(ps5tm_orig_prepare_body("unpack", "PPSA00001", b, &r, err, sizeof(err)) == 0 && r.mode == PS5TM_ORIG_KEEP);
    cJSON_Delete(b);
  }

  printf("%d Prüfungen, %d Fehler\n", g_n, g_fail);
  return g_fail ? 1 : 0;
}
