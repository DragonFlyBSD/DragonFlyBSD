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
#include <sys/cdefs.h>
#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/types.h>
#include <sys/lock.h>
#include <sys/uuid.h>

#include "hammer2.h"

/*
 * Add a reference to a chain element (for shared access).  The chain
 * element must already have at least 1 ref.
 */
void
hammer2_chain_ref(hammer2_mount_t *hmp, hammer2_chain_t *chain)
{
	KKASSERT(chain->refs > 0);
	atomic_add_int(&chain->refs, 1);
}

/*
 * Drop the callers reference to the chain element.  If the ref count
 * reaches zero the chain element and any related structure (typically an
 * inode or indirect block) will be freed.
 *
 * Keep in mind that hammer2_chain structures are typically directly embedded
 * in major hammer2 memory structures.
 */
void
hammer2_chain_drop(hammer2_mount_t *hmp, hammer2_chain_t *chain)
{
	if (atomic_fetchadd_int(&chain->refs, -1) == 1) {
		if (chain->bref.type == HAMMER2_BREF_TYPE_INODE) {
			KKASSERT(chain == &chain->u.ip->chain);
			hammer2_inode_free(chain->u.ip);
		}
	}
}

void
hammer2_chain_link(hammer2_mount_t *hmp __unused, hammer2_chain_t *parent,
		   int index, hammer2_chain_t *chain)
{
	chain->parent = parent;
	chain->index = index;
	chain->refs = 1;
	chain->next = parent->subs;
	parent->subs = chain;
}

/*
 * Remove the chain linkage to its parent.  The chain must not have any
 * children.
 */
void
hammer2_chain_unlink(hammer2_mount_t *hmp __unused, hammer2_chain_t *chain)
{
	hammer2_chain_t **scanp;
	hammer2_chain_t *scan;

	KKASSERT(chain->subs == NULL);
	if (chain->parent) {
		scanp = &chain->parent->subs;
		while ((scan = *scanp) != NULL) {
			if (scan == chain)
				break;
		}
		KKASSERT(scan == chain);
		*scanp = scan->next;
		chain->parent = NULL;
	}
}

/*
 * Find or allocate a chain element representing a blockref.
 *
 * The blockref at (parent, index) representing (bref) is located or
 * created and returned.  I/O is issued to read the contents if necessary.
 *
 * XXX use RB-Tree instead of linked list
 */
hammer2_chain_t *
hammer2_chain_get(hammer2_mount_t *hmp, hammer2_chain_t *parent,
		  int index, hammer2_blockref_t *bref)
{
	struct buf *bp;
	void *data;
	hammer2_chain_t *chain;
	hammer2_inode_t *ip;
	hammer2_off_t dblock;
	int off;
	int error;

	/*
	 * Look for a cached chain element
	 */
	for (chain = parent->subs; chain; chain = chain->next) {
		if (chain->index == index) {
			atomic_add_int(&chain->refs, 1);
			return (chain);
		}
	}

	/*
	 * Issue I/O to read the underlying object
	 */
	dblock = bref->data_off & HAMMER2_OFF_MASK_HI;
	off = (int)bref->data_off & HAMMER2_OFF_MASK_LO;
	KKASSERT(dblock != 0);

	error = bread(hmp->devvp, dblock, HAMMER2_PBUFSIZE, &bp);
	if (error) {
		kprintf("hammer2_chain_get: I/O error %016jx: %d\n",
			(intmax_t)dblock, error);
		brelse(bp);
		return(NULL);
	}
	data = (char *)bp->b_data + off;

	/*
	 * Construct the appropriate system structure.
	 */
	switch(bref->type) {
	case HAMMER2_BREF_TYPE_INODE:
		ip = hammer2_inode_alloc(hmp, data);
		chain = &ip->chain;
		chain->bref = *bref;
		chain->u.ip = ip;
		hammer2_chain_link(hmp, parent, index, chain);
		kprintf("found inode %jd\n", (intmax_t)ip->data.inum);
		/* hammer2_chain_unbusy */
		break;
	case HAMMER2_BREF_TYPE_INDIRECT:
		kprintf("hammer2_chain_get: indirect (not impl yet)\n");
		break;
	case HAMMER2_BREF_TYPE_DATA:
		panic("hammer2_chain_get: data type illegal for op");
		break;
	case HAMMER2_BREF_TYPE_VOLUME:
		panic("hammer2_chain_get: volume type illegal for op");
		break;
	default:
		panic("hammer2_chain_get: unrecognized blockref type: %d",
		      bref->type);
	}
	brelse(bp);

	return (chain);
}

void
hammer2_chain_put(hammer2_mount_t *hmp, hammer2_chain_t *chain)
{
	hammer2_chain_drop(hmp, chain);
}

/*
 * Locate the exact key relative to the parent chain.
 */
hammer2_chain_t *
hammer2_chain_push(hammer2_mount_t *hmp, hammer2_chain_t *parent,
		   hammer2_key_t key)
{
	hammer2_chain_t *chain;
	hammer2_blockref_t *bref;
	hammer2_blockset_t *bset;
	int count = 0;
	int n;
	int i;

	chain = NULL;
	bset = NULL;

	/*
	 * Locate the blockset(s) to search.  Currently we do a fully
	 * associative search across all blocksets instead of seeking to
	 * a blockset and doing a fully associative search inside one set.
	 */
	switch(parent->bref.type) {
	case HAMMER2_BREF_TYPE_INODE:
		bset = &parent->u.ip->data.u.blockset;
		count = 1;
		break;
	case HAMMER2_BREF_TYPE_INDIRECT:
		bset = &parent->u.ind->data->blocksets[0];
		count = HAMMER2_IND_COUNT / HAMMER2_SET_COUNT;
		break;
	case HAMMER2_BREF_TYPE_DATA:
		break;
	case HAMMER2_BREF_TYPE_VOLUME:
		bset = &hmp->voldata.sroot_blockset;
		count = 1;
		break;
	default:
		panic("hammer2_chain_push: unrecognized blockref type: %d",
		      parent->bref.type);
	}

	for (n = 0; n < count; ++n) {
		bref = NULL;
		for (i = 0; i < HAMMER2_SET_COUNT; ++i) {
			bref = &bset->blockref[i];
			if (bref->key == key &&
			    bref->keybits == 0 &&
			    bref->type != 0) {
				break;
			}
		}
		if (i != HAMMER2_SET_COUNT) {
			chain = hammer2_chain_get(hmp, parent,
						  n * HAMMER2_SET_COUNT + i,
						  bref);
			break;
		}
		++bset;
	}
	return (chain);
}

/*
 * Initiate a ranged search, locating the first element matching (key & ~mask).
 * That is, the passed mask represents the bits we wish to ignore.
 */
hammer2_chain_t *
hammer2_chain_first(hammer2_mount_t *hmp, hammer2_chain_t *parent,
		    hammer2_key_t key, hammer2_key_t mask)
{
	hammer2_chain_t *chain;
	hammer2_blockref_t *bref;
	hammer2_blockset_t *bset;
	int count = 0;
	int n;
	int i;

	chain = NULL;
	bset = NULL;

	/*
	 * Locate the blockset(s) to search.  Currently we do a fully
	 * associative search across all blocksets instead of seeking to
	 * a blockset and doing a fully associative search inside one set.
	 */
	switch(parent->bref.type) {
	case HAMMER2_BREF_TYPE_INODE:
		bset = &parent->u.ip->data.u.blockset;
		count = 1;
		break;
	case HAMMER2_BREF_TYPE_INDIRECT:
		bset = &parent->u.ind->data->blocksets[0];
		count = HAMMER2_IND_COUNT / HAMMER2_SET_COUNT;
		break;
	case HAMMER2_BREF_TYPE_DATA:
		break;
	case HAMMER2_BREF_TYPE_VOLUME:
		bset = &hmp->voldata.sroot_blockset;
		count = 1;
		break;
	default:
		panic("hammer2_chain_first: unrecognized blockref type: %d",
		      parent->bref.type);
	}
	kprintf("first key %016jx %016jx\n", (intmax_t)key, (intmax_t)mask);

	for (n = 0; n < count; ++n) {
		bref = NULL;
		for (i = 0; i < HAMMER2_SET_COUNT; ++i) {
			bref = &bset->blockref[i];
			kprintf("test %016jx\n", bref->key);
			if ((bref->key & ~((1ULL << bref->keybits) - 1) & ~mask)
			    == (key & ~mask) &&
			    bref->type != 0) {
				break;
			}
		}
		if (i != HAMMER2_SET_COUNT) {
			chain = hammer2_chain_get(hmp, parent,
						  n * HAMMER2_SET_COUNT + i,
						  bref);
			break;
		}
		++bset;
	}
	return (chain);
}

/*
 * Locate the next element matching (key & ~mask) occuring after the current
 * element.
 */
hammer2_chain_t *
hammer2_chain_next(hammer2_mount_t *hmp, hammer2_chain_t *current,
		   hammer2_key_t key, hammer2_key_t mask)
{
	hammer2_chain_t *parent;
	hammer2_chain_t *chain;
	hammer2_blockref_t *bref;
	hammer2_blockset_t *bset;
	int count = 0;
	int n;
	int i;

	chain = NULL;
	bset = NULL;

	/*
	 * Locate the blockset(s) to search.  Currently we do a fully
	 * associative search across all blocksets instead of seeking to
	 * a blockset and doing a fully associative search inside one set.
	 */
	parent = current->parent;

	switch(parent->bref.type) {
	case HAMMER2_BREF_TYPE_INODE:
		bset = &parent->u.ip->data.u.blockset;
		count = 1;
		break;
	case HAMMER2_BREF_TYPE_INDIRECT:
		bset = &parent->u.ind->data->blocksets[0];
		count = HAMMER2_IND_COUNT / HAMMER2_SET_COUNT;
		break;
	case HAMMER2_BREF_TYPE_DATA:
		break;
	case HAMMER2_BREF_TYPE_VOLUME:
		bset = &hmp->voldata.sroot_blockset;
		count = 1;
		break;
	default:
		panic("hammer2_chain_next: unrecognized blockref type: %d",
		      parent->bref.type);
	}

	kprintf("next key %016jx %016jx\n", (intmax_t)key, (intmax_t)mask);

	n = current->index / HAMMER2_SET_COUNT;
	i = current->index % HAMMER2_SET_COUNT;
	bset += n;

	while (n < count) {
		bref = NULL;
		while (i < HAMMER2_SET_COUNT) {
			bref = &bset->blockref[i];
			kprintf("test %016jx\n", bref->key);
			if ((bref->key & ~((1ULL << bref->keybits) - 1) & ~mask)
			    == (key & ~mask) &&
			    bref->type != 0) {
				break;
			}
			++i;
		}
		if (i != HAMMER2_SET_COUNT) {
			chain = hammer2_chain_get(hmp, parent,
						  n * HAMMER2_SET_COUNT + i,
						  bref);
			break;
		}
		++bset;
		++n;
		i = 0;
	}
	return (chain);
}
