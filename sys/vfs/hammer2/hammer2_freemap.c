/*
 * Copyright (c) 2011-2012 The DragonFly Project.  All rights reserved.
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

/*
 * Allocate media space, returning a combined data offset and radix.
 *
 * XXX when diving a new full block create a clean empty buffer and bqrelse()
 *     it, so small data structures do not have to issue read-IO when they
 *     do the read-modify-write on the backing store.
 */
hammer2_off_t
hammer2_freemap_alloc(hammer2_mount_t *hmp, int type, size_t bytes)
{
	hammer2_off_t data_off;
	hammer2_off_t data_next;
	/*struct buf *bp;*/
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

	/*
	 * Figure out the base 2 radix of the allocation (rounded up)
	 */
	radix = hammer2_bytes_to_radix(bytes);
	bytes = 1 << radix;

	lockmgr(&hmp->alloclk, LK_EXCLUSIVE);
	if (radix <= HAMMER2_MAX_RADIX && hmp->freecache[fctype][radix]) {
		/*
		 * Allocate from our packing cache
		 */
		data_off = hmp->freecache[fctype][radix];
		hmp->freecache[fctype][radix] += bytes;
		if ((hmp->freecache[fctype][radix] & HAMMER2_SEGMASK) == 0)
			hmp->freecache[fctype][radix] = 0;
	} else {
		/*
		 * Allocate from the allocation iterator using a SEGSIZE
		 * aligned block and reload the packing cache if possible.
		 */
		data_off = hmp->voldata.allocator_beg;
		data_off = (data_off + HAMMER2_SEGMASK64) & ~HAMMER2_SEGMASK64;
		data_next = data_off + bytes;

		if ((data_next & HAMMER2_SEGMASK) == 0) {
			hmp->voldata.allocator_beg = data_next;
		} else {
			KKASSERT(radix <= HAMMER2_MAX_RADIX);
			hmp->voldata.allocator_beg =
					(data_next + HAMMER2_SEGMASK64) &
					~HAMMER2_SEGMASK64;
			hmp->freecache[fctype][radix] = data_next;
		}
	}
	lockmgr(&hmp->alloclk, LK_RELEASE);

#if 0
	/*
	 * Allocations on-media are always in multiples of 64K but
	 * partial-block allocations can be tracked in-memory.
	 *
	 * We can reduce the need for read-modify-write IOs by
	 * telling the kernel that the contents of a new 64K block is
	 * initially good (before we use any of it).
	 *
	 * Worst case is the kernel evicts the buffer and causes HAMMER2's
	 * bread later on to actually issue a read I/O.
	 *
	 * XXX Maybe do this in SEGSIZE increments? Needs a lot of work.
	 *     Also watch out for buffer size mismatches.
	 */
	if (bytes < HAMMER2_MINIOSIZE &&
	    (data_off & (HAMMER2_MINIOSIZE - 1)) == 0) {
		bp = getblk(hmp->devvp, data_off, HAMMER2_MINIOSIZE, 0, 0);
		bp->b_flags |= B_CACHE;
		bp->b_resid = 0;
		bqrelse(bp);
	}
#endif

	if (hammer2_debug & 0x0001) {
		kprintf("hammer2: allocate %d %016jx: %zd\n",
			type, (intmax_t)data_off, bytes);
	}
	return (data_off | radix);
}

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
