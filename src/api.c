/* REST surface consumed by the bundled web UI, under /api/v1. */

#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "third_party/libdeflate/libdeflate.h"
#include "ps5tm.h"
#include "third_party/cJSON.h"

#define JB(v) ((v) ? "true" : "false")

/* Tile state is cached: the dashboard polls once per second and probing the
   installer on every tick would be wasteful. */
static ps5tm_tile_state_t g_tile_state = PS5TM_TILE_UNKNOWN;
static char               g_tile_message[256] = "Kachelstatus wird geprüft.";
static pthread_mutex_t    g_tile_lock = PTHREAD_MUTEX_INITIALIZER;


void
ps5tm_api_tile_refresh(void) {
  char msg[256];
  ps5tm_tile_state_t st = ps5tm_tile_query(msg, sizeof(msg));
  pthread_mutex_lock(&g_tile_lock);
  g_tile_state = st;
  snprintf(g_tile_message, sizeof(g_tile_message), "%s", msg);
  pthread_mutex_unlock(&g_tile_lock);
}


/* ------------------------------------------------------------------ helpers */

static void
json_escape(const char *in, char *out, size_t out_len) {
  size_t o = 0;
  for(size_t i = 0; in && in[i] && o + 7 < out_len; i++) {
    unsigned char c = (unsigned char)in[i];
    if(c == '"' || c == '\\') { out[o++] = '\\'; out[o++] = (char)c; }
    else if(c == '\n')        { out[o++] = '\\'; out[o++] = 'n';     }
    else if(c == '\r')        { out[o++] = '\\'; out[o++] = 'r';     }
    else if(c == '\t')        { out[o++] = '\\'; out[o++] = 't';     }
    else if(c < 0x20)         { o += (size_t)snprintf(out + o, out_len - o,
                                                      "\\u%04x", c); }
    else                      { out[o++] = (char)c; }
  }
  out[o] = 0;
}

/* The id a game rule is stored under. Real title ids are four capitals and
   five digits, but homebrew and old dumps stray from that, so this only asks
   for what the rest of the code relies on: it fits the field and stays out of
   the way of the settings file and the JSON answers. */
static int
rule_id_ok(const char *s) {
  size_t n = s ? strlen(s) : 0;
  if(n == 0 || n >= sizeof(((ps5tm_game_rule_t *)0)->title_id)) return 0;
  for(size_t i = 0; i < n; i++) {
    unsigned char c = (unsigned char)s[i];
    if(!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
         (c >= 'a' && c <= 'z') || c == '_' || c == '-')) return 0;
  }
  return 1;
}

/* ------------------------------------------------------------------- status */

/* ---------------------------------------------- telemetry (1.45.0) */

/* Logical CPUs and the game/system split, under an existing load object. */
static void
add_cpu_detail(cJSON *l, const ps5tm_sensors_t *s) {
  cJSON *cpus = cJSON_AddArrayToObject(l, "cpus");
  for(int i = 0; i < s->cpu_count && i < PS5TM_MAX_CPUS; i++)
    cJSON_AddItemToArray(cpus, cJSON_CreateNumber(s->cpu_pct[i]));
  cJSON_AddNumberToObject(l, "system_cpu_mask", s->system_cpu_mask);
  if(s->game_load_valid)
    cJSON_AddNumberToObject(l, "game_pct", s->game_load_pct);
  if(s->system_load_valid)
    cJSON_AddNumberToObject(l, "system_pct", s->system_load_pct);
}

/* Live clocks, as opposed to the power-mode table under "clocks". */
static void
add_live_clocks(cJSON *l, const ps5tm_sensors_t *s) {
  const ps5tm_live_clocks_t *c = &s->live_clocks;
  if(!c->valid && !c->cores_valid) return;
  cJSON *k = cJSON_AddObjectToObject(l, "live_clocks");
  if(c->valid) {
    cJSON_AddNumberToObject(k, "gfx_mhz", c->gfx_mhz);
    if(c->gfx_limit_mhz) cJSON_AddNumberToObject(k, "gfx_limit_mhz", c->gfx_limit_mhz);
    if(c->uclk_mhz)      cJSON_AddNumberToObject(k, "mem_mhz",       c->uclk_mhz);
    if(c->fclk_mhz)      cJSON_AddNumberToObject(k, "fabric_mhz",    c->fclk_mhz);
    cJSON *d = cJSON_AddArrayToObject(k, "domains");
    for(int i = 0; i < PS5TM_SOC_CLOCK_DOMAINS; i++)
      cJSON_AddItemToArray(d, cJSON_CreateNumber(c->domain[i]));
  }
  if(c->cores_valid) {
    cJSON *cores = cJSON_AddArrayToObject(k, "core_mhz");
    for(int i = 0; i < PS5TM_MAX_CORES; i++)
      cJSON_AddItemToArray(cores, cJSON_CreateNumber(c->core_mhz[i]));
  }
  cJSON_AddNumberToObject(k, "age_s",
    c->sampled_ms ? (double)((ps5tm_now_ms() - c->sampled_ms) / 1000) : -1);
}

/* The power rails. Power, not utilisation: nothing on the PS5 counts GPU
   busy time, and a watt figure is what there honestly is. */
static void
add_power(cJSON *root, const ps5tm_sensors_t *s) {
  const ps5tm_power_t *p = &s->power;
  cJSON *o = cJSON_AddObjectToObject(root, "power");
  cJSON_AddBoolToObject(o, "valid", p->valid);
  if(!p->valid) return;
  cJSON_AddNumberToObject(o, "total_w", ps5tm_power_sum_w(p, 0, PS5TM_POWER_RAILS - 1));
  cJSON_AddNumberToObject(o, "gpu_w",   ps5tm_power_sum_w(p, 0, 1));
  cJSON_AddNumberToObject(o, "cpu_w",   ps5tm_power_sum_w(p, 2, 3));
  cJSON_AddNumberToObject(o, "mem_w",   ps5tm_power_sum_w(p, 4, 7));
  /* The CPU rails stood still for an hour on the PS5 Pro — see telemetry.c.
     live_w is what demonstrably moves: GPU and memory, plus the CPU rails
     only once they have proved to be a reading. total_w stays the plain sum
     of all eight, for anyone who wants it, but it is not a measurement. */
  cJSON_AddBoolToObject  (o, "cpu_live", p->cpu_live);
  cJSON_AddNumberToObject(o, "live_w",
    ps5tm_power_sum_w(p, 0, 1) + ps5tm_power_sum_w(p, 4, 7) +
    (p->cpu_live ? ps5tm_power_sum_w(p, 2, 3) : 0.0));
  cJSON *rails = cJSON_AddArrayToObject(o, "rails");
  for(int r = 0; r < PS5TM_POWER_RAILS; r++) {
    cJSON *e = cJSON_CreateObject();
    cJSON_AddStringToObject(e, "name", ps5tm_power_rail_name(r));
    cJSON_AddNumberToObject(e, "w", p->mw[r] / 1000.0);
    cJSON_AddNumberToObject(e, "v", p->mv[r] / 1000.0);
    cJSON_AddNumberToObject(e, "a", p->ma[r] / 1000.0);
    cJSON_AddItemToArray(rails, e);
  }
  /* Uninterpreted, see telemetry.c. */
  cJSON *aux = cJSON_AddObjectToObject(o, "aux");
  cJSON *b = cJSON_AddArrayToObject(aux, "bytes");
  for(int i = 0; i < 4; i++) cJSON_AddItemToArray(b, cJSON_CreateNumber(p->aux_c[i]));
  cJSON *w = cJSON_AddArrayToObject(aux, "words");
  for(int i = 0; i < 3; i++) cJSON_AddItemToArray(w, cJSON_CreateNumber(p->aux_word[i]));
  cJSON_AddNumberToObject(o, "age_s",
    p->sampled_ms ? (double)((ps5tm_now_ms() - p->sampled_ms) / 1000) : -1);
}

static void
add_fps(cJSON *root, const ps5tm_sensors_t *s) {
  static const char *names[] = { "aus", "ok", "verweigert", "fehler", "kein Spiel" };
  const ps5tm_fps_t *f = &s->fps;
  ps5tm_config_lock();
  int enabled = (g_config.probe_mask & PS5TM_PROBE_FPS) != 0;
  ps5tm_config_unlock();

  cJSON *o = cJSON_AddObjectToObject(root, "fps");
  cJSON_AddBoolToObject(o, "enabled", enabled);
  cJSON_AddStringToObject(o, "state",
    (f->state >= 0 && f->state <= 4) ? names[f->state] : "aus");
  cJSON_AddBoolToObject(o, "valid", f->valid);
  if(f->valid) cJSON_AddNumberToObject(o, "fps", f->fps);
  cJSON_AddNumberToObject(o, "flips", (double)f->flips);
  if(f->last_errno) cJSON_AddNumberToObject(o, "errno", f->last_errno);
}


static void
handle_status(int fd) {
  ps5tm_snapshot_t snap;
  ps5tm_fan_snapshot(&snap);

  uint32_t fw = ps5tm_platform_firmware();
  int fw_ok   = ps5tm_platform_firmware_recognized(fw);

  pthread_mutex_lock(&g_tile_lock);
  ps5tm_tile_state_t tile_state = g_tile_state;
  char tile_msg[256];
  snprintf(tile_msg, sizeof(tile_msg), "%s", g_tile_message);
  pthread_mutex_unlock(&g_tile_lock);

  const char *sensors_adapter =
      (snap.sensors.cpu_valid || snap.sensors.soc_valid)
        ? PS5TM_ADAPTER_READY : PS5TM_ADAPTER_UNAVAILABLE;
  const char *fan_adapter =
      snap.fan_available ? PS5TM_ADAPTER_READY : PS5TM_ADAPTER_UNAVAILABLE;

  char sensors_msg[192];
  if(snap.sensors.cpu_valid || snap.sensors.soc_valid) {
    snprintf(sensors_msg, sizeof(sensors_msg), "Sensoren werden gelesen.");
  } else {
    snprintf(sensors_msg, sizeof(sensors_msg),
             "CPU-Temperatur konnte nicht sicher gelesen werden.");
  }

  const ps5tm_sensors_t *s = &snap.sensors;

  /* Channel 7 is called the graphics side only on the model it was measured
     on; elsewhere it stays one of the raw channels. */
  ps5tm_sysinfo_t sysinfo;
  ps5tm_sysinfo_get(&sysinfo);
  int gpu_known = ps5tm_sysinfo_gpu_channel_known(&sysinfo) && s->gpu_valid;

  cJSON *root = cJSON_CreateObject();
  cJSON_AddBoolToObject(root, "ok", 1);
  cJSON_AddNumberToObject(root, "timestamp_ms",
      (double)(snap.sampled_at_ms ? snap.sampled_at_ms : ps5tm_now_ms()));

  /* ---- temperatures ---- */
  {
    cJSON *t = cJSON_AddObjectToObject(root, "temperatures");
    cJSON_AddNumberToObject(t, "cpu_c", s->cpu_c);
    cJSON_AddBoolToObject  (t, "cpu_valid", s->cpu_valid);
    cJSON_AddNumberToObject(t, "soc_c", s->soc_c);
    cJSON_AddBoolToObject  (t, "soc_valid", s->soc_valid);
    /* Since 1.45.1, in place of vrm_c and m2_c: channels 1 and 2 are
       neither a regulator nor the M.2 drive, see platform.c. */
    cJSON_AddNumberToObject(t, "gpu_c", gpu_known ? s->gpu_c : -1);
    cJSON_AddBoolToObject  (t, "gpu_valid", gpu_known);

    int hottest = -1;
    if(s->cpu_valid && s->cpu_c > hottest) hottest = s->cpu_c;
    if(s->soc_valid && s->soc_c > hottest) hottest = s->soc_c;
    cJSON_AddNumberToObject(t, "hottest_c", hottest);

    /* Only the channels that mean something to a reader are published. Since
     * 1.45.1 that is channel 7, and only on the model it was measured on.
     *
     * Channels 0-2 were published under names until then: "Hauptchip,
     * gedämpft", "Stromversorgung", "Zusatz-SSD". The measurement of 26 and
     * 27.09.2026 refuted all three (platform.c). They are raw channels again,
     * shown by /api/v1/channels and the diagnostics view.
     *
     * ⚠ An older comment here claimed channels 3-7 "track the named ones
     * closely". Measured on 01.08.2026, that holds at idle only; under load
     * the eight spread by 10.5 °C and no pair keeps a constant difference.
     * They are eight separate sensors. */
    cJSON *ch = cJSON_AddArrayToObject(t, "channels");
    if(gpu_known) {
      cJSON *e = cJSON_CreateObject();
      cJSON_AddNumberToObject(e, "channel", 7);
      cJSON_AddStringToObject(e, "label", "Grafikeinheit");
      cJSON_AddNumberToObject(e, "temp_c", s->gpu_c);
      cJSON_AddItemToArray(ch, e);
    }
  }

  /* ---- load ---- */
  {
    cJSON *l = cJSON_AddObjectToObject(root, "load");
    cJSON_AddNumberToObject(l, "cpu_pct", s->cpu_load_pct);
    cJSON_AddBoolToObject  (l, "cpu_valid", s->cpu_load_valid);
    cJSON_AddBoolToObject  (l, "gpu_valid", s->gpu_load_valid);

    /* Physical cores since 1.45.0 (the mean of each core's two logical
       CPUs); before, these were logical CPUs 0..7 under the wrong name. */
    cJSON *cores = cJSON_AddArrayToObject(l, "cores");
    for(int i = 0; i < s->core_count && i < PS5TM_MAX_CORES; i++)
      cJSON_AddItemToArray(cores, cJSON_CreateNumber(s->core_pct[i]));
    add_cpu_detail(l, s);
    add_live_clocks(l, s);

    if(s->cpu_mhz_valid) cJSON_AddNumberToObject(l, "cpu_mhz", (double)s->cpu_mhz);
    if(s->soc_power_valid)
      cJSON_AddNumberToObject(l, "soc_power_w", s->soc_power_w);
    /* Clock rates from the console's own power-state table: the per-core
       figures and the graphics clock, neither of which the syscalls provide.
       Event-driven, so the age goes with them. */
    {
      ps5tm_clocks_t ck;
      ps5tm_clocks_get(&ck);
      if(ck.valid) {
        cJSON *k = cJSON_AddObjectToObject(l, "clocks");
        cJSON *cores = cJSON_AddArrayToObject(k, "core_mhz");
        for(int i = 0; i < ck.core_count; i++)
          cJSON_AddItemToArray(cores, cJSON_CreateNumber(ck.core_mhz[i]));
        cJSON_AddNumberToObject(k, "gfx_mhz",    ck.gfx_mhz);
        cJSON_AddNumberToObject(k, "mem_mhz",    ck.mem_mhz);
        cJSON_AddNumberToObject(k, "fabric_mhz", ck.fabric_mhz);
        cJSON_AddBoolToObject  (k, "bapm",       ck.bapm_on);
        if(ck.pcie_gen[0]) cJSON_AddStringToObject(k, "pcie", ck.pcie_gen);
        cJSON_AddNumberToObject(k, "age_s",
          ck.seen_ms ? (double)((ps5tm_now_ms() - ck.seen_ms) / 1000) : -1);
      }
    }

    /* The raw value only. There used to be a label here — 0 "Normal",
     * 1 "Boost", 2 "Spiel" — taken from other payloads and never verified.
     * The console showed "Boost" at 800 MHz, which cannot both be true, and a
     * value of 4 has been seen as well, so the mapping covers less than it
     * claims. A wrong word is worse than no word: it invites decisions.
     *
     * The number stays, so the meaning can still be worked out later by
     * watching it under known conditions. It is deliberately not shown in the
     * UI — see app.js. */
    if(s->cpu_mode_valid)
      cJSON_AddNumberToObject(l, "cpu_mode", s->cpu_mode);
  }

  /* ---- power rails and frame rate (1.45.0) ---- */
  add_power(root, s);
  add_fps(root, s);

  /* ---- fan ---- */
  {
    cJSON *f = cJSON_AddObjectToObject(root, "fan");
    cJSON_AddNumberToObject(f, "threshold_c",       snap.applied_threshold_c);
    cJSON_AddNumberToObject(f, "target_duty_pct",   snap.target_duty_pct);
    cJSON_AddNumberToObject(f, "measured_duty_pct", s->fan_duty_pct);
    cJSON_AddBoolToObject  (f, "measured_valid",    s->fan_duty_valid);
    cJSON_AddNumberToObject(f, "measured_duty_raw", s->fan_duty_raw);
    cJSON_AddBoolToObject  (f, "automatic",         snap.automatic);
    cJSON_AddBoolToObject  (f, "available",         snap.fan_available);

    /* Read back from the controller rather than remembered from our own last
       write — the two differ exactly when the console has overruled us. */
    cJSON_AddBoolToObject  (f, "readback_available", snap.readback_available);
    if(snap.readback_available)
      cJSON_AddNumberToObject(f, "observed_threshold_c",
                              snap.observed_threshold_c);
    cJSON_AddNumberToObject(f, "override_count",   (double)snap.override_count);
    if(snap.override_count)
      cJSON_AddNumberToObject(f, "last_override_c", snap.last_override_c);
  }

  /* ---- what the controller is thinking ---- */
  {
    ps5tm_config_lock();
    unsigned target  = g_config.target_temp_c;
    unsigned dead    = g_config.deadband_c;
    unsigned safety  = g_config.safety_temp_c;
    const char *prof = g_config.profile == PS5TM_PROFILE_COOL     ? "cool"
                     : g_config.profile == PS5TM_PROFILE_BALANCED ? "balanced"
                                                                  : "comfort";
    ps5tm_config_unlock();

    cJSON *c = cJSON_AddObjectToObject(root, "control");
    cJSON_AddNumberToObject(c, "target_temp_c", target);
    cJSON_AddNumberToObject(c, "deadband_c",    dead);
    cJSON_AddNumberToObject(c, "safety_temp_c", safety);
    cJSON_AddStringToObject(c, "profile",       prof);
    cJSON_AddNumberToObject(c, "avg_temp_c10",  snap.avg_temp_c10);
    cJSON_AddNumberToObject(c, "trend_c100",    snap.trend_c100);
    cJSON_AddBoolToObject  (c, "safety_active", snap.safety_active);
    cJSON_AddNumberToObject(c, "samples",       snap.samples);
    /* What the controller is really aiming at — a per-game rule may have
       replaced the configured target. */
    cJSON_AddNumberToObject(c, "effective_target_c", snap.effective_target_c);
    cJSON_AddStringToObject(c, "active_game_rule",   ps5tm_fan_active_rule());
    cJSON_AddBoolToObject  (c, "warning_active", snap.warning_active != 0);
    cJSON_AddNumberToObject(c, "warning_since_ms",
                            (double)snap.warning_since_ms);
    cJSON_AddNumberToObject(c, "warning_countdown_s",
                            (double)snap.warning_countdown_s);

    cJSON *lb = cJSON_AddObjectToObject(c, "lightbar");
    cJSON_AddBoolToObject  (lb, "enabled",   snap.lightbar_enabled != 0);
    cJSON_AddBoolToObject  (lb, "supported", snap.lightbar_supported != 0);
    cJSON_AddNumberToObject(lb, "state",     snap.lightbar_state);
    cJSON_AddStringToObject(lb, "state_label",
      snap.lightbar_state >= 2 ? "hot"
      : snap.lightbar_state == 1 ? "warn" : "ok");
  }

  /* ---- what is running ---- */
  {
    ps5tm_gamestate_t gs;
    ps5tm_gamestate_get(&gs);

    cJSON *g = cJSON_AddObjectToObject(root, "game");
    cJSON_AddStringToObject(g, "state", gs.state);
    cJSON_AddBoolToObject  (g, "foreground", gs.foreground);
    cJSON_AddBoolToObject  (g, "suspended",  gs.suspended);
    /* Which route answered, so a wrong name can be traced to its source. */
    cJSON_AddStringToObject(g, "source", gs.source ? gs.source : "none");
    /* True once the focus line has scrolled out of the kernel buffer and the
       last one seen stands in for it — normal after half an hour of play,
       and said rather than hidden. */
    cJSON_AddBoolToObject  (g, "focus_remembered", gs.focus_remembered);
    /* The name is emitted whenever there is one, the id only when it is real.
       This firmware never writes the title path into the kernel buffer, so a
       running game has a name ("Unbekannter Titel (0x2018)") but no id — and
       the old condition dropped both, leaving the UI with a game it knew was
       running and nothing to call it. */
    if(gs.title_name[0])
      cJSON_AddStringToObject(g, "title_name", gs.title_name);
    if(gs.title_id[0]) {
      cJSON_AddStringToObject(g, "title_id",   gs.title_id);
      if(gs.title_version[0])
        cJSON_AddStringToObject(g, "title_version", gs.title_version);
    }
  }

  /* ---- warnings, adapters, messages ---- */
  {
    cJSON *w = cJSON_AddObjectToObject(root, "warnings");
    cJSON_AddBoolToObject(w, "cpu", snap.warning_cpu);
    cJSON_AddBoolToObject(w, "soc", snap.warning_soc);

    /* Our own process id. A newly sent payload asks the instance already
       holding the port for this, so it can stop exactly that process instead
       of guessing at process names — see takeover_port() in main.c. */
    cJSON_AddNumberToObject(root, "pid", (double)getpid());

    cJSON *a = cJSON_AddObjectToObject(root, "adapters");
    cJSON_AddStringToObject(a, "sensors", sensors_adapter);
    cJSON_AddStringToObject(a, "fan",     fan_adapter);
    cJSON_AddStringToObject(a, "tile",    ps5tm_tile_state_name(tile_state));

    cJSON *m = cJSON_AddObjectToObject(root, "messages");
    cJSON_AddStringToObject(m, "sensors", sensors_msg);
    cJSON_AddStringToObject(m, "fan",     ps5tm_platform_fan_message());
    cJSON_AddStringToObject(m, "tile",    tile_msg);

    cJSON *fwo = cJSON_AddObjectToObject(root, "firmware");
    cJSON_AddNumberToObject(fwo, "raw_version", (double)fw);
    cJSON_AddBoolToObject  (fwo, "recognized",  fw_ok);
    cJSON_AddStringToObject(fwo, "group",       ps5tm_platform_firmware_group(fw));
  }

  char *txt = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if(!txt) {
    ps5tm_http_send_error(fd, 500, "alloc_failed", "Kein Speicher.");
    return;
  }
  ps5tm_http_send(fd, 200, "OK", "application/json", txt, strlen(txt), NULL);
  free(txt);
}


/* ----------------------------------------------------------------- history */

static void
handle_history(int fd) {
  enum { MAX_POINTS = 1440 };
  ps5tm_history_entry_t *rows = malloc(sizeof(*rows) * MAX_POINTS);
  if(!rows) {
    ps5tm_http_send_error(fd, 500, "alloc_failed", "Kein Speicher.");
    return;
  }

  ps5tm_history_peaks_t peaks;
  unsigned n = ps5tm_history_snapshot(rows, MAX_POINTS, &peaks);

  cJSON *root = cJSON_CreateObject();
  cJSON_AddBoolToObject(root, "ok", 1);

  cJSON *pk = cJSON_AddObjectToObject(root, "peaks");
  cJSON_AddNumberToObject(pk, "cpu_c",   peaks.cpu_c);
  cJSON_AddNumberToObject(pk, "soc_c",   peaks.soc_c);
  cJSON_AddNumberToObject(pk, "fan_pct", peaks.fan_pct);
  if(peaks.cpu_at_ms)
    cJSON_AddNumberToObject(pk, "cpu_at_ms", (double)peaks.cpu_at_ms);

  /* Flat parallel arrays rather than an array of objects: a day of samples
     is 1440 points and the object form roughly triples the payload. */
  cJSON *t   = cJSON_AddArrayToObject(root, "t_ms");
  cJSON *cpu = cJSON_AddArrayToObject(root, "cpu_c");
  cJSON *soc = cJSON_AddArrayToObject(root, "soc_c");
  cJSON *fan = cJSON_AddArrayToObject(root, "fan_pct");
  for(unsigned i = 0; i < n; i++) {
    cJSON_AddItemToArray(t,   cJSON_CreateNumber((double)rows[i].t_ms));
    cJSON_AddItemToArray(cpu, cJSON_CreateNumber(rows[i].cpu_c));
    cJSON_AddItemToArray(soc, cJSON_CreateNumber(rows[i].soc_c));
    cJSON_AddItemToArray(fan, cJSON_CreateNumber(rows[i].fan_pct));
  }
  cJSON_AddNumberToObject(root, "count", n);
  free(rows);

  char *txt = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if(!txt) {
    ps5tm_http_send_error(fd, 500, "alloc_failed", "Kein Speicher.");
    return;
  }
  ps5tm_http_send(fd, 200, "OK", "application/json", txt, strlen(txt), NULL);
  free(txt);
}


/* ------------------------------------------------------------- game rules */

static const char *
profile_name(ps5tm_profile_t p) {
  return p == PS5TM_PROFILE_COOL     ? "cool"
       : p == PS5TM_PROFILE_BALANCED ? "balanced" : "comfort";
}

static ps5tm_profile_t
profile_from(const char *s) {
  if(s && !strcmp(s, "cool"))     return PS5TM_PROFILE_COOL;
  if(s && !strcmp(s, "balanced")) return PS5TM_PROFILE_BALANCED;
  return PS5TM_PROFILE_COMFORT;
}

static void
handle_game_rules_get(int fd) {
  cJSON *root = cJSON_CreateObject();
  cJSON_AddBoolToObject(root, "ok", 1);
  cJSON_AddStringToObject(root, "active", ps5tm_fan_active_rule());

  cJSON *arr = cJSON_AddArrayToObject(root, "rules");
  ps5tm_config_lock();
  for(unsigned i = 0; i < g_config.game_rule_count; i++) {
    const ps5tm_game_rule_t *g = &g_config.game_rules[i];
    cJSON *e = cJSON_CreateObject();
    cJSON_AddStringToObject(e, "title_id",      g->title_id);
    cJSON_AddStringToObject(e, "title_name",    g->title_name);
    cJSON_AddNumberToObject(e, "target_temp_c", g->target_temp_c);
    cJSON_AddStringToObject(e, "profile",       profile_name(g->profile));
    cJSON_AddItemToArray(arr, e);
  }
  ps5tm_config_unlock();

  char *txt = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if(!txt) { ps5tm_http_send_error(fd, 500, "alloc_failed", "Kein Speicher."); return; }
  ps5tm_http_send(fd, 200, "OK", "application/json", txt, strlen(txt), NULL);
  free(txt);
}

static void
handle_game_rules_export(int fd) {
  cJSON *root = cJSON_CreateObject();
  cJSON_AddBoolToObject(root, "ok", 1);
  cJSON_AddStringToObject(root, "format", "ps5tm-game-rules-v1");
  cJSON_AddNumberToObject(root, "exported_at_ms", (double)ps5tm_now_ms());

  cJSON *arr = cJSON_AddArrayToObject(root, "rules");
  ps5tm_config_lock();
  for(unsigned i = 0; i < g_config.game_rule_count; i++) {
    const ps5tm_game_rule_t *g = &g_config.game_rules[i];
    cJSON *e = cJSON_CreateObject();
    cJSON_AddStringToObject(e, "title_id",      g->title_id);
    cJSON_AddStringToObject(e, "title_name",    g->title_name);
    cJSON_AddNumberToObject(e, "target_temp_c", g->target_temp_c);
    cJSON_AddStringToObject(e, "profile",       profile_name(g->profile));
    cJSON_AddItemToArray(arr, e);
  }
  ps5tm_config_unlock();

  char *txt = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if(!txt) {
    ps5tm_http_send_error(fd, 500, "alloc_failed", "Kein Speicher.");
    return;
  }
  ps5tm_http_send(fd, 200, "OK", "application/json", txt, strlen(txt),
                  "Content-Disposition: attachment; "
                  "filename=ps5tm-game-rules.json\r\n");
  free(txt);
}

static void
handle_game_rules_import(int fd, const ps5tm_request_t *req) {
  cJSON *root = req->body ? cJSON_Parse(req->body) : NULL;
  const cJSON *arr = root ? cJSON_GetObjectItem(root, "rules") : NULL;
  if(!cJSON_IsArray(arr)) {
    cJSON_Delete(root);
    ps5tm_http_send_error(fd, 400, "invalid_rules", "rules-Array fehlt.");
    return;
  }

  ps5tm_game_rule_t tmp[PS5TM_MAX_GAME_RULES];
  unsigned n = 0;
  const cJSON *e;
  cJSON_ArrayForEach(e, arr) {
    if(n >= PS5TM_MAX_GAME_RULES) break;
    const cJSON *tid = cJSON_GetObjectItem(e, "title_id");
    if(!cJSON_IsString(tid) || !rule_id_ok(tid->valuestring)) continue;

    memset(&tmp[n], 0, sizeof(tmp[n]));
    snprintf(tmp[n].title_id, sizeof(tmp[n].title_id), "%s", tid->valuestring);

    const cJSON *nm = cJSON_GetObjectItem(e, "title_name");
    if(cJSON_IsString(nm))
      ps5tm_copy_utf8(tmp[n].title_name, sizeof(tmp[n].title_name), nm->valuestring);

    const cJSON *tt = cJSON_GetObjectItem(e, "target_temp_c");
    unsigned v = cJSON_IsNumber(tt) ? ps5tm_num_u32(tt->valuedouble) : g_config.target_temp_c;
    if(v < PS5TM_TARGET_MIN_C) v = PS5TM_TARGET_MIN_C;
    if(v > PS5TM_TARGET_MAX_C) v = PS5TM_TARGET_MAX_C;
    tmp[n].target_temp_c = v;

    const cJSON *pf = cJSON_GetObjectItem(e, "profile");
    tmp[n].profile = profile_from(cJSON_IsString(pf) ? pf->valuestring : NULL);
    n++;
  }
  cJSON_Delete(root);

  ps5tm_config_lock();
  memset(g_config.game_rules, 0, sizeof(g_config.game_rules));
  for(unsigned i = 0; i < n; i++) g_config.game_rules[i] = tmp[i];
  g_config.game_rule_count = n;
  ps5tm_config_unlock();
  int saved = (ps5tm_config_save() == 0);

  if(!saved) {
    ps5tm_http_send_error(fd, 500, "config_save_failed",
                          "Import konnte nicht gespeichert werden.");
    return;
  }

  char body[96];
  snprintf(body, sizeof(body), "{\"ok\":true,\"imported\":%u}", n);
  ps5tm_http_send_json(fd, 200, body);
}

/* Adds or replaces the rule for one title; an empty body removes it. */
static void
handle_game_rules_put(int fd, const ps5tm_request_t *req) {
  cJSON *root = req->body ? cJSON_Parse(req->body) : NULL;
  const cJSON *tid = root ? cJSON_GetObjectItem(root, "title_id") : NULL;
  if(!cJSON_IsString(tid) || !rule_id_ok(tid->valuestring)) {
    cJSON_Delete(root);
    ps5tm_http_send_error(fd, 400, "missing_title",
                          "title_id fehlt oder ist ungültig.");
    return;
  }

  int remove = cJSON_IsTrue(cJSON_GetObjectItem(root, "remove"));

  ps5tm_config_lock();
  unsigned idx = g_config.game_rule_count;
  for(unsigned i = 0; i < g_config.game_rule_count; i++) {
    if(!strcmp(g_config.game_rules[i].title_id, tid->valuestring)) { idx = i; break; }
  }

  int ok = 1;
  if(remove) {
    if(idx < g_config.game_rule_count) {
      for(unsigned i = idx; i + 1 < g_config.game_rule_count; i++)
        g_config.game_rules[i] = g_config.game_rules[i + 1];
      g_config.game_rule_count--;
    }
  } else if(idx == g_config.game_rule_count &&
            g_config.game_rule_count >= PS5TM_MAX_GAME_RULES) {
    ok = 0;                                  /* table full */
  } else {
    ps5tm_game_rule_t *g = &g_config.game_rules[idx];
    if(idx == g_config.game_rule_count) {
      memset(g, 0, sizeof(*g));
      g_config.game_rule_count++;
    }
    snprintf(g->title_id, sizeof(g->title_id), "%s", tid->valuestring);

    const cJSON *nm = cJSON_GetObjectItem(root, "title_name");
    if(cJSON_IsString(nm))
      ps5tm_copy_utf8(g->title_name, sizeof(g->title_name), nm->valuestring);

    const cJSON *tt = cJSON_GetObjectItem(root, "target_temp_c");
    unsigned v = cJSON_IsNumber(tt) ? ps5tm_num_u32(tt->valuedouble)
                                    : g_config.target_temp_c;
    if(v < PS5TM_TARGET_MIN_C) v = PS5TM_TARGET_MIN_C;
    if(v > PS5TM_TARGET_MAX_C) v = PS5TM_TARGET_MAX_C;
    g->target_temp_c = v;

    const cJSON *pf = cJSON_GetObjectItem(root, "profile");
    g->profile = profile_from(cJSON_IsString(pf) ? pf->valuestring : NULL);
  }

  ps5tm_config_unlock();
  cJSON_Delete(root);
  int saved = ok ? (ps5tm_config_save() == 0) : 0;

  if(!ok) {
    ps5tm_http_send_error(fd, 400, "rules_full",
                          "Es sind bereits 16 Spielprofile gespeichert.");
    return;
  }
  if(!saved) {
    ps5tm_http_send_error(fd, 500, "config_save_failed",
                          "Das Spielprofil konnte nicht gespeichert werden.");
    return;
  }
  ps5tm_http_send_json(fd, 200, "{\"ok\":true}");
}


/* ------------------------------------------------------ sensor channel log
 *
 * Raw and unlabelled on purpose: the point of the recording is to find out
 * what the channels are, so putting names on them here would be assuming the
 * answer. Parallel arrays, one entry per sample, same order throughout.
 */
static void
handle_channels(int fd) {
  static ps5tm_chan_sample_t buf[PS5TM_CHANLOG_CAPACITY];
  unsigned n = ps5tm_chanlog_snapshot(buf, PS5TM_CHANLOG_CAPACITY);

  cJSON *root = cJSON_CreateObject();
  cJSON_AddBoolToObject  (root, "ok", 1);
  cJSON_AddNumberToObject(root, "count", n);
  cJSON_AddNumberToObject(root, "channel_count", PS5TM_SOC_CHANNELS);
  cJSON_AddNumberToObject(root, "interval_s", 5);

  cJSON *t = cJSON_AddArrayToObject(root, "t_s");
  for(unsigned i = 0; i < n; i++)
    cJSON_AddItemToArray(t, cJSON_CreateNumber(buf[i].t_s));

  cJSON *chans = cJSON_AddArrayToObject(root, "channels");
  for(int c = 0; c < PS5TM_SOC_CHANNELS; c++) {
    cJSON *series = cJSON_CreateArray();
    for(unsigned i = 0; i < n; i++)
      cJSON_AddItemToArray(series, cJSON_CreateNumber(buf[i].ch[c]));
    cJSON_AddItemToArray(chans, series);
  }

  cJSON *cpu = cJSON_AddArrayToObject(root, "cpu_c");
  cJSON *ld  = cJSON_AddArrayToObject(root, "load_pct");
  cJSON *fan = cJSON_AddArrayToObject(root, "fan_pct");
  for(unsigned i = 0; i < n; i++) {
    cJSON_AddItemToArray(cpu, cJSON_CreateNumber(buf[i].cpu_c));
    cJSON_AddItemToArray(ld,  cJSON_CreateNumber(buf[i].load_x10 / 10.0));
    cJSON_AddItemToArray(fan, cJSON_CreateNumber(buf[i].fan_pct));
  }

  /* Since 1.45.0: the power rails, the frame rate and the four unnamed bytes
     after the rails, on the same timestamps. -1 / null where not measured. */
  {
    cJSON *gw = cJSON_AddArrayToObject(root, "gpu_w");
    cJSON *cw = cJSON_AddArrayToObject(root, "cpu_w");
    cJSON *mw = cJSON_AddArrayToObject(root, "mem_w");
    cJSON *fp = cJSON_AddArrayToObject(root, "fps");
    for(unsigned i = 0; i < n; i++) {
      cJSON_AddItemToArray(gw, cJSON_CreateNumber(buf[i].gpu_w10 < 0 ? -1 : buf[i].gpu_w10 / 10.0));
      cJSON_AddItemToArray(cw, cJSON_CreateNumber(buf[i].cpu_w10 < 0 ? -1 : buf[i].cpu_w10 / 10.0));
      cJSON_AddItemToArray(mw, cJSON_CreateNumber(buf[i].mem_w10 < 0 ? -1 : buf[i].mem_w10 / 10.0));
      cJSON_AddItemToArray(fp, cJSON_CreateNumber(buf[i].fps10   < 0 ? -1 : buf[i].fps10   / 10.0));
    }
    cJSON *aux = cJSON_AddArrayToObject(root, "power_aux");
    for(int b = 0; b < 4; b++) {
      cJSON *series = cJSON_CreateArray();
      for(unsigned i = 0; i < n; i++)
        cJSON_AddItemToArray(series, buf[i].aux_valid
                                       ? cJSON_CreateNumber(buf[i].aux_c[b])
                                       : cJSON_CreateNull());
      cJSON_AddItemToArray(aux, series);
    }
  }

  /* The fan controller's own block, one series per byte, in the same order and
     over the same timestamps as everything above — so a byte can be laid
     against temperature, load and fan speed directly.
     `icc_moved` answers the actual question without anyone having to scan
     28 columns: which bytes changed at all over the recorded window. A byte
     that never moves is a setting; one that moves with the temperature is a
     reading, and this project has no other source for those. */
  {
    cJSON *icc = cJSON_AddObjectToObject(root, "icc_fan");
    cJSON_AddNumberToObject(icc, "size", PS5TM_ICC_FAN_CONFIG_SIZE);
    cJSON_AddNumberToObject(icc, "target_offset", PS5TM_ICC_FAN_TARGET_OFFSET);

    cJSON *valid = cJSON_AddArrayToObject(icc, "valid");
    for(unsigned i = 0; i < n; i++)
      cJSON_AddItemToArray(valid, cJSON_CreateBool(buf[i].icc_valid ? 1 : 0));

    cJSON *bytes = cJSON_AddArrayToObject(icc, "bytes");
    cJSON *moved = cJSON_AddArrayToObject(icc, "moved");
    cJSON *lo    = cJSON_AddArrayToObject(icc, "min");
    cJSON *hi    = cJSON_AddArrayToObject(icc, "max");

    for(unsigned b = 0; b < PS5TM_ICC_FAN_CONFIG_SIZE; b++) {
      cJSON *series = cJSON_CreateArray();
      int seen = 0, bmin = 0, bmax = 0;
      for(unsigned i = 0; i < n; i++) {
        if(!buf[i].icc_valid) { cJSON_AddItemToArray(series, cJSON_CreateNull()); continue; }
        int v = buf[i].icc[b];
        cJSON_AddItemToArray(series, cJSON_CreateNumber(v));
        if(!seen) { bmin = bmax = v; seen = 1; }
        else { if(v < bmin) bmin = v; if(v > bmax) bmax = v; }
      }
      cJSON_AddItemToArray(bytes, series);
      cJSON_AddItemToArray(moved, cJSON_CreateBool(seen && bmax != bmin));
      cJSON_AddItemToArray(lo, seen ? cJSON_CreateNumber(bmin) : cJSON_CreateNull());
      cJSON_AddItemToArray(hi, seen ? cJSON_CreateNumber(bmax) : cJSON_CreateNull());
    }
  }

  char *txt = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if(!txt) { ps5tm_http_send_error(fd, 500, "alloc_failed", "Kein Speicher."); return; }
  ps5tm_http_send(fd, 200, "OK", "application/json", txt, strlen(txt), NULL);
  free(txt);
}


/* ------------------------------------------------------ cooling over time */
static void
handle_thermal(int fd) {
  ps5tm_thermal_week_t w[PS5TM_THERMAL_WEEKS];
  unsigned n = ps5tm_thermal_snapshot(w, PS5TM_THERMAL_WEEKS);

  cJSON *root = cJSON_CreateObject();
  cJSON_AddBoolToObject  (root, "ok", 1);
  cJSON_AddNumberToObject(root, "weeks", n);

  cJSON *arr = cJSON_AddArrayToObject(root, "history");
  for(unsigned i = 0; i < n; i++) {
    cJSON *e = cJSON_CreateObject();
    cJSON_AddNumberToObject(e, "week",     w[i].week);
    cJSON_AddNumberToObject(e, "samples",  w[i].samples);
    cJSON_AddNumberToObject(e, "temp_c",   w[i].temp_c10  / 10.0);
    cJSON_AddNumberToObject(e, "fan_pct",  w[i].fan_pct10 / 10.0);
    cJSON_AddNumberToObject(e, "load_pct", w[i].load_pct);
    cJSON_AddNumberToObject(e, "activity_pct", w[i].activity_pct);
    cJSON_AddBoolToObject  (e, "usable",   w[i].usable);
    cJSON_AddItemToArray(arr, e);
  }

  int delta = 0, base = 0, cur = 0;
  unsigned usable = 0;
  if(ps5tm_thermal_verdict(&delta, &usable, &base, &cur) == 0) {
    cJSON_AddBoolToObject  (root, "verdict_ready", 1);
    cJSON_AddNumberToObject(root, "baseline_c", base  / 10.0);
    cJSON_AddNumberToObject(root, "current_c",  cur   / 10.0);
    cJSON_AddNumberToObject(root, "delta_c",    delta / 10.0);
  } else {
    cJSON_AddBoolToObject  (root, "verdict_ready", 0);
  }
  cJSON_AddNumberToObject(root, "weeks_usable", usable);

  char *txt = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if(!txt) { ps5tm_http_send_error(fd, 500, "alloc_failed", "Kein Speicher."); return; }
  ps5tm_http_send(fd, 200, "OK", "application/json", txt, strlen(txt), NULL);
  free(txt);
}


/* ---------------------------------------------------------- payload list */

static void
handle_payloads_get(int fd) {
  ps5tm_process_t list[64];
  unsigned n = ps5tm_procmgr_list(list, 64);

  cJSON *root = cJSON_CreateObject();
  cJSON_AddBoolToObject(root, "ok", 1);
  cJSON *arr = cJSON_AddArrayToObject(root, "payloads");

  for(unsigned i = 0; i < n; i++) {
    cJSON *e = cJSON_CreateObject();
    cJSON_AddNumberToObject(e, "pid",       list[i].pid);
    cJSON_AddStringToObject(e, "name",      list[i].name);
    cJSON_AddNumberToObject(e, "memory_mb", list[i].memory_mb);
    cJSON_AddBoolToObject  (e, "is_self",   list[i].is_self);
    cJSON_AddItemToArray(arr, e);
  }
  cJSON_AddNumberToObject(root, "count", n);

  char *txt = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if(!txt) { ps5tm_http_send_error(fd, 500, "alloc_failed", "Kein Speicher."); return; }
  ps5tm_http_send(fd, 200, "OK", "application/json", txt, strlen(txt), NULL);
  free(txt);
}

static void
handle_payload_kill(int fd, const ps5tm_request_t *req) {
  cJSON *root = req->body ? cJSON_Parse(req->body) : NULL;
  const cJSON *p = root ? cJSON_GetObjectItem(root, "pid") : NULL;
  if(!cJSON_IsNumber(p)) {
    cJSON_Delete(root);
    ps5tm_http_send_error(fd, 400, "missing_pid",
                          "pid fehlt oder ist keine Zahl.");
    return;
  }
  int pid = ps5tm_num_i32(p->valuedouble);
  cJSON_Delete(root);

  const char *why = NULL;
  if(ps5tm_procmgr_kill(pid, &why) != 0) {
    ps5tm_http_send_error(fd, 400, "kill_refused",
                          why ? why : "Beenden nicht möglich.");
    return;
  }
  ps5tm_http_send_json(fd, 200, "{\"ok\":true}");
}

/* ---------------------------------------------------------- payload files */

static void
handle_payload_files_get(int fd) {
  char *txt = ps5tm_payload_list();
  if(!txt) {
    ps5tm_http_send_error(fd, 503, "busy",
      "Die Liste ist gerade nicht lesbar — antwortet ein USB-Speicher nicht? "
      "Gleich noch einmal versuchen.");
    return;
  }
  ps5tm_http_send(fd, 200, "OK", "application/json", txt, strlen(txt), NULL);
  free(txt);
}

static const char *
json_text(const cJSON *root, const char *key) {
  const cJSON *it = root ? cJSON_GetObjectItem(root, key) : NULL;
  return cJSON_IsString(it) && it->valuestring ? it->valuestring : NULL;
}

/* One handler for the three operations on payload files. The page names a
   place and a file name, never a path: payloads.c checks every part again. */
enum { PLOP_START, PLOP_COPY, PLOP_DELETE };

static void
handle_payload_files_op(int fd, const ps5tm_request_t *req, int op) {
  cJSON *root = req->body ? cJSON_Parse(req->body) : NULL;
  if(!cJSON_IsObject(root)) {
    cJSON_Delete(root);
    ps5tm_http_send_error(fd, 400, "invalid_json",
                          "Die Anfrage ist kein gültiges JSON.");
    return;
  }
  const char *source = json_text(root, "source");
  const char *mount  = json_text(root, "mount");
  const char *dir    = json_text(root, "dir");
  const char *name   = json_text(root, "name");

  ps5tm_payload_result_t res;
  int st;
  if(op == PLOP_START)
    st = ps5tm_payload_start(source, mount, dir ? dir : "", name ? name : "", &res);
  else if(op == PLOP_COPY)
    st = ps5tm_payload_copy(mount, dir ? dir : "", name ? name : "", &res);
  else
    st = ps5tm_payload_delete(name ? name : "", &res);
  cJSON_Delete(root);                           /* the strings above lived in it */

  if(st != 200) {
    ps5tm_http_send_error(fd, st, res.code, res.msg);
    return;
  }

  cJSON *out = cJSON_CreateObject();
  cJSON_AddBoolToObject(out, "ok", 1);
  cJSON_AddNumberToObject(out, "bytes", (double)res.bytes);
  if(res.reply[0]) cJSON_AddStringToObject(out, "output", res.reply);
  char *txt = cJSON_PrintUnformatted(out);
  cJSON_Delete(out);
  if(!txt) { ps5tm_http_send_error(fd, 500, "alloc_failed", "Kein Speicher."); return; }
  ps5tm_http_send(fd, 200, "OK", "application/json", txt, strlen(txt), NULL);
  free(txt);
}

static void
handle_controller_diag(int fd) {
  ps5tm_pad_diag_t d;
  ps5tm_pad_diag_get(&d);

  cJSON *root = cJSON_CreateObject();
  cJSON_AddBoolToObject(root, "ok", 1);
  cJSON_AddStringToObject(root, "kind", "passive_controller_backend_probe");
  cJSON_AddBoolToObject(root, "host_test", d.host_test != 0);

  cJSON *pad = cJSON_AddObjectToObject(root, "scepad");
  cJSON_AddBoolToObject(pad, "init", d.scepad_init != 0);
  cJSON_AddBoolToObject(pad, "open", d.scepad_open != 0);
  cJSON_AddBoolToObject(pad, "read_state", d.scepad_read_state != 0);
  cJSON_AddBoolToObject(pad, "get_handle", d.scepad_get_handle != 0);
  cJSON_AddBoolToObject(pad, "set_lightbar", d.scepad_set_lightbar != 0);
  cJSON_AddBoolToObject(pad, "reset_lightbar", d.scepad_reset_lightbar != 0);
  cJSON_AddBoolToObject(pad, "set_process_privilege",
                        d.scepad_set_process_privilege != 0);
  cJSON_AddNumberToObject(pad, "cached_refused_rc", d.cached_pad_refused_rc);

  cJSON *hid = cJSON_AddObjectToObject(root, "hid");
  /* False until the controller probe has looked (it is off by default): the
     three flags below then mean "not checked", not "absent". */
  cJSON_AddBoolToObject(hid, "resolved", d.hid_resolved != 0);
  cJSON_AddBoolToObject(hid, "dev_hid_present", d.dev_hid_present != 0);
  cJSON_AddBoolToObject(hid, "dev_bluetooth_hid_present",
                        d.dev_bluetooth_hid_present != 0);
  cJSON_AddBoolToObject(hid, "hidcontrol_get_battery_state",
                        d.hidcontrol_get_battery_state != 0);
  cJSON_AddBoolToObject(hid, "hidcontrol_init", d.hidcontrol_init != 0);
  cJSON_AddBoolToObject(hid, "bluetoothhid_init", d.bluetoothhid_init != 0);

    cJSON_AddStringToObject(root, "note",
      "Nur Verfügbarkeit. Keine Controller-Open-, Report- oder Akkuaufrufe.");

  char *txt = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if(!txt) {
    ps5tm_http_send_error(fd, 500, "alloc_failed", "Kein Speicher.");
    return;
  }
  ps5tm_http_send(fd, 200, "OK", "application/json", txt, strlen(txt), NULL);
  free(txt);
}

static void
handle_risky_sensors(int fd) {
  ps5tm_sensors_t s;
  if(ps5tm_platform_probe_risky_once(&s) != 0) {
    ps5tm_http_send_error(fd, 503, "risky_probe_unavailable",
                          "Risikotelemetrie aktuell nicht verfügbar.");
    return;
  }

  cJSON *root = cJSON_CreateObject();
  cJSON_AddBoolToObject(root, "ok", 1);
  cJSON_AddNumberToObject(root, "timestamp_ms", (double)ps5tm_now_ms());

  cJSON *load = cJSON_AddObjectToObject(root, "load");
  cJSON_AddNumberToObject(load, "cpu_pct", s.cpu_load_pct);
  cJSON_AddBoolToObject  (load, "cpu_valid", s.cpu_load_valid);
  cJSON_AddBoolToObject  (load, "gpu_valid", s.gpu_load_valid);
  cJSON_AddNumberToObject(load, "cpu_mhz", (double)s.cpu_mhz);
  cJSON_AddBoolToObject  (load, "cpu_mhz_valid", s.cpu_mhz_valid);
  cJSON_AddNumberToObject(load, "cpu_mode", s.cpu_mode);
  cJSON_AddBoolToObject  (load, "cpu_mode_valid", s.cpu_mode_valid);
  cJSON_AddNumberToObject(load, "soc_power_w", s.soc_power_w);
  cJSON_AddBoolToObject  (load, "soc_power_valid", s.soc_power_valid);

  cJSON *cores = cJSON_AddArrayToObject(load, "cores");
  for(int i = 0; i < s.core_count && i < PS5TM_MAX_CORES; i++)
    cJSON_AddItemToArray(cores, cJSON_CreateNumber(s.core_pct[i]));
  add_cpu_detail(load, &s);
  add_live_clocks(load, &s);
  add_power(root, &s);

  /* Who keeps the CPUs busy — see platform.c, update_top_threads(). */
  {
    ps5tm_thread_load_t top[PS5TM_TOP_THREADS];
    unsigned n = ps5tm_platform_top_threads(top, PS5TM_TOP_THREADS);
    cJSON *arr = cJSON_AddArrayToObject(root, "top_threads");
    for(unsigned i = 0; i < n; i++) {
      cJSON *e = cJSON_CreateObject();
      cJSON_AddNumberToObject(e, "tid", top[i].tid);
      cJSON_AddNumberToObject(e, "pid", top[i].pid);
      cJSON_AddBoolToObject  (e, "own", top[i].pid == (uint32_t)getpid());
      cJSON_AddStringToObject(e, "name", top[i].name);
      cJSON_AddNumberToObject(e, "pct", top[i].pct);
      cJSON_AddItemToArray(arr, e);
    }
  }

  char *txt = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if(!txt) {
    ps5tm_http_send_error(fd, 500, "alloc_failed", "Kein Speicher.");
    return;
  }
  ps5tm_http_send(fd, 200, "OK", "application/json", txt, strlen(txt), NULL);
  free(txt);
}

static void
handle_drives(int fd) {
  ps5tm_config_lock();
  int enabled = (g_config.probe_mask & PS5TM_PROBE_DRIVE) != 0;
  ps5tm_config_unlock();

  if(!enabled) {
    ps5tm_http_send_error(fd, 409, "drive_probe_disabled",
                          "Laufwerks-Telemetrie ist deaktiviert "
                          "(Zusatzabfragen).");
    return;
  }

  ps5tm_sysinfo_t info;
  ps5tm_sysinfo_get(&info);

  cJSON *root = cJSON_CreateObject();
  cJSON_AddBoolToObject(root, "ok", 1);
  cJSON_AddStringToObject(root, "kind", "drive_telemetry");
  cJSON_AddBoolToObject(root, "m2_present", info.m2_present ? 1 : 0);

  unsigned raw_da = 0;
  unsigned with_temp = 0;

  cJSON *arr = cJSON_AddArrayToObject(root, "drives");
  for(unsigned i = 0; i < info.volume_count; i++) {
    const char *dev = info.volumes[i].device;
    const char *mnt = info.volumes[i].path;
    const char *kind = "unknown";
    const char *sensor = "none";
    const char *note = "Für dieses Volume liegt keine direkte Laufwerkstemperatur vor.";
    int da = -1;
    if(dev[0] && sscanf(dev, "/dev/da%d", &da) == 1) {
      raw_da++;
      kind = "block_da";
      sensor = "fw_unavailable";
      note = "Direkte SMART-Temperatur aus diesem Kontext auf dieser Firmware nicht verfügbar.";
    } else if(!strcmp(mnt, "/user")) {
      kind = "internal_ssd";
      sensor = "not_exposed";
      note = "Interne SSD: kein direkter Temperatursensor in diesem Kontext exponiert.";
    } else if(!strcmp(mnt, "/mnt/ext1")) {
      kind = "m2_expansion";
      /* Until 1.45.1 SoC channel 2 stood in for this drive's temperature.
         It sits on the chip (platform.c), so the drive has none. */
      if(info.m2_present) {
        sensor = "not_exposed";
        note = "M.2-Erweiterung: kein Temperatursensor bekannt. SoC-Kanal 2, "
               "früher dafür angezeigt, misst den Hauptchip.";
      } else {
        sensor = "not_present";
        note = "Kein M.2-Laufwerk erkannt.";
      }
    } else if(!strncmp(mnt, "/mnt/usb", 8)) {
      kind = "usb_storage";
      sensor = "fw_unavailable";
      note = "USB-Laufwerke: Temperatur wird hier nicht bereitgestellt.";
    }

    cJSON *d = cJSON_CreateObject();
    if(dev[0]) cJSON_AddStringToObject(d, "device", dev);
    cJSON_AddNumberToObject(d, "da_index", da);
    cJSON_AddStringToObject(d, "kind", kind);
    cJSON_AddStringToObject(d, "label", info.volumes[i].label);
    cJSON_AddStringToObject(d, "mount", mnt);
    cJSON_AddNumberToObject(d, "total_bytes", (double)info.volumes[i].total_bytes);
    cJSON_AddNumberToObject(d, "used_bytes",  (double)info.volumes[i].used_bytes);
    cJSON_AddNumberToObject(d, "free_bytes",  (double)info.volumes[i].free_bytes);
    cJSON_AddBoolToObject  (d, "temp_valid", 0);
    cJSON_AddStringToObject(d, "temp_sensor", sensor);
    cJSON_AddStringToObject(d, "temp_note", note);
    cJSON_AddItemToArray(arr, d);
  }

  cJSON_AddNumberToObject(root, "count", (double)info.volume_count);
  cJSON_AddNumberToObject(root, "raw_da_count", (double)raw_da);
  cJSON_AddNumberToObject(root, "temp_capable_count", (double)with_temp);

  char *txt = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if(!txt) {
    ps5tm_http_send_error(fd, 500, "alloc_failed", "Kein Speicher.");
    return;
  }
  ps5tm_http_send(fd, 200, "OK", "application/json", txt, strlen(txt), NULL);
  free(txt);
}


/* ----------------------------------------------------------------- library */

/* The games on the home screen (library.c). /api/v1/games was taken — it
   holds the per-game cooling rules — hence "library". */
static void
handle_library(int fd) {
  cJSON *root = ps5tm_library_json();
  int    ok   = cJSON_IsTrue(cJSON_GetObjectItem(root, "ok"));
  char  *txt  = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if(!txt) {
    ps5tm_http_send_error(fd, 500, "alloc_failed", "Kein Speicher.");
    return;
  }
  ps5tm_http_send_json(fd, ok ? 200 : 503, txt);
  free(txt);
}

/* key=value among the query parameters, or empty. Values here are title
   ids and fixed words; nothing is URL-decoded. */
static void
query_param(const char *query, const char *key, char *out, size_t out_len) {
  size_t kl = strlen(key);
  out[0] = 0;
  for(const char *p = query; p && *p; ) {
    if(!strncmp(p, key, kl) && p[kl] == '=') {
      size_t n = strcspn(p + kl + 1, "&");
      if(n < out_len) { memcpy(out, p + kl + 1, n); out[n] = 0; }
      return;
    }
    p = strchr(p, '&');
    if(p) p++;
  }
}

static void
query_title_id(const char *query, char *id, size_t id_len) {
  query_param(query, "id", id, id_len);
}

static void
handle_library_cover(int fd, const ps5tm_request_t *req) {
  /* ts= is only for the browser cache. */
  char id[16];
  query_title_id(req->query, id, sizeof(id));

  char path[160];
  if(ps5tm_library_cover(id, path, sizeof(path)) != 0 ||
     ps5tm_http_send_file(fd, path, "image/png", 8u * 1024 * 1024, 86400) != 0)
    ps5tm_http_send_error(fd, 404, "no_cover",
                          "Für diesen Titel liegt kein Cover vor.");
}

/* The start button.
 *
 * A game starts directly, in front, the way the Homebrew Launcher starts one
 * (ps5tm_library_start; measured 29.09.2026). The same title already running
 * is brought forward through its tile link, which the shell handles itself
 * (ps5tm_library_hand_over). Pressed in the console's own browser, both also
 * close the browser, which would otherwise stay in front of the game.
 *
 * A different game running, or a tile that only opens a web page: the console
 * gets a toast with a "Starten" button instead, which opens the tile's own
 * link and leaves the decision to the shell and the person at the console.
 * Nothing here ever closes a game. */

/* The title started or offered last, and when.
 *
 * A start must never be asked for twice. Elf Arsenal's notes say it plainly
 * (src/websrv.c): re-launching the title that is already running
 * kernel-panics the PS5. On 03.10.2026 the console went down a minute after
 * this app had offered the same game twice in six seconds through the
 * notification — each offer carries a "Starten" button, and both can be
 * pressed. So a direct start and an offer both count, for half a minute. The
 * game-state reading can also be four seconds behind, so "no game running"
 * right after a start does not mean the start failed. */
#define START_GUARD_MS 30000
static pthread_mutex_t g_started_lock = PTHREAD_MUTEX_INITIALIZER;
static char            g_started_id[16];
static uint64_t        g_started_ms;

static int
started_recently(const char *id) {
  pthread_mutex_lock(&g_started_lock);
  int yes = g_started_id[0] && !strcmp(g_started_id, id) &&
            ps5tm_mono_ms() - g_started_ms < START_GUARD_MS;
  pthread_mutex_unlock(&g_started_lock);
  return yes;
}

static void
remember_started(const char *id) {
  pthread_mutex_lock(&g_started_lock);
  snprintf(g_started_id, sizeof(g_started_id), "%s", id);
  g_started_ms = ps5tm_mono_ms();
  pthread_mutex_unlock(&g_started_lock);
}

/* 0x8094000C from the launcher: this very title is running already
 * (lnc_manager.cpp:190, checkExistingApp; another game is 0x80940010 from
 * line 208 of the same function). Seen twice on 03.10.2026 with a game paused
 * in the background: the shell closed it for this start itself, started the
 * title, and only then answered the call — with 0x8094000C. The start has
 * happened then. Confirmed by the kernel, it counts as one, and the title is
 * not offered a second time through the notification. The game's process
 * may trail the answer by a moment, so the kernel is asked for two seconds. */
static int
launched_anyway(const char *id, int rc) {
  if((unsigned)rc != 0x8094000Cu) return 0;
  for(int i = 0; i < 8; i++) {
    if(ps5tm_procmgr_title_running(id) == 1) return 1;
    usleep(250 * 1000);
  }
  return 0;
}

static void send_cjson(int fd, int status, cJSON *root);

/* The cover and metadata folder (libcache.c): how much is in it, and emptying it. */
static void
handle_library_cache_clear(int fd) {
  unsigned titles = 0;
  uint64_t bytes = 0;
  int rc = ps5tm_libcache_clear(&titles, &bytes);
  if(rc) {
    char msg[160];
    snprintf(msg, sizeof(msg), "Der Ordner ließ sich nicht lesen: %s", strerror(rc));
    ps5tm_http_send_error(fd, 500, "clear_failed", msg);
    return;
  }
  ps5tm_library_forget();                 /* the next list is read afresh: with the switch on, it fills the folder again */
  cJSON *o = cJSON_CreateObject();
  cJSON_AddBoolToObject(o, "ok", 1);
  cJSON_AddNumberToObject(o, "titles", titles);
  cJSON_AddNumberToObject(o, "bytes", (double)bytes);
  send_cjson(fd, 200, o);
}


/* Closing the running game, and starting another one after it.
 *
 * Only ever asked for by the person tapping "Spiel beenden" (no question since
 * 03.10.2026, at the user's wish; the page warns beforehand, in the notice that
 * another game is running and on the button) — until then this app never
 * closes a game. The page starts nothing after it: the next game is the next
 * tap. (The launch route's `close` still starts one after the other.) It runs
 * on its own thread and the request is answered first: the game going down
 * takes the console browser's connection with it (Elf Arsenal saw
 * ERR_CONNECTION_RESET when it closed synchronously). The page asks
 * GET /api/v1/library/close how it went. */
enum { SW_IDLE, SW_CLOSING, SW_STARTING, SW_DONE, SW_FAILED };
static const char *const k_sw_state[] = { "idle", "closing", "starting",
                                          "done", "failed" };

typedef struct {
  int  state;
  char close_id[16], close_name[128];
  char then_id[16], then_name[128], then_link[96];
  int  on_console;
  char message[320];
} sw_job_t;

/* After the kill: how long the closed game's sandbox may take to go, the time
   left for ShadowMountPlus after that, and the fixed wait when the sandbox
   folder cannot be looked at. Then the number of launcher calls, the pause
   between them, and how many answers that are neither "busy" nor "another
   game is running" are tried before the shell's own way is offered. */
#define SW_SANDBOX_MAX_MS  6000
#define SW_AFTER_SANDBOX_MS 1000
#define SW_GRACE_MS        3000
#define SW_RETRY_MS        1500
#define SW_TRIES           8
#define SW_OTHER_TRIES     3

static pthread_mutex_t g_sw_lock = PTHREAD_MUTEX_INITIALIZER;
static sw_job_t        g_sw;

/* usleep() in slices: a value of a second or more is not portable. */
static void
sw_pause(int ms) {
  for(; ms > 0; ms -= 250) usleep((unsigned)(ms < 250 ? ms : 250) * 1000);
}

/* Whether the shell still keeps a sandbox for the title, /mnt/sandbox/<ID>_000
   (or _001 ...): 1 yes, 0 no, -1 when the folder cannot be read. */
static int
sandbox_exists(const char *title_id) {
  DIR *d = opendir("/mnt/sandbox");
  if(!d) return errno == ENOENT ? 0 : -1;
  size_t n = strlen(title_id);
  int found = 0;
  struct dirent *e;
  while(!found && (e = readdir(d)) != NULL)
    found = !strncmp(e->d_name, title_id, n) && e->d_name[n] == '_' &&
            e->d_name[n + 1] != '\0';
  closedir(d);
  return found;
}

/* Waiting for the shell to let go of a closed game, the way ShadowMountPlus
 * does before it releases the game's mounts (sm_game_lifecycle.c,
 * maybe_finalize_pending_game_exit): until the shell has removed the title's
 * sandbox. That is the end of the shell's own cleanup; the launcher takes a
 * new game from then on. On 03.10.2026 the sandbox went 0.5–0.6 s after each
 * kill, and ShadowMountPlus had released and unmounted everything within the
 * next half second — so one more second is left for it. The launcher is not
 * asked earlier: every refused call makes ShadowMountPlus mount the title and
 * release it again. Without a way to look, the fixed wait of before.
 *
 * Writes what happened into `how`, for the log line of the start. */
static void
sw_wait_for_shell(const char *closed_id, char *how, size_t how_len) {
  uint64_t start = ps5tm_mono_ms();
  for(;;) {
    int s = sandbox_exists(closed_id);
    if(s < 0) {
      snprintf(how, how_len, "Sandbox nicht lesbar, %d s gewartet",
               SW_GRACE_MS / 1000);
      sw_pause(SW_GRACE_MS);
      return;
    }
    uint64_t waited = ps5tm_mono_ms() - start;
    if(s == 0) {
      snprintf(how, how_len, "Sandbox nach %u ms weg", (unsigned)waited);
      break;
    }
    if(waited >= SW_SANDBOX_MAX_MS) {
      snprintf(how, how_len, "Sandbox nach %u ms noch da", (unsigned)waited);
      break;
    }
    sw_pause(100);
  }
  sw_pause(SW_AFTER_SANDBOX_MS);
}

static void sw_set(int state, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

static void
sw_set(int state, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  pthread_mutex_lock(&g_sw_lock);
  g_sw.state = state;
  vsnprintf(g_sw.message, sizeof(g_sw.message), fmt, ap);
  pthread_mutex_unlock(&g_sw_lock);
  va_end(ap);
}

static void *
sw_thread(void *arg) {
  (void)arg;
  sw_job_t job;
  pthread_mutex_lock(&g_sw_lock);
  job = g_sw;
  pthread_mutex_unlock(&g_sw_lock);

  usleep(400 * 1000);                     /* let the answer leave first */

  int rc = ps5tm_procmgr_close_game(job.close_id, 6000);
  if(rc < 0) {
    sw_set(SW_FAILED, "„%s“ ließ sich nicht beenden.", job.close_name);
    PS5TM_WARN("game_close_failed", "%s (%s) ließ sich nicht beenden.",
               job.close_name, job.close_id);
    ps5tm_notify("„%s“ ließ sich nicht beenden.", job.close_name);
    return NULL;
  }
  if(rc == 0)
    PS5TM_INFO("game_closed", "Beendet: %s (%s).", job.close_name, job.close_id);

  /* rc 1: the game had already gone — the game-state reading runs a few
     seconds behind, so the button can outlive the game.

     "Beendet" is said once the console is ready for the next game: since
     03.10.2026 the page starts nothing itself after closing — the person taps
     "Starten" next, and that should not meet a shell still letting go. */
  if(!job.then_id[0]) {
    if(rc == 0) {
      char waited[64] = "";
      sw_wait_for_shell(job.close_id, waited, sizeof(waited));
      PS5TM_INFO("game_released", "Freigegeben: %s (%s), %s.", job.close_name,
                 job.close_id, waited);
      sw_set(SW_DONE, "„%s“ wurde beendet.", job.close_name);
    } else {
      sw_set(SW_DONE, "„%s“ lief schon nicht mehr.", job.close_name);
    }
    return NULL;
  }

  sw_set(SW_STARTING, "„%s“ wurde beendet, „%s“ wird gestartet …",
         job.close_name, job.then_name);

  /* Asked too early, the launcher refuses — 0x80020010 to a call right after
     the kill, then 0x80940010 (03.10.2026). So it is asked once the shell has
     let go, and a few times after that, a while apart — not in a quick
     series. */
  char waited[64] = "";
  sw_wait_for_shell(job.close_id, waited, sizeof(waited));
  int lrc = -1, other = 0, anyway = 0, tries = 0;
  while(tries < SW_TRIES) {
    lrc = ps5tm_library_start(job.then_id);
    tries++;
    if(lrc >= 0) break;
    if((anyway = launched_anyway(job.then_id, lrc)) != 0) break;
    unsigned c = (unsigned)lrc;
    if(c == 0xFFFFFFFFu || c == 0x80940033u) break;  /* not asked / not mounted */
    if(c != 0x80940010u && c != 0x80020010u && ++other >= SW_OTHER_TRIES) break;
    sw_pause(SW_RETRY_MS);
  }
  if(lrc >= 0 || anyway) {
    remember_started(job.then_id);
    int closing = job.on_console && ps5tm_library_hand_over(job.then_id, 1) == 0;
    char how[48];
    if(anyway) snprintf(how, sizeof(how), "läuft (Antwort 0x%08X)", (unsigned)lrc);
    else       snprintf(how, sizeof(how), "App-ID 0x%X", (unsigned)lrc);
    PS5TM_INFO("library_started",
               "Gestartet nach dem Beenden von %s: %s (%s), %s; %s, %d. Startaufruf%s.",
               job.close_name, job.then_name, job.then_id, how, waited, tries,
               closing ? " (Browser der Konsole wird geschlossen)" : "");
    sw_set(SW_DONE, "„%s“ wurde beendet, „%s“ startet.", job.close_name,
           job.then_name);
    return NULL;
  }

  /* The launcher refused for another reason — an image title that
     ShadowMountPlus has not mounted answers 0x80940033. Then the shell's own
     way, through the tile link, exactly as a start with no game running. */
  char icon[160];
  if(ps5tm_library_cover(job.then_id, icon, sizeof(icon)) != 0) icon[0] = 0;
  int nrc = ps5tm_notify_action(job.then_name, "Zum Spielen „Starten“ wählen",
                                icon[0] ? icon : NULL, "Starten", job.then_link);
  if(nrc == 0) {
    remember_started(job.then_id);
    PS5TM_INFO("library_launch_offered",
               "Start angeboten nach dem Beenden von %s: %s (%s) — der direkte "
               "Start ergab 0x%08X; %s, %d Startaufrufe.", job.close_name,
               job.then_name, job.then_id, (unsigned)lrc, waited, tries);
    sw_set(SW_DONE, "„%s“ wurde beendet. Auf der PS5 oben rechts „Starten“ "
           "wählen, dann startet „%s“.", job.close_name, job.then_name);
  } else {
    sw_set(SW_FAILED, "„%s“ wurde beendet, aber die Konsole nahm weder den "
           "Start (0x%08X) noch die Meldung (0x%08X) an.", job.close_name,
           (unsigned)lrc, (unsigned)nrc);
  }
  return NULL;
}

/* 0 when the job runs; -1 when one is already running or no thread could be
   made, with `err` saying so. */
static int
sw_start(const char *close_id, const char *close_name, const char *then_id,
         const char *then_name, const char *then_link, int on_console,
         char *err, size_t err_len) {
  pthread_mutex_lock(&g_sw_lock);
  if(g_sw.state == SW_CLOSING || g_sw.state == SW_STARTING) {
    pthread_mutex_unlock(&g_sw_lock);
    snprintf(err, err_len, "Es wird gerade schon ein Spiel beendet.");
    return -1;
  }
  memset(&g_sw, 0, sizeof(g_sw));
  g_sw.state = SW_CLOSING;
  snprintf(g_sw.close_id, sizeof(g_sw.close_id), "%s", close_id);
  snprintf(g_sw.close_name, sizeof(g_sw.close_name), "%s", close_name);
  snprintf(g_sw.then_id, sizeof(g_sw.then_id), "%s", then_id ? then_id : "");
  snprintf(g_sw.then_name, sizeof(g_sw.then_name), "%s", then_name ? then_name : "");
  snprintf(g_sw.then_link, sizeof(g_sw.then_link), "%s", then_link ? then_link : "");
  g_sw.on_console = on_console;
  snprintf(g_sw.message, sizeof(g_sw.message), "„%s“ wird beendet …", close_name);
  pthread_mutex_unlock(&g_sw_lock);

  pthread_t t;
  if(pthread_create(&t, NULL, sw_thread, NULL) != 0) {
    sw_set(SW_FAILED, "Kein Thread für das Beenden verfügbar.");
    snprintf(err, err_len, "Die Konsole hat gerade keinen Platz für einen "
             "weiteren Vorgang.");
    return -1;
  }
  pthread_detach(t);
  return 0;
}

/* Another game is running: nothing is started or offered. The page shows the
   name in a notice; only a request naming that game closes it. */
static void
send_other_running(int fd, const char *run_id, const char *run_name,
                   const char *id, const char *name) {
  cJSON *root = cJSON_CreateObject();
  cJSON_AddBoolToObject(root, "ok", 0);
  cJSON_AddStringToObject(root, "code", "other_running");
  char msg[320];
  snprintf(msg, sizeof(msg), "„%s“ läuft noch und muss zuerst beendet werden.",
           run_name);
  cJSON_AddStringToObject(root, "message", msg);
  cJSON *r = cJSON_AddObjectToObject(root, "running");
  cJSON_AddStringToObject(r, "id", run_id);
  cJSON_AddStringToObject(r, "name", run_name);
  cJSON *t = cJSON_AddObjectToObject(root, "target");
  cJSON_AddStringToObject(t, "id", id);
  cJSON_AddStringToObject(t, "name", name);
  send_cjson(fd, 409, root);
}

/* The running game as the kernel sees it, when the game-state reading has
   none: the launcher said "another game is running" and is to be believed. */
static int
running_by_kernel(char *id, size_t id_len, char *name, size_t name_len) {
  if(ps5tm_procmgr_game_title(0, id, id_len) != 0 || !id[0]) return -1;
  char link[96];
  if(ps5tm_library_launch_info(id, name, name_len, link, sizeof(link)) != 0)
    snprintf(name, name_len, "%s", id);
  return 0;
}

static void
handle_library_launch(int fd, const ps5tm_request_t *req) {
  cJSON *body = req->body ? cJSON_Parse(req->body) : NULL;
  const cJSON *jid    = body ? cJSON_GetObjectItem(body, "id") : NULL;
  const cJSON *jclose = body ? cJSON_GetObjectItem(body, "close") : NULL;
  char id[16] = "", close_id[16] = "";
  if(cJSON_IsString(jid)) snprintf(id, sizeof(id), "%s", jid->valuestring);
  if(cJSON_IsString(jclose)) snprintf(close_id, sizeof(close_id), "%s", jclose->valuestring);
  cJSON_Delete(body);

  char name[128], link[96], icon[160];
  if(ps5tm_library_launch_info(id, name, sizeof(name), link, sizeof(link)) != 0) {
    ps5tm_http_send_error(fd, 404, "not_launchable",
                          "Dieser Titel steht nicht in der Spieleliste oder "
                          "hat keinen Startlink.");
    return;
  }

  /* The page in the console's own browser: a game started from there comes
     up behind the browser (the console does not take focus from the page in
     front, measured 29.09.2026), so the shell is asked to close it. Its
     requests come from 127.0.0.1 via the tile, or from the console's own LAN
     address when typed in. The browser's user agent turned out not to be a
     reliable sign. */
  char own_ip[16] = "";
  ps5tm_local_ip(own_ip, sizeof(own_ip));
  int on_console = !strcmp(req->peer, "127.0.0.1") ||
                   (req->peer[0] && !strcmp(req->peer, own_ip));
  int game_link  = !strncmp(link, "psgm:play?id=", 13);

  /* Only with no game running does the app start one itself. The same game
     already in front needs nothing — unless the request comes from the
     console's browser, which is then in front of it whatever the last focus
     reading said. In the background, its tile link brings it forward: that is
     what the shell does for a tile, not a second launch. A different game
     is closed only by a request that names exactly that game. */
  ps5tm_gamestate_t gs;
  ps5tm_gamestate_get(&gs);

  /* The reading is a few seconds old, so the kernel decides both ways. A game
     just closed lingers in it for up to one refresh: "Starten" right after
     "Spiel beenden" would ask about a game that is gone. And it has missed
     games: twice on 03.10.2026 it said "kein Spiel" — Tetris paused in the
     background, then Offroad Racing, which the shell had started behind the
     console's browser. The launcher did not refuse then: the shell closed the
     paused game for the start itself, without anyone being asked. */
  if(gs.title_id[0] && ps5tm_procmgr_title_running(gs.title_id) == 0)
    gs.title_id[0] = '\0';
  if(!gs.title_id[0]) {
    char kid[16] = "", kname[128] = "";
    if(running_by_kernel(kid, sizeof(kid), kname, sizeof(kname)) == 0) {
      snprintf(gs.title_id, sizeof(gs.title_id), "%s", kid);
      snprintf(gs.title_name, sizeof(gs.title_name), "%s", kname);
      gs.foreground = 0;
    }
  }

  int same_running  = gs.title_id[0] && !strcmp(gs.title_id, id);
  int other_running = gs.title_id[0] && !same_running;
  char out[256];

  if(same_running && gs.foreground && !on_console) {
    ps5tm_http_send_json(fd, 200,
        "{\"ok\":true,\"started\":false,\"already_front\":true}");
    return;
  }

  if(same_running && game_link && ps5tm_library_hand_over(id, on_console) == 0) {
    PS5TM_INFO("library_front", "Nach vorn: %s (%s), angefragt von %s%s.",
               name, id, req->peer[0] ? req->peer : "?",
               on_console ? " (Browser der Konsole, wird geschlossen)" : "");
    snprintf(out, sizeof(out),
             "{\"ok\":true,\"started\":false,\"brought_forward\":true,"
             "\"on_console\":%s,\"closing\":%s}",
             on_console ? "true" : "false", on_console ? "true" : "false");
    ps5tm_http_send_json(fd, 200, out);
    return;
  }

  /* Never twice — see START_GUARD_MS. */
  if(started_recently(id)) {
    ps5tm_http_send_json(fd, 200,
        "{\"ok\":true,\"started\":true,\"already_starting\":true}");
    return;
  }

  if(other_running) {
    const char *run_name = gs.title_name[0] ? gs.title_name : gs.title_id;
    if(close_id[0] && !strcmp(close_id, gs.title_id)) {
      char err[160];
      if(sw_start(gs.title_id, run_name, id, name, link, on_console, err,
                  sizeof(err)) != 0) {
        ps5tm_http_send_error(fd, 409, "busy", err);
        return;
      }
      PS5TM_INFO("game_switch", "Beenden von %s (%s) und Start von %s (%s) "
                 "bestätigt, angefragt von %s.", run_name, gs.title_id, name,
                 id, req->peer[0] ? req->peer : "?");
      snprintf(out, sizeof(out), "{\"ok\":true,\"state\":\"closing\","
               "\"closing\":\"%s\",\"then\":\"%s\"}", gs.title_id, id);
      ps5tm_http_send_json(fd, 202, out);
      return;
    }
    send_other_running(fd, gs.title_id, run_name, id, name);
    return;
  }

  /* Only with nothing running: a running title — the same one included, when
     handing it over failed — is never launched again (START_GUARD_MS). */
  int rc = -1;
  if(game_link && !gs.title_id[0]) {
    rc = ps5tm_library_start(id);
    int anyway = rc < 0 && launched_anyway(id, rc);
    if(rc >= 0 || anyway) {
      remember_started(id);
      int closing = on_console && ps5tm_library_hand_over(id, 1) == 0;
      char how[48];
      if(anyway) snprintf(how, sizeof(how), "läuft (Antwort 0x%08X)", (unsigned)rc);
      else       snprintf(how, sizeof(how), "App-ID 0x%X", (unsigned)rc);
      PS5TM_INFO("library_started", "Gestartet: %s (%s), %s, angefragt von %s%s.",
                 name, id, how, req->peer[0] ? req->peer : "?",
                 !on_console ? ""
                 : closing ? " (Browser der Konsole, wird geschlossen)"
                           : " (Browser der Konsole, bleibt offen)");
      snprintf(out, sizeof(out),
               "{\"ok\":true,\"started\":true,\"app_id\":%d,\"on_console\":%s,"
               "\"closing\":%s}",
               anyway ? -1 : rc, on_console ? "true" : "false",
               closing ? "true" : "false");
      ps5tm_http_send_json(fd, 200, out);
      return;
    }
  }

  /* 0x80940010: another game is running — the launcher refuses and closes
     nothing (test of 29.09.2026: Two Point Hospital while Tetris ran). The
     game-state reading can miss a game started behind the console's browser
     or across an app restart; the launcher's answer does not, and the kernel
     names the game. */
  if((unsigned)rc == 0x80940010u) {
    char run_id[16] = "", run_name[128] = "";
    if(running_by_kernel(run_id, sizeof(run_id), run_name, sizeof(run_name)) == 0 &&
       strcmp(run_id, id)) {
      if(close_id[0] && !strcmp(close_id, run_id)) {
        char err[160];
        if(sw_start(run_id, run_name, id, name, link, on_console, err,
                    sizeof(err)) != 0) {
          ps5tm_http_send_error(fd, 409, "busy", err);
          return;
        }
        snprintf(out, sizeof(out), "{\"ok\":true,\"state\":\"closing\","
                 "\"closing\":\"%s\",\"then\":\"%s\"}", run_id, id);
        ps5tm_http_send_json(fd, 202, out);
        return;
      }
      send_other_running(fd, run_id, run_name, id, name);
      return;
    }
    ps5tm_http_send_error(fd, 409, "other_running_unknown",
                          "Es läuft noch ein anderes Spiel. Bitte schließe es "
                          "zuerst über das Menü der PS-Taste.");
    return;
  }

  if(ps5tm_library_cover(id, icon, sizeof(icon)) != 0) icon[0] = 0;
  int nrc = ps5tm_notify_action(name, "Zum Spielen „Starten“ wählen",
                                icon[0] ? icon : NULL, "Starten", link);
  if(nrc != 0) {
    char msg[160];
    snprintf(msg, sizeof(msg), "Die Konsole hat weder den Start (0x%08X) noch die "
             "Meldung (0x%08X) angenommen.", (unsigned)rc, (unsigned)nrc);
    ps5tm_http_send_error(fd, 503, "launch_refused", msg);
    return;
  }
  remember_started(id);
  PS5TM_INFO("library_launch_offered",
             "Start angeboten: %s (%s) über %s — der direkte Start ergab 0x%08X.",
             name, id, link, (unsigned)rc);
  snprintf(out, sizeof(out),
           "{\"ok\":true,\"started\":false,\"via\":\"notification\","
           "\"already_running\":%s,\"other_running\":false,\"code\":\"0x%08X\"}",
           same_running ? "true" : "false", (unsigned)rc);
  ps5tm_http_send_json(fd, 200, out);
}


/* "Spiel beenden". The page names the game it shows as running; another one
   is never closed in its place. */
static void
handle_library_close(int fd, const ps5tm_request_t *req) {
  cJSON *body = req->body ? cJSON_Parse(req->body) : NULL;
  const cJSON *jid = body ? cJSON_GetObjectItem(body, "id") : NULL;
  char id[16] = "";
  if(cJSON_IsString(jid)) snprintf(id, sizeof(id), "%s", jid->valuestring);
  cJSON_Delete(body);

  ps5tm_gamestate_t gs;
  ps5tm_gamestate_get(&gs);
  char run_id[16] = "", run_name[128] = "";
  if(gs.title_id[0]) {
    snprintf(run_id, sizeof(run_id), "%s", gs.title_id);
    snprintf(run_name, sizeof(run_name), "%s",
             gs.title_name[0] ? gs.title_name : gs.title_id);
  } else if(running_by_kernel(run_id, sizeof(run_id), run_name,
                              sizeof(run_name)) != 0) {
    ps5tm_http_send_error(fd, 409, "no_game", "Es läuft kein Spiel.");
    return;
  }
  if(!id[0] || strcmp(id, run_id)) {
    char msg[256];
    snprintf(msg, sizeof(msg), "Gerade läuft „%s“, nicht das genannte Spiel. "
             "Die Seite neu laden und erneut versuchen.", run_name);
    ps5tm_http_send_error(fd, 409, "not_running", msg);
    return;
  }

  char err[640];
  if(sw_start(run_id, run_name, "", "", "", 0, err, sizeof(err)) != 0) {
    ps5tm_http_send_error(fd, 409, "busy", err);
    return;
  }
  PS5TM_INFO("game_close_requested", "Beenden von %s (%s) angefordert von %s.",
             run_name, run_id, req->peer[0] ? req->peer : "?");
  char out[96];
  snprintf(out, sizeof(out), "{\"ok\":true,\"state\":\"closing\",\"closing\":\"%s\"}",
           run_id);
  ps5tm_http_send_json(fd, 202, out);
}

static void
handle_library_close_status(int fd) {
  sw_job_t job;
  pthread_mutex_lock(&g_sw_lock);
  job = g_sw;
  pthread_mutex_unlock(&g_sw_lock);
  cJSON *root = cJSON_CreateObject();
  cJSON_AddBoolToObject(root, "ok", 1);
  cJSON_AddStringToObject(root, "state", k_sw_state[job.state]);
  cJSON_AddStringToObject(root, "closing", job.close_id);
  cJSON_AddStringToObject(root, "then", job.then_id);
  cJSON_AddStringToObject(root, "message", job.message);
  send_cjson(fd, 200, root);
}


/* Copying a game folder (gamecopy.c). The folder comes from the game list,
   never from the request; the request names only a drive the console has
   and one of two fixed destinations. */
static void
send_cjson(int fd, int status, cJSON *root) {
  char *txt = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if(!txt) {
    ps5tm_http_send_error(fd, 500, "alloc_failed", "Kein Speicher.");
    return;
  }
  ps5tm_http_send_json(fd, status, txt);
  free(txt);
}

static void
handle_copy_plan(int fd, const ps5tm_request_t *req) {
  char id[16], err[256] = "";
  query_title_id(req->query, id, sizeof(id));
  cJSON *root = ps5tm_gamecopy_plan(id, err, sizeof(err));
  if(!root) {
    ps5tm_http_send_error(fd, 409, "not_copyable", err);
    return;
  }
  send_cjson(fd, 200, root);
}

static void
handle_copy_start(int fd, const ps5tm_request_t *req) {
  cJSON *body = req->body ? cJSON_Parse(req->body) : NULL;
  const cJSON *jid  = body ? cJSON_GetObjectItem(body, "id")     : NULL;
  const cJSON *jdst = body ? cJSON_GetObjectItem(body, "target") : NULL;
  const cJSON *jm   = body ? cJSON_GetObjectItem(body, "mode")   : NULL;
  char id[16] = "", target[40] = "", mode[12] = "";
  if(cJSON_IsString(jid))  snprintf(id, sizeof(id), "%s", jid->valuestring);
  if(cJSON_IsString(jdst)) snprintf(target, sizeof(target), "%s", jdst->valuestring);
  if(cJSON_IsString(jm))   snprintf(mode, sizeof(mode), "%s", jm->valuestring);
  cJSON_Delete(body);

  char err[640] = "";
  int status = ps5tm_gamecopy_start(id, target, mode, err, sizeof(err));
  if(status != 200) {
    ps5tm_http_send_error(fd, status, "copy_refused", err);
    return;
  }
  send_cjson(fd, 200, ps5tm_gamecopy_status());
}

/* Deleting a game or one of the app's backups (gamedelete.c). The plan is a POST: it hands out the token that a
   start must carry, so it is a step of its own and not something a link could do. Both name what to delete in the body
   ({"kind":"game"|"backup","id":title id or backup path}). */
static void
delete_args(const ps5tm_request_t *req, char *kind, size_t kl, char *id, size_t il, char *tok, size_t tl) {
  kind[0] = id[0] = tok[0] = 0;
  cJSON *body = req->body ? cJSON_Parse(req->body) : NULL;
  const cJSON *k = body ? cJSON_GetObjectItem(body, "kind") : NULL;
  const cJSON *i = body ? cJSON_GetObjectItem(body, "id") : NULL;
  const cJSON *t = body ? cJSON_GetObjectItem(body, "confirm") : NULL;
  if(cJSON_IsString(k)) snprintf(kind, kl, "%s", k->valuestring);
  if(cJSON_IsString(i)) snprintf(id, il, "%s", i->valuestring);
  if(cJSON_IsString(t)) snprintf(tok, tl, "%s", t->valuestring);
  cJSON_Delete(body);
}

static void
handle_delete_plan(int fd, const ps5tm_request_t *req) {
  char kind[16], id[320], tok[40];
  delete_args(req, kind, sizeof(kind), id, sizeof(id), tok, sizeof(tok));
  int http = 0;
  cJSON *root = ps5tm_gamedelete_plan(kind, id, &http);
  if(!root) {
    ps5tm_http_send_error(fd, http == 404 ? 404 : 400, http == 404 ? "not_found" : "bad_request",
                          http == 404 ? "Das gibt es nicht (mehr)." : "Ungültige Angabe.");
    return;
  }
  send_cjson(fd, 200, root);
}

static void
handle_delete_start(int fd, const ps5tm_request_t *req) {
  char kind[16], id[320], tok[40], err[640] = "";
  delete_args(req, kind, sizeof(kind), id, sizeof(id), tok, sizeof(tok));
  int status = ps5tm_gamedelete_start(kind, id, tok, err, sizeof(err));
  if(status != 200) {
    ps5tm_http_send_error(fd, status, "delete_refused", err);
    return;
  }
  send_cjson(fd, 200, ps5tm_gamedelete_status());
}

/* Converting (gameconvert.c): the request names a title, "exfat" or
   "ffpfsc", a drive and one of the two destinations copying knows. */
static void
handle_convert_plan(int fd, const ps5tm_request_t *req) {
  char id[16], err[256] = "";
  query_title_id(req->query, id, sizeof(id));
  cJSON *root = ps5tm_gameconvert_plan(id, err, sizeof(err));
  if(!root) {
    ps5tm_http_send_error(fd, 409, "not_convertible", err);
    return;
  }
  send_cjson(fd, 200, root);
}

static void
handle_convert_start(int fd, const ps5tm_request_t *req) {
  cJSON *body = req->body ? cJSON_Parse(req->body) : NULL;
  const cJSON *jid  = body ? cJSON_GetObjectItem(body, "id")     : NULL;
  const cJSON *jop  = body ? cJSON_GetObjectItem(body, "op")     : NULL;
  const cJSON *jdst = body ? cJSON_GetObjectItem(body, "target") : NULL;
  const cJSON *jm   = body ? cJSON_GetObjectItem(body, "mode")   : NULL;
  char id[16] = "", op[12] = "", target[40] = "", mode[12] = "";
  if(cJSON_IsString(jid))  snprintf(id, sizeof(id), "%s", jid->valuestring);
  if(cJSON_IsString(jop))  snprintf(op, sizeof(op), "%s", jop->valuestring);
  if(cJSON_IsString(jdst)) snprintf(target, sizeof(target), "%s", jdst->valuestring);
  if(cJSON_IsString(jm))   snprintf(mode, sizeof(mode), "%s", jm->valuestring);
  cJSON_Delete(body);

  char err[640] = "";
  int status = ps5tm_gameconvert_start(id, op, target, mode, err, sizeof(err));
  if(status != 200) {
    ps5tm_http_send_error(fd, status, "convert_refused", err);
    return;
  }
  send_cjson(fd, 200, ps5tm_gameconvert_status());
}

/* Moving and unpacking, done by ShadowMountPlus (gamemove.c). The request
   names a title, "move" or "unpack", and a destination the plan offered. */
static void
handle_storage_plan(int fd, const ps5tm_request_t *req) {
  char id[16], op[12], err[256] = "";
  query_title_id(req->query, id, sizeof(id));
  query_param(req->query, "op", op, sizeof(op));
  cJSON *root = ps5tm_gamemove_plan(id, op, err, sizeof(err));
  if(!root) {
    ps5tm_http_send_error(fd, 409, "not_possible", err);
    return;
  }
  send_cjson(fd, 200, root);
}

static void
handle_storage_start(int fd, const ps5tm_request_t *req) {
  cJSON *body = req->body ? cJSON_Parse(req->body) : NULL;
  const cJSON *jid  = body ? cJSON_GetObjectItem(body, "id")     : NULL;
  const cJSON *jop  = body ? cJSON_GetObjectItem(body, "op")     : NULL;
  const cJSON *jdst = body ? cJSON_GetObjectItem(body, "target") : NULL;
  int del = body && cJSON_IsTrue(cJSON_GetObjectItem(body, "delete_source"));
  char id[16] = "", op[12] = "", target[128] = "";
  if(cJSON_IsString(jid))  snprintf(id, sizeof(id), "%s", jid->valuestring);
  if(cJSON_IsString(jop))  snprintf(op, sizeof(op), "%s", jop->valuestring);
  if(cJSON_IsString(jdst)) snprintf(target, sizeof(target), "%s", jdst->valuestring);
  cJSON_Delete(body);

  char err[256] = "";
  int status = ps5tm_gamemove_start(id, op, target, del, err, sizeof(err));
  if(status != 200) {
    ps5tm_http_send_error(fd, status, "storage_refused", err);
    return;
  }
  cJSON *job = ps5tm_gamemove_status(err, sizeof(err));
  if(!job) {
    ps5tm_http_send_json(fd, 200, "{\"ok\":true,\"active\":true}");
    return;
  }
  send_cjson(fd, 200, job);
}

static void
handle_storage_status(int fd) {
  char err[256] = "";
  cJSON *job = ps5tm_gamemove_status(err, sizeof(err));
  if(!job) {
    ps5tm_http_send_error(fd, 503, "smp_unavailable", err);
    return;
  }
  send_cjson(fd, 200, job);
}


/* ------------------------------------------------------------------ system */

static void
handle_system(int fd) {
  ps5tm_sysinfo_t info;
  ps5tm_sysinfo_get(&info);

  cJSON *root = cJSON_CreateObject();
  cJSON_AddBoolToObject  (root, "ok", 1);
  cJSON_AddStringToObject(root, "model", info.model);
  if(info.serial_masked[0])
    cJSON_AddStringToObject(root, "serial_masked", info.serial_masked);

  char fwstr[16];
  snprintf(fwstr, sizeof(fwstr), "0x%08X", info.firmware);
  cJSON_AddStringToObject(root, "firmware_raw", fwstr);

  char fwtext[24];
  ps5tm_platform_firmware_text(info.firmware, fwtext, sizeof(fwtext));
  cJSON_AddStringToObject(root, "firmware_version", fwtext);
  cJSON_AddStringToObject(root, "firmware_group",
                          ps5tm_platform_firmware_group(info.firmware));
  cJSON_AddStringToObject(root, "app_version", PS5TM_VERSION);

  /* What the system itself calls its software version. Kept beside our own
     decode rather than replacing it — if the two ever disagree, that is a
     fact worth seeing, not one to paper over. */
  if(info.firmware_text_valid)
    cJSON_AddStringToObject(root, "firmware_reported", info.firmware_text);

  if(info.uptime_valid)
    cJSON_AddNumberToObject(root, "uptime_sec", (double)info.uptime_sec);

  /* operating_time_sec, boot_count, icc_raw, thermal_alert, power_up_cause,
     product_shape, ram and vram were removed on 31.07.2026 — the calls behind
     them succeed and return nothing on this firmware. See sysinfo.c. */

  {
    cJSON *net = cJSON_AddObjectToObject(root, "network");
    /* Whether the probe ran at all. Without this a console that was never
       asked looks exactly like one with no network — and the page said "keine
       Angaben verfügbar" while being served over that very network, with no
       hint that a switch under "Zusatzabfragen" was all it needed. The
       controller object has carried its probe_mask for this reason all along;
       the network object did not. */
    cJSON_AddBoolToObject(net, "probed",
                          (g_config.probe_mask & PS5TM_PROBE_NETDISP) != 0);
    cJSON_AddBoolToObject(net, "up", info.net_up);
    if(info.net_up) {
      cJSON_AddStringToObject(net, "kind", info.net_wifi ? "wlan" : "kabel");
      if(info.net_ip[0])   cJSON_AddStringToObject(net, "ip",   info.net_ip);
      if(info.net_ssid[0]) cJSON_AddStringToObject(net, "ssid", info.net_ssid);
      if(info.net_rssi_valid)
        cJSON_AddNumberToObject(net, "signal_pct", info.net_rssi_pct);
      if(info.net_link_valid)
        cJSON_AddNumberToObject(net, "link_mbit", info.net_link_mbit);
      if(info.net_band_valid)
        cJSON_AddNumberToObject(net, "band_ghz", info.net_band_ghz10 / 10.0);
    }
  }

  if(info.tv_name[0] || info.tv_resolution_valid) {
    cJSON *tv = cJSON_AddObjectToObject(root, "display");
    if(info.tv_name[0]) cJSON_AddStringToObject(tv, "name", info.tv_name);
    if(info.tv_resolution_valid) {
      cJSON_AddNumberToObject(tv, "width",  info.tv_width);
      cJSON_AddNumberToObject(tv, "height", info.tv_height);
    }
    if(info.tv_refresh_valid)
      cJSON_AddNumberToObject(tv, "refresh_hz", info.tv_refresh_hz);
    if(info.tv_hdr_valid) cJSON_AddBoolToObject(tv, "hdr", info.tv_hdr);
  }

  {
    ps5tm_pad_t pad;
    ps5tm_pad_get(&pad);

    cJSON *c = cJSON_AddObjectToObject(root, "controller");
    cJSON_AddNumberToObject(c, "probe_mask", g_config.probe_mask);
    cJSON_AddBoolToObject(c, "connected", pad.connected);
    /* Always, not only on success. "connected: false" on its own cannot say
       whether the symbols are missing, the open was refused, or there really
       is no controller — and on 01.08.2026 all three looked identical from
       outside while a game was being played with that controller. */
    cJSON_AddNumberToObject(c, "probe_rc", pad.probe_rc);

    /* Whether a controller is genuinely in use. "connected" above cannot say
       — scePad refuses this process, so it is false even with a game being
       played. A logged press of the PS button is real evidence. */
    if(pad.press_seen) {
      cJSON_AddBoolToObject  (c, "in_use", 1);
      cJSON_AddNumberToObject(c, "press_device_id", pad.press_device_id);
      cJSON_AddNumberToObject(c, "press_age_s",
        pad.last_press_ms
          ? (double)((ps5tm_now_ms() - pad.last_press_ms) / 1000) : -1);
    }

    /* Read out of the kernel log, so it is a last-known figure, not a live
       one. The age travels with it — a stale percentage presented as current
       would be worse than none. */
    if(pad.from_log) {
      cJSON_AddBoolToObject  (c, "from_log", 1);
      cJSON_AddBoolToObject  (c, "restored", pad.restored_from_disk != 0);
      cJSON_AddNumberToObject(c, "battery_pct", pad.battery_pct);
      cJSON_AddBoolToObject  (c, "charging", pad.charging);
      cJSON_AddNumberToObject(c, "age_s",
        pad.battery_seen_ms
          ? (double)((ps5tm_now_ms() - pad.battery_seen_ms) / 1000) : -1);
    }

    if(pad.battery_valid && !pad.from_log) {
      cJSON_AddNumberToObject(c, "battery_pct", pad.battery_pct);
      cJSON_AddBoolToObject  (c, "charging", pad.charging);
      cJSON_AddBoolToObject  (c, "full",     pad.full);
      /* Until the offset has been confirmed against a full and an empty
         controller, say so — a number nobody can check is worse than one
         that admits what it is. */
      cJSON_AddBoolToObject  (c, "calibrated", pad.candidates == 1);
      cJSON_AddNumberToObject(c, "status_offset", pad.status_offset);
      cJSON_AddNumberToObject(c, "candidates",    pad.candidates);
      char sb[8];
      snprintf(sb, sizeof(sb), "0x%02X", pad.status_byte);
      cJSON_AddStringToObject(c, "status_byte", sb);
    }
    if(pad.raw_len > 0) {
      char hex[2 * 96 + 1];
      int  o = 0;
      for(int i = 0; i < pad.raw_len && o + 2 < (int)sizeof(hex); i++)
        o += snprintf(hex + o, sizeof(hex) - (size_t)o, "%02x", pad.raw[i]);
      cJSON_AddStringToObject(c, "raw", hex);
    }
  }

  {
    ps5tm_user_t prof;
    ps5tm_user_get(&prof);

    cJSON *u = cJSON_AddObjectToObject(root, "user");
    cJSON_AddBoolToObject  (u, "valid",    prof.valid);
    cJSON_AddNumberToObject(u, "name_max", prof.name_max);
    if(prof.valid) {
      char uid_hex[16];
      snprintf(uid_hex, sizeof(uid_hex), "0x%08X", prof.uid);
      cJSON_AddNumberToObject(u, "uid",     (double)prof.uid);
      cJSON_AddStringToObject(u, "uid_hex", uid_hex);
      cJSON_AddStringToObject(u, "username", prof.username);
      cJSON_AddBoolToObject  (u, "can_rename", prof.can_rename);
    }
  }

  {
    ps5tm_regstats_t rs;
    ps5tm_regstats_get(&rs);
    if(rs.nickname[0])
      cJSON_AddStringToObject(root, "console_name", rs.nickname);

    if(rs.language_valid || rs.time_zone_valid || rs.timezone_offset_valid ||
       rs.region_valid) {
      cJSON *loc = cJSON_AddObjectToObject(root, "locale");
      if(rs.language_valid)
        cJSON_AddNumberToObject(loc, "language", rs.language_code);
      if(rs.time_zone_valid)
        cJSON_AddNumberToObject(loc, "time_zone", rs.time_zone_code);
      if(rs.timezone_offset_valid)
        cJSON_AddNumberToObject(loc, "timezone_offset_min",
                                rs.timezone_offset_min);
      if(rs.region_valid)
        cJSON_AddStringToObject(loc, "region_code", rs.region_code);
    }
  }

  /* Memory and power state, read out of the console's log. The syscall route
     for memory returned nothing on this firmware and was removed in 1.9.5 —
     these figures come from SceShellCore's own printout instead. */
  {
    ps5tm_sysstate_t st;
    ps5tm_sysstate_get(&st);
    if(st.valid) {
      cJSON *m = cJSON_AddObjectToObject(root, "memory");
      if(st.rss_mb    > 0) cJSON_AddNumberToObject(m, "rss_mb",    st.rss_mb);
      if(st.kernel_mb > 0) cJSON_AddNumberToObject(m, "kernel_mb", st.kernel_mb);
      if(st.wire_mb   > 0) cJSON_AddNumberToObject(m, "wire_mb",   st.wire_mb);
      cJSON_AddNumberToObject(m, "swap_mb", st.swap_mb);
      if(st.pt_cpu_total > 0) {
        cJSON_AddNumberToObject(m, "pt_cpu_used",  st.pt_cpu_used);
        cJSON_AddNumberToObject(m, "pt_cpu_total", st.pt_cpu_total);
        cJSON_AddNumberToObject(m, "pt_gpu_used",  st.pt_gpu_used);
        cJSON_AddNumberToObject(m, "pt_gpu_total", st.pt_gpu_total);
      }
      if(st.proc_count) {
        cJSON *pa = cJSON_AddArrayToObject(m, "processes");
        for(int i = 0; i < st.proc_count; i++) {
          cJSON *e = cJSON_CreateObject();
          cJSON_AddStringToObject(e, "name",     st.procs[i].name);
          cJSON_AddNumberToObject(e, "used_mb",  st.procs[i].used_mb);
          cJSON_AddNumberToObject(e, "total_mb", st.procs[i].total_mb);
          cJSON_AddItemToArray(pa, e);
        }
      }

      cJSON *pw = cJSON_AddObjectToObject(root, "power_state");
      if(st.power_mode[0])
        cJSON_AddStringToObject(pw, "mode", st.power_mode);
      if(st.idle_sec >= 0)
        cJSON_AddNumberToObject(pw, "idle_s", st.idle_sec);
      cJSON_AddNumberToObject(pw, "age_s",
        st.seen_ms ? (double)((ps5tm_now_ms() - st.seen_ms) / 1000) : -1);
    }
  }

  cJSON *vols = cJSON_AddArrayToObject(root, "volumes");
  for(unsigned i = 0; i < info.volume_count; i++) {
    cJSON *v = cJSON_CreateObject();
    cJSON_AddStringToObject(v, "label", info.volumes[i].label);
    cJSON_AddStringToObject(v, "path",  info.volumes[i].path);
    cJSON_AddNumberToObject(v, "total_bytes", (double)info.volumes[i].total_bytes);
    cJSON_AddNumberToObject(v, "used_bytes",  (double)info.volumes[i].used_bytes);
    cJSON_AddNumberToObject(v, "free_bytes",  (double)info.volumes[i].free_bytes);
    cJSON_AddItemToArray(vols, v);
  }

  char *txt = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if(!txt) {
    ps5tm_http_send_error(fd, 500, "alloc_failed", "Kein Speicher.");
    return;
  }
  ps5tm_http_send(fd, 200, "OK", "application/json", txt, strlen(txt), NULL);
  free(txt);
}


/* ------------------------------------------------------------------ profile */

/* Renaming the console user.
 *
 * The uid is not taken from the request. The browser has no business naming
 * which account gets renamed — it would be one typo away from renaming a
 * different profile — so the payload resolves the foreground user itself and
 * the request only carries the new name. */
static void
handle_profile_username(int fd, const ps5tm_request_t *req) {
  cJSON *root = req->body ? cJSON_Parse(req->body) : NULL;
  const cJSON *n = root ? cJSON_GetObjectItem(root, "name") : NULL;

  if(!cJSON_IsString(n) || !n->valuestring) {
    cJSON_Delete(root);
    ps5tm_http_send_error(fd, 400, "missing_name", "Kein Name angegeben.");
    return;
  }

  char name[PS5TM_USERNAME_MAX + 1];
  snprintf(name, sizeof(name), "%s", n->valuestring);
  int too_long = (strlen(n->valuestring) > PS5TM_USERNAME_MAX);
  cJSON_Delete(root);

  if(too_long) {
    ps5tm_http_send_error(fd, 400, "name_too_long",
                          "Die PS5 erlaubt höchstens 16 Zeichen.");
    return;
  }

  ps5tm_user_t prof;
  ps5tm_user_get(&prof);
  if(!prof.valid) {
    ps5tm_http_send_error(fd, 409, "no_user",
                          "Es konnte kein angemeldeter Benutzer ermittelt "
                          "werden.");
    return;
  }

  const char *why = NULL;
  if(ps5tm_user_set_name(prof.uid, name, &why) != 0) {
    ps5tm_http_send_error(fd, 400, "rename_failed",
                          why ? why : "Umbenennen fehlgeschlagen.");
    return;
  }

  /* Read it back rather than echoing the request: the console is the
     authority on what the name ended up as. */
  char now[PS5TM_USERNAME_MAX + 1] = {0};
  ps5tm_user_read_name(prof.uid, now, sizeof(now));

  cJSON *out = cJSON_CreateObject();
  cJSON_AddBoolToObject  (out, "ok", 1);
  cJSON_AddStringToObject(out, "username", now[0] ? now : name);
  char *txt = cJSON_PrintUnformatted(out);
  cJSON_Delete(out);
  if(!txt) {
    ps5tm_http_send_error(fd, 500, "alloc_failed", "Kein Speicher.");
    return;
  }
  ps5tm_http_send(fd, 200, "OK", "application/json", txt, strlen(txt), NULL);
  free(txt);
}


/* ------------------------------------------------------------------- config */

static void
handle_config_get(int fd) {
  char body[4096];
  char bind_esc[160];
  char revert[96] = "";
  size_t o = 0;

  /* While a timed probe session is running the page can pick it up again
     after a reload: how long is left, and the mask that comes back. */
  unsigned back_mask = 0, left_s = 0;
  if(ps5tm_config_probe_revert_state(&back_mask, &left_s))
    snprintf(revert, sizeof(revert),
             ",\"probe_revert_in_s\":%u,\"probe_revert_mask\":%u",
             left_s, back_mask);

  ps5tm_config_lock();
  json_escape(g_config.bind_address, bind_esc, sizeof(bind_esc));
  o += (size_t)snprintf(body + o, sizeof(body) - o,
      "{\"ok\":true,\"schema_version\":%u,\"mode\":\"%s\","
      "\"fan_threshold_c\":%u,\"fan_reapply_sec\":%u,"
      "\"warning_cpu_c\":%u,\"warning_soc_c\":%u,"
      "\"http_port\":%u,\"bind_address\":\"%s\",\"probe_mask\":%u%s,"
      "\"target_temp_c\":%u,\"target_min_c\":%u,\"target_max_c\":%u,"
      "\"threshold_min_c\":%u,\"threshold_max_c\":%u,"
      "\"safety_min_c\":%u,\"safety_max_c\":%u,"
      /* The five below are reported, not offered. They follow the profile —
         see the PUT handler. band_min_c/band_max_c used to sit here too and
         advertised a range of 5..30 that nothing could actually select; a
         limit on a value you cannot set is an invitation to a bug report. */
      "\"control_band_c\":%u,"
      "\"deadband_c\":%u,\"control_interval_s\":%u,\"average_window_s\":%u,"
      "\"max_step_pct\":%u,\"profile_owns_control_values\":true,"
      "\"safety_temp_c\":%u,"
      "\"warning_countdown_s\":%u,"
      "\"telemetry_retention_days\":%u,"
      "\"lightbar_enabled\":%u,"
      "\"lightbar_warn_c\":%u,"
      "\"lightbar_hot_c\":%u,"
      "\"ps_button_status\":%u,"
      "\"library_cache\":%u,"
      "\"profile\":\"%s\","
      "\"curve\":[",
      g_config.schema_version,
      g_config.mode == PS5TM_MODE_AUTOMATIC ? "automatic" : "observe",
      g_config.fan_threshold_c, g_config.fan_reapply_sec,
      g_config.warning_cpu_c,
      g_config.warning_soc_c, g_config.http_port, bind_esc,
      g_config.probe_mask, revert,
      g_config.target_temp_c, PS5TM_TARGET_MIN_C, PS5TM_TARGET_MAX_C,
      PS5TM_THRESHOLD_MIN_C, PS5TM_THRESHOLD_MAX_C,
      PS5TM_SAFETY_MIN_C, PS5TM_SAFETY_MAX_C,
      g_config.control_band_c,
      g_config.deadband_c, g_config.control_interval_s,
      g_config.average_window_s, g_config.max_step_pct,
      g_config.safety_temp_c,
      g_config.warning_countdown_s,
      g_config.telemetry_retention_days,
      g_config.lightbar_enabled,
      g_config.lightbar_warn_c,
      g_config.lightbar_hot_c,
      g_config.ps_button_status,
      g_config.library_cache,
      g_config.profile == PS5TM_PROFILE_COOL       ? "cool"
      : g_config.profile == PS5TM_PROFILE_BALANCED ? "balanced"
                                                   : "comfort");

  for(unsigned i = 0; i < g_config.curve_len && o < sizeof(body) - 64; i++) {
    o += (size_t)snprintf(body + o, sizeof(body) - o,
                          "%s{\"temperature_c\":%u,\"duty_pct\":%u}",
                          i ? "," : "",
                          g_config.curve[i].temperature_c,
                          g_config.curve[i].duty_pct);
  }
  ps5tm_config_unlock();

  snprintf(body + o, sizeof(body) - o, "]}");
  ps5tm_http_send_json(fd, 200, body);
}


static void
handle_config_put(int fd, const ps5tm_request_t *req) {
  if(!req->body || !req->body_len) {
    ps5tm_http_send_error(fd, 400, "empty_body",
                          "Es wurden keine Daten übermittelt.");
    return;
  }

  cJSON *root = cJSON_Parse(req->body);
  if(!root) {
    ps5tm_http_send_error(fd, 400, "invalid_json",
                          "Die Konfiguration ist kein gültiges JSON.");
    return;
  }

  /* Refused before anything is touched: a bind address that cannot be bound
     would otherwise be accepted, saved, and only found out at the next start. */
  const cJSON *bind = cJSON_GetObjectItem(root, "bind_address");
  if(cJSON_IsString(bind) && !ps5tm_config_bind_valid(bind->valuestring)) {
    cJSON_Delete(root);
    ps5tm_http_send_error(fd, 400, "invalid_bind_address",
                          "bind_address muss eine IPv4-Adresse sein, "
                          "zum Beispiel 0.0.0.0.");
    return;
  }

  /* The port is where the page itself lives: a value the console cannot use
     leaves the person with no page to correct it on, and the tile fixed on
     8086 with nothing behind it. 0 and the like also arrive when a form sends
     an empty field as a number. */
  const cJSON *port = cJSON_GetObjectItem(root, "http_port");
  if(cJSON_IsNumber(port)) {
    if(port->valuedouble < 1024 || port->valuedouble > 65535) {
      cJSON_Delete(root);
      ps5tm_http_send_error(fd, 400, "invalid_port",
                            "http_port muss zwischen 1024 und 65535 liegen.");
      return;
    }
    unsigned want = ps5tm_num_u32(port->valuedouble);
    ps5tm_config_lock();
    unsigned have = g_config.http_port;
    ps5tm_config_unlock();
    if(want != have && !ps5tm_http_port_free(want)) {
      cJSON_Delete(root);
      char msg[96];
      snprintf(msg, sizeof(msg), "Port %u ist auf der Konsole schon belegt.", want);
      ps5tm_http_send_error(fd, 409, "port_in_use", msg);
      return;
    }
  }

  /* A timed probe session: probe_mask with probe_revert_after_s. */
  const cJSON *jmask   = cJSON_GetObjectItem(root, "probe_mask");
  const cJSON *jrevert = cJSON_GetObjectItem(root, "probe_revert_after_s");
  int      has_probe   = cJSON_IsNumber(jmask);
  unsigned revert_secs = 0;
  if(jrevert) {
    if(!has_probe || !cJSON_IsNumber(jrevert) ||
       jrevert->valuedouble < 10 || jrevert->valuedouble > 900) {
      cJSON_Delete(root);
      ps5tm_http_send_error(fd, 400, "invalid_probe_revert",
                            "probe_revert_after_s braucht probe_mask und "
                            "muss zwischen 10 und 900 liegen.");
      return;
    }
    revert_secs = ps5tm_num_u32(jrevert->valuedouble);
  }

  ps5tm_config_lock();
  ps5tm_config_t next = g_config;   /* anything absent keeps its old value */

  const cJSON *it;
  if(cJSON_IsString(it = cJSON_GetObjectItem(root, "mode")))
    next.mode = strcmp(it->valuestring, "automatic") == 0
                  ? PS5TM_MODE_AUTOMATIC : PS5TM_MODE_OBSERVE;
  if(cJSON_IsNumber(it = cJSON_GetObjectItem(root, "fan_threshold_c")))
    next.fan_threshold_c = ps5tm_num_u32(it->valuedouble);
  if(cJSON_IsNumber(it = cJSON_GetObjectItem(root, "fan_reapply_sec")))
    next.fan_reapply_sec = ps5tm_num_u32(it->valuedouble);
  if(cJSON_IsNumber(it = cJSON_GetObjectItem(root, "warning_cpu_c")))
    next.warning_cpu_c = ps5tm_num_u32(it->valuedouble);
  if(cJSON_IsNumber(it = cJSON_GetObjectItem(root, "warning_soc_c")))
    next.warning_soc_c = ps5tm_num_u32(it->valuedouble);
  if(cJSON_IsNumber(it = cJSON_GetObjectItem(root, "http_port")))
    next.http_port = ps5tm_num_u32(it->valuedouble);
  if(cJSON_IsString(it = cJSON_GetObjectItem(root, "bind_address")))
    snprintf(next.bind_address, sizeof(next.bind_address), "%s", it->valuestring);
  if(cJSON_IsNumber(it = cJSON_GetObjectItem(root, "probe_mask")))
    next.probe_mask = ps5tm_num_u32(it->valuedouble);

  if(cJSON_IsNumber(it = cJSON_GetObjectItem(root, "target_temp_c")))
    next.target_temp_c = ps5tm_num_u32(it->valuedouble);
  if(cJSON_IsNumber(it = cJSON_GetObjectItem(root, "safety_temp_c")))
    next.safety_temp_c = ps5tm_num_u32(it->valuedouble);
  if(cJSON_IsNumber(it = cJSON_GetObjectItem(root, "warning_countdown_s")))
    next.warning_countdown_s = ps5tm_num_u32(it->valuedouble);
  if(cJSON_IsNumber(it = cJSON_GetObjectItem(root, "telemetry_retention_days")))
    next.telemetry_retention_days = ps5tm_num_u32(it->valuedouble);
  if(cJSON_IsNumber(it = cJSON_GetObjectItem(root, "lightbar_enabled")))
    next.lightbar_enabled = ps5tm_num_u32(it->valuedouble);
  if(cJSON_IsNumber(it = cJSON_GetObjectItem(root, "lightbar_warn_c")))
    next.lightbar_warn_c = ps5tm_num_u32(it->valuedouble);
  if(cJSON_IsNumber(it = cJSON_GetObjectItem(root, "lightbar_hot_c")))
    next.lightbar_hot_c = ps5tm_num_u32(it->valuedouble);
  if(cJSON_IsNumber(it = cJSON_GetObjectItem(root, "ps_button_status")))
    next.ps_button_status = ps5tm_num_u32(it->valuedouble);
  if(cJSON_IsNumber(it = cJSON_GetObjectItem(root, "library_cache")))
    next.library_cache = ps5tm_num_u32(it->valuedouble);

  /* control_band_c, deadband_c, control_interval_s, average_window_s and
     max_step_pct are deliberately NOT read here.
     They belong to the profile. ps5tm_config_clamp() calls
     apply_profile_presets() unconditionally, so anything accepted here was
     overwritten a few lines later — the request was stored, saved to disk,
     answered with 200 OK, and silently discarded. Taking a value the caller
     cannot influence is worse than refusing it, so the pretence is gone. The
     GET response still reports the effective five, which is the honest half:
     it says what the profile chose, not what may be chosen. */
  if(cJSON_IsString(it = cJSON_GetObjectItem(root, "profile"))) {
    next.profile = !strcmp(it->valuestring, "cool")     ? PS5TM_PROFILE_COOL
                 : !strcmp(it->valuestring, "balanced") ? PS5TM_PROFILE_BALANCED
                 : PS5TM_PROFILE_COMFORT;
  }

  const cJSON *curve = cJSON_GetObjectItem(root, "curve");
  if(cJSON_IsArray(curve)) {
    unsigned len = 0;
    const cJSON *pt;
    cJSON_ArrayForEach(pt, curve) {
      if(len >= PS5TM_CURVE_MAX) break;
      const cJSON *t = cJSON_GetObjectItem(pt, "temperature_c");
      const cJSON *d = cJSON_GetObjectItem(pt, "duty_pct");
      if(!cJSON_IsNumber(t) || !cJSON_IsNumber(d)) continue;
      next.curve[len].temperature_c = ps5tm_num_u32(t->valuedouble);
      next.curve[len].duty_pct      = ps5tm_num_u32(d->valuedouble);
      len++;
    }
    if(len >= 2) next.curve_len = len;
  }
  cJSON_Delete(root);

  ps5tm_mode_t before = g_config.mode;
  unsigned before_probe = g_config.probe_mask;
  unsigned before_cache = g_config.library_cache;
  g_config = next;
  ps5tm_config_clamp(&g_config);
  if(has_probe) {
    if(revert_secs) ps5tm_config_probe_revert_arm(before_probe, revert_secs);
    else            ps5tm_config_probe_revert_cancel();   /* a plain change wins */
  }
  ps5tm_mode_t after = g_config.mode;
  unsigned after_cache = g_config.library_cache;
  ps5tm_config_unlock();
  int saved = (ps5tm_config_save() == 0);

  if(before_cache != after_cache) {
    ps5tm_library_forget();               /* the next list is read afresh: it fills (or stops filling) the folder */
    PS5TM_INFO("library_cache_switched", "Covers & Metadaten speichern: %s.", after_cache ? "an" : "aus");
  }
  if(before != after) {
    PS5TM_INFO("mode_changed", "Betriebsart: %s",
               after == PS5TM_MODE_AUTOMATIC
                 ? "automatische Regelung" : "nur beobachten");
  }

  if(!saved) {
    ps5tm_http_send_error(fd, 500, "config_save_failed",
                          "Die Konfiguration konnte nicht gespeichert werden.");
    return;
  }
  ps5tm_http_send_json(fd, 200, "{\"ok\":true}");
}


static void
handle_config_export(int fd) {
  ps5tm_config_lock();

  cJSON *root = cJSON_CreateObject();
  cJSON_AddBoolToObject(root, "ok", 1);
  cJSON_AddStringToObject(root, "format", "ps5tm-config-v1");
  cJSON_AddNumberToObject(root, "exported_at_ms", (double)ps5tm_now_ms());

  cJSON *cfg = cJSON_AddObjectToObject(root, "config");
  cJSON_AddStringToObject(cfg, "mode",
      g_config.mode == PS5TM_MODE_AUTOMATIC ? "automatic" : "observe");
  cJSON_AddNumberToObject(cfg, "fan_threshold_c", g_config.fan_threshold_c);
  cJSON_AddNumberToObject(cfg, "fan_reapply_sec", g_config.fan_reapply_sec);
  cJSON_AddNumberToObject(cfg, "warning_cpu_c", g_config.warning_cpu_c);
  cJSON_AddNumberToObject(cfg, "warning_soc_c", g_config.warning_soc_c);
  cJSON_AddNumberToObject(cfg, "http_port", g_config.http_port);
  cJSON_AddStringToObject(cfg, "bind_address", g_config.bind_address);
  cJSON_AddNumberToObject(cfg, "probe_mask", g_config.probe_mask);
  cJSON_AddNumberToObject(cfg, "target_temp_c", g_config.target_temp_c);
  cJSON_AddNumberToObject(cfg, "safety_temp_c", g_config.safety_temp_c);
  cJSON_AddNumberToObject(cfg, "warning_countdown_s", g_config.warning_countdown_s);
  cJSON_AddNumberToObject(cfg, "telemetry_retention_days",
                          g_config.telemetry_retention_days);
  cJSON_AddNumberToObject(cfg, "lightbar_enabled", g_config.lightbar_enabled);
  cJSON_AddNumberToObject(cfg, "lightbar_warn_c", g_config.lightbar_warn_c);
  cJSON_AddNumberToObject(cfg, "lightbar_hot_c", g_config.lightbar_hot_c);
  cJSON_AddNumberToObject(cfg, "ps_button_status", g_config.ps_button_status);
  cJSON_AddNumberToObject(cfg, "library_cache", g_config.library_cache);
  cJSON_AddStringToObject(cfg, "profile",
      g_config.profile == PS5TM_PROFILE_COOL ? "cool"
      : g_config.profile == PS5TM_PROFILE_BALANCED ? "balanced"
      : "comfort");

  cJSON *curve = cJSON_AddArrayToObject(cfg, "curve");
  for(unsigned i = 0; i < g_config.curve_len; i++) {
    cJSON *pt = cJSON_CreateObject();
    cJSON_AddNumberToObject(pt, "temperature_c", g_config.curve[i].temperature_c);
    cJSON_AddNumberToObject(pt, "duty_pct",      g_config.curve[i].duty_pct);
    cJSON_AddItemToArray(curve, pt);
  }

  cJSON *rules = cJSON_AddArrayToObject(cfg, "game_rules");
  for(unsigned i = 0; i < g_config.game_rule_count; i++) {
    const ps5tm_game_rule_t *g = &g_config.game_rules[i];
    cJSON *e = cJSON_CreateObject();
    cJSON_AddStringToObject(e, "title_id", g->title_id);
    cJSON_AddStringToObject(e, "title_name", g->title_name);
    cJSON_AddNumberToObject(e, "target_temp_c", g->target_temp_c);
    cJSON_AddStringToObject(e, "profile",
      g->profile == PS5TM_PROFILE_COOL ? "cool"
      : g->profile == PS5TM_PROFILE_BALANCED ? "balanced"
      : "comfort");
    cJSON_AddItemToArray(rules, e);
  }

  ps5tm_config_unlock();

  char *txt = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if(!txt) {
    ps5tm_http_send_error(fd, 500, "alloc_failed", "Kein Speicher.");
    return;
  }
  ps5tm_http_send(fd, 200, "OK", "application/json", txt, strlen(txt),
                  "Content-Disposition: attachment; "
                  "filename=ps5tm-config.json\r\n");
  free(txt);
}


static void
handle_config_import(int fd, const ps5tm_request_t *req) {
  if(!req->body || !req->body_len) {
    ps5tm_http_send_error(fd, 400, "empty_body",
                          "Es wurden keine Daten übermittelt.");
    return;
  }

  cJSON *root = cJSON_Parse(req->body);
  if(!root) {
    ps5tm_http_send_error(fd, 400, "invalid_json",
                          "Die Importdatei ist kein gültiges JSON.");
    return;
  }

  cJSON *cfg = cJSON_GetObjectItem(root, "config");
  if(!cJSON_IsObject(cfg)) cfg = root; /* kompatibel zu nacktem Config-JSON */

  ps5tm_config_lock();
  ps5tm_config_t next = g_config;

  const cJSON *it;
  if(cJSON_IsString(it = cJSON_GetObjectItem(cfg, "mode")))
    next.mode = strcmp(it->valuestring, "automatic") == 0
                  ? PS5TM_MODE_AUTOMATIC : PS5TM_MODE_OBSERVE;
  if(cJSON_IsNumber(it = cJSON_GetObjectItem(cfg, "fan_threshold_c")))
    next.fan_threshold_c = ps5tm_num_u32(it->valuedouble);
  if(cJSON_IsNumber(it = cJSON_GetObjectItem(cfg, "fan_reapply_sec")))
    next.fan_reapply_sec = ps5tm_num_u32(it->valuedouble);
  if(cJSON_IsNumber(it = cJSON_GetObjectItem(cfg, "warning_cpu_c")))
    next.warning_cpu_c = ps5tm_num_u32(it->valuedouble);
  if(cJSON_IsNumber(it = cJSON_GetObjectItem(cfg, "warning_soc_c")))
    next.warning_soc_c = ps5tm_num_u32(it->valuedouble);
  /* Someone else's file must never take the page away from this console: its
     port is only taken over when it is a usable one and nothing else holds it. */
  if(cJSON_IsNumber(it = cJSON_GetObjectItem(cfg, "http_port"))) {
    unsigned want = ps5tm_num_u32(it->valuedouble);
    if(want >= 1024 && want <= 65535 &&
       (want == next.http_port || ps5tm_http_port_free(want)))
      next.http_port = want;
  }
  if(cJSON_IsString(it = cJSON_GetObjectItem(cfg, "bind_address")))
    snprintf(next.bind_address, sizeof(next.bind_address), "%s", it->valuestring);
  if(cJSON_IsNumber(it = cJSON_GetObjectItem(cfg, "probe_mask")))
    next.probe_mask = ps5tm_num_u32(it->valuedouble);
  if(cJSON_IsNumber(it = cJSON_GetObjectItem(cfg, "target_temp_c")))
    next.target_temp_c = ps5tm_num_u32(it->valuedouble);
  if(cJSON_IsNumber(it = cJSON_GetObjectItem(cfg, "safety_temp_c")))
    next.safety_temp_c = ps5tm_num_u32(it->valuedouble);
  if(cJSON_IsNumber(it = cJSON_GetObjectItem(cfg, "warning_countdown_s")))
    next.warning_countdown_s = ps5tm_num_u32(it->valuedouble);
  if(cJSON_IsNumber(it = cJSON_GetObjectItem(cfg, "telemetry_retention_days")))
    next.telemetry_retention_days = ps5tm_num_u32(it->valuedouble);
  if(cJSON_IsNumber(it = cJSON_GetObjectItem(cfg, "lightbar_enabled")))
    next.lightbar_enabled = ps5tm_num_u32(it->valuedouble);
  if(cJSON_IsNumber(it = cJSON_GetObjectItem(cfg, "lightbar_warn_c")))
    next.lightbar_warn_c = ps5tm_num_u32(it->valuedouble);
  if(cJSON_IsNumber(it = cJSON_GetObjectItem(cfg, "lightbar_hot_c")))
    next.lightbar_hot_c = ps5tm_num_u32(it->valuedouble);
  if(cJSON_IsNumber(it = cJSON_GetObjectItem(cfg, "ps_button_status")))
    next.ps_button_status = ps5tm_num_u32(it->valuedouble);
  if(cJSON_IsNumber(it = cJSON_GetObjectItem(cfg, "library_cache")))
    next.library_cache = ps5tm_num_u32(it->valuedouble);
  if(cJSON_IsString(it = cJSON_GetObjectItem(cfg, "profile"))) {
    next.profile = !strcmp(it->valuestring, "cool")     ? PS5TM_PROFILE_COOL
                 : !strcmp(it->valuestring, "balanced") ? PS5TM_PROFILE_BALANCED
                 : PS5TM_PROFILE_COMFORT;
  }

  const cJSON *curve = cJSON_GetObjectItem(cfg, "curve");
  if(cJSON_IsArray(curve)) {
    unsigned len = 0;
    const cJSON *pt;
    cJSON_ArrayForEach(pt, curve) {
      if(len >= PS5TM_CURVE_MAX) break;
      const cJSON *t = cJSON_GetObjectItem(pt, "temperature_c");
      const cJSON *d = cJSON_GetObjectItem(pt, "duty_pct");
      if(!cJSON_IsNumber(t) || !cJSON_IsNumber(d)) continue;
      next.curve[len].temperature_c = ps5tm_num_u32(t->valuedouble);
      next.curve[len].duty_pct      = ps5tm_num_u32(d->valuedouble);
      len++;
    }
    if(len >= 2) next.curve_len = len;
  }

  const cJSON *rules = cJSON_GetObjectItem(cfg, "game_rules");
  if(!cJSON_IsArray(rules)) rules = cJSON_GetObjectItem(cfg, "rules");
  if(cJSON_IsArray(rules)) {
    memset(next.game_rules, 0, sizeof(next.game_rules));
    next.game_rule_count = 0;
    const cJSON *e;
    cJSON_ArrayForEach(e, rules) {
      if(next.game_rule_count >= PS5TM_MAX_GAME_RULES) break;
      const cJSON *tid = cJSON_GetObjectItem(e, "title_id");
      if(!cJSON_IsString(tid) || !rule_id_ok(tid->valuestring))
        continue;

      ps5tm_game_rule_t *g = &next.game_rules[next.game_rule_count];
      snprintf(g->title_id, sizeof(g->title_id), "%s", tid->valuestring);

      const cJSON *nm = cJSON_GetObjectItem(e, "title_name");
      if(cJSON_IsString(nm) && nm->valuestring)
        ps5tm_copy_utf8(g->title_name, sizeof(g->title_name), nm->valuestring);

      const cJSON *tt = cJSON_GetObjectItem(e, "target_temp_c");
      g->target_temp_c = cJSON_IsNumber(tt)
        ? ps5tm_num_u32(tt->valuedouble)
        : next.target_temp_c;

      const cJSON *pf = cJSON_GetObjectItem(e, "profile");
      g->profile = cJSON_IsString(pf)
        ? (!strcmp(pf->valuestring, "cool") ? PS5TM_PROFILE_COOL
          : !strcmp(pf->valuestring, "balanced") ? PS5TM_PROFILE_BALANCED
          : PS5TM_PROFILE_COMFORT)
        : next.profile;

      next.game_rule_count++;
    }
  }

  g_config = next;
  ps5tm_config_clamp(&g_config);
  ps5tm_config_probe_revert_cancel();     /* an import is an explicit setting */
  ps5tm_config_unlock();
  cJSON_Delete(root);
  int saved = (ps5tm_config_save() == 0);

  if(!saved) {
    ps5tm_http_send_error(fd, 500, "config_save_failed",
                          "Die importierte Konfiguration konnte nicht "
                          "gespeichert werden.");
    return;
  }

  ps5tm_http_send_json(fd, 200, "{\"ok\":true}");
}


/* -------------------------------------------------------------- fan control */

static void
handle_fan_threshold(int fd, const ps5tm_request_t *req) {
  if(!req->body) {
    ps5tm_http_send_error(fd, 400, "empty_body", "Es fehlt der Anfragetext.");
    return;
  }

  cJSON *root = cJSON_Parse(req->body);
  const cJSON *th = root ? cJSON_GetObjectItem(root, "threshold_c") : NULL;
  if(!cJSON_IsNumber(th)) {
    cJSON_Delete(root);
    ps5tm_http_send_error(fd, 400, "invalid_threshold",
                          "threshold_c muss eine ganze Zahl sein.");
    return;
  }
  int requested = ps5tm_num_i32(th->valuedouble);
  cJSON_Delete(root);

  ps5tm_config_lock();
  int automatic = (g_config.mode == PS5TM_MODE_AUTOMATIC);
  ps5tm_config_unlock();

  /* In automatic mode the slider no longer sets a raw ICC threshold — the
     controller owns that — but the temperature the console should be held
     at, which is the number users actually care about. */
  if(automatic) {
    int clamped = requested;
    if(clamped < PS5TM_TARGET_MIN_C) clamped = PS5TM_TARGET_MIN_C;
    if(clamped > PS5TM_TARGET_MAX_C) clamped = PS5TM_TARGET_MAX_C;

    ps5tm_config_lock();
    g_config.target_temp_c = (unsigned)clamped;
    ps5tm_config_unlock();
    int saved = (ps5tm_config_save() == 0);

    if(!saved) {
      ps5tm_http_send_error(fd, 500, "threshold_save_failed",
                            "Die Zieltemperatur konnte nicht gespeichert "
                            "werden.");
      return;
    }

    PS5TM_INFO("target_temp_changed", "Zieltemperatur auf %d °C gesetzt.",
               clamped);

    char body[224];
    snprintf(body, sizeof(body),
             "{\"ok\":true,\"threshold_c\":%d,\"target_temp_c\":%d,"
             "\"message\":\"Zieltemperatur %d °C (Bereich %d–%d °C). Der "
             "Lüfter wird sanft nachgeführt.\"}",
             clamped, clamped, clamped,
             PS5TM_TARGET_MIN_C, PS5TM_TARGET_MAX_C);
    ps5tm_http_send_json(fd, 200, body);
    return;
  }

  /* Observe mode: the value really is a raw threshold, pinned as given. */
  int clamped = requested;
  if(clamped < PS5TM_THRESHOLD_MIN_C) clamped = PS5TM_THRESHOLD_MIN_C;
  if(clamped > PS5TM_THRESHOLD_MAX_C) clamped = PS5TM_THRESHOLD_MAX_C;

  int eno = 0;
  if(ps5tm_fan_apply_manual(clamped, &eno) != 0) {
    ps5tm_http_send_error(fd, 500, "icc_fan_ioctl_failed",
                          ps5tm_platform_fan_message());
    return;
  }

  ps5tm_config_lock();
  g_config.fan_threshold_c = (unsigned)clamped;
  ps5tm_config_unlock();
  int saved = (ps5tm_config_save() == 0);

  if(!saved) {
    ps5tm_http_send_error(fd, 500, "threshold_save_failed",
                          "Die Schwelle wurde gesetzt, konnte aber nicht "
                          "dauerhaft gespeichert werden.");
    return;
  }

  char body[192];
  snprintf(body, sizeof(body),
           "{\"ok\":true,\"threshold_c\":%d,"
           "\"message\":\"Feste Schwelle %d °C (Bereich %d–%d °C).\"}",
           clamped, clamped, PS5TM_THRESHOLD_MIN_C, PS5TM_THRESHOLD_MAX_C);
  ps5tm_http_send_json(fd, 200, body);
}


/* --------------------------------------------------------------------- tile */

/* Only queues the job.
 *
 * Installing takes the ShellCore identity, loads a module and waits on a
 * service that is free to take fifteen seconds — none of which may happen on
 * a request thread. Doing so once wedged the entire server. The background
 * thread picks this up within a second and writes every step to the log. */
static void
handle_tile_ensure(int fd) {
  ps5tm_tile_request_install();
  ps5tm_http_send_json(fd, 202,
      "{\"ok\":true,\"state\":\"installing\",\"message\":"
      "\"Installation angestoßen. Der Verlauf steht im Protokoll.\"}");
}


/* --------------------------------------------------------------------- logs */

#define LOG_PAGE 150

static void
handle_logs(int fd) {
  ps5tm_log_entry_t *entries = malloc(sizeof(*entries) * LOG_PAGE);
  char              *body    = malloc(96 * 1024);
  if(!entries || !body) {
    free(entries); free(body);
    ps5tm_http_send_error(fd, 500, "alloc_failed", "Kein Speicher.");
    return;
  }

  unsigned n = ps5tm_log_snapshot(entries, LOG_PAGE);
  size_t   o = (size_t)snprintf(body, 96 * 1024, "{\"ok\":true,\"entries\":[");

  for(unsigned i = 0; i < n; i++) {
    char esc_msg[512], esc_code[128];
    json_escape(entries[i].message, esc_msg,  sizeof(esc_msg));
    json_escape(entries[i].code,    esc_code, sizeof(esc_code));
    o += (size_t)snprintf(body + o, 96 * 1024 - o,
        "%s{\"timestamp_ms\":%llu,\"level\":\"%s\",\"code\":\"%s\","
        "\"message\":\"%s\"}",
        i ? "," : "", (unsigned long long)entries[i].timestamp_ms,
        entries[i].level, esc_code, esc_msg);
    if(o > 96 * 1024 - 1024) break;
  }
  snprintf(body + o, 96 * 1024 - o, "]}");

  ps5tm_http_send_json(fd, 200, body);
  free(entries);
  free(body);
}


static void
handle_logs_export(int fd) {
  ps5tm_log_entry_t *entries = malloc(sizeof(*entries) * PS5TM_LOG_CAPACITY);
  char              *body    = malloc(96 * 1024);
  if(!entries || !body) {
    free(entries); free(body);
    ps5tm_http_send_error(fd, 500, "alloc_failed", "Kein Speicher.");
    return;
  }

  unsigned n = ps5tm_log_snapshot(entries, PS5TM_LOG_CAPACITY);
  size_t   o = 0;
  for(unsigned i = 0; i < n && o < 96 * 1024 - 512; i++) {
    time_t    secs = (time_t)(entries[i].timestamp_ms / 1000);
    struct tm tmv;
    char      stamp[32] = "0000-00-00T00:00:00";
    if(gmtime_r(&secs, &tmv)) strftime(stamp, sizeof(stamp),
                                       "%Y-%m-%dT%H:%M:%S", &tmv);
    o += (size_t)snprintf(body + o, 96 * 1024 - o, "%s  %-5s  %-28s  %s\n",
                          stamp, entries[i].level, entries[i].code,
                          entries[i].message);
  }

  ps5tm_http_send(fd, 200, "OK", "text/plain; charset=utf-8", body, o,
                  "Content-Disposition: attachment; "
                  "filename=ps5-temperature-manager.log\r\n");
  free(entries);
  free(body);
}

/* The kernel log, live (klog.c): the lines newer than `after`, at most `max`
   (default 500, up to 1000). The page asks again with the number of the last
   line it got. A line is {n, t, s} — number, wall clock in ms, text — and a
   note of ours, such as the one that says lines were lost, also carries m:1. */
#define KLOG_BODY_MAX (1024 * 1024)
#define KLOG_HDR_ROOM 192            /* in front of the lines: the head is written last */

static void
handle_klog(int fd, const ps5tm_request_t *req) {
  char     val[24];
  uint64_t after = 0;
  unsigned want  = 500;

  query_param(req->query, "after", val, sizeof(val));
  if(val[0]) after = strtoull(val, NULL, 10);
  query_param(req->query, "max", val, sizeof(val));
  if(val[0]) {
    unsigned long v = strtoul(val, NULL, 10);
    if(v >= 1 && v <= 1000) want = (unsigned)v;
  }

  ps5tm_klog_line_t *lines = malloc(sizeof(*lines) * want);
  char              *body  = malloc(KLOG_BODY_MAX);
  if(!lines || !body) {
    free(lines); free(body);
    ps5tm_http_send_error(fd, 500, "alloc_failed", "Kein Speicher.");
    return;
  }

  uint64_t newest = 0;
  int      more = 0, lost = 0;
  unsigned n = ps5tm_klog_fetch(after, want, lines, &newest, &more, &lost);

  /* The lines first, from KLOG_HDR_ROOM on: whether lines are left over is only
     known once they are written (the answer may be cut short by its size). The
     head then goes right in front of them. */
  char  *arr = body + KLOG_HDR_ROOM;
  size_t cap = KLOG_BODY_MAX - KLOG_HDR_ROOM;
  size_t o   = 0;
  char   esc[PS5TM_KLOG_LINE * 6 + 8];
  for(unsigned i = 0; i < n; i++) {
    json_escape(lines[i].text, esc, sizeof(esc));
    if(o + strlen(esc) + 128 > cap) { more = 1; break; }
    o += (size_t)snprintf(arr + o, cap - o,
                          "%s{\"n\":%llu,\"t\":%llu,%s\"s\":\"%s\"}",
                          i ? "," : "", (unsigned long long)lines[i].seq,
                          (unsigned long long)lines[i].t_ms,
                          lines[i].marker ? "\"m\":1," : "", esc);
  }
  snprintf(arr + o, cap - o, "]}");

  char head[KLOG_HDR_ROOM];
  int  hl = snprintf(head, sizeof(head),
                     "{\"ok\":true,\"newest\":%llu,\"lost\":%s,\"more\":%s,"
                     "\"lines\":[", (unsigned long long)newest,
                     lost ? "true" : "false", more ? "true" : "false");
  if(hl < 0 || hl >= KLOG_HDR_ROOM) {
    free(lines); free(body);
    ps5tm_http_send_error(fd, 500, "klog_head", "Antwort zu lang.");
    return;
  }
  memcpy(arr - hl, head, (size_t)hl);

  ps5tm_http_send_json(fd, 200, arr - hl);
  free(lines);
  free(body);
}


/* Play time (playtime.c): the saved sessions, newest first, the one in progress
   and the name of each title. `max` limits the sessions; the default is every
   one the file keeps, which is what the page needs for its totals. */
static void
handle_playtime(int fd, const ps5tm_request_t *req) {
  char     val[16];
  unsigned want = 3500;

  query_param(req->query, "max", val, sizeof(val));
  if(val[0]) {
    unsigned long v = strtoul(val, NULL, 10);
    if(v >= 1 && v <= 3500) want = (unsigned)v;
  }

  cJSON *root = ps5tm_playtime_json(want);
  char  *txt  = root ? cJSON_PrintUnformatted(root) : NULL;
  cJSON_Delete(root);
  if(!txt) {
    ps5tm_http_send_error(fd, 500, "alloc_failed", "Kein Speicher.");
    return;
  }
  ps5tm_http_send_json(fd, 200, txt);
  free(txt);
}


/* Saved games (savebackup.c). The page asks for the whole picture when it opens
   and then only for the job; every start answers with the job. */
static void
saves_started(int fd, int status, const char *code, const char *err) {
  if(status != 200) {
    ps5tm_http_send_error(fd, status, code, err);
    return;
  }
  send_cjson(fd, 200, ps5tm_saves_job_json());
}

static void
handle_saves_backup(int fd, const ps5tm_request_t *req) {
  cJSON *body = req->body ? cJSON_Parse(req->body) : NULL;
  const cJSON *jt = body ? cJSON_GetObjectItem(body, "target") : NULL;
  char target[40] = "";
  if(cJSON_IsString(jt)) snprintf(target, sizeof(target), "%s", jt->valuestring);

  /* users and titles: strings, a handful of them; anything else is not a name */
  const char *users[16], *titles[400];
  unsigned nu = 0, nt = 0;
  const cJSON *ju = body ? cJSON_GetObjectItem(body, "users") : NULL;
  const cJSON *jl = body ? cJSON_GetObjectItem(body, "titles") : NULL;
  const cJSON *x;
  int bad_list = (ju && !cJSON_IsArray(ju)) || (jl && !cJSON_IsArray(jl));
  cJSON_ArrayForEach(x, ju) {
    if(!cJSON_IsString(x) || nu >= 16) { bad_list = 1; break; }
    users[nu++] = x->valuestring;
  }
  cJSON_ArrayForEach(x, jl) {
    if(!cJSON_IsString(x) || nt >= 400) { bad_list = 1; break; }
    titles[nt++] = x->valuestring;
  }
  if(bad_list) {
    /* a list that is silently cut or has other things in it would turn into "everything" */
    cJSON_Delete(body);
    ps5tm_http_send_error(fd, 400, "bad_request", "Benutzer und Titel müssen Listen aus Texten sein (höchstens 16 Benutzer, 400 Titel).");
    return;
  }

  char err[640] = "";
  int status = ps5tm_saves_backup_start(target, users, nu, titles, nt, err, sizeof(err));
  cJSON_Delete(body);
  saves_started(fd, status, "saves_refused", err);
}

/* ------------------------------------------------------------ packages (pkgscan.c, pkgparse.c, pkgsplit.c) */

/* The icon of a package the last search listed. The id is a hash of the path, so a page can only ask for what the
   list handed out; no path travels in the request. */
static void
handle_packages_icon(int fd, const ps5tm_request_t *req) {
  char id[24];
  query_param(req->query, "id", id, sizeof(id));
  ps5tm_pkg_t p;
  uint8_t *data = NULL;
  size_t n = 0;
  if(ps5tm_pkgscan_find(id, &p) != 0 || ps5tm_pkg_icon(&p, &data, &n) != 0) {
    ps5tm_http_send_error(fd, 404, "no_icon", "Für dieses Paket liegt kein Bild vor.");
    return;
  }
  ps5tm_http_send_cached(fd, "image/png", data, n, 300);
  free(data);
}

static void
handle_packages_split(int fd, const ps5tm_request_t *req) {
  cJSON *body = req->body ? cJSON_Parse(req->body) : NULL;
  const cJSON *ji = body ? cJSON_GetObjectItem(body, "id") : NULL;
  const cJSON *jt = body ? cJSON_GetObjectItem(body, "target") : NULL;
  const cJSON *jm = body ? cJSON_GetObjectItem(body, "part_mb") : NULL;
  char id[24] = "", target[40] = "", err[320] = "";
  if(cJSON_IsString(ji)) snprintf(id, sizeof(id), "%s", ji->valuestring);
  if(cJSON_IsString(jt)) snprintf(target, sizeof(target), "%s", jt->valuestring);
  double mb = cJSON_IsNumber(jm) ? jm->valuedouble : 0;
  cJSON_Delete(body);
  if(!(mb >= 1 && mb <= 1048576)) {
    ps5tm_http_send_error(fd, 400, "bad_request", "Die Größe eines Teils (part_mb, in MB) fehlt oder ist unzulässig.");
    return;
  }
  int status = ps5tm_pkgsplit_start(id, target, (uint64_t)mb << 20, err, sizeof(err));
  if(status != 200) {
    ps5tm_http_send_error(fd, status, "packages_refused", err);
    return;
  }
  send_cjson(fd, 200, ps5tm_pkgsplit_job_json());
}

/* Installing a package the last search listed (pkginstall.c). What would happen first (the plan), then the start, the
   state, the stop. The id is the one the list handed out, so no path travels in the request. */
static void
handle_packages_install_plan(int fd, const ps5tm_request_t *req) {
  char id[24];
  query_param(req->query, "id", id, sizeof(id));
  int http = 200;
  cJSON *j = ps5tm_pkginst_plan_json(id, &http);
  if(!j) { ps5tm_http_send_error(fd, 503, "no_memory", "Zu wenig Speicher."); return; }
  if(http == 404) {
    const cJSON *m = cJSON_GetObjectItem(j, "blocked");
    ps5tm_http_send_error(fd, 404, "packages_unknown", cJSON_IsString(m) ? m->valuestring : "Dieses Paket gibt es nicht.");
    cJSON_Delete(j);
    return;
  }
  send_cjson(fd, 200, j);
}

static void
handle_packages_install(int fd, const ps5tm_request_t *req) {
  cJSON *body = req->body ? cJSON_Parse(req->body) : NULL;
  const cJSON *ji = body ? cJSON_GetObjectItem(body, "id") : NULL;
  char id[24] = "", err[600] = "";
  if(cJSON_IsString(ji)) snprintf(id, sizeof(id), "%s", ji->valuestring);
  cJSON_Delete(body);
  int status = ps5tm_pkginst_start(id, err, sizeof(err));
  if(status != 200) {
    ps5tm_http_send_error(fd, status, "install_refused", err);
    return;
  }
  send_cjson(fd, 200, ps5tm_pkginst_job_json());
}

static void
handle_saves_delete(int fd, const ps5tm_request_t *req) {
  cJSON *root = req->body ? cJSON_Parse(req->body) : NULL;
  const cJSON *u = root ? cJSON_GetObjectItem(root, "uid") : NULL, *i = root ? cJSON_GetObjectItem(root, "id") : NULL,
              *m = root ? cJSON_GetObjectItem(root, "mount") : NULL;
  char err[640] = "";
  int st = ps5tm_saves_delete_start(cJSON_IsString(u) ? u->valuestring : NULL, cJSON_IsString(i) ? i->valuestring : NULL,
                                    cJSON_IsString(m) ? m->valuestring : "", err, sizeof(err));
  cJSON_Delete(root);
  saves_started(fd, st, "saves_refused", err);
}

static void
handle_saves_verify(int fd, const ps5tm_request_t *req) {
  cJSON *body = req->body ? cJSON_Parse(req->body) : NULL;
  const cJSON *jp = body ? cJSON_GetObjectItem(body, "path") : NULL;
  char path[320] = "", err[256] = "";
  if(cJSON_IsString(jp)) snprintf(path, sizeof(path), "%s", jp->valuestring);
  cJSON_Delete(body);
  saves_started(fd, ps5tm_saves_verify_start(path, err, sizeof(err)), "saves_refused", err);
}

/* "confirm": true is part of the request, so that nothing that merely finds this
   address can put a save back; the page sends it after its second click. */
static void
handle_saves_restore(int fd, const ps5tm_request_t *req) {
  cJSON *body = req->body ? cJSON_Parse(req->body) : NULL;
  const cJSON *jp = body ? cJSON_GetObjectItem(body, "path")  : NULL;
  const cJSON *ju = body ? cJSON_GetObjectItem(body, "user")  : NULL;
  const cJSON *jt = body ? cJSON_GetObjectItem(body, "title") : NULL;
  int confirmed = body && cJSON_IsTrue(cJSON_GetObjectItem(body, "confirm"));
  char path[320] = "", uid[16] = "", title[16] = "", err[256] = "";
  if(cJSON_IsString(jp)) snprintf(path, sizeof(path), "%s", jp->valuestring);
  if(cJSON_IsString(ju)) snprintf(uid, sizeof(uid), "%s", ju->valuestring);
  if(cJSON_IsString(jt)) snprintf(title, sizeof(title), "%s", jt->valuestring);
  cJSON_Delete(body);
  if(!confirmed) {
    ps5tm_http_send_error(fd, 400, "confirm_missing",
                          "Zurückspielen braucht die Bestätigung (\"confirm\": true).");
    return;
  }
  saves_started(fd, ps5tm_saves_restore_start(path, uid, title, err, sizeof(err)),
                "saves_refused", err);
}

static void
handle_logs_tail(int fd, const ps5tm_request_t *req) {
  unsigned want = 40;
  if(req->query[0]) {
    const char *p = strstr(req->query, "count=");
    if(p) {
      unsigned v = (unsigned)strtoul(p + 6, NULL, 10);
      if(v >= 1 && v <= LOG_PAGE) want = v;
    }
  }

  ps5tm_log_entry_t *entries = malloc(sizeof(*entries) * LOG_PAGE);
  char              *body    = malloc(96 * 1024);
  if(!entries || !body) {
    free(entries); free(body);
    ps5tm_http_send_error(fd, 500, "alloc_failed", "Kein Speicher.");
    return;
  }

  unsigned n = ps5tm_log_snapshot(entries, LOG_PAGE);
  unsigned start = (n > want) ? (n - want) : 0;

  size_t o = (size_t)snprintf(body, 96 * 1024,
                              "{\"ok\":true,\"count\":%u,\"entries\":[",
                              n - start);
  for(unsigned i = start; i < n; i++) {
    char esc_msg[512], esc_code[128];
    json_escape(entries[i].message, esc_msg,  sizeof(esc_msg));
    json_escape(entries[i].code,    esc_code, sizeof(esc_code));
    o += (size_t)snprintf(body + o, 96 * 1024 - o,
        "%s{\"timestamp_ms\":%llu,\"level\":\"%s\",\"code\":\"%s\","
        "\"message\":\"%s\"}",
        i == start ? "" : ",", (unsigned long long)entries[i].timestamp_ms,
        entries[i].level, esc_code, esc_msg);
    if(o > 96 * 1024 - 1024) break;
  }
  snprintf(body + o, 96 * 1024 - o, "]}");
  ps5tm_http_send_json(fd, 200, body);
  free(entries);
  free(body);
}


/* -------------------------------------------------------------- tile upload */

/* Takes the package straight from the browser onto the console.
 *
 * The separate installer payload never produced a single line of output on the
 * test console — not even a notification issued as its first action — so there
 * was no way to tell whether it ran at all. This path runs inside the app,
 * which demonstrably works and whose log can be read remotely, so every step
 * is visible instead of guessed at.
 *
 * The body is written in small pieces as it arrives; ten megabytes never sit
 * in memory at once.
 */

/* Both uploads write to a fixed temporary name, so two at once would end up
   interleaved in one file and still report success. One at a time; the page
   never sends two, so a second caller is a stray tab or a second tool. */
static pthread_mutex_t g_upload_lock = PTHREAD_MUTEX_INITIALIZER;

/* Whole-transfer ceilings. The socket's receive timeout only catches a sender
   that goes quiet; one that trickles a byte every few seconds would hold the
   thread, the lock and a half-written file indefinitely. */
#define TILE_UPLOAD_BUDGET_MS   (10u * 60 * 1000)
#define AVATAR_UPLOAD_BUDGET_MS (2u * 60 * 1000)

/* Puts the body on disk: what already arrived together with the headers, then
   the rest from the socket. Returns how many bytes made it. */
static size_t
receive_body(int fd, FILE *f, const char *prefix, size_t prefix_len,
             size_t total, uint64_t budget_ms) {
  size_t written = 0;
  if(prefix_len) {
    size_t n = prefix_len < total ? prefix_len : total;
    written = fwrite(prefix, 1, n, f);
  }

  uint64_t deadline = ps5tm_mono_ms() + budget_ms;
  char     buf[16384];
  while(written < total && ps5tm_mono_ms() < deadline) {
    size_t chunk = total - written;
    if(chunk > sizeof(buf)) chunk = sizeof(buf);
    ssize_t n = read(fd, buf, chunk);
    if(n < 0 && errno == EINTR) continue;
    if(n <= 0) break;
    if(fwrite(buf, 1, (size_t)n, f) != (size_t)n) break;
    written += (size_t)n;
  }
  return written;
}

/* A full disk shows up in the stream's error flag and in fclose(), not
   necessarily in fflush(). */
static int
close_file_ok(FILE *f) {
  int ok = (fflush(f) == 0 && !ferror(f));
  if(fclose(f) != 0) ok = 0;
  return ok;
}

static void
receive_tile_package(int fd, const char *prefix, size_t prefix_len,
                     size_t total) {
  if(total == 0 || total > 64u * 1024 * 1024) {
    ps5tm_http_send_error(fd, 400, "bad_upload",
                          "Ungültige Paketgröße.");
    return;
  }

  mkdir(PS5TM_DATA_DIR, 0755);

  const char *tmp = PS5TM_DATA_DIR "/tile.pkg.part";
  FILE *f = fopen(tmp, "wb");
  if(!f) {
    PS5TM_ERROR("upload_open_failed", "%s ist nicht beschreibbar.", tmp);
    ps5tm_http_send_error(fd, 500, "write_failed",
                          "Datei konnte nicht angelegt werden.");
    return;
  }

  size_t written = receive_body(fd, f, prefix, prefix_len, total,
                                TILE_UPLOAD_BUDGET_MS);
  int    flushed = close_file_ok(f);

  if(written != total || !flushed) {
    unlink(tmp);
    PS5TM_ERROR("upload_incomplete",
                "Nur %zu von %zu Bytes empfangen.", written, total);
    ps5tm_http_send_error(fd, 500, "upload_incomplete",
                          "Die Übertragung wurde abgebrochen.");
    return;
  }

  unlink(PS5TM_DATA_DIR "/tile.pkg");
  if(rename(tmp, PS5TM_DATA_DIR "/tile.pkg") != 0) {
    unlink(tmp);
    ps5tm_http_send_error(fd, 500, "rename_failed",
                          "Paket konnte nicht abgelegt werden.");
    return;
  }

  PS5TM_INFO("upload_ok",
             "Kachel-Paket empfangen (%zu KiB) und unter %s/tile.pkg "
             "abgelegt.", total / 1024, PS5TM_DATA_DIR);

  char body[224];
  snprintf(body, sizeof(body),
           "{\"ok\":true,\"bytes\":%zu,\"message\":\"Paket empfangen. "
           "Jetzt „Kachel installieren“ drücken.\"}", total);
  ps5tm_http_send_json(fd, 200, body);
}

void
ps5tm_api_receive_upload(int fd, const char *prefix, size_t prefix_len,
                         size_t total) {
  if(pthread_mutex_trylock(&g_upload_lock) != 0) {
    ps5tm_http_send_error(fd, 409, "upload_busy",
                          "Es läuft bereits eine Übertragung.");
    return;
  }
  receive_tile_package(fd, prefix, prefix_len, total);
  pthread_mutex_unlock(&g_upload_lock);
}


/* ------------------------------------------------------------ avatar upload */

/* The browser generates eleven files — four texture sizes, each written twice
 * as avatar<N>.dds and picture<N>.dds, the squared source as PNG, and
 * online.json — and sends them here one at a time before asking for them to
 * be applied.
 *
 * Staging first and applying second is not ceremony. The profile cache is
 * live: a set that is only half uploaded when the connection drops would
 * leave the console with a broken picture. Nothing goes near the cache until
 * every file has arrived and the browser says so.
 */

/* A whitelist, not a filter.
 *
 * This writes a file under a name the browser chose, and the only safe way to
 * do that is to accept nothing except the exact names the pipeline produces.
 * No separators to escape, no traversal to reason about, no argument. */
static int
avatar_name_ok(const char *name) {
  static const char *allowed[] = {
    "avatar64.dds",  "avatar128.dds",  "avatar260.dds",  "avatar440.dds",
    "picture64.dds", "picture128.dds", "picture260.dds", "picture440.dds",
    "avatar.png", "picture.png", "online.json",
  };
  for(unsigned i = 0; i < sizeof(allowed) / sizeof(allowed[0]); i++)
    if(!strcmp(name, allowed[i])) return 1;
  return 0;
}

/* Pulls name=<value> out of the query string. Matches only a whole parameter,
   so a hypothetical "filename=" cannot be mistaken for it. */
static int
query_name(const char *query, char *out, size_t out_size) {
  out[0] = 0;
  if(!query) return -1;

  for(const char *p = query; p && *p; ) {
    if(!strncmp(p, "name=", 5)) {
      const char *v = p + 5;
      size_t i = 0;
      while(v[i] && v[i] != '&' && i + 1 < out_size) { out[i] = v[i]; i++; }
      out[i] = 0;
      return out[0] ? 0 : -1;
    }
    p = strchr(p, '&');
    if(p) p++;
  }
  return -1;
}

static void
receive_avatar_file(int fd, const char *query, const char *prefix,
                    size_t prefix_len, size_t total) {
  char name[64];
  if(query_name(query, name, sizeof(name)) != 0 || !avatar_name_ok(name)) {
    ps5tm_http_send_error(fd, 400, "bad_name",
                          "Unbekannter Dateiname.");
    return;
  }

  /* 440×440 as DXT5 is 189 KiB; the PNG is capped well below this. Anything
     larger is not something this pipeline produced. */
  if(total == 0 || total > 4u * 1024 * 1024) {
    ps5tm_http_send_error(fd, 400, "bad_upload", "Ungültige Dateigröße.");
    return;
  }

  mkdir(PS5TM_DATA_DIR, 0755);
  mkdir(ps5tm_avatar_stage_dir(), 0755);

  char path[256], tmp[280];
  snprintf(path, sizeof(path), "%s/%s", ps5tm_avatar_stage_dir(), name);
  snprintf(tmp,  sizeof(tmp),  "%s.part", path);

  FILE *f = fopen(tmp, "wb");
  if(!f) {
    ps5tm_http_send_error(fd, 500, "write_failed",
                          "Datei konnte nicht angelegt werden.");
    return;
  }

  size_t written = receive_body(fd, f, prefix, prefix_len, total,
                                AVATAR_UPLOAD_BUDGET_MS);
  int    flushed = close_file_ok(f);

  if(written != total || !flushed) {
    unlink(tmp);
    ps5tm_http_send_error(fd, 500, "upload_incomplete",
                          "Die Übertragung wurde abgebrochen.");
    return;
  }

  unlink(path);
  if(rename(tmp, path) != 0) {
    unlink(tmp);
    ps5tm_http_send_error(fd, 500, "rename_failed",
                          "Datei konnte nicht abgelegt werden.");
    return;
  }

  /* Protokolliert, weil dies der einzige Beleg dafür ist, wie weit ein
     Übernehmen gekommen ist — der Browser sieht seine eigenen Fehler, die
     Konsole nicht. */
  PS5TM_INFO("avatar_file", "Bilddatei empfangen: %s (%zu Bytes).",
             name, total);

  char body[128];
  snprintf(body, sizeof(body), "{\"ok\":true,\"bytes\":%zu}", total);
  ps5tm_http_send_json(fd, 200, body);
}

void
ps5tm_api_receive_avatar_file(int fd, const char *query, const char *prefix,
                              size_t prefix_len, size_t total) {
  if(pthread_mutex_trylock(&g_upload_lock) != 0) {
    ps5tm_http_send_error(fd, 409, "upload_busy",
                          "Es läuft bereits eine Übertragung.");
    return;
  }
  receive_avatar_file(fd, query, prefix, prefix_len, total);
  pthread_mutex_unlock(&g_upload_lock);
}


/* --------------------------------------------- kernel-log files (klogfiles.c) */

/* The page's own time zone, minutes east of UTC, as tz=<n>; no tz is UTC. */
static int
query_tz(const char *query) {
  for(const char *p = query; p && *p; ) {
    if(!strncmp(p, "tz=", 3)) {
      char *end = NULL;
      long  v = strtol(p + 3, &end, 10);
      if(end == p + 3) return 0;
      return v < -840 ? -840 : v > 840 ? 840 : (int)v;
    }
    p = strchr(p, '&');
    if(p) p++;
  }
  return 0;
}

#define KLOG_SAVE_MAX           (8u * 1024 * 1024)
#define KLOG_UPLOAD_BUDGET_MS   (2u * 60 * 1000)

static void
receive_klog_save(int fd, const char *query, const char *prefix, size_t prefix_len, size_t total) {
  if(total == 0 || total > KLOG_SAVE_MAX) {
    ps5tm_http_send_error(fd, 400, "bad_upload", "Ungültige Größe (höchstens 8 MB).");
    return;
  }
  char name[64], path[320];
  int  ofd = ps5tm_klogfiles_create("klog", query_tz(query), name, sizeof(name));
  if(ofd < 0) {
    char msg[160];
    snprintf(msg, sizeof(msg), "Die Datei ließ sich nicht anlegen: %s", strerror(-ofd));
    ps5tm_http_send_error(fd, 500, "write_failed", msg);
    return;
  }
  if(ps5tm_klogfiles_path(name, path, sizeof(path)) != 0) path[0] = 0;
  FILE *f = fdopen(ofd, "wb");
  if(!f) {
    close(ofd);
    if(path[0]) unlink(path);
    ps5tm_http_send_error(fd, 500, "write_failed", "Die Datei ließ sich nicht öffnen.");
    return;
  }
  size_t written = receive_body(fd, f, prefix, prefix_len, total, KLOG_UPLOAD_BUDGET_MS);
  int    flushed = close_file_ok(f);
  if(written != total || !flushed) {
    if(path[0]) unlink(path);                  /* no half file */
    ps5tm_http_send_error(fd, 500, "upload_incomplete", "Die Übertragung wurde abgebrochen.");
    return;
  }
  PS5TM_INFO("klog_saved", "Kernel-Log gespeichert: %s (%zu KB).", name, total / 1024);
  cJSON *o = cJSON_CreateObject();
  cJSON_AddBoolToObject(o, "ok", 1);
  cJSON_AddStringToObject(o, "name", name);
  cJSON_AddNumberToObject(o, "bytes", (double)total);
  send_cjson(fd, 200, o);
}

void
ps5tm_api_receive_klog_save(int fd, const char *query, const char *prefix, size_t prefix_len, size_t total) {
  if(pthread_mutex_trylock(&g_upload_lock) != 0) {
    ps5tm_http_send_error(fd, 409, "upload_busy", "Es läuft bereits eine Übertragung.");
    return;
  }
  receive_klog_save(fd, query, prefix, prefix_len, total);
  pthread_mutex_unlock(&g_upload_lock);
}

/* One of our log files as a download. Only names klogfiles.c itself makes. */
static void
handle_klog_file(int fd, const ps5tm_request_t *req) {
  char name[80], path[320];
  if(query_name(req->query, name, sizeof(name)) != 0 || ps5tm_klogfiles_path(name, path, sizeof(path)) != 0 ||
     ps5tm_http_send_download(fd, path, "text/plain; charset=utf-8", name, 128ull << 20) != 0)
    ps5tm_http_send_error(fd, 404, "not_found", "Diese Datei gibt es nicht (mehr).");
}

static void
handle_klog_files_clear(int fd) {
  unsigned deleted = 0, kept = 0;
  uint64_t bytes = 0;
  int rc = ps5tm_klogfiles_clear(&deleted, &bytes, &kept);
  if(rc) {
    char msg[160];
    snprintf(msg, sizeof(msg), "Der Ordner ließ sich nicht lesen: %s", strerror(rc));
    ps5tm_http_send_error(fd, 500, "clear_failed", msg);
    return;
  }
  cJSON *o = cJSON_CreateObject();
  cJSON_AddBoolToObject(o, "ok", 1);
  cJSON_AddNumberToObject(o, "deleted", deleted);
  cJSON_AddNumberToObject(o, "bytes", (double)bytes);
  cJSON_AddNumberToObject(o, "kept", kept);
  send_cjson(fd, 200, o);
}

/* {"on": true, "tz": <minutes east of UTC>} starts a recording, {"on": false} ends it. */
static void
handle_klog_record_post(int fd, const ps5tm_request_t *req) {
  cJSON *body = req->body ? cJSON_Parse(req->body) : NULL;
  const cJSON *jon = body ? cJSON_GetObjectItem(body, "on") : NULL;
  const cJSON *jtz = body ? cJSON_GetObjectItem(body, "tz") : NULL;
  int on = cJSON_IsTrue(jon);
  int off = cJSON_IsFalse(jon);
  int tz = cJSON_IsNumber(jtz) ? (int)jtz->valuedouble : 0;
  cJSON_Delete(body);
  if(!on && !off) {
    ps5tm_http_send_error(fd, 400, "bad_request", "Es fehlt \"on\": true oder false.");
    return;
  }
  if(on) {
    char err[200] = "";
    int  st = ps5tm_klogrec_start(tz, err, sizeof(err));
    if(st != 200) { ps5tm_http_send_error(fd, st, "klog_record_refused", err); return; }
  } else {
    ps5tm_klogrec_stop();
  }
  cJSON *o = ps5tm_klogrec_json();
  if(o) cJSON_AddBoolToObject(o, "ok", 1);
  send_cjson(fd, 200, o);
}

/* Applying, and undoing it. Both resolve the user themselves for the same
   reason the rename does: the browser must not get to name the account. */
/* The picture the console currently shows for the signed-in user, so the
 * profile page can greet them with it. Read-only: it is the square PNG the
 * shell keeps beside the DDS set in the profile cache (the same directory
 * ps5tm_avatar_apply() writes to). A user without a custom picture has no
 * such file, which is normal — 404, and the page falls back to an initial. */
static void
handle_avatar_current(int fd) {
  ps5tm_user_t prof;
  ps5tm_user_get(&prof);
  if(!prof.valid) {
    ps5tm_http_send_error(fd, 409, "no_user",
                          "Es konnte kein angemeldeter Benutzer ermittelt "
                          "werden.");
    return;
  }

  static const char *const names[] = { "avatar.png", "picture.png" };
  for(size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
    char path[160];
    snprintf(path, sizeof(path), "/system_data/priv/cache/profile/0x%08X/%s",
             prof.uid, names[i]);
    FILE *f = fopen(path, "rb");
    if(!f) continue;

    struct stat st;
    if(fstat(fileno(f), &st) != 0 || st.st_size <= 0
       || st.st_size > 4 * 1024 * 1024) {
      fclose(f);
      continue;
    }
    unsigned char *buf = malloc((size_t)st.st_size);
    if(!buf) { fclose(f); break; }
    size_t n = fread(buf, 1, (size_t)st.st_size, f);
    fclose(f);
    if(n == (size_t)st.st_size) {
      ps5tm_http_send(fd, 200, "OK", "image/png", buf, n, NULL);
      free(buf);
      return;
    }
    free(buf);
  }

  ps5tm_http_send_error(fd, 404, "no_avatar",
                        "Für diesen Benutzer liegt kein Profilbild vor.");
}


/* The picture of one of the users whose saved games the page lists: ?uid=<8 hex digits>. Read-only, the same file as
   above; a user without a custom picture has none (404, the page shows an initial). */
static void
handle_saves_avatar(int fd, const char *query) {
  char uid[16];
  query_param(query, "uid", uid, sizeof(uid));
  if(strlen(uid) != 8 || strspn(uid, "0123456789abcdefABCDEF") != 8) {
    ps5tm_http_send_error(fd, 400, "bad_uid", "Ungültige Benutzerkennung.");
    return;
  }
  for(int i = 0; i < 8; i++) uid[i] = (char)(uid[i] >= 'a' && uid[i] <= 'f' ? uid[i] - 32 : uid[i]);
  static const char *const names[] = { "avatar.png", "picture.png" };
  for(size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
    char path[160];
    snprintf(path, sizeof(path), "/system_data/priv/cache/profile/0x%s/%s", uid, names[i]);
    FILE *f = fopen(path, "rb");
    if(!f) continue;
    struct stat st;
    if(fstat(fileno(f), &st) != 0 || st.st_size <= 0 || st.st_size > 4 * 1024 * 1024) { fclose(f); continue; }
    unsigned char *buf = malloc((size_t)st.st_size);
    if(!buf) { fclose(f); break; }
    size_t n = fread(buf, 1, (size_t)st.st_size, f);
    fclose(f);
    if(n == (size_t)st.st_size) {
      ps5tm_http_send(fd, 200, "OK", "image/png", buf, n, NULL);
      free(buf);
      return;
    }
    free(buf);
  }
  ps5tm_http_send_error(fd, 404, "no_avatar", "Für diesen Benutzer liegt kein Profilbild vor.");
}


static void
handle_avatar_commit(int fd, int restore) {
  ps5tm_user_t prof;
  ps5tm_user_get(&prof);
  if(!prof.valid) {
    ps5tm_http_send_error(fd, 409, "no_user",
                          "Es konnte kein angemeldeter Benutzer ermittelt "
                          "werden.");
    return;
  }

  int         copied = 0;
  const char *why    = NULL;
  int rc = restore ? ps5tm_avatar_restore(prof.uid, &copied, &why)
                   : ps5tm_avatar_apply  (prof.uid, &copied, &why);

  if(rc != 0) {
    ps5tm_http_send_error(fd, 400,
                          restore ? "restore_failed" : "apply_failed",
                          why ? why : "Vorgang fehlgeschlagen.");
    return;
  }

  char uid_hex[16];
  snprintf(uid_hex, sizeof(uid_hex), "0x%08X", prof.uid);

  cJSON *out = cJSON_CreateObject();
  cJSON_AddBoolToObject  (out, "ok", 1);
  cJSON_AddNumberToObject(out, "copied", copied);
  cJSON_AddStringToObject(out, "uid_hex", uid_hex);
  char *txt = cJSON_PrintUnformatted(out);
  cJSON_Delete(out);
  if(!txt) {
    ps5tm_http_send_error(fd, 500, "alloc_failed", "Kein Speicher.");
    return;
  }
  ps5tm_http_send(fd, 200, "OK", "application/json", txt, strlen(txt), NULL);
  free(txt);
}

/* ------------------------------------------------------ avatar library */

#define AVATAR_LIBRARY_DIR PS5TM_DATA_DIR "/Avatars"

static int
avatar_pack_name_ok(const char *name) {
  if(!name || !name[0]) return 0;
  size_t n = strlen(name);
  if(n < 1 || n > 48) return 0;
  if(name[0] == '.' || name[n - 1] == '.' || name[0] == ' ' || name[n - 1] == ' ')
    return 0;
  for(size_t i = 0; i < n; i++) {
    unsigned char c = (unsigned char)name[i];
    if((c >= 'a' && c <= 'z') ||
       (c >= 'A' && c <= 'Z') ||
       (c >= '0' && c <= '9') ||
       c == '_' || c == '-' || c == ' ')
      continue;
    return 0;
  }
  return 1;
}

static int
avatar_copy_file(const char *src, const char *dst) {
  FILE *in = fopen(src, "rb");
  if(!in) return -1;
  FILE *out = fopen(dst, "wb");
  if(!out) { fclose(in); return -1; }

  int rc = 0;
  char buf[16384];
  for(;;) {
    size_t n = fread(buf, 1, sizeof(buf), in);
    if(n > 0 && fwrite(buf, 1, n, out) != n) { rc = -1; break; }
    if(n < sizeof(buf)) {
      if(ferror(in)) rc = -1;
      break;
    }
  }
  fclose(in);
  fclose(out);
  return rc;
}

static int
avatar_copy_dir_files(const char *from, const char *to) {
  DIR *d = opendir(from);
  if(!d) return -1;
  mkdir(to, 0755);

  int copied = 0;
  struct dirent *e;
  while((e = readdir(d)) != NULL) {
    if(e->d_name[0] == '.') continue;

    char src[320], dst[320];
    snprintf(src, sizeof(src), "%s/%s", from, e->d_name);
    snprintf(dst, sizeof(dst), "%s/%s", to,   e->d_name);

    struct stat st;
    if(stat(src, &st) != 0 || !S_ISREG(st.st_mode)) continue;
    if(avatar_copy_file(src, dst) == 0) copied++;
  }
  closedir(d);
  return copied;
}

static int
avatar_clear_dir_files(const char *dir) {
  DIR *d = opendir(dir);
  if(!d) return 0;

  int removed = 0;
  struct dirent *e;
  while((e = readdir(d)) != NULL) {
    if(e->d_name[0] == '.') continue;

    char path[320];
    snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
    struct stat st;
    if(stat(path, &st) != 0 || !S_ISREG(st.st_mode)) continue;
    if(unlink(path) == 0) removed++;
  }
  closedir(d);
  return removed;
}

static void
avatar_read_name_from_body(const ps5tm_request_t *req, char *name, size_t size) {
  name[0] = 0;
  if(!req || !req->body || !req->body_len) return;

  cJSON *root = cJSON_Parse(req->body);
  const cJSON *n = root ? cJSON_GetObjectItem(root, "name") : NULL;
  if(cJSON_IsString(n) && n->valuestring)
    snprintf(name, size, "%s", n->valuestring);
  cJSON_Delete(root);
}

static void
handle_avatar_library_list(int fd) {
  mkdir(PS5TM_DATA_DIR, 0755);
  mkdir(AVATAR_LIBRARY_DIR, 0755);

  cJSON *root = cJSON_CreateObject();
  cJSON_AddBoolToObject(root, "ok", 1);
  cJSON_AddStringToObject(root, "dir", AVATAR_LIBRARY_DIR);
  cJSON *arr = cJSON_AddArrayToObject(root, "packs");

  DIR *d = opendir(AVATAR_LIBRARY_DIR);
  if(d) {
    struct dirent *e;
    while((e = readdir(d)) != NULL) {
      if(e->d_name[0] == '.') continue;
      if(!avatar_pack_name_ok(e->d_name)) continue;

      char path[320];
      struct stat st;
      snprintf(path, sizeof(path), "%s/%s", AVATAR_LIBRARY_DIR, e->d_name);
      if(stat(path, &st) != 0 || !S_ISDIR(st.st_mode)) continue;

      cJSON *p = cJSON_CreateObject();
      cJSON_AddStringToObject(p, "name", e->d_name);
      cJSON_AddNumberToObject(p, "mtime", (double)st.st_mtime);
      cJSON_AddItemToArray(arr, p);
    }
    closedir(d);
  }

  char *txt = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if(!txt) {
    ps5tm_http_send_error(fd, 500, "alloc_failed", "Kein Speicher.");
    return;
  }
  ps5tm_http_send(fd, 200, "OK", "application/json", txt, strlen(txt), NULL);
  free(txt);
}

static void
handle_avatar_library_save(int fd, const ps5tm_request_t *req) {
  char name[64];
  avatar_read_name_from_body(req, name, sizeof(name));
  if(!avatar_pack_name_ok(name)) {
    ps5tm_http_send_error(fd, 400, "bad_name",
                          "Ungültiger Name. Erlaubt: Buchstaben, Zahlen, Leerzeichen, - und _ (max. 48 Zeichen).");
    return;
  }

  mkdir(PS5TM_DATA_DIR, 0755);
  mkdir(AVATAR_LIBRARY_DIR, 0755);

  char dst[320];
  snprintf(dst, sizeof(dst), "%s/%s", AVATAR_LIBRARY_DIR, name);
  mkdir(dst, 0755);

  int copied = avatar_copy_dir_files(ps5tm_avatar_stage_dir(), dst);
  if(copied <= 0) {
    ps5tm_http_send_error(fd, 400, "no_stage_files",
                          "Keine vorbereiteten Avatar-Dateien gefunden. Bitte zuerst ein Bild erstellen.");
    return;
  }

  PS5TM_INFO("avatar_library_saved",
             "Avatar-Paket '%s' gespeichert (%d Dateien).", name, copied);

  char body[256];
  snprintf(body, sizeof(body),
           "{\"ok\":true,\"name\":\"%s\",\"copied\":%d}",
           name, copied);
  ps5tm_http_send_json(fd, 200, body);
}

static void
handle_avatar_library_load(int fd, const ps5tm_request_t *req) {
  char name[64];
  avatar_read_name_from_body(req, name, sizeof(name));
  if(!avatar_pack_name_ok(name)) {
    ps5tm_http_send_error(fd, 400, "bad_name", "Ungültiger Paketname.");
    return;
  }

  char src[320];
  snprintf(src, sizeof(src), "%s/%s", AVATAR_LIBRARY_DIR, name);

  struct stat st;
  if(stat(src, &st) != 0 || !S_ISDIR(st.st_mode)) {
    ps5tm_http_send_error(fd, 404, "not_found", "Avatar-Paket nicht gefunden.");
    return;
  }

  mkdir(PS5TM_DATA_DIR, 0755);
  mkdir(ps5tm_avatar_stage_dir(), 0755);
  avatar_clear_dir_files(ps5tm_avatar_stage_dir());
  int copied = avatar_copy_dir_files(src, ps5tm_avatar_stage_dir());
  if(copied <= 0) {
    ps5tm_http_send_error(fd, 500, "load_failed",
                          "Avatar-Paket konnte nicht in den Staging-Ordner geladen werden.");
    return;
  }

  PS5TM_INFO("avatar_library_loaded",
             "Avatar-Paket '%s' in Stage geladen (%d Dateien).", name, copied);

  char body[256];
  snprintf(body, sizeof(body),
           "{\"ok\":true,\"name\":\"%s\",\"copied\":%d}",
           name, copied);
  ps5tm_http_send_json(fd, 200, body);
}

static void
handle_avatar_library_delete(int fd, const ps5tm_request_t *req) {
  char name[64];
  avatar_read_name_from_body(req, name, sizeof(name));
  if(!avatar_pack_name_ok(name)) {
    ps5tm_http_send_error(fd, 400, "bad_name", "Ungültiger Paketname.");
    return;
  }

  char dir[320];
  snprintf(dir, sizeof(dir), "%s/%s", AVATAR_LIBRARY_DIR, name);

  struct stat st;
  if(stat(dir, &st) != 0 || !S_ISDIR(st.st_mode)) {
    ps5tm_http_send_error(fd, 404, "not_found", "Avatar-Paket nicht gefunden.");
    return;
  }

  int removed = avatar_clear_dir_files(dir);
  if(rmdir(dir) != 0) {
    ps5tm_http_send_error(fd, 500, "delete_failed",
                          "Avatar-Paket konnte nicht gelöscht werden.");
    return;
  }

  PS5TM_INFO("avatar_library_deleted",
             "Avatar-Paket '%s' gelöscht (%d Dateien).", name, removed);

  char body[256];
  snprintf(body, sizeof(body),
           "{\"ok\":true,\"name\":\"%s\",\"removed\":%d}",
           name, removed);
  ps5tm_http_send_json(fd, 200, body);
}


/* --------------------------------------------------------------------- power */

/* Every one of these interrupts whatever the console is doing, so the action
   has to be named explicitly in the request body — there is no default, and a
   typo cannot accidentally shut the machine down. */
static void
handle_power(int fd, ps5tm_request_t *req) {
  cJSON *root = req->body ? cJSON_Parse(req->body) : NULL;
  const cJSON *act = root ? cJSON_GetObjectItem(root, "action") : NULL;

  if(!cJSON_IsString(act)) {
    cJSON_Delete(root);
    ps5tm_http_send_error(fd, 400, "missing_action",
                          "Es fehlt die Angabe, was geschehen soll "
                          "(\"off\", \"reboot\", \"standby\" oder "
                          "\"safemode\").");
    return;
  }

  char action[16];
  snprintf(action, sizeof(action), "%s", act->valuestring);
  cJSON_Delete(root);

  int         rc  = -1;
  const char *msg = NULL;

  if(!strcmp(action, "off")) {
    rc  = ps5tm_power_off();
    msg = "Die Konsole wird ausgeschaltet.";
  } else if(!strcmp(action, "reboot")) {
    rc  = ps5tm_power_reboot();
    msg = "Die Konsole startet neu.";
  } else if(!strcmp(action, "standby")) {
    rc  = ps5tm_power_standby();
    msg = "Die Konsole geht in den Ruhemodus.";
    if(rc == -2) {
      ps5tm_http_send_error(fd, 409, "standby_disabled",
                            "Der Ruhemodus ist in den Energieeinstellungen "
                            "der PS5 abgeschaltet. Dort zuerst aktivieren.");
      return;
    }
  } else if(!strcmp(action, "safemode")) {
    rc  = ps5tm_power_safemode();
    msg = "Die Konsole startet in den abgesicherten Modus.";
  } else {
    ps5tm_http_send_error(fd, 400, "unknown_action",
                          "Unbekannte Aktion.");
    return;
  }

  if(rc != 0) {
    ps5tm_http_send_error(fd, 503, "power_action_failed",
                          "Die Konsole hat den Befehl nicht angenommen. "
                          "Möglicherweise unterstützt diese Firmware ihn "
                          "nicht — Einzelheiten stehen im Protokoll.");
    return;
  }

  char body[192];
  snprintf(body, sizeof(body), "{\"ok\":true,\"message\":\"%s\"}", msg);
  ps5tm_http_send_json(fd, 200, body);
}


/* -------------------------------------------------------------------- router */

void
ps5tm_api_handle(int fd, ps5tm_request_t *req) {
  int is_get  = !strcmp(req->method, "GET");
  int is_post = !strcmp(req->method, "POST");
  int is_put  = !strcmp(req->method, "PUT");

  if(ps5tm_filemgr_api(fd, req)) return;              /* /api/v1/files/... */

  if(!strcmp(req->path, "/api/v1/status")) {
    if(!is_get) { ps5tm_http_send_error(fd, 405, "method_not_allowed",
                                        "Nur GET erlaubt."); return; }
    handle_status(fd);
    return;
  }

  if(!strcmp(req->path, "/api/v1/history")) {
    if(is_get) { handle_history(fd); return; }
    if(is_post) { ps5tm_history_reset();
                  ps5tm_http_send_json(fd, 200, "{\"ok\":true}"); return; }
    ps5tm_http_send_error(fd, 405, "method_not_allowed", "Nur GET und POST.");
    return;
  }

  if(!strcmp(req->path, "/api/v1/cooling-health")) {
    if(!is_get) { ps5tm_http_send_error(fd, 405, "method_not_allowed",
                                        "Nur GET erlaubt."); return; }
    handle_thermal(fd);
    return;
  }

  if(!strcmp(req->path, "/api/v1/channels")) {
    if(!is_get) { ps5tm_http_send_error(fd, 405, "method_not_allowed",
                                        "Nur GET erlaubt."); return; }
    handle_channels(fd);
    return;
  }

  if(!strcmp(req->path, "/api/v1/sensors/risky")) {
    if(!is_get) { ps5tm_http_send_error(fd, 405, "method_not_allowed",
                                        "Nur GET erlaubt."); return; }
    handle_risky_sensors(fd);
    return;
  }

  if(!strcmp(req->path, "/api/v1/drives")) {
    if(!is_get) { ps5tm_http_send_error(fd, 405, "method_not_allowed",
                                        "Nur GET erlaubt."); return; }
    handle_drives(fd);
    return;
  }

  if(!strcmp(req->path, "/api/v1/games")) {
    if(is_get) { handle_game_rules_get(fd); return; }
    if(is_put) { handle_game_rules_put(fd, req); return; }
    ps5tm_http_send_error(fd, 405, "method_not_allowed", "Nur GET und PUT.");
    return;
  }

  if(!strcmp(req->path, "/api/v1/games/export")) {
    if(!is_get) { ps5tm_http_send_error(fd, 405, "method_not_allowed",
                                        "Nur GET erlaubt."); return; }
    handle_game_rules_export(fd);
    return;
  }

  if(!strcmp(req->path, "/api/v1/games/import")) {
    if(!is_post) { ps5tm_http_send_error(fd, 405, "method_not_allowed",
                                         "Nur POST erlaubt."); return; }
    handle_game_rules_import(fd, req);
    return;
  }

  if(!strcmp(req->path, "/api/v1/library")) {
    if(!is_get) { ps5tm_http_send_error(fd, 405, "method_not_allowed",
                                        "Nur GET erlaubt."); return; }
    handle_library(fd);
    return;
  }

  if(!strcmp(req->path, "/api/v1/library/cache")) {
    if(!is_get) { ps5tm_http_send_error(fd, 405, "method_not_allowed",
                                        "Nur GET erlaubt."); return; }
    send_cjson(fd, 200, ps5tm_libcache_json());
    return;
  }

  if(!strcmp(req->path, "/api/v1/library/cache/clear")) {
    if(!is_post) { ps5tm_http_send_error(fd, 405, "method_not_allowed",
                                         "Nur POST erlaubt."); return; }
    handle_library_cache_clear(fd);
    return;
  }

  if(!strcmp(req->path, "/api/v1/library/probe")) {
    if(!is_post) { ps5tm_http_send_error(fd, 405, "method_not_allowed",
                                         "Nur POST erlaubt."); return; }
    if(ps5tm_library_probe_now() != 0) {
      ps5tm_http_send_error(fd, 503, "probe_not_running", "Die Prüfung der Abbilder läuft nicht.");
      return;
    }
    ps5tm_http_send_json(fd, 202, "{\"ok\":true,\"message\":\"Die Prüfung der Abbilder wurde angefordert.\"}");
    return;
  }

  if(!strcmp(req->path, "/api/v1/library/cover")) {
    if(!is_get) { ps5tm_http_send_error(fd, 405, "method_not_allowed",
                                        "Nur GET erlaubt."); return; }
    handle_library_cover(fd, req);
    return;
  }

  if(!strcmp(req->path, "/api/v1/library/launch")) {
    if(!is_post) { ps5tm_http_send_error(fd, 405, "method_not_allowed",
                                         "Nur POST erlaubt."); return; }
    handle_library_launch(fd, req);
    return;
  }

  if(!strcmp(req->path, "/api/v1/library/close")) {
    if(is_get)  { handle_library_close_status(fd); return; }
    if(is_post) { handle_library_close(fd, req); return; }
    ps5tm_http_send_error(fd, 405, "method_not_allowed", "Nur GET und POST.");
    return;
  }

  if(!strcmp(req->path, "/api/v1/library/delete/plan")) {
    if(!is_post) { ps5tm_http_send_error(fd, 405, "method_not_allowed", "Nur POST erlaubt."); return; }
    handle_delete_plan(fd, req);
    return;
  }

  if(!strcmp(req->path, "/api/v1/library/delete")) {
    if(is_get)  { send_cjson(fd, 200, ps5tm_gamedelete_status()); return; }
    if(is_post) { handle_delete_start(fd, req); return; }
    ps5tm_http_send_error(fd, 405, "method_not_allowed", "Nur GET und POST.");
    return;
  }

  if(!strcmp(req->path, "/api/v1/library/backups")) {
    if(!is_get) { ps5tm_http_send_error(fd, 405, "method_not_allowed", "Nur GET erlaubt."); return; }
    send_cjson(fd, 200, ps5tm_gamedelete_backups_json());
    return;
  }

  if(!strcmp(req->path, "/api/v1/library/copy/plan")) {
    if(!is_get) { ps5tm_http_send_error(fd, 405, "method_not_allowed",
                                        "Nur GET erlaubt."); return; }
    handle_copy_plan(fd, req);
    return;
  }

  if(!strcmp(req->path, "/api/v1/library/copy")) {
    if(is_get)  { send_cjson(fd, 200, ps5tm_gamecopy_status()); return; }
    if(is_post) { handle_copy_start(fd, req); return; }
    ps5tm_http_send_error(fd, 405, "method_not_allowed", "Nur GET und POST.");
    return;
  }

  if(!strcmp(req->path, "/api/v1/library/copy/cancel")) {
    if(!is_post) { ps5tm_http_send_error(fd, 405, "method_not_allowed",
                                         "Nur POST erlaubt."); return; }
    if(ps5tm_gamecopy_cancel() != 0) {
      ps5tm_http_send_error(fd, 409, "no_copy", "Es läuft keine Kopie.");
      return;
    }
    ps5tm_http_send_json(fd, 200, "{\"ok\":true}");
    return;
  }

  if(!strcmp(req->path, "/api/v1/library/convert/plan")) {
    if(!is_get) { ps5tm_http_send_error(fd, 405, "method_not_allowed",
                                        "Nur GET erlaubt."); return; }
    handle_convert_plan(fd, req);
    return;
  }

  if(!strcmp(req->path, "/api/v1/library/convert")) {
    if(is_get)  { send_cjson(fd, 200, ps5tm_gameconvert_status()); return; }
    if(is_post) { handle_convert_start(fd, req); return; }
    ps5tm_http_send_error(fd, 405, "method_not_allowed", "Nur GET und POST.");
    return;
  }

  if(!strcmp(req->path, "/api/v1/library/convert/cancel")) {
    if(!is_post) { ps5tm_http_send_error(fd, 405, "method_not_allowed",
                                         "Nur POST erlaubt."); return; }
    if(ps5tm_gameconvert_cancel() != 0) {
      ps5tm_http_send_error(fd, 409, "no_convert", "Es läuft keine Konvertierung.");
      return;
    }
    ps5tm_http_send_json(fd, 200, "{\"ok\":true}");
    return;
  }

  if(!strcmp(req->path, "/api/v1/library/storage/plan")) {
    if(!is_get) { ps5tm_http_send_error(fd, 405, "method_not_allowed",
                                        "Nur GET erlaubt."); return; }
    handle_storage_plan(fd, req);
    return;
  }

  if(!strcmp(req->path, "/api/v1/library/storage")) {
    if(is_get)  { handle_storage_status(fd); return; }
    if(is_post) { handle_storage_start(fd, req); return; }
    ps5tm_http_send_error(fd, 405, "method_not_allowed", "Nur GET und POST.");
    return;
  }

  if(!strcmp(req->path, "/api/v1/library/storage/cancel")) {
    if(!is_post) { ps5tm_http_send_error(fd, 405, "method_not_allowed",
                                         "Nur POST erlaubt."); return; }
    char err[256] = "";
    int st = ps5tm_gamemove_cancel(err, sizeof(err));
    if(st != 200) { ps5tm_http_send_error(fd, st, "no_job", err); return; }
    ps5tm_http_send_json(fd, 200, "{\"ok\":true}");
    return;
  }

  if(!strcmp(req->path, "/api/v1/payloads")) {
    if(is_get) { handle_payloads_get(fd); return; }
    ps5tm_http_send_error(fd, 405, "method_not_allowed", "Nur GET erlaubt.");
    return;
  }

  if(!strcmp(req->path, "/api/v1/payloads/kill")) {
    if(!is_post) { ps5tm_http_send_error(fd, 405, "method_not_allowed",
                                         "Nur POST erlaubt."); return; }
    handle_payload_kill(fd, req);
    return;
  }

  if(!strcmp(req->path, "/api/v1/payload-profiles")) {
    if(is_get) {
      char *txt = ps5tm_payprof_get_json();
      if(!txt) { ps5tm_http_send_error(fd, 500, "alloc_failed", "Kein Speicher."); return; }
      ps5tm_http_send(fd, 200, "OK", "application/json", txt, strlen(txt), NULL);
      free(txt);
      return;
    }
    if(!is_post) { ps5tm_http_send_error(fd, 405, "method_not_allowed", "Nur GET oder POST erlaubt."); return; }
    char err[256] = "", *out = NULL;
    int st = ps5tm_payprof_save(req->body ? req->body : "", &out, err, sizeof(err));
    if(st != 200) { ps5tm_http_send_error(fd, st, "profiles_refused", err); return; }
    ps5tm_http_send(fd, 200, "OK", "application/json", out, strlen(out), NULL);
    free(out);
    return;
  }

  if(!strcmp(req->path, "/api/v1/payload-profiles/status")) {
    if(!is_get) { ps5tm_http_send_error(fd, 405, "method_not_allowed", "Nur GET erlaubt."); return; }
    char *txt = ps5tm_payprof_status_json();
    if(!txt) { ps5tm_http_send_error(fd, 500, "alloc_failed", "Kein Speicher."); return; }
    ps5tm_http_send(fd, 200, "OK", "application/json", txt, strlen(txt), NULL);
    free(txt);
    return;
  }

  if(!strcmp(req->path, "/api/v1/payload-profiles/run") || !strcmp(req->path, "/api/v1/payload-profiles/stop")) {
    if(!is_post) { ps5tm_http_send_error(fd, 405, "method_not_allowed", "Nur POST erlaubt."); return; }
    if(!strcmp(req->path, "/api/v1/payload-profiles/stop")) {
      ps5tm_payprof_stop();
      ps5tm_http_send_json(fd, 200, "{\"ok\":true}");
      return;
    }
    cJSON *root = req->body ? cJSON_Parse(req->body) : NULL;
    const char *id = json_text(root, "id");
    char err[160] = "";
    int st = ps5tm_payprof_run(id, 0, err, sizeof(err));
    cJSON_Delete(root);
    if(st != 200) { ps5tm_http_send_error(fd, st, "profile_not_started", err); return; }
    ps5tm_http_send_json(fd, 202, "{\"ok\":true}");
    return;
  }

  if(!strcmp(req->path, "/api/v1/payload-files")) {
    if(is_get) { handle_payload_files_get(fd); return; }
    ps5tm_http_send_error(fd, 405, "method_not_allowed", "Nur GET erlaubt.");
    return;
  }

  if(!strcmp(req->path, "/api/v1/payload-files/start") ||
     !strcmp(req->path, "/api/v1/payload-files/copy") ||
     !strcmp(req->path, "/api/v1/payload-files/delete")) {
    if(!is_post) { ps5tm_http_send_error(fd, 405, "method_not_allowed",
                                         "Nur POST erlaubt."); return; }
    int op = !strcmp(req->path, "/api/v1/payload-files/start") ? PLOP_START
           : !strcmp(req->path, "/api/v1/payload-files/copy")  ? PLOP_COPY
           : PLOP_DELETE;
    handle_payload_files_op(fd, req, op);
    return;
  }

  if(!strcmp(req->path, "/api/v1/system")) {
    if(!is_get) { ps5tm_http_send_error(fd, 405, "method_not_allowed",
                                        "Nur GET erlaubt."); return; }
    handle_system(fd);
    return;
  }

  if(!strcmp(req->path, "/api/v1/controller/diag")) {
    if(!is_get) { ps5tm_http_send_error(fd, 405, "method_not_allowed",
                                        "Nur GET erlaubt."); return; }
    handle_controller_diag(fd);
    return;
  }

  if(!strcmp(req->path, "/api/v1/power")) {
    if(!is_post) { ps5tm_http_send_error(fd, 405, "method_not_allowed",
                                         "Nur POST erlaubt."); return; }
    handle_power(fd, req);
    return;
  }

  if(!strcmp(req->path, "/api/v1/profile/username")) {
    if(!is_post) { ps5tm_http_send_error(fd, 405, "method_not_allowed",
                                         "Nur POST erlaubt."); return; }
    handle_profile_username(fd, req);
    return;
  }

  if(!strcmp(req->path, "/api/v1/profile/avatar/current")) {
    if(!is_get) { ps5tm_http_send_error(fd, 405, "method_not_allowed",
                                        "Nur GET erlaubt."); return; }
    handle_avatar_current(fd);
    return;
  }

  if(!strcmp(req->path, "/api/v1/profile/avatar/apply")) {
    if(!is_post) { ps5tm_http_send_error(fd, 405, "method_not_allowed",
                                         "Nur POST erlaubt."); return; }
    handle_avatar_commit(fd, 0);
    return;
  }

  if(!strcmp(req->path, "/api/v1/profile/avatar/restore")) {
    if(!is_post) { ps5tm_http_send_error(fd, 405, "method_not_allowed",
                                         "Nur POST erlaubt."); return; }
    handle_avatar_commit(fd, 1);
    return;
  }

  if(!strcmp(req->path, "/api/v1/profile/avatar/library")) {
    if(is_get) { handle_avatar_library_list(fd); return; }
    ps5tm_http_send_error(fd, 405, "method_not_allowed", "Nur GET erlaubt.");
    return;
  }

  if(!strcmp(req->path, "/api/v1/profile/avatar/library/save")) {
    if(!is_post) { ps5tm_http_send_error(fd, 405, "method_not_allowed",
                                         "Nur POST erlaubt."); return; }
    handle_avatar_library_save(fd, req);
    return;
  }

  if(!strcmp(req->path, "/api/v1/profile/avatar/library/load")) {
    if(!is_post) { ps5tm_http_send_error(fd, 405, "method_not_allowed",
                                         "Nur POST erlaubt."); return; }
    handle_avatar_library_load(fd, req);
    return;
  }

  if(!strcmp(req->path, "/api/v1/profile/avatar/library/delete")) {
    if(!is_post) { ps5tm_http_send_error(fd, 405, "method_not_allowed",
                                         "Nur POST erlaubt."); return; }
    handle_avatar_library_delete(fd, req);
    return;
  }

  if(!strcmp(req->path, "/api/v1/config")) {
    if(is_get) { handle_config_get(fd); return; }
    if(is_put) { handle_config_put(fd, req); return; }
    ps5tm_http_send_error(fd, 405, "method_not_allowed",
                          "Nur GET und PUT erlaubt.");
    return;
  }

  if(!strcmp(req->path, "/api/v1/config/export")) {
    if(is_get) { handle_config_export(fd); return; }
    ps5tm_http_send_error(fd, 405, "method_not_allowed",
                          "Nur GET erlaubt.");
    return;
  }

  if(!strcmp(req->path, "/api/v1/config/import")) {
    if(is_post) { handle_config_import(fd, req); return; }
    ps5tm_http_send_error(fd, 405, "method_not_allowed",
                          "Nur POST erlaubt.");
    return;
  }

  if(!strcmp(req->path, "/api/v1/fan/threshold")) {
    if(!is_post) { ps5tm_http_send_error(fd, 405, "method_not_allowed",
                                         "Nur POST erlaubt."); return; }
    handle_fan_threshold(fd, req);
    return;
  }

  if(!strcmp(req->path, "/api/v1/tile/ensure")) {
    if(!is_post) { ps5tm_http_send_error(fd, 405, "method_not_allowed",
                                         "Nur POST erlaubt."); return; }
    handle_tile_ensure(fd);
    return;
  }

  if(!strcmp(req->path, "/api/v1/logs")) {
    if(!is_get) { ps5tm_http_send_error(fd, 405, "method_not_allowed",
                                        "Nur GET erlaubt."); return; }
    handle_logs(fd);
    return;
  }

  if(!strcmp(req->path, "/api/v1/logs/export")) {
    if(!is_get) { ps5tm_http_send_error(fd, 405, "method_not_allowed",
                                        "Nur GET erlaubt."); return; }
    handle_logs_export(fd);
    return;
  }

  if(!strcmp(req->path, "/api/v1/klog")) {
    if(!is_get) { ps5tm_http_send_error(fd, 405, "method_not_allowed",
                                        "Nur GET erlaubt."); return; }
    handle_klog(fd, req);
    return;
  }

  if(!strcmp(req->path, "/api/v1/klog/files")) {
    if(!is_get) { ps5tm_http_send_error(fd, 405, "method_not_allowed",
                                        "Nur GET erlaubt."); return; }
    send_cjson(fd, 200, ps5tm_klogfiles_json());
    return;
  }

  if(!strcmp(req->path, "/api/v1/klog/file")) {
    if(!is_get) { ps5tm_http_send_error(fd, 405, "method_not_allowed",
                                        "Nur GET erlaubt."); return; }
    handle_klog_file(fd, req);
    return;
  }

  if(!strcmp(req->path, "/api/v1/klog/files/clear")) {
    if(!is_post) { ps5tm_http_send_error(fd, 405, "method_not_allowed",
                                         "Nur POST erlaubt."); return; }
    handle_klog_files_clear(fd);
    return;
  }

  if(!strcmp(req->path, "/api/v1/klog/record")) {
    if(is_get) {
      cJSON *o = ps5tm_klogrec_json();
      if(o) cJSON_AddBoolToObject(o, "ok", 1);
      send_cjson(fd, 200, o);
      return;
    }
    if(!is_post) { ps5tm_http_send_error(fd, 405, "method_not_allowed",
                                         "Nur GET oder POST erlaubt."); return; }
    handle_klog_record_post(fd, req);
    return;
  }

  if(!strcmp(req->path, "/api/v1/klog/save")) {
    /* POST arrives at the receiver in http.c before it gets here. */
    ps5tm_http_send_error(fd, 405, "method_not_allowed", "Nur POST erlaubt.");
    return;
  }

  if(!strcmp(req->path, "/api/v1/playtime")) {
    if(!is_get) { ps5tm_http_send_error(fd, 405, "method_not_allowed",
                                        "Nur GET erlaubt."); return; }
    handle_playtime(fd, req);
    return;
  }

  if(!strcmp(req->path, "/api/v1/playtime/reset")) {
    if(!is_post) { ps5tm_http_send_error(fd, 405, "method_not_allowed",
                                         "Nur POST erlaubt."); return; }
    if(ps5tm_playtime_reset() != 0) {
      ps5tm_http_send_error(fd, 500, "playtime_reset_failed",
                            "Der Spielzeit-Verlauf ließ sich nicht löschen.");
      return;
    }
    ps5tm_http_send_json(fd, 200, "{\"ok\":true}");
    return;
  }

  if(!strcmp(req->path, "/api/v1/saves")) {
    if(!is_get) { ps5tm_http_send_error(fd, 405, "method_not_allowed",
                                        "Nur GET erlaubt."); return; }
    send_cjson(fd, 200, ps5tm_saves_json());
    return;
  }

  if(!strcmp(req->path, "/api/v1/saves/avatar")) {
    if(!is_get) { ps5tm_http_send_error(fd, 405, "method_not_allowed",
                                        "Nur GET erlaubt."); return; }
    handle_saves_avatar(fd, req->query);
    return;
  }

  if(!strcmp(req->path, "/api/v1/saves/job")) {
    if(!is_get) { ps5tm_http_send_error(fd, 405, "method_not_allowed",
                                        "Nur GET erlaubt."); return; }
    send_cjson(fd, 200, ps5tm_saves_job_json());
    return;
  }

  if(!strcmp(req->path, "/api/v1/saves/backup") ||
     !strcmp(req->path, "/api/v1/saves/verify") ||
     !strcmp(req->path, "/api/v1/saves/restore") ||
     !strcmp(req->path, "/api/v1/saves/delete") ||
     !strcmp(req->path, "/api/v1/saves/cancel")) {
    if(!is_post) { ps5tm_http_send_error(fd, 405, "method_not_allowed",
                                         "Nur POST erlaubt."); return; }
    if(!strcmp(req->path, "/api/v1/saves/backup"))       handle_saves_backup(fd, req);
    else if(!strcmp(req->path, "/api/v1/saves/verify"))  handle_saves_verify(fd, req);
    else if(!strcmp(req->path, "/api/v1/saves/restore")) handle_saves_restore(fd, req);
    else if(!strcmp(req->path, "/api/v1/saves/delete"))  handle_saves_delete(fd, req);
    else { ps5tm_saves_cancel(); send_cjson(fd, 200, ps5tm_saves_job_json()); }
    return;
  }

  if(!strcmp(req->path, "/api/v1/packages")) {
    if(!is_get) { ps5tm_http_send_error(fd, 405, "method_not_allowed",
                                        "Nur GET erlaubt."); return; }
    send_cjson(fd, 200, ps5tm_pkgscan_json());
    return;
  }

  if(!strcmp(req->path, "/api/v1/packages/scan")) {
    if(!is_post) { ps5tm_http_send_error(fd, 405, "method_not_allowed",
                                         "Nur POST erlaubt."); return; }
    if(ps5tm_pkgscan_start() < 0) {
      ps5tm_http_send_error(fd, 503, "scan_failed", "Die Suche ließ sich nicht starten.");
      return;
    }
    send_cjson(fd, 200, ps5tm_pkgscan_json());
    return;
  }

  if(!strcmp(req->path, "/api/v1/packages/icon")) {
    if(!is_get) { ps5tm_http_send_error(fd, 405, "method_not_allowed",
                                        "Nur GET erlaubt."); return; }
    handle_packages_icon(fd, req);
    return;
  }

  if(!strcmp(req->path, "/api/v1/packages/job")) {
    if(!is_get) { ps5tm_http_send_error(fd, 405, "method_not_allowed",
                                        "Nur GET erlaubt."); return; }
    send_cjson(fd, 200, ps5tm_pkgsplit_job_json());
    return;
  }

  if(!strcmp(req->path, "/api/v1/packages/split") ||
     !strcmp(req->path, "/api/v1/packages/cancel")) {
    if(!is_post) { ps5tm_http_send_error(fd, 405, "method_not_allowed",
                                         "Nur POST erlaubt."); return; }
    if(!strcmp(req->path, "/api/v1/packages/split")) handle_packages_split(fd, req);
    else { ps5tm_pkgsplit_cancel(); send_cjson(fd, 200, ps5tm_pkgsplit_job_json()); }
    return;
  }

  if(!strcmp(req->path, "/api/v1/packages/install/plan")) {
    if(!is_get) { ps5tm_http_send_error(fd, 405, "method_not_allowed",
                                        "Nur GET erlaubt."); return; }
    handle_packages_install_plan(fd, req);
    return;
  }

  if(!strcmp(req->path, "/api/v1/packages/install/job")) {
    if(!is_get) { ps5tm_http_send_error(fd, 405, "method_not_allowed",
                                        "Nur GET erlaubt."); return; }
    send_cjson(fd, 200, ps5tm_pkginst_job_json());
    return;
  }

  if(!strcmp(req->path, "/api/v1/packages/install") ||
     !strcmp(req->path, "/api/v1/packages/install/cancel")) {
    if(!is_post) { ps5tm_http_send_error(fd, 405, "method_not_allowed",
                                         "Nur POST erlaubt."); return; }
    if(!strcmp(req->path, "/api/v1/packages/install")) handle_packages_install(fd, req);
    else { ps5tm_pkginst_cancel(); send_cjson(fd, 200, ps5tm_pkginst_job_json()); }
    return;
  }

  if(!strcmp(req->path, "/api/v1/logs/tail")) {
    if(!is_get) { ps5tm_http_send_error(fd, 405, "method_not_allowed",
                                        "Nur GET erlaubt."); return; }
    handle_logs_tail(fd, req);
    return;
  }

  if(is_get && ps5tm_asset_serve(fd, req)) return;

  ps5tm_http_send_error(fd, 404, "not_found",
                        "Diese Adresse existiert nicht.");
}
