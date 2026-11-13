/* The name of a converted image from a template - see cvname.h. Pure functions, tested on the host (test/cvname). */
#include <stdio.h>
#include <string.h>

#include "cvname.h"

static int
field_value(const char *key, size_t klen, const ps5tm_cvname_fields_t *f, const char **v) {
  struct { const char *k; const char *val; } t[] = {
    { "titel", f->titel }, { "id", f->id }, { "version", f->version }, { "quelle", f->quelle },
  };
  for(size_t i = 0; i < sizeof(t) / sizeof(t[0]); i++) {
    if(strlen(t[i].k) == klen && !strncmp(t[i].k, key, klen)) { *v = t[i].val ? t[i].val : ""; return 0; }
  }
  return -1;
}

/* Appends bytes of s to out, replacing what a file name cannot hold. A UTF-8 sequence stays whole. */
static int
put(char *out, size_t out_len, size_t *at, const char *s, size_t n) {
  for(size_t i = 0; i < n; i++) {
    unsigned char c = (unsigned char)s[i];
    char o = (char)c;
    if(c < 0x20 || c == 0x7f || c == '/' || c == 0x5c || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') o = '_';
    if(*at + 1 >= out_len) return -1;
    out[(*at)++] = o;
  }
  return 0;
}

/* Makes the bytes of s valid UTF-8 and takes out what only fakes or hides a name: a byte that is not part of a valid sequence, the C1
   controls, zero-width and direction marks (U+200B-200F, U+202A-202E, U+2060-2069), the line and paragraph separators and U+FEFF each
   become one "_". Works in place (the result is never longer); returns the new length. */
static size_t
clean_utf8(char *s, size_t n) {
  size_t r = 0, w = 0;
  while(r < n) {
    unsigned char c = (unsigned char)s[r];
    size_t len = 0;
    unsigned cp = 0;
    if(c < 0x80) { s[w++] = (char)c; r++; continue; }
    if(c >= 0xC2 && c <= 0xDF) { len = 2; cp = c & 0x1f; }
    else if(c >= 0xE0 && c <= 0xEF) { len = 3; cp = c & 0x0f; }
    else if(c >= 0xF0 && c <= 0xF4) { len = 4; cp = c & 0x07; }
    int good = len && r + len <= n;
    for(size_t k = 1; good && k < len; k++) {
      unsigned char d = (unsigned char)s[r + k];
      if((d & 0xC0) != 0x80) good = 0; else cp = (cp << 6) | (d & 0x3f);
    }
    if(good && ((len == 3 && cp < 0x800) || (len == 4 && (cp < 0x10000 || cp > 0x10FFFF)) || (cp >= 0xD800 && cp <= 0xDFFF))) good = 0;
    int bad = !good || (cp >= 0x80 && cp <= 0x9F) || (cp >= 0x200B && cp <= 0x200F) || (cp >= 0x2028 && cp <= 0x202E) ||
              (cp >= 0x2060 && cp <= 0x2069) || cp == 0xFEFF;
    if(bad) { s[w++] = '_'; r += good ? len : 1; continue; }
    memmove(s + w, s + r, len);
    w += len;
    r += len;
  }
  s[w] = 0;
  return w;
}

int
ps5tm_cvname_expand(const char *tpl, const ps5tm_cvname_fields_t *f, size_t ext_len, char *out, size_t out_len, size_t *bytes,
                    char *err, size_t err_len) {
  if(!out || out_len < 2) return -1;
  size_t at = 0;
  const char *t = (tpl && tpl[0]) ? tpl : "{quelle}";
  for(const char *p = t; *p;) {
    if(*p == '{') {
      const char *e = strchr(p, '}');
      if(!e) { snprintf(err, err_len, "In der Vorlage fehlt ein „}“."); return -1; }
      const char *v;
      if(field_value(p + 1, (size_t)(e - p - 1), f, &v) != 0) {
        snprintf(err, err_len, "Die Vorlage kennt „%.*s“ nicht. Es gibt {titel}, {id}, {version} und {quelle}.", (int)(e - p + 1), p);
        return -1;
      }
      if(put(out, out_len, &at, v, strlen(v)) != 0) { snprintf(err, err_len, "Der Name ist zu lang."); return -1; }
      p = e + 1;
    } else {
      if(put(out, out_len, &at, p, 1) != 0) { snprintf(err, err_len, "Der Name ist zu lang."); return -1; }
      p++;
    }
  }
  out[at] = 0;
  at = clean_utf8(out, at);
  /* spaces at both ends and dots at the end do not survive on every drive */
  while(at > 0 && (out[at - 1] == ' ' || out[at - 1] == '.')) out[--at] = 0;
  size_t lead = 0;
  while(out[lead] == ' ') lead++;
  if(lead) { memmove(out, out + lead, at - lead + 1); at -= lead; }
  if(at == 0 || !strcmp(out, ".") || !strcmp(out, "..")) { snprintf(err, err_len, "Der Name ist leer."); return -1; }
  size_t total = at + ext_len + PS5TM_CVNAME_SUFFIX;
  if(bytes) *bytes = at + ext_len;
  if(total > PS5TM_CVNAME_MAX) {
    snprintf(err, err_len, "Der Name ist zu lang: %zu Bytes, erlaubt sind %d (mit Endung und Prüfsummen-Datei).", at + ext_len, PS5TM_CVNAME_MAX - PS5TM_CVNAME_SUFFIX);
    return -1;
  }
  return 0;
}
