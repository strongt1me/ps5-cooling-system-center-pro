/* Moving a game, and unpacking an image into a folder (1.46.0).
 *
 * Both are done by ShadowMountPlus itself (smp.c). It knows which title lives
 * where and what is mounted, and a move behind its back would leave its
 * registration pointing at nothing until its next removal pass. The app
 * plans — which folders there are to go to, and how much room they have —
 * and follows the job; ShadowMountPlus copies, deletes and registers.
 *
 * The destinations are ShadowMountPlus's own scan roots on each drive the
 * console has: homebrew and etaHEN/games, and on USB and M.2 drives the drive
 * root, which it scans as well (README, 29.09.2026). A moved game therefore
 * stays playable. A missing folder is created — an empty folder, nothing
 * more — and only folders from that list are ever handed on. The start checks
 * again what the plan checked (a name already there, the room, FAT32), and
 * does not start beside a copy or a conversion of the app's own: one file job
 * at a time.
 *
 * Moving deletes the source once the copy is complete; that is what the word
 * means, and the page asks first. Unpacking keeps the image unless asked to
 * delete it, and ShadowMountPlus deletes it only after a successful unpack;
 * the folder it unpacks into is <destination>/<title id>. */

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <unistd.h>
#ifndef PS5TM_HOST_TEST
#include <sys/mount.h>
#endif

#include "ps5tm.h"
#include "third_party/cJSON.h"
#include "unpackfb.h"

#define SPACE_RESERVE    (256ull * 1024 * 1024)
#define FAT32_MAX_FILE   0xFFFFFFFFull
#define MAX_DESTS        40
#define TILE_WAIT_S      600          /* how long a refused job waits for the app's own tile to close */

typedef struct {
  char     path[96];
  char     drive[40];
  char     fs[16];
  uint64_t free_bytes;
  int      same_device;       /* a move there is a rename, it needs no room */
  int      exists;
} dest_t;

static int
op_ok(const char *op) {
  return op && (!strcmp(op, "move") || !strcmp(op, "unpack"));
}

static const char *
op_capability(const char *op) {
  return !strcmp(op, "move") ? "move_game_source" : "unpack_game_image";
}

static int
mkdir_p(const char *path) {
  char tmp[256];
  snprintf(tmp, sizeof(tmp), "%s", path);
  for(char *p = tmp + 1; *p; p++) {
    if(*p != '/') continue;
    *p = 0;
    if(mkdir(tmp, 0777) != 0 && errno != EEXIST) return -1;
    *p = '/';
  }
  return (mkdir(tmp, 0777) == 0 || errno == EEXIST) ? 0 : -1;
}

/* The folder the source lies in. */
static void
parent_dir(const char *path, char *out, size_t out_len) {
  snprintf(out, out_len, "%s", path);
  char *sl = strrchr(out, '/');
  if(sl && sl != out) *sl = 0;
}

/* Every scan root on every drive, with its room and filesystem. */
static int
list_dests(const char *source, dest_t *out, int max) {
  ps5tm_sysinfo_t info;
  ps5tm_sysinfo_get(&info);
  int n = 0;
  for(unsigned i = 0; i < info.volume_count && n < max; i++) {
    const char *mount = info.volumes[i].path;
    int internal = !strcmp(mount, "/user");
    const char *base = internal ? "/data" : mount;

    struct statvfs sv;
    if(statvfs(base, &sv) != 0) continue;
    uint64_t fr    = sv.f_frsize ? sv.f_frsize : sv.f_bsize;
    uint64_t avail = (uint64_t)sv.f_bavail * fr;
    char fs[16] = "";
#ifndef PS5TM_HOST_TEST
    struct statfs sf;
    if(statfs(base, &sf) == 0) snprintf(fs, sizeof(fs), "%s", sf.f_fstypename);
#endif
    /* Judged by the path, not by device numbers. On a USB or M.2 drive the
       scan roots are plain folders of one filesystem: a move between them is
       a rename. On the internal SSD /data/homebrew and /data/etaHEN/games are
       nullfs mounts of their own, and a rename across two mounts fails with
       EXDEV — ShadowMountPlus then copies and deletes, and that needs the
       room, so the internal drive never counts as "the same device". */
    size_t bl   = strlen(base);
    int    same = !internal && !strncmp(source, base, bl) && source[bl] == '/';

    static const char *const subs[] = { "/homebrew", "/etaHEN/games", "" };
    for(int k = 0; k < 3 && n < max; k++) {
      if(internal && !subs[k][0]) continue;       /* /data is no scan root */
      dest_t *d = &out[n];
      memset(d, 0, sizeof(*d));
      snprintf(d->path, sizeof(d->path), "%s%s", base, subs[k]);
      snprintf(d->drive, sizeof(d->drive), "%s", info.volumes[i].label);
      snprintf(d->fs, sizeof(d->fs), "%s", fs);
      d->free_bytes = avail;
      struct stat st;
      d->exists      = stat(d->path, &st) == 0 && S_ISDIR(st.st_mode);
      d->same_device = same;
      n++;
    }
  }
  return n;
}

static int
game_running(const char *title_id) {
  ps5tm_gamestate_t gs;
  ps5tm_gamestate_get(&gs);
  return gs.title_id[0] && !strcmp(gs.title_id, title_id);
}

/* The title as ShadowMountPlus manages it, checked for the operation. */
static int
check_title(const char *title_id, const char *op, ps5tm_smp_game_t *s,
            char *err, size_t err_len) {
  if(!op_ok(op)) {
    snprintf(err, err_len, "Unbekannter Auftrag.");
    return 400;
  }
  if(!ps5tm_smp_can(op_capability(op))) {
    snprintf(err, err_len, "Dafür braucht es ShadowMountPlus 1.7 mit seiner "
             "Schnittstelle, und die antwortet gerade nicht.");
    return 503;
  }
  if(!title_id || ps5tm_smp_find(title_id, s) != 0) {
    snprintf(err, err_len, "ShadowMountPlus verwaltet dieses Spiel nicht.");
    return 404;
  }
  if(!s->available) {
    snprintf(err, err_len, "Die Spieldaten sind gerade nicht erreichbar.");
    return 409;
  }
  if(!strcmp(s->source_type, "pkg")) {         /* ShadowMountPlus 1.7-beta4+ lists installed packages too; it refuses both jobs */
    snprintf(err, err_len, "Das Spiel ist als Paket installiert. ShadowMountPlus verschiebt und entpackt nur Spiele, die als "
             "Ordner oder Abbild vorliegen.");
    return 409;
  }
  if(!strcmp(op, "unpack") && strcmp(s->source_type, "image")) {
    snprintf(err, err_len, "Das Spiel liegt schon als Ordner vor.");
    return 409;
  }
  if(s->mounted) {
    snprintf(err, err_len, "Das Spiel ist gerade eingehängt. Bitte erst "
             "beenden.");
    return 409;
  }
  return 200;
}

cJSON *
ps5tm_gamemove_plan(const char *title_id, const char *op, char *err, size_t err_len) {
  ps5tm_smp_game_t s;
  if(check_title(title_id, op, &s, err, err_len) != 200) return NULL;

  uint64_t size = 0, largest = 0;
  if(ps5tm_gamecopy_measure(s.path, &size, &largest) != 0) {
    snprintf(err, err_len, "Die Spieldaten sind nicht lesbar.");
    return NULL;
  }
  char here[256];
  parent_dir(s.path, here, sizeof(here));
  int unpack = !strcmp(op, "unpack");
  /* The name the result gets in the destination: ShadowMountPlus keeps the
     source's own for a move and makes <title id> for an unpack. */
  const char *leaf = strrchr(s.path, '/');
  leaf = leaf ? leaf + 1 : s.path;
  if(unpack) leaf = title_id;

  dest_t d[MAX_DESTS];
  int n = list_dests(s.path, d, MAX_DESTS);

  cJSON *root = cJSON_CreateObject();
  cJSON_AddBoolToObject  (root, "ok", 1);
  cJSON_AddStringToObject(root, "op", op);
  cJSON_AddStringToObject(root, "title_id", title_id);
  cJSON_AddStringToObject(root, "source", s.path);
  cJSON_AddStringToObject(root, "source_dir", here);
  cJSON_AddStringToObject(root, "source_type", s.source_type);
  cJSON_AddStringToObject(root, "image_type", s.image_type);
  cJSON_AddNumberToObject(root, "size_bytes", (double)size);
  cJSON_AddBoolToObject  (root, "running", game_running(title_id));

  cJSON *arr = cJSON_AddArrayToObject(root, "destinations");
  for(int i = 0; i < n; i++) {
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "path", d[i].path);
    cJSON_AddStringToObject(o, "drive", d[i].drive);
    cJSON_AddStringToObject(o, "fs", d[i].fs);
    cJSON_AddNumberToObject(o, "free_bytes", (double)d[i].free_bytes);
    cJSON_AddBoolToObject  (o, "exists", d[i].exists);
    cJSON_AddBoolToObject  (o, "here", !strcmp(d[i].path, here));
    /* A move within one drive is a rename; everything else needs the room.
       Unpacked, a compressed image grows, so its size is only a floor. */
    int needs = unpack || !d[i].same_device;
    cJSON_AddBoolToObject  (o, "enough_space",
                            !needs || d[i].free_bytes >= size + SPACE_RESERVE);
    cJSON_AddBoolToObject  (o, "fat32_too_big",
                            !strcmp(d[i].fs, "msdosfs") && largest > FAT32_MAX_FILE);
    /* Where the result would lie, and whether something is there already: the
       start refuses it (409), so the page can say so beforehand. Given for a
       move as well as for an unpack. */
    if(leaf[0]) {
      char target[512];
      struct stat st;
      snprintf(target, sizeof(target), "%s/%s", d[i].path, leaf);
      cJSON_AddStringToObject(o, "target", target);
      cJSON_AddBoolToObject  (o, "target_exists", lstat(target, &st) == 0);
    }
    cJSON_AddItemToArray(arr, o);
  }
  return root;
}

/* ---------------------------------------------------------------- the app's own tile in the way

   ShadowMountPlus up to 1.7beta5fix1 accepts a move, then ends it at once with "game_active" (or "runtime_prepared")
   while any title it mounted runs - the app's own full-screen tile included, which it mounts from /data/homebrew (console
   test 28.10.2026, the same lock as for a delete, gamedelete.c). A watcher follows every job this app starts: when it
   ends for that reason and only the app's tile runs, it waits until the tile has closed (the full-screen app closes itself
   when the status says "waiting_for":"tile"), starts the same job again and reports its end as a notification. */

typedef struct {
  char   title_id[12], op[8], dest[256], name[128];
  int    delete_source;
  double job_id;
  unsigned gen;            /* g_wait.gen of the start of this watcher */
  uint64_t started_ms;     /* when the app started the job (ps5tm_mono_ms): "Original danach" (origafter.c) takes only the end of a job that began after the choice */
} watch_t;

static pthread_mutex_t g_wlock = PTHREAD_MUTEX_INITIALIZER;
static struct {
  int      on, watching;
  double   watch_job;
  char     title_id[12], op[8], tile[16];
  unsigned gen;            /* of the last watcher that was started: a watcher that ends takes "watching" away only while it is still the last one (audit 17.11.2026) */
  int      cancel_pending; /* a "cancel" came in after ShadowMountPlus's job had ended and before the app's own unpack had its place: given to that unpack */
} g_wait;

/* ---------------------------------------------------------------- the way out when ShadowMountPlus cannot unpack

   ShadowMountPlus makes the unpack folder with the permissions of the folder in the image, and cannot put a file into it when that has no write
   bit: the job ends at once with "Permission denied" (Asterix & Obelix: Heroes, 17.11.2026, the image itself was fine: the app's check found
   132 files in order, and its own unpack of the same image ran through). The app's own unpack gives every game folder 0777, so when the job
   ends like that the app unpacks into the same folder itself. The pages keep following the same job (the id of ShadowMountPlus's failed one):
   ps5tm_gamemove_status answers from the app's own job meanwhile and keeps its result until ShadowMountPlus runs another one. The image is
   not deleted afterwards, also when the person asked for that: nothing the app did not check is thrown away, and the notice says so. */

static struct {
  int      active;                    /* the app's own unpack runs */
  int      have;                      /* a record of one is kept (running or ended) */
  double   smp_job;                   /* ShadowMountPlus's job it stands in for: the id the pages follow */
  char     title_id[12], image[256], dest[320], state[16], error[300];     /* state: "transferring" has 12 letters */
  unsigned total_files, done_files;
  uint64_t total_bytes, done_bytes;   /* the progress of the app's job (written, then read back: twice the bytes of the files) - the percentage comes from these */
  uint64_t files_bytes;               /* the bytes of the files of the image: what the pages show as the size */
  /* the hand-over from ShadowMountPlus's job to the app's own (audit 17.11.2026): until the app's job has taken its place the app counts as busy for everybody
     else, but not for its own start (handover, thread); a "cancel" that comes in that moment is kept (cancel) and given to the job when it exists; the job is
     known by the time it started (token), so another job's record is never taken for it. */
  int         handover, cancel;
  pthread_t   thread;
  uint64_t    token;
} g_fb;

/* The record in the form of ShadowMountPlus's own job record (the fields both pages read). */
static cJSON *
fb_json(void) {
  pthread_mutex_lock(&g_wlock);
  int active = g_fb.active;
  double job = g_fb.smp_job;
  char title_id[12], image[256], dest[320], state[16], error[300];
  snprintf(title_id, sizeof(title_id), "%s", g_fb.title_id);
  snprintf(image, sizeof(image), "%s", g_fb.image);
  snprintf(dest, sizeof(dest), "%s", g_fb.dest);
  snprintf(state, sizeof(state), "%s", g_fb.state);
  snprintf(error, sizeof(error), "%s", g_fb.error);
  unsigned tf = g_fb.total_files, df = g_fb.done_files;
  uint64_t tb = g_fb.total_bytes, db = g_fb.done_bytes, fbytes = g_fb.files_bytes, token = g_fb.token;
  pthread_mutex_unlock(&g_wlock);
  double rate = 0;
  char phase[160];                         /* a copy: the record it comes from is freed below (17.11.2026: the text was read after that) */
  snprintf(phase, sizeof(phase), "%s", "Wird mit dem eigenen Entpacken der App fortgesetzt");
  if(active && token) {                    /* before the app's job has its place the record is the one before it: not read (audit 17.11.2026) */
    cJSON *s = ps5tm_imgjob_status();
    const cJSON *own = s ? cJSON_GetObjectItem(s, "started_ms") : NULL;
    if(s && cJSON_IsNumber(own) && (uint64_t)own->valuedouble == token) {
      const cJSON *x;
      if(cJSON_IsString(x = cJSON_GetObjectItem(s, "image"))) snprintf(image, sizeof(image), "%s", x->valuestring);
      if(cJSON_IsNumber(x = cJSON_GetObjectItem(s, "bytes_files"))) fbytes = (uint64_t)x->valuedouble;
      if(cJSON_IsNumber(x = cJSON_GetObjectItem(s, "work_total"))) tb = (uint64_t)x->valuedouble;
      if(cJSON_IsNumber(x = cJSON_GetObjectItem(s, "work_done"))) db = (uint64_t)x->valuedouble;
      if(cJSON_IsNumber(x = cJSON_GetObjectItem(s, "files_total"))) tf = (unsigned)x->valuedouble;
      if(cJSON_IsNumber(x = cJSON_GetObjectItem(s, "files_done"))) df = (unsigned)x->valuedouble;
      if(cJSON_IsNumber(x = cJSON_GetObjectItem(s, "bytes_per_s"))) rate = x->valuedouble;
      if(cJSON_IsString(x = cJSON_GetObjectItem(s, "phase")) && x->valuestring[0]) snprintf(phase, sizeof(phase), "%s", x->valuestring);
    }
    cJSON_Delete(s);
    snprintf(state, sizeof(state), "%s", tb > 0 && db > 0 ? "transferring" : "measuring");
  }
  /* The size the pages show is the files' (a 20 GB game is "20 GB", not "40 GB"); the progress keeps coming from the work done of the whole job. */
  const uint64_t shown_total = fbytes ? fbytes : tb;
  const uint64_t shown_done = tb ? (uint64_t)((double)db * (double)shown_total / (double)tb) : 0;
  cJSON *o = cJSON_CreateObject();
  cJSON_AddBoolToObject(o, "ok", 1);
  cJSON_AddNumberToObject(o, "job_id", job);
  cJSON_AddStringToObject(o, "operation", "unpack");
  cJSON_AddStringToObject(o, "state", state);
  cJSON_AddBoolToObject(o, "active", active);
  cJSON_AddBoolToObject(o, "cancellable", active);
  cJSON_AddBoolToObject(o, "cancel_requested", 0);
  cJSON_AddStringToObject(o, "title_id", title_id);
  cJSON_AddStringToObject(o, "source", image);
  cJSON_AddStringToObject(o, "destination", dest);
  cJSON_AddStringToObject(o, "phase", phase);
  cJSON_AddNumberToObject(o, "progress_percent", ps5tm_unpack_fb_percent(db, tb));
  cJSON_AddNumberToObject(o, "total_bytes", (double)shown_total);
  cJSON_AddNumberToObject(o, "processed_bytes", (double)shown_done);
  cJSON_AddNumberToObject(o, "total_files", tf);
  cJSON_AddNumberToObject(o, "processed_files", df);
  cJSON_AddNumberToObject(o, "speed_bytes_per_second", rate);
  cJSON_AddBoolToObject(o, "fallback", 1);
  if(!active && !strcmp(state, "failed")) {
    cJSON_AddNumberToObject(o, "result_status", 5);
    cJSON_AddStringToObject(o, "result_error", error[0] ? error : "Das Entpacken ist gescheitert.");
  } else if(!active) {
    cJSON_AddNumberToObject(o, "result_status", !strcmp(state, "completed") ? 0 : 125);
  }
  return o;
}

static void
fb_end(const char *state, const char *error) {
  pthread_mutex_lock(&g_wlock);
  g_fb.active = 0;
  snprintf(g_fb.state, sizeof(g_fb.state), "%s", state);
  snprintf(g_fb.error, sizeof(g_fb.error), "%s", error ? error : "");
  pthread_mutex_unlock(&g_wlock);
}

/* Runs on the watcher's thread after ShadowMountPlus's unpack ended with "Permission denied": the app's own unpack into the same folder
   (<destination>/<title id>), followed to its end. 1 when it completed (origafter.c carries out what the person chose for the image then). */
static int
fallback_unpack(const watch_t *w, const char *smp_msg) {
  char child[320], err[200] = "", image[256] = "";
  snprintf(child, sizeof(child), "%s/%s", w->dest, w->title_id);
  {
    ps5tm_smp_game_t sg;                      /* asked before the lock: ShadowMountPlus may take a moment */
    if(ps5tm_smp_find(w->title_id, &sg) == 0) snprintf(image, sizeof(image), "%s", sg.path);
  }
  pthread_mutex_lock(&g_wlock);
  memset(&g_fb, 0, sizeof(g_fb));
  g_fb.have = 1;
  g_fb.active = 1;
  g_fb.cancel = g_wait.cancel_pending;      /* a "cancel" that came in between the end of ShadowMountPlus's job and now: the own unpack is not started at all */
  const int pre_cancel = g_fb.cancel;
  g_wait.cancel_pending = 0;
  g_fb.handover = 1;                        /* the app's own job is not there yet: for everybody else the app is busy from now on, for this thread it is not */
  g_fb.thread = pthread_self();
  g_fb.smp_job = w->job_id;
  snprintf(g_fb.title_id, sizeof(g_fb.title_id), "%s", w->title_id);
  snprintf(g_fb.dest, sizeof(g_fb.dest), "%s", child);
  snprintf(g_fb.image, sizeof(g_fb.image), "%s", image);
  snprintf(g_fb.state, sizeof(g_fb.state), "%s", "measuring");
  /* g_wait.watching stays 1 until the app's job has its place: the image job asks whether a move or unpack is busy, and ps5tm_gamemove_busy_local
     leaves this thread out of the answer (handover). Nobody else gets into the gap. */
  pthread_mutex_unlock(&g_wlock);
  if(pre_cancel) {                          /* the person cancelled before the own unpack could begin: it does not begin */
    pthread_mutex_lock(&g_wlock);
    g_fb.handover = 0;
    if(g_wait.gen == w->gen) g_wait.watching = 0;
    pthread_mutex_unlock(&g_wlock);
    fb_end("cancelled", NULL);
    PS5TM_INFO("game_storage_fallback", "Das Entpacken von %s wurde abgebrochen, bevor das eigene Entpacken der App begann.", w->title_id);
    return 0;
  }
  PS5TM_WARN("game_storage_fallback", "ShadowMountPlus konnte %s nicht entpacken (%.120s); die App entpackt das Abbild selbst nach %s.",
             w->title_id, smp_msg, child);
  uint64_t token = 0;                       /* the job's start time, handed out by the start itself: it tells this job from every other one */
  int rc = ps5tm_imgjob_unpack_to(w->title_id, child, &token, err, sizeof(err));    /* (a "cancel" given to g_fb.cancel is handed on below, as soon as the job has its place) */
  if(rc != 200) token = 0;
  pthread_mutex_lock(&g_wlock);
  g_fb.handover = 0;
  g_fb.token = token;
  if(g_wait.gen == w->gen) g_wait.watching = 0;      /* the image job counts as busy by itself from here (or there is none); not the marker of a newer watcher */
  const int cancel_asked = g_fb.cancel;
  pthread_mutex_unlock(&g_wlock);
  if(rc != 200) {
    fb_end("failed", err[0] ? err : "Das eigene Entpacken ließ sich nicht starten.");
    ps5tm_notify("„%s“ wurde nicht entpackt: %.120s", w->name, err[0] ? err : "Das eigene Entpacken ließ sich nicht starten.");
    return 0;
  }
  if(cancel_asked && token) (void)ps5tm_imgjob_cancel_token(token);      /* "Abbrechen" came in while the job was being handed over: it goes to the job now */
  int completed = 0;
  for(;;) {
    usleep(250 * 1000);
    cJSON *s = ps5tm_imgjob_status();
    if(!s) continue;
    const cJSON *st = cJSON_GetObjectItem(s, "state"), *er = cJSON_GetObjectItem(s, "error");
    const cJSON *own = cJSON_GetObjectItem(s, "started_ms");
    const char *state = cJSON_IsString(st) ? st->valuestring : "";
    /* the job is ours when it started when ours did (and not merely when it is an unpack of the same game): of two jobs one after the other the second
       is not ours, and a start that was turned away wipes the record (imgjob.c) - then the state is "idle" with no start time */
    const cJSON *jid = cJSON_GetObjectItem(s, "id"), *jkind = cJSON_GetObjectItem(s, "kind");
    int ours = token ? (cJSON_IsNumber(own) && (uint64_t)own->valuedouble == token)
                     : (cJSON_IsString(jid) && !strcmp(jid->valuestring, w->title_id) && cJSON_IsString(jkind) && !strcmp(jkind->valuestring, "unpack"));   /* no start time read: as before */
    if(!strcmp(state, "running") && ours) { cJSON_Delete(s); continue; }
    char msg[300] = "";
    if(cJSON_IsString(er)) snprintf(msg, sizeof(msg), "%s", er->valuestring);
    const cJSON *tf = cJSON_GetObjectItem(s, "files_total"), *df = cJSON_GetObjectItem(s, "files_done");
    const cJSON *tb = cJSON_GetObjectItem(s, "work_total"), *db = cJSON_GetObjectItem(s, "work_done"), *bf = cJSON_GetObjectItem(s, "bytes_files");
    pthread_mutex_lock(&g_wlock);
    if(ours) {
      if(cJSON_IsNumber(tf)) g_fb.total_files = (unsigned)tf->valuedouble;
      if(cJSON_IsNumber(df)) g_fb.done_files = (unsigned)df->valuedouble;
      if(cJSON_IsNumber(tb)) g_fb.total_bytes = (uint64_t)tb->valuedouble;
      if(cJSON_IsNumber(db)) g_fb.done_bytes = (uint64_t)db->valuedouble;
      if(cJSON_IsNumber(bf)) g_fb.files_bytes = (uint64_t)bf->valuedouble;
    }
    pthread_mutex_unlock(&g_wlock);
    int done = ours && !strcmp(state, "done");
    int cancelled = ours && !strcmp(state, "cancelled");
    cJSON_Delete(s);
    if(!ours) {                             /* another job has taken the place before this one's end was seen: what became of it is not known */
      fb_end("failed", "Das Ergebnis des Entpackens ließ sich nicht mehr lesen, weil gleich danach ein anderer Vorgang begann. Bitte den Zielordner prüfen.");
      ps5tm_notify("„%s“: Das Ergebnis des Entpackens ließ sich nicht lesen. Bitte den Zielordner prüfen.", w->name);
      break;
    }
    if(done) {
      pthread_mutex_lock(&g_wlock);
      g_fb.done_bytes = g_fb.total_bytes;
      pthread_mutex_unlock(&g_wlock);
      fb_end("completed", NULL);
      completed = 1;
      ps5tm_notify("„%s“ ist entpackt (von der App selbst: ShadowMountPlus meldete „Permission denied“).%s", w->name,
                   w->delete_source ? " Das Abbild wurde nicht gelöscht." : "");
    } else if(cancelled) {
      fb_end("cancelled", NULL);
    } else {
      fb_end("failed", msg[0] ? msg : "Das Entpacken ist gescheitert.");
      ps5tm_notify("„%s“ wurde nicht entpackt: %.120s", w->name, msg[0] ? msg : "Das Entpacken ist gescheitert.");
    }
    break;
  }
  ps5tm_smp_forget();
  ps5tm_library_forget();
  return completed;
}

static cJSON *
smp_start(const watch_t *w, char *err, size_t err_len) {
  cJSON *body = cJSON_CreateObject();
  cJSON_AddStringToObject(body, "title_id", w->title_id);
  cJSON_AddStringToObject(body, "destination_dir", w->dest);
  if(!strcmp(w->op, "unpack"))
    cJSON_AddBoolToObject(body, "delete_source", w->delete_source ? 1 : 0);
  cJSON *ans = ps5tm_smp_call(!strcmp(w->op, "move") ? "/games/move" : "/games/unpack", body, 8000, err, err_len);
  cJSON_Delete(body);
  return ans;
}

static double
job_id_of(const cJSON *o) {
  const cJSON *id = cJSON_GetObjectItem(o, "job_id");
  return cJSON_IsNumber(id) ? id->valuedouble : 0;
}

static int
tile_reason(const char *reason) {
  return !strcmp(reason, "game_active") || !strcmp(reason, "runtime_prepared");
}

/* Waits for ShadowMountPlus's job job_id to end: 1 done, 0 failed (reason and message), -1 lost sight of it. *given_out: 1 when its record carried a result
   value (result_status) - a record without one is taken for done, but it is only its word without a number ("Original danach" then does not delete). */
static int
follow_job(double job_id, char *reason, size_t reason_len, char *msg, size_t msg_len, int *status_out, int *given_out) {
  reason[0] = msg[0] = '\0';
  if(status_out) *status_out = 0;
  if(given_out) *given_out = 0;
  for(int misses = 0; misses < 20; ) {
    usleep(1500 * 1000);
    char err[160];
    cJSON *s = ps5tm_smp_call("/games/storage/status", NULL, 4000, err, sizeof(err));
    if(!s) { misses++; continue; }
    misses = 0;
    if(job_id > 0 && job_id_of(s) != job_id) { cJSON_Delete(s); return -1; }
    if(cJSON_IsTrue(cJSON_GetObjectItem(s, "active"))) { cJSON_Delete(s); continue; }
    const cJSON *rs = cJSON_GetObjectItem(s, "result_status");
    const cJSON *rr = cJSON_GetObjectItem(s, "result_error_reason");
    const cJSON *re = cJSON_GetObjectItem(s, "result_error");
    int ok = !cJSON_IsNumber(rs) || rs->valuedouble == 0;
    if(status_out) *status_out = cJSON_IsNumber(rs) ? (int)rs->valuedouble : 0;
    if(given_out) *given_out = cJSON_IsNumber(rs);
    snprintf(reason, reason_len, "%s", cJSON_IsString(rr) ? rr->valuestring : "");
    snprintf(msg, msg_len, "%s", cJSON_IsString(re) ? re->valuestring : "");
    cJSON_Delete(s);
    return ok;
  }
  return -1;
}

static void *
watch_thread(void *arg) {
  watch_t *w = arg;
  char reason[64], msg[200], tile[16], game[16];
  int st = 0, given = 0;
  int r = follow_job(w->job_id, reason, sizeof(reason), msg, sizeof(msg), &st, &given);
  /* the unpack ended well (by ShadowMountPlus or by the app's own way out): what the person chose for the original (origafter.c) follows; 1 when ShadowMountPlus
     named a result value, 2 when it only ended without one (a move is then still allowed, a delete is not) */
  int unpack_ok = (r == 1) ? (given ? 1 : 2) : 0;
  if(r == 0 && !tile_reason(reason) && ps5tm_unpack_fallback_wanted(w->op, st, msg))
    unpack_ok = fallback_unpack(w, msg);
  else if(r == 0 && tile_reason(reason) && ps5tm_procmgr_own_tile_running(tile, sizeof(tile)) &&
     ps5tm_procmgr_game_title(0, game, sizeof(game)) != 0) {
    pthread_mutex_lock(&g_wlock);
    g_wait.on = 1;
    snprintf(g_wait.title_id, sizeof(g_wait.title_id), "%s", w->title_id);
    snprintf(g_wait.op, sizeof(g_wait.op), "%s", w->op);
    snprintf(g_wait.tile, sizeof(g_wait.tile), "%s", tile);
    pthread_mutex_unlock(&g_wlock);
    PS5TM_INFO("game_storage_wait_tile", "%s von %s wartet, bis %s geschlossen ist (ShadowMountPlus sperrt solange).",
               !strcmp(w->op, "move") ? "Verschieben" : "Entpacken", w->title_id, tile);
    const char *what = !strcmp(w->op, "move") ? "verschoben" : "entpackt";
    uint64_t t0 = ps5tm_mono_ms();
    int started = 0, st2 = 0, given2 = 0;
    char err[300] = "";
    while(ps5tm_mono_ms() - t0 < (uint64_t)TILE_WAIT_S * 1000) {
      usleep(2000 * 1000);
      if(ps5tm_procmgr_own_tile_running(tile, sizeof(tile))) continue;
      err[0] = '\0';
      cJSON *ans = smp_start(w, err, sizeof(err));
      if(!ans) {
        if(strstr(err, "game is active")) continue;        /* the tile's exit is still being cleaned up */
        break;
      }
      double id = job_id_of(ans);
      cJSON_Delete(ans);
      ps5tm_smp_forget();
      pthread_mutex_lock(&g_wlock);
      g_wait.on = 0;                                       /* from here on ShadowMountPlus's own record tells the progress */
      pthread_mutex_unlock(&g_wlock);
      w->job_id = id;                                      /* the pages follow this job from here (and the own unpack stands in for it when it ends so) */
      pthread_mutex_lock(&g_wlock);
      g_wait.watch_job = id;                               /* the status route compares with this one */
      pthread_mutex_unlock(&g_wlock);
      r = follow_job(id, reason, sizeof(reason), msg, sizeof(msg), &st2, &given2);
      if(r == 0 && tile_reason(reason)) {
        pthread_mutex_lock(&g_wlock);
        g_wait.on = 1;                                     /* refused again: still waiting, still busy */
        pthread_mutex_unlock(&g_wlock);
        continue;
      }
      started = 1;
      break;
    }
    pthread_mutex_lock(&g_wlock);
    g_wait.on = 0;
    pthread_mutex_unlock(&g_wlock);
    ps5tm_smp_forget();
    ps5tm_library_forget();
    if(started && r == 0 && !tile_reason(reason) && ps5tm_unpack_fallback_wanted(w->op, st2, msg))
      unpack_ok = fallback_unpack(w, msg);                 /* the same way out as for the first try: it tells the person itself */
    else if(started && r == 1) { unpack_ok = given2 ? 1 : 2; ps5tm_notify("„%s“ ist %s.", w->name, what); }
    else if(started && r == 0) ps5tm_notify("„%s“ wurde nicht %s: %.120s", w->name, what, msg[0] ? msg : reason);
    else if(!started) ps5tm_notify("„%s“ wurde nicht %s: %.120s", w->name, what,
                                   err[0] ? err : "die Vollbild-App wurde nicht geschlossen");
  }
  pthread_mutex_lock(&g_wlock);
  if(g_wait.gen == w->gen) { g_wait.watching = 0; g_wait.cancel_pending = 0; }     /* only while this is still the last watcher */
  pthread_mutex_unlock(&g_wlock);
  if(!strcmp(w->op, "unpack")) {                           /* "Original danach" (origafter.c): the result lies in <destination>/<title id>; records only */
    char result[320];
    snprintf(result, sizeof(result), "%s/%s", w->dest, w->title_id);
    ps5tm_orig_job_done("unpack", w->title_id, unpack_ok, result, w->started_ms);
  }
  free(w);
  return NULL;
}

static void
watch_spawn(const watch_t *req, double job_id) {
  watch_t *w = calloc(1, sizeof(*w));
  if(!w) {
    if(!strcmp(req->op, "unpack")) ps5tm_orig_job_done("unpack", req->title_id, 0, NULL, req->started_ms);     /* nobody follows the job: what was chosen for the original is dropped */
    return;
  }
  *w = *req;
  w->job_id = job_id;
  char link[96];
  if(ps5tm_library_launch_info(w->title_id, w->name, sizeof(w->name), link, sizeof(link)) != 0)
    snprintf(w->name, sizeof(w->name), "%s", w->title_id);
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
  pthread_attr_setstacksize(&attr, 256 * 1024);
  pthread_mutex_lock(&g_wlock);
  g_wait.watching = 1;
  g_wait.watch_job = job_id;
  g_wait.cancel_pending = 0;
  w->gen = ++g_wait.gen;
  pthread_mutex_unlock(&g_wlock);
  pthread_t th;
  if(pthread_create(&th, &attr, watch_thread, w) != 0) {
    pthread_mutex_lock(&g_wlock);
    if(g_wait.gen == w->gen) g_wait.watching = 0;
    pthread_mutex_unlock(&g_wlock);
    if(!strcmp(w->op, "unpack")) ps5tm_orig_job_done("unpack", w->title_id, 0, NULL, w->started_ms);
    free(w);
  }
  pthread_attr_destroy(&attr);
}

static int
waiting_for_tile(void) {
  pthread_mutex_lock(&g_wlock);
  int on = g_wait.on;
  pthread_mutex_unlock(&g_wlock);
  return on;
}

/* 1 from the first check of a start until ShadowMountPlus has taken the job (audit 11.11.2026, F-07): the other jobs of the app see a move
   that is only being started (ps5tm_gamemove_busy) and do not start beside it, and a second move does not start in the gap between asking
   and ShadowMountPlus' reply. */
static int g_starting;

static int
gamemove_start_checked(const char *title_id, const char *op, const char *dest_dir,
                       int delete_source, char *err, size_t err_len) {
  /* First of all: while the app follows a job (or unpacks itself) nothing else starts. Before the question to ShadowMountPlus: the g_starting of this start
     must not make the app's own hand-over (which asks the other jobs for room) give way. */
  pthread_mutex_lock(&g_wlock);
  const int following = g_fb.active || g_wait.watching;
  pthread_mutex_unlock(&g_wlock);
  if(following) {
    snprintf(err, err_len, "Ein Verschieben oder Entpacken läuft noch (die App folgt dem Auftrag). Bitte warten, bis es fertig ist.");
    return 409;
  }
  ps5tm_smp_game_t s;
  int st = check_title(title_id, op, &s, err, err_len);
  if(st != 200) return st;
  if(game_running(title_id)) {
    snprintf(err, err_len, "Das Spiel läuft gerade. Bitte erst beenden.");
    return 409;
  }
  if(waiting_for_tile()) {
    snprintf(err, err_len, "Ein Auftrag von ShadowMountPlus wartet noch darauf, dass die Vollbild-App geschlossen wird.");
    return 409;
  }

  /* Only a folder from the plan's own list goes on to ShadowMountPlus. */
  dest_t d[MAX_DESTS];
  int n = list_dests(s.path, d, MAX_DESTS), found = -1;
  for(int i = 0; i < n; i++)
    if(dest_dir && !strcmp(d[i].path, dest_dir)) { found = i; break; }
  if(found < 0) {
    snprintf(err, err_len, "Dieses Ziel gibt es nicht.");
    return 400;
  }
  char here[256];
  parent_dir(s.path, here, sizeof(here));
  if(!strcmp(op, "move") && !strcmp(here, dest_dir)) {
    snprintf(err, err_len, "Das Spiel liegt schon dort.");
    return 409;
  }

  /* What the plan checked is checked again: the page may be a minute old, and
     a request does not have to come from the page at all. ShadowMountPlus
     keeps the source's own name for a move and makes <title id> for an unpack;
     what already lies there must not be handed to it. */
  int         unpack = !strcmp(op, "unpack");
  const char *base   = strrchr(s.path, '/');
  base = base ? base + 1 : s.path;
  const char *leaf = unpack ? title_id : base;
  if(leaf[0]) {
    char child[512];
    struct stat cst;
    snprintf(child, sizeof(child), "%s/%s", dest_dir, leaf);
    if(lstat(child, &cst) == 0) {
      snprintf(err, err_len, "Dort liegt schon etwas mit diesem Namen: %s", child);
      return 409;
    }
  }
  uint64_t size = 0, largest = 0;
  if(ps5tm_gamecopy_measure(s.path, &size, &largest) != 0) {
    snprintf(err, err_len, "Die Spieldaten sind nicht lesbar.");
    return 409;
  }
  const dest_t *dd = &d[found];
  if(!strcmp(dd->fs, "msdosfs") && largest > FAT32_MAX_FILE) {
    snprintf(err, err_len, "Das Ziel ist mit FAT32 formatiert und nimmt keine "
             "Datei über 4 GB auf.");
    return 409;
  }
  if((unpack || !dd->same_device) && dd->free_bytes < size + SPACE_RESERVE) {
    snprintf(err, err_len, "Auf dem Ziel ist nicht genug Platz.");
    return 409;
  }

  /* One file job at a time: not beside a copy or a conversion of the app's own
     (they would share the room, and a move deletes what they read).
     ShadowMountPlus refuses a second job of its own by itself. Asked last,
     right before the call, so the gap in which one could start between the
     asking and ShadowMountPlus' reply stays as short as it can. */
  {
    char w[160];
    if(ps5tm_jobs_busy_except(PS5TM_JOB_GAMEMOVE, 0, w, sizeof(w))) {
      snprintf(err, err_len, "%s. Kopieren, Konvertieren und Verschieben laufen nicht gleichzeitig.", w);
      return 409;
    }
  }
  if(mkdir_p(dest_dir) != 0) {
    snprintf(err, err_len, "Der Zielordner ließ sich nicht anlegen.");
    return 500;
  }

  watch_t req = { .delete_source = delete_source, .started_ms = ps5tm_mono_ms() };
  snprintf(req.title_id, sizeof(req.title_id), "%s", title_id);
  snprintf(req.op, sizeof(req.op), "%s", op);
  snprintf(req.dest, sizeof(req.dest), "%s", dest_dir);
  cJSON *ans = smp_start(&req, err, err_len);
  if(!ans) return 503;
  watch_spawn(&req, job_id_of(ans));
  cJSON_Delete(ans);
  ps5tm_smp_forget();
  PS5TM_INFO("game_storage_started", "ShadowMountPlus: %s %s nach %s%s.",
             !strcmp(op, "move") ? "Verschieben" : "Entpacken", title_id,
             dest_dir, delete_source ? " (Abbild danach löschen)" : "");
  return 200;
}

cJSON *
ps5tm_gamemove_status(char *err, size_t err_len) {
  pthread_mutex_lock(&g_wlock);
  int on = g_wait.on, watching = g_wait.watching;
  int fb_active = g_fb.active, fb_have = g_fb.have;
  double fb_job = g_fb.smp_job;
  double watch_job = g_wait.watch_job;
  char id[12], op[8], tile[16];
  snprintf(id, sizeof(id), "%s", g_wait.title_id);
  snprintf(op, sizeof(op), "%s", g_wait.op);
  snprintf(tile, sizeof(tile), "%s", g_wait.tile);
  pthread_mutex_unlock(&g_wlock);
  if(on) {
    /* ShadowMountPlus's own record shows the refused job; what goes on is the wait. */
    cJSON *w = cJSON_CreateObject();
    cJSON_AddBoolToObject(w, "ok", 1);
    cJSON_AddNumberToObject(w, "job_id", 0);
    cJSON_AddStringToObject(w, "operation", op);
    cJSON_AddStringToObject(w, "state", "waiting");
    cJSON_AddBoolToObject(w, "active", 1);
    cJSON_AddBoolToObject(w, "cancellable", 0);
    cJSON_AddStringToObject(w, "title_id", id);
    cJSON_AddStringToObject(w, "phase", "Wartet, bis die Vollbild-App geschlossen ist");
    cJSON_AddStringToObject(w, "waiting_for", "tile");
    cJSON_AddStringToObject(w, "tile", tile);
    return w;
  }
  if(fb_active) return fb_json();            /* the app's own unpack stands in for the one ShadowMountPlus could not do: no question to it */
  cJSON *ans = ps5tm_smp_call("/games/storage/status", NULL, 4000, err, err_len);
  if(!ans) return NULL;
  /* The record of the app's own unpack stays until ShadowMountPlus runs another job. */
  if(fb_have && job_id_of(ans) == fb_job) { cJSON_Delete(ans); return fb_json(); }
  /* Ended with "Permission denied" a moment ago: the watcher is about to unpack it itself, so that is what to show (not the failure). */
  {
    const cJSON *op = cJSON_GetObjectItem(ans, "operation"), *rs = cJSON_GetObjectItem(ans, "result_status");
    const cJSON *re = cJSON_GetObjectItem(ans, "result_error");
    if(watching && job_id_of(ans) == watch_job && !cJSON_IsTrue(cJSON_GetObjectItem(ans, "active")) &&
       ps5tm_unpack_fallback_wanted(cJSON_IsString(op) ? op->valuestring : "", cJSON_IsNumber(rs) ? (int)rs->valuedouble : 0,
                                    cJSON_IsString(re) ? re->valuestring : "")) {
      cJSON_ReplaceItemInObject(ans, "active", cJSON_CreateBool(1));
      cJSON_ReplaceItemInObject(ans, "state", cJSON_CreateString("measuring"));
      cJSON_AddStringToObject(ans, "phase", "Wird mit dem eigenen Entpacken der App fortgesetzt");
      cJSON_AddBoolToObject(ans, "ok", 1);
      return ans;
    }
  }
  /* Refused for the app's own tile a moment ago: the watcher is about to wait for it, so that is what to show (a
     "failed" in between would read as the end of it). */
  const cJSON *rr = cJSON_GetObjectItem(ans, "result_error_reason");
  if(watching && job_id_of(ans) == watch_job &&
     !cJSON_IsTrue(cJSON_GetObjectItem(ans, "active")) && cJSON_IsString(rr) && tile_reason(rr->valuestring) &&
     ps5tm_procmgr_own_tile_running(tile, sizeof(tile))) {
    cJSON_ReplaceItemInObject(ans, "active", cJSON_CreateBool(1));
    cJSON_ReplaceItemInObject(ans, "state", cJSON_CreateString("waiting"));
    cJSON_AddStringToObject(ans, "phase", "Wartet, bis die Vollbild-App geschlossen ist");
    cJSON_AddStringToObject(ans, "waiting_for", "tile");
    cJSON_AddStringToObject(ans, "tile", tile);
    cJSON_AddBoolToObject(ans, "ok", 1);
    return ans;
  }
  /* Once it has finished, ShadowMountPlus's list shows the new place. */
  if(!cJSON_IsTrue(cJSON_GetObjectItem(ans, "active"))) {
    ps5tm_smp_forget();
    ps5tm_library_forget();
  }
  cJSON_AddBoolToObject(ans, "ok", 1);
  return ans;
}

int
ps5tm_gamemove_start(const char *title_id, const char *op, const char *dest_dir,
                     int delete_source, char *err, size_t err_len) {
  if(__atomic_exchange_n(&g_starting, 1, __ATOMIC_ACQ_REL)) {
    snprintf(err, err_len, "Es wird gerade schon ein Verschieben oder Entpacken gestartet. Bitte einen Moment warten.");
    return 409;
  }
  int rc = gamemove_start_checked(title_id, op, dest_dir, delete_source, err, err_len);
  __atomic_store_n(&g_starting, 0, __ATOMIC_RELEASE);
  return rc;
}

/* The part that needs no question to ShadowMountPlus: a start under way, a wait for the app's own tile, a job being followed. The jobs
   list (jobreg.c) asks this alone when it is told not to ask ShadowMountPlus (PS5TM_JOBS_NO_SMP), so that skipping the HTTP call never
   skips the app's own marks (audit F-34). */
int
ps5tm_gamemove_busy_local(void) {
  pthread_mutex_lock(&g_wlock);
  int on = g_wait.on || g_wait.watching;               /* waiting for the tile, or following the job ShadowMountPlus has taken */
  const int own = g_fb.handover && pthread_equal(g_fb.thread, pthread_self());     /* the app's own unpack asking for its own place */
  if(own && !g_wait.on) on = 0;
  pthread_mutex_unlock(&g_wlock);
  /* Another start that is only being checked (g_starting) does not hold the app's own hand-over up: that start looks for the follow-up first and gives way. */
  if(!own && __atomic_load_n(&g_starting, __ATOMIC_ACQUIRE)) return 1;
  return on;
}

/* 1 while ShadowMountPlus runs a storage job (a move, an unpack). For the
   app's own copy and conversion, which do not start beside one. A short
   timeout: this sits in the way of a start, and a ShadowMountPlus that does
   not answer, or has no job to report, is read as idle — it must never keep
   the person from copying. */
int
ps5tm_gamemove_busy(void) {
  if(ps5tm_gamemove_busy_local()) return 1;
  char err[128];
  cJSON *job = ps5tm_smp_call("/games/storage/status", NULL, 1500, err, sizeof(err));
  if(!job) return 0;
  int active = cJSON_IsTrue(cJSON_GetObjectItem(job, "active"));
  cJSON_Delete(job);
  return active;
}

int
ps5tm_gamemove_cancel(char *err, size_t err_len) {
  /* Decided under the lock the hand-over ends under (handover = 0 and the read of cancel are one step in fallback_unpack): a "cancel" is either kept for
     the job that is being handed over, or goes to the job by its start time - never to a job of somebody else, never into the gap between the two. */
  pthread_mutex_lock(&g_wlock);
  int fb_active = g_fb.active, handing = g_fb.handover;
  uint64_t fb_token = g_fb.token;
  if(fb_active && handing) g_fb.cancel = 1;
  pthread_mutex_unlock(&g_wlock);
  if(fb_active) {                                                   /* the app's own unpack stands in for ShadowMountPlus's */
    if(!handing && fb_token) (void)ps5tm_imgjob_cancel_token(fb_token);
    return 200;
  }
  cJSON *job = ps5tm_smp_call("/games/storage/status", NULL, 4000, err, err_len);
  if(!job) return 503;
  const cJSON *id = cJSON_GetObjectItem(job, "job_id");
  int active = cJSON_IsTrue(cJSON_GetObjectItem(job, "active"));
  double job_id = cJSON_IsNumber(id) ? id->valuedouble : 0;
  const cJSON *opj = cJSON_GetObjectItem(job, "operation"), *rsj = cJSON_GetObjectItem(job, "result_status"), *rej = cJSON_GetObjectItem(job, "result_error");
  const cJSON *rrj = cJSON_GetObjectItem(job, "result_error_reason");
  char opname[16] = "", emsg[200] = "", ereason[64] = "";
  int rstat = cJSON_IsNumber(rsj) ? (int)rsj->valuedouble : 0;
  if(cJSON_IsString(opj)) snprintf(opname, sizeof(opname), "%s", opj->valuestring);
  if(cJSON_IsString(rej)) snprintf(emsg, sizeof(emsg), "%s", rej->valuestring);
  if(cJSON_IsString(rrj)) snprintf(ereason, sizeof(ereason), "%s", rrj->valuestring);
  cJSON_Delete(job);
  if(!active || job_id <= 0) {
    /* The hand-over may have begun while ShadowMountPlus was asked (up to 4 s): then the cancel goes where it belongs - to the job being handed over, or to the
       own job by its start time - and not into a mark that nobody reads any more. */
    pthread_mutex_lock(&g_wlock);
    if(g_fb.active) {
      const int handing = g_fb.handover;
      const uint64_t tok = g_fb.token;
      if(handing) g_fb.cancel = 1;
      pthread_mutex_unlock(&g_wlock);
      if(!handing && tok) (void)ps5tm_imgjob_cancel_token(tok);
      return 200;
    }
    /* ShadowMountPlus's job is over, and the app follows it. When it ended the way that leads to the app's own unpack ("Permission denied"), the cancel is kept for
       that unpack (audit 17.11.2026) - it used to be turned away here and the unpack then started all the same. For any other end (done, another failure) there
       is nothing to cancel: the answer stays "no running job". Looked at under the same lock as g_fb above: the hand-over takes the mark under it, too. */
    const int gap = g_wait.watching && !g_wait.on && job_id > 0 && job_id == g_wait.watch_job && rstat != 0 && !tile_reason(ereason) &&
                    ps5tm_unpack_fallback_wanted(opname, rstat, emsg);
    if(gap) g_wait.cancel_pending = 1;
    pthread_mutex_unlock(&g_wlock);
    if(gap) return 200;
    snprintf(err, err_len, "ShadowMountPlus hat keinen laufenden Auftrag.");
    return 409;
  }
  /* The job may end with "Permission denied" between the status above and the cancel below, and the app's own unpack then starts all the same: the cancel is kept
     for it as well (cleared when the watcher ends, so it never outlives this job). */
  pthread_mutex_lock(&g_wlock);
  if(g_wait.watching && !g_wait.on && job_id == g_wait.watch_job) g_wait.cancel_pending = 1;
  pthread_mutex_unlock(&g_wlock);
  cJSON *body = cJSON_CreateObject();
  cJSON_AddNumberToObject(body, "job_id", job_id);
  cJSON *ans = ps5tm_smp_call("/games/storage/cancel", body, 4000, err, err_len);
  cJSON_Delete(body);
  if(!ans) return 503;
  cJSON_Delete(ans);
  return 200;
}
