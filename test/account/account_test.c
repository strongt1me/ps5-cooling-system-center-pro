/* Host test of the account code (src/account.c) against a registry in memory: OffAct's way of activating an existing
 * account, and that undoing the fake sign-in leaves the account alone (the bug of 1.76.1: "Alles zurücksetzen" wiped
 * the name). account.c is included, not linked, to reach its registry stub and its statics; see run.sh. */
#define _GNU_SOURCE
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define PS5TM_HOST_TEST 1
#include "../../src/account.c"

/* ---- the rest of the app, as far as account.c asks */
static int g_user_valid = 1;
void ps5tm_user_get(ps5tm_user_t *out) { memset(out, 0, sizeof(*out)); out->valid = g_user_valid; out->uid = 0x1EA2F4D9; }
int ps5tm_loader_inject_mem(const unsigned char *d, unsigned long n, const char **why) { (void)d; (void)n; if(why) *why = NULL; return 0; }
void ps5tm_log(const char *level, const char *code, const char *fmt, ...) { (void)level; (void)code; (void)fmt; }
const unsigned char ps5tm_npfakesignin[4] = { 0x7f, 'E', 'L', 'F' };
const unsigned long ps5tm_npfakesignin_len = 4;

static int g_ok, g_fail;
#define CHECK(c, ...) do { if(c) g_ok++; else { g_fail++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while(0)

static void reg_clear(void) { memset(g_reg, 0, sizeof(g_reg)); }
static void user(int slot, const char *name) { sceRegMgrSetStr(KEY_ACCOUNT_NAME(slot), name, PS5TM_ACCOUNT_NAME_BUF); }

static const char *
str(int key) {
  static char buf[64];
  return sceRegMgrGetStr(key, buf, sizeof(buf)) == 0 ? buf : "(fehlt)";
}

int
main(void) {
  ps5tm_account_entry_t l[PS5TM_ACCOUNT_SLOTS];

  /* 1. the key numbers are OffAct's, slot by slot */
  CHECK(KEY_ACCOUNT_NAME(1) == 125829632 && KEY_ACCOUNT_NAME(2) == 125829632 + 65536, "name keys");
  CHECK(KEY_ACCOUNT_ID(1) == 125830400 && KEY_ACCOUNT_FLAGS(1) == 125831168 && KEY_ACCOUNT_TYPE(1) == 125874183, "slot 1 keys");
  CHECK(KEY_ACCOUNT_NAME(16) == 125829632 + 15 * 65536, "slot 16");

  /* 2. only slots with a name are accounts */
  reg_clear();
  CHECK(ps5tm_account_list(l, 16) == 0, "empty registry: no accounts");
  user(1, "Yolo");
  user(3, "Gast");
  int n = ps5tm_account_list(l, 16);
  CHECK(n == 2 && l[0].slot == 1 && !strcmp(l[0].name, "Yolo") && l[1].slot == 3 && !strcmp(l[1].name, "Gast"), "two accounts: %d", n);
  CHECK(!l[0].active && l[0].id == 0 && l[0].type[0] == 0, "not activated yet");
  CHECK(ps5tm_account_status().activated == 0, "status: not activated");

  /* 3. activating sets id, type np and flags 4098, and leaves the name alone */
  const char *why = NULL;
  CHECK(ps5tm_account_activate_slot(1, 0, &why) == 0, "activate slot 1: %s", why ? why : "");
  n = ps5tm_account_list(l, 16);
  CHECK(l[0].active && !strcmp(l[0].type, "np") && l[0].flags == 4098, "activated: type %s flags %d", l[0].type, l[0].flags);
  CHECK(l[0].id == gen_account_id("Yolo") && l[0].id != 0, "id made from the name");
  CHECK(!strcmp(str(KEY_ACCOUNT_NAME(1)), "Yolo"), "name untouched");
  CHECK(!l[1].active && l[1].id == 0, "the other account untouched");
  ps5tm_account_status_t st = ps5tm_account_status();
  CHECK(st.activated == 1 && !strcmp(st.username, "Yolo") && st.account_id == l[0].id, "status shows the activated account");

  /* 4. activating again keeps the id that is set; an id given wins */
  uint64_t first = l[0].id;
  CHECK(ps5tm_account_activate_slot(1, 0, &why) == 0, "again");
  ps5tm_account_list(l, 16);
  CHECK(l[0].id == first, "id kept");
  CHECK(ps5tm_account_activate_slot(1, 0xABCDEFull, &why) == 0, "own id");
  ps5tm_account_list(l, 16);
  CHECK(l[0].id == 0xABCDEFull, "own id set: %llx", (unsigned long long)l[0].id);

  /* 5. by name (the older callers); only existing users */
  CHECK(ps5tm_account_activate("Gast", &why) == 0, "by name");
  ps5tm_account_list(l, 16);
  CHECK(l[1].active && l[1].slot == 3, "slot 3 active");
  CHECK(ps5tm_account_activate("Niemand", &why) != 0 && why != NULL, "unknown name refused");
  CHECK(ps5tm_account_activate_slot(2, 0, &why) != 0, "a slot without a user refused");
  CHECK(ps5tm_account_activate_slot(0, 0, &why) != 0 && ps5tm_account_activate_slot(17, 0, &why) != 0, "slot out of range");
  char probe[64];
  CHECK(sceRegMgrGetStr(KEY_ACCOUNT_NAME(2), probe, sizeof(probe)) != 0, "no name was written to slot 2");

  /* 6. the id formula is OffAct's, bytes of 0x80 and above included (char is signed there) */
  CHECK(gen_account_id("") == 0, "empty name");
  uint64_t h = 0;
  for(const signed char *c = (const signed char *)"M\xC3\xBCller"; *c; c++) h = 0x100000001B3ULL * (h ^ (uint64_t)(int64_t)*c);
  CHECK(gen_account_id("M\xC3\xBCller") == h, "umlaut name hashed with signed bytes");
  uint64_t u = 0;
  for(const unsigned char *c = (const unsigned char *)"M\xC3\xBCller"; *c; c++) u = 0x100000001B3ULL * (u ^ *c);
  CHECK(h != u, "and that differs from the unsigned hash (the test would prove nothing otherwise)");

  /* 7. undoing the fake sign-in leaves the account as it is (1.76.2) */
  sceRegMgrSetStr(RKEY_EMAIL, "x@y.z", 128);
  sceRegMgrSetStr(RKEY_ONLINE_ID, "Yolo", 17);
  sceRegMgrSetInt(RKEY_SIGNIN_FLAG, 1);
  ps5tm_account_list(l, 16);
  uint64_t id0 = l[0].id;
  CHECK(ps5tm_account_restore(&why) == 0, "restore");
  CHECK(!strcmp(str(RKEY_EMAIL), "") && !strcmp(str(RKEY_ONLINE_ID), ""), "fake sign-in keys cleared");
  n = ps5tm_account_list(l, 16);
  CHECK(n == 2 && !strcmp(l[0].name, "Yolo") && l[0].active && l[0].id == id0 && l[0].flags == 4098, "account still there and activated");

  printf("%d checks passed, %d failed\n", g_ok, g_fail);
  return g_fail ? 1 : 0;
}
