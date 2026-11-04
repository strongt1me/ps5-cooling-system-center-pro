#!/usr/bin/env bash
# Builds src/filemgr.c for the PC with real cJSON and runs conflict_test.c on a scratch folder (needs clang, Linux/WSL).
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
work=${TMPDIR:-/tmp}/fmtest
rm -rf "$work"
mkdir -p "$work"
clang -g -fsanitize=address,undefined -Wall -Wextra -DPS5TM_HOST_TEST -DPS5TM_NO_THREAD_WRAP \
  -DFM_ROOT="\"$work/root\"" -I"$repo/src" -I"$repo/src/third_party" -I"$repo/test/include" \
  "$here/conflict_test.c" "$repo/src/filemgr.c" "$repo/src/third_party/cJSON.c" -lpthread -o "$work/conflict_test"
"$work/conflict_test"
