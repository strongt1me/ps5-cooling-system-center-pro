/* SPDX-License-Identifier: GPL-3.0-or-later
 * The PS4 dump as an image for ShadowMountPlus — see dump_image.c. */
#ifndef PS5TM_DUMP_IMAGE_H
#define PS5TM_DUMP_IMAGE_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
  char title_id[12];       /* CUSA12345 */
  char content_id[40];
  char title[128];         /* what the param.sfo calls the game */
  char app_ver[16];
  int  have_patch;
} dimg_info_t;

/* Checks a dump (base folder, optionally the update folder) and reads what the image needs from the param.sfo that will
   be the final one (the update's, else the base's). 0 good; -1 with the reason, in German, in err. Reads two small files. */
int dimg_check(const char *base, const char *patch, dimg_info_t *info, char *err, size_t err_len);

/* Writes the files the image gets beyond the dump into overlay_dir/sce_sys: the param.json ShadowMountPlus reads and, if
   the dump's npbind.dat has a wrong SHA-1 footer, a repaired one. *npbind_repaired is 1 then. Replaces what an earlier run
   of this left there; other files in the folder stay. 0 good, -1 with the reason in err. */
int dimg_write_overlay(const char *base, const char *patch, const char *overlay_dir, const dimg_info_t *info,
                       int *npbind_repaired, char *err, size_t err_len);

/* Removes the files dimg_write_overlay made, and the folders if they are empty by then. */
void dimg_remove_overlay(const char *overlay_dir);

/* "<Title> [<CUSA12345>]" as a file name stem: characters a drive refuses are left out. */
void dimg_stem(const char *title, const char *title_id, char *out, size_t out_len);

/* ---- the parts, for tests ---- */

/* The param.json for an SFO (malloc'ed, *len bytes, ends with a newline); NULL on a bad SFO. title_id and title are
   what the file says as titleId and titleName. */
char *dimg_param_json(const uint8_t *sfo, size_t sfo_len, const char *title_id, const char *title, size_t *len);

/* 0 the footer is right, 1 it was wrong and is now corrected in data, -1 not an npbind.dat of the known layout. */
int dimg_npbind_fix(uint8_t *data, size_t len);

/* One text value of an SFO; 0 found (stripped of white space at both ends), -1 not there or not text. */
int dimg_sfo_text(const uint8_t *sfo, size_t sfo_len, const char *key, char *out, size_t out_len);
/* The title as the converter picks it (TITLE, else the English one, else any, else the ids). */
void dimg_sfo_title(const uint8_t *sfo, size_t sfo_len, char *out, size_t out_len);

#endif
