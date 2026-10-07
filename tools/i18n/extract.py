#!/usr/bin/env python3
"""Finds the German texts of the web interface: the text nodes and some attributes of web/index.html and every
string / template literal of web/app.js. Templates keep their ${...} as placeholders {0}, {1}, ...; HTML inside a
template is cut into the text between the tags (and the title / aria-label / placeholder / alt attributes).
Output: strings.json = list of {"id", "src", "kind", "where"} (kind: "exact" or "pattern", pattern when {n} occurs).

Used the same way for the C sources (extract_c.py) and checked against what the browser really shows (coverage test)."""
import html.parser
import json
import os
import re

WEB = os.environ.get("I18N_WEB") or os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "web")
HERE = os.environ.get("I18N_OUT") or os.getcwd()   # where strings*.json are written

KEYWORDS_BEFORE_REGEX = {"return", "typeof", "case", "do", "else", "in", "of", "void", "delete", "throw", "new", "yield", "await"}


class JsTok:
    """Cuts a JavaScript source into string literals and template literals (static parts + expression sources).
    One scanner handles code and the inside of ${...} alike (a regex literal like /"/g must not open a string)."""

    def __init__(self, src):
        self.s = src
        self.n = len(src)
        self.lits = []          # (kind, parts, line, joined): 'str' -> [text]; 'tpl' -> [text, expr, text, expr, ..., text];
                                # joined = the literal follows "<literal> +": the program glues the two into one text
        self.join_next = False

    def line(self, i):
        return self.s.count("\n", 0, i) + 1

    def escape(self, i):
        s = self.s
        c = s[i + 1] if i + 1 < self.n else ""
        m = {"n": "\n", "t": "\t", "r": "\r", "b": "\b", "f": "\f", "v": "\v", "0": "\0"}
        if c in m:
            return m[c], i + 2
        if c == "u":
            if s[i + 2] == "{":
                j = s.index("}", i)
                return chr(int(s[i + 3:j], 16)), j + 1
            return chr(int(s[i + 2:i + 6], 16)), i + 6
        if c == "x":
            return chr(int(s[i + 2:i + 4], 16)), i + 4
        if c == "\n":
            return "", i + 2
        return c, i + 2

    def read_string(self, i):
        q = self.s[i]
        i += 1
        out = []
        while i < self.n and self.s[i] != q:
            if self.s[i] == "\\":
                t, i = self.escape(i)
                out.append(t)
            else:
                out.append(self.s[i])
                i += 1
        return "".join(out), i + 1

    def read_template(self, i):
        start = i
        joined = self.join_next          # the scan of the ${...} expressions below resets the flag
        i += 1
        parts, cur = [], []
        while i < self.n and self.s[i] != "`":
            c = self.s[i]
            if c == "\\":
                t, i = self.escape(i)
                cur.append(t)
            elif c == "$" and self.s[i + 1:i + 2] == "{":
                parts.append("".join(cur))
                cur = []
                expr_start = i + 2
                i = self.scan(expr_start, in_expr=True)
                parts.append(self.s[expr_start:i - 1])
            else:
                cur.append(c)
                i += 1
        parts.append("".join(cur))
        self.lits.append(("tpl", parts, self.line(start), joined))
        self.join_next = False
        return i + 1

    def scan(self, i, in_expr=False):
        """Scans code from i. At top level to the end of the source; inside ${...} until its closing brace
        (returns the index after it). Literals met on the way are collected."""
        s = self.s
        depth = 0
        prev = None             # 'id' | 'num' | 'str' | 'close' | 'punct' | a keyword
        while i < self.n:
            c = s[i]
            if c in " \t\r\n":
                i += 1
                continue
            if s.startswith("//", i):
                j = s.find("\n", i)
                if j < 0:
                    return self.n
                i = j
                continue
            if s.startswith("/*", i):
                i = s.index("*/", i) + 2
                continue
            if c in "\"'":
                st = i
                text, i = self.read_string(i)
                self.lits.append(("str", [text], self.line(st), self.join_next))
                self.join_next = False
                prev = "str"
                continue
            if c == "`":
                i = self.read_template(i)
                self.join_next = False
                prev = "str"
                continue
            if c == "/":
                divide = prev in ("id", "num", "str", "close") and prev not in KEYWORDS_BEFORE_REGEX
                if divide:
                    i += 1
                    prev = "punct"
                    continue
                j = i + 1
                in_class = False
                while j < self.n:
                    if s[j] == "\\":
                        j += 2
                        continue
                    if s[j] == "[":
                        in_class = True
                    elif s[j] == "]":
                        in_class = False
                    elif s[j] == "/" and not in_class:
                        break
                    elif s[j] == "\n":
                        break
                    j += 1
                i = j + 1
                while i < self.n and s[i].isalpha():
                    i += 1
                prev = "num"
                continue
            if c.isalpha() or c in "_$":
                j = i
                while j < self.n and (s[j].isalnum() or s[j] in "_$"):
                    j += 1
                word = s[i:j]
                self.join_next = False
                prev = word if word in KEYWORDS_BEFORE_REGEX else "id"
                i = j
                continue
            if c.isdigit():
                j = i
                while j < self.n and (s[j].isalnum() or s[j] in "._"):
                    j += 1
                i = j
                self.join_next = False
                prev = "num"
                continue
            if c == "{":
                depth += 1
            elif c == "}":
                if in_expr and depth == 0:
                    return i + 1
                depth -= 1
            self.join_next = (c == "+" and prev == "str" and s[i + 1:i + 2] not in ("+", "="))
            prev = "close" if c in ")]}" else "punct"
            i += 1
        return self.n

    def run(self):
        self.scan(0)
        return self.lits


TAG = re.compile(r"<[^<>]*>")
ATTR = re.compile(r"""\b(title|aria-label|placeholder|alt)\s*=\s*(?:"([^"]*)"|'([^']*)')""")
LETTERS = re.compile(r"[A-Za-zÄÖÜäöüßÀ-ÿ]")
UMLAUT = re.compile(r"[äöüÄÖÜß]")
GERMAN_LOWER = {"von", "bis", "und", "oder", "aus", "nach", "mit", "ohne", "bei", "für", "auf", "im", "als", "zu", "zum", "zur",
                "an", "am", "in", "ist", "sind", "wird", "kein", "keine", "nicht", "noch", "nur", "auch", "über", "unter", "dann",
                "wenn", "dem", "den", "der", "die", "das", "ein", "eine", "einen", "einem", "dateien", "datei", "gelöscht"}
PLACEHOLDER = re.compile(r"\{\d+\}")


EDGES = re.compile(r"^(\s*(?:[·•,;]\s*)?)(.*?)(\s*[·,;]?\s*)$", re.S)


def norm(t):
    """The key of a text: the same core i18n.js looks up (a leading or trailing '·', ',' or ';' and white space are not part
    of it; white space inside is one blank)."""
    return re.sub(r"\s+", " ", EDGES.match(t).group(2)).strip()


def candidate(text):
    """Is this fragment a text a person reads (and that has to be translated)?"""
    t = norm(PLACEHOLDER.sub("", text))
    if not t or not LETTERS.search(t):
        return False
    if len(re.findall(r"[A-Za-zÄÖÜäöüßÀ-ÿ]{2,}", t)) == 0:
        return False
    if re.match(r"^(https?:|/api/|/[a-z]|#|\.(?!\s)|data:|[a-z]+://)", t):
        return False
    if "=>" in t or "function(" in t or t.startswith("{") or (t.startswith("[") and t.endswith("]") and " " not in t):
        return False
    if re.search(r"[;{}]\s*(const|let|if|return|var)\b|\)\s*[;{]", t):
        return False                      # code that slipped in
    words = t.split(" ")
    if len(words) == 1:
        w = words[0].rstrip(":")
        if re.search(r"[-_/.#:=\[\]@()$]", w) and not UMLAUT.search(w):
            return False
        if not UMLAUT.search(w) and not w[0].isupper():
            return False
        if w.isupper() and len(w) <= 6 and not UMLAUT.search(w):
            return False
        if any(ch.isdigit() for ch in w) and not UMLAUT.search(w):
            return False
        return True
    if UMLAUT.search(t) or t[:1].isupper() or any(w.lower() in GERMAN_LOWER for w in words):
        return True
    if all(re.fullmatch(r"[a-z0-9_\-]+", w) for w in words):
        return False                      # a list of CSS classes or words of code
    if len(words) >= 2 and any(w[:1] in "ABCDEFGHIJKLMNOPQRSTUVWXYZÄÖÜ" and re.fullmatch(r"[A-Za-zÄÖÜäöüß\-]+", w) for w in words[1:]) \
            and not re.search(r"[=<>{}$_/\\]", t):
        return True                       # "gemessene Drehzahl": a German noun in the middle of lower-case words
    return any(ch in t for ch in ".,:;!?")


def fragments_of(parts):
    """A literal as one string with {n} placeholders, then its text fragments (HTML cut open)."""
    whole = ""
    k = 0
    for idx, p in enumerate(parts):
        if idx % 2 == 0:
            whole += p
        else:
            whole += "{%d}" % k
            k += 1
    frags = []
    if "<" in whole or ">" in whole:
        for m in ATTR.finditer(whole):
            frags.append(m.group(2) if m.group(2) is not None else m.group(3))
        rest = TAG.sub("\x00", whole)
        for piece in rest.split("\x00"):
            # a piece may start inside a tag (its beginning was in another literal) or end inside one
            gt = piece.find(">")
            while gt >= 0 and (piece[gt + 1:gt + 2] == "=" or piece[gt - 1:gt] in (" ", "=")):
                gt = piece.find(">", gt + 1)                  # a comparison, not the end of a tag
            if gt >= 0 and "<" not in piece[:gt]:
                for m in ATTR.finditer(piece[:gt + 1]):
                    frags.append(m.group(2) if m.group(2) is not None else m.group(3))
                piece = piece[gt + 1:]
            lt = piece.rfind("<")
            if lt >= 0 and ">" not in piece[lt:]:
                piece = piece[:lt]
            frags.append(piece)
    else:
        frags.append(whole)
    return frags


TERNARY = re.compile(r"""^[^?]+\?\s*(["'`])((?:\\.|(?!\1)[^\\$])*)\1\s*:\s*(["'`])((?:\\.|(?!\3)[^\\$])*)\3\s*$""", re.S)


def unescape(t):
    return re.sub(r"\\(.)", lambda m: {"n": "\n", "t": "\t"}.get(m.group(1), m.group(1)), t)


def variants(parts):
    """A template whose expression is `cond ? "a" : "b"` shows either text; each combination (at most 8) is a text of its own.
    The other expressions stay placeholders."""
    outs = [list(parts)]
    for idx in range(1, len(parts), 2):
        m = TERNARY.match(parts[idx]) if parts[idx] is not None else None
        if not m:
            continue
        nxt = []
        for alt in (unescape(m.group(2)), unescape(m.group(4))):
            for o in outs:
                if len(nxt) >= 8:
                    break
                o2 = list(o)
                o2[idx + 1] = o[idx - 1] + alt + o[idx + 1]
                o2[idx - 1] = None
                o2[idx] = None
                nxt.append(o2)
        outs = nxt or outs
    res = []
    for o in outs:
        out, buf = [], ""
        for j, p in enumerate(o):
            if p is None:
                continue
            if j % 2 == 0:
                buf += p
            else:
                out += [buf, p]
                buf = ""
        res.append(out + [buf])
    return res


def from_js():
    src = open(os.path.join(WEB, "app.js"), encoding="utf-8").read()
    out = {}
    groups = []
    for kind, parts, line, joined in JsTok(src).run():
        if joined and groups:
            prev = groups[-1][0]
            groups[-1][0] = prev[:-1] + [prev[-1] + parts[0]] + parts[1:]      # "a" + "b" is one text for the page
        else:
            groups.append([list(parts), line])
    for parts, line in groups:
        for pv in variants(parts):
            for f in fragments_of(pv):
                if candidate(f):
                    out.setdefault(norm(f), []).append(line)
    return out


class HtmlTexts(html.parser.HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.texts = []
        self.skip = 0

    def handle_starttag(self, tag, attrs):
        if tag in ("script", "style"):
            self.skip += 1
        for k, v in attrs:
            if k in ("title", "aria-label", "placeholder", "alt") and v:
                self.texts.append((v, self.getpos()[0]))

    def handle_endtag(self, tag):
        if tag in ("script", "style"):
            self.skip = max(0, self.skip - 1)

    def handle_data(self, data):
        if not self.skip:
            self.texts.append((data, self.getpos()[0]))


def from_html(name):
    p = HtmlTexts()
    p.feed(open(os.path.join(WEB, name), encoding="utf-8").read())
    out = {}
    for t, line in p.texts:
        if candidate(t):
            out.setdefault(norm(t), []).append(line)
    return out


def from_avatars():
    """The names under the profile pictures stand in a data file, not in the page."""
    p = os.path.join(WEB, "avatars", "index.json")
    out = {}
    if os.path.exists(p):
        for im in json.load(open(p, encoding="utf-8")).get("images", []):
            if im.get("label"):
                out.setdefault(norm(im["label"]), []).append(1)
    return out


def from_extra():
    """Texts the program inserts into sentences as words (verbs, units of time ...): they are single lower-case words,
    which the filter in candidate() cannot tell from code, so they are listed by hand."""
    p = os.path.join(os.path.dirname(os.path.abspath(__file__)), "extra_sources.json")
    out = {}
    if os.path.exists(p):
        for t in json.load(open(p, encoding="utf-8")):
            out.setdefault(norm(t), []).append(1)
    return out


def main():
    js = from_js()
    ix = from_html("index.html")
    allk = {}
    for src_name, d in (("index.html", ix), ("app.js", js), ("avatars/index.json", from_avatars()), ("extra_sources.json", from_extra())):
        for k, lines in d.items():
            allk.setdefault(k, {"where": []})["where"].append("%s:%d" % (src_name, lines[0]))
    items = []
    for i, (k, v) in enumerate(sorted(allk.items(), key=lambda kv: kv[0])):
        items.append({"id": i, "src": k, "kind": "pattern" if PLACEHOLDER.search(k) else "exact", "where": v["where"][:3]})
    json.dump(items, open(os.path.join(HERE, "strings.json"), "w", encoding="utf-8"), ensure_ascii=False, indent=0)
    ex = sum(1 for x in items if x["kind"] == "exact")
    print("index.html: %d, app.js: %d, zusammen eindeutig: %d (exakt %d, Muster %d), Zeichen: %d" % (
        len(ix), len(js), len(items), ex, len(items) - ex, sum(len(x["src"]) for x in items)))


if __name__ == "__main__":
    main()
