#!/bin/sh
# Host test of src/cvsample.h (the pieces kept for the quick check of an exFAT image) – Linux/WSL, gcc:
#   sh test/cvsample/run.sh
set -e
cd "$(dirname "$0")/../.."
OUT=${CVSAMPLEOUT:-$HOME/.cache/ps5tm-cvsample}
mkdir -p "$OUT"
gcc -std=gnu11 -D_DEFAULT_SOURCE -O1 -g -fsanitize=address,undefined -Wall -Wextra -Werror -Isrc -o "$OUT/test" test/cvsample/test.c
"$OUT/test"
