#!/bin/sh
# Host test of the transfer part of src/savebackup.c (lists, start of an import of one or several ZIP files, the job, the start of a read-out; the console's mount is a stub) - Linux/WSL, gcc:
#   sh test/savebackup/run.sh
set -e
cd "$(dirname "$0")/../.."
OUT=${SAVEBACKUPOUT:-$HOME/.cache/ps5tm-savebackup}
WORK=${SAVEBACKUPWORK:-/tmp/ps5tm-savebackup}
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
# the three folders of the console are redirected into the work folder (the test makes and removes it)
D1="-DPS5TM_DATA_DIR=\"$WORK/data\""
D2="-DPS5TM_SAVE_HOME=\"$WORK/home\""
D3="-DPS5TM_SYS_DATA=\"$WORK/system\""
D4="-DPS5TM_APPMETA_ROOTS=\"$WORK/appmeta\""
CF="-std=gnu11 -D_GNU_SOURCE -DPS5TM_HOST_TEST -O1 -g -fsanitize=address,undefined -Isrc -Isrc/third_party -Isrc/third_party/libdeflate"
# savebackup.c is built for the PS5 with clang -Werror; gcc's extra format-truncation warnings on its older parts are not this test's business
gcc $CF -w "$D1" "$D2" "$D3" "$D4" -c -o "$OUT/savebackup.o" src/savebackup.c
gcc $CF -Wall -Wextra -Werror "$D1" "$D2" "$D3" "$D4" -o "$OUT/test" test/savebackup/test.c "$OUT/savebackup.o" src/saveimport.c src/savexport.c src/savedb.c src/savemnt.c \
  src/chives.c src/savesfo.c src/savezip.c $LIBS "$SQ" -lm -lpthread
"$OUT/test" "$WORK"
