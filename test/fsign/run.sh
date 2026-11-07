#!/bin/sh
# Host test of src/fsign.c (unsigned PS5 executables written as fake-signed SELFs) – Linux/WSL, gcc, python3:
#   sh test/fsign/run.sh
# MAKE_FSELF=/path/to/make_fself.py (tools/make_fself.py of ampr-emu-ng, GPL-3.0; not part of this project) makes the test compare
# the files with the script's, byte for byte. Without it that part is skipped and said so.
set -e
cd "$(dirname "$0")/../.."
OUT=${FSIGNOUT:-$HOME/.cache/ps5tm-fsign}
mkdir -p "$OUT"
# libdeflate's CRC once (the image writers use it), without the sanitizers
DSRC="src/third_party/libdeflate/lib/crc32.c src/third_party/libdeflate/lib/adler32.c src/third_party/libdeflate/lib/utils.c src/third_party/libdeflate/lib/x86/cpu_features.c"
LIBS=""
mkdir -p "$OUT/obj"
for f in $DSRC; do
  o="$OUT/obj/$(echo "$f" | tr '/' '_').o"
  if [ ! -f "$o" ] || [ "$f" -nt "$o" ]; then
    gcc -O2 -w -Isrc -Isrc/third_party -Isrc/third_party/libdeflate -c -o "$o" "$f"
  fi
  LIBS="$LIBS $o"
done
# the image writers are not what is tested here (conv_ufs2.c defines its own DT_ constants, which clash with the system's)
for f in src/conv_exfat.c src/conv_ufs2.c; do
  o="$OUT/obj/$(echo "$f" | tr '/' '_').o"
  if [ ! -f "$o" ] || [ "$f" -nt "$o" ]; then
    gcc -std=gnu11 -D_DEFAULT_SOURCE -DPS5TM_HOST_TEST -O1 -w -Isrc -Isrc/third_party -c -o "$o" "$f"
  fi
  LIBS="$LIBS $o"
done
WARN="-Wall -Wextra -Werror -Wno-format-truncation -Wno-unused-function"
gcc -std=gnu11 -D_DEFAULT_SOURCE -DPS5TM_HOST_TEST -O1 -g -fsanitize=address,undefined $WARN -Isrc   -o "$OUT/test" test/fsign/test.c src/fsign.c src/sha256.c
gcc -std=gnu11 -D_DEFAULT_SOURCE -DPS5TM_HOST_TEST -O1 -g -fsanitize=address,undefined $WARN -Wno-sign-compare -Isrc -Isrc/third_party   -o "$OUT/e2e" test/fsign/e2e.c src/fsign.c src/sha256.c $LIBS
"$OUT/test" "$OUT/work"
"$OUT/e2e" "$OUT/work-e2e"
if [ -n "$MAKE_FSELF" ] && [ -f "$MAKE_FSELF" ]; then
  for k in exec dyn; do
    python3 "$MAKE_FSELF" "$OUT/work/elf_$k.bin" "$OUT/work/ref_$k.self" --ptype fake >/dev/null
    if cmp "$OUT/work/ref_$k.self" "$OUT/work/self_$k.bin"; then
      echo "identical to make_fself.py --ptype fake: $k ($(wc -c < "$OUT/work/self_$k.bin") bytes)"
    else
      echo "DIFFERENT from make_fself.py: $k"; exit 1
    fi
  done
else
  echo "MAKE_FSELF not set: the comparison with make_fself.py was skipped"
fi
# the vector of ps5-dump-forge's own test (the SELF of the same ELF, 18917 bytes), when python3 has the blake3 module
if python3 -c "import blake3" 2>/dev/null; then
  python3 - "$OUT/work/self_exec.bin" <<'PY'
import sys, blake3
d = open(sys.argv[1], "rb").read()
want = "d3539ca69b63349fc50f624471f88bf7e6ad91dbaa7deb57b02202931f60eb1b"
ok = len(d) == 18917 and blake3.blake3(d).hexdigest() == want
print("BLAKE3 vector of ps5-dump-forge:", "ok" if ok else "DIFFERENT")
sys.exit(0 if ok else 1)
PY
fi
