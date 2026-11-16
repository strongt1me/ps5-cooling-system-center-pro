/* PS5 Cooling & System Center - Pro: the recording of the fan regulation (16.11.2026).
 *
 * The fan loop writes one line a second into a ring of the last 30 minutes: the temperatures, what the controller asked for, what the fan did,
 * the threshold, the target and the profile. On 16.11.2026 a question about the fan had to be answered from a one-minute history and a few
 * log lines; with this ring (GET /api/v1/fan/trace as a table, POST /api/v1/fan/trace/save as a file in the data folder) the answer is in the
 * numbers. Memory only: nothing is written until somebody asks. */
#include "ps5tm.h"

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define FT_RING 1800                    /* seconds kept */

typedef struct {
  int64_t  wall_s;                      /* seconds since 1970 (the console's clock), 0 = empty */
  int16_t  cpu_c, soc_c;
  int16_t  avg_c10;                     /* the controller's average, tenths of a degree */
  int16_t  wish, fan_x10;               /* wish in percent, real speed in tenths of a percent (-1 = not read) */
  int16_t  thr_c, target_c;
  uint8_t  profile, game, safety, auto_mode;
} ft_row_t;

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static ft_row_t       *g_ring;
static unsigned        g_next;
static unsigned        g_count;

void
ps5tm_fantrace_add(int cpu_c, int soc_c, int avg_c100, int wish, int fan_x10, int thr_c, int target_c, int profile, int game, int safety, int automatic) {
  pthread_mutex_lock(&g_lock);
  if(!g_ring) g_ring = calloc(FT_RING, sizeof(*g_ring));
  if(g_ring) {
    ft_row_t *r = &g_ring[g_next];
    r->wall_s = (int64_t)(ps5tm_now_ms() / 1000);
    r->cpu_c = (int16_t)cpu_c;
    r->soc_c = (int16_t)soc_c;
    r->avg_c10 = (int16_t)(avg_c100 < 0 ? -1 : (avg_c100 + 5) / 10);
    r->wish = (int16_t)wish;
    r->fan_x10 = (int16_t)fan_x10;
    r->thr_c = (int16_t)thr_c;
    r->target_c = (int16_t)target_c;
    r->profile = (uint8_t)profile;
    r->game = (uint8_t)(game != 0);
    r->safety = (uint8_t)(safety != 0);
    r->auto_mode = (uint8_t)(automatic != 0);
    g_next = (g_next + 1) % FT_RING;
    if(g_count < FT_RING) g_count++;
  }
  pthread_mutex_unlock(&g_lock);
}

/* The ring as a table: a header and one line a second, oldest first. The caller frees it. NULL without memory. */
char *
ps5tm_fantrace_csv(size_t *len_out) {
  static const char *const prof[] = { "leise", "ausgewogen", "kuehl" };
  pthread_mutex_lock(&g_lock);
  unsigned n = g_count;
  size_t cap = 256 + (size_t)n * 72;
  char *out = malloc(cap);
  if(!out) { pthread_mutex_unlock(&g_lock); return NULL; }
  size_t used = (size_t)snprintf(out, cap,
      "# Regelaufzeichnung der Lüfterregelung (eine Zeile je Sekunde, älteste zuerst)\n"
      "# zeit_utc;cpu_c;soc_c;mittel_c;wunsch_pct;luefter_pct;schwelle_c;ziel_c;profil;spiel;sicherheit;automatik\n");
  unsigned start = (g_next + FT_RING - n) % FT_RING;
  for(unsigned k = 0; k < n && used + 80 < cap; k++) {
    const ft_row_t *r = &g_ring[(start + k) % FT_RING];
    time_t t = (time_t)r->wall_s;
    struct tm tmv;
    char hms[16] = "--:--:--";
    if(gmtime_r(&t, &tmv)) strftime(hms, sizeof(hms), "%H:%M:%S", &tmv);   /* UTC: the page shows its own clock; the file says it */
    used += (size_t)snprintf(out + used, cap - used, "%s;%d;%d;%d.%d;%d;%d.%d;%d;%d;%s;%d;%d;%d\n", hms, r->cpu_c, r->soc_c,
                             r->avg_c10 / 10, r->avg_c10 < 0 ? 0 : r->avg_c10 % 10, r->wish, r->fan_x10 / 10, r->fan_x10 < 0 ? 0 : r->fan_x10 % 10,
                             r->thr_c, r->target_c, r->profile < 3 ? prof[r->profile] : "?", r->game, r->safety, r->auto_mode);
  }
  pthread_mutex_unlock(&g_lock);
  if(len_out) *len_out = used;
  return out;
}

/* A file in the data folder: /data/PS5-Cooling-Center/regelaufzeichnung-<date>_<time>.csv (the clock of the console). The name in name, 0 or an errno. */
int
ps5tm_fantrace_save(char *name, size_t name_len) {
  size_t len = 0;
  char *csv = ps5tm_fantrace_csv(&len);
  if(!csv) return ENOMEM;
  time_t now = (time_t)(ps5tm_now_ms() / 1000);
  struct tm tmv;
  char stamp[32] = "unbekannt";
  if(gmtime_r(&now, &tmv)) strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &tmv);
  char nm[96], path[160];
  snprintf(nm, sizeof(nm), "regelaufzeichnung-%s.csv", stamp);
  snprintf(path, sizeof(path), "%s/%s", PS5TM_DATA_DIR, nm);
  mkdir(PS5TM_DATA_DIR, 0777);
  FILE *f = fopen(path, "wb");
  int rc = 0;
  if(!f) rc = errno ? errno : EIO;
  else {
    if(fwrite(csv, 1, len, f) != len) rc = EIO;
    if(fclose(f) != 0 && !rc) rc = EIO;
  }
  free(csv);
  if(!rc && name) snprintf(name, name_len, "%s", nm);
  return rc;
}
