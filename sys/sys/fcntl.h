/*-
 * Copyright (c) 1983, 1990, 1993
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
 *	@(#)fcntl.h	8.3 (Berkeley) 1/21/94
 * $FreeBSD: src/sys/sys/fcntl.h,v 1.9.2.2 2001/06/03 05:00:10 dillon Exp $
 */

#ifndef _SYS_FCNTL_H_
#define	_SYS_FCNTL_H_

#include <sys/cdefs.h>
#ifndef _SYS_TYPES_H_
#include <sys/types.h>
#endif

/*
 * File status flags: these are used by open(2), fcntl(2).
 * They are also used (indirectly) in the kernel file structure f_flags,
 * which is a superset of the open/fcntl flags.  Open flags and f_flags
 * are inter-convertible using OFLAGS(fflags) and FFLAGS(oflags).
 * Open/fcntl flags begin with O_; kernel-internal flags begin with F.
 */
/* open-only flags */
#define	O_RDONLY	0x0000		/* open for reading only */
#define	O_WRONLY	0x0001		/* open for writing only */
#define	O_RDWR		0x0002		/* open for reading and writing */
#define	O_ACCMODE	0x0003		/* mask for above modes */

/*
 * Kernel encoding of open mode; separate read and write bits that are
 * independently testable: 1 greater than the above.
 *
 * XXX
 * FREAD and FWRITE are excluded from the #ifdef _KERNEL so that TIOCFLUSH,
 * which was documented to use FREAD/FWRITE, continues to work.
 */
#if __BSD_VISIBLE
#define	FREAD		0x0001
#define	FWRITE		0x0002
#endif
#define	O_NONBLOCK	0x0004		/* no delay */
#define	O_APPEND	0x0008		/* set append mode */
#if __BSD_VISIBLE
#define	O_SHLOCK	0x0010		/* open with shared file lock */
#define	O_EXLOCK	0x0020		/* open with exclusive file lock */
#define	O_ASYNC		0x0040		/* signal pgrp when data ready */
#define	O_FSYNC		0x0080		/* synchronous writes */
#endif
#define	O_SYNC		0x0080		/* Same as O_FSYNC, but POSIX */
#if __POSIX_VISIBLE >= 200809
#define	O_NOFOLLOW	0x0100		/* don't follow symlinks */
#endif
#define	O_CREAT		0x0200		/* create if nonexistent */
#define	O_TRUNC		0x0400		/* truncate to zero length */
#define	O_EXCL		0x0800		/* error if already exists */
#if defined(_KERNEL) || defined(_KERNEL_STRUCTURES)
#define	FMARK		0x1000		/* mark during gc() */
#define	FDEFER		0x2000		/* defer for next gc pass */
#define	FHASLOCK	0x4000		/* descriptor holds advisory lock */
#endif

/* Defined by POSIX 1003.1; BSD default, but must be distinct from O_RDONLY. */
#define	O_NOCTTY	0x8000		/* don't assign controlling terminal */

#if __BSD_VISIBLE
/* Attempt to bypass the buffer cache */
#define	O_DIRECT	0x00010000
#endif

#if __POSIX_VISIBLE >= 200809
#define	O_CLOEXEC	0x00020000	/* atomically set FD_CLOEXEC */
#endif
#define	O_FBLOCKING	0x00040000	/* force blocking I/O */
#define	O_FNONBLOCKING	0x00080000	/* force non-blocking I/O */
#define	O_FAPPEND	0x00100000	/* force append mode for write */
#define	O_FOFFSET	0x00200000	/* force specific offset */
#define	O_FSYNCWRITE	0x00400000	/* force synchronous write */
#define	O_FASYNCWRITE	0x00800000	/* force asynchronous write */
#define	FCDEVPRIV	0x01000000	/* f_data1 used for cdevpriv */
#define	O_UNUSED25	0x02000000
#define	O_MAPONREAD	0x04000000	/* memory map read buffer */

#if __POSIX_VISIBLE >= 200809
#define	O_DIRECTORY	0x08000000	/* error if not a directory */
#endif

#if defined(_KERNEL) || defined(_KERNEL_STRUCTURES)
#define	FREVOKED	0x10000000	/* revoked by fdrevoke() */
#define	FAPPENDONLY	0x20000000	/* O_APPEND cannot be changed */
#define	FOFFSETLOCK	0x40000000	/* f_offset locked */
#define	FOFFSETWAKE	0x80000000	/* f_offset wakeup */
#endif

#define	O_FMASK		(O_FBLOCKING|O_FNONBLOCKING|O_FAPPEND|O_FOFFSET|\
			 O_FSYNCWRITE|O_FASYNCWRITE|O_MAPONREAD)

#ifdef _KERNEL
/* convert from open() flags to/from fflags; convert O_RD/WR to FREAD/FWRITE */
#define	FFLAGS(oflags)	((oflags) + 1)
#define	OFLAGS(fflags)	((fflags) - 1)

/*
 * Bits to save after open from the ap.  Remaining bits are retained.
 */
#define	FMASK		(FREAD|FWRITE|FAPPEND|FASYNC|FFSYNC|FNONBLOCK|\
			 FAPPENDONLY|FREVOKED|O_DIRECT|O_MAPONREAD)
/* bits settable by fcntl(F_SETFL, ...) */
#define	FCNTLFLAGS	(FAPPEND|FASYNC|FFSYNC|FNONBLOCK|FPOSIXSHM|\
			 O_DIRECT|O_MAPONREAD)
#endif

/*
 * The O_* flags used to have only F* names, which were used in the kernel
 * and by fcntl.  We retain the F* names for the kernel f_flag field
 * and for backward compatibility for fcntl.
 */
#if __BSD_VISIBLE
#define	FAPPEND		O_APPEND	/* kernel/compat */
#define	FASYNC		O_ASYNC		/* kernel/compat */
#define	FFSYNC		O_FSYNC		/* kernel */
#define	FNONBLOCK	O_NONBLOCK	/* kernel */
#define	FNDELAY		O_NONBLOCK	/* compat */
#define	O_NDELAY	O_NONBLOCK	/* compat */
#endif

/*
 * We are out of bits in f_flag (which is a short).  However,
 * the flag bits not set in FMASK are only meaningful in the
 * initial open syscall.  Those bits can thus be given a
 * different meaning for fcntl(2).
 */
#if __BSD_VISIBLE

/*
 * Set by shm_open(3) to get automatic MAP_ASYNC behavior
 * for POSIX shared memory objects (which are otherwise
 * implemented as plain files).
 */
#define	FPOSIXSHM	O_NOFOLLOW
#endif

#if __POSIX_VISIBLE >= 200809
/*
 * Constants used by "at" family of system calls.
 */
#define	AT_FDCWD		0xFFFAFDCD	/* invalid file descriptor */
#define	AT_SYMLINK_NOFOLLOW	1
#define	AT_REMOVEDIR		2
#define	AT_EACCESS		4
#define	AT_SYMLINK_FOLLOW	8
#endif

/*
 * Constants used for fcntl(2)
 */

/* command values */
#define	F_DUPFD		0		/* duplicate file descriptor */
#define	F_GETFD		1		/* get file descriptor flags */
#define	F_SETFD		2		/* set file descriptor flags */
#define	F_GETFL		3		/* get file status flags */
#define	F_SETFL		4		/* set file status flags */
#if __XSI_VISIBLE || __POSIX_VISIBLE >= 200112
#define	F_GETOWN	5		/* get SIGIO/SIGURG proc/pgrp */
#define	F_SETOWN	6		/* set SIGIO/SIGURG proc/pgrp */
#endif
#define	F_GETLK		7		/* get record locking information */
#define	F_SETLK		8		/* set record locking information */
#define	F_SETLKW	9		/* F_SETLK; wait if blocked */
#if __BSD_VISIBLE
#define	F_DUP2FD	10		/* duplicate file descriptor to arg */
#endif
#if __POSIX_VISIBLE >= 200809
#define	F_DUPFD_CLOEXEC	17		/* Like F_DUPFD with FD_CLOEXEC set */
#endif
#if __BSD_VISIBLE
#define	F_DUP2FD_CLOEXEC 18		/* Like F_DUP2FD with FD_CLOEXEC set */
#endif

/* file descriptor flags (F_GETFD, F_SETFD) */
#define	FD_CLOEXEC	1		/* close-on-exec flag */

/* record locking flags (F_GETLK, F_SETLK, F_SETLKW) */
#define	F_RDLCK		1		/* shared or read lock */
#define	F_UNLCK		2		/* unlock */
#define	F_WRLCK		3		/* exclusive or write lock */
#if defined(_KERNEL) || defined(_KERNEL_STRUCTURES)
#define	F_WAIT		0x010		/* Wait until lock is granted */
#define	F_UNUSED020	0x020
#define	F_POSIX		0x040	 	/* Use POSIX semantics for lock */
#define	F_NOEND		0x080		/* l_len = 0, internally used */
#endif


/*
 * Advisory file segment locking data type -
 * information passed to system by user
 */
struct flock {
	off_t	l_start;	/* starting offset */
	off_t	l_len;		/* len = 0 means until end of file */
	pid_t	l_pid;		/* lock owner */
	short	l_type;		/* lock type: read/write, etc. */
	short	l_whence;	/* type of l_start */
};

#ifdef _KERNEL
union fcntl_dat {
	int		fc_fd;		/* F_DUPFD */
	int		fc_cloexec;	/* F_GETFD/F_SETFD */
	int		fc_flags;	/* F_GETFL/F_SETFL */
	int		fc_owner;	/* F_GETOWN/F_SETOWN */
	struct flock	fc_flock;	/* F_GETLK/F_SETLK */
};
#endif /* _KERNEL */


#if __BSD_VISIBLE
/* lock operations for flock(2) */
#define	LOCK_SH		0x01		/* shared file lock */
#define	LOCK_EX		0x02		/* exclusive file lock */
#define	LOCK_NB		0x04		/* don't block when locking */
#define	LOCK_UN		0x08		/* unlock file */
#endif

#if !defined(_KERNEL) || defined(_KERNEL_VIRTUAL)
__BEGIN_DECLS
int	open(const char *, int, ...);
#if __POSIX_VISIBLE >= 200809
int	openat(int, const char *, int, ...);
#endif
int	creat(const char *, mode_t);
int	fcntl(int, int, ...);
#if __BSD_VISIBLE
int	flock(int, int);
#endif /* !_POSIX_SOURCE */
__END_DECLS
#endif

#endif /* !_SYS_FCNTL_H_ */
