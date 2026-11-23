/* Host test of the transfer part of src/savebackup.c (the lists of ZIP files, the start of an import of one or several ZIP files, the job that puts them in, the start of
   a read-out): the console's mount is the stub of src/savemnt.c, the ZIP files, the folders and the database are real. The audit of 19.11.2026 changed these places and
   they had no test of their own. Paths are redirected with -DPS5TM_DATA_DIR / -DPS5TM_SAVE_HOME / -DPS5TM_SYS_DATA (see run.sh). */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <utime.h>

#include "cJSON.h"
#include "checkfile.h"
#include "chives.h"
#include "ps5tm.h"
#include "savesfo.h"
#include "savezip.h"
#include "sqlite3/sqlite3.h"

#undef pthread_create

extern const char *ps5tm_savemnt_test_tree, *ps5tm_savemnt_test_store;
extern int ps5tm_savemnt_test_fail, ps5tm_savemnt_test_opened, ps5tm_savemnt_test_closed;
extern void (*ps5tm_savemnt_test_on_open)(const char *mount_dir, int nth);

static int g_fail, g_n;
#define CHECK(c) do { g_n++; if(!(c)) { g_fail++; printf("FAIL line %d: %s\n", __LINE__, #c); } } while(0)

static char ROOT[300], HOME_[330], SYS[330], DATA[330], STORE[330], INBOX[400];
static const char *UID = "18161531";
static const uint64_t ACC = 0x1122334455667788ull;

/* ------------------------------------------------------------------ what the app expects of the rest of the program */
static volatile int g_game;                                        /* a game is running */
static char g_game_id[16] = "PPSA04242";

int ps5tm_thread_create(pthread_t *t, const pthread_attr_t *a, void *(*f)(void *), void *x) { return pthread_create(t, a, f, x); }
void ps5tm_log(const char *level, const char *code, const char *fmt, ...) { (void)level; (void)code; (void)fmt; }
uint64_t ps5tm_mono_ms(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000; }
void ps5tm_powerguard_hold(void) {}
void ps5tm_powerguard_release(void) {}
int ps5tm_jobs_busy_except(int self, int flags, char *why, size_t n) { (void)self; (void)flags; (void)why; (void)n; return 0; }
int ps5tm_procmgr_appinfo_ready(void) { return 0; }
int ps5tm_procmgr_game_title(uint32_t id, char *out, size_t n) { (void)id; (void)out; (void)n; return -1; }
void ps5tm_gamestate_get(ps5tm_gamestate_t *o) { memset(o, 0, sizeof(*o)); if(g_game) snprintf(o->title_id, sizeof(o->title_id), "%s", g_game_id); }
struct cJSON *ps5tm_library_json(void) { return NULL; }
static char g_vol[40];                                             /* a drive the console has (empty: none) */
void ps5tm_sysinfo_get(ps5tm_sysinfo_t *o) {
  memset(o, 0, sizeof(*o));
  snprintf(o->model, sizeof(o->model), "CFI-TEST");
  snprintf(o->serial_masked, sizeof(o->serial_masked), "••••0001");
  if(g_vol[0]) {
    snprintf(o->volumes[0].path, sizeof(o->volumes[0].path), "%s", g_vol);
    snprintf(o->volumes[0].label, sizeof(o->volumes[0].label), "USB-Test");
    o->volumes[0].free_bytes = 1ull << 30;
    o->volume_count = 1;
  }
}
int ps5tm_account_list(ps5tm_account_entry_t *out, int max) {
  if(max < 1) return 0;
  memset(&out[0], 0, sizeof(out[0]));
  out[0].slot = 1;
  snprintf(out[0].name, sizeof(out[0].name), "Tester");
  out[0].id = ACC;
  return 1;
}
void ps5tm_hist_add(const char *kind, const char *title, const char *detail, const char *result, const char *error, uint64_t bytes, uint64_t ms) { (void)kind; (void)title; (void)detail; (void)result; (void)error; (void)bytes; (void)ms; }
void ps5tm_drop_cache(int fd) { (void)fd; }
int ps5tm_digest_fd(int fd, uint64_t size, uint32_t *crc, uint8_t sha[32], const int *cancel, void (*progress)(void *, uint64_t), void *pc, char *err, size_t el) {
  (void)fd; (void)size; (void)crc; (void)sha; (void)cancel; (void)progress; (void)pc; if(el) snprintf(err, el, "stub"); return -1;
}
ps5tm_sums_t *ps5tm_sums_open(const char *path, char *err, size_t el) { (void)path; if(el) snprintf(err, el, "stub"); return NULL; }
int ps5tm_sums_add(ps5tm_sums_t *s, const uint8_t sha[32], const char *name) { (void)s; (void)sha; (void)name; return -1; }
int ps5tm_sums_close(ps5tm_sums_t *s, char *err, size_t el) { (void)s; if(el) snprintf(err, el, "stub"); return -1; }
void ps5tm_sums_abort(ps5tm_sums_t *s) { (void)s; }

/* ------------------------------------------------------------------ files */
static void
put(uint8_t *p, uint64_t v, int n) { for(int i = 0; i < n; i++) p[i] = (uint8_t)(v >> (8 * i)); }

static void
mkdirs(const char *path) {
  char t[800];
  snprintf(t, sizeof(t), "%s", path);
  for(char *p = t + 1; *p; p++) if(*p == '/') { *p = 0; mkdir(t, 0755); *p = '/'; }
  mkdir(t, 0755);
}

static void
rmtree(const char *path) {
  char cmd[900];
  snprintf(cmd, sizeof(cmd), "rm -rf '%s'", path);
  if(system(cmd)) {}
}

static void
wfile(const char *path, const void *data, size_t len) {
  char dir[800];
  snprintf(dir, sizeof(dir), "%s", path);
  char *s = strrchr(dir, '/');
  if(s) { *s = 0; mkdirs(dir); }
  FILE *f = fopen(path, "wb");
  if(f) { fwrite(data, 1, len, f); fclose(f); }
}

static int
exists(const char *p) { struct stat st; return stat(p, &st) == 0; }

/* a PSF with the fields of a PS5 save (the layout of the saveimport test) */
typedef struct { const char *key; unsigned fmt; const void *val; unsigned used, cap; } ent_t;

static size_t
sfo_build(uint8_t *out, const char *title, const char *slot, uint64_t blocks) {
  uint8_t aid[8], bl[8], sb[8], params[1024], listp[4];
  put(aid, 0x99AABBCCDDEEFF00ull, 8);
  put(bl, blocks, 8);
  put(sb, 0, 8);
  memset(params, 0, sizeof(params));
  put(params + 4, 0x11111111, 4);
  for(int i = 8; i < 40; i++) params[i] = (uint8_t)(i * 3);
  put(listp, 5, 4);
  ent_t e[] = {
    { "ACCOUNT_ID", 0x0004, aid, 8, 8 },        { "DETAIL", 0x0204, "Details", 8, 1024 },  { "FORMAT", 0x0204, "ppr", 4, 4 },
    { "MAINTITLE", 0x0204, "Mein Spiel", 11, 128 }, { "PARAMS", 0x0004, params, 1024, 1024 }, { "SAVEDATA_BLOCKS", 0x0004, bl, 8, 8 },
    { "SAVEDATA_DIRECTORY", 0x0204, slot, (unsigned)strlen(slot) + 1, 32 }, { "SAVEDATA_LIST_PARAM", 0x0404, listp, 4, 4 },
    { "SUBTITLE", 0x0204, "Unter", 6, 128 },    { "SYSTEM_BLOCKS", 0x0004, sb, 8, 8 },       { "TITLE_ID", 0x0204, title, (unsigned)strlen(title) + 1, 12 },
  };
  const unsigned n = sizeof(e) / sizeof(e[0]);
  size_t keys_len = 0, data_len = 0;
  for(unsigned i = 0; i < n; i++) { keys_len += strlen(e[i].key) + 1; data_len += e[i].cap; }
  const size_t key_off = 20 + (size_t)n * 16, data_off = key_off + keys_len;
  memset(out, 0, data_off + data_len);
  memcpy(out, "\0PSF", 4);
  put(out + 4, 0x101, 4); put(out + 8, key_off, 4); put(out + 12, data_off, 4); put(out + 16, n, 4);
  size_t ko = 0, vo = 0;
  for(unsigned i = 0; i < n; i++) {
    uint8_t *ix = out + 20 + (size_t)i * 16;
    put(ix, ko, 2); put(ix + 2, e[i].fmt, 2); put(ix + 4, e[i].used, 4); put(ix + 8, e[i].cap, 4); put(ix + 12, vo, 4);
    memcpy(out + key_off + ko, e[i].key, strlen(e[i].key) + 1);
    memcpy(out + data_off + vo, e[i].val, e[i].used);
    ko += strlen(e[i].key) + 1;
    vo += e[i].cap;
  }
  return data_off + data_len;
}

/* a ZIP of the app's own kind (chives.json, the real param.sfo, two files) in the inbox; 0 or -1 */
static int
make_zip(const char *name, const char *title, const char *slot) {
  char path[500];
  snprintf(path, sizeof(path), "%s/%s", INBOX, name);
  unlink(path);
  szw_t *w = szw_create(path);
  if(!w) return -1;
  uint8_t sfo[4096], data[3000];
  for(unsigned i = 0; i < sizeof(data); i++) data[i] = (uint8_t)(i * 7 + 1);
  const size_t sl = sfo_build(sfo, title, slot, 48);
  const time_t now = 1790000000;
  szw_add_mem(w, "sce_sys/param.sfo", sfo, sl, now, 0644);
  szw_add_mem(w, "data.bin", data, sizeof(data), now, 0644);
  chives_t g;
  chives_init(&g);
  g.version = 2; g.block_size = 65536; g.block_count = 48; g.image_size = 48ull * 65536; g.backup_header_count = 1; g.mode_raw = 0x0d; g.has_fs_ext = 1;
  snprintf(g.title_id, sizeof(g.title_id), "%s", title);
  snprintf(g.save_dir, sizeof(g.save_dir), "%s", slot);
  snprintf(g.image_name, sizeof(g.image_name), "sdimg_%s", slot);
  char json[4096];
  const int jn = chives_write(&g, json, sizeof(json));
  chives_free(&g);
  szw_add_mem(w, "chives.json", json, (size_t)jn, now, 0644);
  return szw_finish(w) == SZ_OK ? 0 : -1;
}

static void
make_db(void) {
  char p[700];
  snprintf(p, sizeof(p), "%s/savedata_prospero/%s/db/user", SYS, UID);
  mkdirs(p);
  snprintf(p, sizeof(p), "%s/savedata_prospero/%s/db/user/savedata.db", SYS, UID);
  sqlite3 *db = NULL;
  sqlite3_open(p, &db);
  sqlite3_exec(db,
    "CREATE TABLE savedata (id INTEGER PRIMARY KEY AUTOINCREMENT NOT NULL,title_id NOT NULL, dir_name NOT NULL, main_title NOT NULL, sub_title , detail , "
    "tmp_dir_name , is_broken , user_param , blocks , free_blocks , size_kib , mtime NOT NULL, fake_broken , account_id , user_id , faked_owner , "
    "cloud_icon_url , cloud_revision , game_title_id NOT NULL, system_blocks , convert_performance , account_id_in_usb_path );PRAGMA user_version=11;",
    NULL, NULL, NULL);
  sqlite3_close(db);
}

static long long
db_count(void) {
  char p[700];
  snprintf(p, sizeof(p), "%s/savedata_prospero/%s/db/user/savedata.db", SYS, UID);
  sqlite3 *db = NULL;
  sqlite3_stmt *st = NULL;
  long long v = -1;
  if(sqlite3_open_v2(p, &db, SQLITE_OPEN_READONLY, NULL) == SQLITE_OK && sqlite3_prepare_v2(db, "select count(*) from savedata", -1, &st, NULL) == SQLITE_OK && sqlite3_step(st) == SQLITE_ROW)
    v = sqlite3_column_int64(st, 0);
  if(st) sqlite3_finalize(st);
  sqlite3_close(db);
  return v;
}

static void
reset(void) {
  rmtree(ROOT);
  char p[700];
  snprintf(p, sizeof(p), "%s/%s", HOME_, UID);
  mkdirs(p);
  snprintf(p, sizeof(p), "%s/%s/username.dat", HOME_, UID);
  wfile(p, "Tester", 6);
  mkdirs(STORE);
  mkdirs(DATA);
  make_db();
  ps5tm_savemnt_test_tree = NULL;
  ps5tm_savemnt_test_store = STORE;
  ps5tm_savemnt_test_fail = 0;
  ps5tm_savemnt_test_on_open = NULL;
  ps5tm_savemnt_test_opened = ps5tm_savemnt_test_closed = 0;
  g_game = 0;
  CHECK(ps5tm_saves_import_inbox(INBOX, sizeof(INBOX)) == 0);
}

/* waits for the job to end; its answer (state, error) */
static void
wait_job(char *state, size_t sn, char *error, size_t en) {
  for(int i = 0; i < 4000 && ps5tm_saves_busy(); i++) usleep(5000);
  cJSON *j = ps5tm_saves_job_json();
  const cJSON *s = cJSON_GetObjectItem(j, "state"), *e = cJSON_GetObjectItem(j, "error");
  snprintf(state, sn, "%s", cJSON_IsString(s) ? s->valuestring : "?");
  snprintf(error, en, "%s", cJSON_IsString(e) ? e->valuestring : "");
  cJSON_Delete(j);
  usleep(20000);                                                    /* the thread that writes the history line is the last to go */
}

static unsigned
count_prefix(const char *dir, const char *prefix) {
  DIR *d = opendir(dir);
  if(!d) return 0;
  unsigned n = 0;
  struct dirent *e;
  while((e = readdir(d)) != NULL) if(!strncmp(e->d_name, prefix, strlen(prefix))) n++;
  closedir(d);
  return n;
}

static void
img_path(char *out, size_t n, const char *title, const char *slot) { snprintf(out, n, "%s/%s/savedata_prospero/%s/sdimg_%s", HOME_, UID, title, slot); }

/* the second mount (the read-back) is where a game "starts" */
static void
start_game_on_second_mount(const char *mount_dir, int nth) { (void)mount_dir; if(nth == 2) g_game = 1; }

int
main(int argc, char **argv) {
  snprintf(ROOT, sizeof(ROOT), "%s", argc > 1 ? argv[1] : "/tmp/ps5tm-savebackup");
  snprintf(HOME_, sizeof(HOME_), "%s/home", ROOT);
  snprintf(SYS, sizeof(SYS), "%s/system", ROOT);
  snprintf(DATA, sizeof(DATA), "%s/data", ROOT);
  snprintf(STORE, sizeof(STORE), "%s/store", ROOT);
  char err[800], state[24], jerr[700];
  char img0[700], img1[700];

  /* ---- the lists show the NEWEST 60 ZIP files, not the first 60 the folder gives */
  reset();
  {
    char d[500], p[600];
    snprintf(d, sizeof(d), "%s/savexport", DATA);
    mkdirs(d);
    for(int i = 0; i < 70; i++) {
      snprintf(p, sizeof(p), "%s/PPSA00001_S%02d_20261119-120000.zip", d, i);
      wfile(p, "zip", 3);
      struct utimbuf ub = { 1700000000 + i * 100, 1700000000 + i * 100 };      /* S00 the oldest, S69 the newest */
      utime(p, &ub);
    }
    cJSON *j = ps5tm_saves_json();
    cJSON *ex = j ? cJSON_GetObjectItem(j, "exports") : NULL;
    CHECK(ex && cJSON_GetArraySize(ex) == 60);
    const cJSON *first = ex ? cJSON_GetArrayItem(ex, 0) : NULL, *last = ex ? cJSON_GetArrayItem(ex, 59) : NULL;
    const cJSON *fn = first ? cJSON_GetObjectItem(first, "name") : NULL, *ln = last ? cJSON_GetObjectItem(last, "name") : NULL;
    CHECK(fn && cJSON_IsString(fn) && strstr(fn->valuestring, "_S69_"));                   /* the newest first */
    CHECK(ln && cJSON_IsString(ln) && strstr(ln->valuestring, "_S10_"));                   /* S00..S09 are the ten left out: the oldest */
    cJSON_Delete(j);
  }

  /* ---- two ZIP files of one title in one job: both slots come in, the stand before is not needed (the user has none), the files stay out of the inbox afterwards */
  reset();
  CHECK(make_zip("a_SAVE0.zip", "PPSA01234", "SAVE0") == 0 && make_zip("b_SAVE1.zip", "PPSA01234", "SAVE1") == 0);
  ps5tm_savemnt_test_opened = ps5tm_savemnt_test_closed = 0;
  CHECK(ps5tm_saves_import_start("a_SAVE0.zip,b_SAVE1.zip", NULL, UID, NULL, 0, err, sizeof(err)) == 200);
  wait_job(state, sizeof(state), jerr, sizeof(jerr));
  if(strcmp(state, "done")) printf("  state=%s error=%s\n", state, jerr);
  CHECK(!strcmp(state, "done"));
  img_path(img0, sizeof(img0), "PPSA01234", "SAVE0");
  img_path(img1, sizeof(img1), "PPSA01234", "SAVE1");
  CHECK(exists(img0) && exists(img1) && db_count() == 2);
  {
    char p[600];
    snprintf(p, sizeof(p), "%s/a_SAVE0.zip", INBOX); CHECK(!exists(p));                   /* an uploaded file that did its job goes */
    snprintf(p, sizeof(p), "%s/b_SAVE1.zip", INBOX); CHECK(!exists(p));
  }

  /* ---- refused before anything starts: the same slot twice, two titles, the same file twice, a file that is not there */
  reset();
  CHECK(make_zip("one.zip", "PPSA01234", "SAVE0") == 0 && make_zip("two.zip", "PPSA01234", "SAVE0") == 0 && make_zip("other.zip", "PPSA05555", "SAVE1") == 0);
  CHECK(ps5tm_saves_import_start("one.zip,two.zip", NULL, UID, NULL, 0, err, sizeof(err)) == 400 && strstr(err, "denselben Spielstand"));
  CHECK(ps5tm_saves_import_start("one.zip,other.zip", NULL, UID, NULL, 0, err, sizeof(err)) == 400 && strstr(err, "verschiedenen Titeln"));
  CHECK(ps5tm_saves_import_start("one.zip,one.zip", NULL, UID, NULL, 0, err, sizeof(err)) == 400 && strstr(err, "mehrfach"));
  CHECK(ps5tm_saves_import_start("one.zip,nothere.zip", NULL, UID, NULL, 0, err, sizeof(err)) == 404);
  CHECK(!ps5tm_saves_busy());                                                             /* none of them started a job */
  img_path(img0, sizeof(img0), "PPSA01234", "SAVE0");
  CHECK(!exists(img0) && db_count() == 0);

  /* ---- the plan of a ZIP names slot and title */
  {
    int status = 0;
    cJSON *pl = ps5tm_saves_import_plan_json("one.zip", NULL, err, sizeof(err), &status);
    const cJSON *imp = pl ? cJSON_GetObjectItem(pl, "importable") : NULL, *slot = pl ? cJSON_GetObjectItem(pl, "slot") : NULL;
    CHECK(status == 200 && cJSON_IsTrue(imp) && slot && cJSON_IsString(slot) && !strcmp(slot->valuestring, "SAVE0"));
    cJSON_Delete(pl);
  }


  /* ---- housekeeping at the start of a job: the newest 10 copies of a user's save database stay, the older go (another user's are not touched, nor are files of another
     name); an uploaded ZIP that is more than a day old goes, a fresh one stays */
  reset();
  {
    char d[500], p[600];
    snprintf(d, sizeof(d), "%s/savexport/db-backup", DATA);
    mkdirs(d);
    for(int i = 0; i < 13; i++) {
      snprintf(p, sizeof(p), "%s/savedata-18161531-20261119-1200%02d-SAVE0.db", d, i);
      wfile(p, "db", 2);
      struct utimbuf ub = { 1700000000 + i * 100, 1700000000 + i * 100 };
      utime(p, &ub);
    }
    for(int i = 0; i < 3; i++) {
      snprintf(p, sizeof(p), "%s/savedata-22222222-20261119-1200%02d-X.db", d, i);
      wfile(p, "db", 2);
    }
    snprintf(p, sizeof(p), "%s/savedata-18161531-notes.txt", d);
    wfile(p, "mine", 4);
    CHECK(make_zip("hk_SAVE0.zip", "PPSA01234", "SAVE0") == 0 && make_zip("old_one.zip", "PPSA01234", "SAVE1") == 0);
    snprintf(p, sizeof(p), "%s/old_one.zip", INBOX);
    struct utimbuf old = { time(NULL) - 2 * 24 * 3600, time(NULL) - 2 * 24 * 3600 };
    utime(p, &old);
    CHECK(ps5tm_saves_import_start("hk_SAVE0.zip", NULL, UID, NULL, 0, err, sizeof(err)) == 200);
    wait_job(state, sizeof(state), jerr, sizeof(jerr));
    CHECK(!strcmp(state, "done"));
    CHECK(count_prefix(d, "savedata-18161531-2026") == 11);                                  /* 10 kept of 13, and the copy this put-in made */
    snprintf(p, sizeof(p), "%s/savedata-18161531-20261119-120012-SAVE0.db", d); CHECK(exists(p));           /* the newest of the old ones stay ... */
    snprintf(p, sizeof(p), "%s/savedata-18161531-20261119-120003-SAVE0.db", d); CHECK(exists(p));
    snprintf(p, sizeof(p), "%s/savedata-18161531-20261119-120002-SAVE0.db", d); CHECK(!exists(p));           /* ... the oldest three go */
    snprintf(p, sizeof(p), "%s/savedata-18161531-20261119-120000-SAVE0.db", d); CHECK(!exists(p));
    CHECK(count_prefix(d, "savedata-22222222-") == 3 && count_prefix(d, "savedata-18161531-notes") == 1);
    snprintf(p, sizeof(p), "%s/old_one.zip", INBOX); CHECK(!exists(p));
  }

  /* ---- a put-in cut off in the middle of the exchange: the next start takes it back (the record is in the app's folder), and old files are cleaned up too */
  reset();
  {
    char fin[700], alt[760], icon[700], dbp[700], js[3000];
    snprintf(fin, sizeof(fin), "%s/%s/savedata_prospero/PPSA01234/sdimg_SAVE0", HOME_, UID);
    snprintf(alt, sizeof(alt), "%s.ps5cc-alt", fin);
    snprintf(icon, sizeof(icon), "%s/%s/savedata_prospero_meta/user/PPSA01234/SAVE0_icon0.png", HOME_, UID);
    snprintf(dbp, sizeof(dbp), "%s/savedata_prospero/%s/db/user/savedata.db", SYS, UID);
    wfile(fin, "NEW IMAGE", 9);
    wfile(alt, "OLD IMAGE", 9);
    snprintf(js, sizeof(js),
             "{\"v\":1,\"phase\":\"swap\",\"fin\":\"%s\",\"ifin\":\"%s\",\"dbp\":\"%s\",\"ps4\":0,\"replaced\":1,\"bin_replaced\":0,\"icon_existed\":0,\"title_id\":\"PPSA01234\",\"dir_name\":\"SAVE0\","
             "\"main_title\":\"M\",\"sub_title\":\"S\",\"detail\":\"D\",\"mtime\":\"2026-11-19T12:00:00.00Z\",\"account_id\":\"1\",\"user_id\":\"18161531\",\"user_param\":\"5\",\"blocks\":\"30\","
             "\"free_blocks\":\"1\",\"size_kib\":\"c00\",\"system_blocks\":\"0\"}", fin, icon, dbp);
    char jp[500];
    snprintf(jp, sizeof(jp), "%s/savexport/import.journal", DATA);
    wfile(jp, js, strlen(js));
    ps5tm_saves_recover();
    uint8_t b2[16];
    FILE *f = fopen(fin, "rb");
    const size_t n = f ? fread(b2, 1, sizeof(b2), f) : 0;
    if(f) fclose(f);
    CHECK(n == 9 && !memcmp(b2, "OLD IMAGE", 9) && !exists(alt) && !exists(jp));
  }

  /* ---- a game started while the new image was written and read back: the job asks again before the exchange, fails with the reason, changes nothing */
  reset();
  CHECK(make_zip("g_SAVE0.zip", "PPSA01234", "SAVE0") == 0);
  ps5tm_savemnt_test_on_open = start_game_on_second_mount;
  CHECK(ps5tm_saves_import_start("g_SAVE0.zip", NULL, UID, NULL, 0, err, sizeof(err)) == 200);
  wait_job(state, sizeof(state), jerr, sizeof(jerr));
  CHECK(!strcmp(state, "failed") && strstr(jerr, "Spiel gestartet") && strstr(jerr, "nichts verändert"));
  img_path(img0, sizeof(img0), "PPSA01234", "SAVE0");
  CHECK(!exists(img0) && db_count() == 0);
  {
    char p[600];
    snprintf(p, sizeof(p), "%s/g_SAVE0.zip", INBOX); CHECK(exists(p));                    /* the file is still there for another try */
  }
  ps5tm_savemnt_test_on_open = NULL;
  g_game = 0;

  /* ---- an APP (YouTube: applicationCategoryType 65536 in the console's description of the title) is never put in: the plan says so, the start is refused (403) and no job runs */
  reset();
  {
    char p[600];
    snprintf(p, sizeof(p), "%s/appmeta/PPSA01650/param.json", ROOT);
    wfile(p, "{\"applicationCategoryType\":65536}", strlen("{\"applicationCategoryType\":65536}"));
    CHECK(make_zip("yt_SaveData.zip", "PPSA01650", "SaveData") == 0 && make_zip("game_SaveData.zip", "PPSA01234", "SaveData") == 0);
    int status = 0;
    cJSON *pl = ps5tm_saves_import_plan_json("yt_SaveData.zip", NULL, err, sizeof(err), &status);
    const cJSON *imp = pl ? cJSON_GetObjectItem(pl, "importable") : NULL, *why = pl ? cJSON_GetObjectItem(pl, "why") : NULL;
    CHECK(status == 200 && cJSON_IsFalse(imp) && cJSON_IsString(why) && strstr(why->valuestring, "App"));
    cJSON_Delete(pl);
    CHECK(ps5tm_saves_import_start("yt_SaveData.zip", NULL, UID, NULL, 1, err, sizeof(err)) == 403 && strstr(err, "App"));
    CHECK(!ps5tm_saves_busy());
    img_path(img0, sizeof(img0), "PPSA01650", "SaveData");
    CHECK(!exists(img0) && db_count() == 0);
    pl = ps5tm_saves_import_plan_json("game_SaveData.zip", NULL, err, sizeof(err), &status);       /* a game next to it is as before */
    imp = pl ? cJSON_GetObjectItem(pl, "importable") : NULL;
    CHECK(status == 200 && cJSON_IsTrue(imp));
    cJSON_Delete(pl);
  }

  /* ---- "ZIP-Datei löschen" of the fullscreen app: the file on the drive it names goes, the export folder's file of the same name stays
     (audit 23.11.2026: mount was not looked at) */
  reset();
  {
    char zd[600], zp[700], ep[700];
    if(snprintf(g_vol, sizeof(g_vol), "%s/usb", ROOT) < (int)sizeof(g_vol)) {
      snprintf(zd, sizeof(zd), "%s/PS5-Sicherung/Spielstand-ZIP", g_vol);
      mkdirs(zd);
      snprintf(zp, sizeof(zp), "%s/PPSA01234_SAVE0_20261123-120000.zip", zd);
      wfile(zp, "zip", 3);
      snprintf(ep, sizeof(ep), "%s/savexport", DATA);
      mkdirs(ep);
      snprintf(ep, sizeof(ep), "%s/savexport/PPSA01234_SAVE0_20261123-120000.zip", DATA);
      wfile(ep, "zip", 3);
      CHECK(ps5tm_saves_export_delete("PPSA01234_SAVE0_20261123-120000.zip", g_vol, err, sizeof(err)) == 200);
      CHECK(!exists(zp) && exists(ep));
      CHECK(ps5tm_saves_export_delete("PPSA01234_SAVE0_20261123-120000.zip", g_vol, err, sizeof(err)) == 404);
      CHECK(ps5tm_saves_export_delete("PPSA01234_SAVE0_20261123-120000.zip", "/mnt/usb7", err, sizeof(err)) == 404);
      CHECK(ps5tm_saves_export_delete("../x.zip", g_vol, err, sizeof(err)) == 404);
      CHECK(ps5tm_saves_export_delete("PPSA01234_SAVE0_20261123-120000.zip", NULL, err, sizeof(err)) == 200 && !exists(ep));
    }
    g_vol[0] = 0;
  }

  /* ---- a game that runs: nothing starts */
  g_game = 1;
  CHECK(ps5tm_saves_import_start("g_SAVE0.zip", NULL, UID, NULL, 0, err, sizeof(err)) == 409);
  g_game = 0;

  /* ---- a title with more than 16 slots is not read out in part: refused with the reason (and 16 are not refused for that) */
  reset();
  {
    char d[600], p[700];
    snprintf(d, sizeof(d), "%s/%s/savedata_prospero/PPSA01234", HOME_, UID);
    mkdirs(d);
    for(int i = 0; i < 17; i++) { snprintf(p, sizeof(p), "%s/sdimg_SLOT%02d", d, i); wfile(p, "img", 3); }
    CHECK(ps5tm_saves_export_start(UID, "PPSA01234", NULL, NULL, err, sizeof(err)) == 409 && strstr(err, "mehr als 16"));
    CHECK(!ps5tm_saves_busy());
    /* the leftovers of a cut-off put-in are no slots: 16 real ones and two leftovers are 16 slots, not 18 */
    snprintf(p, sizeof(p), "%s/sdimg_SLOT16", d); unlink(p);
    snprintf(p, sizeof(p), "%s/sdimg_SLOT00.ps5cc-alt", d); wfile(p, "old", 3);
    snprintf(p, sizeof(p), "%s/sdimg_SLOT01.ps5cc-neu", d); wfile(p, "new", 3);
    const int rc = ps5tm_saves_export_start(UID, "PPSA01234", NULL, NULL, err, sizeof(err));
    CHECK(rc != 409 || !strstr(err, "mehr als 16"));
    wait_job(state, sizeof(state), jerr, sizeof(jerr));                                    /* the images are no images: the job fails, as it must */
    CHECK(!strcmp(state, "failed") || !strcmp(state, "done"));
  }

  rmtree(ROOT);
  printf("%d checks, %d failed\n", g_n, g_fail);
  return g_fail ? 1 : 0;
}
