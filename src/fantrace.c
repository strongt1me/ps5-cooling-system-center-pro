/* PS5 Cooling & System Center - Pro: the recording of the fan regulation (16.11.2026).
 *
 * The fan loop writes one line a second into a ring of the last 30 minutes: the temperatures, what the controller asked for, what the fan did,
 * the threshold, the target and the profile. On 16.11.2026 a question about the fan had to be answered from a one-minute history and a few
 * log lines; with this ring (GET /api/v1/fan/trace as a table, POST /api/v1/fan/trace/save as a file in the data folder) the answer is in the
 * numbers. Memory only: nothing is written until somebody asks. */
#include "ps5tm.h"

#include <dirent.h>
#include <errno.h>
#include <pthread.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define FT_RING 1800                    /* seconds kept */
#define FT_SAVE_GAP_MS 10000u           /* a file at most every 10 s ... */
#define FT_KEEP_FILES  20               /* ... and the newest 20 stay (audit 16.11.2026: a loop of requests filled the data folder) */

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
  /* A copy under the lock, the formatting without it: the fan loop adds a row every second and never waits for 1800 lines of text. */
  ft_row_t *rows = malloc(FT_RING * sizeof(*rows));
  if(!rows) return NULL;
  pthread_mutex_lock(&g_lock);
  unsigned n = g_count;
  if(n) {
    unsigned first = (g_next + FT_RING - n) % FT_RING;
    for(unsigned k = 0; k < n; k++) rows[k] = g_ring[(first + k) % FT_RING];
  }
  pthread_mutex_unlock(&g_lock);
  size_t cap = 256 + (size_t)n * 80;
  char *out = malloc(cap);
  if(!out) { free(rows); return NULL; }
  size_t used = (size_t)snprintf(out, cap,
      "# Regelaufzeichnung der Lüfterregelung (eine Zeile je Sekunde, älteste zuerst)\n"
      "# zeit_utc;cpu_c;soc_c;mittel_c;wunsch_pct;luefter_pct;schwelle_c;ziel_c;profil;spiel;sicherheit;automatik\n");
  for(unsigned k = 0; k < n && used + 90 < cap; k++) {
    const ft_row_t *r = &rows[k];
    time_t t = (time_t)r->wall_s;
    struct tm tmv;
    char hms[16] = "--:--:--";
    if(gmtime_r(&t, &tmv)) strftime(hms, sizeof(hms), "%H:%M:%S", &tmv);   /* UTC: the page shows its own clock; the file says it */
    char avg[16] = "", fan[16] = "";                       /* a value that was not read is an empty field, not "0.0" */
    if(r->avg_c10 >= 0) snprintf(avg, sizeof(avg), "%d.%d", r->avg_c10 / 10, r->avg_c10 % 10);
    if(r->fan_x10 >= 0) snprintf(fan, sizeof(fan), "%d.%d", r->fan_x10 / 10, r->fan_x10 % 10);
    used += (size_t)snprintf(out + used, cap - used, "%s;%d;%d;%s;%d;%s;%d;%d;%s;%d;%d;%d\n", hms, r->cpu_c, r->soc_c, avg, r->wish, fan,
                             r->thr_c, r->target_c, r->profile < 3 ? prof[r->profile] : "?", r->game, r->safety, r->auto_mode);
  }
  free(rows);
  if(len_out) *len_out = used;
  return out;
}

/* A file in the data folder: /data/PS5-Cooling-Center/regelaufzeichnung-<date>_<time>.csv (the clock of the console). The name in name, 0 or an errno. */
/* The recordings in the data folder, newest FT_KEEP_FILES left: the names carry the date, so the smallest is the oldest. */
static void
prune_files(const char *keep) {
  for(int round = 0; round < 64; round++) {
    DIR *d = opendir(PS5TM_DATA_DIR);
    if(!d) return;
    int n = 0;
    char oldest[96] = "";
    struct dirent *e;
    while((e = readdir(d)) != NULL) {
      size_t l = strlen(e->d_name);
      if(l < 22 || l >= sizeof(oldest) || strncmp(e->d_name, "regelaufzeichnung-", 18) || strcmp(e->d_name + l - 4, ".csv")) continue;
      n++;
      if(keep && !strcmp(e->d_name, keep)) continue;           /* the file that was just written stays, whatever the clock says (a clock set back) */
      if(!oldest[0] || strcmp(e->d_name, oldest) < 0) snprintf(oldest, sizeof(oldest), "%s", e->d_name);
    }
    closedir(d);
    if(n <= FT_KEEP_FILES || !oldest[0]) return;
    char p[160];
    snprintf(p, sizeof(p), "%s/%s", PS5TM_DATA_DIR, oldest);
    if(unlink(p) != 0) return;
  }
}

int
ps5tm_fantrace_save(char *name, size_t name_len) {
  static uint64_t last_ms;
  const uint64_t now_ms = ps5tm_now_ms();
  pthread_mutex_lock(&g_lock);
  const int too_soon = last_ms && now_ms >= last_ms && now_ms - last_ms < FT_SAVE_GAP_MS;
  if(!too_soon) last_ms = now_ms;
  pthread_mutex_unlock(&g_lock);
  if(too_soon) return EAGAIN;
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
  if(rc) unlink(path);                                      /* a half file on a full disk is no recording */
  if(!rc && name) snprintf(name, name_len, "%s", nm);
  if(!rc) prune_files(nm);
  return rc;
}
