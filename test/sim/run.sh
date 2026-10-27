#!/bin/sh
# Closed-loop simulation of the fan regulation and the guard (Linux/WSL, gcc).
#   sh test/sim/run.sh            all scenarios
#   sh test/sim/run.sh step_t66   one scenario; SIMV=1 shows the log, SIMTRACE=file.csv writes the curve
set -e
cd "$(dirname "$0")/../.."
OUT=${SIMOUT:-$HOME/.cache/ps5tm-fansim}   # not /tmp: WSL may clear it between calls
mkdir -p "$OUT"
CF="-O1 -g -Isrc -Isrc/third_party -DPS5TM_HOST_TEST -DPS5TM_DATA_DIR=\"$OUT/data\" -Wno-unused-function"
gcc $CF -c -o "$OUT/fansim.o"   test/sim/fansim.c
gcc $CF -Dsleep=sim_sleep -c -o "$OUT/fanguard.o" src/fanguard.c
gcc $CF -c -o "$OUT/config.o"   src/config.c
gcc -O1 -w -Isrc/third_party -c -o "$OUT/cjson.o" src/third_party/cJSON.c
gcc -o "$OUT/fansim" "$OUT/fansim.o" "$OUT/fanguard.o" "$OUT/config.o" "$OUT/cjson.o" -lpthread -lm
if [ -n "$1" ]; then exec "$OUT/fansim" "$1"; fi
for s in $("$OUT/fansim"); do timeout 60 "$OUT/fansim" "$s" || echo "$s FAILED ($?)"; done
