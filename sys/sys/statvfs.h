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
 */

#ifndef _SYS_STATVFS_H_
#define _SYS_STATVFS_H_

#include <machine/stdint.h>
#include <sys/uuid.h>

#ifndef _FSBLKCNT_T_DECLARED
typedef	__uint64_t	fsblkcnt_t;	/* filesystem block count */
#define	_FSBLKCNT_T_DECLARED
#endif
#ifndef _FSFILCNT_T_DECLARED
typedef	__uint64_t	fsfilcnt_t;	/* filesystem file count */
#define	_FSFILCNT_T_DECLARED
#endif
#ifndef _UID_T_DECLARED
typedef	__uint32_t	uid_t;		/* user id */
#define	_UID_T_DECLARED
#endif

#if __BSD_VISIBLE
struct fhandle;
struct statfs;
#endif

/*
 * The POSIX 1003.1 standard uses free and available nomenclature to mean the
 * following:
 * - free means all space, including privileged processes,
 * - avail means all space available to unprivileged processes. 
 */

struct statvfs {
	unsigned long	f_bsize;	/* file system block size */
	unsigned long	f_frsize;	/* fundamental file system block size */
	fsblkcnt_t	f_blocks;	/* total number of blocks on fs */
	fsblkcnt_t	f_bfree;	/* total number of free blocks */
	fsblkcnt_t	f_bavail;	/* total number of available blocks */
	fsfilcnt_t	f_files;	/* total number of file serial num */
	fsfilcnt_t	f_ffree;	/* total number of free file ser num */
	fsfilcnt_t	f_favail;	/* total number of avail file ser num */
	unsigned long	f_fsid;		/* file system ID */
	unsigned long	f_flag;		/* bit mask of f_flag values */
	unsigned long	f_namemax;	/* maximum filename length */
	uid_t		f_owner;	/* user that mounted the filesystem */
	unsigned int	f_type;		/* filesystem type */

	__uint64_t  	f_syncreads;	/* count of sync reads since mount */
	__uint64_t  	f_syncwrites;	/* count of sync writes since mount */

	__uint64_t  	f_asyncreads;	/* count of async reads since mount */
	__uint64_t  	f_asyncwrites;	/* count of async writes since mount */

	/*
	 * DragonFly extensions - full uuid FSID and owner
	 */
	uuid_t		f_fsid_uuid;
	uuid_t		f_uid_uuid;
};

/*
 * f_flag definitions
 */
#define ST_RDONLY	0x00000001	/* fs is read-only */
#define ST_NOSUID	0x00000002	/* ST_ISUID or ST_ISGID not supported */

#if __BSD_VISIBLE
/*
 * DragonFly specific flags
 */
#define ST_FSID_UUID	0x40000000	/* f_fsid_uuid field is valid */
#define ST_OWNER_UUID	0x80000000	/* f_owner_uuid field is valid */
#endif

__BEGIN_DECLS
int	fstatvfs(int, struct statvfs *);
int	statvfs(const char * __restrict, struct statvfs * __restrict);

#if __BSD_VISIBLE
int	fhstatvfs(const struct fhandle *, struct statvfs * __restrict);
int	getvfsstat(struct statfs * __restrict, struct statvfs * __restrict, long, int);
#endif
__END_DECLS

#endif /* _SYS_STATVFS_H_ */
