/*
 * Copyright (c) 2004 The DragonFly Project.  All rights reserved.
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
 * $DragonFly: src/sys/kern/vfs_lock.c,v 1.30 2008/06/30 03:57:41 dillon Exp $
 */

/*
 * External virtual filesystem routines
 */
#include "opt_ddb.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mount.h>
#include <sys/proc.h>
#include <sys/vnode.h>
#include <sys/buf.h>
#include <sys/sysctl.h>

#include <machine/limits.h>

#include <vm/vm.h>
#include <vm/vm_object.h>

#include <sys/buf2.h>
#include <sys/thread2.h>
#include <sys/sysref2.h>

static void vnode_terminate(struct vnode *vp);
static boolean_t vnode_ctor(void *obj, void *private, int ocflags);
static void vnode_dtor(void *obj, void *private);

static MALLOC_DEFINE(M_VNODE, "vnodes", "vnode structures");
static struct sysref_class vnode_sysref_class = {
	.name =		"vnode",
	.mtype =	M_VNODE,
	.proto =	SYSREF_PROTO_VNODE,
	.offset =	offsetof(struct vnode, v_sysref),
	.objsize =	sizeof(struct vnode),
	.nom_cache =	256,
	.flags =	SRC_MANAGEDINIT,
	.ctor =		vnode_ctor,
	.dtor =		vnode_dtor,
	.ops = {
		.terminate = (sysref_terminate_func_t)vnode_terminate,
		.lock = (sysref_terminate_func_t)vx_lock,
		.unlock = (sysref_terminate_func_t)vx_unlock
	}
};

/*
 * The vnode free list hold inactive vnodes.  Aged inactive vnodes
 * are inserted prior to the mid point, and otherwise inserted
 * at the tail.
 */
static TAILQ_HEAD(freelst, vnode) vnode_free_list;
static struct vnode	vnode_free_mid1;
static struct vnode	vnode_free_mid2;
static struct vnode	vnode_free_rover;
static struct spinlock	vfs_spin = SPINLOCK_INITIALIZER(vfs_spin);
static enum { ROVER_MID1, ROVER_MID2 } rover_state = ROVER_MID2;

int  freevnodes = 0;
SYSCTL_INT(_debug, OID_AUTO, freevnodes, CTLFLAG_RD,
	&freevnodes, 0, "Number of free nodes");
static int wantfreevnodes = 25;
SYSCTL_INT(_debug, OID_AUTO, wantfreevnodes, CTLFLAG_RW,
	&wantfreevnodes, 0, "Desired number of free vnodes");
#ifdef TRACKVNODE
static ulong trackvnode;
SYSCTL_ULONG(_debug, OID_AUTO, trackvnode, CTLFLAG_RW,
		&trackvnode, 0, "");
#endif

/*
 * Called from vfsinit()
 */
void
vfs_lock_init(void)
{
	TAILQ_INIT(&vnode_free_list);
	TAILQ_INSERT_TAIL(&vnode_free_list, &vnode_free_mid1, v_freelist);
	TAILQ_INSERT_TAIL(&vnode_free_list, &vnode_free_mid2, v_freelist);
	TAILQ_INSERT_TAIL(&vnode_free_list, &vnode_free_rover, v_freelist);
	spin_init(&vfs_spin);
	kmalloc_raise_limit(M_VNODE, 0);	/* unlimited */
}

/*
 * Misc functions
 */
static __inline
void
_vsetflags(struct vnode *vp, int flags)
{
	atomic_set_int(&vp->v_flag, flags);
}

static __inline
void
_vclrflags(struct vnode *vp, int flags)
{
	atomic_clear_int(&vp->v_flag, flags);
}

void
vsetflags(struct vnode *vp, int flags)
{
	_vsetflags(vp, flags);
}

void
vclrflags(struct vnode *vp, int flags)
{
	_vclrflags(vp, flags);
}

/*
 * Inline helper functions.
 *
 * WARNING: vbusy() may only be called while the vnode lock or VX lock
 *	    is held.  The vnode spinlock need not be held.
 *
 * MPSAFE
 */
static __inline
void
__vbusy_interlocked(struct vnode *vp)
{
	KKASSERT(vp->v_flag & VFREE);
	TAILQ_REMOVE(&vnode_free_list, vp, v_freelist);
	freevnodes--;
	_vclrflags(vp, VFREE);
}

static __inline 
void
__vbusy(struct vnode *vp)
{
#ifdef TRACKVNODE
	if ((ulong)vp == trackvnode)
		kprintf("__vbusy %p %08x\n", vp, vp->v_flag);
#endif
	spin_lock(&vfs_spin);
	__vbusy_interlocked(vp);
	spin_unlock(&vfs_spin);
}

/*
 * Put a vnode on the free list.  The caller has cleared VCACHED or owns the
 * implied sysref related to having removed the vnode from the freelist
 * (and VCACHED is already clear in that case).
 *
 * MPSAFE
 */
static __inline
void
__vfree(struct vnode *vp)
{
#ifdef TRACKVNODE
	if ((ulong)vp == trackvnode) {
		kprintf("__vfree %p %08x\n", vp, vp->v_flag);
		print_backtrace(-1);
	}
#endif
	spin_lock(&vfs_spin);
	KKASSERT((vp->v_flag & VFREE) == 0);

	/*
	 * Distinguish between basically dead vnodes, vnodes with cached
	 * data, and vnodes without cached data.  A rover will shift the
	 * vnodes around as their cache status is lost.
	 */
	if (vp->v_flag & VRECLAIMED) {
		TAILQ_INSERT_HEAD(&vnode_free_list, vp, v_freelist);
	} else if (vp->v_object && vp->v_object->resident_page_count) {
		TAILQ_INSERT_TAIL(&vnode_free_list, vp, v_freelist);
	} else if (vp->v_object && vp->v_object->swblock_count) {
		TAILQ_INSERT_BEFORE(&vnode_free_mid2, vp, v_freelist);
	} else {
		TAILQ_INSERT_BEFORE(&vnode_free_mid1, vp, v_freelist);
	}
	freevnodes++;
	_vsetflags(vp, VFREE);
	spin_unlock(&vfs_spin);
}

/*
 * Put a vnode on the free list.  The caller has cleared VCACHED or owns the
 * implied sysref related to having removed the vnode from the freelist
 * (and VCACHED is already clear in that case).
 *
 * MPSAFE
 */
static __inline
void
__vfreetail(struct vnode *vp)
{
#ifdef TRACKVNODE
	if ((ulong)vp == trackvnode)
		kprintf("__vfreetail %p %08x\n", vp, vp->v_flag);
#endif
	spin_lock(&vfs_spin);
	KKASSERT((vp->v_flag & VFREE) == 0);
	TAILQ_INSERT_TAIL(&vnode_free_list, vp, v_freelist);
	freevnodes++;
	_vsetflags(vp, VFREE);
	spin_unlock(&vfs_spin);
}

/*
 * Return a C boolean if we should put the vnode on the freelist (VFREE),
 * or leave it / mark it as VCACHED.
 *
 * This routine is only valid if the vnode is already either VFREE or
 * VCACHED, or if it can become VFREE or VCACHED via vnode_terminate().
 *
 * WARNING!  This functions is typically called with v_spinlock held.
 *
 * MPSAFE
 */
static __inline boolean_t
vshouldfree(struct vnode *vp)
{
	return (vp->v_auxrefs == 0 &&
	    (vp->v_object == NULL || vp->v_object->resident_page_count == 0));
}

/*
 * Add a ref to an active vnode.  This function should never be called
 * with an inactive vnode (use vget() instead).
 *
 * MPSAFE
 */
void
vref(struct vnode *vp)
{
	KKASSERT(vp->v_sysref.refcnt > 0 && 
		 (vp->v_flag & (VFREE|VINACTIVE)) == 0);
	sysref_get(&vp->v_sysref);
}

/*
 * Release a ref on an active or inactive vnode.  The sysref termination
 * function will be called when the active last active reference is released,
 * and the vnode is returned to the objcache when the last inactive
 * reference is released.
 */
void
vrele(struct vnode *vp)
{
	sysref_put(&vp->v_sysref);
}

/*
 * Add an auxiliary data structure reference to the vnode.  Auxiliary
 * references do not change the state of the vnode or prevent them
 * from being deactivated, reclaimed, or placed on or removed from
 * the free list.
 *
 * An auxiliary reference DOES prevent the vnode from being destroyed,
 * allowing you to vx_lock() it, test state, etc.
 *
 * An auxiliary reference DOES NOT move a vnode out of the VFREE state
 * once it has entered it.
 *
 * WARNING!  vhold() and vhold_interlocked() must not acquire v_spinlock.
 *	     The spinlock may or may not already be held by the caller.
 *	     vdrop() will clean up the free list state.
 *
 * MPSAFE
 */
void
vhold(struct vnode *vp)
{
	KKASSERT(vp->v_sysref.refcnt != 0);
	atomic_add_int(&vp->v_auxrefs, 1);
}

void
vhold_interlocked(struct vnode *vp)
{
	atomic_add_int(&vp->v_auxrefs, 1);
}

/*
 * Remove an auxiliary reference from the vnode.
 *
 * vdrop needs to check for a VCACHE->VFREE transition to catch cases
 * where a vnode is held past its reclamation.  We use v_spinlock to
 * interlock VCACHED -> !VCACHED transitions.
 *
 * MPSAFE
 */
void
vdrop(struct vnode *vp)
{
	KKASSERT(vp->v_sysref.refcnt != 0 && vp->v_auxrefs > 0);
	spin_lock(&vp->v_spinlock);
	atomic_subtract_int(&vp->v_auxrefs, 1);
	if ((vp->v_flag & VCACHED) && vshouldfree(vp)) {
		_vclrflags(vp, VCACHED);
		__vfree(vp);
	}
	spin_unlock(&vp->v_spinlock);
}

/*
 * This function is called when the last active reference on the vnode
 * is released, typically via vrele().  SYSREF will VX lock the vnode
 * and then give the vnode a negative ref count, indicating that it is
 * undergoing termination or is being set aside for the cache, and one
 * final sysref_put() is required to actually return it to the memory
 * subsystem.
 *
 * Additional inactive sysrefs may race us but that's ok.  Reactivations
 * cannot race us because the sysref code interlocked with the VX lock
 * (which is held on call).
 *
 * MPSAFE
 */
void
vnode_terminate(struct vnode *vp)
{
	/*
	 * We own the VX lock, it should not be possible for someone else
	 * to have reactivated the vp.
	 */
	KKASSERT(sysref_isinactive(&vp->v_sysref));

	/*
	 * Deactivate the vnode by marking it VFREE or VCACHED.
	 * The vnode can be reactivated from either state until
	 * reclaimed.  These states inherit the 'last' sysref on the
	 * vnode.
	 *
	 * NOTE: There may be additional inactive references from
	 * other entities blocking on the VX lock while we hold it,
	 * but this does not prevent us from changing the vnode's
	 * state.
	 *
	 * NOTE: The vnode could already be marked inactive.  XXX
	 *	 how?
	 *
	 * NOTE: v_mount may be NULL due to assignment to
	 *	 dead_vnode_vops
	 *
	 * NOTE: The vnode may be marked inactive with dirty buffers
	 *	 or dirty pages in its cached VM object still present.
	 *
	 * NOTE: VCACHED should not be set on entry.  We lose control
	 *	 of the sysref the instant the vnode is placed on the
	 *	 free list or when VCACHED is set.
	 *
	 *	 The VX lock is required when transitioning to
	 *	 +VCACHED but is not sufficient for the vshouldfree()
	 *	 interlocked test or when transitioning to -VCACHED.
	 */
	if ((vp->v_flag & VINACTIVE) == 0) {
		_vsetflags(vp, VINACTIVE);
		if (vp->v_mount)
			VOP_INACTIVE(vp);
	}
	spin_lock(&vp->v_spinlock);
	KKASSERT((vp->v_flag & (VFREE|VCACHED)) == 0);
	if (vshouldfree(vp))
		__vfree(vp);
	else
		_vsetflags(vp, VCACHED); /* inactive but not yet free*/
	spin_unlock(&vp->v_spinlock);
	vx_unlock(vp);
}

/*
 * Physical vnode constructor / destructor.  These are only executed on
 * the backend of the objcache.  They are NOT executed on every vnode
 * allocation or deallocation.
 *
 * MPSAFE
 */
boolean_t
vnode_ctor(void *obj, void *private, int ocflags)
{
	struct vnode *vp = obj;

	lwkt_token_init(&vp->v_token, "vnode");
	lockinit(&vp->v_lock, "vnode", 0, 0);
	ccms_dataspace_init(&vp->v_ccms);
	TAILQ_INIT(&vp->v_namecache);
	RB_INIT(&vp->v_rbclean_tree);
	RB_INIT(&vp->v_rbdirty_tree);
	RB_INIT(&vp->v_rbhash_tree);
	return(TRUE);
}

/*
 * MPSAFE
 */
void
vnode_dtor(void *obj, void *private)
{
	struct vnode *vp = obj;

	KKASSERT((vp->v_flag & (VCACHED|VFREE)) == 0);
	ccms_dataspace_destroy(&vp->v_ccms);
}

/****************************************************************
 *			VX LOCKING FUNCTIONS			*
 ****************************************************************
 *
 * These functions lock vnodes for reclamation and deactivation related
 * activities.  The caller must already be holding some sort of reference
 * on the vnode.
 *
 * MPSAFE
 */
void
vx_lock(struct vnode *vp)
{
	lockmgr(&vp->v_lock, LK_EXCLUSIVE);
}

/*
 * The non-blocking version also uses a slightly different mechanic.
 * This function will explicitly fail not only if it cannot acquire
 * the lock normally, but also if the caller already holds a lock.
 *
 * The adjusted mechanic is used to close a loophole where complex
 * VOP_RECLAIM code can circle around recursively and allocate the
 * same vnode it is trying to destroy from the freelist.
 *
 * Any filesystem (aka UFS) which puts LK_CANRECURSE in lk_flags can
 * cause the incorrect behavior to occur.  If not for that lockmgr()
 * would do the right thing.
 */
static int
vx_lock_nonblock(struct vnode *vp)
{
	if (lockcountnb(&vp->v_lock))
		return(EBUSY);
	return(lockmgr(&vp->v_lock, LK_EXCLUSIVE | LK_NOWAIT | LK_NOSPINWAIT));
}

void
vx_unlock(struct vnode *vp)
{
	lockmgr(&vp->v_lock, LK_RELEASE);
}

/****************************************************************
 *			VNODE ACQUISITION FUNCTIONS		*
 ****************************************************************
 *
 * These functions must be used when accessing a vnode via an auxiliary
 * reference such as the namecache or free list, or when you wish to
 * do a combo ref+lock sequence.
 *
 * These functions are MANDATORY for any code chain accessing a vnode
 * whos activation state is not known.
 *
 * vget() can be called with LK_NOWAIT and will return EBUSY if the
 * lock cannot be immediately acquired.
 *
 * vget()/vput() are used when reactivation is desired.
 *
 * vx_get() and vx_put() are used when reactivation is not desired.
 */
int
vget(struct vnode *vp, int flags)
{
	int error;

	/*
	 * A lock type must be passed
	 */
	if ((flags & LK_TYPE_MASK) == 0) {
		panic("vget() called with no lock specified!");
		/* NOT REACHED */
	}

	/*
	 * Reference the structure and then acquire the lock.  0->1
	 * transitions and refs during termination are allowed here so
	 * call sysref directly.
	 *
	 * NOTE: The requested lock might be a shared lock and does
	 *	 not protect our access to the refcnt or other fields.
	 */
	sysref_get(&vp->v_sysref);
	if ((error = vn_lock(vp, flags)) != 0) {
		/*
		 * The lock failed, undo and return an error.
		 */
		sysref_put(&vp->v_sysref);
	} else if (vp->v_flag & VRECLAIMED) {
		/*
		 * The node is being reclaimed and cannot be reactivated
		 * any more, undo and return ENOENT.
		 */
		vn_unlock(vp);
		vrele(vp);
		error = ENOENT;
	} else {
		/*
		 * If the vnode is marked VFREE or VCACHED it needs to be
		 * reactivated, otherwise it had better already be active.
		 * VINACTIVE must also be cleared.
		 *
		 * In the VFREE/VCACHED case we have to throw away the
		 * sysref that was earmarking those cases and preventing
		 * the vnode from being destroyed.  Our sysref is still held.
		 *
		 * We are allowed to reactivate the vnode while we hold
		 * the VX lock, assuming it can be reactivated.
		 */
		spin_lock(&vp->v_spinlock);
		if (vp->v_flag & VFREE) {
			__vbusy(vp);
			sysref_activate(&vp->v_sysref);
			spin_unlock(&vp->v_spinlock);
			sysref_put(&vp->v_sysref);
		} else if (vp->v_flag & VCACHED) {
			_vclrflags(vp, VCACHED);
			sysref_activate(&vp->v_sysref);
			spin_unlock(&vp->v_spinlock);
			sysref_put(&vp->v_sysref);
		} else {
			if (sysref_isinactive(&vp->v_sysref)) {
				sysref_activate(&vp->v_sysref);
				kprintf("Warning vp %p reactivation race\n",
					vp);
			}
			spin_unlock(&vp->v_spinlock);
		}
		_vclrflags(vp, VINACTIVE);
		error = 0;
	}
	return(error);
}

/*
 * MPSAFE
 */
void
vput(struct vnode *vp)
{
	vn_unlock(vp);
	vrele(vp);
}

/*
 * XXX The vx_*() locks should use auxrefs, not the main reference counter.
 *
 * MPSAFE
 */
void
vx_get(struct vnode *vp)
{
	sysref_get(&vp->v_sysref);
	lockmgr(&vp->v_lock, LK_EXCLUSIVE);
}

/*
 * MPSAFE
 */
int
vx_get_nonblock(struct vnode *vp)
{
	int error;

	sysref_get(&vp->v_sysref);
	error = lockmgr(&vp->v_lock, LK_EXCLUSIVE | LK_NOWAIT);
	if (error)
		sysref_put(&vp->v_sysref);
	return(error);
}

/*
 * Relase a VX lock that also held a ref on the vnode.
 *
 * vx_put needs to check for a VCACHED->VFREE transition to catch the
 * case where e.g. vnlru issues a vgone*().
 *
 * MPSAFE
 */
void
vx_put(struct vnode *vp)
{
	spin_lock(&vp->v_spinlock);
	if ((vp->v_flag & VCACHED) && vshouldfree(vp)) {
		_vclrflags(vp, VCACHED);
		__vfree(vp);
	}
	spin_unlock(&vp->v_spinlock);
	lockmgr(&vp->v_lock, LK_RELEASE);
	sysref_put(&vp->v_sysref);
}

/*
 * The rover looks for vnodes past the midline with no cached data and
 * moves them to before the midline.  If we do not do this the midline
 * can wind up in a degenerate state.
 */
static
void
vnode_rover_locked(void)
{
	struct vnode *vp;

	/*
	 * Get the vnode after the rover.  The rover roves between mid1 and
	 * the end so the only special vnode it can encounter is mid2.
	 */
	vp = TAILQ_NEXT(&vnode_free_rover, v_freelist);
	if (vp == &vnode_free_mid2) {
		vp = TAILQ_NEXT(vp, v_freelist);
		rover_state = ROVER_MID2;
	}
	KKASSERT(vp != &vnode_free_mid1);

	/*
	 * Start over if we finished the scan.
	 */
	TAILQ_REMOVE(&vnode_free_list, &vnode_free_rover, v_freelist);
	if (vp == NULL) {
		TAILQ_INSERT_AFTER(&vnode_free_list, &vnode_free_mid1,
				   &vnode_free_rover, v_freelist);
		rover_state = ROVER_MID1;
		return;
	}
	TAILQ_INSERT_AFTER(&vnode_free_list, vp, &vnode_free_rover, v_freelist);

	/*
	 * Shift vp if appropriate.
	 */
	if (vp->v_object && vp->v_object->resident_page_count) {
		/*
		 * Promote vnode with resident pages to section 3.
		 * (This case shouldn't happen).
		 */
		if (rover_state == ROVER_MID1) {
			TAILQ_REMOVE(&vnode_free_list, vp, v_freelist);
			TAILQ_INSERT_TAIL(&vnode_free_list, vp, v_freelist);
		}
	} else if (vp->v_object && vp->v_object->swblock_count) {
		/*
		 * Demote vnode with only swap pages to section 2
		 */
		if (rover_state == ROVER_MID2) {
			TAILQ_REMOVE(&vnode_free_list, vp, v_freelist);
			TAILQ_INSERT_BEFORE(&vnode_free_mid2, vp, v_freelist);
		}
	} else {
		/*
		 * Demote vnode with no cached data to section 1
		 */
		TAILQ_REMOVE(&vnode_free_list, vp, v_freelist);
		TAILQ_INSERT_BEFORE(&vnode_free_mid1, vp, v_freelist);
	}
}

/*
 * Try to reuse a vnode from the free list.
 *
 * NOTE: The returned vnode is not completely initialized.
 *
 * WARNING: The freevnodes count can race, NULL can be returned even if
 *	    freevnodes != 0.
 *
 * MPSAFE
 */
static
struct vnode *
allocfreevnode(void)
{
	struct vnode *vp;
	int count;

	for (count = 0; count < freevnodes; count++) {
		/*
		 * Try to lock the first vnode on the free list.
		 * Cycle if we can't.
		 *
		 * We use a bad hack in vx_lock_nonblock() which avoids
		 * the lock order reversal between vfs_spin and v_spinlock.
		 * This is very fragile code and I don't want to use
		 * vhold here.
		 */
		spin_lock(&vfs_spin);
		vnode_rover_locked();
		vnode_rover_locked();
		vp = TAILQ_FIRST(&vnode_free_list);
		while (vp == &vnode_free_mid1 || vp == &vnode_free_mid2 ||
		       vp == &vnode_free_rover) {
			vp = TAILQ_NEXT(vp, v_freelist);
		}
		if (vp == NULL)
			break;
		if (vx_lock_nonblock(vp)) {
			KKASSERT(vp->v_flag & VFREE);
			TAILQ_REMOVE(&vnode_free_list, vp, v_freelist);
			TAILQ_INSERT_TAIL(&vnode_free_list,
					  vp, v_freelist);
			spin_unlock(&vfs_spin);
			continue;
		}

		/*
		 * We inherit the sysref associated the vnode on the free
		 * list.  Because VCACHED is clear the vnode will not
		 * be placed back on the free list.  We own the sysref
		 * free and clear and thus control the disposition of
		 * the vnode.
		 */
		__vbusy_interlocked(vp);
		spin_unlock(&vfs_spin);
#ifdef TRACKVNODE
		if ((ulong)vp == trackvnode)
			kprintf("allocfreevnode %p %08x\n", vp, vp->v_flag);
#endif
		/*
		 * Do not reclaim/reuse a vnode while auxillary refs exists.
		 * This includes namecache refs due to a related ncp being
		 * locked or having children.
		 *
		 * We will make this test several times as auxrefs can
		 * get incremented on us without any spinlocks being held
		 * until we have removed all namecache and inode references
		 * to the vnode.
		 *
		 * Because VCACHED is already in the correct state (cleared)
		 * we cannot race other vdrop()s occuring at the same time
		 * and can safely place vp on the free list.
		 *
		 * The free list association reinherits the sysref.
		 */
		if (vp->v_auxrefs) {
			__vfreetail(vp);
			vx_unlock(vp);
			continue;
		}

		/*
		 * We inherit the reference that was previously associated
		 * with the vnode being on the free list.  VCACHED had better
		 * not be set because the reference and VX lock prevents
		 * the sysref from transitioning to an active state.
		 */
		KKASSERT((vp->v_flag & (VINACTIVE|VCACHED)) == VINACTIVE);
		KKASSERT(sysref_isinactive(&vp->v_sysref));

		/*
		 * Holding the VX lock on an inactive vnode prevents it
		 * from being reactivated or reused.  New namecache
		 * associations can only be made using active vnodes.
		 *
		 * Another thread may be blocked on our vnode lock while
		 * holding a namecache lock.  We can only reuse this vnode
		 * if we can clear all namecache associations without
		 * blocking.
		 *
		 * Because VCACHED is already in the correct state (cleared)
		 * we cannot race other vdrop()s occuring at the same time
		 * and can safely place vp on the free list.
		 */
		if ((vp->v_flag & VRECLAIMED) == 0) {
			if (cache_inval_vp_nonblock(vp)) {
				__vfreetail(vp);
				vx_unlock(vp);
				continue;
			}
			vgone_vxlocked(vp);
			/* vnode is still VX locked */
		}

		/*
		 * We can reuse the vnode if no primary or auxiliary
		 * references remain other then ours, else put it
		 * back on the free list and keep looking.
		 *
		 * Either the free list inherits the last reference
		 * or we fall through and sysref_activate() the last
		 * reference.
		 *
		 * Since the vnode is in a VRECLAIMED state, no new
		 * namecache associations could have been made.
		 */
		KKASSERT(TAILQ_EMPTY(&vp->v_namecache));
		if (vp->v_auxrefs ||
		    !sysref_islastdeactivation(&vp->v_sysref)) {
			__vfreetail(vp);
			vx_unlock(vp);
			continue;
		}

		/*
		 * Return a VX locked vnode suitable for reuse.  The caller
		 * inherits the sysref.
		 */
		return(vp);
	}
	return(NULL);
}

/*
 * Obtain a new vnode from the freelist, allocating more if necessary.
 * The returned vnode is VX locked & refd.
 *
 * All new vnodes set the VAGE flags.  An open() of the vnode will
 * decrement the (2-bit) flags.  Vnodes which are opened several times
 * are thus retained in the cache over vnodes which are merely stat()d.
 *
 * MPSAFE
 */
struct vnode *
allocvnode(int lktimeout, int lkflags)
{
	struct vnode *vp;

	/*
	 * Try to reuse vnodes if we hit the max.  This situation only
	 * occurs in certain large-memory (2G+) situations.  We cannot
	 * attempt to directly reclaim vnodes due to nasty recursion
	 * problems.
	 */
	while (numvnodes - freevnodes > desiredvnodes)
		vnlru_proc_wait();

	/*
	 * Try to build up as many vnodes as we can before reallocating
	 * from the free list.  A vnode on the free list simply means
	 * that it is inactive with no resident pages.  It may or may not
	 * have been reclaimed and could have valuable information associated 
	 * with it that we shouldn't throw away unless we really need to.
	 *
	 * HAMMER NOTE: Re-establishing a vnode is a fairly expensive
	 * operation for HAMMER but this should benefit UFS as well.
	 */
	if (freevnodes >= wantfreevnodes && numvnodes >= desiredvnodes)
		vp = allocfreevnode();
	else
		vp = NULL;
	if (vp == NULL) {
		vp = sysref_alloc(&vnode_sysref_class);
		KKASSERT((vp->v_flag & (VCACHED|VFREE)) == 0);
		lockmgr(&vp->v_lock, LK_EXCLUSIVE);
		numvnodes++;
	}

	/*
	 * We are using a managed sysref class, vnode fields are only
	 * zerod on initial allocation from the backing store, not
	 * on reallocation.  Thus we have to clear these fields for both
	 * reallocation and reuse.
	 */
#ifdef INVARIANTS
	if (vp->v_data)
		panic("cleaned vnode isn't");
	if (bio_track_active(&vp->v_track_read) ||
	    bio_track_active(&vp->v_track_write)) {
		panic("Clean vnode has pending I/O's");
	}
	if (vp->v_flag & VONWORKLST)
		panic("Clean vnode still pending on syncer worklist!");
	if (!RB_EMPTY(&vp->v_rbdirty_tree))
		panic("Clean vnode still has dirty buffers!");
	if (!RB_EMPTY(&vp->v_rbclean_tree))
		panic("Clean vnode still has clean buffers!");
	if (!RB_EMPTY(&vp->v_rbhash_tree))
		panic("Clean vnode still on hash tree!");
	KKASSERT(vp->v_mount == NULL);
#endif
	vp->v_flag = VAGE0 | VAGE1;
	vp->v_lastw = 0;
	vp->v_lasta = 0;
	vp->v_cstart = 0;
	vp->v_clen = 0;
	vp->v_socket = 0;
	vp->v_opencount = 0;
	vp->v_writecount = 0;	/* XXX */

	/*
	 * lktimeout only applies when LK_TIMELOCK is used, and only
	 * the pageout daemon uses it.  The timeout may not be zero
	 * or the pageout daemon can deadlock in low-VM situations.
	 */
	if (lktimeout == 0)
		lktimeout = hz / 10;
	lockreinit(&vp->v_lock, "vnode", lktimeout, lkflags);
	KKASSERT(TAILQ_EMPTY(&vp->v_namecache));
	/* exclusive lock still held */

	/*
	 * Note: sysref needs to be activated to convert -0x40000000 to +1.
	 * The -0x40000000 comes from the last ref on reuse, and from
	 * sysref_init() on allocate.
	 */
	sysref_activate(&vp->v_sysref);
	vp->v_filesize = NOOFFSET;
	vp->v_type = VNON;
	vp->v_tag = 0;
	vp->v_ops = NULL;
	vp->v_data = NULL;
	KKASSERT(vp->v_mount == NULL);

	return (vp);
}

/*
 * MPSAFE
 */
int
freesomevnodes(int n)
{
	struct vnode *vp;
	int count = 0;

	while (n) {
		--n;
		if ((vp = allocfreevnode()) == NULL)
			break;
		vx_put(vp);
		--numvnodes;
	}
	return(count);
}
