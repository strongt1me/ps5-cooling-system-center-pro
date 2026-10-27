#!/usr/bin/env bash
# Builds the tile for /data/homebrew: starter/out/PPSA86921/
# Since 27.10.2026 the tile is the full-screen app (tvapp/, built on ps5-homebrew-ui): it starts the app when it is not running
# (the app ELF is embedded in eboot.bin) and shows cooling, games and jobs with the controller; the web UI stays reachable from its
# menu. Identity in tvapp/sce_sys/param.json; backgrounds from starter/sce_sys.
# Usage: tools/make_starter.sh [payload.elf]   (default: PS5_Cooling_Center.elf). Needs WSL (Ubuntu-26.04) with clang, ninja and
# Python; the first run fetches the pinned ps5-opengl SDK (tools/fetch-opengl-sdk.sh in tvapp/ checks its sha256).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PAYLOAD="${1:-$ROOT/PS5_Cooling_Center.elf}"
DISTRO="${PS5TM_WSL_DISTRO:-Ubuntu-26.04}"
wsl_path() { cygpath -u "$(cd "$(dirname "$1")" && pwd)/$(basename "$1")" | sed 's#^/\([a-z]\)/#/mnt/\1/#'; }
WROOT="$(cygpath -u "$ROOT" | sed 's#^/\([a-z]\)/#/mnt/\1/#')"
WPAYLOAD="$(wsl_path "$PAYLOAD")"
OUT="$ROOT/starter/out/PPSA86921"
rm -rf "$ROOT/starter/out"
sed -i 's/\r$//' "$ROOT/tools/tvapp-build.sh"
MSYS_NO_PATHCONV=1 wsl -d "$DISTRO" -- bash "$WROOT/tools/tvapp-build.sh" "$WROOT" "$WPAYLOAD" "$WROOT/starter/out/PPSA86921"
echo "ok: $OUT"; ls -l "$OUT" "$OUT/sce_sys"
