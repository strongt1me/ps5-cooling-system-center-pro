#!/usr/bin/env python3
"""Makes the demo fixtures (tools/demo/fixtures/*.json) from answers of a real console, with every personal value replaced (21.11.2026).

The demo mode (tools/demo/demo_server.py) answers the web UI with these files instead of a console, so the screenshots of the manual show invented data.
Run this only to make the fixtures again (it needs the recorded answers of a console; the fixtures themselves are in the repository, the recordings are not):

    python tools/demo/record.py http://<console>:8086 <raw dir>        read-only: opens the pages, writes the answers to <raw dir>
    python tools/demo/make_fixtures.py <raw dir>                       writes tools/demo/fixtures/

What happens to the answers:
  - titles become "Spiel N", title ids PPSA9NNNN, the account "Spieler", the network "Heimnetz" / 192.168.1.50, the console "PS5";
  - in the routes of STRICT every string that is not a short plain word, a version, a number or a date becomes "demo";
  - a few answers are written by hand (HAND below): the event log, the payload lists, the folder list, the drives, the account.
At the end the script looks for every real value in its own output and stops when one is still there."""
import json, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "fixtures")
RAW = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "raw")

PERSONAL = {                      # key (lower case) -> the invented value
    "serial_masked": "XXXXX0000", "serial": "XXXXX0000", "console_name": "PS5 Pro", "username": "Spieler", "ssid": "Heimnetz", "ip": "192.168.1.50",
    "uid": "1234567890", "uid_hex": "0x499602D2", "account_id": "0000000000000000", "psn_name": "Spieler", "mac": "00:00:00:00:00:00",
    "bind_ip": "192.168.1.50", "host": "192.168.1.50", "console": "PS5 Pro", "owner": "Spieler",
}
STRICT = {"library", "playtime", "saves", "packages", "library_backups", "jobs_history", "kraken", "library_convert_plan", "library_orig_plan",
          "library_duplicates", "klog_files", "ps4mount", "library_storage"}
SAFE_SEG = {"data", "user", "mnt", "usb0", "usb1", "usb2", "ext0", "ext1", "homebrew", "etaHEN", "games", "FPKG-Games", "PS5-Cooling-Center", "payloads",
            "PS5-Sicherung", "Spiele", "Spielstaende", "app", "system", "system_data", "download0", "pkg", "dumps", "kraken", "dumpforge", "shadowmnt",
            "fakelib", "AMPR_EMU", "PlayGo", "LZ4_Profile", "traces", "backups", "Sicherungen", "Dumps", "PS4", "PS5", "tmp", "sandbox", "appmeta",
            "packages", "payload-store", "payload-avatars", "FAKE86921", "FAKE86922", "sce_sys", "priv", "mms"}
ID = re.compile(r"(PPSA|CUSA)(\d{5})")
ENUMISH = re.compile(r"^[A-Za-z][A-Za-z0-9_\-\.]{0,12}$|^v?\d+(\.\d+)*[a-z0-9\-]*$|^\d+$|^$"
                     r"|^\d{4}-\d{2}-\d{2}([ T]\d{2}:\d{2}(:\d{2})?Z?)?$")
NAME_KEYS = {"name", "title", "title_name", "display_name", "game", "app_name"}
LABELS = {"Interne SSD", "M.2-Erweiterung", "USB-Laufwerk"}


class Anon:
    def __init__(self):
        self.ids, self.names, self.segs = {}, {}, {}

    def fake_id(self, real):
        if real not in self.ids:
            self.ids[real] = "PPSA9%04d" % (len(self.ids) + 1)
        return self.ids[real]

    def fake_seg(self, seg):
        base, dot, ext = seg.rpartition(".")
        if not dot or len(ext) > 6:
            ext = ""
        if seg not in self.segs:
            self.segs[seg] = "demo-%d" % (len(self.segs) + 1)
        return self.segs[seg] + ("." + ext if ext else "")

    def sub_ids(self, s):
        return ID.sub(lambda m: self.fake_id(m.group(0)), s)

    def path(self, s):
        out = []
        for seg in s.split("/"):
            if seg == "" or seg in SAFE_SEG or re.fullmatch(r"[\d\-_]+", seg):
                out.append(seg)
            elif ID.search(seg):
                m = ID.search(seg)
                ext = seg[seg.rfind("."):] if "." in seg and len(seg) - seg.rfind(".") <= 7 else ""
                out.append(self.fake_id(m.group(0)) + ext)       # a name with the title in it: only the id and the extension stay
            else:
                out.append(self.fake_seg(seg))
        return "/".join(out)

    def fake_name_by_text(self, v):
        if v not in self.names:
            self.names[v] = "Spiel %d" % (900 + len(self.names) + 1)
        return self.names[v]

    def string(self, key, v, strict):
        k = (key or "").lower()
        if k in PERSONAL:
            return PERSONAL[k]
        if k == "slot":                                  # the name of a save folder: it is named after the game
            return "save"
        if k == "content_id" and re.fullmatch(r"[A-Z]{2}\d{4}-(PPSA|CUSA)\d{5}_\d\d-\S+", v):
            return "EP0000-%s_00-DEMO0000000000000" % self.fake_id(ID.search(v).group(0))
        if ID.fullmatch(v):
            return self.fake_id(v)
        if v.startswith("/") or ("/" in v and ID.search(v)):
            return self.path(v)
        if k == "title_name" and v:
            return self.fake_name_by_text(v)
        if ID.search(v):
            m = ID.search(v)
            ext = v[v.rfind("."):] if "." in v and len(v) - v.rfind(".") <= 7 else ""
            return self.fake_id(m.group(0)) + ext if strict else self.sub_ids(v)
        if k == "drive" and strict:
            return v if ENUMISH.match(v) else "Interne SSD"
        if k == "type" and strict:
            return v if ENUMISH.match(v) else "folder"
        if k == "label" and strict:
            return v if v in LABELS or ENUMISH.match(v) else "USB-Laufwerk"
        if k in NAME_KEYS and strict:
            return self.fake_name_by_text(v)
        if not strict or ENUMISH.match(v):
            return v
        return "demo"

    def walk(self, o, key=None, strict=False):
        if isinstance(o, dict):
            return {(self.sub_ids(k) if ID.search(k) else k): self.walk(v, k, strict) for k, v in o.items()}
        if isinstance(o, list):
            return [self.walk(x, key, strict) for x in o]
        if isinstance(o, str):
            return self.string(key, o, strict)
        return o


def num(fid):
    return int(fid[5:]) if isinstance(fid, str) and fid.startswith("PPSA9") else None


def spiel(fid):
    return "Spiel %d" % (num(fid) or 1)


def subdemo(o, repl):
    """The parts of a path that were replaced one by one ("demo-12") belong to one game: they get its id."""
    if isinstance(o, dict):
        return {k: subdemo(v, repl) for k, v in o.items()}
    if isinstance(o, list):
        return [subdemo(x, repl) for x in o]
    if isinstance(o, str):
        return re.sub(r"demo-\d+", repl, o)
    return o


def fix_games(an, route, res):
    """Names and paths of the games, from their (invented) ids."""
    if route == "library":
        games = [g for g in res["games"] if g["title_id"].startswith("PPSA9")][:18]
        for g in games:
            g["name"] = spiel(g["title_id"])
            fmt = g.get("format", "")
            if g.get("source", "").startswith("/mnt/shadowmnt"):
                g["source"] = "/mnt/shadowmnt/" + g["title_id"]
            if g.get("path", "").startswith("/"):
                base = g["path"].rsplit("/", 1)[0]
                g["path"] = "%s/%s%s" % (base, g["title_id"], ("." + fmt) if fmt in ("exfat", "ffpkg", "ffpfsc") else "")
        for i, g in enumerate(games):
            if isinstance(g.get("mods"), dict):
                g["mods"]["fakelib"] = []
            g["cover"] = "/api/v1/library/cover?id=%s&ts=1795000000" % g["title_id"]       # the demo server draws it
            g["launch_url"] = "psgm:play?id=%s" % g["title_id"]
            games[i] = subdemo(g, g["title_id"])
        res["games"] = games
        res["count"] = len(games)
        res["conflicts"] = []
    elif route == "playtime":
        res["titles"] = {k: spiel(k) for k in res["titles"]}
        if res.get("live"):
            res["live"]["name"] = spiel(res["live"]["id"])
    elif route == "saves":
        res = json.loads(re.sub(r"demo-\d+", "2026-11-18_20-15-00", json.dumps(res)))
        for u in res.get("users", []):
            u["name"] = "Spieler"
            for t in u.get("titles", []):
                t["name"] = spiel(t["id"])
        for b in res.get("backups", []):
            for u in b.get("users", []):
                u["name"] = "Spieler"
            for t in b.get("titles", []):
                t["name"] = spiel(t["id"])
        res["console"] = "PS5 Pro"
    elif route == "packages":
        for i, p in enumerate(res.get("packages", [])):
            p["name"] = spiel(p.get("title_id"))
            p["file"] = "%s.pkg" % p["title_id"]
            p["path"] = "%s/%s.pkg" % (p["path"].rsplit("/", 1)[0], p["title_id"])
            p["id"] = "pkg%04d" % (i + 1)
    elif route in ("library_convert_plan", "library_orig_plan"):
        tid = res.get("title_id") or res.get("id")
        res["name"] = spiel(tid)
        res = subdemo(res, tid)
        if route == "library_convert_plan":
            res.get("dumpforge", {})["project"] = "https://github.com/quer3q/ps5-dump-forge"
            res.get("dumpforge", {})["folder"] = "/data/PS5-Cooling-Center/dumpforge"
        else:
            res["delete_why"] = ""
            res["move_why"] = ""
            res["default_folder"] = "/data/PS5-Sicherung/Spiele"
    elif route == "kraken":
        res["project"] = "https://github.com/quer3q/kraken-mount"
        for g in res.get("kraken", {}).get("games", []):
            g["title_name"] = spiel(g.get("title_id"))
    elif route == "library_duplicates":
        ids = sorted(g for g in an.ids.values())[:9]
        k = iter(ids)
        res = json.loads(re.sub(r"demo-\d+", lambda m: next(k, "PPSA90001"), json.dumps(res)))
    elif route == "library_storage":
        res = json.loads(json.dumps(res).replace("/demo-3/demo-5", "/dev/da1"))
    elif route == "ps4mount":
        res["version"] = "0.8.0"
    elif route == "kraken":
        res = json.loads(re.sub(r"demo-\d+", "kraken", json.dumps(res)))
        res["project"] = "https://github.com/quer3q/kraken-mount"
    elif route == "jobs_history":
        res["entries"] = res["entries"][:14]
        for e in res["entries"]:
            e["detail"] = ""
            if e.get("error"):
                e["error"] = ""
            if ID.fullmatch(e.get("title", "").split(".")[0]) or str(e.get("title", "")).startswith("PPSA9"):
                e["title"] = spiel(e["title"].split(".")[0])
            elif e.get("title", "").startswith("Spiel "):
                pass
    return res


TUNE = {"target_temp_c": 66, "target_c": 66, "effective_target_c": 66, "fan_cap_pct": 100, "cap_pct": 100, "safety_temp_c": 78, "safety_c": 78}


def tune(o):
    """The recorded console was set to a target of 83 degrees and a cap of 70 %: the demo shows the settings of a new installation."""
    if isinstance(o, dict):
        for k, v in list(o.items()):
            if k in TUNE and isinstance(v, (int, float)) and v in (83, 70, 95):
                o[k] = TUNE[k]
            elif k == "points" and isinstance(v, list) and v and isinstance(v[0], dict) and "c" in v[0]:
                for pt in v:
                    pt["c"] -= 17
            else:
                tune(v)
    elif isinstance(o, list):
        for x in o:
            tune(x)


def hand(name):
    """Answers that are better written than cleaned."""
    t0 = 1795241000
    if name == "logs":
        ev = [("INFO", "boot", "PS5 Cooling & System Center - Pro gestartet."),
              ("INFO", "jb_escalation_ok", "Berechtigungen erweitert."),
              ("INFO", "firmware", "Firmware 12.00 erkannt."),
              ("INFO", "config_loaded", "Einstellungen geladen."),
              ("INFO", "fan_ok", "Lüfterbaustein gefunden, Regelung bereit."),
              ("INFO", "http_listen", "Weboberfläche auf Port 8086."),
              ("INFO", "tile_ok", "Kachel vorhanden."),
              ("INFO", "game_front", "Ein Spiel läuft im Vordergrund."),
              ("INFO", "target_set", "Ziel 66 °C, Betriebsart ausgewogen."),
              ("WARN", "temp_high", "Prozessor 78 °C: Lüfter wird schneller."),
              ("INFO", "temp_ok", "Prozessor wieder unter 70 °C."),
              ("INFO", "mic_button_shown", "Mikrofon-Taste zweimal gedrückt: Prozessor 52 °C, Lüfter 38 % als Meldung gezeigt."),
              ("INFO", "paystore_fetched", "Payload-Liste geladen: 38 Payloads.")]
        return {"ok": True, "entries": [{"timestamp_ms": (t0 + i * 41) * 1000, "level": lv, "code": c, "message": m} for i, (lv, c, m) in enumerate(ev)]}
    if name == "payloads":
        ps = [("PS5 Cooling & System Center - Pro", 41, True), ("elfldr", 3, False), ("kstuff", 7, False), ("ShadowMountPlus", 29, False), ("ftpsrv", 2, False),
              ("etaHEN", 12, False)]
        return {"ok": True, "payloads": [{"pid": 230 - i * 17, "name": n, "memory_mb": m, "is_self": s} for i, (n, m, s) in enumerate(ps)], "count": len(ps)}
    if name == "payload_files":
        fl = [("elfldr.elf", 132352), ("kstuff-lite.elf", 1476744), ("ShadowMountPlus.elf", 4676224), ("ftpsrv.elf", 394568), ("etaHEN.bin", 20612136),
              ("websrv.elf", 1845320), ("PKG-Manager.elf", 3583688), ("ps5upload.elf", 2795992)]
        return {"ok": True, "dir": "/data/PS5-Cooling-Center/payloads", "dir_exists": True,
                "internal": [{"name": n, "dir": "", "size": s, "mtime": 1794263349 - i * 86400, "valid": True} for i, (n, s) in enumerate(fl)],
                "usb": [], "usb_count": 0}
    if name == "files_places":
        return {"ok": True, "places": [{"label": "Interne SSD", "path": "/data", "free_bytes": 1083896823808, "writable": True, "ejectable": False},
                                       {"label": "usb0", "path": "/mnt/usb0", "free_bytes": 127025741824, "writable": True, "ejectable": True},
                                       {"label": "usb1", "path": "/mnt/usb1", "free_bytes": 50104696832, "writable": True, "ejectable": True},
                                       {"label": "Konsole", "path": "/", "free_bytes": 778240, "writable": False, "ejectable": False}]}
    if name == "files_list":
        ents = [("FPKG-Games", "dir", 0), ("PS5-Cooling-Center", "dir", 0), ("PS5-Sicherung", "dir", 0), ("homebrew", "dir", 0), ("etaHEN", "dir", 0),
                ("pkg", "dir", 0), ("ShadowMountPlus", "dir", 0), ("config.ini", "file", 1480), ("notizen.txt", "file", 612), ("startliste.txt", "file", 2048)]
        return {"ok": True, "path": "/data", "parent": "/", "writable": True, "free_bytes": 1083896823808, "total": len(ents), "truncated": False,
                "entries": [{"name": n, "type": t, "size": s, "mtime": 1794263349 - i * 5000, "writable": True} for i, (n, t, s) in enumerate(ents)]}
    if name == "profile_account_status":
        return {"valid": True, "uid": "1234567890", "activated": True, "fakesignin": True, "account_id": "0000000000000000", "username": "Spieler",
                "accounts": [{"slot": 1, "name": "Spieler", "id": "0000000000000000", "type": "np", "flags": 4098, "active": True}]}
    if name == "ps4mount_log":
        return {"log": "PS4Mount ist ausgeschaltet.\n"}
    return None


def route_of(fname):
    return re.sub(r"^api_v1_", "", fname[:-5].strip("_"))


def main():
    an = Anon()
    files = sorted(f for f in os.listdir(RAW) if f.endswith(".json"))
    lib = json.load(open(os.path.join(RAW, "_api_v1_library.json"), encoding="utf-8"))
    for g in lib.get("games", []):          # "Spiel 1 ..." follow the order of the home screen
        an.fake_id(g["title_id"])
    os.makedirs(OUT, exist_ok=True)
    for f in os.listdir(OUT):
        if f.endswith(".json") and f != "qr.json":
            os.remove(os.path.join(OUT, f))
    for f in files:
        route = route_of(f)
        if route.startswith("klog_after") or route == "qr":      # the log is written by hand, the QR code is made for an invented address
            continue
        data = json.load(open(os.path.join(RAW, f), encoding="utf-8"))
        if route == "paystore_refresh_1":
            route = "paystore"
        strict = route in STRICT or route.startswith(("library_convert_plan", "library_orig_plan"))
        res = an.walk(data, None, strict)
        out = re.sub(r"_(id|path)_.*$", "", route)
        res = fix_games(an, out, res)
        if out == "status" and isinstance(res.get("game"), dict) and res["game"].get("title_id"):
            res["game"]["title_name"] = spiel(res["game"]["title_id"])
        if out in ("status", "config", "fan_curve", "cooling_health", "fan_selftest"):
            tune(res)
        if out == "paystore":
            for it in res.get("items", []):
                it.pop("installed", None)
                it["update"] = False
                it["name_taken"] = False
            res["job"] = {"state": "idle", "id": "", "name": "", "done": 0, "total": 0}
        h = hand(out)
        if h is not None:
            res = h
        json.dump(res, open(os.path.join(OUT, out + ".json"), "w", encoding="utf-8"), ensure_ascii=False, separators=(",", ":"))
    for name in ("payloads", "payload_files", "files_places", "files_list", "profile_account_status", "logs", "ps4mount_log"):
        pass
    leak_check(an, files)
    print("fixtures:", len(os.listdir(OUT)), "files,", sum(os.path.getsize(os.path.join(OUT, x)) for x in os.listdir(OUT)) // 1024, "KB")


def leak_check(an, files):
    """None of the real values (ids, names, odd path parts, the account, the network) may be left in the files that are not the public lists."""
    text = ""
    for f in os.listdir(OUT):
        if f.startswith("paystore") or f.startswith("qr"):
            continue
        text += open(os.path.join(OUT, f), encoding="utf-8").read().lower() + " "
    real = set(an.ids) | {n for n in an.names if n not in SAFE_SEG} | {g for g in an.segs if g not in SAFE_SEG}
    for f in files:
        d = json.load(open(os.path.join(RAW, f), encoding="utf-8"))
        if isinstance(d, dict):
            for k in ("username", "console_name", "ssid", "ip", "serial_masked", "uid_hex"):
                if isinstance(d.get(k), str):
                    real.add(d[k])
            for sub in ("network", "user", "game"):
                x = d.get(sub)
                if isinstance(x, dict):
                    for k in ("ssid", "ip", "username", "title_name"):
                        if isinstance(x.get(k), str):
                            real.add(x[k])
    real |= {"192.168.1.94", "192.168.1.52"}
    ok_words = {"elfldr", "elfldr.elf", "ftpsrv", "ftpsrv.elf", "kstuff.elf", "disc", "library", "payload.elf"}
    bad = sorted(v for v in real if len(v) >= 4 and v.lower() in text and v not in PERSONAL.values() and v not in SAFE_SEG and v.lower() not in ok_words
                 and not re.fullmatch(r"[\d\-_]+", v))             # the dates of the folders of the backups stay
    if bad:
        print("STILL IN THE OUTPUT:", bad[:40])
        sys.exit(1)
    print("leak check: none of the %d real values is left" % len(real))


main()
