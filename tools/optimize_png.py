#!/usr/bin/env python3
"""Makes PNG files smaller without changing a single pixel: python tools/optimize_png.py <file or folder>... [--dry]

The compressed image data (IDAT) is deflated again with zopfli (pip install zopfli), which finds a smaller stream for the same
scanlines, and text, time and EXIF chunks are dropped (colour chunks - iCCP, sRGB, gAMA, cHRM, PLTE, tRNS - stay). A file is
replaced only when the result is smaller AND decodes to exactly the same pixels (checked with Pillow). The ELF carries these pictures
(web/img, the tile in tile/sce_sys) byte for byte, so every byte saved here is a byte less in the payload (06.11.2026)."""
import os
import struct
import sys
import zlib

try:
    import zopfli.zlib as zz
    from PIL import Image
except ImportError:
    sys.exit("optimize_png: pip install zopfli pillow")

DROP = {b"tEXt", b"iTXt", b"zTXt", b"tIME", b"eXIf"}


def chunks(data):
    pos = 8
    while pos < len(data):
        n = struct.unpack(">I", data[pos:pos + 4])[0]
        yield data[pos + 4:pos + 8], data[pos + 8:pos + 8 + n]
        pos += 12 + n


def chunk(typ, body):
    return struct.pack(">I", len(body)) + typ + body + struct.pack(">I", zlib.crc32(typ + body))


def optimize(data):
    out, idat = [data[:8]], b""
    wrote = False
    for typ, body in chunks(data):
        if typ == b"IDAT":
            idat += body
            continue
        if idat and not wrote:
            out.append(chunk(b"IDAT", zz.compress(zlib.decompress(idat), numiterations=15)))
            wrote = True
        if typ in DROP:
            continue
        out.append(chunk(typ, body))
    return b"".join(out)


def same_pixels(a, b):
    import io
    ia, ib = Image.open(io.BytesIO(a)), Image.open(io.BytesIO(b))
    return ia.size == ib.size and ia.mode == ib.mode and ia.tobytes() == ib.tobytes()


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    dry = "--dry" in sys.argv
    files = []
    for a in args:
        if os.path.isdir(a):
            for r, _, names in os.walk(a):
                files += [os.path.join(r, n) for n in names if n.lower().endswith(".png")]
        else:
            files.append(a)
    before = after = 0
    for f in sorted(files):
        d = open(f, "rb").read()
        if d[:8] != b"\x89PNG\r\n\x1a\n":
            continue
        try:
            n = optimize(d)
        except (zlib.error, struct.error):
            print("skipped (unreadable):", f)
            continue
        before += len(d)
        if len(n) < len(d) and same_pixels(d, n):
            after += len(n)
            print("%7d -> %7d  %s" % (len(d), len(n), f))
            if not dry:
                open(f, "wb").write(n)
        else:
            after += len(d)
    print("total %d -> %d (%d saved)" % (before, after, before - after))


if __name__ == "__main__":
    main()
