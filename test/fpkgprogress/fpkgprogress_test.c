/* Host test of src/fpkgprogress.h: Dump Forge's job-wide progress as the two bars of the page (audit of the console, 18.11.2026). */
#include <stdio.h>
#include <stdint.h>

#include "fpkgprogress.h"

static int fails;
#define CHECK(c, ...) do { if(!(c)) { fails++; printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while(0)

int
main(void) {
  /* the numbers of the run on the console: PPSA34356 unpacked from zstd, 17.51 GB of files, package 5.19 GB */
  const uint64_t F = 17513002957ull, E0 = 70052011831ull, W = 5192166199ull;
  ps5tm_fpkgprog_t p = {0};
  uint64_t d, t;

  /* first look at the compress pass: the game is a quarter of the job-wide total, the bar is at its start */
  ps5tm_fpkgprog_write(&p, 1, 400000000ull, E0, &d, &t);
  CHECK(t == E0 / 4 && t > F - 4 && t < F + 4, "game %llu", (unsigned long long)t);
  CHECK(d == 400000000ull, "done %llu", (unsigned long long)d);

  /* later in the pass: the job-wide total did not change, the count grows */
  ps5tm_fpkgprog_write(&p, 1, F / 2, E0, &d, &t);
  CHECK(d == F / 2 && t == E0 / 4, "half");
  /* the time to go uses these: half of the game left */
  CHECK(t - d == t / 2 + (t & 1), "left");

  /* the end of the compress pass: the bar is full */
  ps5tm_fpkgprog_write(&p, 1, F, E0, &d, &t);
  CHECK(d == t, "full at the end of compress (%llu of %llu)", (unsigned long long)d, (unsigned long long)t);

  /* the write pass: the job-wide count goes on, the total was renewed (17.5 + 2 x 5.19 + 17.5); the bar stays full and does not move */
  ps5tm_fpkgprog_write(&p, 0, F + W / 2, F + 2 * W + F, &d, &t);
  CHECK(d == t && t == E0 / 4, "stays full while the package is written");

  /* an estimate that was too low: the pass reads more than a quarter of the total, the bar stays at its end and the total grows */
  ps5tm_fpkgprog_t q = {0};
  ps5tm_fpkgprog_write(&q, 1, 1000, 8000, &d, &t);
  CHECK(t == 2000 && d == 1000, "small start");
  ps5tm_fpkgprog_write(&q, 1, 3000, 8000, &d, &t);
  CHECK(t == 3000 && d == 3000, "grown to what was read (%llu of %llu)", (unsigned long long)d, (unsigned long long)t);

  /* a first look in the write stage (a tiny game, the compress pass was over before the first look): the game is what was read so far */
  ps5tm_fpkgprog_t r = {0};
  ps5tm_fpkgprog_write(&r, 0, 5000, 12000, &d, &t);
  CHECK(t == 5000 && d == 5000, "first look in write");

  /* reading back: the bar starts at 0 where the check began and ends at the job's end */
  const uint64_t ET = F + 2 * W + F;                     /* the job-wide total after the write pass renewed it */
  ps5tm_fpkgprog_t c = {0};
  ps5tm_fpkgprog_check(&c, F + W, ET, &d, &t);
  CHECK(d == 0 && t == ET - (F + W), "check starts at 0 (%llu of %llu)", (unsigned long long)d, (unsigned long long)t);
  ps5tm_fpkgprog_check(&c, F + W + (ET - (F + W)) / 2, ET, &d, &t);
  CHECK(d * 2 >= t - 1 && d * 2 <= t + 1, "check at half");
  ps5tm_fpkgprog_check(&c, ET, ET, &d, &t);
  CHECK(d == t && t > 0, "check ends full");

  /* a total that is renewed lower than the count (a pass that came out smaller): never more than 100 %, never a negative size */
  ps5tm_fpkgprog_check(&c, ET + 10, ET, &d, &t);
  CHECK(d == t, "capped");
  ps5tm_fpkgprog_t z = {0};
  ps5tm_fpkgprog_check(&z, 500, 100, &d, &t);
  CHECK(t == 1 && d == 0, "total below the start of the check");

  /* an image with packs (19.11.2026): the measure pass ended at M, the write pass begins there; its size is a share of what the job still has to move */
  {
    const uint64_t M = 20000000000ull, REST = 150000000000ull;           /* measure moved 20 GB; 150 GB are left: the write pass, then the checks */
    ps5tm_fpkgprog_t w = {0};
    ps5tm_fpkgprog_packwrite(&w, 1, M, M + REST, &d, &t);                /* first look in the write stage: the bar is at its start */
    CHECK(d == 0 && t == REST / 100 * 35, "pack write starts at 0 (%llu of %llu)", (unsigned long long)d, (unsigned long long)t);
    ps5tm_fpkgprog_packwrite(&w, 1, M + t / 2, M + REST, &d, &t);
    CHECK(d == t / 2 || d == t / 2 + 1 || d + 1 == t / 2, "pack write at half");
    ps5tm_fpkgprog_packwrite(&w, 1, M + REST / 100 * 35, M + REST, &d, &t);
    CHECK(d == t, "the share's end: full");
    /* a pass that reads more than the share: the bar stays at its end and the total grows with it, never a count beyond the total */
    ps5tm_fpkgprog_packwrite(&w, 1, M + REST / 2, M + REST, &d, &t);
    CHECK(d == t && d == REST / 2, "past the share: capped (%llu of %llu)", (unsigned long long)d, (unsigned long long)t);
    /* a total that was renewed higher (a pass learned its size): the share grows and never shrinks */
    ps5tm_fpkgprog_t w2 = {0};
    ps5tm_fpkgprog_packwrite(&w2, 1, 100, 1100, &d, &t);
    const uint64_t t1 = t;
    ps5tm_fpkgprog_packwrite(&w2, 1, 200, 2100, &d, &t);
    CHECK(t >= t1 && d == 100, "the share grows with the total");
    ps5tm_fpkgprog_packwrite(&w2, 1, 300, 800, &d, &t);                  /* the total comes out lower again: not lower than before */
    CHECK(t >= t1 && d == 200 && d <= t, "the share does not shrink");
    /* quick check: most of what is left is the write pass */
    ps5tm_fpkgprog_t w3 = {0};
    ps5tm_fpkgprog_packwrite(&w3, 0, M, M + REST, &d, &t);
    CHECK(t == REST / 100 * 85, "quick: the write pass is most of the rest");
    /* nothing left, a total below the count, a first look at zero: no division, no underflow */
    ps5tm_fpkgprog_t w4 = {0};
    ps5tm_fpkgprog_packwrite(&w4, 1, 500, 100, &d, &t);
    CHECK(t >= 1 && d == 0 && d <= t, "total below the count");
    ps5tm_fpkgprog_t w5 = {0};
    ps5tm_fpkgprog_packwrite(&w5, 1, 0, 0, &d, &t);
    CHECK(t >= 1 && d == 0, "all zero");
    /* the count goes down (a pass that began again): the bar starts again from there */
    ps5tm_fpkgprog_t w6 = {0};
    ps5tm_fpkgprog_packwrite(&w6, 1, 1000, 100000, &d, &t);
    ps5tm_fpkgprog_packwrite(&w6, 1, 400, 100000, &d, &t);
    CHECK(d == 0 && d <= t, "count went down");
  }

  printf(fails ? "%d FAILED\n" : "all passed\n", fails);
  return fails != 0;
}
