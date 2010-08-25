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
 * $DragonFly: src/sys/sys/buf2.h,v 1.21 2008/01/28 07:19:06 nth Exp $
 */

#ifndef _SYS_BUF2_H_
#define	_SYS_BUF2_H_

#ifdef _KERNEL

#ifndef _SYS_BUF_H_
#include <sys/buf.h>		/* crit_*() functions */
#endif
#ifndef _SYS_GLOBALDATA_H_
#include <sys/globaldata.h>	/* curthread */
#endif
#ifndef _SYS_THREAD2_H_
#include <sys/thread2.h>	/* crit_*() functions */
#endif
#ifndef _SYS_SPINLOCK2_H_
#include <sys/spinlock2.h>	/* crit_*() functions */
#endif
#ifndef _SYS_MOUNT_H_
#include <sys/mount.h>
#endif
#ifndef _SYS_VNODE_H_
#include <sys/vnode.h>
#endif
#ifndef _VM_VM_PAGE_H_
#include <vm/vm_page.h>
#endif

/*
 * Initialize a lock.
 */
#define BUF_LOCKINIT(bp) \
	lockinit(&(bp)->b_lock, buf_wmesg, 0, 0)

/*
 *
 * Get a lock sleeping non-interruptably until it becomes available.
 *
 * XXX lk_wmesg can race, but should not result in any operational issues.
 */
static __inline int
BUF_LOCK(struct buf *bp, int locktype)
{
	bp->b_lock.lk_wmesg = buf_wmesg;
	return (lockmgr(&(bp)->b_lock, locktype));
}
/*
 * Get a lock sleeping with specified interruptably and timeout.
 *
 * XXX lk_timo can race against other entities calling BUF_TIMELOCK,
 * but will not interfere with entities calling BUF_LOCK since LK_TIMELOCK
 * will not be set in that case.
 *
 * XXX lk_wmesg can race, but should not result in any operational issues.
 */
static __inline int
BUF_TIMELOCK(struct buf *bp, int locktype, char *wmesg, int timo)
{
	bp->b_lock.lk_wmesg = wmesg;
	bp->b_lock.lk_timo = timo;
	return (lockmgr(&(bp)->b_lock, locktype | LK_TIMELOCK));
}
/*
 * Release a lock. Only the acquiring process may free the lock unless
 * it has been handed off to biodone.
 */
static __inline void
BUF_UNLOCK(struct buf *bp)
{
	lockmgr(&(bp)->b_lock, LK_RELEASE);
}

/*
 * When initiating asynchronous I/O, change ownership of the lock to the
 * kernel. Once done, the lock may legally released by biodone. The
 * original owning process can no longer acquire it recursively, but must
 * wait until the I/O is completed and the lock has been freed by biodone.
 */
static __inline void
BUF_KERNPROC(struct buf *bp)
{
	lockmgr_kernproc(&(bp)->b_lock);
}
/*
 * Find out the number of references to a lock.
 *
 * The non-blocking version should only be used for assertions in cases
 * where the buffer is expected to be owned or otherwise data stable.
 */
static __inline int
BUF_REFCNT(struct buf *bp)
{
	return (lockcount(&(bp)->b_lock));
}

static __inline int
BUF_REFCNTNB(struct buf *bp)
{
	return (lockcountnb(&(bp)->b_lock));
}

/*
 * Free a buffer lock.
 */
#define BUF_LOCKFREE(bp) 			\
	if (BUF_REFCNTNB(bp) > 0)		\
		panic("free locked buf")

static __inline void
bioq_init(struct bio_queue_head *bioq)
{
	TAILQ_INIT(&bioq->queue);
	bioq->off_unused = 0;
	bioq->reorder = 0;
	bioq->transition = NULL;
	bioq->bio_unused = NULL;
}

static __inline void
bioq_insert_tail(struct bio_queue_head *bioq, struct bio *bio)
{
	bioq->transition = NULL;
	TAILQ_INSERT_TAIL(&bioq->queue, bio, bio_act);
}

static __inline void
bioq_remove(struct bio_queue_head *bioq, struct bio *bio)
{
	/*
	 * Adjust read insertion point when removing the bioq.  The
	 * bio after the insert point is a write so move backwards
	 * one (NULL will indicate all the reads have cleared).
	 */
	if (bio == bioq->transition)
		bioq->transition = TAILQ_NEXT(bio, bio_act);
	TAILQ_REMOVE(&bioq->queue, bio, bio_act);
}

static __inline struct bio *
bioq_first(struct bio_queue_head *bioq)
{
	return (TAILQ_FIRST(&bioq->queue));
}

static __inline struct bio *
bioq_takefirst(struct bio_queue_head *bioq)
{
	struct bio *bp;

	bp = TAILQ_FIRST(&bioq->queue);
	if (bp != NULL)
		bioq_remove(bioq, bp);
	return (bp);
}

/*
 * Adjust buffer cache buffer's activity count.  This
 * works similarly to vm_page->act_count.
 */
static __inline void
buf_act_advance(struct buf *bp)
{
	if (bp->b_act_count > ACT_MAX - ACT_ADVANCE)
		bp->b_act_count = ACT_MAX;
	else
		bp->b_act_count += ACT_ADVANCE;
}

static __inline void
buf_act_decline(struct buf *bp)
{
	if (bp->b_act_count < ACT_DECLINE)
		bp->b_act_count = 0;
	else
		bp->b_act_count -= ACT_DECLINE;
}

/*
 * biodeps inlines - used by softupdates and HAMMER.
 *
 * All bioops are MPSAFE
 */
static __inline void
buf_dep_init(struct buf *bp)
{
	bp->b_ops = NULL;
	LIST_INIT(&bp->b_dep);
}

/*
 * Precondition: the buffer has some dependencies.
 *
 * MPSAFE
 */
static __inline void
buf_deallocate(struct buf *bp)
{
	struct bio_ops *ops = bp->b_ops;

	KKASSERT(! LIST_EMPTY(&bp->b_dep));
	if (ops)
		ops->io_deallocate(bp);
}

/*
 * MPSAFE
 */
static __inline int
buf_countdeps(struct buf *bp, int n)
{
	struct bio_ops *ops = bp->b_ops;
	int r;

	if (ops)
		r = ops->io_countdeps(bp, n);
	else
		r = 0;
	return(r);
}

/*
 * MPSAFE
 */
static __inline void
buf_start(struct buf *bp)
{
	struct bio_ops *ops = bp->b_ops;

	if (ops)
		ops->io_start(bp);
}

/*
 * MPSAFE
 */
static __inline void
buf_complete(struct buf *bp)
{
	struct bio_ops *ops = bp->b_ops;

	if (ops)
		ops->io_complete(bp);
}

/*
 * MPSAFE
 */
static __inline int
buf_fsync(struct vnode *vp)
{
	struct bio_ops *ops = vp->v_mount->mnt_bioops;
	int r;

	if (ops)
		r = ops->io_fsync(vp);
	else
		r = 0;
	return(r);
}

/*
 * MPSAFE
 */
static __inline void
buf_movedeps(struct buf *bp1, struct buf *bp2)
{
	struct bio_ops *ops = bp1->b_ops;

	if (ops)
		ops->io_movedeps(bp1, bp2);
}

/*
 * MPSAFE
 */
static __inline int
buf_checkread(struct buf *bp)
{
	struct bio_ops *ops = bp->b_ops;

	if (ops)
		return(ops->io_checkread(bp));
	return(0);
}

/*
 * MPSAFE
 */
static __inline int
buf_checkwrite(struct buf *bp)
{
	struct bio_ops *ops = bp->b_ops;

	if (ops)
		return(ops->io_checkwrite(bp));
	return(0);
}

/*
 * Chained biodone.  The bio callback was made and the callback function
 * wishes to chain the biodone.  If no BIO's are left we call bpdone()
 * with elseit=TRUE (asynchronous completion).
 *
 * MPSAFE
 */
static __inline void
biodone_chain(struct bio *bio)
{
	if (bio->bio_prev)
		biodone(bio->bio_prev);
	else
		bpdone(bio->bio_buf, 1);
}

#endif /* _KERNEL */

#endif /* !_SYS_BUF2_H_ */
