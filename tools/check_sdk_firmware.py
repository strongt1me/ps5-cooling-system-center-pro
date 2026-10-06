#!/usr/bin/env python3
"""Can the start-up code of this SDK (or of this ELF) initialise a given firmware?

Every payload starts in the SDK's crt1.o. Before main() runs, its
__kernel_init() reads the console's firmware version and compares it with the
versions it has kernel offsets for, one case after the other:

    cmpl $0x13600000, %r14d        ; 13.60

A firmware that has no case there makes the start fail before main(), with no
line in any log. SDK v0.41 stops at 13.40; the case for 13.60 came with v0.42,
and v0.43 is what this project is built with.

This script looks for those comparisons in the machine code:

    check_sdk_firmware.py sdk <PS5_PAYLOAD_SDK> [firmware ...]
        the SDK's target/lib/crt1.o: is it new enough to build with?
    check_sdk_firmware.py elf <file.elf> [firmware ...]
        a finished ELF: the CRT is linked into it, so this is what the console
        will run (used after the build, and before a release)

Firmware is written the way people say it: 13.60. A version is found when its
value (BCD, major and minor in the top half: 13.60 is 0x13600000) occurs as the
operand of a compare instruction. A value that occurs only somewhere else in
the file is reported as "not as a comparison" and still counts as found with
a notice, since a future compiler may lay the switch out differently; a value
that does not occur at all fails.

Exit status: 0 all found, 1 something missing or unreadable, 2 wrong usage.

Written for this project (03.10.2026). The idea of checking the CRT for the
13.60 case, and the SDK version that has it, come from PS5 Game Compressor's
build, which does the same with a single byte pattern.
"""

import pathlib
import struct
import sys


def bcd_word(text):
    """'13.60' -> 0x13600000."""
    major, minor = text.split(".")
    if len(major) != 2 or len(minor) != 2:
        raise ValueError(text)
    return (int(major, 16) << 24) | (int(minor, 16) << 16)


def find(data, word):
    """(offset of a compare against word or None, whether the bytes occur at all)."""
    needle = struct.pack("<I", word)
    weak = False
    i = data.find(needle)
    while i != -1:
        weak = True
        # cmp eax, imm32            3D id
        # cmp r/m32 (reg), imm32    81 F8+r id  (REX.B before it for r8d-r15d)
        if i >= 1 and data[i - 1] == 0x3D:
            return i - 1, True
        if i >= 2 and data[i - 2] == 0x81 and 0xF8 <= data[i - 1] <= 0xFF:
            return i - 2, True
        i = data.find(needle, i + 1)
    return None, weak


def main(argv):
    if len(argv) < 3 or argv[1] not in ("sdk", "elf"):
        print(__doc__.split("\n\n")[0], file=sys.stderr)
        print("usage: check_sdk_firmware.py sdk <PS5_PAYLOAD_SDK> [firmware ...]\n"
              "       check_sdk_firmware.py elf <file.elf> [firmware ...]", file=sys.stderr)
        return 2

    mode, target, wanted = argv[1], pathlib.Path(argv[2]), argv[3:]
    path = target / "target" / "lib" / "crt1.o" if mode == "sdk" else target
    try:
        data = path.read_bytes()
        words = [(fw, bcd_word(fw)) for fw in wanted]
    except OSError as exc:
        print(f"Cannot read {path}: {exc}", file=sys.stderr)
        return 1
    except ValueError as exc:
        print(f"Not a firmware version like 13.60: {exc}", file=sys.stderr)
        return 2

    missing = []
    for fw, word in words:
        at, weak = find(data, word)
        if at is not None:
            print(f"  {fw}: found, compared at file offset 0x{at:x}")
        elif weak:
            print(f"  {fw}: found, but not as a comparison - check by hand if this SDK is newer than v0.43")
        else:
            print(f"  {fw}: MISSING")
            missing.append(fw)
    sys.stdout.flush()

    if missing:
        what = "This SDK's CRT" if mode == "sdk" else "This ELF"
        print(f"{what} has no case for firmware {', '.join(missing)}: a console on that "
              "firmware would not get past the start-up code, and nothing would be logged.\n"
              "Build with ps5-payload-sdk v0.43 or newer "
              "(https://github.com/ps5-payload-dev/sdk/releases), "
              "or name the firmware you need with FW_NEED=... when you build for older ones.",
              file=sys.stderr)
        return 1
    if wanted:
        print(f"{path.name}: start-up code covers firmware {', '.join(wanted)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
