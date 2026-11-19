#!/bin/sh
# Host test of src/chives.c (the sidecar of an exported save: geometry, written and read back, refused when it lies) - Linux/WSL, gcc:
#   sh test/chives/run.sh
set -e
cd "$(dirname "$0")/../.."
OUT=${CHIVESOUT:-$HOME/.cache/ps5tm-chives}
mkdir -p "$OUT"
# cJSON once, quiet (the test is about our code)
if [ ! -f "$OUT/cjson.o" ] || [ src/third_party/cJSON.c -nt "$OUT/cjson.o" ]; then
  gcc -O2 -w -Isrc/third_party -c -o "$OUT/cjson.o" src/third_party/cJSON.c
fi
gcc -std=gnu11 -D_DEFAULT_SOURCE -O1 -g -fsanitize=address,undefined -Wall -Wextra -Werror -Isrc -Isrc/third_party \
  -o "$OUT/test" test/chives/test.c src/chives.c "$OUT/cjson.o" -lm
"$OUT/test"
