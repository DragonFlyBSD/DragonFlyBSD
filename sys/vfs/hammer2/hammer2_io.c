/*
 * Copyright (c) 2013-2014 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@dragonflybsd.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "hammer2.h"

/*
 * Implements an abstraction layer for synchronous and asynchronous
 * buffered device I/O.  Can be used for OS-abstraction but the main
 * purpose is to allow larger buffers to be used against hammer2_chain's
 * using smaller allocations, without causing deadlocks.
 *
 */
static int hammer2_io_cleanup_callback(hammer2_io_t *dio, void *arg);

static int
hammer2_io_cmp(hammer2_io_t *io1, hammer2_io_t *io2)
{
	if (io2->pbase < io1->pbase)
		return(-1);
	if (io2->pbase > io1->pbase)
		return(1);
	return(0);
}

RB_PROTOTYPE2(hammer2_io_tree, hammer2_io, rbnode, hammer2_io_cmp, off_t);
RB_GENERATE2(hammer2_io_tree, hammer2_io, rbnode, hammer2_io_cmp,
		off_t, pbase);

struct hammer2_cleanupcb_info {
	struct hammer2_io_tree tmptree;
	int	count;
};

#define HAMMER2_GETBLK_GOOD	0
#define HAMMER2_GETBLK_QUEUED	1
#define HAMMER2_GETBLK_OWNED	2

/*
 * Allocate/Locate the requested dio, reference it, issue or queue iocb.
 */
void
hammer2_io_getblk(hammer2_mount_t *hmp, off_t lbase, int lsize,
		  hammer2_iocb_t *iocb)
{
	hammer2_io_t *dio;
	hammer2_io_t *xio;
	off_t pbase;
	off_t pmask;
	/*
	 * XXX after free, buffer reuse case w/ different size can clash
	 * with dio cache.  Lets avoid it for now.  Ultimate we need to
	 * invalidate the dio cache when freeing blocks to allow a mix
	 * of 16KB and 64KB block sizes).
	 */
	/*int psize = hammer2_devblksize(lsize);*/
	int psize = HAMMER2_PBUFSIZE;
	int refs;

	pmask = ~(hammer2_off_t)(psize - 1);

	KKASSERT((1 << (int)(lbase & HAMMER2_OFF_MASK_RADIX)) == lsize);
	lbase &= ~HAMMER2_OFF_MASK_RADIX;
	pbase = lbase & pmask;
	KKASSERT(pbase != 0 && ((lbase + lsize - 1) & pmask) == pbase);

	/*
	 * Access/Allocate the DIO, bump dio->refs to prevent destruction.
	 */
	hammer2_spin_sh(&hmp->io_spin);
	dio = RB_LOOKUP(hammer2_io_tree, &hmp->iotree, pbase);
	if (dio) {
		if ((atomic_fetchadd_int(&dio->refs, 1) &
		     HAMMER2_DIO_MASK) == 0) {
			atomic_add_int(&dio->hmp->iofree_count, -1);
		}
		hammer2_spin_unsh(&hmp->io_spin);
	} else {
		hammer2_spin_unsh(&hmp->io_spin);
		dio = kmalloc(sizeof(*dio), M_HAMMER2, M_INTWAIT | M_ZERO);
		dio->hmp = hmp;
		dio->pbase = pbase;
		dio->psize = psize;
		dio->refs = 1;
		hammer2_spin_init(&dio->spin, "h2dio");
		TAILQ_INIT(&dio->iocbq);
		hammer2_spin_ex(&hmp->io_spin);
		xio = RB_INSERT(hammer2_io_tree, &hmp->iotree, dio);
		if (xio == NULL) {
			atomic_add_int(&hammer2_dio_count, 1);
			hammer2_spin_unex(&hmp->io_spin);
		} else {
			if ((atomic_fetchadd_int(&xio->refs, 1) &
			     HAMMER2_DIO_MASK) == 0) {
				atomic_add_int(&xio->hmp->iofree_count, -1);
			}
			hammer2_spin_unex(&hmp->io_spin);
			kfree(dio, M_HAMMER2);
			dio = xio;
		}
	}

	/*
	 * Obtain/Validate the buffer.
	 */
	iocb->dio = dio;

	if (dio->act < 5)	/* SMP race ok */
		++dio->act;

	for (;;) {
		refs = dio->refs;
		cpu_ccfence();

		/*
		 * Issue the iocb immediately if the buffer is already good.
		 * Once set GOOD cannot be cleared until refs drops to 0.
		 */
		if (refs & HAMMER2_DIO_GOOD) {
			iocb->callback(iocb);
			break;
		}

		/*
		 * Try to own the DIO by setting INPROG so we can issue
		 * I/O on it.
		 */
		if (refs & HAMMER2_DIO_INPROG) {
			/*
			 * If DIO_INPROG is already set then set WAITING and
			 * queue the iocb.
			 */
			hammer2_spin_ex(&dio->spin);
			if (atomic_cmpset_int(&dio->refs, refs,
					      refs | HAMMER2_DIO_WAITING)) {
				iocb->flags |= HAMMER2_IOCB_ONQ |
					       HAMMER2_IOCB_INPROG;
				TAILQ_INSERT_TAIL(&dio->iocbq, iocb, entry);
				hammer2_spin_unex(&dio->spin);
				break;
			}
			hammer2_spin_unex(&dio->spin);
			/* retry */
		} else {
			/*
			 * If DIO_INPROG is not set then set it and issue the
			 * callback immediately to start I/O.
			 */
			if (atomic_cmpset_int(&dio->refs, refs,
					      refs | HAMMER2_DIO_INPROG)) {
				iocb->flags |= HAMMER2_IOCB_INPROG;
				iocb->callback(iocb);
				break;
			}
			/* retry */
		}
		/* retry */
	}
}

/*
 * The originator of the iocb is finished with it.
 */
void
hammer2_io_complete(hammer2_iocb_t *iocb)
{
	hammer2_io_t *dio = iocb->dio;
	hammer2_iocb_t *cbtmp;
	uint32_t orefs;
	uint32_t nrefs;
	uint32_t oflags;
	uint32_t nflags;

	/*
	 * If IOCB_INPROG was not set completion is synchronous due to the
	 * buffer already being good.  We can simply set IOCB_DONE and return.
	 * In this situation DIO_INPROG is not set and we have no visibility
	 * on dio->bp.
	 */
	if ((iocb->flags & HAMMER2_IOCB_INPROG) == 0) {
		atomic_set_int(&iocb->flags, HAMMER2_IOCB_DONE);
		return;
	}

	/*
	 * The iocb was queued, obtained DIO_INPROG, and its callback was
	 * made.  The callback is now complete.  We still own DIO_INPROG.
	 *
	 * We can set DIO_GOOD if no error occurred, which gives certain
	 * stability guarantees to dio->bp and allows other accessors to
	 * short-cut access.  DIO_GOOD cannot be cleared until the last
	 * ref is dropped.
	 */
	KKASSERT(dio->refs & HAMMER2_DIO_INPROG);
	if (dio->bp) {
		BUF_KERNPROC(dio->bp);
		if ((dio->bp->b_flags & B_ERROR) == 0) {
			KKASSERT(dio->bp->b_flags & B_CACHE);
			atomic_set_int(&dio->refs, HAMMER2_DIO_GOOD);
		}
	}

	/*
	 * Clean up the dio before marking the iocb as being done.  If another
	 * iocb is pending we chain to it while leaving DIO_INPROG set (it
	 * will call io completion and presumably clear DIO_INPROG).
	 *
	 * Otherwise if no other iocbs are pending we clear DIO_INPROG before
	 * finishing up the cbio.  This means that DIO_INPROG is cleared at
	 * the end of the chain before ANY of the cbios are marked done.
	 *
	 * NOTE: The TAILQ is not stable until the spin-lock is held.
	 */
	for (;;) {
		orefs = dio->refs;
		nrefs = orefs & ~(HAMMER2_DIO_WAITING | HAMMER2_DIO_INPROG);

		if (orefs & HAMMER2_DIO_WAITING) {
			hammer2_spin_ex(&dio->spin);
			cbtmp = TAILQ_FIRST(&dio->iocbq);
			if (cbtmp) {
				/*
				 * NOTE: flags not adjusted in this case.
				 *	 Flags will be adjusted by the last
				 *	 iocb.
				 */
				TAILQ_REMOVE(&dio->iocbq, cbtmp, entry);
				hammer2_spin_unex(&dio->spin);
				cbtmp->callback(cbtmp);	/* chained */
				break;
			} else if (atomic_cmpset_int(&dio->refs,
						     orefs, nrefs)) {
				hammer2_spin_unex(&dio->spin);
				break;
			}
			hammer2_spin_unex(&dio->spin);
			/* retry */
		} else if (atomic_cmpset_int(&dio->refs, orefs, nrefs)) {
			break;
		} /* else retry */
		/* retry */
	}

	/*
	 * Mark the iocb as done and wakeup any waiters.  This is done after
	 * all iocb chains have been called back and after DIO_INPROG has been
	 * cleared.  This avoids races against ref count drops by the waiting
	 * threads (a hard but not impossible SMP race) which might result in
	 * a 1->0 transition of the refs while DIO_INPROG is still set.
	 */
	for (;;) {
		oflags = iocb->flags;
		cpu_ccfence();
		nflags = oflags;
		nflags &= ~(HAMMER2_IOCB_WAKEUP | HAMMER2_IOCB_INPROG);
		nflags |= HAMMER2_IOCB_DONE;

		if (atomic_cmpset_int(&iocb->flags, oflags, nflags)) {
			if (oflags & HAMMER2_IOCB_WAKEUP)
				wakeup(iocb);
			/* SMP: iocb is now stale */
			break;
		}
		/* retry */
	}
	iocb = NULL;

}

/*
 * Wait for an iocb's I/O to finish.
 */
void
hammer2_iocb_wait(hammer2_iocb_t *iocb)
{
	uint32_t oflags;
	uint32_t nflags;

	for (;;) {
		oflags = iocb->flags;
		cpu_ccfence();
		nflags = oflags | HAMMER2_IOCB_WAKEUP;
		if (oflags & HAMMER2_IOCB_DONE)
			break;
		tsleep_interlock(iocb, 0);
		if (atomic_cmpset_int(&iocb->flags, oflags, nflags)) {
			tsleep(iocb, PINTERLOCKED, "h2iocb", hz);
		}
	}

}

/*
 * Release our ref on *diop.
 *
 * On the last ref we must atomically clear DIO_GOOD and set DIO_INPROG,
 * then dispose of the underlying buffer.
 */
void
hammer2_io_putblk(hammer2_io_t **diop)
{
	hammer2_mount_t *hmp;
	hammer2_io_t *dio;
	hammer2_iocb_t iocb;
	struct buf *bp;
	off_t peof;
	off_t pbase;
	int psize;
	int refs;

	dio = *diop;
	*diop = NULL;

	/*
	 * Drop refs, on 1->0 transition clear flags, set INPROG.
	 */
	for (;;) {
		refs = dio->refs;

		if ((refs & HAMMER2_DIO_MASK) == 1) {
			if (refs & HAMMER2_DIO_INPROG) {
				hammer2_iocb_t *xcb;

				xcb = TAILQ_FIRST(&dio->iocbq);
				kprintf("BAD REFS dio %p %08x/%08x, cbio %p\n",
					dio, refs, dio->refs, xcb);
				if (xcb)
					kprintf("   IOCB: func=%p dio=%p cl=%p ch=%p ptr=%p\n",
						xcb->callback,
						xcb->dio,
						xcb->cluster,
						xcb->chain,
						xcb->ptr);
			}
			KKASSERT((refs & HAMMER2_DIO_INPROG) == 0);
			if (atomic_cmpset_int(&dio->refs, refs,
					      ((refs - 1) &
					       ~(HAMMER2_DIO_GOOD |
						 HAMMER2_DIO_DIRTY)) |
					      HAMMER2_DIO_INPROG)) {
				break;
			}
			/* retry */
		} else {
			if (atomic_cmpset_int(&dio->refs, refs, refs - 1))
				return;
			/* retry */
		}
		/* retry */
	}

	/*
	 * We have set DIO_INPROG to gain control of the buffer and we have
	 * cleared DIO_GOOD to prevent other accessors from thinking it is
	 * still good.
	 *
	 * We can now dispose of the buffer, and should do it before calling
	 * io_complete() in case there's a race against a new reference
	 * which causes io_complete() to chain and instantiate the bp again.
	 */
	pbase = dio->pbase;
	psize = dio->psize;
	bp = dio->bp;
	dio->bp = NULL;

	if (refs & HAMMER2_DIO_GOOD) {
		KKASSERT(bp != NULL);
		if (refs & HAMMER2_DIO_DIRTY) {
			if (hammer2_cluster_enable) {
				peof = (pbase + HAMMER2_SEGMASK64) &
				       ~HAMMER2_SEGMASK64;
				cluster_write(bp, peof, psize, 4);
			} else {
				bp->b_flags |= B_CLUSTEROK;
				bdwrite(bp);
			}
		} else if (bp->b_flags & (B_ERROR | B_INVAL | B_RELBUF)) {
			brelse(bp);
		} else {
			bqrelse(bp);
		}
	} else if (bp) {
		if (refs & HAMMER2_DIO_DIRTY) {
			bdwrite(bp);
		} else {
			brelse(bp);
		}
	}

	/*
	 * The instant we call io_complete dio is a free agent again and
	 * can be ripped out from under us.
	 *
	 * we can cleanup our final DIO_INPROG by simulating an iocb
	 * completion.
	 */
	hmp = dio->hmp;				/* extract fields */
	atomic_add_int(&hmp->iofree_count, 1);
	cpu_ccfence();

	iocb.dio = dio;
	iocb.flags = HAMMER2_IOCB_INPROG;
	hammer2_io_complete(&iocb);
	dio = NULL;				/* dio stale */

	/*
	 * We cache free buffers so re-use cases can use a shared lock, but
	 * if too many build up we have to clean them out.
	 */
	if (hmp->iofree_count > 1000) {
		struct hammer2_cleanupcb_info info;

		RB_INIT(&info.tmptree);
		hammer2_spin_ex(&hmp->io_spin);
		if (hmp->iofree_count > 1000) {
			info.count = hmp->iofree_count / 2;
			RB_SCAN(hammer2_io_tree, &hmp->iotree, NULL,
				hammer2_io_cleanup_callback, &info);
		}
		hammer2_spin_unex(&hmp->io_spin);
		hammer2_io_cleanup(hmp, &info.tmptree);
	}
}

/*
 * Cleanup any dio's with (INPROG | refs) == 0.
 *
 * Called to clean up cached DIOs on umount after all activity has been
 * flushed.
 */
static
int
hammer2_io_cleanup_callback(hammer2_io_t *dio, void *arg)
{
	struct hammer2_cleanupcb_info *info = arg;
	hammer2_io_t *xio;

	if ((dio->refs & (HAMMER2_DIO_MASK | HAMMER2_DIO_INPROG)) == 0) {
		if (dio->act > 0) {
			--dio->act;
			return 0;
		}
		KKASSERT(dio->bp == NULL);
		RB_REMOVE(hammer2_io_tree, &dio->hmp->iotree, dio);
		xio = RB_INSERT(hammer2_io_tree, &info->tmptree, dio);
		KKASSERT(xio == NULL);
		if (--info->count <= 0)	/* limit scan */
			return(-1);
	}
	return 0;
}

void
hammer2_io_cleanup(hammer2_mount_t *hmp, struct hammer2_io_tree *tree)
{
	hammer2_io_t *dio;

	while ((dio = RB_ROOT(tree)) != NULL) {
		RB_REMOVE(hammer2_io_tree, tree, dio);
		KKASSERT(dio->bp == NULL &&
		    (dio->refs & (HAMMER2_DIO_MASK | HAMMER2_DIO_INPROG)) == 0);
		kfree(dio, M_HAMMER2);
		atomic_add_int(&hammer2_dio_count, -1);
		atomic_add_int(&hmp->iofree_count, -1);
	}
}

/*
 * Returns a pointer to the requested data.
 */
char *
hammer2_io_data(hammer2_io_t *dio, off_t lbase)
{
	struct buf *bp;
	int off;

	bp = dio->bp;
	KKASSERT(bp != NULL);
	off = (lbase & ~HAMMER2_OFF_MASK_RADIX) - bp->b_loffset;
	KKASSERT(off >= 0 && off < bp->b_bufsize);
	return(bp->b_data + off);
}

/*
 * Helpers for hammer2_io_new*() functions
 */
static
void
hammer2_iocb_new_callback(hammer2_iocb_t *iocb)
{
	hammer2_io_t *dio = iocb->dio;
	int gbctl = (iocb->flags & HAMMER2_IOCB_QUICK) ? GETBLK_NOWAIT : 0;

	/*
	 * If IOCB_INPROG is not set the dio already has a good buffer and we
	 * can't mess with it other than zero the requested range.
	 *
	 * If IOCB_INPROG is set we also own DIO_INPROG at this time and can
	 * do what needs to be done with dio->bp.
	 */
	if (iocb->flags & HAMMER2_IOCB_INPROG) {
		if ((iocb->flags & HAMMER2_IOCB_READ) == 0) {
			if (iocb->lsize == dio->psize) {
				/*
				 * Fully covered buffer, try to optimize to
				 * avoid any I/O.  We might already have the
				 * buffer due to iocb chaining.
				 */
				if (dio->bp == NULL) {
					dio->bp = getblk(dio->hmp->devvp,
							 dio->pbase, dio->psize,
							 gbctl, 0);
				}
				if (dio->bp) {
					vfs_bio_clrbuf(dio->bp);
					dio->bp->b_flags |= B_CACHE;
				}
			} else if (iocb->flags & HAMMER2_IOCB_QUICK) {
				/*
				 * Partial buffer, quick mode.  Do nothing.
				 * Do not instantiate the buffer or try to
				 * mark it B_CACHE because other portions of
				 * the buffer might have to be read by other
				 * accessors.
				 */
			} else if (dio->bp == NULL ||
				   (dio->bp->b_flags & B_CACHE) == 0) {
				/*
				 * Partial buffer, normal mode, requires
				 * read-before-write.  Chain the read.
				 *
				 * We might already have the buffer due to
				 * iocb chaining.  XXX unclear if we really
				 * need to write/release it and reacquire
				 * in that case.
				 *
				 * QUEUE ASYNC I/O, IOCB IS NOT YET COMPLETE.
				 */
				if (dio->bp) {
					if (dio->refs & HAMMER2_DIO_DIRTY)
						bdwrite(dio->bp);
					else
						bqrelse(dio->bp);
					dio->bp = NULL;
				}
				atomic_set_int(&iocb->flags, HAMMER2_IOCB_READ);
				breadcb(dio->hmp->devvp,
					dio->pbase, dio->psize,
					hammer2_io_callback, iocb);
				return;
			} /* else buffer is good */
		} /* else callback from breadcb is complete */
	}
	if (dio->bp) {
		if (iocb->flags & HAMMER2_IOCB_ZERO)
			bzero(hammer2_io_data(dio, iocb->lbase), iocb->lsize);
		atomic_set_int(&dio->refs, HAMMER2_DIO_DIRTY);
	}
	hammer2_io_complete(iocb);
}

static
int
_hammer2_io_new(hammer2_mount_t *hmp, off_t lbase, int lsize,
	        hammer2_io_t **diop, int flags)
{
	hammer2_iocb_t iocb;
	hammer2_io_t *dio;

	iocb.callback = hammer2_iocb_new_callback;
	iocb.cluster = NULL;
	iocb.chain = NULL;
	iocb.ptr = NULL;
	iocb.lbase = lbase;
	iocb.lsize = lsize;
	iocb.flags = flags;
	iocb.error = 0;
	hammer2_io_getblk(hmp, lbase, lsize, &iocb);
	if ((iocb.flags & HAMMER2_IOCB_DONE) == 0)
		hammer2_iocb_wait(&iocb);
	dio = *diop = iocb.dio;

	return (iocb.error);
}

int
hammer2_io_new(hammer2_mount_t *hmp, off_t lbase, int lsize,
	       hammer2_io_t **diop)
{
	return(_hammer2_io_new(hmp, lbase, lsize, diop, HAMMER2_IOCB_ZERO));
}

int
hammer2_io_newnz(hammer2_mount_t *hmp, off_t lbase, int lsize,
	       hammer2_io_t **diop)
{
	return(_hammer2_io_new(hmp, lbase, lsize, diop, 0));
}

int
hammer2_io_newq(hammer2_mount_t *hmp, off_t lbase, int lsize,
	       hammer2_io_t **diop)
{
	return(_hammer2_io_new(hmp, lbase, lsize, diop, HAMMER2_IOCB_QUICK));
}

static
void
hammer2_iocb_bread_callback(hammer2_iocb_t *iocb)
{
	hammer2_io_t *dio = iocb->dio;
	off_t peof;
	int error;

	/*
	 * If IOCB_INPROG is not set the dio already has a good buffer and we
	 * can't mess with it other than zero the requested range.
	 *
	 * If IOCB_INPROG is set we also own DIO_INPROG at this time and can
	 * do what needs to be done with dio->bp.
	 */
	if (iocb->flags & HAMMER2_IOCB_INPROG) {
		if (dio->bp && (dio->bp->b_flags & B_CACHE)) {
			/*
			 * Already good, likely due to being chained from
			 * another iocb.
			 */
			error = 0;
		} else if (hammer2_cluster_enable) {
			/*
			 * Synchronous cluster I/O for now.
			 */
			if (dio->bp) {
				bqrelse(dio->bp);
				dio->bp = NULL;
			}
			peof = (dio->pbase + HAMMER2_SEGMASK64) &
			       ~HAMMER2_SEGMASK64;
			error = cluster_read(dio->hmp->devvp, peof, dio->pbase,
					     dio->psize,
					     dio->psize, HAMMER2_PBUFSIZE*4,
					     &dio->bp);
		} else {
			/*
			 * Synchronous I/O for now.
			 */
			if (dio->bp) {
				bqrelse(dio->bp);
				dio->bp = NULL;
			}
			error = bread(dio->hmp->devvp, dio->pbase,
				      dio->psize, &dio->bp);
		}
		if (error) {
			brelse(dio->bp);
			dio->bp = NULL;
		}
	}
	hammer2_io_complete(iocb);
}

int
hammer2_io_bread(hammer2_mount_t *hmp, off_t lbase, int lsize,
		hammer2_io_t **diop)
{
	hammer2_iocb_t iocb;
	hammer2_io_t *dio;

	iocb.callback = hammer2_iocb_bread_callback;
	iocb.cluster = NULL;
	iocb.chain = NULL;
	iocb.ptr = NULL;
	iocb.lbase = lbase;
	iocb.lsize = lsize;
	iocb.flags = 0;
	iocb.error = 0;
	hammer2_io_getblk(hmp, lbase, lsize, &iocb);
	if ((iocb.flags & HAMMER2_IOCB_DONE) == 0)
		hammer2_iocb_wait(&iocb);
	dio = *diop = iocb.dio;

	return (iocb.error);
}

/*
 * System buf/bio async callback extracts the iocb and chains
 * to the iocb callback.
 */
void
hammer2_io_callback(struct bio *bio)
{
	struct buf *dbp = bio->bio_buf;
	hammer2_iocb_t *iocb = bio->bio_caller_info1.ptr;
	hammer2_io_t *dio;

	dio = iocb->dio;
	if ((bio->bio_flags & BIO_DONE) == 0)
		bpdone(dbp, 0);
	bio->bio_flags &= ~(BIO_DONE | BIO_SYNC);
	dio->bp = bio->bio_buf;
	iocb->callback(iocb);
}

void
hammer2_io_bawrite(hammer2_io_t **diop)
{
	atomic_set_int(&(*diop)->refs, HAMMER2_DIO_DIRTY);
	hammer2_io_putblk(diop);
}

void
hammer2_io_bdwrite(hammer2_io_t **diop)
{
	atomic_set_int(&(*diop)->refs, HAMMER2_DIO_DIRTY);
	hammer2_io_putblk(diop);
}

int
hammer2_io_bwrite(hammer2_io_t **diop)
{
	atomic_set_int(&(*diop)->refs, HAMMER2_DIO_DIRTY);
	hammer2_io_putblk(diop);
	return (0);	/* XXX */
}

void
hammer2_io_setdirty(hammer2_io_t *dio)
{
	atomic_set_int(&dio->refs, HAMMER2_DIO_DIRTY);
}

void
hammer2_io_setinval(hammer2_io_t *dio, u_int bytes)
{
	if ((u_int)dio->psize == bytes)
		dio->bp->b_flags |= B_INVAL | B_RELBUF;
}

void
hammer2_io_brelse(hammer2_io_t **diop)
{
	hammer2_io_putblk(diop);
}

void
hammer2_io_bqrelse(hammer2_io_t **diop)
{
	hammer2_io_putblk(diop);
}

int
hammer2_io_isdirty(hammer2_io_t *dio)
{
	return((dio->refs & HAMMER2_DIO_DIRTY) != 0);
}
