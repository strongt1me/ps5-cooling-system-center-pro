/* Fuzz harness: a hostile ZIP file put in front of the reader and of the inspection of a save (src/savezip.c, src/chives.c, src/savesfo.c, src/saveimport.c).
 *
 * The input is written as a file (the readers take a path), opened, every entry read into memory (capped) and extracted to /dev/null, and the file is looked at
 * as a save to be put in (saveimport_inspect: nothing is written to the console). Nothing may crash, read out of bounds or run away; the sanitizers say so.
 * Built and run by test/fuzz/run.sh (clang, libFuzzer, address + undefined sanitizer). */
#define _GNU_SOURCE
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "saveimport.h"
#include "savezip.h"

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  static char path[64];
  if(!path[0]) snprintf(path, sizeof(path), "/dev/shm/ps5tm-fuzz-%d.zip", (int)getpid());
  if(size > (4u << 20)) return 0;
  int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
  if(fd < 0) return 0;
  size_t off = 0;
  while(off < size) {
    ssize_t w = write(fd, data + off, size - off);
    if(w <= 0) break;
    off += (size_t)w;
  }
  close(fd);

  int err = 0;
  szr_t *z = szr_open(path, &err);
  if(z) {
    const unsigned n = szr_count(z);
    int dn = open("/dev/null", O_WRONLY);
    for(unsigned i = 0; i < n && i < 64; i++) {
      const szr_entry_t *e = szr_at(z, i);
      if(!e || e->is_dir) continue;
      uint8_t *buf = NULL;
      size_t len = 0;
      if(szr_read(z, i, &buf, &len, 1u << 20) == SZ_OK) free(buf);
      if(dn >= 0) (void)szr_extract(z, i, dn, 1u << 20, NULL, NULL);
    }
    if(dn >= 0) close(dn);
    szr_close(z);
  }

  saveimport_info_t info;
  char msg[400];
  (void)saveimport_inspect(path, &info, msg, sizeof(msg));
  unlink(path);
  return 0;
}
