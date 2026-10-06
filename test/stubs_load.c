/* Load profile: idle 65 °C -> gaming 78 °C -> back to idle, each phase with
 * the ±2 °C sensor jitter seen on the real console. Verifies that cooling
 * engages under load, that it does not oscillate, and that it releases once
 * the console has genuinely cooled. */

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

/* 0-40 s idle 65, 40-100 s load ramping to 78, 100-160 s cooling back down. */
static int
profile_base(void) {
  static time_t t0 = 0;
  if(!t0) t0 = time(NULL);
  long t = (long)(time(NULL) - t0);

  if(t < 40)  return 65;
  if(t < 100) return 65 + (int)((t - 40) * 13 / 60);   /* 65 -> 78 */
  if(t < 160) return 78 - (int)((t - 100) * 16 / 60);  /* 78 -> 62 */
  return 62;
}

static int noisy(int base) { return base + (rand() % 5) - 2; }

int sceKernelGetCpuTemperature(int *o) { *o = noisy(profile_base()); return 0; }

int
sceKernelGetSocSensorTemperature(int ch, int *o) {
  if(ch == 0) { *o = noisy(profile_base() - 6); return 0; }
  if(ch == 2) { *o = noisy(45); return 0; }
  return -1;
}

int
sceKernelGetCurrentFanDuty(uint16_t *o, void *s) {
  (void)s;
  *o = 131;
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
