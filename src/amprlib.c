/* The APR-EMU file of a game (11.10.2026): which libSceAmpr.sprx a game loads, choosing one of the versions the person
 * keeps, putting it in, taking it out, putting the game's own file back. The same for the PlayGo stub (12.10.2026,
 * libScePlayGo.sprx of drakmor's pgo_stub, store folder PlayGo): everything below holds for both, K_AMPR and K_PLAYGO say
 * where they differ.
 *
 * How ShadowMountPlus finds the file (checked at its source by the converter this follows, "PS5 Dump & Image Converter",
 * shadowmount_generation.py, and by ShadowMountPlus 1.7beta1 to 1.7beta6-fix2): first in
 * <scanpath>/backports/<TITLE_ID>/ (fakelib2, then fakelib; since beta6 also a folder that only begins with the title id,
 * "PPSA12345 Game Name": the exact name wins, else the first in byte order), then in the game (fakelib2 since 1.7beta1, then fakelib);
 * the first folder that exists wins, even an empty one, and only that one is used. Folders are named exactly fakelib
 * or fakelib2. Only the name fakelib is read by every version, so that is the one this app writes; a fakelib2 beside
 * it would win from 1.7beta1 on and leave the file unused, which is why the page warns about it.
 *
 * Where it is put:
 *   game      a dump folder, an .exfat or an .ffpkg: <game>/fakelib/libSceAmpr.sprx. An image is mounted read-write
 *             through ShadowMountPlus for the job and released again (amprindex.c, the use the person allowed on 11.10.2026).
 *   backport  a compressed .ffpfsc cannot be written, so the file goes to <scanpath>/backports/<TITLE_ID>/fakelib, the
 *             game stays untouched. Because that folder hides the game's own fakelib, a new one is first given a copy of
 *             what the game's fakelib holds (read from the image, nothing mounted). A backport folder that is already
 *             there is used for every format, since it is what ShadowMountPlus reads first.
 *
 * The file the game brought itself is kept once as libSceAmpr.sprx.orig and never overwritten by a later change. A file
 * that is itself an APR-EMU (it carries "[AMPR_EMU]", true for every build of drakmor's ampr_emu) or a PlayGo stub is no
 * original and is not kept. Changing, restoring and removing never touch another file of the game.
 *
 * The versions are files in /data/PS5-Cooling-Center/AMPR_EMU, named like drakmor's release files:
 * libSceAmpr.sprx-<version>-<variant> (0.4.2.1-test-pack, 0.3.6.6-test-debug ...). They are put there by the person (the
 * file manager of this app does it); each is checked for the SELF header and the APR-EMU mark before it is offered.
 * The variants with "pack" in their name (not "nopack") can read LZ4 asset packs. ShadowMountPlus can also replace the
 * file at the start of a game from /data/shadowmount/emus (update_emulators, on by default): the page says so. */
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "imgread.h"
#include "ps5tm.h"
#include "sha256.h"
#include "third_party/cJSON.h"
#include "third_party/libdeflate/libdeflate.h"

#define AL_ORIG       ".orig"
#define AL_MAX_BYTES  (4u * 1024 * 1024)
#define AL_STACK      (768 * 1024)
#ifndef AL_SM_DIR                                 /* the host test points these at folders of its own */
#define AL_SM_DIR     "/data/shadowmount"
#endif
#ifndef AL_BP_FALLBACK
#define AL_BP_FALLBACK "/data/homebrew"
#endif
#define AL_SM_CONFIG  AL_SM_DIR "/config.ini"

/* The two libraries a game can bring and this page replaces: the APR-EMU (libSceAmpr.sprx) and the PlayGo stub
   (libScePlayGo.sprx, drakmor's pgo_stub). Same folders, same rules; they differ in name, in the store folder, in the
   mark that says "this is a build of that project" and in the built-in versions. Only the APR-EMU can read LZ4 packs. */
typedef struct {
  const char             *id;           /* "ampr" or "playgo" (the page's lib=) */
  const char             *lib;          /* the file name in fakelib */
  const char             *prefix;       /* the names of the versions: <prefix><version>-<variant> */
  const char             *dir;          /* the store folder */
  const char             *old_dir;      /* the name of the first version of that folder, renamed when found, or NULL */
  const char             *mark;         /* in every build of the project */
  const ps5tm_emu_blob_t *blobs;
  const unsigned         *nblobs;
} al_kind_t;

static const al_kind_t K_AMPR = { "ampr", "libSceAmpr.sprx", "libSceAmpr.sprx-", PS5TM_DATA_DIR "/AMPR_EMU", PS5TM_DATA_DIR "/ampr-emu",
                                  "[AMPR_EMU]", ps5tm_emu_blobs, &ps5tm_emu_blob_count };
static const al_kind_t K_PLAYGO = { "playgo", "libScePlayGo.sprx", "libScePlayGo.sprx-", PS5TM_DATA_DIR "/PlayGo", NULL,
                                    "playgo_stub.dat", ps5tm_playgo_blobs, &ps5tm_playgo_blob_count };

static const al_kind_t *
kind_of(const char *lib) {
  if(!lib || !lib[0] || !strcmp(lib, "ampr")) return &K_AMPR;
  if(!strcmp(lib, "playgo")) return &K_PLAYGO;
  return NULL;
}

/* ---------------------------------------------------------------- small helpers */

typedef struct {
  char     name[96];
  char     version[24];
  char     variant[40];
  int      pack, debug, valid;
  int      stored;                 /* the file is in the folder AMPR_EMU */
  int      bundled;                /* the app carries this very file (same name and hash) */
  uint64_t size;
  char     sha[65];
} al_ver_t;

static int
has_mark(const uint8_t *d, size_t n, const char *mark) {
  size_t m = strlen(mark);
  if(n < m) return 0;
  for(size_t i = 0; i + m <= n; i++)
    if(d[i] == (uint8_t)mark[0] && !memcmp(d + i, mark, m)) return 1;
  return 0;
}

/* A build of the APR-EMU or a PlayGo stub: not what a game brings itself. */
static int
is_replacement(const uint8_t *d, size_t n) {
  return has_mark(d, n, "[AMPR_EMU]") || has_mark(d, n, "playgo_stub.dat");
}

/* An APR-EMU with the reader of LZ4 packs: the PackedStdio builds ("pack", "debug-pack") carry the names of the two formats
   (AMPRPAK4 manifest, AMPRDAT3 volumes), the LooseStdio builds ("nopack", the 0.3 ones) do not (checked on the five built-in
   files, 15.10.2026). By content, so that a pack build of another name or version counts too. */
static int
reads_packs(const uint8_t *d, size_t n) {
  return has_mark(d, n, "[AMPR_EMU]") && has_mark(d, n, "AMPRPAK4");
}

static void
sha_of(const uint8_t *d, size_t n, char out[65]) {
  ps5tm_sha256_t s;
  uint8_t dg[32];
  ps5tm_sha256_init(&s);
  ps5tm_sha256_update(&s, d, n);
  ps5tm_sha256_final(&s, dg);
  ps5tm_sha256_hex(dg, out);
}

/* Reads a whole file of at most max bytes into memory. 0 and *data (malloc) / *len, or an errno. */
static int
slurp(const char *path, uint8_t **data, size_t *len, size_t max) {
  int fd = open(path, O_RDONLY);
  if(fd < 0) return errno ? errno : EIO;
  struct stat st;
  if(fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) { int e = errno ? errno : EINVAL; close(fd); return e; }
  if((uint64_t)st.st_size > max) { close(fd); return EFBIG; }
  uint8_t *buf = malloc((size_t)st.st_size + 1);
  if(!buf) { close(fd); return ENOMEM; }
  size_t have = 0;
  while(have < (size_t)st.st_size) {
    ssize_t r = read(fd, buf + have, (size_t)st.st_size - have);
    if(r < 0 && errno == EINTR) continue;
    if(r <= 0) { int e = r < 0 ? errno : EIO; free(buf); close(fd); return e; }
    have += (size_t)r;
  }
  close(fd);
  *data = buf;
  *len = have;
  return 0;
}

/* tmp file, fsync, rename: a half-written file is never seen under the real name.
 * The file is a library the game loads (12.10.2026, Arkanoid): on the internal drive a file without an execute bit
 * (0644) is refused by the loader ("mount flag / attribute error", the game ends at once), while eboot.bin and the
 * files of PC tools are 0777 / 0755. So it is made 0755 whatever the umask says. */
static int
put_file(const char *path, const uint8_t *d, size_t n) {
  char tmp[PATH_MAX];
  if(snprintf(tmp, sizeof(tmp), "%s.ps5cc-teil", path) >= (int)sizeof(tmp)) return ENAMETOOLONG;
  int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0755);
  if(fd < 0) return errno ? errno : EIO;
  (void)fchmod(fd, 0755);
  size_t off = 0;
  int rc = 0;
  while(off < n) {
    ssize_t w = write(fd, d + off, n - off);
    if(w < 0 && errno == EINTR) continue;
    if(w <= 0) { rc = w < 0 && errno ? errno : EIO; break; }
    off += (size_t)w;
  }
  if(!rc && fsync(fd) != 0) rc = errno ? errno : EIO;
  if(close(fd) != 0 && !rc) rc = errno ? errno : EIO;
  if(!rc && rename(tmp, path) != 0) rc = errno ? errno : EIO;
  if(rc) unlink(tmp);
  return rc;
}

static void
make_writable(const char *path) {
  struct stat st;
  if(stat(path, &st) == 0 && !(st.st_mode & S_IWUSR)) chmod(path, st.st_mode | S_IWUSR);
}

static int
mkdir_p(const char *path) {
  char tmp[PATH_MAX];
  snprintf(tmp, sizeof(tmp), "%s", path);
  for(char *p = tmp + 1; *p; p++) {
    if(*p != '/') continue;
    *p = 0;
    if(mkdir(tmp, 0777) != 0 && errno != EEXIST) return errno ? errno : EIO;
    *p = '/';
  }
  if(mkdir(tmp, 0777) != 0 && errno != EEXIST) return errno ? errno : EIO;
  return 0;
}

static int
is_dir(const char *p) {
  struct stat st;
  return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

static int
is_file(const char *p) {
  struct stat st;
  return stat(p, &st) == 0 && S_ISREG(st.st_mode);
}

/* "libSceAmpr.sprx-0.4.2.1-test-pack": version and variant. 0 when the name is not of that shape. */
static int
parse_name(const al_kind_t *k, const char *name, al_ver_t *v) {
  memset(v, 0, sizeof(*v));
  size_t pl = strlen(k->prefix);
  if(strncmp(name, k->prefix, pl) || strlen(name) >= sizeof(v->name)) return 0;
  const char *p = name + pl;
  size_t vl = 0;
  while(p[vl] && (isdigit((unsigned char)p[vl]) || p[vl] == '.')) vl++;
  if(vl == 0 || vl >= sizeof(v->version) || !isdigit((unsigned char)p[0]) || p[vl - 1] == '.') return 0;
  memcpy(v->version, p, vl);
  const char *var = p + vl;
  if(*var == '-') var++;
  if(strlen(var) >= sizeof(v->variant)) return 0;
  for(const char *c = var; *c; c++)
    if(!isalnum((unsigned char)*c) && *c != '-' && *c != '_' && *c != '.') return 0;
  snprintf(v->variant, sizeof(v->variant), "%s", var);
  snprintf(v->name, sizeof(v->name), "%s", name);
  v->debug = strstr(var, "debug") != NULL || (k == &K_PLAYGO && !strcmp(var, "log"));          /* writes a log */
  v->pack = k == &K_AMPR && strstr(var, "pack") != NULL && strstr(var, "nopack") == NULL;
  return 1;
}

/* ---------------------------------------------------------------- the versions built into the app */

static const ps5tm_emu_blob_t *
find_blob(const al_kind_t *k, const char *name) {
  for(unsigned i = 0; i < *k->nblobs; i++)
    if(!strcmp(k->blobs[i].name, name)) return &k->blobs[i];
  return NULL;
}

/* The store folder (AMPR_EMU, PlayGo): made if it is not there; the folder of the first version of this feature is renamed. */
static void
store_ready(const al_kind_t *k) {
  if(k->old_dir && !is_dir(k->dir) && is_dir(k->old_dir)) rename(k->old_dir, k->dir);
  mkdir_p(k->dir);
}

/* Puts a built-in version into the folder AMPR_EMU (unpacked, hash checked). A file of that name that is already there stays,
   whatever it holds: it is the person's. 0 or an errno with the reason in why. */
static int
materialize(const al_kind_t *k, const ps5tm_emu_blob_t *b, char *why, size_t why_len) {
  store_ready(k);
  char path[PATH_MAX];
  if(snprintf(path, sizeof(path), "%s/%s", k->dir, b->name) >= (int)sizeof(path)) return ENAMETOOLONG;
  if(is_file(path)) return 0;
  uint8_t *buf = malloc(b->size ? b->size : 1);
  if(!buf) { snprintf(why, why_len, "Kein Speicher."); return ENOMEM; }
  struct libdeflate_decompressor *dc = libdeflate_alloc_decompressor();
  size_t got = 0;
  int rc = 0;
  if(!dc || libdeflate_zlib_decompress(dc, b->z, b->z_size, buf, b->size, &got) != LIBDEFLATE_SUCCESS || got != b->size) {
    rc = EIO;
    snprintf(why, why_len, "Die mitgelieferte Datei %s ließ sich nicht entpacken.", b->name);
  } else {
    char sha[65];
    sha_of(buf, got, sha);
    if(strcmp(sha, b->sha256)) { rc = EIO; snprintf(why, why_len, "Die mitgelieferte Datei %s ist beschädigt (Prüfsumme).", b->name); }
    else {
      rc = put_file(path, buf, got);
      if(rc) snprintf(why, why_len, "%s ließ sich nicht in den Ordner %s legen: %s", b->name, strrchr(k->dir, '/') + 1, strerror(rc));
    }
  }
  if(dc) libdeflate_free_decompressor(dc);
  free(buf);
  return rc;
}

/* A built-in file unpacked and checked, for the other modules that carry built-in files (plugins.c, trace.c). */
int
ps5tm_blob_unpack(const ps5tm_emu_blob_t *b, uint8_t **data, size_t *len) {
  uint8_t *buf = malloc(b->size ? b->size : 1);
  if(!buf) return ENOMEM;
  struct libdeflate_decompressor *dc = libdeflate_alloc_decompressor();
  size_t got = 0;
  int rc = 0;
  if(!dc || libdeflate_zlib_decompress(dc, b->z, b->z_size, buf, b->size, &got) != LIBDEFLATE_SUCCESS || got != b->size) rc = EIO;
  else {
    char sha[65];
    sha_of(buf, got, sha);
    if(strcmp(sha, b->sha256)) rc = EIO;
  }
  if(dc) libdeflate_free_decompressor(dc);
  if(rc) { free(buf); return rc; }
  *data = buf;
  *len = got;
  return 0;
}

int
ps5tm_put_lib_file(const char *path, const uint8_t *d, size_t n) {
  return put_file(path, d, n);
}

/* Reads one store file: its mark, size and hash. 1 when it can be offered. */
static int
read_version(const al_kind_t *k, const char *name, al_ver_t *v) {
  if(!parse_name(k, name, v)) return 0;
  char path[PATH_MAX];
  snprintf(path, sizeof(path), "%s/%s", k->dir, name);
  uint8_t *d = NULL;
  size_t n = 0;
  if(slurp(path, &d, &n, AL_MAX_BYTES) != 0) return 0;
  v->size = n;
  sha_of(d, n, v->sha);
  /* SELF magic 4F 15 3D 1D, and the mark of every APR-EMU build */
  v->valid = n > 0x100 && d[0] == 0x4F && d[1] == 0x15 && d[2] == 0x3D && d[3] == 0x1D && has_mark(d, n, k->mark);
  v->stored = 1;
  const ps5tm_emu_blob_t *b = find_blob(k, name);
  v->bundled = b && !strcmp(b->sha256, v->sha);
  free(d);
  return 1;
}

static int
ver_cmp_num(const char *a, const char *b) {
  while(*a || *b) {
    unsigned long x = strtoul(a, (char **)&a, 10), y = strtoul(b, (char **)&b, 10);
    if(x != y) return x < y ? -1 : 1;
    if(*a == '.') a++;
    if(*b == '.') b++;
  }
  return 0;
}

static int
cmp_ver(const void *pa, const void *pb) {
  const al_ver_t *a = pa, *b = pb;
  int c = ver_cmp_num(b->version, a->version);          /* newest first */
  if(c) return c;
  return strcmp(a->variant, b->variant);
}

/* All versions: the files of the folder AMPR_EMU and those built into the app that are not in the folder yet, newest first.
   The caller frees *out. */
static size_t
list_versions(const al_kind_t *k, al_ver_t **out) {
  *out = NULL;
  store_ready(k);
  size_t n = 0, cap = 16 + *k->nblobs;
  al_ver_t *v = malloc(cap * sizeof(*v));
  if(!v) return 0;
  DIR *d = opendir(k->dir);
  struct dirent *e;
  while(d && (e = readdir(d)) != NULL) {
    if(e->d_name[0] == '.') continue;
    al_ver_t x;
    if(!read_version(k, e->d_name, &x)) continue;
    if(n == cap) {
      al_ver_t *nv = realloc(v, (cap *= 2) * sizeof(*v));
      if(!nv) break;
      v = nv;
    }
    v[n++] = x;
  }
  if(d) closedir(d);
  for(unsigned i = 0; i < *k->nblobs; i++) {
    const ps5tm_emu_blob_t *b = &k->blobs[i];
    int have = 0;
    for(size_t j = 0; j < n; j++) if(!strcmp(v[j].name, b->name)) { have = 1; break; }
    if(have || n == cap) continue;
    al_ver_t x;
    if(!parse_name(k, b->name, &x)) continue;
    x.valid = 1;
    x.bundled = 1;
    x.stored = 0;
    x.size = b->size;
    snprintf(x.sha, sizeof(x.sha), "%s", b->sha256);
    v[n++] = x;
  }
  if(n) qsort(v, n, sizeof(*v), cmp_ver);
  *out = v;
  return n;
}

static const al_ver_t *
find_by_sha(const al_ver_t *v, size_t n, const char *sha) {
  for(size_t i = 0; i < n; i++)
    if(v[i].valid && !strcmp(v[i].sha, sha)) return &v[i];
  return NULL;
}

static void
label_of(const al_ver_t *v, char *out, size_t n) {
  snprintf(out, n, "%s%s%s", v->version, v->variant[0] ? " " : "", v->variant);
}

/* ---------------------------------------------------------------- ShadowMountPlus settings that matter here */

typedef struct { int present; int update_emulators; char path[300]; } al_emus_t;

static void
read_emus(const al_kind_t *k, al_emus_t *e) {
  memset(e, 0, sizeof(*e));
  e->update_emulators = 1;                                   /* the default of ShadowMountPlus */
  snprintf(e->path, sizeof(e->path), "%s/emus/%s", AL_SM_DIR, k->lib);
  uint8_t *d = NULL;
  size_t n = 0;
  if(slurp(AL_SM_CONFIG, &d, &n, 256 * 1024) == 0) {
    d[n] = 0;
    char *save = NULL;
    for(char *line = strtok_r((char *)d, "\r\n", &save); line; line = strtok_r(NULL, "\r\n", &save)) {
      while(*line == ' ' || *line == '\t') line++;
      if(*line == '#' || *line == ';' || !*line) continue;
      char *eq = strchr(line, '=');
      if(!eq) continue;
      *eq = 0;
      char *key = line, *val = eq + 1;
      for(char *q = key + strlen(key); q > key && (q[-1] == ' ' || q[-1] == '\t'); ) *--q = 0;
      while(*val == ' ' || *val == '\t') val++;
      for(char *q = val + strlen(val); q > val && (q[-1] == ' ' || q[-1] == '\t'); ) *--q = 0;
      if(!strcasecmp(key, "update_emulators"))
        e->update_emulators = !(!strcasecmp(val, "0") || !strcasecmp(val, "false") || !strcasecmp(val, "no") || !strcasecmp(val, "off"));
      else if(!strcasecmp(key, "emulators_path") && val[0] == '/' && strlen(val) < 200)
        snprintf(e->path, sizeof(e->path), "%s/%s", val[strlen(val) - 1] == '/' ? (val[strlen(val) - 1] = 0, val) : val, k->lib);
    }
    free(d);
  }
  e->present = is_file(e->path);
}

/* ---------------------------------------------------------------- a big stack for the image reader */

typedef struct { void *(*fn)(void *); void *arg; } big_t;

static void *
big_main(void *p) {
  big_t *b = p;
  b->fn(b->arg);
  return NULL;
}

static int
run_big(void *(*fn)(void *), void *arg) {
  big_t b = { fn, arg };
  pthread_t th;
  pthread_attr_t at;
  pthread_attr_init(&at);
  pthread_attr_setstacksize(&at, AL_STACK);
  int rc = pthread_create(&th, &at, big_main, &b);
  pthread_attr_destroy(&at);
  if(rc != 0) return -1;
  pthread_join(th, NULL);
  return 0;
}

/* ---------------------------------------------------------------- what is in a game, and where a change goes */

typedef struct {
  const al_kind_t *k;
  ps5tm_libdel_t li;
  int      folder;                 /* the game is a folder */
  int      readable;               /* the content could be looked at */
  int      lib_present, lib_emu, lib_packs, has_orig, fakelib_dir, fakelib2, packed;
  uint64_t lib_size;
  char     lib_sha[65];
  char     scanpath[256];
  char     bp_dir[300];            /* <scanpath>/backports/<id>[ suffix] that exists, or the one that would be made */
  int      bp_exists, bp_fakelib, bp_fakelib2, bp_lib_present, bp_lib_emu, bp_lib_packs, bp_has_orig;
  char     bp_sha[65];
  uint64_t bp_lib_size;
  char     place[10];              /* "game", "backport" or "" */
  char     why[240];
} al_probe_t;

static void
parent_of(const char *path, char *out, size_t n) {
  snprintf(out, n, "%s", path);
  char *s = strrchr(out, '/');
  if(s && s != out) *s = 0;
}

static void
probe_backport(al_probe_t *p, const char *id) {
  const char *roots[2] = { p->scanpath, AL_BP_FALLBACK };
  snprintf(p->bp_dir, sizeof(p->bp_dir), "%s/backports/%s", p->scanpath, id);
  for(int i = 0; i < 2; i++) {
    char found[300];
    if(!ps5tm_backport_dir(roots[i], id, found, sizeof(found))) continue;
    snprintf(p->bp_dir, sizeof(p->bp_dir), "%s", found);
    p->bp_exists = 1;
    break;
  }
  if(!p->bp_exists) return;
  char f[PATH_MAX];
  snprintf(f, sizeof(f), "%s/fakelib", p->bp_dir);
  p->bp_fakelib = is_dir(f);
  snprintf(f, sizeof(f), "%s/fakelib2", p->bp_dir);
  p->bp_fakelib2 = is_dir(f);
  snprintf(f, sizeof(f), "%s/fakelib/%s", p->bp_dir, p->k->lib);
  uint8_t *d = NULL;
  size_t n = 0;
  if(slurp(f, &d, &n, AL_MAX_BYTES) == 0) {
    p->bp_lib_present = 1;
    p->bp_lib_emu = is_replacement(d, n);
    p->bp_lib_packs = reads_packs(d, n);
    p->bp_lib_size = n;
    sha_of(d, n, p->bp_sha);
    free(d);
  }
  snprintf(f, sizeof(f), "%s/fakelib/%s" AL_ORIG, p->bp_dir, p->k->lib);
  p->bp_has_orig = is_file(f);
}

/* the reading half, on a big stack (an image goes through imgread.c) */
static void *
probe_thread(void *arg) {
  al_probe_t *p = arg;
  uint8_t *d = NULL;
  size_t n = 0;
  if(p->folder) {
    char f[PATH_MAX];
    snprintf(f, sizeof(f), "%s/fakelib", p->li.path);
    p->fakelib_dir = is_dir(f);
    snprintf(f, sizeof(f), "%s/fakelib2", p->li.path);
    p->fakelib2 = is_dir(f);
    snprintf(f, sizeof(f), "%s/ampr_assets.index", p->li.path);
    p->packed = p->k == &K_AMPR && is_file(f);
    snprintf(f, sizeof(f), "%s/fakelib/%s", p->li.path, p->k->lib);
    if(slurp(f, &d, &n, AL_MAX_BYTES) == 0) {
      p->lib_present = 1; p->lib_emu = is_replacement(d, n); p->lib_packs = reads_packs(d, n); p->lib_size = n; sha_of(d, n, p->lib_sha);
      free(d);
    }
    snprintf(f, sizeof(f), "%s/fakelib/%s" AL_ORIG, p->li.path, p->k->lib);
    p->has_orig = is_file(f);
    p->readable = 1;
    return NULL;
  }
  imgr_t *r = imgr_open(p->li.path, NULL, 0);
  if(!r) return NULL;
  p->readable = 1;
  uint8_t probe[1];
  p->fakelib_dir = imgr_list(r, "/fakelib", NULL, NULL) == 0;
  p->fakelib2 = imgr_list(r, "/fakelib2", NULL, NULL) == 0;
  p->packed = p->k == &K_AMPR && imgr_pread(r, "/ampr_assets.index", 0, probe, 1) >= 0;
  char ip[160];
  snprintf(ip, sizeof(ip), "/fakelib/%s" AL_ORIG, p->k->lib);
  p->has_orig = imgr_pread(r, ip, 0, probe, 1) >= 0;
  d = malloc(AL_MAX_BYTES);
  if(d) {
    snprintf(ip, sizeof(ip), "/fakelib/%s", p->k->lib);
    long got = imgr_pread(r, ip, 0, d, AL_MAX_BYTES);
    if(got >= 0) {
      p->lib_present = 1; p->lib_emu = is_replacement(d, (size_t)got); p->lib_packs = reads_packs(d, (size_t)got); p->lib_size = (uint64_t)got;
      sha_of(d, (size_t)got, p->lib_sha);
    }
    free(d);
  }
  imgr_close(r);
  return NULL;
}

/* Fills the probe for a title. 0, or -1 when the title is not on the list. */
static int
probe_game(const al_kind_t *k, const char *id, al_probe_t *p) {
  memset(p, 0, sizeof(*p));
  p->k = k;
  if(!id || ps5tm_library_delete_info(id, &p->li) != 0) return -1;
  p->folder = !strcmp(p->li.format, "folder");
  int image = !p->folder && strcmp(p->li.format, "pkg") && p->li.path[0] == '/';
  if(!p->folder && !image) {
    snprintf(p->why, sizeof(p->why), "%s", !strcmp(p->li.format, "pkg") ? "Installierte Spiele (PKG) haben keinen Spielordner, in den die Datei käme."
                                                                       : "Wo die Spieldaten liegen, ist nicht bekannt.");
    return 0;
  }
  parent_of(p->li.path, p->scanpath, sizeof(p->scanpath));
  if(run_big(probe_thread, p) != 0) { snprintf(p->why, sizeof(p->why), "Das Spiel lässt sich gerade nicht lesen."); return 0; }
  probe_backport(p, id);
  if(!p->readable && !p->folder) { snprintf(p->why, sizeof(p->why), "Das Abbild lässt sich nicht lesen."); return 0; }
  /* where a change goes */
  if(p->bp_exists) {
    snprintf(p->place, sizeof(p->place), "backport");
    if(p->bp_fakelib2) snprintf(p->why, sizeof(p->why), "Im Backport-Ordner liegt ein fakelib2: ShadowMountPlus liest nur dieses, eine Datei in fakelib bliebe ungenutzt. Zuerst fakelib2 entfernen.");
  } else if(!strcmp(p->li.format, "ffpfsc")) {
    snprintf(p->place, sizeof(p->place), "backport");
    if(p->fakelib2) snprintf(p->why, sizeof(p->why), "Das Abbild enthält ein fakelib2; ein Backport-Ordner würde es verdecken. Zuerst entpacken.");
  } else {
    snprintf(p->place, sizeof(p->place), "game");
    if(!p->folder && !p->li.smp) snprintf(p->why, sizeof(p->why), "Nur für Abbilder, die ShadowMountPlus verwaltet.");
    else if(!p->folder && !ps5tm_smp_available(NULL, 0)) snprintf(p->why, sizeof(p->why), "ShadowMountPlus antwortet gerade nicht.");
  }
  return 0;
}

/* ---------------------------------------------------------------- the page: versions and state */

struct cJSON *
ps5tm_amprlib_versions(const char *lib) {
  const al_kind_t *k = kind_of(lib);
  if(!k) return NULL;
  cJSON *o = cJSON_CreateObject();
  cJSON_AddBoolToObject(o, "ok", 1);
  cJSON_AddStringToObject(o, "lib", k->id);
  cJSON_AddStringToObject(o, "dir", k->dir);
  cJSON *arr = cJSON_AddArrayToObject(o, "versions");
  al_ver_t *v = NULL;
  size_t n = list_versions(k, &v);                             /* also makes the folder, so that the page can upload into it */
  for(size_t i = 0; i < n; i++) {
    cJSON *e = cJSON_CreateObject();
    char lab[80];
    label_of(&v[i], lab, sizeof(lab));
    cJSON_AddStringToObject(e, "name", v[i].name);
    cJSON_AddStringToObject(e, "label", lab);
    cJSON_AddStringToObject(e, "version", v[i].version);
    cJSON_AddStringToObject(e, "variant", v[i].variant);
    cJSON_AddNumberToObject(e, "size", (double)v[i].size);
    cJSON_AddStringToObject(e, "sha256", v[i].sha);
    cJSON_AddBoolToObject(e, "pack", v[i].pack);
    cJSON_AddBoolToObject(e, "debug", v[i].debug);
    cJSON_AddBoolToObject(e, "valid", v[i].valid);
    cJSON_AddBoolToObject(e, "stored", v[i].stored);
    cJSON_AddBoolToObject(e, "bundled", v[i].bundled);
    cJSON_AddItemToArray(arr, e);
  }
  free(v);
  al_emus_t em;
  read_emus(k, &em);
  cJSON *e = cJSON_AddObjectToObject(o, "emus");
  cJSON_AddStringToObject(e, "path", em.path);
  cJSON_AddBoolToObject(e, "present", em.present);
  cJSON_AddBoolToObject(e, "update_emulators", em.update_emulators);
  return o;
}

/* The built-in versions put into the folder AMPR_EMU, those that are not there yet. The versions JSON, with "installed". */
struct cJSON *
ps5tm_amprlib_install_bundled(const char *lib, char *why, size_t why_len) {
  if(why && why_len) why[0] = 0;
  const al_kind_t *k = kind_of(lib);
  if(!k) { snprintf(why, why_len, "Unbekannte Bibliothek."); return NULL; }
  unsigned put = 0;
  for(unsigned i = 0; i < *k->nblobs; i++) {
    const ps5tm_emu_blob_t *b = &k->blobs[i];
    char path[PATH_MAX];
    store_ready(k);
    snprintf(path, sizeof(path), "%s/%s", k->dir, b->name);
    if(is_file(path)) continue;
    int rc = materialize(k, b, why, why_len);
    if(rc) return NULL;
    put++;
  }
  cJSON *o = ps5tm_amprlib_versions(lib);
  cJSON_AddNumberToObject(o, "installed", put);
  return o;
}

struct cJSON *
ps5tm_amprlib_state(const char *lib, const char *id, int *http) {
  *http = 200;
  const al_kind_t *k = kind_of(lib);
  if(!k) { *http = 400; return NULL; }
  al_probe_t *p = calloc(1, sizeof(*p));
  if(!p) { *http = 500; return NULL; }
  if(probe_game(k, id, p) != 0) { free(p); *http = 404; return NULL; }
  al_ver_t *vers = NULL;
  size_t nv = list_versions(k, &vers);
  cJSON *o = cJSON_CreateObject();
  cJSON_AddBoolToObject(o, "ok", 1);
  cJSON_AddStringToObject(o, "lib", k->id);
  cJSON_AddStringToObject(o, "id", id);
  cJSON_AddStringToObject(o, "name", p->li.name);
  cJSON_AddStringToObject(o, "format", p->li.format);
  cJSON_AddStringToObject(o, "place", p->place);
  cJSON_AddStringToObject(o, "game_path", p->li.path);
  cJSON_AddBoolToObject(o, "can_change", p->place[0] && !p->why[0] && p->readable);
  if(p->why[0]) cJSON_AddStringToObject(o, "why", p->why);
  cJSON_AddBoolToObject(o, "packed", p->packed);
  cJSON_AddBoolToObject(o, "fakelib2", p->fakelib2);
  /* the file that counts: the backport one when ShadowMountPlus reads that folder, else the game's own */
  int use_bp = !strcmp(p->place, "backport");
  int present = use_bp ? p->bp_lib_present : p->lib_present;
  cJSON *inst = cJSON_AddObjectToObject(o, "installed");
  cJSON_AddBoolToObject(inst, "present", present);
  if(present) {
    const char *sha = use_bp ? p->bp_sha : p->lib_sha;
    const al_ver_t *m = find_by_sha(vers, nv, sha);
    cJSON_AddNumberToObject(inst, "size", (double)(use_bp ? p->bp_lib_size : p->lib_size));
    cJSON_AddStringToObject(inst, "sha256", sha);
    cJSON_AddBoolToObject(inst, "emu", use_bp ? p->bp_lib_emu : p->lib_emu);
    cJSON_AddBoolToObject(inst, "reads_packs", use_bp ? p->bp_lib_packs : p->lib_packs);
    cJSON_AddBoolToObject(inst, "known", m != NULL);
    if(m) { char lab[80]; label_of(m, lab, sizeof(lab)); cJSON_AddStringToObject(inst, "label", lab); cJSON_AddStringToObject(inst, "name", m->name); cJSON_AddBoolToObject(inst, "pack", m->pack); }
  }
  cJSON_AddBoolToObject(inst, "has_orig", use_bp ? p->bp_has_orig : p->has_orig);
  /* what the game itself brings, when a backport hides it */
  if(use_bp) {
    cJSON_AddBoolToObject(inst, "game_lib", p->lib_present);
    cJSON_AddBoolToObject(inst, "game_lib_emu", p->lib_emu);
  }
  cJSON *bp = cJSON_AddObjectToObject(o, "backport");
  cJSON_AddStringToObject(bp, "dir", p->bp_dir);
  cJSON_AddBoolToObject(bp, "exists", p->bp_exists);
  cJSON_AddBoolToObject(bp, "fakelib2", p->bp_fakelib2);
  al_emus_t em;
  read_emus(k, &em);
  cJSON *e = cJSON_AddObjectToObject(o, "emus");
  cJSON_AddStringToObject(e, "path", em.path);
  cJSON_AddBoolToObject(e, "present", em.present);
  cJSON_AddBoolToObject(e, "update_emulators", em.update_emulators);
  cJSON_AddBoolToObject(e, "overrides", em.present && em.update_emulators);
  cJSON_AddNumberToObject(o, "versions", (double)nv);
  free(vers);
  free(p);
  return o;
}


/* ---------------------------------------------------------------- the changes */

#include <stdarg.h>

static void say(char *buf, size_t n, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
static void
say(char *buf, size_t n, const char *fmt, ...) {
  if(!buf || !n) return;
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, n, fmt, ap);
  va_end(ap);
}

static void
add_note(char *buf, size_t n, const char *text) {
  if(!buf || !n || !text[0]) return;
  size_t l = strlen(buf);
  if(l && l + 1 < n) { buf[l++] = ' '; buf[l] = 0; }
  snprintf(buf + l, n - l, "%s", text);
}

/* Puts the store file in <fakelib>/libSceAmpr.sprx. The game's own file is kept once as .orig: taken from the target if
   it is there and no replacement, else from game_own (what the image's fakelib holds, for a new backport). */
static int
put_lib(const al_kind_t *k, const char *fakelib, const al_ver_t *ver, const uint8_t *data, size_t n, const uint8_t *game_own, size_t game_own_len,
        char *note, size_t note_len, char *why, size_t why_len) {
  char target[PATH_MAX], orig[PATH_MAX];
  if(snprintf(target, sizeof(target), "%s/%s", fakelib, k->lib) >= (int)sizeof(target) ||
     snprintf(orig, sizeof(orig), "%s/%s" AL_ORIG, fakelib, k->lib) >= (int)sizeof(orig)) {
    say(why, why_len, "Der Pfad ist zu lang.");
    return ENAMETOOLONG;
  }
  int rc = mkdir_p(fakelib);
  if(rc) { say(why, why_len, "Der Ordner fakelib ließ sich nicht anlegen: %s", strerror(rc)); return rc; }
  if(!is_file(orig)) {
    uint8_t *cur = NULL;
    size_t cl = 0;
    if(slurp(target, &cur, &cl, AL_MAX_BYTES) == 0) {
      if(!is_replacement(cur, cl)) {
        rc = put_file(orig, cur, cl);
        if(rc) { free(cur); say(why, why_len, "Das Original ließ sich nicht sichern: %s", strerror(rc)); return rc; }
        add_note(note, note_len, "Die Datei des Spiels ist als libSceAmpr.sprx.orig gesichert.");
      }
      free(cur);
    } else if(game_own && game_own_len && !is_replacement(game_own, game_own_len)) {
      rc = put_file(orig, game_own, game_own_len);
      if(rc) { say(why, why_len, "Das Original ließ sich nicht sichern: %s", strerror(rc)); return rc; }
      add_note(note, note_len, "Die Datei des Spiels ist als libSceAmpr.sprx.orig gesichert.");
    }
  }
  make_writable(target);
  rc = put_file(target, data, n);
  if(rc) { say(why, why_len, "Die Datei ließ sich nicht schreiben: %s", strerror(rc)); return rc; }
  uint8_t *back = NULL;
  size_t bl = 0;
  char sha[65];
  if(slurp(target, &back, &bl, AL_MAX_BYTES) != 0) { say(why, why_len, "Die geschriebene Datei ist nicht lesbar."); return EIO; }
  sha_of(back, bl, sha);
  free(back);
  if(strcmp(sha, ver->sha)) { say(why, why_len, "Die geschriebene Datei weicht von der Version ab. Ist das Laufwerk in Ordnung?"); return EIO; }
  return 0;
}

/* The .orig back as libSceAmpr.sprx; the .orig goes. */
static int
restore_lib(const al_kind_t *k, const char *fakelib, char *why, size_t why_len) {
  char target[PATH_MAX], orig[PATH_MAX];
  snprintf(target, sizeof(target), "%s/%s", fakelib, k->lib);
  snprintf(orig, sizeof(orig), "%s/%s" AL_ORIG, fakelib, k->lib);
  uint8_t *d = NULL;
  size_t n = 0;
  int rc = slurp(orig, &d, &n, AL_MAX_BYTES);
  if(rc) { say(why, why_len, "Das gesicherte Original ist nicht lesbar: %s", strerror(rc)); return rc; }
  make_writable(target);
  rc = put_file(target, d, n);
  free(d);
  if(rc) { say(why, why_len, "Das Original ließ sich nicht zurückschreiben: %s", strerror(rc)); return rc; }
  make_writable(orig);
  unlink(orig);
  return 0;
}

/* names of the image's /fakelib */
typedef struct { char names[64][96]; unsigned n; } al_names_t;

static int
collect_cb(const char *name, int dir, void *ctx) {
  al_names_t *c = ctx;
  if(!dir && c->n < 64 && strlen(name) < sizeof(c->names[0]) && !strchr(name, '/')) snprintf(c->names[c->n++], sizeof(c->names[0]), "%s", name);
  return 0;
}

/* What the image's fakelib holds (read, nothing mounted): the game's own libSceAmpr.sprx, and with mirror a copy of the
   other files into the backport fakelib dest (never over a file that is there). */
typedef struct {
  const al_kind_t *k;
  const char *image;
  const char *dest;
  int         mirror;
  unsigned    copied;
  int         rc;
  char        why[240];
  uint8_t    *own;
  size_t      own_len;
} al_img_t;

/* A whole file of the image; the caller frees. NULL when it is not there or too big (64 MiB). */
static uint8_t *
img_read_all(imgr_t *r, const char *path, size_t *len) {
  size_t cap = 1u << 20, have = 0;
  uint8_t *buf = malloc(cap);
  if(!buf) return NULL;
  for(;;) {
    if(have == cap) {
      if(cap >= (64u << 20)) { free(buf); return NULL; }
      uint8_t *nb = realloc(buf, cap *= 2);
      if(!nb) { free(buf); return NULL; }
      buf = nb;
    }
    long got = imgr_pread(r, path, have, buf + have, cap - have);
    if(got < 0) { free(buf); return NULL; }
    if(got == 0) break;
    have += (size_t)got;
  }
  *len = have;
  return buf;
}

static void *
img_thread(void *arg) {
  al_img_t *m = arg;
  imgr_t *r = imgr_open(m->image, NULL, 0);
  if(!r) { m->rc = EIO; say(m->why, sizeof(m->why), "Das Abbild lässt sich nicht lesen."); return NULL; }
  char ownp[160];
  snprintf(ownp, sizeof(ownp), "/fakelib/%s", m->k->lib);
  m->own = img_read_all(r, ownp, &m->own_len);
  if(m->mirror) {
    al_names_t *c = calloc(1, sizeof(*c));
    if(c && imgr_list(r, "/fakelib", collect_cb, c) == 0) {
      for(unsigned i = 0; i < c->n && !m->rc; i++) {
        char own_orig[120];
        snprintf(own_orig, sizeof(own_orig), "%s" AL_ORIG, m->k->lib);
        if(!strcmp(c->names[i], m->k->lib) || !strcmp(c->names[i], own_orig)) continue;
        char src[160], dst[PATH_MAX];
        snprintf(src, sizeof(src), "/fakelib/%s", c->names[i]);
        if(snprintf(dst, sizeof(dst), "%s/%s", m->dest, c->names[i]) >= (int)sizeof(dst)) continue;
        if(is_file(dst)) continue;
        size_t ln = 0;
        uint8_t *d = img_read_all(r, src, &ln);
        if(!d) { m->rc = EIO; say(m->why, sizeof(m->why), "%s im Abbild ließ sich nicht lesen.", c->names[i]); break; }
        int rc = put_file(dst, d, ln);
        free(d);
        if(rc) { m->rc = rc; say(m->why, sizeof(m->why), "%s ließ sich nicht kopieren: %s", c->names[i], strerror(rc)); break; }
        m->copied++;
      }
    }
    free(c);
  }
  imgr_close(r);
  return NULL;
}

static int
dir_empty(const char *p) {
  DIR *d = opendir(p);
  if(!d) return 0;
  struct dirent *e;
  int empty = 1;
  while((e = readdir(d)) != NULL)
    if(strcmp(e->d_name, ".") && strcmp(e->d_name, "..")) { empty = 0; break; }
  closedir(d);
  return empty;
}

/* An empty backport folder would hide the game's own fakelib for nothing: it goes, and the "backports" folder above it
   when that is empty too. */
static void
drop_empty_backport(const char *fakelib, const char *bp_dir) {
  if(dir_empty(fakelib)) rmdir(fakelib);
  if(dir_empty(bp_dir)) {
    rmdir(bp_dir);
    char parent[300];
    snprintf(parent, sizeof(parent), "%s", bp_dir);
    char *s = strrchr(parent, '/');
    if(s && s != parent) {
      *s = 0;
      const char *base = strrchr(parent, '/');
      if(base && !strcmp(base + 1, "backports") && dir_empty(parent)) rmdir(parent);
    }
  }
}

/* The emulator files of ShadowMountPlus replace this file at the start of a game. */
static void
note_emus(const al_kind_t *k, char *note, size_t n) {
  al_emus_t em;
  read_emus(k, &em);
  if(em.present && em.update_emulators)
    add_note(note, n, k == &K_AMPR ? "ShadowMountPlus ersetzt libSceAmpr.sprx beim Spielstart aus dem Emulator-Ordner (update_emulators=1): diese Datei gewinnt dann."
                                   : "ShadowMountPlus ersetzt libScePlayGo.sprx beim Spielstart aus dem Emulator-Ordner (update_emulators=1): diese Datei gewinnt dann.");
}

int
ps5tm_amprlib_place(const char *lib, const char *id, char *place, size_t place_len, char *why, size_t why_len) {
  const al_kind_t *k = kind_of(lib);
  if(!k) return -1;
  al_probe_t *p = calloc(1, sizeof(*p));
  if(!p) return -1;
  int rc = probe_game(k, id, p);
  if(rc == 0) {
    say(place, place_len, "%s", p->place);
    say(why, why_len, "%s", p->why);
    if(!p->place[0] && !p->why[0]) say(why, why_len, "Für dieses Spiel geht das nicht.");
  }
  free(p);
  return rc;
}

/* op: "apply" (store_name), "restore" (the .orig back) or "remove" (the .orig back when there is one, else the file goes).
   root: the game's root (a folder, or an image mounted read-write) for the place "game"; NULL for the place "backport".
   0 on success (note says what else is worth knowing); an errno with why otherwise. */
int
ps5tm_amprlib_run(const char *lib, const char *op, const char *id, const char *root, const char *store_name, char *note, size_t note_len,
                  char *why, size_t why_len) {
  if(note && note_len) note[0] = 0;
  if(why && why_len) why[0] = 0;
  const al_kind_t *k = kind_of(lib);
  if(!k) { say(why, why_len, "Unbekannte Bibliothek."); return EINVAL; }
  int apply = !strcmp(op, "apply"), restore = !strcmp(op, "restore"), remove_ = !strcmp(op, "remove");
  if(!apply && !restore && !remove_) { say(why, why_len, "Unbekannter Vorgang."); return EINVAL; }
  al_ver_t ver;
  uint8_t *data = NULL;
  size_t dn = 0;
  memset(&ver, 0, sizeof(ver));
  if(apply) {
    const ps5tm_emu_blob_t *blob = store_name ? find_blob(k, store_name) : NULL;
    if(blob) {
      int rc1 = materialize(k, blob, why, why_len);              /* it then also stands in the store folder */
      if(rc1) return rc1;
    }
    if(!store_name || !read_version(k, store_name, &ver) || !ver.valid) {
      say(why, why_len, k == &K_AMPR ? "Diese Version gibt es nicht im Speicher, oder sie ist keine APR-EMU-Datei."
                                     : "Diese Version gibt es nicht im Speicher, oder sie ist keine PlayGo-Datei.");
      return ENOENT;
    }
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/%s", k->dir, store_name);
    int rc0 = slurp(path, &data, &dn, AL_MAX_BYTES);
    if(rc0) { say(why, why_len, "Die Datei der Version ist nicht lesbar: %s", strerror(rc0)); return rc0; }
    char sha[65];
    sha_of(data, dn, sha);
    if(strcmp(sha, ver.sha)) { free(data); say(why, why_len, "Die Datei der Version hat sich beim Lesen verändert."); return EIO; }
  }
  int rc = 0;
  char fakelib[PATH_MAX];
  int packed = 0;
  uint8_t *own = NULL;
  size_t own_len = 0;
  al_probe_t *p = NULL;
  char bp_dir[300] = "";

  if(root) {                                              /* the game itself */
    snprintf(fakelib, sizeof(fakelib), "%s/fakelib", root);
    char f[PATH_MAX];
    snprintf(f, sizeof(f), "%s/ampr_assets.index", root);
    packed = k == &K_AMPR && is_file(f);
    snprintf(f, sizeof(f), "%s/fakelib2", root);
    if(is_dir(f)) add_note(note, note_len, "Im Spielordner liegt auch ein fakelib2: ShadowMountPlus ab 1.7beta1 liest dann nur dieses, die Datei in fakelib bliebe ungenutzt.");
  } else {                                                /* a backport folder */
    p = calloc(1, sizeof(*p));
    if(!p || probe_game(k, id, p) != 0) { rc = ENOENT; say(why, why_len, "Das Spiel ist nicht mehr in der Liste."); goto out; }
    if(strcmp(p->place, "backport") || p->why[0]) { rc = EPERM; say(why, why_len, "%s", p->why[0] ? p->why : "Hier geht das nicht."); goto out; }
    snprintf(fakelib, sizeof(fakelib), "%s/fakelib", p->bp_dir);
    snprintf(bp_dir, sizeof(bp_dir), "%s", p->bp_dir);
    packed = p->packed;
    if(apply && !(packed && !ver.pack) && !p->bp_exists) {
      /* a new backport hides the game's own fakelib: it gets a copy of what is there first */
      rc = mkdir_p(fakelib);
      if(rc) { say(why, why_len, "Der Backport-Ordner ließ sich nicht anlegen: %s", strerror(rc)); goto out; }
      al_img_t *m = calloc(1, sizeof(*m));
      if(!m) { rc = ENOMEM; goto out; }
      m->k = k;
      m->image = p->li.path;
      m->dest = fakelib;
      m->mirror = 1;
      if(run_big(img_thread, m) != 0) { m->rc = EIO; say(m->why, sizeof(m->why), "Das Abbild lässt sich gerade nicht lesen."); }
      own = m->own;
      own_len = m->own_len;
      if(m->rc) {
        rc = m->rc;
        say(why, why_len, "%s", m->why);
        free(m);
        drop_empty_backport(fakelib, bp_dir);
        goto out;
      }
      if(m->copied) {
        char t[200];
        say(t, sizeof(t), "Der neue Backport-Ordner verdeckt die fakelib des Spiels; %u ihrer Dateien sind hineinkopiert.", m->copied);
        add_note(note, note_len, t);
      }
      free(m);
      add_note(note, note_len, "Die Datei liegt im Backport-Ordner, das Spiel selbst bleibt unberührt.");
    }
  }

  if(packed && !(apply && ver.pack)) {
    rc = EPERM;
    say(why, why_len, apply ? "Das Spiel liegt in LZ4-Paketen: nur eine Version mit Paketunterstützung (pack) kann sie lesen."
                            : "Das Spiel liegt in LZ4-Paketen: die APR-EMU-Datei lässt sich erst ändern, wenn es wieder entpackt ist.");
    goto out;
  }

  if(apply) {
    rc = put_lib(k, fakelib, &ver, data, dn, own, own_len, note, note_len, why, why_len);
    if(!rc) note_emus(k, note, note_len);
  } else {
    char orig[PATH_MAX], target[PATH_MAX];
    snprintf(orig, sizeof(orig), "%s/%s" AL_ORIG, fakelib, k->lib);
    snprintf(target, sizeof(target), "%s/%s", fakelib, k->lib);
    if(is_file(orig)) {
      rc = restore_lib(k, fakelib, why, why_len);
    } else if(restore) {
      rc = ENOENT;
      say(why, why_len, "Es ist kein Original gesichert.");
    } else {
      make_writable(target);
      if(is_file(target) && unlink(target) != 0) {
        rc = errno ? errno : EIO;
        say(why, why_len, "Die Datei ließ sich nicht entfernen: %s", strerror(rc));
      } else if(bp_dir[0]) {
        drop_empty_backport(fakelib, bp_dir);
      }
    }
  }
out:
  free(data);
  free(own);
  free(p);
  return rc;
}

/* For the check of LZ4 packs (imgjob_lz4.inc, after LZ4 Compression 1.4.3, read on 15.10.2026): does the APR-EMU that
   ShadowMountPlus gives the game read packs? Counted is the folder that wins: the backport folder before the game, fakelib2
   before fakelib, the first that exists. 0 it does; 1 it was missing or reads no packs, and with repair the built-in
   0.4.2.1 test-pack is now in fakelib (the game's own file kept as .orig); 2 a fakelib2 without the reader wins, which this
   app leaves alone (why says so); -1 with why. warn: ShadowMountPlus would replace the file at the start of the game from
   its emulator folder by one that reads no packs. root: the game folder. */
int
ps5tm_amprlib_pack_check(const char *id, const char *root, int repair, char *warn, size_t warn_len, char *why, size_t why_len) {
  if(warn && warn_len) warn[0] = 0;
  if(why && why_len) why[0] = 0;
  al_probe_t *p = calloc(1, sizeof(*p));
  if(!p) { say(why, why_len, "Kein Speicher."); return -1; }
  if(probe_game(&K_AMPR, id, p) != 0 || !p->folder) { free(p); say(why, why_len, "Das Spiel ist nicht mehr in der Liste."); return -1; }
  char dir[PATH_MAX];
  int bp = 0, two = 0;
  if(p->bp_exists && p->bp_fakelib2) { snprintf(dir, sizeof(dir), "%s/fakelib2", p->bp_dir); bp = 1; two = 1; }
  else if(p->bp_exists && p->bp_fakelib) { snprintf(dir, sizeof(dir), "%s/fakelib", p->bp_dir); bp = 1; }
  else if(p->fakelib2) { snprintf(dir, sizeof(dir), "%s/fakelib2", root); two = 1; }
  else snprintf(dir, sizeof(dir), "%s/fakelib", root);
  char f[PATH_MAX + 32];
  snprintf(f, sizeof(f), "%s/%s", dir, K_AMPR.lib);
  uint8_t *d = NULL;
  size_t n = 0;
  int ok = slurp(f, &d, &n, AL_MAX_BYTES) == 0 && reads_packs(d, n);
  free(d);
  int rc = 0;
  if(!ok && two) {
    rc = 2;
    if(bp) say(why, why_len, "Im Backport-Ordner liegt ein fakelib2 ohne Paketleser; ShadowMountPlus liest dann nur dieses. Bitte fakelib2 dort entfernen.");
    else say(why, why_len, "Im Spielordner liegt ein fakelib2 ohne Paketleser; ShadowMountPlus liest dann nur dieses. Bitte den Mitschnitt beenden oder fakelib2 entfernen.");
  } else if(!ok) {
    rc = 1;
    if(repair) {
      char note[300] = "";
      int r = ps5tm_amprlib_run("ampr", "apply", id, bp ? NULL : root, "libSceAmpr.sprx-0.4.2.1-test-pack", note, sizeof(note), why, why_len);
      if(r) rc = -1;
      else {                                                    /* a file of the person under that name is used instead of the app's */
        d = NULL;
        n = 0;
        if(!(slurp(f, &d, &n, AL_MAX_BYTES) == 0 && reads_packs(d, n))) {
          rc = -1;
          say(why, why_len, "Die Datei libSceAmpr.sprx-0.4.2.1-test-pack im Ordner AMPR_EMU liest keine LZ4-Pakete. Lösche sie dort, dann nimmt die App die mitgelieferte.");
        }
        free(d);
      }
    }
  }
  al_emus_t em;
  read_emus(&K_AMPR, &em);
  if(em.present && em.update_emulators) {
    d = NULL;
    n = 0;
    if(slurp(em.path, &d, &n, AL_MAX_BYTES) == 0 && !reads_packs(d, n))
      say(warn, warn_len, "ShadowMountPlus ersetzt libSceAmpr.sprx beim Spielstart aus %s (update_emulators=1), und diese Datei liest keine LZ4-Pakete: "
          "Das gepackte Spiel bliebe schwarz. Lege dort eine Version mit „pack“ ab oder setze update_emulators=0.", em.path);
    free(d);
  }
  free(p);
  return rc;
}
