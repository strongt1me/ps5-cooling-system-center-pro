/* PS5 Cooling & System Center - Pro — payload entry point. */

#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <unistd.h>

#include "ps5tm.h"

void ps5tm_api_tile_refresh(void);


/* The data directory lost its spaces in 1.3. Move the whole thing rather than
   migrating file by file, so the temperature history and any staged package
   come along with the settings. rename() is atomic and both paths sit on the
   same filesystem, so this either happens completely or not at all. */
static void
migrate_data_dir(void) {
  struct stat st;

  if(stat(PS5TM_DATA_DIR, &st) == 0) return;          /* already moved */
  if(stat(PS5TM_LEGACY_DATA_DIR, &st) != 0) return;   /* nothing to move */
  if(!S_ISDIR(st.st_mode)) return;

  if(rename(PS5TM_LEGACY_DATA_DIR, PS5TM_DATA_DIR) == 0)
    PS5TM_INFO("data_dir_migrated",
               "Datenordner umgezogen: \"%s\" → \"%s\" (Einstellungen und "
               "Verlauf bleiben erhalten).",
               PS5TM_LEGACY_DATA_DIR, PS5TM_DATA_DIR);
  else
    PS5TM_WARN("data_dir_migrate_failed",
               "Datenordner konnte nicht von \"%s\" nach \"%s\" umgezogen "
               "werden – die Einstellungen werden einzeln übernommen.",
               PS5TM_LEGACY_DATA_DIR, PS5TM_DATA_DIR);
}


/* Payloads cannot be unloaded on the PS5 — every ELF sent to elfldr keeps
   running until the console reboots. Two instances therefore each drive the
   fan from their own control loop and fight each other, which looks exactly
   like a broken controller.

   Returns 1 when the program answering on `port` is this app. On a match
   *pid_out receives its process id, or 0 when the instance predates the pid
   being reported and can therefore not be targeted. */
static int
sibling_on_port(unsigned port, int *pid_out) {
  if(pid_out) *pid_out = 0;

  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if(fd < 0) return 0;

  struct timeval tv = { 1, 0 };
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family      = AF_INET;
  addr.sin_port        = htons((uint16_t)port);
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  int found = 0;
  if(connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
    const char *req = "GET /api/v1/status HTTP/1.0\r\n\r\n";
    if(write(fd, req, strlen(req)) > 0) {
      /* Read until the peer closes (HTTP/1.0, so it will). A single read()
         used to stand in for the whole response, which worked by luck: a
         socket may hand over any fraction of a document at a time, and the
         status body runs to several kilobytes. Now that a positive answer
         leads to kill(), the evidence has to be gathered properly. */
      char    buf[4096];
      size_t  len = 0;
      ssize_t n;
      while(len < sizeof(buf) - 1 &&
            (n = read(fd, buf + len, sizeof(buf) - 1 - len)) > 0)
        len += (size_t)n;
      buf[len] = '\0';

      /* Only our own API answers with this pair of keys. */
      if(strstr(buf, "\"ok\":true") && strstr(buf, "\"adapters\"")) {
        found = 1;
        const char *p = strstr(buf, "\"pid\":");
        if(p && pid_out) *pid_out = atoi(p + 6);
      }
    }
  }
  close(fd);
  return found;
}

/* A killed process does not vanish instantly; its listening socket lingers
   for a moment. Without this wait the bind below fails once and reports the
   port as taken by a stranger, which is both wrong and alarming. */
static void
wait_for_release(unsigned port) {
  for(int i = 0; i < 20; i++) {          /* at most two seconds */
    int probe = 0;
    if(!sibling_on_port(port, &probe)) return;
    usleep(100 * 1000);
  }
}


/* Stops an older instance of this app that is holding `port`, so a freshly
   sent payload can take over. Returns 1 when something was actually stopped.

   Payloads cannot be unloaded on the PS5, so a second send used to be a dead
   end: the older instance kept the port and the newer one had no way past it,
   leaving "restart the console" as the only advice. It can be done better —
   the instance holding the port reports its own pid over the very API we use
   to recognise it, so the newcomer stops exactly that process.

   Deliberately *not* done by matching process names, the way
   ps5-payload-dev/klogsrv clears its own predecessor. Name matching does work
   on this platform — SYS_thr_set_name sets ki_comm (offset 447), which is the
   field klogsrv compares, even though its local variable calls it ki_tdname;
   both offsets were checked against the SDK headers with _Static_assert.
   But it rests on assumptions that have already proven brittle here: which
   thread named the process last, and whether the name survives at all.
   Asking the process itself needs none of them. */
static int
takeover_port(unsigned port) {
  int pid = 0;
  if(!sibling_on_port(port, &pid)) return 0;   /* not ours — hands off */
  if(pid <= 1 || pid == (int)getpid())  return 0;

  if(kill(pid, SIGKILL) != 0) {
    PS5TM_WARN("takeover_failed",
               "Die ältere Ausführung dieser App auf Port %u (Prozess %d) "
               "ließ sich nicht beenden.", port, pid);
    return 0;
  }

  PS5TM_INFO("takeover",
             "Ältere Ausführung dieser App auf Port %u beendet (Prozess %d) "
             "— die neue übernimmt.", port, pid);
  wait_for_release(port);
  return 1;
}


/* Scans the current default range plus the old 8080-8089 one, so an instance
   left over from a pre-1.3 build is still recognised as a sibling.

   This used to only warn. But a warning does not stop the other instance from
   driving the fan, and two control loops fighting over the same hardware is
   the actual harm — the person was left to reboot the console by hand. Where
   the leftover can be identified it is now stopped; where it cannot (an old
   build that does not report its pid), the warning is all that remains. */
static void
retire_siblings(unsigned own_port) {
  for(unsigned i = 0; i < 21; i++) {
    /* The configured port first. A leftover instance is most likely sitting
       on exactly the port we are about to ask for, and that one case used to
       be skipped — so the person saw "Port belegt" with no explanation. */
    unsigned p = (i == 0)  ? own_port
               : (i <= 10) ? PS5TM_DEFAULT_HTTP_PORT + (i - 1)
                           : 8080 + (i - 11);
    if(i > 0 && p == own_port) continue;          /* already looked at */

    int pid = 0;
    if(!sibling_on_port(p, &pid)) continue;

    if(pid > 1 && pid != (int)getpid() && kill(pid, SIGKILL) == 0) {
      PS5TM_INFO("sibling_retired",
                 "Ältere Ausführung dieser App auf Port %u beendet "
                 "(Prozess %d). Sonst hätten zwei gleichzeitig am Lüfter "
                 "gedreht und sich gegenseitig gestört.", p, pid);
      wait_for_release(p);
      continue;
    }

    PS5TM_WARN("duplicate_instance",
               "Auf Port %u läuft diese App bereits, ließ sich aber nicht "
               "beenden. Zwei gleichzeitig laufende drehen beide am Lüfter "
               "und stören sich gegenseitig — PS5 neu starten und die App "
               "nur einmal an die Konsole schicken.", p);
  }
}


int
main(int argc, char **argv) {
  (void)argc; (void)argv;

  /* Before the first character of output, and that ordering is the whole
   * point.
   *
   * A payload's stdout is the socket it was sent over. Some senders keep that
   * connection open and show what the payload prints; others transfer the ELF
   * and hang up immediately. Against the second kind, a write to stdout
   * raises SIGPIPE — whose default action is to kill the process. The banner
   * used to be printed above this line, so with such a sender the app died on
   * its very first puts(): no banner, no port, no log entry, nothing to go on.
   * It looked exactly like an ELF that refuses to load, and cost an evening.
   *
   * Ignoring it first costs nothing and makes the payload independent of how
   * it was delivered. Writes to a dead stdout then fail quietly with EPIPE,
   * and the log file keeps everything anyway. */
  signal(SIGPIPE, SIG_IGN);
  signal(SIGCHLD, SIG_IGN);

  /* SIGHUP too: when the sending connection goes away the loader hangs up on
     whatever it started. A background service has no terminal to lose. */
  signal(SIGHUP, SIG_IGN);

  /* NOTHING here may touch the process structure. Learned the hard way.
   *
   * On 01.08.2026 this spot briefly held fork() + setsid(), an attempt to
   * outlive the sending connection. It caused a **kernel panic** on the
   * console. A payload does not run as an ordinary process: it borrows a
   * jailbroken context with a rewritten ucred, and duplicating or re-parenting
   * that is not something the kernel is prepared for.
   *
   * Three things had already failed to keep the app alive past the socket
   * closing — ignoring SIGPIPE, ignoring SIGHUP, and setsid() on its own — so
   * the lifetime is the loader's business, not ours. It is a property of how
   * the payload is delivered, and the fix belongs on that side: send with a
   * tool that keeps the connection open.
   *
   * Do not try fork(), vfork(), setsid(), daemon() or setpgid() here again. */

  setvbuf(stdout, NULL, _IOLBF, 0);

  puts("");
  puts("  " PS5TM_APP_NAME " " PS5TM_VERSION);
  puts("  " PS5TM_APP_TAGLINE);
  puts("");
#ifdef SYS_thr_set_name
  /* Despite the name, this sets the *process* name on the PS5 (ki_comm), not
   * the thread's — see the comment in fan_worker(). It is therefore set once,
   * here, and nowhere else.
   *
   * The ".elf" matters. Every payload manager on this platform, ours included,
   * decides what may be listed and stopped by that suffix. Named plainly
   * "ps5tm" the app was invisible in its own list and in every other tool on
   * the console — which is not privacy, just a process nobody can account
   * for. FreeBSD-only; the syscall is absent on host builds. */
  syscall(SYS_thr_set_name, -1, "ps5tm.elf");
#endif

  ps5tm_log_init();
  PS5TM_INFO("boot", PS5TM_APP_NAME " %s wird gestartet.", PS5TM_VERSION);

  /* Without this, /dev/icc_fan cannot be opened and the fan stays on the
     firmware's own schedule. */
  if(ps5tm_platform_escalate() != 0) {
    PS5TM_WARN("jb_escalation_partial",
               "Rechteausweitung unvollständig – die Lüftersteuerung "
               "funktioniert möglicherweise nicht.");
  } else {
    PS5TM_INFO("jb_escalation_ok", "Rechteausweitung erfolgreich (pid:%d).",
               (int)getpid());
  }

  /* After the thread has been named above, so the name logged is the final
     one. Cheap, and it answers questions that cannot be answered from
     outside — see the comment on the function. */
  ps5tm_procmgr_log_identity();
  ps5tm_platform_log_sensor_symbols();

  uint32_t fw = ps5tm_platform_firmware();
  PS5TM_INFO("firmware", "Firmware 0x%08X erkannt (Gruppe %s).",
             (unsigned)fw, ps5tm_platform_firmware_group(fw));

  migrate_data_dir();
  mkdir(PS5TM_DATA_DIR, 0755);
  ps5tm_config_load();
  ps5tm_payloads_init();                 /* the folder for own payloads, only if missing */

  ps5tm_config_lock();
  unsigned own_port = g_config.http_port;
  ps5tm_config_unlock();
  retire_siblings(own_port);

  if(own_port != PS5TM_TILE_DEEPLINK_PORT)
    PS5TM_WARN("tile_port_mismatch",
               "Die Startmenü-Kachel ruft fest Port %u auf, die App läuft "
               "aber auf %u — über die Kachel ist sie so nicht erreichbar. "
               "Entweder den Port auf %u zurückstellen oder die Kachel über "
               "den Browser der Konsole umgehen.",
               PS5TM_TILE_DEEPLINK_PORT, own_port, PS5TM_TILE_DEEPLINK_PORT);

  ps5tm_history_load();
  ps5tm_thermal_load();
  ps5tm_pad_load();

  /* Before the fan controller starts: it needs the process to hold the fan
     identity, and the tile installer needs a different one. */
  ps5tm_tile_bootstrap();

  ps5tm_api_tile_refresh();
  /* The log already says why when the regulating thread cannot be started
     (the console is short of memory, say); what it does not do is reach a
     person who is not reading it, and an app that looks fine while it is not
     regulating anything is the worst way to fail. Tried once more after a
     moment; a failed start leaves nothing marked as started, so that is safe. */
  if(ps5tm_fan_init() != 0) {
    sleep(2);
    if(ps5tm_fan_init() != 0)
      ps5tm_notify("Achtung: Die Lüftersteuerung konnte nicht gestartet "
                   "werden.\nDer Lüfter bleibt bei der zuletzt gesetzten "
                   "Schwelle.");
  }

  /* The probe thread deliberately does NOT start here — see below. */

  /* ps5tm_http_run() returns 0 when the configured port changed, so the
     settings page can move the UI without a restart.
     If the preferred port is taken — another payload, or a previous instance
     of this one — walk up to the next free port rather than looping on the
     error. The winning port is written back to the config so it stays put
     across restarts, and so ps5tm_http_run() does not mistake it for a
     settings change and rebind immediately. */
  unsigned announced_port = 0;

  /* Complained about once per spell of being blocked, not every five
     seconds — a log that repeats itself is a log nobody reads. */
  int busy_reported = 0;

  for(;;) {
    ps5tm_config_lock();
    unsigned port = g_config.http_port;
    ps5tm_config_unlock();

    int srv = ps5tm_http_bind(port);

    /* Busy. There used to be a fallback here: walk up to the next free port,
     * write it into the config and carry on. It was a mistake, and the
     * console showed why — the home-screen tile carries a fixed address, so
     * an app that quietly moves to 8087 is an app the tile can no longer
     * reach. Worse, the new port was saved, so one collision moved it for
     * good and the tile stayed broken.
     *
     * A port nobody can guess is not availability. Staying put and saying
     * plainly what is in the way is more use than a silent detour. */
    if(srv == -2) {
      /* Second line of defence. retire_siblings() already cleared the field
         before the first bind, but an instance can still appear here — a
         payload sent twice in quick succession, or one that was starting up
         and not yet answering when we looked. */
      if(takeover_port(port)) {
        busy_reported = 0;
        continue;
      }

      if(!busy_reported) {
        busy_reported = 1;

        int other = 0;
        if(sibling_on_port(port, &other)) {
          PS5TM_ERROR("http_port_taken_by_self",
                      "Port %u wird von einer älteren Ausführung dieser App "
                      "belegt, die sich nicht beenden ließ — sie ist zu alt, "
                      "um ihre Prozessnummer zu nennen. Konsole neu starten "
                      "und die App nur einmal schicken. Die Lüftersteuerung "
                      "läuft hier weiter, nur die Weboberfläche bleibt aus.",
                      port);
          ps5tm_notify("Port %u ist von einer älteren Ausführung dieser App "
                       "belegt.\nPS5 neu starten.", port);
        } else {
          PS5TM_ERROR("http_port_taken",
                      "Port %u ist von einem anderen Programm belegt. Die "
                      "Lüftersteuerung läuft weiter; für die Oberfläche "
                      "entweder das andere Programm beenden oder den Port in "
                      "den Einstellungen ändern — dann zeigt allerdings die "
                      "Startmenü-Kachel ins Leere.", port);
          ps5tm_notify("Port %u ist belegt — Web-UI nicht erreichbar.", port);
        }
      }
      sleep(5);
      continue;                            /* same port, again, patiently */
    }

    if(srv < 0) {
      PS5TM_ERROR("http_bind_failed",
                  "Port %u konnte nicht geöffnet werden.", port);
      sleep(5);
      continue;
    }

    busy_reported = 0;

    /* Announce on the TV only once the socket is actually listening, so the
       address in the toast is one that works. Repeated on a port change. */
    char ip[INET_ADDRSTRLEN] = "127.0.0.1";
    int have_ip = (ps5tm_local_ip(ip, sizeof(ip)) == 0);

    if(announced_port != port) {
      announced_port = port;

      if(have_ip) {
        ps5tm_notify(PS5TM_APP_NAME " %s gestartet\n"
                     "Web-UI: http://%s:%u", PS5TM_VERSION, ip, port);
      } else {
        ps5tm_notify(PS5TM_APP_NAME " %s gestartet\n"
                     "Web-UI auf Port %u (keine Netzwerkadresse gefunden)",
                     PS5TM_VERSION, port);
      }
      PS5TM_INFO("ready", "Web-UI erreichbar unter http://%s:%u", ip, port);

      /* Without kstuff the fan cannot be driven at all — worth saying on the
         screen rather than burying it in the log. */
      if(!ps5tm_platform_fan_available()) {
        ps5tm_notify("Achtung: Lüftersteuerung nicht verfügbar.\n"
                     "Ist kstuff geladen?");
      }
    }

    /* Only now, with the socket listening and the address announced.
     *
     * The probe thread is the one place that calls into Sony's services, and
     * such a call can block while holding a lock the whole process shares —
     * the runtime linker's, or the log's. Started before the bind loop, it
     * took the main thread down with it: on FW 12.00 the last line ever
     * logged was "Port 8086 ist belegt", and nothing after it ran. Everything
     * above this point is sysctl and socket work that cannot block that way,
     * so the web UI and the fan controller are up before the risk is taken.
     *
     * Starting once is enough; the call is a no-op afterwards. */
    ps5tm_probe_start();

    /* The play-time recorder only reads what the probe and the fan thread have
       cached and writes its own two files, so it waits for nothing; it goes
       here only because there is no reason for it to run earlier. */
    ps5tm_playtime_start();
    ps5tm_library_probe_start();
    ps5tm_payprof_start();

    ps5tm_http_run(srv, port);
  }

  return 0;
}
