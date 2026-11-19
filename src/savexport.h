/* PS5 Cooling & System Center - Pro: a save of this console, read out into a ZIP of plain files (savexport.h, 19.11.2026).
 *
 * A save is an encrypted image bound to this console. To carry it to another console its files are read in the clear - through the console's own mount (savemnt.h),
 * nothing is decrypted by this app - and put into a ZIP together with the sidecar chives.json (chives.h), which says how to build the image again.
 *
 * The order, chosen so that the console is changed as little and for as short a time as possible:
 *   1. the image is COPIED to the work folder - the original is never mounted, never written (a mount straight from where the console keeps it fails too);
 *   2. the copy is mounted (the identity of the save-data service and the Sony lock are held from here ...), its files are copied out to a stage folder,
 *      the real param.sfo is read from the image header;
 *   3. the copy is unmounted (... to here), and deleted;
 *   4. the ZIP is written from the stage folder - checksums taken, data written, the sidecar last - under a ".partial" name and renamed when whole;
 *   5. the stage folder is deleted.
 * A failure or a stop at any point removes what this run made (a mount that will not let go is the one thing that stays, and is reported).
 *
 * The engine knows nothing of jobs or HTTP: the caller gives paths and hooks. */
#ifndef PS5TM_SAVEXPORT_H
#define PS5TM_SAVEXPORT_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
  void *ctx;
  /* a new step: its text and its size in bytes (0 when not known) */
  void (*phase)(void *ctx, const char *text, uint64_t total);
  /* bytes done in the step */
  void (*work)(void *ctx, uint64_t bytes);
  /* the file being worked on (a name below the save's root) */
  void (*current)(void *ctx, const char *rel);
  /* non-zero: stop as soon as possible */
  int (*stop)(void *ctx);
} savexport_hooks_t;

typedef struct {
  const char *image;       /* the console's image: .../savedata_prospero/<PPSA>/sdimg_<slot>, or for a PS4 save .../savedata/<CUSA>/sdimg_<slot> (its key is read from <slot>.bin beside it) */
  const char *title_id;    /* only a fallback when the save's own metadata does not say */
  const char *slot;
  const char *work_dir;    /* scratch space on the internal drive, made if missing; its content is this engine's to delete */
  const char *out_dir;     /* where the ZIP goes, made if missing */
  const char *zip_name;    /* the file name of the ZIP (no folder) */
} savexport_args_t;

typedef struct {
  char     zip[320];       /* the ZIP made */
  unsigned files;          /* files in it (the sidecar not counted) */
  uint64_t bytes;          /* their size together */
  int      sfo_from_header;/* 1: the real param.sfo came from the image header */
  unsigned skipped;        /* entries of the save that are neither a file nor a folder (left out) */
  char     title_id[16];   /* as the save's metadata says */
  char     save_dir[96];
  char     note[560];      /* something the person should know although it worked (empty mostly) */
} savexport_result_t;

/* 0 on success; ECANCELED when stopped; any other value is a failure, err holds a sentence for the person. */
int savexport_run(const savexport_args_t *a, const savexport_hooks_t *h, savexport_result_t *r, char *err, size_t err_len);

#endif
