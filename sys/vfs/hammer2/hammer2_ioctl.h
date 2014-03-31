/*
 * Copyright (c) 2011-2014 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@dragonflybsd.org>
 * by Venkatesh Srinivas <vsrinivas@dragonflybsd.org>
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

#ifndef _VFS_HAMMER2_IOCTL_H_
#define _VFS_HAMMER2_IOCTL_H_

#ifndef _SYS_IOCCOM_H_
#include <sys/ioccom.h>
#endif
#ifndef _VFS_HAMMER2_DISK_H_
#include "hammer2_disk.h"
#endif
#ifndef _VFS_HAMMER2_MOUNT_H_
#include "hammer2_mount.h"
#endif

/*
 * get_version
 */
struct hammer2_ioc_version {
	int			version;
	char			reserved[256 - 4];
};

typedef struct hammer2_ioc_version hammer2_ioc_version_t;

struct hammer2_ioc_recluster {
	int			fd;
	char			reserved[256 - 4];
};

typedef struct hammer2_ioc_recluster hammer2_ioc_recluster_t;

/*
 * Ioctls to manage the volume->copyinfo[] array and to associate or
 * disassociate sockets
 */
struct hammer2_ioc_remote {
	int			copyid;
	int			nextid;	/* for iteration (get only) */
	int			fd;	/* socket descriptor if applicable */
	int			reserved03;
	int			reserved04[8];
	dmsg_vol_data_t		copy1;	/* copy spec */
	dmsg_vol_data_t		copy2;	/* copy spec (rename ops only) */
};

typedef struct hammer2_ioc_remote hammer2_ioc_remote_t;

/*
 * Ioctls to manage PFSs
 *
 * PFSs can be clustered by matching their pfs_clid, and the PFSs making up
 * a cluster can be uniquely identified by combining the vol_id with
 * the pfs_clid.
 */
struct hammer2_ioc_pfs {
	hammer2_key_t		name_key;	/* super-root directory scan */
	hammer2_key_t		name_next;	/* (GET only) */
	uint8_t			pfs_type;	/* e.g. MASTER, SLAVE, ... */
	uint8_t			reserved0011;
	uint8_t			reserved0012;
	uint8_t			reserved0013;
	uint32_t		reserved0014;
	uint64_t		reserved0018;
	uuid_t			pfs_fsid;	/* identifies PFS instance */
	uuid_t			pfs_clid;	/* identifies PFS cluster */
	char			name[NAME_MAX+1]; /* device@name mtpt */
};

typedef struct hammer2_ioc_pfs hammer2_ioc_pfs_t;

/*
 * Ioctls to manage inodes
 */
struct hammer2_ioc_inode {
	uint32_t		flags;
	void			*kdata;
	hammer2_inode_data_t	ip_data;
};

typedef struct hammer2_ioc_inode hammer2_ioc_inode_t;

#define HAMMER2IOC_INODE_FLAG_IQUOTA	0x00000001
#define HAMMER2IOC_INODE_FLAG_DQUOTA	0x00000002
#define HAMMER2IOC_INODE_FLAG_COPIES	0x00000004

/*
 * Ioctl list
 */

#define HAMMER2IOC_VERSION_GET	_IOWR('h', 64, struct hammer2_ioc_version)
#define HAMMER2IOC_RECLUSTER	_IOWR('h', 65, struct hammer2_ioc_recluster)

#define HAMMER2IOC_REMOTE_SCAN	_IOWR('h', 68, struct hammer2_ioc_remote)
#define HAMMER2IOC_REMOTE_ADD	_IOWR('h', 69, struct hammer2_ioc_remote)
#define HAMMER2IOC_REMOTE_DEL	_IOWR('h', 70, struct hammer2_ioc_remote)
#define HAMMER2IOC_REMOTE_REP	_IOWR('h', 71, struct hammer2_ioc_remote)

#define HAMMER2IOC_SOCKET_GET	_IOWR('h', 76, struct hammer2_ioc_remote)
#define HAMMER2IOC_SOCKET_SET	_IOWR('h', 77, struct hammer2_ioc_remote)

#define HAMMER2IOC_PFS_GET	_IOWR('h', 80, struct hammer2_ioc_pfs)
#define HAMMER2IOC_PFS_CREATE	_IOWR('h', 81, struct hammer2_ioc_pfs)
#define HAMMER2IOC_PFS_DELETE	_IOWR('h', 82, struct hammer2_ioc_pfs)
#define HAMMER2IOC_PFS_LOOKUP	_IOWR('h', 83, struct hammer2_ioc_pfs)
#define HAMMER2IOC_PFS_SNAPSHOT	_IOWR('h', 84, struct hammer2_ioc_pfs)

#define HAMMER2IOC_INODE_GET	_IOWR('h', 86, struct hammer2_ioc_inode)
#define HAMMER2IOC_INODE_SET	_IOWR('h', 87, struct hammer2_ioc_inode)

/*#define HAMMER2IOC_INODE_COMP_SET	_IOWR('h', 88, struct hammer2_ioc_inode) //set compression mode on inode
#define HAMMER2IOC_INODE_COMP_REC_SET	_IOWR('h', 89, struct hammer2_ioc_inode)
#define HAMMER2IOC_INODE_COMP_REC_SET2	_IOWR('h', 90, struct hammer2_ioc_inode)*/

#define HAMMER2IOC_DEBUG_DUMP	_IOWR('h', 91, int)

#endif /* !_VFS_HAMMER2_IOCTL_H_ */
