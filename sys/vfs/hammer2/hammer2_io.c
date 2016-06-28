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
static void dio_write_stats_update(hammer2_io_t *dio);

static int
hammer2_io_cmp(hammer2_io_t *io1, hammer2_io_t *io2)
{
	if (io1->pbase < io2->pbase)
		return(-1);
	if (io1->pbase > io2->pbase)
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

static __inline
uint64_t
hammer2_io_mask(hammer2_io_t *dio, hammer2_off_t off, u_int bytes)
{
	uint64_t mask;
	int i;

	if (bytes < 1024)	/* smaller chunks not supported */
		return 0;

	/*
	 * Calculate crc check mask for larger chunks
	 */
	i = (((off & ~HAMMER2_OFF_MASK_RADIX) - dio->pbase) &
	     HAMMER2_PBUFMASK) >> 10;
	if (i == 0 && bytes == HAMMER2_PBUFSIZE)
		return((uint64_t)-1);
	mask = ((uint64_t)1U << (bytes >> 10)) - 1;
	mask <<= i;

	return mask;
}

#define HAMMER2_GETBLK_GOOD	0
#define HAMMER2_GETBLK_QUEUED	1
#define HAMMER2_GETBLK_OWNED	2

/*
 * Allocate/Locate the requested dio, reference it, issue or queue iocb.
 */
void
hammer2_io_getblk(hammer2_dev_t *hmp, off_t lbase, int lsize,
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
	uint64_t refs;

	pmask = ~(hammer2_off_t)(psize - 1);

	KKASSERT((1 << (int)(lbase & HAMMER2_OFF_MASK_RADIX)) == lsize);
	lbase &= ~HAMMER2_OFF_MASK_RADIX;
	pbase = lbase & pmask;
	if (pbase == 0 || ((lbase + lsize - 1) & pmask) != pbase) {
		kprintf("Illegal: %016jx %016jx+%08x / %016jx\n",
			pbase, lbase, lsize, pmask);
	}
	KKASSERT(pbase != 0 && ((lbase + lsize - 1) & pmask) == pbase);

	/*
	 * Access/Allocate the DIO, bump dio->refs to prevent destruction.
	 */
	hammer2_spin_sh(&hmp->io_spin);
	dio = RB_LOOKUP(hammer2_io_tree, &hmp->iotree, pbase);
	if (dio) {
		if ((atomic_fetchadd_64(&dio->refs, 1) &
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
		dio->btype = iocb->btype;
		dio->refs = 1;
		hammer2_spin_init(&dio->spin, "h2dio");
		TAILQ_INIT(&dio->iocbq);
		hammer2_spin_ex(&hmp->io_spin);
		xio = RB_INSERT(hammer2_io_tree, &hmp->iotree, dio);
		if (xio == NULL) {
			atomic_add_int(&hammer2_dio_count, 1);
			hammer2_spin_unex(&hmp->io_spin);
		} else {
			if ((atomic_fetchadd_64(&xio->refs, 1) &
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
		 *
		 * lfence required because dio's are not interlocked for
		 * the DIO_GOOD test.
		 */
		if (refs & HAMMER2_DIO_GOOD) {
			cpu_lfence();
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
			if (atomic_cmpset_64(&dio->refs, refs,
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
			if (atomic_cmpset_64(&dio->refs, refs,
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
 * Quickly obtain a good DIO buffer, return NULL if the system no longer
 * caches the data.
 */
hammer2_io_t *
hammer2_io_getquick(hammer2_dev_t *hmp, off_t lbase, int lsize)
{
	hammer2_iocb_t iocb;
	hammer2_io_t *dio;
	struct buf *bp;
	off_t pbase;
	off_t pmask;
	int psize = HAMMER2_PBUFSIZE;
	uint64_t orefs;
	uint64_t nrefs;

	pmask = ~(hammer2_off_t)(psize - 1);

	KKASSERT((1 << (int)(lbase & HAMMER2_OFF_MASK_RADIX)) == lsize);
	lbase &= ~HAMMER2_OFF_MASK_RADIX;
	pbase = lbase & pmask;
	if (pbase == 0 || ((lbase + lsize - 1) & pmask) != pbase) {
		kprintf("Illegal: %016jx %016jx+%08x / %016jx\n",
			pbase, lbase, lsize, pmask);
	}
	KKASSERT(pbase != 0 && ((lbase + lsize - 1) & pmask) == pbase);

	/*
	 * Access/Allocate the DIO, bump dio->refs to prevent destruction.
	 */
	hammer2_spin_sh(&hmp->io_spin);
	dio = RB_LOOKUP(hammer2_io_tree, &hmp->iotree, pbase);
	if (dio == NULL) {
		hammer2_spin_unsh(&hmp->io_spin);
		return NULL;
	}

	if ((atomic_fetchadd_64(&dio->refs, 1) & HAMMER2_DIO_MASK) == 0)
		atomic_add_int(&dio->hmp->iofree_count, -1);
	hammer2_spin_unsh(&hmp->io_spin);

	if (dio->act < 5)	/* SMP race ok */
		++dio->act;

	/*
	 * Obtain/validate the buffer.  Do NOT issue I/O.  Discard if
	 * the system does not have the data already cached.
	 */
	nrefs = (uint64_t)-1;
	for (;;) {
		orefs = dio->refs;
		cpu_ccfence();

		/*
		 * Issue the iocb immediately if the buffer is already good.
		 * Once set GOOD cannot be cleared until refs drops to 0.
		 *
		 * lfence required because dio is not interlockedf for
		 * the DIO_GOOD test.
		 */
		if (orefs & HAMMER2_DIO_GOOD) {
			cpu_lfence();
			break;
		}

		/*
		 * Try to own the DIO by setting INPROG so we can issue
		 * I/O on it.  INPROG might already be set, in which case
		 * there is no way we can do this non-blocking so we punt.
		 */
		if ((orefs & HAMMER2_DIO_INPROG))
			break;
		nrefs = orefs | HAMMER2_DIO_INPROG;
		if (atomic_cmpset_64(&dio->refs, orefs, nrefs) == 0)
			continue;

		/*
		 * We own DIO_INPROG, try to set DIO_GOOD.
		 *
		 * For now do not use GETBLK_NOWAIT because 
		 */
		bp = dio->bp;
		dio->bp = NULL;
		if (bp == NULL) {
#if 0
			bp = getblk(hmp->devvp, dio->pbase, dio->psize, 0, 0);
#endif
			bread(hmp->devvp, dio->pbase, dio->psize, &bp);
		}

		/*
		 * System buffer must also have remained cached.
		 */
		if (bp) {
			if ((bp->b_flags & B_ERROR) == 0 &&
			    (bp->b_flags & B_CACHE)) {
				dio->bp = bp;	/* assign BEFORE setting flag */
				atomic_set_64(&dio->refs, HAMMER2_DIO_GOOD);
			} else {
				bqrelse(bp);
				bp = NULL;
			}
		}

		/*
		 * Clear DIO_INPROG.
		 *
		 * This is actually a bit complicated, see
		 * hammer2_io_complete() for more information.
		 */
		iocb.dio = dio;
		iocb.flags = HAMMER2_IOCB_INPROG;
		hammer2_io_complete(&iocb);
		break;
	}

	/*
	 * Only return the dio if its buffer is good.  If the buffer is not
	 * good be sure to clear INVALOK, meaning that invalidation is no
	 * longer acceptable
	 */
	if ((dio->refs & HAMMER2_DIO_GOOD) == 0) {
		hammer2_io_putblk(&dio);
	}
	return dio;
}

/*
 * Make sure that INVALOK is cleared on the dio associated with the specified
 * data offset.  Called from bulkfree when a block becomes reusable.
 */
void
hammer2_io_resetinval(hammer2_dev_t *hmp, off_t data_off)
{
	hammer2_io_t *dio;

	data_off &= ~HAMMER2_PBUFMASK64;
	hammer2_spin_sh(&hmp->io_spin);
	dio = RB_LOOKUP(hammer2_io_tree, &hmp->iotree, data_off);
	if (dio)
		atomic_clear_64(&dio->refs, HAMMER2_DIO_INVALOK);
	hammer2_spin_unsh(&hmp->io_spin);
}

/*
 * The originator of the iocb is finished with it.
 */
void
hammer2_io_complete(hammer2_iocb_t *iocb)
{
	hammer2_io_t *dio = iocb->dio;
	hammer2_iocb_t *cbtmp;
	uint64_t orefs;
	uint64_t nrefs;
	uint32_t oflags;
	uint32_t nflags;

	/*
	 * If IOCB_INPROG was not set completion is synchronous due to the
	 * buffer already being good.  We can simply set IOCB_DONE and return.
	 *
	 * In this situation DIO_INPROG is not set and we have no visibility
	 * on dio->bp.  We should not try to mess with dio->bp because another
	 * thread may be finishing up its processing.  dio->bp should already
	 * be set to BUF_KERNPROC()!
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
			atomic_set_64(&dio->refs, HAMMER2_DIO_GOOD);
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
			} else if (atomic_cmpset_64(&dio->refs, orefs, nrefs)) {
				hammer2_spin_unex(&dio->spin);
				break;
			}
			hammer2_spin_unex(&dio->spin);
			/* retry */
		} else if (atomic_cmpset_64(&dio->refs, orefs, nrefs)) {
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
	hammer2_dev_t *hmp;
	hammer2_io_t *dio;
	hammer2_iocb_t iocb;
	struct buf *bp;
	off_t peof;
	off_t pbase;
	int psize;
	uint64_t orefs;
	uint64_t nrefs;

	dio = *diop;
	*diop = NULL;
	hmp = dio->hmp;

	/*
	 * Drop refs.
	 *
	 * On the 1->0 transition clear flags and set INPROG.
	 *
	 * On the 1->0 transition if INPROG is already set, another thread
	 * is in lastdrop and we can just return after the transition.
	 *
	 * On any other transition we can generally just return.
	 */
	for (;;) {
		orefs = dio->refs;
		cpu_ccfence();
		nrefs = orefs - 1;

		if ((orefs & HAMMER2_DIO_MASK) == 1 &&
		    (orefs & HAMMER2_DIO_INPROG) == 0) {
			/*
			 * Lastdrop case, INPROG can be set.
			 */
			nrefs &= ~(HAMMER2_DIO_GOOD | HAMMER2_DIO_DIRTY);
			nrefs &= ~(HAMMER2_DIO_INVAL);
			nrefs |= HAMMER2_DIO_INPROG;
			if (atomic_cmpset_64(&dio->refs, orefs, nrefs))
				break;
		} else if ((orefs & HAMMER2_DIO_MASK) == 1) {
			/*
			 * Lastdrop case, INPROG already set.
			 */
			if (atomic_cmpset_64(&dio->refs, orefs, nrefs)) {
				atomic_add_int(&hmp->iofree_count, 1);
				return;
			}
		} else {
			/*
			 * Normal drop case.
			 */
			if (atomic_cmpset_64(&dio->refs, orefs, nrefs))
				return;
		}
		cpu_pause();
		/* retry */
	}

	/*
	 * Lastdrop (1->0 transition).  INPROG has been set, GOOD and DIRTY
	 * have been cleared.
	 *
	 * We can now dispose of the buffer, and should do it before calling
	 * io_complete() in case there's a race against a new reference
	 * which causes io_complete() to chain and instantiate the bp again.
	 */
	pbase = dio->pbase;
	psize = dio->psize;
	bp = dio->bp;
	dio->bp = NULL;

	if (orefs & HAMMER2_DIO_GOOD) {
		KKASSERT(bp != NULL);
#if 1
		if (hammer2_inval_enable &&
		    (orefs & HAMMER2_DIO_INVALBITS) == HAMMER2_DIO_INVALBITS) {
			++hammer2_iod_invals;
			bp->b_flags |= B_INVAL | B_RELBUF;
			brelse(bp);
		} else
#endif
		if (orefs & HAMMER2_DIO_DIRTY) {
			int hce;

			dio_write_stats_update(dio);
			if ((hce = hammer2_cluster_write) > 0) {
				/*
				 * Allows write-behind to keep the buffer
				 * cache sane.
				 */
				peof = (pbase + HAMMER2_SEGMASK64) &
				       ~HAMMER2_SEGMASK64;
				bp->b_flags |= B_CLUSTEROK;
				cluster_write(bp, peof, psize, hce);
			} else {
				/*
				 * Allows dirty buffers to accumulate and
				 * possibly be canceled (e.g. by a 'rm'),
				 * will burst-write later.
				 */
				bp->b_flags |= B_CLUSTEROK;
				bdwrite(bp);
			}
		} else if (bp->b_flags & (B_ERROR | B_INVAL | B_RELBUF)) {
			brelse(bp);
		} else {
			bqrelse(bp);
		}
	} else if (bp) {
#if 1
		if (hammer2_inval_enable &&
		    (orefs & HAMMER2_DIO_INVALBITS) == HAMMER2_DIO_INVALBITS) {
			++hammer2_iod_invals;
			bp->b_flags |= B_INVAL | B_RELBUF;
			brelse(bp);
		} else
#endif
		if (orefs & HAMMER2_DIO_DIRTY) {
			dio_write_stats_update(dio);
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
	if (hmp->iofree_count > 65536) {
		struct hammer2_cleanupcb_info info;

		RB_INIT(&info.tmptree);
		hammer2_spin_ex(&hmp->io_spin);
		if (hmp->iofree_count > 65536) {
			info.count = hmp->iofree_count / 4;
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
hammer2_io_cleanup(hammer2_dev_t *hmp, struct hammer2_io_tree *tree)
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

#if 0
/*
 * Keep track of good CRCs in dio->good_crc_mask. XXX needs to be done
 * in the chain structure, but chain structure needs to be persistent as
 * well on refs=0 and it isn't.
 */
int
hammer2_io_crc_good(hammer2_chain_t *chain, uint64_t *maskp)
{
	hammer2_io_t *dio;
	uint64_t mask;

	if ((dio = chain->dio) != NULL && chain->bytes >= 1024) {
		mask = hammer2_io_mask(dio, chain->bref.data_off, chain->bytes);
		*maskp = mask;
		if ((dio->crc_good_mask & mask) == mask)
			return 1;
		return 0;
	}
	*maskp = 0;

	return 0;
}

void
hammer2_io_crc_setmask(hammer2_io_t *dio, uint64_t mask)
{
	if (dio) {
		if (sizeof(long) == 8) {
			atomic_set_long(&dio->crc_good_mask, mask);
		} else {
#if _BYTE_ORDER == _LITTLE_ENDIAN
			atomic_set_int(&((int *)&dio->crc_good_mask)[0],
					(uint32_t)mask);
			atomic_set_int(&((int *)&dio->crc_good_mask)[1],
					(uint32_t)(mask >> 32));
#else
			atomic_set_int(&((int *)&dio->crc_good_mask)[0],
					(uint32_t)(mask >> 32));
			atomic_set_int(&((int *)&dio->crc_good_mask)[1],
					(uint32_t)mask);
#endif
		}
	}
}

void
hammer2_io_crc_clrmask(hammer2_io_t *dio, uint64_t mask)
{
	if (dio) {
		if (sizeof(long) == 8) {
			atomic_clear_long(&dio->crc_good_mask, mask);
		} else {
#if _BYTE_ORDER == _LITTLE_ENDIAN
			atomic_clear_int(&((int *)&dio->crc_good_mask)[0],
					(uint32_t)mask);
			atomic_clear_int(&((int *)&dio->crc_good_mask)[1],
					(uint32_t)(mask >> 32));
#else
			atomic_clear_int(&((int *)&dio->crc_good_mask)[0],
					(uint32_t)(mask >> 32));
			atomic_clear_int(&((int *)&dio->crc_good_mask)[1],
					(uint32_t)mask);
#endif
		}
	}
}
#endif

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

				/*
				 * Invalidation is ok on newly allocated
				 * buffers which cover the entire buffer.
				 * Flag will be cleared on use by the de-dup
				 * code.
				 *
				 * hammer2_chain_modify() also checks this flag.
				 *
				 * QUICK mode is used by the freemap code to
				 * pre-validate a junk buffer to prevent an
				 * unnecessary read I/O.  We do NOT want
				 * to set INVALOK in that situation as the
				 * underlying allocations may be smaller.
				 */
				if ((iocb->flags & HAMMER2_IOCB_QUICK) == 0) {
					atomic_set_64(&dio->refs,
						      HAMMER2_DIO_INVALOK);
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
					if (dio->refs & HAMMER2_DIO_DIRTY) {
						dio_write_stats_update(dio);
						bdwrite(dio->bp);
					} else {
						bqrelse(dio->bp);
					}
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
		atomic_set_64(&dio->refs, HAMMER2_DIO_DIRTY);
	}
	hammer2_io_complete(iocb);
}

static
int
_hammer2_io_new(hammer2_dev_t *hmp, int btype, off_t lbase, int lsize,
	        hammer2_io_t **diop, int flags)
{
	hammer2_iocb_t iocb;

	iocb.callback = hammer2_iocb_new_callback;
	iocb.cluster = NULL;
	iocb.chain = NULL;
	iocb.ptr = NULL;
	iocb.lbase = lbase;
	iocb.lsize = lsize;
	iocb.flags = flags;
	iocb.btype = btype;
	iocb.error = 0;
	hammer2_io_getblk(hmp, lbase, lsize, &iocb);
	if ((iocb.flags & HAMMER2_IOCB_DONE) == 0)
		hammer2_iocb_wait(&iocb);
	*diop = iocb.dio;

	return (iocb.error);
}

int
hammer2_io_new(hammer2_dev_t *hmp, int btype, off_t lbase, int lsize,
	       hammer2_io_t **diop)
{
	return(_hammer2_io_new(hmp, btype, lbase, lsize,
			       diop, HAMMER2_IOCB_ZERO));
}

int
hammer2_io_newnz(hammer2_dev_t *hmp, int btype, off_t lbase, int lsize,
		 hammer2_io_t **diop)
{
	return(_hammer2_io_new(hmp, btype, lbase, lsize, diop, 0));
}

/*
 * This is called from the freemap to pre-validate a full-sized buffer
 * whos contents we don't care about, in order to prevent an unnecessary
 * read-before-write.
 */
void
hammer2_io_newq(hammer2_dev_t *hmp, int btype, off_t lbase, int lsize)
{
	hammer2_io_t *dio = NULL;

	_hammer2_io_new(hmp, btype, lbase, lsize, &dio, HAMMER2_IOCB_QUICK);
	hammer2_io_bqrelse(&dio);
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
		int hce;

		if (dio->bp && (dio->bp->b_flags & B_CACHE)) {
			/*
			 * Already good, likely due to being chained from
			 * another iocb.
			 */
			error = 0;
		} else if ((hce = hammer2_cluster_read) > 0) {
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
					     dio->psize, HAMMER2_PBUFSIZE*hce,
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
hammer2_io_bread(hammer2_dev_t *hmp, int btype, off_t lbase, int lsize,
		hammer2_io_t **diop)
{
	hammer2_iocb_t iocb;

	iocb.callback = hammer2_iocb_bread_callback;
	iocb.cluster = NULL;
	iocb.chain = NULL;
	iocb.ptr = NULL;
	iocb.lbase = lbase;
	iocb.lsize = lsize;
	iocb.btype = btype;
	iocb.flags = 0;
	iocb.error = 0;
	hammer2_io_getblk(hmp, lbase, lsize, &iocb);
	if ((iocb.flags & HAMMER2_IOCB_DONE) == 0)
		hammer2_iocb_wait(&iocb);
	*diop = iocb.dio;

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
	atomic_set_64(&(*diop)->refs, HAMMER2_DIO_DIRTY);
	hammer2_io_putblk(diop);
}

void
hammer2_io_bdwrite(hammer2_io_t **diop)
{
	atomic_set_64(&(*diop)->refs, HAMMER2_DIO_DIRTY);
	hammer2_io_putblk(diop);
}

int
hammer2_io_bwrite(hammer2_io_t **diop)
{
	atomic_set_64(&(*diop)->refs, HAMMER2_DIO_DIRTY);
	hammer2_io_putblk(diop);
	return (0);	/* XXX */
}

void
hammer2_io_setdirty(hammer2_io_t *dio)
{
	atomic_set_64(&dio->refs, HAMMER2_DIO_DIRTY);
}

/*
 * Request an invalidation.  The hammer2_io code will oblige only if
 * DIO_INVALOK is also set.  INVALOK is cleared if the dio is used
 * in a dedup lookup and prevents invalidation of the dirty buffer.
 */
void
hammer2_io_setinval(hammer2_io_t *dio, hammer2_off_t off, u_int bytes)
{
	if ((u_int)dio->psize == bytes)
		atomic_set_64(&dio->refs, HAMMER2_DIO_INVAL);
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

static
void
dio_write_stats_update(hammer2_io_t *dio)
{
	long *counterp;

	switch(dio->btype) {
	case 0:
		return;
	case HAMMER2_BREF_TYPE_DATA:
		counterp = &hammer2_iod_file_write;
		break;
	case HAMMER2_BREF_TYPE_INODE:
		counterp = &hammer2_iod_meta_write;
		break;
	case HAMMER2_BREF_TYPE_INDIRECT:
		counterp = &hammer2_iod_indr_write;
		break;
	case HAMMER2_BREF_TYPE_FREEMAP_NODE:
	case HAMMER2_BREF_TYPE_FREEMAP_LEAF:
		counterp = &hammer2_iod_fmap_write;
		break;
	default:
		counterp = &hammer2_iod_volu_write;
		break;
	}
	*counterp += dio->psize;
}
