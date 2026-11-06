#!/bin/sh
# Host test of src/smpcfg.c (ShadowMountPlus's config.ini: update_emulators, flat and v2) – Linux/WSL, gcc:
#   sh test/smpcfg/run.sh
set -e
cd "$(dirname "$0")/../.."
OUT=${SMPCFGOUT:-$HOME/.cache/ps5tm-smpcfg}
mkdir -p "$OUT"
gcc -std=c11 -D_DEFAULT_SOURCE -O1 -g -fsanitize=address,undefined -Wall -Wextra -Werror -Isrc \
  -o "$OUT/test" test/smpcfg/test.c src/smpcfg.c
"$OUT/test" "$OUT/work"
