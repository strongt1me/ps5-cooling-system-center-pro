/* PS5 Cooling & System Center - Pro: the param.sfo of a save (savesfo.h). */

#include <string.h>

#include "savesfo.h"

#define SFO_HEADER 20u
#define SFO_ENTRY  16u

static uint16_t
rd16(const uint8_t *p) {
  return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t
rd32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void
wr32(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
  p[2] = (uint8_t)(v >> 16);
  p[3] = (uint8_t)(v >> 24);
}

/* The three numbers of the header, checked against the size. */
static int
header(const uint8_t *buf, size_t len, uint32_t *keys, uint32_t *data, uint32_t *count) {
  if(!buf || len < SFO_HEADER || memcmp(buf, "\0PSF", 4) != 0) return -1;
  *keys = rd32(buf + 8);
  *data = rd32(buf + 12);
  *count = rd32(buf + 16);
  if(!*count || *count > (len - SFO_HEADER) / SFO_ENTRY) return -1;
  if(*keys < SFO_HEADER + (uint64_t)*count * SFO_ENTRY || *keys >= *data || *data > len) return -1;
  return 0;
}

int
savesfo_valid(const uint8_t *buf, size_t len) {
  uint32_t keys, data, count;
  if(header(buf, len, &keys, &data, &count) != 0) return 0;
  for(uint32_t i = 0; i < count; i++) {
    const uint8_t *e = buf + SFO_HEADER + (size_t)i * SFO_ENTRY;
    const uint64_t kp = (uint64_t)keys + rd16(e);
    const uint32_t used = rd32(e + 4), cap = rd32(e + 8), off = rd32(e + 12);
    const uint64_t vp = (uint64_t)data + off;
    if(kp >= data || !memchr(buf + kp, 0, data - kp)) return 0;          /* the key ends inside the key table */
    if(used > cap || vp > len || cap > len - vp) return 0;               /* the value lies inside the buffer */
  }
  return 1;
}

int
savesfo_find(const uint8_t *buf, size_t len, const char *key, size_t *off, size_t *used, size_t *cap) {
  uint32_t keys, data, count;
  if(!key || header(buf, len, &keys, &data, &count) != 0) return -1;
  for(uint32_t i = 0; i < count; i++) {
    const uint8_t *e = buf + SFO_HEADER + (size_t)i * SFO_ENTRY;
    const uint64_t kp = (uint64_t)keys + rd16(e);
    if(kp >= data) continue;
    const size_t room = data - (size_t)kp;
    const uint8_t *z = memchr(buf + kp, 0, room);
    if(!z || strcmp((const char *)(buf + kp), key) != 0) continue;
    const uint32_t u = rd32(e + 4), c = rd32(e + 8), o = rd32(e + 12);
    const uint64_t vp = (uint64_t)data + o;
    if(u > c || vp > len || c > len - vp) return -1;
    if(off) *off = (size_t)vp;
    if(used) *used = u;
    if(cap) *cap = c;
    return 0;
  }
  return -1;
}

int
savesfo_get_str(const uint8_t *buf, size_t len, const char *key, char *out, size_t out_cap) {
  size_t off, used;
  if(!out || !out_cap) return -1;
  out[0] = '\0';
  if(savesfo_find(buf, len, key, &off, &used, NULL) != 0) return -1;
  size_t n = used < out_cap - 1 ? used : out_cap - 1;
  memcpy(out, buf + off, n);
  out[n] = '\0';
  return (int)strlen(out);
}

int
savesfo_get_raw(const uint8_t *buf, size_t len, const char *key, uint8_t *out, size_t out_cap) {
  size_t off, used;
  if(!out || savesfo_find(buf, len, key, &off, &used, NULL) != 0) return -1;
  size_t n = used < out_cap ? used : out_cap;
  memcpy(out, buf + off, n);
  return (int)n;
}

int
savesfo_get_u32(const uint8_t *buf, size_t len, const char *key, uint32_t *out) {
  size_t off, used;
  if(savesfo_find(buf, len, key, &off, &used, NULL) != 0 || used < 4) return -1;
  *out = rd32(buf + off);
  return 0;
}

int
savesfo_set_raw(uint8_t *buf, size_t len, const char *key, size_t off, const void *data, size_t n) {
  size_t voff, used;
  if(!buf || !data || savesfo_find(buf, len, key, &voff, &used, NULL) != 0) return -1;
  if(off > used || n > used - off) return -1;
  memcpy(buf + voff + off, data, n);
  return 0;
}

int
savesfo_patch_owner(uint8_t *buf, size_t len, uint64_t account_id, int has_uid, uint32_t uid) {
  size_t aoff, aused, acap, uoff = 0, uused = 0, poff, pused;
  if(savesfo_find(buf, len, "ACCOUNT_ID", &aoff, &aused, &acap) != 0 || acap < 8 || aused < 8) return -1;
  int place = 0;                                              /* 1: USER_ID, 2: the second word of PARAMS */
  if(has_uid) {
    if(savesfo_find(buf, len, "USER_ID", &uoff, &uused, NULL) == 0 && uused >= 4) place = 1;
    else if(savesfo_find(buf, len, "PARAMS", &poff, &pused, NULL) == 0 && pused >= 8) { uoff = poff + 4; place = 2; }
    if(!place) return -2;
  }
  for(int i = 0; i < 8; i++) buf[aoff + (size_t)i] = (uint8_t)(account_id >> (8 * i));
  if(has_uid) wr32(buf + uoff, uid);
  (void)place;
  return 0;
}
