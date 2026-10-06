/* Which game is running, and is it actually in the foreground.
 *
 * The kernel writes its Shell/LNC lifecycle events to the message buffer, and
 * those events are the only reliable evidence of foreground activity — a
 * loaded process proves nothing, because a suspended game stays resident
 * while the user is back on the home screen. The patterns below are the ones
 * ps-game-state-lib established (MIT, StonedModder):
 *
 *   "VideoOut: shared (pid=0x… appId=0x…)"  the title that owns the display
 *   "SetControllerFocus(<id>)"              who currently has the pad;
 *                                           0x7 is the Shell itself, in
 *                                           the low bits (SHELL_FOCUS_ID)
 *   "/user/app/<TITLEID>"                   four capitals plus five digits
 *
 * The human-readable name is then read straight out of the installed app's
 * own param.json — param.sfo for a PS4 title — rather than going through
 * Sony's app database, which would mean dragging in SQLite for a single
 * string.
 */

/* No module is ever loaded at run time.
 *
 * sceKernelLoadStartModule() takes the runtime linker's lock, and on FW 12.00
 * it does not always come back. That lock is process-wide, so a single stuck
 * call froze every other thread the moment it needed a symbol resolved — the
 * web server answered exactly one request and then went silent. Moving the
 * call to a background thread did not help, because the lock is shared.
 *
 * Only libraries the ELF is linked against are used, and only through dlsym.
 * A symbol that is not there leaves a null pointer and the feature reports
 * nothing, which is a far better failure than a dead dashboard. */

#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef PS5TM_HOST_TEST
#include <sys/sysctl.h>
#endif

#include "klogown.h"
#include "ps5tm.h"
#include "third_party/cJSON.h"

#define GAMESTATE_TTL_MS  4000

/* Four readers take their answer out of the kernel buffer on the probe thread —
   this file, the controller battery, the mic button and the clock table —
   so what limits a read has to be generous: a refusal silences all four at
   once. The ring measured about half a megabyte on the tested console; the cap
   is only a guard against a nonsense size, not a tuning knob. It used to be
   exactly 512 KiB, and a ring of that size or larger could never be read at
   all (the kernel appends a terminating NUL to its answer, so the buffer has to
   be one byte bigger than the ring). */
#define MSGBUF_MAX        (4 * 1024 * 1024)
/* What to ask for when the size query itself goes unanswered. */
#define MSGBUF_GUESS      (256 * 1024)

/* An application id carries a generation above its low 13 bits. Measured on
   24.09.2026, FW 12.00: the shell held the pad as 0x7 until the console went
   into rest mode, and as 0x2007 after it came back; four game launches in the
   same session were 0x18, 0x2018, 0x4018 and 0x6018. Only the part below the
   generation says what holds the pad, so the shell is compared there — an
   exact 0x7 turned the home screen into a running game after every rest. */
#define APP_ID_SLOT_MASK  0x1FFFu
#define SHELL_FOCUS_ID    0x7u

static ps5tm_gamestate_t g_cache;
static uint64_t          g_cache_ms = 0;
static uint64_t          g_cache_mono_ms = 0;     /* the same moment, monotonic */
static pthread_mutex_t   g_lock = PTHREAD_MUTEX_INITIALIZER;

/* The newest focus change seen by any pass — see "remembered" in refresh().
   Only the probe thread touches these, like refresh() itself. */
static unsigned g_last_focus      = 0;
static int      g_have_last_focus = 0;


/* A PS5 title id is four capitals followed by five digits: PPSA01650. */
static int
is_title_id(const char *s) {
  for(int i = 0; i < 4; i++) if(s[i] < 'A' || s[i] > 'Z') return 0;
  for(int i = 4; i < 9; i++) if(s[i] < '0' || s[i] > '9') return 0;
  return 1;
}


/* Every reader walks the buffer as one C string, so a NUL byte anywhere hides
 * every line behind it — and the kernel hands out a ring that has not wrapped
 * yet with its unwritten part first, as NULs. For the PS-button count it is
 * worse than a blind spot: as such a stretch scrolls out of the ring the lines
 * behind it come into view all at once, the count jumps, and a jump is read as
 * a press. A newline is the neutral replacement: it ends a line and matches no
 * marker. */
static void
scrub_nuls(char *buf, size_t len) {
  for(char *p = buf; (p = memchr(p, 0, (size_t)(buf + len - p))) != NULL; p++)
    *p = '\n';
}


#ifndef PS5TM_HOST_TEST
/* Said once, with the reason: nothing else records that four features have
   gone quiet together, and "kein Spiel" looks exactly like a console that has
   no game running. */
static void
msgbuf_log_failure(int eno, size_t size) {
  static int logged = 0;
  if(__atomic_exchange_n(&logged, 1, __ATOMIC_RELAXED)) return;
  PS5TM_WARN("msgbuf_unreadable",
             "Der Kernel-Meldungspuffer lässt sich nicht lesen (Fehler %d: %s, "
             "Größe %zu Bytes) – Spielerkennung, Mikrofon-Taste, Controller-Akku und "
             "Taktangaben bleiben leer.", eno, strerror(eno), size);
}
#endif


/* Shared with dualsense.c, which reads the controller's battery level out of
   the same buffer, and with clocks.c and micbutton.c. Caller frees. */
char *
ps5tm_msgbuf_read(size_t *len_out) {
#ifdef PS5TM_HOST_TEST
  /* Host builds have no kernel message buffer; a captured sample can be
     dropped in via PS5TM_MSGBUF so the parser itself stays testable. */
  {
    const char *path = getenv("PS5TM_MSGBUF");
    if(!path) return NULL;
    FILE *f = fopen(path, "rb");
    if(!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if(n <= 0 || n > MSGBUF_MAX) { fclose(f); return NULL; }
    char *buf = malloc((size_t)n + 1);
    if(!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, (size_t)n, f);
    scrub_nuls(buf, got);
    buf[got] = 0;
    fclose(f);
    *len_out = got;
    return buf;
  }
#else
  int    eno  = 0;
  size_t need = 0;

  for(int attempt = 0; attempt < 3; attempt++) {
    need = 0;
    if(sysctlbyname("kern.msgbuf", NULL, &need, NULL, 0) != 0 || need == 0)
      need = MSGBUF_GUESS;
    if(need > MSGBUF_MAX) { eno = EFBIG; break; }

    /* Room for the NUL the kernel appends, and for what is written between the
       size query and the read. */
    size_t cap = need + need / 8 + 4096;
    char  *buf = malloc(cap + 1);
    if(!buf) { eno = ENOMEM; break; }

    size_t got = cap;
    int    rc  = sysctlbyname("kern.msgbuf", buf, &got, NULL, 0);
    if(rc == 0 && got > 0) {
      scrub_nuls(buf, got);
      buf[got] = 0;
      *len_out = got;
      return buf;
    }

    eno = (rc != 0) ? errno : EIO;
    free(buf);

    /* ENOMEM: the ring outgrew the buffer. The kernel hands it out oldest line
       first, so settling for what fitted would keep the head and lose exactly
       the newest lines — the only ones any reader cares about. Ask again with
       a fresh size instead. Any other error will not get better by asking. */
    if(eno != ENOMEM) break;
  }

  msgbuf_log_failure(eno, need);
  return NULL;
#endif
}


/* Reads the title's display name and version out of one param.json. */
static int
read_title_meta(const char *path, char *name, size_t name_len,
                char *ver, size_t ver_len) {
  FILE *f = fopen(path, "r");
  if(!f) return -1;

  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  if(n <= 0 || n > 262144) { fclose(f); return -1; }

  char *raw = malloc((size_t)n + 1);
  if(!raw) { fclose(f); return -1; }
  size_t got = fread(raw, 1, (size_t)n, f);
  raw[got] = 0;
  fclose(f);

  cJSON *root = cJSON_Parse(raw);
  free(raw);
  if(!root) return -1;

  const cJSON *v = cJSON_GetObjectItem(root, "contentVersion");
  if(!cJSON_IsString(v)) v = cJSON_GetObjectItem(root, "originContentVersion");
  if(cJSON_IsString(v) && v->valuestring[0])
    snprintf(ver, ver_len, "%s", v->valuestring);

  const cJSON *loc = cJSON_GetObjectItem(root, "localizedParameters");
  if(cJSON_IsObject(loc)) {
    const cJSON *def  = cJSON_GetObjectItem(loc, "defaultLanguage");
    const cJSON *pick = NULL;

    if(cJSON_IsString(def)) pick = cJSON_GetObjectItem(loc, def->valuestring);
    if(!pick)               pick = cJSON_GetObjectItem(loc, "en-US");
    if(!pick) {
      /* Fall back to whichever localisation the package happens to carry. */
      const cJSON *it;
      cJSON_ArrayForEach(it, loc) {
        if(cJSON_IsObject(it)) { pick = it; break; }
      }
    }

    const cJSON *nm = pick ? cJSON_GetObjectItem(pick, "titleName") : NULL;
    if(cJSON_IsString(nm) && nm->valuestring[0])
      snprintf(name, name_len, "%s", nm->valuestring);
  }
  cJSON_Delete(root);
  return name[0] ? 0 : -1;
}


/* The same for a PS4 title, whose metadata is an SFO rather than JSON.
 *
 * Measured on 25.09.2026: /system_data/priv/appmeta/CUSA08519/ holds
 * param.sfo, nptitle.dat and npbind.dat, and /user/app/CUSA08519/ only the
 * package. With param.json as the only route, every PS4 game — Red Dead
 * Redemption 2 here — was shown by its id.
 *
 * The format is small and fixed: a 20-byte header ("\0PSF", version, offset
 * of the key table, offset of the data table, entry count), then one 16-byte
 * index entry per key. Every offset taken from the file is checked against
 * its size before use; the file sits on the system partition, but a wrong
 * number must still not turn into a read past the buffer. */
#define SFO_MAX_BYTES    (64 * 1024)
#define SFO_FMT_UTF8_RAW 0x0004   /* UTF-8, not terminated */
#define SFO_FMT_UTF8     0x0204   /* UTF-8, NUL-terminated */

static uint32_t
sfo_le32(const unsigned char *p) {
  return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
         (uint32_t)p[3] << 24;
}

/* Copies one text value without its padding. A value too long for dst is cut
   before the character it would split, so "Gran Turismo™ 7" can never end in
   half a ™. */
static void
sfo_copy(char *dst, size_t dst_len, const unsigned char *src, size_t len) {
  while(len > 0 && src[len - 1] == 0) len--;
  if(len >= dst_len) {
    len = dst_len - 1;
    while(len > 0 && (src[len] & 0xC0) == 0x80) len--;
  }
  memcpy(dst, src, len);
  dst[len] = 0;
}

static int
read_title_sfo(const char *path, char *name, size_t name_len,
               char *ver, size_t ver_len) {
  FILE *f = fopen(path, "rb");
  if(!f) return -1;

  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  if(n < 20 || n > SFO_MAX_BYTES) { fclose(f); return -1; }

  unsigned char *b = malloc((size_t)n);
  if(!b) { fclose(f); return -1; }
  size_t size = fread(b, 1, (size_t)n, f);
  fclose(f);

  if(size < 20 || memcmp(b, "\0PSF", 4) != 0) { free(b); return -1; }

  uint32_t keys  = sfo_le32(b + 8);
  uint32_t data  = sfo_le32(b + 12);
  uint32_t count = sfo_le32(b + 16);
  if(count > (size - 20) / 16 || keys < 20 + count * 16 ||
     keys > size || data > size) {
    free(b);
    return -1;
  }

  for(uint32_t i = 0; i < count; i++) {
    const unsigned char *e = b + 20 + i * 16;
    uint32_t koff = (uint32_t)e[0] | (uint32_t)e[1] << 8;
    uint32_t fmt  = (uint32_t)e[2] | (uint32_t)e[3] << 8;
    uint32_t len  = sfo_le32(e + 4);
    uint32_t doff = sfo_le32(e + 12);

    if(fmt != SFO_FMT_UTF8 && fmt != SFO_FMT_UTF8_RAW) continue;
    if(koff >= size - keys || doff > size - data) continue;
    if(len > size - data - doff) continue;

    const char *key = (const char *)b + keys + koff;
    if(!memchr(key, 0, size - keys - koff)) continue;   /* unterminated */

    if(!strcmp(key, "TITLE"))
      sfo_copy(name, name_len, b + data + doff, len);
    else if(!strcmp(key, "APP_VER"))
      sfo_copy(ver, ver_len, b + data + doff, len);
  }

  free(b);
  return name[0] ? 0 : -1;
}


/* Looks the title up wherever its metadata may live.
 *
 * The system's own appmeta directory comes first: it covers every installed
 * title including those on an external drive, where /user/app is empty. The
 * in-package copy stays as a fallback for anything appmeta does not know.
 * PS4 titles have no param.json anywhere, so their SFO comes last. */
static void
resolve_title_meta(const char *title_id, char *name, size_t name_len,
                   char *ver, size_t ver_len) {
  const char *patterns[] = {
    "/system_data/priv/appmeta/%s/param.json",
    "/user/app/%s/sce_sys/param.json",
    "/mnt/ext1/user/app/%s/sce_sys/param.json",
  };

  char path[160];
  for(unsigned i = 0; i < sizeof(patterns) / sizeof(patterns[0]); i++) {
    snprintf(path, sizeof(path), patterns[i], title_id);
    if(read_title_meta(path, name, name_len, ver, ver_len) == 0) return;
  }

  snprintf(path, sizeof(path), "/system_data/priv/appmeta/%s/param.sfo",
           title_id);
  read_title_sfo(path, name, name_len, ver, ver_len);
}


/* There used to be a second, "better" route here: ask the system directly via
 * sceSystemServiceGetAppIdOfRunningBigApp(), the way the shell does. It was
 * removed on 01.08.2026 because it kills the payload.
 *
 * It only ever misbehaves when there is genuinely a game to find. With the
 * console idle the call returns "nothing running" and everything looks fine,
 * which is why it survived review and weeks of testing. Start a title and the
 * app was dead within five seconds — reproduced deliberately by enabling the
 * probes one at a time while a game ran, with the console answering right up
 * to the moment this one was switched on.
 *
 * The call goes through LNC and IPMI, and this console complains about that
 * subsystem before our first line of output even appears:
 *   [SceLncUtil] getAppStatus: LNC_ISOK::0x80940004
 *
 * The message-buffer route below is a plain sysctl read of kern.msgbuf. It
 * cannot contend with a running title over anything, and it was verified to
 * work. Slightly less precise, and entirely alive — which beats precise.
 *
 * Do not reintroduce the service call without a way to test it against a
 * running game. */
static void
refresh(ps5tm_gamestate_t *st) {
  memset(st, 0, sizeof(*st));
  st->state  = "unbekannt";
  st->source = "none";

  size_t len = 0;
  char  *buf = ps5tm_msgbuf_read(&len);
  if(!buf) {
    st->state = "nicht ermittelbar";
    return;
  }

  /* Walk the whole buffer keeping the most recent hit for each marker; the
     message buffer is chronological, so the last one wins. */
  unsigned app_id = 0, focus_id = 0;
  int      have_focus = 0;
  char     title[16] = {0};

  /* Switch on the first character before comparing.
   *
   * This is the hottest loop in the app: half a megabyte, every four seconds,
   * and the plain form ran three strncmp() across every byte of it. Testing
   * one character first skips all three for the overwhelming majority and
   * cannot change the result — a pattern only matches where its own first
   * character sits. */
  for(const char *p = buf; *p; p++) {
    switch(*p) {
      case 'a':
        if(!strncmp(p, "appId=0x", 8) && !ps5tm_klog_own_line(buf, p)) {
          unsigned v = (unsigned)strtoul(p + 8, NULL, 16);
          if(v) app_id = v;
        }
        break;
      case 'S':
        if(!strncmp(p, "SetControllerFocus(", 19) && !ps5tm_klog_own_line(buf, p)) {
          const char *a = p + 19;
          /* "(-1)" is the instant between two owners, when nobody holds the
             pad; the real owner follows within the same millisecond. Read as
             a number it came out as 0xFFFFFFFF — not the shell, therefore a
             "game" — so a pass landing in that instant reported one. */
          if(a[0] != '-') {
            char         *end = NULL;
            unsigned long v   = strtoul(a, &end,
                                        (a[0] == '0' && a[1] == 'x') ? 16 : 10);
            /* Only a finished number counts. A read can land in the middle of
               a line the kernel is still writing, and the end of the buffer
               then reads "SetControllerFocus(" or "SetControllerFocus(0x20":
               as a number that was 0 or half an id — for one pass the game
               was "geladen" or gone, and the half id was remembered below.
               Real lines close the bracket right behind the number:
               SetControllerFocus(0x00008018). */
            if(end != a && *end == ')') {
              focus_id   = (unsigned)v;
              have_focus = 1;
            }
          }
        }
        break;
      case '/':
        if(!strncmp(p, "/user/app/", 10) && is_title_id(p + 10) && !ps5tm_klog_own_line(buf, p)) {
          memcpy(title, p + 10, 9);
          title[9] = 0;
        }
        break;
      default:
        break;
    }
  }
  free(buf);

  /* The buffer is a ring, and during a game it fills fast: FW 12.00 logs a
   * memory report for every running process every two seconds, about 290
   * bytes a second in all. Measured on 24.09.2026, the focus change that
   * started a game had scrolled out of the 512 KB after 30 minutes, and from
   * then on the game "was not running" while it ran — its per-title profile
   * switched off with it.
   *
   * Remembering the last focus loses nothing. The pad does not change hands
   * without a new line, and the newest line is always the last to scroll out,
   * so a buffer without one means "no change since the one already seen".
   * Whether the remembered holder still exists is checked below exactly like
   * a fresh one. */
  int remembered = 0;
  if(have_focus) {
    g_last_focus      = focus_id;
    g_have_last_focus = 1;
  } else if(g_have_last_focus) {
    focus_id   = g_last_focus;
    have_focus = 1;
    remembered = 1;
  }

  st->app_id           = app_id;
  st->focus_id         = focus_id;
  st->focus_remembered = remembered;

  /* The controller focus decides, not the title string.
   *
   * Until 01.08.2026 a missing title meant "kein Spiel" and nothing else was
   * looked at. On this firmware the title never arrives: with a game running,
   * the buffer held 150 "appId=" lines and three "SetControllerFocus(...)",
   * and not one "/user/app/<TITLEID>". Confirmed twice over — once by the app
   * finding no title, once by reading the kernel buffer through klogsrv. So
   * the app reported "kein Spiel" while the console sat at 76 °C under load.
   *
   * "appId=" is no basis for the decision either. The loop above keeps the
   * last one in the buffer, and that was 0x19 — some system app — while the
   * game was 0x2018. The old focus_id == app_id test would have failed even
   * with a title present.
   *
   * What can be trusted is the focus: whoever holds the controller is in the
   * foreground, and anything that is not the shell is a candidate title —
   * confirmed below by asking the kernel whether it really is a game. The
   * title string stays what it always was, a label rather than the evidence.
   *
   * The title string stays a label rather than the evidence — but it no
   * longer has to come from this buffer at all; see below. */
  int shell_focus = have_focus &&
                    (focus_id & APP_ID_SLOT_MASK) == SHELL_FOCUS_ID;
  int game_focus  = have_focus && focus_id != 0 && !shell_focus;

  /* No title and the pad with the shell is also what a running game looks
   * like: one paused in the background once its title has scrolled out of the
   * buffer, or one the shell started behind the console's browser. On
   * 03.10.2026 this said "kein Spiel" for both — Tetris paused, then Offroad
   * Racing — and the start of another game went ahead without the question,
   * while the shell closed the paused one itself. So the kernel below is asked
   * whenever it can answer; the buffer alone decides only without it. */
  if(!title[0] && !game_focus && !ps5tm_procmgr_appinfo_ready()) {
    st->state = "kein Spiel";
    return;
  }

  /* Ask the kernel which game is running — and whether one is at all.
   *
   * sceKernelGetAppInfo() carries the title id next to the app id, and it is
   * a kernel call — not the LNC/IPMI service that killed the payload within
   * seconds of a game starting. Found on 01.08.2026 in ShadowMountPlus, which
   * does exactly this and runs on this console. The focus id doubles as the
   * application id, so it selects the right process; without a game holding
   * the pad, any resident game is taken.
   *
   * Where the kernel can answer, it decides — the buffer only remembers. A
   * focus id may belong to the store, the settings or the shell under an id
   * not seen yet, and a remembered focus to a game that has ended since. A
   * title from the buffer is worse still. The note of 01.08. had never seen
   * "/user/app/<TITLEID>" there, but on 25.09.2026 Double Dragon Revive
   * (PPSA23000) left one — and it stays until it scrolls out, long after the
   * game has closed: the game ended at 14:49, and six minutes later the app
   * still reported it "pausiert". On 24.09. the missing kernel check had let
   * the home screen pass for a game called "Unbekannter Titel (0x2007)".
   *
   * Without a live game process nothing is running. The buffer's title and
   * the bare focus id remain the fallback for consoles where the kernel route
   * is missing. */
  if(ps5tm_procmgr_appinfo_ready()) {
    char kt[16] = {0};
    if(ps5tm_procmgr_game_title(game_focus ? focus_id : 0, kt, sizeof(kt)) != 0
       || !kt[0]) {
      st->state = "kein Spiel";
      return;
    }
    snprintf(title, sizeof(title), "%s", kt);
    st->source = "appinfo";
  }

  if(title[0]) {
    snprintf(st->title_id, sizeof(st->title_id), "%s", title);
    /* Only if the kernel route above did not already claim it — knowing which
       source answered is what let the earlier wrong assumptions be found. */
    if(!st->source || strcmp(st->source, "appinfo") != 0)
      st->source = "msgbuf";
    resolve_title_meta(title, st->title_name, sizeof(st->title_name),
                       st->title_version, sizeof(st->title_version));
    if(!st->title_name[0])
      snprintf(st->title_name, sizeof(st->title_name), "%s", title);
  } else {
    /* Running, but nothing in the buffer says which title. Name it after the
       id rather than inventing something. */
    st->source = "msgbuf-focus";
    snprintf(st->title_name, sizeof(st->title_name),
             "Unbekannter Titel (0x%X)", focus_id);
  }

  if(shell_focus) {
    st->state      = "pausiert";      /* user is back on the home screen */
    st->foreground = 0;
    st->suspended  = 1;
  } else if(game_focus) {
    st->state      = "läuft";
    st->foreground = 1;
  } else {
    st->state      = "geladen";       /* resident, no decisive focus event */
    st->foreground = 0;
  }
}


/* Background thread only — refresh() calls into the shell services, which are
   free to take as long as they like. See probe.c. */
void
ps5tm_gamestate_refresh(void) {
  ps5tm_gamestate_t fresh;
  refresh(&fresh);

  pthread_mutex_lock(&g_lock);
  g_cache    = fresh;
  g_cache_ms = ps5tm_now_ms();
  g_cache_mono_ms = ps5tm_mono_ms();
  pthread_mutex_unlock(&g_lock);
}


void
ps5tm_gamestate_get(ps5tm_gamestate_t *out) {
  pthread_mutex_lock(&g_lock);
  if(g_cache_ms == 0) {
    /* Nothing measured yet: say so rather than blocking until it is. */
    memset(out, 0, sizeof(*out));
    out->state  = "wird ermittelt";
    out->source = "none";
  } else {
    *out = g_cache;
  }
  pthread_mutex_unlock(&g_lock);
}


/* "Nothing measured yet" and "nothing in front" both come back from
   ps5tm_gamestate_get() with foreground 0. Whoever must not act while a game
   might be in front — the display probe — has to be able to tell them apart:
   with the game probe switched off nothing is ever measured, and the second
   reading is a guess. */
int
ps5tm_gamestate_measured(void) {
  pthread_mutex_lock(&g_lock);
  int measured = (g_cache_ms != 0);
  pthread_mutex_unlock(&g_lock);
  return measured;
}


/* The monotonic clock, not the wall clock: the console's own clock can be set
   or corrected at any moment, and an age that jumps with it would make a fresh
   reading look stale, or a stale one fresh. */
uint64_t
ps5tm_gamestate_age_ms(void) {
  pthread_mutex_lock(&g_lock);
  uint64_t at = g_cache_mono_ms;
  pthread_mutex_unlock(&g_lock);
  if(!at) return UINT64_MAX;
  uint64_t now = ps5tm_mono_ms();
  return now > at ? now - at : 0;
}
