/* Closed-loop test rig.
 *
 * Models the firmware's own behaviour as measured on FW 12.00: the duty rises
 * with the gap between temperature and threshold (-7 °C -> 24 %, +6 °C -> 48 %,
 * +9 °C -> 73 %), and the fan takes a few seconds to reach a new speed. The
 * threshold written through the ICC hook feeds straight back in, so the
 * controller is exercised against a real feedback path rather than a constant.
 */

#include <stdint.h>
#include <stdlib.h>
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

/* Set by the ICC hook in platform.c (host build). */
int g_sim_threshold_c = 80;

static int g_duty_x10 = 130;   /* current fan duty, tenths of a percent */

static int
base_temp(void) {
  static time_t t0 = 0;
  if(!t0) t0 = time(NULL);
  long t = (long)(time(NULL) - t0);

  if(t <  30) return 66;                       /* idle            */
  if(t <  70) return 66 + (int)((t - 30) / 4); /* warming to ~75  */
  if(t < 130) return 76 + (int)((t - 70) / 20);/* heavy load ~78  */
  if(t < 190) return 78 - (int)((t - 130) / 5);/* cooling down    */
  return 66;
}

static int noisy(int v) { return v + (rand() % 5) - 2; }

/* Firmware model: duty follows the temperature-minus-threshold gap, with a
   floor of ~13 % and a first-order lag so it cannot jump instantly. */
static void
advance_fan(int temp_c) {
  int gap = temp_c - g_sim_threshold_c;

  int want;
  if(gap <= -20)     want = 130;
  else if(gap <= 0)  want = 130 + (20 + gap) * 5;      /* 130..230 */
  else if(gap <= 10) want = 230 + gap * 50;            /* 230..730 */
  else               want = 730 + (gap - 10) * 27;
  if(want > 1000) want = 1000;
  if(want < 130)  want = 130;

  g_duty_x10 += (want - g_duty_x10) / 4;               /* spin-up lag */
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
