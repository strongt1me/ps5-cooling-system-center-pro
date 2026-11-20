#!/bin/sh
# Host test of src/dfcheck.h (is a Dump Forge file of a folder whole: SHA-256 in pieces, ELF not cut off) - Linux/WSL, gcc:
#   sh test/dfcheck/run.sh                     (DF_REAL=<the released ps5-dump-forge-<version>-ps5.elf> also tries the real file)
set -e
cd "$(dirname "$0")/../.."
OUT=${DFCHECKOUT:-$HOME/.cache/ps5tm-dfcheck}
mkdir -p "$OUT"
gcc -std=gnu11 -D_DEFAULT_SOURCE -O1 -g -fsanitize=address,undefined -Wall -Wextra -Werror -Isrc \
  -o "$OUT/test" test/dfcheck/dfcheck_test.c src/sha256.c
timeout 60 "$OUT/test" "$OUT/work"
