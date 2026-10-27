#!/usr/bin/env bash
# Runs inside WSL: builds the tile's full-screen app (tvapp/) in a build tree on the Linux file system and copies the title folder out.
# Usage: tvapp-build.sh <repo root> <payload.elf> <out dir>   (all as WSL paths; called by tools/make_starter.sh)
# The build tree keeps .deps (the pinned ps5-opengl SDK, checked by sha256 when fetched) between runs; PS5TM_TVAPP_TREE moves it.
set -euo pipefail
REPO=$1
PAYLOAD=$2
OUT=$3
TREE=${PS5TM_TVAPP_TREE:-$HOME/ps5cc-tvapp}
mkdir -p "$TREE"
rsync -a --delete --exclude build --exclude dist --exclude .deps --exclude out --exclude .local "$REPO/tvapp/" "$TREE/"
cd "$TREE"
[[ -d .deps/ps5-opengl/current ]] || make deps
TITLE=$(python3 -c 'import json;print(json.load(open("sce_sys/param.json"))["titleId"])')
USE_CCACHE=0 make app PAYLOAD_ELF="$PAYLOAD" | tail -3
rm -rf "$OUT"
mkdir -p "$OUT"
cp -r "dist/$TITLE/." "$OUT/"
# the home-screen backgrounds of the tile as before
cp "$REPO/starter/sce_sys/pic0.png" "$REPO/starter/sce_sys/pic1.png" "$OUT/sce_sys/"
