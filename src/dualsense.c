/* DualSense battery.
 *
 * Sony does not document the struct scePadReadState() fills, but the DualSense
 * itself is documented: Linux' hid-playstation driver defines the status byte
 * of the controller's input report as
 *
 *     bits 0-3   battery capacity, 0..10
 *     bits 4-7   charging state, 0 = discharging, 1 = charging, 2 = full
 *
 * and converts it with  percent = min(capacity * 10 + 5, 100).
 *
 * That decode is not a guess. What we do not know is *where* in Sony's struct
 * that byte ends up, so it is located rather than assumed: a candidate must
 * satisfy both nibble constraints and, crucially, must not change between two
 * reads a moment apart. A battery does not move in half a second; sticks,
 * timers and sequence counters do, and that is what eliminates them.
 *
 * The located offset is reported alongside the value, so a single reading from
 * a full and an empty controller pins it permanently and this search can be
 * replaced by a constant.
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

#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>       /* open — the HID device nodes are read directly */
#include <stdatomic.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>      /* atoi, strtoul, free — for the log-based battery */
#include <sys/stat.h>    /* mkdir — the battery reading is kept on disk */
#include <string.h>
#include <pthread.h>
#include <unistd.h>

#include "klogown.h"
#include "ps5tm.h"
#include "dynsym.h"

/* Once the offset is confirmed on hardware, set this and the search is
   skipped entirely. Negative means "not yet established". */
#define DS_STATUS_OFFSET (-1)

#define PAD_STATE_BYTES 256

/* Lightbar routing state.
  g_lightbar_supported: -1 unavailable, 0 unknown, 1 confirmed working. */
static int g_lightbar_supported = 0;
static int g_lightbar_enabled   = 0;
static int g_lightbar_state     = 0;
static int g_lightbar_applied   = -1;
#ifndef PS5TM_HOST_TEST
static int g_lightbar_logged_unavailable = 0;
#endif
static pthread_mutex_t g_lightbar_lock = PTHREAD_MUTEX_INITIALIZER;

#ifndef PS5TM_HOST_TEST


/* Weakly linked, not looked up by name.
 *
 * These four used to go through dlsym(RTLD_DEFAULT, "scePadInit") and friends,
 * and they never resolved — Sony's modules export by NID, not by name, so a
 * name lookup cannot reach them. libScePad was not even in the link line. The
 * pointers therefore stayed null and the controller reported "not connected"
 * for as long as the feature has existed, on 01.08.2026 confirmed while a game
 * was being played with that very controller.
 *
 * ⚠ It also invalidated a test. On the same day the pad probe was switched on
 * with a game running to see whether it froze the console; it did not, and the
 * probe looked innocent. It was not exonerated — nothing had been executed.
 * The first real test of this code is still outstanding.
 *
 * Weak linking is the mechanism that works: the linker binds through the SDK
 * stub, and a symbol missing at run time leaves a null pointer instead of
 * killing the payload before main(). Same pattern as profile.c. */
extern int scePadInit(void) __attribute__((weak));
extern int scePadOpen(int user_id, int type, int index, const void *param)
    __attribute__((weak));
extern int scePadClose(int handle) __attribute__((weak));

/* ⚠ Two arguments, not three. Ours used to be declared
 *     scePadReadState(int handle, void *data, int count)
 * which is the signature of scePadRead — a different function. Corrected from
 * etaHEN's daemon/include/pad.hpp, which declares both side by side:
 *     int32_t scePadReadState(int32_t handle, OrbisPadData *pData);
 *     int32_t scePadRead(int32_t handle, OrbisPadData *pData, int32_t num);
 * The stray third argument was harmless on this ABI, but it meant nobody
 * reading the code could tell which of the two was actually being called. */
extern int scePadReadState(int handle, void *data) __attribute__((weak));

/* Undocumented, and the reason this is worth trying at all: etaHEN declares
 * it right next to the other pad functions, which suggests a process must ask
 * for pad access before scePadOpen() will grant it — and this payload is not
 * an application. **The argument is not documented anywhere**: etaHEN only
 * declares the function and never calls it, and no object in its bundled
 * libSDL2d.a references it either. 1 is therefore a guess, and it is treated
 * as one — the return value is logged and nothing depends on it succeeding. */
extern int scePadSetProcessPrivilege(int priv) __attribute__((weak));

/* The way in that does not require opening anything.
 *
 * scePadOpen() has never succeeded from this payload — 0x809B0001 before the
 * symbols were bound properly, 0x809B0081 since. That is consistent with the
 * pad already being open: the system holds it, and a second opener is refused.
 *
 * scePadGetHandle() asks for the handle of a controller that is *already* open
 * instead of opening one. PS4 homebrew has used this for years, and it is the
 * one function in libScePad that fits our situation exactly.
 *
 * Signature from the OpenOrbis PS4 toolchain documentation:
 *     int scePadGetHandle(int userID, uint32_t type, uint32_t index)
 *
 * ✔ Safe to call even if that were wrong: three integers in, one integer out,
 * not a single pointer anywhere. There is nothing for a mismatched argument to
 * dereference. That is what separates this from sceHidControlGetBatteryState,
 * whose unknown signature may well contain an output pointer.
 *
 * ⚠ A handle obtained this way is **borrowed and must never be closed**. It
 * belongs to whichever process opened it — during a game, that is the game.
 * Calling scePadClose() on it would take the controller away from the player.
 * See the `borrowed` flag at the call site.
 *
 * ── Tried on hardware, 02.08.2026: refused ───────────────────────────────
 *     scePadGetHandle(0x18161531, 0, 0)  ->  0x80920008
 *
 * That is not an accident of our arguments. Ghostpad names this exact value
 * for this exact function:
 *
 *   "SceShellUI is authid-protected, SceShellCore and others always return
 *    0x80920008 for both Open and GetHandle."
 *
 * We are one of the "others". A borrowed handle is refused on the same grounds
 * as opening one: the caller's process identity, which no argument can change.
 * Ghostpad's own answer is to PT_ATTACH to SceRemotePlay and call from inside
 * it — process injection, deliberately out of scope for a fan controller.
 *
 * The call is kept because it is the evidence, but it is attempted **once**
 * per run. See g_pad_refused. */
extern int scePadGetHandle(int user_id, uint32_t type, uint32_t index)
    __attribute__((weak));
extern int scePadSetLightBar(int handle, const void *param)
    __attribute__((weak));
extern int scePadResetLightBar(int handle) __attribute__((weak));

typedef struct {
  unsigned char r;
  unsigned char g;
  unsigned char b;
  unsigned char reserved;
} sce_pad_color_t;

/* Both routes to a handle are refused on process identity, so nothing about
   this console will change between one probe cycle and the next. Retrying
   every few seconds would be pure waste. */
static int g_pad_refused = 0;

static int g_ready = 0;

/* What resolve() found out about the HID and Bluetooth modules, kept for the
 * diagnostics page. Written once, on the probe thread; read by request threads,
 * hence atomic. The page reads these instead of looking the symbols up itself
 * because a lookup is a dlopen(), and a request thread must never load a Sony
 * module: loading takes the runtime linker's lock, which the whole process
 * shares, and on FW 12.00 it has been seen not to come back (see the note at
 * the top of this file). */
static atomic_int g_diag_hid_resolved = 0;
static atomic_int g_diag_hid_battery  = 0;
static atomic_int g_diag_hid_init     = 0;
static atomic_int g_diag_bt_init      = 0;

/* After a failed lightbar attempt the service waits before asking again. The
 * pad has been refused on every console so far, and each attempt is three calls
 * into Sony services under the Sony lock — made once a second, for as long as
 * the lightbar is switched on, that is a steady knocking on a door that stays
 * shut. A slow retry keeps a later hot-plug of a controller working. Where the
 * lightbar did work earlier, a failure usually means the controller was
 * switched off, and the wait is shorter. Probe thread only. */
#define LIGHTBAR_RETRY_REFUSED_SEC 60
#define LIGHTBAR_RETRY_LOST_SEC    10
static uint64_t g_lightbar_retry_ms = 0;     /* monotonic; 0 = not waiting */

static void
resolve(void) {
  if(g_ready) return;
  g_ready = 1;

  /* The user service is not looked up here. profile.c links those symbols
     weakly, and a name that is weakly declared anywhere must never also be
     passed to dlsym: the unresolved stub answers to its own name and returns
     an address that is neither NULL nor callable. */
  ps5tm_user_service_init();

  /* The return value used to be discarded. With scePadOpen() answering
     0x809B0001 on this console both with and without a game running, a failed
     init is one of the two remaining explanations — and one line of log tells
     them apart. */
  int init_rc = scePadInit ? scePadInit() : 1;

  /* Before opening, ask for pad access. See the declaration: the argument is
     a guess, so its answer is recorded rather than relied upon. */
  int priv_rc = scePadSetProcessPrivilege ? scePadSetProcessPrivilege(1) : 1;

  /* Is libSceHidControl reachable at all?
   *
   * Sony's own code reads the battery through it — the log shows
   * "getHidBatteryState: sceHidControlGetBatteryState() failed" — and it is a
   * different subsystem from scePad, which refuses a payload outright. The
   * SDK ships no stub for it, so the linker cannot help; the only way in is
   * the NID, which prospero-nid computes as akmC1nXbrLE.
   *
   * ‼ This resolves and reports. It does **not** call. The signature is
   * unknown — the log says only that it takes a device id — and invoking a
   * function with guessed arguments can write through a garbage pointer.
   * Knowing whether the door exists is worth one log line; opening it blind
   * is not. */
  {
    static const char *HID = "libSceHidControl.sprx";
    void *batt = ps5tm_dynsym_in(0, HID, "sceHidControlGetBatteryState");
    const char *how = ps5tm_dynsym_route_name();
    void *init = ps5tm_dynsym_in(0, HID, "sceHidControlInit");
    void *list = ps5tm_dynsym_in(0, HID, "sceHidControlGetDeviceList");
    void *bt   = ps5tm_dynsym_in(0, "libSceBluetoothHid.sprx",
                                 "sceBluetoothHidInit");

    PS5TM_INFO("hid_probe",
               "libSceHidControl: GetBatteryState %s (Weg: %s), Init %s, "
               "GetDeviceList %s. Nur aufgelöst, nicht aufgerufen.",
               batt ? "gefunden" : "fehlt", how,
               init ? "gefunden" : "fehlt",
               list ? "gefunden" : "fehlt");

    /* For the diagnostics page, which only reports what is found here. */
    atomic_store(&g_diag_hid_battery, batt != NULL);
    atomic_store(&g_diag_hid_init,    init != NULL);
    atomic_store(&g_diag_bt_init,     bt   != NULL);
    atomic_store(&g_diag_hid_resolved, 1);
  }

  /* Is there a device node that yields a raw HID report?
   *
   * The fan is driven through /dev/icc_fan — a plain open() and ioctl(), no
   * Sony library involved. If the controller were reachable the same way, the
   * whole problem would dissolve: the DualSense input report layout is public
   * (Linux' hid-playstation driver documents the status byte), so a raw report
   * would be decodable without any API at all.
   *
   * ── Answered on hardware, 02.08.2026 ─────────────────────────────────────
   * /dev holds bluetooth_hid, hid, bt, wlanbt, usb, usbc and usbctl.
   *
   * /dev/bluetooth_hid and /dev/hid both **open without complaint** — access
   * is not the obstacle. But read() on either returns errno 19, which on
   * FreeBSD is ENODEV, "Operation not supported by device". They are ioctl-only
   * nodes, exactly like /dev/icc_fan.
   *
   * So the door is unlocked and we do not know the knock. Learning it means
   * reading the ioctl numbers out of libSceBluetoothHid — the same decrypted
   * binary that would also settle the signature of
   * sceHidControlGetBatteryState. Three different routes, one missing file.
   *
   * Guessing ioctl numbers is not an option here. On a Bluetooth driver a
   * wrong command can disturb the live controller connection, and this payload
   * runs in a borrowed system process with escalated privileges.
   *
   * The check is kept because it is cheap and it re-confirms itself on every
   * firmware — but it reports one line, not three. bt and wlanbt are left
   * alone on purpose: they look like the Bluetooth stack's own control nodes,
   * and an exclusive-open driver would kick whatever already holds them, which
   * here is the controller connection itself. */
  {
    int n_nodes = 0;
    DIR *d = opendir("/dev");
    if(d) {
      struct dirent *e;
      while((e = readdir(d)))
        if(strcasestr(e->d_name, "hid") || strcasestr(e->d_name, "pad"))
          n_nodes++;
      closedir(d);
    }

    static const char *nodes[] = { "/dev/bluetooth_hid", "/dev/hid" };
    char   verdict[160] = {0};
    size_t used = 0;
    for(size_t i = 0; i < sizeof(nodes) / sizeof(nodes[0]); i++) {
      const char *how;
      int fd = open(nodes[i], O_RDONLY | O_NONBLOCK);
      if(fd < 0) {
        how = "kein Zugriff";
      } else {
        unsigned char buf[64];
        errno = 0;                 /* read() == 0 leaves errno untouched, and
                                      a stale value from an earlier call would
                                      be read as this call's answer */
        ssize_t got = read(fd, buf, sizeof(buf));
        int     err = errno;
        close(fd);
        /* ENODEV is the measured answer and means "talks via ioctl only".
           Anything else would be news. */
        how = got > 0          ? "liefert Daten"
            : got == 0         ? "offen, aber leer"
            : err == ENODEV    ? "nur über Steuerbefehle"
                               : "offen, aber stumm";
      }
      used += (size_t)snprintf(verdict + used, sizeof(verdict) - used,
                               "%s%s: %s", used ? ", " : "", nodes[i], how);
      if(used >= sizeof(verdict)) break;
    }

    PS5TM_INFO("dev_hid_scan",
               "Controller-Gerätedateien: %d vorhanden — %s.", n_nodes, verdict);
  }

  PS5TM_INFO("pad_resolved",
             "Controller-Abfrage: Init %s (0x%08X), Zugriffsrecht %s (0x%08X), "
             "Open %s, ReadState %s.",
             scePadInit ? "bereit" : "fehlt", (unsigned)init_rc,
             scePadSetProcessPrivilege ? "bereit" : "fehlt", (unsigned)priv_rc,
             scePadOpen      ? "bereit" : "fehlt",
             scePadReadState ? "bereit" : "fehlt");
}

static int
pad_open_or_borrow(int *borrowed_out) {
  int user_id = -1;
  if(ps5tm_user_service_active_user(&user_id) != 0 || user_id <= 0)
    user_id = 0xFF;

  int borrowed = 0;
  int handle = scePadOpen ? scePadOpen(user_id, 0, 0, NULL) : -1;
  if(handle < 0 && scePadGetHandle) {
    int got = scePadGetHandle(user_id, 0, 0);
    if(got >= 0) {
      handle = got;
      borrowed = 1;
    }
  }

  if(borrowed_out) *borrowed_out = borrowed;
  return handle;
}

static int
lightbar_apply_internal(int state) {
  if(state < 0) state = 0;
  if(state > 2) state = 2;

  if(!scePadSetLightBar || (!scePadOpen && !scePadGetHandle)) {
    if(!g_lightbar_logged_unavailable) {
      g_lightbar_logged_unavailable = 1;
      PS5TM_WARN("pad_lightbar_unavailable",
                 "Prospero Lights nicht verfügbar (ScePad-Funktionen fehlen).");
    }
    return -1;
  }

  int borrowed = 0;
  int handle = pad_open_or_borrow(&borrowed);
  if(handle < 0) {
    if(!g_lightbar_logged_unavailable) {
      g_lightbar_logged_unavailable = 1;
      PS5TM_WARN("pad_lightbar_refused",
                 "Prospero Lights abgelehnt (0x%08X).",
                 (unsigned)handle);
    }
    return -1;
  }

  sce_pad_color_t color = {0, 0, 0, 0};
  if(state == 0) { color.r = 24;  color.g = 120; color.b = 24; }
  if(state == 1) { color.r = 148; color.g = 96;  color.b = 0; }
  if(state == 2) { color.r = 170; color.g = 12;  color.b = 12; }

  int rc = scePadSetLightBar(handle, &color);
  if(rc < 0 && state == 0 && scePadResetLightBar)
    rc = scePadResetLightBar(handle);

  if(!borrowed && scePadClose) scePadClose(handle);
  return rc;
}

#endif  /* !PS5TM_HOST_TEST */


/* Does this byte decode as a DualSense status byte at all? */
static int
looks_like_status(unsigned char b) {
  unsigned capacity = b & 0x0Fu;
  unsigned charging = (b >> 4) & 0x0Fu;
  if(capacity > 10) return 0;
  /* 0/1/2 are the documented states; 0xA and 0xB are error codes the driver
     treats as "unknown", and everything else is not a status byte at all. */
  return charging <= 2;
}

/* The documented conversion, unchanged. */
static void
decode_status(unsigned char b, ps5tm_pad_t *out) {
  unsigned capacity = b & 0x0Fu;
  unsigned charging = (b >> 4) & 0x0Fu;

  if(charging == 2) out->battery_pct = 100;             /* fully charged */
  else {
    int pct = (int)capacity * 10 + 5;
    out->battery_pct = pct > 100 ? 100 : pct;
  }
  out->battery_valid = 1;

  out->charging       = (charging == 1);
  out->full           = (charging == 2);
  out->charging_valid = 1;
  out->status_byte    = b;
}


/* Picks the offset whose byte satisfies the layout in both samples and is
   identical across them. Ambiguity is reported rather than hidden: when
   several offsets qualify, the count travels with the answer so the later
   calibration knows it still has work to do. */
static int
locate_status(const unsigned char *a, const unsigned char *b, int len,
              int *candidates_out) {
  int found = -1, count = 0;

  for(int i = 0; i < len; i++) {
    if(a[i] != b[i])            continue;   /* moved: not a battery */
    if(!looks_like_status(a[i])) continue;
    /* An all-zero byte reads as "empty and discharging", which is also what
       padding looks like. Padding is far more likely, and a controller that
       flat is about to disconnect anyway. */
    if(a[i] == 0x00)            continue;

    count++;
    if(found < 0) found = i;
  }

  if(candidates_out) *candidates_out = count;
  return found;
}


static void
pad_probe(ps5tm_pad_t *out) {
  memset(out, 0, sizeof(*out));
  out->status_offset = -1;

#ifdef PS5TM_HOST_TEST
  /* 0x16 = charging, capacity 6 -> 65 %. */
  out->connected = 1;
  out->status_offset = 52;
  out->candidates = 1;
  decode_status(0x16, out);
#else
  resolve();
  if(!scePadOpen || !scePadClose || !scePadReadState) {
    out->probe_rc = -1;
    return;
  }
  if(g_pad_refused) { out->probe_rc = g_pad_refused; return; }

  int user_id = -1;
  /* 0xFF ("any user") is a last resort, not a plan: scePadOpen() answered it
     with 0x809B0001 on this console. The real id comes from the foreground
     user — see ps5tm_user_service_active_user(). */
  if(ps5tm_user_service_active_user(&user_id) != 0 || user_id <= 0)
    user_id = 0xFF;

  int handle   = scePadOpen(user_id, 0, 0, NULL);
  int open_rc  = handle;
  int borrowed = 0;

  /* Opening is refused, so ask for the handle the system already holds. See
     the declaration for why this is safe and why it must not be closed. */
  int get_rc = 0;
  if(handle < 0 && scePadGetHandle) {
    get_rc = scePadGetHandle(user_id, 0, 0);
    if(get_rc >= 0) { handle = get_rc; borrowed = 1; }
  }

  /* Once, not on every probe cycle: which user id was passed and how each of
     the two routes answered. */
  static int logged_open = 0;
  if(!logged_open) {
    logged_open = 1;
    char lend[32];
    if(!scePadGetHandle) snprintf(lend, sizeof(lend), "Funktion fehlt");
    else                 snprintf(lend, sizeof(lend), "0x%08X", (unsigned)get_rc);
    PS5TM_INFO("pad_open",
               "Benutzer %d / 0x%X — Öffnen ergab 0x%08X, Ausleihen ergab %s. %s",
               user_id, (unsigned)user_id, (unsigned)open_rc, lend,
               borrowed ? "Geliehene Kennung wird benutzt und nicht geschlossen."
                        : "Beide Wege abgelehnt – wird nicht wiederholt.");
  }
  if(handle < 0) {
    /* Refused on process identity: nothing will change while this payload
       runs, so stop asking. */
    g_pad_refused = handle;
    out->probe_rc = handle;
    return;
  }

  unsigned char first[PAD_STATE_BYTES], second[PAD_STATE_BYTES];
  memset(first, 0, sizeof(first));
  memset(second, 0, sizeof(second));

  int rc = scePadReadState(handle, first);
  if(rc >= 0) {
    usleep(400000);                          /* long enough for noise to move */
    rc = scePadReadState(handle, second);
  }
  /* ⚠ Only a handle we opened ourselves is ours to close. A borrowed one
     belongs to the process that opened it — closing it during a game would
     disconnect the player's controller. */
  if(!borrowed) scePadClose(handle);

  out->probe_rc = rc;
  if(rc < 0) return;
  out->connected = 1;

  memcpy(out->raw, first, sizeof(out->raw));
  out->raw_len = (int)sizeof(out->raw);

  int offset = DS_STATUS_OFFSET;
  if(offset < 0)
    offset = locate_status(first, second, PAD_STATE_BYTES, &out->candidates);
  else
    out->candidates = 1;

  if(offset >= 0 && offset < PAD_STATE_BYTES) {
    out->status_offset = offset;
    decode_status(first[offset], out);
  }
#endif
}


/* ─────────────────── the route that actually works ───────────────────────
 *
 * The console writes the battery level into its own kernel log, and reading
 * that needs no permission at all:
 *
 *     #LOGIN MGR# Battery status changed.
 *     Battery status : 0
 *     Battery level : 85
 *     #LOGIN MGR# The battery level of 0x4d0300 is getting low.
 *
 * Measured on 01.08.2026 with two controllers, one nearly full and one nearly
 * empty: **85** and **5**. So the figure is a straight percentage, 0..100 —
 * not the 0..10 index that Linux's hid-playstation driver uses and that this
 * project had assumed. The "getting low" line accompanied the 5.
 *
 * This is the same trick the game detection uses, and for the same reason:
 * scePadOpen() refuses a payload outright (0x809B0081, see the notes above),
 * while the log is just there for the reading. Sony's own code shows why the
 * detour exists at all — the log is full of
 *     getHidBatteryState: sceHidControlGetBatteryState() failed, ret=0x803b0003
 * so even the system does not always get an answer from the direct call.
 *
 * ⚠ Two limits worth knowing:
 *   - The value appears when a controller is switched on, not continuously.
 *     What is reported is therefore the last figure seen, with its age; a
 *     number presented as current would be a lie.
 *   - The device id changes on every connection (0x4b0302 → 0x4d0300 for the
 *     same physical controller), so it cannot identify one across sessions.
 */
static int
battery_from_log(int *level_out, int *status_out, unsigned *devid_out,
                 int *press_count_out, unsigned *press_id_out,
                 int *level_count_out) {
#ifdef PS5TM_HOST_TEST
  (void)level_out; (void)status_out; (void)devid_out;
  (void)press_count_out; (void)press_id_out; (void)level_count_out;
  return -2;
#else
  size_t len = 0;
  char  *buf = ps5tm_msgbuf_read(&len);
  /* ‼ -2, not -1, and the distinction matters.
   *
   * -1 means "the log was read and holds no battery figure" — then the two
   * counters below are valid and say zero. **-2 means the log was not read at
   * all**, and the counters are meaningless.
   *
   * Handing a meaningless zero to the caller would poison both counters: the
   * next successful poll would see the count jump from 0 to its real value and
   * report a button press that never happened *and* a fresh measurement that
   * never happened. That is precisely the false-freshness bug fixed in 1.25.4,
   * re-entering through a side door. */
  if(!buf) return -2;

  /* The buffer is chronological, so the last hit is the newest.
   *
   * ⚠ The device id is deliberately not read here any more. It only appears
   * in the separate "The battery level of 0x… is getting low" line, which is
   * emitted only for a low battery — so pairing it with the level meant
   * pairing two unrelated events. On 01.08.2026 that produced a reading of
   * 85 % carrying the id of the *empty* controller from an earlier session.
   * The id changes on every connection anyway and identifies nothing, so the
   * fix is to drop it rather than to correlate it. */
  int level = -1, status = -1;

  /* Presses of the PS button, counted in the same pass.
   *
   * Tested 02.08.2026: opening the control centre does **not** write a battery
   * line, even though the console shows the percentage there. The check was
   * valid — the very same capture contains
   *     [SceShellUI] W/RNPS.rnps-control-center.NPXS40003 : JS thread was busy
   * so the log does see the control centre; it simply reads the battery over a
   * path that never reaches the log.
   *
   * What it does write, once per press, is
   *     [LoginMgr] [onPSButtonPressed] value={"DeviceId":1245952,"UserId":…}
   *
   * That is worth having on its own. scePad cannot tell us whether a
   * controller is connected — it refuses this process outright — so "connected"
   * has been permanently false and meaningless. A press is proof that someone
   * is holding a live controller. */
  static const char PRESS[] = "[onPSButtonPressed] value={\"DeviceId\":";
  int      presses = 0;
  unsigned press_id = 0;

  int levels = 0;

  /* Switch on the first character before comparing.
   *
   * This walks roughly 100 KB every time it runs, and the naive form called
   * three strncmp() per byte — the longest of them 37 characters. Checking one
   * character first skips all three for well over 90 % of the buffer, and it
   * cannot change the outcome: each pattern can only match where its own first
   * character sits. */
  for(const char *p = buf; *p; p++) {
    if(*p == 'B') {
      if(!strncmp(p, "Battery level : ", 16) && !ps5tm_klog_own_line(buf, p)) {
        level = atoi(p + 16);
        levels++;
      }
      else if(!strncmp(p, "Battery status : ", 17) && !ps5tm_klog_own_line(buf, p))
        status = atoi(p + 17);
    }
    else if(*p == '[' && !strncmp(p, PRESS, sizeof(PRESS) - 1) && !ps5tm_klog_own_line(buf, p)) {
      presses++;
      press_id = (unsigned)strtoul(p + sizeof(PRESS) - 1, NULL, 10);
    }
  }
  free(buf);

  if(press_count_out) *press_count_out = presses;
  if(press_id_out)    *press_id_out    = press_id;
  if(level_count_out) *level_count_out = levels;

  if(level < 0 || level > 100) return -1;
  if(level_out)  *level_out  = level;
  if(status_out) *status_out = status;
  (void)devid_out;
  return 0;
#endif
}


/* Opening the pad and waiting 400 ms between two reads is far too slow for a
   request, and scePadOpen may block outright when no controller is paired.
   Both therefore happen on the background thread; requests read the cache. */
static ps5tm_pad_t     g_pad;
static pthread_mutex_t g_pad_lock = PTHREAD_MUTEX_INITIALIZER;

/* The reading outlives the process, because otherwise it usually does not
 * exist at all.
 *
 * The level is only in the kernel log while a controller connection is still
 * in the ring buffer, and the buffer turns over in minutes. Every restart of
 * this payload — after a console reboot, after any new build — therefore
 * started with an empty field until someone switched a controller on again.
 *
 * Keeping the last figure on disk with the time it was taken means the age
 * shown after a restart is the real elapsed time, not a fiction. "85 %,
 * yesterday evening" is a useful thing to be told; a blank card is not, and a
 * figure without an age would be a lie. */
#define PAD_BATTERY_PATH PS5TM_DATA_DIR "/pad-battery.txt"

/* Runs on the probe thread only, so unlike the files the fan thread writes it
   may fsync: a console switched off right after a change would otherwise keep
   a renamed but empty file. A file that came up short — full drive, failed
   close — never replaces the good one, and the failure is reported once per
   streak. */
static void
battery_store(int level, int status, uint64_t seen_ms) {
  static int failed_logged = 0;

  mkdir(PS5TM_DATA_DIR, 0755);
  char tmp[sizeof(PAD_BATTERY_PATH) + 8];
  snprintf(tmp, sizeof(tmp), "%s.tmp", PAD_BATTERY_PATH);

  int ok  = 0;
  int eno = 0;
  FILE *f = fopen(tmp, "w");
  if(!f) {
    eno = errno;
  } else {
    fprintf(f, "%d %d %llu\n", level, status, (unsigned long long)seen_ms);
    ok  = (fflush(f) == 0 && !ferror(f) && fsync(fileno(f)) == 0);
    eno = errno;
    if(fclose(f) != 0 && ok) { ok = 0; eno = errno; }
    if(ok && rename(tmp, PAD_BATTERY_PATH) != 0) { ok = 0; eno = errno; }
    if(!ok) unlink(tmp);
  }

  if(ok) {
    failed_logged = 0;
  } else if(!failed_logged) {
    failed_logged = 1;
    PS5TM_WARN("pad_battery_save_failed",
               "Der Controller-Ladestand konnte nicht gespeichert werden "
               "(Fehler %d: %s) – die vorhandene Datei bleibt unverändert.",
               eno, strerror(eno));
  }
}


void
ps5tm_pad_load(void) {
  FILE *f = fopen(PAD_BATTERY_PATH, "r");
  if(!f) return;

  int level = -1, status = -1;
  unsigned long long seen = 0;
  int got = fscanf(f, "%d %d %llu", &level, &status, &seen);
  fclose(f);
  if(got != 3 || level < 0 || level > 100) return;

  pthread_mutex_lock(&g_pad_lock);
  g_pad.battery_pct     = level;
  g_pad.battery_valid   = 1;
  g_pad.from_log        = 1;
  g_pad.restored_from_disk = 1;
  g_pad.charging        = (status == 1);
  g_pad.full            = (status == 2);
  g_pad.battery_seen_ms = (uint64_t)seen;
  pthread_mutex_unlock(&g_pad_lock);

  PS5TM_INFO("pad_battery_restored",
             "Letzter Controller-Ladestand übernommen: %d %%.", level);
}

void
ps5tm_pad_refresh(void) {
  ps5tm_pad_t fresh;
  ps5tm_sony_api_lock();
  pad_probe(&fresh);
  ps5tm_sony_api_unlock();

  /* The log route first: it is the one that answers. The scePad attempt above
     stays because it costs nothing and would be the better source the day it
     starts working — but it has never yet returned a handle on this console. */
  int level = -1, status = -1;
  unsigned devid = 0;
  int      presses  = 0;
  unsigned press_id = 0;
  int      levels   = 0;
  int      log_rc   = battery_from_log(&level, &status, &devid,
                                       &presses, &press_id, &levels);

  /* Has a new press appeared since the previous poll?
   *
   * The log carries no timestamps, so the count is the clock: the buffer holds
   * whatever fits, and any change in how many presses it contains means the
   * console logged one in the meantime. A shrinking count counts too — that is
   * old presses scrolling out while new ones arrive.
   *
   * The first poll establishes the baseline and claims nothing. Without that,
   * every restart of the payload would report a press that never happened. */
  const int scanned = (log_rc != -2);   /* -2: the log was never read */

  {
    static int prev_presses = -1;
    pthread_mutex_lock(&g_pad_lock);
    ps5tm_pad_t last = g_pad;
    pthread_mutex_unlock(&g_pad_lock);

    if(!scanned) {
      /* Nothing was read, so nothing is known. Carry the previous answer
         forward untouched and, above all, leave prev_presses alone — updating
         it with a zero that means "not measured" would fake a press on the
         next successful poll. */
      fresh.press_seen      = last.press_seen;
      fresh.press_device_id = last.press_device_id;
      fresh.last_press_ms   = last.last_press_ms;
    } else {
      uint64_t carried = last.last_press_ms;
      if(prev_presses >= 0 && presses != prev_presses)
        carried = ps5tm_now_ms();
      prev_presses = presses;

      fresh.press_seen      = presses > 0;
      fresh.press_device_id = press_id;
      fresh.last_press_ms   = carried;
    }
  }

  /* Is a new measurement in the log, or is it the same line still sitting in
   * the buffer?
   *
   * The age used to be tied to the *value* changing, which was wrong in a way
   * the user found on 02.08.2026: switching the controller off and on made the
   * console measure again, and the log duly gained a second
   *     #LOGIN MGR# Battery status changed. / Battery level : 75
   * — but because 75 equalled 75, the age went on climbing and claimed the
   * reading was 41 minutes old when it was two minutes old.
   *
   * So count the readings instead of comparing them. **Only an increase
   * counts.** The buffer is a ring: a falling count means old lines scrolled
   * out, not that anything was measured, and treating that as fresh would
   * bring back exactly the lie the old comment warned about — a stale figure
   * looking new. A value change still counts on its own, so a reading that
   * arrives while an old line drops out is not missed either.
   *
   * This sits outside the `log_rc == 0` branch on purpose. A log that was read
   * and holds no battery line at all is a real observation worth recording as
   * zero; leaving the counter at its old value would make the next reading fail
   * the "greater than" test and go unnoticed. Only an unread log is skipped. */
  int remeasured = 0;
  if(scanned) {
    static int prev_levels = -1;
    remeasured = (prev_levels >= 0 && levels > prev_levels);
    prev_levels = levels;
  }

  if(log_rc == 0) {
    pthread_mutex_lock(&g_pad_lock);
    int changed = (level != g_pad.battery_pct) || !g_pad.battery_valid;

    fresh.battery_seen_ms = (changed || remeasured) ? ps5tm_now_ms()
                                                    : g_pad.battery_seen_ms;
    pthread_mutex_unlock(&g_pad_lock);

    fresh.battery_pct   = level;
    fresh.battery_valid = 1;
    fresh.from_log      = 1;
    /* We now have a value from the live kernel log path for this runtime. */
    fresh.restored_from_disk = 0;
    fresh.charging      = (status == 1);
    fresh.full          = (status == 2);
    fresh.device_id     = devid;

    /* Only when something actually happened: the same line sits in the buffer
       for a while, and rewriting the file every fifteen seconds would be
       pointless wear. A re-measurement counts even at an unchanged value —
       its timestamp is new, and without storing it a restart would resurrect
       the old age. */
    if(changed || remeasured)
      battery_store(level, status, fresh.battery_seen_ms);
  } else {
    /* Nothing in the buffer this time — that is the normal state a few
       minutes after a controller was switched on. Keep what we had rather
       than blanking the card. */
    pthread_mutex_lock(&g_pad_lock);
    if(g_pad.battery_valid) {
      fresh.battery_pct     = g_pad.battery_pct;
      fresh.battery_valid   = 1;
      fresh.from_log        = 1;
      fresh.restored_from_disk = g_pad.restored_from_disk;
      fresh.charging        = g_pad.charging;
      fresh.full            = g_pad.full;
      fresh.battery_seen_ms = g_pad.battery_seen_ms;
    }
    pthread_mutex_unlock(&g_pad_lock);
  }

  pthread_mutex_lock(&g_pad_lock);
  g_pad = fresh;
  pthread_mutex_unlock(&g_pad_lock);
}

void
ps5tm_pad_get(ps5tm_pad_t *out) {
  pthread_mutex_lock(&g_pad_lock);
  *out = g_pad;
  pthread_mutex_unlock(&g_pad_lock);
}

void
ps5tm_pad_lightbar_request(int enabled, int state) {
  pthread_mutex_lock(&g_lightbar_lock);
  g_lightbar_enabled = enabled ? 1 : 0;
  if(state < 0) state = 0;
  if(state > 2) state = 2;
  g_lightbar_state = state;
  pthread_mutex_unlock(&g_lightbar_lock);
}

int
ps5tm_pad_lightbar_supported(void) {
  pthread_mutex_lock(&g_lightbar_lock);
  int ok = (g_lightbar_supported > 0) ? 1 : 0;
  pthread_mutex_unlock(&g_lightbar_lock);
  return ok;
}

#ifndef PS5TM_HOST_TEST
/* True while the wait after a failed attempt has not run out. */
static int
lightbar_waiting(void) {
  uint64_t now = ps5tm_mono_ms();
  return now && now < g_lightbar_retry_ms;
}

/* Called after every attempt, once g_lightbar_supported has been updated:
   arms the wait after a failure, clears it after a success. */
static void
lightbar_arm_retry(int rc) {
  uint64_t now = ps5tm_mono_ms();
  if(rc >= 0 || !now) {
    g_lightbar_retry_ms = 0;
    return;
  }

  pthread_mutex_lock(&g_lightbar_lock);
  int worked_before = (g_lightbar_supported == 1);
  pthread_mutex_unlock(&g_lightbar_lock);

  g_lightbar_retry_ms = now + (worked_before ? LIGHTBAR_RETRY_LOST_SEC
                                             : LIGHTBAR_RETRY_REFUSED_SEC)
                              * 1000ull;
}
#endif

void
ps5tm_pad_lightbar_service(void) {
  int enabled = 0;
  int wanted = 0;
  int applied = -1;

  pthread_mutex_lock(&g_lightbar_lock);
  enabled = g_lightbar_enabled;
  wanted  = g_lightbar_state;
  applied = g_lightbar_applied;
  pthread_mutex_unlock(&g_lightbar_lock);

  if(!enabled) {
    if(applied < 0) return;
    int rc = 0;
#ifndef PS5TM_HOST_TEST
    if(lightbar_waiting()) return;
    ps5tm_sony_api_lock();
    resolve();
    rc = lightbar_apply_internal(0);
    ps5tm_sony_api_unlock();
    lightbar_arm_retry(rc);
#endif
    if(rc < 0) return;
    pthread_mutex_lock(&g_lightbar_lock);
    g_lightbar_applied = -1;
    pthread_mutex_unlock(&g_lightbar_lock);
    return;
  }

  if(wanted == applied) return;

#ifndef PS5TM_HOST_TEST
  if(lightbar_waiting()) return;

  ps5tm_sony_api_lock();
  resolve();
  int rc = lightbar_apply_internal(wanted);
  ps5tm_sony_api_unlock();

  pthread_mutex_lock(&g_lightbar_lock);
  if(rc >= 0) {
    if(g_lightbar_supported <= 0)
      PS5TM_INFO("pad_lightbar_enabled",
                 "Prospero Lights aktiv (Status %d).", wanted);
    g_lightbar_supported = 1;
    g_lightbar_applied = wanted;
  } else {
    if(g_lightbar_supported == 0) g_lightbar_supported = -1;
  }
  pthread_mutex_unlock(&g_lightbar_lock);

  lightbar_arm_retry(rc);
#endif
}

void
ps5tm_pad_diag_get(ps5tm_pad_diag_t *out) {
  memset(out, 0, sizeof(*out));

#ifdef PS5TM_HOST_TEST
  out->host_test = 1;
#else
  out->host_test = 0;
  out->scepad_init = scePadInit != NULL;
  out->scepad_open = scePadOpen != NULL;
  out->scepad_read_state = scePadReadState != NULL;
  out->scepad_get_handle = scePadGetHandle != NULL;
  out->scepad_set_lightbar = scePadSetLightBar != NULL;
  out->scepad_reset_lightbar = scePadResetLightBar != NULL;
  out->scepad_set_process_privilege = scePadSetProcessPrivilege != NULL;

  out->dev_hid_present = access("/dev/hid", F_OK) == 0;
  out->dev_bluetooth_hid_present = access("/dev/bluetooth_hid", F_OK) == 0;

  /* Only what the controller probe has already found out — see the note at
     g_diag_hid_resolved. This runs on a request thread, and looking the
     symbols up here meant a dlopen() of two Sony modules from it, every time
     the page was opened. Nothing is loaded; until the probe has looked
     (hid_resolved is 0) the three flags are 0 and mean "not checked". */
  out->hid_resolved = atomic_load(&g_diag_hid_resolved);
  out->hidcontrol_get_battery_state = atomic_load(&g_diag_hid_battery);
  out->hidcontrol_init              = atomic_load(&g_diag_hid_init);
  out->bluetoothhid_init            = atomic_load(&g_diag_bt_init);

  out->cached_pad_refused_rc = g_pad_refused;
#endif
}
