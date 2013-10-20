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
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * External virtual filesystem routines
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mount.h>
#include <sys/proc.h>
#include <sys/vnode.h>
#include <sys/buf.h>
#include <sys/eventhandler.h>
#include <sys/kthread.h>
#include <sys/sysctl.h>

#include <machine/limits.h>

#include <sys/buf2.h>
#include <sys/thread2.h>
#include <sys/sysref2.h>

#include <vm/vm.h>
#include <vm/vm_object.h>

struct mountscan_info {
	TAILQ_ENTRY(mountscan_info) msi_entry;
	int msi_how;
	struct mount *msi_node;
};

struct vmntvnodescan_info {
	TAILQ_ENTRY(vmntvnodescan_info) entry;
	struct vnode *vp;
};

struct vnlru_info {
	int	pass;
};

static int vnlru_nowhere = 0;
SYSCTL_INT(_debug, OID_AUTO, vnlru_nowhere, CTLFLAG_RD,
	    &vnlru_nowhere, 0,
	    "Number of times the vnlru process ran without success");


static struct lwkt_token mntid_token;
static struct mount dummymount;

/* note: mountlist exported to pstat */
struct mntlist mountlist = TAILQ_HEAD_INITIALIZER(mountlist);
static TAILQ_HEAD(,mountscan_info) mountscan_list;
static struct lwkt_token mountlist_token;

static TAILQ_HEAD(,bio_ops) bio_ops_list = TAILQ_HEAD_INITIALIZER(bio_ops_list);

/*
 * Called from vfsinit()
 */
void
vfs_mount_init(void)
{
	lwkt_token_init(&mountlist_token, "mntlist");
	lwkt_token_init(&mntid_token, "mntid");
	TAILQ_INIT(&mountscan_list);
	mount_init(&dummymount);
	dummymount.mnt_flag |= MNT_RDONLY;
	dummymount.mnt_kern_flag |= MNTK_ALL_MPSAFE;
}

/*
 * Support function called to remove a vnode from the mountlist and
 * deal with side effects for scans in progress.
 *
 * Target mnt_token is held on call.
 */
static void
vremovevnodemnt(struct vnode *vp)
{
        struct vmntvnodescan_info *info;
	struct mount *mp = vp->v_mount;

	TAILQ_FOREACH(info, &mp->mnt_vnodescan_list, entry) {
		if (info->vp == vp)
			info->vp = TAILQ_NEXT(vp, v_nmntvnodes);
	}
	TAILQ_REMOVE(&vp->v_mount->mnt_nvnodelist, vp, v_nmntvnodes);
}

/*
 * Allocate a new vnode and associate it with a tag, mount point, and
 * operations vector.
 *
 * A VX locked and refd vnode is returned.  The caller should setup the
 * remaining fields and vx_put() or, if he wishes to leave a vref,
 * vx_unlock() the vnode.
 */
int
getnewvnode(enum vtagtype tag, struct mount *mp,
		struct vnode **vpp, int lktimeout, int lkflags)
{
	struct vnode *vp;

	KKASSERT(mp != NULL);

	vp = allocvnode(lktimeout, lkflags);
	vp->v_tag = tag;
	vp->v_data = NULL;

	/*
	 * By default the vnode is assigned the mount point's normal
	 * operations vector.
	 */
	vp->v_ops = &mp->mnt_vn_use_ops;

	/*
	 * Placing the vnode on the mount point's queue makes it visible.
	 * VNON prevents it from being messed with, however.
	 */
	insmntque(vp, mp);

	/*
	 * A VX locked & refd vnode is returned.
	 */
	*vpp = vp;
	return (0);
}

/*
 * This function creates vnodes with special operations vectors.  The
 * mount point is optional.
 *
 * This routine is being phased out but is still used by vfs_conf to
 * create vnodes for devices prior to the root mount (with mp == NULL).
 */
int
getspecialvnode(enum vtagtype tag, struct mount *mp,
		struct vop_ops **ops,
		struct vnode **vpp, int lktimeout, int lkflags)
{
	struct vnode *vp;

	vp = allocvnode(lktimeout, lkflags);
	vp->v_tag = tag;
	vp->v_data = NULL;
	vp->v_ops = ops;

	if (mp == NULL)
		mp = &dummymount;

	/*
	 * Placing the vnode on the mount point's queue makes it visible.
	 * VNON prevents it from being messed with, however.
	 */
	insmntque(vp, mp);

	/*
	 * A VX locked & refd vnode is returned.
	 */
	*vpp = vp;
	return (0);
}

/*
 * Interlock against an unmount, return 0 on success, non-zero on failure.
 *
 * The passed flag may be 0 or LK_NOWAIT and is only used if an unmount
 * is in-progress.  
 *
 * If no unmount is in-progress LK_NOWAIT is ignored.  No other flag bits
 * are used.  A shared locked will be obtained and the filesystem will not
 * be unmountable until the lock is released.
 */
int
vfs_busy(struct mount *mp, int flags)
{
	int lkflags;

	atomic_add_int(&mp->mnt_refs, 1);
	lwkt_gettoken(&mp->mnt_token);
	if (mp->mnt_kern_flag & MNTK_UNMOUNT) {
		if (flags & LK_NOWAIT) {
			lwkt_reltoken(&mp->mnt_token);
			atomic_add_int(&mp->mnt_refs, -1);
			return (ENOENT);
		}
		/* XXX not MP safe */
		mp->mnt_kern_flag |= MNTK_MWAIT;
		/*
		 * Since all busy locks are shared except the exclusive
		 * lock granted when unmounting, the only place that a
		 * wakeup needs to be done is at the release of the
		 * exclusive lock at the end of dounmount.
		 */
		tsleep((caddr_t)mp, 0, "vfs_busy", 0);
		lwkt_reltoken(&mp->mnt_token);
		atomic_add_int(&mp->mnt_refs, -1);
		return (ENOENT);
	}
	lkflags = LK_SHARED;
	if (lockmgr(&mp->mnt_lock, lkflags))
		panic("vfs_busy: unexpected lock failure");
	lwkt_reltoken(&mp->mnt_token);
	return (0);
}

/*
 * Free a busy filesystem.
 *
 * Decrement refs before releasing the lock so e.g. a pending umount
 * doesn't give us an unexpected busy error.
 */
void
vfs_unbusy(struct mount *mp)
{
	atomic_add_int(&mp->mnt_refs, -1);
	lockmgr(&mp->mnt_lock, LK_RELEASE);
}

/*
 * Lookup a filesystem type, and if found allocate and initialize
 * a mount structure for it.
 *
 * Devname is usually updated by mount(8) after booting.
 */
int
vfs_rootmountalloc(char *fstypename, char *devname, struct mount **mpp)
{
	struct vfsconf *vfsp;
	struct mount *mp;

	if (fstypename == NULL)
		return (ENODEV);

	vfsp = vfsconf_find_by_name(fstypename);
	if (vfsp == NULL)
		return (ENODEV);
	mp = kmalloc(sizeof(struct mount), M_MOUNT, M_WAITOK | M_ZERO);
	mount_init(mp);
	lockinit(&mp->mnt_lock, "vfslock", VLKTIMEOUT, 0);

	vfs_busy(mp, 0);
	mp->mnt_vfc = vfsp;
	mp->mnt_op = vfsp->vfc_vfsops;
	vfsp->vfc_refcount++;
	mp->mnt_stat.f_type = vfsp->vfc_typenum;
	mp->mnt_flag |= MNT_RDONLY;
	mp->mnt_flag |= vfsp->vfc_flags & MNT_VISFLAGMASK;
	strncpy(mp->mnt_stat.f_fstypename, vfsp->vfc_name, MFSNAMELEN);
	copystr(devname, mp->mnt_stat.f_mntfromname, MNAMELEN - 1, 0);
	*mpp = mp;
	return (0);
}

/*
 * Basic mount structure initialization
 */
void
mount_init(struct mount *mp)
{
	lockinit(&mp->mnt_lock, "vfslock", hz*5, 0);
	lwkt_token_init(&mp->mnt_token, "permnt");

	TAILQ_INIT(&mp->mnt_vnodescan_list);
	TAILQ_INIT(&mp->mnt_nvnodelist);
	TAILQ_INIT(&mp->mnt_reservedvnlist);
	TAILQ_INIT(&mp->mnt_jlist);
	mp->mnt_nvnodelistsize = 0;
	mp->mnt_flag = 0;
	mp->mnt_iosize_max = MAXPHYS;
	vn_syncer_thr_create(mp);
}

/*
 * Lookup a mount point by filesystem identifier.
 */
struct mount *
vfs_getvfs(fsid_t *fsid)
{
	struct mount *mp;

	lwkt_gettoken(&mountlist_token);
	TAILQ_FOREACH(mp, &mountlist, mnt_list) {
		if (mp->mnt_stat.f_fsid.val[0] == fsid->val[0] &&
		    mp->mnt_stat.f_fsid.val[1] == fsid->val[1]) {
			break;
		}
	}
	lwkt_reltoken(&mountlist_token);
	return (mp);
}

/*
 * Get a new unique fsid.  Try to make its val[0] unique, since this value
 * will be used to create fake device numbers for stat().  Also try (but
 * not so hard) make its val[0] unique mod 2^16, since some emulators only
 * support 16-bit device numbers.  We end up with unique val[0]'s for the
 * first 2^16 calls and unique val[0]'s mod 2^16 for the first 2^8 calls.
 *
 * Keep in mind that several mounts may be running in parallel.  Starting
 * the search one past where the previous search terminated is both a
 * micro-optimization and a defense against returning the same fsid to
 * different mounts.
 */
void
vfs_getnewfsid(struct mount *mp)
{
	static u_int16_t mntid_base;
	fsid_t tfsid;
	int mtype;

	lwkt_gettoken(&mntid_token);
	mtype = mp->mnt_vfc->vfc_typenum;
	tfsid.val[1] = mtype;
	mtype = (mtype & 0xFF) << 24;
	for (;;) {
		tfsid.val[0] = makeudev(255,
		    mtype | ((mntid_base & 0xFF00) << 8) | (mntid_base & 0xFF));
		mntid_base++;
		if (vfs_getvfs(&tfsid) == NULL)
			break;
	}
	mp->mnt_stat.f_fsid.val[0] = tfsid.val[0];
	mp->mnt_stat.f_fsid.val[1] = tfsid.val[1];
	lwkt_reltoken(&mntid_token);
}

/*
 * Set the FSID for a new mount point to the template.  Adjust
 * the FSID to avoid collisions.
 */
int
vfs_setfsid(struct mount *mp, fsid_t *template)
{
	int didmunge = 0;

	bzero(&mp->mnt_stat.f_fsid, sizeof(mp->mnt_stat.f_fsid));
	for (;;) {
		if (vfs_getvfs(template) == NULL)
			break;
		didmunge = 1;
		++template->val[1];
	}
	mp->mnt_stat.f_fsid = *template;
	return(didmunge);
}

/*
 * This routine is called when we have too many vnodes.  It attempts
 * to free <count> vnodes and will potentially free vnodes that still
 * have VM backing store (VM backing store is typically the cause
 * of a vnode blowout so we want to do this).  Therefore, this operation
 * is not considered cheap.
 *
 * A number of conditions may prevent a vnode from being reclaimed.
 * the buffer cache may have references on the vnode, a directory
 * vnode may still have references due to the namei cache representing
 * underlying files, or the vnode may be in active use.   It is not
 * desireable to reuse such vnodes.  These conditions may cause the
 * number of vnodes to reach some minimum value regardless of what
 * you set kern.maxvnodes to.  Do not set kern.maxvnodes too low.
 */

/*
 * This is a quick non-blocking check to determine if the vnode is a good
 * candidate for being (eventually) vgone()'d.  Returns 0 if the vnode is
 * not a good candidate, 1 if it is.
 */
static __inline int 
vmightfree(struct vnode *vp, int page_count, int pass)
{
	if (vp->v_flag & VRECLAIMED)
		return (0);
	if (VREFCNT(vp) > 0)
		return (0);
	if (vp->v_object && vp->v_object->resident_page_count >= page_count)
		return (0);

	/*
	 * XXX horrible hack.  Up to four passes will be taken.  Each pass
	 * makes a larger set of vnodes eligible.  For now what this really
	 * means is that we try to recycle files opened only once before
	 * recycling files opened multiple times.
	 */
	switch(vp->v_flag & (VAGE0 | VAGE1)) {
	case 0:
		if (pass < 3)
			return(0);
		break;
	case VAGE0:
		if (pass < 2)
			return(0);
		break;
	case VAGE1:
		if (pass < 1)
			return(0);
		break;
	case VAGE0 | VAGE1:
		break;
	}
	return (1);
}

/*
 * The vnode was found to be possibly vgone()able and the caller has locked it
 * (thus the usecount should be 1 now).  Determine if the vnode is actually
 * vgone()able, doing some cleanups in the process.  Returns 1 if the vnode
 * can be vgone()'d, 0 otherwise.
 *
 * Note that v_auxrefs may be non-zero because (A) this vnode is not a leaf
 * in the namecache topology and (B) this vnode has buffer cache bufs.
 * We cannot remove vnodes with non-leaf namecache associations.  We do a
 * tentitive leaf check prior to attempting to flush out any buffers but the
 * 'real' test when all is said in done is that v_auxrefs must become 0 for
 * the vnode to be freeable.
 *
 * We could theoretically just unconditionally flush when v_auxrefs != 0,
 * but flushing data associated with non-leaf nodes (which are always
 * directories), just throws it away for no benefit.  It is the buffer 
 * cache's responsibility to choose buffers to recycle from the cached
 * data point of view.
 */
static int
visleaf(struct vnode *vp)
{
	struct namecache *ncp;

	spin_lock(&vp->v_spin);
	TAILQ_FOREACH(ncp, &vp->v_namecache, nc_vnode) {
		if (!TAILQ_EMPTY(&ncp->nc_list)) {
			spin_unlock(&vp->v_spin);
			return(0);
		}
	}
	spin_unlock(&vp->v_spin);
	return(1);
}

/*
 * Try to clean up the vnode to the point where it can be vgone()'d, returning
 * 0 if it cannot be vgone()'d (or already has been), 1 if it can.  Unlike
 * vmightfree() this routine may flush the vnode and block.
 */
static int
vtrytomakegoneable(struct vnode *vp, int page_count)
{
	if (vp->v_flag & VRECLAIMED)
		return (0);
	if (VREFCNT(vp) > 1)
		return (0);
	if (vp->v_object && vp->v_object->resident_page_count >= page_count)
		return (0);
	if (vp->v_auxrefs && visleaf(vp)) {
		vinvalbuf(vp, V_SAVE, 0, 0);
#if 0	/* DEBUG */
		kprintf((vp->v_auxrefs ? "vrecycle: vp %p failed: %s\n" :
			"vrecycle: vp %p succeeded: %s\n"), vp,
			(TAILQ_FIRST(&vp->v_namecache) ? 
			    TAILQ_FIRST(&vp->v_namecache)->nc_name : "?"));
#endif
	}

	/*
	 * This sequence may seem a little strange, but we need to optimize
	 * the critical path a bit.  We can't recycle vnodes with other
	 * references and because we are trying to recycle an otherwise
	 * perfectly fine vnode we have to invalidate the namecache in a
	 * way that avoids possible deadlocks (since the vnode lock is being
	 * held here).  Finally, we have to check for other references one
	 * last time in case something snuck in during the inval.
	 */
	if (VREFCNT(vp) > 1 || vp->v_auxrefs != 0)
		return (0);
	if (cache_inval_vp_nonblock(vp))
		return (0);
	return (VREFCNT(vp) <= 1 && vp->v_auxrefs == 0);
}

/*
 * Reclaim up to 1/10 of the vnodes associated with a mount point.  Try
 * to avoid vnodes which have lots of resident pages (we are trying to free
 * vnodes, not memory).  
 *
 * This routine is a callback from the mountlist scan.  The mount point
 * in question will be busied.
 *
 * NOTE: The 1/10 reclamation also ensures that the inactive data set
 *	 (the vnodes being recycled by the one-time use) does not degenerate
 *	 into too-small a set.  This is important because once a vnode is
 *	 marked as not being one-time-use (VAGE0/VAGE1 both 0) that vnode
 *	 will not be destroyed EXCEPT by this mechanism.  VM pages can still
 *	 be cleaned/freed by the pageout daemon.
 */
static int
vlrureclaim(struct mount *mp, void *data)
{
	struct vnlru_info *info = data;
	struct vnode *vp;
	int done;
	int trigger;
	int usevnodes;
	int count;
	int trigger_mult = vnlru_nowhere;

	/*
	 * Calculate the trigger point for the resident pages check.  The
	 * minimum trigger value is approximately the number of pages in
	 * the system divded by the number of vnodes.  However, due to
	 * various other system memory overheads unrelated to data caching
	 * it is a good idea to double the trigger (at least).  
	 *
	 * trigger_mult starts at 0.  If the recycler is having problems
	 * finding enough freeable vnodes it will increase trigger_mult.
	 * This should not happen in normal operation, even on machines with
	 * low amounts of memory, but extraordinary memory use by the system
	 * verses the amount of cached data can trigger it.
	 *
	 * (long) -> deal with 64 bit machines, intermediate overflow
	 */
	usevnodes = desiredvnodes;
	if (usevnodes <= 0)
		usevnodes = 1;
	trigger = (long)vmstats.v_page_count * (trigger_mult + 2) / usevnodes;

	done = 0;
	lwkt_gettoken(&mp->mnt_token);
	count = mp->mnt_nvnodelistsize / 10 + 1;

	while (count && mp->mnt_syncer) {
		/*
		 * Next vnode.  Use the special syncer vnode to placemark
		 * the LRU.  This way the LRU code does not interfere with
		 * vmntvnodescan().
		 */
		vp = TAILQ_NEXT(mp->mnt_syncer, v_nmntvnodes);
		TAILQ_REMOVE(&mp->mnt_nvnodelist, mp->mnt_syncer, v_nmntvnodes);
		if (vp) {
			TAILQ_INSERT_AFTER(&mp->mnt_nvnodelist, vp,
					   mp->mnt_syncer, v_nmntvnodes);
		} else {
			TAILQ_INSERT_HEAD(&mp->mnt_nvnodelist, mp->mnt_syncer,
					  v_nmntvnodes);
			vp = TAILQ_NEXT(mp->mnt_syncer, v_nmntvnodes);
			if (vp == NULL)
				break;
		}

		/*
		 * __VNODESCAN__
		 *
		 * The VP will stick around while we hold mnt_token,
		 * at least until we block, so we can safely do an initial
		 * check, and then must check again after we lock the vnode.
		 */
		if (vp->v_type == VNON ||	/* syncer or indeterminant */
		    !vmightfree(vp, trigger, info->pass) /* critical path opt */
		) {
			--count;
			continue;
		}

		/*
		 * VX get the candidate vnode.  If the VX get fails the 
		 * vnode might still be on the mountlist.  Our loop depends
		 * on us at least cycling the vnode to the end of the
		 * mountlist.
		 */
		if (vx_get_nonblock(vp) != 0) {
			--count;
			continue;
		}

		/*
		 * Since we blocked locking the vp, make sure it is still
		 * a candidate for reclamation.  That is, it has not already
		 * been reclaimed and only has our VX reference associated
		 * with it.
		 */
		if (vp->v_type == VNON ||	/* syncer or indeterminant */
		    (vp->v_flag & VRECLAIMED) ||
		    vp->v_mount != mp ||
		    !vtrytomakegoneable(vp, trigger)	/* critical path opt */
		) {
			--count;
			vx_put(vp);
			continue;
		}

		/*
		 * All right, we are good, move the vp to the end of the
		 * mountlist and clean it out.  The vget will have returned
		 * an error if the vnode was destroyed (VRECLAIMED set), so we
		 * do not have to check again.  The vput() will move the 
		 * vnode to the free list if the vgone() was successful.
		 */
		KKASSERT(vp->v_mount == mp);
		vgone_vxlocked(vp);
		vx_put(vp);
		++done;
		--count;
	}
	lwkt_reltoken(&mp->mnt_token);
	return (done);
}

/*
 * Attempt to recycle vnodes in a context that is always safe to block.
 * Calling vlrurecycle() from the bowels of file system code has some
 * interesting deadlock problems.
 */
static struct thread *vnlruthread;

static void 
vnlru_proc(void)
{
	struct thread *td = curthread;
	struct vnlru_info info;
	int done;

	EVENTHANDLER_REGISTER(shutdown_pre_sync, shutdown_kproc, td,
			      SHUTDOWN_PRI_FIRST);

	for (;;) {
		kproc_suspend_loop();

		/*
		 * Do some opportunistic roving.
		 */
		if (numvnodes > 100000)
			vnode_free_rover_scan(50);
		else if (numvnodes > 10000)
			vnode_free_rover_scan(20);
		else
			vnode_free_rover_scan(5);

		/*
		 * Try to free some vnodes if we have too many
		 *
		 * (long) -> deal with 64 bit machines, intermediate overflow
		 */
		if (numvnodes > desiredvnodes &&
		    cachedvnodes > desiredvnodes * 2 / 10) {
			int count = numvnodes - desiredvnodes;

			if (count > cachedvnodes / 100)
				count = cachedvnodes / 100;
			if (count < 5)
				count = 5;
			freesomevnodes(count);
		}

		/*
		 * Do non-critical-path (more robust) cache cleaning,
		 * even if vnode counts are nominal, to try to avoid
		 * having to do it in the critical path.
		 */
		cache_hysteresis(0);

		/*
		 * Nothing to do if most of our vnodes are already on
		 * the free list.
		 */
		if (numvnodes - cachedvnodes <= (long)desiredvnodes * 9 / 10) {
			tsleep(vnlruthread, 0, "vlruwt", hz);
			continue;
		}

		/*
		 * The pass iterates through the four combinations of
		 * VAGE0/VAGE1.  We want to get rid of aged small files
		 * first.
		 */
		info.pass = 0;
		done = 0;
		while (done == 0 && info.pass < 4) {
			done = mountlist_scan(vlrureclaim, &info,
					      MNTSCAN_FORWARD);
			++info.pass;
		}

		/*
		 * The vlrureclaim() call only processes 1/10 of the vnodes
		 * on each mount.  If we couldn't find any repeat the loop
		 * at least enough times to cover all available vnodes before
		 * we start sleeping.  Complain if the failure extends past
		 * 30 second, every 30 seconds.
		 */
		if (done == 0) {
			++vnlru_nowhere;
			if (vnlru_nowhere % 10 == 0)
				tsleep(vnlruthread, 0, "vlrup", hz * 3);
			if (vnlru_nowhere % 100 == 0)
				kprintf("vnlru_proc: vnode recycler stopped working!\n");
			if (vnlru_nowhere == 1000)
				vnlru_nowhere = 900;
		} else {
			vnlru_nowhere = 0;
		}
	}
}

/*
 * MOUNTLIST FUNCTIONS
 */

/*
 * mountlist_insert (MP SAFE)
 *
 * Add a new mount point to the mount list.
 */
void
mountlist_insert(struct mount *mp, int how)
{
	lwkt_gettoken(&mountlist_token);
	if (how == MNTINS_FIRST)
	    TAILQ_INSERT_HEAD(&mountlist, mp, mnt_list);
	else
	    TAILQ_INSERT_TAIL(&mountlist, mp, mnt_list);
	lwkt_reltoken(&mountlist_token);
}

/*
 * mountlist_interlock (MP SAFE)
 *
 * Execute the specified interlock function with the mountlist token
 * held.  The function will be called in a serialized fashion verses
 * other functions called through this mechanism.
 */
int
mountlist_interlock(int (*callback)(struct mount *), struct mount *mp)
{
	int error;

	lwkt_gettoken(&mountlist_token);
	error = callback(mp);
	lwkt_reltoken(&mountlist_token);
	return (error);
}

/*
 * mountlist_boot_getfirst (DURING BOOT ONLY)
 *
 * This function returns the first mount on the mountlist, which is
 * expected to be the root mount.  Since no interlocks are obtained
 * this function is only safe to use during booting.
 */

struct mount *
mountlist_boot_getfirst(void)
{
	return(TAILQ_FIRST(&mountlist));
}

/*
 * mountlist_remove (MP SAFE)
 *
 * Remove a node from the mountlist.  If this node is the next scan node
 * for any active mountlist scans, the active mountlist scan will be 
 * adjusted to skip the node, thus allowing removals during mountlist
 * scans.
 */
void
mountlist_remove(struct mount *mp)
{
	struct mountscan_info *msi;

	lwkt_gettoken(&mountlist_token);
	TAILQ_FOREACH(msi, &mountscan_list, msi_entry) {
		if (msi->msi_node == mp) {
			if (msi->msi_how & MNTSCAN_FORWARD)
				msi->msi_node = TAILQ_NEXT(mp, mnt_list);
			else
				msi->msi_node = TAILQ_PREV(mp, mntlist, mnt_list);
		}
	}
	TAILQ_REMOVE(&mountlist, mp, mnt_list);
	lwkt_reltoken(&mountlist_token);
}

/*
 * mountlist_exists (MP SAFE)
 *
 * Checks if a node exists in the mountlist.
 * This function is mainly used by VFS quota code to check if a
 * cached nullfs struct mount pointer is still valid at use time
 *
 * FIXME: there is no warranty the mp passed to that function
 * will be the same one used by VFS_ACCOUNT() later
 */
int
mountlist_exists(struct mount *mp)
{
	int node_exists = 0;
	struct mount* lmp;

	lwkt_gettoken(&mountlist_token);
	TAILQ_FOREACH(lmp, &mountlist, mnt_list) {
		if (lmp == mp) {
			node_exists = 1;
			break;
		}
	}
	lwkt_reltoken(&mountlist_token);
	return(node_exists);
}

/*
 * mountlist_scan (MP SAFE)
 *
 * Safely scan the mount points on the mount list.  Unless otherwise 
 * specified each mount point will be busied prior to the callback and
 * unbusied afterwords.  The callback may safely remove any mount point
 * without interfering with the scan.  If the current callback
 * mount is removed the scanner will not attempt to unbusy it.
 *
 * If a mount node cannot be busied it is silently skipped.
 *
 * The callback return value is aggregated and a total is returned.  A return
 * value of < 0 is not aggregated and will terminate the scan.
 *
 * MNTSCAN_FORWARD	- the mountlist is scanned in the forward direction
 * MNTSCAN_REVERSE	- the mountlist is scanned in reverse
 * MNTSCAN_NOBUSY	- the scanner will make the callback without busying
 *			  the mount node.
 */
int
mountlist_scan(int (*callback)(struct mount *, void *), void *data, int how)
{
	struct mountscan_info info;
	struct mount *mp;
	int count;
	int res;

	lwkt_gettoken(&mountlist_token);

	info.msi_how = how;
	info.msi_node = NULL;	/* paranoia */
	TAILQ_INSERT_TAIL(&mountscan_list, &info, msi_entry);

	res = 0;

	if (how & MNTSCAN_FORWARD) {
		info.msi_node = TAILQ_FIRST(&mountlist);
		while ((mp = info.msi_node) != NULL) {
			if (how & MNTSCAN_NOBUSY) {
				count = callback(mp, data);
			} else if (vfs_busy(mp, LK_NOWAIT) == 0) {
				count = callback(mp, data);
				if (mp == info.msi_node)
					vfs_unbusy(mp);
			} else {
				count = 0;
			}
			if (count < 0)
				break;
			res += count;
			if (mp == info.msi_node)
				info.msi_node = TAILQ_NEXT(mp, mnt_list);
		}
	} else if (how & MNTSCAN_REVERSE) {
		info.msi_node = TAILQ_LAST(&mountlist, mntlist);
		while ((mp = info.msi_node) != NULL) {
			if (how & MNTSCAN_NOBUSY) {
				count = callback(mp, data);
			} else if (vfs_busy(mp, LK_NOWAIT) == 0) {
				count = callback(mp, data);
				if (mp == info.msi_node)
					vfs_unbusy(mp);
			} else {
				count = 0;
			}
			if (count < 0)
				break;
			res += count;
			if (mp == info.msi_node)
				info.msi_node = TAILQ_PREV(mp, mntlist, mnt_list);
		}
	}
	TAILQ_REMOVE(&mountscan_list, &info, msi_entry);
	lwkt_reltoken(&mountlist_token);
	return(res);
}

/*
 * MOUNT RELATED VNODE FUNCTIONS
 */

static struct kproc_desc vnlru_kp = {
	"vnlru",
	vnlru_proc,
	&vnlruthread
};
SYSINIT(vnlru, SI_SUB_KTHREAD_UPDATE, SI_ORDER_FIRST, kproc_start, &vnlru_kp)

/*
 * Move a vnode from one mount queue to another.
 *
 * MPSAFE
 */
void
insmntque(struct vnode *vp, struct mount *mp)
{
	struct mount *omp;

	/*
	 * Delete from old mount point vnode list, if on one.
	 */
	if ((omp = vp->v_mount) != NULL) {
		lwkt_gettoken(&omp->mnt_token);
		KKASSERT(omp == vp->v_mount);
		KASSERT(omp->mnt_nvnodelistsize > 0,
			("bad mount point vnode list size"));
		vremovevnodemnt(vp);
		omp->mnt_nvnodelistsize--;
		lwkt_reltoken(&omp->mnt_token);
	}

	/*
	 * Insert into list of vnodes for the new mount point, if available.
	 * The 'end' of the LRU list is the vnode prior to mp->mnt_syncer.
	 */
	if (mp == NULL) {
		vp->v_mount = NULL;
		return;
	}
	lwkt_gettoken(&mp->mnt_token);
	vp->v_mount = mp;
	if (mp->mnt_syncer) {
		TAILQ_INSERT_BEFORE(mp->mnt_syncer, vp, v_nmntvnodes);
	} else {
		TAILQ_INSERT_TAIL(&mp->mnt_nvnodelist, vp, v_nmntvnodes);
	}
	mp->mnt_nvnodelistsize++;
	lwkt_reltoken(&mp->mnt_token);
}


/*
 * Scan the vnodes under a mount point and issue appropriate callbacks.
 *
 * The fastfunc() callback is called with just the mountlist token held
 * (no vnode lock).  It may not block and the vnode may be undergoing
 * modifications while the caller is processing it.  The vnode will
 * not be entirely destroyed, however, due to the fact that the mountlist
 * token is held.  A return value < 0 skips to the next vnode without calling
 * the slowfunc(), a return value > 0 terminates the loop.
 *
 * The slowfunc() callback is called after the vnode has been successfully
 * locked based on passed flags.  The vnode is skipped if it gets rearranged
 * or destroyed while blocking on the lock.  A non-zero return value from
 * the slow function terminates the loop.  The slow function is allowed to
 * arbitrarily block.  The scanning code guarentees consistency of operation
 * even if the slow function deletes or moves the node, or blocks and some
 * other thread deletes or moves the node.
 *
 * NOTE: We hold vmobj_token to prevent a VM object from being destroyed
 *	 out from under the fastfunc()'s vnode test.  It will not prevent
 *	 v_object from getting NULL'd out but it will ensure that the
 *	 pointer (if we race) will remain stable.  Only needed when
 *	 fastfunc is non-NULL.
 */
int
vmntvnodescan(
    struct mount *mp, 
    int flags,
    int (*fastfunc)(struct mount *mp, struct vnode *vp, void *data),
    int (*slowfunc)(struct mount *mp, struct vnode *vp, void *data),
    void *data
) {
	struct vmntvnodescan_info info;
	struct vnode *vp;
	int r = 0;
	int maxcount = mp->mnt_nvnodelistsize * 2;
	int stopcount = 0;
	int count = 0;

	lwkt_gettoken(&mp->mnt_token);
	if (fastfunc)
		lwkt_gettoken(&vmobj_token);

	/*
	 * If asked to do one pass stop after iterating available vnodes.
	 * Under heavy loads new vnodes can be added while we are scanning,
	 * so this isn't perfect.  Create a slop factor of 2x.
	 */
	if (flags & VMSC_ONEPASS)
		stopcount = mp->mnt_nvnodelistsize;

	info.vp = TAILQ_FIRST(&mp->mnt_nvnodelist);
	TAILQ_INSERT_TAIL(&mp->mnt_vnodescan_list, &info, entry);

	while ((vp = info.vp) != NULL) {
		if (--maxcount == 0) {
			kprintf("Warning: excessive fssync iteration\n");
			maxcount = mp->mnt_nvnodelistsize * 2;
		}

		/*
		 * Skip if visible but not ready, or special (e.g.
		 * mp->mnt_syncer) 
		 */
		if (vp->v_type == VNON)
			goto next;
		KKASSERT(vp->v_mount == mp);

		/*
		 * Quick test.  A negative return continues the loop without
		 * calling the slow test.  0 continues onto the slow test.
		 * A positive number aborts the loop.
		 */
		if (fastfunc) {
			if ((r = fastfunc(mp, vp, data)) < 0) {
				r = 0;
				goto next;
			}
			if (r)
				break;
		}

		/*
		 * Get a vxlock on the vnode, retry if it has moved or isn't
		 * in the mountlist where we expect it.
		 */
		if (slowfunc) {
			int error;

			switch(flags & (VMSC_GETVP|VMSC_GETVX|VMSC_NOWAIT)) {
			case VMSC_GETVP:
				error = vget(vp, LK_EXCLUSIVE);
				break;
			case VMSC_GETVP|VMSC_NOWAIT:
				error = vget(vp, LK_EXCLUSIVE|LK_NOWAIT);
				break;
			case VMSC_GETVX:
				vx_get(vp);
				error = 0;
				break;
			default:
				error = 0;
				break;
			}
			if (error)
				goto next;
			/*
			 * Do not call the slow function if the vnode is
			 * invalid or if it was ripped out from under us
			 * while we (potentially) blocked.
			 */
			if (info.vp == vp && vp->v_type != VNON)
				r = slowfunc(mp, vp, data);

			/*
			 * Cleanup
			 */
			switch(flags & (VMSC_GETVP|VMSC_GETVX|VMSC_NOWAIT)) {
			case VMSC_GETVP:
			case VMSC_GETVP|VMSC_NOWAIT:
				vput(vp);
				break;
			case VMSC_GETVX:
				vx_put(vp);
				break;
			default:
				break;
			}
			if (r != 0)
				break;
		}

next:
		/*
		 * Yield after some processing.  Depending on the number
		 * of vnodes, we might wind up running for a long time.
		 * Because threads are not preemptable, time critical
		 * userland processes might starve.  Give them a chance
		 * now and then.
		 */
		if (++count == 10000) {
			/*
			 * We really want to yield a bit, so we simply
			 * sleep a tick
			 */
			tsleep(mp, 0, "vnodescn", 1);
			count = 0;
		}

		/*
		 * If doing one pass this decrements to zero.  If it starts
		 * at zero it is effectively unlimited for the purposes of
		 * this loop.
		 */
		if (--stopcount == 0)
			break;

		/*
		 * Iterate.  If the vnode was ripped out from under us
		 * info.vp will already point to the next vnode, otherwise
		 * we have to obtain the next valid vnode ourselves.
		 */
		if (info.vp == vp)
			info.vp = TAILQ_NEXT(vp, v_nmntvnodes);
	}

	TAILQ_REMOVE(&mp->mnt_vnodescan_list, &info, entry);
	if (fastfunc)
		lwkt_reltoken(&vmobj_token);
	lwkt_reltoken(&mp->mnt_token);
	return(r);
}

/*
 * Remove any vnodes in the vnode table belonging to mount point mp.
 *
 * If FORCECLOSE is not specified, there should not be any active ones,
 * return error if any are found (nb: this is a user error, not a
 * system error). If FORCECLOSE is specified, detach any active vnodes
 * that are found.
 *
 * If WRITECLOSE is set, only flush out regular file vnodes open for
 * writing.
 *
 * SKIPSYSTEM causes any vnodes marked VSYSTEM to be skipped.
 *
 * `rootrefs' specifies the base reference count for the root vnode
 * of this filesystem. The root vnode is considered busy if its
 * v_refcnt exceeds this value. On a successful return, vflush()
 * will call vrele() on the root vnode exactly rootrefs times.
 * If the SKIPSYSTEM or WRITECLOSE flags are specified, rootrefs must
 * be zero.
 */
#ifdef DIAGNOSTIC
static int busyprt = 0;		/* print out busy vnodes */
SYSCTL_INT(_debug, OID_AUTO, busyprt, CTLFLAG_RW, &busyprt, 0, "");
#endif

static int vflush_scan(struct mount *mp, struct vnode *vp, void *data);

struct vflush_info {
	int flags;
	int busy;
	thread_t td;
};

int
vflush(struct mount *mp, int rootrefs, int flags)
{
	struct thread *td = curthread;	/* XXX */
	struct vnode *rootvp = NULL;
	int error;
	struct vflush_info vflush_info;

	if (rootrefs > 0) {
		KASSERT((flags & (SKIPSYSTEM | WRITECLOSE)) == 0,
		    ("vflush: bad args"));
		/*
		 * Get the filesystem root vnode. We can vput() it
		 * immediately, since with rootrefs > 0, it won't go away.
		 */
		if ((error = VFS_ROOT(mp, &rootvp)) != 0) {
			if ((flags & FORCECLOSE) == 0)
				return (error);
			rootrefs = 0;
			/* continue anyway */
		}
		if (rootrefs)
			vput(rootvp);
	}

	vflush_info.busy = 0;
	vflush_info.flags = flags;
	vflush_info.td = td;
	vmntvnodescan(mp, VMSC_GETVX, NULL, vflush_scan, &vflush_info);

	if (rootrefs > 0 && (flags & FORCECLOSE) == 0) {
		/*
		 * If just the root vnode is busy, and if its refcount
		 * is equal to `rootrefs', then go ahead and kill it.
		 */
		KASSERT(vflush_info.busy > 0, ("vflush: not busy"));
		KASSERT(VREFCNT(rootvp) >= rootrefs, ("vflush: rootrefs"));
		if (vflush_info.busy == 1 && VREFCNT(rootvp) == rootrefs) {
			vx_lock(rootvp);
			vgone_vxlocked(rootvp);
			vx_unlock(rootvp);
			vflush_info.busy = 0;
		}
	}
	if (vflush_info.busy)
		return (EBUSY);
	for (; rootrefs > 0; rootrefs--)
		vrele(rootvp);
	return (0);
}

/*
 * The scan callback is made with an VX locked vnode.
 */
static int
vflush_scan(struct mount *mp, struct vnode *vp, void *data)
{
	struct vflush_info *info = data;
	struct vattr vattr;
	int flags = info->flags;

	/*
	 * Generally speaking try to deactivate on 0 refs (catch-all)
	 */
	atomic_set_int(&vp->v_refcnt, VREF_FINALIZE);

	/*
	 * Skip over a vnodes marked VSYSTEM.
	 */
	if ((flags & SKIPSYSTEM) && (vp->v_flag & VSYSTEM)) {
		return(0);
	}

	/*
	 * Do not force-close VCHR or VBLK vnodes
	 */
	if (vp->v_type == VCHR || vp->v_type == VBLK)
		flags &= ~(WRITECLOSE|FORCECLOSE);

	/*
	 * If WRITECLOSE is set, flush out unlinked but still open
	 * files (even if open only for reading) and regular file
	 * vnodes open for writing. 
	 */
	if ((flags & WRITECLOSE) &&
	    (vp->v_type == VNON ||
	    (VOP_GETATTR(vp, &vattr) == 0 &&
	    vattr.va_nlink > 0)) &&
	    (vp->v_writecount == 0 || vp->v_type != VREG)) {
		return(0);
	}

	/*
	 * If we are the only holder (refcnt of 1) or the vnode is in
	 * termination (refcnt < 0), we can vgone the vnode.
	 */
	if (VREFCNT(vp) <= 1) {
		vgone_vxlocked(vp);
		return(0);
	}

	/*
	 * If FORCECLOSE is set, forcibly destroy the vnode and then move
	 * it to a dummymount structure so vop_*() functions don't deref
	 * a NULL pointer.
	 */
	if (flags & FORCECLOSE) {
		vhold(vp);
		vgone_vxlocked(vp);
		if (vp->v_mount == NULL)
			insmntque(vp, &dummymount);
		vdrop(vp);
		return(0);
	}
	if (vp->v_type == VCHR || vp->v_type == VBLK)
		kprintf("vflush: Warning, cannot destroy busy device vnode\n");
#ifdef DIAGNOSTIC
	if (busyprt)
		vprint("vflush: busy vnode", vp);
#endif
	++info->busy;
	return(0);
}

void
add_bio_ops(struct bio_ops *ops)
{
	TAILQ_INSERT_TAIL(&bio_ops_list, ops, entry);
}

void
rem_bio_ops(struct bio_ops *ops)
{
	TAILQ_REMOVE(&bio_ops_list, ops, entry);
}

/*
 * This calls the bio_ops io_sync function either for a mount point
 * or generally.
 *
 * WARNING: softdeps is weirdly coded and just isn't happy unless
 * io_sync is called with a NULL mount from the general syncing code.
 */
void
bio_ops_sync(struct mount *mp)
{
	struct bio_ops *ops;

	if (mp) {
		if ((ops = mp->mnt_bioops) != NULL)
			ops->io_sync(mp);
	} else {
		TAILQ_FOREACH(ops, &bio_ops_list, entry) {
			ops->io_sync(NULL);
		}
	}
}

/*
 * Lookup a mount point by nch
 */
struct mount *
mount_get_by_nc(struct namecache *ncp)
{
	struct mount *mp = NULL;

	lwkt_gettoken(&mountlist_token);
	TAILQ_FOREACH(mp, &mountlist, mnt_list) {
		if (ncp == mp->mnt_ncmountpt.ncp)
			break;
	}
	lwkt_reltoken(&mountlist_token);
	return (mp);
}

