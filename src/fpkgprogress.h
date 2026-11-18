/* PS5 Cooling & System Center - Pro: the progress of a debug FPKG job as the page shows it (fpkgprogress.h, 18.11.2026). The conversion is here, without any
 * I/O, so a host test can try it (test/fpkgprogress).
 *
 * Dump Forge reports ONE bar for the whole job (jobs.rs: Ctx::expect_rest / progress): "done" is the bytes of all passes so far, "total" what it expects
 * all passes to move together, renewed whenever a pass learns its size. At the first pass (compress) the total is the game read once (the source), the package
 * written and read back (twice the estimate of its size, taken as the game) and the files read back: about four times the game's bytes (a Marsupilami 2 that was
 * unpacked from zstd packs: 70.1 GB for 17.5 GB of files). The page shows two bars, one for writing and one for reading back, each with "x of y" and a time to
 * go, so the job-wide numbers made the writing bar end at 25 % ("17.5 of 70.1 GB"), the time to go four times too long, the check start at 38 % and the
 * end read "5.2 GB instead of 70.1 GB (7 %)" (found on the console, 18.11.2026).
 *
 * Writing (stages "compress" and "write"): the bar counts the game's own bytes. Their number is a quarter of the job-wide total seen at the first pass (and
 * grows to what the pass has read when that was less), the count is the job-wide one cut off at it: the compress pass is the one that takes the time.
 * Reading back (stages "verify" and "finalize"): the bar starts at the job-wide count of the moment the check began and ends where the job ends. */
#ifndef PS5TM_FPKGPROGRESS_H
#define PS5TM_FPKGPROGRESS_H

#include <stdint.h>

typedef struct {
  uint64_t game;      /* the game's bytes as Dump Forge counts them; 0 until the first pass is seen */
  uint64_t check0;    /* the job-wide count when the check began */
  int      check_on;  /* check0 is taken */
} ps5tm_fpkgprog_t;

/* The writing bar. compress: the stage is "compress" (the first pass); jdone/jtotal: the job-wide numbers of Dump Forge. */
static inline void
ps5tm_fpkgprog_write(ps5tm_fpkgprog_t *p, int compress, uint64_t jdone, uint64_t jtotal, uint64_t *done, uint64_t *total) {
  if(!p->game) p->game = compress ? jtotal / 4 : jdone;      /* a first look in the write stage: the compress pass has read the game by now */
  if(compress && jdone > p->game) p->game = jdone;           /* the estimate was too low: the bar stays at its end */
  *total = p->game;
  *done = jdone < p->game ? jdone : p->game;
}

/* The bar of reading back. */
static inline void
ps5tm_fpkgprog_check(ps5tm_fpkgprog_t *p, uint64_t jdone, uint64_t jtotal, uint64_t *done, uint64_t *total) {
  if(!p->check_on) { p->check_on = 1; p->check0 = jdone; }
  if(jdone < p->check0) p->check0 = jdone;
  const uint64_t t = jtotal > p->check0 ? jtotal - p->check0 : 1;
  const uint64_t d = jdone - p->check0;
  *total = t;
  *done = d < t ? d : t;
}

#endif
