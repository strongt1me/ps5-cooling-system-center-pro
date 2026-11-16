/* PS5 Cooling & System Center - Pro: progress of a long job in the notice at the top left of the screen (progressnote.h, 16.11.2026).
 *
 * A dump runs for minutes with the game on the screen and the app behind it, and the person sees nothing of it (wish of the user, 16.11.2026: "when a
 * dump runs, show it in the window at the left with a progress display"). The notice of the console cannot be changed once it is shown, so the progress
 * is a short series: one line at every tenth part of the work, but not more often than every 20 seconds, so that a fast dump says its start and its end
 * and a slow one does not fill the corner. The lines are single lines ("a · b"), for a line break plays the alert sound (see ps5tm_notify_plain). */
#ifndef PS5TM_PROGRESSNOTE_H
#define PS5TM_PROGRESSNOTE_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define PS5TM_PNOTE_STEP_PCT 10
#define PS5TM_PNOTE_MIN_GAP_MS 20000ull

typedef struct {
  uint64_t last_ms;       /* when the last line was due (or the job began) */
  int      next_pct;      /* the next tenth that is owed */
} ps5tm_pnote_t;

static inline void
ps5tm_pnote_reset(ps5tm_pnote_t *pn, uint64_t now_ms) {
  pn->last_ms = now_ms;
  pn->next_pct = PS5TM_PNOTE_STEP_PCT;
}

/* 1 when a line is due at pct (0..100) now: a tenth has been passed and the gap since the last line is over. The 100 % line is not sent: the job's own
   end message follows. */
static inline int
ps5tm_pnote_due(ps5tm_pnote_t *pn, int pct, uint64_t now_ms) {
  if(pct >= 100 || pct < pn->next_pct) return 0;
  if(now_ms < pn->last_ms || now_ms - pn->last_ms < PS5TM_PNOTE_MIN_GAP_MS) return 0;
  pn->last_ms = now_ms;
  pn->next_pct = (pct / PS5TM_PNOTE_STEP_PCT + 1) * PS5TM_PNOTE_STEP_PCT;
  return 1;
}

static inline void
ps5tm_pnote_size(char *out, size_t n, uint64_t bytes) {
  if(bytes >= (1ull << 30)) snprintf(out, n, "%.1f GB", (double)bytes / 1073741824.0);
  else                      snprintf(out, n, "%llu MB", (unsigned long long)((bytes + (1u << 19)) >> 20));
  for(char *p = out; *p; p++) if(*p == '.') *p = ',';
}

/* "Dump · Star Wars Racer Revenge · 40 % · 0,3 von 0,6 GB · noch etwa 4 Min." What is the kind of job ("Dump", "Disc sichern"). */
static inline void
ps5tm_pnote_text(char *out, size_t n, const char *what, const char *name, int pct, uint64_t done, uint64_t total, double rate) {
  char nm[48];
  size_t cut = strlen(name ? name : "");
  if(cut > 36) {
    cut = 36;
    while(cut > 0 && (((unsigned char)name[cut]) & 0xC0) == 0x80) cut--;      /* not in the middle of a letter */
    snprintf(nm, sizeof(nm), "%.*s…", (int)cut, name);
  } else {
    snprintf(nm, sizeof(nm), "%s", name ? name : "");
  }
  char d[24], t[24], eta[40] = "";
  ps5tm_pnote_size(d, sizeof(d), done);
  ps5tm_pnote_size(t, sizeof(t), total);
  if(rate > 1.0 && total > done) {
    double s = (double)(total - done) / rate;
    if(s < 90.0)            snprintf(eta, sizeof(eta), " · noch %d s", (int)(s + 0.5));
    else if(s < 5400.0)     snprintf(eta, sizeof(eta), " · noch etwa %d Min.", (int)(s / 60.0 + 0.5));
    else                    snprintf(eta, sizeof(eta), " · noch etwa %d Std.", (int)(s / 3600.0 + 0.5));
  }
  snprintf(out, n, "%s · %s · %d %% · %s von %s%s", what, nm, pct, d, t, eta);
}

#endif
