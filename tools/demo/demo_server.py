#!/usr/bin/env python3
"""Demo mode of the web interface (21.11.2026): serves web/ and answers /api/v1/ with the invented data of tools/demo/fixtures, no console needed.

    python tools/demo/demo_server.py [--port 8099] [--lang de|en|es|fr|it|ru|sv]

The pages of the manual are photographed from it (tools/demo/make_shots.py). Nothing here is part of the app on the console; the app is not changed.
What it does:
  - GET /api/v1/<route> answers fixtures/<route>.json ("/" and "-" become "_"), with the clock moved to now (timestamps of status, history and playtime);
  - what a game is called follows --lang ("Spiel 3", "Game 3", ...);
  - the covers of the games are drawn here (a colour, a game pad, "Spiel N"; needs Pillow, without it they are not there and the page draws its own);
    icons of packages are not there (404);
  - POST answers {"ok": true}; a route without a fixture answers 404 and is named on stderr, so a missing one is easy to add."""
import argparse, colorsys, io, json, os, re, sys, time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlsplit, parse_qs

HERE = os.path.dirname(os.path.abspath(__file__))
WEB = os.path.join(HERE, "..", "..", "web")
FIX = os.path.join(HERE, "fixtures")
try:
    VERSION = re.search(r'#define\s+PS5TM_VERSION\s+"([^"]+)"', open(os.path.join(HERE, "..", "..", "src", "ps5tm.h"), encoding="utf-8").read()).group(1)
except (OSError, AttributeError):
    VERSION = ""
GAME = {"de": "Spiel", "en": "Game", "es": "Juego", "fr": "Jeu", "it": "Gioco", "ru": "Игра", "sv": "Spel"}
PLAYER = {"de": "Spieler", "en": "Player", "es": "Jugador", "fr": "Joueur", "it": "Giocatore", "ru": "Игрок", "sv": "Spelare"}
TYPES = {".html": "text/html; charset=utf-8", ".js": "text/javascript; charset=utf-8", ".css": "text/css; charset=utf-8", ".json": "application/json",
         ".png": "image/png", ".jpg": "image/jpeg", ".svg": "image/svg+xml", ".ico": "image/x-icon", ".webp": "image/webp", ".woff2": "font/woff2"}


class Demo(BaseHTTPRequestHandler):
    lang = "de"
    cache = {}
    covers = {}

    def log_message(self, *a):
        pass

    def send(self, code, body, ctype="application/json"):
        if isinstance(body, str):
            body = body.encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def fixture(self, name):
        if name not in self.cache:
            p = os.path.join(FIX, name + ".json")
            self.cache[name] = open(p, encoding="utf-8").read() if os.path.exists(p) else None
        return self.cache[name]

    def api(self, path, query):
        route = re.sub(r"[/\-]", "_", path[len("/api/v1/"):].strip("/"))
        if route == "klog":
            return self.klog(query)
        if route == "library_cover":
            png = self.cover((query.get("id") or [""])[0])
            return self.send(200, png, "image/png") if png else self.send(404, '{"ok":false,"error":"no_cover"}')
        if route in ("kraken_icon", "packages_icon", "ps4mount_icon"):
            return self.send(404, '{"ok":false,"error":"no_cover"}')
        if route in ("profile_avatar_current", "saves_avatar"):
            p = os.path.join(WEB, "avatars", "05-blau-astronaut.jpg")
            return self.send(200, open(p, "rb").read(), "image/jpeg")
        text = self.fixture(route)
        if text is None:
            sys.stderr.write("demo: no fixture for %s\n" % path)
            return self.send(404, json.dumps({"ok": False, "error": "demo_no_fixture", "message": "Im Demo-Modus nicht vorhanden."}))
        d = json.loads(text)
        now_ms = int(time.time() * 1000)
        if route == "status":
            d["timestamp_ms"] = now_ms
        elif route == "history":
            self.shift_history(d, now_ms)
        elif route == "playtime":
            self.shift_playtime(d, now_ms // 1000)
        elif route == "system" and VERSION:
            d["app_version"] = VERSION          # the sidebar shows the version of the sources, not the one the fixtures were recorded with
        text = json.dumps(d, ensure_ascii=False)
        word = GAME.get(self.lang, "Spiel")
        if word != "Spiel":
            text = text.replace("Spiel ", word + " ")
        text = text.replace('"Spieler"', '"%s"' % PLAYER.get(self.lang, "Spieler"))     # the account name
        return self.send(200, text)

    def cover(self, title_id):
        """A cover for an invented game: two colours by its number, a game pad, its name."""
        try:
            from PIL import Image, ImageDraw, ImageFont
        except ImportError:
            return None
        m = re.search(r"(\d{4})$", title_id)
        n = int(m.group(1)) if m else 1
        key = (self.lang, n)
        if key in self.covers:
            return self.covers[key]
        w = 460
        hue = (n * 0.137) % 1.0
        c1 = tuple(int(255 * x) for x in colorsys.hsv_to_rgb(hue, 0.65, 0.55))
        c2 = tuple(int(255 * x) for x in colorsys.hsv_to_rgb((hue + 0.08) % 1.0, 0.7, 0.28))
        im = Image.new("RGB", (w, w))
        px = im.load()
        for y in range(w):
            for x in range(w):
                t = (x + y) / (2.0 * w)
                px[x, y] = tuple(int(c1[i] * (1 - t) + c2[i] * t) for i in range(3))
        d = ImageDraw.Draw(im, "RGBA")
        ink = (255, 255, 255, 150)
        d.rounded_rectangle((95, 120, 365, 250), radius=62, outline=ink, width=9)       # the pad
        d.ellipse((100, 190, 175, 285), outline=ink, width=9)
        d.ellipse((285, 190, 360, 285), outline=ink, width=9)
        d.line((150, 160, 150, 205), fill=ink, width=9)                                   # the cross
        d.line((128, 182, 172, 182), fill=ink, width=9)
        d.ellipse((290, 150, 306, 166), fill=ink)
        d.ellipse((318, 172, 334, 188), fill=ink)
        font = None
        for f in ("C:/Windows/Fonts/segoeuib.ttf", "C:/Windows/Fonts/arialbd.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"):
            if os.path.exists(f):
                font = ImageFont.truetype(f, 46)
                break
        label = "%s %d" % (GAME.get(self.lang, "Spiel"), n)
        if font is not None:
            tw = d.textlength(label, font=font)
            d.text(((w - tw) / 2, 355), label, font=font, fill=(255, 255, 255, 235))
        b = io.BytesIO()
        im.save(b, "PNG")
        self.covers[key] = b.getvalue()
        return self.covers[key]

    @staticmethod
    def shift_history(d, now_ms):
        """The recorded curve ends now."""
        def last(o):
            best = 0
            if isinstance(o, dict):
                for k, v in o.items():
                    if k in ("t", "ts", "timestamp_ms", "time_ms") and isinstance(v, (int, float)):
                        best = max(best, v)
                    else:
                        best = max(best, last(v))
            elif isinstance(o, list):
                for x in o:
                    best = max(best, last(x))
            return best
        end = last(d)
        if not end:
            return
        delta = now_ms - end

        def shift(o):
            if isinstance(o, dict):
                for k, v in o.items():
                    if k in ("t", "ts", "timestamp_ms", "time_ms") and isinstance(v, (int, float)) and v > 10 ** 11:
                        o[k] = v + delta
                    else:
                        shift(v)
            elif isinstance(o, list):
                for x in o:
                    shift(x)
        shift(d)

    @staticmethod
    def shift_playtime(d, now_s):
        """The sessions end now as well: the page compares "now" of the console with the clock of the browser and, more than 10 minutes apart,
        says the console's clock is wrong (24.11.2026: three days after the recording it said "65 h behind")."""
        if not isinstance(d.get("now"), (int, float)):
            return
        delta = now_s - int(d["now"])
        d["now"] = now_s
        for s in ([d["live"]] if isinstance(d.get("live"), dict) else []) + list(d.get("sessions") or []):
            for k in ("start", "end"):
                if isinstance(s.get(k), (int, float)) and s[k] > 10 ** 9:
                    s[k] += delta

    def klog(self, query):
        """Kernel messages of the demo: the same few lines again and again, numbered."""
        after = int((query.get("after") or ["0"])[0])
        mx = int((query.get("max") or ["800"])[0])
        base = ["WARNING: [GFX] vblank wait took 18 ms", "sceKernelUsleep: slept 1000 us", "[SceShellCore] app foreground changed", "ahci0: link up 6.0 Gbps",
                "[HID] pad connected", "net0: rx 1500 bytes", "[sflash] read ok", "vm_pageout: free pages 41231", "[ICC] fan duty 38 %", "[thermal] cpu 52 C soc 49 C",
                "[audio] output underrun 0", "usb0: da1 attached"]
        total = 1200
        n0 = max(after, total - mx) if after else total - mx
        t0 = int(time.time() * 1000) - total * 1000
        lines = [{"n": n, "t": t0 + n * 1000, "s": base[n % len(base)]} for n in range(n0 + 1 if after else n0, total)]
        return self.send(200, json.dumps({"ok": True, "newest": total - 1, "lost": False, "more": False, "lines": lines[-mx:]}))

    def do_GET(self):
        u = urlsplit(self.path)
        if u.path.startswith("/api/v1/"):
            return self.api(u.path, parse_qs(u.query))
        if u.path == "/favicon.ico":                     # like the app since v1.93.0 (src/assets.c): empty, no 404 in the console of the browser
            self.send_response(204)
            self.send_header("Content-Type", "image/x-icon")
            self.end_headers()
            return
        rel = "index.html" if u.path in ("/", "") else u.path.lstrip("/")
        p = os.path.normpath(os.path.join(WEB, rel))
        if not p.startswith(os.path.normpath(WEB)) or not os.path.isfile(p):
            return self.send(404, "not found", "text/plain")
        return self.send(200, open(p, "rb").read(), TYPES.get(os.path.splitext(p)[1].lower(), "application/octet-stream"))

    def do_POST(self):
        n = int(self.headers.get("Content-Length") or 0)
        if n:
            self.rfile.read(n)
        return self.send(200, '{"ok":true,"message":"Im Demo-Modus nicht ausgeführt."}')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8099)
    ap.add_argument("--lang", default="de", choices=sorted(GAME))
    a = ap.parse_args()
    Demo.lang = a.lang
    srv = ThreadingHTTPServer(("127.0.0.1", a.port), Demo)
    print("demo mode on http://127.0.0.1:%d/ (names of games: %s)" % (a.port, GAME[a.lang]), flush=True)
    srv.serve_forever()


if __name__ == "__main__":
    main()
