/*
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
 *	@(#)vfs_subr.c	8.31 (Berkeley) 5/26/95
 * $FreeBSD: src/sys/kern/vfs_subr.c,v 1.249.2.30 2003/04/04 20:35:57 tegge Exp $
 * $DragonFly: src/sys/kern/vfs_subr.c,v 1.25 2004/02/10 07:34:42 dillon Exp $
 */

/*
 * External virtual filesystem routines
 */
#include "opt_ddb.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/conf.h>
#include <sys/dirent.h>
#include <sys/domain.h>
#include <sys/eventhandler.h>
#include <sys/fcntl.h>
#include <sys/kernel.h>
#include <sys/kthread.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/mount.h>
#include <sys/proc.h>
#include <sys/namei.h>
#include <sys/reboot.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/syslog.h>
#include <sys/vmmeter.h>
#include <sys/vnode.h>

#include <machine/limits.h>

#include <vm/vm.h>
#include <vm/vm_object.h>
#include <vm/vm_extern.h>
#include <vm/vm_kern.h>
#include <vm/pmap.h>
#include <vm/vm_map.h>
#include <vm/vm_page.h>
#include <vm/vm_pager.h>
#include <vm/vnode_pager.h>
#include <vm/vm_zone.h>

#include <sys/buf2.h>
#include <sys/thread2.h>

static MALLOC_DEFINE(M_NETADDR, "Export Host", "Export host address structure");

static void	insmntque (struct vnode *vp, struct mount *mp);
static void	vclean (struct vnode *vp, int flags, struct thread *td);
static unsigned long	numvnodes;
static void	vlruvp(struct vnode *vp);
SYSCTL_INT(_debug, OID_AUTO, numvnodes, CTLFLAG_RD, &numvnodes, 0, "");

enum vtype iftovt_tab[16] = {
	VNON, VFIFO, VCHR, VNON, VDIR, VNON, VBLK, VNON,
	VREG, VNON, VLNK, VNON, VSOCK, VNON, VNON, VBAD,
};
int vttoif_tab[9] = {
	0, S_IFREG, S_IFDIR, S_IFBLK, S_IFCHR, S_IFLNK,
	S_IFSOCK, S_IFIFO, S_IFMT,
};

static TAILQ_HEAD(freelst, vnode) vnode_free_list;	/* vnode free list */

static u_long wantfreevnodes = 25;
SYSCTL_INT(_debug, OID_AUTO, wantfreevnodes, CTLFLAG_RW, &wantfreevnodes, 0, "");
static u_long freevnodes = 0;
SYSCTL_INT(_debug, OID_AUTO, freevnodes, CTLFLAG_RD, &freevnodes, 0, "");

static int reassignbufcalls;
SYSCTL_INT(_vfs, OID_AUTO, reassignbufcalls, CTLFLAG_RW, &reassignbufcalls, 0, "");
static int reassignbufloops;
SYSCTL_INT(_vfs, OID_AUTO, reassignbufloops, CTLFLAG_RW, &reassignbufloops, 0, "");
static int reassignbufsortgood;
SYSCTL_INT(_vfs, OID_AUTO, reassignbufsortgood, CTLFLAG_RW, &reassignbufsortgood, 0, "");
static int reassignbufsortbad;
SYSCTL_INT(_vfs, OID_AUTO, reassignbufsortbad, CTLFLAG_RW, &reassignbufsortbad, 0, "");
static int reassignbufmethod = 1;
SYSCTL_INT(_vfs, OID_AUTO, reassignbufmethod, CTLFLAG_RW, &reassignbufmethod, 0, "");

#ifdef ENABLE_VFS_IOOPT
int vfs_ioopt = 0;
SYSCTL_INT(_vfs, OID_AUTO, ioopt, CTLFLAG_RW, &vfs_ioopt, 0, "");
#endif

struct mntlist mountlist = TAILQ_HEAD_INITIALIZER(mountlist); /* mounted fs */
struct lwkt_token mountlist_token;
struct lwkt_token mntvnode_token;
int	nfs_mount_type = -1;
static struct lwkt_token mntid_token;
static struct lwkt_token vnode_free_list_token;
static struct lwkt_token spechash_token;
struct nfs_public nfs_pub;	/* publicly exported FS */
static vm_zone_t vnode_zone;

/*
 * The workitem queue.
 */
#define SYNCER_MAXDELAY		32
static int syncer_maxdelay = SYNCER_MAXDELAY;	/* maximum delay time */
time_t syncdelay = 30;		/* max time to delay syncing data */
SYSCTL_INT(_kern, OID_AUTO, syncdelay, CTLFLAG_RW, &syncdelay, 0,
	"VFS data synchronization delay");
time_t filedelay = 30;		/* time to delay syncing files */
SYSCTL_INT(_kern, OID_AUTO, filedelay, CTLFLAG_RW, &filedelay, 0,
	"File synchronization delay");
time_t dirdelay = 29;		/* time to delay syncing directories */
SYSCTL_INT(_kern, OID_AUTO, dirdelay, CTLFLAG_RW, &dirdelay, 0,
	"Directory synchronization delay");
time_t metadelay = 28;		/* time to delay syncing metadata */
SYSCTL_INT(_kern, OID_AUTO, metadelay, CTLFLAG_RW, &metadelay, 0,
	"VFS metadata synchronization delay");
static int rushjob;			/* number of slots to run ASAP */
static int stat_rush_requests;	/* number of times I/O speeded up */
SYSCTL_INT(_debug, OID_AUTO, rush_requests, CTLFLAG_RW, &stat_rush_requests, 0, "");

static int syncer_delayno = 0;
static long syncer_mask; 
LIST_HEAD(synclist, vnode);
static struct synclist *syncer_workitem_pending;

int desiredvnodes;
SYSCTL_INT(_kern, KERN_MAXVNODES, maxvnodes, CTLFLAG_RW, 
    &desiredvnodes, 0, "Maximum number of vnodes");
static int minvnodes;
SYSCTL_INT(_kern, OID_AUTO, minvnodes, CTLFLAG_RW, 
    &minvnodes, 0, "Minimum number of vnodes");
static int vnlru_nowhere = 0;
SYSCTL_INT(_debug, OID_AUTO, vnlru_nowhere, CTLFLAG_RW, &vnlru_nowhere, 0,
    "Number of times the vnlru process ran without success");

static void	vfs_free_addrlist (struct netexport *nep);
static int	vfs_free_netcred (struct radix_node *rn, void *w);
static int	vfs_hang_addrlist (struct mount *mp, struct netexport *nep,
				       struct export_args *argp);

/*
 * Initialize the vnode management data structures.
 */
void
vntblinit()
{

	/*
	 * Desired vnodes is a result of the physical page count
	 * and the size of kernel's heap.  It scales in proportion
	 * to the amount of available physical memory.  This can
	 * cause trouble on 64-bit and large memory platforms.
	 */
	/* desiredvnodes = maxproc + vmstats.v_page_count / 4; */
	desiredvnodes =
		min(maxproc + vmstats.v_page_count /4,
		    2 * (VM_MAX_KERNEL_ADDRESS - VM_MIN_KERNEL_ADDRESS) /
		    (5 * (sizeof(struct vm_object) + sizeof(struct vnode))));

	minvnodes = desiredvnodes / 4;
	lwkt_inittoken(&mountlist_token);
	lwkt_inittoken(&mntvnode_token);
	lwkt_inittoken(&mntid_token);
	lwkt_inittoken(&spechash_token);
	TAILQ_INIT(&vnode_free_list);
	lwkt_inittoken(&vnode_free_list_token);
	vnode_zone = zinit("VNODE", sizeof (struct vnode), 0, 0, 5);
	/*
	 * Initialize the filesystem syncer.
	 */     
	syncer_workitem_pending = hashinit(syncer_maxdelay, M_VNODE, 
		&syncer_mask);
	syncer_maxdelay = syncer_mask + 1;
}

/*
 * Mark a mount point as busy. Used to synchronize access and to delay
 * unmounting. Interlock is not released on failure.
 */
int
vfs_busy(struct mount *mp, int flags, struct lwkt_token *interlkp,
	struct thread *td)
{
	int lkflags;

	if (mp->mnt_kern_flag & MNTK_UNMOUNT) {
		if (flags & LK_NOWAIT)
			return (ENOENT);
		mp->mnt_kern_flag |= MNTK_MWAIT;
		if (interlkp) {
			lwkt_reltoken(interlkp);
		}
		/*
		 * Since all busy locks are shared except the exclusive
		 * lock granted when unmounting, the only place that a
		 * wakeup needs to be done is at the release of the
		 * exclusive lock at the end of dounmount.
		 */
		tsleep((caddr_t)mp, 0, "vfs_busy", 0);
		if (interlkp) {
			lwkt_gettoken(interlkp);
		}
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
	for (vfsp = vfsconf; vfsp; vfsp = vfsp->vfc_next)
		if (!strcmp(vfsp->vfc_name, fstypename))
			break;
	if (vfsp == NULL)
		return (ENODEV);
	mp = malloc((u_long)sizeof(struct mount), M_MOUNT, M_WAITOK);
	bzero((char *)mp, (u_long)sizeof(struct mount));
	lockinit(&mp->mnt_lock, 0, "vfslock", VLKTIMEOUT, LK_NOPAUSE);
	(void)vfs_busy(mp, LK_NOWAIT, 0, td);
	TAILQ_INIT(&mp->mnt_nvnodelist);
	TAILQ_INIT(&mp->mnt_reservedvnlist);
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
	mp->mnt_stat.f_mntonname[0] = '/';
	mp->mnt_stat.f_mntonname[1] = 0;
	(void) copystr(devname, mp->mnt_stat.f_mntfromname, MNAMELEN - 1, 0);
	*mpp = mp;
	return (0);
}

/*
 * Find an appropriate filesystem to use for the root. If a filesystem
 * has not been preselected, walk through the list of known filesystems
 * trying those that have mountroot routines, and try them until one
 * works or we have tried them all.
 */
#ifdef notdef	/* XXX JH */
int
lite2_vfs_mountroot()
{
	struct vfsconf *vfsp;
	extern int (*lite2_mountroot) (void);
	int error;

	if (lite2_mountroot != NULL)
		return ((*lite2_mountroot)());
	for (vfsp = vfsconf; vfsp; vfsp = vfsp->vfc_next) {
		if (vfsp->vfc_mountroot == NULL)
			continue;
		if ((error = (*vfsp->vfc_mountroot)()) == 0)
			return (0);
		printf("%s_mountroot failed: %d\n", vfsp->vfc_name, error);
	}
	return (ENODEV);
}
#endif

/*
 * Lookup a mount point by filesystem identifier.
 */
struct mount *
vfs_getvfs(fsid)
	fsid_t *fsid;
{
	struct mount *mp;

	lwkt_gettoken(&mountlist_token);
	TAILQ_FOREACH(mp, &mountlist, mnt_list) {
		if (mp->mnt_stat.f_fsid.val[0] == fsid->val[0] &&
		    mp->mnt_stat.f_fsid.val[1] == fsid->val[1]) {
			lwkt_reltoken(&mountlist_token);
			return (mp);
	    }
	}
	lwkt_reltoken(&mountlist_token);
	return ((struct mount *) 0);
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
vfs_getnewfsid(mp)
	struct mount *mp;
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
 * Knob to control the precision of file timestamps:
 *
 *   0 = seconds only; nanoseconds zeroed.
 *   1 = seconds and nanoseconds, accurate within 1/HZ.
 *   2 = seconds and nanoseconds, truncated to microseconds.
 * >=3 = seconds and nanoseconds, maximum precision.
 */
enum { TSP_SEC, TSP_HZ, TSP_USEC, TSP_NSEC };

static int timestamp_precision = TSP_SEC;
SYSCTL_INT(_vfs, OID_AUTO, timestamp_precision, CTLFLAG_RW,
    &timestamp_precision, 0, "");

/*
 * Get a current timestamp.
 */
void
vfs_timestamp(tsp)
	struct timespec *tsp;
{
	struct timeval tv;

	switch (timestamp_precision) {
	case TSP_SEC:
		tsp->tv_sec = time_second;
		tsp->tv_nsec = 0;
		break;
	case TSP_HZ:
		getnanotime(tsp);
		break;
	case TSP_USEC:
		microtime(&tv);
		TIMEVAL_TO_TIMESPEC(&tv, tsp);
		break;
	case TSP_NSEC:
	default:
		nanotime(tsp);
		break;
	}
}

/*
 * Set vnode attributes to VNOVAL
 */
void
vattr_null(vap)
	struct vattr *vap;
{

	vap->va_type = VNON;
	vap->va_size = VNOVAL;
	vap->va_bytes = VNOVAL;
	vap->va_mode = VNOVAL;
	vap->va_nlink = VNOVAL;
	vap->va_uid = VNOVAL;
	vap->va_gid = VNOVAL;
	vap->va_fsid = VNOVAL;
	vap->va_fileid = VNOVAL;
	vap->va_blocksize = VNOVAL;
	vap->va_rdev = VNOVAL;
	vap->va_atime.tv_sec = VNOVAL;
	vap->va_atime.tv_nsec = VNOVAL;
	vap->va_mtime.tv_sec = VNOVAL;
	vap->va_mtime.tv_nsec = VNOVAL;
	vap->va_ctime.tv_sec = VNOVAL;
	vap->va_ctime.tv_nsec = VNOVAL;
	vap->va_flags = VNOVAL;
	vap->va_gen = VNOVAL;
	vap->va_vaflags = 0;
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
static int
vlrureclaim(struct mount *mp)
{
	struct vnode *vp;
	int done;
	int trigger;
	int usevnodes;
	int count;
	int gen;

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
	gen = lwkt_gettoken(&mntvnode_token);
	count = mp->mnt_nvnodelistsize / 10 + 1;
	while (count && (vp = TAILQ_FIRST(&mp->mnt_nvnodelist)) != NULL) {
		TAILQ_REMOVE(&mp->mnt_nvnodelist, vp, v_nmntvnodes);
		TAILQ_INSERT_TAIL(&mp->mnt_nvnodelist, vp, v_nmntvnodes);

		if (vp->v_type != VNON &&
		    vp->v_type != VBAD &&
		    VMIGHTFREE(vp) &&		/* critical path opt */
		    (vp->v_object == NULL || vp->v_object->resident_page_count < trigger)
		) {
			lwkt_gettoken(&vp->v_interlock);
			if (lwkt_gentoken(&mntvnode_token, &gen) == 0) {
				if (VMIGHTFREE(vp)) {
					vgonel(vp, curthread);
					done++;
				} else {
					lwkt_reltoken(&vp->v_interlock);
				}
			} else {
				lwkt_reltoken(&vp->v_interlock);
			}
		}
		--count;
	}
	lwkt_reltoken(&mntvnode_token);
	return done;
}

/*
 * Attempt to recycle vnodes in a context that is always safe to block.
 * Calling vlrurecycle() from the bowels of file system code has some
 * interesting deadlock problems.
 */
static struct thread *vnlruthread;
static int vnlruproc_sig;

static void 
vnlru_proc(void)
{
	struct mount *mp, *nmp;
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
		lwkt_gettoken(&mountlist_token);
		for (mp = TAILQ_FIRST(&mountlist); mp != NULL; mp = nmp) {
			if (vfs_busy(mp, LK_NOWAIT, &mountlist_token, td)) {
				nmp = TAILQ_NEXT(mp, mnt_list);
				continue;
			}
			done += vlrureclaim(mp);
			lwkt_gettoken(&mountlist_token);
			nmp = TAILQ_NEXT(mp, mnt_list);
			vfs_unbusy(mp, td);
		}
		lwkt_reltoken(&mountlist_token);
		if (done == 0) {
			vnlru_nowhere++;
			tsleep(td, 0, "vlrup", hz * 3);
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
 * Routines having to do with the management of the vnode table.
 */
extern vop_t **dead_vnodeop_p;

/*
 * Return the next vnode from the free list.
 */
int
getnewvnode(tag, mp, vops, vpp)
	enum vtagtype tag;
	struct mount *mp;
	vop_t **vops;
	struct vnode **vpp;
{
	int s;
	int gen;
	int vgen;
	struct thread *td = curthread;	/* XXX */
	struct vnode *vp = NULL;
	vm_object_t object;

	s = splbio();

	/*
	 * Try to reuse vnodes if we hit the max.  This situation only
	 * occurs in certain large-memory (2G+) situations.  We cannot
	 * attempt to directly reclaim vnodes due to nasty recursion
	 * problems.
	 */
	while (numvnodes - freevnodes > desiredvnodes) {
		if (vnlruproc_sig == 0) {
			vnlruproc_sig = 1;	/* avoid unnecessary wakeups */
			wakeup(vnlruthread);
		}
		tsleep(&vnlruproc_sig, 0, "vlruwk", hz);
	}


	/*
	 * Attempt to reuse a vnode already on the free list, allocating
	 * a new vnode if we can't find one or if we have not reached a
	 * good minimum for good LRU performance.
	 */
	gen = lwkt_gettoken(&vnode_free_list_token);
	if (freevnodes >= wantfreevnodes && numvnodes >= minvnodes) {
		int count;

		for (count = 0; count < freevnodes; count++) {
			vp = TAILQ_FIRST(&vnode_free_list);
			if (vp == NULL || vp->v_usecount)
				panic("getnewvnode: free vnode isn't");

			/*
			 * Get the vnode's interlock, then re-obtain 
			 * vnode_free_list_token in case we lost it.  If we
			 * did lose it while getting the vnode interlock,
			 * even if we got it back again, then retry.
			 */
			vgen = lwkt_gettoken(&vp->v_interlock);
			if (lwkt_gentoken(&vnode_free_list_token, &gen) != 0) {
				--count;
				lwkt_reltoken(&vp->v_interlock);
				vp = NULL;
				continue;
			}

			/*
			 * Whew!  We have both tokens.  Since we didn't lose
			 * the free list VFREE had better still be set.  But
			 * we aren't out of the woods yet.  We have to get
			 * the object (may block).  If the vnode is not 
			 * suitable then move it to the end of the list
			 * if we can.  If we can't move it to the end of the
			 * list retry again.
			 */
			if ((VOP_GETVOBJECT(vp, &object) == 0 &&
			    (object->resident_page_count || object->ref_count))
			) {
				if (lwkt_gentoken(&vp->v_interlock, &vgen) == 0 &&
				   lwkt_gentoken(&vnode_free_list_token, &gen) == 0
				) {
					TAILQ_REMOVE(&vnode_free_list, vp, v_freelist);
					TAILQ_INSERT_TAIL(&vnode_free_list, vp, v_freelist);
				} else {
					--count;
				}
				lwkt_reltoken(&vp->v_interlock);
				vp = NULL;
				continue;
			}

			/*
			 * Still not out of the woods.  VOBJECT might have
			 * blocked, if we did not retain our tokens we have
			 * to retry.
			 */
			if (lwkt_gentoken(&vp->v_interlock, &vgen) != 0 ||
			    lwkt_gentoken(&vnode_free_list_token, &gen) != 0) {
				--count;
				vp = NULL;
				continue;
			}
			TAILQ_REMOVE(&vnode_free_list, vp, v_freelist);
			KKASSERT(vp->v_flag & VFREE);

			/*
			 * If we have children in the namecache we cannot
			 * reuse the vnode yet because it will break the
			 * namecache chain (YYY use nc_refs for the check?)
			 */
			if (TAILQ_FIRST(&vp->v_namecache)) {
				if (cache_leaf_test(vp) < 0) {
					lwkt_reltoken(&vp->v_interlock);
					TAILQ_INSERT_TAIL(&vnode_free_list, vp, v_freelist);
					vp = NULL;
					continue;
				}
			}
			break;
		}
	}

	if (vp) {
		vp->v_flag |= VDOOMED;
		vp->v_flag &= ~VFREE;
		freevnodes--;
		lwkt_reltoken(&vnode_free_list_token);
		cache_purge(vp);	/* YYY may block */
		vp->v_lease = NULL;
		if (vp->v_type != VBAD) {
			vgonel(vp, td);
		} else {
			lwkt_reltoken(&vp->v_interlock);
		}

#ifdef INVARIANTS
		{
			int s;

			if (vp->v_data)
				panic("cleaned vnode isn't");
			s = splbio();
			if (vp->v_numoutput)
				panic("Clean vnode has pending I/O's");
			splx(s);
		}
#endif
		vp->v_flag = 0;
		vp->v_lastw = 0;
		vp->v_lasta = 0;
		vp->v_cstart = 0;
		vp->v_clen = 0;
		vp->v_socket = 0;
		vp->v_writecount = 0;	/* XXX */
	} else {
		lwkt_reltoken(&vnode_free_list_token);
		vp = (struct vnode *) zalloc(vnode_zone);
		bzero((char *) vp, sizeof *vp);
		lwkt_inittoken(&vp->v_interlock);
		lwkt_inittoken(&vp->v_pollinfo.vpi_token);
		vp->v_dd = vp;
		cache_purge(vp);
		TAILQ_INIT(&vp->v_namecache);
		numvnodes++;
	}

	TAILQ_INIT(&vp->v_cleanblkhd);
	TAILQ_INIT(&vp->v_dirtyblkhd);
	vp->v_type = VNON;
	vp->v_tag = tag;
	vp->v_op = vops;
	insmntque(vp, mp);
	*vpp = vp;
	vp->v_usecount = 1;
	vp->v_data = 0;
	splx(s);

	vfs_object_create(vp, td);
	return (0);
}

/*
 * Move a vnode from one mount queue to another.
 */
static void
insmntque(vp, mp)
	struct vnode *vp;
	struct mount *mp;
{

	lwkt_gettoken(&mntvnode_token);
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
		lwkt_reltoken(&mntvnode_token);
		return;
	}
	TAILQ_INSERT_TAIL(&mp->mnt_nvnodelist, vp, v_nmntvnodes);
	mp->mnt_nvnodelistsize++;
	lwkt_reltoken(&mntvnode_token);
}

/*
 * Update outstanding I/O count and do wakeup if requested.
 */
void
vwakeup(bp)
	struct buf *bp;
{
	struct vnode *vp;

	bp->b_flags &= ~B_WRITEINPROG;
	if ((vp = bp->b_vp)) {
		vp->v_numoutput--;
		if (vp->v_numoutput < 0)
			panic("vwakeup: neg numoutput");
		if ((vp->v_numoutput == 0) && (vp->v_flag & VBWAIT)) {
			vp->v_flag &= ~VBWAIT;
			wakeup((caddr_t) &vp->v_numoutput);
		}
	}
}

/*
 * Flush out and invalidate all buffers associated with a vnode.
 * Called with the underlying object locked.
 */
int
vinvalbuf(struct vnode *vp, int flags, struct thread *td,
	int slpflag, int slptimeo)
{
	struct buf *bp;
	struct buf *nbp, *blist;
	int s, error;
	vm_object_t object;

	if (flags & V_SAVE) {
		s = splbio();
		while (vp->v_numoutput) {
			vp->v_flag |= VBWAIT;
			error = tsleep((caddr_t)&vp->v_numoutput,
			    slpflag, "vinvlbuf", slptimeo);
			if (error) {
				splx(s);
				return (error);
			}
		}
		if (!TAILQ_EMPTY(&vp->v_dirtyblkhd)) {
			splx(s);
			if ((error = VOP_FSYNC(vp, MNT_WAIT, td)) != 0)
				return (error);
			s = splbio();
			if (vp->v_numoutput > 0 ||
			    !TAILQ_EMPTY(&vp->v_dirtyblkhd))
				panic("vinvalbuf: dirty bufs");
		}
		splx(s);
  	}
	s = splbio();
	for (;;) {
		blist = TAILQ_FIRST(&vp->v_cleanblkhd);
		if (!blist)
			blist = TAILQ_FIRST(&vp->v_dirtyblkhd);
		if (!blist)
			break;

		for (bp = blist; bp; bp = nbp) {
			nbp = TAILQ_NEXT(bp, b_vnbufs);
			if (BUF_LOCK(bp, LK_EXCLUSIVE | LK_NOWAIT)) {
				error = BUF_TIMELOCK(bp,
				    LK_EXCLUSIVE | LK_SLEEPFAIL,
				    "vinvalbuf", slpflag, slptimeo);
				if (error == ENOLCK)
					break;
				splx(s);
				return (error);
			}
			/*
			 * XXX Since there are no node locks for NFS, I
			 * believe there is a slight chance that a delayed
			 * write will occur while sleeping just above, so
			 * check for it.  Note that vfs_bio_awrite expects
			 * buffers to reside on a queue, while VOP_BWRITE and
			 * brelse do not.
			 */
			if (((bp->b_flags & (B_DELWRI | B_INVAL)) == B_DELWRI) &&
				(flags & V_SAVE)) {

				if (bp->b_vp == vp) {
					if (bp->b_flags & B_CLUSTEROK) {
						BUF_UNLOCK(bp);
						vfs_bio_awrite(bp);
					} else {
						bremfree(bp);
						bp->b_flags |= B_ASYNC;
						VOP_BWRITE(bp->b_vp, bp);
					}
				} else {
					bremfree(bp);
					(void) VOP_BWRITE(bp->b_vp, bp);
				}
				break;
			}
			bremfree(bp);
			bp->b_flags |= (B_INVAL | B_NOCACHE | B_RELBUF);
			bp->b_flags &= ~B_ASYNC;
			brelse(bp);
		}
	}

	/*
	 * Wait for I/O to complete.  XXX needs cleaning up.  The vnode can
	 * have write I/O in-progress but if there is a VM object then the
	 * VM object can also have read-I/O in-progress.
	 */
	do {
		while (vp->v_numoutput > 0) {
			vp->v_flag |= VBWAIT;
			tsleep(&vp->v_numoutput, 0, "vnvlbv", 0);
		}
		if (VOP_GETVOBJECT(vp, &object) == 0) {
			while (object->paging_in_progress)
				vm_object_pip_sleep(object, "vnvlbx");
		}
	} while (vp->v_numoutput > 0);

	splx(s);

	/*
	 * Destroy the copy in the VM cache, too.
	 */
	lwkt_gettoken(&vp->v_interlock);
	if (VOP_GETVOBJECT(vp, &object) == 0) {
		vm_object_page_remove(object, 0, 0,
			(flags & V_SAVE) ? TRUE : FALSE);
	}
	lwkt_reltoken(&vp->v_interlock);

	if (!TAILQ_EMPTY(&vp->v_dirtyblkhd) || !TAILQ_EMPTY(&vp->v_cleanblkhd))
		panic("vinvalbuf: flush failed");
	return (0);
}

/*
 * Truncate a file's buffer and pages to a specified length.  This
 * is in lieu of the old vinvalbuf mechanism, which performed unneeded
 * sync activity.
 */
int
vtruncbuf(struct vnode *vp, struct thread *td, off_t length, int blksize)
{
	struct buf *bp;
	struct buf *nbp;
	int s, anyfreed;
	int trunclbn;

	/*
	 * Round up to the *next* lbn.
	 */
	trunclbn = (length + blksize - 1) / blksize;

	s = splbio();
restart:
	anyfreed = 1;
	for (;anyfreed;) {
		anyfreed = 0;
		for (bp = TAILQ_FIRST(&vp->v_cleanblkhd); bp; bp = nbp) {
			nbp = TAILQ_NEXT(bp, b_vnbufs);
			if (bp->b_lblkno >= trunclbn) {
				if (BUF_LOCK(bp, LK_EXCLUSIVE | LK_NOWAIT)) {
					BUF_LOCK(bp, LK_EXCLUSIVE|LK_SLEEPFAIL);
					goto restart;
				} else {
					bremfree(bp);
					bp->b_flags |= (B_INVAL | B_RELBUF);
					bp->b_flags &= ~B_ASYNC;
					brelse(bp);
					anyfreed = 1;
				}
				if (nbp &&
				    (((nbp->b_xflags & BX_VNCLEAN) == 0) ||
				    (nbp->b_vp != vp) ||
				    (nbp->b_flags & B_DELWRI))) {
					goto restart;
				}
			}
		}

		for (bp = TAILQ_FIRST(&vp->v_dirtyblkhd); bp; bp = nbp) {
			nbp = TAILQ_NEXT(bp, b_vnbufs);
			if (bp->b_lblkno >= trunclbn) {
				if (BUF_LOCK(bp, LK_EXCLUSIVE | LK_NOWAIT)) {
					BUF_LOCK(bp, LK_EXCLUSIVE|LK_SLEEPFAIL);
					goto restart;
				} else {
					bremfree(bp);
					bp->b_flags |= (B_INVAL | B_RELBUF);
					bp->b_flags &= ~B_ASYNC;
					brelse(bp);
					anyfreed = 1;
				}
				if (nbp &&
				    (((nbp->b_xflags & BX_VNDIRTY) == 0) ||
				    (nbp->b_vp != vp) ||
				    (nbp->b_flags & B_DELWRI) == 0)) {
					goto restart;
				}
			}
		}
	}

	if (length > 0) {
restartsync:
		for (bp = TAILQ_FIRST(&vp->v_dirtyblkhd); bp; bp = nbp) {
			nbp = TAILQ_NEXT(bp, b_vnbufs);
			if ((bp->b_flags & B_DELWRI) && (bp->b_lblkno < 0)) {
				if (BUF_LOCK(bp, LK_EXCLUSIVE | LK_NOWAIT)) {
					BUF_LOCK(bp, LK_EXCLUSIVE|LK_SLEEPFAIL);
					goto restart;
				} else {
					bremfree(bp);
					if (bp->b_vp == vp) {
						bp->b_flags |= B_ASYNC;
					} else {
						bp->b_flags &= ~B_ASYNC;
					}
					VOP_BWRITE(bp->b_vp, bp);
				}
				goto restartsync;
			}

		}
	}

	while (vp->v_numoutput > 0) {
		vp->v_flag |= VBWAIT;
		tsleep(&vp->v_numoutput, 0, "vbtrunc", 0);
	}

	splx(s);

	vnode_pager_setsize(vp, length);

	return (0);
}

/*
 * Associate a buffer with a vnode.
 */
void
bgetvp(vp, bp)
	struct vnode *vp;
	struct buf *bp;
{
	int s;

	KASSERT(bp->b_vp == NULL, ("bgetvp: not free"));

	vhold(vp);
	bp->b_vp = vp;
	bp->b_dev = vn_todev(vp);
	/*
	 * Insert onto list for new vnode.
	 */
	s = splbio();
	bp->b_xflags |= BX_VNCLEAN;
	bp->b_xflags &= ~BX_VNDIRTY;
	TAILQ_INSERT_TAIL(&vp->v_cleanblkhd, bp, b_vnbufs);
	splx(s);
}

/*
 * Disassociate a buffer from a vnode.
 */
void
brelvp(bp)
	struct buf *bp;
{
	struct vnode *vp;
	struct buflists *listheadp;
	int s;

	KASSERT(bp->b_vp != NULL, ("brelvp: NULL"));

	/*
	 * Delete from old vnode list, if on one.
	 */
	vp = bp->b_vp;
	s = splbio();
	if (bp->b_xflags & (BX_VNDIRTY | BX_VNCLEAN)) {
		if (bp->b_xflags & BX_VNDIRTY)
			listheadp = &vp->v_dirtyblkhd;
		else 
			listheadp = &vp->v_cleanblkhd;
		TAILQ_REMOVE(listheadp, bp, b_vnbufs);
		bp->b_xflags &= ~(BX_VNDIRTY | BX_VNCLEAN);
	}
	if ((vp->v_flag & VONWORKLST) && TAILQ_EMPTY(&vp->v_dirtyblkhd)) {
		vp->v_flag &= ~VONWORKLST;
		LIST_REMOVE(vp, v_synclist);
	}
	splx(s);
	bp->b_vp = (struct vnode *) 0;
	vdrop(vp);
}

/*
 * The workitem queue.
 * 
 * It is useful to delay writes of file data and filesystem metadata
 * for tens of seconds so that quickly created and deleted files need
 * not waste disk bandwidth being created and removed. To realize this,
 * we append vnodes to a "workitem" queue. When running with a soft
 * updates implementation, most pending metadata dependencies should
 * not wait for more than a few seconds. Thus, mounted on block devices
 * are delayed only about a half the time that file data is delayed.
 * Similarly, directory updates are more critical, so are only delayed
 * about a third the time that file data is delayed. Thus, there are
 * SYNCER_MAXDELAY queues that are processed round-robin at a rate of
 * one each second (driven off the filesystem syncer process). The
 * syncer_delayno variable indicates the next queue that is to be processed.
 * Items that need to be processed soon are placed in this queue:
 *
 *	syncer_workitem_pending[syncer_delayno]
 *
 * A delay of fifteen seconds is done by placing the request fifteen
 * entries later in the queue:
 *
 *	syncer_workitem_pending[(syncer_delayno + 15) & syncer_mask]
 *
 */

/*
 * Add an item to the syncer work queue.
 */
static void
vn_syncer_add_to_worklist(struct vnode *vp, int delay)
{
	int s, slot;

	s = splbio();

	if (vp->v_flag & VONWORKLST) {
		LIST_REMOVE(vp, v_synclist);
	}

	if (delay > syncer_maxdelay - 2)
		delay = syncer_maxdelay - 2;
	slot = (syncer_delayno + delay) & syncer_mask;

	LIST_INSERT_HEAD(&syncer_workitem_pending[slot], vp, v_synclist);
	vp->v_flag |= VONWORKLST;
	splx(s);
}

struct  thread *updatethread;
static void sched_sync (void);
static struct kproc_desc up_kp = {
	"syncer",
	sched_sync,
	&updatethread
};
SYSINIT(syncer, SI_SUB_KTHREAD_UPDATE, SI_ORDER_FIRST, kproc_start, &up_kp)

/*
 * System filesystem synchronizer daemon.
 */
void 
sched_sync(void)
{
	struct synclist *slp;
	struct vnode *vp;
	long starttime;
	int s;
	struct thread *td = curthread;

	EVENTHANDLER_REGISTER(shutdown_pre_sync, shutdown_kproc, td,
	    SHUTDOWN_PRI_LAST);   

	for (;;) {
		kproc_suspend_loop();

		starttime = time_second;

		/*
		 * Push files whose dirty time has expired.  Be careful
		 * of interrupt race on slp queue.
		 */
		s = splbio();
		slp = &syncer_workitem_pending[syncer_delayno];
		syncer_delayno += 1;
		if (syncer_delayno == syncer_maxdelay)
			syncer_delayno = 0;
		splx(s);

		while ((vp = LIST_FIRST(slp)) != NULL) {
			if (VOP_ISLOCKED(vp, NULL) == 0) {
				vn_lock(vp, LK_EXCLUSIVE | LK_RETRY, td);
				(void) VOP_FSYNC(vp, MNT_LAZY, td);
				VOP_UNLOCK(vp, 0, td);
			}
			s = splbio();
			if (LIST_FIRST(slp) == vp) {
				/*
				 * Note: v_tag VT_VFS vps can remain on the
				 * worklist too with no dirty blocks, but 
				 * since sync_fsync() moves it to a different 
				 * slot we are safe.
				 */
				if (TAILQ_EMPTY(&vp->v_dirtyblkhd) &&
				    !vn_isdisk(vp, NULL))
					panic("sched_sync: fsync failed vp %p tag %d", vp, vp->v_tag);
				/*
				 * Put us back on the worklist.  The worklist
				 * routine will remove us from our current
				 * position and then add us back in at a later
				 * position.
				 */
				vn_syncer_add_to_worklist(vp, syncdelay);
			}
			splx(s);
		}

		/*
		 * Do soft update processing.
		 */
		if (bioops.io_sync)
			(*bioops.io_sync)(NULL);

		/*
		 * The variable rushjob allows the kernel to speed up the
		 * processing of the filesystem syncer process. A rushjob
		 * value of N tells the filesystem syncer to process the next
		 * N seconds worth of work on its queue ASAP. Currently rushjob
		 * is used by the soft update code to speed up the filesystem
		 * syncer process when the incore state is getting so far
		 * ahead of the disk that the kernel memory pool is being
		 * threatened with exhaustion.
		 */
		if (rushjob > 0) {
			rushjob -= 1;
			continue;
		}
		/*
		 * If it has taken us less than a second to process the
		 * current work, then wait. Otherwise start right over
		 * again. We can still lose time if any single round
		 * takes more than two seconds, but it does not really
		 * matter as we are just trying to generally pace the
		 * filesystem activity.
		 */
		if (time_second == starttime)
			tsleep(&lbolt, 0, "syncer", 0);
	}
}

/*
 * Request the syncer daemon to speed up its work.
 * We never push it to speed up more than half of its
 * normal turn time, otherwise it could take over the cpu.
 *
 * YYY wchan field protected by the BGL.
 */
int
speedup_syncer()
{
	crit_enter();
	if (updatethread->td_wchan == &lbolt) { /* YYY */
		unsleep(updatethread);
		lwkt_schedule(updatethread);
	}
	crit_exit();
	if (rushjob < syncdelay / 2) {
		rushjob += 1;
		stat_rush_requests += 1;
		return (1);
	}
	return(0);
}

/*
 * Associate a p-buffer with a vnode.
 *
 * Also sets B_PAGING flag to indicate that vnode is not fully associated
 * with the buffer.  i.e. the bp has not been linked into the vnode or
 * ref-counted.
 */
void
pbgetvp(vp, bp)
	struct vnode *vp;
	struct buf *bp;
{

	KASSERT(bp->b_vp == NULL, ("pbgetvp: not free"));

	bp->b_vp = vp;
	bp->b_flags |= B_PAGING;
	bp->b_dev = vn_todev(vp);
}

/*
 * Disassociate a p-buffer from a vnode.
 */
void
pbrelvp(bp)
	struct buf *bp;
{

	KASSERT(bp->b_vp != NULL, ("pbrelvp: NULL"));

	/* XXX REMOVE ME */
	if (TAILQ_NEXT(bp, b_vnbufs) != NULL) {
		panic(
		    "relpbuf(): b_vp was probably reassignbuf()d %p %x", 
		    bp,
		    (int)bp->b_flags
		);
	}
	bp->b_vp = (struct vnode *) 0;
	bp->b_flags &= ~B_PAGING;
}

void
pbreassignbuf(bp, newvp)
	struct buf *bp;
	struct vnode *newvp;
{
	if ((bp->b_flags & B_PAGING) == 0) {
		panic(
		    "pbreassignbuf() on non phys bp %p", 
		    bp
		);
	}
	bp->b_vp = newvp;
}

/*
 * Reassign a buffer from one vnode to another.
 * Used to assign file specific control information
 * (indirect blocks) to the vnode to which they belong.
 */
void
reassignbuf(bp, newvp)
	struct buf *bp;
	struct vnode *newvp;
{
	struct buflists *listheadp;
	int delay;
	int s;

	if (newvp == NULL) {
		printf("reassignbuf: NULL");
		return;
	}
	++reassignbufcalls;

	/*
	 * B_PAGING flagged buffers cannot be reassigned because their vp
	 * is not fully linked in.
	 */
	if (bp->b_flags & B_PAGING)
		panic("cannot reassign paging buffer");

	s = splbio();
	/*
	 * Delete from old vnode list, if on one.
	 */
	if (bp->b_xflags & (BX_VNDIRTY | BX_VNCLEAN)) {
		if (bp->b_xflags & BX_VNDIRTY)
			listheadp = &bp->b_vp->v_dirtyblkhd;
		else 
			listheadp = &bp->b_vp->v_cleanblkhd;
		TAILQ_REMOVE(listheadp, bp, b_vnbufs);
		bp->b_xflags &= ~(BX_VNDIRTY | BX_VNCLEAN);
		if (bp->b_vp != newvp) {
			vdrop(bp->b_vp);
			bp->b_vp = NULL;	/* for clarification */
		}
	}
	/*
	 * If dirty, put on list of dirty buffers; otherwise insert onto list
	 * of clean buffers.
	 */
	if (bp->b_flags & B_DELWRI) {
		struct buf *tbp;

		listheadp = &newvp->v_dirtyblkhd;
		if ((newvp->v_flag & VONWORKLST) == 0) {
			switch (newvp->v_type) {
			case VDIR:
				delay = dirdelay;
				break;
			case VCHR:
			case VBLK:
				if (newvp->v_specmountpoint != NULL) {
					delay = metadelay;
					break;
				}
				/* fall through */
			default:
				delay = filedelay;
			}
			vn_syncer_add_to_worklist(newvp, delay);
		}
		bp->b_xflags |= BX_VNDIRTY;
		tbp = TAILQ_FIRST(listheadp);
		if (tbp == NULL ||
		    bp->b_lblkno == 0 ||
		    (bp->b_lblkno > 0 && tbp->b_lblkno < 0) ||
		    (bp->b_lblkno > 0 && bp->b_lblkno < tbp->b_lblkno)) {
			TAILQ_INSERT_HEAD(listheadp, bp, b_vnbufs);
			++reassignbufsortgood;
		} else if (bp->b_lblkno < 0) {
			TAILQ_INSERT_TAIL(listheadp, bp, b_vnbufs);
			++reassignbufsortgood;
		} else if (reassignbufmethod == 1) {
			/*
			 * New sorting algorithm, only handle sequential case,
			 * otherwise append to end (but before metadata)
			 */
			if ((tbp = gbincore(newvp, bp->b_lblkno - 1)) != NULL &&
			    (tbp->b_xflags & BX_VNDIRTY)) {
				/*
				 * Found the best place to insert the buffer
				 */
				TAILQ_INSERT_AFTER(listheadp, tbp, bp, b_vnbufs);
				++reassignbufsortgood;
			} else {
				/*
				 * Missed, append to end, but before meta-data.
				 * We know that the head buffer in the list is
				 * not meta-data due to prior conditionals.
				 *
				 * Indirect effects:  NFS second stage write
				 * tends to wind up here, giving maximum 
				 * distance between the unstable write and the
				 * commit rpc.
				 */
				tbp = TAILQ_LAST(listheadp, buflists);
				while (tbp && tbp->b_lblkno < 0)
					tbp = TAILQ_PREV(tbp, buflists, b_vnbufs);
				TAILQ_INSERT_AFTER(listheadp, tbp, bp, b_vnbufs);
				++reassignbufsortbad;
			}
		} else {
			/*
			 * Old sorting algorithm, scan queue and insert
			 */
			struct buf *ttbp;
			while ((ttbp = TAILQ_NEXT(tbp, b_vnbufs)) &&
			    (ttbp->b_lblkno < bp->b_lblkno)) {
				++reassignbufloops;
				tbp = ttbp;
			}
			TAILQ_INSERT_AFTER(listheadp, tbp, bp, b_vnbufs);
		}
	} else {
		bp->b_xflags |= BX_VNCLEAN;
		TAILQ_INSERT_TAIL(&newvp->v_cleanblkhd, bp, b_vnbufs);
		if ((newvp->v_flag & VONWORKLST) &&
		    TAILQ_EMPTY(&newvp->v_dirtyblkhd)) {
			newvp->v_flag &= ~VONWORKLST;
			LIST_REMOVE(newvp, v_synclist);
		}
	}
	if (bp->b_vp != newvp) {
		bp->b_vp = newvp;
		vhold(bp->b_vp);
	}
	splx(s);
}

/*
 * Create a vnode for a block device.
 * Used for mounting the root file system.
 */
int
bdevvp(dev, vpp)
	dev_t dev;
	struct vnode **vpp;
{
	struct vnode *vp;
	struct vnode *nvp;
	int error;

	if (dev == NODEV) {
		*vpp = NULLVP;
		return (ENXIO);
	}
	error = getnewvnode(VT_NON, (struct mount *)0, spec_vnodeop_p, &nvp);
	if (error) {
		*vpp = NULLVP;
		return (error);
	}
	vp = nvp;
	vp->v_type = VBLK;
	addalias(vp, dev);
	*vpp = vp;
	return (0);
}

/*
 * Add a vnode to the alias list hung off the dev_t.
 *
 * The reason for this gunk is that multiple vnodes can reference
 * the same physical device, so checking vp->v_usecount to see
 * how many users there are is inadequate; the v_usecount for
 * the vnodes need to be accumulated.  vcount() does that.
 */
void
addaliasu(struct vnode *nvp, udev_t nvp_rdev)
{
	dev_t dev;

	if (nvp->v_type != VBLK && nvp->v_type != VCHR)
		panic("addaliasu on non-special vnode");
	dev = udev2dev(nvp_rdev, nvp->v_type == VBLK ? 1 : 0);
	if (dev != NODEV) {
		nvp->v_rdev = dev;
		addalias(nvp, dev);
	} else
		nvp->v_rdev = NULL;
}

void
addalias(struct vnode *nvp, dev_t dev)
{

	if (nvp->v_type != VBLK && nvp->v_type != VCHR)
		panic("addalias on non-special vnode");

	nvp->v_rdev = dev;
	lwkt_gettoken(&spechash_token);
	SLIST_INSERT_HEAD(&dev->si_hlist, nvp, v_specnext);
	lwkt_reltoken(&spechash_token);
}

/*
 * Grab a particular vnode from the free list, increment its
 * reference count and lock it. The vnode lock bit is set if the
 * vnode is being eliminated in vgone. The process is awakened
 * when the transition is completed, and an error returned to
 * indicate that the vnode is no longer usable (possibly having
 * been changed to a new file system type).
 */
int
vget(vp, flags, td)
	struct vnode *vp;
	int flags;
	struct thread *td;
{
	int error;

	/*
	 * If the vnode is in the process of being cleaned out for
	 * another use, we wait for the cleaning to finish and then
	 * return failure. Cleaning is determined by checking that
	 * the VXLOCK flag is set.
	 */
	if (vp->v_flag & VXLOCK) {
		if (vp->v_vxproc == curproc) {
#if 0
			/* this can now occur in normal operation */
			log(LOG_INFO, "VXLOCK interlock avoided\n");
#endif
		} else {
			vp->v_flag |= VXWANT;
			tsleep((caddr_t)vp, 0, "vget", 0);
			return (ENOENT);
		}
	}

	/*
	 * Bump v_usecount to prevent the vnode from being cleaned.  If the
	 * vnode gets cleaned unexpectedly we could wind up calling lockmgr
	 * on a lock embedded in an inode which is then ripped out from
	 * it.
	 */
	vp->v_usecount++;	/* XXX MP */

	if ((flags & LK_INTERLOCK) == 0) {
		lwkt_gettoken(&vp->v_interlock);
	}

	if (VSHOULDBUSY(vp))
		vbusy(vp);
	if (flags & LK_TYPE_MASK) {
		if ((error = vn_lock(vp, flags | LK_INTERLOCK, td)) != 0) {
			/*
			 * must expand vrele here because we do not want
			 * to call VOP_INACTIVE if the reference count
			 * drops back to zero since it was never really
			 * active. We must remove it from the free list
			 * before sleeping so that multiple processes do
			 * not try to recycle it.
			 */
			lwkt_gettoken(&vp->v_interlock);
			vp->v_usecount--;
			if (VSHOULDFREE(vp))
				vfree(vp);
			else
				vlruvp(vp);
			lwkt_reltoken(&vp->v_interlock);
		}
		return (error);
	}
	lwkt_reltoken(&vp->v_interlock);
	return (0);
}

void
vref(struct vnode *vp)
{
	vp->v_usecount++;	/* XXX MP */
}

/*
 * Vnode put/release.
 * If count drops to zero, call inactive routine and return to freelist.
 */
void
vrele(struct vnode *vp)
{
	struct thread *td = curthread;	/* XXX */

	KASSERT(vp != NULL, ("vrele: null vp"));

	lwkt_gettoken(&vp->v_interlock);

	if (vp->v_usecount > 1) {

		vp->v_usecount--;
		lwkt_reltoken(&vp->v_interlock);

		return;
	}

	if (vp->v_usecount == 1) {
		vp->v_usecount--;
		/*
		 * We must call VOP_INACTIVE with the node locked.
		 * If we are doing a vpu, the node is already locked,
		 * but, in the case of vrele, we must explicitly lock
		 * the vnode before calling VOP_INACTIVE
		 */

		if (vn_lock(vp, LK_EXCLUSIVE | LK_INTERLOCK, td) == 0)
			VOP_INACTIVE(vp, td);
		if (VSHOULDFREE(vp))
			vfree(vp);
		else
			vlruvp(vp);
	} else {
#ifdef DIAGNOSTIC
		vprint("vrele: negative ref count", vp);
		lwkt_reltoken(&vp->v_interlock);
#endif
		panic("vrele: negative ref cnt");
	}
}

void
vput(struct vnode *vp)
{
	struct thread *td = curthread;	/* XXX */

	KASSERT(vp != NULL, ("vput: null vp"));

	lwkt_gettoken(&vp->v_interlock);

	if (vp->v_usecount > 1) {
		vp->v_usecount--;
		VOP_UNLOCK(vp, LK_INTERLOCK, td);
		return;
	}

	if (vp->v_usecount == 1) {
		vp->v_usecount--;
		/*
		 * We must call VOP_INACTIVE with the node locked.
		 * If we are doing a vpu, the node is already locked,
		 * so we just need to release the vnode mutex.
		 */
		lwkt_reltoken(&vp->v_interlock);
		VOP_INACTIVE(vp, td);
		if (VSHOULDFREE(vp))
			vfree(vp);
		else
			vlruvp(vp);
	} else {
#ifdef DIAGNOSTIC
		vprint("vput: negative ref count", vp);
#endif
		panic("vput: negative ref cnt");
	}
}

/*
 * Somebody doesn't want the vnode recycled.
 */
void
vhold(vp)
	struct vnode *vp;
{
	int s;

  	s = splbio();
	vp->v_holdcnt++;
	if (VSHOULDBUSY(vp))
		vbusy(vp);
	splx(s);
}

/*
 * One less who cares about this vnode.
 */
void
vdrop(vp)
	struct vnode *vp;
{
	int s;

	s = splbio();
	if (vp->v_holdcnt <= 0)
		panic("vdrop: holdcnt");
	vp->v_holdcnt--;
	if (VSHOULDFREE(vp))
		vfree(vp);
	splx(s);
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

int
vflush(mp, rootrefs, flags)
	struct mount *mp;
	int rootrefs;
	int flags;
{
	struct thread *td = curthread;	/* XXX */
	struct vnode *vp, *nvp, *rootvp = NULL;
	struct vattr vattr;
	int busy = 0, error;

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
	lwkt_gettoken(&mntvnode_token);
loop:
	for (vp = TAILQ_FIRST(&mp->mnt_nvnodelist); vp; vp = nvp) {
		/*
		 * Make sure this vnode wasn't reclaimed in getnewvnode().
		 * Start over if it has (it won't be on the list anymore).
		 */
		if (vp->v_mount != mp)
			goto loop;
		nvp = TAILQ_NEXT(vp, v_nmntvnodes);

		lwkt_gettoken(&vp->v_interlock);
		/*
		 * Skip over a vnodes marked VSYSTEM.
		 */
		if ((flags & SKIPSYSTEM) && (vp->v_flag & VSYSTEM)) {
			lwkt_reltoken(&vp->v_interlock);
			continue;
		}
		/*
		 * If WRITECLOSE is set, flush out unlinked but still open
		 * files (even if open only for reading) and regular file
		 * vnodes open for writing. 
		 */
		if ((flags & WRITECLOSE) &&
		    (vp->v_type == VNON ||
		    (VOP_GETATTR(vp, &vattr, td) == 0 &&
		    vattr.va_nlink > 0)) &&
		    (vp->v_writecount == 0 || vp->v_type != VREG)) {
			lwkt_reltoken(&vp->v_interlock);
			continue;
		}

		/*
		 * With v_usecount == 0, all we need to do is clear out the
		 * vnode data structures and we are done.
		 */
		if (vp->v_usecount == 0) {
			lwkt_reltoken(&mntvnode_token);
			vgonel(vp, td);
			lwkt_gettoken(&mntvnode_token);
			continue;
		}

		/*
		 * If FORCECLOSE is set, forcibly close the vnode. For block
		 * or character devices, revert to an anonymous device. For
		 * all other files, just kill them.
		 */
		if (flags & FORCECLOSE) {
			lwkt_reltoken(&mntvnode_token);
			if (vp->v_type != VBLK && vp->v_type != VCHR) {
				vgonel(vp, td);
			} else {
				vclean(vp, 0, td);
				vp->v_op = spec_vnodeop_p;
				insmntque(vp, (struct mount *) 0);
			}
			lwkt_gettoken(&mntvnode_token);
			continue;
		}
#ifdef DIAGNOSTIC
		if (busyprt)
			vprint("vflush: busy vnode", vp);
#endif
		lwkt_reltoken(&vp->v_interlock);
		busy++;
	}
	lwkt_reltoken(&mntvnode_token);
	if (rootrefs > 0 && (flags & FORCECLOSE) == 0) {
		/*
		 * If just the root vnode is busy, and if its refcount
		 * is equal to `rootrefs', then go ahead and kill it.
		 */
		lwkt_gettoken(&rootvp->v_interlock);
		KASSERT(busy > 0, ("vflush: not busy"));
		KASSERT(rootvp->v_usecount >= rootrefs, ("vflush: rootrefs"));
		if (busy == 1 && rootvp->v_usecount == rootrefs) {
			vgonel(rootvp, td);
			busy = 0;
		} else
			lwkt_reltoken(&rootvp->v_interlock);
	}
	if (busy)
		return (EBUSY);
	for (; rootrefs > 0; rootrefs--)
		vrele(rootvp);
	return (0);
}

/*
 * We do not want to recycle the vnode too quickly.
 *
 * XXX we can't move vp's around the nvnodelist without really screwing
 * up the efficiency of filesystem SYNC and friends.  This code is 
 * disabled until we fix the syncing code's scanning algorithm.
 */
static void
vlruvp(struct vnode *vp)
{
#if 0
	struct mount *mp;

	if ((mp = vp->v_mount) != NULL) {
		lwkt_gettoken(&mntvnode_token);
		TAILQ_REMOVE(&mp->mnt_nvnodelist, vp, v_nmntvnodes);
		TAILQ_INSERT_TAIL(&mp->mnt_nvnodelist, vp, v_nmntvnodes);
		lwkt_reltoken(&mntvnode_token);
	}
#endif
}

/*
 * Disassociate the underlying file system from a vnode.
 */
static void
vclean(struct vnode *vp, int flags, struct thread *td)
{
	int active;

	/*
	 * Check to see if the vnode is in use. If so we have to reference it
	 * before we clean it out so that its count cannot fall to zero and
	 * generate a race against ourselves to recycle it.
	 */
	if ((active = vp->v_usecount))
		vp->v_usecount++;

	/*
	 * Prevent the vnode from being recycled or brought into use while we
	 * clean it out.
	 */
	if (vp->v_flag & VXLOCK)
		panic("vclean: deadlock");
	vp->v_flag |= VXLOCK;
	vp->v_vxproc = curproc;
	/*
	 * Even if the count is zero, the VOP_INACTIVE routine may still
	 * have the object locked while it cleans it out. The VOP_LOCK
	 * ensures that the VOP_INACTIVE routine is done with its work.
	 * For active vnodes, it ensures that no other activity can
	 * occur while the underlying object is being cleaned out.
	 */
	VOP_LOCK(vp, LK_DRAIN | LK_INTERLOCK, td);

	/*
	 * Clean out any buffers associated with the vnode.
	 */
	vinvalbuf(vp, V_SAVE, td, 0, 0);

	VOP_DESTROYVOBJECT(vp);

	/*
	 * If purging an active vnode, it must be closed and
	 * deactivated before being reclaimed. Note that the
	 * VOP_INACTIVE will unlock the vnode.
	 */
	if (active) {
		if (flags & DOCLOSE)
			VOP_CLOSE(vp, FNONBLOCK, td);
		VOP_INACTIVE(vp, td);
	} else {
		/*
		 * Any other processes trying to obtain this lock must first
		 * wait for VXLOCK to clear, then call the new lock operation.
		 */
		VOP_UNLOCK(vp, 0, td);
	}
	/*
	 * Reclaim the vnode.
	 */
	if (VOP_RECLAIM(vp, td))
		panic("vclean: cannot reclaim");

	if (active) {
		/*
		 * Inline copy of vrele() since VOP_INACTIVE
		 * has already been called.
		 */
		lwkt_gettoken(&vp->v_interlock);
		if (--vp->v_usecount <= 0) {
#ifdef DIAGNOSTIC
			if (vp->v_usecount < 0 || vp->v_writecount != 0) {
				vprint("vclean: bad ref count", vp);
				panic("vclean: ref cnt");
			}
#endif
			vfree(vp);
		}
		lwkt_reltoken(&vp->v_interlock);
	}

	cache_purge(vp);
	vp->v_vnlock = NULL;

	if (VSHOULDFREE(vp))
		vfree(vp);
	
	/*
	 * Done with purge, notify sleepers of the grim news.
	 */
	vp->v_op = dead_vnodeop_p;
	vn_pollgone(vp);
	vp->v_tag = VT_NON;
	vp->v_flag &= ~VXLOCK;
	vp->v_vxproc = NULL;
	if (vp->v_flag & VXWANT) {
		vp->v_flag &= ~VXWANT;
		wakeup((caddr_t) vp);
	}
}

/*
 * Eliminate all activity associated with the requested vnode
 * and with all vnodes aliased to the requested vnode.
 */
int
vop_revoke(ap)
	struct vop_revoke_args /* {
		struct vnode *a_vp;
		int a_flags;
	} */ *ap;
{
	struct vnode *vp, *vq;
	dev_t dev;

	KASSERT((ap->a_flags & REVOKEALL) != 0, ("vop_revoke"));

	vp = ap->a_vp;
	/*
	 * If a vgone (or vclean) is already in progress,
	 * wait until it is done and return.
	 */
	if (vp->v_flag & VXLOCK) {
		vp->v_flag |= VXWANT;
		lwkt_reltoken(&vp->v_interlock);
		tsleep((caddr_t)vp, 0, "vop_revokeall", 0);
		return (0);
	}
	dev = vp->v_rdev;
	for (;;) {
		lwkt_gettoken(&spechash_token);
		vq = SLIST_FIRST(&dev->si_hlist);
		lwkt_reltoken(&spechash_token);
		if (!vq)
			break;
		vgone(vq);
	}
	return (0);
}

/*
 * Recycle an unused vnode to the front of the free list.
 * Release the passed interlock if the vnode will be recycled.
 */
int
vrecycle(struct vnode *vp, struct lwkt_token *inter_lkp, struct thread *td)
{
	lwkt_gettoken(&vp->v_interlock);
	if (vp->v_usecount == 0) {
		if (inter_lkp) {
			lwkt_reltoken(inter_lkp);
		}
		vgonel(vp, td);
		return (1);
	}
	lwkt_reltoken(&vp->v_interlock);
	return (0);
}

/*
 * Eliminate all activity associated with a vnode
 * in preparation for reuse.
 */
void
vgone(struct vnode *vp)
{
	struct thread *td = curthread;	/* XXX */

	lwkt_gettoken(&vp->v_interlock);
	vgonel(vp, td);
}

/*
 * vgone, with the vp interlock held.
 */
void
vgonel(struct vnode *vp, struct thread *td)
{
	int s;

	/*
	 * If a vgone (or vclean) is already in progress,
	 * wait until it is done and return.
	 */
	if (vp->v_flag & VXLOCK) {
		vp->v_flag |= VXWANT;
		lwkt_reltoken(&vp->v_interlock);
		tsleep((caddr_t)vp, 0, "vgone", 0);
		return;
	}

	/*
	 * Clean out the filesystem specific data.
	 */
	vclean(vp, DOCLOSE, td);
	lwkt_gettoken(&vp->v_interlock);

	/*
	 * Delete from old mount point vnode list, if on one.
	 */
	if (vp->v_mount != NULL)
		insmntque(vp, (struct mount *)0);
	/*
	 * If special device, remove it from special device alias list
	 * if it is on one.
	 */
	if ((vp->v_type == VBLK || vp->v_type == VCHR) && vp->v_rdev != NULL) {
		lwkt_gettoken(&spechash_token);
		SLIST_REMOVE(&vp->v_hashchain, vp, vnode, v_specnext);
		freedev(vp->v_rdev);
		lwkt_reltoken(&spechash_token);
		vp->v_rdev = NULL;
	}

	/*
	 * If it is on the freelist and not already at the head,
	 * move it to the head of the list. The test of the
	 * VDOOMED flag and the reference count of zero is because
	 * it will be removed from the free list by getnewvnode,
	 * but will not have its reference count incremented until
	 * after calling vgone. If the reference count were
	 * incremented first, vgone would (incorrectly) try to
	 * close the previous instance of the underlying object.
	 */
	if (vp->v_usecount == 0 && !(vp->v_flag & VDOOMED)) {
		s = splbio();
		lwkt_gettoken(&vnode_free_list_token);
		if (vp->v_flag & VFREE)
			TAILQ_REMOVE(&vnode_free_list, vp, v_freelist);
		else
			freevnodes++;
		vp->v_flag |= VFREE;
		TAILQ_INSERT_HEAD(&vnode_free_list, vp, v_freelist);
		lwkt_reltoken(&vnode_free_list_token);
		splx(s);
	}

	vp->v_type = VBAD;
	lwkt_reltoken(&vp->v_interlock);
}

/*
 * Lookup a vnode by device number.
 */
int
vfinddev(dev, type, vpp)
	dev_t dev;
	enum vtype type;
	struct vnode **vpp;
{
	struct vnode *vp;

	lwkt_gettoken(&spechash_token);
	SLIST_FOREACH(vp, &dev->si_hlist, v_specnext) {
		if (type == vp->v_type) {
			*vpp = vp;
			lwkt_reltoken(&spechash_token);
			return (1);
		}
	}
	lwkt_reltoken(&spechash_token);
	return (0);
}

/*
 * Calculate the total number of references to a special device.
 */
int
vcount(vp)
	struct vnode *vp;
{
	struct vnode *vq;
	int count;

	count = 0;
	lwkt_gettoken(&spechash_token);
	SLIST_FOREACH(vq, &vp->v_hashchain, v_specnext)
		count += vq->v_usecount;
	lwkt_reltoken(&spechash_token);
	return (count);
}

/*
 * Same as above, but using the dev_t as argument
 */

int
count_dev(dev)
	dev_t dev;
{
	struct vnode *vp;

	vp = SLIST_FIRST(&dev->si_hlist);
	if (vp == NULL)
		return (0);
	return(vcount(vp));
}

/*
 * Print out a description of a vnode.
 */
static char *typename[] =
{"VNON", "VREG", "VDIR", "VBLK", "VCHR", "VLNK", "VSOCK", "VFIFO", "VBAD"};

void
vprint(label, vp)
	char *label;
	struct vnode *vp;
{
	char buf[96];

	if (label != NULL)
		printf("%s: %p: ", label, (void *)vp);
	else
		printf("%p: ", (void *)vp);
	printf("type %s, usecount %d, writecount %d, refcount %d,",
	    typename[vp->v_type], vp->v_usecount, vp->v_writecount,
	    vp->v_holdcnt);
	buf[0] = '\0';
	if (vp->v_flag & VROOT)
		strcat(buf, "|VROOT");
	if (vp->v_flag & VTEXT)
		strcat(buf, "|VTEXT");
	if (vp->v_flag & VSYSTEM)
		strcat(buf, "|VSYSTEM");
	if (vp->v_flag & VXLOCK)
		strcat(buf, "|VXLOCK");
	if (vp->v_flag & VXWANT)
		strcat(buf, "|VXWANT");
	if (vp->v_flag & VBWAIT)
		strcat(buf, "|VBWAIT");
	if (vp->v_flag & VDOOMED)
		strcat(buf, "|VDOOMED");
	if (vp->v_flag & VFREE)
		strcat(buf, "|VFREE");
	if (vp->v_flag & VOBJBUF)
		strcat(buf, "|VOBJBUF");
	if (buf[0] != '\0')
		printf(" flags (%s)", &buf[1]);
	if (vp->v_data == NULL) {
		printf("\n");
	} else {
		printf("\n\t");
		VOP_PRINT(vp);
	}
}

#ifdef DDB
#include <ddb/ddb.h>
/*
 * List all of the locked vnodes in the system.
 * Called when debugging the kernel.
 */
DB_SHOW_COMMAND(lockedvnodes, lockedvnodes)
{
	struct thread *td = curthread;	/* XXX */
	struct mount *mp, *nmp;
	struct vnode *vp;

	printf("Locked vnodes\n");
	lwkt_gettoken(&mountlist_token);
	for (mp = TAILQ_FIRST(&mountlist); mp != NULL; mp = nmp) {
		if (vfs_busy(mp, LK_NOWAIT, &mountlist_token, td)) {
			nmp = TAILQ_NEXT(mp, mnt_list);
			continue;
		}
		TAILQ_FOREACH(vp, &mp->mnt_nvnodelist, v_nmntvnodes) {
			if (VOP_ISLOCKED(vp, NULL))
				vprint((char *)0, vp);
		}
		lwkt_gettoken(&mountlist_token);
		nmp = TAILQ_NEXT(mp, mnt_list);
		vfs_unbusy(mp, td);
	}
	lwkt_reltoken(&mountlist_token);
}
#endif

/*
 * Top level filesystem related information gathering.
 */
static int	sysctl_ovfs_conf (SYSCTL_HANDLER_ARGS);

static int
vfs_sysctl(SYSCTL_HANDLER_ARGS)
{
	int *name = (int *)arg1 - 1;	/* XXX */
	u_int namelen = arg2 + 1;	/* XXX */
	struct vfsconf *vfsp;

#if 1 || defined(COMPAT_PRELITE2)
	/* Resolve ambiguity between VFS_VFSCONF and VFS_GENERIC. */
	if (namelen == 1)
		return (sysctl_ovfs_conf(oidp, arg1, arg2, req));
#endif

#ifdef notyet
	/* all sysctl names at this level are at least name and field */
	if (namelen < 2)
		return (ENOTDIR);		/* overloaded */
	if (name[0] != VFS_GENERIC) {
		for (vfsp = vfsconf; vfsp; vfsp = vfsp->vfc_next)
			if (vfsp->vfc_typenum == name[0])
				break;
		if (vfsp == NULL)
			return (EOPNOTSUPP);
		return ((*vfsp->vfc_vfsops->vfs_sysctl)(&name[1], namelen - 1,
		    oldp, oldlenp, newp, newlen, p));
	}
#endif
	switch (name[1]) {
	case VFS_MAXTYPENUM:
		if (namelen != 2)
			return (ENOTDIR);
		return (SYSCTL_OUT(req, &maxvfsconf, sizeof(int)));
	case VFS_CONF:
		if (namelen != 3)
			return (ENOTDIR);	/* overloaded */
		for (vfsp = vfsconf; vfsp; vfsp = vfsp->vfc_next)
			if (vfsp->vfc_typenum == name[2])
				break;
		if (vfsp == NULL)
			return (EOPNOTSUPP);
		return (SYSCTL_OUT(req, vfsp, sizeof *vfsp));
	}
	return (EOPNOTSUPP);
}

SYSCTL_NODE(_vfs, VFS_GENERIC, generic, CTLFLAG_RD, vfs_sysctl,
	"Generic filesystem");

#if 1 || defined(COMPAT_PRELITE2)

static int
sysctl_ovfs_conf(SYSCTL_HANDLER_ARGS)
{
	int error;
	struct vfsconf *vfsp;
	struct ovfsconf ovfs;

	for (vfsp = vfsconf; vfsp; vfsp = vfsp->vfc_next) {
		ovfs.vfc_vfsops = vfsp->vfc_vfsops;	/* XXX used as flag */
		strcpy(ovfs.vfc_name, vfsp->vfc_name);
		ovfs.vfc_index = vfsp->vfc_typenum;
		ovfs.vfc_refcount = vfsp->vfc_refcount;
		ovfs.vfc_flags = vfsp->vfc_flags;
		error = SYSCTL_OUT(req, &ovfs, sizeof ovfs);
		if (error)
			return error;
	}
	return 0;
}

#endif /* 1 || COMPAT_PRELITE2 */

#if 0
#define KINFO_VNODESLOP	10
/*
 * Dump vnode list (via sysctl).
 * Copyout address of vnode followed by vnode.
 */
/* ARGSUSED */
static int
sysctl_vnode(SYSCTL_HANDLER_ARGS)
{
	struct proc *p = curproc;	/* XXX */
	struct mount *mp, *nmp;
	struct vnode *nvp, *vp;
	int error;

#define VPTRSZ	sizeof (struct vnode *)
#define VNODESZ	sizeof (struct vnode)

	req->lock = 0;
	if (!req->oldptr) /* Make an estimate */
		return (SYSCTL_OUT(req, 0,
			(numvnodes + KINFO_VNODESLOP) * (VPTRSZ + VNODESZ)));

	lwkt_gettoken(&mountlist_token);
	for (mp = TAILQ_FIRST(&mountlist); mp != NULL; mp = nmp) {
		if (vfs_busy(mp, LK_NOWAIT, &mountlist_token, p)) {
			nmp = TAILQ_NEXT(mp, mnt_list);
			continue;
		}
again:
		lwkt_gettoken(&mntvnode_token);
		for (vp = TAILQ_FIRST(&mp->mnt_nvnodelist);
		     vp != NULL;
		     vp = nvp) {
			/*
			 * Check that the vp is still associated with
			 * this filesystem.  RACE: could have been
			 * recycled onto the same filesystem.
			 */
			if (vp->v_mount != mp) {
				lwkt_reltoken(&mntvnode_token);
				goto again;
			}
			nvp = TAILQ_NEXT(vp, v_nmntvnodes);
			lwkt_reltoken(&mntvnode_token);
			if ((error = SYSCTL_OUT(req, &vp, VPTRSZ)) ||
			    (error = SYSCTL_OUT(req, vp, VNODESZ)))
				return (error);
			lwkt_gettoken(&mntvnode_token);
		}
		lwkt_reltoken(&mntvnode_token);
		lwkt_gettoken(&mountlist_token);
		nmp = TAILQ_NEXT(mp, mnt_list);
		vfs_unbusy(mp, p);
	}
	lwkt_reltoken(&mountlist_token);

	return (0);
}
#endif

/*
 * XXX
 * Exporting the vnode list on large systems causes them to crash.
 * Exporting the vnode list on medium systems causes sysctl to coredump.
 */
#if 0
SYSCTL_PROC(_kern, KERN_VNODE, vnode, CTLTYPE_OPAQUE|CTLFLAG_RD,
	0, 0, sysctl_vnode, "S,vnode", "");
#endif

/*
 * Check to see if a filesystem is mounted on a block device.
 */
int
vfs_mountedon(vp)
	struct vnode *vp;
{

	if (vp->v_specmountpoint != NULL)
		return (EBUSY);
	return (0);
}

/*
 * Unmount all filesystems. The list is traversed in reverse order
 * of mounting to avoid dependencies.
 */
void
vfs_unmountall()
{
	struct mount *mp;
	struct thread *td = curthread;
	int error;

	if (td->td_proc == NULL)
		td = initproc->p_thread;	/* XXX XXX use proc0 instead? */

	/*
	 * Since this only runs when rebooting, it is not interlocked.
	 */
	while(!TAILQ_EMPTY(&mountlist)) {
		mp = TAILQ_LAST(&mountlist, mntlist);
		error = dounmount(mp, MNT_FORCE, td);
		if (error) {
			TAILQ_REMOVE(&mountlist, mp, mnt_list);
			printf("unmount of %s failed (",
			    mp->mnt_stat.f_mntonname);
			if (error == EBUSY)
				printf("BUSY)\n");
			else
				printf("%d)\n", error);
		} else {
			/* The unmount has removed mp from the mountlist */
		}
	}
}

/*
 * Build hash lists of net addresses and hang them off the mount point.
 * Called by ufs_mount() to set up the lists of export addresses.
 */
static int
vfs_hang_addrlist(mp, nep, argp)
	struct mount *mp;
	struct netexport *nep;
	struct export_args *argp;
{
	struct netcred *np;
	struct radix_node_head *rnh;
	int i;
	struct radix_node *rn;
	struct sockaddr *saddr, *smask = 0;
	struct domain *dom;
	int error;

	if (argp->ex_addrlen == 0) {
		if (mp->mnt_flag & MNT_DEFEXPORTED)
			return (EPERM);
		np = &nep->ne_defexported;
		np->netc_exflags = argp->ex_flags;
		np->netc_anon = argp->ex_anon;
		np->netc_anon.cr_ref = 1;
		mp->mnt_flag |= MNT_DEFEXPORTED;
		return (0);
	}

	if (argp->ex_addrlen < 0 || argp->ex_addrlen > MLEN)
		return (EINVAL);
	if (argp->ex_masklen < 0 || argp->ex_masklen > MLEN)
		return (EINVAL);

	i = sizeof(struct netcred) + argp->ex_addrlen + argp->ex_masklen;
	np = (struct netcred *) malloc(i, M_NETADDR, M_WAITOK);
	bzero((caddr_t) np, i);
	saddr = (struct sockaddr *) (np + 1);
	if ((error = copyin(argp->ex_addr, (caddr_t) saddr, argp->ex_addrlen)))
		goto out;
	if (saddr->sa_len > argp->ex_addrlen)
		saddr->sa_len = argp->ex_addrlen;
	if (argp->ex_masklen) {
		smask = (struct sockaddr *) ((caddr_t) saddr + argp->ex_addrlen);
		error = copyin(argp->ex_mask, (caddr_t) smask, argp->ex_masklen);
		if (error)
			goto out;
		if (smask->sa_len > argp->ex_masklen)
			smask->sa_len = argp->ex_masklen;
	}
	i = saddr->sa_family;
	if ((rnh = nep->ne_rtable[i]) == 0) {
		/*
		 * Seems silly to initialize every AF when most are not used,
		 * do so on demand here
		 */
		for (dom = domains; dom; dom = dom->dom_next)
			if (dom->dom_family == i && dom->dom_rtattach) {
				dom->dom_rtattach((void **) &nep->ne_rtable[i],
				    dom->dom_rtoffset);
				break;
			}
		if ((rnh = nep->ne_rtable[i]) == 0) {
			error = ENOBUFS;
			goto out;
		}
	}
	rn = (*rnh->rnh_addaddr) ((caddr_t) saddr, (caddr_t) smask, rnh,
	    np->netc_rnodes);
	if (rn == 0 || np != (struct netcred *) rn) {	/* already exists */
		error = EPERM;
		goto out;
	}
	np->netc_exflags = argp->ex_flags;
	np->netc_anon = argp->ex_anon;
	np->netc_anon.cr_ref = 1;
	return (0);
out:
	free(np, M_NETADDR);
	return (error);
}

/* ARGSUSED */
static int
vfs_free_netcred(rn, w)
	struct radix_node *rn;
	void *w;
{
	struct radix_node_head *rnh = (struct radix_node_head *) w;

	(*rnh->rnh_deladdr) (rn->rn_key, rn->rn_mask, rnh);
	free((caddr_t) rn, M_NETADDR);
	return (0);
}

/*
 * Free the net address hash lists that are hanging off the mount points.
 */
static void
vfs_free_addrlist(nep)
	struct netexport *nep;
{
	int i;
	struct radix_node_head *rnh;

	for (i = 0; i <= AF_MAX; i++)
		if ((rnh = nep->ne_rtable[i])) {
			(*rnh->rnh_walktree) (rnh, vfs_free_netcred,
			    (caddr_t) rnh);
			free((caddr_t) rnh, M_RTABLE);
			nep->ne_rtable[i] = 0;
		}
}

int
vfs_export(mp, nep, argp)
	struct mount *mp;
	struct netexport *nep;
	struct export_args *argp;
{
	int error;

	if (argp->ex_flags & MNT_DELEXPORT) {
		if (mp->mnt_flag & MNT_EXPUBLIC) {
			vfs_setpublicfs(NULL, NULL, NULL);
			mp->mnt_flag &= ~MNT_EXPUBLIC;
		}
		vfs_free_addrlist(nep);
		mp->mnt_flag &= ~(MNT_EXPORTED | MNT_DEFEXPORTED);
	}
	if (argp->ex_flags & MNT_EXPORTED) {
		if (argp->ex_flags & MNT_EXPUBLIC) {
			if ((error = vfs_setpublicfs(mp, nep, argp)) != 0)
				return (error);
			mp->mnt_flag |= MNT_EXPUBLIC;
		}
		if ((error = vfs_hang_addrlist(mp, nep, argp)))
			return (error);
		mp->mnt_flag |= MNT_EXPORTED;
	}
	return (0);
}


/*
 * Set the publicly exported filesystem (WebNFS). Currently, only
 * one public filesystem is possible in the spec (RFC 2054 and 2055)
 */
int
vfs_setpublicfs(mp, nep, argp)
	struct mount *mp;
	struct netexport *nep;
	struct export_args *argp;
{
	int error;
	struct vnode *rvp;
	char *cp;

	/*
	 * mp == NULL -> invalidate the current info, the FS is
	 * no longer exported. May be called from either vfs_export
	 * or unmount, so check if it hasn't already been done.
	 */
	if (mp == NULL) {
		if (nfs_pub.np_valid) {
			nfs_pub.np_valid = 0;
			if (nfs_pub.np_index != NULL) {
				FREE(nfs_pub.np_index, M_TEMP);
				nfs_pub.np_index = NULL;
			}
		}
		return (0);
	}

	/*
	 * Only one allowed at a time.
	 */
	if (nfs_pub.np_valid != 0 && mp != nfs_pub.np_mount)
		return (EBUSY);

	/*
	 * Get real filehandle for root of exported FS.
	 */
	bzero((caddr_t)&nfs_pub.np_handle, sizeof(nfs_pub.np_handle));
	nfs_pub.np_handle.fh_fsid = mp->mnt_stat.f_fsid;

	if ((error = VFS_ROOT(mp, &rvp)))
		return (error);

	if ((error = VFS_VPTOFH(rvp, &nfs_pub.np_handle.fh_fid)))
		return (error);

	vput(rvp);

	/*
	 * If an indexfile was specified, pull it in.
	 */
	if (argp->ex_indexfile != NULL) {
		MALLOC(nfs_pub.np_index, char *, MAXNAMLEN + 1, M_TEMP,
		    M_WAITOK);
		error = copyinstr(argp->ex_indexfile, nfs_pub.np_index,
		    MAXNAMLEN, (size_t *)0);
		if (!error) {
			/*
			 * Check for illegal filenames.
			 */
			for (cp = nfs_pub.np_index; *cp; cp++) {
				if (*cp == '/') {
					error = EINVAL;
					break;
				}
			}
		}
		if (error) {
			FREE(nfs_pub.np_index, M_TEMP);
			return (error);
		}
	}

	nfs_pub.np_mount = mp;
	nfs_pub.np_valid = 1;
	return (0);
}

struct netcred *
vfs_export_lookup(mp, nep, nam)
	struct mount *mp;
	struct netexport *nep;
	struct sockaddr *nam;
{
	struct netcred *np;
	struct radix_node_head *rnh;
	struct sockaddr *saddr;

	np = NULL;
	if (mp->mnt_flag & MNT_EXPORTED) {
		/*
		 * Lookup in the export list first.
		 */
		if (nam != NULL) {
			saddr = nam;
			rnh = nep->ne_rtable[saddr->sa_family];
			if (rnh != NULL) {
				np = (struct netcred *)
					(*rnh->rnh_matchaddr)((caddr_t)saddr,
							      rnh);
				if (np && np->netc_rnodes->rn_flags & RNF_ROOT)
					np = NULL;
			}
		}
		/*
		 * If no address match, use the default if it exists.
		 */
		if (np == NULL && mp->mnt_flag & MNT_DEFEXPORTED)
			np = &nep->ne_defexported;
	}
	return (np);
}

/*
 * perform msync on all vnodes under a mount point
 * the mount point must be locked.
 */
void
vfs_msync(struct mount *mp, int flags) 
{
	struct thread *td = curthread;	/* XXX */
	struct vnode *vp, *nvp;
	struct vm_object *obj;
	int tries;

	tries = 5;
	lwkt_gettoken(&mntvnode_token);
loop:
	for (vp = TAILQ_FIRST(&mp->mnt_nvnodelist); vp != NULL; vp = nvp) {
		if (vp->v_mount != mp) {
			if (--tries > 0)
				goto loop;
			break;
		}
		nvp = TAILQ_NEXT(vp, v_nmntvnodes);

		if (vp->v_flag & VXLOCK)	/* XXX: what if MNT_WAIT? */
			continue;

		/*
		 * There could be hundreds of thousands of vnodes, we cannot
		 * afford to do anything heavy-weight until we have a fairly
		 * good indication that there is something to do.
		 */
		if ((vp->v_flag & VOBJDIRTY) &&
		    (flags == MNT_WAIT || VOP_ISLOCKED(vp, NULL) == 0)) {
			lwkt_reltoken(&mntvnode_token);
			if (!vget(vp,
			    LK_EXCLUSIVE | LK_RETRY | LK_NOOBJ, td)) {
				if (VOP_GETVOBJECT(vp, &obj) == 0) {
					vm_object_page_clean(obj, 0, 0, flags == MNT_WAIT ? OBJPC_SYNC : OBJPC_NOSYNC);
				}
				vput(vp);
			}
			lwkt_gettoken(&mntvnode_token);
			if (TAILQ_NEXT(vp, v_nmntvnodes) != nvp) {
				if (--tries > 0)
					goto loop;
				break;
			}
		}
	}
	lwkt_reltoken(&mntvnode_token);
}

/*
 * Create the VM object needed for VMIO and mmap support.  This
 * is done for all VREG files in the system.  Some filesystems might
 * afford the additional metadata buffering capability of the
 * VMIO code by making the device node be VMIO mode also.
 *
 * vp must be locked when vfs_object_create is called.
 */
int
vfs_object_create(struct vnode *vp, struct thread *td)
{
	return (VOP_CREATEVOBJECT(vp, td));
}

void
vfree(vp)
	struct vnode *vp;
{
	int s;

	s = splbio();
	lwkt_gettoken(&vnode_free_list_token);
	KASSERT((vp->v_flag & VFREE) == 0, ("vnode already free"));
	if (vp->v_flag & VAGE) {
		TAILQ_INSERT_HEAD(&vnode_free_list, vp, v_freelist);
	} else {
		TAILQ_INSERT_TAIL(&vnode_free_list, vp, v_freelist);
	}
	freevnodes++;
	lwkt_reltoken(&vnode_free_list_token);
	vp->v_flag &= ~VAGE;
	vp->v_flag |= VFREE;
	splx(s);
}

void
vbusy(vp)
	struct vnode *vp;
{
	int s;

	s = splbio();
	lwkt_gettoken(&vnode_free_list_token);
	KASSERT((vp->v_flag & VFREE) != 0, ("vnode not free"));
	TAILQ_REMOVE(&vnode_free_list, vp, v_freelist);
	freevnodes--;
	lwkt_reltoken(&vnode_free_list_token);
	vp->v_flag &= ~(VFREE|VAGE);
	splx(s);
}

/*
 * Record a process's interest in events which might happen to
 * a vnode.  Because poll uses the historic select-style interface
 * internally, this routine serves as both the ``check for any
 * pending events'' and the ``record my interest in future events''
 * functions.  (These are done together, while the lock is held,
 * to avoid race conditions.)
 */
int
vn_pollrecord(struct vnode *vp, struct thread *td, int events)
{
	lwkt_gettoken(&vp->v_pollinfo.vpi_token);
	if (vp->v_pollinfo.vpi_revents & events) {
		/*
		 * This leaves events we are not interested
		 * in available for the other process which
		 * which presumably had requested them
		 * (otherwise they would never have been
		 * recorded).
		 */
		events &= vp->v_pollinfo.vpi_revents;
		vp->v_pollinfo.vpi_revents &= ~events;

		lwkt_reltoken(&vp->v_pollinfo.vpi_token);
		return events;
	}
	vp->v_pollinfo.vpi_events |= events;
	selrecord(td, &vp->v_pollinfo.vpi_selinfo);
	lwkt_reltoken(&vp->v_pollinfo.vpi_token);
	return 0;
}

/*
 * Note the occurrence of an event.  If the VN_POLLEVENT macro is used,
 * it is possible for us to miss an event due to race conditions, but
 * that condition is expected to be rare, so for the moment it is the
 * preferred interface.
 */
void
vn_pollevent(vp, events)
	struct vnode *vp;
	short events;
{
	lwkt_gettoken(&vp->v_pollinfo.vpi_token);
	if (vp->v_pollinfo.vpi_events & events) {
		/*
		 * We clear vpi_events so that we don't
		 * call selwakeup() twice if two events are
		 * posted before the polling process(es) is
		 * awakened.  This also ensures that we take at
		 * most one selwakeup() if the polling process
		 * is no longer interested.  However, it does
		 * mean that only one event can be noticed at
		 * a time.  (Perhaps we should only clear those
		 * event bits which we note?) XXX
		 */
		vp->v_pollinfo.vpi_events = 0;	/* &= ~events ??? */
		vp->v_pollinfo.vpi_revents |= events;
		selwakeup(&vp->v_pollinfo.vpi_selinfo);
	}
	lwkt_reltoken(&vp->v_pollinfo.vpi_token);
}

/*
 * Wake up anyone polling on vp because it is being revoked.
 * This depends on dead_poll() returning POLLHUP for correct
 * behavior.
 */
void
vn_pollgone(vp)
	struct vnode *vp;
{
	lwkt_gettoken(&vp->v_pollinfo.vpi_token);
	if (vp->v_pollinfo.vpi_events) {
		vp->v_pollinfo.vpi_events = 0;
		selwakeup(&vp->v_pollinfo.vpi_selinfo);
	}
	lwkt_reltoken(&vp->v_pollinfo.vpi_token);
}



/*
 * Routine to create and manage a filesystem syncer vnode.
 */
#define sync_close ((int (*) (struct  vop_close_args *))nullop)
static int	sync_fsync (struct  vop_fsync_args *);
static int	sync_inactive (struct  vop_inactive_args *);
static int	sync_reclaim  (struct  vop_reclaim_args *);
#define sync_lock ((int (*) (struct  vop_lock_args *))vop_nolock)
#define sync_unlock ((int (*) (struct  vop_unlock_args *))vop_nounlock)
static int	sync_print (struct vop_print_args *);
#define sync_islocked ((int(*) (struct vop_islocked_args *))vop_noislocked)

static vop_t **sync_vnodeop_p;
static struct vnodeopv_entry_desc sync_vnodeop_entries[] = {
	{ &vop_default_desc,	(vop_t *) vop_eopnotsupp },
	{ &vop_close_desc,	(vop_t *) sync_close },		/* close */
	{ &vop_fsync_desc,	(vop_t *) sync_fsync },		/* fsync */
	{ &vop_inactive_desc,	(vop_t *) sync_inactive },	/* inactive */
	{ &vop_reclaim_desc,	(vop_t *) sync_reclaim },	/* reclaim */
	{ &vop_lock_desc,	(vop_t *) sync_lock },		/* lock */
	{ &vop_unlock_desc,	(vop_t *) sync_unlock },	/* unlock */
	{ &vop_print_desc,	(vop_t *) sync_print },		/* print */
	{ &vop_islocked_desc,	(vop_t *) sync_islocked },	/* islocked */
	{ NULL, NULL }
};
static struct vnodeopv_desc sync_vnodeop_opv_desc =
	{ &sync_vnodeop_p, sync_vnodeop_entries };

VNODEOP_SET(sync_vnodeop_opv_desc);

/*
 * Create a new filesystem syncer vnode for the specified mount point.
 */
int
vfs_allocate_syncvnode(mp)
	struct mount *mp;
{
	struct vnode *vp;
	static long start, incr, next;
	int error;

	/* Allocate a new vnode */
	if ((error = getnewvnode(VT_VFS, mp, sync_vnodeop_p, &vp)) != 0) {
		mp->mnt_syncer = NULL;
		return (error);
	}
	vp->v_type = VNON;
	/*
	 * Place the vnode onto the syncer worklist. We attempt to
	 * scatter them about on the list so that they will go off
	 * at evenly distributed times even if all the filesystems
	 * are mounted at once.
	 */
	next += incr;
	if (next == 0 || next > syncer_maxdelay) {
		start /= 2;
		incr /= 2;
		if (start == 0) {
			start = syncer_maxdelay / 2;
			incr = syncer_maxdelay;
		}
		next = start;
	}
	vn_syncer_add_to_worklist(vp, syncdelay > 0 ? next % syncdelay : 0);
	mp->mnt_syncer = vp;
	return (0);
}

/*
 * Do a lazy sync of the filesystem.
 */
static int
sync_fsync(ap)
	struct vop_fsync_args /* {
		struct vnode *a_vp;
		struct ucred *a_cred;
		int a_waitfor;
		struct thread *a_td;
	} */ *ap;
{
	struct vnode *syncvp = ap->a_vp;
	struct mount *mp = syncvp->v_mount;
	struct thread *td = ap->a_td;
	int asyncflag;

	/*
	 * We only need to do something if this is a lazy evaluation.
	 */
	if (ap->a_waitfor != MNT_LAZY)
		return (0);

	/*
	 * Move ourselves to the back of the sync list.
	 */
	vn_syncer_add_to_worklist(syncvp, syncdelay);

	/*
	 * Walk the list of vnodes pushing all that are dirty and
	 * not already on the sync list.
	 */
	lwkt_gettoken(&mountlist_token);
	if (vfs_busy(mp, LK_EXCLUSIVE | LK_NOWAIT, &mountlist_token, td) != 0) {
		lwkt_reltoken(&mountlist_token);
		return (0);
	}
	asyncflag = mp->mnt_flag & MNT_ASYNC;
	mp->mnt_flag &= ~MNT_ASYNC;
	vfs_msync(mp, MNT_NOWAIT);
	VFS_SYNC(mp, MNT_LAZY, td);
	if (asyncflag)
		mp->mnt_flag |= MNT_ASYNC;
	vfs_unbusy(mp, td);
	return (0);
}

/*
 * The syncer vnode is no referenced.
 */
static int
sync_inactive(ap)
	struct vop_inactive_args /* {
		struct vnode *a_vp;
		struct proc *a_p;
	} */ *ap;
{

	vgone(ap->a_vp);
	return (0);
}

/*
 * The syncer vnode is no longer needed and is being decommissioned.
 *
 * Modifications to the worklist must be protected at splbio().
 */
static int
sync_reclaim(ap)
	struct vop_reclaim_args /* {
		struct vnode *a_vp;
	} */ *ap;
{
	struct vnode *vp = ap->a_vp;
	int s;

	s = splbio();
	vp->v_mount->mnt_syncer = NULL;
	if (vp->v_flag & VONWORKLST) {
		LIST_REMOVE(vp, v_synclist);
		vp->v_flag &= ~VONWORKLST;
	}
	splx(s);

	return (0);
}

/*
 * Print out a syncer vnode.
 */
static int
sync_print(ap)
	struct vop_print_args /* {
		struct vnode *a_vp;
	} */ *ap;
{
	struct vnode *vp = ap->a_vp;

	printf("syncer vnode");
	if (vp->v_vnlock != NULL)
		lockmgr_printinfo(vp->v_vnlock);
	printf("\n");
	return (0);
}

/*
 * extract the dev_t from a VBLK or VCHR
 */
dev_t
vn_todev(vp)
	struct vnode *vp;
{
	if (vp->v_type != VBLK && vp->v_type != VCHR)
		return (NODEV);
	return (vp->v_rdev);
}

/*
 * Check if vnode represents a disk device
 */
int
vn_isdisk(vp, errp)
	struct vnode *vp;
	int *errp;
{
	if (vp->v_type != VBLK && vp->v_type != VCHR) {
		if (errp != NULL)
			*errp = ENOTBLK;
		return (0);
	}
	if (vp->v_rdev == NULL) {
		if (errp != NULL)
			*errp = ENXIO;
		return (0);
	}
	if (!dev_dport(vp->v_rdev)) {
		if (errp != NULL)
			*errp = ENXIO;
		return (0);
	}
	if (!(dev_dflags(vp->v_rdev) & D_DISK)) {
		if (errp != NULL)
			*errp = ENOTBLK;
		return (0);
	}
	if (errp != NULL)
		*errp = 0;
	return (1);
}

void
NDFREE(ndp, flags)
     struct nameidata *ndp;
     const uint flags;
{
	if (!(flags & NDF_NO_FREE_PNBUF) &&
	    (ndp->ni_cnd.cn_flags & CNP_HASBUF)) {
		zfree(namei_zone, ndp->ni_cnd.cn_pnbuf);
		ndp->ni_cnd.cn_flags &= ~CNP_HASBUF;
	}
	if (!(flags & NDF_NO_DNCP_RELE) &&
	    (ndp->ni_cnd.cn_flags & CNP_WANTDNCP) &&
	    ndp->ni_dncp) {
		cache_drop(ndp->ni_dncp);
		ndp->ni_dncp = NULL;
	}
	if (!(flags & NDF_NO_NCP_RELE) &&
	    (ndp->ni_cnd.cn_flags & CNP_WANTNCP) &&
	    ndp->ni_ncp) {
		cache_drop(ndp->ni_ncp);
		ndp->ni_ncp = NULL;
	}
	if (!(flags & NDF_NO_DVP_UNLOCK) &&
	    (ndp->ni_cnd.cn_flags & CNP_LOCKPARENT) &&
	    ndp->ni_dvp != ndp->ni_vp) {
		VOP_UNLOCK(ndp->ni_dvp, 0, ndp->ni_cnd.cn_td);
	}
	if (!(flags & NDF_NO_DVP_RELE) &&
	    (ndp->ni_cnd.cn_flags & (CNP_LOCKPARENT|CNP_WANTPARENT))) {
		vrele(ndp->ni_dvp);
		ndp->ni_dvp = NULL;
	}
	if (!(flags & NDF_NO_VP_UNLOCK) &&
	    (ndp->ni_cnd.cn_flags & CNP_LOCKLEAF) && ndp->ni_vp) {
		VOP_UNLOCK(ndp->ni_vp, 0, ndp->ni_cnd.cn_td);
	}
	if (!(flags & NDF_NO_VP_RELE) &&
	    ndp->ni_vp) {
		vrele(ndp->ni_vp);
		ndp->ni_vp = NULL;
	}
	if (!(flags & NDF_NO_STARTDIR_RELE) &&
	    (ndp->ni_cnd.cn_flags & CNP_SAVESTART)) {
		vrele(ndp->ni_startdir);
		ndp->ni_startdir = NULL;
	}
}

