/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 gasb07
 * PS4 pkg reader: header, entry table, fake-key entry decryption, param.sfo. */
/* PS4 pkg reader: header, entry table, fake-key entry decryption, param.sfo. */
#include "ps4m_pkg.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "ps4m_crypto.h"
#include "ps4m_keys.h"

static int unwrap_key3(pkg_t *pkg);

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static uint64_t be64(const uint8_t *p) { return (uint64_t)be32(p) << 32 | be32(p + 4); }

static int read_at(int fd, void *buf, size_t len, uint64_t off) {
    uint8_t *p = buf;
    while (len) {
        ssize_t n = pread(fd, p, len, (off_t)off);
        if (n <= 0)
            return -1;
        p += n;
        len -= (size_t)n;
        off += (uint64_t)n;
    }
    return 0;
}

int p4m_pkg_fake_key(pkg_t *pkg) {
    if (!pkg->dk3_tried) {
        pkg->dk3_tried = 1;
        unwrap_key3(pkg);
    }
    return pkg->have_dk3;
}

static int unwrap_key3(pkg_t *pkg) {
    size_t len;
    uint8_t *keys = p4m_pkg_entry(pkg, 0x10, &len);
    if (!keys || len < 0x20 + 7 * 0x20 + 4 * 0x100) {
        free(keys);
        return -1;
    }
    uint8_t block[256];
    p4m_rsa2048_modexp(keys + 0x20 + 7 * 0x20 + 3 * 0x100, k_pkg_key3_d, k_pkg_key3_n, block);
    free(keys);
    if (block[0] != 0 || block[1] != 2)
        return -1;
    int i = 2;
    while (i < 256 && block[i])
        i++;
    if (256 - (i + 1) != 32)
        return -1;
    memcpy(pkg->dk3, block + i + 1, 32);
    pkg->have_dk3 = 1;
    return 0;
}

int p4m_pkg_open(pkg_t *pkg, const char *path) {
    memset(pkg, 0, sizeof(*pkg));
    pkg->fd = open(path, O_RDONLY);
    if (pkg->fd < 0)
        return -1;
    struct stat st; /* (app) the file's size, so that no entry is read past it (PS4Mount PR #18) */
    if (fstat(pkg->fd, &st) || read_at(pkg->fd, pkg->header, sizeof(pkg->header), 0) || be32(pkg->header) != 0x7f434e54)
        goto fail;
    pkg->file_size = (uint64_t)st.st_size;
    memcpy(pkg->content_id, pkg->header + 0x40, 36);
    /* (app) the content id names folders too (/user/playgo/addcont/<id>, and its label from byte 20 on below addcont/<title>,
       which drop_dlc removes): a real one is always 36 characters of A-Z, a-z, 0-9, '-' and '_'. A shorter one would leave
       an empty label and with it the folder of all add-ons of the game; '/' or ".." would leave the folder altogether. */
    for (int k = 0; k < 36; k++) {
        char c = pkg->content_id[k];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_'))
            goto fail;
    }
    memcpy(pkg->title_id, pkg->content_id + 7, 9);
    /* (app) the title id from the header names folders on the console (/user/appmeta/<id>, ...): only AAAA00000 */
    for (int k = 0; k < 9; k++)
        if (k < 4 ? (pkg->title_id[k] < 'A' || pkg->title_id[k] > 'Z') : (pkg->title_id[k] < '0' || pkg->title_id[k] > '9'))
            goto fail;
    pkg->pfs_offset = be64(pkg->header + 0x410);
    pkg->pfs_size = be64(pkg->header + 0x418);
    pkg->pkg_size = be64(pkg->header + 0x430);
    memcpy(pkg->digest, pkg->header + 0xfe0, 32);
    uint32_t count = be32(pkg->header + 0x10), table = be32(pkg->header + 0x18);
    if (count > PKG_MAX_ENTRIES)
        goto fail;
    uint8_t *tbl = malloc(count * 32u);
    if (!tbl || read_at(pkg->fd, tbl, count * 32u, table)) {
        free(tbl);
        goto fail;
    }
    for (uint32_t i = 0; i < count; i++) {
        pkg_entry_t *e = &pkg->entries[i];
        memcpy(e->raw, tbl + i * 32, 32);
        e->id = be32(e->raw);
        e->flags1 = be32(e->raw + 8);
        e->flags2 = be32(e->raw + 12);
        e->offset = be32(e->raw + 16);
        e->size = be32(e->raw + 20);
    }
    free(tbl);
    pkg->nentries = (int)count;
    return 0;
fail:
    close(pkg->fd);
    pkg->fd = -1;
    return -1;
}

void p4m_pkg_close(pkg_t *pkg) {
    if (pkg->fd >= 0)
        close(pkg->fd);
    pkg->fd = -1;
}

uint8_t *p4m_pkg_entry(pkg_t *pkg, uint32_t id, size_t *len) {
    for (int i = 0; i < pkg->nentries; i++) {
        pkg_entry_t *e = &pkg->entries[i];
        if (e->id != id)
            continue;
        /* the size comes from the file: the entries read here (param.sfo, artwork, sound,
         * PlayGo data) are far smaller, and a bigger one is a damaged pkg */
        if (e->size > PKG_MAX_ENTRY_SIZE)
            return NULL;
        size_t padded = ((size_t)e->size + 15u) & ~(size_t)15u;
        if ((uint64_t)e->offset + padded > pkg->file_size) /* (app) PS4Mount PR #18 */
            return NULL;
        uint8_t *buf = malloc(padded + 1);
        if (!buf || read_at(pkg->fd, buf, padded, e->offset)) {
            free(buf);
            return NULL;
        }
        if (e->flags1 & 0x80000000u) {
            if (!p4m_pkg_fake_key(pkg) || ((e->flags2 >> 12) & 0xf) != 3) {
                free(buf);
                return NULL;
            }
            uint8_t seed[64], ivk[32];
            memcpy(seed, e->raw, 32);
            memcpy(seed + 32, pkg->dk3, 32);
            p4m_sha256(seed, sizeof(seed), ivk);
            p4m_aes128_cbc_decrypt(ivk + 16, ivk, buf, padded);
        }
        buf[e->size] = 0;
        *len = e->size;
        return buf;
    }
    return NULL;
}

static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

int p4m_sfo_parse(const uint8_t *d, size_t len, sfo_t *out) {
    memset(out, 0, sizeof(*out));
    if (len < 20 || le32(d) != 0x46535000)
        return -1;
    uint32_t ko = le32(d + 8), dof = le32(d + 12), n = le32(d + 16);
    for (uint32_t i = 0; i < n && out->count < 128; i++) {
        size_t eo = 20 + (size_t)i * 16; /* (app) checked before the pointer is formed (PS4Mount PR #18) */
        if (eo + 16 > len)
            return -1;
        const uint8_t *ent = d + eo;
        uint16_t kof = le16(ent), fmt = le16(ent + 2);
        uint32_t ln = le32(ent + 4), vof = le32(ent + 12);
        /* offsets and lengths come from the file: everything must lie inside it */
        uint64_t key = (uint64_t)ko + kof, val = (uint64_t)dof + vof;
        if (key >= len || val > len || (fmt == 0x404 ? 4 : ln) > len - val)
            return -1;
        sfo_item_t *it = &out->items[out->count++];
        size_t kl = strnlen((const char *)d + key, len - key);
        if (kl > sizeof(it->key) - 1)
            kl = sizeof(it->key) - 1;
        memcpy(it->key, d + key, kl);
        it->key[kl] = 0;
        const uint8_t *v = d + val;
        if (fmt == 0x404) {
            it->is_int = 1;
            it->ival = le32(v);
        } else {
            size_t l = ln < sizeof(it->sval) - 1 ? ln : sizeof(it->sval) - 1;
            memcpy(it->sval, v, l);
            it->sval[l] = 0;
        }
    }
    return 0;
}

const sfo_item_t *p4m_sfo_get(const sfo_t *sfo, const char *key) {
    for (int i = 0; i < sfo->count; i++)
        if (!strcmp(sfo->items[i].key, key))
            return &sfo->items[i];
    return NULL;
}

/* s matches pat: 'A' an uppercase letter, '9' a digit, 'X' a letter or digit, anything
 * else itself. */
static int id_like(const char *s, const char *pat) {
    for (; *pat; s++, pat++) {
        int up = *s >= 'A' && *s <= 'Z', dig = *s >= '0' && *s <= '9';
        if (*pat == 'A' ? !up : *pat == '9' ? !dig : *pat == 'X' ? !(up || dig) : *s != *pat)
            return 0;
    }
    return !*s;
}

int p4m_sfo_check(const sfo_t *sfo, char *why, size_t n) {
    static const struct {
        const char *key, *pat;
        int optional;
    } k_fields[] = {
        {"TITLE_ID", "AAAA99999", 0},
        {"CONTENT_ID", "AA9999-AAAA99999_00-XXXXXXXXXXXXXXXX", 0},
        {"APP_VER", "99.99", 1},
        {"VERSION", "99.99", 1},
    };
    for (size_t i = 0; i < sizeof(k_fields) / sizeof(k_fields[0]); i++) {
        const sfo_item_t *it = p4m_sfo_get(sfo, k_fields[i].key);
        if (!it && k_fields[i].optional)
            continue;
        if (!it || it->is_int || !id_like(it->sval, k_fields[i].pat)) {
            snprintf(why, n, "%s is %s", k_fields[i].key, it ? "malformed" : "missing");
            return -1;
        }
    }
    /* every add-on service ID key, also ones past the usual _1.._7 */
    for (int i = 0; i < sfo->count; i++) {
        const sfo_item_t *it = &sfo->items[i];
        if (strncmp(it->key, "SERVICE_ID_ADDCONT_ADD_", 23))
            continue;
        if (it->is_int || (it->sval[0] && !id_like(it->sval, "AA9999-AAAA99999_00"))) {
            snprintf(why, n, "%s is malformed", it->key);
            return -1;
        }
    }
    return 0;
}
