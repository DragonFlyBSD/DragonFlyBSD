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
 * $DragonFly: src/sys/sys/socketvar2.h,v 1.1 2007/11/07 18:24:04 dillon Exp $
 */

#ifndef _SYS_SOCKETVAR2_H_
#define _SYS_SOCKETVAR2_H_

#include <sys/lock.h>
#include <sys/globaldata.h>
#include <sys/socketvar.h>
#include <sys/spinlock2.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <netinet/in.h>

/*
 * Acquire a lock on a signalsockbuf, sleep if the lock is already held.
 * The sleep is interruptable unless SSB_NOINTR is set in the ssb.
 *
 * Returns 0 on success, non-zero if the lock could not be acquired.
 */
static inline int
ssb_lock(struct signalsockbuf *ssb, int wf)
{
	u_int flags = LK_EXCLUSIVE;
	if (!(wf & M_WAITOK)) {
		flags |= LK_NOWAIT;
	}
	return lockmgr(&ssb->lk, flags);
}

static inline void
ssb_init(struct signalsockbuf *ssb)
{
	lockinit(&ssb->lk, "ssb", ssb->ssb_timeo, 0);
	sb_init(&ssb->sb);
}

static inline void
ssb_uninit(struct signalsockbuf *ssb)
{
	sb_uninit(&ssb->sb);
	lockuninit(&ssb->lk);
}

/*
 * Release a previously acquired lock on a signalsockbuf.
 */
static inline void
ssb_unlock(struct signalsockbuf *ssb)
{
	lockmgr(&ssb->lk, LK_RELEASE);
}

static inline int
so_lock_rcv(struct socket *so, int wf)
{
	struct thread *td = curthread;
	int lkst;
	/*
	 * Enforce locking order. Code that wants both locks must
	 * first lock rcv, then snd
	 */
	lkst = lockstatus(&so->so_snd.lk, td);
	KKASSERT((lkst != LK_EXCLUSIVE) && (lkst != LK_SHARED));
	return ssb_lock(&so->so_rcv, wf);
}

static inline int
so_lock_snd(struct socket *so, int wf)
{
	return ssb_lock(&so->so_snd, wf);
}

static inline void
so_unlock_rcv(struct socket *so)
{
	ssb_unlock(&so->so_rcv);
}

static inline void
so_unlock_snd(struct socket *so)
{
	ssb_unlock(&so->so_snd);
}

static inline void
so_lock(struct socket *so)
{
	lockmgr(&so->so_lock, LK_EXCLUSIVE);
}

static inline void
so_unlock(struct socket *so)
{
	lockmgr(&so->so_lock, LK_RELEASE);
}

static inline int
so_locked_p(struct socket *so)
{
	return lockstatus(&so->so_lock, curthread) == LK_EXCLUSIVE;
}

static inline void
so_qlock(struct socket *so)
{
	spin_lock_wr(&so->so_qlock);
}

static inline void
so_qunlock(struct socket *so)
{
	spin_unlock_wr(&so->so_qlock);
}

static inline int
soreadable(struct socket *so)
{
	struct thread *td = curthread;

	if (td->td_lwp != NULL) {
		int lkst;
		/*
		 * if we are in process context, we must have
		 * the rcvbuf locked
		 */
		lkst = lockstatus(&so->so_rcv.lk, td);
		KKASSERT((lkst == LK_EXCLUSIVE) || (lkst == LK_SHARED));
	}
	return (sb_cc_est(&so->so_rcv.sb) >= so->so_rcv.ssb_lowat) ||
		(so->so_state & SS_CANTRCVMORE) || !TAILQ_EMPTY(&so->so_comp) ||
		(so->so_error != 0);
}
static inline int
soreadable_igneof(struct socket *so)
{
	struct thread *td = curthread;

	if (td->td_lwp != NULL) {
		int lkst;
		/*
		 * if we are in process context, we must have
		 * the rcvbuf locked
		 */
		lkst = lockstatus(&so->so_rcv.lk, td);
		KKASSERT((lkst == LK_EXCLUSIVE) || (lkst == LK_SHARED));
	}
	return (sb_cc_est(&so->so_rcv.sb) >= so->so_rcv.ssb_lowat) ||
		!TAILQ_EMPTY(&so->so_comp) || (so->so_error != 0);
}

static inline int
sowriteable(struct socket *so)
{
	struct thread *td = curthread;

	if (td->td_lwp != NULL) {
		int lkst;
		/*
		 * if we are in process context, we must have
		 * the sndbuf locked
		 */
		lkst = lockstatus(&so->so_snd.lk, td);
		KKASSERT((lkst == LK_EXCLUSIVE) || (lkst == LK_SHARED));
	}
	return (ssb_space(&so->so_snd) >= so->so_snd.ssb_lowat) &&
		((so->so_state & SS_ISCONNECTED) ||
		 !(so->so_proto->pr_flags & PR_CONNREQUIRED) ||
		 (so->so_state & SS_CANTSENDMORE) ||
		 (so->so_error != 0));
}

/*
 * Do we need to notify the other side when I/O is possible?
 */
static inline int
ssb_notify(struct signalsockbuf *ssb)
{
	return (ssb->ssb_waiting ||
		(ssb->ssb_flags & (SSB_SEL | SSB_ASYNC | SSB_UPCALL |
				   SSB_AIO | SSB_KNOTE | SSB_MEVENT)));
}

static inline void
sorwakeup(struct socket *so)
{
	if (ssb_notify(&so->so_rcv)) {
		get_mplock();
		sowakeup(so, &(so)->so_rcv);
		rel_mplock();
	}
}

static inline void
sowwakeup(struct socket *so)
{
	if (ssb_notify(&so->so_snd)) {
		get_mplock();
		sowakeup(so, &(so)->so_snd);
		rel_mplock();
	}
}

#endif
