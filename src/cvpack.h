/* PS5 Cooling & System Center - Pro - the choice to pack a game into LZ4 or zstd asset packs while an image is written (19.11.2026).
 *
 * The request of the pages (convert start, check, queue) carries "pack": "lz4" | "zstd" | "none", "pack_level" and "pack_runtime": "ng" | "ampr_emu".
 * Dump Forge does the packing (gameconvert.c asks it for the image), so what counts here is only that the choice is one it takes. */
#ifndef PS5TM_CVPACK_H
#define PS5TM_CVPACK_H

#include <stddef.h>

struct cJSON;

/* kind 0 = none, 1 = LZ4, 2 = zstd (only with AMPR EMU NG); level LZ4 0 or 3..12, zstd 1..22; runtime 0 = AMPR EMU NG (the only one since Dump Forge 0.6.0, which
   no longer carries the ampr_emu release build: "pack_runtime":"ampr_emu" is refused with 400; 1 was that build). */
typedef struct { int kind, level, runtime; } ps5tm_cvpack_t;

/* Reads "pack" ("lz4", "zstd", "none" or absent), "pack_level" (a whole number; absent = 9 for LZ4 and 5 for zstd, the pages' middle choice) and "pack_runtime"
   ("ng", "ampr_emu"; LZ4 only) of a request body. 0, or 400 with a sentence in err (an unknown kind, a level the kind does not have, "ampr_emu" with zstd).
   A NULL body, or one without "pack", is no packing (kind 0, nothing else read). */
int ps5tm_cvpack_parse(const struct cJSON *body, ps5tm_cvpack_t *pack, char *err, size_t err_len);

#endif
