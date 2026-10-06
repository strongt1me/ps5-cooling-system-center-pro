/* Network link and connected display.
 *
 * Neither is needed to cool the console; both answer the question "is this
 * thing set up the way I think it is" that people actually ask when they open
 * a dashboard. Everything here is best-effort: a value that does not arrive
 * is left invalid rather than filled with a plausible-looking guess.
 *
 * Every symbol goes through dlsym against what is already loaded. These two
 * libraries are not linked in, so on a console where they are absent from the
 * process the cards simply stay empty — see the note below on why nothing is
 * loaded on demand any more.
 */

/* No module is ever loaded at run time.
 *
 * sceKernelLoadStartModule() takes the runtime linker's lock, and on FW 12.00
 * it does not always come back. That lock is process-wide, so a single stuck
 * call froze every other thread the moment it needed a symbol resolved — the
 * web server answered exactly one request and then went silent. Moving the
 * call to a background thread did not help, because the lock is shared.
 *
 * Only libraries the ELF is linked against are used, and only through dlsym.
 * A symbol that is not there leaves a null pointer and the feature reports
 * nothing, which is a far better failure than a dead dashboard. */

#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "dynsym.h"
#include "ps5tm.h"

#ifndef PS5TM_HOST_TEST


/* ── network ─────────────────────────────────────────────────────────── */

/* sceNetCtlGetInfo() fills a union sized for the largest member; the code
   only ever reads the member matching the code it asked for. 64 bytes covers
   every documented variant with room to spare. */
typedef union {
  unsigned char raw[64];
  char          str[64];
  uint32_t      u32;
  uint8_t       u8;
} netctl_info_t;

/* Codes are stable across firmware; these are the ones we use.
 *
 * SSID and the signal strength were wrong until 31.07.2026, and the console
 * said so in a way that left no doubt: the SSID came back as the single
 * character "d". That is byte 0x64 — decimal 100 — the signal percentage read
 * as if it were text. We had been asking for code 9 and calling it SSID, when
 * 9 is the RSSI percentage; 12 is the DHCP host name, which is why the
 * strength then read as zero.
 *
 * Sony's order: 1 device, 5 BSSID, 6 SSID, 8 RSSI in dBm, 9 RSSI in percent,
 * 12 DHCP host name, 14 IP address. Device and IP were right all along, which
 * is why the connection type and address always looked correct. */
#define NETCTL_INFO_DEVICE        1   /* 0 = wired, 1 = wireless */
#define NETCTL_INFO_SSID          6
#define NETCTL_INFO_RSSI_PERCENT  9
#define NETCTL_INFO_CHANNEL      10
#define NETCTL_INFO_IP_ADDRESS   14
#define NETCTL_STATE_DISCONNECTED 0

typedef int (*fn_netctl_init_t)(void);
typedef int (*fn_netctl_state_t)(int *);
typedef int (*fn_netctl_info_t)(int, netctl_info_t *);
typedef int (*fn_netctl_link_t)(int *);
typedef int (*fn_netctl_wifi_t)(int *);
typedef int (*fn_netctl_nat_t)(void *);

static fn_netctl_init_t  p_net_init  = NULL;
static fn_netctl_state_t p_net_state = NULL;
static fn_netctl_info_t  p_net_info  = NULL;
static fn_netctl_link_t  p_net_link  = NULL;
static fn_netctl_wifi_t  p_net_wifi  = NULL;

/* ── display ─────────────────────────────────────────────────────────── */

typedef int (*fn_vo_open_t)(int, int, int, const void *);
typedef int (*fn_vo_close_t)(int);
typedef int (*fn_vo_resolution_t)(int, void *);
typedef int (*fn_vo_monitor_t)(int, void *);

static fn_vo_open_t       p_vo_open   = NULL;
static fn_vo_close_t      p_vo_close  = NULL;
static fn_vo_resolution_t p_vo_res    = NULL;
static fn_vo_monitor_t    p_vo_mon    = NULL;

static int g_resolved = 0;

/* Distinguishable from any Sony error code. */
#define NET_NOT_BOUND 0x7FFFFFFF

/* Weakly bound, never looked up — same reasoning as in sysinfo.c: a Sony
 * export carries a NID, not a name, so only the linker can reach it, and weak
 * keeps a missing one from killing the payload at load time.
 *
 * libSceNetCtl is on the link line (ps5debug-NG links it and runs on FW 12.00,
 * so it is proven safe), which is why these bind. libSceVideoOut deliberately
 * is not: no reference payload links it, and an unproven NEEDED entry is what
 * kills a payload before main().
 *
 * ⚠ The conclusion drawn from that used to be written here — that the four
 * VideoOut functions therefore stay NULL and the display card stays blank.
 * **Measured on 01.08.2026, that is wrong.** ps5tm_dynsym() reaches them
 * anyway (it resolves through the kernel, not only through NEEDED), and the
 * console reported 3840x2160. Only sceVideoOutGetHdmiMonitorInfo fails, so
 * the resolution arrives and the TV's model name does not. Not linking the
 * library is still right; the pessimism about the outcome was not. */
#ifndef PS5TM_HOST_TEST
extern int sceNetCtlInit(void)                        __attribute__((weak));
extern int sceNetCtlGetState(int *)                   __attribute__((weak));
extern int sceNetCtlGetInfo(int, netctl_info_t *)     __attribute__((weak));
extern int sceNetCtlGetEtherLinkMode(int *)           __attribute__((weak));
extern int sceNetCtlGetWifiType(int *)                __attribute__((weak));
#endif

static void
resolve(void) {
  if(g_resolved) return;
  g_resolved = 1;

#ifndef PS5TM_HOST_TEST
  p_net_init  = sceNetCtlInit;
  p_net_state = sceNetCtlGetState;
  p_net_info  = sceNetCtlGetInfo;
  p_net_link  = sceNetCtlGetEtherLinkMode;
  p_net_wifi  = sceNetCtlGetWifiType;
#endif

  const char *VOUT = "libSceVideoOut.sprx";

  p_vo_open  = (fn_vo_open_t)      ps5tm_dynsym(VOUT, "sceVideoOutOpen");
  p_vo_close = (fn_vo_close_t)     ps5tm_dynsym(VOUT, "sceVideoOutClose");
  p_vo_res   = (fn_vo_resolution_t)ps5tm_dynsym(VOUT,
                                                "sceVideoOutGetResolutionStatus");
  p_vo_mon   = (fn_vo_monitor_t)   ps5tm_dynsym(VOUT,
                                                "sceVideoOutGetHdmiMonitorInfo");

  PS5TM_INFO("netctl_resolved",
             "Netzwerkabfrage: GetState %s, GetInfo %s.",
             p_net_state ? "bereit" : "fehlt",
             p_net_info  ? "bereit" : "fehlt");

  /* The display side gets its own line. It used to report nothing and say
     nothing about why — from outside, "no TV connected", "the call failed"
     and "the function was never reachable" look identical, and that ambiguity
     has already cost a wasted round of testing elsewhere.
     The line settled it immediately: all but the monitor-info call resolve. */
  if(p_vo_open && p_vo_close)
    PS5TM_INFO("videoout_resolved",
               "Bildschirmabfrage bereit (Auflösung %s, Fernsehermodell %s).",
               p_vo_res ? "ja" : "nein", p_vo_mon ? "ja" : "nein");
  else
    PS5TM_INFO("videoout_unavailable",
               "Bildschirmabfrage nicht verfügbar — die Karte „Bildschirm\" "
               "bleibt leer. Die Kühlung ist davon nicht betroffen.");

  /* Measured on 31.07.2026: this returns 0 on FW 12.00, so its result is not
     kept any more. It was worth checking once — an ignored return code is
     exactly the kind of silence that cost four rounds on the ICC counters. */
  if(p_net_init) p_net_init();
}


static void
read_network(ps5tm_sysinfo_t *info) {
  /* The step-by-step diagnostic that lived here was removed once it had done
     its job: every call returns 0 and the values are right. What it proved is
     worth keeping in mind, though — "up: false" turned out to mean the probe
     was switched off, not that anything was broken. */
  if(!p_net_state || !p_net_info) return;

  int state = -1;
  if(p_net_state(&state) != 0 || state == NETCTL_STATE_DISCONNECTED) return;
  info->net_up = 1;

  netctl_info_t v;

  memset(&v, 0, sizeof(v));
  if(p_net_info(NETCTL_INFO_DEVICE, &v) == 0)
    info->net_wifi = (v.u32 != 0);

  memset(&v, 0, sizeof(v));
  if(p_net_info(NETCTL_INFO_IP_ADDRESS, &v) == 0) {
    v.str[sizeof(v.str) - 1] = 0;
    snprintf(info->net_ip, sizeof(info->net_ip), "%s", v.str);
  }

  if(info->net_wifi) {
    memset(&v, 0, sizeof(v));
    if(p_net_info(NETCTL_INFO_SSID, &v) == 0) {
      v.str[sizeof(v.str) - 1] = 0;
      snprintf(info->net_ssid, sizeof(info->net_ssid), "%s", v.str);
    }

    memset(&v, 0, sizeof(v));
    if(p_net_info(NETCTL_INFO_RSSI_PERCENT, &v) == 0 && v.u32 <= 100) {
      info->net_rssi_pct   = (int)v.u32;
      info->net_rssi_valid = 1;
    }

    /* The band is derived from the channel, not from sceNetCtlGetWifiType.
     *
     * That call used to decide it, as "0 means 2.4 GHz, anything else means
     * 5 GHz" — a rule nobody had checked. Measured on 31.07.2026 it returned
     * 4 on a network whose name marks it as 2.4 GHz, so the
     * rule is wrong; and one sample is not enough to work out what the number
     * actually means. It may well be the wireless standard rather than a
     * frequency, in which case no two-way split could ever be right.
     *
     * The channel needs no interpretation at all. Channels 1–14 are the
     * 2.4 GHz band and 32 upwards is 5 GHz, by international allocation
     * rather than by anyone's choice — and the channel is the more useful
     * number of the two for someone chasing a poor connection. */
    memset(&v, 0, sizeof(v));
    unsigned channel = (p_net_info(NETCTL_INFO_CHANNEL, &v) == 0) ? v.u32 : 0;

    if(channel >= 1 && channel <= 14) {
      info->net_band_ghz10 = 24;
      info->net_band_valid = 1;
    } else if(channel >= 32) {
      info->net_band_ghz10 = 50;
      info->net_band_valid = 1;
    }

    /* Measured on 01.08.2026: channel 1 on the home Wi-Fi, which the
       rule above turns into 2.4 GHz — matching the router's own name, where
       sceNetCtlGetWifiType had claimed 5 GHz. Code 10 is therefore the
       channel, and the diagnostic that established it has been removed. */
  } else {
    /* Ethernet link mode is a bitfield; the speed lives in the low bits and
       the documented values are 10, 100 and 1000 Mbit. */
    int mode = 0;
    if(p_net_link && p_net_link(&mode) == 0) {
      int mbit = (mode & 0x4) ? 1000 : (mode & 0x2) ? 100 : (mode & 0x1) ? 10 : 0;
      if(mbit) { info->net_link_mbit = mbit; info->net_link_valid = 1; }
    }
  }
}


/* sceVideoOutGetResolutionStatus fills a struct whose first two 32-bit words
   are the full width and height. Only those are read; the rest of the layout
   is not documented well enough to trust. */
static void
read_display(ps5tm_sysinfo_t *info) {
  if(!p_vo_open || !p_vo_close) return;

  int handle = p_vo_open(0xFF, 0, 0, NULL);
  if(handle < 0) return;

  if(p_vo_res) {
    uint32_t st[16];
    memset(st, 0, sizeof(st));
    if(p_vo_res(handle, st) == 0 &&
       st[0] >= 640 && st[0] <= 7680 && st[1] >= 480 && st[1] <= 4320) {
      info->tv_width            = (int)st[0];
      info->tv_height           = (int)st[1];
      info->tv_resolution_valid = 1;
    }
  }

  /* The monitor block starts with the EDID-derived display name. Anything
     unprintable means the layout is not what we assumed, so nothing is
     reported rather than mojibake. */
  if(p_vo_mon) {
    unsigned char mi[512];
    memset(mi, 0, sizeof(mi));
    if(p_vo_mon(handle, mi) == 0) {
      size_t n = 0;
      while(n < 63 && mi[n] >= 0x20 && mi[n] < 0x7f) n++;
      if(n >= 3) {
        memcpy(info->tv_name, mi, n);
        info->tv_name[n] = 0;
      }
    }
  }

  p_vo_close(handle);
}


void
ps5tm_netdisp_fill(ps5tm_sysinfo_t *info) {
  resolve();
  read_network(info);

  /* Not while a game has the screen. The game owns the video output then,
     and opening it from here only ever failed — measured on 24.09.2026: the
     resolution arrived on the home screen and never once in 50 minutes of
     play, while the attempt went out every five seconds. Something that
     cannot answer has no business touching the display path of a running
     game; the one time the picture went black that evening was the TV
     renegotiating HDMI for game mode, but there is no reason to be anywhere
     near that when nothing is gained. The card simply stays empty during
     play, as it did before.

     "No game in front" has to be something that was measured. With the game
     probe switched off — the setting used while hunting for whatever freezes
     the console — nothing is ever measured, foreground stays 0 for good, and
     this opened the video output every five seconds during play after all. */
  ps5tm_gamestate_t gs;
  ps5tm_gamestate_get(&gs);
  if(ps5tm_gamestate_measured() && !gs.foreground)
    read_display(info);
}

#else  /* host test */

void
ps5tm_netdisp_fill(ps5tm_sysinfo_t *info) {
  info->net_up = 1;
  info->net_wifi = 1;
  snprintf(info->net_ssid, sizeof(info->net_ssid), "Testnetz");
  snprintf(info->net_ip, sizeof(info->net_ip), "127.0.0.1");
  info->net_rssi_pct = 78;   info->net_rssi_valid = 1;
  info->net_band_ghz10 = 50; info->net_band_valid = 1;
  snprintf(info->tv_name, sizeof(info->tv_name), "Testanzeige");
  info->tv_width = 1920; info->tv_height = 1080;
  info->tv_resolution_valid = 1;
}

#endif
