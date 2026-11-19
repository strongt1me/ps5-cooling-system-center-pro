/* PS5 Cooling & System Center - Pro: the param.sfo of a save (19.11.2026).
 *
 * A save carries a small key/value file in the PSF format: its title id, the folder name of the slot, the name shown in the list and the account that owns it.
 * To carry a save to another console only two things are read and one is changed: the strings above are read, and the owner (ACCOUNT_ID, 8 bytes, and the
 * local user id) is set to the person's account on the other console.
 *
 * The format (header 20 bytes: "\0PSF", version, key table offset, data table offset, entry count; then 16 bytes per entry: key offset u16, format u16,
 * used length u32, capacity u32, data offset u32; then the key names, then the values) is looked at with every offset checked against the size of the buffer:
 * a file that lies about itself is refused, never read past its end. Nothing is ever resized: a value is changed in the room it already has.
 *
 * Pure functions on a buffer, so a host test can try them (test/savesfo). */
#ifndef PS5TM_SAVESFO_H
#define PS5TM_SAVESFO_H

#include <stddef.h>
#include <stdint.h>

/* 1 when the buffer is a sound PSF (magic, tables inside the buffer, every key terminated, every value inside its capacity), else 0. */
int savesfo_valid(const uint8_t *buf, size_t len);

/* The place of the value of key: off (from the start of the buffer), used (bytes in use), cap (bytes it may take). 0 when found, -1 when not (or the buffer is not sound). */
int savesfo_find(const uint8_t *buf, size_t len, const char *key, size_t *off, size_t *used, size_t *cap);

/* A text value, always terminated: its length, or -1 when the key is missing. */
int savesfo_get_str(const uint8_t *buf, size_t len, const char *key, char *out, size_t out_cap);

/* The raw bytes of a value (at most out_cap): how many were copied, or -1. */
int savesfo_get_raw(const uint8_t *buf, size_t len, const char *key, uint8_t *out, size_t out_cap);

/* A 32-bit value (little endian): 0 when found, -1 when not (or shorter than four bytes). */
int savesfo_get_u32(const uint8_t *buf, size_t len, const char *key, uint32_t *out);

/* Writes n bytes into the value of key, starting at offset off inside it (the value keeps its size; the bytes must lie inside what is in use). 0, or -1 when the key is
   missing or the bytes do not fit. */
int savesfo_set_raw(uint8_t *buf, size_t len, const char *key, size_t off, const void *data, size_t n);

/* The owner of a PS5 save: ACCOUNT_ID (8 bytes, little endian) and, when has_uid, the local user id (the field USER_ID when there is one, else the second 32-bit
 * word of PARAMS). 0 when done; -1 when there is no ACCOUNT_ID of 8 bytes; -2 when there is no place for the user id. Nothing is changed unless all is found. */
int savesfo_patch_owner(uint8_t *buf, size_t len, uint64_t account_id, int has_uid, uint32_t uid);

#endif
