/* Reads rowid tables out of a SQLite 3 database held in memory — read only,
 * without SQLite. See sqlite_ro.c for why and how.
 *
 * Deliberately free of ps5tm.h and of any system call: the file is loaded by
 * the caller, and this part can be built and checked on its own against the
 * real app.db from a console. */

#ifndef PS5TM_SQLITE_RO_H
#define PS5TM_SQLITE_RO_H

#include <stddef.h>
#include <stdint.h>

/* The widest table the app reads has 88 columns (tbl_iconinfo_*, FW 12.00).
   Columns past this limit are skipped, never misread. */
#define SQLRO_MAX_COLS  128
#define SQLRO_NAME_LEN  48

enum { SQLRO_NULL = 0, SQLRO_INT, SQLRO_FLOAT, SQLRO_TEXT, SQLRO_BLOB };

typedef struct {
  const unsigned char *buf;
  size_t               size;
  uint32_t             page_size;
  uint32_t             usable;      /* page size minus the reserved tail   */
  uint32_t             pages;
} sqlro_db_t;

typedef struct {
  uint32_t root;
  int      ncols;
  int      ipk;                     /* INTEGER PRIMARY KEY column, or -1   */
  char     col[SQLRO_MAX_COLS][SQLRO_NAME_LEN];
} sqlro_table_t;

typedef struct {
  int64_t              rowid;
  int                  ncols;       /* columns stored in this record       */
  uint8_t              kind[SQLRO_MAX_COLS];
  const unsigned char *data[SQLRO_MAX_COLS];
  uint32_t             len[SQLRO_MAX_COLS];
  int64_t              ival[SQLRO_MAX_COLS];  /* INT value; FLOAT bits    */
} sqlro_row_t;

/* Return 0 to go on, 1 to stop the scan. */
typedef int (*sqlro_row_fn)(void *ctx, const sqlro_table_t *t,
                            const sqlro_row_t *row);

/* All return 0 on success and -1 on anything unexpected; sqlro_scan also 1
   when the callback stopped it. Nothing here allocates more than one
   record's worth of memory, and every offset from the file is checked. */
int  sqlro_open_mem(sqlro_db_t *db, const unsigned char *buf, size_t size);
/* out is 6 KB; callers on a thread stack should allocate it. */
int  sqlro_find_table(const sqlro_db_t *db, const char *name,
                      sqlro_table_t *out);
/* The first table whose name starts with prefix, by name only. */
int  sqlro_find_table_prefix(const sqlro_db_t *db, const char *prefix,
                             char *name, size_t name_len);
int  sqlro_col(const sqlro_table_t *t, const char *name);
int  sqlro_scan(const sqlro_db_t *db, const sqlro_table_t *t,
                sqlro_row_fn fn, void *ctx);

/* Text or blob as a string, an integer in decimal. -1 for NULL or absent. */
int  sqlro_text(const sqlro_row_t *r, int col, char *out, size_t out_len);
/* An integer, or text that is one ("138"). -1 otherwise. */
int  sqlro_int(const sqlro_row_t *r, int col, int64_t *out);

#endif
