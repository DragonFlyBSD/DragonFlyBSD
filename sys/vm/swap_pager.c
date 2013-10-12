/*
 * (MPSAFE)
 *
 * Copyright (c) 1998-2010 The DragonFly Project.  All rights reserved.
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
 * Copyright (c) 1994 John S. Dyson
 * Copyright (c) 1990 University of Utah.
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * the Systems Programming Group of the University of Utah Computer
 * Science Department.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
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
 *				New Swap System
 *				Matthew Dillon
 *
 * Radix Bitmap 'blists'.
 *
 *	- The new swapper uses the new radix bitmap code.  This should scale
 *	  to arbitrarily small or arbitrarily large swap spaces and an almost
 *	  arbitrary degree of fragmentation.
 *
 * Features:
 *
 *	- on the fly reallocation of swap during putpages.  The new system
 *	  does not try to keep previously allocated swap blocks for dirty
 *	  pages.  
 *
 *	- on the fly deallocation of swap
 *
 *	- No more garbage collection required.  Unnecessarily allocated swap
 *	  blocks only exist for dirty vm_page_t's now and these are already
 *	  cycled (in a high-load system) by the pager.  We also do on-the-fly
 *	  removal of invalidated swap blocks when a page is destroyed
 *	  or renamed.
 *
 * from: Utah $Hdr: swap_pager.c 1.4 91/04/30$
 * @(#)swap_pager.c	8.9 (Berkeley) 3/21/94
 * $FreeBSD: src/sys/vm/swap_pager.c,v 1.130.2.12 2002/08/31 21:15:55 dillon Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/buf.h>
#include <sys/vnode.h>
#include <sys/malloc.h>
#include <sys/vmmeter.h>
#include <sys/sysctl.h>
#include <sys/blist.h>
#include <sys/lock.h>
#include <sys/thread2.h>

#include "opt_swap.h"
#include <vm/vm.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <vm/vm_pager.h>
#include <vm/vm_pageout.h>
#include <vm/swap_pager.h>
#include <vm/vm_extern.h>
#include <vm/vm_zone.h>
#include <vm/vnode_pager.h>

#include <sys/buf2.h>
#include <vm/vm_page2.h>

#ifndef MAX_PAGEOUT_CLUSTER
#define MAX_PAGEOUT_CLUSTER	SWB_NPAGES
#endif

#define SWM_FREE	0x02	/* free, period			*/
#define SWM_POP		0x04	/* pop out			*/

#define SWBIO_READ	0x01
#define SWBIO_WRITE	0x02
#define SWBIO_SYNC	0x04

struct swfreeinfo {
	vm_object_t	object;
	vm_pindex_t	basei;
	vm_pindex_t	begi;
	vm_pindex_t	endi;	/* inclusive */
};

struct swswapoffinfo {
	vm_object_t	object;
	int		devidx;
	int		shared;
};

/*
 * vm_swap_size is in page-sized chunks now.  It was DEV_BSIZE'd chunks
 * in the old system.
 */

int swap_pager_full;		/* swap space exhaustion (task killing) */
int vm_swap_cache_use;
int vm_swap_anon_use;
static int vm_report_swap_allocs;

static int swap_pager_almost_full; /* swap space exhaustion (w/ hysteresis)*/
static int nsw_rcount;		/* free read buffers			*/
static int nsw_wcount_sync;	/* limit write buffers / synchronous	*/
static int nsw_wcount_async;	/* limit write buffers / asynchronous	*/
static int nsw_wcount_async_max;/* assigned maximum			*/
static int nsw_cluster_max;	/* maximum VOP I/O allowed		*/

struct blist *swapblist;
static int swap_async_max = 4;	/* maximum in-progress async I/O's	*/
static int swap_burst_read = 0;	/* allow burst reading */
static swblk_t swapiterator;	/* linearize allocations */

/* from vm_swap.c */
extern struct vnode *swapdev_vp;
extern struct swdevt *swdevt;
extern int nswdev;

#define BLK2DEVIDX(blk) (nswdev > 1 ? blk / dmmax % nswdev : 0)

SYSCTL_INT(_vm, OID_AUTO, swap_async_max,
        CTLFLAG_RW, &swap_async_max, 0, "Maximum running async swap ops");
SYSCTL_INT(_vm, OID_AUTO, swap_burst_read,
        CTLFLAG_RW, &swap_burst_read, 0, "Allow burst reads for pageins");

SYSCTL_INT(_vm, OID_AUTO, swap_cache_use,
        CTLFLAG_RD, &vm_swap_cache_use, 0, "");
SYSCTL_INT(_vm, OID_AUTO, swap_anon_use,
        CTLFLAG_RD, &vm_swap_anon_use, 0, "");
SYSCTL_INT(_vm, OID_AUTO, swap_size,
        CTLFLAG_RD, &vm_swap_size, 0, "");
SYSCTL_INT(_vm, OID_AUTO, report_swap_allocs,
        CTLFLAG_RW, &vm_report_swap_allocs, 0, "");

vm_zone_t		swap_zone;

/*
 * Red-Black tree for swblock entries
 *
 * The caller must hold vm_token
 */
RB_GENERATE2(swblock_rb_tree, swblock, swb_entry, rb_swblock_compare,
	     vm_pindex_t, swb_index);

int
rb_swblock_compare(struct swblock *swb1, struct swblock *swb2)
{
	if (swb1->swb_index < swb2->swb_index)
		return(-1);
	if (swb1->swb_index > swb2->swb_index)
		return(1);
	return(0);
}

static
int
rb_swblock_scancmp(struct swblock *swb, void *data)
{
	struct swfreeinfo *info = data;

	if (swb->swb_index < info->basei)
		return(-1);
	if (swb->swb_index > info->endi)
		return(1);
	return(0);
}

static
int
rb_swblock_condcmp(struct swblock *swb, void *data)
{
	struct swfreeinfo *info = data;

	if (swb->swb_index < info->basei)
		return(-1);
	return(0);
}

/*
 * pagerops for OBJT_SWAP - "swap pager".  Some ops are also global procedure
 * calls hooked from other parts of the VM system and do not appear here.
 * (see vm/swap_pager.h).
 */

static void	swap_pager_dealloc (vm_object_t object);
static int	swap_pager_getpage (vm_object_t, vm_page_t *, int);
static void	swap_chain_iodone(struct bio *biox);

struct pagerops swappagerops = {
	swap_pager_dealloc,	/* deallocate an OBJT_SWAP object	*/
	swap_pager_getpage,	/* pagein				*/
	swap_pager_putpages,	/* pageout				*/
	swap_pager_haspage	/* get backing store status for page	*/
};

/*
 * dmmax is in page-sized chunks with the new swap system.  It was
 * dev-bsized chunks in the old.  dmmax is always a power of 2.
 *
 * swap_*() routines are externally accessible.  swp_*() routines are
 * internal.
 */

int dmmax;
static int dmmax_mask;
int nswap_lowat = 128;		/* in pages, swap_pager_almost_full warn */
int nswap_hiwat = 512;		/* in pages, swap_pager_almost_full warn */

static __inline void	swp_sizecheck (void);
static void	swp_pager_async_iodone (struct bio *bio);

/*
 * Swap bitmap functions
 */

static __inline void	swp_pager_freeswapspace(vm_object_t object,
						swblk_t blk, int npages);
static __inline swblk_t	swp_pager_getswapspace(vm_object_t object, int npages);

/*
 * Metadata functions
 */

static void swp_pager_meta_convert(vm_object_t);
static void swp_pager_meta_build(vm_object_t, vm_pindex_t, swblk_t);
static void swp_pager_meta_free(vm_object_t, vm_pindex_t, vm_pindex_t);
static void swp_pager_meta_free_all(vm_object_t);
static swblk_t swp_pager_meta_ctl(vm_object_t, vm_pindex_t, int);

/*
 * SWP_SIZECHECK() -	update swap_pager_full indication
 *	
 *	update the swap_pager_almost_full indication and warn when we are
 *	about to run out of swap space, using lowat/hiwat hysteresis.
 *
 *	Clear swap_pager_full ( task killing ) indication when lowat is met.
 *
 * No restrictions on call
 * This routine may not block.
 * SMP races are ok.
 */
static __inline void
swp_sizecheck(void)
{
	if (vm_swap_size < nswap_lowat) {
		if (swap_pager_almost_full == 0) {
			kprintf("swap_pager: out of swap space\n");
			swap_pager_almost_full = 1;
		}
	} else {
		swap_pager_full = 0;
		if (vm_swap_size > nswap_hiwat)
			swap_pager_almost_full = 0;
	}
}

/*
 * SWAP_PAGER_INIT() -	initialize the swap pager!
 *
 *	Expected to be started from system init.  NOTE:  This code is run 
 *	before much else so be careful what you depend on.  Most of the VM
 *	system has yet to be initialized at this point.
 *
 * Called from the low level boot code only.
 */
static void
swap_pager_init(void *arg __unused)
{
	/*
	 * Device Stripe, in PAGE_SIZE'd blocks
	 */
	dmmax = SWB_NPAGES * 2;
	dmmax_mask = ~(dmmax - 1);
}
SYSINIT(vm_mem, SI_BOOT1_VM, SI_ORDER_THIRD, swap_pager_init, NULL)

/*
 * SWAP_PAGER_SWAP_INIT() - swap pager initialization from pageout process
 *
 *	Expected to be started from pageout process once, prior to entering
 *	its main loop.
 *
 * Called from the low level boot code only.
 */
void
swap_pager_swap_init(void)
{
	int n, n2;

	/*
	 * Number of in-transit swap bp operations.  Don't
	 * exhaust the pbufs completely.  Make sure we
	 * initialize workable values (0 will work for hysteresis
	 * but it isn't very efficient).
	 *
	 * The nsw_cluster_max is constrained by the number of pages an XIO
	 * holds, i.e., (MAXPHYS/PAGE_SIZE) and our locally defined
	 * MAX_PAGEOUT_CLUSTER.   Also be aware that swap ops are
	 * constrained by the swap device interleave stripe size.
	 *
	 * Currently we hardwire nsw_wcount_async to 4.  This limit is 
	 * designed to prevent other I/O from having high latencies due to
	 * our pageout I/O.  The value 4 works well for one or two active swap
	 * devices but is probably a little low if you have more.  Even so,
	 * a higher value would probably generate only a limited improvement
	 * with three or four active swap devices since the system does not
	 * typically have to pageout at extreme bandwidths.   We will want
	 * at least 2 per swap devices, and 4 is a pretty good value if you
	 * have one NFS swap device due to the command/ack latency over NFS.
	 * So it all works out pretty well.
	 */

	nsw_cluster_max = min((MAXPHYS/PAGE_SIZE), MAX_PAGEOUT_CLUSTER);

	nsw_rcount = (nswbuf + 1) / 2;
	nsw_wcount_sync = (nswbuf + 3) / 4;
	nsw_wcount_async = 4;
	nsw_wcount_async_max = nsw_wcount_async;

	/*
	 * The zone is dynamically allocated so generally size it to
	 * maxswzone (32MB to 512MB of KVM).  Set a minimum size based
	 * on physical memory of around 8x (each swblock can hold 16 pages).
	 *
	 * With the advent of SSDs (vs HDs) the practical (swap:memory) ratio
	 * has increased dramatically.
	 */
	n = vmstats.v_page_count / 2;
	if (maxswzone && n < maxswzone / sizeof(struct swblock))
		n = maxswzone / sizeof(struct swblock);
	n2 = n;

	do {
		swap_zone = zinit(
			"SWAPMETA", 
			sizeof(struct swblock), 
			n,
			ZONE_INTERRUPT, 
			1);
		if (swap_zone != NULL)
			break;
		/*
		 * if the allocation failed, try a zone two thirds the
		 * size of the previous attempt.
		 */
		n -= ((n + 2) / 3);
	} while (n > 0);

	if (swap_zone == NULL)
		panic("swap_pager_swap_init: swap_zone == NULL");
	if (n2 != n)
		kprintf("Swap zone entries reduced from %d to %d.\n", n2, n);
}

/*
 * SWAP_PAGER_ALLOC() -	allocate a new OBJT_SWAP VM object and instantiate
 *			its metadata structures.
 *
 *	This routine is called from the mmap and fork code to create a new
 *	OBJT_SWAP object.  We do this by creating an OBJT_DEFAULT object
 *	and then converting it with swp_pager_meta_convert().
 *
 *	We only support unnamed objects.
 *
 * No restrictions.
 */
vm_object_t
swap_pager_alloc(void *handle, off_t size, vm_prot_t prot, off_t offset)
{
	vm_object_t object;

	KKASSERT(handle == NULL);
	object = vm_object_allocate_hold(OBJT_DEFAULT,
					 OFF_TO_IDX(offset + PAGE_MASK + size));
	swp_pager_meta_convert(object);
	vm_object_drop(object);

	return (object);
}

/*
 * SWAP_PAGER_DEALLOC() -	remove swap metadata from object
 *
 *	The swap backing for the object is destroyed.  The code is 
 *	designed such that we can reinstantiate it later, but this
 *	routine is typically called only when the entire object is
 *	about to be destroyed.
 *
 * The object must be locked or unreferenceable.
 * No other requirements.
 */
static void
swap_pager_dealloc(vm_object_t object)
{
	vm_object_hold(object);
	vm_object_pip_wait(object, "swpdea");

	/*
	 * Free all remaining metadata.  We only bother to free it from 
	 * the swap meta data.  We do not attempt to free swapblk's still
	 * associated with vm_page_t's for this object.  We do not care
	 * if paging is still in progress on some objects.
	 */
	swp_pager_meta_free_all(object);
	vm_object_drop(object);
}

/************************************************************************
 *			SWAP PAGER BITMAP ROUTINES			*
 ************************************************************************/

/*
 * SWP_PAGER_GETSWAPSPACE() -	allocate raw swap space
 *
 *	Allocate swap for the requested number of pages.  The starting
 *	swap block number (a page index) is returned or SWAPBLK_NONE
 *	if the allocation failed.
 *
 *	Also has the side effect of advising that somebody made a mistake
 *	when they configured swap and didn't configure enough.
 *
 * The caller must hold the object.
 * This routine may not block.
 */
static __inline swblk_t
swp_pager_getswapspace(vm_object_t object, int npages)
{
	swblk_t blk;

	lwkt_gettoken(&vm_token);
	blk = blist_allocat(swapblist, npages, swapiterator);
	if (blk == SWAPBLK_NONE)
		blk = blist_allocat(swapblist, npages, 0);
	if (blk == SWAPBLK_NONE) {
		if (swap_pager_full != 2) {
			kprintf("swap_pager_getswapspace: failed alloc=%d\n",
				npages);
			swap_pager_full = 2;
			swap_pager_almost_full = 1;
		}
	} else {
		/* swapiterator = blk; disable for now, doesn't work well */
		swapacctspace(blk, -npages);
		if (object->type == OBJT_SWAP)
			vm_swap_anon_use += npages;
		else
			vm_swap_cache_use += npages;
		swp_sizecheck();
	}
	lwkt_reltoken(&vm_token);
	return(blk);
}

/*
 * SWP_PAGER_FREESWAPSPACE() -	free raw swap space 
 *
 *	This routine returns the specified swap blocks back to the bitmap.
 *
 *	Note:  This routine may not block (it could in the old swap code),
 *	and through the use of the new blist routines it does not block.
 *
 *	We must be called at splvm() to avoid races with bitmap frees from
 *	vm_page_remove() aka swap_pager_page_removed().
 *
 * This routine may not block.
 */

static __inline void
swp_pager_freeswapspace(vm_object_t object, swblk_t blk, int npages)
{
	struct swdevt *sp = &swdevt[BLK2DEVIDX(blk)];

	lwkt_gettoken(&vm_token);
	sp->sw_nused -= npages;
	if (object->type == OBJT_SWAP)
		vm_swap_anon_use -= npages;
	else
		vm_swap_cache_use -= npages;

	if (sp->sw_flags & SW_CLOSING) {
		lwkt_reltoken(&vm_token);
		return;
	}

	blist_free(swapblist, blk, npages);
	vm_swap_size += npages;
	swp_sizecheck();
	lwkt_reltoken(&vm_token);
}

/*
 * SWAP_PAGER_FREESPACE() -	frees swap blocks associated with a page
 *				range within an object.
 *
 *	This is a globally accessible routine.
 *
 *	This routine removes swapblk assignments from swap metadata.
 *
 *	The external callers of this routine typically have already destroyed 
 *	or renamed vm_page_t's associated with this range in the object so 
 *	we should be ok.
 *
 * No requirements.
 */
void
swap_pager_freespace(vm_object_t object, vm_pindex_t start, vm_pindex_t size)
{
	vm_object_hold(object);
	swp_pager_meta_free(object, start, size);
	vm_object_drop(object);
}

/*
 * No requirements.
 */
void
swap_pager_freespace_all(vm_object_t object)
{
	vm_object_hold(object);
	swp_pager_meta_free_all(object);
	vm_object_drop(object);
}

/*
 * This function conditionally frees swap cache swap starting at
 * (*basei) in the object.  (count) swap blocks will be nominally freed.
 * The actual number of blocks freed can be more or less than the
 * requested number.
 *
 * This function nominally returns the number of blocks freed.  However,
 * the actual number of blocks freed may be less then the returned value.
 * If the function is unable to exhaust the object or if it is able to
 * free (approximately) the requested number of blocks it returns
 * a value n > count.
 *
 * If we exhaust the object we will return a value n <= count.
 *
 * The caller must hold the object.
 *
 * WARNING!  If count == 0 then -1 can be returned as a degenerate case,
 *	     callers should always pass a count value > 0.
 */
static int swap_pager_condfree_callback(struct swblock *swap, void *data);

int
swap_pager_condfree(vm_object_t object, vm_pindex_t *basei, int count)
{
	struct swfreeinfo info;
	int n;
	int t;

	ASSERT_LWKT_TOKEN_HELD(vm_object_token(object));

	info.object = object;
	info.basei = *basei;	/* skip up to this page index */
	info.begi = count;	/* max swap pages to destroy */
	info.endi = count * 8;	/* max swblocks to scan */

	swblock_rb_tree_RB_SCAN(&object->swblock_root, rb_swblock_condcmp,
				swap_pager_condfree_callback, &info);
	*basei = info.basei;

	/*
	 * Take the higher difference swblocks vs pages
	 */
	n = count - (int)info.begi;
	t = count * 8 - (int)info.endi;
	if (n < t)
		n = t;
	if (n < 1)
		n = 1;
	return(n);
}

/*
 * The idea is to free whole meta-block to avoid fragmenting
 * the swap space or disk I/O.  We only do this if NO VM pages
 * are present.
 *
 * We do not have to deal with clearing PG_SWAPPED in related VM
 * pages because there are no related VM pages.
 *
 * The caller must hold the object.
 */
static int
swap_pager_condfree_callback(struct swblock *swap, void *data)
{
	struct swfreeinfo *info = data;
	vm_object_t object = info->object;
	int i;

	for (i = 0; i < SWAP_META_PAGES; ++i) {
		if (vm_page_lookup(object, swap->swb_index + i))
			break;
	}
	info->basei = swap->swb_index + SWAP_META_PAGES;
	if (i == SWAP_META_PAGES) {
		info->begi -= swap->swb_count;
		swap_pager_freespace(object, swap->swb_index, SWAP_META_PAGES);
	}
	--info->endi;
	if ((int)info->begi < 0 || (int)info->endi < 0)
		return(-1);
	lwkt_yield();
	return(0);
}

/*
 * Called by vm_page_alloc() when a new VM page is inserted
 * into a VM object.  Checks whether swap has been assigned to
 * the page and sets PG_SWAPPED as necessary.
 *
 * No requirements.
 */
void
swap_pager_page_inserted(vm_page_t m)
{
	if (m->object->swblock_count) {
		vm_object_hold(m->object);
		if (swp_pager_meta_ctl(m->object, m->pindex, 0) != SWAPBLK_NONE)
			vm_page_flag_set(m, PG_SWAPPED);
		vm_object_drop(m->object);
	}
}

/*
 * SWAP_PAGER_RESERVE() - reserve swap blocks in object
 *
 *	Assigns swap blocks to the specified range within the object.  The 
 *	swap blocks are not zerod.  Any previous swap assignment is destroyed.
 *
 *	Returns 0 on success, -1 on failure.
 *
 * The caller is responsible for avoiding races in the specified range.
 * No other requirements.
 */
int
swap_pager_reserve(vm_object_t object, vm_pindex_t start, vm_size_t size)
{
	int n = 0;
	swblk_t blk = SWAPBLK_NONE;
	vm_pindex_t beg = start;	/* save start index */

	vm_object_hold(object);

	while (size) {
		if (n == 0) {
			n = BLIST_MAX_ALLOC;
			while ((blk = swp_pager_getswapspace(object, n)) ==
			       SWAPBLK_NONE)
			{
				n >>= 1;
				if (n == 0) {
					swp_pager_meta_free(object, beg,
							    start - beg);
					vm_object_drop(object);
					return(-1);
				}
			}
		}
		swp_pager_meta_build(object, start, blk);
		--size;
		++start;
		++blk;
		--n;
	}
	swp_pager_meta_free(object, start, n);
	vm_object_drop(object);
	return(0);
}

/*
 * SWAP_PAGER_COPY() -  copy blocks from source pager to destination pager
 *			and destroy the source.
 *
 *	Copy any valid swapblks from the source to the destination.  In
 *	cases where both the source and destination have a valid swapblk,
 *	we keep the destination's.
 *
 *	This routine is allowed to block.  It may block allocating metadata
 *	indirectly through swp_pager_meta_build() or if paging is still in
 *	progress on the source. 
 *
 *	XXX vm_page_collapse() kinda expects us not to block because we 
 *	supposedly do not need to allocate memory, but for the moment we
 *	*may* have to get a little memory from the zone allocator, but
 *	it is taken from the interrupt memory.  We should be ok. 
 *
 *	The source object contains no vm_page_t's (which is just as well)
 *	The source object is of type OBJT_SWAP.
 *
 *	The source and destination objects must be held by the caller.
 */
void
swap_pager_copy(vm_object_t srcobject, vm_object_t dstobject,
		vm_pindex_t base_index, int destroysource)
{
	vm_pindex_t i;

	ASSERT_LWKT_TOKEN_HELD(vm_object_token(srcobject));
	ASSERT_LWKT_TOKEN_HELD(vm_object_token(dstobject));

	/*
	 * transfer source to destination.
	 */
	for (i = 0; i < dstobject->size; ++i) {
		swblk_t dstaddr;

		/*
		 * Locate (without changing) the swapblk on the destination,
		 * unless it is invalid in which case free it silently, or
		 * if the destination is a resident page, in which case the
		 * source is thrown away.
		 */
		dstaddr = swp_pager_meta_ctl(dstobject, i, 0);

		if (dstaddr == SWAPBLK_NONE) {
			/*
			 * Destination has no swapblk and is not resident,
			 * copy source.
			 */
			swblk_t srcaddr;

			srcaddr = swp_pager_meta_ctl(srcobject,
						     base_index + i, SWM_POP);

			if (srcaddr != SWAPBLK_NONE)
				swp_pager_meta_build(dstobject, i, srcaddr);
		} else {
			/*
			 * Destination has valid swapblk or it is represented
			 * by a resident page.  We destroy the sourceblock.
			 */
			swp_pager_meta_ctl(srcobject, base_index + i, SWM_FREE);
		}
	}

	/*
	 * Free left over swap blocks in source.
	 *
	 * We have to revert the type to OBJT_DEFAULT so we do not accidently
	 * double-remove the object from the swap queues.
	 */
	if (destroysource) {
		/*
		 * Reverting the type is not necessary, the caller is going
		 * to destroy srcobject directly, but I'm doing it here
		 * for consistency since we've removed the object from its
		 * queues.
		 */
		swp_pager_meta_free_all(srcobject);
		if (srcobject->type == OBJT_SWAP)
			srcobject->type = OBJT_DEFAULT;
	}
}

/*
 * SWAP_PAGER_HASPAGE() -	determine if we have good backing store for
 *				the requested page.
 *
 *	We determine whether good backing store exists for the requested
 *	page and return TRUE if it does, FALSE if it doesn't.
 *
 *	If TRUE, we also try to determine how much valid, contiguous backing
 *	store exists before and after the requested page within a reasonable
 *	distance.  We do not try to restrict it to the swap device stripe
 *	(that is handled in getpages/putpages).  It probably isn't worth
 *	doing here.
 *
 * No requirements.
 */
boolean_t
swap_pager_haspage(vm_object_t object, vm_pindex_t pindex)
{
	swblk_t blk0;

	/*
	 * do we have good backing store at the requested index ?
	 */
	vm_object_hold(object);
	blk0 = swp_pager_meta_ctl(object, pindex, 0);

	if (blk0 == SWAPBLK_NONE) {
		vm_object_drop(object);
		return (FALSE);
	}
	vm_object_drop(object);
	return (TRUE);
}

/*
 * SWAP_PAGER_PAGE_UNSWAPPED() - remove swap backing store related to page
 *
 * This removes any associated swap backing store, whether valid or
 * not, from the page.  This operates on any VM object, not just OBJT_SWAP
 * objects.
 *
 * This routine is typically called when a page is made dirty, at
 * which point any associated swap can be freed.  MADV_FREE also
 * calls us in a special-case situation
 *
 * NOTE!!!  If the page is clean and the swap was valid, the caller
 * should make the page dirty before calling this routine.  This routine
 * does NOT change the m->dirty status of the page.  Also: MADV_FREE
 * depends on it.
 *
 * The page must be busied or soft-busied.
 * The caller can hold the object to avoid blocking, else we might block.
 * No other requirements.
 */
void
swap_pager_unswapped(vm_page_t m)
{
	if (m->flags & PG_SWAPPED) {
		vm_object_hold(m->object);
		KKASSERT(m->flags & PG_SWAPPED);
		swp_pager_meta_ctl(m->object, m->pindex, SWM_FREE);
		vm_page_flag_clear(m, PG_SWAPPED);
		vm_object_drop(m->object);
	}
}

/*
 * SWAP_PAGER_STRATEGY() - read, write, free blocks
 *
 * This implements a VM OBJECT strategy function using swap backing store.
 * This can operate on any VM OBJECT type, not necessarily just OBJT_SWAP
 * types.
 *
 * This is intended to be a cacheless interface (i.e. caching occurs at
 * higher levels), and is also used as a swap-based SSD cache for vnode
 * and device objects.
 *
 * All I/O goes directly to and from the swap device.
 *	
 * We currently attempt to run I/O synchronously or asynchronously as
 * the caller requests.  This isn't perfect because we loose error
 * sequencing when we run multiple ops in parallel to satisfy a request.
 * But this is swap, so we let it all hang out.
 *
 * No requirements.
 */
void
swap_pager_strategy(vm_object_t object, struct bio *bio)
{
	struct buf *bp = bio->bio_buf;
	struct bio *nbio;
	vm_pindex_t start;
	vm_pindex_t biox_blkno = 0;
	int count;
	char *data;
	struct bio *biox;
	struct buf *bufx;
#if 0
	struct bio_track *track;
#endif

#if 0
	/*
	 * tracking for swapdev vnode I/Os
	 */
	if (bp->b_cmd == BUF_CMD_READ)
		track = &swapdev_vp->v_track_read;
	else
		track = &swapdev_vp->v_track_write;
#endif

	if (bp->b_bcount & PAGE_MASK) {
		bp->b_error = EINVAL;
		bp->b_flags |= B_ERROR | B_INVAL;
		biodone(bio);
		kprintf("swap_pager_strategy: bp %p offset %lld size %d, "
			"not page bounded\n",
			bp, (long long)bio->bio_offset, (int)bp->b_bcount);
		return;
	}

	/*
	 * Clear error indication, initialize page index, count, data pointer.
	 */
	bp->b_error = 0;
	bp->b_flags &= ~B_ERROR;
	bp->b_resid = bp->b_bcount;

	start = (vm_pindex_t)(bio->bio_offset >> PAGE_SHIFT);
	count = howmany(bp->b_bcount, PAGE_SIZE);
	data = bp->b_data;

	/*
	 * Deal with BUF_CMD_FREEBLKS
	 */
	if (bp->b_cmd == BUF_CMD_FREEBLKS) {
		/*
		 * FREE PAGE(s) - destroy underlying swap that is no longer
		 *		  needed.
		 */
		vm_object_hold(object);
		swp_pager_meta_free(object, start, count);
		vm_object_drop(object);
		bp->b_resid = 0;
		biodone(bio);
		return;
	}

	/*
	 * We need to be able to create a new cluster of I/O's.  We cannot
	 * use the caller fields of the passed bio so push a new one.
	 *
	 * Because nbio is just a placeholder for the cluster links,
	 * we can biodone() the original bio instead of nbio to make
	 * things a bit more efficient.
	 */
	nbio = push_bio(bio);
	nbio->bio_offset = bio->bio_offset;
	nbio->bio_caller_info1.cluster_head = NULL;
	nbio->bio_caller_info2.cluster_tail = NULL;

	biox = NULL;
	bufx = NULL;

	/*
	 * Execute read or write
	 */
	vm_object_hold(object);

	while (count > 0) {
		swblk_t blk;

		/*
		 * Obtain block.  If block not found and writing, allocate a
		 * new block and build it into the object.
		 */
		blk = swp_pager_meta_ctl(object, start, 0);
		if ((blk == SWAPBLK_NONE) && bp->b_cmd != BUF_CMD_READ) {
			blk = swp_pager_getswapspace(object, 1);
			if (blk == SWAPBLK_NONE) {
				bp->b_error = ENOMEM;
				bp->b_flags |= B_ERROR;
				break;
			}
			swp_pager_meta_build(object, start, blk);
		}
			
		/*
		 * Do we have to flush our current collection?  Yes if:
		 *
		 *	- no swap block at this index
		 *	- swap block is not contiguous
		 *	- we cross a physical disk boundry in the
		 *	  stripe.
		 */
		if (
		    biox && (biox_blkno + btoc(bufx->b_bcount) != blk ||
		     ((biox_blkno ^ blk) & dmmax_mask)
		    )
		) {
			if (bp->b_cmd == BUF_CMD_READ) {
				++mycpu->gd_cnt.v_swapin;
				mycpu->gd_cnt.v_swappgsin += btoc(bufx->b_bcount);
			} else {
				++mycpu->gd_cnt.v_swapout;
				mycpu->gd_cnt.v_swappgsout += btoc(bufx->b_bcount);
				bufx->b_dirtyend = bufx->b_bcount;
			}

			/*
			 * Finished with this buf.
			 */
			KKASSERT(bufx->b_bcount != 0);
			if (bufx->b_cmd != BUF_CMD_READ)
				bufx->b_dirtyend = bufx->b_bcount;
			biox = NULL;
			bufx = NULL;
		}

		/*
		 * Add new swapblk to biox, instantiating biox if necessary.
		 * Zero-fill reads are able to take a shortcut.
		 */
		if (blk == SWAPBLK_NONE) {
			/*
			 * We can only get here if we are reading.  Since
			 * we are at splvm() we can safely modify b_resid,
			 * even if chain ops are in progress.
			 */
			bzero(data, PAGE_SIZE);
			bp->b_resid -= PAGE_SIZE;
		} else {
			if (biox == NULL) {
				/* XXX chain count > 4, wait to <= 4 */

				bufx = getpbuf(NULL);
				biox = &bufx->b_bio1;
				cluster_append(nbio, bufx);
				bufx->b_flags |= (bp->b_flags & B_ORDERED);
				bufx->b_cmd = bp->b_cmd;
				biox->bio_done = swap_chain_iodone;
				biox->bio_offset = (off_t)blk << PAGE_SHIFT;
				biox->bio_caller_info1.cluster_parent = nbio;
				biox_blkno = blk;
				bufx->b_bcount = 0;
				bufx->b_data = data;
			}
			bufx->b_bcount += PAGE_SIZE;
		}
		--count;
		++start;
		data += PAGE_SIZE;
	}

	vm_object_drop(object);

	/*
	 *  Flush out last buffer
	 */
	if (biox) {
		if (bufx->b_cmd == BUF_CMD_READ) {
			++mycpu->gd_cnt.v_swapin;
			mycpu->gd_cnt.v_swappgsin += btoc(bufx->b_bcount);
		} else {
			++mycpu->gd_cnt.v_swapout;
			mycpu->gd_cnt.v_swappgsout += btoc(bufx->b_bcount);
			bufx->b_dirtyend = bufx->b_bcount;
		}
		KKASSERT(bufx->b_bcount);
		if (bufx->b_cmd != BUF_CMD_READ)
			bufx->b_dirtyend = bufx->b_bcount;
		/* biox, bufx = NULL */
	}

	/*
	 * Now initiate all the I/O.  Be careful looping on our chain as
	 * I/O's may complete while we are still initiating them.
	 *
	 * If the request is a 100% sparse read no bios will be present
	 * and we just biodone() the buffer.
	 */
	nbio->bio_caller_info2.cluster_tail = NULL;
	bufx = nbio->bio_caller_info1.cluster_head;

	if (bufx) {
		while (bufx) {
			biox = &bufx->b_bio1;
			BUF_KERNPROC(bufx);
			bufx = bufx->b_cluster_next;
			vn_strategy(swapdev_vp, biox);
		}
	} else {
		biodone(bio);
	}

	/*
	 * Completion of the cluster will also call biodone_chain(nbio).
	 * We never call biodone(nbio) so we don't have to worry about
	 * setting up a bio_done callback.  It's handled in the sub-IO.
	 */
	/**/
}

/*
 * biodone callback
 *
 * No requirements.
 */
static void
swap_chain_iodone(struct bio *biox)
{
	struct buf **nextp;
	struct buf *bufx;	/* chained sub-buffer */
	struct bio *nbio;	/* parent nbio with chain glue */
	struct buf *bp;		/* original bp associated with nbio */
	int chain_empty;

	bufx = biox->bio_buf;
	nbio = biox->bio_caller_info1.cluster_parent;
	bp = nbio->bio_buf;

	/*
	 * Update the original buffer
	 */
        KKASSERT(bp != NULL);
	if (bufx->b_flags & B_ERROR) {
		atomic_set_int(&bufx->b_flags, B_ERROR);
		bp->b_error = bufx->b_error;	/* race ok */
	} else if (bufx->b_resid != 0) {
		atomic_set_int(&bufx->b_flags, B_ERROR);
		bp->b_error = EINVAL;		/* race ok */
	} else {
		atomic_subtract_int(&bp->b_resid, bufx->b_bcount);
	}

	/*
	 * Remove us from the chain.
	 */
	spin_lock(&bp->b_lock.lk_spinlock);
	nextp = &nbio->bio_caller_info1.cluster_head;
	while (*nextp != bufx) {
		KKASSERT(*nextp != NULL);
		nextp = &(*nextp)->b_cluster_next;
	}
	*nextp = bufx->b_cluster_next;
	chain_empty = (nbio->bio_caller_info1.cluster_head == NULL);
	spin_unlock(&bp->b_lock.lk_spinlock);

	/*
	 * Clean up bufx.  If the chain is now empty we finish out
	 * the parent.  Note that we may be racing other completions
	 * so we must use the chain_empty status from above.
	 */
	if (chain_empty) {
		if (bp->b_resid != 0 && !(bp->b_flags & B_ERROR)) {
			atomic_set_int(&bp->b_flags, B_ERROR);
			bp->b_error = EINVAL;
		}
		biodone_chain(nbio);
        }
        relpbuf(bufx, NULL);
}

/*
 * SWAP_PAGER_GETPAGES() - bring page in from swap
 *
 * The requested page may have to be brought in from swap.  Calculate the
 * swap block and bring in additional pages if possible.  All pages must
 * have contiguous swap block assignments and reside in the same object.
 *
 * The caller has a single vm_object_pip_add() reference prior to
 * calling us and we should return with the same.
 *
 * The caller has BUSY'd the page.  We should return with (*mpp) left busy,
 * and any additinal pages unbusied.
 *
 * If the caller encounters a PG_RAM page it will pass it to us even though
 * it may be valid and dirty.  We cannot overwrite the page in this case!
 * The case is used to allow us to issue pure read-aheads.
 *
 * NOTE! XXX This code does not entirely pipeline yet due to the fact that
 *       the PG_RAM page is validated at the same time as mreq.  What we
 *	 really need to do is issue a separate read-ahead pbuf.
 *
 * No requirements.
 */
static int
swap_pager_getpage(vm_object_t object, vm_page_t *mpp, int seqaccess)
{
	struct buf *bp;
	struct bio *bio;
	vm_page_t mreq;
	vm_page_t m;
	vm_offset_t kva;
	swblk_t blk;
	int i;
	int j;
	int raonly;
	int error;
	u_int32_t flags;
	vm_page_t marray[XIO_INTERNAL_PAGES];

	mreq = *mpp;

	vm_object_hold(object);
	if (mreq->object != object) {
		panic("swap_pager_getpages: object mismatch %p/%p", 
		    object, 
		    mreq->object
		);
	}

	/*
	 * We don't want to overwrite a fully valid page as it might be
	 * dirty.  This case can occur when e.g. vm_fault hits a perfectly
	 * valid page with PG_RAM set.
	 *
	 * In this case we see if the next page is a suitable page-in
	 * candidate and if it is we issue read-ahead.  PG_RAM will be
	 * set on the last page of the read-ahead to continue the pipeline.
	 */
	if (mreq->valid == VM_PAGE_BITS_ALL) {
		if (swap_burst_read == 0 || mreq->pindex + 1 >= object->size) {
			vm_object_drop(object);
			return(VM_PAGER_OK);
		}
		blk = swp_pager_meta_ctl(object, mreq->pindex + 1, 0);
		if (blk == SWAPBLK_NONE) {
			vm_object_drop(object);
			return(VM_PAGER_OK);
		}
		m = vm_page_lookup_busy_try(object, mreq->pindex + 1,
					    TRUE, &error);
		if (error) {
			vm_object_drop(object);
			return(VM_PAGER_OK);
		} else if (m == NULL) {
			/*
			 * Use VM_ALLOC_QUICK to avoid blocking on cache
			 * page reuse.
			 */
			m = vm_page_alloc(object, mreq->pindex + 1,
					  VM_ALLOC_QUICK);
			if (m == NULL) {
				vm_object_drop(object);
				return(VM_PAGER_OK);
			}
		} else {
			if (m->valid) {
				vm_page_wakeup(m);
				vm_object_drop(object);
				return(VM_PAGER_OK);
			}
			vm_page_unqueue_nowakeup(m);
		}
		/* page is busy */
		mreq = m;
		raonly = 1;
	} else {
		raonly = 0;
	}

	/*
	 * Try to block-read contiguous pages from swap if sequential,
	 * otherwise just read one page.  Contiguous pages from swap must
	 * reside within a single device stripe because the I/O cannot be
	 * broken up across multiple stripes.
	 *
	 * Note that blk and iblk can be SWAPBLK_NONE but the loop is
	 * set up such that the case(s) are handled implicitly.
	 */
	blk = swp_pager_meta_ctl(mreq->object, mreq->pindex, 0);
	marray[0] = mreq;

	for (i = 1; swap_burst_read &&
		    i < XIO_INTERNAL_PAGES &&
		    mreq->pindex + i < object->size; ++i) {
		swblk_t iblk;

		iblk = swp_pager_meta_ctl(object, mreq->pindex + i, 0);
		if (iblk != blk + i)
			break;
		if ((blk ^ iblk) & dmmax_mask)
			break;
		m = vm_page_lookup_busy_try(object, mreq->pindex + i,
					    TRUE, &error);
		if (error) {
			break;
		} else if (m == NULL) {
			/*
			 * Use VM_ALLOC_QUICK to avoid blocking on cache
			 * page reuse.
			 */
			m = vm_page_alloc(object, mreq->pindex + i,
					  VM_ALLOC_QUICK);
			if (m == NULL)
				break;
		} else {
			if (m->valid) {
				vm_page_wakeup(m);
				break;
			}
			vm_page_unqueue_nowakeup(m);
		}
		/* page is busy */
		marray[i] = m;
	}
	if (i > 1)
		vm_page_flag_set(marray[i - 1], PG_RAM);

	/*
	 * If mreq is the requested page and we have nothing to do return
	 * VM_PAGER_FAIL.  If raonly is set mreq is just another read-ahead
	 * page and must be cleaned up.
	 */
	if (blk == SWAPBLK_NONE) {
		KKASSERT(i == 1);
		if (raonly) {
			vnode_pager_freepage(mreq);
			vm_object_drop(object);
			return(VM_PAGER_OK);
		} else {
			vm_object_drop(object);
			return(VM_PAGER_FAIL);
		}
	}

	/*
	 * map our page(s) into kva for input
	 */
	bp = getpbuf_kva(&nsw_rcount);
	bio = &bp->b_bio1;
	kva = (vm_offset_t) bp->b_kvabase;
	bcopy(marray, bp->b_xio.xio_pages, i * sizeof(vm_page_t));
	pmap_qenter(kva, bp->b_xio.xio_pages, i);

	bp->b_data = (caddr_t)kva;
	bp->b_bcount = PAGE_SIZE * i;
	bp->b_xio.xio_npages = i;
	bio->bio_done = swp_pager_async_iodone;
	bio->bio_offset = (off_t)blk << PAGE_SHIFT;
	bio->bio_caller_info1.index = SWBIO_READ;

	/*
	 * Set index.  If raonly set the index beyond the array so all
	 * the pages are treated the same, otherwise the original mreq is
	 * at index 0.
	 */
	if (raonly)
		bio->bio_driver_info = (void *)(intptr_t)i;
	else
		bio->bio_driver_info = (void *)(intptr_t)0;

	for (j = 0; j < i; ++j)
		vm_page_flag_set(bp->b_xio.xio_pages[j], PG_SWAPINPROG);

	mycpu->gd_cnt.v_swapin++;
	mycpu->gd_cnt.v_swappgsin += bp->b_xio.xio_npages;

	/*
	 * We still hold the lock on mreq, and our automatic completion routine
	 * does not remove it.
	 */
	vm_object_pip_add(object, bp->b_xio.xio_npages);

	/*
	 * perform the I/O.  NOTE!!!  bp cannot be considered valid after
	 * this point because we automatically release it on completion.
	 * Instead, we look at the one page we are interested in which we
	 * still hold a lock on even through the I/O completion.
	 *
	 * The other pages in our m[] array are also released on completion,
	 * so we cannot assume they are valid anymore either.
	 */
	bp->b_cmd = BUF_CMD_READ;
	BUF_KERNPROC(bp);
	vn_strategy(swapdev_vp, bio);

	/*
	 * Wait for the page we want to complete.  PG_SWAPINPROG is always
	 * cleared on completion.  If an I/O error occurs, SWAPBLK_NONE
	 * is set in the meta-data.
	 *
	 * If this is a read-ahead only we return immediately without
	 * waiting for I/O.
	 */
	if (raonly) {
		vm_object_drop(object);
		return(VM_PAGER_OK);
	}

	/*
	 * Read-ahead includes originally requested page case.
	 */
	for (;;) {
		flags = mreq->flags;
		cpu_ccfence();
		if ((flags & PG_SWAPINPROG) == 0)
			break;
		tsleep_interlock(mreq, 0);
		if (!atomic_cmpset_int(&mreq->flags, flags,
				       flags | PG_WANTED | PG_REFERENCED)) {
			continue;
		}
		mycpu->gd_cnt.v_intrans++;
		if (tsleep(mreq, PINTERLOCKED, "swread", hz*20)) {
			kprintf(
			    "swap_pager: indefinite wait buffer: "
				" offset: %lld, size: %ld\n",
			    (long long)bio->bio_offset,
			    (long)bp->b_bcount
			);
		}
	}

	/*
	 * mreq is left bussied after completion, but all the other pages
	 * are freed.  If we had an unrecoverable read error the page will
	 * not be valid.
	 */
	vm_object_drop(object);
	if (mreq->valid != VM_PAGE_BITS_ALL)
		return(VM_PAGER_ERROR);
	else
		return(VM_PAGER_OK);

	/*
	 * A final note: in a low swap situation, we cannot deallocate swap
	 * and mark a page dirty here because the caller is likely to mark
	 * the page clean when we return, causing the page to possibly revert 
	 * to all-zero's later.
	 */
}

/*
 *	swap_pager_putpages: 
 *
 *	Assign swap (if necessary) and initiate I/O on the specified pages.
 *
 *	We support both OBJT_DEFAULT and OBJT_SWAP objects.  DEFAULT objects
 *	are automatically converted to SWAP objects.
 *
 *	In a low memory situation we may block in vn_strategy(), but the new 
 *	vm_page reservation system coupled with properly written VFS devices 
 *	should ensure that no low-memory deadlock occurs.  This is an area
 *	which needs work.
 *
 *	The parent has N vm_object_pip_add() references prior to
 *	calling us and will remove references for rtvals[] that are
 *	not set to VM_PAGER_PEND.  We need to remove the rest on I/O
 *	completion.
 *
 *	The parent has soft-busy'd the pages it passes us and will unbusy
 *	those whos rtvals[] entry is not set to VM_PAGER_PEND on return.
 *	We need to unbusy the rest on I/O completion.
 *
 * No requirements.
 */
void
swap_pager_putpages(vm_object_t object, vm_page_t *m, int count,
		    boolean_t sync, int *rtvals)
{
	int i;
	int n = 0;

	vm_object_hold(object);

	if (count && m[0]->object != object) {
		panic("swap_pager_getpages: object mismatch %p/%p", 
		    object, 
		    m[0]->object
		);
	}

	/*
	 * Step 1
	 *
	 * Turn object into OBJT_SWAP
	 * check for bogus sysops
	 * force sync if not pageout process
	 */
	if (object->type == OBJT_DEFAULT) {
		if (object->type == OBJT_DEFAULT)
			swp_pager_meta_convert(object);
	}

	if (curthread != pagethread)
		sync = TRUE;

	/*
	 * Step 2
	 *
	 * Update nsw parameters from swap_async_max sysctl values.  
	 * Do not let the sysop crash the machine with bogus numbers.
	 */
	if (swap_async_max != nsw_wcount_async_max) {
		int n;

		/*
		 * limit range
		 */
		if ((n = swap_async_max) > nswbuf / 2)
			n = nswbuf / 2;
		if (n < 1)
			n = 1;
		swap_async_max = n;

		/*
		 * Adjust difference ( if possible ).  If the current async
		 * count is too low, we may not be able to make the adjustment
		 * at this time.
		 *
		 * vm_token needed for nsw_wcount sleep interlock
		 */
		lwkt_gettoken(&vm_token);
		n -= nsw_wcount_async_max;
		if (nsw_wcount_async + n >= 0) {
			nsw_wcount_async_max += n;
			pbuf_adjcount(&nsw_wcount_async, n);
		}
		lwkt_reltoken(&vm_token);
	}

	/*
	 * Step 3
	 *
	 * Assign swap blocks and issue I/O.  We reallocate swap on the fly.
	 * The page is left dirty until the pageout operation completes
	 * successfully.
	 */

	for (i = 0; i < count; i += n) {
		struct buf *bp;
		struct bio *bio;
		swblk_t blk;
		int j;

		/*
		 * Maximum I/O size is limited by a number of factors.
		 */

		n = min(BLIST_MAX_ALLOC, count - i);
		n = min(n, nsw_cluster_max);

		lwkt_gettoken(&vm_token);

		/*
		 * Get biggest block of swap we can.  If we fail, fall
		 * back and try to allocate a smaller block.  Don't go
		 * overboard trying to allocate space if it would overly
		 * fragment swap.
		 */
		while (
		    (blk = swp_pager_getswapspace(object, n)) == SWAPBLK_NONE &&
		    n > 4
		) {
			n >>= 1;
		}
		if (blk == SWAPBLK_NONE) {
			for (j = 0; j < n; ++j)
				rtvals[i+j] = VM_PAGER_FAIL;
			lwkt_reltoken(&vm_token);
			continue;
		}
		if (vm_report_swap_allocs > 0) {
			kprintf("swap_alloc %08jx,%d\n", (intmax_t)blk, n);
			--vm_report_swap_allocs;
		}

		/*
		 * The I/O we are constructing cannot cross a physical
		 * disk boundry in the swap stripe.  Note: we are still
		 * at splvm().
		 */
		if ((blk ^ (blk + n)) & dmmax_mask) {
			j = ((blk + dmmax) & dmmax_mask) - blk;
			swp_pager_freeswapspace(object, blk + j, n - j);
			n = j;
		}

		/*
		 * All I/O parameters have been satisfied, build the I/O
		 * request and assign the swap space.
		 */
		if (sync == TRUE)
			bp = getpbuf_kva(&nsw_wcount_sync);
		else
			bp = getpbuf_kva(&nsw_wcount_async);
		bio = &bp->b_bio1;

		lwkt_reltoken(&vm_token);

		pmap_qenter((vm_offset_t)bp->b_data, &m[i], n);

		bp->b_bcount = PAGE_SIZE * n;
		bio->bio_offset = (off_t)blk << PAGE_SHIFT;

		for (j = 0; j < n; ++j) {
			vm_page_t mreq = m[i+j];

			swp_pager_meta_build(mreq->object, mreq->pindex,
					     blk + j);
			if (object->type == OBJT_SWAP)
				vm_page_dirty(mreq);
			rtvals[i+j] = VM_PAGER_OK;

			vm_page_flag_set(mreq, PG_SWAPINPROG);
			bp->b_xio.xio_pages[j] = mreq;
		}
		bp->b_xio.xio_npages = n;

		mycpu->gd_cnt.v_swapout++;
		mycpu->gd_cnt.v_swappgsout += bp->b_xio.xio_npages;

		bp->b_dirtyoff = 0;		/* req'd for NFS */
		bp->b_dirtyend = bp->b_bcount;	/* req'd for NFS */
		bp->b_cmd = BUF_CMD_WRITE;
		bio->bio_caller_info1.index = SWBIO_WRITE;

		/*
		 * asynchronous
		 */
		if (sync == FALSE) {
			bio->bio_done = swp_pager_async_iodone;
			BUF_KERNPROC(bp);
			vn_strategy(swapdev_vp, bio);

			for (j = 0; j < n; ++j)
				rtvals[i+j] = VM_PAGER_PEND;
			continue;
		}

		/*
		 * Issue synchrnously.
		 *
		 * Wait for the sync I/O to complete, then update rtvals.
		 * We just set the rtvals[] to VM_PAGER_PEND so we can call
		 * our async completion routine at the end, thus avoiding a
		 * double-free.
		 */
		bio->bio_caller_info1.index |= SWBIO_SYNC;
		bio->bio_done = biodone_sync;
		bio->bio_flags |= BIO_SYNC;
		vn_strategy(swapdev_vp, bio);
		biowait(bio, "swwrt");

		for (j = 0; j < n; ++j)
			rtvals[i+j] = VM_PAGER_PEND;

		/*
		 * Now that we are through with the bp, we can call the
		 * normal async completion, which frees everything up.
		 */
		swp_pager_async_iodone(bio);
	}
	vm_object_drop(object);
}

/*
 * No requirements.
 */
void
swap_pager_newswap(void)
{
	swp_sizecheck();
}

/*
 *	swp_pager_async_iodone:
 *
 *	Completion routine for asynchronous reads and writes from/to swap.
 *	Also called manually by synchronous code to finish up a bp.
 *
 *	For READ operations, the pages are PG_BUSY'd.  For WRITE operations, 
 *	the pages are vm_page_t->busy'd.  For READ operations, we PG_BUSY 
 *	unbusy all pages except the 'main' request page.  For WRITE 
 *	operations, we vm_page_t->busy'd unbusy all pages ( we can do this 
 *	because we marked them all VM_PAGER_PEND on return from putpages ).
 *
 *	This routine may not block.
 *
 * No requirements.
 */
static void
swp_pager_async_iodone(struct bio *bio)
{
	struct buf *bp = bio->bio_buf;
	vm_object_t object = NULL;
	int i;
	int *nswptr;

	/*
	 * report error
	 */
	if (bp->b_flags & B_ERROR) {
		kprintf(
		    "swap_pager: I/O error - %s failed; offset %lld,"
			"size %ld, error %d\n",
		    ((bio->bio_caller_info1.index & SWBIO_READ) ?
			"pagein" : "pageout"),
		    (long long)bio->bio_offset,
		    (long)bp->b_bcount,
		    bp->b_error
		);
	}

	/*
	 * set object, raise to splvm().
	 */
	if (bp->b_xio.xio_npages)
		object = bp->b_xio.xio_pages[0]->object;

	/*
	 * remove the mapping for kernel virtual
	 */
	pmap_qremove((vm_offset_t)bp->b_data, bp->b_xio.xio_npages);

	/*
	 * cleanup pages.  If an error occurs writing to swap, we are in
	 * very serious trouble.  If it happens to be a disk error, though,
	 * we may be able to recover by reassigning the swap later on.  So
	 * in this case we remove the m->swapblk assignment for the page 
	 * but do not free it in the rlist.  The errornous block(s) are thus
	 * never reallocated as swap.  Redirty the page and continue.
	 */
	for (i = 0; i < bp->b_xio.xio_npages; ++i) {
		vm_page_t m = bp->b_xio.xio_pages[i];

		if (bp->b_flags & B_ERROR) {
			/*
			 * If an error occurs I'd love to throw the swapblk
			 * away without freeing it back to swapspace, so it
			 * can never be used again.  But I can't from an 
			 * interrupt.
			 */

			if (bio->bio_caller_info1.index & SWBIO_READ) {
				/*
				 * When reading, reqpage needs to stay
				 * locked for the parent, but all other
				 * pages can be freed.  We still want to
				 * wakeup the parent waiting on the page,
				 * though.  ( also: pg_reqpage can be -1 and 
				 * not match anything ).
				 *
				 * We have to wake specifically requested pages
				 * up too because we cleared PG_SWAPINPROG and
				 * someone may be waiting for that.
				 *
				 * NOTE: for reads, m->dirty will probably
				 * be overridden by the original caller of
				 * getpages so don't play cute tricks here.
				 *
				 * NOTE: We can't actually free the page from
				 * here, because this is an interrupt.  It
				 * is not legal to mess with object->memq
				 * from an interrupt.  Deactivate the page
				 * instead.
				 */

				m->valid = 0;
				vm_page_flag_clear(m, PG_ZERO);
				vm_page_flag_clear(m, PG_SWAPINPROG);

				/*
				 * bio_driver_info holds the requested page
				 * index.
				 */
				if (i != (int)(intptr_t)bio->bio_driver_info) {
					vm_page_deactivate(m);
					vm_page_wakeup(m);
				} else {
					vm_page_flash(m);
				}
				/*
				 * If i == bp->b_pager.pg_reqpage, do not wake 
				 * the page up.  The caller needs to.
				 */
			} else {
				/*
				 * If a write error occurs remove the swap
				 * assignment (note that PG_SWAPPED may or
				 * may not be set depending on prior activity).
				 *
				 * Re-dirty OBJT_SWAP pages as there is no
				 * other backing store, we can't throw the
				 * page away.
				 *
				 * Non-OBJT_SWAP pages (aka swapcache) must
				 * not be dirtied since they may not have
				 * been dirty in the first place, and they
				 * do have backing store (the vnode).
				 */
				vm_page_busy_wait(m, FALSE, "swadpg");
				swp_pager_meta_ctl(m->object, m->pindex,
						   SWM_FREE);
				vm_page_flag_clear(m, PG_SWAPPED);
				if (m->object->type == OBJT_SWAP) {
					vm_page_dirty(m);
					vm_page_activate(m);
				}
				vm_page_flag_clear(m, PG_SWAPINPROG);
				vm_page_io_finish(m);
				vm_page_wakeup(m);
			}
		} else if (bio->bio_caller_info1.index & SWBIO_READ) {
			/*
			 * NOTE: for reads, m->dirty will probably be 
			 * overridden by the original caller of getpages so
			 * we cannot set them in order to free the underlying
			 * swap in a low-swap situation.  I don't think we'd
			 * want to do that anyway, but it was an optimization
			 * that existed in the old swapper for a time before
			 * it got ripped out due to precisely this problem.
			 *
			 * clear PG_ZERO in page.
			 *
			 * If not the requested page then deactivate it.
			 *
			 * Note that the requested page, reqpage, is left
			 * busied, but we still have to wake it up.  The
			 * other pages are released (unbusied) by 
			 * vm_page_wakeup().  We do not set reqpage's
			 * valid bits here, it is up to the caller.
			 */

			/* 
			 * NOTE: can't call pmap_clear_modify(m) from an
			 * interrupt thread, the pmap code may have to map
			 * non-kernel pmaps and currently asserts the case.
			 */
			/*pmap_clear_modify(m);*/
			m->valid = VM_PAGE_BITS_ALL;
			vm_page_undirty(m);
			vm_page_flag_clear(m, PG_ZERO | PG_SWAPINPROG);
			vm_page_flag_set(m, PG_SWAPPED);

			/*
			 * We have to wake specifically requested pages
			 * up too because we cleared PG_SWAPINPROG and
			 * could be waiting for it in getpages.  However,
			 * be sure to not unbusy getpages specifically
			 * requested page - getpages expects it to be 
			 * left busy.
			 *
			 * bio_driver_info holds the requested page
			 */
			if (i != (int)(intptr_t)bio->bio_driver_info) {
				vm_page_deactivate(m);
				vm_page_wakeup(m);
			} else {
				vm_page_flash(m);
			}
		} else {
			/*
			 * Mark the page clean but do not mess with the
			 * pmap-layer's modified state.  That state should
			 * also be clear since the caller protected the
			 * page VM_PROT_READ, but allow the case.
			 *
			 * We are in an interrupt, avoid pmap operations.
			 *
			 * If we have a severe page deficit, deactivate the
			 * page.  Do not try to cache it (which would also
			 * involve a pmap op), because the page might still
			 * be read-heavy.
			 *
			 * When using the swap to cache clean vnode pages
			 * we do not mess with the page dirty bits.
			 */
			vm_page_busy_wait(m, FALSE, "swadpg");
			if (m->object->type == OBJT_SWAP)
				vm_page_undirty(m);
			vm_page_flag_clear(m, PG_SWAPINPROG);
			vm_page_flag_set(m, PG_SWAPPED);
			if (vm_page_count_severe())
				vm_page_deactivate(m);
#if 0
			if (!vm_page_count_severe() || !vm_page_try_to_cache(m))
				vm_page_protect(m, VM_PROT_READ);
#endif
			vm_page_io_finish(m);
			vm_page_wakeup(m);
		}
	}

	/*
	 * adjust pip.  NOTE: the original parent may still have its own
	 * pip refs on the object.
	 */

	if (object)
		vm_object_pip_wakeup_n(object, bp->b_xio.xio_npages);

	/*
	 * Release the physical I/O buffer.
	 *
	 * NOTE: Due to synchronous operations in the write case b_cmd may
	 *	 already be set to BUF_CMD_DONE and BIO_SYNC may have already
	 *	 been cleared.
	 *
	 * Use vm_token to interlock nsw_rcount/wcount wakeup?
	 */
	lwkt_gettoken(&vm_token);
	if (bio->bio_caller_info1.index & SWBIO_READ)
		nswptr = &nsw_rcount;
	else if (bio->bio_caller_info1.index & SWBIO_SYNC)
		nswptr = &nsw_wcount_sync;
	else
		nswptr = &nsw_wcount_async;
	bp->b_cmd = BUF_CMD_DONE;
	relpbuf(bp, nswptr);
	lwkt_reltoken(&vm_token);
}

/*
 * Fault-in a potentially swapped page and remove the swap reference.
 * (used by swapoff code)
 *
 * object must be held.
 */
static __inline void
swp_pager_fault_page(vm_object_t object, int *sharedp, vm_pindex_t pindex)
{
	struct vnode *vp;
	vm_page_t m;
	int error;

	ASSERT_LWKT_TOKEN_HELD(vm_object_token(object));

	if (object->type == OBJT_VNODE) {
		/*
		 * Any swap related to a vnode is due to swapcache.  We must
		 * vget() the vnode in case it is not active (otherwise
		 * vref() will panic).  Calling vm_object_page_remove() will
		 * ensure that any swap ref is removed interlocked with the
		 * page.  clean_only is set to TRUE so we don't throw away
		 * dirty pages.
		 */
		vp = object->handle;
		error = vget(vp, LK_SHARED | LK_RETRY | LK_CANRECURSE);
		if (error == 0) {
			vm_object_page_remove(object, pindex, pindex + 1, TRUE);
			vput(vp);
		}
	} else {
		/*
		 * Otherwise it is a normal OBJT_SWAP object and we can
		 * fault the page in and remove the swap.
		 */
		m = vm_fault_object_page(object, IDX_TO_OFF(pindex),
					 VM_PROT_NONE,
					 VM_FAULT_DIRTY | VM_FAULT_UNSWAP,
					 sharedp, &error);
		if (m)
			vm_page_unhold(m);
	}
}

/*
 * This removes all swap blocks related to a particular device.  We have
 * to be careful of ripups during the scan.
 */
static int swp_pager_swapoff_callback(struct swblock *swap, void *data);

int
swap_pager_swapoff(int devidx)
{
	struct vm_object marker;
	vm_object_t object;
	struct swswapoffinfo info;

	bzero(&marker, sizeof(marker));
	marker.type = OBJT_MARKER;

	lwkt_gettoken(&vmobj_token);
	TAILQ_INSERT_HEAD(&vm_object_list, &marker, object_list);

	while ((object = TAILQ_NEXT(&marker, object_list)) != NULL) {
		if (object->type == OBJT_MARKER)
			goto skip;
		if (object->type != OBJT_SWAP && object->type != OBJT_VNODE)
			goto skip;
		vm_object_hold(object);
		if (object->type != OBJT_SWAP && object->type != OBJT_VNODE) {
			vm_object_drop(object);
			goto skip;
		}
		info.object = object;
		info.shared = 0;
		info.devidx = devidx;
		swblock_rb_tree_RB_SCAN(&object->swblock_root,
					NULL,
					swp_pager_swapoff_callback,
					&info);
		vm_object_drop(object);
skip:
		if (object == TAILQ_NEXT(&marker, object_list)) {
			TAILQ_REMOVE(&vm_object_list, &marker, object_list);
			TAILQ_INSERT_AFTER(&vm_object_list, object,
					   &marker, object_list);
		}
	}
	TAILQ_REMOVE(&vm_object_list, &marker, object_list);
	lwkt_reltoken(&vmobj_token);

	/*
	 * If we fail to locate all swblocks we just fail gracefully and
	 * do not bother to restore paging on the swap device.  If the
	 * user wants to retry the user can retry.
	 */
	if (swdevt[devidx].sw_nused)
		return (1);
	else
		return (0);
}

static
int
swp_pager_swapoff_callback(struct swblock *swap, void *data)
{
	struct swswapoffinfo *info = data;
	vm_object_t object = info->object;
	vm_pindex_t index;
	swblk_t v;
	int i;

	index = swap->swb_index;
	for (i = 0; i < SWAP_META_PAGES; ++i) {
		/*
		 * Make sure we don't race a dying object.  This will
		 * kill the scan of the object's swap blocks entirely.
		 */
		if (object->flags & OBJ_DEAD)
			return(-1);

		/*
		 * Fault the page, which can obviously block.  If the swap
		 * structure disappears break out.
		 */
		v = swap->swb_pages[i];
		if (v != SWAPBLK_NONE && BLK2DEVIDX(v) == info->devidx) {
			swp_pager_fault_page(object, &info->shared,
					     swap->swb_index + i);
			/* swap ptr might go away */
			if (RB_LOOKUP(swblock_rb_tree,
				      &object->swblock_root, index) != swap) {
				break;
			}
		}
	}
	return(0);
}

/************************************************************************
 *				SWAP META DATA 				*
 ************************************************************************
 *
 *	These routines manipulate the swap metadata stored in the 
 *	OBJT_SWAP object.  All swp_*() routines must be called at
 *	splvm() because swap can be freed up by the low level vm_page
 *	code which might be called from interrupts beyond what splbio() covers.
 *
 *	Swap metadata is implemented with a global hash and not directly
 *	linked into the object.  Instead the object simply contains
 *	appropriate tracking counters.
 */

/*
 * Lookup the swblock containing the specified swap block index.
 *
 * The caller must hold the object.
 */
static __inline
struct swblock *
swp_pager_lookup(vm_object_t object, vm_pindex_t index)
{
	ASSERT_LWKT_TOKEN_HELD(vm_object_token(object));
	index &= ~(vm_pindex_t)SWAP_META_MASK;
	return (RB_LOOKUP(swblock_rb_tree, &object->swblock_root, index));
}

/*
 * Remove a swblock from the RB tree.
 *
 * The caller must hold the object.
 */
static __inline
void
swp_pager_remove(vm_object_t object, struct swblock *swap)
{
	ASSERT_LWKT_TOKEN_HELD(vm_object_token(object));
	RB_REMOVE(swblock_rb_tree, &object->swblock_root, swap);
}

/*
 * Convert default object to swap object if necessary
 *
 * The caller must hold the object.
 */
static void
swp_pager_meta_convert(vm_object_t object)
{
	if (object->type == OBJT_DEFAULT) {
		object->type = OBJT_SWAP;
		KKASSERT(object->swblock_count == 0);
	}
}

/*
 * SWP_PAGER_META_BUILD() -	add swap block to swap meta data for object
 *
 *	We first convert the object to a swap object if it is a default
 *	object.  Vnode objects do not need to be converted.
 *
 *	The specified swapblk is added to the object's swap metadata.  If
 *	the swapblk is not valid, it is freed instead.  Any previously
 *	assigned swapblk is freed.
 *
 * The caller must hold the object.
 */
static void
swp_pager_meta_build(vm_object_t object, vm_pindex_t index, swblk_t swapblk)
{
	struct swblock *swap;
	struct swblock *oswap;
	vm_pindex_t v;

	KKASSERT(swapblk != SWAPBLK_NONE);
	ASSERT_LWKT_TOKEN_HELD(vm_object_token(object));

	/*
	 * Convert object if necessary
	 */
	if (object->type == OBJT_DEFAULT)
		swp_pager_meta_convert(object);
	
	/*
	 * Locate swblock.  If not found create, but if we aren't adding
	 * anything just return.  If we run out of space in the map we wait
	 * and, since the hash table may have changed, retry.
	 */
retry:
	swap = swp_pager_lookup(object, index);

	if (swap == NULL) {
		int i;

		swap = zalloc(swap_zone);
		if (swap == NULL) {
			vm_wait(0);
			goto retry;
		}
		swap->swb_index = index & ~(vm_pindex_t)SWAP_META_MASK;
		swap->swb_count = 0;

		++object->swblock_count;

		for (i = 0; i < SWAP_META_PAGES; ++i)
			swap->swb_pages[i] = SWAPBLK_NONE;
		oswap = RB_INSERT(swblock_rb_tree, &object->swblock_root, swap);
		KKASSERT(oswap == NULL);
	}

	/*
	 * Delete prior contents of metadata.
	 *
	 * NOTE: Decrement swb_count after the freeing operation (which
	 *	 might block) to prevent racing destruction of the swblock.
	 */
	index &= SWAP_META_MASK;

	while ((v = swap->swb_pages[index]) != SWAPBLK_NONE) {
		swap->swb_pages[index] = SWAPBLK_NONE;
		/* can block */
		swp_pager_freeswapspace(object, v, 1);
		--swap->swb_count;
		--mycpu->gd_vmtotal.t_vm;
	}

	/*
	 * Enter block into metadata
	 */
	swap->swb_pages[index] = swapblk;
	if (swapblk != SWAPBLK_NONE) {
		++swap->swb_count;
		++mycpu->gd_vmtotal.t_vm;
	}
}

/*
 * SWP_PAGER_META_FREE() - free a range of blocks in the object's swap metadata
 *
 *	The requested range of blocks is freed, with any associated swap 
 *	returned to the swap bitmap.
 *
 *	This routine will free swap metadata structures as they are cleaned 
 *	out.  This routine does *NOT* operate on swap metadata associated
 *	with resident pages.
 *
 * The caller must hold the object.
 */
static int swp_pager_meta_free_callback(struct swblock *swb, void *data);

static void
swp_pager_meta_free(vm_object_t object, vm_pindex_t index, vm_pindex_t count)
{
	struct swfreeinfo info;

	ASSERT_LWKT_TOKEN_HELD(vm_object_token(object));

	/*
	 * Nothing to do
	 */
	if (object->swblock_count == 0) {
		KKASSERT(RB_EMPTY(&object->swblock_root));
		return;
	}
	if (count == 0)
		return;

	/*
	 * Setup for RB tree scan.  Note that the pindex range can be huge
	 * due to the 64 bit page index space so we cannot safely iterate.
	 */
	info.object = object;
	info.basei = index & ~(vm_pindex_t)SWAP_META_MASK;
	info.begi = index;
	info.endi = index + count - 1;
	swblock_rb_tree_RB_SCAN(&object->swblock_root, rb_swblock_scancmp,
				swp_pager_meta_free_callback, &info);
}

/*
 * The caller must hold the object.
 */
static
int
swp_pager_meta_free_callback(struct swblock *swap, void *data)
{
	struct swfreeinfo *info = data;
	vm_object_t object = info->object;
	int index;
	int eindex;

	/*
	 * Figure out the range within the swblock.  The wider scan may
	 * return edge-case swap blocks when the start and/or end points
	 * are in the middle of a block.
	 */
	if (swap->swb_index < info->begi)
		index = (int)info->begi & SWAP_META_MASK;
	else
		index = 0;

	if (swap->swb_index + SWAP_META_PAGES > info->endi)
		eindex = (int)info->endi & SWAP_META_MASK;
	else
		eindex = SWAP_META_MASK;

	/*
	 * Scan and free the blocks.  The loop terminates early
	 * if (swap) runs out of blocks and could be freed.
	 *
	 * NOTE: Decrement swb_count after swp_pager_freeswapspace()
	 *	 to deal with a zfree race.
	 */
	while (index <= eindex) {
		swblk_t v = swap->swb_pages[index];

		if (v != SWAPBLK_NONE) {
			swap->swb_pages[index] = SWAPBLK_NONE;
			/* can block */
			swp_pager_freeswapspace(object, v, 1);
			--mycpu->gd_vmtotal.t_vm;
			if (--swap->swb_count == 0) {
				swp_pager_remove(object, swap);
				zfree(swap_zone, swap);
				--object->swblock_count;
				break;
			}
		}
		++index;
	}

	/* swap may be invalid here due to zfree above */
	lwkt_yield();

	return(0);
}

/*
 * SWP_PAGER_META_FREE_ALL() - destroy all swap metadata associated with object
 *
 *	This routine locates and destroys all swap metadata associated with
 *	an object.
 *
 * NOTE: Decrement swb_count after the freeing operation (which
 *	 might block) to prevent racing destruction of the swblock.
 *
 * The caller must hold the object.
 */
static void
swp_pager_meta_free_all(vm_object_t object)
{
	struct swblock *swap;
	int i;

	ASSERT_LWKT_TOKEN_HELD(vm_object_token(object));

	while ((swap = RB_ROOT(&object->swblock_root)) != NULL) {
		swp_pager_remove(object, swap);
		for (i = 0; i < SWAP_META_PAGES; ++i) {
			swblk_t v = swap->swb_pages[i];
			if (v != SWAPBLK_NONE) {
				/* can block */
				swp_pager_freeswapspace(object, v, 1);
				--swap->swb_count;
				--mycpu->gd_vmtotal.t_vm;
			}
		}
		if (swap->swb_count != 0)
			panic("swap_pager_meta_free_all: swb_count != 0");
		zfree(swap_zone, swap);
		--object->swblock_count;
		lwkt_yield();
	}
	KKASSERT(object->swblock_count == 0);
}

/*
 * SWP_PAGER_METACTL() -  misc control of swap and vm_page_t meta data.
 *
 *	This routine is capable of looking up, popping, or freeing
 *	swapblk assignments in the swap meta data or in the vm_page_t.
 *	The routine typically returns the swapblk being looked-up, or popped,
 *	or SWAPBLK_NONE if the block was freed, or SWAPBLK_NONE if the block
 *	was invalid.  This routine will automatically free any invalid 
 *	meta-data swapblks.
 *
 *	It is not possible to store invalid swapblks in the swap meta data
 *	(other then a literal 'SWAPBLK_NONE'), so we don't bother checking.
 *
 *	When acting on a busy resident page and paging is in progress, we 
 *	have to wait until paging is complete but otherwise can act on the 
 *	busy page.
 *
 *	SWM_FREE	remove and free swap block from metadata
 *	SWM_POP		remove from meta data but do not free.. pop it out
 *
 * The caller must hold the object.
 */
static swblk_t
swp_pager_meta_ctl(vm_object_t object, vm_pindex_t index, int flags)
{
	struct swblock *swap;
	swblk_t r1;

	if (object->swblock_count == 0)
		return(SWAPBLK_NONE);

	r1 = SWAPBLK_NONE;
	swap = swp_pager_lookup(object, index);

	if (swap != NULL) {
		index &= SWAP_META_MASK;
		r1 = swap->swb_pages[index];

		if (r1 != SWAPBLK_NONE) {
			if (flags & (SWM_FREE|SWM_POP)) {
				swap->swb_pages[index] = SWAPBLK_NONE;
				--mycpu->gd_vmtotal.t_vm;
				if (--swap->swb_count == 0) {
					swp_pager_remove(object, swap);
					zfree(swap_zone, swap);
					--object->swblock_count;
				}
			} 
			/* swap ptr may be invalid */
			if (flags & SWM_FREE) {
				swp_pager_freeswapspace(object, r1, 1);
				r1 = SWAPBLK_NONE;
			}
		}
		/* swap ptr may be invalid */
	}
	return(r1);
}
