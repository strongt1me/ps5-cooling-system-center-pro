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
} watch_t;

static pthread_mutex_t g_wlock = PTHREAD_MUTEX_INITIALIZER;
static struct { int on, watching; double watch_job; char title_id[12], op[8], tile[16]; } g_wait;

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

/* Waits for ShadowMountPlus's job job_id to end: 1 done, 0 failed (reason and message), -1 lost sight of it. */
static int
follow_job(double job_id, char *reason, size_t reason_len, char *msg, size_t msg_len) {
  reason[0] = msg[0] = '\0';
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
  int r = follow_job(w->job_id, reason, sizeof(reason), msg, sizeof(msg));
  if(r == 0 && tile_reason(reason) && ps5tm_procmgr_own_tile_running(tile, sizeof(tile)) &&
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
    int started = 0;
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
      r = follow_job(id, reason, sizeof(reason), msg, sizeof(msg));
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
    if(started && r == 1) ps5tm_notify("„%s“ ist %s.", w->name, what);
    else if(started && r == 0) ps5tm_notify("„%s“ wurde nicht %s: %.120s", w->name, what, msg[0] ? msg : reason);
    else if(!started) ps5tm_notify("„%s“ wurde nicht %s: %.120s", w->name, what,
                                   err[0] ? err : "die Vollbild-App wurde nicht geschlossen");
  }
  pthread_mutex_lock(&g_wlock);
  g_wait.watching = 0;
  pthread_mutex_unlock(&g_wlock);
  free(w);
  return NULL;
}

static void
watch_spawn(const watch_t *req, double job_id) {
  watch_t *w = calloc(1, sizeof(*w));
  if(!w) return;
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
  pthread_mutex_unlock(&g_wlock);
  pthread_t th;
  if(pthread_create(&th, &attr, watch_thread, w) != 0) {
    pthread_mutex_lock(&g_wlock);
    g_wait.watching = 0;
    pthread_mutex_unlock(&g_wlock);
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
  if(ps5tm_saves_busy()) {
    snprintf(err, err_len, "Es läuft gerade ein Vorgang mit den Spielständen. Kopieren, Konvertieren "
             "und Verschieben laufen nicht gleichzeitig damit.");
    return 409;
  }
  if(ps5tm_pkgsplit_busy()) {
    snprintf(err, err_len, "Es wird gerade ein Paket geteilt. Kopieren, Konvertieren "
             "und Verschieben laufen nicht gleichzeitig damit.");
    return 409;
  }
  if(ps5tm_ampr_busy() || ps5tm_imgjob_busy()) {
    snprintf(err, err_len, "Es wird gerade ein AMPR-Index gebaut. Bitte danach noch einmal.");
    return 409;
  }
  if(ps5tm_filemgr_busy()) {
    snprintf(err, err_len, "Der Dateimanager arbeitet gerade (Kopieren, Verschieben oder Löschen). Bitte danach noch einmal.");
    return 409;
  }
  if(ps5tm_pkginst_busy()) {
    snprintf(err, err_len, "Es wird gerade ein Paket installiert. Kopieren, Konvertieren "
             "und Verschieben laufen nicht gleichzeitig damit.");
    return 409;
  }
  if(ps5tm_usbeject_busy()) {
    snprintf(err, err_len, "Ein Laufwerk wird gerade ausgeworfen. Kopieren, Konvertieren und Verschieben laufen nicht gleichzeitig damit.");
    return 409;
  }
  if(ps5tm_gamedelete_busy()) {
    snprintf(err, err_len, "Es wird gerade ein Spiel oder eine Sicherung gelöscht. Kopieren, Konvertieren "
             "und Verschieben laufen nicht gleichzeitig damit.");
    return 409;
  }
  if(ps5tm_gamecopy_busy()) {
    snprintf(err, err_len, "Es läuft gerade eine Kopie. Kopieren, Konvertieren "
             "und Verschieben laufen nicht gleichzeitig.");
    return 409;
  }
  if(ps5tm_gameconvert_busy()) {
    snprintf(err, err_len, "Es läuft gerade eine Konvertierung. Kopieren, "
             "Konvertieren und Verschieben laufen nicht gleichzeitig.");
    return 409;
  }
  if(mkdir_p(dest_dir) != 0) {
    snprintf(err, err_len, "Der Zielordner ließ sich nicht anlegen.");
    return 500;
  }

  watch_t req = { .delete_source = delete_source };
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
  cJSON *ans = ps5tm_smp_call("/games/storage/status", NULL, 4000, err, err_len);
  if(!ans) return NULL;
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

/* 1 while ShadowMountPlus runs a storage job (a move, an unpack). For the
   app's own copy and conversion, which do not start beside one. A short
   timeout: this sits in the way of a start, and a ShadowMountPlus that does
   not answer, or has no job to report, is read as idle — it must never keep
   the person from copying. */
int
ps5tm_gamemove_busy(void) {
  if(__atomic_load_n(&g_starting, __ATOMIC_ACQUIRE)) return 1;
  if(waiting_for_tile()) return 1;
  char err[128];
  cJSON *job = ps5tm_smp_call("/games/storage/status", NULL, 1500, err, sizeof(err));
  if(!job) return 0;
  int active = cJSON_IsTrue(cJSON_GetObjectItem(job, "active"));
  cJSON_Delete(job);
  return active;
}

int
ps5tm_gamemove_cancel(char *err, size_t err_len) {
  cJSON *job = ps5tm_smp_call("/games/storage/status", NULL, 4000, err, err_len);
  if(!job) return 503;
  const cJSON *id = cJSON_GetObjectItem(job, "job_id");
  int active = cJSON_IsTrue(cJSON_GetObjectItem(job, "active"));
  double job_id = cJSON_IsNumber(id) ? id->valuedouble : 0;
  cJSON_Delete(job);
  if(!active || job_id <= 0) {
    snprintf(err, err_len, "ShadowMountPlus hat keinen laufenden Auftrag.");
    return 409;
  }
  cJSON *body = cJSON_CreateObject();
  cJSON_AddNumberToObject(body, "job_id", job_id);
  cJSON *ans = ps5tm_smp_call("/games/storage/cancel", body, 4000, err, err_len);
  cJSON_Delete(body);
  if(!ans) return 503;
  cJSON_Delete(ans);
  return 200;
}
