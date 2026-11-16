#!/bin/sh
# Host test of src/progressnote.h (progress lines of a long job at the top left of the screen) - Linux/WSL, gcc:
#   sh test/progressnote/run.sh
set -e
cd "$(dirname "$0")/../.."
OUT=${PROGRESSNOTEOUT:-$HOME/.cache/ps5tm-progressnote}
mkdir -p "$OUT"
gcc -std=gnu11 -O1 -g -fsanitize=address,undefined -Wall -Wextra -Isrc -o "$OUT/test" test/progressnote/progressnote_test.c
"$OUT/test"
