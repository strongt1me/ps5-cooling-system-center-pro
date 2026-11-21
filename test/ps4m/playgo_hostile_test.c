/* Host test of the PS4Mount port with hostile playgo-chunk.dat contents (review of 21.11.2026, M2): the scenario's first chunk index came from the file
   and (first + i) * 2 wrapped in 32 bits, so the bounds check passed and le16() read about 4 GiB past the buffer. Run under ASan (test/ps4m/run.sh). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ps4m_build.h"

static void p32(uint8_t *p, uint32_t v) { for(int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i)); }
static void p16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }

int
main(void) {
  const size_t len = 0x200;
  uint8_t *pg = calloc(1, len);
  if(!pg) return 2;
  memcpy(pg, "plgo", 4);
  p16(pg + 0x0a, 1);
  p16(pg + 0x0e, 1);
  p32(pg + 0xc0 + 4 * 8, 0x100);   /* table 4: the scenario attributes, 32 bytes at 0x100 */
  p32(pg + 0xc4 + 4 * 8, 32);
  p32(pg + 0xc0 + 5 * 8, 0x120);   /* table 5: the scenario chunks, 2 bytes at 0x120 */
  p32(pg + 0xc4 + 5 * 8, 2);
  p32(pg + 0x100 + 0x10, 0xFFFFFFFEu);   /* first = 0x7FFFFFFF: (first + 1) * 2 + 2 wrapped to 2 */
  p16(pg + 0x100 + 0x16, 1);
  buf_t out = { 0 };
  int rc = p4m_build_app_xml(&out, pg, len);
  int ok = rc == 0 && out.data && strstr((const char *)out.data, "<scenario></scenario>") != NULL;   /* the index out of the table is left out */
  printf("playgo hostile: rc=%d %s\n", rc, ok ? "ok" : "FAILED");
  free(out.data);
  free(pg);
  return ok ? 0 : 1;
}
