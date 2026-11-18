#!/bin/sh
# Host test of src/smpunreg.h (the steps that take a title off ShadowMountPlus before its data is deleted) - Linux/WSL, gcc:
#   sh test/smpunreg/run.sh
set -e
cd "$(dirname "$0")/../.."
OUT=${SMPUNREGOUT:-$HOME/.cache/ps5tm-smpunreg}
mkdir -p "$OUT"
gcc -std=gnu11 -O1 -g -fsanitize=address,undefined -Wall -Wextra -Isrc -o "$OUT/test" test/smpunreg/smpunreg_test.c
"$OUT/test"
