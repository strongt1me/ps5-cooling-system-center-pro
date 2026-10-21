#!/usr/bin/env python3
"""Prüft, ob die Fremdbausteine der App (tools/upstream.json) neuere Fassungen auf GitHub haben.

Aufruf:  python tools/upstream_check.py            Tabelle, Exit-Code 1 wenn etwas neu ist
         python tools/upstream_check.py --json     dasselbe als JSON (für den Skill upstream-update)
         python tools/upstream_check.py --only ps4mount,patchdl

Nimmt die GitHub-CLI (gh, angemeldet: höheres Limit), sonst die offene API. Ändert nichts am Projekt.
Verglichen wird nach Reihenfolge der Fassung (Zahlenfolgen als Zahlen, Buchstabenfolgen ohne Groß/Klein), nicht nach
Gleichheit: eine Fassung, die der Nutzer vorab hat (z. B. 1.7beta6-fix2 gegen 1.7-beta5fix1 auf GitHub), ist nicht "neu".
"""
import json
import os
import re
import shutil
import subprocess
import sys
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
MANIFEST = os.path.join(HERE, "upstream.json")


FIXES = {"fix", "hotfix", "patch", "rev"}   # "1.7beta6-fix2" kommt nach "1.7beta6"


def tokens(v):
    v = v.strip()
    if v[:1] in "vV" and v[1:2].isdigit():
        v = v[1:]
    out = []
    for m in re.finditer(r"\d+|[A-Za-z]+", v):
        t = m.group(0)
        out.append((0, int(t)) if t.isdigit() else (1, t.lower()))
    return out


def cmp_ver(a, b):
    """-1, 0, 1. Wo eine Seite endet: eine weitere Buchstabenfolge zählt weniger ("1.7" > "1.7beta3"), eine weitere Zahl mehr."""
    ta, tb = tokens(a), tokens(b)
    for x, y in zip(ta, tb):
        if x == y:
            continue
        if x[0] != y[0]:
            return 1 if x[0] == 0 else -1
        return 1 if x[1] > y[1] else -1
    if len(ta) == len(tb):
        return 0
    longer, sign = (ta, 1) if len(ta) > len(tb) else (tb, -1)
    extra = longer[min(len(ta), len(tb))]
    return sign if extra[0] == 0 or extra[1] in FIXES else -sign


def api(path):
    gh = shutil.which("gh")
    if gh:
        r = subprocess.run([gh, "api", path], capture_output=True, text=True, encoding="utf-8")
        if r.returncode == 0:
            return json.loads(r.stdout)
    req = urllib.request.Request("https://api.github.com/" + path, headers={"User-Agent": "ps5tm-upstream-check"})
    with urllib.request.urlopen(req, timeout=30) as f:
        return json.load(f)


def latest(c):
    if c.get("prereleases"):
        rel = api("repos/%s/releases?per_page=5" % c["repo"])
        rel = [r for r in rel if not r.get("draft")]
        best = None
        for r in rel:
            if best is None or cmp_ver(r["tag_name"], best["tag_name"]) > 0:
                best = r
        rel = best
    else:
        rel = api("repos/%s/releases/latest" % c["repo"])
    return {
        "tag": rel["tag_name"],
        "date": (rel.get("published_at") or "")[:10],
        "prerelease": bool(rel.get("prerelease")),
        "url": rel.get("html_url", ""),
        "assets": [a["name"] for a in rel.get("assets", [])],
        "notes": (rel.get("body") or "")[:1500],
    }


def main():
    args = sys.argv[1:]
    as_json = "--json" in args
    only = None
    if "--only" in args:
        only = set(args[args.index("--only") + 1].split(","))
    comps = json.load(open(MANIFEST, encoding="utf-8"))["components"]
    rows = []
    for c in comps:
        if only and c["id"] not in only:
            continue
        row = {"id": c["id"], "repo": c["repo"], "current": c["current"], "watch": bool(c.get("watch")), "where": c["where"], "how": c["how"]}
        try:
            row.update(latest(c))
            d = cmp_ver(row["tag"], c["current"])
            row["status"] = "NEU" if d > 0 else ("aktuell" if d == 0 else "aktuell (Projekt neuer)")
        except Exception as e:  # Netz, Limit, gelöschtes Repo
            row["status"] = "FEHLER"
            row["error"] = str(e)[:200]
        rows.append(row)
    if as_json:
        print(json.dumps(rows, ensure_ascii=False, indent=1))
    else:
        w = max(len(r["id"]) for r in rows)
        for r in rows:
            print("%-*s  %-14s -> %-14s %-10s %s" % (w, r["id"], r["current"], r.get("tag", "?"), r.get("date", ""), r["status"] + (" (nur beobachten)" if r["watch"] and r["status"] == "NEU" else "")))
    new = [r for r in rows if r["status"] == "NEU"]
    return 1 if new else 0


if __name__ == "__main__":
    sys.exit(main())
