/* Host test of src/savexport.c: a save of the console read out into a ZIP (the console's mount is a stub that copies a prepared folder). */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "chives.h"
#include "savemnt.h"
#include "savesfo.h"
#include "savexport.h"
#include "savezip.h"

extern const char *ps5tm_savemnt_test_tree, *ps5tm_savemnt_test_param;
extern int ps5tm_savemnt_test_fail, ps5tm_savemnt_test_opened, ps5tm_savemnt_test_closed, ps5tm_savemnt_test_param_rc;

static int g_fail, g_n;
#define CHECK(c) do { g_n++; if(!(c)) { g_fail++; printf("FAIL line %d: %s\n", __LINE__, #c); } } while(0)

static char ROOT[400];

static void
put(uint8_t *p, uint64_t v, int n) {
  for(int i = 0; i < n; i++) p[i] = (uint8_t)(v >> (8 * i));
}

static void
mkdirs(const char *path) {
  char t[800];
  snprintf(t, sizeof(t), "%s", path);
  for(char *p = t + 1; *p; p++) if(*p == '/') { *p = 0; mkdir(t, 0755); *p = '/'; }
  mkdir(t, 0755);
}

static void
rmtree(const char *path) {
  char cmd[900];
  snprintf(cmd, sizeof(cmd), "rm -rf '%s'", path);
  if(system(cmd)) {}
}

static void
wfile(const char *path, const void *data, size_t len) {
  char dir[800];
  snprintf(dir, sizeof(dir), "%s", path);
  char *s = strrchr(dir, '/');
  if(s) { *s = 0; mkdirs(dir); }
  FILE *f = fopen(path, "wb");
  if(f) { fwrite(data, 1, len, f); fclose(f); }
}

static int
exists(const char *path) {
  struct stat st;
  return stat(path, &st) == 0;
}

static unsigned
count_dir(const char *path) {
  DIR *d = opendir(path);
  if(!d) return 0;
  unsigned n = 0;
  struct dirent *e;
  while((e = readdir(d)) != NULL) if(strcmp(e->d_name, ".") && strcmp(e->d_name, "..")) n++;
  closedir(d);
  return n;
}

/* a PSF as a console writes it (the layout of the savesfo test) */
static size_t
sfo_build(uint8_t *out, const char *title, const char *dir, const uint8_t aid[8]) {
  struct { const char *key; unsigned fmt; const void *val; unsigned used, cap; } e[] = {
    { "ACCOUNT_ID", 0x0004, aid, 8, 8 },
    { "MAINTITLE", 0x0204, "Testspiel", 10, 128 },
    { "SAVEDATA_DIRECTORY", 0x0204, dir, (unsigned)strlen(dir) + 1, 32 },
    { "TITLE_ID", 0x0204, title, (unsigned)strlen(title) + 1, 16 },
  };
  const unsigned n = 4;
  size_t keys_len = 0, data_len = 0;
  for(unsigned i = 0; i < n; i++) { keys_len += strlen(e[i].key) + 1; data_len += e[i].cap; }
  const size_t key_off = 20 + (size_t)n * 16, data_off = key_off + keys_len;
  memset(out, 0, data_off + data_len);
  memcpy(out, "\0PSF", 4);
  put(out + 4, 0x101, 4);
  put(out + 8, key_off, 4);
  put(out + 12, data_off, 4);
  put(out + 16, n, 4);
  size_t ko = 0, vo = 0;
  for(unsigned i = 0; i < n; i++) {
    uint8_t *ix = out + 20 + (size_t)i * 16;
    put(ix, ko, 2);
    put(ix + 2, e[i].fmt, 2);
    put(ix + 4, e[i].used, 4);
    put(ix + 8, e[i].cap, 4);
    put(ix + 12, vo, 4);
    memcpy(out + key_off + ko, e[i].key, strlen(e[i].key) + 1);
    memcpy(out + data_off + vo, e[i].val, e[i].used);
    ko += strlen(e[i].key) + 1;
    vo += e[i].cap;
  }
  return data_off + data_len;
}

/* a PS5 image: the header the engine reads, then bytes */
static void
make_image(const char *path, unsigned version, unsigned bs, uint64_t blocks, size_t size) {
  uint8_t *b = calloc(1, size < 0x400 ? 0x400 : size);               /* a short image is the header cut off */
  put(b, version, 8);
  put(b + 8, 20130315, 8);
  put(b + 0x1c, 0x0b, 2);
  put(b + 0x20, bs, 4);
  put(b + 0x24, 2, 4);
  put(b + 0x38, blocks, 8);
  for(size_t i = 0x400; i < size; i++) b[i] = (uint8_t)(i * 31 + 7);
  wfile(path, b, size);
  free(b);
}

typedef struct { unsigned phases, works, stop_after, polls; uint64_t bytes; char last[200]; char phase_names[8][48]; } probe_t;

static void p_phase(void *c, const char *t, uint64_t total) { probe_t *p = c; (void)total; if(p->phases < 8) snprintf(p->phase_names[p->phases], 48, "%s", t); p->phases++; }
static void p_work(void *c, uint64_t n) { probe_t *p = c; p->works++; p->bytes += n; }
static void p_cur(void *c, const char *r) { probe_t *p = c; snprintf(p->last, sizeof(p->last), "%s", r); }
static int  p_stop(void *c) { probe_t *p = c; p->polls++; return p->stop_after && p->polls >= p->stop_after; }

static const savexport_hooks_t *
hooks(probe_t *p) {
  static savexport_hooks_t h;
  h.ctx = p; h.phase = p_phase; h.work = p_work; h.current = p_cur; h.stop = p_stop;
  return &h;
}

static char IMG[500], WORK[500], OUT[500], TREE[500];

static void
reset_dirs(void) {
  rmtree(WORK);
  rmtree(OUT);
  mkdirs(WORK);
}

static void
prepare_tree(int with_real_sfo_in_fs) {
  rmtree(TREE);
  char p[700];
  uint8_t zeros[3072] = { 0 };
  uint8_t keystone[96];
  for(unsigned i = 0; i < sizeof(keystone); i++) keystone[i] = (uint8_t)(i + 1);
  uint8_t big[100000];
  for(unsigned i = 0; i < sizeof(big); i++) big[i] = (uint8_t)(i * 13 + 5);
  snprintf(p, sizeof(p), "%s/sce_sys/keystone", TREE); wfile(p, keystone, sizeof(keystone));
  snprintf(p, sizeof(p), "%s/data.bin", TREE); wfile(p, big, sizeof(big));
  snprintf(p, sizeof(p), "%s/sub/dir/a.txt", TREE); wfile(p, "hello save\n", 11);
  snprintf(p, sizeof(p), "%s/empty.dat", TREE); wfile(p, "", 0);
  snprintf(p, sizeof(p), "%s/emptydir", TREE); mkdirs(p);
  snprintf(p, sizeof(p), "%s/sce_sys/param.sfo", TREE);
  if(with_real_sfo_in_fs) {
    uint8_t sfo[2048];
    const uint8_t aid[8] = { 9, 9, 9, 9, 9, 9, 9, 9 };
    size_t n = sfo_build(sfo, "PPSA09999", "FSSAVE", aid);
    wfile(p, sfo, n);
  } else {
    wfile(p, zeros, sizeof(zeros));
  }
}

static unsigned
find_entry(szr_t *z, const char *name) {
  for(unsigned i = 0; i < szr_count(z); i++) if(!strcmp(szr_at(z, i)->name, name)) return i;
  return (unsigned)-1;
}

int
main(int argc, char **argv) {
  snprintf(ROOT, sizeof(ROOT), "%s", argc > 1 ? argv[1] : "/tmp/ps5tm-savexport");
  rmtree(ROOT);
  mkdirs(ROOT);
  snprintf(IMG, sizeof(IMG), "%s/home/1ea2f4d9/savedata_prospero/PPSA01234/sdimg_SAVE0", ROOT);
  snprintf(WORK, sizeof(WORK), "%s/data/work", ROOT);
  snprintf(OUT, sizeof(OUT), "%s/data/out", ROOT);
  snprintf(TREE, sizeof(TREE), "%s/fake_mount_source", ROOT);
  ps5tm_savemnt_test_tree = TREE;

  make_image(IMG, 2, 32768, 12, 400000);
  static char pparam[600];
  snprintf(pparam, sizeof(pparam), "%s/blob.param", ROOT);
  ps5tm_savemnt_test_param = pparam;
  uint8_t sfo[2048];
  const uint8_t aid[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
  size_t sfo_len = sfo_build(sfo, "PPSA01234", "SAVE0", aid);
  wfile(pparam, sfo, sfo_len);

  savexport_args_t a = { IMG, "PPSA01234", "SAVE0", WORK, OUT, "PPSA01234_SAVE0_test.zip" };
  savexport_result_t res;
  char err[600];
  probe_t pr;

  /* --- a good run (with the image copy of a run that was cut off lying about: another slot, swept) */
  reset_dirs();
  prepare_tree(0);
  {
    char stale[700];
    snprintf(stale, sizeof(stale), "%s/sdimg_OTHERSLOT", WORK);
    wfile(stale, "stale", 5);
    snprintf(stale, sizeof(stale), "%s/keepme.txt", WORK);                       /* a file that is not an image copy stays */
    wfile(stale, "k", 1);
  }
  memset(&pr, 0, sizeof(pr));
  int rc = savexport_run(&a, hooks(&pr), &res, err, sizeof(err));
  if(rc) printf("  err: %s\n", err);
  CHECK(rc == 0);
  CHECK(ps5tm_savemnt_test_opened == 1 && ps5tm_savemnt_test_closed == 1);
  CHECK(res.files == 5 && res.sfo_from_header == 1 && !strcmp(res.title_id, "PPSA01234") && !strcmp(res.save_dir, "SAVE0"));
  char zp[700];
  snprintf(zp, sizeof(zp), "%s/%s", OUT, a.zip_name);
  CHECK(!strcmp(res.zip, zp) && exists(zp));
  CHECK(count_dir(OUT) == 1);                                    /* nothing but the ZIP: no .partial */
  {
    char k[700], o[700];
    snprintf(k, sizeof(k), "%s/keepme.txt", WORK);
    snprintf(o, sizeof(o), "%s/sdimg_OTHERSLOT", WORK);
    CHECK(count_dir(WORK) == 1 && exists(k) && !exists(o));      /* the copy, the mount point, the stage, the stale image: gone; the file that is none of those stays */
    unlink(k);
  }
  CHECK(pr.phases >= 5 && !strcmp(pr.phase_names[0], "Abbild kopieren") && !strcmp(pr.phase_names[3], "Spielstand aushängen"));
  CHECK(pr.bytes >= 400000 + 100000 + 3072);                     /* progress: the image, then the files, then the ZIP */
  int zerr = 0;
  szr_t *z = szr_open(zp, &zerr);
  CHECK(z != NULL);
  if(z) {
    CHECK(szr_count(z) == 6);                                    /* 5 files (param.sfo among them) + chives.json: the folders are not entries */
    unsigned i;
    uint8_t *buf; size_t len;
    CHECK((i = find_entry(z, "sce_sys/param.sfo")) != (unsigned)-1 && szr_read(z, i, &buf, &len, 1 << 20) == SZ_OK);
    if(i != (unsigned)-1) {
      /* the real one, not the zeros of the file system - with one change: the account of the user is not given away (ACCOUNT_ID zero), everything else is as it was */
      uint8_t want[4096], raw[8];
      CHECK(sfo_len <= sizeof(want));
      memcpy(want, sfo, sfo_len);
      CHECK(savesfo_patch_owner(want, sfo_len, 0, 0, 0) == 0);
      CHECK(len == sfo_len && !memcmp(buf, want, len));
      CHECK(savesfo_get_raw(buf, len, "ACCOUNT_ID", raw, 8) == 8 && !memcmp(raw, (uint8_t[]){0, 0, 0, 0, 0, 0, 0, 0}, 8));
      free(buf);
    }
    CHECK((i = find_entry(z, "data.bin")) != (unsigned)-1 && szr_at(z, i)->usize == 100000);
    CHECK((i = find_entry(z, "sub/dir/a.txt")) != (unsigned)-1 && szr_read(z, i, &buf, &len, 100) == SZ_OK && len == 11 && !memcmp(buf, "hello save\n", 11));
    free(buf);
    CHECK(find_entry(z, "empty.dat") != (unsigned)-1 && find_entry(z, "sce_sys/keystone") != (unsigned)-1);
    CHECK(find_entry(z, "emptydir") == (unsigned)-1 && find_entry(z, "save.img") == (unsigned)-1);
    i = find_entry(z, "chives.json");
    CHECK(i != (unsigned)-1 && szr_read(z, i, &buf, &len, CHIVES_MAX_JSON) == SZ_OK);
    if(i != (unsigned)-1) {
      chives_t g;
      CHECK(chives_parse((const char *)buf, len, &g) == 0);
      CHECK(g.block_size == 32768 && g.block_count == 12 && g.image_size == 400000 && g.backup_header_count == 2 && g.mode_raw == 0x0b);
      CHECK(!strcmp(g.title_id, "PPSA01234") && !strcmp(g.save_dir, "SAVE0") && !strcmp(g.image_name, "sdimg_SAVE0"));
      CHECK(g.nown == 9);                                        /* the 5 files and the 4 folders */
      unsigned seen_dir = 0;
      for(unsigned k = 0; k < g.nown; k++) if(!strcmp(g.own[k].path, "emptydir") || !strcmp(g.own[k].path, "sub") || !strcmp(g.own[k].path, "sub/dir")) seen_dir++;
      CHECK(seen_dir == 3 && g.nown >= 4);
      chives_free(&g);
      free(buf);
    }
    szr_close(z);
  }

  /* the same name again: the ZIP is replaced? (the engine never overwrites a finished ZIP's name unseen: the caller picks fresh names) */
  reset_dirs();
  prepare_tree(0);

  /* --- a run whose files hold a real param.sfo (no blob in the header) */
  unlink(pparam);
  prepare_tree(1);
  memset(&pr, 0, sizeof(pr));
  a.zip_name = "second.zip";
  rc = savexport_run(&a, hooks(&pr), &res, err, sizeof(err));
  CHECK(rc == 0 && res.sfo_from_header == 0 && !strcmp(res.title_id, "PPSA09999") && !strcmp(res.save_dir, "FSSAVE"));
  CHECK(count_dir(WORK) == 0);

  /* --- the console does not understand the question for the blob (ENOTTY on every node): the file system's own param.sfo is used, and the answer is told */
  reset_dirs();
  prepare_tree(1);
  memset(&pr, 0, sizeof(pr));
  ps5tm_savemnt_test_param_rc = -2;
  a.zip_name = "enotty.zip";
  rc = savexport_run(&a, hooks(&pr), &res, err, sizeof(err));
  CHECK(rc == 0 && res.sfo_from_header == 0 && !strcmp(res.title_id, "PPSA09999") && strstr(res.note, "aus dem Spielstand selbst") && strstr(res.note, "pfsctldev:9116=25"));
  CHECK(count_dir(WORK) == 0);
  /* ... and when the file system holds only zeros as well: refused, with the console's answer in the message */
  reset_dirs();
  prepare_tree(0);
  a.zip_name = "enotty2.zip";
  rc = savexport_run(&a, hooks(&pr), &res, err, sizeof(err));
  CHECK(rc != 0 && rc != ECANCELED && strstr(err, "Metadaten") && strstr(err, "Antwort der Konsole") && strstr(err, "pfsctldev:9116=25"));
  CHECK(count_dir(OUT) == 0 && count_dir(WORK) == 0);
  ps5tm_savemnt_test_param_rc = 0;

  /* --- no blob and only zeros in the file system: the save cannot be put back later, so it is not exported */
  reset_dirs();
  prepare_tree(0);
  memset(&pr, 0, sizeof(pr));
  a.zip_name = "third.zip";
  rc = savexport_run(&a, hooks(&pr), &res, err, sizeof(err));
  CHECK(rc != 0 && rc != ECANCELED && strstr(err, "Metadaten"));
  CHECK(count_dir(OUT) == 0 && count_dir(WORK) == 0);
  wfile(pparam, sfo, sfo_len);

  /* --- a stop: in the middle of the first copy, in the middle of reading the files, in the middle of the ZIP */
  for(unsigned stop = 1; stop <= 12; stop += 2) {
    reset_dirs();
    prepare_tree(0);
    memset(&pr, 0, sizeof(pr));
    pr.stop_after = stop;
    a.zip_name = "stopped.zip";
    ps5tm_savemnt_test_opened = ps5tm_savemnt_test_closed = 0;
    rc = savexport_run(&a, hooks(&pr), &res, err, sizeof(err));
    CHECK(rc == ECANCELED || rc == 0);
    if(rc == ECANCELED) {
      CHECK(count_dir(OUT) == 0 && count_dir(WORK) == 0);
      CHECK(ps5tm_savemnt_test_opened == ps5tm_savemnt_test_closed);                                     /* never left mounted */
    }
  }

  /* --- the console refuses to mount */
  reset_dirs();
  prepare_tree(0);
  memset(&pr, 0, sizeof(pr));
  ps5tm_savemnt_test_fail = 1;
  a.zip_name = "nomount.zip";
  rc = savexport_run(&a, hooks(&pr), &res, err, sizeof(err));
  CHECK(rc != 0 && rc != ECANCELED && strstr(err, "nicht eingehängt"));
  CHECK(count_dir(OUT) == 0 && count_dir(WORK) == 0);

  /* --- the console will not let go: the copy of the image stays (it is still mounted), the next run tells the console to let go first */
  reset_dirs();
  prepare_tree(0);
  memset(&pr, 0, sizeof(pr));
  ps5tm_savemnt_test_fail = 2;
  a.zip_name = "stuck.zip";
  rc = savexport_run(&a, hooks(&pr), &res, err, sizeof(err));
  CHECK(rc != 0 && rc != ECANCELED && strstr(err, "neu starten"));
  CHECK(count_dir(OUT) == 0);
  char img_copy[700], mnt[700];
  snprintf(img_copy, sizeof(img_copy), "%s/sdimg_SAVE0", WORK);
  snprintf(mnt, sizeof(mnt), "%s/mnt", WORK);
  CHECK(exists(img_copy) && exists(mnt));
  memset(&pr, 0, sizeof(pr));
  a.zip_name = "afterstuck.zip";
  rc = savexport_run(&a, hooks(&pr), &res, err, sizeof(err));
  if(rc) printf("  err: %s\n", err);
  CHECK(rc == 0 && count_dir(WORK) == 0);                                                                 /* the leftover was let go and removed */
  /* ... and when even that is refused */
  reset_dirs();
  prepare_tree(0);
  ps5tm_savemnt_test_fail = 2;
  a.zip_name = "stuck2.zip";
  rc = savexport_run(&a, hooks(&pr), &res, err, sizeof(err));
  CHECK(rc != 0);
  ps5tm_savemnt_test_fail = 4;
  a.zip_name = "stuck3.zip";
  rc = savexport_run(&a, hooks(&pr), &res, err, sizeof(err));
  CHECK(rc != 0 && strstr(err, "früheren Versuch") && exists(img_copy));
  rc = savexport_run(&a, hooks(&pr), &res, err, sizeof(err));
  CHECK(rc == 0);

  /* --- not a save this app can carry */
  reset_dirs();
  prepare_tree(0);
  make_image(IMG, 1, 32768, 12, 400000);                                                                  /* a PS4 save without its key file */
  a.zip_name = "ps4.zip";
  rc = savexport_run(&a, hooks(&pr), &res, err, sizeof(err));
  CHECK(rc != 0 && strstr(err, "Schlüsseldatei") && count_dir(OUT) == 0 && count_dir(WORK) == 0);
  {
    /* ... with it: the files travel alone, without a sidecar (the other tool writes none for a PS4 save) */
    char binp[600];
    snprintf(binp, sizeof(binp), "%s/home/1ea2f4d9/savedata_prospero/PPSA01234/SAVE0.bin", ROOT);
    uint8_t key[96];
    for(int i = 0; i < 96; i++) key[i] = (uint8_t)(i + 1);
    wfile(binp, key, sizeof(key));
    reset_dirs();
    prepare_tree(1);                                                                                      /* a param.sfo in the file system: PS4 saves keep it there */
    unlink(pparam);                                                                                       /* ... and no blob in the header, as no PS4 save has */
    memset(&pr, 0, sizeof(pr));
    a.zip_name = "ps4ok.zip";
    ps5tm_savemnt_test_param_rc = -2;                                                                     /* the blob question must not even be asked: it would fail */
    rc = savexport_run(&a, hooks(&pr), &res, err, sizeof(err));
    ps5tm_savemnt_test_param_rc = 0;
    if(rc) printf("  err: %s\n", err);
    CHECK(rc == 0 && res.files == 5 && !res.note[0] && !strcmp(res.title_id, "PPSA09999"));
    char zp2[700];
    snprintf(zp2, sizeof(zp2), "%s/ps4ok.zip", OUT);
    szr_t *z2 = szr_open(zp2, &zerr);
    CHECK(z2 != NULL);
    if(z2) {
      CHECK(szr_count(z2) == 5 && find_entry(z2, "chives.json") == (unsigned)-1 && find_entry(z2, "sce_sys/param.sfo") != (unsigned)-1 && find_entry(z2, "data.bin") != (unsigned)-1);
      szr_close(z2);
    }
    CHECK(count_dir(WORK) == 0);
    wfile(pparam, sfo, sfo_len);
    unlink(binp);
  }
  make_image(IMG, 2, 4096, 12, 400000);                                                                   /* a block size no save has */
  rc = savexport_run(&a, hooks(&pr), &res, err, sizeof(err));
  CHECK(rc != 0 && strstr(err, "kein Spielstand"));
  make_image(IMG, 2, 32768, 12, 0x20);                                                                    /* shorter than a header */
  rc = savexport_run(&a, hooks(&pr), &res, err, sizeof(err));
  CHECK(rc != 0);
  a.image = "/nonexistent/sdimg_X";
  rc = savexport_run(&a, hooks(&pr), &res, err, sizeof(err));
  CHECK(rc != 0 && strstr(err, "gibt es nicht"));
  a.image = IMG;
  make_image(IMG, 2, 32768, 12, 400000);

  /* --- names that cannot travel */
  reset_dirs();
  prepare_tree(0);
  {
    char p[700];
    snprintf(p, sizeof(p), "%s/bad:name.txt", TREE);
    wfile(p, "x", 1);
    a.zip_name = "badname.zip";
    rc = savexport_run(&a, hooks(&pr), &res, err, sizeof(err));
    CHECK(rc != 0 && strstr(err, "bad:name.txt") && count_dir(OUT) == 0 && count_dir(WORK) == 0);
    unlink(p);
  }
  {
    char p[900];
    char longname[220];
    memset(longname, 'n', sizeof(longname) - 1);
    longname[sizeof(longname) - 1] = 0;
    snprintf(p, sizeof(p), "%s/%s", TREE, longname);
    wfile(p, "x", 1);
    rc = savexport_run(&a, hooks(&pr), &res, err, sizeof(err));
    CHECK(rc != 0 && count_dir(OUT) == 0 && count_dir(WORK) == 0);
    unlink(p);
  }
  /* a folder nested deeper than a save ever goes */
  {
    char p[900];
    snprintf(p, sizeof(p), "%s/a/b/c/d/e/f/g/h/i/j/k.txt", TREE);
    wfile(p, "x", 1);
    rc = savexport_run(&a, hooks(&pr), &res, err, sizeof(err));
    CHECK(rc != 0 && count_dir(OUT) == 0 && count_dir(WORK) == 0);
    rmtree("/tmp/ps5tm-savexport/fake_mount_source/a");
  }
  /* more files than the limit */
  {
    char p[900];
    for(unsigned i = 0; i < 5100; i++) { snprintf(p, sizeof(p), "%s/many/f%u", TREE, i); wfile(p, "x", 1); }
    rc = savexport_run(&a, hooks(&pr), &res, err, sizeof(err));
    CHECK(rc != 0 && strstr(err, "mehr als") && count_dir(OUT) == 0 && count_dir(WORK) == 0);
  }

  /* --- an unwritable place for the ZIP */
  reset_dirs();
  prepare_tree(0);
  rmtree(OUT);
  wfile(OUT, "I am a file where a folder should be", 10);
  a.zip_name = "blocked.zip";
  rc = savexport_run(&a, hooks(&pr), &res, err, sizeof(err));
  CHECK(rc != 0 && count_dir(WORK) == 0);
  unlink(OUT);

  /* --- a name for the ZIP that holds a folder */
  a.zip_name = "x/y.zip";
  rc = savexport_run(&a, hooks(&pr), &res, err, sizeof(err));
  CHECK(rc != 0 && strstr(err, "Name der Datei"));

  rmtree(ROOT);
  printf("%d checks, %d failed\n", g_n, g_fail);
  return g_fail ? 1 : 0;
}
