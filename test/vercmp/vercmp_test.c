/* Host test of src/vercmp.h: version texts from outside must compare, and the comparison must end (audit 18.11.2026). */
#include <stdio.h>

#include "vercmp.h"

static int fails;
#define CHECK(c, ...) do { if(!(c)) { fails++; printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while(0)

int
main(void) {
  /* ordinary versions */
  CHECK(ps5tm_ver_newer("0.5.5", "0.5.4") == 1, "0.5.5 > 0.5.4");
  CHECK(ps5tm_ver_newer("0.5.4", "0.5.5") == 0, "0.5.4 < 0.5.5");
  CHECK(ps5tm_ver_newer("0.5.0", "0.5.0") == 0, "equal");
  CHECK(ps5tm_ver_newer("0.5.0", "0.4.2") == 1, "0.5.0 > 0.4.2");
  CHECK(ps5tm_ver_newer("0.5.0", "0.10.0") == 0, "0.10.0 is newer than 0.5.0 (numbers, not text)");
  CHECK(ps5tm_ver_newer("0.10.0", "0.5.0") == 1, "0.10.0 > 0.5.0");
  CHECK(ps5tm_ver_newer("0.5.0", "1.0") == 0, "1.0 is newer");
  CHECK(ps5tm_ver_newer("0.5.0", "0.5") == 0, "0.5 against 0.5.0: equal as far as it goes");
  CHECK(ps5tm_ver_newer("0.5.1", "0.5") == 1, "0.5.1 > 0.5");
  /* "" is the oldest */
  CHECK(ps5tm_ver_newer("0.5.5", "") == 1, "anything > empty");
  CHECK(ps5tm_ver_newer("", "0.5.5") == 0, "empty is not newer");
  CHECK(ps5tm_ver_newer("", "") == 0, "empty equal");

  /* text from outside: every one of these must come back (the old loop did not for the first three) */
  CHECK(ps5tm_ver_newer("0.5.0", "0.5.0-pre1") == 0, "pre-release of the same number: not older");
  CHECK(ps5tm_ver_newer("0.5.0", "0.5.0+x") == 0, "build text");
  CHECK(ps5tm_ver_newer("0.5.0", "0.5.0 ") == 0, "trailing blank");
  CHECK(ps5tm_ver_newer("0.5.0", "0.4.9-pre1") == 1, "an older pre-release is older");
  CHECK(ps5tm_ver_newer("0.5.0", "v0.5.6") == 0, "leading v: 0.5.6 is newer than 0.5.0");
  CHECK(ps5tm_ver_newer("0.5.0", "v0.4.2") == 1, "leading v: 0.4.2 is older");
  CHECK(ps5tm_ver_newer("0.5.0", "abc") == 1 || ps5tm_ver_newer("0.5.0", "abc") == 0, "garbage ends");
  CHECK(ps5tm_ver_newer("0.5.0", "-") == 1 || ps5tm_ver_newer("0.5.0", "-") == 0, "dash ends");
  CHECK(ps5tm_ver_newer("0.5.0", "....") == 1 || ps5tm_ver_newer("0.5.0", "....") == 0, "dots end");
  CHECK(ps5tm_ver_newer("999.0.0-pre1", "999.0.0-pre1") == 0, "the same pre-release");

  /* known: only a text that starts with a number can be compared */
  CHECK(ps5tm_ver_known("0.5.6") == 1, "known");
  CHECK(ps5tm_ver_known("v0.5.6") == 1, "known with v");
  CHECK(ps5tm_ver_known("") == 0, "empty is not known");
  CHECK(ps5tm_ver_known("abc") == 0, "garbage is not known");
  CHECK(ps5tm_ver_known("-pre") == 0, "dash is not known");

  printf(fails ? "%d FAILED\n" : "all passed\n", fails);
  return fails != 0;
}
