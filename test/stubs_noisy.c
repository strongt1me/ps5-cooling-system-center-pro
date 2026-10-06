/* Variant of test/stubs.c that reproduces the console log from 2026-07-30:
 * a load hovering around 65 °C with the ±2 °C read-to-read jitter that made
 * the threshold oscillate every second. Used to verify the damping. */

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

/* Steady 65 °C plus uniform noise in [-2, +2]. */
static int
noisy(int base) {
  return base + (rand() % 5) - 2;
}

int
sceKernelGetCpuTemperature(int *out_celsius) {
  *out_celsius = noisy(65);
  return 0;
}

int
sceKernelGetSocSensorTemperature(int channel, int *out_celsius) {
  if(channel == 0) { *out_celsius = noisy(66); return 0; }
  if(channel == 2) { *out_celsius = noisy(45); return 0; }
  return -1;
}

int
sceKernelGetCurrentFanDuty(uint16_t *out_duty, void *scratch) {
  (void)scratch;
  *out_duty = 42;
  return 0;
}

int
sceKernelGetCpuUsageAll(int *per_core_pct, int *count_out) {
  for(int i = 0; i < 8; i++) per_core_pct[i] = 40;
  *count_out = 8;
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
