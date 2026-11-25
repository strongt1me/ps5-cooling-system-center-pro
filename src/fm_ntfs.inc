/* NTFS drives in the file manager (23.11.2026, the user's wish: the driver of PS5-NTFS-BROWSER by abcde6543 in the app, reading and
 * writing). Included by filemgr.c: it uses that file's job (bar, current file, Abbrechen), its helpers and its limits.
 *
 * A drive is the path /ntfsN, its folders /ntfsN/... (ntfsvol.c). The driver adds files and folders and deletes them; it never
 * changes or renames a file that is there. So on an NTFS drive:
 *  - listing, looking (text and pictures), downloading, the size of a folder: as everywhere;
 *  - a new folder, an upload, copying and moving onto it: as everywhere, but a name that is taken is never replaced ("Ersetzen" is
 *    refused; "Beide behalten" and "Überspringen" work), and names Windows cannot hold (\ : * ? " < > |, a trailing dot ...) are
 *    adjusted as the original does;
 *  - deleting: files and folders with everything in them; files Windows tracks elsewhere (hard links, object ids, reparse points)
 *    are refused by the driver with a reason;
 *  - renaming and editing a text file: refused (both would change a file that is there).
 * Moving from or to an NTFS drive (and within one) is copying, and the original is deleted only where the copy holds the same file.
 *
 * Copy, move and delete jobs that touch an NTFS drive go through a small layer below ("a node": the console's file system or a
 * drive); jobs only on the console stay on the code above, as they were. Each folder is read into a list before it is worked on:
 * the driver holds its volume lock while it lists, so nothing else may ask the same drive from inside a listing. */

/* ------------------------------------------------------------------ words */

static const char *
ntfs_words(int e) {
  if(e == EFBIG) return "Die Datei passt nicht in einem Stück auf das NTFS-Laufwerk: Der freie Platz ist zu zerstückelt. Am PC "
                        "defragmentieren oder auf ein leeres Laufwerk kopieren.";
  if(e == ENOSPC) return "Auf dem Laufwerk ist kein Platz mehr.";
  if(e == EROFS) return "Das NTFS-Laufwerk ist nur lesbar.";
  if(e == ENOTSUP) return "Windows führt diese Datei noch an anderer Stelle (Hardlink, Objekt-ID oder Analysepunkt); sie lässt sich nur "
                          "an einem PC löschen.";
  if(e == EPERM) return "Das ist eine Systemdatei von NTFS; sie bleibt.";
  if(e == EACCES) return "Die Datei ist verschlüsselt (EFS); ihr Inhalt lässt sich nicht lesen.";
  if(e == ENOTEMPTY) return "Der Ordner ist nicht leer.";
  if(e == EEXIST) return "Diesen Namen gibt es hier schon (NTFS unterscheidet nicht nach Groß- und Kleinschreibung).";
  if(e == ENODEV) return "Das NTFS-Laufwerk ist nicht (mehr) da.";
  if(e == EIO) return "Lese- oder Schreibfehler auf dem Laufwerk (ist es noch eingesteckt?).";
  if(e == EBUSY) return "Von dem NTFS-Laufwerk wird gerade heruntergeladen oder eine Datei angesehen; danach noch einmal.";
  return io_words(e);
}

/* A name Windows can hold, as the original makes it (vfs_safe_name): no \ : * ? " < > |, no control character, no trailing dot or
   space, and not a device name such as CON (also COM0, LPT0 and COM¹ … LPT³, which Windows treats the same; upstream PR #20). 0, or -1
   when the name does not fit: it is never cut, a cut name could be another one that is there (upstream PR #14). */
static int
fmn_safe_name(char *dst, size_t len, const char *src) {
  static const char *const reserved[] = { "CON", "PRN", "AUX", "NUL", "COM0", "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7",
                                          "COM8", "COM9", "LPT0", "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9",
                                          "COM\xC2\xB9", "COM\xC2\xB2", "COM\xC2\xB3", "LPT\xC2\xB9", "LPT\xC2\xB2", "LPT\xC2\xB3" };
  char tmp[FM_NAME];
  if(snprintf(tmp, sizeof(tmp), "%s", src) >= (int)sizeof(tmp)) return -1;
  for(char *p = tmp; *p; p++)
    if(strchr("\\:*?\"<>|", *p) || (unsigned char)*p < 0x20) *p = '_';
  size_t n = strlen(tmp);
  while(n && (tmp[n - 1] == '.' || tmp[n - 1] == ' ')) tmp[--n] = '_';
  if(!n) snprintf(tmp, sizeof(tmp), "_");
  char stem[16];
  size_t sl = strcspn(tmp, ".");
  if(sl < sizeof(stem)) {
    memcpy(stem, tmp, sl);
    stem[sl] = 0;
    for(size_t i = 0; i < sizeof(reserved) / sizeof(reserved[0]); i++)
      if(!strcasecmp(stem, reserved[i])) return snprintf(dst, len, "_%s", tmp) >= (int)len ? -1 : 0;
  }
  return snprintf(dst, len, "%s", tmp) >= (int)len ? -1 : 0;
}


/* ------------------------------------------------------------------ nodes: the console's file system or an NTFS drive */

typedef struct {
  int              ntfs;
  ps5tm_ntfsvol_t *vol;                  /* ntfs: the drive (its reference is held by whoever made the node) */
  char            *p;                    /* console: the real path; ntfs: the path inside the drive. PATH_MAX, grows while going down */
  size_t           len;
  dev_t            dev;                  /* console: the drive a walk stays on */
} fmn_node_t;

typedef struct {
  char     type;                         /* 'd' folder, 'f' file, 'o' anything else (link, device, reparse point) */
  uint64_t size;
  int64_t  mtime;
  uint64_t id;                           /* inode */
  dev_t    dev;
  uint32_t attrs;                        /* ntfs: NTFS_FA_* */
} fmn_st_t;

static struct ntfs_vol *nh(const fmn_node_t *n) { return ps5tm_ntfs_handle(n->vol); }

/* The node of a page path, with its own buffer and (for a drive) a reference: 0, or ENODEV (the drive is not there), ENAMETOOLONG,
   ENOMEM. fmn_unmake gives it back. */
static int
fmn_make(const char *vpath, fmn_node_t *n) {
  memset(n, 0, sizeof(*n));
  n->p = malloc(PATH_MAX);
  if(!n->p) return ENOMEM;
  if(ps5tm_ntfs_path(vpath)) {
    const char *rest = NULL;
    n->vol = ps5tm_ntfs_get(vpath, &rest);
    if(!n->vol) { free(n->p); n->p = NULL; return ENODEV; }
    n->ntfs = 1;
    if(snprintf(n->p, PATH_MAX, "%s", rest) >= PATH_MAX) { ps5tm_ntfs_put(n->vol); free(n->p); n->p = NULL; return ENAMETOOLONG; }
  } else if(real_path(vpath, n->p, PATH_MAX) != 0) {
    free(n->p);
    n->p = NULL;
    return ENAMETOOLONG;
  }
  n->len = strlen(n->p);
  return 0;
}

static void
fmn_unmake(fmn_node_t *n) {
  if(n->vol) ps5tm_ntfs_put(n->vol);
  free(n->p);
  memset(n, 0, sizeof(*n));
}

/* name below the node (the caller restores n->len afterwards with fmn_pop) */
static int
fmn_push(fmn_node_t *n, const char *name) {
  size_t l = strlen(name);
  int root = n->len == 1 && n->p[0] == '/';
  if(n->len + 1 + l >= PATH_MAX) return ENAMETOOLONG;
  if(root) { memcpy(n->p + 1, name, l + 1); n->len = 1 + l; }
  else { n->p[n->len] = '/'; memcpy(n->p + n->len + 1, name, l + 1); n->len += 1 + l; }
  return 0;
}

static void fmn_pop(fmn_node_t *n, size_t was) { n->len = was; n->p[was] = 0; }

static int
fmn_stat(fmn_node_t *n, fmn_st_t *st) {
  memset(st, 0, sizeof(*st));
  if(!n->ntfs) {
    struct stat s;
    if(lstat(n->p, &s) != 0) return errno ? errno : EIO;
    st->type = S_ISREG(s.st_mode) ? 'f' : S_ISDIR(s.st_mode) ? 'd' : 'o';
    st->size = S_ISREG(s.st_mode) ? (uint64_t)s.st_size : 0;
    st->mtime = (int64_t)s.st_mtime;
    st->id = (uint64_t)s.st_ino;
    st->dev = s.st_dev;
    return 0;
  }
  uint64_t ino;
  ntfs_stat_t ns;
  int rc = ntfs_lookup(nh(n), n->p, &ino);
  if(!rc) rc = ntfs_stat(nh(n), ino, &ns);
  if(rc) return -rc;
  st->type = (ns.attrs & NTFS_FA_REPARSE) ? 'o' : ns.is_dir ? 'd' : 'f';
  st->size = ns.is_dir ? 0 : ns.size;
  st->mtime = ns.mtime;
  st->id = ino;
  st->attrs = ns.attrs;
  return 0;
}

/* A folder's entries, read whole before anything is done with them. */
typedef struct { char *name; fmn_st_t st; } fmn_ent_t;
typedef struct {
  fmn_ent_t *e;
  size_t     n, cap;
  size_t     max;                        /* kept at most (0: all); `total` counts every entry */
  unsigned   total;
  int        err;
} fmn_dir_t;

static void
fmn_dir_free(fmn_dir_t *d) {
  for(size_t i = 0; i < d->n; i++) free(d->e[i].name);
  free(d->e);
  memset(d, 0, sizeof(*d));
}

static int
fmn_dir_add(fmn_dir_t *d, const char *name, const fmn_st_t *st) {
  d->total++;
  if((d->max && d->n >= d->max) || strlen(name) >= FM_NAME) return 0;
  if(d->n == d->cap) {
    size_t nc = d->cap ? d->cap * 2 : 64;
    fmn_ent_t *ne = realloc(d->e, nc * sizeof(*ne));
    if(!ne) return ENOMEM;
    d->e = ne;
    d->cap = nc;
  }
  d->e[d->n].name = strdup(name);
  if(!d->e[d->n].name) return ENOMEM;
  d->e[d->n].st = *st;
  d->n++;
  return 0;
}

/* inside the driver's listing: nothing else is asked of the drive here */
static int
fmn_ntfs_dir_cb(void *ctx, const char *name, const ntfs_stat_t *ns) {
  fmn_dir_t *d = ctx;
  fmn_st_t st;
  memset(&st, 0, sizeof(st));
  st.type = (ns->attrs & NTFS_FA_REPARSE) ? 'o' : ns->is_dir ? 'd' : 'f';
  st.size = ns->is_dir ? 0 : ns->size;
  st.mtime = ns->mtime;
  st.id = ns->inode;
  st.attrs = ns->attrs;
  d->err = fmn_dir_add(d, name, &st);
  return d->err != 0;
}

static int
fmn_list(fmn_node_t *n, fmn_dir_t *d) {
  if(n->ntfs) {
    uint64_t ino;
    int rc = ntfs_lookup(nh(n), n->p, &ino);
    if(!rc) rc = ntfs_readdir(nh(n), ino, fmn_ntfs_dir_cb, d);
    if(rc) return -rc;
    return d->err;
  }
  DIR *dir = opendir(n->p);
  if(!dir) return errno ? errno : EIO;
  size_t was = n->len;
  int rc = 0;
  for(;;) {
    errno = 0;
    struct dirent *e = readdir(dir);
    if(!e) { if(errno) rc = errno; break; }       /* a read error is not the end of the folder */
    if(!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
    fmn_st_t st;
    memset(&st, 0, sizeof(st));
    if(fmn_push(n, e->d_name) == 0) {
      if(fmn_stat(n, &st) != 0) st.type = 'o';
      fmn_pop(n, was);
    } else st.type = 'o';
    if((rc = fmn_dir_add(d, e->d_name, &st)) != 0) break;
  }
  closedir(dir);
  return rc;
}

/* The NTFS parent of a node and its last name. n->p is cut for the lookup and put back. */
static int
fmn_ntfs_split(fmn_node_t *n, uint64_t *parent, const char **name) {
  char *slash = strrchr(n->p, '/');
  if(!slash || !slash[1]) return EINVAL;
  *name = slash + 1;
  int rc;
  if(slash == n->p) rc = ntfs_lookup(nh(n), "/", parent);
  else {
    *slash = 0;
    rc = ntfs_lookup(nh(n), n->p, parent);
    *slash = '/';
  }
  return rc ? -rc : 0;
}

typedef struct { int fd; ntfs_file_t *nf; uint64_t size; ps5tm_ntfsvol_t *rvol; } fmn_rf_t;

static int
fmn_ropen(fmn_node_t *n, fmn_rf_t *f) {
  f->fd = -1;
  f->nf = NULL;
  f->size = 0;
  f->rvol = NULL;
  if(n->ntfs) {
    uint64_t ino;
    ntfs_stat_t ns;
    /* counted before the file is looked up: the driver knows no open files, and a file deleted while it is read would hand out
       whatever the drive puts into its clusters next (audit 23.11.2026) */
    if(ps5tm_ntfs_reading(n->vol, 1)) return EBUSY;   /* a delete step that does not end */
    int rc = ntfs_lookup(nh(n), n->p, &ino);
    if(!rc) rc = ntfs_stat(nh(n), ino, &ns);
    if(!rc && (ns.is_dir || (ns.attrs & NTFS_FA_REPARSE))) rc = -EISDIR;
    if(!rc) rc = ntfs_open(nh(n), ino, &f->nf);
    if(rc) { ps5tm_ntfs_reading(n->vol, 0); return -rc; }
    f->rvol = n->vol;
    f->size = ntfs_file_size(f->nf);
    return 0;
  }
  struct stat pre, st;                           /* only a regular file is opened (review S2) */
  if(lstat(n->p, &pre) != 0) return errno ? errno : EIO;
  if(!S_ISREG(pre.st_mode)) return EISDIR;
  f->fd = open(n->p, O_RDONLY | O_NONBLOCK | O_NOFOLLOW);
  if(f->fd < 0) return errno ? errno : EIO;
  if(fstat(f->fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_dev != pre.st_dev || st.st_ino != pre.st_ino) {
    close(f->fd);
    f->fd = -1;
    return EAGAIN;
  }
  f->size = (uint64_t)st.st_size;
  return 0;
}

/* bytes read, 0 at the end, or -errno */
static ssize_t
fmn_read(fmn_rf_t *f, void *buf, size_t len, uint64_t off) {
  if(f->nf) return ntfs_pread(f->nf, buf, len, off);
  ssize_t r;
  do r = pread(f->fd, buf, len, (off_t)off); while(r < 0 && errno == EINTR);
  return r < 0 ? -(errno ? errno : EIO) : r;
}

static void
fmn_rclose(fmn_rf_t *f) {
  if(f->nf) ntfs_close(f->nf);
  if(f->fd >= 0) close(f->fd);
  if(f->rvol) ps5tm_ntfs_reading(f->rvol, 0);
  f->nf = NULL;
  f->fd = -1;
  f->rvol = NULL;
}

/* wvol: the drive counts this file as written to (ps5tm_ntfs_writing) from its creation to its end */
typedef struct { int fd; ntfs_wfile_t *nw; int replace; char tmp[PATH_MAX]; ps5tm_ntfsvol_t *wvol; } fmn_wf_t;

/* A new file of size bytes at the node. On the console as copy_file makes it (0777; a file that is there is written beside it and
   renamed over it at the end, when replace); on a drive with its final size and the source's time, never over a file that is there. */
static int
fmn_wcreate(fmn_node_t *t, uint64_t size, int64_t mtime, int replace, fmn_wf_t *w) {
  w->fd = -1;
  w->nw = NULL;
  w->replace = 0;
  w->tmp[0] = 0;
  w->wvol = NULL;
  if(t->ntfs) {
    uint64_t parent;
    const char *name;
    ps5tm_ntfs_writing(t->vol, 1);                 /* the places use their last answer meanwhile (the driver's lock is held long) */
    int rc = fmn_ntfs_split(t, &parent, &name);
    if(!rc) rc = -ntfs_create(nh(t), parent, name, size, mtime, &w->nw);
    if(rc) ps5tm_ntfs_writing(t->vol, 0);
    else w->wvol = t->vol;
    return rc;
  }
  const char *dst = t->p;
  struct stat ex;
  if(replace && lstat(t->p, &ex) == 0) {
    if(!S_ISREG(ex.st_mode)) return S_ISDIR(ex.st_mode) ? EISDIR : EEXIST;
    if(snprintf(w->tmp, sizeof(w->tmp), "%s.fm-tmp", t->p) >= (int)sizeof(w->tmp)) return ENAMETOOLONG;
    struct stat lt;
    if(lstat(w->tmp, &lt) == 0 && S_ISREG(lt.st_mode)) unlink(w->tmp);   /* a leftover of an earlier run of this copy (audit F-127) */
    dst = w->tmp;
    w->replace = 1;
  }
  w->fd = open(dst, O_WRONLY | O_CREAT | O_EXCL, 0777);
  if(w->fd < 0) return errno ? errno : EIO;
  (void)fchmod(w->fd, 0777);
  return 0;
}

static int
fmn_write(fmn_wf_t *w, const void *buf, size_t len) {
  if(w->nw) { int rc = ntfs_wwrite(w->nw, buf, len); return rc ? -rc : 0; }
  const char *p = buf;
  while(len) {
    ssize_t k = write(w->fd, p, len);
    if(k < 0 && errno == EINTR) continue;
    if(k <= 0) return errno ? errno : EIO;
    p += k;
    len -= (size_t)k;
  }
  return 0;
}

static int fmn_remove(fmn_node_t *n, char type);

/* Ends the file; ok = 0 (or a failure here) removes what was written. 0 or an errno. */
static int
fmn_wclose(fmn_node_t *t, fmn_wf_t *w, int ok) {
  int rc = 0;
  if(t->ntfs) {
    if(w->nw) { int r = ntfs_wclose(w->nw); if(r && ok) rc = -r; w->nw = NULL; }
    if(!ok || rc) (void)fmn_remove(t, 'f');
    if(w->wvol) { ps5tm_ntfs_writing(w->wvol, 0); w->wvol = NULL; }
    return rc;
  }
  if(w->fd >= 0) {
    if(ok && fsync(w->fd) != 0) rc = errno ? errno : EIO;     /* on the drive before it counts as copied */
    if(close(w->fd) != 0 && ok && !rc) rc = errno ? errno : EIO;
    w->fd = -1;
  }
  if(w->replace) {
    if(ok && !rc && rename(w->tmp, t->p) != 0) rc = errno ? errno : EIO;
    if(!ok || rc) unlink(w->tmp);
  } else if(!ok || rc) unlink(t->p);
  return rc;
}

static int
fmn_mkdir(fmn_node_t *t, int64_t mtime) {
  if(t->ntfs) {
    uint64_t parent;
    const char *name;
    ps5tm_ntfs_writing(t->vol, 1);
    int rc = fmn_ntfs_split(t, &parent, &name);
    if(!rc) rc = -ntfs_mkdir(nh(t), parent, name, mtime > 0 ? mtime : (int64_t)time(NULL), NULL);
    ps5tm_ntfs_writing(t->vol, 0);
    return rc;
  }
  if(mkdir(t->p, 0777) != 0) return errno ? errno : EIO;
  (void)chmod(t->p, 0777);
  return 0;
}

/* a file, or a folder that is empty */
static int
fmn_remove(fmn_node_t *n, char type) {
  if(n->ntfs) {
    uint64_t parent;
    const char *name;
    ps5tm_ntfs_writing(n->vol, 1);
    int rc = fmn_ntfs_split(n, &parent, &name);
    if(!rc) rc = -ntfs_unlink(nh(n), parent, name);
    ps5tm_ntfs_writing(n->vol, 0);
    return rc;
  }
  if(type == 'd') return rmdir(n->p) == 0 ? 0 : (errno ? errno : EIO);
  return unlink(n->p) == 0 ? 0 : (errno ? errno : EIO);
}

static int
fmn_free_space(fmn_node_t *n, uint64_t *fr) {
  if(n->ntfs) return ntfs_free_bytes(nh(n), fr) == 0 ? 0 : EIO;
  struct statvfs sv;
  if(statvfs(n->p, &sv) != 0) return errno ? errno : EIO;
  *fr = (uint64_t)sv.f_bavail * (sv.f_frsize ? sv.f_frsize : sv.f_bsize);
  return 0;
}

/* Files and bytes below the node (on the console: count_tree, not following links nor entering another drive). */
static void
fmn_count(fmn_node_t *n, dev_t dev, int depth, count_t *c) {
  if(!n->ntfs) { count_tree(n->p, dev, depth, c); return; }
  if(c->err) return;
  if(depth > FM_DEPTH) { c->err = ELOOP; return; }
  if(++c->seen > FM_COUNT_MAX) { c->err = E2BIG; return; }
  if(c->deadline_ms && (c->seen & 255) == 0 && ps5tm_mono_ms() > c->deadline_ms) { c->partial = 1; c->err = ETIMEDOUT; return; }
  fmn_st_t st;
  int rc = fmn_stat(n, &st);
  if(rc) { c->err = rc; return; }
  if(st.type == 'f') { c->files++; c->bytes += st.size; return; }
  if(st.type != 'd') { c->files++; return; }
  c->dirs++;
  fmn_dir_t d;
  memset(&d, 0, sizeof(d));
  rc = fmn_list(n, &d);
  if(rc) { fmn_dir_free(&d); c->err = rc; return; }
  c->files += d.total - (unsigned)d.n;            /* names too long to be taken: counted as the copy counts them (left out) */
  size_t was = n->len;
  for(size_t i = 0; i < d.n && !c->err; i++) {
    if(fmn_push(n, d.e[i].name) != 0) { c->err = ENAMETOOLONG; break; }
    fmn_count(n, dev, depth + 1, c);
    fmn_pop(n, was);
  }
  fmn_dir_free(&d);
}

/* What a page path is ("dev:ino" as identities() writes it, "n<serial>:<inode>" on a drive; "-" when it is not there). */
static void
fmn_identity(const char *vpath, char *out, size_t n) {
  fmn_node_t nd;
  fmn_st_t st;
  if(fmn_make(vpath, &nd) != 0) { snprintf(out, n, "-"); return; }
  if(fmn_stat(&nd, &st) != 0) snprintf(out, n, "-");
  else if(nd.ntfs) snprintf(out, n, "n%016llx:%llu", (unsigned long long)ps5tm_ntfs_serial(nd.vol), (unsigned long long)st.id);
  else snprintf(out, n, "%llu:%llu", (unsigned long long)st.dev, (unsigned long long)st.id);
  fmn_unmake(&nd);
}


/* ------------------------------------------------------------------ requests */

/* folders first, then by name as handle_list sorts (entry_cmp) */
static int
fmn_ent_cmp(const void *a, const void *b) {
  const fmn_ent_t *x = a, *y = b;
  int dx = x->st.type == 'd', dy = y->st.type == 'd';
  if(dx != dy) return dy - dx;
  int c = strcasecmp(x->name, y->name);
  return c ? c : strcmp(x->name, y->name);
}

/* GET list on a drive: as handle_list answers, with "fs":"ntfs" and what the page needs to grey out (rename, edit). */
static void
fmn_handle_list(int fd, const char *p) {
  fmn_node_t n;
  int rc = fmn_make(p, &n);
  if(rc) { ps5tm_http_send_error(fd, rc == ENODEV ? 404 : 400, "list_failed", ntfs_words(rc)); return; }
  fmn_st_t st;
  rc = fmn_stat(&n, &st);
  if(!rc && st.type != 'd') rc = ENOTDIR;
  fmn_dir_t d;
  memset(&d, 0, sizeof(d));
  d.max = FM_LIST_MAX;
  if(!rc) rc = fmn_list(&n, &d);
  if(rc) {
    fmn_dir_free(&d);
    fmn_unmake(&n);
    char msg[400];
    snprintf(msg, sizeof(msg), "Der Ordner lässt sich nicht öffnen: %s", ntfs_words(rc));
    ps5tm_http_send_error(fd, rc == ENOENT || rc == ENOTDIR ? 404 : 500, "list_failed", msg);
    return;
  }
  if(d.n) qsort(d.e, d.n, sizeof(fmn_ent_t), fmn_ent_cmp);
  char why[PS5TM_NTFS_WHY] = "";                   /* the longest reason is 221 bytes (audit 23.11.2026: 200 cut it) */
  int here = ps5tm_ntfs_writable(n.vol, why, sizeof(why));
  uint64_t fr = 0;
  int have_free = fmn_free_space(&n, &fr) == 0;
  cJSON *o = cJSON_CreateObject();
  cJSON_AddBoolToObject(o, "ok", 1);
  cJSON_AddStringToObject(o, "path", p);
  char parent[FM_PATH];
  dir_name(p, parent, sizeof(parent));
  cJSON_AddStringToObject(o, "parent", strcmp(n.p, "/") ? parent : "");   /* the drive's root has no folder above it */
  cJSON_AddStringToObject(o, "fs", "ntfs");
  cJSON_AddBoolToObject(o, "writable", here);
  cJSON_AddBoolToObject(o, "renamable", 0);
  if(!here) cJSON_AddStringToObject(o, "read_only_why", why);
  cJSON_AddNumberToObject(o, "free_bytes", have_free ? (double)fr : -1);
  cJSON_AddNumberToObject(o, "total", d.total);
  cJSON_AddBoolToObject(o, "truncated", d.total > d.n);
  cJSON *arr = cJSON_AddArrayToObject(o, "entries");
  for(size_t i = 0; i < d.n; i++) {
    const fmn_st_t *e = &d.e[i].st;
    cJSON *x = cJSON_CreateObject();
    char shown[FM_NAME + 8];
    const int bad_name = name_for_json(shown, sizeof(shown), d.e[i].name);     /* the walks keep the real name; only the answer is cleaned */
    cJSON_AddStringToObject(x, "name", shown);
    cJSON_AddStringToObject(x, "type", e->type == 'd' ? "dir" : e->type == 'f' ? "file" : "link");
    cJSON_AddNumberToObject(x, "size", (double)e->size);
    cJSON_AddNumberToObject(x, "mtime", (double)e->mtime);
    /* system entries of Windows ("System Volume Information", "$RECYCLE.BIN") are shown but not offered for deleting */
    cJSON_AddBoolToObject(x, "writable", here && e->type != 'o' && !bad_name && !(e->attrs & NTFS_FA_SYSTEM));
    fmt_add(x, NULL, shown, e->type);                                         /* only the system's records: nothing on the stick is read for it */
    cJSON_AddItemToArray(arr, x);
  }
  fmn_dir_free(&d);
  fmn_unmake(&n);
  send_obj(fd, 200, o);
}

/* The bytes of a file to the socket, from where it is open; 0 when all went out. */
static int
fmn_send_body(int fd, fmn_rf_t *f, uint64_t count, size_t chunk) {
  char *buf = malloc(chunk);
  if(!buf) return -1;
  uint64_t off = 0;
  int ok = 1;
  while(ok && off < count) {
    size_t want = count - off < chunk ? (size_t)(count - off) : chunk;
    ssize_t r = fmn_read(f, buf, want, off);
    if(r <= 0) { ok = 0; break; }
    /* at least 64 KiB in every 30 s (review 24.11.2026): a trickling reader held the thread, the drive's reference (eject: 409) and the
       read count for ever */
    if(ps5tm_http_write_all(fd, buf, (size_t)r) != 0) { ok = 0; break; }
    off += (uint64_t)r;
  }
  free(buf);
  return ok ? 0 : -1;
}

static void
fmn_handle_download(int fd, const char *p) {
  fmn_node_t n;
  fmn_rf_t f;
  int rc = fmn_make(p, &n);
  if(!rc) {
    rc = fmn_ropen(&n, &f);
    if(rc) fmn_unmake(&n);
  }
  if(rc) {
    ps5tm_http_send_error(fd, 404, "not_a_file", rc == EACCES || rc == ENODEV ? ntfs_words(rc) : "Das ist keine Datei, die sich herunterladen lässt.");
    return;
  }
  if(download_head(fd, base_name(p), (long long)f.size) == 0) {
    struct timeval tv = { 60, 0 };
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    (void)fmn_send_body(fd, &f, f.size, 256 * 1024);
  }
  fmn_rclose(&f);
  fmn_unmake(&n);
}

static void
fmn_handle_view(int fd, const char *p, size_t max) {
  fmn_node_t n;
  fmn_rf_t f;
  int rc = fmn_make(p, &n);
  if(!rc) {
    rc = fmn_ropen(&n, &f);
    if(rc) fmn_unmake(&n);
  }
  if(rc) {
    ps5tm_http_send_error(fd, 404, "not_a_file", rc == EACCES ? ntfs_words(rc) : "Das ist keine Datei, die sich ansehen lässt.");
    return;
  }
  const char *img = image_type(base_name(p));
  uint64_t send_n;
  if(img) {
    if(f.size > FM_VIEW_IMAGE_MAX) {
      fmn_rclose(&f);
      fmn_unmake(&n);
      ps5tm_http_send_error(fd, 413, "too_big", "Das Bild ist zu groß für die Vorschau (über 16 MB).");
      return;
    }
    send_n = f.size;
  } else {
    unsigned char probe[8192];
    ssize_t k = fmn_read(&f, probe, sizeof(probe), 0);
    if(k < 0 || !looks_like_text(probe, (size_t)k)) {
      fmn_rclose(&f);
      fmn_unmake(&n);
      ps5tm_http_send_error(fd, 415, "not_text", "Das ist keine Textdatei; sie lässt sich nur herunterladen.");
      return;
    }
    send_n = f.size > max ? max : f.size;
  }
  if(view_head(fd, img, (size_t)send_n, (long long)f.size) == 0) {
    struct timeval tv = { 30, 0 };
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    (void)fmn_send_body(fd, &f, send_n, 128 * 1024);
  }
  fmn_rclose(&f);
  fmn_unmake(&n);
}

static void
fmn_handle_size(int fd, const char *p) {
  fmn_node_t n;
  int rc = fmn_make(p, &n);
  if(rc) { ps5tm_http_send_error(fd, 404, "not_found", ntfs_words(rc)); return; }
  fmn_st_t st;
  if(fmn_stat(&n, &st) != 0) { fmn_unmake(&n); ps5tm_http_send_error(fd, 404, "not_found", "Das gibt es nicht (mehr)."); return; }
  count_t c = { 0 };
  c.deadline_ms = ps5tm_mono_ms() + FM_SIZE_BUDGET_MS;
  fmn_count(&n, 0, 0, &c);
  fmn_unmake(&n);
  if(c.err && !c.partial) {
    char msg[300];
    snprintf(msg, sizeof(msg), "Die Größe ließ sich nicht ermitteln: %s", ntfs_words(c.err));
    ps5tm_http_send_error(fd, c.err == E2BIG ? 413 : 500, "size_failed", msg);
    return;
  }
  cJSON *o = cJSON_CreateObject();
  cJSON_AddBoolToObject(o, "ok", 1);
  cJSON_AddStringToObject(o, "path", p);
  cJSON_AddNumberToObject(o, "bytes", (double)c.bytes);
  cJSON_AddNumberToObject(o, "files", (double)c.files);
  cJSON_AddNumberToObject(o, "folders", (double)(c.dirs > 0 ? c.dirs - 1 : 0));
  cJSON_AddBoolToObject(o, "partial", c.partial);
  send_obj(fd, 200, o);
}

/* Is the drive of a node writable? 0, or 403 with the reason in msg. */
static int
fmn_writable_or(fmn_node_t *n, char *msg, size_t mn) {
  char why[PS5TM_NTFS_WHY];
  if(ps5tm_ntfs_writable(n->vol, why, sizeof(why))) return 0;
  snprintf(msg, mn, "Das NTFS-Laufwerk ist nur lesbar: %s", why);
  return 403;
}

static void
fmn_handle_mkdir(int fd, const char *dir, const char *name_in) {
  char name[FM_NAME], t[FM_PATH], msg[400];
  if(fmn_safe_name(name, sizeof(name), name_in) != 0 || join(dir, name, t, sizeof(t)) != 0) {
    ps5tm_http_send_error(fd, 400, "bad_name", "Ungültiger Ordner oder Name.");
    return;
  }
  fmn_node_t n;
  int rc = fmn_make(t, &n);
  if(rc) { ps5tm_http_send_error(fd, rc == ENODEV ? 404 : 400, "mkdir_failed", ntfs_words(rc)); return; }
  int http = fmn_writable_or(&n, msg, sizeof(msg));
  if(http) { fmn_unmake(&n); ps5tm_http_send_error(fd, http, "read_only", msg); return; }
  if(ps5tm_filemgr_busy()) {
    fmn_unmake(&n);
    ps5tm_http_send_error(fd, 409, "files_busy", "Der Dateimanager arbeitet gerade (Kopieren, Verschieben oder Löschen). Bitte danach noch einmal.");
    return;
  }
  if(other_job_active(msg, sizeof(msg))) { fmn_unmake(&n); ps5tm_http_send_error(fd, 409, "job_busy", msg); return; }
  rc = fmn_mkdir(&n, (int64_t)time(NULL));
  {                                                  /* also after a failure: the drive's dirty flag is cleared by the sync */
    int s = -ntfs_sync(nh(&n));
    if(!rc) rc = s;
  }
  fmn_unmake(&n);
  if(rc) {
    snprintf(msg, sizeof(msg), rc == EEXIST ? "Diesen Namen gibt es hier schon." : "Der Ordner ließ sich nicht anlegen: %s", ntfs_words(rc));
    ps5tm_http_send_error(fd, rc == EEXIST ? 409 : 500, "mkdir_failed", msg);
    return;
  }
  PS5TM_INFO("files_mkdir", "Dateien: Ordner angelegt: %.150s", t);
  cJSON *o = cJSON_CreateObject();
  cJSON_AddBoolToObject(o, "ok", 1);
  cJSON_AddStringToObject(o, "path", t);
  send_obj(fd, 200, o);
}

/* "name (2)", "name (3)" … on a drive (keep_both_name for the console). */
static int
fmn_free_name(fmn_node_t *dir, const char *base, int is_dir, char *out, size_t on) {
  const char *dot = is_dir ? NULL : strrchr(base, '.');
  if(dot == base) dot = NULL;
  int stem = dot ? (int)(dot - base) : (int)strlen(base);
  size_t was = dir->len;
  for(int k = 2; k < 1000; k++) {
    char name[FM_NAME + 16];
    if(snprintf(name, sizeof(name), "%.*s (%d)%s", stem, base, k, dot ? dot : "") >= (int)sizeof(name) || strlen(name) >= FM_NAME) return -1;
    if(fmn_push(dir, name) != 0) return -1;
    fmn_st_t st;
    int rc = fmn_stat(dir, &st);
    fmn_pop(dir, was);
    if(rc == ENOENT) { snprintf(out, on, "%s", name); return 0; }
    if(rc) return -1;
  }
  return -1;
}

/* An upload onto a drive (receive_upload for the console). The whole size is known before (the request's length): the file is made
   with it, filled as the bytes come, and removed again when the transfer breaks off. */
static void
fmn_receive_upload(int fd, const char *dir, const char *name_in, const char *conflict, const char *prefix, size_t prefix_len, size_t total) {
  char name[FM_NAME], t[FM_PATH], msg[500];
  if(fmn_safe_name(name, sizeof(name), name_in) != 0) {
    drain_body(fd, prefix_len, total);
    ps5tm_http_send_error(fd, 400, "bad_name", "Ungültiger Ordner oder Name.");
    return;
  }
  fmn_node_t d;
  int rc = fmn_make(dir, &d);
  if(rc) {                                              /* a stick that is gone is no bad name (audit 23.11.2026) */
    drain_body(fd, prefix_len, total);
    ps5tm_http_send_error(fd, rc == ENODEV ? 404 : 400, rc == ENODEV ? "not_found" : "bad_name", ntfs_words(rc));
    return;
  }
  int http = fmn_writable_or(&d, msg, sizeof(msg));
  if(http) { fmn_unmake(&d); drain_body(fd, prefix_len, total); ps5tm_http_send_error(fd, http, "read_only", msg); return; }
  size_t was = d.len;
  fmn_st_t st;
  if(fmn_push(&d, name) != 0) { fmn_unmake(&d); drain_body(fd, prefix_len, total); ps5tm_http_send_error(fd, 400, "bad_name", "Pfad zu lang."); return; }
  int exists = fmn_stat(&d, &st) == 0;
  fmn_pop(&d, was);
  if(exists && !strcmp(conflict, "skip")) {
    fmn_unmake(&d);
    drain_body(fd, prefix_len, total);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "ok", 1);
    cJSON_AddBoolToObject(o, "skipped", 1);
    if(join(dir, name, t, sizeof(t)) == 0) cJSON_AddStringToObject(o, "path", t);
    send_obj(fd, 200, o);
    return;
  }
  if(exists && !strcmp(conflict, "replace")) {
    fmn_unmake(&d);
    drain_body(fd, prefix_len, total);
    ps5tm_http_send_error(fd, 409, "no_replace", "Auf einem NTFS-Laufwerk ersetzt die App keine Datei. Bitte „Beide behalten“ oder „Überspringen“ wählen.");
    return;
  }
  if(exists && conflict[0] == 0) {
    fmn_unmake(&d);
    drain_body(fd, prefix_len, total);
    ps5tm_http_send_error(fd, 409, "exists", "Eine Datei dieses Namens gibt es hier schon.");
    return;
  }
  if(exists && fmn_free_name(&d, name, 0, name, sizeof(name)) != 0) {   /* "keep" */
    fmn_unmake(&d);
    drain_body(fd, prefix_len, total);
    ps5tm_http_send_error(fd, 500, "name_failed", "Für diese Datei ließ sich im Ziel kein freier Name finden.");
    return;
  }
  uint64_t fr = 0;
  if(fmn_free_space(&d, &fr) == 0 && (fr < FM_SPACE_SPARE || fr - FM_SPACE_SPARE < (uint64_t)total)) {
    fmn_unmake(&d);
    drain_body(fd, prefix_len, total);
    ps5tm_http_send_error(fd, 409, "no_space", "Auf dem Laufwerk ist dafür nicht genug Platz.");
    return;
  }
  if(join(dir, name, t, sizeof(t)) != 0 || fmn_push(&d, name) != 0) {
    fmn_unmake(&d);
    drain_body(fd, prefix_len, total);
    ps5tm_http_send_error(fd, 400, "bad_name", "Pfad zu lang.");
    return;
  }
  fmn_wf_t w;
  rc = fmn_wcreate(&d, (uint64_t)total, (int64_t)time(NULL), 0, &w);
  if(rc) {
    fmn_unmake(&d);
    drain_body(fd, prefix_len, total);
    snprintf(msg, sizeof(msg), "Die Datei ließ sich nicht anlegen: %s", ntfs_words(rc));
    ps5tm_http_send_error(fd, rc == EEXIST ? 409 : 500, "create_failed", msg);
    return;
  }
  ps5tm_powerguard_hold();
  /* File data goes straight to the device (the driver's cache holds only the small metadata writes): the pieces the network hands over
     (a few KB, of any length) became one read and rewrite of the same edge sectors each, and a cheap stick fell to 17 KB/s (console test
     23.11.2026, a 60 GB stick; copying onto it ran at 3 MB/s). So the body is gathered into whole blocks of FM_BUF first, as a copy writes them. */
  char *buf = malloc(FM_BUF);
  size_t got = 0, fill = 0;
  int werr = buf ? 0 : ENOMEM, too_slow = 0;
  size_t first = prefix_len < total ? prefix_len : total;
  if(!werr && first > FM_BUF) werr = fmn_write(&w, prefix, first);   /* (the part read with the head is far smaller) */
  else if(!werr && first) { memcpy(buf, prefix, first); fill = first; }
  if(!werr) got = first;
  if(!werr && fill && got == total) { werr = fmn_write(&w, buf, fill); fill = 0; }
  uint64_t t0 = ps5tm_mono_ms();
  while(!werr && got < total) {
    if(ps5tm_ntfs_closing()) { werr = ECANCELED; break; }   /* the app ends: removed, not left behind half written (audit 23.11.2026) */
    size_t want = total - got < FM_BUF - fill ? total - got : FM_BUF - fill;
    int r = read_some(fd, buf + fill, want);
    if(r <= 0) break;
    uint64_t el = ps5tm_mono_ms() - t0;           /* at least 16 KB/s after two minutes, as on the console (review 15.10.2026) */
    if(el > 120000 && (uint64_t)got < (el / 1000) * 16384) { too_slow = 1; break; }
    fill += (size_t)r;
    got += (size_t)r;
    if(fill == FM_BUF || got == total) { werr = fmn_write(&w, buf, fill); fill = 0; }
  }
  free(buf);
  int ok = !werr && got == total;
  int crc = fmn_wclose(&d, &w, ok);
  if(ok && crc) { werr = crc; ok = 0; }
  {                                                  /* also after a failure (the half file is removed): the dirty flag goes */
    int s = ntfs_sync(nh(&d));
    if(ok && s) { werr = -s; ok = 0; }
  }
  fmn_unmake(&d);
  ps5tm_powerguard_release();
  if(!ok) {
    if(werr) snprintf(msg, sizeof(msg), "Schreiben ging nicht: %s", ntfs_words(werr));
    else if(too_slow) snprintf(msg, sizeof(msg), "Die Übertragung war zu langsam und wurde nach %zu von %zu Bytes beendet. Nichts wurde gespeichert.", got, total);
    else snprintf(msg, sizeof(msg), "Die Übertragung brach nach %zu von %zu Bytes ab; nichts wurde gespeichert.", got, total);
    PS5TM_WARN("files_upload_failed", "Dateien: Hochladen von %.120s gescheitert: %.200s", t, msg);
    ps5tm_http_send_error(fd, werr ? 500 : 400, "upload_incomplete", msg);
    return;
  }
  PS5TM_INFO("files_upload", "Dateien: hochgeladen: %.140s (%zu KB).", t, total / 1024);
  cJSON *o = cJSON_CreateObject();
  cJSON_AddBoolToObject(o, "ok", 1);
  cJSON_AddStringToObject(o, "path", t);
  cJSON_AddNumberToObject(o, "bytes", (double)total);
  send_obj(fd, 200, o);
}


/* ------------------------------------------------------------------ the job: copying, moving and deleting with a drive */

static int
fmn_copy_file(fmn_node_t *s, fmn_node_t *t, const char *vs, const fmn_st_t *st) {
  job_current(vs);
  fmn_rf_t in;
  int rc = fmn_ropen(s, &in);
  if(rc) return rc;
  fmn_wf_t out;
  rc = fmn_wcreate(t, in.size, st->mtime, g_replace, &out);
  if(rc) { fmn_rclose(&in); return rc; }
  uint64_t off = 0;
  while(!rc && off < in.size) {
    if(cancelled()) { rc = ECANCELED; break; }
    size_t want = in.size - off < FM_BUF ? (size_t)(in.size - off) : FM_BUF;
    ssize_t r = fmn_read(&in, g_buf, want, off);
    if(r < 0) { rc = (int)-r; break; }
    if(r == 0) { rc = EIO; break; }                /* the source got shorter while it was copied */
    rc = fmn_write(&out, g_buf, (size_t)r);
    if(rc) break;
    job_add((uint64_t)r, 0);
    off += (uint64_t)r;
  }
  int crc = fmn_wclose(t, &out, rc == 0);
  if(!rc) rc = crc;
  fmn_rclose(&in);
  if(!rc) job_add(0, 1);
  return rc;
}

/* A file or folder from s to t (both nodes are walked down and back). On a drive the names are adjusted to what Windows can hold; links,
   devices and reparse points are left out (counted as skipped), and on the console another drive below is not entered. */
static int
fmn_copy(fmn_node_t *s, fmn_node_t *t, char *vs, size_t vl, dev_t dev, int depth) {
  if(cancelled()) return ECANCELED;
  if(depth > FM_DEPTH) return ELOOP;
  fmn_st_t st;
  int rc = fmn_stat(s, &st);
  if(rc) return rc;
  if(st.type == 'f') return fmn_copy_file(s, t, vs, &st);
  if(st.type != 'd' || (!s->ntfs && st.dev != dev)) {
    pthread_mutex_lock(&g_lock);
    g_job.skipped++;
    g_job.files_done++;
    pthread_mutex_unlock(&g_lock);
    return 0;
  }
  rc = fmn_mkdir(t, st.mtime);
  if(rc) {
    fmn_st_t ts;
    if(rc != EEXIST || !g_replace) return rc;
    if(fmn_stat(t, &ts) != 0 || ts.type != 'd') return EEXIST;
    /* merged into the folder that is there: not ours, so a failure does not delete it */
  } else if(depth == 0) g_made_top = 1;
  fmn_dir_t d;
  memset(&d, 0, sizeof(d));
  rc = fmn_list(s, &d);
  if(!rc && d.total > d.n) {                       /* names of 256 bytes and more (NTFS holds up to 765): left out, but counted (audit 23.11.2026) */
    pthread_mutex_lock(&g_lock);
    g_job.skipped += d.total - (unsigned)d.n;
    g_job.files_done += d.total - (unsigned)d.n;
    pthread_mutex_unlock(&g_lock);
  }
  size_t sw = s->len, tw = t->len;
  for(size_t i = 0; !rc && i < d.n; i++) {
    char tn[FM_NAME];
    if(t->ntfs ? fmn_safe_name(tn, sizeof(tn), d.e[i].name) != 0 : snprintf(tn, sizeof(tn), "%s", d.e[i].name) >= (int)sizeof(tn)) {
      rc = ENAMETOOLONG;
      break;
    }
    size_t nl = strlen(d.e[i].name);
    if(vl + 1 + nl >= PATH_MAX) { rc = ENAMETOOLONG; break; }
    if((rc = fmn_push(s, d.e[i].name)) != 0 || (rc = fmn_push(t, tn)) != 0) { fmn_pop(s, sw); fmn_pop(t, tw); break; }
    vs[vl] = '/';
    memcpy(vs + vl + 1, d.e[i].name, nl + 1);
    rc = fmn_copy(s, t, vs, vl + 1 + nl, dev, depth + 1);
    vs[vl] = 0;
    fmn_pop(s, sw);
    fmn_pop(t, tw);
  }
  fmn_dir_free(&d);
  return rc;
}

/* Removes the node and all below it. live: the job's own deleting (counted, current file shown, Abbrechen stops it); not live: tidying
   up after a copy, which must finish even after Abbrechen. */
static int
fmn_delete(fmn_node_t *n, char *vp, size_t vl, dev_t dev, int depth, int live) {
  if(live && cancelled()) return ECANCELED;
  if(depth > FM_DEPTH) return ELOOP;
  fmn_st_t st;
  int rc = fmn_stat(n, &st);
  if(rc) return rc;
  if(st.type == 'd') {
    if(!n->ntfs && st.dev != dev) return EXDEV;
    fmn_dir_t d;
    memset(&d, 0, sizeof(d));
    rc = fmn_list(n, &d);
    size_t was = n->len;
    for(size_t i = 0; !rc && i < d.n; i++) {
      size_t nl = strlen(d.e[i].name);
      if(vl + 1 + nl >= PATH_MAX) { rc = ENAMETOOLONG; break; }
      if((rc = fmn_push(n, d.e[i].name)) != 0) break;
      vp[vl] = '/';
      memcpy(vp + vl + 1, d.e[i].name, nl + 1);
      rc = fmn_delete(n, vp, vl + 1 + nl, dev, depth + 1, live);
      vp[vl] = 0;
      fmn_pop(n, was);
    }
    fmn_dir_free(&d);
    if(rc) return rc;
    return fmn_remove(n, 'd');
  }
  if(live) job_current(vp);
  /* the job's own deleting waits for no download: it stops with a message while a file of the drive is read, its clusters stay
     (audit 23.11.2026); tidying up a copy (not live) deletes only what it wrote itself */
  int held = 0;
  if(live && n->ntfs) {
    if(ps5tm_ntfs_delete_begin(n->vol)) return EBUSY;
    held = 1;
  }
  rc = fmn_remove(n, st.type);
  if(held) ps5tm_ntfs_delete_end(n->vol);
  if(rc) return rc;
  if(live) job_add(st.type == 'f' ? st.size : 0, 1);
  return 0;
}

/* After a move: removes from the source exactly what the copy holds (same name, a file of the same size not newer than its copy), then
   the folders that became empty; what is not in the copy stays and is counted in *kept (delete_copied for the console). */
static int
fmn_delete_copied(fmn_node_t *s, fmn_node_t *t, int depth, unsigned *kept, unsigned *read_kept) {
  if(depth > FM_DEPTH) return ELOOP;
  fmn_st_t ss, ts;
  int rc = fmn_stat(s, &ss);
  if(rc) return rc == ENOENT ? 0 : rc;
  if(fmn_stat(t, &ts) != 0) { (*kept)++; return 0; }
  if(ss.type == 'f') {
    if(ts.type == 'f' && ts.size == ss.size && ss.mtime <= ts.mtime) {
      if(!s->ntfs) return fmn_remove(s, 'f');
      if(ps5tm_ntfs_delete_begin(s->vol)) { (*read_kept)++; return 0; }   /* read meanwhile: the original stays */
      rc = fmn_remove(s, 'f');
      ps5tm_ntfs_delete_end(s->vol);
      return rc;
    }
    (*kept)++;
    return 0;
  }
  if(ss.type != 'd' || ts.type != 'd') { (*kept)++; return 0; }
  fmn_dir_t d;
  memset(&d, 0, sizeof(d));
  rc = fmn_list(s, &d);
  size_t sw = s->len, tw = t->len;
  for(size_t i = 0; !rc && i < d.n; i++) {
    char tn[FM_NAME];
    if(t->ntfs ? fmn_safe_name(tn, sizeof(tn), d.e[i].name) != 0 : snprintf(tn, sizeof(tn), "%s", d.e[i].name) >= (int)sizeof(tn)) {
      rc = ENAMETOOLONG;
      break;
    }
    if((rc = fmn_push(s, d.e[i].name)) != 0 || (rc = fmn_push(t, tn)) != 0) { fmn_pop(s, sw); fmn_pop(t, tw); break; }
    rc = fmn_delete_copied(s, t, depth + 1, kept, read_kept);
    fmn_pop(s, sw);
    fmn_pop(t, tw);
  }
  fmn_dir_free(&d);
  if(rc) return rc;
  rc = fmn_remove(s, 'd');
  if(rc == ENOTEMPTY || rc == EEXIST) { (*kept)++; rc = 0; }
  return rc;
}

/* 1 when the job a touches an NTFS drive (a source or the target). */
static int
fmn_job_has_ntfs(const fm_args_t *a) {
  if(a->dest[0] && ps5tm_ntfs_path(a->dest)) return 1;
  for(unsigned i = 0; i < a->n; i++)
    if(ps5tm_ntfs_path(a->src[i])) return 1;
  return 0;
}

static void
fmn_run_copy(fm_args_t *a, int move) {
  job_state(FM_COPYING);
  unsigned conflict_skipped = 0, move_kept = 0, read_kept = 0;
  char err[800] = "", *vs = malloc(PATH_MAX);
  fmn_node_t t;
  int rc = vs ? fmn_make(a->dest, &t) : ENOMEM;
  fmn_st_t dst;
  if(!rc && (fmn_stat(&t, &dst) != 0 || dst.type != 'd')) { fmn_unmake(&t); rc = ENOENT; }
  if(rc) {
    free(vs);
    job_finish(FM_FAILED, rc == ENODEV ? "Das NTFS-Laufwerk ist nicht mehr da." : "Der Zielordner ist nicht mehr da.", NULL);
    return;
  }
  size_t tw = t.len;
  for(unsigned i = 0; i < a->n; i++) {
    fmn_node_t s;
    fmn_st_t ss, ts;
    rc = fmn_make(a->src[i], &s);
    if(!rc && (rc = fmn_stat(&s, &ss)) != 0) fmn_unmake(&s);
    if(rc) {
      snprintf(err, sizeof(err), "%.300s ist nicht mehr da.", a->src[i]);
      break;
    }
    if(!t.ntfs) {                                 /* the drive went away (or another one sits there now) */
      struct stat now;
      if(stat(t.p, &now) != 0 || now.st_dev != dst.dev) {
        snprintf(err, sizeof(err), "Das Ziellaufwerk ist nicht mehr da.%s", i ? " Die Einträge davor sind fertig." : "");
        fmn_unmake(&s);
        break;
      }
    }
    char name[FM_NAME];
    if(t.ntfs ? fmn_safe_name(name, sizeof(name), base_name(a->src[i])) != 0
              : snprintf(name, sizeof(name), "%s", base_name(a->src[i])) >= (int)sizeof(name)) rc = ENAMETOOLONG;
    int replacing = 0, skip = 0;
    if(rc || (rc = fmn_push(&t, name)) != 0) { fmn_unmake(&s); snprintf(err, sizeof(err), "Bei %.300s: %s", a->src[i], ntfs_words(rc)); break; }
    if(fmn_stat(&t, &ts) == 0) {
      if(a->conflict == FM_C_SKIP || (a->conflict == FM_C_REPLACE && !s.ntfs && !t.ntfs && ts.dev == ss.dev && ts.id == ss.id)) skip = 1;
      else if(a->conflict == FM_C_KEEP) {
        fmn_pop(&t, tw);
        if(fmn_free_name(&t, name, ss.type == 'd', name, sizeof(name)) != 0 || fmn_push(&t, name) != 0) {
          snprintf(err, sizeof(err), "Für „%.200s“ ließ sich im Ziel kein freier Name finden.", base_name(a->src[i]));
          fmn_unmake(&s);
          break;
        }
      } else if(a->conflict == FM_C_REPLACE && !t.ntfs && (ss.type == 'd') == (ts.type == 'd') && ts.type != 'o') replacing = 1;
      else {
        snprintf(err, sizeof(err), t.ntfs && a->conflict == FM_C_REPLACE
                                     ? "Auf einem NTFS-Laufwerk ersetzt die App nichts; „%.200s“ gibt es dort schon. Nichts wurde überschrieben."
                                     : "Im Ziel gibt es inzwischen schon „%.200s“. Nichts wurde überschrieben.", name);
        fmn_unmake(&s);
        break;
      }
    }
    if(skip) {
      conflict_skipped++;
      fmn_pop(&t, tw);
      fmn_unmake(&s);
      pthread_mutex_lock(&g_lock);
      g_job.items_done++;
      pthread_mutex_unlock(&g_lock);
      continue;
    }
    g_replace = replacing;
    g_made_top = 0;
    pthread_mutex_lock(&g_lock);
    unsigned skipped_before = g_job.skipped;
    pthread_mutex_unlock(&g_lock);
    snprintf(vs, PATH_MAX, "%s", a->src[i]);
    rc = fmn_copy(&s, &t, vs, strlen(vs), ss.dev, 0);
    pthread_mutex_lock(&g_lock);
    int skipped_some = g_job.skipped != skipped_before;
    pthread_mutex_unlock(&g_lock);
    if(rc) {
      /* what this item left behind is its own and goes again, unless the very first step found the name taken (made by someone else) */
      /* a replaced file neither: the copy went to a temporary file beside it (fmn_wcreate), the name still holds the old file
         (review 24.11.2026: the clean-up deleted it after "Ersetzen" with a source that could not be read) */
      int ours = ss.type == 'd' ? g_made_top : (!replacing && rc != EEXIST);
      fmn_st_t ps;
      if(ours && fmn_stat(&t, &ps) == 0) (void)fmn_delete(&t, vs, strlen(vs), ps.dev, 0, 0);
    } else if(move && skipped_some) {
      snprintf(err, sizeof(err), "%.250s wurde kopiert, aber Verknüpfungen, Gerätedateien oder zu lange Namen darin ließen sich nicht "
               "mitnehmen. Das Original bleibt deshalb liegen (es liegt jetzt an beiden Orten).", a->src[i]);
      fmn_pop(&t, tw);
      fmn_unmake(&s);
      break;
    } else if(move) {
      job_current(a->src[i]);
      /* (audit 24.11.2026) The driver keeps the new names, records and bitmap bits in its cache until a sync; the original goes only once
         the copy is on the stick. (ntfs_flush_cache would also empty the stick's own cache, at the price of a slow command on some bridges.) */
      if(t.ntfs && ntfs_sync(nh(&t)) != 0) {
        snprintf(err, sizeof(err), "%.250s wurde kopiert, ließ sich aber nicht sicher auf das NTFS-Laufwerk schreiben. Das Original bleibt liegen.",
                 a->src[i]);
        fmn_pop(&t, tw);
        fmn_unmake(&s);
        break;
      }
      int drc = fmn_delete_copied(&s, &t, 0, &move_kept, &read_kept);
      if(drc) {
        snprintf(err, sizeof(err), "%.250s wurde kopiert, aber das Original ließ sich nicht ganz löschen: %s. Es liegt jetzt an beiden "
                 "Orten (das Original vielleicht nur noch zum Teil).", a->src[i], ntfs_words(drc));
        fmn_pop(&t, tw);
        fmn_unmake(&s);
        break;
      }
    }
    fmn_pop(&t, tw);
    if(s.ntfs && move) (void)ntfs_sync(nh(&s));
    fmn_unmake(&s);
    if(rc) {
      if(rc == ECANCELED) {
        char note[400];
        snprintf(note, sizeof(note), "Abgebrochen. %u von %u Einträgen sind fertig; der angefangene wurde im Ziel wieder entfernt.", i, a->n);
        if(t.ntfs) (void)ntfs_sync(nh(&t));
        fmn_unmake(&t);
        free(vs);
        g_replace = 0;
        job_finish(FM_CANCELLED, NULL, note);
        PS5TM_INFO("files_copy_cancelled", "Dateien: %s abgebrochen bei %.120s", move ? "Verschieben" : "Kopieren", a->src[i]);
        return;
      }
      snprintf(err, sizeof(err), "Bei %.300s: %s%s", a->src[i], ntfs_words(rc), i ? " Die Einträge davor sind fertig." : "");
      break;
    }
    pthread_mutex_lock(&g_lock);
    g_job.items_done++;
    pthread_mutex_unlock(&g_lock);
  }
  g_replace = 0;
  int flush = t.ntfs ? ntfs_sync(nh(&t)) : 0;     /* on the drive before it counts as done: a stick pulled a moment later keeps it */
  fmn_unmake(&t);
  free(vs);
  if(!err[0] && flush) snprintf(err, sizeof(err), "Beim Schreiben auf das NTFS-Laufwerk gab es einen Fehler: %s", ntfs_words(-flush));
  if(err[0]) {
    PS5TM_WARN(move ? "files_move_failed" : "files_copy_failed", "Dateien: %.170s", err);
    job_finish(FM_FAILED, err, NULL);
    return;
  }
  char note[800] = "";
  pthread_mutex_lock(&g_lock);
  unsigned skipped = g_job.skipped;
  uint64_t done = g_job.bytes_done;
  pthread_mutex_unlock(&g_lock);
  if(skipped) snprintf(note, sizeof(note), "%u Einträge wurden ausgelassen (Verknüpfungen, Gerätedateien, zu lange Namen oder ein anderes "
                       "Laufwerk darin).", skipped);
  if(conflict_skipped) {
    size_t nl = strlen(note);
    snprintf(note + nl, sizeof(note) - nl, "%s%u Einträge gab es im Ziel schon; sie blieben unverändert.", nl ? " " : "", conflict_skipped);
  }
  if(move_kept) {
    size_t nl = strlen(note);
    snprintf(note + nl, sizeof(note) - nl, "%s%u Einträge sind während des Verschiebens im Original neu entstanden oder haben sich "
             "geändert; sie blieben dort liegen.", nl ? " " : "", move_kept);
  }
  if(read_kept) {
    size_t nl = strlen(note);
    snprintf(note + nl, sizeof(note) - nl, "%s%u Dateien wurden während des Verschiebens heruntergeladen oder angesehen; ihr Original "
             "blieb liegen.", nl ? " " : "", read_kept);
  }
  PS5TM_INFO("files_copy_done", "Dateien: %u Einträge %s nach %.100s (%llu MB).", a->n, move ? "verschoben" : "kopiert", a->dest,
             (unsigned long long)(done >> 20));
  job_finish(FM_DONE, NULL, note);
}

static void
fmn_run_delete(fm_args_t *a) {
  job_state(FM_DELETING);
  char err[800] = "", *vp = malloc(PATH_MAX);
  for(unsigned i = 0; vp && i < a->n; i++) {
    fmn_node_t n;
    fmn_st_t st;
    int rc = fmn_make(a->src[i], &n);
    if(rc == ENODEV) { snprintf(err, sizeof(err), "Beim Löschen von %.300s: %s", a->src[i], ntfs_words(rc)); break; }
    if(rc) continue;
    rc = fmn_stat(&n, &st);
    if(rc == ENOENT) { fmn_unmake(&n); continue; }   /* gone already */
    if(!rc) {
      snprintf(vp, PATH_MAX, "%s", a->src[i]);
      rc = fmn_delete(&n, vp, strlen(vp), st.dev, 0, 1);
    }
    if(n.ntfs) (void)ntfs_sync(nh(&n));
    fmn_unmake(&n);
    if(rc == ECANCELED) {
      free(vp);
      job_finish(FM_CANCELLED, NULL, "Abgebrochen. Was bis dahin gelöscht war, ist weg; der Rest ist noch da.");
      PS5TM_INFO("files_delete_cancelled", "Dateien: Löschen abgebrochen bei %.120s", a->src[i]);
      return;
    }
    if(rc) {
      pthread_mutex_lock(&g_lock);
      char cur[FM_PATH];
      snprintf(cur, sizeof(cur), "%s", g_job.current[0] ? g_job.current : a->src[i]);
      pthread_mutex_unlock(&g_lock);
      snprintf(err, sizeof(err), "Beim Löschen von %.300s: %s", cur, rc == EXDEV ? "Ein Teil liegt auf einem anderen Laufwerk und wurde nicht angefasst." : ntfs_words(rc));
      break;
    }
    pthread_mutex_lock(&g_lock);
    g_job.items_done++;
    pthread_mutex_unlock(&g_lock);
  }
  if(!vp) snprintf(err, sizeof(err), "Kein Speicher.");
  free(vp);
  if(err[0]) {
    PS5TM_WARN("files_delete_failed", "Dateien: %.170s", err);
    job_finish(FM_FAILED, err, NULL);
    return;
  }
  pthread_mutex_lock(&g_lock);
  uint64_t files = g_job.files_done, bytes = g_job.bytes_done;
  pthread_mutex_unlock(&g_lock);
  PS5TM_INFO("files_delete_done", "Dateien: %u Einträge gelöscht (%llu Dateien, %llu MB).", a->n, (unsigned long long)files,
             (unsigned long long)(bytes >> 20));
  job_finish(FM_DONE, NULL, NULL);
}

/* Is the NTFS folder ino the target folder or one above it? NTFS finds a folder under any spelling of its name (upper and lower case,
   the 8.3 short name), so the names alone do not tell (audit 23.11.2026). */
static int
fmn_ntfs_inside(fmn_node_t *t, uint64_t ino) {
  char *p = t->p;
  size_t l = strlen(p);
  for(size_t i = 1; i <= l; i++) {
    if(p[i] != '/' && p[i] != 0) continue;
    char c = p[i];
    p[i] = 0;
    uint64_t got;
    int hit = ntfs_lookup(nh(t), p, &got) == 0 && got == ino;
    p[i] = c;
    if(hit) return 1;
  }
  return 0;
}

/* POST copy/move with a drive in it: what handle_copy checks for the console (the target, each source, names that are taken, room),
   on nodes. 0 with bytes and files counted, or an HTTP status with err (and the taken names in conflicts). */
static int
fmn_copy_check(fm_args_t *a, int move, cJSON *conflicts, uint64_t *bytes, uint64_t *files, char *err, size_t en) {
  fmn_node_t t;
  int rc = fmn_make(a->dest, &t);
  if(rc) { snprintf(err, en, "%s", rc == ENODEV ? ntfs_words(rc) : "Ungültiger Zielordner."); return rc == ENODEV ? 404 : 400; }
  fmn_st_t dst;
  int http = 0;
  if(fmn_stat(&t, &dst) != 0 || dst.type != 'd') { http = 403; snprintf(err, en, "Den Zielordner gibt es nicht (mehr)."); }
  else if(t.ntfs) http = fmn_writable_or(&t, err, en);
  else if(!dir_writable(a->dest)) {
    http = 403;
    snprintf(err, en, "In diesen Ordner darf nichts %s werden (geschützt sind nur „/“, /dev, /proc, eingehängte Laufwerke und Verknüpfungen).", move ? "verschoben" : "kopiert");
  }
  size_t tw = t.len;
  for(unsigned i = 0; !http && i < a->n; i++) {
    fmn_node_t s;
    fmn_st_t ss, ts;
    size_t sl = strlen(a->src[i]);
    rc = fmn_make(a->src[i], &s);
    if(!rc && (rc = fmn_stat(&s, &ss)) != 0) fmn_unmake(&s);
    if(rc) { http = 404; snprintf(err, en, "%.300s gibt es nicht (mehr).", a->src[i]); break; }
    if(!s.ntfs && ss.type == 'o') { http = 400; snprintf(err, en, "%.300s ist eine Verknüpfung; die wird nicht kopiert.", a->src[i]); }
    else if(s.ntfs && ss.type == 'o') { http = 400; snprintf(err, en, "%.300s ist eine Verknüpfung von Windows; die wird nicht kopiert.", a->src[i]); }
    else if(move && s.ntfs && !strcmp(s.p, "/")) { http = 400; snprintf(err, en, "Ein ganzes Laufwerk lässt sich nicht verschieben."); }
    else if(move && s.ntfs) http = fmn_writable_or(&s, err, en);
    else if(move && !writable(a->src[i])) {
      http = 403;
      snprintf(err, en, "%.300s darf nicht verschoben werden (geschützt sind nur „/“, /dev, /proc, eingehängte Laufwerke und Verknüpfungen).", a->src[i]);
    }
    if(!http && ((!strncmp(a->dest, a->src[i], sl) && (a->dest[sl] == 0 || a->dest[sl] == '/')) ||
                 (s.ntfs && t.ntfs && s.vol == t.vol && ss.type == 'd' && fmn_ntfs_inside(&t, ss.id)))) {
      http = 400;
      snprintf(err, en, "Ein Ordner kann nicht in sich selbst %s werden.", move ? "verschoben" : "kopiert");
    }
    /* (24.11.2026, upstream PR #16) Windows' own entries (system attribute: $Extend and what is in it, System Volume Information) are not
       moved: the move ends with deleting the original, and the delete refuses them too */
    if(!http && move && s.ntfs && (ss.attrs & NTFS_FA_SYSTEM)) {
      http = 403;
      snprintf(err, en, "%.300s gehört zu Windows (Systemordner) und bleibt.", a->src[i]);
    }
    char name[FM_NAME], tv[FM_PATH];
    int long_name = t.ntfs ? fmn_safe_name(name, sizeof(name), s.ntfs && !strcmp(s.p, "/") ? ps5tm_ntfs_name(s.vol) : base_name(a->src[i])) != 0
                           : snprintf(name, sizeof(name), "%s", base_name(a->src[i])) >= (int)sizeof(name);
    if(!http && long_name) { http = 400; snprintf(err, en, "Pfad zu lang."); }   /* never a cut name, which could be another one (upstream PR #14) */
    if(!http && (join(a->dest, name, tv, sizeof(tv)) != 0 || (!t.ntfs && !writable(tv)))) {
      http = 403;
      snprintf(err, en, "Dorthin darf „%.200s“ nicht.", name);
    }
    if(!http && fmn_push(&t, name) == 0) {
      if(fmn_stat(&t, &ts) == 0) {
        if(a->conflict == FM_C_NONE) cJSON_AddItemToArray(conflicts, cJSON_CreateString(name));
        else if(a->conflict == FM_C_REPLACE && t.ntfs) {
          http = 409;
          snprintf(err, en, "Auf einem NTFS-Laufwerk ersetzt die App nichts („%.200s“ gibt es dort schon). Bitte „Beide behalten“ oder „Überspringen“ wählen.", name);
        } else if(a->conflict == FM_C_REPLACE && ((ss.type == 'd') != (ts.type == 'd') || ts.type == 'o')) {
          http = 409;
          snprintf(err, en, "„%.200s“ ist im Ziel %s und lässt sich nicht durch %s ersetzen. Wähle „Beide behalten“ oder „Überspringen“.", name,
                   ts.type == 'd' ? "ein Ordner" : "eine Datei oder Verknüpfung", ss.type == 'd' ? "einen Ordner" : "eine Datei");
        }
      }
      fmn_pop(&t, tw);
    } else if(!http) { http = 400; snprintf(err, en, "Pfad zu lang."); }
    if(!http) {
      count_t c = { 0 };
      fmn_count(&s, ss.dev, 0, &c);
      if(c.err) { http = 500; snprintf(err, en, "%.300s ließ sich nicht durchzählen: %s", a->src[i], ntfs_words(c.err)); }
      *bytes += c.bytes;
      *files += c.files;
    }
    fmn_unmake(&s);
  }
  if(!http && cJSON_GetArraySize(conflicts) > 0) {
    http = 409;
    snprintf(err, en, "Im Ziel gibt es schon Einträge mit diesen Namen; nichts wurde überschrieben.");
  }
  uint64_t fr = 0;
  if(!http && fmn_free_space(&t, &fr) == 0 && (fr < FM_SPACE_SPARE || fr - FM_SPACE_SPARE < *bytes)) {
    http = 409;
    snprintf(err, en, "Im Ziel ist nicht genug Platz: gebraucht %llu MB, frei %llu MB.", (unsigned long long)(*bytes >> 20),
             (unsigned long long)(fr >> 20));
  }
  fmn_unmake(&t);
  return http;
}

/* delete/plan and delete with a drive in it: may each path go, and how much is below it. 0, or an HTTP status with err. */
static int
fmn_delete_check(const fm_args_t *a, uint64_t *bytes, uint64_t *files, uint64_t *dirs, char *err, size_t en) {
  for(unsigned i = 0; i < a->n; i++) {
    fmn_node_t n;
    fmn_st_t st;
    int rc = fmn_make(a->src[i], &n);
    if(rc == ENODEV) { snprintf(err, en, "%s", ntfs_words(rc)); return 404; }
    if(rc) { snprintf(err, en, "Ein ausgewählter Pfad ist ungültig."); return 400; }
    int http = 0;
    if(!n.ntfs && !writable(a->src[i])) {
      http = 403;
      snprintf(err, en, "%.300s darf nicht gelöscht werden (geschützt sind nur „/“, /dev, /proc, eingehängte Laufwerke und Verknüpfungen).", a->src[i]);
    } else if(n.ntfs && !strcmp(n.p, "/")) { http = 403; snprintf(err, en, "Ein ganzes Laufwerk lässt sich nicht löschen."); }
    else if(n.ntfs) http = fmn_writable_or(&n, err, en);
    if(!http && fmn_stat(&n, &st) != 0) { http = 404; snprintf(err, en, "%.300s gibt es nicht (mehr).", a->src[i]); }
    if(!http && n.ntfs && (st.attrs & NTFS_FA_SYSTEM)) { http = 403; snprintf(err, en, "%.300s gehört zu Windows (Systemordner) und bleibt.", a->src[i]); }
    if(!http) {
      if(st.type == 'd') (*dirs)++;
      count_t c = { 0 };
      fmn_count(&n, st.dev, 0, &c);
      *files += c.files;
      *bytes += c.bytes;
      if(c.err == E2BIG) { http = 400; snprintf(err, en, "%.300s enthält zu viele Dateien für einen Vorgang.", a->src[i]); }
    }
    fmn_unmake(&n);
    if(http) return http;
  }
  return 0;
}

/* POST eject {path:"/ntfsN"}: "Sicher auswerfen" for an NTFS stick. What is pending is written out and the drive let go (ntfsvol.c); not
   while the file manager copies, uploads or deletes. Answered at once (an exFAT stick goes through ShadowMountPlus instead,
   /api/v1/storage/eject). */
static void
fmn_handle_eject(int fd, const ps5tm_request_t *req) {
  cJSON *b = parse_body(req);
  const char *p = jstr(b, "path");
  char name[8] = "";
  if(p && ps5tm_ntfs_path(p) && p[6] == 0) snprintf(name, sizeof(name), "%s", p + 1);
  cJSON_Delete(b);
  if(!name[0]) { ps5tm_http_send_error(fd, 400, "bad_path", "Das ist kein NTFS-Laufwerk."); return; }
  {
    char mount[16];
    snprintf(mount, sizeof(mount), "/%s", name);
    ps5tm_filemgr_search_stop(mount);             /* a search on the stick ends first (it holds the drive) */
  }
  if(ps5tm_filemgr_busy()) {
    ps5tm_http_send_error(fd, 409, "files_busy", "Der Dateimanager arbeitet gerade (Kopieren, Verschieben, Löschen oder Hochladen). Bitte danach noch einmal.");
    return;
  }
  char msg[300];
  int http = ps5tm_ntfs_eject(name, msg, sizeof(msg));
  if(http) { ps5tm_http_send_error(fd, http, http == 409 ? "busy" : http == 404 ? "not_found" : "flush_failed", msg); return; }
  cJSON *o = cJSON_CreateObject();
  char path[16];
  snprintf(path, sizeof(path), "/%s", name);
  cJSON_AddBoolToObject(o, "ok", 1);
  cJSON_AddStringToObject(o, "path", path);
  cJSON_AddStringToObject(o, "message", msg);
  send_obj(fd, 200, o);
}

/* The NTFS places of the file manager (places_json). */
static void
fmn_places(cJSON *arr) {
  ps5tm_ntfs_scan(0);
  ps5tm_ntfsinfo_t info[PS5TM_NTFS_MAX];
  unsigned n = ps5tm_ntfs_list(info, PS5TM_NTFS_MAX);
  for(unsigned i = 0; i < n; i++) {
    char label[120], path[16];
    if(info[i].label[0]) snprintf(label, sizeof(label), "%s (NTFS · %.60s)", info[i].name, info[i].label);
    else snprintf(label, sizeof(label), "%s (NTFS)", info[i].name);
    snprintf(path, sizeof(path), "/%s", info[i].name);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "label", label);
    cJSON_AddStringToObject(o, "path", path);
    cJSON_AddNumberToObject(o, "free_bytes", info[i].free_known ? (double)info[i].free_bytes : -1);
    cJSON_AddNumberToObject(o, "total_bytes", (double)info[i].total_bytes);
    cJSON_AddBoolToObject(o, "writable", info[i].writable);
    cJSON_AddBoolToObject(o, "ejectable", 1);
    cJSON_AddStringToObject(o, "fs", "ntfs");
    if(!info[i].writable) cJSON_AddStringToObject(o, "read_only_why", info[i].why);
    cJSON_AddItemToArray(arr, o);
  }
}
