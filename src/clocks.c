/* Clock rates, read out of the console's own power-state table.
 *
 * SceSystemStateMgr writes this pair of lines whenever the power mode
 * changes:
 *
 *   CPU                                     HLT GC     GFX  F    U   ID       FAN BAPM
 *    800  800  800  800 3200 3200 3200 3200   1   gen2  900 1200 875 00840f80   0 off
 *
 * Sixteen fields: eight per-core CPU clocks, a halt flag, the PCIe
 * generation, the **graphics clock**, fabric (F = FCLK) and memory-controller
 * (U = UCLK) clocks, an id word, a fan field and AMD's power-budget switch.
 *
 * ⚠ F and U were read the other way round until 1.45.0 — F as the memory
 * clock, U as the fabric clock. The live clock call settled it on 26.09.2026:
 * its domain 24 (FCLK) read 750 and domain 23 (UCLK) 225, while this table
 * said F 750, U 225 at the same moment.
 *
 * ── Why this is worth having
 *
 * Two of those are things this project had written off:
 *
 *   - **Per-core clocks.** sceKernelGetCpuFrequency() returns a single
 *     figure, and on 02.08.2026 it read 800 MHz while this table showed four
 *     cores at 800 and four at 3200. The dashboard was not wrong so much as
 *     misleading — it showed the slowest core and called it "the" clock.
 *
 *   - **The graphics clock.** The accepted position, recorded in this
 *     project's own notes and taken from etaHEN's source, is that the PS5
 *     exposes nothing GPU-side. That holds for the utilisation counters. It
 *     does not hold here: GFX is right there in the console's own log.
 *
 * ── The limit, stated up front
 *
 * These lines appear on a **power mode change**, not continuously. What is
 * published is therefore the last table seen, with its age — the same
 * honesty the battery reading needs, and for the same reason.
 */

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "klogown.h"
#include "ps5tm.h"

#define MARKER "FAN BAPM"

static ps5tm_clocks_t   g_clocks;
static ps5tm_sysstate_t g_state;
static pthread_mutex_t  g_lock = PTHREAD_MUTEX_INITIALIZER;


/* Memory and power mode, parsed from the same buffer as the clock table.
 *
 * SceShellCore prints these periodically:
 *
 *   VM Stats: RSS 1341.5 0.0, kernel 427.9 0.0, wire count 517.5 0.0,
 *             swap out 0.0, page table CPU 5077/8192 GPU 158/8192
 *   FMEM   6.3/  19.7   0.0/   0.0 ps5tm.elf
 *
 * The memory figures are another thing this project gave up on: the syscall
 * route (get_page_table_stats and friends) returned nothing on this firmware
 * and the fields were removed in 1.9.5. They were in the log all along.
 *
 * FMEM is per process and includes ours, which makes it the honest answer to
 * "what is this app costing the console".
 */
static void
parse_sysstate(const char *buf) {
  ps5tm_sysstate_t s;
  memset(&s, 0, sizeof(s));
  s.idle_sec = -1;

  const char *vm = NULL, *pm = NULL, *idle = NULL;
  for(const char *p = buf; *p; p++) {
    if(!strncmp(p, "VM Stats: ", 10) && !ps5tm_klog_own_line(buf, p)) {
      vm = p + 10;
      /* Each round of memory output starts with this line, and the buffer
         holds several rounds. Without the reset the list filled up with the
         *oldest* round and then stopped — 24 entries, exactly the cap, which
         is what gave it away. Starting over here keeps the newest round. */
      s.proc_count = 0;
    }
    else if(!strncmp(p, "Power Mode Change: ", 19) && !ps5tm_klog_own_line(buf, p)) pm   = p + 19;
    else if(!strncmp(p, "No user input for ", 18) && !ps5tm_klog_own_line(buf, p))  idle = p + 18;
    else if(!strncmp(p, "FMEM ", 5) && s.proc_count < PS5TM_FMEM_MAX && !ps5tm_klog_own_line(buf, p)) {
      double a = 0, b = 0, c = 0, d = 0;
      int    n = 0;
      if(sscanf(p + 5, "%lf / %lf %lf / %lf %n", &a, &b, &c, &d, &n) == 4 &&
         n > 0) {
        const char *nm = p + 5 + n;
        while(*nm == ' ') nm++;
        size_t k = 0;
        ps5tm_fmem_t *e = &s.procs[s.proc_count];
        while(nm[k] && nm[k] != '\n' && nm[k] != '\r' &&
              k < sizeof(e->name) - 1) { e->name[k] = nm[k]; k++; }
        e->name[k] = '\0';
        if(k) {
          e->used_mb  = a;
          e->total_mb = b;
          s.proc_count++;
        }
      }
    }
  }

  if(vm) {
    /* Each figure is looked up by its own label rather than by counting
       fields — the line has changed shape between firmwares before. */
    const char *q;
    if((q = strstr(vm, "RSS ")))          sscanf(q + 4,  "%lf", &s.rss_mb);
    if((q = strstr(vm, "kernel ")))       sscanf(q + 7,  "%lf", &s.kernel_mb);
    if((q = strstr(vm, "wire count ")))   sscanf(q + 11, "%lf", &s.wire_mb);
    if((q = strstr(vm, "swap out ")))     sscanf(q + 9,  "%lf", &s.swap_mb);
    if((q = strstr(vm, "page table CPU ")))
      sscanf(q + 15, "%d/%d GPU %d/%d",
             &s.pt_cpu_used, &s.pt_cpu_total,
             &s.pt_gpu_used, &s.pt_gpu_total);
    s.valid = 1;
  }

  if(pm) {
    size_t k = 0;
    while(pm[k] && pm[k] != '\n' && pm[k] != '\r' &&
          k < sizeof(s.power_mode) - 1) { s.power_mode[k] = pm[k]; k++; }
    s.power_mode[k] = '\0';
    s.valid = 1;
  }
  if(idle) { s.idle_sec = atoi(idle); s.valid = 1; }

  /* The page-table figures come from the kernel when it will give them, and
     from the log only as a fallback.
     The log route was the only one until 02.08.2026 and mostly yielded
     nothing — SceShellCore prints that line rarely, so the memory card showed
     an empty pair of numbers for weeks. get_page_table_stats() answers every
     time and is current rather than however old the last printout was. */
  {
    int cu = 0, ct = 0, gu = 0, gt = 0;
    if(ps5tm_platform_page_stats(&cu, &ct, &gu, &gt) == 0) {
      s.pt_cpu_used  = cu;  s.pt_cpu_total = ct;
      s.pt_gpu_used  = gu;  s.pt_gpu_total = gt;
      s.valid = 1;
    }
  }

  if(!s.valid) return;

  pthread_mutex_lock(&g_lock);
  s.seen_ms = ps5tm_now_ms();
  g_state = s;
  pthread_mutex_unlock(&g_lock);
}


void
ps5tm_sysstate_get(ps5tm_sysstate_t *out) {
  if(!out) return;
  pthread_mutex_lock(&g_lock);
  *out = g_state;
  pthread_mutex_unlock(&g_lock);
}


/* Splits a line into whitespace-separated tokens. Returns how many were
   found, at most `max`. The fields are read by position, which is why the
   count is checked before anything is believed. */
static int
tokenise(const char *line, const char *end, char tok[][20], int max) {
  int n = 0;
  const char *p = line;
  while(p < end && n < max) {
    while(p < end && (*p == ' ' || *p == '\t')) p++;
    if(p >= end || *p == '\n' || *p == '\r') break;
    int k = 0;
    while(p < end && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') {
      if(k < 19) tok[n][k++] = *p;
      p++;
    }
    tok[n][k] = '\0';
    n++;
  }
  return n;
}


void
ps5tm_clocks_refresh(void) {
#ifndef PS5TM_HOST_TEST
  size_t len = 0;
  char  *buf = ps5tm_msgbuf_read(&len);
  if(!buf) return;

  /* Everything out of one pass. The buffer runs to half a megabyte, and three
     modules each fetching their own copy would be three times the work for
     the same bytes — this one already has it in hand. */
  parse_sysstate(buf);

  /* Chronological buffer, so the last header is the newest table. */
  const char *last = NULL;
  for(const char *p = buf; *p; p++)
    if(!strncmp(p, MARKER, sizeof(MARKER) - 1) && !ps5tm_klog_own_line(buf, p)) last = p;

  if(!last) { free(buf); return; }

  /* The numbers are on the line after the header. */
  const char *nl = strchr(last, '\n');
  if(!nl) { free(buf); return; }
  const char *line = nl + 1;
  const char *end  = strchr(line, '\n');
  if(!end) end = line + strlen(line);

  /* Skip the log prefix "<118>[SceSystemStateMgr] " — start at the first
     digit, which is where the table begins. */
  const char *p = line;
  while(p < end && (*p < '0' || *p > '9')) p++;
  if(p >= end) { free(buf); return; }

  char tok[20][20];
  int  n = tokenise(p, end, tok, 20);
  free(buf);

  /* 8 cores + HLT + GC + GFX + F + U + ID + FAN + BAPM = 16. Anything else
     is a table this code was not written for, and guessing at it would be
     worse than reporting nothing. */
  if(n < 16) return;

  ps5tm_clocks_t c;
  memset(&c, 0, sizeof(c));

  int cores = PS5TM_MAX_CORES < 8 ? PS5TM_MAX_CORES : 8;
  for(int i = 0; i < cores; i++) c.core_mhz[i] = atoi(tok[i]);
  c.core_count = cores;

  c.gfx_mhz    = atoi(tok[10]);
  c.fabric_mhz = atoi(tok[11]);   /* F — FCLK */
  c.mem_mhz    = atoi(tok[12]);   /* U — UCLK */
  c.fan_raw    = atoi(tok[14]);
  c.bapm_on    = (strcmp(tok[15], "off") != 0);
  snprintf(c.pcie_gen, sizeof(c.pcie_gen), "%s", tok[9]);

  /* A clock of zero means the line was not what we took it for. */
  if(c.gfx_mhz <= 0 || c.core_mhz[0] <= 0) return;
  c.valid = 1;

  pthread_mutex_lock(&g_lock);
  /* Age counts from when the figures last *changed*. The same table sits in
     the buffer for a while, and refreshing the timestamp on every read would
     dress a stale reading up as a fresh one. */
  int same = g_clocks.valid &&
             g_clocks.gfx_mhz == c.gfx_mhz &&
             memcmp(g_clocks.core_mhz, c.core_mhz, sizeof(c.core_mhz)) == 0;
  c.seen_ms = same ? g_clocks.seen_ms : ps5tm_now_ms();
  g_clocks = c;
  pthread_mutex_unlock(&g_lock);
#endif
}


void
ps5tm_clocks_get(ps5tm_clocks_t *out) {
  if(!out) return;
  pthread_mutex_lock(&g_lock);
  *out = g_clocks;
  pthread_mutex_unlock(&g_lock);
}
