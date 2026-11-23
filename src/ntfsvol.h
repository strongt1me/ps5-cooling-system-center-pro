/* NTFS drives in the file manager (23.11.2026, the user's wish: "PS5-NTFS-BROWSER" of abcde6543 in the app, reading and writing).
 *
 * The console itself mounts only exFAT and FAT32; a stick formatted as NTFS stays a raw device (/dev/daN). The driver of that project
 * (src/third_party/ntfsb, GPL-3.0-or-later, v1.4.0) reads such a drive directly and can add files and folders and delete them; it never
 * changes or renames a file that is there. This module finds the drives and gives each a name like the console's own drives: ntfs0,
 * ntfs1 ... (the lowest that is free). In the file manager a drive is the path /ntfsN, its folders /ntfsN/...; src/fm_ntfs.inc does the
 * rest there.
 *
 * Looking for drives opens only whole disks (daN) that the console has not mounted, never while a drive is being ejected or settles
 * after it (ps5tm_usbeject_active), and each disk once: what is no NTFS is remembered until it is unplugged. A clean drive is writable
 * at once (as in the original); a drive Windows did not close cleanly (dirty, Fast Startup, journal) stays read-only, with the reason.
 * Nothing is written until something is copied, uploaded, made or deleted there. */
#ifndef PS5TM_NTFSVOL_H
#define PS5TM_NTFSVOL_H

#include <stddef.h>
#include <stdint.h>

struct ntfs_vol;
typedef struct ps5tm_ntfsvol ps5tm_ntfsvol_t;

#define PS5TM_NTFS_MAX 8                 /* drives at once */

typedef struct {
  char     name[8];                      /* "ntfs0" */
  char     label[64];                    /* the drive's own name (may be empty) */
  char     node[48];                     /* "/dev/da2" */
  uint64_t total_bytes, free_bytes;      /* free_bytes 0 when it could not be read */
  int      free_known;
  int      writable;
  char     why[200];                     /* why it is read-only (empty when writable) */
} ps5tm_ntfsinfo_t;

/* 1 when p is an NTFS drive's path ("/ntfs3" or "/ntfs3/..."), whether or not that drive is there. */
int  ps5tm_ntfs_path(const char *p);

/* Looks for drives that came or went; at most every 3 s unless force. Never waits for a look that is under way. */
void ps5tm_ntfs_scan(int force);

/* The drive of path p with a reference (give it back with ps5tm_ntfs_put), and in *rest the path inside it ("/" for its root).
   NULL when that drive is not there (*err says so). */
ps5tm_ntfsvol_t *ps5tm_ntfs_get(const char *p, const char **rest);
void ps5tm_ntfs_put(ps5tm_ntfsvol_t *v);
struct ntfs_vol *ps5tm_ntfs_handle(ps5tm_ntfsvol_t *v);
const char *ps5tm_ntfs_name(const ps5tm_ntfsvol_t *v);
uint64_t ps5tm_ntfs_serial(const ps5tm_ntfsvol_t *v);
/* 1 writable; 0 read-only with the reason in why. */
int  ps5tm_ntfs_writable(ps5tm_ntfsvol_t *v, char *why, size_t n);

/* The drives there are now, for the places of the file manager. */
unsigned ps5tm_ntfs_list(ps5tm_ntfsinfo_t *out, unsigned max);

/* "Sicher auswerfen": writes out what is pending and lets the drive go; it is not taken up again until it is plugged in anew.
   0, or an HTTP status with the reason (409 while the file manager uses it, 404 not there, 500 the flush failed). */
int  ps5tm_ntfs_eject(const char *name, char *msg, size_t n);

/* The app ends: what is written is flushed to the drives. */
void ps5tm_ntfs_shutdown(void);

/* Host tests: an image file to be looked at like a drive. */
void ps5tm_ntfs_add_source(const char *path);

#endif
