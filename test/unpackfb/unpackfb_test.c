/* Host test of src/unpackfb.h: when the app unpacks itself because ShadowMountPlus could not, and the percent of its job. */
#include <stdio.h>
#include <string.h>

#include "unpackfb.h"

static int g_fail, g_n;
#define CHECK(c) do { g_n++; if(!(c)) { g_fail++; printf("FAIL line %d: %s\n", __LINE__, #c); } } while(0)

int
main(void) {
  /* the case seen on 17.11.2026: ShadowMountPlus's unpack ended with status 13, "Permission denied" */
  CHECK(ps5tm_unpack_fallback_wanted("unpack", 13, "Permission denied") == 1);
  CHECK(ps5tm_unpack_fallback_wanted("unpack", 13, "") == 1);                 /* the number alone is enough */
  CHECK(ps5tm_unpack_fallback_wanted("unpack", 13, NULL) == 1);
  CHECK(ps5tm_unpack_fallback_wanted("unpack", 0, "Permission denied") == 1);  /* the text alone as well (a status that did not come) */
  CHECK(ps5tm_unpack_fallback_wanted("unpack", 5, "Permission denied (nested)") == 1);

  /* never a move, never another error, never a success */
  CHECK(ps5tm_unpack_fallback_wanted("move", 13, "Permission denied") == 0);
  CHECK(ps5tm_unpack_fallback_wanted("unpack", 0, "") == 0);
  CHECK(ps5tm_unpack_fallback_wanted("unpack", 0, NULL) == 0);
  CHECK(ps5tm_unpack_fallback_wanted("unpack", 28, "No space left on device") == 0);
  CHECK(ps5tm_unpack_fallback_wanted("unpack", 16, "Device busy") == 0);
  CHECK(ps5tm_unpack_fallback_wanted("unpack", 17, "File exists") == 0);
  CHECK(ps5tm_unpack_fallback_wanted("", 13, "Permission denied") == 0);
  CHECK(ps5tm_unpack_fallback_wanted(NULL, 13, "Permission denied") == 0);

  /* the percent of the app's own job (written, then read back: work_done/work_total) */
  CHECK(ps5tm_unpack_fb_percent(0, 0) == 0);
  CHECK(ps5tm_unpack_fb_percent(0, 1000) == 0);
  CHECK(ps5tm_unpack_fb_percent(1, 1000) == 0);
  CHECK(ps5tm_unpack_fb_percent(500, 1000) == 50);
  CHECK(ps5tm_unpack_fb_percent(999, 1000) == 99);
  CHECK(ps5tm_unpack_fb_percent(1000, 1000) == 100);
  CHECK(ps5tm_unpack_fb_percent(2000, 1000) == 100);
  CHECK(ps5tm_unpack_fb_percent(1738373738ull, 3476747476ull) == 50);          /* the size of Asterix & Obelix: Heroes, 1.7 GB twice */

  printf("%d checks, %d failed\n", g_n, g_fail);
  return g_fail ? 1 : 0;
}
