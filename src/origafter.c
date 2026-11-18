/* PS5 Cooling & System Center - Pro: what happens to the ORIGINAL of a game after a conversion, a copy or an unpack (origafter.c, 18.11.2026).
 *
 * The user's wish (18.11.2026): "Wenn ein Spiel konvertiert wird, kopiert oder entpackt usw. sollte es die Option noch geben, das man das Spiel
 * danach löschen bzw. verschieben (dort wo SMP nicht nachschaut) kann (mit Auswahl), damit keine Probleme mit dem Doppelten Spiel auf der PS5
 * entsteht (SMP macht da Probleme)." The same title twice on the console, in two places ShadowMountPlus searches, is what goes wrong.
 *
 * The choice is made in the window of the job: "keep" (the default: nothing changes), "delete" or "move" (to a folder on a drive the person
 * picks; the app suggests <drive>/PS5-Sicherung/Spiele, which ShadowMountPlus never searches, and refuses a place it would search).
 *
 *   prepare   before the job starts: reads the choice, finds the original (the folder or the image the title runs from, as the library lists it
 *             now), checks that it may be deleted (gamedelete.c: only below a drive's mount point or /data, no link, game not running ...) and,
 *             for "move", that the target is not searched by ShadowMountPlus, does not exist and has room. Nothing is started when this refuses.
 *   arm       the choice is noted as "pending" before the job is started (so that a job that ends at once cannot be missed) ...
 *   commit    ... and made firm when the job has started (dropped when it was refused).
 *   hook      the job modules call ps5tm_orig_job_done() at their end (gamecopy.c, gameconvert.c, gamemove.c). It only records: the job's own
 *             thread does nothing slow. A job that did not end well leaves the original alone, whatever was chosen.
 *   worker    one thread of its own, which waits until none of the app's jobs runs any more, then carries out the choice:
 *             delete  gamedelete.c's job for exactly that path (ShadowMountPlus lets the title go first when it lists exactly this data);
 *             move    a verified copy to the target (gamecopy.c: every file is read back and compared; a checksum file comes with it), and only
 *                     when that ended well the original is deleted as above. A move that fails leaves the original where it was.
 *
 * The result of the job must be there before anything is deleted (the hook hands over where it lies). Saved games are never touched. Nothing is
 * overwritten. The list of choices is short (8) and in memory: what became of each stays in the history of the app (jobhist.c: the copy and the
 * delete are jobs of their own) and in a notification. */

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#ifndef PS5TM_HOST_TEST
#include <sys/param.h>
#include <sys/mount.h>
#endif

#include "origafter.h"
#include "ps5tm.h"
#include "third_party/cJSON.h"

#define OA_MAX            8
#define OA_IDLE_WAIT_S    (12 * 3600)        /* how long a choice waits for the app's other jobs to end */
#define OA_JOB_WAIT_S     (24 * 3600)        /* how long it follows its own copy or delete */
#define OA_SPACE_RESERVE  (256ull << 20)     /* a drive is never filled up completely (as gamecopy.c) */
#ifndef OA_RETRY_TRIES
#define OA_RETRY_TRIES    40                     /* how often (every 3 s: two minutes) it asks again while another job that started in the same moment gives up its place */
#endif
#define OA_STATE_FILE     OA_APP_DIR "/orig-open.json"   /* what is open, for the next start of the app (ps5tm_orig_recover) */
#define OA_APP_DIR        "/data/PS5-Cooling-Center"
/* folders of the console itself: no game goes there (below any drive's mount point) */
static const char *const OA_SYS_NAMES[] = { "/user", "/system", "/system_ex", "/preinst", "/update", "/pkg", "/sandbox", "/etaHEN" };
/* Where the console's drives hang: "" on the console; the host test sets a folder of its own (as gamedelete.c's DL_ROOT), and everything is then looked at below it. */
#ifndef OA_ROOT
#define OA_ROOT           ""
#endif
static const char *
oa_fs(char *buf, size_t n, const char *p) {
  snprintf(buf, n, "%s%s", OA_ROOT, p);
  return buf;
}
#define OA_LSTAT(p, st)    ({ char oa_b[700]; lstat(oa_fs(oa_b, sizeof(oa_b), (p)), (st)); })
#define OA_STATVFS(p, sv)  ({ char oa_b[700]; statvfs(oa_fs(oa_b, sizeof(oa_b), (p)), (sv)); })
#ifndef OA_SLEEP
#define OA_SLEEP(s)       sleep(s)        /* the host test makes the waits short */
#endif

enum { OA_PENDING, OA_ARMED, OA_WAIT, OA_RUN, OA_DONE, OA_FAILED, OA_SKIPPED };

typedef struct {
  int                used, state, taken;
  uint64_t           seq, updated_ms;
  char               kind[10];
  ps5tm_orig_req_t   r;
  uint64_t           armed_ms;           /* when the choice was noted: the end of a job that began before is another job's */
  int                probing;            /* the worker is looking whether the app is quiet: counts as busy for the others (ps5tm_orig_busy) */
  int                outcome;            /* -1 the job has not ended yet, 0 it did not end well, 1 it did, 2 it did but its result was only spot-checked */
  char               result[320];        /* where the job's result lies (from the hook), "" when not told */
  char               message[400];
  char               target[340];        /* move: where the original goes / went */
} oa_item_t;

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_wlock = PTHREAD_MUTEX_INITIALIZER;     /* one writer of the note file at a time (taken before g_lock, never while g_lock is held) */
static oa_item_t       g_it[OA_MAX];
static uint64_t        g_seq;
static int             g_worker;
static pthread_t       g_worker_th;                                    /* valid while g_worker is 1 */

/* ------------------------------------------------------------------ the drives */

typedef struct {
  char     mount[48];                    /* "/data" for the internal SSD, else the drive's own mount point */
  char     label[40];
  char     kind[12];                     /* "internal", "m2", "usb" */
  uint64_t free_bytes;
} oa_drive_t;

#define OA_DRIVES (PS5TM_MAX_VOLUMES + 1)

/* The drives the console has now that a game may lie on or be moved to. */
static int
list_drives(oa_drive_t *out, int max) {
  ps5tm_sysinfo_t *info = malloc(sizeof(*info));
  if(!info) return 0;
  ps5tm_sysinfo_get(info);
  int n = 0, have_internal = 0;
  for(unsigned i = 0; i < info->volume_count && n < max; i++) {
    const ps5tm_volume_t *v = &info->volumes[i];
    int internal = !strcmp(v->path, "/user") || !strcmp(v->path, "/data");
    int m2 = !strncmp(v->path, "/mnt/ext", 8), usb = !strncmp(v->path, "/mnt/usb", 8);
    if(!internal && !m2 && !usb) continue;
    if(internal && have_internal) continue;
    oa_drive_t *d = &out[n++];
    memset(d, 0, sizeof(*d));
    if(internal) {
      have_internal = 1;
      snprintf(d->mount, sizeof(d->mount), "/data");
      snprintf(d->label, sizeof(d->label), "Interne SSD");
      snprintf(d->kind, sizeof(d->kind), "internal");
    } else {
      snprintf(d->mount, sizeof(d->mount), "%s", v->path);
      snprintf(d->label, sizeof(d->label), "%s", v->label[0] ? v->label : v->path);
      snprintf(d->kind, sizeof(d->kind), "%s", m2 ? "m2" : "usb");
    }
    d->free_bytes = v->free_bytes;
  }
  free(info);
  return n;
}

/* The drive a path lies on: the longest mount point it lies below; -1 when none. */
static int
drive_of(const char *path, const oa_drive_t *d, int n) {
  int best = -1;
  size_t bl = 0;
  for(int i = 0; i < n; i++) {
    size_t l = strlen(d[i].mount);
    if(ps5tm_orig_under(path, d[i].mount) && l >= bl) { best = i; bl = l; }
  }
  return best;
}

/* ------------------------------------------------------------------ the original */

/* The title's name and the folder or the image file it runs from: the same source the copy and the conversion take. */
static int
source_of(const char *title, char *name, size_t nn, char *path, size_t pn) {
  if(ps5tm_library_copy_info(title, name, nn, path, pn) == 0 && strncmp(path, "/mnt/shadowmnt", 14)) return 0;
  ps5tm_smp_game_t s;
  ps5tm_libdel_t li;
  if(ps5tm_smp_find(title, &s) == 0 && s.path[0] == '/' && strncmp(s.path, "/mnt/shadowmnt", 14) && strcmp(s.source_type, "pkg")) {
    snprintf(path, pn, "%s", s.path);
    snprintf(name, nn, "%s", ps5tm_library_delete_info(title, &li) == 0 && li.name[0] ? li.name : title);
    return 0;
  }
  return -1;
}

static const char *
kind_of(mode_t m) {
  return S_ISDIR(m) ? "folder" : S_ISREG(m) ? "image" : "other";
}

/* The size of the original: an image file's own, a folder's as the library knows it (cached), else walked once (gamecopy.c's cache). */
static uint64_t
size_of_original(const char *title, const char *path, const struct stat *st) {
  if(S_ISREG(st->st_mode)) return (uint64_t)st->st_size;
  ps5tm_libdel_t li;
  if(ps5tm_library_delete_info(title, &li) == 0 && li.size > 0 && !strcmp(li.path, path)) return (uint64_t)li.size;
  uint64_t bytes = 0, largest = 0;
  return ps5tm_gamecopy_measure(path, &bytes, &largest) == 0 ? bytes : 0;
}

/* ------------------------------------------------------------------ the target of a move */

typedef struct {
  char     dest[300];                    /* the folder, normalised */
  char     target[340];                  /* where the original will lie: dest/<its own name> */
  char     mount[48], label[40];
  uint64_t free_bytes, need_bytes;
  int      smp;                          /* ShadowMountPlus would search there */
  int      rename;                       /* the target is on the file system of the original: a rename moves it, no copy and no room needed */
} oa_dest_t;

/* The kind of file system the drive with this mount point has (the kernel's name: "msdosfs" for FAT32), "" when not known. */
#ifdef PS5TM_HOST_TEST
const char *g_oa_test_fstype;                   /* the host test sets it */
int g_oa_test_rename;                          /* the host test: 1 lets a target on the same file system (every one there) be renamed to */
#endif
static void
oa_fstype(const char *mount, char *out, size_t n) {
  out[0] = '\0';
#ifdef PS5TM_HOST_TEST
  (void)mount;
  snprintf(out, n, "%s", g_oa_test_fstype ? g_oa_test_fstype : "");
#else
  struct statfs sf;
  char b[700];
  if(statfs(oa_fs(b, sizeof(b), mount), &sf) == 0) snprintf(out, n, "%s", sf.f_fstypename);
#endif
}

/* 0 when a move of the original at path to dest_in may go ahead, else -1 with the reason in why (German, for the person). */
static int
check_dest(const char *path, const char *dest_in, uint64_t size, oa_dest_t *d, char *why, size_t wn, int allow_rename) {
  memset(d, 0, sizeof(*d));
  d->need_bytes = size + OA_SPACE_RESERVE;
  if(!dest_in || !dest_in[0]) { snprintf(why, wn, "Es ist kein Zielordner gewählt."); return -1; }
  if(ps5tm_orig_normalize(dest_in, d->dest, sizeof(d->dest)) != 0) {
    snprintf(why, wn, "Der Zielordner ist ungültig: Er muss ein vollständiger Pfad sein, ohne „.“ und „..“.");
    return -1;
  }
  const char *base = ps5tm_orig_basename(path);
  if(!ps5tm_orig_name_ok(base) || snprintf(d->target, sizeof(d->target), "%s/%s", !strcmp(d->dest, "/") ? "" : d->dest, base) >= (int)sizeof(d->target) ||
     strlen(d->target) >= 319) {
    snprintf(why, wn, "Der Pfad am Ziel wäre zu lang.");
    return -1;
  }
  oa_drive_t dr[OA_DRIVES];
  int nd = list_drives(dr, OA_DRIVES);
  int di = drive_of(d->dest, dr, nd);
  if(di < 0) {
    snprintf(why, wn, "Der Zielordner liegt auf keinem Laufwerk der Konsole (interne SSD, M.2-Erweiterung oder USB).");
    return -1;
  }
  snprintf(d->mount, sizeof(d->mount), "%s", dr[di].mount);
  snprintf(d->label, sizeof(d->label), "%s", dr[di].label);
  d->free_bytes = dr[di].free_bytes;
  if(ps5tm_orig_under(d->dest, OA_APP_DIR)) {
    snprintf(why, wn, "In den Ordner der App selbst kommt kein Spiel.");
    return -1;
  }
  if(ps5tm_orig_under(d->dest, path)) {
    snprintf(why, wn, "Der Zielordner liegt im Original selbst.");
    return -1;
  }
  if(!strcasecmp(d->target, path)) {
    snprintf(why, wn, "Das Original liegt schon in diesem Ordner.");
    return -1;
  }
  for(size_t i = 0; i < sizeof(OA_SYS_NAMES) / sizeof(OA_SYS_NAMES[0]); i++) {
    char sys[400];
    snprintf(sys, sizeof(sys), "%s%s", !strcmp(d->mount, "/") ? "" : d->mount, OA_SYS_NAMES[i]);
    if(ps5tm_orig_under(d->dest, sys)) {
      snprintf(why, wn, "In diesem Systemordner liegt kein Spiel.");
      return -1;
    }
  }
  /* ShadowMountPlus: <drive>/homebrew, <drive>/etaHEN/games and the roots of USB and M.2 drives down to two levels. The folder PS5-Sicherung/Spiele is
     three levels below a drive and never found. */
  d->smp = ps5tm_orig_smp_finds(d->target, d->mount);
  if(d->smp) {
    snprintf(why, wn, "ShadowMountPlus sucht in diesem Ordner nach Spielen und würde das Spiel dort wieder einbinden. Bitte einen Ordner wählen, den es nicht durchsucht, zum Beispiel %s/%s.",
             !strcmp(d->mount, "/data") ? "/data" : d->mount, PS5TM_ORIG_FOLDER);
    return -1;
  }
  /* what exists of the way must be plain folders (no link that carries the copy somewhere else); the deepest one that exists says on which file system the target lies */
  struct stat anc;
  int have_anc = OA_LSTAT(d->mount, &anc) == 0;
  {
    char part[300];
    size_t ml = strlen(d->mount), l = strlen(d->dest);
    for(size_t i = ml; i <= l; i++) {
      if(d->dest[i] != '/' && d->dest[i] != '\0') continue;
      if(i == ml) continue;
      memcpy(part, d->dest, i);
      part[i] = '\0';
      struct stat ps;
      if(OA_LSTAT(part, &ps) != 0) break;                              /* from here on it does not exist: the copy makes it */
      if(S_ISLNK(ps.st_mode) || !S_ISDIR(ps.st_mode)) {
        snprintf(why, wn, "Auf dem Weg zum Zielordner liegt eine Verknüpfung oder eine Datei: %.200s", part);
        return -1;
      }
      anc = ps;
      have_anc = 1;
    }
  }
  struct stat ts;
  const int target_exists = OA_LSTAT(d->target, &ts) == 0;
  /* what an earlier copy of this app left unfinished (its mark is in it) is not somebody's: the copy replaces it (gamecopy.c) */
  if(target_exists && !ps5tm_gamecopy_leftover(d->target)) {
    snprintf(why, wn, "Am Ziel gibt es „%.150s“ schon.", d->target);
    return -1;
  }
  /* A move inside one file system needs no copy: the original is renamed (gamedelete.c), so no room is needed and the 4 GiB limit of FAT32 does not matter. A leftover at the
     target, or a rename the file system refuses (EXDEV), leaves the copy. */
  struct stat os, ss;
  char sums[400];
  const int sums_there = snprintf(sums, sizeof(sums), "%s.sha256", d->target) < (int)sizeof(sums) && OA_LSTAT(sums, &ss) == 0;     /* the checksum file goes along with the rename */
  d->rename = allow_rename && !target_exists && !sums_there && have_anc && OA_LSTAT(path, &os) == 0 && (uint64_t)anc.st_dev == (uint64_t)os.st_dev;
#ifdef PS5TM_HOST_TEST
  if(!g_oa_test_rename) d->rename = 0;
#endif
  if(d->rename) d->need_bytes = 0;
  /* a FAT32 drive takes no file of 4 GiB or more: said now, not after hours (the copy would refuse it only when it starts) */
  if(!d->rename) {
    char fst[16];
    oa_fstype(d->mount, fst, sizeof(fst));
    if(!strcmp(fst, "msdosfs")) {
      struct stat ps;
      uint64_t bytes = 0, largest = 0;
      if(OA_LSTAT(path, &ps) == 0 && S_ISREG(ps.st_mode)) largest = (uint64_t)ps.st_size;
      else (void)ps5tm_gamecopy_measure(path, &bytes, &largest);
      if(ps5tm_orig_fat32_refuses(fst, largest)) {
        snprintf(why, wn, "Das Ziel ist mit FAT32 formatiert und nimmt keine Datei über 4 GB auf. Dieses Spiel hat größere.");
        return -1;
      }
    }
  }
  struct statvfs sv;
  if(OA_STATVFS(d->mount, &sv) == 0) {                                 /* the drive's room now, not as of the last look at the list of drives */
    uint64_t fr = sv.f_frsize ? sv.f_frsize : sv.f_bsize;
    d->free_bytes = (uint64_t)sv.f_bavail * fr;
  }
  if(!d->rename && d->free_bytes < d->need_bytes) {
    char a[24], b[24];
    ps5tm_orig_fmt_bytes(a, sizeof(a), d->free_bytes);
    ps5tm_orig_fmt_bytes(b, sizeof(b), d->need_bytes);
    snprintf(why, wn, "Auf dem Ziel ist nicht genug Platz: frei %s, gebraucht %s.", a, b);
    return -1;
  }
  return 0;
}

/* ------------------------------------------------------------------ prepare / arm / commit */

int
ps5tm_orig_prepare_for(const char *kind, const char *title_id, const char *after, const char *dest, const char *expect_path, ps5tm_orig_req_t *r, char *err, size_t err_len) {
  memset(r, 0, sizeof(*r));
  err[0] = '\0';
  if(!after || !after[0] || !strcmp(after, "keep")) return 0;
  if(!strcmp(after, "delete")) r->mode = PS5TM_ORIG_DELETE;
  else if(!strcmp(after, "move")) r->mode = PS5TM_ORIG_MOVE;
  else { snprintf(err, err_len, "Ungültige Angabe, was mit dem Original geschehen soll."); return 400; }
  if(!kind || !title_id || !title_id[0] || strlen(title_id) >= sizeof(r->title)) { snprintf(err, err_len, "Ungültige Titel-ID."); return 400; }
  snprintf(r->title, sizeof(r->title), "%s", title_id);
  if(source_of(title_id, r->name, sizeof(r->name), r->path, sizeof(r->path)) != 0) {
    snprintf(err, err_len, "Zu diesem Spiel gibt es kein Original, das sich löschen oder verschieben lässt.");
    return 404;
  }
  /* the page showed the person one original and asked about exactly that one (audit 18.11.2026, F-301): where the title is listed from now must be the same place */
  if(expect_path && expect_path[0] && strcmp(expect_path, r->path)) {
    snprintf(err, err_len, "Das Original liegt inzwischen woanders als bestätigt (jetzt: %.200s). Bitte die Auswahl noch einmal öffnen.", r->path);
    return 409;
  }
  char why[400];
  if(ps5tm_gamedelete_original_ok(title_id, r->path, why, sizeof(why)) != 0) {
    if(r->mode == PS5TM_ORIG_MOVE) snprintf(err, err_len, "Das Original lässt sich nicht verschieben: %s", why);
    else snprintf(err, err_len, "Das Original lässt sich nicht löschen: %s", why);
    return 409;
  }
  struct stat st;
  if(OA_LSTAT(r->path, &st) != 0) { snprintf(err, err_len, "Das Original ist nicht mehr da."); return 409; }
  r->dev = (uint64_t)st.st_dev;
  r->ino = (uint64_t)st.st_ino;
  if(r->mode == PS5TM_ORIG_MOVE) {
    oa_dest_t d;
    if(strlen(dest ? dest : "") >= sizeof(r->dest)) { snprintf(err, err_len, "Der Zielordner ist zu lang."); return 400; }
    if(check_dest(r->path, dest, size_of_original(title_id, r->path, &st), &d, why, sizeof(why), 1) != 0) {
      snprintf(err, err_len, "%s", why);
      return 409;
    }
    snprintf(r->dest, sizeof(r->dest), "%s", d.dest);
  }
  return 0;
}

int
ps5tm_orig_prepare(const char *kind, const char *title_id, const char *after, const char *dest, ps5tm_orig_req_t *r, char *err, size_t err_len) {
  return ps5tm_orig_prepare_for(kind, title_id, after, dest, NULL, r, err, err_len);
}

int
ps5tm_orig_prepare_body(const char *kind, const char *title_id, const cJSON *body, ps5tm_orig_req_t *r, char *err, size_t err_len) {
  const cJSON *ja = body ? cJSON_GetObjectItem(body, "orig_after") : NULL;
  const cJSON *jd = body ? cJSON_GetObjectItem(body, "orig_dest") : NULL;
  const cJSON *jp = body ? cJSON_GetObjectItem(body, "orig_path") : NULL;
  const char *after = cJSON_IsString(ja) ? ja->valuestring : "";
  if(!after[0] && kind && !strcmp(kind, "unpack") && body && cJSON_IsTrue(cJSON_GetObjectItem(body, "delete_source"))) after = "delete";     /* the older field of the unpack */
  return ps5tm_orig_prepare_for(kind, title_id, after, cJSON_IsString(jd) ? jd->valuestring : "", cJSON_IsString(jp) ? jp->valuestring : "", r, err, err_len);
}

static oa_item_t *
find_open_locked(const char *kind, const char *title) {
  for(int i = 0; i < OA_MAX; i++)
    if(g_it[i].used && (g_it[i].state == OA_PENDING || g_it[i].state == OA_ARMED) && !strcmp(g_it[i].kind, kind) && !strcmp(g_it[i].r.title, title))
      return &g_it[i];
  return NULL;
}

static oa_item_t *
find_seq_locked(uint64_t seq) {
  for(int i = 0; i < OA_MAX; i++) if(g_it[i].used && g_it[i].seq == seq) return &g_it[i];
  return NULL;
}

static void
touch_locked(oa_item_t *it, int state, const char *msg) {
  it->state = state;
  if(msg) snprintf(it->message, sizeof(it->message), "%s", msg);
  it->updated_ms = ps5tm_mono_ms();
}

int
ps5tm_orig_arm(const ps5tm_orig_req_t *r, const char *kind) {
  if(!r || r->mode == PS5TM_ORIG_KEEP) return 0;
  pthread_mutex_lock(&g_lock);
  oa_item_t *it = find_open_locked(kind, r->title);
  /* an open choice for the same kind of job of the same title belongs to a job that runs (or is about to): it is not replaced, the other request is refused; only one
     that is a day old is taken for the leftover of a job whose end was never seen */
  if(it && ps5tm_mono_ms() - it->updated_ms < 24ull * 3600 * 1000) { pthread_mutex_unlock(&g_lock); return -2; }
  if(!it) {
    for(int i = 0; i < OA_MAX && !it; i++) if(!g_it[i].used) it = &g_it[i];
    if(!it) {                                                /* the oldest one that has ended */
      for(int i = 0; i < OA_MAX; i++)
        if(g_it[i].state >= OA_DONE && (!it || g_it[i].seq < it->seq)) it = &g_it[i];
    }
  }
  if(!it) { pthread_mutex_unlock(&g_lock); return -1; }
  memset(it, 0, sizeof(*it));
  it->used = 1;
  it->seq = ++g_seq;
  snprintf(it->kind, sizeof(it->kind), "%s", kind);
  it->r = *r;
  it->outcome = -1;
  it->armed_ms = ps5tm_mono_ms();
  touch_locked(it, OA_PENDING, "");
  pthread_mutex_unlock(&g_lock);
  return 0;
}

static void *worker(void *arg);
static void persist(void);

/* Starts the worker when it is not there. */
static void
ensure_worker(void) {
  pthread_mutex_lock(&g_lock);
  if(g_worker) { pthread_mutex_unlock(&g_lock); return; }
  g_worker = 1;
  pthread_mutex_unlock(&g_lock);
  pthread_attr_t at;
  pthread_attr_init(&at);
  pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
  pthread_attr_setstacksize(&at, 512 * 1024);
  pthread_t th;
  int rc = pthread_create(&th, &at, worker, NULL);
  pthread_attr_destroy(&at);
  if(rc != 0) {
    pthread_mutex_lock(&g_lock);
    g_worker = 0;
    for(int i = 0; i < OA_MAX; i++)
      if(g_it[i].used && g_it[i].state == OA_WAIT) touch_locked(&g_it[i], OA_FAILED, "Das Original blieb liegen: Der Vorgang dafür ließ sich nicht starten.");
    pthread_mutex_unlock(&g_lock);
    persist();
  }
}

/* The job of a choice that is firm has ended: carry it out (1) or leave the original alone (0). The caller holds g_lock and starts the worker when
   this returns 1. */
static int
settle_locked(oa_item_t *it) {
  if(it->outcome == 2 && it->r.mode == PS5TM_ORIG_DELETE) {                 /* a spot check is no reason to throw 60 GB away; a move is: its copy is checked on its own */
    char m[400];
    if(!strcmp(it->kind, "unpack"))
      snprintf(m, sizeof(m), "Das Original „%.120s“ blieb liegen, weil ShadowMountPlus das Entpacken ohne Erfolgswert beendet hat und sich das Ergebnis nicht prüfen ließ. Bitte das entpackte Spiel prüfen und das Abbild von Hand löschen.", it->r.name);
    else
      snprintf(m, sizeof(m), "Das Original „%.120s“ blieb liegen, weil das Ergebnis nur schnell geprüft wurde. Zum Löschen bitte die gründliche Prüfung wählen.", it->r.name);
    touch_locked(it, OA_SKIPPED, m);
    PS5TM_INFO("orig_skipped", "Original von %s bleibt liegen: das Ergebnis (%s) wurde nur schnell geprüft.", it->r.title, it->kind);
    return 0;
  }
  if(it->outcome >= 1) {
    touch_locked(it, OA_WAIT, it->r.mode == PS5TM_ORIG_MOVE ? "Das Original wird verschoben, sobald nichts anderes mehr läuft."
                                                            : "Das Original wird gelöscht, sobald nichts anderes mehr läuft.");
    return 1;
  }
  char m[300];
  snprintf(m, sizeof(m), "Das Original „%.120s“ blieb liegen, weil der Vorgang nicht erfolgreich war.", it->r.name);
  touch_locked(it, OA_SKIPPED, m);
  PS5TM_INFO("orig_skipped", "Original von %s bleibt liegen: der Vorgang (%s) war nicht erfolgreich.", it->r.title, it->kind);
  return 0;
}

void
ps5tm_orig_commit(const ps5tm_orig_req_t *r, const char *kind, int started) {
  if(!r || r->mode == PS5TM_ORIG_KEEP) return;
  int go = 0, touched = 0;
  pthread_mutex_lock(&g_lock);
  for(int i = 0; i < OA_MAX; i++) {
    oa_item_t *it = &g_it[i];
    if(!it->used || it->state != OA_PENDING || strcmp(it->kind, kind) || strcmp(it->r.title, r->title)) continue;
    if(!started) { memset(it, 0, sizeof(*it)); break; }
    touch_locked(it, OA_ARMED, "");
    touched = 1;
    if(it->outcome >= 0) go = settle_locked(it);              /* the job ended before this was made firm */
    break;
  }
  pthread_mutex_unlock(&g_lock);
  if(touched) persist();                                      /* an armed choice is noted too: a restart in the middle of the job is reported (audit round 3, G-5) */
  if(go) ensure_worker();
}

void
ps5tm_orig_job_done(const char *kind, const char *title_id, int ok, const char *target, uint64_t started_ms) {
  int go = 0, touched = 0;
  pthread_mutex_lock(&g_lock);
  oa_item_t *it = find_open_locked(kind, title_id);
  /* a job that began before this choice was noted is another job of the same kind and title (a direct start and a queue entry): its end is not this one's */
  if(it && started_ms != 0 && started_ms < it->armed_ms) it = NULL;
  if(it) {
    it->outcome = ok < 0 ? 0 : ok > 2 ? 2 : ok;
    snprintf(it->result, sizeof(it->result), "%s", target ? target : "");
    if(it->state == OA_ARMED) { go = settle_locked(it); touched = 1; }
  }
  pthread_mutex_unlock(&g_lock);
  if(touched) persist();
  if(go) ensure_worker();
}

void
ps5tm_orig_gate(int max_s) {
  for(int s = 0; s < max_s; s++) {
    int waiting = 0;
    const int busy = ps5tm_gameconvert_busy();                 /* first the job, then the notes: a job that ends in between has left its note already */
    pthread_mutex_lock(&g_lock);
    for(int i = 0; i < OA_MAX; i++) if(g_it[i].used && (g_it[i].state == OA_WAIT || g_it[i].state == OA_RUN)) waiting = 1;
    pthread_mutex_unlock(&g_lock);
    if(!waiting && !busy) return;
    OA_SLEEP(1);
  }
}

/* ------------------------------------------------------------------ the worker */

static void
finish(uint64_t seq, int state, const char *msg, const char *target) {
  int changed = 0;
  pthread_mutex_lock(&g_lock);
  oa_item_t *it = find_seq_locked(seq);
  if(it) {
    changed = it->state != state;
    touch_locked(it, state, msg);
    if(target) snprintf(it->target, sizeof(it->target), "%s", target);
  }
  pthread_mutex_unlock(&g_lock);
  if(changed) persist();                                   /* only when the state changed: a copy reports its progress every two seconds */
}

static void
progress(uint64_t seq, const char *msg, const char *target) {
  finish(seq, OA_RUN, msg, target);
}

int
ps5tm_orig_busy(void) {
  pthread_mutex_lock(&g_lock);
  const int me = g_worker && pthread_equal(g_worker_th, pthread_self());          /* the thread that carries a choice out may start the copy and the delete it needs */
  int run = 0;
  for(int i = 0; i < OA_MAX; i++) if(g_it[i].used && (g_it[i].state == OA_RUN || g_it[i].probing)) run = 1;
  pthread_mutex_unlock(&g_lock);
  return run && !me;
}

static void
set_probing(uint64_t seq, int v) {
  pthread_mutex_lock(&g_lock);
  oa_item_t *it = find_seq_locked(seq);
  if(it) it->probing = v;
  pthread_mutex_unlock(&g_lock);
}

/* Waits until none of the app's jobs runs any more. 0 when it is so (the choice is then "running"), -1 when it did not come to that.
   While it looks, the choice counts as busy for the others (ps5tm_orig_busy): a job that starts in that moment finds the place taken and stands down (its own second
   look). When all is quiet, the state turns to "running" in the same step, so that no job slips in between the look and the work (audit round 3, G-2). */
static int
wait_idle(uint64_t seq, int move) {
  char why[160];
  for(int s = 0; s < OA_IDLE_WAIT_S; s += 3) {
    set_probing(seq, 1);
    if(!ps5tm_jobs_busy_except(-1, PS5TM_JOBS_NO_SMP, why, sizeof(why)) && !ps5tm_gamecopy_busy() && !ps5tm_gameconvert_busy() && !ps5tm_gamedelete_busy() &&
       !ps5tm_jobs_busy_except(-1, 0, why, sizeof(why))) {                /* only now asked of ShadowMountPlus: does it still move or unpack something? */
      pthread_mutex_lock(&g_lock);
      oa_item_t *it = find_seq_locked(seq);
      if(it) { it->state = OA_RUN; it->probing = 0; it->updated_ms = ps5tm_mono_ms(); }
      pthread_mutex_unlock(&g_lock);
      persist();
      return 0;
    }
    set_probing(seq, 0);
    if(s == 0) finish(seq, OA_WAIT, move ? "Das Original wird verschoben, sobald nichts anderes mehr läuft." : "Das Original wird gelöscht, sobald nichts anderes mehr läuft.", NULL);
    OA_SLEEP(3);
  }
  return -1;
}

/* 1 when the folder at path still has the markers of a game (sce_sys/param.json or param.sfo). */
static int
has_markers(const char *path) {
  static const char *const marks[] = { "/sce_sys/param.json", "/sce_sys/param.sfo" };
  for(size_t i = 0; i < sizeof(marks) / sizeof(marks[0]); i++) {
    char mk[400];
    struct stat ms;
    if(snprintf(mk, sizeof(mk), "%s%s", path, marks[i]) < (int)sizeof(mk) && OA_LSTAT(mk, &ms) == 0 && S_ISREG(ms.st_mode)) return 1;
  }
  return 0;
}

/* Deletes exactly it->r.path through gamedelete.c's job and follows that to its end. 0 when the original is gone, else -1 with the reason in why; *partial is 1 when
   part of it is gone already (its markers or files): it can no longer be played, and what is left lies where it was. What decides is the original itself, not the
   record of the delete job: another delete may have claimed that record in the meantime (audit 18.11.2026, F-310). */
static int
delete_original(const oa_item_t *it, char *why, size_t wn, int *partial) {
  *partial = 0;
  struct stat was;
  if(OA_LSTAT(it->r.path, &was) != 0) {
    snprintf(why, wn, "Das Original ist nicht mehr da.");
    return -1;
  }
  if((uint64_t)was.st_dev != it->r.dev || (uint64_t)was.st_ino != it->r.ino) {
    snprintf(why, wn, "An dieser Stelle liegt inzwischen etwas anderes als vorher. Es wurde nichts gelöscht.");
    return -1;
  }
  int rc = 0;
  for(int tries = 0; tries < OA_RETRY_TRIES; tries++) {      /* the other job that ended a moment ago (or started in this moment) may still hold its place */
    rc = ps5tm_gamedelete_original(it->r.title, it->r.name, it->r.path, &was, why, wn);
    if(rc != 409 || (!strstr(why, "gleichzeitig") && !strstr(why, "Gelöscht wird erst danach") && !strstr(why, "gerade schon etwas gelöscht"))) break;
    OA_SLEEP(3);
  }
  if(rc != 200) {
    /* another choice for the same original may have taken its markers already: then it cannot be played any more either */
    struct stat cur;
    if(S_ISDIR(was.st_mode) && OA_LSTAT(it->r.path, &cur) == 0 && cur.st_dev == was.st_dev && cur.st_ino == was.st_ino && !has_markers(it->r.path)) *partial = 1;
    return -1;
  }
  for(int s = 0; s < OA_JOB_WAIT_S; s++) {
    if(!ps5tm_gamedelete_busy()) break;
    OA_SLEEP(1);
  }
  cJSON *st = ps5tm_gamedelete_status();
  const cJSON *error = st ? cJSON_GetObjectItem(st, "error") : NULL, *kind = st ? cJSON_GetObjectItem(st, "kind") : NULL;
  const cJSON *id = st ? cJSON_GetObjectItem(st, "id") : NULL, *files = st ? cJSON_GetObjectItem(st, "files") : NULL;
  const int mine = !((cJSON_IsString(kind) && strcmp(kind->valuestring, "orig")) || (cJSON_IsString(id) && strcmp(id->valuestring, it->r.title)));
  struct stat now;
  memset(&now, 0, sizeof(now));
  const int lr = OA_LSTAT(it->r.path, &now);
  const int le = errno;
  if(lr != 0) {
    /* gone counts only while its drive is still there: with the drive pulled the path is gone as well, and the data lies untouched on the drive */
    char parent[330];
    struct stat ps;
    snprintf(parent, sizeof(parent), "%s", it->r.path);
    char *sl = strrchr(parent, '/');
    if(le == ENOENT && sl && sl > parent) {
      *sl = '\0';
      if(OA_LSTAT(parent, &ps) == 0 && (uint64_t)ps.st_dev == it->r.dev) {
        cJSON_Delete(st);
        return 0;
      }
    }
    snprintf(why, wn, "Das Laufwerk ist nicht mehr erreichbar. Ob das Original gelöscht wurde, ließ sich nicht prüfen.");
    cJSON_Delete(st);
    return -1;
  }
  snprintf(why, wn, "%s", mine && cJSON_IsString(error) && error->valuestring[0] ? error->valuestring : "Das Löschen ist gescheitert.");
  if(S_ISDIR(now.st_mode) && ((mine && cJSON_IsNumber(files) && files->valuedouble > 0) || !has_markers(it->r.path))) *partial = 1;
  cJSON_Delete(st);
  return -1;
}

/* Moves exactly it->r.path to target by a rename (gamedelete.c's job, which lets ShadowMountPlus go first). 0 when the original lies at target now (the same thing: device and
   inode as when the choice was made), -2 when the file system refuses a rename between the two places (EXDEV; nothing changed), else -1 with the reason in why. */
static int
rename_original(const oa_item_t *it, const char *target, char *why, size_t wn) {
  struct stat was;
  if(OA_LSTAT(it->r.path, &was) != 0) {
    snprintf(why, wn, "Das Original ist nicht mehr da.");
    return -1;
  }
  if((uint64_t)was.st_dev != it->r.dev || (uint64_t)was.st_ino != it->r.ino) {
    snprintf(why, wn, "An dieser Stelle liegt inzwischen etwas anderes als vorher. Es wurde nichts verschoben.");
    return -1;
  }
  int rc = 0;
  for(int tries = 0; tries < OA_RETRY_TRIES; tries++) {
    rc = ps5tm_gamedelete_original_rename(it->r.title, it->r.name, it->r.path, &was, target, why, wn);
    if(rc != 409 || (!strstr(why, "gleichzeitig") && !strstr(why, "Gelöscht wird erst danach") && !strstr(why, "gerade schon etwas gelöscht"))) break;
    OA_SLEEP(3);
  }
  if(rc != 200) return -1;
  for(int s = 0; s < OA_JOB_WAIT_S; s++) {
    if(!ps5tm_gamedelete_busy()) break;
    OA_SLEEP(1);
  }
  cJSON *st = ps5tm_gamedelete_status();
  const cJSON *error = st ? cJSON_GetObjectItem(st, "error") : NULL, *kind = st ? cJSON_GetObjectItem(st, "kind") : NULL, *id = st ? cJSON_GetObjectItem(st, "id") : NULL;
  const int mine = !((cJSON_IsString(kind) && strcmp(kind->valuestring, "orig")) || (cJSON_IsString(id) && strcmp(id->valuestring, it->r.title)));
  /* What decides is what lies there now: the same kind of thing on the same drive (a folder with its markers, a file of the same size) and nothing at the old place. The inode
     number is not compared: FAT and exFAT give a file another one when it changes its folder. */
  struct stat tn, gone;
  const int there = OA_LSTAT(target, &tn) == 0 && (uint64_t)tn.st_dev == it->r.dev &&
                    (S_ISDIR(tn.st_mode) ? S_ISDIR(was.st_mode) && has_markers(target) : S_ISREG(tn.st_mode) && S_ISREG(was.st_mode) && tn.st_size == was.st_size);
  if(there && OA_LSTAT(it->r.path, &gone) != 0 && errno == ENOENT) {
    cJSON_Delete(st);
    return 0;
  }
  const char *e = mine && cJSON_IsString(error) ? error->valuestring : "";
  const int xdev = strstr(e, "EXDEV") != NULL;
  snprintf(why, wn, "%s", e[0] ? e : "Das Verschieben ist gescheitert.");
  cJSON_Delete(st);
  return xdev ? -2 : -1;
}

/* Copies the original to its target through gamecopy.c (read back and compared) and follows that to its end. 0 when the copy is whole. */
static int
copy_original(uint64_t seq, const oa_item_t *it, const char *mount, const char *target, char *why, size_t wn) {
  int rc = 0;
  const uint64_t t0 = ps5tm_mono_ms();                        /* the copy's own record counts only when the copy began after this */
  for(int tries = 0; tries < OA_RETRY_TRIES; tries++) {
    rc = ps5tm_gamecopy_start_to(it->r.title, mount, it->r.dest, it->r.path, why, wn);
    if(rc != 409 || (!strstr(why, "gleichzeitig") && !strstr(why, "Es läuft schon eine Kopie"))) break;
    OA_SLEEP(3);
  }
  if(rc != 200) return -1;
  int lost = 0, was_mine = 0;
  for(int s = 0; s < OA_JOB_WAIT_S / 2; s++) {                /* a round is two seconds */
    OA_SLEEP(2);
    cJSON *st = ps5tm_gamecopy_status();
    if(!st) continue;                                         /* no answer (no memory): nothing is known, so nothing is decided */
    const cJSON *state = cJSON_GetObjectItem(st, "state");
    const cJSON *tg = cJSON_GetObjectItem(st, "target");
    const cJSON *act = cJSON_GetObjectItem(st, "active");
    const cJSON *error = cJSON_GetObjectItem(st, "error");
    const cJSON *tb = cJSON_GetObjectItem(st, "total_bytes"), *db = cJSON_GetObjectItem(st, "done_bytes"), *cb = cJSON_GetObjectItem(st, "checked_bytes");
    const cJSON *src = cJSON_GetObjectItem(st, "source"), *skp = cJSON_GetObjectItem(st, "skipped");
    int mine = cJSON_IsString(tg) && !strcmp(tg->valuestring, target);
    int end = 0, ok = 0;
    lost = mine ? 0 : lost + 1;
    was_mine = mine;
    if(mine && cJSON_IsString(src) && strcmp(src->valuestring, it->r.path)) {   /* the copy takes another source than the original: it is stopped, nothing is deleted */
      (void)ps5tm_gamecopy_cancel();
      end = 1;
      snprintf(why, wn, "Das Spiel liegt inzwischen woanders. Die Kopie wurde abgebrochen.");
    } else if(mine && !cJSON_IsTrue(act) && cJSON_IsString(state)) {
      if(!strcmp(state->valuestring, "done") && cJSON_IsNumber(skp) && skp->valuedouble > 0) {
        end = 1;                                            /* links and special files are left out of a copy: it is not the whole original */
        snprintf(why, wn, "Die Kopie hat Verknüpfungen oder Sonderdateien ausgelassen.");
      }
      else if(!strcmp(state->valuestring, "done")) { end = 1; ok = 1; }
      else if(!strcmp(state->valuestring, "failed") || !strcmp(state->valuestring, "cancelled")) {
        end = 1;
        snprintf(why, wn, "%s", !strcmp(state->valuestring, "cancelled") ? "Das Kopieren wurde abgebrochen."
                                 : cJSON_IsString(error) && error->valuestring[0] ? error->valuestring : "Das Kopieren ist gescheitert.");
      }
    } else if(!mine) {
      /* The record of the running copy is not there (another job claimed it, or a refused start cleared it): how this copy ended is kept on its own (gamecopy.c) */
      char le[400];
      const int lr = ps5tm_gamecopy_last_result(target, t0, le, sizeof(le));
      if(lr == 1) { end = 1; ok = 1; }
      else if(lr == 0) { end = 1; snprintf(why, wn, "%s", le); }
      else if(cJSON_IsString(state) && strcmp(state->valuestring, "idle") && !cJSON_IsTrue(act)) {
        end = 1;                                            /* another job has taken the place and this one's end was not seen */
        snprintf(why, wn, "Das Ergebnis des Kopierens ließ sich nicht mehr lesen, weil gleich danach ein anderer Vorgang begann.");
      } else if(lost > 30) {                                /* a minute of not seeing the copy at all: it is gone */
        end = 1;
        snprintf(why, wn, "Die Kopie ist nicht mehr zu sehen.");
      }
    }
    if(!end && mine && cJSON_IsNumber(tb) && tb->valuedouble > 0 && cJSON_IsNumber(db)) {           /* written, then read back: both count */
      double part = (db->valuedouble + (cJSON_IsNumber(cb) ? cb->valuedouble : 0)) / (2 * tb->valuedouble);
      char m[400];
      snprintf(m, sizeof(m), "Das Original „%.100s“ wird nach %.150s kopiert und geprüft … (%d %%)", it->r.name, it->r.dest, (int)(part > 1 ? 100 : part * 100));
      progress(seq, m, target);
    }
    cJSON_Delete(st);
    if(end) return ok ? 0 : -1;
  }
  if(was_mine) (void)ps5tm_gamecopy_cancel();                 /* too long: the copy is stopped, not left to write on beside the next choice */
  snprintf(why, wn, "Das Kopieren dauert zu lange.");
  return -1;
}

static void
process(uint64_t seq, const oa_item_t *it) {
  const int move = it->r.mode == PS5TM_ORIG_MOVE;
  char why[600] = "", msg[600];
  char target[340] = "";

  if(wait_idle(seq, move) != 0) {
    snprintf(msg, sizeof(msg), "Das Original „%.120s“ blieb liegen: Es lief zu lange etwas anderes.", it->r.name);
    finish(seq, OA_FAILED, msg, NULL);
    return;
  }
  /* From here on the original is in work: no other job of the app starts beside it (ps5tm_orig_busy), not between the copy and the delete either (audit 18.11.2026, F-302) */
  if(move) snprintf(msg, sizeof(msg), "Das Original „%.100s“ wird nach %.150s kopiert und geprüft …", it->r.name, it->r.dest);
  else snprintf(msg, sizeof(msg), "Das Original „%.120s“ wird gelöscht …", it->r.name);
  finish(seq, OA_RUN, msg, NULL);
  /* The result of the job must be known and be there, or nothing goes; and it must not be the original itself or lie in it (or the original in it). */
  if(!it->result[0]) {
    snprintf(msg, sizeof(msg), "Das Original „%.120s“ blieb liegen: Das Ergebnis ist nicht bekannt.", it->r.name);
    finish(seq, OA_FAILED, msg, NULL);
    return;
  }
  if(it->result[0]) {
    struct stat rs;
    if(ps5tm_orig_under(it->result, it->r.path) || ps5tm_orig_under(it->r.path, it->result)) {
      snprintf(msg, sizeof(msg), "Das Original „%.120s“ blieb liegen: Ergebnis und Original liegen ineinander.", it->r.name);
      finish(seq, OA_FAILED, msg, NULL);
      PS5TM_WARN("orig_failed", "Original von %s bleibt liegen: Ergebnis %.150s und Original liegen ineinander.", it->r.title, it->result);
      return;
    }
    if(OA_LSTAT(it->result, &rs) != 0) {
      snprintf(msg, sizeof(msg), "Das Original „%.120s“ blieb liegen: Das Ergebnis des Vorgangs wurde nicht gefunden (%.150s).", it->r.name, it->result);
      finish(seq, OA_FAILED, msg, NULL);
      PS5TM_WARN("orig_failed", "Original von %s bleibt liegen: das Ergebnis %.150s fehlt.", it->r.title, it->result);
      ps5tm_notify_plain("Das Original „%s“ blieb liegen: Das Ergebnis wurde nicht gefunden.", it->r.name);
      return;
    }
    if(!strcmp(it->kind, "unpack")) {          /* ShadowMountPlus makes the folder when it starts: its word that it is done is not enough, the game must be in it */
      static const char *const marks[] = { "/sce_sys/param.json", "/sce_sys/param.sfo" };
      int whole = 0;
      for(size_t i = 0; i < sizeof(marks) / sizeof(marks[0]); i++) {
        char mk[400];
        struct stat ms;
        if(snprintf(mk, sizeof(mk), "%s%s", it->result, marks[i]) < (int)sizeof(mk) && OA_LSTAT(mk, &ms) == 0 && S_ISREG(ms.st_mode)) whole = 1;
      }
      if(!S_ISDIR(rs.st_mode) || !whole) {
        snprintf(msg, sizeof(msg), "Das Original „%.120s“ blieb liegen: Das entpackte Spiel ist unvollständig (sce_sys/param.json fehlt).", it->r.name);
        finish(seq, OA_FAILED, msg, NULL);
        PS5TM_WARN("orig_failed", "Original von %s bleibt liegen: im entpackten Ordner %.150s fehlt sce_sys/param.json.", it->r.title, it->result);
        return;
      }
    }
  }
  /* The original must be what the choice was made for. */
  {
    struct stat os;
    if(OA_LSTAT(it->r.path, &os) != 0 || (uint64_t)os.st_dev != it->r.dev || (uint64_t)os.st_ino != it->r.ino) {
      snprintf(msg, sizeof(msg), "Das Original „%.120s“ blieb liegen: Es ist nicht mehr an seinem Platz.", it->r.name);
      finish(seq, OA_FAILED, msg, NULL);
      return;
    }
  }

  if(move) {
    oa_dest_t d;
    struct stat os;
    if(OA_LSTAT(it->r.path, &os) != 0 || check_dest(it->r.path, it->r.dest, size_of_original(it->r.title, it->r.path, &os), &d, why, sizeof(why), 1) != 0) {
      snprintf(msg, sizeof(msg), "Das Original „%.120s“ blieb liegen: %.300s", it->r.name, why[0] ? why : "Das Ziel ist nicht mehr in Ordnung.");
      finish(seq, OA_FAILED, msg, NULL);
      ps5tm_notify_plain("Das Original „%s“ blieb liegen: %.120s", it->r.name, why);
      return;
    }
    snprintf(target, sizeof(target), "%s", d.target);
    if(d.rename) {                                          /* the target lies on the original's own file system: a rename, no copy (audit round 2, F-309) */
      snprintf(msg, sizeof(msg), "Das Original „%.100s“ wird nach %.150s verschoben …", it->r.name, it->r.dest);
      progress(seq, msg, target);
      PS5TM_INFO("orig_move", "Original von %s wird nach %s verschoben (umbenannt, keine Kopie).", it->r.title, target);
      const int rr = rename_original(it, target, why, sizeof(why));
      if(rr == 0) {
        snprintf(msg, sizeof(msg), "Das Original „%.100s“ liegt jetzt in %.150s.", it->r.name, it->r.dest);
        finish(seq, OA_DONE, msg, target);
        PS5TM_INFO("orig_done", "Original von %s verschoben nach %s.", it->r.title, target);
        ps5tm_notify_plain("Das Original „%s“ liegt jetzt in %s.", it->r.name, it->r.dest);
        return;
      }
      if(rr == -1) {
        snprintf(msg, sizeof(msg), "Das Original „%.100s“ blieb liegen: %.300s", it->r.name, why);
        finish(seq, OA_FAILED, msg, NULL);
        PS5TM_WARN("orig_failed", "Original von %s blieb liegen: %.200s", it->r.title, why);
        ps5tm_notify_plain("Das Original „%s“ blieb liegen: %.120s", it->r.name, why);
        return;
      }
      /* the file system refuses the rename between these two places (EXDEV): the copy goes ahead after all, its room looked at again */
      if(check_dest(it->r.path, it->r.dest, size_of_original(it->r.title, it->r.path, &os), &d, why, sizeof(why), 0) != 0) {
        snprintf(msg, sizeof(msg), "Das Original „%.120s“ blieb liegen: %.300s", it->r.name, why);
        finish(seq, OA_FAILED, msg, NULL);
        ps5tm_notify_plain("Das Original „%s“ blieb liegen: %.120s", it->r.name, why);
        return;
      }
    }
    snprintf(msg, sizeof(msg), "Das Original „%.100s“ wird nach %.150s kopiert und geprüft …", it->r.name, it->r.dest);
    progress(seq, msg, target);
    PS5TM_INFO("orig_move", "Original von %s wird nach %s verschoben (kopiert, geprüft, dann gelöscht).", it->r.title, target);
    if(copy_original(seq, it, d.mount, target, why, sizeof(why)) != 0) {
      snprintf(msg, sizeof(msg), "Das Original „%.100s“ blieb liegen: %.300s", it->r.name, why);
      finish(seq, OA_FAILED, msg, NULL);
      PS5TM_WARN("orig_failed", "Original von %s blieb liegen: %.200s", it->r.title, why);
      ps5tm_notify_plain("Das Original „%s“ blieb liegen: %.120s", it->r.name, why);
      return;
    }
    struct stat ts;
    if(OA_LSTAT(target, &ts) != 0) {                          /* it said done, and it is not there: nothing is deleted */
      snprintf(msg, sizeof(msg), "Das Original „%.120s“ blieb liegen: Die Kopie wurde nicht gefunden.", it->r.name);
      finish(seq, OA_FAILED, msg, NULL);
      return;
    }
    snprintf(msg, sizeof(msg), "Das Original „%.100s“ ist kopiert und geprüft und wird jetzt gelöscht …", it->r.name);
    progress(seq, msg, target);
  } else {
    snprintf(msg, sizeof(msg), "Das Original „%.120s“ wird gelöscht …", it->r.name);
    progress(seq, msg, NULL);
  }

  int partial = 0;
  if(delete_original(it, why, sizeof(why), &partial) != 0) {
    if(partial) {                                           /* the markers or files are gone already: "blieb liegen" would be wrong */
      if(move) snprintf(msg, sizeof(msg), "Das Original „%.60s“ ist kopiert (nach %.100s), aber nur teilweise gelöscht und nicht mehr spielbar (Rest in %.100s): %.120s", it->r.name, target, it->r.path, why);
      else snprintf(msg, sizeof(msg), "Das Original „%.80s“ ist nur teilweise gelöscht und nicht mehr spielbar (Rest in %.120s): %.160s", it->r.name, it->r.path, why);
    } else if(move) snprintf(msg, sizeof(msg), "Das Original „%.60s“ ist kopiert (nach %.120s), ließ sich aber nicht löschen: %.200s", it->r.name, target, why);
    else snprintf(msg, sizeof(msg), "Das Original „%.120s“ blieb liegen: %.300s", it->r.name, why);
    finish(seq, OA_FAILED, msg, move ? target : NULL);
    PS5TM_WARN("orig_failed", "Original von %s ließ sich nicht löschen: %.200s", it->r.title, why);
    if(partial && move) ps5tm_notify_plain("Das Original „%s“ ist kopiert, aber nur teilweise gelöscht und nicht mehr spielbar (Rest in %s)", it->r.name, it->r.path);
    else if(partial) ps5tm_notify_plain("Das Original „%s“ ist nur teilweise gelöscht und nicht mehr spielbar (Rest in %s)", it->r.name, it->r.path);
    else if(move) ps5tm_notify_plain("Das Original „%s“ ist kopiert, ließ sich aber nicht löschen: %.100s", it->r.name, why);
    else ps5tm_notify_plain("Das Original „%s“ blieb liegen: %.120s", it->r.name, why);
    return;
  }
  if(move) {
    snprintf(msg, sizeof(msg), "Das Original „%.100s“ liegt jetzt in %.150s.", it->r.name, it->r.dest);
    finish(seq, OA_DONE, msg, target);
    PS5TM_INFO("orig_done", "Original von %s verschoben nach %s.", it->r.title, target);
    ps5tm_notify_plain("Das Original „%s“ liegt jetzt in %s.", it->r.name, it->r.dest);
  } else {
    snprintf(msg, sizeof(msg), "Das Original „%.120s“ wurde gelöscht.", it->r.name);
    finish(seq, OA_DONE, msg, NULL);
    PS5TM_INFO("orig_done", "Original von %s gelöscht.", it->r.title);
    ps5tm_notify_plain("Das Original „%s“ wurde gelöscht.", it->r.name);
  }
}

static void *
worker(void *arg) {
  (void)arg;
  pthread_mutex_lock(&g_lock);
  g_worker_th = pthread_self();                              /* ps5tm_orig_busy: this thread may start the copy and the delete it needs */
  pthread_mutex_unlock(&g_lock);
  ps5tm_powerguard_hold();                                  /* a long copy follows: the console stays awake */
  for(;;) {
    oa_item_t cur;
    int found = 0;
    pthread_mutex_lock(&g_lock);
    oa_item_t *pick = NULL;
    for(int i = 0; i < OA_MAX; i++)
      if(g_it[i].used && g_it[i].state == OA_WAIT && !g_it[i].taken && (!pick || g_it[i].seq < pick->seq)) pick = &g_it[i];
    if(pick) { pick->taken = 1; cur = *pick; found = 1; }
    else g_worker = 0;                                      /* decided under the lock the hook looks at */
    pthread_mutex_unlock(&g_lock);
    if(!found) break;
    process(cur.seq, &cur);
  }
  ps5tm_powerguard_release();
  return NULL;
}

/* ------------------------------------------------------------------ what is open, for the next start of the app */

/* What waits for the app's other jobs or is being carried out is noted in a file of the app, so that a restart in the middle (a new ELF sent, the console's own
   end of the app) does not make it vanish: the next start reports it (ps5tm_orig_recover). Never held under g_lock. */
static void
persist(void) {
  oa_item_t *snap = malloc(sizeof(g_it));
  if(!snap) return;
  pthread_mutex_lock(&g_wlock);
  pthread_mutex_lock(&g_lock);
  memcpy(snap, g_it, sizeof(g_it));
  pthread_mutex_unlock(&g_lock);
  cJSON *arr = cJSON_CreateArray();
  for(int i = 0; i < OA_MAX; i++) {
    const oa_item_t *it = &snap[i];
    if(!it->used || (it->state != OA_WAIT && it->state != OA_RUN && it->state != OA_ARMED)) continue;
    cJSON *e = cJSON_CreateObject();
    cJSON_AddStringToObject(e, "id", it->r.title);
    cJSON_AddStringToObject(e, "name", it->r.name);
    cJSON_AddStringToObject(e, "kind", it->kind);
    cJSON_AddStringToObject(e, "mode", it->r.mode == PS5TM_ORIG_MOVE ? "move" : "delete");
    cJSON_AddStringToObject(e, "state", it->state == OA_RUN ? "running" : "waiting");
    cJSON_AddStringToObject(e, "path", it->r.path);
    cJSON_AddStringToObject(e, "dest", it->r.dest);
    cJSON_AddItemToArray(arr, e);
  }
  char path[700], tmp[720];
  oa_fs(path, sizeof(path), OA_STATE_FILE);
  if(cJSON_GetArraySize(arr) == 0) {
    (void)unlink(path);
    cJSON_Delete(arr);
  } else {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "v", 1);
    cJSON_AddItemToObject(root, "items", arr);
    char *text = cJSON_PrintUnformatted(root);
    if(text) {
      snprintf(tmp, sizeof(tmp), "%s.tmp", path);
      FILE *f = fopen(tmp, "w");
      if(f) {
        const size_t n = strlen(text);
        int ok = fwrite(text, 1, n, f) == n;
        ok = fclose(f) == 0 && ok;
        if(ok) (void)rename(tmp, path);
        else (void)unlink(tmp);
      }
      cJSON_free(text);
    }
    cJSON_Delete(root);
  }
  pthread_mutex_unlock(&g_wlock);
  free(snap);
}

void
ps5tm_orig_recover(void) {
  static int done;                                            /* once per run of the app: what is open now is not a leftover */
  if(done) return;
  done = 1;
  char path[700];
  oa_fs(path, sizeof(path), OA_STATE_FILE);
  FILE *f = fopen(path, "r");
  if(!f) return;
  char *buf = malloc(65536);
  const size_t n = buf ? fread(buf, 1, 65535, f) : 0;
  fclose(f);
  (void)unlink(path);                                         /* reported once */
  if(!buf) return;
  buf[n] = '\0';
  cJSON *root = cJSON_Parse(buf);
  free(buf);
  const cJSON *items = root ? cJSON_GetObjectItem(root, "items") : NULL;
  const cJSON *e;
  cJSON_ArrayForEach(e, items) {
    const cJSON *jn = cJSON_GetObjectItem(e, "name"), *js = cJSON_GetObjectItem(e, "state"), *jp = cJSON_GetObjectItem(e, "path");
    const char *name = cJSON_IsString(jn) ? jn->valuestring : "?", *op = cJSON_IsString(jp) ? jp->valuestring : "?";
    char msg[500];
    if(cJSON_IsString(js) && !strcmp(js->valuestring, "running")) {
      snprintf(msg, sizeof(msg), "Das Löschen oder Verschieben des Originals von „%.80s“ wurde durch den Neustart der App unterbrochen. Bitte nachsehen, ob in %.120s noch ein Rest liegt.", name, op);
      ps5tm_notify_plain("Das Löschen oder Verschieben des Originals von „%s“ wurde durch den Neustart der App unterbrochen. Bitte nachsehen, ob in %s noch ein Rest liegt.", name, op);
    } else {
      snprintf(msg, sizeof(msg), "Die Wahl für das Original von „%.80s“ wurde durch den Neustart der App nicht ausgeführt. Das Original liegt unverändert in %.120s.", name, op);
      ps5tm_notify_plain("Die Wahl für das Original von „%s“ wurde durch den Neustart der App nicht ausgeführt. Das Original liegt unverändert in %s.", name, op);
    }
    PS5TM_WARN("orig_interrupted", "%.300s", msg);
    ps5tm_hist_add("delete", name, "Original", "failed", msg, 0, 0);
  }
  cJSON_Delete(root);
}

/* ------------------------------------------------------------------ what the pages ask */

cJSON *
ps5tm_orig_plan_json(const char *title_id, int *http) {
  char name[128], path[320];
  *http = 200;
  if(!title_id || !title_id[0] || source_of(title_id, name, sizeof(name), path, sizeof(path)) != 0) { *http = 404; return NULL; }
  struct stat st;
  int have = OA_LSTAT(path, &st) == 0;
  oa_drive_t dr[OA_DRIVES];
  int nd = list_drives(dr, OA_DRIVES);
  int src_drive = drive_of(path, dr, nd);

  cJSON *o = cJSON_CreateObject();
  cJSON_AddBoolToObject(o, "ok", 1);
  cJSON_AddStringToObject(o, "id", title_id);
  cJSON_AddStringToObject(o, "name", name);
  cJSON *s = cJSON_AddObjectToObject(o, "source");
  cJSON_AddStringToObject(s, "path", path);
  cJSON_AddStringToObject(s, "kind", have ? kind_of(st.st_mode) : "other");
  ps5tm_libdel_t li;
  double size = -1;
  if(have && S_ISREG(st.st_mode)) size = (double)st.st_size;
  else if(ps5tm_library_delete_info(title_id, &li) == 0 && li.size > 0 && !strcmp(li.path, path)) size = (double)li.size;
  cJSON_AddNumberToObject(s, "size_bytes", size);
  cJSON_AddStringToObject(s, "mount", src_drive >= 0 ? dr[src_drive].mount : "");
  cJSON_AddStringToObject(s, "label", src_drive >= 0 ? dr[src_drive].label : "");

  char why[400];
  int can = ps5tm_gamedelete_original_ok(title_id, path, why, sizeof(why)) == 0;
  cJSON_AddBoolToObject(o, "can_delete", can);
  cJSON_AddStringToObject(o, "delete_why", can ? "" : why);
  cJSON_AddBoolToObject(o, "can_move", can && nd > 0);
  cJSON_AddStringToObject(o, "move_why", can ? (nd > 0 ? "" : "Es gibt kein Laufwerk, auf das sich das Original verschieben ließe.") : why);
  cJSON_AddStringToObject(o, "default_folder", PS5TM_ORIG_FOLDER);
  cJSON *arr = cJSON_AddArrayToObject(o, "drives");
  for(int i = 0; i < nd; i++) {
    cJSON *d = cJSON_CreateObject();
    cJSON_AddStringToObject(d, "mount", dr[i].mount);
    cJSON_AddStringToObject(d, "label", dr[i].label);
    cJSON_AddStringToObject(d, "kind", dr[i].kind);
    cJSON_AddNumberToObject(d, "free_bytes", (double)dr[i].free_bytes);
    char def[400];
    snprintf(def, sizeof(def), "%s/%s", dr[i].mount, PS5TM_ORIG_FOLDER);
    cJSON_AddStringToObject(d, "default_dest", def);
    cJSON_AddItemToArray(arr, d);
  }
  return o;
}

cJSON *
ps5tm_orig_check_json(const char *title_id, const char *dest, int *http) {
  char name[128], path[320];
  *http = 200;
  if(!title_id || !title_id[0] || source_of(title_id, name, sizeof(name), path, sizeof(path)) != 0) { *http = 404; return NULL; }
  struct stat st;
  char why[500] = "", text[700];
  oa_dest_t d;
  memset(&d, 0, sizeof(d));
  uint64_t osz = 0;
  int ok = 0;
  if(OA_LSTAT(path, &st) != 0) snprintf(why, sizeof(why), "Das Original ist nicht mehr da.");
  else if(ps5tm_gamedelete_original_ok(title_id, path, why, sizeof(why)) == 0)
    { osz = size_of_original(title_id, path, &st); ok = check_dest(path, dest, osz, &d, why, sizeof(why), 1) == 0; }
  if(ok) {
    char a[24], b[24];
    ps5tm_orig_fmt_bytes(a, sizeof(a), osz);
    ps5tm_orig_fmt_bytes(b, sizeof(b), d.free_bytes);
    if(d.rename) snprintf(text, sizeof(text), "Das Original „%.100s“ (%s) wird nach %.150s verschoben, ohne zu kopieren (gleiches Laufwerk).", name, a, d.target);
    else snprintf(text, sizeof(text), "Das Original „%.100s“ (%s) wird nach %.150s verschoben. Frei auf dem Ziel: %s.", name, a, d.target, b);
  } else {
    snprintf(text, sizeof(text), "%s", why);
  }
  cJSON *o = cJSON_CreateObject();
  cJSON_AddBoolToObject(o, "ok", 1);
  cJSON_AddStringToObject(o, "level", ok ? "ok" : "error");
  cJSON_AddBoolToObject(o, "can_move", ok);
  cJSON_AddStringToObject(o, "text", text);
  cJSON_AddStringToObject(o, "dest", d.dest);
  cJSON_AddStringToObject(o, "target", d.target);
  cJSON_AddStringToObject(o, "mount", d.mount);
  cJSON_AddNumberToObject(o, "free_bytes", (double)d.free_bytes);
  cJSON_AddNumberToObject(o, "need_bytes", (double)d.need_bytes);
  cJSON_AddBoolToObject(o, "smp_searches", d.smp);
  cJSON_AddBoolToObject(o, "rename", d.rename);
  return o;
}

static const char *const k_state[] = { "waiting", "waiting", "waiting", "running", "done", "failed", "skipped" };

cJSON *
ps5tm_orig_status_json(void) {
  oa_item_t snap[OA_MAX];
  pthread_mutex_lock(&g_lock);
  memcpy(snap, g_it, sizeof(snap));
  pthread_mutex_unlock(&g_lock);
  /* newest first */
  int order[OA_MAX], n = 0;
  for(int i = 0; i < OA_MAX; i++) if(snap[i].used) order[n++] = i;
  for(int a = 1; a < n; a++) {
    int k = order[a], b = a - 1;
    while(b >= 0 && snap[order[b]].seq < snap[k].seq) { order[b + 1] = order[b]; b--; }
    order[b + 1] = k;
  }
  cJSON *o = cJSON_CreateObject();
  cJSON_AddBoolToObject(o, "ok", 1);
  cJSON *arr = cJSON_AddArrayToObject(o, "items");
  int active = 0;
  for(int j = 0; j < n; j++) {
    const oa_item_t *it = &snap[order[j]];
    if(it->state == OA_ARMED || it->state == OA_WAIT || it->state == OA_RUN) active = 1;
    cJSON *e = cJSON_CreateObject();
    cJSON_AddStringToObject(e, "id", it->r.title);
    cJSON_AddStringToObject(e, "name", it->r.name);
    cJSON_AddStringToObject(e, "kind", it->kind);
    cJSON_AddStringToObject(e, "mode", it->r.mode == PS5TM_ORIG_MOVE ? "move" : "delete");
    cJSON_AddStringToObject(e, "state", k_state[it->state]);
    cJSON_AddStringToObject(e, "message", it->message);
    cJSON_AddStringToObject(e, "target", it->target);
    cJSON_AddNumberToObject(e, "updated_ms", (double)it->updated_ms);
    cJSON_AddItemToArray(arr, e);
  }
  cJSON_AddBoolToObject(o, "active", active);
  return o;
}
