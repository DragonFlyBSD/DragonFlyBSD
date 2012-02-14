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
 *
 * chain->data will be pointed either at the embedded data (e.g. for
 * inodes), in which case the buffer cache buffer is released, or will
 * point into the bp->b_data buffer with the bp left intact while locked.
 */
int
hammer2_chain_lock(hammer2_mount_t *hmp, hammer2_chain_t *chain)
{
	hammer2_blockref_t *bref;
	hammer2_off_t off_hi;
	size_t off_lo;
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
	 * bp must be NULL, so if the data pointer is valid here it points
	 * to embedded data and no I/O is necessary (whether modified or not).
	 */
	KKASSERT(chain->bp == NULL);
	if (chain->data)
		return (0);

	/*
	 * If data is NULL we must issue I/O.  Any error returns the error
	 * code but leaves the chain locked.
	 *
	 * If the chain was modified a new bref will have already been
	 * allocated and its related bp is probably still sitting in the
	 * buffer cache.
	 */
	bref = &chain->bref;

	off_hi = bref->data_off & HAMMER2_OFF_MASK_HI;
	off_lo = (size_t)bref->data_off & HAMMER2_OFF_MASK_LO;
	KKASSERT(off_hi != 0);
	error = bread(hmp->devvp, off_hi, HAMMER2_PBUFSIZE, &chain->bp);

	if (error) {
		kprintf("hammer2_chain_get: I/O error %016jx: %d\n",
			(intmax_t)off_hi, error);
		brelse(chain->bp);
		chain->bp = NULL;
		return (error);
	}

	/*
	 * Setup the data pointer, either pointing it to an embedded data
	 * structure and copying the data from the buffer, or pointint it
	 * into the buffer.
	 *
	 * The buffer is not retained when copying to an embedded data
	 * structure in order to avoid potential deadlocks or recursions
	 * on the same physical buffer.
	 */
	switch (bref->type) {
	case HAMMER2_BREF_TYPE_VOLUME:
		/*
		 * Copy data from bp to embedded buffer
		 */
		KKASSERT(0);	/* not yet - have mount use this soon */
		KKASSERT(off_hi == 0);
		bcopy((char *)chain->bp->b_data + off_lo,
		      &hmp->voldata, HAMMER2_PBUFSIZE);
		chain->data = (void *)&hmp->voldata;
		brelse(chain->bp);
		chain->bp = NULL;
		break;
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
 *
 * If not already marked modified a new physical block will be allocated
 * and assigned to the bref.  If the data is pointing into an existing
 * bp it will be copied to the new bp and the new bp will replace the
 * existing bp.
 *
 * If the data is embedded we allocate the new physical block but don't
 * bother copying the data into it (yet).
 */
void
hammer2_chain_modify(hammer2_mount_t *hmp, hammer2_chain_t *chain)
{
	hammer2_chain_t *parent;
	struct buf *nbp;
	size_t bytes;
	void *ndata;
	int error;

	/*
	 * If the chain is already marked modified we can just return.
	 */
	if (chain->flags & HAMMER2_CHAIN_MODIFIED) {
		KKASSERT(chain->data != NULL);
		return;
	}

	/*
	 * The MODIFIED bit is not yet set, we must allocate the
	 * copy-on-write block.
	 *
	 * If the data is embedded no other action is required.
	 *
	 * If the data is not embedded we acquire and clear the
	 * new block.  If chain->data is not NULL we then do the
	 * copy-on-write.  chain->data will then be repointed to the new
	 * buffer and the old buffer will be released.
	 */
	atomic_set_int(&chain->flags, HAMMER2_CHAIN_MODIFIED);
	hammer2_chain_ref(hmp, chain);	/* ref for modified bit */

	bytes = 1 << (int)(chain->bref.data_off & HAMMER2_OFF_MASK_RADIX);
	if (chain != &hmp->vchain) {
		chain->bref.data_off = hammer2_freemap_alloc(hmp, bytes);
		/* XXX failed allocation */
	}

	switch(chain->bref.type) {
	case HAMMER2_BREF_TYPE_VOLUME:		/* embedded */
	case HAMMER2_BREF_TYPE_INODE:		/* embedded */
		/*
		 * data points to embedded structure, no copy needed
		 */
		error = 0;
		break;
	case HAMMER2_BREF_TYPE_INDIRECT:
	case HAMMER2_BREF_TYPE_DATA:
		/*
		 * data (if not NULL) points into original bp, copy-on-write
		 * to new block.
		 */
		KKASSERT(chain != &hmp->vchain);	/* safety */
		KKASSERT(chain->data == NULL || chain->bp != NULL);
		if (bytes == HAMMER2_PBUFSIZE) {
			nbp = getblk(hmp->devvp,
				     chain->bref.data_off & HAMMER2_OFF_MASK_HI,
				     HAMMER2_PBUFSIZE, 0, 0);
			vfs_bio_clrbuf(nbp);
			bdirty(nbp);
			error = 0;
		} else {
			error = bread(hmp->devvp,
				     chain->bref.data_off & HAMMER2_OFF_MASK_HI,
				     HAMMER2_PBUFSIZE, &nbp);
			KKASSERT(error == 0);/* XXX handle error */
		}
		ndata = nbp->b_data + (chain->bref.data_off &
				       HAMMER2_OFF_MASK_LO);
		if (chain->data) {
			bcopy(chain->data, ndata, bytes);
			KKASSERT(chain->bp != NULL);
			brelse(chain->bp);
		}
		chain->bp = nbp;
		chain->data = ndata;
		break;
	default:
		panic("hammer2_chain_modify: unknown bref type");
		break;

	}

	/*
	 * Recursively mark the parent chain elements so flushes can find
	 * modified elements.
	 */
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
 * Non-embedded data references (chain->bp != NULL) are returned to the
 * system and the data field is cleared in that case.  If modified the
 * dirty buffer is still returned to the system, can be flushed to disk by
 * the system at any time, and will be reconstituted/re-read as needed.
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
 * Locate an in-memory chain.  The parent must be locked.  The in-memory
 * chain is returned or NULL if no in-memory chain is present.
 *
 * NOTE: A chain on-media might exist for this index when NULL is returned.
 */
hammer2_chain_t *
hammer2_chain_find(hammer2_mount_t *hmp, hammer2_chain_t *parent, int index)
{
	hammer2_chain_t dummy;
	hammer2_chain_t *chain;

	dummy.index = index;
	chain = SPLAY_FIND(hammer2_chain_splay, &parent->shead, &dummy);
	return (chain);
}

/*
 * Return a locked chain structure with all associated data acquired.
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
	 * Otherwise lookup the bref and issue I/O (switch on the parent)
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
	hammer2_chain_t *tmp;
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
		tmp = hammer2_chain_find(hmp, parent, i);
		bref = (tmp) ? &tmp->bref : &base[i];
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
	hammer2_chain_t *tmp;
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
		tmp = hammer2_chain_find(hmp, parent, i);
		bref = (tmp) ? &tmp->bref : &base[i];
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
 * This element will also be marked as modified and contain a data area
 * ready for initialization.
 */
hammer2_chain_t *
hammer2_chain_create(hammer2_mount_t *hmp, hammer2_chain_t *parent,
		     hammer2_key_t key, int keybits, int type, size_t bytes)
{
	hammer2_blockref_t dummy;
	hammer2_blockref_t *base;
	hammer2_blockref_t *bref;
	hammer2_chain_t *chain;
	hammer2_chain_t dummy_chain;
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
	case HAMMER2_BREF_TYPE_VOLUME:
		panic("hammer2_chain_create: called with volume type");
		break;
	case HAMMER2_BREF_TYPE_INODE:
		KKASSERT(bytes == HAMMER2_INODE_BYTES);
		chain->data = (void *)&chain->u.ip->ip_data;
		break;
	default:
		/* leave chain->data NULL */
		KKASSERT(chain->data == NULL);
		break;
	}
	atomic_set_int(&chain->flags, HAMMER2_CHAIN_INITIAL);

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

	/*
	 * Scan for an unallocated bref, also skipping any slots occupied
	 * by in-memory chain elements that may not yet have been updated
	 * in the parent's bref array.
	 */
	bzero(&dummy_chain, sizeof(dummy_chain));
	bref = NULL;
	for (i = 0; i < count; ++i) {
		bref = &base[i];
		dummy_chain.index = i;
		if (bref->type == 0 &&
		    SPLAY_FIND(hammer2_chain_splay,
			       &parent->shead, &dummy_chain) == NULL) {
			break;
		}
	}

	/*
	 * If no free blockref count be found, fail us for now
	 */
	if (i == count) {
		hammer2_chain_free(hmp, chain);
		return (NULL);
	}

	/*
	 * Link the chain into its parent.
	 */
	chain->parent = parent;
	chain->index = i;
	if (SPLAY_INSERT(hammer2_chain_splay, &parent->shead, chain))
		panic("hammer2_chain_link: collision");
	KKASSERT(parent->refs > 1);
	atomic_add_int(&parent->refs, 1);

	/*
	 * Additional linkage for inodes.  Reuse the parent pointer to
	 * find the parent directory.
	 */
	if (chain->bref.type == HAMMER2_BREF_TYPE_INODE) {
		while (parent->bref.type == HAMMER2_BREF_TYPE_INDIRECT)
			parent = parent->parent;
		if (parent->bref.type == HAMMER2_BREF_TYPE_INODE)
			chain->u.ip->pip = parent->u.ip;
	}

	/*
	 * Mark the newly created chain element as modified and fully
	 * resolve the chain->data pointer.
	 *
	 * Chain elements with embedded data will not issue I/O at this time.
	 * A new block will be allocated for the buffer but not instantiated.
	 *
	 * Chain elements which do not use embedded data will allocate
	 * the new block AND instantiate its buffer cache buffer, pointing
	 * the data at the bp.
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

/*
 * Recursively flush the specified chain.  The chain is locked and
 * referenced by the caller and will remain so on return.
 *
 * This cannot be called with the volume header's vchain
 */
void
hammer2_chain_flush(hammer2_mount_t *hmp, hammer2_chain_t *chain,
		    hammer2_blockref_t *parent_bref)
{
	hammer2_chain_t *scan;

	/*
	 * Flush any children of this chain entry.
	 */
	if (chain->flags & HAMMER2_CHAIN_SUBMODIFIED) {
		hammer2_blockref_t *base;
		hammer2_blockref_t bref;
		int count;
		int submodified = 0;

		/*
		 * Modifications to the children will propagate up, forcing
		 * us to become modified and copy-on-write too.
		 */
		hammer2_chain_modify(hmp, chain);

		/*
		 * The blockref in the parent's array must be repointed at
		 * the new block allocated by the child after its flush.
		 *
		 * Calculate the base of the array.
		 */
		switch(chain->bref.type) {
		case HAMMER2_BREF_TYPE_INODE:
			KKASSERT(index >= 0 && index < HAMMER2_SET_COUNT);
			base = &chain->data->ipdata.u.blockset.blockref[0];
			count = HAMMER2_SET_COUNT;
			break;
		case HAMMER2_BREF_TYPE_INDIRECT:
			base = &chain->data->npdata.blockref[0];
			count = HAMMER2_IND_COUNT;
			break;
		case HAMMER2_BREF_TYPE_VOLUME:
			KKASSERT(index >= 0 && index < HAMMER2_SET_COUNT);
			base = &hmp->voldata.sroot_blockset.blockref[0];
			count = HAMMER2_SET_COUNT;
			break;
		default:
			base = NULL;
			panic("hammer2_chain_get: unrecognized blockref type: %d",
			      chain->bref.type);
		}

		/*
		 * Flush the children and update the blockrefs in the parent
		 */
		SPLAY_FOREACH(scan, hammer2_chain_splay, &chain->shead) {
			if (scan->flags & (HAMMER2_CHAIN_SUBMODIFIED |
					   HAMMER2_CHAIN_MODIFIED)) {
				hammer2_chain_ref(hmp, scan);
				hammer2_chain_lock(hmp, scan);
				bref = base[scan->index];
				hammer2_chain_flush(hmp, scan, &bref);
				if (scan->flags & (HAMMER2_CHAIN_SUBMODIFIED |
						   HAMMER2_CHAIN_MODIFIED)) {
					submodified = 1;
					kprintf("flush race, sub dirty\n");
				} else {
					KKASSERT(scan->index < count);
					base[scan->index] = bref;
				}
				hammer2_chain_put(hmp, scan);
			}
		}
		if (submodified == 0) {
			atomic_clear_int(&chain->flags,
					 HAMMER2_CHAIN_SUBMODIFIED);
		}
	}

	/*
	 * Flush this chain entry only if it is marked modified.
	 */
	if ((chain->flags & HAMMER2_CHAIN_MODIFIED) == 0)
		return;

	/*
	 * If this is part of a recursive flush we can go ahead and write
	 * out the buffer cache buffer and pass a new bref back up the chain.
	 *
	 * This will never be a volume header.
	 */
	if (parent_bref) {
		hammer2_blockref_t *bref;
		hammer2_off_t off_hi;
		struct buf *bp;
		size_t off_lo;
		size_t bytes;
		int error;

		KKASSERT(chain->data != NULL);
		bref = &chain->bref;

		off_hi = bref->data_off & HAMMER2_OFF_MASK_HI;
		off_lo = (size_t)bref->data_off & HAMMER2_OFF_MASK_LO;
		bytes = 1 << (int)(bref->data_off & HAMMER2_OFF_MASK_RADIX);
		KKASSERT(off_hi != 0);	/* not the root volume header */

		if (chain->bp) {
			/*
			 * The data is mapped directly to the bp, no further
			 * action is required.
			 */
			;
		} else {
			/*
			 * The data is embedded, we have to acquire the
			 * buffer cache buffer and copy the data into it.
			 */
			bp = NULL;
			error = bread(hmp->devvp, off_hi,
				      HAMMER2_PBUFSIZE, &bp);
			KKASSERT(error == 0); /* XXX */

			/*
			 * Copy the data to the buffer, mark the buffer
			 * dirty, and convert the chain to unmodified.
			 */
			bcopy(chain->data, (char *)bp->b_data + off_lo, bytes);
			bdwrite(bp);
			bp = NULL;

			chain->bref.check.iscsi32.value =
					hammer2_icrc32(chain->data, bytes);

			/*
			 * If parent_bref is not NULL we can clear the modified
			 * flag and remove the ref associated with that flag.
			 *
			 * If parent_bref is NULL the chain must be left in a
			 * modified state because the blockref in the parent
			 * that points to this chain is not being updated.
			 */
			*parent_bref = chain->bref;
		}
		kprintf("flush %016jx\n", (intmax_t)(off_hi | off_lo));
		hammer2_chain_drop(hmp, chain);
		atomic_clear_int(&chain->flags, HAMMER2_CHAIN_MODIFIED);
	} else {
		hammer2_blockref_t *bref;

		KKASSERT(chain->data != NULL);
		KKASSERT(chain->bp == NULL);
		bref = &chain->bref;

		switch(bref->type) {
		case HAMMER2_BREF_TYPE_VOLUME:
			hmp->voldata.icrc_sects[HAMMER2_VOL_ICRC_SECT1]=
				hammer2_icrc32(
					(char *)&hmp->voldata +
					 HAMMER2_VOLUME_ICRC1_OFF,
					HAMMER2_VOLUME_ICRC1_SIZE);
			hmp->voldata.icrc_sects[HAMMER2_VOL_ICRC_SECT0]=
				hammer2_icrc32(
					(char *)&hmp->voldata +
					 HAMMER2_VOLUME_ICRC0_OFF,
					HAMMER2_VOLUME_ICRC0_SIZE);
			hmp->voldata.icrc_volheader =
				hammer2_icrc32(
					(char *)&hmp->voldata +
					 HAMMER2_VOLUME_ICRCVH_OFF,
					HAMMER2_VOLUME_ICRCVH_SIZE);
			break;
		}
	}
}
