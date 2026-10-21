/* savesfo.c — minimal SFO parser / patcher for re-signing save data.
 *
 * SFO format (psdevwiki.com/ps4/System_File_Object_(SFO)):
 *   header (20 bytes):   magic 0x46535000, version 0x0101,
 *                        key_table_offset, data_table_offset, num_entries
 *   index table:         num_entries × 16-byte records (key_offset u16,
 *                        param_format u16, param_length u32, param_max_length u32,
 *                        data_offset u32)
 *   key table:           NUL-terminated key strings
 *   data table:          values, each padded to param_max_length
 *
 * We never resize any field (the ACCOUNT_ID and PARAMS fields are patched
 * in-place at their existing size), so the format can be handled as a
 * flat buffer with offset arithmetic — no dynamic structures needed.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "savesfo.h"

/* ── SFO constants ─────────────────────────────────────────────────────── */

#define SFO_MAGIC      0x46535000u
#define SFO_VERSION    0x0101u
#define FMT_UTF8_SPEC  0x0004u          /* NUL-terminated UTF-8 string  */
#define FMT_UTF8_PLAIN 0x0204u          /* NUL-terminated UTF-8 string  */
#define FMT_INT32      0x0404u          /* little-endian u32            */
#define FMT_RAW        0x0804u          /* raw bytes (PARAMS etc.)      */

#define PARAMS_LEN     1024             /* PARAMS blob is always 1024 B */
#define ACCOUNT_ID_LEN    8

/* ── Layout helpers ────────────────────────────────────────────────────── */

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t version;
    uint32_t key_table_off;
    uint32_t data_table_off;
    uint32_t num_entries;
} sfo_hdr_t;

typedef struct __attribute__((packed)) {
    uint16_t key_off;
    uint16_t param_fmt;
    uint32_t param_len;
    uint32_t param_max_len;
    uint32_t data_off;
} sfo_idx_t;

/* ── Internal helpers ──────────────────────────────────────────────────── */

static int sfo_valid(const savesfo_t *sfo)
{
    if (sfo->len < sizeof(sfo_hdr_t))
        return 0;
    const sfo_hdr_t *h = (const sfo_hdr_t *)sfo->buf;
    if (h->magic != SFO_MAGIC || h->version != SFO_VERSION)
        return 0;
    size_t idx_end = sizeof(sfo_hdr_t) + (size_t)h->num_entries * sizeof(sfo_idx_t);
    if (idx_end > sfo->len || h->key_table_off < idx_end)
        return 0;
    if (h->data_table_off < h->key_table_off || h->data_table_off > sfo->len)
        return 0;
    return 1;
}

/* Find an entry by key name.  Sets *idx_out (may be NULL) and returns the
 * pointer to the data field, or NULL when not found.
 * *data_len_out receives param_len (actual data, not max). */
static uint8_t *sfo_find(const savesfo_t *sfo, const char *key,
                          const sfo_idx_t **idx_out, uint32_t *data_len_out)
{
    const sfo_hdr_t *h = (const sfo_hdr_t *)sfo->buf;
    const sfo_idx_t *table =
        (const sfo_idx_t *)(sfo->buf + sizeof(sfo_hdr_t));

    for (uint32_t i = 0; i < h->num_entries; i++) {
        size_t koff = h->key_table_off + table[i].key_off;
        if (koff >= sfo->len)
            continue;
        const char *kp = (const char *)sfo->buf + koff;
        /* Ensure NUL-termination within the buffer */
        size_t max_key = sfo->len - koff;
        if (strnlen(kp, max_key) >= max_key)
            continue;
        if (strcmp(kp, key) != 0)
            continue;

        size_t doff = h->data_table_off + table[i].data_off;
        uint32_t dlen = table[i].param_len;
        uint32_t dmax = table[i].param_max_len;
        if (doff + dmax > sfo->len)
            continue;

        if (idx_out)       *idx_out       = &table[i];
        if (data_len_out)  *data_len_out  = dlen;
        return sfo->buf + doff;
    }
    return NULL;
}

/* ── Public API ────────────────────────────────────────────────────────── */

int savesfo_read(int fd, savesfo_t *out)
{
    out->buf = NULL;
    out->len = 0;

    /* Determine size */
    off_t sz = lseek(fd, 0, SEEK_END);
    if (sz < (off_t)sizeof(sfo_hdr_t) || sz > (1 << 20)) {
        errno = EINVAL;
        return -1;
    }
    if (lseek(fd, 0, SEEK_SET) != 0)
        return -1;

    uint8_t *buf = malloc((size_t)sz);
    if (!buf)
        return -1;

    ssize_t n = read(fd, buf, (size_t)sz);
    if (n != (ssize_t)sz) {
        free(buf);
        return -1;
    }

    out->buf = buf;
    out->len = (size_t)sz;

    if (!sfo_valid(out)) {
        savesfo_free(out);
        errno = EINVAL;
        return -1;
    }
    return 0;
}

void savesfo_free(savesfo_t *sfo)
{
    free(sfo->buf);
    sfo->buf = NULL;
    sfo->len = 0;
}

int savesfo_write(int fd, const savesfo_t *sfo)
{
    if (lseek(fd, 0, SEEK_SET) != 0)
        return -1;
    if (ftruncate(fd, (off_t)sfo->len) != 0)
        return -1;
    ssize_t written = write(fd, sfo->buf, sfo->len);
    return (written == (ssize_t)sfo->len) ? 0 : -1;
}

int savesfo_patch_ps5(savesfo_t *sfo, uint64_t account_id, uint32_t user_id)
{
    /* Patch ACCOUNT_ID (8 raw bytes, little-endian u64) */
    uint32_t aid_len;
    uint8_t *aid_ptr = sfo_find(sfo, "ACCOUNT_ID", NULL, &aid_len);
    if (!aid_ptr || aid_len < ACCOUNT_ID_LEN)
        return -1;
    memcpy(aid_ptr, &account_id, ACCOUNT_ID_LEN);

    /* Patch USER_ID inside PARAMS at byte offset 4.
     * PARAMS layout (Apollo PS4 / Garlic): u32 unk1, u32 user_id, ... */
    uint32_t params_len;
    uint8_t *params = sfo_find(sfo, "PARAMS", NULL, &params_len);
    if (params && params_len >= 8) {
        memcpy(params + 4, &user_id, 4);
    }
    /* USER_ID may also appear as its own key on some firmwares */
    uint32_t uid_len;
    uint8_t *uid_ptr = sfo_find(sfo, "USER_ID", NULL, &uid_len);
    if (uid_ptr && uid_len >= 4) {
        memcpy(uid_ptr, &user_id, 4);
    }
    return 0;
}

int savesfo_patch_ps4(savesfo_t *dst, const savesfo_t *tmpl)
{
    uint32_t dst_len, tmpl_len;
    uint8_t *dst_params  = sfo_find(dst,  "PARAMS", NULL, &dst_len);
    const uint8_t *src_params = sfo_find(tmpl, "PARAMS", NULL, &tmpl_len);
    if (!dst_params || !src_params)
        return -1;
    if (dst_len != PARAMS_LEN || tmpl_len != PARAMS_LEN)
        return -1;
    /* Copy PARAMS wholesale — this includes user_id + psid_hmac + title IDs */
    memcpy(dst_params, src_params, PARAMS_LEN);
    return 0;
}

int savesfo_get_str(const savesfo_t *sfo, const char *key,
                    char *buf, size_t buf_len)
{
    if (!buf_len)
        return -1;
    uint32_t dlen;
    const uint8_t *d = sfo_find(sfo, key, NULL, &dlen);
    if (!d)
        return -1;
    size_t copy = (dlen < buf_len) ? dlen : buf_len - 1;
    memcpy(buf, d, copy);
    buf[copy] = '\0';
    return (int)strnlen(buf, buf_len);
}

int savesfo_get_u32(const savesfo_t *sfo, const char *key, uint32_t *out)
{
    uint32_t dlen;
    const uint8_t *d = sfo_find(sfo, key, NULL, &dlen);
    if (!d || dlen < 4)
        return -1;
    memcpy(out, d, 4);
    return 0;
}

int savesfo_get_raw(const savesfo_t *sfo, const char *key,
                    uint8_t *buf, size_t buf_len)
{
    if (!buf_len)
        return -1;
    uint32_t dlen;
    const uint8_t *d = sfo_find(sfo, key, NULL, &dlen);
    if (!d)
        return -1;
    size_t copy = (dlen < buf_len) ? dlen : buf_len;
    memcpy(buf, d, copy);
    return (int)copy;
}
