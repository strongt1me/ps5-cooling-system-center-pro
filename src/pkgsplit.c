/* Splitting a package into parts (04.10.2026).
 *
 * A package of 20 GB does not fit on a FAT32 stick (4 GB per file) and not on one disc. This job cuts a package
 * the last search found into parts: "<name>.pkg.part1", ".part2", ... in the folder "pkg" of the drive the page
 * names. Each part is a 4096-byte header (PS5MPKG1: which package, which part of how many, where its slice
 * begins and ends, title, content id, type, version, the icon's place), in the first part the icon, then a slice of
 * the package as it is. The PS5 PKG Manager reads the same parts: it is its container format, and it is
 * also what this app's own package list shows as one entry.
 *
 * The package is read once, in order. Every part is written to a temporary name and checked: the header is read
 * back and compared, and the slice is read back from the drive and its SHA-256 must equal the one taken of what
 * was read from the package. Only when every part is good do they get their real names, so nothing half
 * written is ever seen as a part (by the list, or by the PKG Manager). The package itself is never touched.
 *
 * Nothing already there is overwritten: a part name that exists stops the job before it starts. A stop ("cancel") or any
 * failure removes what was written. One job at a time, and not alongside a copy, a conversion or a backup of saved games:
 * they all want the same drives. */

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <time.h>
#include <unistd.h>

#include "checkfile.h"
#include "ioerr.h"
#include "iopolicy.h"
#include "ps5tm.h"
#include "sha256.h"
#include "third_party/cJSON.h"

#define SP_HEADER        4096u
#define SP_BUF           (1u << 20)
#define SP_PREFETCH_BUFS 4
#define SP_MIN_PART      (64ull << 20)           /* a part below this is not worth having */
#define SP_MAX_PART      (64ull << 30)
#define SP_MAX_PARTS     256u
#define SP_RESERVE       (64ull << 20)           /* what is left free on the target */
#define SP_TMP_SUFFIX    ".ps5cc-teil"

enum { J_IDLE, J_SPLIT, J_VERIFY, J_DONE, J_FAILED, J_CANCELLED };
static const char *const k_state[] = { "idle", "splitting", "verifying", "done", "failed", "cancelled" };

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static struct {
  int      state;
  char     name[160], file[256];
  char     phase[40];
  unsigned parts, part;
  uint64_t bytes_total, bytes_done;
  char     dir[PS5TM_PKG_PATH];
  char     error[640], note[320];       /* error: room for PS5TM_IO_TEXT_MAX (ioerr.h) after a label */
  char     sha[65];
  uint64_t started_ms, finished_ms;
} g_job;
static int g_cancel;

#ifdef PS5TM_HOST_TEST
void (*ps5tm_pkgsplit_test_hook)(const char *tmp_path, unsigned part);      /* after a part is written (tests damage it) */
#endif

static uint64_t
wall_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  return (uint64_t)ts.tv_sec * 1000 + (uint64_t)(ts.tv_nsec / 1000000);
}

static int
active_locked(void) { return g_job.state == J_SPLIT || g_job.state == J_VERIFY; }

int
ps5tm_pkgsplit_busy(void) {
  pthread_mutex_lock(&g_lock);
  int a = active_locked();
  pthread_mutex_unlock(&g_lock);
  return a;
}

static void
job_phase(int state, const char *phase) {
  pthread_mutex_lock(&g_lock);
  g_job.state = state;
  snprintf(g_job.phase, sizeof(g_job.phase), "%s", phase);
  pthread_mutex_unlock(&g_lock);
}

static void
job_progress(uint64_t add) {
  pthread_mutex_lock(&g_lock);
  g_job.bytes_done += add;
  pthread_mutex_unlock(&g_lock);
}

static void
job_end(int state) {
  pthread_mutex_lock(&g_lock);
  g_job.state = state;
  g_job.finished_ms = wall_ms();
  pthread_mutex_unlock(&g_lock);
}

static void
job_fail(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void
job_fail(const char *fmt, ...) {
  pthread_mutex_lock(&g_lock);
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(g_job.error, sizeof(g_job.error), fmt, ap);
  va_end(ap);
  g_job.state = J_FAILED;
  g_job.finished_ms = wall_ms();
  pthread_mutex_unlock(&g_lock);
  PS5TM_WARN("pkg_split_failed", "Pakete: Aufteilen fehlgeschlagen.");
}

static int
cancelled(void) { return __atomic_load_n(&g_cancel, __ATOMIC_ACQUIRE); }

void
ps5tm_pkgsplit_cancel(void) {
  pthread_mutex_lock(&g_lock);
  int a = active_locked();
  pthread_mutex_unlock(&g_lock);
  if(a) __atomic_store_n(&g_cancel, 1, __ATOMIC_RELEASE);
}

/* ------------------------------------------------------------------ the header of a part */

static void put_le32(uint8_t *p, uint32_t v) { for(int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i)); }
static void put_le64(uint8_t *p, uint64_t v) { for(int i = 0; i < 8; i++) p[i] = (uint8_t)(v >> (8 * i)); }
static void put_str(uint8_t *p, size_t n, const char *s) { size_t l = strlen(s); if(l > n - 1) l = n - 1; memcpy(p, s, l); }

typedef struct {
  unsigned part, parts;
  uint64_t part_data, total_pkg, total_archive, part_off;
  uint32_t data_off, icon_off, icon_size;
  uint8_t  uuid[16];
} plan_t;

static void
make_header(uint8_t h[SP_HEADER], const plan_t *pl, const ps5tm_pkg_t *p, const char *orig) {
  memset(h, 0, SP_HEADER);
  memcpy(h, "PS5MPKG1", 8);
  put_le32(h + 8, 1);                        /* header version */
  put_le32(h + 12, pl->part);
  put_le32(h + 16, pl->parts);
  put_le32(h + 20, 0);                       /* the slice is stored as it is */
  put_le32(h + 24, SP_BUF);
  put_le32(h + 28, 0);
  put_le64(h + 32, pl->part_data);
  put_le64(h + 40, pl->total_pkg);
  put_le64(h + 48, pl->total_archive);
  put_str(h + 56, 256, orig);
  put_str(h + 312, 32, p->title_id);
  put_str(h + 344, 256, p->name);
  put_str(h + 600, 64, p->content_id);
  memcpy(h + 664, pl->uuid, 16);
  put_le32(h + 680, pl->icon_off);
  put_le32(h + 684, pl->icon_size);
  put_str(h + 688, 32, p->version);
  put_str(h + 720, 16, p->kind);
  put_le64(h + 736, pl->part_off);
  put_le32(h + 744, pl->data_off);
}

static void
random_bytes(uint8_t *out, size_t n) {
  int fd = open("/dev/urandom", O_RDONLY);
  size_t got = 0;
  if(fd >= 0) {
    while(got < n) {
      ssize_t r = read(fd, out + got, n - got);
      if(r < 0 && errno == EINTR) continue;
      if(r <= 0) break;
      got += (size_t)r;
    }
    close(fd);
  }
  if(got < n) {                               /* no random device: time and addresses are unique enough for a group id */
    uint64_t x = wall_ms() * 6364136223846793005ull + (uint64_t)(uintptr_t)out;
    for(; got < n; got++) { x = x * 6364136223846793005ull + 1442695040888963407ull; out[got] = (uint8_t)(x >> 56); }
  }
}

/* ------------------------------------------------------------------ the job */

typedef struct {
  ps5tm_pkg_t pkg;
  char        dir[PS5TM_PKG_PATH];            /* the folder the parts go to */
  char        base[256];                      /* "<file name of the package>" */
  uint64_t    part_bytes;                     /* the biggest size of a part file */
  int         parallel;
} args_t;

typedef struct { int fd; } fd_src_t;

static ssize_t
fd_read(void *ctx, void *buf, size_t len) {
  fd_src_t *f = ctx;
  for(;;) {
    ssize_t r = read(f->fd, buf, len);
    if(r >= 0 || errno != EINTR) return r;
  }
}

static int
write_all(int fd, const void *buf, size_t n) {
  const uint8_t *p = buf;
  while(n) {
    ssize_t w = write(fd, p, n);
    if(w < 0 && errno == EINTR) continue;
    if(w <= 0) return w < 0 ? errno : EIO;
    p += w;
    n -= (size_t)w;
  }
  return 0;
}

static void
part_names(const args_t *a, unsigned k, char *tmp, size_t tn, char *fin, size_t fn) {
  snprintf(fin, fn, "%s/%s.part%u", a->dir, a->base, k);
  snprintf(tmp, tn, "%s" SP_TMP_SUFFIX, fin);
}

static void
remove_temps(const args_t *a, unsigned parts) {
  for(unsigned k = 1; k <= parts; k++) {
    char tmp[PS5TM_PKG_PATH + 32], fin[PS5TM_PKG_PATH + 32];
    part_names(a, k, tmp, sizeof(tmp), fin, sizeof(fin));
    unlink(tmp);
  }
}

/* The plan of part k of `parts`: its slice and its place. */
static void
plan_part(plan_t *pl, unsigned k, unsigned parts, const args_t *a, uint32_t icon_n, const uint8_t uuid[16]) {
  uint64_t first_off = SP_HEADER + icon_n;
  uint64_t first_payload = a->part_bytes - first_off, next_payload = a->part_bytes - SP_HEADER;
  memset(pl, 0, sizeof(*pl));
  pl->part = k;
  pl->parts = parts;
  pl->total_pkg = a->pkg.size;
  pl->total_archive = (uint64_t)parts * SP_HEADER + icon_n + a->pkg.size;
  memcpy(pl->uuid, uuid, 16);
  if(k == 1) {
    pl->data_off = (uint32_t)first_off;
    pl->part_off = 0;
    pl->part_data = a->pkg.size < first_payload ? a->pkg.size : first_payload;
    if(icon_n) { pl->icon_off = SP_HEADER; pl->icon_size = icon_n; }
  } else {
    pl->data_off = SP_HEADER;
    pl->part_off = first_payload + (uint64_t)(k - 2) * next_payload;
    uint64_t left = a->pkg.size - pl->part_off;
    pl->part_data = left < next_payload ? left : next_payload;
  }
}

static unsigned
parts_needed(const args_t *a, uint32_t icon_n) {
  uint64_t first_payload = a->part_bytes - SP_HEADER - icon_n, next_payload = a->part_bytes - SP_HEADER;
  if(a->pkg.size <= first_payload) return 1;
  return 1 + (unsigned)((a->pkg.size - first_payload + next_payload - 1) / next_payload);
}

static void *
split_main(void *arg) {
  args_t *a = arg;
  ps5tm_powerguard_hold();
  uint8_t *buf = malloc(SP_BUF), *icon = NULL, *hdr = malloc(SP_HEADER), *hdr2 = malloc(SP_HEADER);
  size_t icon_n = 0;
  uint8_t (*digests)[32] = calloc(SP_MAX_PARTS, 32);
  int in = -1, out = -1;
  ps5tm_prefetch_t *pf = NULL;
  unsigned parts = 0, written = 0;
  ps5tm_sha256_t whole;
  uint8_t uuid[16];
  if(!buf || !hdr || !hdr2 || !digests) { job_fail("Kein Speicher."); goto out; }

  in = open(a->pkg.path, O_RDONLY);
  struct stat st;
  if(in < 0 || fstat(in, &st) != 0 || (uint64_t)st.st_size != a->pkg.size || (int64_t)st.st_mtime != a->pkg.mtime) {
    job_fail("Das Paket hat sich seit der letzten Suche verändert oder ist nicht mehr da. Bitte noch einmal suchen.");
    goto out;
  }
  if(a->pkg.icon_size) {
    uint8_t *ic = NULL;
    size_t n = 0;
    if(ps5tm_pkg_icon(&a->pkg, &ic, &n) == 0 && n <= 0x7FFFFFF0u) { icon = ic; icon_n = n; }
  }
  parts = parts_needed(a, (uint32_t)icon_n);
  if(parts < 2 || parts > SP_MAX_PARTS) { job_fail("Mit dieser Teilgröße ergeben sich %u Teile; erlaubt sind 2 bis %u.", parts, SP_MAX_PARTS); goto out; }
  random_bytes(uuid, sizeof(uuid));
  pthread_mutex_lock(&g_lock);
  g_job.parts = parts;
  g_job.bytes_total = 2 * a->pkg.size;                        /* writing, then reading back */
  g_job.bytes_done = 0;
  pthread_mutex_unlock(&g_lock);

  ps5tm_sha256_init(&whole);
  fd_src_t fs = { in };
  if(a->parallel) pf = ps5tm_prefetch_start(fd_read, &fs, SP_BUF, SP_PREFETCH_BUFS);

  /* 1: write every part under a temporary name */
  job_phase(J_SPLIT, "Teilen");
  for(unsigned k = 1; k <= parts; k++) {
    plan_t pl;
    plan_part(&pl, k, parts, a, (uint32_t)icon_n, uuid);
    char tmp[PS5TM_PKG_PATH + 32], fin[PS5TM_PKG_PATH + 32];
    part_names(a, k, tmp, sizeof(tmp), fin, sizeof(fin));
    pthread_mutex_lock(&g_lock);
    g_job.part = k;
    pthread_mutex_unlock(&g_lock);
    out = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if(out < 0) { job_fail("Ein Teil ließ sich nicht anlegen: %s", ps5tm_io_strerror(errno)); goto cleanup; }
    written = k;                                              /* the temp exists from here on */
    make_header(hdr, &pl, &a->pkg, a->pkg.file);
    int rc = write_all(out, hdr, SP_HEADER);
    if(!rc && k == 1 && icon_n) rc = write_all(out, icon, icon_n);
    uint64_t left = pl.part_data;
    ps5tm_sha256_t part;
    ps5tm_sha256_init(&part);
    while(!rc && left > 0) {
      if(cancelled()) { job_end(J_CANCELLED); goto cleanup; }
      size_t want = left < SP_BUF ? (size_t)left : SP_BUF;
      ssize_t n = pf ? ps5tm_prefetch_read(pf, buf, want) : read(in, buf, want);
      if(n < 0) {
        if(!pf && errno == EINTR) continue;
        job_fail("Das Paket ließ sich nicht lesen: %s", ps5tm_io_strerror(errno ? errno : EIO));
        goto cleanup;
      }
      if(n == 0) { job_fail("Das Paket ist kürzer, als die Suche meldete."); goto cleanup; }
      ps5tm_sha256_update(&part, buf, (size_t)n);
      ps5tm_sha256_update(&whole, buf, (size_t)n);
      rc = write_all(out, buf, (size_t)n);
      left -= (uint64_t)n;
      job_progress((uint64_t)n);
    }
    if(!rc && fsync(out) != 0) rc = errno ? errno : EIO;
    if(close(out) != 0 && !rc) rc = errno ? errno : EIO;
    out = -1;
    if(rc) {
      job_fail("Teil %u ließ sich nicht schreiben: %s", k, ps5tm_io_strerror(rc));
      goto cleanup;
    }
    ps5tm_sha256_final(&part, digests[k - 1]);
#ifdef PS5TM_HOST_TEST
    if(ps5tm_pkgsplit_test_hook) ps5tm_pkgsplit_test_hook(tmp, k);
#endif
  }
  {
    uint8_t d[32];
    ps5tm_sha256_final(&whole, d);
    char hx[65];
    ps5tm_sha256_hex(d, hx);
    pthread_mutex_lock(&g_lock);
    snprintf(g_job.sha, sizeof(g_job.sha), "%s", hx);
    pthread_mutex_unlock(&g_lock);
  }
  ps5tm_prefetch_stop(pf);
  pf = NULL;
  close(in);
  in = -1;

  /* 2: read every part back from the drive: the header as written, the slice as read from the package */
  job_phase(J_VERIFY, "Prüfen");
  for(unsigned k = 1; k <= parts; k++) {
    plan_t pl;
    plan_part(&pl, k, parts, a, (uint32_t)icon_n, uuid);
    char tmp[PS5TM_PKG_PATH + 32], fin[PS5TM_PKG_PATH + 32];
    part_names(a, k, tmp, sizeof(tmp), fin, sizeof(fin));
    pthread_mutex_lock(&g_lock);
    g_job.part = k;
    pthread_mutex_unlock(&g_lock);
    int fd = open(tmp, O_RDONLY);
    struct stat ps;
    uint64_t want_size = pl.data_off + pl.part_data;
    if(fd < 0 || fstat(fd, &ps) != 0 || (uint64_t)ps.st_size != want_size) {
      if(fd >= 0) close(fd);
      job_fail("Teil %u hat nach dem Schreiben nicht die erwartete Größe.", k);
      goto cleanup;
    }
    ps5tm_drop_cache(fd);
    make_header(hdr2, &pl, &a->pkg, a->pkg.file);
    if(pread(fd, hdr, SP_HEADER, 0) != (ssize_t)SP_HEADER || memcmp(hdr, hdr2, SP_HEADER) != 0) {
      close(fd);
      job_fail("Der Kopf von Teil %u wurde nicht richtig geschrieben.", k);
      goto cleanup;
    }
    ps5tm_sha256_t rd;
    ps5tm_sha256_init(&rd);
    uint64_t left = pl.part_data, off = pl.data_off;
    while(left > 0) {
      if(cancelled()) { close(fd); job_end(J_CANCELLED); goto cleanup; }
      size_t want = left < SP_BUF ? (size_t)left : SP_BUF;
      ssize_t n = pread(fd, buf, want, (off_t)off);
      if(n < 0 && errno == EINTR) continue;
      if(n <= 0) { close(fd); job_fail("Teil %u ließ sich zum Prüfen nicht lesen: %s", k, ps5tm_io_strerror(n < 0 ? errno : EIO)); goto cleanup; }
      ps5tm_sha256_update(&rd, buf, (size_t)n);
      left -= (uint64_t)n;
      off += (uint64_t)n;
      job_progress((uint64_t)n);
    }
    close(fd);
    uint8_t got[32];
    ps5tm_sha256_final(&rd, got);
    if(memcmp(got, digests[k - 1], 32) != 0) {
      job_fail("Teil %u weicht nach dem Schreiben vom Paket ab (das Laufwerk hat falsch geschrieben). Es wurde nichts angelegt.", k);
      goto cleanup;
    }
  }

  /* 3: only now the parts get their names */
  for(unsigned k = 1; k <= parts; k++) {
    char tmp[PS5TM_PKG_PATH + 32], fin[PS5TM_PKG_PATH + 32];
    part_names(a, k, tmp, sizeof(tmp), fin, sizeof(fin));
    if(access(fin, F_OK) == 0 || rename(tmp, fin) != 0) {
      int e = errno;
      for(unsigned j = 1; j < k; j++) {                       /* the ones already named go again */
        char t2[PS5TM_PKG_PATH + 32], f2[PS5TM_PKG_PATH + 32];
        part_names(a, j, t2, sizeof(t2), f2, sizeof(f2));
        unlink(f2);
      }
      job_fail("Teil %u ließ sich nicht an seinen Platz setzen (%s).", k, access(fin, F_OK) == 0 ? "den Namen gibt es schon" : ps5tm_io_strerror(e));
      goto cleanup;
    }
  }
  {
    char sz[32];
    double gb = (double)a->part_bytes / (1024.0 * 1024.0 * 1024.0);
    snprintf(sz, sizeof(sz), "%.1f", gb);
    pthread_mutex_lock(&g_lock);
    snprintf(g_job.note, sizeof(g_job.note), "%u Teile in %s, höchstens %s GB je Teil; jeder Teil wurde nach dem Schreiben zurückgelesen und geprüft.", parts, a->dir, sz);
    pthread_mutex_unlock(&g_lock);
  }
  job_end(J_DONE);
  PS5TM_INFO("pkg_split_done", "Pakete: ein Paket in %u Teile aufgeteilt, jeder Teil zurückgelesen und geprüft.", parts);
  ps5tm_pkgscan_start();                                      /* the list shows the parts as one package */
  goto out;

cleanup:
  if(out >= 0) { close(out); out = -1; }
  remove_temps(a, written ? written : 0);
out:
  if(pf) ps5tm_prefetch_stop(pf);
  if(in >= 0) close(in);
  free(icon);
  free(buf);
  free(hdr);
  free(hdr2);
  free(digests);
  ps5tm_powerguard_release();
  free(a);
  return NULL;
}

/* ------------------------------------------------------------------ the start */

static int
other_job_active(char *err, size_t n) {
  if(ps5tm_gamecopy_busy() || ps5tm_gameconvert_busy() || ps5tm_gamemove_busy() || ps5tm_saves_busy() || ps5tm_gamedelete_busy()) {
    snprintf(err, n, "Es läuft gerade eine Kopie, Konvertierung, ein Verschieben, eine Sicherung oder ein Löschen. Das läuft nicht gleichzeitig mit dem Teilen eines Pakets.");
    return 1;
  }
  if(ps5tm_pkginst_busy()) {
    snprintf(err, n, "Es wird gerade ein Paket installiert. Das läuft nicht gleichzeitig mit dem Teilen eines Pakets.");
    return 1;
  }
  return 0;
}

static uint64_t
free_bytes_at(const char *dir) {
#ifdef PS5TM_HOST_TEST
  const char *e = getenv("PS5TM_PKG_FREE");
  if(e && *e) return strtoull(e, NULL, 10);
#endif
  struct statvfs v;
  if(statvfs(dir, &v) != 0) return UINT64_MAX;                 /* cannot tell: the writes will say */
  return (uint64_t)v.f_bavail * (uint64_t)v.f_frsize;
}

static uint64_t
min_part(void) {
#ifdef PS5TM_HOST_TEST
  const char *e = getenv("PS5TM_PKG_MINPART");                 /* the tests cut small packages into small parts */
  if(e && *e) return strtoull(e, NULL, 10);
#endif
  return SP_MIN_PART;
}

/* The status of an HTTP answer: 200 started; 400 bad request; 404 no such package or drive; 409 cannot be done now. */
int
ps5tm_pkgsplit_start(const char *id, const char *target_mount, uint64_t part_bytes, char *err, size_t err_len) {
  err[0] = 0;
  if(part_bytes < min_part() || part_bytes > SP_MAX_PART) {
    snprintf(err, err_len, "Die Größe eines Teils muss zwischen 64 MB und 64 GB liegen.");
    return 400;
  }
  ps5tm_pkg_t pkg;
  if(ps5tm_pkgscan_find(id, &pkg) != 0) {
    snprintf(err, err_len, "Dieses Paket kennt die letzte Suche nicht (mehr). Bitte noch einmal suchen.");
    return 404;
  }
  if(pkg.parts != 0) {
    snprintf(err, err_len, "Das ist schon ein Teil eines geteilten Pakets.");
    return 409;
  }
  if(other_job_active(err, err_len)) return 409;

  ps5tm_pkgdrive_t dr[PS5TM_MAX_VOLUMES];
  unsigned nd = ps5tm_pkg_drives(dr, PS5TM_MAX_VOLUMES), di = nd;
  for(unsigned i = 0; target_mount && i < nd; i++) if(!strcmp(dr[i].mount, target_mount)) di = i;
  if(di == nd) {
    snprintf(err, err_len, "Dieses Ziel gibt es an der Konsole gerade nicht.");
    return 404;
  }

  args_t *a = calloc(1, sizeof(*a));
  if(!a) { snprintf(err, err_len, "Kein Speicher."); return 409; }
  a->pkg = pkg;
  a->part_bytes = part_bytes;
  snprintf(a->base, sizeof(a->base), "%s", pkg.file);
  if(snprintf(a->dir, sizeof(a->dir), "%s/pkg", dr[di].base) >= (int)sizeof(a->dir)) { free(a); snprintf(err, err_len, "Der Pfad ist zu lang."); return 400; }

  uint32_t icon_n = 0;
  if(pkg.icon_size && pkg.icon_size < (10u << 20)) icon_n = pkg.icon_size;
  unsigned parts = parts_needed(a, icon_n);
  if(parts < 2) {
    free(a);
    snprintf(err, err_len, "Das Paket ist nicht größer als ein Teil (%.1f GB); es muss nicht geteilt werden.", (double)part_bytes / 1073741824.0);
    return 409;
  }
  if(parts > SP_MAX_PARTS) {
    free(a);
    snprintf(err, err_len, "Mit dieser Teilgröße ergeben sich mehr als %u Teile. Bitte eine größere Teilgröße wählen.", SP_MAX_PARTS);
    return 400;
  }

  /* the folder, and no part of that name there already */
  struct stat st;
  if(mkdir(a->dir, 0755) != 0 && errno != EEXIST) { snprintf(err, err_len, "Der Ordner pkg ließ sich auf dem Ziel nicht anlegen: %s", ps5tm_io_strerror(errno)); free(a); return 409; }
  if(stat(a->dir, &st) != 0 || !S_ISDIR(st.st_mode)) { snprintf(err, err_len, "Auf dem Ziel ist pkg kein Ordner."); free(a); return 409; }
  for(unsigned k = 1; k <= parts + 8; k++) {
    char tmp[PS5TM_PKG_PATH + 32], fin[PS5TM_PKG_PATH + 32];
    part_names(a, k, tmp, sizeof(tmp), fin, sizeof(fin));
    if(access(fin, F_OK) == 0) {
      snprintf(err, err_len, "Im Ordner pkg des Ziels gibt es schon Teile dieses Pakets (%s.part%u). Bitte zuerst am PC oder per FTP entfernen; die App überschreibt nichts.", a->base, k);
      free(a);
      return 409;
    }
    if(access(tmp, F_OK) == 0) {
      snprintf(err, err_len, "Im Ordner pkg des Ziels liegt noch ein Rest eines früheren Aufteilens (%s.part%u" SP_TMP_SUFFIX "). Bitte am PC oder per FTP entfernen; die App überschreibt nichts.", a->base, k);
      free(a);
      return 409;
    }
  }
  uint64_t need = (uint64_t)parts * SP_HEADER + icon_n + pkg.size + SP_RESERVE;
  uint64_t avail = free_bytes_at(a->dir);
  if(avail != UINT64_MAX && avail < need) {
    snprintf(err, err_len, "Auf dem Ziel ist nicht genug Platz: nötig %llu MB, frei %llu MB.", (unsigned long long)(need >> 20), (unsigned long long)(avail >> 20));
    free(a);
    return 409;
  }
  {
    char why[96];
    a->parallel = ps5tm_io_parallel(pkg.path, a->dir, why, sizeof(why));
  }

  pthread_mutex_lock(&g_lock);
  if(active_locked()) {
    pthread_mutex_unlock(&g_lock);
    snprintf(err, err_len, "Es läuft schon ein Vorgang mit einem Paket.");
    free(a);
    return 409;
  }
  memset(&g_job, 0, sizeof(g_job));
  g_job.state = J_SPLIT;
  snprintf(g_job.phase, sizeof(g_job.phase), "Vorbereiten");
  snprintf(g_job.name, sizeof(g_job.name), "%s", pkg.name);
  snprintf(g_job.file, sizeof(g_job.file), "%s", pkg.file);
  snprintf(g_job.dir, sizeof(g_job.dir), "%s", a->dir);
  g_job.parts = parts;
  g_job.bytes_total = 2 * pkg.size;
  g_job.started_ms = wall_ms();
  __atomic_store_n(&g_cancel, 0, __ATOMIC_RELEASE);
  pthread_mutex_unlock(&g_lock);

  /* The question at the top and this claim are not one step (the drive may have taken seconds to answer in between): a
     job of another kind may have started on the way. The others ask for this one before they claim their own, so of two
     that meet at least one gives way. No lock of this file is held while asking. */
  if(other_job_active(err, err_len)) {
    pthread_mutex_lock(&g_lock);
    memset(&g_job, 0, sizeof(g_job));                      /* idle again */
    pthread_mutex_unlock(&g_lock);
    free(a);
    return 409;
  }

  pthread_t th;
  pthread_attr_t at;
  pthread_attr_init(&at);
  pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
  pthread_attr_setstacksize(&at, 256 * 1024);
  int rc = pthread_create(&th, &at, split_main, a);
  pthread_attr_destroy(&at);
  if(rc != 0) {
    pthread_mutex_lock(&g_lock);
    g_job.state = J_IDLE;
    pthread_mutex_unlock(&g_lock);
    free(a);
    snprintf(err, err_len, "Der Vorgang ließ sich nicht starten.");
    return 409;
  }
  return 200;
}

cJSON *
ps5tm_pkgsplit_job_json(void) {
  cJSON *o = cJSON_CreateObject();
  if(!o) return NULL;
  pthread_mutex_lock(&g_lock);
  int act = active_locked();
  uint64_t now = wall_ms();
  cJSON_AddBoolToObject(o, "ok", 1);
  cJSON_AddStringToObject(o, "state", k_state[g_job.state]);
  cJSON_AddBoolToObject(o, "active", act);
  cJSON_AddBoolToObject(o, "can_cancel", act);
  cJSON_AddStringToObject(o, "phase", g_job.phase);
  cJSON_AddStringToObject(o, "name", g_job.name);
  cJSON_AddStringToObject(o, "file", g_job.file);
  cJSON_AddStringToObject(o, "dir", g_job.dir);
  cJSON_AddNumberToObject(o, "parts", g_job.parts);
  cJSON_AddNumberToObject(o, "part", g_job.part);
  cJSON_AddNumberToObject(o, "bytes_total", (double)g_job.bytes_total);
  cJSON_AddNumberToObject(o, "bytes_done", (double)g_job.bytes_done);
  cJSON_AddNumberToObject(o, "percent", g_job.bytes_total ? (g_job.state == J_DONE ? 100 : (double)(g_job.bytes_done * 100 / g_job.bytes_total)) : 0);
  cJSON_AddStringToObject(o, "error", g_job.error);
  cJSON_AddStringToObject(o, "note", g_job.note);
  cJSON_AddStringToObject(o, "sha256", g_job.sha);
  cJSON_AddNumberToObject(o, "elapsed_s", g_job.started_ms ? (double)(((act ? now : g_job.finished_ms) - g_job.started_ms) / 1000) : 0);
  cJSON_AddNumberToObject(o, "finished_ago_s", g_job.finished_ms ? (double)((now - g_job.finished_ms) / 1000) : -1);
  pthread_mutex_unlock(&g_lock);
  return o;
}
