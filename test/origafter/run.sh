#!/bin/sh
# Host tests of src/origafter.h (the decisions that need no I/O) and src/origafter.c (what happens to the original after a conversion, a copy
# or an unpack: the jobs are stubs, the files are real, below a folder of the test's own) - Linux/WSL, gcc:
#   sh test/origafter/run.sh
set -e
cd "$(dirname "$0")/../.."
OUT=${ORIGAFTEROUT:-$HOME/.cache/ps5tm-origafter}
ROOT=${ORIGAFTERROOT:-/tmp/ps5tm-oa-root}
mkdir -p "$OUT"
gcc -std=gnu11 -O1 -g -fsanitize=address,undefined -Wall -Wextra -Isrc -o "$OUT/test" test/origafter/origafter_test.c
"$OUT/test"
gcc -std=gnu11 -D_GNU_SOURCE -O1 -g -fsanitize=address,undefined -Wall -Wextra -Isrc -Isrc/third_party \
  "-DOA_ROOT=\"$ROOT\"" '-DOA_SLEEP(s)=usleep((s)*15000)' \
  -o "$OUT/flow" test/origafter/flow_test.c src/origafter.c src/third_party/cJSON.c -lpthread -lm
"$OUT/flow"
rm -rf "$ROOT"
