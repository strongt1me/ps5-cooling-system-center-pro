/* PS5 Cooling & System Center - Pro: a game the app mounted through ShadowMountPlus and nobody started (14.11.2026).
 *
 * ShadowMountPlus 1.7beta12 has one place for a "prepared" runtime mount (claim_prepared_title in sm_shellcore_service.c). A
 * game mounted by POST /games/mount keeps it until it is started, unmounted, or ShadowMountPlus restarts; every other launch,
 * a game or a tile, is refused meanwhile with EBUSY ("launch mount unavailable ... Device busy", on the console the launch
 * error dialog). Reproduced on 14.11.2026: after the "Einhängen" button of the app, both tiles would not start; "Aushängen"
 * and they did. The first time it was left standing for 4.5 hours, after the rollback of an FPKG test.
 *
 * So the app does not leave such a mount standing. Every game it mounts through ShadowMountPlus (the buttons of the game card,
 * the probe that reads the fakelibs, the AMPR job: all go through ps5tm_smp_call) is written down here, in memory and in a small
 * file (so that a restart of the app, for instance a new ELF, does not forget it). The person must not have to know any of this, and
 * must not wait ("wenn nichts mehr startet, denken die Leute, etwas sei kaputt", 14.11.2026), so three things release the game:
 *  - the app itself starts a title (ps5tm_library_start): every other game it holds is unmounted first, then the start goes through;
 *  - the console refuses to start another title (ps5tm_smphold_check_launch): the kernel log says "LaunchFlowError.<id>" when the
 *    refusal comes; the held games are unmounted within a second and the person gets a notice to start again (what the console
 *    showed was only an error dialog). Seen on the test console: the failed start leaves launchApp(<id>) and that line;
 *  - nobody did anything: a game still mounted and not started PS5TM_SMPHOLD_GRACE_MS after the app mounted it is unmounted, with a
 *    notice on the console and a line in the log. The time only runs while no game plays and none of the app's own jobs runs.
 * Nothing the app did not mount is touched: another way of mounting (ShadowMountPlus's own pages, or its persistent image mounts) is
 * the person's own business. */
#include "ps5tm.h"
#include "smphold.h"
#include "third_party/cJSON.h"
#include "titleid.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifndef __linux__
#include <sys/sysctl.h>
#include <sys/time.h>
#endif

#define HOLD_MAX      8
#define HOLD_PATH     PS5TM_DATA_DIR "/smp-mounted.txt"
#define HOLD_TMP      PS5TM_DATA_DIR "/smp-mounted.txt.part"
#define HOLD_RETRY_MS 10000u       /* after a refusal, the next try comes this much later ... */
#define HOLD_RETRY_MAX_MS (5u * 60u * 1000u)   /* ... twice as late each time, never later than this */
#define HOLD_STATE_AGE_MS 15000u   /* a reading of the game state older than this says nothing */
#define HOLD_SMP_GONE_MS  (10u * 60u * 1000u)  /* ShadowMountPlus silent this long: it was restarted or stopped, what it held is gone */

typedef struct {
  char     id[12];
  uint64_t since_ms;               /* the clock only runs while nothing keeps it back */
  uint64_t retry_at;               /* after a refused unmount: not before this; 0 none */
  unsigned fails;                  /* refused unmounts in a row (the retry gets later, the log only the first) */
} hold_t;

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_rel_lock = PTHREAD_MUTEX_INITIALIZER;   /* one release at a time; taken before g_lock, never inside it */
static hold_t          g_hold[HOLD_MAX];
static int             g_n;
static int             g_up;
static uint64_t        g_smp_gone_since;                          /* when ShadowMountPlus stopped answering; 0 while it does */

/* The real world; a test swaps it (ps5tm_smphold_test_env). */
static ps5tm_smphold_env_t g_env;

static int
env_find(const char *id, int *mounted) {
  ps5tm_smp_game_t g;
  if(ps5tm_smp_find(id, &g) != 0) return -1;
  *mounted = g.mounted;
  return 0;
}

static int
env_smp_up(void) {
  return ps5tm_smp_available(NULL, 0);
}

/* Only a fresh reading that says "kein Spiel" frees the clock. Not known yet, a reading that is old (the game probe was switched off, or
   is stuck), a title that is running but unnamed (the state is then not "kein Spiel", the id empty), and a state that cannot be told
   ("nicht ermittelbar") all count as a game playing: the clock waits, nothing is unmounted under it. */
static int
env_game_running(void) {
  if(!ps5tm_gamestate_measured()) return 1;
  if(ps5tm_gamestate_age_ms() > HOLD_STATE_AGE_MS) return 1;
  ps5tm_gamestate_t gs;
  ps5tm_gamestate_get(&gs);
  if(gs.title_id[0]) return 1;
  return !gs.state || strcmp(gs.state, "kein Spiel") != 0;
}

/* Is this very title the one that runs? Then what the app held is done: the person started it. */
static int
env_title_running(const char *id) {
  if(ps5tm_procmgr_title_running(id) == 1) return 1;       /* an own tile never counts as the game of the game state (audit 16.11.2026) */
  if(!ps5tm_gamestate_measured() || ps5tm_gamestate_age_ms() > HOLD_STATE_AGE_MS) return 0;
  ps5tm_gamestate_t gs;
  ps5tm_gamestate_get(&gs);
  return gs.title_id[0] && !strcmp(gs.title_id, id);
}

/* The boot of the console (kern.boottime, seconds): what the file was written under; another boot, another ShadowMountPlus. 0: not known. */
static uint64_t
env_boot_id(void) {
#ifdef __linux__
  return 0;
#else
  struct timeval bt;
  size_t sz = sizeof(bt);
  if(sysctlbyname("kern.boottime", &bt, &sz, NULL, 0) != 0) return 0;
  return (uint64_t)bt.tv_sec;
#endif
}

static int
env_job_running(void) {
  char why[200];
  return ps5tm_app_job_running(why, sizeof(why));
}

static int
env_unmount(const char *id, char *msg, size_t n) {
  return ps5tm_smpremount(id, "unmount", msg, n);
}

/* What the person knows a title by: its name from the list, the app for its own tiles, else the id. */
static void
name_of(const char *id, char *out, size_t n) {
  if(ps5tm_is_own_tile(id)) {
    snprintf(out, n, "PS5 Cooling & System Center");
    return;
  }
  char link[96];
  if(ps5tm_library_launch_info(id, out, n, link, sizeof(link)) != 0) snprintf(out, n, "%s", id);
}

static void
env_notice(const char *held, const char *other) {
  char hn[128], on[128];
  name_of(held, hn, sizeof(hn));
  if(other && other[0]) {
    name_of(other, on, sizeof(on));
    ps5tm_notify_plain("Der Start von „%s“ ist gescheitert. Das eingehängte „%s“ kann der Grund sein: Die App hat es ausgehängt. Bitte den Start noch einmal versuchen.",
                       on, hn);
  } else {
    ps5tm_notify_plain("„%s“ war eingehängt, aber nicht gestartet: Die App hat es ausgehängt, damit andere Spiele und Kacheln starten.", hn);   /* after the grace */
  }
}

/* The console's refusal to start a title leaves "LaunchFlowError.<id>" in the kernel log (the error dialog's scene). The cursor is the
   number of the last line looked at; only the watcher thread comes here, so the buffer is not guarded. */
static uint64_t g_klog_after;
static ps5tm_klog_line_t g_klog_lines[32];

/* Nine capitals and digits: a title id, the app's own tiles included (a refused start of a tile is just what happened on 14.11.). */
static int
tid9_ok(const char *id) {
  if(!id || strlen(id) != 9) return 0;
  for(int i = 0; i < 9; i++)
    if(!((id[i] >= 'A' && id[i] <= 'Z') || (id[i] >= '0' && id[i] <= '9'))) return 0;
  return 1;
}

static int
env_launch_failed(int reset, char *id, size_t n) {
  uint64_t newest = 0;
  int more = 0, lost = 0;
  if(reset || !g_klog_after) {                         /* a mark that did not take (the log could not be read then) is made again */
    ps5tm_klog_fetch(0, 1, g_klog_lines, &newest, &more, &lost);
    g_klog_after = newest;
    return 0;
  }
  for(int round = 0; round < 4; round++) {
    unsigned got = ps5tm_klog_fetch(g_klog_after, 32, g_klog_lines, &newest, &more, &lost);
    for(unsigned i = 0; i < got; i++) {
      g_klog_after = g_klog_lines[i].seq;
      if(g_klog_lines[i].marker) continue;
      /* Only the shell's own lines count: the app's lines ("<118>[ps5tm.elf] ...") land in the same buffer and carry names and
         other text that nobody must be able to turn into a refused start. */
      if(strncmp(g_klog_lines[i].text, "[SceShellUI]", 12) != 0) continue;
      const char *p = strstr(g_klog_lines[i].text, "LaunchFlowError.");
      if(!p) continue;
      p += 16;
      char t[10] = "";
      if(strlen(p) >= 9) { memcpy(t, p, 9); t[9] = 0; }
      if(tid9_ok(t) && n > 9) {
        memcpy(id, t, 10);
        return 1;
      }
    }
    if(!more) break;
  }
  return 0;
}

static int
env_start_title(const char *id) {
  return ps5tm_library_start(id);
}

/* The home screen, as the PS button does (ShellUI "navigateToHome"; the browser's close of library.c): the error dialog of the refused start is a scene
   of the shell, and the person had to confirm it before the tile that was started again showed (user, 16.11.2026). A moment is given to the shell
   to finish before the start follows. */
static void
env_go_home(void) {
#ifndef PS5TM_HOST_TEST
  if(ps5tm_library_close_browser() == 0) usleep(1200 * 1000);
#endif
}

/* The lines of ShadowMountPlus's log (POST /api/v1/debug-log, read only) that speak of a start that was refused: "launch mount unavailable",
   "Device busy", the title. The last few, joined, for one line of the app's log, so that the reason of a refused start is in the app and
   not only on the screen of a diagnosis tool. */
static int
env_smp_excerpt(const char *id, char *out, size_t n) {
  if(!n) return 0;
  out[0] = 0;
  if(!ps5tm_smp_available(NULL, 0)) return 0;
  char err[120] = "";
  cJSON *ans = ps5tm_smp_call("/debug-log", NULL, 1500, err, sizeof(err));
  if(!ans) return 0;
  const cJSON *c = cJSON_GetObjectItem(ans, "content");
  if(cJSON_IsString(c) && c->valuestring) {
    const char *txt = c->valuestring;
    const char *lines[6];
    size_t lens[6];
    int keep = 0, w = 0;
    for(const char *p = txt; *p;) {
      const char *e = strchr(p, '\n');
      size_t l = e ? (size_t)(e - p) : strlen(p);
      char buf[300];
      size_t m = l < sizeof(buf) - 1 ? l : sizeof(buf) - 1;
      memcpy(buf, p, m);
      buf[m] = 0;
      if(strstr(buf, "unavailable") || strstr(buf, "Device busy") || (id && id[0] && strstr(buf, id) && strstr(buf, "launch"))) {
        lines[w % 6] = p;
        lens[w % 6] = l;
        w++;
        if(keep < 6) keep++;
      }
      if(!e) break;
      p = e + 1;
    }
    size_t used = 0;
    for(int k = 0; k < keep; k++) {
      int idx = (w - keep + k) % 6;
      size_t l = lens[idx] > 150 ? 150 : lens[idx];
      if(used + l + 4 >= n) break;
      if(used) { memcpy(out + used, " | ", 3); used += 3; }
      memcpy(out + used, lines[idx], l);
      used += l;
      out[used] = 0;
    }
  }
  cJSON_Delete(ans);
  return out[0] != 0;
}

static void
env_default(void) {
  g_env.find = env_find;
  g_env.smp_up = env_smp_up;
  g_env.game_running = env_game_running;
  g_env.job_running = env_job_running;
  g_env.unmount = env_unmount;
  g_env.now_ms = ps5tm_mono_ms;
  g_env.notice = env_notice;
  g_env.launch_failed = env_launch_failed;
  g_env.start_title = env_start_title;
  g_env.go_home = env_go_home;
  g_env.smp_excerpt = env_smp_excerpt;
  g_env.title_running = env_title_running;
  g_env.boot_id = env_boot_id;
}

/* An id as ShadowMountPlus manages them: PPSA, CUSA, or a homebrew's FAKE and the like (nine letters and digits). */
static int
id_ok(const char *id) {
  if(!id || strlen(id) != 9) return 0;
  for(int i = 0; i < 9; i++)
    if(!((id[i] >= 'A' && id[i] <= 'Z') || (id[i] >= '0' && id[i] <= '9'))) return 0;
  return !ps5tm_is_own_tile(id);
}

static void
save_locked(void) {
  char buf[HOLD_MAX * 16 + 40];
  size_t k = 0;
  uint64_t boot = g_env.boot_id ? g_env.boot_id() : 0;
  if(boot) k += (size_t)snprintf(buf + k, sizeof(buf) - k, "boot=%llu\n", (unsigned long long)boot);   /* what the ids were written under */
  for(int i = 0; i < g_n; i++) k += (size_t)snprintf(buf + k, sizeof(buf) - k, "%s\n", g_hold[i].id);
  if(g_n == 0) {
    unlink(HOLD_PATH);
    return;
  }
  int fd = open(HOLD_TMP, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if(fd < 0) {
    PS5TM_WARN("smp_hold_save", "Die Haltdatei ließ sich nicht schreiben: %s", strerror(errno));
    return;
  }
  ssize_t w = write(fd, buf, k);
  int ok = w == (ssize_t)k;
  if(ok) fsync(fd);
  if(close(fd) != 0) ok = 0;
  if(ok && rename(HOLD_TMP, HOLD_PATH) != 0) ok = 0;
  if(!ok) {
    PS5TM_WARN("smp_hold_save", "Die Haltdatei ließ sich nicht schreiben: %s", strerror(errno));
    unlink(HOLD_TMP);
  }
}

static int
index_of_locked(const char *id) {
  for(int i = 0; i < g_n; i++)
    if(!strcmp(g_hold[i].id, id)) return i;
  return -1;
}

static void
drop_locked(int i) {
  g_hold[i] = g_hold[--g_n];
}

void
ps5tm_smphold_note(const char *id, int mounted) {
  if(!id_ok(id)) return;
  if(!g_env.now_ms) env_default();
  pthread_mutex_lock(&g_lock);
  int i = index_of_locked(id);
  int changed = 0;
  if(mounted) {
    if(i < 0 && g_n < HOLD_MAX) {
      memcpy(g_hold[g_n].id, id, 9);                   /* id_ok: nine letters and digits */
      g_hold[g_n].id[9] = 0;
      i = g_n++;
      changed = 1;
    } else if(i < 0) {
      PS5TM_WARN("smp_hold_full", "%s wurde eingehängt, aber die App kann nicht mehr als %d eingehängte Spiele im Auge behalten: es wird nicht von selbst ausgehängt.", id, HOLD_MAX);
    }
    if(i >= 0) {
      g_hold[i].since_ms = g_env.now_ms();             /* mounted afresh: the clock, and what a refusal had made later, start over */
      g_hold[i].retry_at = 0;
      g_hold[i].fails = 0;
    }
  } else if(i >= 0) {
    drop_locked(i);
    changed = 1;
  }
  if(changed) save_locked();
  pthread_mutex_unlock(&g_lock);
}

/* What the app wrote down before a restart: the clock starts again at the start. */
static void
load(void) {
  FILE *f = fopen(HOLD_PATH, "r");
  if(!f) return;
  char line[48];
  uint64_t now = g_env.now_ms();
  uint64_t boot = g_env.boot_id ? g_env.boot_id() : 0;
  pthread_mutex_lock(&g_lock);
  while(fgets(line, sizeof(line), f) && g_n < HOLD_MAX) {
    line[strcspn(line, "\r\n")] = 0;
    if(!strncmp(line, "boot=", 5)) {
      /* Written under another boot of the console: ShadowMountPlus started afresh since, what the app had mounted is gone, and a game
         it mounts itself now is not the app's to take out. (A second of difference is the clock's rounding.) */
      unsigned long long saved = strtoull(line + 5, NULL, 10);
      if(boot && saved && (saved > boot ? saved - boot : boot - saved) > 5) {
        g_n = 0;
        break;
      }
      continue;
    }
    if(!id_ok(line) || index_of_locked(line) >= 0) continue;
    memcpy(g_hold[g_n].id, line, 9);                   /* id_ok: nine letters and digits */
    g_hold[g_n].id[9] = 0;
    g_hold[g_n].since_ms = now;
    g_hold[g_n].retry_at = 0;
    g_hold[g_n].fails = 0;
    g_n++;
  }
  if(g_n == 0) unlink(HOLD_PATH);                      /* nothing worth keeping (another boot, or only wrong lines) */
  pthread_mutex_unlock(&g_lock);
  fclose(f);
}

int
ps5tm_smphold_count(void) {
  pthread_mutex_lock(&g_lock);
  int n = g_n;
  pthread_mutex_unlock(&g_lock);
  return n;
}

/* Why a held game is let go; the log line says it (three fixed sentences, so the log page can translate them). */
enum { REL_GRACE, REL_REFUSED, REL_START };

/* Is this entry still written down, and (after the grace) is its time really up? Looked at again right before the unmount, under the
   release lock: the person may have mounted it afresh, or another thread released it, since the caller made its copy. */
static int
still_due_locked(const char *id, int why) {
  int k = index_of_locked(id);
  if(k < 0) return 0;
  if(why == REL_GRACE && g_env.now_ms() - g_hold[k].since_ms < PS5TM_SMPHOLD_GRACE_MS) return 0;
  return 1;
}

/* Unmounts one title the app holds and writes it off. The HTTP status of the unmount (200 done), 0 when there was nothing to do any more.
   `other` is the title whose start was refused (REL_REFUSED) or that the app is about to start (REL_START). Only the first two tell the
   person: after the third he started it. `quiet`: a refusal that repeats is not written to the log again. */
static int
release_one(const char *id, const char *other, int why, int quiet) {
  char msg[300] = "";
  pthread_mutex_lock(&g_rel_lock);
  pthread_mutex_lock(&g_lock);
  int due = still_due_locked(id, why);
  pthread_mutex_unlock(&g_lock);
  if(!due) {
    pthread_mutex_unlock(&g_rel_lock);
    return 0;
  }
  const uint64_t t_unmount = g_env.now_ms();
  int http = g_env.unmount(id, msg, sizeof(msg));
  pthread_mutex_unlock(&g_rel_lock);
  if(http == 200) {
    /* ps5tm_smp_call has already written it off (the unmount went through it); this is for a test with its own unmount. An entry that is
       younger than the unmount is the person's mount of the same title in the meantime: it stays (audit 15.11.2026). */
    pthread_mutex_lock(&g_lock);
    int k = index_of_locked(id);
    if(k >= 0 && g_hold[k].since_ms <= t_unmount) { drop_locked(k); save_locked(); }
    pthread_mutex_unlock(&g_lock);
    if(why == REL_GRACE)
      PS5TM_INFO("smp_hold_released", "%s war eingehängt, aber nicht gestartet. Die App hat es nach %u Sekunden ausgehängt, damit andere Spiele und Kacheln starten.",
                 id, (unsigned)(PS5TM_SMPHOLD_GRACE_MS / 1000u));
    else if(why == REL_REFUSED)
      PS5TM_INFO("smp_hold_released", "%s war eingehängt, aber nicht gestartet. Der Start von %s wurde abgelehnt; die App hat es ausgehängt, damit andere Spiele und Kacheln starten.",
                 id, other);
    else
      PS5TM_INFO("smp_hold_released", "%s war eingehängt, aber nicht gestartet. Die App hat es vor dem Start von %s ausgehängt.", id, other);
    if(why != REL_START && g_env.notice) g_env.notice(id, why == REL_REFUSED ? other : "");
  } else if(!quiet) {
    PS5TM_WARN("smp_hold_failed", "%s ist eingehängt, aber nicht gestartet, und ließ sich nicht aushängen: %s", id, msg);
  }
  return http;
}

int
ps5tm_smphold_is_held(const char *title_id) {
  if(!title_id) return 0;
  pthread_mutex_lock(&g_lock);
  int yes = index_of_locked(title_id) >= 0;
  pthread_mutex_unlock(&g_lock);
  return yes;
}

/* Writes a title off without unmounting it: the app has no business with it any more. */
static void
forget_hold(const char *id) {
  pthread_mutex_lock(&g_lock);
  int k = index_of_locked(id);
  if(k >= 0) { drop_locked(k); save_locked(); }
  pthread_mutex_unlock(&g_lock);
}

/* One look at everything written down. Returns how many games it unmounted. */
int
ps5tm_smphold_step(void) {
  if(!g_env.now_ms) env_default();
  hold_t cur[HOLD_MAX];
  int n;
  pthread_mutex_lock(&g_lock);
  n = g_n;
  memcpy(cur, g_hold, sizeof(g_hold[0]) * (size_t)n);
  pthread_mutex_unlock(&g_lock);
  if(n == 0) { g_smp_gone_since = 0; return 0; }
  if(!g_env.smp_up()) {                                /* ShadowMountPlus not answering: nothing is known, nothing is done ... */
    uint64_t t = g_env.now_ms();
    if(!g_smp_gone_since) g_smp_gone_since = t;
    else if(t - g_smp_gone_since >= HOLD_SMP_GONE_MS) {   /* ... but not for ever: stopped or restarted, it holds nothing any more */
      for(int i = 0; i < n; i++) forget_hold(cur[i].id);
      g_smp_gone_since = 0;
    }
    return 0;
  }
  g_smp_gone_since = 0;

  /* While a game plays (an independent title mounted then does not take the place), or one of the app's jobs works with
     games, the time waits: it starts again afterwards. */
  int paused = g_env.game_running() || g_env.job_running();
  uint64_t now = g_env.now_ms();
  int done = 0;
  for(int i = 0; i < n; i++) {
    int mounted = 0;
    int found = g_env.find(cur[i].id, &mounted);
    /* ShadowMountPlus fell silent between the look above and this question (its list is asked again after a few seconds): nothing is
       known, the title stays written down. Only an answer that does not have the title, or has it unmounted, writes it off. */
    if(found != 0 && !g_env.smp_up()) continue;
    if(found != 0 || !mounted) {                       /* released meanwhile: started and ended, or ShadowMountPlus restarted */
      forget_hold(cur[i].id);
      continue;
    }
    if(g_env.title_running && g_env.title_running(cur[i].id)) {   /* the person started it: ShadowMountPlus has it now, not the app */
      forget_hold(cur[i].id);
      continue;
    }
    if(paused) {
      pthread_mutex_lock(&g_lock);
      int k = index_of_locked(cur[i].id);
      if(k >= 0) g_hold[k].since_ms = now;
      pthread_mutex_unlock(&g_lock);
      continue;
    }
    if(now - cur[i].since_ms < PS5TM_SMPHOLD_GRACE_MS) continue;
    if(cur[i].retry_at && now < cur[i].retry_at) continue;   /* a refused unmount: not before its time */

    int http = release_one(cur[i].id, "", REL_GRACE, cur[i].fails > 0 && cur[i].fails % 6 != 0);
    if(http == 200) {
      done++;
    } else if(http != 0) {
      /* Refused: asked again later, twice as late each time (10 s, 20 s ... 5 min); the log has the first and every sixth. The clock
         is read again: the unmount itself may have taken half a minute. */
      pthread_mutex_lock(&g_lock);
      int k = index_of_locked(cur[i].id);
      if(k >= 0) {
        unsigned shift = g_hold[k].fails < 5 ? g_hold[k].fails : 5;
        uint64_t wait = (uint64_t)HOLD_RETRY_MS << shift;
        if(wait > HOLD_RETRY_MAX_MS) wait = HOLD_RETRY_MAX_MS;
        g_hold[k].fails++;
        g_hold[k].retry_at = g_env.now_ms() + wait;
      }
      pthread_mutex_unlock(&g_lock);
    }
  }
  return done;
}

static void tile_refused(const char *id);

/* The console refused to start a title: it may well be the held game that did it, so it goes, at once. The person starts again. */
int
ps5tm_smphold_check_launch(void) {
  if(!g_env.now_ms) env_default();
  if(!g_env.launch_failed || ps5tm_smphold_count() == 0) return 0;
  char other[16] = "";
  if(!g_env.launch_failed(0, other, sizeof(other))) return 0;
  tile_refused(other);                                 /* an own tile that was refused is started again (ps5tm_smphold_tile_tick) */
  if(!g_env.smp_up()) return 0;
  hold_t cur[HOLD_MAX];
  int n;
  pthread_mutex_lock(&g_lock);
  n = g_n;
  memcpy(cur, g_hold, sizeof(g_hold[0]) * (size_t)n);
  pthread_mutex_unlock(&g_lock);
  int done = 0;
  for(int i = 0; i < n; i++) {
    if(!strcmp(cur[i].id, other)) continue;            /* its own start failed: not what the game held */
    if(g_env.title_running && g_env.title_running(cur[i].id)) {   /* it plays: not the one that blocked, and never unmounted under itself */
      forget_hold(cur[i].id);
      continue;
    }
    if(release_one(cur[i].id, other, REL_REFUSED, 0) == 200) done++;
  }
  return done;
}

/* 1 while the time of the held titles waits (a game plays or one of the app's jobs works with games): they are not released then. */
int
ps5tm_smphold_paused(void) {
  if(!g_env.now_ms) env_default();
  return g_env.game_running() || g_env.job_running();
}

/* The app starts a title itself (ps5tm_library_start): what it holds apart from that goes first, so this start cannot be refused. */
int
ps5tm_smphold_release_for_start(const char *title_id) {
  if(!g_env.now_ms) env_default();
  hold_t cur[HOLD_MAX];
  int n;
  pthread_mutex_lock(&g_lock);
  n = g_n;
  memcpy(cur, g_hold, sizeof(g_hold[0]) * (size_t)n);
  pthread_mutex_unlock(&g_lock);
  if(n == 0 || !g_env.smp_up()) return 0;
  int done = 0;
  /* In all not longer than 20 s: the start protection of the app (30 s from the tap) must not run out while SMP hangs at an unmount, or a
     second tap could start the same title twice (audit 15.11.2026). What is left is released by the refusal path or the watcher. */
  const uint64_t t_begin = g_env.now_ms();
  for(int i = 0; i < n; i++) {
    const int is_target = title_id && !strcmp(cur[i].id, title_id);
    if(!is_target && g_env.now_ms() - t_begin > 20000) continue;   /* the clock of the title being started is still set below */
    if(is_target) {
      /* Starting it is what it waits for. Its clock starts again now: the game state needs seconds to show the start, and the watcher
         must not unmount a title that is just being started because its half minute ran out meanwhile. */
      pthread_mutex_lock(&g_lock);
      int k = index_of_locked(cur[i].id);
      if(k >= 0) g_hold[k].since_ms = g_env.now_ms();
      pthread_mutex_unlock(&g_lock);
      continue;
    }
    if(g_env.title_running && g_env.title_running(cur[i].id)) { forget_hold(cur[i].id); continue; }
    if(release_one(cur[i].id, title_id ? title_id : "?", REL_START, 0) == 200) done++;
  }
  return done;
}

void
ps5tm_smphold_test_env(const ps5tm_smphold_env_t *env) {
  if(!env) {                                           /* the real world again (env_default at the next call) */
    memset(&g_env, 0, sizeof(g_env));
    g_klog_after = 0;
    return;
  }
  g_env = *env;
}

void
ps5tm_smphold_mark(void) {
  if(!g_env.now_ms) env_default();
  if(g_env.launch_failed) g_env.launch_failed(1, NULL, 0);   /* refusals from before do not count */
}

void
ps5tm_smphold_test_reset(void) {
  pthread_mutex_lock(&g_lock);
  g_n = 0;
  pthread_mutex_unlock(&g_lock);
  g_smp_gone_since = 0;
}

void
ps5tm_smphold_test_load(void) {
  if(!g_env.now_ms) env_default();
  load();
}

/* ---- an own tile refused while a game waits in the background (16.11.2026) ----
 *
 * The person starts a game, presses the PS button and opens the app's tile: the console shows an error ("launch mount unavailable ...
 * status=16, Device busy", ShadowMountPlus keeps its one place for the game that is running) and only the second press works. Seen on the
 * console of 16.11.2026 twice (11:18, 12:41). The refusal leaves "LaunchFlowError.<id>" in the kernel log, as for a held game above; the
 * second start works because the refusal has let go of the place. So the app does that second start itself: two seconds after the
 * refusal, at most three tries four seconds apart, at most three starts a minute for the same tile (a console that refuses for good is not
 * pestered). Only the app's own tiles, and only while a game is there, so the kernel log is read only then. */
#define TR_FIRST_DELAY_MS   2000u
#define TR_GAP_MS           4000u
#define TR_TRIES            3
#define TR_WINDOW_MS        60000u
#define TR_WINDOW_STARTS    3
#define TR_READ_GAP_MS      2000u                /* the kernel log is looked at every two seconds, not every one (audit 16.11.2026) */

static struct {
  char     id[12];
  uint64_t at;
  int      tries;
  int      fresh;                                /* the refusal has not been written to the log yet */
} g_tr;                                          /* a start waiting; id[0] == 0 none (the watcher thread only) */
static int      g_tr_watching;
static uint64_t g_tr_read_ms;
static uint64_t g_tr_window_ms;
static int      g_tr_window_starts;

/* A refusal was read from the kernel log (here or by the check for held games): an own tile is started again soon. */
static void
tile_refused(const char *id) {
  if(!id || !ps5tm_is_own_tile(id) || !strcmp(g_tr.id, id)) return;
  snprintf(g_tr.id, sizeof(g_tr.id), "%s", id);
  g_tr.tries = 0;
  g_tr.fresh = 1;
  g_tr.at = (g_env.now_ms ? g_env.now_ms() : 0) + TR_FIRST_DELAY_MS;
}

/* read_log: look at the kernel log for a refusal (not while held games exist: their check reads the log and hands the refusal on). */
static void
tile_step(int read_log) {
  if(!g_env.now_ms) env_default();
  if(!g_env.launch_failed || !g_env.start_title) return;
  const int game = g_env.game_running();
  if(!game && !g_tr.id[0]) { g_tr_watching = 0; return; }     /* no game, nothing waiting: the log is not read */
  const uint64_t now = g_env.now_ms();
  if(read_log) {
    if(!g_tr_watching) { g_env.launch_failed(1, NULL, 0); g_tr_watching = 1; g_tr_read_ms = now; }   /* refusals from before do not count */
    /* Not every second: the log is a copy of the kernel's buffer, and a game can run for hours. The refusal stays in the log, so a read two seconds
       later finds it as well. (No question to ShadowMountPlus here: smp_up would send it two requests every ten seconds for as long as a game runs.) */
    else if(now - g_tr_read_ms >= TR_READ_GAP_MS) {
      g_tr_read_ms = now;
      char id[16] = "";
      if(g_env.launch_failed(0, id, sizeof(id))) tile_refused(id);
    }
  }
  if(!g_tr.id[0] || now < g_tr.at) return;

  if(g_env.title_running && g_env.title_running(g_tr.id)) { g_tr.id[0] = 0; return; }   /* it is up already */
  if(now - g_tr_window_ms >= TR_WINDOW_MS) { g_tr_window_ms = now; g_tr_window_starts = 0; }
  if(g_tr_window_starts >= TR_WINDOW_STARTS) {                  /* it keeps being refused: left to the person */
    PS5TM_WARN("tile_retry_giveup", "Der Start der Kachel %s wurde mehrfach abgelehnt; die App versucht es nicht weiter.", g_tr.id);
    g_tr.id[0] = 0;
    return;
  }
  g_tr_window_starts++;
  g_tr.tries++;
  const int say = g_tr.fresh;
  g_tr.fresh = 0;
  char tile[12];
  snprintf(tile, sizeof(tile), "%s", g_tr.id);
  /* The reason is read BEFORE the new start (which writes its own lines about the same tile into the log of ShadowMountPlus) and written to the app's log
     after it; the read is short, so the start is not held up long. */
  char why[480] = "";
  const int have_why = say && g_env.smp_excerpt && g_env.smp_excerpt(tile, why, sizeof(why));
  if(g_env.go_home) g_env.go_home();                            /* the dialog of the refusal out of the way first */
  int rc = g_env.start_title(g_tr.id);
  if(say) {                                                     /* the reason, as ShadowMountPlus gave it, into the app's log */
    if(have_why)
      PS5TM_INFO("tile_refused", "Der Start der Kachel %s wurde abgelehnt. ShadowMountPlus meldet: %s", tile, why);
    else
      PS5TM_INFO("tile_refused", "Der Start der Kachel %s wurde abgelehnt (die Meldung von ShadowMountPlus war nicht zu lesen).", tile);
    char ver[8] = "";
    ps5tm_config_smp_version(ver, sizeof(ver));
    if(!strcmp(ver, "old"))
      PS5TM_WARN("tile_refused_old_smp", "ShadowMountPlus ist älter als 1.7beta12: Dieser Fehler („Device busy“ nach einem Spielwechsel) ist dort bekannt und ab 1.7beta12 behoben.");
  }
  if(rc >= 0) {
    PS5TM_INFO("tile_retry_started", "Die Kachel %s wurde abgelehnt (ein Spiel wartet im Hintergrund) und von der App noch einmal gestartet (Versuch %d).", g_tr.id, g_tr.tries);
    g_tr.id[0] = 0;
  } else if(g_tr.tries >= TR_TRIES) {
    PS5TM_WARN("tile_retry_failed", "Die Kachel %s ließ sich auch nach %d Versuchen nicht starten (0x%08X).", g_tr.id, g_tr.tries, (unsigned)rc);
    g_tr.id[0] = 0;
  } else {
    g_tr.at = now + TR_GAP_MS;
  }
}

void
ps5tm_smphold_tile_tick(void) {
  tile_step(1);
}

void
ps5tm_smphold_tile_test_reset(void) {
  memset(&g_tr, 0, sizeof(g_tr));
  g_tr_watching = 0;
  g_tr_read_ms = 0;
  g_tr_window_ms = 0;
  g_tr_window_starts = 0;
}

static void *
hold_thread(void *arg) {
  (void)arg;
  int was = 0;
  unsigned tick = 0;
  for(;;) {
    sleep(1);
    if(ps5tm_smphold_count() == 0) { was = 0; tile_step(1); continue; }   /* nothing held: nothing is read for games, nothing asked */
    if(!was) {
      ps5tm_smphold_mark();
      was = 1;
      tick = 0;
    }
    ps5tm_smphold_check_launch();
    tile_step(0);
    if(++tick % 3 == 0) ps5tm_smphold_step();
  }
  return NULL;
}

void
ps5tm_smphold_start(void) {
  if(g_up) return;
  if(!g_env.now_ms) env_default();
  load();
  pthread_t th;
  pthread_attr_t at;
  pthread_attr_init(&at);
  pthread_attr_setstacksize(&at, 512 * 1024);
  pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
  if(pthread_create(&th, &at, hold_thread, NULL) == 0) g_up = 1;
  else PS5TM_WARN("smp_hold_thread", "Der Wächter für eingehängte, nicht gestartete Spiele ließ sich nicht starten.");
  pthread_attr_destroy(&at);
}
