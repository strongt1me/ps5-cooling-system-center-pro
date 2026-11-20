#!/bin/sh
# Host test of src/cvpack.c (the choice to pack a game into LZ4 or zstd packs while an image is written) - Linux/WSL, gcc:
#   sh test/cvpack/run.sh
set -e
cd "$(dirname "$0")/../.."
OUT=${CVPACKOUT:-$HOME/.cache/ps5tm-cvpack}
mkdir -p "$OUT"
gcc -std=gnu11 -D_DEFAULT_SOURCE -O1 -g -fsanitize=address,undefined -Wall -Wextra -Isrc -Isrc/third_party -o "$OUT/test" test/cvpack/cvpack_test.c src/cvpack.c src/third_party/cJSON.c -lm
"$OUT/test"
