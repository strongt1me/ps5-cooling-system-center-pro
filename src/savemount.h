/* savemount.h — privilege-escalated PS4/PS5 save image mount / unmount.
 *
 * A save data image (sdimg_<slot>) is an encrypted filesystem image.  The
 * PS5 exposes it through libSceFsInternalForVsh (a system library not in
 * the payload SDK stubs) plus an ioctl on /dev/pfsmgr to decrypt the key.
 *
 * To mount:
 *   1. Read the encrypted key material from the image or its companion file.
 *   2. Decrypt it via ioctl(pfsmgr, 0xc0845302, buf[256]).
 *   3. Escalate the process identity to authid 0x4800000000000010, cap[7]|=0x40.
 *   4. Call sceFsMountSaveData(opt, image_path, mountpoint, key_32).
 *   5. Do the needed work inside mountpoint.
 *   6. Unmount with sceFsUmountSaveData, then restore the FAN identity.
 *
 * The image MUST NOT be opened from its original savedata_prospero location
 * (that causes EPIPE): copy it to /data/ps5cc-savemount/ first.
 *
 * This module manages the scratch area and the per-session mount directory.
 * Only one mount is allowed at a time (the lock is internal).
 *
 * save_type is 0 for PS5 (byte 0 of image == 2) and 1 for PS4 (byte 0 == 1).
 */

#pragma once
#include <stdint.h>

/* Maximum length of a mountpoint path handed back by savemount_open */
#define SAVEMOUNT_MP_MAX 256

/* Mount a save image for read/write access.
 *
 *   img_path   source image file (may be anywhere writable; this module
 *              copies it to a private scratch area automatically if it lives
 *              inside /user/home or /user/savedata — supply the original path)
 *   key_src    for PS5: path to the image file itself (key is at offset 0x800)
 *              for PS4: path to the companion <slot>.bin (96-byte key file)
 *   save_type  SAVEMOUNT_PS5 or SAVEMOUNT_PS4
 *   mp_out     receives the mount point path (NUL-terminated, at most
 *              SAVEMOUNT_MP_MAX bytes); the caller works inside this directory
 *
 * Returns 0 on success, −1 on error (errno set; an additional explanation is
 * printed to the ps5tm log). */
int savemount_open(const char *img_path, const char *key_src,
                   int save_type, char mp_out[SAVEMOUNT_MP_MAX]);

/* Unmount a previously mounted save and clean up the scratch area.
 * Always call this even when the mount succeeded only partially.
 * Returns 0 on success. */
int savemount_close(const char *mp);

/* Platform type identifiers */
#define SAVEMOUNT_PS5  0
#define SAVEMOUNT_PS4  1
