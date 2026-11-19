#!/bin/sh
# Fuzzing of the readers a hostile save can reach (the ZIP reader, chives.json, param.sfo, the inspection of a save) - Linux/WSL, clang with libFuzzer:
#   sh test/fuzz/run.sh [seconds per target] [seed folder]
# Not part of the CI list: it runs for a while and needs the libFuzzer runtime (libclang-rt-dev). A crash leaves crash-<hash> in the output folder.
set -e
cd "$(dirname "$0")/../.."
SECS=${1:-120}
SEEDS=${2:-}
OUT=${FUZZOUT:-$HOME/.cache/ps5tm-fuzz}
mkdir -p "$OUT/obj" "$OUT/corpus-zip" "$OUT/corpus-sfo"
DL=src/third_party/libdeflate/lib
LIBS=""
for f in $DL/adler32.c $DL/crc32.c $DL/deflate_compress.c $DL/deflate_decompress.c $DL/zlib_compress.c $DL/zlib_decompress.c $DL/utils.c $DL/x86/cpu_features.c src/third_party/cJSON.c; do
  o="$OUT/obj/$(echo "$f" | tr '/' '_').o"
  if [ ! -f "$o" ] || [ "$f" -nt "$o" ]; then
    clang -O2 -w -Isrc -Isrc/third_party -Isrc/third_party/libdeflate -c -o "$o" "$f"
  fi
  LIBS="$LIBS $o"
done
SQ="$HOME/.cache/ps5tm-savedb/sqlite3.o"
if [ ! -f "$SQ" ] || [ src/third_party/sqlite3/sqlite3.c -nt "$SQ" ]; then
  mkdir -p "$HOME/.cache/ps5tm-savedb"
  clang -O1 -w -Isrc/third_party/sqlite3 -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_THREADSAFE=0 -DSQLITE_OMIT_WAL -c -o "$SQ" src/third_party/sqlite3/sqlite3.c
fi
CF="-std=gnu11 -D_GNU_SOURCE -DPS5TM_HOST_TEST -O1 -g -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=undefined -Wall -Wextra -Isrc -Isrc/third_party -Isrc/third_party/libdeflate"
clang $CF -o "$OUT/fuzz_zip" test/fuzz/fuzz_zip.c src/saveimport.c src/savedb.c src/savemnt.c src/chives.c src/savesfo.c src/savezip.c $LIBS "$SQ" -lm -lpthread
clang $CF -o "$OUT/fuzz_sfo" test/fuzz/fuzz_sfo.c src/chives.c src/savesfo.c $LIBS -lm
[ -n "$SEEDS" ] && cp -n "$SEEDS"/*.zip "$OUT/corpus-zip/" 2>/dev/null || true
(cd "$OUT" && ./fuzz_zip -max_total_time="$SECS" -max_len=1048576 -rss_limit_mb=2048 -artifact_prefix="$OUT/" corpus-zip)
(cd "$OUT" && ./fuzz_sfo -max_total_time="$SECS" -max_len=65536 -rss_limit_mb=2048 -artifact_prefix="$OUT/" corpus-sfo)
echo "fuzz: no crash in ${SECS} s per target"
