/*
 * Copyright (c) 2015 The DragonFly Project.  All rights reserved.
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
/*
 * This module implements various PFS-based helper threads.
 */
#include "hammer2.h"

static void hammer2_sync_slaves(hammer2_syncthr_t *thr, hammer2_pfs_t *pmp,
			hammer2_cluster_t **cparentp);
static void hammer2_update_pfs_status(hammer2_pfs_t *pmp,
			hammer2_cluster_t *cparent);

/*
 * Initialize the suspplied syncthr structure, starting the specified
 * thread.
 */
void
hammer2_syncthr_create(hammer2_syncthr_t *thr, hammer2_pfs_t *pmp,
		       void (*func)(void *arg))
{
	lockinit(&thr->lk, "h2syncthr", 0, 0);
	thr->pmp = pmp;
	lwkt_create(func, thr, &thr->td, NULL, 0, -1, "h2pfs");
}

/*
 * Terminate a syncthr.  This function will silently return if the syncthr
 * was never initialized or has already been deleted.
 *
 * This is accomplished by setting the STOP flag and waiting for the td
 * structure to become NULL.
 */
void
hammer2_syncthr_delete(hammer2_syncthr_t *thr)
{
	if (thr->td == NULL)
		return;
	lockmgr(&thr->lk, LK_EXCLUSIVE);
	atomic_set_int(&thr->flags, HAMMER2_SYNCTHR_STOP);
	wakeup(&thr->flags);
	while (thr->td) {
		lksleep(thr, &thr->lk, 0, "h2thr", hz);
	}
	lockmgr(&thr->lk, LK_RELEASE);
	thr->pmp = NULL;
	lockuninit(&thr->lk);
}

/*
 * Asynchronous remaster request.  Ask the synchronization thread to
 * start over soon (as if it were frozen and unfrozen, but without waiting).
 * The thread always recalculates mastership relationships when restarting.
 */
void
hammer2_syncthr_remaster(hammer2_syncthr_t *thr)
{
	if (thr->td == NULL)
		return;
	lockmgr(&thr->lk, LK_EXCLUSIVE);
	atomic_set_int(&thr->flags, HAMMER2_SYNCTHR_REMASTER);
	wakeup(&thr->flags);
	lockmgr(&thr->lk, LK_RELEASE);
}

void
hammer2_syncthr_freeze(hammer2_syncthr_t *thr)
{
	if (thr->td == NULL)
		return;
	lockmgr(&thr->lk, LK_EXCLUSIVE);
	atomic_set_int(&thr->flags, HAMMER2_SYNCTHR_FREEZE);
	wakeup(&thr->flags);
	while ((thr->flags & HAMMER2_SYNCTHR_FROZEN) == 0) {
		lksleep(thr, &thr->lk, 0, "h2frz", hz);
	}
	lockmgr(&thr->lk, LK_RELEASE);
}

void
hammer2_syncthr_unfreeze(hammer2_syncthr_t *thr)
{
	if (thr->td == NULL)
		return;
	lockmgr(&thr->lk, LK_EXCLUSIVE);
	atomic_clear_int(&thr->flags, HAMMER2_SYNCTHR_FROZEN);
	wakeup(&thr->flags);
	lockmgr(&thr->lk, LK_RELEASE);
}

/*
 * Primary management thread.
 *
 * On the SPMP - handles bulkfree and dedup operations
 * On a PFS    - handles remastering and synchronization
 */
void
hammer2_syncthr_primary(void *arg)
{
	hammer2_syncthr_t *thr = arg;
	hammer2_cluster_t *cparent;
	hammer2_pfs_t *pmp;
	hammer2_trans_t trans;

	pmp = thr->pmp;

	lockmgr(&thr->lk, LK_EXCLUSIVE);
	while ((thr->flags & HAMMER2_SYNCTHR_STOP) == 0) {
		/*
		 * Handle freeze request
		 */
		if (thr->flags & HAMMER2_SYNCTHR_FREEZE) {
			atomic_set_int(&thr->flags, HAMMER2_SYNCTHR_FROZEN);
			atomic_clear_int(&thr->flags, HAMMER2_SYNCTHR_FREEZE);
		}

		/*
		 * Force idle if frozen until unfrozen or stopped.
		 */
		if (thr->flags & HAMMER2_SYNCTHR_FROZEN) {
			lksleep(&thr->flags, &thr->lk, 0, "h2idle", 0);
			continue;
		}

		/*
		 * Reset state on REMASTER request
		 */
		if (thr->flags & HAMMER2_SYNCTHR_REMASTER) {
			atomic_clear_int(&thr->flags, HAMMER2_SYNCTHR_REMASTER);
			/* reset state */
		}

		/*
		 * Synchronization scan.
		 */
		hammer2_trans_init(&trans, pmp, 0);
		cparent = hammer2_inode_lock(pmp->iroot, HAMMER2_RESOLVE_NEVER);
		hammer2_update_pfs_status(pmp, cparent);
		hammer2_sync_slaves(thr, pmp, &cparent);
		hammer2_inode_unlock(pmp->iroot, cparent);
		hammer2_trans_done(&trans);

		/*
		 * Wait for event, or 5-second poll.
		 */
		lksleep(&thr->flags, &thr->lk, 0, "h2idle", hz * 5);
	}
	thr->td = NULL;
	wakeup(thr);
	lockmgr(&thr->lk, LK_RELEASE);
	/* thr structure can go invalid after this point */
}

/*
 * Given a locked cluster created from pmp->iroot, update the PFS's
 * reporting status.
 */
static
void
hammer2_update_pfs_status(hammer2_pfs_t *pmp, hammer2_cluster_t *cparent)
{
	uint32_t flags;

	flags = cparent->flags & HAMMER2_CLUSTER_ZFLAGS;
	if (pmp->status_flags == flags)
		return;
	pmp->status_flags = flags;

	kprintf("pfs %p", pmp);
	if (flags & HAMMER2_CLUSTER_MSYNCED)
		kprintf(" masters-all-good");
	if (flags & HAMMER2_CLUSTER_SSYNCED)
		kprintf(" slaves-all-good");

	if (flags & HAMMER2_CLUSTER_WRHARD)
		kprintf(" quorum/rw");
	else if (flags & HAMMER2_CLUSTER_RDHARD)
		kprintf(" quorum/ro");

	if (flags & HAMMER2_CLUSTER_UNHARD)
		kprintf(" out-of-sync-masters");
	else if (flags & HAMMER2_CLUSTER_NOHARD)
		kprintf(" no-masters-visible");

	if (flags & HAMMER2_CLUSTER_WRSOFT)
		kprintf(" soft/rw");
	else if (flags & HAMMER2_CLUSTER_RDSOFT)
		kprintf(" soft/ro");

	if (flags & HAMMER2_CLUSTER_UNSOFT)
		kprintf(" out-of-sync-slaves");
	else if (flags & HAMMER2_CLUSTER_NOSOFT)
		kprintf(" no-slaves-visible");
	kprintf("\n");
}

/*
 *
 */
static
void
hammer2_sync_slaves(hammer2_syncthr_t *thr, hammer2_pfs_t *pmp,
		    hammer2_cluster_t **cparentp)
{


}
