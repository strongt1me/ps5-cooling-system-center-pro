/* Host test of src/saveimport.c: a save from a ZIP of plain files put in (the console's mount and formatter are stubs; the files, the ZIP and the database are real). */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "chives.h"
#include "saveimport.h"
#include "savedb.h"
#include "savemnt.h"
#include "savesfo.h"
#include "savezip.h"
#include "sqlite3/sqlite3.h"

extern const char *ps5tm_savemnt_test_tree, *ps5tm_savemnt_test_store;
extern int ps5tm_savemnt_test_fail, ps5tm_savemnt_test_opened, ps5tm_savemnt_test_closed, ps5tm_savemnt_test_geom_bad;
extern void (*ps5tm_savemnt_test_on_open)(const char *mount_dir, int nth);

static int g_fail, g_n;
#define CHECK(c) do { g_n++; if(!(c)) { g_fail++; printf("FAIL line %d: %s\n", __LINE__, #c); } } while(0)

static char ROOT[300], HOME_[330], SYS[330], WORK[330], STORE[330], DBBK[330], ZIP[330], JOURNAL[360];
static const char *UID = "18161531";
static const uint32_t UIDN = 0x18161531;
static const uint64_t ACC = 0x1122334455667788ull;

static void
put(uint8_t *p, uint64_t v, int n) {
  for(int i = 0; i < n; i++) p[i] = (uint8_t)(v >> (8 * i));
}

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
exists(const char *p) {
  struct stat st;
  return stat(p, &st) == 0;
}

static long long
fsize(const char *p) {
  struct stat st;
  return stat(p, &st) == 0 ? (long long)st.st_size : -1;
}

static unsigned
count_dir(const char *path) {
  DIR *d = opendir(path);
  if(!d) return 0;
  unsigned n = 0;
  struct dirent *e;
  while((e = readdir(d)) != NULL) if(strcmp(e->d_name, ".") && strcmp(e->d_name, "..")) n++;
  closedir(d);
  return n;
}

static size_t
slurp(const char *p, uint8_t *buf, size_t cap) {
  FILE *f = fopen(p, "rb");
  if(!f) return 0;
  size_t n = fread(buf, 1, cap, f);
  fclose(f);
  return n;
}

/* a PSF with the fields of a PS5 save (the layout of the savexport test) */
typedef struct { const char *key; unsigned fmt; const void *val; unsigned used, cap; } ent_t;

static size_t
sfo_build(uint8_t *out, const char *title, const char *slot, uint64_t blocks, uint64_t sysblocks, const char *format, uint64_t account, uint32_t uid) {
  uint8_t aid[8], bl[8], sb[8], params[1024], listp[4];
  put(aid, account, 8);
  put(bl, blocks, 8);
  put(sb, sysblocks, 8);
  memset(params, 0, sizeof(params));
  put(params + 4, uid, 4);
  for(int i = 8; i < 40; i++) params[i] = (uint8_t)(i * 3);
  put(listp, 5, 4);
  ent_t e[] = {
    { "ACCOUNT_ID", 0x0004, aid, 8, 8 },
    { "DETAIL", 0x0204, "Details", 8, 1024 },
    { "FORMAT", 0x0204, format, (unsigned)strlen(format) + 1, 4 },
    { "MAINTITLE", 0x0204, "Mein Spiel", 11, 128 },
    { "PARAMS", 0x0004, params, 1024, 1024 },
    { "SAVEDATA_BLOCKS", 0x0004, bl, 8, 8 },
    { "SAVEDATA_DIRECTORY", 0x0204, slot, (unsigned)strlen(slot) + 1, 32 },
    { "SAVEDATA_LIST_PARAM", 0x0404, listp, 4, 4 },
    { "SUBTITLE", 0x0204, "Unter", 6, 128 },
    { "SYSTEM_BLOCKS", 0x0004, sb, 8, 8 },
    { "TITLE_ID", 0x0204, title, (unsigned)strlen(title) + 1, 12 },
  };
  const unsigned n = sizeof(e) / sizeof(e[0]);
  size_t keys_len = 0, data_len = 0;
  for(unsigned i = 0; i < n; i++) { keys_len += strlen(e[i].key) + 1; data_len += e[i].cap; }
  const size_t key_off = 20 + (size_t)n * 16, data_off = key_off + keys_len;
  memset(out, 0, data_off + data_len);
  memcpy(out, "\0PSF", 4);
  put(out + 4, 0x101, 4);
  put(out + 8, key_off, 4);
  put(out + 12, data_off, 4);
  put(out + 16, n, 4);
  size_t ko = 0, vo = 0;
  for(unsigned i = 0; i < n; i++) {
    uint8_t *ix = out + 20 + (size_t)i * 16;
    put(ix, ko, 2);
    put(ix + 2, e[i].fmt, 2);
    put(ix + 4, e[i].used, 4);
    put(ix + 8, e[i].cap, 4);
    put(ix + 12, vo, 4);
    memcpy(out + key_off + ko, e[i].key, strlen(e[i].key) + 1);
    memcpy(out + data_off + vo, e[i].val, e[i].used);
    ko += strlen(e[i].key) + 1;
    vo += e[i].cap;
  }
  return data_off + data_len;
}

typedef struct {
  const char *prefix;                  /* "" or "x/y/" */
  const char *title, *slot, *format;
  uint64_t blocks, sysblocks;
  uint64_t sfo_blocks;                 /* SAVEDATA_BLOCKS in the param.sfo when it is not the block count of the image (0: the same) */
  uint32_t block_size;
  unsigned fs_ext, backup_headers, mode_raw;
  int with_chives, with_sfo, zero_sfo, two_chives, with_icon, own_list;
  int corrupt_data;                    /* flip a byte of data.bin inside the ZIP after writing */
  int foreign;                         /* the entries the other tool writes beside the files (the raw image, its key, a size) */
  int dir_entries;                     /* folder entries: the save's folder itself, the ones on the way to it, sce_sys (7-Zip and the file manager of Windows write them) */
} zipspec_t;

static uint8_t DATA[100000], ICON[3000], KEYSTONE[96];

static void
init_payload(void) {
  for(unsigned i = 0; i < sizeof(DATA); i++) DATA[i] = (uint8_t)(i * 13 + 5);
  for(unsigned i = 0; i < sizeof(ICON); i++) ICON[i] = (uint8_t)(i ^ 0x5a);
  for(unsigned i = 0; i < sizeof(KEYSTONE); i++) KEYSTONE[i] = (uint8_t)(i + 1);
}

static zipspec_t
good(void) {
  zipspec_t z;
  memset(&z, 0, sizeof(z));
  z.prefix = "";
  z.title = "PPSA01234";
  z.slot = "SAVE0";
  z.format = "ppr";
  z.blocks = 48;
  z.sysblocks = 0;
  z.block_size = 65536;
  z.backup_headers = 1;
  z.mode_raw = 0x0d;
  z.with_chives = z.with_sfo = z.with_icon = z.own_list = 1;
  return z;
}

static int
make_zip(const char *path, const zipspec_t *sp) {
  unlink(path);
  szw_t *w = szw_create(path);
  if(!w) return -1;
  char name[400];
  uint8_t sfo[4096];
  size_t sl = sfo_build(sfo, sp->title, sp->slot, sp->sfo_blocks ? sp->sfo_blocks : sp->blocks, sp->sysblocks, sp->format, 0x99AABBCCDDEEFF00ull, 0x11111111);
  if(sp->zero_sfo) { memset(sfo, 0, 3072); sl = 3072; }
  time_t now = 1790000000;
  if(sp->dir_entries) {
    const size_t pl = strlen(sp->prefix);
    for(size_t i = 0; i < pl; i++)
      if(sp->prefix[i] == '/') { memcpy(name, sp->prefix, i); name[i] = 0; szw_add_dir(w, name, now, 0755); }
    snprintf(name, sizeof(name), "%ssce_sys", sp->prefix);
    szw_add_dir(w, name, now, 0755);
  }
  if(sp->with_sfo) { snprintf(name, sizeof(name), "%ssce_sys/param.sfo", sp->prefix); szw_add_mem(w, name, sfo, sl, now, 0644); }
  snprintf(name, sizeof(name), "%ssce_sys/keystone", sp->prefix); szw_add_mem(w, name, KEYSTONE, sizeof(KEYSTONE), now, 0640);
  snprintf(name, sizeof(name), "%sdata.bin", sp->prefix); szw_add_mem(w, name, DATA, sizeof(DATA), now, 0644);
  snprintf(name, sizeof(name), "%ssub/dir/a.txt", sp->prefix); szw_add_mem(w, name, "hello save\n", 11, now, 0600);
  if(sp->with_icon) { snprintf(name, sizeof(name), "%ssce_sys/icon0.png", sp->prefix); szw_add_mem(w, name, ICON, sizeof(ICON), now, 0644); }
  if(sp->foreign) {
    snprintf(name, sizeof(name), "%sPS5IMAGE", sp->prefix); szw_add_mem(w, name, DATA, 5000, now, 0644);
    snprintf(name, sizeof(name), "%sPS5KEY", sp->prefix); szw_add_mem(w, name, KEYSTONE, 32, now, 0644);
    snprintf(name, sizeof(name), "%sPS5PARAM_SIZE", sp->prefix); szw_add_mem(w, name, "3072", 4, now, 0644);
  }
  if(sp->with_chives) {
    chives_t g;
    chives_init(&g);
    g.version = 2;
    g.block_size = sp->block_size;
    g.block_count = sp->blocks;
    g.image_size = (uint64_t)sp->blocks * sp->block_size;
    g.backup_header_count = sp->backup_headers;
    g.mode_raw = (uint16_t)sp->mode_raw;
    g.has_fs_ext = 1;
    g.fs_ext = sp->fs_ext;
    snprintf(g.title_id, sizeof(g.title_id), "%s", sp->title);
    snprintf(g.save_dir, sizeof(g.save_dir), "%s", sp->slot);
    snprintf(g.image_name, sizeof(g.image_name), "sdimg_%s", sp->slot);
    if(sp->own_list) {
      chives_own_add(&g, "data.bin", 0, 0, 0644);
      chives_own_add(&g, "sce_sys", 0, 0, 0750);
      chives_own_add(&g, "sce_sys/keystone", 0, 0, 0640);
      chives_own_add(&g, "sub", 0, 0, 0750);
      chives_own_add(&g, "sub/dir", 0, 0, 0750);
      chives_own_add(&g, "sub/dir/a.txt", 0, 0, 0600);
      chives_own_add(&g, "emptydir", 0, 0, 0750);
    }
    char json[8192];
    int jn = chives_write(&g, json, sizeof(json));
    chives_free(&g);
    snprintf(name, sizeof(name), "%schives.json", sp->prefix);
    szw_add_mem(w, name, json, (size_t)jn, now, 0644);
    if(sp->two_chives) { snprintf(name, sizeof(name), "%sother/chives.json", sp->prefix); szw_add_mem(w, name, json, (size_t)jn, now, 0644); }
  }
  if(szw_finish(w) != SZ_OK) return -1;
  if(sp->corrupt_data) {                                          /* a bit of the stored data.bin changes: the checksum no longer fits */
    FILE *f = fopen(path, "r+b");
    if(f) { fseek(f, 3000 + 2000, SEEK_SET); int c = fgetc(f); fseek(f, 3000 + 2000, SEEK_SET); fputc(c ^ 0xff, f); fclose(f); }
  }
  return 0;
}

typedef struct { unsigned phases, works, stop_after, polls; uint64_t bytes; unsigned no_return_calls; unsigned polls_at_commit; char names[16][48]; } probe_t;
static void p_phase(void *c, const char *t, uint64_t total) { probe_t *p = c; (void)total; if(p->phases < 16) snprintf(p->names[p->phases], 48, "%s", t); p->phases++; }
static void p_work(void *c, uint64_t n) { probe_t *p = c; p->works++; p->bytes += n; }
static void p_cur(void *c, const char *r) { (void)c; (void)r; }
static int  p_stop(void *c) { probe_t *p = c; p->polls++; return p->stop_after && p->polls >= p->stop_after && !p->no_return_calls; }
static int  p_nocommit(void *c, char *why, size_t n) { (void)c; snprintf(why, n, "Währenddessen wurde ein Spiel gestartet (PPSA00042)."); return 1; }
static int  skip_ppsa99(const char *t) { return !strcmp(t, "PPSA00099"); }
static void p_noret(void *c) { probe_t *p = c; p->no_return_calls++; p->polls_at_commit = p->polls; }

static saveimport_hooks_t *
hooks(probe_t *p) {
  static saveimport_hooks_t h;
  h.base.ctx = p; h.base.phase = p_phase; h.base.work = p_work; h.base.current = p_cur; h.base.stop = p_stop;
  h.no_return = p_noret;
  h.may_commit = NULL;
  return &h;
}

static saveimport_args_t
args(void) {
  saveimport_args_t a;
  memset(&a, 0, sizeof(a));
  a.zip = ZIP;
  a.work_dir = WORK;
  a.home_dir = HOME_;
  a.sys_dir = SYS;
  a.uid = UID;
  a.user_id = UIDN;
  a.account_id = ACC;
  a.db_backup_dir = DBBK;
  a.journal_path = JOURNAL;
  return a;
}

static char IMGDIR[500], IMG[520], ICONP[560], DBP[500];

static void
reset(void) {
  rmtree(ROOT);
  mkdirs(WORK);
  mkdirs(STORE);
  char p[700];
  snprintf(p, sizeof(p), "%s/%s", HOME_, UID);
  mkdirs(p);
  snprintf(p, sizeof(p), "%s/savedata_prospero/%s/db/user", SYS, UID);
  mkdirs(p);
  ps5tm_savemnt_test_tree = NULL;
  ps5tm_savemnt_test_store = STORE;
  ps5tm_savemnt_test_fail = 0;
  ps5tm_savemnt_test_on_open = NULL;
}

static long long
scalar(const char *path, const char *q) {
  sqlite3 *db = NULL;
  sqlite3_stmt *st = NULL;
  long long v = -1;
  if(sqlite3_open_v2(path, &db, SQLITE_OPEN_READONLY, NULL) == SQLITE_OK && sqlite3_prepare_v2(db, q, -1, &st, NULL) == SQLITE_OK && sqlite3_step(st) == SQLITE_ROW)
    v = sqlite3_column_int64(st, 0);
  if(st) sqlite3_finalize(st);
  sqlite3_close(db);
  return v;
}

static void
make_db(void) {
  sqlite3 *db = NULL;
  sqlite3_open(DBP, &db);
  sqlite3_exec(db,
    "CREATE TABLE savedata (id INTEGER PRIMARY KEY AUTOINCREMENT NOT NULL,title_id NOT NULL, dir_name NOT NULL, main_title NOT NULL, sub_title , detail , "
    "tmp_dir_name , is_broken , user_param , blocks , free_blocks , size_kib , mtime NOT NULL, fake_broken , account_id , user_id , faked_owner , "
    "cloud_icon_url , cloud_revision , game_title_id NOT NULL, system_blocks , convert_performance , account_id_in_usb_path );"
    "PRAGMA user_version=11;"
    "INSERT INTO savedata VALUES (7,'PPSA01234','SAVE0','Alt','','','',0,0,10,3,640,'2020-01-01T00:00:00.00Z',0,0,1,0,'',0,'PPSA01234',0,0,'0000000000000000');"
    "INSERT INTO savedata VALUES (9,'PPSA00001','X','Anderes','','','',0,0,10,3,640,'2020-01-01T00:00:00.00Z',0,0,1,0,'',0,'PPSA00001',0,0,'0000000000000000');",
    NULL, NULL, NULL);
  sqlite3_close(db);
}

static char STORED[600];

/* a record of an exchange as an earlier run that was cut off would have left it */
static void
write_journal(const char *phase, const char *title, const char *slot, int replaced, int ps4, int bin_replaced, int icon_existed, const char *fin, const char *ifin, const char *dbp) {
  char js[4096];
  snprintf(js, sizeof(js),
           "{\"v\":1,\"phase\":\"%s\",\"fin\":\"%s\",\"ifin\":\"%s\",\"dbp\":\"%s\",\"ps4\":%d,\"replaced\":%d,\"bin_replaced\":%d,\"icon_existed\":%d,"
           "\"title_id\":\"%s\",\"dir_name\":\"%s\",\"main_title\":\"Mein Spiel\",\"sub_title\":\"Unter\",\"detail\":\"Details\",\"mtime\":\"2026-11-19T12:00:00.00Z\","
           "\"account_id\":\"1122334455667788\",\"user_id\":\"18161531\",\"user_param\":\"5\",\"blocks\":\"30\",\"free_blocks\":\"10\",\"size_kib\":\"c00\",\"system_blocks\":\"0\"}",
           phase, fin, ifin, dbp, ps4, replaced, bin_replaced, icon_existed, title, slot);
  wfile(JOURNAL, js, strlen(js));
}

/* ---- PS4: a ZIP of plain files only (no sidecar), the donors a user's other saves are */
static int
make_ps4_zip(const char *path, const char *title, const char *slot) {
  unlink(path);
  szw_t *w = szw_create(path);
  if(!w) return -1;
  uint8_t sfo[4096];
  size_t sl = sfo_build(sfo, title, slot, 586, 0, "pfs", 0x99AABBCCDDEEFF00ull, 0x11111111);
  time_t now = 1790000000;
  szw_add_mem(w, "sce_sys/param.sfo", sfo, sl, now, 0644);
  szw_add_mem(w, "sce_sys/icon0.png", ICON, sizeof(ICON), now, 0644);
  szw_add_mem(w, "data.bin", DATA, sizeof(DATA), now, 0640);
  szw_add_mem(w, "sub/a.txt", "hello ps4\n", 10, now, 0600);
  return szw_finish(w) == SZ_OK ? 0 : -1;
}

static char IMG4[560], BIN4[560], ICON4[600], DBP4[520];
static uint8_t g_marker;                                        /* nonzero: a donor's PARAMS bytes 8..40 are marker + 0..31 and its byte 100 is 0x77 (the incoming ones are 24..117 and 0) */

static size_t
mark_params(uint8_t *sfo, size_t sl) {
  if(g_marker) {
    uint8_t mk[32];
    for(int i = 0; i < 32; i++) mk[i] = (uint8_t)(g_marker + i);
    const uint8_t b = 0x77;
    savesfo_set_raw(sfo, sl, "PARAMS", 8, mk, sizeof(mk));
    savesfo_set_raw(sfo, sl, "PARAMS", 100, &b, 1);
  }
  return sl;
}

/* a save of the same user that is already on the console: image, key file, and what its mount shows (a param.sfo that names the account and the user) */
static void
make_donor(const char *title, const char *slot, uint64_t account, uint32_t uid) {
  char p[800];
  uint8_t key[96];
  for(int i = 0; i < 96; i++) key[i] = (uint8_t)(0x30 + i);
  snprintf(p, sizeof(p), "%s/%s/savedata/%s/sdimg_%s", HOME_, UID, title, slot);
  wfile(p, "DONOR IMAGE BYTES", 17);
  snprintf(p, sizeof(p), "%s/%s/savedata/%s/%s.bin", HOME_, UID, title, slot);
  wfile(p, key, sizeof(key));
  uint8_t sfo[4096];
  size_t sl = sfo_build(sfo, title, slot, 100, 0, "pfs", account, uid);
  sl = mark_params(sfo, sl);
  snprintf(p, sizeof(p), "%s/sdimg_%s/sce_sys/param.sfo", STORE, slot);
  wfile(p, sfo, sl);
}

/* the same for a PS5 save of the user: an image in savedata_prospero (no key file), what its mount shows */
static void
make_donor5(const char *title, const char *slot, uint64_t account, uint32_t uid) {
  char p[800];
  snprintf(p, sizeof(p), "%s/%s/savedata_prospero/%s/sdimg_%s", HOME_, UID, title, slot);
  wfile(p, "DONOR5 IMAGE BYTES", 18);
  uint8_t sfo[4096];
  size_t sl = sfo_build(sfo, title, slot, 100, 0, "ppr", account, uid);
  sl = mark_params(sfo, sl);
  snprintf(p, sizeof(p), "%s/sdimg_%s/sce_sys/param.sfo", STORE, slot);
  wfile(p, sfo, sl);
}

/* PARAMS of the param.sfo the last import left in the mount of an image */
static int
stored_params(const char *slot, uint8_t params[1024]) {
  uint8_t buf[8192];
  char p[800];
  snprintf(p, sizeof(p), "%s/sdimg_%s/sce_sys/param.sfo", STORE, slot);
  const size_t n = slurp(p, buf, sizeof(buf));
  return n > 100 && savesfo_valid(buf, n) && savesfo_get_raw(buf, n, "PARAMS", params, 1024) == 1024 ? 0 : -1;
}

static void
reset4(void) {
  reset();
  char p[700];
  snprintf(p, sizeof(p), "%s/savedata/%s/db/user", SYS, UID);
  mkdirs(p);
  snprintf(DBP4, sizeof(DBP4), "%s/savedata/%s/db/user/savedata.db", SYS, UID);
  snprintf(IMG4, sizeof(IMG4), "%s/%s/savedata/CUSA12345/sdimg_SLOT0", HOME_, UID);
  snprintf(BIN4, sizeof(BIN4), "%s/%s/savedata/CUSA12345/SLOT0.bin", HOME_, UID);
  snprintf(ICON4, sizeof(ICON4), "%s/%s/savedata_meta/user/CUSA12345/SLOT0_icon0.png", HOME_, UID);
}

static void
make_db4(void) {
  sqlite3 *db = NULL;
  sqlite3_open(DBP4, &db);
  sqlite3_exec(db,
    "CREATE TABLE savedata (id INTEGER PRIMARY KEY AUTOINCREMENT NOT NULL,title_id NOT NULL, dir_name NOT NULL, main_title NOT NULL, sub_title , detail , "
    "tmp_dir_name , is_broken , user_param , blocks , free_blocks , size_kib , mtime NOT NULL, fake_broken , account_id , user_id , faked_owner , "
    "cloud_icon_url , cloud_revision , game_title_id NOT NULL, system_blocks , convert_performance , account_id_in_usb_path );"
    "PRAGMA user_version=11;"
    "INSERT INTO savedata VALUES (3,'CUSA12345','SLOT0','Alt','','','',0,0,10,3,320,'2020-01-01T00:00:00.00Z',0,0,1,0,'',0,'CUSA12345',0,0,'0000000000000000');",
    NULL, NULL, NULL);
  sqlite3_close(db);
}

int
main(int argc, char **argv) {
  snprintf(ROOT, sizeof(ROOT), "%s", argc > 1 ? argv[1] : "/tmp/ps5tm-saveimport");
  snprintf(HOME_, sizeof(HOME_), "%s/home", ROOT);
  snprintf(SYS, sizeof(SYS), "%s/system", ROOT);
  snprintf(WORK, sizeof(WORK), "%s/data/work", ROOT);
  snprintf(STORE, sizeof(STORE), "%s/store", ROOT);
  snprintf(DBBK, sizeof(DBBK), "%s/data/dbbk", ROOT);
  snprintf(ZIP, sizeof(ZIP), "%s/in.zip", ROOT);
  snprintf(JOURNAL, sizeof(JOURNAL), "%s/data/journal/import.journal", ROOT);
  snprintf(IMGDIR, sizeof(IMGDIR), "%s/%s/savedata_prospero/PPSA01234", HOME_, UID);
  snprintf(IMG, sizeof(IMG), "%s/sdimg_SAVE0", IMGDIR);
  snprintf(ICONP, sizeof(ICONP), "%s/%s/savedata_prospero_meta/user/PPSA01234/SAVE0_icon0.png", HOME_, UID);
  snprintf(DBP, sizeof(DBP), "%s/savedata_prospero/%s/db/user/savedata.db", SYS, UID);
  snprintf(STORED, sizeof(STORED), "%s/sdimg_SAVE0", STORE);
  init_payload();

  char err[800];
  probe_t pr;
  saveimport_result_t res;
  saveimport_info_t info;
  zipspec_t sp;

  /* ---- inspect: what a ZIP says */
  reset();
  sp = good();
  CHECK(make_zip(ZIP, &sp) == 0);
  CHECK(saveimport_inspect(ZIP, &info, err, sizeof(err)) == 0 && info.importable);
  CHECK(!strcmp(info.title_id, "PPSA01234") && !strcmp(info.slot, "SAVE0") && !strcmp(info.main_title, "Mein Spiel") && !strcmp(info.sub_title, "Unter"));
  CHECK(info.files == 5 && info.dirs == 0 && info.bytes == sizeof(DATA) + sizeof(ICON) + sizeof(KEYSTONE) + 11 + info.bytes - (sizeof(DATA) + sizeof(ICON) + sizeof(KEYSTONE) + 11));
  CHECK(info.block_size == 65536 && info.block_count == 48 && info.image_size == 48ull * 65536 && info.user_param == 5);
  CHECK(info.src_account == 0x99AABBCCDDEEFF00ull && info.src_user == 0x11111111 && info.prefix[0] == 0);

  sp = good(); sp.prefix = "PPSA01234/SAVE0/";
  CHECK(make_zip(ZIP, &sp) == 0 && saveimport_inspect(ZIP, &info, err, sizeof(err)) == 0 && info.importable && !strcmp(info.prefix, "PPSA01234/SAVE0/") && info.files == 5);

  struct { zipspec_t s; const char *want; } bad[16];
  int nb = 0;
  sp = good(); sp.with_chives = 0; bad[nb].s = sp; bad[nb++].want = "fehlt chives.json";
  sp = good(); sp.two_chives = 1; bad[nb].s = sp; bad[nb++].want = "mehrere Spielstände";
  sp = good(); sp.fs_ext = 3; bad[nb].s = sp; bad[nb++].want = "Blockzahlen";                    /* fs_ext says a copy-on-write image, the param.sfo says it is not */
  sp = good(); sp.mode_raw = 5; bad[nb].s = sp; bad[nb++].want = "Abbildformat";
  sp = good(); sp.zero_sfo = 1; bad[nb].s = sp; bad[nb++].want = "param.sfo";
  sp = good(); sp.with_sfo = 0; bad[nb].s = sp; bad[nb++].want = "fehlt sce_sys/param.sfo";
  sp = good(); sp.format = "pfs"; bad[nb].s = sp; bad[nb++].want = "ppr";
  sp = good(); sp.blocks = 48; sp.sysblocks = 5; bad[nb].s = sp; bad[nb++].want = "Blockzahlen";
  sp = good(); sp.title = "CUSA01234"; bad[nb].s = sp; bad[nb++].want = "PS5-Kennung";
  sp = good(); sp.slot = "sce_bu_x"; bad[nb].s = sp; bad[nb++].want = "ungültig";
  sp = good(); sp.backup_headers = 2; bad[nb].s = sp; bad[nb++].want = "Abbildformat";
  sp = good(); sp.blocks = 96; sp.fs_ext = 96; sp.sfo_blocks = 0; sp.sysblocks = 0; bad[nb].s = sp; bad[nb++].want = "keine gültige Beschreibung";    /* no data blocks at all: the sidecar itself is refused */
  sp = good(); sp.blocks = 96; sp.fs_ext = 48; sp.sfo_blocks = 48; sp.sysblocks = 0; bad[nb].s = sp; bad[nb++].want = "Blockzahlen";               /* copy-on-write, but SYSTEM_BLOCKS 0 */
  sp = good(); sp.blocks = 96; sp.fs_ext = 48; sp.sfo_blocks = 96; sp.sysblocks = 48; bad[nb].s = sp; bad[nb++].want = "Blockzahlen";              /* SAVEDATA_BLOCKS counts the system blocks too */
  for(int i = 0; i < nb; i++) {
    make_zip(ZIP, &bad[i].s);
    int rc = saveimport_inspect(ZIP, &info, err, sizeof(err));
    if(rc != 0 || info.importable || !strstr(info.why, bad[i].want)) printf("  case %d: rc=%d importable=%d why=%s\n", i, rc, info.importable, info.why);
    CHECK(rc == 0 && !info.importable && strstr(info.why, bad[i].want));
  }
  wfile(ZIP, "this is no zip", 14);
  CHECK(saveimport_inspect(ZIP, &info, err, sizeof(err)) != 0 && strstr(err, "keine lesbare ZIP"));
  CHECK(saveimport_inspect("/nonexistent/in.zip", &info, err, sizeof(err)) != 0);

  /* ---- a good import: new slot, no database yet */
  reset();
  sp = good();
  make_zip(ZIP, &sp);
  saveimport_args_t a = args();
  memset(&pr, 0, sizeof(pr));
  int rc = saveimport_run(&a, hooks(&pr), &res, err, sizeof(err));
  if(rc) printf("  err: %s\n", err);
  CHECK(rc == 0 && res.files == 5 && !res.replaced && res.db_inserted == 1 && !strcmp(res.title_id, "PPSA01234") && !strcmp(res.slot, "SAVE0"));
  CHECK(exists(IMG) && fsize(IMG) == 48ll * 65536 && !strcmp(res.image, IMG));
  CHECK(exists(ICONP) && fsize(ICONP) == (long long)sizeof(ICON));
  CHECK(scalar(DBP, "select count(*) from savedata") == 1 && scalar(DBP, "select user_id from savedata") == UIDN && scalar(DBP, "select account_id from savedata") == (long long)ACC);
  CHECK(scalar(DBP, "select blocks from savedata") == 48 && scalar(DBP, "select size_kib from savedata") == 3072 && scalar(DBP, "select user_param from savedata") == 5);
  CHECK(scalar(DBP, "select free_blocks from savedata") >= 0 && scalar(DBP, "select free_blocks from savedata") <= 48);
  CHECK(count_dir(WORK) == 0);                                    /* the work folder is clean again */
  CHECK(!exists(JOURNAL));                                        /* and no record of an exchange is left: it ended */
  CHECK(ps5tm_savemnt_test_opened == 2 && ps5tm_savemnt_test_closed == 2);   /* once to write, once to check */
  CHECK(pr.no_return_calls == 1 && pr.bytes > 100000);
  CHECK(!strcmp(pr.names[0], "ZIP-Datei prüfen") && !strcmp(pr.names[1], "Besitzer prüfen") && !strcmp(pr.names[2], "Abbild anlegen"));
  {
    uint8_t buf[200000];
    char p[700];
    snprintf(p, sizeof(p), "%s/data.bin", STORED);
    CHECK(slurp(p, buf, sizeof(buf)) == sizeof(DATA) && !memcmp(buf, DATA, sizeof(DATA)));
    snprintf(p, sizeof(p), "%s/sub/dir/a.txt", STORED);
    CHECK(slurp(p, buf, sizeof(buf)) == 11 && !memcmp(buf, "hello save\n", 11));
    snprintf(p, sizeof(p), "%s/emptydir", STORED);
    CHECK(exists(p));
    struct stat st;
    snprintf(p, sizeof(p), "%s/sce_sys/keystone", STORED);
    CHECK(stat(p, &st) == 0 && (st.st_mode & 07777) == 0640);
    snprintf(p, sizeof(p), "%s/sub/dir/a.txt", STORED);
    CHECK(stat(p, &st) == 0 && (st.st_mode & 07777) == 0600);
    snprintf(p, sizeof(p), "%s/sub", STORED);
    CHECK(stat(p, &st) == 0 && (st.st_mode & 07777) == 0750);
    /* the owner in the param.sfo: the account and the user of THIS console */
    snprintf(p, sizeof(p), "%s/sce_sys/param.sfo", STORED);
    size_t n = slurp(p, buf, sizeof(buf));
    uint8_t raw[8];
    uint8_t params[1024];
    CHECK(n > 100 && savesfo_valid(buf, n) && savesfo_get_raw(buf, n, "ACCOUNT_ID", raw, 8) == 8 && !memcmp(raw, (uint8_t[]){0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11}, 8));
    CHECK(savesfo_get_raw(buf, n, "PARAMS", params, sizeof(params)) == 1024 && params[4] == 0x31 && params[5] == 0x15 && params[6] == 0x16 && params[7] == 0x18);
    CHECK(params[8] == 24 && params[39] == 117);                    /* the rest of PARAMS is as it was */
  }

  /* ---- replacing a slot that is there: the old image and picture are kept until the end, the row is updated */
  reset();
  make_zip(ZIP, &sp);
  wfile(IMG, "OLD IMAGE", 9);
  wfile(ICONP, "OLD ICON", 8);
  make_db();
  a = args();
  memset(&pr, 0, sizeof(pr));
  rc = saveimport_run(&a, hooks(&pr), &res, err, sizeof(err));
  if(rc) printf("  err: %s\n", err);
  CHECK(rc == 0 && res.replaced == 1 && res.db_inserted == 0);
  CHECK(fsize(IMG) == 48ll * 65536 && fsize(ICONP) == (long long)sizeof(ICON));
  {
    char p[700];
    snprintf(p, sizeof(p), "%s.ps5cc-alt", IMG); CHECK(!exists(p));
    snprintf(p, sizeof(p), "%s.ps5cc-neu", IMG); CHECK(!exists(p));
    snprintf(p, sizeof(p), "%s.ps5cc-alt", ICONP); CHECK(!exists(p));
  }
  CHECK(scalar(DBP, "select count(*) from savedata") == 2 && scalar(DBP, "select id from savedata where dir_name='SAVE0'") == 7);
  CHECK(scalar(DBP, "select user_id from savedata where id=7") == UIDN && scalar(DBP, "select blocks from savedata where id=7") == 48);
  CHECK(scalar(DBP, "select user_id from savedata where id=9") == 1);                              /* the other title's row is as it was */
  CHECK(res.db_backup[0] && exists(res.db_backup) && scalar(res.db_backup, "select blocks from savedata where id=7") == 10);   /* the copy holds the state before */
  CHECK(count_dir(DBBK) == 1);

  /* ---- the database reads but cannot take the row (another shape): everything goes back as it was */
  reset();
  make_zip(ZIP, &sp);
  wfile(IMG, "OLD IMAGE", 9);
  wfile(ICONP, "OLD ICON", 8);
  {
    sqlite3 *db = NULL;
    sqlite3_open(DBP, &db);
    sqlite3_exec(db, "CREATE TABLE savedata (title_id, dir_name); INSERT INTO savedata VALUES ('PPSA00001','X');", NULL, NULL, NULL);   /* readable for the check, too short for a row */
    sqlite3_close(db);
  }
  long long dbsize = fsize(DBP);
  a = args();
  memset(&pr, 0, sizeof(pr));
  rc = saveimport_run(&a, hooks(&pr), &res, err, sizeof(err));
  CHECK(rc != 0 && rc != ECANCELED && strstr(err, "wieder so wie vorher"));
  CHECK(fsize(IMG) == 9 && fsize(ICONP) == 8 && fsize(DBP) == dbsize);
  {
    uint8_t b2[16];
    CHECK(slurp(IMG, b2, sizeof(b2)) == 9 && !memcmp(b2, "OLD IMAGE", 9));
    char p[700];
    snprintf(p, sizeof(p), "%s.ps5cc-alt", IMG); CHECK(!exists(p));
    snprintf(p, sizeof(p), "%s.ps5cc-neu", IMG); CHECK(!exists(p));
  }
  CHECK(count_dir(WORK) == 0);
  /* ... and with nothing there before: the image and the picture are removed again */
  reset();
  make_zip(ZIP, &sp);
  {
    sqlite3 *db = NULL;
    sqlite3_open(DBP, &db);
    sqlite3_exec(db, "CREATE TABLE savedata (title_id, dir_name);", NULL, NULL, NULL);
    sqlite3_close(db);
  }
  rc = saveimport_run(&a, hooks(&pr), &res, err, sizeof(err));
  CHECK(rc != 0 && !exists(IMG) && !exists(ICONP) && count_dir(WORK) == 0);

  /* ---- a stop at every point before the console changes: nothing on the console, nothing left in the work folder */
  for(unsigned stop = 1; stop <= 40; stop += 3) {
    reset();
    make_zip(ZIP, &sp);
    wfile(IMG, "OLD IMAGE", 9);
    make_db();
    a = args();
    memset(&pr, 0, sizeof(pr));
    pr.stop_after = stop;
    ps5tm_savemnt_test_opened = ps5tm_savemnt_test_closed = 0;
    rc = saveimport_run(&a, hooks(&pr), &res, err, sizeof(err));
    CHECK(rc == ECANCELED || rc == 0);
    if(rc == ECANCELED) {
      CHECK(fsize(IMG) == 9 && !exists(ICONP) && scalar(DBP, "select blocks from savedata where id=7") == 10 && count_dir(WORK) == 0);
      CHECK(ps5tm_savemnt_test_opened == ps5tm_savemnt_test_closed && pr.no_return_calls == 0 && count_dir(DBBK) == 0);
    }
  }

  /* ---- the console refuses: formatting, mounting, geometry */
  reset();
  make_zip(ZIP, &sp);
  a = args();
  memset(&pr, 0, sizeof(pr));
  ps5tm_savemnt_test_fail = 8;
  rc = saveimport_run(&a, hooks(&pr), &res, err, sizeof(err));
  CHECK(rc != 0 && strstr(err, "nicht anlegen") && !exists(IMG) && count_dir(WORK) == 0);
  ps5tm_savemnt_test_fail = 1;
  rc = saveimport_run(&a, hooks(&pr), &res, err, sizeof(err));
  CHECK(rc != 0 && strstr(err, "nicht eingehängt") && !exists(IMG) && count_dir(WORK) == 0);
  ps5tm_savemnt_test_geom_bad = 1;
  rc = saveimport_run(&a, hooks(&pr), &res, err, sizeof(err));
  CHECK(rc != 0 && strstr(err, "nicht den Aufbau") && !exists(IMG) && count_dir(WORK) == 0);

  /* ---- a mount that will not let go: the image stays (still mounted), the console is not changed, the next run recovers */
  reset();
  make_zip(ZIP, &sp);
  a = args();
  ps5tm_savemnt_test_fail = 2;
  rc = saveimport_run(&a, hooks(&pr), &res, err, sizeof(err));
  CHECK(rc != 0 && strstr(err, "neu starten") && !exists(IMG));
  {
    char p[700];
    snprintf(p, sizeof(p), "%s/sdimg_SAVE0", WORK);
    CHECK(exists(p));
  }
  memset(&pr, 0, sizeof(pr));
  rc = saveimport_run(&a, hooks(&pr), &res, err, sizeof(err));
  CHECK(rc == 0 && exists(IMG) && count_dir(WORK) == 0);

  /* ---- the read-back finds a file that is not what was written: nothing is installed */
  reset();
  make_zip(ZIP, &sp);
  a = args();
  ps5tm_savemnt_test_on_open = NULL;
  {
    extern void corrupt_on_second(const char *mount_dir, int nth);
    ps5tm_savemnt_test_on_open = corrupt_on_second;
  }
  ps5tm_savemnt_test_opened = ps5tm_savemnt_test_closed = 0;
  memset(&pr, 0, sizeof(pr));
  rc = saveimport_run(&a, hooks(&pr), &res, err, sizeof(err));
  CHECK(rc != 0 && strstr(err, "weicht") && !exists(IMG) && !exists(ICONP) && !exists(DBP) && pr.no_return_calls == 0 && count_dir(WORK) == 0);
  CHECK(ps5tm_savemnt_test_opened == ps5tm_savemnt_test_closed);
  ps5tm_savemnt_test_on_open = NULL;

  /* ---- a ZIP whose data is damaged: the checksum catches it while writing */
  reset();
  sp = good(); sp.corrupt_data = 1;
  make_zip(ZIP, &sp);
  a = args();
  memset(&pr, 0, sizeof(pr));
  rc = saveimport_run(&a, hooks(&pr), &res, err, sizeof(err));
  CHECK(rc != 0 && strstr(err, "beschädigt") && !exists(IMG) && count_dir(WORK) == 0);

  /* ---- not importable: refused with the reason, nothing done */
  reset();
  sp = good(); sp.mode_raw = 5;
  make_zip(ZIP, &sp);
  a = args();
  ps5tm_savemnt_test_opened = 0;
  rc = saveimport_run(&a, hooks(&pr), &res, err, sizeof(err));
  CHECK(rc != 0 && strstr(err, "Abbildformat") && ps5tm_savemnt_test_opened == 0 && !exists(IMG) && count_dir(WORK) == 0);

  /* ---- a cut-off earlier installation: the old image goes back first */
  reset();
  sp = good();
  make_zip(ZIP, &sp);
  {
    char alt[700];
    snprintf(alt, sizeof(alt), "%s.ps5cc-alt", IMG);
    wfile(alt, "OLD IMAGE", 9);                                   /* a run died after it put the old image aside */
    wfile(IMG, "HALF NEW", 8);
  }
  a = args();
  rc = saveimport_run(&a, hooks(&pr), &res, err, sizeof(err));
  CHECK(rc == 0 && res.replaced == 1 && fsize(IMG) == 48ll * 65536);
  {
    char alt[700];
    snprintf(alt, sizeof(alt), "%s.ps5cc-alt", IMG);
    CHECK(!exists(alt));
  }

  /* ---- a nested ZIP (the save's files below a folder) goes in as it is, without the folder */
  reset();
  sp = good(); sp.prefix = "PPSA01234/SAVE0/";
  make_zip(ZIP, &sp);
  a = args();
  rc = saveimport_run(&a, hooks(&pr), &res, err, sizeof(err));
  CHECK(rc == 0);
  {
    char p[700];
    snprintf(p, sizeof(p), "%s/data.bin", STORED);
    CHECK(exists(p));
    snprintf(p, sizeof(p), "%s/PPSA01234", STORED);
    CHECK(!exists(p));
  }

  /* ---- the user has no folder for the save database: refused before anything of the console is touched, no folders made */
  reset();
  {
    char p[700];
    snprintf(p, sizeof(p), "%s/savedata_prospero", SYS);
    rmtree(p);
    make_zip(ZIP, &sp);
    a = args();
    rc = saveimport_run(&a, hooks(&pr), &res, err, sizeof(err));
    CHECK(rc != 0 && strstr(err, "keinen Ordner für die Spielstand-Datenbank") && !exists(IMG) && !exists(ICONP) && !exists(p) && count_dir(WORK) == 0 && count_dir(DBBK) == 0);
    mkdirs(p);
  }
  /* ... and a database that does not open: also before */
  reset();
  wfile(DBP, "this is not a database, just text of some length so that it is not tiny", 70);
  make_zip(ZIP, &sp);
  a = args();
  rc = saveimport_run(&a, hooks(&pr), &res, err, sizeof(err));
  CHECK(rc != 0 && strstr(err, "nicht lesen") && !exists(IMG) && !exists(ICONP) && count_dir(WORK) == 0);

  /* ================================================================ PS4 */
  /* ---- inspect: no sidecar, recognised by the title */
  reset4();
  CHECK(make_ps4_zip(ZIP, "CUSA12345", "SLOT0") == 0);
  CHECK(saveimport_inspect(ZIP, &info, err, sizeof(err)) == 0 && info.importable && info.ps4 == 1);
  CHECK(!strcmp(info.title_id, "CUSA12345") && !strcmp(info.slot, "SLOT0") && info.files == 4 && info.block_size == 32768);
  CHECK(info.image_size == 32ull << 20 && info.block_count == 1024);                                    /* small save: the 32 MB minimum, in 32 KiB steps */
  {
    int rc2 = make_ps4_zip(ZIP, "CUSA12345", "bad slot");
    CHECK(rc2 == 0 && saveimport_inspect(ZIP, &info, err, sizeof(err)) == 0 && !info.importable && strstr(info.why, "ungültig"));
    make_ps4_zip(ZIP, "PPSA12345", "SLOT0");                                                            /* a PS5 title without sidecar: still refused for the missing chives.json */
    CHECK(saveimport_inspect(ZIP, &info, err, sizeof(err)) == 0 && !info.importable && strstr(info.why, "fehlt chives.json"));
  }

  /* ---- a good PS4 import, a donor of another title (rank 2): the user id and the shared value come from it */
  reset4();
  make_ps4_zip(ZIP, "CUSA12345", "SLOT0");
  make_donor("CUSA00099", "DON", ACC, UIDN);
  a = args();
  memset(&pr, 0, sizeof(pr));
  rc = saveimport_run(&a, hooks(&pr), &res, err, sizeof(err));
  if(rc) printf("  err: %s\n", err);
  CHECK(rc == 0 && res.files == 4 && !res.replaced && res.db_inserted == 1 && !strcmp(res.title_id, "CUSA12345"));
  CHECK(exists(IMG4) && exists(BIN4) && fsize(BIN4) == 96 && exists(ICON4) && fsize(ICON4) == (long long)sizeof(ICON));
  CHECK(!exists(IMG) && !exists(DBP));                                                                   /* nothing in the PS5 places */
  {
    uint8_t k[96];
    CHECK(slurp(BIN4, k, sizeof(k)) == 96 && k[0] == 0xA0 && k[95] == (uint8_t)(0xA0 + 95));        /* the key the formatter made */
    char p[700];
    snprintf(p, sizeof(p), "%s.ps5cc-neu", BIN4); CHECK(!exists(p));
    snprintf(p, sizeof(p), "%s.ps5cc-alt", BIN4); CHECK(!exists(p));
  }
  CHECK(scalar(DBP4, "select count(*) from savedata") == 1 && scalar(DBP4, "select blocks from savedata") == 1024 && scalar(DBP4, "select size_kib from savedata") == 32768);
  CHECK(scalar(DBP4, "select user_id from savedata") == UIDN && scalar(DBP4, "select account_id from savedata") == (long long)ACC);
  CHECK(count_dir(WORK) == 0);
  CHECK(strstr(res.note, "anderen Spielstand"));
  {
    char p[700];
    uint8_t buf[200000];
    struct stat st;
    snprintf(p, sizeof(p), "%s/sdimg_SLOT0/data.bin", STORE);
    CHECK(stat(p, &st) == 0 && (st.st_mode & 07777) == 0640 && slurp(p, buf, sizeof(buf)) == sizeof(DATA) && !memcmp(buf, DATA, sizeof(DATA)));
    snprintf(p, sizeof(p), "%s/sdimg_SLOT0/sub/a.txt", STORE);
    CHECK(stat(p, &st) == 0 && (st.st_mode & 07777) == 0600);                                          /* the modes of the ZIP entries */
    snprintf(p, sizeof(p), "%s/sdimg_SLOT0/sce_sys/param.sfo", STORE);
    size_t n = slurp(p, buf, sizeof(buf));
    uint8_t raw[8], params[1024];
    CHECK(n > 100 && savesfo_get_raw(buf, n, "ACCOUNT_ID", raw, 8) == 8 && !memcmp(raw, (uint8_t[]){0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11}, 8));
    CHECK(savesfo_get_raw(buf, n, "PARAMS", params, sizeof(params)) == 1024 && params[4] == 0x31 && params[7] == 0x18);
    /* bytes 8..40 are the donor's (sfo_build writes i*3, the same as the incoming one: told apart below with a donor that differs) */
    CHECK(params[8] == 24 && params[39] == 117);
  }

  /* ---- the donor is a save of the same title and slot (rank 0): the whole record; the old save is replaced and the key file with it */
  reset4();
  make_ps4_zip(ZIP, "CUSA12345", "SLOT0");
  make_db4();
  make_donor("CUSA12345", "SLOT0", ACC, UIDN);                                                           /* the image the import replaces is its own donor (its key file is the old one) */
  wfile(ICON4, "OLD ICON", 8);
  a = args();
  memset(&pr, 0, sizeof(pr));
  rc = saveimport_run(&a, hooks(&pr), &res, err, sizeof(err));
  if(rc) printf("  err: %s\n", err);
  if(!(rc == 0 && res.replaced == 1 && res.db_inserted == 0)) printf("  rc=%d err=%s replaced=%d ins=%d note=%s\n", rc, err, res.replaced, res.db_inserted, res.note);
  CHECK(rc == 0 && res.replaced == 1 && res.db_inserted == 0 && !strstr(res.note, "anderen Spielstand") && !strstr(res.note, "kein Spielstand"));
  {
    uint8_t k[96];
    CHECK(fsize(BIN4) == 96 && slurp(BIN4, k, sizeof(k)) == 96 && k[0] == 0xA0 && fsize(ICON4) == (long long)sizeof(ICON));    /* the new key, not the donor's old one (0x30) */
    char p[700];
    snprintf(p, sizeof(p), "%s.ps5cc-alt", BIN4); CHECK(!exists(p));
    snprintf(p, sizeof(p), "%s.ps5cc-alt", IMG4); CHECK(!exists(p));
  }
  CHECK(scalar(DBP4, "select count(*) from savedata") == 1 && scalar(DBP4, "select blocks from savedata where id=3") == 1024 && scalar(DBP4, "select user_id from savedata where id=3") == UIDN);

  /* ---- no save of this user to take the owner from: the import goes on and says so */
  reset4();
  make_ps4_zip(ZIP, "CUSA12345", "SLOT0");
  a = args();
  rc = saveimport_run(&a, hooks(&pr), &res, err, sizeof(err));
  CHECK(rc == 0 && strstr(res.note, "kein Spielstand zu finden") && exists(IMG4) && exists(BIN4));
  /* ... a donor of another account is not taken */
  reset4();
  make_ps4_zip(ZIP, "CUSA12345", "SLOT0");
  make_donor("CUSA00099", "DON", 0x1234, UIDN);
  rc = saveimport_run(&a, hooks(&pr), &res, err, sizeof(err));
  CHECK(rc == 0 && strstr(res.note, "kein Spielstand zu finden"));
  /* ... nor one of another user id */
  reset4();
  make_ps4_zip(ZIP, "CUSA12345", "SLOT0");
  make_donor("CUSA00099", "DON", ACC, 0x42424242);
  rc = saveimport_run(&a, hooks(&pr), &res, err, sizeof(err));
  CHECK(rc == 0 && strstr(res.note, "kein Spielstand zu finden") && count_dir(WORK) == 0);

  /* ---- the console refuses the new image, the database cannot take the row, a stop: nothing of the console changes */
  reset4();
  make_ps4_zip(ZIP, "CUSA12345", "SLOT0");
  a = args();
  ps5tm_savemnt_test_fail = 8;
  rc = saveimport_run(&a, hooks(&pr), &res, err, sizeof(err));
  CHECK(rc != 0 && strstr(err, "nicht anlegen") && !exists(IMG4) && !exists(BIN4) && count_dir(WORK) == 0);
  reset4();
  make_ps4_zip(ZIP, "CUSA12345", "SLOT0");
  wfile(IMG4, "OLD IMAGE", 9);
  wfile(BIN4, "OLD KEY FILE", 12);
  {
    sqlite3 *db = NULL;
    sqlite3_open(DBP4, &db);
    sqlite3_exec(db, "CREATE TABLE savedata (title_id, dir_name);", NULL, NULL, NULL);
    sqlite3_close(db);
  }
  rc = saveimport_run(&a, hooks(&pr), &res, err, sizeof(err));
  {
    uint8_t b2[16];
    CHECK(rc != 0 && strstr(err, "wieder so wie vorher") && fsize(IMG4) == 9 && slurp(BIN4, b2, sizeof(b2)) == 12 && !memcmp(b2, "OLD KEY FILE", 12) && !exists(ICON4));
    char p[700];
    snprintf(p, sizeof(p), "%s.ps5cc-alt", BIN4); CHECK(!exists(p));
    snprintf(p, sizeof(p), "%s.ps5cc-neu", BIN4); CHECK(!exists(p));
  }
  for(unsigned stop = 1; stop <= 30; stop += 4) {
    reset4();
    make_ps4_zip(ZIP, "CUSA12345", "SLOT0");
    make_donor("CUSA00099", "DON", ACC, UIDN);
    wfile(IMG4, "OLD IMAGE", 9);
    wfile(BIN4, "OLD KEY FILE", 12);
    make_db4();
    a = args();
    memset(&pr, 0, sizeof(pr));
    pr.stop_after = stop;
    ps5tm_savemnt_test_opened = ps5tm_savemnt_test_closed = 0;
    rc = saveimport_run(&a, hooks(&pr), &res, err, sizeof(err));
    CHECK(rc == ECANCELED || rc == 0);
    if(rc == ECANCELED) {
      CHECK(fsize(IMG4) == 9 && fsize(BIN4) == 12 && count_dir(WORK) == 0 && ps5tm_savemnt_test_opened == ps5tm_savemnt_test_closed && pr.no_return_calls == 0);
    }
  }

  /* ---- a donor that will not let go: nothing is changed, the message says restart */
  reset4();
  make_ps4_zip(ZIP, "CUSA12345", "SLOT0");
  make_donor("CUSA00099", "DON", ACC, UIDN);
  ps5tm_savemnt_test_fail = 2;
  rc = saveimport_run(&a, hooks(&pr), &res, err, sizeof(err));
  CHECK(rc != 0 && strstr(err, "neu starten") && !exists(IMG4) && !exists(BIN4));

  /* ---- the entries the other tool writes beside the files are never written into the new image */
  reset();
  sp = good(); sp.foreign = 1;
  make_zip(ZIP, &sp);
  CHECK(saveimport_inspect(ZIP, &info, err, sizeof(err)) == 0 && info.importable && info.files == 5);
  a = args();
  rc = saveimport_run(&a, hooks(&pr), &res, err, sizeof(err));
  CHECK(rc == 0 && res.files == 5);
  {
    char p[700];
    snprintf(p, sizeof(p), "%s/PS5KEY", STORED); CHECK(!exists(p));
    snprintf(p, sizeof(p), "%s/PS5IMAGE", STORED); CHECK(!exists(p));
    snprintf(p, sizeof(p), "%s/PS5PARAM_SIZE", STORED); CHECK(!exists(p));
    snprintf(p, sizeof(p), "%s/data.bin", STORED); CHECK(exists(p));
  }

  /* ---- an image larger than any save can be: the sidecar is refused, nothing is made */
  reset();
  sp = good(); sp.blocks = 4000000;
  make_zip(ZIP, &sp);
  a = args();
  ps5tm_savemnt_test_opened = 0;
  rc = saveimport_run(&a, hooks(&pr), &res, err, sizeof(err));
  CHECK(rc != 0 && strstr(err, "keine gültige Beschreibung") && ps5tm_savemnt_test_opened == 0 && count_dir(WORK) == 0);

  /* ---- the copy-on-write layout (fs_ext != 0): 96 blocks of 64 KiB, 48 of them system blocks, the param.sfo counts 48 data + 48 system. It is made as the classic image of 48 blocks. */
  sp = good(); sp.blocks = 96; sp.fs_ext = 48; sp.sfo_blocks = 48; sp.sysblocks = 48;
  make_zip(ZIP, &sp);
  CHECK(saveimport_inspect(ZIP, &info, err, sizeof(err)) == 0 && info.importable && info.converted == 1 && info.src_system_blocks == 48);
  CHECK(info.block_count == 48 && info.image_size == 48ull * 65536 && info.block_size == 65536 && !info.ps4);
  reset();
  make_zip(ZIP, &sp);
  a = args();
  memset(&pr, 0, sizeof(pr));
  ps5tm_savemnt_test_opened = ps5tm_savemnt_test_closed = 0;
  rc = saveimport_run(&a, hooks(&pr), &res, err, sizeof(err));
  if(rc) printf("  err: %s\n", err);
  CHECK(rc == 0 && res.files == 5 && res.db_inserted == 1 && strstr(res.note, "neueren Abbildformat") && strstr(res.note, "48 Systemblöcken") && strstr(res.note, "48 Blöcken"));
  CHECK(exists(IMG) && fsize(IMG) == 48ll * 65536);                    /* the classic image of the data blocks, not 96 blocks */
  CHECK(scalar(DBP, "select blocks from savedata") == 48 && scalar(DBP, "select system_blocks from savedata") == 0 && scalar(DBP, "select size_kib from savedata") == 3072);
  {
    uint8_t buf[200000];
    char p[700];
    snprintf(p, sizeof(p), "%s/sce_sys/param.sfo", STORED);
    const size_t n = slurp(p, buf, sizeof(buf));
    uint8_t b8[8], s8[8];
    CHECK(n > 100 && savesfo_valid(buf, n) && savesfo_get_raw(buf, n, "SAVEDATA_BLOCKS", b8, 8) == 8 && b8[0] == 48 && !b8[1] && !b8[7]);
    CHECK(savesfo_get_raw(buf, n, "SYSTEM_BLOCKS", s8, 8) == 8 && !s8[0] && !s8[1] && !s8[7]);     /* no system blocks in the classic image */
    snprintf(p, sizeof(p), "%s/data.bin", STORED);
    CHECK(slurp(p, buf, sizeof(buf)) == sizeof(DATA) && !memcmp(buf, DATA, sizeof(DATA)));
  }
  CHECK(ps5tm_savemnt_test_opened == 2 && ps5tm_savemnt_test_closed == 2 && count_dir(WORK) == 0);

  /* ... where the data blocks would not hold the files (4 blocks of 64 KiB for 2 blocks of files: the room the file system needs is 16 blocks more): the image gets the room, SAVEDATA_BLOCKS says so */
  reset();
  sp = good(); sp.blocks = 8; sp.fs_ext = 4; sp.sfo_blocks = 4; sp.sysblocks = 4;
  make_zip(ZIP, &sp);
  CHECK(saveimport_inspect(ZIP, &info, err, sizeof(err)) == 0 && info.importable && info.converted == 1 && info.block_count == 18 && info.image_size == 18ull * 65536);
  a = args();
  memset(&pr, 0, sizeof(pr));
  rc = saveimport_run(&a, hooks(&pr), &res, err, sizeof(err));
  if(rc) printf("  err: %s\n", err);
  CHECK(rc == 0 && exists(IMG) && fsize(IMG) == 18ll * 65536 && scalar(DBP, "select blocks from savedata") == 18 && scalar(DBP, "select system_blocks from savedata") == 0);
  {
    uint8_t buf[8192];
    char p[700];
    snprintf(p, sizeof(p), "%s/sce_sys/param.sfo", STORED);
    const size_t n = slurp(p, buf, sizeof(buf));
    uint8_t b8[8];
    CHECK(n > 100 && savesfo_get_raw(buf, n, "SAVEDATA_BLOCKS", b8, 8) == 8 && b8[0] == 18 && !b8[1]);
  }

  /* a classic save says nothing of a conversion */
  reset();
  sp = good();
  make_zip(ZIP, &sp);
  CHECK(saveimport_inspect(ZIP, &info, err, sizeof(err)) == 0 && info.importable && info.converted == 0);
  a = args();
  rc = saveimport_run(&a, hooks(&pr), &res, err, sizeof(err));
  CHECK(rc == 0 && !strstr(res.note, "Abbildformat"));

  /* ---- the owner of a PS5 save is made right from a save of the same user as well: bytes 4..40 of a donor (another title: rank 2) */
  reset();
  g_marker = 0xD0;
  sp = good();
  make_zip(ZIP, &sp);
  make_donor5("PPSA00099", "DON", ACC, UIDN);
  a = args();
  memset(&pr, 0, sizeof(pr));
  ps5tm_savemnt_test_opened = ps5tm_savemnt_test_closed = 0;
  rc = saveimport_run(&a, hooks(&pr), &res, err, sizeof(err));
  if(rc) printf("  err: %s\n", err);
  CHECK(rc == 0 && strstr(res.note, "anderen Spielstand") && ps5tm_savemnt_test_opened == 3 && ps5tm_savemnt_test_closed == 3 && count_dir(WORK) == 0);
  {
    uint8_t pa[1024];
    CHECK(stored_params("SAVE0", pa) == 0 && pa[4] == 0x31 && pa[7] == 0x18);                       /* this console's user */
    CHECK(pa[8] == 0xD0 && pa[39] == 0xD0 + 31 && pa[100] == 0);                                    /* the donor's value; the rest of the record is the incoming one's */
  }
  CHECK(exists(IMG) && fsize(IMG) == 48ll * 65536);

  /* ... the save it replaces is the donor (rank 0): the whole record */
  reset();
  g_marker = 0xE0;
  make_zip(ZIP, &sp);
  make_donor5("PPSA01234", "SAVE0", ACC, UIDN);
  wfile(IMG, "OLD IMAGE", 9);
  memset(&pr, 0, sizeof(pr));
  rc = saveimport_run(&a, hooks(&pr), &res, err, sizeof(err));
  if(rc) printf("  err: %s\n", err);
  CHECK(rc == 0 && res.replaced == 1 && !strstr(res.note, "anderen Spielstand") && !strstr(res.note, "kein Spielstand") && count_dir(WORK) == 0);
  {
    uint8_t pa[1024];
    CHECK(stored_params("SAVE0", pa) == 0 && pa[4] == 0x31 && pa[8] == 0xE0 && pa[39] == 0xE0 + 31 && pa[100] == 0x77);
  }
  CHECK(fsize(IMG) == 48ll * 65536);

  /* ... only a save of the other kind is there (a PS4 save of the user: rank 3), and it is taken */
  reset();
  g_marker = 0xC0;
  make_zip(ZIP, &sp);
  make_donor("CUSA00099", "DON4", ACC, UIDN);
  memset(&pr, 0, sizeof(pr));
  rc = saveimport_run(&a, hooks(&pr), &res, err, sizeof(err));
  if(rc) printf("  err: %s\n", err);
  CHECK(rc == 0 && strstr(res.note, "anderen Spielstand") && count_dir(WORK) == 0);
  {
    uint8_t pa[1024];
    CHECK(stored_params("SAVE0", pa) == 0 && pa[8] == 0xC0 && pa[39] == 0xC0 + 31 && pa[100] == 0);
  }

  /* ... a PS5 donor beats a PS4 one (the PS5 one is tried first) */
  reset();
  g_marker = 0xB0;
  make_zip(ZIP, &sp);
  make_donor("CUSA00099", "DON4", ACC, UIDN);
  g_marker = 0xA8;
  make_donor5("PPSA00099", "DON5", ACC, UIDN);
  rc = saveimport_run(&a, hooks(&pr), &res, err, sizeof(err));
  {
    uint8_t pa[1024];
    CHECK(rc == 0 && stored_params("SAVE0", pa) == 0 && pa[8] == 0xA8);
  }

  /* ... no save of the user, or only ones of another account or user id: the import goes on and says so (the incoming value stays) */
  g_marker = 0;
  reset();
  make_zip(ZIP, &sp);
  rc = saveimport_run(&a, hooks(&pr), &res, err, sizeof(err));
  {
    uint8_t pa[1024];
    CHECK(rc == 0 && strstr(res.note, "kein Spielstand zu finden") && stored_params("SAVE0", pa) == 0 && pa[4] == 0x31 && pa[8] == 24 && pa[39] == 117);
  }
  reset();
  g_marker = 0xD0;
  make_zip(ZIP, &sp);
  make_donor5("PPSA00099", "DON", 0x1234, UIDN);
  rc = saveimport_run(&a, hooks(&pr), &res, err, sizeof(err));
  CHECK(rc == 0 && strstr(res.note, "kein Spielstand zu finden") && count_dir(WORK) == 0);
  reset();
  make_zip(ZIP, &sp);
  make_donor5("PPSA00099", "DON", ACC, 0x42424242);
  rc = saveimport_run(&a, hooks(&pr), &res, err, sizeof(err));
  CHECK(rc == 0 && strstr(res.note, "kein Spielstand zu finden") && count_dir(WORK) == 0);
  g_marker = 0;

  /* ... a donor that will not let go: nothing is changed, the message says restart */
  reset();
  g_marker = 0xD0;
  make_zip(ZIP, &sp);
  make_donor5("PPSA00099", "DON", ACC, UIDN);
  ps5tm_savemnt_test_fail = 2;                                    /* the unmount of the donor fails */
  rc = saveimport_run(&a, hooks(&pr), &res, err, sizeof(err));
  CHECK(rc != 0 && strstr(err, "neu starten") && !exists(IMG));
  g_marker = 0;

  /* ================================================================ the audit of 19.11.2026 */
  /* ---- a game started while the image was written: asked before the point of no return, nothing is changed */
  reset();
  sp = good();
  make_zip(ZIP, &sp);
  make_db();
  wfile(IMG, "OLD IMAGE", 9);
  wfile(ICONP, "OLD ICON", 8);
  {
    const long long dbsz = fsize(DBP);
    a = args();
    memset(&pr, 0, sizeof(pr));
    saveimport_hooks_t *hk = hooks(&pr);
    hk->may_commit = p_nocommit;
    ps5tm_savemnt_test_opened = ps5tm_savemnt_test_closed = 0;
    rc = saveimport_run(&a, hk, &res, err, sizeof(err));
    CHECK(rc != 0 && rc != ECANCELED && strstr(err, "Spiel gestartet") && strstr(err, "nichts verändert"));
    CHECK(pr.no_return_calls == 0 && fsize(IMG) == 9 && fsize(ICONP) == 8 && fsize(DBP) == dbsz && count_dir(WORK) == 0 && count_dir(DBBK) == 0);
    CHECK(ps5tm_savemnt_test_opened == ps5tm_savemnt_test_closed);
    char p[700];
    snprintf(p, sizeof(p), "%s.ps5cc-alt", IMG); CHECK(!exists(p));
    snprintf(p, sizeof(p), "%s.ps5cc-neu", IMG); CHECK(!exists(p));
  }

  /* ---- an app's saves are never a donor: not mounted, not read (the owner note says no donor was found) */
  reset();
  g_marker = 0xD0;
  make_zip(ZIP, &sp);
  make_donor5("PPSA00099", "DON", ACC, UIDN);
  a = args();
  a.skip_title = skip_ppsa99;
  memset(&pr, 0, sizeof(pr));
  ps5tm_savemnt_test_opened = ps5tm_savemnt_test_closed = 0;
  rc = saveimport_run(&a, hooks(&pr), &res, err, sizeof(err));
  CHECK(rc == 0 && strstr(res.note, "kein Spielstand zu finden") && ps5tm_savemnt_test_opened == 2 && ps5tm_savemnt_test_closed == 2);
  /* ... and the same donor, not skipped, is the one taken (the control of the test above) */
  reset();
  make_zip(ZIP, &sp);
  make_donor5("PPSA00099", "DON", ACC, UIDN);
  a = args();
  memset(&pr, 0, sizeof(pr));
  ps5tm_savemnt_test_opened = ps5tm_savemnt_test_closed = 0;
  rc = saveimport_run(&a, hooks(&pr), &res, err, sizeof(err));
  CHECK(rc == 0 && strstr(res.note, "anderen Spielstand") && ps5tm_savemnt_test_opened == 3);
  g_marker = 0;

  /* ---- the database is locked by another writer: SQLite changes nothing, so the file is NOT replaced by a copy (its inode stays), the images go back, the message is true */
  reset();
  make_zip(ZIP, &sp);
  make_db();
  wfile(IMG, "OLD IMAGE", 9);
  wfile(ICONP, "OLD ICON", 8);
  {
    struct stat before, after;
    CHECK(stat(DBP, &before) == 0);
    sqlite3 *other = NULL;
    CHECK(sqlite3_open(DBP, &other) == SQLITE_OK && sqlite3_exec(other, "BEGIN IMMEDIATE", NULL, NULL, NULL) == SQLITE_OK);   /* readers still read, a second writer waits */
    a = args();
    memset(&pr, 0, sizeof(pr));
    rc = saveimport_run(&a, hooks(&pr), &res, err, sizeof(err));
    sqlite3_exec(other, "ROLLBACK", NULL, NULL, NULL);
    sqlite3_close(other);
    CHECK(rc != 0 && rc != ECANCELED && strstr(err, "gesperrt") && strstr(err, "wieder so wie vorher"));
    CHECK(stat(DBP, &after) == 0 && after.st_ino == before.st_ino && after.st_size == before.st_size);
    uint8_t b2[16];
    CHECK(slurp(IMG, b2, sizeof(b2)) == 9 && !memcmp(b2, "OLD IMAGE", 9) && fsize(ICONP) == 8);
    CHECK(scalar(DBP, "select blocks from savedata where id=7") == 10 && count_dir(WORK) == 0);
    char p[700];
    snprintf(p, sizeof(p), "%s.ps5cc-alt", IMG); CHECK(!exists(p));
    snprintf(p, sizeof(p), "%s.ps5cc-neu", IMG); CHECK(!exists(p));
  }

  /* ---- two puts-in in the same second get two copies of the database under two names (the slot is in the name; a name that is taken gets a count) */
  reset();
  make_zip(ZIP, &sp);
  make_db();
  a = args();
  rc = saveimport_run(&a, hooks(&pr), &res, err, sizeof(err));
  CHECK(rc == 0 && strstr(res.db_backup, "SAVE0"));
  {
    char first[400];
    snprintf(first, sizeof(first), "%s", res.db_backup);
    rc = saveimport_run(&a, hooks(&pr), &res, err, sizeof(err));
    CHECK(rc == 0 && strcmp(first, res.db_backup) != 0 && count_dir(DBBK) == 2 && exists(first) && exists(res.db_backup));
  }

  /* ---- a cut-off earlier run left only the old image (".ps5cc-alt", nothing in its place): it is the save and comes back when this run fails, too */
  reset();
  make_zip(ZIP, &sp);
  {
    sqlite3 *db = NULL;
    sqlite3_open(DBP, &db);
    sqlite3_exec(db, "CREATE TABLE savedata (title_id, dir_name);", NULL, NULL, NULL);
    sqlite3_close(db);
  }
  {
    char alt[700];
    snprintf(alt, sizeof(alt), "%s.ps5cc-alt", IMG);
    wfile(alt, "OLD IMAGE", 9);
    a = args();
    rc = saveimport_run(&a, hooks(&pr), &res, err, sizeof(err));
    uint8_t b2[16];
    CHECK(rc != 0 && slurp(IMG, b2, sizeof(b2)) == 9 && !memcmp(b2, "OLD IMAGE", 9) && !exists(alt));
  }
  /* ... where both are there, the old one is a leftover and the file in place is the one that is replaced (and kept for the way back) */
  reset();
  make_zip(ZIP, &sp);
  {
    char alt[700];
    snprintf(alt, sizeof(alt), "%s.ps5cc-alt", IMG);
    wfile(alt, "STALE OLD", 9);
    wfile(IMG, "CURRENT IMG", 11);
    sqlite3 *db = NULL;
    sqlite3_open(DBP, &db);
    sqlite3_exec(db, "CREATE TABLE savedata (title_id, dir_name);", NULL, NULL, NULL);
    sqlite3_close(db);
    a = args();
    rc = saveimport_run(&a, hooks(&pr), &res, err, sizeof(err));
    uint8_t b2[16];
    CHECK(rc != 0 && slurp(IMG, b2, sizeof(b2)) == 11 && !memcmp(b2, "CURRENT IMG", 11));                    /* the current one is what comes back */
  }

  /* ---- PS4: a run cut between the two renames (new image in place, new key file never arrived, the old key file aside): the old PAIR goes back when this run fails too */
  reset4();
  make_ps4_zip(ZIP, "CUSA12345", "SLOT0");
  {
    char alt[700], balt[700], bneu[700];
    snprintf(alt, sizeof(alt), "%s.ps5cc-alt", IMG4);
    snprintf(balt, sizeof(balt), "%s.ps5cc-alt", BIN4);
    snprintf(bneu, sizeof(bneu), "%s.ps5cc-neu", BIN4);
    wfile(IMG4, "NEW IMAGE", 9);
    wfile(alt, "OLD IMAGE", 9);
    wfile(balt, "OLD KEY FILE", 12);
    wfile(bneu, "NEW KEY FILE", 12);
    sqlite3 *db = NULL;
    sqlite3_open(DBP4, &db);
    sqlite3_exec(db, "CREATE TABLE savedata (title_id, dir_name);", NULL, NULL, NULL);
    sqlite3_close(db);
    a = args();
    rc = saveimport_run(&a, hooks(&pr), &res, err, sizeof(err));
    uint8_t b1[16], b2[16];
    CHECK(rc != 0 && slurp(IMG4, b1, sizeof(b1)) == 9 && !memcmp(b1, "OLD IMAGE", 9) && slurp(BIN4, b2, sizeof(b2)) == 12 && !memcmp(b2, "OLD KEY FILE", 12));
    CHECK(!exists(alt) && !exists(balt) && !exists(bneu));
  }

  /* ---- a slot that ends as the leftovers of a cut-off run do is no slot */
  reset();
  sp = good(); sp.slot = "SAVE0.ps5cc-alt";
  make_zip(ZIP, &sp);
  CHECK(saveimport_inspect(ZIP, &info, err, sizeof(err)) == 0 && !info.importable && strstr(info.why, "ungültig"));
  sp = good(); sp.slot = "SAVE0.ps5cc-neu";
  make_zip(ZIP, &sp);
  CHECK(saveimport_inspect(ZIP, &info, err, sizeof(err)) == 0 && !info.importable && strstr(info.why, "ungültig"));

  /* ---- folder entries in the ZIP (the save's folder, the ones on the way to it, sce_sys): they hold nothing and are no files outside of the save */
  reset();
  sp = good(); sp.dir_entries = 1;
  make_zip(ZIP, &sp);
  CHECK(saveimport_inspect(ZIP, &info, err, sizeof(err)) == 0 && info.importable && info.files == 5 && info.prefix[0] == 0);
  sp = good(); sp.dir_entries = 1; sp.prefix = "PPSA01234/SAVE0/";
  make_zip(ZIP, &sp);
  CHECK(saveimport_inspect(ZIP, &info, err, sizeof(err)) == 0 && info.importable && info.files == 5 && !strcmp(info.prefix, "PPSA01234/SAVE0/"));
  a = args();
  memset(&pr, 0, sizeof(pr));
  rc = saveimport_run(&a, hooks(&pr), &res, err, sizeof(err));
  CHECK(rc == 0 && res.files == 5 && exists(IMG));
  /* ... but a folder that is not on the way to the save is still outside of it */
  {
    unlink(ZIP);
    szw_t *w = szw_create(ZIP);
    uint8_t sfo[4096];
    CHECK(w != NULL);
    if(!w) return 1;
    szw_add_dir(w, "OTHER", 1790000000, 0755);
    const size_t sl = sfo_build(sfo, "PPSA01234", "SAVE0", 48, 0, "ppr", 0x99AABBCCDDEEFF00ull, 0x11111111);
    szw_add_mem(w, "PPSA01234/SAVE0/sce_sys/param.sfo", sfo, sl, 1790000000, 0644);
    szw_add_mem(w, "PPSA01234/SAVE0/data.bin", DATA, 100, 1790000000, 0644);
    chives_t g;
    chives_init(&g);
    g.version = 2; g.block_size = 65536; g.block_count = 48; g.image_size = 48ull * 65536; g.backup_header_count = 1; g.mode_raw = 0x0d; g.has_fs_ext = 1;
    snprintf(g.title_id, sizeof(g.title_id), "PPSA01234");
    snprintf(g.save_dir, sizeof(g.save_dir), "SAVE0");
    snprintf(g.image_name, sizeof(g.image_name), "sdimg_SAVE0");
    char json[4096];
    const int jn = chives_write(&g, json, sizeof(json));
    chives_free(&g);
    szw_add_mem(w, "PPSA01234/SAVE0/chives.json", json, (size_t)jn, 1790000000, 0644);
    CHECK(szw_finish(w) == SZ_OK);
    CHECK(saveimport_inspect(ZIP, &info, err, sizeof(err)) == 0 && !info.importable && strstr(info.why, "außerhalb"));
  }

  /* ================================================================ the record of an exchange: a run that is cut off is finished or taken back */
  sp = good();
  /* ---- phase "swap" (the images were being exchanged): the old image comes back, the new one and the leftovers go, the picture likewise */
  reset();
  make_db();
  {
    char alt[700], neu[700], ialt[700];
    snprintf(alt, sizeof(alt), "%s.ps5cc-alt", IMG);
    snprintf(neu, sizeof(neu), "%s.ps5cc-neu", IMG);
    snprintf(ialt, sizeof(ialt), "%s.ps5cc-alt", ICONP);
    wfile(IMG, "NEW IMAGE", 9);                                   /* the new one was already in place ... */
    wfile(alt, "OLD IMAGE", 9);                                   /* ... and the old one aside */
    wfile(neu, "LEFTOVER", 8);
    wfile(ICONP, "NEW ICON", 8);
    wfile(ialt, "OLD ICON", 8);
    write_journal("swap", "PPSA01234", "SAVE0", 1, 0, 0, 1, IMG, ICONP, DBP);
    char rep[500];
    const long long dbsz = fsize(DBP);
    CHECK(saveimport_recover(JOURNAL, rep, sizeof(rep)) == 1 && strstr(rep, "zurückgenommen") && strstr(rep, "PPSA01234"));
    uint8_t b2[16];
    CHECK(slurp(IMG, b2, sizeof(b2)) == 9 && !memcmp(b2, "OLD IMAGE", 9) && slurp(ICONP, b2, sizeof(b2)) == 8 && !memcmp(b2, "OLD ICON", 8));
    CHECK(!exists(alt) && !exists(neu) && !exists(ialt) && !exists(JOURNAL) && fsize(DBP) == dbsz);
    CHECK(saveimport_recover(JOURNAL, rep, sizeof(rep)) == 0);                              /* nothing is left to do */
  }
  /* ... nothing was there before: the new image and its picture go, and the cut-off before any rename leaves the old image where it is */
  reset();
  wfile(IMG, "NEW IMAGE", 9);
  wfile(ICONP, "NEW ICON", 8);
  write_journal("swap", "PPSA01234", "SAVE0", 0, 0, 0, 0, IMG, ICONP, DBP);
  CHECK(saveimport_recover(JOURNAL, err, sizeof(err)) == 1 && !exists(IMG) && !exists(ICONP) && !exists(JOURNAL));
  reset();
  wfile(IMG, "OLD IMAGE", 9);                                     /* the old image never went aside: it is still the one in place */
  write_journal("swap", "PPSA01234", "SAVE0", 1, 0, 0, 0, IMG, ICONP, DBP);
  CHECK(saveimport_recover(JOURNAL, err, sizeof(err)) == 1 && fsize(IMG) == 9 && !exists(JOURNAL));
  /* ... phase "rollback" (the run was taking the files back itself when it was cut off) is the same */
  reset();
  {
    char alt[700];
    snprintf(alt, sizeof(alt), "%s.ps5cc-alt", IMG);
    wfile(IMG, "NEW IMAGE", 9);
    wfile(alt, "OLD IMAGE", 9);
    write_journal("rollback", "PPSA01234", "SAVE0", 1, 0, 0, 0, IMG, ICONP, DBP);
    uint8_t b2[16];
    CHECK(saveimport_recover(JOURNAL, err, sizeof(err)) == 1 && slurp(IMG, b2, sizeof(b2)) == 9 && !memcmp(b2, "OLD IMAGE", 9) && !exists(alt));
  }

  /* ---- phase "db" (the images are in place, the row is what was left): the row is written, the old files go */
  reset();
  make_db();
  {
    char alt[700], ialt[700];
    snprintf(alt, sizeof(alt), "%s.ps5cc-alt", IMG);
    snprintf(ialt, sizeof(ialt), "%s.ps5cc-alt", ICONP);
    wfile(IMG, "NEW IMAGE", 9);
    wfile(alt, "OLD IMAGE", 9);
    wfile(ICONP, "NEW ICON", 8);
    wfile(ialt, "OLD ICON", 8);
    write_journal("db", "PPSA01234", "SAVE0", 1, 0, 0, 1, IMG, ICONP, DBP);
    char rep[500];
    CHECK(scalar(DBP, "select count(*) from savedata") == 2 && scalar(DBP, "select blocks from savedata where dir_name='SAVE0'") == 10);        /* the row of the old state */
    CHECK(saveimport_recover(JOURNAL, rep, sizeof(rep)) == 1 && strstr(rep, "zu Ende geführt"));
    CHECK(scalar(DBP, "select count(*) from savedata") == 2 && scalar(DBP, "select blocks from savedata where dir_name='SAVE0'") == 48 && scalar(DBP, "select user_id from savedata where dir_name='SAVE0'") == UIDN);
    CHECK(scalar(DBP, "select account_id from savedata where dir_name='SAVE0'") == (long long)ACC && scalar(DBP, "select size_kib from savedata where dir_name='SAVE0'") == 3072);
    CHECK(scalar(DBP, "select user_id from savedata where id=9") == 1);                    /* the other row is as it was */
    uint8_t b2[16];
    CHECK(slurp(IMG, b2, sizeof(b2)) == 9 && !memcmp(b2, "NEW IMAGE", 9) && !exists(alt) && !exists(ialt) && !exists(JOURNAL));         /* the new one stays */
    /* the same record again (a run cut off after the row was written): the row is written once more, nothing doubles */
    write_journal("db", "PPSA01234", "SAVE0", 1, 0, 0, 1, IMG, ICONP, DBP);
    CHECK(saveimport_recover(JOURNAL, rep, sizeof(rep)) == 1 && scalar(DBP, "select count(*) from savedata") == 2);
  }
  /* ... and where the row did not exist yet, it is made */
  reset();
  make_db();
  {
    sqlite3 *db = NULL;
    sqlite3_open(DBP, &db);
    sqlite3_exec(db, "DELETE FROM savedata", NULL, NULL, NULL);
    sqlite3_close(db);
    wfile(IMG, "NEW IMAGE", 9);
    write_journal("db", "PPSA01234", "SAVE0", 0, 0, 0, 0, IMG, ICONP, DBP);
    CHECK(saveimport_recover(JOURNAL, err, sizeof(err)) == 1 && scalar(DBP, "select count(*) from savedata") == 1 && scalar(DBP, "select blocks from savedata") == 48 && fsize(IMG) == 9);
  }
  /* ... the database is held by another writer: it is told, the record stays, the files stay, and the next try (the lock gone) finishes it */
  reset();
  make_db();
  {
    char alt[700];
    snprintf(alt, sizeof(alt), "%s.ps5cc-alt", IMG);
    wfile(IMG, "NEW IMAGE", 9);
    wfile(alt, "OLD IMAGE", 9);
    write_journal("db", "PPSA01234", "SAVE0", 1, 0, 0, 0, IMG, ICONP, DBP);
    sqlite3 *other = NULL;
    CHECK(sqlite3_open(DBP, &other) == SQLITE_OK && sqlite3_exec(other, "BEGIN IMMEDIATE", NULL, NULL, NULL) == SQLITE_OK);
    char rep[500];
    CHECK(saveimport_recover(JOURNAL, rep, sizeof(rep)) == -1 && strstr(rep, "fast zu Ende") && exists(JOURNAL) && exists(alt) && fsize(IMG) == 9);
    /* a new put-in does not start on top of it */
    make_zip(ZIP, &sp);
    a = args();
    memset(&pr, 0, sizeof(pr));
    rc = saveimport_run(&a, hooks(&pr), &res, err, sizeof(err));
    CHECK(rc != 0 && strstr(err, "noch etwas offen") && strstr(err, "nichts verändert") && pr.no_return_calls == 0 && exists(JOURNAL) && count_dir(WORK) == 0);
    sqlite3_exec(other, "ROLLBACK", NULL, NULL, NULL);
    sqlite3_close(other);
    CHECK(saveimport_recover(JOURNAL, rep, sizeof(rep)) == 1 && !exists(JOURNAL) && !exists(alt) && scalar(DBP, "select blocks from savedata where dir_name='SAVE0'") == 48);
  }

  /* ---- a put-in that finds the record of an earlier cut-off run takes it back first, goes on, and says so */
  reset();
  make_zip(ZIP, &sp);
  make_db();
  {
    char alt[700];
    snprintf(alt, sizeof(alt), "%s.ps5cc-alt", IMG);
    wfile(IMG, "NEW IMAGE", 9);
    wfile(alt, "OLD IMAGE", 9);
    write_journal("swap", "PPSA01234", "SAVE0", 1, 0, 0, 0, IMG, ICONP, DBP);
    a = args();
    memset(&pr, 0, sizeof(pr));
    rc = saveimport_run(&a, hooks(&pr), &res, err, sizeof(err));
    CHECK(rc == 0 && strstr(res.note, "zurückgenommen") && res.replaced == 1 && fsize(IMG) == 48ll * 65536 && !exists(alt) && !exists(JOURNAL));
  }

  /* ---- no record is left behind by a run that ends, whichever way: done, failed (the row cannot be written), stopped */
  reset();
  make_zip(ZIP, &sp);
  a = args();
  rc = saveimport_run(&a, hooks(&pr), &res, err, sizeof(err));
  CHECK(rc == 0 && !exists(JOURNAL));
  reset();
  make_zip(ZIP, &sp);
  {
    sqlite3 *db = NULL;
    sqlite3_open(DBP, &db);
    sqlite3_exec(db, "CREATE TABLE savedata (title_id, dir_name);", NULL, NULL, NULL);
    sqlite3_close(db);
  }
  wfile(IMG, "OLD IMAGE", 9);
  rc = saveimport_run(&a, hooks(&pr), &res, err, sizeof(err));
  CHECK(rc != 0 && fsize(IMG) == 9 && !exists(JOURNAL));

  /* ---- a record that cannot be believed is removed and touches nothing: garbage, a way out of the folders, a name that is not the slot's */
  reset();
  wfile(IMG, "OLD IMAGE", 9);
  wfile(JOURNAL, "this is not json at all", 23);
  CHECK(saveimport_recover(JOURNAL, err, sizeof(err)) == 1 && strstr(err, "nicht lesbar") && !exists(JOURNAL) && fsize(IMG) == 9);
  write_journal("swap", "PPSA01234", "SAVE0", 0, 0, 0, 0, "/tmp/../etc/sdimg_SAVE0", ICONP, DBP);
  CHECK(saveimport_recover(JOURNAL, err, sizeof(err)) == 1 && strstr(err, "nicht lesbar") && !exists(JOURNAL));
  write_journal("swap", "PPSA01234", "SAVE0", 0, 0, 0, 0, "/tmp/ps5tm-x/home/sdimg_OTHER", ICONP, DBP);          /* the image is not the slot's */
  CHECK(saveimport_recover(JOURNAL, err, sizeof(err)) == 1 && strstr(err, "nicht lesbar") && !exists(JOURNAL));
  CHECK(saveimport_recover(NULL, err, sizeof(err)) == 0 && saveimport_recover("", err, sizeof(err)) == 0);

  /* ---- PS4: the image and its key file go back as a pair */
  reset4();
  make_db4();
  {
    char alt[700], balt[700], bneu[700];
    snprintf(alt, sizeof(alt), "%s.ps5cc-alt", IMG4);
    snprintf(balt, sizeof(balt), "%s.ps5cc-alt", BIN4);
    snprintf(bneu, sizeof(bneu), "%s.ps5cc-neu", BIN4);
    wfile(IMG4, "NEW IMAGE", 9);
    wfile(alt, "OLD IMAGE", 9);
    wfile(balt, "OLD KEY FILE", 12);
    wfile(bneu, "NEW KEY FILE", 12);                                  /* cut off between the two renames: the new key file never arrived */
    write_journal("swap", "CUSA12345", "SLOT0", 1, 1, 1, 0, IMG4, ICON4, DBP4);
    uint8_t b1[16], b2[16];
    CHECK(saveimport_recover(JOURNAL, err, sizeof(err)) == 1 && slurp(IMG4, b1, sizeof(b1)) == 9 && !memcmp(b1, "OLD IMAGE", 9) && slurp(BIN4, b2, sizeof(b2)) == 12 && !memcmp(b2, "OLD KEY FILE", 12));
    CHECK(!exists(alt) && !exists(balt) && !exists(bneu) && !exists(JOURNAL));
  }

  /* ================================================================ the owner search is done once for the slots of one job */
  /* ---- found for the first slot: the second slot takes the values without mounting a donor again */
  reset();
  g_marker = 0xD0;
  sp = good();
  make_zip(ZIP, &sp);
  make_donor5("PPSA00099", "DON", ACC, UIDN);
  {
    saveimport_owner_t oc;
    memset(&oc, 0, sizeof(oc));
    a = args();
    a.owner_cache = &oc;
    memset(&pr, 0, sizeof(pr));
    ps5tm_savemnt_test_opened = ps5tm_savemnt_test_closed = 0;
    rc = saveimport_run(&a, hooks(&pr), &res, err, sizeof(err));
    CHECK(rc == 0 && oc.state == 1 && oc.rank == 2 && ps5tm_savemnt_test_opened == 3);                 /* its own two mounts and the donor's */
    sp = good(); sp.slot = "SAVE1";
    make_zip(ZIP, &sp);
    memset(&pr, 0, sizeof(pr));
    ps5tm_savemnt_test_opened = ps5tm_savemnt_test_closed = 0;
    rc = saveimport_run(&a, hooks(&pr), &res, err, sizeof(err));
    uint8_t pa[1024];
    CHECK(rc == 0 && ps5tm_savemnt_test_opened == 2 && ps5tm_savemnt_test_closed == 2 && strstr(res.note, "anderen Spielstand"));          /* no donor was mounted */
    CHECK(stored_params("SAVE1", pa) == 0 && pa[4] == 0x31 && pa[8] == 0xD0 && pa[39] == 0xD0 + 31);                                         /* the same values */
    /* ... a slot that has a save of its own is searched again: its own record is the best donor (rank 0, the whole record) */
    g_marker = 0xE0;
    make_donor5("PPSA01234", "SAVE2", ACC, UIDN);
    sp = good(); sp.slot = "SAVE2";
    make_zip(ZIP, &sp);
    memset(&pr, 0, sizeof(pr));
    ps5tm_savemnt_test_opened = ps5tm_savemnt_test_closed = 0;
    rc = saveimport_run(&a, hooks(&pr), &res, err, sizeof(err));
    CHECK(rc == 0 && ps5tm_savemnt_test_opened == 3 && oc.rank == 0 && stored_params("SAVE2", pa) == 0 && pa[8] == 0xE0 && pa[100] == 0x77);
  }
  /* ---- nothing found for the first slot (a donor of another account): the second slot does not look again, and says the same */
  reset();
  g_marker = 0xD0;
  sp = good();
  make_zip(ZIP, &sp);
  make_donor5("PPSA00099", "DON", 0x1234, UIDN);
  make_donor5("PPSA00098", "DON", 0x1234, UIDN);
  {
    saveimport_owner_t oc;
    memset(&oc, 0, sizeof(oc));
    a = args();
    a.owner_cache = &oc;
    memset(&pr, 0, sizeof(pr));
    ps5tm_savemnt_test_opened = ps5tm_savemnt_test_closed = 0;
    rc = saveimport_run(&a, hooks(&pr), &res, err, sizeof(err));
    CHECK(rc == 0 && oc.state == 2 && ps5tm_savemnt_test_opened == 4 && strstr(res.note, "kein Spielstand zu finden"));              /* two of its own, two donors tried */
    sp = good(); sp.slot = "SAVE1";
    make_zip(ZIP, &sp);
    memset(&pr, 0, sizeof(pr));
    ps5tm_savemnt_test_opened = ps5tm_savemnt_test_closed = 0;
    rc = saveimport_run(&a, hooks(&pr), &res, err, sizeof(err));
    CHECK(rc == 0 && ps5tm_savemnt_test_opened == 2 && strstr(res.note, "kein Spielstand zu finden"));
  }
  /* ---- without a cache every run searches, as before */
  reset();
  sp = good();
  make_zip(ZIP, &sp);
  make_donor5("PPSA00099", "DON", ACC, UIDN);
  a = args();
  ps5tm_savemnt_test_opened = ps5tm_savemnt_test_closed = 0;
  rc = saveimport_run(&a, hooks(&pr), &res, err, sizeof(err));
  CHECK(rc == 0 && ps5tm_savemnt_test_opened == 3);
  g_marker = 0;

  rmtree(ROOT);
  printf("%d checks, %d failed\n", g_n, g_fail);
  return g_fail ? 1 : 0;
}

/* the mount of the second pass shows a file that is not what was written */
void
corrupt_on_second(const char *mount_dir, int nth) {
  if(nth != 2) return;
  char p[700];
  snprintf(p, sizeof(p), "%s/data.bin", mount_dir);
  FILE *f = fopen(p, "r+b");
  if(f) { fputc(0x42, f); fclose(f); }
}
