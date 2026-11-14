#!/usr/bin/env bash
# Dump Forge (quer3q, GPL-3.0-or-later): the pinned PS5 program that writes the debug FPKG (format "FPKG (Debug-.pkg)").
# Downloads the release's ps5-dump-forge-<version>-ps5.elf into gen/dumpforge/ (ignored by git), checks it against the pinned SHA-256
# (the one in the release's SHA256SUMS) and prints its path. tools/make_starter.sh puts it into the folders of both tiles; the app
# itself does not embed it (it fetches it on a button press, see src/dumpforge.c). A newer release: change version and sha256 here,
# in third_party/dump_forge/ORIGIN.md and in tools/upstream.json.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
version=0.5.5
sha256=858b8ae2954341a3389e4a9f998c6a57d01bc4e4fb8f01a71c41719001c30be7
name="ps5-dump-forge-$version-ps5.elf"
url="https://github.com/quer3q/ps5-dump-forge/releases/download/v$version/$name"
cache="$root/gen/dumpforge"
file="$cache/$name"
mkdir -p "$cache"

ok() { [[ -f $file ]] && [[ "$(sha256sum "$file" | cut -d' ' -f1)" == "$sha256" ]]; }

if ! ok; then
    printf '==> [dumpforge] Downloading %s\n' "$name" >&2
    curl -fL --retry 3 -o "$file.part" "$url" >&2
    mv -- "$file.part" "$file"
    ok || {
        echo "Dump Forge $version: checksum mismatch" >&2
        rm -f -- "$file"
        exit 2
    }
fi
printf '%s\n' "$file"
