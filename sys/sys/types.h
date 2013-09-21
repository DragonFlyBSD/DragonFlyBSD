/*-
 * Copyright (c) 1982, 1986, 1991, 1993, 1994
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
 *	@(#)types.h	8.6 (Berkeley) 2/19/95
 * $FreeBSD: src/sys/sys/types.h,v 1.40.2.2 2001/04/21 14:53:06 ume Exp $
 */

#ifndef _SYS_TYPES_H_
#define	_SYS_TYPES_H_

#ifndef _SYS_CDEFS_H_
#include <sys/cdefs.h>
#endif
#ifndef _STDINT_H_
#include <stdint.h>
#endif
#include <machine/stdarg.h>
#ifndef _MACHINE_ENDIANT_H_
#include <machine/endian.h>
#endif
#ifndef _MACHINE_TYPES_H_
#include <machine/types.h>
#endif
#ifndef _SYS_STDINT_H_
#include <sys/stdint.h>
#endif
#ifndef _SYS__PTHREADTYPES_H_
#include <sys/_pthreadtypes.h>
#endif

#ifndef _POSIX_SOURCE
typedef	unsigned char	u_char;
typedef	unsigned short	u_short;
typedef	unsigned int	u_int;
typedef	unsigned long	u_long;

typedef	unsigned char	unchar;		/* Sys V compatibility */
typedef	unsigned short	ushort;		/* Sys V compatibility */
typedef	unsigned int	uint;		/* Sys V compatibility */
typedef	unsigned long	ulong;		/* Sys V compatibility */
#endif

typedef __uint8_t	u_int8_t;
typedef __uint16_t	u_int16_t;
typedef __uint32_t	u_int32_t;
typedef __uint64_t	u_int64_t;
typedef	__uint64_t	u_quad_t;	/* quads */
typedef	__int64_t	quad_t;
typedef	quad_t *	qaddr_t;

typedef __int64_t	blkcnt_t;	/* file block count */
typedef __int64_t	blksize_t;	/* block size */
typedef	char *		caddr_t;	/* core address */
typedef	const char *	c_caddr_t;	/* core address, pointer to const */
typedef	volatile char *	v_caddr_t;	/* core address, pointer to volatile */
typedef	__int32_t	daddr_t;	/* disk address */
typedef	__uint32_t	u_daddr_t;	/* unsigned disk address */
typedef	__uint32_t	fixpt_t;	/* fixed point number */
typedef __uint64_t	fsblkcnt_t;	/* filesystem block count */
typedef __uint64_t	fsfilcnt_t;	/* filesystem file count */
#ifndef _GID_T_DECLARED
typedef	__uint32_t	gid_t;		/* group id */
#define	_GID_T_DECLARED
#endif
typedef __int64_t	id_t;		/* general id, can hold gid/pid/uid_t */
typedef	__uint32_t	in_addr_t;	/* base type for internet address */
typedef	__uint16_t	in_port_t;
typedef	__uint64_t	ino_t;		/* inode number */
typedef	long		key_t;		/* IPC key (for Sys V IPC) */
typedef	__uint16_t	mode_t;		/* permissions */
typedef	__uint32_t	nlink_t;	/* link count */
#ifndef _OFF_T_DECLARED
typedef	__off_t		off_t;		/* file offset */
#define _OFF_T_DECLARED
#endif
#ifndef _PID_T_DECLARED
typedef	__pid_t		pid_t;		/* process id */
#define _PID_T_DECLARED
#endif
typedef	__pid_t		lwpid_t;	/* light weight process id */
typedef	quad_t		rlim_t;		/* resource limit */
typedef	__segsz_t	segsz_t;	/* segment size */
#ifndef _UID_T_DECLARED
typedef	__uint32_t	uid_t;		/* user id */
#define	_UID_T_DECLARED
#endif
typedef	long		suseconds_t;	/* microseconds (signed) */
typedef	__uint32_t	useconds_t;	/* microseconds (unsigned) */
typedef	int		mqd_t;		/* message queue descriptor */

#if defined(_KERNEL) || defined(_KERNEL_STRUCTURES)

#ifndef __BOOLEAN_T_DEFINED__
#define __BOOLEAN_T_DEFINED__
typedef	__boolean_t	boolean_t;
#endif

typedef	u_int64_t	uoff_t;

#endif

/*
 * XXX cdev_t has different meanings for userland vs kernel compiles.  What
 * do we do for _KERNEL_STRUCTURES ?  For the moment stick with the userland
 * meaning as being the more compatible solution.
 */

#if defined(_KERNEL) || defined(_KERNEL_STRUCTURES)

struct cdev;

typedef	u_int32_t	udev_t;		/* device number */
typedef struct cdev	*cdev_t;

#endif

/*
 * The kernel now uses only udev_t or cdev_t.  Userland uses dev_t.
 * Virtual kernel builds needs dev_t in order to include userland header
 * files.
 */
#ifdef _KERNEL

#define offsetof(type, field) __offsetof(type, field)
typedef udev_t		dev_t;		/* device number */

#else

typedef	u_int32_t	dev_t;		/* device number */
#define udev_t dev_t

#ifndef _POSIX_SOURCE

/*
 * minor() gives a cookie instead of an index since we don't want to
 * change the meanings of bits 0-15 or waste time and space shifting
 * bits 16-31 for devices that don't use them.
 */
#define major(x)        ((int)(((u_int)(x) >> 8)&0xff)) /* major number */
#define minor(x)        ((int)((x)&0xffff00ff))         /* minor number */
#define makedev(x,y)    ((dev_t)(((x) << 8) | (y)))     /* create dev_t */

#endif /* _POSIX_SOURCE */

#endif /* !_KERNEL */

#ifndef _CLOCK_T_DECLARED
#define _CLOCK_T_DECLARED
typedef	__clock_t	clock_t;
#endif

#ifndef _CLOCKID_T_DECLARED
#define _CLOCKID_T_DECLARED
typedef __clockid_t	clockid_t;
#endif

#ifndef _SIZE_T_DECLARED
#define _SIZE_T_DECLARED
typedef __size_t	size_t;
#endif

#ifndef _SSIZE_T_DECLARED
#define _SSIZE_T_DECLARED
typedef __ssize_t	ssize_t;
#endif

#ifndef _TIME_T_DECLARED
#define _TIME_T_DECLARED
typedef __time_t	time_t;
#endif

#ifndef _TIMER_T_DECLARED
#define _TIMER_T_DECLARED
typedef __timer_t	timer_t;
#endif

#ifdef __BSD_VISIBLE

#include <sys/fd_set.h>
#include <sys/_timeval.h>

#define NBBY 8		/* number of bits in a byte */
/*
 * These declarations belong elsewhere, but are repeated here and in
 * <stdio.h> to give broken programs a better chance of working with
 * 64-bit off_t's.
 */
#ifndef _KERNEL
__BEGIN_DECLS
#ifndef _FTRUNCATE_DECLARED
#define	_FTRUNCATE_DECLARED
int	 ftruncate (int, off_t);
#endif
#ifndef _LSEEK_DECLARED
#define	_LSEEK_DECLARED
off_t	 lseek (int, off_t, int);
#endif
#ifndef _MMAP_DECLARED
#define	_MMAP_DECLARED
void *	 mmap (void *, size_t, int, int, int, off_t);
#endif
#ifndef _TRUNCATE_DECLARED
#define	_TRUNCATE_DECLARED
int	 truncate (const char *, off_t);
#endif
__END_DECLS
#endif /* !_KERNEL */

#endif /* __BSD_VISIBLE */

/*
 * rune_t is declared to be an ``int'' instead of the more natural
 * ``unsigned long'' or ``long''.  Two things are happening here.  It is not
 * unsigned so that EOF (-1) can be naturally assigned to it and used.  Also,
 * it looks like 10646 will be a 31 bit standard.  This means that if your
 * ints cannot hold 32 bits, you will be in trouble.  The reason an int was
 * chosen over a long is that the is*() and to*() routines take ints (says
 * ANSI C), but they use __ct_rune_t instead of int.
 *
 * NOTE: rune_t is not covered by ANSI nor other standards, and should not
 * be instantiated outside of lib/libc/locale.  Use wchar_t.  wint_t and
 * rune_t must be the same type.  Also, wint_t should be able to hold all
 * members of the largest character set plus one extra value (WEOF), and
 * must be at least 16 bits.
 */
typedef	int		__ct_rune_t;	/* arg type for ctype funcs */
typedef	__ct_rune_t	__rune_t;	/* rune_t (see above) */
typedef	__ct_rune_t	__wint_t;	/* wint_t (see above) */

/*
 * Also required for locale support
 */
typedef int		__nl_item;

/* Clang already provides these types as built-ins, but only in c++ mode. */
#if !defined(__clang__) || !defined(__cplusplus)
typedef __uint_least16_t	__char16_t;
typedef __uint_least32_t	__char32_t;
#endif

#endif /* !_SYS_TYPES_H_ */
