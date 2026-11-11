#!/bin/sh
# Host test of src/sha256.c (plain C against the SHA extensions, known answers, sha256sum, speed) – Linux/WSL, gcc:
#   sh test/sha256/run.sh
set -e
cd "$(dirname "$0")/../.."
OUT=${SHA256OUT:-$HOME/.cache/ps5tm-sha256}
mkdir -p "$OUT"
# sanitizers on for the correctness run; the speed run is a second build without them (-O2, as the app is built)
gcc -std=gnu11 -D_DEFAULT_SOURCE -O1 -g -fsanitize=address,undefined -Wall -Wextra -Werror -Isrc \
  -o "$OUT/test" test/sha256/test.c src/sha256.c
"$OUT/test" "$OUT/work" | grep -v "^speed"
gcc -std=gnu11 -D_DEFAULT_SOURCE -O2 -Wall -Wextra -Werror -Isrc -o "$OUT/speed" test/sha256/test.c src/sha256.c
"$OUT/speed" "$OUT/work" | grep "^speed\|checks"
