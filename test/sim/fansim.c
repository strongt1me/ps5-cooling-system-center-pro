/* Closed-loop simulation of the fan regulation (fan.c) and the guard (fanguard.c) in virtual time.
 *
 * fan.c is included directly and its real fan_worker() runs in the main thread; sleep() is redirected to sim_sleep(),
 * which advances a virtual clock by one second once every participating thread is asleep (lockstep). A run of an hour
 * takes well under a second. The plant:
 *
 *   - firmware: fan duty = floor (20 % idle, 34 % in a game) + about 5.6 % per kelvin the CPU is above the threshold,
 *     a few percent within 20 K below it (FW 12.00 / Pro measurements in fan.c and test/stubs_*.c); first-order fan lag;
 *     the firmware resets the threshold to 91 °C whenever a game starts or ends.
 *   - thermal: two nodes, die (fast, about 5 s) over a heatsink (slow, about 100 s) whose conductance to the air grows
 *     with the fan duty. 100 W in a game reaches about 88 °C at the 34 % floor, 32 W idle about 50 °C.
 *   - sensors: CPU = die + noise, main chip = die - 8 + noise, integers.
 *
 * Build and run: see test/sim/run.sh. One scenario per process: ./fansim <scenario>. */

#define _GNU_SOURCE
#include <math.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define sleep sim_sleep
unsigned int sim_sleep(unsigned int s);
#include "../../src/fan.c"
#undef sleep

/* ------------------------------------------------------------------ scenario */

typedef struct {
  const char *name;
  int    target, dur;
  int    load_kind;          /* 0 idle, 1 step idle->game at t1, 2 game->idle at t1, 3 ramp t1..t2, 4 pulses period t1, 5 game */
  int    t1, t2;
  double fw_gain, lag_mul, tau_mul, noise;
  int    profile;            /* PS5TM_PROFILE_* */
  int    guard;              /* start fanguard */
  int    hang_at, hang_len;  /* fan loop stops beating */
  int    pause_at, pause_len;/* rest mode: everything frozen, the clock jumps */
  int    broken;             /* the loop's writes are ignored (threshold stuck) */
  int    fan_full;           /* the firmware runs the fan at >= 95 % whatever */
} scen_t;

static scen_t S;

#define BASE(n, t, k) .name = n, .target = t, .load_kind = k, .fw_gain = 1, .lag_mul = 1, .tau_mul = 1, .noise = 1.5, \
                      .profile = PS5TM_PROFILE_COMFORT, .dur = 1800, .t1 = 300
static const scen_t SCEN[] = {
  { BASE("idle_t66", 66, 0) },
  { BASE("step_t66", 66, 1) },
  { BASE("exit_t66", 66, 2) },
  { BASE("ramp_t66", 66, 3), .t2 = 900 },
  { BASE("noisy_t66", 66, 1), .t1 = 60, .noise = 3 },
  { BASE("pulses_t66", 66, 4), .t1 = 60 },
  { BASE("step_t50", 50, 1) },
  { BASE("step_t65", 65, 1) },
  { BASE("step_t70", 70, 1) },
  { BASE("step_t72", 72, 1) },
  { BASE("step_t75", 75, 1) },
  { BASE("step_t78", 78, 1) },
  { BASE("step_t80", 80, 1) },
  { BASE("step_t82", 82, 1) },
  { BASE("step_t78_safety90", 78, 1), .t2 = 90 },
  { BASE("step_t85", 85, 1) },
  { BASE("step_t91", 91, 1) },
  { BASE("pulses_t85", 85, 4), .t1 = 60 },
  { BASE("step_t66_bal", 66, 1), .profile = PS5TM_PROFILE_BALANCED },
  { BASE("step_t66_cool", 66, 1), .profile = PS5TM_PROFILE_COOL },
  /* sensitivity */
  { BASE("step_t66_gain0.5", 66, 1), .fw_gain = 0.5 },
  { BASE("step_t66_gain2", 66, 1), .fw_gain = 2 },
  { BASE("step_t66_gain3", 66, 1), .fw_gain = 3 },
  { BASE("step_t66_lag2", 66, 1), .lag_mul = 2 },
  { BASE("step_t66_lag4", 66, 1), .lag_mul = 4 },
  { BASE("step_t66_tau0.5", 66, 1), .tau_mul = 0.5 },
  { BASE("step_t66_tau2", 66, 1), .tau_mul = 2 },
  { BASE("step_t66_noise3", 66, 1), .noise = 3 },
  { BASE("step_t85_gain2", 85, 1), .fw_gain = 2 },
  { BASE("step_t85_lag2", 85, 1), .lag_mul = 2 },
  { BASE("step_t85_noise3", 85, 1), .noise = 3 },
  { BASE("step_t91_gain0.5", 91, 1), .fw_gain = 0.5 },
  { BASE("pulses_t66_gain2_lag2", 66, 4), .t1 = 60, .fw_gain = 2, .lag_mul = 2 },
  /* guard */
  { BASE("G_deadman_cool", 66, 0), .dur = 400, .guard = 1, .hang_at = 120, .hang_len = 60 },
  { BASE("G_deadman_hot", 78, 5), .dur = 700, .guard = 1, .hang_at = 300, .hang_len = 200 },
  { BASE("G_pause", 66, 5), .dur = 600, .guard = 1, .pause_at = 200, .pause_len = 120 },
  { BASE("G_broken", 66, 5), .dur = 900, .guard = 1, .broken = 1 },
  { BASE("G_broken_fanfull", 66, 5), .dur = 600, .guard = 1, .broken = 1, .fan_full = 1 },
  { BASE("G_normal_t66", 66, 4), .t1 = 60, .dur = 1200, .guard = 1 },
  { BASE("G_normal_t91", 91, 1), .dur = 1200, .guard = 1 },
};

/* --------------------------------------------------------------------- plant */

#define T_AMB 25.0
#define R_DH  0.15            /* K/W die -> heatsink */
#define C_D   33.0            /* J/K die */
#define C_H   200.0           /* J/K heatsink */

static double g_td, g_th;     /* die, heatsink */
static double g_duty = 20;    /* actual fan duty, percent */
static atomic_int g_thr = 91; /* threshold the firmware holds */
static int g_in_game_prev = -1;

static double G_of(double duty) { return 0.81 + 3.74 * duty / 100.0; }

static double
power_at(int t) {
  switch(S.load_kind) {
    case 0: return 32;
    case 1: return t < S.t1 ? 32 : 100;
    case 2: return t < S.t1 ? 100 : 32;
    case 3: return t < S.t1 ? 32 : t >= S.t2 ? 100 : 32 + 68.0 * (t - S.t1) / (S.t2 - S.t1);
    case 4: return (t / S.t1) % 2 ? 100 : 32;
    default: return 100;
  }
}
static int in_game_at(int t) { return power_at(t) > 60; }

static double
fw_duty(double t_cpu, int thr, int game) {
  if(S.fan_full) return 96;
  double floor = game ? 34 : 20;
  double gap = t_cpu - thr, extra;
  if(gap <= -20)    extra = 0;
  else if(gap <= 0) extra = (20 + gap) * 0.15;
  else              extra = 3 + 5.6 * S.fw_gain * gap;
  double d = floor + extra;
  return d > 100 ? 100 : d;
}

static void
plant_init(void) {
  double P = power_at(0);
  double d = in_game_at(0) ? 34 : 20;
  g_duty = d;
  g_th = T_AMB + P / G_of(d);
  g_td = g_th + P * R_DH;
}

static void
plant_step(int t) {
  int game = in_game_at(t);
  if(g_in_game_prev >= 0 && game != g_in_game_prev) atomic_store(&g_thr, 91);   /* firmware reset */
  g_in_game_prev = game;
  double P = power_at(t);
  const int N = 20;
  double dt = 1.0 / N, tau_f = 2.0 * S.lag_mul;
  for(int i = 0; i < N; i++) {
    double want = fw_duty(g_td, atomic_load(&g_thr), game);
    g_duty += (want - g_duty) * dt / tau_f;
    double q_dh = (g_td - g_th) / R_DH;
    g_td += (P - q_dh) * dt / (C_D * S.tau_mul);
    g_th += (q_dh - G_of(g_duty) * (g_th - T_AMB)) * dt / (C_H * S.tau_mul);
  }
}

static int noisy(double v) {
  double n = ((double)rand() / RAND_MAX * 2 - 1) * S.noise;
  return (int)lround(v + n);
}

/* ------------------------------------------------------------------ lockstep */

static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_cv = PTHREAD_COND_INITIALIZER;
static uint64_t g_vms = 0;
static unsigned long g_tick = 0;
static int g_participants = 1, g_waiting = 0;
static uint64_t g_hung_until = 0;   /* a hung thread due back: no tick until it has rejoined (else the mutex starves it) */
static pthread_t g_fan_thread;

uint64_t ps5tm_mono_ms(void) { return g_vms; }
uint64_t ps5tm_now_ms(void)  { return 1790000000000ull + g_vms; }

static void advance(void);

static void
release_tick(void) {
  advance();
  g_waiting = 0;
  g_tick++;
  pthread_cond_broadcast(&g_cv);
}

unsigned int
sim_sleep(unsigned int s) {
  (void)s;
  pthread_mutex_lock(&g_mu);
  unsigned long my = g_tick;
  if(++g_waiting >= g_participants && !(g_hung_until && g_vms >= g_hung_until)) release_tick();
  else while(g_tick == my) pthread_cond_wait(&g_cv, &g_mu);
  pthread_mutex_unlock(&g_mu);
  return 0;
}

static void
sim_hang(uint64_t until_ms) {
  pthread_mutex_lock(&g_mu);
  g_participants--;
  g_hung_until = until_ms;
  if(g_participants > 0 && g_waiting >= g_participants) release_tick();
  while(g_vms < until_ms) pthread_cond_wait(&g_cv, &g_mu);
  g_participants++;
  g_hung_until = 0;
  if(g_waiting >= g_participants) release_tick();   /* the others were held back for this thread */
  pthread_mutex_unlock(&g_mu);
}

/* ------------------------------------------------------------------- metrics */

#define MAXT 4000
static int  m_avg10[MAXT], m_thr[MAXT], m_duty[MAXT], m_cpu[MAXT], m_safety[MAXT], m_guard[MAXT], m_safelim[MAXT];
static int  m_n = 0;
static int  fan_writes_in_hold = 0, fan_writes = 0, guard_writes = 0;
static int  ev_deadman = -1, ev_deadman_left = -1, ev_hot = -1, ev_hot_left = -1, ev_safety = -1;
static int  hot_events = 0;
static int  guard_thr_seen[256];

static void
advance(void) {
  int t = (int)(g_vms / 1000);
  if(S.pause_at && t == S.pause_at) g_vms += (uint64_t)S.pause_len * 1000;   /* rest mode: nothing ran */
  g_vms += 1000;
  plant_step(t);
  if(m_n < MAXT) {
    m_avg10[m_n]  = g_snapshot.avg_temp_c10;
    m_thr[m_n]    = atomic_load(&g_thr);
    m_duty[m_n]   = (int)lround(g_duty);
    m_cpu[m_n]    = (int)lround(g_td);
    m_safety[m_n] = g_snapshot.safety_active;
    m_guard[m_n]  = ps5tm_fanguard_state();
    m_safelim[m_n]= g_config.safety_temp_c > (unsigned)(g_snapshot.effective_target_c + 4)
                      ? (int)g_config.safety_temp_c : g_snapshot.effective_target_c + 4;
    if(m_guard[m_n] && m_thr[m_n] < 256) guard_thr_seen[m_thr[m_n]]++;
    m_n++;
  }
  if((int)(g_vms / 1000) >= S.dur) {
    extern void report(void);
    report();
    fflush(stdout);
    _exit(0);
  }
}

static double mean_range(const int *a, int from, int to) {
  double s = 0; int n = 0;
  for(int i = from; i < to; i++) { s += a[i]; n++; }
  return n ? s / n : 0;
}

void
report(void) {
  int n = m_n;
  int ev = (S.load_kind == 1 || S.load_kind == 2 || S.load_kind == 3) ? (S.load_kind == 3 ? S.t2 : S.t1) : 60;
  if(ev >= n) ev = 0;
  double fin = mean_range(m_avg10, n - 120, n) / 10.0;
  /* settled: the 30-s mean of the controller's own average stays within 1 K of the final value (1.5 K: the comfort design lets it drift by about 1 K) */
  int settle = 0;
  for(int i = n - 1; i >= ev + 30; i--)
    if(fabs(mean_range(m_avg10, i - 30, i) / 10.0 - fin) > 1.5) { settle = i - ev; break; }
  int reach = -1;   /* first time the 30-s mean is within 1.5 K of the final value */
  for(int i = ev + 30; i < n; i++)
    if(fabs(mean_range(m_avg10, i - 30, i) / 10.0 - fin) <= 1.5) { reach = i - ev; break; }
  double mx = -1;
  for(int i = ev; i < n; i++) if(m_avg10[i] / 10.0 > mx) mx = m_avg10[i] / 10.0;
  /* limit cycle: last 300 s */
  int from = n - 300 < ev ? ev : n - 300;
  int rev = 0, lastdir = 0, dmin = 999, dmax = -1, tmin = 999, tmax = -1;
  for(int i = from + 1; i < n; i++) {
    int d = m_thr[i] - m_thr[i - 1];
    int dir = d > 0 ? 1 : d < 0 ? -1 : 0;
    if(dir && lastdir && dir != lastdir) rev++;
    if(dir) lastdir = dir;
    if(m_duty[i] < dmin) dmin = m_duty[i];
    if(m_duty[i] > dmax) dmax = m_duty[i];
    if(m_avg10[i] < tmin) tmin = m_avg10[i];
    if(m_avg10[i] > tmax) tmax = m_avg10[i];
  }
  int lc = (S.load_kind != 4) && rev >= 4 && dmax - dmin >= 3;
  /* audible changes: a step of 2 % or more from the last heard level */
  int lvl = m_duty[60 < n ? 60 : 0], changes = 0;
  for(int i = 60; i < n; i++) if(abs(m_duty[i] - lvl) >= 2) { changes++; lvl = m_duty[i]; }
  double cpm = changes / ((n - 60) / 60.0);
  int above = 0, safety_s = 0, guard_s = 0, maxcpu = 0;
  for(int i = 0; i < n; i++) {
    if(m_avg10[i] / 10 >= m_safelim[i]) above++;
    safety_s += m_safety[i];
    guard_s += m_guard[i] != 0;
    if(m_cpu[i] > maxcpu) maxcpu = m_cpu[i];
  }
  printf("%-22s reach=%4ds settle=%4ds final=%5.1f ss_err=%+5.1f over_final=%4.1f over_target=%+5.1f maxdie=%3d "
         "lastduty=%d..%d lastT=%.1f..%.1f thr_rev=%d LC=%s chg/min=%.2f above_safety=%ds safety=%ds guard=%ds",
         S.name, reach, settle, fin, fin - S.target, mx - fin, mx - S.target, maxcpu, dmin, dmax, tmin / 10.0, tmax / 10.0,
         rev, lc ? "JA" : "nein", cpm, above, safety_s, guard_s);
  if(S.guard)
    printf(" | deadman@%d left@%d hot@%d(x%d) hot_left@%d fan_writes_in_hold=%d guard_thr:%s%s",
           ev_deadman, ev_deadman_left, ev_hot, hot_events, ev_hot_left, fan_writes_in_hold,
           guard_thr_seen[45] ? " 45" : "", guard_thr_seen[91] ? " 91" : "");
  if(ev_safety >= 0) printf(" safety_mode@%d", ev_safety);
  printf("\n");
  if(getenv("SIMTRACE")) {
    FILE *f = fopen(getenv("SIMTRACE"), "w");
    if(f) {
      fprintf(f, "t,avg,cpu,thr,duty,safety,guard\n");
      for(int i = 0; i < n; i++) fprintf(f, "%d,%.1f,%d,%d,%d,%d,%d\n", i, m_avg10[i] / 10.0, m_cpu[i], m_thr[i], m_duty[i], m_safety[i], m_guard[i]);
      fclose(f);
    }
  }
}

/* --------------------------------------------------------------------- stubs */

void
ps5tm_log(const char *level, const char *code, const char *fmt, ...) {
  int t = (int)(g_vms / 1000);
  if(!strcmp(code, "fanguard_deadman") && ev_deadman < 0) ev_deadman = t;
  if(!strcmp(code, "fanguard_deadman_left") && ev_deadman_left < 0) ev_deadman_left = t;
  if(!strcmp(code, "fanguard_hot")) { if(ev_hot < 0) ev_hot = t; hot_events++; }
  if(!strcmp(code, "fanguard_hot_left") && ev_hot_left < 0) ev_hot_left = t;
  if(!strcmp(code, "safety_mode") && ev_safety < 0) ev_safety = t;
  if(getenv("SIMV")) {
    va_list ap; va_start(ap, fmt);
    fprintf(stderr, "[%5d] %s %s: ", t, level, code);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
  }
}
void ps5tm_notify(const char *fmt, ...) { (void)fmt; }
void ps5tm_log_file_end(const char *why) { (void)why; }
void ps5tm_log_file_crash_signal_safe(int sig) { (void)sig; }
void ps5tm_show_dialog(const char *text) { (void)text; }
void ps5tm_chanlog_sample(const ps5tm_sensors_t *s) { (void)s; }
void ps5tm_thermal_sample(int a, int b, double c, int d, int e, int f, int g) { (void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g; }
void ps5tm_history_tick(int a, int b, int c, int d, int e, int f, int g) { (void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g; }
void ps5tm_pad_lightbar_request(int e, int s) { (void)e; (void)s; }
int  ps5tm_pad_lightbar_supported(void) { return 0; }
int  ps5tm_sony_api_trylock(void) { return 1; }
void ps5tm_sony_api_unlock(void) {}
int  ps5tm_platform_escalate(void) { return 0; }
int  ps5tm_platform_fan_available(void) { return 1; }
const char *ps5tm_platform_fan_message(void) { return "sim"; }
uint32_t ps5tm_platform_firmware(void) { return 0x12000043; }
uint64_t ps5tm_gamestate_age_ms(void) { return 0; }

void
ps5tm_gamestate_get(ps5tm_gamestate_t *out) {
  memset(out, 0, sizeof(*out));
  int t = (int)(g_vms / 1000);
  if(in_game_at(t)) { out->foreground = 1; strcpy(out->title_id, "PPSA00000"); }
  static int hung = 0;
  if(S.hang_at && !hung && t >= S.hang_at && pthread_equal(pthread_self(), g_fan_thread)) {
    hung = 1;
    sim_hang((uint64_t)(S.hang_at + S.hang_len) * 1000);
  }
}

int
ps5tm_platform_read_sensors(ps5tm_sensors_t *out) {
  memset(out, 0, sizeof(*out));
  out->cpu_c = noisy(g_td);           out->cpu_valid = 1;
  out->soc_c = noisy(g_td - 8);       out->soc_valid = 1;
  out->gpu_c = -1;
  int p10 = (int)lround(g_duty * 10);
  out->fan_duty_pct10 = p10;
  out->fan_duty_pct = (p10 + 5) / 10;
  out->fan_duty_valid = 1;
  return 0;
}

int
ps5tm_platform_read_temps(ps5tm_sensors_t *out) {
  return ps5tm_platform_read_sensors(out);
}

int
ps5tm_platform_read_fan_config(unsigned char *o, int *e) {
  memset(o, 0, PS5TM_ICC_FAN_CONFIG_SIZE);
  o[PS5TM_ICC_FAN_TARGET_OFFSET] = (unsigned char)atomic_load(&g_thr);
  if(e) *e = 0;
  return 0;
}

static int
clamp_hw(int c) { return c < 30 ? 30 : c > 91 ? 91 : c; }

int
ps5tm_platform_set_fan_threshold(int c, int *e) {
  if(e) *e = 0;
  if(pthread_equal(pthread_self(), g_fan_thread)) {
    fan_writes++;
    if(ps5tm_fanguard_state() != PS5TM_GUARD_IDLE) fan_writes_in_hold++;
    if(S.broken) c = 91;             /* a regulation that decides wrongly: always the quietest setting */
  } else {
    guard_writes++;
  }
  atomic_store(&g_thr, clamp_hw(c));
  return 0;
}
int ps5tm_platform_set_fan_threshold_signal_safe(int c) { atomic_store(&g_thr, clamp_hw(c)); return 0; }

/* ---------------------------------------------------------------------- main */

int
main(int argc, char **argv) {
  if(argc < 2) {
    for(unsigned i = 0; i < sizeof(SCEN) / sizeof(SCEN[0]); i++) printf("%s\n", SCEN[i].name);
    return 0;
  }
  int found = 0;
  for(unsigned i = 0; i < sizeof(SCEN) / sizeof(SCEN[0]); i++)
    if(!strcmp(SCEN[i].name, argv[1])) { S = SCEN[i]; found = 1; }
  if(!found) { fprintf(stderr, "unknown scenario %s\n", argv[1]); return 2; }
  srand(12345);
  mkdir(PS5TM_DATA_DIR, 0755);
  unlink(FANMODEL_FILE);

  ps5tm_config_defaults(&g_config);
  g_config.mode = PS5TM_MODE_AUTOMATIC;
  g_config.target_temp_c = (unsigned)S.target;
  g_config.profile = S.profile;
  if(S.load_kind == 1 && S.t2) g_config.safety_temp_c = (unsigned)S.t2;   /* step scenarios: t2 = a safety limit set by hand */
  ps5tm_config_clamp(&g_config);

  plant_init();
  g_in_game_prev = in_game_at(0);
  g_fan_thread = pthread_self();
  if(S.guard) {
    g_participants = 2;
    ps5tm_fanguard_start();
  }
  fan_worker(NULL);
  return 0;
}
