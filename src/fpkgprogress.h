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
  uint64_t meas0;     /* an image with packs: the job-wide count when its measure pass began */
  uint64_t meas_t;    /* ... and the length of that pass in the same count (an estimate; grows to what the pass has done when that was more) */
  int      meas_on;   /* meas0 and meas_t are taken */
} ps5tm_fpkgprog_t;

/* The writing bar. compress: the stage is "compress" (the first pass); jdone/jtotal: the job-wide numbers of Dump Forge. */
static inline void
ps5tm_fpkgprog_write(ps5tm_fpkgprog_t *p, int compress, uint64_t jdone, uint64_t jtotal, uint64_t *done, uint64_t *total) {
  if(!p->game) p->game = compress ? jtotal / 4 : jdone;      /* a first look in the write stage: the compress pass has read the game by now */
  if(compress && jdone > p->game) p->game = jdone;           /* the estimate was too low: the bar stays at its end */
  *total = p->game;
  *done = jdone < p->game ? jdone : p->game;
}

/* The bar of the MEASURE pass of an image with packs (20.11.2026: on a 6 GB game the page said only "wird vermessen" for nearly three minutes, as if it hung,
 * though Dump Forge counts this pass like every other). Dump Forge's numbers are the job-wide ones again (jobs.rs: Ctx::expect_rest, Ctx::progress): when the pass
 * begins, "total" is the job's whole expected movement, measure pass + write pass + the checks, and nothing before the pass has moved yet ("done" is the count
 * so far). The pass itself is told only through its end (convert.rs measure_packs, zstd_pack.rs measure): LZ4 reads the packed files once, zstd counts every
 * one of them twice ("one pass, counted as two"); the write pass is every file of the game again (src, the bytes the plan counted), and a full check reads every
 * byte twice more, as written and through the packs (convert.rs after_measure: src + 2 * check). So with a full check the length of the pass is what is left
 * over: span - 3 * src. A quick check reads only a sample (its size is not known here) and an unknown size of the game leaves nothing to subtract from: the pass
 * is then taken as what each kind of pack makes of the game (all files packed), or as its usual share of the job. A derived length that is more than that guess
 * (a wrong size of the game) is not believed. The bar never goes past its end, and, like the writing bar, it counts the job's own bytes, not the game's: the page
 * shows a percentage and a time to go.
 * src: the bytes of the game's files by the plan (0 = not known); zstd: the kind of packs; full: the check afterwards reads everything. */
static inline void
ps5tm_fpkgprog_measure(ps5tm_fpkgprog_t *p, int full, int zstd, uint64_t src, uint64_t jdone, uint64_t jtotal, uint64_t *done, uint64_t *total) {
  if(!p->meas_on) {
    p->meas_on = 1;
    p->meas0 = jdone;
    const uint64_t span = jtotal > jdone ? jtotal - jdone : 0;
    uint64_t t;
    if(src) {
      t = zstd ? src * 2 : src;                                            /* all files packed */
      if(full && span > src * 3) {
        const uint64_t m = span - src * 3;
        if(m <= t) t = m;                                                  /* some are not (small ones, loose ones) */
      }
    } else {
      /* no size of the game: lz4 full = 4 parts (pass, write, 2 checks), zstd full = 5 (the pass counts 2), a quick check is mostly the pass and the write */
      t = full ? (zstd ? span / 5 * 2 : span / 4) : (zstd ? span / 3 * 2 : span / 2);
    }
    if(t > span) t = span;
    if(t == 0) t = 1;
    p->meas_t = t;
  }
  if(jdone < p->meas0) p->meas0 = jdone;
  const uint64_t d = jdone - p->meas0;
  if(d > p->meas_t) p->meas_t = d;
  *total = p->meas_t;
  *done = d;
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

/* Dump Forge 0.6.0 and newer tell the bytes of the stage itself beside the job-wide ones (jobs.rs Ctx::progress: stage_done, stage_total; the Queue of its own
 * page draws one bar per stage from them). For the measure pass and the write pass of an image with packs that is exactly what the two guesses above stand
 * in for, so the page takes them when they are there: 1 with the bar in done/total, 0 when Dump Forge does not say (an older version, or a stage that moves
 * nothing) and the guess has to do. The count never goes past the end. (0.6.0 also counts the zstd measure pass once, no longer twice.) */
static inline int
ps5tm_fpkgprog_stage(uint64_t sdone, uint64_t stotal, uint64_t *done, uint64_t *total) {
  if(!stotal) return 0;
  *total = stotal;
  *done = sdone < stotal ? sdone : stotal;
  return 1;
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
