/*	$NetBSD: puffs_vfsops.c,v 1.96 2011/06/12 03:35:54 rmind Exp $	*/

/*
 * Copyright (c) 2005, 2006  Antti Kantee.  All Rights Reserved.
 *
 * Development of this software was supported by the
 * Google Summer of Code program and the Ulla Tuominen Foundation.
 * The Google SoC project was mentored by Bill Studenmund.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/mount.h>
#include <sys/malloc.h>
#include <sys/extattr.h>
#include <sys/queue.h>
#include <sys/vnode.h>
#include <sys/dirent.h>
#include <sys/proc.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/kthread.h>
#include <sys/libkern.h>
#include <sys/sysctl.h>

#include <vfs/nfs/nfsproto.h>

#include <dev/misc/putter/putter_sys.h>

#include <vfs/puffs/puffs_msgif.h>
#include <vfs/puffs/puffs_sys.h>

#ifndef PUFFS_PNODEBUCKETS
#define PUFFS_PNODEBUCKETS 256
#endif
#ifndef PUFFS_MAXPNODEBUCKETS
#define PUFFS_MAXPNODEBUCKETS 8192
#endif
int puffs_pnodebuckets_default = PUFFS_PNODEBUCKETS;
int puffs_maxpnodebuckets = PUFFS_MAXPNODEBUCKETS;

#define BUCKETALLOC(a) (sizeof(struct puffs_pnode_hashlist *) * (a))

#ifndef __arraycount
#define __arraycount(__x)	(sizeof(__x) / sizeof(__x[0]))
#endif

SYSCTL_NODE(_vfs, OID_AUTO, puffs, CTLFLAG_RW, 0, "PUFFS filesystem");

static int puffs_use_pagecache = 0;
SYSCTL_INT(_vfs_puffs, OID_AUTO, pagecache, CTLFLAG_RW, &puffs_use_pagecache,
        0, "Enable page cache");

static struct putter_ops puffs_putter = {
	.pop_getout	= puffs_msgif_getout,
	.pop_releaseout	= puffs_msgif_releaseout,
	.pop_waitcount	= puffs_msgif_waitcount,
	.pop_dispatch	= puffs_msgif_dispatch,
	.pop_close	= puffs_msgif_close,
};

static int
puffs_vfsop_mount(struct mount *mp, char *path, char *data,
	struct ucred *cred)
{
	struct puffs_mount *pmp = NULL;
	struct puffs_kargs *args, kargs;
	char *p;
	int error = 0, i;
	pid_t mntpid = curproc->p_pid;

	/* update is not supported currently */
	if (mp->mnt_flag & MNT_UPDATE)
		return EOPNOTSUPP;

	/*
	 * We need the file system name
	 */
	if (!data)
		return EINVAL;

	copyin(data, &kargs, sizeof(kargs));
	args = &kargs;

	if (args->pa_vers != PUFFSVERSION) {
		kprintf("puffs_mount: development version mismatch: "
		    "kernel %d, lib %d\n", PUFFSVERSION, args->pa_vers);
		error = EINVAL;
		goto out;
	}

	if ((args->pa_flags & ~PUFFS_KFLAG_MASK) != 0) {
		kprintf("puffs_mount: invalid KFLAGs 0x%x\n", args->pa_flags);
		error = EINVAL;
		goto out;
	}
	if ((args->pa_fhflags & ~PUFFS_FHFLAG_MASK) != 0) {
		kprintf("puffs_mount: invalid FHFLAGs 0x%x\n", args->pa_fhflags);
		error = EINVAL;
		goto out;
	}

	for (i = 0; i < __arraycount(args->pa_spare); i++) {
		if (args->pa_spare[i] != 0) {
			kprintf("puffs_mount: pa_spare[%d] = 0x%x\n",
			    i, args->pa_spare[i]);
			error = EINVAL;
			goto out;
		}
	}

	/* use dummy value for passthrough */
	if (args->pa_fhflags & PUFFS_FHFLAG_PASSTHROUGH)
		args->pa_fhsize = MAXFIDSZ;

	/* sanitize file handle length */
	if (PUFFS_TOFHSIZE(args->pa_fhsize) > sizeof(struct fid)) {
		kprintf("puffs_mount: handle size %zu too large\n",
		    args->pa_fhsize);
		error = EINVAL;
		goto out;
	}
	/* sanity check file handle max sizes */
	if (args->pa_fhsize && args->pa_fhflags & PUFFS_FHFLAG_PROTOMASK) {
		size_t kfhsize = PUFFS_TOFHSIZE(args->pa_fhsize);

		if (args->pa_fhflags & PUFFS_FHFLAG_NFSV2) {
			if (kfhsize > NFSX_FH(0)) {
				kprintf("puffs_mount: fhsize larger than "
				    "NFSv2 max %d\n",
				    PUFFS_FROMFHSIZE(NFSX_V2FH));
				error = EINVAL;
				goto out;
			}
		}

		if (args->pa_fhflags & PUFFS_FHFLAG_NFSV3) {
			if (kfhsize > NFSX_FH(1)) {
				kprintf("puffs_mount: fhsize larger than "
				    "NFSv3 max %d\n",
				    PUFFS_FROMFHSIZE(NFSX_V3FHMAX));
				error = EINVAL;
				goto out;
			}
		}
	}

	/* don't allow non-printing characters (like my sweet umlauts.. snif) */
	args->pa_typename[sizeof(args->pa_typename)-1] = '\0';
	for (p = args->pa_typename; *p; p++)
		if (*p < ' ' || *p > '~')
			*p = '.';

	args->pa_mntfromname[sizeof(args->pa_mntfromname)-1] = '\0';
	for (p = args->pa_mntfromname; *p; p++)
		if (*p < ' ' || *p > '~')
			*p = '.';

	/* build real name */
	bzero(mp->mnt_stat.f_fstypename, MFSNAMELEN);
	(void)strlcpy(mp->mnt_stat.f_fstypename, PUFFS_TYPEPREFIX, MFSNAMELEN);
	(void)strlcat(mp->mnt_stat.f_fstypename, args->pa_typename, MFSNAMELEN);

	bzero(mp->mnt_stat.f_mntfromname, MNAMELEN);
	strlcpy(mp->mnt_stat.f_mntfromname, args->pa_mntfromname, MFSNAMELEN);
	bzero(mp->mnt_stat.f_mntonname, MNAMELEN);
	copyinstr(path, mp->mnt_stat.f_mntonname, MNAMELEN, NULL);

	/* inform user server if it got the max request size it wanted */
	if (args->pa_maxmsglen == 0 || args->pa_maxmsglen > PUFFS_MSG_MAXSIZE)
		args->pa_maxmsglen = PUFFS_MSG_MAXSIZE;
	else if (args->pa_maxmsglen < 2*PUFFS_MSGSTRUCT_MAX)
		args->pa_maxmsglen = 2*PUFFS_MSGSTRUCT_MAX;

	if (args->pa_nhashbuckets == 0)
		args->pa_nhashbuckets = puffs_pnodebuckets_default;
	if (args->pa_nhashbuckets < 1)
		args->pa_nhashbuckets = 1;
	if (args->pa_nhashbuckets > PUFFS_MAXPNODEBUCKETS) {
		args->pa_nhashbuckets = puffs_maxpnodebuckets;
		kprintf("puffs_mount: using %d hash buckets. "
		    "adjust puffs_maxpnodebuckets for more\n",
		    puffs_maxpnodebuckets);
	}

	mp->mnt_stat.f_iosize = DEV_BSIZE;
	mp->mnt_stat.f_bsize = DEV_BSIZE;
	mp->mnt_vstat.f_frsize = DEV_BSIZE;
	mp->mnt_vstat.f_bsize = DEV_BSIZE;
	mp->mnt_vstat.f_namemax = args->pa_svfsb.f_namemax;

	pmp = kmalloc(sizeof(struct puffs_mount), M_PUFFS, M_ZERO | M_WAITOK);

	mp->mnt_flag &= ~MNT_LOCAL; /* we don't really know, so ... */
	mp->mnt_data = (qaddr_t)pmp;

#if 0
	/*
	 * XXX: puffs code is MPSAFE.  However, VFS really isn't.
	 * Currently, there is nothing which protects an inode from
	 * reclaim while there are threads inside the file system.
	 * This means that in the event of a server crash, an MPSAFE
	 * mount is likely to end up accessing invalid memory.  For the
	 * non-mpsafe case, the kernel lock, general structure of
	 * puffs and pmp_refcount protect the threads during escape.
	 *
	 * Fixing this will require:
	 *  a) fixing vfs
	 * OR
	 *  b) adding a small sleep to puffs_msgif_close() between
	 *     userdead() and dounmount().
	 *     (well, this isn't really a fix, but would solve
	 *     99.999% of the race conditions).
	 *
	 * Also, in the event of "b", unmount -f should be used,
	 * like with any other file system, sparingly and only when
	 * it is "known" to be safe.
	 */
	mp->mnt_iflags |= IMNT_MPSAFE;
#endif

	pmp->pmp_status = PUFFSTAT_MOUNTING;
	pmp->pmp_mp = mp;
	pmp->pmp_msg_maxsize = args->pa_maxmsglen;
	pmp->pmp_args = *args;

	if (puffs_use_pagecache == 0)
		pmp->pmp_flags |= PUFFS_KFLAG_NOCACHE_PAGE;

	pmp->pmp_npnodehash = args->pa_nhashbuckets;
	pmp->pmp_pnodehash = kmalloc(BUCKETALLOC(pmp->pmp_npnodehash),
	    M_PUFFS, M_WAITOK);
	for (i = 0; i < pmp->pmp_npnodehash; i++)
		LIST_INIT(&pmp->pmp_pnodehash[i]);
	LIST_INIT(&pmp->pmp_newcookie);

	/*
	 * Inform the fileops processing code that we have a mountpoint.
	 * If it doesn't know about anyone with our pid/fd having the
	 * device open, punt
	 */
	if ((pmp->pmp_pi
	    = putter_attach(mntpid, args->pa_minor, pmp, &puffs_putter)) == NULL) {
		error = ENOENT;
		goto out;
	}

	/* XXX: check parameters */
	pmp->pmp_root_cookie = args->pa_root_cookie;
	pmp->pmp_root_vtype = args->pa_root_vtype;
	pmp->pmp_root_vsize = args->pa_root_vsize;
	pmp->pmp_root_rdev = args->pa_root_rdev;

	lockinit(&pmp->pmp_lock, "puffs pmp_lock", 0, 0);
	lockinit(&pmp->pmp_sopmtx, "puffs pmp_sopmtx", 0, 0);
	cv_init(&pmp->pmp_msg_waiter_cv, "puffsget");
	cv_init(&pmp->pmp_refcount_cv, "puffsref");
	cv_init(&pmp->pmp_unmounting_cv, "puffsum");
	cv_init(&pmp->pmp_sopcv, "puffsop");
	TAILQ_INIT(&pmp->pmp_msg_touser);
	TAILQ_INIT(&pmp->pmp_msg_replywait);
	TAILQ_INIT(&pmp->pmp_sopreqs);

	if ((error = kthread_create(puffs_sop_thread, pmp, NULL,
	    "puffsop")) != 0)
		goto out;
	pmp->pmp_sopthrcount = 1;

	DPRINTF(("puffs_mount: mount point at %p, puffs specific at %p\n",
	    mp, MPTOPUFFSMP(mp)));

	vfs_getnewfsid(mp);

	vfs_add_vnodeops(mp, &puffs_vnode_vops, &mp->mnt_vn_norm_ops);
	vfs_add_vnodeops(mp, &puffs_fifo_vops, &mp->mnt_vn_fifo_ops);

 out:
	if (error && pmp && pmp->pmp_pi)
		putter_detach(pmp->pmp_pi);
	if (error && pmp && pmp->pmp_pnodehash)
		kfree(pmp->pmp_pnodehash, M_PUFFS);
	if (error && pmp)
		kfree(pmp, M_PUFFS);
	return error;
}

static int
puffs_vfsop_unmount(struct mount *mp, int mntflags)
{
	PUFFS_MSG_VARS(vfs, unmount);
	struct puffs_mount *pmp;
	int error, force;

	error = 0;
	force = mntflags & MNT_FORCE;
	pmp = MPTOPUFFSMP(mp);

	DPRINTF(("puffs_unmount: detach filesystem from vfs, current "
	    "status 0x%x\n", pmp->pmp_status));

	/*
	 * flush all the vnodes.  VOP_RECLAIM() takes care that the
	 * root vnode does not get flushed until unmount.  The
	 * userspace root node cookie is stored in the mount
	 * structure, so we can always re-instantiate a root vnode,
	 * should userspace unmount decide it doesn't want to
	 * cooperate.
	 */
	error = vflush(mp, 1, force ? FORCECLOSE : 0);
	if (error)
		goto out;

	/*
	 * If we are not DYING, we should ask userspace's opinion
	 * about the situation
	 */
	lockmgr(&pmp->pmp_lock, LK_EXCLUSIVE);
	if (pmp->pmp_status != PUFFSTAT_DYING) {
		pmp->pmp_unmounting = 1;
		lockmgr(&pmp->pmp_lock, LK_RELEASE);

		PUFFS_MSG_ALLOC(vfs, unmount);
		puffs_msg_setinfo(park_unmount,
		    PUFFSOP_VFS, PUFFS_VFS_UNMOUNT, NULL);
		unmount_msg->pvfsr_flags = mntflags;

		PUFFS_MSG_ENQUEUEWAIT(pmp, park_unmount, error);
		PUFFS_MSG_RELEASE(unmount);

		error = checkerr(pmp, error, __func__);
		DPRINTF(("puffs_unmount: error %d force %d\n", error, force));

		lockmgr(&pmp->pmp_lock, LK_EXCLUSIVE);
		pmp->pmp_unmounting = 0;
		cv_broadcast(&pmp->pmp_unmounting_cv);
	}

	/*
	 * if userspace cooperated or we really need to die,
	 * screw what userland thinks and just die.
	 */
	if (error == 0 || force) {
		struct puffs_sopreq *psopr;

		/* tell waiters & other resources to go unwait themselves */
		puffs_userdead(pmp);
		putter_detach(pmp->pmp_pi);

		/*
		 * Wait until there are no more users for the mount resource.
		 * Notice that this is hooked against transport_close
		 * and return from touser.  In an ideal world, it would
		 * be hooked against final return from all operations.
		 * But currently it works well enough, since nobody
		 * does weird blocking voodoo after return from touser().
		 */
		while (pmp->pmp_refcount != 0)
			cv_wait(&pmp->pmp_refcount_cv, &pmp->pmp_lock);
		lockmgr(&pmp->pmp_lock, LK_RELEASE);

		/*
		 * Release kernel thread now that there is nothing
		 * it would be wanting to lock.
		 */
		psopr = kmalloc(sizeof(*psopr), M_PUFFS, M_WAITOK);
		psopr->psopr_sopreq = PUFFS_SOPREQSYS_EXIT;
		lockmgr(&pmp->pmp_sopmtx, LK_EXCLUSIVE);
		if (pmp->pmp_sopthrcount == 0) {
			lockmgr(&pmp->pmp_sopmtx, LK_RELEASE);
			kfree(psopr, M_PUFFS);
			lockmgr(&pmp->pmp_sopmtx, LK_EXCLUSIVE);
			KKASSERT(pmp->pmp_sopthrcount == 0);
		} else {
			TAILQ_INSERT_TAIL(&pmp->pmp_sopreqs,
			    psopr, psopr_entries);
			cv_signal(&pmp->pmp_sopcv);
		}
		while (pmp->pmp_sopthrcount > 0)
			cv_wait(&pmp->pmp_sopcv, &pmp->pmp_sopmtx);
		lockmgr(&pmp->pmp_sopmtx, LK_RELEASE);

		/* free resources now that we hopefully have no waiters left */
		cv_destroy(&pmp->pmp_unmounting_cv);
		cv_destroy(&pmp->pmp_refcount_cv);
		cv_destroy(&pmp->pmp_msg_waiter_cv);
		cv_destroy(&pmp->pmp_sopcv);
		lockuninit(&pmp->pmp_lock);
		lockuninit(&pmp->pmp_sopmtx);

		kfree(pmp->pmp_pnodehash, M_PUFFS);
		kfree(pmp, M_PUFFS);
		error = 0;
	} else {
		lockmgr(&pmp->pmp_lock, LK_RELEASE);
	}

 out:
	DPRINTF(("puffs_unmount: return %d\n", error));
	return error;
}

/*
 * This doesn't need to travel to userspace
 */
static int
puffs_vfsop_root(struct mount *mp, struct vnode **vpp)
{
	struct puffs_mount *pmp = MPTOPUFFSMP(mp);
	int rv;

	rv = puffs_cookie2vnode(pmp, pmp->pmp_root_cookie, 1, vpp);
	KKASSERT(rv != PUFFS_NOSUCHCOOKIE);

	return rv;
}

static int
puffs_vfsop_statvfs(struct mount *mp, struct statvfs *sbp, struct ucred *cred)
{
	PUFFS_MSG_VARS(vfs, statvfs);
	struct puffs_mount *pmp;
	int error = 0;

	pmp = MPTOPUFFSMP(mp);

	/*
	 * If we are mounting, it means that the userspace counterpart
	 * is calling mount(2), but mount(2) also calls statvfs.  So
	 * requesting statvfs from userspace would mean a deadlock.
	 * Compensate.
	 */
	if (__predict_false(pmp->pmp_status == PUFFSTAT_MOUNTING))
		return EINPROGRESS;

	PUFFS_MSG_ALLOC(vfs, statvfs);
	puffs_msg_setinfo(park_statvfs, PUFFSOP_VFS, PUFFS_VFS_STATVFS, NULL);

	PUFFS_MSG_ENQUEUEWAIT(pmp, park_statvfs, error);
	error = checkerr(pmp, error, __func__);
	statvfs_msg->pvfsr_sb.f_bsize = DEV_BSIZE;

	/*
	 * Try to produce a sensible result even in the event
	 * of userspace error.
	 *
	 * XXX: cache the copy in non-error case
	 */
	if (!error) {
		(void)memcpy(sbp, &statvfs_msg->pvfsr_sb,
		    sizeof(struct statvfs));
	} else {
		(void)memcpy(sbp, &mp->mnt_stat,
		    sizeof(struct statvfs));
	}

	PUFFS_MSG_RELEASE(statvfs);
	return error;
}

#ifdef XXXDF
static int
pageflush(struct mount *mp, kauth_cred_t cred, int waitfor)
{
	struct puffs_node *pn;
	struct vnode *vp, *mvp;
	int error, rv;

	error = 0;

	/* Allocate a marker vnode. */
	if ((mvp = vnalloc(mp)) == NULL)
		return ENOMEM;

	/*
	 * Sync all cached data from regular vnodes (which are not
	 * currently locked, see below).  After this we call VFS_SYNC
	 * for the fs server, which should handle data and metadata for
	 * all the nodes it knows to exist.
	 */
	lockmgr(&mntvnode_lock, LK_EXCLUSIVE);
 loop:
	for (vp = TAILQ_FIRST(&mp->mnt_vnodelist); vp; vp = vunmark(mvp)) {
		vmark(mvp, vp);
		if (vp->v_mount != mp || vismarker(vp))
			continue;

		lockmgr(&vp->v_interlock, LK_EXCLUSIVE);
		pn = VPTOPP(vp);
		if (vp->v_type != VREG || UVM_OBJ_IS_CLEAN(&vp->v_uobj)) {
			lockmgr(&vp->v_interlock, LK_RELEASE);
			continue;
		}

		lockmgr(&mntvnode_lock, LK_RELEASE);

		/*
		 * Here we try to get a reference to the vnode and to
		 * lock it.  This is mostly cargo-culted, but I will
		 * offer an explanation to why I believe this might
		 * actually do the right thing.
		 *
		 * If the vnode is a goner, we quite obviously don't need
		 * to sync it.
		 *
		 * If the vnode was busy, we don't need to sync it because
		 * this is never called with MNT_WAIT except from
		 * dounmount(), when we are wait-flushing all the dirty
		 * vnodes through other routes in any case.  So there,
		 * sync() doesn't actually sync.  Happy now?
		 */
		rv = vget(vp, LK_EXCLUSIVE | LK_NOWAIT);
		if (rv) {
			lockmgr(&mntvnode_lock, LK_EXCLUSIVE);
			if (rv == ENOENT) {
				(void)vunmark(mvp);
				goto loop;
			}
			continue;
		}

		/* hmm.. is the FAF thing entirely sensible? */
		if (waitfor == MNT_LAZY) {
			lockmgr(&vp->v_interlock, LK_EXCLUSIVE);
			pn->pn_stat |= PNODE_FAF;
			lockmgr(&vp->v_interlock, LK_RELEASE);
		}
		rv = VOP_FSYNC(vp, cred, waitfor, 0, 0);
		if (waitfor == MNT_LAZY) {
			lockmgr(&vp->v_interlock, LK_EXCLUSIVE);
			pn->pn_stat &= ~PNODE_FAF;
			lockmgr(&vp->v_interlock, LK_RELEASE);
		}
		if (rv)
			error = rv;
		vput(vp);
		lockmgr(&mntvnode_lock, LK_EXCLUSIVE);
	}
	lockmgr(&mntvnode_lock, LK_RELEASE);
	vnfree(mvp);

	return error;
}
#endif

static int
puffs_vfsop_sync(struct mount *mp, int waitfor)
{
	PUFFS_MSG_VARS(vfs, sync);
	struct puffs_mount *pmp = MPTOPUFFSMP(mp);
	int error, rv;

#ifdef XXXDF
	error = pageflush(mp, cred, waitfor);
#endif

	/* sync fs */
	PUFFS_MSG_ALLOC(vfs, sync);
	sync_msg->pvfsr_waitfor = waitfor;
	puffs_msg_setinfo(park_sync, PUFFSOP_VFS, PUFFS_VFS_SYNC, NULL);

	PUFFS_MSG_ENQUEUEWAIT(pmp, park_sync, rv);
	error = checkerr(pmp, rv, __func__);

	PUFFS_MSG_RELEASE(sync);
	DPRINTF(("puffs_vfsop_sync: result %d\n", error));
	return error;
}

#ifdef XXXDF
static int
puffs_vfsop_fhtovp(struct mount *mp, struct vnode *rootvp, struct fid *fhp,
    struct vnode **vpp)
{
	PUFFS_MSG_VARS(vfs, fhtonode);
	struct puffs_mount *pmp = MPTOPUFFSMP(mp);
	struct vnode *vp;
	void *fhdata;
	size_t argsize, fhlen;
	int error;

	if (pmp->pmp_args.pa_fhsize == 0)
		return EOPNOTSUPP;

	if (pmp->pmp_args.pa_fhflags & PUFFS_FHFLAG_PASSTHROUGH) {
		fhlen = fhp->fid_len;
		fhdata = fhp;
	} else {
		fhlen = PUFFS_FROMFHSIZE(fhp->fid_len);
		fhdata = fhp->fid_data;

		if (pmp->pmp_args.pa_fhflags & PUFFS_FHFLAG_DYNAMIC) {
			if (pmp->pmp_args.pa_fhsize < fhlen)
				return EINVAL;
		} else {
			if (pmp->pmp_args.pa_fhsize != fhlen)
				return EINVAL;
		}
	}

	argsize = sizeof(struct puffs_vfsmsg_fhtonode) + fhlen;
	puffs_msgmem_alloc(argsize, &park_fhtonode, (void *)&fhtonode_msg, 1);
	fhtonode_msg->pvfsr_dsize = fhlen;
	memcpy(fhtonode_msg->pvfsr_data, fhdata, fhlen);
	puffs_msg_setinfo(park_fhtonode, PUFFSOP_VFS, PUFFS_VFS_FHTOVP, NULL);

	PUFFS_MSG_ENQUEUEWAIT(pmp, park_fhtonode, error);
	error = checkerr(pmp, error, __func__);
	if (error)
		goto out;

	error = puffs_cookie2vnode(pmp, fhtonode_msg->pvfsr_fhcookie, 1,1,&vp);
	DPRINTF(("puffs_fhtovp: got cookie %p, existing vnode %p\n",
	    fhtonode_msg->pvfsr_fhcookie, vp));
	if (error == PUFFS_NOSUCHCOOKIE) {
		error = puffs_getvnode(mp, fhtonode_msg->pvfsr_fhcookie,
		    fhtonode_msg->pvfsr_vtype, fhtonode_msg->pvfsr_size,
		    fhtonode_msg->pvfsr_rdev, &vp);
		if (error)
			goto out;
	} else if (error) {
		goto out;
	}

	*vpp = vp;
 out:
	puffs_msgmem_release(park_fhtonode);
	return error;
}

static int
puffs_vfsop_vptofh(struct vnode *vp, struct fid *fhp)
{
	PUFFS_MSG_VARS(vfs, nodetofh);
	struct puffs_mount *pmp = MPTOPUFFSMP(vp->v_mount);
	size_t argsize, fhlen;
	int error;

	if (pmp->pmp_args.pa_fhsize == 0)
		return EOPNOTSUPP;

	/* if file handles are static len, we can test len immediately */
	if (((pmp->pmp_args.pa_fhflags & PUFFS_FHFLAG_DYNAMIC) == 0)
	    && ((pmp->pmp_args.pa_fhflags & PUFFS_FHFLAG_PASSTHROUGH) == 0)
	    && (PUFFS_FROMFHSIZE(*fh_size) < pmp->pmp_args.pa_fhsize)) {
		*fh_size = PUFFS_TOFHSIZE(pmp->pmp_args.pa_fhsize);
		return E2BIG;
	}

	if (pmp->pmp_args.pa_fhflags & PUFFS_FHFLAG_PASSTHROUGH)
		fhlen = *fh_size;
	else
		fhlen = PUFFS_FROMFHSIZE(*fh_size);

	argsize = sizeof(struct puffs_vfsmsg_nodetofh) + fhlen;
	puffs_msgmem_alloc(argsize, &park_nodetofh, (void *)&nodetofh_msg, 1);
	nodetofh_msg->pvfsr_fhcookie = VPTOPNC(vp);
	nodetofh_msg->pvfsr_dsize = fhlen;
	puffs_msg_setinfo(park_nodetofh, PUFFSOP_VFS, PUFFS_VFS_VPTOFH, NULL);

	PUFFS_MSG_ENQUEUEWAIT(pmp, park_nodetofh, error);
	error = checkerr(pmp, error, __func__);

	if (pmp->pmp_args.pa_fhflags & PUFFS_FHFLAG_PASSTHROUGH)
		fhlen = nodetofh_msg->pvfsr_dsize;
	else if (pmp->pmp_args.pa_fhflags & PUFFS_FHFLAG_DYNAMIC)
		fhlen = PUFFS_TOFHSIZE(nodetofh_msg->pvfsr_dsize);
	else
		fhlen = PUFFS_TOFHSIZE(pmp->pmp_args.pa_fhsize);

	if (error) {
		if (error == E2BIG)
			*fh_size = fhlen;
		goto out;
	}

	if (fhlen > FHANDLE_SIZE_MAX) {
		puffs_senderr(pmp, PUFFS_ERR_VPTOFH, E2BIG,
		    "file handle too big", VPTOPNC(vp));
		error = EPROTO;
		goto out;
	}

	if (*fh_size < fhlen) {
		*fh_size = fhlen;
		error = E2BIG;
		goto out;
	}
	*fh_size = fhlen;

	if (fhp) {
		if (pmp->pmp_args.pa_fhflags & PUFFS_FHFLAG_PASSTHROUGH) {
			memcpy(fhp, nodetofh_msg->pvfsr_data, fhlen);
		} else {
			fhp->fid_len = *fh_size;
			memcpy(fhp->fid_data, nodetofh_msg->pvfsr_data,
			    nodetofh_msg->pvfsr_dsize);
		}
	}

 out:
	puffs_msgmem_release(park_nodetofh);
	return error;
}
#endif

static int
puffs_vfsop_start(struct mount *mp, int flags)
{
	struct puffs_mount *pmp = MPTOPUFFSMP(mp);

	KKASSERT(pmp->pmp_status == PUFFSTAT_MOUNTING);
	pmp->pmp_status = PUFFSTAT_RUNNING;

	return 0;
}

static int
puffs_vfsop_init(struct vfsconf *vfc)
{

	puffs_msgif_init();
	return 0;
}

static int
puffs_vfsop_uninit(struct vfsconf *vfc)
{

	puffs_msgif_destroy();
	return 0;
}

#ifdef XXXDF
static int
puffs_vfsop_extattrctl(struct mount *mp, int cmd, struct vnode *vp,
	int attrnamespace, const char *attrname, struct ucred *cred)
{
	PUFFS_MSG_VARS(vfs, extattrctl);
	struct puffs_mount *pmp = MPTOPUFFSMP(mp);
	struct puffs_node *pnp;
	puffs_cookie_t pnc;
	int error, flags;

	if (vp) {
		/* doesn't make sense for puffs servers */
		if (vp->v_mount != mp)
			return EXDEV;
		pnp = VPTOPP(vp);
		pnc = pnp->pn_cookie;
		flags = PUFFS_EXTATTRCTL_HASNODE;
	} else {
		pnp = pnc = NULL;
		flags = 0;
	}

	PUFFS_MSG_ALLOC(vfs, extattrctl);
	extattrctl_msg->pvfsr_cmd = cmd;
	extattrctl_msg->pvfsr_attrnamespace = attrnamespace;
	extattrctl_msg->pvfsr_flags = flags;
	if (attrname) {
		strlcpy(extattrctl_msg->pvfsr_attrname, attrname,
		    sizeof(extattrctl_msg->pvfsr_attrname));
		extattrctl_msg->pvfsr_flags |= PUFFS_EXTATTRCTL_HASATTRNAME;
	}
	puffs_msg_setinfo(park_extattrctl,
	    PUFFSOP_VFS, PUFFS_VFS_EXTATTRCTL, pnc);

	puffs_msg_enqueue(pmp, park_extattrctl);
	if (vp) {
		lockmgr(&pnp->pn_mtx, LK_EXCLUSIVE);
		puffs_referencenode(pnp);
		lockmgr(&pnp->pn_mtx, LK_RELEASE);
		VOP_UNLOCK(vp);
	}
	error = puffs_msg_wait2(pmp, park_extattrctl, pnp, NULL);
	PUFFS_MSG_RELEASE(extattrctl);
	if (vp) {
		puffs_releasenode(pnp);
	}

	return checkerr(pmp, error, __func__);
}
#endif

static struct vfsops puffs_vfsops = {
	.vfs_mount =		puffs_vfsop_mount,
	.vfs_unmount =		puffs_vfsop_unmount,
	.vfs_root =		puffs_vfsop_root,
	.vfs_statvfs =		puffs_vfsop_statvfs,
	.vfs_sync =		puffs_vfsop_sync,
#ifdef XXXFD
	.vfs_fhtovp =		puffs_vfsop_fhtovp,
	.vfs_vptofh =		puffs_vfsop_vptofh,
	.vfs_extattrctl =	puffs_vfsop_extattrctl,
#endif
	.vfs_start =		puffs_vfsop_start,
	.vfs_init =		puffs_vfsop_init,
	.vfs_uninit =		puffs_vfsop_uninit,
};

VFS_SET(puffs_vfsops, puffs, 0);
MODULE_DEPEND(puffs, putter, 1, 1, 1);
