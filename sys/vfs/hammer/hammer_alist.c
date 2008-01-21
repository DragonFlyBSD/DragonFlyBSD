/*
 * HAMMER_ALIST.C -
 *
 * Bitmap allocator/deallocator, using a radix tree with hinting.
 * Unlimited-size allocations, power-of-2 only, power-of-2 aligned results
 * only.  This module has been separated from the generic kernel module and
 * written specifically for embedding in HAMMER storage structures.
 * 
 * Copyright (c) 2007 The DragonFly Project.  All rights reserved.
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
 * $DragonFly: src/sys/vfs/hammer/Attic/hammer_alist.c,v 1.10 2008/01/21 00:00:19 dillon Exp $
 */
/*
 * This module implements a generic allocator through the use of a hinted
 * radix tree.  All allocations must be in powers of 2 and will return
 * similarly aligned results.  The radix tree typically recurses within
 * a memory buffer and then continues its recursion by chaining to other
 * memory buffers, creating a seemless whole capable of managing any amount
 * of storage.
 *
 * The radix tree is layed out recursively using a linear array.  Each meta
 * node is immediately followed (layed out sequentially in memory) by
 * HAMMER_ALIST_META_RADIX lower level nodes.  This is a recursive structure
 * but one that can be easily scanned through a very simple 'skip'
 * calculation.
 *
 * The radix tree supports an early-termination optimization which
 * effectively allows us to efficiently mix large and small allocations
 * with a single abstraction.  The address space can be partitioned
 * arbitrarily without adding much in the way of additional meta-storage
 * for the allocator.
 *
 * The radix tree supports allocator layering. By supplying a base_radix > 1
 * the allocator will issue callbacks to recurse into lower layers once 
 * higher layers have been exhausted.
 *
 * ALLOCATIONS IN MULTIPLES OF base_radix WILL BE ENTIRELY RETAINED IN THE
 * HIGHER LEVEL ALLOCATOR AND NEVER RECURSE.  This means the init function
 * will never be called and the A-list will consider the underlying zone
 * as being uninitialized.  If you then do a partial free, the A-list will
 * call the init function before freeing.  Most users of this API, including
 * HAMMER, only allocate and free whole zones, or only allocate and free
 * partial zones, and never mix their metaphors.
 *
 * This code can be compiled stand-alone for debugging.
 */

#ifdef _KERNEL

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/lock.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <vm/vm.h>
#include <vm/vm_object.h>
#include <vm/vm_kern.h>
#include <vm/vm_extern.h>
#include <vm/vm_page.h>

#include "hammer_alist.h"
#include "hammer_disk.h"

#else

#ifndef ALIST_NO_DEBUG
#define ALIST_DEBUG
#endif

#include <sys/types.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

#define kmalloc(a,b,c)	malloc(a)
#define kfree(a,b)	free(a)
#define kprintf		printf
#define KKASSERT(exp)	assert(exp)
struct malloc_type;

#include "hammer_alist.h"

void panic(const char *ctl, ...);

#endif

/*
 * static support functions
 */
static int32_t hammer_alst_leaf_alloc_fwd(hammer_almeta_t scan,
					int32_t blk, int count, int32_t atblk);
static int32_t hammer_alst_meta_alloc_fwd(hammer_alist_t live,
					hammer_almeta_t scan,
					int32_t blk, int32_t count,
					int32_t radix, int skip, int32_t atblk);
static int32_t hammer_alst_leaf_alloc_rev(hammer_almeta_t scan,
					int32_t blk, int count, int32_t atblk);
static int32_t hammer_alst_meta_alloc_rev(hammer_alist_t live,
					hammer_almeta_t scan,
					int32_t blk, int32_t count,
					int32_t radix, int skip, int32_t atblk);
static int32_t hammer_alst_find(hammer_alist_t live, hammer_almeta_t scan,
					int32_t blk, int32_t radix,
					int32_t skip, int32_t atblk);
static void hammer_alst_leaf_free(hammer_almeta_t scan, int32_t relblk,
					int count);
static void hammer_alst_meta_free(hammer_alist_t live, hammer_almeta_t scan,
					int32_t freeBlk, int32_t count, 
					int32_t radix, int skip, int32_t blk);
static int32_t	hammer_alst_radix_init(hammer_almeta_t scan,
					int32_t radix, int skip, int32_t count);
static void	hammer_alst_radix_recover(hammer_alist_recover_t info,
					hammer_almeta_t scan, int32_t blk,
					int32_t radix, int skip, int32_t count,
					int32_t a_beg, int32_t a_end);
#ifdef ALIST_DEBUG
static void	hammer_alst_radix_print(hammer_alist_t live,
					hammer_almeta_t scan, int32_t blk,
					int32_t radix, int skip, int tab);
#endif

/*
 * Initialize an a-list config structure for use.  The config structure
 * describes the basic structure of an a-list's topology and may be
 * shared by any number of a-lists which have the same topology.
 *
 * blocks is the total number of blocks, that is the number of blocks
 * handled at this layer multiplied by the base radix.
 *
 * When base_radix != 1 the A-list has only meta elements and does not have
 * any leaves, in order to be able to track partial allocations.
 */
void
hammer_alist_template(hammer_alist_config_t bl, int32_t blocks,
		      int32_t base_radix, int32_t maxmeta)
{
	int radix;
	int skip;

	/*
	 * Calculate radix and skip field used for scanning.  The leaf nodes
	 * in our tree are either BMAP or META nodes depending on whether
	 * we chain to a lower level allocation layer or not.
	 */
	if (base_radix == 1)
		radix = HAMMER_ALIST_BMAP_RADIX;
	else
		radix = HAMMER_ALIST_META_RADIX;
	skip = 1;

	while (radix < blocks / base_radix) {
		radix *= HAMMER_ALIST_META_RADIX;
		skip = skip * HAMMER_ALIST_META_RADIX + 1;
	}

	/*
	 * Increase the radix based on the number of blocks a lower level
	 * allocator is able to handle at the 'base' of our allocator.
	 * If base_radix != 1 the caller will have to initialize the callback
	 * fields to implement the lower level allocator.
	 */
	KKASSERT((int64_t)radix * (int64_t)base_radix < 0x80000000LL);
	radix *= base_radix;

	bzero(bl, sizeof(*bl));

	bl->bl_blocks = blocks;
	bl->bl_base_radix = base_radix;
	bl->bl_radix = radix;
	bl->bl_skip = skip;
	bl->bl_rootblks = hammer_alst_radix_init(NULL, bl->bl_radix,
						 bl->bl_skip, blocks);
	++bl->bl_rootblks;	/* one more for freeblks header */
	if (base_radix == 1)
		bl->bl_terminal = 1;
	KKASSERT(maxmeta == 0 || bl->bl_rootblks <= maxmeta);

#if defined(ALIST_DEBUG)
	kprintf(
		"PRIMARY ALIST LAYER manages %d blocks"
		", requiring %dK (%d bytes) of ram\n",
		bl->bl_blocks / bl->bl_base_radix,
		(bl->bl_rootblks * sizeof(struct hammer_almeta) + 1023) / 1024,
		(bl->bl_rootblks * sizeof(struct hammer_almeta))
	);
	kprintf("ALIST raw radix tree contains %d records\n", bl->bl_rootblks);
#endif
}

/*
 * Initialize a new A-list
 */
void
hammer_alist_init(hammer_alist_t live, int32_t start, int32_t count,
		  enum hammer_alloc_state state)
{
	hammer_alist_config_t bl = live->config;

	/*
	 * Note: base_freeblks is a count, not a block number limit.
	 */
	live->meta->bm_alist_freeblks = 0;
	live->meta->bm_alist_base_freeblks = count;
	hammer_alst_radix_init(live->meta + 1, bl->bl_radix,
			       bl->bl_skip, bl->bl_blocks);
	if (count && state == HAMMER_ASTATE_FREE)
		hammer_alist_free(live, start, count);
}

#if !defined(_KERNEL) && defined(ALIST_DEBUG)

/*
 * hammer_alist_create()	(userland only)
 *
 *	create a alist capable of handling up to the specified number of
 *	blocks.  blocks must be greater then 0
 *
 *	The smallest alist consists of a single leaf node capable of 
 *	managing HAMMER_ALIST_BMAP_RADIX blocks, or (if base_radix != 1)
 *	a single meta node capable of managing HAMMER_ALIST_META_RADIX
 *	blocks which recurses into other storage layers for a total
 *	handling capability of (HAMMER_ALIST_META_RADIX * base_radix) blocks.
 *
 *	Larger a-list's increase their capability exponentially by
 *	HAMMER_ALIST_META_RADIX.
 *
 *	The block count is the total number of blocks inclusive of any
 *	layering.  blocks can be less then base_radix and will result in
 *	a radix tree with a single leaf node which then chains down.
 */

hammer_alist_t 
hammer_alist_create(int32_t blocks, int32_t base_radix,
		    struct malloc_type *mtype, enum hammer_alloc_state state)
{
	hammer_alist_t live;
	hammer_alist_config_t bl;
	size_t metasize;

	live = kmalloc(sizeof(*live), mtype, M_WAITOK);
	live->config = bl = kmalloc(sizeof(*bl), mtype, M_WAITOK);
	hammer_alist_template(bl, blocks, base_radix, 0);

	metasize = sizeof(*live->meta) * bl->bl_rootblks;
	live->meta = kmalloc(metasize, mtype, M_WAITOK);
	bzero(live->meta, metasize);

#if defined(ALIST_DEBUG)
	kprintf(
		"ALIST representing %d blocks (%d MB of swap)"
		", requiring %dK (%d bytes) of ram\n",
		bl->bl_blocks,
		bl->bl_blocks * 4 / 1024,
		(bl->bl_rootblks * sizeof(*live->meta) + 1023) / 1024,
		(bl->bl_rootblks * sizeof(*live->meta))
	);
	if (base_radix != 1) {
		kprintf("ALIST recurses when it reaches a base_radix of %d\n",
			base_radix);
	}
	kprintf("ALIST raw radix tree contains %d records\n", bl->bl_rootblks);
#endif
	hammer_alist_init(live, 0, blocks, state);
	return(live);
}

void
hammer_alist_destroy(hammer_alist_t live, struct malloc_type *mtype)
{
	kfree(live->config, mtype);
	kfree(live->meta, mtype);
	live->config = NULL;
	live->meta = NULL;
	kfree(live, mtype);
}

#endif

/*
 * hammer_alist_alloc()
 *
 *	Reserve space in the block bitmap.  Return the base of a contiguous
 *	region or HAMMER_ALIST_BLOCK_NONE if space could not be allocated.
 */

int32_t 
hammer_alist_alloc(hammer_alist_t live, int32_t count)
{
	int32_t blk = HAMMER_ALIST_BLOCK_NONE;
	hammer_alist_config_t bl = live->config;

	KKASSERT((count | (count - 1)) == (count << 1) - 1);

	if (bl && count <= bl->bl_radix) {
		/*
		 * When skip is 1 we are at a leaf.  If we are the terminal
		 * allocator we use our native leaf functions and radix will
		 * be HAMMER_ALIST_BMAP_RADIX.  Otherwise we are a meta node
		 * which will chain to another allocator.
		 */
		if (bl->bl_skip == 1 && bl->bl_terminal) {
#ifndef _KERNEL
			KKASSERT(bl->bl_radix == HAMMER_ALIST_BMAP_RADIX);
#endif
			blk = hammer_alst_leaf_alloc_fwd(
				    live->meta + 1, 0, count, 0);
		} else {
			blk = hammer_alst_meta_alloc_fwd(
				    live, live->meta + 1,
				    0, count, bl->bl_radix, bl->bl_skip, 0);
		}
		if (blk != HAMMER_ALIST_BLOCK_NONE)
			live->meta->bm_alist_freeblks -= count;
	}
	return(blk);
}

int32_t 
hammer_alist_alloc_fwd(hammer_alist_t live, int32_t count, int32_t atblk)
{
	int32_t blk = HAMMER_ALIST_BLOCK_NONE;
	hammer_alist_config_t bl = live->config;

	KKASSERT((count | (count - 1)) == (count << 1) - 1);

	if (bl && count <= bl->bl_radix) {
		/*
		 * When skip is 1 we are at a leaf.  If we are the terminal
		 * allocator we use our native leaf functions and radix will
		 * be HAMMER_ALIST_BMAP_RADIX.  Otherwise we are a meta node
		 * which will chain to another allocator.
		 */
		if (bl->bl_skip == 1 && bl->bl_terminal) {
#ifndef _KERNEL
			KKASSERT(bl->bl_radix == HAMMER_ALIST_BMAP_RADIX);
#endif
			blk = hammer_alst_leaf_alloc_fwd(
				    live->meta + 1, 0, count, atblk);
		} else {
			blk = hammer_alst_meta_alloc_fwd(
				    live, live->meta + 1,
				    0, count, bl->bl_radix, bl->bl_skip, atblk);
		}
		if (blk != HAMMER_ALIST_BLOCK_NONE)
			live->meta->bm_alist_freeblks -= count;
	}
	return(blk);
}

int32_t 
hammer_alist_alloc_rev(hammer_alist_t live, int32_t count, int32_t atblk)
{
	hammer_alist_config_t bl = live->config;
	int32_t blk = HAMMER_ALIST_BLOCK_NONE;

	KKASSERT((count | (count - 1)) == (count << 1) - 1);

	if (bl && count < bl->bl_radix) {
		if (bl->bl_skip == 1 && bl->bl_terminal) {
#ifndef _KERNEL
			KKASSERT(bl->bl_radix == HAMMER_ALIST_BMAP_RADIX);
#endif
			blk = hammer_alst_leaf_alloc_rev(
				    live->meta + 1, 0, count, atblk);
		} else {
			blk = hammer_alst_meta_alloc_rev(
				    live, live->meta + 1,
				    0, count, bl->bl_radix, bl->bl_skip, atblk);
		}
		if (blk != HAMMER_ALIST_BLOCK_NONE)
			live->meta->bm_alist_freeblks -= count;
	}
	return(blk);
}

/*
 * hammer_alist_find()
 *
 *	Locate the first block >= atblk and < maxblk marked as allocated
 *	in the A-list and return it.  Return HAMMER_ALIST_BLOCK_NONE if
 *	no block could be found.
 */
int32_t
hammer_alist_find(hammer_alist_t live, int32_t atblk, int32_t maxblk)
{
	hammer_alist_config_t bl = live->config;
	KKASSERT(live->config != NULL);
	KKASSERT(atblk >= 0);
	atblk = hammer_alst_find(live, live->meta + 1, 0, bl->bl_radix,
				 bl->bl_skip, atblk);
	if (atblk >= maxblk)
		atblk = HAMMER_ALIST_BLOCK_NONE;
	return(atblk);
}

/*
 * hammer_alist_free()
 *
 *	Free up space in the block bitmap.  Return the base of a contiguous
 *	region.  Panic if an inconsistancy is found.
 *
 *	Unlike allocations, there are no alignment requirements for blkno or
 *	count when freeing blocks.
 */

void 
hammer_alist_free(hammer_alist_t live, int32_t blkno, int32_t count)
{
	hammer_alist_config_t bl = live->config;

	KKASSERT(blkno + count <= bl->bl_blocks);
	if (bl->bl_skip == 1 && bl->bl_terminal) {
#ifndef _KERNEL
		KKASSERT(bl->bl_radix == HAMMER_ALIST_BMAP_RADIX);
#endif
		hammer_alst_leaf_free(live->meta + 1, blkno, count);
	} else {
		hammer_alst_meta_free(live, live->meta + 1,
				      blkno, count,
				      bl->bl_radix, bl->bl_skip, 0);
	}
	live->meta->bm_alist_freeblks += count;
}

/*
 * Recover an A-list.  This will dive down to the leaves and regenerate
 * the hints and the freeblks count.  This function will also recurse
 * through any stacked A-lists.  > 0 is returned on success, a negative
 * error code on failure.
 *
 * Since A-lists have no pointers the only thing that can prevent recovery
 * is an I/O error in e.g. a stacked A-list.  This doesn't mean the recovered
 * map will be meaningful, however.
 *
 * blk is usually passed as 0 at the top level and is adjusted as the recovery
 * code scans the A-list.  It is only used when recursing down a stacked
 * A-list.
 *
 * (start,count) describes the region of the A-list which is allowed to contain
 * free blocks.  Any region to the left or right will be marked as allocated.
 */
int
hammer_alist_recover(hammer_alist_t live, int32_t blk, int32_t start,
		     int32_t count)
{
	hammer_alist_config_t bl = live->config;
	struct hammer_alist_recover info;

	info.live = live;
	info.error = 0;

	live->meta->bm_alist_freeblks = 0;
	live->meta->bm_alist_base_freeblks = count;
	hammer_alst_radix_recover(&info, live->meta + 1, blk, bl->bl_radix,
				  bl->bl_skip, bl->bl_blocks,
				  start, start + count);
	if (info.error)
		return(info.error);
	return(live->meta->bm_alist_freeblks);
}

int
hammer_alist_isfull(hammer_alist_t live)
{
	return(live->meta->bm_alist_freeblks == 0);
}

int
hammer_alist_isempty(hammer_alist_t live)
{
	return((int)live->meta->bm_alist_freeblks ==
	       live->meta->bm_alist_base_freeblks);
}

#ifdef ALIST_DEBUG

/*
 * alist_print()    - dump radix tree
 */

void
hammer_alist_print(hammer_alist_t live, int tab)
{
	hammer_alist_config_t bl = live->config;

	kprintf("%*.*sALIST (%d/%d free blocks) {\n",
		tab, tab, "",
		live->meta->bm_alist_freeblks,
		live->meta->bm_alist_base_freeblks);
	hammer_alst_radix_print(live, live->meta + 1, 0,
				bl->bl_radix, bl->bl_skip, tab + 4);
	kprintf("%*.*s}\n", tab, tab, "");
}

#endif

/************************************************************************
 *			  ALLOCATION SUPPORT FUNCTIONS			*
 ************************************************************************
 *
 *	These support functions do all the actual work.  They may seem 
 *	rather longish, but that's because I've commented them up.  The
 *	actual code is straight forward.
 *
 */

/*
 * hammer_alist_leaf_alloc_fwd()
 *
 *	Allocate at a leaf in the radix tree (a bitmap).
 *
 *	This is the core of the allocator and is optimized for the 1 block
 *	and the HAMMER_ALIST_BMAP_RADIX block allocation cases.  Other cases
 *	are somewhat slower.  The 1 block allocation case is log2 and extremely
 *	quick.
 */

static int32_t
hammer_alst_leaf_alloc_fwd(hammer_almeta_t scan, int32_t blk,
			   int count, int32_t atblk)
{
	u_int32_t orig = scan->bm_bitmap;
	int32_t saveblk = blk;

	/*
	 * Optimize bitmap all-allocated case.  Also, count = 1
	 * case assumes at least 1 bit is free in the bitmap, so
	 * we have to take care of this case here.
	 */
	if (orig == 0) {
		scan->bm_bighint = 0;
		return(HAMMER_ALIST_BLOCK_NONE);
	}

#if 0
	/*
	 * Optimized code to allocate one bit out of the bitmap
	 *
	 * mask iterates e.g. 00001111 00000011 00000001
	 *
	 * mask starts at 00001111
	 */
	if (count == 1) {
		u_int32_t mask;
		int j = HAMMER_ALIST_BMAP_RADIX/2;
		int r = 0;

		mask = (u_int32_t)-1 >> (HAMMER_ALIST_BMAP_RADIX/2);

		while (j) {
			if ((orig & mask) == 0) {
			    r += j;
			    orig >>= j;
			}
			j >>= 1;
			mask >>= j;
		}
		scan->bm_bitmap &= ~(1 << r);
		return(blk + r);
	}
#endif

	/*
	 * non-optimized code to allocate N bits out of the bitmap.
	 * The more bits, the faster the code runs.  It will run
	 * the slowest allocating 2 bits, but since there aren't any
	 * memory ops in the core loop (or shouldn't be, anyway),
	 * you probably won't notice the difference.
	 *
	 * Similar to the blist case, the alist code also requires
	 * allocations to be power-of-2 sized and aligned to the
	 * size of the allocation, which simplifies the algorithm.
	 */
	{
		int j;
		int n = HAMMER_ALIST_BMAP_RADIX - count;
		u_int32_t mask;

		mask = (u_int32_t)-1 >> n;

		for (j = 0; j <= n; j += count) {
			if ((orig & mask) == mask && blk >= atblk) {
				scan->bm_bitmap &= ~mask;
				return(blk);
			}
			mask = mask << count;
			blk += count;
		}
	}

	/*
	 * We couldn't allocate count in this subtree, update bighint if
	 * atblk didn't interfere with the hinting mechanism.
	 */
	if (saveblk >= atblk)
		scan->bm_bighint = count - 1;
	return(HAMMER_ALIST_BLOCK_NONE);
}

/*
 * This version allocates blocks in the reverse direction.
 */
static int32_t
hammer_alst_leaf_alloc_rev(hammer_almeta_t scan, int32_t blk,
			   int count, int32_t atblk)
{
	u_int32_t orig = scan->bm_bitmap;
	int32_t saveblk;

	/*
	 * Optimize bitmap all-allocated case.  Also, count = 1
	 * case assumes at least 1 bit is free in the bitmap, so
	 * we have to take care of this case here.
	 */
	if (orig == 0) {
		scan->bm_bighint = 0;
		return(HAMMER_ALIST_BLOCK_NONE);
	}

#if 0
	/*
	 * Optimized code to allocate one bit out of the bitmap
	 */
	if (count == 1) {
		u_int32_t mask;
		int j = HAMMER_ALIST_BMAP_RADIX/2;
		int r = HAMMER_ALIST_BMAP_RADIX - 1;

		mask = ~((u_int32_t)-1 >> (HAMMER_ALIST_BMAP_RADIX/2));

		while (j) {
			if ((orig & mask) == 0) {
			    r -= j;
			    orig <<= j;
			}
			j >>= 1;
			mask <<= j;
		}
		scan->bm_bitmap &= ~(1 << r);
		return(blk + r);
	}
#endif

	/*
	 * non-optimized code to allocate N bits out of the bitmap.
	 * The more bits, the faster the code runs.  It will run
	 * the slowest allocating 2 bits, but since there aren't any
	 * memory ops in the core loop (or shouldn't be, anyway),
	 * you probably won't notice the difference.
	 *
	 * Similar to the blist case, the alist code also requires
	 * allocations to be power-of-2 sized and aligned to the
	 * size of the allocation, which simplifies the algorithm.
	 *
	 * initial mask if count == 2:  1100....0000
	 */
	{
		int j;
		int n = HAMMER_ALIST_BMAP_RADIX - count;
		u_int32_t mask;

		mask = ((u_int32_t)-1 >> n) << n;
		blk += n;
		saveblk = blk;

		for (j = n; j >= 0; j -= count) {
			if ((orig & mask) == mask && blk <= atblk) {
				scan->bm_bitmap &= ~mask;
				return(blk);
			}
			mask = mask >> count;
			blk -= count;
		}
	}

	/*
	 * We couldn't allocate count in this subtree, update bighint if
	 * atblk didn't interfere with it.
	 */
	if (saveblk <= atblk)
		scan->bm_bighint = count - 1;
	return(HAMMER_ALIST_BLOCK_NONE);
}

/*
 * hammer_alist_meta_alloc_fwd()
 *
 *	Allocate at a meta in the radix tree.
 *
 *	Attempt to allocate at a meta node.  If we can't, we update
 *	bighint and return a failure.  Updating bighint optimize future
 *	calls that hit this node.  We have to check for our collapse cases
 *	and we have a few optimizations strewn in as well.
 */
static int32_t
hammer_alst_meta_alloc_fwd(hammer_alist_t live, hammer_almeta_t scan,
			   int32_t blk, int32_t count,
			   int32_t radix, int skip, int32_t atblk
) {
	hammer_alist_config_t bl;
	u_int32_t mask;
	u_int32_t pmask;
	int32_t saveblk;
	int next_skip;
	int i;

	/*
	 * ALL-ALLOCATED special case
	 */
	if (scan->bm_bitmap == 0 || scan->bm_bitmap == 0xAAAAAAAAU)  {
		scan->bm_bighint = 0;
		return(HAMMER_ALIST_BLOCK_NONE);
	}

	radix /= HAMMER_ALIST_META_RADIX;
	bl = live->config;

	/*
	 * Radix now represents each bitmap entry for this meta node.  If
	 * the number of blocks being allocated can be fully represented,
	 * we allocate directly out of this meta node.
	 *
	 * Meta node bitmaps use 2 bits per block.
	 *
	 *	00	ALL-ALLOCATED - UNINITIALIZED
	 *	01	PARTIALLY-FREE/PARTIALLY-ALLOCATED
	 *	10	ALL-ALLOCATED - INITIALIZED
	 *	11	ALL-FREE      - UNINITIALIZED
	 */
	if (count >= radix) {
		int n = count / radix * 2;	/* number of bits */
		int nd2 = n / 2;
		int j;

		mask = (u_int32_t)-1 >> (HAMMER_ALIST_BMAP_RADIX - n);
		saveblk = blk;
		for (j = 0; j < (int)HAMMER_ALIST_META_RADIX; j += nd2) {
			if ((scan->bm_bitmap & mask) == mask && blk >= atblk) {
				/*
				 * NOTE: Marked all-allocate/uninitialized
				 * rather then all-allocated/initialized.
				 * See the warning at the top of the file.
				 */
				scan->bm_bitmap &= ~mask;
				return(blk);
			}
			mask <<= n;
			blk += radix * nd2;
		}
		if (scan->bm_bighint >= count && saveblk >= atblk)
			scan->bm_bighint = count >> 1;
		return(HAMMER_ALIST_BLOCK_NONE);
	}

	/*
	 * If the count is too big we couldn't allocate anything from a
	 * recursion even if the sub-tree were entirely free.
	 */
	saveblk = blk;
	if (count > radix)
		goto failed;

	/*
	 * If not we have to recurse.
	 */
	mask = 0x00000003;
	pmask = 0x00000001;

	if (skip == 1) {
		/*
		 * If skip is 1 we are a meta leaf node, which means there
		 * is another allocation layer we have to dive down into.
		 */
		for (i = 0; i < (int)HAMMER_ALIST_META_RADIX; ++i) {
			/*
			 * If the next layer is completely free then call
			 * its init function to initialize it.
			 */
			if ((scan->bm_bitmap & mask) == mask &&
			    blk + radix > atblk) {
				if (bl->bl_radix_init(live->info, blk, radix, HAMMER_ASTATE_FREE) == 0) {
					/*
					 * NOTE: Marked all-allocate/uninit-
					 * ialized rather then all-allocated/
					 * initialized.  See the warning at
					 * the top of the file.
					 */
					scan->bm_bitmap &= ~mask;
					scan->bm_bitmap |= pmask;
				}
			}

			/*
			 * If there may be some free blocks try to allocate
			 * out of the layer.  If the layer indicates that
			 * it is completely full then clear both bits to
			 * propogate the condition.
			 */
			if ((scan->bm_bitmap & mask) == pmask &&
			    blk + radix > atblk) {
				int32_t r;
				int32_t full;

				r = bl->bl_radix_alloc_fwd(live->info, blk,
							   radix, count,
							   atblk, &full);
				if (full) {
					scan->bm_bitmap &= ~mask;
					scan->bm_bitmap |= pmask << 1;
				}
				if (r != HAMMER_ALIST_BLOCK_NONE)
					return(r);
			}
			blk += radix;
			mask <<= 2;
			pmask <<= 2;
		}
	} else {
		/*
		 * Otherwise there are sub-records in the current a-list
		 * layer.  We can also peek into the sub-layers to get
		 * more accurate size hints.
		 */
		next_skip = (skip - 1) / HAMMER_ALIST_META_RADIX;
		for (i = 1; i < skip; i += next_skip) {
			if (scan[i].bm_bighint == (int32_t)-1) {
				/* 
				 * Terminator
				 */
				break;
			}

			/*
			 * Initialize bitmap if allocating from the all-free
			 * case.
			 */
			if ((scan->bm_bitmap & mask) == mask) {
				scan[i].bm_bitmap = (u_int32_t)-1;
				scan[i].bm_bighint = radix;
			}

			if (count <= scan[i].bm_bighint &&
			    blk + radix > atblk) {
				/*
				 * count fits in object, recurse into the
				 * next layer.  If the next_skip is 1 it
				 * will be either a normal leaf or a meta
				 * leaf.
				 */
				int32_t r;

				if (next_skip == 1 && bl->bl_terminal) {
					r = hammer_alst_leaf_alloc_fwd(
						&scan[i], blk, count, atblk);
				} else {
					r = hammer_alst_meta_alloc_fwd(
						live, &scan[i],
						blk, count,
						radix, next_skip, atblk);
				}
				if (r != HAMMER_ALIST_BLOCK_NONE) {
					if (scan[i].bm_bitmap == 0) {
						scan->bm_bitmap &= ~mask;
					} else if (scan[i].bm_bitmap == 0xAAAAAAAAU) {
						scan->bm_bitmap &= ~mask;
						scan->bm_bitmap |= pmask << 1;
					} else {
						scan->bm_bitmap &= ~mask;
						scan->bm_bitmap |= pmask;
					}
					return(r);
				}
			}
			blk += radix;
			mask <<= 2;
			pmask <<= 2;
		}
	}

failed:
	/*
	 * We couldn't allocate count in this subtree, update bighint.
	 */
	if (scan->bm_bighint >= count && saveblk >= atblk)
		scan->bm_bighint = count >> 1;
	return(HAMMER_ALIST_BLOCK_NONE);
}

/*
 * This version allocates blocks in the reverse direction.
 *
 * This is really nasty code
 */
static int32_t
hammer_alst_meta_alloc_rev(hammer_alist_t live, hammer_almeta_t scan,
			   int32_t blk, int32_t count,
			   int32_t radix, int skip, int32_t atblk
) {
	hammer_alist_config_t bl;
	int i;
	int j;
	u_int32_t mask;
	u_int32_t pmask;
	int32_t saveblk;
	int next_skip;

	/*
	 * ALL-ALLOCATED special case
	 */
	if (scan->bm_bitmap == 0 || scan->bm_bitmap == 0xAAAAAAAAU)  {
		scan->bm_bighint = 0;
		return(HAMMER_ALIST_BLOCK_NONE);
	}

	radix /= HAMMER_ALIST_META_RADIX;
	bl = live->config;

	/*
	 * Radix now represents each bitmap entry for this meta node.  If
	 * the number of blocks being allocated can be fully represented,
	 * we allocate directly out of this meta node.
	 *
	 * Meta node bitmaps use 2 bits per block.
	 *
	 *	00	ALL-ALLOCATED (uninitialized)
	 *	01	PARTIALLY-FREE/PARTIALLY-ALLOCATED
	 *	10	ALL-ALLOCATED (initialized)
	 *	11	ALL-FREE
	 */
	if (count >= radix) {
		int n = count / radix * 2;	/* number of bits */
		int nd2 = n / 2;		/* number of radi */

		/*
		 * Initial mask if e.g. n == 2:  1100....0000
		 */
		mask = (u_int32_t)-1 >> (HAMMER_ALIST_BMAP_RADIX - n) <<
			(HAMMER_ALIST_BMAP_RADIX - n);
		blk += (HAMMER_ALIST_META_RADIX - nd2) * radix;
		saveblk = blk;
		for (j = HAMMER_ALIST_META_RADIX - nd2; j >= 0; j -= nd2) {
			if ((scan->bm_bitmap & mask) == mask && blk <= atblk) {
				scan->bm_bitmap &= ~mask;
				return(blk);
			}
			mask >>= n;
			blk -= nd2 * radix;
		}
		if (scan->bm_bighint >= count && saveblk <= atblk)
			scan->bm_bighint = count >> 1;
		return(HAMMER_ALIST_BLOCK_NONE);
	}

	/*
	 * NOTE: saveblk must represent the entire layer, not the base blk
	 * of the last element.  Otherwise an atblk that is inside the
	 * last element can cause bighint to be updated for a failed
	 * allocation when we didn't actually test all available blocks.
	 */

	if (skip == 1) {
		/*
		 * We need the recurse but we are at a meta node leaf, which
		 * means there is another layer under us.
		 */
		mask = 0xC0000000;
		pmask = 0x40000000;
		blk += radix * HAMMER_ALIST_META_RADIX;

		saveblk = blk;
		blk -= radix;

		for (i = 0; i < (int)HAMMER_ALIST_META_RADIX; ++i) {
			/*
			 * If the next layer is completely free then call
			 * its init function to initialize it.  The init
			 * function is responsible for the initial freeing.
			 */
			if ((scan->bm_bitmap & mask) == mask && blk <= atblk) {
				if (bl->bl_radix_init(live->info, blk, radix, HAMMER_ASTATE_FREE) == 0) {
					scan->bm_bitmap &= ~mask;
					scan->bm_bitmap |= pmask;
				}
			}

			/*
			 * If there may be some free blocks try to allocate
			 * out of the layer.  If the layer indicates that
			 * it is completely full then clear both bits to
			 * propogate the condition.
			 */
			if ((scan->bm_bitmap & mask) == pmask && blk <= atblk) {
				int32_t r;
				int32_t full;

				r = bl->bl_radix_alloc_rev(live->info, blk,
							   radix, count,
							   atblk, &full);
				if (full) {
					scan->bm_bitmap &= ~mask;
					scan->bm_bitmap |= pmask << 1;
				}
				if (r != HAMMER_ALIST_BLOCK_NONE)
					return(r);
			}
			mask >>= 2;
			pmask >>= 2;
			blk -= radix;
		}
	} else {
		/*
		 * Since we are going in the reverse direction we need an
		 * extra loop to determine if there is a terminator, then
		 * run backwards.
		 *
		 * This is a little weird but we do not want to overflow the
		 * mask/pmask in the loop.
		 */
		next_skip = (skip - 1) / HAMMER_ALIST_META_RADIX;
		j = 0;
		for (i = 1; i < skip; i += next_skip) {
			if (scan[i].bm_bighint == (int32_t)-1) {
				KKASSERT(j != 0);
				break;
			}
			blk += radix;
			j += 2;
		}

		saveblk = blk;
		blk -= radix;
		j -= 2;
		mask = 0x00000003 << j;
		pmask = 0x00000001 << j;
		i -= next_skip;

		while (i >= 1) {
			/*
			 * Initialize the bitmap in the child if allocating
			 * from the all-free case.
			 */
			if ((scan->bm_bitmap & mask) == mask) {
				scan[i].bm_bitmap = (u_int32_t)-1;
				scan[i].bm_bighint = radix;
			}

			/*
			 * Handle various count cases.  Bighint may be too
			 * large but is never too small.
			 */
			if (count <= scan[i].bm_bighint && blk <= atblk) {
				/*
				 * count fits in object
				 */
				int32_t r;
				if (next_skip == 1 && bl->bl_terminal) {
					r = hammer_alst_leaf_alloc_rev(
						&scan[i], blk, count, atblk);
				} else {
					r = hammer_alst_meta_alloc_rev(
						live, &scan[i],
						blk, count,
						radix, next_skip, atblk);
				}
				if (r != HAMMER_ALIST_BLOCK_NONE) {
					if (scan[i].bm_bitmap == 0) {
						scan->bm_bitmap &= ~mask;
					} else if (scan[i].bm_bitmap == 0xAAAAAAAAU) {
						scan->bm_bitmap &= ~mask;
						scan->bm_bitmap |= pmask << 1;
					} else {
						scan->bm_bitmap &= ~mask;
						scan->bm_bitmap |= pmask;
					}
					return(r);
				}
			}
			blk -= radix;
			mask >>= 2;
			pmask >>= 2;
			i -= next_skip;
		}
	}

	/*
	 * We couldn't allocate count in this subtree, update bighint.
	 * Since we are restricted to powers of 2, the next highest count
	 * we might be able to allocate is (count >> 1).
	 */
	if (scan->bm_bighint >= count && saveblk <= atblk)
		scan->bm_bighint = count >> 1;
	return(HAMMER_ALIST_BLOCK_NONE);
}

/*
 * HAMMER_ALST_FIND()
 *
 * Locate the first allocated block greater or equal to atblk.
 */
static int32_t
hammer_alst_find(hammer_alist_t live, hammer_almeta_t scan, int32_t blk,
		 int32_t radix, int32_t skip, int32_t atblk)
{
	u_int32_t mask;
	u_int32_t pmask;
	int32_t next_skip;
	int32_t tmpblk;
	int i;
	int j;

	/*
	 * Leaf node (currently hammer_alist_find() only works on terminal
	 * a-list's and the case is asserted in hammer_alist_find()).
	 */
	if (skip == 1 && live->config->bl_terminal) {
		if (scan->bm_bitmap == (u_int32_t)-1)
			return(HAMMER_ALIST_BLOCK_NONE);
		for (i = 0; i < (int)HAMMER_ALIST_BMAP_RADIX; ++i) {
			if (blk + i < atblk)
				continue;
			if ((scan->bm_bitmap & (1 << i)) == 0)
				return(blk + i);
		}
		return(HAMMER_ALIST_BLOCK_NONE);
	}

	/*
	 * Meta
	 */
	radix /= HAMMER_ALIST_META_RADIX;
	next_skip = (skip - 1) / HAMMER_ALIST_META_RADIX;
	mask = 0x00000003;
	pmask = 0x00000001;
	for (j = 0, i = 1; j < (int)HAMMER_ALIST_META_RADIX;
	     (i += next_skip), ++j) {
		/*
		 * Check Terminator
		 */
		if (scan[i].bm_bighint == (int32_t)-1) {
			break;
		}

		/*
		 * Recurse if this meta might contain a desired block.
		 */
		if (blk + radix > atblk) {
			if ((scan->bm_bitmap & mask) == 0) {
				/*
				 * 00 - all-allocated, uninitialized
				 */
				return(atblk < blk ? blk : atblk);
			} else if ((scan->bm_bitmap & mask) == (pmask << 1)) {
				/*
				 * 10 - all-allocated, initialized
				 */
				return(atblk < blk ? blk : atblk);
			} else if ((scan->bm_bitmap & mask) == mask) {
				/* 
				 * 11 - all-free (skip)
				 */
			} else if (next_skip == 0) {
				/*
				 * Partially allocated but we have to recurse
				 * into a stacked A-list.
				 */
				tmpblk = live->config->bl_radix_find(
						live->info, blk, radix, atblk);
				if (tmpblk != HAMMER_ALIST_BLOCK_NONE)
					return(tmpblk);
			} else if ((scan->bm_bitmap & mask) == pmask) {
				/*
				 * 01 - partially-allocated
				 */
				tmpblk = hammer_alst_find(live, &scan[i],
							  blk, radix,
							  next_skip, atblk);
				if (tmpblk != HAMMER_ALIST_BLOCK_NONE)
					return(tmpblk);

			}
		}
		mask <<= 2;
		pmask <<= 2;
		blk += radix;
	}
	return(HAMMER_ALIST_BLOCK_NONE);
}

/*
 * HAMMER_ALST_LEAF_FREE()
 *
 *	Free allocated blocks from leaf bitmap.  The allocation code is
 *	restricted to powers of 2, the freeing code is not.
 */
static void
hammer_alst_leaf_free(hammer_almeta_t scan, int32_t blk, int count) {
	/*
	 * free some data in this bitmap
	 *
	 * e.g.
	 *	0000111111111110000
	 *          \_________/\__/
	 *		v        n
	 */
	int n = blk & (HAMMER_ALIST_BMAP_RADIX - 1);
	u_int32_t mask;

	mask = ((u_int32_t)-1 << n) &
	    ((u_int32_t)-1 >> (HAMMER_ALIST_BMAP_RADIX - count - n));

	if (scan->bm_bitmap & mask)
		panic("hammer_alst_radix_free: freeing free block");
	scan->bm_bitmap |= mask;

	/*
	 * We could probably do a better job here.  We are required to make
	 * bighint at least as large as the biggest contiguous block of 
	 * data.  If we just shoehorn it, a little extra overhead will
	 * be incured on the next allocation (but only that one typically).
	 */
	scan->bm_bighint = HAMMER_ALIST_BMAP_RADIX;
}

/*
 * BLST_META_FREE()
 *
 *	Free allocated blocks from radix tree meta info.
 *
 *	This support routine frees a range of blocks from the bitmap.
 *	The range must be entirely enclosed by this radix node.  If a
 *	meta node, we break the range down recursively to free blocks
 *	in subnodes (which means that this code can free an arbitrary
 *	range whereas the allocation code cannot allocate an arbitrary
 *	range).
 */

static void 
hammer_alst_meta_free(hammer_alist_t live, hammer_almeta_t scan, 
		      int32_t freeBlk, int32_t count,
		      int32_t radix, int skip, int32_t blk
) {
	hammer_alist_config_t bl;
	int next_skip;
	u_int32_t mask;
	u_int32_t pmask;
	int i;

	/*
	 * Break the free down into its components.  Because it is so easy
	 * to implement, frees are not limited to power-of-2 sizes.
	 *
	 * Each block in a meta-node bitmap takes two bits.
	 */
	radix /= HAMMER_ALIST_META_RADIX;
	bl = live->config;

	i = (freeBlk - blk) / radix;
	blk += i * radix;
	mask = 0x00000003 << (i * 2);
	pmask = 0x00000001 << (i * 2);

	if (skip == 1) {
		/*
		 * Our meta node is a leaf node, which means it must recurse
		 * into another allocator.
		 */
		while (i < (int)HAMMER_ALIST_META_RADIX &&
		       blk < freeBlk + count) {
			int32_t v;

			v = blk + radix - freeBlk;
			if (v > count)
				v = count;

			if (scan->bm_bighint == (int32_t)-1)
				panic("hammer_alst_meta_free: freeing unexpected range");
			KKASSERT((scan->bm_bitmap & mask) != mask);

			if (freeBlk == blk && count >= radix) {
				/*
				 * Freeing an entire zone.  Only recurse if
				 * the zone was initialized.  A 00 state means
				 * that the zone is marked all-allocated,
				 * but was never initialized.
				 *
				 * Then set the zone to the all-free state (11).
				 */
				int32_t empty;

				if (scan->bm_bitmap & mask) {
					bl->bl_radix_free(live->info, blk, radix,
							  freeBlk - blk, v, &empty);
					KKASSERT(empty);
					bl->bl_radix_destroy(live->info, blk, radix);
				}
				scan->bm_bitmap |= mask;
				scan->bm_bighint = radix * HAMMER_ALIST_META_RADIX;
				/* XXX bighint not being set properly */
			} else {
				/*
				 * Recursion case, partial free.  If 00 the
				 * zone is marked all allocated but has never
				 * been initialized, so we init it.
				 */
				int32_t empty;

				if ((scan->bm_bitmap & mask) == 0)
					bl->bl_radix_init(live->info, blk, radix, HAMMER_ASTATE_ALLOC);
				bl->bl_radix_free(live->info, blk, radix,
						  freeBlk - blk, v, &empty);
				if (empty) {
					if (scan->bm_bitmap & mask)
						bl->bl_radix_destroy(live->info, blk, radix);
					scan->bm_bitmap |= mask;
					scan->bm_bighint = radix * HAMMER_ALIST_META_RADIX;
					/* XXX bighint not being set properly */
				} else {
					scan->bm_bitmap &= ~mask;
					scan->bm_bitmap |= pmask;
					if (scan->bm_bighint < radix / 2)
						scan->bm_bighint = radix / 2;
					/* XXX bighint not being set properly */
				}
			}
			++i;
			mask <<= 2;
			pmask <<= 2;
			count -= v;
			freeBlk += v;
			blk += radix;
		}
	} else {
		next_skip = (skip - 1) / HAMMER_ALIST_META_RADIX;
		i = 1 + i * next_skip;

		while (i <= skip && blk < freeBlk + count) {
			int32_t v;

			KKASSERT(mask != 0);
			KKASSERT(count != 0);

			v = blk + radix - freeBlk;
			if (v > count)
				v = count;

			if (scan->bm_bighint == (int32_t)-1)
				panic("hammer_alst_meta_free: freeing unexpected range");

			if (freeBlk == blk && count >= radix) {
				/*
				 * All-free case, no need to update sub-tree
				 */
				scan->bm_bitmap |= mask;
				scan->bm_bighint = radix * HAMMER_ALIST_META_RADIX;
				/* XXX bighint not being set properly */
			} else {
				/*
				 * Recursion case
				 */
				if (next_skip == 1 && bl->bl_terminal) {
					hammer_alst_leaf_free(&scan[i], freeBlk, v);
				} else {
					hammer_alst_meta_free(live, &scan[i],
							      freeBlk, v,
							      radix, next_skip,
							      blk);
				}
				if (scan[i].bm_bitmap == (u_int32_t)-1) {
					scan->bm_bitmap |= mask;
					/* XXX bighint not set properly */
					scan->bm_bighint = radix * HAMMER_ALIST_META_RADIX;
				} else {
					scan->bm_bitmap &= ~mask;
					scan->bm_bitmap |= pmask;
					/* XXX bighint not set properly */
					if (scan->bm_bighint < scan[i].bm_bighint)
						scan->bm_bighint = scan[i].bm_bighint;
				}
			}
			mask <<= 2;
			pmask <<= 2;
			count -= v;
			freeBlk += v;
			blk += radix;
			i += next_skip;
		}
	}
}

/*
 * hammer_alst_radix_init()
 *
 *	Initialize our meta structures and bitmaps and calculate the exact
 *	number of meta-nodes required to manage 'count' blocks.  
 *
 *	The required space may be truncated due to early termination records.
 */
static int32_t	
hammer_alst_radix_init(hammer_almeta_t scan, int32_t radix,
		       int skip, int32_t count)
{
	int i;
	int next_skip;
	int32_t memindex = 1;
	u_int32_t mask;
	u_int32_t pmask;

	/*
	 * Basic initialization of the almeta for meta or leaf nodes.  This
	 * marks the element as all-allocated.
	 */
	if (scan) {
		scan->bm_bighint = 0;
		scan->bm_bitmap = 0;
	}

	/*
	 * We are at a terminal node, we only eat one meta element.   If
	 * live->config->bl_terminal is set this is a leaf node, otherwise
	 * it is a meta node for a stacked A-list.  We do NOT recurse into
	 * stacked A-lists but simply mark the entire stack as all-free using
	 * code 00 (meaning all-free & uninitialized).
	 */
	if (skip == 1)
		return(memindex);

	/*
	 * Meta node.  If allocating the entire object we can special
	 * case it.  However, we need to figure out how much memory
	 * is required to manage 'count' blocks, so we continue on anyway.
	 */
	radix /= HAMMER_ALIST_META_RADIX;
	next_skip = (skip - 1) / HAMMER_ALIST_META_RADIX;
	mask = 0x00000003;
	pmask = 0x00000001;

	for (i = 1; i < skip; i += next_skip) {
		/*
		 * We eat up to this record
		 */
		memindex = i;

		KKASSERT(mask != 0);

		if (count >= radix) {
			/*
			 * Allocate the entire object
			 */
			memindex += hammer_alst_radix_init(
			    ((scan) ? &scan[i] : NULL),
			    radix,
			    next_skip,
			    radix
			);
			count -= radix;
			/* already marked as wholely allocated */
		} else if (count > 0) {
			/*
			 * Allocate a partial object
			 */
			memindex += hammer_alst_radix_init(
			    ((scan) ? &scan[i] : NULL),
			    radix,
			    next_skip,
			    count
			);
			count = 0;

			/*
			 * Mark as partially allocated
			 */
			if (scan) {
				scan->bm_bitmap &= ~mask;
				scan->bm_bitmap |= pmask;
			}
		} else {
			/*
			 * Add terminator and break out.  The terminal
			 * eats the meta node at scan[i].
			 */
			++memindex;
			if (scan)
				scan[i].bm_bighint = (int32_t)-1;
			/* already marked as wholely allocated */
			break;
		}
		mask <<= 2;
		pmask <<= 2;
	}
	return(memindex);
}

/*
 * hammer_alst_radix_recover()
 *
 *	This code is basically a duplicate of hammer_alst_radix_init()
 *	except it recovers the a-list instead of initializes it.
 *
 *	(a_beg,a_end) specifies the global allocatable range within
 *	the radix tree.  The recovery code guarentees that blocks
 *	within the tree but outside this range are marked as being
 *	allocated to prevent them from being allocated.
 */
static void	
hammer_alst_radix_recover(hammer_alist_recover_t info, hammer_almeta_t scan,
			  int32_t blk, int32_t radix, int skip, int32_t count,
			  int32_t a_beg, int32_t a_end)
{
	hammer_alist_t live = info->live;
	u_int32_t mask;
	u_int32_t pmask;
	int next_skip;
	int i;
	int j;
	int n;

	/*
	 * Don't try to recover bighint, just set it to its maximum
	 * value and let the A-list allocations reoptimize it.  XXX
	 */
	scan->bm_bighint = radix;

	/*
	 * If we are at a terminal node (i.e. not stacked on top of another
	 * A-list), just count the free blocks.
	 */
	if (skip == 1 && live->config->bl_terminal) {
		for (i = 0; i < (int)HAMMER_ALIST_BMAP_RADIX; ++i) {
			if (blk + i < a_beg || blk + i >= a_end)
				scan->bm_bitmap &= ~(1 << i);
			if (scan->bm_bitmap & (1 << i))
				++info->live->meta->bm_alist_freeblks;
		}
		return;
	}

	/*
	 * Recursive meta node (next_skip != 0) or terminal meta
	 * node (next_skip == 0).
	 */
	radix /= HAMMER_ALIST_META_RADIX;
	next_skip = (skip - 1) / HAMMER_ALIST_META_RADIX;
	mask = 0x00000003;
	pmask = 0x00000001;

	for (i = 1, j = 0; j < (int)HAMMER_ALIST_META_RADIX;
	      ++j, (i += next_skip)) {
		/*
		 * Check mask:
		 *
		 *	00	ALL-ALLOCATED - UNINITIALIZED
		 *	01	PARTIALLY-FREE/PARTIALLY-ALLOCATED
		 *	10	ALL-ALLOCATED - INITIALIZED
		 *	11	ALL-FREE      - UNINITIALIZED
		 */
		KKASSERT(mask);

		/*
		 * Adjust the bitmap for out of bounds or partially out of
		 * bounds elements.  If we are completely out of bounds
		 * mark as all-allocated.  If we are partially out of
		 * bounds do not allow the area to be marked all-free.
		 */
		if (blk + radix <= a_beg || blk >= a_end) {
			scan->bm_bitmap &= ~mask;
		} else if (blk < a_beg || blk + radix > a_end) {
			if ((scan->bm_bitmap & mask) == mask) {
				scan->bm_bitmap &= ~mask;
				scan->bm_bitmap |= pmask;
			}
		}

		if (count >= radix) {
			/*
			 * Recover the entire object
			 */
			if ((scan->bm_bitmap & mask) == 0) {
				/*
				 * All-allocated (uninited), do nothing
				 */
			} else if ((scan->bm_bitmap & mask) == mask) {
				/*
				 * All-free (uninited), do nothing
				 */
				live->meta->bm_alist_freeblks += radix;
			} else if (next_skip) {
				/*
				 * Normal meta node, initialized.  Recover and
				 * adjust to either an all-allocated (inited)
				 * or partially-allocated state.
				 */
				hammer_alst_radix_recover(
				    info,
				    &scan[i],
				    blk, radix,
				    next_skip, radix,
				    a_beg, a_end
				);
				if (scan[i].bm_bitmap == 0) {
					scan->bm_bitmap &= ~mask;
				} else if (scan[i].bm_bitmap == 0xAAAAAAAAU) {
					scan->bm_bitmap &= ~mask;
					scan->bm_bitmap |= pmask << 1;
				} else if (scan[i].bm_bitmap == (u_int32_t)-1) {
					scan->bm_bitmap |= mask;
				} else {
					scan->bm_bitmap &= ~mask;
					scan->bm_bitmap |= pmask;
				}
			} else {
				/*
				 * Stacked meta node, recurse.
				 *
				 * XXX need a more sophisticated return
				 * code to control how the bitmap is set
				 * in the parent.
				 */
				n = live->config->bl_radix_recover(
					    live->info,
					    blk, radix, radix);
				if (n >= 0) {
					live->meta->bm_alist_freeblks += n;
					if (n == 0) {
						scan->bm_bitmap =
						    (scan->bm_bitmap & ~mask) |
						    (pmask << 1);
					} else if (n == radix) {
						scan->bm_bitmap |= mask;
					} else {
						scan->bm_bitmap =
						    (scan->bm_bitmap & ~mask) |
						    pmask;
					}
				} else {
					info->error = n;
				}
			}
			count -= radix;
		} else if (count > 0) {
			/*
			 * Recover a partial object.  The object can become
			 * wholely allocated but never wholely free.
			 */
			if (next_skip) {
				hammer_alst_radix_recover(
				    info,
				    &scan[i],
				    blk, radix,
				    next_skip, count,
				    a_beg, a_end
				);
				if (scan[i].bm_bitmap == 0) {
					scan->bm_bitmap &= ~mask;
				} else if (scan[i].bm_bitmap == 0xAAAAAAAAU) {
					scan->bm_bitmap &= ~mask;
					scan->bm_bitmap |= pmask << 1;
				} else {
					scan->bm_bitmap &= ~mask;
					scan->bm_bitmap |= pmask;
				}
			} else {
				n = live->config->bl_radix_recover(
					    live->info,
					    blk, radix, count);
				if (n >= 0) {
					live->meta->bm_alist_freeblks += n;
					if (n == 0) {
						scan->bm_bitmap &= ~mask;
						scan->bm_bitmap |= pmask << 1;
					} else {
						scan->bm_bitmap &= ~mask;
						scan->bm_bitmap |= pmask;
					}
				} else {
					info->error = n;
				}
			}
			count = 0;
		} else if (next_skip) {
			/*
			 * Add terminator.  The terminator eats the meta
			 * node at scan[i].  There is only ONE terminator,
			 * make sure we don't write out any more (set count to
			 * -1) or we may overflow our allocation.
			 */
			if (count == 0) {
				scan[i].bm_bighint = (int32_t)-1;
				count = -1;
			}
			scan->bm_bitmap &= ~mask;	/* all-allocated/uni */
		} else {
			scan->bm_bitmap &= ~mask;	/* all-allocated/uni */
		}
		mask <<= 2;
		pmask <<= 2;
		blk += radix;
	}
}

#ifdef ALIST_DEBUG

static void	
hammer_alst_radix_print(hammer_alist_t live, hammer_almeta_t scan,
			int32_t blk, int32_t radix, int skip, int tab)
{
	int i;
	int next_skip;
	int lastState = 0;
	u_int32_t mask;

	if (skip == 1 && live->config->bl_terminal) {
		kprintf(
		    "%*.*s(%04x,%d): bitmap %08x big=%d\n", 
		    tab, tab, "",
		    blk, radix,
		    scan->bm_bitmap,
		    scan->bm_bighint
		);
		return;
	}

	if (scan->bm_bitmap == 0) {
		kprintf(
		    "%*.*s(%04x,%d) ALL ALLOCATED\n",
		    tab, tab, "",
		    blk,
		    radix
		);
		return;
	}
	if (scan->bm_bitmap == (u_int32_t)-1) {
		kprintf(
		    "%*.*s(%04x,%d) ALL FREE\n",
		    tab, tab, "",
		    blk,
		    radix
		);
		return;
	}

	kprintf(
	    "%*.*s(%04x,%d): %s (%d) bitmap=%08x big=%d {\n",
	    tab, tab, "",
	    blk, radix,
	    (skip == 1 ? "LAYER" : "subtree"),
	    radix,
	    scan->bm_bitmap,
	    scan->bm_bighint
	);

	radix /= HAMMER_ALIST_META_RADIX;
	tab += 4;
	mask = 0x00000003;

	if (skip == 1) {
		for (i = 0; i < HAMMER_ALIST_META_RADIX; ++i) {
			if ((scan->bm_bitmap & mask) == mask) {
				kprintf(
				    "%*.*s(%04x,%d): ALL FREE\n",
				    tab, tab, "",
				    blk, radix
				);
			} else if ((scan->bm_bitmap & mask) == 0) {
				kprintf(
				    "%*.*s(%04x,%d): ALL ALLOCATED\n",
				    tab, tab, "",
				    blk, radix
				);
			} else {
				live->config->bl_radix_print(
						live->info, blk, radix, tab);
			}
			blk += radix;
			mask <<= 2;
		}
	} else {
		next_skip = ((u_int)(skip - 1) / HAMMER_ALIST_META_RADIX);

		for (i = 1; i < skip; i += next_skip) {
			KKASSERT(mask != 0);
			if (scan[i].bm_bighint == (int32_t)-1) {
				kprintf(
				    "%*.*s(%04x,%d): Terminator\n",
				    tab, tab, "",
				    blk, radix
				);
				lastState = 0;
				break;
			}
			if ((scan->bm_bitmap & mask) == mask) {
				kprintf(
				    "%*.*s(%04x,%d): ALL FREE\n",
				    tab, tab, "",
				    blk, radix
				);
			} else if ((scan->bm_bitmap & mask) == 0) {
				kprintf(
				    "%*.*s(%04x,%d): ALL ALLOCATED\n",
				    tab, tab, "",
				    blk, radix
				);
			} else {
				hammer_alst_radix_print(
				    live,
				    &scan[i],
				    blk,
				    radix,
				    next_skip,
				    tab
				);
			}
			blk += radix;
			mask <<= 2;
		}
	}
	tab -= 4;

	kprintf(
	    "%*.*s}\n",
	    tab, tab, ""
	);
}

#endif

#ifdef ALIST_DEBUG

static struct hammer_alist_live **layers;	/* initialized by main */
static int32_t layer_radix = -1;

/*
 * Initialize a zone.
 *
 * If allocating is non-zero this init is being called when transitioning out
 * of an all-free state.  Allocate the zone and mark the whole mess as being
 * free so the caller can then allocate out of this zone.
 *
 * If freeing this init is being called when transitioning out of an
 * initial all-allocated (00) state.  Allocate the zone but leave the whole
 * mess left all-allocated.  The caller will then free the appropriate range.
 */
static
int
debug_radix_init(void *info, int32_t blk, int32_t radix,
		 enum hammer_alloc_state state)
{
	hammer_alist_t layer;
	int layer_no = blk / layer_radix;

	printf("lower layer: init (%04x,%d) layer_radix=%d\n",
	       blk, radix, layer_radix);
	KKASSERT(layer_radix == radix);
	KKASSERT(layers[layer_no] == NULL);
	layer = layers[layer_no] = hammer_alist_create(radix, 1, NULL, state); 
	return(0);
}

static
int32_t
debug_radix_recover(void *info, int32_t blk, int32_t radix, int32_t count)
{
	hammer_alist_t layer;
	int layer_no = blk / layer_radix;
	int32_t n;

	KKASSERT(layer_radix == radix);
	KKASSERT(layers[layer_no] != NULL);
	layer = layers[layer_no];
	n = hammer_alist_recover(layer, blk, 0, count);
	printf("Recover layer %d blk %d result %d/%d\n",
		layer_no, blk, n, count);
	return(n);
}

static
int32_t
debug_radix_find(void *info, int32_t blk, int32_t radix, int32_t atblk)
{
	hammer_alist_t layer;
	int layer_no = blk / layer_radix;
	int32_t res;

	KKASSERT(layer_radix == radix);
	KKASSERT(layers[layer_no] != NULL);
	layer = layers[layer_no];
	res = hammer_alist_find(layer, atblk - blk, radix);
	if (res != HAMMER_ALIST_BLOCK_NONE)
		res += blk;
	return(res);
}

/*
 * This is called when a zone becomes entirely free, typically after a
 * call to debug_radix_free() has indicated that the entire zone is now
 * free.
 */
static
int
debug_radix_destroy(void *info, int32_t blk, int32_t radix)
{
	hammer_alist_t layer;
	int layer_no = blk / layer_radix;

	printf("lower layer: destroy (%04x,%d)\n", blk, radix);
	layer = layers[layer_no];
	KKASSERT(layer != NULL);
	hammer_alist_destroy(layer, NULL);
	layers[layer_no] = NULL;
	return(0);
}


static
int32_t
debug_radix_alloc_fwd(void *info, int32_t blk, int32_t radix,
		      int32_t count, int32_t atblk, int32_t *fullp)
{
	hammer_alist_t layer = layers[blk / layer_radix];
	int32_t r;

	r = hammer_alist_alloc_fwd(layer, count, atblk - blk);
	*fullp = hammer_alist_isfull(layer);
	if (r != HAMMER_ALIST_BLOCK_NONE)
		r += blk;
	return(r);
}

static
int32_t
debug_radix_alloc_rev(void *info, int32_t blk, int32_t radix,
		      int32_t count, int32_t atblk, int32_t *fullp)
{
	hammer_alist_t layer = layers[blk / layer_radix];
	int32_t r;

	r = hammer_alist_alloc_rev(layer, count, atblk - blk);
	*fullp = hammer_alist_isfull(layer);
	if (r != HAMMER_ALIST_BLOCK_NONE)
		r += blk;
	return(r);
}

static
void
debug_radix_free(void *info, int32_t blk, int32_t radix,
		 int32_t base_blk, int32_t count, int32_t *emptyp)
{
	int layer_no = blk / layer_radix;
	hammer_alist_t layer = layers[layer_no];

	KKASSERT(layer);
	hammer_alist_free(layer, base_blk, count);
	*emptyp = hammer_alist_isempty(layer);
}

static
void
debug_radix_print(void *info, int32_t blk, int32_t radix, int tab)
{
	hammer_alist_t layer = layers[blk / layer_radix];

	hammer_alist_print(layer, tab);
}

int
main(int ac, char **av)
{
	int32_t size = -1;
	int i;
	hammer_alist_t live;
	hammer_almeta_t meta = NULL;

	for (i = 1; i < ac; ++i) {
		const char *ptr = av[i];
		if (*ptr != '-') {
			if (size == -1)
				size = strtol(ptr, NULL, 0);
			else if (layer_radix == -1)
				layer_radix = strtol(ptr, NULL, 0);
			else
				;
			continue;
		}
		ptr += 2;
		fprintf(stderr, "Bad option: %s\n", ptr - 2);
		exit(1);
	}
	if (size == -1)
		size = 1024;
	if (layer_radix == -1)
		layer_radix = 1;	/* no second storage layer */
	if ((size ^ (size - 1)) != (size << 1) - 1) {
		fprintf(stderr, "size must be a power of 2\n");
		exit(1);
	}
	if ((layer_radix ^ (layer_radix - 1)) != (layer_radix << 1) - 1) {
		fprintf(stderr, "the second layer radix must be a power of 2\n");
		exit(1);
	}

	live = hammer_alist_create(size, layer_radix, NULL,
				   HAMMER_ASTATE_ALLOC);
	layers = calloc(size, sizeof(hammer_alist_t));

	printf("A-LIST TEST %d blocks, first-layer radix %d, "
	       "second-layer radix %d\n",
		size, live->config->bl_radix / layer_radix, layer_radix);

	live->config->bl_radix_init = debug_radix_init;
	live->config->bl_radix_recover = debug_radix_recover;
	live->config->bl_radix_find = debug_radix_find;
	live->config->bl_radix_destroy = debug_radix_destroy;
	live->config->bl_radix_alloc_fwd = debug_radix_alloc_fwd;
	live->config->bl_radix_alloc_rev = debug_radix_alloc_rev;
	live->config->bl_radix_free = debug_radix_free;
	live->config->bl_radix_print = debug_radix_print;

	hammer_alist_free(live, 0, size);

	for (;;) {
		char buf[1024];
		int32_t da = 0;
		int32_t count = 0;
		int32_t atblk;
		int32_t blk;

		kprintf("%d/%d> ",
			live->meta->bm_alist_freeblks, size);
		fflush(stdout);
		if (fgets(buf, sizeof(buf), stdin) == NULL)
			break;
		switch(buf[0]) {
		case 'p':
			hammer_alist_print(live, 0);
			atblk = 0;
			kprintf("allocated: ");
			while ((atblk = hammer_alist_find(live, atblk, size)) != HAMMER_ALIST_BLOCK_NONE) {
				kprintf(" %d", atblk);
				++atblk;
			}
			kprintf("\n");
			break;
		case 'a':
			atblk = 0;
			if (sscanf(buf + 1, "%d %d", &count, &atblk) >= 1) {
				blk = hammer_alist_alloc_fwd(live, count, atblk);
				kprintf("    R=%04x\n", blk);
			} else {
				kprintf("?\n");
			}
			break;
		case 'r':
			atblk = HAMMER_ALIST_BLOCK_MAX;
			if (sscanf(buf + 1, "%d %d", &count, &atblk) >= 1) {
				blk = hammer_alist_alloc_rev(live, count, atblk);
				kprintf("    R=%04x\n", blk);
			} else {
				kprintf("?\n");
			}
			break;
		case 'f':
			if (sscanf(buf + 1, "%x %d", &da, &count) == 2) {
				hammer_alist_free(live, da, count);
				if (hammer_alist_isempty(live))
					kprintf("a-list is now 100%% empty\n");
			} else {
				kprintf("?\n");
			}
			break;
		case 'R':
			{
				int n;

				n = hammer_alist_recover(live, 0, 0,
					   live->meta->bm_alist_base_freeblks);
				if (n < 0)
					kprintf("recover: error %d\n", -n);
				else
					kprintf("recover: %d free\n", n);
			}
			break;
		case '?':
		case 'h':
			puts(
			    "p          -print\n"
			    "a %d       -allocate\n"
			    "r %d       -allocate reverse\n"
			    "f %x %d    -free\n"
			    "R		-recovery a-list\n"
			    "h/?        -help"
			);
			break;
		default:
			kprintf("?\n");
			break;
		}
	}
	return(0);
}

void
panic(const char *ctl, ...)
{
	__va_list va;

	__va_start(va, ctl);
	vfprintf(stderr, ctl, va);
	fprintf(stderr, "\n");
	__va_end(va);
	exit(1);
}

#endif

