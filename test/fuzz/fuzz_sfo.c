/* Fuzz harness: a hostile param.sfo and a hostile chives.json (src/savesfo.c, src/chives.c).
 *
 * The first byte chooses what the rest is: a PSF file put through every reader and every writer of src/savesfo.h (on a copy), or a JSON text put through
 * chives_parse. Built and run by test/fuzz/run.sh (clang, libFuzzer, address + undefined sanitizer). */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "chives.h"
#include "savesfo.h"

static const char *const KEYS[] = { "ACCOUNT_ID", "PARAMS", "TITLE_ID", "SAVEDATA_DIRECTORY", "SAVEDATA_BLOCKS", "SYSTEM_BLOCKS", "USER_ID", "MAINTITLE", "NICHT_DA" };

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if(size < 2 || size > (1u << 20)) return 0;
  const uint8_t mode = data[0];
  data++;
  size--;
  if(mode & 1) {
    chives_t g;
    chives_init(&g);
    char *js = malloc(size + 1);
    if(!js) return 0;
    memcpy(js, data, size);
    js[size] = 0;
    if(chives_parse(js, size, &g) == 0) (void)chives_valid(&g);
    chives_free(&g);
    free(js);
    return 0;
  }
  uint8_t *buf = malloc(size);
  if(!buf) return 0;
  memcpy(buf, data, size);
  (void)savesfo_valid(buf, size);
  for(size_t k = 0; k < sizeof(KEYS) / sizeof(KEYS[0]); k++) {
    char s[64];
    uint8_t raw[1100];
    uint32_t u;
    size_t off, used, cap;
    (void)savesfo_get_str(buf, size, KEYS[k], s, sizeof(s));
    (void)savesfo_get_raw(buf, size, KEYS[k], raw, sizeof(raw));
    (void)savesfo_get_u32(buf, size, KEYS[k], &u);
    (void)savesfo_find(buf, size, KEYS[k], &off, &used, &cap);
    const uint8_t patch[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    (void)savesfo_set_raw(buf, size, KEYS[k], (size_t)mode, patch, (size_t)(mode >> 4));
  }
  (void)savesfo_patch_owner(buf, size, 0x1122334455667788ull, (int)(mode & 2), 0xCAFEBABE);
  free(buf);
  return 0;
}
