/*
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed
 * to Berkeley by John Heidemann of the UCLA Ficus project.
 *
 * The statvfs->statfs conversion code was contributed to the DragonFly
 * Project by Joerg Sonnenberger <joerg@bec.de>.
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
 * Source: * @(#)i405_init.c 2.10 92/04/27 UCLA Ficus project
 * $FreeBSD: src/sys/kern/vfs_default.c,v 1.28.2.7 2003/01/10 18:23:26 bde Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/conf.h>
#include <sys/fcntl.h>
#include <sys/file.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mount.h>
#include <sys/unistd.h>
#include <sys/vnode.h>
#include <sys/namei.h>
#include <sys/nlookup.h>
#include <sys/mountctl.h>
#include <sys/vfs_quota.h>

#include <machine/limits.h>

#include <vm/vm.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <vm/vm_pager.h>
#include <vm/vnode_pager.h>

static int	vop_nolookup (struct vop_old_lookup_args *);
static int	vop_nostrategy (struct vop_strategy_args *);

/*
 * This vnode table stores what we want to do if the filesystem doesn't
 * implement a particular VOP.
 *
 * If there is no specific entry here, we will return EOPNOTSUPP.
 */
struct vop_ops default_vnode_vops = {
	.vop_default		= vop_eopnotsupp,
	.vop_advlock		= (void *)vop_einval,
	.vop_fsync		= (void *)vop_null,
	.vop_ioctl		= (void *)vop_enotty,
	.vop_mmap		= (void *)vop_einval,
	.vop_old_lookup		= vop_nolookup,
	.vop_open		= vop_stdopen,
	.vop_close		= vop_stdclose,
	.vop_pathconf		= vop_stdpathconf,
	.vop_readlink		= (void *)vop_einval,
	.vop_reallocblks	= (void *)vop_eopnotsupp,
	.vop_strategy		= vop_nostrategy,
	.vop_getacl		= (void *)vop_eopnotsupp,
	.vop_setacl		= (void *)vop_eopnotsupp,
	.vop_aclcheck		= (void *)vop_eopnotsupp,
	.vop_getextattr		= (void *)vop_eopnotsupp,
	.vop_setextattr		= (void *)vop_eopnotsupp,
	.vop_markatime		= vop_stdmarkatime,
	.vop_nresolve		= vop_compat_nresolve,
	.vop_nlookupdotdot	= vop_compat_nlookupdotdot,
	.vop_ncreate		= vop_compat_ncreate,
	.vop_nmkdir		= vop_compat_nmkdir,
	.vop_nmknod		= vop_compat_nmknod,
	.vop_nlink		= vop_compat_nlink,
	.vop_nsymlink		= vop_compat_nsymlink,
	.vop_nwhiteout		= vop_compat_nwhiteout,
	.vop_nremove		= vop_compat_nremove,
	.vop_nrmdir		= vop_compat_nrmdir,
	.vop_nrename		= vop_compat_nrename,
	.vop_mountctl		= vop_stdmountctl
};

VNODEOP_SET(default_vnode_vops);

int
vop_eopnotsupp(struct vop_generic_args *ap)
{
	return (EOPNOTSUPP);
}

int
vop_ebadf(struct vop_generic_args *ap)
{
	return (EBADF);
}

int
vop_enotty(struct vop_generic_args *ap)
{
	return (ENOTTY);
}

int
vop_einval(struct vop_generic_args *ap)
{
	return (EINVAL);
}

int
vop_stdmarkatime(struct vop_markatime_args *ap)
{
	return (EOPNOTSUPP);
}

int
vop_null(struct vop_generic_args *ap)
{
	return (0);
}

int
vop_defaultop(struct vop_generic_args *ap)
{
	return (VOCALL(&default_vnode_vops, ap));
}

int
vop_panic(struct vop_generic_args *ap)
{
	panic("filesystem goof: vop_panic[%s]", ap->a_desc->sd_name);
}

/*
 * vop_compat_resolve { struct nchandle *a_nch, struct vnode *dvp }
 * XXX STOPGAP FUNCTION
 *
 * XXX OLD API ROUTINE!  WHEN ALL VFSs HAVE BEEN CLEANED UP THIS PROCEDURE
 * WILL BE REMOVED.  This procedure exists for all VFSs which have not
 * yet implemented VOP_NRESOLVE().  It converts VOP_NRESOLVE() into a 
 * vop_old_lookup() and does appropriate translations.
 *
 * Resolve a ncp for VFSs which do not support the VOP.  Eventually all
 * VFSs will support this VOP and this routine can be removed, since
 * VOP_NRESOLVE() is far less complex then the older LOOKUP/CACHEDLOOKUP
 * API.
 *
 * A locked ncp is passed in to be resolved.  The NCP is resolved by
 * figuring out the vnode (if any) and calling cache_setvp() to attach the
 * vnode to the entry.  If the entry represents a non-existant node then
 * cache_setvp() is called with a NULL vnode to resolve the entry into a
 * negative cache entry.  No vnode locks are retained and the
 * ncp is left locked on return.
 *
 * The ncp will NEVER represent "", "." or "..", or contain any slashes.
 *
 * There is a potential directory and vnode interlock.   The lock order
 * requirement is: namecache, governing directory, resolved vnode.
 */
int
vop_compat_nresolve(struct vop_nresolve_args *ap)
{
	int error;
	struct vnode *dvp;
	struct vnode *vp;
	struct nchandle *nch;
	struct namecache *ncp;
	struct componentname cnp;

	nch = ap->a_nch;	/* locked namecache node */
	ncp = nch->ncp;
	dvp = ap->a_dvp;

	/*
	 * UFS currently stores all sorts of side effects, including a loop
	 * variable, in the directory inode.  That needs to be fixed and the
	 * other VFS's audited before we can switch to LK_SHARED.
	 */
	if ((error = vget(dvp, LK_EXCLUSIVE)) != 0) {
		kprintf("[diagnostic] vop_compat_resolve: EAGAIN on ncp %p %s\n",
			ncp, ncp->nc_name);
		return(EAGAIN);
	}

	bzero(&cnp, sizeof(cnp));
	cnp.cn_nameiop = NAMEI_LOOKUP;
	cnp.cn_flags = 0;
	cnp.cn_nameptr = ncp->nc_name;
	cnp.cn_namelen = ncp->nc_nlen;
	cnp.cn_cred = ap->a_cred;
	cnp.cn_td = curthread; /* XXX */

	/*
	 * vop_old_lookup() always returns vp locked.  dvp may or may not be
	 * left locked depending on CNP_PDIRUNLOCK.
	 */
	error = vop_old_lookup(ap->a_head.a_ops, dvp, &vp, &cnp);
	if (error == 0)
		vn_unlock(vp);
	if ((cnp.cn_flags & CNP_PDIRUNLOCK) == 0)
		vn_unlock(dvp);
	if ((ncp->nc_flag & NCF_UNRESOLVED) == 0) {
		/* was resolved by another process while we were unlocked */
		if (error == 0)
			vrele(vp);
	} else if (error == 0) {
		KKASSERT(vp != NULL);
		cache_setvp(nch, vp);
		vrele(vp);
	} else if (error == ENOENT) {
		KKASSERT(vp == NULL);
		if (cnp.cn_flags & CNP_ISWHITEOUT)
			ncp->nc_flag |= NCF_WHITEOUT;
		cache_setvp(nch, NULL);
	}
	vrele(dvp);
	return (error);
}

/*
 * vop_compat_nlookupdotdot { struct vnode *a_dvp,
 *			struct vnode **a_vpp,
 *			struct ucred *a_cred }
 *
 * Lookup the vnode representing the parent directory of the specified
 * directory vnode.  a_dvp should not be locked.  If no error occurs *a_vpp
 * will contained the parent vnode, locked and refd, else *a_vpp will be NULL.
 *
 * This function is designed to aid NFS server-side operations and is
 * used by cache_fromdvp() to create a consistent, connected namecache
 * topology.
 *
 * As part of the NEW API work, VFSs will first split their CNP_ISDOTDOT
 * code out from their *_lookup() and create *_nlookupdotdot().  Then as time
 * permits VFSs will implement the remaining *_n*() calls and finally get
 * rid of their *_lookup() call.
 */
int
vop_compat_nlookupdotdot(struct vop_nlookupdotdot_args *ap)
{
	struct componentname cnp;
	int error;

	/*
	 * UFS currently stores all sorts of side effects, including a loop
	 * variable, in the directory inode.  That needs to be fixed and the
	 * other VFS's audited before we can switch to LK_SHARED.
	 */
	*ap->a_vpp = NULL;
	if ((error = vget(ap->a_dvp, LK_EXCLUSIVE)) != 0)
		return (error);
	if (ap->a_dvp->v_type != VDIR) {
		vput(ap->a_dvp);
		return (ENOTDIR);
	}

	bzero(&cnp, sizeof(cnp));
	cnp.cn_nameiop = NAMEI_LOOKUP;
	cnp.cn_flags = CNP_ISDOTDOT;
	cnp.cn_nameptr = "..";
	cnp.cn_namelen = 2;
	cnp.cn_cred = ap->a_cred;
	cnp.cn_td = curthread; /* XXX */

	/*
	 * vop_old_lookup() always returns vp locked.  dvp may or may not be
	 * left locked depending on CNP_PDIRUNLOCK.
	 *
	 * (*vpp) will be returned locked if no error occured, which is the
	 * state we want.
	 */
	error = vop_old_lookup(ap->a_head.a_ops, ap->a_dvp, ap->a_vpp, &cnp);
	if (cnp.cn_flags & CNP_PDIRUNLOCK)
		vrele(ap->a_dvp);
	else
		vput(ap->a_dvp);
	return (error);
}

/*
 * vop_compat_ncreate { struct nchandle *a_nch, 	XXX STOPGAP FUNCTION
 *			struct vnode *a_dvp,
 *			struct vnode **a_vpp,
 *			struct ucred *a_cred,
 *			struct vattr *a_vap }
 *
 * Create a file as specified by a_vap.  Compatibility requires us to issue
 * the appropriate VOP_OLD_LOOKUP before we issue VOP_OLD_CREATE in order
 * to setup the directory inode's i_offset and i_count (e.g. in UFS).
 */
int
vop_compat_ncreate(struct vop_ncreate_args *ap)
{
	struct thread *td = curthread;
	struct componentname cnp;
	struct nchandle *nch;
	struct namecache *ncp;
	struct vnode *dvp;
	int error;

	/*
	 * Sanity checks, get a locked directory vnode.
	 */
	nch = ap->a_nch;		/* locked namecache node */
	dvp = ap->a_dvp;
	ncp = nch->ncp;

	if ((error = vget(dvp, LK_EXCLUSIVE)) != 0) {
		kprintf("[diagnostic] vop_compat_resolve: EAGAIN on ncp %p %s\n",
			ncp, ncp->nc_name);
		return(EAGAIN);
	}

	/*
	 * Setup the cnp for a traditional vop_old_lookup() call.  The lookup
	 * caches all information required to create the entry in the
	 * directory inode.  We expect a return code of EJUSTRETURN for
	 * the CREATE case.  The cnp must simulated a saved-name situation.
	 */
	bzero(&cnp, sizeof(cnp));
	cnp.cn_nameiop = NAMEI_CREATE;
	cnp.cn_flags = CNP_LOCKPARENT;
	cnp.cn_nameptr = ncp->nc_name;
	cnp.cn_namelen = ncp->nc_nlen;
	cnp.cn_cred = ap->a_cred;
	cnp.cn_td = td;
	*ap->a_vpp = NULL;

	error = vop_old_lookup(ap->a_head.a_ops, dvp, ap->a_vpp, &cnp);

	/*
	 * EJUSTRETURN should be returned for this case, which means that
	 * the VFS has setup the directory inode for the create.  The dvp we
	 * passed in is expected to remain in a locked state.
	 *
	 * If the VOP_OLD_CREATE is successful we are responsible for updating
	 * the cache state of the locked ncp that was passed to us.
	 */
	if (error == EJUSTRETURN) {
		KKASSERT((cnp.cn_flags & CNP_PDIRUNLOCK) == 0);
		error = VOP_OLD_CREATE(dvp, ap->a_vpp, &cnp, ap->a_vap);
		if (error == 0) {
			cache_setunresolved(nch);
			cache_setvp(nch, *ap->a_vpp);
		}
	} else {
		if (error == 0) {
			vput(*ap->a_vpp);
			*ap->a_vpp = NULL;
			error = EEXIST;
		}
		KKASSERT(*ap->a_vpp == NULL);
	}
	if ((cnp.cn_flags & CNP_PDIRUNLOCK) == 0)
		vn_unlock(dvp);
	vrele(dvp);
	return (error);
}

/*
 * vop_compat_nmkdir { struct nchandle *a_nch, 	XXX STOPGAP FUNCTION
 *			struct vnode *a_dvp,
 *			struct vnode **a_vpp,
 *			struct ucred *a_cred,
 *			struct vattr *a_vap }
 *
 * Create a directory as specified by a_vap.  Compatibility requires us to
 * issue the appropriate VOP_OLD_LOOKUP before we issue VOP_OLD_MKDIR in
 * order to setup the directory inode's i_offset and i_count (e.g. in UFS).
 */
int
vop_compat_nmkdir(struct vop_nmkdir_args *ap)
{
	struct thread *td = curthread;
	struct componentname cnp;
	struct nchandle *nch;
	struct namecache *ncp;
	struct vnode *dvp;
	int error;

	/*
	 * Sanity checks, get a locked directory vnode.
	 */
	nch = ap->a_nch;		/* locked namecache node */
	ncp = nch->ncp;
	dvp = ap->a_dvp;
	if ((error = vget(dvp, LK_EXCLUSIVE)) != 0) {
		kprintf("[diagnostic] vop_compat_resolve: EAGAIN on ncp %p %s\n",
			ncp, ncp->nc_name);
		return(EAGAIN);
	}

	/*
	 * Setup the cnp for a traditional vop_old_lookup() call.  The lookup
	 * caches all information required to create the entry in the
	 * directory inode.  We expect a return code of EJUSTRETURN for
	 * the CREATE case.  The cnp must simulated a saved-name situation.
	 */
	bzero(&cnp, sizeof(cnp));
	cnp.cn_nameiop = NAMEI_CREATE;
	cnp.cn_flags = CNP_LOCKPARENT;
	cnp.cn_nameptr = ncp->nc_name;
	cnp.cn_namelen = ncp->nc_nlen;
	cnp.cn_cred = ap->a_cred;
	cnp.cn_td = td;
	*ap->a_vpp = NULL;

	error = vop_old_lookup(ap->a_head.a_ops, dvp, ap->a_vpp, &cnp);

	/*
	 * EJUSTRETURN should be returned for this case, which means that
	 * the VFS has setup the directory inode for the create.  The dvp we
	 * passed in is expected to remain in a locked state.
	 *
	 * If the VOP_OLD_MKDIR is successful we are responsible for updating
	 * the cache state of the locked ncp that was passed to us.
	 */
	if (error == EJUSTRETURN) {
		KKASSERT((cnp.cn_flags & CNP_PDIRUNLOCK) == 0);
		error = VOP_OLD_MKDIR(dvp, ap->a_vpp, &cnp, ap->a_vap);
		if (error == 0) {
			cache_setunresolved(nch);
			cache_setvp(nch, *ap->a_vpp);
		}
	} else {
		if (error == 0) {
			vput(*ap->a_vpp);
			*ap->a_vpp = NULL;
			error = EEXIST;
		}
		KKASSERT(*ap->a_vpp == NULL);
	}
	if ((cnp.cn_flags & CNP_PDIRUNLOCK) == 0)
		vn_unlock(dvp);
	vrele(dvp);
	return (error);
}

/*
 * vop_compat_nmknod { struct nchandle *a_nch, 	XXX STOPGAP FUNCTION
 *			struct vnode *a_dvp,
 *			struct vnode **a_vpp,
 *			struct ucred *a_cred,
 *			struct vattr *a_vap }
 *
 * Create a device or fifo node as specified by a_vap.  Compatibility requires
 * us to issue the appropriate VOP_OLD_LOOKUP before we issue VOP_OLD_MKNOD
 * in order to setup the directory inode's i_offset and i_count (e.g. in UFS).
 */
int
vop_compat_nmknod(struct vop_nmknod_args *ap)
{
	struct thread *td = curthread;
	struct componentname cnp;
	struct nchandle *nch;
	struct namecache *ncp;
	struct vnode *dvp;
	int error;

	/*
	 * Sanity checks, get a locked directory vnode.
	 */
	nch = ap->a_nch;		/* locked namecache node */
	ncp = nch->ncp;
	dvp = ap->a_dvp;

	if ((error = vget(dvp, LK_EXCLUSIVE)) != 0) {
		kprintf("[diagnostic] vop_compat_resolve: EAGAIN on ncp %p %s\n",
			ncp, ncp->nc_name);
		return(EAGAIN);
	}

	/*
	 * Setup the cnp for a traditional vop_old_lookup() call.  The lookup
	 * caches all information required to create the entry in the
	 * directory inode.  We expect a return code of EJUSTRETURN for
	 * the CREATE case.  The cnp must simulated a saved-name situation.
	 */
	bzero(&cnp, sizeof(cnp));
	cnp.cn_nameiop = NAMEI_CREATE;
	cnp.cn_flags = CNP_LOCKPARENT;
	cnp.cn_nameptr = ncp->nc_name;
	cnp.cn_namelen = ncp->nc_nlen;
	cnp.cn_cred = ap->a_cred;
	cnp.cn_td = td;
	*ap->a_vpp = NULL;

	error = vop_old_lookup(ap->a_head.a_ops, dvp, ap->a_vpp, &cnp);

	/*
	 * EJUSTRETURN should be returned for this case, which means that
	 * the VFS has setup the directory inode for the create.  The dvp we
	 * passed in is expected to remain in a locked state.
	 *
	 * If the VOP_OLD_MKNOD is successful we are responsible for updating
	 * the cache state of the locked ncp that was passed to us.
	 */
	if (error == EJUSTRETURN) {
		KKASSERT((cnp.cn_flags & CNP_PDIRUNLOCK) == 0);
		error = VOP_OLD_MKNOD(dvp, ap->a_vpp, &cnp, ap->a_vap);
		if (error == 0) {
			cache_setunresolved(nch);
			cache_setvp(nch, *ap->a_vpp);
		}
	} else {
		if (error == 0) {
			vput(*ap->a_vpp);
			*ap->a_vpp = NULL;
			error = EEXIST;
		}
		KKASSERT(*ap->a_vpp == NULL);
	}
	if ((cnp.cn_flags & CNP_PDIRUNLOCK) == 0)
		vn_unlock(dvp);
	vrele(dvp);
	return (error);
}

/*
 * vop_compat_nlink { struct nchandle *a_nch, 	XXX STOPGAP FUNCTION
 *			struct vnode *a_dvp,
 *			struct vnode *a_vp,
 *			struct ucred *a_cred }
 *
 * The passed vp is locked and represents the source.  The passed ncp is
 * locked and represents the target to create.
 */
int
vop_compat_nlink(struct vop_nlink_args *ap)
{
	struct thread *td = curthread;
	struct componentname cnp;
	struct nchandle *nch;
	struct namecache *ncp;
	struct vnode *dvp;
	struct vnode *tvp;
	int error;

	/*
	 * Sanity checks, get a locked directory vnode.
	 */
	nch = ap->a_nch;		/* locked namecache node */
	ncp = nch->ncp;
	dvp = ap->a_dvp;

	if ((error = vget(dvp, LK_EXCLUSIVE)) != 0) {
		kprintf("[diagnostic] vop_compat_resolve: EAGAIN on ncp %p %s\n",
			ncp, ncp->nc_name);
		return(EAGAIN);
	}

	/*
	 * Setup the cnp for a traditional vop_old_lookup() call.  The lookup
	 * caches all information required to create the entry in the
	 * directory inode.  We expect a return code of EJUSTRETURN for
	 * the CREATE case.  The cnp must simulated a saved-name situation.
	 *
	 * It should not be possible for there to be a vnode collision
	 * between the source vp and target (name lookup).  However NFS
	 * clients racing each other can cause NFS to alias the same vnode
	 * across several names without the rest of the system knowing it.
	 * Use CNP_NOTVP to avoid a panic in this situation.
	 */
	bzero(&cnp, sizeof(cnp));
	cnp.cn_nameiop = NAMEI_CREATE;
	cnp.cn_flags = CNP_LOCKPARENT | CNP_NOTVP;
	cnp.cn_nameptr = ncp->nc_name;
	cnp.cn_namelen = ncp->nc_nlen;
	cnp.cn_cred = ap->a_cred;
	cnp.cn_td = td;
	cnp.cn_notvp = ap->a_vp;

	tvp = NULL;
	error = vop_old_lookup(ap->a_head.a_ops, dvp, &tvp, &cnp);

	/*
	 * EJUSTRETURN should be returned for this case, which means that
	 * the VFS has setup the directory inode for the create.  The dvp we
	 * passed in is expected to remain in a locked state.
	 *
	 * If the VOP_OLD_LINK is successful we are responsible for updating
	 * the cache state of the locked ncp that was passed to us.
	 */
	if (error == EJUSTRETURN) {
		KKASSERT((cnp.cn_flags & CNP_PDIRUNLOCK) == 0);
		error = VOP_OLD_LINK(dvp, ap->a_vp, &cnp);
		if (error == 0) {
			cache_setunresolved(nch);
			cache_setvp(nch, ap->a_vp);
		}
	} else {
		if (error == 0) {
			vput(tvp);
			error = EEXIST;
		}
	}
	if ((cnp.cn_flags & CNP_PDIRUNLOCK) == 0)
		vn_unlock(dvp);
	vrele(dvp);
	return (error);
}

int
vop_compat_nsymlink(struct vop_nsymlink_args *ap)
{
	struct thread *td = curthread;
	struct componentname cnp;
	struct nchandle *nch;
	struct namecache *ncp;
	struct vnode *dvp;
	struct vnode *vp;
	int error;

	/*
	 * Sanity checks, get a locked directory vnode.
	 */
	*ap->a_vpp = NULL;
	nch = ap->a_nch;		/* locked namecache node */
	ncp = nch->ncp;
	dvp = ap->a_dvp;

	if ((error = vget(dvp, LK_EXCLUSIVE)) != 0) {
		kprintf("[diagnostic] vop_compat_resolve: EAGAIN on ncp %p %s\n",
			ncp, ncp->nc_name);
		return(EAGAIN);
	}

	/*
	 * Setup the cnp for a traditional vop_old_lookup() call.  The lookup
	 * caches all information required to create the entry in the
	 * directory inode.  We expect a return code of EJUSTRETURN for
	 * the CREATE case.  The cnp must simulated a saved-name situation.
	 */
	bzero(&cnp, sizeof(cnp));
	cnp.cn_nameiop = NAMEI_CREATE;
	cnp.cn_flags = CNP_LOCKPARENT;
	cnp.cn_nameptr = ncp->nc_name;
	cnp.cn_namelen = ncp->nc_nlen;
	cnp.cn_cred = ap->a_cred;
	cnp.cn_td = td;

	vp = NULL;
	error = vop_old_lookup(ap->a_head.a_ops, dvp, &vp, &cnp);

	/*
	 * EJUSTRETURN should be returned for this case, which means that
	 * the VFS has setup the directory inode for the create.  The dvp we
	 * passed in is expected to remain in a locked state.
	 *
	 * If the VOP_OLD_SYMLINK is successful we are responsible for updating
	 * the cache state of the locked ncp that was passed to us.
	 */
	if (error == EJUSTRETURN) {
		KKASSERT((cnp.cn_flags & CNP_PDIRUNLOCK) == 0);
		error = VOP_OLD_SYMLINK(dvp, &vp, &cnp, ap->a_vap, ap->a_target);
		if (error == 0) {
			cache_setunresolved(nch);
			cache_setvp(nch, vp);
			*ap->a_vpp = vp;
		}
	} else {
		if (error == 0) {
			vput(vp);
			vp = NULL;
			error = EEXIST;
		}
		KKASSERT(vp == NULL);
	}
	if ((cnp.cn_flags & CNP_PDIRUNLOCK) == 0)
		vn_unlock(dvp);
	vrele(dvp);
	return (error);
}

/*
 * vop_compat_nwhiteout { struct nchandle *a_nch, 	XXX STOPGAP FUNCTION
 *			  struct vnode *a_dvp,
 *			  struct ucred *a_cred,
 *			  int a_flags }
 *
 * Issie a whiteout operation (create, lookup, or delete).  Compatibility 
 * requires us to issue the appropriate VOP_OLD_LOOKUP before we issue 
 * VOP_OLD_WHITEOUT in order to setup the directory inode's i_offset and i_count
 * (e.g. in UFS) for the NAMEI_CREATE and NAMEI_DELETE ops.  For NAMEI_LOOKUP
 * no lookup is necessary.
 */
int
vop_compat_nwhiteout(struct vop_nwhiteout_args *ap)
{
	struct thread *td = curthread;
	struct componentname cnp;
	struct nchandle *nch;
	struct namecache *ncp;
	struct vnode *dvp;
	struct vnode *vp;
	int error;

	/*
	 * Sanity checks, get a locked directory vnode.
	 */
	nch = ap->a_nch;		/* locked namecache node */
	ncp = nch->ncp;
	dvp = ap->a_dvp;

	if ((error = vget(dvp, LK_EXCLUSIVE)) != 0) {
		kprintf("[diagnostic] vop_compat_resolve: EAGAIN on ncp %p %s\n",
			ncp, ncp->nc_name);
		return(EAGAIN);
	}

	/*
	 * Setup the cnp for a traditional vop_old_lookup() call.  The lookup
	 * caches all information required to create the entry in the
	 * directory inode.  We expect a return code of EJUSTRETURN for
	 * the CREATE case.  The cnp must simulated a saved-name situation.
	 */
	bzero(&cnp, sizeof(cnp));
	cnp.cn_nameiop = ap->a_flags;
	cnp.cn_flags = CNP_LOCKPARENT;
	cnp.cn_nameptr = ncp->nc_name;
	cnp.cn_namelen = ncp->nc_nlen;
	cnp.cn_cred = ap->a_cred;
	cnp.cn_td = td;

	vp = NULL;

	/*
	 * EJUSTRETURN should be returned for the CREATE or DELETE cases.
	 * The VFS has setup the directory inode for the create.  The dvp we
	 * passed in is expected to remain in a locked state.
	 *
	 * If the VOP_OLD_WHITEOUT is successful we are responsible for updating
	 * the cache state of the locked ncp that was passed to us.
	 */
	switch(ap->a_flags) {
	case NAMEI_DELETE:
		cnp.cn_flags |= CNP_DOWHITEOUT;
		/* fall through */
	case NAMEI_CREATE:
		error = vop_old_lookup(ap->a_head.a_ops, dvp, &vp, &cnp);
		if (error == EJUSTRETURN) {
			KKASSERT((cnp.cn_flags & CNP_PDIRUNLOCK) == 0);
			error = VOP_OLD_WHITEOUT(dvp, &cnp, ap->a_flags);
			if (error == 0)
				cache_setunresolved(nch);
		} else {
			if (error == 0) {
				vput(vp);
				vp = NULL;
				error = EEXIST;
			}
			KKASSERT(vp == NULL);
		}
		break;
	case NAMEI_LOOKUP:
		error = VOP_OLD_WHITEOUT(dvp, NULL, ap->a_flags);
		break;
	default:
		error = EINVAL;
		break;
	}
	if ((cnp.cn_flags & CNP_PDIRUNLOCK) == 0)
		vn_unlock(dvp);
	vrele(dvp);
	return (error);
}


/*
 * vop_compat_nremove { struct nchandle *a_nch, 	XXX STOPGAP FUNCTION
 *			struct vnode *a_dvp,
 *			struct ucred *a_cred }
 */
int
vop_compat_nremove(struct vop_nremove_args *ap)
{
	struct thread *td = curthread;
	struct componentname cnp;
	struct nchandle *nch;
	struct namecache *ncp;
	struct vnode *dvp;
	struct vnode *vp;
	int error;

	/*
	 * Sanity checks, get a locked directory vnode.
	 */
	nch = ap->a_nch;		/* locked namecache node */
	ncp = nch->ncp;
	dvp = ap->a_dvp;

	if ((error = vget(dvp, LK_EXCLUSIVE)) != 0) {
		kprintf("[diagnostic] vop_compat_resolve: EAGAIN on ncp %p %s\n",
			ncp, ncp->nc_name);
		return(EAGAIN);
	}

	/*
	 * Setup the cnp for a traditional vop_old_lookup() call.  The lookup
	 * caches all information required to delete the entry in the
	 * directory inode.  We expect a return code of 0 for the DELETE
	 * case (meaning that a vp has been found).  The cnp must simulated
	 * a saved-name situation.
	 */
	bzero(&cnp, sizeof(cnp));
	cnp.cn_nameiop = NAMEI_DELETE;
	cnp.cn_flags = CNP_LOCKPARENT;
	cnp.cn_nameptr = ncp->nc_name;
	cnp.cn_namelen = ncp->nc_nlen;
	cnp.cn_cred = ap->a_cred;
	cnp.cn_td = td;

	/*
	 * The vnode must be a directory and must not represent the
	 * current directory.
	 */
	vp = NULL;
	error = vop_old_lookup(ap->a_head.a_ops, dvp, &vp, &cnp);
	if (error == 0 && vp->v_type == VDIR)
		error = EPERM;
	if (error == 0) {
		KKASSERT((cnp.cn_flags & CNP_PDIRUNLOCK) == 0);
		error = VOP_OLD_REMOVE(dvp, vp, &cnp);
		if (error == 0)
			cache_unlink(nch);
	}
	if (vp) {
		if (dvp == vp)
			vrele(vp);
		else	
			vput(vp);
	}
	if ((cnp.cn_flags & CNP_PDIRUNLOCK) == 0)
		vn_unlock(dvp);
	vrele(dvp);
	return (error);
}

/*
 * vop_compat_nrmdir { struct nchandle *a_nch, 	XXX STOPGAP FUNCTION
 *		       struct vnode *dvp,
 *		       struct ucred *a_cred }
 */
int
vop_compat_nrmdir(struct vop_nrmdir_args *ap)
{
	struct thread *td = curthread;
	struct componentname cnp;
	struct nchandle *nch;
	struct namecache *ncp;
	struct vnode *dvp;
	struct vnode *vp;
	int error;

	/*
	 * Sanity checks, get a locked directory vnode.
	 */
	nch = ap->a_nch;		/* locked namecache node */
	ncp = nch->ncp;
	dvp = ap->a_dvp;

	if ((error = vget(dvp, LK_EXCLUSIVE)) != 0) {
		kprintf("[diagnostic] vop_compat_resolve: EAGAIN on ncp %p %s\n",
			ncp, ncp->nc_name);
		return(EAGAIN);
	}

	/*
	 * Setup the cnp for a traditional vop_old_lookup() call.  The lookup
	 * caches all information required to delete the entry in the
	 * directory inode.  We expect a return code of 0 for the DELETE
	 * case (meaning that a vp has been found).  The cnp must simulated
	 * a saved-name situation.
	 */
	bzero(&cnp, sizeof(cnp));
	cnp.cn_nameiop = NAMEI_DELETE;
	cnp.cn_flags = CNP_LOCKPARENT;
	cnp.cn_nameptr = ncp->nc_name;
	cnp.cn_namelen = ncp->nc_nlen;
	cnp.cn_cred = ap->a_cred;
	cnp.cn_td = td;

	/*
	 * The vnode must be a directory and must not represent the
	 * current directory.
	 */
	vp = NULL;
	error = vop_old_lookup(ap->a_head.a_ops, dvp, &vp, &cnp);
	if (error == 0 && vp->v_type != VDIR)
		error = ENOTDIR;
	if (error == 0 && vp == dvp)
		error = EINVAL;
	if (error == 0 && (vp->v_flag & VROOT))
		error = EBUSY;
	if (error == 0) {
		KKASSERT((cnp.cn_flags & CNP_PDIRUNLOCK) == 0);
		error = VOP_OLD_RMDIR(dvp, vp, &cnp);

		/*
		 * Note that this invalidation will cause any process
		 * currently CD'd into the directory being removed to be
		 * disconnected from the topology and not be able to ".."
		 * back out.
		 */
		if (error == 0) {
			cache_inval(nch, CINV_DESTROY);
			cache_inval_vp(vp, CINV_DESTROY);
		}
	}
	if (vp) {
		if (dvp == vp)
			vrele(vp);
		else	
			vput(vp);
	}
	if ((cnp.cn_flags & CNP_PDIRUNLOCK) == 0)
		vn_unlock(dvp);
	vrele(dvp);
	return (error);
}

/*
 * vop_compat_nrename { struct nchandle *a_fnch, 	XXX STOPGAP FUNCTION
 *			struct nchandle *a_tnch,
 *			struct ucred *a_cred }
 *
 * This is a fairly difficult procedure.  The old VOP_OLD_RENAME requires that
 * the source directory and vnode be unlocked and the target directory and
 * vnode (if it exists) be locked.  All arguments will be vrele'd and 
 * the targets will also be unlocked regardless of the return code.
 */
int
vop_compat_nrename(struct vop_nrename_args *ap)
{
	struct thread *td = curthread;
	struct componentname fcnp;
	struct componentname tcnp;
	struct nchandle *fnch;
	struct nchandle *tnch;
	struct namecache *fncp;
	struct namecache *tncp;
	struct vnode *fdvp, *fvp;
	struct vnode *tdvp, *tvp;
	int error;

	/*
	 * Sanity checks, get referenced vnodes representing the source.
	 */
	fnch = ap->a_fnch;		/* locked namecache node */
	fncp = fnch->ncp;
	fdvp = ap->a_fdvp;

	/*
	 * Temporarily lock the source directory and lookup in DELETE mode to
	 * check permissions.  XXX delete permissions should have been
	 * checked by nlookup(), we need to add NLC_DELETE for delete
	 * checking.  It is unclear whether VFS's require the directory setup
	 * info NAMEI_DELETE causes to be stored in the fdvp's inode, but
	 * since it isn't locked and since UFS always does a relookup of
	 * the source, it is believed that the only side effect that matters
	 * is the permissions check.
	 */
	if ((error = vget(fdvp, LK_EXCLUSIVE)) != 0) {
		kprintf("[diagnostic] vop_compat_resolve: EAGAIN on ncp %p %s\n",
			fncp, fncp->nc_name);
		return(EAGAIN);
	}

	bzero(&fcnp, sizeof(fcnp));
	fcnp.cn_nameiop = NAMEI_DELETE;
	fcnp.cn_flags = CNP_LOCKPARENT;
	fcnp.cn_nameptr = fncp->nc_name;
	fcnp.cn_namelen = fncp->nc_nlen;
	fcnp.cn_cred = ap->a_cred;
	fcnp.cn_td = td;

	/*
	 * note: vop_old_lookup (i.e. VOP_OLD_LOOKUP) always returns a locked
	 * fvp.
	 */
	fvp = NULL;
	error = vop_old_lookup(ap->a_head.a_ops, fdvp, &fvp, &fcnp);
	if (error == 0 && (fvp->v_flag & VROOT)) {
		vput(fvp);	/* as if vop_old_lookup had failed */
		error = EBUSY;
	}
	if ((fcnp.cn_flags & CNP_PDIRUNLOCK) == 0) {
		fcnp.cn_flags |= CNP_PDIRUNLOCK;
		vn_unlock(fdvp);
	}
	if (error) {
		vrele(fdvp);
		return (error);
	}
	vn_unlock(fvp);

	/*
	 * fdvp and fvp are now referenced and unlocked.
	 *
	 * Get a locked directory vnode for the target and lookup the target
	 * in CREATE mode so it places the required information in the
	 * directory inode.
	 */
	tnch = ap->a_tnch;		/* locked namecache node */
	tncp = tnch->ncp;
	tdvp = ap->a_tdvp;
	if (error) {
		vrele(fdvp);
		vrele(fvp);
		return (error);
	}
	if ((error = vget(tdvp, LK_EXCLUSIVE)) != 0) {
		kprintf("[diagnostic] vop_compat_resolve: EAGAIN on ncp %p %s\n",
			tncp, tncp->nc_name);
		vrele(fdvp);
		vrele(fvp);
		return(EAGAIN);
	}

	/*
	 * Setup the cnp for a traditional vop_old_lookup() call.  The lookup
	 * caches all information required to create the entry in the
	 * target directory inode.
	 */
	bzero(&tcnp, sizeof(tcnp));
	tcnp.cn_nameiop = NAMEI_RENAME;
	tcnp.cn_flags = CNP_LOCKPARENT;
	tcnp.cn_nameptr = tncp->nc_name;
	tcnp.cn_namelen = tncp->nc_nlen;
	tcnp.cn_cred = ap->a_cred;
	tcnp.cn_td = td;

	tvp = NULL;
	error = vop_old_lookup(ap->a_head.a_ops, tdvp, &tvp, &tcnp);

	if (error == EJUSTRETURN) {
		/*
		 * Target does not exist.  tvp should be NULL.
		 */
		KKASSERT(tvp == NULL);
		KKASSERT((tcnp.cn_flags & CNP_PDIRUNLOCK) == 0);
		error = VOP_OLD_RENAME(fdvp, fvp, &fcnp, tdvp, tvp, &tcnp);
		if (error == 0)
			cache_rename(fnch, tnch);
	} else if (error == 0) {
		/*
		 * Target exists.  VOP_OLD_RENAME should correctly delete the
		 * target.
		 */
		KKASSERT((tcnp.cn_flags & CNP_PDIRUNLOCK) == 0);
		error = VOP_OLD_RENAME(fdvp, fvp, &fcnp, tdvp, tvp, &tcnp);
		if (error == 0)
			cache_rename(fnch, tnch);
	} else {
		vrele(fdvp);
		vrele(fvp);
		if (tcnp.cn_flags & CNP_PDIRUNLOCK)
			vrele(tdvp);
		else
			vput(tdvp);
	}
	return (error);
}

static int
vop_nolookup(struct vop_old_lookup_args *ap)
{

	*ap->a_vpp = NULL;
	return (ENOTDIR);
}

/*
 *	vop_nostrategy:
 *
 *	Strategy routine for VFS devices that have none.
 *
 *	B_ERROR and B_INVAL must be cleared prior to calling any strategy
 *	routine.  Typically this is done for a BUF_CMD_READ strategy call.
 *	Typically B_INVAL is assumed to already be clear prior to a write
 *	and should not be cleared manually unless you just made the buffer
 *	invalid.  B_ERROR should be cleared either way.
 */

static int
vop_nostrategy (struct vop_strategy_args *ap)
{
	kprintf("No strategy for buffer at %p\n", ap->a_bio->bio_buf);
	vprint("", ap->a_vp);
	ap->a_bio->bio_buf->b_flags |= B_ERROR;
	ap->a_bio->bio_buf->b_error = EOPNOTSUPP;
	biodone(ap->a_bio);
	return (EOPNOTSUPP);
}

int
vop_stdpathconf(struct vop_pathconf_args *ap)
{
	int error = 0;

	switch (ap->a_name) {
	case _PC_LINK_MAX:
		*ap->a_retval = LINK_MAX;
		break;
	case _PC_NAME_MAX:
		*ap->a_retval = NAME_MAX;
		break;
	case _PC_PATH_MAX:
		*ap->a_retval = PATH_MAX;
		break;
	case _PC_MAX_CANON:
		*ap->a_retval = MAX_CANON;
		break;
	case _PC_MAX_INPUT:
		*ap->a_retval = MAX_INPUT;
		break;
	case _PC_PIPE_BUF:
		*ap->a_retval = PIPE_BUF;
		break;
	case _PC_CHOWN_RESTRICTED:
		*ap->a_retval = 1;
		break;
	case _PC_NO_TRUNC:
		*ap->a_retval = 1;
		break;
	case _PC_VDISABLE:
		*ap->a_retval = _POSIX_VDISABLE;
		break;
	default:
		error = EINVAL;
		break;
	}
	return (error);
}

/*
 * Standard open.
 *
 * (struct vnode *a_vp, int a_mode, struct ucred *a_ucred, struct file *a_fp)
 *
 * a_mode: note, 'F' modes, e.g. FREAD, FWRITE
 */
int
vop_stdopen(struct vop_open_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct file *fp;

	if ((fp = ap->a_fp) != NULL) {
		switch(vp->v_type) {
		case VFIFO:
			fp->f_type = DTYPE_FIFO;
			break;
		default:
			fp->f_type = DTYPE_VNODE;
			break;
		}
		fp->f_flag = ap->a_mode & FMASK;
		fp->f_ops = &vnode_fileops;
		fp->f_data = vp;
		vref(vp);
	}
	if (ap->a_mode & FWRITE)
		atomic_add_int(&vp->v_writecount, 1);
	KKASSERT(vp->v_opencount >= 0 && vp->v_opencount != INT_MAX);
	atomic_add_int(&vp->v_opencount, 1);
	return (0);
}

/*
 * Standard close.
 *
 * (struct vnode *a_vp, int a_fflag)
 *
 * a_fflag: note, 'F' modes, e.g. FREAD, FWRITE.  same as a_mode in stdopen?
 */
int
vop_stdclose(struct vop_close_args *ap)
{
	struct vnode *vp = ap->a_vp;

	KASSERT(vp->v_opencount > 0,
		("VOP_STDCLOSE: BAD OPENCOUNT %p %d type=%d ops=%p flgs=%08x",
		vp, vp->v_opencount, vp->v_type, *vp->v_ops, vp->v_flag));
	if (ap->a_fflag & FWRITE) {
		KASSERT(vp->v_writecount > 0,
			("VOP_STDCLOSE: BAD WRITECOUNT %p %d",
			vp, vp->v_writecount));
		atomic_add_int(&vp->v_writecount, -1);
	}
	atomic_add_int(&vp->v_opencount, -1);
	return (0);
}

/*
 * Implement standard getpages and putpages.  All filesystems must use
 * the buffer cache to back regular files.
 */
int
vop_stdgetpages(struct vop_getpages_args *ap)
{
	struct mount *mp;
	int error;

	if ((mp = ap->a_vp->v_mount) != NULL) {
		error = vnode_pager_generic_getpages(
				ap->a_vp, ap->a_m, ap->a_count,
				ap->a_reqpage, ap->a_seqaccess);
	} else {
		error = VM_PAGER_BAD;
	}
	return (error);
}

int
vop_stdputpages(struct vop_putpages_args *ap)
{
	struct mount *mp;
	int error;

	if ((mp = ap->a_vp->v_mount) != NULL) {
		error = vnode_pager_generic_putpages(
				ap->a_vp, ap->a_m, ap->a_count,
				ap->a_sync, ap->a_rtvals);
	} else {
		error = VM_PAGER_BAD;
	}
	return (error);
}

int
vop_stdnoread(struct vop_read_args *ap)
{
	return (EINVAL);
}

int
vop_stdnowrite(struct vop_write_args *ap)
{
	return (EINVAL);
}

/* 
 * vfs default ops
 * used to fill the vfs fucntion table to get reasonable default return values.
 */
int 
vfs_stdmount(struct mount *mp, char *path, caddr_t data, struct ucred *cred)
{
	return (0);
}

int	
vfs_stdunmount(struct mount *mp, int mntflags)
{
	return (0);
}

int
vop_stdmountctl(struct vop_mountctl_args *ap)
{

	struct mount *mp;
	int error = 0;

	mp = ap->a_head.a_ops->head.vv_mount;

	switch(ap->a_op) {
	case MOUNTCTL_MOUNTFLAGS:
		/*
		 * Get a string buffer with all the mount flags
		 * names comman separated.
		 * mount(2) will use this information.
		 */
		*ap->a_res = vfs_flagstostr(mp->mnt_flag & MNT_VISFLAGMASK, NULL,
					    ap->a_buf, ap->a_buflen, &error);
		break;
	case MOUNTCTL_INSTALL_VFS_JOURNAL:
	case MOUNTCTL_RESTART_VFS_JOURNAL:
	case MOUNTCTL_REMOVE_VFS_JOURNAL:
	case MOUNTCTL_RESYNC_VFS_JOURNAL:
	case MOUNTCTL_STATUS_VFS_JOURNAL:
		error = journal_mountctl(ap);
		break;
	default:
		error = EOPNOTSUPP;
		break;
	}
	return (error);
}

int	
vfs_stdroot(struct mount *mp, struct vnode **vpp)
{
	return (EOPNOTSUPP);
}

int	
vfs_stdstatfs(struct mount *mp, struct statfs *sbp, struct ucred *cred)
{
	return (EOPNOTSUPP);
}

/*
 * If the VFS does not implement statvfs, then call statfs and convert
 * the values.  This code was taken from libc's __cvtstatvfs() function,
 * contributed by Joerg Sonnenberger.
 */
int	
vfs_stdstatvfs(struct mount *mp, struct statvfs *sbp, struct ucred *cred)
{
	struct statfs *in;
	int error;

	in = &mp->mnt_stat;
	error = VFS_STATFS(mp, in, cred);
	if (error == 0) {
		bzero(sbp, sizeof(*sbp));

		sbp->f_bsize = in->f_bsize;
		sbp->f_frsize = in->f_bsize;
		sbp->f_blocks = in->f_blocks;
		sbp->f_bfree = in->f_bfree;
		sbp->f_bavail = in->f_bavail;
		sbp->f_files = in->f_files;
		sbp->f_ffree = in->f_ffree;

		/*
		 * XXX
		 * This field counts the number of available inodes to non-root
		 * users, but this information is not available via statfs.
		 * Just ignore this issue by returning the total number 
		 * instead.
		 */
		sbp->f_favail = in->f_ffree;

		/*
		 * XXX
		 * This field has a different meaning for statfs and statvfs.
		 * For the former it is the cookie exported for NFS and not
		 * intended for normal userland use.
		 */
		sbp->f_fsid = 0;

		sbp->f_flag = 0;
		if (in->f_flags & MNT_RDONLY)
			sbp->f_flag |= ST_RDONLY;
		if (in->f_flags & MNT_NOSUID)
			sbp->f_flag |= ST_NOSUID;
		sbp->f_namemax = 0;
		sbp->f_owner = in->f_owner;
		/*
		 * XXX
		 * statfs contains the type as string, statvfs expects it as
		 * enumeration.
		 */
		sbp->f_type = 0;

		sbp->f_syncreads = in->f_syncreads;
		sbp->f_syncwrites = in->f_syncwrites;
		sbp->f_asyncreads = in->f_asyncreads;
		sbp->f_asyncwrites = in->f_asyncwrites;
	}
	return (error);
}

int
vfs_stdvptofh(struct vnode *vp, struct fid *fhp)
{
	return (EOPNOTSUPP);
}

int	
vfs_stdstart(struct mount *mp, int flags)
{
	return (0);
}

int	
vfs_stdquotactl(struct mount *mp, int cmds, uid_t uid,
	caddr_t arg, struct ucred *cred)
{
	return (EOPNOTSUPP);
}

int	
vfs_stdsync(struct mount *mp, int waitfor)
{
	return (0);
}

int
vfs_stdnosync(struct mount *mp, int waitfor)
{
	return (EOPNOTSUPP);
}

int	
vfs_stdvget(struct mount *mp, struct vnode *dvp, ino_t ino, struct vnode **vpp)
{
	return (EOPNOTSUPP);
}

int	
vfs_stdfhtovp(struct mount *mp, struct vnode *rootvp,
	      struct fid *fhp, struct vnode **vpp)
{
	return (EOPNOTSUPP);
}

int 
vfs_stdcheckexp(struct mount *mp, struct sockaddr *nam, int *extflagsp,
	struct ucred **credanonp)
{
	return (EOPNOTSUPP);
}

int
vfs_stdinit(struct vfsconf *vfsp)
{
	return (0);
}

int
vfs_stduninit(struct vfsconf *vfsp)
{
	return(0);
}

int
vfs_stdextattrctl(struct mount *mp, int cmd, struct vnode *vp,
		 int attrnamespace, const char *attrname,
		 struct ucred *cred)
{
	return(EOPNOTSUPP);
}

#define ACCOUNTING_NB_FSTYPES 7

static const char *accounting_fstypes[ACCOUNTING_NB_FSTYPES] = {
	"ext2fs", "hammer", "mfs", "ntfs", "null", "tmpfs", "ufs" };

int
vfs_stdac_init(struct mount *mp)
{
	const char* fs_type;
	int i, fstype_ok = 0;

	/* is mounted fs type one we want to do some accounting for ? */
	for (i=0; i<ACCOUNTING_NB_FSTYPES; i++) {
		fs_type = accounting_fstypes[i];
		if (strncmp(mp->mnt_stat.f_fstypename, fs_type,
					sizeof(mp->mnt_stat)) == 0) {
			fstype_ok = 1;
			break;
		}
	}
	if (fstype_ok == 0)
		return (0);

	vq_init(mp);
	return (0);
}

void
vfs_stdac_done(struct mount *mp)
{
	vq_done(mp);
}

void
vfs_stdncpgen_set(struct mount *mp, struct namecache *ncp)
{
}

int
vfs_stdncpgen_test(struct mount *mp, struct namecache *ncp)
{
	return 0;
}
/* end of vfs default ops */
