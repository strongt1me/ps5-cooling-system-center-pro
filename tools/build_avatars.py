#!/usr/bin/env python3
"""Builds the profile pictures that ship inside the app (web/avatars/).

Usage: build_avatars.py <source-dir> [<web-dir>]

  source-dir  the original pictures, one file each, named NN-name.png
              (square, at least 440 px; the originals are 1024 px, ~2.5 MB each)
  web-dir     the web/ folder to write into (default: ../web next to this script)

Writes web/avatars/NN-name.jpg (440 x 440, progressive JPEG) and
web/avatars/index.json (what the Profil page lists). tools/gen_assets.py then
embeds both like every other file under web/.

Why so small: everything under web/ becomes part of the ELF and stays in memory
while the app runs. The app is about 3 MB; one build grew to 10.5 MB and the
console then had too little memory left to serve a single request (see the
note above TILE_PKG in the Makefile). Thirty pictures at 440 px cost about
1.3 MB. 440 px is also the largest size the page ever makes of a profile
picture (four textures of 64/128/260/440 px and a 440 px PNG), so nothing is
lost by not embedding the 1024 px originals.

Needs Pillow (pip install pillow). Run it whenever the pictures change. The
result is committed, the originals are not (75 MB).
"""
import glob
import json
import os
import re
import sys

from PIL import Image

SIZE = 440
QUALITY = 82
# The generator behind the originals leaves a one-pixel border that is slightly
# transparent. Scaled down it would show as a dark hairline, so it is cut off.
EDGE = 2
# What a picture with real transparency is laid on (the page's own dark base).
BASE = (11, 18, 32)
# Guard against bloating the ELF by accident (see above).
MAX_TOTAL = 2 * 1024 * 1024

# German names for the page; a picture that is not listed here gets its file
# name, capitalised.
LABELS = {
    "neon-portraet": "Neon-Porträt",
    "neon-fuchs": "Neon-Fuchs",
    "neon-roboter": "Neon-Roboter",
    "neon-pilz": "Neon-Pilz",
    "blau-astronaut": "Astronaut",
    "blau-katze": "Katze",
    "blau-wolf": "Wolf",
    "blau-samurai": "Samurai",
    "blau-drache": "Drache",
    "blau-eule": "Eule",
    "blau-gamer": "Gamer",
    "blau-panda": "Panda",
    "blau-loewe": "Löwe",
    "blau-cyber-schaedel": "Cyber-Schädel",
    "blau-raumfahrer": "Raumfahrerin",
    "blau-katana": "Katana",
    "blau-krake": "Krake",
    "blau-biene": "Biene",
    "blau-wesen": "Wesen",
    "blau-planet": "Planet",
    "blau-controller": "Controller",
    "blau-kopfhoerer": "Kopfhörer",
    "blau-ninja": "Ninja",
    "blau-ritterhelm": "Ritterhelm",
    "blau-tiger": "Tiger",
    "blau-phoenix": "Phönix",
    "blau-schlange": "Schlange",
    "blau-schmetterling": "Schmetterling",
    "blau-kolibri": "Kolibri",
    "blau-qualle": "Qualle",
}


def label_for(stem):
    key = re.sub(r"^\d+-", "", stem)
    return LABELS.get(key) or key.replace("-", " ").capitalize()


def convert(path):
    im = Image.open(path)
    im.load()
    w, h = im.size
    if min(w, h) < SIZE + 2 * EDGE:
        raise SystemExit("%s: %d x %d is too small (need at least %d px)"
                         % (path, w, h, SIZE + 2 * EDGE))
    side = min(w, h)
    left, top = (w - side) // 2, (h - side) // 2
    im = im.crop((left + EDGE, top + EDGE, left + side - EDGE, top + side - EDGE))
    if im.mode in ("RGBA", "LA", "PA") or "transparency" in im.info:
        im = im.convert("RGBA")
        if im.getchannel("A").getextrema()[0] < 250:
            base = Image.new("RGBA", im.size, BASE + (255,))
            base.alpha_composite(im)
            im = base
    return im.convert("RGB").resize((SIZE, SIZE), Image.LANCZOS)


def main():
    if len(sys.argv) not in (2, 3):
        sys.exit(__doc__)
    src = sys.argv[1]
    web = sys.argv[2] if len(sys.argv) == 3 else os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "..", "web")
    out = os.path.join(web, "avatars")

    files = sorted(glob.glob(os.path.join(src, "*.png")))
    if not files:
        sys.exit("no *.png in %s" % src)

    os.makedirs(out, exist_ok=True)
    for old in glob.glob(os.path.join(out, "*.jpg")) + glob.glob(os.path.join(out, "index.json")):
        os.remove(old)

    images, total = [], 0
    for f in files:
        stem = os.path.splitext(os.path.basename(f))[0]
        dst = os.path.join(out, stem + ".jpg")
        convert(f).save(dst, "JPEG", quality=QUALITY, optimize=True,
                        progressive=True, subsampling=2)
        size = os.path.getsize(dst)
        total += size
        images.append({"file": stem + ".jpg", "label": label_for(stem)})
        print("%-28s %5.1f KB" % (stem + ".jpg", size / 1024))

    if total > MAX_TOTAL:
        sys.exit("build_avatars: %d KB in total is over the %d KB limit — the "
                 "pictures would bloat the ELF" % (total // 1024, MAX_TOTAL // 1024))

    with open(os.path.join(out, "index.json"), "w", encoding="utf-8", newline="\n") as fh:
        json.dump({"size": SIZE, "images": images}, fh, ensure_ascii=False, indent=1)
        fh.write("\n")
    print("build_avatars: %d pictures, %.0f KB -> %s" % (len(images), total / 1024, out))


if __name__ == "__main__":
    main()
