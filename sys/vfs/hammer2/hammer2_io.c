/*
 * Copyright (c) 2013-2018 The DragonFly Project.  All rights reserved.
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

#define HAMMER2_DOP_READ	1
#define HAMMER2_DOP_NEW		2
#define HAMMER2_DOP_NEWNZ	3
#define HAMMER2_DOP_READQ	4

/*
 * Implements an abstraction layer for synchronous and asynchronous
 * buffered device I/O.  Can be used as an OS-abstraction but the main
 * purpose is to allow larger buffers to be used against hammer2_chain's
 * using smaller allocations, without causing deadlocks.
 *
 * The DIOs also record temporary state with limited persistence.  This
 * feature is used to keep track of dedupable blocks.
 */
static int hammer2_io_cleanup_callback(hammer2_io_t *dio, void *arg);
static void dio_write_stats_update(hammer2_io_t *dio, struct buf *bp);

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

#if 0
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
#endif

#ifdef HAMMER2_IO_DEBUG

static __inline void
DIO_RECORD(hammer2_io_t *dio HAMMER2_IO_DEBUG_ARGS)
{
	int i;

	i = atomic_fetchadd_int(&dio->debug_index, 1) & HAMMER2_IO_DEBUG_MASK;

	dio->debug_file[i] = file;
	dio->debug_line[i] = line;
	dio->debug_refs[i] = dio->refs;
	dio->debug_td[i] = curthread;
}

#else

#define DIO_RECORD(dio)

#endif

/*
 * Returns the DIO corresponding to the data|radix, creating it if necessary.
 *
 * If createit is 0, NULL can be returned indicating that the DIO does not
 * exist.  (btype) is ignored when createit is 0.
 */
static __inline
hammer2_io_t *
hammer2_io_alloc(hammer2_dev_t *hmp, hammer2_key_t data_off, uint8_t btype,
		 int createit, int *isgoodp)
{
	hammer2_io_t *dio;
	hammer2_io_t *xio;
	hammer2_key_t lbase;
	hammer2_key_t pbase;
	hammer2_key_t pmask;
	uint64_t refs;
	int lsize;
	int psize;

	psize = HAMMER2_PBUFSIZE;
	pmask = ~(hammer2_off_t)(psize - 1);
	lsize = 1 << (int)(data_off & HAMMER2_OFF_MASK_RADIX);
	lbase = data_off & ~HAMMER2_OFF_MASK_RADIX;
	pbase = lbase & pmask;

	if (pbase == 0 || ((lbase + lsize - 1) & pmask) != pbase) {
		kprintf("Illegal: %016jx %016jx+%08x / %016jx\n",
			pbase, lbase, lsize, pmask);
	}
	KKASSERT(pbase != 0 && ((lbase + lsize - 1) & pmask) == pbase);
	*isgoodp = 0;

	/*
	 * Access/Allocate the DIO, bump dio->refs to prevent destruction.
	 *
	 * If DIO_GOOD is set the ref should prevent it from being cleared
	 * out from under us, we can set *isgoodp, and the caller can operate
	 * on the buffer without any further interaction.
	 */
	hammer2_spin_sh(&hmp->io_spin);
	dio = RB_LOOKUP(hammer2_io_tree, &hmp->iotree, pbase);
	if (dio) {
		refs = atomic_fetchadd_64(&dio->refs, 1);
		if ((refs & HAMMER2_DIO_MASK) == 0) {
			atomic_add_int(&dio->hmp->iofree_count, -1);
		}
		if (refs & HAMMER2_DIO_GOOD)
			*isgoodp = 1;
		hammer2_spin_unsh(&hmp->io_spin);
	} else if (createit) {
		refs = 0;
		hammer2_spin_unsh(&hmp->io_spin);
		dio = kmalloc(sizeof(*dio), M_HAMMER2, M_INTWAIT | M_ZERO);
		dio->hmp = hmp;
		dio->pbase = pbase;
		dio->psize = psize;
		dio->btype = btype;
		dio->refs = refs + 1;
		dio->act = 5;
		hammer2_spin_ex(&hmp->io_spin);
		xio = RB_INSERT(hammer2_io_tree, &hmp->iotree, dio);
		if (xio == NULL) {
			atomic_add_int(&hammer2_dio_count, 1);
			hammer2_spin_unex(&hmp->io_spin);
		} else {
			refs = atomic_fetchadd_64(&xio->refs, 1);
			if ((refs & HAMMER2_DIO_MASK) == 0)
				atomic_add_int(&xio->hmp->iofree_count, -1);
			if (refs & HAMMER2_DIO_GOOD)
				*isgoodp = 1;
			hammer2_spin_unex(&hmp->io_spin);
			kfree(dio, M_HAMMER2);
			dio = xio;
		}
	} else {
		hammer2_spin_unsh(&hmp->io_spin);
		return NULL;
	}
	dio->ticks = ticks;
	if (dio->act < 10)
		++dio->act;

	return dio;
}

/*
 * Acquire the requested dio.  If DIO_GOOD is not set we must instantiate
 * a buffer.  If set the buffer already exists and is good to go.
 */
hammer2_io_t *
_hammer2_io_getblk(hammer2_dev_t *hmp, int btype, off_t lbase,
		   int lsize, int op HAMMER2_IO_DEBUG_ARGS)
{
	hammer2_io_t *dio;
	off_t peof;
	uint64_t orefs;
	uint64_t nrefs;
	int isgood;
	int error;
	int hce;
	int bflags;

	bflags = ((btype == HAMMER2_BREF_TYPE_DATA) ? B_NOTMETA : 0);
	bflags |= B_KVABIO;

	KKASSERT((1 << (int)(lbase & HAMMER2_OFF_MASK_RADIX)) == lsize);

	if (op == HAMMER2_DOP_READQ) {
		dio = hammer2_io_alloc(hmp, lbase, btype, 0, &isgood);
		if (dio == NULL)
			return NULL;
		op = HAMMER2_DOP_READ;
	} else {
		dio = hammer2_io_alloc(hmp, lbase, btype, 1, &isgood);
	}

	for (;;) {
		orefs = dio->refs;
		cpu_ccfence();

		/*
		 * Buffer is already good, handle the op and return.
		 */
		if (orefs & HAMMER2_DIO_GOOD) {
			if (isgood == 0)
				cpu_mfence();
			bkvasync(dio->bp);

			switch(op) {
			case HAMMER2_DOP_NEW:
				bzero(hammer2_io_data(dio, lbase), lsize);
				/* fall through */
			case HAMMER2_DOP_NEWNZ:
				atomic_set_long(&dio->refs, HAMMER2_DIO_DIRTY);
				break;
			case HAMMER2_DOP_READ:
			default:
				/* nothing to do */
				break;
			}
			DIO_RECORD(dio HAMMER2_IO_DEBUG_CALL);
			return (dio);
		}

		/*
		 * Try to own the DIO
		 */
		if (orefs & HAMMER2_DIO_INPROG) {
			nrefs = orefs | HAMMER2_DIO_WAITING;
			tsleep_interlock(dio, 0);
			if (atomic_cmpset_64(&dio->refs, orefs, nrefs)) {
				tsleep(dio, PINTERLOCKED, "h2dio", hz);
			}
			/* retry */
		} else {
			nrefs = orefs | HAMMER2_DIO_INPROG;
			if (atomic_cmpset_64(&dio->refs, orefs, nrefs)) {
				break;
			}
		}
	}

	/*
	 * We break to here if GOOD is not set and we acquired INPROG for
	 * the I/O.
	 */
	KKASSERT(dio->bp == NULL);
	if (btype == HAMMER2_BREF_TYPE_DATA)
		hce = hammer2_cluster_data_read;
	else
		hce = hammer2_cluster_meta_read;

	error = 0;
	if (dio->pbase == (lbase & ~HAMMER2_OFF_MASK_RADIX) &&
	    dio->psize == lsize) {
		switch(op) {
		case HAMMER2_DOP_NEW:
		case HAMMER2_DOP_NEWNZ:
			dio->bp = getblk(dio->hmp->devvp,
					 dio->pbase, dio->psize,
					 GETBLK_KVABIO, 0);
			if (op == HAMMER2_DOP_NEW) {
				bkvasync(dio->bp);
				bzero(dio->bp->b_data, dio->psize);
			}
			atomic_set_long(&dio->refs, HAMMER2_DIO_DIRTY);
			break;
		case HAMMER2_DOP_READ:
		default:
			KKASSERT(dio->bp == NULL);
			if (hce > 0) {
				/*
				 * Synchronous cluster I/O for now.
				 */
				peof = (dio->pbase + HAMMER2_SEGMASK64) &
				       ~HAMMER2_SEGMASK64;
				error = cluster_readx(dio->hmp->devvp,
						     peof, dio->pbase,
						     dio->psize, bflags,
						     dio->psize,
						     HAMMER2_PBUFSIZE*hce,
						     &dio->bp);
			} else {
				error = breadnx(dio->hmp->devvp, dio->pbase,
						dio->psize, bflags,
					        NULL, NULL, 0, &dio->bp);
			}
		}
	} else {
		if (hce > 0) {
			/*
			 * Synchronous cluster I/O for now.
			 */
			peof = (dio->pbase + HAMMER2_SEGMASK64) &
			       ~HAMMER2_SEGMASK64;
			error = cluster_readx(dio->hmp->devvp,
					      peof, dio->pbase, dio->psize,
					      bflags,
					      dio->psize, HAMMER2_PBUFSIZE*hce,
					      &dio->bp);
		} else {
			error = breadnx(dio->hmp->devvp, dio->pbase,
				        dio->psize, bflags,
					NULL, NULL, 0, &dio->bp);
		}
		if (dio->bp) {
			/*
			 * Handle NEW flags
			 */
			switch(op) {
			case HAMMER2_DOP_NEW:
				bkvasync(dio->bp);
				bzero(hammer2_io_data(dio, lbase), lsize);
				/* fall through */
			case HAMMER2_DOP_NEWNZ:
				atomic_set_long(&dio->refs, HAMMER2_DIO_DIRTY);
				break;
			case HAMMER2_DOP_READ:
			default:
				break;
			}

			/*
			 * Tell the kernel that the buffer cache is not
			 * meta-data based on the btype.  This allows
			 * swapcache to distinguish between data and
			 * meta-data.
			 */
			switch(btype) {
			case HAMMER2_BREF_TYPE_DATA:
				dio->bp->b_flags |= B_NOTMETA;
				break;
			default:
				break;
			}
		}
	}

	if (dio->bp) {
		bkvasync(dio->bp);
		BUF_KERNPROC(dio->bp);
		dio->bp->b_flags &= ~B_AGE;
		/* dio->bp->b_debug_info2 = dio; */
	}
	dio->error = error;

	/*
	 * Clear INPROG and WAITING, set GOOD wake up anyone waiting.
	 */
	for (;;) {
		orefs = dio->refs;
		cpu_ccfence();
		nrefs = orefs & ~(HAMMER2_DIO_INPROG | HAMMER2_DIO_WAITING);
		if (error == 0)
			nrefs |= HAMMER2_DIO_GOOD;
		if (atomic_cmpset_64(&dio->refs, orefs, nrefs)) {
			if (orefs & HAMMER2_DIO_WAITING)
				wakeup(dio);
			break;
		}
		cpu_pause();
	}

	/* XXX error handling */
	DIO_RECORD(dio HAMMER2_IO_DEBUG_CALL);

	return dio;
}

/*
 * Release our ref on *diop.
 *
 * On the 1->0 transition we clear DIO_GOOD, set DIO_INPROG, and dispose
 * of dio->bp.  Then we clean up DIO_INPROG and DIO_WAITING.
 */
void
_hammer2_io_putblk(hammer2_io_t **diop HAMMER2_IO_DEBUG_ARGS)
{
	hammer2_dev_t *hmp;
	hammer2_io_t *dio;
	struct buf *bp;
	off_t pbase;
	int psize;
	int dio_limit;
	uint64_t orefs;
	uint64_t nrefs;

	dio = *diop;
	*diop = NULL;
	hmp = dio->hmp;
	DIO_RECORD(dio HAMMER2_IO_DEBUG_CALL);

	KKASSERT((dio->refs & HAMMER2_DIO_MASK) != 0);

	/*
	 * Drop refs.
	 *
	 * On the 1->0 transition clear GOOD and set INPROG, and break.
	 * On any other transition we can return early.
	 */
	for (;;) {
		orefs = dio->refs;
		cpu_ccfence();

		if ((orefs & HAMMER2_DIO_MASK) == 1 &&
		    (orefs & HAMMER2_DIO_INPROG) == 0) {
			/*
			 * Lastdrop case, INPROG can be set.  GOOD must be
			 * cleared to prevent the getblk shortcut.
			 */
			nrefs = orefs - 1;
			nrefs &= ~(HAMMER2_DIO_GOOD | HAMMER2_DIO_DIRTY);
			nrefs |= HAMMER2_DIO_INPROG;
			if (atomic_cmpset_64(&dio->refs, orefs, nrefs))
				break;
		} else if ((orefs & HAMMER2_DIO_MASK) == 1) {
			/*
			 * Lastdrop case, INPROG already set.  We must
			 * wait for INPROG to clear.
			 */
			nrefs = orefs | HAMMER2_DIO_WAITING;
			tsleep_interlock(dio, 0);
			if (atomic_cmpset_64(&dio->refs, orefs, nrefs)) {
				tsleep(dio, PINTERLOCKED, "h2dio", hz);
			}
			/* retry */
		} else {
			/*
			 * Normal drop case.
			 */
			nrefs = orefs - 1;
			if (atomic_cmpset_64(&dio->refs, orefs, nrefs))
				return;
			/* retry */
		}
		cpu_pause();
		/* retry */
	}

	/*
	 * Lastdrop (1->0 transition).  INPROG has been set, GOOD and DIRTY
	 * have been cleared.  iofree_count has not yet been incremented,
	 * note that another accessor race will decrement iofree_count so
	 * we have to increment it regardless.
	 *
	 * We can now dispose of the buffer, and should do it before calling
	 * io_complete() in case there's a race against a new reference
	 * which causes io_complete() to chain and instantiate the bp again.
	 */
	pbase = dio->pbase;
	psize = dio->psize;
	bp = dio->bp;
	dio->bp = NULL;

	if ((orefs & HAMMER2_DIO_GOOD) && bp) {
		/*
		 * Non-errored disposal of bp
		 */
		if (orefs & HAMMER2_DIO_DIRTY) {
			dio_write_stats_update(dio, bp);

			/*
			 * Allows dirty buffers to accumulate and
			 * possibly be canceled (e.g. by a 'rm'),
			 * by default we will burst-write later.
			 *
			 * We generally do NOT want to issue an actual
			 * b[a]write() or cluster_write() here.  Due to
			 * the way chains are locked, buffers may be cycled
			 * in and out quite often and disposal here can cause
			 * multiple writes or write-read stalls.
			 *
			 * If FLUSH is set we do want to issue the actual
			 * write.  This typically occurs in the write-behind
			 * case when writing to large files.
			 */
			off_t peof;
			int hce;
			if (dio->refs & HAMMER2_DIO_FLUSH) {
				if ((hce = hammer2_cluster_write) != 0) {
					peof = (pbase + HAMMER2_SEGMASK64) &
					       ~HAMMER2_SEGMASK64;
					bp->b_flags |= B_CLUSTEROK;
					cluster_write(bp, peof, psize, hce);
				} else {
					bp->b_flags &= ~B_CLUSTEROK;
					bawrite(bp);
				}
			} else {
				bp->b_flags &= ~B_CLUSTEROK;
				bdwrite(bp);
			}
		} else if (bp->b_flags & (B_ERROR | B_INVAL | B_RELBUF)) {
			brelse(bp);
		} else {
			bqrelse(bp);
		}
	} else if (bp) {
		/*
		 * Errored disposal of bp
		 */
		brelse(bp);
	}

	/*
	 * Update iofree_count before disposing of the dio
	 */
	hmp = dio->hmp;
	atomic_add_int(&hmp->iofree_count, 1);

	/*
	 * Clear INPROG, GOOD, and WAITING (GOOD should already be clear).
	 *
	 * Also clear FLUSH as it was handled above.
	 */
	for (;;) {
		orefs = dio->refs;
		cpu_ccfence();
		nrefs = orefs & ~(HAMMER2_DIO_INPROG | HAMMER2_DIO_GOOD |
				  HAMMER2_DIO_WAITING | HAMMER2_DIO_FLUSH);
		if (atomic_cmpset_64(&dio->refs, orefs, nrefs)) {
			if (orefs & HAMMER2_DIO_WAITING)
				wakeup(dio);
			break;
		}
		cpu_pause();
	}

	/*
	 * We cache free buffers so re-use cases can use a shared lock, but
	 * if too many build up we have to clean them out.
	 */
	dio_limit = hammer2_dio_limit;
	if (dio_limit < 256)
		dio_limit = 256;
	if (dio_limit > 1024*1024)
		dio_limit = 1024*1024;
	if (hmp->iofree_count > dio_limit) {
		struct hammer2_cleanupcb_info info;

		RB_INIT(&info.tmptree);
		hammer2_spin_ex(&hmp->io_spin);
		if (hmp->iofree_count > dio_limit) {
			info.count = hmp->iofree_count / 5;
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
			int act;

			act = dio->act - (ticks - dio->ticks) / hz - 1;
			if (act > 0) {
				dio->act = act;
				return 0;
			}
			dio->act = 0;
		}
		KKASSERT(dio->bp == NULL);
		if (info->count > 0) {
			RB_REMOVE(hammer2_io_tree, &dio->hmp->iotree, dio);
			xio = RB_INSERT(hammer2_io_tree, &info->tmptree, dio);
			KKASSERT(xio == NULL);
			--info->count;
		}
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
		if (dio->refs & HAMMER2_DIO_DIRTY) {
			kprintf("hammer2_io_cleanup: Dirty buffer "
				"%016jx/%d (bp=%p)\n",
				dio->pbase, dio->psize, dio->bp);
		}
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
	bkvasync(bp);
	off = (lbase & ~HAMMER2_OFF_MASK_RADIX) - bp->b_loffset;
	KKASSERT(off >= 0 && off < bp->b_bufsize);
	return(bp->b_data + off);
}

int
hammer2_io_new(hammer2_dev_t *hmp, int btype, off_t lbase, int lsize,
	       hammer2_io_t **diop)
{
	*diop = hammer2_io_getblk(hmp, btype, lbase, lsize, HAMMER2_DOP_NEW);
	return ((*diop)->error);
}

int
hammer2_io_newnz(hammer2_dev_t *hmp, int btype, off_t lbase, int lsize,
		 hammer2_io_t **diop)
{
	*diop = hammer2_io_getblk(hmp, btype, lbase, lsize, HAMMER2_DOP_NEWNZ);
	return ((*diop)->error);
}

int
_hammer2_io_bread(hammer2_dev_t *hmp, int btype, off_t lbase, int lsize,
		hammer2_io_t **diop HAMMER2_IO_DEBUG_ARGS)
{
#ifdef HAMMER2_IO_DEBUG
	hammer2_io_t *dio;
#endif

	*diop = _hammer2_io_getblk(hmp, btype, lbase, lsize,
				   HAMMER2_DOP_READ HAMMER2_IO_DEBUG_CALL);
#ifdef HAMMER2_IO_DEBUG
	if ((dio = *diop) != NULL) {
		int i = (dio->debug_index - 1) & HAMMER2_IO_DEBUG_MASK;
		dio->debug_data[i] = debug_data;
	}
#endif
	return ((*diop)->error);
}

hammer2_io_t *
_hammer2_io_getquick(hammer2_dev_t *hmp, off_t lbase,
		     int lsize HAMMER2_IO_DEBUG_ARGS)
{
	hammer2_io_t *dio;

	dio = _hammer2_io_getblk(hmp, 0, lbase, lsize,
				 HAMMER2_DOP_READQ HAMMER2_IO_DEBUG_CALL);
	return dio;
}

void
_hammer2_io_bawrite(hammer2_io_t **diop HAMMER2_IO_DEBUG_ARGS)
{
	atomic_set_64(&(*diop)->refs, HAMMER2_DIO_DIRTY |
				      HAMMER2_DIO_FLUSH);
	_hammer2_io_putblk(diop HAMMER2_IO_DEBUG_CALL);
}

void
_hammer2_io_bdwrite(hammer2_io_t **diop HAMMER2_IO_DEBUG_ARGS)
{
	atomic_set_64(&(*diop)->refs, HAMMER2_DIO_DIRTY);
	_hammer2_io_putblk(diop HAMMER2_IO_DEBUG_CALL);
}

int
_hammer2_io_bwrite(hammer2_io_t **diop HAMMER2_IO_DEBUG_ARGS)
{
	atomic_set_64(&(*diop)->refs, HAMMER2_DIO_DIRTY |
				      HAMMER2_DIO_FLUSH);
	_hammer2_io_putblk(diop HAMMER2_IO_DEBUG_CALL);
	return (0);	/* XXX */
}

void
hammer2_io_setdirty(hammer2_io_t *dio)
{
	atomic_set_64(&dio->refs, HAMMER2_DIO_DIRTY);
}

/*
 * This routine is called when a MODIFIED chain is being DESTROYED,
 * in an attempt to allow the related buffer cache buffer to be
 * invalidated and discarded instead of flushing it to disk.
 *
 * At the moment this case is only really useful for file meta-data.
 * File data is already handled via the logical buffer cache associated
 * with the vnode, and will be discarded if it was never flushed to disk.
 * File meta-data may include inodes, directory entries, and indirect blocks.
 *
 * XXX
 * However, our DIO buffers are PBUFSIZE'd (64KB), and the area being
 * invalidated might be smaller.  Most of the meta-data structures above
 * are in the 'smaller' category.  For now, don't try to invalidate the
 * data areas.
 */
void
hammer2_io_inval(hammer2_io_t *dio, hammer2_off_t data_off, u_int bytes)
{
	/* NOP */
}

void
_hammer2_io_brelse(hammer2_io_t **diop HAMMER2_IO_DEBUG_ARGS)
{
	_hammer2_io_putblk(diop HAMMER2_IO_DEBUG_CALL);
}

void
_hammer2_io_bqrelse(hammer2_io_t **diop HAMMER2_IO_DEBUG_ARGS)
{
	_hammer2_io_putblk(diop HAMMER2_IO_DEBUG_CALL);
}

/*
 * Set dedup validation bits in a DIO.  We do not need the buffer cache
 * buffer for this.  This must be done concurrent with setting bits in
 * the freemap so as to interlock with bulkfree's clearing of those bits.
 */
void
hammer2_io_dedup_set(hammer2_dev_t *hmp, hammer2_blockref_t *bref)
{
	hammer2_io_t *dio;
	uint64_t mask;
	int lsize;
	int isgood;

	dio = hammer2_io_alloc(hmp, bref->data_off, bref->type, 1, &isgood);
	lsize = 1 << (int)(bref->data_off & HAMMER2_OFF_MASK_RADIX);
	mask = hammer2_dedup_mask(dio, bref->data_off, lsize);
	atomic_clear_64(&dio->dedup_valid, mask);
	atomic_set_64(&dio->dedup_alloc, mask);
	hammer2_io_putblk(&dio);
}

/*
 * Clear dedup validation bits in a DIO.  This is typically done when
 * a modified chain is destroyed or by the bulkfree code.  No buffer
 * is needed for this operation.  If the DIO no longer exists it is
 * equivalent to the bits not being set.
 */
void
hammer2_io_dedup_delete(hammer2_dev_t *hmp, uint8_t btype,
			hammer2_off_t data_off, u_int bytes)
{
	hammer2_io_t *dio;
	uint64_t mask;
	int isgood;

	if ((data_off & ~HAMMER2_OFF_MASK_RADIX) == 0)
		return;
	if (btype != HAMMER2_BREF_TYPE_DATA)
		return;
	dio = hammer2_io_alloc(hmp, data_off, btype, 0, &isgood);
	if (dio) {
		if (data_off < dio->pbase ||
		    (data_off & ~HAMMER2_OFF_MASK_RADIX) + bytes >
		    dio->pbase + dio->psize) {
			panic("hammer2_io_dedup_delete: DATAOFF BAD "
			      "%016jx/%d %016jx\n",
			      data_off, bytes, dio->pbase);
		}
		mask = hammer2_dedup_mask(dio, data_off, bytes);
		atomic_clear_64(&dio->dedup_alloc, mask);
		atomic_clear_64(&dio->dedup_valid, mask);
		hammer2_io_putblk(&dio);
	}
}

/*
 * Assert that dedup allocation bits in a DIO are not set.  This operation
 * does not require a buffer.  The DIO does not need to exist.
 */
void
hammer2_io_dedup_assert(hammer2_dev_t *hmp, hammer2_off_t data_off, u_int bytes)
{
	hammer2_io_t *dio;
	int isgood;

	dio = hammer2_io_alloc(hmp, data_off, HAMMER2_BREF_TYPE_DATA,
			       0, &isgood);
	if (dio) {
		KASSERT((dio->dedup_alloc &
			  hammer2_dedup_mask(dio, data_off, bytes)) == 0,
			("hammer2_dedup_assert: %016jx/%d %016jx/%016jx",
			data_off,
			bytes,
			hammer2_dedup_mask(dio, data_off, bytes),
			dio->dedup_alloc));
		hammer2_io_putblk(&dio);
	}
}

static
void
dio_write_stats_update(hammer2_io_t *dio, struct buf *bp)
{
	if (bp->b_flags & B_DELWRI)
		return;
	hammer2_adjwritecounter(dio->btype, dio->psize);
}

void
hammer2_io_bkvasync(hammer2_io_t *dio)
{
	KKASSERT(dio->bp != NULL);
	bkvasync(dio->bp);
}

/*
 * Ref a dio that is already owned
 */
void
_hammer2_io_ref(hammer2_io_t *dio HAMMER2_IO_DEBUG_ARGS)
{
	DIO_RECORD(dio HAMMER2_IO_DEBUG_CALL);
	atomic_add_64(&dio->refs, 1);
}
