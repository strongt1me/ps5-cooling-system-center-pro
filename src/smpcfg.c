/* ShadowMountPlus's config.ini: reading and changing update_emulators (see smpcfg.h). */
#include "smpcfg.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#define CFG_MAX (256u * 1024)

typedef struct {
  const char *s;        /* the whole line without its line ending */
  size_t      len;
  size_t      next;     /* offset of the following line */
} line_t;

/* One line at off (0 at the end of the text). */
static int
next_line(const char *t, size_t n, size_t off, line_t *l) {
  if(off >= n) return 0;
  size_t i = off;
  while(i < n && t[i] != '\n') i++;
  l->s = t + off;
  l->len = i - off;
  l->next = i < n ? i + 1 : n;
  if(l->len && l->s[l->len - 1] == '\r') l->len--;
  return 1;
}

/* The line without blanks around it. */
static void
trim(const char **s, size_t *len) {
  while(*len && (**s == ' ' || **s == '\t')) { (*s)++; (*len)--; }
  while(*len && ((*s)[*len - 1] == ' ' || (*s)[*len - 1] == '\t')) (*len)--;
}

typedef enum { L_OTHER, L_SECTION, L_KEY } kind_t;

/* What a line is: a section header (name in key), a key = value line (key and value, trimmed), or something else
 * (blank, comment). */
static kind_t
classify(const line_t *l, char *key, size_t klen, char *val, size_t vlen) {
  const char *s = l->s;
  size_t n = l->len;
  trim(&s, &n);
  key[0] = val[0] = 0;
  if(!n || s[0] == '#' || s[0] == ';') return L_OTHER;
  if(s[0] == '[') {
    if(s[n - 1] != ']') return L_OTHER;
    size_t m = n - 2 < klen - 1 ? n - 2 : klen - 1;
    memcpy(key, s + 1, m);
    key[m] = 0;
    for(char *q = key; *q; q++) if(*q >= 'A' && *q <= 'Z') *q += 32;
    return L_SECTION;
  }
  const char *eq = memchr(s, '=', n);
  if(!eq) return L_OTHER;
  const char *k = s;
  size_t kn = (size_t)(eq - s);
  trim(&k, &kn);
  const char *v = eq + 1;
  size_t vn = n - (size_t)(eq - s) - 1;
  trim(&v, &vn);
  if(!kn) return L_OTHER;
  size_t a = kn < klen - 1 ? kn : klen - 1;
  size_t b = vn < vlen - 1 ? vn : vlen - 1;
  memcpy(key, k, a);
  key[a] = 0;
  memcpy(val, v, b);
  val[b] = 0;
  return L_KEY;
}

static int
is_off(const char *v) {
  return !strcasecmp(v, "0") || !strcasecmp(v, "false") || !strcasecmp(v, "no") || !strcasecmp(v, "off");
}

void
ps5tm_smpcfg_parse(const char *text, size_t len, ps5tm_smpcfg_t *c) {
  memset(c, 0, sizeof(*c));
  c->update_emulators = 1;
  char sec[96] = "", key[96], val[256];
  line_t l;
  for(size_t off = 0; next_line(text, len, off, &l); off = l.next) {
    kind_t k = classify(&l, key, sizeof(key), val, sizeof(val));
    if(k == L_SECTION) {
      snprintf(sec, sizeof(sec), "%s", key);
      if(!strcmp(sec, "config") || !strcmp(sec, "emulators")) c->v2 = 1;
    } else if(k == L_KEY) {
      int emu_sec = !strcmp(sec, "emulators");
      if(!strcasecmp(key, "update_emulators") || (emu_sec && !strcasecmp(key, "update_enabled")))
        c->update_emulators = !is_off(val);
      else if((!strcasecmp(key, "emulators_path") || (emu_sec && !strcasecmp(key, "path"))) && val[0] == '/' &&
              strlen(val) < sizeof(c->emulators_path))
        memcpy(c->emulators_path, val, strlen(val) + 1);      /* the length was checked above */
    }
  }
}

/* Appends to a growing buffer. */
typedef struct { char *p; size_t n, cap; int bad; } out_t;

static void
put(out_t *o, const char *s, size_t n) {
  if(o->bad) return;
  if(o->n + n + 1 > o->cap) {
    size_t cap = o->cap ? o->cap : 4096;
    while(cap < o->n + n + 1) cap *= 2;
    char *np = realloc(o->p, cap);
    if(!np) { o->bad = 1; return; }
    o->p = np;
    o->cap = cap;
  }
  memcpy(o->p + o->n, s, n);
  o->n += n;
  o->p[o->n] = 0;
}

static void
puts_(out_t *o, const char *s) { put(o, s, strlen(s)); }

char *
ps5tm_smpcfg_with_update(const char *text, size_t len, int on, size_t *out_len) {
  ps5tm_smpcfg_t c;
  ps5tm_smpcfg_parse(text, len, &c);
  const char *eol = memchr(text, '\r', len) ? "\r\n" : "\n";
  const char *val = on ? "1" : "0";
  out_t o = { 0 };
  put(&o, "", 0);
  char sec[96] = "", key[96], v[256];
  int found = 0, header_end = -1;                 /* header_end: offset after the [emulators] header line */
  line_t l;
  for(size_t off = 0; next_line(text, len, off, &l); off = l.next) {
    kind_t k = classify(&l, key, sizeof(key), v, sizeof(v));
    int hit = 0;
    if(k == L_SECTION) {
      snprintf(sec, sizeof(sec), "%s", key);
      if(!strcmp(sec, "emulators")) header_end = (int)l.next;
    } else if(k == L_KEY) {
      hit = !strcasecmp(key, "update_emulators") || (!strcmp(sec, "emulators") && !strcasecmp(key, "update_enabled"));
    }
    if(hit) {
      /* the same line with only the value changed: indentation, key spelling and the line ending stay */
      const char *eq = memchr(l.s, '=', l.len);
      size_t head = (size_t)(eq - l.s) + 1;
      put(&o, l.s, head);
      puts_(&o, val);
      put(&o, text + off + l.len, l.next - off - l.len);          /* the line ending (and nothing else: blanks after the value go) */
      found = 1;
    } else {
      put(&o, text + off, l.next - off);
    }
  }
  if(!found && !on) {
    /* not there: add it where the format keeps it (a flat file at its end, a v2 file inside [emulators]) */
    if(c.v2) {
      if(header_end >= 0) {
        char ins[64];
        snprintf(ins, sizeof(ins), "update_enabled=0%s", eol);
        out_t o2 = { 0 };
        put(&o2, "", 0);
        put(&o2, o.p, (size_t)header_end);
        puts_(&o2, ins);
        put(&o2, o.p + header_end, o.n - (size_t)header_end);
        free(o.p);
        o = o2;
      } else {
        if(o.n && o.p[o.n - 1] != '\n') puts_(&o, eol);
        puts_(&o, eol);
        puts_(&o, "[emulators]");
        puts_(&o, eol);
        puts_(&o, "update_enabled=0");
        puts_(&o, eol);
      }
    } else {
      if(o.n && o.p[o.n - 1] != '\n') puts_(&o, eol);
      puts_(&o, "update_emulators=0");
      puts_(&o, eol);
    }
  }
  if(o.bad) { free(o.p); return NULL; }
  if(out_len) *out_len = o.n;
  return o.p;
}

static void
say(char *why, size_t n, const char *fmt, const char *a) {
  if(why && n) snprintf(why, n, fmt, a ? a : "");
}

int
ps5tm_smpcfg_set_update(const char *path, int on, char *why, size_t why_len) {
  if(why && why_len) why[0] = 0;
  char *old = NULL;
  size_t olen = 0;
  struct stat st;
  int have = stat(path, &st) == 0 && S_ISREG(st.st_mode);
  if(have) {
    if(st.st_size < 0 || (size_t)st.st_size > CFG_MAX) { say(why, why_len, "%s ist zu groß, um sie zu ändern.", path); return EFBIG; }
    FILE *f = fopen(path, "rb");
    if(!f) { say(why, why_len, "%s ließ sich nicht lesen.", path); return errno ? errno : EIO; }
    old = malloc((size_t)st.st_size + 1);
    if(!old) { fclose(f); say(why, why_len, "Kein Speicher.", NULL); return ENOMEM; }
    olen = fread(old, 1, (size_t)st.st_size, f);
    fclose(f);
    old[olen] = 0;
  } else {
    /* a missing file is made only where ShadowMountPlus lives (its folder is there) */
    char dir[300];
    snprintf(dir, sizeof(dir), "%s", path);
    char *slash = strrchr(dir, '/');
    if(slash) *slash = 0;
    struct stat ds;
    if(!slash || stat(dir, &ds) != 0 || !S_ISDIR(ds.st_mode)) {
      say(why, why_len, "ShadowMountPlus ist nicht eingerichtet: Der Ordner für %s fehlt.", path);
      return ENOENT;
    }
    old = malloc(1);
    if(!old) { say(why, why_len, "Kein Speicher.", NULL); return ENOMEM; }
    old[0] = 0;
  }
  size_t nlen = 0;
  char *neu = ps5tm_smpcfg_with_update(old, olen, on, &nlen);
  if(!neu) { free(old); say(why, why_len, "Kein Speicher.", NULL); return ENOMEM; }
  int rc = 0;
  if(have && nlen == olen && !memcmp(neu, old, olen)) goto done;        /* already as wanted: nothing to write */
  /* the old file stays once as a copy (it holds the person's whole setup, SMB connections included) */
  if(have) {
    char bak[320];
    snprintf(bak, sizeof(bak), "%s.ps5cc-bak", path);
    struct stat bs;
    if(stat(bak, &bs) != 0) {
      FILE *b = fopen(bak, "wb");
      if(b) {
        fchmod(fileno(b), st.st_mode & 0777);
        fwrite(old, 1, olen, b);
        fclose(b);
      }
    }
  }
  char tmp[320];
  snprintf(tmp, sizeof(tmp), "%s.ps5cc-tmp", path);
  FILE *f = fopen(tmp, "wb");
  if(!f) { rc = errno ? errno : EIO; say(why, why_len, "%s ließ sich nicht schreiben.", tmp); goto done; }
  if(have) fchmod(fileno(f), st.st_mode & 0777);
  if(fwrite(neu, 1, nlen, f) != nlen || fflush(f) != 0) {
    rc = errno ? errno : EIO;
    fclose(f);
    unlink(tmp);
    say(why, why_len, "%s ließ sich nicht schreiben.", tmp);
    goto done;
  }
  fsync(fileno(f));
  fclose(f);
  if(rename(tmp, path) != 0) {
    rc = errno ? errno : EIO;
    unlink(tmp);
    say(why, why_len, "%s ließ sich nicht ersetzen.", path);
    goto done;
  }
  /* look again: what is in the file now says what was asked */
  {
    ps5tm_smpcfg_t c;
    ps5tm_smpcfg_parse(neu, nlen, &c);
    if(c.update_emulators != (on ? 1 : 0)) { rc = EIO; say(why, why_len, "Die Einstellung in %s hat sich nicht ändern lassen.", path); }
  }
done:
  free(old);
  free(neu);
  return rc;
}
