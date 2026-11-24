/* The pairing state of Remote Play, the pure part (24.11.2026): no call into the console of its own. Everything that touches the
 * console goes through ps5tm_rpair_ops_t, so test/rpair can drive this file with fake calls; remoteplay.c supplies the real ones.
 *
 * The state machine follows ps5upload's payload/src/rp_pair.c (phantomptr, GPL-3.0-or-later), written again for this app (the
 * values of the status that the console reports are facts measured by that project; see rpair.c).
 *
 *   idle     nothing pending
 *   waiting  a PIN is live; seconds_left counts down to its end
 *   paired   a device was registered (the console said so, or the table of paired devices grew while the PIN was live)
 *   failed   the request or the registration failed; err says why
 *   timeout  the PIN ran out without a registration
 *
 * A cancel or a new request bumps the generation: an answer of the console that was already on its way is dropped, so a late
 * "paired" never overwrites the idle state of a cancel. The struct's own mutex guards its fields and is never held during a call
 * of the console.
 */
#ifndef PS5TM_RPAIR_H
#define PS5TM_RPAIR_H

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>

#define PS5TM_RPAIR_IDLE    0
#define PS5TM_RPAIR_WAITING 1
#define PS5TM_RPAIR_PAIRED  2
#define PS5TM_RPAIR_FAILED  3
#define PS5TM_RPAIR_TIMEOUT 4

/* How long a PIN stays usable: our own limit, the console reports none; at the end the PIN is given up on the console. */
#define PS5TM_RPAIR_WAIT_MS (300 * 1000)

/* The answer of sceRemoteplayConfirmDeviceRegist (observed values) */
#define PS5TM_RP_CONFIRM_PAIRED  2u
#define PS5TM_RP_CONFIRM_FAILED  3u
#define PS5TM_RP_CONFIRM_ABORTED 4u
/* and the reasons that come with FAILED and that a person can act on */
#define PS5TM_RP_ERR_PIN_INVALID     0x80FC1047u
#define PS5TM_RP_ERR_ACCOUNT_INVALID 0x80FC1040u

typedef struct {
  void *ctx;
  /* checks and set-up before a PIN (module loaded, a user with an account); 0 = go, else the reason in err (a sentence) and code
     (the token, see ps5tm_rpair_t); may be NULL */
  int (*prepare)(void *ctx, char *err, size_t err_cap, char *code, size_t code_cap);
  /* sceRemoteplayGeneratePinCode: 0 and the PIN, or the console's error */
  int (*gen_pin)(void *ctx, uint32_t *pin);
  /* sceRemoteplayConfirmDeviceRegist: 0 and status/err, or the console's error */
  int (*confirm)(void *ctx, uint32_t *status, uint32_t *err);
  /* make the live PIN unusable (sceRemoteplayNotifyPinCodeError) */
  int (*invalidate)(void *ctx);
  /* the paired devices in the table, or -1 when it cannot be read; may be NULL */
  int (*device_count)(void *ctx);
  /* a line for the console's own screen; level 0 info, 1 warning, 2 error; may be NULL */
  void (*notify)(void *ctx, const char *msg, int level);
} ps5tm_rpair_ops_t;

typedef struct {
  pthread_mutex_t mtx;
  int      state;
  uint32_t gen;               /* bumped by request and cancel */
  char     pin[12];           /* 8 digits while a PIN is live, else empty */
  char     err[160];          /* the reason as a sentence (German, with the numbers of the console) */
  char     code[24];          /* the same as a short token the pages translate: pin_expired, not_available, gen_failed, pin_invalid,
                                 account_invalid, aborted, refused; empty when there is no error */
  int64_t  deadline_ms;       /* monotonic; 0 = none */
  int      devices_at_start;  /* devices when the PIN was made, -1 unknown */
  int      invalidate_owed;   /* a PIN was given up but the console has not been told yet */
  int      last_rc;           /* the last answer of ConfirmDeviceRegist, for the log */
  uint32_t last_status;
  uint32_t last_err;
  uint32_t probes;
} ps5tm_rpair_t;

typedef struct {
  int      state;
  char     pin[12];
  char     err[160];
  char     code[24];
  int      seconds_left;
  int      last_rc;
  uint32_t last_status;
  uint32_t last_err;
  uint32_t probes;
} ps5tm_rpair_view_t;

void ps5tm_rpair_init(ps5tm_rpair_t *p);
/* Makes a new PIN and replaces a live one (given up first). 0 = waiting, -1 = failed (the reason is in the state). Calls the console. */
int  ps5tm_rpair_request(ps5tm_rpair_t *p, const ps5tm_rpair_ops_t *ops, int64_t now_ms);
/* One probe while waiting: the end of the PIN, then ConfirmDeviceRegist, then the table. Settles an owed give-up. Returns the state. */
int  ps5tm_rpair_poll(ps5tm_rpair_t *p, const ps5tm_rpair_ops_t *ops, int64_t now_ms);
/* Back to idle at once, no call of the console. 1 when the live PIN still has to be given up there (ps5tm_rpair_settle). */
int  ps5tm_rpair_cancel(ps5tm_rpair_t *p);
/* Tells the console about a PIN that was given up (only ops->invalidate is used). */
void ps5tm_rpair_settle(ps5tm_rpair_t *p, const ps5tm_rpair_ops_t *ops);
/* A consistent copy for the report; no call of the console. */
void ps5tm_rpair_view(ps5tm_rpair_t *p, int64_t now_ms, ps5tm_rpair_view_t *out);
const char *ps5tm_rpair_state_name(int state);

#endif /* PS5TM_RPAIR_H */
