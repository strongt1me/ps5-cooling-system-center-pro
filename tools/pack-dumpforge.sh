#!/usr/bin/env bash
# Makes the zstd frame of Dump Forge that goes into the app ELF (21.11.2026; src/dumpforge.c, tools/gen_zstd_blob.py, Makefile DF_ZST).
#
#   tools/pack-dumpforge.sh
#
# 1. tools/fetch-dumpforge.sh downloads the pinned release file (ps5-dump-forge-<version>-ps5.elf) and checks its SHA-256 against the pin.
# 2. zstd -19 packs it from the FILE (not through a pipe: the frame must carry the size of the program, gen_zstd_blob.py reads it from there)
#    into third_party/dump_forge/ps5-dump-forge-<version>-ps5.elf.zst (17.7 MiB -> 8.0 MiB for 0.5.6 and 0.5.7).
# 3. The frame is unpacked again and its SHA-256 compared with the pin; the pin goes into the .sha256 beside the frame.
# 4. Frames of other versions in that folder are removed (the Makefile names exactly one: DF_ZST).
# Needs the zstd tool (WSL: apt install zstd). Afterwards: change DF_ZST in the Makefile, third_party/dump_forge/ORIGIN.md, tools/upstream.json.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
command -v zstd >/dev/null || { echo "pack-dumpforge: the zstd tool is missing" >&2; exit 2; }
elf="$(bash "$root/tools/fetch-dumpforge.sh")"
name="$(basename "$elf")"
pin="$(sha256sum "$elf" | cut -d' ' -f1)"
dest="$root/third_party/dump_forge/$name.zst"
zstd -19 -q -f -o "$dest" "$elf"
back="$(zstd -d -q -c "$dest" | sha256sum | cut -d' ' -f1)"
[[ "$back" == "$pin" ]] || { echo "pack-dumpforge: the unpacked frame does not match the pinned SHA-256" >&2; rm -f "$dest"; exit 2; }
printf '%s  %s\n' "$pin" "$name" > "${dest%.zst}.sha256"   # <name>.elf.sha256: the name the Makefile (DF_ZST:.zst=.sha256) and gen_zstd_blob.py read
for f in "$root"/third_party/dump_forge/ps5-dump-forge-*-ps5.elf.zst; do
  [[ "$f" == "$dest" ]] || { rm -f -- "$f" "${f%.zst}.sha256"; echo "removed $(basename "$f")" >&2; }
done
ls -l "$elf" "$dest" | awk '{print $5, $9}'
