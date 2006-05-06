/*
 * Copyright (c) 1992, 1993, 1995
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software donated to Berkeley by
 * Jan-Simon Pendry.
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
 *	@(#)null_vfsops.c	8.2 (Berkeley) 1/21/94
 *
 * @(#)lofs_vfsops.c	1.2 (Berkeley) 6/18/92
 * $FreeBSD: src/sys/miscfs/nullfs/null_vfsops.c,v 1.35.2.3 2001/07/26 20:37:11 iedowse Exp $
 * $DragonFly: src/sys/vfs/nullfs/null_vfsops.c,v 1.21 2006/05/06 18:48:53 dillon Exp $
 */

/*
 * Null Layer
 * (See null_vnops.c for a description of what this does.)
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/malloc.h>
#include <sys/vnode.h>
#include <sys/mount.h>
#include <sys/nlookup.h>
#include "null.h"

extern struct vnodeopv_entry_desc null_vnodeop_entries[];

static MALLOC_DEFINE(M_NULLFSMNT, "NULLFS mount", "NULLFS mount structure");

static int	nullfs_root(struct mount *mp, struct vnode **vpp);
static int	nullfs_statfs(struct mount *mp, struct statfs *sbp,
				struct ucred *cred);

/*
 * Mount null layer
 */
static int
nullfs_mount(struct mount *mp, char *path, caddr_t data, struct ucred *cred)
{
	int error = 0;
	struct null_args args;
	struct vnode *rootvp;
	struct null_mount *xmp;
	u_int size;
	struct nlookupdata nd;

	NULLFSDEBUG("nullfs_mount(mp = %p)\n", (void *)mp);

	/*
	 * Update is a no-op
	 */
	if (mp->mnt_flag & MNT_UPDATE) {
		return (EOPNOTSUPP);
	}

	/*
	 * Get argument
	 */
	error = copyin(data, (caddr_t)&args, sizeof(struct null_args));
	if (error)
		return (error);

	/*
	 * Find lower node
	 */
	rootvp = NULL;
	error = nlookup_init(&nd, args.target, UIO_USERSPACE, NLC_FOLLOW);
	if (error == 0)
		error = nlookup(&nd);
	if (error == 0) {
		error = cache_vget(nd.nl_ncp, nd.nl_cred, LK_EXCLUSIVE, 
					&rootvp);
	}

	xmp = (struct null_mount *) malloc(sizeof(struct null_mount),
				M_NULLFSMNT, M_WAITOK);	/* XXX */

	/*
	 * Save reference to underlying FS
	 */
        /*
         * As lite stacking enters the scene, the old way of doing this
	 * -- via the vnode -- is not good enough anymore...
	 */
	xmp->nullm_vfs = nd.nl_ncp->nc_mount;
	nlookup_done(&nd);

	vfs_add_vnodeops(mp, &mp->mnt_vn_norm_ops, 
			 null_vnodeop_entries, 0);

	VOP_UNLOCK(rootvp, 0);

	/*
	 * Keep a held reference to the root vnode.
	 * It is vrele'd in nullfs_unmount.
	 */
	xmp->nullm_rootvp = rootvp;
	/*
	 * XXX What's the proper safety condition for querying
	 * the underlying mount? Is this flag tuning necessary
	 * at all?
	 */
	if (xmp->nullm_vfs->mnt_flag & MNT_LOCAL)
		mp->mnt_flag |= MNT_LOCAL;
	mp->mnt_data = (qaddr_t) xmp;
	vfs_getnewfsid(mp);

	(void) copyinstr(args.target, mp->mnt_stat.f_mntfromname, MNAMELEN - 1,
	    &size);
	bzero(mp->mnt_stat.f_mntfromname + size, MNAMELEN - size);
	(void)nullfs_statfs(mp, &mp->mnt_stat, cred);
	NULLFSDEBUG("nullfs_mount: lower %s, alias at %s\n",
		mp->mnt_stat.f_mntfromname, mp->mnt_stat.f_mntfromname);
	return (0);
}

/*
 * Free reference to null layer
 */
static int
nullfs_unmount(struct mount *mp, int mntflags)
{
	void *mntdata;
	int flags = 0;

	NULLFSDEBUG("nullfs_unmount: mp = %p\n", (void *)mp);

	if (mntflags & MNT_FORCE)
		flags |= FORCECLOSE;

	/*
	 * Finally, throw away the null_mount structure
	 */
	mntdata = mp->mnt_data;
	mp->mnt_data = 0;
	free(mntdata, M_NULLFSMNT);
	return 0;
}

static int
nullfs_root(struct mount *mp, struct vnode **vpp)
{
	struct vnode *vp;

	NULLFSDEBUG("nullfs_root(mp = %p, vp = %p)\n", (void *)mp,
	    (void *)MOUNTTONULLMOUNT(mp)->nullm_rootvp);

	/*
	 * Return locked reference to root.
	 */
	vp = MOUNTTONULLMOUNT(mp)->nullm_rootvp;
	vref(vp);

#ifdef NULLFS_DEBUG
	if (VOP_ISLOCKED(vp, NULL)) {
		Debugger("root vnode is locked.\n");
		vrele(vp);
		return (EDEADLK);
	}
#endif
	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
	*vpp = vp;
	return 0;
}

static int
nullfs_quotactl(struct mount *mp, int cmd, uid_t uid, caddr_t arg,
		struct ucred *cred)
{
	return VFS_QUOTACTL(MOUNTTONULLMOUNT(mp)->nullm_vfs, cmd, uid, arg, cred);
}

static int
nullfs_statfs(struct mount *mp, struct statfs *sbp, struct ucred *cred)
{
	int error;
	struct statfs mstat;

	NULLFSDEBUG("nullfs_statfs(mp = %p, vp = %p)\n", (void *)mp,
	    (void *)MOUNTTONULLMOUNT(mp)->nullm_rootvp);

	bzero(&mstat, sizeof(mstat));

	error = VFS_STATFS(MOUNTTONULLMOUNT(mp)->nullm_vfs, &mstat, cred);
	if (error)
		return (error);

	/* now copy across the "interesting" information and fake the rest */
	sbp->f_type = mstat.f_type;
	sbp->f_flags = mstat.f_flags;
	sbp->f_bsize = mstat.f_bsize;
	sbp->f_iosize = mstat.f_iosize;
	sbp->f_blocks = mstat.f_blocks;
	sbp->f_bfree = mstat.f_bfree;
	sbp->f_bavail = mstat.f_bavail;
	sbp->f_files = mstat.f_files;
	sbp->f_ffree = mstat.f_ffree;
	if (sbp != &mp->mnt_stat) {
		bcopy(&mp->mnt_stat.f_fsid, &sbp->f_fsid, sizeof(sbp->f_fsid));
		bcopy(mp->mnt_stat.f_mntfromname, sbp->f_mntfromname, MNAMELEN);
	}
	return (0);
}

static int
nullfs_checkexp(struct mount *mp, struct sockaddr *nam, int *extflagsp,
		struct ucred **credanonp)
{

	return VFS_CHECKEXP(MOUNTTONULLMOUNT(mp)->nullm_vfs, nam, 
		extflagsp, credanonp);
}

static int                        
nullfs_extattrctl(struct mount *mp, int cmd, const char *attrname, caddr_t arg,
		  struct ucred *cred)
{
	return VFS_EXTATTRCTL(MOUNTTONULLMOUNT(mp)->nullm_vfs, cmd, attrname,
	    arg, cred);
}


static struct vfsops null_vfsops = {
	.vfs_mount =   	 	nullfs_mount,
	.vfs_unmount =   	nullfs_unmount,
	.vfs_root =     	nullfs_root,
	.vfs_quotactl =   	nullfs_quotactl,
	.vfs_statfs =    	nullfs_statfs,
	.vfs_sync =     	vfs_stdsync,
	.vfs_checkexp =  	nullfs_checkexp,
	.vfs_extattrctl =  	nullfs_extattrctl
};

VFS_SET(null_vfsops, null, VFCF_LOOPBACK);
