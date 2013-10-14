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
 *	@(#)null_vfsops.c	8.2 (Berkeley) 1/21/94
 *
 * @(#)lofs_vfsops.c	1.2 (Berkeley) 6/18/92
 * $FreeBSD: src/sys/miscfs/nullfs/null_vfsops.c,v 1.35.2.3 2001/07/26 20:37:11 iedowse Exp $
 * $DragonFly: src/sys/vfs/nullfs/null_vfsops.c,v 1.31 2008/09/17 21:44:25 dillon Exp $
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
#include <sys/namecache.h>
#include <sys/nlookup.h>
#include <sys/mountctl.h>
#include "null.h"

extern struct vop_ops null_vnode_vops;

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
	size_t size;
	struct nlookupdata nd;
	fhandle_t fh;

	NULLFSDEBUG("nullfs_mount(mp = %p)\n", (void *)mp);

	/*
	 * Get argument
	 */
	error = copyin(data, (caddr_t)&args, sizeof(struct null_args));
        if (error)
		return (error);

	/*
	 * XXX: Should we process mount export info ?
	 * If not, returning zero here is enough as the actual ro/rw update is
	 * being done in sys_mount().
	 */
	if (mp->mnt_flag & MNT_UPDATE) {
		xmp = MOUNTTONULLMOUNT(mp);
		error = vfs_export(mp, &xmp->export, &args.export);
		return (error);
	}

	/*
	 * Find lower node
	 */
	rootvp = NULL;
	error = nlookup_init(&nd, args.target, UIO_USERSPACE, NLC_FOLLOW);
	if (error)
		goto fail1;
	error = nlookup(&nd);
	if (error)
		goto fail2;
	error = cache_vget(&nd.nl_nch, nd.nl_cred, LK_EXCLUSIVE, &rootvp);
	if (error)
		goto fail2;

	xmp = (struct null_mount *) kmalloc(sizeof(struct null_mount),
				M_NULLFSMNT, M_WAITOK | M_ZERO);

	/*
	 * Save reference to underlying FS
	 *
         * As lite stacking enters the scene, the old way of doing this
	 * -- via the vnode -- is not good enough anymore.  Use the
	 * underlying filesystem's namecache handle as our mount point
	 * root, adjusting the mount to point to us. 
	 *
	 * NCF_ISMOUNTPT is normally set on the mount point, but we also
	 * want to set it on the base directory being mounted to prevent
	 * that directory from being destroyed out from under the nullfs
	 * mount. 
	 *
	 * The forwarding mount pointer (xmp->nullm_vfs) must be set to
	 * the actual target filesystem.  If the target filesystem was
	 * resolved via a nullfs mount nd.nl_nch.mount will be pointing
	 * to the nullfs mount structure instead of the target filesystem,
	 * which would otherwise cause the mount VOPS and VFSOPS to recurse
	 * endlessly.  If we are mounting via a nullfs mount we inherit
	 * its read-only state, if set.
	 */
	xmp->nullm_vfs = nd.nl_nch.mount;
	if (xmp->nullm_vfs != rootvp->v_mount) {
		if (xmp->nullm_vfs->mnt_flag & MNT_RDONLY)
			mp->mnt_flag |= MNT_RDONLY;
		if (xmp->nullm_vfs->mnt_flag & MNT_NOEXEC)
			mp->mnt_flag |= MNT_NOEXEC;
		xmp->nullm_vfs = rootvp->v_mount;
	}

	/*
	 * ncmountpt is the parent glue.  When mounting a nullfs via a nullfs
	 * we retain the parent nullfs to create a unique chain tuple.
	 */
	mp->mnt_ncmountpt = nd.nl_nch;
	cache_changemount(&mp->mnt_ncmountpt, mp);
	mp->mnt_ncmountpt.ncp->nc_flag |= NCF_ISMOUNTPT;
	cache_unlock(&mp->mnt_ncmountpt);
	cache_zero(&nd.nl_nch);
	nlookup_done(&nd);

	vfs_add_vnodeops(mp, &null_vnode_vops, &mp->mnt_vn_norm_ops);

	vn_unlock(rootvp);		/* leave reference intact */

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

	/*
	 * Try to create a unique but non-random fsid for the nullfs to
	 * allow it to be exported via NFS.
	 */
	bzero(&fh, sizeof(fh));
	fh.fh_fsid = rootvp->v_mount->mnt_stat.f_fsid;
	if (VFS_VPTOFH(rootvp, &fh.fh_fid) == 0) {
		fh.fh_fsid.val[1] ^= crc32(&fh.fh_fid, sizeof(fh.fh_fid));
		vfs_setfsid(mp, &fh.fh_fsid);
	} else {
		vfs_getnewfsid(mp);
	}

	(void) copyinstr(args.target, mp->mnt_stat.f_mntfromname, MNAMELEN - 1,
			    &size);
	bzero(mp->mnt_stat.f_mntfromname + size, MNAMELEN - size);
	(void)nullfs_statfs(mp, &mp->mnt_stat, cred);
	NULLFSDEBUG("nullfs_mount: lower %s, alias at %s\n",
		mp->mnt_stat.f_mntfromname, mp->mnt_stat.f_mntfromname);

	bzero(mp->mnt_stat.f_mntonname, MNAMELEN);
	if (path != NULL) {
		(void) copyinstr(path, mp->mnt_stat.f_mntonname, MNAMELEN - 1,
			&size);
	}

	/*
	 * Set NCALIASED so unmount won't complain about namecache refs
	 * still existing.
	 *
	 * All NULLFS operations are MPSAFE, though it will be short-lived
	 * if the underlying filesystem is not.
	 */
	mp->mnt_kern_flag |= MNTK_NCALIASED | MNTK_ALL_MPSAFE;

	/*
	 * And we don't need a syncer thread
	 */
	vn_syncer_thr_stop(mp);
	return (0);
fail2:
	nlookup_done(&nd);
fail1:
	return (error);
}

/*
 * Free reference to null layer
 */
static int
nullfs_unmount(struct mount *mp, int mntflags)
{
	struct null_mount *xmp;

	NULLFSDEBUG("nullfs_unmount: mp = %p\n", (void *)mp);

	/*
	 * Throw away the null_mount structure
	 */
	xmp = (void *)mp->mnt_data;
	mp->mnt_data = 0;
	if (xmp->nullm_rootvp) {
		vrele(xmp->nullm_rootvp);
		xmp->nullm_rootvp = NULL;
	}
	kfree(xmp, M_NULLFSMNT);
	return 0;
}

static int
nullfs_root(struct mount *mp, struct vnode **vpp)
{
	struct vnode *vp;
	int error;

	NULLFSDEBUG("nullfs_root(mp = %p, vp = %p)\n", (void *)mp,
	    (void *)MOUNTTONULLMOUNT(mp)->nullm_rootvp);

	/*
	 * Return locked reference to root.
	 */
	vp = MOUNTTONULLMOUNT(mp)->nullm_rootvp;
	error = vget(vp, LK_EXCLUSIVE | LK_RETRY);
	if (error == 0)
		*vpp = vp;
	return (error);
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
		bcopy(mp->mnt_stat.f_mntonname, sbp->f_mntonname, MNAMELEN);
	}
	return (0);
}

/*
 * Implement NFS export tracking
 */
static int
nullfs_checkexp(struct mount *mp, struct sockaddr *nam, int *extflagsp,
		struct ucred **credanonp)
{
	struct null_mount *xmp = (void *)mp->mnt_data;
	struct netcred *np;
	int error;

	np = vfs_export_lookup(mp, &xmp->export, nam);
	if (np) {
		*extflagsp = np->netc_exflags;
		*credanonp = &np->netc_anon;
		error = 0;
	} else {
		error = EACCES;
	}
	return(error);
#if 0
	return VFS_CHECKEXP(MOUNTTONULLMOUNT(mp)->nullm_vfs, nam, 
		extflagsp, credanonp);
#endif
}

int
nullfs_export(struct mount *mp, int op, const struct export_args *export)
{
	struct null_mount *xmp = (void *)mp->mnt_data;
	int error;

	switch(op) {
	case MOUNTCTL_SET_EXPORT:
		error = vfs_export(mp, &xmp->export, export);
		break;
	default:
		error = EOPNOTSUPP;
		break;
	}
	return(error);
}

/*
 * Pass through file handle conversion functions.
 */
static int
nullfs_vptofh(struct vnode *vp, struct fid *fhp)
{
	return VFS_VPTOFH(vp, fhp);
}

/*
 * Pass through file handle conversion functions.
 *
 * NOTE: currently only HAMMER uses rootvp.  HAMMER uses rootvp only
 * to enforce PFS isolation.
 */
static int
nullfs_fhtovp(struct mount *mp, struct vnode *rootvp,
              struct fid *fhp, struct vnode **vpp)
{
	struct null_mount *xmp = MOUNTTONULLMOUNT(mp);

	return VFS_FHTOVP(xmp->nullm_vfs, xmp->nullm_rootvp, fhp, vpp);
}

static int                        
nullfs_extattrctl(struct mount *mp, int cmd, struct vnode *vp,
		  int attrnamespace, const char *attrname, struct ucred *cred)
{
	return VFS_EXTATTRCTL(MOUNTTONULLMOUNT(mp)->nullm_vfs, cmd,
			      vp, attrnamespace, attrname, cred);
}

static void
nullfs_ncpgen_set(struct mount *mp, struct namecache *ncp)
{
	struct null_mount *xmp = MOUNTTONULLMOUNT(mp);

	VFS_NCPGEN_SET(xmp->nullm_vfs, ncp);
}


static int
nullfs_ncpgen_test(struct mount *mp, struct namecache *ncp)
{
	struct null_mount *xmp = MOUNTTONULLMOUNT(mp);

	return VFS_NCPGEN_TEST(xmp->nullm_vfs, ncp);
}


static struct vfsops null_vfsops = {
	.vfs_mount =   	 	nullfs_mount,
	.vfs_unmount =   	nullfs_unmount,
	.vfs_root =     	nullfs_root,
	.vfs_quotactl =   	nullfs_quotactl,
	.vfs_statfs =    	nullfs_statfs,
	.vfs_sync =     	vfs_stdsync,
	.vfs_extattrctl =  	nullfs_extattrctl,
	.vfs_fhtovp =		nullfs_fhtovp,
	.vfs_vptofh =		nullfs_vptofh,
	.vfs_ncpgen_set =	nullfs_ncpgen_set,
	.vfs_ncpgen_test =	nullfs_ncpgen_test,
	.vfs_checkexp =  	nullfs_checkexp
};

VFS_SET(null_vfsops, null, VFCF_LOOPBACK);
MODULE_VERSION(null, 1);
