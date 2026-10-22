/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Knutwurst (PatchDL 0.3.4: patchdl_prospero.c, manifest_from_link in patchdl_websrv.c), adapted for
 * PS5 Cooling & System Center - Pro
 *
 * The patch history sites: prosperopatches.com (PS5) and orbispatches.com (PS4) list every version of a game's patches.
 * The official servers name only the newest one, so older versions, and the newest one that runs on an older firmware,
 * come from here. Only the list comes from the sites: a package is always fetched from the official servers, after the
 * person has pasted a link of that very package (upd_patchsite_link). */
#ifndef PS5TM_UPD_PATCHSITE_H
#define PS5TM_UPD_PATCHSITE_H

#include <stddef.h>
#include <stdint.h>

#define UPD_PS_MAX 512                 /* more versions than any game has had; the answer is capped at 512 KB */

typedef struct {
  char     version[16];                /* "01.003.000" (PS5) or "01.07" (PS4) */
  char     required_fw[16];            /* "7.01"; "?" when the site gave none; for PS4 a PS4 firmware */
  char     size[16];                   /* the site's label, "71.7 GB" */
  uint32_t fw_bin;                     /* PS5 firmware it needs (0xFFFFFFFF: unknown), 0 for PS4 */
} upd_pver_t;

/* Every version the site lists for title_id (9 characters). 0 with *count entries, or -1 with a short reason in why. */
int  upd_patchsite_versions(const char *title_id, upd_pver_t *out, int max, int *count, char *why, size_t wn);

/* Turns a link copied from the site ("Copy all links" or the SC link) into the URL of the package manifest on the
   official servers. NULL when it is usable, else a reason code: url_invalid, url_not_allowed, not_an_sc_link,
   url_title_mismatch, url_too_long. */
const char *upd_patchsite_link(const char *title_id, const char *in, char *canon, size_t sz);

#endif
