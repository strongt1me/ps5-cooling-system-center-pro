/* savedb.c — write access to the console's savedata.db for save re-signing.
 *
 * The savedata.db schema (from a real console, user_version 11):
 *
 *   CREATE TABLE savedata (
 *     id INTEGER PRIMARY KEY,
 *     title_id TEXT NOT NULL,
 *     dir_name TEXT NOT NULL,
 *     main_title TEXT NOT NULL,
 *     sub_title TEXT,
 *     detail TEXT,
 *     tmp_dir_name TEXT,
 *     is_broken INTEGER DEFAULT 0,
 *     user_param BLOB,
 *     blocks INTEGER NOT NULL DEFAULT 0,
 *     free_blocks INTEGER NOT NULL DEFAULT 0,
 *     size_kib INTEGER NOT NULL DEFAULT 0,
 *     mtime TEXT NOT NULL,
 *     fake_broken INTEGER DEFAULT 0,
 *     account_id TEXT NOT NULL DEFAULT '0000000000000000',
 *     user_id INTEGER NOT NULL DEFAULT 0,
 *     faked_owner INTEGER DEFAULT 0,
 *     cloud_icon_url TEXT,
 *     cloud_revision TEXT,
 *     game_title_id TEXT,
 *     system_blocks INTEGER NOT NULL DEFAULT 0,
 *     convert_performance INTEGER,
 *     account_id_in_usb_path TEXT NOT NULL DEFAULT '0000000000000000'
 *   );
 *
 * We only update the ownership + size fields; everything else is LEFT AS IS
 * on UPDATE so the console's own entries are not clobbered.
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "savedb.h"
#include "third_party/sqlite3/sqlite3.h"

int savedb_upsert(const char *db_path, const savedb_row_t *row)
{
#ifdef PS5TM_HOST_TEST
    /* Under host tests just print what we would do */
    printf("[savedb] upsert %s/%s uid=0x%x acid=%016llx blocks=%llu\n",
           row->title_id, row->dir_name, row->user_id,
           (unsigned long long)row->account_id,
           (unsigned long long)row->blocks);
    return 0;
#else
    sqlite3 *db = NULL;
    int rc;

    rc = sqlite3_open_v2(db_path, &db,
                         SQLITE_OPEN_READWRITE | SQLITE_OPEN_FULLMUTEX, NULL);
    if (rc != SQLITE_OK) {
        printf("[savedb] open %s: %s\n", db_path, sqlite3_errmsg(db));
        sqlite3_close(db);
        return -1;
    }

    /* Format account_id as 16 hex digits (the column is TEXT) */
    char acid_str[17];
    snprintf(acid_str, sizeof(acid_str), "%016llx",
             (unsigned long long)row->account_id);

    uint64_t size_kib = row->blocks * 0x40ULL;

    /* Try UPDATE first (most common path: row already exists for target user) */
    const char *sql_upd =
        "UPDATE savedata SET "
        "  account_id=?1, user_id=?2, blocks=?3, free_blocks=0, "
        "  size_kib=?4, mtime=?5, faked_owner=0, is_broken=0 "
        "WHERE title_id=?6 AND dir_name=?7";

    sqlite3_stmt *stmt = NULL;
    rc = sqlite3_prepare_v2(db, sql_upd, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        printf("[savedb] prepare UPDATE: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return -1;
    }
    sqlite3_bind_text (stmt, 1, acid_str,          -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 2, (sqlite3_int64)row->user_id);
    sqlite3_bind_int64(stmt, 3, (sqlite3_int64)row->blocks);
    sqlite3_bind_int64(stmt, 4, (sqlite3_int64)size_kib);
    sqlite3_bind_text (stmt, 5, row->mtime,         -1, SQLITE_STATIC);
    sqlite3_bind_text (stmt, 6, row->title_id,      -1, SQLITE_STATIC);
    sqlite3_bind_text (stmt, 7, row->dir_name,      -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    stmt = NULL;

    int rows_changed = sqlite3_changes(db);

    if (rc != SQLITE_DONE) {
        printf("[savedb] UPDATE step: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return -1;
    }

    if (rows_changed == 0) {
        /* No existing row — INSERT */
        const char *sql_ins =
            "INSERT INTO savedata "
            "  (title_id, dir_name, main_title, sub_title, detail, "
            "   is_broken, blocks, free_blocks, size_kib, mtime, "
            "   account_id, user_id, faked_owner, game_title_id, "
            "   system_blocks, account_id_in_usb_path) "
            "VALUES (?1,?2,?3,'','', 0,?4,0,?5,?6, ?7,?8,0,?9, 0,'0000000000000000')";

        rc = sqlite3_prepare_v2(db, sql_ins, -1, &stmt, NULL);
        if (rc != SQLITE_OK) {
            printf("[savedb] prepare INSERT: %s\n", sqlite3_errmsg(db));
            sqlite3_close(db);
            return -1;
        }
        sqlite3_bind_text (stmt, 1, row->title_id,   -1, SQLITE_STATIC);
        sqlite3_bind_text (stmt, 2, row->dir_name,   -1, SQLITE_STATIC);
        sqlite3_bind_text (stmt, 3, row->main_title, -1, SQLITE_STATIC);
        sqlite3_bind_int64(stmt, 4, (sqlite3_int64)row->blocks);
        sqlite3_bind_int64(stmt, 5, (sqlite3_int64)size_kib);
        sqlite3_bind_text (stmt, 6, row->mtime,      -1, SQLITE_STATIC);
        sqlite3_bind_text (stmt, 7, acid_str,         -1, SQLITE_STATIC);
        sqlite3_bind_int64(stmt, 8, (sqlite3_int64)row->user_id);
        sqlite3_bind_text (stmt, 9, row->title_id,   -1, SQLITE_STATIC);

        rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        if (rc != SQLITE_DONE) {
            printf("[savedb] INSERT step: %s\n", sqlite3_errmsg(db));
            sqlite3_close(db);
            return -1;
        }
    }

    sqlite3_close(db);
    return 0;
#endif
}
