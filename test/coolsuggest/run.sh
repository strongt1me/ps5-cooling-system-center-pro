#!/bin/sh
# Host test of the profile suggestions (28.11.2026): src/playtime.c measures each session (time above the target, the
# fan's jumps, the setting in force) and writes the v2 rows; src/coolsuggest.c judges them per game - Linux/WSL, gcc:
#   sh test/coolsuggest/run.sh
set -e
cd "$(dirname "$0")/../.."
OUT=${COOLSUGGESTOUT:-$HOME/.cache/ps5tm-coolsuggest}
rm -rf "$OUT/data"
mkdir -p "$OUT/data"
gcc -O1 -g -w -fsanitize=address,undefined -Isrc/third_party -c -o "$OUT/cjson.o" src/third_party/cJSON.c
gcc -std=gnu11 -D_DEFAULT_SOURCE -O1 -g -fsanitize=address,undefined -Wall -Wextra -Isrc -Isrc/third_party \
  "-DPS5TM_DATA_DIR=\"$OUT/data\"" -DPS5TM_HOST_TEST \
  -o "$OUT/test" test/coolsuggest/cs_test.c src/playtime.c src/coolsuggest.c "$OUT/cjson.o" -lpthread -lm
"$OUT/test"
