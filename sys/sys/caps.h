/*
 * Copyright (c) 2023 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#ifndef _SYS_CAPS_H_
#define	_SYS_CAPS_H_

#ifndef _MACHINE_STDINT_H_
#include <machine/stdint.h>
#endif

/*
 * NOTE: 2 bits per system capability.  System capabilities are negatives,
 *	 i.e. restrictions, rather than allowances.
 */
typedef __uint64_t		__syscapelm_t;

#define __SYSCAP_EXECMASK	((__syscapelm_t)0xAAAAAAAAAAAAAAAALLU)

#define __SYSCAP_COUNT		256
#define __SYSCAP_BITS_PER	2
#define __SYSCAP_BITS_MASK	((1 << __SYSCAP_BITS_PER) - 1)
#define __SYSCAP_PER_ELM	(sizeof(__syscapelm_t) * 8 / __SYSCAP_BITS_PER)
#define __SYSCAP_PER_ELM_MASK	(__SYSCAP_PER_ELM - 1)
#define __SYSCAP_NUMELMS	(__SYSCAP_COUNT / __SYSCAP_PER_ELM)

#define __SYSCAP_INDEX(cap)	((cap) / __SYSCAP_PER_ELM)
#define __SYSCAP_SHIFT(cap)	(((cap) & __SYSCAP_PER_ELM_MASK) *	\
				 __SYSCAP_BITS_PER)


typedef struct syscaps {
	__syscapelm_t	caps[__SYSCAP_NUMELMS];
} __syscaps_t;

/*
 * Resource data passed to/from userland
 */
typedef struct syscap_base {
	size_t		res;	/* resource type, EOF ends list */
	size_t		len;	/* total bytes including this header */
} syscap_base_t;

#define SYSCAP_RESOURCE_EOF	0x0000

#if 0
/* TODO / DISCUSS */
#define SYSCAP_RESOURCE_PATH	0x0001		/* allowed wildcard paths */
#endif

/*
 * Restriction application.  Restrictions applied to EXEC or ALL are inherited
 * by children recursively.  A restriction that only applies to SELF is not
 * inherited by children.
 *
 * Restrictions cannot be downgraded once applied (this is a feature).
 * The syscap_set() operation is a logical OR.
 */
#define __SYSCAP_NONE		0x00000000	/* no restriction */
#define __SYSCAP_SELF		0x00000001	/* restrict for us */
#define __SYSCAP_EXEC		0x00000002	/* restrict for execs */
#define __SYSCAP_ALL		0x00000003	/* restrict for us and fork */

#define __SYSCAP_XFLAGS		0x7FFF0000	/* extra flags (in 'cap') */
#define __SYSCAP_INPARENT	0x00010000	/* set in parent process */
#define __SYSCAP_NULLCRED	0x00020000	/* null cred ok */
#define __SYSCAP_NOROOTTEST	0x00040000	/* don't test uid */

#define __SYSCAP_GROUP_0	0x00000000
#define __SYSCAP_GROUP_1	0x00000010
#define __SYSCAP_GROUP_2	0x00000020
#define __SYSCAP_GROUP_3	0x00000030
#define __SYSCAP_GROUP_4	0x00000040
#define __SYSCAP_GROUP_5	0x00000050
#define __SYSCAP_GROUP_6	0x00000060
#define __SYSCAP_GROUP_7	0x00000070
#define __SYSCAP_GROUP_8	0x00000080
#define __SYSCAP_GROUP_9	0x00000090
#define __SYSCAP_GROUP_10	0x000000A0
#define __SYSCAP_GROUP_11	0x000000B0
#define __SYSCAP_GROUP_12	0x000000C0
#define __SYSCAP_GROUP_13	0x000000D0
#define __SYSCAP_GROUP_14	0x000000E0
#define __SYSCAP_GROUP_15	0x000000F0

#define __SYSCAP_GROUP_MASK	0x000000F0
#define __SYSCAP_GROUP_SHIFT	4

/*
 * Base capability restrictions.  Group 0 can be used to disable the whole
 * of any other group 1...15.
 *
 * RESTRICTEDROOT	- Catch-all for dangerous root interactions,
 *			  always disabled by jails and also automatically
 *			  disabled upon chroot.
 *
 * SENSITIVEROOT	- Catch-all for sensitive root operations such as
 *			  mount, umount, ifconfig, and so forth, some of
 *			  which might be enabled in jails.
 */
#define SYSCAP_ANY		(__SYSCAP_GROUP_0 | 0)
#define SYSCAP_RESTRICTEDROOT	(__SYSCAP_GROUP_0 | 1)
#define SYSCAP_SENSITIVEROOT	(__SYSCAP_GROUP_0 | 2)
#define SYSCAP_NOEXEC		(__SYSCAP_GROUP_0 | 3)
#define SYSCAP_NOCRED		(__SYSCAP_GROUP_0 | 4)
#define SYSCAP_NOJAIL		(__SYSCAP_GROUP_0 | 5)
#define SYSCAP_NONET		(__SYSCAP_GROUP_0 | 6)
#define SYSCAP_NONET_SENSITIVE	(__SYSCAP_GROUP_0 | 7)
#define SYSCAP_NOVFS		(__SYSCAP_GROUP_0 | 8)
#define SYSCAP_NOVFS_SENSITIVE	(__SYSCAP_GROUP_0 | 9)
#define SYSCAP_NOMOUNT		(__SYSCAP_GROUP_0 | 10)
#define SYSCAP_NO11		(__SYSCAP_GROUP_0 | 11)
#define SYSCAP_NO12		(__SYSCAP_GROUP_0 | 12)
#define SYSCAP_NO13		(__SYSCAP_GROUP_0 | 13)
#define SYSCAP_NO14		(__SYSCAP_GROUP_0 | 14)
#define SYSCAP_NO15		(__SYSCAP_GROUP_0 | 15)

/*
 * Automatic if SYSCAP_RESTRICTEDROOT is set
 */
#define SYSCAP_NODRIVER		(__SYSCAP_GROUP_1 | 0)
#define SYSCAP_NOVM_MLOCK	(__SYSCAP_GROUP_1 | 1)
#define SYSCAP_NOVM_RESIDENT	(__SYSCAP_GROUP_1 | 2)
#define SYSCAP_NOCPUCTL_WRMSR	(__SYSCAP_GROUP_1 | 3)
#define SYSCAP_NOCPUCTL_UPDATE	(__SYSCAP_GROUP_1 | 4)
#define SYSCAP_NOACCT		(__SYSCAP_GROUP_1 | 5)
#define SYSCAP_NOKENV_WR	(__SYSCAP_GROUP_1 | 6)
#define SYSCAP_NOKLD		(__SYSCAP_GROUP_1 | 7)
#define SYSCAP_NOKERN_WR	(__SYSCAP_GROUP_1 | 8)
#define SYSCAP_NOREBOOT		(__SYSCAP_GROUP_1 | 9)	/* incs shutdown */

/*
 * Automatic if SYSCAP_SENSITIVEROOT is set
 */
#define SYSCAP_NOPROC_TRESPASS	(__SYSCAP_GROUP_2 | 0)
#define SYSCAP_NOPROC_SETLOGIN	(__SYSCAP_GROUP_2 | 1)
#define SYSCAP_NOPROC_SETRLIMIT	(__SYSCAP_GROUP_2 | 2)
#define SYSCAP_NOSYSCTL_WR	(__SYSCAP_GROUP_2 | 3)
#define SYSCAP_NOVARSYM_SYS	(__SYSCAP_GROUP_2 | 4)
#define SYSCAP_NOSETHOSTNAME	(__SYSCAP_GROUP_2 | 5)
#define SYSCAP_NOQUOTA_WR	(__SYSCAP_GROUP_2 | 6)
#define SYSCAP_NODEBUG_UNPRIV	(__SYSCAP_GROUP_2 | 7)
#define SYSCAP_NOSETTIME	(__SYSCAP_GROUP_2 | 8)
#define SYSCAP_NOSCHED		(__SYSCAP_GROUP_2 | 9)
#define SYSCAP_NOSCHED_CPUSET	(__SYSCAP_GROUP_2 | 10)

#define SYSCAP_NOEXEC_SUID	(__SYSCAP_GROUP_3 | 0)
#define SYSCAP_NOEXEC_SGID	(__SYSCAP_GROUP_3 | 1)

#define SYSCAP_NOCRED_SETUID	(__SYSCAP_GROUP_4 | 0)
#define SYSCAP_NOCRED_SETGID	(__SYSCAP_GROUP_4 | 1)
#define SYSCAP_NOCRED_SETEUID	(__SYSCAP_GROUP_4 | 2)
#define SYSCAP_NOCRED_SETEGID	(__SYSCAP_GROUP_4 | 3)
#define SYSCAP_NOCRED_SETREUID	(__SYSCAP_GROUP_4 | 4)
#define SYSCAP_NOCRED_SETREGID	(__SYSCAP_GROUP_4 | 5)
#define SYSCAP_NOCRED_SETRESUID	(__SYSCAP_GROUP_4 | 6)
#define SYSCAP_NOCRED_SETRESGID	(__SYSCAP_GROUP_4 | 7)
#define SYSCAP_NOCRED_SETGROUPS	(__SYSCAP_GROUP_4 | 8)

#define SYSCAP_NOJAIL_CREATE	(__SYSCAP_GROUP_5 | 0)
#define SYSCAP_NOJAIL_ATTACH	(__SYSCAP_GROUP_5 | 1)

#define SYSCAP_NONET_RESPORT	(__SYSCAP_GROUP_6 | 0)
#define SYSCAP_NONET_RAW	(__SYSCAP_GROUP_6 | 1)

#define SYSCAP_NONET_IFCONFIG	(__SYSCAP_GROUP_7 | 0)	/* sensitive NET */
#define SYSCAP_NONET_ROUTE	(__SYSCAP_GROUP_7 | 1)
#define SYSCAP_NONET_LAGG	(__SYSCAP_GROUP_7 | 2)
#define SYSCAP_NONET_NETGRAPH	(__SYSCAP_GROUP_7 | 3)
#define SYSCAP_NONET_BT_RAW	(__SYSCAP_GROUP_7 | 4)
#define SYSCAP_NONET_WIFI	(__SYSCAP_GROUP_7 | 5)

#define SYSCAP_NOVFS_SYSFLAGS	(__SYSCAP_GROUP_8 | 0)
#define SYSCAP_NOVFS_CHOWN	(__SYSCAP_GROUP_8 | 1)
#define SYSCAP_NOVFS_CHMOD	(__SYSCAP_GROUP_8 | 2)
#define SYSCAP_NOVFS_LINK	(__SYSCAP_GROUP_8 | 3)
#define SYSCAP_NOVFS_CHFLAGS_DEV (__SYSCAP_GROUP_8 | 4)
#define SYSCAP_NOVFS_SETATTR	(__SYSCAP_GROUP_8 | 5)
#define SYSCAP_NOVFS_SETGID	(__SYSCAP_GROUP_8 | 6)
#define SYSCAP_NOVFS_GENERATION	(__SYSCAP_GROUP_8 | 7)
#define SYSCAP_NOVFS_RETAINSUGID (__SYSCAP_GROUP_8 | 8)

#define SYSCAP_NOVFS_MKNOD_BAD	(__SYSCAP_GROUP_9 | 0)	/* sensitive VFS */
#define SYSCAP_NOVFS_MKNOD_WHT	(__SYSCAP_GROUP_9 | 1)
#define SYSCAP_NOVFS_MKNOD_DIR	(__SYSCAP_GROUP_9 | 2)
#define SYSCAP_NOVFS_MKNOD_DEV	(__SYSCAP_GROUP_9 | 3)
#define SYSCAP_NOVFS_IOCTL	(__SYSCAP_GROUP_9 | 4)	/* sensitive ioctls */
#define SYSCAP_NOVFS_CHROOT	(__SYSCAP_GROUP_9 | 5)
#define SYSCAP_NOVFS_REVOKE	(__SYSCAP_GROUP_9 | 6)

#define SYSCAP_NOMOUNT_NULLFS	(__SYSCAP_GROUP_10 | 0)
#define SYSCAP_NOMOUNT_DEVFS	(__SYSCAP_GROUP_10 | 1)
#define SYSCAP_NOMOUNT_TMPFS	(__SYSCAP_GROUP_10 | 2)
#define SYSCAP_NOMOUNT_UMOUNT	(__SYSCAP_GROUP_10 | 3)
#define SYSCAP_NOMOUNT_FUSE	(__SYSCAP_GROUP_10 | 4)
#define SYSCAP_NOMOUNT_PROCFS	(__SYSCAP_GROUP_10 | 5)

#if 0
/* TODO/DISCUSS */

#define SYSCAP_NOBIND		8
#define SYSCAP_NOLISTEN		9
#define SYSCAP_NOACCEPT		10
#define SYSCAP_NOCONNECT	11

#define SYSCAP_NOFILE_UIDR	16	/* restrict uid match to (list) */
#define SYSCAP_NOFILE_UIDW	17	/* restrict uid match to (list) */
#define SYSCAP_NOFILE_UIDX	18	/* restrict uid match to (list) */
#define SYSCAP_NOFILE_GIDR	19	/* restrict gid match to (list) */
#define SYSCAP_NOFILE_GIDW	20	/* restrict gid match to (list) */
#define SYSCAP_NOFILE_GIDX	21	/* restrict gid match to (list) */
#define SYSCAP_NOFILE_OIDR	22	/* restrict catch-all to (list) */
#define SYSCAP_NOFILE_OIDW	23	/* restrict catch-all to (list) */
#define SYSCAP_NOFILE_OIDX	24	/* restrict catch-all to (list) */
#endif

#define __SYSCAP_STRINGS_0		\
		"any",			\
		"restricted_root",	\
		"sensitive_root",	\
		"noexec",		\
		"nocred",		\
		"nojail",		\
		"nonet",		\
		"nonet_sensitive",	\
		"novfs",		\
		"novfs_sensitive",	\
		"nomount"

#define __SYSCAP_STRINGS_1		\
		"nodriver",		\
		"novm_mlock",		\
		"novm_resident",	\
		"nocpuctl_wrmsr",	\
		"nocpuctl_update",	\
		"noacct",		\
		"nokenv_wr",		\
		"nokld",		\
		"nokern_wr",		\
		"noreboot"

#define __SYSCAP_STRINGS_2		\
		"noproc_trespass",	\
		"noproc_setlogin",	\
		"noproc_setrlimit",	\
		"nosysctl_wr",		\
		"novarsym_sys",		\
		"nosethostname",	\
		"noquota_wr",		\
		"nodebug_unpriv",	\
		"nosettime",		\
		"nosched",		\
		"nosched_cpuset"

#define __SYSCAP_STRINGS_3		\
		"noexec_suid",		\
		"noexec_sgid"

#define __SYSCAP_STRINGS_4		\
		"nocred_setuid",	\
		"nocred_setgid",	\
		"nocred_seteuid",	\
		"nocred_setegid",	\
		"nocred_setreuid",	\
		"nocred_setregid",	\
		"nocred_setresuid",	\
		"nocred_setresgid",	\
		"nocred_setgroups"

#define __SYSCAP_STRINGS_5		\
		"nojail_create",	\
		"nojail_attach"

#define __SYSCAP_STRINGS_6		\
		"nonet_resport",	\
		"nonet_raw"

#define __SYSCAP_STRINGS_7		\
		"nonet_ifconfig",	\
		"nonet_route",		\
		"nonet_lagg",		\
		"nonet_netgraph",	\
		"nonet_bt_raw",		\
		"nonet_wifi"

#define __SYSCAP_STRINGS_8		\
		"novfs_sysflags",	\
		"novfs_chown",		\
		"novfs_chmod",		\
		"novfs_chroot",		\
		"novfs_link",		\
		"novfs_chflags_dev",	\
		"novfs_revoke",		\
		"novfs_setattr",	\
		"novfs_setgid",		\
		"novfs_generation",	\
		"novfs_retainsugid"

#define __SYSCAP_STRINGS_9		\
		"novfs_mknod_bad",	\
		"novfs_mknod_wht",	\
		"novfs_mknod_dir",	\
		"novfs_mknod_dev",	\
		"novfs_ioct"

#define __SYSCAP_STRINGS_10		\
		"nomount_nullfs",	\
		"nomount_devfs",	\
		"nomount_tmpfs",	\
		"nomount_umount",	\
		"nomount_fuse"

#define __SYSCAP_ALLSTRINGS					\
	const char *SyscapAllStrings[__SYSCAP_COUNT/16][16] = {	\
		{ __SYSCAP_STRINGS_0 },				\
		{ __SYSCAP_STRINGS_1 },				\
		{ __SYSCAP_STRINGS_2 },				\
		{ __SYSCAP_STRINGS_3 },				\
		{ __SYSCAP_STRINGS_4 },				\
		{ __SYSCAP_STRINGS_5 },				\
		{ __SYSCAP_STRINGS_6 },				\
		{ __SYSCAP_STRINGS_7 },				\
		{ __SYSCAP_STRINGS_8 },				\
		{ __SYSCAP_STRINGS_9 },				\
		{ __SYSCAP_STRINGS_10 }				\
	}

/*
 * userland prototypes
 */
__BEGIN_DECLS
int syscap_get(int cap, void *data, size_t bytes);
int syscap_set(int cap, int flags, const void *data, size_t bytes);
__END_DECLS

/*
 * kern_caps.c support functions
 */
#ifdef _KERNEL

struct proc;
struct thread;
struct ucred;

void caps_exec(struct proc *p);
int caps_get(struct ucred *cred, int cap);
void caps_set_locked(struct proc *p, int cap, int flags);
int caps_priv_check(struct ucred *cred, int cap);
int caps_priv_check_self(int cap);
int caps_priv_check_td(struct thread *td, int cap);

#endif

#endif
