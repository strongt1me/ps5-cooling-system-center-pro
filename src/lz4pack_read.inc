/* The reading half of lz4pack.c (included from there): the state of a pack set, verifying, unpacking, removing. */

static void
join_path(char *out, size_t n, const char *dir, const char *name) {
  snprintf(out, n, "%s/%s", dir, name);
}

/* ---------------------------------------------------------------- info */

int
lzp_info(const char *dir, lzp_info_t *info) {
  memset(info, 0, sizeof(*info));
  char idx[PATH_MAX], tmp[PATH_MAX + 16];
  join_path(idx, sizeof(idx), dir, LZP_INDEX_NAME);
  if(!file_exists(idx)) return 0;
  man_t m;
  char err[200] = "";
  if(man_load(idx, &m, 0, err, sizeof(err)) != 0) {
    snprintf(info->why, sizeof(info->why), "%s", err);
    return 0;
  }
  info->packed = 1;
  for(size_t i = 0; i < m.nf; i++) {
    if(m.f[i].flags & FF_PACKED) { info->files_packed++; info->logical_bytes += m.f[i].logical; }
    else info->files_loose++;
  }
  info->chunks = m.nc;
  info->volumes = m.np;
  for(size_t i = 0; i < m.np; i++) info->stored_bytes += m.p[i].file_size;
  for(int i = 0; i < 16; i++) snprintf(info->build_id + i * 2, 3, "%02x", m.build[i]);
  snprintf(tmp, sizeof(tmp), "%s.crc", idx);
  info->has_crc = file_exists(tmp);
  snprintf(tmp, sizeof(tmp), "%s.runtime", idx);
  info->has_runtime = file_exists(tmp);
  man_free(&m);
  return 1;
}

/* ---------------------------------------------------------------- one set opened for reading */

typedef struct {
  man_t    m;
  vols_t   v;
  uint32_t *crcs;
  uint8_t  *stored, *raw, *cmp;
} set_t;

static void
set_close(set_t *s) {
  vols_close(&s->v);
  man_free(&s->m);
  free(s->crcs);
  free(s->stored); free(s->raw); free(s->cmp);
  memset(s, 0, sizeof(*s));
}

/* need_crc: a missing sidecar is an error */
static int
set_open(const char *dir, set_t *s, int need_crc, char *err, size_t en) {
  memset(s, 0, sizeof(*s));
  char idx[PATH_MAX], crc[PATH_MAX + 8];
  join_path(idx, sizeof(idx), dir, LZP_INDEX_NAME);
  snprintf(crc, sizeof(crc), "%s.crc", idx);
  if(man_load(idx, &s->m, 1, err, en) != 0) return -1;
  int rc = crc_load(crc, &s->m, &s->crcs, err, en);
  if(rc < 0) { set_close(s); return -1; }
  if(rc == 1 && need_crc) { set_close(s); say(err, en, "Die Prüfsummen-Datei (ampr_assets.index.crc) fehlt: Ohne sie lassen sich die Pakete nicht sicher prüfen."); return -1; }
  if(vols_open(dir, &s->m, &s->v, err, en) != 0) { set_close(s); return -1; }
  s->stored = malloc(MAX_BLOCK + 64);
  s->raw = malloc(MAX_BLOCK + 64);
  s->cmp = malloc(MAX_BLOCK + 64);
  if(!s->stored || !s->raw || !s->cmp) { set_close(s); say(err, en, "Kein Speicher."); return -1; }
  return 0;
}

static const char *
file_rel(const set_t *s, size_t id) {
  const frec_t *f = &s->m.f[id];
  return s->m.str + f->path_off + 6;
}

/* ---------------------------------------------------------------- verify */

int
lzp_verify(const char *dir, const char *srcroot, lzp_stats_t *st, const lzp_ui_t *ui, char *err, size_t en) {
  lzp_stats_t zs;
  if(!st) st = &zs;
  memset(st, 0, sizeof(*st));
  set_t s;
  ui_phase(ui, "Das Paketverzeichnis wird gelesen");
  if(set_open(dir, &s, srcroot == NULL ? 0 : 1, err, en) != 0) return -1;
  uint64_t total = 0;
  unsigned nfiles = 0;
  for(size_t i = 0; i < s.m.nf; i++) if(s.m.f[i].flags & FF_PACKED) { total += s.m.f[i].logical; nfiles++; }
  ui_total(ui, total);
  ui_files(ui, 0, nfiles);
  ui_phase(ui, srcroot ? "Die Pakete werden Byte für Byte mit den Originalen verglichen" : "Die Pakete werden gelesen und geprüft");
  int rc = -1;
  unsigned done = 0;
  uint64_t rep = 0;
  for(size_t id = 0; id < s.m.nf; id++) {
    const frec_t *f = &s.m.f[id];
    if(!(f->flags & FF_PACKED)) continue;
    const char *rel = file_rel(&s, id);
    ui_current(ui, rel);
    int sfd = -1;
    if(srcroot) {
      char sp[PATH_MAX];
      snprintf(sp, sizeof(sp), "%s/%s", srcroot, rel);
      struct stat sst;
      if(lstat(sp, &sst) != 0) { say(err, en, "Das Original fehlt beim Vergleich: %s", rel); goto out; }
      if(!S_ISREG(sst.st_mode) || (uint64_t)sst.st_size != f->logical) { say(err, en, "Das Original hat nicht die Größe der gepackten Datei: %s", rel); goto out; }
      sfd = open(sp, O_RDONLY);
      if(sfd < 0) { say(err, en, "Das Original ließ sich nicht öffnen: %s", rel); goto out; }
    }
    uint64_t pos = 0;
    for(uint32_t k = 0; k < f->count; k++) {
      size_t ci = f->first + k;
      uint32_t rs = raw_of(f, k);
      if(chunk_read(&s.m, &s.v, s.crcs, ci, rs, s.stored, s.raw, err, en) != 0) {
        char tmp[300];
        snprintf(tmp, sizeof(tmp), "%s (Datei %s, Block %u)", err, rel, (unsigned)k);
        snprintf(err, en, "%s", tmp);
        if(sfd >= 0) close(sfd);
        goto out;
      }
      if(sfd >= 0) {
        if(read_all(sfd, s.cmp, rs, pos) != 0) { close(sfd); say(err, en, "Das Original ist kürzer als die gepackte Datei: %s", rel); goto out; }
        if(memcmp(s.cmp, s.raw, rs) != 0) { close(sfd); say(err, en, "Das Original weicht von den Paketen ab: %s (ab Byte %llu).", rel, (unsigned long long)pos); goto out; }
      }
      pos += rs;
      st->chunks++;
      if(c_codec(&s.m, ci) == CODEC_LZ4) st->chunks_lz4++; else st->chunks_raw++;
      st->logical_bytes += rs;
      st->stored_bytes += c_stored(&s.m, ci);
      rep += rs;
      if(rep >= (4u << 20)) { ui_work(ui, rep); rep = 0; if(cancelled(ui)) { if(sfd >= 0) close(sfd); say(err, en, "Abgebrochen."); rc = ECANCELED; goto out; } }
    }
    if(sfd >= 0) close(sfd);
    st->files_packed++;
    done++;
    ui_files(ui, done, nfiles);
  }
  ui_work(ui, rep);
  rc = 0;
out:
  set_close(&s);
  return rc;
}

/* ---------------------------------------------------------------- unpack */

static void
set_mtime(const char *path, int64_t mtime) {
  struct timeval tv[2];
  tv[0].tv_sec = tv[1].tv_sec = (time_t)mtime;
  tv[0].tv_usec = tv[1].tv_usec = 0;
  utimes(path, tv);
}

/* One packed file written to dst: as <dst>.ps5cc-teil first, CRC-checked block by block, 0777, then renamed, with the mtime of
   the manifest. *rep counts the bytes not yet reported to the page. 0, -1 with err, or ECANCELED. */
static int
write_packed(set_t *s, const frec_t *f, const char *rel, const char *dst, lzp_stats_t *st, const lzp_ui_t *ui, uint64_t *rep,
             char *err, size_t en) {
  char parent[PATH_MAX], tmp[PATH_MAX + 16];
  snprintf(parent, sizeof(parent), "%s", dst);
  char *sl = strrchr(parent, '/');
  if(sl) { *sl = 0; int mr = mkdir_p(parent); if(mr) { say(err, en, "Ein Ordner ließ sich nicht anlegen (%s): %s", strerror(mr), rel); return -1; } }
  snprintf(tmp, sizeof(tmp), "%s%s", dst, LZP_PART_SUFFIX);
  int ofd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, LZP_FILE_MODE);
  if(ofd < 0) { say(err, en, "Eine Datei ließ sich nicht anlegen (%s): %s", strerror(errno), rel); return -1; }
  (void)fchmod(ofd, LZP_FILE_MODE);
  int rc = -1;
  for(uint32_t k = 0; k < f->count; k++) {
    size_t ci = f->first + k;
    uint32_t rs = raw_of(f, k);
    if(chunk_read(&s->m, &s->v, s->crcs, ci, rs, s->stored, s->raw, err, en) != 0) {
      char t2[300];
      snprintf(t2, sizeof(t2), "%s (Datei %s, Block %u)", err, rel, (unsigned)k);
      snprintf(err, en, "%s", t2);
      goto out;
    }
    if(write_all(ofd, s->raw, rs) != 0) { say(err, en, "Schreiben schlug fehl (%s): %s", strerror(errno), rel); goto out; }
    st->logical_bytes += rs;
    st->chunks++;
    *rep += rs;
    if(*rep >= (4u << 20)) { ui_work(ui, *rep); *rep = 0; if(cancelled(ui)) { say(err, en, "Abgebrochen."); rc = ECANCELED; goto out; } }
  }
  if(fsync(ofd) != 0 || close(ofd) != 0) { ofd = -1; say(err, en, "Schreiben schlug fehl (%s): %s", strerror(errno), rel); goto out; }
  ofd = -1;
  if(rename(tmp, dst) != 0) { say(err, en, "Eine Datei ließ sich nicht umbenennen (%s): %s", strerror(errno), rel); goto out; }
  tmp[0] = 0;
  set_mtime(dst, f->mtime);
  rc = 0;
out:
  if(ofd >= 0) close(ofd);
  if(tmp[0]) unlink(tmp);
  return rc;
}

int
lzp_unpack(const char *dir, const char *destroot, lzp_stats_t *st, const lzp_ui_t *ui, char *err, size_t en) {
  lzp_stats_t zs;
  if(!st) st = &zs;
  memset(st, 0, sizeof(*st));
  set_t s;
  ui_phase(ui, "Das Paketverzeichnis wird gelesen");
  if(set_open(dir, &s, 1, err, en) != 0) return -1;
  uint64_t total = 0;
  unsigned nfiles = 0;
  for(size_t i = 0; i < s.m.nf; i++) if(s.m.f[i].flags & FF_PACKED) { total += s.m.f[i].logical; nfiles++; }
  ui_total(ui, total);
  ui_files(ui, 0, nfiles);
  ui_phase(ui, "Die Dateien werden aus den Paketen geschrieben");
  int rc = -1;
  unsigned done = 0;
  uint64_t rep = 0;
  for(size_t id = 0; id < s.m.nf; id++) {
    const frec_t *f = &s.m.f[id];
    if(!(f->flags & FF_PACKED)) continue;
    const char *rel = file_rel(&s, id);
    ui_current(ui, rel);
    char dst[PATH_MAX];
    if(snprintf(dst, sizeof(dst), "%s/%s", destroot, rel) >= (int)sizeof(dst) - 16) { say(err, en, "Ein Pfad ist zu lang: %s", rel); goto out; }
    int wr = write_packed(&s, f, rel, dst, st, ui, &rep, err, en);
    if(wr != 0) { rc = wr; goto out; }
    st->files_packed++;
    done++;
    ui_files(ui, done, nfiles);
  }
  ui_work(ui, rep);
  rc = 0;
out:
  set_close(&s);
  return rc;
}

/* ---------------------------------------------------------------- repairs of the check */

int
lzp_restore_disk_files(const char *dir, int repair, unsigned *missing, unsigned *restored, char *err, size_t en) {
  *missing = *restored = 0;
  set_t s;
  if(set_open(dir, &s, 0, err, en) != 0) return -1;
  lzp_stats_t st;
  memset(&st, 0, sizeof(st));
  lzp_ui_t quiet;
  memset(&quiet, 0, sizeof(quiet));
  uint64_t rep = 0;
  int rc = -1;
  for(size_t id = 0; id < s.m.nf; id++) {
    const frec_t *f = &s.m.f[id];
    if(!(f->flags & FF_PACKED)) continue;
    const char *rel = file_rel(&s, id);
    if(!lzc_keep_on_disk(rel, f->logical) || !safe_rel(rel)) continue;
    char dst[PATH_MAX];
    if(snprintf(dst, sizeof(dst), "%s/%s", dir, rel) >= (int)sizeof(dst) - 16) { say(err, en, "Ein Pfad ist zu lang: %s", rel); goto out; }
    struct stat sst;
    if(lstat(dst, &sst) == 0 && S_ISREG(sst.st_mode) && (uint64_t)sst.st_size == f->logical) continue;
    (*missing)++;
    if(!repair) continue;
    if(write_packed(&s, f, rel, dst, &st, &quiet, &rep, err, en) != 0) goto out;
    (*restored)++;
  }
  rc = 0;
out:
  set_close(&s);
  return rc;
}

static void
fix_modes_walk(char *path, size_t len, int depth, unsigned *fixed, unsigned *failed) {
  struct stat st;
  if(depth > 64 || lstat(path, &st) != 0) return;
  if(!S_ISDIR(st.st_mode) && !S_ISREG(st.st_mode)) return;     /* links and the rest stay as they are */
  if((st.st_mode & 07777) != LZP_FILE_MODE) {
    struct stat now;                                            /* a drive without modes says yes and changes nothing */
    if(chmod(path, LZP_FILE_MODE) == 0 && lstat(path, &now) == 0 && (now.st_mode & 07777) == LZP_FILE_MODE) (*fixed)++;
    else (*failed)++;
  }
  if(!S_ISDIR(st.st_mode)) return;
  DIR *d = opendir(path);
  if(!d) return;
  struct dirent *e;
  while((e = readdir(d)) != NULL) {
    if(!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
    size_t n = strlen(e->d_name);
    if(len + 1 + n >= PATH_MAX) continue;
    path[len] = '/';
    memcpy(path + len + 1, e->d_name, n + 1);
    fix_modes_walk(path, len + 1 + n, depth + 1, fixed, failed);
    path[len] = 0;
  }
  closedir(d);
}

void
lzp_fix_modes(const char *root, unsigned *fixed, unsigned *failed) {
  *fixed = *failed = 0;
  char *p = malloc(PATH_MAX);
  if(!p) return;
  snprintf(p, PATH_MAX, "%s", root);
  fix_modes_walk(p, strlen(p), 0, fixed, failed);
  free(p);
}

/* ---------------------------------------------------------------- remove the sources */

int
lzp_remove_sources(const char *dir, const char *root, int empty_dirs, lzp_stats_t *st, const lzp_ui_t *ui, char *err, size_t en) {
  lzp_stats_t zs;
  if(!st) st = &zs;
  memset(st, 0, sizeof(*st));
  man_t m;
  char idx[PATH_MAX];
  join_path(idx, sizeof(idx), dir, LZP_INDEX_NAME);
  if(man_load(idx, &m, 0, err, en) != 0) return -1;
  int rc = -1;
  unsigned nfiles = 0;
  /* first look at everything: nothing is removed unless every file is as the manifest says */
  for(size_t id = 0; id < m.nf; id++) {
    const frec_t *f = &m.f[id];
    if(!(f->flags & FF_PACKED)) continue;
    const char *rel = m.str + f->path_off + 6;
    if(lzc_protected(rel)) { say(err, en, "Das Verzeichnis nennt eine geschützte Datei als gepackt: %s", rel); goto out; }
    if(lzc_keep_on_disk(rel, f->logical)) continue;           /* the PS5 needs it on disk: it stays even when it is packed */
    char sp[PATH_MAX];
    snprintf(sp, sizeof(sp), "%s/%s", root, rel);
    struct stat sst;
    if(lstat(sp, &sst) != 0) { say(err, en, "Eine gepackte Datei fehlt schon im Spielordner: %s", rel); goto out; }
    if(!S_ISREG(sst.st_mode) || (uint64_t)sst.st_size != f->logical) { say(err, en, "Eine Datei im Spielordner ist keine reguläre Datei der erwarteten Größe: %s", rel); goto out; }
    nfiles++;
  }
  ui_phase(ui, "Die Originale werden entfernt");
  ui_files(ui, 0, nfiles);
  unsigned done = 0;
  for(size_t id = 0; id < m.nf; id++) {
    const frec_t *f = &m.f[id];
    if(!(f->flags & FF_PACKED)) continue;
    const char *rel = m.str + f->path_off + 6;
    if(lzc_keep_on_disk(rel, f->logical)) continue;
    char sp[PATH_MAX];
    snprintf(sp, sizeof(sp), "%s/%s", root, rel);
    if(unlink(sp) != 0) { say(err, en, "Eine Datei ließ sich nicht entfernen (%s): %s", strerror(errno), rel); goto out; }
    st->files_packed++;
    st->logical_bytes += f->logical;
    if(empty_dirs) {
      char d[PATH_MAX];
      snprintf(d, sizeof(d), "%s", sp);
      size_t rl = strlen(root);
      for(;;) {
        char *sl = strrchr(d, '/');
        if(!sl || (size_t)(sl - d) <= rl) break;
        *sl = 0;
        if(rmdir(d) != 0) break;
      }
    }
    done++;
    if((done & 255) == 0) { ui_files(ui, done, nfiles); ui_current(ui, rel); }
  }
  ui_files(ui, done, nfiles);
  rc = 0;
out:
  man_free(&m);
  return rc;
}

/* ---------------------------------------------------------------- the list of packed files */

static int
cmp_str(const void *a, const void *b) {
  return strcmp(*(char *const *)a, *(char *const *)b);
}

int
lzp_list(const char *dir, lzp_list_t *out, char *err, size_t en) {
  memset(out, 0, sizeof(*out));
  man_t m;
  char idx[PATH_MAX];
  join_path(idx, sizeof(idx), dir, LZP_INDEX_NAME);
  if(man_load(idx, &m, 0, err, en) != 0) return -1;
  size_t n = 0;
  for(size_t i = 0; i < m.nf; i++) if(m.f[i].flags & FF_PACKED) n++;
  out->rel = calloc(n ? n : 1, sizeof(char *));
  if(!out->rel) { man_free(&m); say(err, en, "Kein Speicher."); return -1; }
  for(size_t i = 0; i < m.nf; i++) {
    if(!(m.f[i].flags & FF_PACKED)) continue;
    char *s = strdup(m.str + m.f[i].path_off + 6);
    if(!s) { lzp_list_free(out); man_free(&m); say(err, en, "Kein Speicher."); return -1; }
    out->rel[out->n++] = s;
  }
  qsort(out->rel, out->n, sizeof(char *), cmp_str);
  man_free(&m);
  return 0;
}

void
lzp_list_free(lzp_list_t *l) {
  for(size_t i = 0; i < l->n; i++) free(l->rel[i]);
  free(l->rel);
  memset(l, 0, sizeof(*l));
}

int
lzp_list_has(const lzp_list_t *l, const char *rel) {
  return l->n && bsearch(&rel, l->rel, l->n, sizeof(char *), cmp_str) != NULL;
}

/* ---------------------------------------------------------------- remove the set */

int
lzp_remove_set(const char *dir, char *err, size_t en) {
  char idx[PATH_MAX], p[PATH_MAX + 16];
  join_path(idx, sizeof(idx), dir, LZP_INDEX_NAME);
  man_t m;
  char e2[200] = "";
  if(man_load(idx, &m, 0, e2, sizeof(e2)) == 0) {
    for(size_t i = 0; i < m.np; i++) {
      const char *name = man_string(&m, m.p[i].name_off, m.p[i].name_len);
      if(!name) continue;
      join_path(p, sizeof(p), dir, name);
      if(unlink(p) != 0 && errno != ENOENT) { say(err, en, "Eine Paketdatei ließ sich nicht löschen (%s): %s", strerror(errno), name); man_free(&m); return -1; }
    }
    man_free(&m);
  } else {                                   /* an unreadable manifest: the volumes of this app by their names */
    DIR *d = opendir(dir);
    if(d) {
      struct dirent *de;
      while((de = readdir(d))) {
        size_t nl = strlen(de->d_name);
        if(!strncmp(de->d_name, "ampr_assets-", 12) && nl > 16 && !strcmp(de->d_name + nl - 4, ".pak")) {
          join_path(p, sizeof(p), dir, de->d_name);
          unlink(p);
        }
      }
      closedir(d);
    }
  }
  const char *tails[] = { "", ".crc", ".runtime", ".ps5cc-teil", ".crc.ps5cc-teil", ".runtime.ps5cc-teil" };
  for(unsigned i = 0; i < sizeof(tails) / sizeof(tails[0]); i++) {
    snprintf(p, sizeof(p), "%s%s", idx, tails[i]);
    if(unlink(p) != 0 && errno != ENOENT) { say(err, en, "Eine Datei ließ sich nicht löschen (%s): %s", strerror(errno), p); return -1; }
  }
  return 0;
}
