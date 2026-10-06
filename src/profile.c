/* The console user's profile: reading and renaming the local display name.
 *
 * Ported from ps5upload's payload/src/profile.c (GPL-3.0, phantomptr), which
 * documents the calls and their privilege requirements. Two deliberate
 * departures from the reference:
 *
 *  - It resolves sceUserService* through dlsym. We link them weakly instead.
 *    A Sony library exports by NID, not by name, and the name exists only in
 *    the SDK's stub — so only the linker can reach these functions. That cost
 *    this project several rounds on real hardware to establish; see the note
 *    in sysinfo.c.
 *
 *  - It also drives the offline-account registry slots. Those write raw
 *    sceRegMgr keys and are a different, riskier feature; only the local user
 *    rename is implemented here.
 *
 * This file is the single owner of sceUserService in the payload. Everything
 * else asks it, so the names stay out of dlsym's reach: a weak symbol answers
 * to its own name and hands back an address that is not NULL and not usable.
 * Mixing the two is what silently broke the ICC counters.
 *
 * Privilege: renaming needs the elevated ucred the jailbreak installs. Without
 * it the call returns a Sony error code, which is reported rather than hidden.
 */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "ps5tm.h"

#ifndef PS5TM_HOST_TEST
extern int sceUserServiceInitialize(void *)                   __attribute__((weak));
extern int sceUserServiceGetForegroundUser(int *)             __attribute__((weak));
extern int sceUserServiceGetInitialUser(int *)                __attribute__((weak));
extern int sceUserServiceGetUserName(int32_t, char *, size_t) __attribute__((weak));
extern int sceUserServiceSetUserName(int32_t, const char *)   __attribute__((weak));
#endif

/* Initialised once, on first use, under the Sony lock — and under nothing else.
 *
 * This used to be pthread_once(), with the Sony lock taken inside the once: two
 * locks, taken in opposite orders. The pad probe holds the Sony lock and, as
 * its first act, reaches this function; an HTTP thread asking for the user's
 * name can already be inside the once, waiting for the Sony lock. Each waits for
 * the other for good, and the fan worker, which needs the Sony lock once a
 * second, stops with them. (Reproduced with this file and sony_api_lock.c: the
 * pair hangs.) The Sony lock is recursive, so a caller that already holds it
 * simply carries on, and there is no second lock left to invert against. */
static int g_service_ready = 0;

void
ps5tm_user_service_init(void) {
  if(__atomic_load_n(&g_service_ready, __ATOMIC_ACQUIRE)) return;

#ifndef PS5TM_HOST_TEST
  ps5tm_sony_api_lock();
  if(!__atomic_load_n(&g_service_ready, __ATOMIC_RELAXED)) {
    /* Idempotent, and the name calls do nothing until it has run. */
    if(sceUserServiceInitialize) sceUserServiceInitialize(NULL);
    __atomic_store_n(&g_service_ready, 1, __ATOMIC_RELEASE);
  }
  ps5tm_sony_api_unlock();
#else
  __atomic_store_n(&g_service_ready, 1, __ATOMIC_RELEASE);
#endif
}


/* Whoever is currently using the console: the foreground user, and only
 * failing that the initial one.
 *
 * This used to ask GetInitialUser and nothing else, and on this console that
 * call does not answer. The consequence surfaced far away from here: the
 * controller probe fell back to user id 0xFF and scePadOpen() refused it with
 * 0x809B0001, so the battery reported "no controller" while one was in use.
 * Meanwhile ps5tm_user_get() — two functions further down, for the same
 * console, in the same file — got the real id every time, because it tried
 * the foreground user first. Two routes to one answer, one of them wrong.
 * Measured on 01.08.2026: foreground gives 0x18161531, initial gives nothing. */
int
ps5tm_user_service_active_user(int *out_uid) {
  if(!out_uid) return -1;
  *out_uid = -1;

#ifndef PS5TM_HOST_TEST
  ps5tm_user_service_init();

  ps5tm_sony_api_lock();
  if(sceUserServiceGetForegroundUser &&
     sceUserServiceGetForegroundUser(out_uid) == 0 && *out_uid > 0) {
    ps5tm_sony_api_unlock();
    return 0;
  }

  if(sceUserServiceGetInitialUser &&
     sceUserServiceGetInitialUser(out_uid) == 0 && *out_uid > 0) {
    ps5tm_sony_api_unlock();
    return 0;
  }
  ps5tm_sony_api_unlock();

  *out_uid = -1;
  return -1;
#else
  return -1;
#endif
}


int
ps5tm_user_read_name(uint32_t uid, char *out, size_t out_size) {
  if(!out || out_size == 0) return -1;
  out[0] = '\0';

#ifndef PS5TM_HOST_TEST
  ps5tm_user_service_init();
  if(!sceUserServiceGetUserName) return -1;

  /* Sony's limit is 16 characters, so 17 bytes with the terminator. Asking
     for more than the service expects is how this call fails on some
     firmwares, so the size is fixed here rather than passed through. */
  char tmp[PS5TM_USERNAME_MAX + 1];
  memset(tmp, 0, sizeof(tmp));
  ps5tm_sony_api_lock();
  int rc = sceUserServiceGetUserName((int32_t)uid, tmp, sizeof(tmp));
  ps5tm_sony_api_unlock();
  if(rc != 0) return -1;

  size_t n = strlen(tmp);
  if(n >= out_size) n = out_size - 1;
  memcpy(out, tmp, n);
  out[n] = '\0';
  return 0;
#else
  (void)uid;
  snprintf(out, out_size, "Testbenutzer");
  return 0;
#endif
}


void
ps5tm_user_get(ps5tm_user_t *out) {
  memset(out, 0, sizeof(*out));
  out->name_max = PS5TM_USERNAME_MAX;

#ifdef PS5TM_HOST_TEST
  out->uid = 0x18161531u;
  snprintf(out->username, sizeof(out->username), "Testbenutzer");
  out->valid = 1;
  return;
#else
  ps5tm_user_service_init();

  int uid = 0;
  /* Foreground first, initial as fallback — that order now lives in one place
     so it cannot drift apart from the one the controller probe uses. */
  if(ps5tm_user_service_active_user(&uid) != 0)
    return;                                  /* no user, nothing to show */
  out->uid = (uint32_t)uid;

  out->valid = 1;
  ps5tm_user_read_name(out->uid, out->username, sizeof(out->username));
  out->can_rename = (sceUserServiceSetUserName != NULL);
#endif
}


/* Sony accepts a narrower set than this, and rejects the rest with its own
 * error — but these three are worth catching here, because the message we can
 * give is far more useful than a hex code. */
static const char *
name_objection(const char *name) {
  if(!name || !*name) return "Der Name darf nicht leer sein.";

  size_t len = strlen(name);
  if(len > PS5TM_USERNAME_MAX)
    return "Der Name ist zu lang — die PS5 erlaubt höchstens 16 Zeichen.";

  for(size_t i = 0; i < len; i++) {
    unsigned char c = (unsigned char)name[i];
    if(c < 0x20 || c == 0x7F)
      return "Der Name enthält Steuerzeichen.";
  }
  return NULL;
}


int
ps5tm_user_set_name(uint32_t uid, const char *name, const char **why) {
  if(why) *why = NULL;

  const char *objection = name_objection(name);
  if(objection) {
    if(why) *why = objection;
    return -1;
  }

#ifdef PS5TM_HOST_TEST
  (void)uid;
  return 0;
#else
  ps5tm_user_service_init();

  if(!sceUserServiceSetUserName) {
    if(why) *why = "Diese Firmware bietet das Umbenennen nicht an.";
    return -1;
  }

  ps5tm_sony_api_lock();
  int rc = sceUserServiceSetUserName((int32_t)uid, name);
  ps5tm_sony_api_unlock();
  if(rc != 0) {
    /* The code is worth keeping: it is the only thing that distinguishes
       "not allowed" from "name rejected" when a rename fails. */
    static char msg[128];
    snprintf(msg, sizeof(msg),
             "Die Konsole hat den Namen abgelehnt (0x%08X). Ist der Jailbreak "
             "vollständig geladen?", (unsigned)rc);
    if(why) *why = msg;
    PS5TM_WARN("profile_rename_failed",
               "Umbenennen von Benutzer 0x%08X auf \"%s\" fehlgeschlagen "
               "(0x%08X).", uid, name, (unsigned)rc);
    return -1;
  }

  PS5TM_INFO("profile_renamed",
             "Benutzer 0x%08X heißt jetzt \"%s\".", uid, name);
  return 0;
#endif
}


/* ------------------------------------------------------------------ avatar */

/* The finished avatar files are produced in the browser and staged here; this
 * side only copies them into the live profile cache, which sits outside the
 * writable roots and needs the elevated ucred.
 *
 * ps5upload does the same split for the same reason — decoding and DXT5-
 * encoding an image has no business happening on the console. What it does
 * not do, and what is added here, is keep a copy of what was there before.
 * The cache is live: overwrite it with something the console dislikes and the
 * profile picture is gone with no way back. The backup is taken once, on the
 * first apply, so it always holds the *original* rather than the previous
 * attempt.
 */

#define AVATAR_STAGE_DIR  PS5TM_DATA_DIR "/avatar"
#define AVATAR_BACKUP_DIR PS5TM_DATA_DIR "/avatar-backup"
/* Where the console keeps each user's pictures, one directory per user.
   Overridable, like PS5TM_DATA_DIR, so the host test can run against a scratch
   directory instead of the console's system partition. */
#ifndef AVATAR_LIVE_ROOT
#define AVATAR_LIVE_ROOT  "/system_data/priv/cache/profile"
#endif
#define AVATAR_PATH_MAX   320

const char *
ps5tm_avatar_stage_dir(void) {
  return AVATAR_STAGE_DIR;
}

/* The files the browser's pipeline produces — four texture sizes, each written
 * twice, the squared source as PNG and online.json. api.c whitelists the same
 * names for the upload; keep the two lists the same. An apply needs all of
 * them: a set that is only partly there is a broken picture in the live cache. */
static const char *const k_avatar_set[] = {
  "avatar64.dds",  "avatar128.dds",  "avatar260.dds",  "avatar440.dds",
  "picture64.dds", "picture128.dds", "picture260.dds", "picture440.dds",
  "avatar.png", "picture.png", "online.json",
};
#define AVATAR_SET_COUNT (sizeof(k_avatar_set) / sizeof(k_avatar_set[0]))

/* Both operations write temporary names next to the live files, and two at the
   same time would share them. */
static pthread_mutex_t g_avatar_lock = PTHREAD_MUTEX_INITIALIZER;

static int
has_suffix(const char *s, const char *suffix) {
  size_t n = strlen(s), m = strlen(suffix);
  return n >= m && !strcmp(s + n - m, suffix);
}

/* What a staged file has to look like, judged by its first bytes: DXT5
 * textures and PNGs. Anything else — a page of HTML from a proxy, a cut-off
 * upload — must not reach the live cache. A DDS file is a 128-byte header (the
 * magic included) before any pixel. */
static int
header_ok(const char *name, const unsigned char *h, size_t n, off_t size) {
  if(has_suffix(name, ".dds"))
    return size >= 128 && n >= 4 && !memcmp(h, "DDS ", 4);
  if(has_suffix(name, ".png"))
    return n >= 8 && !memcmp(h, "\x89PNG\r\n\x1a\n", 8);
  return 1;
}

/* Phase one of a copy: src goes to dst's temporary name, ".part". The live
 * file is not touched — the old code opened it with O_TRUNC, so a copy that
 * died half-way (full disk, unplugged power) left a truncated picture in the
 * live profile cache. `check` names the staged file this is, and has it judged
 * by size and header; NULL copies whatever is there (backup and restore, where
 * the content is the console's own). 0 when the temporary file is complete. */
static int
stage_copy(const char *src, const char *dst, const char *check) {
  char tmp[AVATAR_PATH_MAX];
  if(snprintf(tmp, sizeof(tmp), "%s.part", dst) >= (int)sizeof(tmp)) return -1;

  int in = open(src, O_RDONLY | O_NOFOLLOW);
  if(in < 0) return -1;

  struct stat sst;
  if(fstat(in, &sst) != 0 || !S_ISREG(sst.st_mode) ||
     (check && sst.st_size <= 0)) {
    close(in);
    return -1;
  }

  /* What the console had under this name keeps its owner and mode, as it did
     when the file was overwritten in place. */
  struct stat old;
  int had = (lstat(dst, &old) == 0 && S_ISREG(old.st_mode));

  unlink(tmp);                        /* a leftover of an interrupted copy */
  int out = open(tmp, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0644);
  if(out < 0) { close(in); return -1; }
  if(had) {
    (void)fchmod(out, old.st_mode & 07777);
    (void)fchown(out, old.st_uid, old.st_gid);
  }

  char buf[16384];
  int  rc = 0, first = 1;
  for(;;) {
    ssize_t n = read(in, buf, sizeof(buf));
    if(n < 0) { if(errno == EINTR) continue; rc = -1; break; }
    if(n == 0) break;

    if(first) {
      first = 0;
      if(check && !header_ok(check, (const unsigned char *)buf, (size_t)n,
                             sst.st_size)) { rc = -1; break; }
    }

    ssize_t off = 0;
    while(off < n) {
      ssize_t w = write(out, buf + off, (size_t)(n - off));
      if(w < 0 && errno == EINTR) continue;
      if(w <= 0) { rc = -1; break; }
      off += w;
    }
    if(rc != 0) break;
  }
  if(check && first) rc = -1;         /* nothing was read: not a picture */

  (void)fsync(out);                   /* best effort; the rename follows */
  if(close(out) != 0) rc = -1;
  close(in);

  if(rc != 0) unlink(tmp);
  return rc;
}

/* Phase two: the temporary name becomes the real one, in one step. */
static int
commit_copy(const char *dst) {
  char tmp[AVATAR_PATH_MAX];
  if(snprintf(tmp, sizeof(tmp), "%s.part", dst) >= (int)sizeof(tmp)) return -1;
  if(rename(tmp, dst) != 0) { unlink(tmp); return -1; }
  return 0;
}

static void
discard_copy(const char *dst) {
  char tmp[AVATAR_PATH_MAX];
  if(snprintf(tmp, sizeof(tmp), "%s.part", dst) < (int)sizeof(tmp)) unlink(tmp);
}

/* Removes every regular file in a directory, leaving the directory itself.
   Returns how many went. */
static int
clear_dir_files(const char *dir) {
  DIR *d = opendir(dir);
  if(!d) return 0;

  int removed = 0;
  struct dirent *e;
  while((e = readdir(d)) != NULL) {
    if(e->d_name[0] == '.') continue;

    char path[AVATAR_PATH_MAX];
    snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);

    struct stat st;
    if(stat(path, &st) != 0 || !S_ISREG(st.st_mode)) continue;
    if(unlink(path) == 0) removed++;
  }
  closedir(d);
  return removed;
}

/* Copies every regular file from `from` into `to`, creating `to`. Returns the
 * number copied, or -1 when the source cannot be read at all; `failed`, when
 * given, receives how many could not be copied. Dot files are left alone and so
 * are ".part" names: those are temporary files of an upload or of a copy of
 * ours, never something to put into the live cache or to back up as if the
 * console had made it. */
static int
copy_dir_files(const char *from, const char *to, int *failed) {
  if(failed) *failed = 0;

  DIR *d = opendir(from);
  if(!d) return -1;

  mkdir(to, 0755);

  int copied = 0;
  struct dirent *e;
  while((e = readdir(d)) != NULL) {
    if(e->d_name[0] == '.' || has_suffix(e->d_name, ".part")) continue;

    char src[AVATAR_PATH_MAX], dst[AVATAR_PATH_MAX];
    int  a = snprintf(src, sizeof(src), "%s/%s", from, e->d_name);
    int  b = snprintf(dst, sizeof(dst), "%s/%s", to,   e->d_name);

    struct stat st;
    if(lstat(src, &st) != 0 || !S_ISREG(st.st_mode)) continue;

    if(a >= (int)sizeof(src) || b >= (int)sizeof(dst) ||
       stage_copy(src, dst, NULL) != 0 || commit_copy(dst) != 0) {
      if(failed) (*failed)++;
      continue;
    }
    copied++;
  }
  closedir(d);
  return copied;
}


/* The one-time backup of what the console has in the user's directory.
 * Returns 0 when a backup exists afterwards, whether taken now or before.
 *
 * "Exists" has to mean "complete": the copy is made under a temporary name and
 * renamed once every file is in it, so an interrupted or partly failed backup
 * is not mistaken for the original on the next apply. And it has to exist even
 * when the console had no directory at all: the original is then "nothing", and
 * that goes on record as an empty backup. Without it the second apply backed
 * up the first apply's own files and called them the original, and a restore
 * handed back our picture instead of the console's own state. */
static int
ensure_backup(const char *dest, const char *backup) {
  struct stat st;
  if(stat(backup, &st) == 0) return S_ISDIR(st.st_mode) ? 0 : -1;

  mkdir(AVATAR_BACKUP_DIR, 0755);

  if(stat(dest, &st) != 0) {
    if(errno != ENOENT) return -1;    /* cannot tell what is there: no backup */
    if(mkdir(backup, 0755) != 0 && errno != EEXIST) return -1;
    PS5TM_INFO("avatar_backup_empty",
               "Kein bisheriges Profilbild zum Sichern gefunden — die "
               "Konsole legt den Ordner sonst selbst an.");
    return 0;
  }

  char part[AVATAR_PATH_MAX];
  if(snprintf(part, sizeof(part), "%s.part", backup) >= (int)sizeof(part))
    return -1;
  clear_dir_files(part);              /* leftover of an interrupted backup */

  int failed = 0;
  int saved  = copy_dir_files(dest, part, &failed);
  if(saved < 0 || failed > 0 || rename(part, backup) != 0) {
    clear_dir_files(part);
    rmdir(part);
    return -1;
  }

  if(saved > 0)
    PS5TM_INFO("avatar_backup",
               "Bisheriges Profilbild gesichert (%d Dateien) unter %s.",
               saved, backup);
  else
    PS5TM_INFO("avatar_backup_empty",
               "Kein bisheriges Profilbild zum Sichern gefunden — die "
               "Konsole legt den Ordner sonst selbst an.");
  return 0;
}


/* Where the i-th file of the set is staged, and where it goes. Worked out
   again where it is needed rather than kept in two arrays of eleven paths:
   this runs on a request thread, next to a 16 KiB copy buffer. */
static void
avatar_paths(size_t i, const char *dest, char *src, char *dst) {
  snprintf(src, AVATAR_PATH_MAX, "%s/%s", AVATAR_STAGE_DIR, k_avatar_set[i]);
  snprintf(dst, AVATAR_PATH_MAX, "%s/%s", dest, k_avatar_set[i]);
}


static int
avatar_apply_locked(uint32_t uid, int *out_copied, const char **why) {
  char dest[192], backup[192];
  snprintf(dest, sizeof(dest), AVATAR_LIVE_ROOT "/0x%08X", uid);
  snprintf(backup, sizeof(backup), "%s/0x%08X", AVATAR_BACKUP_DIR, uid);

  /* Nothing staged means nothing to do — and saying so beats copying an empty
     directory over a working avatar. */
  DIR *stage = opendir(AVATAR_STAGE_DIR);
  if(!stage) {
    if(why) *why = "Es liegen keine vorbereiteten Bilddateien bereit.";
    return -1;
  }
  closedir(stage);

  /* All of them, or none. The browser says when it is done, but nothing here
     checked that it had finished: a set cut short — a tab closed mid-upload, a
     failed request — went into the live cache as it was. Which file is
     missing goes into the log; the answer to the browser stays one sentence,
     because it is returned by pointer and must not depend on this call. */
  char src[AVATAR_PATH_MAX], dst[AVATAR_PATH_MAX];
  for(size_t i = 0; i < AVATAR_SET_COUNT; i++) {
    avatar_paths(i, dest, src, dst);

    struct stat st;
    if(lstat(src, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size <= 0) {
      PS5TM_WARN("avatar_set_incomplete",
                 "Bilddatei %s fehlt oder ist leer — Übernehmen abgebrochen, "
                 "am Profil wurde nichts verändert.", k_avatar_set[i]);
      if(why) *why = "Die vorbereiteten Bilddateien sind unvollständig — bitte "
                     "das Bild erneut hochladen.";
      return -1;
    }
  }

  /* Before anything live is touched. */
  if(ensure_backup(dest, backup) != 0) {
    PS5TM_ERROR("avatar_backup_failed",
                "Das bisherige Profilbild ließ sich nicht sichern — "
                "Übernehmen abgebrochen, am Profil wurde nichts verändert.");
    if(why) *why = "Das bisherige Profilbild ließ sich nicht sichern — es "
                   "wurde nichts verändert.";
    return -1;
  }

  mkdir(AVATAR_LIVE_ROOT, 0755);
  mkdir(dest, 0755);

  /* Every file goes to its temporary name first, and is judged on the bytes
     actually copied (size, DDS or PNG header). The live directory still holds
     the old picture while this runs; one bad file ends it there. */
  for(size_t i = 0; i < AVATAR_SET_COUNT; i++) {
    avatar_paths(i, dest, src, dst);
    if(stage_copy(src, dst, k_avatar_set[i]) == 0) continue;

    for(size_t j = 0; j <= i; j++) {
      avatar_paths(j, dest, src, dst);
      discard_copy(dst);
    }
    PS5TM_ERROR("avatar_apply_failed",
                "Bilddatei %s ließ sich nicht nach %s kopieren oder ist keine "
                "gültige DDS-/PNG-Datei — nichts übernommen.",
                k_avatar_set[i], dest);
    if(why) *why = "Eine Bilddatei ließ sich nicht kopieren oder ist "
                   "beschädigt — am Profil wurde nichts verändert.";
    return -1;
  }

  /* Then the names are swapped in, one rename each. */
  int copied = 0;
  for(size_t i = 0; i < AVATAR_SET_COUNT; i++) {
    avatar_paths(i, dest, src, dst);
    if(commit_copy(dst) == 0) copied++;
  }

  if(out_copied) *out_copied = copied;
  if((size_t)copied != AVATAR_SET_COUNT) {
    for(size_t i = 0; i < AVATAR_SET_COUNT; i++) {
      avatar_paths(i, dest, src, dst);
      discard_copy(dst);
    }
    PS5TM_ERROR("avatar_apply_partial",
                "Nur %d von %zu Bilddateien nach %s übernommen — mit "
                "„Vorheriges zurückholen“ lässt sich der Zustand davor "
                "wiederherstellen.", copied, (size_t)AVATAR_SET_COUNT, dest);
    if(why) *why = "Das Profilbild wurde nur teilweise übernommen — bitte "
                   "„Vorheriges zurückholen“ wählen.";
    return -1;
  }

  PS5TM_INFO("avatar_applied",
             "Profilbild übernommen: %d Dateien nach %s kopiert.",
             copied, dest);
  return 0;
}


int
ps5tm_avatar_apply(uint32_t uid, int *out_copied, const char **why) {
  if(out_copied) *out_copied = 0;
  if(why) *why = NULL;

  pthread_mutex_lock(&g_avatar_lock);
  int rc = avatar_apply_locked(uid, out_copied, why);
  pthread_mutex_unlock(&g_avatar_lock);
  return rc;
}


/* Puts the console back exactly as it was.
 *
 * Copying the backup over the top is not enough, and the first console test
 * showed why: the backup held a single file while we had written eleven, so
 * a restore would have left one original and ten of ours side by side. The
 * directory is therefore emptied first — what the backup does not contain
 * did not exist before, and must not exist afterwards.
 *
 * An empty backup is a legitimate outcome, not a failure: it means the
 * console had nothing there to begin with, and an empty directory is then
 * precisely the state we are restoring. */
static int
avatar_restore_locked(uint32_t uid, int *out_copied, const char **why) {
  char dest[192], backup[192];
  snprintf(dest, sizeof(dest), AVATAR_LIVE_ROOT "/0x%08X", uid);
  snprintf(backup, sizeof(backup), "%s/0x%08X", AVATAR_BACKUP_DIR, uid);

  struct stat st;
  if(stat(backup, &st) != 0 || !S_ISDIR(st.st_mode)) {
    if(why) *why = "Es gibt keine Sicherung — es wurde auf dieser Konsole "
                   "noch nie ein Profilbild über diese App gesetzt.";
    return -1;
  }

  /* The console may have dropped the directory since; copying into one that is
     not there used to report "0 files restored" as if that were the original. */
  mkdir(AVATAR_LIVE_ROOT, 0755);
  mkdir(dest, 0755);

  int removed = clear_dir_files(dest);
  int failed  = 0;
  int copied  = copy_dir_files(backup, dest, &failed);
  if(copied < 0) copied = 0;

  if(out_copied) *out_copied = copied;
  if(failed > 0) {
    PS5TM_ERROR("avatar_restore_partial",
                "Profilbild nur teilweise zurückgesetzt: %d Datei(en) "
                "entfernt, %d aus der Sicherung zurückgespielt, %d ließen "
                "sich nicht kopieren.", removed, copied, failed);
    if(why) *why = "Das vorherige Profilbild ließ sich nur teilweise "
                   "zurückspielen — die Sicherung bleibt erhalten, bitte "
                   "noch einmal versuchen.";
    return -1;
  }

  PS5TM_INFO("avatar_restored",
             "Profilbild zurückgesetzt: %d Datei(en) entfernt, %d aus der "
             "Sicherung zurückgespielt.", removed, copied);
  return 0;
}


int
ps5tm_avatar_restore(uint32_t uid, int *out_copied, const char **why) {
  if(out_copied) *out_copied = 0;
  if(why) *why = NULL;

  pthread_mutex_lock(&g_avatar_lock);
  int rc = avatar_restore_locked(uid, out_copied, why);
  pthread_mutex_unlock(&g_avatar_lock);
  return rc;
}
