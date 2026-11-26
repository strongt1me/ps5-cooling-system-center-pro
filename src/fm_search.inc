/* The search of the file manager (25.11.2026, wish of the user): names in a folder and everything below it, on the console's drives and on
 * NTFS sticks alike (the nodes of fm_ntfs.inc). One search at a time, on a thread of its own; the page asks for its state and its hits.
 *
 *   POST /api/v1/files/search        {path, q}: starts it (a running one is stopped first); 400 bad path or term, 409 drive being ejected
 *   GET  /api/v1/files/search        {state (idle|running|done|cancelled|failed), path, q, scanned, folders, current, hits[], truncated, error}
 *   POST /api/v1/files/search/cancel stops it
 *
 * A term without * or ? matches a part of the name; with them it matches the whole name (*.pkg, PPSA?????). Upper and lower case are the same
 * (ASCII and the Latin letters with accents). Links are not followed, another drive below is not entered (as when copying), "/" and the device
 * and process file systems are not searched. At most FM_SEARCH_HITS hits are kept (truncated then), the walk ends after FM_COUNT_MAX entries.
 * While it runs it counts as a reader of the file manager (an eject waits for it, jobreg.c), and an eject that starts stops it
 * (ps5tm_filemgr_search_stop). Included by filemgr.c after fm_ntfs.inc. */

#define FM_SEARCH_HITS 2000
#define FM_SEARCH_TERM 128

enum { FS_IDLE, FS_RUNNING, FS_DONE, FS_CANCELLED, FS_FAILED };

typedef struct {
  char    *path;                         /* the page path of the hit */
  char     type;                         /* 'd' folder, 'f' file, 'o' other */
  int      writable;                     /* may be moved or deleted (the same rule as the folder list) */
  uint64_t size;
  int64_t  mtime;
  char    *title;                        /* the game of a name with a title ID (fm_title.inc), NULL when none */
} fs_hit_t;

static struct {
  int       state;
  char      root[FM_PATH];               /* the page path searched */
  char      q[FM_SEARCH_TERM];
  uint64_t  scanned, folders;
  char      current[FM_PATH];
  fs_hit_t *hits;
  unsigned  nhits;
  int       truncated;
  char      error[300];
  uint64_t  started_ms, finished_ms;
  unsigned  gen;                         /* counts the searches: a thread that ends late leaves a newer one alone */
} g_search;
static int g_search_cancel;              /* atomic */

/* ---------------------------------------------------------------- matching */

/* one character, folded: ASCII and the Latin-1 letters (U+00C0 to U+00DE, but not ×) to lower case. Returns the code point, advances *s. */
static unsigned
fs_fold(const char **s) {
  const unsigned char *p = (const unsigned char *)*s;
  unsigned c;
  if(p[0] < 0x80) { c = p[0]; *s += 1; return c >= 'A' && c <= 'Z' ? c + 32 : c; }
  if((p[0] & 0xE0) == 0xC0 && (p[1] & 0xC0) == 0x80) {
    c = ((unsigned)(p[0] & 0x1F) << 6) | (p[1] & 0x3F);
    *s += 2;
    return c >= 0xC0 && c <= 0xDE && c != 0xD7 ? c + 32 : c;
  }
  if((p[0] & 0xF0) == 0xE0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) {
    *s += 3;
    return ((unsigned)(p[0] & 0x0F) << 12) | ((unsigned)(p[1] & 0x3F) << 6) | (p[2] & 0x3F);
  }
  if((p[0] & 0xF8) == 0xF0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80 && (p[3] & 0xC0) == 0x80) {
    *s += 4;
    return ((unsigned)(p[0] & 0x07) << 18) | ((unsigned)(p[1] & 0x3F) << 12) | ((unsigned)(p[2] & 0x3F) << 6) | (p[3] & 0x3F);
  }
  *s += 1;                               /* a stray byte stands for itself */
  return 0x110000u + p[0];
}

/* the term as folded code points; 0 (and n) when it fits, -1 when it is too long */
static int
fs_compile(const char *q, unsigned *out, size_t max, size_t *n, int *wild) {
  *n = 0;
  *wild = 0;
  while(*q) {
    if(*n >= max) return -1;
    unsigned c = fs_fold(&q);
    if(c == '*' || c == '?') *wild = 1;
    out[(*n)++] = c;
  }
  return 0;
}

/* glob over code points: '*' any run, '?' one character */
static int
fs_glob(const unsigned *p, size_t pn, const unsigned *s, size_t sn) {
  size_t pi = 0, si = 0, star = (size_t)-1, mark = 0;
  while(si < sn) {
    /* the star first: compared as a character it would be used up by a '*' in the name ("*.pkg" did not find "*x.pkg"; audit 26.11.2026) */
    if(pi < pn && p[pi] == '*') { star = pi++; mark = si; continue; }
    if(pi < pn && (p[pi] == '?' || p[pi] == s[si])) { pi++; si++; continue; }
    if(star != (size_t)-1) { pi = star + 1; si = ++mark; continue; }
    return 0;
  }
  while(pi < pn && p[pi] == '*') pi++;
  return pi == pn;
}

/* Does name match the compiled term? Without wildcards: a part of it; with them: all of it. */
static int
fs_match(const unsigned *t, size_t tn, int wild, const char *name) {
  unsigned s[FM_NAME];
  size_t sn = 0;
  while(*name && sn < FM_NAME) s[sn++] = fs_fold(&name);
  if(wild) return fs_glob(t, tn, s, sn);
  if(tn == 0) return 0;
  for(size_t i = 0; i + tn <= sn; i++)
    if(!memcmp(s + i, t, tn * sizeof(unsigned))) return 1;
  return 0;
}

/* ---------------------------------------------------------------- the walk */

typedef struct {
  const unsigned *t;
  size_t          tn;
  int             wild;
  int             ntfs;
  int             drive_w;               /* ntfs: the stick may be written */
  dev_t           dev;                   /* console: the drive the walk stays on */
  char           *v;                     /* the page path of the folder walked (FM_PATH + FM_NAME) */
  size_t          vl;
  unsigned        gen;
  int             stop;                  /* ECANCELED, ENODEV (the drive goes), E2BIG, EIO (the folder searched could not be read) */
  unsigned        unread;                /* folders below that could not be read: the search says so at its end */
} fs_walk_t;

static void
fs_note(fs_walk_t *w, uint64_t scanned_add, int folder) {
  pthread_mutex_lock(&g_lock);
  if(g_search.gen == w->gen) {
    g_search.scanned += scanned_add;
    if(folder) {
      g_search.folders++;
      snprintf(g_search.current, sizeof(g_search.current), "%s", w->v);
    }
  }
  pthread_mutex_unlock(&g_lock);
}

static void
fs_hit(fs_walk_t *w, const char *name, const fmn_st_t *st, const char *title) {
  char p[FM_PATH + FM_NAME + 2];
  int root = w->vl == 1 && w->v[0] == '/';
  snprintf(p, sizeof(p), "%s%s%s", w->v, root ? "" : "/", name);
  /* the whole path, not only the name: a hit below a folder whose name is no UTF-8 cannot be addressed by the page either (the path it would send holds
     the replacement character), so it is shown but not offered for changing (audit 26.11.2026) */
  char shown[sizeof(p) + 8];
  const int bad_name = name_for_json(shown, sizeof(shown), p);
  const int wr = !bad_name && st->type != 'o' &&
                 (w->ntfs ? w->drive_w && !(st->attrs & NTFS_FA_SYSTEM) : writable(p));
  char *dup = strdup(p);
  if(!dup) return;
  char *tdup = title && title[0] ? strdup(title) : NULL;
  pthread_mutex_lock(&g_lock);
  if(g_search.gen == w->gen) {
    if(g_search.nhits >= FM_SEARCH_HITS) g_search.truncated = 1;
    else {
      fs_hit_t *h = &g_search.hits[g_search.nhits++];
      h->path = dup;
      dup = NULL;
      h->title = tdup;
      tdup = NULL;
      h->type = st->type;
      h->writable = wr;
      h->size = st->size;
      h->mtime = st->mtime;
    }
  }
  pthread_mutex_unlock(&g_lock);
  free(dup);
  free(tdup);
}

static void
fs_walk(fmn_node_t *n, fs_walk_t *w, int depth) {
  if(w->stop) return;
  {
    /* 2: stopped by an eject (ps5tm_filemgr_search_stop): it ends as "the drive goes", with the sentence that says so, not as a bare "cancelled" */
    const int c = __atomic_load_n(&g_search_cancel, __ATOMIC_ACQUIRE);
    if(c) { w->stop = c == 2 ? ENODEV : ECANCELED; return; }
  }
  if(ps5tm_usbeject_settling(w->v) || (!w->ntfs && ps5tm_usbeject_settling(n->p))) { w->stop = ENODEV; return; }
  if(depth > FM_DEPTH) return;
  fmn_dir_t d;
  memset(&d, 0, sizeof(d));
  int rc = fmn_list(n, &d);
  fs_note(w, d.total, 1);
  if(rc) {
    /* A folder that cannot be read is passed by, but not silently (audit 26.11.2026: a stick pulled in the middle left every further folder unread, and
       the search ended as complete). ENXIO is what the console answers once the device is gone: the search ends there. The folder searched itself must
       be readable; other folders are counted and named at the end. */
    fmn_dir_free(&d);
    if(rc == ENODEV || rc == ENXIO) w->stop = ENODEV;
    else if(depth == 0) w->stop = EIO;
    else w->unread++;
    return;
  }
  pthread_mutex_lock(&g_lock);
  int too_many = g_search.scanned > FM_COUNT_MAX;
  pthread_mutex_unlock(&g_lock);
  if(too_many) { fmn_dir_free(&d); w->stop = E2BIG; return; }
  for(size_t i = 0; i < d.n && !w->stop; i++) {
    const char *nm = d.e[i].name;
    int m = fs_match(w->t, w->tn, w->wild, nm);
    /* a name with a title ID: also found by its game ("lantern" finds PPSA01325), and the hit names the game */
    char tid[10], title[FMT_TITLE] = "";
    if(fmt_tid(nm, tid)) {
      char dir[FM_PATH + FM_NAME + 64];
      const int own = !w->ntfs && d.e[i].st.type == 'd' && snprintf(dir, sizeof(dir), "%s/%s", n->p, nm) < (int)sizeof(dir);
      if(fmt_title(own ? dir : NULL, tid, title, sizeof(title)) != 0) title[0] = 0;
      if(!m && title[0]) m = fs_match(w->t, w->tn, w->wild, title);
    }
    if(m) fs_hit(w, nm, &d.e[i].st, title);
  }
  size_t was = n->len, vwas = w->vl;
  for(size_t i = 0; i < d.n && !w->stop; i++) {
    const fmn_st_t *st = &d.e[i].st;
    if(st->type != 'd') continue;                                           /* links and reparse points are not followed */
    if(!w->ntfs && st->dev != w->dev) continue;                              /* another drive hangs here */
    if(n->ntfs && (st->attrs & NTFS_FA_SYSTEM) && !strcmp(d.e[i].name, "$Extend")) continue;
    size_t nl = strlen(d.e[i].name);
    int vroot = w->vl == 1 && w->v[0] == '/';
    if(w->vl + 1 + nl >= FM_PATH || fmn_push(n, d.e[i].name) != 0) continue;  /* too deep a path: passed by */
    if(!vroot) w->v[w->vl++] = '/';
    memcpy(w->v + w->vl, d.e[i].name, nl + 1);
    w->vl += nl;
    fs_walk(n, w, depth + 1);
    fmn_pop(n, was);
    w->vl = vwas;
    w->v[vwas] = 0;
  }
  fmn_dir_free(&d);
}

typedef struct { char root[FM_PATH]; char q[FM_SEARCH_TERM]; unsigned gen; } fs_args_t;

static void
fs_finish(unsigned gen, int state, const char *error) {
  pthread_mutex_lock(&g_lock);
  if(g_search.gen == gen) {
    g_search.state = state;
    snprintf(g_search.error, sizeof(g_search.error), "%s", error ? error : "");
    g_search.current[0] = 0;
    g_search.finished_ms = ps5tm_mono_ms();
  }
  g_reads--;                                                                 /* entered in fs_start */
  pthread_mutex_unlock(&g_lock);
}

static void *
fs_thread(void *arg) {
  fs_args_t *a = arg;
  unsigned t[FM_SEARCH_TERM];
  size_t tn = 0;
  int wild = 0;
  fs_compile(a->q, t, FM_SEARCH_TERM, &tn, &wild);
  if(ps5tm_usbeject_settling(a->root)) {                                     /* an eject began since the start asked: the drive is not touched at all */
    fs_finish(a->gen, FS_CANCELLED, "Das Laufwerk wird ausgeworfen oder ist nicht mehr da; die Suche wurde beendet.");
    free(a);
    return NULL;
  }
  fmn_node_t n;
  int rc = fmn_make(a->root, &n);
  fmn_st_t st;
  if(!rc && fmn_stat(&n, &st) != 0) { fmn_unmake(&n); rc = ENOENT; }
  if(!rc && st.type != 'd') { fmn_unmake(&n); rc = ENOTDIR; }
  if(rc) {
    fs_finish(a->gen, FS_FAILED, rc == ENODEV ? "Das NTFS-Laufwerk ist nicht (mehr) da." : "Den Ordner gibt es nicht (mehr).");
    free(a);
    return NULL;
  }
  char *v = malloc(FM_PATH + FM_NAME + 2);
  if(!v) { fmn_unmake(&n); fs_finish(a->gen, FS_FAILED, "Kein Speicher."); free(a); return NULL; }
  snprintf(v, FM_PATH, "%s", a->root);
  char why[PS5TM_NTFS_WHY];
  fs_walk_t w = { t, tn, wild, n.ntfs, n.ntfs && ps5tm_ntfs_writable(n.vol, why, sizeof(why)), st.dev, v, strlen(v), a->gen, 0, 0 };
  fs_walk(&n, &w, 0);
  fmn_unmake(&n);
  free(v);
  if(w.stop == ECANCELED) fs_finish(a->gen, FS_CANCELLED, NULL);
  else if(w.stop == ENODEV) fs_finish(a->gen, FS_CANCELLED, "Das Laufwerk wird ausgeworfen oder ist nicht mehr da; die Suche wurde beendet.");
  else if(w.stop == EIO) fs_finish(a->gen, FS_FAILED, "Der Ordner ließ sich nicht lesen.");
  else if(w.stop == E2BIG) {
    pthread_mutex_lock(&g_lock);
    if(g_search.gen == a->gen) g_search.truncated = 1;
    pthread_mutex_unlock(&g_lock);
    fs_finish(a->gen, FS_DONE, "Nach einer Million Einträgen beendet; bitte in einem Unterordner weitersuchen.");
  } else if(w.unread) {
    char note[96];
    snprintf(note, sizeof(note), "Nicht lesbare Ordner: %u. Sie fehlen in der Suche.", w.unread);
    fs_finish(a->gen, FS_DONE, note);
  } else fs_finish(a->gen, FS_DONE, NULL);
  free(a);
  return NULL;
}

static void
fs_clear_hits_locked(void) {
  for(unsigned i = 0; i < g_search.nhits; i++) { free(g_search.hits[i].path); free(g_search.hits[i].title); }
  g_search.nhits = 0;
  g_search.truncated = 0;
}

/* Waits (up to ms) until no search runs. 1 when none runs any more. */
static int
fs_wait_idle(unsigned ms) {
  for(unsigned waited = 0;; waited += 20) {
    pthread_mutex_lock(&g_lock);
    int running = g_search.state == FS_RUNNING;
    pthread_mutex_unlock(&g_lock);
    if(!running) return 1;
    if(waited >= ms) return 0;
    usleep(20 * 1000);
  }
}

/* A search on the drive of `mount` (a page path such as /mnt/usb0 or /ntfs0; NULL: any) is stopped, and waited for a little (eject, end of the app). */
void
ps5tm_filemgr_search_stop(const char *mount) {
  pthread_mutex_lock(&g_lock);
  int hit = g_search.state == FS_RUNNING;
  if(hit && mount) {
    size_t ml = strlen(mount);
    char real[FM_PATH + 64];
    hit = (!strncmp(g_search.root, mount, ml) && (g_search.root[ml] == '/' || !g_search.root[ml])) ||
          (real_path(g_search.root, real, sizeof(real)) == 0 && !strncmp(real, mount, ml) && (real[ml] == '/' || !real[ml]));
  }
  if(hit) __atomic_store_n(&g_search_cancel, mount ? 2 : 1, __ATOMIC_RELEASE);   /* 2: the drive goes (fs_walk says so in the search's last word) */
  pthread_mutex_unlock(&g_lock);
  if(hit) fs_wait_idle(3000);
}

static void fs_start_entered(int fd, const char *root, const char *term);

static void
fs_start(int fd, const ps5tm_request_t *req) {
  cJSON *b = parse_body(req);
  const char *p = jstr(b, "path"), *q = jstr(b, "q");
  char root[FM_PATH], term[FM_SEARCH_TERM];
  int whole = p && !strcmp(p, "/");
  int bad_path = !p || !path_plain(p) || strlen(p) >= sizeof(root) || whole;
  int bad_term = !q || !q[0] || strlen(q) >= sizeof(term);
  if(!bad_path) snprintf(root, sizeof(root), "%s", p);
  if(!bad_term) snprintf(term, sizeof(term), "%s", q);
  cJSON_Delete(b);
  if(bad_path) {
    ps5tm_http_send_error(fd, 400, "bad_path", whole ? "Das ganze System wird nicht durchsucht. Bitte zuerst ein Laufwerk oder einen Ordner öffnen."
                                                                     : "Ungültiger Ordner.");
    return;
  }
  if(bad_term) { ps5tm_http_send_error(fd, 400, "bad_term", "Bitte einen Suchbegriff eingeben (höchstens 127 Zeichen)."); return; }
  {
    unsigned t[FM_SEARCH_TERM];
    size_t tn;
    int wild;
    if(fs_compile(term, t, FM_SEARCH_TERM, &tn, &wild) != 0) { ps5tm_http_send_error(fd, 400, "bad_term", "Der Suchbegriff ist zu lang."); return; }
    int only_stars = 1;
    for(size_t i = 0; i < tn; i++) if(t[i] != '*') only_stars = 0;
    if(only_stars) { ps5tm_http_send_error(fd, 400, "bad_term", "Ein Suchbegriff nur aus * findet alles; bitte genauer suchen."); return; }
  }
  /* Entered as a read first, asked about an eject afterwards, and the drive is touched only after that (as with_read does; audit 26.11.2026: the start
     looked at the drive with statfs before it asked, and counted itself only once the search was set up, so an eject that began beside it saw no reader).
     The request stays entered until it returns; the search itself is entered below, under the same lock, before this count goes. */
  pthread_mutex_lock(&g_lock);
  g_reads++;
  pthread_mutex_unlock(&g_lock);
  fs_start_entered(fd, root, term);
  pthread_mutex_lock(&g_lock);
  g_reads--;
  pthread_mutex_unlock(&g_lock);
}

static void
fs_start_entered(int fd, const char *root, const char *term) {
  char real[FM_PATH + 64];
  const int ntfs = ps5tm_ntfs_path(root);
  if(ps5tm_usbeject_settling(root)) {
    ps5tm_http_send_error(fd, 409, "drive_ejecting", "Das Laufwerk wird gerade ausgeworfen. Danach geht es wieder.");
    return;
  }
  if(!ntfs && real_path(root, real, sizeof(real)) != 0) {
    ps5tm_http_send_error(fd, 400, "bad_path", "Dieser Ordner wird nicht durchsucht.");
    return;
  }
  if(!ntfs && ps5tm_usbeject_settling(real)) {                              /* a link may lead onto the drive */
    ps5tm_http_send_error(fd, 409, "drive_ejecting", "Das Laufwerk wird gerade ausgeworfen. Danach geht es wieder.");
    return;
  }
  if(!ntfs && special_fs(root)) {
    ps5tm_http_send_error(fd, 400, "bad_path", "Dieser Ordner wird nicht durchsucht.");
    return;
  }
  /* a running search is stopped first (the page starts a new one when the term changes) */
  pthread_mutex_lock(&g_lock);
  if(g_search.state == FS_RUNNING) __atomic_store_n(&g_search_cancel, 1, __ATOMIC_RELEASE);
  pthread_mutex_unlock(&g_lock);
  if(!fs_wait_idle(3000)) { ps5tm_http_send_error(fd, 409, "busy", "Die vorige Suche endet gerade noch. Bitte gleich noch einmal."); return; }
  fs_args_t *a = malloc(sizeof(*a));
  fs_hit_t *hits = a ? malloc(sizeof(fs_hit_t) * FM_SEARCH_HITS) : NULL;
  if(!a || !hits) { free(a); free(hits); ps5tm_http_send_error(fd, 500, "alloc_failed", "Kein Speicher."); return; }
  pthread_mutex_lock(&g_lock);
  if(g_search.state == FS_RUNNING) {                                         /* another request started one meanwhile */
    pthread_mutex_unlock(&g_lock);
    free(a);
    free(hits);
    ps5tm_http_send_error(fd, 409, "busy", "Es läuft gerade schon eine Suche.");
    return;
  }
  fs_clear_hits_locked();
  free(g_search.hits);
  g_search.hits = hits;
  g_search.state = FS_RUNNING;
  g_search.gen++;
  snprintf(g_search.root, sizeof(g_search.root), "%s", root);
  snprintf(g_search.q, sizeof(g_search.q), "%s", term);
  g_search.scanned = g_search.folders = 0;
  g_search.current[0] = g_search.error[0] = 0;
  g_search.started_ms = ps5tm_mono_ms();
  g_search.finished_ms = 0;
  g_reads++;                                                                 /* a reader until fs_finish: an eject waits for it, or stops it */
  __atomic_store_n(&g_search_cancel, 0, __ATOMIC_RELEASE);
  snprintf(a->root, sizeof(a->root), "%s", root);
  snprintf(a->q, sizeof(a->q), "%s", term);
  a->gen = g_search.gen;
  pthread_mutex_unlock(&g_lock);
  pthread_t th;
  pthread_attr_t at;
  pthread_attr_init(&at);
  pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
  pthread_attr_setstacksize(&at, 1024 * 1024);                               /* fs_walk recurses (up to 64 levels): never the 64 KiB default */
  int rc = pthread_create(&th, &at, fs_thread, a);
  pthread_attr_destroy(&at);
  if(rc != 0) {
    unsigned gen = a->gen;
    free(a);
    fs_finish(gen, FS_FAILED, "Die Suche ließ sich nicht starten.");
    ps5tm_http_send_error(fd, 500, "start_failed", "Die Suche ließ sich nicht starten.");
    return;
  }
  ps5tm_http_send_json(fd, 200, "{\"ok\":true}");
}

static cJSON *
fs_json(void) {
  static const char *const names[] = { "idle", "running", "done", "cancelled", "failed" };
  cJSON *o = cJSON_CreateObject();
  cJSON_AddBoolToObject(o, "ok", 1);
  pthread_mutex_lock(&g_lock);
  cJSON_AddStringToObject(o, "state", names[g_search.state]);
  cJSON_AddStringToObject(o, "path", g_search.root);
  cJSON_AddStringToObject(o, "q", g_search.q);
  cJSON_AddNumberToObject(o, "scanned", (double)g_search.scanned);
  cJSON_AddNumberToObject(o, "folders", (double)g_search.folders);
  char cur[FM_PATH * 2];
  name_for_json(cur, sizeof(cur), g_search.current);
  cJSON_AddStringToObject(o, "current", cur);
  cJSON_AddBoolToObject(o, "truncated", g_search.truncated);
  cJSON_AddStringToObject(o, "error", g_search.error);
  uint64_t now = ps5tm_mono_ms();
  cJSON_AddNumberToObject(o, "elapsed_s", g_search.started_ms ? (double)(((g_search.finished_ms ? g_search.finished_ms : now) - g_search.started_ms) / 1000) : 0);
  cJSON *arr = cJSON_AddArrayToObject(o, "hits");
  for(unsigned i = 0; i < g_search.nhits; i++) {
    const fs_hit_t *h = &g_search.hits[i];
    char pj[FM_PATH * 2];
    name_for_json(pj, sizeof(pj), h->path);
    cJSON *e = cJSON_CreateObject();
    cJSON_AddStringToObject(e, "path", pj);
    cJSON_AddStringToObject(e, "type", h->type == 'd' ? "dir" : h->type == 'f' ? "file" : "other");
    cJSON_AddBoolToObject(e, "writable", h->writable);
    cJSON_AddNumberToObject(e, "size", (double)h->size);
    cJSON_AddNumberToObject(e, "mtime", (double)h->mtime);
    if(h->title) cJSON_AddStringToObject(e, "title", h->title);
    cJSON_AddItemToArray(arr, e);
  }
  pthread_mutex_unlock(&g_lock);
  return o;
}

static void
fs_cancel(int fd) {
  pthread_mutex_lock(&g_lock);
  int running = g_search.state == FS_RUNNING;
  if(running) __atomic_store_n(&g_search_cancel, 1, __ATOMIC_RELEASE);
  pthread_mutex_unlock(&g_lock);
  if(!running) { ps5tm_http_send_error(fd, 409, "not_running", "Es läuft gerade keine Suche."); return; }
  ps5tm_http_send_json(fd, 200, "{\"ok\":true}");
}
