/* The background-pause helper (17.11.2026): a program of its own, handed to the payload loader by the app (src/bgpause.c) for exactly one
 * call - the console's own service is asked to pause the game that waits in the background (sceLncUtilSuspendApp, the call the shell
 * itself makes 65 ms after a tile is tapped).
 *
 * Why in a process of its own: a call into the LNC service from inside the app killed the app within seconds whenever a game ran (the
 * note in gamestate.c). Here the same call has run from a short-lived payload while a game was resident (measured on the Pro and on
 * the second console, FW 12.00, 17.11.2026): it answered 0 and the process left. If it ever dies, the app loses one pause and nothing
 * else.
 *
 * What it does, and nothing more:
 *   - reads the application id the app wants paused from <data>/bgpause.req (one number, written just before the start) and deletes
 *     the file;
 *   - asks the service which big application is running and pauses it ONLY when it is the one asked for: never a tile of the app,
 *     never another game that has meanwhile been started;
 *   - looks into the kernel log once more right before the pause: when the shell's last scene change no longer went to the home screen,
 *     the person is back in the game, and it is left alone (audit 17.11.2026);
 *   - ends itself after 5 seconds (alarm): the service has hung calls before, and a hung helper must not stay.
 *   - writes what happened to <data>/bgpause.res (one line, read and deleted by the app) and to the kernel log, and leaves.
 *
 * Built for the console as this file alone (make: gen/bgpause_helper.elf), which the app embeds (tools/gen_blob.py). The signatures
 * are the system library's: sceLncUtilSuspendApp(app id, cause) - the shell logs "suspendApp() appId={...}, causeCode={0x0}" for it. */

#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#include <unistd.h>

#define BGP_REQ "/data/PS5-Cooling-Center/bgpause.req"
#define BGP_RES "/data/PS5-Cooling-Center/bgpause.res"

int sceUserServiceInitialize(void *params);
int sceLncUtilInitialize(void);
int sceLncUtilGetAppIdOfRunningBigApp(void);
int sceLncUtilSuspendApp(int app_id, uint32_t cause);
int klog_printf(const char *fmt, ...);

static void
answer(const char *what, int rc, int big, unsigned want) {
  char line[160];
  int n = snprintf(line, sizeof(line), "%s rc=%d big=0x%x want=0x%x\n", what, rc, (unsigned)big, want);
  klog_printf("[bgpause] %s", line);
  /* A file of its own, put in place whole: the app reads it twice a second and must never see half of it. */
  int fd = open(BGP_RES ".tmp", O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if(fd >= 0) {
    int ok = n > 0 && write(fd, line, (size_t)n) == n;
    close(fd);
    if(ok) (void)rename(BGP_RES ".tmp", BGP_RES); else (void)unlink(BGP_RES ".tmp");
  }
}

/* The home screen is still the shell's last scene (gamestate.c reads the same line: "OnFocusActiveSceneChanged [A] -> [Render.NPXS40002 : ...]").
   1 yes, 0 no - the person went back into the game -, -1 the log cannot be read (then the app's own look, a second ago, stands). */
static int
home_scene_still(void) {
  size_t need = 0;
  if(sysctlbyname("kern.msgbuf", NULL, &need, NULL, 0) != 0 || need == 0 || need > (4u << 20)) return -1;
  size_t cap = need + need / 8 + 4096, got = cap;
  char *buf = malloc(cap + 1);
  if(!buf) return -1;
  if(sysctlbyname("kern.msgbuf", buf, &got, NULL, 0) != 0 || got == 0) { free(buf); return -1; }
  for(size_t i = 0; i < got; i++) if(buf[i] == 0) buf[i] = '\n';
  buf[got] = 0;
  int home = -1;
  const char *key = "OnFocusActiveSceneChanged [";
  for(const char *p = buf; (p = strstr(p, key)) != NULL; p += 27) {
    if(p - buf < 9 || strncmp(p - 9, "PSM.UI : ", 9) != 0) continue;       /* a line of the shell, not an echo of it */
    const char *eol = strchr(p, '\n');
    const char *arrow = strstr(p, "-> [");
    if(!arrow || (eol && arrow > eol)) continue;                          /* a line still being written */
    home = strncmp(arrow + 4, "Render.NPXS40002 ", 17) == 0;
  }
  free(buf);
  return home;
}

int
main(void) {
  (void)alarm(5);                                    /* SIGALRM ends the process: the service's calls can hang for good */
  unsigned want = 0;
  int fd = open(BGP_REQ, O_RDONLY);
  if(fd >= 0) {
    char buf[40];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    (void)unlink(BGP_REQ);
    if(n > 0) { buf[n] = 0; want = (unsigned)strtoul(buf, NULL, 0); }
  }
  if(want == 0 || want > 0xfffffu) { answer("keine-anfrage", 0, 0, want); return 0; }

  (void)sceUserServiceInitialize(0);
  int init = sceLncUtilInitialize();
  if(init != 0) { answer("init-fehler", init, 0, want); return 0; }
  int big = sceLncUtilGetAppIdOfRunningBigApp();
  if(big == -1) { answer("schon-pausiert", 0, big, want); return 0; }          /* no big application runs: the game is paused already (or gone) */
  if(big != (int)want) { answer("anderes-spiel", 0, big, want); return 0; }
  if(home_scene_still() == 0) { answer("wieder-vorne", 0, big, want); return 0; }        /* back in the game: leave it be */
  int rc = sceLncUtilSuspendApp(big, 0);
  answer(rc == 0 ? "pausiert" : "pause-fehler", rc, big, want);
  return 0;
}
