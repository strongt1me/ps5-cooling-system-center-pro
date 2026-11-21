#!/bin/sh
# Host test of src/driveholders.c (who has a file open on a drive; the Linux variant reads /proc) - Linux/WSL, gcc:
#   sh test/driveholders/run.sh
set -e
cd "$(dirname "$0")/../.."
OUT=${DRIVEHOLDERSOUT:-$HOME/.cache/ps5tm-driveholders}
mkdir -p "$OUT"
gcc -std=gnu11 -D_DEFAULT_SOURCE -DPS5TM_HOST_TEST -O1 -g -fsanitize=address,undefined -Wall -Wextra -Isrc -Isrc/third_party -o "$OUT/test" test/driveholders/holders_test.c src/driveholders.c
"$OUT/test"
