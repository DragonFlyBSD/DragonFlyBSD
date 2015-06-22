/*
 * Copyright (c) 2004,2009 The DragonFly Project.  All rights reserved.
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
 */
/*
 * Implement vnode ops wrappers.  All vnode ops are wrapped through
 * these functions.
 *
 * These wrappers are responsible for hanlding all MPSAFE issues related
 * to a vnode operation.
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
#include <sys/vfsops.h>
#include <sys/sysmsg.h>
#include <sys/vfs_quota.h>

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
#include <sys/mplock2.h>

#define VDESCNAME(name)	__CONCAT(__CONCAT(vop_,name),_desc)

#define VNODEOP_DESC_INIT(name)						\
	struct syslink_desc VDESCNAME(name) = {				\
		__offsetof(struct vop_ops, __CONCAT(vop_, name)),	\
		#name }

VNODEOP_DESC_INIT(default);
VNODEOP_DESC_INIT(old_lookup);
VNODEOP_DESC_INIT(old_create);
VNODEOP_DESC_INIT(old_whiteout);
VNODEOP_DESC_INIT(old_mknod);
VNODEOP_DESC_INIT(open);
VNODEOP_DESC_INIT(close);
VNODEOP_DESC_INIT(access);
VNODEOP_DESC_INIT(getattr);
VNODEOP_DESC_INIT(setattr);
VNODEOP_DESC_INIT(read);
VNODEOP_DESC_INIT(write);
VNODEOP_DESC_INIT(ioctl);
VNODEOP_DESC_INIT(poll);
VNODEOP_DESC_INIT(kqfilter);
VNODEOP_DESC_INIT(mmap);
VNODEOP_DESC_INIT(fsync);
VNODEOP_DESC_INIT(old_remove);
VNODEOP_DESC_INIT(old_link);
VNODEOP_DESC_INIT(old_rename);

VNODEOP_DESC_INIT(old_mkdir);
VNODEOP_DESC_INIT(old_rmdir);
VNODEOP_DESC_INIT(old_symlink);
VNODEOP_DESC_INIT(readdir);
VNODEOP_DESC_INIT(readlink);
VNODEOP_DESC_INIT(inactive);
VNODEOP_DESC_INIT(reclaim);
VNODEOP_DESC_INIT(bmap);
VNODEOP_DESC_INIT(strategy);
VNODEOP_DESC_INIT(print);
VNODEOP_DESC_INIT(pathconf);
VNODEOP_DESC_INIT(advlock);
VNODEOP_DESC_INIT(balloc);
VNODEOP_DESC_INIT(reallocblks);
VNODEOP_DESC_INIT(getpages);
VNODEOP_DESC_INIT(putpages);
VNODEOP_DESC_INIT(freeblks);
VNODEOP_DESC_INIT(getacl);
VNODEOP_DESC_INIT(setacl);
VNODEOP_DESC_INIT(aclcheck);
VNODEOP_DESC_INIT(getextattr);
VNODEOP_DESC_INIT(setextattr);
VNODEOP_DESC_INIT(mountctl);
VNODEOP_DESC_INIT(markatime);

VNODEOP_DESC_INIT(nresolve);
VNODEOP_DESC_INIT(nlookupdotdot);
VNODEOP_DESC_INIT(ncreate);
VNODEOP_DESC_INIT(nmkdir);
VNODEOP_DESC_INIT(nmknod);
VNODEOP_DESC_INIT(nlink);
VNODEOP_DESC_INIT(nsymlink);
VNODEOP_DESC_INIT(nwhiteout);
VNODEOP_DESC_INIT(nremove);
VNODEOP_DESC_INIT(nrmdir);
VNODEOP_DESC_INIT(nrename);

#define DO_OPS(ops, error, ap, vop_field)	\
	error = ops->vop_field(ap)

/************************************************************************
 *		PRIMARY HIGH LEVEL VNODE OPERATIONS CALLS		*
 ************************************************************************
 *
 * These procedures are called directly from the kernel and/or fileops 
 * code to perform file/device operations on the system.
 *
 * NOTE: The old namespace api functions such as vop_rename() are no
 *	 longer available for general use and have been renamed to
 *	 vop_old_*().  Only the code in vfs_default.c is allowed to call
 *	 those ops.
 *
 * NOTE: The VFS_MPLOCK() macro handle mounts which do not set MNTK_MPSAFE.
 *
 * MPSAFE
 */

int
vop_old_lookup(struct vop_ops *ops, struct vnode *dvp,
	struct vnode **vpp, struct componentname *cnp)
{
	struct vop_old_lookup_args ap;
	VFS_MPLOCK_DECLARE;
	int error;

	ap.a_head.a_desc = &vop_old_lookup_desc;
	ap.a_head.a_ops = ops;
	ap.a_dvp = dvp;
	ap.a_vpp = vpp;
	ap.a_cnp = cnp;
	VFS_MPLOCK(dvp->v_mount);
	DO_OPS(ops, error, &ap, vop_old_lookup);
	VFS_MPUNLOCK(dvp->v_mount);
	return(error);
}

/*
 * MPSAFE
 */
int
vop_old_create(struct vop_ops *ops, struct vnode *dvp,
	struct vnode **vpp, struct componentname *cnp, struct vattr *vap)
{
	struct vop_old_create_args ap;
	VFS_MPLOCK_DECLARE;
	int error;

	ap.a_head.a_desc = &vop_old_create_desc;
	ap.a_head.a_ops = ops;
	ap.a_dvp = dvp;
	ap.a_vpp = vpp;
	ap.a_cnp = cnp;
	ap.a_vap = vap;

	VFS_MPLOCK(dvp->v_mount);
	DO_OPS(ops, error, &ap, vop_old_create);
	VFS_MPUNLOCK(dvp->v_mount);
	return(error);
}

/*
 * MPSAFE
 */
int
vop_old_whiteout(struct vop_ops *ops, struct vnode *dvp,
	struct componentname *cnp, int flags)
{
	struct vop_old_whiteout_args ap;
	VFS_MPLOCK_DECLARE;
	int error;

	ap.a_head.a_desc = &vop_old_whiteout_desc;
	ap.a_head.a_ops = ops;
	ap.a_dvp = dvp;
	ap.a_cnp = cnp;
	ap.a_flags = flags;

	VFS_MPLOCK(dvp->v_mount);
	DO_OPS(ops, error, &ap, vop_old_whiteout);
	VFS_MPUNLOCK(dvp->v_mount);
	return(error);
}

/*
 * MPSAFE
 */
int
vop_old_mknod(struct vop_ops *ops, struct vnode *dvp, 
	struct vnode **vpp, struct componentname *cnp, struct vattr *vap)
{
	struct vop_old_mknod_args ap;
	VFS_MPLOCK_DECLARE;
	int error;

	ap.a_head.a_desc = &vop_old_mknod_desc;
	ap.a_head.a_ops = ops;
	ap.a_dvp = dvp;
	ap.a_vpp = vpp;
	ap.a_cnp = cnp;
	ap.a_vap = vap;

	VFS_MPLOCK(dvp->v_mount);
	DO_OPS(ops, error, &ap, vop_old_mknod);
	VFS_MPUNLOCK(dvp->v_mount);
	return(error);
}

/*
 * NOTE: VAGE is always cleared when calling VOP_OPEN().
 */
int
vop_open(struct vop_ops *ops, struct vnode *vp, int mode, struct ucred *cred,
	struct file *fp)
{
	struct vop_open_args ap;
	VFS_MPLOCK_DECLARE;
	int error;

	/*
	 * Decrement 3-2-1-0.  Does not decrement beyond 0
	 */
	if (vp->v_flag & VAGE0) {
		vclrflags(vp, VAGE0);
	} else if (vp->v_flag & VAGE1) {
		vclrflags(vp, VAGE1);
		vsetflags(vp, VAGE0);
	}

	ap.a_head.a_desc = &vop_open_desc;
	ap.a_head.a_ops = ops;
	ap.a_vp = vp;
	ap.a_fp = fp;
	ap.a_mode = mode;
	ap.a_cred = cred;

	VFS_MPLOCK(vp->v_mount);
	DO_OPS(ops, error, &ap, vop_open);
	VFS_MPUNLOCK(vp->v_mount);
	return(error);
}

/*
 * MPSAFE
 */
int
vop_close(struct vop_ops *ops, struct vnode *vp, int fflag,
         struct file *fp)
{
	struct vop_close_args ap;
	VFS_MPLOCK_DECLARE;
	int error;

	ap.a_head.a_desc = &vop_close_desc;
	ap.a_head.a_ops = ops;
	ap.a_vp = vp;
	ap.a_fp = fp;
	ap.a_fflag = fflag;

	VFS_MPLOCK(vp->v_mount);
	DO_OPS(ops, error, &ap, vop_close);
	VFS_MPUNLOCK(vp->v_mount);
	return(error);
}

/*
 * MPSAFE
 */
int
vop_access(struct vop_ops *ops, struct vnode *vp, int mode, int flags,
	   struct ucred *cred)
{
	struct vop_access_args ap;
	VFS_MPLOCK_DECLARE;
	int error;

	ap.a_head.a_desc = &vop_access_desc;
	ap.a_head.a_ops = ops;
	ap.a_vp = vp;
	ap.a_mode = mode;
	ap.a_flags = flags;
	ap.a_cred = cred;

	VFS_MPLOCK(vp->v_mount);
	DO_OPS(ops, error, &ap, vop_access);
	VFS_MPUNLOCK(vp->v_mount);
	return(error);
}

/*
 * MPSAFE
 */
int
vop_getattr(struct vop_ops *ops, struct vnode *vp, struct vattr *vap)
{
	struct vop_getattr_args ap;
	VFS_MPLOCK_DECLARE;
	int error;

	ap.a_head.a_desc = &vop_getattr_desc;
	ap.a_head.a_ops = ops;
	ap.a_vp = vp;
	ap.a_vap = vap;

	VFS_MPLOCK_FLAG(vp->v_mount, MNTK_GA_MPSAFE);
	DO_OPS(ops, error, &ap, vop_getattr);
	VFS_MPUNLOCK(vp->v_mount);

	return(error);
}

/*
 * MPSAFE
 */
int
vop_setattr(struct vop_ops *ops, struct vnode *vp, struct vattr *vap,
	struct ucred *cred)
{
	struct vop_setattr_args ap;
	VFS_MPLOCK_DECLARE;
	int error;

	ap.a_head.a_desc = &vop_setattr_desc;
	ap.a_head.a_ops = ops;
	ap.a_vp = vp;
	ap.a_vap = vap;
	ap.a_cred = cred;

	VFS_MPLOCK(vp->v_mount);
	DO_OPS(ops, error, &ap, vop_setattr);
	VFS_MPUNLOCK(vp->v_mount);
	return(error);
}

/*
 * MPSAFE
 */
int
vop_read(struct vop_ops *ops, struct vnode *vp, struct uio *uio, int ioflag,
	struct ucred *cred)
{
	struct vop_read_args ap;
	VFS_MPLOCK_DECLARE;
	int error;

	ap.a_head.a_desc = &vop_read_desc;
	ap.a_head.a_ops = ops;
	ap.a_vp = vp;
	ap.a_uio = uio;
	ap.a_ioflag = ioflag;
	ap.a_cred = cred;

	VFS_MPLOCK_FLAG(vp->v_mount, MNTK_RD_MPSAFE);
	DO_OPS(ops, error, &ap, vop_read);
	VFS_MPUNLOCK(vp->v_mount);
	return(error);
}

/*
 * MPSAFE
 */
int
vop_write(struct vop_ops *ops, struct vnode *vp, struct uio *uio, int ioflag,
	struct ucred *cred)
{
	struct vop_write_args ap;
	VFS_MPLOCK_DECLARE;
	int error, do_accounting = 0;
	struct vattr va;
	uint64_t size_before=0, size_after=0;
	struct mount *mp;
	uint64_t offset, delta;

	ap.a_head.a_desc = &vop_write_desc;
	ap.a_head.a_ops = ops;
	ap.a_vp = vp;
	ap.a_uio = uio;
	ap.a_ioflag = ioflag;
	ap.a_cred = cred;

	/* is this a regular vnode ? */
	VFS_MPLOCK_FLAG(vp->v_mount, MNTK_WR_MPSAFE);
	if (vfs_quota_enabled && (vp->v_type == VREG)) {
		if ((error = VOP_GETATTR(vp, &va)) != 0)
			goto done;
		size_before = va.va_size;
		/* this file may already have been removed */
		if (va.va_nlink > 0)
			do_accounting = 1;

		offset = uio->uio_offset;
		if (ioflag & IO_APPEND)
			offset = size_before;
		size_after = offset + uio->uio_resid;
		if (size_after < size_before)
			size_after = size_before;
		delta = size_after - size_before;
		mp = vq_vptomp(vp);
		/* QUOTA CHECK */
		if (!vq_write_ok(mp, va.va_uid, va.va_gid, delta)) {
			error = EDQUOT;
			goto done;
		}
	}
	DO_OPS(ops, error, &ap, vop_write);
	if ((error == 0) && do_accounting) {
		VFS_ACCOUNT(mp, va.va_uid, va.va_gid, size_after - size_before);
	}
done:
	VFS_MPUNLOCK(vp->v_mount);
	return(error);
}

/*
 * MPSAFE
 */
int
vop_ioctl(struct vop_ops *ops, struct vnode *vp, u_long command, caddr_t data,
	int fflag, struct ucred *cred, struct sysmsg *msg)
{
	struct vop_ioctl_args ap;
	VFS_MPLOCK_DECLARE;
	int error;

	ap.a_head.a_desc = &vop_ioctl_desc;
	ap.a_head.a_ops = ops;
	ap.a_vp = vp;
	ap.a_command = command;
	ap.a_data = data;
	ap.a_fflag = fflag;
	ap.a_cred = cred;
	ap.a_sysmsg = msg;

	VFS_MPLOCK(vp->v_mount);
	DO_OPS(ops, error, &ap, vop_ioctl);
	VFS_MPUNLOCK(vp->v_mount);
	return(error);
}

/*
 * MPSAFE
 */
int
vop_poll(struct vop_ops *ops, struct vnode *vp, int events, struct ucred *cred)
{
	struct vop_poll_args ap;
	VFS_MPLOCK_DECLARE;
	int error;

	ap.a_head.a_desc = &vop_poll_desc;
	ap.a_head.a_ops = ops;
	ap.a_vp = vp;
	ap.a_events = events;
	ap.a_cred = cred;

	VFS_MPLOCK(vp->v_mount);
	DO_OPS(ops, error, &ap, vop_poll);
	VFS_MPUNLOCK(vp->v_mount);
	return(error);
}

/*
 * MPSAFE
 */
int
vop_kqfilter(struct vop_ops *ops, struct vnode *vp, struct knote *kn)
{
	struct vop_kqfilter_args ap;
	VFS_MPLOCK_DECLARE;
	int error;

	ap.a_head.a_desc = &vop_kqfilter_desc;
	ap.a_head.a_ops = ops;
	ap.a_vp = vp;
	ap.a_kn = kn;

	VFS_MPLOCK(vp->v_mount);
	DO_OPS(ops, error, &ap, vop_kqfilter);
	VFS_MPUNLOCK(vp->v_mount);
	return(error);
}

/*
 * MPSAFE
 */
int
vop_mmap(struct vop_ops *ops, struct vnode *vp, int fflags, struct ucred *cred)
{
	struct vop_mmap_args ap;
	VFS_MPLOCK_DECLARE;
	int error;

	ap.a_head.a_desc = &vop_mmap_desc;
	ap.a_head.a_ops = ops;
	ap.a_vp = vp;
	ap.a_fflags = fflags;
	ap.a_cred = cred;

	VFS_MPLOCK(vp->v_mount);
	DO_OPS(ops, error, &ap, vop_mmap);
	VFS_MPUNLOCK(vp->v_mount);
	return(error);
}

/*
 * MPSAFE
 */
int
vop_fsync(struct vop_ops *ops, struct vnode *vp, int waitfor, int flags)
{
	struct vop_fsync_args ap;
	VFS_MPLOCK_DECLARE;
	int error;

	ap.a_head.a_desc = &vop_fsync_desc;
	ap.a_head.a_ops = ops;
	ap.a_vp = vp;
	ap.a_waitfor = waitfor;
	ap.a_flags = flags;

	VFS_MPLOCK(vp->v_mount);
	DO_OPS(ops, error, &ap, vop_fsync);
	VFS_MPUNLOCK(vp->v_mount);
	return(error);
}

/*
 * MPSAFE
 */
int
vop_old_remove(struct vop_ops *ops, struct vnode *dvp, 
	struct vnode *vp, struct componentname *cnp)
{
	struct vop_old_remove_args ap;
	VFS_MPLOCK_DECLARE;
	int error;

	ap.a_head.a_desc = &vop_old_remove_desc;
	ap.a_head.a_ops = ops;
	ap.a_dvp = dvp;
	ap.a_vp = vp;
	ap.a_cnp = cnp;

	VFS_MPLOCK(dvp->v_mount);
	DO_OPS(ops, error, &ap, vop_old_remove);
	VFS_MPUNLOCK(dvp->v_mount);
	return(error);
}

/*
 * MPSAFE
 */
int
vop_old_link(struct vop_ops *ops, struct vnode *tdvp, 
	struct vnode *vp, struct componentname *cnp)
{
	struct vop_old_link_args ap;
	VFS_MPLOCK_DECLARE;
	int error;

	ap.a_head.a_desc = &vop_old_link_desc;
	ap.a_head.a_ops = ops;
	ap.a_tdvp = tdvp;
	ap.a_vp = vp;
	ap.a_cnp = cnp;

	VFS_MPLOCK(tdvp->v_mount);
	DO_OPS(ops, error, &ap, vop_old_link);
	VFS_MPUNLOCK(tdvp->v_mount);
	return(error);
}

/*
 * MPSAFE
 */
int
vop_old_rename(struct vop_ops *ops, 
	   struct vnode *fdvp, struct vnode *fvp, struct componentname *fcnp,
	   struct vnode *tdvp, struct vnode *tvp, struct componentname *tcnp)
{
	struct vop_old_rename_args ap;
	VFS_MPLOCK_DECLARE;
	int error;

	ap.a_head.a_desc = &vop_old_rename_desc;
	ap.a_head.a_ops = ops;
	ap.a_fdvp = fdvp;
	ap.a_fvp = fvp;
	ap.a_fcnp = fcnp;
	ap.a_tdvp = tdvp;
	ap.a_tvp = tvp;
	ap.a_tcnp = tcnp;

	VFS_MPLOCK(tdvp->v_mount);
	DO_OPS(ops, error, &ap, vop_old_rename);
	VFS_MPUNLOCK(tdvp->v_mount);
	return(error);
}

/*
 * MPSAFE
 */
int
vop_old_mkdir(struct vop_ops *ops, struct vnode *dvp, 
	struct vnode **vpp, struct componentname *cnp, struct vattr *vap)
{
	struct vop_old_mkdir_args ap;
	VFS_MPLOCK_DECLARE;
	int error;

	ap.a_head.a_desc = &vop_old_mkdir_desc;
	ap.a_head.a_ops = ops;
	ap.a_dvp = dvp;
	ap.a_vpp = vpp;
	ap.a_cnp = cnp;
	ap.a_vap = vap;

	VFS_MPLOCK(dvp->v_mount);
	DO_OPS(ops, error, &ap, vop_old_mkdir);
	VFS_MPUNLOCK(dvp->v_mount);
	return(error);
}

/*
 * MPSAFE
 */
int
vop_old_rmdir(struct vop_ops *ops, struct vnode *dvp, 
	struct vnode *vp, struct componentname *cnp)
{
	struct vop_old_rmdir_args ap;
	VFS_MPLOCK_DECLARE;
	int error;

	ap.a_head.a_desc = &vop_old_rmdir_desc;
	ap.a_head.a_ops = ops;
	ap.a_dvp = dvp;
	ap.a_vp = vp;
	ap.a_cnp = cnp;

	VFS_MPLOCK(dvp->v_mount);
	DO_OPS(ops, error, &ap, vop_old_rmdir);
	VFS_MPUNLOCK(dvp->v_mount);
	return(error);
}

/*
 * MPSAFE
 */
int
vop_old_symlink(struct vop_ops *ops, struct vnode *dvp,
	struct vnode **vpp, struct componentname *cnp,
	struct vattr *vap, char *target)
{
	struct vop_old_symlink_args ap;
	VFS_MPLOCK_DECLARE;
	int error;

	ap.a_head.a_desc = &vop_old_symlink_desc;
	ap.a_head.a_ops = ops;
	ap.a_dvp = dvp;
	ap.a_vpp = vpp;
	ap.a_cnp = cnp;
	ap.a_vap = vap;
	ap.a_target = target;

	VFS_MPLOCK(dvp->v_mount);
	DO_OPS(ops, error, &ap, vop_old_symlink);
	VFS_MPUNLOCK(dvp->v_mount);
	return(error);
}

/*
 * MPSAFE
 */
int
vop_readdir(struct vop_ops *ops, struct vnode *vp, struct uio *uio,
	struct ucred *cred, int *eofflag, int *ncookies, off_t **cookies)
{
	struct vop_readdir_args ap;
	VFS_MPLOCK_DECLARE;
	int error;

	ap.a_head.a_desc = &vop_readdir_desc;
	ap.a_head.a_ops = ops;
	ap.a_vp = vp;
	ap.a_uio = uio;
	ap.a_cred = cred;
	ap.a_eofflag = eofflag;
	ap.a_ncookies = ncookies;
	ap.a_cookies = cookies;

	VFS_MPLOCK(vp->v_mount);
	DO_OPS(ops, error, &ap, vop_readdir);
	VFS_MPUNLOCK(vp->v_mount);
	return(error);
}

/*
 * MPSAFE
 */
int
vop_readlink(struct vop_ops *ops, struct vnode *vp, struct uio *uio,
	struct ucred *cred)
{
	struct vop_readlink_args ap;
	VFS_MPLOCK_DECLARE;
	int error;

	ap.a_head.a_desc = &vop_readlink_desc;
	ap.a_head.a_ops = ops;
	ap.a_vp = vp;
	ap.a_uio = uio;
	ap.a_cred = cred;

	VFS_MPLOCK(vp->v_mount);
	DO_OPS(ops, error, &ap, vop_readlink);
	VFS_MPUNLOCK(vp->v_mount);
	return(error);
}

/*
 * MPSAFE
 */
int
vop_inactive(struct vop_ops *ops, struct vnode *vp)
{
	struct vop_inactive_args ap;
	struct mount *mp;
	VFS_MPLOCK_DECLARE;
	int error;

	ap.a_head.a_desc = &vop_inactive_desc;
	ap.a_head.a_ops = ops;
	ap.a_vp = vp;

	/*
	 * WARNING!  Deactivation of the vnode can cause it to be recycled,
	 *	     clearing vp->v_mount.
	 */
	mp = vp->v_mount;
	VFS_MPLOCK_FLAG(mp, MNTK_IN_MPSAFE);
	DO_OPS(ops, error, &ap, vop_inactive);
	VFS_MPUNLOCK(mp);
	return(error);
}

/*
 * MPSAFE
 */
int
vop_reclaim(struct vop_ops *ops, struct vnode *vp)
{
	struct vop_reclaim_args ap;
	struct mount *mp;
	VFS_MPLOCK_DECLARE;
	int error;

	ap.a_head.a_desc = &vop_reclaim_desc;
	ap.a_head.a_ops = ops;
	ap.a_vp = vp;

	/*
	 * WARNING!  Reclamation of the vnode will clear vp->v_mount.
	 */
	mp = vp->v_mount;
	VFS_MPLOCK(mp);
	DO_OPS(ops, error, &ap, vop_reclaim);
	VFS_MPUNLOCK(mp);
	return(error);
}

/*
 * MPSAFE
 */
int
vop_bmap(struct vop_ops *ops, struct vnode *vp, off_t loffset,
	off_t *doffsetp, int *runp, int *runb, buf_cmd_t cmd)
{
	struct vop_bmap_args ap;
	VFS_MPLOCK_DECLARE;
	int error;

	ap.a_head.a_desc = &vop_bmap_desc;
	ap.a_head.a_ops = ops;
	ap.a_vp = vp;
	ap.a_loffset = loffset;
	ap.a_doffsetp = doffsetp;
	ap.a_runp = runp;
	ap.a_runb = runb;
	ap.a_cmd = cmd;

	VFS_MPLOCK(vp->v_mount);
	DO_OPS(ops, error, &ap, vop_bmap);
	VFS_MPUNLOCK(vp->v_mount);
	return(error);
}

/*
 * MPSAFE
 */
int
vop_strategy(struct vop_ops *ops, struct vnode *vp, struct bio *bio)
{
	struct vop_strategy_args ap;
	VFS_MPLOCK_DECLARE;
	int error;

	ap.a_head.a_desc = &vop_strategy_desc;
	ap.a_head.a_ops = ops;
	ap.a_vp = vp;
	ap.a_bio = bio;

	if (vp->v_mount) {
		VFS_MPLOCK_FLAG(vp->v_mount, MNTK_SG_MPSAFE);
		DO_OPS(ops, error, &ap, vop_strategy);
		VFS_MPUNLOCK(vp->v_mount);
	} else {
		/* ugly hack for swap */
		get_mplock();
		DO_OPS(ops, error, &ap, vop_strategy);
		rel_mplock();
	}
	return(error);
}

/*
 * MPSAFE
 */
int
vop_print(struct vop_ops *ops, struct vnode *vp)
{
	struct vop_print_args ap;
	VFS_MPLOCK_DECLARE;
	int error;

	ap.a_head.a_desc = &vop_print_desc;
	ap.a_head.a_ops = ops;
	ap.a_vp = vp;

	VFS_MPLOCK(vp->v_mount);
	DO_OPS(ops, error, &ap, vop_print);
	VFS_MPUNLOCK(vp->v_mount);
	return(error);
}

/*
 * MPSAFE
 */
int
vop_pathconf(struct vop_ops *ops, struct vnode *vp, int name,
	register_t *retval)
{
	struct vop_pathconf_args ap;
	VFS_MPLOCK_DECLARE;
	int error;

	ap.a_head.a_desc = &vop_pathconf_desc;
	ap.a_head.a_ops = ops;
	ap.a_vp = vp;
	ap.a_name = name;
	ap.a_retval = retval;

	VFS_MPLOCK(vp->v_mount);
	DO_OPS(ops, error, &ap, vop_pathconf);
	VFS_MPUNLOCK(vp->v_mount);
	return(error);
}

/*
 * MPSAFE
 */
int
vop_advlock(struct vop_ops *ops, struct vnode *vp, caddr_t id, int op,
	struct flock *fl, int flags)
{
	struct vop_advlock_args ap;
	VFS_MPLOCK_DECLARE;
	int error;

	ap.a_head.a_desc = &vop_advlock_desc;
	ap.a_head.a_ops = ops;
	ap.a_vp = vp;
	ap.a_id = id;
	ap.a_op = op;
	ap.a_fl = fl;
	ap.a_flags = flags;

	VFS_MPLOCK(vp->v_mount);
	DO_OPS(ops, error, &ap, vop_advlock);
	VFS_MPUNLOCK(vp->v_mount);
	return(error);
}

/*
 * MPSAFE
 */
int
vop_balloc(struct vop_ops *ops, struct vnode *vp, off_t startoffset,
	int size, struct ucred *cred, int flags,
	struct buf **bpp)
{
	struct vop_balloc_args ap;
	VFS_MPLOCK_DECLARE;
	int error;

	ap.a_head.a_desc = &vop_balloc_desc;
	ap.a_head.a_ops = ops;
	ap.a_vp = vp;
	ap.a_startoffset = startoffset;
	ap.a_size = size;
	ap.a_cred = cred;
	ap.a_flags = flags;
	ap.a_bpp = bpp;

	VFS_MPLOCK(vp->v_mount);
	DO_OPS(ops, error, &ap, vop_balloc);
	VFS_MPUNLOCK(vp->v_mount);
	return(error);
}

/*
 * MPSAFE
 */
int
vop_reallocblks(struct vop_ops *ops, struct vnode *vp,
	struct cluster_save *buflist)
{
	struct vop_reallocblks_args ap;
	VFS_MPLOCK_DECLARE;
	int error;

	ap.a_head.a_desc = &vop_reallocblks_desc;
	ap.a_head.a_ops = ops;
	ap.a_vp = vp;
	ap.a_buflist = buflist;

	VFS_MPLOCK(vp->v_mount);
	DO_OPS(ops, error, &ap, vop_reallocblks);
	VFS_MPUNLOCK(vp->v_mount);
	return(error);
}

/*
 * MPSAFE
 */
int
vop_getpages(struct vop_ops *ops, struct vnode *vp, vm_page_t *m, int count,
	int reqpage, vm_ooffset_t offset, int seqaccess)
{
	struct vop_getpages_args ap;
	VFS_MPLOCK_DECLARE;
	int error;

	ap.a_head.a_desc = &vop_getpages_desc;
	ap.a_head.a_ops = ops;
	ap.a_vp = vp;
	ap.a_m = m;
	ap.a_count = count;
	ap.a_reqpage = reqpage;
	ap.a_offset = offset;
	ap.a_seqaccess = seqaccess;

	VFS_MPLOCK(vp->v_mount);
	DO_OPS(ops, error, &ap, vop_getpages);
	VFS_MPUNLOCK(vp->v_mount);
	return(error);
}

/*
 * MPSAFE
 */
int
vop_putpages(struct vop_ops *ops, struct vnode *vp, vm_page_t *m, int count,
	int sync, int *rtvals, vm_ooffset_t offset)
{
	struct vop_putpages_args ap;
	VFS_MPLOCK_DECLARE;
	int error;

	ap.a_head.a_desc = &vop_putpages_desc;
	ap.a_head.a_ops = ops;
	ap.a_vp = vp;
	ap.a_m = m;
	ap.a_count = count;
	ap.a_sync = sync;
	ap.a_rtvals = rtvals;
	ap.a_offset = offset;

	VFS_MPLOCK(vp->v_mount);
	DO_OPS(ops, error, &ap, vop_putpages);
	VFS_MPUNLOCK(vp->v_mount);
	return(error);
}

/*
 * MPSAFE
 */
int
vop_freeblks(struct vop_ops *ops, struct vnode *vp, off_t offset, int length)
{
	struct vop_freeblks_args ap;
	VFS_MPLOCK_DECLARE;
	int error;

	ap.a_head.a_desc = &vop_freeblks_desc;
	ap.a_head.a_ops = ops;
	ap.a_vp = vp;
	ap.a_offset = offset;
	ap.a_length = length;

	VFS_MPLOCK(vp->v_mount);
	DO_OPS(ops, error, &ap, vop_freeblks);
	VFS_MPUNLOCK(vp->v_mount);
	return(error);
}

/*
 * MPSAFE
 */
int
vop_getacl(struct vop_ops *ops, struct vnode *vp, acl_type_t type,
	struct acl *aclp, struct ucred *cred)
{
	struct vop_getacl_args ap;
	VFS_MPLOCK_DECLARE;
	int error;

	ap.a_head.a_desc = &vop_getacl_desc;
	ap.a_head.a_ops = ops;
	ap.a_vp = vp;
	ap.a_type = type;
	ap.a_aclp = aclp;
	ap.a_cred = cred;

	VFS_MPLOCK(vp->v_mount);
	DO_OPS(ops, error, &ap, vop_getacl);
	VFS_MPUNLOCK(vp->v_mount);
	return(error);
}

/*
 * MPSAFE
 */
int
vop_setacl(struct vop_ops *ops, struct vnode *vp, acl_type_t type,
	struct acl *aclp, struct ucred *cred)
{
	struct vop_setacl_args ap;
	VFS_MPLOCK_DECLARE;
	int error;

	ap.a_head.a_desc = &vop_setacl_desc;
	ap.a_head.a_ops = ops;
	ap.a_vp = vp;
	ap.a_type = type;
	ap.a_aclp = aclp;
	ap.a_cred = cred;

	VFS_MPLOCK(vp->v_mount);
	DO_OPS(ops, error, &ap, vop_setacl);
	VFS_MPUNLOCK(vp->v_mount);
	return(error);
}

/*
 * MPSAFE
 */
int
vop_aclcheck(struct vop_ops *ops, struct vnode *vp, acl_type_t type,
	struct acl *aclp, struct ucred *cred)
{
	struct vop_aclcheck_args ap;
	VFS_MPLOCK_DECLARE;
	int error;

	ap.a_head.a_desc = &vop_aclcheck_desc;
	ap.a_head.a_ops = ops;
	ap.a_vp = vp;
	ap.a_type = type;
	ap.a_aclp = aclp;
	ap.a_cred = cred;

	VFS_MPLOCK(vp->v_mount);
	DO_OPS(ops, error, &ap, vop_aclcheck);
	VFS_MPUNLOCK(vp->v_mount);
	return(error);
}

/*
 * MPSAFE
 */
int
vop_getextattr(struct vop_ops *ops, struct vnode *vp, int attrnamespace,
	       char *attrname, struct uio *uio, struct ucred *cred)
{
	struct vop_getextattr_args ap;
	VFS_MPLOCK_DECLARE;
	int error;

	ap.a_head.a_desc = &vop_getextattr_desc;
	ap.a_head.a_ops = ops;
	ap.a_vp = vp;
	ap.a_attrnamespace = attrnamespace;
	ap.a_attrname = attrname;
	ap.a_uio = uio;
	ap.a_cred = cred;

	VFS_MPLOCK(vp->v_mount);
	DO_OPS(ops, error, &ap, vop_getextattr);
	VFS_MPUNLOCK(vp->v_mount);
	return(error);
}

/*
 * MPSAFE
 */
int
vop_setextattr(struct vop_ops *ops, struct vnode *vp, int attrnamespace,
	       char *attrname, struct uio *uio, struct ucred *cred)
{
	struct vop_setextattr_args ap;
	VFS_MPLOCK_DECLARE;
	int error;

	ap.a_head.a_desc = &vop_setextattr_desc;
	ap.a_head.a_ops = ops;
	ap.a_vp = vp;
	ap.a_attrnamespace = attrnamespace;
	ap.a_attrname = attrname;
	ap.a_uio = uio;
	ap.a_cred = cred;

	VFS_MPLOCK(vp->v_mount);
	DO_OPS(ops, error, &ap, vop_setextattr);
	VFS_MPUNLOCK(vp->v_mount);
	return(error);
}

/*
 * MPSAFE
 */
int
vop_mountctl(struct vop_ops *ops, struct vnode *vp, int op, struct file *fp,
	     const void *ctl, int ctllen, void *buf, int buflen, int *res)
{
	struct vop_mountctl_args ap;
	VFS_MPLOCK_DECLARE;
	int error;

	ap.a_head.a_desc = &vop_mountctl_desc;
	ap.a_head.a_ops = ops;
	ap.a_op = op;
	ap.a_ctl = ctl;
	ap.a_fp = fp;
	ap.a_ctllen = ctllen;
	ap.a_buf = buf;
	ap.a_buflen = buflen;
	ap.a_res = res;

	VFS_MPLOCK(vp->v_mount);
	DO_OPS(ops, error, &ap, vop_mountctl);
	VFS_MPUNLOCK(vp->v_mount);
	return(error);
}

/*
 * MPSAFE
 */
int
vop_markatime(struct vop_ops *ops, struct vnode *vp, struct ucred *cred)
{
	struct vop_markatime_args ap;
	VFS_MPLOCK_DECLARE;
	int error;

	ap.a_head.a_desc = &vop_markatime_desc;
	ap.a_head.a_ops = ops;
	ap.a_vp = vp;
	ap.a_cred = cred;

	VFS_MPLOCK(vp->v_mount);
	DO_OPS(ops, error, &ap, vop_markatime);
	VFS_MPUNLOCK(vp->v_mount);
	return(error);
}

/*
 * NEW API FUNCTIONS
 *
 * nresolve takes a locked ncp, a referenced but unlocked dvp, and a cred,
 * and resolves the ncp into a positive or negative hit.
 *
 * The namecache is automatically adjusted by this function.  The ncp
 * is left locked on return.
 *
 * MPSAFE
 */
int
vop_nresolve(struct vop_ops *ops, struct nchandle *nch,
	     struct vnode *dvp, struct ucred *cred)
{
	struct vop_nresolve_args ap;
	VFS_MPLOCK_DECLARE;
	int error;

	ap.a_head.a_desc = &vop_nresolve_desc;
	ap.a_head.a_ops = ops;
	ap.a_nch = nch;
	ap.a_dvp = dvp;
	ap.a_cred = cred;

	VFS_MPLOCK(dvp->v_mount);
	DO_OPS(ops, error, &ap, vop_nresolve);
	VFS_MPUNLOCK(dvp->v_mount);
	return(error);
}

/*
 * nlookupdotdot takes an unlocked directory, referenced dvp, and looks
 * up "..", returning a locked parent directory in *vpp.  If an error
 * occurs *vpp will be NULL.
 *
 * MPSAFE
 */
int
vop_nlookupdotdot(struct vop_ops *ops, struct vnode *dvp,
	struct vnode **vpp, struct ucred *cred, char **fakename)
{
	struct vop_nlookupdotdot_args ap;
	VFS_MPLOCK_DECLARE;
	int error;

	ap.a_head.a_desc = &vop_nlookupdotdot_desc;
	ap.a_head.a_ops = ops;
	ap.a_dvp = dvp;
	ap.a_vpp = vpp;
	ap.a_cred = cred;
	ap.a_fakename = fakename;

	VFS_MPLOCK(dvp->v_mount);
	DO_OPS(ops, error, &ap, vop_nlookupdotdot);
	VFS_MPUNLOCK(dvp->v_mount);
	return(error);
}

/*
 * ncreate takes a locked, resolved ncp that typically represents a negative
 * cache hit and creates the file or node specified by the ncp, cred, and
 * vattr.  If no error occurs a locked vnode is returned in *vpp.
 *
 * The dvp passed in is referenced but unlocked.
 *
 * The namecache is automatically adjusted by this function.  The ncp
 * is left locked on return.
 *
 * MPSAFE
 */
int
vop_ncreate(struct vop_ops *ops, struct nchandle *nch, struct vnode *dvp,
	struct vnode **vpp, struct ucred *cred, struct vattr *vap)
{
	struct vop_ncreate_args ap;
	VFS_MPLOCK_DECLARE;
	int error;

	ap.a_head.a_desc = &vop_ncreate_desc;
	ap.a_head.a_ops = ops;
	ap.a_nch = nch;
	ap.a_dvp = dvp;
	ap.a_vpp = vpp;
	ap.a_cred = cred;
	ap.a_vap = vap;

	VFS_MPLOCK(dvp->v_mount);
	DO_OPS(ops, error, &ap, vop_ncreate);
	VFS_MPUNLOCK(dvp->v_mount);
	return(error);
}

/*
 * nmkdir takes a locked, resolved ncp that typically represents a negative
 * cache hit and creates the directory specified by the ncp, cred, and
 * vattr.  If no error occurs a locked vnode is returned in *vpp.
 *
 * The dvp passed in is referenced but unlocked.
 *
 * The namecache is automatically adjusted by this function.  The ncp
 * is left locked on return.
 *
 * MPSAFE
 */
int
vop_nmkdir(struct vop_ops *ops, struct nchandle *nch, struct vnode *dvp,
	struct vnode **vpp, struct ucred *cred, struct vattr *vap)
{
	struct vop_nmkdir_args ap;
	VFS_MPLOCK_DECLARE;
	int error;

	ap.a_head.a_desc = &vop_nmkdir_desc;
	ap.a_head.a_ops = ops;
	ap.a_nch = nch;
	ap.a_dvp = dvp;
	ap.a_vpp = vpp;
	ap.a_cred = cred;
	ap.a_vap = vap;

	VFS_MPLOCK(dvp->v_mount);
	DO_OPS(ops, error, &ap, vop_nmkdir);
	VFS_MPUNLOCK(dvp->v_mount);
	return(error);
}

/*
 * nmknod takes a locked, resolved ncp that typically represents a negative
 * cache hit and creates the node specified by the ncp, cred, and
 * vattr.  If no error occurs a locked vnode is returned in *vpp.
 *
 * The dvp passed in is referenced but unlocked.
 *
 * The namecache is automatically adjusted by this function.  The ncp
 * is left locked on return.
 *
 * MPSAFE
 */
int
vop_nmknod(struct vop_ops *ops, struct nchandle *nch, struct vnode *dvp,
	struct vnode **vpp, struct ucred *cred, struct vattr *vap)
{
	struct vop_nmknod_args ap;
	VFS_MPLOCK_DECLARE;
	int error;

	ap.a_head.a_desc = &vop_nmknod_desc;
	ap.a_head.a_ops = ops;
	ap.a_nch = nch;
	ap.a_dvp = dvp;
	ap.a_vpp = vpp;
	ap.a_cred = cred;
	ap.a_vap = vap;

	VFS_MPLOCK(dvp->v_mount);
	DO_OPS(ops, error, &ap, vop_nmknod);
	VFS_MPUNLOCK(dvp->v_mount);
	return(error);
}

/*
 * nlink takes a locked, resolved ncp that typically represents a negative
 * cache hit and creates the node specified by the ncp, cred, and
 * existing vnode.  The passed vp must be locked and will remain locked
 * on return, as does the ncp, whether an error occurs or not.
 *
 * The dvp passed in is referenced but unlocked.
 *
 * The namecache is automatically adjusted by this function.  The ncp
 * is left locked on return.
 *
 * MPSAFE
 */
int
vop_nlink(struct vop_ops *ops, struct nchandle *nch, struct vnode *dvp,
	  struct vnode *vp, struct ucred *cred)
{
	struct vop_nlink_args ap;
	VFS_MPLOCK_DECLARE;
	int error;

	ap.a_head.a_desc = &vop_nlink_desc;
	ap.a_head.a_ops = ops;
	ap.a_nch = nch;
	ap.a_dvp = dvp;
	ap.a_vp = vp;
	ap.a_cred = cred;

	VFS_MPLOCK(dvp->v_mount);
	DO_OPS(ops, error, &ap, vop_nlink);
	VFS_MPUNLOCK(dvp->v_mount);
	return(error);
}

/*
 * nsymlink takes a locked, resolved ncp that typically represents a negative
 * cache hit and creates a symbolic link based on cred, vap, and target (the
 * contents of the link).  If no error occurs a locked vnode is returned in
 * *vpp.
 *
 * The dvp passed in is referenced but unlocked.
 *
 * The namecache is automatically adjusted by this function.  The ncp
 * is left locked on return.
 *
 * MPSAFE
 */
int
vop_nsymlink(struct vop_ops *ops, struct nchandle *nch, struct vnode *dvp,
	struct vnode **vpp, struct ucred *cred,
	struct vattr *vap, char *target)
{
	struct vop_nsymlink_args ap;
	VFS_MPLOCK_DECLARE;
	int error;

	ap.a_head.a_desc = &vop_nsymlink_desc;
	ap.a_head.a_ops = ops;
	ap.a_nch = nch;
	ap.a_dvp = dvp;
	ap.a_vpp = vpp;
	ap.a_cred = cred;
	ap.a_vap = vap;
	ap.a_target = target;

	VFS_MPLOCK(dvp->v_mount);
	DO_OPS(ops, error, &ap, vop_nsymlink);
	VFS_MPUNLOCK(dvp->v_mount);
	return(error);
}

/*
 * nwhiteout takes a locked, resolved ncp that can represent a positive or
 * negative hit and executes the whiteout function specified in flags.
 *
 * The dvp passed in is referenced but unlocked.
 *
 * The namecache is automatically adjusted by this function.  The ncp
 * is left locked on return.
 *
 * MPSAFE
 */
int
vop_nwhiteout(struct vop_ops *ops, struct nchandle *nch, struct vnode *dvp,
	struct ucred *cred, int flags)
{
	struct vop_nwhiteout_args ap;
	VFS_MPLOCK_DECLARE;
	int error;

	ap.a_head.a_desc = &vop_nwhiteout_desc;
	ap.a_head.a_ops = ops;
	ap.a_nch = nch;
	ap.a_dvp = dvp;
	ap.a_cred = cred;
	ap.a_flags = flags;

	VFS_MPLOCK(dvp->v_mount);
	DO_OPS(ops, error, &ap, vop_nwhiteout);
	VFS_MPUNLOCK(dvp->v_mount);
	return(error);
}

/*
 * nremove takes a locked, resolved ncp that generally represents a
 * positive hit and removes the file.
 *
 * The dvp passed in is referenced but unlocked.
 *
 * The namecache is automatically adjusted by this function.  The ncp
 * is left locked on return.
 *
 * MPSAFE
 */
int
vop_nremove(struct vop_ops *ops, struct nchandle *nch, struct vnode *dvp,
	    struct ucred *cred)
{
	struct vop_nremove_args ap;
	VFS_MPLOCK_DECLARE;
	int error;
	struct vattr va;

	ap.a_head.a_desc = &vop_nremove_desc;
	ap.a_head.a_ops = ops;
	ap.a_nch = nch;
	ap.a_dvp = dvp;
	ap.a_cred = cred;

	if ((error = VOP_GETATTR(nch->ncp->nc_vp, &va)) != 0)
		return (error);

	VFS_MPLOCK(dvp->v_mount);
	DO_OPS(ops, error, &ap, vop_nremove);
	/* Only update space counters if this is the last hard link */
	if ((error == 0) && (va.va_nlink == 1)) {
		VFS_ACCOUNT(nch->mount, va.va_uid, va.va_gid, -va.va_size);
	}
	VFS_MPUNLOCK(dvp->v_mount);
	return(error);
}

/*
 * nrmdir takes a locked, resolved ncp that generally represents a
 * directory and removes the directory.
 *
 * The dvp passed in is referenced but unlocked.
 *
 * The namecache is automatically adjusted by this function.  The ncp
 * is left locked on return.
 *
 * MPSAFE
 */
int
vop_nrmdir(struct vop_ops *ops, struct nchandle *nch, struct vnode *dvp,
	   struct ucred *cred)
{
	struct vop_nrmdir_args ap;
	VFS_MPLOCK_DECLARE;
	int error;

	ap.a_head.a_desc = &vop_nrmdir_desc;
	ap.a_head.a_ops = ops;
	ap.a_nch = nch;
	ap.a_dvp = dvp;
	ap.a_cred = cred;

	VFS_MPLOCK(dvp->v_mount);
	DO_OPS(ops, error, &ap, vop_nrmdir);
	VFS_MPUNLOCK(dvp->v_mount);
	return(error);
}

/*
 * nrename takes TWO locked, resolved ncp's and the cred of the caller
 * and renames the source ncp to the target ncp.  The target ncp may 
 * represent a positive or negative hit.
 *
 * The fdvp and tdvp passed in are referenced but unlocked.
 *
 * The namecache is automatically adjusted by this function.  The ncp
 * is left locked on return.  The source ncp is typically changed to
 * a negative cache hit and the target ncp typically takes on the
 * source ncp's underlying file.
 *
 * MPSAFE
 */
int
vop_nrename(struct vop_ops *ops,
	    struct nchandle *fnch, struct nchandle *tnch,
	    struct vnode *fdvp, struct vnode *tdvp,
	    struct ucred *cred)
{
	struct vop_nrename_args ap;
	VFS_MPLOCK_DECLARE;
	int error;

	ap.a_head.a_desc = &vop_nrename_desc;
	ap.a_head.a_ops = ops;
	ap.a_fnch = fnch;
	ap.a_tnch = tnch;
	ap.a_fdvp = fdvp;
	ap.a_tdvp = tdvp;
	ap.a_cred = cred;

	VFS_MPLOCK(fdvp->v_mount);
	DO_OPS(ops, error, &ap, vop_nrename);
	VFS_MPUNLOCK(fdvp->v_mount);
	return(error);
}

/************************************************************************
 *		PRIMARY VNODE OPERATIONS FORWARDING CALLS		*
 ************************************************************************
 *
 * These procedures are called from VFSs such as unionfs and nullfs
 * when they wish to forward an operation on one VFS to another.  The
 * argument structure/message is modified and then directly passed to the
 * appropriate routine.  This routines may also be called by initiators
 * who have an argument structure in hand rather then discreet arguments.
 *
 * MPSAFE - Caller expected to already hold the appropriate vfs lock.
 */
int
vop_vnoperate_ap(struct vop_generic_args *ap)
{
	struct vop_ops *ops;
	int error;

	ops = ap->a_ops;
	error = VOCALL(ops, ap);

	return (error);
}

/*
 * This routine is called by the cache coherency layer to execute the actual
 * VFS operation.  If a journaling layer is present we call though it, else
 * we call the native VOP functions.
 */
int
vop_cache_operate_ap(struct vop_generic_args *ap)
{
	struct vop_ops *ops;
	int error;

	ops = ap->a_ops;
	if (ops->head.vv_mount->mnt_vn_journal_ops)
		error = VOCALL(ops->head.vv_mount->mnt_vn_journal_ops, ap);
	else
		error = VOCALL(ops->head.vv_mount->mnt_vn_norm_ops, ap);
	return (error);
}


/*
 * This routine is called by the journaling layer to execute the actual
 * VFS operation.
 */
int
vop_journal_operate_ap(struct vop_generic_args *ap)
{
	struct vop_ops *ops;
	int error;

	ops = ap->a_ops;
	error = VOCALL(ops->head.vv_mount->mnt_vn_norm_ops, ap);

	return (error);
}

int
vop_open_ap(struct vop_open_args *ap)
{
	int error;

	DO_OPS(ap->a_head.a_ops, error, ap, vop_open);
	return(error);
}

int
vop_close_ap(struct vop_close_args *ap)
{
	int error;

	DO_OPS(ap->a_head.a_ops, error, ap, vop_close);
	return(error);
}

int
vop_access_ap(struct vop_access_args *ap)
{
	int error;

	DO_OPS(ap->a_head.a_ops, error, ap, vop_access);
	return(error);
}

int
vop_getattr_ap(struct vop_getattr_args *ap)
{
	int error;

	DO_OPS(ap->a_head.a_ops, error, ap, vop_getattr);
	return(error);
}

int
vop_setattr_ap(struct vop_setattr_args *ap)
{
	int error;

	DO_OPS(ap->a_head.a_ops, error, ap, vop_setattr);
	return(error);
}

int
vop_read_ap(struct vop_read_args *ap)
{
	int error;

	DO_OPS(ap->a_head.a_ops, error, ap, vop_read);
	return(error);
}

int
vop_write_ap(struct vop_write_args *ap)
{
	int error;

	DO_OPS(ap->a_head.a_ops, error, ap, vop_write);
	return(error);
}

int
vop_ioctl_ap(struct vop_ioctl_args *ap)
{
	int error;

	DO_OPS(ap->a_head.a_ops, error, ap, vop_ioctl);
	return(error);
}

int
vop_poll_ap(struct vop_poll_args *ap)
{
	int error;

	DO_OPS(ap->a_head.a_ops, error, ap, vop_poll);
	return(error);
}

int
vop_kqfilter_ap(struct vop_kqfilter_args *ap)
{
	int error;

	DO_OPS(ap->a_head.a_ops, error, ap, vop_kqfilter);
	return(error);
}

int
vop_mmap_ap(struct vop_mmap_args *ap)
{
	int error;

	DO_OPS(ap->a_head.a_ops, error, ap, vop_mmap);
	return(error);
}

int
vop_fsync_ap(struct vop_fsync_args *ap)
{
	int error;

	DO_OPS(ap->a_head.a_ops, error, ap, vop_fsync);
	return(error);
}

int
vop_readdir_ap(struct vop_readdir_args *ap)
{
	int error;

	DO_OPS(ap->a_head.a_ops, error, ap, vop_readdir);
	return(error);
}

int
vop_readlink_ap(struct vop_readlink_args *ap)
{
	int error;

	DO_OPS(ap->a_head.a_ops, error, ap, vop_readlink);
	return(error);
}

int
vop_inactive_ap(struct vop_inactive_args *ap)
{
	int error;

	DO_OPS(ap->a_head.a_ops, error, ap, vop_inactive);
	return(error);
}

int
vop_reclaim_ap(struct vop_reclaim_args *ap)
{
	int error;

	DO_OPS(ap->a_head.a_ops, error, ap, vop_reclaim);
	return(error);
}

int
vop_bmap_ap(struct vop_bmap_args *ap)
{
	int error;

	DO_OPS(ap->a_head.a_ops, error, ap, vop_bmap);
	return(error);
}

int
vop_strategy_ap(struct vop_strategy_args *ap)
{
	int error;

	DO_OPS(ap->a_head.a_ops, error, ap, vop_strategy);
	return(error);
}

int
vop_print_ap(struct vop_print_args *ap)
{
	int error;

	DO_OPS(ap->a_head.a_ops, error, ap, vop_print);
	return(error);
}

int
vop_pathconf_ap(struct vop_pathconf_args *ap)
{
	int error;

	DO_OPS(ap->a_head.a_ops, error, ap, vop_pathconf);
	return(error);
}

int
vop_advlock_ap(struct vop_advlock_args *ap)
{
	int error;

	DO_OPS(ap->a_head.a_ops, error, ap, vop_advlock);
	return(error);
}

int
vop_balloc_ap(struct vop_balloc_args *ap)
{
	int error;

	DO_OPS(ap->a_head.a_ops, error, ap, vop_balloc);
	return(error);
}

int
vop_reallocblks_ap(struct vop_reallocblks_args *ap)
{
	int error;

	DO_OPS(ap->a_head.a_ops, error, ap, vop_reallocblks);
	return(error);
}

int
vop_getpages_ap(struct vop_getpages_args *ap)
{
	int error;

	DO_OPS(ap->a_head.a_ops, error, ap, vop_getpages);
	return(error);
}

int
vop_putpages_ap(struct vop_putpages_args *ap)
{
	int error;

	DO_OPS(ap->a_head.a_ops, error, ap, vop_putpages);
	return(error);
}

int
vop_freeblks_ap(struct vop_freeblks_args *ap)
{
	int error;

	DO_OPS(ap->a_head.a_ops, error, ap, vop_freeblks);
	return(error);
}

int
vop_getacl_ap(struct vop_getacl_args *ap)
{
	int error;

	DO_OPS(ap->a_head.a_ops, error, ap, vop_getacl);
	return(error);
}

int
vop_setacl_ap(struct vop_setacl_args *ap)
{
	int error;

	DO_OPS(ap->a_head.a_ops, error, ap, vop_setacl);
	return(error);
}

int
vop_aclcheck_ap(struct vop_aclcheck_args *ap)
{
	int error;

	DO_OPS(ap->a_head.a_ops, error, ap, vop_aclcheck);
	return(error);
}

int
vop_getextattr_ap(struct vop_getextattr_args *ap)
{
	int error;

	DO_OPS(ap->a_head.a_ops, error, ap, vop_getextattr);
	return(error);
}

int
vop_setextattr_ap(struct vop_setextattr_args *ap)
{
	int error;

	DO_OPS(ap->a_head.a_ops, error, ap, vop_setextattr);
	return(error);
}

int
vop_mountctl_ap(struct vop_mountctl_args *ap)
{
	int error;

	DO_OPS(ap->a_head.a_ops, error, ap, vop_mountctl);
	return(error);
}

int
vop_markatime_ap(struct vop_markatime_args *ap)
{
	int error;

	DO_OPS(ap->a_head.a_ops, error, ap, vop_markatime);
	return(error);
}

int
vop_nresolve_ap(struct vop_nresolve_args *ap)
{
	int error;

	DO_OPS(ap->a_head.a_ops, error, ap, vop_nresolve);
	return(error);
}

int
vop_nlookupdotdot_ap(struct vop_nlookupdotdot_args *ap)
{
	int error;

	DO_OPS(ap->a_head.a_ops, error, ap, vop_nlookupdotdot);
	return(error);
}

int
vop_ncreate_ap(struct vop_ncreate_args *ap)
{
	int error;

	DO_OPS(ap->a_head.a_ops, error, ap, vop_ncreate);
	return(error);
}

int
vop_nmkdir_ap(struct vop_nmkdir_args *ap)
{
	int error;

	DO_OPS(ap->a_head.a_ops, error, ap, vop_nmkdir);
	return(error);
}

int
vop_nmknod_ap(struct vop_nmknod_args *ap)
{
	int error;

	DO_OPS(ap->a_head.a_ops, error, ap, vop_nmknod);
	return(error);
}

int
vop_nlink_ap(struct vop_nlink_args *ap)
{
	int error;

	DO_OPS(ap->a_head.a_ops, error, ap, vop_nlink);
	return(error);
}

int
vop_nsymlink_ap(struct vop_nsymlink_args *ap)
{
	int error;

	DO_OPS(ap->a_head.a_ops, error, ap, vop_nsymlink);
	return(error);
}

int
vop_nwhiteout_ap(struct vop_nwhiteout_args *ap)
{
	int error;

	DO_OPS(ap->a_head.a_ops, error, ap, vop_nwhiteout);
	return(error);
}

int
vop_nremove_ap(struct vop_nremove_args *ap)
{
	int error;

	DO_OPS(ap->a_head.a_ops, error, ap, vop_nremove);
	return(error);
}

int
vop_nrmdir_ap(struct vop_nrmdir_args *ap)
{
	int error;

	DO_OPS(ap->a_head.a_ops, error, ap, vop_nrmdir);
	return(error);
}

int
vop_nrename_ap(struct vop_nrename_args *ap)
{
	int error;

	DO_OPS(ap->a_head.a_ops, error, ap, vop_nrename);
	return(error);
}

