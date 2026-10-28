#!/usr/bin/env bash
# Builds the two tiles for /data/homebrew:
#   starter/out/FAKE86921/  the full-screen app (tvapp/, built on ps5-homebrew-ui): starts the app when it is not running (the app ELF
#                           is embedded in eboot.bin) and shows cooling, games, jobs, settings, payloads and logs with the controller.
#                           Identity in tvapp/sce_sys/param.json.
#   starter/out/FAKE86922/  the browser tile (starter/browser/, a freestanding native title): starts the app when needed (ELF
#                           embedded as well) and opens the web UI in the console's browser. Identity in starter/browser/sce_sys.
# Both take the home-screen backgrounds from starter/sce_sys.
# Usage: tools/make_starter.sh [payload.elf]   (default: PS5_Cooling_Center.elf). Needs WSL (Ubuntu-26.04) with clang, clang-18,
# ninja, Python and ps5-native-tool; the first run fetches the pinned ps5-opengl SDK (tvapp/tools/fetch-opengl-sdk.sh checks its sha256).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PAYLOAD="${1:-$ROOT/PS5_Cooling_Center.elf}"
DISTRO="${PS5TM_WSL_DISTRO:-Ubuntu-26.04}"
wsl_path() { cygpath -u "$(cd "$(dirname "$1")" && pwd)/$(basename "$1")" | sed 's#^/\([a-z]\)/#/mnt/\1/#'; }
WROOT="$(cygpath -u "$ROOT" | sed 's#^/\([a-z]\)/#/mnt/\1/#')"
WPAYLOAD="$(wsl_path "$PAYLOAD")"
rm -rf "$ROOT/starter/out"

# full-screen tile
sed -i 's/\r$//' "$ROOT/tools/tvapp-build.sh"
MSYS_NO_PATHCONV=1 wsl -d "$DISTRO" -- bash "$WROOT/tools/tvapp-build.sh" "$WROOT" "$WPAYLOAD" "$WROOT/starter/out/FAKE86921"

# browser tile
B="$ROOT/starter/browser"
cp "$PAYLOAD" "$B/payload.elf"
sed -i 's/\r$//' "$B/build_title.sh"
MSYS_NO_PATHCONV=1 wsl -d "$DISTRO" -- bash -c "PS5_PAYLOAD_SDK='$WROOT/PS5_PAYLOAD_SDK' bash '$WROOT/starter/browser/build_title.sh'" | tail -2
OUT="$ROOT/starter/out/FAKE86922"
mkdir -p "$OUT/sce_sys" "$OUT/sce_module"
cp "$B/eboot.bin" "$OUT/eboot.bin"
cp "$B/sce_module/libc.prx" "$OUT/sce_module/"
cp "$B/sce_sys/param.json" "$B/sce_sys/icon0.png" "$ROOT/starter/sce_sys/pic0.png" "$ROOT/starter/sce_sys/pic1.png" "$OUT/sce_sys/"

for t in FAKE86921 FAKE86922; do echo "ok: starter/out/$t"; ls -l "$ROOT/starter/out/$t/eboot.bin"; done
