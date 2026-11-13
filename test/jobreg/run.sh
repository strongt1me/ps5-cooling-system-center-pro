#!/bin/sh
# Host test of src/jobreg.c (the one list of jobs that do not run beside each other) - Linux/WSL, gcc:
#   sh test/jobreg/run.sh
set -e
cd "$(dirname "$0")/../.."
OUT=${JOBREGOUT:-$HOME/.cache/ps5tm-jobreg}
mkdir -p "$OUT"
gcc -std=gnu11 -D_DEFAULT_SOURCE -O1 -g -fsanitize=address,undefined -Wall -Wextra -Isrc -Isrc/third_party -o "$OUT/test" test/jobreg/jobreg_test.c src/jobreg.c
"$OUT/test"
