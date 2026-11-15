#!/usr/bin/env python3
"""Makes Kraken Mount v0.2.0 and later not wait for ShadowMountPlus (PS5 Cooling & System Center - Pro).

usage: patch_kraken.py <kraken-mount.elf> <out.elf>

Since v0.2.0 Kraken Mount waits at start (src/main.rs wait_ready) until kstuff is loaded AND no process named "shadowmountplus.elf" runs, and
mounts nothing before. This app runs next to ShadowMountPlus on purpose (separate search folders, see FPKGMount in the manual), as it did with
v0.0.1-beta. The name of the process is a string constant of 19 bytes in the program; its last byte is changed ("shadowmountplus.elf" ->
"shadowmountplus.elX"), so that no process has that name and the wait for ShadowMountPlus ends at once. The wait for kstuff stays. Nothing else
of the program changes (same length, same layout). The same change is made to a downloaded newer version before it is stored (src/kraken.c,
patch_smp_wait). Kraken Mount is GPL-3.0-or-later (quer3q); this is a modified copy: see third_party/kraken_mount/ORIGIN.md.
"""
import sys

NAME = b"shadowmountplus.elf"
PATCHED = b"shadowmountplus.elX"

def main():
    src, dst = sys.argv[1], sys.argv[2]
    data = open(src, "rb").read()
    n = data.count(NAME)
    if n == 0:
        # an upstream version without the wait: nothing to change
        print("patch_kraken: no process name found in %s, copied unchanged" % src)
    else:
        data = data.replace(NAME, PATCHED)
        print("patch_kraken: %d place(s) changed in %s" % (n, src))
    open(dst, "wb").write(data)

if __name__ == "__main__":
    main()
