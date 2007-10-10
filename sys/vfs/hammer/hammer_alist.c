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
 * $DragonFly: src/sys/vfs/hammer/Attic/hammer_alist.c,v 1.1 2007/10/10 19:37:25 dillon Exp $
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

#include "hammerfs.h"

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

#include "hammerfs.h"

void panic(const char *ctl, ...);

#endif

/*
 * static support functions
 */

static int32_t hammer_alst_leaf_alloc_fwd(hammer_almeta_t *scan,
					int32_t blk, int count);
static int32_t hammer_alst_meta_alloc_fwd(hammer_almeta_t *scan,
					int32_t blk, int32_t count,
					int32_t radix, int skip);
static int32_t hammer_alst_leaf_alloc_rev(hammer_almeta_t *scan,
					int32_t blk, int count);
static int32_t hammer_alst_meta_alloc_rev(hammer_almeta_t *scan,
					int32_t blk, int32_t count,
					int32_t radix, int skip);
static void hammer_alst_leaf_free(hammer_almeta_t *scan,
					int32_t relblk, int count);
static void hammer_alst_meta_free(hammer_almeta_t *scan,
					int32_t freeBlk, int32_t count, 
					int32_t radix, int skip, int32_t blk);
static int32_t	hammer_alst_radix_init(hammer_almeta_t *scan,
					int32_t radix, int skip, int32_t count);
#ifdef ALIST_DEBUG
static void	hammer_alst_radix_print(hammer_almeta_t *scan,
					int32_t blk,
					int32_t radix, int skip, int tab);
#endif

/*
 * Initialize an alist for use.  The alist will initially be marked
 * all-allocated so the caller must free the portion it wishes to manage.
 */
void
hammer_alist_template(hammer_alist_t bl, int blocks, int maxmeta)
{
	int radix;
	int skip = 0;

	/*
	 * Calculate radix and skip field used for scanning.
	 */
	radix = HAMMER_ALIST_BMAP_RADIX;

	while (radix < blocks) {
		radix *= HAMMER_ALIST_META_RADIX;
		skip = (skip + 1) * HAMMER_ALIST_META_RADIX;
	}

	bzero(bl, sizeof(*bl));

	bl->bl_blocks = blocks;
	bl->bl_radix = radix;
	bl->bl_skip = skip;
	bl->bl_rootblks = 1 +
	    hammer_alst_radix_init(NULL, bl->bl_radix, bl->bl_skip, blocks);
	KKASSERT(bl->bl_rootblks <= maxmeta);

#if defined(ALIST_DEBUG)
	kprintf(
		"ALIST representing %d blocks (%d MB of swap)"
		", requiring %dK (%d bytes) of ram\n",
		bl->bl_blocks,
		bl->bl_blocks * 4 / 1024,
		(bl->bl_rootblks * sizeof(hammer_almeta_t) + 1023) / 1024,
		(bl->bl_rootblks * sizeof(hammer_almeta_t))
	);
	kprintf("ALIST raw radix tree contains %d records\n", bl->bl_rootblks);
#endif
}

void
hammer_alist_init(hammer_alist_t bl, hammer_almeta_t *meta)
{
	hammer_alst_radix_init(meta, bl->bl_radix, bl->bl_skip, bl->bl_blocks);
}

#if !defined(_KERNEL) && defined(ALIST_DEBUG)

/*
 * hammer_alist_create()	(userland only)
 *
 *	create a alist capable of handling up to the specified number of
 *	blocks.  blocks must be greater then 0
 *
 *	The smallest alist consists of a single leaf node capable of 
 *	managing HAMMER_ALIST_BMAP_RADIX blocks.
 */

hammer_alist_t 
hammer_alist_create(int32_t blocks, struct malloc_type *mtype,
		    hammer_almeta_t **metap)
{
	hammer_alist_t bl;
	hammer_almeta_t *meta;
	int radix;
	int skip = 0;
	int rootblks;
	size_t metasize;

	/*
	 * Calculate radix and skip field used for scanning.
	 */
	radix = HAMMER_ALIST_BMAP_RADIX;

	while (radix < blocks) {
		radix *= HAMMER_ALIST_META_RADIX;
		skip = (skip + 1) * HAMMER_ALIST_META_RADIX;
	}

	rootblks = 1 + hammer_alst_radix_init(NULL, radix, skip, blocks);
	metasize = sizeof(struct hammer_almeta) * rootblks;
	bl = kmalloc(sizeof(struct hammer_alist), mtype, M_WAITOK);
	meta = kmalloc(metasize, mtype, M_WAITOK);

	bzero(bl, sizeof(*bl));
	bzero(meta, metasize);

	bl->bl_blocks = blocks;
	bl->bl_radix = radix;
	bl->bl_skip = skip;
	bl->bl_rootblks = rootblks;

#if defined(ALIST_DEBUG)
	kprintf(
		"ALIST representing %d blocks (%d MB of swap)"
		", requiring %dK (%d bytes) of ram\n",
		bl->bl_blocks,
		bl->bl_blocks * 4 / 1024,
		(bl->bl_rootblks * sizeof(hammer_almeta_t) + 1023) / 1024,
		(bl->bl_rootblks * sizeof(hammer_almeta_t))
	);
	kprintf("ALIST raw radix tree contains %d records\n", bl->bl_rootblks);
#endif
	hammer_alst_radix_init(meta, bl->bl_radix, bl->bl_skip, blocks);

	*metap = meta;
	return(bl);
}

void
hammer_alist_destroy(hammer_alist_t bl, struct malloc_type *mtype)
{
	kfree(bl, mtype);
}

#endif

/*
 * hammer_alist_alloc()
 *
 *	Reserve space in the block bitmap.  Return the base of a contiguous
 *	region or HAMMER_ALIST_BLOCK_NONE if space could not be allocated.
 */

int32_t 
hammer_alist_alloc(hammer_alist_t bl, hammer_almeta_t *meta, int32_t count)
{
	int32_t blk = HAMMER_ALIST_BLOCK_NONE;

	KKASSERT((count | (count - 1)) == (count << 1) - 1);

	if (bl && count < bl->bl_radix) {
		if (bl->bl_radix == HAMMER_ALIST_BMAP_RADIX) {
			blk = hammer_alst_leaf_alloc_fwd(meta, 0, count);
		} else {
			blk = hammer_alst_meta_alloc_fwd(
				    meta, 0, count, bl->bl_radix, bl->bl_skip);
		}
		if (blk != HAMMER_ALIST_BLOCK_NONE)
			bl->bl_free -= count;
	}
	return(blk);
}

int32_t 
hammer_alist_alloc_rev(hammer_alist_t bl, hammer_almeta_t *meta, int32_t count)
{
	int32_t blk = HAMMER_ALIST_BLOCK_NONE;

	KKASSERT((count | (count - 1)) == (count << 1) - 1);

	if (bl && count < bl->bl_radix) {
		if (bl->bl_radix == HAMMER_ALIST_BMAP_RADIX) {
			blk = hammer_alst_leaf_alloc_rev(meta, 0, count);
		} else {
			blk = hammer_alst_meta_alloc_rev(
				    meta, 0, count, bl->bl_radix, bl->bl_skip);
		}
		if (blk != HAMMER_ALIST_BLOCK_NONE)
			bl->bl_free -= count;
	}
	return(blk);
}

#if 0

/*
 * hammer_alist_alloc_from()
 *
 *	An extended version of hammer_alist_alloc() which locates free space
 *	starting at the specified block either forwards or backwards.
 *	HAMMER_ALIST_BLOCK_NONE is returned if space could not be allocated.
 *
 *	Note: when allocating from a particular point forwards space is never
 *	allocated behind that start point, and similarly when going backwards.
 */
int32_t 
hammer_alist_alloc_from(hammer_alist_t bl, hammer_almeta_t *meta,
			int32_t count, int32_t start, int flags)

{
}

#endif

/*
 * alist_free()
 *
 *	Free up space in the block bitmap.  Return the base of a contiguous
 *	region.  Panic if an inconsistancy is found.
 *
 *	Unlike allocations, there are no alignment requirements for blkno or
 *	count when freeing blocks.
 */

void 
hammer_alist_free(hammer_alist_t bl, hammer_almeta_t *meta,
		  int32_t blkno, int32_t count)
{
	if (bl) {
		KKASSERT(blkno + count <= bl->bl_blocks);
		if (bl->bl_radix == HAMMER_ALIST_BMAP_RADIX)
			hammer_alst_leaf_free(meta, blkno, count);
		else
			hammer_alst_meta_free(meta, blkno, count,
					      bl->bl_radix, bl->bl_skip, 0);
		bl->bl_free += count;
	}
}

#ifdef ALIST_DEBUG

/*
 * alist_print()    - dump radix tree
 */

void
hammer_alist_print(hammer_alist_t bl, hammer_almeta_t *meta)
{
	kprintf("ALIST {\n");
	hammer_alst_radix_print(meta, 0, bl->bl_radix, bl->bl_skip, 4);
	kprintf("}\n");
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
hammer_alst_leaf_alloc_fwd(hammer_almeta_t *scan, int32_t blk, int count)
{
	u_int32_t orig = scan->bm_bitmap;

	/*
	 * Optimize bitmap all-allocated case.  Also, count = 1
	 * case assumes at least 1 bit is free in the bitmap, so
	 * we have to take care of this case here.
	 */
	if (orig == 0) {
		scan->bm_bighint = 0;
		return(HAMMER_ALIST_BLOCK_NONE);
	}

	/*
	 * Optimized code to allocate one bit out of the bitmap
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
			if ((orig & mask) == mask) {
				scan->bm_bitmap &= ~mask;
				return(blk + j);
			}
			mask = mask << count;
		}
	}

	/*
	 * We couldn't allocate count in this subtree, update bighint.
	 */
	scan->bm_bighint = count - 1;
	return(HAMMER_ALIST_BLOCK_NONE);
}

/*
 * This version allocates blocks in the reverse direction.
 */
static int32_t
hammer_alst_leaf_alloc_rev(hammer_almeta_t *scan, int32_t blk, int count)
{
	u_int32_t orig = scan->bm_bitmap;

	/*
	 * Optimize bitmap all-allocated case.  Also, count = 1
	 * case assumes at least 1 bit is free in the bitmap, so
	 * we have to take care of this case here.
	 */
	if (orig == 0) {
		scan->bm_bighint = 0;
		return(HAMMER_ALIST_BLOCK_NONE);
	}

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

		for (j = n; j >= 0; j -= count) {
			if ((orig & mask) == mask) {
				scan->bm_bitmap &= ~mask;
				return(blk + j);
			}
			mask = mask >> count;
		}
	}

	/*
	 * We couldn't allocate count in this subtree, update bighint.
	 */
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
hammer_alst_meta_alloc_fwd(hammer_almeta_t *scan, int32_t blk, int32_t count,
			   int32_t radix, int skip
) {
	int i;
	u_int32_t mask;
	u_int32_t pmask;
	int next_skip = ((u_int)skip / HAMMER_ALIST_META_RADIX);

	/*
	 * ALL-ALLOCATED special case
	 */
	if (scan->bm_bitmap == 0)  {
		scan->bm_bighint = 0;
		return(HAMMER_ALIST_BLOCK_NONE);
	}

	radix /= HAMMER_ALIST_META_RADIX;

	/*
	 * Radix now represents each bitmap entry for this meta node.  If
	 * the number of blocks being allocated can be fully represented,
	 * we allocate directly out of this meta node.
	 *
	 * Meta node bitmaps use 2 bits per block.
	 *
	 *	00	ALL-ALLOCATED
	 *	01	PARTIALLY-FREE/PARTIALLY-ALLOCATED
	 *	10	(RESERVED)
	 *	11	ALL-FREE
	 */
	if (count >= radix) {
		int n = count / radix * 2;	/* number of bits */
		int j;

		mask = (u_int32_t)-1 >> (HAMMER_ALIST_BMAP_RADIX - n);
		for (j = 0; j < HAMMER_ALIST_META_RADIX; j += n / 2) {
			if ((scan->bm_bitmap & mask) == mask) {
				scan->bm_bitmap &= ~mask;
				return(blk + j * radix);
			}
			mask <<= n;
		}
		if (scan->bm_bighint >= count)
			scan->bm_bighint = count >> 1;
		return(HAMMER_ALIST_BLOCK_NONE);
	}

	/*
	 * If not we have to recurse.
	 */
	mask = 0x00000003;
	pmask = 0x00000001;
	for (i = 1; i <= skip; i += next_skip) {
		if (scan[i].bm_bighint == (int32_t)-1) {
			/* 
			 * Terminator
			 */
			break;
		}
		if ((scan->bm_bitmap & mask) == mask) {
			scan[i].bm_bitmap = (u_int32_t)-1;
			scan[i].bm_bighint = radix;
		}

		if (count <= scan[i].bm_bighint) {
			/*
			 * count fits in object
			 */
			int32_t r;
			if (next_skip == 1) {
				r = hammer_alst_leaf_alloc_fwd(
					&scan[i], blk, count);
			} else {
				r = hammer_alst_meta_alloc_fwd(
					&scan[i], blk, count,
					radix, next_skip - 1);
			}
			if (r != HAMMER_ALIST_BLOCK_NONE) {
				if (scan[i].bm_bitmap == 0) {
					scan->bm_bitmap &= ~mask;
				} else {
					scan->bm_bitmap &= ~mask;
					scan->bm_bitmap |= pmask;
				}
				return(r);
			}
		} else if (count > radix) {
			/*
			 * count does not fit in object even if it were
			 * completely free.
			 */
			break;
		}
		blk += radix;
		mask <<= 2;
		pmask <<= 2;
	}

	/*
	 * We couldn't allocate count in this subtree, update bighint.
	 */
	if (scan->bm_bighint >= count)
		scan->bm_bighint = count >> 1;
	return(HAMMER_ALIST_BLOCK_NONE);
}

/*
 * This version allocates blocks in the reverse direction.
 */
static int32_t
hammer_alst_meta_alloc_rev(hammer_almeta_t *scan, int32_t blk, int32_t count,
			   int32_t radix, int skip
) {
	int i;
	int j;
	u_int32_t mask;
	u_int32_t pmask;
	int next_skip = ((u_int)skip / HAMMER_ALIST_META_RADIX);

	/*
	 * ALL-ALLOCATED special case
	 */
	if (scan->bm_bitmap == 0)  {
		scan->bm_bighint = 0;
		return(HAMMER_ALIST_BLOCK_NONE);
	}

	radix /= HAMMER_ALIST_META_RADIX;

	/*
	 * Radix now represents each bitmap entry for this meta node.  If
	 * the number of blocks being allocated can be fully represented,
	 * we allocate directly out of this meta node.
	 *
	 * Meta node bitmaps use 2 bits per block.
	 *
	 *	00	ALL-ALLOCATED
	 *	01	PARTIALLY-FREE/PARTIALLY-ALLOCATED
	 *	10	(RESERVED)
	 *	11	ALL-FREE
	 */
	if (count >= radix) {
		int n = count / radix * 2;	/* number of bits */
		int j;

		/*
		 * Initial mask if e.g. n == 2:  1100....0000
		 */
		mask = (u_int32_t)-1 >> (HAMMER_ALIST_BMAP_RADIX - n) <<
			(HAMMER_ALIST_BMAP_RADIX - n);
		for (j = HAMMER_ALIST_META_RADIX - n / 2; j >= 0; j -= n / 2) {
			if ((scan->bm_bitmap & mask) == mask) {
				scan->bm_bitmap &= ~mask;
				return(blk + j * radix);
			}
			mask >>= n;
		}
		if (scan->bm_bighint >= count)
			scan->bm_bighint = count >> 1;
		return(HAMMER_ALIST_BLOCK_NONE);
	}

	/*
	 * If not we have to recurse.  Since we are going in the reverse
	 * direction we need an extra loop to determine if there is a
	 * terminator, then run backwards.
	 *
	 * This is a little weird but we do not want to overflow the
	 * mask/pmask in the loop.
	 */
	j = 0;
	for (i = 1; i <= skip; i += next_skip) {
		if (scan[i].bm_bighint == (int32_t)-1)
			break;
		blk += radix;
		j += 2;
	}
	blk -= radix;
	j -= 2;
	mask = 0x00000003 << j;
	pmask = 0x00000001 << j;
	i -= next_skip;

	while (i >= 1) {
		/*
		 * Initialize the bitmap in the child if allocating from
		 * the all-free case.
		 */
		if ((scan->bm_bitmap & mask) == mask) {
			scan[i].bm_bitmap = (u_int32_t)-1;
			scan[i].bm_bighint = radix;
		}

		/*
		 * Handle various count cases.  Bighint may be too large but
		 * is never too small.
		 */
		if (count <= scan[i].bm_bighint) {
			/*
			 * count fits in object
			 */
			int32_t r;
			if (next_skip == 1) {
				r = hammer_alst_leaf_alloc_rev(
					&scan[i], blk, count);
			} else {
				r = hammer_alst_meta_alloc_rev(
					&scan[i], blk, count,
					radix, next_skip - 1);
			}
			if (r != HAMMER_ALIST_BLOCK_NONE) {
				if (scan[i].bm_bitmap == 0) {
					scan->bm_bitmap &= ~mask;
				} else {
					scan->bm_bitmap &= ~mask;
					scan->bm_bitmap |= pmask;
				}
				return(r);
			}
		} else if (count > radix) {
			/*
			 * count does not fit in object even if it were
			 * completely free.
			 */
			break;
		}
		blk -= radix;
		mask >>= 2;
		pmask >>= 2;
		i -= next_skip;
	}

	/*
	 * We couldn't allocate count in this subtree, update bighint.
	 */
	if (scan->bm_bighint >= count)
		scan->bm_bighint = count >> 1;
	return(HAMMER_ALIST_BLOCK_NONE);
}

/*
 * BLST_LEAF_FREE()
 *
 *	Free allocated blocks from leaf bitmap.  The allocation code is
 *	restricted to powers of 2, the freeing code is not.
 */
static void
hammer_alst_leaf_free(
	hammer_almeta_t *scan,
	int32_t blk,
	int count
) {
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
hammer_alst_meta_free(
	hammer_almeta_t *scan, 
	int32_t freeBlk,
	int32_t count,
	int32_t radix, 
	int skip,
	int32_t blk
) {
	int next_skip = ((u_int)skip / HAMMER_ALIST_META_RADIX);
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

	i = (freeBlk - blk) / radix;
	blk += i * radix;
	mask = 0x00000003 << (i * 2);
	pmask = 0x00000001 << (i * 2);

	i = i * next_skip + 1;

	while (i <= skip && blk < freeBlk + count) {
		int32_t v;

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
			/*XXX*/
		} else {
			/*
			 * Recursion case
			 */
			if (next_skip == 1)
				hammer_alst_leaf_free(&scan[i], freeBlk, v);
			else
				hammer_alst_meta_free(&scan[i], freeBlk, v, radix, next_skip - 1, blk);
			if (scan[i].bm_bitmap == (u_int32_t)-1)
				scan->bm_bitmap |= mask;
			else
				scan->bm_bitmap |= pmask;
			if (scan->bm_bighint < scan[i].bm_bighint)
			    scan->bm_bighint = scan[i].bm_bighint;
		}
		mask <<= 2;
		pmask <<= 2;
		count -= v;
		freeBlk += v;
		blk += radix;
		i += next_skip;
	}
}

/*
 * BLST_RADIX_INIT()
 *
 *	Initialize our meta structures and bitmaps and calculate the exact
 *	amount of space required to manage 'count' blocks - this space may
 *	be considerably less then the calculated radix due to the large
 *	RADIX values we use.
 */

static int32_t	
hammer_alst_radix_init(hammer_almeta_t *scan, int32_t radix,
		       int skip, int32_t count)
{
	int i;
	int next_skip;
	int32_t memindex = 0;
	u_int32_t mask;
	u_int32_t pmask;

	/*
	 * Leaf node
	 */
	if (radix == HAMMER_ALIST_BMAP_RADIX) {
		if (scan) {
			scan->bm_bighint = 0;
			scan->bm_bitmap = 0;
		}
		return(memindex);
	}

	/*
	 * Meta node.  If allocating the entire object we can special
	 * case it.  However, we need to figure out how much memory
	 * is required to manage 'count' blocks, so we continue on anyway.
	 */

	if (scan) {
		scan->bm_bighint = 0;
		scan->bm_bitmap = 0;
	}

	radix /= HAMMER_ALIST_META_RADIX;
	next_skip = ((u_int)skip / HAMMER_ALIST_META_RADIX);
	mask = 0x00000003;
	pmask = 0x00000001;

	for (i = 1; i <= skip; i += next_skip) {
		if (count >= radix) {
			/*
			 * Allocate the entire object
			 */
			memindex = i + hammer_alst_radix_init(
			    ((scan) ? &scan[i] : NULL),
			    radix,
			    next_skip - 1,
			    radix
			);
			count -= radix;
			/* already marked as wholely allocated */
		} else if (count > 0) {
			/*
			 * Allocate a partial object
			 */
			memindex = i + hammer_alst_radix_init(
			    ((scan) ? &scan[i] : NULL),
			    radix,
			    next_skip - 1,
			    count
			);
			count = 0;

			/*
			 * Mark as partially allocated
			 */
			if (scan)
				scan->bm_bitmap |= pmask;
		} else {
			/*
			 * Add terminator and break out
			 */
			if (scan)
				scan[i].bm_bighint = (int32_t)-1;
			/* already marked as wholely allocated */
			break;
		}
		mask <<= 2;
		pmask <<= 2;
	}
	if (memindex < i)
		memindex = i;
	return(memindex);
}

#ifdef ALIST_DEBUG

static void	
hammer_alst_radix_print(hammer_almeta_t *scan, int32_t blk,
			int32_t radix, int skip, int tab)
{
	int i;
	int next_skip;
	int lastState = 0;
	u_int32_t mask;

	if (radix == HAMMER_ALIST_BMAP_RADIX) {
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
	    "%*.*s(%04x,%d): subtree (%d) bitmap=%08x big=%d {\n",
	    tab, tab, "",
	    blk, radix,
	    radix,
	    scan->bm_bitmap,
	    scan->bm_bighint
	);

	radix /= HAMMER_ALIST_META_RADIX;
	next_skip = ((u_int)skip / HAMMER_ALIST_META_RADIX);
	tab += 4;
	mask = 0x00000003;

	for (i = 1; i <= skip; i += next_skip) {
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
			    &scan[i],
			    blk,
			    radix,
			    next_skip - 1,
			    tab
			);
		}
		blk += radix;
		mask <<= 2;
	}
	tab -= 4;

	kprintf(
	    "%*.*s}\n",
	    tab, tab, ""
	);
}

#endif

#ifdef ALIST_DEBUG

int
main(int ac, char **av)
{
	int size = 1024;
	int i;
	hammer_alist_t bl;
	hammer_almeta_t *meta = NULL;

	for (i = 1; i < ac; ++i) {
		const char *ptr = av[i];
		if (*ptr != '-') {
			size = strtol(ptr, NULL, 0);
			continue;
		}
		ptr += 2;
		fprintf(stderr, "Bad option: %s\n", ptr - 2);
		exit(1);
	}
	bl = hammer_alist_create(size, NULL, &meta);
	hammer_alist_free(bl, meta, 0, size);

	for (;;) {
		char buf[1024];
		int32_t da = 0;
		int32_t count = 0;
		int32_t blk;

		kprintf("%d/%d/%d> ", bl->bl_free, size, bl->bl_radix);
		fflush(stdout);
		if (fgets(buf, sizeof(buf), stdin) == NULL)
			break;
		switch(buf[0]) {
		case 'p':
			hammer_alist_print(bl, meta);
			break;
		case 'a':
			if (sscanf(buf + 1, "%d", &count) == 1) {
				blk = hammer_alist_alloc(bl, meta, count);
				kprintf("    R=%04x\n", blk);
			} else {
				kprintf("?\n");
			}
			break;
		case 'r':
			if (sscanf(buf + 1, "%d", &count) == 1) {
				blk = hammer_alist_alloc_rev(bl, meta, count);
				kprintf("    R=%04x\n", blk);
			} else {
				kprintf("?\n");
			}
			break;
		case 'f':
			if (sscanf(buf + 1, "%x %d", &da, &count) == 2) {
				hammer_alist_free(bl, meta, da, count);
			} else {
				kprintf("?\n");
			}
			break;
		case '?':
		case 'h':
			puts(
			    "p          -print\n"
			    "a %d       -allocate\n"
			    "r %d       -allocate reverse\n"
			    "f %x %d    -free\n"
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

