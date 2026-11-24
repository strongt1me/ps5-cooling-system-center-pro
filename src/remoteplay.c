/* Remote Play, the preparation on the console's side (24.11.2026, wish of the user): switch the service on, see which users may use it,
 * make a pairing PIN, list the paired devices. Played afterwards with chiaki-ng or ps5upload on the PC or the phone; this app is no
 * client and does not stream.
 *
 *   GET  /api/v1/remoteplay              the whole state (service, users, paired devices, PIN, backups)
 *   GET  /api/v1/remoteplay/pin          only the PIN (cheap: the pages ask it every second while a PIN is live)
 *   POST /api/v1/remoteplay/service      {"enabled":bool}            the system-wide switch
 *   POST /api/v1/remoteplay/user         {"slot":n,"enabled":bool}   the permission of one user (firmware 10.00 and later)
 *   POST /api/v1/remoteplay/pin                                      make a PIN (switches the service on when it is off)
 *   POST /api/v1/remoteplay/pin/cancel
 *
 * The offline activation of an account is the existing /api/v1/profile/account/activate (account.c).
 *
 * What is written and how it is kept safe:
 *  - Only three kinds of registry value are ever written here: the service switch, the permission of one user, and (account.c, with
 *    the same helper) the three values of an offline activation. Before every write the old values are saved to
 *    /data/PS5-Cooling-Center/remoteplay-backup/<date>-<what>.json (the newest 40 are kept); when the backup cannot be written,
 *    nothing is changed. After the write the value is read back and must be the one written.
 *  - Paired devices are only read: the console stores the user and a kind per device, no name, and how a registration is cleared
 *    properly is not known well enough to write blindly, so there is no "remove" here.
 *  - libSceRemoteplay is loaded on a thread of its own, never on a request thread (a Sony module that does not come back must not
 *    take the web server with it); a request waits for it for a limited time and then says so.
 *
 * The registry keys and the pairing state are in rpkeys.h and rpair.c (test/rpair). The facts about the module (call order, PIN
 * handling, status values, key bases) come from ps5upload's payload (phantomptr, GPL-3.0-or-later) and elf-arsenal (GPL-3.0-or-later);
 * the code is written again for this app.
 */
#include <dirent.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "dynsym.h"
#include "ps5tm.h"
#include "rpair.h"
#include "rpkeys.h"
#include "third_party/cJSON.h"

extern int sceRegMgrGetStr(int, char *, size_t)        __attribute__((weak));
extern int sceRegMgrGetBin(int, void *, size_t)        __attribute__((weak));
extern int sceRegMgrGetInt(int, int *)                 __attribute__((weak));
extern int sceRegMgrSetInt(int, int)                   __attribute__((weak));

#define RP_BACKUP_DIR   PS5TM_DATA_DIR "/remoteplay-backup"
#define RP_BACKUP_KEEP  40
#define RP_MODULE       "libSceRemoteplay.sprx"
/* "already initialised": what every call after the first returns, and it means ready */
#define RP_ERR_ALREADY_INITIALIZED ((int)0x80FC0003)

/* ---------------------------------------------------------------- registry */

/* The Sony lock is held for the call itself and nothing longer: the fan worker never waits for it (it only tries). A request thread
   waits for it for a limited time only: the worker of this file holds it inside the module's calls, and one that does not come back
   must not take the web server with it. 0 = not taken (the caller reports "not readable"/"not written"). */
#define RP_LOCK_MS 3000

static int
sony_lock_for(int ms) {
  for(int waited = 0;; waited += 10) {
    if(ps5tm_sony_api_trylock()) return 1;
    if(waited >= ms) return 0;
    usleep(10 * 1000);
  }
}

static int
reg_get_int(uint32_t key, int *v) {
  if(!sceRegMgrGetInt) return -1;
  if(!sony_lock_for(RP_LOCK_MS)) return -2;
  int rc = sceRegMgrGetInt((int)key, v);
  ps5tm_sony_api_unlock();
  return rc;
}

static int
reg_set_int(uint32_t key, int v) {
  if(!sceRegMgrSetInt) return -1;
  if(!sony_lock_for(RP_LOCK_MS)) return -2;
  int rc = sceRegMgrSetInt((int)key, v);
  ps5tm_sony_api_unlock();
  return rc;
}

/* ------------------------------------------------------------------ backup */

static pthread_mutex_t g_write = PTHREAD_MUTEX_INITIALIZER;     /* one write at a time, backup and change as one step */

static void
mkdir_backup(void) {
  (void)mkdir(PS5TM_DATA_DIR, 0777);
  (void)mkdir(RP_BACKUP_DIR, 0777);
}

static int
name_cmp_desc(const void *a, const void *b) {
  return strcmp((const char *)b, (const char *)a);
}

/* the names in the backup folder (only *.json), newest first (the names start with the date); returns how many (at most `max`) */
static int
list_backups(char (*names)[80], int max) {
  DIR *d = opendir(RP_BACKUP_DIR);
  if(!d) return 0;
  int n = 0;
  struct dirent *e;
  while((e = readdir(d)) != NULL && n < max) {
    size_t l = strlen(e->d_name);
    if(l < 6 || l >= 80 || strcmp(e->d_name + l - 5, ".json") != 0) continue;
    snprintf(names[n++], 80, "%s", e->d_name);
  }
  closedir(d);
  qsort(names, (size_t)n, 80, name_cmp_desc);
  return n;
}

/* 512 names of 80 bytes: on the heap, two requests may ask at once */
#define RP_NAMES_MAX 512

static void
prune_backups(void) {
  char (*names)[80] = malloc((size_t)RP_NAMES_MAX * 80);
  if(!names) return;
  int n = list_backups(names, RP_NAMES_MAX);
  for(int i = RP_BACKUP_KEEP; i < n; i++) {
    char p[160];
    snprintf(p, sizeof(p), "%s/%s", RP_BACKUP_DIR, names[i]);
    (void)unlink(p);
  }
  free(names);
}

/* Saves the current values of `keys` before they are changed. kinds[i]: 'i' an int, 'b' 8 raw bytes, 's' a string of up to 16 characters.
   0 = saved. A key that cannot be read is recorded as such (a key that was never set reads as an error on the console). */
int
ps5tm_regbackup(const char *what, const uint32_t *keys, const char *kinds, int n) {
  mkdir_backup();
  cJSON *o = cJSON_CreateObject();
  if(!o) return -1;
  time_t now = time(NULL);
  struct tm tm;
  gmtime_r(&now, &tm);
  char when[32], stamp[24], fw[16];
  strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S UTC", &tm);
  strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &tm);
  ps5tm_platform_firmware_text(ps5tm_platform_firmware(), fw, sizeof(fw));
  cJSON_AddStringToObject(o, "what", what);
  cJSON_AddStringToObject(o, "when", when);
  cJSON_AddStringToObject(o, "firmware", fw);
  cJSON *arr = cJSON_AddArrayToObject(o, "entries");
  for(int i = 0; i < n; i++) {
    cJSON *e = cJSON_CreateObject();
    cJSON_AddNumberToObject(e, "key", (double)keys[i]);
    int ok = 0;
    if(kinds[i] == 'i') {
      int v = 0;
      ok = reg_get_int(keys[i], &v) == 0;
      cJSON_AddStringToObject(e, "kind", "int");
      if(ok) cJSON_AddNumberToObject(e, "value", v);
    } else if(kinds[i] == 'b') {
      uint8_t raw[8] = {0};
      int rc = -1;
      if(sceRegMgrGetBin && sony_lock_for(RP_LOCK_MS)) {
        rc = sceRegMgrGetBin((int)keys[i], raw, sizeof(raw));
        ps5tm_sony_api_unlock();
      }
      ok = rc == 0;
      char hex[20];
      for(int j = 0; j < 8; j++) snprintf(hex + j * 2, 3, "%02x", raw[j]);
      cJSON_AddStringToObject(e, "kind", "bin8");
      if(ok) cJSON_AddStringToObject(e, "hex", hex);
    } else {
      char s[PS5TM_ACCOUNT_TYPE_MAX];
      memset(s, 0, sizeof(s));
      int rc = -1;
      if(sceRegMgrGetStr && sony_lock_for(RP_LOCK_MS)) {
        rc = sceRegMgrGetStr((int)keys[i], s, sizeof(s));
        ps5tm_sony_api_unlock();
      }
      ok = rc == 0;
      s[sizeof(s) - 1] = 0;
      cJSON_AddStringToObject(e, "kind", "str");
      if(ok) cJSON_AddStringToObject(e, "value", s);
    }
    cJSON_AddBoolToObject(e, "readable", ok);
    cJSON_AddItemToArray(arr, e);
  }
  char *txt = cJSON_Print(o);
  cJSON_Delete(o);
  if(!txt) return -1;

  int rc = -1;
  for(int k = 0; k < 20 && rc != 0; k++) {
    char path[200], tmp[210];
    if(k == 0) snprintf(path, sizeof(path), "%s/%s-%s.json", RP_BACKUP_DIR, stamp, what);
    else       snprintf(path, sizeof(path), "%s/%s-%s-%d.json", RP_BACKUP_DIR, stamp, what, k + 1);
    struct stat st;
    if(stat(path, &st) == 0) continue;                 /* the same second twice: the next name */
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    if(!f) break;
    size_t len = strlen(txt);
    int wrote = fwrite(txt, 1, len, f) == len && fflush(f) == 0;
    wrote = fclose(f) == 0 && wrote;
    if(wrote && rename(tmp, path) == 0) rc = 0;
    else (void)unlink(tmp);
    break;
  }
  free(txt);
  if(rc == 0) prune_backups();
  else PS5TM_WARN("remoteplay_backup_failed", "Remote Play: Die Sicherung der Einstellungen (%s) konnte nicht geschrieben werden.", what);
  return rc;
}

/* ---------------------------------------------------------- users, devices */

typedef struct {
  int  slot;
  char name[PS5TM_ACCOUNT_NAME_BUF];
  int  uid;                 /* the user id of the slot, 0 unknown */
  int  activated;           /* an account id is set and the type is "np" */
  int  has_id;
  uint8_t raw[8];
  int  allowed;             /* the permission of the user: 1 / 0, -1 not known (firmware before 10.00 or unreadable) */
} rp_user_t;

static int
collect_users(rp_user_t *out, int max, int per_user) {
  ps5tm_account_entry_t list[PS5TM_ACCOUNT_SLOTS];
  int n = ps5tm_account_list(list, PS5TM_ACCOUNT_SLOTS);
  int k = 0;
  for(int i = 0; i < n && k < max; i++) {
    rp_user_t *u = &out[k++];
    memset(u, 0, sizeof(*u));
    u->slot = list[i].slot;
    snprintf(u->name, sizeof(u->name), "%s", list[i].name);
    u->activated = list[i].active;
    u->has_id = list[i].id != 0;
    memcpy(u->raw, &list[i].id, 8);
    int v = 0;
    if(reg_get_int(ps5tm_rp_key_user_id((uint32_t)u->slot), &v) == 0) u->uid = v;
    u->allowed = -1;
    if(per_user && reg_get_int(ps5tm_rp_key_user_enable((uint32_t)u->slot), &v) == 0) u->allowed = v != 0;
  }
  return k;
}

/* The user a pairing is made for: the one at the console (foreground, else the initial user) when that one has an account, else the
   first user with an account. -1 when there is none. via: "foreground", "first" or "none". */
static int
pairing_slot(const rp_user_t *u, int n, const char **via) {
  *via = "none";
  int uid = -1;
  /* the lock is recursive: with it taken here (for a limited time) the call below does not wait again */
  if(sony_lock_for(RP_LOCK_MS)) {
    int ok = ps5tm_user_service_active_user(&uid) == 0 && uid > 0;
    ps5tm_sony_api_unlock();
    if(ok) {
      for(int i = 0; i < n; i++) {
        if(u[i].uid == uid && u[i].activated) { *via = "foreground"; return u[i].slot; }
      }
    }
  }
  for(int i = 0; i < n; i++) {
    if(u[i].activated) { *via = "first"; return u[i].slot; }
  }
  return -1;
}

static int
device_used(uint32_t slot, int *user, int *client) {
  int uid = 0;
  if(reg_get_int(ps5tm_rp_key_regist_user(slot), &uid) != 0) return 0;
  if(uid == 0 || uid == -1) return 0;
  if(user) *user = uid;
  if(client) {
    *client = 0;
    (void)reg_get_int(ps5tm_rp_key_regist_client(slot), client);
  }
  return 1;
}

/* the paired devices, or -1 when the registry cannot be read at all */
static int
device_count(void) {
  if(!sceRegMgrGetInt) return -1;
  int n = 0;
  for(uint32_t s = 1; s <= PS5TM_RP_DEVICE_SLOTS; s++) n += device_used(s, NULL, NULL);
  return n;
}

/* --------------------------------------------------- the module's own thread */

static ps5tm_rpair_t g_pair;
static pthread_once_t g_pair_once = PTHREAD_ONCE_INIT;

static void pair_init_once(void) { ps5tm_rpair_init(&g_pair); }

static int (*g_rp_init)(void *, size_t);
static int (*g_rp_gen_pin)(uint32_t *);
static int (*g_rp_confirm)(uint32_t *, uint32_t *);
static int (*g_rp_notify_err)(int);
static volatile int g_mod_state;                     /* 0 not tried, 1 loading, 2 ready, 3 not there */

/* Only on the worker's thread: finding the symbols loads the module. */
static void
resolve_module(void) {
  g_mod_state = 1;
  void *a = ps5tm_dynsym(RP_MODULE, "sceRemoteplayInitialize");
  void *b = ps5tm_dynsym(RP_MODULE, "sceRemoteplayGeneratePinCode");
  void *c = ps5tm_dynsym(RP_MODULE, "sceRemoteplayConfirmDeviceRegist");
  void *d = ps5tm_dynsym(RP_MODULE, "sceRemoteplayNotifyPinCodeError");
  g_rp_init = (int (*)(void *, size_t))a;
  g_rp_gen_pin = (int (*)(uint32_t *))b;
  g_rp_confirm = (int (*)(uint32_t *, uint32_t *))c;
  g_rp_notify_err = (int (*)(int))d;
  g_mod_state = (a && b && c) ? 2 : 3;
  if(g_mod_state == 3) PS5TM_WARN("remoteplay_module", "Remote Play: libSceRemoteplay stellt die Aufrufe nicht bereit (Firmware?).");
}

static int
cb_prepare(void *ctx, char *err, size_t ec, char *code, size_t cc) {
  (void)ctx;
  if(g_mod_state == 0) resolve_module();
  if(g_mod_state != 2) {
    snprintf(err, ec, "Diese Firmware stellt Remote Play für Programme nicht bereit.");
    snprintf(code, cc, "no_module");
    return -1;
  }
  ps5tm_sony_api_lock();
  int rc = g_rp_init(0, 0);
  ps5tm_sony_api_unlock();
  if(rc != 0 && rc != RP_ERR_ALREADY_INITIALIZED) {
    PS5TM_WARN("remoteplay_init", "Remote Play: sceRemoteplayInitialize meldet 0x%08X.", (unsigned)rc);
    snprintf(err, ec, "Remote Play ließ sich nicht starten.");
    snprintf(code, cc, "init_failed");
    return -1;
  }
  rp_user_t users[PS5TM_ACCOUNT_SLOTS];
  int per_user = ps5tm_rp_fw_has_per_user(ps5tm_platform_firmware());
  int n = collect_users(users, PS5TM_ACCOUNT_SLOTS, per_user);
  const char *via = NULL;
  if(pairing_slot(users, n, &via) < 0) {
    snprintf(err, ec, "Kein Benutzer mit aktiviertem Konto. Aktiviere zuerst das Offline-Konto (Seite Profil).");
    snprintf(code, cc, "no_account");
    return -1;
  }
  return 0;
}

static int
cb_gen_pin(void *ctx, uint32_t *pin) {
  (void)ctx;
  ps5tm_sony_api_lock();
  int rc = g_rp_gen_pin(pin);
  ps5tm_sony_api_unlock();
  if(rc != 0) PS5TM_WARN("remoteplay_pin", "Remote Play: sceRemoteplayGeneratePinCode meldet 0x%08X.", (unsigned)rc);
  return rc;
}

static int
cb_confirm(void *ctx, uint32_t *status, uint32_t *err) {
  (void)ctx;
  ps5tm_sony_api_lock();
  int rc = g_rp_confirm(status, err);
  ps5tm_sony_api_unlock();
  return rc;
}

static int
cb_invalidate(void *ctx) {
  (void)ctx;
  if(g_mod_state != 2 || !g_rp_notify_err) return -1;
  ps5tm_sony_api_lock();
  int rc = g_rp_notify_err(1);
  ps5tm_sony_api_unlock();
  return rc;
}

static int
cb_devices(void *ctx) {
  (void)ctx;
  return device_count();
}

static void
cb_notify(void *ctx, const char *msg, int level) {
  (void)ctx;
  ps5tm_notify_plain("%s", msg);
  /* the PIN stays out of the log: only the fact is written */
  const char *logged = strstr(msg, "Kopplungs-PIN") ? "Remote Play: Eine Kopplungs-PIN wurde erzeugt." : msg;
  if(level >= 1) PS5TM_WARN("remoteplay", "%s", logged);
  else PS5TM_INFO("remoteplay", "%s", logged);
}

static ps5tm_rpair_ops_t
pair_ops(void) {
  ps5tm_rpair_ops_t o;
  memset(&o, 0, sizeof(o));
  o.prepare = cb_prepare;
  o.gen_pin = cb_gen_pin;
  o.confirm = cb_confirm;
  o.invalidate = cb_invalidate;
  o.device_count = cb_devices;
  o.notify = cb_notify;
  return o;
}

enum { W_NONE = 0, W_REQUEST = 1, W_SETTLE = 2 };

static pthread_mutex_t g_wmu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_wcv_work = PTHREAD_COND_INITIALIZER;
static pthread_cond_t  g_wcv_done = PTHREAD_COND_INITIALIZER;
static int      g_wcmd = W_NONE;
static int      g_wstarted = 0;
static int      g_wbusy = 0;                          /* the worker is inside a command (or the module) */
static int      g_wresult = 0;
static uint64_t g_wseq = 0, g_wdone = 0;

static int64_t
now_ms(void) {
  return (int64_t)ps5tm_mono_ms();
}

static void *
worker_main(void *arg) {
  (void)arg;
  for(;;) {
    pthread_mutex_lock(&g_wmu);
    while(g_wcmd == W_NONE) {
      struct timespec ts;
      clock_gettime(CLOCK_REALTIME, &ts);
      ts.tv_sec += 2;
      if(pthread_cond_timedwait(&g_wcv_work, &g_wmu, &ts) == ETIMEDOUT) break;
    }
    int cmd = g_wcmd;
    uint64_t seq = g_wseq;
    g_wcmd = W_NONE;
    if(cmd != W_NONE) g_wbusy = 1;
    pthread_mutex_unlock(&g_wmu);

    ps5tm_rpair_ops_t ops = pair_ops();
    int result = 0;
    if(cmd == W_REQUEST) {
      result = ps5tm_rpair_request(&g_pair, &ops, now_ms());
    } else if(cmd == W_SETTLE) {
      ps5tm_rpair_settle(&g_pair, &ops);
    } else {
      /* the quiet tick: a live PIN is probed, a PIN that was given up is told to the console */
      ps5tm_rpair_view_t v;
      ps5tm_rpair_view(&g_pair, now_ms(), &v);
      if(v.state == PS5TM_RPAIR_WAITING) (void)ps5tm_rpair_poll(&g_pair, &ops, now_ms());
      else ps5tm_rpair_settle(&g_pair, &ops);
    }
    if(cmd != W_NONE) {
      pthread_mutex_lock(&g_wmu);
      g_wresult = result;
      g_wdone = seq;
      g_wbusy = 0;
      pthread_cond_broadcast(&g_wcv_done);
      pthread_mutex_unlock(&g_wmu);
    }
  }
  return NULL;
}

/* Hands a command to the worker. wait_ms 0: do not wait. 0 = done (result in *res), -EBUSY = the worker is inside the console and
   does not answer, -ETIMEDOUT = it did not finish in time (it carries on; the PIN, if there will be one, shows in the next state). */
static int
worker_call(int cmd, int wait_ms, int *res) {
  pthread_once(&g_pair_once, pair_init_once);
  pthread_mutex_lock(&g_wmu);
  if(!g_wstarted) {
    pthread_t th;
    pthread_attr_t at;
    pthread_attr_init(&at);
    pthread_attr_setstacksize(&at, 256 * 1024);
    pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
    int rc = pthread_create(&th, &at, worker_main, NULL);
    pthread_attr_destroy(&at);
    if(rc != 0) { pthread_mutex_unlock(&g_wmu); return -EAGAIN; }
    g_wstarted = 1;
  }
  if(g_wbusy || g_wcmd != W_NONE) { pthread_mutex_unlock(&g_wmu); return -EBUSY; }
  g_wcmd = cmd;
  uint64_t my = ++g_wseq;
  pthread_cond_signal(&g_wcv_work);
  if(wait_ms <= 0) { pthread_mutex_unlock(&g_wmu); return 0; }
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  ts.tv_sec += wait_ms / 1000;
  ts.tv_nsec += (long)(wait_ms % 1000) * 1000000L;
  if(ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
  while(g_wdone < my) {
    if(pthread_cond_timedwait(&g_wcv_done, &g_wmu, &ts) == ETIMEDOUT && g_wdone < my) {
      pthread_mutex_unlock(&g_wmu);
      return -ETIMEDOUT;
    }
  }
  if(res) *res = g_wresult;
  pthread_mutex_unlock(&g_wmu);
  return 0;
}

/* ------------------------------------------------------------------- state */

static void
add_pairing(cJSON *o) {
  pthread_once(&g_pair_once, pair_init_once);
  ps5tm_rpair_view_t v;
  ps5tm_rpair_view(&g_pair, now_ms(), &v);
  cJSON *p = cJSON_AddObjectToObject(o, "pairing");
  cJSON_AddStringToObject(p, "state", ps5tm_rpair_state_name(v.state));
  cJSON_AddStringToObject(p, "pin", v.pin);
  cJSON_AddNumberToObject(p, "seconds_left", v.seconds_left);
  cJSON_AddStringToObject(p, "code", v.code);
  cJSON_AddStringToObject(p, "err", v.err);
  pthread_mutex_lock(&g_wmu);
  int busy = g_wbusy;
  pthread_mutex_unlock(&g_wmu);
  cJSON_AddBoolToObject(p, "busy", busy);
}

static void
add_backup(cJSON *o) {
  char (*names)[80] = malloc((size_t)RP_NAMES_MAX * 80);
  int n = names ? list_backups(names, RP_NAMES_MAX) : 0;
  cJSON *b = cJSON_AddObjectToObject(o, "backup");
  cJSON_AddStringToObject(b, "dir", RP_BACKUP_DIR);
  cJSON_AddNumberToObject(b, "files", n);
  cJSON_AddStringToObject(b, "last", n > 0 ? names[0] : "");
  free(names);
}

static cJSON *
state_json(void) {
  cJSON *o = cJSON_CreateObject();
  if(!o) return NULL;
  uint32_t fw = ps5tm_platform_firmware();
  char fwtext[16];
  ps5tm_platform_firmware_text(fw, fwtext, sizeof(fwtext));
  int per_user = ps5tm_rp_fw_has_per_user(fw);
  cJSON_AddBoolToObject(o, "ok", 1);
  cJSON_AddBoolToObject(o, "available", sceRegMgrGetInt != NULL);
  cJSON_AddStringToObject(o, "firmware", fwtext);
  cJSON_AddBoolToObject(o, "per_user", per_user);

  int svc = 0;
  int svc_rc = reg_get_int(PS5TM_RP_KEY_SERVICE, &svc);
  cJSON *s = cJSON_AddObjectToObject(o, "service");
  if(svc_rc == 0) cJSON_AddBoolToObject(s, "enabled", svc != 0);
  else cJSON_AddNullToObject(s, "enabled");

  rp_user_t users[PS5TM_ACCOUNT_SLOTS];
  int n = collect_users(users, PS5TM_ACCOUNT_SLOTS, per_user);
  cJSON *ua = cJSON_AddArrayToObject(o, "users");
  for(int i = 0; i < n; i++) {
    cJSON *u = cJSON_CreateObject();
    cJSON_AddNumberToObject(u, "slot", users[i].slot);
    cJSON_AddStringToObject(u, "name", users[i].name);
    cJSON_AddBoolToObject(u, "activated", users[i].activated);
    char b64[16] = "";
    if(users[i].has_id) ps5tm_rp_base64(users[i].raw, 8, b64);
    cJSON_AddStringToObject(u, "account_id", b64);
    if(users[i].allowed >= 0) cJSON_AddBoolToObject(u, "allowed", users[i].allowed);
    else cJSON_AddNullToObject(u, "allowed");
    cJSON_AddItemToArray(ua, u);
  }
  const char *via = "none";
  int slot = pairing_slot(users, n, &via);
  cJSON *pu = cJSON_AddObjectToObject(o, "pairing_user");
  cJSON_AddNumberToObject(pu, "slot", slot);
  cJSON_AddStringToObject(pu, "via", via);

  cJSON *da = cJSON_AddArrayToObject(o, "devices");
  for(uint32_t d = 1; d <= PS5TM_RP_DEVICE_SLOTS; d++) {
    int uid = 0, client = 0;
    if(!device_used(d, &uid, &client)) continue;
    cJSON *e = cJSON_CreateObject();
    cJSON_AddNumberToObject(e, "slot", d);
    cJSON_AddNumberToObject(e, "client_type", client);
    const char *name = "";
    for(int i = 0; i < n; i++) if(users[i].uid == uid) name = users[i].name;
    cJSON_AddStringToObject(e, "user", name);
    cJSON_AddItemToArray(da, e);
  }

  add_pairing(o);
  const char *ms = g_mod_state == 2 ? "ready" : g_mod_state == 1 ? "loading" : g_mod_state == 3 ? "missing" : "idle";
  cJSON *m = cJSON_AddObjectToObject(o, "module");
  cJSON_AddStringToObject(m, "state", ms);
  add_backup(o);
  return o;
}

static void
send_obj(int fd, cJSON *o) {
  char *txt = o ? cJSON_PrintUnformatted(o) : NULL;
  if(o) cJSON_Delete(o);
  if(!txt) { ps5tm_http_send_error(fd, 500, "alloc_failed", "Kein Speicher."); return; }
  ps5tm_http_send_json(fd, 200, txt);
  free(txt);
}

static void
send_state(int fd) {
  send_obj(fd, state_json());
}

/* ------------------------------------------------------------------ writes */

/* Writes one int under the rule of this file: back it up, write it, read it back. Returns 0, else the HTTP status and the error. */
static int
write_int_checked(const char *what, uint32_t key, int value, const char **code, const char **msg) {
  pthread_mutex_lock(&g_write);
  int cur = 0;
  int readable = reg_get_int(key, &cur) == 0;
  if(readable && cur == value) { pthread_mutex_unlock(&g_write); return 0; }
  const char kinds[1] = {'i'};
  if(ps5tm_regbackup(what, &key, kinds, 1) != 0) {
    pthread_mutex_unlock(&g_write);
    *code = "rp_backup_failed";
    *msg = "Die Sicherung der Einstellungen ist nicht möglich, deshalb ändert die App nichts.";
    return 500;
  }
  int rc = reg_set_int(key, value);
  int back = -1;
  int rr = rc == 0 ? reg_get_int(key, &back) : -1;
  pthread_mutex_unlock(&g_write);
  if(rc != 0 || rr != 0 || back != value) {
    PS5TM_WARN("remoteplay_write", "Remote Play: %s: Schreiben 0x%08X, Zurücklesen %s.", what, (unsigned)rc, rr == 0 ? "anderer Wert" : "nicht möglich");
    *code = "rp_write_failed";
    *msg = "Die Konsole hat die Änderung nicht übernommen.";
    return 500;
  }
  return 0;
}

static const cJSON *
body_item(cJSON *root, const char *name) {
  return root ? cJSON_GetObjectItemCaseSensitive(root, name) : NULL;
}

static void
handle_service(int fd, const ps5tm_request_t *req) {
  cJSON *root = req->body ? cJSON_Parse(req->body) : NULL;
  const cJSON *en = body_item(root, "enabled");
  if(!cJSON_IsBool(en)) {
    cJSON_Delete(root);
    ps5tm_http_send_error(fd, 400, "rp_bad_request", "Ungültige Angabe: „enabled“ muss wahr oder falsch sein.");
    return;
  }
  int on = cJSON_IsTrue(en);
  cJSON_Delete(root);
  if(!sceRegMgrSetInt) {
    ps5tm_http_send_error(fd, 503, "rp_unavailable", "Die Registry der Konsole ist nicht erreichbar. Läuft der Jailbreak?");
    return;
  }
  const char *code = NULL, *msg = NULL;
  int st = write_int_checked("service", PS5TM_RP_KEY_SERVICE, on, &code, &msg);
  if(st != 0) { ps5tm_http_send_error(fd, st, code, msg); return; }
  PS5TM_INFO("remoteplay_service", "Remote Play wurde %s.", on ? "eingeschaltet" : "ausgeschaltet");
  send_state(fd);
}

static void
handle_user(int fd, const ps5tm_request_t *req) {
  cJSON *root = req->body ? cJSON_Parse(req->body) : NULL;
  const cJSON *js = body_item(root, "slot");
  const cJSON *en = body_item(root, "enabled");
  if(!cJSON_IsNumber(js) || !cJSON_IsBool(en)) {
    cJSON_Delete(root);
    ps5tm_http_send_error(fd, 400, "rp_bad_request", "Ungültige Angabe: „slot“ und „enabled“ werden gebraucht.");
    return;
  }
  int slot = (int)js->valuedouble;
  int on = cJSON_IsTrue(en);
  cJSON_Delete(root);
  uint32_t fw = ps5tm_platform_firmware();
  if(!ps5tm_rp_fw_has_per_user(fw)) {
    ps5tm_http_send_error(fd, 409, "rp_no_per_user", "Diese Firmware kennt die Erlaubnis je Benutzer nicht (erst ab 10.00).");
    return;
  }
  rp_user_t users[PS5TM_ACCOUNT_SLOTS];
  int n = collect_users(users, PS5TM_ACCOUNT_SLOTS, 0);
  int found = 0;
  for(int i = 0; i < n; i++) if(users[i].slot == slot) found = 1;
  if(!found) {
    ps5tm_http_send_error(fd, 404, "rp_no_user", "Auf diesem Platz gibt es keinen Benutzer.");
    return;
  }
  if(!sceRegMgrSetInt) {
    ps5tm_http_send_error(fd, 503, "rp_unavailable", "Die Registry der Konsole ist nicht erreichbar. Läuft der Jailbreak?");
    return;
  }
  char what[24];
  snprintf(what, sizeof(what), "user%d", slot);
  const char *code = NULL, *msg = NULL;
  int st = write_int_checked(what, ps5tm_rp_key_user_enable((uint32_t)slot), on, &code, &msg);
  if(st != 0) { ps5tm_http_send_error(fd, st, code, msg); return; }
  PS5TM_INFO("remoteplay_user", "Remote Play für Benutzer %d wurde %s.", slot, on ? "erlaubt" : "gesperrt");
  send_state(fd);
}

static void
handle_pin(int fd) {
  uint32_t fw = ps5tm_platform_firmware();
  int per_user = ps5tm_rp_fw_has_per_user(fw);
  rp_user_t users[PS5TM_ACCOUNT_SLOTS];
  int n = collect_users(users, PS5TM_ACCOUNT_SLOTS, per_user);
  const char *via = NULL;
  if(pairing_slot(users, n, &via) < 0) {
    ps5tm_http_send_error(fd, 409, "rp_no_account", "Kein Benutzer mit aktiviertem Konto. Aktiviere zuerst das Offline-Konto (Seite Profil).");
    return;
  }
  /* with the service off the console makes a PIN that cannot be used: switch it on first (backed up like every write) */
  int svc = 1;
  if(reg_get_int(PS5TM_RP_KEY_SERVICE, &svc) == 0 && svc == 0) {
    const char *code = NULL, *msg = NULL;
    int st = write_int_checked("service", PS5TM_RP_KEY_SERVICE, 1, &code, &msg);
    if(st != 0) { ps5tm_http_send_error(fd, st, code, msg); return; }
    PS5TM_INFO("remoteplay_service", "Remote Play wurde für die PIN eingeschaltet.");
  }
  int res = 0;
  int rc = worker_call(W_REQUEST, 25000, &res);
  if(rc == -EBUSY || rc == -ETIMEDOUT || rc == -EAGAIN) {
    ps5tm_http_send_error(fd, 503, "rp_busy", "Remote Play antwortet gerade nicht. Versuche es in einer Minute noch einmal.");
    return;
  }
  send_state(fd);
}

static void
handle_pin_cancel(int fd) {
  pthread_once(&g_pair_once, pair_init_once);
  if(ps5tm_rpair_cancel(&g_pair)) (void)worker_call(W_SETTLE, 0, NULL);
  send_state(fd);
}

void
ps5tm_remoteplay_api(int fd, const ps5tm_request_t *req) {
  const char *rest = req->path + strlen("/api/v1/remoteplay");
  int is_get = !strcmp(req->method, "GET");
  int is_post = !strcmp(req->method, "POST");

  if(!strcmp(rest, "")) {
    if(!is_get) { ps5tm_http_send_error(fd, 405, "method_not_allowed", "Nur GET erlaubt."); return; }
    send_state(fd);
    return;
  }
  if(!strcmp(rest, "/pin") && is_get) {
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "ok", 1);
    add_pairing(o);
    send_obj(fd, o);
    return;
  }
  if(!strcmp(rest, "/service") || !strcmp(rest, "/user") || !strcmp(rest, "/pin") || !strcmp(rest, "/pin/cancel")) {
    if(!is_post) { ps5tm_http_send_error(fd, 405, "method_not_allowed", "Nur POST erlaubt."); return; }
    if(!strcmp(rest, "/service")) handle_service(fd, req);
    else if(!strcmp(rest, "/user")) handle_user(fd, req);
    else if(!strcmp(rest, "/pin")) handle_pin(fd);
    else handle_pin_cancel(fd);
    return;
  }
  ps5tm_http_send_error(fd, 404, "not_found", "Unbekannter Pfad.");
}
