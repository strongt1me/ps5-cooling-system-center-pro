/* The LZ4 pack jobs (12.10.2026), included at the end of imgjob.c: they run in the same job slot as the image jobs, so the
 * window shows their progress the same way and no other job of the app runs beside them.
 *
 *   lz4pack    "Mit LZ4 packen", originals in the game folder. The AMPR index is built afresh, the data files are packed
 *              into ampr_assets-NNN.pak (lz4pack.c), every packed file is compared byte for byte with its original, the
 *              APR-EMU that reads packs is put into fakelib (the game's own file kept as .orig, amprlib.c), and only then are
 *              the originals removed. Whatever fails before that takes the packs away again: the game is as it was.
 *   lz4copy    The same, into a new folder <name>-lz4 beside the game (made as <name>-lz4.ps5cc-teil, renamed when whole):
 *              the originals stay untouched; the files that are not packed are copied and read back.
 *   lz4verify  Reads every block of the packs, decodes it and checks its CRC.
 *   lz4unpack  Writes the packed files back into the game folder, compares them with the packs, then takes the packs away. */

typedef struct {
  char kind[10];
  int  level;
  char profile[PATH_MAX];        /* "" = automatic, else the path of a TOML profile */
} lz4arg_t;

/* ---------------------------------------------------------------- what the engine shows */

static void lz4_ui_phase(void *c, const char *t) { (void)c; set_phase(t); }

static void
lz4_ui_total(void *c, uint64_t b) {
  (void)c;
  pthread_mutex_lock(&g_lock);
  g_job.work_total = b ? b : 1;
  g_job.work_done = 0;
  g_job.rate = 0;
  g_job.window_ms = 0;
  pthread_mutex_unlock(&g_lock);
}

static void lz4_ui_work(void *c, uint64_t b) { (void)c; work(b); }

static void
lz4_ui_files(void *c, unsigned done, unsigned total) {
  (void)c;
  pthread_mutex_lock(&g_lock);
  g_job.files_done = done;
  g_job.files_total = total;
  pthread_mutex_unlock(&g_lock);
}

static void lz4_ui_current(void *c, const char *p) { (void)c; set_current(p); }

static lzp_ui_t
lz4_ui(void) {
  lzp_ui_t u;
  memset(&u, 0, sizeof(u));
  u.phase = lz4_ui_phase;
  u.total = lz4_ui_total;
  u.work = lz4_ui_work;
  u.files = lz4_ui_files;
  u.current = lz4_ui_current;
  u.cancel = &g_cancel;
  return u;
}

/* ---------------------------------------------------------------- profiles the person keeps */

#define LZ4_PROF_MAX 48

typedef struct {
  char id[110];                  /* "app:name.toml", "usb0:name.toml" */
  char name[96];
  char where[16];
  char path[PATH_MAX];
} lz4prof_t;

/* the tested profiles built into the app (gen/lz4prof_blob.c): id "builtin:<id>", the path is the id too */
#define LZ4_BUILTIN "builtin:"

static const ps5tm_lz4prof_t *
lz4_builtin(const char *id) {
  if(!id || strncmp(id, LZ4_BUILTIN, sizeof(LZ4_BUILTIN) - 1)) return NULL;
  for(unsigned i = 0; i < ps5tm_lz4prof_count; i++)
    if(!strcmp(ps5tm_lz4profs[i].id, id + sizeof(LZ4_BUILTIN) - 1)) return &ps5tm_lz4profs[i];
  return NULL;
}

static unsigned
lz4_profiles(lz4prof_t *out, unsigned max) {
  unsigned n = 0;
  for(unsigned i = 0; i < ps5tm_lz4prof_count && n < max; i++) {
    lz4prof_t *o = &out[n++];
    snprintf(o->name, sizeof(o->name), "%s", ps5tm_lz4profs[i].label);
    snprintf(o->where, sizeof(o->where), "builtin");
    snprintf(o->id, sizeof(o->id), LZ4_BUILTIN "%s", ps5tm_lz4profs[i].id);
    snprintf(o->path, sizeof(o->path), "%s", o->id);
  }
  static const struct { const char *where; const char *dir; } dirs[] = {
    { "app", PS5TM_DATA_DIR "/LZ4_Profile" },
    { "usb0", "/mnt/usb0/LZ4_Profile" }, { "usb1", "/mnt/usb1/LZ4_Profile" }, { "usb2", "/mnt/usb2/LZ4_Profile" },
    { "usb3", "/mnt/usb3/LZ4_Profile" }, { "usb4", "/mnt/usb4/LZ4_Profile" }, { "usb5", "/mnt/usb5/LZ4_Profile" },
    { "ext0", "/mnt/ext0/LZ4_Profile" }, { "ext1", "/mnt/ext1/LZ4_Profile" } };
  mkdir(PS5TM_DATA_DIR, 0777);
  mkdir(PS5TM_DATA_DIR "/LZ4_Profile", 0777);
  for(unsigned i = 0; i < sizeof(dirs) / sizeof(dirs[0]) && n < max; i++) {
    DIR *d = opendir(dirs[i].dir);
    if(!d) continue;
    struct dirent *e;
    while((e = readdir(d)) && n < max) {
      size_t l = strlen(e->d_name);
      if(l < 6 || l >= sizeof(out[0].name) || strcasecmp(e->d_name + l - 5, ".toml") || e->d_name[0] == '.') continue;
      char p[PATH_MAX];
      snprintf(p, sizeof(p), "%s/%s", dirs[i].dir, e->d_name);
      struct stat st;
      if(stat(p, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size > (8 << 20)) continue;
      lz4prof_t *o = &out[n++];
      snprintf(o->name, sizeof(o->name), "%s", e->d_name);
      snprintf(o->where, sizeof(o->where), "%s", dirs[i].where);
      snprintf(o->id, sizeof(o->id), "%s:%s", dirs[i].where, e->d_name);
      snprintf(o->path, sizeof(o->path), "%s", p);
    }
    closedir(d);
  }
  return n;
}

static int
lz4_profile_path(const char *id, char *out, size_t n) {
  if(!id || !id[0]) { out[0] = 0; return 0; }
  lz4prof_t *l = malloc(LZ4_PROF_MAX * sizeof(*l));
  if(!l) return -1;
  unsigned c = lz4_profiles(l, LZ4_PROF_MAX), i;
  int rc = -1;
  for(i = 0; i < c; i++)
    if(!strcmp(l[i].id, id)) { snprintf(out, n, "%s", l[i].path); rc = 0; break; }
  free(l);
  return rc;
}

/* ---------------------------------------------------------------- the last check of a game's packs */

typedef struct {
  char     id[16];
  int64_t  when;
  int      bytewise;              /* compared with the originals */
  uint64_t files, bytes;
} lz4last_t;

static lz4last_t g_lz4_last[IJ_LAST_MAX];
static unsigned  g_lz4_last_next;

static void
lz4_last_set(const char *id, int bytewise, uint64_t files, uint64_t bytes) {
  pthread_mutex_lock(&g_lock);
  lz4last_t *l = NULL;
  for(unsigned i = 0; i < IJ_LAST_MAX; i++) if(!strcmp(g_lz4_last[i].id, id)) { l = &g_lz4_last[i]; break; }
  if(!l) l = &g_lz4_last[g_lz4_last_next++ % IJ_LAST_MAX];
  snprintf(l->id, sizeof(l->id), "%s", id);
  l->when = (int64_t)time(NULL);
  l->bytewise = bytewise;
  l->files = files;
  l->bytes = bytes;
  pthread_mutex_unlock(&g_lock);
}

/* ---------------------------------------------------------------- helpers of the jobs */

static int
lz4_fail(const char *err, int rc) {
  char m[300];
  if(!err[0]) {                                          /* no reason given: at least the step it failed in */
    pthread_mutex_lock(&g_lock);
    snprintf(m, sizeof(m), "Gescheitert bei: %s", g_job.phase[0] ? g_job.phase : "Vorbereitung");
    pthread_mutex_unlock(&g_lock);
    err = m;
  }
  finish(rc == ECANCELED || cancelled() ? IJ_CANCELLED : IJ_FAILED, err);
  return -1;
}

static void
lz4_job_stats(const lzp_stats_t *s, uint64_t loose) {
  pthread_mutex_lock(&g_lock);
  g_job.lz4_logical = s->logical_bytes;
  g_job.lz4_stored = s->stored_bytes;
  g_job.lz4_loose = (unsigned)loose;
  g_job.lz4_packed = (unsigned)s->files_packed;
  pthread_mutex_unlock(&g_lock);
}

/* the APR-EMU that reads LZ4 packs in the game: kept when there is one (any build with the reader), else test-pack goes in (amprlib.c) */
static int
lz4_install_apr(const char *id, const char *root, int backport, char *why, size_t wn) {
  int http = 0;
  cJSON *st = ps5tm_amprlib_state("ampr", id, &http);
  int have = 0;
  if(st) {
    const cJSON *ins = cJSON_GetObjectItem(st, "installed");
    const cJSON *pk = ins ? cJSON_GetObjectItem(ins, "reads_packs") : NULL;
    const cJSON *pr = ins ? cJSON_GetObjectItem(ins, "present") : NULL;
    have = cJSON_IsTrue(pk) && cJSON_IsTrue(pr) && !backport;
    cJSON_Delete(st);
  }
  if(have) return 0;
  char note[300] = "";
  int rc = ps5tm_amprlib_run("ampr", "apply", id, backport ? NULL : root, "libSceAmpr.sprx-0.4.2.1-test-pack", note, sizeof(note), why, wn);
  return rc;
}

/* a copy of the folder without the files that are packed; every file is read back and compared (CRC-32) */
typedef struct {
  const lzp_list_t *packed;
  uint8_t          *buf;
  size_t            srclen, dstlen;
  unsigned          files, links;
  char              why[400];
  int               rc;
} cp_t;

static int
cp_file(cp_t *c, const char *src, const char *dst, const struct stat *st) {
  int in = open(src, O_RDONLY);
  if(in < 0) { snprintf(c->why, sizeof(c->why), "Eine Datei ließ sich nicht lesen: %s", src + c->srclen + 1); return -1; }
  int out = open(dst, O_WRONLY | O_CREAT | O_EXCL, LZP_FILE_MODE);
  if(out < 0) { close(in); snprintf(c->why, sizeof(c->why), "Eine Datei ließ sich nicht anlegen (%s): %s", ps5tm_io_strerror(errno), dst + c->dstlen + 1); return -1; }
  (void)fchmod(out, LZP_FILE_MODE);
  uint32_t crc = 0;
  uint64_t total = 0;
  for(;;) {
    ssize_t r = read(in, c->buf, IJ_BUF_BYTES);
    if(r < 0 && errno == EINTR) continue;
    if(r < 0) { snprintf(c->why, sizeof(c->why), "Lesen schlug fehl (%s): %s", ps5tm_io_strerror(errno), src + c->srclen + 1); goto bad; }
    if(r == 0) break;
    crc = libdeflate_crc32(crc, c->buf, (size_t)r);
    const uint8_t *p = c->buf;
    size_t left = (size_t)r;
    while(left) {
      ssize_t w = write(out, p, left);
      if(w < 0 && errno == EINTR) continue;
      if(w <= 0) { snprintf(c->why, sizeof(c->why), "Schreiben schlug fehl (%s): %s", ps5tm_io_strerror(errno), dst + c->dstlen + 1); goto bad; }
      p += w;
      left -= (size_t)w;
    }
    total += (uint64_t)r;
    work((uint64_t)r);
    if(cancelled()) { c->rc = ECANCELED; goto bad; }
  }
  close(in);
  in = -1;
  {
    int werr = fsync(out) != 0 ? errno : 0;              /* closed in any case (review 15.10.2026) */
    if(close(out) != 0 && !werr) werr = errno ? errno : EIO;
    out = -1;
    if(werr) { snprintf(c->why, sizeof(c->why), "Schreiben schlug fehl (%s): %s", ps5tm_io_strerror(werr), dst + c->dstlen + 1); goto bad; }
  }
  out = -1;
  /* read back */
  int rb = open(dst, O_RDONLY);
  if(rb < 0) { snprintf(c->why, sizeof(c->why), "Eine Kopie ließ sich nicht zurücklesen: %s", dst + c->dstlen + 1); goto bad2; }
  uint32_t crc2 = 0;
  uint64_t got = 0;
  for(;;) {
    ssize_t r = read(rb, c->buf, IJ_BUF_BYTES);
    if(r < 0 && errno == EINTR) continue;
    if(r <= 0) break;
    crc2 = libdeflate_crc32(crc2, c->buf, (size_t)r);
    got += (uint64_t)r;
    work((uint64_t)r);
    if(cancelled()) { close(rb); c->rc = ECANCELED; goto bad2; }
  }
  close(rb);
  if(got != total || crc2 != crc) { snprintf(c->why, sizeof(c->why), "Eine Kopie weicht vom Original ab: %s", dst + c->dstlen + 1); goto bad2; }
  struct timeval tv[2];
  tv[0].tv_sec = tv[1].tv_sec = st->st_mtime;
  tv[0].tv_usec = tv[1].tv_usec = 0;
  utimes(dst, tv);
  c->files++;
  return 0;
bad:
  if(in >= 0) close(in);
  if(out >= 0) close(out);
bad2:
  unlink(dst);
  if(!c->rc) c->rc = -1;
  return -1;
}

static int
cp_walk(cp_t *c, char *src, size_t sl, char *dst, size_t dl, int depth) {
  if(depth > 60) { snprintf(c->why, sizeof(c->why), "Die Ordner sind zu tief verschachtelt."); c->rc = -1; return -1; }
  DIR *d = opendir(src);
  if(!d) { snprintf(c->why, sizeof(c->why), "Ein Ordner ließ sich nicht lesen: %s", src + c->srclen); c->rc = -1; return -1; }
  struct dirent *e;
  int rc = 0;
  for(;;) {
    errno = 0;
    if(!(e = readdir(d))) {
      if(errno) { snprintf(c->why, sizeof(c->why), "Ein Ordner ließ sich nicht ganz lesen (%s): %s", ps5tm_io_strerror(errno), src + c->srclen); c->rc = -1; rc = -1; }
      break;
    }
    if(!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
    size_t n = strlen(e->d_name);
    if(sl + 1 + n >= PATH_MAX - 8 || dl + 1 + n >= PATH_MAX - 8) { snprintf(c->why, sizeof(c->why), "Ein Pfad ist zu lang: %s", e->d_name); c->rc = -1; rc = -1; break; }
    src[sl] = '/'; memcpy(src + sl + 1, e->d_name, n + 1);
    dst[dl] = '/'; memcpy(dst + dl + 1, e->d_name, n + 1);
    struct stat st;
    if(lstat(src, &st) != 0) {
      int le = errno;
      if(le != ENOENT) {                                  /* gone meanwhile is fine; unreadable would leave the copy short */
        snprintf(c->why, sizeof(c->why), "Eine Datei ließ sich nicht ansehen (%s): %s", ps5tm_io_strerror(le), src + c->srclen + 1);
        c->rc = -1; rc = -1;
      }
      src[sl] = 0; dst[dl] = 0;
      if(rc != 0) break;
      continue;
    }
    if(S_ISDIR(st.st_mode)) {
      if(mkdir(dst, 0777) != 0 && errno != EEXIST) { snprintf(c->why, sizeof(c->why), "Ein Ordner ließ sich nicht anlegen (%s): %s", ps5tm_io_strerror(errno), dst + c->dstlen + 1); c->rc = -1; rc = -1; }
      else { (void)chmod(dst, LZP_FILE_MODE); rc = cp_walk(c, src, sl + 1 + n, dst, dl + 1 + n, depth + 1); }
    } else if(S_ISREG(st.st_mode)) {
      const char *rel = src + c->srclen + 1;
      if(!lzp_list_has(c->packed, rel)) rc = cp_file(c, src, dst, &st);
    } else c->links++;
    src[sl] = 0;
    dst[dl] = 0;
    if(rc != 0) break;
    if(cancelled()) { c->rc = ECANCELED; rc = -1; break; }
  }
  closedir(d);
  return rc;
}

/* ---------------------------------------------------------------- how long packing takes (after exFAT2AMPR 1.104 by V-10)

   The speed of the packing phase per LZ4 level and of the check against the originals, as this console measured them
   (PS5TM_DATA_DIR/lz4-speed.txt, lines "pack <level> <bytes/s>" and "verify <bytes/s>"), else a first guess. The guesses
   are those exFAT2AMPR 1.104 uses for its own packer on the PS5 (read on 18.10.2026): levels 10 to 12 switch LZ4 HC to
   its optimal parser, several times slower for less than one per cent of room. A level not measured yet is scaled from the
   nearest one that was, by the ratio of the guesses. Each new run moves the stored value halfway towards what it measured. */
#define LZ4_SPEED_FILE  PS5TM_DATA_DIR "/lz4-speed.txt"
static const double LZ4_PACK_GUESS[13] = { 0, 220e6, 220e6, 220e6, 190e6, 180e6, 170e6, 150e6, 130e6, 110e6, 35e6, 30e6, 25e6 };
#define LZ4_VERIFY_GUESS 400e6
#ifndef LZ4_SPEED_MIN_MS
#define LZ4_SPEED_MIN_MS    20000           /* shorter phases say little about the speed */
#endif
#ifndef LZ4_SPEED_MIN_BYTES
#define LZ4_SPEED_MIN_BYTES (512ull << 20)
#endif
static pthread_mutex_t g_lz4_speed_lock = PTHREAD_MUTEX_INITIALIZER;

static void
lz4_speed_load(double pack[13], double *verify) {
  memset(pack, 0, 13 * sizeof(*pack));
  *verify = 0;
  FILE *f = fopen(LZ4_SPEED_FILE, "r");
  if(!f) return;
  char line[96];
  while(fgets(line, sizeof(line), f)) {
    int lv = 0;
    double v = 0;
    if(sscanf(line, "pack %d %lf", &lv, &v) == 2 && lv >= 1 && lv <= 12 && v > 1e5 && v < 1e11) pack[lv] = v;
    else if(sscanf(line, "verify %lf", &v) == 1 && v > 1e5 && v < 1e11) *verify = v;
  }
  fclose(f);
}

/* bytes per second of packing at this level; *learned 1 when this console measured this or another level */
static double
lz4_pack_rate(int level, int *learned) {
  if(level < 1 || level > 12) level = 9;
  double pack[13], ver;
  pthread_mutex_lock(&g_lz4_speed_lock);
  lz4_speed_load(pack, &ver);
  pthread_mutex_unlock(&g_lz4_speed_lock);
  *learned = 1;
  if(pack[level] > 0) return pack[level];
  for(int d = 1; d <= 11; d++)
    for(int s = -1; s <= 1; s += 2) {
      int o = level + s * d;
      if(o >= 1 && o <= 12 && pack[o] > 0) return pack[o] * LZ4_PACK_GUESS[level] / LZ4_PACK_GUESS[o];
    }
  *learned = 0;
  return LZ4_PACK_GUESS[level];
}

static double
lz4_verify_rate(void) {
  double pack[13], ver;
  pthread_mutex_lock(&g_lz4_speed_lock);
  lz4_speed_load(pack, &ver);
  pthread_mutex_unlock(&g_lz4_speed_lock);
  return ver > 0 ? ver : LZ4_VERIFY_GUESS;
}

/* seconds for packing and checking bytes at level (the removal of the originals is quick beside them) */
static double
lz4_estimate_s(uint64_t bytes, int level, int *learned) {
  return (double)bytes / lz4_pack_rate(level, learned) + (double)bytes / lz4_verify_rate();
}

/* what a finished pack measured; short runs say little and are not kept */
static void
lz4_speed_note(int level, uint64_t bytes, uint64_t pack_ms, uint64_t verify_ms) {
  if(level < 1 || level > 12 || bytes < LZ4_SPEED_MIN_BYTES) return;
  double pack[13], ver;
  pthread_mutex_lock(&g_lz4_speed_lock);
  lz4_speed_load(pack, &ver);
  if(pack_ms >= LZ4_SPEED_MIN_MS) {
    double v = (double)bytes * 1000.0 / (double)pack_ms;
    pack[level] = pack[level] > 0 ? (pack[level] + v) / 2 : v;
  }
  if(verify_ms >= LZ4_SPEED_MIN_MS) {
    double v = (double)bytes * 1000.0 / (double)verify_ms;
    ver = ver > 0 ? (ver + v) / 2 : v;
  }
  char b[600];
  size_t at = 0;
  for(int i = 1; i <= 12; i++)
    if(pack[i] > 0 && at < sizeof(b)) at += (size_t)snprintf(b + at, sizeof(b) - at, "pack %d %.0f\n", i, pack[i]);
  if(ver > 0 && at < sizeof(b)) at += (size_t)snprintf(b + at, sizeof(b) - at, "verify %.0f\n", ver);
  if(at > sizeof(b)) at = sizeof(b);
  char tmp[sizeof(LZ4_SPEED_FILE) + 8];
  snprintf(tmp, sizeof(tmp), "%s.tmp", LZ4_SPEED_FILE);
  FILE *f = fopen(tmp, "w");
  int ok = f && fwrite(b, 1, at, f) == at;
  if(f && fclose(f) != 0) ok = 0;
  if(ok && rename(tmp, LZ4_SPEED_FILE) != 0) ok = 0;
  if(!ok) unlink(tmp);
  pthread_mutex_unlock(&g_lz4_speed_lock);
}

/* ---------------------------------------------------------------- what the game folder holds (after exFAT2AMPR 1.95)

   Before anything is packed: every entry of the game folder is a file or a folder that can be read. A link would not be
   packed and, in a copy, would not be copied: the packed game would lack it. Something else (a device, a socket), a name
   that cannot be looked at or a folder that cannot be read all the way leave the same doubt. 0, or -1 with why. */
static int
lz4_tree_walk(char *path, size_t len, size_t rootlen, int depth, char *why, size_t wn) {
  if(depth > 64) { snprintf(why, wn, "Der Spielordner ist zu tief verschachtelt: %s", path + rootlen); return -1; }
  DIR *d = opendir(path);
  if(!d) { snprintf(why, wn, "Ein Ordner im Spiel ließ sich nicht lesen (%s): %s", ps5tm_io_strerror(errno), len > rootlen ? path + rootlen + 1 : "/"); return -1; }
  int rc = 0;
  for(;;) {
    errno = 0;
    struct dirent *e = readdir(d);
    if(!e) {
      if(errno) { snprintf(why, wn, "Ein Ordner im Spiel ließ sich nicht ganz lesen (%s): %s", ps5tm_io_strerror(errno), len > rootlen ? path + rootlen + 1 : "/"); rc = -1; }
      break;
    }
    if(!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
    size_t n = strlen(e->d_name);
    if(len + 1 + n >= PATH_MAX - 8) { snprintf(why, wn, "Ein Pfad im Spiel ist zu lang: %s", e->d_name); rc = -1; break; }
    path[len] = '/';
    memcpy(path + len + 1, e->d_name, n + 1);
    struct stat st;
    if(lstat(path, &st) != 0) {
      snprintf(why, wn, "Eine Datei im Spiel ließ sich nicht ansehen (%s): %s", ps5tm_io_strerror(errno), path + rootlen + 1);
      rc = -1;
    } else if(S_ISLNK(st.st_mode)) {
      snprintf(why, wn, "Im Spielordner liegt eine Verknüpfung: %s. Verknüpfungen werden nicht mitgepackt, das gepackte Spiel wäre "
               "unvollständig. Bitte sie durch die Datei selbst ersetzen oder entfernen.", path + rootlen + 1);
      rc = -1;
    } else if(S_ISDIR(st.st_mode)) {
      rc = lz4_tree_walk(path, len + 1 + n, rootlen, depth + 1, why, wn);
    } else if(!S_ISREG(st.st_mode)) {
      snprintf(why, wn, "Im Spielordner liegt etwas, das weder Datei noch Ordner ist: %s. So lässt sich das Spiel nicht vollständig packen.",
               path + rootlen + 1);
      rc = -1;
    }
    path[len] = 0;
    if(rc != 0 || cancelled()) break;
  }
  closedir(d);
  return rc;
}

static int
lz4_tree_check(const char *root, char *why, size_t wn) {
  char *p = malloc(PATH_MAX);
  if(!p) { snprintf(why, wn, "Kein Speicher."); return -1; }
  snprintf(p, PATH_MAX, "%s", root);
  int rc = lz4_tree_walk(p, strlen(p), strlen(p), 0, why, wn);
  free(p);
  return rc;
}

/* ---------------------------------------------------------------- pack (in place or into a copy) */

static int
lz4_make_cfg(const lz4arg_t *a, lzc_cfg_t *cfg, char *err, size_t en) {
  const ps5tm_lz4prof_t *b = lz4_builtin(a->profile);
  if(b) return lzc_load_text(b->text, b->size, cfg, err, en);
  if(a->profile[0]) return lzc_load(a->profile, cfg, err, en);
  lzc_automatic(cfg, a->level, 16, 1);
  if(!cfg->rules || !cfg->groups) { snprintf(err, en, "Kein Speicher."); return -1; }
  return 0;
}

/* lzp_info on a thread with a big stack (the callers are connection threads) */
typedef struct { char dir[256]; lzp_info_t info; int packed; } lz4infoarg_t;

static void *
lz4_info_thread(void *arg) {
  lz4infoarg_t *x = arg;
  x->packed = lzp_info(x->dir, &x->info);
  return NULL;
}

static int
lz4_info_big(const char *dir, lzp_info_t *out) {
  lz4infoarg_t *x = calloc(1, sizeof(*x));
  if(!x) { memset(out, 0, sizeof(*out)); return 0; }
  snprintf(x->dir, sizeof(x->dir), "%s", dir);
  if(run_big(lz4_info_thread, x) != 0) { free(x); memset(out, 0, sizeof(*out)); return 0; }
  *out = x->info;
  int p = x->packed;
  free(x);
  return p;
}

static uint64_t
lz4_free(const char *path) {
  struct statvfs sv;
  if(statvfs(path, &sv) != 0) return 0;
  return (uint64_t)sv.f_bavail * (sv.f_frsize ? sv.f_frsize : sv.f_bsize);
}

/* Takes the packs of a failed pack away again and says truthfully whether that worked (review 15.10.2026). */
static const char *
lz4_rollback(int copy, const char *tmp, const char *root) {
  if(copy) { rm_tree(tmp, 0); return "die Pakete wurden wieder entfernt"; }
  char e2[300] = "";
  if(lzp_remove_set(root, e2, sizeof(e2)) == 0) return "die Pakete wurden wieder entfernt";
  PS5TM_WARN("lz4_rollback_failed", "LZ4: Die Pakete in %.120s ließen sich nicht wieder entfernen: %.120s", root, e2);
  return "ACHTUNG: Die Pakete ließen sich nicht ganz wieder entfernen (siehe Protokoll). Bitte „LZ4 entpacken“ wählen oder die "
         "Dateien ampr_assets* im Spielordner löschen";
}

static void
lz4_run_pack(const char *id, const char *root, const lz4arg_t *a, int copy) {
  lzp_ui_t ui = lz4_ui();
  char err[500] = "", tmp[PATH_MAX + 32] = "", fin[PATH_MAX + 16] = "";
  lzc_cfg_t cfg;
  memset(&cfg, 0, sizeof(cfg));
  lzp_stats_t st;
  memset(&st, 0, sizeof(st));
  int have_cfg = 0, backport = 0, made_tmp = 0;
  uint64_t loose_bytes = 0;

  char place[16] = "", pwhy[200] = "";
  if(ps5tm_amprlib_place("ampr", id, place, sizeof(place), pwhy, sizeof(pwhy)) != 0) { lz4_fail("Das Spiel ist nicht mehr in der Liste.", -1); return; }
  backport = !strcmp(place, "backport");
  if(copy && backport) { lz4_fail("Dieses Spiel hat einen Backport-Ordner (ShadowMountPlus liest ihn zuerst): Eine Kopie bekäme keine eigene APR-EMU-Datei. Bitte im Spielordner packen.", -1); return; }
  if(pwhy[0] && !backport && !place[0]) { lz4_fail(pwhy, -1); return; }

  set_phase("Der Spielordner wird geprüft");
  if(lz4_tree_check(root, err, sizeof(err)) != 0) { lz4_fail(err, cancelled() ? ECANCELED : -1); return; }
  set_phase("Der AMPR-Index wird gebaut");
  unsigned nf = 0, coll = 0;
  if(ps5tm_ampr_build_dir(root, &nf, &coll, &g_cancel, err, sizeof(err)) != 0) { lz4_fail(err, cancelled() ? ECANCELED : -1); return; }
  if(lz4_make_cfg(a, &cfg, err, sizeof(err)) != 0) { lz4_fail(err, -1); return; }
  have_cfg = 1;
  lzp_plan_t pl;
  if(lzp_plan(root, &cfg, &pl, &ui, err, sizeof(err)) != 0) { lz4_fail(err, cancelled() ? ECANCELED : -1); goto out; }
  if(pl.pack_files == 0) { lz4_fail("Nach diesem Profil gäbe es nichts zu packen.", -1); goto out; }
  if(cfg.automatic) {
    cfg.groups[0].pack_count = pl.pack_bytes >= (8ull << 30) ? 4 : pl.pack_bytes >= (1ull << 30) ? 2 : 1;
    cfg.workers = 3;
  }
  loose_bytes = pl.loose_bytes;
  pthread_mutex_lock(&g_lock);
  g_job.bytes_files = pl.pack_bytes;
  pthread_mutex_unlock(&g_lock);

  /* room: the packs are at most as big as the data; a rough check, a full drive ends the job cleanly anyway */
  const char *space_at = root;
  uint64_t need = pl.pack_bytes / 2 + IJ_RESERVE + (copy ? pl.loose_bytes : 0);
  char parent[PATH_MAX];
  if(copy) {
    snprintf(parent, sizeof(parent), "%s", root);
    char *sl = strrchr(parent, '/');
    if(sl && sl != parent) *sl = 0;
    const char *base = strrchr(root, '/');
    base = base ? base + 1 : root;
    snprintf(fin, sizeof(fin), "%s/%s-lz4", parent, base);
    snprintf(tmp, sizeof(tmp), "%s" IJ_PART_SUFFIX, fin);
    space_at = parent;
    struct stat st0;
    if(lstat(fin, &st0) == 0) { char m[PATH_MAX + 120]; snprintf(m, sizeof(m), "Den Ordner %s-lz4 gibt es schon. Bitte ihn zuerst umbenennen oder löschen.", base); lz4_fail(m, -1); goto out; }
    if(lstat(tmp, &st0) == 0 && S_ISDIR(st0.st_mode)) rm_tree(tmp, 0);
    pthread_mutex_lock(&g_lock);
    snprintf(g_job.dest, sizeof(g_job.dest), "%s", fin);
    pthread_mutex_unlock(&g_lock);
  }
  uint64_t avail = lz4_free(space_at);
  {                                                            /* a FAT32 drive takes no file of 4 GiB or more: smaller pack files */
    char fst[16] = "";
    uint64_t av2 = 0;
    if(drive_free(space_at, &av2, fst, sizeof(fst)) == 0 && !strcmp(fst, "msdosfs")) {
      for(unsigned g = 0; g < cfg.ngroups; g++)
        if(!cfg.groups[g].max_pack_size || cfg.groups[g].max_pack_size > (3500ull << 20)) cfg.groups[g].max_pack_size = 3500ull << 20;
    }
  }
  if(avail < need) {
    char m[200];
    snprintf(m, sizeof(m), "Auf dem Laufwerk ist wahrscheinlich nicht genug Platz: nötig sind etwa %.1f GB, frei sind %.1f GB.", (double)need / 1e9, (double)avail / 1e9);
    lz4_fail(m, -1);
    goto out;
  }
  const char *outdir = root;
  if(copy) {
    if(mkdir(tmp, 0777) != 0) { char m[300]; snprintf(m, sizeof(m), "Der Zielordner lässt sich nicht anlegen: %s", ps5tm_io_strerror(errno)); lz4_fail(m, -1); goto out; }
    (void)chmod(tmp, LZP_FILE_MODE);
    made_tmp = 1;
    outdir = tmp;
  }

  uint64_t t_pack = ps5tm_mono_ms();
  if(lzp_pack(root, outdir, &cfg, &st, &ui, err, sizeof(err)) != 0) {
    int c = cancelled();
    if(copy) rm_tree(tmp, 0);
    lz4_fail(err, c ? ECANCELED : -1);
    goto out;
  }
  uint64_t pack_ms = ps5tm_mono_ms() - t_pack;
  lz4_job_stats(&st, st.files_loose);
  lzp_stats_t vs;
  uint64_t t_verify = ps5tm_mono_ms();
  if(lzp_verify(outdir, root, &vs, &ui, err, sizeof(err)) != 0) {                   /* byte for byte against the originals */
    int c = cancelled();
    const char *rb = lz4_rollback(copy, tmp, root);
    char m[900];
    if(c) snprintf(m, sizeof(m), "Abgebrochen, %s.", rb);
    else snprintf(m, sizeof(m), "Die Prüfung gegen die Originale ist fehlgeschlagen, %s: %s", rb, err);
    lz4_fail(m, c ? ECANCELED : -1);
    goto out;
  }
  lz4_last_set(id, 1, vs.files_packed, vs.logical_bytes);
  lz4_speed_note(cfg.fast ? 1 : cfg.level, pl.pack_bytes, pack_ms, ps5tm_mono_ms() - t_verify);

  if(copy) {
    /* the files that stay loose, copied and read back */
    lzp_list_t pk;
    cp_t *c = calloc(1, sizeof(*c));
    uint8_t *buf = malloc(IJ_BUF_BYTES);
    if(!c || !buf || lzp_list(outdir, &pk, err, sizeof(err)) != 0) { free(c); free(buf); rm_tree(tmp, 0); lz4_fail(err[0] ? err : "Kein Speicher.", -1); goto out; }
    c->packed = &pk;
    c->buf = buf;
    c->srclen = strlen(root);
    c->dstlen = strlen(tmp);
    char *s2 = malloc(PATH_MAX), *d2 = malloc(PATH_MAX);
    if(!s2 || !d2) { free(s2); free(d2); free(c); free(buf); lzp_list_free(&pk); rm_tree(tmp, 0); lz4_fail("Kein Speicher.", -1); goto out; }
    snprintf(s2, PATH_MAX, "%s", root);
    snprintf(d2, PATH_MAX, "%s", tmp);
    lz4_ui_total(NULL, loose_bytes * 2 + 1);
    set_phase("Die Dateien, die lose bleiben, werden kopiert");
    cp_walk(c, s2, strlen(s2), d2, strlen(d2), 0);
    int crc_rc = c->rc;
    char why[400];
    snprintf(why, sizeof(why), "%s", c->why);
    free(s2); free(d2); free(c); free(buf);
    lzp_list_free(&pk);
    if(crc_rc) { rm_tree(tmp, 0); lz4_fail(why[0] ? why : "Abgebrochen.", crc_rc); goto out; }
  }

  /* the APR-EMU that reads the packs */
  set_phase("Die APR-EMU-Datei wird eingebaut");
  char awhy[300] = "";
  if(lz4_install_apr(id, copy ? tmp : root, backport, awhy, sizeof(awhy)) != 0) {
    const char *rb = lz4_rollback(copy, tmp, root);
    char m[800];
    snprintf(m, sizeof(m), "Die APR-EMU-Datei, die Pakete liest, ließ sich nicht einbauen (%s), %s.", awhy[0] ? awhy : "ohne Angabe", rb);
    lz4_fail(m, -1);
    goto out;
  }
  if(copy) {
    if(rename(tmp, fin) != 0) { int e = errno; rm_tree(tmp, 0); char m[300]; snprintf(m, sizeof(m), "Der fertige Ordner ließ sich nicht umbenennen: %s", ps5tm_io_strerror(e)); lz4_fail(m, -1); goto out; }
    made_tmp = 0;
  } else {
    lzp_stats_t rs;
    if(lzp_remove_sources(root, root, 1, &rs, &ui, err, sizeof(err)) != 0) {
      /* the packs and the APR-EMU are in, the game works with the originals it still has: say so */
      char m[600];
      snprintf(m, sizeof(m), "Die Pakete sind fertig und geprüft, aber das Entfernen der Originale ist unterbrochen worden (%s). Das Spiel läuft mit den Dateien, die noch da sind.", err);
      lz4_fail(m, cancelled() ? ECANCELED : -1);
      ps5tm_library_forget();
      goto out;
    }
  }
  {                                                            /* originals that came as 0666 (an .exfat copy) crash the packed game */
    unsigned mf = 0, mfail = 0;
    lzp_fix_modes(copy ? fin : root, &mf, &mfail);
    if(mf) PS5TM_INFO("lz4_pack_modes", "LZ4-Pakete für %s: Rechte an %u Einträgen auf 0777 gesetzt.", id, mf);
    if(mfail) {                                                /* said, not swallowed: such a file can crash the game */
      PS5TM_WARN("lz4_pack_modes_failed", "LZ4-Pakete für %s: Rechte an %u Einträgen ließen sich nicht auf 0777 setzen.", id, mfail);
      pthread_mutex_lock(&g_lock);
      snprintf(g_job.note, sizeof(g_job.note), "Rechte an %u Dateien oder Ordnern ließen sich nicht auf 0777 setzen. Startet das Spiel nicht, "
               "„Pakete prüfen“ wählen.", mfail);
      pthread_mutex_unlock(&g_lock);
    }
  }
  ps5tm_library_forget();
  PS5TM_INFO("lz4_pack_done", "LZ4-Pakete für %s: %llu Dateien, %.1f → %.1f GB%s.", id, (unsigned long long)st.files_packed,
             (double)st.logical_bytes / 1e9, (double)st.stored_bytes / 1e9, copy ? " (in einem neuen Ordner)" : "");
  finish(IJ_DONE, NULL);
out:
  (void)made_tmp;
  if(have_cfg) lzc_free(&cfg);
}

/* ---------------------------------------------------------------- verify, unpack */

/* Reads every block, then repairs what good packs need besides themselves (after LZ4 Compression 1.4.3, read on 15.10.2026):
   the APR-EMU that reads them, the files the PS5 looks for on disk (a set made before that rule holds them only packed) and the
   modes of the game folder (0777). The game is not running (checked at the start). */
static void
lz4_run_verify(const char *id, const char *root, const lz4arg_t *a) {
  (void)a;
  lzp_ui_t ui = lz4_ui();
  char err[500] = "";
  lzp_stats_t st;
  if(lzp_verify(root, NULL, &st, &ui, err, sizeof(err)) != 0) { lz4_fail(err, cancelled() ? ECANCELED : -1); return; }
  lz4_job_stats(&st, 0);
  lz4_last_set(id, 0, st.files_packed, st.logical_bytes);

  set_phase("Die APR-EMU-Datei wird geprüft");
  char warn[640] = "", why[300] = "";
  int apr = ps5tm_amprlib_pack_check(id, root, 1, warn, sizeof(warn), why, sizeof(why));
  if(apr < 0 || apr == 2) {
    char m[600];
    snprintf(m, sizeof(m), apr < 0 ? "Die Pakete sind in Ordnung, aber die APR-EMU-Datei, die sie liest, ließ sich nicht einbauen: %s"
                                   : "Die Pakete sind in Ordnung, aber das Spiel bekäme keine APR-EMU, die sie liest: %s", why[0] ? why : "ohne Angabe");
    lz4_fail(m, -1);
    return;
  }
  set_phase("Die Dateien, die die PS5 auf der Platte sucht, werden geprüft");
  unsigned missing = 0, restored = 0;
  if(lzp_restore_disk_files(root, 1, &missing, &restored, err, sizeof(err)) != 0) {
    char m[600];
    snprintf(m, sizeof(m), "Die Pakete sind in Ordnung, aber Dateien, die die PS5 auf der Platte sucht, ließen sich nicht aus ihnen zurückholen: %s", err);
    lz4_fail(m, -1);
    return;
  }
  set_phase("Die Rechte im Spielordner werden geprüft");
  unsigned fixed = 0, failed = 0;
  lzp_fix_modes(root, &fixed, &failed);
  pthread_mutex_lock(&g_lock);
  g_job.lz4_apr_fixed = apr == 1;
  g_job.lz4_restored = restored;
  g_job.lz4_modes = fixed;
  snprintf(g_job.note, sizeof(g_job.note), "%s", warn);
  if(failed) {
    size_t nl = strlen(g_job.note);
    snprintf(g_job.note + nl, sizeof(g_job.note) - nl, "%sRechte an %u Dateien oder Ordnern ließen sich nicht auf 0777 setzen.", nl ? " " : "", failed);
  }
  pthread_mutex_unlock(&g_lock);
  if(failed)
    PS5TM_WARN("lz4_verify_modes_failed", "LZ4-Pakete von %s: Rechte an %u Einträgen ließen sich nicht auf 0777 setzen.", id, failed);
  if(apr == 1)
    PS5TM_INFO("lz4_verify_repaired", "LZ4-Pakete von %s geprüft und repariert: APR-EMU neu eingebaut, %u Dateien zurück auf die Platte, Rechte an %u Einträgen gesetzt (%u ließen sich nicht ändern).",
               id, restored, fixed, failed);
  else if(restored || fixed)
    PS5TM_INFO("lz4_verify_repaired", "LZ4-Pakete von %s geprüft und repariert: %u Dateien zurück auf die Platte, Rechte an %u Einträgen gesetzt (%u ließen sich nicht ändern).",
               id, restored, fixed, failed);
  finish(IJ_DONE, NULL);
}

static void
lz4_run_unpack(const char *id, const char *root, const lz4arg_t *a) {
  (void)a;
  lzp_ui_t ui = lz4_ui();
  char err[500] = "";
  lzp_info_t inf;
  if(!lzp_info(root, &inf)) { lz4_fail(inf.why[0] ? inf.why : "Das Spiel liegt nicht in LZ4-Paketen.", -1); return; }
  uint64_t need = inf.logical_bytes + IJ_RESERVE;
  uint64_t avail = lz4_free(root);
  if(avail < need) {
    char m[200];
    snprintf(m, sizeof(m), "Zum Entpacken ist nicht genug Platz: nötig sind %.1f GB, frei sind %.1f GB.", (double)need / 1e9, (double)avail / 1e9);
    lz4_fail(m, -1);
    return;
  }
  lzp_stats_t st;
  if(lzp_unpack(root, root, &st, &ui, err, sizeof(err)) != 0) { lz4_fail(err, cancelled() ? ECANCELED : -1); return; }
  lzp_stats_t vs;
  if(lzp_verify(root, root, &vs, &ui, err, sizeof(err)) != 0) {
    char m[600];
    snprintf(m, sizeof(m), "Die entpackten Dateien weichen von den Paketen ab (%s). Die Pakete bleiben.", err);
    lz4_fail(m, cancelled() ? ECANCELED : -1);
    return;
  }
  set_phase("Die Pakete werden entfernt");
  if(lzp_remove_set(root, err, sizeof(err)) != 0) { lz4_fail(err, -1); return; }
  lz4_job_stats(&st, 0);
  pthread_mutex_lock(&g_lock);
  g_job.bytes_files = st.logical_bytes;
  pthread_mutex_unlock(&g_lock);
  ps5tm_library_forget();
  PS5TM_INFO("lz4_unpack_done", "LZ4-Pakete von %s entpackt: %llu Dateien.", id, (unsigned long long)st.files_packed);
  finish(IJ_DONE, NULL);
}

static void *
lz4_run(void *arg) {
  lz4arg_t *a = arg;
  ps5tm_powerguard_hold();
  char id[16], root[256];
  pthread_mutex_lock(&g_lock);
  snprintf(id, sizeof(id), "%s", g_job.id);
  snprintf(root, sizeof(root), "%s", g_job.image);
  pthread_mutex_unlock(&g_lock);
  if(!strcmp(a->kind, "lz4pack")) lz4_run_pack(id, root, a, 0);
  else if(!strcmp(a->kind, "lz4copy")) lz4_run_pack(id, root, a, 1);
  else if(!strcmp(a->kind, "lz4verify")) lz4_run_verify(id, root, a);
  else lz4_run_unpack(id, root, a);
  free(a);
  ps5tm_powerguard_release();
  return NULL;
}

/* ---------------------------------------------------------------- the window: state and estimate, start */

typedef struct {
  char         root[256];
  lzc_cfg_t    cfg;
  lzp_plan_t   plan;
  int          rc;
  char         err[300];
  char         profile[PATH_MAX];
  int          level;
  int          eff_level;              /* the level of the configuration (a profile names its own); LZ4 fast counts as 1 */
} lz4planarg_t;

static void *
lz4_plan_thread(void *arg) {
  lz4planarg_t *p = arg;
  lz4arg_t a;
  memset(&a, 0, sizeof(a));
  snprintf(a.profile, sizeof(a.profile), "%s", p->profile);
  a.level = p->level;
  if(lz4_make_cfg(&a, &p->cfg, p->err, sizeof(p->err)) != 0) { p->rc = -1; return NULL; }
  lzp_ui_t ui;
  memset(&ui, 0, sizeof(ui));
  p->rc = lzp_plan(p->root, &p->cfg, &p->plan, &ui, p->err, sizeof(p->err));
  p->eff_level = p->cfg.fast ? 1 : p->cfg.level;
  lzc_free(&p->cfg);
  return NULL;
}

static int
lz4_folder_game(const char *id, ps5tm_libdel_t *li, char *why, size_t wn) {
  if(!id || ps5tm_library_delete_info(id, li) != 0) { snprintf(why, wn, "Das Spiel ist nicht in der Liste."); return 404; }
  if(strcmp(li->format, "folder") || li->path[0] != '/') {
    snprintf(why, wn, "Nur für Spiele, die als Ordner vorliegen. Ein Abbild lässt sich vorher mit „In einen Ordner entpacken“ in einen Ordner bringen.");
    return 409;
  }
  return 200;
}

int
ps5tm_lz4_packed_dir(const char *dir) {
  char p[PATH_MAX];
  struct stat st;
  snprintf(p, sizeof(p), "%s/" LZP_INDEX_NAME, dir);
  return stat(p, &st) == 0 && S_ISREG(st.st_mode);
}

/* Does the game load libSceAmpr itself? The tools for LZ4 asset packs ask exactly this (12.10.2026, the LZ4 tool of theghostonline,
 * the MkPFS profiles): the name of the module stands in the list of needed libraries of eboot.bin or of a module in sce_module.
 * A game that does not has no use for packs, and its files, once packed, are not found again (Arkanoid, PPSA06328: it crashed
 * with the pack-capable APR-EMU even unpacked). 1 yes, 0 no, -1 eboot.bin cannot be read (then the app does not decide). */
#define LZ4_AMPR_NAME  "libSceAmpr"
#define LZ4_AMPR_LIMIT (192ull << 20)                 /* per file: the list of libraries is in the first part, and the scan is short */

static int
lz4_file_has_ampr(const char *path, int *readable) {
  int fd = open(path, O_RDONLY);
  if(fd < 0) return 0;
  const size_t nl = sizeof(LZ4_AMPR_NAME) - 1;
  const size_t chunk = 256 * 1024;
  unsigned char *buf = malloc(chunk + nl);
  if(!buf) { close(fd); return 0; }
  *readable = 1;
  size_t keep = 0;
  uint64_t total = 0;
  int found = 0;
  while(total < LZ4_AMPR_LIMIT) {
    ssize_t r = read(fd, buf + keep, chunk);
    if(r < 0 && errno == EINTR) continue;
    if(r <= 0) break;
    size_t have = keep + (size_t)r;
    for(size_t i = 0; i + nl <= have; i++) {
      if(buf[i] == 'l' && !memcmp(buf + i, LZ4_AMPR_NAME, nl)) { found = 1; break; }
    }
    if(found) break;
    keep = have < nl - 1 ? have : nl - 1;             /* a name cut by the end of the block */
    memmove(buf, buf + have - keep, keep);
    total += (uint64_t)r;
  }
  free(buf);
  close(fd);
  return found;
}

static int
lz4_ampr_import(const char *dir) {
  char p[PATH_MAX];
  int readable = 0;
  snprintf(p, sizeof(p), "%s/eboot.bin", dir);
  if(lz4_file_has_ampr(p, &readable)) return 1;
  if(!readable) return -1;
  snprintf(p, sizeof(p), "%s/sce_module", dir);
  DIR *d = opendir(p);
  if(!d) return 0;
  int found = 0;
  struct dirent *e;
  while(!found && (e = readdir(d)) != NULL) {
    size_t n = strlen(e->d_name);
    if(n < 4 || (strcasecmp(e->d_name + n - 4, ".prx") && (n < 5 || strcasecmp(e->d_name + n - 5, ".sprx")))) continue;
    char f[PATH_MAX];
    if(snprintf(f, sizeof(f), "%s/%s", p, e->d_name) >= (int)sizeof(f)) continue;
    int r2 = 0;
    found = lz4_file_has_ampr(f, &r2);
  }
  closedir(d);
  return found;
}

int
ps5tm_lz4_ampr_import(const char *game_dir) {
  return lz4_ampr_import(game_dir);
}

/* ---------------------------------------------------------------- which tested profile fits a game (after LZ4 Compression 1.4.0) */

/* one of the comma separated entries, upper and lower case alike */
static int
lz4_csv_has(const char *csv, const char *v) {
  size_t n = strlen(v);
  for(const char *p = csv; n && p && *p;) {
    const char *c = strchr(p, ',');
    size_t l = c ? (size_t)(c - p) : strlen(p);
    if(l == n && !strncasecmp(p, v, n)) return 1;
    p = c ? c + 1 : NULL;
  }
  return 0;
}

/* one of the comma separated words (4 letters or more) stands in text */
static int
lz4_csv_in(const char *csv, const char *text) {
  char w[64];
  for(const char *p = csv; text[0] && p && *p;) {
    const char *c = strchr(p, ',');
    size_t l = c ? (size_t)(c - p) : strlen(p);
    if(l >= 4 && l < sizeof(w)) {
      memcpy(w, p, l);
      w[l] = 0;
      if(strstr(text, w)) return 1;
    }
    p = c ? c + 1 : NULL;
  }
  return 0;
}

static int
lz4_isdir(const char *p) {
  struct stat st;
  return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

static int
lz4_isfile(const char *p) {
  struct stat st;
  return stat(p, &st) == 0 && S_ISREG(st.st_mode);
}

/* The engines the profiles cover, by the file layout (told apart as LZ4 Compression 1.5.0 does it): Insomniac keeps toc, dag
   and a folder d at the top; Unity has StreamingAssets next to its Data files, at the top or under Media. "" for neither. */
static const char *
lz4_engine(const char *root) {
  char a[PATH_MAX], b[PATH_MAX], c[PATH_MAX];
  snprintf(a, sizeof(a), "%s/toc", root);
  snprintf(b, sizeof(b), "%s/dag", root);
  snprintf(c, sizeof(c), "%s/d", root);
  if(lz4_isfile(a) && lz4_isfile(b) && lz4_isdir(c)) return "insomniac";
  static const char *const bases[] = { "", "/Media" };
  static const char *const marks[] = { "Data/globalgamemanagers", "Data/data.unity3d", "Data/Managed/Metadata/global-metadata.dat" };
  for(unsigned i = 0; i < 2; i++) {
    snprintf(a, sizeof(a), "%s%s/StreamingAssets", root, bases[i]);
    if(!lz4_isdir(a)) continue;
    for(unsigned k = 0; k < 2; k++)
      for(unsigned m = 0; m < 3; m++) {
        snprintf(b, sizeof(b), "%s%s/%s", root, bases[k], marks[m]);
        if(lz4_isfile(b)) return "unity";
      }
  }
  return "";
}

/* The built-in profile for a game: by Title ID or a word of the name first (made and tested for that game), then by the
   engine. *how: "title", "name" or "engine". NULL when none fits. */
static const ps5tm_lz4prof_t *
lz4_suggest(const char *id, const char *name, const char *root, const char **how) {
  char nm[160];
  size_t j = 0;
  for(const char *s = name; *s && j + 1 < sizeof(nm); s++)                 /* letters and digits, lower case: "ASTRO BOT™" -> "astrobot" */
    if(isalnum((unsigned char)*s) && (unsigned char)*s < 0x80) nm[j++] = (char)tolower((unsigned char)*s);
  nm[j] = 0;
  for(unsigned i = 0; i < ps5tm_lz4prof_count; i++)
    if(lz4_csv_has(ps5tm_lz4profs[i].title_ids, id)) { *how = "title"; return &ps5tm_lz4profs[i]; }
  for(unsigned i = 0; i < ps5tm_lz4prof_count; i++)
    if(lz4_csv_in(ps5tm_lz4profs[i].names, nm)) { *how = "name"; return &ps5tm_lz4profs[i]; }
  const char *eng = lz4_engine(root);
  for(unsigned i = 0; eng[0] && i < ps5tm_lz4prof_count; i++)
    if(!strcmp(ps5tm_lz4profs[i].engine, eng)) { *how = "engine"; return &ps5tm_lz4profs[i]; }
  return NULL;
}

struct cJSON *
ps5tm_lz4_info(const char *id, int *http) {
  *http = 200;
  ps5tm_libdel_t li;
  char why[300] = "";
  int rc = lz4_folder_game(id, &li, why, sizeof(why));
  if(rc == 404) { *http = 404; return NULL; }
  cJSON *o = cJSON_CreateObject();
  cJSON_AddBoolToObject(o, "ok", 1);
  cJSON_AddStringToObject(o, "id", id);
  cJSON_AddStringToObject(o, "name", li.name);
  cJSON_AddStringToObject(o, "format", li.format);
  cJSON_AddBoolToObject(o, "can", rc == 200);
  if(rc != 200) { cJSON_AddStringToObject(o, "why", why); return o; }
  cJSON_AddStringToObject(o, "path", li.path);
  cJSON_AddNumberToObject(o, "ampr_import", lz4_ampr_import(li.path));
  char ip[PATH_MAX];
  snprintf(ip, sizeof(ip), "%s/ampr_emu.index", li.path);
  struct stat st;
  cJSON_AddBoolToObject(o, "has_index", stat(ip, &st) == 0);
  cJSON_AddNumberToObject(o, "free_bytes", (double)lz4_free(li.path));
  lzp_info_t inf;
  int packed = lz4_info_big(li.path, &inf);
  cJSON_AddBoolToObject(o, "packed", packed);
  if(packed) {
    cJSON *p = cJSON_AddObjectToObject(o, "pack");
    cJSON_AddNumberToObject(p, "files_packed", (double)inf.files_packed);
    cJSON_AddNumberToObject(p, "files_loose", (double)inf.files_loose);
    cJSON_AddNumberToObject(p, "logical_bytes", (double)inf.logical_bytes);
    cJSON_AddNumberToObject(p, "stored_bytes", (double)inf.stored_bytes);
    cJSON_AddNumberToObject(p, "volumes", (double)inf.volumes);
    cJSON_AddNumberToObject(p, "chunks", (double)inf.chunks);
    cJSON_AddBoolToObject(p, "has_crc", inf.has_crc);
    cJSON_AddBoolToObject(p, "has_runtime", inf.has_runtime);
    cJSON_AddStringToObject(p, "build_id", inf.build_id);
    /* what would keep the packed game from starting, as far as the app can see it (the start check of exFAT2AMPR 1.104 by
       V-10): eboot.bin without the right to run, and ShadowMountPlus mounting no fakelib for it (then the APR-EMU that reads
       the packs is not loaded). The APR-EMU itself, fakelib2 and the backport folder are the row "APR-EMU im Spiel". */
    cJSON *sp = cJSON_AddArrayToObject(o, "start_problems");
    char eb[PATH_MAX];
    snprintf(eb, sizeof(eb), "%s/eboot.bin", li.path);
    struct stat est;
    if(stat(eb, &est) == 0 && !(est.st_mode & 0111))
      cJSON_AddItemToArray(sp, cJSON_CreateString("eboot.bin darf nicht ausgeführt werden. „Pakete prüfen“ setzt die Rechte im Spielordner auf 0777."));
    ps5tm_smp_game_t sg;
    if(ps5tm_smp_find(id, &sg) == 0) {
      if(!strcmp(sg.fakelib_mode, "disabled"))
        cJSON_AddItemToArray(sp, cJSON_CreateString("ShadowMountPlus bindet für dieses Spiel keine fakelib ein (fakelib-Modus „Deaktiviert“). "
                             "Dann fehlt beim Start die APR-EMU, die die Pakete liest, und das Spiel startet nicht. In der Weboberfläche von "
                             "ShadowMountPlus den Modus des Spiels auf „Emulatoren“ oder „Voll“ stellen."));
      else if(sg.fakelib_effective == 0)
        cJSON_AddItemToArray(sp, cJSON_CreateString("ShadowMountPlus bindet beim Start keine fakelib ein (in seinen Einstellungen ist das "
                             "Einbinden ausgeschaltet). Dann fehlt die APR-EMU, die die Pakete liest, und das Spiel startet nicht."));
    }
  } else if(inf.why[0]) cJSON_AddStringToObject(o, "pack_why", inf.why);
  pthread_mutex_lock(&g_lock);
  for(unsigned i = 0; i < IJ_LAST_MAX; i++) {
    if(strcmp(g_lz4_last[i].id, id)) continue;
    cJSON *l = cJSON_AddObjectToObject(o, "last_check");
    cJSON_AddNumberToObject(l, "when", (double)g_lz4_last[i].when);
    cJSON_AddBoolToObject(l, "bytewise", g_lz4_last[i].bytewise);
    cJSON_AddNumberToObject(l, "files", (double)g_lz4_last[i].files);
    cJSON_AddNumberToObject(l, "bytes", (double)g_lz4_last[i].bytes);
  }
  pthread_mutex_unlock(&g_lock);
  cJSON *arr = cJSON_AddArrayToObject(o, "profiles");
  lz4prof_t *l = malloc(LZ4_PROF_MAX * sizeof(*l));
  if(l) {
    unsigned c = lz4_profiles(l, LZ4_PROF_MAX);
    const char *own = NULL;                                                   /* a profile of the person for this game wins */
    for(unsigned i = 0; i < c; i++) {
      cJSON *e = cJSON_CreateObject();
      cJSON_AddStringToObject(e, "id", l[i].id);
      cJSON_AddStringToObject(e, "name", l[i].name);
      cJSON_AddStringToObject(e, "where", l[i].where);
      const ps5tm_lz4prof_t *b = lz4_builtin(l[i].id);
      if(b) {
        cJSON_AddStringToObject(e, "tested", b->tested);
        cJSON_AddStringToObject(e, "source", b->source);
      } else if(!own && !strncasecmp(l[i].name, id, strlen(id))) own = l[i].id;
      cJSON_AddItemToArray(arr, e);
    }
    if(own) cJSON_AddStringToObject(o, "own_profile", own);
    free(l);
  }
  const char *how = "";
  const ps5tm_lz4prof_t *sg = lz4_suggest(id, li.name, li.path, &how);
  if(sg) {
    cJSON *s = cJSON_AddObjectToObject(o, "suggest");
    char sid[64];
    snprintf(sid, sizeof(sid), LZ4_BUILTIN "%s", sg->id);
    cJSON_AddStringToObject(s, "id", sid);
    cJSON_AddStringToObject(s, "label", sg->label);
    cJSON_AddStringToObject(s, "tested", sg->tested);
    cJSON_AddStringToObject(s, "source", sg->source);
    cJSON_AddStringToObject(s, "how", how);
    if(sg->engine[0]) cJSON_AddStringToObject(s, "engine", sg->engine);
  }
  return o;
}

/* what a profile would do with the game: files, bytes, blocks, without packing anything */
struct cJSON *
ps5tm_lz4_plan(const char *id, int level, const char *profile, int *http) {
  *http = 200;
  ps5tm_libdel_t li;
  char why[300] = "";
  int rc = lz4_folder_game(id, &li, why, sizeof(why));
  if(rc != 200) { *http = rc; return NULL; }
  lz4planarg_t *p = calloc(1, sizeof(*p));
  if(!p) { *http = 500; return NULL; }
  snprintf(p->root, sizeof(p->root), "%s", li.path);
  p->level = level < 1 ? 9 : level > 12 ? 12 : level;
  if(lz4_profile_path(profile, p->profile, sizeof(p->profile)) != 0) { free(p); *http = 400; return NULL; }
  cJSON *o = cJSON_CreateObject();
  cJSON_AddBoolToObject(o, "ok", 1);
  if(run_big(lz4_plan_thread, p) != 0) { cJSON_AddBoolToObject(o, "ok", 0); cJSON_AddStringToObject(o, "error", "Die Auswahl lässt sich gerade nicht berechnen."); free(p); return o; }
  if(p->rc != 0) {
    cJSON_AddBoolToObject(o, "valid", 0);
    cJSON_AddStringToObject(o, "error", p->err[0] ? p->err : "Die Auswahl ließ sich nicht berechnen.");
  } else {
    cJSON_AddBoolToObject(o, "valid", 1);
    cJSON_AddNumberToObject(o, "files_total", (double)p->plan.files_total);
    cJSON_AddNumberToObject(o, "pack_files", (double)p->plan.pack_files);
    cJSON_AddNumberToObject(o, "pack_bytes", (double)p->plan.pack_bytes);
    cJSON_AddNumberToObject(o, "loose_files", (double)p->plan.loose_files);
    cJSON_AddNumberToObject(o, "loose_bytes", (double)p->plan.loose_bytes);
    cJSON_AddNumberToObject(o, "chunks", (double)p->plan.chunks);
    cJSON_AddNumberToObject(o, "block_shift", p->plan.block_shift);
    int learned = 0, l9 = 0;
    double est = lz4_estimate_s(p->plan.pack_bytes, p->eff_level, &learned);
    cJSON_AddNumberToObject(o, "est_s", (double)(uint64_t)(est + 0.5));
    cJSON_AddBoolToObject(o, "est_learned", learned);
    cJSON_AddNumberToObject(o, "est_level", p->eff_level);
    if(p->eff_level >= 10) cJSON_AddNumberToObject(o, "est9_s", (double)(uint64_t)(lz4_estimate_s(p->plan.pack_bytes, 9, &l9) + 0.5));
  }
  free(p);
  return o;
}

int
ps5tm_lz4_start(const char *kind_in, const char *id, int level, const char *profile, char *err, size_t n) {
  char kind[10] = "";
  if(kind_in && !strcmp(kind_in, "pack")) strcpy(kind, "lz4pack");
  else if(kind_in && !strcmp(kind_in, "copy")) strcpy(kind, "lz4copy");
  else if(kind_in && !strcmp(kind_in, "verify")) strcpy(kind, "lz4verify");
  else if(kind_in && !strcmp(kind_in, "unpack")) strcpy(kind, "lz4unpack");
  else { snprintf(err, n, "Unbekannter Vorgang."); return 400; }
  int is_pack = !strcmp(kind, "lz4pack") || !strcmp(kind, "lz4copy");
  ps5tm_libdel_t li;
  int rc = lz4_folder_game(id, &li, err, n);
  if(rc != 200) return rc;
  char prof[PATH_MAX] = "";
  if(is_pack && lz4_profile_path(profile, prof, sizeof(prof)) != 0) { snprintf(err, n, "Das Profil gibt es nicht (mehr)."); return 400; }
  if(is_pack && (level < 1 || level > 12)) level = 9;
  if(ps5tm_procmgr_title_running(id) == 1) { snprintf(err, n, "Das Spiel läuft gerade. Bitte zuerst beenden."); return 409; }
  /* what is there: reading the manifest takes a big stack */
  lzp_info_t inf0;
  int packed = lz4_info_big(li.path, &inf0);
  if(is_pack && packed) { snprintf(err, n, "Das Spiel liegt schon in LZ4-Paketen."); return 409; }
  if(is_pack && lz4_ampr_import(li.path) == 0) {
    snprintf(err, n, "Dieses Spiel bindet libSceAmpr weder in eboot.bin noch in sce_module ein. Es würde seine Dateien in den Paketen nicht wiederfinden. Für solche Spiele gibt es nur die Komprimierung zu einem .ffpfsc-Abbild (Knopf „Konvertieren“).");
    return 409;
  }
  if(!is_pack && !packed) { snprintf(err, n, "Das Spiel liegt nicht in LZ4-Paketen."); return 409; }
  if(!strcmp(kind, "lz4copy")) {                              /* the folder of the copy must not be there yet: asked before anything is touched */
    char fin[PATH_MAX];
    const char *base = strrchr(li.path, '/');
    base = base ? base + 1 : li.path;
    snprintf(fin, sizeof(fin), "%s-lz4", li.path);
    struct stat st0;
    if(lstat(fin, &st0) == 0) { snprintf(err, n, "Den Ordner %s-lz4 gibt es schon. Bitte ihn zuerst umbenennen oder löschen.", base); return 409; }
  }
  lz4arg_t *a = calloc(1, sizeof(*a));
  if(!a) { snprintf(err, n, "Kein Speicher."); return 500; }
  snprintf(a->kind, sizeof(a->kind), "%s", kind);
  a->level = level;
  snprintf(a->profile, sizeof(a->profile), "%s", prof);
  pthread_mutex_lock(&g_lock);
  if(active_locked()) { pthread_mutex_unlock(&g_lock); free(a); snprintf(err, n, "Es läuft schon ein Vorgang an einem Abbild oder an LZ4-Paketen."); return 409; }
  memset(&g_job, 0, sizeof(g_job));
  g_job.state = IJ_RUNNING;
  snprintf(g_job.kind, sizeof(g_job.kind), "%s", kind);
  snprintf(g_job.id, sizeof(g_job.id), "%s", id);
  snprintf(g_job.name, sizeof(g_job.name), "%s", li.name);
  snprintf(g_job.image, sizeof(g_job.image), "%s", li.path);
  snprintf(g_job.format, sizeof(g_job.format), "folder");
  snprintf(g_job.phase, sizeof(g_job.phase), "Wird vorbereitet");
  g_job.started_ms = ps5tm_mono_ms();
  __atomic_store_n(&g_cancel, 0, __ATOMIC_RELEASE);
  pthread_mutex_unlock(&g_lock);
  /* claim first, then ask the others (they do the same): of two starts at the same moment one gives way (review 19.10.2026) */
  if(other_job(err, n)) {
    pthread_mutex_lock(&g_lock);
    memset(&g_job, 0, sizeof(g_job));
    pthread_mutex_unlock(&g_lock);
    free(a);
    return 409;
  }
  pthread_t th;
  pthread_attr_t at;
  pthread_attr_init(&at);
  pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
  pthread_attr_setstacksize(&at, IJ_STACK);
  int prc = pthread_create(&th, &at, lz4_run, a);
  pthread_attr_destroy(&at);
  if(prc != 0) {
    free(a);
    finish(IJ_FAILED, "Der Vorgang ließ sich nicht starten.");
    snprintf(err, n, "Der Vorgang ließ sich nicht starten.");
    return 500;
  }
  PS5TM_INFO("lz4_job_start", "LZ4-Vorgang gestartet: %s von %s (%s).", kind, li.name, id);
  return 200;
}
