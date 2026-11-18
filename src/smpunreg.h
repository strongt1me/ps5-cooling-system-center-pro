/* PS5 Cooling & System Center - Pro: taking a title off ShadowMountPlus before its data is deleted (smpunreg.h, 18.11.2026).
 *
 * Finding (console test of "Original danach", 18.11.2026): a game folder that ShadowMountPlus had mounted (the title was not running) could not be
 * unregistered: "title runtime could not be released; its mount or sandbox is still busy". The copy of the original was whole and checked, the
 * original stayed (nothing is deleted when the unregistering is refused), and the person was left with two copies.
 *
 * So the unregistering goes in steps: when ShadowMountPlus lists the title as mounted it is unmounted first (never a title that runs: the caller's
 * unmount callback refuses that), and a refusal that says the mount is still busy is tried again after a short pause, three times in all. Any other
 * refusal ends it at once: that is an answer, not a moment. The steps are here as plain functions over callbacks, so that a host test can try the
 * order (test/smpunreg); gamedelete.c supplies the real calls. */
#ifndef PS5TM_SMPUNREG_H
#define PS5TM_SMPUNREG_H

#include <stddef.h>

#define PS5TM_UNREG_TRIES 3

typedef struct {
  int  (*mounted)(void *ctx);                              /* 1 when ShadowMountPlus lists the title as mounted (a fresh look) */
  int  (*unmount)(void *ctx, char *err, size_t n);         /* 0 when it took the title out; refuses a title that runs */
  int  (*uninstall)(void *ctx, char *err, size_t n);       /* 0 when the title is unregistered, else the refusal in err */
  void (*pause)(void *ctx, int seconds);
  void *ctx;
} ps5tm_unreg_ops_t;

/* Case-insensitive "contains" (strcasestr is not on every libc). */
static inline int
ps5tm_unreg_has(const char *text, const char *word) {
  if(!text || !word || !word[0]) return 0;
  for(const char *p = text; *p; p++) {
    size_t i = 0;
    while(word[i] && p[i] && ((p[i] | 0x20) == (word[i] | 0x20))) i++;     /* the words are letters and spaces: |0x20 folds the case */
    if(!word[i]) return 1;
  }
  return 0;
}

/* 1 when the refusal is the kind that passes: the mount or the sandbox of the title is still busy. */
static inline int
ps5tm_unreg_busy(const char *err) {
  return ps5tm_unreg_has(err, "busy") || ps5tm_unreg_has(err, "could not be released");
}

/* 0 when the title is unregistered; else -1 and the last refusal in err. unmounts counts the unmount attempts (may be NULL). */
static inline int
ps5tm_unreg_run(const ps5tm_unreg_ops_t *o, int tries, int *unmounts, char *err, size_t n) {
  if(unmounts) *unmounts = 0;
  for(int t = 0; t < tries; t++) {
    if(o->mounted(o->ctx)) {
      char ue[200];
      ue[0] = '\0';
      (void)o->unmount(o->ctx, ue, sizeof(ue));            /* a refusal is not the end: the uninstall below says what ShadowMountPlus thinks */
      if(unmounts) (*unmounts)++;
      o->pause(o->ctx, 1);                                 /* the mount point is released a moment after the answer */
    }
    err[0] = '\0';
    if(o->uninstall(o->ctx, err, n) == 0) return 0;
    if(!ps5tm_unreg_busy(err)) return -1;
    if(t + 1 < tries) o->pause(o->ctx, 3);
  }
  return -1;
}

#endif
