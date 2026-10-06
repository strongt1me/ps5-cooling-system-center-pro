#!/usr/bin/env python3
"""Derive the tile's sce_sys artwork from the two originals in pkg/.

The PS5 wants exact pixel sizes: a 512x512 icon for the home-screen tile and
a 1920x1080 picture for the background that fills the screen while the tile is
selected. Both originals are close to the right aspect already, so each is
scaled to cover and centre-cropped rather than squashed.

Run from anywhere:  python tools/build_tile_assets.py
"""

import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    sys.exit("Pillow fehlt:  pip install Pillow")

ROOT = Path(__file__).resolve().parent.parent
PKG  = ROOT / "pkg"
OUT  = PKG / "tile" / "sce_sys"

# (source, target name, width, height, vertical anchor)
#
# The anchor decides which end of the overhang is thrown away: 0.0 keeps the
# top, 1.0 keeps the bottom, 0.5 splits it evenly.
#
# The background has been 16:9 since 25.09.2026 (1672x941): a new design with
# the lettering moved down into the middle, where the home screen's tile row no
# longer covers it. Next to nothing is cropped. The anchor stays at 1.0 all the
# same, because the old 3:2 original needed it (background_original_v2_konsole
# .png): split evenly, the crop went straight through the second line of the
# four captions at the bottom. A taller original would need it again.
JOBS = [
    ("icon0_original.png",      "icon0.png",  512,  512, 0.5),
    ("background_original.png", "pic1.png",  1920, 1080, 1.0),
    # pic0 is the smaller still the shell uses in some views; same artwork.
    ("background_original.png", "pic0.png",  1920, 1080, 1.0),
]


def cover(im: Image.Image, w: int, h: int, anchor: float = 0.5) -> Image.Image:
    """Scale to fill w x h, then crop the overhang away at the given anchor."""
    scale = max(w / im.width, h / im.height)
    tmp = im.resize((round(im.width * scale), round(im.height * scale)),
                    Image.LANCZOS)
    left = (tmp.width - w) // 2
    top = round((tmp.height - h) * anchor)
    return tmp.crop((left, top, left + w, top + h))


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    for src_name, dst_name, w, h, anchor in JOBS:
        src = PKG / src_name
        if not src.exists():
            sys.exit(f"Vorlage fehlt: {src}")

        im = Image.open(src).convert("RGB")
        out = cover(im, w, h, anchor)
        dst = OUT / dst_name
        out.save(dst, "PNG", optimize=True)
        print(f"{dst.relative_to(ROOT)}  {w}x{h}  "
              f"{dst.stat().st_size // 1024} KiB")


if __name__ == "__main__":
    main()
