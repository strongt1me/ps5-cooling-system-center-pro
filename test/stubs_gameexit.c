/* Reproduces the observed Astro Bot behaviour: ~70 °C while playing, then the
 * home button is pressed and the die falls to ~54 °C within seconds, then the
 * game is resumed. Checks that the fan comes down in reasonable time and that
 * the threshold does not wind up, which would make the restart deafening.
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

int g_sim_threshold_c = 80;
static int g_duty_x10 = 200;

/*   0- 90 s  playing, ~70 °C
 *  90-180 s  home button: 54 °C almost immediately
 * 180-240 s  back in the game, 70 °C again
 */
static int
base_temp(void) {
  static time_t t0 = 0;
  if(!t0) t0 = time(NULL);
  long t = (long)(time(NULL) - t0);

  if(t <  90) return 70;
  if(t <  93) return 70 - (int)((t - 90) * 5);  /* fast drop */
  if(t < 180) return 54;
  if(t < 183) return 54 + (int)((t - 180) * 5); /* fast rise */
  return 70;
}

static int noisy(int v) { return v + (rand() % 5) - 2; }

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
