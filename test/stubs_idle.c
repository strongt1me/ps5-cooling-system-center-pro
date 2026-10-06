/* Idle console, far below the target: 39 °C against a 66 °C setpoint.
 *
 * Reproduces the state a user reported — the controller kept counting its
 * requested duty down towards zero while the fan sat at its physical floor
 * of ~13 %, so the two figures drifted apart and the request became
 * meaningless. The desired duty must settle at the floor instead.
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

int g_sim_threshold_c = 80;
static int g_duty_x10 = 130;          /* the fan's floor: 13 % */

static int noisy(int v) { return v + (rand() % 3) - 1; }

/* Cold console; the fan cannot go below 13 % no matter what we ask for. */
static void
advance_fan(int temp_c) {
  int gap = temp_c - g_sim_threshold_c;
  int want = (gap <= 0) ? 130 : 230 + gap * 50;
  if(want > 1000) want = 1000;
  if(want < 130)  want = 130;
  g_duty_x10 += (want - g_duty_x10) / 4;
}

int
sceKernelGetCpuTemperature(int *o) {
  advance_fan(39);
  *o = noisy(39);
  return 0;
}

int
sceKernelGetSocSensorTemperature(int ch, int *o) {
  if(ch == 0) { *o = noisy(37); return 0; }
  return -1;
}

int
sceKernelGetCurrentFanDuty(uint16_t *o, void *s) {
  (void)s;
  *o = (uint16_t)g_duty_x10;
  return 0;
}

/* Matches FW 12.00: the call exists but reports nothing, which is what makes
   the dashboard fall back to a different figure. */
int
sceKernelGetCpuUsageAll(int *p, int *n) {
  (void)p; (void)n;
  return -1;
}

long sceKernelGetCpuFrequency(void) { return 800000000L; }
int  sceKernelGetCpumode(void)      { return 0; }
int  sceKernelGetSocPowerConsumption(uint64_t *out, double r) { (void)r; out[0] = 42000; return 0; }
int  sceKernelGetHwModelName(char *out)    { strcpy(out, "CFI-2116 A01Y"); return 0; }
int  sceKernelGetHwSerialNumber(char *out) { strcpy(out, "TESTSERIAL1865"); return 0; }
int  sceKernelIccGetPowerNumberOfBootShutdown(uint64_t *o) { *o = 168ull << 32; return 0; }
int  sceKernelIccGetPowerOperatingTime(uint64_t *o)        { *o = 0; return 0; }
int  sceAppInstUtilInitialize(void) { return 0; }
int  sceAppInstUtilAppInstallPkg(const char *p, void *i) { (void)p; (void)i; return -1; }

/* Module loading is a no-op off-console; the tile code only checks that the
   call returns, not what it returns. */
int sceKernelLoadStartModule(const char *p, unsigned long a, const void *b,
                             unsigned c, void *d, int *res) {
  (void)p; (void)a; (void)b; (void)c; (void)d;
  if(res) *res = 0;
  return 0;
}
