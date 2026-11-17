/* PS5 Cooling & System Center - Pro: the name of the connected display out of the block that sceVideoOutGetMonitorInfo() fills (tvname.h, 16.11.2026,
 * corrected 17.11.2026 twice).
 *
 * What is known of the call, read from libSceVideoOut and libSceAvSetting of every firmware from 1.00 to 12.70 (7.00 is encrypted) and measured on a console:
 *   - exported by libSceVideoOut in all of them (the name the app looked for until 16.11., sceVideoOutGetHdmiMonitorInfo, exists in none);
 *   - two arguments: the display (0x7000 is the main output) and the block. The handle of sceVideoOutOpen is refused with 0x80290018 (measured on the .52:
 *     it is a large composite value, 1309671680 there, not a small index as an earlier note said);
 *   - it fills exactly 0x400 = 1024 bytes into the block (a memcpy of 0x400 at the end of the function, the same count in all 41 readable firmwares; the
 *     first version of this file said "about 76 bytes" and gave 512: wrong, read from a neighbouring function). A buffer must hold 1024;
 *   - the block is NOT an EDID image. Measured on the .52 (Samsung TV of 2019, 17.11.2026) it starts 4c 2d f1 0f xx xx xx xx 01 1d 02 53 xx xx xx xx
 *     4e 47 00 ..., then flag words (0c 40 at 28 and 36). The first ten bytes are the maker "SAM" (0x4C2D, three 5 bit letters), the product code 0x0FF1, the
 *     serial number (4..7), the week and the year (0x1d = 2019), copied from the EDID by the system; what follows is the system's own structure, and no name
 *     descriptor (00 00 00 FC 00) is in it. Where the name of the display is, if it is in the block at all, is not known yet: the log shows the non-zero parts
 *     and the text runs of the block once, so the layout can be read from a console. A block that does start with the EDID header is read the usual way.
 * A name is only reported when an EDID name descriptor (00 00 00 FC 00 + up to 13 characters, ended by a line feed) is in the block; nothing is guessed. The
 * serial number of the display is never written to the log (both places where it could be, bytes 4..7 and 12..15, are left out of what is logged). */
#ifndef PS5TM_TVNAME_H
#define PS5TM_TVNAME_H

#include <stddef.h>
#include <string.h>

#define PS5TM_EDID_SIZE 1024        /* what sceVideoOutGetMonitorInfo writes */
#define PS5TM_TVNAME_MIN 3          /* shorter is not a name */

static inline int
ps5tm_tvname_printable(unsigned char c) {
  return c >= 0x20 && c < 0x7f;
}

/* 1 when the block starts with the EDID header 00 FF FF FF FF FF FF 00. */
static inline int
ps5tm_edid_header_ok(const unsigned char *b, size_t n) {
  static const unsigned char hdr[8] = { 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00 };
  return b && n >= 128 && memcmp(b, hdr, sizeof(hdr)) == 0;
}

/* The three letters of a maker id at `at` (two bytes: three 5 bit groups, 1 = A) into out[4]; 1 when they are letters. */
static inline int
ps5tm_edid_letters(const unsigned char *b, size_t n, size_t at, char out[4]) {
  out[0] = 0;
  if(!b || at + 2 > n) return 0;
  unsigned v = ((unsigned)b[at] << 8) | b[at + 1];
  if(v & 0x8000u) return 0;
  for(int i = 0; i < 3; i++) {
    unsigned c = (v >> (10 - 5 * i)) & 31u;
    if(c < 1 || c > 26) { out[0] = 0; return 0; }
    out[i] = (char)('A' + (int)c - 1);
  }
  out[3] = 0;
  return 1;
}

/* Where the maker id is: 8 with the EDID header, 0 without it (the block of the console), -1 when neither reads as three letters. */
static inline int
ps5tm_edid_id_at(const unsigned char *b, size_t n) {
  char l[4];
  if(!b || n < 128) return -1;
  if(ps5tm_edid_header_ok(b, n)) return ps5tm_edid_letters(b, n, 8, l) ? 8 : -1;
  return ps5tm_edid_letters(b, n, 0, l) ? 0 : -1;
}

/* The maker (three letters) into out[4]; 1 when found. */
static inline int
ps5tm_edid_maker(const unsigned char *b, size_t n, char out[4]) {
  int at = ps5tm_edid_id_at(b, n);
  out[0] = 0;
  return at >= 0 && ps5tm_edid_letters(b, n, (size_t)at, out);
}

/* The product code (the two bytes behind the maker id, little endian); 0 when the maker id was not found. */
static inline unsigned
ps5tm_edid_product(const unsigned char *b, size_t n) {
  int at = ps5tm_edid_id_at(b, n);
  return at >= 0 ? (unsigned)b[at + 2] | ((unsigned)b[at + 3] << 8) : 0;
}

/* The year of manufacture (1990 + the byte two bytes behind the serial number, i.e. 8 behind the maker id); 0 when not plausible. */
static inline unsigned
ps5tm_edid_year(const unsigned char *b, size_t n) {
  int at = ps5tm_edid_id_at(b, n);
  if(at < 0) return 0;
  unsigned y = 1990u + b[at + 9];
  return y >= 1996u && y <= 2040u ? y : 0;
}

/* Where descriptors of the kinds 0xFC (name), 0xFD (ranges), 0xFE (text) and 0xFF (serial) start in the first 256 bytes, as "fc@46 fd@64 ..." into out;
   empty when there are none. */
static inline void
ps5tm_edid_descriptors(const unsigned char *b, size_t n, char *out, size_t outsz) {
  static const char hex[] = "0123456789abcdef";
  if(!out || outsz == 0) return;
  out[0] = 0;
  size_t o = 0;
  if(!b) return;
  for(size_t i = 0; i + 5 <= n && i < 256; i++) {
    if(b[i] || b[i + 1] || b[i + 2] || b[i + 4] || b[i + 3] < 0xFC) continue;
    if(o + 9 >= outsz) break;
    if(o) out[o++] = ' ';
    out[o++] = hex[b[i + 3] >> 4];
    out[o++] = hex[b[i + 3] & 15];
    out[o++] = '@';
    size_t v = i;
    char t[8];
    int k = 0;
    do { t[k++] = (char)('0' + v % 10); v /= 10; } while(v && k < 7);
    while(k) out[o++] = t[--k];
    out[o] = 0;
    i += 4;
  }
}

/* Copies the name of the display to out (outsz bytes, always terminated) and returns 1, or returns 0 and leaves out empty. The name descriptor
   (00 00 00 FC 00 + characters) is looked for in the first 512 bytes; the name ends at the line feed, trailing blanks are cut. */
static inline int
ps5tm_tvname_from_block(const unsigned char *b, size_t n, char *out, size_t outsz) {
  if(!out || outsz < 2) return 0;
  out[0] = 0;
  if(!b) return 0;
  size_t max = outsz - 1;
  if(max > 13) max = 13;
  for(size_t i = 0; i + 5 < n && i < 512; i++) {
    if(b[i] || b[i + 1] || b[i + 2] || b[i + 3] != 0xFC || b[i + 4]) continue;
    const unsigned char *d = b + i + 5;
    size_t room = n - (i + 5);
    size_t k = 0;
    while(k < 13 && k < max && k < room && ps5tm_tvname_printable(d[k])) k++;
    while(k && d[k - 1] == ' ') k--;                         /* trailing blanks */
    if(k < PS5TM_TVNAME_MIN) continue;                      /* not a name: look on */
    memcpy(out, d, k);
    out[k] = 0;
    return 1;
  }
  return 0;
}

/* The first `n` bytes of a block as hex for the log ("aa bb cc ..."), terminated; at most (outsz-1)/3 bytes fit. The bytes 4..7 and 12..15, where the
   serial number of the display is (without and with the EDID header), are written as "xx". */
static inline void
ps5tm_tvname_hex(const unsigned char *buf, size_t n, char *out, size_t outsz) {
  static const char hex[] = "0123456789abcdef";
  size_t o = 0;
  if(!out || outsz == 0) return;
  for(size_t i = 0; buf && i < n && o + 3 < outsz; i++) {
    int serial = (i >= 4 && i < 8) || (i >= 12 && i < 16);
    out[o++] = serial ? 'x' : hex[buf[i] >> 4];
    out[o++] = serial ? 'x' : hex[buf[i] & 15];
    out[o++] = ' ';
  }
  if(o) o--;                          /* no blank at the end */
  out[o] = 0;
}

#endif
