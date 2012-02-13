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
/*
 * This subsystem handles direct and indirect block searches, recursions,
 * creation, and deletion.  Chains of blockrefs are tracked and modifications
 * are flag for propagation... eventually all the way back to the volume
 * header.
 */

#include <sys/cdefs.h>
#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/types.h>
#include <sys/lock.h>
#include <sys/uuid.h>

#include "hammer2.h"

SPLAY_GENERATE(hammer2_chain_splay, hammer2_chain, snode, hammer2_chain_cmp);

/*
 * Compare function for chain splay tree
 */
int
hammer2_chain_cmp(hammer2_chain_t *chain1, hammer2_chain_t *chain2)
{
	return(chain2->index - chain1->index);
}

/*
 * Allocate a new disconnected chain element representing the specified
 * bref.  The chain element is locked exclusively and refs is set to 1.
 *
 * This essentially allocates a system memory structure representing one
 * of the media structure types, including inodes.
 */
hammer2_chain_t *
hammer2_chain_alloc(hammer2_mount_t *hmp, hammer2_blockref_t *bref)
{
	hammer2_chain_t *chain;
	hammer2_inode_t *ip;
	hammer2_indblock_t *np;
	hammer2_data_t *dp;

	/*
	 * Construct the appropriate system structure.
	 */
	switch(bref->type) {
	case HAMMER2_BREF_TYPE_INODE:
		ip = kmalloc(sizeof(*ip), hmp->minode, M_WAITOK | M_ZERO);
		chain = &ip->chain;
		chain->u.ip = ip;
		lockinit(&chain->lk, "inode", 0, LK_CANRECURSE);
		ip->hmp = hmp;
		break;
	case HAMMER2_BREF_TYPE_INDIRECT:
		np = kmalloc(sizeof(*np), hmp->mchain, M_WAITOK | M_ZERO);
		chain = &np->chain;
		chain->u.np = np;
		lockinit(&chain->lk, "iblk", 0, LK_CANRECURSE);
		break;
	case HAMMER2_BREF_TYPE_DATA:
		dp = kmalloc(sizeof(*dp), hmp->mchain, M_WAITOK | M_ZERO);
		chain = &dp->chain;
		chain->u.dp = dp;
		lockinit(&chain->lk, "dblk", 0, LK_CANRECURSE);
		break;
	case HAMMER2_BREF_TYPE_VOLUME:
		chain = NULL;
		panic("hammer2_chain_get: volume type illegal for op");
	default:
		chain = NULL;
		panic("hammer2_chain_get: unrecognized blockref type: %d",
		      bref->type);
	}
	chain->bref = *bref;
	chain->refs = 1;
	lockmgr(&chain->lk, LK_EXCLUSIVE);

	return (chain);
}

/*
 * Free a disconnected chain element
 */
void
hammer2_chain_free(hammer2_mount_t *hmp, hammer2_chain_t *chain)
{
	void *mem;

	KKASSERT(chain->bp == NULL);
	KKASSERT(chain->data == NULL);
	KKASSERT(chain->bref.type != HAMMER2_BREF_TYPE_INODE ||
		 chain->u.ip->vp == NULL);

	if ((mem = chain->u.mem) != NULL) {
		chain->u.mem = NULL;
		if (chain->bref.type == HAMMER2_BREF_TYPE_INODE)
			kfree(mem, hmp->minode);
		else
			kfree(mem, hmp->mchain);
	}
}

/*
 * Add a reference to a chain element (for shared access).  The chain
 * element must already have at least 1 ref controlled by the caller.
 */
void
hammer2_chain_ref(hammer2_mount_t *hmp, hammer2_chain_t *chain)
{
	KKASSERT(chain->refs > 0);
	atomic_add_int(&chain->refs, 1);
}

/*
 * Drop the callers reference to the chain element.  If the ref count
 * reaches zero the chain element and its related structure (typically an
 * inode or indirect block) will be freed and the parent will be
 * recursively dropped.
 *
 * Modified elements hold an additional reference so it should not be
 * possible for the count on a modified element to drop to 0.
 *
 * The chain element must NOT be locked by the caller.
 *
 * The parent might or might not be locked by the caller but if so it
 * will also be referenced so we shouldn't recurse upward.
 */
void
hammer2_chain_drop(hammer2_mount_t *hmp, hammer2_chain_t *chain)
{
	hammer2_chain_t *parent;
	u_int refs;

	while (chain) {
		refs = chain->refs;
		cpu_ccfence();
		if (refs == 1) {
			KKASSERT(chain != &hmp->vchain);
			parent = chain->parent;
			lockmgr(&parent->lk, LK_EXCLUSIVE);
			if (atomic_cmpset_int(&chain->refs, 1, 0)) {
				/*
				 * Succeeded, recurse and drop parent
				 */
				SPLAY_REMOVE(hammer2_chain_splay,
					     &parent->shead, chain);
				chain->parent = NULL;
				lockmgr(&parent->lk, LK_RELEASE);
				hammer2_chain_free(hmp, chain);
				chain = parent;
			} else {
				lockmgr(&parent->lk, LK_RELEASE);
			}
		} else {
			if (atomic_cmpset_int(&chain->refs, refs, refs - 1)) {
				/*
				 * Succeeded, count did not reach zero so
				 * cut out of the loop.
				 */
				break;
			}
		}
	}
}

/*
 * Lock a chain element, acquiring its data with I/O if necessary.
 *
 * Returns 0 on success or an error code if the data could not be acquired.
 * The chain element is locked either way.
 */
int
hammer2_chain_lock(hammer2_mount_t *hmp, hammer2_chain_t *chain)
{
	hammer2_blockref_t *bref;
	hammer2_off_t off_hi;
	size_t off_lo;
	size_t bytes;
	int error;
	void *data;

	/*
	 * Lock the element.  Under certain conditions this might end up
	 * being a recursive lock.
	 */
	KKASSERT(chain->refs > 0);
	lockmgr(&chain->lk, LK_EXCLUSIVE);

	/*
	 * The volume header is a special case
	 */
	if (chain->bref.type == HAMMER2_BREF_TYPE_VOLUME)
		return(0);

	/*
	 * If data is present the element may have been modified.  Assert
	 * the case and return the chain.
	 *
	 * data will point to the inode-embedded copy of hammer2_inode_data
	 * for inodes, whether modified or not.
	 */
	if (chain->data) {
		KKASSERT(chain->bp == NULL);
		return (0);
	}

	/*
	 * Issue I/O.  If an error occurs we return the error but still
	 * leave the chain locked.
	 */
	KKASSERT(chain->bp == NULL);
	bref = &chain->bref;

	off_hi = bref->data_off & HAMMER2_OFF_MASK_HI;
	off_lo = (size_t)bref->data_off & HAMMER2_OFF_MASK_LO;
	bytes = HAMMER2_PBUFSIZE;
	KKASSERT(off_hi != 0);
	KKASSERT(chain->bp == NULL);
	error = bread(hmp->devvp, off_hi, bytes, &chain->bp);

	if (error) {
		kprintf("hammer2_chain_get: I/O error %016jx: %d\n",
			(intmax_t)off_hi, error);
		brelse(chain->bp);
		chain->bp = NULL;
		return (error);
	}

	/*
	 * Extract the data.  Inodes have local embedded copies.  We can
	 * retain the bp for all other types.
	 *
	 * WARNING: Generally speaking we can only retain bp's that represent
	 *	    full blocks to avoid potential deadlocks or attempts to
	 *	    recursively acquire the same bp.
	 */
	switch (bref->type) {
	case HAMMER2_BREF_TYPE_INODE:
		/*
		 * Copy data from bp to embedded buffer.
		 */
		bcopy((char *)chain->bp->b_data + off_lo,
		      &chain->u.ip->ip_data,
		      HAMMER2_INODE_BYTES);
		chain->data = (void *)&chain->u.ip->ip_data;
		brelse(chain->bp);
		chain->bp = NULL;
		break;
	default:
		/*
		 * Leave bp intact
		 */
		data = (char *)chain->bp->b_data + off_lo;
		chain->data = data;
		break;
	}
	return (0);
}

/*
 * Convert a locked chain that was retrieved read-only to read-write.
 */
void
hammer2_chain_modify(hammer2_mount_t *hmp, hammer2_chain_t *chain)
{
	hammer2_chain_t *parent;
	size_t off_lo;
	size_t bytes;
	void *data;

	/*
	 * Nothing to do if it is already RW
	 */
	if (chain->flags & HAMMER2_CHAIN_MODIFIED) {
		KKASSERT(chain->bp == NULL);
		KKASSERT(chain->data != NULL);
		return;
	}
	hammer2_chain_ref(hmp, chain);

	/*
	 * Create an allocated copy of the data if it is not embedded.
	 */
	if (chain->data == NULL) {
		/*
		 * Allocate in-memory data and copy from the read-only bp
		 */
		KKASSERT(chain->bp != NULL);
		off_lo = (size_t)chain->bref.data_off & HAMMER2_OFF_MASK_LO;
		data = (char *)chain->bp->b_data + off_lo;

		bytes = 1 << (int)(chain->bref.data_off &
				   HAMMER2_OFF_MASK_RADIX);
		chain->data = kmalloc(bytes, hmp->mchain, M_WAITOK | M_ZERO);
		bcopy(data, chain->data, bytes);
		brelse(chain->bp);
		chain->bp = NULL;

		/*
		 * Assert correctness to prevent system memory corruption.
		 */
		switch(chain->bref.type) {
		case HAMMER2_BREF_TYPE_INODE:
			KKASSERT(bytes == sizeof(hammer2_inode_data_t));
			break;
		case HAMMER2_BREF_TYPE_INDIRECT:
			KKASSERT(bytes == sizeof(hammer2_indblock_data_t));
			break;
		default:
			/* do nothing */
			break;
		}
	}

	/*
	 * Mark modified
	 */
	atomic_set_int(&chain->flags, HAMMER2_CHAIN_MODIFIED);
	parent = chain->parent;
	while (parent && (parent->flags & HAMMER2_CHAIN_SUBMODIFIED) == 0) {
		atomic_set_int(&parent->flags, HAMMER2_CHAIN_SUBMODIFIED);
		parent = parent->parent;
	}
}

/*
 * Unlock a chain element without dropping its reference count.
 * (see hammer2_chain_put() to do both).
 *
 * Any read-only data reference (bp present) is cleaned out.  If the
 * chain was modified the writable copy of the data sticks around.
 */
void
hammer2_chain_unlock(hammer2_mount_t *hmp, hammer2_chain_t *chain)
{
	if (chain->bp) {
		chain->data = NULL;
		brelse(chain->bp);
		chain->bp = NULL;
	}
	lockmgr(&chain->lk, LK_RELEASE);
}

/*
 * Return a locked chain structure with all associated data acquired.
 *
 * If this is a read-only request the data may wind up pointing into a
 * buffer cache buffer.  If read-write a kmalloc()'d copy of the data
 * will be made and the buffer released, and the chain will be marked
 * modified.
 *
 * Caller must lock the parent on call, the returned child will be locked.
 */
hammer2_chain_t *
hammer2_chain_get(hammer2_mount_t *hmp, hammer2_chain_t *parent, int index)
{
	hammer2_blockref_t *bref;
	hammer2_chain_t *chain;
	hammer2_chain_t dummy;

	/*
	 * First see if we have a (possibly modified) chain element cached
	 * for this (parent, index).  Acquire the data if necessary.
	 *
	 * If chain->data is non-NULL the chain should already be marked
	 * modified.
	 */
	dummy.index = index;
	chain = SPLAY_FIND(hammer2_chain_splay, &parent->shead, &dummy);
	if (chain) {
		hammer2_chain_ref(hmp, chain);
		hammer2_chain_lock(hmp, chain);
		return(chain);
	}

	/*
	 * Otherwise lookup the bref and issue I/O
	 */
	switch(parent->bref.type) {
	case HAMMER2_BREF_TYPE_INODE:
		KKASSERT(index >= 0 && index < HAMMER2_SET_COUNT);
		bref = &parent->data->ipdata.u.blockset.blockref[index];
		break;
	case HAMMER2_BREF_TYPE_INDIRECT:
		KKASSERT(index >= 0 && index < HAMMER2_IND_COUNT);
		bref = &parent->data->npdata.blockref[index];
		break;
	case HAMMER2_BREF_TYPE_VOLUME:
		KKASSERT(index >= 0 && index < HAMMER2_SET_COUNT);
		bref = &hmp->voldata.sroot_blockset.blockref[index];
		break;
	default:
		bref = NULL;
		panic("hammer2_chain_get: unrecognized blockref type: %d",
		      parent->bref.type);
	}
	chain = hammer2_chain_alloc(hmp, bref);

	/*
	 * Link the chain into its parent.  Caller is expected to hold an
	 * exclusive lock on the parent.
	 */
	chain->parent = parent;
	chain->index = index;
	if (SPLAY_INSERT(hammer2_chain_splay, &parent->shead, chain))
		panic("hammer2_chain_link: collision");
	KKASSERT(parent->refs > 1);
	atomic_add_int(&parent->refs, 1);

	/*
	 * Additional linkage for inodes.  Reuse the parent pointer to
	 * find the parent directory.
	 */
	if (bref->type == HAMMER2_BREF_TYPE_INODE) {
		while (parent->bref.type == HAMMER2_BREF_TYPE_INDIRECT)
			parent = parent->parent;
		if (parent->bref.type == HAMMER2_BREF_TYPE_INODE)
			chain->u.ip->pip = parent->u.ip;
	}

	/*
	 * Our new chain structure has already been referenced and locked
	 * but the lock code handles the I/O so call it to resolve the data.
	 * Then release one of our two exclusive locks.
	 */
	hammer2_chain_lock(hmp, chain);
	lockmgr(&chain->lk, LK_RELEASE);
	/* still locked */

	return (chain);
}

/*
 * Unlock and dereference a chain after use.  It is possible for this to
 * recurse up the chain.
 */
void
hammer2_chain_put(hammer2_mount_t *hmp, hammer2_chain_t *chain)
{
	hammer2_chain_unlock(hmp, chain);
	hammer2_chain_drop(hmp, chain);
}

/*
 * Locate any key between key_beg and key_end inclusive.  (*parentp)
 * typically points to an inode but can also point to a related indirect
 * block and this function will recurse upwards and find the inode again.
 *
 * (*parentp) must be exclusively locked and referenced and can be an inode
 * or an existing indirect block within the inode.
 *
 * On return (*parentp) will be modified to point at the deepest parent chain
 * element encountered during the search, as a helper for an insertion or
 * deletion.   The new (*parentp) will be locked and referenced and the old
 * will be unlocked and dereferenced (no change if they are both the same).
 *
 * The matching chain will be returned exclusively locked and referenced.
 *
 * NULL is returned if no match was found, but (*parentp) will still
 * potentially be adjusted.
 *
 * This function will also recurse up the chain if the key is not within the
 * current parent's range.  (*parentp) can never be set to NULL.  An iteration
 * can simply allow (*parentp) to float inside the loop.
 */
hammer2_chain_t *
hammer2_chain_lookup(hammer2_mount_t *hmp, hammer2_chain_t **parentp,
		     hammer2_key_t key_beg, hammer2_key_t key_end)
{
	hammer2_chain_t *parent;
	hammer2_chain_t *chain;
	hammer2_blockref_t *base;
	hammer2_blockref_t *bref;
	hammer2_key_t scan_beg;
	hammer2_key_t scan_end;
	int count = 0;
	int i;

	/*
	 * Recurse (*parentp) upward if necessary until the parent completely
	 * encloses the key range or we hit the inode.
	 */
	parent = *parentp;
	while (parent->bref.type == HAMMER2_BREF_TYPE_INDIRECT) {
		scan_beg = parent->bref.key;
		scan_end = scan_beg +
			   ((hammer2_key_t)1 << parent->bref.keybits) - 1;
		if (key_beg >= scan_beg && key_end <= scan_end)
			break;
		hammer2_chain_unlock(hmp, parent);
		parent = parent->parent;
		hammer2_chain_ref(hmp, parent);		/* ref new parent */
		hammer2_chain_lock(hmp, parent);	/* lock new parent */
		hammer2_chain_drop(hmp, *parentp);	/* drop old parent */
		*parentp = parent;			/* new parent */
	}

again:
	/*
	 * Locate the blockref array.  Currently we do a fully associative
	 * search through the array.
	 */
	switch(parent->bref.type) {
	case HAMMER2_BREF_TYPE_INODE:
		base = &parent->data->ipdata.u.blockset.blockref[0];
		count = HAMMER2_SET_COUNT;
		break;
	case HAMMER2_BREF_TYPE_INDIRECT:
		base = &parent->data->npdata.blockref[0];
		count = HAMMER2_IND_COUNT;
		break;
	case HAMMER2_BREF_TYPE_VOLUME:
		base = &hmp->voldata.sroot_blockset.blockref[0];
		count = HAMMER2_SET_COUNT;
		break;
	default:
		panic("hammer2_chain_push: unrecognized blockref type: %d",
		      parent->bref.type);
		base = NULL;	/* safety */
		count = 0;	/* safety */
	}

	/*
	 * If the element and key overlap we use the element.
	 */
	bref = NULL;
	for (i = 0; i < count; ++i) {
		bref = &base[i];
		scan_beg = bref->key;
		scan_end = scan_beg + ((hammer2_key_t)1 << bref->keybits) - 1;
		if (key_beg <= scan_end && key_end >= scan_beg)
			break;
	}
	if (i == count) {
		if (key_beg == key_end)
			return (NULL);
		return (hammer2_chain_next(hmp, parentp, NULL,
					   key_beg, key_end));
	}

	/*
	 * Acquire the new chain element.  If the chain element is an
	 * indirect block we must search recursively.
	 */
	chain = hammer2_chain_get(hmp, parent, i);
	if (chain == NULL)
		return (NULL);

	/*
	 * If the chain element is an indirect block it becomes the new
	 * parent and we loop on it.
	 */
	if (chain->bref.type == HAMMER2_BREF_TYPE_INDIRECT) {
		hammer2_chain_put(hmp, parent);
		*parentp = parent = chain;
		goto again;
	}

	/*
	 * All done, return chain
	 */
	return (chain);
}

/*
 * After having issued a lookup we can iterate all matching keys.
 *
 * If chain is non-NULL we continue the iteration from just after it's index.
 *
 * If chain is NULL we assume the parent was exhausted and continue the
 * iteration at the next parent.
 */
hammer2_chain_t *
hammer2_chain_next(hammer2_mount_t *hmp, hammer2_chain_t **parentp,
		   hammer2_chain_t *chain,
		   hammer2_key_t key_beg, hammer2_key_t key_end)
{
	hammer2_chain_t *parent;
	hammer2_blockref_t *base;
	hammer2_blockref_t *bref;
	hammer2_key_t scan_beg;
	hammer2_key_t scan_end;
	int i;
	int count;

	parent = *parentp;

again:
	/*
	 * Calculate the next index and recalculate the parent if necessary.
	 */
	if (chain) {
		/*
		 * Continue iteration within current parent
		 */
		i = chain->index + 1;
		hammer2_chain_put(hmp, chain);
		chain = NULL;
	} else if (parent->bref.type != HAMMER2_BREF_TYPE_INDIRECT) {
		/*
		 * We reached the end of the iteration.
		 */
		return (NULL);
	} else {
		/*
		 * Continue iteration with next parent
		 */
		if (parent->bref.type != HAMMER2_BREF_TYPE_INDIRECT)
			return (NULL);
		i = parent->index + 1;
		hammer2_chain_unlock(hmp, parent);
		parent = parent->parent;
		hammer2_chain_ref(hmp, parent);		/* ref new parent */
		hammer2_chain_lock(hmp, parent);	/* lock new parent */
		hammer2_chain_drop(hmp, *parentp);	/* drop old parent */
		*parentp = parent;			/* new parent */
	}

again2:
	/*
	 * Locate the blockref array.  Currently we do a fully associative
	 * search through the array.
	 */
	switch(parent->bref.type) {
	case HAMMER2_BREF_TYPE_INODE:
		base = &parent->data->ipdata.u.blockset.blockref[0];
		count = HAMMER2_SET_COUNT;
		break;
	case HAMMER2_BREF_TYPE_INDIRECT:
		base = &parent->data->npdata.blockref[0];
		count = HAMMER2_IND_COUNT;
		break;
	case HAMMER2_BREF_TYPE_VOLUME:
		base = &hmp->voldata.sroot_blockset.blockref[0];
		count = HAMMER2_SET_COUNT;
		break;
	default:
		panic("hammer2_chain_push: unrecognized blockref type: %d",
		      parent->bref.type);
		base = NULL;	/* safety */
		count = 0;	/* safety */
		break;
	}
	KKASSERT(i <= count);

	/*
	 * Look for the key.  If we are unable to find a match and an exact
	 * match was requested we return NULL.  If a range was requested we
	 * run hammer2_chain_next() to iterate.
	 */
	bref = NULL;
	while (i < count) {
		bref = &base[i];
		scan_beg = bref->key;
		scan_end = scan_beg + ((hammer2_key_t)1 << bref->keybits) - 1;
		if (key_beg <= scan_end && key_end >= scan_beg)
			break;
		++i;
	}

	/*
	 * If we couldn't find a match recurse up a parent to continue the
	 * search.
	 */
	if (i == count)
		goto again;

	/*
	 * Acquire the new chain element.  If the chain element is an
	 * indirect block we must search recursively.
	 */
	chain = hammer2_chain_get(hmp, parent, i);
	if (chain == NULL)
		return (NULL);

	/*
	 * If the chain element is an indirect block it becomes the new
	 * parent and we loop on it.
	 */
	if (chain->bref.type == HAMMER2_BREF_TYPE_INDIRECT) {
		hammer2_chain_put(hmp, parent);
		*parentp = parent = chain;
		i = 0;
		goto again2;
	}

	/*
	 * All done, return chain
	 */
	return (chain);
}

/*
 * Create and return a new hammer2 system memory structure of the specified
 * key, type and size and insert it under (parent).  (parent) is typically
 * acquired as a side effect of issuing a prior lookup.  parent must be locked
 * and held.
 *
 * Non-indirect types will automatically allocate indirect blocks as required
 * if the new item does not fit in the current (parent).
 *
 * Indirect types will move a portion of the existing blockref array in
 * (parent) into the new indirect type and then use one of the free slots
 * to emplace the new indirect type.
 *
 * A new locked, referenced chain element is returned of the specified type.
 * This element will also be marked as modified and contain a kmalloc()'d
 * pre-zero'd data area that is ready for initialization.
 */
hammer2_chain_t *
hammer2_chain_create(hammer2_mount_t *hmp, hammer2_chain_t *parent,
		     hammer2_key_t key, int keybits, int type, size_t bytes)
{
	hammer2_blockref_t dummy;
	hammer2_blockref_t *base;
	hammer2_blockref_t *bref;
	hammer2_chain_t *chain;
	int count;
	int i;

	/*
	 * First allocate media space and construct the dummy bref, then
	 * allocate the in-memory chain structure.
	 */
	bzero(&dummy, sizeof(dummy));
	dummy.type = type;
	dummy.key = key;
	dummy.keybits = keybits;
	dummy.data_off = hammer2_freemap_alloc(hmp, bytes);
	chain = hammer2_chain_alloc(hmp, &dummy);

	/*
	 * Recalculate bytes to reflect the actual media block allocation,
	 * then allocate the local memory copy.  This is a new structure
	 * so no I/O is performed.
	 */
	bytes = (hammer2_off_t)1 <<
		(int)(chain->bref.data_off & HAMMER2_OFF_MASK_RADIX);
	switch(type) {
	case HAMMER2_BREF_TYPE_INODE:
		KKASSERT(bytes == HAMMER2_INODE_BYTES);
		chain->data = (void *)&chain->u.ip->ip_data;
		break;
	default:
		chain->data = kmalloc(bytes, hmp->mchain, M_WAITOK | M_ZERO);
		break;
	}
	chain->flags |= HAMMER2_CHAIN_INITIAL;

	/*
	 * Indicate that the parent is being modified before we dive
	 * the parent's media data (since this will relocate the media
	 * data pointer).
	 */
	hammer2_chain_modify(hmp, parent);

	/*
	 * Locate a free blockref in the parent's array
	 */
	switch(parent->bref.type) {
	case HAMMER2_BREF_TYPE_INODE:
		base = &parent->data->ipdata.u.blockset.blockref[0];
		count = HAMMER2_SET_COUNT;
		break;
	case HAMMER2_BREF_TYPE_INDIRECT:
		base = &parent->data->npdata.blockref[0];
		count = HAMMER2_IND_COUNT;
		break;
	case HAMMER2_BREF_TYPE_VOLUME:
		base = &hmp->voldata.sroot_blockset.blockref[0];
		count = HAMMER2_SET_COUNT;
		break;
	default:
		panic("hammer2_chain_push: unrecognized blockref type: %d",
		      parent->bref.type);
		count = 0;
		break;
	}

	bref = NULL;
	for (i = 0; i < count; ++i) {
		bref = &base[i];
		if (bref->type == 0)
			break;
	}

	/*
	 * If no free blockref count be found, fail us for now
	 */
	if (i == count) {
		hammer2_chain_free(hmp, chain);
		return (NULL);
	}

	/*
	 * Link the chain into its parent and load the blockref slot in
	 * the parent with a pointer to the data allocation.
	 */
	chain->parent = parent;
	chain->index = i;
	if (SPLAY_INSERT(hammer2_chain_splay, &parent->shead, chain))
		panic("hammer2_chain_link: collision");
	KKASSERT(parent->refs > 1);
	atomic_add_int(&parent->refs, 1);
	*bref = chain->bref;

	/*
	 * Additional linkage for inodes.  Reuse the parent pointer to
	 * find the parent directory.
	 */
	if (bref->type == HAMMER2_BREF_TYPE_INODE) {
		while (parent->bref.type == HAMMER2_BREF_TYPE_INDIRECT)
			parent = parent->parent;
		if (parent->bref.type == HAMMER2_BREF_TYPE_INODE)
			chain->u.ip->pip = parent->u.ip;
	}

	/*
	 * Make the newly created chain as modified.  We have already
	 * assigned the data pointer so it will not try to read from the
	 * media.
	 */
	hammer2_chain_modify(hmp, chain);

	return (chain);
}

/*
 * Physically delete the specified chain element.  Note that inodes with
 * open descriptors should not be deleted (as with other filesystems) until
 * the last open descriptor is closed.
 *
 * This routine will remove the chain element from its parent and potentially
 * also recurse upward and delete indirect blocks which become empty as a
 * side effect.
 *
 * The caller must pass a pointer to the chain's parent, also locked and
 * referenced.  (*parentp) will be modified in a manner similar to a lookup
 * or iteration when indirect blocks are also deleted as a side effect.
 */
void
hammer2_chain_delete(hammer2_mount_t *hmp, hammer2_chain_t **parentp,
		     hammer2_chain_t *chain)
{
}
