#!/usr/bin/env python3
"""Checks the dictionary of a language of the web interface: python tools/i18n/check_lang.py <lang> [--list]

  - every German text found in web/index.html, web/app.js and src/*.c has an entry (else: missing, German stays)
  - the placeholders {0}, {1}, ... of an entry are the same as in its German key (any order)
  - the entry is not empty, and has no HTML the key did not have
Exit code 1 when a placeholder or an empty entry is found; missing texts are only reported (--list shows them all).
A text may legitimately be missing when it is the same in the language (a name, a unit): such entries are left out of the files."""
import json
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
PH = re.compile(r"\{\d+\}")
PLG = re.compile(r"\{\d+\|[^{}]*\}")      # plural group {0|one|few|many} in a translation: stands for a word, not a value
TAG = re.compile(r"<[^<>]+>")


def sources():
    with tempfile.TemporaryDirectory() as tmp:
        env = dict(os.environ, I18N_OUT=tmp)
        for script in ("extract.py", "extract_c.py"):
            subprocess.run([sys.executable, os.path.join(HERE, script)], env=env, check=True, stdout=subprocess.DEVNULL)
        out = {}
        for f in ("strings.json", "strings_c.json"):
            for it in json.load(open(os.path.join(tmp, f), encoding="utf-8")):
                out.setdefault(it["src"], it["where"])
        return out


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    lang = sys.argv[1]
    path = os.path.join(ROOT, "web", "lang", lang + ".json")
    if not os.path.exists(path):
        sys.exit("kein Wörterbuch: " + path)
    t = json.load(open(path, encoding="utf-8"))["t"]
    src = sources()
    bad = []
    for k, v in t.items():
        if not isinstance(v, str) or not v.strip():
            bad.append("leer: %r" % k[:60])
        elif sorted(PH.findall(k)) != sorted(PH.findall(PLG.sub("", v))):
            bad.append("Platzhalter: %r -> %r" % (k[:50], v[:50]))
        elif TAG.search(v) and not TAG.search(k):
            bad.append("HTML dazugekommen: %r -> %r" % (k[:50], v[:50]))
    missing = [s for s in src if s not in t and re.search(r"[A-Za-zÄÖÜäöüß]{3,}", PH.sub("", s))]
    print("%s: %d Einträge, %d deutsche Texte im Quelltext, %d ohne Eintrag, %d Fehler" % (lang, len(t), len(src), len(missing), len(bad)))
    for b in bad:
        print("  FEHLER", b)
    for m in missing[: (len(missing) if "--list" in sys.argv else 15)]:
        print("  fehlt: %-60s %s" % (m[:60], src[m][0] if src[m] else ""))
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
