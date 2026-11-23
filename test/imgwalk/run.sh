#!/bin/sh
# Host test of the image walker in src/imgread.c (whole tree of an exFAT and an .ffpkg image, a folder loop, a cross-link) – Linux/WSL, gcc:
#   sh test/imgwalk/run.sh
# The work folder has to be on a file system that knows holes (ext4, tmpfs: not /mnt/c), as for test/ufs2plan.
set -e
cd "$(dirname "$0")/../.."
OUT=${IMGWALKOUT:-$HOME/.cache/ps5tm-imgwalk}
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
# the writers only make the images (built without warnings, as in test/ufs2plan); the walker and the test with all of them
gcc -std=gnu11 -D_DEFAULT_SOURCE -DPS5TM_HOST_TEST -O1 -g -fsanitize=address,undefined -w -Isrc -Isrc/third_party -c -o "$OUT/obj/conv_ufs2.o" src/conv_ufs2.c
gcc -std=gnu11 -D_DEFAULT_SOURCE -DPS5TM_HOST_TEST -O1 -g -fsanitize=address,undefined -w -Isrc -Isrc/third_party -c -o "$OUT/obj/conv_exfat.o" src/conv_exfat.c
gcc -std=gnu11 -D_DEFAULT_SOURCE -DPS5TM_HOST_TEST -O1 -g -fsanitize=address,undefined -Wall -Wextra -Werror -Isrc -Isrc/third_party \
  -o "$OUT/test" test/imgwalk/test.c src/imgread.c "$OUT/obj/conv_ufs2.o" "$OUT/obj/conv_exfat.o" $LIBS -lpthread
rm -rf "$OUT/work"
"$OUT/test" "$OUT/work"
