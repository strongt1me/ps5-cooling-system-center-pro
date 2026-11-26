#!/usr/bin/env bash
# Dump Forge (quer3q, GPL-3.0-or-later): the pinned PS5 program that writes the debug FPKG (format "FPKG (Debug-.pkg)").
# Downloads the release's ps5-dump-forge-<version>-ps5.elf into gen/dumpforge/ (ignored by git), checks it against the pinned SHA-256
# (the one in the release's SHA256SUMS) and prints its path. Since 21.11.2026 the app carries the program itself, as a zstd frame in
# third_party/dump_forge (src/dumpforge.c): tools/pack-dumpforge.sh calls this script and makes that frame. A newer release: change version and
# sha256 here, run tools/pack-dumpforge.sh, change DF_ZST in the Makefile, third_party/dump_forge/ORIGIN.md and tools/upstream.json.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
version=0.6.0
sha256=420265c626d517ed433ecf360092dc28e1061fd0ac29346de488c41cde3ac0d2
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
