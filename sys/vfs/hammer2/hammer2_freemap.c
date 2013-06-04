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

#define USE_SIMPLE_ALLOC	0

#if !USE_SIMPLE_ALLOC

static int hammer2_freemap_try_alloc(hammer2_trans_t *trans,
			hammer2_chain_t **parentp, hammer2_blockref_t *bref,
			int radix, hammer2_off_t bpref, hammer2_off_t *bnext);
static int hammer2_freemap_iterate(hammer2_trans_t *trans,
			hammer2_chain_t **parentp, hammer2_chain_t **chainp,
			hammer2_off_t bpref, hammer2_off_t *bnextp);

#endif

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
		KKASSERT(off < HAMMER2_ZONE_FREEMAP_D + 8);

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
		KKASSERT(bref->type == HAMMER2_BREF_TYPE_FREEMAP_NODE);
		KKASSERT(bytes == HAMMER2_FREEMAP_LEVELN_PSIZE);
		off += H2FMBASE(bref->key, HAMMER2_FREEMAP_LEVEL1_RADIX) +
		       HAMMER2_ZONEFM_LEVEL1 * HAMMER2_PBUFSIZE;
		break;
	case HAMMER2_FREEMAP_LEVEL0_RADIX:	/* 2MB (256 byte bitmap) */
		/*
		 * Terminal bitmap, start with 2GB base, then offset by
		 * 256 bytes of device offset per 2MB of logical space
		 * (8 bits per byte, 1024 byte allocation chunking).
		 */
		KKASSERT(bref->type == HAMMER2_BREF_TYPE_FREEMAP_LEAF);
		KKASSERT(bytes == HAMMER2_FREEMAP_LEVEL0_PSIZE);
		off += H2FMBASE(bref->key, HAMMER2_FREEMAP_LEVEL1_RADIX) +
		       HAMMER2_ZONEFM_LEVEL0 * HAMMER2_PBUFSIZE;

		off += ((bref->key >> HAMMER2_FREEMAP_LEVEL0_RADIX) &
		        ((1 << (HAMMER2_FREEMAP_LEVEL1_RADIX -
			       HAMMER2_FREEMAP_LEVEL0_RADIX)) - 1)) *
			HAMMER2_FREEMAP_LEVEL0_PSIZE;
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
 * Allocate media space, returning a combined data offset and radix.
 * THIS ROUTINE IS USE FOR DEBUGGING ONLY.
 *
 * This version of the routine is ONLY usable for testing and debug
 * purposes and only if the filesystem never instantiated an actual
 * freemap.  It uses the initial allocation iterator that newfs_hammer2
 * used to build the filesystem to allocate new space and is not capable
 * of dealing with freed space.
 */
#if USE_SIMPLE_ALLOC

static
int
hammer2_freemap_simple_alloc(hammer2_mount_t *hmp, hammer2_blockref_t *bref,
			     int radix)
{
	hammer2_off_t data_off;
	hammer2_off_t data_next;
	hammer2_freecache_t *fc;
	/*struct buf *bp;*/
	size_t bytes;
	int fctype;

	bytes = (size_t)(1 << radix);
	KKASSERT(bytes >= HAMMER2_MIN_ALLOC &&
		 bytes <= HAMMER2_MAX_ALLOC);

	/*
	 * Must not be used if the filesystem is using a real freemap.
	 */
	KKASSERT(hmp->voldata.freemap_blockset.blockref[0].data_off == 0);

	switch(bref->type) {
	case HAMMER2_BREF_TYPE_INODE:
		fctype = HAMMER2_FREECACHE_INODE;
		break;
	case HAMMER2_BREF_TYPE_INDIRECT:
		fctype = HAMMER2_FREECACHE_INODE;
		break;
	case HAMMER2_BREF_TYPE_DATA:
		fctype = HAMMER2_FREECACHE_DATA;
		break;
	default:
		fctype = HAMMER2_FREECACHE_DATA;
		break;
	}

	if (radix <= HAMMER2_MAX_RADIX)
		fc = &hmp->freecache[fctype][radix];
	else
		fc = NULL;

	lockmgr(&hmp->alloclk, LK_EXCLUSIVE);
	if (fc && fc->single) {
		/*
		 * Allocate from our single-block cache.
		 */
		data_off = fc->single;
		fc->single = 0;
	} else if (fc && fc->bulk) {
		/*
		 * Allocate from our packing cache.
		 */
		data_off = fc->bulk;
		fc->bulk += bytes;
		if ((fc->bulk & HAMMER2_SEGMASK) == 0)
			fc->bulk = 0;
	} else {
		/*
		 * Allocate from the allocation iterator using a SEGSIZE
		 * aligned block and reload the packing cache if possible.
		 *
		 * Skip reserved areas at the beginning of each zone.
		 */
		hammer2_voldata_lock(hmp);
		data_off = hmp->voldata.allocator_beg;
		data_off = (data_off + HAMMER2_SEGMASK64) & ~HAMMER2_SEGMASK64;
		if ((data_off & HAMMER2_ZONE_MASK64) < HAMMER2_ZONE_SEG) {
			KKASSERT((data_off & HAMMER2_ZONE_MASK64) == 0);
			data_off += HAMMER2_ZONE_SEG64;
		}
		data_next = data_off + bytes;

		if ((data_next & HAMMER2_SEGMASK) == 0) {
			hmp->voldata.allocator_beg = data_next;
		} else {
			KKASSERT(radix <= HAMMER2_MAX_RADIX);
			hmp->voldata.allocator_beg =
					(data_next + HAMMER2_SEGMASK64) &
					~HAMMER2_SEGMASK64;
			fc->bulk = data_next;
		}
		hammer2_voldata_unlock(hmp, 1);
	}
	lockmgr(&hmp->alloclk, LK_RELEASE);

	bref->data_off = data_off | radix;
	return (0);
}

#endif

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
hammer2_freemap_alloc(hammer2_trans_t *trans,
		      hammer2_blockref_t *bref, size_t bytes)
{
	hammer2_mount_t *hmp = trans->hmp;
	hammer2_chain_t *parent;
	hammer2_off_t bpref;
	hammer2_off_t bnext;
	int freemap_radix;
	int radix;
	int error;

	/*
	 * Validate the allocation size.  It must be a power of 2.
	 */
	radix = hammer2_getradix(bytes);
	KKASSERT((size_t)1 << radix == bytes);

	/*
	 * Freemap elements are assigned from the reserve area.
	 * Note that FREEMAP_LEVEL0_PSIZE is 256 bytes which is
	 * allowed for this case.
	 */
	if (bref->type == HAMMER2_BREF_TYPE_FREEMAP_NODE ||
	    bref->type == HAMMER2_BREF_TYPE_FREEMAP_LEAF) {
		return(hammer2_freemap_reserve(hmp, bref, radix));
	}
#if USE_SIMPLE_ALLOC
	return (hammer2_freemap_simple_alloc(hmp, bref, radix));
#else

	KKASSERT(bytes >= HAMMER2_MIN_ALLOC &&
		 bytes <= HAMMER2_MAX_ALLOC);

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
	freemap_radix = hammer2_freemapradix(radix);
#if 0
	if (bref->data_off & ~HAMMER2_OFF_MASK_RADIX)
		bpref = bref->data_off & ~HAMMER2_OFF_MASK_RADIX;
	else if (trans->tmp_bpref)
		bpref = trans->tmp_bpref;
	else if (trans->tmp_ip)
		bpref = trans->tmp_ip->chain->bref.data_off;
	else
#endif
	KKASSERT(radix >= 0 && radix <= HAMMER2_MAX_RADIX);
	bpref = hmp->heur_freemap[freemap_radix];

	/*
	 * Make sure bpref is in-bounds.  It's ok if bpref covers a zone's
	 * reserved area, the try code will iterate past it.
	 */
	if (bpref > hmp->voldata.volu_size)
		bpref = hmp->voldata.volu_size - 1;

	/*
	 * Iterate the freemap looking for free space before and after.
	 */
	parent = &hmp->fchain;
	hammer2_chain_lock(parent, HAMMER2_RESOLVE_ALWAYS);
	error = EAGAIN;
	bnext = bpref;

	while (error == EAGAIN) {
		error = hammer2_freemap_try_alloc(trans, &parent, bref,
						  radix, bpref, &bnext);
	}
	hmp->heur_freemap[freemap_radix] = bnext;
	hammer2_chain_unlock(parent);

	return (error);
#endif
}

#if !USE_SIMPLE_ALLOC

/*
 * Attempt to allocate (1 << radix) bytes from the freemap at bnext.
 * Return 0 on success with the bref appropriately updated, non-zero
 * on failure.  Updates bnextp and returns EAGAIN to continue the
 * iteration.
 *
 * This function will create missing freemap infrastructure as well as
 * properly initialize reserved areas as already having been allocated.
 */
static __inline
int
countbits(uint64_t *data)
{
	int i;
	int r = 0;
	uint64_t v;

	for (i = 0; i < 32; ++i) {
		v = data[i];
		while (v) {
			if (v & 1)
				++r;
			v >>= 1;
		}
	}
	return(r);
}

static int
hammer2_freemap_try_alloc(hammer2_trans_t *trans, hammer2_chain_t **parentp,
			  hammer2_blockref_t *bref, int radix,
			  hammer2_off_t bpref, hammer2_off_t *bnextp)
{
	hammer2_mount_t *hmp = trans->hmp;
	hammer2_off_t l0mask;
	hammer2_off_t l0size;
	hammer2_chain_t *chain;
	hammer2_off_t key;
	hammer2_off_t tmp;
	size_t bytes;
	uint64_t mask;
	uint64_t tmp_mask;
	uint64_t *data;
	int error = 0;
	int bits;
	int index;
	int count;
	int subindex;
	int freemap_radix;
	int devblk_radix;

	/*
	 * Calculate the number of bytes being allocated, the number
	 * of contiguous bits of bitmap being allocated, and the bitmap
	 * mask.
	 *
	 * WARNING! cpu hardware may mask bits == 64 -> 0 and blow up the
	 *	    mask calculation.
	 */
	bytes = (size_t)1 << radix;
	bits = 1 << (radix - HAMMER2_MIN_RADIX);
	mask = (bits == 64) ? (uint64_t)-1 : (((uint64_t)1 << bits) - 1);

	devblk_radix = hammer2_devblkradix(radix);
	freemap_radix = hammer2_freemapradix(radix);

	/*
	 * Lookup the level0 freemap chain, creating and initializing one
	 * if necessary.  Intermediate levels will be created automatically
	 * when necessary by hammer2_chain_create().
	 */
	key = H2FMBASE(*bnextp, HAMMER2_FREEMAP_LEVEL0_RADIX);
	l0mask = H2FMSHIFT(HAMMER2_FREEMAP_LEVEL0_RADIX) - 1;
	l0size = H2FMSHIFT(HAMMER2_FREEMAP_LEVEL0_RADIX);

	chain = hammer2_chain_lookup(parentp, key, key + l0mask,
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
		kprintf("freemap create L0 @ %016jx bpref %016jx\n",
			key, bpref);
#endif
		error = hammer2_chain_create(trans, parentp, &chain,
				     key, HAMMER2_FREEMAP_LEVEL0_RADIX,
				     HAMMER2_BREF_TYPE_FREEMAP_LEAF,
				     HAMMER2_FREEMAP_LEVEL0_PSIZE);
		if (error == 0) {
			hammer2_chain_modify(trans, &chain, 0);
			bzero(chain->data->bmdata.array,
			      HAMMER2_FREEMAP_LEVEL0_PSIZE);
			chain->bref.check.freemap.biggest =
					HAMMER2_FREEMAP_LEVEL0_RADIX;
			chain->bref.check.freemap.avail = l0size;
			chain->bref.check.freemap.radix = freemap_radix;

			/*
			 * Preset bitmap for existing static allocations.
			 * 64-bit-align so we don't have to worry about
			 * endian for the memset().
			 */
			tmp = (hmp->voldata.allocator_beg +
			       HAMMER2_MAX_ALLOC - 1) &
			      ~(hammer2_off_t)(HAMMER2_MAX_ALLOC - 1);
			if (key < tmp) {
				if (key + l0size <= tmp) {
					memset(chain->data->bmdata.array, -1,
					       l0size / HAMMER2_MIN_ALLOC / 8);
					chain->bref.check.freemap.avail = 0;
				} else {
					count = (tmp - key) / HAMMER2_MIN_ALLOC;
					kprintf("Init L0 BASE %d\n", count);
					memset(chain->data->bmdata.array, -1,
					       count / 8);
					chain->bref.check.freemap.avail -=
						count * HAMMER2_MIN_ALLOC;
				}
			}

			/*
			 * Preset bitmap for reserved area.  Calculate
			 * 2GB base.
			 */
			tmp = H2FMBASE(key, HAMMER2_FREEMAP_LEVEL1_RADIX);
			if (key - tmp < HAMMER2_ZONE_SEG) {
				memset(chain->data->bmdata.array, -1,
				       l0size / HAMMER2_MIN_ALLOC / 8);
				chain->bref.check.freemap.avail = 0;
			}

			/*
			 * Preset bitmap for end of media
			 */
			if (key >= trans->hmp->voldata.volu_size) {
				memset(chain->data->bmdata.array, -1,
				       l0size / HAMMER2_MIN_ALLOC / 8);
				chain->bref.check.freemap.avail = 0;
			}
		}
	} else if (chain->bref.check.freemap.biggest < radix) {
		/*
		 * Already flagged as not having enough space
		 */
		error = ENOSPC;
	} else if (chain->bref.check.freemap.radix != freemap_radix) {
		/*
		 * Wrong cluster radix, cannot allocate from this leaf.
		 */
		error = ENOSPC;
	} else {
		/*
		 * Modify existing chain to setup for adjustment.
		 */
		hammer2_chain_modify(trans, &chain, 0);
	}
	if (error)
		goto skip;

	/*
	 * Calculate mask and count.  Each bit represents 1KB and (currently)
	 * the maximum allocation is 65536 bytes.  Allocations are always
	 * natively aligned.
	 */
	count = HAMMER2_FREEMAP_LEVEL0_PSIZE / sizeof(uint64_t); /* 32 */
	data = &chain->data->bmdata.array[0];

	tmp_mask = 0; /* avoid compiler warnings */
	subindex = 0; /* avoid compiler warnings */

	/*
	 * Allocate data and meta-data from the beginning and inodes
	 * from the end.
	 */
	for (index = 0; index < count; ++index) {
		if (data[index] == (uint64_t)-1) /* all allocated */
			continue;
		tmp_mask = mask;		 /* iterate */
		for (subindex = 0; subindex < 64; subindex += bits) {
			if ((data[index] & tmp_mask) == 0)
				break;
			tmp_mask <<= bits;
		}
		if (subindex != 64) {
			key += HAMMER2_MIN_ALLOC * 64 * index;
			key += HAMMER2_MIN_ALLOC * subindex;
			break;
		}
	}
	if (index == count)
		error = ENOSPC;

skip:
	if (error == 0) {
		/*
		 * Assert validity.  Must be beyond the static allocator used
		 * by newfs_hammer2 (and thus also beyond the aux area),
		 * not go past the volume size, and must not be in the
		 * reserved segment area for a zone.
		 */
		int prebuf;

		KKASSERT(key >= hmp->voldata.allocator_beg &&
			 key + bytes <= hmp->voldata.volu_size);
		KKASSERT((key & HAMMER2_ZONE_MASK64) >= HAMMER2_ZONE_SEG);

		/*
		 * Modify the chain and set the bitmap appropriately.
		 *
		 * For smaller allocations try to avoid a read-before-write
		 * by priming the buffer cache buffer.  The caller handles
		 * read-avoidance for larger allocations (or more properly,
		 * when the chain is locked).
		 */
		prebuf = 0;
		hammer2_chain_modify(trans, &chain, 0);
		data = &chain->data->bmdata.array[0];
		if (radix != devblk_radix) {
			uint64_t iomask;
			int iobmradix = HAMMER2_MINIORADIX - HAMMER2_MIN_RADIX;
			int ioindex;
			int iobmskip = 1 << iobmradix;

			iomask = ((uint64_t)1 << iobmskip) - 1;
			for (ioindex = 0; ioindex < 64; ioindex += iobmskip) {
				if (tmp_mask & iomask) {
					if ((data[index] & iomask) == 0)
						prebuf = 1;
					break;
				}
				iomask <<= iobmskip;
			}
		}

		KKASSERT((data[index] & tmp_mask) == 0);
		data[index] |= tmp_mask;

		/*
		 * We return the allocated space in bref->data_off.
		 */
		*bnextp = key;
		bref->data_off = key | radix;

		if (prebuf) {
			struct buf *bp;
			hammer2_off_t pbase;
			hammer2_off_t csize;
			hammer2_off_t cmask;

			csize = (hammer2_off_t)1 << devblk_radix;
			cmask = csize - 1;
			pbase = key & ~mask;

			bp = getblk(hmp->devvp, pbase, csize,
				    GETBLK_NOWAIT, 0);
			if (bp) {
				if ((bp->b_flags & B_CACHE) == 0)
					vfs_bio_clrbuf(bp);
				bqrelse(bp);
			}
		}

#if 0
		kprintf("alloc cp=%p %016jx %016jx using %016jx chain->data %d\n",
			chain,
			bref->key, bref->data_off, chain->bref.data_off,
			countbits(data));
#endif
	} else if (error == ENOSPC) {
		/*
		 * Return EAGAIN with next iteration in *bnextp, or
		 * return ENOSPC if the allocation map has been exhausted.
		 */
		if (chain->bref.check.freemap.biggest > radix)
			chain->bref.check.freemap.biggest = radix;
		error = hammer2_freemap_iterate(trans, parentp, &chain,
						bpref, bnextp);
	}

	/*
	 * Cleanup
	 */
	if (chain)
		hammer2_chain_unlock(chain);
	return (error);
}

static int
hammer2_freemap_iterate(hammer2_trans_t *trans, hammer2_chain_t **parentp,
			hammer2_chain_t **chainp,
			hammer2_off_t bpref, hammer2_off_t *bnextp)
{
	*bnextp += H2FMSHIFT(HAMMER2_FREEMAP_LEVEL0_RADIX);
	if (*bnextp >= trans->hmp->voldata.volu_size)
		return (ENOSPC);
	return(EAGAIN);
}

#endif

#if 0

void
hammer2_freemap_free(hammer2_mount_t *hmp, hammer2_off_t data_off, int type)
{
	hammer2_freecache_t *fc;
	int radix;
	int fctype;

	switch(type) {
	case HAMMER2_BREF_TYPE_INODE:
		fctype = HAMMER2_FREECACHE_INODE;
		break;
	case HAMMER2_BREF_TYPE_INDIRECT:
		fctype = HAMMER2_FREECACHE_INODE;
		break;
	case HAMMER2_BREF_TYPE_DATA:
		fctype = HAMMER2_FREECACHE_DATA;
		break;
	default:
		fctype = HAMMER2_FREECACHE_DATA;
		break;
	}
	radix = (int)data_off & HAMMER2_OFF_MASK_RADIX;
	data_off &= ~HAMMER2_OFF_MASK_RADIX;
	if (radix >= HAMMER2_MAX_RADIX)
		return;

	fc = &hmp->freecache[fctype][radix];
	if (fc->single == 0) {
		lockmgr(&hmp->alloclk, LK_EXCLUSIVE);
		fc->single = data_off;
		lockmgr(&hmp->alloclk, LK_RELEASE);
	}
}

#endif

#if 0
/*
 * Allocate media space, returning a combined data offset and radix.
 * Also return the related (device) buffer cache buffer.
 */
hammer2_off_t
hammer2_freemap_alloc_bp(hammer2_mount_t *hmp, size_t bytes, struct buf **bpp)
{
}

#endif
