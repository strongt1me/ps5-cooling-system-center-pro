/* NTFS drives for the file manager (ntfsvol.h, 23.11.2026). After the pattern of vfs.c of PS5-NTFS-BROWSER (abcde6543, v1.4.0,
 * GPL-3.0-or-later): every NTFS partition found in the partition table of a whole disk becomes a drive; the driver is that project's
 * (src/third_party/ntfsb). What differs from vfs.c, for this console's other users of the USB drives:
 *  - only whole disks (daN) are opened, never one the console has mounted something from (an exFAT stick in use, f_mntfromname),
 *  - no look while a drive is being ejected or settles after it (the crashes around ejecting, 01.-02.10. and 21.11.2026),
 *  - a look happens only when the file manager asks (its places every 3-4 s while it is open), at most every 3 s, and a disk that is
 *    no NTFS is opened once until it is unplugged,
 *  - names like the console's drives: ntfs0, ntfs1 ... the lowest that is free; a stick that comes back gets its name again, another
 *    one does not take a name that was let go a moment ago (a page may still show its folders).
 * Unplugged while nobody looked (audit 23.11.2026): a drive asks its own device (an fd kept open on it) whether it is still there, and
 * what was learned about a disk (no NTFS, ejected) keeps its node's identity (inode, device and change time: devfs makes the node
 * anew for each disk that arrives, even under the same name; nothing writes to such a disk). */
#include <dirent.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#ifndef PS5TM_HOST_TEST
#include <sys/disk.h>
#endif

#include "ntfsvol.h"
#include "ps5tm.h"
#include "third_party/ntfsb/bdev.h"
#include "third_party/ntfsb/ntfs.h"
#include "third_party/ntfsb/part.h"

#define NV_NODE    300
#define NV_PROBED  32                    /* disks remembered (found, no NTFS, ejected) */
#define NV_SOURCES 4                     /* host tests: image files */
#define NV_SCAN_MS 3000u
#define NV_TRIES   5                     /* opens of a new disk that fail before it is left alone (some need a moment) */
/* after the n-th failed open the next one waits NV_SCAN_MS << n (6, 12, 24, 48 s): a disk that is still announcing itself (the console
   logs new USB addresses and "cam_open_device failed" then, 21.11.2026) is not touched in the look's 3 s beat (upstream issue #1) */
#define NV_NAME_MS 120000u               /* a name let go is not given to another disk for this long */
#define NV_RETIRED 16
#define NV_SETTLE_S 10                   /* a disk node younger than this is not touched yet */

struct ps5tm_ntfsvol {
  char         name[8];
  char         node[NV_NODE];
  char         label[128];
  int          part;                     /* partition number on the disk */
  ntfs_vol_t  *ntfs;
  uint64_t     serial, size;
  int          watch;                    /* an fd on its disk: it fails once the disk is unplugged (-1: none) */
  int          refs;                     /* the list holds one while the drive is listed */
  int          retired;                  /* out of the list (gone, ejected) and still held by someone */
  int          writing;                  /* driver work under way that writes (the driver's lock is held for long then) */
  int          reading;                  /* downloads and views of a file: nothing is deleted on the drive meanwhile */
  int          deleting;                 /* a file is being deleted: no read starts meanwhile */
  int          want_read;                /* reads waiting for a delete step to end: no next delete starts */
  int          c_valid, c_writable, c_free_known;   /* what the places showed last, used while it is written to */
  uint64_t     c_free;
  char         c_why[PS5TM_NTFS_WHY];
};

typedef struct {
  char     node[NV_NODE];
  uint64_t ident;                        /* node_ident when it was learned; 0 while not known yet (an eject writes out first) */
  int      rejected;                     /* no NTFS on it (or it never opened) */
  int      ejected;                      /* "Sicher auswerfen": not taken up again until it is unplugged */
  int      tries;
  uint64_t next_ms;                      /* not opened again before this (after a failed open) */
} nv_probe_t;

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;   /* the list, references, probe results, sources */
static pthread_mutex_t g_scan = PTHREAD_MUTEX_INITIALIZER;   /* one look at a time */
static ps5tm_ntfsvol_t *g_vol[PS5TM_NTFS_MAX];
static ps5tm_ntfsvol_t *g_retired[NV_RETIRED];               /* out of the list, still held: its disk is not opened twice */
static unsigned         g_nretired;
static unsigned         g_retired_lost;                      /* held ones that did not fit the list: no disk is opened meanwhile */
static struct { uint64_t serial, at_ms; } g_freed[PS5TM_NTFS_MAX];   /* the name a drive let go, and when */
static nv_probe_t       g_probe[NV_PROBED];
static unsigned         g_nprobe;
static char             g_src[NV_SOURCES][NV_NODE];
static unsigned         g_nsrc;
static uint64_t         g_scan_at;
static int              g_closing;                           /* the app ends: no drive is handed out any more */
static int              g_stopping;                          /* the app is asked to end: an upload stops */

int
ps5tm_ntfs_path(const char *p) {
  return p && !strncmp(p, "/ntfs", 5) && p[5] >= '0' && p[5] < '0' + PS5TM_NTFS_MAX && (p[6] == 0 || p[6] == '/');
}

void
ps5tm_ntfs_add_source(const char *path) {
  pthread_mutex_lock(&g_lock);
  if(g_nsrc < NV_SOURCES) snprintf(g_src[g_nsrc++], NV_NODE, "%s", path);
  pthread_mutex_unlock(&g_lock);
}

/* What tells two pluggings of a disk apart: its node's inode, device and creation time (for a host test's image file: inode and
   change time). 0 when the node cannot be read. */
static uint64_t
node_ident(const char *node) {
  struct stat st;
  if(stat(node, &st) != 0) return 0;
  const uint64_t part[4] = { (uint64_t)st.st_ino, (uint64_t)st.st_rdev, (uint64_t)st.st_ctim.tv_sec, (uint64_t)st.st_ctim.tv_nsec };
  uint64_t h = 1469598103934665603ull;
  for(int i = 0; i < 4; i++)
    for(int b = 0; b < 8; b++) { h ^= (part[i] >> (8 * b)) & 0xFF; h *= 1099511628211ull; }
  return h ? h : 1;
}

static void
vol_free(ps5tm_ntfsvol_t *v) {
  if(!v) return;
  if(v->ntfs) ntfs_umount(v->ntfs);               /* writes out what is pending when it was written to */
  if(v->watch >= 0) close(v->watch);
  free(v);
}

/* Is the drive's disk still there? Its own fd says so: on an unplugged disk the console answers with an error, also when another disk
   has come under the same name meanwhile. An image file (host tests) is always there. */
static int
vol_alive(const ps5tm_ntfsvol_t *v) {
#ifndef PS5TM_HOST_TEST
  if(v->watch < 0) return 1;
  off_t ms = 0;
  if(ioctl(v->watch, DIOCGMEDIASIZE, &ms) == 0) return ms > 0;
  return !(errno == ENXIO || errno == EIO || errno == ENODEV || errno == EBADF);
#else
  (void)v;
  return 1;
#endif
}

/* Takes slot k out of the list (under g_lock): its name is remembered, a holder keeps it as retired. Returns the drive when nobody
   holds it any more (the caller frees it outside the lock), else NULL. */
static ps5tm_ntfsvol_t *
unlist_locked(int k) {
  ps5tm_ntfsvol_t *v = g_vol[k];
  g_vol[k] = NULL;
  g_freed[k].serial = v->serial;
  g_freed[k].at_ms = ps5tm_mono_ms();
  if(--v->refs == 0) return v;
  if(g_nretired < NV_RETIRED) { v->retired = 1; g_retired[g_nretired++] = v; }
  else { v->retired = 2; g_retired_lost++; }      /* not known by its node: no disk at all is opened until it is let go */
  return NULL;
}

void
ps5tm_ntfs_put(ps5tm_ntfsvol_t *v) {
  if(!v) return;
  pthread_mutex_lock(&g_lock);
  int last = --v->refs == 0;
  if(last && v->retired == 2 && g_retired_lost) g_retired_lost--;
  else if(last && v->retired)
    for(unsigned i = 0; i < g_nretired; i++)
      if(g_retired[i] == v) { g_retired[i] = g_retired[--g_nretired]; break; }
  pthread_mutex_unlock(&g_lock);
  if(last) vol_free(v);
}

struct ntfs_vol *ps5tm_ntfs_handle(ps5tm_ntfsvol_t *v) { return v ? v->ntfs : NULL; }
const char *ps5tm_ntfs_name(const ps5tm_ntfsvol_t *v) { return v ? v->name : ""; }
uint64_t ps5tm_ntfs_serial(const ps5tm_ntfsvol_t *v) { return v ? v->serial : 0; }

void
ps5tm_ntfs_writing(ps5tm_ntfsvol_t *v, int on) {
  if(!v) return;
  pthread_mutex_lock(&g_lock);
  v->writing += on ? 1 : -1;
  if(v->writing < 0) v->writing = 0;
  pthread_mutex_unlock(&g_lock);
}

int
ps5tm_ntfs_reading(ps5tm_ntfsvol_t *v, int on) {
  if(!v) return 0;
  pthread_mutex_lock(&g_lock);
  if(!on) {
    if(v->reading > 0) v->reading--;
    pthread_mutex_unlock(&g_lock);
    return 0;
  }
  /* a read that begins while a file is deleted waits for that step: it then looks its file up afresh and finds it, or not (audit
     23.11.2026, before the check and the delete were two steps). While it waits no next delete starts (want_read), so it waits for
     one step only; a step that does not end within 10 s refuses the read instead of letting it in beside the delete. */
  v->want_read++;
  for(int i = 0; v->deleting > 0 && i < 2000; i++) {
    pthread_mutex_unlock(&g_lock);
    usleep(5 * 1000);
    pthread_mutex_lock(&g_lock);
  }
  v->want_read--;
  int busy = v->deleting > 0;
  if(!busy) v->reading++;
  pthread_mutex_unlock(&g_lock);
  return busy ? EBUSY : 0;
}

int
ps5tm_ntfs_delete_begin(ps5tm_ntfsvol_t *v) {
  if(!v) return 0;
  pthread_mutex_lock(&g_lock);
  int busy = v->reading > 0 || v->want_read > 0;
  if(!busy) v->deleting++;
  pthread_mutex_unlock(&g_lock);
  return busy ? EBUSY : 0;
}

void
ps5tm_ntfs_delete_end(ps5tm_ntfsvol_t *v) {
  if(!v) return;
  pthread_mutex_lock(&g_lock);
  if(v->deleting > 0) v->deleting--;
  pthread_mutex_unlock(&g_lock);
}

int
ps5tm_ntfs_closing(void) {
  pthread_mutex_lock(&g_lock);
  int c = g_closing || g_stopping;
  pthread_mutex_unlock(&g_lock);
  return c;
}

ps5tm_ntfsvol_t *
ps5tm_ntfs_get(const char *p, const char **rest) {
  if(!ps5tm_ntfs_path(p)) return NULL;
  for(int round = 0; round < 2; round++) {
    ps5tm_ntfsvol_t *found = NULL;
    pthread_mutex_lock(&g_lock);
    for(int k = 0; k < PS5TM_NTFS_MAX && !found && !g_closing; k++)
      if(g_vol[k] && !strncmp(p + 1, g_vol[k]->name, 5)) found = g_vol[k];
    if(found) found->refs++;
    int closing = g_closing;
    pthread_mutex_unlock(&g_lock);
    if(found) {
      if(rest) *rest = p[6] ? p + 6 : "/";
      return found;
    }
    if(closing) break;
    if(round == 0) ps5tm_ntfs_scan(0);              /* plugged in since the last look */
  }
  return NULL;
}

/* The reason a drive is read-only, in the words of the page (the driver says it in English). */
static void
why_words(const char *w, char *out, size_t n) {
  static const struct { const char *start, *de; } t[] = {
    { "Windows did not unmount", "Das Laufwerk wurde nicht sauber getrennt (am PC ohne „Sicher entfernen“ oder an der PS5 mitten im Schreiben abgezogen). Damit die PS5 darauf schreiben kann: an einem Windows-PC prüfen lassen, „Sicher entfernen“ wählen und wieder an die PS5 stecken." },
    { "the drive's journal", "Windows hat auf dem Laufwerk noch nicht alles zu Ende geschrieben (ohne „Sicher entfernen“ abgezogen oder Schnellstart). Damit die PS5 darauf schreiben kann: am PC „Sicher entfernen“ wählen oder Windows ganz herunterfahren, dann wieder an die PS5 stecken." },
    { "Windows is hibernated", "Das Laufwerk ist die Systemplatte eines Windows-PCs im Ruhezustand (Schnellstart). Damit die PS5 darauf schreiben kann: den PC davon starten, mit gedrückter Umschalttaste herunterfahren, dann wieder an die PS5 stecken." },
    { "chkdsk has unfinished", "Die Windows-Prüfung (chkdsk) ist auf dem Laufwerk nicht fertig geworden. Damit die PS5 darauf schreiben kann: chkdsk am Windows-PC zu Ende laufen lassen, dann das Laufwerk wieder an die PS5 stecken." },
    { "only NTFS 3", "Das Laufwerk hat ein sehr altes NTFS (älter als Windows 2000); die PS5 kann es nur lesen." },
    { "the $UpCase", "Dem Laufwerk fehlt ein Teil von NTFS ($UpCase); die PS5 kann es nur lesen. Am Windows-PC mit chkdsk prüfen." },
    { "the drive can't be opened", "Die PS5 lässt das Laufwerk nicht zum Schreiben öffnen. Abziehen, wieder einstecken und noch einmal versuchen." },
    { "the drive could not be prepared", "Das Laufwerk ließ sich nicht zum Schreiben vorbereiten (Schreibfehler). Am Windows-PC mit chkdsk prüfen." },
    { "the copy of the file table", "Die Kopie der Dateitabelle auf dem Laufwerk ($MFTMirr) passt nicht zu seinen Angaben; die PS5 kann es nur lesen. Am Windows-PC mit chkdsk prüfen." },
    { "the drive is smaller than", "Das Laufwerk meldet weniger Platz, als sein NTFS angibt (manche USB-Gehäuse und Adapter tun das); die PS5 kann es nur lesen. An einem anderen Adapter oder direkt am PC prüfen." },
  };
  for(size_t i = 0; i < sizeof(t) / sizeof(t[0]); i++)
    if(!strncmp(w, t[i].start, strlen(t[i].start))) { snprintf(out, n, "%s", t[i].de); return; }
  snprintf(out, n, "Die PS5 kann das Laufwerk nur lesen.");
}

/* Whether the drive takes writes, and why not, from the driver; while something writes to it (the driver's lock is held for seconds
   then, e.g. while it clears Windows' journal) the last answer instead, so that the places and a listing do not wait for it. */
int
ps5tm_ntfs_writable(ps5tm_ntfsvol_t *v, char *why, size_t n) {
  if(n) why[0] = 0;
  if(!v) return 0;
  pthread_mutex_lock(&g_lock);
  int cached = v->writing > 0 && v->c_valid, w = v->c_writable;
  if(cached && n) snprintf(why, n, "%s", v->c_why);
  pthread_mutex_unlock(&g_lock);
  if(cached) return w;
  char wd[PS5TM_NTFS_WHY] = "";
  int ok = ntfs_is_writable(v->ntfs);
  if(!ok) {
    char raw[300] = "";
    ntfs_can_write(v->ntfs, raw, sizeof(raw));
    why_words(raw, wd, sizeof(wd));
  }
  pthread_mutex_lock(&g_lock);
  v->c_writable = ok;
  snprintf(v->c_why, sizeof(v->c_why), "%s", wd);
  v->c_valid = 1;
  pthread_mutex_unlock(&g_lock);
  if(n) snprintf(why, n, "%s", wd);
  return ok;
}

/* Free space, as ps5tm_ntfs_writable: the last count while something writes to the drive. */
static int
free_bytes(ps5tm_ntfsvol_t *v, uint64_t *fr) {
  pthread_mutex_lock(&g_lock);
  int cached = v->writing > 0 && v->c_valid, known = v->c_free_known;
  uint64_t f = v->c_free;
  pthread_mutex_unlock(&g_lock);
  if(cached) { *fr = f; return known ? 0 : -1; }
  int rc = ntfs_free_bytes(v->ntfs, &f);
  pthread_mutex_lock(&g_lock);
  v->c_free_known = rc == 0;
  v->c_free = rc == 0 ? f : 0;
  pthread_mutex_unlock(&g_lock);
  *fr = rc == 0 ? f : 0;
  return rc;
}

#ifndef PS5TM_HOST_TEST
/* The disk numbers the console has mounted something from (/dev/daN, /dev/daNsM, /dev/daN.crypt ...): those are never opened here.
   0 when the mount table could not be read (then nothing is known). */
static int
mounted_disks(unsigned char *m, size_t n) {
  memset(m, 0, n);
  int count = getfsstat(NULL, 0, MNT_NOWAIT);
  struct statfs *list = count > 0 ? malloc(sizeof(*list) * (size_t)(count + 8)) : NULL;
  if(!list) return 0;
  count = getfsstat(list, (long)(sizeof(*list) * (size_t)(count + 8)), MNT_NOWAIT);
  for(int i = 0; i < count; i++) {
    int d = -1;
    if(sscanf(list[i].f_mntfromname, "/dev/da%d", &d) == 1 && d >= 0 && (size_t)d < n) m[d] = 1;
  }
  free(list);
  return count >= 0;
}
#endif

/* The disks to look at: whole disks without a mount (the partitions are read from their table), and the host test's images, each with
   its identity. -1 when the console's disks could not be listed: then nothing may count as gone. */
static int
candidates(char (*out)[NV_NODE], uint64_t *ident, unsigned max) {
  unsigned n = 0;
#ifndef PS5TM_HOST_TEST
  unsigned char mounted[64];
  if(!mounted_disks(mounted, sizeof(mounted))) return -1;
  DIR *d = opendir("/dev");
  if(!d) return -1;
  struct dirent *e;
  while((e = readdir(d)) != NULL && n < max) {
    const char *nm = e->d_name;
    if(strncmp(nm, "da", 2) || nm[2] < '0' || nm[2] > '9') continue;
    size_t k = 2;
    while(nm[k] >= '0' && nm[k] <= '9') k++;
    if(nm[k]) continue;                              /* da1s1, da1.crypt: not a whole disk */
    int num = atoi(nm + 2);
    if(num < 0 || num >= (int)sizeof(mounted) || mounted[num]) continue;
    snprintf(out[n++], NV_NODE, "/dev/%s", nm);
  }
  closedir(d);
#endif
  pthread_mutex_lock(&g_lock);
  for(unsigned i = 0; i < g_nsrc && n < max; i++) snprintf(out[n++], NV_NODE, "%s", g_src[i]);
  pthread_mutex_unlock(&g_lock);
  for(unsigned i = 0; i < n; i++) ident[i] = node_ident(out[i]);
  return (int)n;
}

/* The name for a new drive (under g_lock): the one this drive had before when it comes back; else the lowest that is free and was not
   let go by another drive a moment ago; else the lowest that is free. -1 when all are taken. */
static int
pick_slot_locked(uint64_t serial) {
  uint64_t now = ps5tm_mono_ms();
  for(int k = 0; k < PS5TM_NTFS_MAX; k++)
    if(!g_vol[k] && g_freed[k].at_ms && g_freed[k].serial == serial) return k;
  for(int k = 0; k < PS5TM_NTFS_MAX; k++)
    if(!g_vol[k] && (!g_freed[k].at_ms || now - g_freed[k].at_ms >= NV_NAME_MS)) return k;
  for(int k = 0; k < PS5TM_NTFS_MAX; k++)
    if(!g_vol[k]) return k;
  return -1;
}

/* Every NTFS partition of one whole disk becomes a drive. 0 something found, -1 no NTFS there, -2 the disk did not open (a new one may
   need a moment). */
static int
probe(const char *node) {
  bdev_t *b = bdev_open(node);
  if(!b) return -2;
  part_t parts[PART_MAX];
  int n = part_scan(b, parts, PART_MAX), found = 0;
  for(int i = 0; i < n; i++) {
    bdev_t *s = bdev_slice(b, parts[i].offset, parts[i].size);
    if(!s) continue;
    if(!part_is_ntfs(s)) { bdev_close(s); continue; }
    ntfs_vol_t *nv = NULL;
    char merr[160] = "";
    if(ntfs_mount(s, &nv, merr, sizeof(merr)) != 0) {
      PS5TM_WARN("ntfs_mount_failed", "NTFS: %s, Partition %d ließ sich nicht lesen: %.120s", node, parts[i].index, merr);
      bdev_close(s);
      continue;
    }
    ntfs_info_t info;
    ntfs_get_info(nv, &info);
    char why[300] = "";
    int rw = ntfs_enable_write(nv, why, sizeof(why)) == 0;   /* only allows writing: the drive is first touched by the first write */
    /* the free space is counted once here, before anyone can write: the driver keeps it up to date only from then on, and a count
       that ran beside a write would stay wrong (audit 23.11.2026) */
    uint64_t fr = 0;
    int free_known = ntfs_free_bytes(nv, &fr) == 0;
    ps5tm_ntfsvol_t *v = calloc(1, sizeof(*v));
    if(!v) { ntfs_umount(nv); continue; }
    v->ntfs = nv;
    v->serial = info.serial;
    v->size = info.total_bytes;
    v->part = parts[i].index;
    v->watch = open(node, O_RDONLY | O_CLOEXEC);
    v->refs = 1;
    v->c_free_known = free_known;
    v->c_free = fr;
    snprintf(v->node, sizeof(v->node), "%s", node);
    snprintf(v->label, sizeof(v->label), "%s", info.label);
    pthread_mutex_lock(&g_lock);
    int slot = pick_slot_locked(info.serial);
    if(slot >= 0) {
      snprintf(v->name, sizeof(v->name), "ntfs%d", slot);
      g_vol[slot] = v;
      g_freed[slot].at_ms = 0;
      found++;
    }
    pthread_mutex_unlock(&g_lock);
    if(slot < 0) {
      PS5TM_WARN("ntfs_too_many", "NTFS: %s wird nicht aufgenommen: schon %d NTFS-Laufwerke.", node, PS5TM_NTFS_MAX);
      vol_free(v);
      continue;
    }
    char wd[PS5TM_NTFS_WHY] = "";
    if(!rw) why_words(why, wd, sizeof(wd));
    PS5TM_INFO("ntfs_found", "NTFS: %s gefunden (%s, Partition %d, %s, %llu MB, NTFS %u.%u%s%.300s).", v->name, node, v->part,
               info.label[0] ? info.label : "ohne Namen", (unsigned long long)(info.total_bytes >> 20), info.ver_major, info.ver_minor,
               rw ? ", beschreibbar" : ", nur lesbar: ", rw ? "" : wd);
  }
  bdev_close(b);
  return found ? 0 : -1;
}

/* A disk that has just arrived is not opened for its first seconds: the console mounts an exFAT or FAT stick in that time (it is then
   never opened here), and opening a stick right after it appeared is what upstream issue #1 of PS5-NTFS-BROWSER reports before a kernel
   panic (23.11.2026, a precaution; the next look takes it). Only for the first probe: writing changes the node's time too. */
static int
node_young(const char *node) {
#ifndef PS5TM_HOST_TEST
  struct stat st;
  struct timespec now;
  if(stat(node, &st) != 0 || clock_gettime(CLOCK_REALTIME, &now) != 0) return 0;
  long long age = (long long)now.tv_sec - (long long)st.st_ctim.tv_sec;
  return age > -60 && age < NV_SETTLE_S;
#else
  (void)node;
  return 0;
#endif
}

/* is the disk node among the candidates (with this identity, unless id is 0)? */
static int
listed(char (*cand)[NV_NODE], const uint64_t *ident, unsigned nc, const char *node, uint64_t id) {
  for(unsigned c = 0; c < nc; c++)
    if(!strcmp(cand[c], node)) return !id || ident[c] == id;
  return 0;
}

void
ps5tm_ntfs_scan(int force) {
  if(pthread_mutex_trylock(&g_scan) != 0) return;   /* a look is under way: this caller does not wait for it */
  uint64_t now = ps5tm_mono_ms();
  if(!force && g_scan_at && now - g_scan_at < NV_SCAN_MS) { pthread_mutex_unlock(&g_scan); return; }
  g_scan_at = now;
#ifndef PS5TM_HOST_TEST
  if(ps5tm_usbeject_active()) { pthread_mutex_unlock(&g_scan); return; }   /* never beside an eject, nor in the 30 s after it */
#endif
  static char cand[NV_PROBED][NV_NODE];             /* under g_scan */
  static uint64_t ident[NV_PROBED];
  int got = candidates(cand, ident, NV_PROBED);
  if(got < 0) {                                      /* the disks could not be listed: nothing is taken as gone (audit 23.11.2026) */
    PS5TM_WARN("ntfs_scan_failed", "NTFS: Die Laufwerke ließen sich gerade nicht auflisten; es wird später noch einmal nachgesehen.");
    pthread_mutex_unlock(&g_scan);
    return;
  }
  unsigned nc = (unsigned)got;

  /* drives whose disk is gone (unplugged, or another disk under the same name): out of the list; a reader still holding one ends with
     a read error */
  ps5tm_ntfsvol_t *gone[PS5TM_NTFS_MAX];
  unsigned ngone = 0;
  pthread_mutex_lock(&g_lock);
  for(int k = 0; k < PS5TM_NTFS_MAX; k++) {
    if(!g_vol[k] || (listed(cand, ident, nc, g_vol[k]->node, 0) && vol_alive(g_vol[k]))) continue;
    PS5TM_INFO("ntfs_gone", "NTFS: %s ist nicht mehr da (%s).", g_vol[k]->name, g_vol[k]->node);
    ps5tm_ntfsvol_t *v = unlist_locked(k);
    if(v) gone[ngone++] = v;
  }
  for(unsigned i = 0; i < g_nprobe;) {               /* what was learned about a disk ends when it is unplugged */
    if(!listed(cand, ident, nc, g_probe[i].node, g_probe[i].ident)) g_probe[i] = g_probe[--g_nprobe];
    else i++;
  }
  pthread_mutex_unlock(&g_lock);
  for(unsigned i = 0; i < ngone; i++) vol_free(gone[i]);

  for(unsigned c = 0; c < nc; c++) {
    if(!ident[c]) continue;
    int skip = 0;
    nv_probe_t *p = NULL;
    pthread_mutex_lock(&g_lock);
    for(int k = 0; k < PS5TM_NTFS_MAX && !skip; k++) skip = g_vol[k] && !strcmp(g_vol[k]->node, cand[c]);
    for(unsigned i = 0; i < g_nretired && !skip; i++)     /* a drive of this disk is still held by a reader: never opened twice */
      skip = !strcmp(g_retired[i]->node, cand[c]);
    for(unsigned i = 0; i < g_nprobe && !p; i++)
      if(!strcmp(g_probe[i].node, cand[c])) p = &g_probe[i];
    if(p && (p->rejected || p->ejected)) skip = 1;
    if(p && p->next_ms && now < p->next_ms) skip = 1;
    if(g_closing || g_retired_lost) skip = 1;
    pthread_mutex_unlock(&g_lock);
    if(skip) continue;
#ifndef PS5TM_HOST_TEST
    if(ps5tm_usbeject_active()) break;              /* an eject began meanwhile: its disk is not opened beside it */
#endif
    if(node_young(cand[c])) continue;               /* just plugged in: a later look */
    int rc = probe(cand[c]);
    pthread_mutex_lock(&g_lock);
    p = NULL;
    for(unsigned i = 0; i < g_nprobe && !p; i++)
      if(!strcmp(g_probe[i].node, cand[c])) p = &g_probe[i];
    if(!p && g_nprobe < NV_PROBED) {
      p = &g_probe[g_nprobe++];
      memset(p, 0, sizeof(*p));
      snprintf(p->node, sizeof(p->node), "%s", cand[c]);
    }
    if(p) {
      p->ident = ident[c];
      if(rc == -2) { p->tries++; p->next_ms = ps5tm_mono_ms() + ((uint64_t)NV_SCAN_MS << p->tries); }
      if(rc == 0) { p->tries = 0; p->next_ms = 0; }
      p->rejected = rc == -1 || (rc == -2 && p->tries >= NV_TRIES);
    }
    pthread_mutex_unlock(&g_lock);
  }
  pthread_mutex_unlock(&g_scan);
}

unsigned
ps5tm_ntfs_list(ps5tm_ntfsinfo_t *out, unsigned max) {
  ps5tm_ntfsvol_t *held[PS5TM_NTFS_MAX];
  unsigned n = 0;
  pthread_mutex_lock(&g_lock);
  for(int k = 0; k < PS5TM_NTFS_MAX && n < max; k++)
    if(g_vol[k]) { g_vol[k]->refs++; held[n++] = g_vol[k]; }
  pthread_mutex_unlock(&g_lock);
  for(unsigned i = 0; i < n; i++) {
    ps5tm_ntfsinfo_t *o = &out[i];
    memset(o, 0, sizeof(*o));
    snprintf(o->name, sizeof(o->name), "%s", held[i]->name);
    snprintf(o->label, sizeof(o->label), "%s", held[i]->label);
    snprintf(o->node, sizeof(o->node), "%s", held[i]->node);
    o->total_bytes = held[i]->size;
    uint64_t fr = 0;
    o->free_known = free_bytes(held[i], &fr) == 0;
    o->free_bytes = o->free_known ? fr : 0;
    o->writable = ps5tm_ntfs_writable(held[i], o->why, sizeof(o->why));
    ps5tm_ntfs_put(held[i]);
  }
  return n;
}

/* "Sicher auswerfen" of a drive takes every drive of its disk (a stick with two NTFS partitions is one thing to unplug). It holds the
   look's lock throughout (order g_scan, then g_lock): a look that began before would otherwise take the disk up again after "kann
   jetzt abgezogen werden" (audit 23.11.2026). */
int
ps5tm_ntfs_eject(const char *name, char *msg, size_t n) {
  ps5tm_ntfsvol_t *free_now[PS5TM_NTFS_MAX];
  unsigned nfree = 0;
  char names[64] = "", node[NV_NODE] = "";
  int busy = 0, found = 0;
  pthread_mutex_lock(&g_scan);
  pthread_mutex_lock(&g_lock);
  int k0 = -1;
  for(int k = 0; k < PS5TM_NTFS_MAX && k0 < 0; k++)
    if(g_vol[k] && !strcmp(g_vol[k]->name, name)) k0 = k;
  if(k0 >= 0) {
    found = 1;
    snprintf(node, sizeof(node), "%s", g_vol[k0]->node);
    for(int k = 0; k < PS5TM_NTFS_MAX; k++)
      if(g_vol[k] && !strcmp(g_vol[k]->node, node) && g_vol[k]->refs > 1) busy = 1;   /* a copy, a listing or a download holds it */
    if(!busy) {
      for(int k = 0; k < PS5TM_NTFS_MAX; k++) {
        if(!g_vol[k] || strcmp(g_vol[k]->node, node)) continue;
        size_t l = strlen(names);
        snprintf(names + l, sizeof(names) - l, "%s%s", l ? ", " : "", g_vol[k]->name);
        g_vol[k]->refs++;                            /* kept for the flush below */
        free_now[nfree++] = g_vol[k];
        ps5tm_ntfsvol_t *v = unlist_locked(k);       /* never the last reference: the one above is still held */
        (void)v;
      }
      nv_probe_t *p = NULL;                          /* not taken up again until it is unplugged */
      for(unsigned i = 0; i < g_nprobe && !p; i++)
        if(!strcmp(g_probe[i].node, node)) p = &g_probe[i];
      int fresh = 0;
      if(!p && g_nprobe < NV_PROBED) { p = &g_probe[g_nprobe++]; fresh = 1; }
      for(unsigned i = 0; i < g_nprobe && !p; i++)    /* all taken: a marker that holds nothing (a disk still being tried) goes */
        if(!g_probe[i].ejected && !g_probe[i].rejected) { p = &g_probe[i]; fresh = 1; }
      for(unsigned i = 0; i < g_nprobe && !p; i++)    /* then an "ejected" outweighs what was learned about another disk */
        if(!g_probe[i].ejected) { p = &g_probe[i]; fresh = 1; }
      if(p && fresh) {
        memset(p, 0, sizeof(*p));
        snprintf(p->node, sizeof(p->node), "%s", node);
      }
      if(p) { p->ejected = 1; p->ident = 0; }      /* its identity below, outside g_lock (the look cannot run meanwhile) */
    }
  }
  pthread_mutex_unlock(&g_lock);
  if(busy || !found) pthread_mutex_unlock(&g_scan);
  if(busy) { snprintf(msg, n, "Das Laufwerk wird gerade benutzt (Kopieren, Herunterladen oder eine Liste). Bitte danach noch einmal."); return 409; }
  if(!found) { snprintf(msg, n, "Dieses NTFS-Laufwerk ist nicht (mehr) da."); return 404; }
  int rc = 0;
  uint64_t id0 = node_ident(node);
  for(unsigned i = 0; i < nfree; i++) {
    if(ntfs_sync(free_now[i]->ntfs) != 0) rc = 1;
    ntfs_flush_cache(free_now[i]->ntfs);             /* the stick's own write cache too (a stick that cannot is no error) */
    ps5tm_ntfs_put(free_now[i]);                     /* the last reference: unmounted and freed */
  }
  /* the identity the marker keeps: the node's after the flush (an image file's time changes when written; a device node's does not).
     Gone meanwhile (unplugged during the flush): no marker, else it would stand for any disk that comes under this name (second
     counter-check 23.11.2026) */
  uint64_t id = node_ident(node);
  if(!id0) id = 0;
  pthread_mutex_lock(&g_lock);
  for(unsigned i = 0; i < g_nprobe;) {
    if(!strcmp(g_probe[i].node, node) && g_probe[i].ejected) {
      if(!id) { g_probe[i] = g_probe[--g_nprobe]; continue; }
      g_probe[i].ident = id;
    }
    i++;
  }
  pthread_mutex_unlock(&g_lock);
  pthread_mutex_unlock(&g_scan);
  if(rc) {
    snprintf(msg, n, "Beim Schreiben auf %s gab es einen Fehler. Das Laufwerk ist freigegeben; am PC mit chkdsk prüfen, bevor es weiter benutzt wird.", names);
    PS5TM_WARN("ntfs_eject_flush", "NTFS: %s: Schreiben beim Auswerfen fehlgeschlagen.", names);
    return 500;
  }
  snprintf(msg, n, "%s kann jetzt abgezogen werden.", names);
  PS5TM_INFO("ntfs_ejected", "NTFS: %s ausgeworfen.", names);
  return 0;
}

static void
sync_listed(void) {
  ps5tm_ntfsvol_t *list[PS5TM_NTFS_MAX];
  unsigned n = 0;
  pthread_mutex_lock(&g_lock);
  for(int k = 0; k < PS5TM_NTFS_MAX; k++)
    if(g_vol[k]) { g_vol[k]->refs++; list[n++] = g_vol[k]; }
  pthread_mutex_unlock(&g_lock);
  for(unsigned i = 0; i < n; i++) {
    (void)ntfs_sync(list[i]->ntfs);
    ps5tm_ntfs_put(list[i]);
  }
}

/* The app is asked to end: an upload stops at its next block (a copy is cancelled by the file manager itself). */
void
ps5tm_ntfs_shutdown_begin(void) {
  pthread_mutex_lock(&g_lock);
  g_stopping = 1;
  pthread_mutex_unlock(&g_lock);
}

/* What is written goes out now, early (audit 23.11.2026: the end of the process may come before ps5tm_ntfs_shutdown is through); a drive
   that is being written to is left to the end: its driver lock may be held for seconds (the first write clears the journal). */
void
ps5tm_ntfs_sync_idle(void) {
  ps5tm_ntfsvol_t *list[PS5TM_NTFS_MAX];
  unsigned n = 0;
  pthread_mutex_lock(&g_lock);
  for(int k = 0; k < PS5TM_NTFS_MAX; k++)
    if(g_vol[k] && g_vol[k]->writing == 0) { g_vol[k]->refs++; list[n++] = g_vol[k]; }
  pthread_mutex_unlock(&g_lock);
  for(unsigned i = 0; i < n; i++) {
    (void)ntfs_sync(list[i]->ntfs);
    ps5tm_ntfs_put(list[i]);
  }
}

/* The app ends: what still works on a drive (the end of a cancelled copy, an upload) gets a short while to finish its driver step, then
   everything is written out again (audit 23.11.2026: before, the flush could run in the middle of a step). */
void
ps5tm_ntfs_shutdown(void) {
  pthread_mutex_lock(&g_lock);
  g_closing = 1;
  pthread_mutex_unlock(&g_lock);
  for(int i = 0; i < 15; i++) {                      /* at most 1.5 s */
    int held = 0;
    pthread_mutex_lock(&g_lock);
    for(int k = 0; k < PS5TM_NTFS_MAX; k++) if(g_vol[k] && (g_vol[k]->refs > 1 || g_vol[k]->writing > 0)) held = 1;
    if(g_nretired || g_retired_lost) held = 1;
    pthread_mutex_unlock(&g_lock);
    if(!held) break;
    usleep(100 * 1000);
  }
  sync_listed();
}
