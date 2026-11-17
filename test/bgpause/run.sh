#!/bin/sh
# Host test of src/bgpause.c (the game that waits in the background is paused before a tile of the app is tapped) - Linux/WSL, gcc:
#   sh test/bgpause/run.sh
set -e
cd "$(dirname "$0")/../.."
OUT=${BGPAUSEOUT:-$HOME/.cache/ps5tm-bgpause}
mkdir -p "$OUT"
gcc -std=gnu11 -D_DEFAULT_SOURCE -DPS5TM_HOST_TEST -O1 -g -fsanitize=address,undefined -Wall -Wextra -Isrc -Isrc/third_party \
  -DPS5TM_DATA_DIR="\"$OUT/data\"" -o "$OUT/test" test/bgpause/bgpause_test.c src/bgpause.c -lpthread -lm
"$OUT/test"
