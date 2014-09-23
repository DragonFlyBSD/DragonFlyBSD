/*
 * Copyright (c) 2004 The DragonFly Project.  All rights reserved.
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
 *
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
/*
 * This module serves as a focal point for virtually all filesystem and
 * device related calls.  It is or will be responsible for all high level
 * kernel management for filesystem and device operations, including but
 * limited to:
 *
 *      Function                        Status
 *      ----------------                -----
 *      Journaling                      TODO
 *      Range Locking                   TODO
 *      Cache Coherency                 TODO
 *      VNode Operations Dispatch       TODO
 *      Mount Point Operations          TODO
 *      FileOps Operations              TODO
 */

#ifndef _SYS_VFSCACHE_H_
#define _SYS_VFSCACHE_H_

#ifndef _SYS_TYPES_H_
#include <sys/types.h>
#endif
#ifndef _SYS_TIME_H_
#include <sys/time.h>
#endif
#ifndef _SYS_VFSOPS_H_
#include <sys/vfsops.h>
#endif
#ifndef _SYS_UUID_H_
#include <sys/uuid.h>
#endif

/*
 * Vnode types.  VNON means no type or transitory type.  VINT is used
 * for internal types.  Note that VNON is skipped by the vnode scan.
 */
enum vtype	{ VNON, VREG, VDIR, VBLK, VCHR, VLNK, VSOCK, VFIFO, VBAD,
		  VDATABASE, VINT };

/*
 * Vnode tag types.
 * These are for the benefit of external programs only (e.g., pstat)
 * and should NEVER be inspected by the kernel.
 */
enum vtagtype	{
	VT_NON, VT_UFS, VT_NFS, VT_MFS, VT_PC, VT_LFS, VT_LOFS, VT_FDESC,
	VT_PORTAL, VT_NULL, VT_UNUSED10, VT_KERNFS, VT_PROCFS, VT_AFS,
	VT_ISOFS, VT_UNION, VT_MSDOSFS, VT_TFS, VT_VFS, VT_CODA, VT_NTFS,
	VT_HPFS, VT_SMBFS, VT_UDF, VT_EXT2FS, VT_SYNTH,
	VT_HAMMER, VT_HAMMER2, VT_DEVFS, VT_TMPFS
};

/*
 * Vnode attributes.  A field value of VNOVAL represents a field whose value
 * is unavailable (getattr) or which is not to be changed (setattr).
 *
 * Some vattr fields may be wider then what is reported to userland.
 */
struct vattr {
	enum vtype	va_type;	/* vnode type (for create) */
	u_int64_t	va_nlink;	/* number of references to file */
	u_short		va_mode;	/* files access mode and type */
	uid_t		va_uid;		/* owner user id */
	gid_t		va_gid;		/* owner group id */
	udev_t		va_fsid;	/* file system id */
	ino_t		va_fileid;	/* file id */
	u_quad_t	va_size;	/* file size in bytes */
	long		va_blocksize;	/* blocksize preferred for i/o */
	struct timespec	va_atime;	/* time of last access */
	struct timespec	va_mtime;	/* time of last modification */
	struct timespec	va_ctime;	/* time file changed */
	u_int64_t	va_gen;		/* generation number of file */
	u_long		va_flags;	/* flags defined for file */
	int		va_rmajor;	/* device the special file represents */
	int		va_rminor;
	u_quad_t	va_bytes;	/* bytes of disk space held by file */
	u_quad_t	va_filerev;	/* file modification number */
	u_int		va_vaflags;	/* operations flags, see below */
	long		va_spare;	/* remain quad aligned */
	int64_t		va_unused01;
	uuid_t		va_uid_uuid;	/* native uuids if available */
	uuid_t		va_gid_uuid;
	uuid_t		va_fsid_uuid;
};

/*
 * Flags for va_vaflags.
 *
 * NOTE: The short versions for the uid, gid, and fsid are always populated
 * even when the uuid versions are available.
 */
#define	VA_UTIMES_NULL		0x0001	/* utimes argument was NULL */
#define VA_EXCLUSIVE		0x0002	/* exclusive create request */
#define VA_UID_UUID_VALID	0x0004	/* uuid fields also populated */
#define VA_GID_UUID_VALID	0x0008	/* uuid fields also populated */
#define VA_FSID_UUID_VALID	0x0010	/* uuid fields also populated */

#endif
