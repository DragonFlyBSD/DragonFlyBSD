/*
 * Copyright (c) 2011-2018 The DragonFly Project.  All rights reserved.
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
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/types.h>
#include <sys/lock.h>
#include <sys/uuid.h>

#include "hammer2.h"

#define INODE_DEBUG	0

RB_GENERATE2(hammer2_inode_tree, hammer2_inode, rbnode, hammer2_inode_cmp,
	     hammer2_tid_t, meta.inum);

int
hammer2_inode_cmp(hammer2_inode_t *ip1, hammer2_inode_t *ip2)
{
	if (ip1->meta.inum < ip2->meta.inum)
		return(-1);
	if (ip1->meta.inum > ip2->meta.inum)
		return(1);
	return(0);
}

static __inline
void
hammer2_knote(struct vnode *vp, int flags)
{
	if (flags)
		KNOTE(&vp->v_pollinfo.vpi_kqinfo.ki_note, flags);
}

static
void
hammer2_inode_delayed_sideq(hammer2_inode_t *ip)
{
	hammer2_pfs_t *pmp = ip->pmp;

	if ((ip->flags & (HAMMER2_INODE_SYNCQ | HAMMER2_INODE_SIDEQ)) == 0) {
		hammer2_spin_ex(&pmp->list_spin);
		if ((ip->flags & (HAMMER2_INODE_SYNCQ |
				  HAMMER2_INODE_SIDEQ)) == 0) {
			hammer2_inode_ref(ip);
			atomic_set_int(&ip->flags, HAMMER2_INODE_SIDEQ);
			TAILQ_INSERT_TAIL(&pmp->sideq, ip, entry);
			++pmp->sideq_count;
			hammer2_spin_unex(&pmp->list_spin);
		} else {
			hammer2_spin_unex(&pmp->list_spin);
		}
	}
}

/*
 * Lock an inode, with SYNCQ semantics.
 *
 * HAMMER2 offers shared and exclusive locks on inodes.  Pass a mask of
 * flags for options:
 *
 *	- pass HAMMER2_RESOLVE_SHARED if a shared lock is desired.  The
 *	  inode locking function will automatically set the RDONLY flag.
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
 * When an exclusive lock is obtained on an inode that is on the SYNCQ,
 * HAMMER2 will automatically move the inode to the front of the queue before
 * blocking to avoid long stalls against filesystem sync operations.
 *
 * The inode locking function locks the inode itself, resolves any stale
 * chains in the inode's cluster, and allocates a fresh copy of the
 * cluster with 1 ref and all the underlying chains locked.
 *
 * ip->cluster will be stable while the inode is locked.
 *
 * NOTE: We don't combine the inode/chain lock because putting away an
 *       inode would otherwise confuse multiple lock holders of the inode.
 *
 * NOTE: In-memory inodes always point to hardlink targets (the actual file),
 *	 and never point to a hardlink pointer.
 *
 * NOTE: If caller passes HAMMER2_RESOLVE_RDONLY the exclusive locking code
 *	 will feel free to reduce the chain set in the cluster as an
 *	 optimization.  It will still be validated against the quorum if
 *	 appropriate, but the optimization might be able to reduce data
 *	 accesses to one node.  This flag is automatically set if the inode
 *	 is locked with HAMMER2_RESOLVE_SHARED.
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
		/*how |= HAMMER2_RESOLVE_RDONLY; not used */
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
 * This function will also ensure that if any
 */
void
hammer2_inode_lock4(hammer2_inode_t *ip1, hammer2_inode_t *ip2,
		    hammer2_inode_t *ip3, hammer2_inode_t *ip4)
{
	hammer2_inode_t *ips[4];
	hammer2_inode_t *iptmp;
	hammer2_pfs_t *pmp;
	size_t count;
	size_t i;
	int dosyncq;

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
	dosyncq = 0;

	/*
	 * Lock the inodes in order
	 */
	for (i = 0; i < count; ++i) {
		iptmp = ips[i];
		hammer2_mtx_ex(&iptmp->lock);
		if (iptmp->flags & HAMMER2_INODE_SYNCQ)
			dosyncq |= 1;
		if (iptmp->flags & HAMMER2_INODE_SYNCQ_PASS2)
			dosyncq |= 2;
	}

	/*
	 * If any of the inodes are part of a filesystem sync then we
	 * have to make sure they ALL are, because their modifications
	 * depend on each other (e.g. inode vs dirent).
	 */
	for (i = 0; (dosyncq & 3) && i < count; ++i) {
		iptmp = ips[i];
		hammer2_spin_ex(&pmp->list_spin);
		atomic_set_int(&iptmp->flags, HAMMER2_INODE_SYNCQ_WAKEUP);
		if (iptmp->flags & HAMMER2_INODE_SYNCQ) {
			TAILQ_REMOVE(&pmp->syncq, iptmp, entry);
			TAILQ_INSERT_HEAD(&pmp->syncq, iptmp, entry);
		} else if (iptmp->flags & HAMMER2_INODE_SIDEQ) {
			atomic_set_int(&iptmp->flags, HAMMER2_INODE_SYNCQ);
			atomic_clear_int(&iptmp->flags, HAMMER2_INODE_SIDEQ);
			TAILQ_REMOVE(&pmp->sideq, iptmp, entry);
			TAILQ_INSERT_HEAD(&pmp->syncq, iptmp, entry);
		} else {
			atomic_set_int(&iptmp->flags, HAMMER2_INODE_SYNCQ);
			hammer2_inode_ref(iptmp);
			TAILQ_INSERT_HEAD(&pmp->syncq, iptmp, entry);
		}
		hammer2_spin_unex(&pmp->list_spin);
	}

	/*
	 * Block and retry if any of the inodes are on SYNCQ.  It is
	 * important that we allow the operation to proceed in the
	 * PASS2 case, to avoid deadlocking against the vnode.
	 */
	if (dosyncq & 1) {
		for (i = 0; i < count; ++i)
			hammer2_mtx_unlock(&ips[i]->lock);
		tsleep(&iptmp->flags, 0, "h2sync", 2);
		goto restart;
	}
#if 0
		if (pmp == NULL ||
		    ((iptmp->flags & (HAMMER2_INODE_SYNCQ |
				      HAMMER2_INODE_SYNCQ_PASS2)) == 0 &&
		     dosyncq == 0)) {
			continue;
		}
		dosyncq = 1;
		tsleep_interlock(&iptmp->flags, 0);

		/*
		 * We have to accept the inode if it's got more than one
		 * exclusive count because we can't safely unlock it.
		 */
		if (hammer2_mtx_refs(&iptmp->lock) > 1)
			continue;

		/*
		 * Unlock everything (including the current index) and wait
		 * for our wakeup.
		 */
		for (j = 0; j <= i; ++j)
			hammer2_mtx_unlock(&ips[j]->lock);
		tsleep(&iptmp->flags, PINTERLOCKED, "h2sync", 0);
		/*tsleep(&iptmp->flags, 0, "h2sync2", 1);*/

		goto restart;
	}
#endif
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
 * If either ip1 or ip2 are on SYNCQ, make sure the other one is too.
 * This ensure that dependencies (e.g. directory-v-inode) are flushed
 * together.
 *
 * We must also check SYNCQ_PASS2, which occurs when the syncer cannot
 * immediately lock the inode on SYNCQ and must temporarily move it to
 * SIDEQ to retry again in another pass (but part of the same flush).
 *
 * Both ip1 and ip2 must be locked by the caller.  This also ensures
 * that we can't race the end of the syncer's queue run.
 */
void
hammer2_inode_depend(hammer2_inode_t *ip1, hammer2_inode_t *ip2)
{
	hammer2_pfs_t *pmp;

	pmp = ip1->pmp;
	if (((ip1->flags | ip2->flags) & HAMMER2_INODE_SYNCQ) == 0)
		return;
	if ((ip1->flags & (HAMMER2_INODE_SYNCQ |
			   HAMMER2_INODE_SYNCQ_PASS2)) &&
	    (ip2->flags & (HAMMER2_INODE_SYNCQ |
			   HAMMER2_INODE_SYNCQ_PASS2))) {
		return;
	}
	KKASSERT(pmp == ip2->pmp);
	hammer2_spin_ex(&pmp->list_spin);
	if ((ip1->flags & (HAMMER2_INODE_SYNCQ |
			   HAMMER2_INODE_SYNCQ_PASS2)) == 0) {
		if (ip1->flags & HAMMER2_INODE_SIDEQ) {
			atomic_set_int(&ip1->flags, HAMMER2_INODE_SYNCQ);
			atomic_clear_int(&ip1->flags, HAMMER2_INODE_SIDEQ);
			TAILQ_REMOVE(&pmp->sideq, ip1, entry);
			TAILQ_INSERT_TAIL(&pmp->syncq, ip1, entry);
		} else {
			atomic_set_int(&ip1->flags, HAMMER2_INODE_SYNCQ);
			hammer2_inode_ref(ip1);
			TAILQ_INSERT_TAIL(&pmp->syncq, ip1, entry);
		}
	}
	if ((ip2->flags & (HAMMER2_INODE_SYNCQ |
			   HAMMER2_INODE_SYNCQ_PASS2)) == 0) {
		if (ip2->flags & HAMMER2_INODE_SIDEQ) {
			atomic_set_int(&ip2->flags, HAMMER2_INODE_SYNCQ);
			atomic_clear_int(&ip2->flags, HAMMER2_INODE_SIDEQ);
			TAILQ_REMOVE(&pmp->sideq, ip2, entry);
			TAILQ_INSERT_TAIL(&pmp->syncq, ip2, entry);
		} else {
			atomic_set_int(&ip2->flags, HAMMER2_INODE_SYNCQ);
			hammer2_inode_ref(ip2);
			TAILQ_INSERT_TAIL(&pmp->syncq, ip2, entry);
		}
	}
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

	if (mtx_islocked_ex(&ip->lock)) {
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
		mtx_downgrade(&ip->lock);
}

/*
 * Lookup an inode by inode number
 */
hammer2_inode_t *
hammer2_inode_lookup(hammer2_pfs_t *pmp, hammer2_tid_t inum)
{
	hammer2_inode_t *ip;

	KKASSERT(pmp);
	if (pmp->spmp_hmp) {
		ip = NULL;
	} else {
		hammer2_spin_ex(&pmp->inum_spin);
		ip = RB_LOOKUP(hammer2_inode_tree, &pmp->inum_tree, inum);
		if (ip)
			hammer2_inode_ref(ip);
		hammer2_spin_unex(&pmp->inum_spin);
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
			pmp = ip->pmp;
			KKASSERT(pmp);
			hammer2_spin_ex(&pmp->inum_spin);

			if (atomic_cmpset_int(&ip->refs, 1, 0)) {
				KKASSERT(hammer2_mtx_refs(&ip->lock) == 0);
				if (ip->flags & HAMMER2_INODE_ONRBTREE) {
					atomic_clear_int(&ip->flags,
						     HAMMER2_INODE_ONRBTREE);
					RB_REMOVE(hammer2_inode_tree,
						  &pmp->inum_tree, ip);
					--pmp->inum_count;
				}
				hammer2_spin_unex(&pmp->inum_spin);

				ip->pmp = NULL;

				/*
				 * Cleaning out ip->cluster isn't entirely
				 * trivial.
				 */
				hammer2_inode_repoint(ip, NULL, NULL);

				kfree(ip, pmp->minode);
				atomic_add_long(&pmp->inmem_inodes, -1);
				ip = NULL;	/* will terminate loop */
			} else {
				hammer2_spin_unex(&ip->pmp->inum_spin);
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
struct vnode *
hammer2_igetv(hammer2_inode_t *ip, int *errorp)
{
	hammer2_pfs_t *pmp;
	struct vnode *vp;

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
			vp->v_ops = &pmp->mp->mnt_vn_spec_ops;
			if (ip->meta.type != HAMMER2_OBJTYPE_CDEV)
				vp->v_type = VBLK;
			addaliasu(vp,
				  ip->meta.rmajor,
				  ip->meta.rminor);
			break;
		case HAMMER2_OBJTYPE_FIFO:
			vp->v_type = VFIFO;
			vp->v_ops = &pmp->mp->mnt_vn_fifo_ops;
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
		break;
	}

	/*
	 * Return non-NULL vp and *errorp == 0, or NULL vp and *errorp != 0.
	 */
	if (hammer2_debug & 0x0002) {
		kprintf("igetv vp %p refs 0x%08x aux 0x%08x\n",
			vp, vp->v_refcnt, vp->v_auxrefs);
	}
	return (vp);
}

/*
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
		 * Handle SMP race (not applicable to the super-root spmp
		 * which can't index inodes due to duplicative inode numbers).
		 */
		hammer2_mtx_ex(&nip->lock);
		if (pmp->spmp_hmp == NULL &&
		    (nip->flags & HAMMER2_INODE_ONRBTREE) == 0) {
			hammer2_mtx_unlock(&nip->lock);
			hammer2_inode_drop(nip);
			goto again;
		}
		if (xop) {
			if (idx >= 0)
				hammer2_inode_repoint_one(nip, &xop->cluster,
							  idx);
			else
				hammer2_inode_repoint(nip, NULL, &xop->cluster);
		}
		return nip;
	}

	/*
	 * We couldn't find the inode number, create a new inode and try to
	 * insert it, handle insertion races.
	 */
	nip = kmalloc(sizeof(*nip), pmp->minode, M_WAITOK | M_ZERO);
	spin_init(&nip->cluster_spin, "h2clspin");
	atomic_add_long(&pmp->inmem_inodes, 1);
	hammer2_pfs_memory_inc(pmp);
	hammer2_pfs_memory_wakeup(pmp);
	if (pmp->spmp_hmp)
		nip->flags = HAMMER2_INODE_SROOT;

	/*
	 * Initialize nip's cluster.  A cluster is provided for normal
	 * inodes but typically not for the super-root or PFS inodes.
	 */
	nip->cluster.refs = 1;
	nip->cluster.pmp = pmp;
	nip->cluster.flags |= HAMMER2_CLUSTER_INODE;
	if (xop) {
		nipdata = &hammer2_xop_gdata(xop)->ipdata;
		nip->meta = nipdata->meta;
		hammer2_xop_pdata(xop);
		atomic_set_int(&nip->flags, HAMMER2_INODE_METAGOOD);
		hammer2_inode_repoint(nip, NULL, &xop->cluster);
	} else {
		nip->meta.inum = inum;		/* PFS inum is always 1 XXX */
		/* mtime will be updated when a cluster is available */
		atomic_set_int(&nip->flags, HAMMER2_INODE_METAGOOD);	/*XXX*/
	}

	nip->pmp = pmp;

	/*
	 * ref and lock on nip gives it state compatible to after a
	 * hammer2_inode_lock() call.
	 */
	nip->refs = 1;
	hammer2_mtx_init(&nip->lock, "h2inode");
	hammer2_mtx_ex(&nip->lock);
	/* combination of thread lock and chain lock == inode lock */

	/*
	 * Attempt to add the inode.  If it fails we raced another inode
	 * get.  Undo all the work and try again.
	 */
	if (pmp->spmp_hmp == NULL) {
		hammer2_spin_ex(&pmp->inum_spin);
		if (RB_INSERT(hammer2_inode_tree, &pmp->inum_tree, nip)) {
			hammer2_spin_unex(&pmp->inum_spin);
			hammer2_mtx_unlock(&nip->lock);
			hammer2_inode_drop(nip);
			goto again;
		}
		atomic_set_int(&nip->flags, HAMMER2_INODE_ONRBTREE);
		++pmp->inum_count;
		hammer2_spin_unex(&pmp->inum_spin);
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
		     const uint8_t *name, size_t name_len,
		     int *errorp)
{
	hammer2_xop_create_t *xop;
	hammer2_inode_t *pip;
	hammer2_inode_t *nip;
	int error;
	uuid_t pip_uid;
	uuid_t pip_gid;
	uint32_t pip_mode;
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

	pip_uid = pip->meta.uid;
	pip_gid = pip->meta.gid;
	pip_mode = pip->meta.mode;
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
	hammer2_update_time(&xop->meta.ctime);
	xop->meta.mtime = xop->meta.ctime;
	xop->meta.mode = 0755;
	xop->meta.nlinks = 1;

	/*
	 * Regular files and softlinks allow a small amount of data to be
	 * directly embedded in the inode.  This flag will be cleared if
	 * the size is extended past the embedded limit.
	 */
	if (xop->meta.type == HAMMER2_OBJTYPE_REGFILE ||
	    xop->meta.type == HAMMER2_OBJTYPE_SOFTLINK) {
		xop->meta.op_flags |= HAMMER2_OPFLAG_DIRECTDATA;
	}
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
	uint8_t type;

	dip = pip->pmp->iroot;
	KKASSERT(dip != NULL);

	*errorp = 0;

	hammer2_inode_lock(dip, 0);

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
		nip->meta.rmajor = vap->va_rmajor;
		nip->meta.rminor = vap->va_rminor;
		break;
	default:
		break;
	}
	type = nip->meta.type;

	KKASSERT(nip->meta.inum == inum);
	nip->meta.iparent = pip_inum;
	
	/* Inherit parent's inode compression mode. */
	nip->meta.comp_algo = pip_comp_algo;
	nip->meta.check_algo = pip_check_algo;
	nip->meta.version = HAMMER2_INODE_VERSION_ONE;
	hammer2_update_time(&nip->meta.ctime);
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
	KKASSERT(vap);

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
	hammer2_inode_repoint(nip, NULL, &xop->head.cluster);
done:
	hammer2_xop_retire(&xop->head, HAMMER2_XOPMASK_VOP);
	hammer2_inode_unlock(dip);

	return (nip);
}

/*
 * Create a directory entry under dip with the specified name, inode number,
 * and OBJTYPE (type).
 *
 * This returns a UNIX errno code, not a HAMMER2_ERROR_* code.
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
	hammer2_inode_lock(dip, 0);

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
	hammer2_inode_unlock(dip);

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
hammer2_inode_repoint(hammer2_inode_t *ip, hammer2_inode_t *pip,
		      hammer2_cluster_t *cluster)
{
	hammer2_chain_t *dropch[HAMMER2_MAXCLUSTER];
	hammer2_chain_t *ochain;
	hammer2_chain_t *nchain;
	int i;

	bzero(dropch, sizeof(dropch));

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
	hammer2_chain_t *ochain;
	hammer2_chain_t *nchain;
	int i;

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

/*
 * Called with a locked inode to finish unlinking an inode after xop_unlink
 * had been run.  This function is responsible for decrementing nlinks.
 *
 * We don't bother decrementing nlinks if the file is not open and this was
 * the last link.
 *
 * If the inode is a hardlink target it's chain has not yet been deleted,
 * otherwise it's chain has been deleted.
 *
 * If isopen then any prior deletion was not permanent and the inode is
 * left intact with nlinks == 0;
 */
int
hammer2_inode_unlink_finisher(hammer2_inode_t *ip, int isopen)
{
	hammer2_pfs_t *pmp;
	int error;

	pmp = ip->pmp;

	/*
	 * Decrement nlinks.  If this is the last link and the file is
	 * not open we can just delete the inode and not bother dropping
	 * nlinks to 0 (avoiding unnecessary block updates).
	 */
	if (ip->meta.nlinks == 1) {
		atomic_set_int(&ip->flags, HAMMER2_INODE_ISUNLINKED);
		if (isopen == 0)
			goto killit;
	}

	hammer2_inode_modify(ip);
	--ip->meta.nlinks;
	if ((int64_t)ip->meta.nlinks < 0)
		ip->meta.nlinks = 0;	/* safety */

	/*
	 * If nlinks is not zero we are done.  However, this should only be
	 * possible with a hardlink target.  If the inode is an embedded
	 * hardlink nlinks should have dropped to zero, warn and proceed
	 * with the next step.
	 */
	if (ip->meta.nlinks) {
		if ((ip->meta.name_key & HAMMER2_DIRHASH_VISIBLE) == 0)
			return 0;
		kprintf("hammer2_inode_unlink: nlinks was not 0 (%jd)\n",
			(intmax_t)ip->meta.nlinks);
		return 0;
	}

	if (ip->vp)
		hammer2_knote(ip->vp, NOTE_DELETE);

	/*
	 * nlinks is now an implied zero, delete the inode if not open.
	 * We avoid unnecessary media updates by not bothering to actually
	 * decrement nlinks for the 1->0 transition
	 *
	 * Put the inode on the sideq to ensure that any disconnected chains
	 * get properly flushed (so they can be freed).
	 */
	if (isopen == 0) {
		hammer2_xop_destroy_t *xop;

killit:
		hammer2_inode_delayed_sideq(ip);
		atomic_set_int(&ip->flags, HAMMER2_INODE_ISDELETED);
		xop = hammer2_xop_alloc(ip, HAMMER2_XOP_MODIFYING);
		hammer2_xop_start(&xop->head, &hammer2_inode_destroy_desc);
		error = hammer2_xop_collect(&xop->head, 0);
		hammer2_xop_retire(&xop->head, HAMMER2_XOPMASK_VOP);
	}
	error = 0;	/* XXX */

	return error;
}

/*
 * Mark an inode as being modified, meaning that the caller will modify
 * ip->meta.
 *
 * If a vnode is present we set the vnode dirty and the nominal filesystem
 * sync will also handle synchronizing the inode meta-data.  If no vnode
 * is present we must ensure that the inode is on pmp->sideq.
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
				"insert inode %p %ld\n", ip, ip->meta.inum);
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
	hammer2_xop_fsync_t *xop;
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

#if 0
/*
 * The normal filesystem sync no longer has visibility to an inode structure
 * after its vnode has been reclaimed.  In this situation a dirty inode may
 * require additional processing to synchronize ip->meta to its underlying
 * cluster nodes.
 *
 * In particular, reclaims can occur in almost any state (for example, when
 * doing operations on unrelated vnodes) and flushing the reclaimed inode
 * in the reclaim path itself is a non-starter.
 *
 * Caller must be in a transaction.
 */
void
hammer2_inode_run_sideq(hammer2_pfs_t *pmp, int doall)
{
	hammer2_xop_destroy_t *xop;
	hammer2_inode_sideq_t *ipul;
	hammer2_inode_t *ip;
	int error;

	/*
	 * Nothing to do if sideq is empty or (if doall == 0) there just
	 * aren't very many sideq entries.
	 */
	if (TAILQ_EMPTY(&pmp->sideq))
		return;
	if (doall == 0) {
		if (pmp->sideq_count > (pmp->inum_count >> 3)) {
			if (hammer2_debug & 0x0001) {
				kprintf("hammer2: flush sideq %ld/%ld\n",
					pmp->sideq_count, pmp->inum_count);
			}
		}
	}

	if (doall == 0 && pmp->sideq_count <= (pmp->inum_count >> 3))
		return;

	hammer2_spin_ex(&pmp->list_spin);
	while ((ipul = TAILQ_FIRST(&pmp->sideq)) != NULL) {
		TAILQ_REMOVE(&pmp->sideq, ipul, entry);
		--pmp->sideq_count;
		ip = ipul->ip;
		KKASSERT(ip->flags & HAMMER2_INODE_ONSIDEQ);
		atomic_clear_int(&ip->flags, HAMMER2_INODE_ONSIDEQ);
		hammer2_spin_unex(&pmp->list_spin);
		kfree(ipul, pmp->minode);

		hammer2_inode_lock(ip, 0);
		if (ip->flags & HAMMER2_INODE_ISDELETED) {
			/*
			 * The inode has already been deleted.  This is a
			 * fairly rare circumstance.  For now we don't rock
			 * the boat and synchronize it normally.
			 */
			hammer2_inode_chain_sync(ip);
			hammer2_inode_chain_flush(ip);
		} else if (ip->flags & HAMMER2_INODE_ISUNLINKED) {
			/*
			 * The inode was unlinked while open.  The inode must
			 * be deleted and destroyed.
			 */
			xop = hammer2_xop_alloc(ip, HAMMER2_XOP_MODIFYING);
			hammer2_xop_start(&xop->head,
					  &hammer2_inode_destroy_desc);
			error = hammer2_xop_collect(&xop->head, 0);
			/* XXX error handling */
			hammer2_xop_retire(&xop->head, HAMMER2_XOPMASK_VOP);
		} else {
			/*
			 * The inode was dirty as-of the reclaim, requiring
			 * synchronization of ip->meta with its underlying
			 * chains.
			 */
			hammer2_inode_chain_sync(ip);
			hammer2_inode_chain_flush(ip);
		}

		hammer2_inode_unlock(ip);
		hammer2_inode_drop(ip);			/* ipul ref */

		hammer2_spin_ex(&pmp->list_spin);

		/*
		 * If doall is 0 the original sideq_count was greater than
		 * 1/8 the inode count.  Add some hysteresis in the loop,
		 * don't stop flushing until sideq_count drops below 1/16.
		 */
		if (doall == 0 && pmp->sideq_count <= (pmp->inum_count >> 4)) {
			if (hammer2_debug & 0x0001) {
				kprintf("hammer2: flush sideq %ld/%ld (end)\n",
					pmp->sideq_count, pmp->inum_count);
			}
			break;
		}
	}
	hammer2_spin_unex(&pmp->list_spin);
}
#endif
