/* The pairing state of Remote Play: the state machine. The contract is in rpair.h; this file makes no call of the console of its own
 * and is built for the console and for the host test (test/rpair).
 *
 * After ps5upload's payload/src/rp_pair.c (phantomptr, GPL-3.0-or-later), written again for this app. Facts that come from that
 * project's measurements: the console reports no expiry of a PIN (so the 300 s are ours and the PIN is given up at the end with
 * sceRemoteplayNotifyPinCodeError(1)); ConfirmDeviceRegist answers at once, status 0 while nobody registered; 2 = paired, 3 = failed,
 * 4 = aborted; the table of paired devices only grows when a registration lands, so growth proves a pairing even if Confirm never
 * says so; the console holds one pending PIN, so a PIN is given up before a new one is made.
 */
#include "rpair.h"

#include <stdio.h>
#include <string.h>

const char *
ps5tm_rpair_state_name(int state) {
  switch(state) {
    case PS5TM_RPAIR_IDLE:    return "idle";
    case PS5TM_RPAIR_WAITING: return "waiting";
    case PS5TM_RPAIR_PAIRED:  return "paired";
    case PS5TM_RPAIR_FAILED:  return "failed";
    case PS5TM_RPAIR_TIMEOUT: return "timeout";
    default:                  return "unknown";
  }
}

void
ps5tm_rpair_init(ps5tm_rpair_t *p) {
  memset(p, 0, sizeof(*p));
  pthread_mutex_init(&p->mtx, NULL);
  p->state = PS5TM_RPAIR_IDLE;
  p->devices_at_start = -1;
}

static void
notify(const ps5tm_rpair_ops_t *ops, const char *msg, int level) {
  if(ops && ops->notify) ops->notify(ops->ctx, msg, level);
}

/* the callers hold p->mtx */
static void
clear_pin_locked(ps5tm_rpair_t *p) {
  p->pin[0] = 0;
  p->deadline_ms = 0;
}

static void
fail_locked(ps5tm_rpair_t *p, const char *code, const char *msg) {
  p->state = PS5TM_RPAIR_FAILED;
  clear_pin_locked(p);
  snprintf(p->code, sizeof(p->code), "%s", code);
  snprintf(p->err, sizeof(p->err), "%s", msg);
}

/* the end of the PIN; 1 when it just ran out */
static int
expire_locked(ps5tm_rpair_t *p, int64_t now_ms) {
  if(p->state != PS5TM_RPAIR_WAITING || p->deadline_ms == 0 || now_ms < p->deadline_ms) return 0;
  p->state = PS5TM_RPAIR_TIMEOUT;
  clear_pin_locked(p);
  snprintf(p->code, sizeof(p->code), "pin_expired");
  snprintf(p->err, sizeof(p->err), "Die PIN ist abgelaufen, bevor ein Gerät gekoppelt wurde.");
  p->invalidate_owed = 1;
  return 1;
}

void
ps5tm_rpair_settle(ps5tm_rpair_t *p, const ps5tm_rpair_ops_t *ops) {
  pthread_mutex_lock(&p->mtx);
  int owed = p->invalidate_owed;
  p->invalidate_owed = 0;
  pthread_mutex_unlock(&p->mtx);
  if(owed && ops && ops->invalidate) (void)ops->invalidate(ops->ctx);
}

int
ps5tm_rpair_request(ps5tm_rpair_t *p, const ps5tm_rpair_ops_t *ops, int64_t now_ms) {
  pthread_mutex_lock(&p->mtx);
  p->gen++;
  p->state = PS5TM_RPAIR_IDLE;
  clear_pin_locked(p);
  p->err[0] = 0;
  p->code[0] = 0;
  p->invalidate_owed = 0;
  p->last_rc = 0;
  p->last_status = 0;
  p->last_err = 0;
  p->probes = 0;
  uint32_t gen = p->gen;
  pthread_mutex_unlock(&p->mtx);

  char why[160] = "", wcode[24] = "";
  if(ops->prepare && ops->prepare(ops->ctx, why, sizeof(why), wcode, sizeof(wcode)) != 0) {
    pthread_mutex_lock(&p->mtx);
    if(p->gen == gen) fail_locked(p, wcode[0] ? wcode : "not_available", why[0] ? why : "Remote Play ist nicht verfügbar.");
    pthread_mutex_unlock(&p->mtx);
    return -1;
  }
  /* after prepare, which has set the module up: a PIN that is still pending is given up first (it costs nothing) */
  if(ops->invalidate) (void)ops->invalidate(ops->ctx);

  int devices = ops->device_count ? ops->device_count(ops->ctx) : -1;
  uint32_t pin = 0;
  int rc = ops->gen_pin ? ops->gen_pin(ops->ctx, &pin) : -1;

  pthread_mutex_lock(&p->mtx);
  if(p->gen != gen) {
    /* cancelled while we were inside the console: the PIN just made is not wanted any more */
    if(rc == 0) p->invalidate_owed = 1;
    pthread_mutex_unlock(&p->mtx);
    return -1;
  }
  if(rc != 0) {
    char msg[96];
    snprintf(msg, sizeof(msg), "Die Konsole konnte keine PIN erzeugen (0x%08X).", (unsigned)rc);
    fail_locked(p, "gen_failed", msg);
    pthread_mutex_unlock(&p->mtx);
    return -1;
  }
  snprintf(p->pin, sizeof(p->pin), "%08u", (unsigned)(pin % 100000000u));
  p->state = PS5TM_RPAIR_WAITING;
  p->deadline_ms = now_ms + PS5TM_RPAIR_WAIT_MS;
  p->devices_at_start = devices;
  char msg[96];
  snprintf(msg, sizeof(msg), "Remote Play: Kopplungs-PIN %s (5 Minuten gültig)", p->pin);
  pthread_mutex_unlock(&p->mtx);

  notify(ops, msg, 0);
  return 0;
}

int
ps5tm_rpair_poll(ps5tm_rpair_t *p, const ps5tm_rpair_ops_t *ops, int64_t now_ms) {
  pthread_mutex_lock(&p->mtx);
  int expired = expire_locked(p, now_ms);
  int s = p->state;
  uint32_t gen = p->gen;
  int before = p->devices_at_start;
  pthread_mutex_unlock(&p->mtx);

  if(expired) notify(ops, "Remote Play: Die PIN ist abgelaufen", 1);
  ps5tm_rpair_settle(p, ops);
  if(s != PS5TM_RPAIR_WAITING) return s;

  uint32_t status = 0, err = 0;
  int rc = ops->confirm ? ops->confirm(ops->ctx, &status, &err) : -1;
  /* the table only grows when a registration lands: growth is proof even if Confirm never says so (read only when Confirm has not
     decided already) */
  int grew = 0;
  if(!(rc == 0 && (status == PS5TM_RP_CONFIRM_PAIRED || status == PS5TM_RP_CONFIRM_FAILED || status == PS5TM_RP_CONFIRM_ABORTED)) &&
     before >= 0 && ops->device_count) {
    grew = ops->device_count(ops->ctx) > before;
  }

  char msg[128] = "";
  int level = 0;
  pthread_mutex_lock(&p->mtx);
  if(p->gen != gen || p->state != PS5TM_RPAIR_WAITING) {
    /* a cancel or a new request happened while we were inside the console: this answer belongs to a PIN nobody waits for */
    s = p->state;
    pthread_mutex_unlock(&p->mtx);
    return s;
  }
  p->last_rc = rc;
  p->last_status = status;
  p->last_err = err;
  p->probes++;
  if(rc == 0 && status == PS5TM_RP_CONFIRM_PAIRED) {
    p->state = PS5TM_RPAIR_PAIRED;
    clear_pin_locked(p);
    snprintf(msg, sizeof(msg), "Remote Play: Ein Gerät wurde gekoppelt");
  } else if(rc == 0 && (status == PS5TM_RP_CONFIRM_FAILED || status == PS5TM_RP_CONFIRM_ABORTED)) {
    const char *code = err == PS5TM_RP_ERR_PIN_INVALID ? "pin_invalid"
                     : err == PS5TM_RP_ERR_ACCOUNT_INVALID ? "account_invalid"
                     : status == PS5TM_RP_CONFIRM_ABORTED ? "aborted" : "refused";
    const char *reason = err == PS5TM_RP_ERR_PIN_INVALID ? "die PIN wurde falsch eingegeben"
                       : err == PS5TM_RP_ERR_ACCOUNT_INVALID ? "die Konto-ID wurde nicht angenommen"
                       : status == PS5TM_RP_CONFIRM_ABORTED ? "die Konsole hat die Anmeldung beendet"
                       : "die Konsole hat die Anmeldung abgelehnt";
    char e[160];
    snprintf(e, sizeof(e), "Das Koppeln ist fehlgeschlagen: %s (Status %u, 0x%08X).", reason, (unsigned)status, (unsigned)err);
    fail_locked(p, code, e);
    /* the console is through with this PIN; telling it again is harmless and makes sure it is gone */
    p->invalidate_owed = 1;
    snprintf(msg, sizeof(msg), "Remote Play: Das Koppeln ist fehlgeschlagen (%s)", reason);
    level = 2;
  } else if(grew) {
    p->state = PS5TM_RPAIR_PAIRED;
    clear_pin_locked(p);
    snprintf(msg, sizeof(msg), "Remote Play: Ein Gerät wurde gekoppelt");
  }
  /* anything else (an error of the probe, a status that is not final) leaves the PIN waiting: its end is what stops it */
  s = p->state;
  pthread_mutex_unlock(&p->mtx);

  if(msg[0]) notify(ops, msg, level);
  ps5tm_rpair_settle(p, ops);
  return s;
}

int
ps5tm_rpair_cancel(ps5tm_rpair_t *p) {
  pthread_mutex_lock(&p->mtx);
  p->gen++;
  if(p->state == PS5TM_RPAIR_WAITING) p->invalidate_owed = 1;
  p->state = PS5TM_RPAIR_IDLE;
  clear_pin_locked(p);
  p->err[0] = 0;
  p->code[0] = 0;
  p->last_rc = 0;
  p->last_status = 0;
  p->last_err = 0;
  p->probes = 0;
  int owed = p->invalidate_owed;
  pthread_mutex_unlock(&p->mtx);
  return owed;
}

void
ps5tm_rpair_view(ps5tm_rpair_t *p, int64_t now_ms, ps5tm_rpair_view_t *out) {
  pthread_mutex_lock(&p->mtx);
  out->state = p->state;
  snprintf(out->pin, sizeof(out->pin), "%s", p->pin);
  snprintf(out->err, sizeof(out->err), "%s", p->err);
  snprintf(out->code, sizeof(out->code), "%s", p->code);
  out->seconds_left = 0;
  if(p->state == PS5TM_RPAIR_WAITING && p->deadline_ms > now_ms) {
    /* rounded up: a PIN with 0.4 s left is still live, and "0 s" next to "waiting" reads as a contradiction */
    out->seconds_left = (int)((p->deadline_ms - now_ms + 999) / 1000);
  }
  out->last_rc = p->last_rc;
  out->last_status = p->last_status;
  out->last_err = p->last_err;
  out->probes = p->probes;
  pthread_mutex_unlock(&p->mtx);
}
