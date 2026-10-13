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

static unsigned
lz4_profiles(lz4prof_t *out, unsigned max) {
  static const struct { const char *where; const char *dir; } dirs[] = {
    { "app", PS5TM_DATA_DIR "/LZ4_Profile" },
    { "usb0", "/mnt/usb0/LZ4_Profile" }, { "usb1", "/mnt/usb1/LZ4_Profile" }, { "usb2", "/mnt/usb2/LZ4_Profile" },
    { "usb3", "/mnt/usb3/LZ4_Profile" }, { "usb4", "/mnt/usb4/LZ4_Profile" }, { "usb5", "/mnt/usb5/LZ4_Profile" },
    { "ext0", "/mnt/ext0/LZ4_Profile" }, { "ext1", "/mnt/ext1/LZ4_Profile" } };
  unsigned n = 0;
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
  finish(rc == ECANCELED || cancelled() ? IJ_CANCELLED : IJ_FAILED, err[0] ? err : "Das ging nicht.");
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

/* the APR-EMU that reads LZ4 packs in the game: kept when there is one, else test-pack goes in (amprlib.c) */
static int
lz4_install_apr(const char *id, const char *root, int backport, char *why, size_t wn) {
  int http = 0;
  cJSON *st = ps5tm_amprlib_state("ampr", id, &http);
  int have = 0;
  if(st) {
    const cJSON *ins = cJSON_GetObjectItem(st, "installed");
    const cJSON *pk = ins ? cJSON_GetObjectItem(ins, "pack") : NULL;
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
  int out = open(dst, O_WRONLY | O_CREAT | O_EXCL, 0666);
  if(out < 0) { close(in); snprintf(c->why, sizeof(c->why), "Eine Datei ließ sich nicht anlegen (%s): %s", ps5tm_io_strerror(errno), dst + c->dstlen + 1); return -1; }
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
  if(fsync(out) != 0 || close(out) != 0) { out = -1; snprintf(c->why, sizeof(c->why), "Schreiben schlug fehl (%s): %s", ps5tm_io_strerror(errno), dst + c->dstlen + 1); goto bad; }
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
  while((e = readdir(d))) {
    if(!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
    size_t n = strlen(e->d_name);
    if(sl + 1 + n >= PATH_MAX - 8 || dl + 1 + n >= PATH_MAX - 8) { snprintf(c->why, sizeof(c->why), "Ein Pfad ist zu lang: %s", e->d_name); c->rc = -1; rc = -1; break; }
    src[sl] = '/'; memcpy(src + sl + 1, e->d_name, n + 1);
    dst[dl] = '/'; memcpy(dst + dl + 1, e->d_name, n + 1);
    struct stat st;
    if(lstat(src, &st) != 0) { src[sl] = 0; dst[dl] = 0; continue; }
    if(S_ISDIR(st.st_mode)) {
      if(mkdir(dst, 0777) != 0 && errno != EEXIST) { snprintf(c->why, sizeof(c->why), "Ein Ordner ließ sich nicht anlegen (%s): %s", ps5tm_io_strerror(errno), dst + c->dstlen + 1); c->rc = -1; rc = -1; }
      else rc = cp_walk(c, src, sl + 1 + n, dst, dl + 1 + n, depth + 1);
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

/* ---------------------------------------------------------------- pack (in place or into a copy) */

static int
lz4_make_cfg(const lz4arg_t *a, lzc_cfg_t *cfg, char *err, size_t en) {
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
    made_tmp = 1;
    outdir = tmp;
  }

  if(lzp_pack(root, outdir, &cfg, &st, &ui, err, sizeof(err)) != 0) {
    int c = cancelled();
    if(copy) rm_tree(tmp, 0);
    lz4_fail(err, c ? ECANCELED : -1);
    goto out;
  }
  lz4_job_stats(&st, st.files_loose);
  lzp_stats_t vs;
  if(lzp_verify(outdir, root, &vs, &ui, err, sizeof(err)) != 0) {                   /* byte for byte against the originals */
    int c = cancelled();
    if(copy) rm_tree(tmp, 0); else lzp_remove_set(root, NULL, 0);
    char m[600];
    snprintf(m, sizeof(m), "%s%s", c ? "" : "Die Prüfung gegen die Originale ist fehlgeschlagen, die Pakete wurden wieder entfernt: ", err);
    lz4_fail(c ? "Abgebrochen." : m, c ? ECANCELED : -1);
    goto out;
  }
  lz4_last_set(id, 1, vs.files_packed, vs.logical_bytes);

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
    if(copy) rm_tree(tmp, 0); else lzp_remove_set(root, NULL, 0);
    char m[500];
    snprintf(m, sizeof(m), "Die APR-EMU-Datei, die Pakete liest, ließ sich nicht einbauen (%s). Die Pakete wurden wieder entfernt.", awhy[0] ? awhy : "ohne Angabe");
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
  ps5tm_library_forget();
  PS5TM_INFO("lz4_pack_done", "LZ4-Pakete für %s: %llu Dateien, %.1f → %.1f GB%s.", id, (unsigned long long)st.files_packed,
             (double)st.logical_bytes / 1e9, (double)st.stored_bytes / 1e9, copy ? " (in einem neuen Ordner)" : "");
  finish(IJ_DONE, NULL);
out:
  (void)made_tmp;
  if(have_cfg) lzc_free(&cfg);
}

/* ---------------------------------------------------------------- verify, unpack */

static void
lz4_run_verify(const char *id, const char *root, const lz4arg_t *a) {
  (void)a;
  lzp_ui_t ui = lz4_ui();
  char err[500] = "";
  lzp_stats_t st;
  if(lzp_verify(root, NULL, &st, &ui, err, sizeof(err)) != 0) { lz4_fail(err, cancelled() ? ECANCELED : -1); return; }
  lz4_job_stats(&st, 0);
  lz4_last_set(id, 0, st.files_packed, st.logical_bytes);
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
    for(unsigned i = 0; i < c; i++) {
      cJSON *e = cJSON_CreateObject();
      cJSON_AddStringToObject(e, "id", l[i].id);
      cJSON_AddStringToObject(e, "name", l[i].name);
      cJSON_AddStringToObject(e, "where", l[i].where);
      cJSON_AddItemToArray(arr, e);
    }
    free(l);
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
  if(other_job(err, n)) return 409;
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
