/* The games list's memory between scans, on the drive (04.10.2026).
 *
 * Opening the games page costs a read of the system's app database and, for every
 * title, a look at the drive it lives on: a dump's folder is walked to add up its size,
 * eboot.bin and fakelib are read to tell a backport. library.c remembers all of it in
 * memory — until the app restarts; a drive that is not plugged in leaves nothing to
 * look at; and every cover is read from where the system keeps it, again and again.
 *
 * With "Covers & Metadaten speichern" switched on (config.library_cache) this module
 * keeps, per title, in /data/PS5-Cooling-Center/covers_and_more/<TITLE>/ :
 *
 *   icon0.png   a copy of the cover the database names
 *   meta.json   what the scans found out about the title, in plain JSON a person can read
 *
 * library.c seeds its in-memory caches from the meta files at the first scan after a
 * start, writes back what changed after each scan, and serves covers from the copies.
 * A title that appears in the list is stored with its next scan. Nothing is deleted when
 * a game is: the folder is only emptied on request ("leeren" on the page) and then only of
 * what this module made — folders named like a title id holding meta.json, icon0.png and
 * their temporary files; a folder or file of any other name is left alone.
 *
 * What is trusted, and for how long, is library.c's business: it validates a title's meta
 * the way it validates its own memory (the keys of eboot.bin and fakelib, the mount source,
 * the age of a size). This module only stores, hands out and never interprets.
 *
 * Writes go to a temporary file and are renamed, so a power cut leaves the old file or the
 * new one. The files are 0644 like the rest of the app's data folder. */

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "ps5tm.h"
#include "third_party/cJSON.h"

#ifndef PS5TM_LIBCACHE_DIR
#define PS5TM_LIBCACHE_DIR PS5TM_DATA_DIR "/covers_and_more"
#endif

#define LC_MAX_TITLES  256
#define LC_MAX_COVER   (6u << 20)            /* a cover larger than this is not copied     */
#define LC_MAX_TOTAL   (160ull << 20)        /* the covers together stay below this        */
#define LC_META_MAX    (64u * 1024)          /* a meta file larger than this is not read   */
#define LC_META_FORMAT 1

typedef struct {
  ps5tm_libmeta_t m;
  uint64_t        hash;                      /* of the text last written, or read           */
  int             used;
  int             on_disk;                   /* meta.json is there with that text           */
} entry_t;

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;   /* the table                   */
static pthread_mutex_t g_io   = PTHREAD_MUTEX_INITIALIZER;   /* one write at a time; taken first */
static entry_t        *g_tab;                                /* LC_MAX_TITLES, on first use */


/* ------------------------------------------------------------------ small things */

int
ps5tm_libcache_enabled(void) {
  ps5tm_config_lock();
  int on = g_config.library_cache != 0;
  ps5tm_config_unlock();
  return on;
}

static int
is_tid(const char *s) {
  if(!s || strlen(s) != 9) return 0;
  for(int i = 0; i < 4; i++) if(s[i] < 'A' || s[i] > 'Z') return 0;
  for(int i = 4; i < 9; i++) if(s[i] < '0' || s[i] > '9') return 0;
  return 1;
}

static uint64_t
fnv1a(const char *p, size_t n) {
  uint64_t h = 1469598103934665603ull;
  for(size_t i = 0; i < n; i++) { h ^= (unsigned char)p[i]; h *= 1099511628211ull; }
  return h;
}

/* Copies a string into a field of fixed size, always terminated. */
#define SET(dst, src) snprintf((dst), sizeof(dst), "%s", (src))

static entry_t *
table(void) {
  if(!g_tab) g_tab = calloc(LC_MAX_TITLES, sizeof(*g_tab));
  return g_tab;
}

/* Caller holds g_lock. */
static entry_t *
find_locked(const char *tid, int create) {
  entry_t *t = table();
  if(!t) return NULL;
  entry_t *free_slot = NULL;
  for(int i = 0; i < LC_MAX_TITLES; i++) {
    if(t[i].used) {
      if(!strcmp(t[i].m.title_id, tid)) return &t[i];
    } else if(!free_slot) {
      free_slot = &t[i];
    }
  }
  if(!create || !free_slot) return NULL;
  memset(free_slot, 0, sizeof(*free_slot));
  free_slot->used = 1;
  SET(free_slot->m.title_id, tid);
  free_slot->m.platform = -1;
  return free_slot;
}

static void
title_dir(const char *tid, char *out, size_t n) {
  snprintf(out, n, "%s/%s", PS5TM_LIBCACHE_DIR, tid);
}

/* Is this title's folder a real folder (lstat: not a link, which would lead somewhere that is not ours)? */
static int
is_real_dir(const char *path) {
  struct stat st;
  return lstat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

/* The folder was first called covers_and_moore, a slip of 04.10.2026. One that is still there under that name is renamed,
   once, when nothing of the new name exists: it is the same cache, and nothing is copied or lost. If both are there the old one
   is left alone (the app deletes nothing it has not just made). */
#ifndef PS5TM_LIBCACHE_OLD_DIR
#define PS5TM_LIBCACHE_OLD_DIR PS5TM_DATA_DIR "/covers_and_moore"
#endif
static pthread_once_t g_once = PTHREAD_ONCE_INIT;

static void
migrate_old_dir(void) {
  struct stat st;
  if(lstat(PS5TM_LIBCACHE_DIR, &st) == 0) return;
  if(lstat(PS5TM_LIBCACHE_OLD_DIR, &st) != 0 || !S_ISDIR(st.st_mode)) return;
  if(rename(PS5TM_LIBCACHE_OLD_DIR, PS5TM_LIBCACHE_DIR) == 0)
    PS5TM_INFO("libcache_renamed", "Covers & Metadaten: der Ordner covers_and_moore heißt jetzt covers_and_more.");
}

static int
make_dirs(const char *tid) {
  char d[PATH_MAX];
  pthread_once(&g_once, migrate_old_dir);
  mkdir(PS5TM_DATA_DIR, 0755);
  if(mkdir(PS5TM_LIBCACHE_DIR, 0755) != 0 && errno != EEXIST) return -1;
  title_dir(tid, d, sizeof(d));
  if(mkdir(d, 0755) != 0 && errno != EEXIST) return -1;
  return is_real_dir(d) ? 0 : -1;                    /* a link or a file of that name: nothing is written there */
}


/* ------------------------------------------------------------------ meta.json */

static cJSON *
meta_object(const ps5tm_libmeta_t *m) {
  cJSON *o = cJSON_CreateObject();
  if(!o) return NULL;
  cJSON_AddNumberToObject(o, "format", LC_META_FORMAT);
  cJSON_AddStringToObject(o, "title_id", m->title_id);
  if(m->content_id[0]) cJSON_AddStringToObject(o, "content_id", m->content_id);
  if(m->name[0])       cJSON_AddStringToObject(o, "name", m->name);
  if(m->version[0])    cJSON_AddStringToObject(o, "version", m->version);
  cJSON_AddStringToObject(o, "platform", m->platform == 0 ? "PS5" : m->platform == 1 ? "PS4" : "");
  if(m->source[0])     cJSON_AddStringToObject(o, "source", m->source);
  if(m->format[0] || m->real_path[0]) {
    cJSON *s = cJSON_AddObjectToObject(o, "storage");
    cJSON_AddStringToObject(s, "format", m->format);
    cJSON_AddStringToObject(s, "path", m->real_path);
  }
  if(m->mods_checked) {
    cJSON *md = cJSON_AddObjectToObject(o, "mods");
    cJSON_AddBoolToObject(md, "backport", m->backport);
    cJSON_AddBoolToObject(md, "ampr_emu", m->ampr);
    cJSON_AddBoolToObject(md, "playgo", m->playgo);
    cJSON_AddNumberToObject(md, "extra_libs", m->extra_libs);
    cJSON_AddNumberToObject(md, "eboot_sdk", (double)m->eboot_sdk);
    cJSON *libs = cJSON_AddArrayToObject(md, "fakelib");
    for(int i = 0; i < m->nlibs && i < 6; i++) cJSON_AddItemToArray(libs, cJSON_CreateString(m->libs[i]));
    cJSON_AddNumberToObject(md, "fakelib_more", m->more_libs);
    cJSON *k = cJSON_AddObjectToObject(md, "keys");
    cJSON_AddNumberToObject(k, "eboot_mtime", (double)m->eboot_mtime);
    cJSON_AddNumberToObject(k, "eboot_size", (double)m->eboot_size);
    cJSON_AddNumberToObject(k, "fakelib_mtime", (double)m->fakelib_mtime);
  }
  if(m->size_path[0] && m->size >= 0) {
    cJSON *s = cJSON_AddObjectToObject(o, "size");
    cJSON_AddNumberToObject(s, "bytes", (double)m->size);
    cJSON_AddStringToObject(s, "path", m->size_path);
    cJSON_AddNumberToObject(s, "measured", (double)m->size_at);
  }
  if(m->cover_bytes > 0) {
    cJSON *c = cJSON_AddObjectToObject(o, "cover");
    cJSON_AddStringToObject(c, "ts", m->cover_ts);
    cJSON_AddNumberToObject(c, "bytes", (double)m->cover_bytes);
  }
  return o;
}

static const char *
jstr(const cJSON *o, const char *k) {
  const cJSON *v = cJSON_GetObjectItem(o, k);
  return cJSON_IsString(v) ? v->valuestring : "";
}

static int64_t
jnum(const cJSON *o, const char *k, int64_t dflt) {
  const cJSON *v = cJSON_GetObjectItem(o, k);
  if(!cJSON_IsNumber(v) || v->valuedouble < -9.0e15 || v->valuedouble > 9.0e15) return dflt;
  return (int64_t)v->valuedouble;
}

/* A meta file back into a record. 0, or -1 for one that is not ours or not whole. */
static int
parse_meta(const char *txt, const char *tid, ps5tm_libmeta_t *m) {
  cJSON *o = cJSON_Parse(txt);
  if(!o) return -1;
  int rc = -1;
  if(jnum(o, "format", 0) == LC_META_FORMAT && !strcmp(jstr(o, "title_id"), tid)) {
    memset(m, 0, sizeof(*m));
    SET(m->title_id, tid);
    SET(m->content_id, jstr(o, "content_id"));
    SET(m->name, jstr(o, "name"));
    SET(m->version, jstr(o, "version"));
    const char *pl = jstr(o, "platform");
    m->platform = !strcmp(pl, "PS5") ? 0 : !strcmp(pl, "PS4") ? 1 : -1;
    SET(m->source, jstr(o, "source"));
    const cJSON *s = cJSON_GetObjectItem(o, "storage");
    if(cJSON_IsObject(s)) { SET(m->format, jstr(s, "format")); SET(m->real_path, jstr(s, "path")); }
    const cJSON *md = cJSON_GetObjectItem(o, "mods");
    if(cJSON_IsObject(md)) {
      m->mods_checked = 1;
      m->backport   = cJSON_IsTrue(cJSON_GetObjectItem(md, "backport"));
      m->ampr       = cJSON_IsTrue(cJSON_GetObjectItem(md, "ampr_emu"));
      m->playgo     = cJSON_IsTrue(cJSON_GetObjectItem(md, "playgo"));
      m->extra_libs = (int)jnum(md, "extra_libs", 0);
      m->eboot_sdk  = (uint32_t)jnum(md, "eboot_sdk", 0);
      m->more_libs  = (int)jnum(md, "fakelib_more", 0);
      const cJSON *libs = cJSON_GetObjectItem(md, "fakelib"), *l;
      cJSON_ArrayForEach(l, libs) {
        if(m->nlibs >= 6) break;
        if(cJSON_IsString(l)) SET(m->libs[m->nlibs++], l->valuestring);
      }
      const cJSON *k = cJSON_GetObjectItem(md, "keys");
      if(cJSON_IsObject(k)) {
        m->eboot_mtime   = jnum(k, "eboot_mtime", -1);
        m->eboot_size    = jnum(k, "eboot_size", -1);
        m->fakelib_mtime = jnum(k, "fakelib_mtime", -1);
      } else {
        m->eboot_mtime = m->eboot_size = m->fakelib_mtime = -1;
      }
    }
    const cJSON *sz = cJSON_GetObjectItem(o, "size");
    m->size = -1;
    if(cJSON_IsObject(sz)) {
      m->size    = jnum(sz, "bytes", -1);
      m->size_at = jnum(sz, "measured", 0);
      SET(m->size_path, jstr(sz, "path"));
      if(m->size < 0) m->size_path[0] = 0;
    }
    const cJSON *cv = cJSON_GetObjectItem(o, "cover");
    if(cJSON_IsObject(cv)) {
      SET(m->cover_ts, jstr(cv, "ts"));
      m->cover_bytes = jnum(cv, "bytes", 0);
      if(m->cover_bytes < 0 || m->cover_bytes > (int64_t)LC_MAX_COVER) m->cover_bytes = 0;
    }
    rc = 0;
  }
  cJSON_Delete(o);
  return rc;
}

/* The text of a record's meta file: the one form both the comparison with what is on the disk
   and the file itself use. Caller frees. */
static char *
meta_text(const ps5tm_libmeta_t *m) {
  cJSON *o = meta_object(m);
  char  *txt = o ? cJSON_Print(o) : NULL;
  cJSON_Delete(o);
  return txt;
}

/* Writes a title's meta.json through a temporary file. Caller holds g_io, not g_lock. 0, or -1. */
static int
write_text(const char *tid, const char *txt) {
  size_t len = strlen(txt);
  if(make_dirs(tid) != 0) return -1;
  char dir[PATH_MAX], path[PATH_MAX], tmp[PATH_MAX];
  title_dir(tid, dir, sizeof(dir));
  snprintf(path, sizeof(path), "%s/meta.json", dir);
  snprintf(tmp, sizeof(tmp), "%s/meta.json.tmp", dir);
  int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if(fd < 0) return -1;
  size_t off = 0;
  int ok = 1;
  while(off < len) {
    ssize_t w = write(fd, txt + off, len - off);
    if(w < 0 && errno == EINTR) continue;
    if(w <= 0) { ok = 0; break; }
    off += (size_t)w;
  }
  if(close(fd) != 0) ok = 0;
  if(ok && rename(tmp, path) == 0) return 0;
  unlink(tmp);
  return -1;
}

static void
warn_write_failed(const char *tid) {
  static int warned;                      /* once: a full drive would repeat it for every title */
  if(warned) return;
  warned = 1;
  PS5TM_WARN("libcache_write_failed", "Covers & Metadaten: %s ließ sich nicht schreiben (Ordner covers_and_more).", tid);
}


/* ------------------------------------------------------------------ load and save */

/* Reads every title's meta file into the table and out. A second call starts from the disk again. */
unsigned
ps5tm_libcache_load(ps5tm_libmeta_t *out, unsigned max) {
  pthread_once(&g_once, migrate_old_dir);
  pthread_mutex_lock(&g_io);
  pthread_mutex_lock(&g_lock);
  entry_t *t = table();
  if(t) memset(t, 0, LC_MAX_TITLES * sizeof(*t));
  pthread_mutex_unlock(&g_lock);

  unsigned n = 0;
  DIR *d = opendir(PS5TM_LIBCACHE_DIR);
  struct dirent *e;
  while(t && d && (e = readdir(d)) != NULL) {
    if(!is_tid(e->d_name)) continue;
    char path[PATH_MAX], dir[PATH_MAX];
    snprintf(dir, sizeof(dir), "%s/%s", PS5TM_LIBCACHE_DIR, e->d_name);
    if(!is_real_dir(dir)) continue;
    snprintf(path, sizeof(path), "%s/meta.json", dir);
    struct stat st;
    if(lstat(path, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size <= 0 || (uint64_t)st.st_size > LC_META_MAX) continue;
    char *txt = malloc((size_t)st.st_size + 1);
    if(!txt) continue;
    FILE *f = fopen(path, "rb");
    size_t got = f ? fread(txt, 1, (size_t)st.st_size, f) : 0;
    if(f) fclose(f);
    txt[got] = 0;
    ps5tm_libmeta_t m;
    if(got == (size_t)st.st_size && parse_meta(txt, e->d_name, &m) == 0) {
      pthread_mutex_lock(&g_lock);
      entry_t *en = find_locked(m.title_id, 1);
      if(en) {
        en->m = m;
        en->hash = fnv1a(txt, got);
        en->on_disk = 1;
        if(n < max && out) out[n++] = m;
      }
      pthread_mutex_unlock(&g_lock);
    }
    free(txt);
  }
  if(d) closedir(d);
  pthread_mutex_unlock(&g_io);
  return n;
}

/* Brings the files up to date with what a scan found: only a title whose record
   changed is written. The cover fields of a record are the cover copy's own and are
   taken from the table, whatever the caller put there. */
void
ps5tm_libcache_save(const ps5tm_libmeta_t *m, unsigned n) {
  for(unsigned i = 0; i < n; i++) {
    if(!is_tid(m[i].title_id)) continue;
    pthread_mutex_lock(&g_io);
    ps5tm_libmeta_t rec = m[i];
    pthread_mutex_lock(&g_lock);
    entry_t *e = find_locked(rec.title_id, 1);
    if(e) {
      SET(rec.cover_ts, e->m.cover_ts);
      rec.cover_bytes = e->m.cover_bytes;
    }
    pthread_mutex_unlock(&g_lock);
    if(!e) { pthread_mutex_unlock(&g_io); continue; }     /* the table is full */

    /* what would be written, and whether that is what is there already */
    char *txt = meta_text(&rec);
    if(!txt) { pthread_mutex_unlock(&g_io); continue; }
    uint64_t want = fnv1a(txt, strlen(txt));
    pthread_mutex_lock(&g_lock);
    int same = e->on_disk && e->hash == want;
    pthread_mutex_unlock(&g_lock);

    if(!same) {
      if(write_text(rec.title_id, txt) == 0) {
        pthread_mutex_lock(&g_lock);
        e->m = rec;
        e->hash = want;
        e->on_disk = 1;
        pthread_mutex_unlock(&g_lock);
      } else {
        warn_write_failed(rec.title_id);
      }
    }
    free(txt);
    pthread_mutex_unlock(&g_io);
  }
}


/* ------------------------------------------------------------------ covers */

/* The copy of a title's cover, when it is there and is the one the database now names
   (ts is the database's ?ts=; an empty one accepts any). 0 and the path, or -1. */
int
ps5tm_libcache_cover_get(const char *title_id, const char *ts, char *path, size_t n) {
  pthread_once(&g_once, migrate_old_dir);
  if(!is_tid(title_id) || !path || n < 64) return -1;
  if(!ts) ts = "";
  pthread_mutex_lock(&g_lock);
  entry_t *e = g_tab ? find_locked(title_id, 0) : NULL;
  int64_t bytes = e ? e->m.cover_bytes : 0;
  int ts_ok = e && (!ts[0] || !strcmp(e->m.cover_ts, ts));
  pthread_mutex_unlock(&g_lock);
  if(!e || bytes <= 0 || !ts_ok) return -1;

  snprintf(path, n, "%s/%s/icon0.png", PS5TM_LIBCACHE_DIR, title_id);
  struct stat st;
  if(stat(path, &st) != 0 || !S_ISREG(st.st_mode) || (int64_t)st.st_size != bytes) { path[0] = 0; return -1; }
  return 0;
}

static uint64_t
covers_total_locked(const char *except) {
  uint64_t sum = 0;
  entry_t *t = g_tab;
  for(int i = 0; t && i < LC_MAX_TITLES; i++)
    if(t[i].used && strcmp(t[i].m.title_id, except)) sum += (uint64_t)(t[i].m.cover_bytes > 0 ? t[i].m.cover_bytes : 0);
  return sum;
}

/* Copies a cover into the cache, unless the copy there is already the one for this ts.
   0 when the cache has it afterwards, -1 when not (too big, no room, not readable, a write failed). */
int
ps5tm_libcache_cover_put(const char *title_id, const char *ts, const char *src) {
  if(!is_tid(title_id) || !src || !src[0]) return -1;
  if(!ts) ts = "";
  struct stat st;
  if(stat(src, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size <= 0 || (uint64_t)st.st_size > LC_MAX_COVER) return -1;

  pthread_mutex_lock(&g_io);
  int rc = -1;
  ps5tm_libmeta_t rec;
  pthread_mutex_lock(&g_lock);
  entry_t *e = find_locked(title_id, 1);
  if(!e) { pthread_mutex_unlock(&g_lock); pthread_mutex_unlock(&g_io); return -1; }
  int same = e->m.cover_bytes == (int64_t)st.st_size && !strcmp(e->m.cover_ts, ts);
  int room = covers_total_locked(title_id) + (uint64_t)st.st_size <= LC_MAX_TOTAL;
  pthread_mutex_unlock(&g_lock);

  char dst[PATH_MAX];
  snprintf(dst, sizeof(dst), "%s/%s/icon0.png", PS5TM_LIBCACHE_DIR, title_id);
  struct stat cs;
  if(same && stat(dst, &cs) == 0 && S_ISREG(cs.st_mode) && cs.st_size == st.st_size) {   /* there, and the right one */
    pthread_mutex_unlock(&g_io);
    return 0;
  }
  if(!room || make_dirs(title_id) != 0) goto out;

  {
    char tmp[PATH_MAX];
    snprintf(tmp, sizeof(tmp), "%s.tmp", dst);
    int in = open(src, O_RDONLY);
    int outfd = in >= 0 ? open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644) : -1;
    int ok = in >= 0 && outfd >= 0;
    int64_t copied = 0;
    char buf[16384];
    while(ok) {
      ssize_t r = read(in, buf, sizeof(buf));
      if(r < 0 && errno == EINTR) continue;
      if(r < 0) ok = 0;
      if(r <= 0) break;
      for(ssize_t off = 0; off < r; ) {
        ssize_t w = write(outfd, buf + off, (size_t)(r - off));
        if(w < 0 && errno == EINTR) continue;
        if(w <= 0) { ok = 0; break; }
        off += w;
      }
      copied += r;
    }
    if(in >= 0) close(in);
    if(outfd >= 0 && close(outfd) != 0) ok = 0;
    if(ok && copied == (int64_t)st.st_size && rename(tmp, dst) == 0) {
      pthread_mutex_lock(&g_lock);
      SET(e->m.cover_ts, ts);
      e->m.cover_bytes = copied;
      rec = e->m;
      pthread_mutex_unlock(&g_lock);
      char *txt = meta_text(&rec);
      if(txt && write_text(rec.title_id, txt) == 0) {
        pthread_mutex_lock(&g_lock);
        e->hash = fnv1a(txt, strlen(txt));
        e->on_disk = 1;
        pthread_mutex_unlock(&g_lock);
      }
      free(txt);
      rc = 0;
    } else {
      unlink(tmp);
      warn_write_failed(title_id);
    }
  }
out:
  pthread_mutex_unlock(&g_io);
  return rc;
}


/* ------------------------------------------------------------------ the folder as a whole */

typedef struct { unsigned titles, covers; uint64_t bytes; } stats_t;

static stats_t
scan_stats(void) {
  stats_t s = { 0, 0, 0 };
  DIR *d = opendir(PS5TM_LIBCACHE_DIR);
  struct dirent *e;
  while(d && (e = readdir(d)) != NULL) {
    if(!is_tid(e->d_name)) continue;
    char dir[PATH_MAX];
    snprintf(dir, sizeof(dir), "%s/%s", PS5TM_LIBCACHE_DIR, e->d_name);
    if(!is_real_dir(dir)) continue;                  /* a link of that name is not ours */
    static const char *const names[] = { "meta.json", "icon0.png", "meta.json.tmp", "icon0.png.tmp" };
    int any = 0;
    for(int i = 0; i < 4; i++) {
      char p[PATH_MAX];
      struct stat st;
      snprintf(p, sizeof(p), "%s/%s", dir, names[i]);
      if(lstat(p, &st) != 0 || !S_ISREG(st.st_mode)) continue;
      any = 1;
      s.bytes += (uint64_t)st.st_size;
      if(i == 1) s.covers++;
    }
    if(any) s.titles++;
  }
  if(d) closedir(d);
  return s;
}

cJSON *
ps5tm_libcache_json(void) {
  pthread_once(&g_once, migrate_old_dir);
  cJSON *o = cJSON_CreateObject();
  if(!o) return NULL;
  stats_t s = scan_stats();
  cJSON_AddBoolToObject(o, "ok", 1);
  cJSON_AddBoolToObject(o, "enabled", ps5tm_libcache_enabled());
  cJSON_AddStringToObject(o, "dir", PS5TM_LIBCACHE_DIR);
  cJSON_AddNumberToObject(o, "titles", s.titles);
  cJSON_AddNumberToObject(o, "covers", s.covers);
  cJSON_AddNumberToObject(o, "bytes", (double)s.bytes);
  return o;
}

/* Removes what this module made. 0, or an errno when the folder cannot be read at all. */
int
ps5tm_libcache_clear(unsigned *titles, uint64_t *bytes) {
  pthread_once(&g_once, migrate_old_dir);
  *titles = 0; *bytes = 0;
  pthread_mutex_lock(&g_io);
  DIR *d = opendir(PS5TM_LIBCACHE_DIR);
  if(!d) {
    int eno = errno;
    pthread_mutex_unlock(&g_io);
    return eno == ENOENT ? 0 : (eno ? eno : EIO);
  }
  struct dirent *e;
  while((e = readdir(d)) != NULL) {
    if(!is_tid(e->d_name)) continue;
    char dir[PATH_MAX];
    struct stat ds;
    snprintf(dir, sizeof(dir), "%s/%s", PS5TM_LIBCACHE_DIR, e->d_name);
    if(lstat(dir, &ds) != 0 || !S_ISDIR(ds.st_mode)) continue;        /* a link or a file of that name is not ours */
    static const char *const names[] = { "meta.json", "icon0.png", "meta.json.tmp", "icon0.png.tmp" };
    int any = 0;
    for(int i = 0; i < 4; i++) {
      char p[PATH_MAX];
      struct stat st;
      snprintf(p, sizeof(p), "%s/%s", dir, names[i]);
      if(lstat(p, &st) != 0 || !S_ISREG(st.st_mode)) continue;
      if(unlink(p) == 0) { any = 1; *bytes += (uint64_t)st.st_size; }
    }
    rmdir(dir);                                  /* only when nothing foreign is in it */
    if(any) (*titles)++;
  }
  closedir(d);

  pthread_mutex_lock(&g_lock);
  if(g_tab) memset(g_tab, 0, LC_MAX_TITLES * sizeof(*g_tab));
  pthread_mutex_unlock(&g_lock);
  pthread_mutex_unlock(&g_io);
  PS5TM_INFO("libcache_cleared", "Covers & Metadaten: %u Titel aus covers_and_more gelöscht (%llu KB).",
             *titles, (unsigned long long)(*bytes >> 10));
  return 0;
}
