/* Comfort-controller test rig.
 *
 * Reproduces the situation the controller is meant to survive: a console
 * sitting near the target temperature whose readings wobble by a couple of
 * degrees, interrupted by a genuine load ramp and a short spike. The
 * firmware's duty response is modelled from FW 12.00 measurements, and the
 * threshold written through the ICC hook feeds back into it, so the whole
 * loop is exercised rather than a fixed input.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>

uint32_t kernel_get_fw_version(void)                       { return 0x12000043; }
intptr_t kernel_get_proc(pid_t pid)                        { (void)pid; return 0x1000; }
intptr_t kernel_get_root_vnode(void)                       { return 0x2000; }
int32_t  kernel_set_proc_rootdir(pid_t p, intptr_t v)      { (void)p; (void)v; return 0; }
int32_t  kernel_set_proc_jaildir(pid_t p, intptr_t v)      { (void)p; (void)v; return 0; }
int32_t  kernel_set_ucred_uid(pid_t p, uid_t u)            { (void)p; (void)u; return 0; }
int32_t  kernel_set_ucred_ruid(pid_t p, uid_t u)           { (void)p; (void)u; return 0; }
int32_t  kernel_set_ucred_svuid(pid_t p, uid_t u)          { (void)p; (void)u; return 0; }
int32_t  kernel_set_ucred_rgid(pid_t p, gid_t g)           { (void)p; (void)g; return 0; }
int32_t  kernel_set_ucred_svgid(pid_t p, gid_t g)          { (void)p; (void)g; return 0; }
int32_t  kernel_set_ucred_authid(pid_t p, uint64_t a)      { (void)p; (void)a; return 0; }
int32_t  kernel_set_ucred_caps(pid_t p, const uint8_t c[16])  { (void)p; (void)c; return 0; }
int32_t  kernel_set_ucred_attrs(pid_t p, const uint8_t a[32]) { (void)p; (void)a; return 0; }

int g_sim_threshold_c = 80;          /* written by the ICC hook */
static int g_duty_x10 = 200;

long
sim_seconds(void) {
  static time_t t0 = 0;
  if(!t0) t0 = time(NULL);
  return (long)(time(NULL) - t0);
}

/*  0- 60 s  hovering at the 66 °C target  -> must stay silent
 * 60- 90 s  brief 4 °C spike             -> must be ignored
 * 90-150 s  real load, climbing to 76 °C -> must ramp gently
 *150-210 s  cooling back to 64 °C        -> must ease off slowly
 */
static int
base_temp(void) {
  long t = sim_seconds();
  if(t <  60) return 66;
  if(t <  75) return 70;                        /* spike           */
  if(t <  90) return 66;
  if(t < 150) return 66 + (int)((t - 90) / 6);  /* 66 -> 76        */
  if(t < 210) return 76 - (int)((t - 150) / 5); /* 76 -> 64        */
  return 64;
}

static int noisy(int v) { return v + (rand() % 5) - 2; }

/* Firmware model: duty grows with (temperature - threshold), with a floor
   near 13 % and a first-order lag so it cannot jump instantly. */
static void
advance_fan(int temp_c) {
  int gap = temp_c - g_sim_threshold_c;

  int want;
  if(gap <= -20)     want = 130;
  else if(gap <= 0)  want = 130 + (20 + gap) * 5;
  else if(gap <= 10) want = 230 + gap * 50;
  else               want = 730 + (gap - 10) * 27;
  if(want > 1000) want = 1000;
  if(want < 130)  want = 130;

  g_duty_x10 += (want - g_duty_x10) / 4;
}

int
sceKernelGetCpuTemperature(int *o) {
  int t = base_temp();
  advance_fan(t);
  *o = noisy(t);
  return 0;
}

int
sceKernelGetSocSensorTemperature(int ch, int *o) {
  if(ch == 0) { *o = noisy(base_temp() - 6); return 0; }
  if(ch == 2) { *o = noisy(45); return 0; }
  return -1;
}

int
sceKernelGetCurrentFanDuty(uint16_t *o, void *s) {
  (void)s;
  *o = (uint16_t)g_duty_x10;
  return 0;
}

int
sceKernelGetCpuUsageAll(int *p, int *n) {
  for(int i = 0; i < 8; i++) p[i] = 40;
  *n = 8;
  return 0;
}

long sceKernelGetCpuFrequency(void) { return 3500000000L; }
int  sceKernelGetCpumode(void)      { return 2; }

int
sceKernelGetSocPowerConsumption(uint64_t *out, double reserved) {
  (void)reserved;
  out[0] = 78000 + (uint64_t)(rand() % 6000);   /* ~78-84 W in milliwatts */
  return 0;
}

int
sceKernelGetHwModelName(char *out) {
  strcpy(out, "PlayStation 5 (Testmodus)");
  return 0;
}

int
sceKernelGetHwSerialNumber(char *out) {
  strcpy(out, "TESTSERIAL9427");
  return 0;
}

int sceKernelIccGetPowerNumberOfBootShutdown(uint64_t *o) { *o = 431;    return 0; }
int sceKernelIccGetPowerOperatingTime(uint64_t *o)        { *o = 934210; return 0; }

int sceAppInstUtilInitialize(void) { return 0; }
int sceAppInstUtilAppInstallPkg(const char *p, void *i) { (void)p; (void)i; return -1; }

/* Module loading is a no-op off-console; the tile code only checks that the
   call returns, not what it returns. */
int sceKernelLoadStartModule(const char *p, unsigned long a, const void *b,
                             unsigned c, void *d, int *res) {
  (void)p; (void)a; (void)b; (void)c; (void)d;
  if(res) *res = 0;
  return 0;
}
