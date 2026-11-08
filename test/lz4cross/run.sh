#!/bin/sh
# Host test for swapping the LZ4 library – Linux/WSL, gcc:
#   sh test/lz4cross/run.sh <dir of the other version (lz4.c lz4.h lz4hc.c lz4hc.h)>
# The version in src/third_party/lz4 and the other one write blocks (fast and HC levels, many sizes and kinds of data) and read each
# other's, both ways; a block that does not decode to the same bytes fails the test.
set -e
cd "$(dirname "$0")/../.."
OTHER=${1:?usage: run.sh <dir with the other lz4 version>}
OUT=${LZ4CROSSOUT:-$HOME/.cache/ps5tm-lz4cross}
mkdir -p "$OUT"
gcc -O2 -w -Isrc/third_party/lz4 -o "$OUT/cur" test/lz4cross/test.c src/third_party/lz4/lz4.c src/third_party/lz4/lz4hc.c
gcc -O2 -w -I"$OTHER" -o "$OUT/other" test/lz4cross/test.c "$OTHER/lz4.c" "$OTHER/lz4hc.c"
"$OUT/cur" enc "$OUT/cur.blocks"
"$OUT/other" enc "$OUT/other.blocks"
echo "-- the app's version reads its own and the other's:"
"$OUT/cur" dec "$OUT/cur.blocks"
"$OUT/cur" dec "$OUT/other.blocks"
echo "-- the other version reads its own and the app's:"
"$OUT/other" dec "$OUT/other.blocks"
"$OUT/other" dec "$OUT/cur.blocks"
