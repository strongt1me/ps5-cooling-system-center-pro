#!/bin/sh
# Host test of src/conv_pfs.c (.ffpfsc images: pre-filter, full and quick check, damage) – Linux/WSL, gcc:
#   sh test/convpfs/run.sh
set -e
cd "$(dirname "$0")/../.."
OUT=${CONVPFSOUT:-$HOME/.cache/ps5tm-convpfs}
mkdir -p "$OUT/obj"
DL=src/third_party/libdeflate/lib
LIBS=""
# libdeflate once, without the sanitizers (the test is about our code)
for f in $DL/adler32.c $DL/crc32.c $DL/deflate_compress.c $DL/deflate_decompress.c $DL/zlib_compress.c $DL/zlib_decompress.c $DL/utils.c $DL/x86/cpu_features.c; do
  o="$OUT/obj/$(echo "$f" | tr '/' '_').o"
  if [ ! -f "$o" ] || [ "$f" -nt "$o" ]; then
    gcc -O2 -w -Isrc -Isrc/third_party -Isrc/third_party/libdeflate -c -o "$o" "$f"
  fi
  LIBS="$LIBS $o"
done
gcc -std=gnu11 -D_DEFAULT_SOURCE -DPS5TM_HOST_TEST -DQUICK_BLOCKS=64 -O1 -g -fsanitize=address,undefined -Wall -Wextra -Werror \
  -Isrc -Isrc/third_party -Isrc/third_party/libdeflate -o "$OUT/test" test/convpfs/test.c src/conv_pfs.c src/checkfile.c src/sha256.c $LIBS -lpthread
rm -rf "$OUT/work"
"$OUT/test" "$OUT/work"
