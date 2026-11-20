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
  uint64_t write0;    /* an image with packs: the job-wide count when its write pass began (after the measure pass) */
  uint64_t rest;      /* ... and what the job still had to move then (the largest it has been since) */
  int      write_on;  /* write0 is taken */
} ps5tm_fpkgprog_t;

/* The writing bar. compress: the stage is "compress" (the first pass); jdone/jtotal: the job-wide numbers of Dump Forge. */
static inline void
ps5tm_fpkgprog_write(ps5tm_fpkgprog_t *p, int compress, uint64_t jdone, uint64_t jtotal, uint64_t *done, uint64_t *total) {
  if(!p->game) p->game = compress ? jtotal / 4 : jdone;      /* a first look in the write stage: the compress pass has read the game by now */
  if(compress && jdone > p->game) p->game = jdone;           /* the estimate was too low: the bar stays at its end */
  *total = p->game;
  *done = jdone < p->game ? jdone : p->game;
}

/* The writing bar of an IMAGE WITH PACKS (an .exfat or .ffpkg that Dump Forge writes with the game packed straight in, 19.11.2026). Its stages are "measure"
 * (the packed files are compressed once without keeping the result: the page says "wird vermessen"), "write" (the image is written: every file is read again,
 * the packs are compressed a second time), "verify" and "finalize". Dump Forge's bar is again one for the whole job, and the size of the write pass is not told:
 * it is what the physical files of the image come to (the packs, which are smaller than the game by an amount nobody knows before), and the checks after it add
 * the game once (read through the packs) and the image once or twice more. So the pass is taken as a share of what the job had left when it began: a bit more
 * than a third when every byte is read back (full), most of it when only a sample is (quick). A guess, and a low one on purpose: a bar that reaches its end a
 * little early and waits is better than one that stops short and jumps. It never goes past its end, and the numbers are not the game's bytes (the page shows
 * a percentage and a time to go for such a job, not "x of y GB"). */
static inline void
ps5tm_fpkgprog_packwrite(ps5tm_fpkgprog_t *p, int full, uint64_t jdone, uint64_t jtotal, uint64_t *done, uint64_t *total) {
  if(!p->write_on) { p->write_on = 1; p->write0 = jdone; }
  if(jdone < p->write0) p->write0 = jdone;
  const uint64_t left = jtotal > p->write0 ? jtotal - p->write0 : 1;
  if(left > p->rest) p->rest = left;
  uint64_t t = full ? p->rest / 100 * 35 : p->rest / 100 * 85;
  if(t == 0) t = 1;
  const uint64_t d = jdone - p->write0;
  if(d > t) t = d;
  *total = t;
  *done = d;
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
