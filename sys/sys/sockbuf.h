/*-
 * Copyright (c) 1982, 1986, 1990, 1993
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
 * @(#)socketvar.h	8.3 (Berkeley) 2/19/95
 * $FreeBSD: src/sys/sys/socketvar.h,v 1.46.2.10 2003/08/24 08:24:39 hsu Exp $
 * $DragonFly: src/sys/sys/sockbuf.h,v 1.1 2007/04/22 01:13:17 dillon Exp $
 */

#ifndef _SYS_SOCKBUF_H_
#define _SYS_SOCKBUF_H_

#ifndef _SYS_TYPES_H_
#include <sys/types.h>
#endif

#if defined(_KERNEL) || defined(_KERNEL_STRUCTURES)

/*
 * Generic socket buffer for keeping track of mbuf chains.  These
 * are used primarily to manipulate mbuf chains in standalone pieces
 * of code.
 */
struct sockbuf {
	u_long	sb_cc;		/* actual chars in buffer */
	u_long	sb_mbcnt;	/* chars of mbufs used */
	u_long	sb_cc_prealloc;
	u_long	sb_mbcnt_prealloc;
	u_long	sb_climit;	/* data limit when used for I/O */
	struct	mbuf *sb_mb;	/* the mbuf chain */
	struct	mbuf *sb_lastmbuf;	/* last mbuf in sb_mb */
	struct	mbuf *sb_lastrecord;	/* last record in sb_mb
					 * valid <=> sb_mb non-NULL */
};

#define SB_MAX		(512*1024)	/* default for max chars in sockbuf */

#endif

#ifdef _KERNEL

#include <machine/atomic.h>
#ifndef _SYS_MBUF_H_
#include <sys/mbuf.h>
#endif

/*
 * Macros for sockets and socket buffering.
 */

#ifdef SOCKBUF_DEBUG
#define sbcheck(sb)	_sbcheck(sb)
#else
#define sbcheck(sb)
#endif

/* adjust counters in sb reflecting allocation of m */
#define	sballoc(sb, m) { \
	(sb)->sb_cc += (m)->m_len; \
	(sb)->sb_mbcnt += MSIZE; \
	if ((m)->m_flags & M_EXT) \
		(sb)->sb_mbcnt += (m)->m_ext.ext_size; \
}

/* adjust counters in sb reflecting allocation of m */
#define	sbprealloc(sb, m) { \
	u_long __mbcnt_sz; \
 \
	atomic_add_long(&((sb)->sb_cc_prealloc), (m)->m_len); \
 \
	__mbcnt_sz = MSIZE; \
	if ((m)->m_flags & M_EXT) \
		__mbcnt_sz += (m)->m_ext.ext_size; \
	atomic_add_long(&((sb)->sb_mbcnt_prealloc), __mbcnt_sz); \
}

/* adjust counters in sb reflecting freeing of m */
#define	sbfree(sb, m) { \
	u_long __mbcnt_sz; \
 \
	(sb)->sb_cc -= (m)->m_len; \
	atomic_subtract_long(&((sb)->sb_cc_prealloc), (m)->m_len); \
 \
	__mbcnt_sz = MSIZE; \
	if ((m)->m_flags & M_EXT) \
		__mbcnt_sz += (m)->m_ext.ext_size; \
	(sb)->sb_mbcnt -= __mbcnt_sz; \
	atomic_subtract_long(&((sb)->sb_mbcnt_prealloc), __mbcnt_sz); \
}

static __inline void
sbinit(struct sockbuf *sb, u_long climit)
{
	sb->sb_cc = 0;
	sb->sb_mbcnt = 0;
	sb->sb_cc_prealloc = 0;
	sb->sb_mbcnt_prealloc = 0;
	sb->sb_climit = climit;
	sb->sb_mb = NULL;
	sb->sb_lastmbuf = NULL;
	sb->sb_lastrecord = NULL;
}

void	sbappend (struct sockbuf *sb, struct mbuf *m);
int	sbappendaddr (struct sockbuf *sb, const struct sockaddr *asa,
	    struct mbuf *m0, struct mbuf *control);
int	sbappendcontrol (struct sockbuf *sb, struct mbuf *m0,
	    struct mbuf *control);
void	sbappendrecord (struct sockbuf *sb, struct mbuf *m0);
void	sbappendstream (struct sockbuf *sb, struct mbuf *m);
void	_sbcheck (struct sockbuf *sb);
void	sbcompress (struct sockbuf *sb, struct mbuf *m, struct mbuf *n);
struct mbuf *
	sbcreatecontrol (caddr_t p, int size, int type, int level);
void	sbdrop (struct sockbuf *sb, int len);
void	sbdroprecord (struct sockbuf *sb);
struct mbuf *
	sbunlinkmbuf (struct sockbuf *, struct mbuf *, struct mbuf **);
void	sbflush (struct sockbuf *sb);

#endif /* _KERNEL */

#endif /* !_SYS_SOCKBUF_H_ */
