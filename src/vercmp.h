/* PS5 Cooling & System Center - Pro: comparing version texts (vercmp.h, 18.11.2026). Without any I/O, so a host test can try it (test/vercmp).
 *
 * The version of a program of another project is text from outside ("0.5.6", but a pre-release or a build of someone's own may say "0.5.0-pre1", "v0.5.6",
 * "0.5.0+x"): comparing it must end for every text (the first version of this loop ran for ever on "0.5.0-pre1": audit 18.11.2026, two reviewers). */
#ifndef PS5TM_VERCMP_H
#define PS5TM_VERCMP_H

#include <stdlib.h>

/* 1 when version a is newer than b, numbers part by part ("0.5.5" against "0.5.4"; "" is the oldest; a leading "v" is skipped; text without digits after a
   number ("-pre1", "+x") ends the comparison: what came before decides, else equal). */
static inline int
ps5tm_ver_newer(const char *a, const char *b) {
  if(*a == 'v' || *a == 'V') a++;
  if(*b == 'v' || *b == 'V') b++;
  for(int part = 0; (*a || *b) && part < 16; part++) {
    char *ea, *eb;
    const long x = strtol(a, &ea, 10), y = strtol(b, &eb, 10);
    if(x != y) return x > y;
    if(ea == a && eb == b) break;                      /* neither side has a number here: the rest is no more number */
    a = ea;
    b = eb;
    if(*a == '.') a++;
    if(*b == '.') b++;
  }
  return 0;
}

/* 1 when the text starts with a digit (after an optional "v"): a version that can be compared at all. */
static inline int
ps5tm_ver_known(const char *v) {
  if(*v == 'v' || *v == 'V') v++;
  return *v >= '0' && *v <= '9';
}

#endif
