/* Host test of src/savesfo.c: the param.sfo of a save (read the owner, set the owner, refuse a file that lies about itself). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "savesfo.h"

static int g_fail, g_n;
#define CHECK(c) do { g_n++; if(!(c)) { g_fail++; printf("FAIL line %d: %s\n", __LINE__, #c); } } while(0)

static void w16(uint8_t *p, unsigned v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void w32(uint8_t *p, uint32_t v) { w16(p, v & 0xffff); w16(p + 2, v >> 16); }

typedef struct { const char *key; unsigned fmt; const void *val; unsigned used, cap; } ent_t;

/* A PSF file as a console writes it: header, index, key names, values (each in its capacity). */
static size_t
build(uint8_t *out, const ent_t *e, unsigned n) {
  size_t keys_len = 0, data_len = 0;
  for(unsigned i = 0; i < n; i++) { keys_len += strlen(e[i].key) + 1; data_len += e[i].cap; }
  const size_t key_off = 20 + (size_t)n * 16, data_off = key_off + keys_len;
  memset(out, 0, data_off + data_len);
  memcpy(out, "\0PSF", 4);
  w32(out + 4, 0x101);
  w32(out + 8, (uint32_t)key_off);
  w32(out + 12, (uint32_t)data_off);
  w32(out + 16, n);
  size_t ko = 0, vo = 0;
  for(unsigned i = 0; i < n; i++) {
    uint8_t *ix = out + 20 + (size_t)i * 16;
    w16(ix, (unsigned)ko);
    w16(ix + 2, e[i].fmt);
    w32(ix + 4, e[i].used);
    w32(ix + 8, e[i].cap);
    w32(ix + 12, (uint32_t)vo);
    memcpy(out + key_off + ko, e[i].key, strlen(e[i].key) + 1);
    memcpy(out + data_off + vo, e[i].val, e[i].used);
    ko += strlen(e[i].key) + 1;
    vo += e[i].cap;
  }
  return data_off + data_len;
}

int
main(void) {
  uint8_t params[1024];
  memset(params, 0, sizeof(params));
  for(int i = 0; i < 1024; i++) params[i] = (uint8_t)(i * 7 + 1);
  const uint8_t aid[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
  const uint32_t user = 0x1ea2f4d9;
  ent_t full[] = {
    { "ACCOUNT_ID", 0x0004, aid, 8, 8 },
    { "MAINTITLE", 0x0204, "Spiel Eins", 11, 128 },
    { "PARAMS", 0x0004, params, 1024, 1024 },
    { "SAVEDATA_DIRECTORY", 0x0204, "SAVE0", 6, 32 },
    { "TITLE_ID", 0x0204, "PPSA01234", 10, 16 },
    { "USER_ID", 0x0404, &user, 4, 4 },
  };
  uint8_t buf[8192];
  size_t len = build(buf, full, 6);
  CHECK(savesfo_valid(buf, len));

  char s[64];
  CHECK(savesfo_get_str(buf, len, "TITLE_ID", s, sizeof(s)) == 9 && !strcmp(s, "PPSA01234"));
  CHECK(savesfo_get_str(buf, len, "SAVEDATA_DIRECTORY", s, sizeof(s)) == 5 && !strcmp(s, "SAVE0"));
  CHECK(savesfo_get_str(buf, len, "MAINTITLE", s, sizeof(s)) == 10 && !strcmp(s, "Spiel Eins"));
  CHECK(savesfo_get_str(buf, len, "NICHT_DA", s, sizeof(s)) == -1 && s[0] == '\0');
  char small[4];
  CHECK(savesfo_get_str(buf, len, "TITLE_ID", small, sizeof(small)) == 3 && !strcmp(small, "PPS"));          /* cut, still terminated */
  uint8_t raw[8];
  CHECK(savesfo_get_raw(buf, len, "ACCOUNT_ID", raw, sizeof(raw)) == 8 && !memcmp(raw, aid, 8));
  uint32_t u = 0;
  CHECK(savesfo_get_u32(buf, len, "USER_ID", &u) == 0 && u == user);
  CHECK(savesfo_get_u32(buf, len, "ACCOUNT_ID", &u) == 0);
  CHECK(savesfo_get_u32(buf, len, "TITLE_ID", &u) == 0);            /* ten bytes: four are there */
  size_t off, used, cap;
  CHECK(savesfo_find(buf, len, "PARAMS", &off, &used, &cap) == 0 && used == 1024 && cap == 1024 && off + cap <= len);

  /* the owner: ACCOUNT_ID and USER_ID */
  uint8_t copy[8192];
  memcpy(copy, buf, len);
  CHECK(savesfo_patch_owner(copy, len, 0x1122334455667788ull, 1, 0xCAFEBABE) == 0);
  CHECK(savesfo_get_raw(copy, len, "ACCOUNT_ID", raw, 8) == 8 && raw[0] == 0x88 && raw[7] == 0x11);
  CHECK(savesfo_get_u32(copy, len, "USER_ID", &u) == 0 && u == 0xCAFEBABE);
  CHECK(savesfo_valid(copy, len));
  size_t poff;
  savesfo_find(copy, len, "PARAMS", &poff, NULL, NULL);
  CHECK(!memcmp(copy + poff, params, 1024));                        /* PARAMS untouched while USER_ID exists */
  CHECK(savesfo_patch_owner(copy, len, 5, 0, 0) == 0);              /* the account alone */
  CHECK(savesfo_get_u32(copy, len, "USER_ID", &u) == 0 && u == 0xCAFEBABE);

  /* without USER_ID the second word of PARAMS carries it */
  ent_t noid[] = { full[0], full[1], full[2], full[3], full[4] };
  len = build(buf, noid, 5);
  CHECK(savesfo_valid(buf, len));
  memcpy(copy, buf, len);
  CHECK(savesfo_patch_owner(copy, len, 9, 1, 0x01020304) == 0);
  savesfo_find(copy, len, "PARAMS", &poff, NULL, NULL);
  CHECK(copy[poff + 4] == 0x04 && copy[poff + 7] == 0x01 && copy[poff + 3] == params[3] && copy[poff + 8] == params[8]);

  /* nowhere to put the user id: nothing is changed */
  ent_t noplace[] = { full[0], full[1], full[3], full[4] };
  len = build(buf, noplace, 4);
  memcpy(copy, buf, len);
  CHECK(savesfo_patch_owner(copy, len, 9, 1, 7) == -2 && !memcmp(copy, buf, len));
  CHECK(savesfo_patch_owner(copy, len, 9, 0, 0) == 0);
  /* no account id at all */
  ent_t noacc[] = { full[1], full[3], full[4] };
  len = build(buf, noacc, 3);
  memcpy(copy, buf, len);
  CHECK(savesfo_patch_owner(copy, len, 9, 0, 0) == -1 && !memcmp(copy, buf, len));
  /* an account id too short to hold eight bytes */
  ent_t shortacc[] = { { "ACCOUNT_ID", 0x0004, aid, 4, 4 }, full[4] };
  len = build(buf, shortacc, 2);
  CHECK(savesfo_patch_owner(buf, len, 9, 0, 0) == -1);

  /* files that lie about themselves */
  len = build(buf, full, 6);
  uint8_t lie[8192];
  memcpy(lie, buf, len);
  CHECK(!savesfo_valid(lie, 10));                                   /* shorter than the header */
  CHECK(!savesfo_valid(lie, len - 1));                              /* the last value runs past the end */
  memcpy(lie, buf, len); memcpy(lie, "\0PSX", 4);
  CHECK(!savesfo_valid(lie, len));                                  /* no magic */
  memcpy(lie, buf, len); w32(lie + 16, 0x7fffffff);
  CHECK(!savesfo_valid(lie, len));                                  /* a count the buffer cannot hold */
  memcpy(lie, buf, len); w32(lie + 16, 0);
  CHECK(!savesfo_valid(lie, len));                                  /* no entries */
  memcpy(lie, buf, len); w32(lie + 8, (uint32_t)len + 100);
  CHECK(!savesfo_valid(lie, len));                                  /* key table beyond the buffer */
  memcpy(lie, buf, len); w16(lie + 20, 0xFFF0);
  CHECK(!savesfo_valid(lie, len));                                  /* a key name outside the key table */
  memcpy(lie, buf, len); w32(lie + 20 + 4, 5000);
  CHECK(!savesfo_valid(lie, len));                                  /* a value longer than its room */
  memcpy(lie, buf, len); w32(lie + 20 + 12, 0xFFFFFFF0u);
  CHECK(!savesfo_valid(lie, len));                                  /* a value offset beyond the buffer */
  CHECK(savesfo_find(lie, len, "ACCOUNT_ID", &off, &used, &cap) == -1);          /* the entry that lies is not found */
  CHECK(savesfo_find(lie, len, "TITLE_ID", &off, &used, &cap) == 0);            /* a sound entry still is (the caller asked savesfo_valid first) */
  CHECK(savesfo_patch_owner(lie, len, 1, 0, 0) == -1);
  CHECK(!savesfo_valid(NULL, 0));
  CHECK(savesfo_get_str(NULL, 0, "TITLE_ID", s, sizeof(s)) == -1);
  /* a key name without its end inside the key table */
  memcpy(lie, buf, len);
  {
    uint32_t keys = (uint32_t)(lie[8] | (lie[9] << 8)), data = (uint32_t)(lie[12] | (lie[13] << 8));
    memset(lie + keys, 'A', data - keys);                           /* no NUL left in the table */
  }
  CHECK(!savesfo_valid(lie, len));

  /* bytes inside a value */
  len = build(buf, full, 6);
  memcpy(copy, buf, len);
  const uint8_t four[4] = { 9, 8, 7, 6 };
  CHECK(savesfo_set_raw(copy, len, "PARAMS", 4, four, 4) == 0);
  savesfo_find(copy, len, "PARAMS", &poff, NULL, NULL);
  CHECK(copy[poff + 4] == 9 && copy[poff + 7] == 6 && copy[poff + 3] == params[3] && copy[poff + 8] == params[8] && savesfo_valid(copy, len));
  CHECK(savesfo_set_raw(copy, len, "PARAMS", 1022, four, 4) == -1);                  /* past the bytes in use */
  CHECK(savesfo_set_raw(copy, len, "PARAMS", 0, four, 2000) == -1);
  CHECK(savesfo_set_raw(copy, len, "NICHT_DA", 0, four, 4) == -1);
  CHECK(savesfo_set_raw(copy, len, "TITLE_ID", 8, four, 2) == 0 && savesfo_set_raw(copy, len, "TITLE_ID", 9, four, 2) == -1);   /* ten bytes in use */

  printf("%d checks, %d failed\n", g_n, g_fail);
  return g_fail ? 1 : 0;
}
