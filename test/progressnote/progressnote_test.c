/* Host test of src/progressnote.h: the progress lines of a long job at the top left of the screen (16.11.2026). */
#include "progressnote.h"

static int g_fail;

#define CHECK(c) do { if(!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); g_fail++; } } while(0)

int
main(void) {
  ps5tm_pnote_t pn;

  /* a fast dump (8 s): nothing between start and end */
  ps5tm_pnote_reset(&pn, 1000);
  for(int pct = 0; pct < 100; pct += 5) CHECK(!ps5tm_pnote_due(&pn, pct, 1000 + (uint64_t)pct * 80));

  /* a slow dump: a line at each tenth, 30 s apart */
  ps5tm_pnote_reset(&pn, 0);
  int lines = 0;
  uint64_t t = 0;
  for(int pct = 0; pct < 100; pct++) {
    t += 3000;                                         /* 3 s per percent: 300 s in all */
    if(ps5tm_pnote_due(&pn, pct, t)) lines++;
  }
  CHECK(lines == 9);                                   /* 10, 20, ... 90 */

  /* the gap holds a line back, and it comes as soon as the gap is over (a step is not lost) */
  ps5tm_pnote_reset(&pn, 0);
  CHECK(!ps5tm_pnote_due(&pn, 10, 5000));
  CHECK(ps5tm_pnote_due(&pn, 12, 21000));
  CHECK(!ps5tm_pnote_due(&pn, 15, 30000));             /* the next tenth is 20 */
  CHECK(!ps5tm_pnote_due(&pn, 25, 30000));             /* passed, but the gap is not over */
  CHECK(ps5tm_pnote_due(&pn, 25, 41500));
  CHECK(!ps5tm_pnote_due(&pn, 29, 90000));             /* the next is 30 */
  CHECK(ps5tm_pnote_due(&pn, 95, 90000));              /* a jump over several tenths is one line */
  CHECK(!ps5tm_pnote_due(&pn, 96, 200000));
  CHECK(!ps5tm_pnote_due(&pn, 100, 300000));           /* no 100 % line: the end message follows */

  /* a clock that went back */
  ps5tm_pnote_reset(&pn, 50000);
  CHECK(!ps5tm_pnote_due(&pn, 50, 1000));

  char s[200];
  ps5tm_pnote_text(s, sizeof(s), "Dump", "Star Wars Racer Revenge", 40, 264ull << 20, 660ull << 20, 87.0 * 1048576.0);
  CHECK(!strcmp(s, "Dump · Star Wars Racer Revenge · 40 % · 264 MB von 660 MB · noch 5 s"));
  ps5tm_pnote_text(s, sizeof(s), "Dump", "Forza", 50, 15ull << 30, 30ull << 30, 100.0 * 1048576.0);
  CHECK(!strcmp(s, "Dump · Forza · 50 % · 15,0 GB von 30,0 GB · noch etwa 3 Min."));
  ps5tm_pnote_text(s, sizeof(s), "Dump", "Big", 10, 5ull << 30, 50ull << 30, 5.0 * 1048576.0);
  CHECK(strstr(s, "noch etwa 3 Std.") != NULL);
  ps5tm_pnote_text(s, sizeof(s), "Dump", "X", 10, 1ull << 20, 1ull << 30, 0.0);
  CHECK(strstr(s, "noch") == NULL);                    /* no rate yet: no guess */
  ps5tm_pnote_text(s, sizeof(s), "Dump", "ÄÖÜÄÖÜÄÖÜÄÖÜÄÖÜÄÖÜÄÖÜÄÖÜÄÖÜÄÖÜ", 10, 1, 2, 0.0);
  CHECK(strstr(s, "…") != NULL);
  CHECK(strchr(s, '\n') == NULL);                      /* one line: a line break would play the alert sound */
  for(const unsigned char *p = (const unsigned char *)s; *p; p++) if(*p == 0xC3 && (p[1] & 0xC0) != 0x80) { CHECK(0); break; }   /* no letter cut */

  if(g_fail) { printf("%d failed\n", g_fail); return 1; }
  printf("progressnote: all checks passed\n");
  return 0;
}
