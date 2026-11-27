#!/bin/sh
# Host test of src/krakenmig.h (FPKG-Games becomes Kraken-Games: the config line, the folders) - Linux/WSL, gcc:
#   sh test/krakenmig/run.sh
set -e
cd "$(dirname "$0")/../.."
OUT=${KRAKENMIGOUT:-$HOME/.cache/ps5tm-krakenmig}
mkdir -p "$OUT"
gcc -std=gnu11 -O1 -g -fsanitize=address,undefined -Wall -Wextra -Isrc -o "$OUT/test" test/krakenmig/krakenmig_test.c
TMPDIR="$OUT" "$OUT/test"
