/*
 * Copyright (c) 2006 The DragonFly Project.  All rights reserved.
 * 
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
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
 * 
 * $DragonFly: src/sys/kern/kern_ccms.c,v 1.4 2007/04/30 07:18:53 dillon Exp $
 */
/*
 * The Cache Coherency Management System (CCMS)
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/objcache.h>
#include <sys/ccms.h>
#include <sys/sysctl.h>
#include <sys/uio.h>
#include <machine/limits.h>

#include <sys/spinlock2.h>

struct ccms_lock_scan_info {
	ccms_dataspace_t ds;
	ccms_lock_t lock;
	ccms_cst_t  cst1;
	ccms_cst_t  cst2;
	ccms_cst_t  coll_cst;
};

static int ccms_cst_cmp(ccms_cst_t b1, ccms_cst_t b2);
static int ccms_lock_scan_cmp(ccms_cst_t b1, void *arg);
static int ccms_lock_undo_cmp(ccms_cst_t b1, void *arg);
static int ccms_dataspace_destroy_match(ccms_cst_t cst, void *arg);
static int ccms_lock_get_match(struct ccms_cst *cst, void *arg);
static int ccms_lock_undo_match(struct ccms_cst *cst, void *arg);
static int ccms_lock_redo_match(struct ccms_cst *cst, void *arg);
static int ccms_lock_put_match(struct ccms_cst *cst, void *arg);

RB_GENERATE3(ccms_rb_tree, ccms_cst, rbnode, ccms_cst_cmp,
	     off_t, beg_offset, end_offset);
static MALLOC_DEFINE(M_CCMS, "CCMS", "Cache Coherency Management System");

static int ccms_enable;
SYSCTL_INT(_kern, OID_AUTO, ccms_enable, CTLFLAG_RW, &ccms_enable, 0, "");

static struct objcache *ccms_oc;

/*
 * Initialize the CCMS subsystem
 */
static void
ccmsinit(void *dummy)
{
    ccms_oc = objcache_create_simple(M_CCMS, sizeof(struct ccms_cst));
}
SYSINIT(ccms, SI_BOOT2_MACHDEP, SI_ORDER_ANY, ccmsinit, NULL);

/*
 * Initialize a new CCMS dataspace.  Create a new RB tree with a single
 * element covering the entire 64 bit offset range.  This simplifies 
 * algorithms enormously by removing a number of special cases.
 */
void
ccms_dataspace_init(ccms_dataspace_t ds)
{
    ccms_cst_t cst;

    RB_INIT(&ds->tree);
    ds->info = NULL;
    ds->chain = NULL;
    cst = objcache_get(ccms_oc, M_WAITOK);
    bzero(cst, sizeof(*cst));
    cst->beg_offset = LLONG_MIN;
    cst->end_offset = LLONG_MAX;
    cst->state = CCMS_STATE_INVALID;
    RB_INSERT(ccms_rb_tree, &ds->tree, cst);
    spin_init(&ds->spin);
}

/*
 * Helper to destroy deleted cst's.
 */
static __inline
void
ccms_delayed_free(ccms_cst_t cstn)
{
    ccms_cst_t cst;

    while((cst = cstn) != NULL) {
	cstn = cst->delayed_next;
	objcache_put(ccms_oc, cst);
    }
}

/*
 * Destroy a CCMS dataspace.
 *
 * MPSAFE
 */
void
ccms_dataspace_destroy(ccms_dataspace_t ds)
{
    ccms_cst_t cst;

    spin_lock(&ds->spin);
    RB_SCAN(ccms_rb_tree, &ds->tree, NULL,
	    ccms_dataspace_destroy_match, ds);
    cst = ds->delayed_free;
    ds->delayed_free = NULL;
    spin_unlock(&ds->spin);
    ccms_delayed_free(cst);
}

/*
 * Helper routine to delete matches during a destroy.
 *
 * NOTE: called with spinlock held.
 */
static
int
ccms_dataspace_destroy_match(ccms_cst_t cst, void *arg)
{
    ccms_dataspace_t ds = arg;

    RB_REMOVE(ccms_rb_tree, &ds->tree, cst);
    cst->delayed_next = ds->delayed_free;
    ds->delayed_free = cst;
    return(0);
}

/*
 * Obtain a CCMS lock
 *
 * MPSAFE
 */
int
ccms_lock_get(ccms_dataspace_t ds, ccms_lock_t lock)
{
    struct ccms_lock_scan_info info;
    ccms_cst_t cst;

    if (ccms_enable == 0) {
	lock->ds = NULL;
	return(0);
    }

    /*
     * Partition the CST space so the precise range is covered and
     * attempt to obtain the requested local lock (ltype) at the same 
     * time.
     */
    lock->ds = ds;
    info.lock = lock;
    info.ds = ds;
    info.coll_cst = NULL;
    info.cst1 = objcache_get(ccms_oc, M_WAITOK);
    info.cst2 = objcache_get(ccms_oc, M_WAITOK);

    spin_lock(&ds->spin);
    RB_SCAN(ccms_rb_tree, &ds->tree, ccms_lock_scan_cmp,
	    ccms_lock_get_match, &info);

    /*
     * If a collision occured, undo the fragments we were able to obtain,
     * block, and try again.
     */
    while (info.coll_cst != NULL) {
	RB_SCAN(ccms_rb_tree, &ds->tree, ccms_lock_undo_cmp,
		ccms_lock_undo_match, &info);
	info.coll_cst->blocked = 1;
	ssleep(info.coll_cst, &ds->spin, 0,
	       ((lock->ltype == CCMS_LTYPE_SHARED) ? "rngsh" : "rngex"),
	       hz);
	info.coll_cst = NULL;
	RB_SCAN(ccms_rb_tree, &ds->tree, ccms_lock_scan_cmp,
		ccms_lock_redo_match, &info);
    }
    cst = ds->delayed_free;
    ds->delayed_free = NULL;
    spin_unlock(&ds->spin);

    /*
     * Cleanup
     */
    ccms_delayed_free(cst);
    if (info.cst1)
	objcache_put(ccms_oc, info.cst1);
    if (info.cst2)
	objcache_put(ccms_oc, info.cst2);

    return(0);
}

/*
 * Obtain a CCMS lock, initialize the lock structure from the uio.
 *
 * MPSAFE
 */
int
ccms_lock_get_uio(ccms_dataspace_t ds, ccms_lock_t lock, struct uio *uio)
{
    ccms_ltype_t ltype;
    off_t eoff;

    if (uio->uio_rw == UIO_READ)
	ltype = CCMS_LTYPE_SHARED;
    else
	ltype = CCMS_LTYPE_MODIFYING;

    /*
     * Calculate the ending offset (byte inclusive), make sure a seek
     * overflow does not blow us up.
     */
    eoff = uio->uio_offset + uio->uio_resid - 1;
    if (eoff < uio->uio_offset)
	eoff = 0x7FFFFFFFFFFFFFFFLL;
    ccms_lock_init(lock, uio->uio_offset, eoff, ltype);
    return(ccms_lock_get(ds, lock));
}

/*
 * Helper routine.
 *
 * NOTE: called with spinlock held.
 */
static
int
ccms_lock_get_match(ccms_cst_t cst, void *arg)
{
    struct ccms_lock_scan_info *info = arg;
    ccms_lock_t lock = info->lock;
    ccms_cst_t ncst;

    /*
     * If the lock's left edge is within the CST we must split the CST
     * into two pieces [cst][ncst].  lrefs must be bumped on the CST
     * containing the left edge.
     *
     * NOTE! cst->beg_offset may not be modified.  This allows us to avoid
     * having to manipulate the cst's position in the tree.
     */
    if (lock->beg_offset > cst->beg_offset) {
	ncst = info->cst1;
	info->cst1 = NULL;
	KKASSERT(ncst != NULL);
	*ncst = *cst;
	cst->end_offset = lock->beg_offset - 1;
	cst->rrefs = 0;
	ncst->beg_offset = lock->beg_offset;
	ncst->lrefs = 1;
	RB_INSERT(ccms_rb_tree, &info->ds->tree, ncst);

	/*
	 * ncst becomes our 'matching' cst.
	 */
	cst = ncst;
    } else if (lock->beg_offset == cst->beg_offset) {
	++cst->lrefs;
    }

    /*
     * If the lock's right edge is within the CST we must split the CST
     * into two pieces [cst][ncst].  rrefs must be bumped on the CST
     * containing the right edge.
     *
     * NOTE! cst->beg_offset may not be modified.  This allows us to avoid
     * having to manipulate the cst's position in the tree.
     */
    if (lock->end_offset < cst->end_offset) {
	ncst = info->cst2;
	info->cst2 = NULL;
	KKASSERT(ncst != NULL);
	*ncst = *cst;
	cst->end_offset = lock->end_offset;
	cst->rrefs = 1;
	ncst->beg_offset = lock->end_offset + 1;
	ncst->lrefs = 0;
	RB_INSERT(ccms_rb_tree, &info->ds->tree, ncst);
	/* cst remains our 'matching' cst */
    } else if (lock->end_offset == cst->end_offset) {
	++cst->rrefs;
    }

    /*
     * The lock covers the CST, so increment the CST's coverage count.
     * Then attempt to obtain the shared/exclusive ltype.
     */
    ++cst->xrefs;

    if (info->coll_cst == NULL) {
	switch(lock->ltype) {
	case CCMS_LTYPE_SHARED:
	    if (cst->sharecount < 0) {
		info->coll_cst = cst;
	    } else {
		++cst->sharecount;
		if (ccms_enable >= 9) {
			kprintf("CST SHARE %d %lld-%lld\n",
				cst->sharecount,
				(long long)cst->beg_offset,
				(long long)cst->end_offset);
		}
	    }
	    break;
	case CCMS_LTYPE_EXCLUSIVE:
	    if (cst->sharecount != 0) {
		info->coll_cst = cst;
	    } else {
		--cst->sharecount;
		if (ccms_enable >= 9) {
			kprintf("CST EXCLS %d %lld-%lld\n",
				cst->sharecount,
				(long long)cst->beg_offset,
				(long long)cst->end_offset);
		}
	    }
	    break;
	case CCMS_LTYPE_MODIFYING:
	    if (cst->sharecount != 0) {
		info->coll_cst = cst;
	    } else {
		--cst->sharecount;
		++cst->modifycount;
		if (ccms_enable >= 9) {
			kprintf("CST MODXL %d %lld-%lld\n",
				cst->sharecount,
				(long long)cst->beg_offset,
				(long long)cst->end_offset);
		}
	    }
	    break;
	}
    }
    return(0);
}

/*
 * Undo a partially resolved ccms_ltype rangelock.  This is atomic with
 * the scan/redo code so there should not be any blocked locks when
 * transitioning to 0.
 *
 * NOTE: called with spinlock held.
 */
static
int
ccms_lock_undo_match(ccms_cst_t cst, void *arg)
{
    struct ccms_lock_scan_info *info = arg;
    ccms_lock_t lock = info->lock;

    switch(lock->ltype) {
    case CCMS_LTYPE_SHARED:
	KKASSERT(cst->sharecount > 0);
	--cst->sharecount;
	KKASSERT(cst->sharecount || cst->blocked == 0);
	break;
    case CCMS_LTYPE_EXCLUSIVE:
	KKASSERT(cst->sharecount < 0);
	++cst->sharecount;
	KKASSERT(cst->sharecount || cst->blocked == 0);
	break;
    case CCMS_LTYPE_MODIFYING:
	KKASSERT(cst->sharecount < 0 && cst->modifycount > 0);
	++cst->sharecount;
	--cst->modifycount;
	KKASSERT(cst->sharecount || cst->blocked == 0);
	break;
    }
    return(0);
}

/*
 * Redo the local lock request for a range which has already been 
 * partitioned.
 *
 * NOTE: called with spinlock held.
 */
static
int
ccms_lock_redo_match(ccms_cst_t cst, void *arg)
{
    struct ccms_lock_scan_info *info = arg;
    ccms_lock_t lock = info->lock;

    if (info->coll_cst == NULL) {
	switch(lock->ltype) {
	case CCMS_LTYPE_SHARED:
	    if (cst->sharecount < 0) {
		info->coll_cst = cst;
	    } else {
		if (ccms_enable >= 9) {
			kprintf("CST SHARE %d %lld-%lld\n",
				cst->sharecount,
				(long long)cst->beg_offset,
				(long long)cst->end_offset);
		}
		++cst->sharecount;
	    }
	    break;
	case CCMS_LTYPE_EXCLUSIVE:
	    if (cst->sharecount != 0) {
		info->coll_cst = cst;
	    } else {
		--cst->sharecount;
		if (ccms_enable >= 9) {
			kprintf("CST EXCLS %d %lld-%lld\n",
				cst->sharecount,
				(long long)cst->beg_offset,
				(long long)cst->end_offset);
		}
	    }
	    break;
	case CCMS_LTYPE_MODIFYING:
	    if (cst->sharecount != 0) {
		info->coll_cst = cst;
	    } else {
		--cst->sharecount;
		++cst->modifycount;
		if (ccms_enable >= 9) {
			kprintf("CST MODXL %d %lld-%lld\n",
				cst->sharecount,
				(long long)cst->beg_offset,
				(long long)cst->end_offset);
		}
	    }
	    break;
	}
    }
    return(0);
}

/*
 * Release a CCMS lock
 *
 * MPSAFE
 */
int
ccms_lock_put(ccms_dataspace_t ds, ccms_lock_t lock)
{
    struct ccms_lock_scan_info info;
    ccms_cst_t cst;

    if (lock->ds == NULL)
	return(0);

    lock->ds = NULL;
    info.lock = lock;
    info.ds = ds;
    info.cst1 = NULL;
    info.cst2 = NULL;

    spin_lock(&ds->spin);
    RB_SCAN(ccms_rb_tree, &ds->tree, ccms_lock_scan_cmp,
	    ccms_lock_put_match, &info);
    cst = ds->delayed_free;
    ds->delayed_free = NULL;
    spin_unlock(&ds->spin);

    ccms_delayed_free(cst);
    if (info.cst1)
	objcache_put(ccms_oc, info.cst1);
    if (info.cst2)
	objcache_put(ccms_oc, info.cst2);
    return(0);
}

/*
 * NOTE: called with spinlock held.
 */
static
int
ccms_lock_put_match(ccms_cst_t cst, void *arg)
{
    struct ccms_lock_scan_info *info = arg;
    ccms_lock_t lock = info->lock;
    ccms_cst_t ocst;

    /*
     * Undo the local shared/exclusive rangelock.
     */
    switch(lock->ltype) {
    case CCMS_LTYPE_SHARED:
	KKASSERT(cst->sharecount > 0);
	--cst->sharecount;
	if (ccms_enable >= 9) {
		kprintf("CST UNSHR %d %lld-%lld (%d)\n", cst->sharecount,
			(long long)cst->beg_offset,
			(long long)cst->end_offset,
			cst->blocked);
	}
	if (cst->blocked && cst->sharecount == 0) {
		cst->blocked = 0;
		wakeup(cst);
	}
	break;
    case CCMS_LTYPE_EXCLUSIVE:
	KKASSERT(cst->sharecount < 0);
	++cst->sharecount;
	if (ccms_enable >= 9) {
		kprintf("CST UNEXC %d %lld-%lld (%d)\n", cst->sharecount,
			(long long)cst->beg_offset,
			(long long)cst->end_offset,
			cst->blocked);
	}
	if (cst->blocked && cst->sharecount == 0) {
		cst->blocked = 0;
		wakeup(cst);
	}
	break;
    case CCMS_LTYPE_MODIFYING:
	KKASSERT(cst->sharecount < 0 && cst->modifycount > 0);
	++cst->sharecount;
	--cst->modifycount;
	if (ccms_enable >= 9) {
		kprintf("CST UNMOD %d %lld-%lld (%d)\n", cst->sharecount,
			(long long)cst->beg_offset,
			(long long)cst->end_offset,
			cst->blocked);
	}
	if (cst->blocked && cst->sharecount == 0) {
		cst->blocked = 0;
		wakeup(cst);
	}
	break;
    }

    /*
     * Decrement the lock coverage count on the CST.  Decrement the left and
     * right edge counts as appropriate.
     *
     * When lrefs or rrefs drops to zero we check the adjacent entry to
     * determine whether a merge is possible.  If the appropriate refs field
     * (rrefs for the entry to our left, lrefs for the entry to our right)
     * is 0, then all covering locks must cover both entries and the xrefs
     * field must match.  We can then merge the entries if they have
     * compatible cache states. 
     *
     * However, because we are cleaning up the shared/exclusive count at
     * the same time, the sharecount field may be temporarily out of
     * sync, so require that the sharecount field also match before doing
     * a merge.
     *
     * When merging an element which is being blocked on, the blocking
     * thread(s) will be woken up.
     *
     * If the dataspace has too many CSTs we may be able to merge the
     * entries even if their cache states are not the same, by dropping
     * both to a compatible (lower) cache state and performing the appropriate
     * management operations.  XXX
     */
    --cst->xrefs;
    if (lock->beg_offset == cst->beg_offset) {
	--cst->lrefs;
	if (cst->lrefs == 0) {
	    if ((ocst = RB_PREV(ccms_rb_tree, &info->ds->tree, cst)) != NULL &&
		ocst->rrefs == 0 &&
		ocst->state == cst->state &&
		ocst->sharecount == cst->sharecount
	    ) {
		KKASSERT(ocst->xrefs == cst->xrefs);
		KKASSERT(ocst->end_offset + 1 == cst->beg_offset);
		RB_REMOVE(ccms_rb_tree, &info->ds->tree, ocst);
		cst->beg_offset = ocst->beg_offset;
		cst->lrefs = ocst->lrefs;
		if (ccms_enable >= 9) {
		    kprintf("MERGELEFT %p %lld-%lld (%d)\n", 
			   ocst,
			   (long long)cst->beg_offset,
			   (long long)cst->end_offset,
			   cst->blocked);
		}
		if (ocst->blocked) {
		    ocst->blocked = 0;
		    wakeup(ocst);
		}
		/*objcache_put(ccms_oc, ocst);*/
		ocst->delayed_next = info->ds->delayed_free;
		info->ds->delayed_free = ocst;
	    }
	}
    }
    if (lock->end_offset == cst->end_offset) {
	--cst->rrefs;
	if (cst->rrefs == 0) {
	    if ((ocst = RB_NEXT(ccms_rb_tree, &info->ds->tree, cst)) != NULL &&
		ocst->lrefs == 0 &&
		ocst->state == cst->state &&
		ocst->sharecount == cst->sharecount
	    ) {
		KKASSERT(ocst->xrefs == cst->xrefs);
		KKASSERT(cst->end_offset + 1 == ocst->beg_offset);
		RB_REMOVE(ccms_rb_tree, &info->ds->tree, ocst);
		cst->end_offset = ocst->end_offset;
		cst->rrefs = ocst->rrefs;
		if (ccms_enable >= 9) {
		    kprintf("MERGERIGHT %p %lld-%lld\n",
			   ocst,
			   (long long)cst->beg_offset,
			   (long long)cst->end_offset);
		}
		/*objcache_put(ccms_oc, ocst);*/
		ocst->delayed_next = info->ds->delayed_free;
		info->ds->delayed_free = ocst;
	    }
	}
    }
    return(0);
}

/*
 * RB tree compare function for insertions and deletions.  This function
 * compares two CSTs.
 */
static int
ccms_cst_cmp(ccms_cst_t b1, ccms_cst_t b2)
{
    if (b1->end_offset < b2->beg_offset)
	return(-1);
    if (b1->beg_offset > b2->end_offset)
	return(1);
    return(0);
}

/*
 * RB tree scanning compare function.  This function compares the CST
 * from the tree against the supplied ccms_lock and returns the CST's
 * placement relative to the lock.
 */
static int
ccms_lock_scan_cmp(ccms_cst_t cst, void *arg)
{
    struct ccms_lock_scan_info *info = arg;
    ccms_lock_t lock = info->lock;

    if (cst->end_offset < lock->beg_offset)
	return(-1);
    if (cst->beg_offset > lock->end_offset)
	return(1);
    return(0);
}

/*
 * This function works like ccms_lock_scan_cmp but terminates at the
 * collision point rather then at the lock's ending offset.  Only
 * the CSTs that were already partially resolved are returned by the scan.
 */
static int
ccms_lock_undo_cmp(ccms_cst_t cst, void *arg)
{
    struct ccms_lock_scan_info *info = arg;
    ccms_lock_t lock = info->lock;

    if (cst->end_offset < lock->beg_offset)
	return(-1);
    if (cst->beg_offset >= info->coll_cst->beg_offset)
	return(1);
    return(0);
}

