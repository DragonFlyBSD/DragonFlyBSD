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

#define VACT_MAX	10
#define VACT_INC	2

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
static struct spinlock	vfs_spin = SPINLOCK_INITIALIZER(vfs_spin, "vfs_spin");

int  activevnodes = 0;
SYSCTL_INT(_debug, OID_AUTO, activevnodes, CTLFLAG_RD,
	&activevnodes, 0, "Number of active nodes");
int  cachedvnodes = 0;
SYSCTL_INT(_debug, OID_AUTO, cachedvnodes, CTLFLAG_RD,
	&cachedvnodes, 0, "Number of total cached nodes");
int  inactivevnodes = 0;
SYSCTL_INT(_debug, OID_AUTO, inactivevnodes, CTLFLAG_RD,
	&inactivevnodes, 0, "Number of inactive nodes");
static int batchfreevnodes = 5;
SYSCTL_INT(_debug, OID_AUTO, batchfreevnodes, CTLFLAG_RW,
	&batchfreevnodes, 0, "Number of vnodes to free at once");
#ifdef TRACKVNODE
static u_long trackvnode;
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
	spin_init(&vfs_spin, "vfslock");
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
 * Place the vnode on the active list.
 *
 * Caller must hold vp->v_spin
 */
static __inline 
void
_vactivate(struct vnode *vp)
{
#ifdef TRACKVNODE
	if ((u_long)vp == trackvnode)
		kprintf("_vactivate %p %08x\n", vp, vp->v_flag);
#endif
	spin_lock(&vfs_spin);

	switch(vp->v_state) {
	case VS_ACTIVE:
		panic("_vactivate: already active");
		/* NOT REACHED */
		spin_unlock(&vfs_spin);
		return;
	case VS_INACTIVE:
		TAILQ_REMOVE(&vnode_inactive_list, vp, v_list);
		--inactivevnodes;
		break;
	case VS_CACHED:
	case VS_DYING:
		break;
	}
	TAILQ_INSERT_TAIL(&vnode_active_list, vp, v_list);
	vp->v_state = VS_ACTIVE;
	++activevnodes;

	spin_unlock(&vfs_spin);
}

/*
 * Put a vnode on the inactive list.
 *
 * Caller must hold v_spin
 */
static __inline
void
_vinactive(struct vnode *vp)
{
#ifdef TRACKVNODE
	if ((u_long)vp == trackvnode) {
		kprintf("_vinactive %p %08x\n", vp, vp->v_flag);
		print_backtrace(-1);
	}
#endif
	spin_lock(&vfs_spin);

	/*
	 * Remove from active list if it is sitting on it
	 */
	switch(vp->v_state) {
	case VS_ACTIVE:
		TAILQ_REMOVE(&vnode_active_list, vp, v_list);
		--activevnodes;
		break;
	case VS_INACTIVE:
		panic("_vinactive: already inactive");
		/* NOT REACHED */
		spin_unlock(&vfs_spin);
		return;
	case VS_CACHED:
	case VS_DYING:
		break;
	}

	/*
	 * Distinguish between basically dead vnodes, vnodes with cached
	 * data, and vnodes without cached data.  A rover will shift the
	 * vnodes around as their cache status is lost.
	 */
	if (vp->v_flag & VRECLAIMED) {
		TAILQ_INSERT_HEAD(&vnode_inactive_list, vp, v_list);
	} else {
		TAILQ_INSERT_TAIL(&vnode_inactive_list, vp, v_list);
	}
	++inactivevnodes;
	vp->v_state = VS_INACTIVE;

	spin_unlock(&vfs_spin);
}

static __inline
void
_vinactive_tail(struct vnode *vp)
{
	spin_lock(&vfs_spin);

	/*
	 * Remove from active list if it is sitting on it
	 */
	switch(vp->v_state) {
	case VS_ACTIVE:
		TAILQ_REMOVE(&vnode_active_list, vp, v_list);
		--activevnodes;
		break;
	case VS_INACTIVE:
		panic("_vinactive_tail: already inactive");
		/* NOT REACHED */
		spin_unlock(&vfs_spin);
		return;
	case VS_CACHED:
	case VS_DYING:
		break;
	}

	TAILQ_INSERT_TAIL(&vnode_inactive_list, vp, v_list);
	++inactivevnodes;
	vp->v_state = VS_INACTIVE;

	spin_unlock(&vfs_spin);
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
 * Caller has no other requirements.
 *
 * If VREF_FINALIZE is set this will deactivate the vnode on the 1->0
 * transition, otherwise we leave the vnode in the active list and
 * do a lockless transition to 0, which is very important for the
 * critical path.
 *
 * (vrele() is not called when a vnode is being destroyed w/kfree)
 */
void
vrele(struct vnode *vp)
{
	for (;;) {
		int count = vp->v_refcnt;
		cpu_ccfence();
		KKASSERT((count & VREF_MASK) > 0);
		KKASSERT(vp->v_state == VS_ACTIVE ||
			 vp->v_state == VS_INACTIVE);

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
		 *
		 * WARNING! VREF_TERMINATE can be cleared at any point
		 *	    when the refcnt is non-zero (by vget()) and
		 *	    the vnode has not been reclaimed.  Thus
		 *	    transitions out of VREF_TERMINATE do not have
		 *	    to mess with cachedvnodes.
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
 * references do not change the state of the vnode or prevent deactivation
 * or reclamation of the vnode, but will prevent the vnode from being
 * destroyed (kfree()'d).
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
 */
void
vdrop(struct vnode *vp)
{
	atomic_add_int(&vp->v_auxrefs, -1);
}

/*
 * This function is called on the 1->0 transition (which is actually
 * 1->VREF_TERMINATE) when VREF_FINALIZE is set, forcing deactivation
 * of the vnode.
 *
 * Additional vrefs are allowed to race but will not result in a reentrant
 * call to vnode_terminate() due to refcnt being VREF_TERMINATE.  This
 * prevents additional 1->0 transitions.
 *
 * ONLY A VGET() CAN REACTIVATE THE VNODE.
 *
 * Caller must hold the VX lock.
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
static
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
 */
void
vx_lock(struct vnode *vp)
{
	lockmgr(&vp->v_lock, LK_EXCLUSIVE);
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
	if ((atomic_fetchadd_int(&vp->v_refcnt, 1) & VREF_MASK) == 0)
		atomic_add_int(&cachedvnodes, -1);

	if ((error = vn_lock(vp, flags | LK_FAILRECLAIM)) != 0) {
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
		_vclrflags(vp, VINACTIVE);	/* SMP race ok */
		vp->v_act += VACT_INC;
		if (vp->v_act > VACT_MAX)	/* SMP race ok */
			vp->v_act = VACT_MAX;
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
		 *
		 * VREF_TERMINATE and VREF_FINALIZE can only be cleared when
		 * the refcnt is non-zero and the vnode has not been
		 * reclaimed.  This also means that the transitions do
		 * not affect cachedvnodes.
		 */
		_vclrflags(vp, VINACTIVE);
		vp->v_act += VACT_INC;
		if (vp->v_act > VACT_MAX)	/* SMP race ok */
			vp->v_act = VACT_MAX;
		spin_lock(&vp->v_spin);

		switch(vp->v_state) {
		case VS_INACTIVE:
			_vactivate(vp);
			atomic_clear_int(&vp->v_refcnt, VREF_TERMINATE |
							VREF_FINALIZE);
			spin_unlock(&vp->v_spin);
			break;
		case VS_CACHED:
			_vactivate(vp);
			atomic_clear_int(&vp->v_refcnt, VREF_TERMINATE |
							VREF_FINALIZE);
			spin_unlock(&vp->v_spin);
			break;
		case VS_ACTIVE:
			atomic_clear_int(&vp->v_refcnt, VREF_FINALIZE);
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
 *
 * XXX The vx_*() locks should use auxrefs, not the main reference counter.
 */
void
vx_get(struct vnode *vp)
{
	if ((atomic_fetchadd_int(&vp->v_refcnt, 1) & VREF_MASK) == 0)
		atomic_add_int(&cachedvnodes, -1);
	lockmgr(&vp->v_lock, LK_EXCLUSIVE);
}

int
vx_get_nonblock(struct vnode *vp)
{
	int error;

	if (lockcountnb(&vp->v_lock))
		return(EBUSY);
	error = lockmgr(&vp->v_lock, LK_EXCLUSIVE | LK_NOWAIT);
	if (error == 0) {
		if ((atomic_fetchadd_int(&vp->v_refcnt, 1) & VREF_MASK) == 0)
			atomic_add_int(&cachedvnodes, -1);
	}
	return(error);
}

/*
 * Release a VX lock that also held a ref on the vnode.  vrele() will handle
 * any needed state transitions.
 *
 * However, filesystems use this function to get rid of unwanted new vnodes
 * so try to get the vnode on the correct queue in that case.
 */
void
vx_put(struct vnode *vp)
{
	if (vp->v_type == VNON || vp->v_type == VBAD)
		atomic_set_int(&vp->v_refcnt, VREF_FINALIZE);
	lockmgr(&vp->v_lock, LK_RELEASE);
	vrele(vp);
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
 */
static
struct vnode *
cleanfreevnode(int maxcount)
{
	struct vnode *vp;
	int count;
	int trigger = (long)vmstats.v_page_count / (activevnodes * 2 + 1);

	/*
	 * Try to deactivate some vnodes cached on the active list.
	 */
	if (cachedvnodes < inactivevnodes)
		goto skip;

	for (count = 0; count < maxcount * 2; count++) {
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
		if (vp == NULL) {
			spin_unlock(&vfs_spin);
			continue;
		}
		if ((vp->v_refcnt & VREF_MASK) != 0) {
			spin_unlock(&vfs_spin);
			vp->v_act += VACT_INC;
			if (vp->v_act > VACT_MAX)	/* SMP race ok */
				vp->v_act = VACT_MAX;
			continue;
		}

		/*
		 * decrement by less if the vnode's object has a lot of
		 * VM pages.  XXX possible SMP races.
		 */
		if (vp->v_act > 0) {
			vm_object_t obj;
			if ((obj = vp->v_object) != NULL &&
			    obj->resident_page_count >= trigger) {
				vp->v_act -= 1;
			} else {
				vp->v_act -= VACT_INC;
			}
			if (vp->v_act < 0)
				vp->v_act = 0;
			spin_unlock(&vfs_spin);
			continue;
		}

		/*
		 * Try to deactivate the vnode.
		 */
		if ((atomic_fetchadd_int(&vp->v_refcnt, 1) & VREF_MASK) == 0)
			atomic_add_int(&cachedvnodes, -1);
		atomic_set_int(&vp->v_refcnt, VREF_FINALIZE);

		spin_unlock(&vfs_spin);
		vrele(vp);
	}

skip:
	/*
	 * Loop trying to lock the first vnode on the free list.
	 * Cycle if we can't.
	 */
	for (count = 0; count < maxcount; count++) {
		spin_lock(&vfs_spin);

		vp = TAILQ_FIRST(&vnode_inactive_list);
		if (vp == NULL) {
			spin_unlock(&vfs_spin);
			break;
		}

		/*
		 * non-blocking vx_get will also ref the vnode on success.
		 */
		if (vx_get_nonblock(vp)) {
			KKASSERT(vp->v_state == VS_INACTIVE);
			TAILQ_REMOVE(&vnode_inactive_list, vp, v_list);
			TAILQ_INSERT_TAIL(&vnode_inactive_list, vp, v_list);
			spin_unlock(&vfs_spin);
			continue;
		}

		/*
		 * Because we are holding vfs_spin the vnode should currently
		 * be inactive and VREF_TERMINATE should still be set.
		 *
		 * Once vfs_spin is released the vnode's state should remain
		 * unmodified due to both the lock and ref on it.
		 */
		KKASSERT(vp->v_state == VS_INACTIVE);
		spin_unlock(&vfs_spin);
#ifdef TRACKVNODE
		if ((u_long)vp == trackvnode)
			kprintf("cleanfreevnode %p %08x\n", vp, vp->v_flag);
#endif

		/*
		 * Do not reclaim/reuse a vnode while auxillary refs exists.
		 * This includes namecache refs due to a related ncp being
		 * locked or having children, a VM object association, or
		 * other hold users.
		 *
		 * Do not reclaim/reuse a vnode if someone else has a real
		 * ref on it.  This can occur if a filesystem temporarily
		 * releases the vnode lock during VOP_RECLAIM.
		 */
		if (vp->v_auxrefs ||
		    (vp->v_refcnt & ~VREF_FINALIZE) != VREF_TERMINATE + 1) {
failed:
			if (vp->v_state == VS_INACTIVE) {
				spin_lock(&vfs_spin);
				if (vp->v_state == VS_INACTIVE) {
					TAILQ_REMOVE(&vnode_inactive_list,
						     vp, v_list);
					TAILQ_INSERT_TAIL(&vnode_inactive_list,
							  vp, v_list);
				}
				spin_unlock(&vfs_spin);
			}
			vx_put(vp);
			continue;
		}

		/*
		 * VINACTIVE and VREF_TERMINATE are expected to both be set
		 * for vnodes pulled from the inactive list, and cannot be
		 * changed while we hold the vx lock.
		 *
		 * Try to reclaim the vnode.
		 */
		KKASSERT(vp->v_flag & VINACTIVE);
		KKASSERT(vp->v_refcnt & VREF_TERMINATE);

		if ((vp->v_flag & VRECLAIMED) == 0) {
			if (cache_inval_vp_nonblock(vp))
				goto failed;
			vgone_vxlocked(vp);
			/* vnode is still VX locked */
		}

		/*
		 * At this point if there are no other refs or auxrefs on
		 * the vnode with the inactive list locked, and we remove
		 * the vnode from the inactive list, it should not be
		 * possible for anyone else to access the vnode any more.
		 *
		 * Since the vnode is in a VRECLAIMED state, no new
		 * namecache associations could have been made and the
		 * vnode should have already been removed from its mountlist.
		 *
		 * Since we hold a VX lock on the vnode it cannot have been
		 * reactivated (moved out of the inactive list).
		 */
		KKASSERT(TAILQ_EMPTY(&vp->v_namecache));
		spin_lock(&vfs_spin);
		if (vp->v_auxrefs ||
		    (vp->v_refcnt & ~VREF_FINALIZE) != VREF_TERMINATE + 1) {
			spin_unlock(&vfs_spin);
			goto failed;
		}
		KKASSERT(vp->v_state == VS_INACTIVE);
		TAILQ_REMOVE(&vnode_inactive_list, vp, v_list);
		--inactivevnodes;
		vp->v_state = VS_DYING;
		spin_unlock(&vfs_spin);

		/*
		 * Nothing should have been able to access this vp.  Only
		 * our ref should remain now.
		 */
		atomic_clear_int(&vp->v_refcnt, VREF_TERMINATE|VREF_FINALIZE);
		KASSERT(vp->v_refcnt == 1,
			("vp %p badrefs %08x", vp, vp->v_refcnt));

		/*
		 * Return a VX locked vnode suitable for reuse.
		 */
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
	 * Do not flag for synchronous recyclement unless there are enough
	 * freeable vnodes to recycle and the number of vnodes has
	 * significantly exceeded our target.  We want the normal vnlru
	 * process to handle the cleaning (at 9/10's) before we are forced
	 * to flag it here at 11/10's for userexit path processing.
	 */
	if (numvnodes >= desiredvnodes * 11 / 10 &&
	    cachedvnodes + inactivevnodes >= desiredvnodes * 5 / 10) {
		struct thread *td = curthread;
		if (td->td_lwp)
			atomic_set_int(&td->td_lwp->lwp_mpflags, LWP_MP_VNLRU);
	}

	/*
	 * lktimeout only applies when LK_TIMELOCK is used, and only
	 * the pageout daemon uses it.  The timeout may not be zero
	 * or the pageout daemon can deadlock in low-VM situations.
	 */
	if (lktimeout == 0)
		lktimeout = hz / 10;

	vp = kmalloc(sizeof(*vp), M_VNODE, M_ZERO | M_WAITOK);

	lwkt_token_init(&vp->v_token, "vnode");
	lockinit(&vp->v_lock, "vnode", lktimeout, lkflags);
	TAILQ_INIT(&vp->v_namecache);
	RB_INIT(&vp->v_rbclean_tree);
	RB_INIT(&vp->v_rbdirty_tree);
	RB_INIT(&vp->v_rbhash_tree);
	spin_init(&vp->v_spin, "allocvnode");

	lockmgr(&vp->v_lock, LK_EXCLUSIVE);
	atomic_add_int(&numvnodes, 1);
	vp->v_refcnt = 1;
	vp->v_flag = VAGE0 | VAGE1;

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
 * This function is called just prior to a return to userland if the
 * process at some point had to allocate a new vnode during the last
 * system call and the vnode count was found to be excessive.
 *
 * This is a synchronous path that we do not normally want to execute.
 *
 * Flagged at >= 11/10's, runs if >= 10/10, vnlru runs at 9/10.
 *
 * WARNING: Sometimes numvnodes can blow out due to children being
 *	    present under directory vnodes in the namecache.  For the
 *	    moment use an if() instead of a while() and note that if
 *	    we were to use a while() we would still have to break out
 *	    if freesomevnodes() returned 0.  vnlru will also be trying
 *	    hard to free vnodes at the same time (with a lower trigger
 *	    pointer).
 */
void
allocvnode_gc(void)
{
	if (numvnodes >= desiredvnodes &&
	    cachedvnodes + inactivevnodes >= desiredvnodes * 5 / 10) {
		freesomevnodes(batchfreevnodes);
	}
}

int
freesomevnodes(int n)
{
	struct vnode *vp;
	int count = 0;

	while (n) {
		if ((vp = cleanfreevnode(n)) == NULL)
			break;
		vx_unlock(vp);
		--n;
		++count;
		kfree(vp, M_VNODE);
		atomic_add_int(&numvnodes, -1);
	}
	return(count);
}
