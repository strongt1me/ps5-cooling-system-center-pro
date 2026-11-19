#!/bin/sh
# Host test of src/savexport.c (a save of the console read out into a ZIP; the console's mount is a stub) - Linux/WSL, gcc:
#   sh test/savexport/run.sh
set -e
cd "$(dirname "$0")/../.."
OUT=${SAVEXPORTOUT:-$HOME/.cache/ps5tm-savexport}
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
gcc -std=gnu11 -D_GNU_SOURCE -DPS5TM_HOST_TEST -O1 -g -fsanitize=address,undefined -Wall -Wextra -Werror -Isrc -Isrc/third_party -Isrc/third_party/libdeflate \
  -o "$OUT/test" test/savexport/test.c src/savexport.c src/savemnt.c src/chives.c src/savesfo.c src/savezip.c $LIBS -lm -lpthread
"$OUT/test" "${SAVEXPORTWORK:-/tmp/ps5tm-savexport}"
