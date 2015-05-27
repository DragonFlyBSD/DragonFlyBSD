/*-
 * Copyright (c) 1982, 1986, 1989, 1993
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
 * 3. Neither the name of the University nor the names of its contributors
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
 *	@(#)stat.h	8.12 (Berkeley) 6/16/95
 * $FreeBSD: src/sys/sys/stat.h,v 1.20 1999/12/29 04:24:47 peter Exp $
 */

#ifndef _SYS_STAT_H_
#define	_SYS_STAT_H_

#if !defined(_POSIX_SOURCE)
/*
 * XXX we need this for struct timespec.  We get miscellaneous namespace
 * pollution with it.
 */
#ifndef _SYS_TIME_H_
#include <sys/time.h>
#endif
#else	/* !_POSIX_SOURCE */
#include <sys/_timespec.h>
#endif	/* _POSIX_SOURCE */

#ifdef _KERNEL
#define	__dev_t	udev_t
#else
#define	__dev_t	dev_t
#endif

/*
 * stat structure notes:
 *
 * NOTE: st_fsmid removed in DragonFly 2.5.x.
 */
struct stat {
	ino_t	  st_ino;		/* inode's number */
	nlink_t	  st_nlink;		/* number of hard links */
	__dev_t	  st_dev;		/* inode's device */
	mode_t	  st_mode;		/* inode protection mode */
	uint16_t  st_padding1;
	uid_t	  st_uid;		/* user ID of the file's owner */
	gid_t	  st_gid;		/* group ID of the file's group */
	__dev_t	  st_rdev;		/* device type */
	struct	timespec st_atim;	/* time of last access */
	struct	timespec st_mtim;	/* time of last data modification */
	struct	timespec st_ctim;	/* time of last file status change */
	off_t	  st_size;		/* file size, in bytes */
	int64_t	  st_blocks;		/* blocks allocated for file */
	u_int32_t st_blksize;		/* optimal blocksize for I/O */
	u_int32_t st_flags;		/* user defined flags for file */
	u_int32_t st_gen;		/* file generation number */
	int32_t	  st_lspare;
	int64_t   st_qspare1;		/* was recursive change detect */
	int64_t	  st_qspare2;
};

/*#define _ST_FSMID_PRESENT_*/
#define	_ST_FLAGS_PRESENT_

#undef __dev_t

#define	st_atime st_atim.tv_sec
#define	st_mtime st_mtim.tv_sec
#define	st_ctime st_ctim.tv_sec

/* BSD compatibility */
#ifndef _POSIX_SOURCE
#define	st_atimespec st_atim
#define	st_mtimespec st_mtim
#define	st_ctimespec st_ctim
#endif

#define	S_ISUID	0004000			/* set user id on execution */
#define	S_ISGID	0002000			/* set group id on execution */
#ifndef _POSIX_SOURCE
#define	S_ISTXT	0001000			/* sticky bit */
#endif

#define	S_IRWXU	0000700			/* RWX mask for owner */
#define	S_IRUSR	0000400			/* R for owner */
#define	S_IWUSR	0000200			/* W for owner */
#define	S_IXUSR	0000100			/* X for owner */

#ifndef _POSIX_SOURCE
#define	S_IREAD		S_IRUSR
#define	S_IWRITE	S_IWUSR
#define	S_IEXEC		S_IXUSR
#endif

#define	S_IRWXG	0000070			/* RWX mask for group */
#define	S_IRGRP	0000040			/* R for group */
#define	S_IWGRP	0000020			/* W for group */
#define	S_IXGRP	0000010			/* X for group */

#define	S_IRWXO	0000007			/* RWX mask for other */
#define	S_IROTH	0000004			/* R for other */
#define	S_IWOTH	0000002			/* W for other */
#define	S_IXOTH	0000001			/* X for other */

#ifndef _POSIX_SOURCE
#define	S_IFMT	 0170000		/* type of file mask */
#define	S_IFIFO	 0010000		/* named pipe (fifo) */
#define	S_IFCHR	 0020000		/* character special */
#define	S_IFDIR	 0040000		/* directory */
#define	S_IFBLK	 0060000		/* block special */
#define	S_IFREG	 0100000		/* regular */
#define	S_IFDB	 0110000		/* record access file */
#define	S_IFLNK	 0120000		/* symbolic link */
#define	S_IFSOCK 0140000		/* socket */
#define	S_IFWHT  0160000		/* whiteout */
#define	S_ISVTX	 0001000		/* save swapped text even after use */
#endif

#define	S_ISDIR(m)	(((m) & 0170000) == 0040000)	/* directory */
#define	S_ISCHR(m)	(((m) & 0170000) == 0020000)	/* char special */
#define	S_ISBLK(m)	(((m) & 0170000) == 0060000)	/* block special */
#define	S_ISREG(m)	(((m) & 0170000) == 0100000)	/* regular file */
#define	S_ISDB(m)	(((m) & 0170000) == 0110000)	/* record access file */
#define	S_ISFIFO(m)	(((m) & 0170000) == 0010000)	/* fifo or socket */
#ifndef _POSIX_SOURCE
#define	S_ISLNK(m)	(((m) & 0170000) == 0120000)	/* symbolic link */
#define	S_ISSOCK(m)	(((m) & 0170000) == 0140000)	/* socket */
#define	S_ISWHT(m)	(((m) & 0170000) == 0160000)	/* whiteout */
#endif

/*
 * The value of `buf' is a pointer to a `stat' data structure. Since we don't
 * implement message queues as distinct file types, the following macro
 * evaluates to zero.
 * XXX: What about semaphores and shared memory objects ?
 */
#define	S_TYPEISMQ(buf)		(0)	/* message queue */

#ifndef _POSIX_SOURCE
#define	ACCESSPERMS	(S_IRWXU|S_IRWXG|S_IRWXO)	/* 0777 */
							/* 7777 */
#define	ALLPERMS	(S_ISUID|S_ISGID|S_ISTXT|S_IRWXU|S_IRWXG|S_IRWXO)
							/* 0666 */
#define	DEFFILEMODE	(S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH)

#define	S_BLKSIZE	512		/* block size used in the stat struct */

/*
 * Definitions of flags stored in file flags word.
 *
 * Super-user and owner changeable flags.
 */
#define	UF_SETTABLE	0x0000ffff	/* mask of owner changeable flags */
#define	UF_NODUMP	0x00000001	/* do not dump file */
#define	UF_IMMUTABLE	0x00000002	/* file may not be changed */
#define	UF_APPEND	0x00000004	/* writes to file may only append */
#define	UF_OPAQUE	0x00000008	/* directory is opaque wrt. union */
#define	UF_NOUNLINK	0x00000010	/* file may not be removed or renamed */
#define	UF_FBSDRSVD20	0x00000020	/* (unused) */
#define	UF_NOHISTORY	0x00000040	/* do not retain history/snapshots */
#define	UF_CACHE	0x00000080	/* enable data swapcache */
/*
 * Super-user changeable flags.
 */
#define	SF_SETTABLE	0xffff0000	/* mask of superuser changeable flags */
#define	SF_ARCHIVED	0x00010000	/* file is archived */
#define	SF_IMMUTABLE	0x00020000	/* file may not be changed */
#define	SF_APPEND	0x00040000	/* writes to file may only append */
#define	SF_NOUNLINK	0x00100000	/* file may not be removed or renamed */
#define	SF_FBSDRSVD20	0x00200000	/* (used by FreeBSD for snapshots) */
#define	SF_NOHISTORY	0x00400000	/* do not retain history/snapshots */
#define	SF_NOCACHE	0x00800000	/* disable data swapcache */

#ifdef _KERNEL
/*
 * Shorthand abbreviations of above.
 */
#define	OPAQUE		(UF_OPAQUE)
#define	APPEND		(UF_APPEND | SF_APPEND)
#define	IMMUTABLE	(UF_IMMUTABLE | SF_IMMUTABLE)
#define	NOUNLINK	(UF_NOUNLINK | SF_NOUNLINK)
#endif

#endif /* !_POSIX_SOURCE */

#if __POSIX_VISIBLE >= 200809
#define	UTIME_NOW	-1
#define	UTIME_OMIT	-2
#endif

#if !defined(_KERNEL) || defined(_KERNEL_VIRTUAL)
#include <sys/cdefs.h>

__BEGIN_DECLS
int	chmod(const char *, mode_t);
#if __POSIX_VISIBLE >= 200809
int	fchmodat(int, const char *, mode_t, int);
int	utimensat(int, const char *, const struct timespec *, int);
#endif
int	fstat(int, struct stat *);
int	mkdir(const char *, mode_t);
int	mkfifo(const char *, mode_t);
#if !defined(_MKNOD_DECLARED) && __XSI_VISIBLE
int	mknod(const char *, mode_t, dev_t);
#define	_MKNOD_DECLARED
#endif
int	stat(const char *, struct stat *);
mode_t	umask(mode_t);
#if __POSIX_VISIBLE >= 200809
int	fstatat(int, const char *, struct stat *, int);
int	mkdirat(int, const char *, mode_t);
int	mkfifoat(int, const char *, mode_t);
#endif
#if __XSI_VISIBLE >= 700
int	mknodat(int, const char *, mode_t, dev_t);
#endif

#ifndef _POSIX_SOURCE
int	chflags(const char *, u_long);
int	fchflags(int, u_long);
int	lchflags(const char *, u_long);
int	chflagsat(int, const char *, u_long, int);
int	fchmod(int, mode_t);
int	lchmod(const char *, mode_t);
int	lstat(const char *, struct stat *);
#endif
__END_DECLS

#endif

#endif /* !_SYS_STAT_H_ */
