/* Host-test stubs: simulated console so the server, API, config and curve
 * logic can be exercised on Linux. Only the ICC ioctl itself is out of reach
 * here — that path is verified on the console. */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>

/* ---- kernel.h stand-ins: pretend the jailbreak is fully in place ---- */
uint32_t kernel_get_fw_version(void)                       { return 0x12700000; }
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


/* ---- simulated thermals: a slow sawtooth so the curve visibly reacts ---- */
static int
simulated_temp(int offset) {
  static time_t t0 = 0;
  if(!t0) t0 = time(NULL);
  long elapsed = (long)(time(NULL) - t0);
  int  ramp    = (int)(elapsed % 60);          /* 0..59 */
  return 40 + ramp / 2 + offset;               /* ~40..70 °C */
}

int
sceKernelGetCpuTemperature(int *out_celsius) {
  if(!out_celsius) return -1;
  *out_celsius = simulated_temp(0);
  return 0;
}

int
sceKernelGetSocSensorTemperature(int channel, int *out_celsius) {
  if(!out_celsius) return -1;
  if(channel == 0) { *out_celsius = simulated_temp(3);  return 0; }
  if(channel == 2) { *out_celsius = simulated_temp(-8); return 0; }
  return -1;
}

int
sceKernelGetCurrentFanDuty(uint16_t *out_duty, void *scratch) {
  (void)scratch;
  if(!out_duty) return -1;
  *out_duty = (uint16_t)(30 + simulated_temp(0) / 3);
  return 0;
}

int
sceKernelGetCpuUsageAll(int *per_core_pct, int *count_out) {
  if(!per_core_pct || !count_out) return -1;
  for(int i = 0; i < 8; i++) per_core_pct[i] = 10 + (i * 7) % 60;
  *count_out = 8;
  return 0;
}


/* ---- AppInstUtil: report "not available" rather than fake a success ---- */
int sceAppInstUtilInitialize(void) { return 0; }

int
sceAppInstUtilAppInstallPkg(const char *path, void *pkg_info) {
  (void)path; (void)pkg_info;
  return -1;   /* host has no installer */
}

/* Module loading is a no-op off-console; the tile code only checks that the
   call returns, not what it returns. */
int sceKernelLoadStartModule(const char *p, unsigned long a, const void *b,
                             unsigned c, void *d, int *res) {
  (void)p; (void)a; (void)b; (void)c; (void)d;
  if(res) *res = 0;
  return 0;
}
