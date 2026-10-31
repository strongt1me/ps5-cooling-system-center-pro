/* Independent protection layer for the fan (27.10.2026).
 *
 * The regulation in fan.c already has a safety limit, but it lives in the same
 * thread as the comfort controller: if that thread hangs, or decides wrongly,
 * nothing else looks. This file is the second, separate look. Its own thread
 * reads the sensors itself once a second and only ever does two things:
 *
 *   1. Dead man: the fan loop beats once per pass (ps5tm_fanguard_beat). When
 *      it has not beaten for DEADMAN_S seconds the fan goes back to the console
 *      (neutral threshold) — or to full cooling when it is hot — and stays
 *      there until the loop beats again.
 *
 *   2. Over-temperature: hotter than the loop's own safety limit plus a margin
 *      for HOT_SUSTAIN_S seconds while the fan is not near full speed means the
 *      regulation is not getting it done. The guard then sets full cooling and
 *      holds it; the loop writes nothing meanwhile (ps5tm_fanguard_holding).
 *
 * A crash (SIGSEGV and friends) hands the fan back to the console on the way
 * out, with nothing but open/ioctl/close. SIGKILL cannot be caught.
 *
 * This thread never takes the Sony lock, never shows anything on screen and
 * never waits for the fan loop: it only logs. */

#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ps5tm.h"
#undef pthread_create   /* this file defines the wrapper and calls the real one (also when the simulation includes it after fan.c) */

/* An alternate signal stack per thread (sigaltstack is per thread on FreeBSD), so that on_crash below also runs after a
 * stack overflow. It needs very little: open, ioctl, close, sigaction, raise. */
#define ALT_STACK_BYTES (32 * 1024)

typedef struct {
  void *(*start)(void *);
  void *arg;
} thread_start_t;

static void *
install_alt_stack(void) {
  void *mem = malloc(ALT_STACK_BYTES);
  if(!mem) return NULL;
  stack_t ss;
  memset(&ss, 0, sizeof(ss));
  ss.ss_sp = mem;
  ss.ss_size = ALT_STACK_BYTES;
  if(sigaltstack(&ss, NULL) != 0) {
    free(mem);
    return NULL;
  }
  return mem;
}

static void
drop_alt_stack(void *mem) {
  if(!mem) return;
  stack_t ss;
  memset(&ss, 0, sizeof(ss));
  ss.ss_flags = SS_DISABLE;
  sigaltstack(&ss, NULL);
  free(mem);
}

static void *
thread_trampoline(void *p) {
  thread_start_t ts = *(thread_start_t *)p;
  free(p);
  void *mem = install_alt_stack();
  void *ret;
  pthread_cleanup_push(drop_alt_stack, mem);   /* also when the thread ends with pthread_exit */
  ret = ts.start(ts.arg);
  pthread_cleanup_pop(1);
  return ret;
}

int
ps5tm_thread_create(pthread_t *thread, const pthread_attr_t *attr, void *(*start)(void *), void *arg) {
  thread_start_t *ts = malloc(sizeof(*ts));
  if(!ts) return pthread_create(thread, attr, start, arg);   /* without the extra stack rather than not at all */
  ts->start = start;
  ts->arg = arg;
  int rc = pthread_create(thread, attr, thread_trampoline, ts);
  if(rc != 0) free(ts);
  return rc;
}

#define DEADMAN_S          20      /* a pass normally takes about a second */
#define RESUME_BEATS_S      3      /* the loop beats again for this long before the guard lets go */
#define PAUSE_GAP_MS     5000ull   /* the guard itself stood still: rest mode, nothing to judge */
#define HOT_MARGIN_C        6      /* above the loop's safety limit */
#define HOT_MIN_LIMIT_C    84      /* never fires below this, whatever the safety limit is */
#define HOT_DEFAULT_LIMIT_C 90     /* before the loop has published its limit */
#define HOT_SUSTAIN_S      10
#define HOT_FAN_FULL_PCT   90      /* above this there is nothing left to add */
#define HOT_RELEASE_C       8      /* this far below the limit ... */
#define HOT_HOLD_MIN_S     30      /* ... and held at least this long */
#define REWRITE_S           5      /* the firmware resets the threshold at game start */

static atomic_uint g_beat      = 0;
static atomic_int  g_safety_c  = 0;   /* the loop's safety limit in force, 0 = not yet known */
static atomic_int  g_state     = PS5TM_GUARD_IDLE;
static atomic_int  g_started   = 0;

void ps5tm_fanguard_beat(void)              { atomic_fetch_add(&g_beat, 1); }
void ps5tm_fanguard_publish(int safety_c)   { atomic_store(&g_safety_c, safety_c); }
int  ps5tm_fanguard_state(void)             { return atomic_load(&g_state); }
int  ps5tm_fanguard_holding(void)           { return atomic_load(&g_state) != PS5TM_GUARD_IDLE; }

static int
hot_limit(void) {
  int s = atomic_load(&g_safety_c);
  int lim = s > 0 ? s + HOT_MARGIN_C : HOT_DEFAULT_LIMIT_C;
  return lim < HOT_MIN_LIMIT_C ? HOT_MIN_LIMIT_C : lim;
}

static void
write_threshold(int c) {
  int eno = 0;
  if(ps5tm_platform_set_fan_threshold(c, &eno) != 0)
    PS5TM_ERROR("fanguard_write_failed", "Wächter: Lüftereinstellung %d °C ließ sich nicht schreiben (Fehler %d).", c, eno);
}

static void *
guard_worker(void *arg) {
  (void)arg;
  uint64_t prev        = ps5tm_mono_ms();
  uint64_t last_change = prev;     /* when the beat last moved */
  uint64_t beat_since  = 0;        /* first beat after a dead spell */
  uint64_t hold_since  = 0;
  uint64_t last_write  = 0;
  unsigned last_beat   = atomic_load(&g_beat);
  int      armed       = 0;        /* the loop has beaten at least once */
  int      hot_s       = 0;
  int      held_c      = 0;        /* threshold the guard holds, 0 = none */

  for(;;) {
    sleep(1);
    uint64_t now = ps5tm_mono_ms();
    if(now - prev > PAUSE_GAP_MS) {
      /* Woken from rest mode: every thread stood still, the fan loop's silence says nothing. */
      last_change = now;
      hot_s = 0;
      last_write = 0;
    }
    prev = now;

    unsigned b = atomic_load(&g_beat);
    int beating = (b != last_beat);
    if(beating) {
      last_beat = b;
      last_change = now;
      armed = 1;
      if(!beat_since) beat_since = now;
    }
    int silent_s = (int)((now - last_change) / 1000);
    if(silent_s >= 3) beat_since = 0;   /* a pass a little over a second long skips a tick now and then */

    ps5tm_sensors_t s;
    ps5tm_platform_read_temps(&s);
    int hottest = -1;
    if(s.cpu_valid && s.cpu_c > hottest) hottest = s.cpu_c;
    if(s.soc_valid && s.soc_c > hottest) hottest = s.soc_c;
    int lim = hot_limit();
    int fan_full = s.fan_duty_valid && s.fan_duty_pct >= HOT_FAN_FULL_PCT;

    if(hottest >= lim) hot_s++;
    else               hot_s = 0;

    int state = atomic_load(&g_state);

    /* 1. Dead man. */
    if(armed && silent_s >= DEADMAN_S && state != PS5TM_GUARD_DEADMAN) {
      int hot = (hottest >= 0 && hottest >= lim - HOT_RELEASE_C);
      held_c = hot ? PS5TM_THRESHOLD_MIN_C : PS5TM_NEUTRAL_THRESHOLD_C;
      write_threshold(held_c);
      last_write = now;
      hold_since = now;
      atomic_store(&g_state, PS5TM_GUARD_DEADMAN);
      PS5TM_WARN("fanguard_deadman",
                 "Wächter: Die Lüfterregelung meldet sich seit %d s nicht mehr. %s (%d °C, heißester Sensor %d °C).",
                 silent_s, hot ? "Lüfter auf volle Kühlung" : "Lüfter zurück an die Konsole", held_c, hottest);
      continue;
    }
    if(state == PS5TM_GUARD_DEADMAN) {
      if(beat_since && now - beat_since >= (uint64_t)RESUME_BEATS_S * 1000) {
        atomic_store(&g_state, PS5TM_GUARD_IDLE);
        held_c = 0;
        PS5TM_INFO("fanguard_deadman_left", "Wächter: Die Lüfterregelung läuft wieder und übernimmt.");
        continue;
      }
      /* Still dead: follow the temperature between the two safe settings. */
      int want = (hottest >= 0 && hottest >= lim - HOT_RELEASE_C) ? PS5TM_THRESHOLD_MIN_C
               : (hottest >= 0 && hottest < lim - 2 * HOT_RELEASE_C) ? PS5TM_NEUTRAL_THRESHOLD_C : held_c;
      if(want != held_c || now - last_write >= (uint64_t)REWRITE_S * 1000) {
        held_c = want;
        write_threshold(held_c);
        last_write = now;
      }
      continue;
    }

    /* 2. Over-temperature while the loop runs. */
    if(state == PS5TM_GUARD_IDLE && hot_s >= HOT_SUSTAIN_S && !fan_full) {
      held_c = PS5TM_THRESHOLD_MIN_C;
      write_threshold(held_c);
      last_write = now;
      hold_since = now;
      atomic_store(&g_state, PS5TM_GUARD_HOT);
      PS5TM_WARN("fanguard_hot",
                 "Wächter: %d °C seit %d s über der Grenze von %d °C, der Lüfter läuft nur mit %d %%. "
                 "Volle Kühlung, bis die Konsole unter %d °C ist.",
                 hottest, hot_s, lim, s.fan_duty_valid ? s.fan_duty_pct : -1, lim - HOT_RELEASE_C);
      continue;
    }
    if(state == PS5TM_GUARD_HOT) {
      if(hottest >= 0 && hottest < lim - HOT_RELEASE_C && now - hold_since >= (uint64_t)HOT_HOLD_MIN_S * 1000) {
        atomic_store(&g_state, PS5TM_GUARD_IDLE);
        held_c = 0;
        PS5TM_INFO("fanguard_hot_left", "Wächter: Wieder %d °C, die Regelung übernimmt.", hottest);
        continue;
      }
      if(now - last_write >= (uint64_t)REWRITE_S * 1000) {
        write_threshold(held_c);
        last_write = now;
      }
    }
  }
  return NULL;
}

/* Crash: hand the fan back to the console, then die the way the signal meant. */
static void
on_crash(int sig) {
  ps5tm_platform_set_fan_threshold_signal_safe(PS5TM_NEUTRAL_THRESHOLD_C);
  struct sigaction d;
  memset(&d, 0, sizeof(d));
  d.sa_handler = SIG_DFL;
  sigaction(sig, &d, NULL);
  raise(sig);
}

int
ps5tm_fanguard_start(void) {
  if(atomic_exchange(&g_started, 1)) return 0;

  static const int sigs[] = { SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT };
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = on_crash;
  sa.sa_flags = SA_ONSTACK;                /* on the thread's own signal stack where it has one */
  sigemptyset(&sa.sa_mask);
  for(unsigned i = 0; i < sizeof(sigs) / sizeof(sigs[0]); i++) sigaction(sigs[i], &sa, NULL);
  (void)install_alt_stack();               /* the thread that starts the guard (main) never ends: kept */

  pthread_t t;
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
  int rc = ps5tm_thread_create(&t, &attr, guard_worker, NULL);
  pthread_attr_destroy(&attr);
  if(rc != 0) {
    atomic_store(&g_started, 0);
    PS5TM_ERROR("fanguard_failed", "Der Lüfterwächter ließ sich nicht starten (Fehler %d).", rc);
    return -1;
  }
  PS5TM_INFO("fanguard_started", "Lüfterwächter läuft (Regelung stumm ab %d s, Übertemperatur ab Sicherheitsgrenze + %d °C).",
             DEADMAN_S, HOT_MARGIN_C);
  return 0;
}
