#!/bin/sh
# Host test of src/savesfo.c (the param.sfo of a save: read, set the owner, refuse a lying file) - Linux/WSL, gcc:
#   sh test/savesfo/run.sh
set -e
cd "$(dirname "$0")/../.."
OUT=${SAVESFOOUT:-$HOME/.cache/ps5tm-savesfo}
mkdir -p "$OUT"
gcc -std=gnu11 -D_DEFAULT_SOURCE -O1 -g -fsanitize=address,undefined -Wall -Wextra -Werror -Isrc -o "$OUT/test" test/savesfo/test.c src/savesfo.c
"$OUT/test"
