/* Host test of src/savedb.c: the entry of a save in the console's save database (update, insert, create, refuse a strange schema, back up, check). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "savedb.h"
#include "sqlite3/sqlite3.h"

static int g_fail, g_n;
#define CHECK(c) do { g_n++; if(!(c)) { g_fail++; printf("FAIL line %d: %s\n", __LINE__, #c); } } while(0)

static char DIR_[300], DB[400];

static void
sql(const char *path, const char *q) {
  sqlite3 *db = NULL;
  if(sqlite3_open(path, &db) == SQLITE_OK) {
    char *msg = NULL;
    if(sqlite3_exec(db, q, NULL, NULL, &msg) != SQLITE_OK) { printf("  test sql error: %s\n", msg); sqlite3_free(msg); }
  }
  sqlite3_close(db);
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

static int
text_is(const char *path, const char *q, const char *want) {
  sqlite3 *db = NULL;
  sqlite3_stmt *st = NULL;
  int ok = 0;
  if(sqlite3_open_v2(path, &db, SQLITE_OPEN_READONLY, NULL) == SQLITE_OK && sqlite3_prepare_v2(db, q, -1, &st, NULL) == SQLITE_OK && sqlite3_step(st) == SQLITE_ROW) {
    const char *t = (const char *)sqlite3_column_text(st, 0);
    ok = t && !strcmp(t, want);
  }
  if(st) sqlite3_finalize(st);
  sqlite3_close(db);
  return ok;
}

static const char REAL_SCHEMA[] =
  "CREATE TABLE savedata (id INTEGER PRIMARY KEY AUTOINCREMENT NOT NULL,title_id NOT NULL, dir_name NOT NULL, main_title NOT NULL, sub_title , detail , "
  "tmp_dir_name , is_broken , user_param , blocks , free_blocks , size_kib , mtime NOT NULL, fake_broken , account_id , user_id , faked_owner , "
  "cloud_icon_url , cloud_revision , game_title_id NOT NULL, system_blocks , convert_performance , account_id_in_usb_path );"
  "CREATE INDEX savedata_index on savedata(title_id, dir_name, user_param, size_kib, mtime, game_title_id);"
  "PRAGMA user_version=11;"
  "INSERT INTO savedata VALUES (1,'PPSA01325','sce_sdmemory','Gespeicherte Daten','','','',0,0,48,8,3072,'2026-11-17T16:39:53.00Z',0,0,404100401,0,'',0,'PPSA01325',0,0,'0000000000000000');"
  "INSERT INTO savedata VALUES (6,'PPSA08395','SLOT00','Jade Ross','Fighter','Jade','',0,0,3010,1469,192640,'2026-05-25T20:03:25.00Z',0,0,404100401,0,'',0,'PPSA08395',1505,0,'0000000000000000');";

static savedb_row_t
row(const char *title, const char *dir) {
  savedb_row_t r;
  memset(&r, 0, sizeof(r));
  snprintf(r.title_id, sizeof(r.title_id), "%s", title);
  snprintf(r.dir_name, sizeof(r.dir_name), "%s", dir);
  snprintf(r.main_title, sizeof(r.main_title), "Gespeicherte Daten");
  snprintf(r.mtime, sizeof(r.mtime), "2026-11-19T07:34:00.00Z");
  r.account_id = 0x1122334455667788ull;
  r.user_id = 0x18161531;
  r.user_param = 7;
  r.blocks = 48;
  r.free_blocks = 10;
  r.size_kib = 3072;
  return r;
}

static int
exists_(const char *p) {
  struct stat st;
  return stat(p, &st) == 0;
}

static long long
filesize(const char *p) {
  struct stat st;
  return stat(p, &st) == 0 ? (long long)st.st_size : -1;
}

int
main(int argc, char **argv) {
  snprintf(DIR_, sizeof(DIR_), "%s", argc > 1 ? argv[1] : "/tmp/ps5tm-savedb");
  char cmd[1000];
  snprintf(cmd, sizeof(cmd), "rm -rf '%s' && mkdir -p '%s'", DIR_, DIR_);
  if(system(cmd)) return 2;
  snprintf(DB, sizeof(DB), "%s/savedata.db", DIR_);
  char err[400];
  int ins = -1;

  /* --- update of an existing slot */
  sql(DB, REAL_SCHEMA);
  CHECK(scalar(DB, "select count(*) from savedata") == 2);
  CHECK(savedb_has_slot(DB, "PPSA01325", "sce_sdmemory", err, sizeof(err)) == 1);
  CHECK(savedb_has_slot(DB, "PPSA01325", "nope", err, sizeof(err)) == 0);
  savedb_row_t r = row("PPSA01325", "sce_sdmemory");
  CHECK(savedb_upsert(DB, &r, &ins, err, sizeof(err)) == 0 && ins == 0);
  CHECK(scalar(DB, "select count(*) from savedata") == 2);
  CHECK(scalar(DB, "select user_id from savedata where id=1") == 0x18161531);
  CHECK(scalar(DB, "select account_id from savedata where id=1") == 0x1122334455667788ll);
  CHECK(scalar(DB, "select user_param from savedata where id=1") == 7 && scalar(DB, "select free_blocks from savedata where id=1") == 10);
  CHECK(text_is(DB, "select mtime from savedata where id=1", "2026-11-19T07:34:00.00Z"));
  CHECK(scalar(DB, "select cloud_revision from savedata where id=1") == 0 && text_is(DB, "select game_title_id from savedata where id=1", "PPSA01325"));
  CHECK(scalar(DB, "select blocks from savedata where id=6") == 3010);                              /* the other slot is untouched */

  /* --- insert of a new slot */
  r = row("PPSA06328", "SaveData");
  CHECK(savedb_upsert(DB, &r, &ins, err, sizeof(err)) == 0 && ins == 1);
  CHECK(scalar(DB, "select count(*) from savedata") == 3 && scalar(DB, "select max(id) from savedata") > 6);
  CHECK(text_is(DB, "select game_title_id from savedata where dir_name='SaveData'", "PPSA06328"));
  CHECK(text_is(DB, "select account_id_in_usb_path from savedata where dir_name='SaveData'", "0000000000000000"));
  CHECK(scalar(DB, "select is_broken from savedata where dir_name='SaveData'") == 0);
  CHECK(savedb_check(DB, err, sizeof(err)) == 0);

  /* --- a name with quotes and umlauts goes through as it is (bound, never glued into the statement) */
  r = row("PPSA06328", "Slot'; DROP TABLE savedata;--");
  snprintf(r.main_title, sizeof(r.main_title), "Zürich's \"Spiel\" 日本語");
  CHECK(savedb_upsert(DB, &r, &ins, err, sizeof(err)) == 0 && ins == 1);
  CHECK(scalar(DB, "select count(*) from savedata") == 4);
  CHECK(text_is(DB, "select main_title from savedata where id=(select max(id) from savedata)", "Zürich's \"Spiel\" 日本語"));

  /* --- update twice: no second row */
  r = row("PPSA06328", "SaveData");
  r.blocks = 96;
  CHECK(savedb_upsert(DB, &r, &ins, err, sizeof(err)) == 0 && ins == 0 && scalar(DB, "select count(*) from savedata") == 4);
  CHECK(scalar(DB, "select blocks from savedata where dir_name='SaveData'") == 96);

  /* --- backup */
  char bk[500];
  snprintf(bk, sizeof(bk), "%s/backup.db", DIR_);
  CHECK(savedb_backup(DB, bk, err, sizeof(err)) == 0 && filesize(bk) > 0);
  CHECK(scalar(bk, "select count(*) from savedata") == 4 && savedb_check(bk, err, sizeof(err)) == 0);
  CHECK(savedb_backup(DB, bk, err, sizeof(err)) != 0 && strstr(err, "gibt es schon"));              /* never over an existing copy */
  snprintf(bk, sizeof(bk), "%s/nothing.db", DIR_);
  CHECK(savedb_backup("/nonexistent/savedata.db", bk, err, sizeof(err)) == 0 && filesize(bk) < 0);  /* no database yet: nothing to keep */

  /* --- the database does not exist: made with the console's schema */
  char fresh[500];
  snprintf(fresh, sizeof(fresh), "%s/new/user/savedata.db", DIR_);
  r = row("PPSA09999", "SAVE0");
  CHECK(savedb_upsert(fresh, &r, &ins, err, sizeof(err)) != 0 && strstr(err, "fehlt") && !exists_(fresh));      /* its folder is not there: nothing is made, no tree of folders either */
  {
    char mk[600];
    snprintf(mk, sizeof(mk), "%s/new/user", DIR_);
    char cmd2[700];
    snprintf(cmd2, sizeof(cmd2), "mkdir -p '%s'", mk);
    if(system(cmd2)) {}
  }
  CHECK(savedb_upsert(fresh, &r, &ins, err, sizeof(err)) == 0 && ins == 1);
  CHECK(scalar(fresh, "pragma user_version") == 11 && scalar(fresh, "select count(*) from savedata") == 1);
  CHECK(scalar(fresh, "select count(*) from sqlite_master where name='savedata_index'") == 1);
  CHECK(savedb_check(fresh, err, sizeof(err)) == 0);

  /* --- a database of another shape is refused and left as it is */
  char other[500];
  snprintf(other, sizeof(other), "%s/other.db", DIR_);
  sql(other, "CREATE TABLE savedata (a, b); INSERT INTO savedata VALUES (1,2);");
  long long size_before = filesize(other);
  r = row("PPSA01325", "x");
  CHECK(savedb_upsert(other, &r, &ins, err, sizeof(err)) != 0 && strstr(err, "anderen Aufbau") && filesize(other) == size_before);
  CHECK(scalar(other, "select count(*) from savedata") == 1);
  snprintf(other, sizeof(other), "%s/empty.db", DIR_);
  sql(other, "CREATE TABLE unrelated (a);");
  CHECK(savedb_upsert(other, &r, &ins, err, sizeof(err)) != 0 && strstr(err, "anderen Aufbau"));

  /* --- not a database at all */
  snprintf(other, sizeof(other), "%s/garbage.db", DIR_);
  {
    FILE *f = fopen(other, "wb");
    char junk[5000];
    memset(junk, 0x5a, sizeof(junk));
    fwrite(junk, 1, sizeof(junk), f);
    fclose(f);
  }
  CHECK(savedb_upsert(other, &r, &ins, err, sizeof(err)) != 0);
  CHECK(savedb_check(other, err, sizeof(err)) != 0);
  CHECK(savedb_has_slot(other, "a", "b", err, sizeof(err)) == -1);

  /* --- a database somebody else holds locked: a refusal after the wait, nothing changed */
  {
    sqlite3 *holder = NULL;
    sqlite3_open(DB, &holder);
    sqlite3_exec(holder, "BEGIN EXCLUSIVE", NULL, NULL, NULL);
    r = row("PPSA77777", "LOCKED");
    CHECK(savedb_upsert(DB, &r, &ins, err, sizeof(err)) != 0);
    CHECK(savedb_check(DB, err, sizeof(err)) != 0 && err[0] && strstr(err, "nicht prüfen"));      /* busy is said, not called damage (and not an empty reason) */
    sqlite3_exec(holder, "ROLLBACK", NULL, NULL, NULL);
    sqlite3_close(holder);
    CHECK(scalar(DB, "select count(*) from savedata where dir_name='LOCKED'") == 0);
  }

  snprintf(cmd, sizeof(cmd), "rm -rf '%s'", DIR_);
  if(system(cmd)) {}
  printf("%d checks, %d failed\n", g_n, g_fail);
  return g_fail ? 1 : 0;
}
