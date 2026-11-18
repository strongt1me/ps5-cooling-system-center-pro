/* PS5 Cooling & System Center - Pro: memory of image files the direct reader could not read (probefail.h, 18.11.2026).
 *
 * The reader of library.c (probe_read_images) opens the image file of every image game by itself, once per round (every ten minutes). A result that was
 * read is saved with the image's size and time and not read again until the file changes. A result that could NOT be read was not remembered at all: an
 * image of a format the reader does not know (a ".ffpfs" of 1.2 GB on the Pro of the user) was opened again every round, a "read" mark was written and
 * removed again, and one line stood in the log - 490 times between 15.11. and 18.11.2026.
 *
 * Now a failure is remembered the same way as a success: by title, size and time of the image file. While those are the same, the image is left alone,
 * but not for ever: a failure can also be a short one (a drive that dropped out for a moment), so it is tried again after PS5TM_PFAIL_RETRY_MS, and
 * whenever the person asks for the check ("Prüfung anfordern", POST /api/v1/library/probe), which forgets all of it. Only the first failure of an image
 * state is a log line. The list is in memory (a new start tries once more); it is used by the reader's thread alone. */
#ifndef PS5TM_PROBEFAIL_H
#define PS5TM_PROBEFAIL_H

#include <stdint.h>
#include <string.h>

#define PS5TM_PFAIL_MAX      256                                  /* as many as the list holds (LIBRARY_MAX): the reader looks at all image games (audit F-260) */
#define PS5TM_PFAIL_RETRY_MS (6ull * 60ull * 60ull * 1000ull)     /* a failure is tried again after six hours at the latest */

typedef struct {
  char     tid[12];
  int64_t  size;
  int64_t  mtime;
  uint64_t at_ms;               /* when it failed (monotonic clock) */
} ps5tm_pfail_entry_t;

typedef struct {
  ps5tm_pfail_entry_t e[PS5TM_PFAIL_MAX];
  int                 n;
} ps5tm_pfail_t;

static inline ps5tm_pfail_entry_t *
ps5tm_pfail_find(ps5tm_pfail_t *p, const char *tid) {
  for(int i = 0; i < p->n; i++)
    if(!strcmp(p->e[i].tid, tid)) return &p->e[i];
  return NULL;
}

/* 1 = leave this image alone: it failed with this very size and time and the pause is not over. A changed file, a pause that is over, or a clock that
   went back (the entry is from "the future") is 0: it is read. */
static inline int
ps5tm_pfail_skip(ps5tm_pfail_t *p, const char *tid, int64_t size, int64_t mtime, uint64_t now_ms) {
  const ps5tm_pfail_entry_t *e = ps5tm_pfail_find(p, tid);
  if(!e || e->size != size || e->mtime != mtime) return 0;
  if(now_ms < e->at_ms) return 0;
  return now_ms - e->at_ms < PS5TM_PFAIL_RETRY_MS;
}

/* Remember a failure. Returns 1 when it is news (the first failure of this image state: say it in the log) and 0 when it is the same failure again (a
   retry after the pause that failed once more: say nothing). A full list gives its oldest entry up. */
static inline int
ps5tm_pfail_note(ps5tm_pfail_t *p, const char *tid, int64_t size, int64_t mtime, uint64_t now_ms) {
  ps5tm_pfail_entry_t *e = ps5tm_pfail_find(p, tid);
  int news = 1;
  if(e) {
    news = (e->size != size || e->mtime != mtime);
  } else if(p->n < PS5TM_PFAIL_MAX) {
    e = &p->e[p->n++];
  } else {
    e = &p->e[0];
    for(int i = 1; i < p->n; i++)
      if(p->e[i].at_ms < e->at_ms) e = &p->e[i];
  }
  strncpy(e->tid, tid, sizeof(e->tid) - 1);
  e->tid[sizeof(e->tid) - 1] = '\0';
  e->size = size;
  e->mtime = mtime;
  e->at_ms = now_ms;
  return news;
}

/* The image was read well after all (or is gone): forget its failure. */
static inline void
ps5tm_pfail_forget(ps5tm_pfail_t *p, const char *tid) {
  for(int i = 0; i < p->n; i++) {
    if(!strcmp(p->e[i].tid, tid)) {
      p->e[i] = p->e[p->n - 1];
      p->n--;
      return;
    }
  }
}

/* The person asked for the check: every image is read again. */
static inline void
ps5tm_pfail_clear(ps5tm_pfail_t *p) {
  p->n = 0;
}

#endif
