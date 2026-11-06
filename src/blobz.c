/* The programs the app carries as zlib streams (np-fake-signin, the install helper; tools/gen_blob.py): unpacking them. */
#include <stdlib.h>

#include "ps5tm.h"
#include "third_party/libdeflate/libdeflate.h"

unsigned char *
ps5tm_blob_inflate(const unsigned char *z, unsigned long z_len, unsigned long raw_len, size_t *out_len) {
  if(!z || !z_len || !raw_len) return NULL;
  unsigned char *buf = malloc(raw_len);
  if(!buf) return NULL;
  struct libdeflate_decompressor *dc = libdeflate_alloc_decompressor();
  size_t got = 0;
  if(!dc || libdeflate_zlib_decompress(dc, z, z_len, buf, raw_len, &got) != LIBDEFLATE_SUCCESS || got != raw_len) {
    if(dc) libdeflate_free_decompressor(dc);
    free(buf);
    return NULL;
  }
  libdeflate_free_decompressor(dc);
  if(out_len) *out_len = got;
  return buf;
}
