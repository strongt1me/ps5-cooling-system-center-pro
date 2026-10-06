/* Read-only access to rowid tables in a SQLite 3 file — without SQLite.
 *
 * The home screen draws its tiles from the system's app database,
 * /system_data/priv/mms/app.db, and the games page shows exactly that list.
 * Reading it the usual way is closed off twice: the payload SDK carries no
 * SQLite, and loading the system's own library at run time is what froze
 * this app before (see gamestate.c). Embedding the SQLite amalgamation would
 * add most of a megabyte for a read of two tables.
 *
 * The file format is documented and stable (sqlite.org/fileformat2.html),
 * and reading a rowid table needs only a small part of it:
 *
 *   header      page size (offset 16), reserved bytes per page (20), text
 *               encoding (56) — only UTF-8 is accepted
 *   b-tree      page type 0x05 interior, 0x0D leaf; page 1 carries the
 *               100-byte file header before its own
 *   cell        varint payload size, varint rowid, the payload, and past a
 *               size threshold a chain of overflow pages
 *   record      varint header size, one varint serial type per column, then
 *               the values
 *   schema      sqlite_master on page 1: type, name, tbl_name, rootpage, sql;
 *               the column names come from the CREATE TABLE text
 *
 * Nothing here writes, locks or reads a journal. The system rewrites the file
 * while the shell runs, so a read may catch it halfway: every offset taken
 * from the file is checked before use, a page is visited at most once per
 * page in the file, and anything that does not add up ends the scan with -1
 * rather than a wrong row. The caller retries.
 *
 * No system calls and no ps5tm.h, so this file can be built on its own and
 * checked against the real app.db from a console. */

#include <stdlib.h>
#include <string.h>

#include "sqlite_ro.h"

#define MAX_DEPTH    24           /* a b-tree of this file is 2-3 deep      */
#define MAX_PAYLOAD  (1u << 20)   /* one record; the app tables stay far below */


static uint32_t
be16(const unsigned char *p) {
  return (uint32_t)p[0] << 8 | p[1];
}

static uint32_t
be32(const unsigned char *p) {
  return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 |
         (uint32_t)p[2] << 8  | p[3];
}

/* One to nine bytes, seven bits each, big end first; the ninth byte counts
   all eight. Returns the bytes used, 0 if it ran past avail. */
static size_t
varint(const unsigned char *p, size_t avail, uint64_t *out) {
  uint64_t v = 0;
  for(size_t i = 0; i < 9; i++) {
    if(i >= avail) return 0;
    if(i == 8) { *out = (v << 8) | p[i]; return 9; }
    v = (v << 7) | (p[i] & 0x7F);
    if(!(p[i] & 0x80)) { *out = v; return i + 1; }
  }
  return 0;
}

static const unsigned char *
page_at(const sqlro_db_t *db, uint32_t pgno) {
  if(pgno == 0 || pgno > db->pages) return NULL;
  return db->buf + (size_t)(pgno - 1) * db->page_size;
}


int
sqlro_open_mem(sqlro_db_t *db, const unsigned char *buf, size_t size) {
  memset(db, 0, sizeof(*db));
  /* The literal is 15 characters and its terminating NUL: "SQLite format 3\0". */
  if(!buf || size < 512 || memcmp(buf, "SQLite format 3", 16) != 0) return -1;

  uint32_t ps = be16(buf + 16);
  if(ps == 1) ps = 65536;
  if(ps < 512 || ps > 65536 || (ps & (ps - 1))) return -1;

  uint32_t reserved = buf[20];
  if(ps - reserved < 480) return -1;

  /* Text is handed out as UTF-8. The app database is UTF-8 (measured); one in
     UTF-16 would need converting and is refused instead. 0 = not yet set. */
  uint32_t enc = be32(buf + 56);
  if(enc != 0 && enc != 1) return -1;

  db->buf       = buf;
  db->size      = size;
  db->page_size = ps;
  db->usable    = ps - reserved;
  db->pages     = (uint32_t)(size / ps);
  return db->pages ? 0 : -1;
}


/* How much of a payload sits in the cell itself; the rest is in overflow
   pages. Straight from the file format: X, M and K as named there. */
static uint32_t
local_payload(uint32_t usable, uint64_t total) {
  uint32_t x = usable - 35;
  if(total <= x) return (uint32_t)total;
  uint32_t m = ((usable - 12) * 32 / 255) - 23;
  uint32_t k = m + (uint32_t)((total - m) % (usable - 4));
  return k <= x ? k : m;
}


static int
parse_record(const unsigned char *rec, size_t n, sqlro_row_t *row) {
  uint64_t hsize;
  size_t   p = varint(rec, n, &hsize);
  if(!p || hsize < p || hsize > n) return -1;

  size_t body = (size_t)hsize;
  int    col  = 0;
  while(p < hsize && col < SQLRO_MAX_COLS) {
    uint64_t st;
    size_t   k = varint(rec + p, (size_t)hsize - p, &st);
    if(!k) return -1;
    p += k;

    size_t  sz   = 0;
    uint8_t kind = SQLRO_INT;
    int64_t iv   = 0;
    switch(st) {
      case 0:  kind = SQLRO_NULL; break;
      case 1:  sz = 1; break;
      case 2:  sz = 2; break;
      case 3:  sz = 3; break;
      case 4:  sz = 4; break;
      case 5:  sz = 6; break;
      case 6:  sz = 8; break;
      case 7:  sz = 8; kind = SQLRO_FLOAT; break;
      case 8:  iv = 0; break;
      case 9:  iv = 1; break;
      case 10:
      case 11: return -1;                   /* reserved, never written */
      default:
        sz   = (size_t)((st - 12) / 2);
        kind = (st & 1) ? SQLRO_TEXT : SQLRO_BLOB;
        break;
    }
    if(sz > n - body) return -1;

    if((kind == SQLRO_INT || kind == SQLRO_FLOAT) && sz) {
      uint64_t u = 0;
      for(size_t i = 0; i < sz; i++) u = (u << 8) | rec[body + i];
      /* Integers are two's complement in 1-8 bytes; widen the sign. */
      if(kind == SQLRO_INT && sz < 8 && (rec[body] & 0x80))
        u |= ~(uint64_t)0 << (sz * 8);
      iv = (int64_t)u;
    }

    row->kind[col] = kind;
    row->data[col] = rec + body;
    row->len[col]  = (uint32_t)sz;
    row->ival[col] = iv;
    body += sz;
    col++;
  }
  row->ncols = col;
  return 0;
}


typedef struct {
  const sqlro_db_t    *db;
  const sqlro_table_t *t;
  sqlro_row_fn         fn;
  void                *ctx;
  uint32_t             budget;   /* pages left to visit; bounds a cycle */
} walk_t;

static int
emit_cell(walk_t *w, const unsigned char *pg, uint32_t off) {
  const sqlro_db_t *db  = w->db;
  uint32_t          lim = db->usable;
  if(off >= lim) return -1;

  uint64_t total, rowid;
  size_t k = varint(pg + off, lim - off, &total);
  if(!k) return -1;
  off += (uint32_t)k;
  k = varint(pg + off, lim - off, &rowid);
  if(!k) return -1;
  off += (uint32_t)k;
  if(total > MAX_PAYLOAD) return -1;

  uint32_t local = local_payload(lim, total);
  if(local > lim - off) return -1;

  const unsigned char *rec  = pg + off;
  unsigned char       *heap = NULL;
  if(local < total) {
    if(lim - off - local < 4) return -1;
    uint32_t next = be32(pg + off + local);
    heap = malloc((size_t)total);
    if(!heap) return -1;
    memcpy(heap, rec, local);
    size_t   have = local;
    uint32_t hops = 0;
    while(have < total) {
      const unsigned char *op = page_at(db, next);
      if(!op || ++hops > db->pages) { free(heap); return -1; }
      size_t chunk = (size_t)total - have;
      if(chunk > lim - 4) chunk = lim - 4;
      memcpy(heap + have, op + 4, chunk);
      have += chunk;
      next  = be32(op);
    }
    rec = heap;
  }

  sqlro_row_t row;
  memset(&row, 0, sizeof(row));
  row.rowid = (int64_t)rowid;
  int rc = parse_record(rec, (size_t)total, &row);
  if(rc == 0) {
    /* An INTEGER PRIMARY KEY is the rowid itself; the record keeps NULL. */
    int ipk = w->t->ipk;
    if(ipk >= 0 && ipk < row.ncols && row.kind[ipk] == SQLRO_NULL) {
      row.kind[ipk] = SQLRO_INT;
      row.ival[ipk] = row.rowid;
    }
    rc = w->fn(w->ctx, w->t, &row) ? 1 : 0;
  }
  free(heap);
  return rc;
}

static int
walk(walk_t *w, uint32_t pgno, int depth) {
  const unsigned char *pg = page_at(w->db, pgno);
  if(!pg || depth > MAX_DEPTH || w->budget == 0) return -1;
  w->budget--;

  uint32_t hdr   = pgno == 1 ? 100 : 0;
  uint32_t lim   = w->db->usable;
  uint32_t type  = pg[hdr];
  uint32_t ncell = be16(pg + hdr + 3);

  if(type == 0x0D) {                                  /* table leaf */
    uint32_t cp = hdr + 8;
    if(cp + 2 * ncell > lim) return -1;
    for(uint32_t i = 0; i < ncell; i++) {
      int rc = emit_cell(w, pg, be16(pg + cp + 2 * i));
      if(rc) return rc;
    }
    return 0;
  }

  if(type == 0x05) {                                  /* table interior */
    uint32_t cp = hdr + 12;
    if(cp + 2 * ncell > lim) return -1;
    for(uint32_t i = 0; i < ncell; i++) {
      uint32_t off = be16(pg + cp + 2 * i);
      if(off > lim - 4) return -1;
      int rc = walk(w, be32(pg + off), depth + 1);
      if(rc) return rc;
    }
    return walk(w, be32(pg + hdr + 8), depth + 1);   /* right-most child */
  }

  return -1;                    /* an index page, a free page, or torn */
}

int
sqlro_scan(const sqlro_db_t *db, const sqlro_table_t *t,
           sqlro_row_fn fn, void *ctx) {
  if(!db || !db->buf || !t || !fn) return -1;
  walk_t w = { db, t, fn, ctx, db->pages };
  return walk(&w, t->root, 0);
}


/* ------------------------------------------------------------------ schema */

static int
is_ident(unsigned char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9') || c == '_' || c == '$' || c >= 0x80;
}

static int
is_space(char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static char
upper(char c) {
  return (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
}

/* p[0..n) equals an upper-case word, ignoring case. */
static int
word_is(const char *p, size_t n, const char *word) {
  size_t wl = strlen(word);
  if(n != wl) return 0;
  for(size_t i = 0; i < n; i++) if(upper(p[i]) != word[i]) return 0;
  return 1;
}

/* An upper-case needle anywhere in [p, end), ignoring case. */
static int
contains(const char *p, const char *end, const char *needle) {
  size_t nl = strlen(needle);
  for(; p + nl <= end; p++) {
    size_t i = 0;
    while(i < nl && upper(p[i]) == needle[i]) i++;
    if(i == nl) return 1;
  }
  return 0;
}

static void
add_column(sqlro_table_t *t, const char *def, const char *end) {
  while(def < end && is_space(*def)) def++;
  if(def >= end) return;

  const char *name = def;
  size_t      n;
  if(*def == '"' || *def == '`' || *def == '[') {
    char close = *def == '[' ? ']' : *def;
    name = ++def;
    while(def < end && *def != close) def++;
    n = (size_t)(def - name);
    if(def < end) def++;
  } else {
    while(def < end && is_ident((unsigned char)*def)) def++;
    n = (size_t)(def - name);
    /* A table constraint, not a column. */
    if(word_is(name, n, "CONSTRAINT") || word_is(name, n, "PRIMARY") ||
       word_is(name, n, "UNIQUE") || word_is(name, n, "CHECK") ||
       word_is(name, n, "FOREIGN"))
      return;
  }
  if(n == 0 || t->ncols >= SQLRO_MAX_COLS) return;

  if(n >= SQLRO_NAME_LEN) n = SQLRO_NAME_LEN - 1;
  memcpy(t->col[t->ncols], name, n);
  t->col[t->ncols][n] = 0;

  /* INTEGER PRIMARY KEY turns the column into the rowid itself. */
  while(def < end && is_space(*def)) def++;
  const char *type = def;
  while(def < end && is_ident((unsigned char)*def)) def++;
  if(word_is(type, (size_t)(def - type), "INTEGER") &&
     contains(def, end, "PRIMARY KEY"))
    t->ipk = t->ncols;

  t->ncols++;
}

/* The column names out of "CREATE TABLE x (a TEXT, b INT, ...)". Commas
   inside quotes or parentheses do not split. */
static int
parse_columns(const char *sql, size_t len, sqlro_table_t *t) {
  const char *end  = sql + len;
  const char *open = memchr(sql, '(', len);
  if(!open) return -1;
  const char *close = end;
  while(close > open + 1 && close[-1] != ')') close--;
  if(close <= open + 1) return -1;
  close--;                                           /* at the last ')' */
  if(contains(close, end, "WITHOUT ROWID")) return -1;

  t->ncols = 0;
  t->ipk   = -1;
  int         depth = 0;
  const char *start = open + 1;
  for(const char *p = open + 1; p < close; p++) {
    char c = *p;
    if(c == '\'' || c == '"' || c == '`') {
      for(p++; p < close; p++) {
        if(*p != c) continue;
        if(p + 1 < close && p[1] == c) { p++; continue; }   /* doubled */
        break;
      }
      continue;
    }
    if(c == '[') {
      while(p < close && *p != ']') p++;
      continue;
    }
    if(c == '(') depth++;
    else if(c == ')') depth--;
    else if(c == ',' && depth == 0) { add_column(t, start, p); start = p + 1; }
  }
  add_column(t, start, close);
  return t->ncols > 0 ? 0 : -1;
}


typedef struct {
  const char    *want;
  size_t         want_len;
  int            prefix;
  sqlro_table_t *out;           /* exact lookup */
  char          *name;          /* prefix lookup */
  size_t         name_len;
  int            found;
} find_t;

static int
find_cb(void *ctx, const sqlro_table_t *t, const sqlro_row_t *r) {
  find_t *f = ctx;
  (void)t;
  if(r->ncols < 5 || r->kind[0] != SQLRO_TEXT || r->kind[1] != SQLRO_TEXT)
    return 0;
  if(r->len[0] != 5 || memcmp(r->data[0], "table", 5) != 0) return 0;

  if(f->prefix) {
    if(r->len[1] < f->want_len ||
       memcmp(r->data[1], f->want, f->want_len) != 0)
      return 0;
    size_t n = r->len[1] < f->name_len ? r->len[1] : f->name_len - 1;
    memcpy(f->name, r->data[1], n);
    f->name[n] = 0;
    f->found = 1;
    return 1;
  }

  if(r->len[1] != f->want_len || memcmp(r->data[1], f->want, f->want_len) != 0)
    return 0;
  if(r->kind[3] != SQLRO_INT || r->ival[3] <= 0 || r->kind[4] != SQLRO_TEXT)
    return 1;                                  /* found, but unusable */
  memset(f->out, 0, sizeof(*f->out));
  if(parse_columns((const char *)r->data[4], r->len[4], f->out) != 0) return 1;
  f->out->root = (uint32_t)r->ival[3];
  f->found = 1;
  return 1;
}

static int
scan_master(const sqlro_db_t *db, find_t *f) {
  /* sqlite_master is a table like any other, rooted on page 1. */
  sqlro_table_t *master = malloc(sizeof(*master));
  if(!master) return -1;
  memset(master, 0, sizeof(*master));
  master->root  = 1;
  master->ncols = 5;
  master->ipk   = -1;
  int rc = sqlro_scan(db, master, find_cb, f);
  free(master);
  return (rc >= 0 && f->found) ? 0 : -1;
}

int
sqlro_find_table(const sqlro_db_t *db, const char *name, sqlro_table_t *out) {
  if(!db || !name || !out) return -1;
  find_t f = { name, strlen(name), 0, out, NULL, 0, 0 };
  return scan_master(db, &f);
}

int
sqlro_find_table_prefix(const sqlro_db_t *db, const char *prefix,
                        char *name, size_t name_len) {
  if(!db || !prefix || !name || name_len < 2) return -1;
  find_t f = { prefix, strlen(prefix), 1, NULL, name, name_len, 0 };
  return scan_master(db, &f);
}

int
sqlro_col(const sqlro_table_t *t, const char *name) {
  size_t n = strlen(name);
  for(int i = 0; i < t->ncols; i++) {
    const char *c = t->col[i];
    size_t j = 0;
    while(j < n && c[j] && upper(c[j]) == upper(name[j])) j++;
    if(j == n && c[j] == 0) return i;
  }
  return -1;
}


/* ------------------------------------------------------------------ values */

int
sqlro_text(const sqlro_row_t *r, int col, char *out, size_t out_len) {
  if(!out || out_len == 0) return -1;
  out[0] = 0;
  if(col < 0 || col >= r->ncols) return -1;

  if(r->kind[col] == SQLRO_TEXT || r->kind[col] == SQLRO_BLOB) {
    size_t n = r->len[col];
    if(n >= out_len) {
      /* Cut before the character it would split, so a name never ends in
         half a "™". */
      n = out_len - 1;
      while(n > 0 && (r->data[col][n] & 0xC0) == 0x80) n--;
    }
    memcpy(out, r->data[col], n);
    out[n] = 0;
    return 0;
  }

  if(r->kind[col] == SQLRO_INT) {
    char     tmp[24];
    size_t   i   = 0;
    int64_t  v   = r->ival[col];
    uint64_t mag = v < 0 ? (uint64_t)0 - (uint64_t)v : (uint64_t)v;
    do { tmp[i++] = (char)('0' + mag % 10); mag /= 10; } while(mag);
    if(v < 0) tmp[i++] = '-';
    if(i >= out_len) return -1;
    for(size_t j = 0; j < i; j++) out[j] = tmp[i - 1 - j];
    out[i] = 0;
    return 0;
  }
  return -1;
}

int
sqlro_int(const sqlro_row_t *r, int col, int64_t *out) {
  if(!out || col < 0 || col >= r->ncols) return -1;
  switch(r->kind[col]) {
    case SQLRO_INT:
      *out = r->ival[col];
      return 0;

    case SQLRO_FLOAT: {
      uint64_t bits = (uint64_t)r->ival[col];
      double   d;
      memcpy(&d, &bits, sizeof(d));
      if(!(d > -9.2e18 && d < 9.2e18)) return -1;     /* also NaN */
      *out = (int64_t)d;
      return 0;
    }

    case SQLRO_TEXT: {
      const unsigned char *p = r->data[col];
      size_t n = r->len[col], i = 0;
      int neg = 0;
      if(i < n && p[i] == '-') { neg = 1; i++; }
      if(i == n) return -1;
      uint64_t v = 0;
      for(; i < n; i++) {
        if(p[i] < '0' || p[i] > '9') return -1;
        uint64_t d = (uint64_t)(p[i] - '0');
        if(v > (UINT64_MAX - d) / 10) return -1;
        v = v * 10 + d;
      }
      if(v > (uint64_t)INT64_MAX) return -1;
      *out = neg ? -(int64_t)v : (int64_t)v;
      return 0;
    }

    default:
      return -1;
  }
}
