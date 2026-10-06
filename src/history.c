/* Long-term temperature history.
 *
 * The dashboard's live chart covers two minutes and is gone on restart, which
 * is useless for the question a thermal manager actually has to answer: "does
 * this console run hot over an evening, and when?" One sample per minute for
 * a day is small enough to keep in memory and to rewrite to disk whole, and
 * coarse enough that a long session still fits on one screen.
 *
 * All-time peaks are tracked separately so a single spike is never lost to
 * averaging.
 */

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "ps5tm.h"

#define HISTORY_PATH      PS5TM_DATA_DIR "/history.csv"
#define HISTORY_SLOTS     1440            /* one day at one sample a minute */
#define HISTORY_PERIOD_MS 60000
#define HISTORY_FLUSH_MS  300000          /* rewrite the file every 5 min   */

typedef struct {
  uint64_t t_ms;
  int16_t  cpu_c, soc_c;
  int16_t  fan_pct;
  int16_t  target_c;
} sample_t;

static sample_t       g_ring[HISTORY_SLOTS];
static unsigned       g_count = 0, g_head = 0;
static uint64_t       g_last_sample_ms = 0, g_last_flush_ms = 0;
static int            g_dirty = 0;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

/* Peaks survive the ring: a spike that scrolled out is still worth knowing. */
static int      g_peak_cpu = -1, g_peak_soc = -1, g_peak_fan = -1;
static uint64_t g_peak_cpu_at = 0;


static void
push(const sample_t *s) {
  g_ring[g_head] = *s;
  g_head = (g_head + 1) % HISTORY_SLOTS;
  if(g_count < HISTORY_SLOTS) g_count++;
  g_dirty = 1;
}


/* Whether the last attempt to write the file failed, so that a full drive is
   reported once and not every five minutes. */
static int g_flush_failed;

/* Rewrites the whole file. The old one is replaced only by a new one that got
 * all the way through — flush, stream error flag and close, every one of which
 * can be where a full drive shows up — and stays as it is otherwise, with the
 * data kept in memory for the next flush.
 *
 * No fsync, unlike the settings file: the regular flush runs on the fan
 * thread, and an fsync on a drive that a copy job has busy can take seconds. */
static void
flush_locked(void) {
  if(!g_dirty) return;

  mkdir(PS5TM_DATA_DIR, 0755);

  char tmp[sizeof(HISTORY_PATH) + 8];
  snprintf(tmp, sizeof(tmp), "%s.tmp", HISTORY_PATH);

  int ok  = 0;
  int eno = 0;
  FILE *f = fopen(tmp, "w");
  if(!f) {
    eno = errno;
  } else {
    fprintf(f, "# t_ms,cpu_c,soc_c,fan_pct,target_c\n");
    fprintf(f, "# peak %d %d %d %llu\n", g_peak_cpu, g_peak_soc, g_peak_fan,
            (unsigned long long)g_peak_cpu_at);

    unsigned start = (g_count == HISTORY_SLOTS) ? g_head : 0;
    for(unsigned i = 0; i < g_count; i++) {
      const sample_t *s = &g_ring[(start + i) % HISTORY_SLOTS];
      fprintf(f, "%llu,%d,%d,%d,%d\n", (unsigned long long)s->t_ms,
              s->cpu_c, s->soc_c, s->fan_pct, s->target_c);
    }

    ok  = (fflush(f) == 0 && !ferror(f));
    eno = errno;
    if(fclose(f) != 0 && ok) { ok = 0; eno = errno; }
    if(ok && rename(tmp, HISTORY_PATH) != 0) { ok = 0; eno = errno; }
    if(!ok) unlink(tmp);
  }

  if(ok) {
    g_dirty        = 0;
    g_flush_failed = 0;
  } else if(!g_flush_failed) {
    g_flush_failed = 1;
    PS5TM_WARN("history_save_failed",
               "Der Verlauf konnte nicht gespeichert werden (Fehler %d: %s) – "
               "die vorhandene Datei bleibt unverändert, im Speicher läuft er "
               "weiter.", eno, strerror(eno));
  }
}


void
ps5tm_history_load(void) {
  FILE *f = fopen(HISTORY_PATH, "r");
  if(!f) return;

  uint64_t keep_ms = 0;
  {
    ps5tm_config_lock();
    unsigned days = g_config.telemetry_retention_days;
    ps5tm_config_unlock();
    if(days > 0)
      keep_ms = (uint64_t)days * 24ull * 3600ull * 1000ull;
  }
  uint64_t cutoff = keep_ms ? (ps5tm_now_ms() - keep_ms) : 0;

  pthread_mutex_lock(&g_lock);
  char line[128];
  while(fgets(line, sizeof(line), f)) {
    if(line[0] == '#') {
      int pc, ps, pf; unsigned long long at;
      if(sscanf(line, "# peak %d %d %d %llu", &pc, &ps, &pf, &at) == 4) {
        g_peak_cpu = pc; g_peak_soc = ps; g_peak_fan = pf;
        g_peak_cpu_at = at;
      }
      continue;
    }
    sample_t s;
    unsigned long long t;
    int cpu, soc, fan, tgt;
    if(sscanf(line, "%llu,%d,%d,%d,%d", &t, &cpu, &soc, &fan, &tgt) != 5)
      continue;
    if(cutoff && t < cutoff) continue;
    s.t_ms     = t;
    s.cpu_c    = (int16_t)cpu;
    s.soc_c    = (int16_t)soc;
    s.fan_pct  = (int16_t)fan;
    s.target_c = (int16_t)tgt;
    push(&s);
  }
  g_dirty = 0;      /* what we just read is what is on disk */
  pthread_mutex_unlock(&g_lock);
  fclose(f);

  PS5TM_INFO("history_loaded", "%u gespeicherte Messwerte geladen.", g_count);
}


void
ps5tm_history_tick(int cpu_c, int cpu_valid, int soc_c, int soc_valid,
                   int fan_pct, int fan_valid, int target_c) {
  uint64_t now = ps5tm_now_ms();

  pthread_mutex_lock(&g_lock);

  /* Peaks are updated on every tick, not just when a sample is stored. */
  if(cpu_valid && cpu_c > g_peak_cpu) { g_peak_cpu = cpu_c; g_peak_cpu_at = now; g_dirty = 1; }
  if(soc_valid && soc_c > g_peak_soc) { g_peak_soc = soc_c; g_dirty = 1; }
  if(fan_valid && fan_pct > g_peak_fan) { g_peak_fan = fan_pct; g_dirty = 1; }

  if(g_last_sample_ms == 0 || now - g_last_sample_ms >= HISTORY_PERIOD_MS) {
    sample_t s;
    s.t_ms     = now;
    s.cpu_c    = cpu_valid ? (int16_t)cpu_c   : -1;
    s.soc_c    = soc_valid ? (int16_t)soc_c   : -1;
    s.fan_pct  = fan_valid ? (int16_t)fan_pct : -1;
    s.target_c = (int16_t)target_c;
    push(&s);
    g_last_sample_ms = now;
  }

  if(g_last_flush_ms == 0) g_last_flush_ms = now;
  if(now - g_last_flush_ms >= HISTORY_FLUSH_MS) {
    flush_locked();
    g_last_flush_ms = now;
  }

  pthread_mutex_unlock(&g_lock);
}


unsigned
ps5tm_history_snapshot(ps5tm_history_entry_t *out, unsigned max,
                       ps5tm_history_peaks_t *peaks) {
  pthread_mutex_lock(&g_lock);

  if(peaks) {
    peaks->cpu_c    = g_peak_cpu;
    peaks->soc_c    = g_peak_soc;
    peaks->fan_pct  = g_peak_fan;
    peaks->cpu_at_ms = g_peak_cpu_at;
  }

  unsigned n     = g_count < max ? g_count : max;
  unsigned start = (g_count == HISTORY_SLOTS)
                     ? (g_head + (g_count - n)) % HISTORY_SLOTS
                     : (g_count - n);

  for(unsigned i = 0; i < n; i++) {
    const sample_t *s = &g_ring[(start + i) % HISTORY_SLOTS];
    out[i].t_ms     = s->t_ms;
    out[i].cpu_c    = s->cpu_c;
    out[i].soc_c    = s->soc_c;
    out[i].fan_pct  = s->fan_pct;
    out[i].target_c = s->target_c;
  }

  pthread_mutex_unlock(&g_lock);
  return n;
}


void
ps5tm_history_reset(void) {
  pthread_mutex_lock(&g_lock);
  g_count = g_head = 0;
  g_peak_cpu = g_peak_soc = g_peak_fan = -1;
  g_peak_cpu_at = 0;
  g_dirty = 1;
  flush_locked();
  pthread_mutex_unlock(&g_lock);
  PS5TM_INFO("history_reset", "Verlauf zurückgesetzt.");
}
