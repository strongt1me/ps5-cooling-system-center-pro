#ifndef PS5TM_DYNSYM_H
#define PS5TM_DYNSYM_H

/* Find an exported function, trying every route the SDK offers.
 *
 * `module` is the .sprx file name to search, e.g. "libkernel_sys.sprx"; pass
 * NULL to use plain dlsym alone. Returns NULL when nothing found it — never a
 * bogus address. */
void *ps5tm_dynsym(const char *module, const char *symbol);

/* Same, but starting from a module handle you already hold — the one
 * sceKernelLoadStartModule() handed back, for instance. Pass 0 to have the
 * module looked up by name instead. */
void *ps5tm_dynsym_in(unsigned module_handle, const char *module,
                      const char *symbol);

/* Which route produced the last result, purely for diagnostics:
 * 0 none, 1 dlopen+dlsym, 2 dlsym(RTLD_DEFAULT), 3 kernel by name,
 * 4 kernel by NID. */
extern int ps5tm_dynsym_route;
const char *ps5tm_dynsym_route_name(void);

#endif
