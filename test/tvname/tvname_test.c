/* Host test of src/tvname.h: the name of the connected display out of the monitor block (16.11.2026). */
#include "tvname.h"

#include <stdio.h>

static int g_fail;

static void
name_is(const unsigned char *b, size_t n, int want_rc, const char *want) {
  char out[64];
  int rc = ps5tm_tvname_from_block(b, n, out, sizeof(out));
  if(rc != want_rc || strcmp(out, want)) {
    printf("FAIL rc=%d name=\"%s\", wanted rc=%d \"%s\"\n", rc, out, want_rc, want);
    g_fail++;
  }
}

int
main(void) {
  /* a printable name at the start */
  name_is((const unsigned char *)"LG TV SSCR2\0\0\0\0", 16, 1, "LG TV SSCR2");
  name_is((const unsigned char *)"SAMSUNG   \0", 11, 1, "SAMSUNG");                  /* trailing blanks are cut */
  name_is((const unsigned char *)"ABC", 3, 1, "ABC");
  name_is((const unsigned char *)"AB\0\0", 4, 0, "");                               /* too short */

  /* nothing printable at the start, nothing else */
  {
    unsigned char b[76];
    memset(b, 0, sizeof(b));
    b[0] = 0x01; b[1] = 0x02; b[8] = 0x10;
    name_is(b, sizeof(b), 0, "");
  }

  /* an EDID name descriptor in the middle of the block */
  {
    unsigned char b[76];
    memset(b, 0x7f, sizeof(b));
    memcpy(b + 20, "\x00\x00\x00\xfc\x00" "ACME TV  *00\x0a", 5 + 13);
    b[0] = 0x01;
    name_is(b, sizeof(b), 1, "ACME TV  *00");
  }
  {
    unsigned char b[40];
    memset(b, 0x01, sizeof(b));
    memcpy(b + 4, "\x00\x00\x00\xfc\x00" "BRAVIA\x0a   \x20\x20\x20", 5 + 13);
    name_is(b, sizeof(b), 1, "BRAVIA");                                              /* ended by the line feed */
  }
  {
    unsigned char b[16];                                                              /* the descriptor is cut by the end of the block */
    memset(b, 0x01, sizeof(b));
    memcpy(b + 8, "\x00\x00\x00\xfc\x00" "AB", 7);
    name_is(b, sizeof(b), 0, "");
  }
  {
    unsigned char b[32];                                                              /* the tag without a name behind it */
    memset(b, 0x01, sizeof(b));
    memcpy(b + 2, "\x00\x00\x00\xfc\x00\x0a", 6);
    name_is(b, sizeof(b), 0, "");
  }

  /* the buffer limits */
  {
    char small[5];
    int rc = ps5tm_tvname_from_block((const unsigned char *)"ABCDEFGHIJ", 10, small, sizeof(small));
    if(rc != 1 || strcmp(small, "ABCD")) { printf("FAIL small buffer: rc=%d \"%s\"\n", rc, small); g_fail++; }
    char tiny[1] = { 'x' };
    if(ps5tm_tvname_from_block((const unsigned char *)"ABCDEFGHIJ", 10, tiny, sizeof(tiny)) != 0) { printf("FAIL tiny buffer\n"); g_fail++; }
    char big[200];
    unsigned char longname[100];
    memset(longname, 'x', sizeof(longname));
    rc = ps5tm_tvname_from_block(longname, sizeof(longname), big, sizeof(big));
    if(rc != 1 || strlen(big) != 63) { printf("FAIL long name: rc=%d len=%zu\n", rc, strlen(big)); g_fail++; }
    if(ps5tm_tvname_from_block(NULL, 5, big, sizeof(big)) != 0 || big[0]) { printf("FAIL NULL block\n"); g_fail++; }
    if(ps5tm_tvname_from_block((const unsigned char *)"abc", 0, big, sizeof(big)) != 0) { printf("FAIL empty block\n"); g_fail++; }
  }

  /* hex for the log */
  {
    char h[32];
    const unsigned char b[] = { 0x00, 0xab, 0xff, 0x10 };
    ps5tm_tvname_hex(b, sizeof(b), h, sizeof(h));
    if(strcmp(h, "00 ab ff 10")) { printf("FAIL hex: \"%s\"\n", h); g_fail++; }
    char h2[8];                                                                       /* two bytes fit into 8 */
    ps5tm_tvname_hex(b, sizeof(b), h2, sizeof(h2));
    if(strcmp(h2, "00 ab")) { printf("FAIL hex cut: \"%s\"\n", h2); g_fail++; }
    ps5tm_tvname_hex(b, 0, h, sizeof(h));
    if(h[0]) { printf("FAIL hex empty: \"%s\"\n", h); g_fail++; }
  }

  if(g_fail) { printf("%d failed\n", g_fail); return 1; }
  printf("tvname: all checks passed\n");
  return 0;
}
