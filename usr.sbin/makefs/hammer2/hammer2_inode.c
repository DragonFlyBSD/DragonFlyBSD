/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2022 Tomohiro Kusumi <tkusumi@netbsd.org>
 * Copyright (c) 2011-2023 The DragonFly Project.  All rights reserved.
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
#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/types.h>
#include <sys/lock.h>
#include <sys/uuid.h>
#include <sys/vnode.h>
*/

#include "hammer2.h"

#define INODE_DEBUG	0

/*
 * Initialize inum hash in fresh structure
 */
void
hammer2_inum_hash_init(hammer2_pfs_t *pmp)
{
	hammer2_inum_hash_t *hash;
	int i;

	for (i = 0; i < HAMMER2_INUMHASH_SIZE; ++i) {
		hash = &pmp->inumhash[i];
		hammer2_spin_init(&hash->spin, "h2inum");
	}
}

/*
 * Caller holds pmp->list_spin and the inode should be locked.  Merge ip
 * with the specified depend.
 *
 * If the ip is on SYNCQ it stays there and (void *)-1 is returned, indicating
 * that successive calls must ensure the ip is on a pass2 depend (or they are
 * all SYNCQ).  If the passed-in depend is not NULL and not (void *)-1 then
 * we can set pass2 on it and return.
 *
 * If the ip is not on SYNCQ it is merged with the passed-in depend, creating
 * a self-depend if necessary, and depend->pass2 is set according
 * to the PASS2 flag.  SIDEQ is set.
 */
static __noinline
hammer2_depend_t *
hammer2_inode_setdepend_locked(hammer2_inode_t *ip, hammer2_depend_t *depend)
{
	hammer2_pfs_t *pmp = ip->pmp;
	hammer2_depend_t *dtmp;
	hammer2_inode_t *iptmp;

	/*
	 * If ip is SYNCQ its entry is used for the syncq list and it will
	 * no longer be associated with a dependency.  Merging this status
	 * with a passed-in depend implies PASS2.
	 */
	if (ip->flags & HAMMER2_INODE_SYNCQ) {
		if (depend == (void *)-1 ||
		    depend == NULL) {
			return ((void *)-1);
		}
		depend->pass2 = 1;
		hammer2_trans_setflags(pmp, HAMMER2_TRANS_RESCAN);

		return depend;
	}

	/*
	 * If ip is already SIDEQ, merge ip->depend into the passed-in depend.
	 * If it is not, associate the ip with the passed-in depend, creating
	 * a single-entry dependency using depend_static if necessary.
	 *
	 * NOTE: The use of ip->depend_static always requires that the
	 *	 specific ip containing the structure is part of that
	 *	 particular depend_static's dependency group.
	 */
	if (ip->flags & HAMMER2_INODE_SIDEQ) {
		/*
		 * Merge ip->depend with the passed-in depend.  If the
		 * passed-in depend is not a special case, all ips associated
		 * with ip->depend (including the original ip) must be moved
		 * to the passed-in depend.
		 */
		if (depend == NULL) {
			depend = ip->depend;
		} else if (depend == (void *)-1) {
			depend = ip->depend;
			depend->pass2 = 1;
		} else if (depend != ip->depend) {
#ifdef INVARIANTS
			int sanitychk = 0;
#endif
			dtmp = ip->depend;
			while ((iptmp = TAILQ_FIRST(&dtmp->sideq)) != NULL) {
#ifdef INVARIANTS
				if (iptmp == ip)
					sanitychk = 1;
#endif
				TAILQ_REMOVE(&dtmp->sideq, iptmp, entry);
				TAILQ_INSERT_TAIL(&depend->sideq, iptmp, entry);
				iptmp->depend = depend;
			}
			KKASSERT(sanitychk == 1);
			depend->count += dtmp->count;
			depend->pass2 |= dtmp->pass2;
			TAILQ_REMOVE(&pmp->depq, dtmp, entry);
			dtmp->count = 0;
			dtmp->pass2 = 0;
		}
	} else {
		/*
		 * Add ip to the sideq, creating a self-dependency if
		 * necessary.
		 */
		hammer2_inode_ref(ip); /* extra ref usually via hammer2_inode_modify() */
		atomic_set_int(&ip->flags, HAMMER2_INODE_SIDEQ);
		if (depend == NULL) {
			depend = &ip->depend_static;
			TAILQ_INSERT_TAIL(&pmp->depq, depend, entry);
		} else if (depend == (void *)-1) {
			depend = &ip->depend_static;
			depend->pass2 = 1;
			TAILQ_INSERT_TAIL(&pmp->depq, depend, entry);
		} /* else add ip to passed-in depend */
		TAILQ_INSERT_TAIL(&depend->sideq, ip, entry);
		ip->depend = depend;
		++depend->count;
		++pmp->sideq_count;
	}

	if (ip->flags & HAMMER2_INODE_SYNCQ_PASS2)
		depend->pass2 = 1;
	if (depend->pass2)
		hammer2_trans_setflags(pmp, HAMMER2_TRANS_RESCAN);

	return depend;
}

/*
 * Put a solo inode on the SIDEQ (meaning that its dirty).  This can also
 * occur from inode_lock4() and inode_depend().
 *
 * Caller must pass-in a locked inode.
 */
void
hammer2_inode_delayed_sideq(hammer2_inode_t *ip)
{
	hammer2_pfs_t *pmp = ip->pmp;

	/*
	 * Optimize case to avoid pmp spinlock.
	 */
	if ((ip->flags & (HAMMER2_INODE_SYNCQ | HAMMER2_INODE_SIDEQ)) == 0) {
		hammer2_spin_ex(&pmp->list_spin);
		hammer2_inode_setdepend_locked(ip, NULL);
		hammer2_spin_unex(&pmp->list_spin);
	}
}

/*
 * Lock an inode, with SYNCQ semantics.
 *
 * HAMMER2 offers shared and exclusive locks on inodes.  Pass a mask of
 * flags for options:
 *
 *	- pass HAMMER2_RESOLVE_SHARED if a shared lock is desired.
 *	  shared locks are not subject to SYNCQ semantics, exclusive locks
 *	  are.
 *
 *	- pass HAMMER2_RESOLVE_ALWAYS if you need the inode's meta-data.
 *	  Most front-end inode locks do.
 *
 *	- pass HAMMER2_RESOLVE_NEVER if you do not want to require that
 *	  the inode data be resolved.  This is used by the syncthr because
 *	  it can run on an unresolved/out-of-sync cluster, and also by the
 *	  vnode reclamation code to avoid unnecessary I/O (particularly when
 *	  disposing of hundreds of thousands of cached vnodes).
 *
 * This function, along with lock4, has SYNCQ semantics.  If the inode being
 * locked is on the SYNCQ, that is it has been staged by the syncer, we must
 * block until the operation is complete (even if we can lock the inode).  In
 * order to reduce the stall time, we re-order the inode to the front of the
 * pmp->syncq prior to blocking.  This reordering VERY significantly improves
 * performance.
 *
 * The inode locking function locks the inode itself, resolves any stale
 * chains in the inode's cluster, and allocates a fresh copy of the
 * cluster with 1 ref and all the underlying chains locked.
 *
 * ip->cluster will be stable while the inode is locked.
 *
 * NOTE: We don't combine the inode/chain lock because putting away an
 *       inode would otherwise confuse multiple lock holders of the inode.
 */
void
hammer2_inode_lock(hammer2_inode_t *ip, int how)
{
	hammer2_pfs_t *pmp;

	hammer2_inode_ref(ip);
	pmp = ip->pmp;

	/*
	 * Inode structure mutex - Shared lock
	 */
	if (how & HAMMER2_RESOLVE_SHARED) {
		hammer2_mtx_sh(&ip->lock);
		return;
	}

	/*
	 * Inode structure mutex - Exclusive lock
	 *
	 * An exclusive lock (if not recursive) must wait for inodes on
	 * SYNCQ to flush first, to ensure that meta-data dependencies such
	 * as the nlink count and related directory entries are not split
	 * across flushes.
	 *
	 * If the vnode is locked by the current thread it must be unlocked
	 * across the tsleep() to avoid a deadlock.
	 */
	hammer2_mtx_ex(&ip->lock);
	if (hammer2_mtx_refs(&ip->lock) > 1)
		return;
	while ((ip->flags & HAMMER2_INODE_SYNCQ) && pmp) {
		hammer2_spin_ex(&pmp->list_spin);
		if (ip->flags & HAMMER2_INODE_SYNCQ) {
			tsleep_interlock(&ip->flags, 0);
			atomic_set_int(&ip->flags, HAMMER2_INODE_SYNCQ_WAKEUP);
			TAILQ_REMOVE(&pmp->syncq, ip, entry);
			TAILQ_INSERT_HEAD(&pmp->syncq, ip, entry);
			hammer2_spin_unex(&pmp->list_spin);
			hammer2_mtx_unlock(&ip->lock);
			tsleep(&ip->flags, PINTERLOCKED, "h2sync", 0);
			hammer2_mtx_ex(&ip->lock);
			continue;
		}
		hammer2_spin_unex(&pmp->list_spin);
		break;
	}
}

/*
 * Exclusively lock up to four inodes, in order, with SYNCQ semantics.
 * ip1 and ip2 must not be NULL.  ip3 and ip4 may be NULL, but if ip3 is
 * NULL then ip4 must also be NULL.
 *
 * This creates a dependency between up to four inodes.
 */
void
hammer2_inode_lock4(hammer2_inode_t *ip1, hammer2_inode_t *ip2,
		    hammer2_inode_t *ip3, hammer2_inode_t *ip4)
{
	hammer2_inode_t *ips[4];
	hammer2_inode_t *iptmp;
	hammer2_inode_t *ipslp;
	hammer2_depend_t *depend;
	hammer2_pfs_t *pmp;
	size_t count;
	size_t i;

	pmp = ip1->pmp;			/* may be NULL */
	KKASSERT(pmp == ip2->pmp);

	ips[0] = ip1;
	ips[1] = ip2;
	if (ip3 == NULL) {
		count = 2;
	} else if (ip4 == NULL) {
		count = 3;
		ips[2] = ip3;
		KKASSERT(pmp == ip3->pmp);
	} else {
		count = 4;
		ips[2] = ip3;
		ips[3] = ip4;
		KKASSERT(pmp == ip3->pmp);
		KKASSERT(pmp == ip4->pmp);
	}

	for (i = 0; i < count; ++i)
		hammer2_inode_ref(ips[i]);

restart:
	/*
	 * Lock the inodes in order
	 */
	for (i = 0; i < count; ++i) {
		hammer2_mtx_ex(&ips[i]->lock);
	}

	/*
	 * Associate dependencies, record the first inode found on SYNCQ
	 * (operation is allowed to proceed for inodes on PASS2) for our
	 * sleep operation, this inode is theoretically the last one sync'd
	 * in the sequence.
	 *
	 * All inodes found on SYNCQ are moved to the head of the syncq
	 * to reduce stalls.
	 */
	hammer2_spin_ex(&pmp->list_spin);
	depend = NULL;
	ipslp = NULL;
	for (i = 0; i < count; ++i) {
		iptmp = ips[i];
		depend = hammer2_inode_setdepend_locked(iptmp, depend);
		if (iptmp->flags & HAMMER2_INODE_SYNCQ) {
			TAILQ_REMOVE(&pmp->syncq, iptmp, entry);
			TAILQ_INSERT_HEAD(&pmp->syncq, iptmp, entry);
			if (ipslp == NULL)
				ipslp = iptmp;
		}
	}
	hammer2_spin_unex(&pmp->list_spin);

	/*
	 * Block and retry if any of the inodes are on SYNCQ.  It is
	 * important that we allow the operation to proceed in the
	 * PASS2 case, to avoid deadlocking against the vnode.
	 */
	if (ipslp) {
		for (i = 0; i < count; ++i)
			hammer2_mtx_unlock(&ips[i]->lock);
		tsleep(&ipslp->flags, 0, "h2sync", 2);
		goto restart;
	}
}

/*
 * Release an inode lock.  If another thread is blocked on SYNCQ_WAKEUP
 * we wake them up.
 */
void
hammer2_inode_unlock(hammer2_inode_t *ip)
{
	if (ip->flags & HAMMER2_INODE_SYNCQ_WAKEUP) {
		atomic_clear_int(&ip->flags, HAMMER2_INODE_SYNCQ_WAKEUP);
		hammer2_mtx_unlock(&ip->lock);
		wakeup(&ip->flags);
	} else {
		hammer2_mtx_unlock(&ip->lock);
	}
	hammer2_inode_drop(ip);
}

/*
 * If either ip1 or ip2 have been tapped by the syncer, make sure that both
 * are.  This ensure that dependencies (e.g. dirent-v-inode) are synced
 * together.  For dirent-v-inode depends, pass the dirent as ip1.
 *
 * If neither ip1 or ip2 have been tapped by the syncer, merge them into a
 * single dependency.  Dependencies are entered into pmp->depq.  This
 * effectively flags the inodes SIDEQ.
 *
 * Both ip1 and ip2 must be locked by the caller.  This also ensures
 * that we can't race the end of the syncer's queue run.
 */
void
hammer2_inode_depend(hammer2_inode_t *ip1, hammer2_inode_t *ip2)
{
	hammer2_pfs_t *pmp;
	hammer2_depend_t *depend;

	pmp = ip1->pmp;
	hammer2_spin_ex(&pmp->list_spin);
	depend = hammer2_inode_setdepend_locked(ip1, NULL);
	depend = hammer2_inode_setdepend_locked(ip2, depend);
	hammer2_spin_unex(&pmp->list_spin);
}

/*
 * Select a chain out of an inode's cluster and lock it.
 *
 * The inode does not have to be locked.
 */
hammer2_chain_t *
hammer2_inode_chain(hammer2_inode_t *ip, int clindex, int how)
{
	hammer2_chain_t *chain;
	hammer2_cluster_t *cluster;

	hammer2_spin_sh(&ip->cluster_spin);
	cluster = &ip->cluster;
	if (clindex >= cluster->nchains)
		chain = NULL;
	else
		chain = cluster->array[clindex].chain;
	if (chain) {
		hammer2_chain_ref(chain);
		hammer2_spin_unsh(&ip->cluster_spin);
		hammer2_chain_lock(chain, how);
	} else {
		hammer2_spin_unsh(&ip->cluster_spin);
	}
	return chain;
}

hammer2_chain_t *
hammer2_inode_chain_and_parent(hammer2_inode_t *ip, int clindex,
			       hammer2_chain_t **parentp, int how)
{
	hammer2_chain_t *chain;
	hammer2_chain_t *parent;

	for (;;) {
		hammer2_spin_sh(&ip->cluster_spin);
		if (clindex >= ip->cluster.nchains)
			chain = NULL;
		else
			chain = ip->cluster.array[clindex].chain;
		if (chain) {
			hammer2_chain_ref(chain);
			hammer2_spin_unsh(&ip->cluster_spin);
			hammer2_chain_lock(chain, how);
		} else {
			hammer2_spin_unsh(&ip->cluster_spin);
		}

		/*
		 * Get parent, lock order must be (parent, chain).
		 */
		parent = chain->parent;
		if (parent) {
			hammer2_chain_ref(parent);
			hammer2_chain_unlock(chain);
			hammer2_chain_lock(parent, how);
			hammer2_chain_lock(chain, how);
		}
		if (ip->cluster.array[clindex].chain == chain &&
		    chain->parent == parent) {
			break;
		}

		/*
		 * Retry
		 */
		hammer2_chain_unlock(chain);
		hammer2_chain_drop(chain);
		if (parent) {
			hammer2_chain_unlock(parent);
			hammer2_chain_drop(parent);
		}
	}
	*parentp = parent;

	return chain;
}

/*
 * Temporarily release a lock held shared or exclusive.  Caller must
 * hold the lock shared or exclusive on call and lock will be released
 * on return.
 *
 * Restore a lock that was temporarily released.
 */
hammer2_mtx_state_t
hammer2_inode_lock_temp_release(hammer2_inode_t *ip)
{
	return hammer2_mtx_temp_release(&ip->lock);
}

void
hammer2_inode_lock_temp_restore(hammer2_inode_t *ip, hammer2_mtx_state_t ostate)
{
	hammer2_mtx_temp_restore(&ip->lock, ostate);
}

/*
 * Upgrade a shared inode lock to exclusive and return.  If the inode lock
 * is already held exclusively this is a NOP.
 *
 * The caller MUST hold the inode lock either shared or exclusive on call
 * and will own the lock exclusively on return.
 *
 * Returns non-zero if the lock was already exclusive prior to the upgrade.
 */
int
hammer2_inode_lock_upgrade(hammer2_inode_t *ip)
{
	int wasexclusive;

	/* XXX pretends it wasn't exclusive, but shouldn't matter */
	//if (mtx_islocked_ex(&ip->lock)) {
	if (0) {
		wasexclusive = 1;
	} else {
		hammer2_mtx_unlock(&ip->lock);
		hammer2_mtx_ex(&ip->lock);
		wasexclusive = 0;
	}
	return wasexclusive;
}

/*
 * Downgrade an inode lock from exclusive to shared only if the inode
 * lock was previously shared.  If the inode lock was previously exclusive,
 * this is a NOP.
 */
void
hammer2_inode_lock_downgrade(hammer2_inode_t *ip, int wasexclusive)
{
	if (wasexclusive == 0)
		hammer2_mtx_downgrade(&ip->lock);
}

static __inline hammer2_inum_hash_t *
inumhash(hammer2_pfs_t *pmp, hammer2_tid_t inum)
{
	int hv;

	hv = (int)inum;
	return (&pmp->inumhash[hv & HAMMER2_INUMHASH_MASK]);
}


/*
 * Lookup an inode by inode number
 */
hammer2_inode_t *
hammer2_inode_lookup(hammer2_pfs_t *pmp, hammer2_tid_t inum)
{
	hammer2_inum_hash_t *hash;
	hammer2_inode_t *ip;

	KKASSERT(pmp);
	if (pmp->spmp_hmp) {
		ip = NULL;
	} else {
		hash = inumhash(pmp, inum);
		hammer2_spin_sh(&hash->spin);
		for (ip = hash->base; ip; ip = ip->next) {
			if (ip->meta.inum == inum) {
				hammer2_inode_ref(ip);
				break;
			}
		}
		hammer2_spin_unsh(&hash->spin);
	}
	return(ip);
}

/*
 * Adding a ref to an inode is only legal if the inode already has at least
 * one ref.
 *
 * (can be called with spinlock held)
 */
void
hammer2_inode_ref(hammer2_inode_t *ip)
{
	atomic_add_int(&ip->refs, 1);
	if (hammer2_debug & 0x80000) {
		kprintf("INODE+1 %p (%d->%d)\n", ip, ip->refs - 1, ip->refs);
		print_backtrace(8);
	}
}

/*
 * Drop an inode reference, freeing the inode when the last reference goes
 * away.
 */
void
hammer2_inode_drop(hammer2_inode_t *ip)
{
	hammer2_pfs_t *pmp;
	u_int refs;

	while (ip) {
		if (hammer2_debug & 0x80000) {
			kprintf("INODE-1 %p (%d->%d)\n",
				ip, ip->refs, ip->refs - 1);
			print_backtrace(8);
		}
		refs = ip->refs;
		cpu_ccfence();
		if (refs == 1) {
			/*
			 * Transition to zero, must interlock with
			 * the inode inumber lookup tree (if applicable).
			 * It should not be possible for anyone to race
			 * the transition to 0.
			 */
			hammer2_inum_hash_t *hash;
			hammer2_inode_t **xipp;

			pmp = ip->pmp;
			KKASSERT(pmp);
			hash = inumhash(pmp, ip->meta.inum);

			hammer2_spin_ex(&hash->spin);
			if (atomic_cmpset_int(&ip->refs, 1, 0)) {
				KKASSERT(hammer2_mtx_refs(&ip->lock) == 0);
				if (ip->flags & HAMMER2_INODE_ONHASH) {
					xipp = &hash->base;
					while (*xipp != ip)
						xipp = &(*xipp)->next;
					*xipp = ip->next;
					ip->next = NULL;
					atomic_add_long(&pmp->inum_count, -1);
					atomic_clear_int(&ip->flags,
						     HAMMER2_INODE_ONHASH);
				}
				hammer2_spin_unex(&hash->spin);

				ip->pmp = NULL;

				/*
				 * Cleaning out ip->cluster isn't entirely
				 * trivial.
				 */
				hammer2_inode_repoint(ip, NULL);
				/*
				 * Add inode to reclaim queue.
				 */
				TAILQ_INSERT_TAIL(&pmp->recq, ip, recq_entry);
				ip = NULL;	/* will terminate loop */
			} else {
				hammer2_spin_unex(&hash->spin);
			}
		} else {
			/*
			 * Non zero transition
			 */
			if (atomic_cmpset_int(&ip->refs, refs, refs - 1))
				break;
		}
	}
}

/*
 * Get the vnode associated with the given inode, allocating the vnode if
 * necessary.  The vnode will be returned exclusively locked.
 *
 * *errorp is set to a UNIX error, not a HAMMER2 error.
 *
 * The caller must lock the inode (shared or exclusive).
 *
 * Great care must be taken to avoid deadlocks and vnode acquisition/reclaim
 * races.
 */
struct m_vnode *
hammer2_igetv(hammer2_inode_t *ip, int *errorp)
{
	hammer2_pfs_t *pmp;
	struct m_vnode *vp;

	pmp = ip->pmp;
	KKASSERT(pmp != NULL);
	*errorp = 0;

	for (;;) {
		/*
		 * Attempt to reuse an existing vnode assignment.  It is
		 * possible to race a reclaim so the vget() may fail.  The
		 * inode must be unlocked during the vget() to avoid a
		 * deadlock against a reclaim.
		 */
		int wasexclusive;

		vp = ip->vp;
		if (vp) {
			/*
			 * Inode must be unlocked during the vget() to avoid
			 * possible deadlocks, but leave the ip ref intact.
			 *
			 * vnode is held to prevent destruction during the
			 * vget().  The vget() can still fail if we lost
			 * a reclaim race on the vnode.
			 */
			hammer2_mtx_state_t ostate;

			vhold(vp);
			ostate = hammer2_inode_lock_temp_release(ip);
			if (vget(vp, LK_EXCLUSIVE)) {
				vdrop(vp);
				hammer2_inode_lock_temp_restore(ip, ostate);
				continue;
			}
			hammer2_inode_lock_temp_restore(ip, ostate);
			vdrop(vp);
			/* vp still locked and ref from vget */
			if (ip->vp != vp) {
				kprintf("hammer2: igetv race %p/%p\n",
					ip->vp, vp);
				vput(vp);
				continue;
			}
			*errorp = 0;
			break;
		}

		/*
		 * No vnode exists, allocate a new vnode.  Beware of
		 * allocation races.  This function will return an
		 * exclusively locked and referenced vnode.
		 */
		*errorp = getnewvnode(VT_HAMMER2, pmp->mp, &vp, 0, 0);
		if (*errorp) {
			kprintf("hammer2: igetv getnewvnode failed %d\n",
				*errorp);
			vp = NULL;
			break;
		}

		/*
		 * Lock the inode and check for an allocation race.
		 */
		wasexclusive = hammer2_inode_lock_upgrade(ip);
		if (ip->vp != NULL) {
			vp->v_type = VBAD;
			vx_put(vp);
			hammer2_inode_lock_downgrade(ip, wasexclusive);
			continue;
		}

		switch (ip->meta.type) {
		case HAMMER2_OBJTYPE_DIRECTORY:
			vp->v_type = VDIR;
			break;
		case HAMMER2_OBJTYPE_REGFILE:
			/*
			 * Regular file must use buffer cache I/O
			 * (VKVABIO cpu sync semantics supported)
			 */
			vp->v_type = VREG;
			vsetflags(vp, VKVABIO);
			vinitvmio(vp, ip->meta.size,
				  HAMMER2_LBUFSIZE,
				  (int)ip->meta.size & HAMMER2_LBUFMASK);
			break;
		case HAMMER2_OBJTYPE_SOFTLINK:
			/*
			 * XXX for now we are using the generic file_read
			 * and file_write code so we need a buffer cache
			 * association.
			 *
			 * (VKVABIO cpu sync semantics supported)
			 */
			vp->v_type = VLNK;
			vsetflags(vp, VKVABIO);
			vinitvmio(vp, ip->meta.size,
				  HAMMER2_LBUFSIZE,
				  (int)ip->meta.size & HAMMER2_LBUFMASK);
			break;
		case HAMMER2_OBJTYPE_CDEV:
			vp->v_type = VCHR;
			/* fall through */
		case HAMMER2_OBJTYPE_BDEV:
			//vp->v_ops = &pmp->mp->mnt_vn_spec_ops;
			if (ip->meta.type != HAMMER2_OBJTYPE_CDEV)
				vp->v_type = VBLK;
			addaliasu(vp,
				  ip->meta.rmajor,
				  ip->meta.rminor);
			break;
		case HAMMER2_OBJTYPE_FIFO:
			vp->v_type = VFIFO;
			//vp->v_ops = &pmp->mp->mnt_vn_fifo_ops;
			break;
		case HAMMER2_OBJTYPE_SOCKET:
			vp->v_type = VSOCK;
			break;
		default:
			panic("hammer2: unhandled objtype %d",
			      ip->meta.type);
			break;
		}

		if (ip == pmp->iroot)
			vsetflags(vp, VROOT);

		vp->v_data = ip;
		ip->vp = vp;
		hammer2_inode_ref(ip);		/* vp association */
		hammer2_inode_lock_downgrade(ip, wasexclusive);
		vx_downgrade(vp);
		break;
	}

	/*
	 * Return non-NULL vp and *errorp == 0, or NULL vp and *errorp != 0.
	 */
	if (hammer2_debug & 0x0002) {
		kprintf("igetv vp %p refs 0x%08x aux 0x%08x\n",
			vp, -1, -1);
	}
	return (vp);
}

/*
 * XXX this API needs a rewrite.  It needs to be split into a
 * hammer2_inode_alloc() and hammer2_inode_build() to allow us to get
 * rid of the inode/chain lock reversal fudge.
 *
 * Returns the inode associated with the passed-in cluster, allocating a new
 * hammer2_inode structure if necessary, then synchronizing it to the passed
 * xop cluster.  When synchronizing, if idx >= 0, only cluster index (idx)
 * is synchronized.  Otherwise the whole cluster is synchronized.  inum will
 * be extracted from the passed-in xop and the inum argument will be ignored.
 *
 * If xop is passed as NULL then a new hammer2_inode is allocated with the
 * specified inum, and returned.   For normal inodes, the inode will be
 * indexed in memory and if it already exists the existing ip will be
 * returned instead of allocating a new one.  The superroot and PFS inodes
 * are not indexed in memory.
 *
 * The passed-in cluster must be locked and will remain locked on return.
 * The returned inode will be locked and the caller may dispose of both
 * via hammer2_inode_unlock() + hammer2_inode_drop().  However, if the caller
 * needs to resolve a hardlink it must ref/unlock/relock/drop the inode.
 *
 * The hammer2_inode structure regulates the interface between the high level
 * kernel VNOPS API and the filesystem backend (the chains).
 *
 * On return the inode is locked with the supplied cluster.
 */
hammer2_inode_t *
hammer2_inode_get(hammer2_pfs_t *pmp, hammer2_xop_head_t *xop,
		  hammer2_tid_t inum, int idx)
{
	hammer2_inode_t *nip;
	const hammer2_inode_data_t *iptmp;
	const hammer2_inode_data_t *nipdata;

	KKASSERT(xop == NULL ||
		 hammer2_cluster_type(&xop->cluster) ==
		 HAMMER2_BREF_TYPE_INODE);
	KKASSERT(pmp);

	/*
	 * Interlocked lookup/ref of the inode.  This code is only needed
	 * when looking up inodes with nlinks != 0 (TODO: optimize out
	 * otherwise and test for duplicates).
	 *
	 * Cluster can be NULL during the initial pfs allocation.
	 */
	if (xop) {
		iptmp = &hammer2_xop_gdata(xop)->ipdata;
		inum = iptmp->meta.inum;
		hammer2_xop_pdata(xop);
	}
again:
	nip = hammer2_inode_lookup(pmp, inum);
	if (nip) {
		/*
		 * We may have to unhold the cluster to avoid a deadlock
		 * against vnlru (and possibly other XOPs).
		 */
		if (xop) {
			if (hammer2_mtx_ex_try(&nip->lock) != 0) {
				hammer2_cluster_unhold(&xop->cluster);
				hammer2_mtx_ex(&nip->lock);
				hammer2_cluster_rehold(&xop->cluster);
			}
		} else {
			hammer2_mtx_ex(&nip->lock);
		}

		/*
		 * Handle SMP race (not applicable to the super-root spmp
		 * which can't index inodes due to duplicative inode numbers).
		 */
		if (pmp->spmp_hmp == NULL &&
		    (nip->flags & HAMMER2_INODE_ONHASH) == 0) {
			hammer2_mtx_unlock(&nip->lock);
			hammer2_inode_drop(nip);
			goto again;
		}
		if (xop) {
			if (idx >= 0)
				hammer2_inode_repoint_one(nip, &xop->cluster,
							  idx);
			else
				hammer2_inode_repoint(nip, &xop->cluster);
		}
		return nip;
	}

	/*
	 * We couldn't find the inode number, create a new inode and try to
	 * insert it, handle insertion races.
	 */
	nip = kmalloc_obj(sizeof(*nip), pmp->minode, M_WAITOK | M_ZERO);
	hammer2_spin_init(&nip->cluster_spin, "h2clspin");
	atomic_add_long(&pmp->inmem_inodes, 1);

	/*
	 * Initialize nip's cluster.  A cluster is provided for normal
	 * inodes but typically not for the super-root or PFS inodes.
	 */
	{
		hammer2_inode_t *nnip = nip;
		nip->ihash = (int)hammer2_icrc32(&nnip, sizeof(nnip));
	}

	nip->cluster.refs = 1;
	nip->cluster.pmp = pmp;
	nip->cluster.flags |= HAMMER2_CLUSTER_INODE;
	if (xop) {
		nipdata = &hammer2_xop_gdata(xop)->ipdata;
		nip->meta = nipdata->meta;
		hammer2_xop_pdata(xop);
		hammer2_inode_repoint(nip, &xop->cluster);
	} else {
		nip->meta.inum = inum;		/* PFS inum is always 1 XXX */
		/* mtime will be updated when a cluster is available */
	}

	nip->pmp = pmp;

	/*
	 * ref and lock on nip gives it state compatible to after a
	 * hammer2_inode_lock() call.
	 */
	nip->refs = 1;
	hammer2_mtx_init(&nip->lock, "h2inode");
	hammer2_mtx_init(&nip->truncate_lock, "h2trunc");
	hammer2_mtx_ex(&nip->lock);
	TAILQ_INIT(&nip->depend_static.sideq);
	/* combination of thread lock and chain lock == inode lock */

	/*
	 * Attempt to add the inode.  If it fails we raced another inode
	 * get.  Undo all the work and try again.
	 */
	if (pmp->spmp_hmp == NULL) {
		hammer2_inum_hash_t *hash;
		hammer2_inode_t *xip;
		hammer2_inode_t **xipp;

		hash = inumhash(pmp, nip->meta.inum);
		hammer2_spin_ex(&hash->spin);
		for (xipp = &hash->base;
		     (xip = *xipp) != NULL;
		     xipp = &xip->next)
		{
			if (xip->meta.inum == nip->meta.inum) {
				hammer2_spin_unex(&hash->spin);
				hammer2_mtx_unlock(&nip->lock);
				hammer2_inode_drop(nip);
				goto again;
			}
		}
		nip->next = NULL;
		*xipp = nip;
		atomic_set_int(&nip->flags, HAMMER2_INODE_ONHASH);
		atomic_add_long(&pmp->inum_count, 1);
		hammer2_spin_unex(&hash->spin);
	}
	return (nip);
}

/*
 * Create a PFS inode under the superroot.  This function will create the
 * inode, its media chains, and also insert it into the media.
 *
 * Caller must be in a flush transaction because we are inserting the inode
 * onto the media.
 */
hammer2_inode_t *
hammer2_inode_create_pfs(hammer2_pfs_t *spmp,
		     const char *name, size_t name_len,
		     int *errorp)
{
	hammer2_xop_create_t *xop;
	hammer2_inode_t *pip;
	hammer2_inode_t *nip;
	int error;
	uint8_t pip_comp_algo;
	uint8_t pip_check_algo;
	hammer2_tid_t pip_inum;
	hammer2_key_t lhc;

	pip = spmp->iroot;
	nip = NULL;

	lhc = hammer2_dirhash(name, name_len);
	*errorp = 0;

	/*
	 * Locate the inode or indirect block to create the new
	 * entry in.  At the same time check for key collisions
	 * and iterate until we don't get one.
	 *
	 * Lock the directory exclusively for now to guarantee that
	 * we can find an unused lhc for the name.  Due to collisions,
	 * two different creates can end up with the same lhc so we
	 * cannot depend on the OS to prevent the collision.
	 */
	hammer2_inode_lock(pip, 0);

	pip_comp_algo = pip->meta.comp_algo;
	pip_check_algo = pip->meta.check_algo;
	pip_inum = (pip == pip->pmp->iroot) ? 1 : pip->meta.inum;

	/*
	 * Locate an unused key in the collision space.
	 */
	{
		hammer2_xop_scanlhc_t *sxop;
		hammer2_key_t lhcbase;

		lhcbase = lhc;
		sxop = hammer2_xop_alloc(pip, HAMMER2_XOP_MODIFYING);
		sxop->lhc = lhc;
		hammer2_xop_start(&sxop->head, &hammer2_scanlhc_desc);
		while ((error = hammer2_xop_collect(&sxop->head, 0)) == 0) {
			if (lhc != sxop->head.cluster.focus->bref.key)
				break;
			++lhc;
		}
		hammer2_xop_retire(&sxop->head, HAMMER2_XOPMASK_VOP);

		if (error) {
			if (error != HAMMER2_ERROR_ENOENT)
				goto done2;
			++lhc;
			error = 0;
		}
		if ((lhcbase ^ lhc) & ~HAMMER2_DIRHASH_LOMASK) {
			error = HAMMER2_ERROR_ENOSPC;
			goto done2;
		}
	}

	/*
	 * Create the inode with the lhc as the key.
	 */
	xop = hammer2_xop_alloc(pip, HAMMER2_XOP_MODIFYING);
	xop->lhc = lhc;
	xop->flags = HAMMER2_INSERT_PFSROOT;
	bzero(&xop->meta, sizeof(xop->meta));

	xop->meta.type = HAMMER2_OBJTYPE_DIRECTORY;
	xop->meta.inum = 1;
	xop->meta.iparent = pip_inum;

	/* Inherit parent's inode compression mode. */
	xop->meta.comp_algo = pip_comp_algo;
	xop->meta.check_algo = pip_check_algo;
	xop->meta.version = HAMMER2_INODE_VERSION_ONE;
	hammer2_update_time(&xop->meta.ctime, false);
	xop->meta.mtime = xop->meta.ctime;
	xop->meta.mode = 0755;
	xop->meta.nlinks = 1;

	hammer2_xop_setname(&xop->head, name, name_len);
	xop->meta.name_len = name_len;
	xop->meta.name_key = lhc;
	KKASSERT(name_len < HAMMER2_INODE_MAXNAME);

	hammer2_xop_start(&xop->head, &hammer2_inode_create_desc);

	error = hammer2_xop_collect(&xop->head, 0);
#if INODE_DEBUG
	kprintf("CREATE INODE %*.*s\n",
		(int)name_len, (int)name_len, name);
#endif

	if (error) {
		*errorp = error;
		goto done;
	}

	/*
	 * Set up the new inode if not a hardlink pointer.
	 *
	 * NOTE: *_get() integrates chain's lock into the inode lock.
	 *
	 * NOTE: Only one new inode can currently be created per
	 *	 transaction.  If the need arises we can adjust
	 *	 hammer2_trans_init() to allow more.
	 *
	 * NOTE: nipdata will have chain's blockset data.
	 */
	nip = hammer2_inode_get(pip->pmp, &xop->head, -1, -1);
	nip->comp_heuristic = 0;
done:
	hammer2_xop_retire(&xop->head, HAMMER2_XOPMASK_VOP);
done2:
	hammer2_inode_unlock(pip);

	return (nip);
}

/*
 * Create a new, normal inode.  This function will create the inode,
 * the media chains, but will not insert the chains onto the media topology
 * (doing so would require a flush transaction and cause long stalls).
 *
 * Caller must be in a normal transaction.
 */
hammer2_inode_t *
hammer2_inode_create_normal(hammer2_inode_t *pip,
			    struct vattr *vap, struct ucred *cred,
			    hammer2_key_t inum, int *errorp)
{
	hammer2_xop_create_t *xop;
	hammer2_inode_t *dip;
	hammer2_inode_t *nip;
	int error;
	uid_t xuid;
	uuid_t pip_uid;
	uuid_t pip_gid;
	uint32_t pip_mode;
	uint8_t pip_comp_algo;
	uint8_t pip_check_algo;
	hammer2_tid_t pip_inum;

	dip = pip->pmp->iroot;
	KKASSERT(dip != NULL);

	*errorp = 0;

	/*hammer2_inode_lock(dip, 0);*/

	pip_uid = pip->meta.uid;
	pip_gid = pip->meta.gid;
	pip_mode = pip->meta.mode;
	pip_comp_algo = pip->meta.comp_algo;
	pip_check_algo = pip->meta.check_algo;
	pip_inum = (pip == pip->pmp->iroot) ? 1 : pip->meta.inum;

	/*
	 * Create the in-memory hammer2_inode structure for the specified
	 * inode.
	 */
	nip = hammer2_inode_get(dip->pmp, NULL, inum, -1);
	nip->comp_heuristic = 0;
	KKASSERT((nip->flags & HAMMER2_INODE_CREATING) == 0 &&
		 nip->cluster.nchains == 0);
	atomic_set_int(&nip->flags, HAMMER2_INODE_CREATING);

	/*
	 * Setup the inode meta-data
	 */
	nip->meta.type = hammer2_get_obj_type(vap->va_type);

	switch (nip->meta.type) {
	case HAMMER2_OBJTYPE_CDEV:
	case HAMMER2_OBJTYPE_BDEV:
		assert(0); /* XXX unsupported */
		nip->meta.rmajor = vap->va_rmajor;
		nip->meta.rminor = vap->va_rminor;
		break;
	default:
		break;
	}

	KKASSERT(nip->meta.inum == inum);
	nip->meta.iparent = pip_inum;

	/* Inherit parent's inode compression mode. */
	nip->meta.comp_algo = pip_comp_algo;
	nip->meta.check_algo = pip_check_algo;
	nip->meta.version = HAMMER2_INODE_VERSION_ONE;
	hammer2_update_time(&nip->meta.ctime, false);
	nip->meta.mtime = nip->meta.ctime;
	nip->meta.mode = vap->va_mode;
	nip->meta.nlinks = 1;

	xuid = hammer2_to_unix_xid(&pip_uid);
	xuid = vop_helper_create_uid(dip->pmp->mp, pip_mode,
				     xuid, cred,
				     &vap->va_mode);
	if (vap->va_vaflags & VA_UID_UUID_VALID)
		nip->meta.uid = vap->va_uid_uuid;
	else if (vap->va_uid != (uid_t)VNOVAL)
		hammer2_guid_to_uuid(&nip->meta.uid, vap->va_uid);
	else
		hammer2_guid_to_uuid(&nip->meta.uid, xuid);

	if (vap->va_vaflags & VA_GID_UUID_VALID)
		nip->meta.gid = vap->va_gid_uuid;
	else if (vap->va_gid != (gid_t)VNOVAL)
		hammer2_guid_to_uuid(&nip->meta.gid, vap->va_gid);
	else
		nip->meta.gid = pip_gid;

	/*
	 * Regular files and softlinks allow a small amount of data to be
	 * directly embedded in the inode.  This flag will be cleared if
	 * the size is extended past the embedded limit.
	 */
	if (nip->meta.type == HAMMER2_OBJTYPE_REGFILE ||
	    nip->meta.type == HAMMER2_OBJTYPE_SOFTLINK) {
		nip->meta.op_flags |= HAMMER2_OPFLAG_DIRECTDATA;
	}

	/*
	 * Create the inode using (inum) as the key.  Pass pip for
	 * method inheritance.
	 */
	xop = hammer2_xop_alloc(pip, HAMMER2_XOP_MODIFYING);
	xop->lhc = inum;
	xop->flags = 0;
	xop->meta = nip->meta;

	xop->meta.name_len = hammer2_xop_setname_inum(&xop->head, inum);
	xop->meta.name_key = inum;
	nip->meta.name_len = xop->meta.name_len;
	nip->meta.name_key = xop->meta.name_key;
	hammer2_inode_modify(nip);

	/*
	 * Create the inode media chains but leave them detached.  We are
	 * not in a flush transaction so we can't mess with media topology
	 * above normal inodes (i.e. the index of the inodes themselves).
	 *
	 * We've already set the INODE_CREATING flag.  The inode's media
	 * chains will be inserted onto the media topology on the next
	 * filesystem sync.
	 */
	hammer2_xop_start(&xop->head, &hammer2_inode_create_det_desc);

	error = hammer2_xop_collect(&xop->head, 0);
#if INODE_DEBUG
	kprintf("create inode type %d error %d\n", nip->meta.type, error);
#endif

	if (error) {
		*errorp = error;
		goto done;
	}

	/*
	 * Associate the media chains created by the backend with the
	 * frontend inode.
	 */
	hammer2_inode_repoint(nip, &xop->head.cluster);
done:
	hammer2_xop_retire(&xop->head, HAMMER2_XOPMASK_VOP);
	/*hammer2_inode_unlock(dip);*/

	return (nip);
}

/*
 * Create a directory entry under dip with the specified name, inode number,
 * and OBJTYPE (type).
 *
 * This returns a UNIX errno code, not a HAMMER2_ERROR_* code.
 *
 * Caller must hold dip locked.
 */
int
hammer2_dirent_create(hammer2_inode_t *dip, const char *name, size_t name_len,
		      hammer2_key_t inum, uint8_t type)
{
	hammer2_xop_mkdirent_t *xop;
	hammer2_key_t lhc;
	int error;

	lhc = 0;
	error = 0;

	KKASSERT(name != NULL);
	lhc = hammer2_dirhash(name, name_len);

	/*
	 * Locate the inode or indirect block to create the new
	 * entry in.  At the same time check for key collisions
	 * and iterate until we don't get one.
	 *
	 * Lock the directory exclusively for now to guarantee that
	 * we can find an unused lhc for the name.  Due to collisions,
	 * two different creates can end up with the same lhc so we
	 * cannot depend on the OS to prevent the collision.
	 */
	hammer2_inode_modify(dip);

	/*
	 * If name specified, locate an unused key in the collision space.
	 * Otherwise use the passed-in lhc directly.
	 */
	{
		hammer2_xop_scanlhc_t *sxop;
		hammer2_key_t lhcbase;

		lhcbase = lhc;
		sxop = hammer2_xop_alloc(dip, HAMMER2_XOP_MODIFYING);
		sxop->lhc = lhc;
		hammer2_xop_start(&sxop->head, &hammer2_scanlhc_desc);
		while ((error = hammer2_xop_collect(&sxop->head, 0)) == 0) {
			if (lhc != sxop->head.cluster.focus->bref.key)
				break;
			++lhc;
		}
		hammer2_xop_retire(&sxop->head, HAMMER2_XOPMASK_VOP);

		if (error) {
			if (error != HAMMER2_ERROR_ENOENT)
				goto done2;
			++lhc;
			error = 0;
		}
		if ((lhcbase ^ lhc) & ~HAMMER2_DIRHASH_LOMASK) {
			error = HAMMER2_ERROR_ENOSPC;
			goto done2;
		}
	}

	/*
	 * Create the directory entry with the lhc as the key.
	 */
	xop = hammer2_xop_alloc(dip, HAMMER2_XOP_MODIFYING);
	xop->lhc = lhc;
	bzero(&xop->dirent, sizeof(xop->dirent));
	xop->dirent.inum = inum;
	xop->dirent.type = type;
	xop->dirent.namlen = name_len;

	KKASSERT(name_len < HAMMER2_INODE_MAXNAME);
	hammer2_xop_setname(&xop->head, name, name_len);

	hammer2_xop_start(&xop->head, &hammer2_inode_mkdirent_desc);

	error = hammer2_xop_collect(&xop->head, 0);

	hammer2_xop_retire(&xop->head, HAMMER2_XOPMASK_VOP);
done2:
	error = hammer2_error_to_errno(error);

	return error;
}

/*
 * Repoint ip->cluster's chains to cluster's chains and fixup the default
 * focus.  All items, valid or invalid, are repointed.  hammer2_xop_start()
 * filters out invalid or non-matching elements.
 *
 * Caller must hold the inode and cluster exclusive locked, if not NULL,
 * must also be locked.
 *
 * Cluster may be NULL to clean out any chains in ip->cluster.
 */
void
hammer2_inode_repoint(hammer2_inode_t *ip, hammer2_cluster_t *cluster)
{
	hammer2_chain_t *dropch[HAMMER2_MAXCLUSTER];
	hammer2_chain_t *ochain;
	hammer2_chain_t *nchain;
	int i;

	bzero(dropch, sizeof(dropch));

	/*
	 * Drop any cached (typically data) chains related to this inode
	 */
	hammer2_spin_ex(&ip->cluster_spin);
	for (i = 0; i < ip->ccache_nchains; ++i) {
		dropch[i] = ip->ccache[i].chain;
		ip->ccache[i].flags = 0;
		ip->ccache[i].chain = NULL;
	}
	ip->ccache_nchains = 0;
	hammer2_spin_unex(&ip->cluster_spin);

	while (--i >= 0) {
		if (dropch[i]) {
			hammer2_chain_drop(dropch[i]);
			dropch[i] = NULL;
		}
	}

	/*
	 * Replace chains in ip->cluster with chains from cluster and
	 * adjust the focus if necessary.
	 *
	 * NOTE: nchain and/or ochain can be NULL due to gaps
	 *	 in the cluster arrays.
	 */
	hammer2_spin_ex(&ip->cluster_spin);
	for (i = 0; cluster && i < cluster->nchains; ++i) {
		/*
		 * Do not replace elements which are the same.  Also handle
		 * element count discrepancies.
		 */
		nchain = cluster->array[i].chain;
		if (i < ip->cluster.nchains) {
			ochain = ip->cluster.array[i].chain;
			if (ochain == nchain)
				continue;
		} else {
			ochain = NULL;
		}

		/*
		 * Make adjustments
		 */
		ip->cluster.array[i].chain = nchain;
		ip->cluster.array[i].flags &= ~HAMMER2_CITEM_INVALID;
		ip->cluster.array[i].flags |= cluster->array[i].flags &
					      HAMMER2_CITEM_INVALID;
		if (nchain)
			hammer2_chain_ref(nchain);
		dropch[i] = ochain;
	}

	/*
	 * Release any left-over chains in ip->cluster.
	 */
	while (i < ip->cluster.nchains) {
		nchain = ip->cluster.array[i].chain;
		if (nchain) {
			ip->cluster.array[i].chain = NULL;
			ip->cluster.array[i].flags |= HAMMER2_CITEM_INVALID;
		}
		dropch[i] = nchain;
		++i;
	}

	/*
	 * Fixup fields.  Note that the inode-embedded cluster is never
	 * directly locked.
	 */
	if (cluster) {
		ip->cluster.nchains = cluster->nchains;
		ip->cluster.focus = cluster->focus;
		ip->cluster.flags = cluster->flags & ~HAMMER2_CLUSTER_LOCKED;
	} else {
		ip->cluster.nchains = 0;
		ip->cluster.focus = NULL;
		ip->cluster.flags &= ~HAMMER2_CLUSTER_ZFLAGS;
	}

	hammer2_spin_unex(&ip->cluster_spin);

	/*
	 * Cleanup outside of spinlock
	 */
	while (--i >= 0) {
		if (dropch[i])
			hammer2_chain_drop(dropch[i]);
	}
}

/*
 * Repoint a single element from the cluster to the ip.  Used by the
 * synchronization threads to piecemeal update inodes.  Does not change
 * focus and requires inode to be re-locked to clean-up flags (XXX).
 */
void
hammer2_inode_repoint_one(hammer2_inode_t *ip, hammer2_cluster_t *cluster,
			  int idx)
{
	hammer2_chain_t *dropch[HAMMER2_MAXCLUSTER];
	hammer2_chain_t *ochain;
	hammer2_chain_t *nchain;
	int i;

	/*
	 * Drop any cached (typically data) chains related to this inode
	 */
	hammer2_spin_ex(&ip->cluster_spin);
	for (i = 0; i < ip->ccache_nchains; ++i) {
		dropch[i] = ip->ccache[i].chain;
		ip->ccache[i].chain = NULL;
	}
	ip->ccache_nchains = 0;
	hammer2_spin_unex(&ip->cluster_spin);

	while (--i >= 0) {
		if (dropch[i])
			hammer2_chain_drop(dropch[i]);
	}

	/*
	 * Replace inode chain at index
	 */
	hammer2_spin_ex(&ip->cluster_spin);
	KKASSERT(idx < cluster->nchains);
	if (idx < ip->cluster.nchains) {
		ochain = ip->cluster.array[idx].chain;
		nchain = cluster->array[idx].chain;
	} else {
		ochain = NULL;
		nchain = cluster->array[idx].chain;
		for (i = ip->cluster.nchains; i <= idx; ++i) {
			bzero(&ip->cluster.array[i],
			      sizeof(ip->cluster.array[i]));
			ip->cluster.array[i].flags |= HAMMER2_CITEM_INVALID;
		}
		ip->cluster.nchains = idx + 1;
	}
	if (ochain != nchain) {
		/*
		 * Make adjustments.
		 */
		ip->cluster.array[idx].chain = nchain;
		ip->cluster.array[idx].flags &= ~HAMMER2_CITEM_INVALID;
		ip->cluster.array[idx].flags |= cluster->array[idx].flags &
						HAMMER2_CITEM_INVALID;
	}
	hammer2_spin_unex(&ip->cluster_spin);
	if (ochain != nchain) {
		if (nchain)
			hammer2_chain_ref(nchain);
		if (ochain)
			hammer2_chain_drop(ochain);
	}
}

hammer2_key_t
hammer2_inode_data_count(const hammer2_inode_t *ip)
{
	hammer2_chain_t *chain;
	hammer2_key_t count = 0;
	int i;

	for (i = 0; i < ip->cluster.nchains; ++i) {
		if ((chain = ip->cluster.array[i].chain) != NULL) {
			if (count < chain->bref.embed.stats.data_count)
				count = chain->bref.embed.stats.data_count;
		}
	}
	return count;
}

hammer2_key_t
hammer2_inode_inode_count(const hammer2_inode_t *ip)
{
	hammer2_chain_t *chain;
	hammer2_key_t count = 0;
	int i;

	for (i = 0; i < ip->cluster.nchains; ++i) {
		if ((chain = ip->cluster.array[i].chain) != NULL) {
			if (count < chain->bref.embed.stats.inode_count)
				count = chain->bref.embed.stats.inode_count;
		}
	}
	return count;
}

/*
 * Called with a locked inode to finish unlinking an inode after xop_unlink
 * had been run.  This function is responsible for decrementing nlinks.
 */
int
hammer2_inode_unlink_finisher(hammer2_inode_t *ip, struct m_vnode **vprecyclep)
{
	struct m_vnode *vp;
	uint64_t ctime;

	/*
	 * Decrement nlinks.  Catch a bad nlinks count here too (e.g. 0 or
	 * negative), and just assume a transition to 0.
	 */
	if ((int64_t)ip->meta.nlinks > 1) {
		hammer2_update_time(&ctime, false);
	} else {
		atomic_set_int(&ip->flags, HAMMER2_INODE_ISUNLINKED);

		/*
		 * Scrap the vnode as quickly as possible.  The vp association
		 * stays intact while we hold the inode locked.  However, vp
		 * can be NULL here.
		 */
		vp = ip->vp;
		cpu_ccfence();

		/*
		 * If no vp is associated there is no high-level state to
		 * deal with and we can scrap the inode immediately.
		 */
		if (vp == NULL) {
			if ((ip->flags & HAMMER2_INODE_DELETING) == 0) {
				atomic_set_int(&ip->flags,
					       HAMMER2_INODE_DELETING);
				hammer2_inode_delayed_sideq(ip);
			}
			return 0;
		}

		/*
		 * Because INODE_ISUNLINKED is set with the inode lock
		 * held, the vnode cannot be ripped up from under us.
		 * There may still be refs so knote anyone waiting for
		 * a delete notification.
		 *
		 * The vnode is not necessarily ref'd due to the unlinking
		 * itself, so we have to defer handling to the end of the
		 * VOP, which will then call hammer2_inode_vprecycle().
		 */
		if (vprecyclep) {
			vhold(vp);
			*vprecyclep = vp;
		}
	}

	/*
	 * Adjust nlinks and retain the inode on the media for now
	 */
	hammer2_inode_modify(ip);
	if ((int64_t)ip->meta.nlinks > 1) {
		--ip->meta.nlinks;
		ip->meta.ctime = ctime;
	} else {
		ip->meta.nlinks = 0;
	}

	return 0;
}

/*
 * Called at the end of a VOP that removes a file with a vnode that
 * we want to try to dispose of quickly due to a file deletion.  If
 * we don't do this, the vnode can hang around with 0 refs for a very
 * long time and prevent reclamation of the underlying file and inode
 * (inode remains on-media with nlinks == 0 until the vnode is recycled
 * due to random system activity or a umount).
 */
void
hammer2_inode_vprecycle(struct m_vnode *vp)
{
	if (vget(vp, LK_EXCLUSIVE) == 0) {
		vfinalize(vp);
		hammer2_knote(vp, NOTE_DELETE);
		vdrop(vp);
		vput(vp);
	} else {
		vdrop(vp);
	}
}


/*
 * Mark an inode as being modified, meaning that the caller will modify
 * ip->meta.
 *
 * If a vnode is present we set the vnode dirty and the nominal filesystem
 * sync will also handle synchronizing the inode meta-data.  Unless NOSIDEQ
 * we must ensure that the inode is on pmp->sideq.
 *
 * NOTE: We must always queue the inode to the sideq.  This allows H2 to
 *	 shortcut vsyncscan() and flush inodes and their related vnodes
 *	 in a two stages.  H2 still calls vfsync() for each vnode.
 *
 * NOTE: No mtid (modify_tid) is passed into this routine.  The caller is
 *	 only modifying the in-memory inode.  A modify_tid is synchronized
 *	 later when the inode gets flushed.
 *
 * NOTE: As an exception to the general rule, the inode MAY be locked
 *	 shared for this particular call.
 */
void
hammer2_inode_modify(hammer2_inode_t *ip)
{
	atomic_set_int(&ip->flags, HAMMER2_INODE_MODIFIED);
	if (ip->vp)
		vsetisdirty(ip->vp);
	if (ip->pmp && (ip->flags & HAMMER2_INODE_NOSIDEQ) == 0)
		hammer2_inode_delayed_sideq(ip);
}

/*
 * Synchronize the inode's frontend state with the chain state prior
 * to any explicit flush of the inode or any strategy write call.  This
 * does not flush the inode's chain or its sub-topology to media (higher
 * level layers are responsible for doing that).
 *
 * Called with a locked inode inside a normal transaction.
 *
 * inode must be locked.
 */
int
hammer2_inode_chain_sync(hammer2_inode_t *ip)
{
	int error;

	error = 0;
	if (ip->flags & (HAMMER2_INODE_RESIZED | HAMMER2_INODE_MODIFIED)) {
		hammer2_xop_fsync_t *xop;

		xop = hammer2_xop_alloc(ip, HAMMER2_XOP_MODIFYING);
		xop->clear_directdata = 0;
		if (ip->flags & HAMMER2_INODE_RESIZED) {
			if ((ip->meta.op_flags & HAMMER2_OPFLAG_DIRECTDATA) &&
			    ip->meta.size > HAMMER2_EMBEDDED_BYTES) {
				ip->meta.op_flags &= ~HAMMER2_OPFLAG_DIRECTDATA;
				xop->clear_directdata = 1;
			}
			xop->osize = ip->osize;
		} else {
			xop->osize = ip->meta.size;	/* safety */
		}
		xop->ipflags = ip->flags;
		xop->meta = ip->meta;

		atomic_clear_int(&ip->flags, HAMMER2_INODE_RESIZED |
					     HAMMER2_INODE_MODIFIED);
		hammer2_xop_start(&xop->head, &hammer2_inode_chain_sync_desc);
		error = hammer2_xop_collect(&xop->head, 0);
		hammer2_xop_retire(&xop->head, HAMMER2_XOPMASK_VOP);
		if (error == HAMMER2_ERROR_ENOENT)
			error = 0;
		if (error) {
			kprintf("hammer2: unable to fsync inode %p\n", ip);
			/*
			atomic_set_int(&ip->flags,
				       xop->ipflags & (HAMMER2_INODE_RESIZED |
						       HAMMER2_INODE_MODIFIED));
			*/
			/* XXX return error somehow? */
		}
	}
	return error;
}

/*
 * When an inode is flagged INODE_CREATING its chains have not actually
 * been inserting into the on-media tree yet.
 */
int
hammer2_inode_chain_ins(hammer2_inode_t *ip)
{
	int error;

	error = 0;
	if (ip->flags & HAMMER2_INODE_CREATING) {
		hammer2_xop_create_t *xop;

		atomic_clear_int(&ip->flags, HAMMER2_INODE_CREATING);
		xop = hammer2_xop_alloc(ip, HAMMER2_XOP_MODIFYING);
		xop->lhc = ip->meta.inum;
		xop->flags = 0;
		hammer2_xop_start(&xop->head, &hammer2_inode_create_ins_desc);
		error = hammer2_xop_collect(&xop->head, 0);
		hammer2_xop_retire(&xop->head, HAMMER2_XOPMASK_VOP);
		if (error == HAMMER2_ERROR_ENOENT)
			error = 0;
		if (error) {
			kprintf("hammer2: backend unable to "
				"insert inode %p %ld\n", ip, (long)ip->meta.inum);
			/* XXX return error somehow? */
		}
	}
	return error;
}

/*
 * When an inode is flagged INODE_DELETING it has been deleted (no directory
 * entry or open refs are left, though as an optimization H2 might leave
 * nlinks == 1 to avoid unnecessary block updates).  The backend flush then
 * needs to actually remove it from the topology.
 *
 * NOTE: backend flush must still sync and flush the deleted inode to clean
 *	 out related chains.
 *
 * NOTE: We must clear not only INODE_DELETING, but also INODE_ISUNLINKED
 *	 to prevent the vnode reclaim code from trying to delete it twice.
 */
int
hammer2_inode_chain_des(hammer2_inode_t *ip)
{
	int error;

	error = 0;
	if (ip->flags & HAMMER2_INODE_DELETING) {
		hammer2_xop_destroy_t *xop;

		atomic_clear_int(&ip->flags, HAMMER2_INODE_DELETING |
					     HAMMER2_INODE_ISUNLINKED);
		xop = hammer2_xop_alloc(ip, HAMMER2_XOP_MODIFYING);
		hammer2_xop_start(&xop->head, &hammer2_inode_destroy_desc);
		error = hammer2_xop_collect(&xop->head, 0);
		hammer2_xop_retire(&xop->head, HAMMER2_XOPMASK_VOP);

		if (error == HAMMER2_ERROR_ENOENT)
			error = 0;
		if (error) {
			kprintf("hammer2: backend unable to "
				"delete inode %p %ld\n", ip, (long)ip->meta.inum);
			/* XXX return error somehow? */
		}
	}
	return error;
}

/*
 * Flushes the inode's chain and its sub-topology to media.  Interlocks
 * HAMMER2_INODE_DIRTYDATA by clearing it prior to the flush.  Any strategy
 * function creating or modifying a chain under this inode will re-set the
 * flag.
 *
 * inode must be locked.
 */
int
hammer2_inode_chain_flush(hammer2_inode_t *ip, int flags)
{
	hammer2_xop_flush_t *xop;
	int error;

	atomic_clear_int(&ip->flags, HAMMER2_INODE_DIRTYDATA);
	xop = hammer2_xop_alloc(ip, HAMMER2_XOP_MODIFYING | flags);
	hammer2_xop_start(&xop->head, &hammer2_inode_flush_desc);
	error = hammer2_xop_collect(&xop->head, HAMMER2_XOP_COLLECT_WAITALL);
	hammer2_xop_retire(&xop->head, HAMMER2_XOPMASK_VOP);
	if (error == HAMMER2_ERROR_ENOENT)
		error = 0;

	return error;
}

int
vflush(struct mount *mp, int rootrefs, int flags)
{
	hammer2_pfs_t *pmp = MPTOPMP(mp);
	struct hammer2_inode *ip, *tmp;
	struct m_vnode *vp;
	hammer2_key_t count_before, count_after, count_recq;
	hammer2_inum_hash_t *hash;
	int i;

	printf("%s: total chain %ld\n", __func__, hammer2_chain_allocs);
	printf("%s: total dio %d\n", __func__, hammer2_dio_count);

	for (i = 0; i < HAMMER2_INUMHASH_SIZE; ++i) {
		hash = &pmp->inumhash[i];
		hammer2_spin_ex(&hash->spin);
		count_before = 0;
		for (ip = hash->base; ip; ip = ip->next)
			count_before++;

		for (ip = hash->base; ip;) {
			tmp = ip->next;
			vp = ip->vp;
			assert(vp);
			if (!vp->v_vflushed) {
				/*
				 * Not all inodes are modified and ref'd,
				 * so ip->refs requirement here is the initial 1.
				 */
				assert(ip->refs > 0);
				hammer2_inode_drop(ip);
				vp->v_vflushed = 1;
			}
			ip = tmp;
		}

		count_after = 0;
		for (ip = hash->base; ip; ip = ip->next)
			count_after++;
		hammer2_spin_unex(&hash->spin);
	}

	printf("%s: total inode %jd -> %jd\n",
	    __func__, (intmax_t)count_before, (intmax_t)count_after);
	assert(count_before >= count_after);

	count_recq = 0;
	TAILQ_FOREACH(ip, &pmp->recq, recq_entry)
		count_recq++;
	if (count_recq)
		printf("%s: %jd inode in reclaim queue\n",
		    __func__, (intmax_t)count_recq);

	return 0;
}
