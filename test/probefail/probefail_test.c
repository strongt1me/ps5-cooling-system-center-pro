/* Host test of src/probefail.h: the memory of image files the direct reader could not read (18.11.2026). */
#include <stdio.h>

#include "probefail.h"

static int g_fail;

#define CHECK(c) do { if(!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); g_fail++; } } while(0)

#define MIN(x) ((uint64_t)(x) * 60ull * 1000ull)
#define HOUR(x) (MIN(60) * (uint64_t)(x))

int
main(void) {
  static ps5tm_pfail_t p;                                   /* empty at the start, as in the reader */

  /* nothing remembered: every image is read */
  CHECK(!ps5tm_pfail_skip(&p, "PPSA25872", 1215430656, 1000, 0));

  /* the first failure is news (a log line), then the image is left alone round after round (every 10 min) */
  CHECK(ps5tm_pfail_note(&p, "PPSA25872", 1215430656, 1000, HOUR(1)));
  for(int k = 1; k <= 30; k++) CHECK(ps5tm_pfail_skip(&p, "PPSA25872", 1215430656, 1000, HOUR(1) + MIN(10) * (uint64_t)k));   /* 5 hours in all */

  /* another image is not affected */
  CHECK(!ps5tm_pfail_skip(&p, "PPSA03117", 302907392, 2000, HOUR(1) + MIN(10)));

  /* the file changed (another size, or another time): it is read again */
  CHECK(!ps5tm_pfail_skip(&p, "PPSA25872", 1215430657, 1000, HOUR(1) + MIN(10)));
  CHECK(!ps5tm_pfail_skip(&p, "PPSA25872", 1215430656, 1001, HOUR(1) + MIN(10)));

  /* the pause is over after six hours: tried again - and when it fails once more, that is not news */
  CHECK(ps5tm_pfail_skip(&p, "PPSA25872", 1215430656, 1000, HOUR(1) + PS5TM_PFAIL_RETRY_MS - 1));
  CHECK(!ps5tm_pfail_skip(&p, "PPSA25872", 1215430656, 1000, HOUR(1) + PS5TM_PFAIL_RETRY_MS));
  CHECK(!ps5tm_pfail_note(&p, "PPSA25872", 1215430656, 1000, HOUR(7)));
  CHECK(ps5tm_pfail_skip(&p, "PPSA25872", 1215430656, 1000, HOUR(7) + MIN(10)));            /* the pause starts anew */
  CHECK(p.n == 1);                                                                          /* the same title is one entry */

  /* a file that changed and fails again is news again */
  CHECK(ps5tm_pfail_note(&p, "PPSA25872", 1215430999, 1000, HOUR(8)));
  CHECK(p.n == 1);
  CHECK(ps5tm_pfail_skip(&p, "PPSA25872", 1215430999, 1000, HOUR(8) + MIN(10)));
  CHECK(!ps5tm_pfail_skip(&p, "PPSA25872", 1215430656, 1000, HOUR(8) + MIN(10)));           /* the old state is gone */

  /* a clock that went back (the entry is from the future): read it, do not wait for ever */
  CHECK(!ps5tm_pfail_skip(&p, "PPSA25872", 1215430999, 1000, HOUR(2)));

  /* read well after all: forgotten */
  CHECK(ps5tm_pfail_note(&p, "PPSA03117", 302907392, 2000, HOUR(9)));
  CHECK(p.n == 2);
  ps5tm_pfail_forget(&p, "PPSA25872");
  CHECK(p.n == 1);
  CHECK(!ps5tm_pfail_skip(&p, "PPSA25872", 1215430999, 1000, HOUR(9) + MIN(10)));
  CHECK(ps5tm_pfail_skip(&p, "PPSA03117", 302907392, 2000, HOUR(9) + MIN(10)));             /* the other one stayed */
  ps5tm_pfail_forget(&p, "PPSA99999");                                                      /* an unknown one: nothing happens */
  CHECK(p.n == 1);

  /* the person asked for the check: everything is read again */
  ps5tm_pfail_clear(&p);
  CHECK(p.n == 0);
  CHECK(!ps5tm_pfail_skip(&p, "PPSA03117", 302907392, 2000, HOUR(9) + MIN(10)));
  CHECK(ps5tm_pfail_note(&p, "PPSA03117", 302907392, 2000, HOUR(10)));                      /* and news again if it fails again */

  /* a full list gives its oldest entry up, the newest is kept */
  ps5tm_pfail_clear(&p);
  for(int i = 0; i < PS5TM_PFAIL_MAX; i++) {
    char tid[24];
    snprintf(tid, sizeof(tid), "PPSA%05d", i);
    CHECK(ps5tm_pfail_note(&p, tid, 100 + i, 5, HOUR(1) + MIN(1) * (uint64_t)i));
  }
  CHECK(p.n == PS5TM_PFAIL_MAX);
  CHECK(ps5tm_pfail_note(&p, "PPSA77777", 1, 1, HOUR(3)));
  CHECK(p.n == PS5TM_PFAIL_MAX);
  CHECK(!ps5tm_pfail_skip(&p, "PPSA00000", 100, 5, HOUR(3) + MIN(1)));                      /* the oldest was dropped: it is read again */
  CHECK(ps5tm_pfail_skip(&p, "PPSA77777", 1, 1, HOUR(3) + MIN(1)));
  CHECK(ps5tm_pfail_skip(&p, "PPSA00001", 101, 5, HOUR(3) + MIN(1)));

  /* a title id that is too long is cut, never written past the field */
  ps5tm_pfail_clear(&p);
  CHECK(ps5tm_pfail_note(&p, "PPSA123456789012345", 1, 1, 0));
  CHECK(strlen(p.e[0].tid) == sizeof(p.e[0].tid) - 1);

  if(g_fail) { printf("%d check(s) failed\n", g_fail); return 1; }
  printf("probefail: all checks passed\n");
  return 0;
}
