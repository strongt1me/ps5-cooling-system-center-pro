#!/bin/sh
# Host test of src/fpkgdup.c (a game at Kraken Mount and ShadowMountPlus is unmounted at ShadowMountPlus after it ended) - Linux/WSL, gcc:
#   sh test/fpkgdup/run.sh
set -e
cd "$(dirname "$0")/../.."
OUT=${FPKGDUPOUT:-$HOME/.cache/ps5tm-fpkgdup}
mkdir -p "$OUT"
gcc -std=gnu11 -D_DEFAULT_SOURCE -O1 -g -fsanitize=address,undefined -Wall -Wextra -Isrc -Isrc/third_party \
  -o "$OUT/test" test/fpkgdup/fpkgdup_test.c src/fpkgdup.c src/third_party/cJSON.c -lpthread -lm
"$OUT/test"
