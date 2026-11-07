/* SPDX-License-Identifier: GPL-3.0-or-later
 * Host test of src/fsign.c (unsigned PS5 executables written as fake-signed SELFs). Run by test/fsign/run.sh.
 *
 *   test <workdir>
 *
 * It writes the ELF files and the SELF files the script make_fself.py (ampr-emu-ng) is then run on and compared with (run.sh), and
 * checks the rest itself: which files are found, the AMPR index, the folder laid over a game, links, mistakes and cancelling. */

#include "fsign.h"
#include "testlib.h"

static void prog(void *c, uint64_t b) { *(uint64_t *)c += b; }

int
main(int argc, char **argv) {
  if(argc < 2) { fprintf(stderr, "usage: test <workdir>\n"); return 2; }
  const char *W = argv[1];
  char p[1024], q[1024];
  rm_rf(W);
  mk(W);

  /* --- names and heads */
  CHECK(ps5tm_fsign_name_ok("eboot.bin") && ps5tm_fsign_name_ok("EBOOT.BIN") && ps5tm_fsign_name_ok("a/b/eboot_x.bin"));
  CHECK(!ps5tm_fsign_name_ok("data/blob.bin") && !ps5tm_fsign_name_ok("level.bin") && !ps5tm_fsign_name_ok("noext") && !ps5tm_fsign_name_ok("a.dat"));
  CHECK(ps5tm_fsign_name_ok("x.elf") && ps5tm_fsign_name_ok("x.SELF") && ps5tm_fsign_name_ok("sce_module/libc.prx") && ps5tm_fsign_name_ok("fakelib/a.SPRX"));
  uint8_t *exec = make_elf(0xFE10), *dyn = make_elf(0xFE18), *oth = make_elf(0xFE00);
  CHECK(ps5tm_fsign_head_unsigned(exec, 0x40) && ps5tm_fsign_head_unsigned(dyn, 0x12));
  CHECK(!ps5tm_fsign_head_unsigned(oth, 0x40) && !ps5tm_fsign_head_unsigned(exec, 0x11));

  /* --- one file, for the comparison with make_fself.py (run.sh) */
  snprintf(p, sizeof(p), "%s/elf_exec.bin", W); put_file(p, exec, ELF_LEN);
  snprintf(p, sizeof(p), "%s/elf_dyn.bin", W); put_file(p, dyn, ELF_LEN);
  char p2[1024];
  snprintf(p, sizeof(p), "%s/elf_exec.bin", W);
  snprintf(q, sizeof(q), "%s/self_exec.bin", W);
  uint64_t sz = 0;
  char err[400] = "";
  CHECK(ps5tm_fsign_write(p, q, NULL, NULL, NULL, &sz, err, sizeof(err)) == 0);
  CHECK(sz == 18917);                                         /* the length make_fself.py gives for it (ps5-dump-forge's test) */
  CHECK(fsize(q) == 18917);
  snprintf(p, sizeof(p), "%s/elf_dyn.bin", W);
  snprintf(q, sizeof(q), "%s/self_dyn.bin", W);
  CHECK(ps5tm_fsign_write(p, q, NULL, NULL, NULL, &sz, err, sizeof(err)) == 0 && sz == 18917);
  {
    size_t n;
    uint8_t *s = get_file(q, &n);
    CHECK(n == 18917 && s[0] == 0x4F && s[1] == 0x15 && s[2] == 0x3D && s[3] == 0x1D && s[7] == 0x12);
    CHECK(!ps5tm_fsign_head_unsigned(s, n));                  /* a signed file is no candidate any more */
    free(s);
  }
  /* an existing target is never overwritten */
  CHECK(ps5tm_fsign_write(p, q, NULL, NULL, NULL, NULL, err, sizeof(err)) == -1);
  CHECK(fsize(q) == 18917);

  /* --- a game folder */
  char G[1024];
  snprintf(G, sizeof(G), "%s/game", W);
  mk(G);
  snprintf(p, sizeof(p), "%s/sce_module", G); mk(p);
  snprintf(p, sizeof(p), "%s/fakelib", G); mk(p);
  snprintf(p, sizeof(p), "%s/data/deep/er", G); mk(p);
  snprintf(p, sizeof(p), "%s/eboot.bin", G); put_file(p, exec, ELF_LEN);
  snprintf(p, sizeof(p), "%s/fakelib/libSceFoo.sprx", G); put_file(p, dyn, ELF_LEN);
  snprintf(p, sizeof(p), "%s/data/deep/er/Mod.PRX", G); put_file(p, dyn, ELF_LEN);
  { size_t n; snprintf(p, sizeof(p), "%s/self_dyn.bin", W); uint8_t *s = get_file(p, &n);
    snprintf(p, sizeof(p), "%s/sce_module/libfoo.prx", G); put_file(p, s, n); free(s); }      /* already signed */
  snprintf(p, sizeof(p), "%s/data/blob.bin", G); put_file(p, dyn, ELF_LEN);                     /* a data .bin */
  snprintf(p, sizeof(p), "%s/data/elf.dat", G); put_file(p, dyn, ELF_LEN);                      /* a data file */
  snprintf(p, sizeof(p), "%s/data/level.bin", G); put_file(p, exec, ELF_LEN);
  snprintf(p, sizeof(p), "%s/data/tiny.elf", G); put_file(p, exec, 0x30);                       /* too small */
  snprintf(p, sizeof(p), "%s/data/other.elf", G); put_file(p, oth, ELF_LEN);                    /* another e_type */
  { uint8_t *bad = make_elf(0xFE10); p16(bad, 0x36, 0x20);                                      /* a program header size the script refuses */
    snprintf(p, sizeof(p), "%s/data/bad.self", G); put_file(p, bad, ELF_LEN); free(bad); }
  { uint8_t *cut = make_elf(0xFE10);                                                            /* a segment runs past the end of the file */
    snprintf(p, sizeof(p), "%s/data/cut.prx", G); put_file(p, cut, 0x4000); free(cut); }
  snprintf(p, sizeof(p), "%s/sce_sys", G); mk(p);
  snprintf(p2, sizeof(p2), "%s/link.sprx", G); CHECK(symlink("eboot.bin", p2) == 0);                         /* links are never followed */
  snprintf(p2, sizeof(p2), "%s/dirlink", G); CHECK(symlink("fakelib", p2) == 0);

  fsign_list_t l;
  CHECK(ps5tm_fsign_scan(G, NULL, &l) == 0);
  CHECK(l.n == 3);
  if(l.n == 3) {
    CHECK(!strcmp(l.it[0].rel, "data/deep/er/Mod.PRX") && !strcmp(l.it[1].rel, "eboot.bin") && !strcmp(l.it[2].rel, "fakelib/libSceFoo.sprx"));
    CHECK(l.it[1].elf_size == ELF_LEN && l.it[1].self_size == 18917);
    CHECK(l.elf_bytes == 3ull * ELF_LEN && l.self_bytes == 3ull * 18917);
  }
  CHECK(l.refused == 2 && l.refused_rel[0] && l.refused_why[0]);
  CHECK(ps5tm_fsign_find(&l, "eboot.bin") && ps5tm_fsign_find(&l, "fakelib/libSceFoo.sprx") && !ps5tm_fsign_find(&l, "data/blob.bin"));
  CHECK(!ps5tm_fsign_find(&l, "EBOOT.BIN"));
  { int stop = 1; fsign_list_t c; CHECK(ps5tm_fsign_scan(G, &stop, &c) == 1 && c.n == 0 && !c.it); }

  /* --- the AMPR index */
  {
    const char *paths[] = { "/app0/EBOOT.BIN", "/app0/data/blob.bin", "/app0/Fakelib/libSceFoo.sprx", "/app0/sce_module/libfoo.prx", "/app0/data/deep/er/mod.prx" };
    const uint64_t sizes[] = { ELF_LEN, ELF_LEN, ELF_LEN, 18917, 7 };           /* the last one is not the ELF's size: left alone */
    snprintf(p, sizeof(p), "%s/ampr_emu.index", G);
    make_index(p, paths, sizes, 5);
    snprintf(q, sizeof(q), "%s/index_fixed", W);
    unsigned rs = 0;
    CHECK(ps5tm_fsign_fix_index(p, q, &l, &rs, err, sizeof(err)) == 0 && rs == 2);
    size_t n;
    uint8_t *ix = get_file(q, &n);
    CHECK(ix && n > 48);
    if(ix && n > 48) {
      CHECK(g64(ix, 48 + 8) == 18917);                       /* eboot.bin */
      CHECK(g64(ix, 48 + 24 + 8) == ELF_LEN);                /* a data file: not touched */
      CHECK(g64(ix, 48 + 48 + 8) == 18917);                  /* fakelib/libSceFoo.sprx, case folded */
      CHECK(g64(ix, 48 + 72 + 8) == 18917);                  /* the one that was signed already: untouched, was 18917 */
      CHECK(g64(ix, 48 + 96 + 8) == 7);
    }
    free(ix);
    size_t n0;
    uint8_t *ix0 = get_file(p, &n0);
    CHECK(ix0 && g64(ix0, 48 + 8) == ELF_LEN);               /* the original index is not changed */
    free(ix0);
    snprintf(q, sizeof(q), "%s/index_none", W);
    CHECK(ps5tm_fsign_fix_index(p, q, &l, &rs, err, sizeof(err)) == 0 && rs == 2);
    snprintf(p, sizeof(p), "%s/nope.index", W);
    CHECK(ps5tm_fsign_fix_index(p, q, &l, &rs, err, sizeof(err)) == 0 && rs == 0);       /* no index: nothing to do */
    snprintf(p, sizeof(p), "%s/elf_exec.bin", W);
    CHECK(ps5tm_fsign_fix_index(p, q, &l, &rs, err, sizeof(err)) == 0 && rs == 0);       /* not an index: left alone */
  }

  /* --- a folder to lay over the game */
  {
    char S[1024];
    snprintf(S, sizeof(S), "%s/stage" FSIGN_STAGE_SUFFIX, W);
    unsigned rs = 99;
    uint64_t done = 0;
    CHECK(ps5tm_fsign_stage(G, S, &l, NULL, prog, &done, &rs, err, sizeof(err)) == 0 && rs == 2);
    CHECK(done == 2 * 3 * (0x4321ull + 0x123 + 0x25 + 0));    /* the segments copied, written and read back (PT_LOAD, RELRO, VERSION) */
    snprintf(p, sizeof(p), "%s/eboot.bin", S); CHECK(fsize(p) == 18917);
    snprintf(p, sizeof(p), "%s/fakelib/libSceFoo.sprx", S); CHECK(fsize(p) == 18917);
    snprintf(p, sizeof(p), "%s/data/deep/er/Mod.PRX", S); CHECK(fsize(p) == 18917);
    snprintf(p, sizeof(p), "%s/ampr_emu.index", S); CHECK(exists(p));
    struct stat st;
    snprintf(p, sizeof(p), "%s/eboot.bin", S);
    CHECK(stat(p, &st) == 0 && (st.st_mode & 0777) == 0777);
    ps5tm_fsign_rmtree(S);
    CHECK(!exists(S));
    /* only a folder with our ending is ever removed */
    snprintf(p, sizeof(p), "%s/keep", W); mk(p);
    snprintf(q, sizeof(q), "%s/keep/x", W); put_file(q, "x", 1);
    ps5tm_fsign_rmtree(p);
    CHECK(exists(q));
    /* a stage that cancels is cut off, and nothing of the game folder changed */
    int stop = 1;
    snprintf(S, sizeof(S), "%s/stage2" FSIGN_STAGE_SUFFIX, W);
    CHECK(ps5tm_fsign_stage(G, S, &l, &stop, NULL, NULL, &rs, err, sizeof(err)) == 1);
    snprintf(p, sizeof(p), "%s/eboot.bin", S); CHECK(!exists(p));
    ps5tm_fsign_rmtree(S);
    snprintf(p, sizeof(p), "%s/eboot.bin", G); CHECK(fsize(p) == ELF_LEN);
  }
  ps5tm_fsign_free(&l);

  /* --- mistakes in the ELF: no crash, either signed and read back, or refused, never half a file */
  {
    unsigned signed_n = 0, refused_n = 0;
    uint64_t seed = 12345;
    for(int round = 0; round < 3000; round++) {
      uint8_t *e = make_elf(round & 1 ? 0xFE18 : 0xFE10);
      int hits = 1 + (int)(seed % 4);
      for(int h = 0; h < hits; h++) {
        seed = seed * 6364136223846793005ull + 1442695040888963407ull;
        size_t at = (size_t)((seed >> 33) % (0x40 + 5 * 0x38 + 8));      /* the ELF header and the program headers */
        seed = seed * 6364136223846793005ull + 1442695040888963407ull;
        e[at] = (uint8_t)(seed >> 40);
        if(!(round % 7)) { seed = seed * 6364136223846793005ull + 1442695040888963407ull; p64(e, 0x40 + (size_t)((seed >> 40) % 5) * 0x38 + 0x20, seed); }
      }
      snprintf(p, sizeof(p), "%s/fuzz.elf", W);
      snprintf(q, sizeof(q), "%s/fuzz.self", W);
      put_file(p, e, ELF_LEN);
      unlink(q);
      int r = ps5tm_fsign_write(p, q, NULL, NULL, NULL, &sz, err, sizeof(err));
      if(r == 0) { signed_n++; CHECK(fsize(q) == sz); }
      else { refused_n++; CHECK(r == -1 && !exists(q)); }
      free(e);
    }
    CHECK(signed_n > 100 && refused_n > 100);
    fprintf(stderr, "damaged ELF files: %u signed, %u refused, no crash\n", signed_n, refused_n);
    unlink(q);
  }

  /* --- cancelling leaves no file */
  {
    int stop = 1;
    snprintf(p, sizeof(p), "%s/elf_exec.bin", W);
    snprintf(q, sizeof(q), "%s/cancelled.self", W);
    CHECK(ps5tm_fsign_write(p, q, &stop, NULL, NULL, NULL, err, sizeof(err)) == 1 && !exists(q));
    CHECK(ps5tm_fsign_write(W, q, NULL, NULL, NULL, NULL, err, sizeof(err)) == -1 && !exists(q));            /* a folder is no ELF */
    snprintf(p, sizeof(p), "%s/missing", W);
    CHECK(ps5tm_fsign_write(p, q, NULL, NULL, NULL, NULL, err, sizeof(err)) == -1 && !exists(q));
  }

  free(exec); free(dyn); free(oth);
  fprintf(stderr, "%d checks, %d failed\n", g_checks, g_fail);
  return g_fail ? 1 : 0;
}
