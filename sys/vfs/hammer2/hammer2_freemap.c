/*
 * Copyright (c) 2011-2014 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@dragonflybsd.org>
 * by Venkatesh Srinivas <vsrinivas@dragonflybsd.org>
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
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/fcntl.h>
#include <sys/buf.h>
#include <sys/proc.h>
#include <sys/namei.h>
#include <sys/mount.h>
#include <sys/vnode.h>
#include <sys/mountctl.h>

#include "hammer2.h"

#define FREEMAP_DEBUG	0

struct hammer2_fiterate {
	hammer2_off_t	bpref;
	hammer2_off_t	bnext;
	int		loops;
};

typedef struct hammer2_fiterate hammer2_fiterate_t;

static int hammer2_freemap_try_alloc(hammer2_chain_t **parentp,
			hammer2_blockref_t *bref,
			int radix, hammer2_fiterate_t *iter);
static void hammer2_freemap_init(hammer2_dev_t *hmp,
			hammer2_key_t key, hammer2_chain_t *chain);
static int hammer2_bmap_alloc(hammer2_dev_t *hmp,
			hammer2_bmap_data_t *bmap, uint16_t class,
			int n, int radix, hammer2_key_t *basep);
static int hammer2_freemap_iterate(hammer2_chain_t **parentp,
			hammer2_chain_t **chainp,
			hammer2_fiterate_t *iter);

static __inline
int
hammer2_freemapradix(int radix)
{
	return(radix);
}

/*
 * Calculate the device offset for the specified FREEMAP_NODE or FREEMAP_LEAF
 * bref.  Return a combined media offset and physical size radix.  Freemap
 * chains use fixed storage offsets in the 4MB reserved area at the
 * beginning of each 2GB zone
 *
 * Rotate between four possibilities.  Theoretically this means we have three
 * good freemaps in case of a crash which we can use as a base for the fixup
 * scan at mount-time.
 */
#define H2FMBASE(key, radix)	((key) & ~(((hammer2_off_t)1 << (radix)) - 1))
#define H2FMSHIFT(radix)	((hammer2_off_t)1 << (radix))

static
int
hammer2_freemap_reserve(hammer2_chain_t *chain, int radix)
{
	hammer2_blockref_t *bref = &chain->bref;
	hammer2_off_t off;
	int index;
	int index_inc;
	size_t bytes;

	/*
	 * Physical allocation size.
	 */
	bytes = (size_t)1 << radix;

	/*
	 * Calculate block selection index 0..7 of current block.  If this
	 * is the first allocation of the block (verses a modification of an
	 * existing block), we use index 0, otherwise we use the next rotating
	 * index.
	 */
	if ((bref->data_off & ~HAMMER2_OFF_MASK_RADIX) == 0) {
		index = 0;
	} else {
		off = bref->data_off & ~HAMMER2_OFF_MASK_RADIX &
		      (((hammer2_off_t)1 <<
			HAMMER2_FREEMAP_LEVEL1_RADIX) - 1);
		off = off / HAMMER2_PBUFSIZE;
		KKASSERT(off >= HAMMER2_ZONE_FREEMAP_00 &&
			 off < HAMMER2_ZONE_FREEMAP_END);
		index = (int)(off - HAMMER2_ZONE_FREEMAP_00) /
			HAMMER2_ZONE_FREEMAP_INC;
		KKASSERT(index >= 0 && index < HAMMER2_NFREEMAPS);
		if (++index == HAMMER2_NFREEMAPS)
			index = 0;
	}

	/*
	 * Calculate the block offset of the reserved block.  This will
	 * point into the 4MB reserved area at the base of the appropriate
	 * 2GB zone, once added to the FREEMAP_x selection above.
	 */
	index_inc = index * HAMMER2_ZONE_FREEMAP_INC;

	switch(bref->keybits) {
	/* case HAMMER2_FREEMAP_LEVEL6_RADIX: not applicable */
	case HAMMER2_FREEMAP_LEVEL5_RADIX:	/* 2EB */
		KKASSERT(bref->type == HAMMER2_BREF_TYPE_FREEMAP_NODE);
		KKASSERT(bytes == HAMMER2_FREEMAP_LEVELN_PSIZE);
		off = H2FMBASE(bref->key, HAMMER2_FREEMAP_LEVEL5_RADIX) +
		      (index_inc + HAMMER2_ZONE_FREEMAP_00 +
		       HAMMER2_ZONEFM_LEVEL5) * HAMMER2_PBUFSIZE;
		break;
	case HAMMER2_FREEMAP_LEVEL4_RADIX:	/* 2EB */
		KKASSERT(bref->type == HAMMER2_BREF_TYPE_FREEMAP_NODE);
		KKASSERT(bytes == HAMMER2_FREEMAP_LEVELN_PSIZE);
		off = H2FMBASE(bref->key, HAMMER2_FREEMAP_LEVEL4_RADIX) +
		      (index_inc + HAMMER2_ZONE_FREEMAP_00 +
		       HAMMER2_ZONEFM_LEVEL4) * HAMMER2_PBUFSIZE;
		break;
	case HAMMER2_FREEMAP_LEVEL3_RADIX:	/* 2PB */
		KKASSERT(bref->type == HAMMER2_BREF_TYPE_FREEMAP_NODE);
		KKASSERT(bytes == HAMMER2_FREEMAP_LEVELN_PSIZE);
		off = H2FMBASE(bref->key, HAMMER2_FREEMAP_LEVEL3_RADIX) +
		      (index_inc + HAMMER2_ZONE_FREEMAP_00 +
		       HAMMER2_ZONEFM_LEVEL3) * HAMMER2_PBUFSIZE;
		break;
	case HAMMER2_FREEMAP_LEVEL2_RADIX:	/* 2TB */
		KKASSERT(bref->type == HAMMER2_BREF_TYPE_FREEMAP_NODE);
		KKASSERT(bytes == HAMMER2_FREEMAP_LEVELN_PSIZE);
		off = H2FMBASE(bref->key, HAMMER2_FREEMAP_LEVEL2_RADIX) +
		      (index_inc + HAMMER2_ZONE_FREEMAP_00 +
		       HAMMER2_ZONEFM_LEVEL2) * HAMMER2_PBUFSIZE;
		break;
	case HAMMER2_FREEMAP_LEVEL1_RADIX:	/* 2GB */
		KKASSERT(bref->type == HAMMER2_BREF_TYPE_FREEMAP_LEAF);
		KKASSERT(bytes == HAMMER2_FREEMAP_LEVELN_PSIZE);
		off = H2FMBASE(bref->key, HAMMER2_FREEMAP_LEVEL1_RADIX) +
		      (index_inc + HAMMER2_ZONE_FREEMAP_00 +
		       HAMMER2_ZONEFM_LEVEL1) * HAMMER2_PBUFSIZE;
		break;
	default:
		panic("freemap: bad radix(2) %p %d\n", bref, bref->keybits);
		/* NOT REACHED */
		off = (hammer2_off_t)-1;
		break;
	}
	bref->data_off = off | radix;
#if FREEMAP_DEBUG
	kprintf("FREEMAP BLOCK TYPE %d %016jx/%d DATA_OFF=%016jx\n",
		bref->type, bref->key, bref->keybits, bref->data_off);
#endif
	return (0);
}

/*
 * Normal freemap allocator
 *
 * Use available hints to allocate space using the freemap.  Create missing
 * freemap infrastructure on-the-fly as needed (including marking initial
 * allocations using the iterator as allocated, instantiating new 2GB zones,
 * and dealing with the end-of-media edge case).
 *
 * ip and bpref are only used as a heuristic to determine locality of
 * reference.  bref->key may also be used heuristically.
 */
int
hammer2_freemap_alloc(hammer2_chain_t *chain, size_t bytes)
{
	hammer2_dev_t *hmp = chain->hmp;
	hammer2_blockref_t *bref = &chain->bref;
	hammer2_chain_t *parent;
	int radix;
	int error;
	unsigned int hindex;
	hammer2_fiterate_t iter;

	/*
	 * Validate the allocation size.  It must be a power of 2.
	 *
	 * For now require that the caller be aware of the minimum
	 * allocation (1K).
	 */
	radix = hammer2_getradix(bytes);
	KKASSERT((size_t)1 << radix == bytes);

	if (bref->type == HAMMER2_BREF_TYPE_FREEMAP_NODE ||
	    bref->type == HAMMER2_BREF_TYPE_FREEMAP_LEAF) {
		/*
		 * Freemap blocks themselves are assigned from the reserve
		 * area, not allocated from the freemap.
		 */
		error = hammer2_freemap_reserve(chain, radix);
		return error;
	}

	KKASSERT(bytes >= HAMMER2_ALLOC_MIN && bytes <= HAMMER2_ALLOC_MAX);

	/*
	 * Calculate the starting point for our allocation search.
	 *
	 * Each freemap leaf is dedicated to a specific freemap_radix.
	 * The freemap_radix can be more fine-grained than the device buffer
	 * radix which results in inodes being grouped together in their
	 * own segment, terminal-data (16K or less) and initial indirect
	 * block being grouped together, and then full-indirect and full-data
	 * blocks (64K) being grouped together.
	 *
	 * The single most important aspect of this is the inode grouping
	 * because that is what allows 'find' and 'ls' and other filesystem
	 * topology operations to run fast.
	 */
#if 0
	if (bref->data_off & ~HAMMER2_OFF_MASK_RADIX)
		bpref = bref->data_off & ~HAMMER2_OFF_MASK_RADIX;
	else if (trans->tmp_bpref)
		bpref = trans->tmp_bpref;
	else if (trans->tmp_ip)
		bpref = trans->tmp_ip->chain->bref.data_off;
	else
#endif
	/*
	 * Heuristic tracking index.  We would like one for each distinct
	 * bref type if possible.  heur_freemap[] has room for two classes
	 * for each type.  At a minimum we have to break-up our heuristic
	 * by device block sizes.
	 */
	hindex = hammer2_devblkradix(radix) - HAMMER2_MINIORADIX;
	KKASSERT(hindex < HAMMER2_FREEMAP_HEUR_NRADIX);
	hindex += bref->type * HAMMER2_FREEMAP_HEUR_NRADIX;
	hindex &= HAMMER2_FREEMAP_HEUR_TYPES * HAMMER2_FREEMAP_HEUR_NRADIX - 1;
	KKASSERT(hindex < HAMMER2_FREEMAP_HEUR);

	iter.bpref = hmp->heur_freemap[hindex];

	/*
	 * Make sure bpref is in-bounds.  It's ok if bpref covers a zone's
	 * reserved area, the try code will iterate past it.
	 */
	if (iter.bpref > hmp->voldata.volu_size)
		iter.bpref = hmp->voldata.volu_size - 1;

	/*
	 * Iterate the freemap looking for free space before and after.
	 */
	parent = &hmp->fchain;
	hammer2_chain_ref(parent);
	hammer2_chain_lock(parent, HAMMER2_RESOLVE_ALWAYS);
	error = EAGAIN;
	iter.bnext = iter.bpref;
	iter.loops = 0;

	while (error == EAGAIN) {
		error = hammer2_freemap_try_alloc(&parent, bref, radix, &iter);
	}
	hmp->heur_freemap[hindex] = iter.bnext;
	hammer2_chain_unlock(parent);
	hammer2_chain_drop(parent);

	return (error);
}

static int
hammer2_freemap_try_alloc(hammer2_chain_t **parentp,
			  hammer2_blockref_t *bref, int radix,
			  hammer2_fiterate_t *iter)
{
	hammer2_dev_t *hmp = (*parentp)->hmp;
	hammer2_off_t l0size;
	hammer2_off_t l1size;
	hammer2_off_t l1mask;
	hammer2_key_t key_dummy;
	hammer2_chain_t *chain;
	hammer2_off_t key;
	size_t bytes;
	uint16_t class;
	int error = 0;
	int cache_index = -1;

	/*
	 * Calculate the number of bytes being allocated, the number
	 * of contiguous bits of bitmap being allocated, and the bitmap
	 * mask.
	 *
	 * WARNING! cpu hardware may mask bits == 64 -> 0 and blow up the
	 *	    mask calculation.
	 */
	bytes = (size_t)1 << radix;
	class = (bref->type << 8) | hammer2_devblkradix(radix);

	/*
	 * Lookup the level1 freemap chain, creating and initializing one
	 * if necessary.  Intermediate levels will be created automatically
	 * when necessary by hammer2_chain_create().
	 */
	key = H2FMBASE(iter->bnext, HAMMER2_FREEMAP_LEVEL1_RADIX);
	l0size = H2FMSHIFT(HAMMER2_FREEMAP_LEVEL0_RADIX);
	l1size = H2FMSHIFT(HAMMER2_FREEMAP_LEVEL1_RADIX);
	l1mask = l1size - 1;

	chain = hammer2_chain_lookup(parentp, &key_dummy, key, key + l1mask,
				     &cache_index,
				     HAMMER2_LOOKUP_ALWAYS |
				     HAMMER2_LOOKUP_MATCHIND);

	if (chain == NULL) {
		/*
		 * Create the missing leaf, be sure to initialize
		 * the auxillary freemap tracking information in
		 * the bref.check.freemap structure.
		 */
#if 0
		kprintf("freemap create L1 @ %016jx bpref %016jx\n",
			key, iter->bpref);
#endif
		error = hammer2_chain_create(parentp, &chain, hmp->spmp,
				     key, HAMMER2_FREEMAP_LEVEL1_RADIX,
				     HAMMER2_BREF_TYPE_FREEMAP_LEAF,
				     HAMMER2_FREEMAP_LEVELN_PSIZE,
				     0);
		KKASSERT(error == 0);
		if (error == 0) {
			hammer2_chain_modify(chain, 0);
			bzero(&chain->data->bmdata[0],
			      HAMMER2_FREEMAP_LEVELN_PSIZE);
			chain->bref.check.freemap.bigmask = (uint32_t)-1;
			chain->bref.check.freemap.avail = l1size;
			/* bref.methods should already be inherited */

			hammer2_freemap_init(hmp, key, chain);
		}
	} else if (chain->error) {
		/*
		 * Error during lookup.
		 */
		kprintf("hammer2_freemap_try_alloc: %016jx: error %s\n",
			(intmax_t)bref->data_off,
			hammer2_error_str(chain->error));
		error = EIO;
	} else if ((chain->bref.check.freemap.bigmask &
		   ((size_t)1 << radix)) == 0) {
		/*
		 * Already flagged as not having enough space
		 */
		error = ENOSPC;
	} else {
		/*
		 * Modify existing chain to setup for adjustment.
		 */
		hammer2_chain_modify(chain, 0);
	}

	/*
	 * Scan 2MB entries.
	 */
	if (error == 0) {
		hammer2_bmap_data_t *bmap;
		hammer2_key_t base_key;
		int count;
		int start;
		int n;

		KKASSERT(chain->bref.type == HAMMER2_BREF_TYPE_FREEMAP_LEAF);
		start = (int)((iter->bnext - key) >>
			      HAMMER2_FREEMAP_LEVEL0_RADIX);
		KKASSERT(start >= 0 && start < HAMMER2_FREEMAP_COUNT);
		hammer2_chain_modify(chain, 0);

		error = ENOSPC;
		for (count = 0; count < HAMMER2_FREEMAP_COUNT; ++count) {
			int availchk;

			if (start + count >= HAMMER2_FREEMAP_COUNT &&
			    start - count < 0) {
				break;
			}

			/*
			 * Calculate bmap pointer
			 *
			 * NOTE: bmap pointer is invalid if n >= FREEMAP_COUNT.
			 */
			n = start + count;
			bmap = &chain->data->bmdata[n];

			if (n >= HAMMER2_FREEMAP_COUNT) {
				availchk = 0;
			} else if (bmap->avail) {
				availchk = 1;
			} else if (radix < HAMMER2_FREEMAP_BLOCK_RADIX &&
			          (bmap->linear & HAMMER2_FREEMAP_BLOCK_MASK)) {
				availchk = 1;
			} else {
				availchk = 0;
			}

			if (availchk &&
			    (bmap->class == 0 || bmap->class == class)) {
				base_key = key + n * l0size;
				error = hammer2_bmap_alloc(hmp, bmap,
							   class, n, radix,
							   &base_key);
				if (error != ENOSPC) {
					key = base_key;
					break;
				}
			}

			/*
			 * Must recalculate after potentially having called
			 * hammer2_bmap_alloc() above in case chain was
			 * reallocated.
			 *
			 * NOTE: bmap pointer is invalid if n < 0.
			 */
			n = start - count;
			bmap = &chain->data->bmdata[n];
			if (n < 0) {
				availchk = 0;
			} else if (bmap->avail) {
				availchk = 1;
			} else if (radix < HAMMER2_FREEMAP_BLOCK_RADIX &&
			          (bmap->linear & HAMMER2_FREEMAP_BLOCK_MASK)) {
				availchk = 1;
			} else {
				availchk = 0;
			}

			if (availchk &&
			    (bmap->class == 0 || bmap->class == class)) {
				base_key = key + n * l0size;
				error = hammer2_bmap_alloc(hmp, bmap,
							   class, n, radix,
							   &base_key);
				if (error != ENOSPC) {
					key = base_key;
					break;
				}
			}
		}
		if (error == ENOSPC) {
			chain->bref.check.freemap.bigmask &=
				(uint32_t)~((size_t)1 << radix);
		}
		/* XXX also scan down from original count */
	}

	if (error == 0) {
		/*
		 * Assert validity.  Must be beyond the static allocator used
		 * by newfs_hammer2 (and thus also beyond the aux area),
		 * not go past the volume size, and must not be in the
		 * reserved segment area for a zone.
		 */
		KKASSERT(key >= hmp->voldata.allocator_beg &&
			 key + bytes <= hmp->voldata.volu_size);
		KKASSERT((key & HAMMER2_ZONE_MASK64) >= HAMMER2_ZONE_SEG);
		bref->data_off = key | radix;

#if 0
		kprintf("alloc cp=%p %016jx %016jx using %016jx\n",
			chain,
			bref->key, bref->data_off, chain->bref.data_off);
#endif
	} else if (error == ENOSPC) {
		/*
		 * Return EAGAIN with next iteration in iter->bnext, or
		 * return ENOSPC if the allocation map has been exhausted.
		 */
		error = hammer2_freemap_iterate(parentp, &chain, iter);
	}

	/*
	 * Cleanup
	 */
	if (chain) {
		hammer2_chain_unlock(chain);
		hammer2_chain_drop(chain);
	}
	return (error);
}

/*
 * Allocate (1<<radix) bytes from the bmap whos base data offset is (*basep).
 *
 * If the linear iterator is mid-block we use it directly (the bitmap should
 * already be marked allocated), otherwise we search for a block in the bitmap
 * that fits the allocation request.
 *
 * A partial bitmap allocation sets the minimum bitmap granularity (16KB)
 * to fully allocated and adjusts the linear allocator to allow the
 * remaining space to be allocated.
 */
static
int
hammer2_bmap_alloc(hammer2_dev_t *hmp, hammer2_bmap_data_t *bmap,
		   uint16_t class, int n, int radix, hammer2_key_t *basep)
{
	hammer2_io_t *dio;
	size_t size;
	size_t bgsize;
	int bmradix;
	hammer2_bitmap_t bmmask;
	int offset;
	int error;
	int i;
	int j;

	/*
	 * Take into account 2-bits per block when calculating bmradix.
	 */
	size = (size_t)1 << radix;

	if (radix <= HAMMER2_FREEMAP_BLOCK_RADIX) {
		bmradix = 2;
		/* (16K) 2 bits per allocation block */
	} else {
		bmradix = (hammer2_bitmap_t)2 <<
			  (radix - HAMMER2_FREEMAP_BLOCK_RADIX);
		/* (32K-256K) 4, 8, 16, 32 bits per allocation block */
	}

	/*
	 * Use the linear iterator to pack small allocations, otherwise
	 * fall-back to finding a free 16KB chunk.  The linear iterator
	 * is only valid when *NOT* on a freemap chunking boundary (16KB).
	 * If it is the bitmap must be scanned.  It can become invalid
	 * once we pack to the boundary.  We adjust it after a bitmap
	 * allocation only for sub-16KB allocations (so the perfectly good
	 * previous value can still be used for fragments when 16KB+
	 * allocations are made).
	 *
	 * Beware of hardware artifacts when bmradix == 64 (intermediate
	 * result can wind up being '1' instead of '0' if hardware masks
	 * bit-count & 31).
	 *
	 * NOTE: j needs to be even in the j= calculation.  As an artifact
	 *	 of the /2 division, our bitmask has to clear bit 0.
	 *
	 * NOTE: TODO this can leave little unallocatable fragments lying
	 *	 around.
	 */
	if (((uint32_t)bmap->linear & HAMMER2_FREEMAP_BLOCK_MASK) + size <=
	    HAMMER2_FREEMAP_BLOCK_SIZE &&
	    (bmap->linear & HAMMER2_FREEMAP_BLOCK_MASK) &&
	    bmap->linear < HAMMER2_SEGSIZE) {
		KKASSERT(bmap->linear >= 0 &&
			 bmap->linear + size <= HAMMER2_SEGSIZE &&
			 (bmap->linear & (HAMMER2_ALLOC_MIN - 1)) == 0);
		offset = bmap->linear;
		i = offset / (HAMMER2_SEGSIZE / 8);
		j = (offset / (HAMMER2_FREEMAP_BLOCK_SIZE / 2)) & 30;
		bmmask = (bmradix == HAMMER2_BMAP_BITS_PER_ELEMENT) ?
			 HAMMER2_BMAP_ALLONES :
			 ((hammer2_bitmap_t)1 << bmradix) - 1;
		bmmask <<= j;
		bmap->linear = offset + size;
	} else {
		for (i = 0; i < HAMMER2_BMAP_ELEMENTS; ++i) {
			bmmask = (bmradix == HAMMER2_BMAP_BITS_PER_ELEMENT) ?
				 HAMMER2_BMAP_ALLONES :
				 ((hammer2_bitmap_t)1 << bmradix) - 1;
			for (j = 0;
			     j < HAMMER2_BMAP_BITS_PER_ELEMENT;
			     j += bmradix) {
				if ((bmap->bitmapq[i] & bmmask) == 0)
					goto success;
				bmmask <<= bmradix;
			}
		}
		/*fragments might remain*/
		/*KKASSERT(bmap->avail == 0);*/
		return (ENOSPC);
success:
		offset = i * (HAMMER2_SEGSIZE / HAMMER2_BMAP_ELEMENTS) +
			 (j * (HAMMER2_FREEMAP_BLOCK_SIZE / 2));
		if (size & HAMMER2_FREEMAP_BLOCK_MASK)
			bmap->linear = offset + size;
	}

	/* 8 x (64/2) -> 256 x 16K -> 4MB */
	KKASSERT(i >= 0 && i < HAMMER2_BMAP_ELEMENTS);

	/*
	 * Optimize the buffer cache to avoid unnecessary read-before-write
	 * operations.
	 *
	 * The device block size could be larger than the allocation size
	 * so the actual bitmap test is somewhat more involved.  We have
	 * to use a compatible buffer size for this operation.
	 */
	if ((bmap->bitmapq[i] & bmmask) == 0 &&
	    hammer2_devblksize(size) != size) {
		size_t psize = hammer2_devblksize(size);
		hammer2_off_t pmask = (hammer2_off_t)psize - 1;
		int pbmradix = (hammer2_bitmap_t)2 <<
					(hammer2_devblkradix(radix) -
			       HAMMER2_FREEMAP_BLOCK_RADIX);
		hammer2_bitmap_t pbmmask;
		int pradix = hammer2_getradix(psize);

		pbmmask = (pbmradix == HAMMER2_BMAP_BITS_PER_ELEMENT) ?
			HAMMER2_BMAP_ALLONES :
			((hammer2_bitmap_t)1 << pbmradix) - 1;
		while ((pbmmask & bmmask) == 0)
			pbmmask <<= pbmradix;

#if 0
		kprintf("%016jx mask %016jx %016jx %016jx (%zd/%zd)\n",
			*basep + offset, bmap->bitmapq[i],
			pbmmask, bmmask, size, psize);
#endif

		if ((bmap->bitmapq[i] & pbmmask) == 0) {
			error = hammer2_io_newq(hmp,
						(*basep + (offset & ~pmask)) |
						 pradix,
						psize, &dio);
			hammer2_io_bqrelse(&dio);
		}
	}

#if 0
	/*
	 * When initializing a new inode segment also attempt to initialize
	 * an adjacent segment.  Be careful not to index beyond the array
	 * bounds.
	 *
	 * We do this to try to localize inode accesses to improve
	 * directory scan rates.  XXX doesn't improve scan rates.
	 */
	if (size == HAMMER2_INODE_BYTES) {
		if (n & 1) {
			if (bmap[-1].radix == 0 && bmap[-1].avail)
				bmap[-1].radix = radix;
		} else {
			if (bmap[1].radix == 0 && bmap[1].avail)
				bmap[1].radix = radix;
		}
	}
#endif
	/*
	 * Calculate the bitmap-granular change in bgsize for the volume
	 * header.  We cannot use the fine-grained change here because
	 * the bulkfree code can't undo it.  If the bitmap element is already
	 * marked allocated it has already been accounted for.
	 */
	if (radix < HAMMER2_FREEMAP_BLOCK_RADIX) {
		if (bmap->bitmapq[i] & bmmask)
			bgsize = 0;
		else
			bgsize = HAMMER2_FREEMAP_BLOCK_SIZE;
	} else {
		bgsize = size;
	}

	/*
	 * Adjust the bitmap, set the class (it might have been 0),
	 * and available bytes, update the allocation offset (*basep)
	 * from the L0 base to the actual offset.
	 *
	 * avail must reflect the bitmap-granular availability.  The allocator
	 * tests will also check the linear iterator.
	 */
	bmap->bitmapq[i] |= bmmask;
	bmap->class = class;
	bmap->avail -= bgsize;
	*basep += offset;

	/*
	 * Adjust the volume header's allocator_free parameter.  This
	 * parameter has to be fixed up by bulkfree which has no way to
	 * figure out sub-16K chunking, so it must be adjusted by the
	 * bitmap-granular size.
	 */
	if (bgsize) {
		hammer2_voldata_lock(hmp);
		hammer2_voldata_modify(hmp);
		hmp->voldata.allocator_free -= bgsize;
		hammer2_voldata_unlock(hmp);
	}

	return(0);
}

static
void
hammer2_freemap_init(hammer2_dev_t *hmp, hammer2_key_t key,
		     hammer2_chain_t *chain)
{
	hammer2_off_t l1size;
	hammer2_off_t lokey;
	hammer2_off_t hikey;
	hammer2_bmap_data_t *bmap;
	int count;

	l1size = H2FMSHIFT(HAMMER2_FREEMAP_LEVEL1_RADIX);

	/*
	 * Calculate the portion of the 2GB map that should be initialized
	 * as free.  Portions below or after will be initialized as allocated.
	 * SEGMASK-align the areas so we don't have to worry about sub-scans
	 * or endianess when using memset.
	 *
	 * (1) Ensure that all statically allocated space from newfs_hammer2
	 *     is marked allocated.
	 *
	 * (2) Ensure that the reserved area is marked allocated (typically
	 *     the first 4MB of the 2GB area being represented).
	 *
	 * (3) Ensure that any trailing space at the end-of-volume is marked
	 *     allocated.
	 *
	 * WARNING! It is possible for lokey to be larger than hikey if the
	 *	    entire 2GB segment is within the static allocation.
	 */
	lokey = (hmp->voldata.allocator_beg + HAMMER2_SEGMASK64) &
		~HAMMER2_SEGMASK64;

	if (lokey < H2FMBASE(key, HAMMER2_FREEMAP_LEVEL1_RADIX) +
		  HAMMER2_ZONE_SEG64) {
		lokey = H2FMBASE(key, HAMMER2_FREEMAP_LEVEL1_RADIX) +
			HAMMER2_ZONE_SEG64;
	}

	hikey = key + H2FMSHIFT(HAMMER2_FREEMAP_LEVEL1_RADIX);
	if (hikey > hmp->voldata.volu_size) {
		hikey = hmp->voldata.volu_size & ~HAMMER2_SEGMASK64;
	}

	chain->bref.check.freemap.avail =
		H2FMSHIFT(HAMMER2_FREEMAP_LEVEL1_RADIX);
	bmap = &chain->data->bmdata[0];

	for (count = 0; count < HAMMER2_FREEMAP_COUNT; ++count) {
		if (key < lokey || key >= hikey) {
			memset(bmap->bitmapq, -1,
			       sizeof(bmap->bitmapq));
			bmap->avail = 0;
			bmap->linear = HAMMER2_SEGSIZE;
			chain->bref.check.freemap.avail -=
				H2FMSHIFT(HAMMER2_FREEMAP_LEVEL0_RADIX);
		} else {
			bmap->avail = H2FMSHIFT(HAMMER2_FREEMAP_LEVEL0_RADIX);
		}
		key += H2FMSHIFT(HAMMER2_FREEMAP_LEVEL0_RADIX);
		++bmap;
	}
}

/*
 * The current Level 1 freemap has been exhausted, iterate to the next
 * one, return ENOSPC if no freemaps remain.
 *
 * XXX this should rotate back to the beginning to handle freed-up space
 * XXX or use intermediate entries to locate free space. TODO
 */
static int
hammer2_freemap_iterate(hammer2_chain_t **parentp, hammer2_chain_t **chainp,
			hammer2_fiterate_t *iter)
{
	hammer2_dev_t *hmp = (*parentp)->hmp;

	iter->bnext &= ~(H2FMSHIFT(HAMMER2_FREEMAP_LEVEL1_RADIX) - 1);
	iter->bnext += H2FMSHIFT(HAMMER2_FREEMAP_LEVEL1_RADIX);
	if (iter->bnext >= hmp->voldata.volu_size) {
		iter->bnext = 0;
		if (++iter->loops == 2)
			return (ENOSPC);
	}
	return(EAGAIN);
}

/*
 * Adjust the bit-pattern for data in the freemap bitmap according to
 * (how).  This code is called from on-mount recovery to fixup (mark
 * as allocated) blocks whos freemap upates might not have been committed
 * in the last crash and is used by the bulk freemap scan to stage frees.
 *
 * XXX currently disabled when how == 0 (the normal real-time case).  At
 * the moment we depend on the bulk freescan to actually free blocks.  It
 * will still call this routine with a non-zero how to stage possible frees
 * and to do the actual free.
 */
void
hammer2_freemap_adjust(hammer2_dev_t *hmp, hammer2_blockref_t *bref, int how)
{
	hammer2_off_t data_off = bref->data_off;
	hammer2_chain_t *chain;
	hammer2_chain_t *parent;
	hammer2_bmap_data_t *bmap;
	hammer2_key_t key;
	hammer2_key_t key_dummy;
	hammer2_off_t l0size;
	hammer2_off_t l1size;
	hammer2_off_t l1mask;
	hammer2_bitmap_t *bitmap;
	const hammer2_bitmap_t bmmask00 = 0;
	hammer2_bitmap_t bmmask01;
	hammer2_bitmap_t bmmask10;
	hammer2_bitmap_t bmmask11;
	size_t bytes;
	uint16_t class;
	int radix;
	int start;
	int count;
	int modified = 0;
	int cache_index = -1;
	int error;

	KKASSERT(how == HAMMER2_FREEMAP_DORECOVER);

	radix = (int)data_off & HAMMER2_OFF_MASK_RADIX;
	data_off &= ~HAMMER2_OFF_MASK_RADIX;
	KKASSERT(radix <= HAMMER2_RADIX_MAX);

	bytes = (size_t)1 << radix;
	class = (bref->type << 8) | hammer2_devblkradix(radix);

	/*
	 * We can't adjust thre freemap for data allocations made by
	 * newfs_hammer2.
	 */
	if (data_off < hmp->voldata.allocator_beg)
		return;

	KKASSERT((data_off & HAMMER2_ZONE_MASK64) >= HAMMER2_ZONE_SEG);

	/*
	 * Lookup the level1 freemap chain.  The chain must exist.
	 */
	key = H2FMBASE(data_off, HAMMER2_FREEMAP_LEVEL1_RADIX);
	l0size = H2FMSHIFT(HAMMER2_FREEMAP_LEVEL0_RADIX);
	l1size = H2FMSHIFT(HAMMER2_FREEMAP_LEVEL1_RADIX);
	l1mask = l1size - 1;

	parent = &hmp->fchain;
	hammer2_chain_ref(parent);
	hammer2_chain_lock(parent, HAMMER2_RESOLVE_ALWAYS);

	chain = hammer2_chain_lookup(&parent, &key_dummy, key, key + l1mask,
				     &cache_index,
				     HAMMER2_LOOKUP_ALWAYS |
				     HAMMER2_LOOKUP_MATCHIND);

	/*
	 * Stop early if we are trying to free something but no leaf exists.
	 */
	if (chain == NULL && how != HAMMER2_FREEMAP_DORECOVER) {
		kprintf("hammer2_freemap_adjust: %016jx: no chain\n",
			(intmax_t)bref->data_off);
		goto done;
	}
	if (chain->error) {
		kprintf("hammer2_freemap_adjust: %016jx: error %s\n",
			(intmax_t)bref->data_off,
			hammer2_error_str(chain->error));
		hammer2_chain_unlock(chain);
		hammer2_chain_drop(chain);
		chain = NULL;
		goto done;
	}

	/*
	 * Create any missing leaf(s) if we are doing a recovery (marking
	 * the block(s) as being allocated instead of being freed).  Be sure
	 * to initialize the auxillary freemap tracking info in the
	 * bref.check.freemap structure.
	 */
	if (chain == NULL && how == HAMMER2_FREEMAP_DORECOVER) {
		error = hammer2_chain_create(&parent, &chain, hmp->spmp,
				     key, HAMMER2_FREEMAP_LEVEL1_RADIX,
				     HAMMER2_BREF_TYPE_FREEMAP_LEAF,
				     HAMMER2_FREEMAP_LEVELN_PSIZE,
				     0);

		if (hammer2_debug & 0x0040) {
			kprintf("fixup create chain %p %016jx:%d\n",
				chain, chain->bref.key, chain->bref.keybits);
		}

		if (error == 0) {
			hammer2_chain_modify(chain, 0);
			bzero(&chain->data->bmdata[0],
			      HAMMER2_FREEMAP_LEVELN_PSIZE);
			chain->bref.check.freemap.bigmask = (uint32_t)-1;
			chain->bref.check.freemap.avail = l1size;
			/* bref.methods should already be inherited */

			hammer2_freemap_init(hmp, key, chain);
		}
		/* XXX handle error */
	}

#if FREEMAP_DEBUG
	kprintf("FREEMAP ADJUST TYPE %d %016jx/%d DATA_OFF=%016jx\n",
		chain->bref.type, chain->bref.key,
		chain->bref.keybits, chain->bref.data_off);
#endif

	/*
	 * Calculate the bitmask (runs in 2-bit pairs).
	 */
	start = ((int)(data_off >> HAMMER2_FREEMAP_BLOCK_RADIX) & 15) * 2;
	bmmask01 = (hammer2_bitmap_t)1 << start;
	bmmask10 = (hammer2_bitmap_t)2 << start;
	bmmask11 = (hammer2_bitmap_t)3 << start;

	/*
	 * Fixup the bitmap.  Partial blocks cannot be fully freed unless
	 * a bulk scan is able to roll them up.
	 */
	if (radix < HAMMER2_FREEMAP_BLOCK_RADIX) {
		count = 1;
		if (how == HAMMER2_FREEMAP_DOREALFREE)
			how = HAMMER2_FREEMAP_DOMAYFREE;
	} else {
		count = 1 << (radix - HAMMER2_FREEMAP_BLOCK_RADIX);
	}

	/*
	 * [re]load the bmap and bitmap pointers.  Each bmap entry covers
	 * a 2MB swath.  The bmap itself (LEVEL1) covers 2GB.
	 *
	 * Be sure to reset the linear iterator to ensure that the adjustment
	 * is not ignored.
	 */
again:
	bmap = &chain->data->bmdata[(int)(data_off >> HAMMER2_SEGRADIX) &
				    (HAMMER2_FREEMAP_COUNT - 1)];
	bitmap = &bmap->bitmapq[(int)(data_off >> (HAMMER2_SEGRADIX - 3)) & 7];

	if (modified)
		bmap->linear = 0;

	while (count) {
		KKASSERT(bmmask11);
		if (how == HAMMER2_FREEMAP_DORECOVER) {
			/*
			 * Recovery request, mark as allocated.
			 */
			if ((*bitmap & bmmask11) != bmmask11) {
				if (modified == 0) {
					hammer2_chain_modify(chain, 0);
					modified = 1;
					goto again;
				}
				if ((*bitmap & bmmask11) == bmmask00) {
					bmap->avail -=
						HAMMER2_FREEMAP_BLOCK_SIZE;
				}
				if (bmap->class == 0)
					bmap->class = class;
				*bitmap |= bmmask11;
				if (hammer2_debug & 0x0040) {
					kprintf("hammer2_freemap_recover: "
						"fixup type=%02x "
						"block=%016jx/%zd\n",
						bref->type, data_off, bytes);
				}
			} else {
				/*
				kprintf("hammer2_freemap_recover:  good "
					"type=%02x block=%016jx/%zd\n",
					bref->type, data_off, bytes);
				*/
			}
		}
#if 0
		/*
		 * XXX this stuff doesn't work, avail is miscalculated and
		 * code 10 means something else now.
		 */
		else if ((*bitmap & bmmask11) == bmmask11) {
			/*
			 * Mayfree/Realfree request and bitmap is currently
			 * marked as being fully allocated.
			 */
			if (!modified) {
				hammer2_chain_modify(chain, 0);
				modified = 1;
				goto again;
			}
			if (how == HAMMER2_FREEMAP_DOREALFREE)
				*bitmap &= ~bmmask11;
			else
				*bitmap = (*bitmap & ~bmmask11) | bmmask10;
		} else if ((*bitmap & bmmask11) == bmmask10) {
			/*
			 * Mayfree/Realfree request and bitmap is currently
			 * marked as being possibly freeable.
			 */
			if (how == HAMMER2_FREEMAP_DOREALFREE) {
				if (!modified) {
					hammer2_chain_modify(chain, 0);
					modified = 1;
					goto again;
				}
				*bitmap &= ~bmmask11;
			}
		} else {
			/*
			 * 01 - Not implemented, currently illegal state
			 * 00 - Not allocated at all, illegal free.
			 */
			panic("hammer2_freemap_adjust: "
			      "Illegal state %08x(%08x)",
			      *bitmap, *bitmap & bmmask11);
		}
#endif
		--count;
		bmmask01 <<= 2;
		bmmask10 <<= 2;
		bmmask11 <<= 2;
	}
#if HAMMER2_BMAP_ELEMENTS != 8
#error "hammer2_freemap.c: HAMMER2_BMAP_ELEMENTS expected to be 8"
#endif
	if (how == HAMMER2_FREEMAP_DOREALFREE && modified) {
		bmap->avail += 1 << radix;
		KKASSERT(bmap->avail <= HAMMER2_SEGSIZE);
		if (bmap->avail == HAMMER2_SEGSIZE &&
		    bmap->bitmapq[0] == 0 &&
		    bmap->bitmapq[1] == 0 &&
		    bmap->bitmapq[2] == 0 &&
		    bmap->bitmapq[3] == 0 &&
		    bmap->bitmapq[4] == 0 &&
		    bmap->bitmapq[5] == 0 &&
		    bmap->bitmapq[6] == 0 &&
		    bmap->bitmapq[7] == 0) {
			key = H2FMBASE(data_off, HAMMER2_FREEMAP_LEVEL0_RADIX);
			kprintf("Freeseg %016jx\n", (intmax_t)key);
			bmap->class = 0;
		}
	}

	/*
	 * chain->bref.check.freemap.bigmask (XXX)
	 *
	 * Setting bigmask is a hint to the allocation code that there might
	 * be something allocatable.  We also set this in recovery... it
	 * doesn't hurt and we might want to use the hint for other validation
	 * operations later on.
	 */
	if (modified)
		chain->bref.check.freemap.bigmask |= 1 << radix;

	hammer2_chain_unlock(chain);
	hammer2_chain_drop(chain);
done:
	hammer2_chain_unlock(parent);
	hammer2_chain_drop(parent);
}

/*
 * Validate the freemap, in three stages.
 *
 * stage-1	ALLOCATED     -> POSSIBLY FREE
 *		POSSIBLY FREE -> POSSIBLY FREE (type corrected)
 *
 *	This transitions bitmap entries from ALLOCATED to POSSIBLY FREE.
 *	The POSSIBLY FREE state does not mean that a block is actually free
 *	and may be transitioned back to ALLOCATED in stage-2.
 *
 *	This is typically done during normal filesystem operations when
 *	something is deleted or a block is replaced.
 *
 *	This is done by bulkfree in-bulk after a memory-bounded meta-data
 *	scan to try to determine what might be freeable.
 *
 *	This can be done unconditionally through a freemap scan when the
 *	intention is to brute-force recover the proper state of the freemap.
 *
 * stage-2	POSSIBLY FREE -> ALLOCATED	(scan metadata topology)
 *
 *	This is done by bulkfree during a meta-data scan to ensure that
 *	all blocks still actually allocated by the filesystem are marked
 *	as such.
 *
 *	NOTE! Live filesystem transitions to POSSIBLY FREE can occur while
 *	      the bulkfree stage-2 and stage-3 is running.  The live filesystem
 *	      will use the alternative POSSIBLY FREE type (2) to prevent
 *	      stage-3 from improperly transitioning unvetted possibly-free
 *	      blocks to FREE.
 *
 * stage-3	POSSIBLY FREE (type 1) -> FREE	(scan freemap)
 *
 *	This is done by bulkfree to finalize POSSIBLY FREE states.
 *
 */
