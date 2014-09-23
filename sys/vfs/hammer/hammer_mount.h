/*
 * Copyright (c) 2007 The DragonFly Project.  All rights reserved.
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
 * 
 * $DragonFly: src/sys/vfs/hammer/hammer_mount.h,v 1.11 2008/09/17 21:44:20 dillon Exp $
 */

#ifndef _SYS_TYPES_H_
#include <sys/types.h>
#endif
#ifndef _SYS_MOUNT_H_
#include <sys/mount.h>
#endif

/*
 * This structure is passed from userland to the kernel during the mount
 * system call.
 */
struct hammer_mount_info {
	const char	**volumes;	/* array of pointers to device names */
	int		nvolumes;	/* number of devices */
	int		hflags;		/* extended hammer mount flags */
	int		master_id;	/* -1=no-mirror mode, or 0-15 */
	u_int64_t	asof;		/* asof - HAMMER_MAX_TID is current */
	char		reserved1[136];	/* was struct export_args */
	u_int64_t	reserved2[15];
};

#define HMNT_NOHISTORY	0x00000001
#define HMNT_MASTERID	0x00000002	/* master_id field set */
#define HMNT_EXPORTREQ	0x00000004
#define HMNT_UNDO_DIRTY	0x00000008

#define HMNT_USERFLAGS	(HMNT_NOHISTORY | HMNT_MASTERID)
