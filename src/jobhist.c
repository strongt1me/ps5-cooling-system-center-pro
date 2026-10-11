/* The history of the app's jobs (11.10.2026): every copy, move, conversion, installation, split, deletion,
 * file manager job, saved-data job and AMPR index ends with one line in /data/PS5-Cooling-Center/history.jsonl.
 * The page lists them on the games page ("Verlauf").
 *
 * One JSON object per line, appended (O_APPEND), so a power loss costs at most the line being written. The file
 * keeps the last HIST_KEEP entries: when it holds HIST_TRIM more than that, it is rewritten with only the last
 * HIST_KEEP (temporary file, fsync, rename). Nothing in here is needed for a job to run: a failure to write is
 * logged once and otherwise ignored. */
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "ps5tm.h"
#include "third_party/cJSON.h"

#define HIST_FILE  PS5TM_DATA_DIR "/history.jsonl"
#define HIST_KEEP  500
#define HIST_TRIM  100
#define HIST_LINE  2048

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static int g_lines = -1;              /* lines in the file, -1 until counted */
static int g_warned;

static int
count_lines_locked(void) {
  FILE *f = fopen(HIST_FILE, "r");
  if(!f) return 0;
  int n = 0, c, last = '\n';
  while((c = fgetc(f)) != EOF) { if(c == '\n') n++; last = c; }
  if(last != '\n') n++;
  fclose(f);
  return n;
}

/* Reads the whole file into memory; the caller frees it. NULL when there is none. */
static char *
slurp_locked(size_t *len) {
  int fd = open(HIST_FILE, O_RDONLY);
  if(fd < 0) return NULL;
  struct stat st;
  if(fstat(fd, &st) != 0 || st.st_size < 0 || st.st_size > 8 * 1024 * 1024) { close(fd); return NULL; }
  char *buf = malloc((size_t)st.st_size + 1);
  if(!buf) { close(fd); return NULL; }
  size_t have = 0;
  while(have < (size_t)st.st_size) {
    ssize_t r = read(fd, buf + have, (size_t)st.st_size - have);
    if(r < 0 && errno == EINTR) continue;
    if(r <= 0) break;
    have += (size_t)r;
  }
  close(fd);
  buf[have] = 0;
  *len = have;
  return buf;
}

/* Keeps only the last HIST_KEEP lines. */
static void
trim_locked(void) {
  size_t len = 0;
  char *buf = slurp_locked(&len);
  if(!buf) return;
  int total = 0;
  for(size_t i = 0; i < len; i++) if(buf[i] == '\n') total++;
  int skip = total - HIST_KEEP;
  char *start = buf;
  while(skip > 0 && *start) { if(*start == '\n') skip--; start++; }
  char tmp[sizeof(HIST_FILE) + 8];
  snprintf(tmp, sizeof(tmp), "%s.tmp", HIST_FILE);
  int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if(fd >= 0) {
    size_t n = strlen(start), off = 0;
    int ok = 1;
    while(off < n) {
      ssize_t w = write(fd, start + off, n - off);
      if(w < 0 && errno == EINTR) continue;
      if(w <= 0) { ok = 0; break; }
      off += (size_t)w;
    }
    if(ok && fsync(fd) != 0) ok = 0;
    if(close(fd) != 0) ok = 0;
    if(ok && rename(tmp, HIST_FILE) == 0) g_lines = HIST_KEEP < total ? HIST_KEEP : total;
    else unlink(tmp);
  }
  free(buf);
}

void
ps5tm_hist_add(const char *kind, const char *title, const char *detail, const char *result, const char *error,
               uint64_t bytes, uint64_t dur_ms) {
  cJSON *o = cJSON_CreateObject();
  if(!o) return;
  cJSON_AddNumberToObject(o, "t", (double)time(NULL));
  cJSON_AddStringToObject(o, "kind", kind ? kind : "");
  cJSON_AddStringToObject(o, "title", title ? title : "");
  if(detail && detail[0]) cJSON_AddStringToObject(o, "detail", detail);
  cJSON_AddStringToObject(o, "result", result ? result : "done");
  if(error && error[0]) cJSON_AddStringToObject(o, "error", error);
  if(bytes) cJSON_AddNumberToObject(o, "bytes", (double)bytes);
  cJSON_AddNumberToObject(o, "ms", (double)dur_ms);
  char *txt = cJSON_PrintUnformatted(o);
  cJSON_Delete(o);
  if(!txt) return;
  size_t n = strlen(txt);
  if(n >= HIST_LINE) { free(txt); return; }
  char line[HIST_LINE + 1];
  memcpy(line, txt, n);
  line[n++] = '\n';
  free(txt);

  pthread_mutex_lock(&g_lock);
  mkdir(PS5TM_DATA_DIR, 0755);
  if(g_lines < 0) g_lines = count_lines_locked();
  int fd = open(HIST_FILE, O_WRONLY | O_CREAT | O_APPEND, 0644);
  int ok = 0;
  if(fd >= 0) {
    ssize_t w;
    do { w = write(fd, line, n); } while(w < 0 && errno == EINTR);
    ok = w == (ssize_t)n;
    close(fd);
  }
  if(ok) {
    g_lines++;
    if(g_lines > HIST_KEEP + HIST_TRIM) trim_locked();
  } else if(!g_warned) {
    g_warned = 1;
    PS5TM_WARN("history_write_failed", "Der Verlauf ließ sich nicht schreiben (%s).", HIST_FILE);
  }
  pthread_mutex_unlock(&g_lock);
}

/* The entries, newest first. */
cJSON *
ps5tm_hist_json(void) {
  cJSON *root = cJSON_CreateObject();
  cJSON *arr = cJSON_AddArrayToObject(root, "entries");
  cJSON_AddBoolToObject(root, "ok", 1);
  pthread_mutex_lock(&g_lock);
  size_t len = 0;
  char *buf = slurp_locked(&len);
  pthread_mutex_unlock(&g_lock);
  if(!buf) return root;
  /* walk backwards line by line: newest first, at most HIST_KEEP */
  int shown = 0;
  char *end = buf + len;
  while(end > buf && shown < HIST_KEEP) {
    char *e = end;
    while(e > buf && e[-1] == '\n') e--;
    if(e == buf) break;
    char *s = e;
    while(s > buf && s[-1] != '\n') s--;
    char save = *e;
    *e = 0;
    cJSON *o = cJSON_Parse(s);
    *e = save;
    if(o && cJSON_IsObject(o)) { cJSON_AddItemToArray(arr, o); shown++; }
    else cJSON_Delete(o);
    end = s;
  }
  free(buf);
  return root;
}

int
ps5tm_hist_clear(void) {
  pthread_mutex_lock(&g_lock);
  int rc = unlink(HIST_FILE) == 0 || errno == ENOENT ? 0 : -1;
  if(rc == 0) g_lines = 0;
  pthread_mutex_unlock(&g_lock);
  return rc;
}
