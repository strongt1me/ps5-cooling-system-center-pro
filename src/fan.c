/* Comfort-oriented fan regulation.
 *
 * The goal is a console that stays quiet and *sounds* steady, not one that
 * runs as cold as possible. Two layers do the work:
 *
 *   1. The comfort controller decides how fast the fan should turn. It works
 *      from a moving average rather than the live reading, ignores anything
 *      inside a deadband around the target temperature, weighs the trend more
 *      heavily than any single sample, and moves in 1-2 % steps. Speeding up
 *      is slow; slowing down is slower still. Above the safety limit all of
 *      that is dropped and the fan ramps hard.
 *
 *   2. The duty servo turns that wish into the only knob the hardware offers:
 *      an integer threshold temperature. One degree is worth roughly 5 % of
 *      fan speed, so the servo only moves when the measured duty has drifted
 *      outside a tolerance band. In practice the threshold changes by a
 *      single degree every few cycles, which is what makes the result
 *      inaudible.
 */

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "ps5tm.h"

/* ------------------------------------------------------------- servo tuning */

#define SERVO_TOLERANCE_PCT 4   /* accept this much duty error, then act    */
#define SERVO_INTERVAL_SEC  6   /* the fan needs time to reach a new speed  */
#define SERVO_STEP_MAX_C    3   /* threshold movement per servo correction  */
#define SERVO_IDLE_DUTY_PCT 5   /* below this the fan is left entirely alone*/

#define DUTY_EMA_NUM 1          /* light smoothing on the duty readback     */
#define DUTY_EMA_DEN 3

/* A trend must exceed this (hundredths of a degree across the window) before
   it counts as rising or falling rather than noise. */
#define TREND_SIGNIFICANT_C100 15

/* Event log thinning — the controller makes many small corrections. */
#define LOG_MIN_DELTA_C 2
#define LOG_MIN_GAP_SEC 30
#define RESUME_GAP_MS   20000ull

/* How often the controller is asked what it is actually holding.
 *
 * Measured on FW 12.00 on 07.09.2026, and this is why the watchdog exists at
 * all: when a game launched, the console reset the threshold to 91 °C — a
 * value this app never writes, its own ceiling being 80. Until then the
 * firmware reset was an assumption in a comment; now it has a number.
 *
 * The old answer was to blindly rewrite the threshold every 15 s, which left
 * the console up to 15 seconds on the firmware's setting — right at a game
 * launch, exactly when the load arrives. Two seconds is what fan_target 0.1
 * polls at in shipped form, so the cost is known to be bearable. */
#define WATCHDOG_INTERVAL_SEC 2
#define OVERRIDE_LOG_GAP_SEC 60

/* One failed read of the controller proves nothing: it is busy at exactly the
 * moments the firmware rewrites it, and the process can briefly lack the
 * identity it needs. The watchdog is therefore only given up after this many
 * failures in a row — and a given-up one is tried again now and then, because
 * "cannot be read right now" and "cannot be read on this firmware" look the
 * same from here and only one of them is permanent. */
#define READBACK_FAIL_LATCH 5
#define READBACK_RETRY_SEC  30

/* A moving average of temperatures that stopped arriving is a memory, not a
 * measurement. After this many passes in a row (one a second) without a single
 * valid reading the controller stops trusting it: the fan is held at no less
 * than BLIND_MIN_DUTY_PCT until readings come back. */
#define BLIND_AFTER_PASSES 10
#define BLIND_MIN_DUTY_PCT 70

/* The same for the fan-speed register: after this many passes without a valid
 * read the smoothed value is dropped, because a stale one makes the servo steer
 * by a speed the fan no longer has. */
#define DUTY_STALE_PASSES  10

#define SAMPLE_MAX 60           /* moving-average ring capacity, seconds    */


static ps5tm_snapshot_t g_snapshot;
static pthread_mutex_t  g_snapshot_lock = PTHREAD_MUTEX_INITIALIZER;
static atomic_int       g_started = 0;

/* Threshold the user pinned by hand; 0 = never pinned, so observe mode
   leaves the fan alone. */
static atomic_int g_manual_threshold_c = 0;

static int g_cpu_warning_logged = 0;
static int g_soc_warning_logged = 0;
static uint64_t g_warning_since_ms = 0;

/* Which per-title rule is currently overriding the global settings, so the
   dashboard can say so and the log only reports the transitions. */
static int  g_rule_active = 0;
static char g_active_rule_id[16] = {0};

const char *
ps5tm_fan_active_rule(void) {
  return g_active_rule_id;
}


/* ------------------------------------------------ threshold <-> duty helpers
 *
 * Only used to seed the servo and to describe a manual pin; the closed loop
 * corrects whatever these get wrong. Anchored at PS5TM_THRESHOLD_BASE_C (80),
 * not at the top of the range (91): a threshold above 80 simply means "no
 * extra speed", which is what 0 % stands for here.
 */

int
ps5tm_fan_duty_to_threshold(int duty_pct) {
  if(duty_pct < 0)   duty_pct = 0;
  if(duty_pct > 100) duty_pct = 100;

  const int span = PS5TM_THRESHOLD_BASE_C - PS5TM_THRESHOLD_MIN_C;
  return PS5TM_THRESHOLD_BASE_C - ((duty_pct * span) + 50) / 100;
}

int
ps5tm_fan_threshold_to_duty(int threshold_c) {
  const int span = PS5TM_THRESHOLD_BASE_C - PS5TM_THRESHOLD_MIN_C;
  if(threshold_c <= PS5TM_THRESHOLD_MIN_C) return 100;
  if(threshold_c >= PS5TM_THRESHOLD_BASE_C) return 0;
  return ((PS5TM_THRESHOLD_BASE_C - threshold_c) * 100 + span / 2) / span;
}

int
ps5tm_fan_curve_eval(const ps5tm_config_t *cfg, int temp_c) {
  if(cfg->curve_len == 0) return 0;

  unsigned last = cfg->curve_len - 1;
  if(temp_c <= (int)cfg->curve[0].temperature_c)
    return (int)cfg->curve[0].duty_pct;
  if(temp_c >= (int)cfg->curve[last].temperature_c)
    return (int)cfg->curve[last].duty_pct;

  for(unsigned i = 0; i + 1 < cfg->curve_len; i++) {
    int t0 = (int)cfg->curve[i].temperature_c;
    int t1 = (int)cfg->curve[i + 1].temperature_c;
    if(temp_c < t0 || temp_c > t1) continue;

    int d0 = (int)cfg->curve[i].duty_pct;
    int d1 = (int)cfg->curve[i + 1].duty_pct;
    if(t1 == t0) return d1;
    return d0 + ((d1 - d0) * (temp_c - t0)) / (t1 - t0);
  }
  return (int)cfg->curve[last].duty_pct;
}


/* -------------------------------------------------------- moving average */

typedef struct {
  int      v[SAMPLE_MAX];
  unsigned cap;
  unsigned count;
  unsigned head;
} temp_ring_t;

static void
ring_reset(temp_ring_t *r, unsigned cap) {
  if(cap < 4)          cap = 4;
  if(cap > SAMPLE_MAX) cap = SAMPLE_MAX;
  r->cap   = cap;
  r->count = 0;
  r->head  = 0;
}

static void
ring_push(temp_ring_t *r, int value) {
  r->v[r->head] = value;
  r->head = (r->head + 1) % r->cap;
  if(r->count < r->cap) r->count++;
}

/* Oldest-first index helper. */
static int
ring_at(const temp_ring_t *r, unsigned i) {
  unsigned start = (r->head + r->cap - r->count) % r->cap;
  return r->v[(start + i) % r->cap];
}

/* Change the window length and keep the samples.
 *
 * The call site did this with ring_reset(), whose comment promised a resize
 * "without losing the ring" while the function set count and head to zero and
 * threw every sample away. The window length changes when the profile does, so
 * every profile switch emptied the average.
 *
 * That costs more than it sounds. An empty ring makes the average undefined,
 * and the control block is guarded on `avg_c100 >= 0`, so the controller skips
 * its turn outright. For the seconds after that it works from a one- then
 * two-sample "average" — precisely the raw, ±2 K noisy reading the window
 * exists to suppress.
 *
 * Keeping the newest samples that still fit costs one copy and makes the
 * promise true. */
static void
ring_resize(temp_ring_t *r, unsigned cap) {
  if(cap < 4)          cap = 4;
  if(cap > SAMPLE_MAX) cap = SAMPLE_MAX;
  if(cap == r->cap) return;

  /* Read out before anything is changed — ring_at() depends on the old
     geometry. Oldest-first, dropping the eldest if the window shrank. */
  int      keep[SAMPLE_MAX];
  unsigned n = r->count < cap ? r->count : cap;
  for(unsigned i = 0; i < n; i++)
    keep[i] = ring_at(r, r->count - n + i);

  for(unsigned i = 0; i < n; i++) r->v[i] = keep[i];
  r->cap   = cap;
  r->count = n;
  r->head  = n % cap;   /* leaves ring_at()'s start at 0, matching the copy */
}

static int
ring_avg_c100(const temp_ring_t *r) {
  if(!r->count) return -1;
  int sum = 0;
  for(unsigned i = 0; i < r->count; i++) sum += ring_at(r, i);
  return (sum * 100) / (int)r->count;
}

/* Rise across the window: mean of the newer half minus mean of the older
   half, in hundredths of a degree. Positive means warming up. */
static int
ring_trend_c100(const temp_ring_t *r) {
  if(r->count < 4) return 0;

  unsigned half = r->count / 2;
  int old_sum = 0, new_sum = 0;
  for(unsigned i = 0; i < half; i++)            old_sum += ring_at(r, i);
  for(unsigned i = r->count - half; i < r->count; i++) new_sum += ring_at(r, i);

  return ((new_sum - old_sum) * 100) / (int)half;
}


/* ---------------------------------------------------- comfort controller */

/* How long the temperature must stay low before the fan is allowed to ease
   off by one percent. Slower than speeding up, on purpose.
 *
 * The wait is shortened when the console is a long way under the target.
 * Leaving a game drops the die temperature by fifteen degrees within a few
 * seconds — that is not sensor noise the deadband should absorb, it is a
 * confirmed change, and holding the fan at load speed for the several
 * minutes the base rate would need is exactly the noise this design is
 * meant to remove. */
static unsigned
cooldown_hold_sec(const ps5tm_config_t *cfg, int below_c100) {
  unsigned base;
  switch(cfg->profile) {
    case PS5TM_PROFILE_COOL:     base = 10; break;
    case PS5TM_PROFILE_BALANCED: base = 15; break;
    default:                     base = 20; break;
  }

  if(below_c100 >= 800)      base /= 4;   /* 8 K under target */
  else if(below_c100 >= 300) base /= 2;   /* 3 K under target */

  return base < 3 ? 3 : base;
}

static int
clamp_duty(int v) {
  if(v < 0)   return 0;
  if(v > 100) return 100;
  return v;
}

/* Upper limit for normal operation.
 *
 * Without one the controller behaves like a thermostat that can never reach
 * its setpoint: a console that settles above the target under load keeps
 * getting a little more fan every cycle until it is at full speed. That is
 * technically correct and exactly the noise this design exists to avoid, so
 * comfort mode accepts running a few degrees warmer instead. The safety path
 * ignores this ceiling entirely. */
static int
comfort_ceiling(const ps5tm_config_t *cfg) {
  switch(cfg->profile) {
    case PS5TM_PROFILE_COOL:     return 100;
    case PS5TM_PROFILE_BALANCED: return 85;
    default:                     return 65;
  }
}

/* The target as the controller may use it, whatever the settings say.
 *
 * ps5tm_config_clamp() keeps the global target in its range, but a per-title rule
 * reaches the controller by another road — the settings file and an imported
 * configuration carry it unchecked — and the figure is multiplied by 100 on
 * its way into the band. A rule of 500 °C switched the comfort fan off for
 * that title until the safety limit, one of -5 (read as an unsigned number)
 * ran it at the ceiling for good, and 100000000 overflowed the multiplication
 * outright. The controller therefore never takes the target from the settings
 * unchecked, however well they were checked before. */
static unsigned
clamp_target_c(unsigned t) {
  if(t < PS5TM_TARGET_MIN_C) return PS5TM_TARGET_MIN_C;
  if(t > PS5TM_TARGET_MAX_C) return PS5TM_TARGET_MAX_C;
  return t;
}

static int
target_c100(const ps5tm_config_t *cfg) {
  return (int)clamp_target_c(cfg->target_temp_c) * 100;
}

/* Where the threshold rests while the fan has nothing to do: PS5TM_REST_ABOVE_TARGET_C
 * above the target, but never below PS5TM_THRESHOLD_BASE_C (80, where it always rested) and
 * never above PS5TM_THRESHOLD_MAX_C (91, the firmware's own value).
 *
 * For targets up to 70 °C this is 80 exactly as before. Above, the resting place rises
 * with the target: with the threshold stuck at 80 a console held at 85 °C would sit 5 °C
 * above it, which the firmware answers with extra fan speed — the quiet state that a
 * high target is for would not exist. At a target of 91 the resting threshold is the
 * firmware's own 91, which is "as quiet as without this app". */
static int
rest_threshold_c(const ps5tm_config_t *cfg) {
  int t = (int)clamp_target_c(cfg->target_temp_c) + PS5TM_REST_ABOVE_TARGET_C;
  if(t < PS5TM_THRESHOLD_BASE_C) t = PS5TM_THRESHOLD_BASE_C;
  if(t > PS5TM_THRESHOLD_MAX_C)  t = PS5TM_THRESHOLD_MAX_C;
  return t;
}

/* A profile out of range behaves like the default, as it does everywhere else
   in this file's switch statements — stated here once so that it is also what
   gets reported. */
static ps5tm_profile_t
valid_profile(ps5tm_profile_t p) {
  return (p == PS5TM_PROFILE_BALANCED || p == PS5TM_PROFILE_COOL)
           ? p : PS5TM_PROFILE_COMFORT;
}

/* The speed this temperature calls for. A plain function of the reading, with
 * no memory of what came before — and that is the whole point.
 *
 * At the target the fan sits at its floor; `control_band_c` degrees above it,
 * flat out; linear in between. The same temperature therefore always produces
 * the same speed, whether the console arrived there from above or below and
 * regardless of how long it has been there.
 *
 * What this replaces mattered. The controller used to add a percent or two
 * every cycle for as long as the console was warmer than the target, which is
 * an integrator with nothing to stop it. Against a target the hardware can
 * actually hold, that settles. Against one it cannot — 60 °C while a game
 * keeps the die at 72 °C — the error never goes away, so the speed climbs
 * every five seconds until it hits the ceiling and stays there. The console
 * ended up at full speed within a couple of minutes and never came back down.
 * That is textbook integral wind-up, and no amount of damping fixes it; only
 * removing the integrator does.
 *
 * The band also makes an impossible target harmless instead of ruinous. Ask
 * for 60 °C with a 20 K band and a console at 72 °C runs the fan at 60 % —
 * audible, deliberate, and steady — rather than at 100 % forever. */
static int
band_duty(int avg_c100, const ps5tm_config_t *cfg) {
  int start = target_c100(cfg);
  int span  = (int)cfg->control_band_c * 100;
  if(span < 100) span = 100;                 /* never divide by nothing */

  if(avg_c100 <= start)        return 0;
  if(avg_c100 >= start + span) return 100;
  return (avg_c100 - start) * 100 / span;
}

/* Returns the fan speed the controller wants next: the band says where to end
   up, the rate limit decides how quickly it gets there. */
static int
comfort_step(int duty, int avg_c100, int trend_c100,
             const ps5tm_config_t *cfg, uint64_t now,
             uint64_t *last_down_ms, int *safety_out,
             int at_min_cooling, int measured_duty) {
  int safety = (int)cfg->safety_temp_c * 100;

  /* Hardware first: above the safety limit the band, the trend and the
     comfort ceiling are all ignored and the fan is pushed up hard. */
  if(avg_c100 >= safety) {
    *safety_out = 1;
    return clamp_duty(duty + 10);
  }
  *safety_out = 0;

  int want    = band_duty(avg_c100, cfg);
  int ceiling = comfort_ceiling(cfg);
  if(want > ceiling) want = ceiling;

  /* Without a rate limit a load step would move the fan to its new speed in a
     single cycle — exactly the jump this design exists to prevent. One
     percent per five seconds means a change of forty percent takes three
     minutes, which is slow enough not to be noticed as an event. */
  int step = (int)cfg->max_step_pct;
  if(step < 1) step = 1;
  if(avg_c100 >= safety - 300) step *= 2;    /* near the limit, hurry */

  if(want > duty) {
    /* Already falling on its own? Then the current speed is doing its job. */
    if(trend_c100 <= -TREND_SIGNIFICANT_C100 && avg_c100 < safety - 300)
      return duty;

    /* How far behind the fan is decides how fast it may catch up.
     *
     * ── Why, measured 02.08.2026 ─────────────────────────────────────────
     * A game was started with the console idle and the fan on its 13 % floor.
     * The band asked for 44 % immediately, but the comfort profile allows one
     * percent per five seconds, so closing that gap needed over two and a half
     * minutes. The die reached 78.1 °C first, the safety path fired, threw the
     * fan to 100 %, and the walk back down took a further quarter of an hour:
     *
     *   [WARN] safety_mode: Sicherheitsgrenze erreicht (78.1 °C >= 78 °C)
     *   [INFO] ... gewünschte Drehzahl 64%, tatsächliche 100%
     *
     * Silence, then a roar, then a long decay — precisely the audible event
     * the rate limit exists to prevent, arriving late and louder.
     *
     * The fix is not to raise the limit. Small corrections must stay at one
     * percent, because those are the ones a listener would notice as hunting.
     * What has to scale is catching up from a long way behind: a 30-point gap
     * is a load step, not noise, and treating it with the same caution as a
     * one-point drift is what let the temperature run away.
     *
     * A quarter of the gap keeps it smooth — each cycle covers less than the
     * one before, so the approach eases off rather than slamming into place,
     * and the sound is a swell rather than a jump. The floor stays at the
     * configured step, so nothing gets slower than it was. */
    int gap = want - duty;
    int fast = gap / 4;
    if(fast > step) step = fast;

    int next = duty + step;
    return clamp_duty(next > want ? want : next);
  }

  if(want < duty) {
    if(trend_c100 >= TREND_SIGNIFICANT_C100) return duty;

    /* Asymmetric hysteresis, expressed in the degrees the user set: the
       temperature has to have fallen a real deadband below what the current
       speed corresponds to, not a hair. Otherwise the fan hunts around every
       small fluctuation, which is more noticeable than a slightly high but
       steady speed. */
    int hyst = (int)cfg->deadband_c * 100 / (int)cfg->control_band_c;
    if(hyst < 1) hyst = 1;
    if(want > duty - hyst) return duty;

    /* The fan has a floor. Once the threshold sits at its maximum the
       firmware is already running the console's own idle speed and nothing
       slower exists, so counting the figure down further is meaningless —
       and worse, the controller would then have to climb back through those
       phantom percentages the moment the console warms up. Track the real
       floor instead. */
    if(at_min_cooling && measured_duty > 0 && measured_duty > want)
      return measured_duty;

    int below = target_c100(cfg) - avg_c100;
    if(below < 0) below = 0;
    if(now - *last_down_ms <
       (uint64_t)cooldown_hold_sec(cfg, below) * 1000)
      return duty;

    *last_down_ms = now;
    int next = duty - 1;
    return clamp_duty(next < want ? want : next);
  }

  return duty;
}


/* --------------------------------------------------------- duty servo */

/* `allow_more_cooling` is false once the console is already cooler than
   asked for. Without that guard the servo keeps chasing a stale duty target
   while the temperature collapses — leaving a game drops the die by fifteen
   degrees in seconds — and winds the threshold down towards its minimum. The
   next time a game starts, that wound-up threshold meets a hot console and
   the fan slams to full. */
static int
servo_step(int current, int target_duty, int measured_duty,
           int allow_more_cooling) {
  int err = target_duty - measured_duty;
  int mag = err < 0 ? -err : err;

  if(mag <= SERVO_TOLERANCE_PCT) return current;
  if(err > 0 && !allow_more_cooling) return current;

  int step = mag / 6;
  if(step < 1) step = 1;

  /* Asymmetric on purpose: falling behind while the console is hot is the
     failure that matters, so adding cooling may move further per step than
     taking it away. */
  int cap = (err > 0) ? SERVO_STEP_MAX_C : 1;
  if(step > cap) step = cap;

  /* A lower threshold widens the gap to the temperature, which is what makes
     the firmware spin the fan faster. */
  int next = (err > 0) ? current - step : current + step;

  if(next < PS5TM_THRESHOLD_MIN_C) next = PS5TM_THRESHOLD_MIN_C;
  if(next > PS5TM_THRESHOLD_MAX_C) next = PS5TM_THRESHOLD_MAX_C;
  return next;
}


/* ------------------------------------------------------------- public API */

void
ps5tm_fan_snapshot(ps5tm_snapshot_t *out) {
  pthread_mutex_lock(&g_snapshot_lock);
  *out = g_snapshot;
  pthread_mutex_unlock(&g_snapshot_lock);
}

int
ps5tm_fan_apply_manual(int temp_c, int *errno_out) {
  if(temp_c < PS5TM_THRESHOLD_MIN_C) temp_c = PS5TM_THRESHOLD_MIN_C;
  if(temp_c > PS5TM_THRESHOLD_MAX_C) temp_c = PS5TM_THRESHOLD_MAX_C;

  int rc = ps5tm_platform_set_fan_threshold(temp_c, errno_out);
  if(rc == 0) {
    atomic_store(&g_manual_threshold_c, temp_c);
    pthread_mutex_lock(&g_snapshot_lock);
    g_snapshot.applied_threshold_c = temp_c;
    g_snapshot.target_duty_pct     = ps5tm_fan_threshold_to_duty(temp_c);
    pthread_mutex_unlock(&g_snapshot_lock);
  }
  return rc;
}


/* ------------------------------------------------- warnings on the screen
 *
 * The fan thread decides WHEN a warning is due (check_warnings, below) and
 * never shows it. ps5tm_notify() is a call into the notification service, and
 * ps5tm_show_dialog() takes the Sony lock and calls into the shell; either can
 * wait for as long as the shell likes. This thread has to go on regulating
 * while the console is at its hottest, which is exactly when a warning is due,
 * so the text is left in a slot and a helper thread does the showing.
 *
 * One slot per source (CPU, SoC), the newest text wins. If the helper is stuck
 * inside the shell nothing queues up behind it and the fan thread never waits:
 * it overwrites a slot and carries on. The log line is written by the caller
 * either way, so a warning that could not be shown is still on record. */
enum { WARN_CPU = 0, WARN_SOC = 1, WARN_SLOTS = 2 };

typedef struct {
  int  pending;
  int  dialog;                 /* 1: ps5tm_show_dialog(), 0: ps5tm_notify() */
  char text[192];
} warn_slot_t;

static warn_slot_t     g_warn_slot[WARN_SLOTS];
static pthread_mutex_t g_warn_lock = PTHREAD_MUTEX_INITIALIZER;
static atomic_int      g_warn_helper = 0;      /* 1 once the helper runs */

static void
warn_post(int slot, int dialog, const char *text) {
  if(!atomic_load(&g_warn_helper)) return;     /* no helper: the log stands */
  pthread_mutex_lock(&g_warn_lock);
  g_warn_slot[slot].dialog  = dialog;
  g_warn_slot[slot].pending = 1;
  snprintf(g_warn_slot[slot].text, sizeof(g_warn_slot[slot].text), "%s", text);
  pthread_mutex_unlock(&g_warn_lock);
}

static void *
warn_worker(void *arg) {
  (void)arg;
  for(;;) {
    for(int i = 0; i < WARN_SLOTS; i++) {
      warn_slot_t take;
      pthread_mutex_lock(&g_warn_lock);
      take = g_warn_slot[i];
      g_warn_slot[i].pending = 0;
      pthread_mutex_unlock(&g_warn_lock);

      if(!take.pending) continue;
      if(take.dialog) ps5tm_show_dialog(take.text);
      else            ps5tm_notify("%s", take.text);
    }
    usleep(250 * 1000);
  }
  return NULL;
}


static void
check_warnings(const ps5tm_sensors_t *s, int *warn_cpu, int *warn_soc) {
  ps5tm_config_lock();
  int cpu_limit    = (int)g_config.warning_cpu_c;
  int soc_limit    = (int)g_config.warning_soc_c;
  int safety_limit = (int)g_config.safety_temp_c;
  int automatic    = (g_config.mode == PS5TM_MODE_AUTOMATIC);
  ps5tm_config_unlock();

  /* A warning is for a console that is hotter than it should be. With a target of 91 °C the console is meant to
     run at 85, and a warning limit of 80 would nag all game long. So the limit in force is never lower than the
     target in force + 2 (the target of the running game's own rule included: the snapshot of the previous cycle
     has it); the configured value stays as it is and applies whenever the target is lower. The main chip's limit
     keeps its 5 °C above the processor's. */
  if(automatic) {
    pthread_mutex_lock(&g_snapshot_lock);
    int target = g_snapshot.effective_target_c;
    pthread_mutex_unlock(&g_snapshot_lock);
    if(target > 0 && cpu_limit < target + 2) cpu_limit = target + 2;
    if(target > 0 && soc_limit < cpu_limit + 5) soc_limit = cpu_limit + 5;
  }

  *warn_cpu = (s->cpu_valid && s->cpu_c >= cpu_limit);
  *warn_soc = (s->soc_valid && s->soc_c >= soc_limit);

  if(*warn_cpu && !g_cpu_warning_logged) {
    g_cpu_warning_logged = 1;
    PS5TM_WARN("cpu_temperature_warning",
               "Die CPU-Temperatur hat die konfigurierte Warnschwelle "
               "erreicht (%d °C >= %d °C).", s->cpu_c, cpu_limit);
    /* A log line nobody is looking at is no warning at all — put it on the
       screen, where it also reaches someone in the middle of a game. Past the
       safety limit it becomes a dialog: a corner toast is easy to miss, and
       by then the console genuinely needs attention. Handed to the helper
       thread, see warn_post(): this thread does not call into the shell. */
    char text[192];
    snprintf(text, sizeof(text),
             "Temperaturwarnung\nCPU %d °C (Grenze %d °C). "
             "Sorge für freie Belüftung.", s->cpu_c, cpu_limit);
    warn_post(WARN_CPU, s->cpu_c >= safety_limit, text);
  } else if(s->cpu_valid && s->cpu_c < cpu_limit - 3) {
    g_cpu_warning_logged = 0;   /* 3 °C hysteresis before re-arming */
  }

  if(*warn_soc && !g_soc_warning_logged) {
    g_soc_warning_logged = 1;
    PS5TM_WARN("soc_temperature_warning",
               "Die SoC-Temperatur hat die konfigurierte Warnschwelle "
               "erreicht (%d °C >= %d °C).", s->soc_c, soc_limit);
    char text[192];
    snprintf(text, sizeof(text),
             "Temperaturwarnung\nSoC %d °C (Grenze %d °C). "
             "Sorge für freie Belüftung.", s->soc_c, soc_limit);
    warn_post(WARN_SOC, s->soc_c >= safety_limit, text);
  } else if(s->soc_valid && s->soc_c < soc_limit - 3) {
    g_soc_warning_logged = 0;
  }
}


/* What the fan thread may wait for.
 *
 * It is the only thing that regulates the fan and nothing watches it: when it
 * stops, the fan stays wherever it was. So the list of things it waits for is
 * short, and each of them is brief by construction —
 *
 *   - Never the Sony lock. The optional telemetry in platform.c and
 *     telemetry.c (CPU load, power rails, clocks, frame rate) only try-locks
 *     it and, when it is busy, hands out the previous reading. Whoever holds
 *     that lock is inside a Sony service, which can take as long as it likes.
 *   - Never a Sony service itself. Warnings go to a helper thread (warn_post),
 *     the lightbar to the probe thread.
 *   - Temperature sensors, the fan-speed register and /dev/icc_fan are plain
 *     syscalls; none of them needs a lock.
 *   - Mutexes that are only ever held for a copy: the settings, this file's
 *     snapshot, the ring of log entries, the history and recording buffers.
 *   - The occasional small file write in history.c and thermalog.c, and the
 *     log's mirror to the sender's socket, which has a timeout and switches
 *     itself off when it is not met (log.c).
 */
static void *
fan_worker(void *arg) {
  (void)arg;
  /* This thread deliberately does NOT name itself.
   *
   * On the PS5, SYS_thr_set_name does not do what its name suggests: it sets
   * the *process* name (ki_comm), not the thread's. Measured on 01.08.2026 —
   * with "ps5tm" set in main(), the kernel reported ki_comm "ps5tm" while
   * ki_tdname stayed "SceSpZeroConfMai", a Sony name from the process the
   * loader borrowed.
   *
   * So a rename here renamed the whole app. It showed: every klog line after
   * this point carried the prefix "[ps5tm-fan]", including "ready" and
   * "http_started", which the main thread writes. The prefix was never a
   * per-thread marker — it was the process name, and the last writer won. */

  temp_ring_t ring;
  ps5tm_config_lock();
  ring_reset(&ring, g_config.average_window_s);
  unsigned window_now = g_config.average_window_s;
  ps5tm_config_unlock();

  int      desired_duty      = -1;  /* what the comfort controller asks for */
  int      last_applied      = 0;
  uint64_t last_apply_ms     = 0;   /* any write, including refreshes       */
  uint64_t last_servo_ms     = 0;
  uint64_t last_control_ms   = 0;
  uint64_t last_down_ms      = 0;
  int      duty_ema_x10      = -1;
  int      logged_threshold  = 0;
  uint64_t last_log_ms       = 0;
  int      safety_active     = 0;
  int      safety_logged     = 0;
  int      warning_active    = 0;
  int      warning_left_s    = -1;
  int      lightbar_state    = 0;
  int      sensors_logged_ok = 0;
  int      fan_error_logged  = 0;
  int      was_automatic     = -1;  /* -1 = first pass, mode not seen yet */
  uint64_t last_loop_ms      = 0;

  /* Sensor loss and a dead fan-speed register, counted in passes — one a
     second while this thread runs. Passes rather than clock time on purpose:
     a console that slept for a minute must not wake up "blind" because the
     clock moved on while this thread did not. */
  int      blind             = 0;   /* no valid temperature for a while       */
  int      blind_passes      = 0;
  int      duty_dead_passes  = 0;   /* passes in a row without a duty reading */
  int      duty_lost_logged  = 0;

  /* Watchdog state — see the block inside the loop. `readback_ok` starts
     hopeful. It is cleared when the controller could not be read
     READBACK_FAIL_LATCH times in a row, which is what a firmware without
     command 0x8F08 looks like, and a cleared one is retried every
     READBACK_RETRY_SEC seconds: until then the setting is simply re-sent
     periodically, as it was before the readback existed. */
  int      readback_ok       = 1;
  int      readback_fails    = 0;
  uint64_t last_readback_ms  = 0;
  int      observed_c        = -1;
  unsigned override_count    = 0;
  int      last_override_c   = 0;
  uint64_t last_override_log = 0;

  for(;;) {
    uint64_t now = ps5tm_now_ms();

    if(last_loop_ms && now > last_loop_ms && now - last_loop_ms >= RESUME_GAP_MS) {
      PS5TM_WARN("resume_detected",
                 "Lange Schleifenpause (%llus) erkannt – Rechte und "
                 "Lüftervorgabe werden erneut abgesichert.",
                 (unsigned long long)((now - last_loop_ms) / 1000ull));

      if(ps5tm_platform_escalate() != 0)
        PS5TM_WARN("resume_escalation_failed",
                   "Rechteausweitung nach Pause fehlgeschlagen."
                   " Lüftersteuerung kann ausfallen.");
      else
        PS5TM_INFO("resume_escalation_ok",
                   "Rechteausweitung nach Pause bestätigt.");

      last_apply_ms = 0;
      last_servo_ms = 0;
      duty_ema_x10  = -1;
      fan_error_logged = 0;
    }
    last_loop_ms = now;

    ps5tm_sensors_t s;
    int sensors_ok = (ps5tm_platform_read_sensors(&s) == 0);

    ps5tm_gamestate_t gs;
    ps5tm_gamestate_get(&gs);
    int activity_fg = (gs.foreground && gs.title_id[0]) ? 1 : 0;

    /* The worker already holds a full sensor sweep once a second, so the
       channel recording costs nothing here. It decimates itself. */
    if(sensors_ok) ps5tm_chanlog_sample(&s);

    /* Same free ride for the long-term record. It keeps only samples inside
       its operating window and one in five seconds, so most calls return
       immediately. */
    if(sensors_ok)
      /* The legacy load figure on purpose: the record's operating window
         (load 25-75 %) is defined in it, and the weeks already on disk were
         counted with it. The sixteen-CPU mean of 1.45.0 would move samples
         in and out of the window and break the comparison it exists for. */
      ps5tm_thermal_sample(s.soc_c, s.soc_valid,
                           s.cpu_load_legacy_pct, s.cpu_load_legacy_valid,
                           s.fan_duty_pct, s.fan_duty_valid,
                           activity_fg);

    if(sensors_ok && !sensors_logged_ok) {
      sensors_logged_ok = 1;
      PS5TM_INFO("sensor_adapter_ready",
                 "Sensoren bereit (Prozessor %s, Hauptchip %s).",
                 s.cpu_valid ? "wird gelesen" : "nicht lesbar",
                 s.soc_valid ? "wird gelesen" : "nicht lesbar");
    }

    int warn_cpu = 0, warn_soc = 0;
    check_warnings(&s, &warn_cpu, &warn_soc);

    ps5tm_config_lock();
    ps5tm_config_t cfg = g_config;
    ps5tm_config_unlock();

    /* A saved per-title setting overrides the global one for as long as that
       game is in the foreground. Nothing is written to the configuration —
       the override lives only in this loop's copy, so switching games never
       silently rewrites the user's own defaults. */
    {
      const char *active_rule = NULL;
      if(gs.foreground && gs.title_id[0]) {
        for(unsigned i = 0; i < cfg.game_rule_count; i++) {
          if(strcmp(cfg.game_rules[i].title_id, gs.title_id) != 0) continue;
          /* Checked here, at the point of use, however well the settings
             were checked before: see clamp_target_c(). */
          cfg.target_temp_c = clamp_target_c(cfg.game_rules[i].target_temp_c);
          cfg.profile       = valid_profile(cfg.game_rules[i].profile);
          /* The safety limit has to stay clear of the target in force, or the
             controller flips into emergency mode while doing its job. The
             settings enforce that for the global target; a title's own
             target gets the same here. */
          if(cfg.safety_temp_c < cfg.target_temp_c + 4)
            cfg.safety_temp_c = cfg.target_temp_c + 4;
          active_rule       = cfg.game_rules[i].title_id;
          break;
        }
      }

      if(active_rule && !g_rule_active) {
        g_rule_active = 1;
        PS5TM_INFO("game_rule_applied",
                   "Spielprofil für %s aktiv: Ziel %u °C.",
                   gs.title_name[0] ? gs.title_name : active_rule,
                   cfg.target_temp_c);
      } else if(!active_rule && g_rule_active) {
        g_rule_active = 0;
        PS5TM_INFO("game_rule_cleared",
                   "Spielprofil beendet – zurück auf Ziel %u °C.",
                   cfg.target_temp_c);
      }
      snprintf(g_active_rule_id, sizeof(g_active_rule_id), "%s",
               active_rule ? active_rule : "");
    }

    /* The window length follows the profile, so this fires on every profile
       change. ring_resize() keeps the samples — see there for what using
       ring_reset() here used to cost. */
    if(cfg.average_window_s != window_now) {
      window_now = cfg.average_window_s;
      ring_resize(&ring, window_now);
    }

    /* Sampling stays at 1 Hz — only the regulation is slowed down. */
    int hottest = -1;
    if(s.cpu_valid && s.cpu_c > hottest) hottest = s.cpu_c;
    if(s.soc_valid && s.soc_c > hottest) hottest = s.soc_c;
    if(hottest >= 0) {
      if(blind) {
        /* Readings are back. What is still in the ring was taken before the
           gap: a console that heated up or cooled down in the meantime would
           be averaged against a temperature it no longer has, and the
           controller would step back to it. The average starts over from the
           fresh reading instead. The fan, held at BLIND_MIN_DUTY_PCT until
           now, eases off from there at the normal rate. */
        blind = 0;
        ring_reset(&ring, window_now);
        PS5TM_INFO("sensors_back",
                   "Temperaturwerte sind wieder da – der Durchschnitt beginnt "
                   "neu, die Drehzahl sinkt nur schrittweise.");
      }
      blind_passes = 0;
      ring_push(&ring, hottest);
    } else if(ring.count > 0 && !blind && ++blind_passes >= BLIND_AFTER_PASSES) {
      /* Nothing valid for a while, and there is an average that would
         otherwise go on being acted upon, unchanged, for as long as this
         lasts. Only with something to protect: a console whose sensors never
         answered has no average to distrust. */
      blind = 1;
      if(cfg.mode == PS5TM_MODE_AUTOMATIC)
        PS5TM_WARN("sensors_lost",
                   "Seit %d s kein gültiger Temperaturwert – der Lüfter wird "
                   "auf mindestens %d %% gehalten, bis wieder Messwerte "
                   "kommen.", BLIND_AFTER_PASSES, BLIND_MIN_DUTY_PCT);
      else
        PS5TM_WARN("sensors_lost",
                   "Seit %d s kein gültiger Temperaturwert.",
                   BLIND_AFTER_PASSES);
    }

    int avg_c100   = ring_avg_c100(&ring);
    int trend_c100 = ring_trend_c100(&ring);

    if(s.fan_duty_valid) {
      int m10 = s.fan_duty_pct * 10;
      duty_ema_x10 = (duty_ema_x10 < 0)
        ? m10
        : duty_ema_x10 + ((m10 - duty_ema_x10) * DUTY_EMA_NUM) / DUTY_EMA_DEN;
      duty_dead_passes = 0;
      duty_lost_logged = 0;
    } else if(duty_ema_x10 >= 0 && ++duty_dead_passes >= DUTY_STALE_PASSES) {
      /* The register stopped answering. The smoothed value would stay where it
         was for good, and the servo would steer by a speed the fan no longer
         has — in the worst case by a high one that no wish can exceed, so that
         it can never ask for more cooling again. Dropping it makes the servo
         branch below fall back to the open-loop mapping. */
      duty_ema_x10 = -1;
      if(!duty_lost_logged) {
        duty_lost_logged = 1;
        PS5TM_INFO("fan_duty_lost",
                   "Die Lüfterdrehzahl lässt sich seit %d s nicht mehr lesen – "
                   "die Schwelle folgt dem Wunsch ohne Rückmeldung, bis sie "
                   "wieder da ist.", DUTY_STALE_PASSES);
      }
    }
    int measured = (duty_ema_x10 < 0) ? -1 : (duty_ema_x10 + 5) / 10;

    int      automatic = (cfg.mode == PS5TM_MODE_AUTOMATIC);

    {
      int hottest = -1;
      if(s.cpu_valid && s.cpu_c > hottest) hottest = s.cpu_c;
      if(s.soc_valid && s.soc_c > hottest) hottest = s.soc_c;

      int warn_limit = (int)cfg.warning_cpu_c;
      if((int)cfg.warning_soc_c < warn_limit) warn_limit = (int)cfg.warning_soc_c;

      lightbar_state = 0;
      if(hottest >= (int)cfg.lightbar_hot_c) lightbar_state = 2;
      else if(hottest >= (int)cfg.lightbar_warn_c) lightbar_state = 1;

      if(hottest >= 0 && hottest >= warn_limit) {
        warning_active = 1;
        if(g_warning_since_ms == 0) g_warning_since_ms = now;

        uint64_t elapsed_ms = now - g_warning_since_ms;
        uint64_t total_ms = (uint64_t)cfg.warning_countdown_s * 1000ull;
        warning_left_s = elapsed_ms >= total_ms
          ? 0
          : (int)((total_ms - elapsed_ms + 999ull) / 1000ull);
      } else {
        warning_active = 0;
        warning_left_s = -1;
        g_warning_since_ms = 0;
      }

      {
        int effective = safety_active ? 2 : lightbar_state;
        int allow_pad = (cfg.probe_mask & PS5TM_PROBE_PAD) != 0;
        ps5tm_pad_lightbar_request((cfg.lightbar_enabled && allow_pad) ? 1 : 0,
                                   effective);
      }
    }

    /* Handing control back has to be an action, not just an omission: the
       last threshold we wrote stays in the register until something
       overwrites it. */
    if(was_automatic == 1 && !automatic &&
       atomic_load(&g_manual_threshold_c) <= 0) {
      int eno = 0;
      if(ps5tm_platform_set_fan_threshold(PS5TM_NEUTRAL_THRESHOLD_C,
                                          &eno) == 0) {
        PS5TM_INFO("fan_released",
                   "Automatik aus – die Konsole hat ihre eigene "
                   "Lüftereinstellung (%d °C) zurück und regelt den Lüfter "
                   "wieder selbst.",
                   PS5TM_NEUTRAL_THRESHOLD_C);
      } else {
        PS5TM_WARN("fan_release_failed",
                   "Automatik aus, die eigene Lüftereinstellung der Konsole "
                   "konnte aber nicht zurückgegeben werden: %s",
                   ps5tm_platform_fan_message());
      }
      /* Forget our own setting so nothing is refreshed from here on. */
      last_applied = 0;
      desired_duty = -1;
      duty_ema_x10 = -1;
      logged_threshold = 0;
    }
    was_automatic = automatic;

    /* ---- comfort controller: only every control_interval_s ---- */
    if(automatic && avg_c100 >= 0) {
      if(desired_duty < 0) {
        /* Start from whatever the fan is already doing so the first cycle
           cannot produce an audible jump. */
        desired_duty = (measured >= 0) ? measured : 20;
      }
      if(blind) {
        /* No temperature to steer by, so the controller does not steer: its
           average is a memory and its step logic would act on it. The fan is
           held at no less than BLIND_MIN_DUTY_PCT instead — clearly audible,
           but well short of the full speed of the safety ramp — and the servo
           below is allowed to add cooling to get there. When readings return
           the controller continues from this value at its normal pace. */
        if(desired_duty < BLIND_MIN_DUTY_PCT) desired_duty = BLIND_MIN_DUTY_PCT;
      } else if(now - last_control_ms >=
                (uint64_t)cfg.control_interval_s * 1000) {
        int was_safety = safety_active;
        /* "Nothing more to give": the threshold is already parked at its
           maximum and the fan is turning faster than we are asking for. */
        int at_min = (last_applied >= rest_threshold_c(&cfg)) &&
                     (measured >= 0) && (measured >= desired_duty);
        desired_duty = comfort_step(desired_duty, avg_c100, trend_c100,
                                    &cfg, now, &last_down_ms, &safety_active,
                                    at_min, measured);
        last_control_ms = now;

        if(safety_active && !safety_logged) {
          safety_logged = 1;
          PS5TM_WARN("safety_mode",
                     "Sicherheitsgrenze erreicht (%d.%d °C >= %u °C) – "
                     "Komfortregeln ausgesetzt, Lüfter wird hochgefahren.",
                     avg_c100 / 100, (avg_c100 % 100) / 10, cfg.safety_temp_c);
        } else if(was_safety && !safety_active) {
          safety_logged = 0;
          PS5TM_INFO("safety_mode_left",
                     "Temperatur wieder im Normalbereich – sanfte Regelung "
                     "aktiv.");
        }
      }
    }

    /* ---- duty servo: translate the wish into a threshold ---- */
    int desired_threshold = 0;   /* 0 = leave the fan alone */

    if(automatic && avg_c100 >= 0) {
      if(desired_duty <= SERVO_IDLE_DUTY_PCT) {
        desired_threshold = rest_threshold_c(&cfg);
      } else if(last_applied <= 0) {
        desired_threshold = ps5tm_fan_duty_to_threshold(desired_duty);
      } else if(measured < 0) {
        /* Nothing to correct against: the fan-speed register does not answer
           (a firmware without the call, or one that stopped). Holding the last
           threshold would freeze the loop for good — the safety path included,
           which could then never lower it. The wish is mapped straight onto a
           threshold instead, which the servo normally only uses to seed. It
           is a rougher figure than the servo's, and a working loop would
           correct it; here it is the best there is. */
        desired_threshold = ps5tm_fan_duty_to_threshold(desired_duty);
      } else if(blind && desired_duty < measured) {
        /* Blind, and the fan already runs faster than the floor asks for —
           the firmware is reacting to a heat the sensors no longer report.
           Leave it: while the temperature is unknown, cooling may only grow. */
        desired_threshold = last_applied;
      } else if(now - last_servo_ms >= (uint64_t)SERVO_INTERVAL_SEC * 1000) {
        int allow_more = avg_c100 >
                         target_c100(&cfg) -
                         (int)cfg.deadband_c * 100;
        desired_threshold = servo_step(last_applied, desired_duty, measured,
                                       allow_more || safety_active || blind);
      } else {
        desired_threshold = last_applied;
      }
    } else if(!automatic) {
      int pinned = atomic_load(&g_manual_threshold_c);
      desired_threshold = pinned > 0 ? pinned : 0;
    }

    /* Ask the controller what it is really holding.
     *
     * This is the difference between knowing and assuming. The firmware drops
     * our setting at every change of state — not only when a game launches
     * but when it ends and on the way into and out of rest mode; measured on
     * 24.09.2026, eleven times in 74 minutes, always to 91 °C. The old answer
     * was to rewrite it every 15 s whether or not anything had changed —
     * which meant up to 15 seconds on the console's own value, and no way to
     * tell that it had ever happened. Now the override is seen, corrected
     * within two seconds and said out loud.
     *
     * Reading before deciding also removes the pointless writes: when nothing
     * has touched the register, nothing is sent. */
    if(now - last_readback_ms >= (uint64_t)(readback_ok ? WATCHDOG_INTERVAL_SEC
                                                         : READBACK_RETRY_SEC)
                                 * 1000) {
      unsigned char blk[PS5TM_ICC_FAN_CONFIG_SIZE];
      int rb_eno = 0;
      if(ps5tm_platform_read_fan_config(blk, &rb_eno) == 0) {
        observed_c     = blk[PS5TM_ICC_FAN_TARGET_OFFSET];
        readback_fails = 0;
        if(!readback_ok) {
          readback_ok = 1;
          PS5TM_INFO("fan_readback_restored",
                     "Die Lüftersteuerung lässt sich wieder auslesen – der "
                     "Wächter ist wieder aktiv.");
        }
      } else if(readback_ok && ++readback_fails >= READBACK_FAIL_LATCH) {
        /* Not once but several times in a row. That is what a firmware
           without the command looks like — and also a controller that is
           unreachable for a while, which is why this is retried below rather
           than given up for the rest of the run. Until it answers again the
           setting is simply re-sent periodically. A single failed read keeps
           the last value seen: it says nothing about what the controller
           holds now, and the dashboard should not flicker for it. */
        readback_ok = 0;
        observed_c  = -1;
        PS5TM_INFO("fan_readback_unavailable",
                   "Lüftersteuerung %d-mal hintereinander nicht lesbar (Fehler "
                   "%d: %s) – bis es wieder klappt, wird die Schwelle alle %u s "
                   "neu gesendet; neuer Leseversuch alle %d s.",
                   READBACK_FAIL_LATCH, rb_eno, strerror(rb_eno),
                   cfg.fan_reapply_sec, READBACK_RETRY_SEC);
      }
      last_readback_ms = now;
    }

    if(desired_threshold > 0) {
      int changed = (desired_threshold != last_applied);

      /* Somebody else moved it. `last_applied` is what we last wrote
         successfully, so a difference can only have come from outside. */
      int overridden = (readback_ok && observed_c >= 0 && last_applied > 0 &&
                        observed_c != last_applied);

      if(overridden) {
        override_count++;
        last_override_c = observed_c;
        if(now - last_override_log >= (uint64_t)OVERRIDE_LOG_GAP_SEC * 1000) {
          last_override_log = now;
          PS5TM_INFO("fan_threshold_overridden",
                     "Die Konsole hat die Lüftereinstellung auf %d °C "
                     "gesetzt — das tut sie bei jedem Wechsel, etwa beim "
                     "Starten oder Beenden eines Spiels und beim Ruhemodus. "
                     "Zurückgestellt auf %d °C. Bisher %u mal.",
                     observed_c, desired_threshold, override_count);
        }
      }

      /* Without the readback there is nothing to compare against, so the old
         periodic rewrite stands in for it. */
      int refresh = readback_ok
                      ? overridden
                      : (now - last_apply_ms) >=
                        (uint64_t)cfg.fan_reapply_sec * 1000;

      if(changed || last_applied == 0 || refresh) {
        int eno = 0;
        if(ps5tm_platform_set_fan_threshold(desired_threshold, &eno) == 0) {
          if(changed) last_servo_ms = now;

          int spread = desired_threshold > logged_threshold
                         ? desired_threshold - logged_threshold
                         : logged_threshold - desired_threshold;
          if(changed &&
             (logged_threshold == 0 ||
              (spread >= LOG_MIN_DELTA_C &&
               now - last_log_ms >= (uint64_t)LOG_MIN_GAP_SEC * 1000))) {
            PS5TM_INFO("fan_threshold_applied",
                       "An die Konsole übergeben: %d °C – Durchschnitt "
                       "%d.%d °C, Tendenz %+d, gewünschte Drehzahl %d%%, "
                       "tatsächliche %d%%.",
                       desired_threshold, avg_c100 / 100,
                       (avg_c100 % 100) / 10, trend_c100,
                       desired_duty < 0 ? 0 : desired_duty,
                       measured < 0 ? 0 : measured);
            logged_threshold = desired_threshold;
            last_log_ms      = now;
          }

          last_applied     = desired_threshold;
          last_apply_ms    = now;
          fan_error_logged = 0;

          /* The reading is now stale by definition — we just wrote over it.
             Without this the next pass, a second later, would compare the
             fresh `last_applied` against the value we have already corrected
             and count the same override again, once per second, until the
             next readback caught up. */
          observed_c = desired_threshold;
        } else if(!fan_error_logged) {
          fan_error_logged = 1;
          PS5TM_ERROR("icc_fan_ioctl_failed", "%s",
                      ps5tm_platform_fan_message());
        }
      }
    }

    ps5tm_history_tick(s.cpu_c, s.cpu_valid, s.soc_c, s.soc_valid,
                       s.fan_duty_pct, s.fan_duty_valid,
                       (int)cfg.target_temp_c);

    pthread_mutex_lock(&g_snapshot_lock);
    g_snapshot.sensors             = s;
    g_snapshot.sampled_at_ms       = now;
    g_snapshot.applied_threshold_c = last_applied;
    g_snapshot.observed_threshold_c = readback_ok ? observed_c : -1;
    g_snapshot.readback_available   = readback_ok;
    g_snapshot.override_count       = override_count;
    g_snapshot.last_override_c      = last_override_c;
    g_snapshot.target_duty_pct     = automatic
                                       ? (desired_duty < 0 ? 0 : desired_duty)
                                       : (last_applied > 0
                                            ? ps5tm_fan_threshold_to_duty(last_applied)
                                            : 0);
    g_snapshot.automatic           = automatic;
    g_snapshot.fan_available       = ps5tm_platform_fan_available();
    g_snapshot.warning_cpu         = warn_cpu;
    g_snapshot.warning_soc         = warn_soc;
    g_snapshot.avg_temp_c10        = (avg_c100 < 0) ? -1 : (avg_c100 + 5) / 10;
    g_snapshot.trend_c100          = trend_c100;
    g_snapshot.safety_active       = safety_active;
    g_snapshot.samples             = (int)ring.count;
    g_snapshot.effective_target_c  = (int)cfg.target_temp_c;
    g_snapshot.warning_active      = warning_active;
    g_snapshot.warning_since_ms    = g_warning_since_ms;
    g_snapshot.warning_countdown_s = warning_left_s;
    g_snapshot.lightbar_enabled    = cfg.lightbar_enabled ? 1 : 0;
    g_snapshot.lightbar_supported  = ps5tm_pad_lightbar_supported();
    g_snapshot.lightbar_state      = safety_active ? 2 : lightbar_state;
    pthread_mutex_unlock(&g_snapshot_lock);

    sleep(1);
  }
  return NULL;
}


int
ps5tm_fan_init(void) {
  if(atomic_exchange(&g_started, 1)) return 0;

  ps5tm_config_lock();
  int seed      = (int)g_config.fan_threshold_c;
  int automatic = (g_config.mode == PS5TM_MODE_AUTOMATIC);
  unsigned target   = g_config.target_temp_c;
  unsigned dead     = g_config.deadband_c;
  unsigned interval = g_config.control_interval_s;
  unsigned window   = g_config.average_window_s;
  ps5tm_config_unlock();

  /* Read the controller before this run writes a single byte to it.
   *
   * ⚠ That is NOT the same as reading the factory value, and the first console
   * run proved it. Measured on FW 12.00, 07.09.2026: the block came back with
   * byte 5 = 0x50 = 80 °C, while this app's stored manual threshold is 65 —
   * so 80 was not ours to seed. It is what the *previous* instance left
   * behind, the top of the range our own controller drives to. The firmware
   * keeps whatever was last written until its next change of state resets it
   * — a game starting or ending, rest mode in either direction — so "before
   * we write" only means "before *this* process writes".
   *
   * The console's own untouched value is therefore only readable here after a
   * reboot, before the app has ever run in that session. Worth doing once.
   * 24.09.2026: the first read said 91 °C — the firmware's own value, but
   * left by a game played earlier that evening, not by the boot, so the
   * reboot test is still outstanding.
   *
   * The same measurement is what makes the call trustworthy: a constant or a
   * stale buffer would not have happened to contain exactly the value the
   * previous run had been applying, and 80 differs from the 65 this process
   * was about to write. The block reflects the live register.
   *
   * Diagnosis only: the reading is logged in full and nothing is decided from
   * it. Acting on it — confirming writes, restoring the original on shutdown —
   * comes once the byte layout beyond offset 5 is understood. As far as that
   * goes, 24.09.2026 settled it: 881 samples across a 37–48 °C swing and fan
   * speeds from 20 to 34 %, and only byte 5 ever moved. The rest are
   * settings, not readings. */
  {
    unsigned char cfg[PS5TM_ICC_FAN_CONFIG_SIZE];
    int cfg_eno = 0;
    if(ps5tm_platform_read_fan_config(cfg, &cfg_eno) == 0) {
      char hex[PS5TM_ICC_FAN_CONFIG_SIZE * 3 + 1];
      size_t o = 0;
      for(unsigned i = 0; i < PS5TM_ICC_FAN_CONFIG_SIZE; i++)
        o += (size_t)snprintf(hex + o, sizeof(hex) - o, "%s%02X",
                              i ? " " : "", cfg[i]);
      PS5TM_INFO("icc_fan_config_read",
                 "Lüftersteuerung ausgelesen: sie hält gerade %u °C als "
                 "Schwelle. Nach einem Neustart der Konsole ist das der Wert "
                 "der Konsole selbst, sonst der, den die letzte Ausführung "
                 "dieser App hinterlassen hat. Vollständige Antwort: %s",
                 (unsigned)cfg[PS5TM_ICC_FAN_TARGET_OFFSET], hex);
    } else {
      /* Not an error: the command may simply not exist on this firmware, and
         that is a useful answer too. Everything below carries on unchanged. */
      PS5TM_INFO("icc_fan_config_unavailable",
                 "Die Lüftersteuerung gibt ihre eigene Einstellung nicht "
                 "heraus (Fehler %d: %s). Die Regelung arbeitet wie bisher "
                 "ohne Rückmeldung des Sollwerts.",
                 cfg_eno, strerror(cfg_eno));
    }
  }

  /* Probe the controller once so the UI can report the adapter state before
     the first regulation cycle is due. */
  int eno = 0;
  if(ps5tm_platform_set_fan_threshold(seed, &eno) == 0) {
    if(!automatic) atomic_store(&g_manual_threshold_c, seed);
    PS5TM_INFO("fan_adapter_ready", "Lüftersteuerung bereit.");
  } else {
    PS5TM_ERROR("icc_fan_ioctl_failed", "%s", ps5tm_platform_fan_message());
  }

  /* The helper that puts warnings on the screen (see warn_post). Started first,
     and only once however often this function is tried. If it cannot be
     started the warnings stay in the log: the fan thread must not call into
     the shell itself, so there is no other way to show them. */
  if(!atomic_load(&g_warn_helper)) {
    pthread_t wt;
    pthread_attr_t wattr;
    pthread_attr_init(&wattr);
    pthread_attr_setdetachstate(&wattr, PTHREAD_CREATE_DETACHED);
    int wrc = pthread_create(&wt, &wattr, warn_worker, NULL);
    pthread_attr_destroy(&wattr);
    if(wrc == 0)
      atomic_store(&g_warn_helper, 1);
    else
      PS5TM_WARN("warn_thread_failed",
                 "Der Hilfsthread für Temperaturwarnungen ließ sich nicht "
                 "starten (Fehler %d: %s) – Warnungen erscheinen nur im "
                 "Protokoll.", wrc, strerror(wrc));
  }

  pthread_t t;
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
  int rc = pthread_create(&t, &attr, fan_worker, NULL);
  pthread_attr_destroy(&attr);

  if(rc != 0) {
    /* Without this check the log below announced a controller that did not
       exist, and g_started kept any later call from making one. Nothing is
       left marked as started, so the caller may simply try again. */
    atomic_store(&g_started, 0);
    PS5TM_ERROR("fan_worker_failed",
                "Der Regelthread ließ sich nicht starten (Fehler %d: %s) – "
                "die Lüftersteuerung läuft NICHT, der Lüfter bleibt bei der "
                "zuletzt gesetzten Schwelle.", rc, strerror(rc));
    return -1;
  }

  PS5TM_INFO("fan_worker_started",
             "Automatik läuft – Zieltemperatur %u °C ±%u, Messung jede "
             "Sekunde, Nachregeln alle %u s, geglättet über %u s.",
             target, dead, interval, window);
  return 0;
}
