/*
 * Copyright (c) 2011-2013 The DragonFly Project.  All rights reserved.
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

struct hammer2_fiterate {
	hammer2_off_t	bpref;
	hammer2_off_t	bnext;
	int		loops;
};

typedef struct hammer2_fiterate hammer2_fiterate_t;

static int hammer2_freemap_try_alloc(hammer2_trans_t *trans,
			hammer2_chain_t **parentp, hammer2_blockref_t *bref,
			int radix, hammer2_fiterate_t *iter);
static void hammer2_freemap_init(hammer2_trans_t *trans, hammer2_mount_t *hmp,
			hammer2_key_t key, hammer2_chain_t *chain);
static int hammer2_bmap_alloc(hammer2_trans_t *trans, hammer2_mount_t *hmp,
			hammer2_bmap_data_t *bmap, uint16_t class,
			int n, int radix, hammer2_key_t *basep);
static int hammer2_freemap_iterate(hammer2_trans_t *trans,
			hammer2_chain_t **parentp, hammer2_chain_t **chainp,
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
hammer2_freemap_reserve(hammer2_mount_t *hmp, hammer2_blockref_t *bref,
			int radix)
{
	hammer2_off_t off;
	size_t bytes;

	/*
	 * Physical allocation size -> radix.  Typically either 256 for
	 * a level 0 freemap leaf or 65536 for a level N freemap node.
	 *
	 * NOTE: A 256 byte bitmap represents 256 x 8 x 1024 = 2MB of storage.
	 *	 Do not use hammer2_allocsize() here as it has a min cap.
	 */
	bytes = 1 << radix;

	/*
	 * Adjust by HAMMER2_ZONE_FREEMAP_{A,B,C,D} using the existing
	 * offset as a basis.  Start in zone A if previously unallocated.
	 */
	if ((bref->data_off & ~HAMMER2_OFF_MASK_RADIX) == 0) {
		off = HAMMER2_ZONE_FREEMAP_A;
	} else {
		off = bref->data_off & ~HAMMER2_OFF_MASK_RADIX &
		      (((hammer2_off_t)1 << HAMMER2_FREEMAP_LEVEL1_RADIX) - 1);
		off = off / HAMMER2_PBUFSIZE;
		KKASSERT(off >= HAMMER2_ZONE_FREEMAP_A);
		KKASSERT(off < HAMMER2_ZONE_FREEMAP_D + 4);

		if (off >= HAMMER2_ZONE_FREEMAP_D)
			off = HAMMER2_ZONE_FREEMAP_A;
		else if (off >= HAMMER2_ZONE_FREEMAP_C)
			off = HAMMER2_ZONE_FREEMAP_D;
		else if (off >= HAMMER2_ZONE_FREEMAP_B)
			off = HAMMER2_ZONE_FREEMAP_C;
		else
			off = HAMMER2_ZONE_FREEMAP_B;
	}
	off = off * HAMMER2_PBUFSIZE;

	/*
	 * Calculate the block offset of the reserved block.  This will
	 * point into the 4MB reserved area at the base of the appropriate
	 * 2GB zone, once added to the FREEMAP_x selection above.
	 */
	switch(bref->keybits) {
	/* case HAMMER2_FREEMAP_LEVEL5_RADIX: not applicable */
	case HAMMER2_FREEMAP_LEVEL4_RADIX:	/* 2EB */
		KKASSERT(bref->type == HAMMER2_BREF_TYPE_FREEMAP_NODE);
		KKASSERT(bytes == HAMMER2_FREEMAP_LEVELN_PSIZE);
		off += H2FMBASE(bref->key, HAMMER2_FREEMAP_LEVEL4_RADIX) +
		       HAMMER2_ZONEFM_LEVEL4 * HAMMER2_PBUFSIZE;
		break;
	case HAMMER2_FREEMAP_LEVEL3_RADIX:	/* 2PB */
		KKASSERT(bref->type == HAMMER2_BREF_TYPE_FREEMAP_NODE);
		KKASSERT(bytes == HAMMER2_FREEMAP_LEVELN_PSIZE);
		off += H2FMBASE(bref->key, HAMMER2_FREEMAP_LEVEL3_RADIX) +
		       HAMMER2_ZONEFM_LEVEL3 * HAMMER2_PBUFSIZE;
		break;
	case HAMMER2_FREEMAP_LEVEL2_RADIX:	/* 2TB */
		KKASSERT(bref->type == HAMMER2_BREF_TYPE_FREEMAP_NODE);
		KKASSERT(bytes == HAMMER2_FREEMAP_LEVELN_PSIZE);
		off += H2FMBASE(bref->key, HAMMER2_FREEMAP_LEVEL2_RADIX) +
		       HAMMER2_ZONEFM_LEVEL2 * HAMMER2_PBUFSIZE;
		break;
	case HAMMER2_FREEMAP_LEVEL1_RADIX:	/* 2GB */
		KKASSERT(bref->type == HAMMER2_BREF_TYPE_FREEMAP_LEAF);
		KKASSERT(bytes == HAMMER2_FREEMAP_LEVELN_PSIZE);
		off += H2FMBASE(bref->key, HAMMER2_FREEMAP_LEVEL1_RADIX) +
		       HAMMER2_ZONEFM_LEVEL1 * HAMMER2_PBUFSIZE;
		break;
	default:
		panic("freemap: bad radix(2) %p %d\n", bref, bref->keybits);
		/* NOT REACHED */
		break;
	}
	bref->data_off = off | radix;
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
hammer2_freemap_alloc(hammer2_trans_t *trans, hammer2_mount_t *hmp,
		      hammer2_blockref_t *bref, size_t bytes)
{
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

	/*
	 * Freemap blocks themselves are simply assigned from the reserve
	 * area, not allocated from the freemap.
	 */
	if (bref->type == HAMMER2_BREF_TYPE_FREEMAP_NODE ||
	    bref->type == HAMMER2_BREF_TYPE_FREEMAP_LEAF) {
		return(hammer2_freemap_reserve(hmp, bref, radix));
	}

	if (bref->data_off & ~HAMMER2_OFF_MASK_RADIX)
		hammer2_freemap_free(trans, hmp, bref, 0);

	/*
	 * Normal allocations
	 */
	KKASSERT(bytes >= HAMMER2_MIN_ALLOC && bytes <= HAMMER2_MAX_ALLOC);
	atomic_set_int(&trans->flags, HAMMER2_TRANS_ISALLOCATING);

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
	hammer2_chain_lock(parent, HAMMER2_RESOLVE_ALWAYS);
	error = EAGAIN;
	iter.bnext = iter.bpref;
	iter.loops = 0;

	while (error == EAGAIN) {
		error = hammer2_freemap_try_alloc(trans, &parent, bref,
						  radix, &iter);
	}
	hmp->heur_freemap[hindex] = iter.bnext;
	hammer2_chain_unlock(parent);

	atomic_clear_int(&trans->flags, HAMMER2_TRANS_ISALLOCATING);

	return (error);
}

static int
hammer2_freemap_try_alloc(hammer2_trans_t *trans, hammer2_chain_t **parentp,
			  hammer2_blockref_t *bref, int radix,
			  hammer2_fiterate_t *iter)
{
	hammer2_mount_t *hmp = (*parentp)->hmp;
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
				     HAMMER2_LOOKUP_FREEMAP |
				     HAMMER2_LOOKUP_ALWAYS |
				     HAMMER2_LOOKUP_MATCHIND/*XXX*/);
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
		error = hammer2_chain_create(trans, parentp, &chain,
				     key, HAMMER2_FREEMAP_LEVEL1_RADIX,
				     HAMMER2_BREF_TYPE_FREEMAP_LEAF,
				     HAMMER2_FREEMAP_LEVELN_PSIZE);
		if (error == 0) {
			hammer2_chain_modify(trans, &chain, 0);
			bzero(&chain->data->bmdata[0],
			      HAMMER2_FREEMAP_LEVELN_PSIZE);
			chain->bref.check.freemap.bigmask = (uint32_t)-1;
			chain->bref.check.freemap.avail = l1size;
			/* bref.methods should already be inherited */

			hammer2_freemap_init(trans, hmp, key, chain);
		}
	} else if ((chain->bref.check.freemap.bigmask & (1 << radix)) == 0) {
		/*
		 * Already flagged as not having enough space
		 */
		error = ENOSPC;
	} else {
		/*
		 * Modify existing chain to setup for adjustment.
		 */
		hammer2_chain_modify(trans, &chain, 0);
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

		start = (int)((iter->bnext - key) >>
			      HAMMER2_FREEMAP_LEVEL0_RADIX);
		KKASSERT(start >= 0 && start < HAMMER2_FREEMAP_COUNT);
		hammer2_chain_modify(trans, &chain, 0);

		error = ENOSPC;
		for (count = 0; count < HAMMER2_FREEMAP_COUNT; ++count) {
			if (start + count >= HAMMER2_FREEMAP_COUNT &&
			    start - count < 0) {
				break;
			}
			n = start + count;
			bmap = &chain->data->bmdata[n];
			if (n < HAMMER2_FREEMAP_COUNT && bmap->avail &&
			    (bmap->class == 0 || bmap->class == class)) {
				base_key = key + n * l0size;
				error = hammer2_bmap_alloc(trans, hmp, bmap,
							   class, n, radix,
							   &base_key);
				if (error != ENOSPC) {
					key = base_key;
					break;
				}
			}
			n = start - count;
			bmap = &chain->data->bmdata[n];
			if (n >= 0 && bmap->avail &&
			    (bmap->class == 0 || bmap->class == class)) {
				base_key = key + n * l0size;
				error = hammer2_bmap_alloc(trans, hmp, bmap,
							   class, n, radix,
							   &base_key);
				if (error != ENOSPC) {
					key = base_key;
					break;
				}
			}
		}
		if (error == ENOSPC)
			chain->bref.check.freemap.bigmask &= ~(1 << radix);
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
		error = hammer2_freemap_iterate(trans, parentp, &chain, iter);
	}

	/*
	 * Cleanup
	 */
	if (chain)
		hammer2_chain_unlock(chain);
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
hammer2_bmap_alloc(hammer2_trans_t *trans, hammer2_mount_t *hmp,
		   hammer2_bmap_data_t *bmap,
		   uint16_t class, int n, int radix, hammer2_key_t *basep)
{
	struct buf *bp;
	size_t size;
	size_t bsize;
	int bmradix;
	uint32_t bmmask;
	int offset;
	int i;
	int j;

	/*
	 * Take into account 2-bits per block when calculating bmradix.
	 */
	size = (size_t)1 << radix;

	if (radix <= HAMMER2_FREEMAP_BLOCK_RADIX) {
		bmradix = 2;
		bsize = HAMMER2_FREEMAP_BLOCK_SIZE;
		/* (16K) 2 bits per allocation block */
	} else {
		bmradix = 2 << (radix - HAMMER2_FREEMAP_BLOCK_RADIX);
		bsize = size;
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
	 * Beware of hardware artifacts when bmradix == 32 (intermediate
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
			 (bmap->linear & (HAMMER2_MIN_ALLOC - 1)) == 0);
		offset = bmap->linear;
		i = offset / (HAMMER2_SEGSIZE / 8);
		j = (offset / (HAMMER2_FREEMAP_BLOCK_SIZE / 2)) & 30;
		bmmask = (bmradix == 32) ?
			 0xFFFFFFFFU : (1 << bmradix) - 1;
		bmmask <<= j;
		bmap->linear = offset + size;
	} else {
		for (i = 0; i < 8; ++i) {
			bmmask = (bmradix == 32) ?
				 0xFFFFFFFFU : (1 << bmradix) - 1;
			for (j = 0; j < 32; j += bmradix) {
				if ((bmap->bitmap[i] & bmmask) == 0)
					goto success;
				bmmask <<= bmradix;
			}
		}
		/*fragments might remain*/
		/*KKASSERT(bmap->avail == 0);*/
		return (ENOSPC);
success:
		offset = i * (HAMMER2_SEGSIZE / 8) +
			 (j * (HAMMER2_FREEMAP_BLOCK_SIZE / 2));
		if (size & HAMMER2_FREEMAP_BLOCK_MASK)
			bmap->linear = offset + size;
	}

	KKASSERT(i >= 0 && i < 8);	/* 8 x 16 -> 128 x 16K -> 2MB */

	/*
	 * Optimize the buffer cache to avoid unnecessary read-before-write
	 * operations.
	 *
	 * The device block size could be larger than the allocation size
	 * so the actual bitmap test is somewhat more involved.  We have
	 * to use a compatible buffer size for this operation.
	 */
	if ((bmap->bitmap[i] & bmmask) == 0 &&
	    hammer2_devblksize(size) != size) {
		size_t psize = hammer2_devblksize(size);
		hammer2_off_t pmask = (hammer2_off_t)psize - 1;
		int pbmradix = 2 << (hammer2_devblkradix(radix) -
				     HAMMER2_FREEMAP_BLOCK_RADIX);
		uint32_t pbmmask;

		pbmmask = (pbmradix == 32) ? 0xFFFFFFFFU : (1 << pbmradix) - 1;
		while ((pbmmask & bmmask) == 0)
			pbmmask <<= pbmradix;

#if 0
		kprintf("%016jx mask %08x %08x %08x (%zd/%zd)\n",
			*basep + offset, bmap->bitmap[i],
			pbmmask, bmmask, size, psize);
#endif

		if ((bmap->bitmap[i] & pbmmask) == 0) {
			bp = getblk(hmp->devvp, *basep + (offset & ~pmask),
				    psize, GETBLK_NOWAIT, 0);
			if (bp) {
				if ((bp->b_flags & B_CACHE) == 0)
					vfs_bio_clrbuf(bp);
				bp->b_flags |= B_CACHE;
				bqrelse(bp);
			}
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
	 * Adjust the linear iterator, set the radix if necessary (might as
	 * well just set it unconditionally), adjust *basep to return the
	 * allocated data offset.
	 */
	bmap->bitmap[i] |= bmmask;
	bmap->class = class;
	bmap->avail -= size;
	*basep += offset;

	hammer2_voldata_lock(hmp);
	hmp->voldata.allocator_free -= size;  /* XXX */
	hammer2_voldata_unlock(hmp, 1);

	return(0);
}

static
void
hammer2_freemap_init(hammer2_trans_t *trans, hammer2_mount_t *hmp,
		     hammer2_key_t key, hammer2_chain_t *chain)
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
			memset(bmap->bitmap, -1,
			       sizeof(bmap->bitmap));
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
hammer2_freemap_iterate(hammer2_trans_t *trans, hammer2_chain_t **parentp,
			hammer2_chain_t **chainp, hammer2_fiterate_t *iter)
{
	hammer2_mount_t *hmp = (*parentp)->hmp;

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
 * Free the specified blockref.  This code is only able to fully free
 * blocks when (how) is non-zero, otherwise the block is marked for
 * the bulk freeing pass to check.
 *
 * Normal use is to only mark inodes as possibly being free.  The underlying
 * file blocks are not necessarily marked.  The bulk freescan can
 * theoretically handle the case.
 *
 * XXX currently disabled when how == 0 (the normal real-time case).  At
 * the moment we depend on the bulk freescan to actually free blocks.  It
 * will still call this routine with a non-zero how to stage possible frees
 * and to do the actual free.
 */
void
hammer2_freemap_free(hammer2_trans_t *trans, hammer2_mount_t *hmp,
		     hammer2_blockref_t *bref, int how)
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
	uint32_t *bitmap;
	const uint32_t bmmask00 = 0;
	uint32_t bmmask01;
	uint32_t bmmask10;
	uint32_t bmmask11;
	size_t bytes;
	uint16_t class;
	int radix;
	int start;
	int count;
	int modified = 0;
	int cache_index = -1;

	radix = (int)data_off & HAMMER2_OFF_MASK_RADIX;
	data_off &= ~HAMMER2_OFF_MASK_RADIX;
	KKASSERT(radix <= HAMMER2_MAX_RADIX);

	bytes = (size_t)1 << radix;
	class = (bref->type << 8) | hammer2_devblkradix(radix);

	/*
	 * We can't free data allocated by newfs_hammer2.
	 * Assert validity.
	 */
	if (data_off < hmp->voldata.allocator_beg)
		return;
	KKASSERT((data_off & HAMMER2_ZONE_MASK64) >= HAMMER2_ZONE_SEG);

	atomic_set_int(&trans->flags, HAMMER2_TRANS_ISALLOCATING);

	/*
	 * Lookup the level1 freemap chain.  The chain must exist.
	 */
	key = H2FMBASE(data_off, HAMMER2_FREEMAP_LEVEL1_RADIX);
	l0size = H2FMSHIFT(HAMMER2_FREEMAP_LEVEL0_RADIX);
	l1size = H2FMSHIFT(HAMMER2_FREEMAP_LEVEL1_RADIX);
	l1mask = l1size - 1;

	parent = &hmp->fchain;
	hammer2_chain_lock(parent, HAMMER2_RESOLVE_ALWAYS);

	chain = hammer2_chain_lookup(&parent, &key_dummy, key, key + l1mask,
				     &cache_index,
				     HAMMER2_LOOKUP_FREEMAP |
				     HAMMER2_LOOKUP_ALWAYS |
				     HAMMER2_LOOKUP_MATCHIND/*XXX*/);
	if (chain == NULL) {
		kprintf("hammer2_freemap_free: %016jx: no chain\n",
			(intmax_t)bref->data_off);
		hammer2_chain_unlock(parent);
		return;
	}
	KKASSERT(chain->bref.type == HAMMER2_BREF_TYPE_FREEMAP_LEAF);

	/*
	 * Find the bmap entry (covering a 2MB swath)
	 * Find the bitmap array index
	 * Find the bitmap bit index (runs in 2-bit pairs)
	 */
	bmap = &chain->data->bmdata[(int)(data_off >> HAMMER2_SEGRADIX) &
				    (HAMMER2_FREEMAP_COUNT - 1)];
	bitmap = &bmap->bitmap[(int)(data_off >> (HAMMER2_SEGRADIX - 3)) & 7];

	start = ((int)(data_off >> HAMMER2_FREEMAP_BLOCK_RADIX) & 15) * 2;
	bmmask01 = 1 << start;
	bmmask10 = 2 << start;
	bmmask11 = 3 << start;

	/*
	 * Fixup the bitmap
	 */
	if (radix < HAMMER2_FREEMAP_BLOCK_RADIX) {
		count = 1;
		how = 0;	/* partial block, cannot set to 00 */
	} else {
		count = 1 << (radix - HAMMER2_FREEMAP_BLOCK_RADIX);
	}

	while (count) {
		KKASSERT(bmmask11);
		KKASSERT((*bitmap & bmmask11) != bmmask00);
		if ((*bitmap & bmmask11) == bmmask11) {
			if (!modified) {
				hammer2_chain_modify(trans, &chain, 0);
				modified = 1;
	bmap = &chain->data->bmdata[(int)(data_off >> HAMMER2_SEGRADIX) &
				    (HAMMER2_FREEMAP_COUNT - 1)];
	bitmap = &bmap->bitmap[(int)(data_off >> (HAMMER2_SEGRADIX - 3)) & 7];
			}
			if (how)
				*bitmap &= ~bmmask11;
			else
				*bitmap = (*bitmap & ~bmmask11) | bmmask10;
		} else if ((*bitmap & bmmask11) == bmmask10) {
			if (how) {
				if (!modified) {
					hammer2_chain_modify(trans, &chain, 0);
					modified = 1;
	bmap = &chain->data->bmdata[(int)(data_off >> HAMMER2_SEGRADIX) &
				    (HAMMER2_FREEMAP_COUNT - 1)];
	bitmap = &bmap->bitmap[(int)(data_off >> (HAMMER2_SEGRADIX - 3)) & 7];
				}
				*bitmap &= ~bmmask11;
			}
		} else if ((*bitmap & bmmask11) == bmmask01) {
			KKASSERT(0);
		}
		--count;
		bmmask01 <<= 2;
		bmmask10 <<= 2;
		bmmask11 <<= 2;
	}
	if (how && modified) {
		bmap->avail += 1 << radix;
		KKASSERT(bmap->avail <= HAMMER2_SEGSIZE);
		if (bmap->avail == HAMMER2_SEGSIZE &&
		    bmap->bitmap[0] == 0 &&
		    bmap->bitmap[1] == 0 &&
		    bmap->bitmap[2] == 0 &&
		    bmap->bitmap[3] == 0 &&
		    bmap->bitmap[4] == 0 &&
		    bmap->bitmap[5] == 0 &&
		    bmap->bitmap[6] == 0 &&
		    bmap->bitmap[7] == 0) {
			key = H2FMBASE(data_off, HAMMER2_FREEMAP_LEVEL0_RADIX);
			kprintf("Freeseg %016jx\n", (intmax_t)key);
			bmap->class = 0;
		}
	}

	/*
	 * chain->bref.check.freemap.bigmask (XXX)
	 */
	if (modified)
		chain->bref.check.freemap.bigmask |= 1 << radix;

	hammer2_chain_unlock(chain);
	hammer2_chain_unlock(parent);

	atomic_clear_int(&trans->flags, HAMMER2_TRANS_ISALLOCATING);
}
