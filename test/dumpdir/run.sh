#!/bin/sh
# Host test of src/dumpdir.h (the folder name of the dump settings) - Linux/WSL, gcc:
#   sh test/dumpdir/run.sh
set -e
cd "$(dirname "$0")/../.."
OUT=${DUMPDIROUT:-$HOME/.cache/ps5tm-dumpdir}
mkdir -p "$OUT"
gcc -std=gnu11 -O1 -g -fsanitize=address,undefined -Wall -Wextra -Isrc -o "$OUT/test" test/dumpdir/dumpdir_test.c
"$OUT/test"
