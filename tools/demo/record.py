#!/usr/bin/env python3
"""Records the answers of a console for the demo mode (21.11.2026): opens the pages of the web interface like a person would and keeps every GET answer
of /api/v1/ as a file in <raw dir> (named by the route, e.g. _api_v1_library.json).  READ-ONLY: it only opens pages and the window "Konvertieren" of one game,
it never presses a button that does something.

    python tools/demo/record.py http://<console>:8086 [<raw dir>]       (default raw dir: tools/demo/raw, not in the repository)
    python tools/demo/make_fixtures.py <raw dir>                        makes tools/demo/fixtures from it, with every personal value replaced

Needs Python with playwright and Microsoft Edge. The recordings hold the real data of the console (games, account, network): keep them to yourself."""
import json, os, re, sys

from playwright.sync_api import sync_playwright

BASE = sys.argv[1] if len(sys.argv) > 1 else "http://192.168.1.50:8086"
HERE = os.path.dirname(os.path.abspath(__file__))
RAW = sys.argv[2] if len(sys.argv) > 2 else os.path.join(HERE, "raw")


def main():
    os.makedirs(RAW, exist_ok=True)
    with sync_playwright() as p:
        b = p.chromium.launch(channel="msedge", headless=True)
        page = b.new_context(viewport={"width": 1500, "height": 937}).new_page()

        def on_resp(r):
            if "/api/v1/" in r.url and r.request.method == "GET" and "json" in r.headers.get("content-type", ""):
                try:
                    j = json.loads(r.body().decode("utf-8"))
                except Exception:
                    return
                name = re.sub(r"[^A-Za-z0-9]+", "_", r.url.replace(BASE, ""))[:90]
                open(os.path.join(RAW, name + ".json"), "w", encoding="utf-8").write(json.dumps(j))

        page.on("response", on_resp)

        def step(name, fn):
            try:
                fn()
            except Exception as e:
                print("step", name, "failed:", str(e)[:160])
            page.wait_for_timeout(2500)
            print("  ", name)

        step("load", lambda: page.goto(BASE + "/index.html", wait_until="load"))
        for pg in ["cooling", "games", "files", "payloads", "system", "profile", "log", "credits"]:
            step("page " + pg, lambda pg=pg: page.click('button.tab[data-page="%s"]' % pg))
        page.click('button.tab[data-page="games"]')
        for t in ["time", "saves", "pkg", "p4m", "fpkg", "bk", "hist", "lib"]:
            step("games/" + t, lambda t=t: page.click("#gm-tab-" + t))
        page.wait_for_timeout(1500)

        def conv():
            page.query_selector_all("button[data-convert]:not([disabled])")[0].click()
        step("window Konvertieren", conv)
        step("close it", lambda: page.click('button[data-cp="close"]'))
        page.click('button.tab[data-page="log"]')
        step("kernel log", lambda: page.click("#log-tab-klog"))
        b.close()
    print("answers in", RAW)


main()
