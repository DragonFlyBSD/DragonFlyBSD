/*-
 * Copyright (c) 1992, 1993
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
 *
 *	@(#)select.h	8.2 (Berkeley) 1/4/94
 * $FreeBSD: src/sys/sys/select.h,v 1.6.2.1 2000/05/05 03:50:02 jlemon Exp $
 */

#ifndef _SYS_FD_SET_H_
#define	_SYS_FD_SET_H_

/*
 * Select uses bit masks of file descriptors in longs.  These macros
 * manipulate such bit fields (the filesystem macros use chars).
 * FD_SETSIZE may be defined by the user, but the default here should
 * be enough for most uses.
 */
#ifndef FD_SETSIZE
#define FD_SETSIZE	1024
#endif

#define	__NBBY		8		/* number of bits in a byte */

typedef unsigned long   __fd_mask;
#define __NFDBITS ((unsigned int)sizeof(__fd_mask) * __NBBY)	/* bits per mask */

#ifndef __howmany
#define __howmany(x, y)	(((x) + ((y) - 1)) / (y))
#endif

typedef struct fd_set {
	__fd_mask fds_bits[__howmany(FD_SETSIZE, __NFDBITS)];
} fd_set;

#define _fdset_mask(n)	((__fd_mask)1 << ((n) % __NFDBITS))
#define FD_SET(n, p)	((p)->fds_bits[(n)/__NFDBITS] |= _fdset_mask(n))
#define FD_CLR(n, p)	((p)->fds_bits[(n)/__NFDBITS] &= ~_fdset_mask(n))
#define FD_ISSET(n, p)	((p)->fds_bits[(n)/__NFDBITS] & _fdset_mask(n))
#define FD_ZERO(p)	__builtin_memset((p), 0, sizeof(*(p)))


/*
 * Expose classic BSD names if we're not running in conformance mode.
 */
#if __BSD_VISIBLE

#define fd_mask	__fd_mask
#define NFDBITS	__NFDBITS
#ifndef howmany
#define howmany(a, b)	__howmany(a, b)
#endif

#define FD_COPY(f, t)	__builtin_memcpy((t), (f), sizeof(*(f)))

#endif /* __BSD_VISIBLE */

#endif /* _SYS_FD_SET_H_ */
