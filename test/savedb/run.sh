#!/bin/sh
# Host test of src/savedb.c (the entry of a save in the console's save database, with the embedded SQLite) - Linux/WSL, gcc:
#   sh test/savedb/run.sh
set -e
cd "$(dirname "$0")/../.."
OUT=${SAVEDBOUT:-$HOME/.cache/ps5tm-savedb}
mkdir -p "$OUT"
# SQLite once, without the sanitizers and without warnings (the test is about our code); the same options as the Makefile's
if [ ! -f "$OUT/sqlite3.o" ] || [ src/third_party/sqlite3/sqlite3.c -nt "$OUT/sqlite3.o" ]; then
  gcc -O1 -w -Isrc/third_party/sqlite3 -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_THREADSAFE=0 -DSQLITE_OMIT_WAL -c -o "$OUT/sqlite3.o" src/third_party/sqlite3/sqlite3.c
fi
gcc -std=gnu11 -D_DEFAULT_SOURCE -O1 -g -fsanitize=address,undefined -Wall -Wextra -Werror -Isrc -Isrc/third_party \
  -o "$OUT/test" test/savedb/test.c src/savedb.c "$OUT/sqlite3.o" -lm -lpthread
"$OUT/test" "${SAVEDBWORK:-/tmp/ps5tm-savedb}"
