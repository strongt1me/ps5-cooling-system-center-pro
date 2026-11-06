#!/bin/sh
# Host test of src/zstdpack.c (zstd asset packs) – Linux/WSL, gcc:
#   sh test/zstdpack/run.sh [keep]      "keep" leaves a packed game in $ZSTDPACKOUT/work/game for another reader
set -e
cd "$(dirname "$0")/../.."
OUT=${ZSTDPACKOUT:-$HOME/.cache/ps5tm-zstdpack}
mkdir -p "$OUT/obj"
ZD=src/third_party/zstd/lib
ZSRC="$ZD/common/debug.c $ZD/common/entropy_common.c $ZD/common/error_private.c $ZD/common/fse_decompress.c $ZD/common/xxhash.c $ZD/common/zstd_common.c
 $ZD/compress/fse_compress.c $ZD/compress/hist.c $ZD/compress/huf_compress.c $ZD/compress/zstd_compress.c $ZD/compress/zstd_compress_literals.c
 $ZD/compress/zstd_compress_sequences.c $ZD/compress/zstd_compress_superblock.c $ZD/compress/zstd_double_fast.c $ZD/compress/zstd_fast.c
 $ZD/compress/zstd_lazy.c $ZD/compress/zstd_ldm.c $ZD/compress/zstd_opt.c $ZD/compress/zstd_preSplit.c
 $ZD/decompress/huf_decompress.c $ZD/decompress/zstd_ddict.c $ZD/decompress/zstd_decompress.c $ZD/decompress/zstd_decompress_block.c"
DSRC="src/third_party/libdeflate/lib/crc32.c src/third_party/libdeflate/lib/adler32.c src/third_party/libdeflate/lib/utils.c src/third_party/libdeflate/lib/x86/cpu_features.c"
# the libraries once, without the sanitizers (the test is about our code)
LIBS=""
for f in $ZSRC $DSRC; do
  o="$OUT/obj/$(echo "$f" | tr '/' '_').o"
  if [ ! -f "$o" ] || [ "$f" -nt "$o" ]; then
    gcc -O2 -w -DZSTD_DISABLE_ASM=1 -DZSTD_LEGACY_SUPPORT=0 -DDEBUGLEVEL=0 -Isrc -Isrc/third_party -Isrc/third_party/libdeflate -c -o "$o" "$f"
  fi
  LIBS="$LIBS $o"
done
gcc -O1 -w -Isrc -c -o "$OUT/obj/lz4cfg.o" src/lz4cfg.c      # the profile reader: not what is tested here
gcc -std=gnu11 -D_DEFAULT_SOURCE -DPS5TM_HOST_TEST -O1 -g -fsanitize=address,undefined -Wall -Wextra -Werror -Wno-format-truncation -Isrc -Isrc/third_party \
  -o "$OUT/test" test/zstdpack/test.c src/zstdpack.c $OUT/obj/lz4cfg.o $LIBS -lpthread
"$OUT/test" "$OUT/work" "$@"
