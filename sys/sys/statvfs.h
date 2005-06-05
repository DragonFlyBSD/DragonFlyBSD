/*
 * Copyright (c) 2005 The DragonFly Project.  All rights reserved.
 * 
 * This code is derived from software contributed to The DragonFly Project
 * by Jeroen Ruigrok van der Werven <asmodai@tendra.org>
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
 * $DragonFly: src/sys/sys/statvfs.h,v 1.2 2005/06/05 09:40:46 asmodai Exp $
 */

#ifndef _SYS_STATVFS_H_
#define _SYS_STATVFS_H_

#include <sys/types.h>

/*
 * The POSIX 1003.1 standard uses free and available nomenclature to mean the
 * following:
 * - free means all space, including privileged processes,
 * - avail means all space available to unprivileged processes. 
 */

struct statvfs {
	unsigned long	f_bsize;	/* file system block size */
	unsigned long	f_frsize;	/* fundamental file system */
	fsblkcnt_t	f_blocks;	/* total number of blocks on fs */
	fsblkcnt_t	f_bfree;	/* total number of free blocks */
	fsblkcnt_t	f_bavail;	/* total number of available blocks */
	fsfilcnt_t	f_files;	/* total number of file serial num */
	fsfilcnt_t	f_ffree;	/* total number of free file ser num */
	fsfilcnt_t	f_favail;	/* total number of avail file ser num */
	unsigned long	f_fsid;		/* file system ID */
	unsigned long	f_flag;		/* bit mask of f_flag values */
	unsigned long	f_namemax;	/* maximum filename length */
};

/* f_flag definitions */
#define ST_RDONLY	0x1	/* fs is read-only */
#define ST_NOSUID	0x2	/* fs does not support ST_ISUID or ST_ISGID */

#if 0
int	fstatvfs(int, struct statvfs *);
int	statvfs(const char *__restrict, struct statvfs *__restrict);
#endif

#endif /* _SYS_STATVFS_H_ */
