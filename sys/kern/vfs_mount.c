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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
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
 *
 * $DragonFly: src/sys/kern/vfs_mount.c,v 1.5 2005/02/02 21:34:18 joerg Exp $
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
#include <sys/eventhandler.h>
#include <sys/kthread.h>
#include <sys/sysctl.h>

#include <machine/limits.h>

#include <sys/buf2.h>
#include <sys/thread2.h>

#include <vm/vm.h>
#include <vm/vm_object.h>

static int vnlru_nowhere = 0;
SYSCTL_INT(_debug, OID_AUTO, vnlru_nowhere, CTLFLAG_RW,
	    &vnlru_nowhere, 0,
	    "Number of times the vnlru process ran without success");


static struct lwkt_token mntid_token;

struct mntlist mountlist = TAILQ_HEAD_INITIALIZER(mountlist); /* mounted fs */
struct lwkt_token mountlist_token;
struct lwkt_token mntvnode_token;


/*
 * Called from vfsinit()
 */
void
vfs_mount_init(void)
{
	lwkt_token_init(&mountlist_token);
	lwkt_token_init(&mntvnode_token);
	lwkt_token_init(&mntid_token);
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
	vfs_object_create(vp, curthread);

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
 * This routine is being phased out.
 */
int
getspecialvnode(enum vtagtype tag, struct mount *mp,
		struct vop_ops **ops_pp,
		struct vnode **vpp, int lktimeout, int lkflags)
{
	struct vnode *vp;

	vp = allocvnode(lktimeout, lkflags);
	vp->v_tag = tag;
	vp->v_data = NULL;
	vp->v_ops = ops_pp;

	/*
	 * Placing the vnode on the mount point's queue makes it visible.
	 * VNON prevents it from being messed with, however.
	 */
	insmntque(vp, mp);
	vfs_object_create(vp, curthread);

	/*
	 * A VX locked & refd vnode is returned.
	 */
	*vpp = vp;
	return (0);
}

/*
 * Mark a mount point as busy. Used to synchronize access and to delay
 * unmounting. Interlock is not released on failure.
 */
int
vfs_busy(struct mount *mp, int flags,
	lwkt_tokref_t interlkp, struct thread *td)
{
	int lkflags;

	if (mp->mnt_kern_flag & MNTK_UNMOUNT) {
		if (flags & LK_NOWAIT)
			return (ENOENT);
		mp->mnt_kern_flag |= MNTK_MWAIT;
		/*
		 * Since all busy locks are shared except the exclusive
		 * lock granted when unmounting, the only place that a
		 * wakeup needs to be done is at the release of the
		 * exclusive lock at the end of dounmount.
		 *
		 * note: interlkp is a serializer and thus can be safely
		 * held through any sleep
		 */
		tsleep((caddr_t)mp, 0, "vfs_busy", 0);
		return (ENOENT);
	}
	lkflags = LK_SHARED | LK_NOPAUSE;
	if (interlkp)
		lkflags |= LK_INTERLOCK;
	if (lockmgr(&mp->mnt_lock, lkflags, interlkp, td))
		panic("vfs_busy: unexpected lock failure");
	return (0);
}

/*
 * Free a busy filesystem.
 */
void
vfs_unbusy(struct mount *mp, struct thread *td)
{
	lockmgr(&mp->mnt_lock, LK_RELEASE, NULL, td);
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
	struct thread *td = curthread;	/* XXX */
	struct vfsconf *vfsp;
	struct mount *mp;

	if (fstypename == NULL)
		return (ENODEV);
	for (vfsp = vfsconf; vfsp; vfsp = vfsp->vfc_next) {
		if (!strcmp(vfsp->vfc_name, fstypename))
			break;
	}
	if (vfsp == NULL)
		return (ENODEV);
	mp = malloc(sizeof(struct mount), M_MOUNT, M_WAITOK);
	bzero((char *)mp, (u_long)sizeof(struct mount));
	lockinit(&mp->mnt_lock, 0, "vfslock", VLKTIMEOUT, LK_NOPAUSE);
	vfs_busy(mp, LK_NOWAIT, NULL, td);
	TAILQ_INIT(&mp->mnt_nvnodelist);
	TAILQ_INIT(&mp->mnt_reservedvnlist);
	TAILQ_INIT(&mp->mnt_jlist);
	mp->mnt_nvnodelistsize = 0;
	mp->mnt_vfc = vfsp;
	mp->mnt_op = vfsp->vfc_vfsops;
	mp->mnt_flag = MNT_RDONLY;
	mp->mnt_vnodecovered = NULLVP;
	vfsp->vfc_refcount++;
	mp->mnt_iosize_max = DFLTPHYS;
	mp->mnt_stat.f_type = vfsp->vfc_typenum;
	mp->mnt_flag |= vfsp->vfc_flags & MNT_VISFLAGMASK;
	strncpy(mp->mnt_stat.f_fstypename, vfsp->vfc_name, MFSNAMELEN);
	*mpp = mp;
	return (0);
}

/*
 * Lookup a mount point by filesystem identifier.
 */
struct mount *
vfs_getvfs(fsid_t *fsid)
{
	struct mount *mp;
	lwkt_tokref ilock;

	lwkt_gettoken(&ilock, &mountlist_token);
	TAILQ_FOREACH(mp, &mountlist, mnt_list) {
		if (mp->mnt_stat.f_fsid.val[0] == fsid->val[0] &&
		    mp->mnt_stat.f_fsid.val[1] == fsid->val[1]) {
			break;
	    }
	}
	lwkt_reltoken(&ilock);
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
	lwkt_tokref ilock;
	fsid_t tfsid;
	int mtype;

	lwkt_gettoken(&ilock, &mntid_token);
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
	lwkt_reltoken(&ilock);
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
 * Return 0 if the vnode is not already on the free list, return 1 if the
 * vnode, with some additional work could possibly be placed on the free list.
 */
static __inline int 
vmightfree(struct vnode *vp, int use_count, int page_count)
{
	if (vp->v_flag & VFREE)
		return (0);
	if (vp->v_usecount != use_count || vp->v_holdcnt)
		return (0);
	if (vp->v_object && vp->v_object->resident_page_count >= page_count)
		return (0);
	return (1);
}


static int
vlrureclaim(struct mount *mp)
{
	struct vnode *vp;
	lwkt_tokref ilock;
	int done;
	int trigger;
	int usevnodes;
	int count;

	/*
	 * Calculate the trigger point, don't allow user
	 * screwups to blow us up.   This prevents us from
	 * recycling vnodes with lots of resident pages.  We
	 * aren't trying to free memory, we are trying to
	 * free vnodes.
	 */
	usevnodes = desiredvnodes;
	if (usevnodes <= 0)
		usevnodes = 1;
	trigger = vmstats.v_page_count * 2 / usevnodes;

	done = 0;
	lwkt_gettoken(&ilock, &mntvnode_token);
	count = mp->mnt_nvnodelistsize / 10 + 1;
	while (count && (vp = TAILQ_FIRST(&mp->mnt_nvnodelist)) != NULL) {
		/*
		 * __VNODESCAN__
		 *
		 * The VP will stick around while we hold mntvnode_token,
		 * at least until we block, so we can safely do an initial
		 * check, and then must check again after we lock the vnode.
		 */
		if (vp->v_type == VNON ||	/* XXX */
		    vp->v_type == VBAD ||	/* XXX */
		    !vmightfree(vp, 0, trigger)	/* critical path opt */
		) {
			TAILQ_REMOVE(&mp->mnt_nvnodelist, vp, v_nmntvnodes);
			TAILQ_INSERT_TAIL(&mp->mnt_nvnodelist,vp, v_nmntvnodes);
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
			if (vp->v_mount == mp) {
				TAILQ_REMOVE(&mp->mnt_nvnodelist, 
						vp, v_nmntvnodes);
				TAILQ_INSERT_TAIL(&mp->mnt_nvnodelist,
						vp, v_nmntvnodes);
			}
			--count;
			continue;
		}

		/*
		 * Since we blocked locking the vp, make sure it is still
		 * a candidate for reclamation.  That is, it has not already
		 * been reclaimed and only has our VX reference associated
		 * with it.
		 */
		if (vp->v_type == VNON ||	/* XXX */
		    vp->v_type == VBAD ||	/* XXX */
		    (vp->v_flag & VRECLAIMED) ||
		    vp->v_mount != mp ||
		    !vmightfree(vp, 1, trigger)	/* critical path opt */
		) {
			if (vp->v_mount == mp) {
				TAILQ_REMOVE(&mp->mnt_nvnodelist, 
						vp, v_nmntvnodes);
				TAILQ_INSERT_TAIL(&mp->mnt_nvnodelist,
						vp, v_nmntvnodes);
			}
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
		TAILQ_REMOVE(&mp->mnt_nvnodelist, vp, v_nmntvnodes);
		TAILQ_INSERT_TAIL(&mp->mnt_nvnodelist,vp, v_nmntvnodes);
		vgone(vp);
		vx_put(vp);
		++done;
		--count;
	}
	lwkt_reltoken(&ilock);
	return (done);
}

/*
 * Attempt to recycle vnodes in a context that is always safe to block.
 * Calling vlrurecycle() from the bowels of file system code has some
 * interesting deadlock problems.
 */
static struct thread *vnlruthread;
static int vnlruproc_sig;

void
vnlru_proc_wait(void)
{
	if (vnlruproc_sig == 0) {
		vnlruproc_sig = 1;      /* avoid unnecessary wakeups */
		wakeup(vnlruthread);
	}
	tsleep(&vnlruproc_sig, 0, "vlruwk", hz);
}

static void 
vnlru_proc(void)
{
	struct mount *mp, *nmp;
	lwkt_tokref ilock;
	int s;
	int done;
	struct thread *td = curthread;

	EVENTHANDLER_REGISTER(shutdown_pre_sync, shutdown_kproc, td,
	    SHUTDOWN_PRI_FIRST);   

	s = splbio();
	for (;;) {
		kproc_suspend_loop();
		if (numvnodes - freevnodes <= desiredvnodes * 9 / 10) {
			vnlruproc_sig = 0;
			wakeup(&vnlruproc_sig);
			tsleep(td, 0, "vlruwt", hz);
			continue;
		}
		done = 0;
		cache_cleanneg(0);
		lwkt_gettoken(&ilock, &mountlist_token);
		for (mp = TAILQ_FIRST(&mountlist); mp != NULL; mp = nmp) {
			if (vfs_busy(mp, LK_NOWAIT, &ilock, td)) {
				nmp = TAILQ_NEXT(mp, mnt_list);
				continue;
			}
			done += vlrureclaim(mp);
			lwkt_gettokref(&ilock);
			nmp = TAILQ_NEXT(mp, mnt_list);
			vfs_unbusy(mp, td);
		}
		lwkt_reltoken(&ilock);
		if (done == 0) {
			++vnlru_nowhere;
			tsleep(td, 0, "vlrup", hz * 3);
			if (vnlru_nowhere % 10 == 0)
				printf("vnlru_proc: vnode recycler stopped working!\n");
		} else {
			vnlru_nowhere = 0;
		}
	}
	splx(s);
}

static struct kproc_desc vnlru_kp = {
	"vnlru",
	vnlru_proc,
	&vnlruthread
};
SYSINIT(vnlru, SI_SUB_KTHREAD_UPDATE, SI_ORDER_FIRST, kproc_start, &vnlru_kp)

/*
 * Move a vnode from one mount queue to another.
 */
void
insmntque(struct vnode *vp, struct mount *mp)
{
	lwkt_tokref ilock;

	lwkt_gettoken(&ilock, &mntvnode_token);
	/*
	 * Delete from old mount point vnode list, if on one.
	 */
	if (vp->v_mount != NULL) {
		KASSERT(vp->v_mount->mnt_nvnodelistsize > 0,
			("bad mount point vnode list size"));
		TAILQ_REMOVE(&vp->v_mount->mnt_nvnodelist, vp, v_nmntvnodes);
		vp->v_mount->mnt_nvnodelistsize--;
	}
	/*
	 * Insert into list of vnodes for the new mount point, if available.
	 */
	if ((vp->v_mount = mp) == NULL) {
		lwkt_reltoken(&ilock);
		return;
	}
	TAILQ_INSERT_TAIL(&mp->mnt_nvnodelist, vp, v_nmntvnodes);
	mp->mnt_nvnodelistsize++;
	lwkt_reltoken(&ilock);
}


/*
 * Scan the vnodes under a mount point.  The first function is called
 * with just the mountlist token held (no vnode lock).  The second
 * function is called with the vnode VX locked.
 */
int
vmntvnodescan(
    struct mount *mp, 
    int flags,
    int (*fastfunc)(struct mount *mp, struct vnode *vp, void *data),
    int (*slowfunc)(struct mount *mp, struct vnode *vp, void *data),
    void *data
) {
	lwkt_tokref ilock;
	struct vnode *pvp;
	struct vnode *vp;
	int r = 0;

	/*
	 * Scan the vnodes on the mount's vnode list.  Use a placemarker
	 */
	pvp = allocvnode_placemarker();

	lwkt_gettoken(&ilock, &mntvnode_token);
	TAILQ_INSERT_HEAD(&mp->mnt_nvnodelist, pvp, v_nmntvnodes);

	while ((vp = TAILQ_NEXT(pvp, v_nmntvnodes)) != NULL) {
		/*
		 * Move the placemarker and skip other placemarkers we
		 * encounter.  The nothing can get in our way so the
		 * mount point on the vp must be valid.
		 */
		TAILQ_REMOVE(&mp->mnt_nvnodelist, pvp, v_nmntvnodes);
		TAILQ_INSERT_AFTER(&mp->mnt_nvnodelist, vp, pvp, v_nmntvnodes);
		if (vp->v_flag & VPLACEMARKER)	/* another procs placemarker */
			continue;
		if (vp->v_type == VNON)		/* visible but not ready */
			continue;
		KKASSERT(vp->v_mount == mp);

		/*
		 * Quick test.  A negative return continues the loop without
		 * calling the slow test.  0 continues onto the slow test.
		 * A positive number aborts the loop.
		 */
		if (fastfunc) {
			if ((r = fastfunc(mp, vp, data)) < 0)
				continue;
			if (r)
				break;
		}

		/*
		 * Get a vxlock on the vnode, retry if it has moved or isn't
		 * in the mountlist where we expect it.
		 */
		if (slowfunc) {
			int error;

			switch(flags) {
			case VMSC_GETVP:
				error = vget(vp, LK_EXCLUSIVE, curthread);
				break;
			case VMSC_GETVP|VMSC_NOWAIT:
				error = vget(vp, LK_EXCLUSIVE|LK_NOWAIT,
						curthread);
				break;
			case VMSC_GETVX:
				error = vx_get(vp);
				break;
			case VMSC_REFVP:
				vref(vp);
				/* fall through */
			default:
				error = 0;
				break;
			}
			if (error)
				continue;
			if (TAILQ_PREV(pvp, vnodelst, v_nmntvnodes) != vp)
				goto skip;
			if (vp->v_type == VNON)
				goto skip;
			r = slowfunc(mp, vp, data);
skip:
			switch(flags) {
			case VMSC_GETVP:
			case VMSC_GETVP|VMSC_NOWAIT:
				vput(vp);
				break;
			case VMSC_GETVX:
				vx_put(vp);
				break;
			case VMSC_REFVP:
				vrele(vp);
				/* fall through */
			default:
				break;
			}
			if (r != 0)
				break;
		}
	}
	TAILQ_REMOVE(&mp->mnt_nvnodelist, pvp, v_nmntvnodes);
	freevnode_placemarker(pvp);
	lwkt_reltoken(&ilock);
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
 * v_usecount exceeds this value. On a successful return, vflush()
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
		if ((error = VFS_ROOT(mp, &rootvp)) != 0)
			return (error);
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
		KASSERT(rootvp->v_usecount >= rootrefs, ("vflush: rootrefs"));
		if (vflush_info.busy == 1 && rootvp->v_usecount == rootrefs) {
			if (vx_lock(rootvp) == 0) {
				vgone(rootvp);
				vx_unlock(rootvp);
				vflush_info.busy = 0;
			}
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

	/*
	 * Skip over a vnodes marked VSYSTEM.
	 */
	if ((info->flags & SKIPSYSTEM) && (vp->v_flag & VSYSTEM)) {
		return(0);
	}

	/*
	 * If WRITECLOSE is set, flush out unlinked but still open
	 * files (even if open only for reading) and regular file
	 * vnodes open for writing. 
	 */
	if ((info->flags & WRITECLOSE) &&
	    (vp->v_type == VNON ||
	    (VOP_GETATTR(vp, &vattr, info->td) == 0 &&
	    vattr.va_nlink > 0)) &&
	    (vp->v_writecount == 0 || vp->v_type != VREG)) {
		return(0);
	}

	/*
	 * With v_usecount == 0, all we need to do is clear out the
	 * vnode data structures and we are done.
	 */
	if (vp->v_usecount == 1) {
		vgone(vp);
		return(0);
	}

	/*
	 * If FORCECLOSE is set, forcibly close the vnode. For block
	 * or character devices, revert to an anonymous device. For
	 * all other files, just kill them.
	 */
	if (info->flags & FORCECLOSE) {
		if (vp->v_type != VBLK && vp->v_type != VCHR) {
			vgone(vp);
		} else {
			vclean(vp, 0, info->td);
			vp->v_ops = &spec_vnode_vops;
			insmntque(vp, NULL);
		}
		return(0);
	}
#ifdef DIAGNOSTIC
	if (busyprt)
		vprint("vflush: busy vnode", vp);
#endif
	++info->busy;
	return(0);
}

