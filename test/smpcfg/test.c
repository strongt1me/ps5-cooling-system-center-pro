/* Host test of src/smpcfg.c: reading and changing update_emulators in ShadowMountPlus's config.ini. */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "smpcfg.h"

static int g_fail, g_n;

#define CHECK(c) do { g_n++; if(!(c)) { g_fail++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while(0)

static char *
edit(const char *t, int on) {
  size_t n = 0;
  char *r = ps5tm_smpcfg_with_update(t, strlen(t), on, PS5TM_SMP_AUTO, &n);
  CHECK(r && n == strlen(r));
  return r;
}

static int
state(const char *t) {
  ps5tm_smpcfg_t c;
  ps5tm_smpcfg_parse(t, strlen(t), &c);
  return c.update_emulators;
}

static char *
slurp(const char *p, size_t *n) {
  FILE *f = fopen(p, "rb");
  if(!f) return NULL;
  char *b = malloc(1 << 16);
  *n = fread(b, 1, (1 << 16) - 1, f);
  b[*n] = 0;
  fclose(f);
  return b;
}

/* a flat file as the console has it: the template as comments, the person's lines at the end */
static const char FLAT_LIVE[] =
  "# ShadowMountPlus runtime config\n"
  "# update_emulators=1\n"
  "# emulators_path=/data/shadowmount/emus\n"
  "scanpath=/mnt/usb0\n"
  "\n"
  "update_emulators=0\n"
  "auto_update_ampr=0\n";

int
main(int argc, char **argv) {
  const char *work = argc > 1 ? argv[1] : "/tmp/smpcfg-work";
  char *r;

  /* ---- reading */
  CHECK(state("") == 1);                                   /* the default of ShadowMountPlus is on */
  CHECK(state("# update_emulators=0\n") == 1);             /* a comment says nothing */
  CHECK(state(FLAT_LIVE) == 0);
  CHECK(state("update_emulators = off\n") == 0);
  CHECK(state("update_emulators=No\n") == 0);
  CHECK(state("update_emulators=false\n") == 0);
  CHECK(state("update_emulators=yes\n") == 1);
  CHECK(state("  update_emulators=on  \n") == 1);
  CHECK(state("[emulators]\nupdate_enabled = off\n") == 0);
  CHECK(state("[Emulators]\nupdate_enabled=1\n") == 1);
  CHECK(state("[other]\nupdate_enabled=0\n") == 1);        /* only inside [emulators] */
  {
    ps5tm_smpcfg_t c;
    const char *t = "[config]\nversion=2\n[emulators]\nupdate_enabled=1\npath = /data/x/emus\n";
    ps5tm_smpcfg_parse(t, strlen(t), &c);
    CHECK(c.v2 && !strcmp(c.emulators_path, "/data/x/emus"));
    t = "emulators_path=/data/y\n";
    ps5tm_smpcfg_parse(t, strlen(t), &c);
    CHECK(!c.v2 && !strcmp(c.emulators_path, "/data/y"));
    t = "emulators_path=relative\n";
    ps5tm_smpcfg_parse(t, strlen(t), &c);
    CHECK(c.emulators_path[0] == 0);                       /* only an absolute path counts */
  }

  /* ---- a flat file: the active line changes, the rest stays byte for byte */
  r = edit(FLAT_LIVE, 0);
  CHECK(!strcmp(r, FLAT_LIVE));                            /* already off: the same text */
  free(r);
  r = edit(FLAT_LIVE, 1);
  CHECK(state(r) == 1);
  CHECK(strstr(r, "update_emulators=1\nauto_update_ampr=0\n") != NULL);
  CHECK(strstr(r, "# update_emulators=1\n# emulators_path=/data/shadowmount/emus\nscanpath=/mnt/usb0\n\n") != NULL);
  {
    char *back = edit(r, 0);
    CHECK(!strcmp(back, FLAT_LIVE));                       /* there and back again */
    free(back);
  }
  free(r);

  /* ---- on by an active line */
  {
    const char *t = "scanpath=/mnt/usb0\nupdate_emulators=1\nsmb_password=geheim\n";
    r = edit(t, 0);
    CHECK(!strcmp(r, "scanpath=/mnt/usb0\nupdate_emulators=0\nsmb_password=geheim\n"));
    free(r);
    t = "  Update_Emulators = true\n";
    r = edit(t, 0);
    CHECK(!strcmp(r, "  Update_Emulators = 0\n"));        /* indentation, spelling of the key and the blank after = stay */
    CHECK(state(r) == 0);
    free(r);
    t = "update_emulators=1\nupdate_emulators=1\n";        /* two lines: both */
    r = edit(t, 0);
    CHECK(!strcmp(r, "update_emulators=0\nupdate_emulators=0\n"));
    free(r);
  }

  /* ---- no line at all: added at the end, the line ending of the file kept */
  r = edit("# only comments\nscanpath=/mnt/usb0", 0);
  CHECK(!strcmp(r, "# only comments\nscanpath=/mnt/usb0\nupdate_emulators=0\n"));
  free(r);
  r = edit("scanpath=/mnt/usb0\r\n# x\r\n", 0);
  CHECK(!strcmp(r, "scanpath=/mnt/usb0\r\n# x\r\nupdate_emulators=0\r\n"));
  free(r);
  r = edit("", 0);
  CHECK(!strcmp(r, "update_emulators=0\n"));
  free(r);
  r = edit("scanpath=/mnt/usb0\n", 1);                     /* on is the default: nothing to add */
  CHECK(!strcmp(r, "scanpath=/mnt/usb0\n"));
  free(r);
  r = edit("update_emulators=0\r\nx=1\r\n", 1);
  CHECK(!strcmp(r, "update_emulators=1\r\nx=1\r\n"));      /* CRLF stays */
  free(r);

  /* ---- the sectioned format (v2) */
  {
    const char *t =
      "[config]\nversion=2\n\n[general]\ndebug=on\n\n[emulators]\n# update_enabled = on\n# path = /data/shadowmount/emus\n"
      "auto_update_ampr=0\n\n[smb:NAS]\nhost=10.0.0.2\npassword=geheim\n";
    r = edit(t, 0);
    CHECK(state(r) == 0);
    CHECK(strstr(r, "[emulators]\nupdate_enabled=0\n# update_enabled = on\n") != NULL);
    CHECK(strstr(r, "[smb:NAS]\nhost=10.0.0.2\npassword=geheim\n") != NULL);
    CHECK(strstr(r, "update_emulators") == NULL);          /* a v2 file does not get the flat key */
    CHECK(strlen(r) == strlen(t) + strlen("update_enabled=0\n"));
    free(r);
    t = "[config]\nversion=2\n[emulators]\nupdate_enabled = on\npath=/p\n";
    r = edit(t, 0);
    CHECK(!strcmp(r, "[config]\nversion=2\n[emulators]\nupdate_enabled = 0\npath=/p\n"));
    free(r);
    t = "[config]\nversion=2\n[general]\ndebug=on";
    r = edit(t, 0);
    CHECK(!strcmp(r, "[config]\nversion=2\n[general]\ndebug=on\n\n[emulators]\nupdate_enabled=0\n"));
    CHECK(state(r) == 0);
    free(r);
  }

  /* ---- v2 as ShadowMountPlus 1.7beta12 reads it: comments behind a value, quotes, a header with a comment, only the section keys */
  {
    ps5tm_smpcfg_t c;
    const char *t;
    CHECK(state("[config]\nversion=2\n[emulators]\nupdate_enabled = off  # Test\n") == 0);
    CHECK(state("[config]\nversion=2\n[emulators]\nupdate_enabled = off ; Test\n") == 0);
    CHECK(state("[config]\nversion=2\n[emulators]\nupdate_enabled = on # off\n") == 1);
    CHECK(state("[config]\nversion=2\n[emulators]\nupdate_enabled = \"off\"\n") == 0);
    CHECK(state("[config]\nversion=2\n[emulators]\nupdate_enabled = \"on\" # off\n") == 1);
    CHECK(state("[config]\nversion=2\n[emulators] # the emulators\nupdate_enabled=0\n") == 0);   /* a comment behind the header */
    CHECK(state("[config] ; v2\nversion=2\n[Emulators]  \nupdate_enabled=0\n") == 0);
    CHECK(state("[ emulators ]\nupdate_enabled=0\n") == 0);                                    /* blanks inside the brackets */
    t = "[config]\nversion=2\n[emulators]\nupdate_enabled=1\npath = /data/x/emus   # my folder\n";
    ps5tm_smpcfg_parse(t, strlen(t), &c);
    CHECK(!strcmp(c.emulators_path, "/data/x/emus"));
    t = "[config]\nversion=2\n[emulators]\npath = \"/data/my emus # 1\" ; note\n";
    ps5tm_smpcfg_parse(t, strlen(t), &c);
    CHECK(!strcmp(c.emulators_path, "/data/my emus # 1"));
    t = "[config]\nversion=2\n[emulators]\npath = \"/data/a\\\\b\\\"c\"\n";
    ps5tm_smpcfg_parse(t, strlen(t), &c);
    CHECK(!strcmp(c.emulators_path, "/data/a\\b\"c"));                                         /* \\ and \" */
    /* in a v2 file the old flat names mean nothing, in a flat file the section keys still do */
    CHECK(state("[config]\nversion=2\nupdate_emulators=0\n") == 1);
    CHECK(state("[config]\nversion=2\n[emulators]\nupdate_emulators=0\n") == 1);
    t = "[config]\nversion=2\nemulators_path=/data/old\n";
    ps5tm_smpcfg_parse(t, strlen(t), &c);
    CHECK(c.emulators_path[0] == 0);
    CHECK(state("update_emulators=0\n") == 0);
    /* a comment behind the value, blanks and quotes stay when the value changes */
    r = edit("[config]\nversion=2\n[emulators]\nupdate_enabled = on  # Standard\n", 0);
    CHECK(!strcmp(r, "[config]\nversion=2\n[emulators]\nupdate_enabled = 0  # Standard\n"));
    CHECK(state(r) == 0);
    free(r);
    r = edit("[config]\nversion=2\r\n[emulators]\r\nupdate_enabled = \"on\" ; x\r\n", 0);
    CHECK(!strcmp(r, "[config]\nversion=2\r\n[emulators]\r\nupdate_enabled = 0 ; x\r\n"));
    free(r);
    r = edit("[config]\nversion=2\n[emulators]\nupdate_enabled = # note\n", 0);
    CHECK(!strcmp(r, "[config]\nversion=2\n[emulators]\nupdate_enabled = 0 # note\n"));
    CHECK(state(r) == 0);
    free(r);
    r = edit("update_emulators=1  \n", 0);
    CHECK(!strcmp(r, "update_emulators=0  \n"));                                               /* blanks behind the value stay */
    free(r);
    r = edit("update_emulators=1 # Standard\n", 0);
    CHECK(!strcmp(r, "update_emulators=0 # Standard\n"));
    free(r);
    /* a stray old name in a v2 file is left alone, the right line is added under the header (which has a comment) */
    r = edit("[config]\nversion=2\n[general]\nupdate_emulators=0\n[emulators] # emus\nauto_update_ampr=0\n", 0);
    CHECK(!strcmp(r, "[config]\nversion=2\n[general]\nupdate_emulators=0\n[emulators] # emus\nupdate_enabled=0\nauto_update_ampr=0\n"));
    CHECK(state(r) == 0);
    free(r);
    /* [emulators] is the last line and has no line ending: the new line starts on a line of its own (11.2026: it was glued to the header) */
    r = edit("[config]\nversion=2\n[emulators]", 0);
    CHECK(!strcmp(r, "[config]\nversion=2\n[emulators]\nupdate_enabled=0\n"));
    CHECK(state(r) == 0);
    free(r);
    r = edit("[config]\r\nversion=2\r\n[emulators]", 0);
    CHECK(!strcmp(r, "[config]\r\nversion=2\r\n[emulators]\r\nupdate_enabled=0\r\n"));
    free(r);
  }

  /* ---- the file */
  {
    char cmd[512], p[400], bak[420], tmp[420], why[300];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s' && mkdir -p '%s'", work, work);
    CHECK(system(cmd) == 0);
    snprintf(p, sizeof(p), "%s/config.ini", work);
    snprintf(bak, sizeof(bak), "%s.ps5cc-bak", p);
    snprintf(tmp, sizeof(tmp), "%s.ps5cc-tmp", p);

    /* no file, folder there: made */
    CHECK(ps5tm_smpcfg_set_update(p, 0, PS5TM_SMP_AUTO, why, sizeof(why)) == 0);
    size_t n = 0;
    char *b = slurp(p, &n);
    CHECK(b && !strcmp(b, "update_emulators=0\n"));
    free(b);
    CHECK(access(bak, F_OK) != 0);                          /* nothing to keep */

    /* an existing file: changed, the old one kept once, the mode kept */
    FILE *f = fopen(p, "wb");
    fputs("scanpath=/mnt/usb0\nupdate_emulators=1\nsmb_password=geheim\n", f);
    fclose(f);
    chmod(p, 0600);
    CHECK(ps5tm_smpcfg_set_update(p, 0, PS5TM_SMP_AUTO, why, sizeof(why)) == 0);
    b = slurp(p, &n);
    CHECK(b && !strcmp(b, "scanpath=/mnt/usb0\nupdate_emulators=0\nsmb_password=geheim\n"));
    free(b);
    b = slurp(bak, &n);
    CHECK(b && !strcmp(b, "scanpath=/mnt/usb0\nupdate_emulators=1\nsmb_password=geheim\n"));
    free(b);
    struct stat st;
    CHECK(stat(p, &st) == 0 && (st.st_mode & 0777) == 0600);
    CHECK(stat(bak, &st) == 0 && (st.st_mode & 0777) == 0600);
    CHECK(access(tmp, F_OK) != 0);                          /* no temporary file left */

    /* already as wanted: the file is not touched, the copy stays the first one */
    CHECK(ps5tm_smpcfg_set_update(p, 0, PS5TM_SMP_AUTO, why, sizeof(why)) == 0);
    b = slurp(bak, &n);
    CHECK(b && strstr(b, "update_emulators=1") != NULL);
    free(b);

    /* and back on */
    CHECK(ps5tm_smpcfg_set_update(p, 1, PS5TM_SMP_AUTO, why, sizeof(why)) == 0);
    b = slurp(p, &n);
    CHECK(b && strstr(b, "update_emulators=1") != NULL);
    free(b);

    /* links put where the copy and the temporary file go are never written through; a stale temporary file is replaced */
    {
      char p2[400], l1[420], l2[420], away1[400], away2[400];
      snprintf(p2, sizeof(p2), "%s/c2.ini", work);
      snprintf(l1, sizeof(l1), "%s.ps5cc-bak", p2);
      snprintf(l2, sizeof(l2), "%s.ps5cc-tmp", p2);
      snprintf(away1, sizeof(away1), "%s/away1", work);
      snprintf(away2, sizeof(away2), "%s/away2", work);
      f = fopen(p2, "wb");
      fputs("update_emulators=1\n", f);
      fclose(f);
      CHECK(symlink(away1, l1) == 0 && symlink(away2, l2) == 0);          /* both point at nothing */
      CHECK(ps5tm_smpcfg_set_update(p2, 0, PS5TM_SMP_AUTO, why, sizeof(why)) == 0);
      b = slurp(p2, &n);
      CHECK(b && !strcmp(b, "update_emulators=0\n"));
      free(b);
      CHECK(access(away1, F_OK) != 0 && access(away2, F_OK) != 0);        /* nothing written through the links */
      CHECK(access(l2, F_OK) != 0);                                       /* the temporary name is free again */
      unlink(l1);
      f = fopen(l2, "wb");                                                /* a leftover of an earlier run */
      fputs("garbage", f);
      fclose(f);
      CHECK(ps5tm_smpcfg_set_update(p2, 1, PS5TM_SMP_AUTO, why, sizeof(why)) == 0);
      b = slurp(p2, &n);
      CHECK(b && !strcmp(b, "update_emulators=1\n"));
      free(b);
      CHECK(access(l2, F_OK) != 0);
    }

    /* not a plain file (a folder stands there): refused with a reason, nothing replaced */
    {
      char pd[400];
      snprintf(pd, sizeof(pd), "%s/ordner.ini", work);
      CHECK(mkdir(pd, 0755) == 0);
      CHECK(ps5tm_smpcfg_set_update(pd, 0, PS5TM_SMP_AUTO, why, sizeof(why)) == EINVAL);
      CHECK(strstr(why, "keine gewöhnliche Datei") != NULL);
      struct stat sd;
      CHECK(stat(pd, &sd) == 0 && S_ISDIR(sd.st_mode));
    }

    /* no folder: a reason, nothing made */
    snprintf(p, sizeof(p), "%s/nicht-da/config.ini", work);
    CHECK(ps5tm_smpcfg_set_update(p, 0, PS5TM_SMP_AUTO, why, sizeof(why)) == ENOENT);
    CHECK(strstr(why, "nicht eingerichtet") != NULL);
  }

  /* ---- the version the person named (smp_version, 09.11.2026) */
  {
    CHECK(ps5tm_smpcfg_format("", 0) == PS5TM_SMPFMT_NONE);
    CHECK(ps5tm_smpcfg_format("# only a comment\n\n", 17) == PS5TM_SMPFMT_NONE);
    CHECK(ps5tm_smpcfg_format("scanpath=/mnt/usb0\n", 19) == PS5TM_SMPFMT_FLAT);
    CHECK(ps5tm_smpcfg_format("[config]\nversion=2\n", 19) == PS5TM_SMPFMT_V2);
    CHECK(ps5tm_smpcfg_format("a=1\n[Emulators] # x\n", 20) == PS5TM_SMPFMT_V2);
    CHECK(ps5tm_smpcfg_mode_of("old") == PS5TM_SMP_OLD);
    CHECK(ps5tm_smpcfg_mode_of("v2") == PS5TM_SMP_V2);
    CHECK(ps5tm_smpcfg_mode_of("auto") == PS5TM_SMP_AUTO);
    CHECK(ps5tm_smpcfg_mode_of("") == PS5TM_SMP_AUTO);
    CHECK(ps5tm_smpcfg_mode_of("irgendwas") == PS5TM_SMP_AUTO);
    CHECK(ps5tm_smpcfg_mode_of(NULL) == PS5TM_SMP_AUTO);

    /* the name of the line: the file says, a file that says nothing follows the named version */
    CHECK(!strcmp(ps5tm_smpcfg_key(PS5TM_SMPFMT_FLAT, PS5TM_SMP_V2), "update_emulators"));
    CHECK(!strcmp(ps5tm_smpcfg_key(PS5TM_SMPFMT_V2, PS5TM_SMP_OLD), "update_enabled"));
    CHECK(!strcmp(ps5tm_smpcfg_key(PS5TM_SMPFMT_V2, PS5TM_SMP_AUTO), "update_enabled"));
    CHECK(!strcmp(ps5tm_smpcfg_key(PS5TM_SMPFMT_NONE, PS5TM_SMP_V2), "update_enabled"));
    CHECK(!strcmp(ps5tm_smpcfg_key(PS5TM_SMPFMT_NONE, PS5TM_SMP_OLD), "update_emulators"));
    CHECK(!strcmp(ps5tm_smpcfg_key(PS5TM_SMPFMT_NONE, PS5TM_SMP_AUTO), "update_emulators"));

    /* a hint only when the named version and the file do not fit */
    CHECK(strstr(ps5tm_smpcfg_mismatch(PS5TM_SMPFMT_FLAT, PS5TM_SMP_V2), "update_emulators") != NULL);
    CHECK(strstr(ps5tm_smpcfg_mismatch(PS5TM_SMPFMT_V2, PS5TM_SMP_OLD), "update_enabled") != NULL);
    CHECK(ps5tm_smpcfg_mismatch(PS5TM_SMPFMT_FLAT, PS5TM_SMP_OLD)[0] == 0);
    CHECK(ps5tm_smpcfg_mismatch(PS5TM_SMPFMT_V2, PS5TM_SMP_V2)[0] == 0);
    CHECK(ps5tm_smpcfg_mismatch(PS5TM_SMPFMT_FLAT, PS5TM_SMP_AUTO)[0] == 0);
    CHECK(ps5tm_smpcfg_mismatch(PS5TM_SMPFMT_V2, PS5TM_SMP_AUTO)[0] == 0);
    CHECK(ps5tm_smpcfg_mismatch(PS5TM_SMPFMT_NONE, PS5TM_SMP_V2)[0] == 0);
    CHECK(ps5tm_smpcfg_mismatch(PS5TM_SMPFMT_NONE, PS5TM_SMP_OLD)[0] == 0);

    /* a file that says nothing gets the line of the named version */
    size_t n = 0;
    char *r = ps5tm_smpcfg_with_update("", 0, 0, PS5TM_SMP_V2, &n);
    CHECK(r && !strcmp(r, "[config]\nversion=2\n\n[emulators]\nupdate_enabled=0\n"));
    CHECK(r && state(r) == 0 && ps5tm_smpcfg_format(r, strlen(r)) == PS5TM_SMPFMT_V2);
    free(r);
    r = ps5tm_smpcfg_with_update("# nur ein Kommentar\n", 20, 0, PS5TM_SMP_V2, &n);
    CHECK(r && !strcmp(r, "# nur ein Kommentar\n[config]\nversion=2\n\n[emulators]\nupdate_enabled=0\n"));
    free(r);
    r = ps5tm_smpcfg_with_update("# nur ein Kommentar", 19, 0, PS5TM_SMP_V2, &n);          /* no line ending at the end */
    CHECK(r && !strcmp(r, "# nur ein Kommentar\n[config]\nversion=2\n\n[emulators]\nupdate_enabled=0\n"));
    free(r);
    r = ps5tm_smpcfg_with_update("", 0, 0, PS5TM_SMP_OLD, &n);
    CHECK(r && !strcmp(r, "update_emulators=0\n"));
    free(r);
    r = ps5tm_smpcfg_with_update("", 0, 0, PS5TM_SMP_AUTO, &n);
    CHECK(r && !strcmp(r, "update_emulators=0\n"));
    free(r);
    r = ps5tm_smpcfg_with_update("", 0, 1, PS5TM_SMP_V2, &n);                              /* on is the default: nothing to write */
    CHECK(r && r[0] == 0 && n == 0);
    free(r);

    /* a file that says which format it is in is written in that format, whatever was named */
    r = ps5tm_smpcfg_with_update("scanpath=/mnt/usb0\n", 19, 0, PS5TM_SMP_V2, &n);
    CHECK(r && !strcmp(r, "scanpath=/mnt/usb0\nupdate_emulators=0\n"));
    free(r);
    r = ps5tm_smpcfg_with_update("[config]\nversion=2\n[emulators]\nupdate_enabled=1\n", 48, 0, PS5TM_SMP_OLD, &n);
    CHECK(r && !strcmp(r, "[config]\nversion=2\n[emulators]\nupdate_enabled=0\n"));
    free(r);
  }

  {
    /* the file: a missing one is made in the format of the named version */
    char cmd[512], p[400], why[300];
    size_t n = 0;
    snprintf(cmd, sizeof(cmd), "rm -rf '%s' && mkdir -p '%s'", work, work);
    CHECK(system(cmd) == 0);
    snprintf(p, sizeof(p), "%s/config.ini", work);
    CHECK(ps5tm_smpcfg_set_update(p, 0, PS5TM_SMP_V2, why, sizeof(why)) == 0);
    char *b = slurp(p, &n);
    CHECK(b && !strcmp(b, "[config]\nversion=2\n\n[emulators]\nupdate_enabled=0\n"));
    free(b);
  }

  printf("%d checks, %d failed\n", g_n, g_fail);
  return g_fail ? 1 : 0;
}
