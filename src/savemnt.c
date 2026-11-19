/* PS5 Cooling & System Center - Pro: opening a save of the console (savemnt.h). */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "ps5tm.h"
#include "savemnt.h"

#define AUTHID_SAVEDATA 0x4800000000000010ULL
#define PFSMGR_UNSEAL   0xc0845302u

int
savemnt_is_ps5_header(const uint8_t *header, size_t len) {
  if(!header || len < 0x40) return 0;
  uint64_t v = 0, magic = 0;
  for(int i = 7; i >= 0; i--) { v = (v << 8) | header[i]; magic = (magic << 8) | header[8 + i]; }
  return v == 2 && magic == 20130315u;
}

int
savemnt_header_kind(const uint8_t *header, size_t len) {
  if(!header || len < 0x40) return 0;
  uint64_t v = 0, magic = 0;
  for(int i = 7; i >= 0; i--) { v = (v << 8) | header[i]; magic = (magic << 8) | header[8 + i]; }
  if(magic != 20130315u) return 0;
  return v == 1 ? 1 : v == 2 ? 2 : 0;
}

#ifdef PS5TM_HOST_TEST

/* ------------------------------------------------------------------ host: no kernel
 * A "mount" copies the folder the test names (ps5tm_savemnt_test_tree) into the mount point, an "unmount" empties the mount point again; the param blob is
 * read from the file ps5tm_savemnt_test_param names. ps5tm_savemnt_test_fail makes the next open (1), close (2) or umount_path (4) fail;
 * ps5tm_savemnt_test_param_rc (-2) makes the param read fail the way a console that does not understand the question does. */

const char *ps5tm_savemnt_test_tree;                             /* what a mount shows of an image the store does not know yet */
const char *ps5tm_savemnt_test_store;                            /* a folder: what an image holds is kept in <store>/<image name> from unmount to the next mount */
const char *ps5tm_savemnt_test_param;                            /* the file that stands for the blob in the image header (NULL: none) */
int ps5tm_savemnt_test_fail;
int ps5tm_savemnt_test_param_rc;
void (*ps5tm_savemnt_test_on_open)(const char *mount_dir, int nth);  /* called after a mount went through: a test may change what the mount shows */
int ps5tm_savemnt_test_geom_bad;                                 /* the next create makes an image with the wrong block count */
int ps5tm_savemnt_test_opened;                                   /* how many opens went through */
int ps5tm_savemnt_test_closed;

static int
host_copy_tree(const char *src, const char *dst) {
  DIR *d = opendir(src);
  if(!d) return -1;
  struct dirent *e;
  int rc = 0;
  while(rc == 0 && (e = readdir(d)) != NULL) {
    if(!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
    char s2[1024], d2[1024];
    snprintf(s2, sizeof(s2), "%s/%s", src, e->d_name);
    snprintf(d2, sizeof(d2), "%s/%s", dst, e->d_name);
    struct stat st;
    if(lstat(s2, &st) != 0) { rc = -1; break; }
    if(S_ISDIR(st.st_mode)) {
      if(mkdir(d2, 0755) != 0 && errno != EEXIST) { rc = -1; break; }
      rc = host_copy_tree(s2, d2);
      chmod(d2, st.st_mode & 07777);
    } else if(S_ISREG(st.st_mode)) {
      FILE *in = fopen(s2, "rb"), *out = fopen(d2, "wb");
      if(!in || !out) rc = -1;
      else {
        char buf[4096];
        size_t n;
        while((n = fread(buf, 1, sizeof(buf), in)) > 0) if(fwrite(buf, 1, n, out) != n) rc = -1;
      }
      if(in) fclose(in);
      if(out) fclose(out);
      chmod(d2, st.st_mode & 07777);
    }
  }
  closedir(d);
  return rc;
}

static void
host_clear_tree(const char *dir) {
  DIR *d = opendir(dir);
  if(!d) return;
  struct dirent *e;
  while((e = readdir(d)) != NULL) {
    if(!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
    char p[1024];
    snprintf(p, sizeof(p), "%s/%s", dir, e->d_name);
    struct stat st;
    if(lstat(p, &st) != 0) continue;
    if(S_ISDIR(st.st_mode)) { host_clear_tree(p); rmdir(p); }
    else unlink(p);
  }
  closedir(d);
}

int
savemnt_open(const char *image, const char *mount_dir, savemnt_t *m, char *err, size_t err_len) {
  memset(m, 0, sizeof(*m));
  if(err_len) err[0] = '\0';
  if(ps5tm_savemnt_test_fail & 1) {
    ps5tm_savemnt_test_fail &= ~1;
    snprintf(err, err_len, "Die Konsole hat den Spielstand nicht eingehängt (Antwort 0x80020016).");
    return -1;
  }
  {
    char kept[1100];
    const char *base = strrchr(image, '/');
    base = base ? base + 1 : image;
    struct stat ks;
    if(ps5tm_savemnt_test_store && snprintf(kept, sizeof(kept), "%s/%s", ps5tm_savemnt_test_store, base) < (int)sizeof(kept) && stat(kept, &ks) == 0) {
      if(host_copy_tree(kept, mount_dir) != 0) { snprintf(err, err_len, "Teststub: gespeicherter Baum nicht kopierbar."); return -1; }
    } else if(ps5tm_savemnt_test_tree && host_copy_tree(ps5tm_savemnt_test_tree, mount_dir) != 0) {
      snprintf(err, err_len, "Teststub: Baum nicht kopierbar.");
      return -1;
    }
  }
  snprintf(m->image, sizeof(m->image), "%s", image);
  snprintf(m->mount, sizeof(m->mount), "%s", mount_dir);
  m->open = 1;
  ps5tm_savemnt_test_opened++;
  if(ps5tm_savemnt_test_on_open) ps5tm_savemnt_test_on_open(mount_dir, ps5tm_savemnt_test_opened);
  return 0;
}

int
savemnt_open_sealed(const char *image, const uint8_t sealed[96], const char *mount_dir, savemnt_t *m, char *err, size_t err_len) {
  (void)sealed;
  return savemnt_open(image, mount_dir, m, err, err_len);
}

int
savemnt_create_ps4(const char *path, uint64_t size, uint8_t sealed_out[96], char *err, size_t err_len) {
  if(err_len) err[0] = '\0';
  if(ps5tm_savemnt_test_fail & 8) {
    ps5tm_savemnt_test_fail &= ~8;
    snprintf(err, err_len, "Die Konsole konnte das Abbild nicht anlegen (Antwort 0x80020016).");
    return -1;
  }
  if(size < 65536 || size % 32768) { snprintf(err, err_len, "Teststub: falsche Größe."); return -1; }
  FILE *f = fopen(path, "wbx");
  if(!f) { snprintf(err, err_len, "Teststub: Datei nicht anlegbar."); return -1; }
  uint8_t head[0x400];
  memset(head, 0, sizeof(head));
  const uint64_t version = 1, magic = 20130315;
  const uint32_t bs = 32768;
  memcpy(head, &version, 8);
  memcpy(head + 8, &magic, 8);
  memcpy(head + 0x20, &bs, 4);
  fwrite(head, 1, sizeof(head), f);
  if(fflush(f) != 0 || ftruncate(fileno(f), (off_t)size) != 0) { fclose(f); unlink(path); snprintf(err, err_len, "Teststub: Größe nicht setzbar."); return -1; }
  fclose(f);
  for(int i = 0; i < 96; i++) sealed_out[i] = (uint8_t)(0xA0 + i);
  if(ps5tm_savemnt_test_store) {
    char kept[1100];
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    if(snprintf(kept, sizeof(kept), "%s/%s", ps5tm_savemnt_test_store, base) < (int)sizeof(kept)) { host_clear_tree(kept); rmdir(kept); }
  }
  return 0;
}

int
savemnt_close(savemnt_t *m, char *err, size_t err_len) {
  if(err_len) err[0] = '\0';
  if(!m->open) return 0;
  m->open = 0;
  ps5tm_savemnt_test_closed++;
  if(ps5tm_savemnt_test_fail & 2) {
    ps5tm_savemnt_test_fail &= ~2;
    snprintf(err, err_len, "Die Konsole hat den Spielstand nicht sauber ausgehängt (Antwort 0x80020016).");
    return -1;
  }
  if(ps5tm_savemnt_test_store) {
    char kept[1100];
    const char *base = strrchr(m->image, '/');
    base = base ? base + 1 : m->image;
    if(snprintf(kept, sizeof(kept), "%s/%s", ps5tm_savemnt_test_store, base) < (int)sizeof(kept)) {
      mkdir(ps5tm_savemnt_test_store, 0755);
      mkdir(kept, 0755);
      host_clear_tree(kept);
      host_copy_tree(m->mount, kept);
    }
  }
  host_clear_tree(m->mount);
  return 0;
}

int
savemnt_create(const char *path, uint64_t size, uint32_t block_size, char *err, size_t err_len) {
  if(err_len) err[0] = '\0';
  if(ps5tm_savemnt_test_fail & 8) {
    ps5tm_savemnt_test_fail &= ~8;
    snprintf(err, err_len, "Die Konsole konnte das Abbild nicht anlegen (Antwort 0x80020016).");
    return -1;
  }
  if((block_size != 32768 && block_size != 65536) || size < 2 * (uint64_t)block_size || size % block_size) {
    snprintf(err, err_len, "Teststub: falsche Größe.");
    return -1;
  }
  FILE *f = fopen(path, "wbx");
  if(!f) { snprintf(err, err_len, "Teststub: Datei nicht anlegbar."); return -1; }
  uint8_t head[0x400];
  memset(head, 0, sizeof(head));
  const uint64_t blocks = size / block_size - (ps5tm_savemnt_test_geom_bad ? 1 : 0);
  ps5tm_savemnt_test_geom_bad = 0;
  const uint64_t version = 2, magic = 20130315;
  memcpy(head, &version, 8);
  memcpy(head + 8, &magic, 8);
  head[0x1c] = 0x0d;
  memcpy(head + 0x20, &block_size, 4);
  head[0x24] = 1;
  memcpy(head + 0x38, &blocks, 8);
  fwrite(head, 1, sizeof(head), f);
  if(fflush(f) != 0 || ftruncate(fileno(f), (off_t)size) != 0) { fclose(f); unlink(path); snprintf(err, err_len, "Teststub: Größe nicht setzbar."); return -1; }
  fclose(f);
  if(ps5tm_savemnt_test_store) {                                  /* a new image is empty */
    char kept[1100];
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    if(snprintf(kept, sizeof(kept), "%s/%s", ps5tm_savemnt_test_store, base) < (int)sizeof(kept)) { host_clear_tree(kept); rmdir(kept); }
  }
  return 0;
}

int
savemnt_umount_path(const char *mount_dir, char *err, size_t err_len) {
  if(err_len) err[0] = '\0';
  if(ps5tm_savemnt_test_fail & 4) {
    ps5tm_savemnt_test_fail &= ~4;
    snprintf(err, err_len, "Die Konsole hat einen Rest eines früheren Einhängens nicht gelöst (Antwort 0x1).");
    return -1;
  }
  host_clear_tree(mount_dir);
  return 0;
}

int
savemnt_param_read(const char *image, int already_elevated, uint8_t *sfo, uint32_t sfo_cap, uint32_t *sfo_len, char *err, size_t err_len) {
  (void)already_elevated;
  (void)image;
  if(err_len) err[0] = '\0';
  *sfo_len = 0;
  if(ps5tm_savemnt_test_param_rc) {
    snprintf(err, err_len, "Die Konsole beantwortet die Frage nach den Metadaten des Spielstands nicht (Fehler 25; pfsctldev:9116=25 pfsctldev:9111=25 dev: pfsctldev)");
    return ps5tm_savemnt_test_param_rc;
  }
  FILE *f = ps5tm_savemnt_test_param ? fopen(ps5tm_savemnt_test_param, "rb") : NULL;
  if(!f) return -1;
  size_t n = fread(sfo, 1, sfo_cap, f);
  fclose(f);
  *sfo_len = (uint32_t)n;
  return n ? 0 : -1;
}

#else

/* ------------------------------------------------------------------ console */

#include "dynsym.h"

#define VSH "libSceFsInternalForVsh.sprx"

typedef struct { uint8_t reserved; char *budgetid; } mount_opt_t;          /* the console's own layout: a flag byte, then a pointer */
typedef struct { uint8_t dummy; } umount_opt_t;

typedef int (*fn_init_mount_t)(mount_opt_t *);
typedef int (*fn_mount_t)(mount_opt_t *, const char *, const char *, uint8_t *);
typedef int (*fn_init_umount_t)(umount_opt_t *);
typedef int (*fn_umount_t)(umount_opt_t *, const char *, int, int);

static fn_init_mount_t  fn_init_mount;
static fn_mount_t       fn_mount;
static fn_init_umount_t fn_init_umount;
static fn_umount_t      fn_umount;

static int
vsh_ready(char *err, size_t err_len) {
  if(fn_mount && fn_umount && fn_init_mount && fn_init_umount) return 0;
  fn_init_mount = (fn_init_mount_t)ps5tm_dynsym(VSH, "sceFsInitMountSaveDataOpt");
  fn_mount = (fn_mount_t)ps5tm_dynsym(VSH, "sceFsMountSaveData");
  fn_init_umount = (fn_init_umount_t)ps5tm_dynsym(VSH, "sceFsInitUmountSaveDataOpt");
  fn_umount = (fn_umount_t)ps5tm_dynsym(VSH, "sceFsUmountSaveData");
  if(fn_mount && fn_umount && fn_init_mount && fn_init_umount) return 0;
  fn_mount = NULL;
  snprintf(err, err_len, "Die Konsole stellt das Einhängen von Spielständen nicht bereit (Funktionen in %s nicht gefunden).", VSH);
  return -1;
}

/* The sealed key of the image, opened by the kernel. key32: the 32 bytes that come back. The identity of the service must be set already. */
static int
unseal(const char *image, const uint8_t *sealed, uint8_t key32[32], char *err, size_t err_len) {
  uint8_t *buf = calloc(1, 0x100);                              /* on the heap: the driver writes beyond what a stack buffer of 0x84 would hold */
  if(!buf) { snprintf(err, err_len, "Kein Speicher."); return -1; }
  if(sealed) {
    memcpy(buf, sealed, SAVEMNT_SEALED_BYTES);                  /* a PS4 save: the key came from the companion file */
  } else {
    int fd = open(image, O_RDONLY);
    if(fd < 0 || pread(fd, buf, SAVEMNT_SEALED_BYTES, SAVEMNT_SEALED_OFFSET) != (ssize_t)SAVEMNT_SEALED_BYTES) {
      snprintf(err, err_len, "Der versiegelte Schlüssel des Spielstands ließ sich nicht lesen (Fehler %d).", errno);
      if(fd >= 0) close(fd);
      free(buf);
      return -1;
    }
    close(fd);
  }
  int pfsmgr = open("/dev/pfsmgr", O_RDWR);
  if(pfsmgr < 0) {
    snprintf(err, err_len, "Die Konsole lässt /dev/pfsmgr nicht öffnen (Fehler %d).", errno);
    free(buf);
    return -1;
  }
  errno = 0;
  const int rc = ioctl(pfsmgr, PFSMGR_UNSEAL, buf);
  const int e = errno;
  close(pfsmgr);
  if(rc < 0) {
    snprintf(err, err_len, "Die Konsole konnte den Schlüssel des Spielstands nicht öffnen (Antwort %d, Fehler %d).", rc, e);
    free(buf);
    return -1;
  }
  memcpy(key32, buf + 0x60, 32);
  memset(buf, 0, 0x100);
  free(buf);
  return 0;
}

static int
open_impl(const char *image, const uint8_t *sealed, const char *mount_dir, savemnt_t *m, char *err, size_t err_len) {
  memset(m, 0, sizeof(*m));
  if(err_len) err[0] = '\0';
  if(vsh_ready(err, err_len) != 0) return -1;
  struct stat st;
  if(stat(image, &st) != 0 || !S_ISREG(st.st_mode)) {
    snprintf(err, err_len, "Die Abbild-Datei des Spielstands fehlt.");
    return -1;
  }
  uint8_t head[0x40];
  int hfd = open(image, O_RDONLY);
  const ssize_t hn = hfd >= 0 ? pread(hfd, head, sizeof(head), 0) : -1;
  if(hfd >= 0) close(hfd);
  if(hn != (ssize_t)sizeof(head) || savemnt_header_kind(head, sizeof(head)) != (sealed ? 1 : 2)) {
    snprintf(err, err_len, sealed ? "Das ist kein PS4-Spielstand." : "Das ist kein PS5-Spielstand (nur PS5- und PS4-Spielstände lassen sich übertragen).");
    return -1;
  }
  mkdir(mount_dir, 0755);
  ps5tm_sony_api_lock();
  if(ps5tm_platform_escalate_as(AUTHID_SAVEDATA) != 0) {
    ps5tm_platform_escalate();
    ps5tm_sony_api_unlock();
    snprintf(err, err_len, "Die Rechte für den Spielstand-Dienst ließen sich nicht setzen (läuft die App über den Jailbreak?).");
    return -1;
  }
  uint8_t key[32];
  if(unseal(image, sealed, key, err, err_len) != 0) {
    ps5tm_platform_escalate();
    ps5tm_sony_api_unlock();
    return -1;
  }
  static char budget[] = "system";                               /* the option holds a pointer, not an array */
  mount_opt_t opt;
  memset(&opt, 0, sizeof(opt));
  fn_init_mount(&opt);
  opt.budgetid = budget;
  struct sigaction ign, old;
  memset(&ign, 0, sizeof(ign));
  ign.sa_handler = SIG_DFL;                                      /* as the console's own tools do for the mount: the handshake may use the signal */
  sigaction(SIGPIPE, &ign, &old);
  const int h = fn_mount(&opt, image, mount_dir, key);
  sigaction(SIGPIPE, &old, NULL);
  memset(key, 0, sizeof(key));
  if(h < 0) {
    snprintf(err, err_len, "Die Konsole hat den Spielstand nicht eingehängt (Antwort 0x%x).", (unsigned)h);
    ps5tm_platform_escalate();
    ps5tm_sony_api_unlock();
    return -1;
  }
  m->open = 1;
  m->handle = h;
  snprintf(m->image, sizeof(m->image), "%s", image);
  snprintf(m->mount, sizeof(m->mount), "%s", mount_dir);
  return 0;                                                      /* the identity and the lock stay with the caller until savemnt_close */
}

int
savemnt_open(const char *image, const char *mount_dir, savemnt_t *m, char *err, size_t err_len) {
  return open_impl(image, NULL, mount_dir, m, err, err_len);
}

int
savemnt_open_sealed(const char *image, const uint8_t sealed[96], const char *mount_dir, savemnt_t *m, char *err, size_t err_len) {
  return open_impl(image, sealed, mount_dir, m, err, err_len);
}

int
savemnt_close(savemnt_t *m, char *err, size_t err_len) {
  if(err_len) err[0] = '\0';
  if(!m || !m->open) return 0;
  int rc = 0;
  umount_opt_t uopt;
  memset(&uopt, 0, sizeof(uopt));
  fn_init_umount(&uopt);
  struct sigaction ign, old;
  memset(&ign, 0, sizeof(ign));
  ign.sa_handler = SIG_DFL;
  sigaction(SIGPIPE, &ign, &old);
  const int r = fn_umount(&uopt, m->mount, 0, 0);
  sigaction(SIGPIPE, &old, NULL);
  if(r < 0) {
    snprintf(err, err_len, "Die Konsole hat den Spielstand nicht sauber ausgehängt (Antwort 0x%x).", (unsigned)r);
    rc = -1;
  }
  sync();
  m->open = 0;
  ps5tm_platform_escalate();                                      /* the fan's identity again, then the lock */
  ps5tm_sony_api_unlock();
  return rc;
}

int
savemnt_umount_path(const char *mount_dir, char *err, size_t err_len) {
  if(err_len) err[0] = '\0';
  if(vsh_ready(err, err_len) != 0) return -1;
  ps5tm_sony_api_lock();
  if(ps5tm_platform_escalate_as(AUTHID_SAVEDATA) != 0) {
    ps5tm_platform_escalate();
    ps5tm_sony_api_unlock();
    snprintf(err, err_len, "Die Rechte für den Spielstand-Dienst ließen sich nicht setzen.");
    return -1;
  }
  umount_opt_t uopt;
  memset(&uopt, 0, sizeof(uopt));
  fn_init_umount(&uopt);
  struct sigaction ign, old;
  memset(&ign, 0, sizeof(ign));
  ign.sa_handler = SIG_DFL;
  sigaction(SIGPIPE, &ign, &old);
  const int r = fn_umount(&uopt, mount_dir, 0, 0);
  sigaction(SIGPIPE, &old, NULL);
  ps5tm_platform_escalate();
  ps5tm_sony_api_unlock();
  if(r < 0) {
    snprintf(err, err_len, "Die Konsole hat einen Rest eines früheren Einhängens nicht gelöst (Antwort 0x%x).", (unsigned)r);
    return -1;
  }
  return 0;
}

typedef struct { int blockSize; uint8_t flags[2]; } create_opt_t;                 /* the console's own layout */
typedef int (*fn_init_create_t)(create_opt_t *);
typedef int (*fn_ppr_create_t)(create_opt_t *, const char *, int, uint64_t);
typedef int (*fn_alloc_t)(int, uint64_t, int, void *);

int
savemnt_create(const char *path, uint64_t size, uint32_t block_size, char *err, size_t err_len) {
  if(err_len) err[0] = '\0';
  if((block_size != 32768 && block_size != 65536) || size < 2 * (uint64_t)block_size || size % block_size) {
    snprintf(err, err_len, "Die Größe des neuen Abbilds ist ungültig.");
    return -1;
  }
  fn_init_create_t init = (fn_init_create_t)ps5tm_dynsym(VSH, "sceFsInitCreatePfsSaveDataOpt");
  fn_ppr_create_t create = (fn_ppr_create_t)ps5tm_dynsym(VSH, "sceFsCreatePprPfsSaveDataImage");
  fn_alloc_t alloc = (fn_alloc_t)ps5tm_dynsym(VSH, "sceFsUfsAllocateSaveData");
  if(!init || !create || !alloc) {
    snprintf(err, err_len, "Die Konsole stellt das Anlegen von Spielstand-Abbildern nicht bereit (Funktionen in %s nicht gefunden).", VSH);
    return -1;
  }
  ps5tm_sony_api_lock();
  if(ps5tm_platform_escalate_as(AUTHID_SAVEDATA) != 0) {
    ps5tm_platform_escalate();
    ps5tm_sony_api_unlock();
    snprintf(err, err_len, "Die Rechte für den Spielstand-Dienst ließen sich nicht setzen (läuft die App über den Jailbreak?).");
    return -1;
  }
  int rc = -1;
  const int fd = open(path, O_CREAT | O_EXCL | O_RDWR, 0755);
  if(fd < 0) {
    snprintf(err, err_len, "Das neue Abbild ließ sich nicht anlegen (Fehler %d).", errno);
  } else {
    errno = 0;
    const int ar = alloc(fd, size, 0x80, NULL);                  /* the room is made the way the console's own save images get theirs */
    const int ae = errno;
    close(fd);
    if(ar < 0) {
      snprintf(err, err_len, "Die Konsole konnte den Platz für das neue Abbild nicht anlegen (Antwort 0x%x, Fehler %d).", (unsigned)ar, ae);
      unlink(path);
    } else {
      create_opt_t opt;
      memset(&opt, 0, sizeof(opt));
      init(&opt);
      opt.blockSize = (int)block_size;
      opt.flags[1] = 0;
      struct sigaction ign, old;
      memset(&ign, 0, sizeof(ign));
      ign.sa_handler = SIG_DFL;
      sigaction(SIGPIPE, &ign, &old);
      const int r = create(&opt, path, 0, size);
      sigaction(SIGPIPE, &old, NULL);
      if(r != 0) {
        snprintf(err, err_len, "Die Konsole konnte das neue Abbild nicht formatieren (Antwort 0x%x).", (unsigned)r);
        unlink(path);
      } else {
        sync();
        rc = 0;
      }
    }
  }
  ps5tm_platform_escalate();
  ps5tm_sony_api_unlock();
  return rc;
}

typedef int (*fn_create_old_t)(create_opt_t *, const char *, int, uint64_t, uint8_t *);

int
savemnt_create_ps4(const char *path, uint64_t size, uint8_t sealed_out[96], char *err, size_t err_len) {
  if(err_len) err[0] = '\0';
  if(size < 65536 || size % 32768) {
    snprintf(err, err_len, "Die Größe des neuen Abbilds ist ungültig.");
    return -1;
  }
  fn_init_create_t init = (fn_init_create_t)ps5tm_dynsym(VSH, "sceFsInitCreatePfsSaveDataOpt");
  fn_create_old_t create = (fn_create_old_t)ps5tm_dynsym(VSH, "sceFsCreatePfsSaveDataImage");
  fn_alloc_t alloc = (fn_alloc_t)ps5tm_dynsym(VSH, "sceFsUfsAllocateSaveData");
  if(!init || !create || !alloc) {
    snprintf(err, err_len, "Die Konsole stellt das Anlegen von Spielstand-Abbildern nicht bereit (Funktionen in %s nicht gefunden).", VSH);
    return -1;
  }
  ps5tm_sony_api_lock();
  if(ps5tm_platform_escalate_as(AUTHID_SAVEDATA) != 0) {
    ps5tm_platform_escalate();
    ps5tm_sony_api_unlock();
    snprintf(err, err_len, "Die Rechte für den Spielstand-Dienst ließen sich nicht setzen (läuft die App über den Jailbreak?).");
    return -1;
  }
  int rc = -1;
  uint8_t *buf = calloc(1, 0x100);
  if(!buf) {
    snprintf(err, err_len, "Kein Speicher.");
  } else {
    /* a fresh key, sealed to this console: the same two nodes the console's own tools ask, the first that answers */
    int got = 0;
    int kfd = open("/dev/sbl_srv", O_RDWR);
    if(kfd >= 0) { if(ioctl(kfd, 0x40845303, buf) >= 0) got = 1; close(kfd); }
    if(!got) {
      memset(buf, 0, 0x100);
      kfd = open("/dev/pfsmgr", O_RDWR);
      if(kfd >= 0) { if(ioctl(kfd, 0x40845303, buf) >= 0) got = 1; close(kfd); }
    }
    if(!got) {
      snprintf(err, err_len, "Die Konsole konnte keinen Schlüssel für das neue Abbild erzeugen.");
    } else {
      memcpy(sealed_out, buf, SAVEMNT_SEALED_BYTES);
      uint8_t key[32];
      int ok = 0;
      memset(buf, 0, 0x100);
      memcpy(buf, sealed_out, SAVEMNT_SEALED_BYTES);
      int pf = open("/dev/pfsmgr", O_RDWR);
      if(pf >= 0) { if(ioctl(pf, PFSMGR_UNSEAL, buf) >= 0) { memcpy(key, buf + 0x60, 32); ok = 1; } close(pf); }
      if(!ok) {
        snprintf(err, err_len, "Die Konsole konnte den neuen Schlüssel nicht öffnen.");
      } else {
        const int fd = open(path, O_CREAT | O_EXCL | O_RDWR, 0755);
        if(fd < 0) {
          snprintf(err, err_len, "Das neue Abbild ließ sich nicht anlegen (Fehler %d).", errno);
        } else {
          const int ar = alloc(fd, size, 0, NULL);
          if(ar < 0 && ftruncate(fd, (off_t)size) != 0) {                /* the room is made by the console where it can, else as an ordinary file */
            const int ae = errno;
            close(fd);
            unlink(path);
            snprintf(err, err_len, "Die Konsole konnte den Platz für das neue Abbild nicht anlegen (Antwort 0x%x, Fehler %d).", (unsigned)ar, ae);
          } else {
            close(fd);
            create_opt_t opt;
            memset(&opt, 0, sizeof(opt));
            init(&opt);
            struct sigaction ign, old;
            memset(&ign, 0, sizeof(ign));
            ign.sa_handler = SIG_DFL;
            sigaction(SIGPIPE, &ign, &old);
            const int r = create(&opt, path, 0, size, key);
            sigaction(SIGPIPE, &old, NULL);
            if(r != 0) {
              snprintf(err, err_len, "Die Konsole konnte das neue Abbild nicht formatieren (Antwort 0x%x).", (unsigned)r);
              unlink(path);
            } else {
              sync();
              rc = 0;
            }
          }
        }
        memset(key, 0, sizeof(key));
      }
    }
    memset(buf, 0, 0x100);
    free(buf);
  }
  ps5tm_platform_escalate();
  ps5tm_sony_api_unlock();
  return rc;
}

/* ---- the PS5PARAM blob */

#define PFSCTL_PARAM_READ  0xC0189111u
#define PFSCTL_PARAM_READ2 0xC0209116u

typedef struct {
  const char *path;
  void       *buf;
  uint32_t    size;
  uint32_t    pad;
  uint64_t   *out;
} pfs_param_arg_t;

/* The order the console's own tools try them in; /dev/pfsctldev is created with mode 0400, so it is opened read-only first (the ioctl works whatever the open mode). */
static const char *const k_param_devs[] = { "/dev/pfsctl", "/dev/pfsmgr", "/dev/devpfs.ctl", "/dev/pfs", "/dev/pfsctldev", NULL };

static int
blob_ok(const uint8_t *b) {
  uint64_t magic = 0;
  for(int i = 7; i >= 0; i--) magic = (magic << 8) | b[i];
  return magic == SAVEMNT_PARAM_MAGIC;
}

static void
diag_add(char *d, size_t cap, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
static void
diag_add(char *d, size_t cap, const char *fmt, ...) {
  const size_t n = strlen(d);
  if(n + 2 >= cap) return;
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(d + n, cap - n, fmt, ap);
  va_end(ap);
}

/* The names of the /dev nodes that have "pfs" in them: which one serves the metadata is not the same on every firmware. */
static void
diag_dev_nodes(char *d, size_t cap) {
  DIR *dir = opendir("/dev");
  if(!dir) { diag_add(d, cap, " dev=(opendir %d)", errno); return; }
  diag_add(d, cap, " dev:");
  struct dirent *e;
  unsigned n = 0;
  while((e = readdir(dir)) != NULL && n < 12) {
    if(!strstr(e->d_name, "pfs") && !strstr(e->d_name, "PFS")) continue;
    diag_add(d, cap, " %s", e->d_name);
    n++;
  }
  closedir(dir);
  if(!n) diag_add(d, cap, " (none)");
}

int
savemnt_param_read(const char *image, int already_elevated, uint8_t *sfo, uint32_t sfo_cap, uint32_t *sfo_len, char *err, size_t err_len) {
  if(err_len) err[0] = '\0';
  *sfo_len = 0;
  uint8_t *blob = calloc(1, SAVEMNT_PARAM_BYTES);
  if(!blob) { snprintf(err, err_len, "Kein Speicher."); return -2; }
  if(!already_elevated) {
    ps5tm_sony_api_lock();
    if(ps5tm_platform_escalate_as(AUTHID_SAVEDATA) != 0) {
      ps5tm_platform_escalate();
      ps5tm_sony_api_unlock();
      free(blob);
      snprintf(err, err_len, "Die Rechte für den Spielstand-Dienst ließen sich nicht setzen.");
      return -2;
    }
  }
  static const unsigned cmds[2] = { PFSCTL_PARAM_READ, PFSCTL_PARAM_READ2 };
  char diag[420];
  diag[0] = '\0';
  int found = 0, opened = 0, soft = 0, last_errno = 0;
  for(int d = 0; k_param_devs[d] && !found; d++) {
    int fd = open(k_param_devs[d], O_RDONLY);                    /* ioctl works whatever the open mode; some nodes refuse O_RDWR */
    if(fd < 0 && errno == EACCES) fd = open(k_param_devs[d], O_RDWR);
    if(fd < 0) {
      const int oe = errno;
      diag_add(diag, sizeof(diag), " %s:open=%d", k_param_devs[d] + 5, oe);
      if(!last_errno) last_errno = oe;
      continue;
    }
    opened = 1;
    for(int c = 0; c < 2 && !found; c++) {
      uint64_t out = 0;
      pfs_param_arg_t a;
      memset(&a, 0, sizeof(a));
      memset(blob, 0, SAVEMNT_PARAM_BYTES);
      a.path = image;
      a.buf = blob;
      a.size = SAVEMNT_PARAM_BYTES;
      a.out = &out;
      errno = 0;
      const int r = ioctl(fd, cmds[c], &a);
      const int ie = errno;
      if(r < 0) {
        diag_add(diag, sizeof(diag), " %s:%x=%d", k_param_devs[d] + 5, cmds[c] & 0xffff, ie);
        last_errno = ie ? ie : EIO;
        continue;
      }
      if(blob_ok(blob)) found = 1;
      else { soft = 1; diag_add(diag, sizeof(diag), " %s:%x=nomagic", k_param_devs[d] + 5, cmds[c] & 0xffff); }   /* the node answered, with no blob in it */
    }
    close(fd);
  }
  if(!found) diag_dev_nodes(diag, sizeof(diag));
  if(!already_elevated) {
    ps5tm_platform_escalate();
    ps5tm_sony_api_unlock();
  }
  if(!found) {
    free(blob);
    if(soft) return -1;
    snprintf(err, err_len, "%s (Fehler %d;%s)",
             opened ? "Die Konsole beantwortet die Frage nach den Metadaten des Spielstands nicht" :
                      "Kein Gerät der Konsole für die Metadaten des Spielstands ließ sich öffnen", last_errno, diag);
    return -2;
  }
  uint32_t ver = 0, size = 0, off = 0;
  memcpy(&ver, blob + 8, 4);
  memcpy(&size, blob + 12, 4);
  memcpy(&off, blob + 16, 4);
  if(ver != 0 || size == 0 || size > sfo_cap || off < 20 || (uint64_t)off + size > SAVEMNT_PARAM_BYTES) {
    free(blob);
    snprintf(err, err_len, "Die Metadaten des Spielstands haben ein unbekanntes Format (Version %u, Größe %u).", (unsigned)ver, (unsigned)size);
    return -2;
  }
  memcpy(sfo, blob + off, size);
  *sfo_len = size;
  free(blob);
  return 0;
}

#endif
