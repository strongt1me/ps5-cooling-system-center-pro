/* PS5 Cooling & System Center - Pro: a large folder on NTFS (24.11.2026). Names are now found by going down the sorted index instead of
 * walking every entry, and small writes wait in the driver's cache until the next sync. This test fills one folder far beyond its index
 * root (a tree of several levels), then looks every name up in its own case and in another case, looks up names that are not there,
 * refuses a taken name in another case, deletes every second file and syncs. run.sh reads the result with ntfs-3g.
 *   bigdir <image> <n>
 * Copyright (C) 2026 strongt1me
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "third_party/ntfsb/bdev.h"
#include "third_party/ntfsb/ntfs.h"
#include "third_party/ntfsb/part.h"

static int g_n, g_fail;
#define CHECK(c) do { g_n++; if(!(c)) { g_fail++; if(g_fail < 20) printf("FAIL line %d: %s\n", __LINE__, #c); } } while(0)

static void fname(char *out, size_t n, int i) { snprintf(out, n, "Datei_%05d_Mixed.TXT", i); }

int main(int argc, char **argv) {
  if(argc != 3) return 2;
  int n = atoi(argv[2]);
  bdev_t *b = bdev_open(argv[1]);
  if(!b) return 1;
  part_t parts[PART_MAX];
  int np = part_scan(b, parts, PART_MAX);
  ntfs_vol_t *v = NULL;
  for(int i = 0; i < np && !v; i++) {
    bdev_t *s = bdev_slice(b, parts[i].offset, parts[i].size);
    char err[160];
    if(s && part_is_ntfs(s) && ntfs_mount(s, &v, err, sizeof(err)) == 0) break;
    if(s) bdev_close(s);
    v = NULL;
  }
  if(!v) { printf("not mounted\n"); return 1; }
  char why[300];
  CHECK(ntfs_enable_write(v, why, sizeof(why)) == 0);
  uint64_t root = 0, dir = 0, ino = 0;
  CHECK(ntfs_lookup(v, "/", &root) == 0);
  CHECK(ntfs_mkdir(v, root, "gross", 0, &dir) == 0);

  char name[64], path[96];
  static char data[700];
  for(int i = 0; i < n; i++) {
    fname(name, sizeof(name), i);
    ntfs_wfile_t *f = NULL;
    memset(data, 'a' + i % 26, sizeof(data));
    CHECK(ntfs_create(v, dir, name, sizeof(data), 0, &f) == 0 && ntfs_wwrite(f, data, sizeof(data)) == 0 && ntfs_wclose(f) == 0);
  }
  CHECK(ntfs_sync(v) == 0);

  int miss = 0, other = 0;
  for(int i = 0; i < n; i++) {
    fname(name, sizeof(name), i);
    snprintf(path, sizeof(path), "/gross/%s", name);
    if(ntfs_lookup(v, path, &ino)) miss++;
    for(char *p = path; *p; p++) *p = (char)(i % 2 ? toupper((unsigned char)*p) : tolower((unsigned char)*p));
    if(ntfs_lookup(v, path, &ino)) other++;          /* another case: NTFS names compare without case */
  }
  CHECK(miss == 0);
  CHECK(other == 0);
  int absent = 0;
  for(int i = n; i < n + 200; i++) {
    fname(name, sizeof(name), i);
    snprintf(path, sizeof(path), "/gross/%s", name);
    if(ntfs_lookup(v, path, &ino) == -ENOENT) absent++;
  }
  CHECK(absent == 200);
  CHECK(ntfs_lookup(v, "/gross/Datei_", &ino) == -ENOENT);
  CHECK(ntfs_lookup(v, "/gross/zzzz", &ino) == -ENOENT);
  CHECK(ntfs_lookup(v, "/gross/0", &ino) == -ENOENT);
  {
    ntfs_wfile_t *f = NULL;
    CHECK(ntfs_create(v, dir, "DATEI_00007_MIXED.txt", 10, 0, &f) == -EEXIST);   /* taken, in another case */
    CHECK(ntfs_mkdir(v, dir, "datei_00008_mixed.TXT", 0, NULL) == -EEXIST);
  }

  int del = 0;
  for(int i = 0; i < n; i += 2) {
    fname(name, sizeof(name), i);
    if(ntfs_unlink(v, dir, name) == 0) del++;
  }
  CHECK(del == (n + 1) / 2);
  CHECK(ntfs_sync(v) == 0);
  miss = 0;
  int still = 0;
  for(int i = 0; i < n; i++) {
    fname(name, sizeof(name), i);
    snprintf(path, sizeof(path), "/gross/%s", name);
    int rc = ntfs_lookup(v, path, &ino);
    if(i % 2 == 0 && rc == 0) still++;
    if(i % 2 == 1 && rc != 0) miss++;
  }
  CHECK(still == 0);
  CHECK(miss == 0);
  /* Windows' metadata below $Extend (record numbers from 24 up, past the "< 24" guard) is neither deleted nor written into (24.11.2026,
     upstream PR #16); mkntfs makes $Quota, $ObjId and $Reparse there */
  uint64_t ext = 0;
  CHECK(ntfs_lookup(v, "/$Extend", &ext) == 0);
  CHECK(ntfs_lookup(v, "/$Extend/$Quota", &ino) == 0);
  CHECK(ntfs_unlink(v, ext, "$Quota") == -EPERM);
  CHECK(ntfs_unlink(v, ext, "$ObjId") == -EPERM);
  CHECK(ntfs_mkdir(v, ext, "neu", 0, NULL) == -EPERM);
  CHECK(ntfs_lookup(v, "/$Extend/$Quota", &ino) == 0);
  ntfs_umount(v);
  bdev_close(b);
  printf("bigdir: %d checks, %d failed (%d files, %d deleted)\n", g_n, g_fail, n, del);
  return g_fail ? 1 : 0;
}
