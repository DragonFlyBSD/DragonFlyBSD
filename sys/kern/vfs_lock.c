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
 * $DragonFly: src/sys/kern/vfs_lock.c,v 1.5 2005/01/19 18:57:00 dillon Exp $
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
 */
static __inline 
void
__vbusy(struct vnode *vp)
{
	KKASSERT(vp->v_flag & VFREE);
	TAILQ_REMOVE(&vnode_free_list, vp, v_freelist);
	freevnodes--;
	vp->v_flag &= ~(VFREE|VAGE);
}

static __inline
void
__vfree(struct vnode *vp)
{
	KKASSERT((vp->v_flag & VFREE) == 0);
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
	if (vp->v_holdcnt != 0 || vp->v_usecount != usecount)
		return (0);		/* other holderse */
	if (vp->v_object &&
	    (vp->v_object->ref_count || vp->v_object->resident_page_count)) {
		return (0);
	}
	return (1);
}

/*
 * Reference a vnode or release the reference on a vnode.  The vnode will
 * be taken off the freelist if it is on it and cannot be recycled or 
 * deactivated while refd.  The last release of a vnode will deactivate the
 * vnode via VOP_INACTIVE().
 *
 * Special cases: refing a vnode does not clear VINACTIVE, you have to vget()
 * the vnode shared or exclusive to do that.
 */
static __inline
void
__vref(struct vnode *vp)
{
	++vp->v_usecount;
	if (vp->v_flag & VFREE)
		__vbusy(vp);
}

void
vref(struct vnode *vp)
{
	crit_enter();
	__vref(vp);
	crit_exit();
}

void
vrele(struct vnode *vp)
{
	thread_t td = curthread;

	crit_enter();
	if (vp->v_usecount == 1) {
		KASSERT(lockcountnb(&vp->v_lock) == 0, ("last vrele vp %p still locked", vp));

		/*
		 * Deactivation requires an exclusive v_lock (vx_lock()), and
		 * only occurs if the usecount is still 1 after locking.
		 */
		if ((vp->v_flag & VINACTIVE) == 0) {
			if (vx_lock(vp) == 0) {
				if ((vp->v_flag & VINACTIVE) == 0 &&
				    vp->v_usecount == 1) {
					vp->v_flag |= VINACTIVE;
					VOP_INACTIVE(vp, td);
				}
				vx_unlock(vp);
			}
		}
		if (vshouldfree(vp, 1))
			__vfree(vp);
	} else {
		KKASSERT(vp->v_usecount > 0);
	}
	--vp->v_usecount;
	crit_exit();
}

/*
 * Hold a vnode or drop the hold on a vnode.  The vnode will be taken off
 * the freelist if it is on it and cannot be recycled.  However, the
 * vnode can be deactivated and reactivated while held.
 *
 * Special cases: The last drop of a vnode does nothing special, allowing it
 * to be called from an interrupt.  vrele() on the otherhand cannot be called
 * from an interrupt.
 */
void
vhold(struct vnode *vp)
{
	crit_enter();
	++vp->v_holdcnt;
	if (vp->v_flag & VFREE)
		__vbusy(vp);
	crit_exit();
}

void
vdrop(struct vnode *vp)
{
	crit_enter();
	if (vp->v_holdcnt == 1) {
		--vp->v_holdcnt;
		if (vshouldfree(vp, 0))
			__vfree(vp);
	} else {
		--vp->v_holdcnt;
		KKASSERT(vp->v_holdcnt > 0);
	}
	crit_exit();
}

/****************************************************************
 *			VX LOCKING FUNCTIONS			*
 ****************************************************************
 *
 * These functions lock vnodes for reclamation and deactivation ops.
 * Only vp->v_lock, the top layer of the VFS, is locked.  You must be
 * holding a normal reference in order to be able to safely call vx_lock()
 * and vx_unlock().  vx_get() and vx_put() are combination functions which
 * vref+vx_lock and vrele+vx_unlock.
 */

#define VXLOCKFLAGS	(LK_EXCLUSIVE|LK_RETRY)
#define VXLOCKFLAGS_NB	(LK_EXCLUSIVE|LK_NOWAIT)

static int
__vxlock(struct vnode *vp, int flags)
{
	return(lockmgr(&vp->v_lock, flags, NULL, curthread));
}

static void
__vxunlock(struct vnode *vp)
{
	lockmgr(&vp->v_lock, LK_RELEASE, NULL, curthread);
}

int
vx_lock(struct vnode *vp)
{
	return(__vxlock(vp, VXLOCKFLAGS));
}

void
vx_unlock(struct vnode *vp)
{
	__vxunlock(vp);
}

int
vx_get(struct vnode *vp)
{
	int error;

	vref(vp);
	if ((error = __vxlock(vp, VXLOCKFLAGS)) != 0)
		vrele(vp);
	return(error);
}

int
vx_get_nonblock(struct vnode *vp)
{
	int error;

	vref(vp);
	if ((error = __vxlock(vp, VXLOCKFLAGS_NB)) != 0)
		vrele(vp);
	return(error);
}

void
vx_put(struct vnode *vp)
{
	__vxunlock(vp);
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
vget(struct vnode *vp, int flags, thread_t td)
{
	int error;

	crit_enter();
	__vref(vp);
	if (flags & LK_TYPE_MASK) {
		if ((error = vn_lock(vp, flags, td)) != 0) {
			vrele(vp);
		} else if (vp->v_flag & VRECLAIMED) {
			VOP_UNLOCK(vp, 0, td);
			vrele(vp);
			error = ENOENT;
		} else {
			vp->v_flag &= ~VINACTIVE;
			error = 0;
		}
	} else {
		panic("vget() called with no lock specified!");
		error = ENOENT;	/* not reached, compiler opt */
	}
	crit_exit();
	return(error);
}

void
vput(struct vnode *vp)
{
	VOP_UNLOCK(vp, 0, curthread);
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

			/*
			 * Note the lack of a critical section.  We vx_get()
			 * the vnode before we check it for validity, reducing
			 * the number of checks we have to make.  The vx_get()
			 * will pull it off the freelist.
			 */
			if (vx_get(vp)) {
				vp = NULL;
				continue;
			}

			/*
			 * Can this vnode be recycled?  It must be in a
			 * VINACTIVE state with only our reference to it.
			 * (vx_get(), unlike vget(), does not reactivate
			 * the vnode).  vx_put() will recycle it onto the
			 * end of the freelist.
			 */
			if ((vp->v_flag & VINACTIVE) == 0 ||
			    vp->v_holdcnt || vp->v_usecount != 1) {
				vx_put(vp);
				vp = NULL;
				continue;
			}

			/*
			 * Ok, we can reclaim the vnode if it isn't already
			 * in a reclaimed state.  If the reclamation fails,
			 * or if someone else is referencing the vnode after
			 * we have vgone()'d it, we recycle the vnode on the
			 * freelist or hold it (by calling vx_put()).
			 */
			if ((vp->v_flag & VRECLAIMED) == 0) {
				vgone(vp);
				if ((vp->v_flag & VRECLAIMED) == 0 ||
				    vp->v_holdcnt || vp->v_usecount != 1) {
					vx_put(vp);
					vp = NULL;
					continue;
				}
			}
			KKASSERT(vp->v_flag & VINACTIVE);

			/*
			 * We have a vnode!
			 */
			break;
		}
	}

	/*
	 * If we have a vp it will be refd and VX locked.
	 */
	if (vp) {
		vp->v_lease = NULL;

#ifdef INVARIANTS
		if (vp->v_data)
			panic("cleaned vnode isn't");
		if (vp->v_numoutput)
			panic("Clean vnode has pending I/O's");
		KKASSERT(vp->v_mount == NULL);
#endif
		vp->v_flag = 0;
		vp->v_lastw = 0;
		vp->v_lasta = 0;
		vp->v_cstart = 0;
		vp->v_clen = 0;
		vp->v_socket = 0;
		vp->v_writecount = 0;	/* XXX */
		lockreinit(&vp->v_lock, 0, "vnode", lktimeout, lkflags);
		KKASSERT(TAILQ_FIRST(&vp->v_namecache) == NULL);
	} else {
		/*
		 * A brand-new vnode (we could use malloc() here I think) XXX
		 */
		vp = malloc(sizeof(struct vnode), M_VNODE, M_WAITOK|M_ZERO);
		lwkt_token_init(&vp->v_pollinfo.vpi_token);
		lockinit(&vp->v_lock, 0, "vnode", lktimeout, lkflags);
		TAILQ_INIT(&vp->v_namecache);

		/*
		 * short cut around vfreeing it and looping, just set it up
		 * as if we had pulled a reclaimed vnode off the freelist
		 * and reinitialized it.
		 */
		vp->v_usecount = 1;
		if (__vxlock(vp, VXLOCKFLAGS))
			panic("getnewvnode: __vxlock failed");
		numvnodes++;
	}

	TAILQ_INIT(&vp->v_cleanblkhd);
	TAILQ_INIT(&vp->v_dirtyblkhd);
	vp->v_type = VNON;
	vp->v_tag = 0;
	vp->v_ops = NULL;
	vp->v_data = NULL;
	KKASSERT(vp->v_mount == NULL);
	return (vp);
}

struct vnode *
allocvnode_placemarker(void)
{
 	struct vnode *pvp;

	pvp = malloc(sizeof(struct vnode), 
			M_VNODE, M_WAITOK|M_USE_RESERVE|M_ZERO);
	pvp->v_flag |= VPLACEMARKER;
	return(pvp);
}

void
freevnode_placemarker(struct vnode *pvp)
{
	KKASSERT(pvp->v_flag & VPLACEMARKER);
	free(pvp, M_VNODE);
}

