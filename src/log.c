/* In-memory event ring buffer backing /api/v1/logs, mirrored to app.log on /data. */

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "ps5tm.h"

#ifndef PS5TM_HOST_TEST
/* klog_printf() and friends live in the SDK's crt1.o, which every payload
   links anyway — no extra -l needed. Verified with
   `nm --defined-only /opt/ps5-payload-sdk/target/lib/crt1.o | grep klog`. */
#include <ps5/klog.h>
#endif

static ps5tm_log_entry_t g_entries[PS5TM_LOG_CAPACITY];
static unsigned          g_count;   /* entries written, saturates at capacity */
static unsigned          g_head;    /* next slot to write                     */
static pthread_mutex_t   g_lock = PTHREAD_MUTEX_INITIALIZER;


uint64_t
ps5tm_now_ms(void) {
  struct timespec ts;
  if(clock_gettime(CLOCK_REALTIME, &ts) != 0) return 0;
  return (uint64_t)ts.tv_sec * 1000ull + (uint64_t)(ts.tv_nsec / 1000000);
}


uint64_t
ps5tm_mono_ms(void) {
  struct timespec ts;
  if(clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
  return (uint64_t)ts.tv_sec * 1000ull + (uint64_t)(ts.tv_nsec / 1000000);
}


/* ----------------------------------------------------------- stdout mirror
 *
 * stdout is the socket the payload was sent over. A sender that stays
 * connected but stops reading eventually fills its send buffer, and from then
 * on a plain printf() + fflush() blocks — in whichever thread happens to log,
 * the fan thread included, with stdio's lock held so that every other logging
 * thread queues up behind it. Two defences:
 *
 *   - the socket gets a send timeout, once, so that no write waits longer than
 *     OUT_TIMEOUT_MS;
 *   - the first write that fails or comes up short switches the mirror off for
 *     OUT_MUTE_MS. The klog copy and the in-memory ring, which the web UI
 *     shows, are not touched by any of this, and one line says what happened.
 *
 * The line leaves with a single write() of its own instead of going through
 * stdio. A FILE keeps what it could not write and offers it again on every
 * later call, so a peer that has stopped reading would be paid for once per
 * log line instead of once per episode.
 *
 * Only a socket is treated this way. A pipe, a file or a terminal behaves as
 * before: nothing here can tell a reader that is merely slow from one that is
 * gone, and a payload's stdout has only ever been the socket it came in on. */
#define OUT_TIMEOUT_MS 500
#define OUT_MUTE_MS    60000ull

static pthread_mutex_t g_out_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_once_t  g_out_once = PTHREAD_ONCE_INIT;
static int             g_out_muted;        /* the next three: g_out_lock       */
static uint64_t        g_out_mute_since;
static int             g_out_failing;      /* the last write failed            */

static void
out_setup(void) {
  int       type = 0;
  socklen_t len  = sizeof(type);
  if(getsockopt(STDOUT_FILENO, SOL_SOCKET, SO_TYPE, &type, &len) != 0)
    return;                                   /* not a socket */

  struct timeval tv;
  tv.tv_sec  = OUT_TIMEOUT_MS / 1000;
  tv.tv_usec = (OUT_TIMEOUT_MS % 1000) * 1000;
  setsockopt(STDOUT_FILENO, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}

enum { OUT_NOTHING = 0, OUT_FAILED = 1, OUT_RECOVERED = 2 };

/* Writes one line to stdout unless the mirror is switched off. Returns
   OUT_FAILED for the write that switched it off — the first of a streak, so
   the caller says so once — and OUT_RECOVERED for the first one that went
   through again; *err then holds errno of the failure. */
static int
out_mirror(const char *line, size_t len, int *err) {
  pthread_once(&g_out_once, out_setup);

  pthread_mutex_lock(&g_out_lock);
  uint64_t now = ps5tm_mono_ms();
  if(g_out_muted) {
    if(now && now - g_out_mute_since < OUT_MUTE_MS) {
      pthread_mutex_unlock(&g_out_lock);
      return OUT_NOTHING;
    }
    g_out_muted = 0;                          /* time is up: try again */
  }

#ifdef PS5TM_HOST_TEST
  fflush(stdout);                             /* keep the order with printf()s */
#endif
  ssize_t w;
  do {
    w = write(STDOUT_FILENO, line, len);
  } while(w < 0 && errno == EINTR);

  int rc = OUT_NOTHING;
  if(w != (ssize_t)len) {
    *err = (w < 0) ? errno : EIO;
    g_out_muted      = (now != 0);            /* no clock, no way to time it  */
    g_out_mute_since = now;
    if(!g_out_failing) {
      g_out_failing = 1;
      rc = OUT_FAILED;
    }
  } else if(g_out_failing) {
    g_out_failing = 0;
    rc = OUT_RECOVERED;
  }
  pthread_mutex_unlock(&g_out_lock);
  return rc;
}


static uint64_t        g_seq;     /* entries ever written (g_lock); the file writer counts against it */
static pthread_cond_t  g_file_wake = PTHREAD_COND_INITIALIZER;

static void
ring_add(const ps5tm_log_entry_t *e) {
  pthread_mutex_lock(&g_lock);
  g_entries[g_head] = *e;
  g_head = (g_head + 1) % PS5TM_LOG_CAPACITY;
  if(g_count < PS5TM_LOG_CAPACITY) g_count++;
  g_seq++;
  pthread_mutex_unlock(&g_lock);
  /* A warning or an error goes to the file at once, not with the next batch: it is the line most likely to be
     followed by a crash. No mutex around the signal; a wake-up lost that way costs at most one batch interval. */
  if(e->level[0] == 'W' || e->level[0] == 'E') pthread_cond_signal(&g_file_wake);
}


/* A line about the mirror itself: klog and the ring, never the mirror. */
static void
log_note(const char *level, const char *code, const char *message) {
  ps5tm_log_entry_t e;
  memset(&e, 0, sizeof(e));
  e.timestamp_ms = ps5tm_now_ms();
  snprintf(e.level,   sizeof(e.level),   "%s", level);
  snprintf(e.code,    sizeof(e.code),    "%s", code);
  snprintf(e.message, sizeof(e.message), "%s", message);
#ifndef PS5TM_HOST_TEST
  klog_printf("[%s] %s: %s\n", e.level, e.code, e.message);
#endif
  ring_add(&e);
}


void
ps5tm_log_init(void) {
  pthread_mutex_lock(&g_lock);
  g_count = 0;
  g_head  = 0;
  pthread_mutex_unlock(&g_lock);
}


void
ps5tm_log(const char *level, const char *code, const char *fmt, ...) {
  ps5tm_log_entry_t e;
  memset(&e, 0, sizeof(e));
  e.timestamp_ms = ps5tm_now_ms();
  snprintf(e.level, sizeof(e.level), "%s", level ? level : "INFO");
  snprintf(e.code,  sizeof(e.code),  "%s", code  ? code  : "event");

  va_list ap;
  va_start(ap, fmt);
  vsnprintf(e.message, sizeof(e.message), fmt, ap);
  va_end(ap);
  /* An entry is one line, whatever it was given: a name or a path with a line break in it would otherwise become a
     second line in the kernel log, without the process tag in front, where it could pass for a message of the console
     (see klogown.h). Control characters become blanks, here and so in the ring the page shows as well. */
  for(char *c = e.message; *c; c++)
    if((unsigned char)*c < 0x20 || *c == 0x7f) *c = ' ';

  /* Two mirrors, and the second one is the point.
   *
   * stdout is the socket the payload was sent over. Against a sender that
   * transfers the ELF and hangs up, it is dead from the first line onwards —
   * writes fail with EPIPE (harmless, SIGPIPE is ignored in main()) and every
   * word is lost. That is exactly the state that made the app look like it
   * "does nothing" and cost an evening of guessing on 01.08.2026.
   *
   * klog writes into the kernel message buffer instead, which no hang-up can
   * cut. Read it from a PC with the klogsrv payload running:
   *
   *     nc <ps5-ip> 3232
   *
   * The comment here used to claim klog was covered while only calling
   * printf(). It is covered now.
   *
   * And the first mirror must not be able to take the second one down with
   * it: see out_mirror() for what happens when the sender stops reading. */
  char line[512];
  int  n = snprintf(line, sizeof(line), "[%s] %s: %s\n",
                    e.level, e.code, e.message);
  if(n < 0) n = 0;
  if((size_t)n >= sizeof(line)) {
    /* Too long: cut at a character border (not inside a UTF-8 sequence) and keep the line break, or the next line of the
       mirror is glued to this one. */
    n = (int)sizeof(line) - 2;
    while(n > 0 && ((unsigned char)line[n - 1] & 0xC0) == 0x80) n--;      /* continuation bytes of a cut character */
    if(n > 0 && ((unsigned char)line[n - 1] & 0x80)) n--;                  /* and its lead byte */
    line[n++] = '\n';
    line[n] = 0;
  }
  int err = 0;
  int out = out_mirror(line, (size_t)n, &err);

#ifndef PS5TM_HOST_TEST
  /* No app name here on purpose: the kernel already prefixes every klog line
     with the process name, which is exactly what we would have written.
     Our lines arrive as

       <118>[ps5tm.elf] [INFO] ready: Web-UI erreichbar unter …

     That name comes from the single SYS_thr_set_name call in main().
     ⚠ It is the *process* name, not the thread's — an earlier version of this
     comment claimed the prefix told the threads apart, and it does not. The
     fan worker used to rename itself, which renamed the whole app, and from
     then on even the main thread's lines read "[ps5tm-fan]". */
  klog_printf("[%s] %s: %s\n", e.level, e.code, e.message);
#endif

  /* g_lock is taken for the ring alone, never across a write to stdout. */
  ring_add(&e);

  if(out == OUT_FAILED) {
    char msg[192];
    snprintf(msg, sizeof(msg),
             "Die Sendeverbindung nimmt keine Ausgabe mehr an (Fehler %d: %s) "
             "– die Spiegelung dorthin ruht %d s. Protokoll und Kernel-Log "
             "laufen weiter.", err, strerror(err), (int)(OUT_MUTE_MS / 1000));
    log_note("WARN", "stdout_stalled", msg);
  } else if(out == OUT_RECOVERED) {
    log_note("INFO", "stdout_resumed",
             "Die Sendeverbindung nimmt wieder Ausgabe an – die Spiegelung "
             "ist wieder aktiv.");
  }
}


unsigned
ps5tm_log_snapshot(ps5tm_log_entry_t *out, unsigned max) {
  pthread_mutex_lock(&g_lock);

  unsigned n = g_count < max ? g_count : max;
  /* Oldest first. When the buffer has wrapped, the oldest entry sits at
     g_head; otherwise the buffer starts at 0. */
  unsigned start = (g_count == PS5TM_LOG_CAPACITY)
                     ? (g_head + (g_count - n)) % PS5TM_LOG_CAPACITY
                     : (g_count - n);
  for(unsigned i = 0; i < n; i++) {
    out[i] = g_entries[(start + i) % PS5TM_LOG_CAPACITY];
  }

  pthread_mutex_unlock(&g_lock);
  return n;
}


/* ------------------------------------------------------------ file mirror
 *
 * The ring above is all the pages show, and it dies with the process: after
 * the console restarted on 03.11.2026 nothing told what the app had done
 * before. So the same entries also go to PS5TM_APPLOG_PATH, appended by a
 * thread of their own. ps5tm_log() itself never touches the disk: it is
 * called from the fan worker, which must not wait for a write or an fsync.
 *
 * The thread takes what is new in the ring every APPLOG_BATCH_MS (at once
 * for a warning or an error), writes it as text, and syncs it, so that a
 * hard reset loses at most that interval. Lines that rolled out of the ring
 * before they were written are counted in a line of their own. Past
 * APPLOG_MAX_BYTES the file becomes app.old.log and a new one starts. */
#define APPLOG_BATCH_MS  2000
#define APPLOG_MAX_BYTES (1024 * 1024)

/* Entries from number `from` on, oldest first; *lost is how many of them the ring no longer holds. Returns the count. */
static unsigned
snapshot_since(uint64_t from, ps5tm_log_entry_t *out, uint64_t *next, uint64_t *lost) {
  pthread_mutex_lock(&g_lock);
  uint64_t oldest = g_seq - g_count;          /* number of the oldest entry still in the ring */
  *lost = from < oldest ? oldest - from : 0;
  if(from < oldest) from = oldest;
  unsigned n = (unsigned)(g_seq - from);
  unsigned start = (g_head + PS5TM_LOG_CAPACITY - n) % PS5TM_LOG_CAPACITY;
  for(unsigned i = 0; i < n; i++) out[i] = g_entries[(start + i) % PS5TM_LOG_CAPACITY];
  *next = g_seq;
  pthread_mutex_unlock(&g_lock);
  return n;
}

static int
applog_open(void) {
  struct stat st;
  if(!stat(PS5TM_APPLOG_PATH, &st) && st.st_size > APPLOG_MAX_BYTES)
    rename(PS5TM_APPLOG_PATH, PS5TM_APPLOG_OLD_PATH);
  return open(PS5TM_APPLOG_PATH, O_WRONLY | O_CREAT | O_APPEND | O_NOFOLLOW, 0666);
}

/* The console's clock as the pages show it: local time once the registry has told the offset, UTC (with a Z) until then. */
static void
applog_stamp(uint64_t ms, char *out, size_t n) {
  ps5tm_regstats_t rs;
  ps5tm_regstats_get(&rs);
  time_t secs = (time_t)(ms / 1000) + (rs.local_offset_valid ? (time_t)rs.local_offset_min * 60 : 0);
  struct tm tmv;
  snprintf(out, n, "0000-00-00 00:00:00");
  if(gmtime_r(&secs, &tmv)) strftime(out, n, rs.local_offset_valid ? "%Y-%m-%d %H:%M:%S" : "%Y-%m-%d %H:%M:%SZ", &tmv);
}

static void
applog_write(int fd, const char *s, size_t len) {
  while(len) {
    ssize_t w = write(fd, s, len);
    if(w < 0 && errno == EINTR) continue;
    if(w <= 0) return;                        /* disk full or gone: drop it, the ring still has it */
    s += w;
    len -= (size_t)w;
  }
}

static void *
applog_worker(void *arg) {
  (void)arg;
  ps5tm_log_entry_t *batch = malloc(sizeof(*batch) * PS5TM_LOG_CAPACITY);   /* ~115 KB: never on the thread stack */
  char              *buf   = malloc(PS5TM_LOG_CAPACITY * 512 + 256);
  if(!batch || !buf) { free(batch); free(buf); return NULL; }

  uint64_t next = 0;
  int      fd   = applog_open();
  if(fd >= 0) {
    char head[160], stamp[32];
    applog_stamp(ps5tm_now_ms(), stamp, sizeof(stamp));
    int k = snprintf(head, sizeof(head), "\n===== %s  " PS5TM_APP_NAME " " PS5TM_VERSION " gestartet =====\n", stamp);
    applog_write(fd, head, (size_t)k);
  }

  for(;;) {
    pthread_mutex_lock(&g_lock);
    if(g_seq == next) {
      struct timespec until;
      clock_gettime(CLOCK_REALTIME, &until);
      until.tv_sec  += APPLOG_BATCH_MS / 1000;
      until.tv_nsec += (APPLOG_BATCH_MS % 1000) * 1000000L;
      if(until.tv_nsec >= 1000000000L) { until.tv_sec++; until.tv_nsec -= 1000000000L; }
      pthread_cond_timedwait(&g_file_wake, &g_lock, &until);
    }
    pthread_mutex_unlock(&g_lock);

    uint64_t lost = 0;
    unsigned n = snapshot_since(next, batch, &next, &lost);
    if(!n && !lost) continue;

    if(fd < 0) fd = applog_open();            /* /data was not ready yet, or the file was removed */
    if(fd < 0) continue;

    size_t o = 0;
    char   stamp[32];
    if(lost)
      o += (size_t)snprintf(buf + o, 256, "… %llu Einträge kamen zu schnell und fehlen hier (im Kernel-Log stehen sie).\n",
                            (unsigned long long)lost);
    for(unsigned i = 0; i < n; i++) {
      applog_stamp(batch[i].timestamp_ms, stamp, sizeof(stamp));
      o += (size_t)snprintf(buf + o, 512, "%s  %-5s  %-28s  %s\n", stamp, batch[i].level, batch[i].code, batch[i].message);
    }
    applog_write(fd, buf, o);
    fsync(fd);

    struct stat st;
    if(!fstat(fd, &st) && st.st_size > APPLOG_MAX_BYTES) {
      close(fd);
      fd = applog_open();
    }
  }
  return NULL;
}

void
ps5tm_log_file_start(void) {
  pthread_attr_t attr;
  pthread_t      t;
  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
  if(pthread_create(&t, &attr, applog_worker, NULL) != 0)
    PS5TM_WARN("applog_thread", "Die Protokolldatei %s wird nicht geschrieben (Faden startet nicht).", PS5TM_APPLOG_PATH);
  pthread_attr_destroy(&attr);
}
