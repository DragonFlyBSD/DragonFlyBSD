/*
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed
 * to Berkeley by John Heidemann of the UCLA Ficus project.
 *
 * Source: * @(#)i405_init.c 2.10 92/04/27 UCLA Ficus project
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
 *
 * $FreeBSD: src/sys/kern/vfs_default.c,v 1.28.2.7 2003/01/10 18:23:26 bde Exp $
 * $DragonFly: src/sys/kern/vfs_default.c,v 1.22 2004/11/12 00:09:24 dillon Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mount.h>
#include <sys/unistd.h>
#include <sys/vnode.h>
#include <sys/namei.h>
#include <sys/nlookup.h>
#include <sys/poll.h>

#include <machine/limits.h>

#include <vm/vm.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <vm/vm_pager.h>
#include <vm/vnode_pager.h>

static int	vop_nolookup (struct vop_lookup_args *);
static int	vop_nostrategy (struct vop_strategy_args *);

/*
 * This vnode table stores what we want to do if the filesystem doesn't
 * implement a particular VOP.
 *
 * If there is no specific entry here, we will return EOPNOTSUPP.
 */
struct vop_ops *default_vnode_vops;
static struct vnodeopv_entry_desc default_vnodeop_entries[] = {
	{ &vop_default_desc,		vop_eopnotsupp },
	{ &vop_advlock_desc,		vop_einval },
	{ &vop_bwrite_desc,		(void *) vop_stdbwrite },
	{ &vop_close_desc,		vop_null },
	{ &vop_createvobject_desc,	(void *) vop_stdcreatevobject },
	{ &vop_destroyvobject_desc,	(void *) vop_stddestroyvobject },
	{ &vop_fsync_desc,		vop_null },
	{ &vop_getvobject_desc,		(void *) vop_stdgetvobject },
	{ &vop_ioctl_desc,		vop_enotty },
	{ &vop_islocked_desc,		(void *) vop_stdislocked },
	{ &vop_lease_desc,		vop_null },
	{ &vop_lock_desc,		(void *) vop_stdlock },
	{ &vop_mmap_desc,		vop_einval },
	{ &vop_lookup_desc,		(void *) vop_nolookup },
	{ &vop_open_desc,		vop_null },
	{ &vop_pathconf_desc,		vop_einval },
	{ &vop_poll_desc,		(void *) vop_nopoll },
	{ &vop_readlink_desc,		vop_einval },
	{ &vop_reallocblks_desc,	vop_eopnotsupp },
	{ &vop_revoke_desc,		(void *) vop_stdrevoke },
	{ &vop_strategy_desc,		(void *) vop_nostrategy },
	{ &vop_unlock_desc,		(void *) vop_stdunlock },
	{ &vop_getacl_desc,		vop_eopnotsupp },
	{ &vop_setacl_desc,		vop_eopnotsupp },
	{ &vop_aclcheck_desc,		vop_eopnotsupp },
	{ &vop_getextattr_desc,		vop_eopnotsupp },
	{ &vop_setextattr_desc,		vop_eopnotsupp },
	{ &vop_nresolve_desc,		(void *) vop_compat_nresolve },
	{ &vop_nlookupdotdot_desc,	(void *) vop_compat_nlookupdotdot },
	{ &vop_ncreate_desc,		(void *) vop_compat_ncreate },
	{ &vop_nmkdir_desc,		(void *) vop_compat_nmkdir },
	{ &vop_nmknod_desc,		(void *) vop_compat_nmknod },
	{ &vop_nlink_desc,		(void *) vop_compat_nlink },
	{ &vop_nsymlink_desc,		(void *) vop_compat_nsymlink },
	{ &vop_nwhiteout_desc,		(void *) vop_compat_nwhiteout },
	{ &vop_nremove_desc,		(void *) vop_compat_nremove },
	{ &vop_nrmdir_desc,		(void *) vop_compat_nrmdir },
	{ &vop_nrename_desc,		(void *) vop_compat_nrename },
	{ NULL, NULL }
};

static struct vnodeopv_desc default_vnodeop_opv_desc =
        { &default_vnode_vops, default_vnodeop_entries };

VNODEOP_SET(default_vnodeop_opv_desc);

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
vop_null(struct vop_generic_args *ap)
{
	return (0);
}

int
vop_defaultop(struct vop_generic_args *ap)
{
	return (VOCALL(default_vnode_vops, ap));
}

int
vop_panic(struct vop_generic_args *ap)
{

	panic("filesystem goof: vop_panic[%s]", ap->a_desc->vdesc_name);
}

/*
 * vop_compat_resolve { struct namecache *a_ncp }	XXX STOPGAP FUNCTION
 *
 * XXX OLD API ROUTINE!  WHEN ALL VFSs HAVE BEEN CLEANED UP THIS PROCEDURE
 * WILL BE REMOVED.  This procedure exists for all VFSs which have not
 * yet implemented VOP_NRESOLVE().  It converts VOP_NRESOLVE() into a 
 * vop_lookup() and does appropriate translations.
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
	struct namecache *ncp;
	struct componentname cnp;

	ncp = ap->a_ncp;	/* locked namecache node */
	if (ncp->nc_flag & NCF_MOUNTPT)	/* can't cross a mount point! */
		return(EPERM);
	if (ncp->nc_parent == NULL)
		return(EPERM);
	if ((dvp = ncp->nc_parent->nc_vp) == NULL)
		return(EPERM);

	/*
	 * UFS currently stores all sorts of side effects, including a loop
	 * variable, in the directory inode.  That needs to be fixed and the
	 * other VFS's audited before we can switch to LK_SHARED.
	 */
	if ((error = vget(dvp, LK_EXCLUSIVE, curthread)) != 0) {
		printf("[diagnostic] vop_compat_resolve: EAGAIN on ncp %p %s\n",
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
	 * vop_lookup() always returns vp locked.  dvp may or may not be
	 * left locked depending on CNP_PDIRUNLOCK.
	 */
	error = vop_lookup(ap->a_head.a_ops, dvp, &vp, &cnp);
	if (error == 0)
		VOP_UNLOCK(vp, 0, curthread);
	if ((cnp.cn_flags & CNP_PDIRUNLOCK) == 0)
		VOP_UNLOCK(dvp, 0, curthread);
	if ((ncp->nc_flag & NCF_UNRESOLVED) == 0) {
		/* was resolved by another process while we were unlocked */
		if (error == 0)
			vrele(vp);
	} else if (error == 0) {
		KKASSERT(vp != NULL);
		cache_setvp(ncp, vp);
		vrele(vp);
	} else if (error == ENOENT) {
		KKASSERT(vp == NULL);
		if (cnp.cn_flags & CNP_ISWHITEOUT)
			ncp->nc_flag |= NCF_WHITEOUT;
		cache_setvp(ncp, NULL);
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
	if ((error = vget(ap->a_dvp, LK_EXCLUSIVE, curthread)) != 0)
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
	 * vop_lookup() always returns vp locked.  dvp may or may not be
	 * left locked depending on CNP_PDIRUNLOCK.
	 */
	error = vop_lookup(ap->a_head.a_ops, ap->a_dvp, ap->a_vpp, &cnp);
	if (error == 0)
		VOP_UNLOCK(*ap->a_vpp, 0, curthread);
	if (cnp.cn_flags & CNP_PDIRUNLOCK)
		vrele(ap->a_dvp);
	else
		vput(ap->a_dvp);
	return (error);
}

/*
 * vop_compat_ncreate { struct namecache *a_ncp, 	XXX STOPGAP FUNCTION
 *			struct vnode *a_vpp,
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
	struct namecache *ncp;
	struct vnode *dvp;
	int error;

	/*
	 * Sanity checks, get a locked directory vnode.
	 */
	ncp = ap->a_ncp;		/* locked namecache node */
	if (ncp->nc_flag & NCF_MOUNTPT)	/* can't cross a mount point! */
		return(EPERM);
	if (ncp->nc_parent == NULL)
		return(EPERM);
	if ((dvp = ncp->nc_parent->nc_vp) == NULL)
		return(EPERM);

	if ((error = vget(dvp, LK_EXCLUSIVE, td)) != 0) {
		printf("[diagnostic] vop_compat_resolve: EAGAIN on ncp %p %s\n",
			ncp, ncp->nc_name);
		return(EAGAIN);
	}

	/*
	 * Setup the cnp for a traditional vop_lookup() call.  The lookup
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

	error = vop_lookup(ap->a_head.a_ops, dvp, ap->a_vpp, &cnp);

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
		VOP_LEASE(dvp, td, ap->a_cred, LEASE_WRITE);
		error = VOP_OLD_CREATE(dvp, ap->a_vpp, &cnp, ap->a_vap);
		if (error == 0) {
			cache_setunresolved(ncp);
			cache_setvp(ncp, *ap->a_vpp);
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
		VOP_UNLOCK(dvp, 0, td);
	vrele(dvp);
	return (error);
}

/*
 * vop_compat_nmkdir { struct namecache *a_ncp, 	XXX STOPGAP FUNCTION
 *			struct vnode *a_vpp,
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
	struct namecache *ncp;
	struct vnode *dvp;
	int error;

	/*
	 * Sanity checks, get a locked directory vnode.
	 */
	ncp = ap->a_ncp;		/* locked namecache node */
	if (ncp->nc_flag & NCF_MOUNTPT)	/* can't cross a mount point! */
		return(EPERM);
	if (ncp->nc_parent == NULL)
		return(EPERM);
	if ((dvp = ncp->nc_parent->nc_vp) == NULL)
		return(EPERM);

	if ((error = vget(dvp, LK_EXCLUSIVE, td)) != 0) {
		printf("[diagnostic] vop_compat_resolve: EAGAIN on ncp %p %s\n",
			ncp, ncp->nc_name);
		return(EAGAIN);
	}

	/*
	 * Setup the cnp for a traditional vop_lookup() call.  The lookup
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

	error = vop_lookup(ap->a_head.a_ops, dvp, ap->a_vpp, &cnp);

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
		VOP_LEASE(dvp, td, ap->a_cred, LEASE_WRITE);
		error = VOP_OLD_MKDIR(dvp, ap->a_vpp, &cnp, ap->a_vap);
		if (error == 0) {
			cache_setunresolved(ncp);
			cache_setvp(ncp, *ap->a_vpp);
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
		VOP_UNLOCK(dvp, 0, td);
	vrele(dvp);
	return (error);
}

/*
 * vop_compat_nmknod { struct namecache *a_ncp, 	XXX STOPGAP FUNCTION
 *			struct vnode *a_vpp,
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
	struct namecache *ncp;
	struct vnode *dvp;
	int error;

	/*
	 * Sanity checks, get a locked directory vnode.
	 */
	ncp = ap->a_ncp;		/* locked namecache node */
	if (ncp->nc_flag & NCF_MOUNTPT)	/* can't cross a mount point! */
		return(EPERM);
	if (ncp->nc_parent == NULL)
		return(EPERM);
	if ((dvp = ncp->nc_parent->nc_vp) == NULL)
		return(EPERM);

	if ((error = vget(dvp, LK_EXCLUSIVE, td)) != 0) {
		printf("[diagnostic] vop_compat_resolve: EAGAIN on ncp %p %s\n",
			ncp, ncp->nc_name);
		return(EAGAIN);
	}

	/*
	 * Setup the cnp for a traditional vop_lookup() call.  The lookup
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

	error = vop_lookup(ap->a_head.a_ops, dvp, ap->a_vpp, &cnp);

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
		VOP_LEASE(dvp, td, ap->a_cred, LEASE_WRITE);
		error = VOP_OLD_MKNOD(dvp, ap->a_vpp, &cnp, ap->a_vap);
		if (error == 0) {
			cache_setunresolved(ncp);
			cache_setvp(ncp, *ap->a_vpp);
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
		VOP_UNLOCK(dvp, 0, td);
	vrele(dvp);
	return (error);
}

/*
 * vop_compat_nlink { struct namecache *a_ncp, 	XXX STOPGAP FUNCTION
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
	struct namecache *ncp;
	struct vnode *dvp;
	struct vnode *tvp;
	int error;

	/*
	 * Sanity checks, get a locked directory vnode.
	 */
	ncp = ap->a_ncp;		/* locked namecache node */
	if (ncp->nc_flag & NCF_MOUNTPT)	/* can't cross a mount point! */
		return(EPERM);
	if (ncp->nc_parent == NULL)
		return(EPERM);
	if ((dvp = ncp->nc_parent->nc_vp) == NULL)
		return(EPERM);

	if ((error = vget(dvp, LK_EXCLUSIVE, td)) != 0) {
		printf("[diagnostic] vop_compat_resolve: EAGAIN on ncp %p %s\n",
			ncp, ncp->nc_name);
		return(EAGAIN);
	}

	/*
	 * Setup the cnp for a traditional vop_lookup() call.  The lookup
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

	tvp = NULL;
	error = vop_lookup(ap->a_head.a_ops, dvp, &tvp, &cnp);

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
		VOP_LEASE(dvp, td, ap->a_cred, LEASE_WRITE);
		VOP_LEASE(ap->a_vp, td, ap->a_cred, LEASE_WRITE);
		error = VOP_OLD_LINK(dvp, ap->a_vp, &cnp);
		if (error == 0) {
			cache_setunresolved(ncp);
			cache_setvp(ncp, ap->a_vp);
		}
	} else {
		if (error == 0) {
			vput(tvp);
			error = EEXIST;
		}
	}
	if ((cnp.cn_flags & CNP_PDIRUNLOCK) == 0)
		VOP_UNLOCK(dvp, 0, td);
	vrele(dvp);
	return (error);
}

int
vop_compat_nsymlink(struct vop_nsymlink_args *ap)
{
	struct thread *td = curthread;
	struct componentname cnp;
	struct namecache *ncp;
	struct vnode *dvp;
	struct vnode *vp;
	int error;

	/*
	 * Sanity checks, get a locked directory vnode.
	 */
	*ap->a_vpp = NULL;
	ncp = ap->a_ncp;		/* locked namecache node */
	if (ncp->nc_flag & NCF_MOUNTPT)	/* can't cross a mount point! */
		return(EPERM);
	if (ncp->nc_parent == NULL)
		return(EPERM);
	if ((dvp = ncp->nc_parent->nc_vp) == NULL)
		return(EPERM);

	if ((error = vget(dvp, LK_EXCLUSIVE, td)) != 0) {
		printf("[diagnostic] vop_compat_resolve: EAGAIN on ncp %p %s\n",
			ncp, ncp->nc_name);
		return(EAGAIN);
	}

	/*
	 * Setup the cnp for a traditional vop_lookup() call.  The lookup
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
	error = vop_lookup(ap->a_head.a_ops, dvp, &vp, &cnp);

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
		VOP_LEASE(dvp, td, ap->a_cred, LEASE_WRITE);
		error = VOP_OLD_SYMLINK(dvp, &vp, &cnp, ap->a_vap, ap->a_target);
		if (error == 0) {
			cache_setunresolved(ncp);
			cache_setvp(ncp, vp);
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
		VOP_UNLOCK(dvp, 0, td);
	vrele(dvp);
	return (error);
}

/*
 * vop_compat_nwhiteout { struct namecache *a_ncp, 	XXX STOPGAP FUNCTION
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
	struct namecache *ncp;
	struct vnode *dvp;
	struct vnode *vp;
	int error;

	/*
	 * Sanity checks, get a locked directory vnode.
	 */
	ncp = ap->a_ncp;		/* locked namecache node */
	if (ncp->nc_flag & NCF_MOUNTPT)	/* can't cross a mount point! */
		return(EPERM);
	if (ncp->nc_parent == NULL)
		return(EPERM);
	if ((dvp = ncp->nc_parent->nc_vp) == NULL)
		return(EPERM);

	if ((error = vget(dvp, LK_EXCLUSIVE, td)) != 0) {
		printf("[diagnostic] vop_compat_resolve: EAGAIN on ncp %p %s\n",
			ncp, ncp->nc_name);
		return(EAGAIN);
	}

	/*
	 * Setup the cnp for a traditional vop_lookup() call.  The lookup
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
		error = vop_lookup(ap->a_head.a_ops, dvp, &vp, &cnp);
		if (error == EJUSTRETURN) {
			KKASSERT((cnp.cn_flags & CNP_PDIRUNLOCK) == 0);
			VOP_LEASE(dvp, td, ap->a_cred, LEASE_WRITE);
			error = VOP_OLD_WHITEOUT(dvp, &cnp, ap->a_flags);
			if (error == 0)
				cache_setunresolved(ncp);
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
		VOP_UNLOCK(dvp, 0, td);
	vrele(dvp);
	return (error);
}


/*
 * vop_compat_nremove { struct namecache *a_ncp, 	XXX STOPGAP FUNCTION
 *			  struct ucred *a_cred }
 */
int
vop_compat_nremove(struct vop_nremove_args *ap)
{
	struct thread *td = curthread;
	struct componentname cnp;
	struct namecache *ncp;
	struct vnode *dvp;
	struct vnode *vp;
	int error;

	/*
	 * Sanity checks, get a locked directory vnode.
	 */
	ncp = ap->a_ncp;		/* locked namecache node */
	if (ncp->nc_flag & NCF_MOUNTPT)	/* can't cross a mount point! */
		return(EPERM);
	if (ncp->nc_parent == NULL)
		return(EPERM);
	if ((dvp = ncp->nc_parent->nc_vp) == NULL)
		return(EPERM);

	if ((error = vget(dvp, LK_EXCLUSIVE, td)) != 0) {
		printf("[diagnostic] vop_compat_resolve: EAGAIN on ncp %p %s\n",
			ncp, ncp->nc_name);
		return(EAGAIN);
	}

	/*
	 * Setup the cnp for a traditional vop_lookup() call.  The lookup
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
	error = vop_lookup(ap->a_head.a_ops, dvp, &vp, &cnp);
	if (error == 0 && vp->v_type == VDIR)
		error = EPERM;
	if (error == 0) {
		KKASSERT((cnp.cn_flags & CNP_PDIRUNLOCK) == 0);
		VOP_LEASE(dvp, td, ap->a_cred, LEASE_WRITE);
		VOP_LEASE(vp, td, ap->a_cred, LEASE_WRITE);
		error = VOP_OLD_REMOVE(dvp, vp, &cnp);
		if (error == 0) {
			cache_setunresolved(ncp);
			cache_setvp(ncp, NULL);
		}
	}
	if (vp) {
		if (dvp == vp)
			vrele(vp);
		else	
			vput(vp);
	}
	if ((cnp.cn_flags & CNP_PDIRUNLOCK) == 0)
		VOP_UNLOCK(dvp, 0, td);
	vrele(dvp);
	return (error);
}

/*
 * vop_compat_nrmdir { struct namecache *a_ncp, 	XXX STOPGAP FUNCTION
 *			  struct ucred *a_cred }
 */
int
vop_compat_nrmdir(struct vop_nrmdir_args *ap)
{
	struct thread *td = curthread;
	struct componentname cnp;
	struct namecache *ncp;
	struct vnode *dvp;
	struct vnode *vp;
	int error;

	/*
	 * Sanity checks, get a locked directory vnode.
	 */
	ncp = ap->a_ncp;		/* locked namecache node */
	if (ncp->nc_flag & NCF_MOUNTPT)	/* can't cross a mount point! */
		return(EPERM);
	if (ncp->nc_parent == NULL)
		return(EPERM);
	if ((dvp = ncp->nc_parent->nc_vp) == NULL)
		return(EPERM);

	if ((error = vget(dvp, LK_EXCLUSIVE, td)) != 0) {
		printf("[diagnostic] vop_compat_resolve: EAGAIN on ncp %p %s\n",
			ncp, ncp->nc_name);
		return(EAGAIN);
	}

	/*
	 * Setup the cnp for a traditional vop_lookup() call.  The lookup
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
	error = vop_lookup(ap->a_head.a_ops, dvp, &vp, &cnp);
	if (error == 0 && vp->v_type != VDIR)
		error = ENOTDIR;
	if (error == 0 && vp == dvp)
		error = EINVAL;
	if (error == 0 && (vp->v_flag & VROOT))
		error = EBUSY;
	if (error == 0) {
		KKASSERT((cnp.cn_flags & CNP_PDIRUNLOCK) == 0);
		VOP_LEASE(dvp, td, ap->a_cred, LEASE_WRITE);
		VOP_LEASE(vp, td, ap->a_cred, LEASE_WRITE);
		error = VOP_OLD_RMDIR(dvp, vp, &cnp);

		/*
		 * Note that this invalidation will cause any process
		 * currently CD'd into the directory being removed to be
		 * disconnected from the topology and not be able to ".."
		 * back out.
		 */
		if (error == 0)
			cache_inval(ncp, CINV_SELF|CINV_PARENT);
	}
	if (vp) {
		if (dvp == vp)
			vrele(vp);
		else	
			vput(vp);
	}
	if ((cnp.cn_flags & CNP_PDIRUNLOCK) == 0)
		VOP_UNLOCK(dvp, 0, td);
	vrele(dvp);
	return (error);
}

/*
 * vop_compat_nrename { struct namecache *a_fncp, 	XXX STOPGAP FUNCTION
 *			struct namecache *a_tncp,
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
	struct namecache *fncp;
	struct namecache *tncp;
	struct vnode *fdvp, *fvp;
	struct vnode *tdvp, *tvp;
	int error;

	/*
	 * Sanity checks, get referenced vnodes representing the source.
	 */
	fncp = ap->a_fncp;		/* locked namecache node */
	if (fncp->nc_flag & NCF_MOUNTPT) /* can't cross a mount point! */
		return(EPERM);
	if (fncp->nc_parent == NULL)
		return(EPERM);
	if ((fdvp = fncp->nc_parent->nc_vp) == NULL)
		return(EPERM);

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
	if ((error = vget(fdvp, LK_EXCLUSIVE, td)) != 0) {
		printf("[diagnostic] vop_compat_resolve: EAGAIN on ncp %p %s\n",
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
	 * note: vop_lookup (i.e. VOP_OLD_LOOKUP) always returns a locked
	 * fvp.
	 */
	fvp = NULL;
	error = vop_lookup(ap->a_head.a_ops, fdvp, &fvp, &fcnp);
	if (error == 0 && (fvp->v_flag & VROOT)) {
		vput(fvp);	/* as if vop_lookup had failed */
		error = EBUSY;
	}
	if ((fcnp.cn_flags & CNP_PDIRUNLOCK) == 0) {
		fcnp.cn_flags |= CNP_PDIRUNLOCK;
		VOP_UNLOCK(fdvp, 0, td);
	}
	if (error) {
		vrele(fdvp);
		return (error);
	}
	VOP_UNLOCK(fvp, 0, td);

	/*
	 * fdvp and fvp are now referenced and unlocked.
	 *
	 * Get a locked directory vnode for the target and lookup the target
	 * in CREATE mode so it places the required information in the
	 * directory inode.
	 */
	tncp = ap->a_tncp;		/* locked namecache node */
	if (tncp->nc_flag & NCF_MOUNTPT) /* can't cross a mount point! */
		error = EPERM;
	if (tncp->nc_parent == NULL)
		error = EPERM;
	if ((tdvp = tncp->nc_parent->nc_vp) == NULL)
		error = EPERM;
	if (error) {
		vrele(fdvp);
		vrele(fvp);
		return (error);
	}
	if ((error = vget(tdvp, LK_EXCLUSIVE, td)) != 0) {
		printf("[diagnostic] vop_compat_resolve: EAGAIN on ncp %p %s\n",
			tncp, tncp->nc_name);
		vrele(fdvp);
		vrele(fvp);
		return(EAGAIN);
	}

	/*
	 * Setup the cnp for a traditional vop_lookup() call.  The lookup
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
	error = vop_lookup(ap->a_head.a_ops, tdvp, &tvp, &tcnp);

	if (error == EJUSTRETURN) {
		/*
		 * Target does not exist.  tvp should be NULL.
		 */
		KKASSERT(tvp == NULL);
		KKASSERT((tcnp.cn_flags & CNP_PDIRUNLOCK) == 0);
		error = VOP_OLD_RENAME(fdvp, fvp, &fcnp, tdvp, tvp, &tcnp);
		if (error == 0) {
			cache_rename(fncp, tncp);
			cache_setvp(tncp, fvp);
		}
	} else if (error == 0) {
		/*
		 * Target exists.  VOP_OLD_RENAME should correctly delete the
		 * target.
		 */
		KKASSERT((tcnp.cn_flags & CNP_PDIRUNLOCK) == 0);
		error = VOP_OLD_RENAME(fdvp, fvp, &fcnp, tdvp, tvp, &tcnp);
		if (error == 0) {
			cache_rename(fncp, tncp);
			cache_setvp(tncp, fvp);
		}
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
vop_nolookup(ap)
	struct vop_lookup_args /* {
		struct vnode *a_dvp;
		struct vnode **a_vpp;
		struct componentname *a_cnp;
	} */ *ap;
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
 *	routine.  Typically this is done for a B_READ strategy call.  Typically
 *	B_INVAL is assumed to already be clear prior to a write and should not
 *	be cleared manually unless you just made the buffer invalid.  B_ERROR
 *	should be cleared either way.
 */

static int
vop_nostrategy (struct vop_strategy_args *ap)
{
	printf("No strategy for buffer at %p\n", ap->a_bp);
	vprint("", ap->a_vp);
	vprint("", ap->a_bp->b_vp);
	ap->a_bp->b_flags |= B_ERROR;
	ap->a_bp->b_error = EOPNOTSUPP;
	biodone(ap->a_bp);
	return (EOPNOTSUPP);
}

int
vop_stdpathconf(ap)
	struct vop_pathconf_args /* {
	struct vnode *a_vp;
	int a_name;
	int *a_retval;
	} */ *ap;
{

	switch (ap->a_name) {
		case _PC_LINK_MAX:
			*ap->a_retval = LINK_MAX;
			return (0);
		case _PC_MAX_CANON:
			*ap->a_retval = MAX_CANON;
			return (0);
		case _PC_MAX_INPUT:
			*ap->a_retval = MAX_INPUT;
			return (0);
		case _PC_PIPE_BUF:
			*ap->a_retval = PIPE_BUF;
			return (0);
		case _PC_CHOWN_RESTRICTED:
			*ap->a_retval = 1;
			return (0);
		case _PC_VDISABLE:
			*ap->a_retval = _POSIX_VDISABLE;
			return (0);
		default:
			return (EINVAL);
	}
	/* NOTREACHED */
}

/*
 * Standard lock.  The lock is recursive-capable only if the lock was
 * initialized with LK_CANRECURSE or that flag is passed in a_flags.
 */
int
vop_stdlock(ap)
	struct vop_lock_args /* {
		struct vnode *a_vp;
		int a_flags;
		struct proc *a_p;
	} */ *ap;
{               
	int error;

#ifndef	DEBUG_LOCKS
	error = lockmgr(&ap->a_vp->v_lock, ap->a_flags, NULL, ap->a_td);
#else
	error = debuglockmgr(&ap->a_vp->v_lock, ap->a_flags,
			NULL, ap->a_td,
			"vop_stdlock", ap->a_vp->filename, ap->a_vp->line);
#endif
	return(error);
}

int
vop_stdunlock(ap)
	struct vop_unlock_args /* {
		struct vnode *a_vp;
		int a_flags;
		struct thread *a_td;
	} */ *ap;
{
	int error;

	error = lockmgr(&ap->a_vp->v_lock, ap->a_flags | LK_RELEASE,
			NULL, ap->a_td);
	return(error);
}

int
vop_stdislocked(ap)
	struct vop_islocked_args /* {
		struct vnode *a_vp;
		struct thread *a_td;
	} */ *ap;
{
	return (lockstatus(&ap->a_vp->v_lock, ap->a_td));
}

/*
 * Return true for select/poll.
 */
int
vop_nopoll(ap)
	struct vop_poll_args /* {
		struct vnode *a_vp;
		int  a_events;
		struct ucred *a_cred;
		struct proc *a_p;
	} */ *ap;
{
	/*
	 * Return true for read/write.  If the user asked for something
	 * special, return POLLNVAL, so that clients have a way of
	 * determining reliably whether or not the extended
	 * functionality is present without hard-coding knowledge
	 * of specific filesystem implementations.
	 */
	if (ap->a_events & ~POLLSTANDARD)
		return (POLLNVAL);

	return (ap->a_events & (POLLIN | POLLOUT | POLLRDNORM | POLLWRNORM));
}

/*
 * Implement poll for local filesystems that support it.
 */
int
vop_stdpoll(ap)
	struct vop_poll_args /* {
		struct vnode *a_vp;
		int  a_events;
		struct ucred *a_cred;
		struct thread *a_td;
	} */ *ap;
{
	if (ap->a_events & ~POLLSTANDARD)
		return (vn_pollrecord(ap->a_vp, ap->a_td, ap->a_events));
	return (ap->a_events & (POLLIN | POLLOUT | POLLRDNORM | POLLWRNORM));
}

int
vop_stdbwrite(ap)
	struct vop_bwrite_args *ap;
{
	return (bwrite(ap->a_bp));
}

int
vop_stdcreatevobject(ap)
	struct vop_createvobject_args /* {
		struct vnode *a_vp;
		struct proc *a_td;
	} */ *ap;
{
	struct vnode *vp = ap->a_vp;
	struct thread *td = ap->a_td;
	struct vattr vat;
	vm_object_t object;
	int error = 0;

	if (!vn_isdisk(vp, NULL) && vn_canvmio(vp) == FALSE)
		return (0);

retry:
	if ((object = vp->v_object) == NULL) {
		if (vp->v_type == VREG || vp->v_type == VDIR) {
			if ((error = VOP_GETATTR(vp, &vat, td)) != 0)
				goto retn;
			object = vnode_pager_alloc(vp, vat.va_size, 0, 0);
		} else if (vp->v_rdev && dev_is_good(vp->v_rdev)) {
			/*
			 * XXX v_rdev uses NULL/non-NULL instead of NODEV
			 *
			 * This simply allocates the biggest object possible
			 * for a disk vnode.  This should be fixed, but doesn't
			 * cause any problems (yet).
			 */
			object = vnode_pager_alloc(vp, IDX_TO_OFF(INT_MAX), 0, 0);
		} else {
			goto retn;
		}
		/*
		 * Dereference the reference we just created.  This assumes
		 * that the object is associated with the vp.
		 */
		object->ref_count--;
		vp->v_usecount--;
	} else {
		if (object->flags & OBJ_DEAD) {
			VOP_UNLOCK(vp, 0, td);
			tsleep(object, 0, "vodead", 0);
			vn_lock(vp, LK_EXCLUSIVE | LK_RETRY, td);
			goto retry;
		}
	}

	KASSERT(vp->v_object != NULL, ("vfs_object_create: NULL object"));
	vp->v_flag |= VOBJBUF;

retn:
	return (error);
}

int
vop_stddestroyvobject(ap)
	struct vop_destroyvobject_args /* {
		struct vnode *vp;
	} */ *ap;
{
	struct vnode *vp = ap->a_vp;
	vm_object_t obj = vp->v_object;

	if (vp->v_object == NULL)
		return (0);

	if (obj->ref_count == 0) {
		/*
		 * vclean() may be called twice. The first time
		 * removes the primary reference to the object,
		 * the second time goes one further and is a
		 * special-case to terminate the object.
		 *
		 * don't double-terminate the object.
		 */
		if ((obj->flags & OBJ_DEAD) == 0)
			vm_object_terminate(obj);
	} else {
		/*
		 * Woe to the process that tries to page now :-).
		 */
		vm_pager_deallocate(obj);
	}
	return (0);
}

/*
 * Return the underlying VM object.  This routine may be called with or
 * without the vnode interlock held.  If called without, the returned
 * object is not guarenteed to be valid.  The syncer typically gets the
 * object without holding the interlock in order to quickly test whether
 * it might be dirty before going heavy-weight.  vm_object's use zalloc
 * and thus stable-storage, so this is safe.
 */
int
vop_stdgetvobject(ap)
	struct vop_getvobject_args /* {
		struct vnode *vp;
		struct vm_object **objpp;
	} */ *ap;
{
	struct vnode *vp = ap->a_vp;
	struct vm_object **objpp = ap->a_objpp;

	if (objpp)
		*objpp = vp->v_object;
	return (vp->v_object ? 0 : EINVAL);
}

/* 
 * vfs default ops
 * used to fill the vfs fucntion table to get reasonable default return values.
 */
int 
vfs_stdmount(struct mount *mp, char *path, caddr_t data,
	struct nlookupdata *nd, struct thread *td)
{
	return (0);
}

int	
vfs_stdunmount(struct mount *mp, int mntflags, struct thread *td)
{
	return (0);
}

int	
vfs_stdroot(struct mount *mp, struct vnode **vpp)
{
	return (EOPNOTSUPP);
}

int	
vfs_stdstatfs(struct mount *mp, struct statfs *sbp, struct thread *td)
{
	return (EOPNOTSUPP);
}

int
vfs_stdvptofh(struct vnode *vp, struct fid *fhp)
{
	return (EOPNOTSUPP);
}

int	
vfs_stdstart(struct mount *mp, int flags, struct thread *td)
{
	return (0);
}

int	
vfs_stdquotactl(struct mount *mp, int cmds, uid_t uid,
	caddr_t arg, struct thread *td)
{
	return (EOPNOTSUPP);
}

int	
vfs_stdsync(struct mount *mp, int waitfor, struct thread *td)
{
	return (0);
}

int	
vfs_stdvget(struct mount *mp, ino_t ino, struct vnode **vpp)
{
	return (EOPNOTSUPP);
}

int	
vfs_stdfhtovp(struct mount *mp, struct fid *fhp, struct vnode **vpp)
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
vfs_stdextattrctl(struct mount *mp, int cmd, const char *attrname,
	caddr_t arg, struct thread *td)
{
	return(EOPNOTSUPP);
}

/* end of vfs default ops */
