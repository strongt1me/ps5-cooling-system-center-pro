/* PS5 Cooling & System Center - Pro: Kraken Mount starts with the app (krakenboot.h, 17.11.2026, wish of the user: "Kraken Mount soll standardmäßig
 * eingeschaltet sein"). The decisions are here, without any I/O, so a host test can try them (test/krakenboot).
 *
 * Kraken Mount is a program of its own that keeps running when the app is replaced or restarted. The app starts it ~5 s after its own start unless the person
 * switched it off in the page (the marker file kraken/aus.txt; the page writes it with "Ausschalten" and removes it with "Einschalten"). Without the marker
 * - the state of every console until now - it is on. */
#ifndef PS5TM_KRAKENBOOT_H
#define PS5TM_KRAKENBOOT_H

#include <string.h>

#include "vercmp.h"

/* 1 when the boot thread has to send Kraken Mount now. off: the marker of "switched off in the page" exists. alive: the HTTP status of Kraken Mount's
   /api/state (200 it answers, no_answer_code it listens but is busy - a scan, a drive that hangs - anything else nothing is there). A program that
   is there, however busy, is never started a second time: the second copy would only send the first one away. */
static inline int
ps5tm_krakenboot_should_start(int off, int alive, int no_answer_code) {
  if(off) return 0;
  if(alive == 200 || alive == no_answer_code) return 0;
  return 1;
}

/* The console went down between the marker of a start with the app and now (audit 17.11.2026): the uptime only grows within a boot, so a smaller one is
   another boot. The app starts at about the same time after every boot and writes the marker five seconds after that, so a start-up that crashed the console
   finds a smaller uptime than the marker holds; a replaced app in the same boot finds a larger one. 0 for a marker that could not be read. */
static inline int
ps5tm_krakenboot_went_down(int marker_ok, unsigned long long written_uptime_s, unsigned long long now_uptime_s,
                           long long written_boot_s, long long now_boot_s) {
  if(!marker_ok) return 0;
  if(now_uptime_s < written_uptime_s) return 1;                    /* the uptime fell: another boot */
  /* The boot time of the system (seconds, 0 = not known) moves only a little within one boot (the clock being set: a jump of more than a minute while the marker
   exists, at most a minute after a send, is unlikely); another boot is more than a minute later. Needed
     because the app does not start at the same time after every boot (the jailbreak is run by hand), so the uptime alone is no safe sign. */
  if(written_boot_s > 0 && now_boot_s > 0) {
    long long d = now_boot_s - written_boot_s;
    if(d < 0) d = -d;
    if(d > 60) return 1;                                           /* the console that went down was up for a minute at least and was off for a while: the next boot is more than that later */
  }
  return 0;
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

/* 1 when a downloaded version is to be started instead of the embedded one: only when it is newer (audit F-251, 18.11.2026: a version loaded with the button
   before an app update used to start instead of the newer embedded one - Kraken Mount before 0.3.0 let saves disappear). "" or a text that is no version: no. */
static inline int
ps5tm_krakenboot_take_fetched(const char *fetched, const char *embedded) {
  return fetched && fetched[0] && embedded && ps5tm_ver_known(fetched) && ps5tm_ver_newer(fetched, embedded);
}

#endif
