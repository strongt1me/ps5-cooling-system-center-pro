/* The registry keys of Remote Play and the two small helpers around them (24.11.2026), header-only so the console build and the
 * host test (test/rpair) share one copy.
 *
 * The numbers are the key numbers of the console's registry (sceRegMgr): "entry number" = (slot - 1) * stride + base, with the
 * base of each table; out of range gives the fallback key instead of wrapping onto another slot. Taken as facts (integer
 * literals) from ps5upload's payload/include/rp_keys.h (phantomptr, GPL-3.0-or-later; there from ps5-payload-dev's regmgr.h)
 * and from this app's own account.c, which uses the same bases for the account tables: 125830400 is the account id of slot 1
 * in both. A wrong base does not crash, it silently reads or writes another system setting: test/rpair pins every key.
 */
#ifndef PS5TM_RPKEYS_H
#define PS5TM_RPKEYS_H

#include <stddef.h>
#include <stdint.h>

#define PS5TM_RP_USER_SLOTS    16
#define PS5TM_RP_DEVICE_SLOTS  32

/* The system-wide switch "Enable Remote Play". 0: a PIN is made but the registration fails without a reason. */
#define PS5TM_RP_KEY_SERVICE 1098973184u

static inline uint32_t
ps5tm_rp_ent_num(uint32_t slot, uint32_t max, uint32_t stride, uint32_t base, uint32_t fallback) {
  if(slot < 1 || slot > max) return fallback;
  return (slot - 1) * stride + base;
}

/* The user id (the number the console's user service uses) of a user slot 1..16. */
static inline uint32_t ps5tm_rp_key_user_id(uint32_t slot)       { return ps5tm_rp_ent_num(slot, 16, 65536, 125829376u, 127140096u); }
/* Its account id: 8 bytes, all zero while the user was never activated. */
static inline uint32_t ps5tm_rp_key_account_id(uint32_t slot)    { return ps5tm_rp_ent_num(slot, 16, 65536, 125830400u, 127141120u); }
/* Per-user permission "which users may use Remote Play" (firmware 10.00 and later). */
static inline uint32_t ps5tm_rp_key_user_enable(uint32_t slot)   { return ps5tm_rp_ent_num(slot, 16, 65536, 125859841u, 127170561u); }
/* The table of paired devices: 32 records; only the user and the kind are read here, the keys of a pairing are never touched. */
static inline uint32_t ps5tm_rp_key_regist_user(uint32_t slot)   { return ps5tm_rp_ent_num(slot, 32, 65536, 1090584832u, 1092681984u); }
static inline uint32_t ps5tm_rp_key_regist_client(uint32_t slot) { return ps5tm_rp_ent_num(slot, 32, 65536, 1090585600u, 1092682752u); }

/* Does this firmware have the per-user permission? `fw` is kernel_get_fw_version(): BCD, 0x10000000 is 10.00. 0 (unknown) says no,
   so a write to a key that may not exist is never guessed. */
static inline int
ps5tm_rp_fw_has_per_user(uint32_t fw) {
  return fw != 0u && fw >= 0x10000000u;
}

/* Standard base64 with '=' padding, as chiaki-ng wants the account id (the 8 raw bytes). `out` holds 4 * ((n + 2) / 3) + 1 bytes. */
static inline void
ps5tm_rp_base64(const uint8_t *in, size_t n, char *out) {
  static const char tab[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  size_t i = 0, j = 0;
  while(i < n) {
    size_t rem = n - i;
    uint32_t a = in[i], b = rem > 1 ? in[i + 1] : 0, c = rem > 2 ? in[i + 2] : 0;
    uint32_t v = (a << 16) | (b << 8) | c;
    out[j++] = tab[(v >> 18) & 63];
    out[j++] = tab[(v >> 12) & 63];
    out[j++] = rem > 1 ? tab[(v >> 6) & 63] : '=';
    out[j++] = rem > 2 ? tab[v & 63] : '=';
    i += 3;
  }
  out[j] = 0;
}

#endif /* PS5TM_RPKEYS_H */
