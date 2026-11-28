#!/usr/bin/env bash
# The host snapshots of the fullscreen app for the README pictures (28.11.2026); runs in the WSL build tree of the fullscreen app.
#
#   MSYS_NO_PATHCONV=1 wsl -d Ubuntu-26.04 bash "/mnt/c/.../tools/demo/tv_readme_snapshots.sh" <de|en> <output folder, as WSL path>
#   python tools/demo/make_readme_images.py --tv-de <folder> --tv-en <folder>     (then, on Windows)
#
# The sample data of the fullscreen app (tvapp/src/ps5cc/mock_source.cpp) carry the game list of a test console with real titles,
# and the version of the time they were written. The README promises made-up games, so the titles become invented ones and the
# version the one of src/ps5tm.h - in the build tree only (~/ps5cc-tvapp, or PS5CC_TVAPP_TREE); the next sync puts the file back.
set -euo pipefail

LANGUAGE="${1:?de or en}"
OUT="${2:?output folder}"
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TREE="${PS5CC_TVAPP_TREE:-$HOME/ps5cc-tvapp}"
VERSION="$(sed -n 's/^#define[[:space:]]*PS5TM_VERSION[[:space:]]*"\([^"]*\)".*/\1/p' "$REPO/src/ps5tm.h")"
[ -n "$VERSION" ] || { echo "no PS5TM_VERSION in src/ps5tm.h" >&2; exit 1; }

rsync -a --delete --exclude build --exclude dist --exclude .deps --exclude out --exclude .local "$REPO/tvapp/" "$TREE/"
restore() { rsync -a "$REPO/tvapp/src/ps5cc/mock_source.cpp" "$TREE/src/ps5cc/mock_source.cpp"; }
trap restore EXIT

python3 - "$TREE/src/ps5cc/mock_source.cpp" "$VERSION" <<'PYEOF'
import re, sys
path, version = sys.argv[1], sys.argv[2]
names = [("Crash Bandicoot 4: It's About Time", "Nebula Drift"), ("Crash Bandicoot 4", "Nebula Drift"), ("ASTRO BOT", "Echoes of Ember"),
         ("Maneater", "Stonewatch"), ("Moorhuhn Remake", "Meadow Run"), ("LEGO Batman: Legacy of the Dark Knight", "Pixel Knights"),
         ("Asterix & Obelix Heroes", "Copper Canyon"), ("Double Dragon Revive", "Twin Blades"), ("Arcade Game Zone", "Arcade Corner"),
         ("Two Point Hospital", "Quiet Ward"), ("Arkanoid – Eternal Battle", "Brick Comet"), ("Styx: Blades of Greed", "Shadow Thief"),
         ("Styx_ Blades of Greed", "Shadow Thief")]
s = open(path, encoding="utf-8").read()
for real, made_up in names:
    s = s.replace(real, made_up)
s, n = re.subn(r'(data_\.(?:app|tile)_version = ")[^"]*(")', lambda m: m.group(1) + version + m.group(2), s)
if n != 2:
    sys.exit("mock_source.cpp: expected app_version and tile_version, found %d" % n)
open(path, "w", encoding="utf-8").write(s)
PYEOF

cd "$TREE"
rm -rf build/snapshots
if [ "$LANGUAGE" = "de" ]; then
  USE_CCACHE=0 PS5CC_LANG=de tools/host-snapshots.sh build/snapshots ps5cc
else
  USE_CCACHE=0 PS5CC_LANG="$LANGUAGE" PS5CC_LANG_FILES="$REPO/web/lang/$LANGUAGE.json:$TREE/assets/lang/$LANGUAGE.json" \
    tools/host-snapshots.sh build/snapshots ps5cc
fi
mkdir -p "$OUT"
for s in kuehlung spiele dateien-ordner payloads-laufend spielzeit; do
  cp "build/snapshots/01-ps5cc-$s.png" "$OUT/"
done
echo "ok: 5 snapshots ($LANGUAGE, version $VERSION) in $OUT"
