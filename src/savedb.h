/* savedb.h — write access to the console's savedata.db for save re-signing.
 *
 * The console keeps one SQLite database per user in:
 *   PS5: /system_data/savedata_prospero/<uid>/db/user/savedata.db
 *   PS4: /system_data/savedata/<uid>/db/user/savedata.db
 *
 * savedb_upsert inserts a new row for a title/slot, or updates the existing
 * one.  Only the fields that identify ownership (account_id, user_id) and the
 * size/mtime (blocks, size_kib, mtime) are written; everything else comes from
 * the source save's metadata or sensible defaults.
 *
 * Uses embedded SQLite 3.46.0 (public domain) compiled with
 * SQLITE_OMIT_LOAD_EXTENSION, SQLITE_THREADSAFE=0, SQLITE_OMIT_WAL.
 * The DB is opened in serialised exclusive mode so no concurrent reader sees
 * a partial write.
 */

#pragma once
#include <stdint.h>
#include <stddef.h>

/* Description of a single save slot for the DB row */
typedef struct {
    const char *title_id;       /* "PPSA12345" or "CUSA12345"       */
    const char *dir_name;       /* slot name, e.g. "default"        */
    const char *main_title;     /* game title string                */
    uint64_t    account_id;     /* target user's account ID         */
    uint32_t    user_id;        /* target user's numeric uid        */
    uint64_t    blocks;         /* image size / 0x40000             */
    /* mtime in "2026-10-21T12:00:00.00Z" format; filled by caller  */
    char        mtime[32];
} savedb_row_t;

/* Insert or replace the row for (title_id, dir_name) in the given database.
 *   db_path   full path to savedata.db
 *   row       the fields to write
 * Returns 0 on success, -1 on error (message printed to the ps5tm log). */
int savedb_upsert(const char *db_path, const savedb_row_t *row);
