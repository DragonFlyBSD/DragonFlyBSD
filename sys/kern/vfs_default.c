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
 * $DragonFly: src/sys/kern/vfs_default.c,v 1.14 2004/09/28 00:25:29 dillon Exp $
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
#include <sys/poll.h>

#include <machine/limits.h>

#include <vm/vm.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <vm/vm_pager.h>
#include <vm/vnode_pager.h>

static int	vop_nolookup (struct vop_lookup_args *);
static int	vop_noresolve (struct vop_resolve_args *);
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
	{ &vop_resolve_desc,		(void *) vop_noresolve },
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
	{ NULL, NULL }
};

static struct vnodeopv_desc default_vnodeop_opv_desc =
        { &default_vnode_vops, default_vnodeop_entries };

VNODEOP_SET(default_vnodeop_opv_desc);

int
vop_eopnotsupp(struct vop_generic_args *ap)
{
	/*
	printf("vop_notsupp[%s]\n", ap->a_desc->vdesc_name);
	*/
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
 * vop_noresolve { struct namecache *a_ncp }	XXX STOPGAP FUNCTION
 *
 * Resolve a ncp for VFSs which do not support the VOP.  Eventually all
 * VFSs will support this VOP and this routine can be removed, since
 * vop_resolve() is far less complex then the older LOOKUP/CACHEDLOOKUP
 * API.
 *
 * A locked ncp is passed in to be resolved.  An NCP is resolved by
 * calling cache_setvp() on it.  No vnode locks are retained and the
 * ncp is left locked on return.
 */
static int
vop_noresolve(struct vop_resolve_args *ap)
{
	int error;
	struct vnode *dvp;
	struct vnode *vp;
	struct namecache *ncp;
	struct componentname cnp;

	ncp = ap->a_ncp;	/* locked namecache node */
	if (ncp->nc_parent == NULL)
		return(EPERM);
	if ((dvp = ncp->nc_parent->nc_vp) == NULL)
		return(EPERM);
	vget(dvp, NULL, LK_EXCLUSIVE, curthread);

	bzero(&cnp, sizeof(cnp));
	cnp.cn_nameiop = NAMEI_LOOKUP;
	cnp.cn_flags = CNP_ISLASTCN;
	cnp.cn_nameptr = ncp->nc_name;
	cnp.cn_namelen = ncp->nc_nlen;
	/* creds */
	/* td */
	error = vop_lookup(ap->a_head.a_ops, dvp, &vp, &cnp);
	if (error == 0) {
		KKASSERT(vp != NULL);
		cache_setvp(ncp, vp);
		vrele(vp);
	} else if (error == ENOENT) {
		KKASSERT(vp == NULL);
		if (cnp.cn_flags & CNP_ISWHITEOUT)
			ncp->nc_flag |= NCF_WHITEOUT;
		cache_setvp(ncp, NULL);
	}
	if (cnp.cn_flags & CNP_PDIRUNLOCK)
		vrele(dvp);
	else
		vput(dvp);
	return(error);
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
		lwkt_tokref_t a_vlock;
		int a_flags;
		struct proc *a_p;
	} */ *ap;
{               
	int error;

#ifndef	DEBUG_LOCKS
	error = lockmgr(&ap->a_vp->v_lock, ap->a_flags,
			ap->a_vlock, ap->a_td);
#else
	error = debuglockmgr(&ap->a_vp->v_lock, ap->a_flags,
			ap->a_vlock, ap->a_td,
			"vop_stdlock", ap->a_vp->filename, ap->a_vp->line);
#endif
	return(error);
}

int
vop_stdunlock(ap)
	struct vop_unlock_args /* {
		struct vnode *a_vp;
		lwkt_tokref_t a_vlock;
		int a_flags;
		struct thread *a_td;
	} */ *ap;
{
	int error;

	error = lockmgr(&ap->a_vp->v_lock, ap->a_flags | LK_RELEASE,
			ap->a_vlock, ap->a_td);
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
			VOP_UNLOCK(vp, NULL, 0, td);
			tsleep(object, 0, "vodead", 0);
			vn_lock(vp, NULL, LK_EXCLUSIVE | LK_RETRY, td);
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
	struct nameidata *ndp, struct thread *td)
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
