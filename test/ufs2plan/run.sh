#!/bin/sh
# Host test of the .ffpkg planning and writing of src/conv_ufs2.c (no exFAT plan needed, plan info, no holes, progress, cancel) – Linux/WSL, gcc:
#   sh test/ufs2plan/run.sh
# The work folder has to be on a file system that knows holes (ext4, tmpfs: not /mnt/c). With git available the image is also compared
# byte for byte with the one the committed conv_ufs2.c writes (clock and process id fixed): the format must not change.
set -e
cd "$(dirname "$0")/../.."
OUT=${UFS2PLANOUT:-$HOME/.cache/ps5tm-ufs2plan}
mkdir -p "$OUT/obj"
DL=src/third_party/libdeflate/lib
LIBS=""
for f in $DL/adler32.c $DL/crc32.c $DL/deflate_compress.c $DL/deflate_decompress.c $DL/zlib_compress.c $DL/zlib_decompress.c $DL/utils.c $DL/x86/cpu_features.c; do
  o="$OUT/obj/$(echo "$f" | tr '/' '_').o"
  if [ ! -f "$o" ] || [ "$f" -nt "$o" ]; then
    gcc -O2 -w -Isrc -Isrc/third_party -Isrc/third_party/libdeflate -c -o "$o" "$f"
  fi
  LIBS="$LIBS $o"
done
FIX=""
gcc -std=gnu11 -D_DEFAULT_SOURCE -DPS5TM_HOST_TEST -O1 -g -fsanitize=address,undefined -w $FIX -Isrc -Isrc/third_party \
  -o "$OUT/test" test/ufs2plan/test.c src/conv_ufs2.c src/conv_exfat.c $LIBS -lpthread
rm -rf "$OUT/work"; mkdir -p "$OUT/work"
"$OUT/test" "$OUT/work"
# the byte comparison with the committed writer
if command -v git >/dev/null 2>&1 && git show HEAD:src/conv_ufs2.c > "$OUT/conv_ufs2_head.c" 2>/dev/null; then
  gcc -std=gnu11 -D_DEFAULT_SOURCE -DPS5TM_HOST_TEST -DWRITE_ONLY -O1 -w $FIX -Isrc -Isrc/third_party \
    -o "$OUT/new" test/ufs2plan/test.c src/conv_ufs2.c $LIBS -lpthread
  gcc -std=gnu11 -D_DEFAULT_SOURCE -DPS5TM_HOST_TEST -DWRITE_ONLY -O1 -w $FIX -Isrc -Isrc/third_party \
    -o "$OUT/old" test/ufs2plan/test.c "$OUT/conv_ufs2_head.c" $LIBS -lpthread
  "$OUT/new" "$OUT/work" "$OUT/new.img" >/dev/null
  "$OUT/old" "$OUT/work" "$OUT/old.img" >/dev/null
  cmp "$OUT/new.img" "$OUT/old.img" && echo "image identical to the committed writer's ($(wc -c < "$OUT/new.img") bytes)"
else
  echo "no git HEAD version: the byte comparison was skipped"
fi
