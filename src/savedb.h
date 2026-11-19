/* PS5 Cooling & System Center - Pro: the entry of a save in the console's save database (savedb.h, 19.11.2026).
 *
 * The console lists the saves of a user from <system_data>/savedata_prospero/<user>/db/user/savedata.db (SQLite, rollback journal, user_version 11 on firmware 12.00;
 * table "savedata", one row per save slot). A save image that is put into the user's folder is only complete when it has its row there. The row is written with the
 * embedded SQLite (third_party/sqlite3), never by hand, and only in two ways: the row of the slot is UPDATEd when it exists, INSERTed when it does not. Before anything
 * is written the file is copied (savedb_backup), after it the database is checked (savedb_check) - a database that fails the check is put back from the copy.
 *
 * The database of a user that has no save yet may not exist: it is then made with the schema the console itself uses. */
#ifndef PS5TM_SAVEDB_H
#define PS5TM_SAVEDB_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
  char     title_id[16];
  char     dir_name[100];          /* the slot */
  char     main_title[256];
  char     sub_title[256];
  char     detail[1024];
  char     mtime[40];              /* 2026-11-19T07:34:00.00Z */
  uint64_t account_id;             /* the console's account id of the user (0 when it has none) */
  uint32_t user_id;                /* the local user id (the 8 hex digits of /user/home/<uid>) */
  uint32_t user_param;             /* SAVEDATA_LIST_PARAM of the save's param.sfo */
  uint64_t blocks;                 /* the image in blocks */
  uint64_t free_blocks;
  uint64_t size_kib;               /* the image in KiB */
  uint64_t system_blocks;
} savedb_row_t;

/* The slot's row: UPDATEd when there is one (only the fields that describe the save and its owner), else INSERTed. *inserted tells which. 0 or -1 (err says why).
   The database is made with the console's schema when the file does not exist; a file with another schema is refused, untouched. */
int savedb_upsert(const char *db_path, const savedb_row_t *row, int *inserted, char *err, size_t err_len);

/* A consistent copy of the database into dest_path (a new file; SQLite's own backup, so a database that is open elsewhere is copied whole). 0 or -1. */
int savedb_backup(const char *db_path, const char *dest_path, char *err, size_t err_len);

/* PRAGMA quick_check: 0 when it says "ok". */
int savedb_check(const char *db_path, char *err, size_t err_len);

/* The rows of a slot (0 or 1 normally): -1 on an error. For tests and for "does this slot exist". */
int savedb_has_slot(const char *db_path, const char *title_id, const char *dir_name, char *err, size_t err_len);

#endif
