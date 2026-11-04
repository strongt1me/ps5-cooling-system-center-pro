#!/usr/bin/env bash
# Builds test/account/account_test.c (which includes src/account.c) for the PC and runs it (needs clang, Linux/WSL).
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
work=${TMPDIR:-/tmp}/acctest
rm -rf "$work"
mkdir -p "$work"
clang -g -fsanitize=address,undefined -Wall -Wextra -Wno-pointer-bool-conversion -DPS5TM_NO_THREAD_WRAP -I"$repo/src" -I"$repo/src/third_party" \
  "$here/account_test.c" -o "$work/account_test"
"$work/account_test"
