/* The game plugins of a game (13.10.2026): drakmor's replacement modules in the game's fakelib, kept up to date.
 *
 *   libScePlayGo.sprx                                  pgo_stub  every PlayGo chunk reported installed (embedded: 0.6)
 *   libSceAppContent.sprx, libSceGameUpdate.sprx,
 *   libSceNpEntitlementAccess.sprx                     dlc_emu   the DLC listed in the game's dlc_emu.ini (embedded: 0.3 "log")
 *
 * dlc_emu 0.3 "nolog" crashes the game at every start: its IPC out-descriptor is 16 bytes, the system writes 24, and in that
 * build the 8 bytes past it are a saved register of the caller (measured on hardware by the author, fix 48efe06 of 29.09.2026,
 * in the source only, no release yet). The "log" build of the same source keeps a dead local there and runs. So the app carries
 * the "log" build (it writes /app0/dlc_emu.log), and a "nolog" 0.3 in a game counts as faulty and is replaced (15.10.2026).
 *
 * What is done, per file of the game's fakelib:
 *   current   the build of the app (or its log variant): nothing, only a missing execute bit is set
 *   older     an older release of the project (known by its SHA-256), or one known to be faulty: replaced, the old copy stays
 *             as <name>.old
 *   unknown   another build: left alone and reported
 *   missing   only the DLC modules, and only when the game has a dlc_emu.ini (without that file they would hide the real DLC)
 * The PlayGo stub is never added to a game that has none: it is for games that treat PlayGo content as missing, and a game
 * that does not needs it no more than a game with a real PlayGo file. The person adds it in the row "PlayGo im Spiel".
 *
 * The idea, and the table of the releases that drakmor published (SHA-256 values), follow the plugin module of exFAT2AMPR
 * by V-10 (GPL-3.0-or-later with additional terms, https://github.com/exFAT2AMPR/exfat2ampr, "Made by V-10"); the code is new. */
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "ps5tm.h"
#include "sha256.h"
#include "third_party/cJSON.h"

typedef struct { const char *version, *variant, *sha256; int faulty; } release_t;

/* every build drakmor published; the first entry's version is the current one. faulty: a build of the current version that must
   not stay in a game (see the top of the file) */
static const release_t PLAYGO[] = {
  { "0.6", "nolog", "f5fdb0ed6cc7413e6dcac79a62e0efd8cfbae04cca0bf932e7a741aed850d213", 0 },
  { "0.6", "log",   "7595924889d67c33149dc152903d2f2492abbc3e846196d536c4f16a7b5560f6", 0 },
  { "0.5", "nolog", "645caa464a7d28fab7019ba547b204ecc007f87ccec072d330eb2b72a3e9ddac", 0 },
  { "0.5", "log",   "305e6e117ca3a16cb6062007e3ddee7eb184ca19c3e353d5b12a6161bc92a3bd", 0 },
  { "0.4", "nolog", "a0f20f40107713523ba1a3a0281fb7c4e238b2b5d3fda9354119352d9393d2f0", 0 },
  { "0.4", "log",   "39c508a84208a81042dfe637619ece44a6b1d7f737076523aa8096e5eba3457f", 0 },
};
static const release_t APPCONTENT[] = {
  { "0.3", "nolog", "53e485e22970c693ab70c05bd78b8f627442ed41ca9a49692b38615046494a6b", 1 },
  { "0.3", "log",   "6ca6d0e10399422eeb6a4c1085d0ef6c166b8e0dacc1a458c17aac5408140c86", 0 },
  { "0.2", "nolog", "79f25d1ef1206e368e43f7e3316018ce14a114f8088dcb563c471293f88a2fe2", 0 },
  { "0.2", "log",   "064ee21a3c0110f7a5ab7f682ee3fc70efb831f2cccf041263e19ea9aca50c18", 0 },
  { "0.1", "nolog", "80af9b73508d2cdf318ca18d4d869ef276ad1fb96c550db0c58bcd68eac76cc3", 0 },
  { "0.1", "log",   "db7ce6d3c5766f1c8d7d405b1c5cd51a8a654ee26d079f0701bad25fab4a8513", 0 },
};
static const release_t GAMEUPDATE[] = {
  { "0.3", "nolog", "1e31ddbe4809b7c5ec117e9b7e1227befe757c31d1f5f73e6c7d5a3d2d238973", 1 },
  { "0.3", "log",   "6a3e65faf26356393ff4d7e68b95df46c18521fe8d92c3b7920c8509227fdf15", 0 },
  { "0.2", "nolog", "2d66f588a19c461af572bc7b3138b4100120091fec42c4ce11370080b51dc127", 0 },
  { "0.2", "log",   "5f38340ffc0748a1277c14db59b213ef035ec5ed33c64be9a3e7542d815bf104", 0 },
};
static const release_t ENTITLEMENT[] = {
  { "0.3", "nolog", "3f1826f20e174ff0f7aaa8d7f3f7e386dfcca1136c9e4c0e72a49e041c0b1488", 1 },
  { "0.3", "log",   "51871d03aef46163cf5e4165f8641284c509cf57d9baeeba222b168f83806eb3", 0 },
  { "0.2", "nolog", "5333b83a263fe5e8e2ae5ee53d43f0b11c5c519151e1aa367c22621e6020a0bd", 0 },
  { "0.2", "log",   "1fc44ef546aa621fef73a13eb1b2c74ac60db0d9f0cf2e680e2b733cb062d707", 0 },
  { "0.1", "nolog", "9315332b065a7039708f09bc489602d5ddedc0802690ec6192bbb3ba916d4b98", 0 },
  { "0.1", "log",   "98f8e33dd8d5276756a4c3f8b6f674eefec131cc5c2949b65acf9edad215ea74", 0 },
};

typedef struct {
  const char             *name;          /* the file in fakelib */
  const char             *project;       /* what the page calls it */
  const release_t        *known;
  int                     nknown;
  const ps5tm_emu_blob_t *blobs;         /* the table of the built-in files */
  const unsigned         *nblobs;
  const char             *blob;          /* the built-in file that goes into a game */
  const char             *blob_log;      /* the built-in "log" build for a game that had one; NULL: always blob */
} plugin_t;

enum { P_PLAYGO, P_APPCONTENT, P_GAMEUPDATE, P_ENTITLEMENT, P_N };
static const plugin_t PLUGINS[P_N] = {
  { "libScePlayGo.sprx", "pgo_stub", PLAYGO, 6, ps5tm_playgo_blobs, &ps5tm_playgo_blob_count,
    "libScePlayGo.sprx-0.6-nolog", "libScePlayGo.sprx-0.6-log" },
  { "libSceAppContent.sprx", "dlc_emu", APPCONTENT, 6, ps5tm_dlc_blobs, &ps5tm_dlc_blob_count, "libSceAppContent.sprx-0.3-log", NULL },
  { "libSceGameUpdate.sprx", "dlc_emu", GAMEUPDATE, 4, ps5tm_dlc_blobs, &ps5tm_dlc_blob_count, "libSceGameUpdate.sprx-0.3-log", NULL },
  { "libSceNpEntitlementAccess.sprx", "dlc_emu", ENTITLEMENT, 6, ps5tm_dlc_blobs, &ps5tm_dlc_blob_count,
    "libSceNpEntitlementAccess.sprx-0.3-log", NULL },
};

typedef enum { S_ABSENT, S_CURRENT, S_OLDER, S_UNKNOWN, S_UNREADABLE } fstate_t;

#define PL_MAX_BYTES (4u * 1024 * 1024)

static int
read_file(const char *path, uint8_t **d, size_t *n) {
  int fd = open(path, O_RDONLY);
  if(fd < 0) return errno ? errno : EIO;
  struct stat st;
  if(fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) { int e = errno ? errno : EINVAL; close(fd); return e; }
  if((uint64_t)st.st_size > PL_MAX_BYTES) { close(fd); return EFBIG; }
  uint8_t *b = malloc((size_t)st.st_size + 1);
  if(!b) { close(fd); return ENOMEM; }
  size_t have = 0;
  while(have < (size_t)st.st_size) {
    ssize_t r = read(fd, b + have, (size_t)st.st_size - have);
    if(r < 0 && errno == EINTR) continue;
    if(r <= 0) { free(b); close(fd); return r < 0 ? errno : EIO; }
    have += (size_t)r;
  }
  close(fd);
  *d = b;
  *n = have;
  return 0;
}

static void
sha_hex(const uint8_t *d, size_t n, char out[65]) {
  ps5tm_sha256_t s;
  uint8_t dg[32];
  ps5tm_sha256_init(&s);
  ps5tm_sha256_update(&s, d, n);
  ps5tm_sha256_final(&s, dg);
  ps5tm_sha256_hex(dg, out);
}

typedef struct {
  fstate_t st;
  char     version[8];
  int      is_log;
  int      faulty;       /* a build known to crash (dlc_emu 0.3 "nolog"): replaced like an older one */
  int      noexec;       /* the file has no execute bit: the loader refuses it */
} pfile_t;

/* one file of a game's fakelib against the known releases */
static void
classify(const plugin_t *p, const char *path, pfile_t *out) {
  memset(out, 0, sizeof(*out));
  struct stat st;
  if(stat(path, &st) != 0) { out->st = S_ABSENT; return; }
  out->noexec = !(st.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH));
  uint8_t *d = NULL;
  size_t n = 0;
  if(read_file(path, &d, &n) != 0) { out->st = S_UNREADABLE; return; }
  char hex[65];
  sha_hex(d, n, hex);
  free(d);
  out->st = S_UNKNOWN;
  for(int i = 0; i < p->nknown; i++) {
    if(strcmp(hex, p->known[i].sha256)) continue;
    snprintf(out->version, sizeof(out->version), "%s", p->known[i].version);
    out->is_log = !strcmp(p->known[i].variant, "log");
    out->faulty = p->known[i].faulty;
    out->st = !out->faulty && !strcmp(p->known[i].version, p->known[0].version) ? S_CURRENT : S_OLDER;
    return;
  }
}

static int
is_dir(const char *p) {
  struct stat st;
  return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

/* the entries of dlc_emu.ini: its sections ([PSAC] ...); -1 when there is no such file */
static int
dlc_entries(const char *game) {
  char p[PATH_MAX];
  snprintf(p, sizeof(p), "%s/dlc_emu.ini", game);
  uint8_t *d = NULL;
  size_t n = 0;
  if(read_file(p, &d, &n) != 0) return -1;
  int c = 0;
  size_t i = 0;
  while(i < n) {
    while(i < n && (d[i] == ' ' || d[i] == '\t')) i++;           /* a line may start with blanks */
    if(i < n && d[i] == '[') c++;
    while(i < n && d[i] != '\n') i++;
    i++;
  }
  free(d);
  return c;
}

typedef struct {
  pfile_t f[P_N];
  pfile_t amp;                  /* libSceAmpr.sprx: only its execute bit is looked at */
  int     dlc;                  /* entries of dlc_emu.ini, -1 none */
  int     need[P_N];
} eval_t;

static void
eval_game(const char *game, eval_t *ev) {
  char p[PATH_MAX];
  memset(ev, 0, sizeof(*ev));
  for(int i = 0; i < P_N; i++) {
    snprintf(p, sizeof(p), "%s/fakelib/%s", game, PLUGINS[i].name);
    classify(&PLUGINS[i], p, &ev->f[i]);
  }
  snprintf(p, sizeof(p), "%s/fakelib/libSceAmpr.sprx", game);
  struct stat st;
  if(stat(p, &st) == 0 && S_ISREG(st.st_mode)) { ev->amp.st = S_CURRENT; ev->amp.noexec = !(st.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH)); }
  ev->dlc = dlc_entries(game);
  /* a file that is there is kept up to date; the DLC modules are wanted when the game has the ini that lists the DLC */
  for(int i = 0; i < P_N; i++) ev->need[i] = ev->f[i].st != S_ABSENT;
  if(ev->dlc >= 0) ev->need[P_APPCONTENT] = ev->need[P_GAMEUPDATE] = ev->need[P_ENTITLEMENT] = 1;
}

static const char *
state_name(const eval_t *ev, int i) {
  const pfile_t *f = &ev->f[i];
  if(f->st == S_ABSENT) return ev->need[i] ? "missing" : "not_needed";
  if(f->st == S_UNREADABLE) return "unknown";
  if(f->st == S_OLDER) return f->faulty ? "faulty" : "older";
  if(f->st == S_UNKNOWN) return "unknown";
  return f->noexec ? "noexec" : "current";
}

/* the game's folder, when plugins can be looked at: a folder game on the list */
static int
game_dir(const char *id, ps5tm_libdel_t *li, char *why, size_t wn) {
  if(!id || !id[0] || ps5tm_library_delete_info(id, li) != 0) { snprintf(why, wn, "Das Spiel ist nicht in der Liste."); return 404; }
  if(strcmp(li->format, "folder") || li->path[0] != '/') {
    snprintf(why, wn, "Nur für Spiele, die als Ordner vorliegen. Ein Abbild lässt sich vorher mit „In einen Ordner entpacken“ in einen Ordner bringen.");
    return 409;
  }
  return 200;
}

/* where the files would go: the game's own fakelib, when nothing hides it (fakelib2, a backport folder) */
static int
place_ok(const char *id, const char *game, char *why, size_t wn) {
  char p[PATH_MAX];
  snprintf(p, sizeof(p), "%s/fakelib2", game);
  if(is_dir(p)) {
    snprintf(why, wn, "Im Spielordner liegt ein fakelib2: ShadowMountPlus liest dann nur dieses. Zuerst den Mitschnitt beenden oder fakelib2 entfernen.");
    return 0;
  }
  int http = 200;
  cJSON *st = ps5tm_amprlib_state("ampr", id, &http);
  int ok = 1;
  if(st) {
    const cJSON *pl = cJSON_GetObjectItem(st, "place");
    if(cJSON_IsString(pl) && strcmp(pl->valuestring, "game")) {
      snprintf(why, wn, "Für dieses Spiel liest ShadowMountPlus einen Backport-Ordner. Die Plugins gehören dorthin, nicht in das Spiel.");
      ok = 0;
    }
    cJSON_Delete(st);
  }
  return ok;
}

cJSON *
ps5tm_plugins_info(const char *id, int *http) {
  *http = 200;
  ps5tm_libdel_t li;
  char why[300] = "";
  int rc = game_dir(id, &li, why, sizeof(why));
  if(rc == 404) { *http = 404; return NULL; }
  cJSON *o = cJSON_CreateObject();
  cJSON_AddBoolToObject(o, "ok", 1);
  cJSON_AddStringToObject(o, "id", id);
  cJSON_AddStringToObject(o, "name", li.name);
  if(rc != 200) { cJSON_AddBoolToObject(o, "can", 0); cJSON_AddStringToObject(o, "why", why); return o; }
  eval_t ev;
  eval_game(li.path, &ev);
  char pwhy[300] = "";
  int placed = place_ok(id, li.path, pwhy, sizeof(pwhy));
  cJSON_AddBoolToObject(o, "can", placed);
  if(!placed) cJSON_AddStringToObject(o, "why", pwhy);
  cJSON *files = cJSON_AddArrayToObject(o, "files");
  int update = 0, unknown = 0, any = 0;
  for(int i = 0; i < P_N; i++) {
    const char *s = state_name(&ev, i);
    cJSON *e = cJSON_CreateObject();
    cJSON_AddStringToObject(e, "name", PLUGINS[i].name);
    cJSON_AddStringToObject(e, "project", PLUGINS[i].project);
    cJSON_AddStringToObject(e, "state", s);
    if(ev.f[i].version[0]) cJSON_AddStringToObject(e, "version", ev.f[i].version);
    if(ev.f[i].is_log) cJSON_AddBoolToObject(e, "log", 1);
    cJSON_AddBoolToObject(e, "needed", ev.need[i]);
    cJSON_AddItemToArray(files, e);
    if(strcmp(s, "not_needed")) any = 1;
    if(!strcmp(s, "older") || !strcmp(s, "faulty") || !strcmp(s, "missing") || !strcmp(s, "noexec")) update = 1;
    if(!strcmp(s, "unknown")) unknown = 1;
  }
  if(ev.amp.st == S_CURRENT && ev.amp.noexec) {
    cJSON *e = cJSON_CreateObject();
    cJSON_AddStringToObject(e, "name", "libSceAmpr.sprx");
    cJSON_AddStringToObject(e, "project", "APR-EMU");
    cJSON_AddStringToObject(e, "state", "noexec");
    cJSON_AddBoolToObject(e, "needed", 1);
    cJSON_AddItemToArray(files, e);
    update = any = 1;
  }
  cJSON_AddStringToObject(o, "state", update ? "update" : unknown ? "unknown" : any ? "ok" : "none");
  cJSON *dlc = cJSON_AddObjectToObject(o, "dlc");
  cJSON_AddBoolToObject(dlc, "present", ev.dlc >= 0);
  cJSON_AddNumberToObject(dlc, "entries", ev.dlc >= 0 ? ev.dlc : 0);
  return o;
}

/* a built-in file of a plugin by its name */
static const ps5tm_emu_blob_t *
find_blob(const plugin_t *p, const char *name) {
  for(unsigned i = 0; i < *p->nblobs; i++)
    if(!strcmp(p->blobs[i].name, name)) return &p->blobs[i];
  return NULL;
}

static void
note(char *msg, size_t n, const char *fmt, const char *a) {
  size_t l = strlen(msg);
  if(l + 2 >= n) return;
  snprintf(msg + l, n - l, "%s", l ? " " : "");
  l = strlen(msg);
  snprintf(msg + l, n - l, fmt, a);
}

/* The plugins of a game brought up to date. The HTTP status; msg tells what was done (or why not). */
int
ps5tm_plugins_apply(const char *id, char *msg, size_t n, cJSON **detail) {
  if(detail) *detail = NULL;
  msg[0] = 0;
  ps5tm_libdel_t li;
  int rc = game_dir(id, &li, msg, n);
  if(rc != 200) return rc;
  if(ps5tm_procmgr_title_running(id) == 1) { snprintf(msg, n, "Das Spiel läuft gerade. Bitte zuerst beenden."); return 409; }
  if(!place_ok(id, li.path, msg, n)) return 409;
  eval_t ev;
  eval_game(li.path, &ev);
  char dir[PATH_MAX], path[PATH_MAX], old[PATH_MAX];
  snprintf(dir, sizeof(dir), "%s/fakelib", li.path);
  int written = 0, fixed = 0, unknown = 0;
  char done[400] = "";
  for(int i = 0; i < P_N; i++) {
    const plugin_t *p = &PLUGINS[i];
    const pfile_t *f = &ev.f[i];
    snprintf(path, sizeof(path), "%s/%s", dir, p->name);
    if(f->st == S_UNKNOWN || f->st == S_UNREADABLE) { unknown++; continue; }
    if(f->st == S_CURRENT) {
      if(f->noexec && chmod(path, 0755) == 0) { fixed++; }
      continue;
    }
    if(f->st == S_ABSENT && !ev.need[i]) continue;
    const char *want = (f->is_log && p->blob_log) ? p->blob_log : p->blob;
    const ps5tm_emu_blob_t *b = find_blob(p, want);
    if(!b) continue;
    uint8_t *data = NULL;
    size_t len = 0;
    int e = ps5tm_blob_unpack(b, &data, &len);
    if(e) { snprintf(msg, n, "Die mitgelieferte Datei %s ließ sich nicht entpacken.", want); return 500; }
    if(f->st == S_ABSENT) mkdir(dir, 0777);
    if(f->st == S_OLDER) {
      snprintf(old, sizeof(old), "%s.old", path);
      if(rename(path, old) != 0) { free(data); snprintf(msg, n, "Die alte %s ließ sich nicht beiseite legen: %s", p->name, strerror(errno)); return 500; }
    }
    e = ps5tm_put_lib_file(path, data, len);
    free(data);
    if(e) {
      if(f->st == S_OLDER) rename(old, path);                       /* put the old one back */
      snprintf(msg, n, "%s ließ sich nicht schreiben: %s", p->name, strerror(e));
      return 500;
    }
    written++;
    size_t l = strlen(done);
    snprintf(done + l, sizeof(done) - l, "%s%s", l ? ", " : "", p->name);
  }
  if(ev.amp.st == S_CURRENT && ev.amp.noexec) {
    snprintf(path, sizeof(path), "%s/libSceAmpr.sprx", dir);
    if(chmod(path, 0755) == 0) fixed++;
  }
  char tmp[64];
  if(written) {
    snprintf(tmp, sizeof(tmp), "%d", written);
    note(msg, n, "%s Datei(en) aktualisiert:", tmp);
    note(msg, n, "%s.", done);
  }
  if(fixed) { snprintf(tmp, sizeof(tmp), "%d", fixed); note(msg, n, "Ausführungsrecht bei %s Datei(en) gesetzt.", tmp); }
  if(unknown) { snprintf(tmp, sizeof(tmp), "%d", unknown); note(msg, n, "%s Datei(en) in unbekannter Fassung blieben unverändert.", tmp); }
  if(!written && !fixed && !unknown) snprintf(msg, n, "Alles schon aktuell.");
  if(detail) {                          /* the page builds its sentences from these numbers, each one translated */
    cJSON *d = cJSON_CreateObject();
    cJSON_AddNumberToObject(d, "written", written);
    cJSON_AddNumberToObject(d, "fixed", fixed);
    cJSON_AddNumberToObject(d, "unknown", unknown);
    cJSON_AddStringToObject(d, "names", done);
    *detail = d;
  }
  PS5TM_INFO("plugins_apply", "Spiel-Plugins von %s: %d geschrieben, %d Rechte gesetzt, %d unbekannt.", id, written, fixed, unknown);
  return 200;
}
