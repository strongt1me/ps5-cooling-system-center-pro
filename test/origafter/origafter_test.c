/* Host test of src/origafter.h: the decisions of "Original danach" that need no I/O (18.11.2026). */
#include <stdio.h>

#include "origafter.h"

static int g_fail, g_n;

#define CHECK(c) do { g_n++; if(!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); g_fail++; } } while(0)

static int smp(const char *p, const char *m) { return ps5tm_orig_smp_finds(p, m); }

int
main(void) {
  char out[64];

  /* normalising */
  CHECK(ps5tm_orig_normalize("/mnt/usb0//PS5-Sicherung/Spiele/", out, sizeof(out)) == 0 && !strcmp(out, "/mnt/usb0/PS5-Sicherung/Spiele"));
  CHECK(ps5tm_orig_normalize("/data", out, sizeof(out)) == 0 && !strcmp(out, "/data"));
  CHECK(ps5tm_orig_normalize("/", out, sizeof(out)) == 0 && !strcmp(out, "/"));
  CHECK(ps5tm_orig_normalize("///", out, sizeof(out)) == 0 && !strcmp(out, "/"));
  CHECK(ps5tm_orig_normalize("mnt/usb0", out, sizeof(out)) != 0);                  /* not absolute */
  CHECK(ps5tm_orig_normalize("", out, sizeof(out)) != 0);
  CHECK(ps5tm_orig_normalize(NULL, out, sizeof(out)) != 0);
  CHECK(ps5tm_orig_normalize("/mnt/usb0/../data", out, sizeof(out)) != 0);          /* no ".." */
  CHECK(ps5tm_orig_normalize("/mnt/./usb0", out, sizeof(out)) != 0);                /* no "." */
  CHECK(ps5tm_orig_normalize("/mnt/usb0/a\nb", out, sizeof(out)) != 0);             /* no control character */
  CHECK(ps5tm_orig_normalize("/mnt/..hidden/x", out, sizeof(out)) == 0 && !strcmp(out, "/mnt/..hidden/x"));   /* a name that only starts with dots is fine */
  CHECK(ps5tm_orig_normalize("/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", out, sizeof(out)) != 0);   /* too long for the buffer */

  /* under */
  CHECK(ps5tm_orig_under("/data/homebrew/PPSA1", "/data/homebrew"));
  CHECK(ps5tm_orig_under("/data/homebrew", "/data/homebrew"));
  CHECK(!ps5tm_orig_under("/data/homebrewer", "/data/homebrew"));                   /* whole parts only */
  CHECK(!ps5tm_orig_under("/data", "/data/homebrew"));
  CHECK(ps5tm_orig_under("/anything", "/"));

  /* depth */
  CHECK(ps5tm_orig_depth("/mnt/usb0", "/mnt/usb0") == 0);
  CHECK(ps5tm_orig_depth("/mnt/usb0/Game", "/mnt/usb0") == 1);
  CHECK(ps5tm_orig_depth("/mnt/usb0/PS5-Sicherung/Spiele/Game", "/mnt/usb0") == 3);
  CHECK(ps5tm_orig_depth("/mnt/usb1/Game", "/mnt/usb0") == -1);

  /* where ShadowMountPlus looks: the user's folder PS5-Sicherung/Spiele is safe on every drive, the scan roots are not */
  CHECK(!smp("/mnt/usb0/PS5-Sicherung/Spiele/PPSA23000", "/mnt/usb0"));
  CHECK(!smp("/mnt/ext1/PS5-Sicherung/Spiele/PPSA23000", "/mnt/ext1"));
  CHECK(!smp("/data/PS5-Sicherung/Spiele/PPSA23000", "/data"));
  CHECK(smp("/mnt/usb0/homebrew/PPSA23000", "/mnt/usb0"));                          /* its scan root */
  CHECK(smp("/mnt/usb0/homebrew/sub/deeper/PPSA23000", "/mnt/usb0"));
  CHECK(smp("/mnt/usb0/etaHEN/games/PPSA23000", "/mnt/usb0"));
  CHECK(smp("/data/homebrew/PPSA23000", "/data"));
  CHECK(smp("/data/etaHEN/games/PPSA23000", "/data"));
  CHECK(!smp("/data/Spiele/PPSA23000", "/data"));                                   /* /data is no scan root */
  CHECK(!smp("/data/Spiele/Mehr/Noch/PPSA23000", "/data"));
  CHECK(smp("/mnt/usb0/PPSA23000", "/mnt/usb0"));                                   /* the root of a drive, one level */
  CHECK(smp("/mnt/usb0/Spiele/PPSA23000", "/mnt/usb0"));                            /* two levels */
  CHECK(!smp("/mnt/usb0/Spiele/Mehr/PPSA23000", "/mnt/usb0"));                      /* three levels: not found */
  CHECK(!smp("/mnt/usb0/Backup/2026/PPSA23000.ffpkg/x", "/mnt/usb0"));
  CHECK(smp("/mnt/usb0/PPSA23000.exfat", "/mnt/usb0"));                             /* an image in the root */
  CHECK(!smp("/mnt/usb0/homebrewer/a/b/c", "/mnt/usb0"));                           /* "homebrewer" is not "homebrew" */

  /* upper and lower case are the same (exFAT, FAT) */
  CHECK(smp("/mnt/usb0/HomeBrew/PPSA23000", "/mnt/usb0"));
  CHECK(smp("/mnt/usb0/ETAHEN/GAMES/PPSA23000", "/mnt/usb0"));
  CHECK(ps5tm_orig_under("/MNT/USB0/X", "/mnt/usb0"));
  CHECK(!ps5tm_orig_under("/mnt/usb0x/X", "/mnt/usb0"));

  /* sizes */
  ps5tm_orig_fmt_bytes(out, sizeof(out), 9400000000ull); CHECK(!strcmp(out, "8,8 GB"));
  ps5tm_orig_fmt_bytes(out, sizeof(out), 1073741824ull); CHECK(!strcmp(out, "1,0 GB"));
  ps5tm_orig_fmt_bytes(out, sizeof(out), 512ull << 20);  CHECK(!strcmp(out, "512 MB"));
  ps5tm_orig_fmt_bytes(out, sizeof(out), 0);             CHECK(!strcmp(out, "0 MB"));

  /* names */
  CHECK(ps5tm_orig_name_ok("PPSA23000"));
  CHECK(ps5tm_orig_name_ok("Game v1.02.exfat"));
  CHECK(!ps5tm_orig_name_ok(""));
  CHECK(!ps5tm_orig_name_ok("."));
  CHECK(!ps5tm_orig_name_ok(".."));
  CHECK(!ps5tm_orig_name_ok("a/b"));
  CHECK(!ps5tm_orig_name_ok("a\tb"));
  CHECK(!ps5tm_orig_name_ok(NULL));
  CHECK(!strcmp(ps5tm_orig_basename("/data/homebrew/PPSA23000"), "PPSA23000"));
  CHECK(!strcmp(ps5tm_orig_basename("PPSA23000"), "PPSA23000"));

  /* FAT32 takes no file of 4 GiB or more (the file system's own name is msdosfs) */
  CHECK(ps5tm_orig_fat32_refuses("msdosfs", 4ull << 30));
  CHECK(ps5tm_orig_fat32_refuses("msdosfs", 0xFFFFFFFFull + 1));
  CHECK(!ps5tm_orig_fat32_refuses("msdosfs", 0xFFFFFFFFull));
  CHECK(!ps5tm_orig_fat32_refuses("msdosfs", 1ull << 20));
  CHECK(!ps5tm_orig_fat32_refuses("exfatfs", 8ull << 30));
  CHECK(!ps5tm_orig_fat32_refuses("", 8ull << 30));
  CHECK(!ps5tm_orig_fat32_refuses(NULL, 8ull << 30));

  printf("%d Prüfungen, %d Fehler\n", g_n, g_fail);
  return g_fail ? 1 : 0;
}
