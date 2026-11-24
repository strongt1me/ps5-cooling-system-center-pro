/* threadwrap.h - the thread wrapper of ps5tm.h for the units that stand alone (no ps5tm.h): conv_pfs.c, iopolicy.c, zstdpack.c.
 *
 * Every thread of the app gets a small signal stack of its own (fanguard.c): the crash handler that hands the fan back to the console
 * must also run after a stack overflow, and an alternate signal stack is set per thread. These three files start worker threads but
 * do not include ps5tm.h (the host tests build them alone), so their pthread_create went round the wrapper (audit 24.11.2026).
 * A host test that builds one of them without fanguard.c passes -DPS5TM_NO_THREAD_WRAP, as the other host tests do.
 * Identical to the lines in ps5tm.h: a file that includes both gets the same definition twice, which C allows. */
#ifndef PS5TM_THREADWRAP_H
#define PS5TM_THREADWRAP_H

#include <pthread.h>

int ps5tm_thread_create(pthread_t *thread, const pthread_attr_t *attr, void *(*start)(void *), void *arg);
#ifndef PS5TM_NO_THREAD_WRAP
#define pthread_create(t, a, f, x) ps5tm_thread_create((t), (a), (f), (x))
#endif

#endif
