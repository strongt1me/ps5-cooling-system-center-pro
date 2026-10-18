#!/usr/bin/env python3
"""Builds the small previews of the built-in profile pictures: web/avatars/t/NN-name.jpg (132 x 132).

Usage: build_avatar_thumbs.py [<web-dir>]     (default: ../web next to this script)

The grid on the Profil page shows 30 pictures at 88 px. Fed with the 440 px files (tools/build_avatars.py) the browser had to
download 1.3 MB and decode 5.8 million pixels at once, which the console's browser did slowly (17.10.2026). The previews are made
from those 440 px files, so no originals are needed; the full file is still what a click on a picture loads and uses. 132 px is
88 px at 1.5 times, sharp enough for a TV and about 6 KB each.

Needs Pillow (pip install pillow). Run it after build_avatars.py; the result is committed like the pictures themselves.
"""
import glob
import os
import sys

from PIL import Image

SIZE = 132
QUALITY = 84

here = os.path.dirname(os.path.abspath(__file__))
web = os.path.abspath(sys.argv[1]) if len(sys.argv) > 1 else os.path.join(here, "..", "web")
src = os.path.join(web, "avatars")
dst = os.path.join(src, "t")
os.makedirs(dst, exist_ok=True)

files = sorted(glob.glob(os.path.join(src, "*.jpg")))
if not files:
    sys.exit("no pictures in " + src)
total = 0
for f in files:
    im = Image.open(f).convert("RGB")
    if im.size[0] != im.size[1]:
        sys.exit("not square: " + f)
    out = os.path.join(dst, os.path.basename(f))
    im.resize((SIZE, SIZE), Image.LANCZOS).save(out, "JPEG", quality=QUALITY, optimize=True, progressive=True)
    total += os.path.getsize(out)
print("%d previews, %d x %d px, %.0f KB together -> %s" % (len(files), SIZE, SIZE, total / 1024, dst))
