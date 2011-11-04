/*
 * ALIST.C -	Bitmap allocator/deallocator, using a radix tree with hinting.
 *		Unlimited-size allocations, power-of-2 only, power-of-2
 *		aligned results only.
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
 */
/*
 * This module has been adapted from the BLIST module, which was written
 * by Matthew Dillon many years ago.
 *
 * This module implements a general power-of-2 bitmap allocator/deallocator.
 * All allocations must be in powers of 2 and will return similarly aligned
 * results.  The module does not try to interpret the meaning of a 'block'
 * other then to return ALIST_BLOCK_NONE on an allocation failure.  
 *
 * A maximum of 2 billion blocks is supported so, for example, if one block
 * represented 64 bytes a maximally sized ALIST would represent
 * 128 gigabytes.
 *
 * A radix tree is used to maintain the bitmap and layed out in a manner
 * similar to the blist code.  Meta nodes use a radix of 16 and 2 bits per
 * block while leaf nodes use a radix of 32 and 1 bit per block (stored in
 * a 32 bit bitmap field).  Both meta and leaf nodes have a hint field.
 * This field gives us a hint as to the largest free contiguous range of
 * blocks under the node.  It may contain a value that is too high, but
 * will never contain a value that is too low.  When the radix tree is
 * searched, allocation failures in subtrees update the hint. 
 *
 * The radix tree is layed out recursively using a linear array.  Each meta
 * node is immediately followed (layed out sequentially in memory) by
 * ALIST_META_RADIX lower level nodes.  This is a recursive structure but one
 * that can be easily scanned through a very simple 'skip' calculation.  In
 * order to support large radixes, portions of the tree may reside outside our
 * memory allocation.  We handle this with an early-terminate optimization
 * in the meta-node.  The memory allocation is only large enough to cover
 * the number of blocks requested at creation time even if it must be
 * encompassed in larger root-node radix.
 *
 * This code can be compiled stand-alone for debugging.
 */

#ifdef _KERNEL

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/lock.h>
#include <sys/kernel.h>
#include <sys/alist.h>
#include <sys/malloc.h>
#include <vm/vm.h>
#include <vm/vm_object.h>
#include <vm/vm_kern.h>
#include <vm/vm_extern.h>
#include <vm/vm_page.h>

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

typedef unsigned int u_daddr_t;

#include <sys/alist.h>

void panic(const char *ctl, ...);

#endif

/*
 * static support functions
 */

static daddr_t alst_leaf_alloc(almeta_t *scan, daddr_t blk, int count);
static daddr_t alst_meta_alloc(almeta_t *scan, daddr_t blk, 
				daddr_t count, daddr_t radix, int skip);
static void alst_leaf_free(almeta_t *scan, daddr_t relblk, int count);
static void alst_meta_free(almeta_t *scan, daddr_t freeBlk, daddr_t count, 
					daddr_t radix, int skip, daddr_t blk);
static daddr_t	alst_radix_init(almeta_t *scan, daddr_t radix, 
						int skip, daddr_t count);
#ifndef _KERNEL
static void	alst_radix_print(almeta_t *scan, daddr_t blk, 
					daddr_t radix, int skip, int tab);
#endif

/*
 * alist_create() - create a alist capable of handling up to the specified
 *		    number of blocks
 *
 *	blocks must be greater then 0
 *
 *	The smallest alist consists of a single leaf node capable of 
 *	managing ALIST_BMAP_RADIX blocks.
 */

alist_t 
alist_create(daddr_t blocks, struct malloc_type *mtype)
{
	alist_t bl;
	int radix;
	int skip = 0;

	/*
	 * Calculate radix and skip field used for scanning.
	 */
	radix = ALIST_BMAP_RADIX;

	while (radix < blocks) {
		radix *= ALIST_META_RADIX;
		skip = (skip + 1) * ALIST_META_RADIX;
	}

	bl = kmalloc(sizeof(struct alist), mtype, M_WAITOK | M_ZERO);

	bl->bl_blocks = blocks;
	bl->bl_radix = radix;
	bl->bl_skip = skip;
	bl->bl_rootblks = 1 +
	    alst_radix_init(NULL, bl->bl_radix, bl->bl_skip, blocks);
	bl->bl_root = kmalloc(sizeof(almeta_t) * bl->bl_rootblks, mtype, M_WAITOK);

#if defined(ALIST_DEBUG)
	kprintf(
		"ALIST representing %d blocks (%d MB of swap)"
		", requiring %dK (%d bytes) of ram\n",
		bl->bl_blocks,
		bl->bl_blocks * 4 / 1024,
		(bl->bl_rootblks * sizeof(almeta_t) + 1023) / 1024,
		(bl->bl_rootblks * sizeof(almeta_t))
	);
	kprintf("ALIST raw radix tree contains %d records\n", bl->bl_rootblks);
#endif
	alst_radix_init(bl->bl_root, bl->bl_radix, bl->bl_skip, blocks);

	return(bl);
}

void 
alist_destroy(alist_t bl, struct malloc_type *mtype)
{
	kfree(bl->bl_root, mtype);
	kfree(bl, mtype);
}

/*
 * alist_alloc() - reserve space in the block bitmap.  Return the base
 *		   of a contiguous region or ALIST_BLOCK_NONE if space
 *		   could not be allocated.
 */

daddr_t 
alist_alloc(alist_t bl, daddr_t count)
{
	daddr_t blk = ALIST_BLOCK_NONE;

	KKASSERT((count | (count - 1)) == (count << 1) - 1);

	if (bl && count < bl->bl_radix) {
		if (bl->bl_radix == ALIST_BMAP_RADIX)
			blk = alst_leaf_alloc(bl->bl_root, 0, count);
		else
			blk = alst_meta_alloc(bl->bl_root, 0, count, bl->bl_radix, bl->bl_skip);
		if (blk != ALIST_BLOCK_NONE)
			bl->bl_free -= count;
	}
	return(blk);
}

/*
 * alist_free() -	free up space in the block bitmap.  Return the base
 *		     	of a contiguous region.  Panic if an inconsistancy is
 *			found.
 */

void 
alist_free(alist_t bl, daddr_t blkno, daddr_t count)
{
	if (bl) {
		KKASSERT(blkno + count <= bl->bl_blocks);
		if (bl->bl_radix == ALIST_BMAP_RADIX)
			alst_leaf_free(bl->bl_root, blkno, count);
		else
			alst_meta_free(bl->bl_root, blkno, count, bl->bl_radix, bl->bl_skip, 0);
		bl->bl_free += count;
	}
}

#ifdef ALIST_DEBUG

/*
 * alist_print()    - dump radix tree
 */

void
alist_print(alist_t bl)
{
	kprintf("ALIST {\n");
	alst_radix_print(bl->bl_root, 0, bl->bl_radix, bl->bl_skip, 4);
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
 * alist_leaf_alloc() -	allocate at a leaf in the radix tree (a bitmap).
 *
 *	This is the core of the allocator and is optimized for the 1 block
 *	and the ALIST_BMAP_RADIX block allocation cases.  Other cases are
 *	somewhat slower.  The 1 block allocation case is log2 and extremely
 *	quick.
 */

static daddr_t
alst_leaf_alloc(
	almeta_t *scan,
	daddr_t blk,
	int count
) {
	u_daddr_t orig = scan->bm_bitmap;

	/*
	 * Optimize bitmap all-allocated case.  Also, count = 1
	 * case assumes at least 1 bit is free in the bitmap, so
	 * we have to take care of this case here.
	 */
	if (orig == 0) {
		scan->bm_bighint = 0;
		return(ALIST_BLOCK_NONE);
	}

	/*
	 * Optimized code to allocate one bit out of the bitmap
	 */
	if (count == 1) {
		u_daddr_t mask;
		int j = ALIST_BMAP_RADIX/2;
		int r = 0;

		mask = (u_daddr_t)-1 >> (ALIST_BMAP_RADIX/2);

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
		int n = ALIST_BMAP_RADIX - count;
		u_daddr_t mask;

		mask = (u_daddr_t)-1 >> n;

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
	return(ALIST_BLOCK_NONE);
}

/*
 * alist_meta_alloc() -	allocate at a meta in the radix tree.
 *
 *	Attempt to allocate at a meta node.  If we can't, we update
 *	bighint and return a failure.  Updating bighint optimize future
 *	calls that hit this node.  We have to check for our collapse cases
 *	and we have a few optimizations strewn in as well.
 */

static daddr_t
alst_meta_alloc(
	almeta_t *scan, 
	daddr_t blk,
	daddr_t count,
	daddr_t radix, 
	int skip
) {
	int i;
	u_daddr_t mask;
	u_daddr_t pmask;
	int next_skip = ((u_int)skip / ALIST_META_RADIX);

	/*
	 * ALL-ALLOCATED special case
	 */
	if (scan->bm_bitmap == 0)  {
		scan->bm_bighint = 0;
		return(ALIST_BLOCK_NONE);
	}

	radix /= ALIST_META_RADIX;

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

		mask = (u_daddr_t)-1 >> (ALIST_BMAP_RADIX - n);
		for (j = 0; j < ALIST_META_RADIX; j += n / 2) {
			if ((scan->bm_bitmap & mask) == mask) {
				scan->bm_bitmap &= ~mask;
				return(blk + j * radix);
			}
			mask <<= n;
		}
		if (scan->bm_bighint >= count)
			scan->bm_bighint = count >> 1;
		return(ALIST_BLOCK_NONE);
	}

	/*
	 * If not we have to recurse.
	 */
	mask = 0x00000003;
	pmask = 0x00000001;
	for (i = 1; i <= skip; i += next_skip) {
		if (scan[i].bm_bighint == (daddr_t)-1) {
			/* 
			 * Terminator
			 */
			break;
		}

		/*
		 * If the element is marked completely free (11), initialize
		 * the recursion.
		 */
		if ((scan->bm_bitmap & mask) == mask) {
			scan[i].bm_bitmap = (u_daddr_t)-1;
			scan[i].bm_bighint = radix;
		} 

		if ((scan->bm_bitmap & mask) == 0) {
			/*
			 * Object marked completely allocated, recursion
			 * contains garbage.
			 */
			/* Skip it */
		} else if (count <= scan[i].bm_bighint) {
			/*
			 * count fits in object
			 */
			daddr_t r;
			if (next_skip == 1) {
				r = alst_leaf_alloc(&scan[i], blk, count);
			} else {
				r = alst_meta_alloc(&scan[i], blk, count, radix, next_skip - 1);
			}
			if (r != ALIST_BLOCK_NONE) {
				if (scan[i].bm_bitmap == 0) {
					scan->bm_bitmap &= ~mask;
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

	/*
	 * We couldn't allocate count in this subtree, update bighint.
	 */
	if (scan->bm_bighint >= count)
		scan->bm_bighint = count >> 1;
	return(ALIST_BLOCK_NONE);
}

/*
 * BLST_LEAF_FREE() -	free allocated block from leaf bitmap
 *
 */
static void
alst_leaf_free(
	almeta_t *scan,
	daddr_t blk,
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
	int n = blk & (ALIST_BMAP_RADIX - 1);
	u_daddr_t mask;

	mask = ((u_daddr_t)-1 << n) &
	    ((u_daddr_t)-1 >> (ALIST_BMAP_RADIX - count - n));

	if (scan->bm_bitmap & mask)
		panic("alst_radix_free: freeing free block");
	scan->bm_bitmap |= mask;

	/*
	 * We could probably do a better job here.  We are required to make
	 * bighint at least as large as the biggest contiguous block of 
	 * data.  If we just shoehorn it, a little extra overhead will
	 * be incured on the next allocation (but only that one typically).
	 */
	scan->bm_bighint = ALIST_BMAP_RADIX;
}

/*
 * BLST_META_FREE() - free allocated blocks from radix tree meta info
 *
 *	This support routine frees a range of blocks from the bitmap.
 *	The range must be entirely enclosed by this radix node.  If a
 *	meta node, we break the range down recursively to free blocks
 *	in subnodes (which means that this code can free an arbitrary
 *	range whereas the allocation code cannot allocate an arbitrary
 *	range).
 */

static void 
alst_meta_free(
	almeta_t *scan, 
	daddr_t freeBlk,
	daddr_t count,
	daddr_t radix, 
	int skip,
	daddr_t blk
) {
	int next_skip = ((u_int)skip / ALIST_META_RADIX);
	u_daddr_t mask;
	u_daddr_t pmask;
	int i;

	/*
	 * Break the free down into its components.  Because it is so easy
	 * to implement, frees are not limited to power-of-2 sizes.
	 *
	 * Each block in a meta-node bitmap takes two bits.
	 */
	radix /= ALIST_META_RADIX;

	i = (freeBlk - blk) / radix;
	blk += i * radix;
	mask = 0x00000003 << (i * 2);
	pmask = 0x00000001 << (i * 2);

	i = i * next_skip + 1;

	while (i <= skip && blk < freeBlk + count) {
		daddr_t v;

		v = blk + radix - freeBlk;
		if (v > count)
			v = count;

		if (scan->bm_bighint == (daddr_t)-1)
			panic("alst_meta_free: freeing unexpected range");

		if (freeBlk == blk && count >= radix) {
			/*
			 * All-free case, no need to update sub-tree
			 */
			scan->bm_bitmap |= mask;
			scan->bm_bighint = radix * ALIST_META_RADIX;/*XXX*/
		} else {
			/*
			 * If we were previously marked all-allocated, fix-up
			 * the next layer so we can recurse down into it.
			 */
			if ((scan->bm_bitmap & mask) == 0) {
				scan[i].bm_bitmap = (u_daddr_t)0;
				scan[i].bm_bighint = 0;
			} 

			/*
			 * Recursion case
			 */
			if (next_skip == 1)
				alst_leaf_free(&scan[i], freeBlk, v);
			else
				alst_meta_free(&scan[i], freeBlk, v, radix, next_skip - 1, blk);
			if (scan[i].bm_bitmap == (u_daddr_t)-1)
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
 * BLST_RADIX_INIT() - initialize radix tree
 *
 *	Initialize our meta structures and bitmaps and calculate the exact
 *	amount of space required to manage 'count' blocks - this space may
 *	be considerably less then the calculated radix due to the large
 *	RADIX values we use.
 */

static daddr_t	
alst_radix_init(almeta_t *scan, daddr_t radix, int skip, daddr_t count)
{
	int i;
	int next_skip;
	daddr_t memindex = 0;
	u_daddr_t mask;
	u_daddr_t pmask;

	/*
	 * Leaf node
	 */
	if (radix == ALIST_BMAP_RADIX) {
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

	radix /= ALIST_META_RADIX;
	next_skip = ((u_int)skip / ALIST_META_RADIX);
	mask = 0x00000003;
	pmask = 0x00000001;

	for (i = 1; i <= skip; i += next_skip) {
		if (count >= radix) {
			/*
			 * Allocate the entire object
			 */
			memindex = i + alst_radix_init(
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
			memindex = i + alst_radix_init(
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
				scan[i].bm_bighint = (daddr_t)-1;
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
alst_radix_print(almeta_t *scan, daddr_t blk, daddr_t radix, int skip, int tab)
{
	int i;
	int next_skip;
	int lastState = 0;
	u_daddr_t mask;

	if (radix == ALIST_BMAP_RADIX) {
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
	if (scan->bm_bitmap == (u_daddr_t)-1) {
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

	radix /= ALIST_META_RADIX;
	next_skip = ((u_int)skip / ALIST_META_RADIX);
	tab += 4;
	mask = 0x00000003;

	for (i = 1; i <= skip; i += next_skip) {
		if (scan[i].bm_bighint == (daddr_t)-1) {
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
			alst_radix_print(
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
	alist_t bl;

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
	bl = alist_create(size, NULL);
	alist_free(bl, 0, size);

	for (;;) {
		char buf[1024];
		daddr_t da = 0;
		daddr_t count = 0;


		kprintf("%d/%d/%d> ", bl->bl_free, size, bl->bl_radix);
		fflush(stdout);
		if (fgets(buf, sizeof(buf), stdin) == NULL)
			break;
		switch(buf[0]) {
		case 'p':
			alist_print(bl);
			break;
		case 'a':
			if (sscanf(buf + 1, "%d", &count) == 1) {
				daddr_t blk = alist_alloc(bl, count);
				kprintf("    R=%04x\n", blk);
			} else {
				kprintf("?\n");
			}
			break;
		case 'f':
			if (sscanf(buf + 1, "%x %d", &da, &count) == 2) {
				alist_free(bl, da, count);
			} else {
				kprintf("?\n");
			}
			break;
		case '?':
		case 'h':
			puts(
			    "p          -print\n"
			    "a %d       -allocate\n"
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

