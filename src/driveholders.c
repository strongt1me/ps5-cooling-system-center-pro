/* PS5 Cooling & System Center - Pro: who has a file open on a drive (21.11.2026).
 *
 * The console ejects a USB drive with a forced unmount that asks nobody (see drivegate.c). The only thing that can tell, beforehand, whether a process
 * still has a file, a folder or its working directory on the drive is the process table: sysctl kern.proc.filedesc, one call per process, gives a
 * kinfo_file per open descriptor with the path of the vnode. The same walk is used by Transmission for the PS5 (GPL-3.0-or-later, src/manager/procinfo.c,
 * ps5tm_disk_holders); this is a version of its own, written for the question of this app. Read-only, nothing is changed.
 *
 * ON THE PRO (firmware 12.00, checked 21.11.2026 with the diagnostic line of GET /api/v1/storage/holders): the process table can be read (105 processes), but
 * kern.proc.filedesc answers ENOENT for every process, our own included. What other programs have open cannot be learned that way there; the answer is then "not
 * known" and the eject goes on (the gate and sync stay). What this app has open itself CAN be learned on every firmware: fstatfs() on each descriptor
 * number (own_fds below), and that is checked first.
 *
 * What it does not see: a vnode seen through a game's sandbox (its path then begins with /mnt/sandbox/...: the console removes those itself when it
 * ejects), a memory mapping whose descriptor was closed, a request that is under way without a descriptor (a stat). Those are the gate's business
 * (drivegate.c), and it is why the gate and this walk are used together. */
#include "ps5tm.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#ifdef PS5TM_HOST_TEST
#include <dirent.h>
#else
#include <errno.h>
#include <sys/mount.h>
#include <sys/proc.h>
#include <sys/sysctl.h>
#include <sys/user.h>
#endif

static int
path_under(const char *path, const char *mount, size_t ml) {
  return !strncmp(path, mount, ml) && (path[ml] == '/' || path[ml] == 0);
}

static void
add_holder(ps5tm_holder_t *out, int *n, int max, int pid, const char *name, const char *path) {
  int i;
  for(i = 0; i < *n; i++) if(out[i].pid == pid) break;
  if(i == *n) {
    if(*n >= max) return;
    memset(&out[i], 0, sizeof(out[i]));
    out[i].pid = pid;
    snprintf(out[i].name, sizeof(out[i].name), "%s", name && name[0] ? name : "?");
    snprintf(out[i].sample, sizeof(out[i].sample), "%s", path);
    (*n)++;
  }
  out[i].count++;
}

#ifdef PS5TM_HOST_TEST
/* The host tests run on Linux: /proc/<pid>/fd and /proc/<pid>/cwd stand for the descriptors. */
int
ps5tm_drive_holders(const char *mount, ps5tm_holder_t *out, int max) {
  if(!mount || !out || max <= 0) return -1;
  size_t ml = strlen(mount);
  DIR *pd = opendir("/proc");
  if(!pd) return -1;
  int n = 0;
  struct dirent *e;
  while((e = readdir(pd))) {
    int pid = atoi(e->d_name);
    if(pid <= 0) continue;
    char dir[64], link[300], name[32] = "?", comm[96];
    snprintf(comm, sizeof(comm), "/proc/%d/comm", pid);
    FILE *f = fopen(comm, "r");
    if(f) { if(fgets(name, sizeof(name), f)) name[strcspn(name, "\n")] = 0; fclose(f); }
    snprintf(link, sizeof(link), "/proc/%d/cwd", pid);
    char tgt[300];
    ssize_t r = readlink(link, tgt, sizeof(tgt) - 1);
    if(r > 0) { tgt[r] = 0; if(path_under(tgt, mount, ml)) add_holder(out, &n, max, pid, name, tgt); }
    snprintf(dir, sizeof(dir), "/proc/%d/fd", pid);
    DIR *fd = opendir(dir);
    if(!fd) continue;
    struct dirent *x;
    while((x = readdir(fd))) {
      if(x->d_name[0] == '.') continue;
      snprintf(link, sizeof(link), "/proc/%d/fd/%s", pid, x->d_name);
      r = readlink(link, tgt, sizeof(tgt) - 1);
      if(r <= 0) continue;
      tgt[r] = 0;
      if(path_under(tgt, mount, ml)) add_holder(out, &n, max, pid, name, tgt);
    }
    closedir(fd);
  }
  closedir(pd);
  return n;
}
#else
static void *
sysctl_table(int *mib, u_int miblen, size_t *len_out);

/* The descriptors of THIS process that lie on the mount: every number up to the table size is asked with fstatfs (a file, a folder, anything with a vnode on it).
   Works without sysctl. Returns how many, and the first number in `first`. */
static int
own_fds(const char *mount, int *first) {
  long maxfd = sysconf(_SC_OPEN_MAX);
  if(maxfd < 16 || maxfd > 4096) maxfd = 4096;
  int count = 0;
  *first = -1;
  for(int fd = 0; fd < (int)maxfd; fd++) {
    struct statfs sf;
    if(fstatfs(fd, &sf) != 0) continue;
    if(!strcmp(sf.f_mntonname, mount)) {
      if(!count) *first = fd;
      count++;
    }
  }
  return count;
}

/* What the process table and the descriptor lists answer on this console (the first line of the holders route): how many processes, how many of them
   let their descriptor list be read, the errno of the first one that did not, and what our own list holds. For a console where it is "not known". */
void
ps5tm_drive_holders_diag(char *out, size_t n) {
  int pmib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PROC, 0 };
  size_t plen = 0;
  errno = 0;
  if(sysctl(pmib, 4, NULL, &plen, NULL, 0) != 0 || plen == 0) {
    snprintf(out, n, "process table: sysctl failed, errno %d, len %zu", errno, plen);
    return;
  }
  char *procs = sysctl_table(pmib, 4, &plen);
  if(!procs) { snprintf(out, n, "process table: read failed (%zu bytes announced)", plen); return; }
  const size_t need = offsetof(struct kinfo_proc, ki_comm) + sizeof(((struct kinfo_proc *)0)->ki_comm);
  int procs_n = 0, ok = 0, failed = 0, first_errno = 0, self_files = -1, self_errno = 0;
  size_t self_len = 0;
  for(char *p = procs; p < procs + plen; ) {
    struct kinfo_proc *ki = (struct kinfo_proc *)p;
    if(ki->ki_structsize <= 0 || (size_t)ki->ki_structsize < need) break;
    p += ki->ki_structsize;
    procs_n++;
    int fmib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_FILEDESC, (int)ki->ki_pid };
    size_t flen = 0;
    errno = 0;
    int rc = sysctl(fmib, 4, NULL, &flen, NULL, 0);
    int e = errno;
    if(ki->ki_pid == (pid_t)getpid()) { self_errno = rc == 0 ? 0 : e; self_len = flen; }
    if(rc == 0 && flen > 0) {
      ok++;
      if(ki->ki_pid == (pid_t)getpid()) {
        char *files = sysctl_table(fmib, 4, &flen);
        if(files) {
          int c = 0;
          for(char *q = files; q < files + flen; ) {
            struct kinfo_file *kf = (struct kinfo_file *)q;
            if(kf->kf_structsize <= 0 || q + kf->kf_structsize > files + flen) break;
            q += kf->kf_structsize;
            c++;
          }
          self_files = c;
          free(files);
        }
      }
    } else {
      failed++;
      if(!first_errno) first_errno = rc == 0 ? -1 : e;
    }
  }
  free(procs);
  snprintf(out, n, "processes %d, descriptor list readable %d, not readable %d (first errno %d); own list: len %zu, errno %d, entries %d, kinfo_file size %zu",
           procs_n, ok, failed, first_errno, self_len, self_errno, self_files, sizeof(struct kinfo_file));
}

static void *
sysctl_table(int *mib, u_int miblen, size_t *len_out) {
  for(int attempt = 0; attempt < 3; attempt++) {
    size_t need = 0;
    if(sysctl(mib, miblen, NULL, &need, NULL, 0) != 0 || need == 0) return NULL;
    need += need / 8 + 4096;                       /* the table grows between the two calls */
    void *buf = malloc(need);
    if(!buf) return NULL;
    if(sysctl(mib, miblen, buf, &need, NULL, 0) == 0) { *len_out = need; return buf; }
    free(buf);
  }
  return NULL;
}

/* Processes with at least one descriptor, working directory or root directory whose path lies on `mount`. out: pid, name, how many, the first path.
   Returns how many processes; -1 when the process table cannot be read at all (the caller treats that as "not known", not as "nobody"). */
int
ps5tm_drive_holders(const char *mount, ps5tm_holder_t *out, int max) {
  if(!mount || !out || max <= 0) return -1;
  size_t ml = strlen(mount);
  int n = 0, readable = 0;
  /* this app's own descriptors on the drive: known on every firmware */
  {
    int first = -1;
    int mine = own_fds(mount, &first);
    if(mine > 0) {
      char where[64];
      snprintf(where, sizeof(where), "%s (descriptor %d)", mount, first);
      add_holder(out, &n, max, (int)getpid(), "diese App", where);
      out[n - 1].count = mine;
    }
  }
  int pmib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PROC, 0 };
  size_t plen = 0;
  char *procs = sysctl_table(pmib, 4, &plen);
  if(!procs) return n > 0 ? n : -1;

  const size_t need = offsetof(struct kinfo_proc, ki_comm) + sizeof(((struct kinfo_proc *)0)->ki_comm);
  for(char *p = procs; p < procs + plen; ) {
    struct kinfo_proc *ki = (struct kinfo_proc *)p;
    if(ki->ki_structsize <= 0 || (size_t)ki->ki_structsize < need) break;
    p += ki->ki_structsize;
    if(ki->ki_pid <= 0 || ki->ki_stat == 5 /* SZOMB */ || ki->ki_pid == getpid()) continue;     /* ours: own_fds above */
    int fmib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_FILEDESC, (int)ki->ki_pid };
    size_t flen = 0;
    char *files = sysctl_table(fmib, 4, &flen);
    if(!files) continue;                           /* a process that went away, or one that may not be asked: not counted as readable */
    readable++;
    for(char *q = files; q < files + flen; ) {
      struct kinfo_file *kf = (struct kinfo_file *)q;
      if(kf->kf_structsize <= 0 || q + kf->kf_structsize > files + flen) break;
      q += kf->kf_structsize;
      if(kf->kf_path[0] == '/' && path_under(kf->kf_path, mount, ml)) add_holder(out, &n, max, (int)ki->ki_pid, ki->ki_comm, kf->kf_path);
    }
    free(files);
  }
  free(procs);
  /* no other process could be asked (the Pro: ENOENT for all): what is known is this app's own list; empty, that is "not known", not "nobody" */
  return (readable > 0 || n > 0) ? n : -1;
}
#endif
