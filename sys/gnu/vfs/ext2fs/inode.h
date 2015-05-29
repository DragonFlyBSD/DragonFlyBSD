/*-
 * Copyright (c) 1982, 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
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
 *
 *	@(#)inode.h	8.9 (Berkeley) 5/14/95
 * $FreeBSD: src/sys/ufs/ufs/inode.h,v 1.28.2.2 2001/09/29 12:52:52 iedowse Exp $
 */

#ifndef _VFS_GNU_EXT2FS_INODE_H_
#define	_VFS_GNU_EXT2FS_INODE_H_

#if defined(_KERNEL) || defined(_KERNEL_STRUCTURES)

#ifndef _SYS_LOCK_H_
#include <sys/lock.h>
#endif
#ifndef _SYS_LOCKF_H
#include <sys/lockf.h>
#endif

#endif

#ifndef _SYS_QUEUE_H_
#include <sys/queue.h>
#endif
#include "dinode.h"

/*
 * This must agree with the definition in "dir.h"
 */
#define	doff_t		int32_t

#if defined(_KERNEL) || defined(_KERNEL_STRUCTURES)

/*
 * An EXT2FS inode is basically a UFS inode with some spare fields
 * redefined and a different superblock structure.
 */
struct inode {
	struct inode	*i_next;/* Hash chain */
	struct	vnode  *i_vnode;/* Vnode associated with this inode. */
	struct	vnode  *i_devvp;/* Vnode for block I/O. */
	uint32_t i_flag;	/* flags, see below */
	cdev_t	  i_dev;	/* Device associated with the inode. */
	ino_t	  i_number;	/* The identity of the inode. */
	int	  i_effnlink;	/* i_nlink when I/O completes */

	struct	ext2_sb_info *i_e2fs;	/* EXT2FS */
	struct	 ext2_dquot *i_dquot[MAXQUOTAS]; /* Dquot structures. */
	u_quad_t i_modrev;	/* Revision level for NFS lease. */
	struct	 lockf i_lockf;/* Head of byte-level lock list. */
	/*
	 * Side effects; used during directory lookup.
	 */
	int32_t	  i_count;	/* Size of free slot in directory. */
	doff_t	  i_endoff;	/* End of useful stuff in directory. */
	doff_t	  i_diroff;	/* Offset in dir, where we found last entry. */
	doff_t	  i_offset;	/* Offset of free space in directory. */
	ino_t	  i_ino;	/* Inode number of found directory. */
	uint32_t i_reclen;	/* Size of found directory entry. */
	uint32_t i_spare[3];	/* XXX actually non-spare (for ext2fs). */

	struct dirhash *i_dirhash; /* Hashing for large directories */
	/*
	 * The on-disk dinode itself (128 bytes)
	 */
	struct	ext2_dinode i_din;
};

#ifndef NO_I_DEFINES

#define	i_atime		i_din.di_atime
#define	i_atimensec	i_din.di_atimensec
#define	i_blocks	i_din.di_blocks
#define	i_ctime		i_din.di_ctime
#define	i_ctimensec	i_din.di_ctimensec
#define	i_db		i_din.di_db
#define	i_flags		i_din.di_flags
#define	i_gen		i_din.di_gen
#define	i_gid		i_din.di_gid
#define	i_ib		i_din.di_ib
#define	i_mode		i_din.di_mode
#define	i_mtime		i_din.di_mtime
#define	i_mtimensec	i_din.di_mtimensec
#define	i_nlink		i_din.di_nlink
#define	i_rdev		i_din.di_rdev
#define	i_shortlink	i_din.di_shortlink
#define	i_size		i_din.di_size
#define	i_uid		i_din.di_uid

#endif

#endif

/* These flags are kept in i_flag. */
#define	IN_ACCESS	0x0001		/* Access time update request. */
#define	IN_CHANGE	0x0002		/* Inode change time update request. */
#define	IN_UPDATE	0x0004		/* Modification time update request. */
#define	IN_MODIFIED	0x0008		/* Inode has been modified. */
#define	IN_RENAME	0x0010		/* Inode is being renamed. */
#define	IN_SHLOCK	0x0020		/* File has shared lock. */
#define	IN_EXLOCK	0x0040		/* File has exclusive lock. */
#define	IN_HASHED	0x0080		/* Inode is on hash list */
#define	IN_LAZYMOD	0x0100		/* Modified, but don't write yet. */

#if defined(_KERNEL) || defined(_KERNEL_STRUCTURES)

/*
 * Structure used to pass around logical block paths generated by
 * ext2_getlbns and used by truncate and bmap code.
 */
struct indir {
	ext2_daddr_t in_lbn;		/* Logical block number. */
	int	in_off;			/* Offset in buffer. */
	int	in_exists;		/* Flag if the block exists. */
};

/* Convert between inode pointers and vnode pointers. */
#define VTOI(vp)	((struct inode *)(vp)->v_data)
#define ITOV(ip)	((ip)->i_vnode)

/* Determine if soft dependencies are being done */
#define DOINGSOFTDEP(vp)	((vp)->v_mount->mnt_flag & MNT_SOFTDEP)
#define DOINGASYNC(vp)		((vp)->v_mount->mnt_flag & MNT_ASYNC)

/* This overlays the fid structure (see mount.h). */
struct ufid {
	uint16_t ufid_len;	/* Length of structure. */
	uint16_t ufid_pad;	/* Force 32-bit alignment. */
	ino_t	  ufid_ino;	/* File number (ino). */
	int32_t	  ufid_gen;	/* Generation number. */
};
#endif /* _KERNEL || _KERNEL_STRUCTURES */

#endif /* !_VFS_GNU_EXT2FS_INODE_H_ */
