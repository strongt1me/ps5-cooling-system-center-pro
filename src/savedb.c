/* PS5 Cooling & System Center - Pro: the entry of a save in the console's save database (savedb.h). */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "savedb.h"
#include "sqlite3/sqlite3.h"

/* The schema as the console itself writes it (firmware 12.00, user_version 11). */
static const char k_schema[] =
  "CREATE TABLE savedata (id INTEGER PRIMARY KEY AUTOINCREMENT NOT NULL,title_id NOT NULL, dir_name NOT NULL, main_title NOT NULL, sub_title , detail , "
  "tmp_dir_name , is_broken , user_param , blocks , free_blocks , size_kib , mtime NOT NULL, fake_broken , account_id , user_id , faked_owner , "
  "cloud_icon_url , cloud_revision , game_title_id NOT NULL, system_blocks , convert_performance , account_id_in_usb_path );"
  "CREATE INDEX savedata_index on savedata(title_id, dir_name, user_param, size_kib, mtime, game_title_id);"
  "PRAGMA user_version=11;";

static void
set_err(char *err, size_t n, const char *what, sqlite3 *db) {
  if(n) snprintf(err, n, "%s: %s", what, db ? sqlite3_errmsg(db) : "kein Speicher");
}

/* The columns an upsert uses must all be there. */
static int
schema_ok(sqlite3 *db, char *err, size_t err_len) {
  static const char *const need[] = { "title_id", "dir_name", "main_title", "sub_title", "detail", "is_broken", "user_param", "blocks", "free_blocks", "size_kib",
                                      "mtime", "account_id", "user_id", "faked_owner", "game_title_id", "system_blocks" };
  sqlite3_stmt *st = NULL;
  if(sqlite3_prepare_v2(db, "PRAGMA table_info(savedata)", -1, &st, NULL) != SQLITE_OK) { set_err(err, err_len, "Spielstand-Datenbank", db); return -1; }
  unsigned have = 0, rows = 0;
  while(sqlite3_step(st) == SQLITE_ROW) {
    const char *name = (const char *)sqlite3_column_text(st, 1);
    rows++;
    for(unsigned i = 0; name && i < sizeof(need) / sizeof(need[0]); i++)
      if(!strcmp(name, need[i])) have |= 1u << i;
  }
  sqlite3_finalize(st);
  if(!rows || have != (1u << (sizeof(need) / sizeof(need[0]))) - 1) {
    if(err_len) snprintf(err, err_len, "Die Spielstand-Datenbank hat einen anderen Aufbau als erwartet; sie wurde nicht verändert.");
    return -1;
  }
  return 0;
}

/* A database is only ever made in a folder that is there: a path that is wrong must not make a tree of folders on the console. */
static int
parent_exists(const char *file) {
  char tmp[512];
  if(snprintf(tmp, sizeof(tmp), "%s", file) >= (int)sizeof(tmp)) return 0;
  char *slash = strrchr(tmp, '/');
  if(!slash || slash == tmp) return 0;
  *slash = 0;
  struct stat st;
  return stat(tmp, &st) == 0 && S_ISDIR(st.st_mode);
}

int
savedb_upsert(const char *db_path, const savedb_row_t *row, int *inserted, char *err, size_t err_len) {
  if(err_len) err[0] = 0;
  if(inserted) *inserted = 0;
  sqlite3 *db = NULL;
  struct stat st;
  const int exists = stat(db_path, &st) == 0;
  if(!exists && !parent_exists(db_path)) {
    snprintf(err, err_len, "Der Ordner der Spielstand-Datenbank fehlt; es wurde nichts angelegt.");
    return -1;
  }
  int rc = sqlite3_open_v2(db_path, &db, SQLITE_OPEN_READWRITE | (exists ? 0 : SQLITE_OPEN_CREATE), NULL);
  if(rc != SQLITE_OK) { set_err(err, err_len, "Die Spielstand-Datenbank ließ sich nicht öffnen", db); sqlite3_close(db); return -1; }
  sqlite3_busy_timeout(db, 5000);
  if(!exists) {
    char *msg = NULL;
    if(sqlite3_exec(db, k_schema, NULL, NULL, &msg) != SQLITE_OK) {
      snprintf(err, err_len, "Die Spielstand-Datenbank ließ sich nicht anlegen: %s", msg ? msg : "?");
      sqlite3_free(msg);
      sqlite3_close(db);
      unlink(db_path);
      return -1;
    }
  } else if(schema_ok(db, err, err_len) != 0) {
    sqlite3_close(db);
    return -1;
  }

  char *msg = NULL;
  if(sqlite3_exec(db, "BEGIN IMMEDIATE", NULL, NULL, &msg) != SQLITE_OK) {
    snprintf(err, err_len, "Die Spielstand-Datenbank ist gerade gesperrt: %s", msg ? msg : "?");
    sqlite3_free(msg);
    sqlite3_close(db);
    return -1;
  }
  sqlite3_stmt *stmt = NULL;
  rc = sqlite3_prepare_v2(db,
        "UPDATE savedata SET main_title=?1, sub_title=?2, detail=?3, mtime=?4, account_id=?5, user_id=?6, user_param=?7, is_broken=0, faked_owner=0, "
        "blocks=?8, free_blocks=?9, size_kib=?10, system_blocks=?11 WHERE title_id=?12 AND dir_name=?13", -1, &stmt, NULL);
  if(rc == SQLITE_OK) {
    sqlite3_bind_text(stmt, 1, row->main_title, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, row->sub_title, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, row->detail, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, row->mtime, -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 5, (sqlite3_int64)row->account_id);
    sqlite3_bind_int64(stmt, 6, (sqlite3_int64)row->user_id);
    sqlite3_bind_int64(stmt, 7, (sqlite3_int64)row->user_param);
    sqlite3_bind_int64(stmt, 8, (sqlite3_int64)row->blocks);
    sqlite3_bind_int64(stmt, 9, (sqlite3_int64)row->free_blocks);
    sqlite3_bind_int64(stmt, 10, (sqlite3_int64)row->size_kib);
    sqlite3_bind_int64(stmt, 11, (sqlite3_int64)row->system_blocks);
    sqlite3_bind_text(stmt, 12, row->title_id, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 13, row->dir_name, -1, SQLITE_STATIC);
    rc = sqlite3_step(stmt);
  }
  const int changed = rc == SQLITE_DONE ? sqlite3_changes(db) : 0;
  if(stmt) sqlite3_finalize(stmt);
  stmt = NULL;
  if(rc != SQLITE_DONE) {
    set_err(err, err_len, "Der Eintrag in der Spielstand-Datenbank ließ sich nicht ändern", db);
    sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);
    sqlite3_close(db);
    return -1;
  }
  if(!changed) {
    rc = sqlite3_prepare_v2(db,
          "INSERT INTO savedata (title_id, dir_name, main_title, sub_title, detail, tmp_dir_name, is_broken, user_param, blocks, free_blocks, size_kib, mtime, "
          "fake_broken, account_id, user_id, faked_owner, cloud_icon_url, cloud_revision, game_title_id, system_blocks, convert_performance, account_id_in_usb_path) "
          "VALUES (?1,?2,?3,?4,?5,'',0,?6,?7,?8,?9,?10,0,?11,?12,0,'',0,?1,?13,0,'0000000000000000')", -1, &stmt, NULL);
    if(rc == SQLITE_OK) {
      sqlite3_bind_text(stmt, 1, row->title_id, -1, SQLITE_STATIC);
      sqlite3_bind_text(stmt, 2, row->dir_name, -1, SQLITE_STATIC);
      sqlite3_bind_text(stmt, 3, row->main_title, -1, SQLITE_STATIC);
      sqlite3_bind_text(stmt, 4, row->sub_title, -1, SQLITE_STATIC);
      sqlite3_bind_text(stmt, 5, row->detail, -1, SQLITE_STATIC);
      sqlite3_bind_int64(stmt, 6, (sqlite3_int64)row->user_param);
      sqlite3_bind_int64(stmt, 7, (sqlite3_int64)row->blocks);
      sqlite3_bind_int64(stmt, 8, (sqlite3_int64)row->free_blocks);
      sqlite3_bind_int64(stmt, 9, (sqlite3_int64)row->size_kib);
      sqlite3_bind_text(stmt, 10, row->mtime, -1, SQLITE_STATIC);
      sqlite3_bind_int64(stmt, 11, (sqlite3_int64)row->account_id);
      sqlite3_bind_int64(stmt, 12, (sqlite3_int64)row->user_id);
      sqlite3_bind_int64(stmt, 13, (sqlite3_int64)row->system_blocks);
      rc = sqlite3_step(stmt);
    }
    if(stmt) sqlite3_finalize(stmt);
    if(rc != SQLITE_DONE) {
      set_err(err, err_len, "Der Eintrag in der Spielstand-Datenbank ließ sich nicht anlegen", db);
      sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);
      sqlite3_close(db);
      return -1;
    }
    if(inserted) *inserted = 1;
  }
  if(sqlite3_exec(db, "COMMIT", NULL, NULL, &msg) != SQLITE_OK) {
    snprintf(err, err_len, "Die Spielstand-Datenbank ließ sich nicht speichern: %s", msg ? msg : "?");
    sqlite3_free(msg);
    sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);
    sqlite3_close(db);
    if(inserted) *inserted = 0;
    return -1;
  }
  sqlite3_close(db);
  return 0;
}

int
savedb_backup(const char *db_path, const char *dest_path, char *err, size_t err_len) {
  if(err_len) err[0] = 0;
  struct stat st;
  if(stat(db_path, &st) != 0) return 0;                          /* nothing to keep: the database does not exist yet */
  if(stat(dest_path, &st) == 0) { snprintf(err, err_len, "Die Sicherung der Spielstand-Datenbank gibt es schon."); return -1; }
  sqlite3 *src = NULL, *dst = NULL;
  int rc = sqlite3_open_v2(db_path, &src, SQLITE_OPEN_READONLY, NULL);
  if(rc != SQLITE_OK) { set_err(err, err_len, "Die Spielstand-Datenbank ließ sich nicht öffnen", src); sqlite3_close(src); return -1; }
  sqlite3_busy_timeout(src, 5000);
  rc = sqlite3_open_v2(dest_path, &dst, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL);
  if(rc != SQLITE_OK) { set_err(err, err_len, "Die Sicherung der Spielstand-Datenbank ließ sich nicht anlegen", dst); sqlite3_close(dst); sqlite3_close(src); return -1; }
  sqlite3_backup *b = sqlite3_backup_init(dst, "main", src, "main");
  if(!b) { set_err(err, err_len, "Die Spielstand-Datenbank ließ sich nicht sichern", dst); sqlite3_close(dst); sqlite3_close(src); unlink(dest_path); return -1; }
  rc = sqlite3_backup_step(b, -1);
  sqlite3_backup_finish(b);
  const int ok = rc == SQLITE_DONE;
  if(!ok) set_err(err, err_len, "Die Spielstand-Datenbank ließ sich nicht sichern", dst);
  sqlite3_close(dst);
  sqlite3_close(src);
  if(!ok) unlink(dest_path);
  return ok ? 0 : -1;
}

int
savedb_check(const char *db_path, char *err, size_t err_len) {
  if(err_len) err[0] = 0;
  sqlite3 *db = NULL;
  int rc = sqlite3_open_v2(db_path, &db, SQLITE_OPEN_READONLY, NULL);
  if(rc != SQLITE_OK) { set_err(err, err_len, "Die Spielstand-Datenbank ließ sich nicht öffnen", db); sqlite3_close(db); return -1; }
  sqlite3_busy_timeout(db, 5000);
  sqlite3_stmt *st = NULL;
  int ok = 0;
  if(sqlite3_prepare_v2(db, "PRAGMA quick_check", -1, &st, NULL) == SQLITE_OK) {
    if(sqlite3_step(st) == SQLITE_ROW) {
      const char *t = (const char *)sqlite3_column_text(st, 0);
      ok = t && !strcmp(t, "ok");
      if(!ok && err_len) snprintf(err, err_len, "Die Prüfung der Spielstand-Datenbank meldet: %.200s", t ? t : "?");
    } else {
      set_err(err, err_len, "Die Spielstand-Datenbank ließ sich nicht prüfen", db);     /* busy (the console is writing), an I/O error: said, not called damage */
    }
    sqlite3_finalize(st);
  } else set_err(err, err_len, "Die Spielstand-Datenbank ließ sich nicht prüfen", db);
  sqlite3_close(db);
  return ok ? 0 : -1;
}

int
savedb_has_slot(const char *db_path, const char *title_id, const char *dir_name, char *err, size_t err_len) {
  if(err_len) err[0] = 0;
  sqlite3 *db = NULL;
  int rc = sqlite3_open_v2(db_path, &db, SQLITE_OPEN_READONLY, NULL);
  if(rc != SQLITE_OK) { set_err(err, err_len, "Die Spielstand-Datenbank ließ sich nicht öffnen", db); sqlite3_close(db); return -1; }
  sqlite3_busy_timeout(db, 5000);
  sqlite3_stmt *st = NULL;
  int n = -1;
  if(sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM savedata WHERE title_id=?1 AND dir_name=?2", -1, &st, NULL) == SQLITE_OK) {
    sqlite3_bind_text(st, 1, title_id, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 2, dir_name, -1, SQLITE_STATIC);
    if(sqlite3_step(st) == SQLITE_ROW) n = sqlite3_column_int(st, 0);
    sqlite3_finalize(st);
  } else set_err(err, err_len, "Die Spielstand-Datenbank ließ sich nicht lesen", db);
  sqlite3_close(db);
  return n;
}
