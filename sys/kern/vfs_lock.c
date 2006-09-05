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
 * $DragonFly: src/sys/kern/vfs_lock.c,v 1.24 2006/09/05 00:55:45 dillon Exp $
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


static MALLOC_DEFINE(M_VNODE, "vnodes", "vnode structures");

static TAILQ_HEAD(freelst, vnode) vnode_free_list;	/* vnode free list */

int  freevnodes = 0;
SYSCTL_INT(_debug, OID_AUTO, freevnodes, CTLFLAG_RD,
		&freevnodes, 0, "");
static int wantfreevnodes = 25;
SYSCTL_INT(_debug, OID_AUTO, wantfreevnodes, CTLFLAG_RW,
		&wantfreevnodes, 0, "");
static int minvnodes;
SYSCTL_INT(_kern, OID_AUTO, minvnodes, CTLFLAG_RW,
		&minvnodes, 0, "Minimum number of vnodes");

/*
 * Called from vfsinit()
 */
void
vfs_lock_init(void)
{
	minvnodes = desiredvnodes / 4;

	TAILQ_INIT(&vnode_free_list);
}

/*
 * Inline helper functions.  vbusy() and vfree() must be called while in a
 * critical section.
 *
 * Warning: must be callable if the caller holds a read spinlock to something
 * else, meaning we can't use read spinlocks here.
 */
static __inline 
void
__vbusy(struct vnode *vp)
{
	TAILQ_REMOVE(&vnode_free_list, vp, v_freelist);
	freevnodes--;
	vp->v_flag &= ~(VFREE|VAGE);
}

static __inline
void
__vfree(struct vnode *vp)
{
	if (vp->v_flag & (VAGE|VRECLAIMED))
		TAILQ_INSERT_HEAD(&vnode_free_list, vp, v_freelist);
	else
		TAILQ_INSERT_TAIL(&vnode_free_list, vp, v_freelist);
	freevnodes++;
	vp->v_flag &= ~VAGE;
	vp->v_flag |= VFREE;
}

/*
 * Return 1 if we can immediately place the vnode on the freelist.
 */
static __inline int
vshouldfree(struct vnode *vp, int usecount)
{
	if (vp->v_flag & VFREE)
		return (0);		/* already free */
	if (vp->v_holdcnt != 0 || vp->v_usecount != usecount)
		return (0);		/* other holderse */
	if (vp->v_object &&
	    (vp->v_object->ref_count || vp->v_object->resident_page_count)) {
		return (0);
	}
	return (1);
}

/*
 * Add another ref to a vnode.  The vnode must already have at least one
 * ref.
 *
 * NOTE: The vnode may continue to reside on the free list
 */
void
vref(struct vnode *vp)
{
	KKASSERT(vp->v_usecount > 0 && (vp->v_flag & VINACTIVE) == 0);
	atomic_add_int(&vp->v_usecount, 1);
}

/*
 * Add a ref to a vnode which may not have any refs.  This routine is called
 * from the namecache and vx_get().  If requested, the vnode will be
 * reactivated.
 *
 * Removal of the vnode from the free list is optional.  Since most vnodes
 * are temporary in nature we opt not do it.  This also means we don't have
 * to deal with lock ordering issues between the freelist and vnode
 * spinlocks.
 *
 * We must acquire the vnode's spinlock to interlock against vrele().
 *
 * vget(), cache_vget(), and cache_vref() reactives vnodes.  vx_get() does
 * not.
 */
void
vref_initial(struct vnode *vp, int reactivate)
{
	spin_lock_wr(&vp->v_spinlock);
	atomic_add_int(&vp->v_usecount, 1);
	if (reactivate)
		vp->v_flag &= ~VINACTIVE;
	spin_unlock_wr(&vp->v_spinlock);
}

/*
 * Release a ref on the vnode.  Since 0->1 transitions can only be made
 * by vref_initial(), 1->0 transitions will be protected by the spinlock.
 *
 * When handling a 1->0 transition the vnode is guarenteed to not be locked
 * and we can set the exclusive lock atomically while interlocked with our
 * spinlock.  A panic will occur if the lock is held.
 */
void
vrele(struct vnode *vp)
{
	spin_lock_wr(&vp->v_spinlock);
	if (vp->v_usecount > 1) {
		atomic_subtract_int(&vp->v_usecount, 1);
		spin_unlock_wr(&vp->v_spinlock);
		return;
	}
	KKASSERT(vp->v_usecount == 1);

	/*
	 * This is roughly equivalent to obtaining an exclusive
	 * lock, but the spinlock is already held (and remains held
	 * on return) and the lock must be obtainable without 
	 * blocking, which it is in a 1->0 transition.
	 */
	lockmgr_setexclusive_interlocked(&vp->v_lock);

	/*
	 * VINACTIVE is interlocked by the spinlock, so we have to re-check
	 * the bit if we release and reacquire the spinlock even though
	 * we are holding the exclusive lockmgr lock throughout.
	 *
	 * VOP_INACTIVE can race other VOPs even though we hold an exclusive
	 * lock.  This is ok.  The ref count of 1 must remain intact through
	 * the VOP_INACTIVE call to avoid a recursion.
	 */
	while ((vp->v_flag & VINACTIVE) == 0 && vp->v_usecount == 1) {
		vp->v_flag |= VINACTIVE;
		spin_unlock_wr(&vp->v_spinlock);
		VOP_INACTIVE(vp);
		spin_lock_wr(&vp->v_spinlock);
	}

	/*
	 * NOTE: v_usecount might no longer be 1
	 */
	atomic_subtract_int(&vp->v_usecount, 1);
	if (vshouldfree(vp, 0))
		__vfree(vp);
	lockmgr_clrexclusive_interlocked(&vp->v_lock);
	/* spinlock unlocked */
}

/*
 * Hold a vnode, preventing it from being recycled (unless it is already
 * undergoing a recyclement or already has been recycled).
 *
 * Opting not to remove a vnode from the freelist simply means that
 * allocvnode must do it for us if it finds an unsuitable vnode.
 */
void
vhold(struct vnode *vp)
{
	spin_lock_wr(&vp->v_spinlock);
	atomic_add_int(&vp->v_holdcnt, 1);
	spin_unlock_wr(&vp->v_spinlock);
}

/*
 * Like vrele(), we must atomically place the vnode on the free list if
 * it becomes suitable.  vhold()/vdrop() do not mess with VINACTIVE.
 */
void
vdrop(struct vnode *vp)
{
	KKASSERT(vp->v_holdcnt > 0);
	spin_lock_wr(&vp->v_spinlock);
	atomic_subtract_int(&vp->v_holdcnt, 1);
	if (vshouldfree(vp, 0))
		__vfree(vp);
	spin_unlock_wr(&vp->v_spinlock);
}

/****************************************************************
 *			VX LOCKING FUNCTIONS			*
 ****************************************************************
 *
 * These functions lock vnodes for reclamation and deactivation related
 * activities.  Only vp->v_lock, the top layer of the VFS, is locked.
 * You must be holding a normal reference in order to be able to safely
 * call vx_lock() and vx_unlock().
 *
 * vx_get() also differs from vget() in that it does not clear the
 * VINACTIVE bit on a vnode.
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

void
vx_get(struct vnode *vp)
{
	vref_initial(vp, 0);
	lockmgr(&vp->v_lock, LK_EXCLUSIVE);
}

int
vx_get_nonblock(struct vnode *vp)
{
	int error;

	vref_initial(vp, 0);
	error = lockmgr(&vp->v_lock, LK_EXCLUSIVE | LK_NOWAIT);
	if (error)
		vrele(vp);
	return(error);
}

void
vx_put(struct vnode *vp)
{
	lockmgr(&vp->v_lock, LK_RELEASE);
	vrele(vp);
}

/****************************************************************
 *			VNODE ACQUISITION FUNCTIONS		*
 ****************************************************************
 *
 * vget() and vput() access a vnode for the intent of executing an
 * operation other then a reclamation or deactivation.  vget() will ref
 * and lock the vnode, vput() will unlock and deref the vnode.  
 * The VOP_*() locking functions are used.
 *
 * CALLING VGET IS MANDATORY PRIOR TO ANY MODIFYING OPERATION ON A VNODE.
 * This is because vget handles the VINACTIVE interlock and is responsible
 * for clearing the bit.  If the bit is not cleared inode updates may not
 * make it to disk.
 *
 * Special cases: If vget()'s locking operation fails the vrele() call may
 * cause the vnode to be deactivated (VOP_INACTIVE called).  However, this
 * never occurs if the vnode is in a reclaimed state.  Vnodes in reclaimed
 * states always return an error code of ENOENT.
 *
 * Special cases: vput() will unlock and, if it is the last reference, 
 * deactivate the vnode.  The deactivation uses a separate non-layered
 * VX lock after the normal unlock.  XXX make it more efficient.
 */
int
vget(struct vnode *vp, int flags)
{
	int error;

	vref_initial(vp, 0);
	if (flags & LK_TYPE_MASK) {
		if ((error = vn_lock(vp, flags)) != 0) {
			vrele(vp);
		} else if (vp->v_flag & VRECLAIMED) {
			vn_unlock(vp);
			vrele(vp);
			error = ENOENT;
		} else {
			vp->v_flag &= ~VINACTIVE;	/* XXX not MP safe */
			error = 0;
		}
	} else {
		panic("vget() called with no lock specified!");
		error = ENOENT;	/* not reached, compiler opt */
	}
	return(error);
}

void
vput(struct vnode *vp)
{
	vn_unlock(vp);
	vrele(vp);
}

void
vsetflags(struct vnode *vp, int flags)
{
	crit_enter();
	vp->v_flag |= flags;
	crit_exit();
}

void
vclrflags(struct vnode *vp, int flags)
{
	crit_enter();
	vp->v_flag &= ~flags;
	crit_exit();
}

/*
 * Obtain a new vnode from the freelist, allocating more if necessary.
 * The returned vnode is VX locked & refd.
 */
struct vnode *
allocvnode(int lktimeout, int lkflags)
{
	struct thread *td;
	struct vnode *vp;

	/*
	 * Try to reuse vnodes if we hit the max.  This situation only
	 * occurs in certain large-memory (2G+) situations.  We cannot
	 * attempt to directly reclaim vnodes due to nasty recursion
	 * problems.
	 */
	while (numvnodes - freevnodes > desiredvnodes)
		vnlru_proc_wait();

	td = curthread;
	vp = NULL;

	/*
	 * Attempt to reuse a vnode already on the free list, allocating
	 * a new vnode if we can't find one or if we have not reached a
	 * good minimum for good LRU performance.
	 */
	if (freevnodes >= wantfreevnodes && numvnodes >= minvnodes) {
		int count;

		for (count = 0; count < freevnodes; count++) {
			/*
			 * __VNODESCAN__
			 *
			 * Pull the next vnode off the free list and do some
			 * sanity checks.  Note that regardless of how we
			 * block, if freevnodes is non-zero there had better
			 * be something on the list.
			 */
			vp = TAILQ_FIRST(&vnode_free_list);
			if (vp == NULL)
				panic("getnewvnode: free vnode isn't");

			/* XXX for now */
			KKASSERT(vp->v_flag & VFREE);

			/*
			 * Handle the case where the vnode was pulled off
			 * the free list while we were waiting for the
			 * spinlock.
			 */
			spin_lock_wr(&vp->v_spinlock);
			if ((vp->v_flag & VFREE) == 0) {
				spin_unlock_wr(&vp->v_spinlock);
				vp = NULL;
				continue;
			}

			/*
			 * Lazy removal of the vnode from the freelist if
			 * the vnode has references.
			 */
			if (vp->v_usecount || vp->v_holdcnt) {
				__vbusy(vp);
				spin_unlock_wr(&vp->v_spinlock);
				vp = NULL;
				continue;
			}

			/*
			 * vx_get() equivalent, but atomic with the
			 * spinlock held.  Since 0->1 transitions and the
			 * lockmgr are protected by the spinlock we must
			 * be able to get an exclusive lock without blocking
			 * here.
			 *
			 * Also take the vnode off of the free list and
			 * assert that it is inactive.
			 */
			vp->v_usecount = 1;
			lockmgr_setexclusive_interlocked(&vp->v_lock);
			__vbusy(vp);
			KKASSERT(vp->v_flag & VINACTIVE);

			/*
			 * Reclaim the vnode.  VRECLAIMED will be set
			 * atomically before the spinlock is released
			 * by vgone_interlocked().
			 */
			if ((vp->v_flag & VRECLAIMED) == 0) {
				vgone_interlocked(vp);
				/* spinlock unlocked */
			} else {
				spin_unlock_wr(&vp->v_spinlock);
			}

			/*
			 * We reclaimed the vnode but other claimants may
			 * have referenced it while we were blocked.  We
			 * cannot reuse a vnode until all refs are gone and
			 * the vnode has completed reclamation.
			 */
			KKASSERT(vp->v_flag & VRECLAIMED);
			if (vp->v_usecount != 1 || vp->v_holdcnt) {
				vx_put(vp);
				vp = NULL;
				continue;
			}
			    
			/*
			 * There are no more structural references to the
			 * vnode, referenced or otherwise.  We have a vnode!
			 *
			 * The vnode may have been placed on the free list
			 * while we were blocked.
			 */
			if (vp->v_flag & VFREE)
				__vbusy(vp);
			KKASSERT(vp->v_flag & VINACTIVE);
			break;
		}
	}

	/*
	 * If we have a vp it will be refd and VX locked.
	 */
	if (vp) {
#ifdef INVARIANTS
		if (vp->v_data)
			panic("cleaned vnode isn't");
		if (vp->v_track_read.bk_active + vp->v_track_write.bk_active)
			panic("Clean vnode has pending I/O's");
		KKASSERT(vp->v_mount == NULL);
#endif
		vp->v_flag = 0;
		vp->v_lastw = 0;
		vp->v_lasta = 0;
		vp->v_cstart = 0;
		vp->v_clen = 0;
		vp->v_socket = 0;
		vp->v_opencount = 0;
		vp->v_writecount = 0;	/* XXX */
		lockreinit(&vp->v_lock, "vnode", lktimeout, lkflags);
		KKASSERT(TAILQ_FIRST(&vp->v_namecache) == NULL);
	} else {
		/*
		 * A brand-new vnode (we could use malloc() here I think) XXX
		 */
		vp = kmalloc(sizeof(struct vnode), M_VNODE, M_WAITOK|M_ZERO);
		lwkt_token_init(&vp->v_pollinfo.vpi_token);
		lockinit(&vp->v_lock, "vnode", lktimeout, lkflags);
		ccms_dataspace_init(&vp->v_ccms);
		TAILQ_INIT(&vp->v_namecache);

		/*
		 * short cut around vfreeing it and looping, just set it up
		 * as if we had pulled a reclaimed vnode off the freelist
		 * and reinitialized it.
		 */
		vp->v_usecount = 1;
		lockmgr(&vp->v_lock, LK_EXCLUSIVE);
		numvnodes++;
	}

	RB_INIT(&vp->v_rbclean_tree);
	RB_INIT(&vp->v_rbdirty_tree);
	RB_INIT(&vp->v_rbhash_tree);
	vp->v_filesize = NOOFFSET;
	vp->v_type = VNON;
	vp->v_tag = 0;
	vp->v_ops = NULL;
	vp->v_data = NULL;
	KKASSERT(vp->v_mount == NULL);
	return (vp);
}

