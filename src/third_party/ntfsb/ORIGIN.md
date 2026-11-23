# NTFS driver from PS5-NTFS-BROWSER

- Project: PS5-NTFS-BROWSER by abcde6543, <https://github.com/abcde6543/PS5-NTFS-BROWSER>
- Version: v1.4.0, commit `1ca8e2d6e37cbb33e8d9b0ac3614577d80e32300` (22.11.2026)
- License: GPL-3.0-or-later (README of the project; the full text is the project's `LICENSE`, GPL-3.0, the same as this app's `LICENSE`)
- Taken over: the NTFS reader and writer with the block-device and partition layer, unchanged except the checks listed under
  "Changes by this app":
  `ntfs.c`, `ntfs.h`, `ntfs_priv.h`, `ntfs_write.c`, `part.c`, `part.h`, `bdev.c`, `bdev.h`, `utf.c`, `utf.h`, `endian_util.h`
- Not taken over: the project's web server, interface, copy queue, volume list and start-up (`http.c`, `api.c`, `jobs.c`, `vfs.c`,
  `main.c`, `platform.c`, `web/`). The app has its own file manager; `src/ntfsvol.c` finds the drives (after the pattern of `vfs.c`)
  and `src/fm_ntfs.inc` (included by `src/filemgr.c`) connects them to the file manager.

SHA-256 of the files as taken (first 16 hex digits):

| File | SHA-256 |
| --- | --- |
| ntfs.c | 1443d07afac16c16 |
| ntfs_write.c | 642f0028a53cd520 |
| bdev.c | fff1d751bc66fd96 |
| part.c | b4455dc65457733d |
| utf.c | 7bfcc813a0bc5834 |

Built as objects of their own (`gen/ntfsb/*.o`, Makefile) with `-Wno-unused-parameter`, as upstream builds them.

## Changes by this app (23.11.2026, after the app's audit)

A stick is untrusted input: the driver reads it as soon as it is plugged in and the file manager is open. Found with libFuzzer
(ASan/UBSan, about 400 000 made-up images in two rounds and 4.4 million runs afterwards) and made-up images; each fix is checked by
the app's host test `test/ntfsfm` (four hostile images that hang or crash the driver as taken over, one that crashed a first version
of these fixes, one volume a little larger than its drive that must still be read, and four journals; `dirty.c` checks the dirty
flag). Some are marked `(app, 23.11.2026)`
in the code; the full list:

- `ntfs.c` `read_record`/`read_record_any` (`record_ok`): a record's used and allocated size and its first attribute's offset must fit
  the record size (else inserts and resizes wrote past the 1 KB record: heap overflow when creating in such a folder).
- `ntfs.c` `next_attr`: `len > used - p` instead of `p + len > used` (a length near 4 GB wrapped around: endless loop while mounting,
  copies of up to 4 GB past the record).
- `ntfs.c` `attr_name_eq`, the attribute list in `attr_find`; `ntfs_write.c` `update_nonresident`, `rewrite_seg` and the attribute lists
  of `$INDEX_ALLOCATION`: the name offset must lie inside the attribute or the entry (reads up to 64 KB past a buffer).
- `ntfs.c` `absorb`: the value of a resident attribute must lie inside it (`voff > at_len || vlen > at_len - voff`, the sum wrapped
  around); a compression unit is at most 2^4 clusters.
- `ntfs.c` `decode_runs`: runs, their start and their cluster numbers are bounded (overflow); `read_raw` and (`ntfs_write.c`)
  `write_raw` stop on a step of 0 bytes (a run of 2^52 clusters looped for ever while holding the drive's lock).
- `ntfs.c` `ntfs_mount`: the volume may not be much larger than the device, at most 1 % and 64 MB more (the free-space count ran for
  years over made-up zeros); a volume larger than its device at all (a USB bridge that reports a few sectors less than the partition
  table) is still read but never written (`short_dev`, `ntfs_can_write`: clusters past the end would be handed out); the sectors per cluster
  are a shift of at most 12 and the cluster at most 2 MiB, computed in 64 bits (from a shift of 13 on, 512 * 2^n wrapped to 0 in 32 bits:
  division by zero while mounting).
- `ntfs.c` `index_walk`/`walk_block`: each index block is visited once per walk, and no more often than the blocks really mapped (a few
  blocks that point at each other many times kept a listing busy for minutes and grew it to 700 MB).
- `ntfs_write.c` `res_val`: every resident value the writer reads or writes ($MFT's and an index's $BITMAP, $INDEX_ROOT, $FILE_NAME
  when deleting) must lie inside its attribute, and a bit to set must lie inside the bitmap; resident bitmaps grow no larger than a
  record.
- `ntfs_write.c` `node_parse`/`node_validate`: an index entry's key must hold a whole file name, an entry with a sub-node its pointer.
- `ntfs_write.c` `index_insert`/`ictx_open`: the index header of `$INDEX_ROOT` must fit its value.
- `bdev.c` `bdev_enable_write`: the node opened anew for the first write must still be the disk the drive was read from (a stick
  swapped for another under the same `/dev/daN` was written with the first one's tables: the other stick's files were lost).
- `part.c` `add`: a partition that overlaps one already found is left out (two drives on the same sectors).
- `ntfs_write.c` `free_inode`: the MFT records are released before their clusters are freed (pulled half way, a record still in use
  owned clusters the bitmap called free, and the next file shared them; now the worst case is clusters chkdsk gives back). From a
  review of the upstream code in another session of the user (23.11.2026, patch "loeschreihenfolge").
- `ntfs_write.c` `ntfs_can_write`: both restart pages of `$LogFile` are read and the newer one (higher current LSN) decides, as in
  ntfs-3g; "CHKD" on either page keeps the drive read-only (only page 0 was read: a drive whose newer page had a client open was written
  and its journal cleared). Upstream issue #9, shown by tests of another review session.
- `ntfs_write.c` `mark_volume` (new), `prepare_locked`, `ntfs_wwrite`, `ntfs_wclose`, `ntfs_sync`, `ntfs_disable_write`, `ntfs.c`
  `ntfs_umount`: the dirty flag of `$VOLUME_INFORMATION` is set before the first change after a sync and cleared by the sync once the
  rest is on the disk (it was never set: a stick pulled while writing looked clean to Windows, a half-written file showed its full
  size). Upstream issue #9; test `test/ntfsfm/dirty.c`.
- `bdev.c` `bdev_flush_cache`, `ntfs_write.c` `ntfs_flush_cache` (new): `DIOCGFLUSH`, so that the stick empties its own write cache;
  the app calls it once when a drive is ejected, after `ntfs_sync` (not at every sync: some USB bridges are slow on the command; an
  error is ignored, not every stick knows it).

Not changed: three leaks in `ntfs_write.c` that happen only when memory runs out; calloc sizes from the disk in the index and MFT growth
(a refused allocation is handled).
