/* PS5 Cooling & System Center - Pro: the marker of an eject that did not come back (21.11.2026; the pattern of Transmission for the PS5).
 *
 * It is written, and fsync'ed, just before ShadowMountPlus is asked to cut a drive off, and removed EJECTMARK_LATER_MS after the outcome is known (the
 * re-announce window of the drive: kernel log of the Pro, 21.11.2026). If it is still there when the app starts, the last eject did not come back: the app or
 * the console died inside it, or in the seconds after (a kernel panic). Then the eject stays OFF until the person allows it again (ps5tm_ejectmark_rearm, a
 * button in the pages): a console that crashed once from this does not need the next try, and the user whose console crashed again and again got no warning in
 * between. A planned end of the app removes the marker (ps5tm_ejectmark_shutdown): that is no crash. Its own file so that a host test can run it (test/ejectmark). */
#include "ps5tm.h"

#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#ifndef EJECTMARK_PATH
#define EJECTMARK_PATH PS5TM_DATA_DIR "/usb_eject_offen.txt"
#endif
#ifndef EJECTMARK_LATER_MS
#define EJECTMARK_LATER_MS 30000
#endif

static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static int             g_blocked;
/* Ejects that still need the marker: written, and not yet cleared (outcome unknown, or inside the re-announce window). One file serves them all, so it
   goes only when the last of them is cleared; writing and removing happen under g_mu, so a clear never removes the fresh marker of another eject
   (review of 21.11.2026, F4: two ejects of different drives within 30 s). Every write is followed by exactly one clear_now or clear_later. */
static unsigned        g_live;

void
ps5tm_ejectmark_init(void) {
  if(access(EJECTMARK_PATH, F_OK) != 0) return;
  pthread_mutex_lock(&g_mu);
  g_blocked = 1;
  pthread_mutex_unlock(&g_mu);
  PS5TM_WARN("usb_eject_blocked", "Das letzte Auswerfen eines USB-Laufwerks ist nicht zurückgekommen (die App oder die Konsole ist dabei oder kurz danach abgestürzt). "
             "Die App wirft keine Laufwerke aus, bis du es auf der Seite Dateien oder System wieder erlaubst.");
}

int
ps5tm_ejectmark_blocked(void) {
  pthread_mutex_lock(&g_mu);
  int b = g_blocked;
  pthread_mutex_unlock(&g_mu);
  return b;
}

void
ps5tm_ejectmark_write(const char *mount) {
  pthread_mutex_lock(&g_mu);
  g_live++;                                              /* counted even when the file cannot be written: its clear comes all the same */
  int fd = open(EJECTMARK_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if(fd < 0) { pthread_mutex_unlock(&g_mu); return; }
  char line[96];
  int n = snprintf(line, sizeof(line), "%s %llu\n", mount ? mount : "?", (unsigned long long)(ps5tm_now_ms() / 1000));
  if(n > 0 && write(fd, line, (size_t)n) < 0) { /* nothing to do: the marker is then just empty */ }
  fsync(fd);
  close(fd);
  pthread_mutex_unlock(&g_mu);
}

/* One eject no longer needs the marker; the file goes with the last one (a blocked state keeps it: only rearm lifts that). Called with g_mu held. */
static void
release_locked(void) {
  if(g_live > 0) g_live--;
  if(g_live == 0 && !g_blocked) unlink(EJECTMARK_PATH);
}

void
ps5tm_ejectmark_clear_now(void) {
  pthread_mutex_lock(&g_mu);
  release_locked();
  pthread_mutex_unlock(&g_mu);
}

static void *
clear_thread(void *arg) {
  (void)arg;
  usleep((useconds_t)EJECTMARK_LATER_MS * 1000);
  pthread_mutex_lock(&g_mu);
  release_locked();                                      /* a newer eject that wrote meanwhile keeps the file until it is cleared itself */
  pthread_mutex_unlock(&g_mu);
  return NULL;
}

/* Removed after the re-announce window, by a thread of its own: a crash inside the window leaves the marker. */
void
ps5tm_ejectmark_clear_later(void) {
  pthread_t t;
  pthread_attr_t at;
  pthread_attr_init(&at);
  pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
  if(pthread_create(&t, &at, clear_thread, NULL) != 0) ps5tm_ejectmark_clear_now();   /* no thread: better a clean marker than a false block */
  pthread_attr_destroy(&at);
}

/* The person allows ejecting again; 1 when it was blocked. */
int
ps5tm_ejectmark_rearm(void) {
  pthread_mutex_lock(&g_mu);
  int was = g_blocked;
  g_blocked = 0;
  if(was) unlink(EJECTMARK_PATH);                        /* while blocked no eject ran, so no other eject needs it */
  pthread_mutex_unlock(&g_mu);
  if(was) {
    PS5TM_INFO("usb_eject_rearmed", "Das Auswerfen von USB-Laufwerken wurde wieder erlaubt.");
  }
  return was;
}

/* The app ends as planned (a new version replaces it, the person ends it): that is no crash, so the marker goes - unless the eject is blocked from an
   earlier crash: that block stays over a restart of the app until the person lifts it. */
void
ps5tm_ejectmark_shutdown(void) {
  pthread_mutex_lock(&g_mu);
  g_live = 0;                                            /* the process ends: no eject of it needs the marker any more */
  if(!g_blocked) unlink(EJECTMARK_PATH);
  pthread_mutex_unlock(&g_mu);
}
