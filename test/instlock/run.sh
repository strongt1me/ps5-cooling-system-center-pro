#!/usr/bin/env bash
# Builds src/instlock.c for the PC and runs the two tests (needs clang, Linux/WSL).
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
work=${TMPDIR:-/tmp}/instlocktest
rm -rf "$work"; mkdir -p "$work"
for t in instlock_test stubborn_test; do
  clang -g -fsanitize=address,undefined -Wall -Wextra "$here/$t.c" "$repo/src/instlock.c" -o "$work/$t"
  "$work/$t"
done
