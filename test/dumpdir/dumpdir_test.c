/* Host test of src/dumpdir.h: the folder name of the dump settings (16.11.2026). */
#include "dumpdir.h"

#include <stdio.h>

static int g_fail;

static void
tidy_is(const char *in, const char *mount, const char *want) {
  char b[256];
  snprintf(b, sizeof(b), "%s", in);
  ps5tm_dumpdir_tidy(b, mount);
  if(strcmp(b, want)) { printf("FAIL tidy(\"%s\", \"%s\") = \"%s\", wanted \"%s\"\n", in, mount, b, want); g_fail++; }
}

static void
ok_is(const char *s, int want) {
  int got = ps5tm_dumpdir_ok(s);
  if(got != want) { printf("FAIL ok(\"%s\") = %d, wanted %d\n", s, got, want); g_fail++; }
}

int
main(void) {
  /* what was refused until 16.11.2026 and is now tidied or accepted */
  tidy_is("Dumps/", "/mnt/usb0", "Dumps");
  tidy_is("/Dumps", "/mnt/usb0", "Dumps");
  tidy_is("  Dumps  ", "/mnt/usb0", "Dumps");
  tidy_is("Spiele / PS5 /", "/mnt/usb0", "Spiele/PS5");
  tidy_is("Spiele\\PS5", "/mnt/usb0", "Spiele/PS5");
  tidy_is("a//b///c", "/mnt/usb0", "a/b/c");
  tidy_is("/mnt/usb0/Dumps", "/mnt/usb0", "Dumps");
  tidy_is("/mnt/usb0", "/mnt/usb0", "");
  tidy_is("/data/Dumps", "/user", "Dumps");
  tidy_is("/user/Dumps", "/user", "Dumps");
  tidy_is("/mnt/usb01/Dumps", "/mnt/usb0", "mnt/usb01/Dumps");     /* a longer name is not the drive's path */
  tidy_is("homebrew/ps5-app-dumper/dumps", "/mnt/ext1", "homebrew/ps5-app-dumper/dumps");
  tidy_is("Dumps", "", "Dumps");

  ok_is("homebrew/ps5-app-dumper/dumps", 1);
  ok_is("Dumps", 1);
  ok_is("Meine Dumps", 1);
  ok_is("Spiele für PS5", 1);
  ok_is("Dumps (PS5)", 1);
  ok_is("PS5 & PS4 + Mehr", 1);
  ok_is("Игры", 1);
  ok_is(".hidden", 1);
  ok_is("a/b/c", 1);
  ok_is("a/b/c/d", 0);                 /* four levels */
  ok_is("", 0);
  ok_is("/Dumps", 0);                  /* not tidied */
  ok_is("Dumps/", 0);
  ok_is("a//b", 0);
  ok_is(".", 0);
  ok_is("..", 0);
  ok_is("a/../b", 0);
  ok_is("a/./b", 0);
  ok_is("...", 0);
  ok_is("Dumps.", 0);                  /* exFAT drops a trailing dot */
  ok_is("Dumps /x", 0);
  ok_is("a:b", 0);
  ok_is("a*b", 0);
  ok_is("a?b", 0);
  ok_is("a\"b", 0);
  ok_is("a<b", 0);
  ok_is("a>b", 0);
  ok_is("a|b", 0);
  ok_is("a\\b", 0);
  ok_is("a\tb", 0);

  char longname[200];
  memset(longname, 'x', 95);
  longname[95] = 0;
  ok_is(longname, 1);
  longname[95] = 'x';
  longname[96] = 0;
  ok_is(longname, 0);

  /* a tidied name is always stable */
  char b[256] = "  /mnt/usb0/ Spiele /PS5 / ";
  ps5tm_dumpdir_tidy(b, "/mnt/usb0");
  char c[256];
  snprintf(c, sizeof(c), "%s", b);
  ps5tm_dumpdir_tidy(c, "/mnt/usb0");
  if(strcmp(b, c) || strcmp(b, "Spiele/PS5")) { printf("FAIL stable: \"%s\" / \"%s\"\n", b, c); g_fail++; }

  if(g_fail) { printf("%d failed\n", g_fail); return 1; }
  printf("dumpdir: all checks passed\n");
  return 0;
}
