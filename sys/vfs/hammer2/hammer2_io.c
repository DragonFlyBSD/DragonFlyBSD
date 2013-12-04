/*
 * Copyright (c) 2013 The DragonFly Project.  All rights reserved.
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
static void hammer2_io_callback(struct bio *bio);
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


#define HAMMER2_DIO_INPROG	0x80000000
#define HAMMER2_DIO_GOOD	0x40000000
#define HAMMER2_DIO_WAITING	0x20000000
#define HAMMER2_DIO_DIRTY	0x10000000

#define HAMMER2_DIO_MASK	0x0FFFFFFF

/*
 * Acquire the requested dio, set *ownerp based on state.  If state is good
 * *ownerp is set to 0, otherwise *ownerp is set to DIO_INPROG and the
 * caller must resolve the buffer.
 */
hammer2_io_t *
hammer2_io_getblk(hammer2_mount_t *hmp, off_t lbase, int lsize, int *ownerp)
{
	hammer2_io_t *dio;
	hammer2_io_t *xio;
	off_t pbase;
	off_t pmask;
	int psize = hammer2_devblksize(lsize);
	int refs;

	pmask = ~(hammer2_off_t)(psize - 1);

	KKASSERT((1 << (int)(lbase & HAMMER2_OFF_MASK_RADIX)) == lsize);
	lbase &= ~HAMMER2_OFF_MASK_RADIX;
	pbase = lbase & pmask;
	KKASSERT(pbase != 0 && ((lbase + lsize - 1) & pmask) == pbase);

	/*
	 * Access/Allocate the DIO
	 */
	spin_lock_shared(&hmp->io_spin);
	dio = RB_LOOKUP(hammer2_io_tree, &hmp->iotree, pbase);
	if (dio) {
		if ((atomic_fetchadd_int(&dio->refs, 1) &
		     HAMMER2_DIO_MASK) == 0) {
			atomic_add_int(&dio->hmp->iofree_count, -1);
		}
		spin_unlock_shared(&hmp->io_spin);
	} else {
		spin_unlock_shared(&hmp->io_spin);
		dio = kmalloc(sizeof(*dio), M_HAMMER2, M_INTWAIT | M_ZERO);
		dio->hmp = hmp;
		dio->pbase = pbase;
		dio->psize = psize;
		dio->refs = 1;
		spin_lock(&hmp->io_spin);
		xio = RB_INSERT(hammer2_io_tree, &hmp->iotree, dio);
		if (xio == NULL) {
			spin_unlock(&hmp->io_spin);
		} else {
			if ((atomic_fetchadd_int(&xio->refs, 1) &
			     HAMMER2_DIO_MASK) == 0) {
				atomic_add_int(&xio->hmp->iofree_count, -1);
			}
			spin_unlock(&hmp->io_spin);
			kfree(dio, M_HAMMER2);
			dio = xio;
		}
	}

	/*
	 * Obtain/Validate the buffer.
	 */
	for (;;) {
		refs = dio->refs;
		cpu_ccfence();

		/*
		 * Stop if the buffer is good.  Once set GOOD the flag cannot
		 * be cleared until refs drops to 0.
		 */
		if (refs & HAMMER2_DIO_GOOD) {
			*ownerp = 0;
			goto done;
		}

		/*
		 * We need to acquire the in-progress lock on the buffer
		 */
		if (refs & HAMMER2_DIO_INPROG) {
			tsleep_interlock(dio, 0);
			if (atomic_cmpset_int(&dio->refs, refs,
					      refs | HAMMER2_DIO_WAITING)) {
				tsleep(dio, PINTERLOCKED, "h2dio", 0);
			}
			/* retry */
		} else {
			if (atomic_cmpset_int(&dio->refs, refs,
					      refs | HAMMER2_DIO_INPROG)) {
				break;
			}
		}
		/* retry */
	}

	/*
	 * We need to do more work before the buffer is usable
	 */
	*ownerp = HAMMER2_DIO_INPROG;
done:
	if (dio->act < 5)
		++dio->act;
	return(dio);
}

/*
 * If part of an asynchronous I/O the asynchronous I/O is biodone()'d.
 *
 * If the caller owned INPROG then the dio will be set GOOD or not
 * depending on whether the caller disposed of dio->bp or not.
 */
static
void
hammer2_io_complete(hammer2_io_t *dio, int owner)
{
	int refs;
	int good;

	while (owner & HAMMER2_DIO_INPROG) {
		refs = dio->refs;
		cpu_ccfence();
		good = dio->bp ? HAMMER2_DIO_GOOD : 0;
		if (atomic_cmpset_int(&dio->refs, refs,
				      (refs & ~(HAMMER2_DIO_WAITING |
					        HAMMER2_DIO_INPROG)) |
				      good)) {
			if (refs & HAMMER2_DIO_WAITING)
				wakeup(dio);
			if (good)
				BUF_KERNPROC(dio->bp);
			break;
		}
		/* retry */
	}
}

/*
 * Release our ref on *diop, dispose of the underlying buffer.
 */
void
hammer2_io_putblk(hammer2_io_t **diop)
{
	hammer2_mount_t *hmp;
	hammer2_io_t *dio;
	struct buf *bp;
	off_t peof;
	off_t pbase;
	int psize;
	int refs;

	dio = *diop;
	*diop = NULL;

	for (;;) {
		refs = dio->refs;

		if ((refs & HAMMER2_DIO_MASK) == 1) {
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
	 * Locked INPROG on 1->0 transition and we cleared DIO_GOOD (which is
	 * legal only on the last ref).  This allows us to dispose of the
	 * buffer.  refs is now 0.
	 *
	 * The instant we call io_complete dio is a free agent again and
	 * can be ripped out from under us.  Acquisition of the dio after
	 * this point will require a shared or exclusive spinlock.
	 */
	hmp = dio->hmp;
	bp = dio->bp;
	dio->bp = NULL;
	pbase = dio->pbase;
	psize = dio->psize;
	atomic_add_int(&hmp->iofree_count, 1);
	hammer2_io_complete(dio, HAMMER2_DIO_INPROG);	/* clears INPROG */
	dio = NULL;	/* dio stale */

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
	}

	/*
	 * We cache free buffers so re-use cases can use a shared lock, but
	 * if too many build up we have to clean them out.
	 */
	if (hmp->iofree_count > 1000) {
		struct hammer2_cleanupcb_info info;

		RB_INIT(&info.tmptree);
		spin_lock(&hmp->io_spin);
		if (hmp->iofree_count > 1000) {
			info.count = hmp->iofree_count / 2;
			RB_SCAN(hammer2_io_tree, &hmp->iotree, NULL,
				hammer2_io_cleanup_callback, &info);
		}
		spin_unlock(&hmp->io_spin);
		hammer2_io_cleanup(hmp, &info.tmptree);
	}
}

/*
 * Cleanup any dio's with no references which are not in-progress.
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
		atomic_add_int(&hmp->iofree_count, -1);
	}
}

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

static
int
_hammer2_io_new(hammer2_mount_t *hmp, off_t lbase, int lsize,
	        hammer2_io_t **diop, int dozero, int quick)
{
	hammer2_io_t *dio;
	int owner;
	int error;

	dio = *diop = hammer2_io_getblk(hmp, lbase, lsize, &owner);
	if (owner) {
		if (lsize == dio->psize) {
			dio->bp = getblk(hmp->devvp,
					     dio->pbase, dio->psize,
					     (quick ? GETBLK_NOWAIT : 0),
					     0);
			if (dio->bp) {
				vfs_bio_clrbuf(dio->bp);
				if (quick) {
					dio->bp->b_flags |= B_CACHE;
					bqrelse(dio->bp);
					dio->bp = NULL;
				}
			}
			error = 0;
		} else if (quick) {
			/* do nothing */
			error = 0;
		} else {
			error = bread(hmp->devvp, dio->pbase,
				      dio->psize, &dio->bp);
		}
		if (error) {
			brelse(dio->bp);
			dio->bp = NULL;
		}
		hammer2_io_complete(dio, owner);
	} else {
		error = 0;
	}
	if (dio->bp) {
		if (dozero)
			bzero(hammer2_io_data(dio, lbase), lsize);
		atomic_set_int(&dio->refs, HAMMER2_DIO_DIRTY);
	}
	return error;
}

int
hammer2_io_new(hammer2_mount_t *hmp, off_t lbase, int lsize,
	       hammer2_io_t **diop)
{
	return(_hammer2_io_new(hmp, lbase, lsize, diop, 1, 0));
}

int
hammer2_io_newnz(hammer2_mount_t *hmp, off_t lbase, int lsize,
	       hammer2_io_t **diop)
{
	return(_hammer2_io_new(hmp, lbase, lsize, diop, 0, 0));
}

int
hammer2_io_newq(hammer2_mount_t *hmp, off_t lbase, int lsize,
	       hammer2_io_t **diop)
{
	return(_hammer2_io_new(hmp, lbase, lsize, diop, 0, 1));
}

int
hammer2_io_bread(hammer2_mount_t *hmp, off_t lbase, int lsize,
		hammer2_io_t **diop)
{
	hammer2_io_t *dio;
	off_t peof;
	int owner;
	int error;

	dio = *diop = hammer2_io_getblk(hmp, lbase, lsize, &owner);
	if (owner) {
		if (hammer2_cluster_enable) {
			peof = (dio->pbase + HAMMER2_SEGMASK64) &
			       ~HAMMER2_SEGMASK64;
			error = cluster_read(hmp->devvp, peof, dio->pbase,
					     dio->psize,
					     dio->psize, HAMMER2_PBUFSIZE*4,
					     &dio->bp);
		} else {
			error = bread(hmp->devvp, dio->pbase,
				      dio->psize, &dio->bp);
		}
		if (error) {
			brelse(dio->bp);
			dio->bp = NULL;
		}
		hammer2_io_complete(dio, owner);
	} else {
		error = 0;
	}
	return error;
}

void
hammer2_io_breadcb(hammer2_mount_t *hmp, off_t lbase, int lsize,
		  void (*callback)(hammer2_io_t *dio, hammer2_chain_t *arg_c,
				   void *arg_p, off_t arg_o),
		  hammer2_chain_t *arg_c, void *arg_p, off_t arg_o)
{
	hammer2_io_t *dio;
	int owner;
	int error;

	dio = hammer2_io_getblk(hmp, lbase, lsize, &owner);
	if (owner) {
		dio->callback = callback;
		dio->arg_c = arg_c;
		dio->arg_p = arg_p;
		dio->arg_o = arg_o;
		breadcb(hmp->devvp, dio->pbase, dio->psize,
			hammer2_io_callback, dio);
	} else {
		error = 0;
		callback(dio, arg_c, arg_p, arg_o);
		hammer2_io_bqrelse(&dio);
	}
}

static void
hammer2_io_callback(struct bio *bio)
{
	struct buf *dbp = bio->bio_buf;
	hammer2_io_t *dio = bio->bio_caller_info1.ptr;

	if ((bio->bio_flags & BIO_DONE) == 0)
		bpdone(dbp, 0);
	bio->bio_flags &= ~(BIO_DONE | BIO_SYNC);
	dio->bp = bio->bio_buf;
	KKASSERT((dio->bp->b_flags & B_ERROR) == 0); /* XXX */
	hammer2_io_complete(dio, HAMMER2_DIO_INPROG);

	/*
	 * We still have the ref and DIO_GOOD is now set so nothing else
	 * should mess with the callback fields until we release the dio.
	 */
	dio->callback(dio, dio->arg_c, dio->arg_p, dio->arg_o);
	hammer2_io_bqrelse(&dio);
	/* TODO: async load meta-data and assign chain->dio */
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
