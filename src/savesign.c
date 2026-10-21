/* savesign.c — re-sign a saved game for a different user + view save contents.
 *
 * Re-signing overview
 * ───────────────────
 * The console ties a save to a user account via two mechanisms:
 *   - The encrypted save image (sdimg_<slot>) carries the user's identity
 *     in the sce_sys/param.sfo file that lives inside the mounted filesystem.
 *   - The console's database (savedata[_prospero].db) records who the save
 *     belongs to.
 * Both must be updated for the target user to be able to open the save.
 *
 * PS5 save:
 *   Mount the image (savemount.c), patch ACCOUNT_ID + USER_ID in
 *   sce_sys/param.sfo, unmount; copy the signed image into the target user's
 *   save folder; update the DB row.
 *
 * PS4 save:
 *   The PARAMS field in param.sfo carries a per-user HMAC generated from
 *   the console's OpenPSID.  We cannot recompute it (the key is internal),
 *   but we can COPY it from a save the target user already owns for the same
 *   game.  Without such a template the resign is refused with a clear message.
 *   Rank 1: same CUSA + same slot.  Rank 2: same CUSA + any other slot.
 *   (Rank 3 — different title — is unverified; we refuse with a message.)
 *
 * Account ID of the target user:
 *   Read as 8 raw bytes from ACCOUNT_ID in any param.sfo in the target
 *   user's save meta tree.  The field is binary (little-endian uint64).
 *
 * User ID (32-bit) equals (uint32_t)strtoul(uid, NULL, 16).
 *
 * The job shares the savebackup job slot (ps5tm_saves_busy, etc.) so that
 * the page only ever runs one save operation at a time.
 */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "ps5tm.h"
#include "savebackup.h"
#include "savedb.h"
#include "savemount.h"
#include "savesfo.h"
#include "savesign.h"
#include "third_party/cJSON.h"

#ifndef PS5TM_SAVE_HOME
#define PS5TM_SAVE_HOME "/user/home"
#endif
#ifndef PS5TM_SAVE_SYS
#define PS5TM_SAVE_SYS  "/system_data/savedata"
#endif

/* ── Constants ─────────────────────────────────────────────────────────── */

#define SIGN_BUF    (256u << 10)        /* copy buffer size                */
#define SFO_INSIDE  "sce_sys/param.sfo" /* path inside the mounted image   */

/* ── General helpers ────────────────────────────────────────────────────── */

int ps5tm_savesign_uid_valid(const char *uid)
{
    if (!uid || strlen(uid) != 8)
        return 0;
    for (int i = 0; i < 8; i++)
        if (!((uid[i] >= '0' && uid[i] <= '9') ||
              (uid[i] >= 'a' && uid[i] <= 'f') ||
              (uid[i] >= 'A' && uid[i] <= 'F')))
            return 0;
    return 1;
}

static int copy_file(const char *src, const char *dst)
{
    int fdin = open(src, O_RDONLY);
    if (fdin < 0) return -1;
    int fdout = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fdout < 0) { close(fdin); return -1; }

    uint8_t *buf = malloc(SIGN_BUF);
    if (!buf) { close(fdin); close(fdout); return -1; }

    ssize_t n;
    int ret = 0;
    while ((n = read(fdin, buf, SIGN_BUF)) > 0)
        if (write(fdout, buf, (size_t)n) != n) { ret = -1; break; }
    if (n < 0) ret = -1;

    free(buf);
    close(fdin);
    close(fdout);
    return ret;
}

/* Recursively copy a directory tree (up to 3 levels); dst is created if
 * absent.  Best-effort: errors are logged but not fatal. */
static void copy_dir(const char *src, const char *dst, int depth)
{
    if (depth > 3) return;
    if (mkdir(dst, 0755) != 0 && errno != EEXIST) return;

    DIR *d = opendir(src);
    if (!d) return;

    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (de->d_name[0] == '.') continue;
        char sp[PATH_MAX], dp[PATH_MAX];
        snprintf(sp, sizeof(sp), "%s/%s", src, de->d_name);
        snprintf(dp, sizeof(dp), "%s/%s", dst, de->d_name);
        struct stat st;
        if (stat(sp, &st) != 0) continue;
        if (S_ISDIR(st.st_mode))
            copy_dir(sp, dp, depth + 1);
        else if (S_ISREG(st.st_mode))
            copy_file(sp, dp);   /* best-effort */
    }
    closedir(d);
}

/* ── Account-ID lookup ──────────────────────────────────────────────────── */

/* Read the 8-byte ACCOUNT_ID from any param.sfo found in the user's meta
 * trees.  Returns 0 on success, -1 when no usable SFO found. */
static int uid_get_account_id(const char *uid, uint64_t *acid_out)
{
    static const char *const meta_trees[] = {
        "savedata_prospero_meta/user",
        "savedata_meta/user",
        NULL
    };

    for (int t = 0; meta_trees[t]; t++) {
        char tree[PATH_MAX];
        snprintf(tree, sizeof(tree), "%s/%s/%s",
                 PS5TM_SAVE_HOME, uid, meta_trees[t]);

        DIR *d1 = opendir(tree);
        if (!d1) continue;

        struct dirent *de1;
        int found = 0;
        while (!found && (de1 = readdir(d1)) != NULL) {
            if (de1->d_name[0] == '.') continue;
            char tdir[PATH_MAX];
            snprintf(tdir, sizeof(tdir), "%s/%s", tree, de1->d_name);
            DIR *d2 = opendir(tdir);
            if (!d2) continue;

            struct dirent *de2;
            while (!found && (de2 = readdir(d2)) != NULL) {
                if (de2->d_name[0] == '.') continue;
                size_t nl = strlen(de2->d_name);
                if (nl < 5 || strcmp(de2->d_name + nl - 4, ".sfo") != 0)
                    continue;

                char sfo_path[PATH_MAX];
                snprintf(sfo_path, sizeof(sfo_path), "%s/%s",
                         tdir, de2->d_name);

                int fd = open(sfo_path, O_RDONLY);
                if (fd < 0) continue;

                savesfo_t sfo;
                if (savesfo_read(fd, &sfo) == 0) {
                    uint8_t raw[8];
                    int r = savesfo_get_raw(&sfo, "ACCOUNT_ID",
                                           raw, sizeof(raw));
                    if (r == 8) {
                        uint64_t v;
                        memcpy(&v, raw, 8);
                        if (v != 0) {
                            *acid_out = v;
                            found = 1;
                        }
                    }
                    savesfo_free(&sfo);
                }
                close(fd);
            }
            closedir(d2);
        }
        closedir(d1);
        if (found) return 0;
    }
    return -1;
}

/* ── PS4 template search ────────────────────────────────────────────────── */

/* Find a template param.sfo from the TARGET user's own PS4 saves for the
 * same title.  Rank 1: same slot.  Rank 2: any other slot of the same title.
 * Fills *tmpl_sfo (caller frees) and optionally *tmpl_path.
 * Returns 0 on success, -1 when no template exists. */
static int find_ps4_template(const char *dst_uid, const char *cusa,
                              const char *slot,
                              savesfo_t *tmpl_sfo, char *tmpl_path)
{
    char cusa_dir[PATH_MAX];
    snprintf(cusa_dir, sizeof(cusa_dir),
             "%s/%s/savedata_meta/user/%s",
             PS5TM_SAVE_HOME, dst_uid, cusa);

    DIR *d = opendir(cusa_dir);
    if (!d) return -1;

    int   best_rank = 99;
    char  best_path[PATH_MAX] = "";

    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (de->d_name[0] == '.') continue;
        size_t nl = strlen(de->d_name);
        if (nl < 5 || strcmp(de->d_name + nl - 4, ".sfo") != 0) continue;

        /* Slot name = filename minus ".sfo" */
        char this_slot[64] = {0};
        size_t sl = nl - 4;
        if (sl >= sizeof(this_slot)) continue;
        memcpy(this_slot, de->d_name, sl);

        int rank = (strcmp(this_slot, slot) == 0) ? 1 : 2;
        if (rank < best_rank) {
            best_rank = rank;
            snprintf(best_path, sizeof(best_path),
                     "%s/%s", cusa_dir, de->d_name);
        }
    }
    closedir(d);

    if (!best_path[0]) return -1;

    int fd = open(best_path, O_RDONLY);
    if (fd < 0) return -1;
    int rc = savesfo_read(fd, tmpl_sfo);
    close(fd);
    if (rc != 0) return -1;

    if (tmpl_path)
        snprintf(tmpl_path, PATH_MAX, "%s", best_path);
    return 0;
}

/* ── View-contents file walker ──────────────────────────────────────────── */

static void walk_dir(cJSON *arr, const char *base, int base_len, int depth)
{
    if (depth > 6) return;
    DIR *d = opendir(base);
    if (!d) return;

    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (de->d_name[0] == '.') continue;
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s/%s", base, de->d_name);
        struct stat st;
        if (stat(path, &st) != 0) continue;

        const char *rel = path + base_len;
        if (*rel == '/') rel++;

        if (S_ISDIR(st.st_mode)) {
            walk_dir(arr, path, base_len, depth + 1);
        } else {
            cJSON *obj = cJSON_CreateObject();
            cJSON_AddStringToObject(obj, "name", rel);
            cJSON_AddNumberToObject(obj, "size", (double)st.st_size);
            cJSON_AddItemToArray(arr, obj);
        }
    }
    closedir(d);
}

/* ── View contents (public) ─────────────────────────────────────────────── */

int ps5tm_savesign_view(const char *uid, const char *title_id,
                        const char *slot, cJSON **out_json)
{
    *out_json = NULL;
    if (!ps5tm_savesign_uid_valid(uid) || !title_id || !slot) {
        errno = EINVAL; return -1;
    }

    int is_ps5 = (strncmp(title_id, "PPSA", 4) == 0);
    const char *data_dir = is_ps5 ? "savedata_prospero" : "savedata";

    char img_path[PATH_MAX], key_src[PATH_MAX];
    snprintf(img_path, sizeof(img_path),
             "%s/%s/%s/%s/sdimg_%s",
             PS5TM_SAVE_HOME, uid, data_dir, title_id, slot);
    if (is_ps5) {
        snprintf(key_src, sizeof(key_src), "%s", img_path);
    } else {
        snprintf(key_src, sizeof(key_src),
                 "%s/%s/%s/%s/%s.bin",
                 PS5TM_SAVE_HOME, uid, data_dir, title_id, slot);
    }

    char mp[SAVEMOUNT_MP_MAX];
    if (savemount_open(img_path, key_src,
                       is_ps5 ? SAVEMOUNT_PS5 : SAVEMOUNT_PS4, mp) != 0)
        return -1;

    cJSON *arr = cJSON_CreateArray();
    walk_dir(arr, mp, (int)strlen(mp), 0);
    savemount_close(mp);

    *out_json = arr;
    return 0;
}

/* ══════════════════════════════════════════════════════════════════════════
 * Resign job thread
 * ══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    char src_uid[12];
    char dst_uid[12];
    char title_id[16];
    char slot[64];
} resign_args_t;

static void *resign_thread(void *arg)
{
    resign_args_t *a = (resign_args_t *)arg;

    printf("[savesign] resign %s slot %s: %s → %s\n",
           a->title_id, a->slot, a->src_uid, a->dst_uid);

    int is_ps5 = (strncmp(a->title_id, "PPSA", 4) == 0);
    const char *data_dir  = is_ps5 ? "savedata_prospero"            : "savedata";
    const char *meta_dir  = is_ps5 ? "savedata_prospero_meta/user"  : "savedata_meta/user";
    const char *db_sub    = is_ps5 ? "savedata_prospero"            : "savedata";

    /* ── Target user identity ──────────────────────────────────────────── */
    uint64_t acid = 0;
    if (is_ps5) {
        if (uid_get_account_id(a->dst_uid, &acid) != 0)
            printf("[savesign] warning: no account_id found for %s\n",
                   a->dst_uid);
    }
    uint32_t dst_uid32 = (uint32_t)strtoul(a->dst_uid, NULL, 16);

    /* ── Source image and key paths ───────────────────────────────────── */
    char src_img[PATH_MAX], src_bin[PATH_MAX];
    snprintf(src_img, sizeof(src_img),
             "%s/%s/%s/%s/sdimg_%s",
             PS5TM_SAVE_HOME, a->src_uid, data_dir, a->title_id, a->slot);
    snprintf(src_bin, sizeof(src_bin),
             "%s/%s/%s/%s/%s.bin",
             PS5TM_SAVE_HOME, a->src_uid, data_dir, a->title_id, a->slot);

    /* ── PS4: find template from target user ──────────────────────────── */
    savesfo_t tmpl_sfo;
    memset(&tmpl_sfo, 0, sizeof(tmpl_sfo));
    if (!is_ps5) {
        char tmpl_path[PATH_MAX];
        if (find_ps4_template(a->dst_uid, a->title_id, a->slot,
                              &tmpl_sfo, tmpl_path) != 0) {
            printf("[savesign] no PS4 template for %s on user %s\n",
                   a->title_id, a->dst_uid);
            ps5tm_saves_job_fail(
                "Kein eigener Spielstand des Zielbenutzers für dieses Spiel "
                "gefunden. Für PS4-Spielstände ist ein vorhandener eigener "
                "Spielstand desselben Spiels als Vorlage notwendig.");
            free(a);
            return NULL;
        }
    }

    /* ── Mount source image ───────────────────────────────────────────── */
    char mp[SAVEMOUNT_MP_MAX];
    const char *key_src = is_ps5 ? src_img : src_bin;
    int save_type = is_ps5 ? SAVEMOUNT_PS5 : SAVEMOUNT_PS4;

    if (savemount_open(src_img, key_src, save_type, mp) != 0) {
        printf("[savesign] mount: %s\n", strerror(errno));
        ps5tm_saves_job_fail("Spielstand konnte nicht gemountet werden.");
        if (tmpl_sfo.buf) savesfo_free(&tmpl_sfo);
        free(a);
        return NULL;
    }

    /* ── Read + patch param.sfo ───────────────────────────────────────── */
    char sfo_path[PATH_MAX];
    snprintf(sfo_path, sizeof(sfo_path), "%s/%s", mp, SFO_INSIDE);

    int sfo_fd = open(sfo_path, O_RDWR);
    savesfo_t sfo;
    memset(&sfo, 0, sizeof(sfo));
    int sfo_ok = (sfo_fd >= 0 && savesfo_read(sfo_fd, &sfo) == 0);

    if (!sfo_ok) {
        printf("[savesign] cannot read %s: %s\n", sfo_path, strerror(errno));
        if (sfo_fd >= 0) close(sfo_fd);
        savemount_close(mp);
        ps5tm_saves_job_fail("param.sfo im Spielstand nicht lesbar.");
        if (tmpl_sfo.buf) savesfo_free(&tmpl_sfo);
        free(a);
        return NULL;
    }

    int patch_rc;
    if (is_ps5) {
        patch_rc = savesfo_patch_ps5(&sfo, acid, dst_uid32);
    } else {
        patch_rc = savesfo_patch_ps4(&sfo, &tmpl_sfo);
        savesfo_free(&tmpl_sfo);
        tmpl_sfo.buf = NULL;
    }

    if (patch_rc != 0) {
        savesfo_free(&sfo);
        close(sfo_fd);
        savemount_close(mp);
        ps5tm_saves_job_fail(
            "Signieren schlug fehl (SFO-Felder nicht gefunden).");
        free(a);
        return NULL;
    }

    int write_rc = savesfo_write(sfo_fd, &sfo);
    savesfo_free(&sfo);
    close(sfo_fd);

    savemount_close(mp);   /* also removes the scratch image */

    if (write_rc != 0) {
        ps5tm_saves_job_fail(
            "param.sfo konnte nicht zurückgeschrieben werden.");
        free(a);
        return NULL;
    }

    /* ── Copy signed image to target user ────────────────────────────── */
    char dst_data[PATH_MAX], dst_meta_dir_path[PATH_MAX];
    snprintf(dst_data, sizeof(dst_data),
             "%s/%s/%s/%s",
             PS5TM_SAVE_HOME, a->dst_uid, data_dir, a->title_id);
    snprintf(dst_meta_dir_path, sizeof(dst_meta_dir_path),
             "%s/%s/%s/%s",
             PS5TM_SAVE_HOME, a->dst_uid, meta_dir, a->title_id);
    mkdir(dst_data,          0755);
    mkdir(dst_meta_dir_path, 0755);

    char dst_img[PATH_MAX];
    snprintf(dst_img, sizeof(dst_img), "%s/sdimg_%s", dst_data, a->slot);

    /* savemount left the patched image in its scratch area */
    char patched[PATH_MAX];
    snprintf(patched, sizeof(patched),
             "/data/PS5-Cooling-Center/savemount/sdimg_work");

    if (copy_file(patched, dst_img) != 0) {
        printf("[savesign] copy patched image: %s\n", strerror(errno));
        ps5tm_saves_job_fail(
            "Signierter Spielstand konnte nicht kopiert werden.");
        free(a);
        return NULL;
    }

    if (!is_ps5) {
        /* PS4: companion .bin (key file) — copy unchanged */
        char dst_bin[PATH_MAX];
        snprintf(dst_bin, sizeof(dst_bin), "%s/%s.bin", dst_data, a->slot);
        copy_file(src_bin, dst_bin);
    }

    /* Copy meta folder (icons, SFO copies) from source to destination */
    char src_meta[PATH_MAX];
    snprintf(src_meta, sizeof(src_meta),
             "%s/%s/%s/%s",
             PS5TM_SAVE_HOME, a->src_uid, meta_dir, a->title_id);
    copy_dir(src_meta, dst_meta_dir_path, 0);

    /* Patch the meta SFO (unencrypted, sits in savedata_*_meta/) */
    if (is_ps5) {
        char meta_sfo[PATH_MAX];
        snprintf(meta_sfo, sizeof(meta_sfo),
                 "%s/%s.sfo", dst_meta_dir_path, a->slot);
        int mfd = open(meta_sfo, O_RDWR);
        if (mfd >= 0) {
            savesfo_t ms;
            if (savesfo_read(mfd, &ms) == 0) {
                savesfo_patch_ps5(&ms, acid, dst_uid32);
                savesfo_write(mfd, &ms);
                savesfo_free(&ms);
            }
            close(mfd);
        }
    }

    /* ── Update savedata.db ──────────────────────────────────────────── */
    char db_path[PATH_MAX];
    snprintf(db_path, sizeof(db_path),
             "%s/%s/%s/db/user/savedata.db",
             PS5TM_SAVE_SYS, db_sub, a->dst_uid);

    /* Blocks = image size / 0x40000 */
    struct stat img_st;
    uint64_t blocks = 0;
    if (stat(dst_img, &img_st) == 0)
        blocks = (uint64_t)img_st.st_size / 0x40000ULL;

    /* Title string: try MAINTITLE or TITLE from the meta SFO */
    char main_title[128] = "";
    {
        char ms_path[PATH_MAX];
        snprintf(ms_path, sizeof(ms_path),
                 "%s/%s.sfo", dst_meta_dir_path, a->slot);
        int mfd = open(ms_path, O_RDONLY);
        if (mfd >= 0) {
            savesfo_t ms;
            if (savesfo_read(mfd, &ms) == 0) {
                if (savesfo_get_str(&ms, "MAINTITLE",
                                   main_title, sizeof(main_title)) < 0)
                    savesfo_get_str(&ms, "TITLE",
                                   main_title, sizeof(main_title));
                savesfo_free(&ms);
            }
            close(mfd);
        }
    }
    if (!main_title[0])
        snprintf(main_title, sizeof(main_title), "%s", a->title_id);

    savedb_row_t row;
    memset(&row, 0, sizeof(row));
    row.title_id   = a->title_id;
    row.dir_name   = a->slot;
    row.main_title = main_title;
    row.account_id = acid;
    row.user_id    = dst_uid32;
    row.blocks     = blocks;
    {
        time_t now = time(NULL);
        struct tm *tm = gmtime(&now);
        strftime(row.mtime, sizeof(row.mtime),
                 "%Y-%m-%dT%H:%M:%S.00Z", tm);
    }

    if (savedb_upsert(db_path, &row) != 0)
        printf("[savesign] savedb_upsert: %s (non-fatal)\n", strerror(errno));

    ps5tm_saves_job_done(
        "Spielstand wurde erfolgreich für den anderen Benutzer signiert.");
    free(a);
    return NULL;
}

/* ── Public entry point ─────────────────────────────────────────────────── */

int ps5tm_savesign_start(const char *src_uid, const char *dst_uid,
                         const char *title_id, const char *slot,
                         char *err, size_t err_len)
{
    if (!ps5tm_savesign_uid_valid(src_uid) ||
        !ps5tm_savesign_uid_valid(dst_uid)) {
        snprintf(err, err_len, "Ungültige Benutzer-ID.");
        return 400;
    }
    if (!title_id || strlen(title_id) < 4 || !slot || !slot[0]) {
        snprintf(err, err_len, "Titel oder Slot fehlt.");
        return 400;
    }
    if (strcasecmp(src_uid, dst_uid) == 0) {
        snprintf(err, err_len,
                 "Quell- und Zielbenutzer sind gleich.");
        return 400;
    }

    if (ps5tm_saves_busy()) {
        snprintf(err, err_len,
                 "Es läuft gerade ein anderer Spielstand-Vorgang.");
        return 409;
    }

    resign_args_t *a = calloc(1, sizeof(*a));
    if (!a) {
        snprintf(err, err_len, "Kein Arbeitsspeicher.");
        return 503;
    }
    snprintf(a->src_uid,  sizeof(a->src_uid),  "%s", src_uid);
    snprintf(a->dst_uid,  sizeof(a->dst_uid),  "%s", dst_uid);
    snprintf(a->title_id, sizeof(a->title_id), "%s", title_id);
    snprintf(a->slot,     sizeof(a->slot),     "%s", slot);

    return ps5tm_saves_run_simple("resign", resign_thread, a, err, err_len);
}
