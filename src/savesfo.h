/* savesfo.h — minimal SFO parser / patcher for re-signing save data.
 *
 * Only the two fields needed to bind a save to a different user account are
 * touched.  Everything else in the SFO is left byte-for-byte as found.
 *
 * PS5 save:  patch ACCOUNT_ID (8 bytes) + USER_ID inside PARAMS (u32 at +4).
 * PS4 save:  copy the entire PARAMS blob (1024 bytes) from a "template" SFO
 *            that already belongs to the target user — this also carries the
 *            per-user HMAC, so no key material is needed.
 *
 * All functions operate on a malloc'd in-memory buffer so callers can read the
 * SFO, modify it and write it back without touching the file until it is ready.
 *
 *   savesfo_read   reads the whole SFO from a file descriptor → buffer
 *   savesfo_free   frees that buffer
 *   savesfo_write  writes the buffer back to a file descriptor (O_WRONLY|O_TRUNC)
 *   savesfo_patch_ps5   patches ACCOUNT_ID + USER_ID for a PS5 save
 *   savesfo_patch_ps4   copies the PARAMS blob from a template buffer into dst
 *   savesfo_get_str  reads a string field by key name (e.g. "TITLE_ID")
 *   savesfo_get_u32  reads a u32 field by key name  (e.g. "USER_ID" in PARAMS)
 */

#pragma once
#include <stdint.h>
#include <stddef.h>

/* Opaque SFO buffer.  The caller treats it as a read/write blob. */
typedef struct { uint8_t *buf; size_t len; } savesfo_t;

/* Read a whole SFO from an open fd.  Returns 0 on success, -1 on error
   (errno set).  The SFO is allocated and stored in *out; call savesfo_free
   when done.  The fd is not closed. */
int  savesfo_read(int fd, savesfo_t *out);

/* Free the buffer returned by savesfo_read. */
void savesfo_free(savesfo_t *sfo);

/* Write the (possibly modified) SFO back to an open fd at position 0.
   Returns 0 on success, -1 on error. */
int  savesfo_write(int fd, const savesfo_t *sfo);

/* Patch a PS5 save param.sfo:
 *   account_id  8-byte account id in little-endian
 *   user_id     32-bit user id (written into PARAMS.user_id at byte offset 4)
 * Returns 0 on success, -1 when a required field is not found. */
int  savesfo_patch_ps5(savesfo_t *sfo, uint64_t account_id, uint32_t user_id);

/* Copy the PARAMS blob (1024 bytes) from the template SFO (which already
 * belongs to the target user) into the destination SFO.  This carries the
 * per-user HMAC and user_id together.
 * Returns 0 on success, -1 when either SFO lacks a PARAMS field of the
 * expected size. */
int  savesfo_patch_ps4(savesfo_t *dst, const savesfo_t *tmpl);

/* Read a NUL-terminated string field into buf (at most buf_len bytes including
 * the NUL).  Returns the number of characters written (≥ 0) or -1. */
int  savesfo_get_str(const savesfo_t *sfo, const char *key,
                     char *buf, size_t buf_len);

/* Read a u32 field by key name.  Returns 0 on success, -1 on failure. */
int  savesfo_get_u32(const savesfo_t *sfo, const char *key, uint32_t *out);

/* Read raw bytes from a field into buf (at most buf_len bytes).
 * Returns the number of bytes copied (>= 0) or -1 on failure.
 * Use this for binary fields such as ACCOUNT_ID (8 bytes). */
int  savesfo_get_raw(const savesfo_t *sfo, const char *key,
                     uint8_t *buf, size_t buf_len);
