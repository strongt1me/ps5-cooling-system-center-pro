/* PS5 Temperature Manager — shared types and module interfaces.
 *
 * Homebrew payload: reads the console's thermal sensors and drives the ICC
 * fan controller from a user-defined curve, with a web UI on the LAN.
 */
#ifndef PS5TM_H
#define PS5TM_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define PS5TM_APP_NAME    "PS5 Cooling & System Center - Pro"
#define PS5TM_APP_TAGLINE "Advanced Cooling & Thermal Management for PlayStation 5"

/* ⚠ The data directory below is NOT renamed with the app.
 *
 * It holds the settings, the reading history and the multi-week cooling-health
 * record. Renaming it would orphan all three: the legacy lookup migrates the
 * settings file only, so the history and the week counter would silently start
 * from zero — and the cooling verdict needs four weeks of ordinary use.
 *
 * A directory is a storage location, not a label. It has no reason to follow a
 * display name. */

#define PS5TM_VERSION        "1.59.0"
/* Raised to 4 so the port migration below runs once more: the removed
   fallback had written its own choice into the config, and that value would
   otherwise outlive the code that made it. */
#define PS5TM_SCHEMA_VERSION 4

/* The home-screen tile deeplinks to http://127.0.0.1:<this>/, and that URL is
   baked into the PKG at build time, so the app and the tile have to agree.
   Chosen by the user; pkg/tile/sce_sys/param.json carries the same number. */
#define PS5TM_DEFAULT_HTTP_PORT 8086

/* The address baked into the home-screen tile's param.json as its deeplink.
   It cannot follow a changed setting — the package would have to be rebuilt
   and reinstalled — so a port that differs from this one silently costs the
   tile. The app says so rather than letting it be discovered by clicking. */
#define PS5TM_TILE_DEEPLINK_PORT 8086

/* Overridable so the host test harness can run against a scratch directory.
   No spaces on purpose: this path gets typed into FTP clients, shells and
   file managers, and a quoted path is one more thing to get wrong. */
#ifndef PS5TM_DATA_DIR
#define PS5TM_DATA_DIR    "/data/PS5-Cooling-Center"
#endif
#define PS5TM_CONFIG_PATH PS5TM_DATA_DIR "/config.json"

/* Earlier homes of the settings file, newest first. They are read once, in
   order, so an existing install keeps its configuration across a rename
   instead of silently reverting to defaults. */
#ifndef PS5TM_LEGACY_DATA_DIR
#define PS5TM_LEGACY_DATA_DIR    "/data/PS5 Cooling Center"
#endif
#define PS5TM_LEGACY_CONFIG_PATH "/data/ps5-temperature-manager/config.json"

/* Threshold range the app itself uses (the controller, the pinned value of the
   observe mode, the curve). The top is 91 °C since 07.10.2026 (it was 80): that
   is the value the firmware sets itself at every change of state (measured
   24.09.2026, FW 12.00) and the highest one ShadowMountPlus offers for its
   fan_target_temperature (50..91), so the hardware takes it. 91 is "as quiet
   as the console is without this app". Below 45 the fan is pointlessly loud. */
#define PS5TM_THRESHOLD_MIN_C 45
#define PS5TM_THRESHOLD_MAX_C 91

/* The top of the range up to 1.49.x. Where the threshold rests for targets up
   to 70 °C (see rest_threshold_c in fan.c) and the anchor of the rough
   duty <-> threshold seeds, which the servo corrects anyway; keeping both where
   they were leaves the behaviour for the usual targets exactly as tested. */
#define PS5TM_THRESHOLD_BASE_C 80

/* A fan that has nothing to do rests this far above the target (limited to
   PS5TM_THRESHOLD_BASE_C..PS5TM_THRESHOLD_MAX_C): the firmware raises the fan
   in proportion to how far the die is above the threshold and keeps a little
   extra speed within about 20 °C below it, so a resting threshold just at the
   target would never be as quiet as the console can be. */
#define PS5TM_REST_ABOVE_TARGET_C 10

/* Size of the block both ICC fan ioctls exchange, and where the threshold
   sits inside it. Not a guess: the command number encodes the size
   (0xC01C8F07/08, length field 0x1C = 28), and the offset is confirmed from
   both directions — written there since the beginning, read back as 0x50 = 80
   on FW 12.00 on 07.09.2026. Declared here rather than next to the platform
   functions because the channel recording sizes a buffer with it. */
#define PS5TM_ICC_FAN_CONFIG_SIZE   28
#define PS5TM_ICC_FAN_TARGET_OFFSET 5

#define PS5TM_CURVE_MAX 8

/* Re-apply the threshold at least this often: the firmware resets ICC fan
   state at every change of state — a game starting or ending, rest mode —
   silently dropping our setting (to 91 °C on FW 12.00, measured 24.09.2026).
   Only used where the controller cannot be read back; see fan.c. */
#define PS5TM_REAPPLY_SEC 15
#define PS5TM_REAPPLY_MIN_SEC 1
#define PS5TM_REAPPLY_MAX_SEC 300

/* Written once when automatic control is switched off.
 *
 * Simply stopping would leave whatever the controller last set in the ICC
 * register — the firmware does not restore anything by itself before its next
 * change of state, so switching off during heavy cooling would keep the fan
 * loud until then.
 *
 * The value handed back is the firmware's own. Measured 24.09.2026 on FW
 * 12.00: at every change of state — a game starting or ending, rest mode in
 * either direction — the firmware wrote 91 °C, the same all eleven times.
 * Until 1.43.1 this was 65 °C, the "stock-like" figure of the reference
 * payloads, which is a good deal cooler and louder than the console left to
 * itself. Handing back 91 leaves it exactly where it would be without the
 * app. Note that it lies above PS5TM_THRESHOLD_MAX_C on purpose: that is the
 * limit for the controller and the UI, and this is not a setting of ours. */
#define PS5TM_NEUTRAL_THRESHOLD_C 91

/* Optional probes, off unless switched on. */
#define PS5TM_PROBE_GAME    0x01u   /* shell services: which title is running */
#define PS5TM_PROBE_PAD     0x02u   /* controller battery                     */
#define PS5TM_PROBE_NETDISP 0x04u   /* network link and connected display     */
#define PS5TM_PROBE_RISKY   0x08u   /* optional high-risk sensor extras       */
#define PS5TM_PROBE_DRIVE   0x10u   /* optional drive telemetry               */
#define PS5TM_PROBE_FPS     0x20u   /* frame counter of the display engine    */


/* JSON numbers arrive as doubles, and converting a double that does not fit
 * the integer type is undefined behaviour in C: {"http_port":1e30}, or a plain
 * -5 into an unsigned field, was exactly that — the sanitizer build aborts on
 * it, and on the console the result was whatever the CPU instruction happened
 * to leave behind. These saturate instead, and read NaN as 0. Whether the
 * value is *sensible* stays with the caller (ps5tm_config_clamp and friends);
 * this only guarantees the conversion itself is defined. */
static inline unsigned
ps5tm_num_u32(double v) {
  if(!(v > 0)) return 0;                      /* also catches NaN */
  if(v >= 4294967295.0) return 4294967295u;
  return (unsigned)v;
}

static inline int
ps5tm_num_i32(double v) {
  if(v != v) return 0;
  if(v <= -2147483648.0) return INT32_MIN;
  if(v >= 2147483647.0)  return INT32_MAX;
  return (int)v;
}

/* snprintf("%s") that never cuts a multi-byte character in half: a title in
   Japanese is three bytes a letter, and a name cut in the middle of one is no
   longer valid UTF-8 for the file it is written to or the page that shows it. */
static inline void
ps5tm_copy_utf8(char *dst, size_t size, const char *src) {
  if(!size) return;
  size_t n = 0;
  while(src[n] && n + 1 < size) n++;
  /* src[n] is the first byte that did not fit; a continuation byte there means
     the letter it belongs to would be cut — back up to where it starts. */
  while(n > 0 && src[n] && ((unsigned char)src[n] & 0xC0) == 0x80) n--;
  memcpy(dst, src, n);
  dst[n] = 0;
}

/* True when some component of the path is "..". A plain substring test also
   throws out honest names such as "Edition... Remastered", and the title then
   quietly loses its storage details. */
static inline int
ps5tm_path_has_dotdot(const char *p) {
  size_t i = 0;
  while(p[i]) {
    size_t s = i;
    while(p[i] && p[i] != '/') i++;
    if(i - s == 2 && p[s] == '.' && p[s + 1] == '.') return 1;
    if(p[i]) i++;
  }
  return 0;
}


/* ---------------------------------------------------------------- config */

typedef enum {
  PS5TM_MODE_OBSERVE   = 0,  /* monitor only, never touch the fan */
  PS5TM_MODE_AUTOMATIC = 1   /* hold the target temperature */
} ps5tm_mode_t;

/* Presets that scale how eagerly the controller reacts. */
typedef enum {
  PS5TM_PROFILE_COMFORT  = 0,  /* quietest; slowest to react */
  PS5TM_PROFILE_BALANCED = 1,
  PS5TM_PROFILE_COOL     = 2   /* coolest; still gradual, never abrupt */
} ps5tm_profile_t;

/* Bounds for the user-settable control parameters.
 *
 * The upper bound was 72 until 03.10.2026, when the owner asked for 78, and
 * 78 until 07.10.2026, when he asked for 91 like ShadowMountPlus (a quieter
 * console that runs warmer; 91 is the firmware's own value, see
 * PS5TM_THRESHOLD_MAX_C). Nothing else moves with it by itself — the safety
 * limit follows (target + 4, see ps5tm_config_clamp), while the warning limit
 * (warning_cpu_c, 80 °C unless changed) stays where it is and so lies at or
 * below a high target. */
#define PS5TM_TARGET_MIN_C   60
#define PS5TM_TARGET_MAX_C   91

/* The proportional band: how many degrees above the target the fan needs to
 * go from its floor to full.
 *
 * This is the setting that decides whether the console is bearable. The
 * controller used to have no such notion — it simply added a little speed
 * every cycle for as long as the console was warmer than the target. Against
 * a target the hardware can reach that settles; against one it cannot — 60 °C
 * while a game holds 72 °C — it never stops climbing and ends at full speed,
 * permanently. Classic integral wind-up.
 *
 * With a band the answer is a plain function of temperature: at the target
 * the fan idles, `band` degrees above it runs flat out, and in between it
 * scales linearly. An unreachable target then costs a known, visible fan
 * speed instead of everything the console has. Wide is quiet: 20 °C means a
 * target of 60 reaches full only at 80. */
#define PS5TM_BAND_MIN_C      5
#define PS5TM_BAND_MAX_C     30
#define PS5TM_SAFETY_MIN_C   72
#define PS5TM_SAFETY_MAX_C   95    /* target 91 + 4 */

typedef struct {
  unsigned temperature_c;
  unsigned duty_pct;
} ps5tm_curve_point_t;

/* A saved setting for one title, applied automatically while it runs. */
#define PS5TM_MAX_GAME_RULES 16

typedef struct {
  char            title_id[16];
  char            title_name[64];   /* only so the UI can label the entry */
  unsigned        target_temp_c;
  ps5tm_profile_t profile;
} ps5tm_game_rule_t;

typedef struct {
  unsigned            schema_version;
  ps5tm_mode_t        mode;
  unsigned            fan_threshold_c;   /* manual pin, used in observe mode */
   unsigned            fan_reapply_sec;   /* periodic re-send interval (1..300) */
  unsigned            warning_cpu_c;
  unsigned            warning_soc_c;
  unsigned            http_port;
  char                bind_address[64];
  ps5tm_curve_point_t curve[PS5TM_CURVE_MAX];
  unsigned            curve_len;

  /* --- comfort controller ------------------------------------------- */
  unsigned        target_temp_c;       /* temperature to hold  (60..91)  */
  unsigned        control_band_c;      /* degrees above target = full fan */
  unsigned        deadband_c;          /* no action within +/- this      */
  unsigned        control_interval_s;  /* how often the fan may change   */
  unsigned        average_window_s;    /* moving-average length          */
  unsigned        max_step_pct;        /* fan change per control cycle   */
  unsigned        safety_temp_c;       /* above this, comfort is dropped */
  ps5tm_profile_t profile;

  /* Temperature warning escalation helper.
     When the hottest valid sensor stays above a warning limit for this long,
     the UI can surface a countdown and stronger guidance. */
  unsigned        warning_countdown_s;

  /* Long-term telemetry retention for on-disk history files. */
  unsigned        telemetry_retention_days;

  /* Controller lightbar feedback (best effort, may be unavailable on a
     payload context depending on firmware and permissions). */
  unsigned        lightbar_enabled;
  unsigned        lightbar_warn_c;
  unsigned        lightbar_hot_c;

  /* Processor temperature and fan speed as a system notification when the
     controller's microphone button is pressed twice in quick succession (at
     most every 5 s) — see micbutton.c. The key keeps its name from 1.45.x,
     when the PS button was the trigger: config files and the API stay
     compatible, and an "off" saved then stays off. */
  unsigned        ps_button_status;

  /* "Covers & Metadaten speichern" on the games page: the list keeps covers and the
     results of its slow checks in /data/PS5-Cooling-Center/covers_and_more and uses
     them at the next scan — see libcache.c. Off until someone switches it on. */
  unsigned        library_cache;

  /* Which optional probes may run — see probe.c. Zero is the safe state and
     the default; each bit is switched on individually while hunting for the
     one that freezes the console. */
  unsigned        probe_mask;

  /* Per-title overrides. When the running game matches, its target and
     profile replace the global ones until it stops. */
  ps5tm_game_rule_t game_rules[PS5TM_MAX_GAME_RULES];
  unsigned          game_rule_count;
} ps5tm_config_t;

extern ps5tm_config_t g_config;

void ps5tm_config_defaults(ps5tm_config_t *cfg);
void ps5tm_config_clamp(ps5tm_config_t *cfg);
/* True for a dotted-quad IPv4 address — the only thing bind_address can be. */
int  ps5tm_config_bind_valid(const char *s);
int  ps5tm_config_load(void);
/* Writes the settings file. Must NOT be called with the config lock held: it
   takes a snapshot under the lock and does the disk I/O — which can wait for
   seconds while a copy job has the drive busy — outside it, so that the fan
   worker, which takes the lock several times a second, never queues behind a
   write. Change g_config under the lock, release it, then save. */
int  ps5tm_config_save(void);
void ps5tm_config_lock(void);
void ps5tm_config_unlock(void);

/* A timed probe session ("Diagnose 2/5/10 Min."): the new mask is in force in
   memory at once, the PREVIOUS one is what reaches the disk, and ps5tm_config_
   tick() puts the previous one back when the time is up — with the page gone,
   the browser closed, or the payload restarted in between (then simply by the
   file). A plain change of probe_mask cancels it. */
void ps5tm_config_probe_revert_arm(unsigned previous_mask, unsigned seconds);
void ps5tm_config_probe_revert_cancel(void);
/* 1 while a revert is pending, with the mask that will come back and the
   seconds left. */
int  ps5tm_config_probe_revert_state(unsigned *mask, unsigned *seconds_left);
void ps5tm_config_tick(void);              /* about once a second */


/* --------------------------------------------------------------- logging */

#define PS5TM_LOG_CAPACITY 256

typedef struct {
  uint64_t timestamp_ms;
  char     level[8];    /* INFO / WARN / ERROR */
  char     code[48];
  char     message[192];
} ps5tm_log_entry_t;

void ps5tm_log_init(void);
void ps5tm_log(const char *level, const char *code, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));
/* Copies at most `max` entries (oldest first) into `out`; returns the count. */
unsigned ps5tm_log_snapshot(ps5tm_log_entry_t *out, unsigned max);

/* Serializes fragile Sony API calls across multiple worker/request threads. */
void ps5tm_sony_api_lock(void);
/* The same lock without waiting: 1 when taken (release it with ..._unlock), 0
   when another thread holds it. The fan worker only ever uses this one — a
   thread that queues for the lock waits as long as whoever holds it sits in a
   Sony service. */
int  ps5tm_sony_api_trylock(void);
void ps5tm_sony_api_unlock(void);

#define PS5TM_INFO(code, ...)  ps5tm_log("INFO",  code, __VA_ARGS__)
#define PS5TM_WARN(code, ...)  ps5tm_log("WARN",  code, __VA_ARGS__)
#define PS5TM_ERROR(code, ...) ps5tm_log("ERROR", code, __VA_ARGS__)

/* Wall-clock milliseconds: for timestamps a person reads. */
uint64_t ps5tm_now_ms(void);
/* Milliseconds on a clock that only runs forward. For anything measured as a
   difference — deadlines, TTLs, a job's elapsed time: the wall clock steps
   when the console syncs its time, and a difference taken across such a step
   is garbage (as an unsigned value, an enormous one). */
uint64_t ps5tm_mono_ms(void);


/* ---------------------------------------------------------- notifications */

/* Shows a toast on the console itself. */
void ps5tm_notify(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
/* A toast with one button that opens action_url (notify.c). 0 = accepted. */
int  ps5tm_notify_action(const char *message, const char *sub,
                         const char *icon_path, const char *action_name,
                         const char *action_url);
/* Writes the console's LAN address into `out`; returns 0 when one was found. */
int  ps5tm_local_ip(char *out, size_t out_len);
/* The text (up to 106 bytes) as an SVG picture of its QR code (qr.c). 0, or -1 when it is too long or out too small. */
int  ps5tm_qr_svg(const char *text, char *out, size_t out_size);


/* -------------------------------------------------------------- platform */

/* Adapter states — these strings are understood by the web UI. */
#define PS5TM_ADAPTER_READY        "ready"
#define PS5TM_ADAPTER_UNAVAILABLE  "unavailable"
#define PS5TM_ADAPTER_FAULTED      "faulted"

#define PS5TM_SOC_CHANNELS 8
#define PS5TM_MAX_CORES    8    /* physical cores */
#define PS5TM_MAX_CPUS     16   /* logical CPUs, two per core */

/* The SoC's own power telemetry, read through sceKernelGetSocPowerConsumption
   — see telemetry.c. Eight rails of {mW, mV, mA}. */
#define PS5TM_POWER_RAILS 8

typedef struct {
  int      valid;
  uint32_t mw[PS5TM_POWER_RAILS];
  uint32_t mv[PS5TM_POWER_RAILS];
  uint32_t ma[PS5TM_POWER_RAILS];
  /* The words after the rails. Recorded, not interpreted: the only public
     reading of them carries a question mark on every name. */
  uint8_t  aux_c[4];
  uint32_t aux_word[3];
  uint64_t sampled_ms;
  /* The CPU rails (2 and 3) are a measurement only if they move. On this
     PS5 Pro they did not: 111.4 W for a full hour of play while the GPU rail
     changed at nearly every sample (27.09.2026). Set once they have changed
     three times within a minute — see telemetry.c. */
  int      cpu_live;
} ps5tm_power_t;

/* Live clocks from sceKernelGetSocClock / sceKernelGetCpuCoreClock, as
   opposed to the power-mode table in clocks.c, which only changes when the
   mode does. */
#define PS5TM_SOC_CLOCK_DOMAINS 26

typedef struct {
  int      valid;
  int      gfx_mhz;          /* domain 20 */
  int      gfx_limit_mhz;    /* domain 25 */
  int      uclk_mhz;         /* domain 23 */
  int      fclk_mhz;         /* domain 24 */
  uint32_t domain[PS5TM_SOC_CLOCK_DOMAINS];
  int      cores_valid;
  int      core_mhz[PS5TM_MAX_CORES];
  uint64_t sampled_ms;
} ps5tm_live_clocks_t;

/* Frames per second from the display engine's flip counter (/dev/dce). */
typedef enum {
  PS5TM_FPS_OFF         = 0,   /* probe switched off or not yet tried */
  PS5TM_FPS_OK          = 1,
  PS5TM_FPS_REFUSED     = 2,   /* the device would not open for us */
  PS5TM_FPS_IOCTL_ERROR = 3,
  PS5TM_FPS_NO_GAME     = 4    /* switched on, but no game in front to count */
} ps5tm_fps_state_t;

typedef struct {
  int      valid;            /* a rate over a full interval exists */
  double   fps;
  uint64_t flips;            /* the raw counter */
  int      state;            /* ps5tm_fps_state_t */
  int      last_errno;
} ps5tm_fps_t;

typedef struct {
  int cpu_c,  cpu_valid;
  int soc_c,  soc_valid;    /* hottest of SoC channels 1..7, see platform.c */
  /* Channel 7, the sensor nearest the graphics unit. Read on every console,
     but measured only on a PS5 Pro, so the API names it only there — see
     ps5tm_sysinfo_gpu_channel_known(). Channels 0-2 carried names until
     1.45.1 ("damped", power supply, M.2); the same measurement refuted all
     three, and they are raw channels like the rest now. */
  int gpu_c,  gpu_valid;

  /* Every SoC channel, for the diagnostics view. */
  int soc_raw[PS5TM_SOC_CHANNELS];
  int soc_raw_valid[PS5TM_SOC_CHANNELS];

  /* Mean over every logical CPU that could be measured — all sixteen since
     1.45.0, only 0..7 before. */
  double cpu_load_pct; int cpu_load_valid;
  /* Physical cores: the mean of each core's two logical CPUs. */
  int    core_pct[PS5TM_MAX_CORES];
  int    core_count;

  /* Logical CPUs 0..15, -1 where not measured. The system's share is the set
     this process may run on (0xea00 on FW 12.00: 9, 11, 13, 14, 15); games
     get the rest. */
  int      cpu_pct[PS5TM_MAX_CPUS];
  int      cpu_count;
  unsigned system_cpu_mask;
  double   game_load_pct;   int game_load_valid;
  double   system_load_pct; int system_load_valid;
  /* The pre-1.45.0 figure, the mean of logical CPUs 0..7. Only the long-term
     cooling record uses it: its operating window is defined in these units,
     and weeks already on disk were counted with them. */
  double   cpu_load_legacy_pct; int cpu_load_legacy_valid;

  long   cpu_mhz;      int cpu_mhz_valid;
  int    cpu_mode;     int cpu_mode_valid;
  /* Sum of all power rails, kept under its old name for existing readers. */
  double soc_power_w;  int soc_power_valid;

  ps5tm_power_t       power;
  ps5tm_live_clocks_t live_clocks;
  ps5tm_fps_t         fps;

  /* The PS5 exposes no GPU utilisation counter; this stays invalid rather
     than being invented. */
  double gpu_load_pct; int gpu_load_valid;

  int fan_duty_pct;    int fan_duty_valid;
  /* Unscaled controller register. The unit is not documented — exposed raw
     so the reading can be interpreted instead of guessed at. */
  int fan_duty_raw;
} ps5tm_sensors_t;


/* ------------------------------------------------------------ system info
 * Slow-moving facts: identity, uptime and storage. Refreshed on a timer
 * rather than with every dashboard poll.
 */

/* Internal SSD, two M.2 slots and eight USB mount points — the most the
   console can present at once. The old limit of 6 silently truncated the
   list before the enumeration below even got that far. */
#define PS5TM_MAX_VOLUMES 12

typedef struct {
  char     label[40];
  char     path[40];
  char     device[64];   /* mount source, used to reject duplicates */
  uint64_t total_bytes;
  uint64_t used_bytes;
  uint64_t free_bytes;
} ps5tm_volume_t;

typedef struct {
  char     model[128];
  char     serial_masked[48];
  uint32_t firmware;

  uint64_t uptime_sec;          int uptime_valid;
  /* Betriebsstunden, Startzähler, Einschaltgrund, Sonys Übertemperatur-Melder,
     RAM/VRAM und die Gerätevariante standen hier. Alle am 31.07.2026 entfernt:
     die Aufrufe melden auf FW 12.00 Erfolg und schreiben nichts. Die Messung
     und der Grund stehen über den Symboldeklarationen in sysinfo.c. */

  ps5tm_volume_t volumes[PS5TM_MAX_VOLUMES];
  unsigned       volume_count;

  /* True only when an M.2 expansion drive is really mounted. Until 1.45.1
     this also gated SoC channel 2 as the drive's temperature; that channel
     turned out to sit on the chip (see platform.c), so no temperature is
     attributed to the drive any more. */
  int m2_present;

  /* Firmware as the system itself reports it, next to our own decode of the
     kernel word — when the two disagree, that is worth seeing. */
  char firmware_text[32];   int firmware_text_valid;

  /* The display the console is plugged into. */
  char tv_name[64];
  int  tv_width, tv_height;   int tv_resolution_valid;
  int  tv_refresh_hz;         int tv_refresh_valid;
  int  tv_hdr;                int tv_hdr_valid;

  /* Network, as far as the connection manager will tell us. */
  int  net_up;
  char net_ssid[36];          int net_wifi;      /* 0 = cable, 1 = wireless */
  char net_ip[16];
  int  net_rssi_pct;          int net_rssi_valid;
  int  net_link_mbit;         int net_link_valid;
  int  net_band_ghz10;        int net_band_valid; /* 24 or 50, in tenths     */
  int  net_nat_type;          int net_nat_valid;
} ps5tm_sysinfo_t;

/* Cached; recomputed at most once every few seconds. */
void ps5tm_sysinfo_get(ps5tm_sysinfo_t *out);
/* Background thread only: may block inside a system service. */
void ps5tm_sysinfo_refresh(void);
/* Whether SoC channel 7 may be called the graphics side on this model. */
int  ps5tm_sysinfo_gpu_channel_known(const ps5tm_sysinfo_t *info);

/* Fills the network and display fields above. Separate because it talks to
   two further system libraries that the rest of sysinfo does not need. */
void ps5tm_netdisp_fill(ps5tm_sysinfo_t *info);


/* ------------------------------------------------------------- game state */

typedef struct {
  char        title_id[16];    /* e.g. "PPSA01650"; empty when nothing runs */
  char        title_name[128]; /* display name, falls back to the id        */
  char        title_version[16];/* from the title's own metadata, may be "" */
  unsigned    app_id;
  unsigned    focus_id;
  int         focus_remembered;/* focus_id is from an earlier pass: its line
                                  has scrolled out of the kernel buffer      */
  int         foreground;      /* the game currently owns the controller    */
  int         suspended;       /* paused in the background                  */
  const char *state;           /* short label for the dashboard             */
  /* Which route produced this reading, so a wrong answer can be traced to
     its source instead of guessed at: "bigapp", "msgbuf" or "none". */
  const char *source;
} ps5tm_gamestate_t;

void ps5tm_gamestate_get(ps5tm_gamestate_t *out);
/* 1 once a refresh has produced a reading. Before that — and for good while
   the game probe is switched off — ps5tm_gamestate_get() answers "wird
   ermittelt" with foreground 0, which is not the same as "no game in front". */
int  ps5tm_gamestate_measured(void);
/* How long ago the last reading was taken, in milliseconds on the monotonic
   clock; UINT64_MAX before the first one. A game probe that is stuck leaves
   its last answer in place, and whoever acts on that answer has to be able to
   tell that it is old. */
uint64_t ps5tm_gamestate_age_ms(void);

/* The games on the home screen, read from the system's app database — see
   library.c. The JSON is the caller's to free; the cover path is one of the
   title's own metadata files, or the call fails. */
struct cJSON *ps5tm_library_json(void);
/* The list is stale the moment a job has moved, copied or converted something:
   the next ps5tm_library_json() reads again instead of waiting out its TTL. */
void          ps5tm_library_forget(void);
/* Image probe (library.c): a background thread that, a little after the start and then now and then, mounts each image game
   whose adaptations are unknown (ShadowMountPlus mounts images only while a game runs), reads them and releases the image
   again. probe_now() asks for a round at once; 0 when one was requested, -1 when the thread is not running. */
void          ps5tm_library_probe_start(void);
int           ps5tm_library_probe_now(void);
/* Every title id the app database lists as installed (tbl_contentinfo), read
   afresh. Writes up to `max` of them into `ids` and returns how many there are
   — more than `max` means the list is incomplete — or -1 when the database
   cannot be read, in which case nothing is known. */
int           ps5tm_library_installed_ids(char (*ids)[12], int max);
/* One title of the app database, for checking an installation: 1 when it is listed (ver gets the version its
   AppInfoJson names, "" when it names none), 0 when it is not, -1 when the database cannot be read. */
int           ps5tm_library_title_info(const char *title_id, char *ver, size_t ver_len);
/* Is a content id (a DLC's, for one) listed in the app database? 1, 0, or -1 when it cannot be read. */
int           ps5tm_library_content_listed(const char *content_id);
int           ps5tm_library_cover(const char *title_id, char *path,
                                  size_t path_len);
int           ps5tm_library_launch_info(const char *title_id,
                                        char *name, size_t name_len,
                                        char *link, size_t link_len);
/* Starts the title in front, as the Homebrew Launcher does. >= 0 is the new
   app id; < 0 the launcher's refusal (0x80940010: already running). */
int           ps5tm_library_start(const char *title_id);
/* After a start from the console's own browser: a background thread asks the
   shell to go home, which closes the browser, then brings the title forward
   through its tile link. close_browser 0 skips the first step. Returns 0
   when the thread was started. */
int           ps5tm_library_hand_over(const char *title_id, int close_browser);
/* The name and where a title's data really lies — a folder, or the image
   file ShadowMountPlus mounts — for a title on the list with either.
   Returns 0 on success. */
int           ps5tm_library_copy_info(const char *title_id,
                                      char *name, size_t name_len,
                                      char *source, size_t source_len);

/* For deleting a game (gamedelete.c): what the title is and where it lies. */
typedef struct {
  char    name[128];
  char    format[8];       /* as on the list; "pkg" is what the system installed */
  char    path[256];       /* the app folder, or the image/folder ShadowMountPlus mounts */
  char    version[16];
  int     smp;             /* ShadowMountPlus manages it */
  int64_t size;            /* bytes, -1 when not known */
} ps5tm_libdel_t;
/* 0 and *out filled for a title on the list. */
int           ps5tm_library_delete_info(const char *title_id, ps5tm_libdel_t *out);

/* Deleting a game or one of the app's backups (gamedelete.c). The plan names exactly what would go and hands out a
   token; only a start with that token (once, for ten minutes) deletes. Saved games are never touched. */
struct cJSON *ps5tm_gamedelete_plan(const char *kind, const char *id, int *http);
int           ps5tm_gamedelete_start(const char *kind, const char *id, const char *token, char *err, size_t err_len);
struct cJSON *ps5tm_gamedelete_status(void);
int           ps5tm_gamedelete_busy(void);
struct cJSON *ps5tm_gamedelete_backups_json(void);

/* The file manager (filemgr.c): 1 while it copies, moves or deletes. */
int           ps5tm_filemgr_busy(void);

/* ShadowMountPlus's own interface on 127.0.0.1:10101 — see smp.c. */
#define PS5TM_SMP_MAX_GAMES 256
typedef struct {
  char title_id[12];
  char path[256];          /* the real image file or folder             */
  char runtime_path[256];  /* where an image is mounted, else the same  */
  char source_type[12];    /* "folder" or "image"                       */
  char image_type[12];     /* "exfatfs", "ufs", "pfs", "pfsc"           */
  int  mounted;
  int  available;          /* the source is reachable                   */
} ps5tm_smp_game_t;

/* 0 and *out filled when ShadowMountPlus manages this title. */
int           ps5tm_smp_find(const char *title_id, ps5tm_smp_game_t *out);
/* 1 when it answers; its version into version. */
int           ps5tm_smp_available(char *version, size_t version_len);
/* 1 when it answers and lists this capability ("move_game_source", ...). */
int           ps5tm_smp_can(const char *capability);
/* Drops the cached list, after a job has moved something. */
void          ps5tm_smp_forget(void);
/* POSTs body to /api/v1<route>; the answer on "status": 0, else NULL with a
   reason in err. The caller frees the answer. */
struct cJSON *ps5tm_smp_call(const char *route, const struct cJSON *body,
                             int timeout_ms, char *err, size_t err_len);

/* Moving a game, or unpacking an image into a folder: done by ShadowMountPlus
   itself, planned and followed from here — see gamemove.c. op is "move" or
   "unpack". */
struct cJSON *ps5tm_gamemove_plan(const char *title_id, const char *op,
                                  char *err, size_t err_len);
int           ps5tm_gamemove_start(const char *title_id, const char *op,
                                   const char *dest_dir, int delete_source,
                                   char *err, size_t err_len);
struct cJSON *ps5tm_gamemove_status(char *err, size_t err_len);
int           ps5tm_gamemove_cancel(char *err, size_t err_len);

/* Converting a game on the console — see gameconvert.c: op "exfat" (folder
   to .exfat), "ffpkg" (folder to UFS2 .ffpkg) or "ffpfsc" (folder or image
   to .ffpfsc); mode "homebrew" or "backup" as for copying. start returns an
   HTTP status like copying. */
struct cJSON *ps5tm_gameconvert_plan(const char *title_id, char *err, size_t err_len);
int           ps5tm_gameconvert_start(const char *title_id, const char *op,
                                      const char *mount, const char *mode,
                                      char *err, size_t err_len);
struct cJSON *ps5tm_gameconvert_status(void);
int           ps5tm_gameconvert_cancel(void);

/* Copying a game folder or image file to another drive — see gamecopy.c. The
   plan measures the source (cached for a minute) and lists the drives it
   could go to; NULL with a reason in err when the title cannot be copied.
   start returns an HTTP status: 200 when the copy runs, else err says why. */
struct cJSON *ps5tm_gamecopy_plan(const char *title_id, char *err, size_t err_len);
int           ps5tm_gamecopy_start(const char *title_id, const char *mount,
                                   const char *mode, char *err, size_t err_len);
struct cJSON *ps5tm_gamecopy_status(void);
/* Size of a folder (walked) or file, and its largest file. 0 on success. */
int           ps5tm_gamecopy_measure(const char *path, uint64_t *bytes,
                                     uint64_t *largest);
/* Asks a running copy to stop; it removes what it wrote. -1 if none runs. */
int           ps5tm_gamecopy_cancel(void);

/* One file job at a time: a copy, a conversion and a ShadowMountPlus move or
   unpack share the drives' room, and a move deletes what the others read.
   Each of the three starts answers 409 while another of them runs. The _busy
   calls are for that check — 1 while the job is under way. ps5tm_gamemove_busy
   asks ShadowMountPlus (short timeout) and reads one that does not answer as
   idle. */
int           ps5tm_gamecopy_busy(void);
int           ps5tm_gameconvert_busy(void);
int           ps5tm_gamemove_busy(void);

/* While at least one long job holds it, the console's timer for automatic
   rest mode is reset every ten seconds — see powerguard.c. Every hold needs
   its release. */
void          ps5tm_powerguard_hold(void);
void          ps5tm_powerguard_release(void);

/* Background thread only: may block inside the shell services. */
void ps5tm_gamestate_refresh(void);

/* Starts the one thread allowed to call blocking system services. */
void ps5tm_probe_start(void);


/* -------------------------------------------------------------- dualsense */

typedef struct {
  int connected;
  int battery_pct;      int battery_valid;
  int charging, full;   int charging_valid;

  /* Where the status byte was found and what it held. Once a full and an
     empty controller have been read once, this pins the offset for good and
     the search in dualsense.c can become a constant. */
  int           status_offset;
  unsigned char status_byte;
  int           candidates;    /* >1 means the offset is not yet unambiguous */

  unsigned char raw[96];
  int           raw_len;
  int           probe_rc;

  /* Filled from the console's own kernel log rather than from scePad, which
     refuses a payload. Measured 0..100 as a straight percentage. See
     dualsense.c — including why an age is published alongside it. */
  int      from_log;
  int      restored_from_disk; /* 1 until the log provides a fresh runtime value */
  uint64_t battery_seen_ms;   /* when this reading first appeared */
  unsigned device_id;         /* changes on every connection, not an identity */

  /* Whether a controller is actually being used, which scePad cannot tell us
     because it refuses this process. The console logs every press of the PS
     button, so a press appearing since the previous poll proves someone is
     holding a live controller right now. See dualsense.c. */
  int      press_seen;        /* the log contains at least one press          */
  unsigned press_device_id;   /* which controller pressed it, last one wins   */
  uint64_t last_press_ms;     /* when we first noticed a new press; 0 = never */
} ps5tm_pad_t;

/* Copies the last background reading. Never talks to the controller. */
void ps5tm_pad_get(ps5tm_pad_t *out);
void ps5tm_pad_refresh(void);
/* Restores the last battery reading from disk. Without it the card is empty
   after every restart until a controller happens to be switched on. */
void ps5tm_pad_load(void);
/* Lightbar updates are requested from the fan loop and executed by the probe
   thread, so blocking pad calls cannot stall temperature control. */
void ps5tm_pad_lightbar_request(int enabled, int state);
void ps5tm_pad_lightbar_service(void);
int  ps5tm_pad_lightbar_supported(void);

/* Passive backend availability snapshot.
   Reports only symbol/device-node visibility and cached refusal state.
   Does not open controllers, read reports or call undocumented battery APIs. */
typedef struct {
  int host_test;
  /* The three hid/bluetooth flags below are what the controller probe found
     when it looked; asking for them never loads a module. 0 here means it has
     not looked yet (the probe is off), and then they are 0 as well — "not
     checked", not "absent". */
  int hid_resolved;
  int scepad_init;
  int scepad_open;
  int scepad_read_state;
  int scepad_get_handle;
   int scepad_set_lightbar;
   int scepad_reset_lightbar;
  int scepad_set_process_privilege;
  int hidcontrol_get_battery_state;
  int hidcontrol_init;
  int bluetoothhid_init;
  int dev_hid_present;
  int dev_bluetooth_hid_present;
  int cached_pad_refused_rc;
} ps5tm_pad_diag_t;

void ps5tm_pad_diag_get(ps5tm_pad_diag_t *out);


/* ---------------------------------------------------------------- profile */

/* Sony's own limit on a console user's display name, in bytes. */
#define PS5TM_USERNAME_MAX 16

/* Named for the console user, not "profile" — ps5tm_profile_t is already the
   fan's comfort profile and the two have nothing to do with each other. */
typedef struct {
  uint32_t uid;                              /* 0 when no user was found    */
  char     username[PS5TM_USERNAME_MAX + 1];
  int      name_max;                         /* echoed so the UI can limit  */
  int      valid;                            /* a user was identified       */
  int      can_rename;                       /* the firmware offers the call*/
} ps5tm_user_t;

/* Reads the foreground user, falling back to the initial user. Cheap enough
   for a request: no module loading, no service that can block indefinitely. */
void ps5tm_user_get(ps5tm_user_t *out);

/* Renames a local console user. Returns 0 on success; on failure `why`
   receives a sentence fit to show someone. */
int  ps5tm_user_set_name(uint32_t uid, const char *name, const char **why);

int  ps5tm_user_read_name(uint32_t uid, char *out, size_t out_size);

/* sceUserService lives behind this file alone — see the note at the top of
   profile.c. Never look these names up with dlsym. */
void ps5tm_user_service_init(void);
/* Foreground user, falling back to the initial one. Returns 0 on success. */
int  ps5tm_user_service_active_user(int *out_uid);

/* Where the browser's generated avatar files are staged before they are
   applied. Writable; the live profile cache is not. */
const char *ps5tm_avatar_stage_dir(void);

/* Copies the staged files into the user's live profile cache, keeping a
   one-time backup of whatever was there first. 0 on success. */
int ps5tm_avatar_apply(uint32_t uid, int *out_copied, const char **why);

/* Puts that backup back. Only possible when the console had an avatar before
   the first apply. */
int ps5tm_avatar_restore(uint32_t uid, int *out_copied, const char **why);


/* ------------------------------------------------------------------ power */

/* Ask the system to power off, restart or go to rest. Returns 0 when the
   request was accepted; the console then acts on its own schedule.
   ps5tm_power_standby() returns -2 when rest mode is switched off in the
   console's own energy settings, which is a different problem from "not
   supported" and deserves a different message. */
int ps5tm_power_off(void);
int ps5tm_power_reboot(void);
int ps5tm_power_standby(void);

/* Arms the safe-mode flag and restarts, so the console comes back up in the
   safe-mode menu. Not destructive — that menu can simply restart normally —
   but disruptive, so ask before calling. */
int ps5tm_power_safemode(void);

/* A real dialog on the television, not the small corner toast. Reserved for
   things the person genuinely must see, such as an overheating console. */
void ps5tm_show_dialog(const char *text);


/* --------------------------------------------------------- long-term log */

typedef struct {
  uint64_t t_ms;
  int      cpu_c, soc_c, fan_pct, target_c;   /* -1 where unavailable */
} ps5tm_history_entry_t;

typedef struct {
  int      cpu_c, soc_c, fan_pct;             /* all-time peaks, -1 if none */
  uint64_t cpu_at_ms;
} ps5tm_history_peaks_t;

void ps5tm_history_load(void);
void ps5tm_history_tick(int cpu_c, int cpu_valid, int soc_c, int soc_valid,
                        int fan_pct, int fan_valid, int target_c);
unsigned ps5tm_history_snapshot(ps5tm_history_entry_t *out, unsigned max,
                                ps5tm_history_peaks_t *peaks);
void ps5tm_history_reset(void);


/* --------------------------------------------------------------- play time */

/* What the fan thread last measured, for the session in progress; -1 where
   there is no reading. */
typedef struct {
  int cpu_c, soc_c, fan_pct;
  int safety, warning;       /* the emergency mode / the warning is active */
} ps5tm_playtime_sample_t;

/* Starts the recorder (playtime.c); once is enough. */
void ps5tm_playtime_start(void);
/* One pass of it, exposed so a test can supply the clock. `fresh` says the game
   reading is recent enough to act on. */
void ps5tm_playtime_step(int64_t wall_s, uint64_t mono_ms, int fresh,
                         const ps5tm_gamestate_t *gs,
                         const ps5tm_playtime_sample_t *smp);
/* The saved sessions (at most `max`, newest first), the one in progress and a
   name for every title — the caller frees it. */
struct cJSON *ps5tm_playtime_json(unsigned max);
/* Deletes the saved sessions; a running one is kept. 0 on success. */
int  ps5tm_playtime_reset(void);


/* ------------------------------------------------------------ saved games */

/* Backing up the console's saved games and putting them back (savebackup.c). The
   JSON is the caller's to free. The starts answer 200 when the job is under way,
   else the HTTP status that refuses it (400, 404, 409, 503) with the reason in err. */
struct cJSON *ps5tm_saves_json(void);           /* users and titles, drives, backups, the job */
struct cJSON *ps5tm_saves_job_json(void);       /* the job alone, for polling */
int  ps5tm_saves_backup_start(const char *target_mount,
                              const char **users, unsigned nusers,
                              const char **titles, unsigned ntitles,
                              char *err, size_t err_len);
int  ps5tm_saves_verify_start(const char *path, char *err, size_t err_len);
int  ps5tm_saves_restore_start(const char *path, const char *uid, const char *title,
                               char *err, size_t err_len);
int  ps5tm_saves_delete_start(const char *uid, const char *id, const char *target_mount, char *err, size_t err_len);
void ps5tm_saves_cancel(void);
int  ps5tm_saves_busy(void);                    /* 1 while a job with the saved games is under way */


/* ------------------------------------------------------ running payloads */

typedef struct {
  int  pid;
  char name[64];
  int  memory_mb;
  int  is_self;      /* this very app — never offered for killing */
} ps5tm_process_t;

unsigned ps5tm_procmgr_list(ps5tm_process_t *out, unsigned max);
/* Returns 0 on success; `why` receives a reason the caller can show. */
int      ps5tm_procmgr_kill(int pid, const char **why);
/* Logs this process's own name, thread name and application id, once. */
void     ps5tm_procmgr_log_identity(void);
/* Title id of a running game, read from the kernel via sceKernelGetAppInfo.
   `want_app_id` selects a specific application; 0 takes the first game found.
   Returns 0 on success. */
int      ps5tm_procmgr_game_title(uint32_t want_app_id, char *out, size_t out_len);
/* SIGKILLs the eboot.bin of that game title until it is gone (procmgr.c).
   0 closed, 1 was not running, -1 still running after timeout_ms or unknown. */
int      ps5tm_procmgr_close_game(const char *title_id, unsigned timeout_ms);
/* Whether that game title's eboot.bin is running: 1 yes, 0 no, -1 unknown. */
int      ps5tm_procmgr_title_running(const char *title_id);
/* 1 when the kernel route above is available at all, so that a failed lookup
   means "not a game" rather than "cannot ask". */
int      ps5tm_procmgr_appinfo_ready(void);
/* The pid of the first process with this kernel name (ki_comm); -1 when there
   is none, -2 when the process table could not be read at all. */
int      ps5tm_procmgr_pid_by_name(const char *name);

/* ----------------------------------------------------------- payload files
   The folder /data/PS5-Cooling-Center/payloads and the ELF files in the root
   (and the "payloads" folder) of USB drives; starting one means sending it to
   the ELF loader on this console. See payloads.c. */

/* What an operation answers: on failure a machine-readable code and a German
   message for the page; on success how many bytes moved and, for a start, the
   first words the payload printed. */
typedef struct {
  char     code[40];
  char     msg[256];
  char     reply[600];
  uint64_t bytes;
} ps5tm_payload_result_t;

/* Creates the payloads folder when it is missing; leaves an existing one
   alone. Called when the app starts. */
void  ps5tm_payloads_init(void);
/* The JSON answer for GET /api/v1/payload-files; caller frees. NULL when
   nothing can be said (out of memory, or a first scan is still stuck). */
char *ps5tm_payload_list(void);
/* Each returns an HTTP status (200 on success) and fills the result.
   source is "internal" or "usb"; mount and dir are only read for "usb". */
int   ps5tm_payload_start(const char *source, const char *mount, const char *dir,
                          const char *name, ps5tm_payload_result_t *r);
int   ps5tm_payload_copy(const char *mount, const char *dir, const char *name,
                         ps5tm_payload_result_t *r);
int   ps5tm_payload_delete(const char *name, ps5tm_payload_result_t *r);

/* Payload profiles (payprofiles.c): named sequences of payload files with pauses; one can run at app start. */
char *ps5tm_payprof_get_json(void);
int   ps5tm_payprof_save(const char *json, char **out, char *err, size_t en);
int   ps5tm_payprof_run(const char *id, int from_startup, char *err, size_t en);
void  ps5tm_payprof_stop(void);
char *ps5tm_payprof_status_json(void);
void  ps5tm_payprof_start(void);

/* The two identities this payload needs. A process holds one at a time, but
   the authid field is ours to rewrite, so both jobs fit in one run as long as
   they happen in sequence: install the tile first, then take the fan identity
   and keep it for the rest of the process's life. */
#define PS5TM_AUTHID_FAN       0x4801000000000013ULL  /* opens /dev/icc_fan   */
#define PS5TM_AUTHID_SHELLCORE 0x3800000000000010ULL  /* installs a package   */

/* Escalate our own process so /dev/icc_fan and the sensor syscalls are
   reachable. Returns 0 on success. Safe to call when already root. */
int ps5tm_platform_escalate(void);

/* Same, but takes the identity explicitly. Callable repeatedly. */
int ps5tm_platform_escalate_as(unsigned long long authid);

/* Read from the system registry — see regstats.c. Read only.
   The drive-traffic counters this once also held are gone: the firmware does
   not maintain them, which regstats.c documents with the measurements. */
typedef struct {
  int  valid;
  /* 72 because regmgr.h declares SYSTEM_nickname as 65 bytes; the first
     attempt passed a 32-byte buffer and was refused. */
  char nickname[72];

   /* Expert-only locale and time fields, read from registry keys as raw
       values. The mapping to human labels is firmware-specific and therefore
       intentionally not guessed here. */
   int  language_valid;
   int  language_code;
   int  time_zone_valid;
   int  time_zone_code;
   int  timezone_offset_valid;
   int  timezone_offset_min;
   int  region_valid;
   char region_code[8];
} ps5tm_regstats_t;

/* Background thread only: this goes through sceRegMgr. */
void ps5tm_regstats_refresh(void);
void ps5tm_regstats_get(ps5tm_regstats_t *out);


/* Every SoC channel over time, to find out what the unlabelled ones measure.
   See chanlog.c — an experiment, in memory only, format subject to change. */
#define PS5TM_CHANLOG_CAPACITY 900

typedef struct {
  uint32_t t_s;                       /* seconds since the first sample */
  int16_t  ch[PS5TM_SOC_CHANNELS];    /* -1 where the channel stayed silent */
  int16_t  cpu_c;
  int16_t  load_x10;                  /* CPU load in tenths of a percent */
  int16_t  fan_pct;

  /* The fan controller's own 28-byte block, sampled alongside the sensors.
     Only byte 5 has an established meaning (the threshold). The rest are
     recorded precisely because nobody knows yet: a byte that tracks
     temperature or fan speed across a session is a reading, one that sits
     still is a setting. Same reasoning as the channels above — a recording
     answers this, an argument does not. */
  uint8_t  icc[PS5TM_ICC_FAN_CONFIG_SIZE];
  int8_t   icc_valid;

  /* Since 1.45.0: the power rails and the frame rate beside the channels. A
     channel that follows the GPU rail and not the CPU one sits at the GPU —
     the comparison a CPU-only load could not provide (see ERWEITERUNGEN.md,
     SoC-Kanaltest 26.09.2026). -1 where not measured. */
  int16_t  gpu_w10;                   /* rails 0+1, tenths of a watt */
  int16_t  cpu_w10;                   /* rails 2+3 */
  int16_t  mem_w10;                   /* rails 4..7 */
  int16_t  fps10;
  uint8_t  aux_c[4];
  int8_t   aux_valid;
} ps5tm_chan_sample_t;

/* The kernel message buffer, as one NUL-terminated block. Caller frees.
   Returns NULL when it cannot be read. Used by the game detection, by the
   controller battery and by the mic button, which all take their answer
   out of the console's own log rather than from a service that refuses to
   talk to a payload. */
char *ps5tm_msgbuf_read(size_t *len_out);

/* The kernel log as a live view for the page's "Klog" tab — see klog.c. A
   rolling copy of the message buffer: lines are numbered from 1 and never
   renumbered. A marker line stands where lines were lost between two reads. */
#define PS5TM_KLOG_LINE 400

typedef struct {
  uint64_t seq;                 /* 1, 2, 3 … */
  uint64_t t_ms;                /* wall clock, when this app first saw it */
  int      marker;              /* 1: not a kernel line, a note of ours */
  char     text[PS5TM_KLOG_LINE];
} ps5tm_klog_line_t;

/* Up to `max` lines newer than `after`, oldest first. `after` 0 means "show me
   the latest": the last `max` lines. *newest is the number of the newest line
   there is, *more says that lines beyond the ones returned are waiting, and
   *lost that lines between `after` and the first returned one are gone (the
   copy holds a few thousand). Reads the kernel buffer itself, at most twice a
   second however many ask. */
unsigned ps5tm_klog_fetch(uint64_t after, unsigned max, ps5tm_klog_line_t *out,
                          uint64_t *newest, int *more, int *lost);

/* Saved and recorded kernel logs in /data/PS5-Cooling-Center/klog-live-log — see
   klogfiles.c. Names are "klog-<stamp>.log" (what the page sent) and
   "aufnahme-<stamp>.log" (a recording this app writes itself). */
int  ps5tm_klogfiles_create(const char *kind, int tz_min, char *name, size_t name_n);
int  ps5tm_klogfiles_path(const char *name, char *out, size_t n);
struct cJSON *ps5tm_klogfiles_json(void);                 /* the folder and the recording */
int  ps5tm_klogfiles_clear(unsigned *deleted, uint64_t *bytes, unsigned *kept);
int  ps5tm_klogrec_start(int tz_min, char *err, size_t err_len);   /* 200, else an HTTP status */
int  ps5tm_klogrec_stop(void);
struct cJSON *ps5tm_klogrec_json(void);

/* The games list's memory between scans, on the drive: /data/PS5-Cooling-Center/covers_and_more
   (libcache.c). What a scan found about one title that is slow to find out again: how big
   its folder or image is, whether it is a backport, where it lies — kept in
   <title>/meta.json — and a copy of its cover, <title>/icon0.png. Used while
   config.library_cache is on; library.c does the filling and the using. */
typedef struct {
  char     title_id[12];
  char     content_id[40];
  char     name[128];
  char     version[16];
  int      platform;                  /* 0 PS5, 1 PS4, -1 unknown                   */
  char     source[128];               /* mount.lnk target                           */
  char     format[8];                 /* folder exfat ffpkg ffpfs ffpfsc image pkg  */
  char     real_path[256];
  /* what a look into the game folder found (the keys say when it must be redone) */
  int      mods_checked;              /* 1: the fields below were measured          */
  int      backport, ampr, playgo, extra_libs, nlibs, more_libs;
  uint32_t eboot_sdk;
  char     libs[6][32];
  int64_t  eboot_mtime, eboot_size, fakelib_mtime;
  /* the size of the folder or image, and when it was measured (seconds since 1970) */
  char     size_path[256];
  int64_t  size, size_at;
  /* the cover copy, kept by libcache.c itself */
  char     cover_ts[16];
  int64_t  cover_bytes;
} ps5tm_libmeta_t;

int      ps5tm_libcache_enabled(void);
unsigned ps5tm_libcache_load(ps5tm_libmeta_t *out, unsigned max);
void     ps5tm_libcache_save(const ps5tm_libmeta_t *m, unsigned n);
int      ps5tm_libcache_cover_get(const char *title_id, const char *ts, char *path, size_t n);
int      ps5tm_libcache_cover_put(const char *title_id, const char *ts, const char *src);
struct cJSON *ps5tm_libcache_json(void);
int      ps5tm_libcache_clear(unsigned *titles, uint64_t *bytes);

/* ---------------------------------------------------------------- packages
 * PS4 and PS5 package files on sticks, discs and the console's storage: read (pkgparse.c), found and listed
 * (pkgscan.c), split into parts for FAT32 sticks and discs (pkgsplit.c). */
#define PS5TM_PKG_PATH 512

typedef struct {
  char     path[PS5TM_PKG_PATH];
  char     file[256];
  char     title_id[24];
  char     content_id[64];
  char     name[160];            /* the title to show: German first */
  char     version[24];          /* "v1.03" */
  char     category[8];          /* gd, gp, ac, ... */
  char     kind[8];              /* "base", "update" or "dlc" */
  int      plat;                 /* 5 PS5, 4 PS4, 0 unknown */
  int      valid;
  uint64_t size;                 /* this file */
  uint64_t total;                /* the whole package; for the parts of a split one, all of them together */
  int64_t  mtime;
  unsigned part, parts;          /* of a split package: this part (from 1) of that many; else 0 and 0 */
  uint8_t  uuid[16];             /* the parts of one package share it */
  char     orig_file[256];       /* parts: the file name before the split */
  int      raw;                  /* parts: the slice is stored as it is (the only form read here) */
  uint32_t data_off;             /* parts: where the slice begins in this file */
  uint64_t data_size, part_off;  /* parts: its size, and where it begins in the whole package */
  uint64_t icon_off;             /* where icon0.png lies in this file */
  uint32_t icon_size;
} ps5tm_pkg_t;

int  ps5tm_pkg_parse(const char *path, ps5tm_pkg_t *out);
int  ps5tm_pkg_icon(const ps5tm_pkg_t *p, uint8_t **data, size_t *n);
int  ps5tm_pkg_parse_reader(int (*rd)(void *ctx, void *buf, size_t n, uint64_t off), void *ctx, uint64_t total,
                            const char *file, ps5tm_pkg_t *out);
int  ps5tm_pkg_icon_reader(const ps5tm_pkg_t *p, int (*rd)(void *ctx, void *buf, size_t n, uint64_t off), void *ctx,
                           uint8_t **data, size_t *n);
int  ps5tm_pkg_part_name(const char *file, unsigned *part);

/* The drives packages are looked for on, and written to (parts of a split package): the sticks, discs and
   M.2 drives under /mnt, and the console's own storage (base /data, only its folder "pkg"). */
typedef struct {
  char     mount[40];            /* what the page calls it: "/mnt/usb0", "/user" */
  char     base[40];             /* where its files are: the same, or "/data" for "/user" */
  char     label[40];
  uint64_t free_bytes;
  int      internal;
} ps5tm_pkgdrive_t;
unsigned ps5tm_pkg_drives(ps5tm_pkgdrive_t *out, unsigned max);

int  ps5tm_pkgscan_start(void);                 /* 1 started, 0 one is under way, -1 failed */
int  ps5tm_pkgscan_busy(void);
struct cJSON *ps5tm_pkgscan_json(void);
int  ps5tm_pkgscan_find(const char *id, ps5tm_pkg_t *out);

int  ps5tm_pkgsplit_start(const char *id, const char *target_mount, uint64_t part_bytes, char *err, size_t err_len);
void ps5tm_pkgsplit_cancel(void);
int  ps5tm_pkgsplit_busy(void);
struct cJSON *ps5tm_pkgsplit_job_json(void);

/* Installing a package the last search found (pkginstall.c, pkgstream.c, helper/pkginst_helper.c): the console's own
   installation, one package at a time, and never without a click. */
struct cJSON *ps5tm_pkginst_plan_json(const char *id, int *http);   /* what installing would do, and whether it can */
/* A package that is not on the console: the browser on a PC uploads it, piece by piece, while the console installs it
   (pkglive.c). One at a time; the pieces go into a ring in memory (64 MB), the console reads them back through the same
   server as any package. The id it answers with ("live-N") is used like the id of a package a search found. */
struct cJSON *ps5tm_pkglive_init(const char *name, uint64_t size, int *http, char *err, size_t err_len);
struct cJSON *ps5tm_pkglive_state(const char *id, long since, unsigned wait_ms, int *http);   /* since < 0: answer at once */
void ps5tm_pkglive_receive(int fd, const char *query, const char *prefix, size_t prefix_len, size_t total);   /* http.c */
int  ps5tm_pkglive_cancel(const char *id);

int  ps5tm_pkginst_start(const char *id, char *err, size_t err_len);   /* HTTP status: 200 started, 404 no such package, 409 cannot be done */
/* The same for the queue: *transient says a refusal is only "not now" (a game runs, another job is under way), *seq is the
   number of the job that has started. ps5tm_pkginst_result tells how the last job ended: state 0 going/none, 1 done,
   2 failed, 3 stopped. */
typedef struct {
  unsigned seq;
  int      state, verified, blind, started, error_code;
  char     error[512], note[400];
} ps5tm_pkginst_result_t;
int  ps5tm_pkginst_start_q(const char *id, char *err, size_t err_len, int *transient, unsigned *seq);
void ps5tm_pkginst_result(ps5tm_pkginst_result_t *r);
void ps5tm_pkginst_cancel(void);
int  ps5tm_pkginst_busy(void);
struct cJSON *ps5tm_pkginst_job_json(void);

/* Several packages one after the other (pkgqueue.c): a list the person fills from the packages the search found, started
   with a click. Each package is asked the same questions as an installation of its own when its turn comes (installed
   already? the game before it there? a game running?), so a refused one is skipped with its reason, and a failed one
   halts the queue. In memory only: an app that restarts starts with an empty queue. */
struct cJSON *ps5tm_pkgqueue_json(void);
int  ps5tm_pkgqueue_add(const char *const *ids, unsigned n, unsigned *added, unsigned *ignored, char *err, size_t err_len);   /* HTTP status */
int  ps5tm_pkgqueue_start(char *err, size_t err_len);              /* HTTP status: 200 running, 409 not now */
void ps5tm_pkgqueue_pause(void);                                   /* after the package that runs */
int  ps5tm_pkgqueue_cancel(void);                                  /* stops the package that runs and halts the queue; 1 when one ran */
int  ps5tm_pkgqueue_remove(const char *id);                        /* HTTP status: 200, 404 no such entry, 409 it is running */
int  ps5tm_pkgqueue_retry(const char *id);                         /* NULL: every failed, stopped or skipped one; returns how many */
void ps5tm_pkgqueue_clear(int all);                                /* the finished ones, or all but the running one */

/* The system's uninstall of a title (the game, its updates and add-ons; saved games stay), through a helper of its
   own. Synchronous. 0, or the system's code / a PKGI_E_ code with the reason in why. */
int  ps5tm_pkginst_uninstall(const char *title_id, int *res_pat, int *res_addcont, char *why, size_t why_len);

/* Probe thread, once a second: on a double press of the controller's
   microphone button, processor temperature and fan speed as a system
   notification. See micbutton.c. */
void ps5tm_micbutton_poll(void);

/* Clock rates from SceSystemStateMgr's power-state table — see clocks.c.
   Includes the per-core CPU clocks and the graphics clock, neither of which
   the ordinary syscalls give up. Event-driven, so an age travels with it. */
typedef struct {
  int      valid;
  int      core_mhz[PS5TM_MAX_CORES];
  int      core_count;
  int      gfx_mhz;          /* GFX  — the graphics clock */
  int      mem_mhz;          /* U — UCLK, memory controller */
  int      fabric_mhz;       /* F — FCLK, Infinity Fabric */
  int      fan_raw;          /* FAN, meaning not established */
  int      bapm_on;          /* AMD power/thermal budget */
  char     pcie_gen[8];
  uint64_t seen_ms;
} ps5tm_clocks_t;

/* Memory and power state, from the same log pass — see clocks.c. */
/* One round of FMEM output listed 34 processes on this console; the cap is
   set above that so a full round always fits. */
#define PS5TM_FMEM_MAX 48

typedef struct {
  char   name[48];
  double used_mb, total_mb;
} ps5tm_fmem_t;

typedef struct {
  int      valid;
  double   rss_mb, kernel_mb, wire_mb, swap_mb;
  int      pt_cpu_used, pt_cpu_total;    /* page tables, CPU side */
  int      pt_gpu_used, pt_gpu_total;    /* page tables, GPU side */
  ps5tm_fmem_t procs[PS5TM_FMEM_MAX];
  int      proc_count;

  char     power_mode[40];               /* e.g. NAVIGATION_ACTIVE_1 */
  int      idle_sec;                     /* -1 when not reported */
  uint64_t seen_ms;
} ps5tm_sysstate_t;

void ps5tm_clocks_refresh(void);         /* fills both of the above */
void ps5tm_clocks_get(ps5tm_clocks_t *out);
void ps5tm_sysstate_get(ps5tm_sysstate_t *out);

void     ps5tm_chanlog_sample(const ps5tm_sensors_t *s);
unsigned ps5tm_chanlog_snapshot(ps5tm_chan_sample_t *out, unsigned max);


/* Long-term cooling performance — see thermalog.c. One entry per week, only
   samples taken inside a fixed load/fan window, so that a rise really means
   the cooling has degraded rather than that the games got heavier. */
#define PS5TM_THERMAL_WEEKS 52

typedef struct {
  uint32_t week;        /* whole weeks since the epoch */
  uint32_t samples;
  int      temp_c10;    /* mean chip temperature, tenths of a degree */
  int      fan_pct10;   /* mean fan speed, tenths of a percent */
  int      load_pct;    /* mean load */
   int      activity_pct;/* foreground activity share (0..100) */
  int      usable;      /* enough samples to be worth comparing */
  int      same_settings; /* recorded under the settings in force now */
} ps5tm_thermal_week_t;

void     ps5tm_thermal_load(void);
void     ps5tm_thermal_sample(int temp_c, int temp_valid,
                              double load_pct, int load_valid,
                                             int fan_pct, int fan_valid,
                                             int activity_fg);
unsigned ps5tm_thermal_snapshot(ps5tm_thermal_week_t *out, unsigned max);
/* 0 when a comparison is possible, -1 while there is still too little. Only
   weeks recorded under the settings in force now count (settings stamp). */
int      ps5tm_thermal_verdict(int *delta_c10, unsigned *weeks_usable,
                               int *baseline_c10, int *current_c10,
                               uint64_t *since_ms, unsigned *older_weeks);
void     ps5tm_thermal_restart(void);


/* Fills `out`. Returns 0 if at least one temperature sensor responded. */
int ps5tm_platform_read_sensors(ps5tm_sensors_t *out);

/* Explicit one-shot read of optional risky telemetry (load/clocks/power). */
int ps5tm_platform_probe_risky_once(ps5tm_sensors_t *out);

/* The busiest threads system-wide, refreshed every 10 s from the same
   per-thread times as the load (risky telemetry must be on). */
#define PS5TM_TOP_THREADS 6
typedef struct {
  uint32_t tid;
  uint32_t pid;          /* the process it belongs to */
  char     name[32];
  double   pct;          /* of one CPU over the last interval */
} ps5tm_thread_load_t;
unsigned ps5tm_platform_top_threads(ps5tm_thread_load_t *out, unsigned max);

/* Power rails and live clocks (with_power_clocks) and the frame rate
   (with_fps, honoured only while PS5TM_PROBE_FPS is set), into `out`. See
   telemetry.c. Power and clocks are read at most every 2 s and cached in
   between. The frame counter advances on every call with `with_fps`, so only
   the fan worker's once-a-second pass sets it — which also makes `with_fps`
   the marker for "must not wait": that caller only try-locks the Sony lock
   and, when it is busy, gets the previous readings instead of waiting. */
void ps5tm_telemetry_update(ps5tm_sensors_t *out, int with_power_clocks,
                            int with_fps);

/* Short name of a power rail, e.g. "GPU Core". */
const char *ps5tm_power_rail_name(int rail);

/* Watts summed over rails [first, last]; -1 when there is no reading. */
double ps5tm_power_sum_w(const ps5tm_power_t *p, int first, int last);

/* Logs once which of the seven weakly linked sensor calls this firmware
   provides. Without it a missing symbol looks exactly like a sensor that
   reads nothing. */
void ps5tm_platform_log_sensor_symbols(void);

/* CPU and GPU page-table occupancy in megabytes, straight from the kernel.
   Returns 0 on success, -1 if the call is missing or answers with nothing.
   ⚠ Mapped address space, not physical memory in use — see platform.c. */
int ps5tm_platform_page_stats(int *cpu_used, int *cpu_total,
                              int *gpu_used, int *gpu_total);

/* Writes the ICC fan ramp-up threshold. `errno_out` may be NULL.
   Returns 0 on success, -1 on failure. */
int ps5tm_platform_set_fan_threshold(int temp_c, int *errno_out);

/* Reads that block back out of the controller — the counterpart to the write
   above, and the only way to learn what the controller really holds rather
   than what was last sent to it. `out` must have room for
   PS5TM_ICC_FAN_CONFIG_SIZE bytes; `errno_out` may be NULL.
   Returns 0 on success, -1 on failure. */
int ps5tm_platform_read_fan_config(unsigned char *out, int *errno_out);

/* True once /dev/icc_fan has been opened successfully at least once. */
int ps5tm_platform_fan_available(void);
const char *ps5tm_platform_fan_message(void);

uint32_t ps5tm_platform_firmware(void);
int      ps5tm_platform_firmware_recognized(uint32_t fw);
/* The console's own version number, e.g. "12.00". */
void     ps5tm_platform_firmware_text(uint32_t fw, char *out, size_t out_len);
/* The kernel-offset range this firmware belongs to — a jailbreak-compatibility
   detail, not the version the user sees in the system settings. */
const char *ps5tm_platform_firmware_group(uint32_t fw);


/* ------------------------------------------------------------------- fan */

typedef struct {
  ps5tm_sensors_t sensors;
  uint64_t        sampled_at_ms;
  int             applied_threshold_c;  /* 0 = nothing applied yet */
  int             target_duty_pct;      /* what the controller wants     */
  int             automatic;
  int             fan_available;
  int             warning_cpu;
  int             warning_soc;

  int             avg_temp_c10;   /* moving average, tenths of a degree  */
  int             trend_c100;     /* rise across the window, x100        */
  int             safety_active;  /* comfort rules suspended             */
  int             samples;        /* readings in the average so far      */
  int             effective_target_c; /* after any per-game override     */

   int             warning_active;      /* hottest sensor >= warning limit */
   uint64_t        warning_since_ms;    /* first seen above warning limit  */
   int             warning_countdown_s; /* remaining time, -1 if inactive  */

   int             lightbar_enabled;
   int             lightbar_supported;
   int             lightbar_state;      /* 0=ok,1=warn,2=hot */

   /* What the fan controller actually holds, read back rather than assumed.
      -1 when this firmware does not answer command 0x8F08; the controller
      then falls back to rewriting the threshold periodically. */
   int             observed_threshold_c;
   int             readback_available;
   /* How often the console has overwritten our setting since start, and what
      it last set. Measured on FW 12.00: 91 °C at game launch. */
   unsigned        override_count;
   int             last_override_c;
} ps5tm_snapshot_t;

/* 0 when the controller thread runs (or already did), -1 when it could not be
   started. A failed start leaves nothing marked as started, so a later call
   tries again. */
int  ps5tm_fan_init(void);
/* Title id of the per-game rule currently in force, empty when none. */
const char *ps5tm_fan_active_rule(void);
void ps5tm_fan_snapshot(ps5tm_snapshot_t *out);
/* Applies `temp_c` immediately and pins it. Returns 0 on success. */
int  ps5tm_fan_apply_manual(int temp_c, int *errno_out);
/* Maps a curve duty (0..100) onto an ICC threshold and back. */
int  ps5tm_fan_duty_to_threshold(int duty_pct);
int  ps5tm_fan_threshold_to_duty(int threshold_c);
int  ps5tm_fan_curve_eval(const ps5tm_config_t *cfg, int temp_c);


/* ------------------------------------------------------------------ tile */

typedef enum {
  PS5TM_TILE_UNKNOWN = 0,
  PS5TM_TILE_NOT_INSTALLED,
  PS5TM_TILE_INSTALLING,
  PS5TM_TILE_INSTALLED,
  PS5TM_TILE_ADAPTER_UNAVAILABLE,
  PS5TM_TILE_ERROR
} ps5tm_tile_state_t;

const char *ps5tm_tile_state_name(ps5tm_tile_state_t state);
ps5tm_tile_state_t ps5tm_tile_query(char *msg, size_t msg_len);
ps5tm_tile_state_t ps5tm_tile_ensure(char *msg, size_t msg_len);

/* Start-up hook: installs the built-in tile package if — and only if — no
   tile is present yet. Returns with the fan identity restored. */
void ps5tm_tile_bootstrap(void);

/* Queued by a web request, performed by the background thread. */
void ps5tm_tile_request_install(void);
void ps5tm_tile_service(void);

/* Streams an uploaded package straight to disk. Called from the HTTP layer
   because it must not buffer ten megabytes in memory. */
void ps5tm_api_receive_upload(int fd, const char *prefix, size_t prefix_len,
                              size_t total);

/* Same, for one generated avatar file. `query` carries name=<file>; only the
   names the browser's pipeline produces are accepted. */
void ps5tm_api_receive_avatar_file(int fd, const char *query,
                                   const char *prefix, size_t prefix_len,
                                   size_t total);

/* Same, for the text of a saved kernel log (the page's "Speichern"): a few
   megabytes, too many for a JSON body. `query` may carry tz=<minutes east of UTC>. */
void ps5tm_api_receive_klog_save(int fd, const char *query,
                                 const char *prefix, size_t prefix_len,
                                 size_t total);

/* The file manager (filemgr.c): a file from the page, streamed to disk; `query` carries path=<folder>&name=<name>,
   percent-encoded. */
void ps5tm_filemgr_receive_upload(int fd, const char *query,
                                  const char *prefix, size_t prefix_len,
                                  size_t total);


/* ------------------------------------------------------------------ http */

typedef struct {
  char   method[8];
  char   path[512];
  /* 3 KiB: the file manager (06.10.2026) puts a whole path into the query, percent-encoded (a name in UTF-8
     triples there) */
  char   query[3072];
  char  *body;
  size_t body_len;
  /* Who asked, as dotted IPv4. "127.0.0.1" is the console's own browser,
     opened from the tile (1.46.0: the start button needs to know). */
  char   peer[16];
  /* The browser's Accept-Encoding names gzip: stored pages go out as they are, else they are unpacked first. */
  int    gzip_ok;
} ps5tm_request_t;

/* The file manager's requests (/api/v1/files/...): 1 when it answered, 0 when the path is not its own. */
int  ps5tm_filemgr_api(int fd, const ps5tm_request_t *req);

void ps5tm_http_send(int fd, int status, const char *status_text,
                     const char *ctype, const void *body, size_t len,
                     const char *extra_headers);
void ps5tm_http_send_json(int fd, int status, const char *json);
/* Streams a file in chunks and lets the browser keep it for max_age_s.
   -1 when the file cannot be sent at all, so the caller can answer 404. */
int  ps5tm_http_send_file(int fd, const char *path, const char *ctype,
                          size_t max_bytes, unsigned max_age_s);
/* Sends bytes that are in memory, which the browser may keep for max_age_s. */
void ps5tm_http_send_cached(int fd, const char *ctype, const void *body, size_t len,
                            unsigned max_age_s);
/* Streams a file as a download (the browser saves it under `filename`).
   -1 when it cannot be sent at all. */
int  ps5tm_http_send_download(int fd, const char *path, const char *ctype,
                              const char *filename, uint64_t max_bytes);
void ps5tm_http_send_error(int fd, int status, const char *code,
                           const char *message);

/* Returns a listening socket, -1 on error, or -2 when the port is taken. */
int  ps5tm_http_bind(unsigned port);
/* 1 when a listening socket could be opened on this port right now. Asked
   before a settings change moves the page there: a port that is taken would
   close the page's own listener and leave it with nowhere to go. */
int  ps5tm_http_port_free(unsigned port);
/* Serves on `srv` until the configured port changes; then returns 0 and
   closes the socket. */
int  ps5tm_http_run(int srv, unsigned port);

void ps5tm_api_handle(int fd, ps5tm_request_t *req);
/* Re-probes the tile installer and refreshes the cached state /status reports. */
void ps5tm_api_tile_refresh(void);


/* ---------------------------------------------------------------- assets */

typedef struct {
  const char          *path;
  const char          *ctype;
  const unsigned char *data;
  unsigned             len;       /* the bytes stored: gzip when gz is 1 */
  unsigned             raw_len;   /* the file's own length */
  unsigned             gz;        /* 1: stored as gzip (tools/gen_assets.py) */
} ps5tm_asset_t;

/* Serves one of the embedded pages for a GET (assets.c): 1 when the path is one of them. */
int ps5tm_asset_serve(int fd, const ps5tm_request_t *req);

extern const ps5tm_asset_t ps5tm_assets[];
extern const unsigned      ps5tm_assets_count;

#endif /* PS5TM_H */
