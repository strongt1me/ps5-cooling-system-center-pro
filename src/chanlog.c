/* Records every SoC sensor channel alongside load and fan speed, so the
 * channels can be told apart instead of guessed at.
 *
 * ── Why this exists
 *
 * platform.c already sweeps all PS5TM_SOC_CHANNELS and keeps them in
 * soc_raw[]. Three are labelled; the rest are read and thrown away, because
 * nothing says what they measure and a dashboard full of "Kanal 5" helps
 * nobody. The data was there, the interpretation was missing.
 *
 * The accepted position — etaHEN's own source says so, and so did this
 * project's notes — is that the PS5 exposes no graphics-side temperature.
 * The voltage regulator's datasheet disagrees: the XDPE14286A has **two**
 * temperature inputs, pin 50 TSEN1 marked "VGFX TEMP." (graphics rail) and
 * pin 51 TSEN2 marked "VCORE TEMP." (CPU rail). If two of these channels are
 * those rails, a graphics-side reading does exist — just not where people
 * looked for it.
 *
 * That is a question a recording answers and an argument does not. Channels
 * that move together are one sensor seen twice; a channel that leads during
 * graphics work and lags during compute is a separate one.
 *
 * ── Deliberately separate from history.c
 *
 * The 24-hour history is proven and holds the user's data. This is an
 * experiment with a format that will change as soon as it has been read
 * once, so it lives in its own buffer, in memory only, and touches nothing
 * that already works. Losing it on restart is acceptable — a gaming session
 * is all it needs to cover.
 */

#include <pthread.h>
#include <stdint.h>
#include <string.h>

#include "ps5tm.h"

/* 900 samples at one every five seconds — 75 minutes. Since the fan
   controller's 28-byte block joined each entry that is roughly 50 KB instead
   of 29; still in memory only, still expendable on restart. */
#define CHANLOG_INTERVAL_SEC 5

static ps5tm_chan_sample_t g_ring[PS5TM_CHANLOG_CAPACITY];
static unsigned            g_count;
static unsigned            g_head;
static uint64_t            g_first_ms;
static uint64_t            g_last_ms;
static pthread_mutex_t     g_lock = PTHREAD_MUTEX_INITIALIZER;


void
ps5tm_chanlog_sample(const ps5tm_sensors_t *s) {
  if(!s) return;

  uint64_t now = ps5tm_now_ms();

  pthread_mutex_lock(&g_lock);

  /* Called once a second from the fan worker, which already has the sensors
     in hand — no extra reads. Decimated here rather than there so the control
     loop keeps its own cadence. */
  if(g_last_ms && now - g_last_ms < (uint64_t)CHANLOG_INTERVAL_SEC * 1000) {
    pthread_mutex_unlock(&g_lock);
    return;
  }
  if(!g_first_ms) g_first_ms = now;
  g_last_ms = now;
  pthread_mutex_unlock(&g_lock);

  /* The fan controller's block is read here, deliberately outside the lock:
     it is an open/ioctl/close and has no business inside a critical section.
     Dropping the lock between the decimation check and the append is safe
     because only the fan worker calls this — the timestamp above has already
     claimed this slot, so a second caller could not slip in even if there
     were one.

     A failed read is recorded as such rather than skipping the sample: the
     sensors are still worth having, and a gap in this column is itself
     information. */
  unsigned char icc[PS5TM_ICC_FAN_CONFIG_SIZE];
  int icc_ok = (ps5tm_platform_read_fan_config(icc, NULL) == 0);

  ps5tm_chan_sample_t e;
  memset(&e, 0, sizeof(e));
  e.t_s = (uint32_t)((now - g_first_ms) / 1000);

  e.icc_valid = (int8_t)(icc_ok ? 1 : 0);
  if(icc_ok) memcpy(e.icc, icc, sizeof(e.icc));

  for(int i = 0; i < PS5TM_SOC_CHANNELS; i++)
    e.ch[i] = s->soc_raw_valid[i] ? (int16_t)s->soc_raw[i] : (int16_t)-1;

  e.cpu_c    = s->cpu_valid ? (int16_t)s->cpu_c : (int16_t)-1;
  e.load_x10 = s->cpu_load_valid
                 ? (int16_t)(s->cpu_load_pct * 10.0) : (int16_t)-1;
  e.fan_pct  = s->fan_duty_valid ? (int16_t)s->fan_duty_pct : (int16_t)-1;

  /* The rails beside the channels are what finally separates GPU from CPU
     heat: a game loads both, but not in a fixed ratio, and over a session a
     channel either follows the GPU rail or it does not. */
  double gpu = ps5tm_power_sum_w(&s->power, 0, 1);
  double cpu = ps5tm_power_sum_w(&s->power, 2, 3);
  double mem = ps5tm_power_sum_w(&s->power, 4, 7);
  e.gpu_w10 = gpu >= 0 ? (int16_t)(gpu * 10.0 + 0.5) : (int16_t)-1;
  e.cpu_w10 = cpu >= 0 ? (int16_t)(cpu * 10.0 + 0.5) : (int16_t)-1;
  e.mem_w10 = mem >= 0 ? (int16_t)(mem * 10.0 + 0.5) : (int16_t)-1;
  e.fps10   = s->fps.valid ? (int16_t)(s->fps.fps * 10.0 + 0.5) : (int16_t)-1;
  e.aux_valid = (int8_t)(s->power.valid ? 1 : 0);
  if(s->power.valid) memcpy(e.aux_c, s->power.aux_c, sizeof(e.aux_c));

  pthread_mutex_lock(&g_lock);
  g_ring[g_head] = e;
  g_head = (g_head + 1) % PS5TM_CHANLOG_CAPACITY;
  if(g_count < PS5TM_CHANLOG_CAPACITY) g_count++;
  pthread_mutex_unlock(&g_lock);
}


unsigned
ps5tm_chanlog_snapshot(ps5tm_chan_sample_t *out, unsigned max) {
  if(!out || max == 0) return 0;

  pthread_mutex_lock(&g_lock);

  unsigned n = g_count < max ? g_count : max;
  /* Oldest first. Once wrapped the oldest sits at g_head. */
  unsigned start = (g_count == PS5TM_CHANLOG_CAPACITY)
                     ? (g_head + (g_count - n)) % PS5TM_CHANLOG_CAPACITY
                     : (g_count - n);
  for(unsigned i = 0; i < n; i++)
    out[i] = g_ring[(start + i) % PS5TM_CHANLOG_CAPACITY];

  pthread_mutex_unlock(&g_lock);
  return n;
}
