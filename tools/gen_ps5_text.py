# -*- coding: utf-8 -*-
"""Writes the block "PS5-TEXT" of web/style.css: a copy of every font-size rule, prefixed with html.ps5 and with the small sizes made bigger.

Why: on the console's browser the small texts (12 to 13 px) are hard to read from the sofa. theme-boot.js puts the class "ps5" on <html> when the
browser says "PlayStation"; everywhere else nothing changes. The rules are copied one by one, in their original order and inside their original
@media blocks, so the cascade stays what it was (a rule with a bigger size and higher specificity still wins over a smaller one). Sizes of 15 px and
more are copied unchanged for exactly that reason.

Run after every change of the font sizes in style.css:   python tools/gen_ps5_text.py
Hand-made exceptions (a text that must not grow, a label that must not wrap) go below the END marker, in the block "PS5-Feinheiten".
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CSS = os.path.join(ROOT, "web", "style.css")
BEGIN = "/* ── PS5-TEXT:BEGIN (erzeugt von tools/gen_ps5_text.py, nicht von Hand ändern) ─────────────────────────────────────────── */"
END = "/* ── PS5-TEXT:END ── */"


def grow(px):
    """The size on the console: the smaller the text, the more it grows; from 15 px on it stays as it is."""
    if px >= 15:
        return px
    if px <= 12.5:
        d = 2.0
    elif px <= 13.5:
        d = 1.5
    elif px <= 14:
        d = 1.0
    else:
        d = 0.5
    return px + d


def fmt(px):
    return ("%g" % px) + "px"


def split_top(text, sep=","):
    out, depth, cur = [], 0, ""
    for ch in text:
        if ch in "([":
            depth += 1
        elif ch in ")]":
            depth -= 1
        if ch == sep and depth == 0:
            out.append(cur.strip())
            cur = ""
        else:
            cur += ch
    if cur.strip():
        out.append(cur.strip())
    return out


def parse(text, ctx, out):
    i, n = 0, len(text)
    while i < n:
        j = text.find("{", i)
        if j < 0:
            break
        prelude = text[i:j].strip()
        depth, k = 1, j + 1
        while k < n and depth:
            if text[k] == "{":
                depth += 1
            elif text[k] == "}":
                depth -= 1
            k += 1
        body = text[j + 1:k - 1]
        if prelude.startswith("@media") or prelude.startswith("@supports"):
            parse(body, ctx + (prelude,), out)
        elif prelude.startswith("@"):
            pass                                  # @keyframes, @font-face ...
        else:
            out.append((ctx, prelude, body))
        i = k


SIZE = re.compile(r"(?<![\w-])font-size\s*:\s*([\d.]+)px")
SHORT = re.compile(r"(?<![\w-])font\s*:\s*[^;{}]*?([\d.]+)px")


def prefixed(sel):
    sel = sel.strip()
    if sel.startswith(":root"):
        return "html.ps5" + sel[5:]
    if sel == "html":
        return "html.ps5"
    return "html.ps5 " + sel


def main():
    css = open(CSS, "rb").read().decode("utf-8")
    nl = "\r\n" if "\r\n" in css else "\n"
    css = css.replace("\r\n", "\n")
    a = css.find(BEGIN)
    b = css.index(END, a) + len(END) if a >= 0 else -1
    base = css[:a] + css[b:] if a >= 0 else css          # the rest of the file, without an older generated block
    plain = re.sub(r"/\*.*?\*/", "", base, flags=re.S)
    rules = []
    parse(plain, (), rules)
    lines, last_ctx, count, grown = [], (), 0, 0
    for ctx, prelude, body in rules:
        best, pos = None, -1
        for m in SIZE.finditer(body):
            best, pos = float(m.group(1)), m.start()
        for m in SHORT.finditer(body):
            if m.start() > pos:
                best, pos = float(m.group(1)), m.start()
        if best is None:
            continue
        sels = [prefixed(x) for x in split_top(prelude)]
        if ctx != last_ctx:
            for _ in last_ctx:
                lines.append("}")
            for c in ctx:
                lines.append(c + " {")
            last_ctx = ctx
        new = grow(best)
        count += 1
        grown += new != best
        lines.append("%s { font-size: %s; }" % (", ".join(sels), fmt(new)))
    for _ in last_ctx:
        lines.append("}")
    block = [BEGIN,
             "/* Browser der PS5: kleine Schrift größer (Klasse ps5 an <html>, siehe theme-boot.js). %d Regeln, davon %d größer. */" % (count, grown)]
    block += lines + [END]
    text = "\n".join(block)
    if a >= 0:
        css = css[:a] + text + css[b:]                  # in place: what follows (PS5-Feinheiten) stays behind it
    else:
        css = css.rstrip("\n") + "\n\n" + text + "\n"
    open(CSS, "wb").write(css.replace("\n", nl).encode("utf-8"))
    print("PS5-TEXT: %d rules, %d made bigger" % (count, grown))


if __name__ == "__main__":
    sys.exit(main())
