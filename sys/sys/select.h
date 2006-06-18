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
 * $DragonFly: src/sys/sys/select.h,v 1.9 2006/06/18 01:00:35 corecode Exp $
 */

#ifndef _SYS_SELECT_H_
#define	_SYS_SELECT_H_

#include <sys/cdefs.h>

#ifndef _SYS_SIGNAL_H_
#include <sys/signal.h>
#endif
#ifndef _SYS_TIME_H_
#include <sys/time.h>
#endif

/*
 * Select uses bit masks of file descriptors in longs.  These macros
 * manipulate such bit fields (the filesystem macros use chars).
 * FD_SETSIZE may be defined by the user, but the default here should
 * be enough for most uses.
 */
#ifndef FD_SETSIZE
#define FD_SETSIZE	1024
#endif

#ifndef NBBY
#define NBBY		8
#endif

typedef unsigned long   fd_mask;
#define NFDBITS (sizeof(fd_mask) * NBBY)	/* bits per mask */

#ifndef howmany
#define howmany(x, y)	(((x) + ((y) - 1)) / (y))
#endif

typedef struct fd_set {
	fd_mask fds_bits[howmany(FD_SETSIZE, NFDBITS)];
} fd_set;

#define _fdset_mask(n)	((fd_mask)1 << ((n) % NFDBITS))
#define FD_SET(n, p)	((p)->fds_bits[(n)/NFDBITS] |= _fdset_mask(n))
#define FD_CLR(n, p)	((p)->fds_bits[(n)/NFDBITS] &= ~_fdset_mask(n))
#define FD_ISSET(n, p)	((p)->fds_bits[(n)/NFDBITS] & _fdset_mask(n))
#define FD_COPY(f, t)	bcopy(f, t, sizeof(*(f)))
#define FD_ZERO(p)	bzero(p, sizeof(*(p)))

__BEGIN_DECLS
#ifndef _SELECT_DECLARED
#define _SELECT_DECLARED
struct timeval;
int	select(int, fd_set * __restrict, fd_set * __restrict,
	       fd_set * __restrict, struct timeval * __restrict);
#endif
__END_DECLS

#endif	/* !_SYS_SELECT_H_ */
