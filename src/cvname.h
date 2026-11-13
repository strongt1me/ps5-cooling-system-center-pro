/* PS5 Cooling & System Center - Pro - the name of a converted image, from a template (13.11.2026).
 *
 *   {titel}   the game's name          {id}      the title id (PPSA01100)
 *   {version} the game's version       {quelle}  the name of the source (its folder or image, without the extension)
 *
 * An empty template means {quelle}: what the app always did. The text around the fields is kept; characters a drive does not
 * take in a file name (/ \ : * ? " < > | and control characters) become "_", spaces and dots at the end go. The result, with the
 * extension and the longest suffix the app adds beside it, has to fit the 255 bytes of a name on exFAT, UFS and FAT32. */
#ifndef PS5TM_CVNAME_H
#define PS5TM_CVNAME_H

#include <stddef.h>

#define PS5TM_CVNAME_MAX     255          /* bytes of one name on the drives the app writes to */
#define PS5TM_CVNAME_SUFFIX  16           /* room kept for what follows the stem: ".ffpfsc.sha256" is the longest, ".ps5cc-konv-teil" is longer but never beside it */

typedef struct {
  const char *titel, *id, *version, *quelle;
} ps5tm_cvname_fields_t;

/* Expands tpl into out. Returns 0, or -1 with a reason in err: an unknown {field}, a { without }, an empty result, a result longer
   than the limit (the bytes it would have are in *bytes when not NULL; it is set on success too). */
int ps5tm_cvname_expand(const char *tpl, const ps5tm_cvname_fields_t *f, size_t ext_len, char *out, size_t out_len,
                        size_t *bytes, char *err, size_t err_len);

#endif
