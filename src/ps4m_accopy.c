/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 gasb07 (PS4Mount 0.7.0 accopy.c), adapted for PS5 Cooling & System Center - Pro: functions
 * carry the p4m_ prefix (see src/ps4m_accopy.h); otherwise unchanged. */
#include "ps4m_accopy.h"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static void
state_path(char *out, size_t n, const char *dir, const char *cid, const char *ext) {
  snprintf(out, n, "%s/%.36s.%s", dir, cid, ext);
}

int
p4m_accopy_mark(const char *dir, const char *cid, const char *pkg_path) {
  char p[160];
  state_path(p, sizeof(p), dir, cid, "accopy");
  int fd = open(p, O_WRONLY | O_CREAT | O_TRUNC, 0666);
  if(fd < 0) return -1;
  size_t n = strlen(pkg_path);
  int rc = write(fd, pkg_path, n) == (ssize_t)n ? 0 : -1;
  close(fd);
  return rc;
}

int
p4m_accopy_marked(const char *dir, const char *cid) {
  char p[160];
  state_path(p, sizeof(p), dir, cid, "accopy");
  return access(p, F_OK) == 0;
}

void
p4m_accopy_unmark(const char *dir, const char *cid) {
  char p[160];
  state_path(p, sizeof(p), dir, cid, "accopy");
  unlink(p);
}

int
p4m_acfail_add(const char *dir, const char *cid, const char *reason) {
  char p[160];
  state_path(p, sizeof(p), dir, cid, "acfail");
  int fd = open(p, O_WRONLY | O_CREAT | O_APPEND, 0666);
  if(fd < 0) return -1;
  char line[128];
  int n = snprintf(line, sizeof(line), "%.120s\n", reason);
  int rc = write(fd, line, (size_t)n) == n ? 0 : -1;
  close(fd);
  return rc ? -1 : p4m_acfail_get(dir, cid, NULL, 0);
}

int
p4m_acfail_get(const char *dir, const char *cid, char *reason, size_t cap) {
  char p[160], buf[1024];
  if(reason && cap) reason[0] = 0;
  state_path(p, sizeof(p), dir, cid, "acfail");
  int fd = open(p, O_RDONLY);
  if(fd < 0) return 0;
  ssize_t n = read(fd, buf, sizeof(buf) - 1);
  close(fd);
  if(n <= 0) return 0;
  buf[n] = 0;
  int count = 0;
  char *line = buf, *nl;
  while((nl = strchr(line, '\n'))) {
    *nl = 0;
    count++;
    if(reason && cap) snprintf(reason, cap, "%s", line);
    line = nl + 1;
  }
  return count;
}

void
p4m_acfail_clear(const char *dir, const char *cid) {
  char p[160];
  state_path(p, sizeof(p), dir, cid, "acfail");
  unlink(p);
}

int
p4m_accopy_space_ok(uint64_t free_bytes, uint64_t pkg_size) { return free_bytes > pkg_size + (1ULL << 30); }

void
p4m_acwait_start(p4m_acwait_t *w, time_t now) {
  w->last = 0;
  w->start = w->grew = now;
}

int
p4m_acwait_step(p4m_acwait_t *w, uint64_t cur, uint64_t want, time_t now) {
  if(cur >= want) return P4M_ACW_DONE;
  if(cur > w->last) {
    w->last = cur;
    w->grew = now;
  }
  if(now - w->start > P4M_ACW_TIMEOUT_S) return P4M_ACW_TIMEOUT;
  if(now - w->grew > P4M_ACW_STALL_S) return P4M_ACW_STALL;
  return P4M_ACW_WAIT;
}

int
p4m_accopy_decide(const p4m_acin_t *in) {
  if(in->busy) return P4M_AC_NONE;
  if(in->store) return P4M_AC_STORE;
  if(!in->on) return in->marked ? P4M_AC_UNINSTALL : P4M_AC_NONE;
  if(!in->marked) return in->fails >= P4M_ACFAIL_MAX ? P4M_AC_PROBLEM : P4M_AC_INSTALL;
  if(!in->files) return in->rereg ? P4M_AC_REINSTALL : P4M_AC_TURN_OFF;
  return in->playgo ? P4M_AC_NONE : P4M_AC_REINSTALL;
}
