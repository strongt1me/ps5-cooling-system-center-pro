#!/usr/bin/env bash
# Builds src/filemgr.c for the PC with real cJSON and runs conflict_test.c on a scratch folder (needs clang, Linux/WSL).
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
work=${TMPDIR:-/tmp}/fmtest
rm -rf "$work"
mkdir -p "$work"
clang -g -fsanitize=address,undefined -Wall -Wextra -DPS5TM_HOST_TEST -DPS5TM_NO_THREAD_WRAP \
  -DFM_TEST_HOOK=fm_slow -DFM_TEST_HOOK_READ=fm_read_probe -DFM_ROOT="\"$work/root\"" -I"$repo/src" -I"$repo/src/third_party" -I"$repo/test/include" \
  "$here/conflict_test.c" "$repo/src/filemgr.c" "$repo/src/ntfsvol.c" "$repo/src/third_party/cJSON.c" \
  "$repo/src/third_party/ntfsb/ntfs.c" "$repo/src/third_party/ntfsb/ntfs_write.c" "$repo/src/third_party/ntfsb/bdev.c" "$repo/src/third_party/ntfsb/part.c" "$repo/src/third_party/ntfsb/utf.c" \
  -lpthread -o "$work/conflict_test"
"$work/conflict_test"
