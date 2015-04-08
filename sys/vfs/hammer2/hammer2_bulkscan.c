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

	while ((save = TAILQ_FIRST(&list)) != NULL && doabort == 0) {
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
					   HAMMER2_RESOLVE_SHARED);

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
				hammer2_chain_drop(chain);
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
		hammer2_chain_drop(parent);
		if (save)
			kfree(save, M_HAMMER2);
	}

	/*
	 * Cleanup anything left undone due to an abort
	 */
	while ((save = TAILQ_FIRST(&list)) != NULL) {
		TAILQ_REMOVE(&list, save, entry);
		hammer2_chain_drop(save->parent);
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
	hammer2_dev_t		*hmp;
	hammer2_trans_t		*trans;
	kmem_anon_desc_t	kp;
	hammer2_off_t		sbase;		/* sub-loop iteration */
	hammer2_off_t		sstop;
	hammer2_bmap_data_t	*bmap;
	long			count_10_00;
	long			count_11_10;
	long			count_10_11;
	long			count_l0cleans;
	long			count_linadjusts;
	hammer2_off_t		adj_free;
	time_t			save_time;
} hammer2_bulkfree_info_t;

static int h2_bulkfree_callback(hammer2_chain_t *chain, void *info);
static void h2_bulkfree_sync(hammer2_bulkfree_info_t *cbinfo);
static void h2_bulkfree_sync_adjust(hammer2_bulkfree_info_t *cbinfo,
			hammer2_bmap_data_t *live, hammer2_bmap_data_t *bmap);

int
hammer2_bulkfree_pass(hammer2_dev_t *hmp, hammer2_ioc_bulkfree_t *bfi)
{
	hammer2_trans_t trans;
	hammer2_bulkfree_info_t cbinfo;
	hammer2_off_t incr;
	size_t size;
	int doabort = 0;

	/* hammer2_vfs_sync(hmp->mp, MNT_WAIT); XXX */

	bzero(&cbinfo, sizeof(cbinfo));
	size = (bfi->size + HAMMER2_FREEMAP_LEVELN_PSIZE - 1) &
	       ~(size_t)(HAMMER2_FREEMAP_LEVELN_PSIZE - 1);
	cbinfo.trans = &trans;
	cbinfo.hmp = hmp;
	cbinfo.bmap = kmem_alloc_swapbacked(&cbinfo.kp, size);

	/*
	 * Normalize start point to a 2GB boundary.  We operate on a
	 * 64KB leaf bitmap boundary which represents 2GB of storage.
	 */
	cbinfo.sbase = bfi->sbase;
	if (cbinfo.sbase > hmp->voldata.volu_size)
		cbinfo.sbase = hmp->voldata.volu_size;
	cbinfo.sbase &= ~HAMMER2_FREEMAP_LEVEL1_MASK;

	/*
	 * Loop on a full meta-data scan as many times as required to
	 * get through all available storage.
	 */
	while (cbinfo.sbase < hmp->voldata.volu_size) {
		/*
		 * We have enough ram to represent (incr) bytes of storage.
		 * Each 64KB of ram represents 2GB of storage.
		 */
		bzero(cbinfo.bmap, size);
		incr = size / HAMMER2_FREEMAP_LEVELN_PSIZE *
		       HAMMER2_FREEMAP_LEVEL1_SIZE;
		if (hmp->voldata.volu_size - cbinfo.sbase < incr)
			cbinfo.sstop = hmp->voldata.volu_size;
		else
			cbinfo.sstop = cbinfo.sbase + incr;
		kprintf("bulkfree pass %016jx/%jdGB\n",
			(intmax_t)cbinfo.sbase,
			(intmax_t)incr / HAMMER2_FREEMAP_LEVEL1_SIZE);

		hammer2_trans_init(&trans, hmp->spmp, 0);
		doabort |= hammer2_bulk_scan(&trans, &hmp->vchain,
					    h2_bulkfree_callback, &cbinfo);

		/*
		 * If complete scan succeeded we can synchronize our
		 * in-memory freemap against live storage.  If an abort
		 * did occur we cannot safely synchronize our partially
		 * filled-out in-memory freemap.
		 */
		if (doabort == 0) {
			h2_bulkfree_sync(&cbinfo);

			hammer2_voldata_lock(hmp);
			hammer2_voldata_modify(hmp);
			hmp->voldata.allocator_free += cbinfo.adj_free;
			hammer2_voldata_unlock(hmp);
		}

		/*
		 * Cleanup for next loop.
		 */
		hammer2_trans_done(&trans);
		if (doabort)
			break;
		cbinfo.sbase = cbinfo.sstop;
	}
	kmem_free_swapbacked(&cbinfo.kp);

	bfi->sstop = cbinfo.sbase;

	incr = bfi->sstop / (hmp->voldata.volu_size / 10000);
	if (incr > 10000)
		incr = 10000;

	kprintf("bulkfree pass statistics (%d.%02d%% storage processed):\n",
		(int)incr / 100,
		(int)incr % 100);

	kprintf("    transition->free   %ld\n", cbinfo.count_10_00);
	kprintf("    transition->staged %ld\n", cbinfo.count_11_10);
	kprintf("    raced on           %ld\n", cbinfo.count_10_11);
	kprintf("    ~2MB segs cleaned  %ld\n", cbinfo.count_l0cleans);
	kprintf("    linear adjusts     %ld\n", cbinfo.count_linadjusts);

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

	/*
	 * Check for signal and allow yield to userland during scan
	 */
	if (hammer2_signal_check(&cbinfo->save_time))
		return HAMMER2_BULK_ABORT;

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
	error = 0;
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
		uint32_t bmask;

		bindex = (int)data_off >> (HAMMER2_FREEMAP_BLOCK_RADIX +
					   HAMMER2_BMAP_INDEX_RADIX);
		bmask = 3 << ((((int)data_off & HAMMER2_BMAP_INDEX_MASK) >>
			     HAMMER2_FREEMAP_BLOCK_RADIX) << 1);

		/*
		 * NOTE! The (avail) calculation is bitmap-granular.  Multiple
		 *	 sub-granular records can wind up at the same bitmap
		 *	 position.
		 */
		if ((bmap->bitmap[bindex] & bmask) == 0) {
			if (bytes < HAMMER2_FREEMAP_BLOCK_SIZE) {
				bmap->avail -= HAMMER2_FREEMAP_BLOCK_SIZE;
			} else {
				bmap->avail -= bytes;
			}
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

/*
 * Synchronize the in-memory bitmap with the live freemap.  This is not a
 * direct copy.  Instead the bitmaps must be compared:
 *
 *	In-memory	Live-freemap
 *	   00		  11 -> 10
 *			  10 -> 00
 *	   11		  10 -> 11	handles race against live
 *			  ** -> 11	nominally warn of corruption
 * 
 */
static void
h2_bulkfree_sync(hammer2_bulkfree_info_t *cbinfo)
{
	hammer2_off_t data_off;
	hammer2_key_t key;
	hammer2_key_t key_dummy;
	hammer2_bmap_data_t *bmap;
	hammer2_bmap_data_t *live;
	hammer2_chain_t *live_parent;
	hammer2_chain_t *live_chain;
	int cache_index = -1;
	int bmapindex;

	kprintf("hammer2_bulkfree - range %016jx-%016jx\n",
		(intmax_t)cbinfo->sbase,
		(intmax_t)cbinfo->sstop);
		
	data_off = cbinfo->sbase;
	bmap = cbinfo->bmap;

	live_parent = &cbinfo->hmp->fchain;
	hammer2_chain_ref(live_parent);
	hammer2_chain_lock(live_parent, HAMMER2_RESOLVE_ALWAYS);
	live_chain = NULL;

	while (data_off < cbinfo->sstop) {
		/*
		 * The freemap is not used below allocator_beg or beyond
		 * volu_size.
		 */
		if (data_off < cbinfo->hmp->voldata.allocator_beg)
			goto next;
		if (data_off > cbinfo->hmp->voldata.volu_size)
			goto next;

		/*
		 * Locate the freemap leaf on the live filesystem
		 */
		key = (data_off & ~HAMMER2_FREEMAP_LEVEL1_MASK);
		if (live_chain == NULL || live_chain->bref.key != key) {
			if (live_chain) {
				hammer2_chain_unlock(live_chain);
				hammer2_chain_drop(live_chain);
			}
			live_chain = hammer2_chain_lookup(
					    &live_parent,
					    &key_dummy,
					    key,
					    key + HAMMER2_FREEMAP_LEVEL1_MASK,
					    &cache_index,
					    HAMMER2_LOOKUP_ALWAYS);
			if (live_chain)
				kprintf("live_chain %016jx\n", (intmax_t)key);
					
		}
		if (live_chain == NULL) {
			if (bmap->class &&
			    bmap->avail != HAMMER2_FREEMAP_LEVEL0_SIZE) {
				kprintf("hammer2_bulkfree: cannot locate "
					"live leaf for allocated data "
					"near %016jx\n",
					(intmax_t)data_off);
			}
			goto next;
		}
		if (live_chain->error) {
			kprintf("hammer2_bulkfree: error %s looking up "
				"live leaf for allocated data near %016jx\n",
				hammer2_error_str(live_chain->error),
				(intmax_t)data_off);
			hammer2_chain_unlock(live_chain);
			hammer2_chain_drop(live_chain);
			live_chain = NULL;
			goto next;
		}

		bmapindex = (data_off & HAMMER2_FREEMAP_LEVEL1_MASK) >>
			    HAMMER2_FREEMAP_LEVEL0_RADIX;
		live = &live_chain->data->bmdata[bmapindex];

		/*
		 * For now just handle the 11->10, 10->00, and 10->11
		 * transitions.
		 */
		if (live->class == 0 ||
		    live->avail == HAMMER2_FREEMAP_LEVEL0_SIZE) {
			goto next;
		}
		if (bcmp(live->bitmap, bmap->bitmap, sizeof(bmap->bitmap)) == 0)
			goto next;
		kprintf("live %016jx %04d.%04x (avail=%d)\n",
			data_off, bmapindex, live->class, live->avail);

		hammer2_chain_modify(cbinfo->trans, live_chain, 0);
		h2_bulkfree_sync_adjust(cbinfo, live, bmap);
next:
		data_off += HAMMER2_FREEMAP_LEVEL0_SIZE;
		++bmap;
	}
	if (live_chain) {
		hammer2_chain_unlock(live_chain);
		hammer2_chain_drop(live_chain);
	}
	if (live_parent) {
		hammer2_chain_unlock(live_parent);
		hammer2_chain_drop(live_parent);
	}
}

static
void
h2_bulkfree_sync_adjust(hammer2_bulkfree_info_t *cbinfo,
			hammer2_bmap_data_t *live, hammer2_bmap_data_t *bmap)
{
	int bindex;
	int scount;
	uint32_t lmask;
	uint32_t mmask;

	for (bindex = 0; bindex < 8; ++bindex) {
		lmask = live->bitmap[bindex];
		mmask = bmap->bitmap[bindex];
		if (lmask == mmask)
			continue;

		for (scount = 0; scount < 32; scount += 2) {
			if ((mmask & 3) == 0) {
				/*
				 * in-memory 00		live 11 -> 10
				 *			live 10 -> 00
				 */
				switch (lmask & 3) {
				case 0:	/* 00 */
					break;
				case 1:	/* 01 */
					kprintf("hammer2_bulkfree: cannot "
						"transition m=00/l=01\n");
					break;
				case 2:	/* 10 -> 00 */
					live->bitmap[bindex] &= ~(2 << scount);
					live->avail +=
						HAMMER2_FREEMAP_BLOCK_SIZE;
					cbinfo->adj_free +=
						HAMMER2_FREEMAP_BLOCK_SIZE;
					++cbinfo->count_10_00;
					break;
				case 3:	/* 11 -> 10 */
					live->bitmap[bindex] &= ~(1 << scount);
					++cbinfo->count_11_10;
					break;
				}
			} else if ((lmask & 3) == 3) {
				/*
				 * in-memory 11		live 10 -> 11
				 *			live ** -> 11
				 */
				switch (lmask & 3) {
				case 0:	/* 00 */
					kprintf("hammer2_bulkfree: cannot "
						"transition m=11/l=00\n");
					break;
				case 1:	/* 01 */
					kprintf("hammer2_bulkfree: cannot "
						"transition m=11/l=01\n");
					break;
				case 2:	/* 10 -> 11 */
					live->bitmap[bindex] |= (1 << scount);
					++cbinfo->count_10_11;
					break;
				case 3:	/* 11 */
					break;
				}
			}
			mmask >>= 2;
			lmask >>= 2;
		}
	}

	/*
	 * Determine if the live bitmap is completely free and reset its
	 * fields if so.  Otherwise check to see if we can reduce the linear
	 * offset.
	 */
	for (bindex = 7; bindex >= 0; --bindex) {
		if (live->bitmap[bindex] != 0)
			break;
	}
	if (bindex < 0) {
		live->avail = HAMMER2_FREEMAP_LEVEL0_SIZE;
		live->class = 0;
		live->linear = 0;
		++cbinfo->count_l0cleans;
	} else if (bindex < 7) {
		++bindex;
		if (live->linear > bindex * HAMMER2_FREEMAP_BLOCK_SIZE) {
			live->linear = bindex * HAMMER2_FREEMAP_BLOCK_SIZE;
			++cbinfo->count_linadjusts;
		}
	}

#if 0
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
	}
#endif
}
