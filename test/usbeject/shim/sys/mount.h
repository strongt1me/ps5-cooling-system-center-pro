/* Host test shim: the BSD <sys/mount.h> pieces that src/usbeject.c uses (struct statfs with the mount names, getfsstat); Linux has other ones. */
#ifndef PS5TM_SHIM_SYS_MOUNT_H
#define PS5TM_SHIM_SYS_MOUNT_H
#include <sys/types.h>
struct statfs {
  char f_mntfromname[88];
  char f_mntonname[88];
  char f_fstypename[16];
};
#define MNT_NOWAIT 2
int getfsstat(struct statfs *buf, long bufsize, int mode);
#endif
