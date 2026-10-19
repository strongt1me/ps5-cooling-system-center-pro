/* The packing profile of the LZ4 asset packs: built-in rules and a reader for the TOML profiles of drakmor's
 * ampr_pack.py (https://github.com/drakmor/ampr_emu, tools/ampr_pack.py load_config, GPL-3.0). See lz4cfg.h. */
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <strings.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "lz4cfg.h"

#define DATA_HEADER_SIZE 64u

/* ------------------------------------------------------------------------------------------------ small helpers */

static char *
dup_n(const char *s, size_t n) {
  char *d = malloc(n + 1);
  if(!d) return NULL;
  memcpy(d, s, n);
  d[n] = 0;
  return d;
}

static int
add_str(char ***arr, unsigned *n, const char *s, size_t len) {
  char **na = realloc(*arr, (*n + 1) * sizeof(char *));
  if(!na) return -1;
  *arr = na;
  char *d = dup_n(s, len);
  if(!d) return -1;
  na[(*n)++] = d;
  return 0;
}

static void
free_strs(char **a, unsigned n) {
  for(unsigned i = 0; i < n; i++) free(a[i]);
  free(a);
}

/* a plain test: the SDK's isfinite() macro trips -Werror */
static int
fin_d(double d) {
  return d == d && d <= 1.7e308 && d >= -1.7e308;
}

int
lzc_parse_size(const char *s, uint64_t *out) {
  char buf[64];
  size_t j = 0;
  while(*s == ' ' || *s == '\t') s++;
  for(; *s && j + 1 < sizeof(buf); s++)
    if(*s != '_') buf[j++] = *s;
  buf[j] = 0;
  while(j && (buf[j - 1] == ' ' || buf[j - 1] == '\t')) buf[--j] = 0;
  if(!j) return -1;
  size_t split = j;
  while(split && isalpha((unsigned char)buf[split - 1])) split--;
  char num[64], suf[16];
  memcpy(num, buf, split);
  num[split] = 0;
  size_t sl = j - split;
  if(sl >= sizeof(suf)) return -1;
  for(size_t i = 0; i < sl; i++) suf[i] = (char)tolower((unsigned char)buf[split + i]);
  suf[sl] = 0;
  if(!suf[0]) strcpy(suf, "b");
  static const struct { const char *n; double m; } U[] = {
    {"b", 1}, {"k", 1e3}, {"kb", 1e3}, {"kib", 1024.0}, {"m", 1e6}, {"mb", 1e6}, {"mib", 1048576.0},
    {"g", 1e9}, {"gb", 1e9}, {"gib", 1073741824.0}, {"t", 1e12}, {"tb", 1e12}, {"tib", 1099511627776.0} };
  double mult = 0;
  for(size_t i = 0; i < sizeof(U) / sizeof(U[0]); i++)
    if(!strcmp(U[i].n, suf)) mult = U[i].m;
  if(mult == 0) return -1;
  char *end = NULL;
  double v = strtod(num, &end);
  if(end == num || *end || !fin_d(v) || v < 0 || v * mult >= 1.8e19) return -1;
  *out = (uint64_t)(v * mult);
  return 0;
}

/* ------------------------------------------------------------------------------------------------ fnmatch */

/* [seq] of Python's fnmatch.translate: 1 / 0 for a match / no match, -1 when the bracket never closes (a literal '['). */
static int
class_match(const char *p, int c, const char **after) {
  const char *j = p + 1;
  int neg = 0;
  if(*j == '!') { neg = 1; j++; }
  const char *body = j;
  if(*j == ']') j++;
  while(*j && *j != ']') j++;
  if(!*j) return -1;
  int ok = 0;
  for(const char *k = body; k < j;) {
    if(k + 2 < j && k[1] == '-') {
      if((unsigned char)*k <= c && c <= (unsigned char)k[2]) ok = 1;
      k += 3;
    } else {
      if((unsigned char)*k == c) ok = 1;
      k++;
    }
  }
  *after = j + 1;
  return neg ? !ok : ok;
}

int
lzc_glob(const char *p, const char *s) {
  const char *star = NULL, *ss = NULL;
  while(*s) {
    if(*p == '*') { star = p++; ss = s; continue; }
    if(*p == '?') { p++; s++; continue; }
    if(*p == '[') {
      const char *after = NULL;
      int r = class_match(p, (unsigned char)*s, &after);
      if(r == 1) { p = after; s++; continue; }
      if(r == -1 && *s == '[') { p++; s++; continue; }
    } else if(*p && *p == *s) { p++; s++; continue; }
    if(star) { p = star + 1; s = ++ss; continue; }
    return 0;
  }
  while(*p == '*') p++;
  return !*p;
}

/* a pattern or a path with "\" as "/" and without leading slashes, like ampr_pack_format.glob_matches */
static void
norm_path(const char *in, char *out, size_t n) {
  while(*in == '/' || *in == '\\') in++;
  size_t j = 0;
  for(; *in && j + 1 < n; in++) out[j++] = *in == '\\' ? '/' : *in;
  out[j] = 0;
}

static int
glob_any(char **pats, unsigned n, const char *rel) {
  char p[1100];
  for(unsigned i = 0; i < n; i++) {
    norm_path(pats[i], p, sizeof(p));
    if(lzc_glob(p, rel)) return 1;
  }
  return 0;
}

/* ------------------------------------------------------------------------------------------------ protected paths */

static void
lower_copy(const char *s, char *out, size_t n) {
  size_t j = 0;
  for(; s[j] && j + 1 < n; j++) out[j] = (char)tolower((unsigned char)s[j]);
  out[j] = 0;
}

int
lzc_protected(const char *rel) {
  char l[1100];
  norm_path(rel, l, sizeof(l));
  lower_copy(l, l, sizeof(l));
  static const char *const dirs[] = { "mods", "save", "sce_module", "sce_sys", "system", "fakelib", "fakelib2", "backports", NULL };
  const char *slash = strchr(l, '/');
  size_t first = slash ? (size_t)(slash - l) : strlen(l);
  if(slash)
    for(int i = 0; dirs[i]; i++)
      if(strlen(dirs[i]) == first && !memcmp(l, dirs[i], first)) return 1;
  const char *name = slash ? strrchr(l, '/') + 1 : l;
  static const char *const names[] = { "ampr_assets.index", "ampr_assets.index.crc", "ampr_assets.index.runtime", "ampr_emu.index",
    "ampr_emu.index.bak", "ampr_emu.index.tmp", "ampr_commands.bin", "apr_emu.log", "eboot.bin", "nptitle.dat", "param.sfo",
    "playgo_stub.dat", NULL };
  for(int i = 0; names[i]; i++)
    if(!strcmp(name, names[i])) return 1;
  const char *dot = strrchr(name, '.');
  if(dot && (!strcmp(dot, ".elf") || !strcmp(dot, ".prx") || !strcmp(dot, ".self") || !strcmp(dot, ".sprx"))) return 1;
  size_t nl = strlen(name);
  if(!strncmp(name, "ampr_assets-", 12) && nl > 16 && !strcmp(name + nl - 4, ".pak")) return 1;
  /* the temporary names of this app */
  if(nl > 11 && !strcmp(name + nl - 11, ".ps5cc-teil")) return 1;
  return 0;
}

/* Files the PS5 looks for on disk outside the game process, where the APR-EMU cannot show packed files: PlayGo's chunk
 * markers and the Sucker Punch boot files Ghost of Yotei needs to get past a black screen (PPSA26344 02.002.000). The list is
 * that of LZ4 Compression 1.4.3 by Husam Osman (GPL-3.0, read on 15.10.2026; the files were found by edu-ruiz- on Reddit).
 * Empty files stay too: PlayGo's language placeholders are empty. Remedy's pack tables (*.rmdtoc) stay since 18.10.2026: Alan Wake 2
 * opens them with a plain open that the APR-EMU does not serve from packs and then stops with "No packfiles found!" (found with
 * MkPFS.CSharp 2.5.0 by danthespal; the .rmdblob data is read through APR and is still packed). Upper and lower case do not count. */
int
lzc_keep_on_disk(const char *rel, uint64_t size) {
  if(size == 0) return 1;
  static const char *const keep[] = {
    "cache_ps5/pgc_*_dummy_file", "cache_ps5/playgo.pgm", "cache_ps5/load_menu_catalog.log", "cache_ps5/all_shaders.texmeshman",
    "cache_ps5/game.sprig.packman", "cache_ps5/game.sprig.texmeshman", "cache_ps5/pulse.sprig.texmeshman",
    "sounds/scream/_generated/buss_config.bus", "sounds/scream/_generated/distance_model.dml",
    "sounds/scream/_generated/group_mix.gmx", "*.rmdtoc", NULL };
  char l[1100];
  norm_path(rel, l, sizeof(l));
  lower_copy(l, l, sizeof(l));
  for(int i = 0; keep[i]; i++)
    if(lzc_glob(keep[i], l)) return 1;
  return 0;
}

/* Media that is already compressed: the automatic profile leaves it loose (packing it would only cost decoding). */
static int
media_ext(const char *rel) {
  const char *dot = strrchr(rel, '.');
  const char *slash = strrchr(rel, '/');
  if(!dot || (slash && dot < slash)) return 0;
  char e[16];
  lower_copy(dot + 1, e, sizeof(e));
  static const char *const ex[] = { "mp4", "m4v", "webm", "bk2", "usm", "ogg", "ogv", "opus", "wem", "mp3", "m4a", "aac", "flac", "at9",
    "avi", "mkv", "mov", "wmv", "bik", "smk", "vp9", "wma", NULL };
  for(int i = 0; ex[i]; i++)
    if(!strcmp(e, ex[i])) return 1;
  return 0;
}

/* What the automatic profile leaves loose besides the protected paths and the compressed media (13.10.2026, after the list of
 * exFAT2AMPR by V-10 and Lazy_AMPR's safety list, which both ran on a console): files that sit directly in the game folder, data
 * files that engines read through the file system or mapped into memory (.bin .dat .json .xml .utoc), archives and the game's
 * own .pak files, the folders trophy2 / uds / _DUPLEX_ / decrypted and the movie folders, Wwise sound banks and movie files by
 * their name. A profile of the person (TOML) is not touched by this: it packs what its rules say. */
static int
auto_loose(const char *rel) {
  char l[1100];
  lower_copy(rel, l, sizeof(l));
  const char *slash = strchr(l, '/');
  if(!slash) return 1;
  size_t first = (size_t)(slash - l);
  static const char *const dirs[] = { "trophy2", "uds", "_duplex_", "decrypted", "movie", "movies", "video", "videos", "cinematic", "cutscene", NULL };
  for(int i = 0; dirs[i]; i++)
    if(strlen(dirs[i]) == first && !memcmp(l, dirs[i], first)) return 1;
  const char *name = strrchr(l, '/') + 1;
  static const char *const prefix[] = { "movie", "soundbank", "wem", NULL };
  for(int i = 0; prefix[i]; i++)
    if(!strncmp(name, prefix[i], strlen(prefix[i]))) return 1;
  const char *dot = strrchr(name, '.');
  if(dot) {
    static const char *const ext[] = { ".bin", ".dat", ".json", ".xml", ".utoc", ".zip", ".7z", ".rar", ".crc", ".runtime", ".index", ".pak", NULL };
    for(int i = 0; ext[i]; i++)
      if(!strcmp(dot, ext[i])) return 1;
  }
  return 0;
}

/* ------------------------------------------------------------------------------------------------ built-in profile */

static void
cfg_defaults(lzc_cfg_t *c) {
  memset(c, 0, sizeof(*c));
  snprintf(c->index_name, sizeof(c->index_name), "ampr_assets.index");
  snprintf(c->pack_pattern, sizeof(c->pack_pattern), "ampr_assets-{id:03d}.pak");
  c->default_action = LZC_LOOSE;
  c->default_block_shift = 16;
  c->io_page_size = 65536;
  c->payload_alignment = 65536;
  c->chunk_alignment = 64;
  c->workers = 3;
  c->fast = 0;
  c->level = 12;
  c->accel = 1;
  c->min_sav_bytes = 64;
  c->min_sav_ratio = 0.01;
  c->ion_bytes = 8192;
  c->ion_ratio = 0.125;
  c->dedupe = 1;
  c->preserve_mtime = 1;
  c->validate_index = 1;
  c->auto_loose_large = 1;
  c->al_min_size = 64ull << 20;
  c->al_sample_blocks = 32;
  c->al_sample_bytes = 16ull << 20;
  c->al_min_ratio = 0.05;
  c->al_max_raw = 0.90;
}

void
lzc_automatic(lzc_cfg_t *c, int level, unsigned block_shift, unsigned lanes) {
  cfg_defaults(c);
  c->automatic = 1;
  if(level < 1) level = 1;
  if(level > 12) level = 12;
  if(block_shift < 14) block_shift = 14;
  if(block_shift > 20) block_shift = 20;
  if(lanes < 1) lanes = 1;
  if(lanes > 8) lanes = 8;
  /* 1-5 LZ4 fast (the higher, the better), 6-12 LZ4 HC at that level */
  if(level <= 5) {
    static const int accel[] = { 0, 16, 8, 4, 2, 1 };
    c->fast = 1;
    c->accel = accel[level];
    c->level = 1;
  } else {
    c->fast = 0;
    c->level = level;
  }
  c->default_block_shift = block_shift;
  c->groups = calloc(1, sizeof(lzc_group_t));
  c->ngroups = 1;
  if(!c->groups) { c->ngroups = 0; return; }
  snprintf(c->groups[0].name, sizeof(c->groups[0].name), "default");
  c->groups[0].pack_count = lanes;
  c->groups[0].assignment = LZC_ASSIGN_BALANCED;
  c->groups[0].max_pack_size = 8ull << 30;
  c->groups[0].stripe_threshold = 256ull << 20;
  c->groups[0].stripe_group_blocks = 8;
  c->rules = calloc(1, sizeof(lzc_rule_t));
  c->nrules = c->rules ? 1 : 0;
  if(!c->rules) return;
  lzc_rule_t *r = &c->rules[0];
  r->action = LZC_COMPRESS;
  r->block_shift = block_shift;
  r->group = 0;
  r->fast = c->fast;
  r->level = c->level;
  r->accel = c->accel;
  r->min_sav_bytes = c->min_sav_bytes;
  r->min_sav_ratio = c->min_sav_ratio;
  r->ion_bytes = c->ion_bytes;
  r->ion_ratio = c->ion_ratio;
  r->layout = LZC_LAY_MIXED;
}

void
lzc_free(lzc_cfg_t *c) {
  for(unsigned i = 0; i < c->nrules; i++) {
    free_strs(c->rules[i].inc, c->rules[i].ninc);
    free_strs(c->rules[i].exc, c->rules[i].nexc);
  }
  free(c->rules);
  free(c->groups);
  free_strs(c->required_packed, c->nreq);
  memset(c, 0, sizeof(*c));
}

void
lzc_default_rule(const lzc_cfg_t *c, lzc_rule_t *def) {
  memset(def, 0, sizeof(*def));
  def->action = c->default_action;
  def->block_shift = c->default_block_shift;
  def->fast = c->fast;
  def->level = c->level;
  def->accel = c->accel;
  def->min_sav_bytes = c->min_sav_bytes;
  def->min_sav_ratio = c->min_sav_ratio;
  def->ion_bytes = c->ion_bytes;
  def->ion_ratio = c->ion_ratio;
  def->layout = LZC_LAY_AUTO;
  if(c->automatic) def->action = LZC_LOOSE;
}

const lzc_rule_t *
lzc_select(const lzc_cfg_t *c, const char *rel_in, lzc_rule_t *def) {
  char rel[1100];
  norm_path(rel_in, rel, sizeof(rel));
  lzc_default_rule(c, def);
  if(c->automatic) {
    if(lzc_protected(rel) || media_ext(rel) || auto_loose(rel) || !c->nrules) return def;
    return &c->rules[0];
  }
  const lzc_rule_t *sel = def;
  for(unsigned i = 0; i < c->nrules; i++) {
    const lzc_rule_t *r = &c->rules[i];
    if(glob_any(r->inc, r->ninc, rel) && !glob_any(r->exc, r->nexc, rel)) sel = r;
  }
  return sel;
}

/* ------------------------------------------------------------------------------------------------ TOML subset */

typedef struct {
  enum { TV_NONE, TV_STR, TV_INT, TV_FLT, TV_BOOL, TV_ARR } t;
  char    *s;
  int64_t  i;
  double   f;
  int      b;
  char   **arr;
  unsigned narr;
  int      arr_numbers;           /* an array of numbers (not valid where strings are expected) */
} tv_t;

static void
tv_free(tv_t *v) {
  free(v->s);
  free_strs(v->arr, v->narr);
  memset(v, 0, sizeof(*v));
}

typedef struct {
  const char *p, *end;
  unsigned    line;
  char       *err;
  size_t      errn;
} ps_t;

static int
perr(ps_t *ps, const char *msg) {
  snprintf(ps->err, ps->errn, "Zeile %u: %s", ps->line, msg);
  return -1;
}

/* skips blanks; comments to the end of the line; with nl also line ends */
static void
skip_ws(ps_t *ps, int nl) {
  for(;;) {
    while(ps->p < ps->end && (*ps->p == ' ' || *ps->p == '\t' || *ps->p == '\r')) ps->p++;
    if(ps->p < ps->end && *ps->p == '#') while(ps->p < ps->end && *ps->p != '\n') ps->p++;
    if(nl && ps->p < ps->end && *ps->p == '\n') { ps->p++; ps->line++; continue; }
    break;
  }
}

static int
put_utf8(char *out, size_t *j, size_t cap, unsigned cp) {
  if(cp < 0x80) { if(*j + 1 >= cap) return -1; out[(*j)++] = (char)cp; }
  else if(cp < 0x800) { if(*j + 2 >= cap) return -1; out[(*j)++] = (char)(0xC0 | (cp >> 6)); out[(*j)++] = (char)(0x80 | (cp & 0x3F)); }
  else if(cp < 0x10000) {
    if(*j + 3 >= cap) return -1;
    out[(*j)++] = (char)(0xE0 | (cp >> 12)); out[(*j)++] = (char)(0x80 | ((cp >> 6) & 0x3F)); out[(*j)++] = (char)(0x80 | (cp & 0x3F));
  } else {
    if(*j + 4 >= cap) return -1;
    out[(*j)++] = (char)(0xF0 | (cp >> 18)); out[(*j)++] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[(*j)++] = (char)(0x80 | ((cp >> 6) & 0x3F)); out[(*j)++] = (char)(0x80 | (cp & 0x3F));
  }
  return 0;
}

/* a string at p ("..." or '...'): into a new buffer */
static int
parse_string(ps_t *ps, char **out) {
  char q = *ps->p++;
  size_t cap = (size_t)(ps->end - ps->p) + 8, j = 0;
  char *buf = malloc(cap);
  if(!buf) return perr(ps, "Kein Speicher.");
  for(;;) {
    if(ps->p >= ps->end || *ps->p == '\n') { free(buf); return perr(ps, "Eine Zeichenkette ist nicht geschlossen."); }
    char c = *ps->p++;
    if(c == q) break;
    if(q == '"' && c == '\\') {
      if(ps->p >= ps->end) { free(buf); return perr(ps, "Ungültige Maskierung."); }
      char e = *ps->p++;
      switch(e) {
        case 'n': c = '\n'; break;
        case 't': c = '\t'; break;
        case 'r': c = '\r'; break;
        case 'b': c = '\b'; break;
        case 'f': c = '\f'; break;
        case '"': c = '"'; break;
        case '\\': c = '\\'; break;
        case '/': c = '/'; break;
        case 'u': case 'U': {
          int n = e == 'u' ? 4 : 8;
          unsigned cp = 0;
          for(int k = 0; k < n; k++) {
            if(ps->p >= ps->end || !isxdigit((unsigned char)*ps->p)) { free(buf); return perr(ps, "Ungültige \\u-Maskierung."); }
            char h = *ps->p++;
            cp = cp * 16 + (unsigned)(isdigit((unsigned char)h) ? h - '0' : (tolower((unsigned char)h) - 'a' + 10));
          }
          if(put_utf8(buf, &j, cap, cp) != 0) { free(buf); return perr(ps, "Zeichenkette zu lang."); }
          continue;
        }
        default: free(buf); return perr(ps, "Ungültige Maskierung.");
      }
    }
    if(j + 1 >= cap) { free(buf); return perr(ps, "Zeichenkette zu lang."); }
    buf[j++] = c;
  }
  buf[j] = 0;
  *out = buf;
  return 0;
}

static int
parse_scalar_into(ps_t *ps, tv_t *v) {
  char c = *ps->p;
  if(c == '"' || c == '\'') {
    v->t = TV_STR;
    return parse_string(ps, &v->s);
  }
  if(!strncmp(ps->p, "true", 4) && !isalnum((unsigned char)ps->p[4])) { v->t = TV_BOOL; v->b = 1; ps->p += 4; return 0; }
  if(!strncmp(ps->p, "false", 5) && !isalnum((unsigned char)ps->p[5])) { v->t = TV_BOOL; v->b = 0; ps->p += 5; return 0; }
  char num[64];
  size_t j = 0;
  const char *q = ps->p;
  while(q < ps->end && (isdigit((unsigned char)*q) || *q == '+' || *q == '-' || *q == '.' || *q == '_' || *q == 'e' || *q == 'E') && j + 1 < sizeof(num)) {
    if(*q != '_') num[j++] = *q;
    q++;
  }
  num[j] = 0;
  if(!j) return perr(ps, "Ein Wert wird erwartet.");
  char *end = NULL;
  if(!strpbrk(num, ".eE")) {
    errno = 0;
    long long n = strtoll(num, &end, 10);
    if(*end || errno) return perr(ps, "Ungültige Zahl.");
    v->t = TV_INT;
    v->i = n;
    v->f = (double)n;
  } else {
    double d = strtod(num, &end);
    if(*end || !fin_d(d)) return perr(ps, "Ungültige Zahl.");
    v->t = TV_FLT;
    v->f = d;
  }
  ps->p = q;
  return 0;
}

static int
parse_value(ps_t *ps, tv_t *v) {
  memset(v, 0, sizeof(*v));
  if(ps->p >= ps->end) return perr(ps, "Ein Wert wird erwartet.");
  if(*ps->p == '{') return perr(ps, "Inline-Tabellen werden nicht unterstützt.");
  if(*ps->p != '[') return parse_scalar_into(ps, v);
  ps->p++;
  v->t = TV_ARR;
  for(;;) {
    skip_ws(ps, 1);
    if(ps->p >= ps->end) { tv_free(v); return perr(ps, "Eine Liste ist nicht geschlossen."); }
    if(*ps->p == ']') { ps->p++; break; }
    /* refused before going down: "[[[[…" in a profile from a USB drive would otherwise recurse until the stack runs out */
    if(*ps->p == '[' || *ps->p == '{') { tv_free(v); return perr(ps, "Verschachtelte Listen werden nicht unterstützt."); }
    tv_t e;
    if(parse_value(ps, &e) != 0) { tv_free(v); return -1; }
    if(e.t == TV_ARR) { tv_free(&e); tv_free(v); return perr(ps, "Verschachtelte Listen werden nicht unterstützt."); }
    if(e.t == TV_STR) {
      if(add_str(&v->arr, &v->narr, e.s, strlen(e.s)) != 0) { tv_free(&e); tv_free(v); return perr(ps, "Kein Speicher."); }
    } else {
      v->arr_numbers = 1;
    }
    tv_free(&e);
    skip_ws(ps, 1);
    if(ps->p < ps->end && *ps->p == ',') { ps->p++; continue; }
    skip_ws(ps, 1);
    if(ps->p < ps->end && *ps->p == ']') { ps->p++; break; }
    tv_free(v);
    return perr(ps, "In einer Liste fehlt ein Komma oder ].");
  }
  return 0;
}

/* a rule while parsing: which fields the file set, the rest comes from [pack] afterwards */
enum { H_MODE = 1, H_LEVEL = 2, H_ACCEL = 4, H_MSB = 8, H_MSR = 16, H_IOB = 32, H_IOR = 64, H_BLOCK = 128 };
typedef struct {
  lzc_rule_t r;
  unsigned   has;
  char       group[40];
  char     **incfrom; unsigned nincfrom;
  char     **excfrom; unsigned nexcfrom;
  int        has_include;
} prule_t;

enum { SEC_ROOT, SEC_PACK, SEC_GROUP, SEC_RULE, SEC_RUNTIME, SEC_OTHER };

typedef struct {
  lzc_cfg_t  *c;
  prule_t    *rules;  unsigned nrules;
  const char *dir;                  /* the folder of the TOML file; NULL for a built-in profile (no lists beside it) */
  char       *err; size_t errn;
  int         have_rt[4];
  int         have_payload;
} pstate_t;

static int
want_str(const tv_t *v, const char **s) {
  if(v->t != TV_STR) return -1;
  *s = v->s;
  return 0;
}

static int
want_size(const tv_t *v, uint64_t *out) {
  if(v->t == TV_INT && v->i >= 0) { *out = (uint64_t)v->i; return 0; }
  if(v->t == TV_STR) return lzc_parse_size(v->s, out);
  return -1;
}

static int
want_int(const tv_t *v, long long *out) {
  if(v->t == TV_INT) { *out = v->i; return 0; }
  return -1;
}

static int
want_float(const tv_t *v, double *out) {
  if(v->t == TV_INT || v->t == TV_FLT) { *out = v->f; return 0; }
  return -1;
}

static int
want_bool(const tv_t *v, int *out) {
  if(v->t == TV_BOOL) { *out = v->b; return 0; }
  return -1;
}

static int
want_action(const tv_t *v, int *out) {
  const char *s;
  if(want_str(v, &s) != 0) return -1;
  if(!strcasecmp(s, "compress")) *out = LZC_COMPRESS;
  else if(!strcasecmp(s, "store")) *out = LZC_STORE;
  else if(!strcasecmp(s, "loose")) *out = LZC_LOOSE;
  else return -1;
  return 0;
}

static int
want_block_shift(const tv_t *v, unsigned *out) {
  uint64_t n;
  if(want_size(v, &n) != 0 || n == 0 || (n & (n - 1))) return -1;
  unsigned sh = 0;
  while((1ull << sh) < n) sh++;
  if(sh < 14 || sh > 20) return -1;
  *out = sh;
  return 0;
}

static int
want_mode(const tv_t *v, int *fast) {
  const char *s;
  if(want_str(v, &s) != 0) return -1;
  if(!strcasecmp(s, "fast")) *fast = 1;
  else if(!strcasecmp(s, "hc")) *fast = 0;
  else return -1;
  return 0;
}

/* a list of strings or one string; NULL value = none */
static int
take_strings(const tv_t *v, char ***arr, unsigned *n) {
  if(v->t == TV_STR) return add_str(arr, n, v->s, strlen(v->s));
  if(v->t == TV_ARR && !v->arr_numbers) {
    for(unsigned i = 0; i < v->narr; i++)
      if(add_str(arr, n, v->arr[i], strlen(v->arr[i])) != 0) return -1;
    return 0;
  }
  return -2;
}

#define BAD(section, key) do { snprintf(st->err, st->errn, "%s: ungültiger Wert für %s.", section, key); return -1; } while(0)

static int
set_pack(pstate_t *st, const char *key, const tv_t *v) {
  lzc_cfg_t *c = st->c;
  const char *s;
  uint64_t u;
  long long n;
  double d;
  int b;
  if(!strcmp(key, "index_name")) { if(want_str(v, &s) || strlen(s) >= sizeof(c->index_name)) BAD("[pack]", key); snprintf(c->index_name, sizeof(c->index_name), "%s", s); }
  else if(!strcmp(key, "pack_pattern")) { if(want_str(v, &s) || strlen(s) >= sizeof(c->pack_pattern)) BAD("[pack]", key); snprintf(c->pack_pattern, sizeof(c->pack_pattern), "%s", s); }
  else if(!strcmp(key, "default_action")) { if(want_action(v, &b)) BAD("[pack]", key); c->default_action = b; }
  else if(!strcmp(key, "default_block_size")) { unsigned sh; if(want_block_shift(v, &sh)) BAD("[pack]", key); c->default_block_shift = sh; }
  else if(!strcmp(key, "io_page_size")) { if(want_size(v, &u)) BAD("[pack]", key); c->io_page_size = u; if(!st->have_payload) c->payload_alignment = u; }
  else if(!strcmp(key, "payload_alignment")) { if(want_size(v, &u)) BAD("[pack]", key); c->payload_alignment = u; st->have_payload = 1; }
  else if(!strcmp(key, "chunk_alignment")) { if(want_size(v, &u)) BAD("[pack]", key); c->chunk_alignment = u; }
  else if(!strcmp(key, "workers")) { if(want_int(v, &n) || n < 1 || n > 256) BAD("[pack]", key); c->workers = (unsigned)n; }
  else if(!strcmp(key, "compression_mode")) { if(want_mode(v, &b)) BAD("[pack]", key); c->fast = b; }
  else if(!strcmp(key, "compression_level")) { if(want_int(v, &n) || n < 1 || n > 12) BAD("[pack]", key); c->level = (int)n; }
  else if(!strcmp(key, "acceleration")) { if(want_int(v, &n) || n < 1 || n > 65537) BAD("[pack]", key); c->accel = (int)n; }
  else if(!strcmp(key, "min_savings_bytes")) { if(want_size(v, &u)) BAD("[pack]", key); c->min_sav_bytes = u; }
  else if(!strcmp(key, "min_savings_ratio")) { if(want_float(v, &d) || d < 0 || d >= 1) BAD("[pack]", key); c->min_sav_ratio = d; }
  else if(!strcmp(key, "io_neutral_min_savings_bytes")) { if(want_size(v, &u)) BAD("[pack]", key); c->ion_bytes = u; }
  else if(!strcmp(key, "io_neutral_min_savings_ratio")) { if(want_float(v, &d) || d < 0 || d >= 1) BAD("[pack]", key); c->ion_ratio = d; }
  else if(!strcmp(key, "deduplicate")) { if(want_bool(v, &b)) BAD("[pack]", key); c->dedupe = b; }
  else if(!strcmp(key, "deduplicate_scope")) {
    if(want_str(v, &s)) BAD("[pack]", key);
    if(!strcasecmp(s, "lane")) c->dedupe_group_scope = 0; else if(!strcasecmp(s, "group")) c->dedupe_group_scope = 1; else BAD("[pack]", key);
  }
  else if(!strcmp(key, "deduplicate_streaming")) { if(want_bool(v, &b)) BAD("[pack]", key); c->dedupe_streaming = b; }
  else if(!strcmp(key, "preserve_mtime")) { if(want_bool(v, &b)) BAD("[pack]", key); c->preserve_mtime = b; }
  else if(!strcmp(key, "validate_index_metadata")) { if(want_bool(v, &b)) BAD("[pack]", key); c->validate_index = b; }
  else if(!strcmp(key, "self_contained")) { if(want_bool(v, &b)) BAD("[pack]", key); c->self_contained = b; }
  else if(!strcmp(key, "required_packed")) { if(take_strings(v, &c->required_packed, &c->nreq) != 0) BAD("[pack]", key); }
  else if(!strcmp(key, "auto_loose_large_files")) { if(want_bool(v, &b)) BAD("[pack]", key); c->auto_loose_large = b; }
  else if(!strcmp(key, "auto_loose_hot_files")) { if(want_bool(v, &b)) BAD("[pack]", key); c->auto_loose_hot = b; }
  else if(!strcmp(key, "auto_loose_min_file_size")) { if(want_size(v, &u)) BAD("[pack]", key); c->al_min_size = u; }
  else if(!strcmp(key, "auto_loose_sample_blocks")) { if(want_int(v, &n) || n < 1 || n > 4096) BAD("[pack]", key); c->al_sample_blocks = (unsigned)n; }
  else if(!strcmp(key, "auto_loose_sample_bytes")) { if(want_size(v, &u) || u < 1) BAD("[pack]", key); c->al_sample_bytes = u; }
  else if(!strcmp(key, "auto_loose_min_savings_ratio")) { if(want_float(v, &d) || d < 0 || d >= 1) BAD("[pack]", key); c->al_min_ratio = d; }
  else if(!strcmp(key, "auto_loose_max_raw_ratio")) { if(want_float(v, &d) || d < 0 || d > 1) BAD("[pack]", key); c->al_max_raw = d; }
  return 0;                         /* unknown keys are ignored, as the Python reader does */
}

static int
set_group(pstate_t *st, lzc_group_t *g, const char *key, const tv_t *v) {
  uint64_t u;
  long long n;
  int b;
  const char *s;
  char sec[64];
  snprintf(sec, sizeof(sec), "[groups.%s]", g->name);
  if(!strcmp(key, "pack_count")) { if(want_int(v, &n) || n < 1 || n > 0xFFFF) BAD(sec, key); g->pack_count = (unsigned)n; }
  else if(!strcmp(key, "assignment")) {
    if(want_str(v, &s)) BAD(sec, key);
    if(!strcasecmp(s, "balanced")) g->assignment = LZC_ASSIGN_BALANCED;
    else if(!strcasecmp(s, "hash")) g->assignment = LZC_ASSIGN_HASH;
    else if(!strcasecmp(s, "round_robin")) g->assignment = LZC_ASSIGN_ROUND_ROBIN;
    else BAD(sec, key);
  }
  else if(!strcmp(key, "max_pack_size")) { if(want_size(v, &u)) BAD(sec, key); g->max_pack_size = u; }
  else if(!strcmp(key, "stripe_large_files")) { if(want_bool(v, &b)) BAD(sec, key); g->stripe_large = b; }
  else if(!strcmp(key, "stripe_threshold")) { if(want_size(v, &u)) BAD(sec, key); g->stripe_threshold = u; }
  else if(!strcmp(key, "stripe_group_blocks")) { if(want_int(v, &n) || n < 1 || n > 65536) BAD(sec, key); g->stripe_group_blocks = (unsigned)n; }
  else if(!strcmp(key, "io_page_size")) { if(want_size(v, &u)) BAD(sec, key); g->io_page_size = u; }
  return 0;
}

static int
set_rule(pstate_t *st, prule_t *p, unsigned idx, const char *key, const tv_t *v) {
  lzc_rule_t *r = &p->r;
  uint64_t u;
  long long n;
  double d;
  int b;
  const char *s;
  char sec[32];
  snprintf(sec, sizeof(sec), "Regel %u", idx);
  if(!strcmp(key, "action")) { if(want_action(v, &b)) BAD(sec, key); r->action = b; }
  else if(!strcmp(key, "include")) { p->has_include = 1; if(take_strings(v, &r->inc, &r->ninc) != 0) BAD(sec, key); }
  else if(!strcmp(key, "exclude")) { if(take_strings(v, &r->exc, &r->nexc) != 0) BAD(sec, key); }
  else if(!strcmp(key, "include_from")) { if(take_strings(v, &p->incfrom, &p->nincfrom) != 0) BAD(sec, key); }
  else if(!strcmp(key, "exclude_from")) { if(take_strings(v, &p->excfrom, &p->nexcfrom) != 0) BAD(sec, key); }
  else if(!strcmp(key, "block_size")) { unsigned sh; if(want_block_shift(v, &sh)) BAD(sec, key); r->block_shift = sh; p->has |= H_BLOCK; }
  else if(!strcmp(key, "group")) { if(want_str(v, &s) || strlen(s) >= sizeof(p->group)) BAD(sec, key); snprintf(p->group, sizeof(p->group), "%s", s); }
  else if(!strcmp(key, "compression_mode")) { if(want_mode(v, &b)) BAD(sec, key); r->fast = b; p->has |= H_MODE; }
  else if(!strcmp(key, "compression_level")) { if(want_int(v, &n) || n < 1 || n > 12) BAD(sec, key); r->level = (int)n; p->has |= H_LEVEL; }
  else if(!strcmp(key, "acceleration")) { if(want_int(v, &n) || n < 1 || n > 65537) BAD(sec, key); r->accel = (int)n; p->has |= H_ACCEL; }
  else if(!strcmp(key, "min_savings_bytes")) { if(want_size(v, &u)) BAD(sec, key); r->min_sav_bytes = u; p->has |= H_MSB; }
  else if(!strcmp(key, "min_savings_ratio")) { if(want_float(v, &d) || d < 0 || d >= 1) BAD(sec, key); r->min_sav_ratio = d; p->has |= H_MSR; }
  else if(!strcmp(key, "io_neutral_min_savings_bytes")) { if(want_size(v, &u)) BAD(sec, key); r->ion_bytes = u; p->has |= H_IOB; }
  else if(!strcmp(key, "io_neutral_min_savings_ratio")) { if(want_float(v, &d) || d < 0 || d >= 1) BAD(sec, key); r->ion_ratio = d; p->has |= H_IOR; }
  else if(!strcmp(key, "layout")) {
    if(want_str(v, &s)) BAD(sec, key);
    if(!strcasecmp(s, "auto")) r->layout = LZC_LAY_AUTO;
    else if(!strcasecmp(s, "random")) r->layout = LZC_LAY_RANDOM;
    else if(!strcasecmp(s, "mixed")) r->layout = LZC_LAY_MIXED;
    else if(!strcasecmp(s, "streaming")) r->layout = LZC_LAY_STREAMING;
    else BAD(sec, key);
  }
  else if(!strcmp(key, "streaming")) { if(want_bool(v, &b)) BAD(sec, key); r->streaming = b; }
  else if(!strcmp(key, "hot")) { if(want_bool(v, &b)) BAD(sec, key); r->hot = b; }
  else if(!strcmp(key, "force_pack")) { if(want_bool(v, &b)) BAD(sec, key); r->force_pack = b; }
  return 0;
}

static int
set_runtime(pstate_t *st, const char *key, const tv_t *v) {
  lzc_cfg_t *c = st->c;
  uint64_t u;
  long long n;
  if(!strcmp(key, "decoded_cache_bytes")) { if(want_size(v, &u)) BAD("[runtime]", key); c->rt_decoded = u; st->have_rt[0] = 1; }
  else if(!strcmp(key, "physical_cache_bytes")) { if(want_size(v, &u)) BAD("[runtime]", key); c->rt_physical = u; st->have_rt[1] = 1; }
  else if(!strcmp(key, "workers")) { if(want_int(v, &n)) BAD("[runtime]", key); c->rt_workers = (unsigned)n; st->have_rt[2] = 1; }
  else if(!strcmp(key, "latency_reserve_workers")) { if(want_int(v, &n)) BAD("[runtime]", key); c->rt_reserve = (unsigned)n; st->have_rt[3] = 1; }
  else { snprintf(st->err, st->errn, "[runtime]: unbekannter Schlüssel %s.", key); return -1; }
  return 0;
}

static int
read_whole(const char *path, char **out, size_t *len, size_t max) {
  FILE *f = fopen(path, "rb");
  if(!f) return errno ? errno : EIO;
  struct stat st;
  if(fstat(fileno(f), &st) != 0 || !S_ISREG(st.st_mode)) { fclose(f); return EINVAL; }
  if((uint64_t)st.st_size > max) { fclose(f); return EFBIG; }
  char *b = malloc((size_t)st.st_size + 1);
  if(!b) { fclose(f); return ENOMEM; }
  size_t got = fread(b, 1, (size_t)st.st_size, f);
  fclose(f);
  if(got != (size_t)st.st_size) { free(b); return EIO; }
  b[got] = 0;
  *out = b;
  *len = got;
  return 0;
}

/* the patterns of a list file beside the TOML: a line each, blank lines and # lines skipped */
static int
load_pattern_files(pstate_t *st, char **names, unsigned n, const char *field, char ***arr, unsigned *cnt) {
  for(unsigned i = 0; i < n; i++) {
    const char *nm = names[i];
    if(!st->dir) { snprintf(st->err, st->errn, "%s: Ein mitgeliefertes Profil liest keine Listen nach.", field); return -1; }
    if(nm[0] == '/' || nm[0] == '\\' || strstr(nm, "..") || strchr(nm, ':')) {
      snprintf(st->err, st->errn, "%s: \"%s\" muss ein Name neben der TOML-Datei sein.", field, nm);
      return -1;
    }
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/%s", st->dir, nm);
    char *buf = NULL;
    size_t len = 0;
    int rc = read_whole(path, &buf, &len, 64u << 20);
    if(rc) { snprintf(st->err, st->errn, "%s: \"%s\" ließ sich nicht lesen.", field, nm); return -1; }
    for(char *line = buf; line && *line;) {
      char *nl = strchr(line, '\n');
      if(nl) *nl = 0;
      char *a = line;
      while(*a == ' ' || *a == '\t' || *a == '\r') a++;
      size_t l = strlen(a);
      while(l && (a[l - 1] == ' ' || a[l - 1] == '\t' || a[l - 1] == '\r')) a[--l] = 0;
      if(l && *a != '#' && add_str(arr, cnt, a, l) != 0) { free(buf); snprintf(st->err, st->errn, "Kein Speicher."); return -1; }
      line = nl ? nl + 1 : NULL;
    }
    free(buf);
  }
  return 0;
}

static int
read_key(ps_t *ps, char *key, size_t n) {
  size_t j = 0;
  if(*ps->p == '"' || *ps->p == '\'') {
    char *s = NULL;
    if(parse_string(ps, &s) != 0) return -1;
    snprintf(key, n, "%s", s);
    free(s);
    return 0;
  }
  while(ps->p < ps->end && (isalnum((unsigned char)*ps->p) || *ps->p == '_' || *ps->p == '-') && j + 1 < n) key[j++] = *ps->p++;
  key[j] = 0;
  return j ? 0 : perr(ps, "Ein Schlüssel wird erwartet.");
}

/* the parser; buf (len bytes, NUL after them) is freed here. dir: where include_from / exclude_from lists are, NULL for none */
static int
load_buf(char *buf, size_t len, const char *dir, lzc_cfg_t *c, char *err, size_t errn) {
  pstate_t st = { .c = c, .err = err, .errn = errn };
  st.dir = dir;
  ps_t ps = { buf, buf + len, 1, err, errn };
  int section = SEC_ROOT;
  lzc_group_t *group = NULL;
  prule_t *rule = NULL;
  unsigned rule_idx = 0;
  struct { char name[40]; lzc_group_t g; } *glist = NULL;
  unsigned ng = 0;
  int ok = 0;
  for(;;) {
    skip_ws(&ps, 1);
    if(ps.p >= ps.end) { ok = 1; break; }
    if(*ps.p == '[') {
      int arr = ps.p + 1 < ps.end && ps.p[1] == '[';
      ps.p += arr ? 2 : 1;
      char name[128];
      size_t j = 0;
      while(ps.p < ps.end && *ps.p != ']' && *ps.p != '\n' && j + 1 < sizeof(name)) {
        if(*ps.p == '"' && !arr) {                                  /* a quoted part of a dotted name */
          char *s = NULL;
          if(parse_string(&ps, &s) != 0) goto done;
          for(char *q = s; *q && j + 1 < sizeof(name); q++) name[j++] = *q;
          free(s);
          continue;
        }
        if(*ps.p != ' ' && *ps.p != '\t') name[j++] = *ps.p;
        ps.p++;
      }
      name[j] = 0;
      if(ps.p >= ps.end || *ps.p != ']') { perr(&ps, "Ein Tabellenname ist nicht geschlossen."); goto done; }
      ps.p++;
      if(arr) {
        if(ps.p >= ps.end || *ps.p != ']') { perr(&ps, "Ein Tabellenname ist nicht geschlossen."); goto done; }
        ps.p++;
      }
      if(arr && !strcmp(name, "rule")) {
        prule_t *nr = realloc(st.rules, (st.nrules + 1) * sizeof(prule_t));
        if(!nr) { perr(&ps, "Kein Speicher."); goto done; }
        st.rules = nr;
        rule = &st.rules[st.nrules];
        memset(rule, 0, sizeof(*rule));
        snprintf(rule->group, sizeof(rule->group), "default");
        rule->r.action = LZC_COMPRESS;
        rule_idx = st.nrules++;
        section = SEC_RULE;
      } else if(!arr && !strcmp(name, "pack")) section = SEC_PACK;
      else if(!arr && !strcmp(name, "runtime")) section = SEC_RUNTIME;
      else if(!arr && !strncmp(name, "groups.", 7) && name[7]) {
        void *ngl = realloc(glist, (ng + 1) * sizeof(*glist));
        if(!ngl) { perr(&ps, "Kein Speicher."); goto done; }
        glist = ngl;
        memset(&glist[ng], 0, sizeof(glist[ng]));
        snprintf(glist[ng].name, sizeof(glist[ng].name), "%s", name + 7);
        snprintf(glist[ng].g.name, sizeof(glist[ng].g.name), "%s", name + 7);
        glist[ng].g.pack_count = 1;
        glist[ng].g.stripe_threshold = 256ull << 20;
        glist[ng].g.stripe_group_blocks = 8;
        group = &glist[ng].g;
        ng++;
        section = SEC_GROUP;
      } else section = SEC_OTHER;
      skip_ws(&ps, 0);
      if(ps.p < ps.end && *ps.p != '\n') { perr(&ps, "Nach einem Tabellennamen steht noch etwas."); goto done; }
      continue;
    }
    char key[64];
    if(read_key(&ps, key, sizeof(key)) != 0) goto done;
    skip_ws(&ps, 0);
    if(ps.p >= ps.end || *ps.p != '=') { perr(&ps, "Nach dem Schlüssel fehlt ein =."); goto done; }
    ps.p++;
    skip_ws(&ps, 0);
    tv_t v;
    if(parse_value(&ps, &v) != 0) goto done;
    skip_ws(&ps, 0);
    if(ps.p < ps.end && *ps.p != '\n') { tv_free(&v); perr(&ps, "Nach dem Wert steht noch etwas."); goto done; }
    int r2 = 0;
    switch(section) {
      case SEC_PACK: r2 = set_pack(&st, key, &v); break;
      case SEC_GROUP: r2 = set_group(&st, group, key, &v); break;
      case SEC_RULE: r2 = set_rule(&st, rule, rule_idx, key, &v); break;
      case SEC_RUNTIME: r2 = set_runtime(&st, key, &v); break;
      default: break;
    }
    tv_free(&v);
    if(r2 != 0) goto done;
  }

  /* ---- checks of [pack], as load_config does them */
  {
    ok = 0;
    if(c->io_page_size < 4096 || (c->io_page_size & (c->io_page_size - 1)) || c->io_page_size > (1u << 20)) { snprintf(err, errn, "io_page_size muss eine Zweierpotenz von 4 KiB bis 1 MiB sein."); goto done; }
    if(c->payload_alignment < DATA_HEADER_SIZE || (c->payload_alignment & (c->payload_alignment - 1)) || c->payload_alignment > (1u << 20)) { snprintf(err, errn, "payload_alignment muss eine Zweierpotenz von 64 B bis 1 MiB sein."); goto done; }
    if(c->chunk_alignment < 64 || (c->chunk_alignment & (c->chunk_alignment - 1)) || c->chunk_alignment > c->io_page_size || c->io_page_size % c->chunk_alignment) { snprintf(err, errn, "chunk_alignment muss eine Zweierpotenz von 64 B bis io_page_size sein."); goto done; }
    if(c->index_name[0] == 0 || c->index_name[0] == '/' || c->index_name[0] == '\\' || strstr(c->index_name, "..")) { snprintf(err, errn, "index_name ist unsicher."); goto done; }
    if(!strstr(c->pack_pattern, "{id") || c->pack_pattern[0] == '/' || strstr(c->pack_pattern, "..")) { snprintf(err, errn, "pack_pattern muss {id} enthalten und darf nicht aus dem Ordner führen."); goto done; }
    if(st.have_rt[0] || st.have_rt[1] || st.have_rt[2] || st.have_rt[3]) {
      if(!(st.have_rt[0] && st.have_rt[1] && st.have_rt[2] && st.have_rt[3])) { snprintf(err, errn, "[runtime] braucht genau decoded_cache_bytes, physical_cache_bytes, workers und latency_reserve_workers."); goto done; }
      if(c->rt_workers < 1 || c->rt_workers > 16 || c->rt_reserve >= c->rt_workers || c->rt_decoded % 16384 || c->rt_physical % 16384) { snprintf(err, errn, "[runtime]: Arbeiter 1 bis 16, Reserve kleiner als die Arbeiter, Zwischenspeicher in Vielfachen von 16 KiB."); goto done; }
      c->has_runtime = 1;
    }
  }
  /* ---- groups */
  {
    int has_default = 0;
    for(unsigned i = 0; i < ng; i++) if(!strcmp(glist[i].name, "default")) has_default = 1;
    c->ngroups = ng + (has_default ? 0 : 1);
    if(c->ngroups > LZC_MAX_GROUPS) { snprintf(err, errn, "Das Format kennt höchstens 256 Gruppen."); goto done; }
    c->groups = calloc(c->ngroups, sizeof(lzc_group_t));
    if(!c->groups) { snprintf(err, errn, "Kein Speicher."); goto done; }
    unsigned k = 0;
    for(unsigned i = 0; i < ng; i++) c->groups[k++] = glist[i].g;
    if(!has_default) {
      snprintf(c->groups[k].name, sizeof(c->groups[k].name), "default");
      c->groups[k].pack_count = 1;
      c->groups[k].stripe_threshold = 256ull << 20;
      c->groups[k].stripe_group_blocks = 8;
    }
    for(unsigned i = 0; i < c->ngroups; i++) {
      lzc_group_t *g = &c->groups[i];
      uint64_t page = g->io_page_size ? g->io_page_size : c->io_page_size;
      if(page < 4096 || page > (1u << 20) || (page & (page - 1)) || page % c->chunk_alignment) { snprintf(err, errn, "groups.%s.io_page_size ist ungültig.", g->name); goto done; }
      uint64_t palign = c->payload_alignment > page ? c->payload_alignment : page;
      uint64_t minoff = (DATA_HEADER_SIZE + palign - 1) / palign * palign;
      if(g->max_pack_size && g->max_pack_size < minoff + page) { snprintf(err, errn, "groups.%s.max_pack_size ist zu klein.", g->name); goto done; }
    }
  }
  /* ---- rules: what the file did not set comes from [pack] */
  {
    c->rules = calloc(st.nrules ? st.nrules : 1, sizeof(lzc_rule_t));
    if(!c->rules) { snprintf(err, errn, "Kein Speicher."); goto done; }
    c->nrules = st.nrules;
    for(unsigned i = 0; i < st.nrules; i++) {
      prule_t *p = &st.rules[i];
      lzc_rule_t *r = &c->rules[i];
      *r = p->r;
      p->r.inc = NULL; p->r.ninc = 0; p->r.exc = NULL; p->r.nexc = 0;     /* moved */
      unsigned gi = 0;
      int found = 0;
      for(unsigned k = 0; k < c->ngroups; k++) if(!strcmp(c->groups[k].name, p->group)) { gi = k; found = 1; }
      if(!found) { snprintf(err, errn, "Regel %u nennt die unbekannte Gruppe \"%s\".", i, p->group); goto done; }
      r->group = gi;
      if(!(p->has & H_BLOCK)) r->block_shift = c->default_block_shift;
      if(!(p->has & H_MODE)) r->fast = c->fast;
      if(!(p->has & H_LEVEL)) r->level = c->level;
      if(!(p->has & H_ACCEL)) r->accel = c->accel;
      if(!(p->has & H_MSB)) r->min_sav_bytes = c->min_sav_bytes;
      if(!(p->has & H_MSR)) r->min_sav_ratio = c->min_sav_ratio;
      if(!(p->has & H_IOB)) r->ion_bytes = c->ion_bytes;
      if(!(p->has & H_IOR)) r->ion_ratio = c->ion_ratio;
      if(load_pattern_files(&st, p->incfrom, p->nincfrom, "include_from", &r->inc, &r->ninc) != 0) goto done;
      if(load_pattern_files(&st, p->excfrom, p->nexcfrom, "exclude_from", &r->exc, &r->nexc) != 0) goto done;
      if(!p->has_include && !p->nincfrom && add_str(&r->inc, &r->ninc, "**", 2) != 0) { snprintf(err, errn, "Kein Speicher."); goto done; }
      if(!r->ninc) { snprintf(err, errn, "Regel %u: die Liste include darf nicht leer sein.", i); goto done; }
    }
  }
  ok = 1;
done:
  for(unsigned i = 0; i < st.nrules; i++) {
    free_strs(st.rules[i].r.inc, st.rules[i].r.ninc);
    free_strs(st.rules[i].r.exc, st.rules[i].r.nexc);
    free_strs(st.rules[i].incfrom, st.rules[i].nincfrom);
    free_strs(st.rules[i].excfrom, st.rules[i].nexcfrom);
  }
  free(st.rules);
  free(glist);
  free(buf);
  if(!ok) {
    lzc_free(c);
    if(!err[0]) snprintf(err, errn, "Die Profildatei ist ungültig.");
    return -1;
  }
  return 0;
}

int
lzc_load(const char *path, lzc_cfg_t *c, char *err, size_t errn) {
  cfg_defaults(c);
  char *buf = NULL;
  size_t len = 0;
  int rc = read_whole(path, &buf, &len, 8u << 20);
  if(rc) { snprintf(err, errn, "Die Profildatei ließ sich nicht lesen: %s", strerror(rc)); return -1; }
  char dir[PATH_MAX];
  snprintf(dir, sizeof(dir), "%s", path);
  char *sl = strrchr(dir, '/');
  if(sl) *sl = 0; else strcpy(dir, ".");
  return load_buf(buf, len, dir, c, err, errn);
}

int
lzc_load_text(const char *text, size_t n, lzc_cfg_t *c, char *err, size_t errn) {
  cfg_defaults(c);
  char *buf = malloc(n + 1);
  if(!buf) { snprintf(err, errn, "Kein Speicher."); return -1; }
  memcpy(buf, text, n);
  buf[n] = 0;
  return load_buf(buf, n, NULL, c, err, errn);
}
