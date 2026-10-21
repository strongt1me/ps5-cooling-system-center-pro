/* savemount.c — privilege-escalated PS4/PS5 save image mount / unmount.
 *
 * Sources of knowledge (all studied, none copied):
 *   Garlic savemgr 1.14 — mount / ioctl recipe
 *   Apollo saves for PS5 (bucanero/apollo-ps5, GPL-3) — key layout
 *   ShadowMountPlus 1.7beta7 — privilege pattern (ps5tm_platform_escalate_as)
 *
 * The flow (summarised in savemount.h) is:
 *   read key → decrypt via pfsmgr ioctl → escalate identity → mount
 *   → work in mountpoint → unmount → restore identity
 *
 * SIGPIPE is set to SIG_IGN before the mount call (the kernel may send it
 * during the handshake) and restored to default afterwards.
 *
 * Under PS5TM_HOST_TEST the whole mount/unmount is a no-op so the host-side
 * tests can exercise savesfo.c / savedb.c without real hardware.
 */

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "ps5tm.h"
#include "savemount.h"

/* ── Constants ─────────────────────────────────────────────────────────── */

#define SCRATCH_BASE "/data/PS5-Cooling-Center/savemount"
#define IMG_NAME     "sdimg_work"           /* name of the local copy            */

/* Authid for save data access (same as Garlic savemgr) */
#define AUTHID_SAVEDATA 0x4800000000000010ULL

/* pfsmgr ioctl: decrypt image key.
 *   in:  buf[0x00..0x3F] = encrypted key (64 bytes from the image / .bin)
 *   out: buf[0x60..0x7F] = decrypted 32-byte AES key */
#define PFSMGR_IOCTL_DECRYPT 0xc0845302u
#define PFSMGR_BUF_SIZE      0x100

/* PS5 image key is at file offset 0x800, 0x60 bytes */
#define PS5_KEY_OFF  0x800
#define PS5_KEY_LEN  0x60
/* PS4 companion .bin is 96 bytes long; the whole thing is the key material */
#define PS4_KEY_LEN  96

/* Decrypted key is at offset 0x60 inside the pfsmgr output buffer */
#define DECRYPTED_KEY_OFF 0x60
#define DECRYPTED_KEY_LEN 32

/* ── libSceFsInternalForVsh dynamic bindings ────────────────────────────── */

#ifndef PS5TM_HOST_TEST

#include "dynsym.h"

#define VSH "libSceFsInternalForVsh.sprx"

typedef struct {
    char budgetid[64];            /* "system\0" + padding          */
    uint8_t pad[192];
} scefsopt_t;

typedef int (*fn_init_mount_t)(scefsopt_t *);
typedef int (*fn_mount_t)(scefsopt_t *, const char *, const char *,
                          const uint8_t *);
typedef int (*fn_init_umount_t)(scefsopt_t *);
typedef int (*fn_umount_t)(scefsopt_t *, const char *, int, int);

static fn_init_mount_t  fn_init_mount;
static fn_mount_t       fn_mount;
static fn_init_umount_t fn_init_umount;
static fn_umount_t      fn_umount;
static pthread_mutex_t  g_mount_lock = PTHREAD_MUTEX_INITIALIZER;
static int              g_mount_open;  /* 1 if a mount is currently active */

static int vsh_init(void)
{
    if (fn_mount)   /* already resolved */
        return 0;

    fn_init_mount  = (fn_init_mount_t) ps5tm_dynsym(VSH, "sceFsInitMountSaveDataOpt");
    fn_mount       = (fn_mount_t)      ps5tm_dynsym(VSH, "sceFsMountSaveData");
    fn_init_umount = (fn_init_umount_t)ps5tm_dynsym(VSH, "sceFsInitUmountSaveDataOpt");
    fn_umount      = (fn_umount_t)     ps5tm_dynsym(VSH, "sceFsUmountSaveData");

    if (!fn_init_mount || !fn_mount || !fn_init_umount || !fn_umount) {
        printf("[savemount] one or more symbols missing from %s\n", VSH);
        fn_mount = NULL;   /* reset so next call retries */
        return -1;
    }
    return 0;
}

#endif /* !PS5TM_HOST_TEST */

/* ── Internal helpers ──────────────────────────────────────────────────── */

/* Copy a file; returns 0 on success. */
static int copy_file(const char *src, const char *dst)
{
    int fdin  = open(src, O_RDONLY);
    int fdout = -1;
    if (fdin < 0)
        return -1;

    fdout = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fdout < 0) {
        close(fdin);
        return -1;
    }

    char buf[65536];
    ssize_t n;
    int ret = 0;
    while ((n = read(fdin, buf, sizeof(buf))) > 0) {
        ssize_t w = write(fdout, buf, (size_t)n);
        if (w != n) { ret = -1; break; }
    }
    if (n < 0)
        ret = -1;

    close(fdin);
    close(fdout);
    return ret;
}

/* Read the encrypted key material from the source, pass it through the
 * pfsmgr ioctl and return the 32-byte decrypted key.
 *   key_src:   for PS5 the image path; for PS4 the companion .bin path
 *   save_type: SAVEMOUNT_PS5 or SAVEMOUNT_PS4
 *   key32_out: 32-byte output buffer */
static int decrypt_key(const char *key_src, int save_type, uint8_t *key32_out)
{
#ifdef PS5TM_HOST_TEST
    (void)key_src; (void)save_type;
    memset(key32_out, 0xAB, 32);
    return 0;
#else
    uint8_t buf[PFSMGR_BUF_SIZE];
    memset(buf, 0, sizeof(buf));

    int fd = open(key_src, O_RDONLY);
    if (fd < 0) {
        printf("[savemount] decrypt_key: open %s: %s\n",
               key_src, strerror(errno));
        return -1;
    }

    ssize_t n;
    if (save_type == SAVEMOUNT_PS5) {
        /* PS5: key is at offset 0x800 inside the image, 0x60 bytes */
        if (lseek(fd, PS5_KEY_OFF, SEEK_SET) != PS5_KEY_OFF) {
            close(fd); return -1;
        }
        n = read(fd, buf, PS5_KEY_LEN);
        if (n != PS5_KEY_LEN) {
            close(fd); return -1;
        }
    } else {
        /* PS4: companion <slot>.bin is the key material */
        n = read(fd, buf, PS4_KEY_LEN);
        if (n != PS4_KEY_LEN) {
            close(fd); return -1;
        }
    }
    close(fd);

    /* Decrypt via pfsmgr ioctl */
    int pfsmgr = open("/dev/pfsmgr", O_RDONLY);
    if (pfsmgr < 0) {
        printf("[savemount] open /dev/pfsmgr: %s\n", strerror(errno));
        return -1;
    }
    int rc = ioctl(pfsmgr, PFSMGR_IOCTL_DECRYPT, buf);
    close(pfsmgr);
    if (rc != 0) {
        printf("[savemount] pfsmgr ioctl: %d (errno %d)\n", rc, errno);
        return -1;
    }

    memcpy(key32_out, buf + DECRYPTED_KEY_OFF, DECRYPTED_KEY_LEN);
    return 0;
#endif
}

/* ── Public API ────────────────────────────────────────────────────────── */

int savemount_open(const char *img_path, const char *key_src,
                   int save_type, char mp_out[SAVEMOUNT_MP_MAX])
{
#ifdef PS5TM_HOST_TEST
    /* Under host tests just report a temp directory as the "mount point" */
    snprintf(mp_out, SAVEMOUNT_MP_MAX, "/tmp/ps5tm_savemount_test");
    mkdir(mp_out, 0700);
    return 0;
#else
    /* Only one mount at a time */
    pthread_mutex_lock(&g_mount_lock);
    if (g_mount_open) {
        pthread_mutex_unlock(&g_mount_lock);
        printf("[savemount] already mounted\n");
        errno = EBUSY;
        return -1;
    }

    if (vsh_init() != 0) {
        pthread_mutex_unlock(&g_mount_lock);
        errno = ENOTSUP;
        return -1;
    }

    /* Make scratch directories */
    if (mkdir(SCRATCH_BASE, 0700) != 0 && errno != EEXIST) {
        pthread_mutex_unlock(&g_mount_lock);
        return -1;
    }
    char mp[SAVEMOUNT_MP_MAX];
    snprintf(mp, sizeof(mp), "%s/mnt", SCRATCH_BASE);
    if (mkdir(mp, 0700) != 0 && errno != EEXIST) {
        pthread_mutex_unlock(&g_mount_lock);
        return -1;
    }

    /* Copy image to scratch (must not mount from /user/home — EPIPE) */
    char local_img[SAVEMOUNT_MP_MAX];
    snprintf(local_img, sizeof(local_img), "%s/%s", SCRATCH_BASE, IMG_NAME);
    printf("[savemount] copying image to scratch …\n");
    if (copy_file(img_path, local_img) != 0) {
        printf("[savemount] copy_file: %s\n", strerror(errno));
        pthread_mutex_unlock(&g_mount_lock);
        return -1;
    }

    /* Decrypt key */
    uint8_t key32[DECRYPTED_KEY_LEN];
    if (decrypt_key(key_src, save_type, key32) != 0) {
        unlink(local_img);
        pthread_mutex_unlock(&g_mount_lock);
        return -1;
    }

    /* Escalate identity for save data access */
    ps5tm_sony_api_lock();
    if (ps5tm_platform_escalate_as(AUTHID_SAVEDATA) != 0) {
        printf("[savemount] platform_escalate_as SAVEDATA failed\n");
        ps5tm_sony_api_unlock();
        unlink(local_img);
        pthread_mutex_unlock(&g_mount_lock);
        errno = EPERM;
        return -1;
    }

    /* Suppress SIGPIPE during mount */
    struct sigaction old_sa, ign_sa;
    memset(&ign_sa, 0, sizeof(ign_sa));
    ign_sa.sa_handler = SIG_IGN;
    sigaction(SIGPIPE, &ign_sa, &old_sa);

    /* Mount */
    scefsopt_t opt;
    memset(&opt, 0, sizeof(opt));
    strncpy(opt.budgetid, "system", sizeof(opt.budgetid) - 1);
    fn_init_mount(&opt);
    int rc = fn_mount(&opt, local_img, mp, key32);

    sigaction(SIGPIPE, &old_sa, NULL);

    /* Restore identity to FAN (the process's normal identity) */
    ps5tm_platform_escalate();
    ps5tm_sony_api_unlock();

    if (rc != 0) {
        printf("[savemount] sceFsMountSaveData returned 0x%x\n", (unsigned)rc);
        unlink(local_img);
        pthread_mutex_unlock(&g_mount_lock);
        errno = EIO;
        return -1;
    }

    g_mount_open = 1;
    snprintf(mp_out, SAVEMOUNT_MP_MAX, "%s", mp);
    pthread_mutex_unlock(&g_mount_lock);
    return 0;
#endif
}

int savemount_close(const char *mp)
{
#ifdef PS5TM_HOST_TEST
    (void)mp;
    return 0;
#else
    pthread_mutex_lock(&g_mount_lock);

    int ret = 0;
    if (g_mount_open && fn_umount) {
        ps5tm_sony_api_lock();
        ps5tm_platform_escalate_as(AUTHID_SAVEDATA);

        struct sigaction old_sa, ign_sa;
        memset(&ign_sa, 0, sizeof(ign_sa));
        ign_sa.sa_handler = SIG_IGN;
        sigaction(SIGPIPE, &ign_sa, &old_sa);

        scefsopt_t opt;
        memset(&opt, 0, sizeof(opt));
        strncpy(opt.budgetid, "system", sizeof(opt.budgetid) - 1);
        fn_init_umount(&opt);
        int rc = fn_umount(&opt, mp, 0, 0);
        if (rc != 0)
            printf("[savemount] sceFsUmountSaveData returned 0x%x\n",
                   (unsigned)rc);

        sigaction(SIGPIPE, &old_sa, NULL);
        ps5tm_platform_escalate();
        ps5tm_sony_api_unlock();

        ret = (rc == 0) ? 0 : -1;
        g_mount_open = 0;
    }

    /* Clean up scratch */
    char local_img[SAVEMOUNT_MP_MAX];
    snprintf(local_img, sizeof(local_img), "%s/%s", SCRATCH_BASE, IMG_NAME);
    unlink(local_img);

    pthread_mutex_unlock(&g_mount_lock);
    return ret;
#endif
}
