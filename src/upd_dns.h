/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Knutwurst (PatchDL 0.3.0, src/patchdl_dns.h), adapted for PS5 Cooling & System Center - Pro
 *
 * The resolver of the game updates. The console's DNS is often set up to block Sony's hosts (that is how people keep the
 * system from updating), so the app never asks it for them. A lookup tries, in this order, until one gives a usable answer:
 *
 *   1. plain DNS over UDP to 1.1.1.1, 8.8.8.8, 9.9.9.9, 1.0.0.1
 *   2. DNS over TCP to 1.1.1.1 and 8.8.8.8 (UDP/53 blocked or truncated)
 *   3. DNS over HTTPS to Cloudflare and Google (port 53 blocked or redirected to a filtering resolver)
 *   4. the addresses the last successful lookup saved to disk
 *
 * Answers in private, loopback or "this network" ranges count as blocked: that is what filtering resolvers send back. */
#ifndef PS5TM_UPD_DNS_H
#define PS5TM_UPD_DNS_H

#include <stddef.h>

#define UPD_DNS_MAX_IPS 8

typedef struct {
  int  n;
  char ip[UPD_DNS_MAX_IPS][16];
  char source[40];     /* e.g. "udp 1.1.1.1", "doh dns.google", "saved" */
  char trail[384];     /* what each step answered on the last fresh lookup */
} upd_dns_answer_t;

/* Resolve host (IPv4). Answers are cached for a few minutes and failures for a short while. 0, or -1 when every source failed. */
int      upd_dns_lookup(const char *host, upd_dns_answer_t *out);
/* Drop host's cached answer after its addresses refused or stalled a connection (an answer only seconds old is kept). */
void     upd_dns_forget(const char *host);
/* Drop it unconditionally. */
void     upd_dns_refresh(const char *host);
/* A new starting point among the addresses, once per request: connections spread over the CDN edges. */
unsigned upd_dns_rotation(void);

#ifdef PS5TM_HOST_TEST
/* host tests: these servers instead of the public ones (NULL ends; port for UDP and TCP), DoH off when doh is 0 */
void     upd_dns_test_servers(const char *const *udp, const char *const *tcp, int port, int doh);
#endif

#endif
