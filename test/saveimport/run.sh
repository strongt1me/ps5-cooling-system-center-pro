#!/bin/sh
# Host test of src/saveimport.c (a save from a ZIP of plain files put in; the console's mount and formatter are stubs) - Linux/WSL, gcc:
#   sh test/saveimport/run.sh
set -e
cd "$(dirname "$0")/../.."
OUT=${SAVEIMPORTOUT:-$HOME/.cache/ps5tm-saveimport}
mkdir -p "$OUT/obj"
DL=src/third_party/libdeflate/lib
LIBS=""
for f in $DL/adler32.c $DL/crc32.c $DL/deflate_compress.c $DL/deflate_decompress.c $DL/zlib_compress.c $DL/zlib_decompress.c $DL/utils.c $DL/x86/cpu_features.c src/third_party/cJSON.c; do
  o="$OUT/obj/$(echo "$f" | tr '/' '_').o"
  if [ ! -f "$o" ] || [ "$f" -nt "$o" ]; then
    gcc -O2 -w -Isrc -Isrc/third_party -Isrc/third_party/libdeflate -c -o "$o" "$f"
  fi
  LIBS="$LIBS $o"
done
SQ="$HOME/.cache/ps5tm-savedb/sqlite3.o"
if [ ! -f "$SQ" ] || [ src/third_party/sqlite3/sqlite3.c -nt "$SQ" ]; then
  mkdir -p "$HOME/.cache/ps5tm-savedb"
  gcc -O1 -w -Isrc/third_party/sqlite3 -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_THREADSAFE=0 -DSQLITE_OMIT_WAL -c -o "$SQ" src/third_party/sqlite3/sqlite3.c
fi
gcc -std=gnu11 -D_GNU_SOURCE -DPS5TM_HOST_TEST -O1 -g -fsanitize=address,undefined -Wall -Wextra -Werror -Isrc -Isrc/third_party -Isrc/third_party/libdeflate \
  -o "$OUT/test" test/saveimport/test.c src/saveimport.c src/savedb.c src/savemnt.c src/chives.c src/savesfo.c src/savezip.c $LIBS "$SQ" -lm -lpthread
"$OUT/test" "${SAVEIMPORTWORK:-/tmp/ps5tm-saveimport}"
