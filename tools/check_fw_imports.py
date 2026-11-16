#!/usr/bin/env python3
"""Does every system function the app uses exist in the firmware?

The app calls the console's system libraries in two ways:

  - bound at load time: the undefined symbols of the finished ELF (what the SDK linked against)
  - looked up at run time: dlsym() and ps5tm_dynsym() with a name, found in the sources (src/*.c)

The system libraries export by NID, an encoded hash of the symbol name, not by name. This script computes the NID of
every name the app uses and looks for it in the exports of the system libraries of each firmware, read from a folder of
decrypted dumps (one sub folder per firmware, named 12.70, 12.60, ...):

    check_fw_imports.py [--fw-dir DIR] [--elf FILE] [--src DIR] [--fw 12.70 ...] [--verbose] [--json] [--no-cache]

  --fw-dir  the dumps (default: environment variable PS5_FW_DIR, else C:\\FW 1.00-12.70)
  --elf     the finished ELF (default: PS5_Cooling_Center.elf in the project folder)
  --src     the sources, for the names looked up at run time (default: src in the project folder)
  --fw      only these firmwares
  --verbose every name with the firmwares it is missing in
  --json    machine-readable result on stdout
  --no-cache  scan the dumps again (the exports are otherwise kept in ~/.cache/ps5cc-fw-imports)

What it can and cannot say. A name is "present" when some library of that firmware exports its NID: the function exists
there. It does not say that the call takes the same arguments or answers the same way, and a firmware without a dump is
not covered (the dumps end at 12.70; 7.00 is encrypted and unreadable). A library is only ever read, never copied: the
dumps stay where they are and nothing of them enters the project.

Exit status: 0 nothing unexpected is missing, 1 something is missing, 2 wrong usage or no readable firmware.

Written for this project (16.11.2026). The container format of the libraries (PT_DYNAMIC with the SCE tags, symbols named
"<NID>#<library>#<module>") is read the way the dump collection's own build_import_export_db.py reads it; the NID rule is
the one of tvapp/tooling/native/sce_module_writer.cpp.
"""

import argparse
import base64
import glob
import hashlib
import json
import mmap
import os
import re
import struct
import sys

NID_SALT = bytes.fromhex("518D64A635DED8C1E6B039B1C3E55230")

# Names that are known to be missing somewhere and handled by the app. Anything else that is missing is reported.
KNOWN_OPTIONAL = {
    "sceAppInstUtilAppInstallTitleDir": "not exported from 12.00 on; tile.c and PS4Mount fall back to AppInstallAll",
    "sceSystemServiceShowErrorDialog": "not exported before 3.00; power.c works without the dialog (p_dialog)",
}

ID_ALPHABET = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+-"

ELF_HEADER = struct.Struct("<16sHHIQQQIHHHHHH")
PROGRAM_HEADER = struct.Struct("<IIQQQQQQ")
SECTION_HEADER = struct.Struct("<IIQQQQIIQQ")
DYNAMIC_ENTRY = struct.Struct("<QQ")
SYMBOL_ENTRY = struct.Struct("<IBBHQQ")

PT_LOAD, PT_DYNAMIC = 1, 2
SHT_DYNSYM, SHT_DYNAMIC = 11, 6
DT_NULL, DT_NEEDED, DT_HASH, DT_STRTAB, DT_SYMTAB, DT_STRSZ, DT_SYMENT = 0, 1, 4, 5, 6, 10, 11
DT_SCE_SYMTABSZ = 0x6100003F

# folders of an unpacked firmware that hold the system libraries; the modules of the applications are left out
SKIP_DIR_PARTS = {"app", "psm", "cdc"}
LIB_DIR = re.compile(r"(^|\s)lib($|\s)")          # "lib", but also "lib fw 2.50" and "PS5 Pro 12.40 Lib" of the collected dumps


def nid(name):
    """The NID of a symbol name: SHA-1 of the name and the 16 byte salt, the first 8 bytes reversed, Base64, 11 characters, "/" written as "-"."""
    digest = hashlib.sha1(name.encode("utf-8") + NID_SALT).digest()[:8][::-1]
    return base64.b64encode(digest).decode("ascii")[:11].replace("/", "-")


def fw_key(text):
    return tuple(int(x) for x in text.split("."))


# ---------------------------------------------------------------- the ELF of the app

def payload_imports(path):
    """(imports, needed): imports is [(name, binding)] of the undefined dynamic symbols (binding 'GLOBAL' or 'WEAK'), needed the DT_NEEDED file names."""
    with open(path, "rb") as f:
        data = f.read()
    if data[:4] != b"\x7fELF" or data[4] != 2:
        raise ValueError("%s is not an ELF64 file" % path)
    hdr = ELF_HEADER.unpack_from(data)
    shoff, shentsize, shnum = hdr[6], hdr[11], hdr[12]
    sections = [SECTION_HEADER.unpack_from(data, shoff + i * shentsize) for i in range(shnum)]

    def cstr(base, off):
        end = data.index(b"\0", base + off)
        return data[base + off:end].decode("utf-8", "replace")

    imports, needed = [], []
    for sh in sections:
        _name, typ, _fl, _addr, offset, size, link, _info, _al, entsize = sh
        if typ == SHT_DYNSYM:
            str_off = sections[link][4]
            for i in range(1, size // (entsize or 24)):
                st_name, st_info, _oth, shndx, _val, _sz = SYMBOL_ENTRY.unpack_from(data, offset + i * (entsize or 24))
                if shndx == 0 and st_name:
                    bind = {1: "GLOBAL", 2: "WEAK"}.get(st_info >> 4)
                    if bind:
                        imports.append((cstr(str_off, st_name).split("@")[0], bind))
        elif typ == SHT_DYNAMIC:
            str_off = sections[link][4]
            for i in range(size // 16):
                tag, val = DYNAMIC_ENTRY.unpack_from(data, offset + i * 16)
                if tag == DT_NEEDED:
                    needed.append(cstr(str_off, val))
    return sorted(set(imports)), needed


# ---------------------------------------------------------------- names looked up at run time

_CALL = re.compile(r"\b(ps5tm_dynsym_in|ps5tm_dynsym|kernel_dynlib_dlsym|dlsym)\s*\(((?:[^()]|\([^()]*\))*)\)", re.S)
_LIT = re.compile(r'"((?:[^"\\]|\\.)*)"')
_DEF = re.compile(r'#define\s+(\w+)\s+"([^"]+)"')


def runtime_names(src_dir):
    """{name: set of source files} of the sce* names passed to dlsym() / ps5tm_dynsym()."""
    found = {}
    for path in sorted(glob.glob(os.path.join(src_dir, "*.c"))):
        with open(path, encoding="utf-8", errors="replace") as f:
            text = f.read()
        defs = dict(_DEF.findall(text))
        for m in _CALL.finditer(text):
            for part in re.split(r",(?![^()]*\))", m.group(2)):
                part = part.strip()
                lits = _LIT.findall(part)
                value = "".join(lits) if lits else defs.get(part)
                if value and re.fullmatch(r"sce[A-Za-z0-9_]+", value):
                    found.setdefault(value, set()).add(os.path.basename(path))
    return found


# ---------------------------------------------------------------- the libraries of a firmware

class _Lib:
    """One system library (PS5 ELF64 with the SCE dynamic tags), read through a memory map."""

    def __init__(self, path):
        f = open(path, "rb")
        try:
            self.m = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)
        except ValueError:
            f.close()
            raise
        self.f = f
        try:
            self._parse()
        except Exception:
            self.close()
            raise

    def close(self):
        try:
            self.m.close()
        finally:
            self.f.close()

    def _file(self, address, size=1):
        for vaddr, off, fsz in self.loads:
            rel = address - vaddr
            if 0 <= rel and rel + size <= fsz:
                return off + rel
        raise ValueError("address 0x%x is not in the file" % address)

    def _parse(self):
        m = self.m
        if len(m) < ELF_HEADER.size or m[:4] != b"\x7fELF" or m[4] != 2 or m[5] != 1:
            raise ValueError("not a little-endian ELF64 file")
        hdr = ELF_HEADER.unpack_from(m)
        phoff, phentsize, phnum = hdr[5], hdr[9], hdr[10]
        if phentsize < PROGRAM_HEADER.size or phoff + phentsize * phnum > len(m):
            raise ValueError("program headers outside the file")
        self.loads, dyn = [], None
        for i in range(phnum):
            p_type, _fl, p_off, p_vaddr, _pa, p_filesz, _ms, _al = PROGRAM_HEADER.unpack_from(m, phoff + i * phentsize)
            if p_off + p_filesz > len(m):
                raise ValueError("segment outside the file")
            if p_type == PT_LOAD:
                self.loads.append((p_vaddr, p_off, p_filesz))
            elif p_type == PT_DYNAMIC:
                dyn = (p_off, p_filesz)
        if not self.loads or not dyn:
            raise ValueError("no PT_LOAD or PT_DYNAMIC")
        tags = {}
        for off in range(dyn[0], dyn[0] + dyn[1], 16):
            tag, val = DYNAMIC_ENTRY.unpack_from(m, off)
            if tag == DT_NULL:
                break
            tags.setdefault(tag, val)
        for need in (DT_STRTAB, DT_STRSZ, DT_SYMTAB, DT_SYMENT):
            if need not in tags:
                raise ValueError("dynamic table has no tag 0x%x" % need)
        self.strsz = tags[DT_STRSZ]
        self.strtab = self._file(tags[DT_STRTAB], self.strsz)
        self.syment = tags[DT_SYMENT]
        if self.syment < SYMBOL_ENTRY.size:
            raise ValueError("bad DT_SYMENT")
        count = 0
        if DT_HASH in tags:
            _nb, count = struct.unpack_from("<II", m, self._file(tags[DT_HASH], 8))
        if not count and tags.get(DT_SCE_SYMTABSZ) and tags[DT_SCE_SYMTABSZ] % self.syment == 0:
            count = tags[DT_SCE_SYMTABSZ] // self.syment
        if not count:
            raise ValueError("symbol count unknown")
        self.symtab = self._file(tags[DT_SYMTAB], count * self.syment)
        self.count = count

    def export_nids(self):
        out = set()
        for i in range(1, self.count):
            st_name, st_info, _oth, shndx, _val, _sz = SYMBOL_ENTRY.unpack_from(self.m, self.symtab + i * self.syment)
            if not st_name or shndx == 0 or (st_info >> 4) == 0 or st_name >= self.strsz:
                continue
            start = self.strtab + st_name
            end = self.m.find(b"\0", start, self.strtab + self.strsz)
            if end < 0:
                continue
            raw = self.m[start:end].decode("utf-8", "replace")
            parts = raw.rsplit("#", 2)
            if len(parts) == 3 and len(parts[0]) == 11 and all(c in ID_ALPHABET for c in parts[1] + parts[2]):
                out.add(parts[0])
        return out


def _lib_files(fw_dir):
    """Libraries of one firmware folder: every .sprx/.prx below a folder named lib, except the modules of the applications."""
    found = []
    for dirpath, dirnames, filenames in os.walk(fw_dir):
        rel = os.path.relpath(dirpath, fw_dir).replace("\\", "/")
        parts = [p.lower() for p in rel.split("/")]
        if SKIP_DIR_PARTS & set(parts) or not any(LIB_DIR.search(p) for p in parts):
            continue
        for fn in filenames:
            if fn.lower().endswith((".sprx", ".prx")):
                found.append(os.path.join(dirpath, fn))
    return sorted(found)


def scan_firmware(fw_dir, cache_dir=None, name=None):
    """{'nids': {nid: [library file names]}, 'libs': n, 'unreadable': m}. The result is kept in cache_dir as long as the files do not change."""
    files = _lib_files(fw_dir)
    finger = hashlib.sha1()
    for p in files:
        st = os.stat(p)
        finger.update(("%s|%d|%d\n" % (os.path.relpath(p, fw_dir), st.st_size, st.st_mtime_ns)).encode("utf-8", "replace"))
    fp = finger.hexdigest()
    cache = os.path.join(cache_dir, "%s.json" % name) if cache_dir and name else None
    if cache and os.path.exists(cache):
        try:
            with open(cache, encoding="utf-8") as f:
                c = json.load(f)
            if c.get("fingerprint") == fp and c.get("version") == 1:
                return c["result"]
        except (OSError, ValueError, KeyError):
            pass
    nids, libs, bad = {}, 0, 0
    for p in files:
        try:
            lib = _Lib(p)
        except (ValueError, OSError, struct.error):
            bad += 1
            continue
        try:
            for n in lib.export_nids():
                nids.setdefault(n, []).append(os.path.basename(p))
            libs += 1
        except (ValueError, struct.error):
            bad += 1
        finally:
            lib.close()
    result = {"nids": nids, "libs": libs, "unreadable": bad}
    if cache:
        try:
            os.makedirs(cache_dir, exist_ok=True)
            with open(cache, "w", encoding="utf-8") as f:
                json.dump({"version": 1, "fingerprint": fp, "result": result}, f)
        except OSError:
            pass
    return result


# ---------------------------------------------------------------- evaluation

def evaluate(imports, needed, runtime, fws):
    """fws: {version: scan result}. Returns the report dict."""
    readable = sorted((v for v, r in fws.items() if r["libs"] > 0 and r["nids"]), key=fw_key)
    unreadable = sorted((v for v in fws if v not in readable), key=fw_key)
    report = {"firmwares": readable, "unreadable": unreadable, "static": [], "runtime": [], "problems": 0}
    need_set = set(needed)

    def row(name, kind, extra=None):
        n = nid(name)
        present = [v for v in readable if n in fws[v]["nids"]]
        missing = [v for v in readable if v not in present]
        r = {"name": name, "nid": n, "kind": kind, "present": len(present), "first": present[0] if present else None, "missing": missing}
        if extra:
            r.update(extra)
        return r

    for name, bind in imports:
        r = row(name, "static", {"binding": bind})
        r["outside_needed"] = [v for v in readable if nid(name) in fws[v]["nids"] and not (set(fws[v]["nids"][nid(name)]) & need_set)] if need_set else []
        if bind == "GLOBAL" and r["missing"]:
            report["problems"] += 1
            r["verdict"] = "fehlt"
        elif r["missing"]:
            r["verdict"] = "optional"
        else:
            r["verdict"] = "ok"
        report["static"].append(r)
    for name, files in sorted(runtime.items()):
        r = row(name, "runtime", {"files": sorted(files)})
        if not r["missing"]:
            r["verdict"] = "ok"
        elif name in KNOWN_OPTIONAL:
            r["verdict"] = "bekannt"
            r["reason"] = KNOWN_OPTIONAL[name]
        else:
            r["verdict"] = "fehlt"
            report["problems"] += 1
        report["runtime"].append(r)
    return report


def _ranges(versions, universe):
    """'12.00-12.70' style text for a list of versions taken from the ordered universe."""
    idx = [universe.index(v) for v in versions if v in universe]
    out, i = [], 0
    while i < len(idx):
        j = i
        while j + 1 < len(idx) and idx[j + 1] == idx[j] + 1:
            j += 1
        out.append(universe[idx[i]] if i == j else "%s-%s" % (universe[idx[i]], universe[idx[j]]))
        i = j + 1
    return ", ".join(out)


def print_report(rep, verbose):
    fw = rep["firmwares"]
    print("Firmwares gelesen: %d (%s bis %s)" % (len(fw), fw[0], fw[-1]) if fw else "Keine Firmware lesbar.")
    if rep["unreadable"]:
        print("Nicht lesbar (verschlüsselt oder leer): %s" % ", ".join(rep["unreadable"]))
    st, rt = rep["static"], rep["runtime"]
    strong = [r for r in st if r.get("binding") == "GLOBAL"]
    weak = [r for r in st if r.get("binding") == "WEAK"]
    print("\nBeim Laden gebunden: %d starke Importe, %d schwache (optionale)" % (len(strong), len(weak)))
    print("  starke Importe in allen gelesenen Firmwares vorhanden: %d von %d" % (sum(1 for r in strong if not r["missing"]), len(strong)))
    for r in strong:
        if r["missing"]:
            print("  FEHLT  %-40s in %s" % (r["name"], _ranges(r["missing"], fw)))
    outside = [r for r in strong if r["outside_needed"] and not r["missing"]]
    if outside:
        print("  Hinweis: %d Importe stehen nicht in den gelinkten Bibliotheken (nur anderswo im Dump)" % len(outside))
    for r in weak:
        if r["missing"] and (verbose or r["present"]):
            print("  optional  %-38s fehlt in %s" % (r["name"], _ranges(r["missing"], fw)))
    never = [r["name"] for r in weak if not r["present"]]
    if never:
        print("  optional, in keiner Firmware gefunden: %s" % ", ".join(never))

    print("\nZur Laufzeit gesucht: %d Namen" % len(rt))
    for r in rt:
        if r["verdict"] == "ok":
            if verbose:
                print("  ok      %-40s ab %s" % (r["name"], r["first"]))
        elif r["verdict"] == "bekannt":
            print("  bekannt %-40s fehlt in %s: %s" % (r["name"], _ranges(r["missing"], fw), r["reason"]))
        else:
            print("  FEHLT   %-40s (%s) %s" % (r["name"], ",".join(r["files"]),
                                              "in keiner Firmware" if not r["present"] else "fehlt in " + _ranges(r["missing"], fw)))
    print("\nErgebnis: %s" % ("nichts Unerwartetes fehlt." if not rep["problems"] else "%d Namen fehlen unerwartet." % rep["problems"]))
    print("Grenzen: Vorhanden heißt, dass die Firmware die Funktion exportiert; Argumente und Verhalten sind damit nicht belegt. "
          "Firmwares ohne Dump sind nicht abgedeckt.")


def main(argv=None):
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.dirname(here)
    ap = argparse.ArgumentParser(description="Check the system functions the app uses against the firmware dumps.")
    ap.add_argument("--fw-dir", default=os.environ.get("PS5_FW_DIR", r"C:\FW 1.00-12.70"))
    ap.add_argument("--elf", default=os.path.join(root, "PS5_Cooling_Center.elf"))
    ap.add_argument("--src", default=os.path.join(root, "src"))
    ap.add_argument("--fw", nargs="*", default=[])
    ap.add_argument("--verbose", "-v", action="store_true")
    ap.add_argument("--json", action="store_true")
    ap.add_argument("--no-cache", action="store_true")
    args = ap.parse_args(argv)
    for stream in (sys.stdout, sys.stderr):                  # umlauts survive a pipe or a file on Windows, too
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8", errors="replace")

    if not os.path.isdir(args.fw_dir):
        print("Ordner mit den Firmware-Dumps nicht gefunden: %s (--fw-dir oder PS5_FW_DIR)" % args.fw_dir, file=sys.stderr)
        return 2
    if not os.path.isfile(args.elf):
        print("ELF nicht gefunden: %s (--elf)" % args.elf, file=sys.stderr)
        return 2
    versions = sorted((d for d in os.listdir(args.fw_dir) if re.fullmatch(r"\d+\.\d+", d) and os.path.isdir(os.path.join(args.fw_dir, d))), key=fw_key)
    if args.fw:
        versions = [v for v in versions if v in args.fw]
    if not versions:
        print("Keine Firmware-Ordner (z. B. 12.70) in %s" % args.fw_dir, file=sys.stderr)
        return 2

    imports, needed = payload_imports(args.elf)
    runtime = runtime_names(args.src) if os.path.isdir(args.src) else {}
    cache_dir = None if args.no_cache else os.path.join(os.path.expanduser("~"), ".cache", "ps5cc-fw-imports")
    fws = {}
    for v in versions:
        if not args.json:
            print("lese %s ..." % v, file=sys.stderr, flush=True)
        fws[v] = scan_firmware(os.path.join(args.fw_dir, v), cache_dir, v)

    rep = evaluate(imports, needed, runtime, fws)
    if not rep["firmwares"]:
        print("Keine Firmware lesbar.", file=sys.stderr)
        return 2
    # the rule is checked against itself: in the newest firmware nearly every import must be found, or the NID rule has gone wrong
    newest = rep["firmwares"][-1]
    hit = sum(1 for r in rep["static"] if r["binding"] == "GLOBAL" and newest not in r["missing"])
    strong = sum(1 for r in rep["static"] if r["binding"] == "GLOBAL")
    if strong and hit < 0.8 * strong:
        print("Das NID-Verfahren passt nicht zu den Dumps (nur %d von %d starken Importen in %s gefunden); keine Aussage möglich." % (hit, strong, newest),
              file=sys.stderr)
        return 2
    if args.json:
        json.dump(rep, sys.stdout, indent=1, ensure_ascii=False)
        print()
    else:
        print_report(rep, args.verbose)
    return 1 if rep["problems"] else 0


if __name__ == "__main__":
    sys.exit(main())
