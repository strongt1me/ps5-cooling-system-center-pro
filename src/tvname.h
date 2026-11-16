/* PS5 Cooling & System Center - Pro: the name of the connected display out of the block that sceVideoOutGetMonitorInfo() fills (tvname.h, 16.11.2026).
 *
 * The call is exported by libSceVideoOut in every firmware from 1.00 to 12.70 (checked against the system libraries with tools/check_fw_imports.py). The
 * name the app looked for until then, sceVideoOutGetHdmiMonitorInfo, exists in none of them. What is known of the new call: two arguments (the handle of
 * the video output and the block), the block is about 76 bytes (disassembly of 12.70). Its layout is not documented, so nothing is assumed beyond two
 * shapes that cannot be mistaken for anything else:
 *   - a printable name at the very start of the block (the assumption of the old code), or
 *   - the name descriptor of an EDID, which is 00 00 00 FC 00 followed by up to 13 characters and a line feed.
 * Anything else yields no name, never a guess: a value that did not arrive stays empty. The caller logs the first bytes of the block once, so the layout
 * can be read from a real console. */
#ifndef PS5TM_TVNAME_H
#define PS5TM_TVNAME_H

#include <stddef.h>
#include <string.h>

#define PS5TM_TVNAME_MIN 3          /* shorter is not a name */

static inline int
ps5tm_tvname_printable(unsigned char c) {
  return c >= 0x20 && c < 0x7f;
}

/* Copies the name found in buf[0..n) to out (outsz bytes, always terminated) and returns 1, or returns 0 and leaves out empty. Trailing blanks are cut. */
static inline int
ps5tm_tvname_from_block(const unsigned char *buf, size_t n, char *out, size_t outsz) {
  if(!out || outsz < 2) return 0;
  out[0] = 0;
  if(!buf || n == 0) return 0;
  size_t max = outsz - 1;
  if(max > 63) max = 63;

  /* 1. a printable name at the start */
  size_t k = 0;
  while(k < n && k < max && ps5tm_tvname_printable(buf[k])) k++;
  if(k >= PS5TM_TVNAME_MIN) {
    memcpy(out, buf, k);
    out[k] = 0;
  } else {
    /* 2. the name descriptor of an EDID: 00 00 00 FC 00, then up to 13 characters, ended by a line feed (0x0A) and blanks */
    static const unsigned char tag[5] = { 0x00, 0x00, 0x00, 0xFC, 0x00 };
    for(size_t i = 0; i + sizeof(tag) < n; i++) {
      if(memcmp(buf + i, tag, sizeof(tag)) != 0) continue;
      size_t j = 0;
      size_t at = i + sizeof(tag);
      while(j < 13 && at + j < n && ps5tm_tvname_printable(buf[at + j]) && j < max) j++;
      if(j >= PS5TM_TVNAME_MIN) {
        memcpy(out, buf + at, j);
        out[j] = 0;
      }
      break;
    }
  }
  size_t len = strlen(out);
  while(len && out[len - 1] == ' ') out[--len] = 0;
  if(len < PS5TM_TVNAME_MIN) { out[0] = 0; return 0; }
  return 1;
}

/* The first `n` bytes of a block as hex for the log ("aa bb cc ..."), terminated; at most (outsz-1)/3 bytes fit. */
static inline void
ps5tm_tvname_hex(const unsigned char *buf, size_t n, char *out, size_t outsz) {
  static const char hex[] = "0123456789abcdef";
  size_t o = 0;
  if(!out || outsz == 0) return;
  for(size_t i = 0; buf && i < n && o + 3 < outsz; i++) {
    out[o++] = hex[buf[i] >> 4];
    out[o++] = hex[buf[i] & 15];
    out[o++] = ' ';
  }
  if(o) o--;                          /* no blank at the end */
  out[o] = 0;
}

#endif
