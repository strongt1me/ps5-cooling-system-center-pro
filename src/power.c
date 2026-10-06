/* Powering the console down, restarting it, and the one dialog loud enough
 * that nobody can miss it.
 *
 * These are the only calls in the program that do something irreversible to
 * the machine, so each one is logged before it is made — if the console goes
 * dark, the log says who asked for it.
 */

/* No module is ever loaded at run time.
 *
 * sceKernelLoadStartModule() takes the runtime linker's lock, and on FW 12.00
 * it does not always come back. That lock is process-wide, so a single stuck
 * call froze every other thread the moment it needed a symbol resolved — the
 * web server answered exactly one request and then went silent. Moving the
 * call to a background thread did not help, because the lock is shared.
 *
 * Only libraries the ELF is linked against are used, and only through dlsym.
 * A symbol that is not there leaves a null pointer and the feature reports
 * nothing, which is a far better failure than a dead dashboard. */

#include <dlfcn.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "ps5tm.h"

#ifndef PS5TM_HOST_TEST


typedef int (*fn_void_t)(void);
typedef int (*fn_int_t)(int);
typedef int (*fn_dialog_t)(int, const char *);

static fn_void_t   p_power_off  = NULL;
static fn_void_t   p_reboot     = NULL;
static fn_void_t   p_standby    = NULL;
static fn_void_t   p_standby_ok = NULL;
static fn_int_t    p_safemode   = NULL;
static fn_dialog_t p_dialog     = NULL;
static int         g_resolved   = 0;

static void
resolve(void) {
  if(g_resolved) return;
  g_resolved = 1;


  p_power_off = (fn_void_t)dlsym(RTLD_DEFAULT,
                                 "sceSystemServiceRequestPowerOff");
  p_reboot    = (fn_void_t)dlsym(RTLD_DEFAULT,
                                 "sceSystemServiceRequestReboot");
  p_standby   = (fn_void_t)dlsym(RTLD_DEFAULT,
                                 "sceSystemStateMgrEnterStandby");
  p_standby_ok = (fn_void_t)dlsym(RTLD_DEFAULT,
                                  "sceSystemStateMgrIsStandbyModeEnabled");
  p_safemode  = (fn_int_t) dlsym(RTLD_DEFAULT, "sceKernelSetSafemode");
  p_dialog    = (fn_dialog_t)dlsym(RTLD_DEFAULT,
                                   "sceSystemServiceShowErrorDialog");
}


int
ps5tm_power_off(void) {
  ps5tm_sony_api_lock();
  resolve();
  if(!p_power_off) {
    ps5tm_sony_api_unlock();
    PS5TM_WARN("power_off_unavailable",
               "Ausschalten wird von dieser Firmware nicht angeboten.");
    return -1;
  }

  PS5TM_WARN("power_off_requested", "Ausschalten der Konsole angefordert.");
  int rc = p_power_off();
  ps5tm_sony_api_unlock();
  if(rc != 0)
    PS5TM_ERROR("power_off_failed",
                "Ausschalten abgelehnt (0x%08X).", (unsigned)rc);
  return rc;
}


int
ps5tm_power_reboot(void) {
  ps5tm_sony_api_lock();
  resolve();
  if(!p_reboot) {
    ps5tm_sony_api_unlock();
    PS5TM_WARN("reboot_unavailable",
               "Neustart wird von dieser Firmware nicht angeboten.");
    return -1;
  }

  PS5TM_WARN("reboot_requested", "Neustart der Konsole angefordert.");
  int rc = p_reboot();
  ps5tm_sony_api_unlock();
  if(rc != 0)
    PS5TM_ERROR("reboot_failed",
                "Neustart abgelehnt (0x%08X).", (unsigned)rc);
  return rc;
}


int
ps5tm_power_standby(void) {
  ps5tm_sony_api_lock();
  resolve();
  if(!p_standby) {
    ps5tm_sony_api_unlock();
    PS5TM_WARN("standby_unavailable",
               "Ruhemodus wird von dieser Firmware nicht angeboten.");
    return -1;
  }

  /* Rest mode can be switched off in the console's own power settings. Asking
     first turns a silent no-op into an explanation. */
  if(p_standby_ok && p_standby_ok() == 0) {
    ps5tm_sony_api_unlock();
    PS5TM_WARN("standby_disabled",
               "Der Ruhemodus ist in den Energieeinstellungen der PS5 "
               "abgeschaltet — dort zuerst aktivieren.");
    return -2;
  }

  PS5TM_WARN("standby_requested", "Ruhemodus angefordert.");
  int rc = p_standby();
  ps5tm_sony_api_unlock();
  if(rc != 0)
    PS5TM_ERROR("standby_failed",
                "Ruhemodus abgelehnt (0x%08X).", (unsigned)rc);
  return rc;
}


/* Arms the safe-mode flag and restarts.
 *
 * The flag alone changes nothing: the console reads it during the *next* boot,
 * which is why the reboot has to follow immediately. Nothing here is
 * destructive — safe mode is a menu, and one of its entries is "restart the
 * PS5 normally" — but it does interrupt whatever is running, so the caller is
 * expected to have asked first. */
int
ps5tm_power_safemode(void) {
  ps5tm_sony_api_lock();
  resolve();
  if(!p_safemode || !p_reboot) {
    ps5tm_sony_api_unlock();
    PS5TM_WARN("safemode_unavailable",
               "Abgesicherter Modus wird von dieser Firmware nicht "
               "angeboten.");
    return -1;
  }

  PS5TM_WARN("safemode_requested",
             "Neustart in den abgesicherten Modus angefordert.");

  int rc = p_safemode(1);
  if(rc != 0) {
    ps5tm_sony_api_unlock();
    PS5TM_ERROR("safemode_flag_failed",
                "Kennzeichen für den abgesicherten Modus ließ sich nicht "
                "setzen (0x%08X) — es wird nicht neu gestartet.", (unsigned)rc);
    return rc;
  }

  rc = p_reboot();
  ps5tm_sony_api_unlock();
  if(rc != 0) {
    /* The flag is armed but the console is staying up: undo it, otherwise the
       next ordinary restart would land in safe mode without anyone asking. */
    PS5TM_ERROR("safemode_reboot_failed",
                "Neustart abgelehnt (0x%08X) — Kennzeichen wird "
                "zurückgenommen.", (unsigned)rc);
    p_safemode(0);
  }
  return rc;
}


/* Takes the Sony lock and calls into the shell, which can take as long as it
   likes — so not from the fan thread. Temperature warnings reach it through
   the helper thread in fan.c (warn_post), and only that one; anything new that
   wants a dialog has to be as far from regulation as that. */
void
ps5tm_show_dialog(const char *text) {
  ps5tm_sony_api_lock();
  resolve();

  /* No dialog service is not an error worth shouting about — the toast still
     went out, and that is the path that always works. */
  if(!p_dialog) {
    ps5tm_sony_api_unlock();
    ps5tm_notify("%s", text);
    return;
  }

  int rc = p_dialog(0, text);
  ps5tm_sony_api_unlock();
  if(rc != 0) ps5tm_notify("%s", text);
}

#else  /* host test */

int  ps5tm_power_off(void)     { printf("[POWER] Ausschalten\n");  return 0; }
int  ps5tm_power_reboot(void)  { printf("[POWER] Neustart\n");     return 0; }
int  ps5tm_power_standby(void) { printf("[POWER] Ruhemodus\n");    return 0; }
int  ps5tm_power_safemode(void){ printf("[POWER] Abgesichert\n");  return 0; }
void ps5tm_show_dialog(const char *text) { printf("[DIALOG] %s\n", text); }

#endif
