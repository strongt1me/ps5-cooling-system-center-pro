/* Power rails, live clocks and the frame rate: the SoC telemetry beside the
 * temperatures.
 *
 * ── Power, sceKernelGetSocPowerConsumption
 *
 * One argument, a pointer. The call writes 0x70 bytes: eight rails, each three
 * u32 — milliwatts, millivolts, milliamps — and after them four more words.
 * The rail order, as drakmor/ps5-hwinfo prints it (GPL-3.0; the facts are
 * used here, not the code):
 *
 *     0 GPU core    1 GPU I/O    2 CPU + SoC    3 CPU I/O
 *     4..7 GDDR6, channel pairs 0-1, 2-3, 4-5, 6-7
 *
 * Until 1.45.0 this app declared the call with a second, double argument and
 * read the first eight bytes as a single number: rail 0's milliwatts and
 * millivolts glued into one value, which failed the plausibility check every
 * time. The dashboard never showed a watt, and nobody could tell a missing
 * reading from a missing function.
 *
 * The buffer is 256 bytes and zeroed. ps5upload traced the "hangs" other
 * payloads reported for this call to exactly that: prototypes that handed it
 * too little room, and a copyout that ran over the caller's stack.
 *
 * The four words after the rails are recorded and not interpreted. The only
 * public reading (drakmor again) calls bytes 96..99 GFX, CPU, hotspot and
 * peak temperature and the next word the CPU die temperature in m°C — every
 * name with a question mark. The channel recording puts them beside the SoC
 * sensors, where a match would be obvious.
 *
 * ── Clocks, sceKernelGetSocClock and sceKernelGetCpuCoreClock
 *
 * clocks.c takes its figures from the power-mode table SceSystemStateMgr logs
 * on a mode change. That table states what a mode allows, and it only changes
 * with the mode: 2350 MHz for graphics on this PS5 Pro, whatever the GPU is
 * actually doing. The live readings: 26 SoC clock domains, of which 20 is the
 * graphics clock, 23 the memory clock, 24 the fabric clock and 25 a graphics
 * limit (same source, same caveat), plus one figure per physical CPU core.
 * All 26 are kept so the other domains can be identified later.
 *
 * ── Frame rate, /dev/dce
 *
 * The display engine keeps a flip counter. ioctl 0x80308217 with a 0x30-byte
 * argument — selector 0x10000000A, mask 0x8000000000, a pointer to 0x60 bytes
 * of output, three reserved words — puts it at offset 8 of the output. Some
 * syscall paths reject the request code unless it is sign-extended to
 * 0xFFFFFFFF80308217, so an EINVAL is retried that way once and the working
 * form remembered. The protocol is as onionHEN documents it (GPL-3.0; facts
 * only). onionHEN runs inside the shell; whether a payload may open the
 * device at all is the first thing the log answers.
 *
 * Flips per second is what the display shows, which with a game in front is
 * the game's frame rate. Off unless PS5TM_PROBE_FPS is set.
 */

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "ps5tm.h"

/* Weak for the same reason as the sensor calls in platform.c: a firmware
   without one of them must cost that reading, not the whole payload. */
int sceKernelGetSocPowerConsumption(void *out)       __attribute__((weak));
int sceKernelGetSocClock(uint32_t *out_domains)      __attribute__((weak));
int sceKernelGetCpuCoreClock(int *out_mhz_per_core)  __attribute__((weak));

#define REFRESH_MS    2000u   /* power and clocks: at most one call per 2 s  */
#define STALE_MS     10000u   /* older than this and a reading is withdrawn  */

static const char *const g_rail_names[PS5TM_POWER_RAILS] = {
  "GPU Core", "GPU I/O", "CPU + SoC", "CPU I/O",
  "GDDR6 Ch 0-1", "GDDR6 Ch 2-3", "GDDR6 Ch 4-5", "GDDR6 Ch 6-7"
};

const char *
ps5tm_power_rail_name(int rail) {
  return (rail >= 0 && rail < PS5TM_POWER_RAILS) ? g_rail_names[rail] : "?";
}

double
ps5tm_power_sum_w(const ps5tm_power_t *p, int first, int last) {
  if(!p || !p->valid) return -1.0;
  if(first < 0) first = 0;
  if(last >= PS5TM_POWER_RAILS) last = PS5TM_POWER_RAILS - 1;
  double mw = 0.0;
  for(int r = first; r <= last; r++) mw += (double)p->mw[r];
  return mw / 1000.0;
}


/* ------------------------------------------------------------------ power */

static ps5tm_power_t g_power;
static uint64_t      g_power_try_ms;
static int           g_power_logged;

static int
read_power(ps5tm_power_t *p) {
  if(!sceKernelGetSocPowerConsumption) return -1;

  uint32_t buf[64];
  memset(buf, 0, sizeof(buf));
  int rc = sceKernelGetSocPowerConsumption(buf);
  if(rc != 0) return rc;

  /* A layout other than the expected one shows up as absurd numbers long
     before it shows up as anything else: no rail of this console carries
     400 W or 2.5 V, and all of them together stay under 500 W. */
  uint64_t total_mw = 0;
  int any = 0;
  for(int r = 0; r < PS5TM_POWER_RAILS; r++) {
    uint32_t mw = buf[r * 3], mv = buf[r * 3 + 1], ma = buf[r * 3 + 2];
    if(mw > 400000u || mv > 2500u || ma > 400000u) return -2;
    total_mw += mw;
    any |= (mw | mv | ma) != 0;
  }
  if(!any || total_mw == 0 || total_mw > 500000u) return -2;

  memset(p, 0, sizeof(*p));
  for(int r = 0; r < PS5TM_POWER_RAILS; r++) {
    p->mw[r] = buf[r * 3];
    p->mv[r] = buf[r * 3 + 1];
    p->ma[r] = buf[r * 3 + 2];
  }
  memcpy(p->aux_c, (const uint8_t *)buf + 24 * sizeof(uint32_t), 4);
  p->aux_word[0] = buf[25];
  p->aux_word[1] = buf[26];
  p->aux_word[2] = buf[27];
  p->valid = 1;
  return 0;
}

/* Whether the CPU rails are a live reading at all.
 *
 * Measured over an hour of Assassin's Creed Shadows on a PS5 Pro (27.09.2026):
 * the GPU rail took 40 different values and changed at nearly every sample,
 * the memory rails moved a little — and the two CPU rails did not move once,
 * 111.4 W from start to end, as did the four words after the rails. At idle
 * it was 9.4 W, just as fixed. Whatever that figure is — a budget of the
 * power mode, a value latched at the last mode change — it is not a
 * measurement, and presenting it as one would be invented precision. So the
 * rails have to prove themselves: three changes within a minute. */
static uint32_t g_cpu_mw_prev;
static int      g_cpu_seen;
static uint64_t g_cpu_change_ms[3];

static int
cpu_rails_live(const ps5tm_power_t *p, uint64_t now) {
  uint32_t cpu = p->mw[2] + p->mw[3];
  if(!g_cpu_seen) {
    g_cpu_seen = 1;
    g_cpu_mw_prev = cpu;
  } else if(cpu != g_cpu_mw_prev) {
    g_cpu_mw_prev = cpu;
    g_cpu_change_ms[2] = g_cpu_change_ms[1];
    g_cpu_change_ms[1] = g_cpu_change_ms[0];
    g_cpu_change_ms[0] = now;
  }
  return g_cpu_change_ms[2] && now - g_cpu_change_ms[2] < 60000u;
}

static void
update_power(uint64_t now) {
  if(g_power_try_ms && now - g_power_try_ms < REFRESH_MS) return;
  g_power_try_ms = now;

  ps5tm_power_t fresh;
  int rc = read_power(&fresh);
  if(rc == 0) {
    fresh.sampled_ms = now;
    fresh.cpu_live   = cpu_rails_live(&fresh, now);
    g_power = fresh;
  }

  if(g_power_logged) return;
  g_power_logged = 1;
  if(rc == 0) {
    PS5TM_INFO("power_rails",
               "Stromschienen: GPU %.1f W, CPU+SoC %.1f W, Speicher %.1f W, "
               "gesamt %.1f W. Zusatzwerte %u/%u/%u/%u, %u, %u, %u.",
               ps5tm_power_sum_w(&fresh, 0, 1), ps5tm_power_sum_w(&fresh, 2, 3),
               ps5tm_power_sum_w(&fresh, 4, 7), ps5tm_power_sum_w(&fresh, 0, 7),
               (unsigned)fresh.aux_c[0], (unsigned)fresh.aux_c[1],
               (unsigned)fresh.aux_c[2], (unsigned)fresh.aux_c[3],
               (unsigned)fresh.aux_word[0], (unsigned)fresh.aux_word[1],
               (unsigned)fresh.aux_word[2]);
  } else {
    PS5TM_INFO("power_rails",
               "Stromschienen nicht lesbar (%s, Ergebnis %d).",
               sceKernelGetSocPowerConsumption ? "Funktion da" : "Funktion fehlt",
               rc);
  }
}


/* ----------------------------------------------------------------- clocks */

static ps5tm_live_clocks_t g_clk;
static uint64_t            g_clk_try_ms;
static int                 g_clk_logged;

static int
plausible_mhz(long v, long hi) {
  return v >= 100 && v <= hi;
}

static void
update_clocks(uint64_t now) {
  if(g_clk_try_ms && now - g_clk_try_ms < REFRESH_MS) return;
  g_clk_try_ms = now;

  ps5tm_live_clocks_t c;
  memset(&c, 0, sizeof(c));
  int soc_rc = -1, core_rc = -1;

  if(sceKernelGetSocClock) {
    uint32_t dom[64];
    memset(dom, 0, sizeof(dom));
    soc_rc = sceKernelGetSocClock(dom);
    if(soc_rc == 0 && plausible_mhz(dom[20], 4000)) {
      memcpy(c.domain, dom, sizeof(c.domain));
      c.gfx_mhz       = (int)dom[20];
      c.gfx_limit_mhz = plausible_mhz(dom[25], 4000) ? (int)dom[25] : 0;
      c.uclk_mhz      = plausible_mhz(dom[23], 4000) ? (int)dom[23] : 0;
      c.fclk_mhz      = plausible_mhz(dom[24], 4000) ? (int)dom[24] : 0;
      c.valid = 1;
    }
  }

  if(sceKernelGetCpuCoreClock) {
    int mhz[32];
    memset(mhz, 0, sizeof(mhz));
    core_rc = sceKernelGetCpuCoreClock(mhz);
    if(core_rc == 0) {
      int ok = 1;
      for(int i = 0; i < PS5TM_MAX_CORES; i++)
        if(!plausible_mhz(mhz[i], 6000)) ok = 0;
      if(ok) {
        memcpy(c.core_mhz, mhz, sizeof(c.core_mhz));
        c.cores_valid = 1;
      }
    }
  }

  if(c.valid || c.cores_valid) {
    c.sampled_ms = now;
    g_clk = c;
  }

  if(g_clk_logged) return;
  g_clk_logged = 1;
  if(c.valid) {
    /* Room for 26 full-width u32 and their separators, so nothing is cut. */
    char dom_txt[PS5TM_SOC_CLOCK_DOMAINS * 11 + 1];
    size_t n = 0;
    dom_txt[0] = '\0';
    for(int i = 0; i < PS5TM_SOC_CLOCK_DOMAINS; i++) {
      int w = snprintf(dom_txt + n, sizeof(dom_txt) - n, "%s%u",
                       i ? " " : "", (unsigned)c.domain[i]);
      if(w < 0 || (size_t)w >= sizeof(dom_txt) - n) break;
      n += (size_t)w;
    }
    PS5TM_INFO("live_clocks",
               "Takt live: Grafik %d MHz (Grenze %d), Speicher %d, Fabric %d. "
               "Alle 26 Bereiche: %s.",
               c.gfx_mhz, c.gfx_limit_mhz, c.uclk_mhz, c.fclk_mhz, dom_txt);
  } else {
    PS5TM_INFO("live_clocks", "SoC-Takt nicht lesbar (%s, Ergebnis %d).",
               sceKernelGetSocClock ? "Funktion da" : "Funktion fehlt", soc_rc);
  }
  if(c.cores_valid)
    PS5TM_INFO("live_core_clocks",
               "Kerntakt live: %d %d %d %d %d %d %d %d MHz.",
               c.core_mhz[0], c.core_mhz[1], c.core_mhz[2], c.core_mhz[3],
               c.core_mhz[4], c.core_mhz[5], c.core_mhz[6], c.core_mhz[7]);
  else
    PS5TM_INFO("live_core_clocks", "Kerntakt nicht lesbar (%s, Ergebnis %d).",
               sceKernelGetCpuCoreClock ? "Funktion da" : "Funktion fehlt",
               core_rc);
}


/* ------------------------------------------------------------- frame rate */

#define DCE_DEVICE        "/dev/dce"
#define DCE_FLIP_IOCTL    0x0000000080308217ul
#define DCE_FLIP_IOCTL_SX 0xFFFFFFFF80308217ul
#define DCE_SELECTOR      0x000000010000000Aull
#define DCE_MASK          0x0000008000000000ull
#define DCE_OUT_BYTES     0x60
#define DCE_COUNT_OFFSET  8

typedef struct {
  uint64_t selector;
  uint64_t mask;
  uint64_t output;
  uint64_t reserved[3];
} dce_flip_arg_t;

_Static_assert(sizeof(dce_flip_arg_t) == 0x30, "the ioctl encodes 0x30 bytes");

static int      g_dce_fd = -1;
static int      g_dce_state;        /* ps5tm_fps_state_t */
static int      g_dce_errno;
static int      g_dce_sign_extend;
static int      g_dce_logged;
static uint64_t g_flips_prev, g_flips_prev_ms;
static int      g_flips_have_prev;
static ps5tm_fps_t g_fps;

static void
dce_close(void) {
  if(g_dce_fd >= 0) close(g_dce_fd);
  g_dce_fd = -1;
}

static void
dce_log_once(const char *what) {
  if(g_dce_logged) return;
  g_dce_logged = 1;
  PS5TM_INFO("fps_dce", "Bildwechsel-Zähler (/dev/dce): %s", what);
}

static int
dce_read_flips(uint64_t *flips) {
  if(g_dce_state == PS5TM_FPS_REFUSED) return -1;

  if(g_dce_fd < 0) {
    g_dce_fd = open(DCE_DEVICE, O_RDWR);
    if(g_dce_fd < 0) {
      g_dce_errno = errno;
      char msg[96];
      snprintf(msg, sizeof(msg), "lässt sich nicht öffnen (errno %d: %s).",
               g_dce_errno, strerror(g_dce_errno));
      dce_log_once(msg);
      /* Refused for good: asking again once a second changes nothing. */
      g_dce_state = (g_dce_errno == EPERM || g_dce_errno == EACCES ||
                     g_dce_errno == ENOENT)
                      ? PS5TM_FPS_REFUSED : PS5TM_FPS_IOCTL_ERROR;
      return -1;
    }
  }

  uint8_t out[DCE_OUT_BYTES];
  dce_flip_arg_t arg;
  for(int attempt = 0; attempt < 2; attempt++) {
    unsigned long request = g_dce_sign_extend ? DCE_FLIP_IOCTL_SX
                                              : DCE_FLIP_IOCTL;
    memset(out, 0, sizeof(out));
    memset(&arg, 0, sizeof(arg));
    arg.selector = DCE_SELECTOR;
    arg.mask     = DCE_MASK;
    arg.output   = (uint64_t)(uintptr_t)out;

    if(ioctl(g_dce_fd, request, &arg) == 0) {
      memcpy(flips, out + DCE_COUNT_OFFSET, sizeof(*flips));
      g_dce_state = PS5TM_FPS_OK;
      g_dce_errno = 0;
      dce_log_once(g_dce_sign_extend ? "antwortet (vorzeichenerweiterte Anfrage)."
                                     : "antwortet.");
      return 0;
    }

    int e = errno;
    if(e == EINVAL && !g_dce_sign_extend) {
      g_dce_sign_extend = 1;
      continue;
    }
    g_dce_errno = e;
    g_dce_state = PS5TM_FPS_IOCTL_ERROR;
    if(e == EBADF) dce_close();
    char msg[96];
    snprintf(msg, sizeof(msg), "ioctl abgelehnt (errno %d: %s).", e, strerror(e));
    dce_log_once(msg);
    return -1;
  }
  return -1;
}

static void
update_fps(uint64_t now, int enabled, int game_in_front) {
  if(!enabled) {
    dce_close();
    memset(&g_fps, 0, sizeof(g_fps));
    g_flips_have_prev = 0;
    if(g_dce_state != PS5TM_FPS_REFUSED) g_dce_state = PS5TM_FPS_OFF;
    return;
  }

  /* Without a game in front there is nothing to count. The display engine
     answers ENOENT, and its driver logs "[dce-ft:1013 ERR]" under our pid
     every five seconds (26.09.2026, control centre open) — noise in the very
     buffer the game detection and the mic button read. So no ioctl at all. */
  if(!game_in_front) {
    g_fps.valid       = 0;
    g_flips_have_prev = 0;
    g_fps.state       = (g_dce_state == PS5TM_FPS_REFUSED) ? PS5TM_FPS_REFUSED
                                                           : PS5TM_FPS_NO_GAME;
    g_fps.last_errno  = 0;
    return;
  }

  uint64_t flips = 0;
  if(dce_read_flips(&flips) != 0) {
    g_fps.valid = 0;
    g_flips_have_prev = 0;
  } else if(!g_flips_have_prev || flips < g_flips_prev || now <= g_flips_prev_ms) {
    /* First sample, or the counter went backwards (display re-initialised):
       start a fresh interval rather than reporting a nonsense rate. */
    g_flips_prev = flips;
    g_flips_prev_ms = now;
    g_flips_have_prev = 1;
    g_fps.valid = 0;
  } else if(now - g_flips_prev_ms >= 500) {
    double fps = (double)(flips - g_flips_prev) * 1000.0 /
                 (double)(now - g_flips_prev_ms);
    g_fps.valid = (fps >= 0.0 && fps <= 1000.0);
    g_fps.fps   = g_fps.valid ? fps : 0.0;
    g_flips_prev = flips;
    g_flips_prev_ms = now;
  }

  g_fps.flips      = flips;
  g_fps.state      = g_dce_state;
  g_fps.last_errno = g_dce_errno;
}


/* ------------------------------------------------------------------ entry */

/* The three readings as the last pass that got the Sony lock left them.
 *
 * g_power, g_clk and g_fps are written inside that lock, by whichever thread
 * took it. The fan worker must not queue for it — whoever holds it is inside a
 * Sony service, and those have been known not to come back for a long time —
 * so it asks with a try-lock and, when the answer is no, reads these instead:
 * a second old, ten seconds at the very most (STALE_MS withdraws anything
 * older). They have a small mutex of their own, held for a struct copy and
 * never across a call into anything, so reading them cannot wait on a Sony
 * service either. */
static ps5tm_power_t       g_pub_power;
static ps5tm_live_clocks_t g_pub_clk;
static ps5tm_fps_t         g_pub_fps;
static uint64_t            g_pub_fps_ms;   /* when the worker last updated it */
static pthread_mutex_t     g_pub_lock = PTHREAD_MUTEX_INITIALIZER;

void
ps5tm_telemetry_update(ps5tm_sensors_t *out, int with_power_clocks,
                       int with_fps) {
  if(!out) return;

  uint64_t now = ps5tm_now_ms();

  /* Read outside the Sony lock, so no lock is ever taken inside it. */
  int fps_enabled = 0, game_in_front = 0;
  if(with_fps) {
    ps5tm_config_lock();
    fps_enabled = (g_config.probe_mask & PS5TM_PROBE_FPS) != 0;
    ps5tm_config_unlock();
    ps5tm_gamestate_t gs;
    ps5tm_gamestate_get(&gs);
    game_in_front = gs.foreground && gs.title_id[0] && !gs.suspended;
  }

  /* The fan worker and the HTTP thread's one-shot probe can both land here.
     The Sony calls go through the shared lock, and the frame counter only
     moves on the worker's pass, so a request cannot shorten its interval.

     `with_fps` is how the worker identifies itself — it is the one caller that
     sets it (see the declaration) — and the worker never waits for the lock:
     when it is taken the pass goes as always, when it is not this second's
     refresh is skipped and the previous readings stand. The request thread
     may wait. */
  int locked;
  if(with_fps) {
    locked = ps5tm_sony_api_trylock();
  } else {
    ps5tm_sony_api_lock();
    locked = 1;
  }
  if(locked) {
    if(with_power_clocks) {
      update_power(now);
      update_clocks(now);
    }
    if(with_fps) update_fps(now, fps_enabled, game_in_front);

    pthread_mutex_lock(&g_pub_lock);
    g_pub_power = g_power;
    g_pub_clk   = g_clk;
    g_pub_fps   = g_fps;
    if(with_fps) g_pub_fps_ms = now;
    pthread_mutex_unlock(&g_pub_lock);
    ps5tm_sony_api_unlock();
  }

  pthread_mutex_lock(&g_pub_lock);
  if(with_power_clocks) {
    out->power       = g_pub_power;
    out->live_clocks = g_pub_clk;
  }
  out->fps = g_pub_fps;
  /* The rails and clocks carry their own sampling time and are withdrawn
     below once it is too old; the frame rate has none, so a worker that keeps
     missing the lock withdraws it here instead of showing a rate from minutes
     ago. */
  if(with_fps && !locked && out->fps.valid && now - g_pub_fps_ms > STALE_MS)
    out->fps.valid = 0;
  pthread_mutex_unlock(&g_pub_lock);

  if(!with_power_clocks) return;

  if(out->power.valid && now - out->power.sampled_ms > STALE_MS)
    out->power.valid = 0;
  if(now - out->live_clocks.sampled_ms > STALE_MS) {
    out->live_clocks.valid = 0;
    out->live_clocks.cores_valid = 0;
  }

  /* The old single figure becomes what demonstrably moves — GPU and memory,
     and the CPU rails only once they have proved to be a reading. */
  double live = -1.0;
  if(out->power.valid) {
    live = ps5tm_power_sum_w(&out->power, 0, 1) +
           ps5tm_power_sum_w(&out->power, 4, 7);
    if(out->power.cpu_live) live += ps5tm_power_sum_w(&out->power, 2, 3);
  }
  out->soc_power_valid = live > 0.0;
  out->soc_power_w     = out->soc_power_valid ? live : 0.0;
}
