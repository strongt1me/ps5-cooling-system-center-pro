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

# a hostile stick (audit 23.11.2026): images that lie about lengths, offsets, sizes and the index tree. What the app does when a stick
# is plugged in and the file manager is open must end quickly and without a sanitizer report.
clang -std=gnu11 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -Wall -Wextra -Werror -Wno-unused-parameter \
  -I"$repo/src" "$here/hostile.c" "$nb/ntfs.c" "$nb/ntfs_write.c" "$nb/bdev.c" "$nb/part.c" "$nb/utf.c" -lpthread -o "$work/hostile"
truncate -s 2M "$work/clean.img"
mkntfs -F -Q -q -c 512 -s 512 -L CLEAN "$work/clean.img" >/dev/null 2>&1
python3 -I "$here/hostile.py" "$work/clean.img" "$work" >/dev/null
for h in clean attrlen nameoff bitmap dag div0 short log_p1 log_p0 log_old log_chkd; do
  if out=$(timeout 20 "$work/hostile" "$work/$h.img" 2>&1); then
    echo "   $out" | head -1
    case "$h" in
      clean) echo "$out" | grep -q "mounted, writable 1.*listing read" || { echo "FAIL image $h is not read and writable"; fail=1; } ;;
      short) echo "$out" | grep -q "mounted, writable 0.*listing read" || { echo "FAIL image $h is not read, or writable"; fail=1; } ;;
      log_p1|log_p0) echo "$out" | grep -q "writable 0.*journal has unsaved" || { echo "FAIL image $h: the newer restart page is not heeded"; fail=1; } ;;
      log_chkd) echo "$out" | grep -q "writable 0.*chkdsk" || { echo "FAIL image $h: chkdsk on page 1 is not heeded"; fail=1; } ;;
      log_old) echo "$out" | grep -q "mounted, writable 1" || { echo "FAIL image $h: an older open page made it read-only"; fail=1; } ;;
    esac
  else
    echo "FAIL hostile image $h (exit $?):"; echo "$out" | head -12; fail=1
  fi
done
[ "$fail" = 0 ] || exit 1

# the dirty flag (upstream issue #9): set while the driver writes, cleared by the sync. The images the file manager wrote above are clean
# again; then the whole sequence on a copy of the clean image.
clang -std=gnu11 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -Wall -Wextra -Werror \
  -I"$repo/src" "$here/dirty.c" "$nb/ntfs.c" "$nb/ntfs_write.c" "$nb/bdev.c" "$nb/part.c" "$nb/utf.c" -lpthread -o "$work/dirty"
for i in "$img" "$disk"; do
  out=$("$work/dirty" --clean "$i" 2>&1) || { echo "FAIL dirty flag left set: $out"; fail=1; }
done
cp "$work/clean.img" "$work/dirty.img"
if out=$(timeout 60 "$work/dirty" "$work/dirty.img" 2>&1); then echo "   $out" | tail -1; else echo "FAIL dirty flag:"; echo "$out" | head -20; fail=1; fi
[ "$fail" = 0 ] || exit 1

# a large folder (24.11.2026: names found down the sorted index, small writes cached until the sync): 3000 files, looked up in two
# cases, half deleted; ntfs-3g must list exactly the other half and find the volume consistent
clang -std=gnu11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined -Wall -Wextra -Werror   -I"$repo/src" "$here/bigdir.c" "$nb/ntfs.c" "$nb/ntfs_write.c" "$nb/bdev.c" "$nb/part.c" "$nb/utf.c" -lpthread -o "$work/bigdir"
truncate -s 256M "$work/big.img"
mkntfs -F -Q -q -L GROSS "$work/big.img" >/dev/null 2>&1
if out=$(timeout 300 "$work/bigdir" "$work/big.img" 3000 2>&1); then echo "   $out" | tail -1; else echo "FAIL bigdir:"; echo "$out" | head -20; fail=1; fi
left=$(ntfsls -p /gross "$work/big.img" 2>/dev/null | grep -c "_Mixed.TXT")
[ "$left" = 1500 ] || { echo "FAIL bigdir: ntfs-3g lists $left files, not 1500"; fail=1; }
ntfsresize --check --force "$work/big.img" >"$work/resize3.log" 2>&1 || { echo "FAIL bigdir: ntfs-3g check"; tail -5 "$work/resize3.log"; fail=1; }
"$work/dirty" --clean "$work/big.img" >/dev/null 2>&1 || { echo "FAIL bigdir: dirty flag left set"; fail=1; }
[ "$fail" = 0 ] || exit 1
echo "ok: ntfsfm (file manager on NTFS images, read back by ntfs-3g; 5 hostile images, 1 a little larger than its drive, 4 journals, dirty flag, a folder of 3000 files)"
