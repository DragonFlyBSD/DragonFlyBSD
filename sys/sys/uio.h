/*
 * Copyright (c) 1982, 1986, 1993, 1994
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
 *	@(#)uio.h	8.5 (Berkeley) 2/22/94
 * $FreeBSD: src/sys/sys/uio.h,v 1.11.2.1 2001/09/28 16:58:35 dillon Exp $
 */

#ifndef _SYS_UIO_H_
#define	_SYS_UIO_H_

#include <sys/cdefs.h>
#include <sys/_iovec.h>
#include <machine/stdint.h>

#if __BSD_VISIBLE
#ifndef _OFF_T_DECLARED
typedef	__off_t		off_t;
#define	_OFF_T_DECLARED
#endif
#endif

/* The size_t is expected to be provided by <sys/_iovec.h> */

#ifndef _SSIZE_T_DECLARED
typedef	__ssize_t	ssize_t;
#define	_SSIZE_T_DECLARED
#endif

#if defined(_KERNEL) || defined(_KERNEL_STRUCTURES)
#include <sys/_uio.h>			/* struct uio and enums */

/*
 * Limits
 */
#define UIO_MAXIOV	1024		/* max 1K of iov's */
#define UIO_SMALLIOV	8		/* 8 on stack, else malloc */

#endif /* _KERNEL || _KERNEL_STRUCTURES */

#if defined(_KERNEL)
struct buf;
struct vm_object;
struct vm_page;

int	uiomove (caddr_t, size_t, struct uio *);
int	uiomove_nofault (caddr_t, size_t, struct uio *);
int	uiomovebp (struct buf *, caddr_t, size_t, struct uio *);
int	uiomovez (size_t, struct uio *);
int 	uiomove_frombuf (void *buf, size_t buflen, struct uio *uio);
int     uiomove_fromphys(struct vm_page *ma[], vm_offset_t offset,
			    size_t n, struct uio *uio);
int	uioread (int, struct uio *, struct vm_object *, int *);
int	iovec_copyin(const struct iovec *, struct iovec **, struct iovec *,
			    int, size_t *);

#ifdef MALLOC_DECLARE
MALLOC_DECLARE(M_IOV);	/* only if <sys/malloc.h> included before! */
#endif

#ifdef MALLOC_DEFINE
#include <sys/_null.h>			/* XXX, visibility */
/*
 * MPSAFE
 */
static __inline void
iovec_free(struct iovec **kiov, struct iovec *siov)
{
	if (*kiov != siov) {
		kfree(*kiov, M_IOV);
		*kiov = NULL;
	}
}
#endif
#endif /* _KERNEL */

#ifndef _KERNEL
__BEGIN_DECLS
ssize_t	readv(int, const struct iovec *, int);
ssize_t	writev(int, const struct iovec *, int);
#if __BSD_VISIBLE
ssize_t	preadv(int, const struct iovec *, int, off_t);
ssize_t	pwritev(int, const struct iovec *, int, off_t);
#endif
__END_DECLS
#endif /* !_KERNEL */

#endif /* !_SYS_UIO_H_ */
