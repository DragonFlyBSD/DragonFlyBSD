/*
 * Copyright (c) 2013-2015 The DragonFly Project.  All rights reserved.
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
#include <vm/vm_kern.h>
#include <vm/vm_extern.h>

#include "hammer2.h"

/*
 * breadth-first search
 */
typedef struct hammer2_chain_save {
	TAILQ_ENTRY(hammer2_chain_save)	entry;
	hammer2_chain_t	*parent;
} hammer2_chain_save_t;

TAILQ_HEAD(hammer2_chain_save_list, hammer2_chain_save);
typedef struct hammer2_chain_save_list hammer2_chain_save_list_t;

/*
 * General bulk scan function with callback.  Called with a referenced
 * but UNLOCKED parent.  The original parent is returned in the same state.
 */
int
hammer2_bulk_scan(hammer2_trans_t *trans, hammer2_chain_t *parent,
		  int (*func)(hammer2_chain_t *chain, void *info),
		  void *info)
{
	hammer2_chain_save_list_t list;
	hammer2_chain_save_t *save;
	int doabort = 0;

	TAILQ_INIT(&list);
	hammer2_chain_ref(parent);
	save = kmalloc(sizeof(*save), M_HAMMER2, M_WAITOK | M_ZERO);
	save->parent = parent;
	TAILQ_INSERT_TAIL(&list, save, entry);

	while ((save = TAILQ_FIRST(&list)) != NULL) {
		hammer2_chain_t *chain;
		int cache_index;

		TAILQ_REMOVE(&list, save, entry);

		parent = save->parent;
		save->parent = NULL;
		chain = NULL;
		cache_index = -1;

		/*
		 * lock the parent, the lock eats the ref.
		 */
		hammer2_chain_lock(parent, HAMMER2_RESOLVE_ALWAYS |
					   HAMMER2_RESOLVE_SHARED |
					   HAMMER2_RESOLVE_NOREF);

		/*
		 * Generally loop on the contents if we have not been flagged
		 * for abort.
		 */
		while ((doabort & HAMMER2_BULK_ABORT) == 0) {
			chain = hammer2_chain_scan(parent, chain, &cache_index,
						   HAMMER2_LOOKUP_NODATA |
						   HAMMER2_LOOKUP_SHARED);
			if (chain == NULL)
				break;
			doabort |= func(chain, info);

			if (doabort & HAMMER2_BULK_ABORT) {
				hammer2_chain_unlock(chain);
				chain = NULL;
				break;
			}
			switch(chain->bref.type) {
			case HAMMER2_BREF_TYPE_INODE:
			case HAMMER2_BREF_TYPE_FREEMAP_NODE:
			case HAMMER2_BREF_TYPE_INDIRECT:
			case HAMMER2_BREF_TYPE_VOLUME:
			case HAMMER2_BREF_TYPE_FREEMAP:
				/*
				 * Breadth-first scan.  Chain is referenced
				 * to save for later and will be unlocked on
				 * our loop (so it isn't left locked while on
				 * the list).
				 */
				if (save == NULL) {
					save = kmalloc(sizeof(*save),
						       M_HAMMER2,
						       M_WAITOK | M_ZERO);
				}
				hammer2_chain_ref(chain);
				save->parent = chain;
				TAILQ_INSERT_TAIL(&list, save, entry);
				save = NULL;
				break;
			default:
				/* does not recurse */
				break;
			}
		}

		/*
		 * Releases the lock and the ref the lock inherited.  Free
		 * save structure if we didn't recycle it above.
		 */
		hammer2_chain_unlock(parent);
		if (save)
			kfree(save, M_HAMMER2);
	}
	return doabort;
}

/*
 * Bulkfree algorithm -
 *
 * DoTwice {
 *	flush sync
 *	Scan the whole topology and build the freemap
 *	** -> 11 during scan for all elements scanned (and thus not free)
 *	11 -> 10 after scan if allocated in-topo and free in-memory, mark 10
 *	10 -> 00 after scan if possibly-free in-topo and free in-memory mark 00
 * }
 *
 * Adjustment of the freemap ->10 and ->00 cannot occur until the topology
 * scan is complete.  The scan runs concurrentlyt with normal filesystem
 * operations and any allocation will also remark the freemap bitmap 11.
 * We handle races by performing two scans and only changing the map to
 * fully free (00) if both passes believe it is free.
 *
 * Temporary memory in multiples of 64KB is required to reconstruct leaf
 * hammer2_bmap_data blocks so they can later be compared against the live
 * freemap.  Each 64KB block represents 128 x 16KB x 1024 = ~2 GB of storage.
 * A 32MB save area thus represents around ~1 TB.  The temporary memory
 * allocated can be specified.  If it is not sufficient multiple topology
 * passes will be made.
 */

/*
 * Bulkfree callback info
 */
typedef struct hammer2_bulkfree_info {
	kmem_anon_desc_t	kp;
	hammer2_off_t		sbase;		/* sub-loop iteration */
	hammer2_off_t		sstop;
	hammer2_bmap_data_t	*bmap;
} hammer2_bulkfree_info_t;

static int h2_bulkfree_callback(hammer2_chain_t *chain, void *info);
static void h2_bulkfree_sync(hammer2_bulkfree_info_t *cbinfo);

int
hammer2_bulkfree_pass(hammer2_mount_t *hmp, hammer2_ioc_bulkfree_t *bfi)
{
	hammer2_trans_t trans;
	hammer2_bulkfree_info_t cbinfo;
	size_t size;
	int doabort = 0;

	bzero(&cbinfo, sizeof(cbinfo));
	size = (bfi->size + HAMMER2_FREEMAP_LEVELN_PSIZE - 1) &
	       ~(size_t)(HAMMER2_FREEMAP_LEVELN_PSIZE - 1);
	cbinfo.bmap = kmem_alloc_swapbacked(&cbinfo.kp, size);
	cbinfo.sbase = 0;
	cbinfo.sstop = cbinfo.sbase +
		       size / HAMMER2_FREEMAP_LEVELN_PSIZE *	/* per 64KB */
		       HAMMER2_FREEMAP_LEVEL1_SIZE;		/* 2GB */
	/* XXX also limit to volume size */

	hammer2_trans_init(&trans, hmp->spmp, 0);
	doabort |= hammer2_bulk_scan(&trans, &hmp->vchain,
					h2_bulkfree_callback, &cbinfo);
	h2_bulkfree_sync(&cbinfo);
	hammer2_trans_done(&trans);

	kmem_free_swapbacked(&cbinfo.kp);

	return doabort;
}

static int
h2_bulkfree_callback(hammer2_chain_t *chain, void *info)
{
	hammer2_bulkfree_info_t *cbinfo = info;
	hammer2_bmap_data_t *bmap;
	hammer2_off_t data_off;
	uint16_t class;
	size_t bytes;
	int radix;
	int error;

	error = 0;

#if 0
	kprintf("scan chain %016jx %016jx/%-2d type=%02x\n",
		(intmax_t)chain->bref.data_off,
		(intmax_t)chain->bref.key,
		chain->bref.keybits,
		chain->bref.type);
#endif

	/*
	 * Calculate the data offset and determine if it is within
	 * the current freemap range being gathered.
	 */
	data_off = chain->bref.data_off & ~HAMMER2_OFF_MASK_RADIX;
	if (data_off < cbinfo->sbase || data_off > cbinfo->sstop)
		return 0;
	if (data_off < chain->hmp->voldata.allocator_beg)
		return 0;
	if (data_off > chain->hmp->voldata.volu_size)
		return 0;

	/*
	 * Calculate the information needed to generate the in-memory
	 * freemap record.
	 *
	 * Hammer2 does not allow allocations to cross the L1 (2GB) boundary,
	 * it's a problem if it does.  (Or L0 (2MB) for that matter).
	 */
	radix = (int)(chain->bref.data_off & HAMMER2_OFF_MASK_RADIX);
	bytes = (size_t)1 << radix;
	class = (chain->bref.type << 8) | hammer2_devblkradix(radix);

	if (data_off + bytes > cbinfo->sstop) {
		kprintf("hammer2_bulkfree_scan: illegal 2GB boundary "
			"%016jx %016jx/%d\n",
			(intmax_t)chain->bref.data_off,
			(intmax_t)chain->bref.key,
			chain->bref.keybits);
		bytes = cbinfo->sstop - data_off;	/* XXX */
	}

	/*
	 * Convert to a storage offset relative to the beginning of the
	 * storage range we are collecting.  Then lookup the level0 bmap entry.
	 */
	data_off -= cbinfo->sbase;
	bmap = cbinfo->bmap + (data_off >> HAMMER2_FREEMAP_LEVEL0_RADIX);

	/*
	 * Convert data_off to a bmap-relative value (~2MB storage range).
	 * Adjust linear, class, and avail.
	 *
	 * Hammer2 does not allow allocations to cross the L0 (2MB) boundary,
	 */
	data_off &= HAMMER2_FREEMAP_LEVEL0_MASK;
	if (data_off + bytes > HAMMER2_FREEMAP_LEVEL0_SIZE) {
		kprintf("hammer2_bulkfree_scan: illegal 2MB boundary "
			"%016jx %016jx/%d\n",
			(intmax_t)chain->bref.data_off,
			(intmax_t)chain->bref.key,
			chain->bref.keybits);
		bytes = HAMMER2_FREEMAP_LEVEL0_SIZE - data_off;
	}

	if (bmap->class == 0) {
		bmap->class = class;
		bmap->avail = HAMMER2_FREEMAP_LEVEL0_SIZE;
	}
	if (bmap->class != class) {
		kprintf("hammer2_bulkfree_scan: illegal mixed class "
			"%016jx %016jx/%d (%04x vs %04x)\n",
			(intmax_t)chain->bref.data_off,
			(intmax_t)chain->bref.key,
			chain->bref.keybits,
			class, bmap->class);
	}
	if (bmap->linear < (int32_t)data_off + (int32_t)bytes)
		bmap->linear = (int32_t)data_off + (int32_t)bytes;

	/*
	 * Adjust the uint32_t bitmap[8].  2 bits per entry, to code 11.
	 * Shortcut aligned 64KB allocations.
	 *
	 * NOTE: The allocation can be smaller than HAMMER2_FREEMAP_BLOCK_SIZE.
	 */
	while (bytes > 0) {
		int bindex;
		int bmask;

		bindex = (int)data_off >> (HAMMER2_FREEMAP_BLOCK_RADIX +
					   HAMMER2_BMAP_INDEX_RADIX);
		bmask = 3 << ((((int)data_off & HAMMER2_BMAP_INDEX_MASK) >>
			     HAMMER2_FREEMAP_BLOCK_RADIX) << 1);
		if ((bmap->bitmap[bindex] & bmask) == 0) {
			if (bytes >= HAMMER2_FREEMAP_BLOCK_SIZE)
				bmap->avail -= HAMMER2_FREEMAP_BLOCK_SIZE;
			else
				bmap->avail -= bytes;
			bmap->bitmap[bindex] |= bmask;
		}
		data_off += HAMMER2_FREEMAP_BLOCK_SIZE;
		if (bytes < HAMMER2_FREEMAP_BLOCK_SIZE)
			bytes = 0;
		else
			bytes -= HAMMER2_FREEMAP_BLOCK_SIZE;
	}
	return error;
}

static void
h2_bulkfree_sync(hammer2_bulkfree_info_t *cbinfo)
{
	hammer2_off_t data_off;
	hammer2_bmap_data_t *bmap;
	int didl1;

	kprintf("hammer2_bulkfree - range %016jx-%016jx\n",
		(intmax_t)cbinfo->sbase,
		(intmax_t)cbinfo->sstop);
		
	data_off = cbinfo->sbase;
	bmap = cbinfo->bmap;
	didl1 = 0;

	while (data_off < cbinfo->sstop) {
		if (bmap->class) {
			kprintf("%016jx %04d.%04x (avail=%7d) "
				"%08x %08x %08x %08x %08x %08x %08x %08x\n",
				(intmax_t)data_off,
				(int)((data_off &
				       HAMMER2_FREEMAP_LEVEL1_MASK) >>
				      HAMMER2_FREEMAP_LEVEL0_RADIX),
				bmap->class,
				bmap->avail,
				bmap->bitmap[0], bmap->bitmap[1],
				bmap->bitmap[2], bmap->bitmap[3],
				bmap->bitmap[4], bmap->bitmap[5],
				bmap->bitmap[6], bmap->bitmap[7]);
			didl1 = 1;
		}
		data_off += HAMMER2_FREEMAP_LEVEL0_SIZE;
		++bmap;
		if ((data_off & HAMMER2_FREEMAP_LEVEL1_MASK) == 0) {
			if (didl1)
				kprintf("\n");
			didl1 = 0;
		}
	}
}
