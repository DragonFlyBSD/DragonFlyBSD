/*
 * Copyright (c) 2007-2008 The DragonFly Project.  All rights reserved.
 * 
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
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

#include "hammer.h"

static u_int32_t ocp_allocbit(hammer_objid_cache_t ocp, u_int32_t n);


/*
 * Start a standard transaction.
 *
 * May be called without fs_token
 */
void
hammer_start_transaction(struct hammer_transaction *trans,
			 struct hammer_mount *hmp)
{
	struct timeval tv;
	int error;

	trans->type = HAMMER_TRANS_STD;
	trans->hmp = hmp;
	trans->rootvol = hammer_get_root_volume(hmp, &error);
	KKASSERT(error == 0);
	trans->tid = 0;
	trans->sync_lock_refs = 0;
	trans->flags = 0;

	getmicrotime(&tv);
	trans->time = (unsigned long)tv.tv_sec * 1000000ULL + tv.tv_usec;
	trans->time32 = (u_int32_t)tv.tv_sec;
}

/*
 * Start a simple read-only transaction.  This will not stall.
 *
 * May be called without fs_token
 */
void
hammer_simple_transaction(struct hammer_transaction *trans,
			  struct hammer_mount *hmp)
{
	struct timeval tv;
	int error;

	trans->type = HAMMER_TRANS_RO;
	trans->hmp = hmp;
	trans->rootvol = hammer_get_root_volume(hmp, &error);
	KKASSERT(error == 0);
	trans->tid = 0;
	trans->sync_lock_refs = 0;
	trans->flags = 0;

	getmicrotime(&tv);
	trans->time = (unsigned long)tv.tv_sec * 1000000ULL + tv.tv_usec;
	trans->time32 = (u_int32_t)tv.tv_sec;
}

/*
 * Start a transaction using a particular TID.  Used by the sync code.
 * This does not stall.
 *
 * This routine may only be called from the flusher thread.  We predispose
 * sync_lock_refs, implying serialization against the synchronization stage
 * (which the flusher is responsible for).
 */
void
hammer_start_transaction_fls(struct hammer_transaction *trans,
			     struct hammer_mount *hmp)
{
	struct timeval tv;
	int error;

	bzero(trans, sizeof(*trans));

	trans->type = HAMMER_TRANS_FLS;
	trans->hmp = hmp;
	trans->rootvol = hammer_get_root_volume(hmp, &error);
	KKASSERT(error == 0);
	trans->tid = hammer_alloc_tid(hmp, 1);
	trans->sync_lock_refs = 1;
	trans->flags = 0;

	getmicrotime(&tv);
	trans->time = (unsigned long)tv.tv_sec * 1000000ULL + tv.tv_usec;
	trans->time32 = (u_int32_t)tv.tv_sec;
}

/*
 * May be called without fs_token
 */
void
hammer_done_transaction(struct hammer_transaction *trans)
{
	int expected_lock_refs __debugvar;

	hammer_rel_volume(trans->rootvol, 0);
	trans->rootvol = NULL;
	expected_lock_refs = (trans->type == HAMMER_TRANS_FLS) ? 1 : 0;
	KKASSERT(trans->sync_lock_refs == expected_lock_refs);
	trans->sync_lock_refs = 0;
	if (trans->type != HAMMER_TRANS_FLS) {
		if (trans->flags & HAMMER_TRANSF_NEWINODE) {
			lwkt_gettoken(&trans->hmp->fs_token);
			hammer_inode_waitreclaims(trans);
			lwkt_reltoken(&trans->hmp->fs_token);
		}
	}
}

/*
 * Allocate (count) TIDs.  If running in multi-master mode the returned
 * base will be aligned to a 16-count plus the master id (0-15).  
 * Multi-master mode allows non-conflicting to run and new objects to be
 * created on multiple masters in parallel.  The transaction id identifies
 * the original master.  The object_id is also subject to this rule in
 * order to allow objects to be created on multiple masters in parallel.
 *
 * Directories may pre-allocate a large number of object ids (100,000).
 *
 * NOTE: There is no longer a requirement that successive transaction
 *	 ids be 2 apart for separator generation.
 *
 * NOTE: When called by pseudo-backends such as ioctls the allocated
 *	 TID will be larger then the current flush TID, if a flush is running,
 *	 so any mirroring will pick the records up on a later flush.
 */
hammer_tid_t
hammer_alloc_tid(hammer_mount_t hmp, int count)
{
	hammer_tid_t tid;

	if (hmp->master_id < 0) {
		tid = hmp->next_tid + 1;
		hmp->next_tid = tid + count;
	} else {
		tid = (hmp->next_tid + HAMMER_MAX_MASTERS) &
		      ~(hammer_tid_t)(HAMMER_MAX_MASTERS - 1);
		hmp->next_tid = tid + count * HAMMER_MAX_MASTERS;
		tid |= hmp->master_id;
	}
	if (tid >= 0xFFFFFFFFFF000000ULL)
		panic("hammer_start_transaction: Ran out of TIDs!");
	if (hammer_debug_tid)
		kprintf("alloc_tid %016llx\n", (long long)tid);
	return(tid);
}

/*
 * Allocate an object id.
 *
 * We use the upper OBJID_CACHE_BITS bits of the namekey to try to match
 * the low bits of the objid we allocate.
 */
hammer_tid_t
hammer_alloc_objid(hammer_mount_t hmp, hammer_inode_t dip, int64_t namekey)
{
	hammer_objid_cache_t ocp;
	hammer_tid_t tid;
	u_int32_t n;

	while ((ocp = dip->objid_cache) == NULL) {
		if (hmp->objid_cache_count < OBJID_CACHE_SIZE) {
			ocp = kmalloc(sizeof(*ocp), hmp->m_misc,
				      M_WAITOK|M_ZERO);
			ocp->base_tid = hammer_alloc_tid(hmp,
							OBJID_CACHE_BULK * 2);
			ocp->base_tid += OBJID_CACHE_BULK_MASK64;
			ocp->base_tid &= ~OBJID_CACHE_BULK_MASK64;
			/* may have blocked, recheck */
			if (dip->objid_cache == NULL) {
				TAILQ_INSERT_TAIL(&hmp->objid_cache_list,
						  ocp, entry);
				++hmp->objid_cache_count;
				dip->objid_cache = ocp;
				ocp->dip = dip;
			} else {
				kfree(ocp, hmp->m_misc);
			}
		} else {
			/*
			 * Steal one from another directory?
			 *
			 * Throw away ocp's that are more then half full, they
			 * aren't worth stealing.
			 */
			ocp = TAILQ_FIRST(&hmp->objid_cache_list);
			if (ocp->dip)
				ocp->dip->objid_cache = NULL;
			if (ocp->count >= OBJID_CACHE_BULK / 2) {
				TAILQ_REMOVE(&hmp->objid_cache_list,
					     ocp, entry);
				--hmp->objid_cache_count;
				kfree(ocp, hmp->m_misc);
			} else {
				dip->objid_cache = ocp;
				ocp->dip = dip;
			}
		}
	}
	TAILQ_REMOVE(&hmp->objid_cache_list, ocp, entry);

	/*
	 * Allocate inode numbers uniformly.
	 */

	n = (namekey >> (63 - OBJID_CACHE_BULK_BITS)) & OBJID_CACHE_BULK_MASK;
	n = ocp_allocbit(ocp, n);
	tid = ocp->base_tid + n;

#if 0
	/*
	 * The TID is incremented by 1 or by 16 depending what mode the
	 * mount is operating in.
	 */
	ocp->next_tid += (hmp->master_id < 0) ? 1 : HAMMER_MAX_MASTERS;
#endif
	if (ocp->count >= OBJID_CACHE_BULK * 3 / 4) {
		dip->objid_cache = NULL;
		--hmp->objid_cache_count;
		ocp->dip = NULL;
		kfree(ocp, hmp->m_misc);
	} else {
		TAILQ_INSERT_TAIL(&hmp->objid_cache_list, ocp, entry);
	}
	return(tid);
}

/*
 * Allocate a bit starting with bit n.  Wrap if necessary.
 *
 * This routine is only ever called if a bit is available somewhere
 * in the bitmap.
 */
static u_int32_t
ocp_allocbit(hammer_objid_cache_t ocp, u_int32_t n)
{
	u_int32_t n0;

	n0 = (n >> 5) & 31;
	n &= 31;

	while (ocp->bm1[n0] & (1 << n)) {
		if (ocp->bm0 & (1 << n0)) {
			n0 = (n0 + 1) & 31;
			n = 0;
		} else if (++n == 32) {
			n0 = (n0 + 1) & 31;
			n = 0;
		}
	}
	++ocp->count;
	ocp->bm1[n0] |= 1 << n;
	if (ocp->bm1[n0] == 0xFFFFFFFFU)
		ocp->bm0 |= 1 << n0;
	return((n0 << 5) + n);
}

void
hammer_clear_objid(hammer_inode_t dip)
{
	hammer_objid_cache_t ocp;

	if ((ocp = dip->objid_cache) != NULL) {
		dip->objid_cache = NULL;
		ocp->dip = NULL;
		TAILQ_REMOVE(&dip->hmp->objid_cache_list, ocp, entry);
		TAILQ_INSERT_HEAD(&dip->hmp->objid_cache_list, ocp, entry);
	}
}

void
hammer_destroy_objid_cache(hammer_mount_t hmp)
{
	hammer_objid_cache_t ocp;

	while ((ocp = TAILQ_FIRST(&hmp->objid_cache_list)) != NULL) {
		TAILQ_REMOVE(&hmp->objid_cache_list, ocp, entry);
		if (ocp->dip)
			ocp->dip->objid_cache = NULL;
		kfree(ocp, hmp->m_misc);
		--hmp->objid_cache_count;
	}
	KKASSERT(hmp->objid_cache_count == 0);
}

