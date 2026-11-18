#!/bin/sh
# Host test of src/vercmp.h (comparing version texts from outside) - Linux/WSL, gcc:
#   sh test/vercmp/run.sh
set -e
cd "$(dirname "$0")/../.."
OUT=${VERCMPOUT:-$HOME/.cache/ps5tm-vercmp}
mkdir -p "$OUT"
gcc -std=gnu11 -O1 -g -fsanitize=address,undefined -Wall -Wextra -Isrc -o "$OUT/test" test/vercmp/vercmp_test.c
timeout 20 "$OUT/test"
