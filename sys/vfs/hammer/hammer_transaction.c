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
 * 
 * $DragonFly: src/sys/vfs/hammer/hammer_transaction.c,v 1.14 2008/04/29 01:10:37 dillon Exp $
 */

#include "hammer.h"

static hammer_tid_t hammer_alloc_tid(hammer_transaction_t trans, int count);


/*
 * Start a standard transaction.
 */
void
hammer_start_transaction(struct hammer_transaction *trans,
			 struct hammer_mount *hmp)
{
	int error;

	trans->type = HAMMER_TRANS_STD;
	trans->hmp = hmp;
	trans->rootvol = hammer_get_root_volume(hmp, &error);
	KKASSERT(error == 0);
	trans->tid = 0;
	trans->time = hammer_alloc_tid(trans, 1);
}

/*
 * Start a simple read-only transaction.  This will not stall.
 */
void
hammer_simple_transaction(struct hammer_transaction *trans,
			  struct hammer_mount *hmp)
{
	int error;

	trans->type = HAMMER_TRANS_RO;
	trans->hmp = hmp;
	trans->rootvol = hammer_get_root_volume(hmp, &error);
	KKASSERT(error == 0);
	trans->tid = 0;
	trans->time = hammer_alloc_tid(trans, 1);
}

/*
 * Start a transaction using a particular TID.  Used by the sync code.
 * This does not stall.
 */
void
hammer_start_transaction_fls(struct hammer_transaction *trans,
			     struct hammer_mount *hmp)
{
	int error;

	trans->type = HAMMER_TRANS_FLS;
	trans->hmp = hmp;
	trans->rootvol = hammer_get_root_volume(hmp, &error);
	KKASSERT(error == 0);
	trans->tid = hammer_alloc_tid(trans, 1);
	trans->time = trans->tid;
}

void
hammer_done_transaction(struct hammer_transaction *trans)
{
	hammer_rel_volume(trans->rootvol, 0);
	trans->rootvol = NULL;
}

/*
 * Note: Successive transaction ids must be at least 2 apart so the
 * B-Tree code can make a separator that does not match either the
 * left or right hand sides.
 */
static hammer_tid_t
hammer_alloc_tid(hammer_transaction_t trans, int count)
{
	struct timespec ts;
	hammer_tid_t tid;

	getnanotime(&ts);
	tid = ts.tv_sec * 1000000000LL + ts.tv_nsec;
	if (tid < trans->hmp->next_tid)
		tid = trans->hmp->next_tid;
	if (tid >= 0xFFFFFFFFFFFFF000ULL)
		panic("hammer_start_transaction: Ran out of TIDs!");
	trans->hmp->next_tid = tid + count * 2;
	if (hammer_debug_tid) {
		kprintf("alloc_tid %016llx (0x%08x)\n",
			tid, (int)(tid / 1000000000LL));
	}
	return(tid);
}

/*
 * Allocate an object id
 */
hammer_tid_t
hammer_alloc_objid(hammer_transaction_t trans, hammer_inode_t dip)
{
	hammer_objid_cache_t ocp;
	hammer_tid_t tid;

	while ((ocp = dip->objid_cache) == NULL) {
		if (trans->hmp->objid_cache_count < OBJID_CACHE_SIZE) {
			ocp = kmalloc(sizeof(*ocp), M_HAMMER, M_WAITOK|M_ZERO);
			ocp->next_tid = hammer_alloc_tid(trans,
							 OBJID_CACHE_BULK);
			ocp->count = OBJID_CACHE_BULK;
			TAILQ_INSERT_HEAD(&trans->hmp->objid_cache_list, ocp,
					  entry);
			++trans->hmp->objid_cache_count;
			/* may have blocked, recheck */
			if (dip->objid_cache == NULL) {
				dip->objid_cache = ocp;
				ocp->dip = dip;
			}
		} else {
			ocp = TAILQ_FIRST(&trans->hmp->objid_cache_list);
			if (ocp->dip)
				ocp->dip->objid_cache = NULL;
			dip->objid_cache = ocp;
			ocp->dip = dip;
		}
	}
	TAILQ_REMOVE(&trans->hmp->objid_cache_list, ocp, entry);
	tid = ocp->next_tid;
	ocp->next_tid += 2;
	if (--ocp->count == 0) {
		dip->objid_cache = NULL;
		--trans->hmp->objid_cache_count;
		ocp->dip = NULL;
		kfree(ocp, M_HAMMER);
	} else {
		TAILQ_INSERT_TAIL(&trans->hmp->objid_cache_list, ocp, entry);
	}
	return(tid);
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
		kfree(ocp, M_HAMMER);
	}
}

