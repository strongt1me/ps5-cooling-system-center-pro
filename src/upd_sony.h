/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Knutwurst (PatchDL 0.3.0: patchdl_resolve.c, patchdl_verxml.c, the manifest and piece parts of
 * patchdl_net.c), adapted for PS5 Cooling & System Center - Pro
 *
 * Sony's side of the game updates: which hosts may be asked, where a game's update list (version.xml) is, what it says,
 * the JSON manifest of an update package (its pieces with their SHA-256, SHA-1 for PS4) and fetching one piece. */
#ifndef PS5TM_UPD_SONY_H
#define PS5TM_UPD_SONY_H

#include <stddef.h>
#include <stdint.h>

/* 1 when host is one of Sony's download hosts (or a subdomain of one). */
int  upd_sony_host_ok(const char *host);
/* hop rule for upd_http: Sony hosts; plain http only for the PS4 manifest host gs2. */
int  upd_sony_hop_ok(const char *host, int https, void *ctx);
/* hop rule for pieces: Sony hosts, http allowed (the CDN redirects pieces to signed http edges; every piece is hashed) */
int  upd_sony_piece_hop_ok(const char *host, int https, void *ctx);

/* The version.xml URL of a PS4 game (HMAC-SHA256 of "np_<id>" under the published PS4 update key). 0, or -1. */
int  upd_sony_ps4_url(const char *title_id, char *url, size_t n);

typedef struct {
  char compatible_version[16];       /* highest package with system_ver <= fw, or "" */
  char latest_version[16];           /* highest overall, or "" */
  char latest_required_fw[16];       /* "11.60" for the latest, "" for PS4 */
  char compatible_url[512];          /* manifest_url if present, else the package URL */
  char delta_url[512];               /* DP.pkg bootstrap */
  char compatible_title[16];         /* target title id */
  char compatible_storage_title[16]; /* title id in the delta_url storage path */
} upd_verinfo_t;

/* Parses a version.xml held in memory (exported for tests). */
void upd_sony_parse_verxml(const char *xml, uint32_t fw_bin, upd_verinfo_t *out);
/* Fetches and parses it. 0 when a version was listed, -1 otherwise (step in why). */
int  upd_sony_verxml(const char *url, uint32_t fw_bin, upd_verinfo_t *out, char *why, size_t wn);

typedef struct {
  char    *url;
  int64_t  offset;     /* in the assembled package */
  int64_t  size;
  char     hash[80];   /* SHA-256 (64 hex) for PS5, SHA-1 (40 hex) for PS4, or "" */
} upd_piece_t;

typedef struct {
  upd_piece_t *pieces;
  int          count;
  int64_t      total;
  char         crc_url[768];
} upd_manifest_t;

/* Parses a manifest held in memory: pieces in order, contiguous, each 1 byte to 8 GiB, at most 4096 and 200 GiB in all. */
int  upd_sony_parse_manifest(const char *json, upd_manifest_t *out);
/* Fetches (twice more after a network failure) and parses. 0, or -1 (step in why). */
int  upd_sony_manifest(const char *url, upd_manifest_t *out, char *why, size_t wn);
void upd_manifest_free(upd_manifest_t *m);

/* Downloads bytes [from, from+len) of a piece URL and pwrites them into fd at file_off. *done counts the bytes written so far
   (read by the progress display), abort stops it. 0, -1 network/IO (step in why), -3 the server did not answer the range. */
int  upd_sony_fetch_range(const char *url, int fd, int64_t file_off, int64_t from, int64_t len,
                          volatile int64_t *done, volatile int *abort, char *why, size_t wn);

/* Hashes [off, off+size) of fd: SHA-1 when like_hex has 40 digits, else SHA-256. out >= 65 bytes. 0, or -1 (-2 aborted). */
int  upd_hash_fd(int fd, int64_t off, int64_t size, const char *like_hex, char *out, volatile int *abort);

/* "11.60" for 0x11600000 */
void upd_fw_text(uint32_t bin, char *out, size_t n);

#endif
