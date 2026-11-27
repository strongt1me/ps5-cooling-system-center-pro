/* Host test of src/krakenmig.h (27.11.2026): FPKG-Games becomes Kraken-Games. The config line is recognised and replaced only when it is the app's own
   list, the rest of the file stays byte for byte; the folders are renamed or merged on real folders of the test, nothing is overwritten. */
#include "krakenmig.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int g_fail, g_n;
#define CHECK(c) do { g_n++; if(!(c)) { g_fail++; printf("FAIL line %d: %s\n", __LINE__, #c); } } while(0)

static char g_root[256];

static void
put(const char *rel, const char *text) {
  char p[512];
  snprintf(p, sizeof p, "%s/%s", g_root, rel);
  FILE *f = fopen(p, "w");
  if(!f) { printf("FAIL: cannot write %s\n", p); g_fail++; return; }
  fputs(text, f);
  fclose(f);
}

static void
dir(const char *rel) {
  char p[512];
  snprintf(p, sizeof p, "%s/%s", g_root, rel);
  mkdir(p, 0777);
}

static int
is_file(const char *rel, const char *text) {
  char p[512], buf[64] = "";
  snprintf(p, sizeof p, "%s/%s", g_root, rel);
  FILE *f = fopen(p, "r");
  if(!f) return 0;
  size_t n = fread(buf, 1, sizeof buf - 1, f);
  fclose(f);
  buf[n] = 0;
  return !text || !strcmp(buf, text);
}

static int
is_dir(const char *rel) {
  char p[512];
  struct stat st;
  snprintf(p, sizeof p, "%s/%s", g_root, rel);
  return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

static int
gone(const char *rel) {
  char p[512];
  struct stat st;
  snprintf(p, sizeof p, "%s/%s", g_root, rel);
  return lstat(p, &st) != 0;
}

static void
base(const char *rel, char *out, size_t n) {
  snprintf(out, n, "%s/%s", g_root, rel);
}

int
main(void) {
  /* 1. the line the app writes */
  char line_new[640], line_old[640];
  CHECK(ps5tm_krmig_scanpath(PS5TM_KRAKEN_GAMES, line_new, sizeof line_new) == 0);
  CHECK(ps5tm_krmig_scanpath(PS5TM_KRAKEN_GAMES_OLD, line_old, sizeof line_old) == 0);
  CHECK(!strncmp(line_new, "scanpath = [\"/data/Kraken-Games\", \"/mnt/usb0/Kraken-Games\"", 57));
  CHECK(strstr(line_new, "\"/mnt/usb7/Kraken-Games\", \"/mnt/ext0/Kraken-Games\", \"/mnt/ext1/Kraken-Games\"]") != NULL);
  CHECK(strstr(line_old, "\"/mnt/ext1/FPKG-Games\"]") != NULL);
  char tiny[20];
  CHECK(ps5tm_krmig_scanpath(PS5TM_KRAKEN_GAMES, tiny, sizeof tiny) != 0);

  /* 2. the config: the app's list under the old name is replaced, everything else stays as it was (line ends too) */
  {
    char text[2048], *out = NULL;
    snprintf(text, sizeof text, "# search folders set by the app (page FPKGMount); ShadowMountPlus does not search them\n%s\nlog = true\n", line_old);
    CHECK(ps5tm_krmig_config(text, strlen(text), &out) == PS5TM_KRMIG_CFG_OLD);
    char want[2048];
    snprintf(want, sizeof want, "# search folders set by the app (page FPKGMount); ShadowMountPlus does not search them\n%s\nlog = true\n", line_new);
    CHECK(out && !strcmp(out, want));
    free(out);

    /* Windows line ends and a space after the list (written by hand or another editor) */
    snprintf(text, sizeof text, "notify = true\r\n%s  \r\nlog = true\r\n", line_old);
    CHECK(ps5tm_krmig_config(text, strlen(text), &out) == PS5TM_KRMIG_CFG_OLD);
    snprintf(want, sizeof want, "notify = true\r\n%s  \r\nlog = true\r\n", line_new);
    CHECK(out && !strcmp(out, want));
    free(out);

    /* the last line without a line end */
    snprintf(text, sizeof text, "log = true\n%s", line_old);
    CHECK(ps5tm_krmig_config(text, strlen(text), &out) == PS5TM_KRMIG_CFG_OLD);
    CHECK(out && strstr(out, line_new) && !strstr(out, "FPKG-Games"));
    free(out);

    /* already the new list */
    snprintf(text, sizeof text, "%s\n", line_new);
    CHECK(ps5tm_krmig_config(text, strlen(text), &out) == PS5TM_KRMIG_CFG_NEW && out == NULL);

    /* a list changed by hand (one folder more) and no list at all: left alone */
    char hand[700];
    snprintf(hand, sizeof hand, "%.*s, \"/data/homebrew\"]\n", (int)strlen(line_old) - 1, line_old);
    CHECK(ps5tm_krmig_config(hand, strlen(hand), &out) == PS5TM_KRMIG_CFG_OTHER && out == NULL);
    CHECK(ps5tm_krmig_config("log = true\n", 11, &out) == PS5TM_KRMIG_CFG_OTHER && out == NULL);
    CHECK(ps5tm_krmig_config("", 0, &out) == PS5TM_KRMIG_CFG_OTHER);
    /* the old list commented out is no list */
    snprintf(text, sizeof text, "# %s\n", line_old);
    CHECK(ps5tm_krmig_config(text, strlen(text), &out) == PS5TM_KRMIG_CFG_OTHER);
  }

  /* 3. the folders, on folders of the test */
  const char *tmp = getenv("TMPDIR");
  snprintf(g_root, sizeof g_root, "%s/krakenmig-XXXXXX", tmp && tmp[0] ? tmp : "/tmp");
  if(!mkdtemp(g_root)) { printf("FAIL: mkdtemp\n"); return 1; }
  char b[512];
  ps5tm_krmig_folder_t r;

  /* a. no old folder: nothing */
  dir("a");
  base("a", b, sizeof b);
  ps5tm_krmig_folder(b, &r);
  CHECK(r.state == PS5TM_KRMIG_NONE && gone("a/Kraken-Games"));

  /* b. the old folder alone: renamed whole, its content with it */
  dir("b"); dir("b/FPKG-Games"); dir("b/FPKG-Games/PPSA01234");
  put("b/FPKG-Games/Spiel.pkg", "pkg");
  put("b/FPKG-Games/PPSA01234/eboot.bin", "eboot");
  base("b", b, sizeof b);
  ps5tm_krmig_folder(b, &r);
  CHECK(r.state == PS5TM_KRMIG_RENAMED);
  CHECK(gone("b/FPKG-Games") && is_file("b/Kraken-Games/Spiel.pkg", "pkg") && is_file("b/Kraken-Games/PPSA01234/eboot.bin", "eboot"));
  /* run again: the old one is gone, nothing more happens */
  ps5tm_krmig_folder(b, &r);
  CHECK(r.state == PS5TM_KRMIG_NONE && is_file("b/Kraken-Games/Spiel.pkg", "pkg"));

  /* c. both there, no name taken: every entry moves, the emptied old folder goes */
  dir("c"); dir("c/FPKG-Games"); dir("c/Kraken-Games"); dir("c/FPKG-Games/Ordner");
  put("c/FPKG-Games/x.exfat", "x");
  put("c/FPKG-Games/Ordner/eboot.bin", "o");
  put("c/Kraken-Games/schon.pkg", "schon");
  base("c", b, sizeof b);
  ps5tm_krmig_folder(b, &r);
  CHECK(r.state == PS5TM_KRMIG_MERGED && r.moved == 2 && r.kept == 0);
  CHECK(gone("c/FPKG-Games") && is_file("c/Kraken-Games/x.exfat", "x") && is_file("c/Kraken-Games/Ordner/eboot.bin", "o") && is_file("c/Kraken-Games/schon.pkg", "schon"));

  /* d. a name taken in Kraken-Games: that entry stays (nothing is overwritten), the others move, the old folder stays */
  dir("d"); dir("d/FPKG-Games"); dir("d/Kraken-Games");
  put("d/FPKG-Games/gleich.pkg", "alt");
  put("d/FPKG-Games/neu.pkg", "neu");
  put("d/Kraken-Games/gleich.pkg", "drin");
  base("d", b, sizeof b);
  ps5tm_krmig_folder(b, &r);
  CHECK(r.state == PS5TM_KRMIG_PARTLY && r.moved == 1 && r.kept == 1 && !strcmp(r.first_kept, "gleich.pkg"));
  CHECK(is_file("d/Kraken-Games/gleich.pkg", "drin") && is_file("d/FPKG-Games/gleich.pkg", "alt") && is_file("d/Kraken-Games/neu.pkg", "neu"));
  CHECK(is_dir("d/FPKG-Games") && gone("d/FPKG-Games/neu.pkg"));

  /* e. Kraken-Games is a file: nothing moves */
  dir("e"); dir("e/FPKG-Games");
  put("e/FPKG-Games/s.pkg", "s");
  put("e/Kraken-Games", "eine Datei");
  base("e", b, sizeof b);
  ps5tm_krmig_folder(b, &r);
  CHECK(r.state == PS5TM_KRMIG_NOT_DIR && is_file("e/FPKG-Games/s.pkg", "s") && is_file("e/Kraken-Games", "eine Datei"));

  /* f. FPKG-Games is a file, not a folder: left alone */
  dir("f");
  put("f/FPKG-Games", "datei");
  base("f", b, sizeof b);
  ps5tm_krmig_folder(b, &r);
  CHECK(r.state == PS5TM_KRMIG_NONE && is_file("f/FPKG-Games", "datei") && gone("f/Kraken-Games"));

  char cmd[400];
  snprintf(cmd, sizeof cmd, "rm -rf '%s'", g_root);
  if(system(cmd) != 0) printf("note: %s stays\n", g_root);
  printf("krakenmig: %d checks, %d failed\n", g_n, g_fail);
  return g_fail ? 1 : 0;
}
