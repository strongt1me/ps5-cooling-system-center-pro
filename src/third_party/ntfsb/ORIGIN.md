# NTFS driver from PS5-NTFS-BROWSER

- Project: PS5-NTFS-BROWSER by abcde6543, <https://github.com/abcde6543/PS5-NTFS-BROWSER>
- Version: v1.4.0, commit `1ca8e2d6e37cbb33e8d9b0ac3614577d80e32300` (22.11.2026)
- License: GPL-3.0-or-later (README of the project; the full text is the project's `LICENSE`, GPL-3.0, the same as this app's `LICENSE`)
- Taken over: the NTFS reader and writer with the block-device and partition layer, **unchanged**:
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
