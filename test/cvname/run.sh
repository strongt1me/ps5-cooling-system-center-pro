#!/bin/sh
# Host test of src/cvname.c (the name of a converted image from a template) - Linux/WSL, gcc:
#   sh test/cvname/run.sh
set -e
cd "$(dirname "$0")/../.."
OUT=${CVNAMEOUT:-$HOME/.cache/ps5tm-cvname}
mkdir -p "$OUT"
gcc -std=gnu11 -D_DEFAULT_SOURCE -O1 -g -fsanitize=address,undefined -Wall -Wextra -Isrc -o "$OUT/test" test/cvname/cvname_test.c src/cvname.c
"$OUT/test"
