/* PS5 Cooling & System Center - Pro: the dirty flag of an NTFS drive the app writes to (upstream issue #9 of PS5-NTFS-BROWSER, 23.11.2026).
 * The driver sets the flag in $VOLUME_INFORMATION before its first change after a sync and clears it in the sync, so that a stick pulled
 * in between is marked as not cleanly removed (Windows then checks it, and the app itself keeps it read-only). Each check mounts the image
 * a second time, through its own file descriptor, and reads the flag from the disk.
 *   dirty <image>           the whole sequence on a clean, writable image (it is written to)
 *   dirty --clean <image>   only: the flag is not set (after another test wrote to the image)
 * Copyright (C) 2026 strongt1me
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "third_party/ntfsb/bdev.h"
#include "third_party/ntfsb/ntfs.h"
#include "third_party/ntfsb/part.h"

static int g_n, g_fail;
#define CHECK(c) do { g_n++; if(!(c)) { g_fail++; printf("FAIL line %d: %s\n", __LINE__, #c); } } while(0)

/* the first NTFS partition of the image (a bare volume or one behind a partition table), mounted */
static ntfs_vol_t *mount_first(const char *path, bdev_t **dev) {
  bdev_t *b = bdev_open(path);
  if(!b) return NULL;
  part_t parts[PART_MAX];
  int n = part_scan(b, parts, PART_MAX);
  for(int i = 0; i < n; i++) {
    bdev_t *s = bdev_slice(b, parts[i].offset, parts[i].size);
    if(!s) continue;
    if(!part_is_ntfs(s)) { bdev_close(s); continue; }
    ntfs_vol_t *v = NULL;
    char err[160];
    if(ntfs_mount(s, &v, err, sizeof(err)) == 0) { *dev = b; return v; }
    bdev_close(s);
  }
  bdev_close(b);
  return NULL;
}

/* 1 when the flag is set on the disk, 0 when not, -1 when the image does not mount */
static int dirty_on_disk(const char *path) {
  bdev_t *b = NULL;
  ntfs_vol_t *v = mount_first(path, &b);
  if(!v) return -1;
  ntfs_info_t info;
  ntfs_get_info(v, &info);
  ntfs_umount(v);
  bdev_close(b);
  return info.dirty ? 1 : 0;
}

int main(int argc, char **argv) {
  if(argc == 3 && !strcmp(argv[1], "--clean")) {
    int d = dirty_on_disk(argv[2]);
    printf("%s: dirty flag %s\n", argv[2], d < 0 ? "unreadable" : d ? "SET" : "clear");
    return d == 0 ? 0 : 1;
  }
  if(argc != 2) return 2;
  const char *img = argv[1];
  CHECK(dirty_on_disk(img) == 0);

  bdev_t *b = NULL;
  ntfs_vol_t *v = mount_first(img, &b);
  CHECK(v != NULL);
  if(!v) { printf("%d checks, %d failed\n", g_n, g_fail); return 1; }
  char why[300] = "";
  CHECK(ntfs_enable_write(v, why, sizeof(why)) == 0);
  CHECK(dirty_on_disk(img) == 0);                      /* allowing writes alone touches nothing */
  uint64_t root = 0, ino = 0;
  CHECK(ntfs_lookup(v, "/", &root) == 0);

  /* a folder: marked by the change, clean after the sync */
  CHECK(ntfs_mkdir(v, root, "d1", (int64_t)time(NULL), &ino) == 0);
  CHECK(dirty_on_disk(img) == 1);
  CHECK(ntfs_sync(v) == 0);
  CHECK(dirty_on_disk(img) == 0);

  /* a file half written (the stick pulled now): marked, and such a drive is read-only for the next mount */
  ntfs_wfile_t *f = NULL;
  static char data[64 * 1024];
  memset(data, 0x5A, sizeof(data));
  CHECK(ntfs_create(v, root, "half.bin", 4 * sizeof(data), (int64_t)time(NULL), &f) == 0);
  CHECK(f && ntfs_wwrite(f, data, sizeof(data)) == 0);
  CHECK(dirty_on_disk(img) == 1);
  {
    bdev_t *b2 = NULL;
    ntfs_vol_t *v2 = mount_first(img, &b2);
    char w2[300] = "";
    CHECK(v2 && ntfs_can_write(v2, w2, sizeof(w2)) != 0 && strstr(w2, "Windows did not unmount"));
    if(v2) { ntfs_umount(v2); bdev_close(b2); }
  }
  /* a sync for something else in between clears it; the next block of the file marks it again */
  CHECK(ntfs_sync(v) == 0);
  CHECK(dirty_on_disk(img) == 0);
  CHECK(ntfs_wwrite(f, data, sizeof(data)) == 0);
  CHECK(dirty_on_disk(img) == 1);
  CHECK(ntfs_wwrite(f, data, sizeof(data)) == 0 && ntfs_wwrite(f, data, sizeof(data)) == 0);
  CHECK(ntfs_wclose(f) == 0);
  CHECK(ntfs_sync(v) == 0);
  CHECK(dirty_on_disk(img) == 0);

  /* a delete, then the drive let go without a sync: the unmount clears the flag */
  CHECK(ntfs_unlink(v, root, "half.bin") == 0);
  CHECK(dirty_on_disk(img) == 1);
  ntfs_umount(v);
  bdev_close(b);
  CHECK(dirty_on_disk(img) == 0);

  printf("dirty: %d checks, %d failed\n", g_n, g_fail);
  return g_fail ? 1 : 0;
}
