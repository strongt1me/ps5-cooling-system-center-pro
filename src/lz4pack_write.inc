/* The packing half of lz4pack.c (included from there): planning, the compression pool, the volume writer, lzp_pack. */

/* ---------------------------------------------------------------- compression of one block */

typedef struct {
  void *fast;
  void *hc;
} cstate_t;

static int
cstate_init(cstate_t *c) {
  c->fast = malloc((size_t)LZ4_sizeofState());
  c->hc = malloc((size_t)LZ4_sizeofStateHC());
  return c->fast && c->hc ? 0 : -1;
}

static void
cstate_free(cstate_t *c) {
  free(c->fast);
  free(c->hc);
  c->fast = c->hc = NULL;
}

static void
hash128(const uint8_t *d, size_t n, uint64_t *h1, uint64_t *h2) {
  uint64_t a = 0x9E3779B97F4A7C15ull ^ (uint64_t)n, b = 0xC2B2AE3D27D4EB4Full + (uint64_t)n;
  size_t i = 0;
  for(; i + 8 <= n; i += 8) {
    uint64_t w;
    memcpy(&w, d + i, 8);
    a = (a ^ w) * 0x100000001B3ull;
    a ^= a >> 29;
    b = (b + w) * 0x9E3779B97F4A7C15ull;
    b ^= b >> 32;
    b = (b << 27) | (b >> 37);
  }
  uint64_t t = 0;
  for(size_t k = 0; i + k < n; k++) t |= (uint64_t)d[i + k] << (8 * k);
  a = (a ^ t) * 0x100000001B3ull;
  b = (b + t + 1) * 0x9E3779B97F4A7C15ull;
  a ^= a >> 33; a *= 0xFF51AFD7ED558CCDull; a ^= a >> 33;
  b ^= b >> 31; b *= 0xC4CEB9FE1A85EC53ull; b ^= b >> 29;
  *h1 = a;
  *h2 = b ^ (a << 7);
}

/* Compresses raw[len] by the rule (the decision of ampr_pack._compress_block): out gets the stored bytes, *codec says
   RAW (out is not used) or LZ4. A block that does not save enough stays RAW. */
static void
compress_block(cstate_t *cs, const uint8_t *raw, uint32_t len, const lzc_rule_t *r, uint32_t io_page, uint8_t *out, uint32_t *out_len, uint8_t *codec) {
  *codec = CODEC_RAW;
  *out_len = len;
  if(r->action == LZC_STORE || len == 0) return;
  uint64_t need = r->min_sav_bytes;
  uint64_t by_ratio = (uint64_t)(r->min_sav_ratio * (double)len);
  if((double)by_ratio < r->min_sav_ratio * (double)len) by_ratio++;
  if(by_ratio > need) need = by_ratio;
  if(need < 1) need = 1;
  if(need >= len) return;
  int cap = (int)(len - need);
  int got = r->fast ? LZ4_compress_fast_extState(cs->fast, (const char *)raw, (char *)out, (int)len, cap, r->accel)
                    : LZ4_compress_HC_extStateHC(cs->hc, (const char *)raw, (char *)out, (int)len, cap, r->level);
  if(got <= 0) return;
  uint64_t saved = len - (uint32_t)got;
  double ratio = (double)saved / (double)len;
  uint64_t req_bytes = r->min_sav_bytes;
  double req_ratio = r->min_sav_ratio;
  int streaming = r->layout == LZC_LAY_STREAMING || (r->layout == LZC_LAY_AUTO && r->streaming);
  if(!streaming) {
    uint64_t raw_pages = (len + io_page - 1) / io_page, comp_pages = ((uint32_t)got + io_page - 1) / io_page;
    if(comp_pages >= raw_pages) {
      if(r->ion_bytes > req_bytes) req_bytes = r->ion_bytes;
      if(r->ion_ratio > req_ratio) req_ratio = r->ion_ratio;
    }
  }
  if(saved < req_bytes || ratio < req_ratio) return;
  *codec = CODEC_LZ4;
  *out_len = (uint32_t)got;
}

/* ---------------------------------------------------------------- planning */

typedef struct {
  const lzc_rule_t *rule;        /* NULL: loose */
  uint64_t          size;
  int64_t           mtime;
  unsigned          lane;
  unsigned          layout;      /* LZC_LAY_RANDOM / MIXED / STREAMING */
  uint32_t          flags;       /* file flags */
  uint32_t          io_page;
  char             *src;         /* full path, packed files only */
} item_t;

typedef struct {
  const lzc_cfg_t *cfg;
  lzc_rule_t       defrule;
  ents_t           ents;
  item_t          *items;
  char            *strings;       /* the string table so far */
  size_t           nstr, capstr;
  uint32_t        *path_off, *path_len;
  uint64_t         pack_files, pack_bytes, loose_files, loose_bytes, auto_loose;
  uint64_t         chunks_est;
  unsigned         max_shift;
} plan_t;

static void
plan_free(plan_t *p) {
  if(p->items) for(size_t i = 0; i < p->ents.n; i++) free(p->items[i].src);
  free(p->items);
  free(p->strings);
  free(p->path_off);
  free(p->path_len);
  ents_release(&p->ents);
  memset(p, 0, sizeof(*p));
}

static int
str_add(plan_t *p, const char *s, size_t len, uint32_t *off) {
  if(p->nstr + len + 1 > 0xFFFFFFF0u) return -1;
  if(p->nstr + len + 1 > p->capstr) {
    size_t nc = p->capstr ? p->capstr * 2 : 1u << 16;
    while(nc < p->nstr + len + 1) nc *= 2;
    char *n = realloc(p->strings, nc);
    if(!n) return -1;
    p->strings = n;
    p->capstr = nc;
  }
  *off = (uint32_t)p->nstr;
  memcpy(p->strings + p->nstr, s, len);
  p->strings[p->nstr + len] = 0;
  p->nstr += len + 1;
  return 0;
}

static unsigned
resolved_layout(const lzc_rule_t *r) {
  if(r->layout != LZC_LAY_AUTO) return (unsigned)r->layout;
  if(r->streaming) return LZC_LAY_STREAMING;
  if(r->hot) return LZC_LAY_RANDOM;
  return LZC_LAY_MIXED;
}

static uint32_t
group_page(const lzc_cfg_t *c, const lzc_rule_t *r) {
  uint64_t g = c->groups[r->group].io_page_size;
  return (uint32_t)(g ? g : c->io_page_size);
}

/* the deterministic block samples of a large file (ampr_pack._sample_block_indices) */
static unsigned
sample_indices(uint64_t total, unsigned want, uint64_t *out) {
  if(total == 0 || want == 0) return 0;
  if(total <= want) { for(uint64_t i = 0; i < total; i++) out[i] = i; return (unsigned)total; }
  if(want == 1) { out[0] = total / 2; return 1; }
  unsigned n = 0;
  for(unsigned s = 0; s < want; s++) {
    uint64_t v = (uint64_t)s * (total - 1) / (want - 1);
    if(n == 0 || out[n - 1] != v) out[n++] = v;
  }
  return n;
}

/* ampr_pack._auto_loose_decision: a large file whose sampled blocks hardly shrink stays loose. 1 = loose. */
static int
auto_loose(const lzc_cfg_t *c, const lzc_rule_t *r, const char *src, uint64_t size, cstate_t *cs, uint8_t *raw, uint8_t *out, char *err, size_t en) {
  if(!c->auto_loose_large || r->action != LZC_COMPRESS || r->force_pack || (r->hot && !c->auto_loose_hot) || size < c->al_min_size || size == 0) return 0;
  uint64_t bs = 1ull << r->block_shift, total = (size + bs - 1) / bs;
  uint64_t limited = c->al_sample_bytes / bs;
  if(limited < 1) limited = 1;
  unsigned want = c->al_sample_blocks < limited ? c->al_sample_blocks : (unsigned)limited;
  uint64_t *idx = malloc((want ? want : 1) * sizeof(uint64_t));
  if(!idx) { say(err, en, "Kein Speicher."); return -1; }
  unsigned n = sample_indices(total, want, idx);
  int fd = open(src, O_RDONLY);
  if(fd < 0) { free(idx); say(err, en, "Eine Datei ließ sich nicht öffnen: %s", src); return -1; }
  uint64_t sampled = 0, stored = 0, raw_blocks = 0;
  uint32_t io = group_page(c, r);
  for(unsigned k = 0; k < n; k++) {
    uint64_t off = idx[k] * bs, expect = size - off < bs ? size - off : bs;
    if(read_all(fd, raw, (size_t)expect, off) != 0) { close(fd); free(idx); say(err, en, "Eine Datei ist beim Prüfen kürzer geworden: %s", src); return -1; }
    uint32_t ol; uint8_t codec;
    compress_block(cs, raw, (uint32_t)expect, r, io, out, &ol, &codec);
    sampled += expect;
    stored += ol;
    raw_blocks += codec == CODEC_RAW;
  }
  close(fd);
  free(idx);
  double saving = sampled ? 1.0 - (double)stored / (double)sampled : 0.0;
  double rawr = n ? (double)raw_blocks / (double)n : 0.0;
  return saving < c->al_min_ratio || rawr >= c->al_max_raw;
}

static int
cmp_lane_item(const void *a, const void *b);

typedef struct { uint64_t size; size_t id; const char *rel; } laneitem_t;

static int
cmp_lane_item(const void *a, const void *b) {
  const laneitem_t *x = a, *y = b;
  if(x->size != y->size) return x->size > y->size ? -1 : 1;
  int c = strcasecmp(x->rel, y->rel);
  if(c) return c;
  return x->id < y->id ? -1 : x->id > y->id;
}

static int
plan_build(plan_t *p, const char *root, lzc_cfg_t *cfg, const lzp_ui_t *ui, char *err, size_t en) {
  memset(p, 0, sizeof(*p));
  p->cfg = cfg;
  lzc_default_rule(cfg, &p->defrule);
  char ipath[PATH_MAX];
  snprintf(ipath, sizeof(ipath), "%s/%s", root, LZP_AMPR_INDEX);
  if(read_ampridx3(ipath, &p->ents, err, en) != 0) return -1;
  size_t n = p->ents.n;
  p->items = calloc(n ? n : 1, sizeof(item_t));
  p->path_off = calloc(n ? n : 1, sizeof(uint32_t));
  p->path_len = calloc(n ? n : 1, sizeof(uint32_t));
  if(!p->items || !p->path_off || !p->path_len) { plan_free(p); say(err, en, "Kein Speicher."); return -1; }
  cstate_t cs = { 0, 0 };
  uint8_t *abuf = NULL, *aout = NULL;
  int rc = -1;
  for(size_t i = 0; i < n; i++) {
    const ent_t *e = &p->ents.e[i];
    size_t pl = strlen(e->path);
    if(str_add(p, e->path, pl, &p->path_off[i]) != 0) { say(err, en, "Kein Speicher."); goto out; }
    p->path_len[i] = (uint32_t)pl;
    item_t *it = &p->items[i];
    it->size = e->size;
    it->mtime = e->mtime;
    const char *rel = e->path + 6;
    if(!safe_rel(rel)) { say(err, en, "Der AMPR-Index enthält einen unsicheren Pfad."); goto out; }
    lzc_rule_t tmp;
    const lzc_rule_t *rule = lzc_select(cfg, rel, &tmp);
    /* programs, system folders, the packs and the files the PS5 looks for on disk stay loose whatever a profile says */
    if(lzc_protected(rel) || lzc_keep_on_disk(rel, e->size) || rule->action == LZC_LOOSE) {
      p->loose_files++;
      p->loose_bytes += e->size;
      continue;
    }
    if(rule == &tmp) { p->defrule = tmp; rule = &p->defrule; }  /* a default rule that packs: kept for the items */
    char src[PATH_MAX];
    if(snprintf(src, sizeof(src), "%s/%s", root, rel) >= (int)sizeof(src)) { say(err, en, "Ein Pfad ist zu lang: %s", rel); goto out; }
    struct stat st;
    if(lstat(src, &st) != 0) {
      say(err, en, "Der AMPR-Index passt nicht mehr zu den Dateien (fehlt: %s). Bitte den Index neu bauen.", rel);
      goto out;
    }
    if(!S_ISREG(st.st_mode) || st.st_size == 0) {            /* a link or an empty file stays where it is */
      p->loose_files++;
      p->loose_bytes += e->size;
      continue;
    }
    if(cfg->validate_index && (uint64_t)st.st_size != e->size) {
      say(err, en, "Der AMPR-Index passt nicht mehr zu den Dateien (Größe von %s). Bitte den Index neu bauen.", rel);
      goto out;
    }
    if(cfg->auto_loose_large && rule->action == LZC_COMPRESS && !rule->force_pack && (uint64_t)st.st_size >= cfg->al_min_size) {
      if(!abuf) {
        abuf = malloc(MAX_BLOCK);
        aout = malloc(MAX_BLOCK + 4096);
        if(!abuf || !aout || cstate_init(&cs) != 0) { say(err, en, "Kein Speicher."); goto out; }
      }
      ui_current(ui, rel);
      int al = auto_loose(cfg, rule, src, (uint64_t)st.st_size, &cs, abuf, aout, err, en);
      if(al < 0) goto out;
      if(al && !cfg->self_contained) {
        p->loose_files++;
        p->loose_bytes += e->size;
        p->auto_loose++;
        continue;
      }
    }
    it->rule = rule;
    it->size = (uint64_t)st.st_size;
    it->mtime = cfg->preserve_mtime ? (int64_t)st.st_mtime : e->mtime;
    it->layout = resolved_layout(rule);
    it->io_page = group_page(cfg, rule);
    it->flags = FF_PACKED | (rule->action == LZC_STORE ? FF_STORE_ONLY : 0) | (it->layout == LZC_LAY_STREAMING ? FF_STREAMING : 0) |
                (it->layout == LZC_LAY_RANDOM ? FF_RANDOM : 0) | (rule->hot ? FF_HOT : 0);
    it->src = strdup(src);
    if(!it->src) { say(err, en, "Kein Speicher."); goto out; }
    p->pack_files++;
    p->pack_bytes += it->size;
    uint64_t bs = 1ull << rule->block_shift;
    p->chunks_est += (it->size + bs - 1) / bs;
    if(rule->block_shift > p->max_shift) p->max_shift = rule->block_shift;
    if((i & 1023) == 0) {
      if(cancelled(ui)) { say(err, en, "Abgebrochen."); rc = ECANCELED; goto out; }
      ui_current(ui, rel);
    }
  }
  rc = 0;
out:
  free(abuf);
  free(aout);
  cstate_free(&cs);
  if(rc != 0) plan_free(p);
  return rc;
}

/* lanes of each group: balanced (largest first to the lightest lane), hash, or round robin */
static int
plan_lanes(plan_t *p, char *err, size_t en) {
  const lzc_cfg_t *c = p->cfg;
  for(unsigned g = 0; g < c->ngroups; g++) {
    const lzc_group_t *grp = &c->groups[g];
    size_t cnt = 0;
    for(size_t i = 0; i < p->ents.n; i++) if(p->items[i].rule && p->items[i].rule->group == g) cnt++;
    if(!cnt) continue;
    if(grp->assignment == LZC_ASSIGN_BALANCED) {
      laneitem_t *l = malloc(cnt * sizeof(laneitem_t));
      uint64_t *loads = calloc(grp->pack_count, sizeof(uint64_t));
      if(!l || !loads) { free(l); free(loads); say(err, en, "Kein Speicher."); return -1; }
      size_t k = 0;
      for(size_t i = 0; i < p->ents.n; i++)
        if(p->items[i].rule && p->items[i].rule->group == g) { l[k].size = p->items[i].size; l[k].id = i; l[k].rel = p->ents.e[i].path + 6; k++; }
      qsort(l, cnt, sizeof(*l), cmp_lane_item);
      for(size_t j = 0; j < cnt; j++) {
        unsigned best = 0;
        for(unsigned x = 1; x < grp->pack_count; x++) if(loads[x] < loads[best]) best = x;
        p->items[l[j].id].lane = best;
        loads[best] += l[j].size;
      }
      free(l);
      free(loads);
    } else {
      for(size_t i = 0; i < p->ents.n; i++) {
        if(!p->items[i].rule || p->items[i].rule->group != g) continue;
        p->items[i].lane = grp->assignment == LZC_ASSIGN_HASH ? (unsigned)(path_hash(p->ents.e[i].path + 6) % grp->pack_count)
                                                               : (unsigned)(i % grp->pack_count);
      }
    }
  }
  return 0;
}

int
lzp_plan(const char *root, lzc_cfg_t *cfg, lzp_plan_t *out, const lzp_ui_t *ui, char *err, size_t en) {
  memset(out, 0, sizeof(*out));
  ui_phase(ui, "Die Dateien werden ausgewählt");
  plan_t p;
  int rc = plan_build(&p, root, cfg, ui, err, en);
  if(rc != 0) return rc;
  out->files_total = p.ents.n;
  out->pack_files = p.pack_files;
  out->pack_bytes = p.pack_bytes;
  out->loose_files = p.loose_files;
  out->loose_bytes = p.loose_bytes;
  out->chunks = p.chunks_est;
  out->block_shift = p.max_shift ? p.max_shift : cfg->default_block_shift;
  out->lanes = cfg->ngroups ? cfg->groups[0].pack_count : 1;
  plan_free(&p);
  return 0;
}

/* ---------------------------------------------------------------- the volumes */

typedef struct {
  unsigned id, group, lane, volume_no;
  char     name[200], tmp[PATH_MAX + 40], fin[PATH_MAX];
  FILE    *f;
  uint64_t pos, payload_off, max_size, padding;
  uint32_t io_page, flags, chunk_align;
} vol_t;

typedef struct {
  const lzc_cfg_t *cfg;
  const char      *outdir;
  vol_t          **v;
  unsigned         nv, capv;
  int             *cur;               /* current volume of each (group, lane); -1 none */
  unsigned        *goff;
  unsigned        *vol_no;            /* volumes made so far per (group, lane) */
} layout_t;

static int
layout_init(layout_t *l, const lzc_cfg_t *cfg, const char *outdir) {
  memset(l, 0, sizeof(*l));
  l->cfg = cfg;
  l->outdir = outdir;
  l->goff = calloc(cfg->ngroups + 1, sizeof(unsigned));
  if(!l->goff) return -1;
  unsigned total = 0;
  for(unsigned g = 0; g < cfg->ngroups; g++) { l->goff[g] = total; total += cfg->groups[g].pack_count; }
  l->goff[cfg->ngroups] = total;
  l->cur = malloc((total ? total : 1) * sizeof(int));
  l->vol_no = calloc(total ? total : 1, sizeof(unsigned));
  if(!l->cur || !l->vol_no) return -1;
  for(unsigned i = 0; i < total; i++) l->cur[i] = -1;
  return 0;
}

static void
vol_abort(vol_t *v) {
  if(v->f) { fclose(v->f); v->f = NULL; }
  unlink(v->tmp);
}

static void
layout_free(layout_t *l) {
  for(unsigned i = 0; i < l->nv; i++) { vol_abort(l->v[i]); free(l->v[i]); }
  free(l->v); free(l->cur); free(l->goff); free(l->vol_no);
  memset(l, 0, sizeof(*l));
}

/* "{id:03d}" style names: group, lane, volume and id with an optional zero-padded width */
static int
render_name(const char *pat, const char *group, unsigned lane, unsigned volume, unsigned id, char *out, size_t n) {
  size_t j = 0;
  for(const char *p = pat; *p;) {
    if(*p != '{') { if(j + 1 >= n) return -1; out[j++] = *p++; continue; }
    const char *e = strchr(p, '}');
    if(!e) return -1;
    char field[40];
    size_t fl = (size_t)(e - p - 1);
    if(fl >= sizeof(field)) return -1;
    memcpy(field, p + 1, fl);
    field[fl] = 0;
    char *spec = strchr(field, ':');
    int width = 0;
    if(spec) {
      *spec++ = 0;
      const char *s = spec;
      if(*s == '0') s++;
      width = atoi(s);
      size_t sl = strlen(spec);
      if(sl == 0 || spec[sl - 1] != 'd' || width < 0 || width > 20) return -1;
    }
    char piece[300];
    if(!strcmp(field, "group")) snprintf(piece, sizeof(piece), "%s", group);
    else if(!strcmp(field, "lane")) snprintf(piece, sizeof(piece), "%0*u", width, lane);
    else if(!strcmp(field, "volume")) snprintf(piece, sizeof(piece), "%0*u", width, volume);
    else if(!strcmp(field, "id")) snprintf(piece, sizeof(piece), "%0*u", width, id);
    else return -1;
    size_t pl = strlen(piece);
    if(j + pl + 1 >= n) return -1;
    memcpy(out + j, piece, pl);
    j += pl;
    p = e + 1;
  }
  out[j] = 0;
  return j ? 0 : -1;
}

static vol_t *
layout_new_volume(layout_t *l, unsigned g, unsigned lane, uint32_t flags, char *err, size_t en) {
  const lzc_cfg_t *c = l->cfg;
  if(l->nv >= MAX_PACKS) { say(err, en, "Das Spiel braucht mehr als 1024 Paketdateien. Bitte größere Blöcke wählen."); return NULL; }
  if(l->nv == l->capv) {
    unsigned nc = l->capv ? l->capv * 2 : 8;
    vol_t **nv = realloc(l->v, nc * sizeof(vol_t *));
    if(!nv) { say(err, en, "Kein Speicher."); return NULL; }
    l->v = nv;
    l->capv = nc;
  }
  vol_t *v = calloc(1, sizeof(*v));
  if(!v) { say(err, en, "Kein Speicher."); return NULL; }
  const lzc_group_t *grp = &c->groups[g];
  v->id = l->nv;
  v->group = g;
  v->lane = lane;
  v->volume_no = l->vol_no[l->goff[g] + lane]++;
  if(render_name(c->pack_pattern, grp->name, lane, v->volume_no, v->id, v->name, sizeof(v->name)) != 0 || !safe_rel(v->name)) {
    free(v);
    say(err, en, "Der Paketname (pack_pattern) ist ungültig.");
    return NULL;
  }
  snprintf(v->fin, sizeof(v->fin), "%s/%s", l->outdir, v->name);
  snprintf(v->tmp, sizeof(v->tmp), "%s%s", v->fin, LZP_PART_SUFFIX);
  v->io_page = (uint32_t)(grp->io_page_size ? grp->io_page_size : c->io_page_size);
  uint64_t palign = c->payload_alignment > v->io_page ? c->payload_alignment : v->io_page;
  v->payload_off = align_up(DATA_HEADER_SIZE, palign);
  v->max_size = grp->max_pack_size;
  v->chunk_align = (uint32_t)c->chunk_alignment;
  v->flags = flags | PF_IO_PAGE_LAYOUT;
  char dir[PATH_MAX + 8];
  snprintf(dir, sizeof(dir), "%s", v->fin);
  char *sl = strrchr(dir, '/');
  if(sl) { *sl = 0; mkdir_p(dir); }
  v->f = fopen(v->tmp, "w+b");
  if(!v->f) { say(err, en, "Eine Paketdatei ließ sich nicht anlegen (%s).", strerror(errno)); free(v); return NULL; }
  (void)fchmod(fileno(v->f), LZP_FILE_MODE);
  setvbuf(v->f, NULL, _IOFBF, 1u << 20);
  static const uint8_t zero[4096];
  uint64_t left = v->payload_off;
  while(left) {
    size_t w = left < sizeof(zero) ? (size_t)left : sizeof(zero);
    if(fwrite(zero, 1, w, v->f) != w) { fclose(v->f); unlink(v->tmp); free(v); say(err, en, "Schreiben in eine Paketdatei schlug fehl (%s).", strerror(errno)); return NULL; }
    left -= w;
  }
  v->pos = v->payload_off;
  v->padding = v->payload_off - DATA_HEADER_SIZE;
  l->v[l->nv++] = v;
  l->cur[l->goff[g] + lane] = (int)v->id;
  return v;
}

/* where a chunk of stored_size bytes would go in v (ampr_pack.PackVolumeWriter.placement_for) */
static void
placement(const vol_t *v, uint32_t stored, int streaming, int extent_start, uint64_t *offset, unsigned *pflags) {
  uint64_t position = v->pos, off;
  if(extent_start) position = align_up(position, v->io_page);
  if(streaming) off = align_up(position, v->chunk_align);
  else if(stored >= v->io_page) off = align_up(position, v->io_page);
  else {
    off = align_up(position, v->chunk_align);
    uint64_t page_end = align_up(off + 1, v->io_page);
    if(off + stored > page_end) off = align_up(off, v->io_page);
  }
  unsigned f = 0;
  if(stored <= v->io_page && off / v->io_page == (off + stored - 1) / v->io_page) f |= CF_PAGE_CONTAINED;
  if(off % v->io_page == 0) f |= CF_PAGE_ALIGNED;
  *offset = off;
  *pflags = f;
}

static int
vol_can_fit(const vol_t *v, uint32_t stored, int streaming, int extent_start) {
  uint64_t off;
  unsigned f;
  placement(v, stored, streaming, extent_start, &off, &f);
  uint64_t end = align_up(off + stored, v->io_page);
  return v->max_size == 0 || end <= v->max_size;
}

static vol_t *
layout_writer(layout_t *l, unsigned g, unsigned lane, uint32_t stored, int striped, int streaming, int extent_start, char *err, size_t en) {
  int cur = l->cur[l->goff[g] + lane];
  uint32_t flags = PF_IO_PAGE_LAYOUT | (striped ? PF_STRIPED : 0);
  vol_t *v = cur >= 0 ? l->v[cur] : NULL;
  if(!v) v = layout_new_volume(l, g, lane, flags, err, en);
  if(!v) return NULL;
  if(!vol_can_fit(v, stored, streaming, extent_start)) {
    if(v->f) { fclose(v->f); v->f = NULL; }                 /* full: closed, finished at the end */
    v = layout_new_volume(l, g, lane, flags, err, en);
    if(!v) return NULL;
    if(!vol_can_fit(v, stored, streaming, 1)) { say(err, en, "Ein Block passt nicht in die größte erlaubte Paketdatei."); return NULL; }
  }
  if(striped) v->flags |= PF_STRIPED;
  return v;
}

static int
vol_write(vol_t *v, const uint8_t *data, uint32_t len, uint64_t off, char *err, size_t en) {
  static const uint8_t zero[4096];
  uint64_t pad = off - v->pos;
  while(pad) {
    size_t w = pad < sizeof(zero) ? (size_t)pad : sizeof(zero);
    if(fwrite(zero, 1, w, v->f) != w) { say(err, en, "Schreiben in eine Paketdatei schlug fehl (%s).", strerror(errno)); return -1; }
    pad -= w;
    v->padding += w;
  }
  if(fwrite(data, 1, len, v->f) != len) { say(err, en, "Schreiben in eine Paketdatei schlug fehl (%s).", strerror(errno)); return -1; }
  v->pos = off + len;
  return 0;
}

/* ---------------------------------------------------------------- the compression pool */

enum { SL_FREE = 0, SL_READY, SL_BUSY, SL_DONE };

typedef struct {
  uint8_t         *raw, *out;
  uint32_t         raw_len, out_len, crc, io_page;
  uint8_t          codec;
  uint64_t         seq, h1, h2;
  uint32_t         item, chunk;
  int              last;
  int              state;
  const lzc_rule_t *rule;
} slot_t;

typedef struct {
  pthread_mutex_t mu;
  pthread_cond_t  work, done;
  slot_t         *slots;
  unsigned        nslots;
  int             stop;
  int             normal_quota, normal_used;
} pool_t;

static int g_idle_state = -1;

static int
set_idle_priority(void) {
  int ok = 0;
#if !defined(PS5TM_HOST_TEST) && defined(SYS_rtprio_thread)
  struct rtprio rtp = { RTP_PRIO_IDLE, 0 };
  int set = (int)syscall(SYS_rtprio_thread, RTP_SET, 0, &rtp);
  struct rtprio got = { 0, 0 };
  ok = set == 0 && syscall(SYS_rtprio_thread, RTP_LOOKUP, 0, &got) == 0 && got.type == RTP_PRIO_IDLE;
#endif
  __atomic_store_n(&g_idle_state, ok ? 1 : 0, __ATOMIC_RELEASE);
  return ok;
}

static void *
pool_worker(void *arg) {
  pool_t *p = arg;
  int idle = set_idle_priority();
  pthread_mutex_lock(&p->mu);
  if(!idle && p->normal_used >= p->normal_quota) { pthread_mutex_unlock(&p->mu); return NULL; }    /* one CPU stays free */
  if(!idle) p->normal_used++;
  pthread_mutex_unlock(&p->mu);
  cstate_t cs;
  int ok = cstate_init(&cs) == 0;
  pthread_mutex_lock(&p->mu);
  for(;;) {
    slot_t *s = NULL;
    while(!p->stop) {
      for(unsigned i = 0; i < p->nslots; i++) {
        slot_t *x = &p->slots[i];
        if(x->state == SL_READY && (!s || x->seq < s->seq)) s = x;
      }
      if(s) break;
      pthread_cond_wait(&p->work, &p->mu);
    }
    if(!s) break;
    s->state = SL_BUSY;
    pthread_mutex_unlock(&p->mu);
    s->crc = crc32_of(s->raw, s->raw_len);
    hash128(s->raw, s->raw_len, &s->h1, &s->h2);
    if(ok) compress_block(&cs, s->raw, s->raw_len, s->rule, s->io_page, s->out, &s->out_len, &s->codec);
    else { s->codec = CODEC_RAW; s->out_len = s->raw_len; }
    if(!idle) sched_yield();
    pthread_mutex_lock(&p->mu);
    s->state = SL_DONE;
    pthread_cond_broadcast(&p->done);
  }
  pthread_mutex_unlock(&p->mu);
  if(ok) cstate_free(&cs);
  return NULL;
}

/* ---------------------------------------------------------------- the table of equal blocks */

typedef struct {
  uint64_t h1, h2, loc;
  uint32_t crc, meta, stored;
  uint8_t  flags, used;
} dd_t;

typedef struct {
  dd_t    *t;
  size_t   slots, n;
} ddt_t;

#define DD_MAX_SLOTS (1u << 20)

static dd_t *
dd_find(ddt_t *d, uint64_t h1, uint64_t h2, uint32_t crc, uint32_t meta) {
  if(!d->t) return NULL;
  size_t i = (size_t)(h1 ^ (h2 >> 7)) & (d->slots - 1);
  while(d->t[i].used) {
    dd_t *e = &d->t[i];
    if(e->h1 == h1 && e->h2 == h2 && e->crc == crc && e->meta == meta) return e;
    i = (i + 1) & (d->slots - 1);
  }
  return NULL;
}

static void
dd_put(ddt_t *d, uint64_t h1, uint64_t h2, uint32_t crc, uint32_t meta, uint64_t loc, uint32_t stored, uint8_t flags) {
  if(!d->t) {
    d->slots = 1u << 14;
    d->t = calloc(d->slots, sizeof(dd_t));
    if(!d->t) return;
  }
  if((d->n + 1) * 10 > d->slots * 7) {                                   /* load factor 0.7: grow, or stop at the cap */
    if(d->slots >= DD_MAX_SLOTS) return;
    size_t ns = d->slots * 2;
    dd_t *nt = calloc(ns, sizeof(dd_t));
    if(!nt) return;
    for(size_t k = 0; k < d->slots; k++) {
      if(!d->t[k].used) continue;
      size_t i = (size_t)(d->t[k].h1 ^ (d->t[k].h2 >> 7)) & (ns - 1);
      while(nt[i].used) i = (i + 1) & (ns - 1);
      nt[i] = d->t[k];
    }
    free(d->t);
    d->t = nt;
    d->slots = ns;
  }
  size_t i = (size_t)(h1 ^ (h2 >> 7)) & (d->slots - 1);
  while(d->t[i].used) i = (i + 1) & (d->slots - 1);
  d->t[i] = (dd_t){ h1, h2, loc, crc, meta, stored, flags, 1 };
  d->n++;
}

/* ---------------------------------------------------------------- lzp_pack */

typedef struct {
  uint64_t *loc;
  uint32_t *desc, *crc;
  size_t    n, cap;
} chunks_t;

static int
chunks_push(chunks_t *c, uint64_t loc, uint32_t desc, uint32_t crc) {
  if(c->n == c->cap) {
    size_t nc = c->cap ? c->cap * 2 : 1u << 16;
    uint64_t *nl = realloc(c->loc, nc * sizeof(uint64_t));
    if(!nl) return -1;
    c->loc = nl;
    uint32_t *nd = realloc(c->desc, nc * sizeof(uint32_t));
    if(!nd) return -1;
    c->desc = nd;
    uint32_t *nr = realloc(c->crc, nc * sizeof(uint32_t));
    if(!nr) return -1;
    c->crc = nr;
    c->cap = nc;
  }
  c->loc[c->n] = loc;
  c->desc[c->n] = desc;
  c->crc[c->n] = crc;
  c->n++;
  return 0;
}

static int
write_tmp(const char *path, const uint8_t *a, size_t an, const uint8_t *b, size_t bn) {
  int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, LZP_FILE_MODE);
  if(fd < 0) return -1;
  (void)fchmod(fd, LZP_FILE_MODE);
  if((an && write_all(fd, a, an) != 0) || (bn && write_all(fd, b, bn) != 0) || fsync(fd) != 0) { close(fd); unlink(path); return -1; }
  return close(fd);
}

int
lzp_pack(const char *root, const char *outdir, lzc_cfg_t *cfg, lzp_stats_t *st, const lzp_ui_t *ui, char *err, size_t en) {
  lzp_stats_t zero_stats;
  if(!st) st = &zero_stats;
  memset(st, 0, sizeof(*st));
  plan_t plan;
  layout_t lay;
  pool_t pool;
  chunks_t ch = { 0 };
  ddt_t dd = { 0 };
  man_t man;
  memset(&man, 0, sizeof(man));
  memset(&lay, 0, sizeof(lay));
  memset(&pool, 0, sizeof(pool));
  int rc = -1, pool_up = 0, plan_up = 0;
  pthread_t *th = NULL;
  unsigned nth = 0;
  int fd = -1;
  struct stat src0;
  uint8_t *manb = NULL, *crcb = NULL, *rtb = NULL;
  char tmp_index[PATH_MAX + 40] = "", tmp_crc[PATH_MAX + 60] = "", tmp_rt[PATH_MAX + 60] = "";
  char fin_index[PATH_MAX], fin_crc[PATH_MAX + 8], fin_rt[PATH_MAX + 16];
  unsigned renamed_vols = 0;
  int renamed_idx = 0, renamed_crc = 0, renamed_rt = 0;

  snprintf(fin_index, sizeof(fin_index), "%s/%s", outdir, cfg->index_name);
  snprintf(fin_crc, sizeof(fin_crc), "%s.crc", fin_index);
  snprintf(fin_rt, sizeof(fin_rt), "%s.runtime", fin_index);
  if(file_exists(fin_index)) { say(err, en, "Hier gibt es schon LZ4-Pakete."); return -1; }

  ui_phase(ui, "Die Dateien werden ausgewählt");
  rc = plan_build(&plan, root, cfg, ui, err, en);
  if(rc != 0) return rc;
  plan_up = 1;
  rc = -1;
  /* the automatic profile grows its block size until the chunk table fits the format */
  while(cfg->automatic && plan.chunks_est > 12000000ull && cfg->rules[0].block_shift < MAX_BLOCK_SHIFT) {
    cfg->rules[0].block_shift++;
    uint64_t est = 0;
    for(size_t i = 0; i < plan.ents.n; i++)
      if(plan.items[i].rule) est += (plan.items[i].size + (1ull << cfg->rules[0].block_shift) - 1) >> cfg->rules[0].block_shift;
    plan.chunks_est = est;
  }
  if(plan.ents.n > MAX_FILES) { say(err, en, "Das Spiel hat zu viele Dateien für die Pakete (mehr als 2.000.000)."); goto out; }
  if(plan.chunks_est > MAX_CHUNKS) { say(err, en, "Das Spiel brauchte mehr als 16 Millionen Blöcke. Bitte größere Blöcke wählen oder weniger Dateien packen."); goto out; }
  if(cfg->nreq) {
    for(size_t i = 0; i < plan.ents.n; i++) {
      if(plan.items[i].rule || lzc_keep_on_disk(plan.ents.e[i].path + 6, plan.ents.e[i].size)) continue;
      for(unsigned k = 0; k < cfg->nreq; k++)
        if(lzc_glob(cfg->required_packed[k], plan.ents.e[i].path + 6)) { say(err, en, "required_packed: %s bliebe lose.", plan.ents.e[i].path + 6); goto out; }
    }
  }
  if(plan.pack_files == 0) { say(err, en, "Nach diesem Profil gäbe es nichts zu packen."); goto out; }
  if(plan_lanes(&plan, err, en) != 0) goto out;
  if(layout_init(&lay, cfg, outdir) != 0) { say(err, en, "Kein Speicher."); goto out; }
  mkdir_p(outdir);

  /* file records: loose ones now, packed ones as their chunks arrive */
  man.nf = plan.ents.n;
  man.f = calloc(man.nf ? man.nf : 1, sizeof(frec_t));
  if(!man.f) { say(err, en, "Kein Speicher."); goto out; }
  /* packing_class: the rank of the group name among all names */
  for(size_t i = 0; i < man.nf; i++) {
    frec_t *f = &man.f[i];
    f->path_hash = path_hash(plan.ents.e[i].path);
    f->path_off = plan.path_off[i];
    f->path_len = plan.path_len[i];
    const item_t *it = &plan.items[i];
    if(!it->rule) { f->logical = plan.ents.e[i].size; f->mtime = plan.ents.e[i].mtime; st->files_loose++; continue; }
    f->logical = it->size;
    f->mtime = it->mtime;
    f->flags = it->flags;
    f->shift = (uint8_t)it->rule->block_shift;
    unsigned rank = 0;
    for(unsigned g = 0; g < cfg->ngroups; g++) if(strcmp(cfg->groups[g].name, cfg->groups[it->rule->group].name) < 0) rank++;
    f->pclass = (uint8_t)rank;
  }
  st->files_total = plan.ents.n;
  st->files_auto_loose = plan.auto_loose;
  st->loose_bytes = plan.loose_bytes;
  ui_total(ui, plan.pack_bytes);
  ui_files(ui, 0, (unsigned)plan.pack_files);
  ui_phase(ui, "Die Dateien werden gepackt");

  /* the pool */
  unsigned workers = cfg->workers ? cfg->workers : 3;
  if(workers > 16) workers = 16;
  pool.nslots = workers * 3 + 2;
  pool.slots = calloc(pool.nslots, sizeof(slot_t));
  if(!pool.slots) { say(err, en, "Kein Speicher."); goto out; }
  uint32_t maxblock = 1u << plan.max_shift;
  for(unsigned i = 0; i < pool.nslots; i++) {
    pool.slots[i].raw = malloc(maxblock);
    pool.slots[i].out = malloc(maxblock + 64);
    if(!pool.slots[i].raw || !pool.slots[i].out) { say(err, en, "Kein Speicher."); goto out; }
  }
  pthread_mutex_init(&pool.mu, NULL);
  pthread_cond_init(&pool.work, NULL);
  pthread_cond_init(&pool.done, NULL);
  pool.normal_quota = workers > 1 ? (int)workers - 1 : 1;
  th = calloc(workers, sizeof(pthread_t));
  if(!th) { say(err, en, "Kein Speicher."); goto out; }
  pool_up = 1;
  for(unsigned i = 0; i < workers; i++) {
    pthread_attr_t at;
    pthread_attr_init(&at);
    pthread_attr_setstacksize(&at, 512 * 1024);
    if(pthread_create(&th[nth], &at, pool_worker, &pool) == 0) nth++;
    pthread_attr_destroy(&at);
  }
  if(nth == 0) { say(err, en, "Die Rechenfäden ließen sich nicht starten."); goto out; }

  /* the loop: read blocks in order into free slots, write finished ones in order */
  size_t cur_item = 0;                 /* next item to read blocks of */
  uint64_t cur_off = 0;                /* next offset in it */
  uint32_t cur_chunk = 0;
  uint64_t seq_next = 0, seq_write = 0;
  size_t first_chunk_of_item = 0;
  unsigned files_done = 0;
  uint64_t done_bytes_report = 0;
  int reading = 0;                     /* an item is open */
  int err_state = 0;
  for(;;) {
    /* fill slots */
    for(;;) {
      if(err_state) break;
      if(!reading) {
        while(cur_item < plan.ents.n && !plan.items[cur_item].rule) cur_item++;
        if(cur_item >= plan.ents.n) break;
        const item_t *it = &plan.items[cur_item];
        fd = open(it->src, O_RDONLY);
        if(fd < 0 || fstat(fd, &src0) != 0) { say(err, en, "Eine Datei ließ sich nicht öffnen: %s", it->src + strlen(root) + 1); err_state = 1; break; }
        cur_off = 0;
        cur_chunk = 0;
        reading = 1;
        ui_current(ui, it->src + strlen(root) + 1);
      }
      slot_t *s = NULL;
      pthread_mutex_lock(&pool.mu);
      for(unsigned i = 0; i < pool.nslots; i++) if(pool.slots[i].state == SL_FREE) { s = &pool.slots[i]; break; }
      pthread_mutex_unlock(&pool.mu);
      if(!s) break;
      const item_t *it = &plan.items[cur_item];
      uint64_t bs = 1ull << it->rule->block_shift;
      uint32_t len = (uint32_t)(it->size - cur_off < bs ? it->size - cur_off : bs);
      if(read_all(fd, s->raw, len, cur_off) != 0) { say(err, en, "Eine Datei ist beim Packen kürzer geworden: %s", it->src + strlen(root) + 1); err_state = 1; break; }
      s->raw_len = len;
      s->rule = it->rule;
      s->io_page = it->io_page;
      s->item = (uint32_t)cur_item;
      s->chunk = cur_chunk;
      s->seq = seq_next++;
      cur_off += len;
      cur_chunk++;
      s->last = cur_off >= it->size;
      if(s->last) {
        struct stat st2;
        if(fstat(fd, &st2) != 0 || st2.st_size != src0.st_size || st2.st_mtime != src0.st_mtime) {
          say(err, en, "Die Datei hat sich während des Packens verändert: %s", it->src + strlen(root) + 1);
          err_state = 1;
          break;
        }
        close(fd);
        fd = -1;
        reading = 0;
        cur_item++;
      }
      pthread_mutex_lock(&pool.mu);
      s->state = SL_READY;
      pthread_cond_signal(&pool.work);
      pthread_mutex_unlock(&pool.mu);
    }
    if(err_state) break;
    if(cancelled(ui)) { say(err, en, "Abgebrochen."); rc = ECANCELED; goto out; }
    if(seq_write == seq_next) {                     /* nothing in flight: finished when nothing is left to read */
      if(!reading && cur_item >= plan.ents.n) break;
      continue;
    }
    /* take the next finished one in order */
    slot_t *s = NULL;
    pthread_mutex_lock(&pool.mu);
    for(;;) {
      for(unsigned i = 0; i < pool.nslots; i++)
        if(pool.slots[i].seq == seq_write && pool.slots[i].state == SL_DONE) { s = &pool.slots[i]; break; }
      if(s) break;
      struct timespec ts;
      clock_gettime(CLOCK_REALTIME, &ts);
      ts.tv_nsec += 200 * 1000 * 1000;
      if(ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
      pthread_cond_timedwait(&pool.done, &pool.mu, &ts);
      if(cancelled(ui)) break;
    }
    pthread_mutex_unlock(&pool.mu);
    if(!s) { say(err, en, "Abgebrochen."); rc = ECANCELED; goto out; }
    seq_write++;

    const item_t *it = &plan.items[s->item];
    const lzc_rule_t *rule = it->rule;
    const lzc_group_t *grp = &cfg->groups[rule->group];
    frec_t *fr = &man.f[s->item];
    if(s->chunk == 0) first_chunk_of_item = ch.n;
    int streaming = it->layout == LZC_LAY_STREAMING;
    int striped = grp->stripe_large && grp->pack_count > 1 && it->size >= grp->stripe_threshold;
    unsigned lane = it->lane;
    if(striped) lane = (it->lane + s->chunk / grp->stripe_group_blocks) % grp->pack_count;
    uint32_t stored_len = s->codec == CODEC_RAW ? s->raw_len : s->out_len;
    const uint8_t *stored = s->codec == CODEC_RAW ? s->raw : s->out;
    uint32_t domain = cfg->dedupe_group_scope ? rule->group : rule->group * 65536u + lane;
    uint32_t meta = domain | (streaming ? 1u << 28 : 0) | ((uint32_t)s->codec << 29);
    int allow_dd = cfg->dedupe && (!streaming || cfg->dedupe_streaming);
    dd_t *prior = allow_dd ? dd_find(&dd, s->h1, s->h2, s->crc, meta) : NULL;
    uint64_t loc;
    uint32_t desc;
    if(prior && prior->stored == stored_len) {
      unsigned fl = CF_SHARED | (prior->flags & (CF_PAGE_CONTAINED | CF_PAGE_ALIGNED)) | (streaming ? CF_STREAMING : 0);
      loc = prior->loc;
      desc = (stored_len - 1) | ((uint32_t)s->codec << 20) | (fl << 22);
      st->chunks_shared++;
    } else {
      int extent_start = streaming && (s->chunk == 0 || (striped && s->chunk % grp->stripe_group_blocks == 0));
      vol_t *v = layout_writer(&lay, rule->group, lane, stored_len, striped, streaming, extent_start, err, en);
      if(!v) goto out;
      uint64_t off;
      unsigned pf;
      placement(v, stored_len, streaming, extent_start, &off, &pf);
      if(vol_write(v, stored, stored_len, off, err, en) != 0) goto out;
      unsigned fl = pf | (streaming ? CF_STREAMING : 0);
      int page_safe = stored_len <= v->io_page ? (pf & CF_PAGE_CONTAINED) != 0 : (pf & CF_PAGE_ALIGNED) != 0;
      if(!streaming && !page_safe) { say(err, en, "Ein Block wurde nicht seitensicher abgelegt."); goto out; }
      loc = off | ((uint64_t)v->id << 48);
      desc = (stored_len - 1) | ((uint32_t)s->codec << 20) | (fl << 22);
      if(allow_dd) dd_put(&dd, s->h1, s->h2, s->crc, meta, loc, stored_len, (uint8_t)fl);
      st->stored_bytes += stored_len;
    }
    if(chunks_push(&ch, loc, desc, s->crc) != 0) { say(err, en, "Kein Speicher."); goto out; }
    st->chunks++;
    st->logical_bytes += s->raw_len;
    if(s->codec == CODEC_LZ4) st->chunks_lz4++; else st->chunks_raw++;
    if(s->last) {
      fr->first = (uint32_t)first_chunk_of_item;
      fr->count = (uint32_t)(ch.n - first_chunk_of_item);
      st->files_packed++;
      files_done++;
      ui_files(ui, files_done, (unsigned)plan.pack_files);
    }
    done_bytes_report += s->raw_len;
    if(done_bytes_report >= (4u << 20)) { ui_work(ui, done_bytes_report); done_bytes_report = 0; }
    pthread_mutex_lock(&pool.mu);
    s->state = SL_FREE;
    pthread_mutex_unlock(&pool.mu);
  }
  if(err_state) goto out;
  ui_work(ui, done_bytes_report);

  /* the pool is done */
  pthread_mutex_lock(&pool.mu);
  pool.stop = 1;
  pthread_cond_broadcast(&pool.work);
  pthread_mutex_unlock(&pool.mu);
  for(unsigned i = 0; i < nth; i++) pthread_join(th[i], NULL);
  nth = 0;

  ui_phase(ui, "Das Paketverzeichnis wird geschrieben");
  /* strings: the paths, then the names of the volumes */
  man.nc = ch.n;
  man.loc = ch.loc;
  man.desc = ch.desc;
  man.np = lay.nv;
  man.p = calloc(lay.nv ? lay.nv : 1, sizeof(prec_t));
  if(!man.p) { say(err, en, "Kein Speicher."); goto out; }
  for(unsigned i = 0; i < lay.nv; i++) {
    vol_t *v = lay.v[i];
    uint32_t off;
    if(str_add(&plan, v->name, strlen(v->name), &off) != 0) { say(err, en, "Kein Speicher."); goto out; }
    uint64_t final_size = align_up(v->pos, v->io_page);
    man.p[i].payload_bytes = final_size - v->payload_off;
    man.p[i].file_size = final_size;
    man.p[i].name_off = off;
    man.p[i].name_len = (uint32_t)strlen(v->name);
    man.p[i].flags = v->flags;
    man.p[i].io_page = v->io_page;
    st->padding_bytes += v->padding + (final_size - v->pos);
  }
  st->packs = lay.nv;
  man.str = plan.strings;
  man.nstr = plan.nstr;
  man.flags = 0;
  /* the build id: SHA-256 over the whole manifest with a zero id, and the CRC list */
  {
    size_t n0;
    memset(man.build, 0, 16);
    uint8_t *m0 = man_serialize(&man, &n0);
    if(!m0) { say(err, en, "Kein Speicher."); goto out; }
    ps5tm_sha256_t sh;
    uint8_t dg[32];
    ps5tm_sha256_init(&sh);
    ps5tm_sha256_update(&sh, "AMPRPACK4-PS5CC", 15);
    ps5tm_sha256_update(&sh, m0, n0);
    ps5tm_sha256_update(&sh, ch.crc, ch.n * 4);
    ps5tm_sha256_final(&sh, dg);
    free(m0);
    memcpy(man.build, dg, 16);
  }
  size_t manlen = 0;
  manb = man_serialize(&man, &manlen);
  if(!manb) { say(err, en, "Kein Speicher."); goto out; }
  /* the volumes: tail padding, header, sync */
  ui_phase(ui, "Die Paketdateien werden abgeschlossen");
  for(unsigned i = 0; i < lay.nv; i++) {
    vol_t *v = lay.v[i];
    if(v->f) { if(fflush(v->f) != 0 || fclose(v->f) != 0) { v->f = NULL; say(err, en, "Schreiben in eine Paketdatei schlug fehl (%s).", strerror(errno)); goto out; } v->f = NULL; }
    int vfd = open(v->tmp, O_RDWR);
    if(vfd < 0) { say(err, en, "Eine Paketdatei ließ sich nicht wieder öffnen."); goto out; }
    uint64_t final_size = man.p[i].file_size;
    uint8_t h[DATA_HEADER_SIZE];
    memset(h, 0, sizeof(h));
    memcpy(h, k_data_magic, 8);
    p32(h, 8, 3); p32(h, 12, DATA_HEADER_SIZE); p32(h, 16, v->id); p32(h, 20, v->flags);
    memcpy(h + 24, man.build, 16);
    p64(h, 40, v->payload_off); p64(h, 48, man.p[i].payload_bytes);
    p32(h, 56, crc32_of(h, sizeof(h)));
    if(ftruncate(vfd, (off_t)final_size) != 0 || pwrite(vfd, h, sizeof(h), 0) != (ssize_t)sizeof(h) || fsync(vfd) != 0) {
      close(vfd);
      say(err, en, "Eine Paketdatei ließ sich nicht abschließen (%s).", strerror(errno));
      goto out;
    }
    close(vfd);
  }
  /* the CRC sidecar */
  crcb = malloc(CRC_HEADER_SIZE);
  if(!crcb) { say(err, en, "Kein Speicher."); goto out; }
  memset(crcb, 0, CRC_HEADER_SIZE);
  uint8_t *crcpay = malloc(ch.n * 4 + 1);
  if(!crcpay) { say(err, en, "Kein Speicher."); goto out; }
  for(size_t i = 0; i < ch.n; i++) p32(crcpay, i * 4, ch.crc[i]);
  memcpy(crcb, k_crc_magic, 8);
  p32(crcb, 8, 1); p32(crcb, 12, CRC_HEADER_SIZE);
  memcpy(crcb + 16, man.build, 16);
  p64(crcb, 32, ch.n);
  p32(crcb, 40, crc32_of(crcpay, ch.n * 4));
  p32(crcb, 44, 0);
  p32(crcb, 44, crc32_of(crcb, CRC_HEADER_SIZE));
  snprintf(tmp_crc, sizeof(tmp_crc), "%s%s", fin_crc, LZP_PART_SUFFIX);
  int wr = write_tmp(tmp_crc, crcb, CRC_HEADER_SIZE, crcpay, ch.n * 4);
  free(crcpay);
  if(wr != 0) { tmp_crc[0] = 0; say(err, en, "Die Prüfsummen-Datei ließ sich nicht schreiben (%s).", strerror(errno)); goto out; }
  /* the runtime sidecar */
  if(cfg->has_runtime) {
    rtb = calloc(1, 64);
    if(!rtb) { say(err, en, "Kein Speicher."); goto out; }
    memcpy(rtb, "AMPRCFG1", 8);
    p32(rtb, 8, 1); p32(rtb, 12, 64);
    memcpy(rtb + 16, man.build, 16);
    p64(rtb, 32, cfg->rt_decoded); p64(rtb, 40, cfg->rt_physical);
    p32(rtb, 48, cfg->rt_workers); p32(rtb, 52, cfg->rt_reserve);
    p32(rtb, 56, 0);
    p32(rtb, 56, crc32_of(rtb, 64));
    snprintf(tmp_rt, sizeof(tmp_rt), "%s%s", fin_rt, LZP_PART_SUFFIX);
    if(write_tmp(tmp_rt, rtb, 64, NULL, 0) != 0) { tmp_rt[0] = 0; say(err, en, "Die Laufzeit-Datei ließ sich nicht schreiben (%s).", strerror(errno)); goto out; }
  }
  snprintf(tmp_index, sizeof(tmp_index), "%s%s", fin_index, LZP_PART_SUFFIX);
  if(write_tmp(tmp_index, manb, manlen, NULL, 0) != 0) { tmp_index[0] = 0; say(err, en, "Das Paketverzeichnis ließ sich nicht schreiben (%s).", strerror(errno)); goto out; }

  /* publish: volumes, sidecars, the manifest last */
  ui_phase(ui, "Die Pakete werden veröffentlicht");
  for(unsigned i = 0; i < lay.nv; i++) {
    if(rename(lay.v[i]->tmp, lay.v[i]->fin) != 0) { say(err, en, "Eine Paketdatei ließ sich nicht umbenennen (%s).", strerror(errno)); goto out; }
    renamed_vols = i + 1;
  }
  if(rename(tmp_crc, fin_crc) != 0) { say(err, en, "Die Prüfsummen-Datei ließ sich nicht umbenennen (%s).", strerror(errno)); goto out; }
  renamed_crc = 1;
  tmp_crc[0] = 0;
  if(cfg->has_runtime) {
    if(rename(tmp_rt, fin_rt) != 0) { say(err, en, "Die Laufzeit-Datei ließ sich nicht umbenennen (%s).", strerror(errno)); goto out; }
    renamed_rt = 1;
    tmp_rt[0] = 0;
  }
  if(rename(tmp_index, fin_index) != 0) { say(err, en, "Das Paketverzeichnis ließ sich nicht umbenennen (%s).", strerror(errno)); goto out; }
  renamed_idx = 1;
  tmp_index[0] = 0;
  rc = 0;
out:
  if(fd >= 0) close(fd);
  if(pool_up) {
    pthread_mutex_lock(&pool.mu);
    pool.stop = 1;
    pthread_cond_broadcast(&pool.work);
    pthread_mutex_unlock(&pool.mu);
    for(unsigned i = 0; i < nth; i++) pthread_join(th[i], NULL);
    pthread_mutex_destroy(&pool.mu);
    pthread_cond_destroy(&pool.work);
    pthread_cond_destroy(&pool.done);
  }
  if(pool.slots) for(unsigned i = 0; i < pool.nslots; i++) { free(pool.slots[i].raw); free(pool.slots[i].out); }
  free(pool.slots);
  free(th);
  if(rc != 0) {
    /* take back what was published, remove what is temporary */
    if(renamed_idx) unlink(fin_index);
    if(renamed_rt) unlink(fin_rt);
    if(renamed_crc) unlink(fin_crc);
    for(unsigned i = 0; i < renamed_vols; i++) unlink(lay.v[i]->fin);
    if(tmp_index[0]) unlink(tmp_index);
    if(tmp_crc[0]) unlink(tmp_crc);
    if(tmp_rt[0]) unlink(tmp_rt);
  }
  layout_free(&lay);
  free(manb);
  free(crcb);
  free(rtb);
  /* man.str is plan.strings, man.loc/desc are ch's: they must not be freed twice */
  free(man.f);
  free(man.p);
  free(ch.loc); free(ch.desc); free(ch.crc);
  free(dd.t);
  if(plan_up) plan_free(&plan);
  return rc;
}
