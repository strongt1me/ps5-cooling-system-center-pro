#!/bin/sh
# Host test of src/tvname.h (name of the connected display out of the monitor block) - Linux/WSL, gcc:
#   sh test/tvname/run.sh
set -e
cd "$(dirname "$0")/../.."
OUT=${TVNAMEOUT:-$HOME/.cache/ps5tm-tvname}
mkdir -p "$OUT"
gcc -std=gnu11 -O1 -g -fsanitize=address,undefined -Wall -Wextra -Isrc -o "$OUT/test" test/tvname/tvname_test.c
"$OUT/test"
