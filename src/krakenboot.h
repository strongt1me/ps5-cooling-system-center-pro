/* PS5 Cooling & System Center - Pro: Kraken Mount starts with the app (krakenboot.h, 17.11.2026, wish of the user: "Kraken Mount soll standardmäßig
 * eingeschaltet sein"). The decisions are here, without any I/O, so a host test can try them (test/krakenboot).
 *
 * Kraken Mount is a program of its own that keeps running when the app is replaced or restarted. The app starts it ~25 s after its own start unless the person
 * switched it off in the page (the marker file kraken/aus.txt; the page writes it with "Ausschalten" and removes it with "Einschalten"). Without the marker
 * - the state of every console until now - it is on. */
#ifndef PS5TM_KRAKENBOOT_H
#define PS5TM_KRAKENBOOT_H

#include <string.h>

/* 1 when the boot thread has to send Kraken Mount now. off: the marker of "switched off in the page" exists. alive: the HTTP status of Kraken Mount's
   /api/state (200 it answers, no_answer_code it listens but is busy - a scan, a drive that hangs - anything else nothing is there). A program that
   is there, however busy, is never started a second time: the second copy would only send the first one away. */
static inline int
ps5tm_krakenboot_should_start(int off, int alive, int no_answer_code) {
  if(off) return 0;
  if(alive == 200 || alive == no_answer_code) return 0;
  return 1;
}

/* A version without a leading "v". */
static inline const char *
ps5tm_krakenboot_plain(const char *v) {
  return v && (v[0] == 'v' || v[0] == 'V') ? v + 1 : v;
}

/* 1 when the running Kraken Mount is not the version the next start would take: the person has to switch it off and on again for the new one (the
   app never stops it by itself: a game may run from it). Unknown versions say nothing. */
static inline int
ps5tm_krakenboot_restart_needed(const char *running, const char *next) {
  if(!running || !running[0] || !next || !next[0]) return 0;
  return strcmp(ps5tm_krakenboot_plain(running), ps5tm_krakenboot_plain(next)) != 0;
}

#endif
