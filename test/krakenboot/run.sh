#!/bin/sh
# Host test of src/krakenboot.h (Kraken Mount starts with the app; the version of the running one) - Linux/WSL, gcc:
#   sh test/krakenboot/run.sh
set -e
cd "$(dirname "$0")/../.."
OUT=${KRAKENBOOTOUT:-$HOME/.cache/ps5tm-krakenboot}
mkdir -p "$OUT"
gcc -std=gnu11 -O1 -g -fsanitize=address,undefined -Wall -Wextra -Isrc -o "$OUT/test" test/krakenboot/krakenboot_test.c
"$OUT/test"
