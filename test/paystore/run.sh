#!/bin/sh
# Host test of src/paystore.c (the lists of the payload store: itsPLK's and strongt1me's in one, each saved on its own) - Linux/WSL, gcc:
#   sh test/paystore/run.sh
set -e
cd "$(dirname "$0")/../.."
OUT=${PAYSTOREOUT:-$HOME/.cache/ps5tm-paystore}
mkdir -p "$OUT"
# cJSON once, without warnings (the test is about our code)
if [ ! -f "$OUT/cJSON.o" ] || [ src/third_party/cJSON.c -nt "$OUT/cJSON.o" ]; then
  gcc -std=gnu11 -O1 -w -Isrc/third_party -c -o "$OUT/cJSON.o" src/third_party/cJSON.c
fi
gcc -std=gnu11 -D_DEFAULT_SOURCE -O1 -g -fsanitize=address,undefined -Wall -Wextra -Werror -Isrc -Isrc/third_party \
  -DPS5TM_DATA_DIR=\"$OUT/data\" -o "$OUT/test" test/paystore/test.c src/sha256.c "$OUT/cJSON.o" -lpthread -lm
mkdir -p "$OUT/data"
timeout 60 "$OUT/test" "$OUT/data"
