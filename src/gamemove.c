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

int
ps5tm_gamemove_start(const char *title_id, const char *op, const char *dest_dir,
                     int delete_source, char *err, size_t err_len) {
  ps5tm_smp_game_t s;
  int st = check_title(title_id, op, &s, err, err_len);
  if(st != 200) return st;
  if(game_running(title_id)) {
    snprintf(err, err_len, "Das Spiel läuft gerade. Bitte erst beenden.");
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
  if(ps5tm_pkginst_busy()) {
    snprintf(err, err_len, "Es wird gerade ein Paket installiert. Kopieren, Konvertieren "
             "und Verschieben laufen nicht gleichzeitig damit.");
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

  cJSON *body = cJSON_CreateObject();
  cJSON_AddStringToObject(body, "title_id", title_id);
  cJSON_AddStringToObject(body, "destination_dir", dest_dir);
  if(!strcmp(op, "unpack"))
    cJSON_AddBoolToObject(body, "delete_source", delete_source ? 1 : 0);
  cJSON *ans = ps5tm_smp_call(!strcmp(op, "move") ? "/games/move" : "/games/unpack",
                              body, 8000, err, err_len);
  cJSON_Delete(body);
  if(!ans) return 503;
  cJSON_Delete(ans);
  ps5tm_smp_forget();
  PS5TM_INFO("game_storage_started", "ShadowMountPlus: %s %s nach %s%s.",
             !strcmp(op, "move") ? "Verschieben" : "Entpacken", title_id,
             dest_dir, delete_source ? " (Abbild danach löschen)" : "");
  return 200;
}

cJSON *
ps5tm_gamemove_status(char *err, size_t err_len) {
  cJSON *ans = ps5tm_smp_call("/games/storage/status", NULL, 4000, err, err_len);
  if(!ans) return NULL;
  /* Once it has finished, ShadowMountPlus's list shows the new place. */
  if(!cJSON_IsTrue(cJSON_GetObjectItem(ans, "active"))) {
    ps5tm_smp_forget();
    ps5tm_library_forget();
  }
  cJSON_AddBoolToObject(ans, "ok", 1);
  return ans;
}

/* 1 while ShadowMountPlus runs a storage job (a move, an unpack). For the
   app's own copy and conversion, which do not start beside one. A short
   timeout: this sits in the way of a start, and a ShadowMountPlus that does
   not answer, or has no job to report, is read as idle — it must never keep
   the person from copying. */
int
ps5tm_gamemove_busy(void) {
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
