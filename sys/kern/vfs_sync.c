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
 *
 *	@(#)vfs_subr.c	8.31 (Berkeley) 5/26/95
 * $FreeBSD: src/sys/kern/vfs_subr.c,v 1.249.2.30 2003/04/04 20:35:57 tegge Exp $
 */

/*
 * External virtual filesystem routines
 */

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

#include <sys/buf2.h>
#include <sys/thread2.h>

/*
 * The workitem queue.
 */
#define SYNCER_MAXDELAY		32
static int sysctl_kern_syncdelay(SYSCTL_HANDLER_ARGS);
time_t syncdelay = 30;		/* max time to delay syncing data */
SYSCTL_PROC(_kern, OID_AUTO, syncdelay, CTLTYPE_INT | CTLFLAG_RW, 0, 0,
		sysctl_kern_syncdelay, "I", "VFS data synchronization delay");
time_t filedelay = 30;		/* time to delay syncing files */
SYSCTL_INT(_kern, OID_AUTO, filedelay, CTLFLAG_RW,
		&filedelay, 0, "File synchronization delay");
time_t dirdelay = 29;		/* time to delay syncing directories */
SYSCTL_INT(_kern, OID_AUTO, dirdelay, CTLFLAG_RW,
		&dirdelay, 0, "Directory synchronization delay");
time_t metadelay = 28;		/* time to delay syncing metadata */
SYSCTL_INT(_kern, OID_AUTO, metadelay, CTLFLAG_RW,
		&metadelay, 0, "VFS metadata synchronization delay");
static int rushjob;			/* number of slots to run ASAP */
static int stat_rush_requests;	/* number of times I/O speeded up */
SYSCTL_INT(_debug, OID_AUTO, rush_requests, CTLFLAG_RW,
		&stat_rush_requests, 0, "");

LIST_HEAD(synclist, vnode);

#define	SC_FLAG_EXIT		(0x1)		/* request syncer exit */
#define	SC_FLAG_DONE		(0x2)		/* syncer confirm exit */

struct syncer_ctx {
	struct mount		*sc_mp;
	struct lwkt_token 	sc_token;
	struct thread		*sc_thread;
	int			sc_flags;
	struct synclist 	*syncer_workitem_pending;
	long			syncer_mask;
	int 			syncer_delayno;
	int			syncer_forced;
	int			syncer_rushjob;
};

static void syncer_thread(void *);

static int
sysctl_kern_syncdelay(SYSCTL_HANDLER_ARGS)
{
	int error;
	int v = syncdelay;

	error = sysctl_handle_int(oidp, &v, 0, req);
	if (error || !req->newptr)
		return (error);
	if (v < 1)
		v = 1;
	if (v > SYNCER_MAXDELAY)
		v = SYNCER_MAXDELAY;
	syncdelay = v;

	return(0);
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
 *
 * WARNING: Cannot get vp->v_token here if not already held, we must
 *	    depend on the syncer_token (which might already be held by
 *	    the caller) to protect v_synclist and VONWORKLST.
 *
 * MPSAFE
 */
void
vn_syncer_add(struct vnode *vp, int delay)
{
	struct syncer_ctx *ctx;
	int slot;

	ctx = vp->v_mount->mnt_syncer_ctx;
	lwkt_gettoken(&ctx->sc_token);

	if (vp->v_flag & VONWORKLST)
		LIST_REMOVE(vp, v_synclist);
	if (delay <= 0) {
		slot = -delay & ctx->syncer_mask;
	} else {
		if (delay > SYNCER_MAXDELAY - 2)
			delay = SYNCER_MAXDELAY - 2;
		slot = (ctx->syncer_delayno + delay) & ctx->syncer_mask;
	}

	LIST_INSERT_HEAD(&ctx->syncer_workitem_pending[slot], vp, v_synclist);
	vsetflags(vp, VONWORKLST);

	lwkt_reltoken(&ctx->sc_token);
}

/*
 * Removes the vnode from the syncer list.  Since we might block while
 * acquiring the syncer_token we have to recheck conditions.
 *
 * vp->v_token held on call
 */
void
vn_syncer_remove(struct vnode *vp)
{
	struct syncer_ctx *ctx;

	ctx = vp->v_mount->mnt_syncer_ctx;
	lwkt_gettoken(&ctx->sc_token);

	if ((vp->v_flag & (VISDIRTY | VONWORKLST | VOBJDIRTY)) == VONWORKLST &&
	    RB_EMPTY(&vp->v_rbdirty_tree)) {
		vclrflags(vp, VONWORKLST);
		LIST_REMOVE(vp, v_synclist);
	}

	lwkt_reltoken(&ctx->sc_token);
}

/*
 * vnode must be locked
 */
void
vclrisdirty(struct vnode *vp)
{
	vclrflags(vp, VISDIRTY);
	if (vp->v_flag & VONWORKLST)
		vn_syncer_remove(vp);
}

void
vclrobjdirty(struct vnode *vp)
{
	vclrflags(vp, VOBJDIRTY);
	if (vp->v_flag & VONWORKLST)
		vn_syncer_remove(vp);
}

/*
 * vnode must be stable
 */
void
vsetisdirty(struct vnode *vp)
{
	struct syncer_ctx *ctx;

	if ((vp->v_flag & VISDIRTY) == 0) {
		ctx = vp->v_mount->mnt_syncer_ctx;
		vsetflags(vp, VISDIRTY);
		lwkt_gettoken(&ctx->sc_token);
		if ((vp->v_flag & VONWORKLST) == 0)
			vn_syncer_add(vp, syncdelay);
		lwkt_reltoken(&ctx->sc_token);
	}
}

void
vsetobjdirty(struct vnode *vp)
{
	struct syncer_ctx *ctx;

	if ((vp->v_flag & VOBJDIRTY) == 0) {
		ctx = vp->v_mount->mnt_syncer_ctx;
		vsetflags(vp, VOBJDIRTY);
		lwkt_gettoken(&ctx->sc_token);
		if ((vp->v_flag & VONWORKLST) == 0)
			vn_syncer_add(vp, syncdelay);
		lwkt_reltoken(&ctx->sc_token);
	}
}

/*
 * Create per-filesystem syncer process
 */
void
vn_syncer_thr_create(struct mount *mp)
{
	struct syncer_ctx *ctx;
	static int syncalloc = 0;

	ctx = kmalloc(sizeof(struct syncer_ctx), M_TEMP, M_WAITOK | M_ZERO);
	ctx->sc_mp = mp;
	ctx->sc_flags = 0;
	ctx->syncer_workitem_pending = hashinit(SYNCER_MAXDELAY, M_DEVBUF,
						&ctx->syncer_mask);
	ctx->syncer_delayno = 0;
	lwkt_token_init(&ctx->sc_token, "syncer");
	mp->mnt_syncer_ctx = ctx;
	kthread_create(syncer_thread, ctx, &ctx->sc_thread,
		       "syncer%d", ++syncalloc & 0x7FFFFFFF);
}

/*
 * Stop per-filesystem syncer process
 */
void
vn_syncer_thr_stop(struct mount *mp)
{
	struct syncer_ctx *ctx;

	ctx = mp->mnt_syncer_ctx;
	if (ctx == NULL)
		return;

	lwkt_gettoken(&ctx->sc_token);

	/* Signal the syncer process to exit */
	ctx->sc_flags |= SC_FLAG_EXIT;
	wakeup(ctx);
	
	/* Wait till syncer process exits */
	while ((ctx->sc_flags & SC_FLAG_DONE) == 0) 
		tsleep(&ctx->sc_flags, 0, "syncexit", hz);

	mp->mnt_syncer_ctx = NULL;
	lwkt_reltoken(&ctx->sc_token);

	hashdestroy(ctx->syncer_workitem_pending, M_DEVBUF, ctx->syncer_mask);
	kfree(ctx, M_TEMP);
}

struct  thread *updatethread;

/*
 * System filesystem synchronizer daemon.
 */
static void
syncer_thread(void *_ctx)
{
	struct syncer_ctx *ctx = _ctx;
	struct synclist *slp;
	struct vnode *vp;
	long starttime;
	int *sc_flagsp;
	int sc_flags;
	int vnodes_synced = 0;
	int delta;
	int dummy = 0;

	for (;;) {
		kproc_suspend_loop();

		starttime = time_uptime;
		lwkt_gettoken(&ctx->sc_token);

		/*
		 * Push files whose dirty time has expired.  Be careful
		 * of interrupt race on slp queue.
		 */
		slp = &ctx->syncer_workitem_pending[ctx->syncer_delayno];
		ctx->syncer_delayno = (ctx->syncer_delayno + 1) &
				      ctx->syncer_mask;

		while ((vp = LIST_FIRST(slp)) != NULL) {
			if (ctx->syncer_forced) {
				if (vget(vp, LK_EXCLUSIVE) == 0) {
					VOP_FSYNC(vp, MNT_NOWAIT, 0);
					vput(vp);
					vnodes_synced++;
				}
			} else {
				if (vget(vp, LK_EXCLUSIVE | LK_NOWAIT) == 0) {
					VOP_FSYNC(vp, MNT_LAZY, 0);
					vput(vp);
					vnodes_synced++;
				}
			}

			/*
			 * vp is stale but can still be used if we can
			 * verify that it remains at the head of the list.
			 * Be careful not to try to get vp->v_token as
			 * vp can become stale if this blocks.
			 *
			 * If the vp is still at the head of the list were
			 * unable to completely flush it and move it to
			 * a later slot to give other vnodes a fair shot.
			 *
			 * Note that v_tag VT_VFS vnodes can remain on the
			 * worklist with no dirty blocks, but sync_fsync()
			 * moves it to a later slot so we will never see it
			 * here.
			 *
			 * It is possible to race a vnode with no dirty
			 * buffers being removed from the list.  If this
			 * occurs we will move the vnode in the synclist
			 * and then the other thread will remove it.  Do
			 * not try to remove it here.
			 */
			if (LIST_FIRST(slp) == vp)
				vn_syncer_add(vp, syncdelay);
		}

		sc_flags = ctx->sc_flags;

		/* Exit on unmount */
		if (sc_flags & SC_FLAG_EXIT)
			break;

		lwkt_reltoken(&ctx->sc_token);

		/*
		 * Do sync processing for each mount.
		 */
		if (ctx->sc_mp)
			bio_ops_sync(ctx->sc_mp);

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
		delta = rushjob - ctx->syncer_rushjob;
		if ((u_int)delta > syncdelay / 2) {
			ctx->syncer_rushjob = rushjob - syncdelay / 2;
			tsleep(&dummy, 0, "rush", 1);
			continue;
		}
		if (delta) {
			++ctx->syncer_rushjob;
			tsleep(&dummy, 0, "rush", 1);
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
		if (time_uptime == starttime)
			tsleep(ctx, 0, "syncer", hz);
	}

	/*
	 * Unmount/exit path for per-filesystem syncers; sc_token held
	 */
	ctx->sc_flags |= SC_FLAG_DONE;
	sc_flagsp = &ctx->sc_flags;
	lwkt_reltoken(&ctx->sc_token);
	wakeup(sc_flagsp);

	kthread_exit();
}

/*
 * Request that the syncer daemon for a specific mount speed up its work.
 * If mp is NULL the caller generally wants to speed up all syncers.
 */
void
speedup_syncer(struct mount *mp)
{
	/*
	 * Don't bother protecting the test.  unsleep_and_wakeup_thread()
	 * will only do something real if the thread is in the right state.
	 */
	atomic_add_int(&rushjob, 1);
	++stat_rush_requests;
	if (mp)
		wakeup(mp->mnt_syncer_ctx);
}

/*
 * Routine to create and manage a filesystem syncer vnode.
 */
static int sync_close(struct vop_close_args *);
static int sync_fsync(struct vop_fsync_args *);
static int sync_inactive(struct vop_inactive_args *);
static int sync_reclaim (struct vop_reclaim_args *);
static int sync_print(struct vop_print_args *);

static struct vop_ops sync_vnode_vops = {
	.vop_default =	vop_eopnotsupp,
	.vop_close =	sync_close,
	.vop_fsync =	sync_fsync,
	.vop_inactive =	sync_inactive,
	.vop_reclaim =	sync_reclaim,
	.vop_print =	sync_print,
};

static struct vop_ops *sync_vnode_vops_p = &sync_vnode_vops;

VNODEOP_SET(sync_vnode_vops);

/*
 * Create a new filesystem syncer vnode for the specified mount point.
 * This vnode is placed on the worklist and is responsible for sync'ing
 * the filesystem.
 *
 * NOTE: read-only mounts are also placed on the worklist.  The filesystem
 * sync code is also responsible for cleaning up vnodes.
 */
int
vfs_allocate_syncvnode(struct mount *mp)
{
	struct vnode *vp;
	static long start, incr, next;
	int error;

	/* Allocate a new vnode */
	error = getspecialvnode(VT_VFS, mp, &sync_vnode_vops_p, &vp, 0, 0);
	if (error) {
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
	if (next == 0 || next > SYNCER_MAXDELAY) {
		start /= 2;
		incr /= 2;
		if (start == 0) {
			start = SYNCER_MAXDELAY / 2;
			incr = SYNCER_MAXDELAY;
		}
		next = start;
	}

	/*
	 * Only put the syncer vnode onto the syncer list if we have a
	 * syncer thread.  Some VFS's (aka NULLFS) don't need a syncer
	 * thread.
	 */
	if (mp->mnt_syncer_ctx)
		vn_syncer_add(vp, syncdelay > 0 ? next % syncdelay : 0);

	/*
	 * The mnt_syncer field inherits the vnode reference, which is
	 * held until later decomissioning.
	 */
	mp->mnt_syncer = vp;
	vx_unlock(vp);
	return (0);
}

static int
sync_close(struct vop_close_args *ap)
{
	return (0);
}

/*
 * Do a lazy sync of the filesystem.
 *
 * sync_fsync { struct vnode *a_vp, int a_waitfor }
 */
static int
sync_fsync(struct vop_fsync_args *ap)
{
	struct vnode *syncvp = ap->a_vp;
	struct mount *mp = syncvp->v_mount;
	int asyncflag;

	/*
	 * We only need to do something if this is a lazy evaluation.
	 */
	if ((ap->a_waitfor & MNT_LAZY) == 0)
		return (0);

	/*
	 * Move ourselves to the back of the sync list.
	 */
	vn_syncer_add(syncvp, syncdelay);

	/*
	 * Walk the list of vnodes pushing all that are dirty and
	 * not already on the sync list, and freeing vnodes which have
	 * no refs and whos VM objects are empty.  vfs_msync() handles
	 * the VM issues and must be called whether the mount is readonly
	 * or not.
	 */
	if (vfs_busy(mp, LK_NOWAIT) != 0)
		return (0);
	if (mp->mnt_flag & MNT_RDONLY) {
		vfs_msync(mp, MNT_NOWAIT);
	} else {
		asyncflag = mp->mnt_flag & MNT_ASYNC;
		mp->mnt_flag &= ~MNT_ASYNC;	/* ZZZ hack */
		vfs_msync(mp, MNT_NOWAIT);
		VFS_SYNC(mp, MNT_NOWAIT | MNT_LAZY);
		if (asyncflag)
			mp->mnt_flag |= MNT_ASYNC;
	}
	vfs_unbusy(mp);
	return (0);
}

/*
 * The syncer vnode is no longer referenced.
 *
 * sync_inactive { struct vnode *a_vp, struct proc *a_p }
 */
static int
sync_inactive(struct vop_inactive_args *ap)
{
	vgone_vxlocked(ap->a_vp);
	return (0);
}

/*
 * The syncer vnode is no longer needed and is being decommissioned.
 * This can only occur when the last reference has been released on
 * mp->mnt_syncer, so mp->mnt_syncer had better be NULL.
 *
 * Modifications to the worklist must be protected with a critical
 * section.
 *
 *	sync_reclaim { struct vnode *a_vp }
 */
static int
sync_reclaim(struct vop_reclaim_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct syncer_ctx *ctx;

	ctx = vp->v_mount->mnt_syncer_ctx;
	if (ctx) {
		lwkt_gettoken(&ctx->sc_token);
		KKASSERT(vp->v_mount->mnt_syncer != vp);
		if (vp->v_flag & VONWORKLST) {
			LIST_REMOVE(vp, v_synclist);
			vclrflags(vp, VONWORKLST);
		}
		lwkt_reltoken(&ctx->sc_token);
	} else {
		KKASSERT((vp->v_flag & VONWORKLST) == 0);
	}

	return (0);
}

/*
 * This is very similar to vmntvnodescan() but it only scans the
 * vnodes on the syncer list.  VFS's which support faster VFS_SYNC
 * operations use the VISDIRTY flag on the vnode to ensure that vnodes
 * with dirty inodes are added to the syncer in addition to vnodes
 * with dirty buffers, and can use this function instead of nmntvnodescan().
 * 
 * This is important when a system has millions of vnodes.
 */
int
vsyncscan(
    struct mount *mp,
    int vmsc_flags,
    int (*slowfunc)(struct mount *mp, struct vnode *vp, void *data),
    void *data
) {
	struct syncer_ctx *ctx;
	struct synclist *slp;
	struct vnode *vp;
	int b;
	int i;
	int lkflags;

	if (vmsc_flags & VMSC_NOWAIT)
		lkflags = LK_NOWAIT;
	else
		lkflags = 0;

	/*
	 * Syncer list context.  This API requires a dedicated syncer thread.
	 * (MNTK_THR_SYNC).
	 */
	KKASSERT(mp->mnt_kern_flag & MNTK_THR_SYNC);
	ctx = mp->mnt_syncer_ctx;
	lwkt_gettoken(&ctx->sc_token);

	/*
	 * Setup for loop.  Allow races against the syncer thread but
	 * require that the syncer thread no be lazy if we were told
	 * not to be lazy.
	 */
	b = ctx->syncer_delayno & ctx->syncer_mask;
	i = b;
	if ((vmsc_flags & VMSC_NOWAIT) == 0)
		++ctx->syncer_forced;

	do {
		slp = &ctx->syncer_workitem_pending[i];

		while ((vp = LIST_FIRST(slp)) != NULL) {
			KKASSERT(vp->v_mount == mp);
			if (vmsc_flags & VMSC_GETVP) {
				if (vget(vp, LK_EXCLUSIVE | lkflags) == 0) {
					slowfunc(mp, vp, data);
					vput(vp);
				}
			} else if (vmsc_flags & VMSC_GETVX) {
				vx_get(vp);
				slowfunc(mp, vp, data);
				vx_put(vp);
			} else {
				vhold(vp);
				slowfunc(mp, vp, data);
				vdrop(vp);
			}
			if (LIST_FIRST(slp) == vp)
				vn_syncer_add(vp, -(i + syncdelay));
		}
		i = (i + 1) & ctx->syncer_mask;
	} while (i != b);

	if ((vmsc_flags & VMSC_NOWAIT) == 0)
		--ctx->syncer_forced;
	lwkt_reltoken(&ctx->sc_token);
	return(0);
}

/*
 * Print out a syncer vnode.
 *
 *	sync_print { struct vnode *a_vp }
 */
static int
sync_print(struct vop_print_args *ap)
{
	struct vnode *vp = ap->a_vp;

	kprintf("syncer vnode");
	lockmgr_printinfo(&vp->v_lock);
	kprintf("\n");
	return (0);
}
