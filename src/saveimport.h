/* PS5 Cooling & System Center - Pro: a save from a ZIP of plain files put in on this console (saveimport.h, 19.11.2026).
 *
 * The other half of savexport.h. A ZIP made by savexport (or by the tool the community uses for this, which writes the same chives.json) holds the files of a save in the
 * clear. This console cannot open the image they came from - its key is sealed to the console that made it - so a NEW image is made here by the console's own formatter, the
 * files are written into it through the console's own mount (which encrypts them with the new image's key), the owner in its param.sfo is set to the user the save goes to,
 * and the image is put where the console keeps it, together with its picture and its row in the save database.
 *
 * PS4 saves (CUSA titles) travel as their files alone - the other tool writes no sidecar for them. They are recognised by the title in the param.sfo, the image is made
 * with the size such a save needs (files + 25 % + 4 MB, at least 32 MB, in 32 KiB steps) by the console's older formatter with a fresh key, which goes into the
 * companion file <slot>.bin, and the owner in the param.sfo is made right from a save of the same user (see saveimport.c: the PARAMS record of a PS4 save).
 *
 * Only the layout this app can make exactly is accepted (what a firmware like 12.00 writes: 32 or 64 KiB blocks, no copy-on-write area, a real param.sfo in the file system,
 * SAVEDATA_BLOCKS equal to the block count): saves of newer firmware, whose images carry their metadata in the header, are refused with the reason, never guessed.
 *
 * Order, so that the console changes as late and as little as possible:
 *   1. the ZIP is read through and checked (nothing is written);
 *   2. a new image is made in the work folder and formatted; its geometry must be the one the ZIP says;
 *   3. it is mounted (identity and Sony lock held, as in savexport), the files are written with their modes and owners, param.sfo gets the user's account and id; unmounted;
 *   4. it is mounted a second time and every file is read back and compared with the ZIP (checksum and size) - nothing of the console is touched until this passes;
 *   5. the caller's "no way back" hook is called, then: the image goes beside the old one as ".ps5cc-neu" and replaces it by rename (the old one is kept as ".ps5cc-alt"
 *      until the end), the picture likewise, the database is copied, the row is written and the database is checked; any failure puts the old files and database back.
 * A failure or a stop before step 5 leaves the console exactly as it was.
 *
 * Plain C, no HTTP and no job: the caller gives paths and hooks (savexport_hooks_t, plus the one hook of no return). */
#ifndef PS5TM_SAVEIMPORT_H
#define PS5TM_SAVEIMPORT_H

#include <stddef.h>
#include <stdint.h>

#include "savexport.h"

typedef struct {
  char     title_id[16];
  char     slot[100];
  char     main_title[256];
  char     sub_title[256];
  char     detail[1024];
  char     prefix[200];            /* the folder in the ZIP that holds the save ("" at the root) */
  uint64_t image_size, block_count;
  uint32_t block_size;
  uint32_t user_param;
  uint64_t src_account;            /* the owner the save has in the ZIP */
  uint32_t src_user;
  unsigned files, dirs;
  uint64_t bytes;
  int      ps4;                    /* 1: a PS4 save (no sidecar: the image size is estimated, the key is made new and goes into <slot>.bin) */
  int      importable;             /* 1 when this console can put it in; else why says what is wrong */
  char     why[320];
} saveimport_info_t;

/* Reads the ZIP through (central directory, the sidecar, the param.sfo) without writing anything. 0 when it is a readable ZIP (check info->importable), else -1 with err. */
int saveimport_inspect(const char *zip, saveimport_info_t *info, char *err, size_t err_len);

typedef struct {
  savexport_hooks_t base;
  /* called once, before the first file of the console is changed; from then on a stop is no longer taken */
  void (*no_return)(void *ctx);
} saveimport_hooks_t;

typedef struct {
  const char *zip;
  const char *work_dir;            /* scratch, made if missing */
  const char *home_dir;            /* /user/home */
  const char *sys_dir;             /* /system_data */
  const char *uid;                 /* the user the save goes to: 8 hex digits, a folder below home_dir */
  uint32_t    user_id;             /* the same as a number */
  uint64_t    account_id;          /* the account the user has on this console (0 when none) */
  const char *db_backup_dir;       /* where the copy of the database goes before the row is written */
} saveimport_args_t;

typedef struct {
  char     title_id[16];
  char     slot[100];
  char     image[320];             /* the image as it now lies on the console */
  char     db_backup[320];         /* the copy of the database that was made ("" when there was none to copy) */
  int      replaced;               /* 1: an image of that slot was there and was replaced */
  int      db_inserted;            /* 1: a new row, 0: the row was updated */
  unsigned files;
  uint64_t bytes;
  char     note[560];
} saveimport_result_t;

/* 0 on success; ECANCELED when stopped (before the point of no return); any other value is a failure, err holds a sentence. */
int saveimport_run(const saveimport_args_t *a, const saveimport_hooks_t *h, saveimport_result_t *r, char *err, size_t err_len);

#endif
