/*
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
 *	@(#)buf.h	8.9 (Berkeley) 3/30/95
 * $FreeBSD: src/sys/sys/buf.h,v 1.88.2.10 2003/01/25 19:02:23 dillon Exp $
 * $DragonFly: src/sys/sys/buf2.h,v 1.7 2004/03/01 06:33:19 dillon Exp $
 */

#ifndef _SYS_BUF2_H_
#define	_SYS_BUF2_H_

#ifdef _KERNEL

#ifndef _SYS_GLOBALDATA_H_
#include <sys/globaldata.h>	/* curthread */
#endif

/*
 * Initialize a lock.
 */
#define BUF_LOCKINIT(bp) \
	lockinit(&(bp)->b_lock, 0, buf_wmesg, 0, 0)

/*
 *
 * Get a lock sleeping non-interruptably until it becomes available.
 */
static __inline int BUF_LOCK (struct buf *, int);
static __inline int
BUF_LOCK(struct buf *bp, int locktype)
{
	lwkt_tokref ilock;
	int s, ret;

	s = splbio();
	lwkt_gettoken(&ilock, &buftimetoken);
	locktype |= LK_INTERLOCK;
	bp->b_lock.lk_wmesg = buf_wmesg;
	bp->b_lock.lk_prio = 0;		/* tsleep flags */
	/* bp->b_lock.lk_timo = 0;   not necessary */
	ret = lockmgr(&(bp)->b_lock, locktype, &ilock, curthread);
	splx(s);
	return ret;
}
/*
 * Get a lock sleeping with specified interruptably and timeout.
 */
static __inline int BUF_TIMELOCK (struct buf *, int, char *, int, int);
static __inline int
BUF_TIMELOCK(struct buf *bp, int locktype, char *wmesg, int catch, int timo)
{
	lwkt_tokref ilock;
	int s, ret;

	s = splbio();
	lwkt_gettoken(&ilock, &buftimetoken);
	locktype |= LK_INTERLOCK | LK_TIMELOCK;
	bp->b_lock.lk_wmesg = wmesg;
	bp->b_lock.lk_prio = catch;	/* tsleep flags */
	bp->b_lock.lk_timo = timo;
	ret = lockmgr(&(bp)->b_lock, locktype, &ilock, curthread);
	splx(s);
	return ret;
}
/*
 * Release a lock. Only the acquiring process may free the lock unless
 * it has been handed off to biodone.
 */
static __inline void BUF_UNLOCK (struct buf *);
static __inline void
BUF_UNLOCK(struct buf *bp)
{
	int s;

	s = splbio();
	lockmgr(&(bp)->b_lock, LK_RELEASE, NULL, curthread);
	splx(s);
}

/*
 * Free a buffer lock.
 */
#define BUF_LOCKFREE(bp) 			\
	if (BUF_REFCNT(bp) > 0)			\
		panic("free locked buf")
/*
 * When initiating asynchronous I/O, change ownership of the lock to the
 * kernel. Once done, the lock may legally released by biodone. The
 * original owning process can no longer acquire it recursively, but must
 * wait until the I/O is completed and the lock has been freed by biodone.
 */
static __inline void BUF_KERNPROC (struct buf *);
static __inline void
BUF_KERNPROC(struct buf *bp)
{
	struct thread *td = curthread;

	if (bp->b_lock.lk_lockholder == td)
		td->td_locks--;
	bp->b_lock.lk_lockholder = LK_KERNTHREAD;
}
/*
 * Find out the number of references to a lock.
 */
static __inline int BUF_REFCNT (struct buf *);
static __inline int
BUF_REFCNT(struct buf *bp)
{
	int s, ret;

	s = splbio();
	ret = lockcount(&(bp)->b_lock);
	splx(s);
	return ret;
}

static __inline void
bufq_init(struct buf_queue_head *head)
{
	TAILQ_INIT(&head->queue);
	head->last_pblkno = 0;
	head->insert_point = NULL;
	head->switch_point = NULL;
}

static __inline void
bufq_insert_tail(struct buf_queue_head *head, struct buf *bp)
{
	if ((bp->b_flags & B_ORDERED) != 0) {
		head->insert_point = bp;
		head->switch_point = NULL;
	}
	TAILQ_INSERT_TAIL(&head->queue, bp, b_act);
}

static __inline void
bufq_remove(struct buf_queue_head *head, struct buf *bp)
{
	if (bp == head->switch_point)
		head->switch_point = TAILQ_NEXT(bp, b_act);
	if (bp == head->insert_point) {
		head->insert_point = TAILQ_PREV(bp, buf_queue, b_act);
		if (head->insert_point == NULL)
			head->last_pblkno = 0;
	} else if (bp == TAILQ_FIRST(&head->queue))
		head->last_pblkno = bp->b_pblkno;
	TAILQ_REMOVE(&head->queue, bp, b_act);
	if (TAILQ_FIRST(&head->queue) == head->switch_point)
		head->switch_point = NULL;
}

static __inline struct buf *
bufq_first(struct buf_queue_head *head)
{
	return (TAILQ_FIRST(&head->queue));
}

#endif /* _KERNEL */

#endif /* !_SYS_BUF2_H_ */
