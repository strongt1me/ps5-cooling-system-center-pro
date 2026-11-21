#!/bin/sh
# Host test of src/ejectmark.c (the marker of an eject that did not come back) - Linux/WSL, gcc:
#   sh test/ejectmark/run.sh
set -e
cd "$(dirname "$0")/../.."
OUT=${EJECTMARKOUT:-$HOME/.cache/ps5tm-ejectmark}
mkdir -p "$OUT"
gcc -std=gnu11 -D_DEFAULT_SOURCE -O1 -g -fsanitize=address,undefined -Wall -Wextra -Isrc -Isrc/third_party \
  -DEJECTMARK_PATH="\"$OUT/usb_eject_offen.txt\"" -DEJECTMARK_LATER_MS=120 \
  -o "$OUT/test" test/ejectmark/mark_test.c src/ejectmark.c -lpthread
"$OUT/test"
