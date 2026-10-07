#!/usr/bin/env python3
"""Builds the user manual and the FAQ in every language that has a docs_src.<lang>.json, as
  full    HTML with the screenshots built in (and the PDFs made from them): "Handbuch und FAQ" folder
  app     HTML without screenshots, small, for the app itself: web/handbuch[.<lang>].html and web/faq[.<lang>].html
usage: python tools/i18n/docs_build.py [full|app|both] [lang ...] [--out DIR] [--shots DIR]
       default: both, every language found; --out for "full" (default build/docs), --shots = folder with one subfolder of
       screenshots per language (default docs/shots; a missing picture is left out, "app" never has pictures)
The words live in docs/docs_src.<lang>.json, the look in docs/docs_style.css; {version} and {n} are filled in here.
"app" writes web/handbuch[.<lang>].html and web/faq[.<lang>].html (embedded into the ELF by tools/gen_assets.py)."""
import base64
import io
import os
import re
import sys
import json

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
SRC_DIR = os.path.join(HERE, "docs")
OUT = os.path.join(REPO, "build", "docs")
SHOTS = os.path.join(REPO, "docs", "shots")
VERSION = open(os.path.join(REPO, "src", "ps5tm.h"), encoding="utf-8").read().split('PS5TM_VERSION        "')[1].split('"')[0]
CSS = open(os.path.join(SRC_DIR, "docs_style.css"), encoding="utf-8").read()
CSS += """
.back{position:sticky;top:0;z-index:5;display:inline-block;margin:0 0 10px;padding:9px 16px;border-radius:999px;
background:rgba(20,27,41,.92);border:1px solid rgba(59,130,246,.55);color:#cfe0ff;text-decoration:none;font-weight:600}
.back:hover{background:rgba(59,130,246,.25)}
@media print{.back,.langs{display:none}}
.langs{display:inline-flex;flex-wrap:wrap;gap:6px;margin:0 0 10px 10px;vertical-align:top}
.langs a{padding:9px 12px;border-radius:999px;border:1px solid rgba(255,255,255,.14);color:#aebbd4;text-decoration:none;font-size:14px}
.langs a:hover{border-color:rgba(59,130,246,.6);color:#fff}.langs a.on{background:rgba(59,130,246,.22);color:#fff;border-color:rgba(59,130,246,.55)}
"""
LANGS = ["de", "en", "it", "es", "fr", "ru"]
LANG_NAMES = {"de": "Deutsch", "en": "English", "it": "Italiano", "es": "Español", "fr": "Français", "ru": "Русский"}
APP_LOGO = "/img/app-mark.png"      # inside the app the logo is a file of the app, not a copy in every page


def shots_dir(lang):
    d = os.path.join(SHOTS, lang)
    return d if os.path.isdir(d) else os.path.join(SHOTS, "de")


def img_data(lang, name, width=1500):
    p = os.path.join(shots_dir(lang), name)
    if not os.path.exists(p):
        return None
    im = Image.open(p).convert("RGB")
    im = im.resize((width, int(im.size[1] * width / im.size[0])), Image.LANCZOS)
    b = io.BytesIO()
    im.save(b, "JPEG", quality=80, optimize=True, progressive=True)
    return "data:image/jpeg;base64," + base64.b64encode(b.getvalue()).decode()


def logo():
    im = Image.open(os.path.join(REPO, "web", "img", "app-mark.png")).convert("RGBA")
    im.thumbnail((256, 256))
    b = io.BytesIO()
    im.save(b, "PNG", optimize=True)
    return "data:image/png;base64," + base64.b64encode(b.getvalue()).decode()


def fill(s, **kw):
    s = s.replace("{version}", VERSION)
    for k, v in kw.items():
        s = s.replace("{%s}" % k, str(v))
    return s


def esc_attr(s):
    return s.replace("&", "&amp;").replace('"', "&quot;").replace("<", "&lt;")


def page(lang, title, body, ui, mode, kind):
    back = "<a class='back' href='/'>%s</a>" % fill(ui["back"]) if mode == "app" else ""
    if mode == "app":     # the same text in the other languages
        back += "<span class='langs'>%s</span>" % "".join(
            "<a href='/%s%s.html'%s>%s</a>" % (kind, "" if l == "de" else "." + l, " class='on'" if l == lang else "", LANG_NAMES[l]) for l in LANGS)
    css = CSS + (".cover{min-height:0;padding:28px 0 12px}" if mode == "app" else "")   # in the app the cover is not a whole page
    return ("<!doctype html><html lang='%s'><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,"
            "initial-scale=1'><title>%s</title><style>%s</style></head><body><div class='wrap'>%s%s<div class='foot'>%s</div></div></body></html>"
            % (lang, fill(title).replace("&amp;", "&").replace("&", "&amp;"), css, back, body, fill(ui["footer"])))


def cover(ui, kicker, title, lead, badges, logo_uri):
    return ("<header class='cover'><img class='mark' src='%s' alt=''><div class='kicker'>%s</div><h1>%s</h1>"
            "<p class='lead'>%s</p><div class='badges'>%s</div><div class='psrow'><span class='tri'>△</span>"
            "<span class='cir'>○</span><span class='crs'>✕</span><span class='sqr'>□</span></div></header>"
            % (logo_uri, fill(kicker), fill(title), fill(lead), "".join("<span class='badge'>%s</span>" % fill(b) for b in badges)))


def figure(lang, name, captions, mode):
    if mode == "app":
        return ""
    cap = captions.get(name, "")
    if img_data(lang, name, 8) is None:
        return ""
    return "<figure><img src='%s' alt='%s'><figcaption>%s</figcaption></figure>" % (img_data(lang, name), esc_attr(cap), cap)


def build_manual(lang, d, mode, logo_uri):
    ui = d["ui"]
    logo_uri = APP_LOGO if mode == "app" else logo_uri
    chapters = ""
    for i, c in enumerate(d["chapters"]):
        html = re.sub(r"\{\{FIG:([^}]+)\}\}", lambda m: figure(lang, m.group(1), d["captions"], mode), fill(c["html"]))
        chapters += ("<section class='ch' id='%s'><div class='ch-head'><div class='ch-num'>%d</div><h2>%s</h2></div>%s</section>"
                     % (c["id"], i + 1, fill(c["title"]), html))
    toc = "<nav class='toc'><h2>%s</h2><ol>%s</ol></nav>" % (fill(ui["toc"]), "".join(
        "<li><a href='#%s'>%s</a></li>" % (c["id"], fill(c["title"])) for c in d["chapters"]))
    body = cover(ui, ui["manual_kicker"], ui["manual_cover_title"], ui["manual_lead"], ui["manual_badges"], logo_uri) + toc + chapters
    return page(lang, ui["manual_title"], body, ui, mode, "handbuch")


def build_faq(lang, d, mode, logo_uri):
    ui = d["ui"]
    logo_uri = APP_LOGO if mode == "app" else logo_uri
    n = sum(len(c["items"]) for c in d["faq"])
    body = ""
    for c in d["faq"]:
        body += "<div class='faq-cat'>%s</div>" % fill(c["cat"])
        body += "".join("<details open><summary>%s</summary><div class='a'>%s</div></details>" % (fill(i["q"]), fill(i["a"])) for i in c["items"])
    cov = cover(ui, ui["faq_kicker"], ui["faq_cover_title"], ui["faq_lead"], [fill(b, n=n) for b in ui["faq_badges"]], logo_uri)
    return page(lang, ui["faq_title"], cov + body, ui, mode, "faq")


def names(lang, mode):
    if mode == "app":
        suf = "" if lang == "de" else "." + lang
        return os.path.join(REPO, "web", "handbuch%s.html" % suf), os.path.join(REPO, "web", "faq%s.html" % suf)
    if lang == "de":
        return os.path.join(OUT, "Benutzerhandbuch.html"), os.path.join(OUT, "FAQ.html")
    return os.path.join(OUT, "Manual_%s.html" % lang), os.path.join(OUT, "FAQ_%s.html" % lang)


def main():
    global OUT, SHOTS
    args = sys.argv[1:]
    for flag in ("--out", "--shots"):
        if flag in args:
            i = args.index(flag)
            if flag == "--out":
                OUT = os.path.abspath(args[i + 1])
            else:
                SHOTS = os.path.abspath(args[i + 1])
            del args[i:i + 2]
    modes = ["full", "app"]
    if args and args[0] in ("full", "app", "both"):
        modes = ["full", "app"] if args[0] == "both" else [args[0]]
        args = args[1:]
    langs = args or [l for l in LANGS if os.path.exists(os.path.join(SRC_DIR, "docs_src.%s.json" % l))]
    os.makedirs(OUT, exist_ok=True)
    logo_uri = logo()
    made = []
    for lang in langs:
        d = json.load(open(os.path.join(SRC_DIR, "docs_src.%s.json" % lang), encoding="utf-8"))
        for mode in modes:
            m_path, f_path = names(lang, mode)
            open(m_path, "w", encoding="utf-8").write(build_manual(lang, d, mode, logo_uri))
            open(f_path, "w", encoding="utf-8").write(build_faq(lang, d, mode, logo_uri))
            made += [m_path, f_path]
            print("%s %-4s %6d KB + %5d KB" % (lang, mode, os.path.getsize(m_path) // 1024, os.path.getsize(f_path) // 1024), flush=True)
    print("%d Dateien geschrieben" % len(made))


if __name__ == "__main__":
    main()
