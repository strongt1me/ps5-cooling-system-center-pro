#!/usr/bin/env bash
# Builds the dump folder of the starter app for /data/homebrew: starter/out/PPSA86921/
#   eboot.bin           native PS5 title (starter/title/starter.c, built in WSL by starter/build_title.sh, signed)
#   payload.elf         the app (read by the starter from /app0 and sent to the loader)
#   sce_module/libc.prx demanded by the console's app loader (starter/sce_module/NOTICE.txt)
#   sce_sys/            param.json, icon0.png, pic0.png, pic1.png
# Usage: tools/make_starter.sh [payload.elf]   (default: PS5_Cooling_Center.elf). Needs WSL with clang-18 and ps5-native-tool.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PAYLOAD="${1:-$ROOT/PS5_Cooling_Center.elf}"
DISTRO="${PS5TM_WSL_DISTRO:-Ubuntu-26.04}"
WROOT="$(cygpath -u "$ROOT" | sed 's#^/\([a-z]\)/#/mnt/\1/#')"
sed -i 's/\r$//' "$ROOT/starter/build_title.sh"
MSYS_NO_PATHCONV=1 wsl -d "$DISTRO" -- bash -c "PS5_PAYLOAD_SDK='$WROOT/PS5_PAYLOAD_SDK' bash '$WROOT/starter/build_title.sh'" | tail -4
OUT="$ROOT/starter/out/PPSA86921"
rm -rf "$ROOT/starter/out"; mkdir -p "$OUT/sce_sys" "$OUT/sce_module"
cp "$ROOT/starter/eboot.bin" "$OUT/eboot.bin"
cp "$PAYLOAD" "$OUT/payload.elf"
cp "$ROOT/starter/sce_module/libc.prx" "$OUT/sce_module/"
cp "$ROOT/starter/sce_sys/"* "$OUT/sce_sys/"
echo "ok: $OUT"; ls -lR "$OUT"
