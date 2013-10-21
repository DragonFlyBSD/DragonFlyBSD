/*
 * Copyright (c) 2004,2013 The DragonFly Project.  All rights reserved.
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

/*
 * External lock/ref-related vnode functions
 *
 * vs_state transition locking requirements:
 *
 *	INACTIVE -> CACHED|DYING	vx_lock(excl) + vfs_spin
 *	DYING    -> CACHED		vx_lock(excl)
 *	ACTIVE   -> INACTIVE		(none)       + v_spin + vfs_spin
 *	INACTIVE -> ACTIVE		vn_lock(any) + v_spin + vfs_spin
 *	CACHED   -> ACTIVE		vn_lock(any) + v_spin + vfs_spin
 *
 * NOTE: Switching to/from ACTIVE/INACTIVE requires v_spin and vfs_spin,
 *
 *	 Switching into ACTIVE also requires a vref and vnode lock, however
 *	 the vnode lock is allowed to be SHARED.
 *
 *	 Switching into a CACHED or DYING state requires an exclusive vnode
 *	 lock or vx_lock (which is almost the same thing).
 */

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

static void vnode_terminate(struct vnode *vp);

static MALLOC_DEFINE(M_VNODE, "vnodes", "vnode structures");

/*
 * The vnode free list hold inactive vnodes.  Aged inactive vnodes
 * are inserted prior to the mid point, and otherwise inserted
 * at the tail.
 */
TAILQ_HEAD(freelst, vnode);
static struct freelst	vnode_active_list;
static struct freelst	vnode_inactive_list;
static struct vnode	vnode_active_rover;
static struct vnode	vnode_inactive_mid1;
static struct vnode	vnode_inactive_mid2;
static struct vnode	vnode_inactive_rover;
static struct spinlock	vfs_spin = SPINLOCK_INITIALIZER(vfs_spin);
static enum { ROVER_MID1, ROVER_MID2 } rover_state = ROVER_MID2;

int  activevnodes = 0;
SYSCTL_INT(_debug, OID_AUTO, activevnodes, CTLFLAG_RD,
	&activevnodes, 0, "Number of active nodes");
int  cachedvnodes = 0;
SYSCTL_INT(_debug, OID_AUTO, cachedvnodes, CTLFLAG_RD,
	&cachedvnodes, 0, "Number of total cached nodes");
int  inactivevnodes = 0;
SYSCTL_INT(_debug, OID_AUTO, inactivevnodes, CTLFLAG_RD,
	&inactivevnodes, 0, "Number of inactive nodes");
static int wantfreevnodes = 25;
SYSCTL_INT(_debug, OID_AUTO, wantfreevnodes, CTLFLAG_RW,
	&wantfreevnodes, 0, "Desired number of free vnodes");
static int batchfreevnodes = 5;
SYSCTL_INT(_debug, OID_AUTO, batchfreevnodes, CTLFLAG_RW,
	&batchfreevnodes, 0, "Number of vnodes to free at once");
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
	TAILQ_INIT(&vnode_inactive_list);
	TAILQ_INIT(&vnode_active_list);
	TAILQ_INSERT_TAIL(&vnode_active_list, &vnode_active_rover, v_list);
	TAILQ_INSERT_TAIL(&vnode_inactive_list, &vnode_inactive_mid1, v_list);
	TAILQ_INSERT_TAIL(&vnode_inactive_list, &vnode_inactive_mid2, v_list);
	TAILQ_INSERT_TAIL(&vnode_inactive_list, &vnode_inactive_rover, v_list);
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
 * Remove the vnode from the inactive list.
 *
 * _vactivate() may only be called while the vnode lock or VX lock is held.
 * The vnode spinlock need not be held.
 */
static __inline 
void
_vactivate(struct vnode *vp)
{
#ifdef TRACKVNODE
	if ((ulong)vp == trackvnode)
		kprintf("_vactivate %p %08x\n", vp, vp->v_flag);
#endif
	spin_lock(&vfs_spin);
	KKASSERT(vp->v_state == VS_INACTIVE || vp->v_state == VS_CACHED);
	if (vp->v_state == VS_INACTIVE) {
		TAILQ_REMOVE(&vnode_inactive_list, vp, v_list);
		--inactivevnodes;
	}
	TAILQ_INSERT_TAIL(&vnode_active_list, vp, v_list);
	vp->v_state = VS_ACTIVE;
	++activevnodes;
	spin_unlock(&vfs_spin);
}

/*
 * Put a vnode on the inactive list.  The vnode must not currently reside on
 * any list (must be VS_CACHED).  Vnode should be VINACTIVE.
 *
 * Caller must hold v_spin
 */
static __inline
void
_vinactive(struct vnode *vp)
{
#ifdef TRACKVNODE
	if ((ulong)vp == trackvnode) {
		kprintf("_vinactive %p %08x\n", vp, vp->v_flag);
		print_backtrace(-1);
	}
#endif
	spin_lock(&vfs_spin);
	KKASSERT(vp->v_state == VS_CACHED);

	/*
	 * Distinguish between basically dead vnodes, vnodes with cached
	 * data, and vnodes without cached data.  A rover will shift the
	 * vnodes around as their cache status is lost.
	 */
	if (vp->v_flag & VRECLAIMED) {
		TAILQ_INSERT_HEAD(&vnode_inactive_list, vp, v_list);
	} else if (vp->v_object && vp->v_object->resident_page_count) {
		TAILQ_INSERT_TAIL(&vnode_inactive_list, vp, v_list);
	} else if (vp->v_object && vp->v_object->swblock_count) {
		TAILQ_INSERT_BEFORE(&vnode_inactive_mid2, vp, v_list);
	} else {
		TAILQ_INSERT_BEFORE(&vnode_inactive_mid1, vp, v_list);
	}
	++inactivevnodes;
	vp->v_state = VS_INACTIVE;
	spin_unlock(&vfs_spin);
}

static __inline
void
_vinactive_tail(struct vnode *vp)
{
#ifdef TRACKVNODE
	if ((ulong)vp == trackvnode)
		kprintf("_vinactive_tail %p %08x\n", vp, vp->v_flag);
#endif
	spin_lock(&vfs_spin);
	KKASSERT(vp->v_state == VS_CACHED);
	TAILQ_INSERT_TAIL(&vnode_inactive_list, vp, v_list);
	++inactivevnodes;
	vp->v_state = VS_INACTIVE;
	spin_unlock(&vfs_spin);
}

/*
 * Return a C boolean if we should put the vnode on the inactive list
 * (VS_INACTIVE) or leave it alone.
 *
 * This routine is only valid if the vnode is already either VS_INACTIVE or
 * VS_CACHED, or if it can become VS_INACTIV or VS_CACHED via
 * vnode_terminate().
 *
 * WARNING!  We used to indicate FALSE if the vnode had an object with
 *	     resident pages but we no longer do that because it makes
 *	     managing kern.maxvnodes difficult.  Instead we rely on vfree()
 *	     to place the vnode properly on the list.
 *
 * WARNING!  This functions is typically called with v_spin held.
 */
static __inline boolean_t
vshouldfree(struct vnode *vp)
{
	return (vp->v_auxrefs == 0);
}

/*
 * Add a ref to an active vnode.  This function should never be called
 * with an inactive vnode (use vget() instead), but might be called
 * with other states.
 */
void
vref(struct vnode *vp)
{
	KASSERT((VREFCNT(vp) > 0 && vp->v_state != VS_INACTIVE),
		("vref: bad refcnt %08x %d", vp->v_refcnt, vp->v_state));
	atomic_add_int(&vp->v_refcnt, 1);
}

/*
 * Release a ref on an active or inactive vnode.
 *
 * If VREF_FINALIZE is set this will deactivate the vnode on the 1->0
 * transition, otherwise we leave the vnode in the active list and
 * do a lockless transition to 0, which is very important for the
 * critical path.
 */
void
vrele(struct vnode *vp)
{
	for (;;) {
		int count = vp->v_refcnt;
		cpu_ccfence();
		KKASSERT((count & VREF_MASK) > 0);

		/*
		 * 2+ case
		 */
		if ((count & VREF_MASK) > 1) {
			if (atomic_cmpset_int(&vp->v_refcnt, count, count - 1))
				break;
			continue;
		}

		/*
		 * 1->0 transition case must handle possible finalization.
		 * When finalizing we transition 1->0x40000000.  Note that
		 * cachedvnodes is only adjusted on transitions to ->0.
		 */
		if (count & VREF_FINALIZE) {
			vx_lock(vp);
			if (atomic_cmpset_int(&vp->v_refcnt,
					      count, VREF_TERMINATE)) {
				vnode_terminate(vp);
				break;
			}
			vx_unlock(vp);
		} else {
			if (atomic_cmpset_int(&vp->v_refcnt, count, 0)) {
				atomic_add_int(&cachedvnodes, 1);
				break;
			}
		}
		/* retry */
	}
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
 * An auxiliary reference DOES NOT move a vnode out of the VS_INACTIVE
 * state once it has entered it.
 *
 * WARNING!  vhold() must not acquire v_spin.  The spinlock may or may not
 *	     already be held by the caller.  vdrop() will clean up the
 *	     free list state.
 */
void
vhold(struct vnode *vp)
{
	atomic_add_int(&vp->v_auxrefs, 1);
}

/*
 * Remove an auxiliary reference from the vnode.
 *
 * vdrop must check for the case where a vnode is held past its reclamation.
 * We use v_spin to interlock VS_CACHED -> VS_INACTIVE transitions.
 */
void
vdrop(struct vnode *vp)
{
	for (;;) {
		int count = vp->v_auxrefs;
		cpu_ccfence();
		KKASSERT(count > 0);

		/*
		 * 2+ case
		 */
		if (count > 1) {
			if (atomic_cmpset_int(&vp->v_auxrefs, count, count - 1))
				break;
			continue;
		}

		/*
		 * 1->0 transition case must check for reclaimed vnodes that
		 * are expected to be placed on the inactive list.
		 *
		 * v_spin is required for the 1->0 transition.
		 *
		 * 1->0 and 0->1 transitions are allowed to race.  The
		 * vp simply remains on the inactive list.
		 */
		spin_lock(&vp->v_spin);
		if (atomic_cmpset_int(&vp->v_auxrefs, 1, 0)) {
			if (vp->v_state == VS_CACHED && vshouldfree(vp))
				_vinactive(vp);
			spin_unlock(&vp->v_spin);
			break;
		}
		spin_unlock(&vp->v_spin);
		/* retry */
	}
}

/*
 * This function is called with vp vx_lock'd when the last active reference
 * on the vnode is released, typically via vrele().  v_refcnt will be set
 * to VREF_TERMINATE.
 *
 * Additional vrefs are allowed to race but will not result in a reentrant
 * call to vnode_terminate() due to VREF_TERMINATE.
 *
 * NOTE: v_mount may be NULL due to assigmment to dead_vnode_vops
 *
 * NOTE: The vnode may be marked inactive with dirty buffers
 *	 or dirty pages in its cached VM object still present.
 *
 * NOTE: VS_FREE should not be set on entry (the vnode was expected to
 *	 previously be active).  We lose control of the vnode the instant
 *	 it is placed on the free list.
 *
 *	 The VX lock is required when transitioning to VS_CACHED but is
 *	 not sufficient for the vshouldfree() interlocked test or when
 *	 transitioning away from VS_CACHED.  v_spin is also required for
 *	 those cases.
 */
void
vnode_terminate(struct vnode *vp)
{
	KKASSERT(vp->v_state == VS_ACTIVE);

	if ((vp->v_flag & VINACTIVE) == 0) {
		_vsetflags(vp, VINACTIVE);
		if (vp->v_mount)
			VOP_INACTIVE(vp);
		/* might deactivate page */
	}
	spin_lock(&vp->v_spin);
	if (vp->v_state == VS_ACTIVE) {
		spin_lock(&vfs_spin);
		KKASSERT(vp->v_state == VS_ACTIVE);
		TAILQ_REMOVE(&vnode_active_list, vp, v_list);
		--activevnodes;
		vp->v_state = VS_CACHED;
		spin_unlock(&vfs_spin);
	}
	if (vp->v_state == VS_CACHED && vshouldfree(vp))
		_vinactive(vp);
	spin_unlock(&vp->v_spin);
	vx_unlock(vp);
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
	return(lockmgr(&vp->v_lock, LK_EXCLUSIVE | LK_NOWAIT));
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
 * These functions must be used when accessing a vnode that has no
 * chance of being destroyed in a SMP race.  That means the caller will
 * usually either hold an auxiliary reference (such as the namecache)
 * or hold some other lock that ensures that the vnode cannot be destroyed.
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
	 * Reference the structure and then acquire the lock.
	 *
	 * NOTE: The requested lock might be a shared lock and does
	 *	 not protect our access to the refcnt or other fields.
	 */
	if (atomic_fetchadd_int(&vp->v_refcnt, 1) == 0)
		atomic_add_int(&cachedvnodes, -1);

	if ((error = vn_lock(vp, flags)) != 0) {
		/*
		 * The lock failed, undo and return an error.  This will not
		 * normally trigger a termination.
		 */
		vrele(vp);
	} else if (vp->v_flag & VRECLAIMED) {
		/*
		 * The node is being reclaimed and cannot be reactivated
		 * any more, undo and return ENOENT.
		 */
		vn_unlock(vp);
		vrele(vp);
		error = ENOENT;
	} else if (vp->v_state == VS_ACTIVE) {
		/*
		 * A VS_ACTIVE vnode coupled with the fact that we have
		 * a vnode lock (even if shared) prevents v_state from
		 * changing.  Since the vnode is not in a VRECLAIMED state,
		 * we can safely clear VINACTIVE.
		 *
		 * NOTE! Multiple threads may clear VINACTIVE if this is
		 *	 shared lock.  This race is allowed.
		 */
		_vclrflags(vp, VINACTIVE);
		error = 0;
	} else {
		/*
		 * If the vnode is not VS_ACTIVE it must be reactivated
		 * in addition to clearing VINACTIVE.  An exclusive spin_lock
		 * is needed to manipulate the vnode's list.
		 *
		 * Because the lockmgr lock might be shared, we might race
		 * another reactivation, which we handle.  In this situation,
		 * however, the refcnt prevents other v_state races.
		 *
		 * As with above, clearing VINACTIVE is allowed to race other
		 * clearings of VINACTIVE.
		 */
		_vclrflags(vp, VINACTIVE);
		spin_lock(&vp->v_spin);

		switch(vp->v_state) {
		case VS_INACTIVE:
			_vactivate(vp);
			atomic_clear_int(&vp->v_refcnt, VREF_TERMINATE);
			spin_unlock(&vp->v_spin);
			break;
		case VS_CACHED:
			_vactivate(vp);
			atomic_clear_int(&vp->v_refcnt, VREF_TERMINATE);
			spin_unlock(&vp->v_spin);
			break;
		case VS_ACTIVE:
			spin_unlock(&vp->v_spin);
			break;
		case VS_DYING:
			spin_unlock(&vp->v_spin);
			panic("Impossible VS_DYING state");
			break;
		}
		error = 0;
	}
	return(error);
}

#ifdef DEBUG_VPUT

void
debug_vput(struct vnode *vp, const char *filename, int line)
{
	kprintf("vput(%p) %s:%d\n", vp, filename, line);
	vn_unlock(vp);
	vrele(vp);
}

#else

/*
 * MPSAFE
 */
void
vput(struct vnode *vp)
{
	vn_unlock(vp);
	vrele(vp);
}

#endif

/*
 * Acquire the vnode lock unguarded.
 *
 * XXX The vx_*() locks should use auxrefs, not the main reference counter.
 */
void
vx_get(struct vnode *vp)
{
	if (atomic_fetchadd_int(&vp->v_refcnt, 1) == 0)
		atomic_add_int(&cachedvnodes, -1);
	lockmgr(&vp->v_lock, LK_EXCLUSIVE);
}

int
vx_get_nonblock(struct vnode *vp)
{
	int error;

	error = lockmgr(&vp->v_lock, LK_EXCLUSIVE | LK_NOWAIT);
	if (error == 0) {
		if (atomic_fetchadd_int(&vp->v_refcnt, 1) == 0)
			atomic_add_int(&cachedvnodes, -1);
	}
	return(error);
}

/*
 * Relase a VX lock that also held a ref on the vnode.
 *
 * vx_put needs to check for VS_CACHED->VS_INACTIVE transitions to catch
 * the case where e.g. vnlru issues a vgone*(), but should otherwise
 * not mess with the v_state.
 */
void
vx_put(struct vnode *vp)
{
	if (vp->v_state == VS_CACHED && vshouldfree(vp))
		_vinactive(vp);
	lockmgr(&vp->v_lock, LK_RELEASE);
	vrele(vp);
}

/*
 * The rover looks for vnodes past the midline with no cached data and
 * moves them to before the midline.  If we do not do this the midline
 * can wind up in a degenerate state.
 */
static
void
vnode_free_rover_scan_locked(void)
{
	struct vnode *vp;

	/*
	 * Get the vnode after the rover.  The rover roves between mid1 and
	 * the end so the only special vnode it can encounter is mid2.
	 */
	vp = TAILQ_NEXT(&vnode_inactive_rover, v_list);
	if (vp == &vnode_inactive_mid2) {
		vp = TAILQ_NEXT(vp, v_list);
		rover_state = ROVER_MID2;
	}
	KKASSERT(vp != &vnode_inactive_mid1);

	/*
	 * Start over if we finished the scan.
	 */
	TAILQ_REMOVE(&vnode_inactive_list, &vnode_inactive_rover, v_list);
	if (vp == NULL) {
		TAILQ_INSERT_AFTER(&vnode_inactive_list, &vnode_inactive_mid1,
				   &vnode_inactive_rover, v_list);
		rover_state = ROVER_MID1;
		return;
	}
	TAILQ_INSERT_AFTER(&vnode_inactive_list, vp,
			   &vnode_inactive_rover, v_list);

	/*
	 * Shift vp if appropriate.
	 */
	if (vp->v_object && vp->v_object->resident_page_count) {
		/*
		 * Promote vnode with resident pages to section 3.
		 */
		if (rover_state == ROVER_MID1) {
			TAILQ_REMOVE(&vnode_inactive_list, vp, v_list);
			TAILQ_INSERT_TAIL(&vnode_inactive_list, vp, v_list);
		}
	} else if (vp->v_object && vp->v_object->swblock_count) {
		/*
		 * Demote vnode with only swap pages to section 2
		 */
		if (rover_state == ROVER_MID2) {
			TAILQ_REMOVE(&vnode_inactive_list, vp, v_list);
			TAILQ_INSERT_BEFORE(&vnode_inactive_mid2, vp, v_list);
		}
	} else {
		/*
		 * Demote vnode with no cached data to section 1
		 */
		TAILQ_REMOVE(&vnode_inactive_list, vp, v_list);
		TAILQ_INSERT_BEFORE(&vnode_inactive_mid1, vp, v_list);
	}
}

/*
 * Called from vnlru_proc()
 */
void
vnode_free_rover_scan(int count)
{
	spin_lock(&vfs_spin);
	while (count > 0) {
		--count;
		vnode_free_rover_scan_locked();
	}
	spin_unlock(&vfs_spin);
}

/*
 * Try to reuse a vnode from the free list.  This function is somewhat
 * advisory in that NULL can be returned as a normal case, even if free
 * vnodes are present.
 *
 * The scan is limited because it can result in excessive CPU use during
 * periods of extreme vnode use.
 *
 * NOTE: The returned vnode is not completely initialized.
 *
 * MPSAFE
 */
static
struct vnode *
cleanfreevnode(int maxcount)
{
	struct vnode *vp;
	int count;

	/*
	 * Try to deactivate some vnodes cached on the active list.
	 */
	for (count = 0; count < maxcount; count++) {
		if (cachedvnodes - inactivevnodes < inactivevnodes)
			break;

		spin_lock(&vfs_spin);
		vp = TAILQ_NEXT(&vnode_active_rover, v_list);
		TAILQ_REMOVE(&vnode_active_list, &vnode_active_rover, v_list);
		if (vp == NULL) {
			TAILQ_INSERT_HEAD(&vnode_active_list,
					  &vnode_active_rover, v_list);
		} else {
			TAILQ_INSERT_AFTER(&vnode_active_list, vp,
					   &vnode_active_rover, v_list);
		}
		if (vp == NULL || vp->v_refcnt != 0) {
			spin_unlock(&vfs_spin);
			continue;
		}

		/*
		 * Try to deactivate the vnode.
		 */
		if (atomic_fetchadd_int(&vp->v_refcnt, 1) == 0)
			atomic_add_int(&cachedvnodes, -1);
		atomic_set_int(&vp->v_refcnt, VREF_FINALIZE);
		spin_unlock(&vfs_spin);
		vrele(vp);
	}

	/*
	 * Loop trying to lock the first vnode on the free list.
	 * Cycle if we can't.
	 *
	 * We use a bad hack in vx_lock_nonblock() which avoids
	 * the lock order reversal between vfs_spin and v_spin.
	 * This is very fragile code and I don't want to use
	 * vhold here.
	 */
	for (count = 0; count < maxcount; count++) {
		spin_lock(&vfs_spin);
		vnode_free_rover_scan_locked();
		vnode_free_rover_scan_locked();
		vp = TAILQ_FIRST(&vnode_inactive_list);
		while (vp == &vnode_inactive_mid1 ||
		       vp == &vnode_inactive_mid2 ||
		       vp == &vnode_inactive_rover) {
			vp = TAILQ_NEXT(vp, v_list);
		}
		if (vp == NULL) {
			spin_unlock(&vfs_spin);
			break;
		}
		if (vx_lock_nonblock(vp)) {
			KKASSERT(vp->v_state == VS_INACTIVE);
			TAILQ_REMOVE(&vnode_inactive_list, vp, v_list);
			TAILQ_INSERT_TAIL(&vnode_inactive_list, vp, v_list);
			spin_unlock(&vfs_spin);
			continue;
		}

		/*
		 * The vnode should be inactive (VREF_TERMINATE should still
		 * be set in v_refcnt).  Since we pulled it from the inactive
		 * list it should obviously not be VS_CACHED.  Activate the
		 * vnode.
		 *
		 * Once removed from the inactive list we inherit the
		 * VREF_TERMINATE which prevents loss of control while
		 * we mess with the vnode.
		 */
		KKASSERT(vp->v_state == VS_INACTIVE);
		TAILQ_REMOVE(&vnode_inactive_list, vp, v_list);
		--inactivevnodes;
		vp->v_state = VS_DYING;
		spin_unlock(&vfs_spin);
#ifdef TRACKVNODE
		if ((ulong)vp == trackvnode)
			kprintf("cleanfreevnode %p %08x\n", vp, vp->v_flag);
#endif
		/*
		 * Do not reclaim/reuse a vnode while auxillary refs exists.
		 * This includes namecache refs due to a related ncp being
		 * locked or having children, a VM object association, or
		 * other hold users.
		 *
		 * We will make this test several times as auxrefs can
		 * get incremented on us without any spinlocks being held
		 * until we have removed all namecache and inode references
		 * to the vnode.
		 *
		 * The inactive list association reinherits the v_refcnt.
		 */
		if (vp->v_auxrefs) {
			vp->v_state = VS_CACHED;
			_vinactive_tail(vp);
			vx_unlock(vp);
			continue;
		}

		KKASSERT(vp->v_flag & VINACTIVE);
		KKASSERT(vp->v_refcnt & VREF_TERMINATE);

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
				vp->v_state = VS_CACHED;
				_vinactive_tail(vp);
				vx_unlock(vp);
				continue;
			}
			vgone_vxlocked(vp);
			/* vnode is still VX locked */
		}

		/*
		 * We can destroy the vnode if no primary or auxiliary
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
		KKASSERT(vp->v_state == VS_DYING);
		KKASSERT(TAILQ_EMPTY(&vp->v_namecache));
		if (vp->v_auxrefs ||
		    (vp->v_refcnt & ~VREF_FINALIZE) != VREF_TERMINATE) {
			vp->v_state = VS_CACHED;
			_vinactive_tail(vp);
			vx_unlock(vp);
			continue;
		}

		/*
		 * Nothing should have been able to access this vp.
		 */
		atomic_clear_int(&vp->v_refcnt, VREF_TERMINATE|VREF_FINALIZE);
		KASSERT(vp->v_refcnt == 0,
			("vp %p badrefs %08x", vp, vp->v_refcnt));

		/*
		 * Return a VX locked vnode suitable for reuse.  The caller
		 * inherits the sysref.
		 */
		KKASSERT(vp->v_state == VS_DYING);
		return(vp);
	}
	return(NULL);
}

/*
 * Obtain a new vnode.  The returned vnode is VX locked & vrefd.
 *
 * All new vnodes set the VAGE flags.  An open() of the vnode will
 * decrement the (2-bit) flags.  Vnodes which are opened several times
 * are thus retained in the cache over vnodes which are merely stat()d.
 *
 * We always allocate the vnode.  Attempting to recycle existing vnodes
 * here can lead to numerous deadlocks, particularly with softupdates.
 */
struct vnode *
allocvnode(int lktimeout, int lkflags)
{
	struct vnode *vp;

	/*
	 * Do not flag for recyclement unless there are enough free vnodes
	 * to recycle and the number of vnodes has exceeded our target.
	 */
	if (inactivevnodes >= wantfreevnodes && numvnodes >= desiredvnodes) {
		struct thread *td = curthread;
		if (td->td_lwp)
			atomic_set_int(&td->td_lwp->lwp_mpflags, LWP_MP_VNLRU);
	}

	vp = kmalloc(sizeof(*vp), M_VNODE, M_ZERO | M_WAITOK);

	lwkt_token_init(&vp->v_token, "vnode");
	lockinit(&vp->v_lock, "vnode", 0, 0);
	TAILQ_INIT(&vp->v_namecache);
	RB_INIT(&vp->v_rbclean_tree);
	RB_INIT(&vp->v_rbdirty_tree);
	RB_INIT(&vp->v_rbhash_tree);
	spin_init(&vp->v_spin);

	lockmgr(&vp->v_lock, LK_EXCLUSIVE);
	atomic_add_int(&numvnodes, 1);
	vp->v_refcnt = 1;
	vp->v_flag = VAGE0 | VAGE1;

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

	vp->v_filesize = NOOFFSET;
	vp->v_type = VNON;
	vp->v_tag = 0;
	vp->v_state = VS_CACHED;
	_vactivate(vp);

	return (vp);
}

/*
 * Called after a process has allocated a vnode via allocvnode()
 * and we detected that too many vnodes were present.
 *
 * Try to reuse vnodes if we hit the max.  This situation only
 * occurs in certain large-memory (2G+) situations on 32 bit systems,
 * or if kern.maxvnodes is set to very low values.
 *
 * This function is called just prior to a return to userland if the
 * process at some point had to allocate a new vnode during the last
 * system call and the vnode count was found to be excessive.
 *
 * WARNING: Sometimes numvnodes can blow out due to children being
 *	    present under directory vnodes in the namecache.  For the
 *	    moment use an if() instead of a while() and note that if
 *	    we were to use a while() we would still have to break out
 *	    if freesomevnodes() returned 0.
 */
void
allocvnode_gc(void)
{
	if (numvnodes > desiredvnodes && cachedvnodes > wantfreevnodes)
		freesomevnodes(batchfreevnodes);
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
		if ((vp = cleanfreevnode(n * 2)) == NULL)
			break;
		--n;
		++count;
		kfree(vp, M_VNODE);
		atomic_add_int(&numvnodes, -1);
	}
	return(count);
}
