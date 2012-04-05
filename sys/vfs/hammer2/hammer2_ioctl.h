/*
 * Copyright (c) 2011-2012 The DragonFly Project.  All rights reserved.
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
#ifndef VFS_HAMMER2_IOCTL_H_
#define VFS_HAMMER2_IOCTL_H_

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
	hammer2_copy_data_t	copy1;	/* copy spec */
	hammer2_copy_data_t	copy2;	/* copy spec (rename ops only) */
};

typedef struct hammer2_ioc_remote hammer2_ioc_remote_t;

#define HAMMER2IOC_GET_VERSION	_IOWR('h', 64, struct hammer2_ioc_version)

#define HAMMER2IOC_GET_REMOTE	_IOWR('h', 68, struct hammer2_ioc_remote)
#define HAMMER2IOC_ADD_REMOTE	_IOWR('h', 69, struct hammer2_ioc_remote)
#define HAMMER2IOC_DEL_REMOTE	_IOWR('h', 70, struct hammer2_ioc_remote)
#define HAMMER2IOC_REP_REMOTE	_IOWR('h', 71, struct hammer2_ioc_remote)

#define HAMMER2IOC_GET_SOCKET	_IOWR('h', 76, struct hammer2_ioc_remote)
#define HAMMER2IOC_SET_SOCKET	_IOWR('h', 77, struct hammer2_ioc_remote)

#endif
