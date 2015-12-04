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
 */

#ifndef _SYS_SOCKETVAR2_H_
#define _SYS_SOCKETVAR2_H_

#ifndef _SYS_SOCKETVAR_H_
#include <sys/socketvar.h>
#endif
#ifndef _SYS_SYSTM_H_
#include <sys/systm.h>
#endif
#ifndef _SYS_MALLOC_H_
#include <sys/malloc.h>
#endif
#include <machine/atomic.h>

/*
 * Acquire a lock on a signalsockbuf, sleep if the lock is already held.
 * The sleep is interruptable unless SSB_NOINTR is set in the ssb.
 *
 * We also acquire the token on success.  This token is used to interlock
 * frontend/backend operations until the sockbuf itself can be made mpsafe.
 *
 * Returns 0 on success, non-zero if the lock could not be acquired.
 */
static __inline int
ssb_lock(struct signalsockbuf *ssb, int wf)
{
	uint32_t flags;

	for (;;) {
		flags = ssb->ssb_flags;
		cpu_ccfence();
		if (flags & SSB_LOCK) {
			if (wf == M_WAITOK)
				return _ssb_lock(ssb);
			return EWOULDBLOCK;
		}
		if (atomic_cmpset_int(&ssb->ssb_flags, flags, flags|SSB_LOCK)) {
			lwkt_gettoken(&ssb->ssb_token);
			return(0);
		}
	}
}

/*
 * Release a previously acquired lock on a signalsockbuf.
 *
 * Interlocked wakeup if SSB_WANT was also set.
 */
static __inline void
ssb_unlock(struct signalsockbuf *ssb)
{
	uint32_t flags;

	KKASSERT(ssb->ssb_flags & SSB_LOCK);
	lwkt_reltoken(&ssb->ssb_token);
	for (;;) {
		flags = ssb->ssb_flags;
		cpu_ccfence();
		if (atomic_cmpset_int(&ssb->ssb_flags, flags,
				      flags & ~(SSB_LOCK | SSB_WANT))) {
			if (flags & SSB_WANT)
				wakeup(&ssb->ssb_flags);
			break;
		}
	}
}

static __inline void
sosetstate(struct socket *so, short state)
{
	atomic_set_short(&so->so_state, state);
}

static __inline void
soclrstate(struct socket *so, short state)
{
	atomic_clear_short(&so->so_state, state);
}

static __inline void
soreference(struct socket *so)
{
	/*
	 * 0 -> 1 transition will happen on an aborted
	 * socket, which is left on the so_comp queue,
	 * e.g. accept(2) the aborted socket, or when
	 * the listen(2) socket owning the so_comp queue
	 * is closed.
	 */
	atomic_add_int(&so->so_refs, 1);
}

#endif
