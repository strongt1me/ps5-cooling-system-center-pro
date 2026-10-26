/* SPDX-License-Identifier: GPL-3.0-or-later
 * A plain ELF written as an FSELF (fake-signed SELF), for the "re-sign as FSELF" setting of a dump (25.10.2026).
 *
 * Taken over from ps5-app-dumper 2.10 (Copyright (C) 2025 EchoStretch, Copyright (C) 2026 slopmaster33; GPL-3.0-or-later;
 * elf2fself.c), whose SELF structures follow Cryptogenic's PS5-SELF-Decrypter. The file that comes out has the layout of the
 * dumper's: one digest entry and one block entry for every PT_LOAD, PT_SCE_RELRO, PT_SCE_DYNLIBDATA and PT_SCE_COMMENT segment,
 * the ELF header and program headers, the extended info with the SHA-256 of the whole ELF, the NPDRM block, the metadata blocks
 * and footer, the segments, then the version segment. What differs: the entry properties are packed by hand (the dumper leaves
 * their bit order to the compiler), the sizes of the headers are checked against what fits in them, the program headers are read
 * once into memory instead of three times from the file, and a missing PT_SCE_VERSION segment is a zero (the dumper reads an
 * uninitialised structure there). */

#include "dump_self.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "sha256.h"

#define SELF_PS5_MAGIC     0xEEF51454u
#define PT_LOAD_           1u
#define PT_SCE_DYNLIBDATA  0x61000000u
#define PT_SCE_RELRO       0x61000010u
#define PT_SCE_COMMENT     0x6FFFFF00u
#define PT_SCE_VERSION     0x6FFFFF01u
#define META_ENTRY_SIZE    0x4000u
#define ROUND_UP(v, a)     (((v) + ((a) - 1)) & ~(uint64_t)((a) - 1))
#define FS_PHNUM_MAX       256u
#define FS_BUF             (1u << 16)

typedef struct {
  uint32_t magic;
  uint8_t  version, mode, endian, attrs;
  uint32_t key_type;
  uint16_t header_size, meta_size;
  uint64_t file_size;
  uint16_t num_entries, flags;
  uint8_t  pad[4];
} __attribute__((packed)) fs_head_t;

typedef struct {
  uint64_t props, offset, enc_size, dec_size;
} __attribute__((packed)) fs_entry_t;

typedef struct {
  uint64_t authid, type, app_version, fw_version;
  uint8_t  digest[0x20];
} __attribute__((packed)) fs_exinfo_t;

typedef struct {
  uint16_t type;
  uint8_t  unknown[0x0e];
  uint8_t  content_id[0x13];
  uint8_t  random_pad[0x0d];
} __attribute__((packed)) fs_npdrm_t;

typedef struct {
  uint8_t  unknown0[0x30];
  int32_t  unknown1;
  uint8_t  unknown2[0x1c];
  uint8_t  signature[0x100];
} __attribute__((packed)) fs_metafoot_t;

typedef struct {
  uint8_t  e_ident[16];
  uint16_t e_type, e_machine;
  uint32_t e_version;
  uint64_t e_entry, e_phoff, e_shoff;
  uint32_t e_flags;
  uint16_t e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx;
} __attribute__((packed)) fs_ehdr_t;

typedef struct {
  uint32_t p_type, p_flags;
  uint64_t p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_align;
} __attribute__((packed)) fs_phdr_t;

_Static_assert(sizeof(fs_head_t) == 0x20, "SELF header");
_Static_assert(sizeof(fs_entry_t) == 0x20, "SELF entry");
_Static_assert(sizeof(fs_exinfo_t) == 0x40, "extended info");
_Static_assert(sizeof(fs_npdrm_t) == 0x30, "NPDRM block");
_Static_assert(sizeof(fs_metafoot_t) == 0x150, "metadata footer");
_Static_assert(sizeof(fs_ehdr_t) == 64, "ELF header");
_Static_assert(sizeof(fs_phdr_t) == 56, "ELF program header");

/* The bits of an entry's properties, in the order the dumper's structure declares them (least significant bit first):
   is_ordered 0, is_encrypted 1, is_signed 2, is_compressed 3, unknown0 4-7, window_bits 8-10, has_blocks 11, block_bits 12-15,
   has_digest 16, has_extents 17, unknown1 18-19, segment_index 20-35, unknown2 36-63. */
#define PROP_SIGNED      (1ull << 2)
#define PROP_HAS_BLOCKS  (1ull << 11)
#define PROP_BLOCK_BITS(n) ((uint64_t)(n) << 12)
#define PROP_HAS_DIGEST  (1ull << 16)
#define PROP_SEGMENT(n)  ((uint64_t)(n) << 20)

static int
wr_all(int fd, const void *buf, size_t n) {
  size_t put = 0;
  while(put < n) {
    ssize_t r = write(fd, (const char *)buf + put, n - put);
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
rd_all(int fd, void *buf, size_t n, off_t off) {
  size_t got = 0;
  while(got < n) {
    ssize_t r = pread(fd, (char *)buf + got, n - got, off + (off_t)got);
    if(r < 0) {
      if(errno == EINTR) continue;
      return -1;
    }
    if(r == 0) {
      errno = EIO;
      return -1;
    }
    got += (size_t)r;
  }
  return 0;
}

static int
copy_range(int in, off_t from, int out, uint64_t size, char *buf) {
  for(uint64_t done = 0; done < size; ) {
    size_t n = size - done > FS_BUF ? FS_BUF : (size_t)(size - done);
    if(rd_all(in, buf, n, from + (off_t)done) != 0 || wr_all(out, buf, n) != 0) return -1;
    done += n;
  }
  return 0;
}

static int
fs_wanted(const fs_phdr_t *p) {
  return p->p_type == PT_LOAD_ || p->p_type == PT_SCE_RELRO || p->p_type == PT_SCE_DYNLIBDATA || p->p_type == PT_SCE_COMMENT;
}

static int
fail(char *why, size_t n, const char *msg) {
  if(why && n) snprintf(why, n, "%s", msg);
  return -1;
}

int
ps5tm_dump_elf2fself(const char *elf_path, const char *fself_path, char *why, size_t why_len) {
  int rc = -1;
  int elf_fd = -1, self_fd = -1;
  fs_phdr_t *ph = NULL;
  fs_entry_t *entries = NULL;
  uint64_t *seg_src = NULL;              /* the ELF offset of the data of each block entry */
  char *buf = NULL;

  elf_fd = open(elf_path, O_RDONLY);
  if(elf_fd < 0) return fail(why, why_len, "ELF-Datei ließ sich nicht öffnen");
  struct stat st;
  fs_ehdr_t eh;
  if(fstat(elf_fd, &st) != 0 || rd_all(elf_fd, &eh, sizeof(eh), 0) != 0) {
    fail(why, why_len, "ELF-Kopf ließ sich nicht lesen");
    goto out;
  }
  if(eh.e_ident[0] != 0x7f || eh.e_ident[1] != 'E' || eh.e_ident[2] != 'L' || eh.e_ident[3] != 'F') {
    fail(why, why_len, "Kein ELF");
    goto out;
  }
  if(eh.e_phentsize != sizeof(fs_phdr_t) || eh.e_phnum == 0 || eh.e_phnum > FS_PHNUM_MAX || eh.e_phoff != sizeof(fs_ehdr_t) ||
     eh.e_phoff + (uint64_t)eh.e_phnum * sizeof(fs_phdr_t) > (uint64_t)st.st_size) {
    fail(why, why_len, "Ungewöhnliche Programmköpfe");
    goto out;
  }
  size_t phsz = (size_t)eh.e_phnum * sizeof(fs_phdr_t);
  ph = malloc(phsz);
  buf = malloc(FS_BUF);
  if(!ph || !buf) {
    fail(why, why_len, "Kein Speicher");
    goto out;
  }
  if(rd_all(elf_fd, ph, phsz, (off_t)eh.e_phoff) != 0) {
    fail(why, why_len, "Programmköpfe ließen sich nicht lesen");
    goto out;
  }

  fs_phdr_t version_seg;
  memset(&version_seg, 0, sizeof(version_seg));
  unsigned num_entries = 0;
  for(unsigned i = 0; i < eh.e_phnum; i++) {
    if(ph[i].p_type == PT_SCE_VERSION) version_seg = ph[i];
    if(fs_wanted(&ph[i])) num_entries += 2;
  }
  for(unsigned i = 0; i < eh.e_phnum; i++) {                  /* every piece that will be copied has to lie in the ELF */
    const fs_phdr_t *p = &ph[i];
    if(!(fs_wanted(p) || p->p_type == PT_SCE_VERSION)) continue;
    uint64_t end = p->p_offset + p->p_filesz;
    if(end < p->p_offset || end > (uint64_t)st.st_size) {
      fail(why, why_len, "Ein Abschnitt liegt außerhalb der Datei");
      goto out;
    }
  }

  fs_head_t head;
  memset(&head, 0, sizeof(head));
  head.magic = SELF_PS5_MAGIC;
  head.version = 0;
  head.mode = 1;
  head.endian = 1;
  head.attrs = 0x12;
  head.key_type = 0x101;
  head.flags = 0x22;
  head.num_entries = (uint16_t)num_entries;

  uint64_t header_size = sizeof(fs_head_t) + (uint64_t)num_entries * sizeof(fs_entry_t);
  header_size = ROUND_UP(header_size, 0x10);
  header_size += eh.e_phoff + phsz;
  header_size = ROUND_UP(header_size, 0x10);
  header_size += sizeof(fs_exinfo_t);
  header_size += sizeof(fs_npdrm_t);
  uint64_t meta_size = (uint64_t)num_entries * 0x50 + sizeof(fs_metafoot_t) + 0x100;
  if(header_size > 0xFFFF || meta_size > 0xFFFF) {
    fail(why, why_len, "Zu viele Abschnitte für einen FSELF-Kopf");
    goto out;
  }
  head.header_size = (uint16_t)header_size;
  head.meta_size = (uint16_t)meta_size;

  entries = calloc(num_entries ? num_entries : 1, sizeof(*entries));
  seg_src = calloc(num_entries ? num_entries : 1, sizeof(*seg_src));
  if(!entries || !seg_src) {
    fail(why, why_len, "Kein Speicher");
    goto out;
  }
  uint64_t offset = header_size + meta_size;
  for(unsigned i = 0, j = 0; i < eh.e_phnum; i++) {
    const fs_phdr_t *p = &ph[i];
    if(!fs_wanted(p)) continue;
    /* digest entry */
    entries[j].props = PROP_SIGNED | PROP_HAS_DIGEST | PROP_SEGMENT(j + 1);
    entries[j].enc_size = (ROUND_UP(p->p_filesz, META_ENTRY_SIZE) / META_ENTRY_SIZE) * 32;
    entries[j].dec_size = entries[j].enc_size;
    entries[j].offset = offset;
    offset = ROUND_UP(offset + entries[j].enc_size, 0x10);
    j++;
    /* block entry */
    entries[j].props = PROP_SIGNED | PROP_HAS_BLOCKS | PROP_BLOCK_BITS(2) | PROP_SEGMENT(i);
    entries[j].enc_size = p->p_filesz;
    entries[j].dec_size = p->p_filesz;
    entries[j].offset = offset;
    seg_src[j] = p->p_offset;
    offset = ROUND_UP(offset + entries[j].enc_size, 0x10);
    j++;
  }
  uint64_t file_size = header_size + meta_size;
  for(unsigned i = 0; i < num_entries; i++) file_size += ROUND_UP(entries[i].enc_size, 0x10);
  head.file_size = ROUND_UP(file_size, 0x10);

  self_fd = open(fself_path, O_WRONLY | O_CREAT | O_TRUNC, 0755);
  if(self_fd < 0) {
    fail(why, why_len, "FSELF-Datei ließ sich nicht anlegen");
    goto out;
  }
  if(wr_all(self_fd, &head, sizeof(head)) != 0 || wr_all(self_fd, entries, (size_t)num_entries * sizeof(fs_entry_t)) != 0 ||
     wr_all(self_fd, &eh, sizeof(eh)) != 0 || wr_all(self_fd, ph, phsz) != 0) {
    fail(why, why_len, "Schreibfehler im FSELF-Kopf");
    goto out;
  }
  off_t pos = lseek(self_fd, 0, SEEK_CUR);                    /* align to 0x10 */
  if(pos < 0 || lseek(self_fd, (off_t)ROUND_UP((uint64_t)pos, 0x10), SEEK_SET) < 0) {
    fail(why, why_len, "Schreibfehler im FSELF-Kopf");
    goto out;
  }

  fs_exinfo_t exinfo;
  memset(&exinfo, 0, sizeof(exinfo));
  exinfo.authid = 0x3100000000000002ull;
  exinfo.type = 1;
  ps5tm_sha256_t sha;
  ps5tm_sha256_init(&sha);
  for(uint64_t done = 0; done < (uint64_t)st.st_size; ) {
    size_t n = (uint64_t)st.st_size - done > FS_BUF ? FS_BUF : (size_t)((uint64_t)st.st_size - done);
    if(rd_all(elf_fd, buf, n, (off_t)done) != 0) {
      fail(why, why_len, "ELF ließ sich nicht lesen");
      goto out;
    }
    ps5tm_sha256_update(&sha, buf, n);
    done += n;
  }
  ps5tm_sha256_final(&sha, exinfo.digest);

  fs_npdrm_t npdrm;
  memset(&npdrm, 0, sizeof(npdrm));
  npdrm.type = 3;
  fs_metafoot_t foot;
  memset(&foot, 0, sizeof(foot));
  foot.unknown1 = 0x10000;
  uint8_t blk[0x50];
  memset(blk, 0, sizeof(blk));
  uint8_t sig[0x100];
  memset(sig, 0, sizeof(sig));
  if(wr_all(self_fd, &exinfo, sizeof(exinfo)) != 0 || wr_all(self_fd, &npdrm, sizeof(npdrm)) != 0) {
    fail(why, why_len, "Schreibfehler im FSELF-Kopf");
    goto out;
  }
  for(unsigned i = 0; i < num_entries; i++) {
    if(wr_all(self_fd, blk, sizeof(blk)) != 0) {
      fail(why, why_len, "Schreibfehler bei den Metadaten");
      goto out;
    }
  }
  if(wr_all(self_fd, &foot, sizeof(foot)) != 0 || wr_all(self_fd, sig, sizeof(sig)) != 0) {
    fail(why, why_len, "Schreibfehler bei den Metadaten");
    goto out;
  }

  for(unsigned i = 0; i < num_entries; i++) {                 /* the data of the block entries */
    if(!(entries[i].props & PROP_HAS_BLOCKS)) continue;
    if(lseek(self_fd, (off_t)entries[i].offset, SEEK_SET) < 0 || copy_range(elf_fd, (off_t)seg_src[i], self_fd, entries[i].enc_size, buf) != 0) {
      fail(why, why_len, "Schreibfehler bei den Abschnitten");
      goto out;
    }
  }
  if(version_seg.p_filesz > 0 && copy_range(elf_fd, (off_t)version_seg.p_offset, self_fd, version_seg.p_filesz, buf) != 0) {
    fail(why, why_len, "Schreibfehler beim Versionsabschnitt");
    goto out;
  }
  if(fsync(self_fd) != 0) {
    fail(why, why_len, "Schreibfehler beim Sichern");
    goto out;
  }
  rc = 0;

out:
  if(self_fd >= 0) close(self_fd);
  if(elf_fd >= 0) close(elf_fd);
  if(rc != 0 && self_fd >= 0) unlink(fself_path);
  free(ph);
  free(entries);
  free(seg_src);
  free(buf);
  return rc;
}
