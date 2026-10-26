/* SPDX-License-Identifier: GPL-3.0-or-later
 * Decrypting the SELF/SPRX files of a dump (25.10.2026).
 *
 * Taken over from ps5-app-dumper 2.10 (Copyright (C) 2025 EchoStretch, Copyright (C) 2026 slopmaster33; GPL-3.0-or-later;
 * decrypt.c), which uses the self pager of idlesauce (ps5-self-pager, selfpager.c; its SELF structures follow Cryptogenic's
 * PS5-SELF-Decrypter) -- rewritten for this app: the kernel part and the table of firmwares are the original's (the table is
 * checked against it by a script), the rest is ours: the plain ELF is written segment by segment instead of being built whole in
 * memory, through a temporary name that replaces the copy only when the file is whole, and every size read from a file is
 * checked before it is used.
 *
 * How it works: the console's own pager decrypts a SELF when its pages are read. The self pager makes the kernel's "vnode
 * pager" entry in the pager table point to the SELF pager for the length of one mmap() call, so that the mapping gets the
 * SELF pager's object; the entry is put back at once, and the pages (read through that object, not through the table) come
 * out decrypted. That is a write into kernel memory, with addresses that belong to one firmware each, so:
 *   - only firmwares that the table lists are ever touched, and both pointers read from the table must look like kernel
 *     pointers before anything is written;
 *   - the window is one mmap() call, under a lock of its own, and the value that was there before is written back, read
 *     again and compared, whatever mmap() answered; if that fails three times the module refuses everything from then on
 *     and tells the caller to restart the console (res->critical);
 *   - it only ever runs on the dump's own thread, one run at a time. The app itself maps no file anywhere else. */

#include "dump_self.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "ps5tm.h"

#ifdef PS5TM_HOST_TEST
/* the rig stands in for the console: firmware, kernel memory and the mapping come from the test */
extern uint32_t (*ds_test_fw)(void);
extern uint64_t (*ds_test_get)(intptr_t addr);
extern int32_t (*ds_test_set)(intptr_t addr, uint64_t val);
extern void *(*ds_test_map)(size_t len, int fd, off_t off);
extern intptr_t ds_test_base;
#define ds_fw() ds_test_fw()
#define ds_kget(a) ds_test_get(a)
#define ds_kset(a, v) ds_test_set((a), (v))
#define DS_DATA_BASE ds_test_base
#ifndef MAP_ALIGNED
#define MAP_ALIGNED(n) ((n) << 24)
#endif
#else
#include <ps5/kernel.h>
#define ds_fw() kernel_get_fw_version()
#define ds_kget(a) kernel_getlong(a)
#define ds_kset(a, v) kernel_setlong((a), (v))
#define DS_DATA_BASE KERNEL_ADDRESS_DATA_BASE
#endif

#define DS_E_IO        (-2)
#define DS_E_INTERNAL  (-3)
#define DS_E_FW        (-4)
#define DS_E_NOT_SELF  (-5)
#define DS_E_DECRYPT   (-6)
#define DS_E_TOO_BIG   (-7)

#define SELF_ORBIS_MAGIC    0x1D3D154Fu
#define SELF_PROSPERO_MAGIC 0xEEF51454u
#define SUPERPAGE_SIZE      0x200000u
#define PT_LOAD_            1u
#define PT_SCE_DYNLIBDATA   0x61000000u
#define PT_SCE_RELRO        0x61000010u
#define PT_SCE_COMMENT      0x6FFFFF00u
#define PT_SCE_VERSION      0x6FFFFF01u

#define DS_PHNUM_MAX   256u
#define DS_SEGS_MAX    512u
#define DS_SEG_MAX     (1ull << 30)          /* a segment is held whole in memory while it is decrypted */
#define DS_FILE_MAX    (4ull << 30)
#define DS_BUF         (1u << 20)
#define DS_DEPTH       24
#define DS_ENTRIES_MAX 2000000u
#define DS_SANDBOX     "/mnt/sandbox/pfsmnt"

struct self_header {
  uint32_t magic;
  uint8_t  version, mode, endian, attributes;
  uint32_t key_type;
  uint16_t header_size, metadata_size;
  uint64_t file_size;
  uint16_t segment_count, flags;
  uint8_t  pad[4];
};

typedef struct {
  uint8_t  e_ident[16];
  uint16_t e_type, e_machine;
  uint32_t e_version;
  uint64_t e_entry, e_phoff, e_shoff;
  uint32_t e_flags;
  uint16_t e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx;
} ds_ehdr_t;

typedef struct {
  uint32_t p_type, p_flags;
  uint64_t p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_align;
} ds_phdr_t;

_Static_assert(sizeof(struct self_header) == 0x20, "SELF header");
_Static_assert(sizeof(ds_ehdr_t) == 64, "ELF header");
_Static_assert(sizeof(ds_phdr_t) == 56, "ELF program header");

/* ---------------------------------------------------------------- the table (from selfpager.c, generated) */

static const struct { uint16_t fw; uint32_t off; } k_pagertab[] = {
  { 0x0100, 0xC27C40 },
  { 0x0101, 0xC27C40 },
  { 0x0102, 0xC27C40 },
  { 0x0105, 0xC27C40 },
  { 0x0110, 0xC27C40 },
  { 0x0111, 0xC27C40 },
  { 0x0112, 0xC27C40 },
  { 0x0113, 0xC27CA0 },
  { 0x0114, 0xC27CA0 },
  { 0x0200, 0xC4EF60 },
  { 0x0220, 0xC4EFA0 },
  { 0x0225, 0xC4EFA0 },
  { 0x0226, 0xC4EFA0 },
  { 0x0230, 0xC4F120 },
  { 0x0250, 0xC4F120 },
  { 0x0270, 0xC4F120 },
  { 0x0300, 0xCAF8C0 },
  { 0x0310, 0xCAF8C0 },
  { 0x0320, 0xCAF8C0 },
  { 0x0321, 0xCAF8C0 },
  { 0x0400, 0xD20840 },
  { 0x0402, 0xD20840 },
  { 0x0403, 0xD20840 },
  { 0x0450, 0xD20840 },
  { 0x0451, 0xD20840 },
  { 0x0500, 0xE0FEF0 },
  { 0x0502, 0xE0FEF0 },
  { 0x0510, 0xE0FEF0 },
  { 0x0550, 0xE0FEF0 },
  { 0x0600, 0xE30410 },
  { 0x0602, 0xE30410 },
  { 0x0650, 0xE30410 },
  { 0x0700, 0xE310C0 },
  { 0x0701, 0xE310C0 },
  { 0x0720, 0xE31180 },
  { 0x0740, 0xE31180 },
  { 0x0760, 0xE31180 },
  { 0x0761, 0xE31180 },
  { 0x0800, 0xE31250 },
  { 0x0820, 0xE31250 },
  { 0x0840, 0xE31250 },
  { 0x0860, 0xE31250 },
  { 0x0900, 0xDE0420 },
  { 0x0905, 0xDE0420 },
  { 0x0920, 0xDE0420 },
  { 0x0940, 0xDE0420 },
  { 0x0960, 0xDE0420 },
  { 0x1000, 0xDE04F0 },
  { 0x1001, 0xDE04F0 },
  { 0x1020, 0xDE04F0 },
  { 0x1040, 0xDE04F0 },
  { 0x1060, 0xDE04F0 },
  { 0x1100, 0xDF1940 },
  { 0x1120, 0xDF1940 },
  { 0x1140, 0xDF1960 },
  { 0x1160, 0xDF1960 },
  { 0x1200, 0xDF2860 },
  { 0x1202, 0xDF2860 },
  { 0x1220, 0xDF2860 },
  { 0x1240, 0xDF2860 },
  { 0x1260, 0xDF2860 },
  { 0x1270, 0xDF2860 },
  { 0x1300, 0xE038D0 },
  { 0x1320, 0xE038D0 },
  { 0x1340, 0xE03910 },
  { 0x1342, 0xE03910 },
  { 0x1360, 0xE03910 },
};
#define PAGERTAB_N (sizeof(k_pagertab) / sizeof(k_pagertab[0]))

static const int k_vnode_index = 2;
static const int k_self_index = 7;

static pthread_mutex_t g_win_mx = PTHREAD_MUTEX_INITIALIZER;   /* the window in the pager table */
static pthread_mutex_t g_run_mx = PTHREAD_MUTEX_INITIALIZER;   /* one run at a time */
static int      g_ready;
static int      g_broken;                                      /* the pointer could not be put back */
static uint16_t g_fwver;
static intptr_t g_pagertab, g_vnode_ops, g_self_ops;

static int
table_find(uint16_t fwver, uint32_t *off) {
  for(size_t i = 0; i < PAGERTAB_N; i++) {
    if(k_pagertab[i].fw == fwver) {
      *off = k_pagertab[i].off;
      return 1;
    }
  }
  return 0;
}

int
ps5tm_dump_self_supported(void) {
  uint32_t off;
  return table_find((uint16_t)(ds_fw() >> 16), &off);
}

static int
looks_like_kernel_pointer(uint64_t v) {
  return (v >> 47) == 0x1FFFFu;
}

/* 0, or an errno-like code: ENOSYS firmware not in the table, EINVAL the table does not look as expected */
static int
self_init(void) {
  if(g_ready) return 0;
  g_fwver = (uint16_t)(ds_fw() >> 16);
  uint32_t off;
  if(!table_find(g_fwver, &off)) return ENOSYS;
  intptr_t tab = DS_DATA_BASE + (intptr_t)off;
  uint64_t vn = ds_kget(tab + k_vnode_index * 8);
  uint64_t sf = ds_kget(tab + k_self_index * 8);
  if(!looks_like_kernel_pointer(vn) || !looks_like_kernel_pointer(sf) || vn == sf) return EINVAL;
  g_pagertab = tab;
  g_vnode_ops = (intptr_t)vn;
  g_self_ops = (intptr_t)sf;
  g_ready = 1;
  return 0;
}

/* mmap() of a SELF: the same answer as mmap(); errno is ENOSYS for a firmware that is not supported. *critical is set when the
   pointer could not be restored. */
static void *
self_map(size_t len, int flags, int fd, off_t off, int *critical) {
  void *res;
  int eno;
  pthread_mutex_lock(&g_win_mx);
  if(g_broken) {
    pthread_mutex_unlock(&g_win_mx);
    errno = EIO;
    if(critical) *critical = 1;
    return MAP_FAILED;
  }
  int ir = self_init();
  if(ir != 0) {
    pthread_mutex_unlock(&g_win_mx);
    errno = ir;
    return MAP_FAILED;
  }
  const intptr_t slot = g_pagertab + k_vnode_index * 8;
  if((intptr_t)ds_kget(slot) != g_vnode_ops) {                  /* not what was read at the start: write nothing */
    pthread_mutex_unlock(&g_win_mx);
    errno = EBUSY;
    return MAP_FAILED;
  }
  if(ds_kset(slot, (uint64_t)g_self_ops) != 0) {
    (void)ds_kset(slot, (uint64_t)g_vnode_ops);
    pthread_mutex_unlock(&g_win_mx);
    errno = EIO;
    return MAP_FAILED;
  }
#ifdef PS5TM_HOST_TEST
  res = ds_test_map ? ds_test_map(len, fd, off) : mmap(NULL, len, PROT_READ, flags, fd, off);
#else
  res = mmap(NULL, len, PROT_READ, flags, fd, off);
#endif
  eno = errno;
  int restored = 0;
  for(int i = 0; i < 3 && !restored; i++) {
    if(ds_kset(slot, (uint64_t)g_vnode_ops) == 0 && (intptr_t)ds_kget(slot) == g_vnode_ops) restored = 1;
  }
  if(!restored) {
    g_broken = 1;
    if(critical) *critical = 1;
    PS5TM_ERROR("self_pager_stuck", "Entschlüsseln: Der Kernel-Zeiger ließ sich nicht zurückstellen. Die Konsole sollte neu gestartet werden.");
    if(res != MAP_FAILED) munmap(res, len);
    pthread_mutex_unlock(&g_win_mx);
    errno = EIO;
    return MAP_FAILED;
  }
  pthread_mutex_unlock(&g_win_mx);
  errno = eno;
  return res;
}

/* ---------------------------------------------------------------- small helpers */

static int
cancelled(volatile int *c) {
  return c && __atomic_load_n(c, __ATOMIC_ACQUIRE) != 0;
}

/* 1 whole, 0 short, -1 error */
static int
pread_full(int fd, void *buf, size_t n, off_t off) {
  size_t got = 0;
  while(got < n) {
    ssize_t r = pread(fd, (char *)buf + got, n - got, off + (off_t)got);
    if(r < 0) {
      if(errno == EINTR) continue;
      return -1;
    }
    if(r == 0) return 0;
    got += (size_t)r;
  }
  return 1;
}

static int
pwrite_full(int fd, const void *buf, size_t n, off_t off) {
  size_t put = 0;
  while(put < n) {
    ssize_t r = pwrite(fd, (const char *)buf + put, n - put, off + (off_t)put);
    if(r < 0) {
      if(errno == EINTR) continue;
      return -1;
    }
    if(r == 0) {
      errno = EIO;
      return -1;
    }
    put += (size_t)r;
  }
  return 0;
}

static int
mkdir_p(const char *path) {
  char tmp[PATH_MAX];
  size_t n = strlen(path);
  if(n == 0 || n >= sizeof(tmp)) return ENAMETOOLONG;
  memcpy(tmp, path, n + 1);
  for(size_t i = 1; i < n; i++) {
    if(tmp[i] != '/') continue;
    tmp[i] = 0;
    if(mkdir(tmp, 0777) != 0 && errno != EEXIST) return errno;
    tmp[i] = '/';
  }
  if(mkdir(tmp, 0777) != 0 && errno != EEXIST) return errno;
  return 0;
}

static int
ext_ok(const char *name) {
  const char *e = strrchr(name, '.');
  if(!e) return 0;
  return !strcasecmp(e, ".elf") || !strcasecmp(e, ".self") || !strcasecmp(e, ".prx") || !strcasecmp(e, ".sprx") || !strcasecmp(e, ".bin");
}

static int
is_union_dir(const char *parent, const char *name) {
  return strlen(name) == sizeof("PPSA00000-app0-patch0-union") - 1 && strncmp(parent, DS_SANDBOX, sizeof(DS_SANDBOX) - 1) == 0 &&
         strncmp(name + 9, "-app0-patch0-union", 18) == 0;
}

/* ---------------------------------------------------------------- reading a SELF */

typedef struct {
  ds_ehdr_t  eh;
  ds_phdr_t *ph;
  uint64_t   out_size;
  int        ver_idx;
} ds_plan_t;

static int
seg_wanted(const ds_phdr_t *p) {
  return (p->p_type == PT_LOAD_ || p->p_type == PT_SCE_DYNLIBDATA || p->p_type == PT_SCE_RELRO || p->p_type == PT_SCE_COMMENT) && p->p_filesz != 0;
}

/* The checks of decrypt_self(), and bounds on everything the headers say. 0, or one of the DS_E_ codes. */
static int
plan_read(int fd, ds_plan_t *pl) {
  memset(pl, 0, sizeof(*pl));
  pl->ver_idx = -1;
  struct self_header sh;
  int r = pread_full(fd, &sh, sizeof(sh), 0);
  if(r < 0) return DS_E_IO;
  if(r == 0) return DS_E_NOT_SELF;
  if(sh.magic != SELF_ORBIS_MAGIC && sh.magic != SELF_PROSPERO_MAGIC) return DS_E_NOT_SELF;
  if(sh.segment_count > DS_SEGS_MAX) return DS_E_INTERNAL;
  off_t eh_off = (off_t)sizeof(sh) + (off_t)32 * sh.segment_count;
  if(pread_full(fd, &pl->eh, sizeof(pl->eh), eh_off) != 1) return DS_E_IO;
  if(pl->eh.e_ident[0] != 0x7f || pl->eh.e_ident[1] != 'E' || pl->eh.e_ident[2] != 'L' || pl->eh.e_ident[3] != 'F') return DS_E_INTERNAL;
  if(pl->eh.e_phentsize != sizeof(ds_phdr_t) || pl->eh.e_phnum == 0 || pl->eh.e_phnum > DS_PHNUM_MAX) return DS_E_INTERNAL;
  size_t phsz = (size_t)pl->eh.e_phnum * sizeof(ds_phdr_t);
  pl->ph = malloc(phsz);
  if(!pl->ph) return DS_E_INTERNAL;
  if(pread_full(fd, pl->ph, phsz, eh_off + (off_t)sizeof(pl->eh)) != 1) return DS_E_IO;
  for(unsigned i = 0; i < pl->eh.e_phnum; i++) {
    const ds_phdr_t *p = &pl->ph[i];
    uint64_t end = p->p_offset + p->p_filesz;
    if(end < p->p_offset || end > DS_FILE_MAX) return DS_E_TOO_BIG;
    if(end > pl->out_size) pl->out_size = end;
    if(p->p_type == PT_SCE_VERSION) pl->ver_idx = (int)i;
  }
  if(pl->out_size == 0) return DS_E_INTERNAL;
  if(pl->out_size < sizeof(pl->eh) + phsz) return DS_E_INTERNAL;        /* the headers have to fit in front */
  return 0;
}

/* Writes the plain ELF into out_fd. 0, 1 cancelled, or a DS_E_ code. */
static int
elf_extract(int fd, const ds_plan_t *pl, int out_fd, char *buf, volatile int *cancel, int *critical) {
  if(ftruncate(out_fd, (off_t)pl->out_size) != 0) return DS_E_IO;
  const uint16_t fwver = (uint16_t)(ds_fw() >> 16);
  for(unsigned i = 0; i < pl->eh.e_phnum; i++) {
    const ds_phdr_t *p = &pl->ph[i];
    if(!seg_wanted(p)) continue;
    if(cancelled(cancel)) return 1;
    if(p->p_filesz > DS_SEG_MAX) return DS_E_TOO_BIG;
    uint64_t index = i;
    off_t off = (off_t)(index << 32);
    if(fwver >= 0x900) {                                    /* read here: g_fwver is only set by the first mapping */
      uint64_t aligned_vaddr = p->p_vaddr & ~(p->p_align - 1);
      off |= (off_t)(aligned_vaddr & (SUPERPAGE_SIZE - 1));
    }
    int flags = (int)(MAP_PRIVATE | MAP_ALIGNED(p->p_align));
    void *seg = self_map((size_t)p->p_filesz, flags, fd, off, critical);
    if(seg == MAP_FAILED) return errno == ENOSYS ? DS_E_FW : DS_E_INTERNAL;
    if(mlock(seg, (size_t)p->p_filesz) != 0) {
      munmap(seg, (size_t)p->p_filesz);
      return DS_E_DECRYPT;
    }
    int wr = 0;
    for(uint64_t done = 0; done < p->p_filesz && !wr; ) {
      size_t n = p->p_filesz - done > DS_BUF ? DS_BUF : (size_t)(p->p_filesz - done);
      wr = pwrite_full(out_fd, (const char *)seg + done, n, (off_t)(p->p_offset + done));
      done += n;
    }
    munlock(seg, (size_t)p->p_filesz);
    munmap(seg, (size_t)p->p_filesz);
    if(wr) return DS_E_IO;
  }
  if(pl->ver_idx >= 0) {                                    /* the version segment lies at the end of the SELF, plain */
    const ds_phdr_t *p = &pl->ph[pl->ver_idx];
    struct stat st;
    if(fstat(fd, &st) != 0) return DS_E_IO;
    if(p->p_filesz > DS_SEG_MAX || p->p_filesz > (uint64_t)st.st_size) return DS_E_TOO_BIG;
    uint64_t from = (uint64_t)st.st_size - p->p_filesz;
    for(uint64_t done = 0; done < p->p_filesz; ) {
      size_t n = p->p_filesz - done > DS_BUF ? DS_BUF : (size_t)(p->p_filesz - done);
      int r = pread_full(fd, buf, n, (off_t)(from + done));
      if(r != 1) return DS_E_IO;
      if(pwrite_full(out_fd, buf, n, (off_t)(p->p_offset + done)) != 0) return DS_E_IO;
      done += n;
    }
  }
  size_t phsz = (size_t)pl->eh.e_phnum * sizeof(ds_phdr_t);
  if(pwrite_full(out_fd, &pl->eh, sizeof(pl->eh), 0) != 0) return DS_E_IO;
  if(pwrite_full(out_fd, pl->ph, phsz, (off_t)sizeof(pl->eh)) != 0) return DS_E_IO;
  return 0;
}

/* ---------------------------------------------------------------- the walk */

typedef struct {
  const char   *dst_root;
  const char   *suffix;
  int           fself, keep_plain, count_only;
  volatile int *cancel;
  dj_self_cb_t  cb;
  void         *cbctx;
  dj_self_res_t *res;
  unsigned      done, total;
  uint64_t      largest, bytes;
  unsigned      entries;
  char         *buf;
  char         *sp, *dp, *tp, *t2p, *dec;       /* PATH_MAX each */
  int           stop;                           /* 1 cancelled, DJ_SELF_UNSUPPORTED, or -1 */
} ds_ctx_t;

static void
say(ds_ctx_t *c, const char *rel, const char *fmt, ...) __attribute__((format(printf, 3, 4)));

static void
say(ds_ctx_t *c, const char *rel, const char *fmt, ...) {
  if(!c->cb) return;
  char line[400];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(line, sizeof(line), fmt, ap);
  va_end(ap);
  c->cb(c->cbctx, rel, c->done, c->total, line);
}

static void
progress(ds_ctx_t *c, const char *rel) {
  if(c->cb) c->cb(c->cbctx, rel, c->done, c->total, NULL);
}

static void
hint(ds_ctx_t *c, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

static void
hint(ds_ctx_t *c, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(c->res->why, sizeof(c->res->why), fmt, ap);
  va_end(ap);
}

static int
copy_plain(const char *from, const char *to, char *buf) {
  int in = open(from, O_RDONLY);
  if(in < 0) return errno ? errno : EIO;
  int out = open(to, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if(out < 0) {
    int e = errno ? errno : EIO;
    close(in);
    return e;
  }
  int rc = 0;
  for(;;) {
    ssize_t r = read(in, buf, DS_BUF);
    if(r < 0) {
      if(errno == EINTR) continue;
      rc = errno ? errno : EIO;
      break;
    }
    if(r == 0) break;
    size_t put = 0;
    while(put < (size_t)r) {
      ssize_t w = write(out, buf + put, (size_t)r - put);
      if(w < 0) {
        if(errno == EINTR) continue;
        rc = errno ? errno : EIO;
        break;
      }
      put += (size_t)w;
    }
    if(rc) break;
  }
  if(!rc && fsync(out) != 0) rc = errno ? errno : EIO;
  close(in);
  close(out);
  if(rc) unlink(to);
  return rc;
}

static void
parent_dir(const char *path, char *out, size_t n) {
  snprintf(out, n, "%s", path);
  char *s = strrchr(out, '/');
  if(s) *s = 0;
}

/* One file: src is in the mounted tree, dst is where its plain version goes (dst_root + the same relative path). */
static void
one_file(ds_ctx_t *c, const char *src, const char *dst, const char *rel) {
  dj_self_res_t *res = c->res;
  int fd = open(src, O_RDONLY);
  if(fd < 0) {
    res->failed++;
    hint(c, "%s: ließ sich nicht öffnen", rel);
    say(c, NULL, "Entschlüsseln: %s ließ sich nicht öffnen (%s)", rel, strerror(errno));
    return;
  }
  ds_plan_t pl;
  int rc = plan_read(fd, &pl);
  if(rc == DS_E_NOT_SELF) {
    res->not_self++;
    free(pl.ph);
    close(fd);
    return;
  }
  if(rc != 0) {
    res->failed++;
    if(rc == DS_E_TOO_BIG) hint(c, "%s: ist für das Entschlüsseln zu groß", rel);
    else hint(c, "%s: Kopf der Datei ist ungewöhnlich", rel);
    say(c, NULL, "Entschlüsseln: %s bleibt unverändert (%s)", rel,
        rc == DS_E_TOO_BIG ? "zu groß" : rc == DS_E_IO ? "Lesefehler" : "ungewöhnlicher Kopf");
    free(pl.ph);
    close(fd);
    return;
  }
  char dir[PATH_MAX];
  parent_dir(dst, dir, sizeof(dir));
  int e = mkdir_p(dir);
  if(e) {
    res->failed++;
    hint(c, "%s: Zielordner ließ sich nicht anlegen", rel);
    say(c, NULL, "Entschlüsseln: Zielordner für %s ließ sich nicht anlegen (%s)", rel, strerror(e));
    free(pl.ph);
    close(fd);
    return;
  }
  snprintf(c->tp, PATH_MAX, "%s%s", dst, c->suffix);
  int out = open(c->tp, O_RDWR | O_CREAT | O_TRUNC, 0644);
  if(out < 0) {
    res->failed++;
    hint(c, "%s: Zieldatei ließ sich nicht anlegen", rel);
    say(c, NULL, "Entschlüsseln: Zieldatei für %s ließ sich nicht anlegen (%s)", rel, strerror(errno));
    free(pl.ph);
    close(fd);
    return;
  }
  int critical = 0;
  rc = elf_extract(fd, &pl, out, c->buf, c->cancel, &critical);
  if(critical) res->critical = 1;
  if(rc == 0 && fsync(out) != 0) rc = DS_E_IO;
  close(out);
  close(fd);
  uint64_t out_size = pl.out_size;
  free(pl.ph);
  if(rc == 1) {
    unlink(c->tp);
    c->stop = 1;
    return;
  }
  if(rc == DS_E_FW) {
    unlink(c->tp);
    c->stop = DJ_SELF_UNSUPPORTED;
    return;
  }
  if(rc != 0) {
    unlink(c->tp);
    res->failed++;
    const char *why = rc == DS_E_DECRYPT ? "schon im FSELF-Format oder die Schlüssel fehlen"
                    : rc == DS_E_TOO_BIG ? "ein Abschnitt ist für das Entschlüsseln zu groß"
                    : rc == DS_E_IO ? "Schreib- oder Lesefehler" : "Entschlüsseln gescheitert";
    if(rc == DS_E_DECRYPT) hint(c, "%s: schon im FSELF-Format oder die Schlüssel fehlen", rel);
    else if(rc == DS_E_TOO_BIG) hint(c, "%s: ein Abschnitt ist für das Entschlüsseln zu groß", rel);
    else if(rc == DS_E_IO) hint(c, "%s: Schreib- oder Lesefehler", rel);
    else hint(c, "%s: Entschlüsseln gescheitert", rel);
    say(c, NULL, "Entschlüsseln: %s bleibt unverändert (%s)", rel, why);
    if(res->critical) c->stop = -1;
    return;
  }

  res->bytes += out_size;
  if(c->fself) {
    /* the plain ELF is kept below decrypted/ and the file in place becomes an FSELF; if that fails, the plain ELF stays in place */
    snprintf(c->dec, PATH_MAX, "%s/decrypted/%s", c->dst_root, rel);
    char ddir[PATH_MAX];
    parent_dir(c->dec, ddir, sizeof(ddir));
    int de = mkdir_p(ddir);
    if(!de) de = copy_plain(c->tp, c->dec, c->buf);
    snprintf(c->t2p, PATH_MAX, "%s%s2", dst, c->suffix);
    char fwhy[160] = "";
    if(de == 0 && ps5tm_dump_elf2fself(c->tp, c->t2p, fwhy, sizeof(fwhy)) == 0 && rename(c->t2p, dst) == 0) {
      unlink(c->tp);
      res->fself++;
      res->decrypted++;
      return;
    }
    unlink(c->t2p);
    say(c, NULL, "FSELF: %s bleibt als einfache ELF-Datei (%s)", rel, de ? strerror(de) : fwhy[0] ? fwhy : "Fehler beim Umschreiben");
    hint(c, "%s: FSELF ließ sich nicht erzeugen", rel);
  }
  if(c->keep_plain && !c->fself) {
    /* as the dumper does for the backport: the plain file is also kept below decrypted/, the one in place is patched later */
    snprintf(c->dec, PATH_MAX, "%s/decrypted/%s", c->dst_root, rel);
    char ddir[PATH_MAX];
    parent_dir(c->dec, ddir, sizeof(ddir));
    int de = mkdir_p(ddir);
    if(!de) de = copy_plain(c->tp, c->dec, c->buf);
    if(de) say(c, NULL, "Entschlüsseln: Die unveränderte Kopie von %s im Ordner decrypted ließ sich nicht anlegen (%s)", rel, strerror(de));
  }
  if(rename(c->tp, dst) != 0) {
    int er = errno;
    unlink(c->tp);
    res->failed++;
    hint(c, "%s: Ersetzen der Kopie scheiterte", rel);
    say(c, NULL, "Entschlüsseln: %s ließ sich nicht an seinen Platz legen (%s)", rel, strerror(er));
    return;
  }
  res->decrypted++;
}

static void
walk(ds_ctx_t *c, size_t slen, size_t dlen, int depth) {
  if(depth > DS_DEPTH) return;
  DIR *d = opendir(c->sp);
  if(!d) return;
  struct dirent *ent;
  while(!c->stop && (ent = readdir(d)) != NULL) {
    const char *name = ent->d_name;
    if(!strcmp(name, ".") || !strcmp(name, "..")) continue;
    if(cancelled(c->cancel)) {
      c->stop = 1;
      break;
    }
    if(++c->entries > DS_ENTRIES_MAX) {
      c->stop = -1;
      hint(c, "zu viele Einträge im Spielordner");
      break;
    }
    size_t nl = strlen(name);
    if(slen + 1 + nl >= PATH_MAX || dlen + 1 + nl + 16 >= PATH_MAX) continue;
    c->sp[slen] = '/';
    memcpy(c->sp + slen + 1, name, nl + 1);
    c->dp[dlen] = '/';
    memcpy(c->dp + dlen + 1, name, nl + 1);
    unsigned char type = ent->d_type;
    struct stat st;
    if(type == DT_UNKNOWN) {
      type = lstat(c->sp, &st) == 0 ? (S_ISDIR(st.st_mode) ? DT_DIR : S_ISREG(st.st_mode) ? DT_REG : DT_UNKNOWN) : DT_UNKNOWN;
    }
    if(type == DT_DIR) {
      c->sp[slen] = 0;
      int skip = is_union_dir(c->sp, name);
      c->sp[slen] = '/';
      if(!skip) walk(c, slen + 1 + nl, dlen + 1 + nl, depth + 1);
    } else if(type == DT_REG && ext_ok(name)) {
      if(c->count_only) {
        c->total++;
        if(lstat(c->sp, &st) == 0) {
          c->bytes += (uint64_t)st.st_size;
          if((uint64_t)st.st_size > c->largest) c->largest = (uint64_t)st.st_size;
        }
      } else {
        c->done++;
        const char *rel = c->dp + strlen(c->dst_root) + 1;
        progress(c, rel);
        one_file(c, c->sp, c->dp, rel);
      }
    }
    c->sp[slen] = 0;
    c->dp[dlen] = 0;
  }
  closedir(d);
}

static ds_ctx_t *
ctx_new(void) {
  ds_ctx_t *c = calloc(1, sizeof(*c));
  if(!c) return NULL;
  c->buf = malloc(DS_BUF);
  c->sp = malloc(PATH_MAX);
  c->dp = malloc(PATH_MAX);
  c->tp = malloc(PATH_MAX);
  c->t2p = malloc(PATH_MAX);
  c->dec = malloc(PATH_MAX);
  if(!c->buf || !c->sp || !c->dp || !c->tp || !c->t2p || !c->dec) {
    free(c->buf); free(c->sp); free(c->dp); free(c->tp); free(c->t2p); free(c->dec); free(c);
    return NULL;
  }
  return c;
}

static void
ctx_free(ds_ctx_t *c) {
  free(c->buf); free(c->sp); free(c->dp); free(c->tp); free(c->t2p); free(c->dec); free(c);
}

int
ps5tm_dump_self_count(const char *src_root, volatile int *cancel, unsigned *count, uint64_t *largest, uint64_t *bytes) {
  if(count) *count = 0;
  if(largest) *largest = 0;
  if(bytes) *bytes = 0;
  if(!src_root || strlen(src_root) >= PATH_MAX - 2) return 0;
  ds_ctx_t *c = ctx_new();
  if(!c) return 0;
  dj_self_res_t res;
  memset(&res, 0, sizeof(res));
  c->res = &res;
  c->count_only = 1;
  c->cancel = cancel;
  c->dst_root = "";
  snprintf(c->sp, PATH_MAX, "%s", src_root);
  c->dp[0] = 0;
  walk(c, strlen(c->sp), 0, 0);
  if(count) *count = c->total;
  if(largest) *largest = c->largest;
  if(bytes) *bytes = c->bytes;
  int r = c->stop == 1 ? 1 : 0;
  ctx_free(c);
  return r;
}

int
ps5tm_dump_self_run(const char *src_root, const char *dst_root, const char *temp_suffix, int fself, int keep_plain,
                    volatile int *cancel, dj_self_cb_t cb, void *ctx, dj_self_res_t *res) {
  memset(res, 0, sizeof(*res));
  if(!ps5tm_dump_self_supported()) {
    snprintf(res->why, sizeof(res->why), "Entschlüsseln wird für diese Firmware nicht unterstützt.");
    return DJ_SELF_UNSUPPORTED;
  }
  if(!src_root || !dst_root || !temp_suffix || strlen(src_root) >= PATH_MAX - 2 || strlen(dst_root) >= PATH_MAX - 2) {
    snprintf(res->why, sizeof(res->why), "Der Pfad ist ungültig.");
    return -1;
  }
  if(pthread_mutex_trylock(&g_run_mx) != 0) {
    snprintf(res->why, sizeof(res->why), "Es läuft schon ein Entschlüsseln.");
    return -1;
  }
  ds_ctx_t *c = ctx_new();
  if(!c) {
    pthread_mutex_unlock(&g_run_mx);
    snprintf(res->why, sizeof(res->why), "Kein Speicher.");
    return -1;
  }
  c->res = res;
  c->dst_root = dst_root;
  c->suffix = temp_suffix;
  c->fself = fself;
  c->keep_plain = keep_plain || fself;
  c->cancel = cancel;
  c->cb = cb;
  c->cbctx = ctx;
  snprintf(c->sp, PATH_MAX, "%s", src_root);
  snprintf(c->dp, PATH_MAX, "%s", dst_root);
  size_t sl = strlen(c->sp), dl = strlen(c->dp);
  while(sl > 1 && c->sp[sl - 1] == '/') c->sp[--sl] = 0;
  while(dl > 1 && c->dp[dl - 1] == '/') c->dp[--dl] = 0;
  c->count_only = 1;
  walk(c, sl, dl, 0);
  res->total = c->total;
  c->count_only = 0;
  c->done = 0;
  c->entries = 0;
  c->largest = 0;
  c->sp[sl] = 0;
  c->dp[dl] = 0;
  if(c->stop == 0) walk(c, sl, dl, 0);
  int rc = 0;
  if(c->stop == 1) {
    rc = 1;
  } else if(c->stop == DJ_SELF_UNSUPPORTED) {
    snprintf(res->why, sizeof(res->why), "Entschlüsseln wird für diese Firmware nicht unterstützt.");
    rc = DJ_SELF_UNSUPPORTED;
  } else if(c->stop == -1) {
    if(res->critical) snprintf(res->why, sizeof(res->why), "Der Kernel-Zeiger ließ sich nicht zurückstellen. Bitte die Konsole neu starten.");
    rc = -1;
  }
  ctx_free(c);
  pthread_mutex_unlock(&g_run_mx);
  return rc;
}
