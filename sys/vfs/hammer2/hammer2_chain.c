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

static int hammer2_indirect_optimize;	/* XXX SYSCTL */

static hammer2_chain_t *hammer2_chain_create_indirect(
			hammer2_mount_t *hmp, hammer2_chain_t *parent,
			hammer2_key_t key, int keybits);

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
	u_int bytes = 1U << (int)(bref->data_off & HAMMER2_OFF_MASK_RADIX);

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
		panic("hammer2_chain_alloc volume type illegal for op");
	default:
		chain = NULL;
		panic("hammer2_chain_alloc: unrecognized blockref type: %d",
		      bref->type);
	}
	chain->bref = *bref;
	chain->index = -1;	/* not yet assigned */
	chain->refs = 1;
	chain->bytes = bytes;
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

	if (chain->bref.type == HAMMER2_BREF_TYPE_INODE ||
	    chain->bref.type == HAMMER2_BREF_TYPE_VOLUME) {
		chain->data = NULL;
	}

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
		KKASSERT(refs > 0);
		if (refs == 1) {
			KKASSERT(chain != &hmp->vchain);
			parent = chain->parent;
			if (parent)
				lockmgr(&parent->lk, LK_EXCLUSIVE);
			if (atomic_cmpset_int(&chain->refs, 1, 0)) {
				/*
				 * Succeeded, recurse and drop parent
				 */
				if (!(chain->flags & HAMMER2_CHAIN_DELETED)) {
					SPLAY_REMOVE(hammer2_chain_splay,
						     &parent->shead, chain);
					atomic_set_int(&chain->flags,
						       HAMMER2_CHAIN_DELETED);
					/* parent refs dropped via recursion */
				}
				chain->parent = NULL;
				if (parent)
					lockmgr(&parent->lk, LK_RELEASE);
				hammer2_chain_free(hmp, chain);
				chain = parent;
				/* recurse on parent */
			} else {
				if (parent)
					lockmgr(&parent->lk, LK_RELEASE);
				/* retry the same chain */
			}
		} else {
			if (atomic_cmpset_int(&chain->refs, refs, refs - 1)) {
				/*
				 * Succeeded, count did not reach zero so
				 * cut out of the loop.
				 */
				break;
			}
			/* retry the same chain */
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
 *
 * NOTE: Chain elements of type DATA do not instantiate a buffer or set
 *	 the data pointer.
 */
int
hammer2_chain_lock(hammer2_mount_t *hmp, hammer2_chain_t *chain)
{
	hammer2_blockref_t *bref;
	hammer2_off_t pbase;
	int error;

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
	 * We do not instantiate a device buffer for DATA chain elements,
	 * as this would cause unnecessary double-buffering.
	 */
	if (chain->bref.type == HAMMER2_BREF_TYPE_DATA)
		return(0);

	/*
	 * If data is NULL we must issue I/O.  Any error returns the error
	 * code but leaves the chain locked.
	 *
	 * If the chain was modified a new bref will have already been
	 * allocated and its related bp is probably still sitting in the
	 * buffer cache.
	 *
	 * The buffer cache buffer is variable-sized in powers of 2 down
	 * to HAMMER2_MINIOSIZE (typically 1K).
	 */
	bref = &chain->bref;

	pbase = bref->data_off & ~(hammer2_off_t)(chain->bytes - 1);
	KKASSERT(pbase != 0);
	error = bread(hmp->devvp, pbase, chain->bytes, &chain->bp);

	if (error) {
		kprintf("hammer2_chain_get: I/O error %016jx: %d\n",
			(intmax_t)pbase, error);
		bqrelse(chain->bp);
		chain->bp = NULL;
		return (error);
	}

	/*
	 * Setup the data pointer, either pointing it to an embedded data
	 * structure and copying the data from the buffer, or pointing it
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
		KKASSERT(pbase == 0);
		KKASSERT(chain->bytes == HAMMER2_PBUFSIZE);
		bcopy(chain->bp->b_data, &hmp->voldata, chain->bytes);
		chain->data = (void *)&hmp->voldata;
		bqrelse(chain->bp);
		chain->bp = NULL;
		break;
	case HAMMER2_BREF_TYPE_INODE:
		/*
		 * Copy data from bp to embedded buffer.
		 */
		bcopy(chain->bp->b_data, &chain->u.ip->ip_data, chain->bytes);
		chain->data = (void *)&chain->u.ip->ip_data;
		bqrelse(chain->bp);
		chain->bp = NULL;
		break;
	case HAMMER2_BREF_TYPE_INDIRECT:
	case HAMMER2_BREF_TYPE_DATA:
	default:
		/*
		 * Leave bp intact
		 */
		chain->data = (void *)chain->bp->b_data;
		break;
	}
	return (0);
}

/*
 * Resize the chain's physical storage allocation.  Chains can be resized
 * smaller without reallocating the storage.  Resizing larger will reallocate
 * the storage.
 *
 * Must be passed a locked chain
 */
void
hammer2_chain_resize(hammer2_mount_t *hmp, hammer2_chain_t *chain, int nradix)
{
	hammer2_chain_t *parent;
	struct buf *nbp;
	size_t obytes;
	size_t nbytes;
	void *ndata;
	int error;

	/*
	 * Only data and indirect blocks can be resized for now
	 */
	KKASSERT(chain != &hmp->vchain);
	KKASSERT(chain->bref.type == HAMMER2_BREF_TYPE_DATA ||
		 chain->bref.type == HAMMER2_BREF_TYPE_INDIRECT);

	/*
	 * Nothing to do if the element is already the proper size
	 */
	obytes = chain->bytes;
	nbytes = 1 << nradix;
	if (obytes == nbytes)
		return;

#if 0
	if ((chain->flags & HAMMER2_CHAIN_DELETED) &&
	    chain->bref.type == HAMMER2_BREF_TYPE_INODE) {
		KKASSERT(chain->data != NULL);
		return;
	}
#endif

	/*
	 * Set MODIFIED1 and add a chain ref to prevent destruction.  Both
	 * modified flags share the same ref.
	 */
	if ((chain->flags & HAMMER2_CHAIN_MODIFIED1) == 0) {
		atomic_set_int(&chain->flags, HAMMER2_CHAIN_MODIFIED1);
		hammer2_chain_ref(hmp, chain);
	}

	if (nbytes < obytes) {
		/*
		 * If we are making it smaller we don't have to reallocate
		 * the block but we still need to resize it.
		 */
		chain->bref.data_off &= ~HAMMER2_OFF_MASK_RADIX;
		chain->bref.data_off |= (nradix & HAMMER2_OFF_MASK_RADIX);
		chain->bytes = nbytes;
		allocbuf(chain->bp, nbytes);
	} else {
		/*
		 * Otherwise we do
		 */
		chain->bref.data_off =
			hammer2_freemap_alloc(hmp, chain->bref.type, nbytes);
		chain->bytes = nbytes;

		switch(chain->bref.type) {
		case HAMMER2_BREF_TYPE_VOLUME:		/* embedded */
		case HAMMER2_BREF_TYPE_INODE:		/* embedded */
			/*
			 * data points to embedded structure, no copy needed
			 */
			error = 0;
			break;
		case HAMMER2_BREF_TYPE_INDIRECT:
			panic("hammer2_chain_resize: "
			      "cannot resize indirect block");
			/* NOT REACHED */
			break;
		case HAMMER2_BREF_TYPE_DATA:
			/*
			 * data (if not NULL) points into original bp or
			 * to embedded data.  Copy-on-write to new block.
			 */
			KKASSERT(chain != &hmp->vchain);	/* safety */
			nbp = getblk(hmp->devvp,
				     chain->bref.data_off &
				      ~(hammer2_off_t)(nbytes - 1),
				     nbytes, 0, 0);
			vfs_bio_clrbuf(nbp);
			error = 0;

			/*
			 * The new block is larger than the old one, only
			 * copy what fits.
			 */
			ndata = nbp->b_data;
			if (chain->data) {
				if (nbytes < obytes)
					bcopy(chain->data, ndata, nbytes);
				else
					bcopy(chain->data, ndata, obytes);
				KKASSERT(chain->bp != NULL);
			}
			if (chain->bp) {
				chain->bp->b_flags |= B_RELBUF;
				brelse(chain->bp);
			}
			chain->bp = nbp;
			chain->data = ndata;
			break;
		default:
			panic("hammer2_chain_modify: unknown bref type");
			break;

		}
	}

	/*
	 * Recursively mark the parent chain elements so flushes can find
	 * modified elements.
	 *
	 * NOTE: The flush code will modify a SUBMODIFIED-flagged chain
	 *	 during the flush recursion after clearing the parent's
	 *	 SUBMODIFIED bit.  We don't want to re-set the parent's
	 *	 SUBMODIFIED bit in this case!
	 */
	if ((chain->flags & HAMMER2_CHAIN_SUBMODIFIED) == 0) {
		parent = chain->parent;
		while (parent &&
		       (parent->flags & HAMMER2_CHAIN_SUBMODIFIED) == 0) {
			atomic_set_int(&parent->flags,
				       HAMMER2_CHAIN_SUBMODIFIED);
			parent = parent->parent;
		}
	}
}

/*
 * This is the same as hammer2_chain_resize() except the chain does NOT
 * have to be locked and any underlying data is NOT copied to the new
 * location.
 */
void
hammer2_chain_resize_quick(hammer2_mount_t *hmp, hammer2_chain_t *chain,
			   int nradix)
{
	hammer2_chain_t *parent;
	size_t obytes;
	size_t nbytes;

	/*
	 * Only data and indirect blocks can be resized for now
	 */
	KKASSERT(chain != &hmp->vchain);
	KKASSERT(chain->bref.type == HAMMER2_BREF_TYPE_DATA ||
		 chain->bref.type == HAMMER2_BREF_TYPE_INDIRECT);

	/*
	 * Nothing to do if the element is already the proper size
	 */
	obytes = chain->bytes;
	nbytes = 1 << nradix;
	if (obytes == nbytes)
		return;

	lockmgr(&chain->lk, LK_EXCLUSIVE);
	KKASSERT(chain->bp == NULL);

	/*
	 * Set MODIFIED1 and add a chain ref to prevent destruction.  Both
	 * modified flags share the same ref.
	 */
	if ((chain->flags & HAMMER2_CHAIN_MODIFIED1) == 0) {
		atomic_set_int(&chain->flags, HAMMER2_CHAIN_MODIFIED1);
		hammer2_chain_ref(hmp, chain);
	}

	if (nbytes < obytes) {
		/*
		 * If we are making it smaller we don't have to reallocate
		 * the block but we still need to resize it.
		 */
		chain->bref.data_off &= ~HAMMER2_OFF_MASK_RADIX;
		chain->bref.data_off |= (nradix & HAMMER2_OFF_MASK_RADIX);
		chain->bytes = nbytes;
	} else {
		/*
		 * Otherwise we do
		 */
		chain->bref.data_off =
			hammer2_freemap_alloc(hmp, chain->bref.type, nbytes);
		chain->bytes = nbytes;
	}

	/*
	 * Recursively mark the parent chain elements so flushes can find
	 * modified elements.
	 *
	 * NOTE: The flush code will modify a SUBMODIFIED-flagged chain
	 *	 during the flush recursion after clearing the parent's
	 *	 SUBMODIFIED bit.  We don't want to re-set the parent's
	 *	 SUBMODIFIED bit in this case!
	 */
	if ((chain->flags & HAMMER2_CHAIN_SUBMODIFIED) == 0) {
		parent = chain->parent;
		while (parent &&
		       (parent->flags & HAMMER2_CHAIN_SUBMODIFIED) == 0) {
			atomic_set_int(&parent->flags,
				       HAMMER2_CHAIN_SUBMODIFIED);
			parent = parent->parent;
		}
	}
	lockmgr(&chain->lk, LK_RELEASE);
}

/*
 * Convert a locked chain that was retrieved read-only to read-write.
 *
 * If not already marked modified a new physical block will be allocated
 * and assigned to the bref.
 *
 * allocated->modified (without calling hammer2_chain_lock()) results
 * in chain->data typically being NULL.  In this situation chain->data
 * is assigned and the target area is zero'd out.
 *
 * If the data is pointing into a bp it will be relocated to a new bp.
 * If the data is embedded we leave it alone for now.
 *
 * NOTE: Not used for DATA chain types, hammer2_chain_modify_quick() is
 *	 used instead.  We don't want to allocate a device buffer for
 *	 data that would interfere with the file's logical buffers.
 */
void
hammer2_chain_modify(hammer2_mount_t *hmp, hammer2_chain_t *chain)
{
	hammer2_chain_t *parent;
	hammer2_off_t pbase;
	struct buf *nbp;
	void *ndata;
	int error;

	KKASSERT(chain->bref.type != HAMMER2_BREF_TYPE_DATA);

	/*
	 * Setting the DIRTYBP flag will cause the buffer to be dirtied or
	 * written-out on unlock.  This bit is independent of the MODIFIED1
	 * bit because the chain may still need meta-data adjustments done
	 * by virtue of MODIFIED1 for its parent, and the buffer can be
	 * flushed out (possibly multiple times) by the OS before that.
	 */
	atomic_set_int(&chain->flags, HAMMER2_CHAIN_DIRTYBP);

	/*
	 * If the chain is already marked MODIFIED1 we can just return.
	 */
	if (chain->flags & HAMMER2_CHAIN_MODIFIED1) {
		KKASSERT(chain->data != NULL);
		return;
	}

#if 0
	/*
	 * A deleted inode may still be active but unreachable via sync
	 * because it has been disconnected from the tree.  Do not allow
	 * deleted inodes to be marked as being modified because this will
	 * bump the refs and never get resolved by the sync, leaving the
	 * inode structure allocated after umount.
	 */
	if ((chain->flags & HAMMER2_CHAIN_DELETED) &&
	    chain->bref.type == HAMMER2_BREF_TYPE_INODE) {
		KKASSERT(chain->data != NULL);
		return;
	}
#endif

	/*
	 * Set MODIFIED1 and add a chain ref to prevent destruction.  Both
	 * modified flags share the same ref.
	 */
	atomic_set_int(&chain->flags, HAMMER2_CHAIN_MODIFIED1);
	hammer2_chain_ref(hmp, chain);

	/*
	 * We must allocate the copy-on-write block.
	 *
	 * If the data is embedded no other action is required.
	 *
	 * If the data is not embedded we acquire and clear the
	 * new block.  If chain->data is not NULL we then do the
	 * copy-on-write.  chain->data will then be repointed to the new
	 * buffer and the old buffer will be released.
	 *
	 * For newly created elements with no prior allocation we go
	 * through the copy-on-write steps except without the copying part.
	 */
	if (chain != &hmp->vchain) {
		if ((hammer2_debug & 0x0001) &&
		    (chain->bref.data_off & ~HAMMER2_OFF_MASK_RADIX)) {
			kprintf("Replace %d\n", chain->bytes);
		}
		chain->bref.data_off =
			hammer2_freemap_alloc(hmp, chain->bref.type,
					      chain->bytes);
		/* XXX failed allocation */
	}

	switch(chain->bref.type) {
	case HAMMER2_BREF_TYPE_VOLUME:		/* embedded */
	case HAMMER2_BREF_TYPE_INODE:		/* embedded */
		/*
		 * Inode and Volume data already points to the embedded
		 * structure, no copy is needed
		 */
		error = 0;
		break;
	case HAMMER2_BREF_TYPE_INDIRECT:
	case HAMMER2_BREF_TYPE_DATA:
		/*
		 * data (if not NULL) points into original bp or to embedded
		 * data, copy-on-write to the new block.
		 *
		 * data (if NULL) indicates that no prior copy exists, the
		 * storage must be zero'd.
		 */
		KKASSERT(chain != &hmp->vchain);	/* safety */
		pbase = chain->bref.data_off &
			 ~(hammer2_off_t)(chain->bytes - 1);
		nbp = getblk(hmp->devvp, pbase, chain->bytes, 0, 0);
		vfs_bio_clrbuf(nbp);	/* XXX */
		error = 0;

		/*
		 * Copy or zero-fill on write depending on whether
		 * chain->data exists or not.
		 */
		ndata = nbp->b_data;
		if (chain->data) {
			bcopy(chain->data, ndata, chain->bytes);
			KKASSERT(chain->bp != NULL);
		} else {
			bzero(ndata, chain->bytes);
		}
		if (chain->bp) {
			chain->bp->b_flags |= B_RELBUF;
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
	 *
	 * NOTE: The flush code will modify a SUBMODIFIED-flagged chain
	 *	 during the flush recursion after clearing the parent's
	 *	 SUBMODIFIED bit.  We don't want to re-set the parent's
	 *	 SUBMODIFIED bit in this case!
	 */
	if ((chain->flags & HAMMER2_CHAIN_SUBMODIFIED) == 0) {
		parent = chain->parent;
		while (parent &&
		       (parent->flags & HAMMER2_CHAIN_SUBMODIFIED) == 0) {
			atomic_set_int(&parent->flags,
				       HAMMER2_CHAIN_SUBMODIFIED);
			parent = parent->parent;
		}
	}
}

/*
 * Same as hammer2_chain_modify() except the chain does not have to be
 * locked and the underlying data will NOT be copied to the new location.
 */
void
hammer2_chain_modify_quick(hammer2_mount_t *hmp, hammer2_chain_t *chain)
{
	hammer2_chain_t *parent;

	/*
	 * Set the MODIFIED1 bit and handle degenerate cases.
	 *
	 * We do not set the DIRTYBP flag, we don't want the flush code to
	 * read-modify-write the underlying physical buffer because it
	 * is probably aliased against a logical buffer.
	 *
	 * We must lock the chain but not instantiate its data.
	 *
	 * If the chain is already marked MODIFIED1 we can just return,
	 * but must interlock a failed test to avoid races.
	 */
	if (chain->flags & HAMMER2_CHAIN_MODIFIED1)
		return;
	lockmgr(&chain->lk, LK_EXCLUSIVE);
	if (chain->flags & HAMMER2_CHAIN_MODIFIED1) {
		lockmgr(&chain->lk, LK_RELEASE);
		return;
	}
	atomic_set_int(&chain->flags, HAMMER2_CHAIN_MODIFIED1);
	hammer2_chain_ref(hmp, chain);	/* ref for MODIFIED1 bit */

	/*
	 * We must allocate the copy-on-write block.
	 *
	 * If the data is embedded no other action is required.
	 *
	 * If the data is not embedded we acquire and clear the
	 * new block.  If chain->data is not NULL we then do the
	 * copy-on-write.  chain->data will then be repointed to the new
	 * buffer and the old buffer will be released.
	 *
	 * For newly created elements with no prior allocation we go
	 * through the copy-on-write steps except without the copying part.
	 */
	if (chain != &hmp->vchain) {
		if ((hammer2_debug & 0x0001) &&
		    (chain->bref.data_off & ~HAMMER2_OFF_MASK_RADIX)) {
			kprintf("Replace %d\n", chain->bytes);
		}
		chain->bref.data_off =
			hammer2_freemap_alloc(hmp, chain->bref.type,
					      chain->bytes);
		/* XXX failed allocation */
	}

	/*
	 * Recursively mark the parent chain elements so flushes can find
	 * modified elements.
	 *
	 * NOTE: The flush code will modify a SUBMODIFIED-flagged chain
	 *	 during the flush recursion after clearing the parent's
	 *	 SUBMODIFIED bit.  We don't want to re-set the parent's
	 *	 SUBMODIFIED bit in this case!
	 */
	if ((chain->flags & HAMMER2_CHAIN_SUBMODIFIED) == 0) {
		parent = chain->parent;
		while (parent &&
		       (parent->flags & HAMMER2_CHAIN_SUBMODIFIED) == 0) {
			atomic_set_int(&parent->flags,
				       HAMMER2_CHAIN_SUBMODIFIED);
			parent = parent->parent;
		}
	}
	lockmgr(&chain->lk, LK_RELEASE);
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
		if (chain->flags & HAMMER2_CHAIN_DIRTYBP) {
			if (chain->flags & HAMMER2_CHAIN_IOFLUSH) {
				atomic_clear_int(&chain->flags,
						 HAMMER2_CHAIN_IOFLUSH);
				chain->bp->b_flags |= B_RELBUF;
				bawrite(chain->bp);
			} else {
				chain->bp->b_flags |= B_CLUSTEROK;
				bdwrite(chain->bp);
			}
		} else {
			if (chain->flags & HAMMER2_CHAIN_IOFLUSH) {
				atomic_clear_int(&chain->flags,
						 HAMMER2_CHAIN_IOFLUSH);
				chain->bp->b_flags |= B_RELBUF;
				brelse(chain->bp);
			} else {
				/* bp might still be dirty */
				bqrelse(chain->bp);
			}
		}
		chain->bp = NULL;
	}
	atomic_clear_int(&chain->flags, HAMMER2_CHAIN_DIRTYBP);
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
hammer2_chain_get(hammer2_mount_t *hmp, hammer2_chain_t *parent,
		  int index, int flags)
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
		if ((flags & HAMMER2_LOOKUP_NOLOCK) == 0)
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
		KKASSERT(index >= 0 &&
			 index < parent->bytes / sizeof(hammer2_blockref_t));
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

	/*
	 * Allocate a chain structure representing the existing media
	 * entry.  Thus the chain is *not* INITIAL and certainly not
	 * MODIFIED (yet).
	 */
	chain = hammer2_chain_alloc(hmp, bref);

	/*
	 * Link the chain into its parent.  Caller is expected to hold an
	 * exclusive lock on the parent.
	 */
	chain->parent = parent;
	chain->index = index;
	if (SPLAY_INSERT(hammer2_chain_splay, &parent->shead, chain))
		panic("hammer2_chain_link: collision");
	KKASSERT(parent->refs > 0);
	atomic_add_int(&parent->refs, 1);	/* for splay entry */

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
	 *
	 * If NOLOCK is set the release will release the one-and-only lock.
	 */
	if ((flags & HAMMER2_LOOKUP_NOLOCK) == 0)
		hammer2_chain_lock(hmp, chain);
	lockmgr(&chain->lk, LK_RELEASE);

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
 * WARNING!  THIS DOES NOT RETURN KEYS IN LOGICAL KEY ORDER!  ANY KEY
 *	     WITHIN THE RANGE CAN BE RETURNED.  HOWEVER, AN ITERATION
 *	     WHICH PICKS UP WHERE WE LEFT OFF WILL CONTINUE THE SCAN.
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
		     hammer2_key_t key_beg, hammer2_key_t key_end,
		     int flags)
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
		/*
		 * Special shortcut for embedded data returns the inode
		 * itself.  Callers must detect this condition and access
		 * the embedded data (the strategy code does this for us).
		 *
		 * This is only applicable to regular files and softlinks.
		 */
		if (parent->data->ipdata.op_flags & HAMMER2_OPFLAG_DIRECTDATA) {
			hammer2_chain_ref(hmp, parent);
			if ((flags & HAMMER2_LOOKUP_NOLOCK) == 0)
				hammer2_chain_lock(hmp, parent);
			return (parent);
		}
		base = &parent->data->ipdata.u.blockset.blockref[0];
		count = HAMMER2_SET_COUNT;
		break;
	case HAMMER2_BREF_TYPE_INDIRECT:
		if (parent->data == NULL)
			panic("parent->data is NULL");
		base = &parent->data->npdata.blockref[0];
		count = parent->bytes / sizeof(hammer2_blockref_t);
		break;
	case HAMMER2_BREF_TYPE_VOLUME:
		base = &hmp->voldata.sroot_blockset.blockref[0];
		count = HAMMER2_SET_COUNT;
		break;
	default:
		panic("hammer2_chain_lookup: unrecognized blockref type: %d",
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
		if (bref->type == 0)
			continue;
		scan_beg = bref->key;
		scan_end = scan_beg + ((hammer2_key_t)1 << bref->keybits) - 1;
		if (key_beg <= scan_end && key_end >= scan_beg)
			break;
	}
	if (i == count) {
		if (key_beg == key_end)
			return (NULL);
		return (hammer2_chain_next(hmp, parentp, NULL,
					   key_beg, key_end, flags));
	}

	/*
	 * Acquire the new chain element.  If the chain element is an
	 * indirect block we must search recursively.
	 */
	chain = hammer2_chain_get(hmp, parent, i, flags);
	if (chain == NULL)
		return (NULL);

	/*
	 * If the chain element is an indirect block it becomes the new
	 * parent and we loop on it.  We must fixup the chain we loop on
	 * if the caller passed flags to us that aren't sufficient for our
	 * needs.
	 */
	if (chain->bref.type == HAMMER2_BREF_TYPE_INDIRECT) {
		hammer2_chain_put(hmp, parent);
		*parentp = parent = chain;
		if (flags & HAMMER2_LOOKUP_NOLOCK)
			hammer2_chain_lock(hmp, chain);
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
		   hammer2_key_t key_beg, hammer2_key_t key_end,
		   int flags)
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
		 * Continue iteration within current parent.  If not NULL
		 * the passed-in chain may or may not be locked, based on
		 * the LOOKUP_NOLOCK flag (passed in as returned from lookup
		 * or a prior next).
		 */
		i = chain->index + 1;
		if (flags & HAMMER2_LOOKUP_NOLOCK)
			hammer2_chain_drop(hmp, chain);
		else
			hammer2_chain_put(hmp, chain);

		/*
		 * Any scan where the lookup returned degenerate data embedded
		 * in the inode has an invalid index and must terminate.
		 */
		if (chain == parent)
			return(NULL);
		chain = NULL;
	} else if (parent->bref.type != HAMMER2_BREF_TYPE_INDIRECT) {
		/*
		 * We reached the end of the iteration.
		 */
		return (NULL);
	} else {
		/*
		 * Continue iteration with next parent unless the current
		 * parent covers the range.
		 */
		hammer2_chain_t *nparent;

		if (parent->bref.type != HAMMER2_BREF_TYPE_INDIRECT)
			return (NULL);

		scan_beg = parent->bref.key;
		scan_end = scan_beg +
			    ((hammer2_key_t)1 << parent->bref.keybits) - 1;
		if (key_beg >= scan_beg && key_end <= scan_end)
			return (NULL);

		i = parent->index + 1;
		nparent = parent->parent;
		hammer2_chain_ref(hmp, nparent);	/* ref new parent */
		hammer2_chain_unlock(hmp, parent);
		hammer2_chain_lock(hmp, nparent);	/* lock new parent */
		hammer2_chain_drop(hmp, parent);	/* drop old parent */
		*parentp = parent = nparent;
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
		count = parent->bytes / sizeof(hammer2_blockref_t);
		break;
	case HAMMER2_BREF_TYPE_VOLUME:
		base = &hmp->voldata.sroot_blockset.blockref[0];
		count = HAMMER2_SET_COUNT;
		break;
	default:
		panic("hammer2_chain_next: unrecognized blockref type: %d",
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
		if (bref->type == 0) {
			++i;
			continue;
		}
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
	chain = hammer2_chain_get(hmp, parent, i, flags);
	if (chain == NULL)
		return (NULL);

	/*
	 * If the chain element is an indirect block it becomes the new
	 * parent and we loop on it.  We may have to lock the chain when
	 * cycling it in as the new parent as it will not be locked if the
	 * caller passed NOLOCK.
	 */
	if (chain->bref.type == HAMMER2_BREF_TYPE_INDIRECT) {
		hammer2_chain_put(hmp, parent);
		*parentp = parent = chain;
		if (flags & HAMMER2_LOOKUP_NOLOCK)
			hammer2_chain_lock(hmp, chain);
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
 * key, type and size and insert it RELATIVE TO (PARENT).
 *
 * (parent) is typically either an inode or an indirect  block, acquired
 * acquired as a side effect of issuing a prior failed lookup.  parent
 * must be locked and held.  Do not pass the inode chain to this function
 * unless that is the chain returned by the failed lookup.
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
		     hammer2_chain_t *chain,
		     hammer2_key_t key, int keybits, int type, size_t bytes)
{
	hammer2_blockref_t dummy;
	hammer2_blockref_t *base;
	hammer2_blockref_t *bref;
	hammer2_chain_t dummy_chain;
	int unlock_parent = 0;
	int allocated = 0;
	int count;
	int i;

	if (chain == NULL) {
		/*
		 * First allocate media space and construct the dummy bref,
		 * then allocate the in-memory chain structure.
		 */
		bzero(&dummy, sizeof(dummy));
		dummy.type = type;
		dummy.key = key;
		dummy.keybits = keybits;
		dummy.data_off = hammer2_bytes_to_radix(bytes);
		chain = hammer2_chain_alloc(hmp, &dummy);
		allocated = 1;

		/*
		 * We set the WAS_MODIFIED flag here so the chain gets
		 * marked as modified below.
		 */
		chain->flags |= HAMMER2_CHAIN_INITIAL |
			        HAMMER2_CHAIN_WAS_MODIFIED;

		/*
		 * Recalculate bytes to reflect the actual media block
		 * allocation.
		 */
		bytes = (hammer2_off_t)1 <<
			(int)(chain->bref.data_off & HAMMER2_OFF_MASK_RADIX);
		chain->bytes = bytes;

		switch(type) {
		case HAMMER2_BREF_TYPE_VOLUME:
			panic("hammer2_chain_create: called with volume type");
			break;
		case HAMMER2_BREF_TYPE_INODE:
			KKASSERT(bytes == HAMMER2_INODE_BYTES);
			chain->data = (void *)&chain->u.ip->ip_data;
			break;
		case HAMMER2_BREF_TYPE_INDIRECT:
		case HAMMER2_BREF_TYPE_DATA:
		default:
			/* leave chain->data NULL */
			KKASSERT(chain->data == NULL);
			break;
		}
	} else {
		/*
		 * Potentially update the chain's key/keybits, but it will
		 * only be marked modified if WAS_MODIFIED is set (if it
		 * was modified at the time of its removal during a rename).
		 */
		chain->bref.key = key;
		chain->bref.keybits = keybits;
	}

again:
	/*
	 * Locate a free blockref in the parent's array
	 */
	switch(parent->bref.type) {
	case HAMMER2_BREF_TYPE_INODE:
		KKASSERT(parent->data != NULL);
		base = &parent->data->ipdata.u.blockset.blockref[0];
		count = HAMMER2_SET_COUNT;
		break;
	case HAMMER2_BREF_TYPE_INDIRECT:
		KKASSERT(parent->data != NULL);
		base = &parent->data->npdata.blockref[0];
		count = parent->bytes / sizeof(hammer2_blockref_t);
		break;
	case HAMMER2_BREF_TYPE_VOLUME:
		KKASSERT(parent->data != NULL);
		base = &hmp->voldata.sroot_blockset.blockref[0];
		count = HAMMER2_SET_COUNT;
		break;
	default:
		panic("hammer2_chain_create: unrecognized blockref type: %d",
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
	 * If no free blockref count be found we must create an indirect
	 * block and move a number of blockrefs into it.  With the parent
	 * locked we can safely lock each child in order to move it without
	 * causing a deadlock.
	 *
	 * This may return the new indirect block or the old parent depending
	 * on where the key falls.
	 */
	if (i == count) {
		hammer2_chain_t *nparent;

		nparent = hammer2_chain_create_indirect(hmp, parent,
							key, keybits);
		if (nparent == NULL) {
			if (allocated)
				hammer2_chain_free(hmp, chain);
			chain = NULL;
			goto done;
		}
		if (parent != nparent) {
			if (unlock_parent)
				hammer2_chain_put(hmp, parent);
			parent = nparent;
			unlock_parent = 1;
		}
		goto again;
	}

	/*
	 * Link the chain into its parent.
	 */
	if (chain->parent != NULL)
		panic("hammer2: hammer2_chain_create: chain already connected");
	KKASSERT(chain->parent == NULL);
	chain->parent = parent;
	chain->index = i;
	if (SPLAY_INSERT(hammer2_chain_splay, &parent->shead, chain))
		panic("hammer2_chain_link: collision");
	atomic_clear_int(&chain->flags, HAMMER2_CHAIN_DELETED);
	KKASSERT(parent->refs > 1);
	atomic_add_int(&parent->refs, 1);

	/*
	 * Additional linkage for inodes.  Reuse the parent pointer to
	 * find the parent directory.
	 */
	if (chain->bref.type == HAMMER2_BREF_TYPE_INODE) {
		hammer2_chain_t *scan = parent;
		while (scan->bref.type == HAMMER2_BREF_TYPE_INDIRECT)
			scan = scan->parent;
		if (scan->bref.type == HAMMER2_BREF_TYPE_INODE)
			chain->u.ip->pip = scan->u.ip;
	}

	/*
	 * WAS_MODIFIED indicates that this is a newly-created chain element
	 * rather than a renamed chain element.  In this situation we want
	 * to mark non-data chain elements as modified in order to resolve
	 * the data pointer.
	 *
	 * data chain elements are marked modified but WITHOUT resolving the
	 * data pointer, as a device buffer would interfere otherwise.
	 *
	 * Chain elements with embedded data will not issue I/O at this time.
	 * A new block will be allocated for the buffer but not instantiated.
	 *
	 * NON-DATA chain elements which do not use embedded data will
	 * allocate the new block AND instantiate its buffer cache buffer,
	 * pointing the data at the bp.
	 */
	if (chain->flags & HAMMER2_CHAIN_WAS_MODIFIED) {
		atomic_clear_int(&chain->flags, HAMMER2_CHAIN_WAS_MODIFIED);
		if (chain->bref.type == HAMMER2_BREF_TYPE_DATA)
			hammer2_chain_modify_quick(hmp, chain);
		else
			hammer2_chain_modify(hmp, chain);
	}

done:
	if (unlock_parent)
		hammer2_chain_put(hmp, parent);
	return (chain);
}

/*
 * Create an indirect block that covers one or more of the elements in the
 * current parent.  Either returns the existing parent with no locking or
 * ref changes or returns the new indirect block locked and referenced,
 * depending on what the specified key falls into.
 *
 * The key/keybits for the indirect mode only needs to follow three rules:
 *
 * (1) That all elements underneath it fit within its key space and
 *
 * (2) That all elements outside it are outside its key space.
 *
 * (3) When creating the new indirect block any elements in the current
 *     parent that fit within the new indirect block's keyspace must be
 *     moved into the new indirect block.
 *
 * (4) The keyspace chosen for the inserted indirect block CAN cover a wider
 *     keyspace the the current parent, but lookup/iteration rules will
 *     ensure (and must ensure) that rule (2) for all parents leading up
 *     to the nearest inode or the root volume header is adhered to.  This
 *     is accomplished by always recursing through matching keyspaces in
 *     the hammer2_chain_lookup() and hammer2_chain_next() API.
 *
 * The current implementation calculates the current worst-case keyspace by
 * iterating the current parent and then divides it into two halves, choosing
 * whichever half has the most elements (not necessarily the half containing
 * the requested key).
 *
 * We can also opt to use the half with the least number of elements.  This
 * causes lower-numbered keys (aka logical file offsets) to recurse through
 * fewer indirect blocks and higher-numbered keys to recurse through more.
 * This also has the risk of not moving enough elements to the new indirect
 * block and being forced to create several indirect blocks before the element
 * can be inserted.
 */
static
hammer2_chain_t *
hammer2_chain_create_indirect(hammer2_mount_t *hmp, hammer2_chain_t *parent,
			      hammer2_key_t create_key, int create_bits)
{
	hammer2_blockref_t *base;
	hammer2_blockref_t *bref;
	hammer2_chain_t *chain;
	hammer2_chain_t *ichain;
	hammer2_chain_t dummy;
	hammer2_key_t key = create_key;
	int keybits = create_bits;
	int locount = 0;
	int hicount = 0;
	int count;
	int nbytes;
	int i;

	/*
	 * Mark the parent modified so our base[] pointer remains valid
	 * while we move entries.
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
		count = parent->bytes / sizeof(hammer2_blockref_t);
		break;
	case HAMMER2_BREF_TYPE_VOLUME:
		base = &hmp->voldata.sroot_blockset.blockref[0];
		count = HAMMER2_SET_COUNT;
		break;
	default:
		panic("hammer2_chain_create_indirect: "
		      "unrecognized blockref type: %d",
		      parent->bref.type);
		count = 0;
		break;
	}

	/*
	 * Scan for an unallocated bref, also skipping any slots occupied
	 * by in-memory chain elements that may not yet have been updated
	 * in the parent's bref array.
	 */
	bzero(&dummy, sizeof(dummy));
	for (i = 0; i < count; ++i) {
		int nkeybits;

		bref = &base[i];
		if (bref->type == 0) {
			dummy.index = i;
			chain = SPLAY_FIND(hammer2_chain_splay, &parent->shead,
					   &dummy);
			if (chain == NULL)
				continue;
			bref = &chain->bref;
		}

		/*
		 * Expand our calculated key range (key, keybits) to fit
		 * the scanned key.  nkeybits represents the full range
		 * that we will later cut in half (two halves @ nkeybits - 1).
		 */
		nkeybits = keybits;
		if (nkeybits < bref->keybits)
			nkeybits = bref->keybits;
		while ((~(((hammer2_key_t)1 << nkeybits) - 1) &
		        (key ^ bref->key)) != 0) {
			++nkeybits;
		}

		/*
		 * If the new key range is larger we have to determine
		 * which side of the new key range the existing keys fall
		 * under by checking the high bit, then collapsing the
		 * locount into the hicount or vise-versa.
		 */
		if (keybits != nkeybits) {
			if (((hammer2_key_t)1 << (nkeybits - 1)) & key) {
				hicount += locount;
				locount = 0;
			} else {
				locount += hicount;
				hicount = 0;
			}
			keybits = nkeybits;
		}

		/*
		 * The newly scanned key will be in the lower half or the
		 * higher half of the (new) key range.
		 */
		if (((hammer2_key_t)1 << (nkeybits - 1)) & bref->key)
			++hicount;
		else
			++locount;
	}

	/*
	 * Adjust keybits to represent half of the full range calculated
	 * above.
	 */
	--keybits;

	/*
	 * Select whichever half contains the most elements.  Theoretically
	 * we can select either side as long as it contains at least one
	 * element (in order to ensure that a free slot is present to hold
	 * the indirect block).
	 */
	key &= ~(((hammer2_key_t)1 << keybits) - 1);
	if (hammer2_indirect_optimize) {
		/*
		 * Insert node for least number of keys, this will arrange
		 * the first few blocks of a large file or the first few
		 * inodes in a directory with fewer indirect blocks when
		 * created linearly.
		 */
		if (hicount < locount && hicount != 0)
			key |= (hammer2_key_t)1 << keybits;
		else
			key &= ~(hammer2_key_t)1 << keybits;
	} else {
		/*
		 * Insert node for most number of keys, best for heavily
		 * fragmented files.
		 */
		if (hicount > locount)
			key |= (hammer2_key_t)1 << keybits;
		else
			key &= ~(hammer2_key_t)1 << keybits;
	}

	/*
	 * How big should our new indirect block be?  It has to be at least
	 * as large as its parent.
	 */
	if (parent->bref.type == HAMMER2_BREF_TYPE_INODE)
		nbytes = HAMMER2_IND_BYTES_MIN;
	else
		nbytes = HAMMER2_IND_BYTES_MAX;
	if (nbytes < count * sizeof(hammer2_blockref_t))
		nbytes = count * sizeof(hammer2_blockref_t);

	/*
	 * Ok, create our new indirect block
	 */
	dummy.bref.type = HAMMER2_BREF_TYPE_INDIRECT;
	dummy.bref.key = key;
	dummy.bref.keybits = keybits;
	dummy.bref.data_off = hammer2_bytes_to_radix(nbytes);
	ichain = hammer2_chain_alloc(hmp, &dummy.bref);
	ichain->flags |= HAMMER2_CHAIN_INITIAL;

	/*
	 * Iterate the original parent and move the matching brefs into
	 * the new indirect block.
	 */
	for (i = 0; i < count; ++i) {
		/*
		 * For keying purposes access the bref from the media or
		 * from our in-memory cache.  In cases where the in-memory
		 * cache overrides the media the keyrefs will be the same
		 * anyway so we can avoid checking the cache when the media
		 * has a key.
		 */
		bref = &base[i];
		if (bref->type == 0) {
			dummy.index = i;
			chain = SPLAY_FIND(hammer2_chain_splay, &parent->shead,
					   &dummy);
			if (chain == NULL) {
				/*
				 * Select index indirect block is placed in
				 */
				if (ichain->index < 0)
					ichain->index = i;
				continue;
			}
			bref = &chain->bref;
		}

		/*
		 * Skip keys not in the chosen half (low or high), only bit
		 * (keybits - 1) needs to be compared but for safety we
		 * will compare all msb bits plus that bit again.
		 */
		if ((~(((hammer2_key_t)1 << keybits) - 1) &
		    (key ^ bref->key)) != 0) {
			continue;
		}

		/*
		 * This element is being moved, its slot is available
		 * for our indirect block.
		 */
		if (ichain->index < 0)
			ichain->index = i;

		/*
		 * Load the new indirect block by acquiring or allocating
		 * the related chain entries, then simply move it to the
		 * new parent (ichain).
		 *
		 * Flagging the new chain entry MOVED will cause a flush
		 * to synchronize its block into the new indirect block.
		 * The chain is unlocked after being moved but needs to
		 * retain a reference for the MOVED state
		 *
		 * We must still set SUBMODIFIED in the parent but we do
		 * that after the loop.
		 *
		 * XXX we really need a lock here but we don't need the
		 *     data.  NODATA feature needed.
		 */
		chain = hammer2_chain_get(hmp, parent, i,
					  HAMMER2_LOOKUP_NOLOCK);
		SPLAY_REMOVE(hammer2_chain_splay, &parent->shead, chain);
		if (SPLAY_INSERT(hammer2_chain_splay, &ichain->shead, chain))
			panic("hammer2_chain_create_indirect: collision");
		chain->parent = ichain;
		bzero(&base[i], sizeof(base[i]));
		atomic_add_int(&parent->refs, -1);
		atomic_add_int(&ichain->refs, 1);
		if (chain->flags & HAMMER2_CHAIN_MOVED) {
			/* We don't need the ref from the chain_get */
			hammer2_chain_drop(hmp, chain);
		} else {
			/* MOVED bit inherits the ref from the chain_get */
			atomic_set_int(&chain->flags, HAMMER2_CHAIN_MOVED);
		}
		KKASSERT(parent->refs > 0);
		chain = NULL;
	}

	/*
	 * Insert the new indirect block into the parent now that we've
	 * cleared out some entries in the parent.  We calculated a good
	 * insertion index in the loop above (ichain->index).
	 */
	KKASSERT(ichain->index >= 0);
	if (SPLAY_INSERT(hammer2_chain_splay, &parent->shead, ichain))
		panic("hammer2_chain_create_indirect: ichain insertion");
	ichain->parent = parent;
	atomic_add_int(&parent->refs, 1);

	/*
	 * Mark the new indirect block modified after insertion, which
	 * will propagate up through parent all the way to the root and
	 * also allocate the physical block in ichain for our caller,
	 * and assign ichain->data to a pre-zero'd space (because there
	 * is not prior data to copy into it).
	 *
	 * We have to set SUBMODIFIED in ichain's flags manually so the
	 * flusher knows it has to recurse through it to get to all of
	 * our moved blocks.
	 */
	hammer2_chain_modify(hmp, ichain);
	atomic_set_int(&ichain->flags, HAMMER2_CHAIN_SUBMODIFIED);

	/*
	 * Figure out what to return.
	 */
	if (create_bits >= keybits) {
		/*
		 * Key being created is way outside the key range,
		 * return the original parent.
		 */
		hammer2_chain_put(hmp, ichain);
	} else if (~(((hammer2_key_t)1 << keybits) - 1) &
		   (create_key ^ key)) {
		/*
		 * Key being created is outside the key range,
		 * return the original parent.
		 */
		hammer2_chain_put(hmp, ichain);
	} else {
		/*
		 * Otherwise its in the range, return the new parent.
		 */
		parent = ichain;
	}

	return(parent);
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
hammer2_chain_delete(hammer2_mount_t *hmp, hammer2_chain_t *parent,
		     hammer2_chain_t *chain)
{
	hammer2_blockref_t *base;
	int count;

	if (chain->parent != parent)
		panic("hammer2_chain_delete: parent mismatch");

	/*
	 * Mark the parent modified so our base[] pointer remains valid
	 * while we move entries.
	 *
	 * Calculate the blockref reference in the parent
	 */
	hammer2_chain_modify(hmp, parent);

	switch(parent->bref.type) {
	case HAMMER2_BREF_TYPE_INODE:
		base = &parent->data->ipdata.u.blockset.blockref[0];
		count = HAMMER2_SET_COUNT;
		break;
	case HAMMER2_BREF_TYPE_INDIRECT:
		base = &parent->data->npdata.blockref[0];
		count = parent->bytes / sizeof(hammer2_blockref_t);
		break;
	case HAMMER2_BREF_TYPE_VOLUME:
		base = &hmp->voldata.sroot_blockset.blockref[0];
		count = HAMMER2_SET_COUNT;
		break;
	default:
		panic("hammer2_chain_delete: unrecognized blockref type: %d",
		      parent->bref.type);
		count = 0;
		break;
	}

	/*
	 * Disconnect the bref in the parent, remove the chain, and
	 * disconnect in-memory fields from the parent.
	 */
	KKASSERT(chain->index >= 0 && chain->index < count);
	base += chain->index;
	bzero(base, sizeof(*base));

	SPLAY_REMOVE(hammer2_chain_splay, &parent->shead, chain);
	atomic_set_int(&chain->flags, HAMMER2_CHAIN_DELETED);
	atomic_add_int(&parent->refs, -1);	/* for splay entry */
	chain->index = -1;

	chain->parent = NULL;
	if (chain->bref.type == HAMMER2_BREF_TYPE_INODE)
		chain->u.ip->pip = NULL;

	/*
	 * Nobody references the underlying object any more so we can
	 * clear any pending modification(s) on it.  This can theoretically
	 * recurse downward but even just clearing the bit on this item
	 * will effectively recurse if someone is doing a rm -rf and greatly
	 * reduce the I/O required.
	 *
	 * The MODIFIED1 bit is cleared but we have to remember the old state
	 * in case this deletion is related to a rename.  The ref on the
	 * chain is shared by both modified flags.
	 */
	if (chain->flags & HAMMER2_CHAIN_MODIFIED1) {
		atomic_clear_int(&chain->flags, HAMMER2_CHAIN_MODIFIED1);
		atomic_set_int(&chain->flags, HAMMER2_CHAIN_WAS_MODIFIED);
		hammer2_chain_drop(hmp, chain);
	}
}

/*
 * Recursively flush the specified chain.  The chain is locked and
 * referenced by the caller and will remain so on return.
 *
 * This cannot be called with the volume header's vchain (yet).
 *
 * PASS1 - clear the MODIFIED1 bit.
 */
static void
hammer2_chain_flush_pass1(hammer2_mount_t *hmp, hammer2_chain_t *chain)
{
	hammer2_blockref_t *bref;
	hammer2_off_t pbase;
	struct buf *bp;

	/*
	 * Flush any children of this chain entry.
	 */
	if (chain->flags & HAMMER2_CHAIN_SUBMODIFIED) {
		hammer2_blockref_t *base;
		hammer2_chain_t *scan;
		hammer2_chain_t *next;
		int count;
		int submodified = 0;

		/*
		 * Modifications to the children will propagate up, forcing
		 * us to become modified and copy-on-write too.
		 *
		 * Clear SUBMODIFIED now, races during the flush will re-set
		 * it.
		 */
		hammer2_chain_modify(hmp, chain);
		atomic_clear_int(&chain->flags, HAMMER2_CHAIN_SUBMODIFIED);

		/*
		 * The blockref in the parent's array must be repointed at
		 * the new block allocated by the child after its flush.
		 *
		 * Calculate the base of the array.
		 */
		switch(chain->bref.type) {
		case HAMMER2_BREF_TYPE_INODE:
			base = &chain->data->ipdata.u.blockset.blockref[0];
			count = HAMMER2_SET_COUNT;
			break;
		case HAMMER2_BREF_TYPE_INDIRECT:
			base = &chain->data->npdata.blockref[0];
			count = chain->bytes / sizeof(hammer2_blockref_t);
			break;
		case HAMMER2_BREF_TYPE_VOLUME:
			base = &hmp->voldata.sroot_blockset.blockref[0];
			count = HAMMER2_SET_COUNT;
			break;
		default:
			base = NULL;
			panic("hammer2_chain_get: unrecognized blockref type: %d",
			      chain->bref.type);
		}

		/*
		 * Flush the children and update the blockrefs in the parent.
		 * Be careful of ripouts during the loop.
		 */
		next = SPLAY_MIN(hammer2_chain_splay, &chain->shead);
		while ((scan = next) != NULL) {
			next = SPLAY_NEXT(hammer2_chain_splay, &chain->shead,
					  scan);
			if ((scan->flags & (HAMMER2_CHAIN_SUBMODIFIED |
					    HAMMER2_CHAIN_MODIFIED1 |
					    HAMMER2_CHAIN_MOVED)) == 0) {
				continue;
			}
			KKASSERT(scan->index >= 0 && scan->index < count);
			hammer2_chain_ref(hmp, scan);
			hammer2_chain_lock(hmp, scan);
			hammer2_chain_flush_pass1(hmp, scan);
			if (scan->flags & (HAMMER2_CHAIN_SUBMODIFIED |
					   HAMMER2_CHAIN_MODIFIED1)) {
				submodified = 1;
			} else {
				base[scan->index] = scan->bref;
				if (scan->flags & HAMMER2_CHAIN_MOVED) {
					atomic_clear_int(&scan->flags,
						 HAMMER2_CHAIN_MOVED);
					hammer2_chain_drop(hmp, scan);
				}
			}
			hammer2_chain_put(hmp, scan);
		}
		if (submodified) {
			atomic_set_int(&chain->flags,
				       HAMMER2_CHAIN_SUBMODIFIED);
		}
	}

	/*
	 * Flush this chain entry only if it is marked modified.
	 */
	if ((chain->flags & HAMMER2_CHAIN_MODIFIED1) == 0)
		return;

	/*
	 * Clear MODIFIED1 and set HAMMER2_CHAIN_MOVED.  The MODIFIED{1,2}
	 * bits own a single parent ref and the MOVED bit owns its own
	 * parent ref.
	 */
	atomic_clear_int(&chain->flags, HAMMER2_CHAIN_MODIFIED1);
	if (chain->flags & HAMMER2_CHAIN_MOVED) {
		hammer2_chain_drop(hmp, chain);
	} else {
		/* inherit ref from the MODIFIED1 we cleared */
		atomic_set_int(&chain->flags, HAMMER2_CHAIN_MOVED);
	}

	/*
	 * Deleted nodes do not have to be flushed.
	 */
	if (chain->flags & HAMMER2_CHAIN_DELETED)
		return;

	/*
	 * If this is part of a recursive flush we can go ahead and write
	 * out the buffer cache buffer and pass a new bref back up the chain.
	 *
	 * This will never be a volume header.
	 */
	switch(chain->bref.type) {
	case HAMMER2_BREF_TYPE_VOLUME:
		/*
		 * The volume header is flushed manually by the syncer, not
		 * here.
		 */
		break;
	case HAMMER2_BREF_TYPE_DATA:
		/*
		 * Data elements have already been flushed via the logical
		 * file buffer cache.  Their hash was set in the bref by
		 * the vop_write code.
		 */
		break;
	default:
		KKASSERT(chain->data != NULL);
		bref = &chain->bref;

		pbase = bref->data_off & ~(hammer2_off_t)(chain->bytes - 1);
		KKASSERT(pbase != 0);	/* not the root volume header */

		if (chain->bp == NULL) {
			/*
			 * The data is embedded, we have to acquire the
			 * buffer cache buffer and copy the data into it.
			 */
			bp = getblk(hmp->devvp, pbase, chain->bytes, 0, 0);

			/*
			 * Copy the data to the buffer, mark the buffer
			 * dirty, and convert the chain to unmodified.
			 */
			bcopy(chain->data, bp->b_data, chain->bytes);
			bp->b_flags |= B_CLUSTEROK;
			bdwrite(bp);
			bp = NULL;
			chain->bref.check.iscsi32.value =
				hammer2_icrc32(chain->data, chain->bytes);
		} else {
			chain->bref.check.iscsi32.value =
				hammer2_icrc32(chain->data, chain->bytes);
		}
	}

	/*
	 * Special handling
	 */
	bref = &chain->bref;

	switch(bref->type) {
	case HAMMER2_BREF_TYPE_VOLUME:
		KKASSERT(chain->data != NULL);
		KKASSERT(chain->bp == NULL);

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

#if 0
/*
 * PASS2 - not yet implemented (should be called only with the root chain?)
 */
static void
hammer2_chain_flush_pass2(hammer2_mount_t *hmp, hammer2_chain_t *chain)
{
}
#endif

void
hammer2_chain_flush(hammer2_mount_t *hmp, hammer2_chain_t *chain)
{
	hammer2_chain_flush_pass1(hmp, chain);
}
