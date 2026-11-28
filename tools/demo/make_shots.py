#!/usr/bin/env python3
"""Photographs the pages of the web interface for the manual, from the demo mode (21.11.2026): docs/shots/<lang>/NN-name.png, 1500 x 937.

    python tools/demo/make_shots.py [lang ...] [--out docs/shots] [--only 03,04]

Needs Python with playwright and Microsoft Edge (playwright uses the installed Edge, nothing is downloaded). For every language it starts the demo server
(tools/demo/demo_server.py, invented data, no console), opens the pages like a person would, and saves what is on the screen."""
import argparse, json, os, subprocess, sys, time, urllib.request

from playwright.sync_api import sync_playwright

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
LANGS = ["de", "en", "es", "fr", "it", "ru", "sv"]
PORT = 8099


def server(lang):
    p = subprocess.Popen([sys.executable, "-X", "utf8", os.path.join(HERE, "demo_server.py"), "--port", str(PORT), "--lang", lang],
                         stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    for _ in range(50):
        try:
            urllib.request.urlopen("http://127.0.0.1:%d/api/v1/status" % PORT, timeout=1).read()
            return p
        except Exception:
            time.sleep(0.2)
    p.kill()
    raise SystemExit("the demo server did not start")


def shots(lang, out, only):
    base = "http://127.0.0.1:%d/index.html" % PORT
    fx = json.load(open(os.path.join(HERE, "fixtures", "library.json"), encoding="utf-8"))
    plan = json.load(open(os.path.join(HERE, "fixtures", "library_convert_plan.json"), encoding="utf-8"))
    ids = [g["title_id"] for g in fx["games"]]
    conv_id = plan.get("title_id") if plan.get("title_id") in ids else ids[0]
    os.makedirs(out, exist_ok=True)
    with sync_playwright() as p:
        b = p.chromium.launch(channel="msedge", headless=True)
        ctx = b.new_context(viewport={"width": 1500, "height": 937}, locale={"de": "de-DE", "en": "en-GB"}.get(lang, lang))
        ctx.add_init_script("try{localStorage.setItem('lang','%s');localStorage.setItem('theme','standard');localStorage.setItem('gmview','classic')}catch(e){}" % lang)
        page = ctx.new_page()

        def go(sel, wait=1800):
            page.click(sel)
            page.wait_for_timeout(wait)

        def snap(num, name):
            if only and num not in only:
                return
            page.wait_for_timeout(600)
            page.screenshot(path=os.path.join(out, "%s-%s.png" % (num, name)))
            print("  %s-%s" % (num, name))

        page.goto(base, wait_until="load")
        page.wait_for_timeout(5000)
        snap("01", "kuehlung")
        page.evaluate("document.querySelector('#mic-button-status').scrollIntoView({block:'center'})")
        snap("02", "kuehlung-betriebsart")
        go('button.tab[data-page="games"]', 3000)
        snap("03", "spiele-bibliothek")
        page.click('button[data-convert="%s"]' % conv_id)
        page.wait_for_timeout(2500)
        snap("04", "spiele-konvertieren")
        page.click('button[data-cp="close"]')
        page.wait_for_timeout(600)
        go("#gm-tab-time", 2500)
        snap("05", "spiele-spielzeit")
        go("#gm-tab-saves", 2500)
        snap("06", "spiele-spielstaende")
        go("#gm-tab-pkg", 2500)
        snap("07", "spiele-pakete")
        go("#gm-tab-bk", 2500)
        snap("08", "spiele-sicherungen")
        go('button.tab[data-page="payloads"]', 2500)
        snap("09", "payloads")
        go('button.tab[data-page="profile"]', 2500)
        snap("10", "profil")
        go('button.tab[data-page="system"]', 2500)
        snap("11", "system")
        go('button.tab[data-page="log"]', 1500)
        go("#log-tab-klog", 2500)
        snap("14", "kernel-log-live")
        go('button.tab[data-page="files"]', 2500)
        snap("15", "dateien")
        go('button.tab[data-page="credits"]', 2500)
        snap("16", "credits")
        b.close()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("langs", nargs="*", default=LANGS)
    ap.add_argument("--out", default=os.path.join(REPO, "docs", "shots"))
    ap.add_argument("--only", default="")
    a = ap.parse_args()
    only = set(x for x in a.only.split(",") if x)
    for lang in a.langs:
        print(lang)
        srv = server(lang)
        try:
            shots(lang, os.path.join(a.out, lang), only)
        finally:
            srv.kill()
            err = srv.stderr.read().decode("utf-8", "replace") if srv.stderr else ""
            if err.strip():
                print("  demo server said:\n    " + "\n    ".join(sorted(set(err.strip().splitlines()))))


if __name__ == "__main__":
    main()
