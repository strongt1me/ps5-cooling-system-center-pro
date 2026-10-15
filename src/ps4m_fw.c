/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 gasb07
 * Firmware version and the registration method chosen from it. */
#include "ps4m_fw.h"

#include <stdio.h>

void p4m_fw_decode(uint32_t raw, unsigned *major, unsigned *minor) {
    *major = ((raw >> 28) & 0xf) * 10 + ((raw >> 24) & 0xf);
    *minor = ((raw >> 20) & 0xf) * 10 + ((raw >> 16) & 0xf);
}

void p4m_fw_text(uint32_t raw, char *out, size_t n) {
    unsigned major, minor;
    p4m_fw_decode(raw, &major, &minor);
    snprintf(out, n, "%u.%02u", major, minor);
}

int p4m_fw_reg_mode(unsigned major, int has_title_dir) {
    if (major >= MIN_FW_INSTALL_ALL)
        return REG_ALL;
    return has_title_dir ? REG_TITLEDIR : REG_NONE;
}
