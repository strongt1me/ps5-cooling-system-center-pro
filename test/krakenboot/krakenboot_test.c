/* Host test of src/krakenboot.h: when the app starts Kraken Mount by itself, and when the running one is not the version the next start takes (17.11.2026). */
#include "krakenboot.h"

#include <stdio.h>

static int g_fail;

#define CHECK(cond, ...) do { if(!(cond)) { printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); g_fail++; } } while(0)

#define NO_ANSWER (-2)

int
main(void) {
  /* nothing there, not switched off: start */
  CHECK(ps5tm_krakenboot_should_start(0, -1, NO_ANSWER) == 1, "nothing listens");
  CHECK(ps5tm_krakenboot_should_start(0, 0, NO_ANSWER) == 1, "status 0");
  CHECK(ps5tm_krakenboot_should_start(0, 500, NO_ANSWER) == 1, "a 500 is not a Kraken Mount that works");
  CHECK(ps5tm_krakenboot_should_start(0, 404, NO_ANSWER) == 1, "a 404 on the port is something else");
  /* it answers, or listens and is busy: never a second copy */
  CHECK(ps5tm_krakenboot_should_start(0, 200, NO_ANSWER) == 0, "it answers");
  CHECK(ps5tm_krakenboot_should_start(0, NO_ANSWER, NO_ANSWER) == 0, "it is busy");
  /* switched off in the page: never */
  CHECK(ps5tm_krakenboot_should_start(1, -1, NO_ANSWER) == 0, "off and nothing there");
  CHECK(ps5tm_krakenboot_should_start(1, 200, NO_ANSWER) == 0, "off and it answers");
  CHECK(ps5tm_krakenboot_should_start(1, 500, NO_ANSWER) == 0, "off and a 500");

  /* the version of the running one against the next start */
  CHECK(ps5tm_krakenboot_restart_needed("0.2.0", "0.2.1") == 1, "older running");
  CHECK(ps5tm_krakenboot_restart_needed("0.2.1", "0.2.1") == 0, "same version");
  CHECK(ps5tm_krakenboot_restart_needed("0.2.1", "0.3.0") == 1, "0.2.1 still runs after the app brought 0.3.0 (18.11.2026): the page says so");
  CHECK(ps5tm_krakenboot_restart_needed("0.3.0", "0.3.0") == 0, "0.3.0 runs, 0.3.0 is embedded");
  CHECK(ps5tm_krakenboot_restart_needed("v0.2.1", "0.2.1") == 0, "a v in front of the running one");
  CHECK(ps5tm_krakenboot_restart_needed("0.2.1", "v0.2.1") == 0, "a v in front of the next one");
  CHECK(ps5tm_krakenboot_restart_needed("0.2.2", "0.2.1") == 1, "a newer running one is also different");
  CHECK(ps5tm_krakenboot_restart_needed("0.0.1-beta", "0.2.1") == 1, "a beta");
  CHECK(ps5tm_krakenboot_restart_needed("", "0.2.1") == 0, "unknown running version");
  CHECK(ps5tm_krakenboot_restart_needed(NULL, "0.2.1") == 0, "no running version");
  CHECK(ps5tm_krakenboot_restart_needed("0.2.0", "") == 0, "unknown next version");
  CHECK(ps5tm_krakenboot_restart_needed("0.2.0", NULL) == 0, "no next version");
  CHECK(!strcmp(ps5tm_krakenboot_plain("v1.2"), "1.2") && !strcmp(ps5tm_krakenboot_plain("1.2"), "1.2") && ps5tm_krakenboot_plain(NULL) == NULL, "plain");

  /* the console went down between the marker and now (the uptime of the boot is smaller than the marker's) */
  CHECK(ps5tm_krakenboot_went_down(1, 65, 60, 0, 0) == 1, "a smaller uptime is another boot (the app starts a bit earlier in the next one)");
  CHECK(ps5tm_krakenboot_went_down(1, 65, 70, 0, 0) == 0, "a larger uptime is the same boot: the app was only replaced");
  CHECK(ps5tm_krakenboot_went_down(1, 65, 65, 0, 0) == 0, "the same second is the same boot");
  CHECK(ps5tm_krakenboot_went_down(1, 4000, 70, 0, 0) == 1, "a console that ran for an hour and started again");
  CHECK(ps5tm_krakenboot_went_down(0, 65, 10, 0, 0) == 0, "a marker that could not be read says nothing");
  /* the app starts later after the next boot (the jailbreak is run by hand): the uptime alone would miss it, the boot time does not */
  CHECK(ps5tm_krakenboot_went_down(1, 65, 300, 1000000, 1000900) == 1, "another boot, the app starts later this time");
  CHECK(ps5tm_krakenboot_went_down(1, 65, 300, 1000000, 1000010) == 0, "the same boot: the boot time moved by ten seconds (the clock was set)");
  CHECK(ps5tm_krakenboot_went_down(1, 65, 300, 1000000, 1000045) == 0, "the same boot: the boot time moved by 45 seconds");
  CHECK(ps5tm_krakenboot_went_down(1, 65, 300, 1000000, 1000100) == 1, "another boot: more than a minute later");
  CHECK(ps5tm_krakenboot_went_down(1, 65, 300, 0, 1000900) == 0, "a marker without a boot time: the uptime says");
  CHECK(ps5tm_krakenboot_went_down(1, 65, 300, 1000000, 0) == 0, "a boot time that cannot be read now: the uptime says");

  if(g_fail) { printf("%d failed\n", g_fail); return 1; }
  printf("krakenboot: all checks passed\n");
  return 0;
}
