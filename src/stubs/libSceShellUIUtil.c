/* Link stub for libSceShellUIUtil.sprx, which the SDK does not ship.
 *
 * Built into gen/stubs/ by the Makefile and never run: it only gives the
 * linker the two names library.c uses and records the module as NEEDED. The
 * console's own /system_ex/common_ex/lib/libSceShellUIUtil.sprx is loaded at
 * start like every other module the app links, and the calls go there. */

int sceShellUIUtilInitialize(void) { return 0; }

int sceShellUIUtilLaunchByUri(const char *uri, void *param) {
  (void)uri;
  (void)param;
  return 0;
}
