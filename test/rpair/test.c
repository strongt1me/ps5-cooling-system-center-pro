/* Host test of the Remote Play basics (24.11.2026): the registry keys (src/rpkeys.h), base64, and the pairing state machine
 * (src/rpair.c) driven with fake calls of the console. Linux/WSL, gcc, AddressSanitizer + UBSan: sh test/rpair/run.sh */
#include <stdio.h>
#include <string.h>

#include "rpair.h"
#include "rpkeys.h"

static int g_checks = 0, g_failed = 0;
#define CHECK(c) do { g_checks++; if(!(c)) { g_failed++; printf("FAIL line %d: %s\n", __LINE__, #c); } } while(0)
#define CHECK_STR(a, b) do { g_checks++; if(strcmp((a), (b)) != 0) { g_failed++; printf("FAIL line %d: \"%s\" != \"%s\"\n", __LINE__, (a), (b)); } } while(0)

/* ------------------------------------------------------------------ keys */

static void
test_keys(void) {
  /* slot 1 of every table is its base, slot n adds (n - 1) * 65536, slot 0 and one past the end give the fallback */
  CHECK(PS5TM_RP_KEY_SERVICE == 1098973184u);
  CHECK(ps5tm_rp_key_user_id(1) == 125829376u);
  CHECK(ps5tm_rp_key_user_id(2) == 125829376u + 65536u);
  CHECK(ps5tm_rp_key_user_id(16) == 125829376u + 15u * 65536u);
  CHECK(ps5tm_rp_key_user_id(0) == 127140096u);
  CHECK(ps5tm_rp_key_user_id(17) == 127140096u);
  CHECK(ps5tm_rp_key_account_id(1) == 125830400u);
  CHECK(ps5tm_rp_key_account_id(16) == 125830400u + 15u * 65536u);
  CHECK(ps5tm_rp_key_account_id(0) == 127141120u);
  CHECK(ps5tm_rp_key_account_id(17) == 127141120u);
  CHECK(ps5tm_rp_key_user_enable(1) == 125859841u);
  CHECK(ps5tm_rp_key_user_enable(3) == 125859841u + 2u * 65536u);
  CHECK(ps5tm_rp_key_user_enable(0) == 127170561u);
  CHECK(ps5tm_rp_key_regist_user(1) == 1090584832u);
  CHECK(ps5tm_rp_key_regist_user(32) == 1090584832u + 31u * 65536u);
  CHECK(ps5tm_rp_key_regist_user(33) == 1092681984u);
  CHECK(ps5tm_rp_key_regist_user(0) == 1092681984u);
  CHECK(ps5tm_rp_key_regist_client(1) == 1090585600u);
  CHECK(ps5tm_rp_key_regist_client(32) == 1090585600u + 31u * 65536u);
  CHECK(ps5tm_rp_key_regist_client(0) == 1092682752u);
  /* the account name of slot 1 (account.c, 125829632) lies 256 above its user id: the tables are one family */
  CHECK(125829632u == ps5tm_rp_key_user_id(1) + 256u);
  /* no key is shared by two tables (a slot of one must never be a slot of another) */
  for(uint32_t s = 1; s <= 16; s++) {
    CHECK(ps5tm_rp_key_user_id(s) != ps5tm_rp_key_account_id(s));
    CHECK(ps5tm_rp_key_account_id(s) != ps5tm_rp_key_user_enable(s));
    CHECK(ps5tm_rp_key_user_id(s) != PS5TM_RP_KEY_SERVICE);
  }
  for(uint32_t s = 1; s <= 32; s++) CHECK(ps5tm_rp_key_regist_user(s) != ps5tm_rp_key_regist_client(s));
  /* the per-user permission only from firmware 10.00; an unknown firmware (0) fails closed */
  CHECK(!ps5tm_rp_fw_has_per_user(0));
  CHECK(!ps5tm_rp_fw_has_per_user(0x09600000u));
  CHECK(!ps5tm_rp_fw_has_per_user(0x09999999u));
  CHECK(ps5tm_rp_fw_has_per_user(0x10000000u));
  CHECK(ps5tm_rp_fw_has_per_user(0x12000043u));
  CHECK(ps5tm_rp_fw_has_per_user(0x14220000u));
}

/* ---------------------------------------------------------------- base64 */

static void
b64(const char *in, size_t n, const char *want) {
  char out[64];
  ps5tm_rp_base64((const uint8_t *)in, n, out);
  CHECK_STR(out, want);
}

static void
test_base64(void) {
  /* RFC 4648, section 10 */
  b64("", 0, "");
  b64("f", 1, "Zg==");
  b64("fo", 2, "Zm8=");
  b64("foo", 3, "Zm9v");
  b64("foob", 4, "Zm9vYg==");
  b64("fooba", 5, "Zm9vYmE=");
  b64("foobar", 6, "Zm9vYmFy");
  /* an account id: 8 raw bytes, 12 characters with one '=' (the old copy of this routine in elf-arsenal gave a trailing 'A') */
  const uint8_t id[8] = {0, 0, 0, 0, 7, 0, 0, 0};
  char out[16];
  ps5tm_rp_base64(id, 8, out);
  CHECK_STR(out, "AAAAAAcAAAA=");
  CHECK(strlen(out) == 12);
  const uint8_t hi[8] = {0xFF, 0xFE, 0x80, 0x7F, 0x01, 0xAB, 0xCD, 0xEF};
  ps5tm_rp_base64(hi, 8, out);
  CHECK_STR(out, "//6AfwGrze8=");
}

/* ----------------------------------------------------------- state machine */

typedef struct {
  int prepare_rc;                 /* 0 = go */
  const char *prepare_code;
  int gen_rc;
  uint32_t pin;
  int confirm_rc;
  uint32_t status, err;
  int devices;                    /* returned by device_count; -1 = unreadable */
  int calls_prepare, calls_gen, calls_confirm, calls_invalidate, calls_notify;
  int last_level;
  char last_msg[160];
  ps5tm_rpair_t *cancel_in_gen;   /* a cancel inside gen_pin (a race) */
  ps5tm_rpair_t *cancel_in_confirm;
} fake_t;

static int
f_prepare(void *c, char *err, size_t ec, char *code, size_t cc) {
  fake_t *f = c;
  f->calls_prepare++;
  if(f->prepare_rc) {
    snprintf(err, ec, "Kein Benutzer mit Konto.");
    snprintf(code, cc, "%s", f->prepare_code ? f->prepare_code : "");
  }
  return f->prepare_rc;
}
static int
f_gen(void *c, uint32_t *pin) {
  fake_t *f = c;
  f->calls_gen++;
  if(f->cancel_in_gen) (void)ps5tm_rpair_cancel(f->cancel_in_gen);
  *pin = f->pin;
  return f->gen_rc;
}
static int
f_confirm(void *c, uint32_t *st, uint32_t *er) {
  fake_t *f = c;
  f->calls_confirm++;
  if(f->cancel_in_confirm) (void)ps5tm_rpair_cancel(f->cancel_in_confirm);
  *st = f->status;
  *er = f->err;
  return f->confirm_rc;
}
static int f_invalidate(void *c) { ((fake_t *)c)->calls_invalidate++; return 0; }
static int f_devices(void *c) { return ((fake_t *)c)->devices; }
static void
f_notify(void *c, const char *msg, int level) {
  fake_t *f = c;
  f->calls_notify++;
  f->last_level = level;
  snprintf(f->last_msg, sizeof(f->last_msg), "%s", msg);
}

static ps5tm_rpair_ops_t
ops_of(fake_t *f) {
  ps5tm_rpair_ops_t o;
  memset(&o, 0, sizeof(o));
  o.ctx = f;
  o.prepare = f_prepare;
  o.gen_pin = f_gen;
  o.confirm = f_confirm;
  o.invalidate = f_invalidate;
  o.device_count = f_devices;
  o.notify = f_notify;
  return o;
}

static void
fresh(ps5tm_rpair_t *p, fake_t *f, ps5tm_rpair_ops_t *o) {
  ps5tm_rpair_init(p);
  memset(f, 0, sizeof(*f));
  f->pin = 47291853u;
  f->devices = 2;
  *o = ops_of(f);
}

static ps5tm_rpair_view_t
view_of(ps5tm_rpair_t *p, int64_t now) {
  ps5tm_rpair_view_t v;
  ps5tm_rpair_view(p, now, &v);
  return v;
}

static void
test_request(void) {
  ps5tm_rpair_t p; fake_t f; ps5tm_rpair_ops_t o;

  /* a PIN: eight digits, waiting, 300 s, the toast carries the digits, the old PIN was given up first */
  fresh(&p, &f, &o);
  CHECK(view_of(&p, 0).state == PS5TM_RPAIR_IDLE);
  CHECK(ps5tm_rpair_request(&p, &o, 1000) == 0);
  ps5tm_rpair_view_t v = view_of(&p, 1000);
  CHECK(v.state == PS5TM_RPAIR_WAITING);
  CHECK_STR(v.pin, "47291853");
  CHECK(v.seconds_left == 300);
  CHECK(f.calls_prepare == 1 && f.calls_gen == 1 && f.calls_invalidate == 1);
  CHECK(f.calls_notify == 1 && strstr(f.last_msg, "47291853") != NULL && f.last_level == 0);
  CHECK(v.code[0] == 0 && v.err[0] == 0);

  /* a PIN with leading zeros, and a number over eight digits is cut to its last eight */
  fresh(&p, &f, &o);
  f.pin = 5u;
  CHECK(ps5tm_rpair_request(&p, &o, 0) == 0);
  CHECK_STR(view_of(&p, 0).pin, "00000005");
  fresh(&p, &f, &o);
  f.pin = 4294967295u;
  CHECK(ps5tm_rpair_request(&p, &o, 0) == 0);
  CHECK(strlen(view_of(&p, 0).pin) == 8);

  /* a second request replaces the live PIN (given up first) */
  fresh(&p, &f, &o);
  CHECK(ps5tm_rpair_request(&p, &o, 0) == 0);
  f.pin = 11112222u;
  CHECK(ps5tm_rpair_request(&p, &o, 5000) == 0);
  CHECK_STR(view_of(&p, 5000).pin, "11112222");
  CHECK(f.calls_invalidate == 2 && f.calls_gen == 2);

  /* prepare fails (no user with an account): no PIN is asked for, the token and the sentence are kept */
  fresh(&p, &f, &o);
  f.prepare_rc = -1;
  f.prepare_code = "no_account";
  CHECK(ps5tm_rpair_request(&p, &o, 0) == -1);
  v = view_of(&p, 0);
  CHECK(v.state == PS5TM_RPAIR_FAILED && v.pin[0] == 0);
  CHECK_STR(v.code, "no_account");
  CHECK(strstr(v.err, "Benutzer") != NULL);
  CHECK(f.calls_gen == 0 && f.calls_invalidate == 0);
  /* ... and one without a token falls back to not_available */
  fresh(&p, &f, &o);
  f.prepare_rc = -1;
  CHECK(ps5tm_rpair_request(&p, &o, 0) == -1);
  CHECK_STR(view_of(&p, 0).code, "not_available");

  /* the console refuses the PIN */
  fresh(&p, &f, &o);
  f.gen_rc = (int)0x80FC0001u;
  CHECK(ps5tm_rpair_request(&p, &o, 0) == -1);
  v = view_of(&p, 0);
  CHECK(v.state == PS5TM_RPAIR_FAILED && v.pin[0] == 0);
  CHECK_STR(v.code, "gen_failed");
  CHECK(strstr(v.err, "80FC0001") != NULL);
  CHECK(f.calls_notify == 0);

  /* cancelled while inside gen_pin: nothing is shown, the PIN just made is owed to the console */
  fresh(&p, &f, &o);
  f.cancel_in_gen = &p;
  CHECK(ps5tm_rpair_request(&p, &o, 0) == -1);
  v = view_of(&p, 0);
  CHECK(v.state == PS5TM_RPAIR_IDLE && v.pin[0] == 0);
  CHECK(f.calls_notify == 0);
  int before = f.calls_invalidate;
  ps5tm_rpair_settle(&p, &o);
  CHECK(f.calls_invalidate == before + 1);
}

static void
test_poll(void) {
  ps5tm_rpair_t p; fake_t f; ps5tm_rpair_ops_t o;

  /* nobody has registered: status 0 keeps it waiting */
  fresh(&p, &f, &o);
  ps5tm_rpair_request(&p, &o, 0);
  CHECK(ps5tm_rpair_poll(&p, &o, 2000) == PS5TM_RPAIR_WAITING);
  CHECK(f.calls_confirm == 1);
  CHECK(view_of(&p, 2000).probes == 1);

  /* paired */
  f.status = PS5TM_RP_CONFIRM_PAIRED;
  CHECK(ps5tm_rpair_poll(&p, &o, 4000) == PS5TM_RPAIR_PAIRED);
  ps5tm_rpair_view_t v = view_of(&p, 4000);
  CHECK(v.state == PS5TM_RPAIR_PAIRED && v.pin[0] == 0 && v.seconds_left == 0);
  CHECK(strstr(f.last_msg, "gekoppelt") != NULL);
  int c = f.calls_confirm;
  CHECK(ps5tm_rpair_poll(&p, &o, 6000) == PS5TM_RPAIR_PAIRED);       /* no probe after the end */
  CHECK(f.calls_confirm == c);

  /* failed: the three reasons, and the console is told again at once */
  uint32_t errs[3] = {PS5TM_RP_ERR_PIN_INVALID, PS5TM_RP_ERR_ACCOUNT_INVALID, 0x80FC1999u};
  const char *codes[3] = {"pin_invalid", "account_invalid", "refused"};
  for(int i = 0; i < 3; i++) {
    fresh(&p, &f, &o);
    ps5tm_rpair_request(&p, &o, 0);
    int inv = f.calls_invalidate;
    f.status = PS5TM_RP_CONFIRM_FAILED;
    f.err = errs[i];
    CHECK(ps5tm_rpair_poll(&p, &o, 1000) == PS5TM_RPAIR_FAILED);
    v = view_of(&p, 1000);
    CHECK_STR(v.code, codes[i]);
    CHECK(v.pin[0] == 0 && v.err[0] != 0);
    CHECK(f.calls_invalidate == inv + 1);
    CHECK(f.last_level == 2);
  }
  fresh(&p, &f, &o);
  ps5tm_rpair_request(&p, &o, 0);
  f.status = PS5TM_RP_CONFIRM_ABORTED;
  CHECK(ps5tm_rpair_poll(&p, &o, 1000) == PS5TM_RPAIR_FAILED);
  CHECK_STR(view_of(&p, 1000).code, "aborted");

  /* an error of the probe itself leaves the PIN waiting */
  fresh(&p, &f, &o);
  ps5tm_rpair_request(&p, &o, 0);
  f.confirm_rc = (int)0x80FC0001u;
  f.status = PS5TM_RP_CONFIRM_PAIRED;                 /* ignored: rc is not 0 */
  CHECK(ps5tm_rpair_poll(&p, &o, 1000) == PS5TM_RPAIR_WAITING);

  /* the table of devices grew: paired even though Confirm said nothing */
  fresh(&p, &f, &o);
  ps5tm_rpair_request(&p, &o, 0);
  f.devices = 3;
  CHECK(ps5tm_rpair_poll(&p, &o, 1000) == PS5TM_RPAIR_PAIRED);
  /* an unreadable table at the start (-1) proves nothing */
  fresh(&p, &f, &o);
  f.devices = -1;
  ps5tm_rpair_request(&p, &o, 0);
  f.devices = 9;
  CHECK(ps5tm_rpair_poll(&p, &o, 1000) == PS5TM_RPAIR_WAITING);

  /* the end of the PIN: timeout, the console is told, one line on the screen; nothing is asked of Confirm any more */
  fresh(&p, &f, &o);
  ps5tm_rpair_request(&p, &o, 0);
  int inv = f.calls_invalidate;
  CHECK(ps5tm_rpair_poll(&p, &o, PS5TM_RPAIR_WAIT_MS - 1) == PS5TM_RPAIR_WAITING);
  c = f.calls_confirm;
  CHECK(ps5tm_rpair_poll(&p, &o, PS5TM_RPAIR_WAIT_MS) == PS5TM_RPAIR_TIMEOUT);
  v = view_of(&p, PS5TM_RPAIR_WAIT_MS);
  CHECK(v.state == PS5TM_RPAIR_TIMEOUT && v.pin[0] == 0);
  CHECK_STR(v.code, "pin_expired");
  CHECK(f.calls_invalidate == inv + 1);
  CHECK(f.calls_confirm == c);
  CHECK(f.last_level == 1);

  /* a cancel while the probe is inside the console: the late answer is dropped, the state stays idle */
  fresh(&p, &f, &o);
  ps5tm_rpair_request(&p, &o, 0);
  f.cancel_in_confirm = &p;
  f.status = PS5TM_RP_CONFIRM_PAIRED;
  CHECK(ps5tm_rpair_poll(&p, &o, 1000) == PS5TM_RPAIR_IDLE);
  CHECK(view_of(&p, 1000).state == PS5TM_RPAIR_IDLE);
}

static void
test_cancel_and_view(void) {
  ps5tm_rpair_t p; fake_t f; ps5tm_rpair_ops_t o;

  fresh(&p, &f, &o);
  CHECK(ps5tm_rpair_cancel(&p) == 0);                       /* nothing to give up */
  ps5tm_rpair_request(&p, &o, 0);
  int inv = f.calls_invalidate;
  CHECK(ps5tm_rpair_cancel(&p) == 1);                       /* the live PIN is owed to the console */
  ps5tm_rpair_view_t v = view_of(&p, 0);
  CHECK(v.state == PS5TM_RPAIR_IDLE && v.pin[0] == 0 && v.err[0] == 0 && v.code[0] == 0);
  CHECK(f.calls_invalidate == inv);                         /* the cancel itself calls nothing */
  ps5tm_rpair_settle(&p, &o);
  CHECK(f.calls_invalidate == inv + 1);
  ps5tm_rpair_settle(&p, &o);
  CHECK(f.calls_invalidate == inv + 1);                     /* owed only once */
  CHECK(ps5tm_rpair_poll(&p, &o, 1000) == PS5TM_RPAIR_IDLE);

  /* the seconds left count down and round up */
  fresh(&p, &f, &o);
  ps5tm_rpair_request(&p, &o, 10000);
  CHECK(view_of(&p, 10000).seconds_left == 300);
  CHECK(view_of(&p, 10000 + 1).seconds_left == 300);
  CHECK(view_of(&p, 10000 + 1000).seconds_left == 299);
  CHECK(view_of(&p, 10000 + PS5TM_RPAIR_WAIT_MS - 400).seconds_left == 1);
  CHECK(view_of(&p, 10000 + PS5TM_RPAIR_WAIT_MS).seconds_left == 0);

  CHECK_STR(ps5tm_rpair_state_name(PS5TM_RPAIR_IDLE), "idle");
  CHECK_STR(ps5tm_rpair_state_name(PS5TM_RPAIR_WAITING), "waiting");
  CHECK_STR(ps5tm_rpair_state_name(PS5TM_RPAIR_PAIRED), "paired");
  CHECK_STR(ps5tm_rpair_state_name(PS5TM_RPAIR_FAILED), "failed");
  CHECK_STR(ps5tm_rpair_state_name(PS5TM_RPAIR_TIMEOUT), "timeout");
  CHECK_STR(ps5tm_rpair_state_name(99), "unknown");
}

int
main(void) {
  test_keys();
  test_base64();
  test_request();
  test_poll();
  test_cancel_and_view();
  printf("%d checks, %d failed\n", g_checks, g_failed);
  return g_failed ? 1 : 0;
}
