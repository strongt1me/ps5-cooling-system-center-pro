#!/usr/bin/env python3
"""Finds the German texts the console itself produces (API error messages, job states and notes, log lines): every
C string literal of src/*.c that reads like German, adjacent literals joined, printf conversions turned into {n}
placeholders. Output: strings_c.json (same shape as strings.json, plus "fmt": the original format)."""
import glob
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import extract as E

SRC = os.environ.get("I18N_SRC") or os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "src")
HERE = os.environ.get("I18N_OUT") or os.getcwd()   # where strings*.json are written
CONV = re.compile(r"%(?:%|[-+ #0]*(?:\d+|\*)?(?:\.(?:\d+|\*))?(?:hh|h|ll|l|z|j|t|L)?[diouxXcsfFeEgGp])")


def c_literals(src):
    """(text, line) of every string literal; neighbours joined; comments and char literals skipped."""
    out = []
    i, n = 0, len(src)
    cur, cur_line, last_end = None, 0, -1
    while i < n:
        c = src[i]
        if src.startswith("//", i):
            j = src.find("\n", i)
            i = n if j < 0 else j
            continue
        if src.startswith("/*", i):
            i = src.index("*/", i) + 2
            continue
        if c == "'":
            j = i + 1
            while j < n and src[j] != "'":
                j += 2 if src[j] == "\\" else 1
            i = j + 1
            continue
        if c == "#" and (i == 0 or src[i - 1] == "\n"):
            # a preprocessor line: skip it (but keep #define bodies: messages are sometimes defined there)
            j = i
            while j < n and src[j] != "\n":
                if src[j] == "\\" and src[j + 1] == "\n":
                    j += 2
                    continue
                j += 1
            line = src[i:j]
            if re.match(r"#\s*(include|if|ifdef|ifndef|else|endif|elif|undef|pragma)\b", line):
                i = j
                continue
            i += 1
            continue
        if c == '"':
            st = i
            j = i + 1
            buf = []
            while j < n and src[j] != '"':
                if src[j] == "\\":
                    e = src[j + 1]
                    m = {"n": "\n", "t": "\t", "r": "\r", '"': '"', "\\": "\\", "'": "'", "0": "\0"}
                    if e in m:
                        buf.append(m[e])
                        j += 2
                    elif e == "x":
                        k = j + 2
                        while k < n and src[k] in "0123456789abcdefABCDEF":
                            k += 1
                        buf.append(chr(int(src[j + 2:k], 16)))
                        j = k
                    elif e == "\n":
                        j += 2
                    else:
                        buf.append(e)
                        j += 2
                else:
                    buf.append(src[j])
                    j += 1
            text = "".join(buf)
            between = src[last_end:st] if last_end >= 0 else None
            if cur is not None and between is not None and between.strip() == "":
                cur += text
            else:
                if cur is not None:
                    out.append((cur, cur_line))
                cur, cur_line = text, src.count("\n", 0, st) + 1
            last_end = j + 1
            i = j + 1
            continue
        i += 1
    if cur is not None:
        out.append((cur, cur_line))
    return out


def to_pattern(fmt):
    """printf format -> text with {n}; '%%' becomes '%'."""
    k = [0]

    def rep(m):
        if m.group(0) == "%%":
            return "%"
        s = "{%d}" % k[0]
        k[0] += 1
        return s
    return CONV.sub(rep, fmt)


def main():
    seen = {}
    for path in sorted(glob.glob(os.path.join(SRC, "*.c")) + glob.glob(os.path.join(SRC, "helper", "*.c"))):
        name = os.path.basename(path)
        src = open(path, encoding="utf-8", errors="replace").read()
        for text, line in c_literals(src):
            if "\n" in text.strip("\n"):
                parts = [p for p in text.split("\n") if p.strip()]
            else:
                parts = [text]
            for part in parts:
                pat = to_pattern(part)
                key = E.norm(pat)
                if not E.candidate(pat):
                    continue
                # only texts that are German (a log code, a file name or an English debug text is not)
                words = re.findall(r"[A-Za-zÄÖÜäöüß]+", key)
                german = E.UMLAUT.search(key) or any(w.lower() in E.GERMAN_LOWER or w.lower() in {
                    "konsole", "spiel", "laufwerk", "datei", "ordner", "fehler", "paket", "sicherung", "abgebrochen", "gelöscht",
                    "gestartet", "fertig", "titel", "protokoll", "temperatur", "lüfter", "meldung", "vorgang", "kopie"} for w in words)
                if not german:
                    continue
                e = seen.setdefault(key, {"where": [], "fmt": part})
                e["where"].append("%s:%d" % (name, line))
    items = []
    for i, (k, v) in enumerate(sorted(seen.items(), key=lambda kv: kv[0])):
        items.append({"id": i, "src": k, "kind": "pattern" if E.PLACEHOLDER.search(k) else "exact", "where": v["where"][:3], "fmt": v["fmt"]})
    json.dump(items, open(os.path.join(HERE, "strings_c.json"), "w", encoding="utf-8"), ensure_ascii=False, indent=0)
    ex = sum(1 for x in items if x["kind"] == "exact")
    print("C: %d eindeutige deutsche Texte (exakt %d, Muster %d), Zeichen: %d" % (len(items), ex, len(items) - ex, sum(len(x["src"]) for x in items)))


if __name__ == "__main__":
    main()
