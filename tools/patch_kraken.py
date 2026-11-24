#!/usr/bin/env python3
"""Makes Kraken Mount v0.2.0 and later not wait for ShadowMountPlus, and v0.3.2 and later not offer its own update (PS5 Cooling & System
Center - Pro).

usage: patch_kraken.py <kraken-mount.elf> <out.elf>

1. Since v0.2.0 Kraken Mount waits at start (src/main.rs wait_ready) until kstuff is loaded AND no process named "shadowmountplus.elf" runs,
   and mounts nothing before. This app runs next to ShadowMountPlus on purpose (separate search folders, see FPKGMount in the manual), as it
   did with v0.0.1-beta. The name of the process is a string constant of 19 bytes in the program; its last byte is changed
   ("shadowmountplus.elf" -> "shadowmountplus.elX"), so that no process has that name and the wait for ShadowMountPlus ends at once. The
   wait for kstuff stays.
2. Since v0.3.2 Kraken Mount asks GitHub at every start for a newer release of itself (src/update.rs RELEASE_URL) and its own page then
   offers "Update", which downloads the release ELF and sends it to the loader. That copy is not changed as in 1: it takes the port over
   from this one and then waits for ShadowMountPlus, so nothing mounts while ShadowMountPlus runs. The first byte of that one URL is
   changed ("https://..." -> "xttps://..."): the console's HTTP library refuses the unknown scheme at once (no request leaves the console),
   no newer release is found, the page offers no update. A newer version comes through the app's own button, which makes both changes
   (src/kraken.c). The ampr-emu-ng update of Kraken Mount has a URL of its own and is not touched.

Nothing else of the program changes (same length, same layout). The program carries itself twice (stage 2 with stage 1 inside, for its
self copy), so each text is found twice. Kraken Mount is GPL-3.0-or-later (quer3q); this is a modified copy: see
third_party/kraken_mount/ORIGIN.md.
"""
import sys

CHANGES = [
    (b"shadowmountplus.elf", b"shadowmountplus.elX", "process name of ShadowMountPlus (no wait for it)"),
    (b"https://api.github.com/repos/quer3q/kraken-mount/releases/latest",
     b"xttps://api.github.com/repos/quer3q/kraken-mount/releases/latest", "own update check (no update offered)"),
]


def main():
    src, dst = sys.argv[1], sys.argv[2]
    data = open(src, "rb").read()
    for old, new, what in CHANGES:
        assert len(old) == len(new)
        n = data.count(old)
        if n == 0:
            # an upstream version without it: nothing to change
            print("patch_kraken: %s: not found in %s, unchanged" % (what, src))
        else:
            data = data.replace(old, new)
            print("patch_kraken: %s: %d place(s) changed in %s" % (what, n, src))
    open(dst, "wb").write(data)


if __name__ == "__main__":
    main()
