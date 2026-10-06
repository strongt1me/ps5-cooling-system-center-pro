/* Checking what a copy or a conversion wrote, and the checksum files next to
 * a backup (03.10.2026).
 *
 * The idea comes from PS5 Game Compressor (Juma Sayeh), which keeps a SHA-256
 * per 64 KiB block of every image it writes and checks against it later. Its
 * code has no licence and none of it is used; this is this app's own take:
 *
 *   - while writing, the copy or conversion keeps a CRC-32 of the data it
 *     read from the original (libdeflate, PCLMULQDQ: costs next to nothing)
 *   - afterwards it reads what it wrote back from the drive and compares —
 *     every byte, not a sample
 *   - on that second read every byte also goes through SHA-256, and the
 *     result lands next to the backup as "<name>.sha256" in the format of
 *     sha256sum: a PC checks the backup with sha256sum -c, 7-Zip or similar,
 *     today or in two years
 *
 * SHA-256 is only computed on the read-back, where the drive is the limit
 * anyway; while copying, the one CPU thread that reads and writes would be
 * slowed by it (about 230 MB/s in plain C). The CRC-32 taken while copying is
 * enough to prove that what was written equals what was read.
 *
 * Reading back is only worth something if it reaches the drive. The cached
 * pages are dropped first where the system offers that (posix_fadvise); for
 * a game bigger than the console's memory most of it comes from the drive
 * either way. */

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "checkfile.h"
#include "ioerr.h"
#include "sha256.h"
#include "third_party/libdeflate/libdeflate.h"

#define READ_CHUNK (1u << 20)

void
ps5tm_drop_cache(int fd) {
  /* FreeBSD's system call 531, wrapped by the SDK's libc. A kernel or file
     system without it answers with an error, and nothing changes. */
  if(fd >= 0) (void)posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED);
}

int
ps5tm_digest_fd(int fd, uint64_t size, uint32_t *crc, uint8_t sha[32],
                const int *cancel, void (*progress)(void *ctx, uint64_t done),
                void *progress_ctx, char *err, size_t err_len) {
  /* A game folder has tens of thousands of small files: they get a buffer of
     their own size, not a megabyte each. */
  size_t bufsz = size < READ_CHUNK ? (size ? (size_t)size : 1) : READ_CHUNK;
  uint8_t *buf = malloc(bufsz);
  if(!buf) { snprintf(err, err_len, "Kein Speicher."); return -1; }
  ps5tm_sha256_t s;
  ps5tm_sha256_init(&s);
  uint32_t c = 0;
  uint64_t pos = 0;
  int rc = 0;
  while(pos < size) {
    if(cancel && __atomic_load_n(cancel, __ATOMIC_ACQUIRE)) {
      snprintf(err, err_len, "Abgebrochen.");
      errno = ECANCELED;
      rc = -1;
      break;
    }
    size_t want = size - pos < bufsz ? (size_t)(size - pos) : bufsz;
    ssize_t r = pread(fd, buf, want, (off_t)pos);
    if(r < 0 && errno == EINTR) continue;
    if(r <= 0) {
      if(r < 0) {
        snprintf(err, err_len, "Lesefehler beim Prüfen: %s", ps5tm_io_strerror(errno));
      } else {
        snprintf(err, err_len, "Die Datei ist beim Prüfen kürzer als geschrieben.");
        errno = EIO;
      }
      rc = -1;
      break;
    }
    if(crc) c = libdeflate_crc32(c, buf, (size_t)r);
    if(sha) ps5tm_sha256_update(&s, buf, (size_t)r);
    pos += (uint64_t)r;
    if(progress) progress(progress_ctx, pos);
    sched_yield();          /* the fan and the web server come first */
  }
  free(buf);
  if(rc) return -1;
  if(crc) *crc = c;
  if(sha) ps5tm_sha256_final(&s, sha);
  return 0;
}


/* --------------------------------------------------------- checksum files */

#define SUMS_BUF 65536

struct ps5tm_sums {
  int    fd;
  int    failed;                 /* a write failed; the close says so */
  int    err;                    /* its errno */
  char   path[PATH_MAX];
  char   tmp[PATH_MAX];
  char   buf[SUMS_BUF];
  size_t len;
};

static int
sums_flush(ps5tm_sums_t *s) {
  const char *p = s->buf;
  size_t left = s->len;
  while(left && !s->failed) {
    ssize_t w = write(s->fd, p, left);
    if(w < 0 && errno == EINTR) continue;
    if(w <= 0) { s->failed = 1; s->err = w < 0 ? errno : EIO; break; }
    p += w;
    left -= (size_t)w;
  }
  s->len = 0;
  return s->failed ? -1 : 0;
}

static int
sums_put(ps5tm_sums_t *s, const char *p, size_t n) {
  if(s->failed) return -1;
  if(s->len + n > SUMS_BUF && sums_flush(s) != 0) return -1;
  memcpy(s->buf + s->len, p, n);
  s->len += n;
  return 0;
}

ps5tm_sums_t *
ps5tm_sums_open(const char *path, char *err, size_t err_len) {
  ps5tm_sums_t *s = calloc(1, sizeof(*s));
  if(!s) { snprintf(err, err_len, "Kein Speicher."); return NULL; }
  if(snprintf(s->path, sizeof(s->path), "%s", path) >= (int)sizeof(s->path) ||
     snprintf(s->tmp, sizeof(s->tmp), "%s.ps5cc-teil", path) >= (int)sizeof(s->tmp)) {
    snprintf(err, err_len, "Der Pfad der Prüfsummen-Datei ist zu lang.");
    free(s);
    return NULL;
  }
  unlink(s->tmp);                      /* this app's own leftover of a failed try */
  s->fd = open(s->tmp, O_WRONLY | O_CREAT | O_EXCL, 0666);
  if(s->fd < 0) {
    snprintf(err, err_len, "Die Prüfsummen-Datei ließ sich nicht anlegen: %s", ps5tm_io_strerror(errno));
    free(s);
    return NULL;
  }
  return s;
}

int
ps5tm_sums_add(ps5tm_sums_t *s, const uint8_t sha[32], const char *name) {
  if(!s) return -1;
  /* sha256sum's own rule: a name with a backslash or a line break gets them
     escaped, and the line a leading backslash that says so. */
  int esc = strchr(name, '\\') || strchr(name, '\n');
  char hex[65];
  ps5tm_sha256_hex(sha, hex);
  if(esc && sums_put(s, "\\", 1) != 0) return -1;
  if(sums_put(s, hex, 64) != 0 || sums_put(s, "  ", 2) != 0) return -1;
  for(const char *p = name; *p; p++) {
    int r = *p == '\\' ? sums_put(s, "\\\\", 2)
          : *p == '\n' ? sums_put(s, "\\n", 2)
          : sums_put(s, p, 1);
    if(r != 0) return -1;
  }
  return sums_put(s, "\n", 1);
}

int
ps5tm_sums_close(ps5tm_sums_t *s, char *err, size_t err_len) {
  if(!s) return -1;
  int rc = sums_flush(s);
  int e  = s->err;
  if(!rc && fsync(s->fd) != 0) { rc = -1; e = errno; }
  if(close(s->fd) != 0 && !rc) { rc = -1; e = errno; }
  if(!rc && rename(s->tmp, s->path) != 0) { rc = -1; e = errno; }
  if(rc) {
    unlink(s->tmp);
    snprintf(err, err_len, "Die Prüfsummen-Datei ließ sich nicht schreiben: %s", ps5tm_io_strerror(e));
  }
  free(s);
  return rc ? -1 : 0;
}

void
ps5tm_sums_abort(ps5tm_sums_t *s) {
  if(!s) return;
  close(s->fd);
  unlink(s->tmp);
  free(s);
}
