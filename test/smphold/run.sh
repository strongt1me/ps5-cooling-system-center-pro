#!/bin/sh
# Host test of src/smphold.c (a game the app mounted through ShadowMountPlus and nobody started is unmounted again) - Linux/WSL, gcc:
#   sh test/smphold/run.sh
set -e
cd "$(dirname "$0")/../.."
OUT=${SMPHOLDOUT:-$HOME/.cache/ps5tm-smphold}
mkdir -p "$OUT"
gcc -std=gnu11 -D_DEFAULT_SOURCE -O1 -g -fsanitize=address,undefined -Wall -Wextra -Isrc -Isrc/third_party \
  -DPS5TM_DATA_DIR="\"$OUT/data\"" -o "$OUT/test" test/smphold/smphold_test.c src/smphold.c -lpthread
"$OUT/test"
