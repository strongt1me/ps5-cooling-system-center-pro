#!/bin/sh
# Unit checks of the PS4Mount port (src/ps4m_*.c) against upstream's test/unit.c (Linux/WSL, gcc, python3).
#   sh test/ps4m/run.sh
set -e
cd "$(dirname "$0")/../.."
OUT=${P4MOUT:-$HOME/.cache/ps5tm-ps4m}   # not /tmp: WSL may clear it between calls
mkdir -p "$OUT"
python3 tools/gen_p4m_templates.py "$OUT/pdb_template.h" >/dev/null
gcc -std=c11 -D_DEFAULT_SOURCE -O1 -I"$OUT" -Isrc -o "$OUT/unit" third_party/ps4mount/test/unit.c \
  src/ps4m_pkg.c src/ps4m_build.c src/ps4m_crypto.c src/ps4m_origin.c
"$OUT/unit"
