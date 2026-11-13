/* Host test of src/cvname.c */
#include <stdio.h>
#include <string.h>

#include "cvname.h"

static int g_fail, g_n;
#define CHECK(c) do { g_n++; if(!(c)) { g_fail++; printf("FAIL line %d: %s\n", __LINE__, #c); } } while(0)

static const ps5tm_cvname_fields_t F = { "Lantern Pass", "PPSA01100", "01.004", "lantern-pass-dump" };

static int
ex(const char *tpl, size_t ext, char *out, size_t *bytes, char *err) {
  err[0] = 0;
  return ps5tm_cvname_expand(tpl, &F, ext, out, 600, bytes, err, 200);
}

int
main(void) {
  char o[600], e[200];
  size_t b = 0;
  CHECK(ex("", 7, o, &b, e) == 0 && !strcmp(o, "lantern-pass-dump"));
  CHECK(ex(NULL, 7, o, &b, e) == 0 && !strcmp(o, "lantern-pass-dump"));
  CHECK(ex("{titel} [{id}] v{version}", 7, o, &b, e) == 0 && !strcmp(o, "Lantern Pass [PPSA01100] v01.004"));
  CHECK(b == strlen("Lantern Pass [PPSA01100] v01.004") + 7);
  CHECK(ex("{id}", 6, o, &b, e) == 0 && !strcmp(o, "PPSA01100") && b == 9 + 6);
  /* characters a file name cannot hold */
  CHECK(ex("a/b\\c:d*e?f\"g<h>i|j", 7, o, &b, e) == 0 && !strcmp(o, "a_b_c_d_e_f_g_h_i_j"));
  /* a title with a colon (as many have) */
  { ps5tm_cvname_fields_t g = { "Foo: Bar?", "PPSA00001", "", "x" }; CHECK(ps5tm_cvname_expand("{titel}", &g, 7, o, sizeof(o), &b, e, sizeof(e)) == 0 && !strcmp(o, "Foo_ Bar_")); }
  /* ends and spaces */
  CHECK(ex("  {id}. . ", 7, o, &b, e) == 0 && !strcmp(o, "PPSA01100"));
  /* control characters */
  CHECK(ex("a\tb\nc", 7, o, &b, e) == 0 && !strcmp(o, "a_b_c"));
  /* UTF-8 stays whole */
  { ps5tm_cvname_fields_t g = { "Größe ✓ 日本語", "PPSA00001", "1", "x" }; CHECK(ps5tm_cvname_expand("{titel}", &g, 7, o, sizeof(o), &b, e, sizeof(e)) == 0 && !strcmp(o, "Größe ✓ 日本語")); }
  /* bytes that are no UTF-8, and characters that fake a name */
  CHECK(ex("a\xff" "b", 7, o, &b, e) == 0 && !strcmp(o, "a_b"));                       /* a stray byte */
  CHECK(ex("a\xc0\x80" "b", 7, o, &b, e) == 0 && !strcmp(o, "a__b"));                 /* an overlong sequence */
  CHECK(ex("a\xed\xa0\x80" "b", 7, o, &b, e) == 0 && !strcmp(o, "a___b"));           /* a surrogate: each byte of it */
  CHECK(ex("a\xe2\x80\xae" "b", 7, o, &b, e) == 0 && !strcmp(o, "a_b"));             /* U+202E (right-to-left override) */
  CHECK(ex("a\xe2\x80\x8f" "b", 7, o, &b, e) == 0 && !strcmp(o, "a_b"));             /* U+200F */
  CHECK(ex("a\xc2\x85" "b", 7, o, &b, e) == 0 && !strcmp(o, "a_b"));                  /* U+0085 (a C1 control) */
  CHECK(ex("a\xef\xbb\xbf" "b", 7, o, &b, e) == 0 && !strcmp(o, "a_b"));             /* U+FEFF */
  CHECK(ex("a\xe2\x80" , 7, o, &b, e) == 0 && !strcmp(o, "a__"));                     /* a sequence cut short */
  CHECK(ex("\xc3\xa4\xc3\xb6", 7, o, &b, e) == 0 && !strcmp(o, "\xc3\xa4\xc3\xb6"));   /* ä ö stay */
  /* unknown field, missing brace, empty */
  CHECK(ex("{foo}", 7, o, &b, e) != 0 && strstr(e, "{foo}"));
  CHECK(ex("{titel", 7, o, &b, e) != 0 && strstr(e, "}"));
  CHECK(ex("...", 7, o, &b, e) != 0 && strstr(e, "leer"));
  CHECK(ex("   ", 7, o, &b, e) != 0);
  { ps5tm_cvname_fields_t g = { "", "", "", "" }; CHECK(ps5tm_cvname_expand("{titel}", &g, 7, o, sizeof(o), &b, e, sizeof(e)) != 0); }
  /* the limit: stem + extension + suffix room <= 255 */
  { char t[400]; memset(t, 'a', 232); t[232] = 0;           /* 232 + 7 + 16 = 255: fits */
    CHECK(ex(t, 7, o, &b, e) == 0 && b == 239); }
  { char t[400]; memset(t, 'a', 233); t[233] = 0;           /* one more: too long */
    CHECK(ex(t, 7, o, &b, e) != 0 && strstr(e, "zu lang") && b == 240); }
  /* the output buffer is too small: an error, never a cut */
  { char small[10]; CHECK(ps5tm_cvname_expand("{titel}", &F, 7, small, sizeof(small), &b, e, sizeof(e)) != 0); }
  /* field values are cleaned too, also long ones */
  { char big[400]; memset(big, '/', 399); big[399] = 0; ps5tm_cvname_fields_t g = { big, "", "", "" };
    CHECK(ps5tm_cvname_expand("{titel}", &g, 7, o, sizeof(o), &b, e, sizeof(e)) != 0); }
  printf("%d checks, %d failed\n", g_n, g_fail);
  return g_fail ? 1 : 0;
}
