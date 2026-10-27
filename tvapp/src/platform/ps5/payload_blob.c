/* ps5cc - The app's ELF inside the tile, sent to the ELF loader when the app is not running.
 * Copyright (C) 2026 strongt1me
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * build/gen/ps5cc_payload.inc is written by `make payload` (Makefile, PAYLOAD_ELF): it names the
 * file by its absolute path and carries its SHA-256, so a new ELF also changes this file's input
 * and the blob is rebuilt. */

#include "ps5cc_payload.inc"

__asm__(".section .rodata\n"
        ".global ps5cc_payload_elf\n"
        ".p2align 4\n"
        "ps5cc_payload_elf:\n"
        ".incbin \"" PS5CC_PAYLOAD_PATH "\"\n"
        ".global ps5cc_payload_elf_end\n"
        "ps5cc_payload_elf_end:\n"
        ".previous\n");
