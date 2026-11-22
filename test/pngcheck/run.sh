#!/bin/sh
# Host test of src/pngcheck.h (is a PNG of a game's sce_sys whole) - Linux/WSL, gcc:
#   sh test/pngcheck/run.sh
# Besides the built cases, every PNG of the repository's web/ and sce_sys folders must pass (pictures of real encoders).
set -e
cd "$(dirname "$0")/../.."
OUT=${PNGCHECKOUT:-$HOME/.cache/ps5tm-pngcheck}
mkdir -p "$OUT"
gcc -std=gnu11 -O1 -g -fsanitize=address,undefined -Wall -Wextra -Werror -Isrc -o "$OUT/test" test/pngcheck/pngcheck_test.c
find web starter/sce_sys tvapp/sce_sys -name '*.png' -type f -print0 2>/dev/null | xargs -0 timeout 60 "$OUT/test"
