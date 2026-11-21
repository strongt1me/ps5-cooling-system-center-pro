#!/bin/sh
# Host test of the setting serial_full in src/config.c (Linux/WSL, gcc):
#   sh test/configserial/run.sh
set -e
cd "$(dirname "$0")/../.."
OUT=${CONFIGSERIALOUT:-$HOME/.cache/ps5tm-configserial}
rm -rf "$OUT"; mkdir -p "$OUT/data"
gcc -std=gnu11 -O1 -g -fsanitize=address,undefined -Wall -Wextra -Wno-unused-function -Wno-format-truncation -Wno-unknown-pragmas \
  -Isrc -Isrc/third_party -DPS5TM_HOST_TEST -DPS5TM_DATA_DIR="\"$OUT/data\"" \
  -o "$OUT/test" test/configserial/serial_test.c src/config.c src/third_party/cJSON.c -lpthread -lm
"$OUT/test"
