/* Reading and writing at the same time — but only across two drives
 * (03.10.2026).
 *
 * A copy or conversion used to read a piece, write it, read the next: the
 * source drive idles while the target writes, and the other way round. From
 * the internal SSD to a USB drive, or from one USB drive to another, both can
 * work at once. On ONE drive they cannot — overlapping makes it jump between
 * two places, and the copy gets slower, not faster. PS5 Game Compressor found
 * the same (its 0.9.7 notes: overlap only "when source and destination are
 * confirmed to be on different physical devices"); its code has no licence,
 * this is written anew.
 *
 * Which drive a path is on: USB drives are mounted from /dev/daN (a slice
 * behind that is a partition of the same drive), the M.2 expansion is
 * /mnt/ext1, an extended-storage drive /mnt/ext0, and everything outside /mnt
 * is the internal SSD. What cannot be told for certain is treated as one
 * drive: then nothing changes against before. */

#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef PS5TM_HOST_TEST
#include <sys/mount.h>
#include <sys/param.h>
#endif

#include "iopolicy.h"


/* ------------------------------------------------------------- which drive */

/* The physical drive behind path, as a short name: "da0", "ext1", "internal";
   "" when unknown. A path that does not exist yet is looked up through the
   nearest folder above it that does. */
static void
drive_key(const char *path, char *key, size_t key_len) {
  key[0] = 0;
  if(!path || path[0] != '/') return;
#ifndef PS5TM_HOST_TEST
  char probe[PATH_MAX];
  snprintf(probe, sizeof(probe), "%s", path);
  for(;;) {
    struct statfs sf;
    if(statfs(probe, &sf) == 0) {
      int n;
      if(sscanf(sf.f_mntfromname, "/dev/da%d", &n) == 1) {
        snprintf(key, key_len, "da%d", n);
        return;
      }
      break;
    }
    char *sl = strrchr(probe, '/');
    if(!sl || sl == probe) break;
    *sl = 0;
  }
#endif
  int n;
  if(sscanf(path, "/mnt/ext%d", &n) == 1) snprintf(key, key_len, "ext%d", n);
  else if(sscanf(path, "/mnt/usb%d", &n) == 1) snprintf(key, key_len, "usb%d", n);
  else if(strncmp(path, "/mnt/", 5)) snprintf(key, key_len, "internal");
}

int
ps5tm_io_parallel(const char *src, const char *dst, char *why, size_t why_len) {
#ifdef PS5TM_HOST_TEST
  /* The host has one disk; the tests choose the path to run. */
  const char *force = getenv("PS5TM_IO_PARALLEL");
  if(force) {
    snprintf(why, why_len, "Test: %s", force[0] == '1' ? "parallel" : "nacheinander");
    return force[0] == '1';
  }
#endif
  char a[24], b[24];
  drive_key(src, a, sizeof(a));
  drive_key(dst, b, sizeof(b));
  if(!a[0] || !b[0]) {
    snprintf(why, why_len, "Laufwerk nicht sicher bestimmbar (%s/%s), nacheinander",
             a[0] ? a : "?", b[0] ? b : "?");
    return 0;
  }
  if(!strcmp(a, b)) {
    snprintf(why, why_len, "beides auf %s, nacheinander", a);
    return 0;
  }
  snprintf(why, why_len, "%s → %s, Lesen und Schreiben gleichzeitig", a, b);
  return 1;
}


/* --------------------------------------------------------------- read-ahead */

typedef struct {
  uint8_t *buf;
  size_t   len, pos;
  int      err;                  /* errno of a failed read, 0 = none */
  int      eof;
  int      full;
} pf_slot_t;

struct ps5tm_prefetch {
  ps5tm_read_fn   rd;
  void           *ctx;
  size_t          chunk;
  unsigned        n, head, tail;  /* head: next to fill, tail: next to read */
  pf_slot_t      *slots;
  int             stop, done;
  pthread_mutex_t mu;
  pthread_cond_t  filled, freed;
  pthread_t       th;
};

static void *
pf_thread(void *arg) {
  ps5tm_prefetch_t *pf = arg;
  for(;;) {
    pthread_mutex_lock(&pf->mu);
    while(!pf->stop && pf->slots[pf->head].full)
      pthread_cond_wait(&pf->freed, &pf->mu);
    if(pf->stop) { pthread_mutex_unlock(&pf->mu); break; }
    pf_slot_t *s = &pf->slots[pf->head];
    pthread_mutex_unlock(&pf->mu);

    /* Filled to the brim where the source allows, so the reader on the other
       side gets whole pieces. */
    size_t got = 0;
    int err = 0, eof = 0;
    while(got < pf->chunk) {
      ssize_t r = pf->rd(pf->ctx, s->buf + got, pf->chunk - got);
      if(r < 0) { err = errno ? errno : EIO; break; }
      if(r == 0) { eof = 1; break; }
      got += (size_t)r;
    }

    pthread_mutex_lock(&pf->mu);
    s->len  = got;
    s->pos  = 0;
    s->err  = err;
    s->eof  = eof;
    s->full = 1;
    pf->head = (pf->head + 1) % pf->n;
    pthread_cond_signal(&pf->filled);
    int last = err || eof;
    pthread_mutex_unlock(&pf->mu);
    if(last) break;
  }
  pthread_mutex_lock(&pf->mu);
  pf->done = 1;
  pthread_cond_broadcast(&pf->filled);
  pthread_mutex_unlock(&pf->mu);
  return NULL;
}

ps5tm_prefetch_t *
ps5tm_prefetch_start(ps5tm_read_fn rd, void *ctx, size_t chunk, unsigned nbuf) {
  if(!rd || !chunk || nbuf < 2) return NULL;
  ps5tm_prefetch_t *pf = calloc(1, sizeof(*pf));
  if(!pf) return NULL;
  pf->rd = rd;
  pf->ctx = ctx;
  pf->chunk = chunk;
  pf->n = nbuf;
  pf->slots = calloc(nbuf, sizeof(pf_slot_t));
  int ok = pf->slots != NULL;
  for(unsigned i = 0; ok && i < nbuf; i++)
    ok = (pf->slots[i].buf = malloc(chunk)) != NULL;
  if(ok) {
    pthread_mutex_init(&pf->mu, NULL);
    pthread_cond_init(&pf->filled, NULL);
    pthread_cond_init(&pf->freed, NULL);
    if(pthread_create(&pf->th, NULL, pf_thread, pf) == 0) return pf;
    pthread_cond_destroy(&pf->filled);
    pthread_cond_destroy(&pf->freed);
    pthread_mutex_destroy(&pf->mu);
  }
  for(unsigned i = 0; pf->slots && i < nbuf; i++) free(pf->slots[i].buf);
  free(pf->slots);
  free(pf);
  return NULL;
}

ssize_t
ps5tm_prefetch_read(void *arg, void *buf, size_t len) {
  ps5tm_prefetch_t *pf = arg;
  uint8_t *out = buf;
  size_t given = 0;
  pthread_mutex_lock(&pf->mu);
  while(given < len) {
    pf_slot_t *s = &pf->slots[pf->tail];
    while(!s->full && !pf->done) pthread_cond_wait(&pf->filled, &pf->mu);
    if(!s->full) break;                          /* the thread has ended */
    size_t n = s->len - s->pos;
    if(n > len - given) n = len - given;
    memcpy(out + given, s->buf + s->pos, n);
    s->pos += n;
    given += n;
    if(s->pos < s->len) break;                   /* the caller's buffer is full */
    if(s->err || s->eof) {
      /* Data before the failure or the end goes out first; the failure or the
         end itself on the next call. The slot stays full, so it repeats. */
      if(given) break;
      int err = s->err;
      pthread_mutex_unlock(&pf->mu);
      if(err) { errno = err; return -1; }
      return 0;
    }
    s->full = 0;
    pf->tail = (pf->tail + 1) % pf->n;
    pthread_cond_signal(&pf->freed);
    if(given) break;                             /* hand over what there is */
  }
  pthread_mutex_unlock(&pf->mu);
  return (ssize_t)given;
}

void
ps5tm_prefetch_stop(ps5tm_prefetch_t *pf) {
  if(!pf) return;
  pthread_mutex_lock(&pf->mu);
  pf->stop = 1;
  pthread_cond_broadcast(&pf->freed);
  pthread_mutex_unlock(&pf->mu);
  pthread_join(pf->th, NULL);
  pthread_cond_destroy(&pf->filled);
  pthread_cond_destroy(&pf->freed);
  pthread_mutex_destroy(&pf->mu);
  for(unsigned i = 0; i < pf->n; i++) free(pf->slots[i].buf);
  free(pf->slots);
  free(pf);
}
