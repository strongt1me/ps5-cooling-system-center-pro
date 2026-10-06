/* Host-test stand-in for the SDK's <ps5/kernel.h>.
 * Signatures mirror target/include/ps5/kernel.h exactly. */
#ifndef PS5TM_TEST_PS5_KERNEL_H
#define PS5TM_TEST_PS5_KERNEL_H

#include <stdint.h>
#include <sys/types.h>

uint32_t kernel_get_fw_version(void);
intptr_t kernel_get_proc(pid_t pid);
intptr_t kernel_get_root_vnode(void);
int32_t  kernel_set_proc_rootdir(pid_t pid, intptr_t vnode);
int32_t  kernel_set_proc_jaildir(pid_t pid, intptr_t vnode);
int32_t  kernel_set_ucred_uid(pid_t pid, uid_t uid);
int32_t  kernel_set_ucred_ruid(pid_t pid, uid_t ruid);
int32_t  kernel_set_ucred_svuid(pid_t pid, uid_t svuid);
int32_t  kernel_set_ucred_rgid(pid_t pid, gid_t rgid);
int32_t  kernel_set_ucred_svgid(pid_t pid, gid_t svgid);
int32_t  kernel_set_ucred_authid(pid_t pid, uint64_t authid);
int32_t  kernel_set_ucred_caps(pid_t pid, const uint8_t caps[16]);
int32_t  kernel_set_ucred_attrs(pid_t pid, const uint8_t attrs[32]);

#endif
