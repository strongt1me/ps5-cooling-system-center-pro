/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 gasb07 (PS4Mount 0.6.1), adapted for PS5 Cooling & System Center - Pro
 *
 * PS4Mount inside the app: scans USB drives for PS4 fake PKGs, registers them without installing them (nullfs mounts of internal
 * folders that hold symlinks to the packages), follows the kernel log and offers its list and actions to the web page.
 *
 * What was taken over as it is (https://github.com/gasb07/ps4mount, GPL-3.0-or-later; see third_party/ps4mount/): the scan, the
 * registration, the mounts, the delete window and the settings. What was replaced: the daemon around them. PS4Mount ran as a
 * payload of its own with a single-instance lock, its own web server on port 8770, a klog socket and a main loop. Here the same loop
 * runs as a thread of the app (ps4m_worker), the klog lines come from ps5tm_klog_fetch (a copy of the kernel buffer: nobody is
 * deprived of lines), the web part is the app's own routes (api.c), and the single-instance lock on port 9032 is kept so that a
 * stand-alone PS4Mount and this thread never run together. State stays in /data/ps4mount, as before, so games added by the
 * stand-alone payload keep working.
 *
 * The original header of main.c follows.
 */
/* ps4mount: run PS4 fpkgs from external storage without installing them.
 *
 * Every scan:
 *   1. A PS4 app pkg found at the root of /mnt/usbN or /mnt/extN is moved to
 *      <root>/ps4/<TITLE_ID>/app.pkg (same disk, rename only).
 *   2. For each <root>/ps4/<TITLE_ID>/app.pkg the files a real install leaves
 *      behind are generated from the pkg into an internal folder next to a symlink
 *      to the pkg, that folder is mounted over /user/app/<TITLE_ID> (nullfs) and
 *      the title is added to app.db/appinfo.db.
 *   3. Mounts whose storage disappeared are released.
 * Titles installed the normal way are never touched. */

#ifdef PS5TM_HOST_TEST
/* Host tests (Linux): the module needs FreeBSD's mount calls and a console, so only its calls to the page exist here. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ps4m.h"
void ps4m_boot(void) {}
int ps4m_set_enabled(int on, char *msg, size_t n) { snprintf(msg, n, on ? "PS4Mount ist eingeschaltet" : "PS4Mount ist ausgeschaltet"); return 0; }
char *ps4m_state_json(void) { return strdup("{}"); }
int ps4m_do_action(const char *action, const char *tid, char *msg, size_t n) { (void)action; (void)tid; snprintf(msg, n, "Host-Test"); return -1; }
int ps4m_do_config(const char *form, char *msg, size_t n) { (void)form; snprintf(msg, n, "Host-Test"); return -1; }
int ps4m_icon_png(const char *tid, unsigned char **data, size_t *len) { (void)tid; (void)data; (void)len; return -1; }
char *ps4m_log_tail(size_t max) { (void)max; return strdup(""); }
/* PS5TM_TEST_P4M=running or =standalone lets the host tests try the payload guard (payloads.c) */
int ps4m_running(void) { const char *e = getenv("PS5TM_TEST_P4M"); return e && !strcmp(e, "running"); }
int ps4m_standalone_active(char *why, size_t n) {
    const char *e = getenv("PS5TM_TEST_P4M");
    if (!e || strcmp(e, "standalone")) return 0;
    snprintf(why, n, "Sein Web-Panel antwortet auf Port 8770.");
    return 1;
}
#else
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/mount.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/uio.h>
#include <sys/user.h>
#include <time.h>
#include <unistd.h>

#include "ps4m.h"
#include "ps4m_build.h"
#include "ps4m_crypto.h"
#include "ps4m_fw.h"
#include "ps4m_origin.h"
#include "ps4m_pkg.h"
#include "ps5tm.h"
#include "sqlite_ro.h"
#include <ps5/kernel.h>

#define VERSION "PS4Mount 0.6.1"
#define LOCK_PORT 9032
#define SCAN_SECONDS 10
#define STATE_DIR "/data/ps4mount"
/* Internal folders mounted over /user/app/<TID> and /user/patch/<TID>: the files an
 * install leaves there plus app.pkg/patch.pkg as symlinks to the pkgs on the drive.
 * /user is bfs, which has no symlinks; /system_data is ufs. A delete from the
 * console only ever removes these links, never the pkgs. */
#define HOLD_DIR "/system_data/ps4mount"
#define MAX_TITLES 256
#define MAX_DLC 64
#define BATCH_QUIET 10 /* seconds without writes to the system databases between groups */
#define MAX_DLC_TIDS 8

#define MAX_SCAN_PATHS 8
#define CONFIG_PATH STATE_DIR "/config.txt"
#define RESTORED_LIST STATE_DIR "/restored.txt" /* pkgs moved back: not picked up again */

/* Settings, kept in CONFIG_PATH and edited in the panel. */
typedef struct {
    int batch;      /* new games are added in groups */
    int batch_size; /* games per group */
    int dlc;        /* add-ons are moved and installed */
    char paths[MAX_SCAN_PATHS][128]; /* folders searched for pkgs, relative to each drive; "" is the root */
    int npaths;
    int restore_batch; /* games removed from the library per group when moving pkgs back */
    int fast_reg;   /* (app) PS4Mount 0.6.0: /user/app shows only the new games while the system registers them; off by default */
} config_t;

#define MAX_RESTORE_BATCH 10

static config_t g_cfg = {1, 5, 1, {""}, 1, 5, 0};
static int g_enabled;     /* switched on in the page; kept in config.txt (key enabled=) */


/* Weak: a firmware without them must not keep the whole app (fan control included) from loading; PS4Mount then says so. */
int sceAppInstUtilInitialize(void) __attribute__((weak));
int sceAppInstUtilAppInstallAll(void *) __attribute__((weak));
int sceAppInstUtilAppUnInstall(const char *) __attribute__((weak));

static void mkdirs(const char *path, mode_t mode);
static int p4m_db_is_registered(const char *tid);

/* Log of the module: /data/ps4mount/log.txt as with the stand-alone payload (the page shows its end). As in PS4Mount 0.5.1 every
   line starts with date and time to the millisecond ("2026-10-15 18:08:35.284 "); a line written in several calls gets one stamp,
   a call that holds several lines gets one per line. */
static pthread_mutex_t g_log_lock = PTHREAD_MUTEX_INITIALIZER;
static FILE *g_logf;
static int g_log_at_start = 1;

static void p4m_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void p4m_printf(const char *fmt, ...) {
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n <= 0)
        return;
    size_t len = (size_t)n < sizeof(buf) ? (size_t)n : sizeof(buf) - 1;
    if ((size_t)n >= sizeof(buf))
        buf[len - 1] = '\n'; /* cut off: still a whole line, so that the next one gets its stamp */
    pthread_mutex_lock(&g_log_lock);
    if (!g_logf) {
        mkdirs(STATE_DIR, 0777);
        struct stat st;
        if (!stat(STATE_DIR "/log.txt", &st) && st.st_size > (512 << 10))
            rename(STATE_DIR "/log.txt", STATE_DIR "/log.old");
        g_logf = fopen(STATE_DIR "/log.txt", "a");
        if (g_logf)
            setvbuf(g_logf, NULL, _IOLBF, 0);
    }
    if (g_logf && ftell(g_logf) > (1 << 20)) {   /* (app) kept small while the app runs for days, not only at the start */
        fclose(g_logf);
        rename(STATE_DIR "/log.txt", STATE_DIR "/log.old");
        g_logf = fopen(STATE_DIR "/log.txt", "a");
        if (g_logf)
            setvbuf(g_logf, NULL, _IOLBF, 0);
        g_log_at_start = 1;
    }
    if (g_logf) {
        struct timespec now;
        struct tm tm;
        char day[32], stamp[48];
        clock_gettime(CLOCK_REALTIME, &now);
        localtime_r(&now.tv_sec, &tm);
        strftime(day, sizeof(day), "%Y-%m-%d %H:%M:%S", &tm);
        snprintf(stamp, sizeof(stamp), "%s.%03ld ", day, (long)(now.tv_nsec / 1000000));
        const char *p = buf, *end = buf + len;
        while (p < end) {
            if (g_log_at_start)
                fputs(stamp, g_logf);
            const char *nl = memchr(p, '\n', (size_t)(end - p));
            size_t k = nl ? (size_t)(nl - p) + 1 : (size_t)(end - p);
            fwrite(p, 1, k, g_logf);
            g_log_at_start = nl != NULL;
            p += k;
        }
    }
    pthread_mutex_unlock(&g_log_lock);
}
#define printf p4m_printf

static void notify(const char *fmt, ...) __attribute__((format(__printf__, 1, 2)));
static void notify(const char *fmt, ...) {
    char msg[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    printf("[notify] %s\n", msg);
    ps5tm_notify_plain("%s", msg);
}
/* --- small fs helpers --- */

static int exists(const char *p) {
    struct stat st;
    return stat(p, &st) == 0;
}

static void mkdirs(const char *path, mode_t mode) {
    char tmp[PATH_MAX];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++)
        if (*p == '/') {
            *p = 0;
            mkdir(tmp, mode);
            *p = '/';
        }
    mkdir(tmp, mode);
}

static int write_file(const char *path, const void *data, size_t len, mode_t mode) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, mode);
    if (fd < 0)
        return -1;
    const uint8_t *p = data;
    while (len) {
        ssize_t n = write(fd, p, len);
        if (n <= 0) {
            close(fd);
            return -1;
        }
        p += n;
        len -= (size_t)n;
    }
    close(fd);
    return 0;
}

static int mounted_from(const char *mountpoint, char *from, size_t n) {
    struct statfs sf;
    if (statfs(mountpoint, &sf) || strcmp(sf.f_mntonname, mountpoint) || strcmp(sf.f_fstypename, "nullfs"))
        return 0;
    if (from)
        snprintf(from, n, "%s", sf.f_mntfromname);
    return 1;
}

/* Mounts on path from the mount table. Unlike statfs(path) this also sees a layer
 * whose source drive was disconnected (statfs on it fails). */
static int layers_on(const char *path) {
    int n = getfsstat(NULL, 0, MNT_NOWAIT), count = 0;
    if (n <= 0)
        return 0;
    struct statfs *list = malloc(sizeof(*list) * (size_t)(n + 16));
    if (!list)
        return 0;
    n = getfsstat(list, (long)(sizeof(*list) * (size_t)(n + 16)), MNT_NOWAIT);
    for (int i = 0; i < n; i++)
        if (!strcmp(list[i].f_mntonname, path))
            count++;
    free(list);
    return count;
}

/* A layer is on path but cannot be read: its drive was disconnected (and maybe
 * connected again, which gives it a new mount). */
static int dead_layer(const char *path) {
    struct statfs sf;
    return statfs(path, &sf) && layers_on(path) > 0;
}

static int is_mountpoint(const char *path) {
    struct statfs sf;
    return statfs(path, &sf) == 0 && !strcmp(sf.f_mntonname, path);
}

/* --- appmeta entries copied verbatim from the pkg --- */

typedef struct {
    uint32_t id;
    const char *name;
} meta_entry_t;

static const meta_entry_t k_user_meta[] = {
    {0x1200, "icon0.png"}, {0x1220, "pic0.png"},  {0x1006, "pic1.png"},          {0x1240, "snd0.at9"},
    {0x1280, "icon0.dds"}, {0x12a0, "pic0.dds"},  {0x1004, "pronunciation.xml"}, {0x100b, "shareparam.json"},
    {0, NULL},
};

static int write_entry(pkg_t *pkg, uint32_t id, const char *path) {
    size_t len;
    uint8_t *data = p4m_pkg_entry(pkg, id, &len);
    if (!data)
        return 1; /* absent entry is fine */
    int rc = write_file(path, data, len, 0777);
    free(data);
    return rc;
}

static int write_buf(const char *path, const buf_t *b) {
    if (b->failed) { /* (app) out of memory while building it: a half-built file would break the game (PS4Mount PR #18) */
        printf("[write] %s: out of memory while building it, not written\n", path);
        return -1;
    }
    return write_file(path, b->data, b->len, 0777);
}

/* --- registration --- */

/* An add-on: <root>/ps4/<TID>/addcont/<ADD-ON TID>/<LABEL>/ac.pkg. */
typedef struct {
    char path[PATH_MAX];
    pkg_t pkg;
    sfo_t sfo;
    int data; /* 1: has data (mounted by the game), 0: only unlocks content */
} dlc_src_t;

/* One library folder: <root>/ps4/<TID>/app.pkg plus an optional patch.pkg and add-ons. */
typedef struct {
    char dir[PATH_MAX], app_path[PATH_MAX], patch_path[PATH_MAX];
    pkg_t app, patch;
    int has_patch;
    sfo_t app_sfo, patch_sfo;
    char name[128], ver[8], marker[PATH_MAX + 160];
    char update_issue[256]; /* why patch.pkg was ignored, empty when used or absent */
    dlc_src_t *dlc;
    int ndlc;
    char dlc_tids[MAX_DLC_TIDS][10]; /* add-on title IDs: /user/addcont/<TID> folders */
    int ndlc_tids;
} title_src_t;

static int read_sfo(pkg_t *pkg, sfo_t *sfo) {
    size_t len;
    uint8_t *d = p4m_pkg_entry(pkg, 0x1000, &len);
    int rc = d ? p4m_sfo_parse(d, len, sfo) : -1;
    free(d);
    return rc;
}

static void src_close(title_src_t *t) {
    p4m_pkg_close(&t->app);
    if (t->has_patch)
        p4m_pkg_close(&t->patch);
    for (int i = 0; i < t->ndlc; i++)
        p4m_pkg_close(&t->dlc[i].pkg);
    free(t->dlc);
    t->dlc = NULL;
    t->ndlc = 0;
}

static int dlc_cmp(const void *a, const void *b) {
    return strcmp(((const dlc_src_t *)a)->pkg.content_id, ((const dlc_src_t *)b)->pkg.content_id);
}

/* Add-ons are used when their folder names match the pkg (title ID and label). */
static void dlc_open(title_src_t *t) {
    char base[PATH_MAX];
    snprintf(base, sizeof(base), "%s/addcont", t->dir);
    DIR *d = opendir(base);
    if (!d)
        return;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (strlen(e->d_name) != 9 || strncmp(e->d_name, "CUSA", 4))
            continue;
        char sub[PATH_MAX];
        snprintf(sub, sizeof(sub), "%s/%s", base, e->d_name);
        DIR *l = opendir(sub);
        if (!l)
            continue;
        struct dirent *f;
        while ((f = readdir(l))) {
            if (f->d_name[0] == '.' || strlen(f->d_name) != 16)
                continue;
            if (!t->dlc && !(t->dlc = calloc(MAX_DLC, sizeof(*t->dlc))))
                break;
            if (t->ndlc == MAX_DLC)
                break;
            dlc_src_t *a = &t->dlc[t->ndlc];
            snprintf(a->path, sizeof(a->path), "%s/%s/ac.pkg", sub, f->d_name);
            if (!exists(a->path))
                continue;
            if (p4m_pkg_open(&a->pkg, a->path) || read_sfo(&a->pkg, &a->sfo)) {
                if (a->pkg.fd > 0)
                    p4m_pkg_close(&a->pkg);
                continue;
            }
            const sfo_item_t *cat = p4m_sfo_get(&a->sfo, "CATEGORY");
            /* the fake key was checked when the pkg was moved here; checking it again on
             * every scan would cost an RSA operation per add-on */
            if (!cat || strcmp(cat->sval, "ac") || strcmp(a->pkg.title_id, e->d_name) ||
                strcmp(a->pkg.content_id + 20, f->d_name)) {
                p4m_pkg_close(&a->pkg);
                continue;
            }
            char rm[96];
            snprintf(rm, sizeof(rm), STATE_DIR "/%s.removed", a->pkg.content_id);
            if (exists(rm)) { /* deleted on the console */
                p4m_pkg_close(&a->pkg);
                continue;
            }
            a->data = a->pkg.header[0x77] == 0x1b; /* content type: 0x1b with data, 0x1c license only */
            t->ndlc++;
            int known = 0;
            for (int i = 0; i < t->ndlc_tids; i++)
                known |= !strcmp(t->dlc_tids[i], e->d_name);
            if (!known && t->ndlc_tids < MAX_DLC_TIDS)
                snprintf(t->dlc_tids[t->ndlc_tids++], 10, "%s", e->d_name);
        }
        closedir(l);
    }
    closedir(d);
    if (t->ndlc)
        qsort(t->dlc, (size_t)t->ndlc, sizeof(*t->dlc), dlc_cmp);
}

static int first_time(const char *path);

static int src_open(title_src_t *t, const char *root, const char *tid) {
    memset(t, 0, sizeof(*t));
    snprintf(t->dir, sizeof(t->dir), "%s/ps4/%s", root, tid);
    snprintf(t->app_path, sizeof(t->app_path), "%s/app.pkg", t->dir);
    snprintf(t->patch_path, sizeof(t->patch_path), "%s/patch.pkg", t->dir);
    if (p4m_pkg_open(&t->app, t->app_path) || read_sfo(&t->app, &t->app_sfo)) {
        printf("[scan] %s: unreadable app pkg\n", t->app_path);
        p4m_pkg_close(&t->app); /* (app) an unreadable param.sfo left the pkg open on every scan */
        return -1;
    }
    if (strcmp(t->app.title_id, tid)) { /* (app) the folder carries the header's id: another one is a changed pkg */
        printf("[scan] %s: title id %s in the pkg, not %s\n", t->app_path, t->app.title_id, tid);
        p4m_pkg_close(&t->app);
        return -1;
    }
    if (exists(t->patch_path)) {
        const sfo_item_t *gv = p4m_sfo_get(&t->app_sfo, "VERSION");
        if (p4m_pkg_open(&t->patch, t->patch_path) || read_sfo(&t->patch, &t->patch_sfo)) {
            snprintf(t->update_issue, sizeof(t->update_issue), "Update ignoriert: patch.pkg ließ sich nicht lesen");
            if (t->patch.fd > 0)
                p4m_pkg_close(&t->patch);
        } else {
            /* An update only fits the title it was made for. (Master VERSION may differ:
             * installed games run with base 01.02 + update 01.00.) */
            const sfo_item_t *uv = p4m_sfo_get(&t->patch_sfo, "VERSION");
            const char *gver = gv ? gv->sval : "?", *uver = uv ? uv->sval : "?";
            if (strcmp(t->patch.title_id, tid)) {
                snprintf(t->update_issue, sizeof(t->update_issue),
                         "Update ignoriert: Es gehört zu %s Version %s, das Spiel ist %s Version %s", t->patch.title_id, uver,
                         tid, gver);
                p4m_pkg_close(&t->patch);
            } else {
                t->has_patch = 1;
            }
        }
        if (t->update_issue[0] && first_time(t->patch_path)) {
            const sfo_item_t *title = p4m_sfo_get(&t->app_sfo, "TITLE");
            notify("%s: %s", title ? title->sval : tid, t->update_issue);
        }
    }
    const sfo_t *eff = t->has_patch ? &t->patch_sfo : &t->app_sfo;
    const sfo_item_t *it = p4m_sfo_get(eff, "TITLE");
    snprintf(t->name, sizeof(t->name), "%s", it ? it->sval : tid);
    it = p4m_sfo_get(eff, "APP_VER");
    snprintf(t->ver, sizeof(t->ver), "%s", it && !it->is_int ? it->sval : "01.00");
    char digest[65] = "";
    if (t->has_patch)
        for (int i = 0; i < 32; i++)
            snprintf(digest + i * 2, 3, "%02x", t->patch.digest[i]);
    snprintf(t->marker, sizeof(t->marker), "%s\n%s\n", t->dir, t->has_patch ? digest : "nopatch");
    if (g_cfg.dlc)
        dlc_open(t);
    if (t->ndlc) {
        /* one line for all add-ons, so markers of titles without add-ons stay as before */
        uint8_t h[32];
        buf_t all = {0};
        for (int i = 0; i < t->ndlc; i++)
            p4m_buf_put(&all, t->dlc[i].pkg.digest, 32);
        p4m_sha256(all.data, all.len, h);
        p4m_buf_free(&all);
        size_t n = strlen(t->marker);
        snprintf(t->marker + n, sizeof(t->marker) - n, "dlc %d ", t->ndlc);
        for (int i = 0; i < 32; i++) {
            n = strlen(t->marker);
            snprintf(t->marker + n, sizeof(t->marker) - n, "%02x", h[i]);
        }
        n = strlen(t->marker);
        snprintf(t->marker + n, sizeof(t->marker) - n, "\n");
    }
    return 0;
}

static void write_meta(pkg_t *pkg, const char *meta_dir, int *rc) {
    char q[PATH_MAX];
    for (const meta_entry_t *m = k_user_meta; m->name; m++) {
        snprintf(q, sizeof(q), "%s/%s", meta_dir, m->name);
        *rc |= write_entry(pkg, m->id, q) < 0;
    }
    for (uint32_t id = 0x1260; id <= 0x127f; id++) {
        size_t len;
        uint8_t *d = p4m_pkg_entry(pkg, id, &len);
        if (!d)
            continue;
        snprintf(q, sizeof(q), "%s/changeinfo", meta_dir);
        mkdirs(q, 0777);
        if (id == 0x1260)
            snprintf(q, sizeof(q), "%s/changeinfo/changeinfo.xml", meta_dir);
        else
            snprintf(q, sizeof(q), "%s/changeinfo/changeinfo_%02u.xml", meta_dir, id - 0x1261);
        *rc |= write_file(q, d, len, 0777);
        free(d);
    }
}

static void rm_tree(const char *path) {
    DIR *d = opendir(path);
    if (!d) {
        unlink(path);
        return;
    }
    struct dirent *e;
    char p[PATH_MAX];
    while ((e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
            continue;
        snprintf(p, sizeof(p), "%s/%s", path, e->d_name);
        struct stat st;
        if (!lstat(p, &st) && S_ISDIR(st.st_mode))
            rm_tree(p);
        else
            unlink(p);
    }
    closedir(d);
    rmdir(path);
}

static void hold_path(char *out, size_t n, const char *kind, const char *tid) {
    snprintf(out, n, HOLD_DIR "/%s/%s", kind, tid);
}

/* Fresh internal folder for kind ("app"/"patch") with name -> pkg_path. */
static int hold_prepare(const char *kind, const char *tid, const char *name, const char *pkg_path) {
    char dir[PATH_MAX], link[PATH_MAX];
    hold_path(dir, sizeof(dir), kind, tid);
    mkdirs(dir, 0777);
    snprintf(link, sizeof(link), "%s/%s", dir, name);
    unlink(link);
    if (symlink(pkg_path, link)) {
        printf("[reg] symlink %s: %s\n", link, strerror(errno));
        return -1;
    }
    return 0;
}

/* The link of kind exists and reaches its pkg. */
static int hold_ready(const char *kind, const char *tid, const char *name) {
    char p[PATH_MAX];
    hold_path(p, sizeof(p), kind, tid);
    snprintf(p + strlen(p), sizeof(p) - strlen(p), "/%s", name);
    return exists(p);
}

static uint8_t *read_all(const char *path, size_t *len) {
    struct stat st;
    *len = 0;
    if (stat(path, &st) || st.st_size <= 0 || st.st_size > (1 << 20))
        return NULL;
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return NULL;
    uint8_t *d = malloc((size_t)st.st_size);
    if (d && read(fd, d, (size_t)st.st_size) != st.st_size) {
        free(d);
        d = NULL;
    }
    close(fd);
    if (d)
        *len = (size_t)st.st_size;
    return d;
}

/* Adds or replaces the license slot of pkg in the rif/idx of its content ID prefix,
 * keeping the slots of the game and of other add-ons. */
static int place_license(pkg_t *pkg) {
    size_t lic_len, rl, il;
    uint8_t *lic = p4m_pkg_entry(pkg, 0x400, &lic_len);
    if (!lic || lic_len != 0x400) {
        free(lic);
        printf("[reg] %s: no license.dat\n", pkg->content_id);
        return -1;
    }
    char rp[96], ip[96];
    snprintf(rp, sizeof(rp), "/user/license/fake%.19s.rif", pkg->content_id);
    snprintf(ip, sizeof(ip), "/user/license/fake%.19s.idx", pkg->content_id);
    uint8_t *old_rif = read_all(rp, &rl), *old_idx = read_all(ip, &il);
    buf_t rif = {0}, idx = {0};
    p4m_build_license(&rif, &idx, old_rif, rl, old_idx, il, pkg->content_id, lic);
    int rc = write_buf(rp, &rif) | write_buf(ip, &idx);
    free(lic);
    free(old_rif);
    free(old_idx);
    p4m_buf_free(&rif);
    p4m_buf_free(&idx);
    return rc ? -1 : 0;
}

/* Internal folder of an add-on: HOLD_DIR/addcont/<ADD-ON TID>/<LABEL>, mounted (by
 * title ID) over /user/addcont/<ADD-ON TID>. It holds ac.pkg as a symlink to the pkg on
 * the drive plus ac.json/ac.pbm. Also writes the PlayGo records, the license slot and,
 * for add-ons with data, the icon. */
static int place_dlc(dlc_src_t *a) {
    pkg_t *pkg = &a->pkg;
    const sfo_item_t *v = p4m_sfo_get(&a->sfo, "VERSION");
    const char *ver = v && !v->is_int ? v->sval : "01.00", *label = pkg->content_id + 20;
    char dir[PATH_MAX], p[PATH_MAX], q[PATH_MAX];
    snprintf(dir, sizeof(dir), HOLD_DIR "/addcont/%s/%s", pkg->title_id, label);
    mkdirs(dir, 0777);
    snprintf(q, sizeof(q), "%s/ac.pkg", dir);
    unlink(q);
    int rc = 0;
    if (symlink(a->path, q)) {
        printf("[reg] symlink %s: %s\n", q, strerror(errno));
        rc = -1;
    }
    buf_t json = {0}, pbm = {0}, pdb = {0};
    p4m_build_app_json(&json, pkg, a->path);
    p4m_build_app_pbm(&pbm, pkg, ver, a->data ? PBM_ADDCONT : PBM_LICENSE);
    p4m_build_ac_pdb(&pdb, pkg, a->path, &json);
    snprintf(q, sizeof(q), "%s/ac.json", dir);
    rc |= write_buf(q, &json);
    snprintf(q, sizeof(q), "%s/ac.pbm", dir);
    rc |= write_buf(q, &pbm);
    snprintf(q, sizeof(q), "%s/ac.pbm.backup", dir);
    rc |= write_buf(q, &pbm);
    snprintf(p, sizeof(p), "/user/playgo/addcont/%s/00", pkg->content_id);
    mkdirs(p, 0777);
    snprintf(q, sizeof(q), "%s/d0.pdb", p);
    rc |= write_buf(q, &pdb);
    snprintf(q, sizeof(q), "%s/d1.pdb", p);
    rc |= write_buf(q, &pdb);
    snprintf(q, sizeof(q), "%s/f0.pdb", p);
    rc |= write_file(q, "", 0, 0777);
    rc |= place_license(pkg);
    if (a->data) {
        snprintf(p, sizeof(p), "/user/appmeta/addcont/%s/%s", pkg->title_id, label);
        mkdirs(p, 0777);
        snprintf(q, sizeof(q), "%s/icon0.png", p);
        rc |= write_entry(pkg, 0x1200, q) < 0;
    }
    p4m_buf_free(&json);
    p4m_buf_free(&pbm);
    p4m_buf_free(&pdb);
    if (rc)
        printf("[reg] add-on %s: could not write its files\n", pkg->content_id);
    return rc ? -1 : 0;
}

/* The ac.pkg link of every add-on exists and reaches its pkg. */
static int dlc_ready(const title_src_t *t) {
    char p[PATH_MAX];
    for (int i = 0; i < t->ndlc; i++) {
        snprintf(p, sizeof(p), HOLD_DIR "/addcont/%s/%s/ac.pkg", t->dlc[i].pkg.title_id,
                 t->dlc[i].pkg.content_id + 20);
        if (!exists(p))
            return 0;
    }
    return 1;
}

static int place_files(title_src_t *t) {
    pkg_t *app = &t->app, *pat = t->has_patch ? &t->patch : NULL;
    const char *tid = app->title_id, *cid = app->content_id;
    const sfo_item_t *av = p4m_sfo_get(&t->app_sfo, "APP_VER");
    const char *app_ver = av && !av->is_int ? av->sval : "01.00";
    char p[PATH_MAX], q[PATH_MAX], hold[PATH_MAX];
    int rc = 0;

    snprintf(p, sizeof(p), "/user/appmeta/%s", tid);
    mkdirs(p, 0777);
    write_meta(app, p, &rc);
    if (pat)
        write_meta(pat, p, &rc); /* the update's artwork wins, as with a real install */

    snprintf(p, sizeof(p), "/system_data/priv/appmeta/%s", tid);
    mkdirs(p, 0777);
    snprintf(q, sizeof(q), "%s/param.sfo", p);
    rc |= write_entry(pat ? pat : app, 0x1000, q) != 0;
    snprintf(q, sizeof(q), "%s/nptitle.dat", p);
    rc |= write_entry(app, 0x402, q) < 0;
    snprintf(q, sizeof(q), "%s/npbind.dat", p);
    rc |= write_entry(app, 0x403, q) < 0;

    if (place_license(app))
        return -1;
    for (int i = 0; i < t->ndlc; i++)
        rc |= place_dlc(&t->dlc[i]);

    size_t pl = 0, ppl = 0;
    uint8_t *pg = p4m_pkg_entry(app, 0x1001, &pl), *ppg = pat ? p4m_pkg_entry(pat, 0x1001, &ppl) : NULL;
    buf_t json = {0}, pbm = {0}, xml = {0}, pjson = {0}, ppbm_patch = {0}, pxml = {0}, ppbm = {0}, pdb = {0}, patch_pdb = {0};
    p4m_build_app_json(&json, app, t->app_path);
    p4m_build_app_pbm(&pbm, app, app_ver, PBM_APP);
    patch_ref_t pr = {0}, *prp = NULL;
    if (pat) {
        p4m_build_app_json(&pjson, pat, t->patch_path);
        p4m_build_app_pbm(&ppbm_patch, pat, t->ver, PBM_PATCH);
        pr = (patch_ref_t){pat, ppg, ppl, t->ver, t->patch_path, &pjson};
        prp = &pr;
    }
    if (p4m_build_app_xml(&xml, pg, pl) || p4m_build_pfs_pbm(&ppbm, app, pg, pl, prp) ||
        p4m_build_pdb(&pdb, app, t->ver, t->app_path, &json, pg, pl, prp) ||
        (pat && (p4m_build_app_xml(&pxml, ppg, ppl) || p4m_build_patch_pdb(&patch_pdb, &pr)))) {
        printf("[reg] %s: playgo-chunk.dat unusable\n", tid);
        rc = -1;
        goto out;
    }
    rc |= hold_prepare("app", tid, "app.pkg", t->app_path);
    hold_path(hold, sizeof(hold), "app", tid);
    snprintf(q, sizeof(q), "%s/app.json", hold);
    rc |= write_buf(q, &json);
    snprintf(q, sizeof(q), "%s/app.pbm", hold);
    rc |= write_buf(q, &pbm);
    snprintf(q, sizeof(q), "%s/app.pbm.backup", hold);
    rc |= write_buf(q, &pbm);
    snprintf(q, sizeof(q), "%s/app.xml", hold);
    rc |= write_buf(q, &xml);
    snprintf(p, sizeof(p), "/system_data/playgo/%s", tid);
    mkdirs(p, 0777);
    snprintf(q, sizeof(q), "%s/pfs.pbm", p);
    rc |= write_buf(q, &ppbm);
    snprintf(q, sizeof(q), "%s/pfs.pbm.backup", p);
    rc |= write_buf(q, &ppbm);
    snprintf(p, sizeof(p), "/user/playgo/app/%s/00", cid);
    mkdirs(p, 0777);
    snprintf(q, sizeof(q), "%s/d0.pdb", p);
    rc |= write_buf(q, &pdb);
    snprintf(q, sizeof(q), "%s/d1.pdb", p);
    rc |= write_buf(q, &pdb);
    snprintf(p, sizeof(p), "/user/playgo/patch/%s", cid);
    if (pat) {
        rc |= hold_prepare("patch", tid, "patch.pkg", t->patch_path);
        hold_path(hold, sizeof(hold), "patch", tid);
        snprintf(q, sizeof(q), "%s/patch.json", hold);
        rc |= write_buf(q, &pjson);
        snprintf(q, sizeof(q), "%s/patch.pbm", hold);
        rc |= write_buf(q, &ppbm_patch);
        snprintf(q, sizeof(q), "%s/patch.pbm.backup", hold);
        rc |= write_buf(q, &ppbm_patch);
        snprintf(q, sizeof(q), "%s/patch.xml", hold);
        rc |= write_buf(q, &pxml);
        snprintf(p, sizeof(p), "/user/playgo/patch/%s/00/00.00", cid);
        mkdirs(p, 0777);
        snprintf(q, sizeof(q), "%s/d0.pdb", p);
        rc |= write_buf(q, &patch_pdb);
        snprintf(q, sizeof(q), "%s/d1.pdb", p);
        rc |= write_buf(q, &patch_pdb);
    } else {
        rm_tree(p);
        hold_path(hold, sizeof(hold), "patch", tid);
        rm_tree(hold);
    }
out:
    free(pg);
    free(ppg);
    p4m_buf_free(&json);
    p4m_buf_free(&pbm);
    p4m_buf_free(&xml);
    p4m_buf_free(&pjson);
    p4m_buf_free(&ppbm_patch);
    p4m_buf_free(&pxml);
    p4m_buf_free(&ppbm);
    p4m_buf_free(&pdb);
    p4m_buf_free(&patch_pdb);
    return rc ? -1 : 0;
}

#define IOV(s) {(void *)(s), strlen(s) + 1}

/* Titles this daemon serves. */
typedef struct {
    int used, has_patch, running, ndlc, ndlc_tids;
    char dlc_tids[MAX_DLC_TIDS][10];
    time_t running_since; /* launch seen in klog; the sandbox mount appears shortly after */
    char tid[10], cid[37], name[128], dir[PATH_MAX];
} managed_t;

static managed_t g_managed[MAX_TITLES];
static int g_manual_scan; /* "Scan for games" in the panel */
static int g_deleting; /* a delete screen is open: game folders are unmounted */
static time_t g_deleting_since;
static time_t g_deleting_close_at; /* set when the uninstall finished */

static managed_t *managed_by(const char *tid, const char *cid) {
    for (int i = 0; i < MAX_TITLES; i++)
        if (g_managed[i].used && ((tid && !strcmp(g_managed[i].tid, tid)) || (cid && !strcmp(g_managed[i].cid, cid))))
            return &g_managed[i];
    return NULL;
}

/* Top mount on target: 0 none, 1 our folder, -1 something else (like a USB
 * folder mounted by an older PS4Mount). */
static int top_layer(const char *target, const char *dir) {
    struct statfs sf;
    if (statfs(target, &sf) || strcmp(sf.f_mntonname, target))
        return 0;
    return strcmp(sf.f_fstypename, "nullfs") || strcmp(sf.f_mntfromname, dir) ? -1 : 1;
}

static int nullfs_mount(const char *dir, const char *target) {
    struct iovec iov[] = {IOV("fstype"), IOV("nullfs"), IOV("from"), IOV(dir), IOV("fspath"), IOV(target)};
    if (nmount(iov, 6, 0)) {
        printf("[mount] %s -> %s: %s\n", dir, target, strerror(errno));
        return -1;
    }
    return 0;
}

/* A running PS4 title keeps /mnt/sandbox/pfsmnt/<TID>-app0 mounted. Unlike the klog
 * events this survives a restart of the daemon, so it is the reference for "running". */
/* Sandbox mounts of running titles, read from the mount table at most once a second
 * (the table holds over a thousand entries; reading it per title would be wasteful). */
static char (*g_sandboxes)[10];
static int g_nsandboxes;
static time_t g_sandboxes_at;

static void refresh_sandboxes(void) {
    if (g_sandboxes && time(NULL) == g_sandboxes_at)
        return;
    g_sandboxes_at = time(NULL);
    g_nsandboxes = 0;
    int n = getfsstat(NULL, 0, MNT_NOWAIT);
    if (n <= 0)
        return;
    struct statfs *list = malloc(sizeof(*list) * (size_t)(n + 16));
    char (*ids)[10] = realloc(g_sandboxes, sizeof(*ids) * (size_t)(n + 16));
    if (ids)
        g_sandboxes = ids;
    if (!list || !ids) {
        free(list);
        return;
    }
    n = getfsstat(list, (long)(sizeof(*list) * (size_t)(n + 16)), MNT_NOWAIT);
    static const char prefix[] = "/mnt/sandbox/pfsmnt/";
    for (int i = 0; i < n; i++) {
        const char *on = list[i].f_mntonname;
        if (strlen(on) != sizeof(prefix) - 1 + 9 + 5 || strncmp(on, prefix, sizeof(prefix) - 1) ||
            strcmp(on + sizeof(prefix) - 1 + 9, "-app0"))
            continue;
        snprintf(g_sandboxes[g_nsandboxes++], 10, "%.9s", on + sizeof(prefix) - 1);
    }
    free(list);
}

static int title_running(const char *tid) {
    refresh_sandboxes();
    for (int i = 0; i < g_nsandboxes; i++)
        if (!strcmp(g_sandboxes[i], tid))
            return 1;
    return 0;
}

static void unmount_all_layers(const char *target, int force);

/* Set while /user/app shows only the games being registered (see mask_user_app): the
 * other game folders are hidden then, and must not be mounted or unmounted. */
static int g_masked;

static int masked_path(const char *target) {
    if (!g_masked || strncmp(target, "/user/app/", 10))
        return 0;
    printf("[mount] %s: skipped while /user/app shows only the new games\n", target);
    return 1;
}

static int mount_dir(const char *dir, const char *target) {
    if (masked_path(target))
        return 0;
    /* /user/patch and /user/addcont only exist once the console installed an update or
     * an add-on the normal way: create the parent like the system does (0770) */
    char parent[64];
    snprintf(parent, sizeof(parent), "%s", target);
    char *slash = strrchr(parent, '/');
    if (slash && slash != parent) {
        *slash = 0;
        if (mkdir(parent, 0770) == 0)
            printf("[mount] created %s\n", parent);
    }
    mkdir(target, 0777);
    if (dead_layer(target)) {
        printf("[mount] %s: removing a layer of a disconnected drive\n", target);
        unmount_all_layers(target, 1);
    }
    int top = top_layer(target, dir);
    if (top == 1)
        return 0;
    while (top != 0) {
        if (unmount(target, 0)) {
            printf("[mount] unmount %s: %s\n", target, strerror(errno));
            return -1;
        }
        top = top_layer(target, dir);
    }
    return nullfs_mount(dir, target);
}

static void unmount_all_layers(const char *target, int force) {
    if (masked_path(target))
        return;
    for (int i = 0; i < 8 && (mounted_from(target, NULL, 0) || layers_on(target) > 0); i++)
        if (unmount(target, force ? MNT_FORCE : 0)) {
            printf("[mount] unmount %s: %s\n", target, strerror(errno));
            if (!force)
                return;
        }
}

/* Removes every layer of the title (forced: a system uninstall is about to run). */
static void unmount_title(const char *tid, char (*dtids)[10], int ndtids) {
    char target[64];
    snprintf(target, sizeof(target), "/user/app/%s", tid);
    unmount_all_layers(target, 1);
    snprintf(target, sizeof(target), "/user/patch/%s", tid);
    unmount_all_layers(target, 1);
    for (int i = 0; i < ndtids; i++) {
        snprintf(target, sizeof(target), "/user/addcont/%s", dtids[i]);
        unmount_all_layers(target, 1);
    }
}

/* Add-ons installed the normal way: their folder is never covered by a mount. */
static int addcont_installed(const char *dtid) {
    char target[64];
    snprintf(target, sizeof(target), "/user/addcont/%s", dtid);
    if (mounted_from(target, NULL, 0))
        return 0;
    DIR *d = opendir(target);
    if (!d)
        return 0;
    struct dirent *e;
    int used = 0;
    while (!used && (e = readdir(d)))
        used = e->d_name[0] != '.';
    closedir(d);
    return used;
}

/* /user/app/<TID> always, /user/patch/<TID> only with an update and
 * /user/addcont/<ADD-ON TID> for each add-on title. */
static int mount_paths(const char *tid, int has_patch, char (*dtids)[10], int ndtids) {
    char target[64], hold[PATH_MAX];
    snprintf(target, sizeof(target), "/user/app/%s", tid);
    hold_path(hold, sizeof(hold), "app", tid);
    if (mount_dir(hold, target))
        return -1;
    snprintf(target, sizeof(target), "/user/patch/%s", tid);
    hold_path(hold, sizeof(hold), "patch", tid);
    if (has_patch ? mount_dir(hold, target) : (unmount_all_layers(target, 1), 0))
        return -1;
    for (int i = 0; i < ndtids; i++) {
        if (addcont_installed(dtids[i])) {
            printf("[mount] /user/addcont/%s holds add-ons installed the normal way, not mounted\n", dtids[i]);
            continue;
        }
        snprintf(target, sizeof(target), "/user/addcont/%s", dtids[i]);
        hold_path(hold, sizeof(hold), "addcont", dtids[i]);
        if (mount_dir(hold, target))
            return -1;
    }
    return 0;
}

static int mount_title(title_src_t *t) {
    return mount_paths(t->app.title_id, t->has_patch, t->dlc_tids, t->ndlc_tids);
}

static void manage(const title_src_t *t) {
    managed_t *m = managed_by(t->app.title_id, NULL);
    for (int i = 0; !m && i < MAX_TITLES; i++)
        if (!g_managed[i].used)
            m = &g_managed[i];
    if (!m)
        return;
    /* keep the runtime state across rescans */
    managed_t keep = *m;
    memset(m, 0, sizeof(*m));
    m->used = 1;
    if (keep.used) {
        m->running = keep.running;
        m->running_since = keep.running_since;
    }
    m->has_patch = t->has_patch;
    m->ndlc = t->ndlc;
    m->ndlc_tids = t->ndlc_tids;
    memcpy(m->dlc_tids, t->dlc_tids, sizeof(m->dlc_tids));
    snprintf(m->tid, sizeof(m->tid), "%s", t->app.title_id);
    snprintf(m->cid, sizeof(m->cid), "%s", t->app.content_id);
    snprintf(m->name, sizeof(m->name), "%s", t->name);
    snprintf(m->dir, sizeof(m->dir), "%s", t->dir);
}


static int removed(const char *tid) {
    char p[64];
    snprintf(p, sizeof(p), STATE_DIR "/%s.removed", tid);
    return exists(p);
}

/* Real installs keep app.pkg on the internal drive; never touch those. */
static int installed_internally(const char *tid) {
    char p[64];
    snprintf(p, sizeof(p), "/user/app/%s", tid);
    if (mounted_from(p, NULL, 0))
        return 0;
    snprintf(p, sizeof(p), "/user/app/%s/app.pkg", tid);
    return exists(p);
}

/* PS4Mount 0.6.1: the marker without the drive name: "/mnt/usb0/ps4/<TID>..." -> "/ps4/<TID>...".
 * Drives are named in the order they are mounted, so the same drive can come back
 * under another name; the registration does not depend on it. */
static const char *skip_drive(const char *marker) {
    if (strncmp(marker, "/mnt/", 5))
        return marker;
    const char *s = strchr(marker + 5, '/');
    return s ? s : marker;
}

/* The stored marker, without the drive name, is expected; it is copied to stored. */
static int marker_matches(const char *tid, const char *expected, char *stored, size_t len) {
    char path[64], buf[PATH_MAX + 256] = {0};
    snprintf(path, sizeof(path), STATE_DIR "/%s.src", tid);
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return 0;
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    snprintf(stored, len, "%s", buf);
    return n > 0 && !strcmp(skip_drive(buf), skip_drive(expected));
}

/* 1 = needs (re)registration, 0 = registered here, -1 = skip. */
/* The title is served from another connected drive: the marker names another
 * folder whose game pkg is still there. The first drive keeps it, so the same game
 * on two drives is not switched (and registered again) on every scan. */
static int served_elsewhere(const title_src_t *t) {
    char path[64], buf[PATH_MAX + 256] = {0}, pkg[PATH_MAX + 16];
    snprintf(path, sizeof(path), STATE_DIR "/%s.src", t->app.title_id);
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return 0;
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0)
        return 0;
    buf[strcspn(buf, "\n")] = 0;
    if (!strcmp(buf, t->dir))
        return 0;
    snprintf(pkg, sizeof(pkg), "%s/app.pkg", buf);
    return exists(pkg);
}

/* PS4Mount 0.6.1: a registered game whose drive came back under another name: the links and records
 * that hold the pkg path are written again for the new folder, without registering the
 * game again. */
static int relocate(title_src_t *t, char *stored) {
    if (!strcmp(stored, t->marker))
        return 0;
    char was[PATH_MAX];
    snprintf(was, sizeof(was), "%.*s", (int)strcspn(stored, "\n"), stored);
    if (place_files(t)) {
        printf("[scan] %s: drive renamed (was %s), could not move its files\n", t->dir, was);
        return -1;
    }
    printf("[scan] %s: drive renamed (was %s), kept registered\n", t->dir, was);
    char path[64]; /* named after the folder, which src_open matched to the pkg */
    snprintf(path, sizeof(path), STATE_DIR "/%s.src", strrchr(t->dir, '/') + 1);
    return write_file(path, t->marker, strlen(t->marker), 0666);
}

static int title_state(title_src_t *t) {
    const char *tid = t->app.title_id;
    if (installed_internally(tid) || removed(tid))
        return -1;
    if (served_elsewhere(t)) {
        if (first_time(t->app_path))
            printf("[scan] %s: %s is already served from another drive, this copy is not used\n", t->dir, tid);
        return -1;
    }
    char pdb[PATH_MAX];
    snprintf(pdb, sizeof(pdb), "/user/playgo/app/%s/00/d0.pdb", t->app.content_id);
    char stored[PATH_MAX + 256];
    if (!p4m_db_is_registered(tid) || !exists(pdb) || !marker_matches(tid, t->marker, stored, sizeof(stored)) ||
        relocate(t, stored))
        return 1;
    /* folders set up by 0.3.1 and older lack the internal folder: registered again */
    return !hold_ready("app", tid, "app.pkg") || (t->has_patch && !hold_ready("patch", tid, "patch.pkg")) ||
           !dlc_ready(t);
}

static void pump_events(int ms);

/* The launch check compares /system_data/priv/appmeta/<TID>/param.sfo with the
 * update's param.sfo byte for byte; AppInstallAll writes the game's one there. */
static void enforce_patch_sfo(title_src_t *t) {
    if (!t->has_patch)
        return;
    size_t len;
    uint8_t *sfo = p4m_pkg_entry(&t->patch, 0x1000, &len);
    if (!sfo)
        return;
    char path[96];
    snprintf(path, sizeof(path), "/system_data/priv/appmeta/%s/param.sfo", t->app.title_id);
    int fd = open(path, O_RDONLY), same = 0;
    if (fd >= 0) {
        uint8_t *cur = malloc(len + 1);
        same = cur && read(fd, cur, len + 1) == (ssize_t)len && !memcmp(cur, sfo, len);
        free(cur);
        close(fd);
    }
    if (!same) {
        printf("[reg] %s: priv param.sfo was not the update's, rewritten\n", t->app.title_id);
        write_file(path, sfo, len, 0777);
    }
    free(sfo);
}

/* Registration method: see fw.h. */
typedef int (*title_dir_fn)(const char *title_id, const char *dir, void *reserved);

static int g_reg_mode = REG_ALL;
static title_dir_fn g_title_dir;
static char g_fw_text[16] = "unknown";

static const char *reg_mode_name(void) {
    return g_reg_mode == REG_ALL ? "AppInstallAll" : g_reg_mode == REG_TITLEDIR ? "AppInstallTitleDir" : "none";
}

static void check_firmware(void) {
    uint32_t fw = kernel_get_fw_version();
    unsigned major, minor;
    p4m_fw_decode(fw, &major, &minor);
    p4m_fw_text(fw, g_fw_text, sizeof(g_fw_text));
    /* looked up at runtime: linking it would stop the payload from loading where it is gone */
    uint32_t h = 0;
    if (!kernel_dynlib_handle(getpid(), "libSceAppInstUtil.sprx", &h))
        g_title_dir = (title_dir_fn)kernel_dynlib_dlsym(getpid(), h, "sceAppInstUtilAppInstallTitleDir");
    g_reg_mode = p4m_fw_reg_mode(major, g_title_dir != NULL);
    printf("[main] firmware %s (0x%08x), AppInstallTitleDir %s, registration: %s\n", g_fw_text, fw,
           g_title_dir ? "present" : "missing", reg_mode_name());
}

/* The system registration runs in its own thread: after rest mode InstallAll was seen to
 * block for good, which froze the whole daemon (panel, klog, stop request). A call still
 * running is never doubled; titles keep being checked in the database. */
static volatile int g_install_busy, g_install_rc;
static char g_install_tids[MAX_TITLES][10];
static int g_install_n;

static int register_call(const char *tid) {
    if (g_reg_mode == REG_TITLEDIR) {
        int rc = g_title_dir(tid, "/user/app/", NULL);
        printf("[reg] %s: AppInstallTitleDir=0x%08x\n", tid, rc);
        return rc == (int)0x80990002 ? 0 : rc; /* 0x80990002: already known, restored */
    }
    return sceAppInstUtilAppInstallAll ? sceAppInstUtilAppInstallAll(NULL) : -1; /* (app) weak */
}

static void *install_thread(void *arg) {
    (void)arg;
    int rc = 0;
    if (g_reg_mode == REG_TITLEDIR) {
        for (int i = 0; i < g_install_n; i++) {
            int r = register_call(g_install_tids[i]);
            if (r)
                rc = r;
        }
    } else {
        rc = register_call(NULL);
    }
    g_install_rc = rc;
    g_install_busy = 0;
    return NULL;
}

/* Registers the titles of tids that are not registered yet. */
static int install_all_start(char (*tids)[10], int n) {
    if (g_reg_mode == REG_NONE)
        return -1; /* never where it corrupts the database */
    if (g_install_busy) {
        printf("[reg] registration still running from an earlier call\n");
        return -1;
    }
    g_install_n = 0;
    for (int i = 0; i < n && g_install_n < MAX_TITLES; i++)
        if (!p4m_db_is_registered(tids[i]))
            snprintf(g_install_tids[g_install_n++], 10, "%s", tids[i]);
    g_install_busy = 1;
    pthread_t th;
    if (pthread_create(&th, NULL, install_thread, NULL)) {
        install_thread(NULL); /* no thread: call it here */
        return 0;
    }
    pthread_detach(th);
    return 0;
}

/* Registration runs in three steps so a group of games costs the system one
 * AppInstallAll and no write to its databases while it is still busy:
 *   1. register_prepare: files and mounts of each game of the group;
 *   2. one sceAppInstUtilAppInstallAll, then wait until ShellCore has the rows;
 *   3. register_finish: checks the title is registered, writes the marker. */
static int register_prepare(title_src_t *t) {
    if (place_files(t) || mount_title(t))
        return -1;
    manage(t);
    return 0;
}

/* Max mtime of the system databases (and their journals): ShellCore is busy while it changes. */
static time_t db_mtime(void) {
    static const char *files[] = {"/system_data/priv/mms/app.db", "/system_data/priv/mms/app.db-journal",
                                  "/system_data/priv/mms/appinfo.db", "/system_data/priv/mms/appinfo.db-journal",
                                  "/system_data/priv/mms/addcont.db", NULL};
    time_t t = 0;
    struct stat st;
    for (int i = 0; files[i]; i++)
        if (!stat(files[i], &st) && st.st_mtime > t)
            t = st.st_mtime;
    return t;
}

/* Waits until the system databases were not written for quiet seconds (max limit). */
static void wait_db_quiet(int quiet, int limit) {
    time_t start = time(NULL), last = db_mtime(), changed = time(NULL);
    while (time(NULL) - changed < quiet && time(NULL) - start < limit) {
        pump_events(1000);
        time_t m = db_mtime();
        if (m != last) {
            last = m;
            changed = time(NULL);
        }
    }
}

/* ShellCore registers every new title under /user/app itself; this also refreshes the
 * home screen and fetches the PSN concept. Waits while titles keep arriving. */
/* AppInstallAll walks every folder of /system_ex/app, /system_ex/rnps/apps and /user/app
 * (about 0.7 s per PS4 game, registered or not). While it runs, /user/app shows only the
 * games being added: an empty staging folder is mounted over /user/app and each new
 * game's folder on top of it. The other games stay registered (their rows are not
 * touched) and their own mounts stay in place underneath. (PS4Mount 0.6.0; in the app
 * only when the setting "fast_reg" is on, which is off by default.) */
#define STAGE_DIR HOLD_DIR "/stage"

static int open_games(uint32_t *ids, char (*tids)[10], int max);

static void stage_clear(void) {
    DIR *d = opendir(STAGE_DIR);
    if (!d)
        return;
    struct dirent *e;
    char p[PATH_MAX];
    while ((e = readdir(d)))
        if (e->d_name[0] != '.') {
            snprintf(p, sizeof(p), STAGE_DIR "/%s", e->d_name);
            rmdir(p);
        }
    closedir(d);
}

static int user_app_masked(void) {
    struct statfs sf;
    return !statfs("/user/app", &sf) && !strcmp(sf.f_mntonname, "/user/app") && !strcmp(sf.f_mntfromname, STAGE_DIR);
}

static void unmask_user_app(char (*tids)[10], int n) {
    char target[64];
    for (int i = 0; i < n; i++) {
        snprintf(target, sizeof(target), "/user/app/%s", tids[i]);
        if (user_app_masked() && mounted_from(target, NULL, 0) && unmount(target, 0) && unmount(target, MNT_FORCE))
            printf("[reg] unmount %s: %s\n", target, strerror(errno));
    }
    /* the system keeps the folder open for a moment after AppInstallAll returned */
    for (int i = 0; i < 12 && user_app_masked(); i++) {
        if (!unmount("/user/app", 0))
            break;
        pump_events(250);
    }
    if (user_app_masked() && unmount("/user/app", MNT_FORCE))
        printf("[reg] unmount /user/app: %s\n", strerror(errno));
    if (user_app_masked())
        printf("[reg] /user/app still shows only the new games\n");
    g_masked = 0;
    stage_clear();
}

/* The app (or the daemon) ended while /user/app showed only the new games: show all of them again. */
static void unmask_leftover(void) {
    static char tids[MAX_TITLES][10];
    int n = 0;
    DIR *d = opendir(STAGE_DIR);
    struct dirent *e;
    while (d && (e = readdir(d)) && n < MAX_TITLES)
        if (strlen(e->d_name) == 9)
            snprintf(tids[n++], 10, "%s", e->d_name);
    if (d)
        closedir(d);
    printf("[reg] /user/app showed only %d game(s) from an earlier run, restoring it\n", n);
    g_masked = 1;
    unmask_user_app(tids, n);
}

/* Returns 1 when /user/app now shows only the games of tids that are not registered. */
static int mask_user_app(char (*tids)[10], int n) {
    uint32_t ids[4];
    char open[4][10];
    if (!g_cfg.fast_reg || g_reg_mode != REG_ALL || open_games(ids, open, 4))
        return 0; /* off, or an open game (or an unknown state) keeps its folder in use: plain AppInstallAll */
    if (user_app_masked())
        unmask_leftover();
    stage_clear();
    mkdirs(STAGE_DIR, 0777);
    char new_tids[MAX_TITLES][10];
    int nn = 0;
    for (int i = 0; i < n && nn < MAX_TITLES; i++)
        if (!p4m_db_is_registered(tids[i])) {
            char p[PATH_MAX];
            snprintf(p, sizeof(p), STAGE_DIR "/%s", tids[i]);
            if (mkdir(p, 0777) && errno != EEXIST)
                return 0;
            snprintf(new_tids[nn++], 10, "%s", tids[i]);
        }
    if (!nn || nullfs_mount(STAGE_DIR, "/user/app"))
        return 0;
    for (int i = 0; i < nn; i++) {
        char hold[PATH_MAX], target[64];
        hold_path(hold, sizeof(hold), "app", new_tids[i]);
        snprintf(target, sizeof(target), "/user/app/%s", new_tids[i]);
        if (nullfs_mount(hold, target)) {
            unmask_user_app(new_tids, i);
            return 0;
        }
    }
    g_masked = 1;
    printf("[reg] /user/app shows only the %d new game(s) during AppInstallAll\n", nn);
    return 1;
}

static void install_all(char (*tids)[10], int n) {
    int need = 0;
    for (int i = 0; i < n; i++)
        need += !p4m_db_is_registered(tids[i]);
    if (!need)
        return;
    int masked = mask_user_app(tids, n);
    if (install_all_start(tids, n) && masked) {
        unmask_user_app(tids, n);
        masked = 0;
    }
    int done = n - need, idle = 0;
    while (done < n && idle < 30) {
        pump_events(1000);
        int now = 0;
        for (int i = 0; i < n; i++)
            now += p4m_db_is_registered(tids[i]);
        idle = now > done ? 0 : idle + 1;
        done = now;
        if (masked && !g_install_busy) {
            unmask_user_app(tids, n);
            masked = 0;
        }
    }
    /* the rows can be there before AppInstallAll returns: the other games stay hidden
     * until it does (at most two minutes) */
    for (int i = 0; masked && g_install_busy && i < 120; i++)
        pump_events(1000);
    if (masked)
        unmask_user_app(tids, n);
    printf("[reg] %s=%s0x%08x: %d of %d title(s) registered\n", reg_mode_name(), g_install_busy ? "(still running) " : "",
           g_install_rc, done, n);
}

/* The system databases are only ever written by the system: ShellCore keeps them
 * open with its own cache, and rows written from outside make it flag app.db as
 * corrupted (and rebuild it at the next boot). A title the system did not register
 * is tried again later. */
static int register_finish(title_src_t *t) {
    const char *tid = t->app.title_id;
    if (!p4m_db_is_registered(tid)) {
        printf("[reg] %s: not registered by the system\n", tid);
        return -1;
    }
    enforce_patch_sfo(t);
    char marker[64];
    snprintf(marker, sizeof(marker), STATE_DIR "/%s.src", tid);
    if (write_file(marker, t->marker, strlen(t->marker), 0666))
        return -1;
    manage(t);
    return 0;
}

/* Logs a skipped root file once per daemon run (any number of files). */
static int first_time(const char *path) {
    static char **seen;
    static size_t n, cap;
    for (size_t i = 0; i < n; i++)
        if (!strcmp(seen[i], path))
            return 0;
    if (n == cap) {
        size_t ncap = cap ? cap * 2 : 64;
        char **grown = realloc(seen, ncap * sizeof(*seen));
        if (!grown)
            return 1;
        seen = grown;
        cap = ncap;
    }
    seen[n] = strdup(path);
    if (seen[n])
        n++;
    return 1;
}


/* 1 when sfo lists the add-on prefix (UP0000-CUSA00000_00) as one of its add-on services. */
static int sfo_serves(const sfo_t *sfo, const char *prefix) {
    char key[32];
    for (int i = 1; i <= 7; i++) {
        snprintf(key, sizeof(key), "SERVICE_ID_ADDCONT_ADD_%d", i);
        const sfo_item_t *it = p4m_sfo_get(sfo, key);
        if (it && !it->is_int && !strncmp(it->sval, prefix, 19))
            return 1;
    }
    return 0;
}

/* The game on this drive an add-on belongs to: same title ID, or a game (or its
 * update) that names the add-on's content ID prefix in SERVICE_ID_ADDCONT_ADD_n
 * (regional versions share add-ons this way). */
static int find_dlc_game(const char *root, const pkg_t *dlc, char *tid) {
    char lib[PATH_MAX], p[PATH_MAX];
    snprintf(lib, sizeof(lib), "%s/ps4", root);
    snprintf(p, sizeof(p), "%s/%s/app.pkg", lib, dlc->title_id);
    if (exists(p)) {
        snprintf(tid, 10, "%s", dlc->title_id);
        return 0;
    }
    DIR *d = opendir(lib);
    if (!d)
        return -1;
    struct dirent *e;
    int found = 0;
    while (!found && (e = readdir(d))) {
        if (strlen(e->d_name) != 9 || strncmp(e->d_name, "CUSA", 4))
            continue;
        static const char *names[] = {"app.pkg", "patch.pkg"};
        for (int i = 0; i < 2 && !found; i++) {
            pkg_t pkg;
            sfo_t sfo;
            snprintf(p, sizeof(p), "%s/%s/%s", lib, e->d_name, names[i]);
            if (!exists(p) || p4m_pkg_open(&pkg, p))
                continue;
            found = !read_sfo(&pkg, &sfo) && sfo_serves(&sfo, dlc->content_id);
            p4m_pkg_close(&pkg);
        }
        if (found)
            snprintf(tid, 10, "%s", e->d_name);
    }
    closedir(d);
    return found ? 0 : -1;
}

/* Add-ons left in place during one adoption run, reported in one notification. */
static int g_dlc_orphans, g_dlc_dupes;

/* Add-ons waiting for their game: checked again only when the games of the drive
 * change (the check opens the pkg of every game). */
typedef struct {
    char *path;
    unsigned long sig;
} waiting_t;

static waiting_t *g_waiting;
static size_t g_nwaiting, g_capwaiting;
static unsigned long g_lib_sig; /* games of the drive being scanned */

/* Signature of the games in <root>/ps4: changes when a game is added or removed. */
static unsigned long lib_signature(const char *root) {
    char lib[PATH_MAX], p[PATH_MAX];
    snprintf(lib, sizeof(lib), "%s/ps4", root);
    DIR *d = opendir(lib);
    if (!d)
        return 0;
    struct dirent *e;
    unsigned long sig = 5381;
    while ((e = readdir(d))) {
        if (strlen(e->d_name) != 9 || strncmp(e->d_name, "CUSA", 4))
            continue;
        snprintf(p, sizeof(p), "%s/%s/app.pkg", lib, e->d_name);
        if (!exists(p))
            continue;
        unsigned long h = 0;
        for (const char *c = e->d_name; *c; c++)
            h = h * 33 + (unsigned char)*c;
        sig += h; /* order independent */
    }
    closedir(d);
    return sig;
}

static waiting_t *waiting_find(const char *path) {
    for (size_t i = 0; i < g_nwaiting; i++)
        if (!strcmp(g_waiting[i].path, path))
            return &g_waiting[i];
    return NULL;
}

static void waiting_set(const char *path, unsigned long sig) {
    waiting_t *w = waiting_find(path);
    if (w) {
        w->sig = sig;
        return;
    }
    if (g_nwaiting == g_capwaiting) {
        size_t cap = g_capwaiting ? g_capwaiting * 2 : 64;
        waiting_t *grown = realloc(g_waiting, cap * sizeof(*grown));
        if (!grown)
            return;
        g_waiting = grown;
        g_capwaiting = cap;
    }
    if ((g_waiting[g_nwaiting].path = strdup(path)))
        g_waiting[g_nwaiting++].sig = sig;
}

static void waiting_drop(const char *path) {
    waiting_t *w = waiting_find(path);
    if (!w)
        return;
    free(w->path);
    *w = g_waiting[--g_nwaiting];
}

/* Add-ons of root still waiting for their game. */
static int waiting_count(const char *root) {
    size_t l = strlen(root);
    int n = 0;
    for (size_t i = 0; i < g_nwaiting; i++)
        n += !strncmp(g_waiting[i].path, root, l) && g_waiting[i].path[l] == '/' && exists(g_waiting[i].path);
    return n;
}

/* Path of a file relative to the drive root it is on. */
static const char *drive_rel(const char *root, const char *path) {
    size_t n = strlen(root);
    return !strncmp(path, root, n) && path[n] == '/' ? path + n + 1 : path;
}

/* Records where a pkg moved into <root>/ps4/<TID>/<rel> came from. */
static void record_origin(const char *root, const char *tid, const char *rel, const char *from) {
    char game[PATH_MAX];
    snprintf(game, sizeof(game), "%s/ps4/%s", root, tid);
    if (p4m_origin_add(game, rel, drive_rel(root, from)))
        printf("[scan] %s: could not record where %s came from\n", game, rel);
}

/* Moves an add-on pkg (ac) into <root>/ps4/<GAME>/addcont/<ADD-ON TID>/<LABEL>/ac.pkg. */
static int adopt_dlc(const char *root, const char *name, const char *path, pkg_t *pkg) {
    char game[10], dst[PATH_MAX];
    if (find_dlc_game(root, pkg, game)) {
        if (first_time(path)) {
            /* a game served from another drive is mounted there too: only a real install counts */
            printf("[scan] %s: no game for add-on %s on the drive%s, waiting\n", name, pkg->content_id,
                   installed_internally(pkg->title_id) ? " (that title is installed on the console)" : "");
            g_dlc_orphans++;
        }
        waiting_set(path, g_lib_sig);
        return 0;
    }
    waiting_drop(path);
    snprintf(dst, sizeof(dst), "%s/ps4/%s/addcont/%s/%s", root, game, pkg->title_id, pkg->content_id + 20);
    mkdirs(dst, 0777);
    snprintf(dst, sizeof(dst), "%s/ps4/%s/addcont/%s/%s/ac.pkg", root, game, pkg->title_id, pkg->content_id + 20);
    if (exists(dst)) {
        if (first_time(path)) {
            printf("[scan] %s: %s already has this add-on, the copy was left in place\n", name, game);
            g_dlc_dupes++;
        }
        return 0;
    }
    if (rename(path, dst)) {
        printf("[scan] move %s: %s\n", path, strerror(errno));
        return 0;
    }
    printf("[scan] moved %s -> %s\n", path, dst);
    char rel[64];
    snprintf(rel, sizeof(rel), "addcont/%s/%s/ac.pkg", pkg->title_id, pkg->content_id + 20);
    record_origin(root, game, rel, path);
    return 1;
}

/* Moves PS4 game (gd) and update (gp) pkgs found in dir (the drive root or a scan
 * folder) into <root>/ps4/<TID>/app.pkg and patch.pkg, then add-ons (ac) next to
 * their game. */
static int adopt_dir_pkgs(const char *root, const char *dir) {
    int moved = 0;
    DIR *d = opendir(dir);
    if (!d)
        return 0;
    struct dirent *e;
    char path[PATH_MAX], dst[PATH_MAX];
    for (int pass = 0; pass < 2; pass++, rewinddir(d)) {
    if (pass == 1)
        g_lib_sig = lib_signature(root); /* games moved in pass 0 count */
    while ((e = readdir(d))) {
        size_t n = strlen(e->d_name);
        if (n < 5 || strcasecmp(e->d_name + n - 4, ".pkg"))
            continue;
        snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
        struct stat rs;
        if (!stat(path, &rs) && p4m_restored_has(RESTORED_LIST, drive_rel(root, path), (uint64_t)rs.st_size)) {
            char key[PATH_MAX + 4]; /* its own key: the real reason shows once the pkg is taken up again */
            snprintf(key, sizeof(key), "%s#r", path);
            if (first_time(key))
                printf("[scan] skip %s (moved back from ps4/ on request)\n", path);
            continue;
        }
        waiting_t *w = waiting_find(path);
        if (w && w->sig == g_lib_sig)
            continue; /* waiting add-on, and no new game since the last check */
        pkg_t pkg;
        if (p4m_pkg_open(&pkg, path)) {
            if (pass == 0 && first_time(path))
                printf("[scan] skip %s (cannot read the pkg header: not a PS4 pkg, or unreadable)\n", path);
            continue;
        }
        sfo_t sfo;
        const sfo_item_t *cat = NULL;
        if (!read_sfo(&pkg, &sfo))
            cat = p4m_sfo_get(&sfo, "CATEGORY");
        int is_app = cat && !strcmp(cat->sval, "gd"), is_patch = cat && !strcmp(cat->sval, "gp");
        int is_dlc = cat && !strcmp(cat->sval, "ac");
        if (is_dlc || pass == 1) {
            /* add-ons go in the second pass: their game may be in this folder too */
            if (pass == 1 && is_dlc && strncmp(pkg.title_id, "CUSA", 4)) {
                if (first_time(path))
                    printf("[scan] skip %s (add-on %s is not for a PS4 title)\n", path, pkg.content_id);
            } else if (pass == 1 && is_dlc) {
                if (!g_cfg.dlc) {
                    if (first_time(path))
                        printf("[scan] skip %s (add-ons are turned off)\n", path);
                } else if (p4m_pkg_fake_key(&pkg))
                    moved += adopt_dlc(root, e->d_name, path, &pkg);
                else if (first_time(path))
                    printf("[scan] skip %s (not a fake add-on pkg)\n", path);
            }
            p4m_pkg_close(&pkg);
            continue;
        }
        int ok = (is_app || is_patch) && !strncmp(pkg.title_id, "CUSA", 4) && p4m_pkg_fake_key(&pkg);
        char tid[10], ver[16] = "";
        uint8_t digest[32];
        snprintf(tid, sizeof(tid), "%s", pkg.title_id);
        memcpy(digest, pkg.digest, sizeof(digest));
        const sfo_item_t *av = ok ? p4m_sfo_get(&sfo, "APP_VER") : NULL;
        if (av && !av->is_int)
            snprintf(ver, sizeof(ver), "%s", av->sval);
        p4m_pkg_close(&pkg);
        if (!ok) {
            if (first_time(path))
                printf("[scan] skip %s (not a fake PS4 game or update pkg)\n", path);
            continue;
        }
        if (installed_internally(tid)) {
            if (first_time(path))
                printf("[scan] skip %s (%s is installed internally)\n", path, tid);
            continue;
        }
        snprintf(dst, sizeof(dst), "%s/ps4/%s", root, tid);
        mkdirs(dst, 0777);
        snprintf(dst, sizeof(dst), "%s/ps4/%s/%s", root, tid, is_app ? "app.pkg" : "patch.pkg");
        if (exists(dst)) {
            pkg_t cur;
            int same = !p4m_pkg_open(&cur, dst) && !memcmp(cur.digest, digest, sizeof(digest));
            if (cur.fd > 0)
                p4m_pkg_close(&cur);
            if (same || is_app) {
                /* duplicates, and a second copy of a game, stay where they are */
                if (first_time(path))
                    notify(same ? "%s wird schon für %s benutzt; die Kopie blieb im Hauptordner des USB-Laufwerks"
                                : "%s: %s hat schon ein Spielpaket; das neue blieb im Hauptordner des USB-Laufwerks",
                           e->d_name, tid);
                continue;
            }
            /* a different update: it replaces the current one, which is kept aside */
            char old[PATH_MAX];
            snprintf(old, sizeof(old), "%s/ps4/%s/replaced", root, tid);
            mkdirs(old, 0777);
            long now = (long)time(NULL);
            struct stat taken;
            snprintf(old, sizeof(old), "%s/ps4/%s/replaced/patch-%ld.pkg", root, tid, now);
            for (int k = 2; !stat(old, &taken) && k < 100; k++) /* (app) rename() would replace it (PS4Mount PR #18) */
                snprintf(old, sizeof(old), "%s/ps4/%s/replaced/patch-%ld-%d.pkg", root, tid, now, k);
            if (rename(dst, old)) {
                printf("[scan] keep %s aside: %s\n", dst, strerror(errno));
                continue;
            }
            /* the update kept aside keeps its original name */
            char game[PATH_MAX], was[PATH_MAX];
            snprintf(game, sizeof(game), "%s/ps4/%s", root, tid);
            if (!p4m_origin_find(game, "patch.pkg", was, sizeof(was)))
                p4m_origin_add(game, old + strlen(game) + 1, was);
            notify("Neues Update für %s (v%s): Das bisherige liegt jetzt in ps4/%s/replaced", tid, ver, tid);
        }
        if (rename(path, dst)) {
            printf("[scan] move %s: %s\n", path, strerror(errno));
        } else {
            printf("[scan] moved %s -> %s\n", path, dst);
            record_origin(root, tid, is_app ? "app.pkg" : "patch.pkg", path);
            moved++;
        }
    }
    }
    closedir(d);
    return moved;
}

typedef struct {
    char tid[10], name[128];
    int state, ndlc, has_patch;
} title_t;

/* A ps4/ folder or a .pkg in the root. Other partitions (like the boot partition
 * Ventoy adds to a drive) are skipped without a notification. */
static void scan_dir(char *out, size_t n, const char *root, int i) {
    if (g_cfg.paths[i][0])
        snprintf(out, n, "%s/%s", root, g_cfg.paths[i]);
    else
        snprintf(out, n, "%s", root);
}

static int has_pkg(const char *dir) {
    DIR *d = opendir(dir);
    if (!d)
        return 0;
    struct dirent *e;
    int found = 0;
    while (!found && (e = readdir(d))) {
        size_t n = strlen(e->d_name);
        found = n > 4 && !strcasecmp(e->d_name + n - 4, ".pkg");
    }
    closedir(d);
    return found;
}

/* Titles that failed to be added wait before the next try, and the failure is
 * notified once: when the system refuses registrations, every scan would retry. */
#define RETRY_SECONDS 300

typedef struct {
    char tid[10];
    time_t until;
} retry_t;

static retry_t g_retry[MAX_TITLES];

static retry_t *retry_entry(const char *tid, int create) {
    retry_t *free_slot = NULL;
    for (int i = 0; i < MAX_TITLES; i++) {
        if (!strcmp(g_retry[i].tid, tid))
            return &g_retry[i];
        if (!free_slot && !g_retry[i].tid[0])
            free_slot = &g_retry[i];
    }
    if (create && free_slot)
        snprintf(free_slot->tid, sizeof(free_slot->tid), "%s", tid);
    return create ? free_slot : NULL;
}

static int retry_waiting(const char *tid) {
    retry_t *r = retry_entry(tid, 0);
    return r && time(NULL) < r->until;
}

/* Returns 1 the first time a title fails (to notify it), 0 for repeated failures. */
static int retry_failed(const char *tid) {
    retry_t *r = retry_entry(tid, 1);
    if (!r)
        return 1;
    int first = !r->until;
    r->until = time(NULL) + RETRY_SECONDS;
    printf("[reg] %s: next try in %d seconds\n", tid, RETRY_SECONDS);
    return first;
}

static void retry_clear(const char *tid) {
    retry_t *r = retry_entry(tid, 0);
    if (r)
        memset(r, 0, sizeof(*r));
}

static int has_candidates(const char *root) {
    char p[PATH_MAX];
    snprintf(p, sizeof(p), "%s/ps4", root);
    if (exists(p))
        return 1;
    for (int i = 0; i < g_cfg.npaths; i++) {
        scan_dir(p, sizeof(p), root, i);
        if (has_pkg(p))
            return 1;
    }
    return 0;
}

static int adopt_root_pkgs(const char *root) {
    char dir[PATH_MAX];
    int moved = 0;
    g_dlc_orphans = g_dlc_dupes = 0;
    g_lib_sig = lib_signature(root);
    for (int i = 0; i < g_cfg.npaths; i++) {
        scan_dir(dir, sizeof(dir), root, i);
        moved += adopt_dir_pkgs(root, dir);
    }
    if (g_dlc_orphans)
        notify("%s: %d Zusatzinhalt(e) warten auf ihr Spiel, das nicht auf diesem Laufwerk liegt (siehe Protokoll)", root + 5,
               waiting_count(root));
    if (g_dlc_dupes)
        notify("%s: %d Kopie(n) von Zusatzinhalten blieben liegen, weil das Spiel sie schon hat", root + 5, g_dlc_dupes);
    return moved;
}

/* Add-ons deleted on the console come back with their game: drive reconnected or
 * "Add to library" in the panel. */
static void clear_removed_dlc(void) {
    DIR *d = opendir(STATE_DIR);
    if (!d)
        return;
    struct dirent *e;
    char p[PATH_MAX];
    while ((e = readdir(d)))
        if (strlen(e->d_name) == 36 + 8 && !strcmp(e->d_name + 36, ".removed")) {
            snprintf(p, sizeof(p), STATE_DIR "/%s", e->d_name);
            unlink(p);
        }
    closedir(d);
}

static void scan_root(const char *root, int fresh, int reconnected) {
    const char *label = root + 5; /* "usb0" / "ext0" */
    if (!has_candidates(root)) {
        if (fresh)
            printf("[scan] %s: no ps4 folder or pkg, skipped\n", root);
        return;
    }
    if (fresh)
        notify("%s wird nach PS4-Spielen abgesucht ...", label);
    if (reconnected)
        clear_removed_dlc();
    int moved = adopt_root_pkgs(root);
    if (moved && !fresh)
        notify("%s: %d neue Pakete gefunden", label, moved);
    char lib[PATH_MAX];
    snprintf(lib, sizeof(lib), "%s/ps4", root);
    DIR *d = opendir(lib);
    if (!d) {
        if (fresh)
            notify("%s: Keine PS4-Spiele gefunden", label);
        return;
    }
    static title_t titles[MAX_TITLES];
    int count = 0, pending = 0;
    struct dirent *e;
    while ((e = readdir(d)) && count < MAX_TITLES) {
        if (strlen(e->d_name) != 9 || strncmp(e->d_name, "CUSA", 4))
            continue;
        char pkg_path[PATH_MAX];
        snprintf(pkg_path, sizeof(pkg_path), "%s/%s/app.pkg", lib, e->d_name);
        if (!exists(pkg_path))
            continue;
        title_src_t src;
        if (src_open(&src, root, e->d_name))
            continue;
        if (reconnected) {
            char rm[64];
            snprintf(rm, sizeof(rm), STATE_DIR "/%s.removed", e->d_name);
            unlink(rm); /* reconnecting the USB drive brings removed games back */
        }
        title_t *t = &titles[count++];
        snprintf(t->tid, sizeof(t->tid), "%s", e->d_name);
        snprintf(t->name, sizeof(t->name), "%s", src.name);
        t->ndlc = src.ndlc;
        t->has_patch = src.has_patch;
        t->state = title_state(&src);
        if (t->state == 1 && retry_waiting(t->tid))
            t->state = -1; /* failed recently: tried again later */
        pending += t->state == 1;
        src_close(&src);
    }
    closedir(d);
    if (fresh && !count)
        notify("%s: Keine PS4-Spiele gefunden", label);
    else if (fresh)
        notify("%s: %d PS4-Spiele gefunden, %d neu oder aktualisiert", label, count, pending);
    else if (pending)
        notify("%s: %d neue oder aktualisierte PS4-Spiele", label, pending);
    /* games already in the library: keep them mounted */
    for (int i = 0; i < count; i++) {
        title_t *t = &titles[i];
        title_src_t src;
        if (t->state != 0 || src_open(&src, root, t->tid))
            continue;
        enforce_patch_sfo(&src);
        manage(&src);
        managed_t *m = managed_by(t->tid, NULL);
        int running = title_running(t->tid);
        if (m && running && !m->running) {
            m->running = 1; /* open before this daemon started (restart or crash) */
        } else if (m && !running && m->running && time(NULL) - m->running_since > 60) {
            m->running = 0; /* closed without a klog end event */
        }
        if (mount_title(&src))
            notify("%s (%s) ließ sich nicht einbinden", src.name, t->tid);
        src_close(&src);
    }
    if (!pending)
        return;

    /* New or changed games, in groups: one AppInstallAll per group and a quiet
     * period between groups, so the system is never flooded with registrations. */
    int size = g_cfg.batch && g_cfg.batch_size > 0 ? g_cfg.batch_size : pending;
    int groups = (pending + size - 1) / size, grouped = groups > 1, done = 0, ok = 0, group = 0;
    if (grouped)
        notify("%s: %d PS4-Spiele werden in %d Gruppen zu je höchstens %d hinzugefügt", label, pending, groups, size);
    int *members = malloc(sizeof(int) * (size_t)size);
    char (*ids)[10] = malloc(10 * (size_t)size);
    int *prepared = malloc(sizeof(int) * (size_t)size);
    if (!members || !ids || !prepared) {
        free(members);
        free(ids);
        free(prepared);
        return;
    }
    for (int i = 0; i < count;) {
        int n = 0;
        for (; i < count && n < size; i++)
            if (titles[i].state == 1)
                members[n++] = i;
        if (!n)
            break;
        if (group++)
            wait_db_quiet(BATCH_QUIET, 120);
        if (grouped) {
            buf_t names = {0};
            for (int k = 0; k < n; k++)
                p4m_buf_printf(&names, "%s%s", k ? ", " : "", titles[members[k]].name);
            notify("Gruppe %d/%d, hinzugefügt werden %d-%d von %d: %.400s", group, groups, done + 1, done + n, pending,
                   names.data ? (const char *)names.data : "");
            p4m_buf_free(&names);
        }
        int np = 0;
        for (int k = 0; k < n; k++) {
            title_t *t = &titles[members[k]];
            title_src_t src;
            prepared[k] = 0;
            if (!grouped) {
                char extra[64] = "";
                if (t->ndlc)
                    snprintf(extra, sizeof(extra), " + %d add-on%s", t->ndlc, t->ndlc > 1 ? "s" : "");
                notify("Hinzufügen %d/%d: %s%s%s (%s)", done + k + 1, pending, t->name,
                       t->has_patch ? " mit Update" : "", extra, t->tid);
            }
            if (src_open(&src, root, t->tid) || register_prepare(&src)) {
                if (retry_failed(t->tid))
                    notify("%s (%s) ließ sich nicht hinzufügen, es wird alle paar Minuten neu versucht", t->name, t->tid);
            } else {
                prepared[k] = 1;
                snprintf(ids[np++], 10, "%s", t->tid);
            }
            if (src.app.fd > 0)
                src_close(&src);
        }
        install_all(ids, np);
        /* own rows only after ShellCore stopped writing to its databases */
        wait_db_quiet(3, 30);
        for (int k = 0; k < n; k++) {
            title_t *t = &titles[members[k]];
            title_src_t src;
            if (!prepared[k] || src_open(&src, root, t->tid))
                continue;
            if (register_finish(&src)) {
                if (retry_failed(t->tid))
                    notify("%s (%s) ließ sich nicht hinzufügen, es wird alle paar Minuten neu versucht", t->name, t->tid);
            } else {
                retry_clear(t->tid);
                ok++;
                if (!grouped)
                    notify("%s ist spielbereit", t->name);
            }
            src_close(&src);
        }
        done += n;
    }
    free(members);
    free(ids);
    free(prepared);
    if (pending > 1)
        notify("%s: %d von %d Spielen hinzugefügt%s", label, ok, pending, ok == pending ? ", spielbereit" : "");
}

/* Releases mounts over /user/app/<TID> of older versions, which mounted the drive
 * folder itself, once that folder is gone. The internal folders mounted now stay
 * while the drive is away (rest mode powers drives off): their pkg links simply
 * point to nothing until the drive is back. */
static void release_stale(void) {
    DIR *d = opendir(STATE_DIR);
    if (!d)
        return;
    struct dirent *e;
    while ((e = readdir(d))) {
        size_t n = strlen(e->d_name);
        if (n != 13 || strcmp(e->d_name + 9, ".src"))
            continue;
        char tid[10], target[64], from[PATH_MAX], pkg_path[PATH_MAX + 16];
        snprintf(tid, sizeof(tid), "%.9s", e->d_name);
        snprintf(target, sizeof(target), "/user/app/%s", tid);
        if (!mounted_from(target, from, sizeof(from)) || strncmp(from, "/mnt/", 5))
            continue;
        snprintf(pkg_path, sizeof(pkg_path), "%s/app.pkg", from);
        if (!exists(pkg_path)) {
            printf("[scan] releasing %s (source gone)\n", target);
            managed_t *m = managed_by(tid, NULL);
            if (m) {
                unmount_title(tid, m->dlc_tids, m->ndlc_tids);
                m->used = 0;
            } else {
                unmount_title(tid, NULL, 0);
            }
        }
    }
    closedir(d);
}

static void scan_all(void) {
    static int seen[10], first = 1;
    release_stale();
    char root[32];
    for (int i = 0; i < 10; i++) {
        if (i < 8)
            snprintf(root, sizeof(root), "/mnt/usb%d", i);
        else
            snprintf(root, sizeof(root), "/mnt/ext%d", i - 8);
        int present = is_mountpoint(root);
        if (present)
            scan_root(root, !seen[i] || g_manual_scan, !seen[i] && !first);
        seen[i] = present;
    }
    first = 0;
    g_manual_scan = 0;
}


/* --- klog events --- */

static const char *after(const char *line, const char *tag) {
    const char *p = strstr(line, tag);
    return p ? p + strlen(tag) : NULL;
}

static void end_delete_window(void) {
    g_deleting = 0;
    g_deleting_close_at = 0;
    for (int i = 0; i < MAX_TITLES; i++)
        if (g_managed[i].used)
            mount_paths(g_managed[i].tid, g_managed[i].has_patch, g_managed[i].dlc_tids, g_managed[i].ndlc_tids);
    printf("[delete] window closed, game folders mounted again\n");
}

/* Screens of the two delete flows: the home screen (Options > Delete) and
 * Settings > Storage. From the first of them until focus leaves both flows the game
 * folders are unmounted, so a delete removes /user/app/<TID> cleanly (a mount point
 * cannot be removed). Returns 2 for a screen that opens the window, 1 for a screen
 * that keeps it open (menus and dialogs on the way), 0 otherwise. */
static int delete_scene(const char *name) {
    static const char *opens[] = {"AppDeleteConfirmScene", "console storage delete content", "DeleteDialogScreen", NULL};
    static const char *keeps[] = {"AppDeletingScene", "ReactPopupMenu", "Noname", "Render.NPXS40021",
                                  "storage management", NULL};
    for (int i = 0; opens[i]; i++)
        if (strstr(name, opens[i]))
            return 2;
    for (int i = 0; keeps[i]; i++)
        if (strstr(name, keeps[i]))
            return 1;
    return 0;
}

static void start_delete_window(void) {
    g_deleting_since = time(NULL);
    g_deleting_close_at = 0;
    if (g_deleting)
        return;
    g_deleting = 1;
    for (int i = 0; i < MAX_TITLES; i++) {
        managed_t *m = &g_managed[i];
        if (!m->used)
            continue;
        if (m->running || title_running(m->tid))
            continue; /* still open: unmounted when it closes (see the End event) */
        unmount_title(m->tid, m->dlc_tids, m->ndlc_tids);
    }
    printf("[delete] delete screen open, game folders unmounted\n");
}

/* Copies klog lines about our titles (and crash/mount errors) into the log. */
static void trace_klog(const char *line) {
    static const char *keys[] = {"CRASH", "Trophy:Register", "_pfs_mount", "mountpfs", "PrepareProcessLaunch",
                                 "preLaunchCheck", "scePlayGoCore", "process_mount", "process_check", "sqlite",
                                 "SQLITE", "corrupt", "Corrupt", "CORRUPT", "dbr", "DBR", NULL};
    for (int i = 0; keys[i]; i++)
        if (strstr(line, keys[i])) {
            printf("[klog] %.300s\n", line);
            return;
        }
    for (int i = 0; i < MAX_TITLES; i++)
        if (g_managed[i].used && strstr(line, g_managed[i].tid) && !strstr(line, "BGLayer") && !strstr(line, "PSM.UI")) {
            printf("[klog] %.300s\n", line);
            return;
        }
}

/* "system file disagree [<TID>]": the system refused to start a game with an update.
 * Removing the game from the library and adding it again made it start; this is done
 * once per title and daemon run, so a game that still fails is not removed again. */
static char g_readd_tid[10];
static char g_readd_done[MAX_TITLES][10];
static int g_nreadd_done;

static void disagree_seen(const char *tid) {
    managed_t *m = managed_by(tid, NULL);
    if (!m)
        return;
    m->running = 0; /* the launch was refused */
    for (int i = 0; i < g_nreadd_done; i++)
        if (!strcmp(g_readd_done[i], tid)) {
            printf("[reg] %s: system file disagree again, it was already added again once\n", tid);
            return;
        }
    if (m->has_patch && !g_readd_tid[0])
        snprintf(g_readd_tid, sizeof(g_readd_tid), "%s", tid);
}

static void handle_klog_line(const char *line) {
    const char *p;
    char id[40];
    trace_klog(line);
    if ((p = after(line, "OnFocusActiveSceneChanged ["))) {
        /* "[from : type] -> [to : type]": follow the delete screens */
        const char *to = strstr(p, "] -> [");
        if (g_deleting)
            printf("[delete] screen %.200s\n", p);
        if (to) {
            to += 6;
            int kind = delete_scene(to);
            if (kind == 2 || (kind == 1 && g_deleting))
                start_delete_window();
            else if (!kind && g_deleting)
                end_delete_window();
        }
    } else if ((p = after(line, "launchApp("))) {
        snprintf(id, sizeof(id), "%.9s", p);
        managed_t *m = managed_by(id, NULL);
        if (m) {
            m->running = 1;
            m->running_since = time(NULL);
        }
    } else if ((p = after(line, "system file disagree ["))) {
        snprintf(id, sizeof(id), "%.9s", p);
        disagree_seen(id);
    } else if ((p = after(line, "[PlayGoService][End] end ("))) {
        snprintf(id, sizeof(id), "%.9s", p);
        managed_t *m = managed_by(id, NULL);
        if (m) {
            m->running = 0;
            if (g_deleting) {
                /* closed by a delete: take the folder away before the uninstall runs */
                unmount_title(m->tid, m->dlc_tids, m->ndlc_tids);
                printf("[delete] %s closed, game folder unmounted\n", m->tid);
            }
        }
    } else if ((p = after(line, "[PlayGoCore][Uninstall] end ("))) {
        snprintf(id, sizeof(id), "%.36s", p);
        managed_t *m = managed_by(NULL, id);
        if (m) {
            char path[64];
            snprintf(path, sizeof(path), STATE_DIR "/%s.removed", m->tid);
            write_file(path, m->dir, strlen(m->dir), 0666);
            snprintf(path, sizeof(path), STATE_DIR "/%s.src", m->tid);
            unlink(path);
            char hold[PATH_MAX];
            hold_path(hold, sizeof(hold), "app", m->tid);
            rm_tree(hold);
            hold_path(hold, sizeof(hold), "patch", m->tid);
            rm_tree(hold);
            notify("%s ist aus der Bibliothek entfernt. Die PKG bleibt auf dem USB-Laufwerk.", m->name);
            m->used = 0;
        } else if (strlen(id) == 36 && g_deleting) {
            /* an add-on of one of our games deleted on a delete screen: keep it out until the
             * drive is reconnected. Outside those screens the uninstall is the system's own:
             * registering a game again uninstalls its add-ons' PlayGo data, and taking that
             * for a delete removed the add-ons of the game every time a game was added. */
            for (int i = 0; i < MAX_TITLES; i++) {
                managed_t *g = &g_managed[i];
                for (int j = 0; g->used && j < g->ndlc_tids; j++) {
                    if (strncmp(id + 7, g->dlc_tids[j], 9))
                        continue;
                    char path[96], hold[PATH_MAX];
                    snprintf(path, sizeof(path), STATE_DIR "/%s.removed", id);
                    write_file(path, id, strlen(id), 0666);
                    snprintf(hold, sizeof(hold), HOLD_DIR "/addcont/%.9s/%s", id + 7, id + 20);
                    rm_tree(hold);
                    printf("[delete] add-on %s removed\n", id);
                    i = MAX_TITLES;
                    break;
                }
            }
        }
        /* the delete is done: mount the other games again shortly after */
        if (g_deleting)
            g_deleting_close_at = time(NULL) + 3;
    }
}


/* --- list, actions and settings for the page --- */

static time_t g_next_scan;
static int g_stop;

static void json_str(buf_t *b, const char *s) {
    p4m_buf_put(b, "\"", 1);
    for (; *s; s++) {
        if (*s == '"' || *s == '\\')
            p4m_buf_put(b, "\\", 1);
        if ((unsigned char)*s >= 0x20)
            p4m_buf_put(b, s, 1);
    }
    p4m_buf_put(b, "\"", 1);
}

/* Finds the library folder of tid on any connected storage. */
static int find_root(const char *tid, char *root, size_t n) {
    for (int i = 0; i < 10; i++) {
        char r[32], p[PATH_MAX];
        if (i < 8)
            snprintf(r, sizeof(r), "/mnt/usb%d", i);
        else
            snprintf(r, sizeof(r), "/mnt/ext%d", i - 8);
        snprintf(p, sizeof(p), "%s/ps4/%s/app.pkg", r, tid);
        if (is_mountpoint(r) && exists(p)) {
            snprintf(root, n, "%s", r);
            return 0;
        }
    }
    return -1;
}

void ps4m_games_json(buf_t *out) {
    p4m_buf_printf(out, "{\"version\":");
    json_str(out, VERSION);
    p4m_buf_printf(out, ",\"games\":[");
    int first = 1;
    for (int i = 0; i < 10; i++) {
        char root[32], lib[PATH_MAX];
        if (i < 8)
            snprintf(root, sizeof(root), "/mnt/usb%d", i);
        else
            snprintf(root, sizeof(root), "/mnt/ext%d", i - 8);
        if (!is_mountpoint(root))
            continue;
        snprintf(lib, sizeof(lib), "%s/ps4", root);
        DIR *d = opendir(lib);
        if (!d)
            continue;
        struct dirent *e;
        while ((e = readdir(d))) {
            if (strlen(e->d_name) != 9 || strncmp(e->d_name, "CUSA", 4))
                continue;
            title_src_t t;
            char pkg_path[PATH_MAX];
            snprintf(pkg_path, sizeof(pkg_path), "%s/%s/app.pkg", lib, e->d_name);
            if (!exists(pkg_path) || src_open(&t, root, e->d_name))
                continue;
            const char *tid = e->d_name, *state, *mount = "";
            char target[64], from[PATH_MAX];
            snprintf(target, sizeof(target), "/user/app/%s", tid);
            int mounted = mounted_from(target, from, sizeof(from));
            if (mounted) {
                struct statfs sf;
                statfs(target, &sf);
                mount = sf.f_flags & MNT_RDONLY ? "ro" : "rw";
            }
            managed_t *m = managed_by(tid, NULL);
            if (installed_internally(tid))
                state = "internal";
            else if (served_elsewhere(&t))
                state = "duplicate";
            else if (removed(tid))
                state = "removed";
            else if (m && m->running)
                state = "running";
            else if (mounted && p4m_db_is_registered(tid))
                state = "ready";
            else
                state = "pending";
            p4m_buf_printf(out, "%s{\"title_id\":\"%s\",\"name\":", first ? "" : ",", tid);
            json_str(out, t.name);
            p4m_buf_printf(out, ",\"version\":");
            json_str(out, t.ver);
            p4m_buf_printf(out, ",\"has_patch\":%s,\"size\":%llu,\"state\":\"%s\",\"mount\":\"%s\",\"source\":",
                       t.has_patch ? "true" : "false",
                       (unsigned long long)(t.app.pkg_size + (t.has_patch ? t.patch.pkg_size : 0)), state, mount);
            json_str(out, t.dir);
            p4m_buf_printf(out, ",\"dlc\":%d,\"update_issue\":", t.ndlc);
            json_str(out, t.update_issue);
            p4m_buf_printf(out, "}");
            first = 0;
            src_close(&t);
        }
        closedir(d);
    }
    p4m_buf_printf(out, "]}");
}

int ps4m_icon(const char *tid, buf_t *out) {
    char p[PATH_MAX], root[32];
    snprintf(p, sizeof(p), "/user/appmeta/%s/icon0.png", tid);
    int fd = open(p, O_RDONLY);
    if (fd >= 0) {
        char tmp[65536];
        ssize_t n;
        while ((n = read(fd, tmp, sizeof(tmp))) > 0)
            p4m_buf_put(out, tmp, (size_t)n);
        close(fd);
        return out->len ? 0 : -1;
    }
    if (find_root(tid, root, sizeof(root)))
        return -1;
    snprintf(p, sizeof(p), "%s/ps4/%s/app.pkg", root, tid);
    pkg_t pkg;
    if (p4m_pkg_open(&pkg, p))
        return -1;
    size_t len;
    uint8_t *png = p4m_pkg_entry(&pkg, 0x1200, &len);
    p4m_pkg_close(&pkg);
    if (!png)
        return -1;
    p4m_buf_put(out, png, len);
    free(png);
    return 0;
}

int ps4m_add(const char *tid, char *msg, size_t cap) {
    char rm[64], root[32];
    if (find_root(tid, root, sizeof(root))) {
        snprintf(msg, cap, "%s liegt auf keinem angeschlossenen USB-Laufwerk", tid);
        return -1;
    }
    snprintf(rm, sizeof(rm), STATE_DIR "/%s.removed", tid);
    unlink(rm);
    clear_removed_dlc();
    g_next_scan = 0;
    snprintf(msg, cap, "%s wird zur Bibliothek hinzugefügt", tid);
    return 0;
}

/* sceAppInstUtilAppUnInstall in its own thread, waited for at most UNINSTALL_WAIT seconds:
 * a call that never returns (as InstallAll did after rest mode) must not freeze PS4Mount.
 * The wait does not serve the panel (this also runs from a panel request). */
#define UNINSTALL_WAIT 60

static volatile int g_uninstall_busy, g_uninstall_rc;
static char g_uninstall_tid[10];

static void *uninstall_thread(void *arg) {
    (void)arg;
    g_uninstall_rc = sceAppInstUtilAppUnInstall ? sceAppInstUtilAppUnInstall(g_uninstall_tid) : -1; /* (app) weak */
    g_uninstall_busy = 0;
    return NULL;
}

/* 0 or the system error; -2 while an earlier call is still running or this one timed out. */
static int uninstall_title(const char *tid) {
    if (g_uninstall_busy) {
        printf("[remove] %s: an earlier uninstall (%s) is still running\n", tid, g_uninstall_tid);
        return -2;
    }
    snprintf(g_uninstall_tid, sizeof(g_uninstall_tid), "%s", tid);
    g_uninstall_busy = 1;
    pthread_t th;
    if (pthread_create(&th, NULL, uninstall_thread, NULL)) {
        uninstall_thread(NULL);
        return g_uninstall_rc;
    }
    pthread_detach(th);
    for (int i = 0; i < UNINSTALL_WAIT * 10 && g_uninstall_busy; i++)
        usleep(100000);
    if (g_uninstall_busy) {
        printf("[remove] %s: uninstall did not return within %d s\n", tid, UNINSTALL_WAIT);
        return -2;
    }
    return g_uninstall_rc;
}

int ps4m_remove(const char *tid, char *msg, size_t cap) {
    managed_t *m = managed_by(tid, NULL);
    if (installed_internally(tid)) { /* (app) a game installed on the console is not PS4Mount's to remove */
        printf("[web] remove %s refused: installed on the console\n", tid);
        snprintf(msg, cap, "%s ist fest auf der Konsole installiert; PS4Mount entfernt es nicht", tid);
        return -1;
    }
    if ((m && m->running) || title_running(tid)) {
        printf("[web] remove %s refused: game running\n", tid);
        snprintf(msg, cap, "Bitte das Spiel erst beenden, bevor es entfernt wird");
        return -1;
    }
    if (m)
        unmount_title(tid, m->dlc_tids, m->ndlc_tids);
    else
        unmount_title(tid, NULL, 0);
    char a[64], b[64];
    snprintf(a, sizeof(a), "/user/app/%s", tid);
    snprintf(b, sizeof(b), "/user/patch/%s", tid);
    if (mounted_from(a, NULL, 0) || mounted_from(b, NULL, 0)) {
        /* never let the system uninstall run while the USB folder is reachable */
        snprintf(msg, cap, "%s ließ sich nicht aushängen; es wurde nichts entfernt", tid);
        return -1;
    }
    char path[64];
    snprintf(path, sizeof(path), STATE_DIR "/%s.removed", tid);
    write_file(path, tid, strlen(tid), 0666);
    snprintf(path, sizeof(path), STATE_DIR "/%s.src", tid);
    unlink(path);
    int rc = p4m_db_is_registered(tid) ? uninstall_title(tid) : 0;
    if (m)
        m->used = 0;
    printf("[web] remove %s: uninstall=0x%08x\n", tid, rc);
    if (rc == -2)
        snprintf(msg, cap, "Die Konsole hat %s noch nicht fertig entfernt; bitte gleich noch einmal versuchen", tid);
    else
        snprintf(msg, cap, rc ? "%s ließ sich nicht entfernen (0x%08x)" : "%s ist aus der Bibliothek entfernt. Die PKG bleibt auf dem USB-Laufwerk.",
                 tid, rc);
    return rc ? -1 : 0;
}

/* Removes a game refused with "system file disagree" from the library and adds it again. */
static void readd_title(const char *tid) {
    char msg[256], name[128];
    managed_t *m = managed_by(tid, NULL);
    snprintf(name, sizeof(name), "%s", m ? m->name : tid);
    if (g_nreadd_done < MAX_TITLES)
        snprintf(g_readd_done[g_nreadd_done++], 10, "%s", tid);
    pump_events(5000); /* let the system finish the refused launch */
    notify("%s startete nicht (Systemdatei passt nicht): Das Spiel wird neu in die Bibliothek aufgenommen", name);
    int rc = ps4m_remove(tid, msg, sizeof(msg));
    printf("[reg] %s: removed to add it again: %s\n", tid, msg);
    if (!rc)
        wait_db_quiet(3, 30);
    ps4m_add(tid, msg, sizeof(msg));
}

/* --- moving pkgs back where they came from --- */

static void clean_name(char *s) {
    for (; *s; s++)
        if (strchr("\\/:*?\"<>|", *s) || (unsigned char)*s < 0x20) /* (app) '/' too */
            *s = '_';
}

/* Moves <game_dir>/<rel> to its recorded original path (or fallback, relative to the
 * drive root, for pkgs moved before origins were recorded) and adds it to the list of
 * restored pkgs. 1 moved, 0 nothing there, -1 failed. */
/* (app) A recorded origin from origin.txt on the drive: relative, no "..", no empty part, so the pkg stays on its drive. */
static int origin_path_ok(const char *p) {
    if (!p[0] || p[0] == '/' || strstr(p, "//") || strchr(p, '\\'))
        return 0;
    for (const char *c = p; *c; c = strchr(c, '/') ? strchr(c, '/') + 1 : c + strlen(c))
        if (!strncmp(c, "..", 2) && (c[2] == '/' || !c[2]))
            return 0;
    return 1;
}

static int restore_pkg(const char *root, const char *game_dir, const char *rel, const char *fallback) {
    char src[PATH_MAX], orig[PATH_MAX], dst[PATH_MAX], dir[PATH_MAX];
    snprintf(src, sizeof(src), "%s/%s", game_dir, rel);
    struct stat st;
    if (stat(src, &st))
        return 0;
    if (p4m_origin_find(game_dir, rel, orig, sizeof(orig)) || !origin_path_ok(orig)) {
        snprintf(orig, sizeof(orig), "%s", fallback);
        clean_name(orig);
    }
    snprintf(dir, sizeof(dir), "%s/%s", root, orig);
    char *slash = strrchr(dir, '/');
    if (slash && slash > dir + strlen(root))
        *slash = 0, mkdirs(dir, 0777); /* the original folder may be gone */
    if (p4m_origin_free_path(root, orig, dst, sizeof(dst)) || rename(src, dst)) {
        printf("[restore] %s: %s\n", src, strerror(errno));
        return -1;
    }
    p4m_restored_add(RESTORED_LIST, drive_rel(root, dst), (uint64_t)st.st_size);
    printf("[restore] %s -> %s\n", src, dst);
    return 1;
}

static void unlink_in(const char *dir, const char *const *names) {
    char p[PATH_MAX];
    for (; *names; names++) {
        snprintf(p, sizeof(p), "%s/%s", dir, *names);
        unlink(p);
    }
}

/* Moves every pkg of one game folder back and removes what PS4Mount created for it.
 * Folders are only removed when empty, so nothing else is ever deleted. */
static void restore_files(const char *root, const char *tid, const char *name, int *moved, int *failed) {
    static const char *const generated[] = {"app.json", "app.pbm", "app.pbm.backup", "app.xml", "patch.json",
                                            "patch.pbm", "patch.pbm.backup", "patch.xml", NULL};
    static const char *const ac_generated[] = {"ac.json", "ac.pbm", "ac.pbm.backup", NULL};
    char game[PATH_MAX], rel[PATH_MAX], fb[PATH_MAX], sub[PATH_MAX];
    snprintf(game, sizeof(game), "%s/ps4/%s", root, tid);
    int rc;
#define RESTORE(r, f)                                                                                                  \
    do {                                                                                                               \
        rc = restore_pkg(root, game, r, f);                                                                            \
        *moved += rc > 0;                                                                                              \
        *failed += rc < 0;                                                                                             \
    } while (0)
    snprintf(fb, sizeof(fb), "%s - %s - game.pkg", tid, name);
    RESTORE("app.pkg", fb);
    snprintf(fb, sizeof(fb), "%s - %s - update.pkg", tid, name);
    RESTORE("patch.pkg", fb);
    snprintf(sub, sizeof(sub), "%s/replaced", game);
    DIR *d = opendir(sub);
    struct dirent *e;
    while (d && (e = readdir(d))) {
        if (e->d_name[0] == '.')
            continue;
        snprintf(rel, sizeof(rel), "replaced/%s", e->d_name);
        snprintf(fb, sizeof(fb), "%s - %s - older update %s", tid, name, e->d_name);
        RESTORE(rel, fb);
    }
    if (d)
        closedir(d);
    rmdir(sub);
    snprintf(sub, sizeof(sub), "%s/addcont", game);
    d = opendir(sub);
    while (d && (e = readdir(d))) {
        if (e->d_name[0] == '.')
            continue;
        char tdir[PATH_MAX], hold[PATH_MAX];
        snprintf(tdir, sizeof(tdir), "%s/%s", sub, e->d_name);
        DIR *l = opendir(tdir);
        struct dirent *f;
        while (l && (f = readdir(l))) {
            if (f->d_name[0] == '.')
                continue;
            char ldir[PATH_MAX + 64];
            snprintf(ldir, sizeof(ldir), "%s/%s", tdir, f->d_name);
            snprintf(rel, sizeof(rel), "addcont/%s/%s/ac.pkg", e->d_name, f->d_name);
            snprintf(fb, sizeof(fb), "%s - %s - %s.pkg", e->d_name, name, f->d_name);
            RESTORE(rel, fb);
            unlink_in(ldir, ac_generated);
            rmdir(ldir);
            snprintf(hold, sizeof(hold), HOLD_DIR "/addcont/%s/%s", e->d_name, f->d_name);
            rm_tree(hold);
        }
        if (l)
            closedir(l);
        rmdir(tdir);
        snprintf(hold, sizeof(hold), HOLD_DIR "/addcont/%s", e->d_name);
        rmdir(hold); /* kept while other games still use add-ons of this title */
    }
    if (d)
        closedir(d);
#undef RESTORE
    rmdir(sub);
    unlink_in(game, generated); /* written next to the pkgs by versions before 0.3.2 */
    char marker[64];
    snprintf(marker, sizeof(marker), "%s/" ORIGIN_FILE, game);
    char probe[PATH_MAX];
    snprintf(probe, sizeof(probe), "%s/app.pkg", game);
    if (!exists(probe)) /* keep the record while a pkg could not be moved */
        unlink(marker);
    rmdir(game);
    snprintf(probe, sizeof(probe), HOLD_DIR "/app/%s", tid);
    rm_tree(probe);
    snprintf(probe, sizeof(probe), HOLD_DIR "/patch/%s", tid);
    rm_tree(probe);
}

/* Add-on title IDs of a game folder (its /user/addcont mounts). */
static int folder_dlc_tids(const char *game, char (*tids)[10], int max) {
    char sub[PATH_MAX];
    snprintf(sub, sizeof(sub), "%s/addcont", game);
    DIR *d = opendir(sub);
    int n = 0;
    struct dirent *e;
    while (d && n < max && (e = readdir(d)))
        if (strlen(e->d_name) == 9 && !strncmp(e->d_name, "CUSA", 4))
            snprintf(tids[n++], 10, "%s", e->d_name);
    if (d)
        closedir(d);
    return n;
}

/* Takes one game out of the library (through the system) and unmounts it. */
static int restore_unregister(const char *root, const char *tid) {
    char game[PATH_MAX], dtids[MAX_DLC_TIDS][10];
    managed_t *rm = managed_by(tid, NULL);
    if ((rm && rm->running) || title_running(tid)) { /* (app) checked before anything is unmounted */
        printf("[restore] %s kept: the game is running\n", tid);
        return -1;
    }
    snprintf(game, sizeof(game), "%s/ps4/%s", root, tid);
    int nd = folder_dlc_tids(game, dtids, MAX_DLC_TIDS);
    unmount_title(tid, dtids, nd);
    char msg[256];
    if (!installed_internally(tid) && ps4m_remove(tid, msg, sizeof(msg))) {
        printf("[restore] %s kept: %s\n", tid, msg);
        return -1;
    }
    return 0;
}

static void restore_clear_state(const char *tid) {
    char p[64];
    snprintf(p, sizeof(p), STATE_DIR "/%s.removed", tid);
    unlink(p);
    snprintf(p, sizeof(p), STATE_DIR "/%s.src", tid);
    unlink(p);
    retry_clear(tid);
}

/* Requests from the panel, run by the main loop: some titles, or all of them. */
static char g_restore_tids[MAX_TITLES][10];
static int g_restore_n, g_restore_all;

/* --- closing the open game --- */

typedef struct {
    uint32_t app_id;
    uint64_t unknown1;
    char title_id[10];
    uint32_t app_type;
    char unknown2[0x36];
    char app_spare[160]; /* (app) room: the layout is not known for every firmware */
} app_info_t;

/* (app) weak, and no sceSystemServiceKillApp: games are closed the way "Spiel beenden" does it (procmgr.c, SIGKILL), which
   needs no service call that may be missing or unreliable on some firmware */
int sceKernelGetAppInfo(pid_t pid, app_info_t *info) __attribute__((weak));

/* Open games: processes named eboot.bin (PS4 and PS5 games alike). Fills up to max
 * app ids and title ids, returns how many are open. */
static int open_games(uint32_t *ids, char (*tids)[10], int max) {
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PROC, 0}, n = 0;
    size_t len = 0;
    if (!sceKernelGetAppInfo)
        return -1; /* (app) unknown */
    if (sysctl(mib, 4, NULL, &len, NULL, 0) || !len)
        return -1; /* (app) unknown, not "none open" */
    len += len / 4; /* processes started meanwhile */
    uint8_t *buf = malloc(len);
    if (!buf || sysctl(mib, 4, buf, &len, NULL, 0)) {
        free(buf);
        return -1;
    }
    for (size_t off = 0; off + sizeof(struct kinfo_proc) <= len;) {
        struct kinfo_proc *ki = (struct kinfo_proc *)(buf + off);
        if (ki->ki_structsize < (int)sizeof(*ki) || (size_t)ki->ki_structsize > len - off)
            break;
        app_info_t info;
        memset(&info, 0, sizeof(info));
        if (!strcmp(ki->ki_comm, "eboot.bin") && !sceKernelGetAppInfo(ki->ki_pid, &info) && info.app_id &&
            (!strncmp(info.title_id, "CUSA", 4) || !strncmp(info.title_id, "PPSA", 4))) { /* (app) games only */
            if (n < max) {
                ids[n] = info.app_id;
                snprintf(tids[n], 10, "%.9s", info.title_id);
            }
            n++;
        }
        off += (size_t)ki->ki_structsize;
    }
    free(buf);
    return n;
}

/* Every pkg operation that takes games out of the library closes the open game first,
 * whichever it is: a game left open keeps reading pkgs that are being moved. 0 once no
 * game is open; -1 when one did not close within 30 seconds. */
static int close_open_games(void) {
    uint32_t ids[8];
    char tids[8][10];
    int n = open_games(ids, tids, 8);
    if (n < 0) {
        printf("[restore] the list of processes could not be read\n");
        return -1; /* (app) do not move pkgs a game might still read */
    }
    if (!n)
        return 0;
    for (int i = 0; i < n && i < 8; i++) {
        managed_t *m = managed_by(tids[i], NULL);
        notify("%s wird geschlossen, um die PKGs zurückzuverschieben", m ? m->name : tids[i]);
        int rc = ps5tm_procmgr_close_game(tids[i], 5000); /* (app) SIGKILL, as "Spiel beenden" */
        printf("[restore] close %s (app 0x%08x): %d\n", tids[i], ids[i], rc);
    }
    char closing[8][10];
    int nc = n < 8 ? n : 8;
    memcpy(closing, tids, sizeof(closing[0]) * (size_t)nc);
    for (int s = 0; s < 30; s++) {
        pump_events(1000);
        int mounted = 0;
        for (int i = 0; i < nc; i++)
            mounted |= title_running(closing[i]);
        if (!open_games(ids, tids, 8) && !mounted) {
            pump_events(2000); /* let the system finish unmounting the game */
            for (int i = 0; i < MAX_TITLES; i++)
                g_managed[i].running = 0;
            return 0;
        }
    }
    return -1;
}

/* Folders of games on every connected drive (or only tid), with their name. */
typedef struct {
    char root[32], tid[10], name[128];
} restore_item_t;

/* only: n title ids to keep, or NULL for every game. */
static int restore_collect(char (*only)[10], int nonly, restore_item_t *items, int max) {
    int n = 0;
    for (int i = 0; i < 10 && n < max; i++) {
        char root[32], lib[PATH_MAX];
        if (i < 8)
            snprintf(root, sizeof(root), "/mnt/usb%d", i);
        else
            snprintf(root, sizeof(root), "/mnt/ext%d", i - 8);
        if (!is_mountpoint(root))
            continue;
        snprintf(lib, sizeof(lib), "%s/ps4", root);
        DIR *d = opendir(lib);
        struct dirent *e;
        while (d && n < max && (e = readdir(d))) {
            if (strlen(e->d_name) != 9 || strncmp(e->d_name, "CUSA", 4))
                continue;
            int wanted = !only;
            for (int k = 0; k < nonly && !wanted; k++)
                wanted = !strcmp(only[k], e->d_name);
            if (!wanted)
                continue;
            restore_item_t *it = &items[n++];
            snprintf(it->root, sizeof(it->root), "%s", root);
            snprintf(it->tid, sizeof(it->tid), "%s", e->d_name);
            snprintf(it->name, sizeof(it->name), "%s", e->d_name);
            char p[PATH_MAX];
            snprintf(p, sizeof(p), "%s/%s/app.pkg", lib, e->d_name);
            pkg_t pkg;
            sfo_t sfo;
            if (exists(p) && !p4m_pkg_open(&pkg, p)) {
                const sfo_item_t *t = read_sfo(&pkg, &sfo) ? NULL : p4m_sfo_get(&sfo, "TITLE");
                if (t) {
                    snprintf(it->name, sizeof(it->name), "%s", t->sval);
                    clean_name(it->name);
                }
                p4m_pkg_close(&pkg);
            }
        }
        if (d)
            closedir(d);
    }
    return n;
}

/* Moves the pkgs of the requested games (only, or all when NULL) back, in groups of the
 * restore setting: the games of a group leave the library one at a time, their pkgs are
 * moved, and the next group waits until the system databases are idle. */
static void run_restore(char (*only)[10], int nonly) {
    static restore_item_t items[MAX_TITLES];
    int n = restore_collect(only, nonly, items, MAX_TITLES), moved = 0, failed = 0, done = 0, kept = 0;
    if (!n) {
        notify("Keine PS4Mount-Spiele zum Zurückverschieben");
        return;
    }
    if (close_open_games()) {
        notify("Das laufende Spiel ließ sich nicht schließen; es wurden keine PKGs zurückverschoben");
        return;
    }
    int size = g_cfg.restore_batch;
    if (n == 1)
        notify("Die PKGs von %s werden zurückverschoben ...", items[0].name);
    else
        notify("Die PKGs von %d Spiel(en) werden an ihren alten Ort zurückverschoben, %d auf einmal ...", n, size);
    for (int i = 0; i < n; i += size) {
        if (i)
            wait_db_quiet(BATCH_QUIET, 120);
        int end = i + size < n ? i + size : n;
        for (int k = i; k < end; k++) {
            if (restore_unregister(items[k].root, items[k].tid)) {
                kept++;
                continue;
            }
            /* one removal at a time: the next waits until the system databases are idle */
            wait_db_quiet(3, 30);
            restore_files(items[k].root, items[k].tid, items[k].name, &moved, &failed);
            done++;
        }
    }
    /* the uninstall events arrive late and mark the games as removed: clear that, so a
     * game added again later is not taken for one removed on the console */
    pump_events(3000);
    for (int i = 0; i < n; i++)
        restore_clear_state(items[i].tid);
    clear_removed_dlc();
    g_next_scan = 0;
    if (n == 1 && done)
        notify("%s: %d PKG-Datei(en) zurückverschoben%s", items[0].name, moved,
               failed ? ", einige ließen sich nicht verschieben (siehe Protokoll)" : "");
    else if (n > 1)
        notify("%d PKG-Datei(en) von %d Spiel(en) zurückverschoben%s%s", moved, done,
               failed ? ", einige ließen sich nicht verschieben (siehe Protokoll)" : "",
               kept ? ", einige Spiele ließen sich nicht aus der Bibliothek nehmen (siehe Protokoll)" : "");
}

int ps4m_restore(const char *tids, char *msg, size_t cap) {
    if (g_restore_all || g_restore_n) {
        snprintf(msg, cap, "Es werden schon PKGs zurückverschoben; bitte warten, bis das fertig ist");
        return -1;
    }
    if (!tids) {
        g_restore_all = 1;
        snprintf(msg, cap, "Das laufende Spiel wird geschlossen, dann werden die PKGs aller Spiele an ihren alten Ort zurückverschoben");
        return 0;
    }
    int n = 0;
    for (const char *p = tids; *p;) {
        size_t len = strcspn(p, ",");
        char tid[10], root[32];
        if (len != 9 || strncmp(p, "CUSA", 4) || strspn(p + 4, "0123456789") < 5) {
            snprintf(msg, cap, "Ungültige Titel-ID: %.20s", p);
            return -1;
        }
        snprintf(tid, sizeof(tid), "%.9s", p);
        if (find_root(tid, root, sizeof(root))) {
            snprintf(msg, cap, "%s liegt auf keinem angeschlossenen Laufwerk", tid);
            return -1;
        }
        int dup = 0;
        for (int i = 0; i < n; i++)
            dup |= !strcmp(g_restore_tids[i], tid);
        if (!dup && n < MAX_TITLES)
            snprintf(g_restore_tids[n++], 10, "%s", tid);
        p += len;
        if (*p == ',')
            p++;
    }
    if (!n) {
        snprintf(msg, cap, "Keine Spiele ausgewählt");
        return -1;
    }
    g_restore_n = n;
    if (n == 1)
        snprintf(msg, cap, "Das laufende Spiel wird geschlossen, dann werden die PKGs von %s an ihren alten Ort zurückverschoben",
                 g_restore_tids[0]);
    else
        snprintf(msg, cap, "Das laufende Spiel wird geschlossen, dann werden die PKGs von %d Spielen zurückverschoben, %d auf einmal", n,
                 g_cfg.restore_batch);
    return 0;
}

int ps4m_restored_forget(char *msg, size_t cap) {
    unlink(RESTORED_LIST);
    g_next_scan = 0;
    snprintf(msg, cap, "Zurückverschobene PKGs werden bei der nächsten Suche wieder aufgenommen");
    return 0;
}

/* --- settings --- */

/* A scan folder: relative to the drive, "" for the root. Folders inside ps4/ (where
 * PS4Mount keeps the games) and anything leaving the drive are refused. */
static int clean_scan_path(const char *in, char *out, size_t n, char *why, size_t wn) {
    while (*in == ' ' || *in == '/')
        in++;
    snprintf(out, n, "%s", in);
    size_t l = strlen(out);
    while (l && (out[l - 1] == ' ' || out[l - 1] == '/' || out[l - 1] == '\r'))
        out[--l] = 0;
    if (l >= 120) {
        snprintf(why, wn, "Ordnername zu lang: %.40s ...", out);
        return -1;
    }
    if (strchr(out, '\\') || strchr(out, ':') || strstr(out, "//")) {
        snprintf(why, wn, "Ungültiger Ordner: %s", out);
        return -1;
    }
    for (const char *c = out; *c; c = strchr(c, '/') ? strchr(c, '/') + 1 : c + strlen(c))
        if (!strncmp(c, "..", 2) && (c[2] == '/' || !c[2])) {
            snprintf(why, wn, "Ordner außerhalb des Laufwerks sind nicht erlaubt: %s", out);
            return -1;
        }
    if (!strncasecmp(out, "ps4", 3) && (!out[3] || out[3] == '/')) {
        snprintf(why, wn, "Der Ordner ps4 enthält die schon einsortierten Spiele; bitte einen anderen Ordner wählen");
        return -1;
    }
    return 0;
}

static void config_save(void) {
    buf_t b = {0};
    p4m_buf_printf(&b, "batch=%d\nbatch_size=%d\ndlc=%d\nrestore_batch=%d\nenabled=%d\nfast_reg=%d\n", g_cfg.batch, g_cfg.batch_size, g_cfg.dlc,
               g_cfg.restore_batch, g_enabled, g_cfg.fast_reg);
    for (int i = 0; i < g_cfg.npaths; i++)
        p4m_buf_printf(&b, "path=%s\n", g_cfg.paths[i]);
    if (b.failed) /* (app) PS4Mount PR #18 */
        printf("[config] out of memory: settings not saved\n");
    else
        write_file(CONFIG_PATH, b.data, b.len, 0666);
    p4m_buf_free(&b);
}

static void config_load(void) {
    FILE *f = fopen(CONFIG_PATH, "r");
    if (!f)
        return;
    config_t c = g_cfg;
    c.npaths = 0;
    char line[256], why[160], path[128];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (!strncmp(line, "batch=", 6))
            c.batch = atoi(line + 6) != 0;
        else if (!strncmp(line, "batch_size=", 11))
            c.batch_size = atoi(line + 11);
        else if (!strncmp(line, "dlc=", 4))
            c.dlc = atoi(line + 4) != 0;
        else if (!strncmp(line, "enabled=", 8))
            g_enabled = atoi(line + 8) != 0;
        else if (!strncmp(line, "fast_reg=", 9))
            c.fast_reg = atoi(line + 9) != 0;
        else if (!strncmp(line, "restore_batch=", 14))
            c.restore_batch = atoi(line + 14);
        else if (!strncmp(line, "path=", 5) && c.npaths < MAX_SCAN_PATHS &&
                 !clean_scan_path(line + 5, path, sizeof(path), why, sizeof(why)))
            snprintf(c.paths[c.npaths++], sizeof(c.paths[0]), "%s", path);
    }
    fclose(f);
    if (c.batch_size < 1 || c.batch_size > 50)
        c.batch_size = 5;
    if (c.restore_batch < 1 || c.restore_batch > MAX_RESTORE_BATCH)
        c.restore_batch = 5;
    if (!c.npaths) { /* the root and the folder pkg (the app's addition: many people keep their pkgs there) */
        c.paths[0][0] = 0;
        snprintf(c.paths[1], sizeof(c.paths[1]), "pkg");
        c.npaths = 2;
    }
    g_cfg = c;
    printf("[config] batch=%d batch_size=%d dlc=%d restore_batch=%d fast_reg=%d folders=%d\n", g_cfg.batch, g_cfg.batch_size,
           g_cfg.dlc, g_cfg.restore_batch, g_cfg.fast_reg, g_cfg.npaths);
}

void ps4m_config_json(buf_t *out) {
    p4m_buf_printf(out, "{\"batch\":%s,\"batch_size\":%d,\"dlc\":%s,\"restore_batch\":%d,\"max_restore_batch\":%d,\"fast_reg\":%s,\"paths\":[",
               g_cfg.batch ? "true" : "false", g_cfg.batch_size, g_cfg.dlc ? "true" : "false", g_cfg.restore_batch,
               MAX_RESTORE_BATCH, g_cfg.fast_reg ? "true" : "false");
    for (int i = 0; i < g_cfg.npaths; i++) {
        char shown[130];
        snprintf(shown, sizeof(shown), "/%s", g_cfg.paths[i]);
        p4m_buf_printf(out, "%s", i ? "," : "");
        json_str(out, shown);
    }
    p4m_buf_printf(out, "]}");
}

static void url_decode(const char *in, size_t len, char *out, size_t n) {
    size_t o = 0;
    for (size_t i = 0; i < len && o + 1 < n; i++) {
        if (in[i] == '+')
            out[o++] = ' ';
        else if (in[i] == '%' && i + 2 < len) {
            char hex[3] = {in[i + 1], in[i + 2], 0};
            out[o++] = (char)strtol(hex, NULL, 16);
            i += 2;
        } else
            out[o++] = in[i];
    }
    out[o] = 0;
}

/* body: batch=0|1&batch_size=N&dlc=0|1&restore_batch=N&paths=<folders, one per line> (form
 * encoded); keys left out keep their value. */
int ps4m_config_set(const char *body, char *msg, size_t cap) {
    config_t c = g_cfg;
    char value[1200], why[160] = "";
    for (const char *p = body; p && *p;) {
        const char *amp = strchr(p, '&'), *eq = strchr(p, '=');
        size_t len = amp ? (size_t)(amp - p) : strlen(p);
        if (eq && eq < p + len) {
            url_decode(eq + 1, (size_t)(p + len - eq - 1), value, sizeof(value));
            size_t k = (size_t)(eq - p);
            if (k == 5 && !strncmp(p, "batch", 5))
                c.batch = atoi(value) != 0;
            else if (k == 10 && !strncmp(p, "batch_size", 10))
                c.batch_size = atoi(value);
            else if (k == 3 && !strncmp(p, "dlc", 3))
                c.dlc = atoi(value) != 0;
            else if (k == 8 && !strncmp(p, "fast_reg", 8))
                c.fast_reg = atoi(value) != 0;
            else if (k == 13 && !strncmp(p, "restore_batch", 13)) {
                char *end;
                long v = strtol(value, &end, 10);
                c.restore_batch = *value && !*end ? (int)v : 0;
            } else if (k == 5 && !strncmp(p, "paths", 5)) {
                c.npaths = 0;
                for (char *line = strtok(value, "\n"); line; line = strtok(NULL, "\n")) {
                    char path[128];
                    if (clean_scan_path(line, path, sizeof(path), why, sizeof(why))) {
                        snprintf(msg, cap, "Nicht gespeichert: %s", why);
                        return -1;
                    }
                    int dup = 0;
                    for (int i = 0; i < c.npaths; i++)
                        dup |= !strcmp(c.paths[i], path);
                    if (dup)
                        continue;
                    if (c.npaths == MAX_SCAN_PATHS) {
                        snprintf(msg, cap, "Nicht gespeichert: höchstens %d Ordner", MAX_SCAN_PATHS);
                        return -1;
                    }
                    snprintf(c.paths[c.npaths++], sizeof(c.paths[0]), "%s", path);
                }
            }
        }
        p = amp ? amp + 1 : NULL;
    }
    if (c.batch_size < 1 || c.batch_size > 50) {
        snprintf(msg, cap, "Nicht gespeichert: Spiele pro Gruppe müssen zwischen 1 und 50 liegen");
        return -1;
    }
    if (c.restore_batch < 1 || c.restore_batch > MAX_RESTORE_BATCH) {
        snprintf(msg, cap, "Nicht gespeichert: Spiele pro Gruppe beim Zurückverschieben müssen eine Zahl von 1 bis %d sein",
                 MAX_RESTORE_BATCH);
        return -1;
    }
    if (!c.npaths) {
        snprintf(msg, cap, "Nicht gespeichert: Mindestens ein Ordner nötig (/ ist die oberste Ebene des Laufwerks)");
        return -1;
    }
    g_cfg = c;
    config_save();
    printf("[config] saved: batch=%d batch_size=%d dlc=%d restore_batch=%d fast_reg=%d folders=%d\n", c.batch, c.batch_size, c.dlc,
           c.restore_batch, c.fast_reg, c.npaths);
    g_next_scan = 0;
    snprintf(msg, cap, "Einstellungen gespeichert");
    return 0;
}


/* --- the app's side: lock, klog, worker thread, calls from the page --- */

/* One lock for all state. The worker holds it while it works and lets go of it in pump_events (every wait of the original loop),
 * which is where the stand-alone payload served its web panel; the calls of the page take it there. */
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static volatile int g_alive;          /* the worker thread is running */
static int g_lock_sock = -1;          /* single-instance lock on port 9032 (as the stand-alone PS4Mount) */
static uint64_t g_klog_after;         /* the newest kernel-log line seen */

/* (app) The stand-alone PS4Mount (ps4mount.elf) must never run beside this module: both would register, mount and unmount the
   same games. The lock on 9032 alone is not enough: rest mode closes the sockets of payloads (the stand-alone one's lock too),
   and a payload may be named anything. So any of three signs counts (15.10.2026, wish of the user): its web panel answers on
   port 8770 (the app never listens there), the lock on 9032 is held while this module does not hold it, or a process is named
   like it. While one is seen, the module does not start but waits, and takes over by itself once the stand-alone one is gone. */
#define P4M_PANEL_PORT 8770
static char g_standalone_why[160];    /* why the module waits for the stand-alone one; "" when it does not (under g_mu) */
static volatile int g_sa_waiting;     /* the waiter thread runs */

/* 1 when something accepts a connection on 127.0.0.1:port within 300 ms */
static int port_answers(int port) {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0)
        return 0;
    int fl = fcntl(s, F_GETFL, 0);
    if (fl >= 0)
        fcntl(s, F_SETFL, fl | O_NONBLOCK);
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons((uint16_t)port);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    int ok = 0;
    if (!connect(s, (struct sockaddr *)&a, sizeof(a))) {
        ok = 1;
    } else if (errno == EINPROGRESS) {
        fd_set w;
        FD_ZERO(&w);
        FD_SET(s, &w);
        struct timeval tv = {0, 300 * 1000};
        if (select(s + 1, NULL, &w, NULL, &tv) == 1) {
            int err = 0;
            socklen_t l = sizeof(err);
            ok = !getsockopt(s, SOL_SOCKET, SO_ERROR, &err, &l) && !err;
        }
    }
    close(s);
    return ok;
}

/* "ps4mount" in a process name, any case */
static int name_is_ps4mount(const char *name) {
    char low[sizeof(((ps5tm_process_t *)0)->name)];
    size_t i = 0;
    for (; name[i] && i + 1 < sizeof(low); i++)
        low[i] = (char)((name[i] >= 'A' && name[i] <= 'Z') ? name[i] + 32 : name[i]);
    low[i] = 0;
    return strstr(low, "ps4mount") != NULL;
}

/* 1, with the reason in why, when a stand-alone PS4Mount runs. mine: this module holds the lock on 9032 itself right now (then
   that lock says nothing). Never called with g_mu held: it may wait up to 0.6 s. */
static int standalone_seen(int mine, char *why, size_t n) {
    if (port_answers(P4M_PANEL_PORT)) {
        snprintf(why, n, "Sein Web-Panel antwortet auf Port 8770.");
        return 1;
    }
    if (!mine && port_answers(LOCK_PORT)) {
        snprintf(why, n, "Es hält die Sperre auf Port 9032.");
        return 1;
    }
    ps5tm_process_t procs[96];
    unsigned np = ps5tm_procmgr_list(procs, 96);
    for (unsigned i = 0; i < np; i++) {
        if (procs[i].is_self || !name_is_ps4mount(procs[i].name))
            continue;
        char shown[48];
        size_t k = 0;
        for (const char *c = procs[i].name; *c && k + 1 < sizeof(shown); c++)   /* for the page's JSON: no quotes, no control */
            shown[k++] = (*c == '"' || *c == '\\' || (unsigned char)*c < 0x20) ? '?' : *c;
        shown[k] = 0;
        snprintf(why, n, "Es läuft als Prozess „%s“ (PID %d).", shown, procs[i].pid);
        return 1;
    }
    return 0;
}

int ps4m_standalone_active(char *why, size_t n) {
    return standalone_seen(g_alive, why, n);
}

int ps4m_running(void) {
    return g_alive;
}

static void waiter_start(void);

void ps4m_rescan(void) {
    g_next_scan = 0;
    g_manual_scan = 1;
}

/* Titles listed in app.db, read at most once a second; a read that fails keeps the last list. */
static char (*g_ids)[12];
static int g_nids, g_read_ok;
static time_t g_ids_at;

#define P4M_MAX_IDS 2048

static int p4m_db_is_registered(const char *tid) {
    time_t now = time(NULL);
    if (now != g_ids_at) {                  /* (app) at most once a second, also after a failed read */
        g_ids_at = now;
        if (!g_ids)
            g_ids = malloc(sizeof(*g_ids) * P4M_MAX_IDS);
        if (g_ids) {
            int n = ps5tm_library_installed_ids(g_ids, P4M_MAX_IDS);
            if (n >= 0) {
                g_nids = n < P4M_MAX_IDS ? n : P4M_MAX_IDS;
                g_read_ok = 1;
            }
        }
    }
    /* (app) never read: "unknown" counts as registered, so that nothing is registered again with InstallAll (which would
       also take along whatever else waits under /user/app) while the database cannot be read */
    if (!g_read_ok)
        return 1;
    for (int i = 0; i < g_nids; i++)
        if (!strcmp(g_ids[i], tid))
            return 1;
    return 0;
}

static int instance_lock(void) {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons(LOCK_PORT);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (s < 0 || bind(s, (struct sockaddr *)&a, sizeof(a)) || listen(s, 1)) {
        if (s >= 0)
            close(s);
        return -1;
    }
    return s;
}

/* Rest mode closes the network sockets of payloads: a lock that stopped listening is found here and opened again. */
static int still_listening(int s) {
    int on = 0;
    socklen_t n = sizeof(on);
    return s >= 0 && !getsockopt(s, SOL_SOCKET, SO_ACCEPTCONN, &on, &n) && on;
}

/* The kernel-log lines since the last look, handled with the lock held. */
static void klog_poll(void) {
    static ps5tm_klog_line_t lines[64];
    for (int round = 0; round < 8; round++) {
        uint64_t newest = 0;
        int more = 0, lost = 0;
        pthread_mutex_unlock(&g_mu);
        unsigned n = ps5tm_klog_fetch(g_klog_after, 64, lines, &newest, &more, &lost);
        pthread_mutex_lock(&g_mu);
        for (unsigned i = 0; i < n; i++) {
            g_klog_after = lines[i].seq;
            if (!lines[i].marker)
                handle_klog_line(lines[i].text);
        }
        if (!n || !more)
            break;
    }
}

/* Waits ms milliseconds and follows the kernel log meanwhile; the lock is free while waiting. */
static void pump_events(int ms) {
    pthread_mutex_unlock(&g_mu);
    for (int waited = 0; waited < ms; waited += 250) {
        usleep(250 * 1000);
        pthread_mutex_lock(&g_mu);
        klog_poll();
        pthread_mutex_unlock(&g_mu);
    }
    pthread_mutex_lock(&g_mu);
}

static void *ps4m_worker(void *arg) {
    (void)arg;
    /* (app) Initialize can block for good while the installer service is not up: on a thread of its own, at most 10 s
       (tile.c), and before the lock is taken, so that the page never waits behind it */
    int irc = ps5tm_appinst_init();
    pthread_mutex_lock(&g_mu);
    if (irc)
        printf("[main] sceAppInstUtilInitialize: 0x%08x%s\n", (unsigned)irc, irc == -0xDEAD ? " (timed out)" : irc == -0xBADD ? " (missing on this firmware)" : "");
    if (!sceAppInstUtilAppInstallAll || !sceAppInstUtilAppUnInstall)
        printf("[main] AppInstallAll/AppUnInstall missing on this firmware: games are only added one at a time, if at all\n");
    mkdirs(STATE_DIR, 0777);
    check_firmware();   /* since 0.4.4: InstallAll from 6.00 on, below that one title at a time, or none */
    if (g_reg_mode == REG_ALL && !sceAppInstUtilAppInstallAll) /* (app) the call is weak: missing, the next way or none */
        g_reg_mode = g_title_dir ? REG_TITLEDIR : REG_NONE;
    {
        /* start at the end of the log: what happened before is not ours */
        static ps5tm_klog_line_t one[1];
        uint64_t newest = 0;
        int more = 0, lost = 0;
        ps5tm_klog_fetch(0, 1, one, &newest, &more, &lost);
        g_klog_after = newest;
    }
    if (user_app_masked())   /* 0.6.0: an earlier run ended while /user/app showed only the new games (after the klog anchor: waiting polls it) */
        unmask_leftover();
    g_stop = 0;
    g_next_scan = 0;
    unlink(STATE_DIR "/stop");
    if (g_reg_mode == REG_ALL)
        notify("PS4Mount läuft in PS5 Cooling & System Center");
    else if (g_reg_mode == REG_TITLEDIR)
        notify("PS4Mount läuft (Firmware %s: Spiele werden einzeln hinzugefügt)", g_fw_text);
    else
        notify("PS4Mount: Firmware %s wird noch nicht unterstützt. Zum Schutz der Systemdatenbank werden keine Spiele hinzugefügt.", g_fw_text);
    printf("[main] worker started\n");
    int give_way = 0, seen = 0;
    while (!g_stop && g_enabled) {
        if (exists(STATE_DIR "/stop")) {
            unlink(STATE_DIR "/stop");
            break;
        }
        if (!still_listening(g_lock_sock)) {
            if (g_lock_sock >= 0)
                close(g_lock_sock);
            if ((g_lock_sock = instance_lock()) < 0) {
                /* (app) someone else took the lock (after rest mode): the stand-alone PS4Mount; this module gives way and waits
                   (until 15.10.2026 it switched itself off for the session) */
                snprintf(g_standalone_why, sizeof(g_standalone_why), "Es hält die Sperre auf Port 9032.");
                give_way = 1;
                break;
            }
            printf("[main] instance lock opened again\n");
        }
        /* (app) once per scan: a stand-alone PS4Mount that got going beside this module (both locks were gone after rest mode)
           makes this one give way; seen twice in a row, so that a stand-alone one that only tried to start and quit at once
           because of our lock does not count */
        if (time(NULL) >= g_next_scan && !g_deleting) {
            char why[160];
            pthread_mutex_unlock(&g_mu);
            int sa = standalone_seen(1, why, sizeof(why));
            pthread_mutex_lock(&g_mu);
            seen = sa ? seen + 1 : 0;
            if (seen >= 2) {
                snprintf(g_standalone_why, sizeof(g_standalone_why), "%s", why);
                give_way = 1;
                break;
            }
        }
        if (g_deleting && g_deleting_close_at && time(NULL) >= g_deleting_close_at)
            end_delete_window();
        if (g_deleting && time(NULL) - g_deleting_since > 600)
            end_delete_window(); /* the line of the screen change was missed */
        /* 0.5.0: moving pkgs back (asked by the page) and adding a title again after the system took it out */
        if (g_restore_n || g_restore_all) {
            run_restore(g_restore_all ? NULL : g_restore_tids, g_restore_n);
            g_restore_n = 0;
            g_restore_all = 0;
            continue;
        }
        if (g_readd_tid[0] && !g_deleting) {
            char tid[10];
            snprintf(tid, sizeof(tid), "%s", g_readd_tid);
            g_readd_tid[0] = 0;
            readd_title(tid);
            continue;
        }
        if (time(NULL) >= g_next_scan && !g_deleting) {
            if (g_reg_mode != REG_NONE)
                scan_all();
            else if (g_manual_scan)
                notify("Firmware %s wird noch nicht unterstützt: Es werden keine Spiele hinzugefügt", g_fw_text);
            g_manual_scan = 0;
            g_next_scan = time(NULL) + SCAN_SECONDS;
        }
        pump_events(1000);
    }
    if (give_way) {
        notify("PS4Mount der App hält an: Das eigenständige PS4Mount läuft. Danach übernimmt die App wieder.");
        printf("[main] the stand-alone PS4Mount runs (%s): this module gives way\n", g_standalone_why);
    } else {
        notify("PS4Mount wurde beendet");
    }
    printf("[main] worker stopped\n");
    if (g_lock_sock >= 0) {
        close(g_lock_sock);
        g_lock_sock = -1;
    }
    g_alive = 0;
    pthread_mutex_unlock(&g_mu);
    if (give_way)
        waiter_start();
    return NULL;
}

/* The page never waits long for the lock: the worker may hold it for a minute (an uninstall that does not answer, a group of
   registrations), and requests that pile up behind it would fill every connection of the web server (review 15.10.2026).
   0 when it is taken within ms. */
static int lock_for_page(int ms) {
    for (int waited = 0;; waited += 50) {
        if (!pthread_mutex_trylock(&g_mu))
            return 0;
        if (waited >= ms)
            return -1;
        usleep(50 * 1000);
    }
}

#define P4M_BUSY "PS4Mount ist gerade beschäftigt: Es fügt hinzu, entfernt oder verschiebt. Bitte gleich noch einmal versuchen."

#define P4M_WAITS "Das eigenständige PS4Mount (ps4mount.elf) läuft schon. Die App lädt PS4Mount deshalb nicht ein zweites Mal und übernimmt von selbst, sobald es beendet ist."

/* Starts the thread (the lock on 9032 first). Returns 0; 1 when a stand-alone PS4Mount runs (the module waits for it, msg says so);
   or -1 with a text in msg. Takes g_mu itself, so that two starts at once (the page and the app's start) cannot both open the
   instance lock. */
static int worker_start(char *msg, size_t n) {
    char why[160];
    /* (app) never a second PS4Mount: looked at before anything is loaded (without the lock: it may take up to 0.6 s) */
    int sa = !g_alive && standalone_seen(0, why, sizeof(why));
    if (lock_for_page(3000)) {
        snprintf(msg, n, P4M_BUSY);
        return -1;
    }
    if (g_alive) {
        g_stop = 0; /* off and on again quickly: the stop not yet carried out is taken back */
        pthread_mutex_unlock(&g_mu);
        snprintf(msg, n, "PS4Mount läuft schon");
        return 0;
    }
    if (!sa && (g_lock_sock = instance_lock()) < 0) {
        snprintf(why, sizeof(why), "Es hält die Sperre auf Port 9032.");
        sa = 1;
    }
    if (sa) {
        snprintf(g_standalone_why, sizeof(g_standalone_why), "%s", why);
        pthread_mutex_unlock(&g_mu);
        printf("[main] the stand-alone PS4Mount runs (%s): not loaded a second time, waiting for it to end\n", why);
        waiter_start();
        snprintf(msg, n, P4M_WAITS);
        return 1;
    }
    g_standalone_why[0] = 0;
    g_alive = 1;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_attr_setstacksize(&attr, 8 * 1024 * 1024); /* title_src_t is a few hundred KB on the stack, as in the original */
    pthread_t th;
    int rc = pthread_create(&th, &attr, ps4m_worker, NULL);
    pthread_attr_destroy(&attr);
    if (rc) {
        g_alive = 0;
        close(g_lock_sock);
        g_lock_sock = -1;
        pthread_mutex_unlock(&g_mu);
        snprintf(msg, n, "Der Dienst ließ sich nicht starten");
        return -1;
    }
    pthread_mutex_unlock(&g_mu);
    snprintf(msg, n, "PS4Mount ist eingeschaltet");
    return 0;
}

/* (app) Waits while a stand-alone PS4Mount runs and starts the module once it is gone, as long as the module stays switched on.
   One at a time; looks every 15 s. */
static void *standalone_waiter(void *arg) {
    (void)arg;
    for (;;) {
        sleep(15);
        pthread_mutex_lock(&g_mu);
        int go = g_enabled && !g_alive;
        pthread_mutex_unlock(&g_mu);
        if (!go)
            break;
        char why[160];
        if (standalone_seen(0, why, sizeof(why))) {
            pthread_mutex_lock(&g_mu);
            snprintf(g_standalone_why, sizeof(g_standalone_why), "%s", why);
            pthread_mutex_unlock(&g_mu);
            continue;
        }
        __atomic_store_n(&g_sa_waiting, 0, __ATOMIC_RELEASE);   /* worker_start may need a new waiter (it saw the lock taken) */
        char msg[200];
        int rc = worker_start(msg, sizeof(msg));
        if (rc == 0) {
            printf("[main] the stand-alone PS4Mount is gone: this module took over\n");
            PS5TM_INFO("ps4m_takeover", "PS4Mount: Das eigenständige PS4Mount ist beendet, die App hat übernommen.");
        }
        if (rc >= 0)
            return NULL;        /* started, or a new waiter runs */
        if (__atomic_exchange_n(&g_sa_waiting, 1, __ATOMIC_ACQ_REL))
            return NULL;        /* busy, and another waiter got going meanwhile */
    }
    __atomic_store_n(&g_sa_waiting, 0, __ATOMIC_RELEASE);
    return NULL;
}

static void waiter_start(void) {
    if (__atomic_exchange_n(&g_sa_waiting, 1, __ATOMIC_ACQ_REL))
        return;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_attr_setstacksize(&attr, 256 * 1024);
    pthread_t th;
    if (pthread_create(&th, &attr, standalone_waiter, NULL))
        __atomic_store_n(&g_sa_waiting, 0, __ATOMIC_RELEASE);
    pthread_attr_destroy(&attr);
}

/* --- calls from the page (api.c) --- */

void ps4m_boot(void) {
    static int booted; /* main.c calls this for every new listener (rest mode, a new network): once is enough */
    if (booted)
        return;
    booted = 1;
    pthread_mutex_lock(&g_mu);
    config_load();
    int on = g_enabled;
    pthread_mutex_unlock(&g_mu);
    if (on) {
        char msg[200];
        int rc = worker_start(msg, sizeof(msg));
        if (rc == 1)
            PS5TM_INFO("ps4m_boot", "PS4Mount: %s", msg);
        else if (rc)
            PS5TM_WARN("ps4m_boot", "PS4Mount: %s", msg);
    }
}

int ps4m_set_enabled(int on, char *msg, size_t n) {
    if (lock_for_page(3000)) {
        snprintf(msg, n, P4M_BUSY);
        return -1;
    }
    config_load();
    g_enabled = on != 0;
    config_save();
    if (!on) {
        g_stop = 1;
        /* what was asked for and not yet done does not wait for the next start (it would close a game at once then) */
        g_restore_n = 0;
        g_restore_all = 0;
        g_readd_tid[0] = 0;
    }
    pthread_mutex_unlock(&g_mu);
    if (on) {
        int rc = worker_start(msg, n);
        return rc > 0 ? 0 : rc;   /* waiting for the stand-alone one is no failure: switched on, msg says what happens */
    }
    snprintf(msg, n, "PS4Mount ist ausgeschaltet. Eingebundene Spiele bleiben, bis die Konsole neu startet oder Sie sie entfernen.");
    return 0;
}

/* {"ok":true,"enabled":…,"running":…,"version":…,"config":{…},"games":[…]} as a malloc'd string. */
/* The last answer: given (marked "busy") while the worker holds the lock for long. */
static pthread_mutex_t g_json_mu = PTHREAD_MUTEX_INITIALIZER;
static char *g_json_last;

static char *state_json_impl(void) {
    if (lock_for_page(1500)) {
        char *c = NULL;
        pthread_mutex_lock(&g_json_mu);
        if (g_json_last && g_json_last[0] == '{') {
            size_t l = strlen(g_json_last);
            c = malloc(l + 16);
            if (c) {
                memcpy(c, "{\"busy\":true,", 13);
                memcpy(c + 13, g_json_last + 1, l); /* the rest and its NUL */
            }
        }
        pthread_mutex_unlock(&g_json_mu);
        return c ? c : strdup("{\"ok\":true,\"busy\":true,\"enabled\":true,\"running\":true,\"games\":[],\"config\":{}}");
    }
    buf_t games = {0}, cfg = {0}, out = {0};
    ps4m_games_json(&games);
    ps4m_config_json(&cfg);
    int enabled = g_enabled, alive = g_alive;
    char sa[sizeof(g_standalone_why)];
    snprintf(sa, sizeof(sa), "%s", g_standalone_why);
    pthread_mutex_unlock(&g_mu);
    /* games is {"version":…,"games":[…]}: put the state in front of its fields and the settings at its end */
    const char *g = games.data ? (const char *)games.data : "{}";
    size_t gl = strlen(g);
    p4m_buf_printf(&out, "{\"ok\":true,\"enabled\":%s,\"running\":%s,", enabled ? "true" : "false", alive ? "true" : "false");
    /* (app) why the module waits: the stand-alone PS4Mount runs (the texts hold no quotes; a process name was cleaned) */
    if (enabled && !alive && sa[0])
        p4m_buf_printf(&out, "\"standalone\":\"%s\",", sa);
    if (gl > 2) {
        p4m_buf_put(&out, g + 1, gl - 2);
        p4m_buf_printf(&out, ",\"config\":%s}", cfg.data ? (const char *)cfg.data : "{}");
    } else {
        p4m_buf_printf(&out, "\"games\":[],\"config\":%s}", cfg.data ? (const char *)cfg.data : "{}");
    }
    p4m_buf_free(&games);
    p4m_buf_free(&cfg);
    char *res = (char *)out.data;
    if (res) {
        pthread_mutex_lock(&g_json_mu);
        free(g_json_last);
        g_json_last = strdup(res);
        pthread_mutex_unlock(&g_json_mu);
    }
    return res;
}

/* CUSA and five digits, as the module's own checks want it */
static int tid_ok(const char *t) {
    if (!t || strlen(t) != 9 || strncmp(t, "CUSA", 4))
        return 0;
    for (int i = 4; i < 9; i++)
        if (t[i] < '0' || t[i] > '9')
            return 0;
    return 1;
}

/* action: "scan"; "add", "remove", "restore" (tid; for "restore" a comma-separated list of title IDs); "restore_all";
   "restored_forget". Returns 0 or -1; msg says what happened. Moving back runs in the worker, which must be running. */
int ps4m_do_action(const char *action, const char *tid, char *msg, size_t n) {
    int rc = -1;
    if (lock_for_page(3000)) {
        snprintf(msg, n, P4M_BUSY);
        return -1;
    }
    if (g_masked) { /* 0.6.0: the other games are hidden for a moment; adding, removing and moving wait until they show again */
        pthread_mutex_unlock(&g_mu);
        snprintf(msg, n, P4M_BUSY);
        return -1;
    }
    if (!strcmp(action, "scan")) {
        ps4m_rescan();
        snprintf(msg, n, g_alive ? "Die Laufwerke werden jetzt abgesucht" : "PS4Mount ist ausgeschaltet; bitte zuerst einschalten");
        rc = g_alive ? 0 : -1;
    } else if (!strcmp(action, "restore_all") || !strcmp(action, "restore")) {
        if (!g_alive)
            snprintf(msg, n, "PS4Mount ist ausgeschaltet; bitte zuerst einschalten");
        else if (!strcmp(action, "restore_all"))
            rc = ps4m_restore(NULL, msg, n);
        else if (tid && tid[0])
            rc = ps4m_restore(tid, msg, n);
        else
            snprintf(msg, n, "Keine Spiele ausgewählt");
    } else if (!strcmp(action, "restored_forget")) {
        rc = ps4m_restored_forget(msg, n);
    } else if (tid_ok(tid)) {
        if (!strcmp(action, "add"))
            rc = ps4m_add(tid, msg, n);
        else if (!strcmp(action, "remove"))
            rc = ps4m_remove(tid, msg, n);
        else
            snprintf(msg, n, "Unbekannte Aktion");
    } else {
        snprintf(msg, n, "Unbekannte Aktion oder Titel-ID");
    }
    pthread_mutex_unlock(&g_mu);
    return rc;
}

int ps4m_do_config(const char *form, char *msg, size_t n) {
    if (lock_for_page(3000)) {
        snprintf(msg, n, P4M_BUSY);
        return -1;
    }
    int rc = ps4m_config_set(form, msg, n);
    pthread_mutex_unlock(&g_mu);
    return rc;
}

static int icon_impl(const char *tid, unsigned char **data, size_t *len) {
    if (!tid_ok(tid) || lock_for_page(1500))
        return -1;
    buf_t b = {0};
    int rc = ps4m_icon(tid, &b);
    pthread_mutex_unlock(&g_mu);
    if (rc || !b.data || !b.len) {
        p4m_buf_free(&b);
        return -1;
    }
    *data = b.data;
    *len = b.len;
    return 0;
}


/* The page's calls run on a thread with a big stack: title_src_t and pkg_t are large, and a request thread has little. */
typedef struct {
    char *json;
    const char *tid;
    unsigned char **data;
    size_t *len;
    int rc;
    int which;
} p4m_call_t;

static void *p4m_call_main(void *p) {
    p4m_call_t *c = p;
    if (c->which == 0)
        c->json = state_json_impl();
    else
        c->rc = icon_impl(c->tid, c->data, c->len);
    return NULL;
}

static int p4m_call(p4m_call_t *c) {
    pthread_t th;
    pthread_attr_t at;
    pthread_attr_init(&at);
    pthread_attr_setstacksize(&at, 4 * 1024 * 1024);
    int rc = pthread_create(&th, &at, p4m_call_main, c);
    pthread_attr_destroy(&at);
    if (rc)
        return -1;
    pthread_join(th, NULL);
    return 0;
}

char *ps4m_state_json(void) {
    p4m_call_t c = {0};
    c.which = 0;
    return p4m_call(&c) ? NULL : c.json;
}

int ps4m_icon_png(const char *tid, unsigned char **data, size_t *len) {
    p4m_call_t c = {0};
    c.which = 1;
    c.tid = tid;
    c.data = data;
    c.len = len;
    return p4m_call(&c) ? -1 : c.rc;
}

/* The last bytes of the module's log as a malloc'd string. */
char *ps4m_log_tail(size_t max) {
    FILE *f = fopen(STATE_DIR "/log.txt", "rb");
    if (!f)
        return strdup("");
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    long from = size > (long)max ? size - (long)max : 0;
    fseek(f, from, SEEK_SET);
    char *s = calloc(1, max + 1);
    if (s) {
        size_t r = fread(s, 1, max, f);
        s[r] = 0;
    }
    fclose(f);
    return s ? s : strdup("");
}

#endif /* PS5TM_HOST_TEST */
