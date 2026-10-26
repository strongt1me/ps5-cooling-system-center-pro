/* exFAT image writer, forward only — see conv_exfat.c. */
#ifndef PS5TM_CONV_EXFAT_H
#define PS5TM_CONV_EXFAT_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

typedef struct exfat_image exfat_image_t;

#define EXFAT_MAX_ROOTS 4

/* The same for several source folders laid over each other (a game, then its update, then a few files of our own): a file
   that is in more than one is read from the last, a folder in more than one is merged. Nothing is copied. */
exfat_image_t *exfat_image_plan_roots(const char *const *roots, unsigned nroots, uint32_t serial,
                                      char *err, size_t err_len);

/* Scans source_root and lays the whole volume out; nothing is written yet.
   NULL with a reason in err on failure — also for two names in one directory
   that exFAT cannot tell apart (equal after up-casing). */
exfat_image_t *exfat_image_plan(const char *source_root, uint32_t serial,
                                char *err, size_t err_len);
/* Exact size of the finished image in bytes, and what went into it. */
uint64_t       exfat_image_size(const exfat_image_t *img);
unsigned       exfat_image_files(const exfat_image_t *img);
uint64_t       exfat_image_payload(const exfat_image_t *img);
/* The next up to len bytes of the image, strictly in order. Returns the
   number of bytes produced, 0 at the end, -1 on a read error — errno holds
   the cause and exfat_image_where() the file concerned. */
ssize_t        exfat_image_read(exfat_image_t *img, void *buf, size_t len);
const char    *exfat_image_where(const exfat_image_t *img);
void           exfat_image_free(exfat_image_t *img);

#endif
