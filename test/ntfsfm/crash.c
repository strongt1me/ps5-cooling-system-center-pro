/* PS5 Cooling & System Center - Pro: interrupted index changes on NTFS (24.11.2026, after upstream PR #22 of PS5-NTFS-BROWSER).
 * Adding or removing a name can rewrite several index nodes of a folder (a block splits, two merge, a neighbour lends an entry). If the
 * stick is pulled between two of those writes, the folder must still list every name it had; a name listed twice or a lost index
 * block is fine (chkdsk tidies those up), a vanished name is not. crash.sh runs each step of this tool under a pwrite shim that ends
 * the process after write K, for every K, and checks the result with this tool and with ntfs-3g.
 *   crash <image> prep <n>        folder /c with n files (names from name_of), synced
 *   crash <image> add <name>      one more file in /c, synced
 *   crash <image> rm <name>       one file of /c removed, synced
 *   crash <image> check <list>    every name of the list file (one per line) is found in /c, by listing and by lookup
 * Copyright (C) 2026 strongt1me
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "third_party/ntfsb/bdev.h"
#include "third_party/ntfsb/ntfs.h"
#include "third_party/ntfsb/part.h"

static ntfs_vol_t *mount_image(const char *path) {
  bdev_t *b = bdev_open(path);
  if(!b) return NULL;
  part_t parts[PART_MAX];
  int np = part_scan(b, parts, PART_MAX);
  for(int i = 0; i < np; i++) {
    bdev_t *s = bdev_slice(b, parts[i].offset, parts[i].size);
    ntfs_vol_t *v = NULL;
    char err[160];
    if(s && part_is_ntfs(s) && ntfs_mount(s, &v, err, sizeof(err)) == 0) return v;
    if(s) bdev_close(s);
  }
  return NULL;
}

/* long names: few entries per index block, so splits and merges come early */
static void name_of(char *out, size_t n, int i) { snprintf(out, n, "Eintrag_%05d_mit_einem_recht_langen_Namen_fuer_den_Index", i * 10); }

typedef struct { char **names; int n, cap; } list_t;

static int list_cb(void *ctx, const char *name, const ntfs_stat_t *st) {
  (void)st;
  list_t *l = ctx;
  if(l->n == l->cap) {
    l->cap = l->cap ? l->cap * 2 : 256;
    l->names = realloc(l->names, (size_t)l->cap * sizeof(char *));
    if(!l->names) return -ENOMEM;
  }
  l->names[l->n++] = strdup(name);
  return 0;
}

int main(int argc, char **argv) {
  if(argc != 4) return 2;
  ntfs_vol_t *v = mount_image(argv[1]);
  if(!v) { printf("not mounted\n"); return 3; }
  const char *op = argv[2];
  uint64_t root = 0, dir = 0;
  if(ntfs_lookup(v, "/", &root)) return 3;
  char why[300];
  if(!strcmp(op, "check")) {
    if(ntfs_lookup(v, "/c", &dir)) { printf("folder /c not found\n"); return 1; }
    list_t l = {0};
    int rc = ntfs_readdir(v, dir, list_cb, &l);
    if(rc) { printf("listing failed: %d\n", rc); return 1; }
    FILE *f = fopen(argv[3], "r");
    if(!f) return 2;
    char line[512], path[600];
    int want = 0, unlisted = 0, unfound = 0;
    while(fgets(line, sizeof(line), f)) {
      line[strcspn(line, "\r\n")] = 0;
      if(!line[0]) continue;
      want++;
      int listed = 0;
      for(int i = 0; i < l.n && !listed; i++) listed = !strcmp(l.names[i], line);
      if(!listed) { unlisted++; if(unlisted < 4) printf("not listed: %s\n", line); }
      uint64_t ino = 0;
      snprintf(path, sizeof(path), "/c/%s", line);
      if(ntfs_lookup(v, path, &ino)) { unfound++; if(unfound < 4) printf("not found: %s\n", line); }
    }
    fclose(f);
    printf("check: %d names wanted, %d listed in all, %d not listed, %d not found\n", want, l.n, unlisted, unfound);
    return unlisted || unfound ? 1 : 0;
  }
  if(ntfs_enable_write(v, why, sizeof(why))) { printf("not writable: %s\n", why); return 3; }
  int rc = 0;
  if(!strcmp(op, "prep")) {
    int n = atoi(argv[3]);
    rc = ntfs_mkdir(v, root, "c", 0, &dir);
    char name[128];
    for(int i = 0; i < n && !rc; i++) {
      name_of(name, sizeof(name), i);
      ntfs_wfile_t *f = NULL;
      rc = ntfs_create(v, dir, name, 0, 0, &f);
      if(!rc) rc = ntfs_wclose(f);
    }
  } else if(!strcmp(op, "add")) {
    rc = ntfs_lookup(v, "/c", &dir);
    ntfs_wfile_t *f = NULL;
    if(!rc) rc = ntfs_create(v, dir, argv[3], 0, 0, &f);
    if(!rc) rc = ntfs_wclose(f);
  } else if(!strcmp(op, "rm")) {
    rc = ntfs_lookup(v, "/c", &dir);
    if(!rc) rc = ntfs_unlink(v, dir, argv[3]);
  } else {
    return 2;
  }
  if(!rc) rc = ntfs_sync(v);
  ntfs_umount(v);
  if(rc) printf("%s %s: %d\n", op, argv[3], rc);
  return rc ? 1 : 0;
}
