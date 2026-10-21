/* savebackup.h — internal job-management helpers used by savesign.c.
 *
 * The main public API (ps5tm_saves_*) lives in ps5tm.h.
 * This header exposes only what savesign.c needs to plug into the job slot
 * without duplicating the backup infrastructure.
 */

#pragma once

/* Start a detached job thread in the savebackup job slot.
 * Unlike job_launch the argument lifetime is managed entirely by the
 * thread: fn frees arg before returning.
 * Returns 200 on success, 409 if busy, 503 on failure. */
int  ps5tm_saves_run_simple(const char *kind, void *(*fn)(void *), void *arg,
                            char *err, size_t err_len);

/* Mark the current job as failed (call from the job thread). */
void ps5tm_saves_job_fail(const char *msg);

/* Mark the current job as successfully done (call from the job thread). */
void ps5tm_saves_job_done(const char *msg);
