#!/usr/bin/env bash
# NTFS sticks in the file manager (23.11.2026): builds src/filemgr.c (with fm_ntfs.inc), src/ntfsvol.c and the driver of PS5-NTFS-BROWSER
# for the PC, runs test.c on two images made by mkntfs (one writable, one that cannot be opened for writing), and then reads the
# written image with ntfs-3g's own tools: what the app made must be there, byte for byte, and the volume must pass ntfsresize --check.
# Needs clang and ntfs-3g (mkntfs, ntfsls, ntfscat, ntfsresize); Linux/WSL.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
work=${TMPDIR:-/tmp}/ntfsfm
PATH="$PATH:/usr/sbin:/sbin"
for t in mkntfs ntfsls ntfscat ntfsresize sfdisk; do
  command -v "$t" >/dev/null || { echo "ntfsfm: $t fehlt (Pakete ntfs-3g, fdisk)" >&2; exit 2; }
done
rm -rf "$work"
mkdir -p "$work"
nb="$repo/src/third_party/ntfsb"
clang -std=gnu11 -g -fsanitize=address,undefined -Wall -Wextra -Werror -DPS5TM_HOST_TEST -DPS5TM_NO_THREAD_WRAP \
  -DFM_ROOT="\"$work/root\"" -I"$repo/src" -I"$repo/src/third_party" -I"$repo/test/include" \
  "$here/test.c" "$repo/src/filemgr.c" "$repo/src/ntfsvol.c" "$repo/src/third_party/cJSON.c" \
  "$nb/ntfs.c" "$nb/ntfs_write.c" "$nb/bdev.c" "$nb/part.c" "$nb/utf.c" -lpthread -o "$work/test"

img="$work/ntfs.img"
ro="$work/ro.img"
disk="$work/disk.img"
truncate -s 64M "$img"
mkntfs -F -Q -q -L PS5TEST "$img" >/dev/null 2>&1
truncate -s 32M "$ro"
mkntfs -F -Q -q -L NURLESEN "$ro" >/dev/null 2>&1
chmod 0444 "$ro"
# a whole disk as Windows formats a stick: an MBR, the NTFS partition from 1 MiB on (type 7)
truncate -s 40M "$work/part.img"
mkntfs -F -Q -q -p 2048 -L MITMBR "$work/part.img" >/dev/null 2>&1
{ head -c 1048576 /dev/zero; cat "$work/part.img"; } > "$disk"
printf 'label: dos\nstart=2048, size=%d, type=7\n' $((40 * 2048)) | sfdisk -q "$disk" >/dev/null 2>&1
"$work/test" "$img" "$ro" "$disk"

# ntfs-3g reads what the app wrote
fail=0
check() { if eval "$2"; then :; else echo "FAIL ntfs-3g: $1"; fail=1; fi; }
check "notiz.txt" '[ "$(ntfscat "$img" /Spiele/notiz.txt)" = "Hallo NTFS" ]'
check "notiz (2).txt" '[ "$(ntfscat "$img" "/Spiele/notiz (2).txt")" = "zweite" ]'
check "klein.txt" '[ "$(ntfscat "$img" /Spiele/src/sub/klein.txt)" = "klein" ]'
check "big.bin" 'ntfscat "$img" /Spiele/src/big.bin | cmp -s - "$work/root/data/src/big.bin"'
check "adjusted name" 'ntfsls -p /Spiele/src "$img" | grep -qx "weird_name_.txt"'
check "deleted src (2)" '! ntfsls -p /Spiele "$img" | grep -q "src (2)"'
check "moved away mv" '! ntfsls "$img" | grep -qx "mv"'
check "deleted a_b" '! ntfsls "$img" | grep -qx "a_b"'
check "volume consistent" 'ntfsresize --check --force "$img" >"$work/resize.log" 2>&1 || { cat "$work/resize.log"; false; }'
dd if="$disk" of="$work/part.out" bs=1M skip=1 status=none
check "file on the partition" '[ "$(ntfscat "$work/part.out" /Partition/p.txt)" = "auf der Partition" ]'
check "partition consistent" 'ntfsresize --check --force "$work/part.out" >"$work/resize2.log" 2>&1 || { cat "$work/resize2.log"; false; }'
[ "$fail" = 0 ] || exit 1
echo "ok: ntfsfm (file manager on NTFS images, read back by ntfs-3g)"
