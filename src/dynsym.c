/* Finding a symbol in a Sony module, the hard way.
 *
 * dlsym(RTLD_DEFAULT, …) is not enough on FW 12.00, and the console said so
 * plainly: the installer loaded libSceAppInstUtil successfully — handle 0x31,
 * result 0 — and still reported both sceAppInstUtilInitialize and
 * sceAppInstUtilAppInstallPkg as missing. The module was there; the lookup
 * was wrong.
 *
 * Two reasons, and this file answers both.
 *
 * RTLD_DEFAULT does not mean "every loaded module". The SDK's own hello_dlfcn
 * sample never uses it for Sony libraries — it does dlopen("libSceRandom.sprx")
 * first and then dlsym on that handle. So we do the same.
 *
 * And Sony's .sprx files do not export by name at all: they export by NID, an
 * encoded hash of the symbol name. A name-based search finds nothing no matter
 * which handle it is given. The SDK ships nid_encode() for exactly this, and
 * kernel_dynlib_resolve() takes the NID — both live in crt1.o, so they cost
 * nothing to use.
 *
 * The four routes are tried cheapest first, and ps5tm_dynsym_route says which
 * one worked, so a failure on an untested firmware reports something better
 * than "missing".
 */

#include <stddef.h>
#include <string.h>

#include "dynsym.h"

int ps5tm_dynsym_route = 0;

const char *
ps5tm_dynsym_route_name(void) {
  switch(ps5tm_dynsym_route) {
    case 1:  return "dlopen+dlsym";
    case 2:  return "dlsym";
    case 3:  return "Kernel/Name";
    case 4:  return "Kernel/NID";
    default: return "nicht gefunden";
  }
}

#ifdef PS5TM_HOST_TEST

void *
ps5tm_dynsym_in(unsigned module_handle, const char *module,
                const char *symbol) {
  (void)module_handle; (void)module; (void)symbol;
  ps5tm_dynsym_route = 0;
  return NULL;
}

#else

#include <dlfcn.h>
#include <stdint.h>
#include <unistd.h>

#include <ps5/kernel.h>
#include <ps5/nid.h>

/* dlopen handles are kept rather than closed: the addresses handed out here
   stay in use for the life of the payload, and dlclose on a system module
   that other code also holds is not worth the risk. Module names are string
   literals, so storing the pointer is safe. */
#define DYNSYM_MAX_MODULES 8

static struct {
  const char *name;
  void       *handle;
} g_opened[DYNSYM_MAX_MODULES];

static int g_opened_count = 0;

static void *
open_module(const char *module) {
  for(int i = 0; i < g_opened_count; i++) {
    if(!strcmp(g_opened[i].name, module)) return g_opened[i].handle;
  }

  void *handle = dlopen(module, RTLD_LAZY);
  if(handle && g_opened_count < DYNSYM_MAX_MODULES) {
    g_opened[g_opened_count].name   = module;
    g_opened[g_opened_count].handle = handle;
    g_opened_count++;
  }
  return handle;
}


void *
ps5tm_dynsym_in(unsigned module_handle, const char *module,
                const char *symbol) {
  if(!symbol) { ps5tm_dynsym_route = 0; return NULL; }

  void *addr = NULL;

  /* The module-scoped lookup goes first, and deliberately so.
   *
   * RTLD_DEFAULT searches our own executable as well, and an undefined weak
   * symbol in our dynamic table answers to its own name: dlsym then returns a
   * stub address that is non-NULL and useless. That is not hypothetical — it
   * is what made every ICC counter read zero while the log cheerfully claimed
   * the symbol had been resolved. Asking a specific module cannot go wrong
   * that way, because our own stubs are not in it. */
  if(module) {
    void *mod = open_module(module);
    if(mod && (addr = dlsym(mod, symbol))) {
      ps5tm_dynsym_route = 1;
      return addr;
    }
  }

  addr = dlsym(RTLD_DEFAULT, symbol);
  if(addr) { ps5tm_dynsym_route = 2; return addr; }

  /* From here on the kernel is asked directly, which needs the payload to
     have escalated already. The handle is checked instead of the return code
     because the SDK does not document the latter's convention. */
  pid_t    pid    = getpid();
  uint32_t handle = module_handle;

  if(!handle && module) kernel_dynlib_handle(pid, module, &handle);
  if(!handle) { ps5tm_dynsym_route = 0; return NULL; }

  intptr_t found = kernel_dynlib_dlsym(pid, handle, symbol);
  if(found > 0) { ps5tm_dynsym_route = 3; return (void *)found; }

  /* The one that matters for Sony's own libraries. */
  char nid[12];
  nid_encode(symbol, nid);

  found = kernel_dynlib_resolve(pid, handle, nid);
  if(found > 0) { ps5tm_dynsym_route = 4; return (void *)found; }

  ps5tm_dynsym_route = 0;
  return NULL;
}

#endif  /* PS5TM_HOST_TEST */


void *
ps5tm_dynsym(const char *module, const char *symbol) {
  return ps5tm_dynsym_in(0, module, symbol);
}
