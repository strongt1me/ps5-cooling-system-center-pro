/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Knutwurst (PatchDL 0.3.0: patchdl_prospero.c, manifest_from_link and size_mismatch of
 * patchdl_websrv.c), adapted for PS5 Cooling & System Center - Pro
 *
 * Older patches that still run on this firmware: Sony's update list names only the newest one. Prospero Patches (PS5) and
 * Orbis Patches (PS4) list every version with the firmware it needs; the person picks one there and copies its SC link,
 * which the app turns into the manifest on Sony's CDN. Only the title id is sent to those sites. */
#ifndef PS5TM_UPD_SITE_H
#define PS5TM_UPD_SITE_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
  char     version[16];      /* "01.003.000" (PS5) or "01.07" (PS4) */
  char     required_fw[16];  /* "7.01"; for PS4 a PS4 firmware; "?" unknown */
  char     size[16];         /* the site's label, e.g. "71.7 GB" */
  uint32_t fw_bin;           /* PS5 firmware it needs, 0 for PS4, 0xFFFFFFFF unknown */
} upd_site_version_t;

#define UPD_SITE_MAX_VERSIONS 512

/* "https://prosperopatches.com" or "https://orbispatches.com" */
const char *upd_site_base(const char *title_id);
/* Every version the site lists, newest first as the site orders them. 0 with *count, or -1 (step in why). */
int  upd_site_versions(const char *title_id, upd_site_version_t *out, int max, int *count, char *why, size_t wn);
/* Parses the loadpatches answer (exported for tests). */
int  upd_site_parse_versions(const char *title_id, const char *json, upd_site_version_t *out, int max);
/* The newest version whose firmware is met. 1 found, 0 none, -1 failure. */
int  upd_site_best(const char *title_id, uint32_t fw_bin, upd_site_version_t *best, char *why, size_t wn);

/* An SC/CRC link copied off Prospero/Orbis Patches -> the manifest URL on Sony's CDN (canon). NULL when good, else a reason:
   url_invalid, url_not_allowed, not_an_sc_link, url_title_mismatch, url_too_long. */
const char *upd_site_manifest_from_link(const char *title_id, const char *in, char *canon, size_t sz);
/* 1 when found bytes clearly don't match the size the site lists ("71.7 GB", "8.7GB"): probably a link of another version. */
int  upd_site_size_mismatch(const char *label, int64_t found);

#endif
