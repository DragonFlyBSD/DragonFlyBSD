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
	.mag_capacity =	256,
	.flags =	SRC_MANAGEDINIT,
	.ctor =		vnode_ctor,
	.dtor =		vnode_dtor,
	.ops = {
		.terminate = (sysref_terminate_func_t)vnode_terminate
	}
};

static TAILQ_HEAD(freelst, vnode) vnode_free_list;	/* vnode free list */

int  freevnodes = 0;
SYSCTL_INT(_debug, OID_AUTO, freevnodes, CTLFLAG_RD,
		&freevnodes, 0, "");
static int wantfreevnodes = 25;
SYSCTL_INT(_debug, OID_AUTO, wantfreevnodes, CTLFLAG_RW,
		&wantfreevnodes, 0, "");

/*
 * Called from vfsinit()
 */
void
vfs_lock_init(void)
{
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

static __inline
void
__vfreetail(struct vnode *vp)
{
	TAILQ_INSERT_TAIL(&vnode_free_list, vp, v_freelist);
	freevnodes++;
	vp->v_flag |= VFREE;
}

/*
 * Return a C boolean if we should put the vnode on the freelist (VFREE),
 * or leave it / mark it as VCACHED.
 *
 * This routine is only valid if the vnode is already either VFREE or
 * VCACHED, or if it can become VFREE or VCACHED via vnode_terminate().
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
 * from being deactivated, reclaimed, or placed on the free list.
 *
 * An auxiliary reference DOES prevent the vnode from being destroyed,
 * allowing you to vx_lock() it, test state, etc.
 *
 * An auxiliary reference DOES NOT move a vnode out of the VFREE state
 * once it has entered it.
 */
void
vhold(struct vnode *vp)
{
	KKASSERT(vp->v_sysref.refcnt != 0);
	atomic_add_int(&vp->v_auxrefs, 1);
}

/*
 * Remove an auxiliary reference from the vnode.
 *
 * vdrop needs to check for a VCACHE->VFREE transition to catch cases
 * where a vnode is held past its reclamation.
 */
void
vdrop(struct vnode *vp)
{
	KKASSERT(vp->v_sysref.refcnt != 0 && vp->v_auxrefs > 0);
	atomic_subtract_int(&vp->v_auxrefs, 1);
	if ((vp->v_flag & VCACHED) && vshouldfree(vp)) {
		/*vp->v_flag |= VAGE;*/
		vp->v_flag &= ~VCACHED;
		__vfree(vp);
	}
}

/*
 * This function is called when the last active reference on the vnode
 * is released, typically via vrele().  SYSREF will give the vnode a
 * negative ref count, indicating that it is undergoing termination or
 * is being set aside for the cache, and one final sysref_put() is
 * required to actually return it to the memory subsystem.
 *
 * However, because vnodes may have auxiliary structural references via
 * v_auxrefs, we must interlock auxiliary references against termination
 * via the VX lock mechanism.  It is possible for a vnode to be reactivated
 * while we were blocked on the lock.
 */
void
vnode_terminate(struct vnode *vp)
{
	vx_lock(vp);
	if (sysref_isinactive(&vp->v_sysref)) {
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
		 * how?
		 *
		 * NOTE: The vnode may be marked inactive with dirty buffers
		 * or dirty pages in its cached VM object still present.
		 */
		if ((vp->v_flag & VINACTIVE) == 0) {
			vp->v_flag |= VINACTIVE;
			VOP_INACTIVE(vp);
		}
		KKASSERT((vp->v_flag & (VFREE|VCACHED)) == 0);
		if (vshouldfree(vp))
			__vfree(vp);
		else
			vp->v_flag |= VCACHED;	/* inactive but not yet free */
		vx_unlock(vp);
	} else {
		/*
		 * Someone reactivated the vnode while were blocked on the
		 * VX lock.  Release the VX lock and release the (now active)
		 * last reference which is no longer last.
		 */
		vx_unlock(vp);
		vrele(vp);
	}
}

/*
 * Physical vnode constructor / destructor.  These are only executed on
 * the backend of the objcache.  They are NOT executed on every vnode
 * allocation or deallocation.
 */
boolean_t
vnode_ctor(void *obj, void *private, int ocflags)
{
	struct vnode *vp = obj;

	lwkt_token_init(&vp->v_pollinfo.vpi_token);
	lockinit(&vp->v_lock, "vnode", 0, 0);
	ccms_dataspace_init(&vp->v_ccms);
	TAILQ_INIT(&vp->v_namecache);
	RB_INIT(&vp->v_rbclean_tree);
	RB_INIT(&vp->v_rbdirty_tree);
	RB_INIT(&vp->v_rbhash_tree);
	return(TRUE);
}

void
vnode_dtor(void *obj, void *private)
{
	struct vnode *vp = obj;

	ccms_dataspace_destroy(&vp->v_ccms);
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

static int
vx_lock_nonblock(struct vnode *vp)
{
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
 * These functions must be used when accessing a vnode via an auxiliary
 * reference such as the namecache or free list, or when you wish to
 * do a combo ref+lock sequence.
 *
 * These functions are MANDATORY for any code chain accessing a vnode
 * whos activation state is not known.
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
		 */
		if (vp->v_flag & VFREE) {
			__vbusy(vp);
			sysref_put(&vp->v_sysref);
			sysref_activate(&vp->v_sysref);
		} else if (vp->v_flag & VCACHED) {
			vp->v_flag &= ~VCACHED;
			sysref_put(&vp->v_sysref);
			sysref_activate(&vp->v_sysref);
		} else {
			KKASSERT(sysref_isactive(&vp->v_sysref));
		}
		vp->v_flag &= ~VINACTIVE;
		error = 0;
	}
	return(error);
}

void
vput(struct vnode *vp)
{
	vn_unlock(vp);
	vrele(vp);
}

/*
 * XXX The vx_*() locks should use auxrefs, not the main reference counter.
 */
void
vx_get(struct vnode *vp)
{
	sysref_get(&vp->v_sysref);
	lockmgr(&vp->v_lock, LK_EXCLUSIVE);
}

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
 * vx_put needs to check for a VCACHE->VFREE transition to catch the
 * case where e.g. vnlru issues a vgone*().
 */
void
vx_put(struct vnode *vp)
{
	if ((vp->v_flag & VCACHED) && vshouldfree(vp)) {
		/*vp->v_flag |= VAGE;*/
		vp->v_flag &= ~VCACHED;
		__vfree(vp);
	}
	lockmgr(&vp->v_lock, LK_RELEASE);
	sysref_put(&vp->v_sysref);
}

/*
 * Misc functions
 */

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
 * Try to reuse a vnode from the free list.  NOTE: The returned vnode
 * is not completely initialized.
 */
static
struct vnode *
allocfreevnode(void)
{
	struct vnode *vp;
	int count;

	for (count = 0; count < freevnodes; count++) {
		/*
		 * Note that regardless of how we block in this loop,
		 * we only get here if freevnodes != 0 so there
		 * had better be something on the list.
		 *
		 * Try to lock the first vnode on the free list.
		 * Cycle if we can't.
		 *
		 * XXX NOT MP SAFE
		 */
		vp = TAILQ_FIRST(&vnode_free_list);
		if (vx_lock_nonblock(vp)) {
			KKASSERT(vp->v_flag & VFREE);
			TAILQ_REMOVE(&vnode_free_list, vp, v_freelist);
			TAILQ_INSERT_TAIL(&vnode_free_list,
					  vp, v_freelist);
			continue;
		}

		/*
		 * With the vnode locked we can safely remove it
		 * from the free list.  We inherit the reference
		 * that was previously associated with the vnode
		 * being on the free list.
		 */
		KKASSERT((vp->v_flag & (VFREE|VINACTIVE)) ==
			  (VFREE|VINACTIVE));
		KKASSERT(sysref_isinactive(&vp->v_sysref));
		__vbusy(vp);

		/*
		 * Holding the VX lock on an inactive vnode prevents it
		 * from being reactivated or reused.  New namecache
		 * associations can only be made using active vnodes.
		 *
		 * Another thread may be blocked on our vnode lock while
		 * holding a namecache lock.  We can only reuse this vnode
		 * if we can clear all namecache associations without
		 * blocking.
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
	if (vp->v_track_read.bk_active + vp->v_track_write.bk_active)
		panic("Clean vnode has pending I/O's");
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
	vp->v_flag = 0;
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
	KKASSERT(TAILQ_FIRST(&vp->v_namecache) == NULL);
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

