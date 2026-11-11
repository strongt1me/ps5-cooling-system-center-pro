/* PS5 Cooling & System Center - Pro - one running copy of the app, found without HTTP.

   Until now a newly sent app found its older copy only by asking the web server on its port for the status page (main.c,
   sibling_on_port). An older copy that was overloaded (all connection slots taken, a stuck library read) answered with 503 or not
   at all in time, counted as "someone else's program", and the new app started regulating the fan beside it (audit 11.11.2026,
   F-12). The lock does not depend on any answer: every copy keeps a write lock on one file of the data folder for as long as it
   lives; the system drops it when the process ends, however it ends. */
#ifndef PS5TM_INSTLOCK_H
#define PS5TM_INSTLOCK_H

/* Takes the lock on `path`. Another copy that holds it is asked to end (SIGTERM, which lets it hand the fan back to the console);
   after term_wait_ms it gets SIGKILL, and after kill_wait_ms more the attempt is given up.
   Returns 1: the lock is ours (kept until the process ends); *stopped_pid is the pid of a copy that was ended, else 0.
            0: another copy holds it and could not be ended.
           -1: locks do not work here (the file cannot be opened or locked): the caller goes on as before. */
int ps5tm_instlock_acquire(const char *path, int term_wait_ms, int kill_wait_ms, int *stopped_pid);

#endif
