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
 *	@(#)types.h	8.6 (Berkeley) 2/19/95
 * $FreeBSD: src/sys/sys/types.h,v 1.40.2.2 2001/04/21 14:53:06 ume Exp $
 */

#ifndef _SYS_TYPES_H_
#define	_SYS_TYPES_H_

#ifndef _SYS_CDEFS_H_
#include <sys/cdefs.h>
#endif
#include <machine/endian.h>
#include <machine/stdint.h>

#if __BSD_VISIBLE
typedef	unsigned char	u_char;
typedef	unsigned short	u_short;
typedef	unsigned int	u_int;
typedef	unsigned long	u_long;

typedef	unsigned char	unchar;		/* Sys V compatibility */
typedef	unsigned short	ushort;		/* Sys V compatibility */
typedef	unsigned int	uint;		/* Sys V compatibility */
typedef	unsigned long	ulong;		/* Sys V compatibility */
#endif

typedef	__uint8_t	u_int8_t;
typedef	__uint16_t	u_int16_t;
typedef	__uint32_t	u_int32_t;
typedef	__uint64_t	u_int64_t;
typedef	__uint64_t	u_quad_t;	/* quads */
typedef	__int64_t	quad_t;
typedef	quad_t *	qaddr_t;

#ifndef _BLKCNT_T_DECLARED
typedef	__int64_t	blkcnt_t;	/* file block count */
#define	_BLKCNT_T_DECLARED
#endif
#ifndef _BLKSIZE_T_DECLARED
typedef	__int64_t	blksize_t;	/* block size */
#define	_BLKSIZE_T_DECLARED
#endif
typedef	char *		caddr_t;	/* core address */
typedef	const char *	c_caddr_t;	/* core address, pointer to const */
typedef	volatile char *	v_caddr_t;	/* core address, pointer to volatile */
typedef	__int32_t	daddr_t;	/* disk address */
typedef	__uint32_t	u_daddr_t;	/* unsigned disk address */
typedef	__uint32_t	fixpt_t;	/* fixed point number */
#ifndef _DEV_T_DECLARED
typedef	__uint32_t	dev_t;		/* device number */
#define	_DEV_T_DECLARED
#endif
#ifndef _FSBLKCNT_T_DECLARED
typedef	__uint64_t	fsblkcnt_t;	/* filesystem block count */
#define	_FSBLKCNT_T_DECLARED
#endif
#ifndef _FSFILCNT_T_DECLARED
typedef	__uint64_t	fsfilcnt_t;	/* filesystem file count */
#define	_FSFILCNT_T_DECLARED
#endif
#ifndef _GID_T_DECLARED
typedef	__uint32_t	gid_t;		/* group id */
#define	_GID_T_DECLARED
#endif
#ifndef _ID_T_DECLARED
typedef	__int64_t	id_t;		/* general id, can hold gid/pid/uid_t */
#define	_ID_T_DECLARED
#endif
#ifndef _IN_ADDR_T_DECLARED
typedef	__uint32_t	in_addr_t;	/* base type for internet address */
#define	_IN_ADDR_T_DECLARED
#endif
#ifndef _IN_PORT_T_DECLARED
typedef	__uint16_t	in_port_t;
#define	_IN_PORT_T_DECLARED
#endif
#ifndef _INO_T_DECLARED
typedef	__uint64_t	ino_t;		/* inode number */
#define	_INO_T_DECLARED
#endif
#ifndef _KEY_T_DECLARED
typedef	long		key_t;		/* IPC key (for Sys V IPC) */
#define	_KEY_T_DECLARED
#endif
#ifndef _MODE_T_DECLARED
typedef	__uint16_t	mode_t;		/* permissions */
#define	_MODE_T_DECLARED
#endif
#ifndef _NLINK_T_DECLARED
typedef	__uint32_t	nlink_t;	/* link count */
#define	_NLINK_T_DECLARED
#endif
#ifndef _OFF_T_DECLARED
typedef	__off_t		off_t;		/* file offset */
#define	_OFF_T_DECLARED
#endif
#ifndef _PID_T_DECLARED
typedef	__pid_t		pid_t;		/* process id */
#define	_PID_T_DECLARED
#endif
#if __BSD_VISIBLE
typedef	__register_t	register_t;	/* register-sized type */
#ifndef _RLIM_T_DECLARED
typedef	__rlim_t	rlim_t;		/* resource limit */
#define	_RLIM_T_DECLARED
#endif
typedef	__intlp_t	segsz_t;	/* segment size */
#endif
#ifndef _SUSECONDS_T_DECLARED
typedef	__suseconds_t	suseconds_t;	/* microseconds (signed) */
#define	_SUSECONDS_T_DECLARED
#endif
#ifndef _UID_T_DECLARED
typedef	__uint32_t	uid_t;		/* user id */
#define	_UID_T_DECLARED
#endif
typedef	__uint32_t	useconds_t;	/* microseconds (unsigned) */

/*
 * The kernel uses dev_t or cdev_t.  Userland uses dev_t.
 * Virtual kernel builds needs dev_t in order to include userland headers.
 */
#if defined(_KERNEL) || defined(_KERNEL_STRUCTURES)
struct cdev;
typedef	struct cdev	*cdev_t;
typedef	u_int64_t	uoff_t;		/* uio offset */
#endif /* _KERNEL || _KERNEL_STRUCTURES */

#if __BSD_VISIBLE && !defined(_KERNEL)
/*
 * minor() gives a cookie instead of an index since we don't want to
 * change the meanings of bits 0-15 or waste time and space shifting
 * bits 16-31 for devices that don't use them.
 */
#define	major(x)	((int)(((u_int)(x) >> 8)&0xff)) /* major number */
#define	minor(x)	((int)((x)&0xffff00ff))         /* minor number */
#define	makedev(x,y)	((dev_t)(((x) << 8) | (y)))     /* create dev_t */
#endif /* __BSD_VISIBLE && !_KERNEL */

#ifndef _CLOCK_T_DECLARED
#define	_CLOCK_T_DECLARED
typedef	__clock_t	clock_t;
#endif

#ifndef _CLOCKID_T_DECLARED
#define	_CLOCKID_T_DECLARED
typedef	__clockid_t	clockid_t;
#endif

#if __BSD_VISIBLE
#ifndef _LWPID_T_DECLARED
#define	_LWPID_T_DECLARED
typedef	__pid_t		lwpid_t;	/* light weight process id */
#endif
#endif

#ifndef _SIZE_T_DECLARED
#define	_SIZE_T_DECLARED
typedef	__size_t	size_t;		/* _GCC_SIZE_T OK */
#endif

#ifndef _SSIZE_T_DECLARED
#define	_SSIZE_T_DECLARED
typedef	__ssize_t	ssize_t;
#endif

#ifndef _TIME_T_DECLARED
#define	_TIME_T_DECLARED
typedef	__time_t	time_t;
#endif

#ifndef _TIMER_T_DECLARED
#define	_TIMER_T_DECLARED
typedef	__timer_t	timer_t;
#endif

#ifndef _KERNEL
#ifndef _STDINT_H_
#include <stdint.h>			/* XXX */
#endif
#endif /* !_KERNEL */

#if defined(_KERNEL) || defined(_KERNEL_STRUCTURES)
#ifndef _SYS_STDINT_H_
#include <sys/stdint.h>			/* kernel int types */
#endif
#include <machine/types.h>		/* for vm_offet_t */
#endif

#if __BSD_VISIBLE
#include <sys/_fd_set.h>
#include <sys/_timeval.h>
#endif /* __BSD_VISIBLE */

#include <sys/_pthreadtypes.h>		/* now POSIX thread types */

#endif /* !_SYS_TYPES_H_ */
