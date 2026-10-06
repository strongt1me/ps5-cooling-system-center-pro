/* Hardware access: privilege escalation, thermal sensors, ICC fan controller. */

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#ifndef PS5TM_HOST_TEST
#include <sys/param.h>
#include <sys/cpuset.h>
#include <sys/sysctl.h>
#endif
#include <sys/types.h>
#include <unistd.h>

#include <ps5/kernel.h>

#include "ps5tm.h"


/* ---------------------------------------------------------------- syscalls
 * Exported by libkernel_sys. Signatures confirmed against the hwinfo sample
 * and the Elf Arsenal sources.
 *
 * All seven are declared weak, and every call below is guarded.
 *
 * They used to be plain (strong) declarations, which is a quiet trap: a strong
 * reference to a symbol the firmware does not export kills the payload in the
 * runtime linker, **before main()**. No banner, no log line, nothing to go on
 * — the same failure mode that once cost an evening on the installer ELF.
 * Verified on FW 12.00 they all exist, but that is one firmware out of the
 * range 3.00–12.70 the app claims to know, and the others are untested.
 *
 * Weak turns "the app does not start and nobody knows why" into "one reading
 * is missing and the log says which". Costs a null check per call.
 */
int  sceKernelGetCpuTemperature(int *out_celsius)                 __attribute__((weak));
int  sceKernelGetSocSensorTemperature(int channel, int *out_c)    __attribute__((weak));
int  sceKernelGetCurrentFanDuty(uint16_t *out_duty, void *scratch) __attribute__((weak));
int  sceKernelGetCpuUsageAll(int *per_core_pct, int *count_out)   __attribute__((weak));
long sceKernelGetCpuFrequency(void)                               __attribute__((weak));
int  sceKernelGetCpumode(void)                                    __attribute__((weak));
/* One argument since 1.45.0 — the reading itself lives in telemetry.c. */
int  sceKernelGetSocPowerConsumption(void *out)                   __attribute__((weak));

/* Page-table statistics — system memory and video memory, straight from the
 * kernel instead of scraped out of the log.
 *
 * Found in etaHEN's overlay (shellui/src/prx.cpp), declared there as a plain
 * extern "C" libkernel export, same family as the seven above:
 *
 *     int get_page_table_stats(int vm, int type, int *total, int *free);
 *
 * etaHEN calls it with vm = 1 and type = 1 for main memory, type = 2 for
 * video memory, and treats a failure as -1.
 *
 * ⚠ The unit is NOT taken on trust. etaHEN prints the figure as "%u MB", but
 * the same file also labels a VRAM fill level "GPU usage" and SoC channel 0
 * "GPU temperature" — neither of which is what it says. The raw numbers are
 * therefore logged once and the unit is read off their magnitude: this console
 * has 16 GB, so ~16000 means MB, ~4000000 means 4 KB pages, and anything huge
 * means bytes. */
int  get_page_table_stats(int vm, int type, int *total, int *free)
     __attribute__((weak));


/* Says once which of the seven the firmware actually provides. Without it a
   missing symbol is indistinguishable from a sensor that reads nothing. */
void
ps5tm_platform_log_sensor_symbols(void) {
  PS5TM_INFO("sensor_symbols",
             "Sensorfunktionen: Temperatur %s, Chipsensor %s, Drehzahl %s, "
             "Kernlast %s, Takt %s, Modus %s, Verbrauch %s.",
             sceKernelGetCpuTemperature       ? "da" : "FEHLT",
             sceKernelGetSocSensorTemperature ? "da" : "FEHLT",
             sceKernelGetCurrentFanDuty       ? "da" : "FEHLT",
             sceKernelGetCpuUsageAll          ? "da" : "FEHLT",
             sceKernelGetCpuFrequency         ? "da" : "FEHLT",
             sceKernelGetCpumode              ? "da" : "FEHLT",
             sceKernelGetSocPowerConsumption  ? "da" : "FEHLT");

  /* One-off measurement of the page-table figures, raw and unconverted.
     The point is to establish the unit from the magnitude before any code
     divides by 1024 on faith — see the declaration. */
  if(!get_page_table_stats) {
    PS5TM_INFO("pagetable_probe", "Speicherstatistik: Funktion fehlt.");
    return;
  }
  {
    int cu = 0, ct = 0, gu = 0, gt = 0;
    if(ps5tm_platform_page_stats(&cu, &ct, &gu, &gt) == 0)
      PS5TM_INFO("pagetable_probe",
                 "Speicherzuordnung: Prozessor %d von %d MB, Grafik %d von "
                 "%d MB.", cu, ct, gu, gt);
    else
      PS5TM_INFO("pagetable_probe", "Speicherzuordnung nicht abrufbar.");
  }
}


/* Page-table occupancy for the CPU and the GPU, in megabytes.
 *
 * ── How the arguments were established, 02.08.2026 ───────────────────────
 * etaHEN calls this with vm = 1. From here that returns success with every
 * figure zero: the call works, it just describes an address space this process
 * is not in — etaHEN runs inside SceShellUI, we run in a borrowed system
 * process. Sweeping vm 0..3 against type 0..4 found the pair that answers:
 *
 *     vm 0, type 1  ->  8192 total, 3298 free
 *     vm 0, type 2  ->  8192 total, 8059 free
 *
 * **vm = 0**, and the two types are the CPU and GPU page tables. That is not
 * inferred from etaHEN, whose labels have been wrong twice today — it is what
 * the console calls them itself, in the log line clocks.c already parses:
 *
 *     page table CPU <used>/<total> GPU <used>/<total>
 *
 * The unit is megabytes, and that too is measured rather than assumed: the two
 * totals are 8192 each, and this console has exactly 16384 MB.
 *
 * ⚠ This is mapped address space, not physical memory in use. The CPU table
 * sat at 4894 of 8192 MB on an idle console, which is far more than the system
 * was actually consuming. Anything user-facing has to say "zugeordnet", never
 * "belegt". */
int
ps5tm_platform_page_stats(int *cpu_used, int *cpu_total,
                          int *gpu_used, int *gpu_total) {
  if(!get_page_table_stats) return -1;

  int ct = 0, cf = 0, gt = 0, gf = 0;
  if(get_page_table_stats(0, 1, &ct, &cf) != 0) return -1;
  if(get_page_table_stats(0, 2, &gt, &gf) != 0) return -1;
  if(ct <= 0 || gt <= 0) return -1;

  if(cpu_total) *cpu_total = ct;
  if(cpu_used)  *cpu_used  = ct - cf;
  if(gpu_total) *gpu_total = gt;
  if(gpu_used)  *gpu_used  = gt - gf;
  return 0;
}


/* ------------------------------------------------------------------ ICC fan
 *
 * /dev/icc_fan exposes the fan controller. The knob we get is a *threshold*:
 * the temperature at which the firmware ramps the fan up. There is no ioctl
 * that sets a duty cycle directly — 0xC0068F06 only reads the current one.
 */
#define ICC_FAN_DEVICE          "/dev/icc_fan"
#define ICC_FAN_THRESHOLD_IOCTL 0xC01C8F07ul
#define ICC_FAN_CONFIG_IOCTL    0xC01C8F08ul
#define ICC_FAN_GET_MANUAL_DUTY 0xC0068F06ul

/* Both 0x8F ioctls carry the same block; size and threshold offset live in
   ps5tm.h so fan.c can size its own buffer. */
#define ICC_FAN_TARGET_OFFSET   PS5TM_ICC_FAN_TARGET_OFFSET

/* Both shipped third-party callers open the device with O_DIRECT set
   (fan_target passes the raw 0x10002, ShadowMountPlus writes it out). Keep
   the flag optional so a toolchain without it still builds. */
#ifdef O_DIRECT
#define ICC_FAN_OPEN_DIRECT O_DIRECT
#else
#define ICC_FAN_OPEN_DIRECT 0
#endif

/* Widest band the controller itself accepts, independent of the UI limits.
 *
 * The upper end was 90, taken from the reference payloads. The firmware
 * itself writes 91 at every change of state, and reading the controller back
 * returned 91 (24.09.2026, FW 12.00), so 91 is accepted and held. It matters
 * since 1.43.2: handing control back writes exactly that value, and a limit of
 * 90 would have quietly turned it into 90. Everything the user or the
 * controller sets is still bounded to PS5TM_THRESHOLD_MIN_C..MAX_C first. */
#define ICC_FAN_HW_MIN_C 30
#define ICC_FAN_HW_MAX_C 91

static atomic_int g_fan_available = 0;
static char       g_fan_message[224] = "Lüftersteuerung noch nicht geprüft.";


int
ps5tm_platform_fan_available(void) {
  return atomic_load(&g_fan_available);
}

const char *
ps5tm_platform_fan_message(void) {
  return g_fan_message;
}


int
ps5tm_platform_escalate(void) {
  return ps5tm_platform_escalate_as(PS5TM_AUTHID_FAN);
}


int
ps5tm_platform_escalate_as(unsigned long long authid) {
  pid_t pid = getpid();
  if(pid <= 0) return -1;

  if(!kernel_get_proc(pid)) {
    PS5TM_WARN("jb_unavailable",
               "Kernel-Zugriff nicht verfügbar – läuft der Payload über den "
               "Jailbreak (etaHEN/elfldr)?");
    return -1;
  }

  int rc = 0;
  if(kernel_set_ucred_uid  (pid, 0) != 0) rc = -1;
  if(kernel_set_ucred_ruid (pid, 0) != 0) rc = -1;
  if(kernel_set_ucred_svuid(pid, 0) != 0) rc = -1;
  if(kernel_set_ucred_rgid (pid, 0) != 0) rc = -1;
  if(kernel_set_ucred_svgid(pid, 0) != 0) rc = -1;

  intptr_t rootvnode = kernel_get_root_vnode();
  if(rootvnode) {
    if(kernel_set_proc_rootdir(pid, rootvnode) != 0) rc = -1;
    if(kernel_set_proc_jaildir(pid, rootvnode) != 0) rc = -1;
  }

  /* Without a matching authid the icc_fan open() is rejected even as uid 0,
     and the installer service rejects a package for the same reason — hence
     the caller choosing which identity it needs. */
  if(kernel_set_ucred_authid(pid, authid) != 0) rc = -1;
  uint8_t caps[16];
  memset(caps, 0xff, sizeof(caps));
  if(kernel_set_ucred_caps(pid, caps) != 0) rc = -1;

  /* cr_sceAttr[0] = 0x80 is the high-attribute flag. This SDK takes the
     whole 32-byte attribute block, so the rest must be zeroed explicitly. */
  uint8_t attrs[32];
  memset(attrs, 0, sizeof(attrs));
  attrs[0] = 0x80;
  if(kernel_set_ucred_attrs(pid, attrs) != 0) rc = -1;

  return rc;
}


int
ps5tm_platform_set_fan_threshold(int temp_c, int *errno_out) {
  if(errno_out) *errno_out = 0;
  if(temp_c < ICC_FAN_HW_MIN_C) temp_c = ICC_FAN_HW_MIN_C;
  if(temp_c > ICC_FAN_HW_MAX_C) temp_c = ICC_FAN_HW_MAX_C;

#ifdef PS5TM_HOST_TEST
  /* A host build has no ICC device. Accept the write and hand the value to
     the test rig, which models the firmware's response so the closed loop
     runs against real feedback. Never compiled into a payload build. */
  {
    extern int g_sim_threshold_c;
    g_sim_threshold_c = temp_c;
  }
  atomic_store(&g_fan_available, 1);
  snprintf(g_fan_message, sizeof(g_fan_message),
           "Host-Test: ICC-Schreibzugriff simuliert.");
  return 0;
#endif

  int fd = open(ICC_FAN_DEVICE, O_RDONLY);
  if(fd < 0) {
    int eno = errno;
    if(errno_out) *errno_out = eno;
    atomic_store(&g_fan_available, 0);
    snprintf(g_fan_message, sizeof(g_fan_message),
             "%s ist nicht zugänglich (errno %d: %s). kstuff muss geladen "
             "sein, damit der Lüfter gesteuert werden kann.",
             ICC_FAN_DEVICE, eno, strerror(eno));
    return -1;
  }

  /* The threshold byte lives at offset 5.
   *
   * The ioctl encodes _IOWR with size 0x1C (28), so the kernel copies 28
   * bytes IN and 28 bytes back OUT. The buffer must therefore be at least 28
   * bytes and fully zero-initialised. A shorter buffer hands uninitialised
   * stack to the fan controller and lets the copyout smash the caller's
   * stack; when that garbage is non-zero the threshold is ignored and the
   * fan falls back to the firmware default. This is the single most common
   * reason a PS5 fan-control payload "does nothing".
   */
  unsigned char data[PS5TM_ICC_FAN_CONFIG_SIZE] = {0};
  data[ICC_FAN_TARGET_OFFSET] = (unsigned char)temp_c;

  int rc  = ioctl(fd, ICC_FAN_THRESHOLD_IOCTL, data);
  int eno = errno;
  close(fd);

  if(rc != 0) {
    if(errno_out) *errno_out = eno;
    atomic_store(&g_fan_available, 0);
    snprintf(g_fan_message, sizeof(g_fan_message),
             "ICC-ioctl 0x%lX abgelehnt (rc=%d, errno %d: %s).",
             ICC_FAN_THRESHOLD_IOCTL, rc, eno, strerror(eno));
    return -1;
  }

  atomic_store(&g_fan_available, 1);
  snprintf(g_fan_message, sizeof(g_fan_message), "Lüftersteuerung bereit.");
  return 0;
}


/* Reads the fan controller's own configuration block.
 *
 * Command 0x08 sits directly beside the write at 0x07: same _IOWR encoding,
 * same 28-byte block, same threshold byte. Two independent projects ship it
 * and neither is a sketch — fan_target 0.1 (main.c:21, `get_fan_config`) runs
 * it every two seconds, and ShadowMountPlus carries it unchanged through
 * 1.7alpha10 to alpha13 (src/sm_fan.c:10-19).
 *
 * Why this was missing and why it matters: every write so far has been
 * one-way. The controller was never asked what it actually holds, so a write
 * that the kernel accepted but ignored looked exactly like a write that
 * worked. Worse, ps5tm_fan_init() overwrites the value the console shipped
 * with before anyone has read it, which is why switching the automatic off
 * handed back a hard-coded 65 °C — the real number was never knowable. It is
 * now: the readback showed the firmware setting 91 °C at every change of
 * state, and since 1.43.2 that is what goes back.
 *
 * ⚠ This is not a read-only call. 0xC01C8F08 is _IOWR, so the kernel copies
 * 28 bytes IN as well as out, exactly like the write does. The same rule
 * therefore applies: the buffer is exactly 28 bytes and fully zeroed first.
 *
 * ShadowMountPlus zeroes byte 2 before the call and calls it a selector. It
 * never passes any other value and memset has already zeroed it, so that name
 * is the author's reading rather than an established fact. It is followed
 * here anyway: matching a shipped, working caller byte for byte costs
 * nothing, and if byte 2 does select something, zero is the value that has
 * been proven to answer.
 *
 * Not yet verified on our own hardware — see docs/ERWEITERUNGEN.md. Both
 * shipped callers open the device O_RDWR|O_DIRECT while our write path has
 * always used O_RDONLY; the flags known to work here are the fallback.
 */
int
ps5tm_platform_read_fan_config(unsigned char *out, int *errno_out) {
  if(errno_out) *errno_out = 0;
  if(!out) return -1;
  memset(out, 0, PS5TM_ICC_FAN_CONFIG_SIZE);

#ifdef PS5TM_HOST_TEST
  {
    extern int g_sim_threshold_c;
    out[ICC_FAN_TARGET_OFFSET] = (unsigned char)g_sim_threshold_c;
  }
  return 0;
#else
  int fd = open(ICC_FAN_DEVICE, O_RDWR | ICC_FAN_OPEN_DIRECT);
  if(fd < 0) fd = open(ICC_FAN_DEVICE, O_RDONLY);
  if(fd < 0) {
    if(errno_out) *errno_out = errno;
    return -1;
  }

  unsigned char data[PS5TM_ICC_FAN_CONFIG_SIZE] = {0};
  int rc  = ioctl(fd, ICC_FAN_CONFIG_IOCTL, data);
  int eno = errno;
  close(fd);

  if(rc != 0) {
    if(errno_out) *errno_out = eno;
    return -1;
  }

  memcpy(out, data, PS5TM_ICC_FAN_CONFIG_SIZE);
  return 0;
#endif
}


/* Reads the controller's duty register. Returns -1 when unavailable, else the
   raw register value; `pct_out` receives a best-effort percentage.

   The unit is undocumented. Treating anything >100 as a 0..255 PWM value
   silently pinned the display at 100 % on real hardware, which made the
   readback useless for diagnosis — so the raw value is now carried through
   and the scaling is only a hint. */
static int
read_fan_duty(int *pct_out) {
  uint16_t duty = 0;
  uint8_t  scratch[64] = {0};
  if(!sceKernelGetCurrentFanDuty) return -1;
  if(sceKernelGetCurrentFanDuty(&duty, scratch) != 0) return -1;

  /* The register counts tenths of a percent (0..1000). Measured on FW 12.00:
     131 with the fan idling, 236/480/729 as cooling ramped up. An earlier
     guess of 0..255 scaled these to a constant 100 %, which hid the fact
     that the fan really was pegged. */
  int raw = (int)duty;
  int pct = (raw + 5) / 10;
  if(pct > 100) pct = 100;
  if(pct < 0)   pct = 0;
  if(pct_out) *pct_out = pct;
  return raw;
}


/* CPU load from the kernel's aggregate tick counters, differenced between
   calls.
 *
 * sceKernelGetCpuUsageAll returns nothing on FW 12.00 — the dashboard showed
 * a permanently empty "Auslastung" tile — so this is the fallback that stands
 * a chance. kern.cp_time is a standard FreeBSD counter of the time all cores
 * spent in user/nice/sys/intr/idle; the busy share between two samples is the
 * utilisation. The first call only primes the baseline. */
static int
cpu_load_from_ticks(double *out) {
#ifdef PS5TM_HOST_TEST
  (void)out;
  return -1;
#else
  long   cur[5] = {0};
  size_t sz     = sizeof(cur);
  if(sysctlbyname("kern.cp_time", cur, &sz, NULL, 0) != 0) return -1;

  static long prev[5];
  static int  primed = 0;
  if(!primed) {
    memcpy(prev, cur, sizeof(prev));
    primed = 1;
    return -1;
  }

  long busy = 0, total = 0;
  for(int i = 0; i < 5; i++) {
    long d = cur[i] - prev[i];
    if(d < 0) d = 0;              /* counters wrapped or were reset */
    total += d;
    if(i != 4) busy += d;         /* index 4 is the idle bucket */
  }
  memcpy(prev, cur, sizeof(prev));

  if(total <= 0) return -1;
  double pct = (double)busy * 100.0 / (double)total;
  *out = pct < 0 ? 0 : (pct > 100 ? 100 : pct);
  return 0;
#endif
}


/* CPU load measured through the kernel's idle threads.
 *
 * This is the method etaHEN's overlay uses, and it is the one that actually
 * works on FW 12. sceKernelGetCpuUsage() returns per-thread CPU time for every
 * thread on the system; the kernel runs one idle thread per logical CPU,
 * named "SceIdleCpu0".."SceIdleCpu15" (CPU 13: "SceIdleCpuRv"). However much
 * wall time an idle thread did NOT consume between two samples is the load on
 * that CPU.
 *
 * ⚠ The two times per thread are a timeval — seconds and MICROseconds — not
 * a timespec. Until 1.45.0 this file divided the second field by 10^9: the
 * fractions shrank a thousandfold, only whole seconds counted, and over a
 * one-second window every CPU read either 0 or 100 %. Logged raw on
 * 26.09.2026: 18207.017091, 18208.046802, 18209.015251 — plausible only as
 * microseconds. drakmor/ps5-hwinfo reads them the same way, and also names
 * the first field: it is the process id.
 */
#define PS5TM_MAX_THREADS 3072

typedef struct { int64_t tv_sec, tv_nsec; } ps5_timespec_t;  /* clock_gettime */
typedef struct { int64_t tv_sec, tv_usec; } ps5_timeval_t;   /* thread times  */

typedef struct {
  uint32_t      pid;
  uint32_t      td_tid;
  ps5_timeval_t user_time;
  ps5_timeval_t system_time;
} proc_stats_t;                       /* 0x28 bytes */

_Static_assert(sizeof(proc_stats_t) == 0x28, "the kernel fills 0x28 per thread");

/* Resolved at runtime rather than linked.
 *
 * The SDK's stub libraries export symbols that the console's real .sprx does
 * not always provide. Declaring such a symbol `extern` makes the dynamic
 * loader fail during lib_init — main() never runs, nothing binds, no toast,
 * no log: a completely silent death. dlsym lets the payload load regardless
 * and simply leaves the pointer NULL, so the feature degrades instead of
 * taking the whole app with it. (Failure mode documented by the ps5upload
 * project after hitting it on a live firmware.) */
typedef int (*fn_cpu_usage_t)(proc_stats_t *, int32_t *);
typedef int (*fn_thread_name_t)(uint32_t, char *);
typedef int (*fn_clock_gettime_t)(int, ps5_timespec_t *);

static fn_cpu_usage_t     p_sceKernelGetCpuUsage   = NULL;
static fn_thread_name_t   p_sceKernelGetThreadName = NULL;
static fn_clock_gettime_t p_sceKernelClockGettime  = NULL;
static int                g_cpu_syms_resolved      = 0;

static void
resolve_cpu_symbols(void) {
  if(g_cpu_syms_resolved) return;
  g_cpu_syms_resolved = 1;
#ifndef PS5TM_HOST_TEST
  p_sceKernelGetCpuUsage =
      (fn_cpu_usage_t)dlsym(RTLD_DEFAULT, "sceKernelGetCpuUsage");
  p_sceKernelGetThreadName =
      (fn_thread_name_t)dlsym(RTLD_DEFAULT, "sceKernelGetThreadName");
  p_sceKernelClockGettime =
      (fn_clock_gettime_t)dlsym(RTLD_DEFAULT, "sceKernelClockGettime");
#endif
}

typedef struct {
  proc_stats_t  *threads;
  int32_t        count;
  ps5_timespec_t stamp;
} usage_bank_t;

static usage_bank_t g_bank;              /* the current sample               */
static usage_bank_t g_top_bank;          /* the sample the ranking compares to */
static int          g_top_bank_filled = 0;
static uint32_t     g_idle_tid[PS5TM_MAX_CPUS];
static uint32_t     g_idle13_numbered = 0; /* SceIdleCpu13, for comparison only */
static int          g_idle_known  = 0;

static double
ts_seconds(const ps5_timespec_t *t) {
  return (double)t->tv_sec + (double)t->tv_nsec / 1000000000.0;
}

static double
tv_seconds(const ps5_timeval_t *t) {
  return (double)t->tv_sec + (double)t->tv_usec / 1000000.0;
}

static double
thread_seconds(const proc_stats_t *t) {
  return tv_seconds(&t->user_time) + tv_seconds(&t->system_time);
}

/* Locates the idle threads once; their ids are stable for the lifetime of the
 * system.
 *
 * One per logical CPU, sixteen in all. Up to 1.44.0 the scan stopped at eight
 * and the dashboard called logical CPUs 0..7 "Kern 1-8" — which are the two
 * threads of each of four physical cores, all of them game cores. Load on the
 * system's CPUs never showed: on 26.09.2026 a test program kept four of them
 * busy for six minutes while the display read 0 %.
 *
 * CPU 13 is the exception. Besides a thread named SceIdleCpu13 there is one
 * named "SceIdleCpuRv", and drakmor/ps5-hwinfo counts Rv for CPU 13 and skips
 * SceIdleCpu13 on purpose. The same here; the numbered one is kept aside and
 * the first full measuring window logs how much idle time each of the two
 * collected, so the choice is checked rather than copied. */
static void
find_idle_threads(const proc_stats_t *threads, int32_t count) {
  for(int i = 0; i < PS5TM_MAX_CPUS; i++) g_idle_tid[i] = 0;

  uint32_t rv_tid = 0;
  for(int32_t i = 0; i < count; i++) {
    char name[0x40] = {0};
    if(p_sceKernelGetThreadName(threads[i].td_tid, name) != 0) continue;

    if(!strcmp(name, "SceIdleCpuRv")) {
      if(!rv_tid) rv_tid = threads[i].td_tid;
      continue;
    }
    int cpu = -1;
    if(sscanf(name, "SceIdleCpu%d", &cpu) != 1) continue;
    if(cpu < 0 || cpu >= PS5TM_MAX_CPUS || g_idle_tid[cpu]) continue;
    g_idle_tid[cpu] = threads[i].td_tid;
  }
  int rv_used = 0;
  if(rv_tid) {
    g_idle13_numbered = g_idle_tid[13];
    g_idle_tid[13] = rv_tid;
    rv_used = 1;
  }

  unsigned mask = 0;
  int found = 0;
  for(int c = 0; c < PS5TM_MAX_CPUS; c++)
    if(g_idle_tid[c]) { mask |= 1u << c; found++; }
  g_idle_known = (found > 0);

  static int logged = 0;
  if(!logged && found) {
    logged = 1;
    PS5TM_INFO("cpu_idle_threads",
               "Leerlauf-Threads: %d von %d logischen CPUs gefunden (Maske "
               "0x%04x)%s.", found, PS5TM_MAX_CPUS, mask,
               rv_used ? ", CPU 13 als SceIdleCpuRv" : "");
  }
}

/* The CPUs this process may run on, which for a payload are the system's
   (0xea00 on FW 12.00, measured 26.09.2026); games get the rest. Read once.
   0 when unknown or when it covers everything, so no split is invented. */
static unsigned
system_cpu_mask(void) {
  static int      known = 0;
  static unsigned mask  = 0;
  if(known) return mask;
  known = 1;
#ifndef PS5TM_HOST_TEST
  /* 16 bytes: on 26.09.2026 the kernel refused 32 with ERANGE. */
  unsigned long raw[2] = {0, 0};
  if(cpuset_getaffinity(CPU_LEVEL_CPUSET, CPU_WHICH_PID, -1, sizeof(raw),
                        (cpuset_t *)raw) != 0 &&
     cpuset_getaffinity(CPU_LEVEL_WHICH, CPU_WHICH_PID, -1, sizeof(raw),
                        (cpuset_t *)raw) != 0)
    return mask;
  unsigned m = (unsigned)(raw[0] & 0xffffu);
  if(m && m != 0xffffu) mask = m;
  PS5TM_INFO("cpu_system_mask",
             "Eigene CPU-Menge 0x%04lx: %s.", raw[0] & 0xffffu,
             mask ? "die des Systems, der Rest gehört den Spielen"
                  : "keine Aufteilung ableitbar");
#endif
  return mask;
}

/* The last proper load reading. Only the fan worker measures; the one-shot
   probe of the risky endpoint is handed this instead of disturbing the
   worker's samples — see fill_risky_telemetry().

   It has a small mutex of its own and is NOT guarded by the Sony lock: when
   that lock is busy the fan worker still has to be able to hand out the last
   reading, without waiting for the lock to do it. The mutex is held for a
   struct copy and never across a call into anything. */
static ps5tm_sensors_t g_load_cache;
static int             g_load_cache_valid;
static pthread_mutex_t g_load_cache_lock = PTHREAD_MUTEX_INITIALIZER;

static void
copy_load_fields(ps5tm_sensors_t *dst, const ps5tm_sensors_t *src) {
  memcpy(dst->core_pct, src->core_pct, sizeof(dst->core_pct));
  memcpy(dst->cpu_pct,  src->cpu_pct,  sizeof(dst->cpu_pct));
  dst->core_count            = src->core_count;
  dst->cpu_count             = src->cpu_count;
  dst->system_cpu_mask       = src->system_cpu_mask;
  dst->cpu_load_pct          = src->cpu_load_pct;
  dst->cpu_load_valid        = src->cpu_load_valid;
  dst->game_load_pct         = src->game_load_pct;
  dst->game_load_valid       = src->game_load_valid;
  dst->system_load_pct       = src->system_load_pct;
  dst->system_load_valid     = src->system_load_valid;
  dst->cpu_load_legacy_pct   = src->cpu_load_legacy_pct;
  dst->cpu_load_legacy_valid = src->cpu_load_legacy_valid;
}

/* Copies the last load reading into `out`. 0 when there is none yet, and `out`
   is then left alone. */
static int
load_cache_get(ps5tm_sensors_t *out) {
  pthread_mutex_lock(&g_load_cache_lock);
  int valid = g_load_cache_valid;
  if(valid) copy_load_fields(out, &g_load_cache);
  pthread_mutex_unlock(&g_load_cache_lock);
  return valid;
}

static const proc_stats_t *
find_thread(const usage_bank_t *bank, uint32_t tid) {
  for(int32_t i = 0; i < bank->count; i++)
    if(bank->threads[i].td_tid == tid) return &bank->threads[i];
  return NULL;
}

/* The busiest threads of the whole system, over ten seconds.
 *
 * Built on 26.09.2026 because one of the system's CPUs seemed pinned at 100 %
 * on an idle console. The same per-thread times the load comes from answer
 * who it is, and the process id beside each thread says whose it is. Ten
 * seconds, because a ranking of one second is mostly chance. */
#define TOP_INTERVAL_MS 10000u

static ps5tm_thread_load_t g_top[PS5TM_TOP_THREADS];
static unsigned            g_top_n;
static uint64_t            g_top_ms;
static int                 g_hog_logged;

static int
is_idle_tid(uint32_t tid) {
  if(tid && tid == g_idle13_numbered) return 1;
  for(int c = 0; c < PS5TM_MAX_CPUS; c++)
    if(g_idle_tid[c] == tid) return 1;
  return 0;
}

static void
rank_threads(const usage_bank_t *old, const usage_bank_t *cur, double span) {
  ps5tm_thread_load_t best[PS5TM_TOP_THREADS];
  unsigned nb = 0;
  memset(best, 0, sizeof(best));

  for(int32_t i = 0; i < cur->count; i++) {
    const proc_stats_t *b = &cur->threads[i];
    if(is_idle_tid(b->td_tid)) continue;     /* they would top every list */
    /* The kernel mostly keeps its order between two calls; the linear search
       is the fallback, not the rule. */
    const proc_stats_t *a =
        (i < old->count && old->threads[i].td_tid == b->td_tid)
          ? &old->threads[i] : find_thread(old, b->td_tid);
    if(!a) continue;

    double pct = (thread_seconds(b) - thread_seconds(a)) / span * 100.0;
    if(pct < 1.0) continue;

    unsigned pos = nb;
    while(pos > 0 && best[pos - 1].pct < pct) pos--;
    if(pos >= PS5TM_TOP_THREADS) continue;
    unsigned last = nb < PS5TM_TOP_THREADS ? nb : PS5TM_TOP_THREADS - 1;
    for(unsigned k = last; k > pos; k--) best[k] = best[k - 1];
    best[pos].tid = b->td_tid;
    best[pos].pid = b->pid;
    best[pos].pct = pct;
    best[pos].name[0] = '\0';
    if(nb < PS5TM_TOP_THREADS) nb++;
  }

  for(unsigned k = 0; k < nb; k++) {
    char name[0x40] = {0};
    if(p_sceKernelGetThreadName(best[k].tid, name) == 0)
      snprintf(best[k].name, sizeof(best[k].name), "%s", name);
  }
  memcpy(g_top, best, sizeof(g_top));
  g_top_n = nb;

  if(!g_hog_logged && nb && best[0].pct >= 90.0) {
    g_hog_logged = 1;
    PS5TM_INFO("cpu_hog",
               "Thread \"%s\" (tid %u, Prozess %u%s) belegte über %.0f s eine "
               "CPU zu %.0f %%. Danach \"%s\" mit %.0f %%.",
               best[0].name[0] ? best[0].name : "?", best[0].tid, best[0].pid,
               best[0].pid == (uint32_t)getpid() ? ", diese App" : "", span,
               best[0].pct,
               nb > 1 && best[1].name[0] ? best[1].name : "-",
               nb > 1 ? best[1].pct : 0.0);
  }
}

static void
update_top_threads(void) {
  uint64_t now = ps5tm_now_ms();
  if(g_top_bank_filled && now - g_top_ms < TOP_INTERVAL_MS) return;

  if(g_top_bank_filled) {
    double span = ts_seconds(&g_bank.stamp) - ts_seconds(&g_top_bank.stamp);
    if(span > 0.5) rank_threads(&g_top_bank, &g_bank, span);
  }
  memcpy(g_top_bank.threads, g_bank.threads,
         (size_t)g_bank.count * sizeof(proc_stats_t));
  g_top_bank.count = g_bank.count;
  g_top_bank.stamp = g_bank.stamp;
  g_top_bank_filled = 1;
  g_top_ms = now;
}

unsigned
ps5tm_platform_top_threads(ps5tm_thread_load_t *out, unsigned max) {
  if(!out || !max) return 0;
  ps5tm_sony_api_lock();
  unsigned n = g_top_n < max ? g_top_n : max;
  memcpy(out, g_top, n * sizeof(*out));
  ps5tm_sony_api_unlock();
  return n;
}

/* Cumulative idle time per logical CPU, one row per worker pass.
 *
 * The load is taken over the last two seconds rather than the last one, which
 * steadies the dashboard without making it sluggish. (The 0-or-100 % readings
 * of 26.09.2026 that first prompted a window were not the kernel's doing but
 * the timeval read as a timespec — see proc_stats_t. The first samples are
 * still logged raw, so a firmware that changes the unit shows up at once.) */
#define IDLE_RING     8
#define LOAD_WINDOW_S 2.0

static double g_ring_t[IDLE_RING];
static double g_ring_idle[IDLE_RING][PS5TM_MAX_CPUS];   /* -1 where missing */
static double g_ring_idle13n[IDLE_RING];
static int    g_ring_head = -1;
static int    g_ring_rows = 0;

static int
cpu_load_from_idle_threads(ps5tm_sensors_t *out) {
  resolve_cpu_symbols();
  if(!p_sceKernelGetCpuUsage || !p_sceKernelGetThreadName ||
     !p_sceKernelClockGettime)
    return -1;                      /* firmware does not provide these */

  if(!g_bank.threads) {
    g_bank.threads     = malloc(sizeof(proc_stats_t) * PS5TM_MAX_THREADS);
    g_top_bank.threads = malloc(sizeof(proc_stats_t) * PS5TM_MAX_THREADS);
    if(!g_bank.threads || !g_top_bank.threads) {
      free(g_bank.threads);     g_bank.threads     = NULL;
      free(g_top_bank.threads); g_top_bank.threads = NULL;
      return -1;
    }
  }

  g_bank.count = PS5TM_MAX_THREADS;
  if(p_sceKernelGetCpuUsage(g_bank.threads, &g_bank.count) != 0 ||
     g_bank.count <= 0)
    return -1;
  /* The count is the kernel's to set, and nothing says it cannot name more
     threads than the buffer was offered room for. Every loop below, the
     search for the idle threads and the copy that keeps the ranking's
     baseline walk this many entries — past the end of the allocation if the
     kernel ever reports the total instead of what it wrote. */
  if(g_bank.count > PS5TM_MAX_THREADS) g_bank.count = PS5TM_MAX_THREADS;
  p_sceKernelClockGettime(4, &g_bank.stamp);

  if(!g_idle_known) find_idle_threads(g_bank.threads, g_bank.count);
  if(!g_idle_known) return -1;

  /* One row per pass. */
  int row = (g_ring_head + 1) % IDLE_RING;
  g_ring_t[row] = ts_seconds(&g_bank.stamp);
  for(int c = 0; c < PS5TM_MAX_CPUS; c++) {
    const proc_stats_t *t = g_idle_tid[c] ? find_thread(&g_bank, g_idle_tid[c])
                                          : NULL;
    g_ring_idle[row][c] = t ? thread_seconds(t) : -1.0;
  }
  {
    const proc_stats_t *t = g_idle13_numbered
                              ? find_thread(&g_bank, g_idle13_numbered) : NULL;
    g_ring_idle13n[row] = t ? thread_seconds(t) : -1.0;
  }
  g_ring_head = row;
  if(g_ring_rows < IDLE_RING) g_ring_rows++;

  /* The step size, on record: CPU 0's idle thread, raw, for the first three
     passes. */
  {
    static int raw_logged = 0;
    const proc_stats_t *t = g_idle_tid[0] ? find_thread(&g_bank, g_idle_tid[0])
                                          : NULL;
    if(t && raw_logged < 3) {
      raw_logged++;
      PS5TM_INFO("cpu_idle_raw",
                 "Leerlaufzeit CPU 0, roh: Nutzer %lld s + %lld µs, System "
                 "%lld s + %lld µs, Uhr %.3f s.",
                 (long long)t->user_time.tv_sec,   (long long)t->user_time.tv_usec,
                 (long long)t->system_time.tv_sec, (long long)t->system_time.tv_usec,
                 g_ring_t[row]);
    }
  }

  update_top_threads();

  if(g_ring_rows < 2) return -1;                  /* need two rows */

  /* The oldest row inside the window, at least one step back. */
  int newest = g_ring_head, oldest = -1;
  for(int k = 1; k < g_ring_rows; k++) {
    int r = (g_ring_head - k + IDLE_RING) % IDLE_RING;
    if(oldest >= 0 && g_ring_t[newest] - g_ring_t[r] > LOAD_WINDOW_S + 0.25)
      break;
    oldest = r;
  }
  double span = g_ring_t[newest] - g_ring_t[oldest];
  if(span <= 0.0) return -1;

  /* −1 means "not measured" and stays that way. A CPU whose idle thread we
     cannot find is not a CPU at 0 % — reporting it as one would be a made-up
     number in a list of real ones. */
  for(int i = 0; i < PS5TM_MAX_CORES; i++) out->core_pct[i] = -1;
  for(int i = 0; i < PS5TM_MAX_CPUS;  i++) out->cpu_pct[i]  = -1;

  unsigned sys_mask = system_cpu_mask();
  double   busy_of[PS5TM_MAX_CPUS];
  int      measured = 0;
  int      highest  = -1;
  double   sum      = 0.0;
  double   sum_game = 0.0, sum_sys = 0.0;
  int      n_game   = 0,   n_sys   = 0;

  for(int c = 0; c < PS5TM_MAX_CPUS; c++) {
    busy_of[c] = -1.0;
    double a = g_ring_idle[oldest][c], b = g_ring_idle[newest][c];
    if(a < 0 || b < 0) continue;              /* thread vanished */

    double frac = (b - a) / span;
    if(frac < 0.0) frac = 0.0;
    if(frac > 1.0) frac = 1.0;
    double busy = (1.0 - frac) * 100.0;
    busy_of[c] = busy;

    /* Indexed by the CPU it belongs to, not by how many we have managed so
       far. The old code wrote to core_pct[measured++], so a single missing
       idle thread shifted every core after it into its neighbour's slot and
       the labels silently stopped matching the values. */
    out->cpu_pct[c] = (int)(busy + 0.5);
    if(c > highest) highest = c;
    measured++;
    sum += busy;
    if(sys_mask) {
      if(sys_mask & (1u << c)) { sum_sys  += busy; n_sys++;  }
      else                     { sum_game += busy; n_game++; }
    }
  }
  if(!measured) return -1;

  out->cpu_count       = highest + 1;
  out->cpu_load_pct    = sum / measured;
  out->cpu_load_valid  = 1;
  out->system_cpu_mask = sys_mask;
  if(n_game) { out->game_load_pct   = sum_game / n_game; out->game_load_valid   = 1; }
  if(n_sys)  { out->system_load_pct = sum_sys  / n_sys;  out->system_load_valid = 1; }

  /* The long-term record keeps the pre-1.45.0 figure: logical CPUs 0..7 over
     the last step alone, computed exactly as it was when the weeks already
     on disk were counted. */
  {
    int prev = (g_ring_head - 1 + IDLE_RING) % IDLE_RING;
    double step = g_ring_t[newest] - g_ring_t[prev];
    double s = 0.0;
    int n = 0;
    for(int c = 0; c < 8 && step > 0.0; c++) {
      double a = g_ring_idle[prev][c], b = g_ring_idle[newest][c];
      if(a < 0 || b < 0) continue;
      double frac = (b - a) / step;
      if(frac < 0.0) frac = 0.0;
      if(frac > 1.0) frac = 1.0;
      s += (1.0 - frac) * 100.0;
      n++;
    }
    if(n) {
      out->cpu_load_legacy_pct   = s / n;
      out->cpu_load_legacy_valid = 1;
    }
  }

  /* Physical cores: logical CPUs 2k and 2k+1 are taken as the two threads of
     one core. What supports it: at idle both clock sources show four cores
     at 3200 MHz and four at 800, and the system's five CPUs (9, 11, 13, 14,
     15) fit into exactly four such pairs, 8-9 to 14-15. What is NOT
     established is which physical core a pair is: the power-mode table puts
     the fast four at the end, the live clock call at the start. So "Kern 1"
     is a pair, not a place on the die. */
  int cores_seen = 0;
  for(int k = 0; k < PS5TM_MAX_CORES; k++) {
    double a = busy_of[2 * k], b = busy_of[2 * k + 1];
    double v = (a >= 0 && b >= 0) ? (a + b) / 2.0 : (a >= 0 ? a : b);
    if(v < 0) continue;
    out->core_pct[k] = (int)(v + 0.5);
    cores_seen = k + 1;
  }
  out->core_count = cores_seen;

  /* CPU 13, checked once over a full window: how much idle time did each of
     its two candidate threads collect? */
  {
    static int cmp_logged = 0;
    if(!cmp_logged && g_idle13_numbered && span >= LOAD_WINDOW_S - 0.5) {
      cmp_logged = 1;
      double rv = g_ring_idle[newest][13] - g_ring_idle[oldest][13];
      double nu = (g_ring_idle13n[oldest] >= 0 && g_ring_idle13n[newest] >= 0)
                    ? g_ring_idle13n[newest] - g_ring_idle13n[oldest] : -1.0;
      PS5TM_INFO("cpu13_idle_check",
                 "CPU 13 über %.1f s: SceIdleCpuRv %.2f s Leerlauf, "
                 "SceIdleCpu13 %.2f s.", span, rv, nu);
    }
  }

  pthread_mutex_lock(&g_load_cache_lock);
  copy_load_fields(&g_load_cache, out);
  g_load_cache_valid = 1;
  pthread_mutex_unlock(&g_load_cache_lock);
  return 0;
}

static int
risky_probe_allowed(void) {
  ps5tm_config_lock();
  unsigned mask = g_config.probe_mask;
  ps5tm_config_unlock();
  return (mask & PS5TM_PROBE_RISKY) != 0;
}

/* Optional telemetry that is useful but not required for fan control. */
static int
fill_risky_telemetry(ps5tm_sensors_t *out, int from_worker) {
/* The idle-thread method goes first, and the order matters.
 *
 * ── Measured with a game running, 02.08.2026 ─────────────────────────────
 * The comment that stood here said sceKernelGetCpuUsageAll was "silent on
 * FW 12" and the idle-thread method would step in. It is not silent. It
 * answers — with a per-core figure that is only ever 0 or 100:
 *
 *   cores [0,100,100,0,100,0,100,100]  ->  62.5 %
 *   cores [100,0,100,100,0,0,0,0]      ->  37.5 %
 *   cores [0,0,0,100,0,0,0,0]          ->  12.5 %
 *
 * Six samples over a minute of play, the die swinging 63-78 °C, and every
 * total landing exactly on a multiple of 12.5. That is not a load average
 * over an interval, it is a snapshot of which cores happened to be running
 * at the instant of the call — so the dashboard showed cores pinned at 100 %
 * that were merely awake, and a total that could only take nine values.
 *
 * Because it reported success, the fallback below never ran, and the method
 * that does measure properly sat unused. Reversed: idle threads first, the
 * syscall only when they cannot answer.
 *
 * cpu_load_from_idle_threads() needs two samples before it can subtract, so
 * it declines the very first call and the syscall covers that one reading. */
#ifndef PS5TM_HOST_TEST
  /* Only the fan worker measures, once a second. The one-shot probe of the
     risky endpoint gets the worker's last result: measuring from a second
     thread milliseconds after the worker gave windows too short to mean
     anything (26.09.2026, every CPU at 0 or 100 %). The Sony lock covers the
     measurement's own state either way.

     The fan worker never waits for that lock. Whoever holds it is inside a
     Sony service, which can take as long as it likes, and the load figures
     are a nicety — regulation is not. So the worker only asks: when the lock
     is free it measures as always, when it is not it hands out the previous
     reading and tries again a second later. The request thread may wait. */
  if(from_worker) {
    if(ps5tm_sony_api_trylock()) {
      cpu_load_from_idle_threads(out);
      ps5tm_sony_api_unlock();
    } else {
      load_cache_get(out);
    }
  } else {
    ps5tm_sony_api_lock();
    if(!load_cache_get(out)) cpu_load_from_idle_threads(out);
    ps5tm_sony_api_unlock();
  }
#else
  (void)from_worker;
#endif

  if(!out->cpu_load_valid) {
    /* The call reports how many it wrote but is never told how many it may
       write, and this console has sixteen logical CPUs, not eight. Room for
       64 keeps an answer longer than expected out of the stack; only the
       first PS5TM_MAX_CORES are ever used. */
    int per_core[64];
    for(int i = 0; i < 64; i++) per_core[i] = -1;
    int count = 0;
    if(sceKernelGetCpuUsageAll &&
       sceKernelGetCpuUsageAll(per_core, &count) == 0 &&
       count > 0 && count <= PS5TM_MAX_CORES) {
      int sum = 0, used = 0;
      for(int i = 0; i < count; i++) {
        out->core_pct[i] = per_core[i];
        if(per_core[i] < 0 || per_core[i] > 100) continue;
        sum += per_core[i];
        used++;
      }
      out->core_count = count;
      if(used) {
        out->cpu_load_pct   = (double)sum / (double)used;
        out->cpu_load_valid = 1;
      }
    }
  }

  if(!out->cpu_load_valid) {
    double pct = 0;
    if(cpu_load_from_ticks(&pct) == 0) {
      out->cpu_load_pct   = pct;
      out->cpu_load_valid = 1;
    }
  }

  /* The two fallbacks know nothing of sixteen CPUs; for the long-term record
     their figure is the old one, exactly as before 1.45.0. */
  if(out->cpu_load_valid && !out->cpu_load_legacy_valid) {
    out->cpu_load_legacy_pct   = out->cpu_load_pct;
    out->cpu_load_legacy_valid = 1;
  }

  long hz = sceKernelGetCpuFrequency ? sceKernelGetCpuFrequency() : 0;
  if(hz > 0) {
    out->cpu_mhz       = hz / 1000000L;
    out->cpu_mhz_valid = 1;
  }

  int cpu_mode = sceKernelGetCpumode ? sceKernelGetCpumode() : -1;
  out->cpu_mode_valid = (cpu_mode >= 0);
  if(out->cpu_mode_valid) out->cpu_mode = cpu_mode;

  /* The power reading used to follow here, with the wrong prototype and the
     wrong layout — it never produced a value. It moved to telemetry.c. */
  return out->cpu_load_valid || out->cpu_mhz_valid;
}


int
ps5tm_platform_read_sensors(ps5tm_sensors_t *out) {
  memset(out, 0, sizeof(*out));
  out->cpu_c = out->soc_c = out->gpu_c = -1;

  /* Sensors briefly report nonsense right after a wake from rest mode;
     anything outside 1..130 °C is treated as "not ready".

     Zero is outside on purpose. A running console is never at 0 °C, and a
     sensor that has nothing to say is as likely to answer 0 as to fail: taken
     as a reading, 0 drags the fan's moving average down by a whole window's
     worth of degrees and the controller backs off while the die is hot.
     thermalog.c has always refused it; the fan has to as well. */
  int v = -1;
  if(sceKernelGetCpuTemperature &&
     sceKernelGetCpuTemperature(&v) == 0 && v > 0 && v <= 130) {
    out->cpu_c = v; out->cpu_valid = 1;
  }

  /* Channel 0 used to become soc_c — the value the dashboard shows as the
     main chip. That was the wrong choice; see below. */

  /* Sweep every SoC channel once. Channel 7 is picked out for the dashboard;
     all eight stay available for the diagnostics view. */
  for(int ch = 0; ch < PS5TM_SOC_CHANNELS; ch++) {
    int t = -1;
    if(sceKernelGetSocSensorTemperature &&
       sceKernelGetSocSensorTemperature(ch, &t) == 0 && t > 0 && t <= 130) {
      out->soc_raw[ch]       = t;
      out->soc_raw_valid[ch] = 1;
    } else {
      out->soc_raw[ch]       = -1;
      out->soc_raw_valid[ch] = 0;
    }
  }

  /* The main-chip figure is the hottest of the chip-region channels, not
   * channel 0.
   *
   * Measured on 01.08.2026 over 152 samples spanning idle, a game and the
   * cooldown after it (v1.16.0, /api/v1/channels — the recording is still
   * there):
   *
   *   channel 0   rose 13.4 °C under load, peaked at 62, sat 9.8 °C below the
   *               CPU sensor on average and 17 °C below at times, and after
   *               30 s of cooldown had shed only 75 % of its rise
   *   channels 3,4,6,7  rose 22.5–23.9 °C, peaked at 76 — the same peak as
   *               the CPU sensor — and were fully back down within 30 s
   *
   * So channel 0 is a heavily damped reading, not the die: it made the
   * console look up to 17 °C cooler than it was. The eight channels are also
   * genuinely eight sensors — across 152 samples no pair held a constant
   * difference, which the old comment in api.c denied. That comment was
   * written from idle readings, where all eight sit within 2 °C and look
   * identical.
   *
   * ⚠ Revisited on 26 and 27.09.2026 (PS5 Pro, FW 12.00, v1.45.0), over two
   * sessions and some 2000 samples: channel 0 did not behave as damped.
   * When a 100 W game ended it fell 15 °C within 15 s like every other
   * channel, and it ran within 0.2 °C of channel 2 throughout. The 01.08.
   * result did not repeat. Channel 0 stays out of the maximum all the same:
   * it topped the other seven in a handful of samples only (7 of some 2300,
   * by 2 °C at most), so including it would change next to nothing, and the
   * fan regulates on this figure — not something to move in a patch.
   *
   * Taking the maximum rather than naming one channel keeps this honest: the
   * hottest chip-region sensor is what matters thermally. */
  int hottest_chip = -1;
  for(int ch = 1; ch < PS5TM_SOC_CHANNELS; ch++)
    if(out->soc_raw_valid[ch] && out->soc_raw[ch] > hottest_chip)
      hottest_chip = out->soc_raw[ch];
  if(hottest_chip >= 0) { out->soc_c = hottest_chip; out->soc_valid = 1; }

  /* Channel 7 is the sensor nearest the graphics unit. Measured on the PS5
   * Pro on 26 and 27.09.2026, with the graphics rail beside the channels
   * (Messungen/2026-09-27 Kanaltest zweites Spiel, outside the repository):
   *
   *   - With the fan held at 34 % and the CPU nearly idle (Two Point
   *     Hospital, 80 min, GPU 2-15 W), channel 7 was the only one that rose
   *     with the graphics rail more steeply than the channel mean:
   *     +0.115 ± 0.019 K/W. Every other channel, the CPU sensor included,
   *     stayed within about two standard errors of the mean.
   *   - Under a 100 W game it ran 4.9 °C above the channel mean (4.2 the day
   *     before), the widest margin of the eight, and it fell furthest when
   *     the game ended: 16.5 °C, most of it within 15 s.
   *   - Ranked by how closely they follow the GPU: 7 > 0 ≈ 2 > 4, 5 > 3, 1, 6.
   *
   * The CPU sensor, by contrast, follows the GPU no more than the mean does,
   * and still ran 10 °C above the channels in the 100 W game. The hottest
   * spot on the die is the CPU cores, even in a graphics-heavy game.
   *
   * The same data retired three names:
   *   - Channel 0 is not damped (above).
   *   - Channel 1 was shown as the power supply, but it sits on the chip. At
   *     idle it shared one band with channels 0, 2, 3 and 4, 0.6 °C wide at
   *     most, on both days. Under the 100 W game it warmed least of all,
   *     where a regulator feeding the graphics unit would have warmed most.
   *   - Channel 2 was shown as the M.2 drive whenever one was mounted. It fell
   *     14 °C within 15 s of the game ending, which no SSD does, and it read
   *     chip temperatures on a console with no M.2 fitted.
   * All three are raw channels now.
   *
   * Only the Pro was measured. The field is filled on every console; the API
   * decides whether it may be called the graphics side (api.c). */
  if(out->soc_raw_valid[7]) { out->gpu_c = out->soc_raw[7]; out->gpu_valid = 1; }

  for(int c = 0; c < PS5TM_MAX_CPUS; c++) out->cpu_pct[c] = -1;
  int risky = risky_probe_allowed();
  if(risky) fill_risky_telemetry(out, 1);
  /* Power and live clocks ride on the risky switch like the load does; the
     frame rate has a switch of its own and is checked inside. */
  ps5tm_telemetry_update(out, risky, 1);

  /* No public syscall exposes GPU utilisation; reported as unavailable
     rather than guessed. The research of 26.09.2026 confirmed it: the "GPU %"
     other overlays show is video-memory occupancy. The GPU's power rail is
     the honest stand-in, and it is labelled as power. */
  out->gpu_load_valid = 0;
  out->gpu_load_pct   = 0.0;

  int pct = -1;
  int raw = read_fan_duty(&pct);
  if(raw >= 0) {
    out->fan_duty_raw   = raw;
    out->fan_duty_pct   = pct >= 0 ? pct : 0;
    out->fan_duty_valid = (pct >= 0);
  } else {
    out->fan_duty_raw = -1;
  }

  return (out->cpu_valid || out->soc_valid) ? 0 : -1;
}

int
ps5tm_platform_probe_risky_once(ps5tm_sensors_t *out) {
  if(!out) return -1;
  memset(out, 0, sizeof(*out));
  out->cpu_c = out->soc_c = out->gpu_c = -1;
  for(int c = 0; c < PS5TM_MAX_CPUS; c++) out->cpu_pct[c] = -1;
  fill_risky_telemetry(out, 0);
  /* No frame-rate step from here: that counter belongs to the fan worker's
     once-a-second pass, and a request in between would shorten its interval. */
  ps5tm_telemetry_update(out, 1, 0);
  return (out->cpu_load_valid || out->cpu_mhz_valid || out->soc_power_valid)
           ? 0 : -1;
}


/* ----------------------------------------------------------------- firmware
 *
 * Groups mirror the firmware ranges the SDK's start-up code has kernel offsets
 * for (crt1.o, __kernel_init). They are only a label here: a firmware the SDK
 * does not know never gets this far — the start-up code refuses it before
 * main(), which is why the build checks the SDK (tools/check_sdk_firmware.py).
 * Up to 12.70 they follow "Kernel Offsets 3.00-12.70.txt", shipped with the
 * project earlier; 13.00 to 13.60 came with SDK v0.40 to v0.42 (13.00, 13.20,
 * 13.40, 13.42 and 13.60 are the versions it has a case for).
 * kernel_get_fw_version() returns a packed BCD word, e.g. 0x03000000 for 3.00
 * and 0x13600000 for 13.60; the two high bytes carry major/minor.
 */
uint32_t
ps5tm_platform_firmware(void) {
  return (uint32_t)kernel_get_fw_version();
}

/* The version word packs major and minor as BCD in the top two bytes:
   0x12000043 is 12.00, 0x12700000 is 12.70. The low half carries a build
   number that is not part of the version users see. */
static unsigned
fw_hundredths(uint32_t fw) {
  unsigned major = (fw >> 24) & 0xff;
  unsigned minor = (fw >> 16) & 0xff;
  /* BCD to decimal. */
  major = ((major >> 4) & 0xf) * 10 + (major & 0xf);
  minor = ((minor >> 4) & 0xf) * 10 + (minor & 0xf);
  return major * 100 + minor;
}

void
ps5tm_platform_firmware_text(uint32_t fw, char *out, size_t out_len) {
  unsigned v = fw_hundredths(fw);
  if(!v) {
    snprintf(out, out_len, "unbekannt");
    return;
  }
  snprintf(out, out_len, "%u.%02u", v / 100, v % 100);
}

const char *
ps5tm_platform_firmware_group(uint32_t fw) {
  unsigned v = fw_hundredths(fw);
  if(v >= 300  && v <= 321)  return "3.00-3.21";
  if(v >= 402  && v <= 451)  return "4.02-4.51";
  if(v == 900)               return "9.00";
  if(v >= 920  && v <= 960)  return "9.20-9.60";
  if(v >= 1000 && v <= 1060) return "10.00-10.60";
  if(v >= 1100 && v <= 1160) return "11.00-11.60";
  if(v >= 1200 && v <= 1270) return "12.00-12.70";
  if(v >= 1300 && v <= 1360) return "13.00-13.60";
  return "unbekannt";
}

int
ps5tm_platform_firmware_recognized(uint32_t fw) {
  return strcmp(ps5tm_platform_firmware_group(fw), "unbekannt") != 0;
}
