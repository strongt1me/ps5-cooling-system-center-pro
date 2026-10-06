/* Does the cooling still work as well as it did? A long-term answer.
 *
 * A PS5 gets slower at shedding heat as dust collects and paste dries. That
 * shows up long before anything breaks, and nothing tells the owner: the fan
 * simply runs a little faster each month, too gradually to notice.
 *
 * ── Why a plain temperature history cannot answer this
 *
 * "It was 72 °C in March and it is 76 °C now" says nothing — a different game
 * at a different room temperature explains that easily. The comparison only
 * holds if the console is doing comparable work and the fan is turning at a
 * comparable speed. So only samples inside a fixed operating window are
 * counted, and everything outside it is discarded:
 *
 *     load 25..75 %      fan 20..50 %
 *
 * Within that window the reading answers one question: how hot does this
 * console get for a given amount of work and a given amount of airflow. If
 * that figure climbs over months, the cooling has degraded — there is no
 * other explanation left, because the two things that would otherwise explain
 * it are held constant.
 *
 * Fan speed is the right thing to hold constant precisely because it is a
 * consequence: as cooling degrades the controller compensates by spinning
 * faster, which would hide the drift in a naive temperature average.
 *
 * ── What is stored
 *
 * One line per calendar week, at most a year of them. Not the raw samples —
 * those are the 24-hour history's job, and keeping a year of them would be
 * megabytes to answer a question that needs eight numbers.
 *
 * The temperature used is the hottest chip-region sensor, which since 1.17.0
 * is what soc_c means; channel 0 was up to 17 °C too cool and far too slow to
 * see anything (see platform.c).
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

#define THERMAL_PATH     PS5TM_DATA_DIR "/thermal-health.csv"
/* Once a minute, not once every ten.
 *
 * Ten minutes was the first choice, by analogy with the 24-hour history — and
 * it cost real data immediately. A payload cannot be updated in place: every
 * new version is a new process, and everything still sitting in memory is
 * gone. On 01.08.2026 that lost 28 of 141 samples, which dropped the week
 * below the threshold that makes it usable at all.
 *
 * Since the console has to be re-sent the payload after every restart, that
 * loss would recur constantly. The file holds at most 52 short lines, so
 * writing it every minute costs nothing worth counting. */
#define THERMAL_FLUSH_MS 60000
#define SAMPLE_PERIOD_MS 5000              /* one sample per 5 s is plenty  */

/* The operating window. Wide enough to catch ordinary play, narrow enough
   that what is inside is genuinely comparable. */
#define WIN_LOAD_MIN 25
#define WIN_LOAD_MAX 75
#define WIN_FAN_MIN  20
#define WIN_FAN_MAX  50

/* A week with fewer samples than this is kept but not used for a verdict —
   ten minutes of comparable operation is too thin to trust. */
#define WEEK_MIN_SAMPLES 120

#define MS_PER_WEEK 604800000ull

typedef struct {
  uint32_t week;          /* whole weeks since the epoch */
  uint32_t samples;
  uint64_t sum_temp;      /* °C, summed */
  uint64_t sum_fan;       /* %,  summed */
  uint64_t sum_load10;    /* % × 10, summed */
  uint64_t sum_activity;  /* 0/1 foreground flag, summed */
} week_acc_t;

static week_acc_t      g_weeks[PS5TM_THERMAL_WEEKS];
static unsigned        g_count;
static uint64_t        g_last_sample_ms;
static uint64_t        g_last_flush_ms;
static int             g_dirty;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;


/* Whether the last attempt to write the file failed, so that a full drive is
   reported once and not once a minute. */
static int g_flush_failed;

/* Rewrites the whole file. The old one is replaced only by a new one that got
 * all the way through: a full drive shows up in the stream's error flag or in
 * fclose(), not necessarily in fflush(), and renaming a file that came up
 * short over the good one threw away weeks of the very record this exists to
 * keep (it needs four of them before it says anything). On any failure the old
 * file stays as it is and the data stays in memory, to be tried again at the
 * next flush.
 *
 * No fsync, unlike the settings file: this runs on the fan thread, and an
 * fsync on a drive that a copy job has busy can take seconds. */
static void
flush_locked(void) {
  if(!g_dirty) return;

  mkdir(PS5TM_DATA_DIR, 0755);

  char tmp[sizeof(THERMAL_PATH) + 8];
  snprintf(tmp, sizeof(tmp), "%s.tmp", THERMAL_PATH);

  int ok  = 0;
  int eno = 0;
  FILE *f = fopen(tmp, "w");
  if(!f) {
    eno = errno;
  } else {
    fprintf(f, "# week,samples,sum_temp,sum_fan,sum_load10,sum_activity\n");
    fprintf(f, "# Fenster: Last %d-%d%%, Lüfter %d-%d%%\n",
            WIN_LOAD_MIN, WIN_LOAD_MAX, WIN_FAN_MIN, WIN_FAN_MAX);
    for(unsigned i = 0; i < g_count; i++)
          fprintf(f, "%u,%u,%llu,%llu,%llu,%llu\n",
              g_weeks[i].week, g_weeks[i].samples,
              (unsigned long long)g_weeks[i].sum_temp,
              (unsigned long long)g_weeks[i].sum_fan,
            (unsigned long long)g_weeks[i].sum_load10,
            (unsigned long long)g_weeks[i].sum_activity);

    ok  = (fflush(f) == 0 && !ferror(f));
    eno = errno;
    if(fclose(f) != 0 && ok) { ok = 0; eno = errno; }
    if(ok && rename(tmp, THERMAL_PATH) != 0) { ok = 0; eno = errno; }
    if(!ok) unlink(tmp);
  }

  if(ok) {
    g_dirty        = 0;
    g_flush_failed = 0;
  } else if(!g_flush_failed) {
    g_flush_failed = 1;
    PS5TM_WARN("thermal_save_failed",
               "Kühlleistungs-Aufzeichnung konnte nicht gespeichert werden "
               "(Fehler %d: %s) – die vorhandene Datei bleibt unverändert, im "
               "Speicher läuft die Aufzeichnung weiter.", eno, strerror(eno));
  }
}


void
ps5tm_thermal_load(void) {
  FILE *f = fopen(THERMAL_PATH, "r");
  if(!f) return;

  uint32_t keep_weeks = 0;
  {
    ps5tm_config_lock();
    unsigned days = g_config.telemetry_retention_days;
    ps5tm_config_unlock();
    if(days > 0) keep_weeks = (days + 6) / 7;
    if(keep_weeks < 4) keep_weeks = 4;
  }
  uint32_t now_week = (uint32_t)(ps5tm_now_ms() / MS_PER_WEEK);
  uint32_t cutoff_week = (keep_weeks && now_week > keep_weeks)
    ? (now_week - keep_weeks)
    : 0;

  pthread_mutex_lock(&g_lock);
  g_count = 0;

  char line[160];
  while(fgets(line, sizeof(line), f) && g_count < PS5TM_THERMAL_WEEKS) {
    if(line[0] == '#') continue;
    week_acc_t w;
    memset(&w, 0, sizeof(w));
    unsigned long long st = 0, sf = 0, sl = 0, sa = 0;
    int got = sscanf(line, "%u,%u,%llu,%llu,%llu,%llu",
             &w.week, &w.samples, &st, &sf, &sl, &sa);
    if(got != 5 && got != 6) continue;
    if(cutoff_week && w.week < cutoff_week) continue;
    w.sum_temp   = st;
    w.sum_fan    = sf;
    w.sum_load10 = sl;
    w.sum_activity = (got == 6) ? sa : 0;
    g_weeks[g_count++] = w;
  }
  g_dirty = 0;
  pthread_mutex_unlock(&g_lock);
  fclose(f);

  if(g_count)
    PS5TM_INFO("thermal_health_loaded",
               "Kühlleistungs-Aufzeichnung geladen: %u Woche(n).", g_count);
}


void
ps5tm_thermal_sample(int temp_c, int temp_valid,
                     double load_pct, int load_valid,
                     int fan_pct, int fan_valid,
                     int activity_fg) {
  if(!temp_valid || !load_valid || !fan_valid) return;
  if(temp_c <= 0 || temp_c > 130) return;

  /* Outside the window the sample says nothing comparable — drop it. */
  if(load_pct < WIN_LOAD_MIN || load_pct > WIN_LOAD_MAX) return;
  if(fan_pct  < WIN_FAN_MIN  || fan_pct  > WIN_FAN_MAX)  return;

  uint64_t now = ps5tm_now_ms();
  if(!now) return;

  pthread_mutex_lock(&g_lock);

  if(g_last_sample_ms && now - g_last_sample_ms < SAMPLE_PERIOD_MS) {
    pthread_mutex_unlock(&g_lock);
    return;
  }
  g_last_sample_ms = now;

  uint32_t week = (uint32_t)(now / MS_PER_WEEK);

  week_acc_t *slot = NULL;
  if(g_count && g_weeks[g_count - 1].week == week) {
    slot = &g_weeks[g_count - 1];
  } else {
    if(g_count == PS5TM_THERMAL_WEEKS) {
      /* A year is enough; drop the oldest week to make room. */
      memmove(&g_weeks[0], &g_weeks[1],
              sizeof(g_weeks[0]) * (PS5TM_THERMAL_WEEKS - 1));
      g_count--;
    }
    slot = &g_weeks[g_count++];
    memset(slot, 0, sizeof(*slot));
    slot->week = week;
  }

  slot->samples++;
  slot->sum_temp   += (uint64_t)temp_c;
  slot->sum_fan    += (uint64_t)fan_pct;
  slot->sum_load10 += (uint64_t)(load_pct * 10.0);
  slot->sum_activity += activity_fg ? 1ull : 0ull;
  g_dirty = 1;

  if(!g_last_flush_ms) g_last_flush_ms = now;
  if(now - g_last_flush_ms >= THERMAL_FLUSH_MS) {
    g_last_flush_ms = now;
    flush_locked();
  }

  pthread_mutex_unlock(&g_lock);
}


unsigned
ps5tm_thermal_snapshot(ps5tm_thermal_week_t *out, unsigned max) {
  if(!out || max == 0) return 0;

  pthread_mutex_lock(&g_lock);
  unsigned n = g_count < max ? g_count : max;
  unsigned first = g_count - n;
  for(unsigned i = 0; i < n; i++) {
    const week_acc_t *w = &g_weeks[first + i];
    out[i].week      = w->week;
    out[i].samples   = w->samples;
    out[i].temp_c10  = w->samples ? (int)((w->sum_temp   * 10) / w->samples) : -1;
    out[i].fan_pct10 = w->samples ? (int)((w->sum_fan    * 10) / w->samples) : -1;
    /* sum_load10 holds tenths, so dividing by the count gives tenths too —
       the second division is what turns it back into a percentage. Without
       it the console reported a mean load of 429 %. */
    out[i].load_pct  = w->samples
                         ? (int)(w->sum_load10 / w->samples / 10) : -1;
    out[i].activity_pct = w->samples
                            ? (int)((w->sum_activity * 100ull) / w->samples)
                            : 0;
    out[i].usable    = (w->samples >= WEEK_MIN_SAMPLES);
  }
  pthread_mutex_unlock(&g_lock);
  return n;
}


/* Baseline versus now, in tenths of a degree. Returns 0 when there is not
 * enough to compare yet — deliberately, because a verdict from two thin weeks
 * would be worse than none. The baseline is the mean of up to the first three
 * usable weeks, so one unusual week cannot set it. */
int
ps5tm_thermal_verdict(int *delta_c10, unsigned *weeks_usable,
                      int *baseline_c10, int *current_c10) {
  ps5tm_thermal_week_t w[PS5TM_THERMAL_WEEKS];
  unsigned n = ps5tm_thermal_snapshot(w, PS5TM_THERMAL_WEEKS);

  long base_sum = 0; unsigned base_n = 0;
  int  cur = -1;
  unsigned usable = 0;

  for(unsigned i = 0; i < n; i++) {
    if(!w[i].usable) continue;
    if(w[i].activity_pct < 20) continue;
    usable++;
    if(base_n < 3) { base_sum += w[i].temp_c10; base_n++; }
    cur = w[i].temp_c10;
  }

  /* If there are not enough game-active weeks yet, fall back to the classic
     verdict path instead of returning nothing forever. */
  if(usable < 4) {
    base_sum = 0;
    base_n   = 0;
    cur      = -1;
    usable   = 0;
    for(unsigned i = 0; i < n; i++) {
      if(!w[i].usable) continue;
      usable++;
      if(base_n < 3) { base_sum += w[i].temp_c10; base_n++; }
      cur = w[i].temp_c10;
    }
  }

  if(weeks_usable) *weeks_usable = usable;
  /* At least four usable weeks, and the current one must not be part of the
     baseline, or the comparison is with itself. */
  if(usable < 4 || base_n == 0 || cur < 0) return -1;

  int base = (int)(base_sum / base_n);
  if(baseline_c10) *baseline_c10 = base;
  if(current_c10)  *current_c10  = cur;
  if(delta_c10)    *delta_c10    = cur - base;
  return 0;
}
