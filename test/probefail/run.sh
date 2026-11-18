#!/bin/sh
# Host test of src/probefail.h (memory of image files the direct reader could not read) - Linux/WSL, gcc:
#   sh test/probefail/run.sh
set -e
cd "$(dirname "$0")/../.."
OUT=${PROBEFAILOUT:-$HOME/.cache/ps5tm-probefail}
mkdir -p "$OUT"
gcc -std=gnu11 -O1 -g -fsanitize=address,undefined -Wall -Wextra -Isrc -o "$OUT/test" test/probefail/probefail_test.c
"$OUT/test"
