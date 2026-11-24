/* Offline account activation and fake-signin for the PS5.
 *
 * Two independent features:
 *
 *  1. Offline activation (OffAct, GPL-3.0):
 *     Works on accounts that exist: lists slots 1..16 whose name is set and, for the chosen one, sets the account id,
 *     the type "np" and the flags 4098 — as OffAct's main.c does (OnDialogOutcome). The name is not written; it is the
 *     console user's own. Derived from offact.c / main.c by John Törnblom (GPL-3.0-or-later,
 *     https://github.com/ps5-payload-dev/offact, v0.4.2; master has no commit beyond it as of 04.11.2026).
 *
 *  2. Fake-signin:
 *     Injects the np-fake-signin v1.3 ELF (earthonion) to 127.0.0.1:9021.
 *     The ELF reads the current user itself and writes auth.dat, config.dat
 *     and all registry keys — no arguments needed.
 *
 * Both features require an active jailbreak (elevated ucred).
 *
 * "Restore" deletes auth.dat/config.dat and clears the registry keys that are
 * fake-signin's own. It leaves the account-identity keys (name, account id,
 * type, flags) untouched: those are the same registry fields OffAct's offline
 * activation uses for slot 1 (see the KEY_ACCOUNT_* / RKEY_* numbers below —
 * four of them are numerically identical), so clearing them on every restore
 * silently deactivated an offline account the person had set up separately,
 * leaving the console with no name for that account and breaking anything
 * that expects the slot to have an identity (review 04.11.2026, console
 * report: "Alles zurücksetzen" + reboot left the account nameless and broke
 * Y2JB). Restoring is meant to undo only what fake-signin itself wrote.
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
/* A small registry in memory for the host test (test/account): a key holds what was last written to it, a key never
   written reads as an error, as an unknown key does on the console. */
static struct { int key, used; char s[64]; uint64_t b; int i; } g_reg[128];
static int reg_slot(int k, int make) {
  for(int i = 0; i < 128; i++) if(g_reg[i].used && g_reg[i].key == k) return i;
  if(!make) return -1;
  for(int i = 0; i < 128; i++) if(!g_reg[i].used) { g_reg[i].used = 1; g_reg[i].key = k; return i; }
  return -1;
}
static int sceRegMgrGetStr(int k, char *v, size_t n)
  { int i = reg_slot(k, 0); if(i < 0) return -1; snprintf(v, n, "%s", g_reg[i].s); return 0; }
static int sceRegMgrGetBin(int k, void *v, size_t n)
  { int i = reg_slot(k, 0); if(i < 0) return -1; memcpy(v, &g_reg[i].b, n < 8 ? n : 8); return 0; }
static int sceRegMgrGetInt(int k, int *v)
  { int i = reg_slot(k, 0); if(i < 0) return -1; *v = g_reg[i].i; return 0; }
static int sceRegMgrSetStr(int k, const char *v, size_t n)
  { int i = reg_slot(k, 1); if(i < 0) return -1; snprintf(g_reg[i].s, sizeof(g_reg[i].s), "%.*s", (int)n, v); return 0; }
static int sceRegMgrSetBin(int k, const void *v, size_t n)
  { int i = reg_slot(k, 1); if(i < 0) return -1; g_reg[i].b = 0; memcpy(&g_reg[i].b, v, n < 8 ? n : 8); return 0; }
static int sceRegMgrSetInt(int k, int v)
  { int i = reg_slot(k, 1); if(i < 0) return -1; g_reg[i].i = v; return 0; }
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

/* FNV-based hash used as a deterministic account-id. OffAct hashes `char`, which is signed on this platform: a byte of
   0x80 or more (an umlaut in UTF-8) enters as a negative number extended to 64 bits. The same here, so a name gets the
   same id as in OffAct. */
static uint64_t
gen_account_id(const char *name)
{
  uint64_t h = (uint64_t)(0x5EAF00D) / (uint64_t)(0xCA7F00D);
  if(*name) {
    do { h = 0x100000001B3ULL * (h ^ (uint64_t)(int64_t)(signed char)*name++); } while(*name);
  }
  return h;
}

/* Key numbers for slot 1..16.  Slot 1 is the console's default account. */
#define KEY_ACCOUNT_NAME(s)  entity_number((s),16,65536,125829632,127140352)
#define KEY_ACCOUNT_ID(s)    entity_number((s),16,65536,125830400,127141120)
#define KEY_ACCOUNT_FLAGS(s) entity_number((s),16,65536,125831168,127141888)
#define KEY_ACCOUNT_TYPE(s)  entity_number((s),16,65536,125874183,127184903)

#define ACCOUNT_FLAGS_ACTIVE 4098   /* what OffAct's main.c sets (account_flags = 4098) */

/* One slot as OffAct's GetItemLabel reads it. -1 when the slot has no name (no such user) or the registry is not there. */
static int
read_slot(int slot, ps5tm_account_entry_t *e)
{
  memset(e, 0, sizeof(*e));
  e->slot = slot;
  if(!sceRegMgrGetStr || !sceRegMgrGetBin || !sceRegMgrGetInt) return -1;

  char name[PS5TM_ACCOUNT_NAME_BUF];
  memset(name, 0, sizeof(name));
  if(sceRegMgrGetStr(KEY_ACCOUNT_NAME(slot), name, sizeof(name)) != 0 || !name[0]) return -1;
  memcpy(e->name, name, sizeof(e->name));
  e->name[sizeof(e->name) - 1] = 0;

  uint64_t id = 0;
  if(sceRegMgrGetBin(KEY_ACCOUNT_ID(slot), &id, sizeof(id)) == 0) e->id = id;

  char type[PS5TM_ACCOUNT_TYPE_MAX];
  memset(type, 0, sizeof(type));
  if(sceRegMgrGetStr(KEY_ACCOUNT_TYPE(slot), type, sizeof(type)) == 0)
    memcpy(e->type, type, sizeof(e->type));
  e->type[sizeof(e->type) - 1] = 0;

  int flags = 0;
  if(sceRegMgrGetInt(KEY_ACCOUNT_FLAGS(slot), &flags) == 0) e->flags = flags;

  e->active = e->id != 0 && !strcmp(e->type, "np");
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

  /* offline activation: the first account that is set up (id and type "np"); the earlier check, "slot 1 has a name",
     held for every console user, activated or not */
  ps5tm_account_entry_t list[PS5TM_ACCOUNT_SLOTS];
  int n = ps5tm_account_list(list, PS5TM_ACCOUNT_SLOTS);
  for(int i = 0; i < n; i++) {
    if(!list[i].active) continue;
    s.activated = 1;
    snprintf(s.username, sizeof(s.username), "%s", list[i].name);
    s.account_id = list[i].id;
    break;
  }

  /* fake-signin: check presence of auth.dat */
  char ap[NP_PATH_MAX];
  if(np_path(u.uid, "np/auth.dat", ap, sizeof(ap)) == 0) {
    struct stat st;
    if(stat(ap, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0)
      s.fakesignin = 1;
  }

  return s;
}


/* The accounts that exist: slots with a name, in slot order. */
int
ps5tm_account_list(ps5tm_account_entry_t *out, int max)
{
  int n = 0;
  for(int slot = 1; slot <= PS5TM_ACCOUNT_SLOTS && n < max; slot++) {
    if(read_slot(slot, &out[n]) == 0) n++;
  }
  return n;
}


/* OffAct's activation of one account (main.c, OnDialogOutcome): id, type "np", flags 4098, in that order. The id is the
   one given, else the one already set, else made from the name (OnActivateItem proposes the same). */
int
ps5tm_account_activate_slot(int slot, uint64_t id, const char **why)
{
  if(why) *why = NULL;

  ps5tm_account_entry_t e;
  if(slot < 1 || slot > PS5TM_ACCOUNT_SLOTS || read_slot(slot, &e) != 0) {
    if(why) *why = "Auf diesem Platz gibt es keinen Benutzer.";
    return -1;
  }
  if(!sceRegMgrSetStr || !sceRegMgrSetBin || !sceRegMgrSetInt) {
    if(why) *why = "sceRegMgr nicht verfuegbar — ist der Jailbreak geladen?";
    return -1;
  }
  if(!id) id = e.id ? e.id : gen_account_id(e.name);

  char type[PS5TM_ACCOUNT_TYPE_MAX];
  memset(type, 0, sizeof(type));
  snprintf(type, sizeof(type), "np");

  int rc = sceRegMgrSetBin(KEY_ACCOUNT_ID(slot), &id, sizeof(id));
  if(rc == 0) rc = sceRegMgrSetStr(KEY_ACCOUNT_TYPE(slot), type, sizeof(type));
  if(rc == 0) rc = sceRegMgrSetInt(KEY_ACCOUNT_FLAGS(slot), ACCOUNT_FLAGS_ACTIVE);
  if(rc != 0) {
    static char msg[128];
    snprintf(msg, sizeof(msg), "Registrierungszugriff fehlgeschlagen (0x%08X). Jailbreak aktiv?", (unsigned)rc);
    if(why) *why = msg;
    PS5TM_WARN("account_activate_failed", "Offline-Konto Platz %d (\"%s\") konnte nicht aktiviert werden (0x%08X).",
               slot, e.name, (unsigned)rc);
    return -1;
  }

  PS5TM_INFO("account_activated", "Offline-Konto Platz %d (\"%s\") aktiviert: ID 0x%016llX, Typ np, Kennzeichen %d.",
             slot, e.name, (unsigned long long)id, ACCOUNT_FLAGS_ACTIVE);
  return 0;
}


/* The account with this name. The older callers (the full-screen app of 1.76.4, a page still in a browser's cache)
   send a name instead of a slot. */
int
ps5tm_account_activate(const char *username, const char **why)
{
  if(why) *why = NULL;
  if(!username || !*username) {
    if(why) *why = "Kein Benutzer angegeben.";
    return -1;
  }
  ps5tm_account_entry_t list[PS5TM_ACCOUNT_SLOTS];
  int n = ps5tm_account_list(list, PS5TM_ACCOUNT_SLOTS);
  for(int i = 0; i < n; i++)
    if(!strcmp(list[i].name, username)) return ps5tm_account_activate_slot(list[i].slot, 0, why);
  if(why) *why = "Es gibt keinen Benutzer mit diesem Namen. Das Offline-Konto kann nur für vorhandene Benutzer aktiviert werden.";
  return -1;
}


/* Fake-signin: injects the np-fake-signin ELF (earthonion) to the local
   loader (127.0.0.1:9021). The ELF reads the current user itself and writes
   auth.dat, config.dat and all registry keys — no arguments needed. */
extern const unsigned char ps5tm_npfakesignin[];
extern const unsigned long ps5tm_npfakesignin_len;       /* the zlib stream of the ELF (tools/gen_blob.py) */
extern const unsigned long ps5tm_npfakesignin_raw_len;

int
ps5tm_account_fakesignin(const char **why)
{
  if(why) *why = NULL;
  PS5TM_INFO("account_fakesignin", "Injiziere np-fake-signin v1.3 (%lu Bytes).",
             ps5tm_npfakesignin_raw_len);
  size_t n = 0;
  unsigned char *elf = ps5tm_blob_inflate(ps5tm_npfakesignin, ps5tm_npfakesignin_len, ps5tm_npfakesignin_raw_len, &n);
  if(!elf) {
    if(why) *why = "np-fake-signin ließ sich nicht entpacken.";
    return -1;
  }
  int rc = ps5tm_loader_inject_mem(elf, n, why);
  free(elf);
  return rc;
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

  /* Clear fake-signin's own registry keys. Not RKEY_USERNAME, RKEY_ACCOUNT_ID,
     RKEY_FLAG_2 or RKEY_NP_ENV: those are the same fields as the OffAct slot-1
     account name, id, flags and type (KEY_ACCOUNT_NAME/ID/FLAGS/TYPE(1) above),
     so clearing them here would deactivate an offline account that may still
     be in use — this function undoes fake-signin only. */
  if(sceRegMgrSetStr && sceRegMgrSetBin && sceRegMgrSetInt) {
    char empty[128];
    memset(empty, 0, sizeof(empty));
    int zeroi = 0;

    sceRegMgrSetStr(RKEY_EMAIL,       empty, sizeof(empty));
    sceRegMgrSetStr(RKEY_ONLINE_ID,   empty, PS5TM_USERNAME_MAX + 1);
    sceRegMgrSetStr(RKEY_EXT_1100,    empty, sizeof(empty));
    sceRegMgrSetStr(RKEY_COUNTRY,     empty, 4);
    sceRegMgrSetStr(RKEY_LANGUAGE,    empty, 8);
    sceRegMgrSetStr(RKEY_LOCALE,      empty, 40);
    sceRegMgrSetInt(RKEY_SIGNIN_FLAG, 0);
    sceRegMgrSetBin(RKEY_FLAG_1,     &zeroi, 4);
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
  }

  PS5TM_INFO("account_restored", "Fake-Signin fuer UID 0x%08X zurueckgesetzt (Offline-Konto blieb unangetastet).", u.uid);
  return 0;
}
