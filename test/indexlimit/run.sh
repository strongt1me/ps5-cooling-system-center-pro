#!/bin/sh
# Host test of src/indexlimit.c (will the lookup tables of a zstd pack fit into AMPR EMU NG) - Linux/WSL, gcc:
#   sh test/indexlimit/run.sh
set -e
cd "$(dirname "$0")/../.."
OUT=${INDEXLIMITOUT:-$HOME/.cache/ps5tm-indexlimit}
mkdir -p "$OUT"
gcc -std=gnu11 -D_DEFAULT_SOURCE -O1 -g -fsanitize=address,undefined -Wall -Wextra -Isrc -o "$OUT/test" test/indexlimit/indexlimit_test.c src/indexlimit.c
"$OUT/test"
