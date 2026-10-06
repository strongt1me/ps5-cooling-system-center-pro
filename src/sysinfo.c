/* Slow-moving system facts: identity, uptime and storage.
 *
 * These change on a timescale of minutes, so the result is cached and shared
 * between requests instead of being re-measured for every dashboard poll —
 * statvfs() on a busy console is not free.
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

#include <ctype.h>
#include <dlfcn.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>     /* statfs(): names the mount a path belongs to */
#include <sys/statvfs.h>
#ifndef PS5TM_HOST_TEST
#include <sys/sysctl.h>   /* FreeBSD/PS5 only — absent on a Linux host build */
#endif
#include <sys/time.h>
#include <time.h>

#include <unistd.h>

#include "dynsym.h"
#include "ps5tm.h"

#define SYSINFO_TTL_MS 5000

int sceKernelGetHwModelName(char *out);
int sceKernelGetHwSerialNumber(char *out);
#ifndef PS5TM_HOST_TEST
#endif

/* What this firmware gives a payload, and what it does not.
 *
 * Operating hours, the boot counter, the power-up cause, Sony's thermal alert
 * and the page-table memory figures were all removed on 31.07.2026, after
 * being measured rather than guessed at. They are neither a permission
 * problem nor a lookup problem — the calls succeed and write nothing:
 *
 *   Betriebsstunden rc=0x00000000 wert=0 | Startzähler rc=0x00000000 wert=0
 *   RAM rc=0x00000000 total=0 frei=0     | AuthID 0x4801000000000013
 *
 * sceKernelIccGetThermalAlert does not exist on FW 12.00 at all; its weak
 * binding stayed null.
 *
 * The likeliest explanation is where the code they were copied from runs:
 * get_page_table_stats and the ICC counters come from etaHEN's shellui
 * sources, which are injected into SceShellUI. In that process they have a
 * data source; in a payload started by elfldr they do not, and they say so by
 * returning success with an untouched buffer. Reaching them would mean
 * injecting into ShellUI — a different and far riskier design than this app.
 *
 * Four rounds of work went into establishing that, so: the measurement above
 * is the answer, not a missing trick. Do not reintroduce these without new
 * evidence. An empty card that looks broken is worse than an honest absence.
 *
 * The software version is a separate case — it answers, and it stays. */
typedef int (*fn_ptr_out_t)(void *);

static fn_ptr_out_t p_SwVersion     = NULL;
static int          g_syms_resolved = 0;

#ifndef PS5TM_HOST_TEST
/* Weak, so a firmware that lacks it costs one field instead of the payload:
   a strong reference to a missing symbol makes the loader give up before
   main() runs — no port, no toast, no log. */
extern int sceKernelGetProsperoSystemSwVersion(void *) __attribute__((weak));
#endif

static void
resolve_optional_symbols(void) {
  if(g_syms_resolved) return;
  g_syms_resolved = 1;
#ifndef PS5TM_HOST_TEST
  p_SwVersion = sceKernelGetProsperoSystemSwVersion;
#endif
}


/* The system's own idea of its software version.
 *
 * The struct is undocumented; what is reliable is that a printable version
 * string sits inside it. Rather than guess at field offsets, scan the buffer
 * for the first thing shaped like a version and take that — and if nothing
 * looks right, report nothing instead of garbage. */
static void
read_sw_version(ps5tm_sysinfo_t *info) {
  if(!p_SwVersion) return;

  unsigned char buf[64];
  memset(buf, 0, sizeof(buf));
  if(p_SwVersion(buf) != 0) return;

  for(size_t i = 0; i + 4 < sizeof(buf); i++) {
    if(buf[i] < '0' || buf[i] > '9') continue;
    if(buf[i + 1] != '.' && buf[i + 2] != '.') continue;

    size_t n = 0;
    char   out[32];
    while(i + n < sizeof(buf) && n + 1 < sizeof(out) &&
          (isdigit(buf[i + n]) || buf[i + n] == '.')) {
      out[n] = (char)buf[i + n];
      n++;
    }
    out[n] = 0;
    if(n >= 4 && strchr(out, '.')) {
      snprintf(info->firmware_text, sizeof(info->firmware_text), "%s", out);
      info->firmware_text_valid = 1;
      return;
    }
  }
}

static ps5tm_sysinfo_t g_cache;
static uint64_t        g_cache_ms = 0;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;


/* Seconds since boot, from the kernel's recorded boot timestamp. */
static int
read_uptime(uint64_t *out) {
#ifdef PS5TM_HOST_TEST
  FILE *f = fopen("/proc/uptime", "r");
  if(!f) return -1;
  double up = 0;
  int got = fscanf(f, "%lf", &up);
  fclose(f);
  if(got != 1) return -1;
  *out = (uint64_t)up;
  return 0;
#else
  struct timeval boottime;
  size_t sz = sizeof(boottime);
  if(sysctlbyname("kern.boottime", &boottime, &sz, NULL, 0) != 0) return -1;

  struct timespec now;
  if(clock_gettime(CLOCK_REALTIME, &now) != 0) return -1;
  if(now.tv_sec <= boottime.tv_sec) return -1;

  *out = (uint64_t)(now.tv_sec - boottime.tv_sec);
  return 0;
#endif
}


/* Does the console have any USB storage attached at all?
 *
 * This used to ask the system service, via sceSystemServiceUsbStorageInit()
 * and …IsExist(). Both are IPC calls, and this function runs on the probe
 * thread — an IPC that does not come back there blocks every other thread the
 * moment it needs the runtime linker or the log, which is how the payload
 * came to stop dead one line into the bind loop. The very first line the
 * console prints for this process is already an LNC IPC failure
 * (getAppStatus: 0x80940004), so these services are not dependable here.
 *
 * The service was only ever an optimisation. Its documented fallback — the
 * mount-plus-size heuristic in add_volume() — is what ran whenever the call
 * failed, and it is enough on its own: a real mount point of at least the
 * minimum size, deduplicated by device. So the question is answered without
 * asking anyone. */
static int
usb_storage_present(void) {
  return 1;
}


static void
add_volume(ps5tm_sysinfo_t *info, const char *label, const char *path) {
  if(info->volume_count >= PS5TM_MAX_VOLUMES) return;

#ifndef PS5TM_HOST_TEST
  {
    struct statfs sf;
    if(statfs(path, &sf) != 0) return;
    if(strcmp(sf.f_mntonname, path) != 0) return;   /* not its own mount */

    /* Guard against the same device appearing twice under two names. */
    for(unsigned i = 0; i < info->volume_count; i++) {
      if(!strcmp(info->volumes[i].device, sf.f_mntfromname)) return;
    }
  }
#endif

  struct statvfs sv;
  if(statvfs(path, &sv) != 0 || sv.f_blocks == 0) return;

  uint64_t frsize = sv.f_frsize ? sv.f_frsize : sv.f_bsize;
  uint64_t total  = (uint64_t)sv.f_blocks * frsize;
  uint64_t avail  = (uint64_t)sv.f_bavail * frsize;
  uint64_t freeb  = (uint64_t)sv.f_bfree  * frsize;
  if(!total) return;

  /* An unused USB slot still answered with a 2 MB filesystem and appeared as
     a drive. No storage device a person would care about is that small, so a
     floor removes the remaining phantoms. */
  if(total < 256ull * 1024 * 1024) return;

  ps5tm_volume_t *v = &info->volumes[info->volume_count++];
  snprintf(v->label, sizeof(v->label), "%s", label);
  snprintf(v->path,  sizeof(v->path),  "%s", path);
#ifndef PS5TM_HOST_TEST
  {
    struct statfs sf;
    if(statfs(path, &sf) == 0)
      snprintf(v->device, sizeof(v->device), "%s", sf.f_mntfromname);
  }
#endif
  v->total_bytes = total;
  v->free_bytes  = avail;                 /* what a user can actually fill */
  v->used_bytes  = total - freeb;
}


static void
refresh(ps5tm_sysinfo_t *info) {
  memset(info, 0, sizeof(*info));
  resolve_optional_symbols();

  char model[256] = {0};
  if(sceKernelGetHwModelName(model) == 0 && model[0])
    snprintf(info->model, sizeof(info->model), "%s", model);
  else
    snprintf(info->model, sizeof(info->model), "PlayStation 5");

  /* The serial identifies the console, so only the tail is shown — enough to
     tell two machines apart, not enough to be worth harvesting. */
  char serial[256] = {0};
  if(sceKernelGetHwSerialNumber(serial) == 0 && serial[0]) {
    size_t n = strlen(serial);
    const char *tail = (n > 4) ? serial + n - 4 : serial;
    snprintf(info->serial_masked, sizeof(info->serial_masked), "••••%s", tail);
  }

  info->firmware = ps5tm_platform_firmware();

  if(read_uptime(&info->uptime_sec) == 0) info->uptime_valid = 1;

  /* The ICC counters and the page-table memory figures used to be read here.
     Both were measured to return success with an empty buffer on FW 12.00 —
     see the note above the symbol declarations. */

#ifdef PS5TM_HOST_TEST
  add_volume(info, "Interne SSD (Test)", "/");
  add_volume(info, "Systemdaten (Test)", "/tmp");
#else
  /* /data is deliberately absent: it lives on the same filesystem as /user
     and was being listed a second time with identical figures. */
  add_volume(info, "Interne SSD",     "/user");

  /* Both expansion mount points, not just the second. ext1 is the one an M.2
     card normally lands on and the only one this list used to know, so a
     console using ext0 showed no expansion at all. */
  add_volume(info, "M.2-Erweiterung",   "/mnt/ext0");
  add_volume(info, "M.2-Erweiterung 2", "/mnt/ext1");

  /* USB is asked about rather than guessed at. The old approach mounted-or-not
     could not tell an empty slot from a tiny filesystem, which is why an
     unplugged port once showed up as a 2 MB drive.
     All eight mount points are tried: a hub or a second drive put a device on
     usb2 and upwards, where nobody was looking. The cost of an absent one is
     a statfs() that fails immediately, and add_volume() drops it silently. */
  if(usb_storage_present()) {
    for(unsigned i = 0; i < 8; i++) {
      char label[40], path[40];
      snprintf(label, sizeof(label), "USB-Speicher %u", i + 1);
      snprintf(path,  sizeof(path),  "/mnt/usb%u", i);
      add_volume(info, label, path);
    }
  }

  /* Whether an M.2 slot is populated decides if its temperature channel
     means anything — see ps5tm_sysinfo_t.m2_present. Either slot counts. */
  for(unsigned i = 0; i < info->volume_count; i++) {
    if(!strcmp(info->volumes[i].path, "/mnt/ext0") ||
       !strcmp(info->volumes[i].path, "/mnt/ext1")) {
      info->m2_present = 1;
      break;
    }
  }
#endif

  /* sceKernelGetBasicProductShape reported 255 — the "unset" sentinel — and
     went the same way as the ICC counters. */
  read_sw_version(info);

  /* Gated like the other new probes: on the test console something in this
     area froze the whole process. See probe.c. */
  ps5tm_config_lock();
  unsigned mask = g_config.probe_mask;
  ps5tm_config_unlock();
  if(mask & PS5TM_PROBE_NETDISP) ps5tm_netdisp_fill(info);
}


/* Called only from the background probe thread. Everything in refresh() may
   block on a system service, which is exactly why no request may run it. */
void
ps5tm_sysinfo_refresh(void) {
  ps5tm_sysinfo_t fresh;
  refresh(&fresh);

  pthread_mutex_lock(&g_lock);
  g_cache    = fresh;
  g_cache_ms = ps5tm_now_ms();
  pthread_mutex_unlock(&g_lock);
}


void
ps5tm_sysinfo_get(ps5tm_sysinfo_t *out) {
  pthread_mutex_lock(&g_lock);
  *out = g_cache;                 /* never refreshes: see probe.c */
  pthread_mutex_unlock(&g_lock);
}


/* Measured on a PS5 Pro (CFI-7021, FW 12.00) on 26 and 27.09.2026: with the
 * fan held at 34 % and the CPU nearly idle, channel 7 was the only one that
 * followed the graphics rail more steeply than the channel mean (+0.115 ±
 * 0.019 K/W), and under a 100 W game it ran 4-5 °C above that mean on both
 * days. See platform.c for the rest.
 *
 * The standard and slim consoles carry a different chip, and nobody has
 * measured their channels — they keep the raw numbers only, rather than a
 * name borrowed from another model. Every Pro model number starts "CFI-7".
 * Before the first refresh the model is empty and the answer is no. */
int
ps5tm_sysinfo_gpu_channel_known(const ps5tm_sysinfo_t *info) {
  return info && !strncmp(info->model, "CFI-7", 5);
}
