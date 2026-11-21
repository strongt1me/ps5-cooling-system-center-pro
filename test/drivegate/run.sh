#!/bin/sh
# Host test of src/drivegate.c (the gate an eject waits on) - Linux/WSL, gcc:
#   sh test/drivegate/run.sh
set -e
cd "$(dirname "$0")/../.."
OUT=${DRIVEGATEOUT:-$HOME/.cache/ps5tm-drivegate}
mkdir -p "$OUT"
gcc -std=gnu11 -D_DEFAULT_SOURCE -O1 -g -fsanitize=address,undefined -Wall -Wextra -Isrc -Isrc/third_party -o "$OUT/test" test/drivegate/gate_test.c src/drivegate.c -lpthread
"$OUT/test"
# the same under the thread sanitizer: the claim, the count and the wait must not race
gcc -std=gnu11 -D_DEFAULT_SOURCE -O1 -g -fsanitize=thread -Wall -Wextra -Isrc -Isrc/third_party -o "$OUT/test_tsan" test/drivegate/gate_test.c src/drivegate.c -lpthread
"$OUT/test_tsan"
