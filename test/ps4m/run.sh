#!/bin/sh
# Unit checks of the PS4Mount port (src/ps4m_*.c) against upstream's test/unit.c (Linux/WSL, gcc, python3).
#   sh test/ps4m/run.sh
# PS4Mount 0.8.0 adds the pkg index, the filter for repeated klog lines and the add-on copy rules: upstream's
# test/pkgindex_test.c, klogdup_test.c and accopy_test.c run here with the p4m_ prefix.
set -e
cd "$(dirname "$0")/../.."
OUT=${P4MOUT:-$HOME/.cache/ps5tm-ps4m}   # not /tmp: WSL may clear it between calls
mkdir -p "$OUT"
python3 tools/gen_p4m_templates.py "$OUT/pdb_template.h" >/dev/null
CC="gcc -std=c11 -D_DEFAULT_SOURCE -O1"
$CC -I"$OUT" -Isrc -o "$OUT/unit" third_party/ps4mount/test/unit.c \
  src/ps4m_pkg.c src/ps4m_build.c src/ps4m_crypto.c src/ps4m_origin.c
"$OUT/unit"
$CC -Isrc -o "$OUT/pkgindex_test" third_party/ps4mount/test/pkgindex_test.c \
  src/ps4m_pkgindex.c src/ps4m_pkg.c src/ps4m_crypto.c
"$OUT/pkgindex_test"
$CC -Isrc -o "$OUT/klogdup_test" third_party/ps4mount/test/klogdup_test.c src/ps4m_klogdup.c
"$OUT/klogdup_test"
$CC -Isrc -o "$OUT/accopy_test" third_party/ps4mount/test/accopy_test.c src/ps4m_accopy.c
"$OUT/accopy_test"
