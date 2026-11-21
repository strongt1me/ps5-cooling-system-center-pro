#!/bin/sh
# Host test of src/usbeject.c with ShadowMountPlus, the process list, the jobs and the mount table as stubs (the gate, the marker and cJSON are the real ones):
# when anything speaks against ejecting, ShadowMountPlus is never asked. Linux/WSL, gcc:
#   sh test/usbeject/run.sh
set -e
cd "$(dirname "$0")/../.."
OUT=${USBEJECTOUT:-$HOME/.cache/ps5tm-usbeject}
mkdir -p "$OUT"
gcc -std=gnu11 -D_DEFAULT_SOURCE -O1 -g -fsanitize=address,undefined -Wall -Wextra -Wno-unused-function -Wno-format-truncation -Wno-unknown-pragmas \
  -Itest/usbeject/shim -Isrc -Isrc/third_party \
  -DEJECTMARK_PATH="\"$OUT/usb_eject_offen.txt\"" -DEJECTMARK_LATER_MS=120 \
  -o "$OUT/test" test/usbeject/usbeject_test.c src/usbeject.c src/drivegate.c src/ejectmark.c src/third_party/cJSON.c -lpthread -lm
"$OUT/test"
