/* PS5 Cooling & System Center - Pro: when ShadowMountPlus cannot unpack, the app does (gamemove.c, 17.11.2026). The two small decisions of that, apart
   from the rest so that a host test can look at them. */
#ifndef PS5TM_UNPACKFB_H
#define PS5TM_UNPACKFB_H

#include <stdint.h>
#include <string.h>

/* 1: the job ShadowMountPlus ran ended with "Permission denied" (EACCES = 13, or its text) and was an unpack.
   Seen on 17.11.2026 (Asterix & Obelix: Heroes, a compressed ffpfsc): ShadowMountPlus makes the target folder with the permissions of the folder
   in the image (mkdir(destination, st_mode & 0777), sm_storage.c) and then cannot put the first file into it when that folder has no write bit.
   The app's own unpack gives every game folder 0777 (imgjob.c). A move, or any other error, is never taken over. */
static inline int
ps5tm_unpack_fallback_wanted(const char *op, int status, const char *msg) {
  if(!op || strcmp(op, "unpack")) return 0;
  if(status == 13) return 1;
  return msg && strstr(msg, "Permission denied") != NULL;
}

/* done/total as a whole percent, 0..100 (a total of 0 is 0 %). */
static inline int
ps5tm_unpack_fb_percent(uint64_t done, uint64_t total) {
  if(total == 0) return 0;
  if(done >= total) return 100;
  return (int)((done * 100u) / total);
}

#endif
