#!/usr/bin/env python3
"""The pictures of the READMEs from the demo mode (28.11.2026): docs/images/*.jpg (German, README.de.md) and docs/images/en/*.jpg
(English, README.md and the other languages).

    python tools/demo/make_readme_images.py [de] [en] [--tv-de DIR] [--tv-en DIR] [--only kuehlung,spiele]

The browser pictures (1600 x 1000, the phone one 750 x 1624) are taken like tools/demo/make_shots.py takes the manual's: the demo server
answers with invented data, Edge (through playwright) shows the pages. The five pictures of the fullscreen app (1600 x 900) come from its
host snapshots (tools/host-snapshots.sh in the WSL build tree, 1920 x 1080 PNG, made with PS5CC_LANG=de or en); without --tv-de/--tv-en
they are left as they are; tools/demo/tv_readme_snapshots.sh makes them (with invented game titles). Nothing of a console is used.
Needs Python with playwright and Pillow, and Microsoft Edge."""
import argparse, io, os, sys

from PIL import Image
from playwright.sync_api import sync_playwright

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from make_shots import PORT, server  # noqa: E402  (the same demo server, started per language)

REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
OUT = {"de": os.path.join(REPO, "docs", "images"), "en": os.path.join(REPO, "docs", "images", "en")}
LOCALE = {"de": "de-DE", "en": "en-GB"}
# README picture <- host snapshot of the fullscreen app
TV = {"vollbild-kuehlung": "01-ps5cc-kuehlung.png", "vollbild-spiele": "01-ps5cc-spiele.png", "vollbild-dateien": "01-ps5cc-dateien-ordner.png",
      "vollbild-payloads": "01-ps5cc-payloads-laufend.png", "vollbild-spielzeit": "01-ps5cc-spielzeit.png"}


def save_jpg(png_bytes_or_path, dest, size=None):
    im = Image.open(png_bytes_or_path).convert("RGB")
    if size and im.size != size:
        im = im.resize(size, Image.LANCZOS)
    im.save(dest, "JPEG", quality=86, optimize=True)
    print("  %s (%d x %d, %d KB)" % (os.path.relpath(dest, REPO), im.size[0], im.size[1], os.path.getsize(dest) // 1024))


def browser(lang, only):
    base = "http://127.0.0.1:%d/index.html" % PORT
    out = OUT[lang]

    def want(name):
        return not only or name in only

    with sync_playwright() as p:
        b = p.chromium.launch(channel="msedge", headless=True)
        init = "try{localStorage.setItem('lang','%s');localStorage.setItem('theme','standard');localStorage.setItem('gmview','%%s')}catch(e){}" % lang

        def context(view, **kw):
            ctx = b.new_context(locale=LOCALE[lang], **kw)
            ctx.add_init_script(init % view)
            return ctx

        page = context("classic", viewport={"width": 1600, "height": 1000}).new_page()

        def go(sel, wait=2500):
            page.click(sel)
            page.wait_for_timeout(wait)

        def snap(name):
            if want(name):
                page.wait_for_timeout(700)
                save_jpg(io.BytesIO(page.screenshot()), os.path.join(out, name + ".jpg"))

        page.goto(base, wait_until="load")
        page.wait_for_timeout(5000)
        snap("kuehlung")
        page.evaluate("document.querySelector('#fc-card').scrollIntoView({block:'start'}); window.scrollBy(0, -16)")
        snap("kuehlung-diagramme")
        page.evaluate("window.scrollTo(0, 0)")
        go('button.tab[data-page="games"]', 3500)
        snap("spiele-gross")
        go("#gm-tab-time")
        snap("spielzeit")
        go("#gm-tab-saves")
        snap("spielstaende")
        go("#gm-tab-pkg")
        snap("pakete")
        go("#gm-tab-p4m")
        snap("ps4-von-usb")
        go('button.tab[data-page="files"]')
        snap("dateimanager")
        go('button.tab[data-page="payloads"]')
        snap("payloads")
        go('button.tab[data-page="system"]')
        snap("system")
        go('button.tab[data-page="profile"]')
        snap("profil")
        go('button.tab[data-page="log"]')
        snap("protokoll")

        if want("spiele"):              # the compact view of the library
            page = context("compact", viewport={"width": 1600, "height": 1000}).new_page()
            page.goto(base, wait_until="load")
            page.wait_for_timeout(4000)
            go('button.tab[data-page="games"]', 3500)
            snap("spiele")
        if want("kuehlung-mobil"):      # a phone: 375 x 812 points, twice the pixels
            page = context("classic", viewport={"width": 375, "height": 812}, device_scale_factor=2, is_mobile=True, has_touch=True).new_page()
            page.goto(base, wait_until="load")
            page.wait_for_timeout(5000)
            snap("kuehlung-mobil")
        b.close()


def fullscreen(lang, tvdir, only):
    for name, snap in TV.items():
        if only and name not in only:
            continue
        src = os.path.join(tvdir, snap)
        if not os.path.exists(src):
            raise SystemExit("missing host snapshot: %s" % src)
        save_jpg(src, os.path.join(OUT[lang], name + ".jpg"), (1600, 900))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("langs", nargs="*", default=["de", "en"])
    ap.add_argument("--tv-de", default="")
    ap.add_argument("--tv-en", default="")
    ap.add_argument("--only", default="")
    a = ap.parse_args()
    only = set(x for x in a.only.split(",") if x)
    for lang in a.langs:
        if lang not in OUT:
            raise SystemExit("languages: de, en")
        print(lang)
        os.makedirs(OUT[lang], exist_ok=True)
        if not only or only - set(TV):
            srv = server(lang)
            try:
                browser(lang, only)
            finally:
                srv.kill()
                err = srv.stderr.read().decode("utf-8", "replace") if srv.stderr else ""
                if err.strip():
                    print("  demo server said:\n    " + "\n    ".join(sorted(set(err.strip().splitlines()))))
        tv = {"de": a.tv_de, "en": a.tv_en}[lang]
        if tv:
            fullscreen(lang, tv, only)


if __name__ == "__main__":
    main()
