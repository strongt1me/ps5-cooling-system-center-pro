/* NTFS drives for the file manager (ntfsvol.h, 23.11.2026). After the pattern of vfs.c of PS5-NTFS-BROWSER (abcde6543, v1.4.0,
 * GPL-3.0-or-later): every NTFS partition found in the partition table of a whole disk becomes a drive; the driver is that project's
 * (src/third_party/ntfsb). What differs from vfs.c, for this console's other users of the USB drives:
 *  - only whole disks (daN) are opened, never one the console has mounted something from (an exFAT stick in use, f_mntfromname),
 *  - no look while a drive is being ejected or settles after it (the crashes around ejecting, 01.-02.10. and 21.11.2026),
 *  - a look happens only when the file manager asks (its places every 3-4 s while it is open), at most every 3 s, and a disk that is
 *    no NTFS is opened once until it is unplugged,
 *  - names like the console's drives: ntfs0, ntfs1 ... the lowest that is free. */
#include <dirent.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>

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

struct ps5tm_ntfsvol {
  char         name[8];
  char         node[NV_NODE];
  char         label[128];
  int          part;                     /* partition number on the disk */
  ntfs_vol_t  *ntfs;
  uint64_t     serial, size;
  int          refs;                     /* the list holds one while the drive is listed */
};

typedef struct {
  char node[NV_NODE];
  int  rejected;                         /* no NTFS on it (or it never opened) */
  int  ejected;                          /* "Sicher auswerfen": not taken up again until it is unplugged */
  int  tries;
} nv_probe_t;

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;   /* the list, references, probe results, sources */
static pthread_mutex_t g_scan = PTHREAD_MUTEX_INITIALIZER;   /* one look at a time */
static ps5tm_ntfsvol_t *g_vol[PS5TM_NTFS_MAX];
static nv_probe_t       g_probe[NV_PROBED];
static unsigned         g_nprobe;
static char             g_src[NV_SOURCES][NV_NODE];
static unsigned         g_nsrc;
static uint64_t         g_scan_at;

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

static void
vol_free(ps5tm_ntfsvol_t *v) {
  if(!v) return;
  if(v->ntfs) ntfs_umount(v->ntfs);               /* writes out what is pending when it was written to */
  free(v);
}

void
ps5tm_ntfs_put(ps5tm_ntfsvol_t *v) {
  if(!v) return;
  pthread_mutex_lock(&g_lock);
  int last = --v->refs == 0;
  pthread_mutex_unlock(&g_lock);
  if(last) vol_free(v);
}

struct ntfs_vol *ps5tm_ntfs_handle(ps5tm_ntfsvol_t *v) { return v ? v->ntfs : NULL; }
const char *ps5tm_ntfs_name(const ps5tm_ntfsvol_t *v) { return v ? v->name : ""; }
uint64_t ps5tm_ntfs_serial(const ps5tm_ntfsvol_t *v) { return v ? v->serial : 0; }

ps5tm_ntfsvol_t *
ps5tm_ntfs_get(const char *p, const char **rest) {
  if(!ps5tm_ntfs_path(p)) return NULL;
  for(int round = 0; round < 2; round++) {
    ps5tm_ntfsvol_t *found = NULL;
    pthread_mutex_lock(&g_lock);
    for(int k = 0; k < PS5TM_NTFS_MAX && !found; k++)
      if(g_vol[k] && !strncmp(p + 1, g_vol[k]->name, 5)) found = g_vol[k];
    if(found) found->refs++;
    pthread_mutex_unlock(&g_lock);
    if(found) {
      if(rest) *rest = p[6] ? p + 6 : "/";
      return found;
    }
    if(round == 0) ps5tm_ntfs_scan(0);              /* plugged in since the last look */
  }
  return NULL;
}

/* The reason a drive is read-only, in the words of the page (the driver says it in English). */
static void
why_words(const char *w, char *out, size_t n) {
  static const struct { const char *start, *de; } t[] = {
    { "Windows did not unmount", "Das Laufwerk wurde zuletzt an einem Windows-PC ohne „Sicher entfernen“ abgezogen. Damit die PS5 darauf schreiben kann: am PC einstecken, „Sicher entfernen“ wählen und wieder an die PS5 stecken." },
    { "the drive's journal", "Windows hat auf dem Laufwerk noch nicht alles zu Ende geschrieben (Schnellstart). Damit die PS5 darauf schreiben kann: am PC „Sicher entfernen“ wählen oder Windows ganz herunterfahren, dann wieder an die PS5 stecken." },
    { "Windows is hibernated", "Das Laufwerk ist die Systemplatte eines Windows-PCs im Ruhezustand (Schnellstart). Damit die PS5 darauf schreiben kann: den PC davon starten, mit gedrückter Umschalttaste herunterfahren, dann wieder an die PS5 stecken." },
    { "chkdsk has unfinished", "Die Windows-Prüfung (chkdsk) ist auf dem Laufwerk nicht fertig geworden. Damit die PS5 darauf schreiben kann: chkdsk am Windows-PC zu Ende laufen lassen, dann das Laufwerk wieder an die PS5 stecken." },
    { "only NTFS 3", "Das Laufwerk hat ein sehr altes NTFS (älter als Windows 2000); die PS5 kann es nur lesen." },
    { "the $UpCase", "Dem Laufwerk fehlt ein Teil von NTFS ($UpCase); die PS5 kann es nur lesen. Am Windows-PC mit chkdsk prüfen." },
    { "the drive can't be opened", "Die PS5 lässt das Laufwerk nicht zum Schreiben öffnen. Abziehen, wieder einstecken und noch einmal versuchen." },
    { "the drive could not be prepared", "Das Laufwerk ließ sich nicht zum Schreiben vorbereiten (Schreibfehler). Am Windows-PC mit chkdsk prüfen." },
  };
  for(size_t i = 0; i < sizeof(t) / sizeof(t[0]); i++)
    if(!strncmp(w, t[i].start, strlen(t[i].start))) { snprintf(out, n, "%s", t[i].de); return; }
  snprintf(out, n, "Die PS5 kann das Laufwerk nur lesen.");
}

int
ps5tm_ntfs_writable(ps5tm_ntfsvol_t *v, char *why, size_t n) {
  if(n) why[0] = 0;
  if(!v) return 0;
  if(ntfs_is_writable(v->ntfs)) return 1;
  char w[300] = "";
  ntfs_can_write(v->ntfs, w, sizeof(w));
  if(n) why_words(w, why, n);
  return 0;
}

#ifndef PS5TM_HOST_TEST
/* The disk numbers the console has mounted something from (/dev/daN, /dev/daNsM, /dev/daN.crypt ...): those are never opened here.
   When the mount table cannot be read, every disk counts as mounted (nothing is opened). */
static void
mounted_disks(unsigned char *m, size_t n) {
  memset(m, 0, n);
  int count = getfsstat(NULL, 0, MNT_NOWAIT);
  struct statfs *list = count > 0 ? malloc(sizeof(*list) * (size_t)(count + 8)) : NULL;
  if(!list) { memset(m, 1, n); return; }
  count = getfsstat(list, (long)(sizeof(*list) * (size_t)(count + 8)), MNT_NOWAIT);
  if(count < 0) memset(m, 1, n);
  for(int i = 0; i < count; i++) {
    int d = -1;
    if(sscanf(list[i].f_mntfromname, "/dev/da%d", &d) == 1 && d >= 0 && (size_t)d < n) m[d] = 1;
  }
  free(list);
}
#endif

/* The disks to look at: whole disks without a mount (the partitions are read from their table), and the host test's images. */
static unsigned
candidates(char (*out)[NV_NODE], unsigned max) {
  unsigned n = 0;
#ifndef PS5TM_HOST_TEST
  unsigned char mounted[64];
  mounted_disks(mounted, sizeof(mounted));
  DIR *d = opendir("/dev");
  if(d) {
    struct dirent *e;
    while((e = readdir(d)) != NULL && n < max) {
      const char *nm = e->d_name;
      if(strncmp(nm, "da", 2) || nm[2] < '0' || nm[2] > '9') continue;
      size_t k = 2;
      while(nm[k] >= '0' && nm[k] <= '9') k++;
      if(nm[k]) continue;                            /* da1s1, da1.crypt: not a whole disk */
      int num = atoi(nm + 2);
      if(num < 0 || num >= (int)sizeof(mounted) || mounted[num]) continue;
      snprintf(out[n++], NV_NODE, "/dev/%s", nm);
    }
    closedir(d);
  }
#endif
  pthread_mutex_lock(&g_lock);
  for(unsigned i = 0; i < g_nsrc && n < max; i++) snprintf(out[n++], NV_NODE, "%s", g_src[i]);
  pthread_mutex_unlock(&g_lock);
  return n;
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
    ps5tm_ntfsvol_t *v = calloc(1, sizeof(*v));
    if(!v) { ntfs_umount(nv); continue; }
    v->ntfs = nv;
    v->serial = info.serial;
    v->size = info.total_bytes;
    v->part = parts[i].index;
    v->refs = 1;
    snprintf(v->node, sizeof(v->node), "%s", node);
    snprintf(v->label, sizeof(v->label), "%s", info.label);
    int slot = -1;
    pthread_mutex_lock(&g_lock);
    for(int k = 0; k < PS5TM_NTFS_MAX && slot < 0; k++)
      if(!g_vol[k]) slot = k;
    if(slot >= 0) {
      snprintf(v->name, sizeof(v->name), "ntfs%d", slot);
      g_vol[slot] = v;
      found++;
    }
    pthread_mutex_unlock(&g_lock);
    if(slot < 0) {
      PS5TM_WARN("ntfs_too_many", "NTFS: %s wird nicht aufgenommen: schon %d NTFS-Laufwerke.", node, PS5TM_NTFS_MAX);
      vol_free(v);
      continue;
    }
    char wd[300] = "";
    if(!rw) why_words(why, wd, sizeof(wd));
    PS5TM_INFO("ntfs_found", "NTFS: %s gefunden (%s, Partition %d, %s, %llu MB, NTFS %u.%u%s%.200s).", v->name, node, v->part,
               info.label[0] ? info.label : "ohne Namen", (unsigned long long)(info.total_bytes >> 20), info.ver_major, info.ver_minor,
               rw ? ", beschreibbar" : ", nur lesbar: ", rw ? "" : wd);
  }
  bdev_close(b);
  return found ? 0 : -1;
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
  unsigned nc = candidates(cand, NV_PROBED);

  /* drives whose disk is gone (unplugged): out of the list; a reader still holding one ends with a read error */
  ps5tm_ntfsvol_t *gone[PS5TM_NTFS_MAX];
  unsigned ngone = 0;
  pthread_mutex_lock(&g_lock);
  for(int k = 0; k < PS5TM_NTFS_MAX; k++) {
    if(!g_vol[k]) continue;
    int listed = 0;
    for(unsigned c = 0; c < nc && !listed; c++) listed = !strcmp(cand[c], g_vol[k]->node);
    if(listed) continue;
    PS5TM_INFO("ntfs_gone", "NTFS: %s ist nicht mehr da (%s).", g_vol[k]->name, g_vol[k]->node);
    if(--g_vol[k]->refs == 0) gone[ngone++] = g_vol[k];
    g_vol[k] = NULL;
  }
  for(unsigned i = 0; i < g_nprobe;) {               /* what was learned about a disk ends when it is unplugged */
    int listed = 0;
    for(unsigned c = 0; c < nc && !listed; c++) listed = !strcmp(cand[c], g_probe[i].node);
    if(!listed) g_probe[i] = g_probe[--g_nprobe];
    else i++;
  }
  pthread_mutex_unlock(&g_lock);
  for(unsigned i = 0; i < ngone; i++) vol_free(gone[i]);

  for(unsigned c = 0; c < nc; c++) {
    int skip = 0;
    nv_probe_t *p = NULL;
    pthread_mutex_lock(&g_lock);
    for(int k = 0; k < PS5TM_NTFS_MAX && !skip; k++) skip = g_vol[k] && !strcmp(g_vol[k]->node, cand[c]);
    for(unsigned i = 0; i < g_nprobe && !p; i++)
      if(!strcmp(g_probe[i].node, cand[c])) p = &g_probe[i];
    if(p && (p->rejected || p->ejected)) skip = 1;
    pthread_mutex_unlock(&g_lock);
    if(skip) continue;
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
      if(rc == -2) p->tries++;
      if(rc == 0) p->tries = 0;
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
    o->free_known = ntfs_free_bytes(held[i]->ntfs, &fr) == 0;   /* the first time reads the drive's bitmap, then it is kept */
    o->free_bytes = o->free_known ? fr : 0;
    o->writable = ps5tm_ntfs_writable(held[i], o->why, sizeof(o->why));
    ps5tm_ntfs_put(held[i]);
  }
  return n;
}

int
ps5tm_ntfs_eject(const char *name, char *msg, size_t n) {
  ps5tm_ntfsvol_t *v = NULL;
  int busy = 0;
  pthread_mutex_lock(&g_lock);
  for(int k = 0; k < PS5TM_NTFS_MAX && !v; k++) {
    if(!g_vol[k] || strcmp(g_vol[k]->name, name)) continue;
    if(g_vol[k]->refs > 1) { busy = 1; break; }     /* a copy, a listing or a download holds it */
    v = g_vol[k];
    g_vol[k] = NULL;
    nv_probe_t *p = NULL;                            /* not taken up again until it is unplugged */
    for(unsigned i = 0; i < g_nprobe && !p; i++)
      if(!strcmp(g_probe[i].node, v->node)) p = &g_probe[i];
    if(!p && g_nprobe < NV_PROBED) {
      p = &g_probe[g_nprobe++];
      memset(p, 0, sizeof(*p));
      snprintf(p->node, sizeof(p->node), "%s", v->node);
    }
    if(p) p->ejected = 1;
  }
  pthread_mutex_unlock(&g_lock);
  if(busy) { snprintf(msg, n, "Das Laufwerk wird gerade benutzt (Kopieren, Herunterladen oder eine Liste). Bitte danach noch einmal."); return 409; }
  if(!v) { snprintf(msg, n, "Dieses NTFS-Laufwerk ist nicht (mehr) da."); return 404; }
  int rc = ntfs_sync(v->ntfs);
  char nm[8];
  snprintf(nm, sizeof(nm), "%s", v->name);
  vol_free(v);                                       /* the list held the only reference */
  if(rc) {
    snprintf(msg, n, "Beim Schreiben auf %s gab es einen Fehler. Das Laufwerk ist freigegeben; am PC mit chkdsk prüfen, bevor es weiter benutzt wird.", nm);
    PS5TM_WARN("ntfs_eject_flush", "NTFS: %s: Schreiben beim Auswerfen fehlgeschlagen.", nm);
    return 500;
  }
  snprintf(msg, n, "%s kann jetzt abgezogen werden.", nm);
  PS5TM_INFO("ntfs_ejected", "NTFS: %s ausgeworfen.", nm);
  return 0;
}

void
ps5tm_ntfs_shutdown(void) {
  ps5tm_ntfsvol_t *held[PS5TM_NTFS_MAX];
  unsigned n = 0;
  pthread_mutex_lock(&g_lock);
  for(int k = 0; k < PS5TM_NTFS_MAX; k++)
    if(g_vol[k]) { g_vol[k]->refs++; held[n++] = g_vol[k]; }
  pthread_mutex_unlock(&g_lock);
  for(unsigned i = 0; i < n; i++) {
    (void)ntfs_sync(held[i]->ntfs);
    ps5tm_ntfs_put(held[i]);
  }
}
