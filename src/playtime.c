/* Play time: which game ran when, for how long, and how hot the console got.
 *
 * The console counts play time itself — the cards on the games page show that
 * figure, read out of its own database (library.c). What it cannot say is
 * when, in what pieces, and what the cooling went through meanwhile. This keeps
 * that: one line per session, with the time the game was in front, the hottest
 * and the average temperature while it was, the fan, and whether the emergency
 * mode or the warning came into play. It counts from the day it is installed on
 * and never touches what the console has counted.
 *
 * ── What a session is
 *
 * One stay of one title in the console's memory. The game probe says which
 * title is resident and whether it holds the controller (gamestate.c); this
 * reads that answer once a second and adds up the seconds the game was in
 * front. A game paused on the home screen is still the same session: it stays
 * open, counts nothing, and goes on counting when the player is back.
 *
 * It ends when
 *   - the title has been gone for PT_GRACE_MS — some games restart themselves
 *     and are missing for a few seconds in between,
 *   - another title takes its place, or
 *   - two passes lie PT_GAP_MS apart: the console slept (the payload's threads
 *     stand still in rest mode, see RESUME_GAP_MS in fan.c) or was starved.
 *     That time is nobody's play time, so the session closes at the last
 *     moment it was seen, not at the one it was noticed.
 * A session with less than PT_MIN_PLAY_S seconds in front is not kept. A launch
 * that died on the logo, or a game looked at for a moment, is not play.
 *
 * ── Why a thread of its own
 *
 * The numbers it needs — the game state and the fan thread's last sample — are
 * both cached copies, so one cheap pass a second costs nothing. Putting it on
 * the fan thread would put file writes on the thread that must never wait for a
 * busy drive, and putting it on the probe thread would stop the clock whenever
 * a Sony service holds that thread (probe.c). A stale game state is noticed
 * rather than trusted: with no fresh reading nothing is counted and nothing is
 * decided, and the session simply carries on when readings are back.
 *
 * ── What is stored
 *
 *   sessions.csv      one line per finished session, newest last, trimmed to
 *                     the last PT_KEEP_LINES. Appended to, never rewritten
 *                     except by the trim.
 *   session-open.csv  the session in progress, rewritten every PT_SAVE_MS. A
 *                     console switched off mid-game, or a payload replaced
 *                     while one runs, loses nothing but the last few seconds:
 *                     the next start finds the record and either carries the
 *                     session on (same title still running, seen a moment ago)
 *                     or closes it at the last time it was seen and marks the
 *                     line as an estimate.
 *
 * Memory holds only the session in progress. The page asks for the file when it
 * is opened, which is a few hundred kilobytes read once. The two locks never
 * nest, and neither is held across file I/O of the other kind: g_lock guards
 * the session and is only ever held for a few instructions, g_file_lock guards
 * the files.
 */

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "ps5tm.h"
#include "third_party/cJSON.h"

#define PT_PATH       PS5TM_DATA_DIR "/sessions.csv"
#define PT_OPEN_PATH  PS5TM_DATA_DIR "/session-open.csv"

#define PT_STALE_MS     20000     /* a game reading older than this is not one    */
#define PT_SAMPLE_MS    10000     /* ... and a fan sample older than this neither */
#define PT_STEP_MAX_MS  5000      /* the most one pass may add to the play time   */
#define PT_GAP_MS       45000     /* two passes this far apart: slept or starved  */
#define PT_GRACE_MS     45000     /* a title gone this long is over               */
#define PT_SAVE_MS      30000     /* how often the open session is written down   */
#define PT_RESUME_S     180       /* a restarted payload takes the old session on
                                     if it was seen this recently                 */
#define PT_MIN_PLAY_S   30
#define PT_KEEP_LINES   3000
#define PT_RING         3500      /* rows a read of the file holds at most        */
#define PT_TRIM_BYTES   450000    /* the file grows past this: cut it back        */
#define PT_TRIM_READ    (2u * 1024 * 1024)
#define PT_LINE_MAX     512
#define PT_NAME         96

/* What a row of sessions.csv may say at most. Damage — a line glued to another after a
   cut-off write, a flipped digit — shows up as numbers like these, and one such row
   would wreck the totals and the ranking until it is trimmed away. */
#define PT_T_MIN        946684800LL      /* 2000-01-01 */
#define PT_T_MAX        4102444800LL     /* 2100-01-01 */
#define PT_SPAN_MAX     (400LL * 86400)

#define PT_HEADER \
  "# start_s,end_s,play_s,cpu_max,cpu_avg,soc_max,soc_avg,fan_avg,fan_max,flags,title,name\n"

/* flags */
#define PT_F_SAFETY     0x01u     /* the emergency mode was active at some point  */
#define PT_F_WARNING    0x02u     /* the temperature warning was                  */
#define PT_F_ESTIMATED  0x04u     /* the end is the last note before the payload
                                     stopped, not an observed end                 */

typedef struct {
  char     title[16];
  char     name[PT_NAME];
  int64_t  start_s;               /* wall clock, seconds                          */
  int64_t  seen_s;                /* the last pass that found the title           */
  int64_t  front_s;               /* the last pass that found it in front — the end
                                     of the session as the list shows it: a game left
                                     paused for hours is not "played" until then   */
  uint64_t play_ms;               /* in front                                     */
  int      cpu_max, soc_max, fan_max;         /* -1: no reading yet               */
  uint64_t cpu_sum, soc_sum, fan_sum;
  uint32_t cpu_n,   soc_n,   fan_n;
  unsigned flags;
  int      in_front;              /* only in a record read back: in front when saved */
} session_t;

static pthread_mutex_t g_lock      = PTHREAD_MUTEX_INITIALIZER;   /* the session   */
static pthread_mutex_t g_file_lock = PTHREAD_MUTEX_INITIALIZER;   /* the two files */

static session_t g_cur;
static int       g_open;           /* g_cur holds a session                       */
static int       g_front;          /* ... which was in front on the last pass     */
static int       g_gone;           /* ... whose title was missing on the last one */
static uint64_t  g_gone_ms;        /* ... since this moment (monotonic)           */
static uint64_t  g_prev_mono_ms;
static int64_t   g_prev_wall_s;
static uint64_t  g_saved_ms;       /* when the open record was last written       */
static int       g_save_now;       /* ... and it is to be written in this pass    */
static int       g_started;        /* the first reading has been seen — only the
                                      thread that calls step() looks at this     */
static long      g_file_bytes = -1;/* size of sessions.csv, -1 = not looked yet   */
static int       g_write_failed;   /* said once, not once per session             */


/* What step() has to do once it has let go of g_lock: the lines to write and the
   things to say. Two of each, because a session can end and the next begin in
   one pass. Only the thread that calls step() uses it. */
typedef struct {
  char line[2][PT_LINE_MAX];
  int  nline;
  char open[PT_LINE_MAX];
  int  drop_open;
  char note[2][256];
  int  nnote;
} io_t;

static io_t g_io;


static void
set_name(session_t *s, const char *src) {
  ps5tm_copy_utf8(s->name, sizeof(s->name), src ? src : "");
  for(char *p = s->name; *p; p++)
    if((unsigned char)*p < 0x20 || *p == 0x7f) *p = ' ';
}


static int
avg_of(uint64_t sum, uint32_t n) {
  return n ? (int)((sum + n / 2) / n) : -1;
}


static void
fmt_dur(uint64_t sec, char *out, size_t n) {
  if(sec < 60)        snprintf(out, n, "%u Sek.", (unsigned)sec);
  else if(sec < 3600) snprintf(out, n, "%u Min.", (unsigned)(sec / 60));
  else                snprintf(out, n, "%u Std. %u Min.",
                               (unsigned)(sec / 3600),
                               (unsigned)((sec % 3600) / 60));
}


static void
note(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

static void
note(const char *fmt, ...) {
  if(g_io.nnote >= 2) return;
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(g_io.note[g_io.nnote++], sizeof(g_io.note[0]), fmt, ap);
  va_end(ap);
}


/* ------------------------------------------------------------ the records */

/* A title id as the game probe reports it: four capitals and five digits. Anything
   else in a file is damage. */
static int
title_ok(const char *t) {
  if(strlen(t) != 9) return 0;
  for(int i = 0; i < 4; i++) if(t[i] < 'A' || t[i] > 'Z') return 0;
  for(int i = 4; i < 9; i++) if(t[i] < '0' || t[i] > '9') return 0;
  return 1;
}


/* A finished session as its line in sessions.csv; 0 when it is too short. */
static int
format_line(const session_t *s, unsigned extra, char *out, size_t n) {
  uint64_t play = s->play_ms / 1000;
  if(play < PT_MIN_PLAY_S) return 0;
  int64_t end = s->front_s >= s->start_s ? s->front_s : s->start_s;
  snprintf(out, n, "%lld,%lld,%llu,%d,%d,%d,%d,%d,%d,%u,%s,%s\n",
           (long long)s->start_s, (long long)end, (unsigned long long)play,
           s->cpu_max, avg_of(s->cpu_sum, s->cpu_n),
           s->soc_max, avg_of(s->soc_sum, s->soc_n),
           avg_of(s->fan_sum, s->fan_n), s->fan_max,
           s->flags | extra, s->title, s->name);
  return 1;
}


/* The session in progress, with everything needed to carry it on — including whether
   it was in front, which the game probe cannot say again after a restart once the
   kernel's ring has scrolled past the last focus change. */
static void
format_open(const session_t *s, int front, char *out, size_t n) {
  snprintf(out, n, "v2,%lld,%lld,%lld,%llu,%d,%d,%d,%llu,%u,%llu,%u,%llu,%u,%u,%d,%s,%s\n",
           (long long)s->start_s, (long long)s->seen_s, (long long)s->front_s,
           (unsigned long long)s->play_ms,
           s->cpu_max, s->soc_max, s->fan_max,
           (unsigned long long)s->cpu_sum, s->cpu_n,
           (unsigned long long)s->soc_sum, s->soc_n,
           (unsigned long long)s->fan_sum, s->fan_n,
           s->flags, front ? 1 : 0, s->title, s->name);
}


/* Reads a record back. The first version (the one before front_s and the front flag)
   is still understood: its end is the last pass that saw the title, and it was not
   in front as far as anyone knows. Times that are out of order are put in order, not
   refused: a clock that was set back during a game must not cost the session. */
static int
parse_open(const char *line, session_t *s) {
  long long start, seen, front_s = 0;
  unsigned long long play, cs, ss, fs;
  unsigned cn, sn, fn, flags;
  int cmax, smax, fmax, front = 0, pos = 0, n;
  char title[16];

  if(!strncmp(line, "v2,", 3)) {
    n = sscanf(line, "v2,%lld,%lld,%lld,%llu,%d,%d,%d,%llu,%u,%llu,%u,%llu,%u,%u,%d,%15[^,],%n",
               &start, &seen, &front_s, &play, &cmax, &smax, &fmax,
               &cs, &cn, &ss, &sn, &fs, &fn, &flags, &front, title, &pos);
    if(n != 16 || pos <= 0) return 0;
  } else {
    n = sscanf(line, "v1,%lld,%lld,%llu,%d,%d,%d,%llu,%u,%llu,%u,%llu,%u,%u,%15[^,],%n",
               &start, &seen, &play, &cmax, &smax, &fmax,
               &cs, &cn, &ss, &sn, &fs, &fn, &flags, title, &pos);
    if(n != 14 || pos <= 0) return 0;
    front_s = seen;
  }
  if(start < PT_T_MIN || start > PT_T_MAX || !title_ok(title)) return 0;
  if(play > (unsigned long long)PT_SPAN_MAX * 1000ull) return 0;
  if(seen < start) seen = start;
  if(front_s < start) front_s = start;

  memset(s, 0, sizeof(*s));
  snprintf(s->title, sizeof(s->title), "%s", title);
  char nm[PT_NAME * 2];
  snprintf(nm, sizeof(nm), "%s", line + pos);
  nm[strcspn(nm, "\r\n")] = 0;
  set_name(s, nm);
  s->start_s = start; s->seen_s = seen; s->front_s = front_s; s->play_ms = play;
  s->cpu_max = cmax; s->soc_max = smax; s->fan_max = fmax;
  s->cpu_sum = cs; s->cpu_n = cn; s->soc_sum = ss; s->soc_n = sn;
  s->fan_sum = fs; s->fan_n = fn;
  s->flags = flags;
  s->in_front = front ? 1 : 0;
  return 1;
}


typedef struct {
  int64_t  start, end;
  unsigned play, flags;
  int      cpu_max, cpu_avg, soc_max, soc_avg, fan_avg, fan_max;
  char     title[16];
  char     name[PT_NAME];
} row_t;


static int
parse_row(const char *line, row_t *r) {
  long long start, end;
  unsigned long long play;
  unsigned flags;
  int cm, ca, sm, sa, fa, fm, pos = 0;
  char title[16];

  int n = sscanf(line, "%lld,%lld,%llu,%d,%d,%d,%d,%d,%d,%u,%15[^,],%n",
                 &start, &end, &play, &cm, &ca, &sm, &sa, &fa, &fm, &flags,
                 title, &pos);
  if(n != 11 || pos <= 0 || !title_ok(title)) return 0;
  if(start < PT_T_MIN || start > PT_T_MAX || end < start || end - start > PT_SPAN_MAX ||
     play > (unsigned long long)PT_SPAN_MAX) return 0;

  r->start = start; r->end = end; r->play = (unsigned)play; r->flags = flags;
  r->cpu_max = cm; r->cpu_avg = ca; r->soc_max = sm; r->soc_avg = sa;
  r->fan_avg = fa; r->fan_max = fm;
  snprintf(r->title, sizeof(r->title), "%s", title);
  char nm[PT_NAME * 2];
  snprintf(nm, sizeof(nm), "%s", line + pos);
  nm[strcspn(nm, "\r\n")] = 0;
  ps5tm_copy_utf8(r->name, sizeof(r->name), nm);
  return 1;
}


/* ------------------------------------------------------------------ files */

static void
write_failed(const char *what, int eno) {
  if(g_write_failed) return;
  g_write_failed = 1;
  PS5TM_WARN("playtime_save_failed",
             "Die Spielzeit konnte nicht gespeichert werden (%s, Fehler %d: "
             "%s) – die laufende Sitzung zählt im Speicher weiter; eine beendete "
             "fehlt im Verlauf, wird aber beim nächsten Start aus dem Zwischenstand "
             "nachgetragen, solange keine neue ihn ersetzt.",
             what, eno, strerror(eno));
}


/* Keeps the newest PT_KEEP_LINES lines. Only the end of the file is read: a
   file that has grown beyond reason is cut back all the same. The old file is
   replaced only by a new one that got all the way through. */
static void
trim_locked(void) {
  FILE *f = fopen(PT_PATH, "rb");
  if(!f) return;
  if(fseek(f, 0, SEEK_END) != 0) { fclose(f); return; }
  long size = ftell(f);
  if(size <= 0) { fclose(f); return; }
  long from = size > (long)PT_TRIM_READ ? size - (long)PT_TRIM_READ : 0;
  if(fseek(f, from, SEEK_SET) != 0) { fclose(f); return; }

  size_t want = (size_t)(size - from);
  char  *buf  = malloc(want + 1);
  if(!buf) { fclose(f); return; }
  size_t got = fread(buf, 1, want, f);
  fclose(f);
  buf[got] = 0;

  /* A cut in the middle of the file starts in the middle of a line. */
  char *start = buf;
  if(from > 0) {
    char *nl = memchr(buf, '\n', got);
    start = nl ? nl + 1 : buf + got;
  }

  /* The last PT_KEEP_LINES lines: count newlines back from the end. The one
     that closes the last line does not count, it ends nothing before it. */
  char    *end  = buf + got;
  unsigned seen = 0;
  char    *keep = end;
  while(keep > start) {
    keep--;
    if(*keep == '\n' && keep + 1 != end && ++seen >= PT_KEEP_LINES) { keep++; break; }
  }
  if(keep < start) keep = start;

  /* The header is written afresh; an old one that is still in the kept part
     would stand there twice. */
  while(keep < end && *keep == '#') {
    char *nl = memchr(keep, '\n', (size_t)(end - keep));
    keep = nl ? nl + 1 : end;
  }

  char tmp[sizeof(PT_PATH) + 8];
  snprintf(tmp, sizeof(tmp), "%s.tmp", PT_PATH);

  int   ok  = 0;
  int   eno = 0;
  long  newsize = 0;
  FILE *o = fopen(tmp, "wb");
  if(!o) {
    eno = errno;
  } else {
    size_t hl = strlen(PT_HEADER);
    size_t kl = (size_t)(end - keep);
    ok = fwrite(PT_HEADER, 1, hl, o) == hl && fwrite(keep, 1, kl, o) == kl;
    newsize = (long)(hl + kl);
    if(fflush(o) != 0 || ferror(o)) ok = 0;
    eno = errno;
    if(fclose(o) != 0 && ok) { ok = 0; eno = errno; }
    if(ok && rename(tmp, PT_PATH) != 0) { ok = 0; eno = errno; }
    if(!ok) unlink(tmp);
  }
  free(buf);

  if(ok) g_file_bytes = newsize;
  else   write_failed("Kürzen", eno);
}


/* Appends one line, 0 when it is in the file. One write() for everything that goes in
 * (the header of a new file, a newline, the line), so there is no state in between;
 * and two things the file has to survive:
 *   - a run that died inside a line left no newline behind. The next line would be
 *     glued to the stump, and both are lost — the stump ends first;
 *   - a write that was cut short (a full drive) must not leave half a line: the file
 *     goes back to the size it had.
 * A failure is reported, and the caller keeps what it has (see io_apply). */
static int
append_locked(const char *line) {
  mkdir(PS5TM_DATA_DIR, 0755);

  /* Read and write: the last byte is looked at before anything is added. */
  int fd = open(PT_PATH, O_RDWR | O_APPEND | O_CREAT, 0644);
  if(fd < 0) { write_failed("Öffnen", errno); return -1; }

  struct stat st;
  if(fstat(fd, &st) != 0) {
    int eno = errno;
    close(fd);
    write_failed("Größe", eno);
    return -1;
  }
  off_t before = st.st_size;

  char   buf[PT_LINE_MAX + sizeof(PT_HEADER) + 4];
  size_t n = 0;
  if(before == 0) {
    memcpy(buf, PT_HEADER, sizeof(PT_HEADER) - 1);
    n = sizeof(PT_HEADER) - 1;
  } else {
    char last = '\n';
    if(pread(fd, &last, 1, before - 1) == 1 && last != '\n') buf[n++] = '\n';
  }
  size_t ll = strlen(line);
  if(ll > PT_LINE_MAX) ll = PT_LINE_MAX;
  memcpy(buf + n, line, ll);
  n += ll;

  ssize_t w   = write(fd, buf, n);
  int     ok  = (w == (ssize_t)n);
  int     eno = errno;
  if(!ok && ftruncate(fd, before) != 0) { /* nothing more can be done */ }
  if(close(fd) != 0 && ok) { ok = 0; eno = errno; }

  if(!ok) { write_failed("Schreiben", eno); return -1; }
  g_write_failed = 0;
  g_file_bytes   = (long)before + (long)n;
  if(g_file_bytes > PT_TRIM_BYTES) trim_locked();
  return 0;
}


static void
write_open_locked(const char *line) {
  mkdir(PS5TM_DATA_DIR, 0755);

  char tmp[sizeof(PT_OPEN_PATH) + 8];
  snprintf(tmp, sizeof(tmp), "%s.tmp", PT_OPEN_PATH);

  int   ok  = 0;
  int   eno = 0;
  FILE *f = fopen(tmp, "wb");
  if(!f) {
    eno = errno;
  } else {
    ok  = fputs(line, f) >= 0;
    if(fflush(f) != 0 || ferror(f)) ok = 0;
    eno = errno;
    if(fclose(f) != 0 && ok) { ok = 0; eno = errno; }
    if(ok && rename(tmp, PT_OPEN_PATH) != 0) { ok = 0; eno = errno; }
    if(!ok) unlink(tmp);
  }
  if(!ok) write_failed("Zwischenstand", eno);
}


/* The record a previous run left behind, or 0. Called once, before step() takes
   g_lock for the first time. */
static int
read_open(session_t *out) {
  int ok = 0;
  pthread_mutex_lock(&g_file_lock);
  FILE *f = fopen(PT_OPEN_PATH, "r");
  if(f) {
    char line[PT_LINE_MAX];
    if(fgets(line, sizeof(line), f)) ok = parse_open(line, out);
    fclose(f);
  }
  pthread_mutex_unlock(&g_file_lock);
  return ok;
}


static void
io_apply(void) {
  if(g_io.nline || g_io.open[0] || g_io.drop_open) {
    pthread_mutex_lock(&g_file_lock);
    int all_in = 1;
    for(int i = 0; i < g_io.nline; i++)
      if(append_locked(g_io.line[i]) != 0) all_in = 0;
    /* The record of a finished session goes only once its line is in the file: it is
       the one other copy, and a restart can still close the session from it. */
    if(g_io.open[0])                     write_open_locked(g_io.open);
    else if(g_io.drop_open && all_in)    unlink(PT_OPEN_PATH);
    pthread_mutex_unlock(&g_file_lock);
  }
  for(int i = 0; i < g_io.nnote; i++)
    PS5TM_INFO("playtime_session", "%s", g_io.note[i]);
}


/* --------------------------------------------------------------- sessions */

static void
begin_locked(int64_t wall_s, const ps5tm_gamestate_t *gs) {
  memset(&g_cur, 0, sizeof(g_cur));
  ps5tm_copy_utf8(g_cur.title, sizeof(g_cur.title), gs->title_id);
  set_name(&g_cur, gs->title_name);
  g_cur.start_s = g_cur.seen_s = g_cur.front_s = wall_s;
  g_cur.cpu_max = g_cur.soc_max = g_cur.fan_max = -1;
  g_open = 1; g_front = 0; g_gone = 0;
  g_save_now = 1;                         /* write the record at once */

  g_io.drop_open = 0;
  /* The id, not the name: this line is copied into the kernel's message buffer, and
     the game probe scans that for markers. A title called "SetControllerFocus(0x7)"
     would take the pad from the running game. */
  note("Spielzeit: Titel %s – Sitzung beginnt.", g_cur.title);
}


static void
finish_locked(unsigned extra) {
  if(!g_open) return;

  if(g_io.nline < 2 &&
     format_line(&g_cur, extra, g_io.line[g_io.nline], sizeof(g_io.line[0]))) {
    g_io.nline++;
    char d[32];
    fmt_dur(g_cur.play_ms / 1000, d, sizeof(d));
    if(g_cur.cpu_max >= 0 && g_cur.soc_max >= 0)
      note("Spielzeit: Titel %s – Sitzung beendet, %s vorn, CPU bis %d °C, SoC bis %d °C.",
           g_cur.title, d, g_cur.cpu_max, g_cur.soc_max);
    else
      note("Spielzeit: Titel %s – Sitzung beendet, %s vorn.", g_cur.title, d);
  }

  g_io.open[0]   = 0;
  g_io.drop_open = 1;
  g_open = 0; g_front = 0; g_gone = 0;
}


static void
add_sample(session_t *s, const ps5tm_playtime_sample_t *p) {
  if(!p) return;
  if(p->cpu_c >= 0) {
    if(p->cpu_c > s->cpu_max) s->cpu_max = p->cpu_c;
    s->cpu_sum += (unsigned)p->cpu_c; s->cpu_n++;
  }
  if(p->soc_c >= 0) {
    if(p->soc_c > s->soc_max) s->soc_max = p->soc_c;
    s->soc_sum += (unsigned)p->soc_c; s->soc_n++;
  }
  if(p->fan_pct >= 0) {
    if(p->fan_pct > s->fan_max) s->fan_max = p->fan_pct;
    s->fan_sum += (unsigned)p->fan_pct; s->fan_n++;
  }
  if(p->safety)  s->flags |= PT_F_SAFETY;
  if(p->warning) s->flags |= PT_F_WARNING;
}


/* One pass. `fresh` says the game reading is recent enough to act on; without
   it only the clock moves, and a stop of this thread is still noticed. Called by one
   thread, once a second — and by the tests, which supply the clock themselves. */
void
ps5tm_playtime_step(int64_t wall_s, uint64_t mono_ms, int fresh,
                    const ps5tm_gamestate_t *gs,
                    const ps5tm_playtime_sample_t *smp) {
  memset(&g_io, 0, sizeof(g_io));

  /* What a previous run left, read before g_lock is taken: it is file I/O. */
  session_t left;
  int have_left = 0;
  if(fresh && !g_started) {
    g_started = 1;
    have_left = read_open(&left);
  }

  pthread_mutex_lock(&g_lock);

  uint64_t dt = (g_prev_mono_ms && mono_ms > g_prev_mono_ms)
                  ? mono_ms - g_prev_mono_ms : 0;
  uint64_t gap = dt;
  if(g_prev_wall_s && wall_s > g_prev_wall_s) {
    uint64_t w = (uint64_t)(wall_s - g_prev_wall_s) * 1000ull;
    if(w > gap) gap = w;
  }
  g_prev_mono_ms = mono_ms;
  g_prev_wall_s  = wall_s;

  /* Slept or starved since the last pass: the session ended where the game was last
     seen in front. That is a fact about this thread's clock, not about the game
     reading, so it holds with an old reading too — a console that wakes with a game
     still resident has one for a few passes, and a session left open across them
     would span the whole night. What the title looks like now is judged as a fresh
     start below. */
  if(g_open && gap >= PT_GAP_MS) finish_locked(0);

  if(!fresh || !gs) {
    pthread_mutex_unlock(&g_lock);
    io_apply();
    return;
  }

  int present = gs->title_id[0] != 0;

  if(have_left) {
    if(present && !strcmp(left.title, gs->title_id) &&
       wall_s >= left.seen_s && wall_s - left.seen_s <= PT_RESUME_S) {
      g_cur = left;                       /* the same game is still running */
      g_open = 1; g_front = left.in_front ? 1 : 0; g_gone = 0;
      g_save_now = 1;
      note("Spielzeit: Titel %s – Sitzung nach dem Neustart der App fortgesetzt.", g_cur.title);
    } else {
      /* Closed where it was last seen: what happened after that is unknown. */
      g_cur = left;
      g_open = 1;
      finish_locked(PT_F_ESTIMATED);
    }
  }

  if(present) {
    if(g_open && strcmp(g_cur.title, gs->title_id) != 0)
      finish_locked(0);                   /* another title took over */
    if(!g_open)
      begin_locked(wall_s, gs);

    g_gone = 0;
    g_cur.seen_s = wall_s > g_cur.start_s ? wall_s : g_cur.start_s;   /* never before the start */
    /* A name that arrives late replaces the stand-in. */
    if((!g_cur.name[0] || !strcmp(g_cur.name, g_cur.title)) &&
       gs->title_name[0] && strcmp(gs->title_name, gs->title_id) != 0)
      set_name(&g_cur, gs->title_name);

    /* In front, paused on the home screen — or neither: "geladen" is the probe saying it
       has no focus line to go by (this app was started during the game, or the kernel's
       ring has scrolled past the last one). The pad does not change hands without a new
       line, so what was true stays true; gamestate.c reasons the same way about its own
       remembered focus. */
    g_front = gs->foreground ? 1 : gs->suspended ? 0 : g_front;
    if(g_front) {
      g_cur.play_ms += dt < PT_STEP_MAX_MS ? dt : PT_STEP_MAX_MS;
      g_cur.front_s  = g_cur.seen_s;
      add_sample(&g_cur, smp);
    }
  } else if(g_open) {
    g_front = 0;
    if(!g_gone) {
      g_gone = 1;
      g_gone_ms = mono_ms;
    } else if(mono_ms - g_gone_ms >= PT_GRACE_MS) {
      finish_locked(0);
    }
  }

  if(g_open && !g_gone && (g_save_now || mono_ms - g_saved_ms >= PT_SAVE_MS)) {
    format_open(&g_cur, g_front, g_io.open, sizeof(g_io.open));
    g_saved_ms = mono_ms;
    g_save_now = 0;
  }

  /* A record to drop and a record to write in the same pass: the new one wins. */
  if(g_io.open[0]) g_io.drop_open = 0;

  /* The leftover record was dealt with one way or the other. */
  if(have_left && !g_io.open[0] && !g_open) g_io.drop_open = 1;

  pthread_mutex_unlock(&g_lock);
  io_apply();
}


/* ------------------------------------------------------------------ thread */

/* One pass: look at the two caches and hand what they say to step(). */
static void
playtime_pass(void) {
  static ps5tm_snapshot_t snap;          /* big, and the thread's alone */

  ps5tm_gamestate_t gs;
  ps5tm_gamestate_get(&gs);
  int fresh = ps5tm_gamestate_measured() &&
              ps5tm_gamestate_age_ms() <= PT_STALE_MS;

  ps5tm_playtime_sample_t smp = { -1, -1, -1, 0, 0 };
  ps5tm_fan_snapshot(&snap);
  uint64_t now = ps5tm_now_ms();
  if(snap.sampled_at_ms && now >= snap.sampled_at_ms &&
     now - snap.sampled_at_ms <= PT_SAMPLE_MS) {
    if(snap.sensors.cpu_valid)      smp.cpu_c   = snap.sensors.cpu_c;
    if(snap.sensors.soc_valid)      smp.soc_c   = snap.sensors.soc_c;
    if(snap.sensors.fan_duty_valid) smp.fan_pct = snap.sensors.fan_duty_pct;
    smp.safety  = snap.safety_active  ? 1 : 0;
    smp.warning = snap.warning_active ? 1 : 0;
  }

  ps5tm_playtime_step((int64_t)(now / 1000), ps5tm_mono_ms(), fresh, &gs, &smp);
}


static void *
playtime_worker(void *arg) {
  (void)arg;
  for(;;) {
    playtime_pass();
    sleep(1);
  }
  return NULL;
}


void
ps5tm_playtime_start(void) {
  static int started = 0;
  if(started) return;
  started = 1;

  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

  pthread_t t;
  if(pthread_create(&t, &attr, playtime_worker, NULL) != 0) {
    PS5TM_WARN("playtime_thread_failed",
               "Die Spielzeit-Erfassung konnte nicht gestartet werden – es "
               "wird keine Spielzeit mitgeschrieben. Alles andere läuft "
               "unabhängig davon weiter.");
    started = 0;
  }
  pthread_attr_destroy(&attr);
}


/* -------------------------------------------------------------------- JSON */

static void
add_live(cJSON *root) {
  session_t cur;
  int       open, front, gone;

  pthread_mutex_lock(&g_lock);
  cur = g_cur; open = g_open; front = g_front; gone = g_gone;
  pthread_mutex_unlock(&g_lock);

  if(!open) { cJSON_AddNullToObject(root, "live"); return; }

  cJSON *o = cJSON_AddObjectToObject(root, "live");
  if(!o) return;
  cJSON_AddStringToObject(o, "id",   cur.title);
  cJSON_AddStringToObject(o, "name", cur.name[0] ? cur.name : cur.title);
  cJSON_AddNumberToObject(o, "start", (double)cur.start_s);
  cJSON_AddNumberToObject(o, "play",  (double)(cur.play_ms / 1000));
  cJSON_AddStringToObject(o, "state", gone ? "gone" : front ? "front" : "back");
  cJSON_AddNumberToObject(o, "cpu_max", cur.cpu_max);
  cJSON_AddNumberToObject(o, "cpu_avg", avg_of(cur.cpu_sum, cur.cpu_n));
  cJSON_AddNumberToObject(o, "soc_max", cur.soc_max);
  cJSON_AddNumberToObject(o, "soc_avg", avg_of(cur.soc_sum, cur.soc_n));
  cJSON_AddNumberToObject(o, "fan_avg", avg_of(cur.fan_sum, cur.fan_n));
  cJSON_AddNumberToObject(o, "fan_max", cur.fan_max);
  cJSON_AddNumberToObject(o, "flags", cur.flags);
}


/* `max` sessions, the newest first, with the name each title last had. */
cJSON *
ps5tm_playtime_json(unsigned max) {
  if(max < 1) max = 1;
  if(max > PT_RING) max = PT_RING;

  cJSON *root = cJSON_CreateObject();
  if(!root) return NULL;
  cJSON_AddBoolToObject(root, "ok", 1);
  cJSON_AddNumberToObject(root, "now", (double)(ps5tm_now_ms() / 1000));

  ps5tm_config_lock();
  int on = (g_config.probe_mask & PS5TM_PROBE_GAME) != 0;
  ps5tm_config_unlock();
  cJSON_AddBoolToObject(root, "enabled", on);

  add_live(root);

  row_t *ring = calloc(PT_RING, sizeof(*ring));
  if(!ring) { cJSON_Delete(root); return NULL; }

  unsigned total = 0;
  pthread_mutex_lock(&g_file_lock);
  FILE *f = fopen(PT_PATH, "r");
  if(f) {
    char line[PT_LINE_MAX];
    int  skipping = 0;
    while(fgets(line, sizeof(line), f)) {
      size_t len      = strlen(line);
      int    complete = len && line[len - 1] == '\n';
      /* The rest of a line that was too long, and a last line that is still
         being written, are not rows. */
      if(skipping) { if(complete) skipping = 0; continue; }
      if(!complete) { if(len == sizeof(line) - 1) skipping = 1; continue; }
      if(line[0] == '#') continue;
      row_t r;
      if(!parse_row(line, &r)) continue;
      ring[total % PT_RING] = r;
      total++;
    }
    fclose(f);
  }
  pthread_mutex_unlock(&g_file_lock);

  unsigned have = total < PT_RING ? total : PT_RING;
  unsigned show = have < max ? have : max;

  cJSON *titles   = cJSON_AddObjectToObject(root, "titles");
  cJSON *sessions = cJSON_AddArrayToObject(root, "sessions");
  /* The real number of rows, not the ring's: the page compares it with the last one to
     notice a finished session, and a count that stops at the ring's size stops noticing. */
  cJSON_AddNumberToObject(root, "total", total);
  cJSON_AddNumberToObject(root, "kept",  PT_KEEP_LINES);

  for(unsigned i = 0; i < show && titles && sessions; i++) {
    const row_t *r = &ring[(total - 1 - i) % PT_RING];
    cJSON *o = cJSON_CreateObject();
    if(!o) break;
    cJSON_AddStringToObject(o, "id", r->title);
    cJSON_AddNumberToObject(o, "start", (double)r->start);
    cJSON_AddNumberToObject(o, "end",   (double)r->end);
    cJSON_AddNumberToObject(o, "play",  r->play);
    cJSON_AddNumberToObject(o, "cpu_max", r->cpu_max);
    cJSON_AddNumberToObject(o, "cpu_avg", r->cpu_avg);
    cJSON_AddNumberToObject(o, "soc_max", r->soc_max);
    cJSON_AddNumberToObject(o, "soc_avg", r->soc_avg);
    cJSON_AddNumberToObject(o, "fan_avg", r->fan_avg);
    cJSON_AddNumberToObject(o, "fan_max", r->fan_max);
    cJSON_AddNumberToObject(o, "flags", r->flags);
    cJSON_AddItemToArray(sessions, o);

    /* Newest first, so the first name seen for a title is its latest. */
    if(!cJSON_HasObjectItem(titles, r->title))
      cJSON_AddStringToObject(titles, r->title, r->name[0] ? r->name : r->title);
  }

  free(ring);
  return root;
}


/* Deletes the saved sessions. A game that is running keeps counting; it is
   saved when it ends, like any other. */
int
ps5tm_playtime_reset(void) {
  int rc = 0;
  pthread_mutex_lock(&g_file_lock);
  if(unlink(PT_PATH) != 0 && errno != ENOENT) rc = -1;
  else g_file_bytes = 0;
  pthread_mutex_unlock(&g_file_lock);
  if(rc == 0) PS5TM_INFO("playtime_reset", "Der Spielzeit-Verlauf wurde gelöscht.");
  else        PS5TM_WARN("playtime_reset_failed",
                         "Der Spielzeit-Verlauf ließ sich nicht löschen.");
  return rc;
}
