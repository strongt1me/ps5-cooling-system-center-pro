/* Offline account activation and fake-signin for the PS5.
 *
 * Two independent features:
 *
 *  1. Offline activation (OffAct approach, GPL-3.0):
 *     Sets three sceRegMgr keys — account name, account ID and account flags —
 *     so the console shows the chosen name without touching any file.
 *     Algorithm derived from offact.c by John Törnblom (GPL-3.0-or-later,
 *     https://github.com/ps5-payload-dev/offact, tag v0.4.2).
 *
 *  2. Fake-signin:
 *     Injects the np-fake-signin v1.3 ELF (earthonion) to 127.0.0.1:9021.
 *     The ELF reads the current user itself and writes auth.dat, config.dat
 *     and all registry keys — no arguments needed.
 *
 * Both features require an active jailbreak (elevated ucred).
 *
 * "Restore" deletes auth.dat/config.dat and clears registry keys.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "ps5tm.h"

/* ---------------------------------------------------------------- platform */

#ifndef PS5TM_HOST_TEST
extern int sceRegMgrGetStr(int, char *, size_t) __attribute__((weak));
extern int sceRegMgrGetBin(int, void *,  size_t) __attribute__((weak));
extern int sceRegMgrGetInt(int, int *)           __attribute__((weak));
extern int sceRegMgrSetStr(int, const char *, size_t) __attribute__((weak));
extern int sceRegMgrSetBin(int, const void *, size_t) __attribute__((weak));
extern int sceRegMgrSetInt(int, int)                  __attribute__((weak));
#else
static int sceRegMgrGetStr(int k, char *v, size_t n)
  { (void)k; snprintf(v, n, "TestAcc"); return 0; }
static int sceRegMgrGetBin(int k, void *v, size_t n)
  { (void)k; memset(v, 0, n); return 0; }
static int sceRegMgrGetInt(int k, int *v)
  { (void)k; *v = 0; return 0; }
static int sceRegMgrSetStr(int k, const char *v, size_t n)
  { (void)k; (void)v; (void)n; return 0; }
static int sceRegMgrSetBin(int k, const void *v, size_t n)
  { (void)k; (void)v; (void)n; return 0; }
static int sceRegMgrSetInt(int k, int v)
  { (void)k; (void)v; return 0; }
#endif /* PS5TM_HOST_TEST */


/* ------------------------------------------------- OffAct helpers (GPL-3.0)
 * Directly derived from offact.c by John Törnblom (GPL-3.0-or-later).
 * https://github.com/ps5-payload-dev/offact tag v0.4.2  */

static int
entity_number(int slot, int max, int step, int base, int fallback)
{
  if(slot < 1 || slot > max) return fallback;
  return (slot - 1) * step + base;
}

/* FNV-based hash used as a deterministic account-id. */
static uint64_t
gen_account_id(const char *name)
{
  uint64_t h = (uint64_t)(0x5EAF00D) / (uint64_t)(0xCA7F00D);
  if(*name) {
    do { h = 0x100000001B3ULL * (h ^ (unsigned char)*name++); } while(*name);
  }
  return h;
}

/* Key numbers for slot 1..16.  Slot 1 is the console's default account. */
#define KEY_ACCOUNT_NAME(s)  entity_number((s),16,65536,125829632,127140352)
#define KEY_ACCOUNT_ID(s)    entity_number((s),16,65536,125830400,127141120)
#define KEY_ACCOUNT_FLAGS(s) entity_number((s),16,65536,125831168,127141888)
#define KEY_ACCOUNT_TYPE(s)  entity_number((s),16,65536,125874183,127184903)

/* OffAct activation: set name, id, type and flags for one slot. */
static int
offact_activate(int slot, const char *name, int *rc_out)
{
  if(!sceRegMgrSetStr || !sceRegMgrSetBin || !sceRegMgrSetInt) {
    if(rc_out) *rc_out = -1;
    return -1;
  }

  char   nm[PS5TM_USERNAME_MAX + 1];
  memset(nm, 0, sizeof(nm));
  snprintf(nm, sizeof(nm), "%s", name);

  uint64_t aid = gen_account_id(nm);

  int rc;
  rc = sceRegMgrSetStr(KEY_ACCOUNT_NAME(slot), nm, sizeof(nm));
  if(rc_out) *rc_out = rc;
  if(rc != 0) return -1;

  rc = sceRegMgrSetBin(KEY_ACCOUNT_ID(slot), &aid, sizeof(aid));
  if(rc_out) *rc_out = rc;
  if(rc != 0) return -1;

  /* "np" marks the account as a full PSN account in the slot type field. */
  char typ[PS5TM_ACCOUNT_TYPE_MAX];
  memset(typ, 0, sizeof(typ));
  snprintf(typ, sizeof(typ), "np");
  rc = sceRegMgrSetStr(KEY_ACCOUNT_TYPE(slot), typ, sizeof(typ));
  if(rc_out) *rc_out = rc;
  if(rc != 0) return -1;

  rc = sceRegMgrSetInt(KEY_ACCOUNT_FLAGS(slot), 1);
  if(rc_out) *rc_out = rc;
  return (rc == 0) ? 0 : -1;
}

static int
offact_deactivate(int slot)
{
  if(!sceRegMgrSetStr || !sceRegMgrSetBin || !sceRegMgrSetInt) return -1;

  char empty[PS5TM_USERNAME_MAX + 1];
  memset(empty, 0, sizeof(empty));

  uint64_t zero = 0;
  sceRegMgrSetStr(KEY_ACCOUNT_NAME(slot), empty, sizeof(empty));
  sceRegMgrSetBin(KEY_ACCOUNT_ID(slot), &zero, sizeof(zero));
  sceRegMgrSetStr(KEY_ACCOUNT_TYPE(slot), empty, PS5TM_ACCOUNT_TYPE_MAX);
  sceRegMgrSetInt(KEY_ACCOUNT_FLAGS(slot), 0);
  return 0;
}


/* --------------------------------------------------- fake-signin registry keys
 * Key numbers determined by analysing np-fake-signin.c (earthonion).
 * Each constant is an integer literal — not copyrightable.            */
#define RKEY_USERNAME      125829632
#define RKEY_FLAG_1        125830144
#define RKEY_ACCOUNT_ID    125830400
#define RKEY_NP_A4         125830912
#define RKEY_EMAIL         125830656
#define RKEY_FLAG_2        125831168
#define RKEY_FLAG_3        125831424
#define RKEY_NP_B4         125831936
#define RKEY_FLAG_5C       125832960
#define RKEY_NP_D0         125832704
#define RKEY_NP_D4         125882625
#define RKEY_NP_F4         125833216
#define RKEY_NP_ENV        125874183
#define RKEY_SIGNIN_FLAG   125874185
#define RKEY_ONLINE_ID     125874188
#define RKEY_EXT_1100      125874189
#define RKEY_COUNTRY       125874190
#define RKEY_LANGUAGE      125874191
#define RKEY_LOCALE        125874192
#define RKEY_EXT_1141      125874193
#define RKEY_EXT_114C      125874195
#define RKEY_NP_DC         125854723


/* ---------------------------------------------------------------- file helpers */

#ifndef NP_HOME_ROOT
#define NP_HOME_ROOT "/system_data/priv/home"
#endif
#define NP_PATH_MAX 320

static int
np_path(uint32_t uid, const char *rel, char *out, size_t out_size)
{
  int n = snprintf(out, out_size, "%s/%x/%s", NP_HOME_ROOT, uid, rel);
  return (n > 0 && (size_t)n < out_size) ? 0 : -1;
}

/* ------------------------------------------------------------------ public API */

/* Account ID for a name: same formula as OffAct so the two features agree. */
uint64_t
ps5tm_account_gen_id(const char *name)
{
  return gen_account_id(name ? name : "");
}


/* Status: read the foreground user and check whether the registry shows an
   offline account and/or fake-signin is written. */
ps5tm_account_status_t
ps5tm_account_status(void)
{
  ps5tm_account_status_t s;
  memset(&s, 0, sizeof(s));

  /* user */
  ps5tm_user_t u;
  ps5tm_user_get(&u);
  s.uid   = u.uid;
  s.valid = u.valid;
  if(!u.valid) return s;

  if(!sceRegMgrGetStr || !sceRegMgrGetBin || !sceRegMgrGetInt) return s;

  /* offline activation: slot 1 key */
  char name[PS5TM_USERNAME_MAX + 1];
  memset(name, 0, sizeof(name));
  if(sceRegMgrGetStr(KEY_ACCOUNT_NAME(1), name, sizeof(name)) == 0 && name[0]) {
    s.activated = 1;
    memcpy(s.username, name, sizeof(s.username));
  }

  uint64_t aid = 0;
  sceRegMgrGetBin(KEY_ACCOUNT_ID(1), &aid, sizeof(aid));
  s.account_id = aid;

  /* fake-signin: check presence of auth.dat */
  char ap[NP_PATH_MAX];
  if(np_path(u.uid, "np/auth.dat", ap, sizeof(ap)) == 0) {
    struct stat st;
    if(stat(ap, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0)
      s.fakesignin = 1;
  }

  return s;
}


/* Offline activation — slot 1 by default. */
int
ps5tm_account_activate(const char *username, const char **why)
{
  if(why) *why = NULL;

  if(!username || !*username) {
    if(why) *why = "Kein Benutzername angegeben.";
    return -1;
  }
  if(strlen(username) > (size_t)PS5TM_USERNAME_MAX) {
    if(why) *why = "Name zu lang (max. 16 Zeichen).";
    return -1;
  }

  if(!sceRegMgrSetStr) {
    if(why) *why = "sceRegMgr nicht verfuegbar — ist der Jailbreak geladen?";
    return -1;
  }

  int rc = 0;
  if(offact_activate(1, username, &rc) != 0) {
    static char msg[128];
    snprintf(msg, sizeof(msg),
             "Registrierungszugriff fehlgeschlagen (0x%08X). Jailbreak aktiv?",
             (unsigned)rc);
    if(why) *why = msg;
    PS5TM_WARN("account_activate_failed",
               "Offline-Konto fuer \"%s\" konnte nicht aktiviert werden (0x%08X).",
               username, (unsigned)rc);
    return -1;
  }

  PS5TM_INFO("account_activated",
             "Offline-Konto Slot 1 auf \"%s\" gesetzt (ID 0x%016llX).",
             username, (unsigned long long)gen_account_id(username));
  return 0;
}


/* Fake-signin: injects the np-fake-signin ELF (earthonion) to the local
   loader (127.0.0.1:9021). The ELF reads the current user itself and writes
   auth.dat, config.dat and all registry keys — no arguments needed. */
extern const unsigned char ps5tm_npfakesignin[];
extern const unsigned long ps5tm_npfakesignin_len;

int
ps5tm_account_fakesignin(const char **why)
{
  if(why) *why = NULL;
  PS5TM_INFO("account_fakesignin", "Injiziere np-fake-signin v1.3 (%lu Bytes).",
             ps5tm_npfakesignin_len);
  return ps5tm_loader_inject_mem(ps5tm_npfakesignin, ps5tm_npfakesignin_len, why);
}


/* Restore: delete files and clear registry keys. */
int
ps5tm_account_restore(const char **why)
{
  if(why) *why = NULL;

  ps5tm_user_t u;
  ps5tm_user_get(&u);
  if(!u.valid) {
    if(why) *why = "Kein angemeldeter Benutzer ermittelbar.";
    return -1;
  }

  /* remove files */
  char p[NP_PATH_MAX];
  if(np_path(u.uid, "np/auth.dat",  p, sizeof(p)) == 0) unlink(p);
  if(np_path(u.uid, "config.dat",   p, sizeof(p)) == 0) unlink(p);

  /* clear registry */
  if(sceRegMgrSetStr && sceRegMgrSetBin && sceRegMgrSetInt) {
    char empty[128];
    memset(empty, 0, sizeof(empty));
    uint64_t zero64 = 0;
    int zeroi = 0;

    sceRegMgrSetStr(RKEY_USERNAME,    empty, PS5TM_USERNAME_MAX + 1);
    sceRegMgrSetBin(RKEY_ACCOUNT_ID, &zero64, 8);
    sceRegMgrSetStr(RKEY_EMAIL,       empty, sizeof(empty));
    sceRegMgrSetStr(RKEY_NP_ENV,      empty, 17);
    sceRegMgrSetStr(RKEY_ONLINE_ID,   empty, PS5TM_USERNAME_MAX + 1);
    sceRegMgrSetStr(RKEY_EXT_1100,    empty, sizeof(empty));
    sceRegMgrSetStr(RKEY_COUNTRY,     empty, 4);
    sceRegMgrSetStr(RKEY_LANGUAGE,    empty, 8);
    sceRegMgrSetStr(RKEY_LOCALE,      empty, 40);
    sceRegMgrSetInt(RKEY_SIGNIN_FLAG, 0);
    sceRegMgrSetBin(RKEY_FLAG_1,     &zeroi, 4);
    sceRegMgrSetBin(RKEY_FLAG_2,     &zeroi, 4);
    sceRegMgrSetBin(RKEY_FLAG_3,     &zeroi, 4);
    sceRegMgrSetBin(RKEY_FLAG_5C,    &zeroi, 4);
    sceRegMgrSetBin(RKEY_NP_A4,     &zeroi, 4);
    sceRegMgrSetBin(RKEY_NP_B4,     &zeroi, 4);
    sceRegMgrSetBin(RKEY_NP_D0,     &zeroi, 4);
    sceRegMgrSetBin(RKEY_NP_D4,     &zeroi, 4);
    sceRegMgrSetBin(RKEY_NP_DC,     &zeroi, 4);
    sceRegMgrSetBin(RKEY_NP_F4,     &zeroi, 4);
    sceRegMgrSetStr(RKEY_EXT_1141,   empty, 4);
    sceRegMgrSetStr(RKEY_EXT_114C,   empty, 4);

    offact_deactivate(1);
  }

  PS5TM_INFO("account_restored", "Offline-Konto und Fake-Signin fuer UID 0x%08X zurueckgesetzt.", u.uid);
  return 0;
}
