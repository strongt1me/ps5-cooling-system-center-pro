/* The pieces kept for the quick check of an exFAT image (gameconvert.c, 10.11.2026; a header of its own since 11.11.2026 so that
 * test/cvsample can run it without the rest of the app).
 *
 * While the image goes down in pieces (reads of the source), the writer hands each piece to cv_sampler_add() with its position, length
 * and CRC-32. The sampler keeps the first SAMPLE_EDGE pieces and then one piece about every "stride" bytes (the image divided by
 * SAMPLE_PIECES, at least min_stride, which is the size of one read, so a big image gets a few hundred of them). cv_sampler_finish()
 * adds the last SAMPLE_EDGE pieces, which the regular sampling does not reach. Afterwards smp[0..nsmp) are in ascending order of
 * position, without a piece twice, and are what is read back from the drive and compared with their CRC-32. */
#ifndef PS5TM_CVSAMPLE_H
#define PS5TM_CVSAMPLE_H

#include <stdint.h>

#define SAMPLE_PIECES    400u
#define SAMPLE_EDGE      3u
#define SAMPLE_MAX       (SAMPLE_PIECES + 2 * SAMPLE_EDGE + 8)

/* A piece of the image: where it lies, how long it is and the CRC-32 taken of it while it was written. */
typedef struct { uint64_t pos; uint32_t len, crc; } cv_sample_t;

typedef struct {
  cv_sample_t smp[SAMPLE_MAX];     /* the pieces kept, ascending by position                      */
  cv_sample_t tail[SAMPLE_EDGE];   /* the last SAMPLE_EDGE pieces seen (a ring, by iter)           */
  unsigned    nsmp, iter;          /* how many are kept, how many pieces were offered              */
  uint64_t    next_smp, stride;    /* the position from which the next piece is kept, the distance */
} cv_sampler_t;

static inline void
cv_sampler_init(cv_sampler_t *s, uint64_t raw, uint64_t min_stride) {
  s->nsmp = 0;
  s->iter = 0;
  s->next_smp = 0;
  s->stride = raw / SAMPLE_PIECES;
  if(s->stride < min_stride) s->stride = min_stride;
}

/* One piece of the image, in the order they are written. */
static inline void
cv_sampler_add(cv_sampler_t *s, uint64_t pos, uint32_t len, uint32_t crc) {
  cv_sample_t rec = { pos, len, crc };
  s->tail[s->iter % SAMPLE_EDGE] = rec;
  int take = s->iter < SAMPLE_EDGE || pos >= s->next_smp;
  if(pos >= s->next_smp) s->next_smp = pos + s->stride;
  if(take && s->nsmp < SAMPLE_MAX - SAMPLE_EDGE) s->smp[s->nsmp++] = rec;
  s->iter++;
}

/* After the last piece: the last SAMPLE_EDGE pieces are all in the list. The regular sampling may have kept one of them, or only the
   last but one: whatever it kept from the oldest of them on is one of these pieces, so those are dropped and the pieces added in order. */
static inline void
cv_sampler_finish(cv_sampler_t *s) {
  unsigned last = s->iter < SAMPLE_EDGE ? s->iter : SAMPLE_EDGE;
  if(!last) return;
  const uint64_t oldest = s->tail[(s->iter - last) % SAMPLE_EDGE].pos;
  while(s->nsmp && s->smp[s->nsmp - 1].pos >= oldest) s->nsmp--;
  for(unsigned t = last; t > 0; t--)
    if(s->nsmp < SAMPLE_MAX) s->smp[s->nsmp++] = s->tail[(s->iter - t) % SAMPLE_EDGE];
}

#endif
