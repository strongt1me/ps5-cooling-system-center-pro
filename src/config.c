/* Persistent configuration: /data/ps5-temperature-manager/config.json */

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include "ps5tm.h"
#include "third_party/cJSON.h"

ps5tm_config_t g_config;

/* Recursive: a handler holds the lock while it mutates g_config and calls
   helpers that take it again (the probe-session functions below, for one).
   Not recursive into ps5tm_config_save(): that one is called after the lock has
   been released — see its comment in ps5tm.h for why. */
static pthread_mutex_t g_lock;
static pthread_once_t  g_lock_once = PTHREAD_ONCE_INIT;

static void
lock_init(void) {
  pthread_mutexattr_t attr;
  pthread_mutexattr_init(&attr);
  pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
  pthread_mutex_init(&g_lock, &attr);
  pthread_mutexattr_destroy(&attr);
}

void
ps5tm_config_lock(void) {
  pthread_once(&g_lock_once, lock_init);
  pthread_mutex_lock(&g_lock);
}

void
ps5tm_config_unlock(void) {
  pthread_mutex_unlock(&g_lock);
}


static unsigned
clampu(unsigned v, unsigned lo, unsigned hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

#define PROBE_BITS (PS5TM_PROBE_GAME | PS5TM_PROBE_PAD | PS5TM_PROBE_NETDISP | \
                    PS5TM_PROBE_RISKY | PS5TM_PROBE_DRIVE | PS5TM_PROBE_FPS)


/* The timed probe session. Lives in memory only, under the config lock: a
   restart forgets it, and the file — which never carried the temporary mask —
   is then already the right answer. */
static int      g_revert_pending;
static unsigned g_revert_mask;
static uint64_t g_revert_at_ms;                 /* ps5tm_mono_ms() */

void
ps5tm_config_probe_revert_arm(unsigned previous_mask, unsigned seconds) {
  ps5tm_config_lock();
  /* A second session on top of a running one keeps the first one's mask: that
     is the one the person had chosen, the running one is only a loan. */
  if(!g_revert_pending) g_revert_mask = previous_mask & PROBE_BITS;
  g_revert_pending = 1;
  g_revert_at_ms   = ps5tm_mono_ms() + (uint64_t)seconds * 1000;
  ps5tm_config_unlock();
}

void
ps5tm_config_probe_revert_cancel(void) {
  ps5tm_config_lock();
  g_revert_pending = 0;
  ps5tm_config_unlock();
}

int
ps5tm_config_probe_revert_state(unsigned *mask, unsigned *seconds_left) {
  ps5tm_config_lock();
  int pending = g_revert_pending;
  if(pending) {
    uint64_t now = ps5tm_mono_ms();
    if(mask) *mask = g_revert_mask;
    if(seconds_left)
      *seconds_left = g_revert_at_ms > now ? (unsigned)((g_revert_at_ms - now + 999) / 1000) : 0;
  }
  ps5tm_config_unlock();
  return pending;
}

void
ps5tm_config_tick(void) {
  ps5tm_config_lock();
  int due = g_revert_pending && ps5tm_mono_ms() >= g_revert_at_ms;
  if(due) {
    g_config.probe_mask = g_revert_mask;
    g_revert_pending    = 0;
  }
  unsigned back = g_revert_mask;
  ps5tm_config_unlock();
  if(due)
    PS5TM_INFO("probe_session_ended",
               "Diagnosedauer abgelaufen – Zusatzabfragen wieder auf Stand 0x%02X.",
               back);
}


int
ps5tm_config_bind_valid(const char *s) {
  struct in_addr a;
  return s && inet_pton(AF_INET, s, &a) == 1;
}


void
ps5tm_config_defaults(ps5tm_config_t *cfg) {
  memset(cfg, 0, sizeof(*cfg));
  cfg->schema_version  = PS5TM_SCHEMA_VERSION;
  cfg->mode            = PS5TM_MODE_AUTOMATIC;
  cfg->fan_threshold_c = 65;
  cfg->fan_reapply_sec = PS5TM_REAPPLY_SEC;
  cfg->warning_cpu_c   = 80;
  cfg->warning_soc_c   = 85;
  cfg->http_port       = PS5TM_DEFAULT_HTTP_PORT;
  snprintf(cfg->bind_address, sizeof(cfg->bind_address), "0.0.0.0");
  /* Two legacy probes are proven harmless, so they no longer start switched
   * off. Tested on 01.08.2026 with a game actually running:
   *   GAME    — survives; the fatal call was removed in 1.10.1, and the
   *             message-buffer route it uses now cannot contend with a title
   *   NETDISP — survives and delivers: SSID, address, signal, band
   *   PAD     — NOT included. It appeared to survive the same test, but the
   *             pad functions had never resolved, so nothing was executed and
   *             the result proves nothing. See dualsense.c.
   * Newer optional probes (RISKY/DRIVE) stay off until requested. */
  cfg->probe_mask = PS5TM_PROBE_GAME | PS5TM_PROBE_NETDISP;

  /* Comfort controller. The defaults aim for a console you stop noticing:
     hold 66 °C, do nothing at all between 65 and 67, re-evaluate only every
     5 s and then move the fan by a single percent. */
  cfg->target_temp_c      = 66;
  /* 14 K puts full speed at 80 °C for the default target — above anything
     this console reaches while gaming (69–74 °C), so the fan has room to
     scale instead of sitting at one end. */
  cfg->control_band_c     = 14;
  cfg->deadband_c         = 1;
  cfg->control_interval_s = 5;
  cfg->average_window_s   = 12;
  cfg->max_step_pct       = 1;
  cfg->safety_temp_c      = 78;
  cfg->profile            = PS5TM_PROFILE_COMFORT;
  cfg->warning_countdown_s = 120;
  cfg->telemetry_retention_days = 90;
  cfg->lightbar_enabled = 0;
  cfg->lightbar_warn_c = 70;
  cfg->lightbar_hot_c = 76;
  cfg->ps_button_status = 1;
  cfg->library_cache = 0;

  /* "Quiet when idle, cool under load."
   *
   * The ICC threshold behaves like a target temperature: while it stays
   * ABOVE the current reading the firmware leaves the fan on its own gentle
   * baseline; once it drops BELOW, the fan ramps towards full. The duty
   * figures below are therefore chosen so the resulting threshold stays
   * clear of the console's normal 55-68 °C operating band — an earlier curve
   * put the threshold a degree under the idle temperature, which pinned the
   * fan at 100 % and made it hunt.
   *
   * Calibrated against a real console (FW 12.00): it idles around 65 °C and
   * reaches 69-74 °C while gaming.
   *
   * The steps are deliberately even. An earlier version jumped from 10 % to
   * 40 % between 76 and 78 °C, and because the controller chases whatever the
   * curve asks for, that cliff made the fan lurch instead of ramping. A gentle
   * slope gives the gradual rise in speed that actually sounds right:
   *
   *   70 °C ->  7 %   (at or below the fan's own idle speed: silent)
   *   72 °C -> 15 %
   *   74 °C -> 25 %
   *   76 °C -> 35 %
   *   78 °C -> 55 %
   *   80 °C -> 75 %
   */
  static const ps5tm_curve_point_t defaults[] = {
    { 45,  0 },
    { 68,  0 },
    { 72, 15 },
    { 76, 35 },
    { 80, 75 },
  };
  cfg->curve_len = sizeof(defaults) / sizeof(defaults[0]);
  memcpy(cfg->curve, defaults, sizeof(defaults));
}


static int
curve_cmp(const void *a, const void *b) {
  const ps5tm_curve_point_t *pa = a, *pb = b;
  if(pa->temperature_c < pb->temperature_c) return -1;
  if(pa->temperature_c > pb->temperature_c) return  1;
  return 0;
}


/* The five numbers that give the controller its character are derived from
 * the chosen profile instead of being set one by one.
 *
 * They used to be five separate fields in the settings page, and telling a
 * good value from a bad one meant understanding proportional control. Worse,
 * they duplicated the profile: "Leise" already means wide, slow and heavily
 * smoothed. Two ways to say the same thing, free to contradict each other —
 * a person could pick "Leise" and a 5 °C band and get a loud console.
 *
 * Comfort keeps exactly the numbers measured on the console on 01.08.2026:
 * they held 69–70 °C at 37–39 % fan through a game whose load swung between
 * 13 % and 87 %. Not guessed — see the comfort-controller memory. The other
 * two scale from there: a narrower band and a bigger step react sooner and
 * harder, a shorter window smooths less.
 *
 * The values stay in the config and in /api/v1/config, so what the controller
 * is actually doing remains inspectable. They are simply no longer typed in
 * by hand. */
static void
apply_profile_presets(ps5tm_config_t *cfg) {
  switch(cfg->profile) {
    case PS5TM_PROFILE_COOL:
      cfg->control_band_c     = 7;
      cfg->deadband_c         = 1;
      cfg->control_interval_s = 4;
      cfg->average_window_s   = 8;
      cfg->max_step_pct       = 3;
      break;
    case PS5TM_PROFILE_BALANCED:
      cfg->control_band_c     = 10;
      cfg->deadband_c         = 1;
      cfg->control_interval_s = 5;
      cfg->average_window_s   = 10;
      cfg->max_step_pct       = 2;
      break;
    default:                    /* COMFORT — the values proven on hardware */
      cfg->control_band_c     = 14;
      cfg->deadband_c         = 1;
      cfg->control_interval_s = 5;
      cfg->average_window_s   = 12;
      cfg->max_step_pct       = 1;
      break;
  }
}


void
ps5tm_config_clamp(ps5tm_config_t *cfg) {
  cfg->schema_version  = PS5TM_SCHEMA_VERSION;
  cfg->fan_threshold_c = clampu(cfg->fan_threshold_c,
                                PS5TM_THRESHOLD_MIN_C, PS5TM_THRESHOLD_MAX_C);
  cfg->fan_reapply_sec = clampu(cfg->fan_reapply_sec,
                                PS5TM_REAPPLY_MIN_SEC,
                                PS5TM_REAPPLY_MAX_SEC);
  cfg->warning_cpu_c   = clampu(cfg->warning_cpu_c, 40, 110);
  /* One warning limit is enough to type in. The main chip is given more
     headroom than the CPU core, exactly as before — the two defaults were 80
     and 85 — so keeping that offset turns two fields into one without
     changing when either warning fires. */
  cfg->warning_soc_c   = clampu(cfg->warning_cpu_c + 5, 40, 110);
  cfg->http_port       = clampu(cfg->http_port, 1, 65535);
  if(cfg->mode != PS5TM_MODE_AUTOMATIC) cfg->mode = PS5TM_MODE_OBSERVE;
  /* Only a dotted-quad address can ever be bound, and the string is written
     into the settings file and the JSON answers as it stands — anything else
     (a typo, a hostname, a quote) would at best fail to bind and at worst
     break both. */
  if(!ps5tm_config_bind_valid(cfg->bind_address)) {
    if(cfg->bind_address[0])
      PS5TM_WARN("config_bind_reset",
                 "Ungültige Bind-Adresse in den Einstellungen – 0.0.0.0 wird "
                 "verwendet.");
    snprintf(cfg->bind_address, sizeof(cfg->bind_address), "0.0.0.0");
  }
  /* Keep only bits that mean something; a stray value cannot switch on a
     probe that does not exist. */
  cfg->probe_mask &= (PS5TM_PROBE_GAME | PS5TM_PROBE_PAD |
                      PS5TM_PROBE_NETDISP | PS5TM_PROBE_RISKY |
                      PS5TM_PROBE_DRIVE | PS5TM_PROBE_FPS);

  cfg->target_temp_c      = clampu(cfg->target_temp_c,
                                   PS5TM_TARGET_MIN_C, PS5TM_TARGET_MAX_C);
  cfg->safety_temp_c      = clampu(cfg->safety_temp_c,
                                   PS5TM_SAFETY_MIN_C, PS5TM_SAFETY_MAX_C);
  cfg->warning_countdown_s = clampu(cfg->warning_countdown_s, 30, 900);
  cfg->telemetry_retention_days = clampu(cfg->telemetry_retention_days, 7, 365);
  cfg->lightbar_enabled = cfg->lightbar_enabled ? 1u : 0u;
  cfg->ps_button_status = cfg->ps_button_status ? 1u : 0u;
  cfg->library_cache = cfg->library_cache ? 1u : 0u;
  cfg->lightbar_warn_c = clampu(cfg->lightbar_warn_c, 55, 90);
  cfg->lightbar_hot_c = clampu(cfg->lightbar_hot_c, 56, 95);
  if(cfg->lightbar_hot_c <= cfg->lightbar_warn_c)
    cfg->lightbar_hot_c = cfg->lightbar_warn_c + 1;
  if(cfg->profile != PS5TM_PROFILE_BALANCED &&
     cfg->profile != PS5TM_PROFILE_COOL)
    cfg->profile = PS5TM_PROFILE_COMFORT;

  /* Once the profile is known, and unconditionally — a value left over in an
     older config file must not outlive the field that used to set it. */
  apply_profile_presets(cfg);

  /* The safety limit must stay clear of the target, or the controller would
     flip into emergency mode while it is doing its job correctly. */
  if(cfg->safety_temp_c < cfg->target_temp_c + 4)
    cfg->safety_temp_c = cfg->target_temp_c + 4;

  /* A game's own target is held to the same range as the global one. Only the
     two routes that edit rules used to look at it; a settings file or an
     import with 500 in it kept the fan at its floor until the safety limit,
     and a huge value overflowed the controller's arithmetic. */
  if(cfg->game_rule_count > PS5TM_MAX_GAME_RULES)
    cfg->game_rule_count = PS5TM_MAX_GAME_RULES;
  for(unsigned i = 0; i < cfg->game_rule_count; i++) {
    ps5tm_game_rule_t *g = &cfg->game_rules[i];
    g->title_id[sizeof(g->title_id) - 1]     = 0;
    g->title_name[sizeof(g->title_name) - 1] = 0;
    g->target_temp_c = clampu(g->target_temp_c,
                              PS5TM_TARGET_MIN_C, PS5TM_TARGET_MAX_C);
    if(g->profile != PS5TM_PROFILE_BALANCED && g->profile != PS5TM_PROFILE_COOL)
      g->profile = PS5TM_PROFILE_COMFORT;
  }

  if(cfg->curve_len > PS5TM_CURVE_MAX) cfg->curve_len = PS5TM_CURVE_MAX;
  for(unsigned i = 0; i < cfg->curve_len; i++) {
    cfg->curve[i].temperature_c = clampu(cfg->curve[i].temperature_c,
                                         PS5TM_THRESHOLD_MIN_C,
                                         PS5TM_THRESHOLD_MAX_C);
    cfg->curve[i].duty_pct = clampu(cfg->curve[i].duty_pct, 0, 100);
  }
  /* The curve is interpolated left-to-right; keep it monotonic in temp. */
  if(cfg->curve_len) qsort(cfg->curve, cfg->curve_len,
                           sizeof(cfg->curve[0]), curve_cmp);

  /* The UI enforces >= 2 points; rebuild a usable ramp if a client sent junk. */
  if(cfg->curve_len < 2) {
    ps5tm_config_t tmp;
    ps5tm_config_defaults(&tmp);
    cfg->curve_len = tmp.curve_len;
    memcpy(cfg->curve, tmp.curve, sizeof(tmp.curve));
  }
}


int
ps5tm_config_load(void) {
  ps5tm_config_lock();
  ps5tm_config_defaults(&g_config);
  g_revert_pending = 0;               /* a timed probe session never outlives a load */

  int migrated = 0;
  FILE *f = fopen(PS5TM_CONFIG_PATH, "r");
  if(!f) {
    /* The data directory has been renamed twice; read the old locations once
       so an existing install does not quietly fall back to defaults. Newest
       first, because a console may well carry both. */
    static const char *legacy[] = {
      PS5TM_LEGACY_DATA_DIR "/config.json",
      PS5TM_LEGACY_CONFIG_PATH,
    };
    for(unsigned i = 0; i < sizeof(legacy) / sizeof(legacy[0]) && !f; i++)
      f = fopen(legacy[i], "r");
    migrated = (f != NULL);
  }
  if(!f) {
    ps5tm_config_unlock();
    PS5TM_INFO("config_defaults",
               "Keine Konfiguration gefunden – Standardwerte werden verwendet.");
    return -1;
  }

  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  if(n <= 0 || n > 65536) {
    fclose(f);
    ps5tm_config_unlock();
    PS5TM_WARN("config_invalid", "Konfigurationsdatei hat eine ungültige Größe.");
    return -1;
  }

  char *buf = malloc((size_t)n + 1);
  if(!buf) {
    fclose(f);
    ps5tm_config_unlock();
    return -1;
  }
  size_t got = fread(buf, 1, (size_t)n, f);
  buf[got] = 0;
  fclose(f);

  cJSON *root = cJSON_Parse(buf);
  free(buf);
  if(!root) {
    ps5tm_config_unlock();
    PS5TM_WARN("config_parse_failed",
               "Konfiguration konnte nicht gelesen werden – Standardwerte aktiv.");
    return -1;
  }

  const cJSON *it;
  if(cJSON_IsString(it = cJSON_GetObjectItem(root, "mode")))
    g_config.mode = strcmp(it->valuestring, "automatic") == 0
                      ? PS5TM_MODE_AUTOMATIC : PS5TM_MODE_OBSERVE;
  if(cJSON_IsNumber(it = cJSON_GetObjectItem(root, "fan_threshold_c")))
    g_config.fan_threshold_c = ps5tm_num_u32(it->valuedouble);
  if(cJSON_IsNumber(it = cJSON_GetObjectItem(root, "fan_reapply_sec")))
    g_config.fan_reapply_sec = ps5tm_num_u32(it->valuedouble);
  if(cJSON_IsNumber(it = cJSON_GetObjectItem(root, "warning_cpu_c")))
    g_config.warning_cpu_c = ps5tm_num_u32(it->valuedouble);
  if(cJSON_IsNumber(it = cJSON_GetObjectItem(root, "warning_soc_c")))
    g_config.warning_soc_c = ps5tm_num_u32(it->valuedouble);
  if(cJSON_IsNumber(it = cJSON_GetObjectItem(root, "http_port")))
    g_config.http_port = ps5tm_num_u32(it->valuedouble);
  if(cJSON_IsNumber(it = cJSON_GetObjectItem(root, "probe_mask")))
    g_config.probe_mask = ps5tm_num_u32(it->valuedouble);
  if(cJSON_IsString(it = cJSON_GetObjectItem(root, "bind_address")))
    snprintf(g_config.bind_address, sizeof(g_config.bind_address),
             "%s", it->valuestring);

  if(cJSON_IsNumber(it = cJSON_GetObjectItem(root, "target_temp_c")))
    g_config.target_temp_c = ps5tm_num_u32(it->valuedouble);
  /* control_band_c, deadband_c, control_interval_s, average_window_s and
     max_step_pct are written to the file but never read back: the profile owns
     them, and ps5tm_config_clamp() overwrites all five unconditionally a few
     lines after this parse. Reading them looked like hand-editing the file
     would work. It never did. They stay in the file because it is useful to
     see what the profile chose. */
  if(cJSON_IsNumber(it = cJSON_GetObjectItem(root, "safety_temp_c")))
    g_config.safety_temp_c = ps5tm_num_u32(it->valuedouble);
  if(cJSON_IsNumber(it = cJSON_GetObjectItem(root, "warning_countdown_s")))
    g_config.warning_countdown_s = ps5tm_num_u32(it->valuedouble);
  if(cJSON_IsNumber(it = cJSON_GetObjectItem(root, "telemetry_retention_days")))
    g_config.telemetry_retention_days = ps5tm_num_u32(it->valuedouble);
  if(cJSON_IsNumber(it = cJSON_GetObjectItem(root, "lightbar_enabled")))
    g_config.lightbar_enabled = ps5tm_num_u32(it->valuedouble);
  if(cJSON_IsNumber(it = cJSON_GetObjectItem(root, "ps_button_status")))
    g_config.ps_button_status = ps5tm_num_u32(it->valuedouble);
  if(cJSON_IsNumber(it = cJSON_GetObjectItem(root, "library_cache")))
    g_config.library_cache = ps5tm_num_u32(it->valuedouble);
  if(cJSON_IsNumber(it = cJSON_GetObjectItem(root, "lightbar_warn_c")))
    g_config.lightbar_warn_c = ps5tm_num_u32(it->valuedouble);
  if(cJSON_IsNumber(it = cJSON_GetObjectItem(root, "lightbar_hot_c")))
    g_config.lightbar_hot_c = ps5tm_num_u32(it->valuedouble);
  if(cJSON_IsString(it = cJSON_GetObjectItem(root, "profile"))) {
    g_config.profile = !strcmp(it->valuestring, "cool")     ? PS5TM_PROFILE_COOL
                     : !strcmp(it->valuestring, "balanced") ? PS5TM_PROFILE_BALANCED
                     : PS5TM_PROFILE_COMFORT;
  }

  const cJSON *rules = cJSON_GetObjectItem(root, "game_rules");
  if(cJSON_IsArray(rules)) {
    unsigned n = 0;
    const cJSON *r;
    cJSON_ArrayForEach(r, rules) {
      if(n >= PS5TM_MAX_GAME_RULES) break;
      const cJSON *tid = cJSON_GetObjectItem(r, "title_id");
      if(!cJSON_IsString(tid) || !tid->valuestring[0]) continue;

      ps5tm_game_rule_t *g = &g_config.game_rules[n];
      memset(g, 0, sizeof(*g));
      snprintf(g->title_id, sizeof(g->title_id), "%s", tid->valuestring);

      const cJSON *nm = cJSON_GetObjectItem(r, "title_name");
      if(cJSON_IsString(nm))
        ps5tm_copy_utf8(g->title_name, sizeof(g->title_name), nm->valuestring);

      const cJSON *tt = cJSON_GetObjectItem(r, "target_temp_c");
      g->target_temp_c = cJSON_IsNumber(tt) ? ps5tm_num_u32(tt->valuedouble) : 66;

      const cJSON *pf = cJSON_GetObjectItem(r, "profile");
      g->profile = (cJSON_IsString(pf) && !strcmp(pf->valuestring, "cool"))
                     ? PS5TM_PROFILE_COOL
                 : (cJSON_IsString(pf) && !strcmp(pf->valuestring, "balanced"))
                     ? PS5TM_PROFILE_BALANCED
                     : PS5TM_PROFILE_COMFORT;
      n++;
    }
    g_config.game_rule_count = n;
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
      g_config.curve[len].temperature_c = ps5tm_num_u32(t->valuedouble);
      g_config.curve[len].duty_pct      = ps5tm_num_u32(d->valuedouble);
      len++;
    }
    if(len) g_config.curve_len = len;
  }

  /* Schema 1 defaulted to 8080 and walked upwards when that was busy, so an
     older config can be sitting on any of 8080-8089. The tile's deeplink is
     fixed, so those installs are moved onto the new port; a port set to
     anything else was chosen deliberately and is left alone. */
  int old_schema = cJSON_IsNumber(it = cJSON_GetObjectItem(root,
                                                           "schema_version"))
                     ? (ps5tm_num_u32(it->valuedouble) < PS5TM_SCHEMA_VERSION) : 1;

  /* Every default this app has ever shipped, plus the nine fallback ports
   * each of them could walk to. A stored port inside one of these windows was
   * picked by an older build, not by a person, so it follows the tile to the
   * current default. Anything else was chosen deliberately and is left alone.
   *
   * The current default is in the list for its own window, not for itself: a
   * stored 8087–8095 can only have come from the fallback that used to walk
   * upwards on a collision. On the test console it did exactly that and saved
   * the result, so the tile pointed at a dead port from then on. The fallback
   * is gone (see main.c); this clears up what it left behind. */
  static const unsigned old_defaults[] = { 8080, 8770, PS5TM_DEFAULT_HTTP_PORT };
  int from_old_default = 0;
  for(unsigned i = 0; i < sizeof(old_defaults) / sizeof(old_defaults[0]); i++) {
    if(g_config.http_port >= old_defaults[i] &&
       g_config.http_port <= old_defaults[i] + 9) { from_old_default = 1; break; }
  }

  if(old_schema && from_old_default &&
     g_config.http_port != PS5TM_DEFAULT_HTTP_PORT) {
    PS5TM_INFO("config_port_migrated",
               "Web-UI wechselt von Port %u auf %u — der Startmenü-Kachel "
               "liegt diese Adresse fest zugrunde.",
               g_config.http_port, PS5TM_DEFAULT_HTTP_PORT);
    g_config.http_port = PS5TM_DEFAULT_HTTP_PORT;
  }

  cJSON_Delete(root);
  ps5tm_config_clamp(&g_config);          /* stamps the current schema version */
  ps5tm_config_unlock();

  /* Write back whenever the file on disk no longer matches what we just
     decided — otherwise the same migration runs again at every start and the
     stored port keeps disagreeing with the one actually in use. */
  if(migrated || old_schema) {
    ps5tm_config_save();
    if(migrated)
      PS5TM_INFO("config_migrated",
                 "Einstellungen aus der Vorgängerversion übernommen.");
  }

  PS5TM_INFO("config_loaded",
             "Einstellungen geladen: %s, Ziel %u °C, Profil %s.",
             g_config.mode == PS5TM_MODE_AUTOMATIC ? "Automatik" : "Beobachten",
             g_config.target_temp_c,
             g_config.profile == PS5TM_PROFILE_COOL     ? "kühl"
             : g_config.profile == PS5TM_PROFILE_BALANCED ? "ausgewogen"
                                                          : "leise");
  return 0;
}


/* A JSON string literal, quotes included. The names of titles are free text —
   a quote or a backslash in one used to leave a file that no longer parsed,
   and an unparsable settings file loads as "defaults": every setting gone. */
static void
put_json_string(FILE *f, const char *s) {
  fputc('"', f);
  for(; s && *s; s++) {
    unsigned char c = (unsigned char)*s;
    if(c == '"' || c == '\\')  { fputc('\\', f); fputc(c, f); }
    else if(c < 0x20)          fprintf(f, "\\u%04x", c);
    else                       fputc(c, f);
  }
  fputc('"', f);
}


/* One writer at a time, and in the order the snapshots were taken: the file
   must end up holding the newest state, never an older one that happened to
   finish its write last. */
static pthread_mutex_t g_save_lock = PTHREAD_MUTEX_INITIALIZER;

int
ps5tm_config_save(void) {
  pthread_mutex_lock(&g_save_lock);

  /* The snapshot is the only part that needs the config lock. The disk work
     below can wait for seconds behind a copy job's writes, and the fan worker
     takes that lock several times a second. */
  ps5tm_config_lock();
  ps5tm_config_clamp(&g_config);
  ps5tm_config_t c = g_config;
  if(g_revert_pending) c.probe_mask = g_revert_mask;   /* a loan stays off the disk */
  ps5tm_config_unlock();

  mkdir(PS5TM_DATA_DIR, 0755);

  char tmp_path[sizeof(PS5TM_CONFIG_PATH) + 8];
  snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", PS5TM_CONFIG_PATH);

  FILE *f = fopen(tmp_path, "w");
  if(!f) {
    int eno = errno;
    pthread_mutex_unlock(&g_save_lock);
    PS5TM_ERROR("config_save_failed",
                "Konfiguration konnte nicht geschrieben werden (errno %d).", eno);
    return -1;
  }

  fprintf(f,
          "{\n"
          "  \"schema_version\": %u,\n"
          "  \"mode\": \"%s\",\n"
          "  \"fan_threshold_c\": %u,\n"
          "  \"fan_reapply_sec\": %u,\n"
          "  \"warning_cpu_c\": %u,\n"
          "  \"warning_soc_c\": %u,\n"
          "  \"http_port\": %u,\n"
          "  \"bind_address\": ",
          c.schema_version,
          c.mode == PS5TM_MODE_AUTOMATIC ? "automatic" : "observe",
          c.fan_threshold_c,
          c.fan_reapply_sec,
          c.warning_cpu_c,
          c.warning_soc_c, c.http_port);
  put_json_string(f, c.bind_address);
  fprintf(f,
          ",\n"
          "  \"probe_mask\": %u,\n"
          "  \"target_temp_c\": %u,\n"
          "  \"control_band_c\": %u,\n"
          "  \"deadband_c\": %u,\n"
          "  \"control_interval_s\": %u,\n"
          "  \"average_window_s\": %u,\n"
          "  \"max_step_pct\": %u,\n"
          "  \"safety_temp_c\": %u,\n"
          "  \"warning_countdown_s\": %u,\n"
          "  \"telemetry_retention_days\": %u,\n"
          "  \"lightbar_enabled\": %u,\n"
          "  \"lightbar_warn_c\": %u,\n"
          "  \"lightbar_hot_c\": %u,\n"
          "  \"ps_button_status\": %u,\n"
          "  \"library_cache\": %u,\n"
          "  \"profile\": \"%s\",\n"
          "  \"curve\": [\n",
          c.probe_mask,
          c.target_temp_c, c.control_band_c, c.deadband_c,
          c.control_interval_s, c.average_window_s,
          c.max_step_pct, c.safety_temp_c,
          c.warning_countdown_s,
          c.telemetry_retention_days,
          c.lightbar_enabled,
          c.lightbar_warn_c,
          c.lightbar_hot_c,
          c.ps_button_status,
          c.library_cache,
          c.profile == PS5TM_PROFILE_COOL       ? "cool"
          : c.profile == PS5TM_PROFILE_BALANCED ? "balanced"
                                                : "comfort");

  for(unsigned i = 0; i < c.curve_len; i++) {
    fprintf(f, "    {\"temperature_c\": %u, \"duty_pct\": %u}%s\n",
            c.curve[i].temperature_c, c.curve[i].duty_pct,
            (i + 1 < c.curve_len) ? "," : "");
  }
  fprintf(f, "  ],\n  \"game_rules\": [\n");

  for(unsigned i = 0; i < c.game_rule_count; i++) {
    const ps5tm_game_rule_t *g = &c.game_rules[i];
    fprintf(f, "    {\"title_id\": ");
    put_json_string(f, g->title_id);
    fprintf(f, ", \"title_name\": ");
    put_json_string(f, g->title_name);
    fprintf(f, ", \"target_temp_c\": %u, \"profile\": \"%s\"}%s\n",
            g->target_temp_c,
            g->profile == PS5TM_PROFILE_COOL     ? "cool"
            : g->profile == PS5TM_PROFILE_BALANCED ? "balanced" : "comfort",
            (i + 1 < c.game_rule_count) ? "," : "");
  }
  fprintf(f, "  ]\n}\n");

  /* Every step can fail on its own: a full disk shows up in the stream's error
     flag, not necessarily in fflush(), and fclose() can still report a write
     that never reached the drive. Only a file that got all the way through is
     allowed to replace the old one — and it is synced first, because a console
     that is switched off right after a change would otherwise be left with a
     renamed but empty file, which loads as "defaults". */
  int ok  = (fflush(f) == 0 && !ferror(f) && fsync(fileno(f)) == 0);
  int eno = errno;
  if(fclose(f) != 0 && ok) { ok = 0; eno = errno; }
  if(ok && rename(tmp_path, PS5TM_CONFIG_PATH) != 0) { ok = 0; eno = errno; }

  if(!ok) {
    unlink(tmp_path);
    pthread_mutex_unlock(&g_save_lock);
    PS5TM_ERROR("config_save_failed",
                "Konfiguration konnte nicht ersetzt werden (errno %d).", eno);
    return -1;
  }

  pthread_mutex_unlock(&g_save_lock);
  return 0;
}
