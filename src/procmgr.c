/* Running payloads: list them, and stop the ones that should not be there.
 *
 * A payload cannot be "unloaded" on the PS5 — once elfldr has started it, it
 * runs until the console reboots. What can be done is to kill the process,
 * which is what every payload manager means by unloading. That matters here
 * in particular: two copies of this app each drive the fan from their own
 * control loop and fight each other, and until now the only cure was a
 * reboot.
 *
 * Deciding what may be killed is the delicate part. The rule is the one
 * ps5-payload-manager established: the process name ends in .elf and it has
 * no application id (games and system apps have one), with mini-syscore.elf
 * excluded by name because it satisfies both conditions and is essential.
 */

#include <dlfcn.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>
#ifndef PS5TM_HOST_TEST
#include <sys/proc.h>
#include <sys/sysctl.h>
#include <sys/user.h>
#endif
#include <unistd.h>

#include "ps5tm.h"

/* Layout from ps5-payload-manager; only app_id is actually read. */
typedef struct {
  uint32_t app_id;
  uint64_t unknown1;
  char     title_id[14];
  char     unknown2[0x3c];
} app_info_t;

typedef int (*fn_get_app_info_t)(pid_t, app_info_t *);
static fn_get_app_info_t p_sceKernelGetAppInfo = NULL;
static int               g_resolved = 0;

static void
resolve(void) {
  if(g_resolved) return;
  g_resolved = 1;
#ifndef PS5TM_HOST_TEST
  p_sceKernelGetAppInfo =
      (fn_get_app_info_t)dlsym(RTLD_DEFAULT, "sceKernelGetAppInfo");
#endif
}


/* True when the process is a loaded payload rather than part of the system. */
static int
is_payload(const char *name, uint32_t app_id, pid_t pid) {
  /* This app is a payload whatever the kernel happens to call it, so say so
     before the general tests get a chance to disagree.
     It was missing from its own list until 01.08.2026, and the omission was
     quiet but real: the "you cannot stop this app" guard in
     ps5tm_procmgr_kill() could never fire, because the pid it protects was
     never offered in the first place. Which of the two tests below rejected
     us is recorded by ps5tm_procmgr_log_identity() at startup. */
  if(pid == getpid()) return 1;

  if(!name || !name[0]) return 0;

  /* Essential, and it would otherwise pass both tests below. */
  if(!strcmp(name, "mini-syscore.elf")) return 0;

  /* Anything Sony launched as an application carries an id. */
  if(app_id != 0) return 0;

  const char *ext = strrchr(name, '.');
  return (ext && !strcasecmp(ext, ".elf")) ? 1 : 0;
}


#ifndef PS5TM_HOST_TEST
/* The whole process table: variable-size kinfo_proc records back to back, each
 * one carrying its own length in ki_structsize. The caller frees; NULL when the
 * kernel will not hand it over.
 *
 * The size the kernel reports is the size of the table at that instant. A game
 * spawns whole families of processes while it starts, so the table can outgrow
 * the buffer before the second call — which then fails with ENOMEM instead of
 * returning less. Every reader used to take that failure for "nothing there":
 * an empty payload list, or one pass of "kein Spiel" in the game detection,
 * which switches the per-game fan profile off and on again a few seconds
 * later. Hence some slack, and a fresh start when it was still too small. Any
 * other failure is retried too; a persistent one costs two more calls. */
static void *
read_proc_table(size_t *len_out) {
  int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PROC, 0 };

  for(int attempt = 0; attempt < 3; attempt++) {
    size_t need = 0;
    if(sysctl(mib, 4, NULL, &need, NULL, 0) != 0 || need == 0) return NULL;

    need += need / 8 + sizeof(struct kinfo_proc);
    void *buf = malloc(need);
    if(!buf) return NULL;

    if(sysctl(mib, 4, buf, &need, NULL, 0) == 0) {
      *len_out = need;
      return buf;
    }
    free(buf);
  }
  return NULL;
}
#endif


unsigned
ps5tm_procmgr_list(ps5tm_process_t *out, unsigned max) {
#ifdef PS5TM_HOST_TEST
  (void)out; (void)max;
  return 0;
#else
  resolve();

  size_t need = 0;
  void  *buf  = read_proc_table(&need);
  if(!buf) return 0;

  unsigned n    = 0;
  pid_t    self = getpid();

  for(char *p = buf; p < (char *)buf + need && n < max; ) {
    struct kinfo_proc *ki = (struct kinfo_proc *)p;
    if(ki->ki_structsize == 0) break;          /* records are variable-size */
    p += ki->ki_structsize;

    app_info_t info;
    memset(&info, 0, sizeof(info));
    if(p_sceKernelGetAppInfo && p_sceKernelGetAppInfo(ki->ki_pid, &info) != 0)
      memset(&info, 0, sizeof(info));

    if(!is_payload(ki->ki_comm, info.app_id, ki->ki_pid)) continue;

    ps5tm_process_t *e = &out[n++];
    memset(e, 0, sizeof(*e));
    e->pid = (int)ki->ki_pid;

    /* Our own entry gets a name a person can read. The kernel knows us as
       "ps5tm.elf", which is correct but not what someone scanning a list is
       looking for. It used to be "ps5tm-fan" — the fan worker renamed the
       whole process, because SYS_thr_set_name sets ki_comm on this platform.
       That is fixed at the source now; see main(). */
    if(ki->ki_pid == self)
      snprintf(e->name, sizeof(e->name), "%s (diese App)", PS5TM_APP_NAME);
    else
      snprintf(e->name, sizeof(e->name), "%s", ki->ki_comm);
    e->memory_mb = (int)((double)ki->ki_rssize * (double)PAGE_SIZE /
                         (1024.0 * 1024.0));
    e->is_self   = (ki->ki_pid == self);
  }

  free(buf);
  return n;
#endif
}


/* Records how this process looks to the kernel: the name the payload list
   decides on, the thread name the kernel puts in front of every klog line,
   and whether an application id is attached.

   Written once at startup because none of it can be seen from outside. The
   app was absent from its own payload list and there was no way to tell which
   of the two tests in is_payload() had rejected it — ki_comm is not exposed
   over the network, and the one list that would have shown it is the list we
   were missing from. */
void
ps5tm_procmgr_log_identity(void) {
#ifndef PS5TM_HOST_TEST
  resolve();

  /* Asking about a single process with KERN_PROC_PID is the obvious way and
     simply does not answer on this kernel — it failed on every start of
     v1.10.3. Walking the whole list does work, since that is what the payload
     list has always done, so reuse it and pick our own entry out. */
  size_t need = 0;
  void  *buf  = read_proc_table(&need);
  if(!buf) return;

  pid_t self = getpid();

  for(char *p = buf; p < (char *)buf + need; ) {
    struct kinfo_proc *ki = (struct kinfo_proc *)p;
    if(ki->ki_structsize == 0) break;
    p += ki->ki_structsize;
    if(ki->ki_pid != self) continue;

    app_info_t info;
    memset(&info, 0, sizeof(info));
    if(p_sceKernelGetAppInfo && p_sceKernelGetAppInfo(self, &info) != 0)
      memset(&info, 0, sizeof(info));

    PS5TM_INFO("self_identity",
               "Eigener Prozess: PID %d, Prozessname \"%s\", Threadname "
               "\"%s\", App-ID %u, sceKernelGetAppInfo %s.",
               (int)self, ki->ki_comm, ki->ki_tdname, (unsigned)info.app_id,
               p_sceKernelGetAppInfo ? "aufgelöst" : "nicht aufgelöst");
    break;
  }

  free(buf);
#endif
}


/* A real game's title id: PPSA or CUSA followed by five digits.
   Restricted on purpose — "four capitals plus five digits" also matches
   Sony's own applications (NPXS40000 and friends), which would turn the
   home screen into a "running game". ShadowMountPlus applies the same rule. */
static int
is_game_title_id(const char *s) {
  if(!s || strlen(s) != 9) return 0;
  if(strncmp(s, "PPSA", 4) && strncmp(s, "CUSA", 4)) return 0;
  for(int i = 4; i < 9; i++)
    if(s[i] < '0' || s[i] > '9') return 0;
  return 1;
}


/* The title id of a running game, straight from the kernel.
 *
 * sceKernelGetAppInfo() fills an app_info_t whose second field is the title
 * id — the very thing the message-buffer route cannot supply on this
 * firmware. It has been sitting in a struct this file already reads, next to
 * the app_id we did use, behind a comment saying "only app_id is actually
 * read". Found on 01.08.2026 in ShadowMountPlus (sm_game_lifecycle.c,
 * resolve_game_title_id), which does exactly this and runs on this console.
 *
 * The distinction that matters: this is a **kernel call**, not the LNC/IPMI
 * service call that killed the payload within seconds whenever a game was
 * actually running (see gamestate.c). Same goal, different subsystem.
 *
 * `want_app_id` picks the process carrying that application id; zero takes
 * the first process with a plausible game title. Returns 0 on success. */
int
ps5tm_procmgr_game_title(uint32_t want_app_id, char *out, size_t out_len) {
  if(!out || out_len == 0) return -1;
  out[0] = '\0';

#ifdef PS5TM_HOST_TEST
  (void)want_app_id;
  return -1;
#else
  resolve();
  if(!p_sceKernelGetAppInfo) return -1;

  size_t need = 0;
  void  *buf  = read_proc_table(&need);
  if(!buf) return -1;

  int rc = -1;
  for(char *p = buf; p < (char *)buf + need; ) {
    struct kinfo_proc *ki = (struct kinfo_proc *)p;
    if(ki->ki_structsize == 0) break;
    p += ki->ki_structsize;

    app_info_t info;
    memset(&info, 0, sizeof(info));
    if(p_sceKernelGetAppInfo(ki->ki_pid, &info) != 0) continue;
    if(info.app_id == 0) continue;
    if(want_app_id && info.app_id != want_app_id) continue;

    /* title_id is not guaranteed to be terminated inside its 14 bytes. */
    char t[sizeof(info.title_id) + 1];
    memcpy(t, info.title_id, sizeof(info.title_id));
    t[sizeof(info.title_id)] = '\0';

    if(!is_game_title_id(t)) continue;

    snprintf(out, out_len, "%s", t);
    rc = 0;
    break;
  }

  free(buf);
  return rc;
#endif
}


/* Closing a running game.
 *
 * The way Elf Arsenal's "Close App" does it (src/ps5/sys.c, sys_kill_title):
 * SIGKILL the game's eboot.bin, look again every 100 ms, and repeat until it
 * is gone. Its author notes that sceSystemServiceKillApp, the service call
 * meant for this, was unreliable and made the button appear to do nothing —
 * and the service calls of that family are the ones that killed this payload
 * whenever a game ran (gamestate.c). A signal to a process is what this file
 * already does for payloads, and it involves no service at all.
 *
 * Only the process the kernel names as that title's eboot.bin is touched:
 * the title id comes from sceKernelGetAppInfo, as in ps5tm_procmgr_game_title,
 * and it must be a game id (PPSA/CUSA). What the game had not saved is lost —
 * the page says so before anyone presses the button.
 *
 * 0 when the game was running and is gone now, 1 when it was not running at
 * all, -1 when it is still there after `timeout_ms` or cannot be looked up. */
int
ps5tm_procmgr_close_game(const char *title_id, unsigned timeout_ms) {
#ifdef PS5TM_HOST_TEST
  (void)title_id;
  (void)timeout_ms;
  return -1;
#else
  if(!is_game_title_id(title_id)) return -1;
  resolve();
  if(!p_sceKernelGetAppInfo) return -1;

  uint64_t start  = ps5tm_mono_ms();
  int      killed = 0;
  for(;;) {
    size_t need = 0;
    void  *buf  = read_proc_table(&need);
    if(!buf) return -1;

    int found = 0;
    for(char *p = buf; p < (char *)buf + need; ) {
      struct kinfo_proc *ki = (struct kinfo_proc *)p;
      if(ki->ki_structsize == 0) break;
      p += ki->ki_structsize;
      if(ki->ki_pid == getpid() || strcmp(ki->ki_comm, "eboot.bin")) continue;

      app_info_t info;
      memset(&info, 0, sizeof(info));
      if(p_sceKernelGetAppInfo(ki->ki_pid, &info) != 0 || info.app_id == 0)
        continue;
      char t[sizeof(info.title_id) + 1];
      memcpy(t, info.title_id, sizeof(info.title_id));
      t[sizeof(info.title_id)] = '\0';
      if(strcmp(t, title_id)) continue;

      found++;
      if(kill(ki->ki_pid, SIGKILL) == 0 && killed++ == 0)
        PS5TM_INFO("game_kill", "Spiel %s wird beendet (eboot.bin, PID %d, "
                   "App-ID 0x%X).", title_id, (int)ki->ki_pid,
                   (unsigned)info.app_id);
    }
    free(buf);

    if(!found) return killed ? 0 : 1;
    if(ps5tm_mono_ms() - start >= timeout_ms) return -1;
    usleep(100 * 1000);
  }
#endif
}


/* Whether that title's eboot.bin is running: the lookup above, touching
   nothing. 1 yes, 0 no, -1 when the kernel cannot be asked. */
int
ps5tm_procmgr_title_running(const char *title_id) {
#ifdef PS5TM_HOST_TEST
  (void)title_id;
  return -1;
#else
  if(!is_game_title_id(title_id)) return -1;
  resolve();
  if(!p_sceKernelGetAppInfo) return -1;

  size_t need = 0;
  void  *buf  = read_proc_table(&need);
  if(!buf) return -1;

  int found = 0;
  for(char *p = buf; !found && p < (char *)buf + need; ) {
    struct kinfo_proc *ki = (struct kinfo_proc *)p;
    if(ki->ki_structsize == 0) break;
    p += ki->ki_structsize;
    if(strcmp(ki->ki_comm, "eboot.bin")) continue;

    app_info_t info;
    memset(&info, 0, sizeof(info));
    if(p_sceKernelGetAppInfo(ki->ki_pid, &info) != 0 || info.app_id == 0)
      continue;
    char t[sizeof(info.title_id) + 1];
    memcpy(t, info.title_id, sizeof(info.title_id));
    t[sizeof(info.title_id)] = '\0';
    found = !strcmp(t, title_id);
  }
  free(buf);
  return found;
#endif
}


/* Whether the kernel route above can answer at all. gamestate.c needs to tell
   "that process is not a game" apart from "there is no way to ask", and a
   failed ps5tm_procmgr_game_title() alone means either. */
int
ps5tm_procmgr_appinfo_ready(void) {
#ifdef PS5TM_HOST_TEST
  return 0;
#else
  resolve();
  return p_sceKernelGetAppInfo != NULL;
#endif
}


/* Used to see the console's browser close: it runs as SceNKWebProcess, a
   process of the shell, not an application with its own id (library.c). */
int
ps5tm_procmgr_pid_by_name(const char *name) {
#ifdef PS5TM_HOST_TEST
  (void)name;
  return -1;
#else
  if(!name || !name[0]) return -2;

  /* Failing to read the table at all is -2, not -1: a caller waiting for a
     process to go must not take "could not look" for "gone". */
  size_t need = 0;
  void  *buf  = read_proc_table(&need);
  if(!buf) return -2;

  int pid = -1;
  for(char *p = buf; p < (char *)buf + need; ) {
    struct kinfo_proc *ki = (struct kinfo_proc *)p;
    if(ki->ki_structsize == 0) break;
    p += ki->ki_structsize;
    if(!strcmp(ki->ki_comm, name)) { pid = (int)ki->ki_pid; break; }
  }

  free(buf);
  return pid;
#endif
}


int
ps5tm_procmgr_kill(int pid, const char **why) {
  if(why) *why = NULL;

  if(pid <= 1) {
    if(why) *why = "Diese Prozessnummer ist nicht zulässig.";
    return -1;
  }
  if(pid == (int)getpid()) {
    /* Killing ourselves would stop the fan regulation with no way back short
       of resending the payload — refuse and say so plainly. */
    if(why) *why = "Das ist diese App selbst. Nutze stattdessen den "
                   "Automatik-Schalter, um die Regelung abzugeben.";
    return -1;
  }

#ifdef PS5TM_HOST_TEST
  if(why) *why = "Im Testbetrieb nicht möglich.";
  return -1;
#else
  /* Only processes that pass the payload test may be stopped, even if the
     caller hands us an arbitrary pid. */
  ps5tm_process_t list[64];
  unsigned n = ps5tm_procmgr_list(list, 64);
  const char *name = NULL;
  for(unsigned i = 0; i < n; i++)
    if(list[i].pid == pid) { name = list[i].name; break; }

  if(!name) {
    if(why) *why = "Dieser Prozess ist kein Payload oder läuft nicht mehr.";
    return -1;
  }

  if(kill(pid, SIGKILL) != 0) {
    if(why) *why = "Der Prozess ließ sich nicht beenden.";
    return -1;
  }

  PS5TM_INFO("payload_killed", "Payload %s (PID %d) beendet.", name, pid);
  return 0;
#endif
}
