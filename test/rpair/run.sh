#!/bin/sh
# Host test of src/rpkeys.h (registry keys, base64) and src/rpair.c (pairing state machine of Remote Play) - Linux/WSL, gcc:
#   sh test/rpair/run.sh
set -e
cd "$(dirname "$0")/../.."
OUT=${RPAIROUT:-$HOME/.cache/ps5tm-rpair}
mkdir -p "$OUT"
gcc -std=gnu11 -O1 -g -fsanitize=address,undefined -Wall -Wextra -Werror -Isrc -o "$OUT/test" test/rpair/test.c src/rpair.c -lpthread
timeout 60 "$OUT/test"
