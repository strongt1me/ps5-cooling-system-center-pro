/* PS5 Cooling & System Center - Pro: what happens to the ORIGINAL of a game after a conversion, a copy or an unpack (origafter.h, 18.11.2026).
 *
 * Wish of the user (18.11.2026): after a game was converted, copied or unpacked, the person can have the original deleted or moved to a place that
 * ShadowMountPlus does not search, so that the same title is not on the console twice (ShadowMountPlus has trouble with that). The decisions that
 * need no I/O are here so that a host test can try them (test/origafter); the work itself is in origafter.c.
 *
 * Where ShadowMountPlus looks (README of 1.7beta, and gamecopy.c): <drive>/homebrew and <drive>/etaHEN/games, and the root of a USB or M.2 drive
 * down to two levels (scan_depth); on the internal SSD (/data) only those two folders. "PS5-Sicherung/Spiele/<name>" on a drive is three levels
 * deep and therefore never found. */
#ifndef PS5TM_ORIGAFTER_H
#define PS5TM_ORIGAFTER_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#define PS5TM_ORIG_FOLDER "PS5-Sicherung/Spiele"

/* "/a//b/" -> "/a/b". 0 when in is an absolute path without "." or ".." parts and without control characters, else -1 (out is then not to be used). */
static inline int
ps5tm_orig_normalize(const char *in, char *out, size_t n) {
  if(!in || in[0] != '/' || n < 2) return -1;
  size_t o = 0;
  const char *p = in;
  while(*p) {
    while(*p == '/') p++;
    if(!*p) break;
    const char *e = p;
    while(*e && *e != '/') {
      if((unsigned char)*e < 0x20) return -1;
      e++;
    }
    size_t l = (size_t)(e - p);
    if((l == 1 && p[0] == '.') || (l == 2 && p[0] == '.' && p[1] == '.')) return -1;
    if(o + 1 + l + 1 > n) return -1;
    out[o++] = '/';
    memcpy(out + o, p, l);
    o += l;
    p = e;
  }
  if(o == 0) { out[0] = '/'; o = 1; }
  out[o] = '\0';
  return 0;
}

/* 1 when path is dir or lies below it (whole path parts: "/data/home" is not below "/data/homebrew"). Both normalised. Upper and lower case count as
   the same: on an exFAT or FAT drive "HomeBrew" is the folder "homebrew" (the internal SSD tells them apart; refusing too much there costs nothing). */
static inline int
ps5tm_orig_under(const char *path, const char *dir) {
  size_t dl = strlen(dir);
  if(dl == 1 && dir[0] == '/') return 1;
  return !strncasecmp(path, dir, dl) && (path[dl] == '\0' || path[dl] == '/');
}

/* How many path parts path has below mount (0 = the mount itself), -1 when it is not below it. */
static inline int
ps5tm_orig_depth(const char *path, const char *mount) {
  if(!ps5tm_orig_under(path, mount)) return -1;
  const char *p = path + strlen(mount);
  int d = 0;
  while(*p) {
    while(*p == '/') p++;
    if(!*p) break;
    d++;
    while(*p && *p != '/') p++;
  }
  return d;
}

/* 1 when ShadowMountPlus would find a game (a folder, or an image file) that lies at game_path on the drive with this mount point ("/data" for the
   internal SSD, else the drive's own mount point). */
static inline int
ps5tm_orig_smp_finds(const char *game_path, const char *mount) {
  char r1[320], r2[320];
  snprintf(r1, sizeof(r1), "%s/homebrew", mount);
  snprintf(r2, sizeof(r2), "%s/etaHEN/games", mount);
  if(ps5tm_orig_under(game_path, r1) || ps5tm_orig_under(game_path, r2)) return 1;
  if(!strcmp(mount, "/data")) return 0;                 /* the internal SSD has no scan root of its own beyond those two folders */
  int d = ps5tm_orig_depth(game_path, mount);
  return d >= 0 && d <= 2;                              /* the roots of USB and M.2 drives, two levels deep */
}

/* "9,4 GB" / "512 MB" / "0 MB": German decimal comma, as the app shows sizes. */
static inline void
ps5tm_orig_fmt_bytes(char *out, size_t n, uint64_t bytes) {
  if(bytes >= (1ull << 30)) snprintf(out, n, "%.1f GB", (double)bytes / 1073741824.0);
  else                      snprintf(out, n, "%llu MB", (unsigned long long)((bytes + (1u << 19)) >> 20));
  for(char *p = out; *p; p++) if(*p == '.') *p = ',';
}

/* The name of a folder or a file that may stand in a destination: one path part, not empty, no "." and "..", no slash, no control character. */
static inline int
ps5tm_orig_name_ok(const char *s) {
  if(!s || !s[0] || !strcmp(s, ".") || !strcmp(s, "..")) return 0;
  for(const char *p = s; *p; p++)
    if(*p == '/' || (unsigned char)*p < 0x20) return 0;
  return 1;
}

/* The last part of a path. */
static inline const char *
ps5tm_orig_basename(const char *path) {
  const char *s = strrchr(path, '/');
  return s ? s + 1 : path;
}

#endif
