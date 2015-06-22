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
 *	@(#)dinode.h	8.3 (Berkeley) 1/21/94
 * $FreeBSD: src/sys/ufs/ufs/dinode.h,v 1.7 1999/08/28 00:52:27 peter Exp $
 */

#ifndef _VFS_GNU_EXT2FS_DINODE_H_
#define _VFS_GNU_EXT2FS_DINODE_H_

#include "ext2_fs.h"

/*
 * The root inode is the root of the filesystem.  Inode 0 can't be used for
 * normal purposes and historically bad blocks were linked to inode 1, thus
 * the root inode is 2.  (Inode 1 is no longer used for this purpose, however
 * numerous dump tapes make this assumption, so we are stuck with it).
 */
#define	EXT2_ROOTINO	((ino_t)EXT2_ROOT_INO)

/*
 * The Whiteout inode# is a dummy non-zero inode number which will
 * never be allocated to a real file.  It is used as a place holder
 * in the directory entry which has been tagged as a DT_W entry.
 * See the comments about EXT2_ROOTINO above.
 *
 * Note that ext2 ondisk specification defines 1 as EXT2_BAD_INO.
 */
#define	WINO	((ino_t)1)

/*
 * A dinode contains all the meta-data associated with a EXT2 file.
 * This structure defines the on-disk format of a dinode. Since
 * this structure describes an on-disk structure, all its fields
 * are defined by types with precise widths.
 */

typedef __int32_t       ext2_daddr_t;

/* Direct addresses in inode */
#define	NDADDR	EXT2_NDIR_BLOCKS

/* Indirect addresses in inode */
#define	NIADDR	(EXT2_N_BLOCKS - EXT2_NDIR_BLOCKS)

struct ext2_dinode {
	uint16_t	di_mode;	/*   0: IFMT, permissions; see below. */
	int16_t		di_nlink;	/*   2: File link count. */
	union {
		uint16_t oldids[2];	/*   4: Ffs: old user and group ids. */
		int32_t	  inumber;	/*   4: Lfs: inode number. */
	} di_u;
	uint64_t	di_size;	/*   8: File byte count. */
	int32_t		di_atime;	/*  16: Last access time. */
	int32_t		di_atimensec;	/*  20: Last access time. */
	int32_t		di_mtime;	/*  24: Last modified time. */
	int32_t		di_mtimensec;	/*  28: Last modified time. */
	int32_t		di_ctime;	/*  32: Last inode change time. */
	int32_t		di_ctimensec;	/*  36: Last inode change time. */
	ext2_daddr_t	di_db[NDADDR];	/*  40: Direct disk blocks. */
	ext2_daddr_t	di_ib[NIADDR];	/*  88: Indirect disk blocks. */
	uint32_t	di_flags;	/* 100: Status flags (chflags). */
	int32_t		di_blocks;	/* 104: Blocks actually held. */
	int32_t		di_gen;		/* 108: Generation number. */
	uint32_t	di_uid;		/* 112: File owner. */
	uint32_t	di_gid;		/* 116: File group. */
	int32_t		di_spare[2];	/* 120: used by ext2fs */
};

/*
 * The di_db fields may be overlaid with other information for
 * file types that do not have associated disk storage. Block
 * and character devices overlay the first data block with their
 * cdev_t value. Short symbolic links place their path in the
 * di_db area.
 */
#define	di_inumber	di_u.inumber
#define	di_ogid		di_u.oldids[1]
#define	di_ouid		di_u.oldids[0]
#define	di_rdev		di_db[0]
#define	di_shortlink	di_db
#define	MAXSYMLINKLEN	EXT2_MAXSYMLINKLEN

/* File permissions. */
#define	IEXEC		0000100		/* Executable. */
#define	IWRITE		0000200		/* Writeable. */
#define	IREAD		0000400		/* Readable. */
#define	ISVTX		0001000		/* Sticky bit. */
#define	ISGID		0002000		/* Set-gid. */
#define	ISUID		0004000		/* Set-uid. */

/* File types. */
#define	IFMT		0170000		/* Mask of file type. */
#define	IFIFO		0010000		/* Named pipe (fifo). */
#define	IFCHR		0020000		/* Character device. */
#define	IFDIR		0040000		/* Directory file. */
#define	IFBLK		0060000		/* Block device. */
#define	IFREG		0100000		/* Regular file. */
#define	IFLNK		0120000		/* Symbolic link. */
#define	IFSOCK		0140000		/* UNIX domain socket. */
#define	IFWHT		0160000		/* Whiteout. */

#endif /* !_VFS_GNU_EXT2FS_DINODE_H_ */
