#!/bin/sh
# Host test of src/unpackfb.h (the app unpacks itself when ShadowMountPlus ends with "Permission denied") - Linux/WSL, gcc:
#   sh test/unpackfb/run.sh
set -e
cd "$(dirname "$0")/../.."
OUT=${UNPACKFBOUT:-$HOME/.cache/ps5tm-unpackfb}
mkdir -p "$OUT"
gcc -std=gnu11 -D_DEFAULT_SOURCE -O1 -g -fsanitize=address,undefined -Wall -Wextra -Isrc -o "$OUT/test" test/unpackfb/unpackfb_test.c
"$OUT/test"
