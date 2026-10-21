/* savesign.h — re-sign a save for a different user + view save contents.
 *
 * Re-signing copies a save from one user to another and patches the ownership
 * fields so the console accepts it as the target user's save.
 *
 * PS5: patch ACCOUNT_ID + USER_ID in sce_sys/param.sfo.
 * PS4: copy the PARAMS blob (which carries the per-user HMAC) from a template
 *      save that already belongs to the target user (same CUSA / same or any
 *      slot); if no template exists, the call is refused with a clear message.
 *
 * The "view contents" job mounts the image read-only and returns the file
 * list as JSON (no changes to the save).
 *
 * All jobs share the savebackup job slot (ps5tm_saves_busy / ps5tm_saves_cancel)
 * so the saves page cannot run two operations at once.
 */

#pragma once
#include <stddef.h>
#include <stdint.h>

struct cJSON;

/* ── Re-sign ─────────────────────────────────────────────────────────────
 *
 * Copies the save at (src_uid, title_id, slot) into the target user's save
 * area and patches ownership.  A backup of the target user's existing save
 * (if any) is made first.
 *
 *   src_uid    8-digit hex user folder of the SOURCE user
 *   dst_uid    8-digit hex user folder of the TARGET user (must differ)
 *   title_id   PPSA… (PS5) or CUSA… (PS4)
 *   slot       save slot name, e.g. "default" or "slot00"
 *   err        receives a human-readable refusal reason on failure
 *
 * Returns the HTTP status to send to the browser:
 *   200  job started (poll /api/v1/saves/job)
 *   400  bad arguments
 *   409  busy (another job is running)
 *   422  no template found for PS4 re-sign
 *   503  hardware error
 */
int ps5tm_savesign_start(const char *src_uid, const char *dst_uid,
                         const char *title_id, const char *slot,
                         char *err, size_t err_len);

/* ── View contents ────────────────────────────────────────────────────────
 *
 * Mounts the save at (uid, title_id, slot) read-only and returns a JSON
 * array of { "name": "...", "size": N } objects for every file inside.
 * The mount is released before the function returns; no files are modified.
 *
 * On success *out_json is a cJSON array (caller must free with cJSON_Delete).
 * On error *out_json is NULL and the function returns -1.
 */
int ps5tm_savesign_view(const char *uid, const char *title_id,
                        const char *slot,
                        struct cJSON **out_json);

/* Convenience: is the uid string syntactically valid (8 hex digits)?  */
int ps5tm_savesign_uid_valid(const char *uid);
