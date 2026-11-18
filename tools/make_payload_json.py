#!/usr/bin/env python3
"""Writes payload-manager.json: the repository list of this app for itsPLK's Payload Manager.

The Payload Manager reads a JSON list (name, filename, url, version, category,
description, last_update, checksum, same shape as itsPLK's payloads_v2.json) and
downloads the file from the "url" after checking the SHA-256 in "checksum".
tools/release.sh calls this after the build and uploads the result as a fifth
file of the GitHub release. The address
    https://github.com/<repo>/releases/latest/download/payload-manager.json
then always serves the list of the newest release, so the version, the link and
the checksum can never disagree with the ELF that was published.

    python tools/make_payload_json.py --elf <elf> --version X.Y.Z --repo owner/name --out <file> [--date YYYY-MM-DD]

The checksum is computed from the given ELF file, the link points at the release
asset of the same name. Exit code 0 only if the file was written and read back
correctly.
"""
import argparse
import datetime
import hashlib
import json
import os
import re
import sys

LIST_NAME = "PS5 Cooling & System Center - Pro"
DESCRIPTION = ("Fan control, temperatures, game tools and web UI for your PS5 "
               "(opens in the browser on port 8086)")
CATEGORY = "Utilities & Tools"


def fail(msg):
    print("FEHLER: " + msg, file=sys.stderr)
    sys.exit(1)


def file_sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--elf", required=True, help="the built ELF of this release")
    ap.add_argument("--version", required=True, help="X.Y.Z (without v)")
    ap.add_argument("--repo", required=True, help="GitHub repository, owner/name")
    ap.add_argument("--out", required=True, help="file to write")
    ap.add_argument("--date", help="last_update, YYYY-MM-DD (default: today, UTC)")
    a = ap.parse_args()

    version = a.version[1:] if a.version.startswith("v") else a.version
    if not re.fullmatch(r"\d+\.\d+\.\d+", version):
        fail("Ungültige Version: %s (erwartet X.Y.Z)" % a.version)
    if not re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", a.repo):
        fail("Ungültiges Repository: %s (erwartet owner/name)" % a.repo)
    date = a.date or datetime.datetime.now(datetime.timezone.utc).date().isoformat()
    if not re.fullmatch(r"\d{4}-\d{2}-\d{2}", date):
        fail("Ungültiges Datum: %s (erwartet YYYY-MM-DD)" % date)
    if not os.path.isfile(a.elf):
        fail("ELF nicht gefunden: %s" % a.elf)

    filename = os.path.basename(a.elf)
    if not re.fullmatch(r"[A-Za-z0-9_.-]+\.(elf|bin)", filename):
        fail("Dateiname nicht für den Payload Manager geeignet: %s" % filename)
    if ("v" + version) not in filename:
        fail("Der Dateiname %s nennt die Version v%s nicht" % (filename, version))
    checksum = file_sha256(a.elf)

    tag = "v" + version
    entry = {
        "name": LIST_NAME,
        "filename": filename,
        "url": "https://github.com/%s/releases/download/%s/%s" % (a.repo, tag, filename),
        "source": "https://github.com/%s/releases" % a.repo,
        "description": DESCRIPTION,
        "last_update": date,
        "version": tag,
        "category": CATEGORY,
        "checksum": checksum,
    }
    doc = {"name": LIST_NAME, "payloads": [entry]}
    text = json.dumps(doc, indent=2, ensure_ascii=False) + "\n"
    with open(a.out, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)

    # read it back the way the manager will see it
    back = json.load(open(a.out, encoding="utf-8"))
    e = back["payloads"][0]
    if e["checksum"] != checksum or e["checksum"] != file_sha256(a.elf):
        fail("Die Prüfsumme in %s passt nicht zur ELF" % a.out)
    if not re.fullmatch(r"[0-9a-f]{64}", e["checksum"]):
        fail("Die Prüfsumme in %s ist keine SHA-256" % a.out)
    print("ok: %s (%s, %s, sha256:%s...)" % (a.out, e["filename"], e["version"], checksum[:16]))


if __name__ == "__main__":
    main()
