/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 gasb07
 * Firmware version and the registration method chosen from it. */
#pragma once

#include <stddef.h>
#include <stdint.h>

/* How titles are registered by the system, chosen from the firmware at startup:
 *   REG_ALL      sceAppInstUtilAppInstallAll: the system registers every new title under
 *                /user/app. Used from 6.00 on (works on 6.50 and 13.20).
 *   REG_TITLEDIR sceAppInstUtilAppInstallTitleDir, one title at a time (works on 5.50). On
 *                5.50 InstallAll made the system check its whole app database and flag it
 *                as corrupted; below 6.00 the per-title call is used when the firmware has
 *                it (it was removed in newer firmwares: not present on 13.20).
 *   REG_NONE     neither is safe or available: no games are added. */
enum { REG_ALL, REG_TITLEDIR, REG_NONE };

#ifndef MIN_FW_INSTALL_ALL
#define MIN_FW_INSTALL_ALL 6
#endif

/* Major and minor version from kernel_get_fw_version() (BCD: 0x13200006 is 13.20). */
void p4m_fw_decode(uint32_t raw, unsigned *major, unsigned *minor);
/* "13.20" */
void p4m_fw_text(uint32_t raw, char *out, size_t n);
/* Registration method for a firmware major version; has_title_dir: the firmware has
 * sceAppInstUtilAppInstallTitleDir. */
int p4m_fw_reg_mode(unsigned major, int has_title_dir);
