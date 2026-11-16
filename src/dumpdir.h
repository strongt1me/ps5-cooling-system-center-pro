/* PS5 Cooling & System Center - Pro: the folder name of the dump settings (dumpdir.h, 16.11.2026).
 *
 * The folder is typed by a person ("Bis zu 3 Ordner tief") and joined to the drive: <drive>/<folder>/<game>. Until 16.11.2026 only ASCII letters, digits,
 * '.', '_', '-' and ' ' were accepted, and anything else - an umlaut ("Spiele für PS5"), a bracket, a slash at the end ("Dumps/"), a whole path
 * ("/mnt/usb0/Dumps") - was refused: the page showed a message for a moment and put the old folder back, so the dump went to the old folder and the
 * person saw only that "the folder is not used".
 *
 * Now the name is first tidied (spaces round it, backslashes, double and outer slashes, the drive's own path in front of it) and then checked against
 * what the file systems of the drives really forbid: the characters \ : * ? " < > | and control characters, a segment of dots only, a segment ending in
 * a dot or a space (exFAT and Windows), at most three segments and 95 bytes. UTF-8 letters are fine. */
#ifndef PS5TM_DUMPDIR_H
#define PS5TM_DUMPDIR_H

#include <string.h>

/* Tidies s in place; mount is the chosen drive ("/mnt/usb0", "/user", or "" when none). Returns s. */
static inline char *
ps5tm_dumpdir_tidy(char *s, const char *mount) {
  if(!s) return s;
  for(char *p = s; *p; p++)
    if(*p == '\\') *p = '/';
  size_t lead = strspn(s, " ");
  if(lead) memmove(s, s + lead, strlen(s + lead) + 1);       /* spaces in front, so that the drive's path is found */
  /* the drive's own path in front of it: "/mnt/usb0/Dumps" -> "Dumps" ("/data/Dumps" and "/user/Dumps" for the internal drive) */
  const char *pre[2] = { mount, !strcmp(mount ? mount : "", "/user") ? "/data" : NULL };
  for(int i = 0; i < 2; i++) {
    size_t pl = pre[i] ? strlen(pre[i]) : 0;
    if(pl > 1 && !strncmp(s, pre[i], pl) && (s[pl] == '/' || s[pl] == 0)) {
      memmove(s, s + pl, strlen(s + pl) + 1);
      break;
    }
  }
  /* spaces round the whole name and round every segment, double and outer slashes */
  char *w = s;
  const char *r = s;
  int seg_start = 1;
  while(*r) {
    if(*r == '/') {
      while(w > s && w[-1] == ' ') w--;                     /* spaces at the end of the segment */
      if(w > s && w[-1] != '/') *w++ = '/';
      seg_start = 1;
      r++;
      continue;
    }
    if(seg_start && *r == ' ') { r++; continue; }          /* spaces at the start of the segment */
    seg_start = 0;
    *w++ = *r++;
  }
  while(w > s && (w[-1] == ' ' || w[-1] == '/')) w--;
  *w = 0;
  return s;
}

/* 1 when s is valid UTF-8 (no overlong forms, no surrogates, nothing above U+10FFFF). */
static inline int
ps5tm_dumpdir_utf8_ok(const char *s) {
  const unsigned char *p = (const unsigned char *)s;
  while(*p) {
    if(*p < 0x80) { p++; continue; }
    int n;
    unsigned cp;
    if(*p >= 0xC2 && *p <= 0xDF)      { n = 1; cp = *p & 0x1Fu; }
    else if(*p >= 0xE0 && *p <= 0xEF) { n = 2; cp = *p & 0x0Fu; }
    else if(*p >= 0xF0 && *p <= 0xF4) { n = 3; cp = *p & 0x07u; }
    else return 0;
    for(int i = 1; i <= n; i++) {
      if((p[i] & 0xC0) != 0x80) return 0;
      cp = (cp << 6) | (p[i] & 0x3Fu);
    }
    if((n == 2 && (cp < 0x800 || (cp >= 0xD800 && cp <= 0xDFFF))) || (n == 3 && (cp < 0x10000 || cp > 0x10FFFF))) return 0;
    p += n + 1;
  }
  return 1;
}

/* 1 when s (already tidied) is safe to build a path from. */
static inline int
ps5tm_dumpdir_ok(const char *s) {
  size_t len = s ? strlen(s) : 0;
  if(len == 0 || len >= 96 || !ps5tm_dumpdir_utf8_ok(s)) return 0;
  int segs = 1;
  size_t seglen = 0;
  int only_dots = 1;
  for(size_t i = 0; i <= len; i++) {
    unsigned char c = (unsigned char)s[i];
    if(c == '/' || c == 0) {
      if(seglen == 0) return 0;
      if(only_dots) return 0;                               /* "." and ".." (and "...") */
      if(s[i - 1] == '.' || s[i - 1] == ' ') return 0;      /* exFAT and Windows drop them */
      if(c == 0) break;
      segs++;
      seglen = 0;
      only_dots = 1;
      continue;
    }
    if(c < 0x20 || c == 0x7f || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') return 0;
    if(c != '.') only_dots = 0;
    seglen++;
  }
  return segs <= 3;
}

#endif
