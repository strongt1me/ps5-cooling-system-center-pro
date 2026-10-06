/* Saved and recorded kernel logs, for the page's Klog tab (04.10.2026).
 *
 * One folder, /data/PS5-Cooling-Center/klog-live-log, holds two kinds of plain
 * text files, a kernel line to a line, "YYYY-MM-DD HH:MM:SS  <line>":
 *
 *   klog-<stamp>.log      what the page showed when "Speichern" was pressed. The
 *                         page sends the text; api.c writes it with the receiver
 *                         it has for raw bodies, into a file made here.
 *   aufnahme-<stamp>.log  a recording. This app reads the kernel log itself once a
 *                         second, as the page does (ps5tm_klog_fetch), and appends
 *                         the new lines. It goes on with the page closed or
 *                         reloaded, needs no memory in the browser, and what was
 *                         written is on the drive when the console goes down.
 *
 * Times are those of the page's own time zone, which it sends along (tz, minutes
 * east of UTC): the console's registry gives an offset, but what it means is not
 * known well enough to print it as a clock. Without tz the files are in UTC and
 * say so.
 *
 * A recording stops by itself at KLR_MAX_BYTES, when the drive runs low, or when
 * a write fails, and says why. Clearing the folder removes only files that bear
 * the names above, never the recording that is being written, never a folder.
 *
 * The log lines of this module go to the kernel's message buffer like all of
 * ours, so a recording shows its own start and end. They name the file, which
 * has no characters outside A-Z a-z 0-9 - _ . */

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <time.h>
#include <unistd.h>

#include "ps5tm.h"
#include "third_party/cJSON.h"

#ifndef PS5TM_KLOG_DIR
#define PS5TM_KLOG_DIR PS5TM_DATA_DIR "/klog-live-log"
#endif

#define KLR_MAX_BYTES   (64ull << 20)   /* one recording ends here                */
#define KLR_MIN_FREE    (512ull << 20)  /* no recording starts on less than this  */
#define KLR_STOP_FREE   (128ull << 20)  /* and none goes on on less than this     */
#define KLR_BATCH       200             /* lines per read                         */
#define KLR_CONTEXT     200             /* lines before the start, for context    */
#define KLR_LIST_MAX    200             /* files the listing names                */

/* The limits, which a host build can move by the environment: a test cannot write 64 MB or
   fill a drive on request. On the console they are the constants. */
static uint64_t
limit(const char *env, uint64_t dflt) {
#ifdef PS5TM_HOST_TEST
  const char *e = getenv(env);
  if(e && *e) return strtoull(e, NULL, 10);
#else
  (void)env;
#endif
  return dflt;
}
#define LIM_MAX_BYTES  limit("PS5TM_KLR_MAX",      KLR_MAX_BYTES)
#define LIM_MIN_FREE   limit("PS5TM_KLR_MINFREE",  KLR_MIN_FREE)
#define LIM_STOP_FREE  limit("PS5TM_KLR_STOPFREE", KLR_STOP_FREE)

typedef struct {
  int      running;                     /* the thread is alive                    */
  int      stop;                        /* it is asked to end                     */
  char     name[64];
  int      tz_min;
  uint64_t started_ms, ended_ms;
  uint64_t bytes, lines;
  char     reason[96];                  /* why the last recording ended           */
} rec_t;

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static rec_t           g_rec;


/* ------------------------------------------------------------------ names */

/* "klog-…log" or "aufnahme-…log", the middle part made of A-Z a-z 0-9 - _ only:
   no dot (so no "..") and no slash, whatever the page sends. */
static int
name_ok(const char *n) {
  size_t len = strlen(n);
  const char *rest;
  if(len > 60) return 0;
  if(!strncmp(n, "klog-", 5))         rest = n + 5;
  else if(!strncmp(n, "aufnahme-", 9)) rest = n + 9;
  else return 0;
  if(len < 4 || strcmp(n + len - 4, ".log")) return 0;
  const char *end = n + len - 4;
  if(rest >= end) return 0;
  for(const char *p = rest; p < end; p++)
    if(!isalnum((unsigned char)*p) && *p != '-' && *p != '_') return 0;
  return 1;
}

static int
clamp_tz(int tz_min) {
  return tz_min < -840 ? -840 : tz_min > 840 ? 840 : tz_min;
}

static void
fmt_time(char *out, size_t n, uint64_t ms, int tz_min, int for_name) {
  time_t    t = (time_t)(ms / 1000) + (time_t)tz_min * 60;
  struct tm tmv;
  memset(&tmv, 0, sizeof(tmv));
  gmtime_r(&t, &tmv);
  snprintf(out, n, for_name ? "%04d-%02d-%02d_%02d-%02d-%02d" : "%04d-%02d-%02d %02d:%02d:%02d",
           tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday, tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
}

static void
fmt_zone(char *out, size_t n, int tz_min) {
  if(tz_min == 0) { snprintf(out, n, "UTC"); return; }
  int a = tz_min < 0 ? -tz_min : tz_min;
  snprintf(out, n, "UTC%c%02d:%02d", tz_min < 0 ? '-' : '+', a / 60, a % 60);
}


/* ------------------------------------------------------------------ files */

/* A new file of the given kind ("klog" or "aufnahme"), made with O_EXCL so that
   nothing is ever overwritten. Returns the descriptor, or -errno. */
int
ps5tm_klogfiles_create(const char *kind, int tz_min, char *name, size_t name_n) {
  mkdir(PS5TM_DATA_DIR, 0755);
  if(mkdir(PS5TM_KLOG_DIR, 0755) != 0 && errno != EEXIST) return -(errno ? errno : EIO);

  char st[32];
  fmt_time(st, sizeof(st), ps5tm_now_ms(), clamp_tz(tz_min), 1);
  for(int i = 0; i < 100; i++) {
    char n[64], p[PATH_MAX];
    if(i) snprintf(n, sizeof(n), "%s-%s-%d.log", kind, st, i + 1);
    else  snprintf(n, sizeof(n), "%s-%s.log", kind, st);
    snprintf(p, sizeof(p), "%s/%s", PS5TM_KLOG_DIR, n);
    int fd = open(p, O_WRONLY | O_CREAT | O_EXCL, 0644);
    if(fd >= 0) { snprintf(name, name_n, "%s", n); return fd; }
    if(errno != EEXIST) return -(errno ? errno : EIO);
  }
  return -EEXIST;
}

/* The path of one of our files, for a name the page gave. 0 and the path, or -1:
   not one of our names, or no such regular file. */
int
ps5tm_klogfiles_path(const char *name, char *out, size_t n) {
  if(!name || !name_ok(name)) return -1;
  snprintf(out, n, "%s/%s", PS5TM_KLOG_DIR, name);
  struct stat st;
  if(lstat(out, &st) != 0 || !S_ISREG(st.st_mode)) return -1;
  return 0;
}

/* The name of the file being recorded, "" when none. */
static void
active_name(char *out, size_t n) {
  pthread_mutex_lock(&g_lock);
  snprintf(out, n, "%s", g_rec.running ? g_rec.name : "");
  pthread_mutex_unlock(&g_lock);
}

typedef struct {
  char     name[64];
  uint64_t size;
  int64_t  mtime;
} fent_t;

static int
cmp_newest_first(const void *a, const void *b) {
  const fent_t *x = a, *y = b;
  if(x->mtime != y->mtime) return x->mtime < y->mtime ? 1 : -1;
  return strcmp(y->name, x->name);
}

/* Everything of ours in the folder, newest first, at most KLR_LIST_MAX named. */
static fent_t *
list_files(unsigned *n_out, uint64_t *bytes_out, unsigned *all_out) {
  *n_out = 0; *bytes_out = 0; *all_out = 0;
  DIR *d = opendir(PS5TM_KLOG_DIR);
  if(!d) return NULL;
  size_t  cap = 64, n = 0;
  fent_t *v = malloc(cap * sizeof(*v));
  if(!v) { closedir(d); return NULL; }
  struct dirent *e;
  while((e = readdir(d)) != NULL) {
    if(!name_ok(e->d_name)) continue;
    char p[PATH_MAX];
    struct stat st;
    snprintf(p, sizeof(p), "%s/%s", PS5TM_KLOG_DIR, e->d_name);
    if(lstat(p, &st) != 0 || !S_ISREG(st.st_mode)) continue;
    (*all_out)++;
    *bytes_out += (uint64_t)st.st_size;
    if(n == cap) {
      fent_t *g = realloc(v, cap * 2 * sizeof(*v));
      if(!g) continue;
      v = g; cap *= 2;
    }
    snprintf(v[n].name, sizeof(v[n].name), "%s", e->d_name);
    v[n].size  = (uint64_t)st.st_size;
    v[n].mtime = (int64_t)st.st_mtime;
    n++;
  }
  closedir(d);
  qsort(v, n, sizeof(*v), cmp_newest_first);
  *n_out = n > KLR_LIST_MAX ? KLR_LIST_MAX : (unsigned)n;
  return v;
}

cJSON *
ps5tm_klogfiles_json(void) {
  cJSON *root = cJSON_CreateObject();
  if(!root) return NULL;
  cJSON_AddBoolToObject(root, "ok", 1);
  cJSON_AddStringToObject(root, "dir", PS5TM_KLOG_DIR);

  char act[64];
  active_name(act, sizeof(act));
  unsigned n, all;
  uint64_t bytes;
  fent_t *v = list_files(&n, &bytes, &all);
  cJSON_AddNumberToObject(root, "count", all);
  cJSON_AddNumberToObject(root, "bytes", (double)bytes);
  cJSON *arr = cJSON_AddArrayToObject(root, "files");
  for(unsigned i = 0; i < n; i++) {
    cJSON *f = cJSON_CreateObject();
    cJSON_AddStringToObject(f, "name", v[i].name);
    cJSON_AddNumberToObject(f, "size", (double)v[i].size);
    cJSON_AddNumberToObject(f, "mtime", (double)v[i].mtime);
    cJSON_AddStringToObject(f, "kind", v[i].name[0] == 'a' ? "rec" : "save");
    cJSON_AddBoolToObject(f, "active", act[0] && !strcmp(act, v[i].name));
    cJSON_AddItemToArray(arr, f);
  }
  free(v);
  cJSON_AddItemToObject(root, "record", ps5tm_klogrec_json());
  return root;
}

/* Removes our files, except the one being recorded. Folders and anything with
   another name stay. 0, or an errno when the folder cannot be read. */
int
ps5tm_klogfiles_clear(unsigned *deleted, uint64_t *bytes, unsigned *kept) {
  *deleted = 0; *bytes = 0; *kept = 0;
  char act[64];
  active_name(act, sizeof(act));
  DIR *d = opendir(PS5TM_KLOG_DIR);
  if(!d) return errno == ENOENT ? 0 : (errno ? errno : EIO);
  struct dirent *e;
  while((e = readdir(d)) != NULL) {
    if(!name_ok(e->d_name)) continue;
    char p[PATH_MAX];
    struct stat st;
    snprintf(p, sizeof(p), "%s/%s", PS5TM_KLOG_DIR, e->d_name);
    if(lstat(p, &st) != 0 || !S_ISREG(st.st_mode)) continue;
    if(act[0] && !strcmp(act, e->d_name)) { (*kept)++; continue; }
    if(unlink(p) == 0) { (*deleted)++; *bytes += (uint64_t)st.st_size; }
    else (*kept)++;
  }
  closedir(d);
  PS5TM_INFO("klog_files_cleared", "Kernel-Log: %u Dateien aus klog-live-log gelöscht (%llu KB), %u bleiben.",
             *deleted, (unsigned long long)(*bytes >> 10), *kept);
  return 0;
}


/* -------------------------------------------------------------- recording */

static int
write_all(int fd, const char *p, size_t n) {
  while(n) {
    ssize_t w = write(fd, p, n);
    if(w > 0) { p += w; n -= (size_t)w; continue; }
    if(w < 0 && errno == EINTR) continue;
    return errno ? errno : EIO;
  }
  return 0;
}

static uint64_t
free_bytes(void) {
  struct statvfs sv;
  if(statvfs(PS5TM_KLOG_DIR, &sv) != 0) return UINT64_MAX;     /* unknown: do not block on it */
  uint64_t fr = sv.f_frsize ? sv.f_frsize : sv.f_bsize;
  return (uint64_t)sv.f_bavail * fr;
}

typedef struct { int fd; int tz_min; } rec_arg_t;

/* Formats a batch of lines into out (size cap) and writes it. Returns 0 or an errno. */
static int
write_lines(int fd, const ps5tm_klog_line_t *ln, unsigned n, int tz_min, char *out, size_t cap,
            uint64_t *bytes, uint64_t *lines) {
  size_t o = 0;
  int rc = 0;
  for(unsigned i = 0; i < n && !rc; i++) {
    char ts[32];
    fmt_time(ts, sizeof(ts), ln[i].t_ms, tz_min, 0);
    size_t need = strlen(ts) + 2 + strlen(ln[i].text) + 1;
    if(o + need > cap) {
      rc = write_all(fd, out, o);
      *bytes += o;
      o = 0;
      if(rc) break;
    }
    o += (size_t)snprintf(out + o, cap - o, "%s  %s\n", ts, ln[i].text);
    (*lines)++;
  }
  if(!rc && o) { rc = write_all(fd, out, o); *bytes += o; }
  return rc;
}

static void *
rec_main(void *arg) {
  rec_arg_t a = *(rec_arg_t *)arg;
  free(arg);

  const size_t       cap = 96 * 1024;                  /* KLR_BATCH lines of at most 400 bytes fit in two goes */
  ps5tm_klog_line_t *ln  = malloc(sizeof(*ln) * KLR_BATCH);
  char              *out = malloc(cap);
  char               reason[96] = "angehalten";
  uint64_t           bytes = 0, lines = 0;

  if(!ln || !out) {
    snprintf(reason, sizeof(reason), "Kein Speicher");
    goto done;
  }

  {
    char zone[16], ts[32], head[256];
    fmt_zone(zone, sizeof(zone), a.tz_min);
    fmt_time(ts, sizeof(ts), ps5tm_now_ms(), a.tz_min, 0);
    int hl = snprintf(head, sizeof(head),
                      "# PS5 Kernel-Log, Aufnahme gestartet %s (%s). Eine Zeile je Kernel-Zeile: Zeit, zwei Leerzeichen, Text.\n"
                      "# Die ersten Zeilen sind der Rückblick vor dem Start.\n", ts, zone);
    int rc = write_all(a.fd, head, (size_t)hl);
    if(rc) { snprintf(reason, sizeof(reason), "Schreibfehler (%d)", rc); goto done; }
    bytes += (uint64_t)hl;
  }

  uint64_t after = 0;
  uint64_t t_check = ps5tm_mono_ms(), t_sync = t_check;
  int      first = 1;
  for(;;) {
    uint64_t newest = 0;
    int      more = 0, lost = 0;
    unsigned n = ps5tm_klog_fetch(first ? 0 : after, first ? KLR_CONTEXT : KLR_BATCH, ln, &newest, &more, &lost);
    if(first) {
      first = 0;
      after = newest;
      more  = 0;
    } else if(n) {
      after = ln[n - 1].seq;
    }
    if(n) {
      int rc = 0;
      if(lost && after != 0) {
        /* the read fell behind the copy (more lines than it holds came between two reads) */
        ps5tm_klog_line_t gap;
        memset(&gap, 0, sizeof(gap));
        gap.t_ms   = ln[0].t_ms;
        gap.marker = 1;
        snprintf(gap.text, sizeof(gap.text), "— Lücke: Zeilen fehlen (mehr als die App behalten kann, zwischen zwei Abfragen) —");
        rc = write_lines(a.fd, &gap, 1, a.tz_min, out, cap, &bytes, &lines);
      }
      if(!rc) rc = write_lines(a.fd, ln, n, a.tz_min, out, cap, &bytes, &lines);
      if(rc) { snprintf(reason, sizeof(reason), "Schreibfehler (%d)", rc); break; }
    }
    pthread_mutex_lock(&g_lock);
    g_rec.bytes = bytes;
    g_rec.lines = lines;
    int stop = g_rec.stop;
    pthread_mutex_unlock(&g_lock);
    if(stop) break;
    if(bytes >= LIM_MAX_BYTES) { snprintf(reason, sizeof(reason), "Größe erreicht (%llu MB)", (unsigned long long)(LIM_MAX_BYTES >> 20)); break; }
    if(more) continue;                                    /* a backlog: go on at once */

    /* A second between reads, in slices so that a stop is heard at once. */
    int stopped = 0;
    for(int s = 0; s < 10 && !stopped; s++) {
      usleep(100 * 1000);
      pthread_mutex_lock(&g_lock);
      stopped = g_rec.stop;
      pthread_mutex_unlock(&g_lock);
    }
    if(stopped) {
      /* take what came in the last moments */
      newest = 0; more = 0; lost = 0;
      n = ps5tm_klog_fetch(after, KLR_BATCH, ln, &newest, &more, &lost);
      if(n) (void)write_lines(a.fd, ln, n, a.tz_min, out, cap, &bytes, &lines);
      break;
    }
    uint64_t now = ps5tm_mono_ms();
    if(now - t_sync >= 5000) { fsync(a.fd); t_sync = now; }
    if(now - t_check >= 30000) {
      t_check = now;
      if(free_bytes() < LIM_STOP_FREE) { snprintf(reason, sizeof(reason), "Speicher der Konsole knapp"); break; }
    }
  }

done:;
  {
    char foot[160];
    int fl = snprintf(foot, sizeof(foot), "# Aufnahme beendet: %s\n", reason);
    (void)write_all(a.fd, foot, (size_t)fl);
    bytes += (uint64_t)fl;
  }
  fsync(a.fd);
  close(a.fd);
  free(ln);
  free(out);

  char name[64];
  pthread_mutex_lock(&g_lock);
  g_rec.bytes    = bytes;
  g_rec.lines    = lines;
  g_rec.ended_ms = ps5tm_now_ms();
  snprintf(g_rec.reason, sizeof(g_rec.reason), "%s", reason);
  snprintf(name, sizeof(name), "%s", g_rec.name);
  g_rec.running  = 0;
  pthread_mutex_unlock(&g_lock);
  PS5TM_INFO("klog_record_end", "Kernel-Log: Aufnahme %s beendet (%s, %llu Zeilen, %llu KB).", name, reason,
             (unsigned long long)lines, (unsigned long long)(bytes >> 10));
  return NULL;
}

/* 200 on success, else an HTTP status with the reason in err. The slot is taken
   under the lock and the file work done outside it: a slow drive must not hold up
   the page's status reads. */
int
ps5tm_klogrec_start(int tz_min, char *err, size_t err_len) {
  tz_min = clamp_tz(tz_min);
  pthread_mutex_lock(&g_lock);
  if(g_rec.running) {
    pthread_mutex_unlock(&g_lock);
    snprintf(err, err_len, "Es läuft schon eine Aufnahme.");
    return 409;
  }
  memset(&g_rec, 0, sizeof(g_rec));
  g_rec.running    = 1;                       /* taken: a second start is refused from here on */
  g_rec.tz_min     = tz_min;
  g_rec.started_ms = ps5tm_now_ms();
  pthread_mutex_unlock(&g_lock);

  /* The folder first, so that the free space is asked of the right drive. */
  mkdir(PS5TM_DATA_DIR, 0755);
  mkdir(PS5TM_KLOG_DIR, 0755);
  int       status = 200;
  int       fd = -1;
  char      name[64] = "";
  rec_arg_t *a = NULL;

  uint64_t fr = free_bytes();
  if(fr < LIM_MIN_FREE) {
    snprintf(err, err_len, "Auf der Konsole sind nur noch %llu MB frei; eine Aufnahme braucht mehr als %llu MB Luft.",
             (unsigned long long)(fr >> 20), (unsigned long long)(LIM_MIN_FREE >> 20));
    status = 409;
  } else if((fd = ps5tm_klogfiles_create("aufnahme", tz_min, name, sizeof(name))) < 0) {
    snprintf(err, err_len, "Die Datei ließ sich nicht anlegen: %s", strerror(-fd));
    status = 500;
  } else if(!(a = malloc(sizeof(*a)))) {
    snprintf(err, err_len, "Kein Speicher.");
    status = 503;
  }

  if(status == 200) {
    a->fd = fd;
    a->tz_min = tz_min;
    pthread_mutex_lock(&g_lock);
    snprintf(g_rec.name, sizeof(g_rec.name), "%s", name);
    pthread_mutex_unlock(&g_lock);

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_t t;
    int prc = pthread_create(&t, &attr, rec_main, a);
    pthread_attr_destroy(&attr);
    if(prc != 0) {
      snprintf(err, err_len, "Die Aufnahme ließ sich nicht starten.");
      free(a);
      status = 503;
    }
  }

  if(status != 200) {
    if(fd >= 0) {
      char p[PATH_MAX];
      close(fd);
      snprintf(p, sizeof(p), "%s/%s", PS5TM_KLOG_DIR, name);
      unlink(p);                                /* nothing was written to it */
    }
    pthread_mutex_lock(&g_lock);
    memset(&g_rec, 0, sizeof(g_rec));           /* the slot is free again */
    pthread_mutex_unlock(&g_lock);
    return status;
  }
  PS5TM_INFO("klog_record_start", "Kernel-Log: Aufnahme beginnt (%s).", name);
  return 200;
}

/* Asks a running recording to end and waits for it (a few seconds at most). */
int
ps5tm_klogrec_stop(void) {
  pthread_mutex_lock(&g_lock);
  int was = g_rec.running;
  if(was) g_rec.stop = 1;
  pthread_mutex_unlock(&g_lock);
  if(!was) return 0;
  for(int i = 0; i < 150; i++) {                       /* 3 s */
    pthread_mutex_lock(&g_lock);
    int running = g_rec.running;
    pthread_mutex_unlock(&g_lock);
    if(!running) return 0;
    usleep(20 * 1000);
  }
  return 1;                                            /* it is still finishing: the status says so */
}

cJSON *
ps5tm_klogrec_json(void) {
  rec_t r;
  pthread_mutex_lock(&g_lock);
  r = g_rec;
  pthread_mutex_unlock(&g_lock);

  cJSON *o = cJSON_CreateObject();
  if(!o) return NULL;
  cJSON_AddBoolToObject(o, "recording", r.running);
  cJSON_AddStringToObject(o, "file", r.name);
  cJSON_AddNumberToObject(o, "bytes", (double)r.bytes);
  cJSON_AddNumberToObject(o, "lines", (double)r.lines);
  cJSON_AddNumberToObject(o, "max_bytes", (double)LIM_MAX_BYTES);
  uint64_t end = r.running ? ps5tm_now_ms() : r.ended_ms;
  cJSON_AddNumberToObject(o, "elapsed_s", r.started_ms && end >= r.started_ms ? (double)((end - r.started_ms) / 1000) : 0);
  cJSON_AddStringToObject(o, "reason", r.running ? "" : r.reason);
  return o;
}
