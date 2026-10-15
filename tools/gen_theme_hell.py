#!/usr/bin/env python3
"""Generates the status text colours of the light design "Hell" in web/style.css (15.10.2026).

The dark designs write warnings, errors and tags in light colours (#ffd57b, #9df89d, #ffb0b0 ...). On the light grey of "Hell"
they would be unreadable, so every rule of web/style.css that sets a light text colour gets a twin for :root[data-theme="hell"]
with a darker colour of the same hue. Texts on the dark pictures of the image tiles (status, fan, running game, banners) keep
their colour: those tiles stay dark in every design.

The result replaces what stands between the two marker comments in web/style.css; run it again after adding light text colours.
usage: python tools/gen_theme_hell.py [web/style.css]
"""
import colorsys
import os
import re
import sys

BEGIN = "/* >>> Hell: Statusfarben (erzeugt von tools/gen_theme_hell.py, nicht von Hand ändern) */"
END = "/* <<< Hell: Statusfarben */"
H = ':root[data-theme="hell"]'


def darker(hexcol):
    """the same hue, dark enough for a light background; None for a colour that is dark already"""
    h = hexcol.lstrip("#")
    if len(h) == 3:
        h = "".join(c * 2 for c in h)
    r, g, b = (int(h[i:i + 2], 16) / 255 for i in (0, 2, 4))
    hh, ll, ss = colorsys.rgb_to_hls(r, g, b)
    if ll < 0.55:
        return None
    ll2 = 0.30 if ss > 0.25 else 0.28
    ss2 = min(max(ss, 0.55), 0.85) if ss > 0.12 else ss
    r2, g2, b2 = colorsys.hls_to_rgb(hh, ll2, ss2)
    return "#%02x%02x%02x" % (round(r2 * 255), round(g2 * 255), round(b2 * 255))


def rules(css):
    css = re.sub(r"/\*.*?\*/", "", css, flags=re.S)
    out = []
    for sel, body in re.findall(r"([^{}]+)\{([^{}]*)\}", css):
        sel = sel.strip()
        if sel.startswith("@") or sel in ("from", "to") or re.match(r"^\d+%", sel) or "data-theme" in sel:
            continue
        m = re.search(r"(?:^|;)\s*color\s*:\s*(#[0-9a-fA-F]{3,6})\b", body)
        if not m or m.group(1).lower() in ("#fff", "#ffffff"):
            continue
        if re.search(r"ov-fan|ov-game|ov-hero|hero|banner|pf-|gauge", sel):
            continue                       # on a dark picture: stays light
        d = darker(m.group(1))
        if d:
            out.append("%s { color: %s; }" % (", ".join("%s %s" % (H, s.strip()) for s in sel.split(",") if s.strip()), d))
    return out


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "web", "style.css")
    raw = open(path, "rb").read().decode("utf-8")
    a, b = raw.find(BEGIN), raw.find(END)
    if a < 0 or b < a:
        sys.exit("gen_theme_hell: die Markierungen fehlen in %s" % path)
    rest = raw[:a] + raw[b + len(END):]
    block = BEGIN + "\n" + "\n".join(rules(rest)) + "\n" + END
    out = raw[:a] + block + raw[b + len(END):]
    if out != raw:
        open(path, "wb").write(out.encode("utf-8"))
    print("gen_theme_hell: %d Textfarben" % (block.count("\n") - 1))


if __name__ == "__main__":
    main()
