/* PS5 Cooling & System Center - Pro: what the app does with an NTFS stick once it is plugged in (ntfsvol.c probe, the places, a listing
 * of its root), run on one hostile image (hostile.py). Built with ASan/UBSan; run.sh gives it 20 s: a hang or a sanitizer report fails.
 * Copyright (C) 2026 strongt1me
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdint.h>
#include <stdio.h>

#include "third_party/ntfsb/bdev.h"
#include "third_party/ntfsb/ntfs.h"
#include "third_party/ntfsb/part.h"

static int count_cb(void *ctx, const char *name, const ntfs_stat_t *st) {
  (void)name;
  (void)st;
  ++*(int *)ctx;
  return 0;
}

int main(int argc, char **argv) {
  if (argc < 2) return 2;
  bdev_t *b = bdev_open(argv[1]);
  if (!b) { printf("%s: does not open\n", argv[1]); return 0; }
  part_t parts[PART_MAX];
  int n = part_scan(b, parts, PART_MAX);
  for (int i = 0; i < n; i++) {
    bdev_t *s = bdev_slice(b, parts[i].offset, parts[i].size);
    if (!s) continue;
    if (!part_is_ntfs(s)) { bdev_close(s); continue; }
    ntfs_vol_t *nv = NULL;
    char err[160] = "";
    if (ntfs_mount(s, &nv, err, sizeof err) != 0) {
      printf("%s: not mounted (%s)\n", argv[1], err);
      bdev_close(s);
      continue;
    }
    ntfs_info_t info;
    ntfs_get_info(nv, &info);
    char why[300] = "";
    int rw = ntfs_enable_write(nv, why, sizeof why) == 0;
    uint64_t fr = 0;
    int free_rc = ntfs_free_bytes(nv, &fr);
    ntfs_can_write(nv, why, sizeof why);
    uint64_t root = 0;
    int entries = 0, rc = ntfs_lookup(nv, "/", &root);
    if (!rc) rc = ntfs_readdir(nv, root, count_cb, &entries);
    printf("%s: mounted, writable %d, free %s, root listing %s (%d entries)\n", argv[1], rw, free_rc ? "unknown" : "counted",
           rc ? "refused" : "read", entries);
    ntfs_umount(nv);
  }
  bdev_close(b);
  return 0;
}
