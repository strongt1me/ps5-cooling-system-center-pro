#!/usr/bin/env python3
"""Checks that every loadable segment of a payload ELF starts on a page boundary (16 KiB, the console's).

Usage: check_elf_pages.py <elf-file>

The payload loader maps the segments one by one, by page. A segment that starts in the middle of the page the code lies on
(p_vaddr not a multiple of 0x4000) takes that page over: the page loses its execute right and the process dies with a
SIGSEGV on its first instruction in it. That is what the background-pause helper did on 17.11.2026, built once without
.eh_frame (-fno-asynchronous-unwind-tables) - the linker then packed the second segment right behind the code (0x68b0).
The check runs when the helper is built, so that such an ELF never reaches the app.
"""
import struct
import sys

PAGE = 0x4000
PT_LOAD = 1


def main():
    if len(sys.argv) != 2:
        sys.exit("usage: check_elf_pages.py <elf-file>")
    with open(sys.argv[1], "rb") as f:
        data = f.read()
    if data[:4] != b"\x7fELF" or data[4] != 2:
        sys.exit("check_elf_pages: %s ist kein 64-Bit-ELF" % sys.argv[1])
    phoff, = struct.unpack_from("<Q", data, 32)
    phentsize, phnum = struct.unpack_from("<HH", data, 54)
    bad = []
    for i in range(phnum):
        off = phoff + i * phentsize
        p_type, p_flags, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_align = struct.unpack_from("<IIQQQQQQ", data, off)
        if p_type == PT_LOAD and p_vaddr % PAGE != 0:
            bad.append((i, p_vaddr))
    if bad:
        for i, v in bad:
            sys.stderr.write("check_elf_pages: Segment %d von %s beginnt bei 0x%x, nicht seitenbuendig (0x%x)\n" % (i, sys.argv[1], v, PAGE))
        sys.exit(1)
    print("check_elf_pages: %s: %d Segmente seitenbuendig" % (sys.argv[1], phnum))


if __name__ == "__main__":
    main()
