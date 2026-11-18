#!/bin/sh
# Host test of src/fpkgprogress.h (the progress of a debug FPKG job as the page shows it) - Linux/WSL, gcc:
#   sh test/fpkgprogress/run.sh
set -e
cd "$(dirname "$0")/../.."
OUT=${FPKGPROGRESSOUT:-$HOME/.cache/ps5tm-fpkgprogress}
mkdir -p "$OUT"
gcc -std=gnu11 -O1 -g -fsanitize=address,undefined -Wall -Wextra -Isrc -o "$OUT/test" test/fpkgprogress/fpkgprogress_test.c
"$OUT/test"
