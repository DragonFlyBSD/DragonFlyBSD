/*
 * 
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
 * $DragonFly: src/sys/kern/vfs_vopops.c,v 1.6 2004/09/26 06:00:05 dillon Exp $
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

#define VOFFNAME(name)	__CONCAT(__CONCAT(vop_,name),_vp_offsets)
#define VDESCNAME(name)	__CONCAT(__CONCAT(vop_,name),_desc)
#define VARGSSTRUCT(name) struct __CONCAT(__CONCAT(vop_,name),_args)

#define VNODEOP_DESC_INIT(name, flags, vpoffs, vpp, cred, proc, comp)	\
	struct vnodeop_desc VDESCNAME(name) = {				\
		__offsetof(struct vop_ops, __CONCAT(vop_, name)),	\
		#name, flags, vpoffs, vpp, cred, proc, comp }

#define VNODEOP_DESC_INIT_SIMPLE(name)					\
	VNODEOP_DESC_INIT(name, 0, NULL,				\
		VDESC_NO_OFFSET,					\
		VDESC_NO_OFFSET,					\
		VDESC_NO_OFFSET,					\
		VDESC_NO_OFFSET)

#define VNODEOP_DESC_INIT_VP(name)					\
	static int VOFFNAME(name)[] = { 				\
		__offsetof(VARGSSTRUCT(name), a_vp),			\
		VDESC_NO_OFFSET };					\
	VNODEOP_DESC_INIT(name, 0, VOFFNAME(name), 			\
		VDESC_NO_OFFSET,					\
		VDESC_NO_OFFSET,					\
		VDESC_NO_OFFSET,					\
		VDESC_NO_OFFSET)

#define VNODEOP_DESC_INIT_VP_VPP(name)					\
	static int VOFFNAME(name)[] = { 				\
		__offsetof(VARGSSTRUCT(name), a_vp), 			\
		VDESC_NO_OFFSET };					\
	VNODEOP_DESC_INIT(name, 0, VOFFNAME(name),			\
		__offsetof(VARGSSTRUCT(name), a_vpp),			\
		VDESC_NO_OFFSET,					\
		VDESC_NO_OFFSET,					\
		VDESC_NO_OFFSET)

#define VNODEOP_DESC_INIT_VP_CRED(name)					\
	static int VOFFNAME(name)[] = { 				\
		__offsetof(VARGSSTRUCT(name), a_vp), 			\
		VDESC_NO_OFFSET };					\
	VNODEOP_DESC_INIT(name, 0, VOFFNAME(name),			\
		VDESC_NO_OFFSET,					\
		__offsetof(VARGSSTRUCT(name), a_cred),			\
		VDESC_NO_OFFSET,					\
		VDESC_NO_OFFSET)

#define VNODEOP_DESC_INIT_DVP_VPP_CNP(name)				\
	static int VOFFNAME(name)[] = { 				\
		__offsetof(VARGSSTRUCT(name), a_dvp),			\
		VDESC_NO_OFFSET };					\
	VNODEOP_DESC_INIT(name, 0, VOFFNAME(name), 			\
		__offsetof(VARGSSTRUCT(name), a_vpp),			\
		VDESC_NO_OFFSET,					\
		VDESC_NO_OFFSET,					\
		__offsetof(VARGSSTRUCT(name), a_cnp))

#define VNODEOP_DESC_INIT_DVP_CNP(name)					\
	static int VOFFNAME(name)[] = { 				\
		__offsetof(VARGSSTRUCT(name), a_dvp),			\
		VDESC_NO_OFFSET };					\
	VNODEOP_DESC_INIT(name, 0, VOFFNAME(name), 			\
		VDESC_NO_OFFSET,					\
		VDESC_NO_OFFSET,					\
		VDESC_NO_OFFSET,					\
		__offsetof(VARGSSTRUCT(name), a_cnp))

#define VNODEOP_DESC_INIT_DVP_VP_CNP(name)				\
	static int VOFFNAME(name)[] = { 				\
		__offsetof(VARGSSTRUCT(name), a_dvp),			\
		__offsetof(VARGSSTRUCT(name), a_vp),			\
		VDESC_NO_OFFSET };					\
	VNODEOP_DESC_INIT(name, 0, VOFFNAME(name), 			\
		VDESC_NO_OFFSET, 					\
		VDESC_NO_OFFSET,					\
		VDESC_NO_OFFSET,					\
		__offsetof(VARGSSTRUCT(name), a_cnp))

#define VNODEOP_DESC_INIT_TDVP_VP_CNP(name)				\
	static int VOFFNAME(name)[] = { 				\
		__offsetof(VARGSSTRUCT(name), a_tdvp),			\
		__offsetof(VARGSSTRUCT(name), a_vp),			\
		VDESC_NO_OFFSET };					\
	VNODEOP_DESC_INIT(name, 0, VOFFNAME(name), 			\
		VDESC_NO_OFFSET,					\
		VDESC_NO_OFFSET,					\
		VDESC_NO_OFFSET,					\
		__offsetof(VARGSSTRUCT(name), a_cnp))

VNODEOP_DESC_INIT_SIMPLE(default);
VNODEOP_DESC_INIT_VP(islocked);
VNODEOP_DESC_INIT_DVP_VPP_CNP(lookup);
VNODEOP_DESC_INIT_DVP_VPP_CNP(cachedlookup);
VNODEOP_DESC_INIT_DVP_VPP_CNP(create);
VNODEOP_DESC_INIT_DVP_CNP(whiteout);
VNODEOP_DESC_INIT_DVP_VPP_CNP(mknod);
VNODEOP_DESC_INIT_VP_CRED(open);
VNODEOP_DESC_INIT_VP(close);
VNODEOP_DESC_INIT_VP_CRED(access);
VNODEOP_DESC_INIT_VP(getattr);
VNODEOP_DESC_INIT_VP_CRED(setattr);
VNODEOP_DESC_INIT_VP_CRED(read);
VNODEOP_DESC_INIT_VP_CRED(write);
VNODEOP_DESC_INIT_VP_CRED(lease);
VNODEOP_DESC_INIT_VP_CRED(ioctl);
VNODEOP_DESC_INIT_VP_CRED(poll);
VNODEOP_DESC_INIT_VP(kqfilter);
VNODEOP_DESC_INIT_VP(revoke);
VNODEOP_DESC_INIT_VP_CRED(mmap);
VNODEOP_DESC_INIT_VP(fsync);
VNODEOP_DESC_INIT_DVP_VP_CNP(remove);
VNODEOP_DESC_INIT_TDVP_VP_CNP(link);

static int VOFFNAME(rename)[] = { 
	__offsetof(VARGSSTRUCT(rename), a_fdvp),
	__offsetof(VARGSSTRUCT(rename), a_fvp),
	__offsetof(VARGSSTRUCT(rename), a_tdvp),
	__offsetof(VARGSSTRUCT(rename), a_tvp),
	VDESC_NO_OFFSET
};
VNODEOP_DESC_INIT(rename, 
	VDESC_VP0_WILLRELE|VDESC_VP1_WILLRELE|
	 VDESC_VP2_WILLRELE|VDESC_VP3_WILLRELE|
	 VDESC_VP2_WILLUNLOCK|VDESC_VP3_WILLUNLOCK, /* tdvp and tvp */
	VOFFNAME(rename),
	VDESC_NO_OFFSET,
	VDESC_NO_OFFSET,
	VDESC_NO_OFFSET,
	__offsetof(VARGSSTRUCT(rename), a_fcnp));
VNODEOP_DESC_INIT_DVP_VPP_CNP(mkdir);
VNODEOP_DESC_INIT_DVP_VP_CNP(rmdir);
VNODEOP_DESC_INIT_DVP_VPP_CNP(symlink);
VNODEOP_DESC_INIT_VP_CRED(readdir);
VNODEOP_DESC_INIT_VP_CRED(readlink);
VNODEOP_DESC_INIT_VP(inactive);
VNODEOP_DESC_INIT_VP(reclaim);
VNODEOP_DESC_INIT_VP(lock);
VNODEOP_DESC_INIT_VP(unlock);
VNODEOP_DESC_INIT_VP_VPP(bmap);
VNODEOP_DESC_INIT_VP(strategy);
VNODEOP_DESC_INIT_VP(print);
VNODEOP_DESC_INIT_VP(pathconf);
VNODEOP_DESC_INIT_VP(advlock);
VNODEOP_DESC_INIT_VP_CRED(balloc);
VNODEOP_DESC_INIT_VP(reallocblks);
VNODEOP_DESC_INIT_VP(getpages);
VNODEOP_DESC_INIT_VP(putpages);
VNODEOP_DESC_INIT_VP(freeblks);
VNODEOP_DESC_INIT_VP(bwrite);
VNODEOP_DESC_INIT_VP_CRED(getacl);
VNODEOP_DESC_INIT_VP_CRED(setacl);
VNODEOP_DESC_INIT_VP_CRED(aclcheck);
VNODEOP_DESC_INIT_VP_CRED(getextattr);
VNODEOP_DESC_INIT_VP_CRED(setextattr);
VNODEOP_DESC_INIT_VP(createvobject);
VNODEOP_DESC_INIT_VP(destroyvobject);
VNODEOP_DESC_INIT_VP(getvobject);
VNODEOP_DESC_INIT_SIMPLE(vfsset);

/*
 * The DO_OPS macro basicallys calls ops->blah(&ap) but is also responsible
 * for checking vv_flags and executing additional hook functions.
 *
 * NOTE: vop_vnoperate_ap() rolls its own DO_OPS equivalent.
 */
#define DO_OPS(ops, error, ap, vop_field)				\
	if (ops->vv_flags & VVF_HOOK_MASK) {				\
		if (ops->vv_flags & VVF_JOURNAL_HOOK) 			\
			error = ops->vv_jops->vop_field(ap);		\
		else							\
			error = 0;					\
		if (error == 0)						\
			error = ops->vop_field(ap);			\
	} else {							\
		error = ops->vop_field(ap);				\
	}								\


/************************************************************************
 *		PRIMARY HIGH LEVEL VNODE OPERATIONS CALLS		*
 ************************************************************************
 *
 * These procedures are called directly from the kernel and/or fileops 
 * code to perform file/device operations on the system.
 */

int
vop_islocked(struct vop_ops *ops, struct vnode *vp, struct thread *td)
{
	struct vop_islocked_args ap;
	int error;

	ap.a_head.a_desc = &vop_islocked_desc;
	ap.a_head.a_ops = ops;
	ap.a_vp = vp;
	ap.a_td = td;

	DO_OPS(ops, error, &ap, vop_islocked);
	return(error);
}

int
vop_lookup(struct vop_ops *ops, struct vnode *dvp,
	struct vnode **vpp, struct componentname *cnp)
{
	struct vop_lookup_args ap;
	int error;

	ap.a_head.a_desc = &vop_lookup_desc;
	ap.a_head.a_ops = ops;
	ap.a_dvp = dvp;
	ap.a_vpp = vpp;
	ap.a_cnp = cnp;

	DO_OPS(ops, error, &ap, vop_lookup);
	return(error);
}

int
vop_cachedlookup(struct vop_ops *ops, struct vnode *dvp,
	struct vnode **vpp, struct componentname *cnp)
{
	struct vop_cachedlookup_args ap;
	int error;

	ap.a_head.a_desc = &vop_cachedlookup_desc;
	ap.a_head.a_ops = ops;
	ap.a_dvp = dvp;
	ap.a_vpp = vpp;
	ap.a_cnp = cnp;

	DO_OPS(ops, error, &ap, vop_cachedlookup);
	return(error);
}

int
vop_create(struct vop_ops *ops, struct vnode *dvp, struct namecache *par,
	struct vnode **vpp, struct componentname *cnp, struct vattr *vap)
{
	struct vop_create_args ap;
	int error;

	ap.a_head.a_desc = &vop_create_desc;
	ap.a_head.a_ops = ops;
	ap.a_dvp = dvp;
	ap.a_par = par;
	ap.a_vpp = vpp;
	ap.a_cnp = cnp;
	ap.a_vap = vap;

	DO_OPS(ops, error, &ap, vop_create);
	return(error);
}

int
vop_whiteout(struct vop_ops *ops, struct vnode *dvp, struct namecache *par,
	struct componentname *cnp, int flags)
{
	struct vop_whiteout_args ap;
	int error;

	ap.a_head.a_desc = &vop_whiteout_desc;
	ap.a_head.a_ops = ops;
	ap.a_dvp = dvp;
	ap.a_par = par;
	ap.a_cnp = cnp;
	ap.a_flags = flags;

	DO_OPS(ops, error, &ap, vop_whiteout);
	return(error);
}

int
vop_mknod(struct vop_ops *ops, struct vnode *dvp, struct namecache *par,
	struct vnode **vpp, struct componentname *cnp, struct vattr *vap)
{
	struct vop_mknod_args ap;
	int error;

	ap.a_head.a_desc = &vop_mknod_desc;
	ap.a_head.a_ops = ops;
	ap.a_dvp = dvp;
	ap.a_par = par;
	ap.a_vpp = vpp;
	ap.a_cnp = cnp;
	ap.a_vap = vap;

	DO_OPS(ops, error, &ap, vop_mknod);
	return(error);
}

int
vop_open(struct vop_ops *ops, struct vnode *vp, int mode, struct ucred *cred,
	struct thread *td)
{
	struct vop_open_args ap;
	int error;

	ap.a_head.a_desc = &vop_open_desc;
	ap.a_head.a_ops = ops;
	ap.a_vp = vp;
	ap.a_mode = mode;
	ap.a_cred = cred;
	ap.a_td = td;

	DO_OPS(ops, error, &ap, vop_open);
	return(error);
}

int
vop_close(struct vop_ops *ops, struct vnode *vp, int fflag, struct thread *td)
{
	struct vop_close_args ap;
	int error;

	ap.a_head.a_desc = &vop_close_desc;
	ap.a_head.a_ops = ops;
	ap.a_vp = vp;
	ap.a_fflag = fflag;
	ap.a_td = td;

	DO_OPS(ops, error, &ap, vop_close);
	return(error);
}

int
vop_access(struct vop_ops *ops, struct vnode *vp, int mode, struct ucred *cred,
	struct thread *td)
{
	struct vop_access_args ap;
	int error;

	ap.a_head.a_desc = &vop_access_desc;
	ap.a_head.a_ops = ops;
	ap.a_vp = vp;
	ap.a_mode = mode;
	ap.a_cred = cred;
	ap.a_td = td;

	DO_OPS(ops, error, &ap, vop_access);
	return(error);
}

int
vop_getattr(struct vop_ops *ops, struct vnode *vp, struct vattr *vap,
	struct thread *td)
{
	struct vop_getattr_args ap;
	int error;

	ap.a_head.a_desc = &vop_getattr_desc;
	ap.a_head.a_ops = ops;
	ap.a_vp = vp;
	ap.a_vap = vap;
	ap.a_td = td;

	DO_OPS(ops, error, &ap, vop_getattr);
	return(error);
}

int
vop_setattr(struct vop_ops *ops, struct vnode *vp, struct vattr *vap,
	struct ucred *cred, struct thread *td)
{
	struct vop_setattr_args ap;
	int error;

	ap.a_head.a_desc = &vop_setattr_desc;
	ap.a_head.a_ops = ops;
	ap.a_vp = vp;
	ap.a_vap = vap;
	ap.a_cred = cred;
	ap.a_td = td;

	DO_OPS(ops, error, &ap, vop_setattr);
	return(error);
}

int
vop_read(struct vop_ops *ops, struct vnode *vp, struct uio *uio, int ioflag,
	struct ucred *cred)
{
	struct vop_read_args ap;
	int error;

	ap.a_head.a_desc = &vop_read_desc;
	ap.a_head.a_ops = ops;
	ap.a_vp = vp;
	ap.a_uio = uio;
	ap.a_ioflag = ioflag;
	ap.a_cred = cred;

	DO_OPS(ops, error, &ap, vop_read);
	return(error);
}

int
vop_write(struct vop_ops *ops, struct vnode *vp, struct uio *uio, int ioflag,
	struct ucred *cred)
{
	struct vop_write_args ap;
	int error;

	ap.a_head.a_desc = &vop_write_desc;
	ap.a_head.a_ops = ops;
	ap.a_vp = vp;
	ap.a_uio = uio;
	ap.a_ioflag = ioflag;
	ap.a_cred = cred;

	DO_OPS(ops, error, &ap, vop_write);
	return(error);
}

int
vop_lease(struct vop_ops *ops, struct vnode *vp, struct thread *td,
	struct ucred *cred, int flag)
{
	struct vop_lease_args ap;
	int error;

	ap.a_head.a_desc = &vop_lease_desc;
	ap.a_head.a_ops = ops;
	ap.a_vp = vp;
	ap.a_td = td;
	ap.a_cred = cred;
	ap.a_flag = flag;

	DO_OPS(ops, error, &ap, vop_lease);
	return(error);
}

int
vop_ioctl(struct vop_ops *ops, struct vnode *vp, u_long command, caddr_t data,
	int fflag, struct ucred *cred,
	struct thread *td)
{
	struct vop_ioctl_args ap;
	int error;

	ap.a_head.a_desc = &vop_ioctl_desc;
	ap.a_head.a_ops = ops;
	ap.a_vp = vp;
	ap.a_command = command;
	ap.a_data = data;
	ap.a_fflag = fflag;
	ap.a_cred = cred;
	ap.a_td = td;

	DO_OPS(ops, error, &ap, vop_ioctl);
	return(error);
}

int
vop_poll(struct vop_ops *ops, struct vnode *vp, int events, struct ucred *cred,
	struct thread *td)
{
	struct vop_poll_args ap;
	int error;

	ap.a_head.a_desc = &vop_poll_desc;
	ap.a_head.a_ops = ops;
	ap.a_vp = vp;
	ap.a_events = events;
	ap.a_cred = cred;
	ap.a_td = td;

	DO_OPS(ops, error, &ap, vop_poll);
	return(error);
}

int
vop_kqfilter(struct vop_ops *ops, struct vnode *vp, struct knote *kn)
{
	struct vop_kqfilter_args ap;
	int error;

	ap.a_head.a_desc = &vop_kqfilter_desc;
	ap.a_head.a_ops = ops;
	ap.a_vp = vp;
	ap.a_kn = kn;

	DO_OPS(ops, error, &ap, vop_kqfilter);
	return(error);
}

int
vop_revoke(struct vop_ops *ops, struct vnode *vp, int flags)
{
	struct vop_revoke_args ap;
	int error;

	ap.a_head.a_desc = &vop_revoke_desc;
	ap.a_head.a_ops = ops;
	ap.a_vp = vp;
	ap.a_flags = flags;

	DO_OPS(ops, error, &ap, vop_revoke);
	return(error);
}

int
vop_mmap(struct vop_ops *ops, struct vnode *vp, int fflags, struct ucred *cred,
	struct thread *td)
{
	struct vop_mmap_args ap;
	int error;

	ap.a_head.a_desc = &vop_mmap_desc;
	ap.a_head.a_ops = ops;
	ap.a_vp = vp;
	ap.a_fflags = fflags;
	ap.a_cred = cred;
	ap.a_td = td;

	DO_OPS(ops, error, &ap, vop_mmap);
	return(error);
}

int
vop_fsync(struct vop_ops *ops, struct vnode *vp, int waitfor, struct thread *td)
{
	struct vop_fsync_args ap;
	int error;

	ap.a_head.a_desc = &vop_fsync_desc;
	ap.a_head.a_ops = ops;
	ap.a_vp = vp;
	ap.a_waitfor = waitfor;
	ap.a_td = td;

	DO_OPS(ops, error, &ap, vop_fsync);
	return(error);
}

int
vop_remove(struct vop_ops *ops, struct vnode *dvp, struct namecache *par,
	struct vnode *vp, struct componentname *cnp)
{
	struct vop_remove_args ap;
	int error;

	ap.a_head.a_desc = &vop_remove_desc;
	ap.a_head.a_ops = ops;
	ap.a_dvp = dvp;
	ap.a_par = par;
	ap.a_vp = vp;
	ap.a_cnp = cnp;

	DO_OPS(ops, error, &ap, vop_remove);
	return(error);
}

int
vop_link(struct vop_ops *ops, struct vnode *tdvp, struct namecache *par,
	struct vnode *vp, struct componentname *cnp)
{
	struct vop_link_args ap;
	int error;

	ap.a_head.a_desc = &vop_link_desc;
	ap.a_head.a_ops = ops;
	ap.a_tdvp = tdvp;
	ap.a_par = par;
	ap.a_vp = vp;
	ap.a_cnp = cnp;

	DO_OPS(ops, error, &ap, vop_link);
	return(error);
}

int
vop_rename(struct vop_ops *ops, struct vnode *fdvp, struct namecache *fpar,
	struct vnode *fvp, struct componentname *fcnp,
	struct vnode *tdvp, struct namecache *tpar,
	struct vnode *tvp, struct componentname *tcnp)
{
	struct vop_rename_args ap;
	int error;

	ap.a_head.a_desc = &vop_rename_desc;
	ap.a_head.a_ops = ops;
	ap.a_fdvp = fdvp;
	ap.a_fpar = fpar;
	ap.a_fvp = fvp;
	ap.a_fcnp = fcnp;
	ap.a_tdvp = tdvp;
	ap.a_tpar = tpar;
	ap.a_tvp = tvp;
	ap.a_tcnp = tcnp;

	DO_OPS(ops, error, &ap, vop_rename);
	return(error);
}

int
vop_mkdir(struct vop_ops *ops, struct vnode *dvp, struct namecache *par,
	struct vnode **vpp, struct componentname *cnp, struct vattr *vap)
{
	struct vop_mkdir_args ap;
	int error;

	ap.a_head.a_desc = &vop_mkdir_desc;
	ap.a_head.a_ops = ops;
	ap.a_dvp = dvp;
	ap.a_par = par;
	ap.a_vpp = vpp;
	ap.a_cnp = cnp;
	ap.a_vap = vap;

	DO_OPS(ops, error, &ap, vop_mkdir);
	return(error);
}

int
vop_rmdir(struct vop_ops *ops, struct vnode *dvp, struct namecache *par,
	struct vnode *vp, struct componentname *cnp)
{
	struct vop_rmdir_args ap;
	int error;

	ap.a_head.a_desc = &vop_rmdir_desc;
	ap.a_head.a_ops = ops;
	ap.a_dvp = dvp;
	ap.a_par = par;
	ap.a_vp = vp;
	ap.a_cnp = cnp;

	DO_OPS(ops, error, &ap, vop_rmdir);
	return(error);
}

int
vop_symlink(struct vop_ops *ops, struct vnode *dvp, struct namecache *par,
	struct vnode **vpp, struct componentname *cnp,
	struct vattr *vap, char *target)
{
	struct vop_symlink_args ap;
	int error;

	ap.a_head.a_desc = &vop_symlink_desc;
	ap.a_head.a_ops = ops;
	ap.a_dvp = dvp;
	ap.a_par = par;
	ap.a_vpp = vpp;
	ap.a_cnp = cnp;
	ap.a_vap = vap;
	ap.a_target = target;

	DO_OPS(ops, error, &ap, vop_symlink);
	return(error);
}

int
vop_readdir(struct vop_ops *ops, struct vnode *vp, struct uio *uio,
	struct ucred *cred, int *eofflag, int *ncookies, u_long **cookies)
{
	struct vop_readdir_args ap;
	int error;

	ap.a_head.a_desc = &vop_readdir_desc;
	ap.a_head.a_ops = ops;
	ap.a_vp = vp;
	ap.a_uio = uio;
	ap.a_cred = cred;
	ap.a_eofflag = eofflag;
	ap.a_ncookies = ncookies;
	ap.a_cookies = cookies;

	DO_OPS(ops, error, &ap, vop_readdir);
	return(error);
}

int
vop_readlink(struct vop_ops *ops, struct vnode *vp, struct uio *uio,
	struct ucred *cred)
{
	struct vop_readlink_args ap;
	int error;

	ap.a_head.a_desc = &vop_readlink_desc;
	ap.a_head.a_ops = ops;
	ap.a_vp = vp;
	ap.a_uio = uio;
	ap.a_cred = cred;

	DO_OPS(ops, error, &ap, vop_readlink);
	return(error);
}

int
vop_inactive(struct vop_ops *ops, struct vnode *vp, struct thread *td)
{
	struct vop_inactive_args ap;
	int error;

	ap.a_head.a_desc = &vop_inactive_desc;
	ap.a_head.a_ops = ops;
	ap.a_vp = vp;
	ap.a_td = td;

	DO_OPS(ops, error, &ap, vop_inactive);
	return(error);
}

int
vop_reclaim(struct vop_ops *ops, struct vnode *vp, struct thread *td)
{
	struct vop_reclaim_args ap;
	int error;

	ap.a_head.a_desc = &vop_reclaim_desc;
	ap.a_head.a_ops = ops;
	ap.a_vp = vp;
	ap.a_td = td;

	DO_OPS(ops, error, &ap, vop_reclaim);
	return(error);
}

int
vop_lock(struct vop_ops *ops, struct vnode *vp, struct lwkt_tokref *vlock,
	int flags, struct thread *td)
{
	struct vop_lock_args ap;
	int error;

	ap.a_head.a_desc = &vop_lock_desc;
	ap.a_head.a_ops = ops;
	ap.a_vp = vp;
	ap.a_vlock = vlock;
	ap.a_flags = flags;
	ap.a_td = td;

	DO_OPS(ops, error, &ap, vop_lock);
	return(error);
}

int
vop_unlock(struct vop_ops *ops, struct vnode *vp, struct lwkt_tokref *vlock,
	int flags, struct thread *td)
{
	struct vop_unlock_args ap;
	int error;

	ap.a_head.a_desc = &vop_unlock_desc;
	ap.a_head.a_ops = ops;
	ap.a_vp = vp;
	ap.a_vlock = vlock;
	ap.a_flags = flags;
	ap.a_td = td;

	DO_OPS(ops, error, &ap, vop_unlock);
	return(error);
}

int
vop_bmap(struct vop_ops *ops, struct vnode *vp, daddr_t bn, struct vnode **vpp,
	daddr_t *bnp, int *runp, int *runb)
{
	struct vop_bmap_args ap;
	int error;

	ap.a_head.a_desc = &vop_bmap_desc;
	ap.a_head.a_ops = ops;
	ap.a_vp = vp;
	ap.a_bn = bn;
	ap.a_vpp = vpp;
	ap.a_bnp = bnp;
	ap.a_runp = runp;
	ap.a_runb = runb;

	DO_OPS(ops, error, &ap, vop_bmap);
	return(error);
}

int
vop_strategy(struct vop_ops *ops, struct vnode *vp, struct buf *bp)
{
	struct vop_strategy_args ap;
	int error;

	ap.a_head.a_desc = &vop_strategy_desc;
	ap.a_head.a_ops = ops;
	ap.a_vp = vp;
	ap.a_bp = bp;

	DO_OPS(ops, error, &ap, vop_strategy);
	return(error);
}

int
vop_print(struct vop_ops *ops, struct vnode *vp)
{
	struct vop_print_args ap;
	int error;

	ap.a_head.a_desc = &vop_print_desc;
	ap.a_head.a_ops = ops;
	ap.a_vp = vp;

	DO_OPS(ops, error, &ap, vop_print);
	return(error);
}

int
vop_pathconf(struct vop_ops *ops, struct vnode *vp, int name,
	register_t *retval)
{
	struct vop_pathconf_args ap;
	int error;

	ap.a_head.a_desc = &vop_pathconf_desc;
	ap.a_head.a_ops = ops;
	ap.a_vp = vp;
	ap.a_name = name;
	ap.a_retval = retval;

	DO_OPS(ops, error, &ap, vop_pathconf);
	return(error);
}

int
vop_advlock(struct vop_ops *ops, struct vnode *vp, caddr_t id, int op,
	struct flock *fl, int flags)
{
	struct vop_advlock_args ap;
	int error;

	ap.a_head.a_desc = &vop_advlock_desc;
	ap.a_head.a_ops = ops;
	ap.a_vp = vp;
	ap.a_id = id;
	ap.a_op = op;
	ap.a_fl = fl;
	ap.a_flags = flags;

	DO_OPS(ops, error, &ap, vop_advlock);
	return(error);
}

int
vop_balloc(struct vop_ops *ops, struct vnode *vp, off_t startoffset,
	int size, struct ucred *cred, int flags,
	struct buf **bpp)
{
	struct vop_balloc_args ap;
	int error;

	ap.a_head.a_desc = &vop_balloc_desc;
	ap.a_head.a_ops = ops;
	ap.a_vp = vp;
	ap.a_startoffset = startoffset;
	ap.a_size = size;
	ap.a_cred = cred;
	ap.a_flags = flags;
	ap.a_bpp = bpp;

	DO_OPS(ops, error, &ap, vop_balloc);
	return(error);
}

int
vop_reallocblks(struct vop_ops *ops, struct vnode *vp,
	struct cluster_save *buflist)
{
	struct vop_reallocblks_args ap;
	int error;

	ap.a_head.a_desc = &vop_reallocblks_desc;
	ap.a_head.a_ops = ops;
	ap.a_vp = vp;
	ap.a_buflist = buflist;

	DO_OPS(ops, error, &ap, vop_reallocblks);
	return(error);
}

int
vop_getpages(struct vop_ops *ops, struct vnode *vp, vm_page_t *m, int count,
	int reqpage, vm_ooffset_t offset)
{
	struct vop_getpages_args ap;
	int error;

	ap.a_head.a_desc = &vop_getpages_desc;
	ap.a_head.a_ops = ops;
	ap.a_vp = vp;
	ap.a_m = m;
	ap.a_count = count;
	ap.a_reqpage = reqpage;
	ap.a_offset = offset;

	DO_OPS(ops, error, &ap, vop_getpages);
	return(error);
}

int
vop_putpages(struct vop_ops *ops, struct vnode *vp, vm_page_t *m, int count,
	int sync, int *rtvals, vm_ooffset_t offset)
{
	struct vop_putpages_args ap;
	int error;

	ap.a_head.a_desc = &vop_putpages_desc;
	ap.a_head.a_ops = ops;
	ap.a_vp = vp;
	ap.a_m = m;
	ap.a_count = count;
	ap.a_sync = sync;
	ap.a_rtvals = rtvals;
	ap.a_offset = offset;

	DO_OPS(ops, error, &ap, vop_putpages);
	return(error);
}

int
vop_freeblks(struct vop_ops *ops, struct vnode *vp,
	daddr_t addr, daddr_t length)
{
	struct vop_freeblks_args ap;
	int error;

	ap.a_head.a_desc = &vop_freeblks_desc;
	ap.a_head.a_ops = ops;
	ap.a_vp = vp;
	ap.a_addr = addr;
	ap.a_length = length;

	DO_OPS(ops, error, &ap, vop_freeblks);
	return(error);
}

int
vop_bwrite(struct vop_ops *ops, struct vnode *vp, struct buf *bp)
{
	struct vop_bwrite_args ap;
	int error;

	ap.a_head.a_desc = &vop_bwrite_desc;
	ap.a_head.a_ops = ops;
	ap.a_vp = vp;
	ap.a_bp = bp;

	DO_OPS(ops, error, &ap, vop_bwrite);
	return(error);
}

int
vop_getacl(struct vop_ops *ops, struct vnode *vp, acl_type_t type,
	struct acl *aclp, struct ucred *cred, struct thread *td)
{
	struct vop_getacl_args ap;
	int error;

	ap.a_head.a_desc = &vop_getacl_desc;
	ap.a_head.a_ops = ops;
	ap.a_vp = vp;
	ap.a_type = type;
	ap.a_aclp = aclp;
	ap.a_cred = cred;
	ap.a_td = td;

	DO_OPS(ops, error, &ap, vop_getacl);
	return(error);
}

int
vop_setacl(struct vop_ops *ops, struct vnode *vp, acl_type_t type,
	struct acl *aclp, struct ucred *cred, struct thread *td)
{
	struct vop_setacl_args ap;
	int error;

	ap.a_head.a_desc = &vop_setacl_desc;
	ap.a_head.a_ops = ops;
	ap.a_vp = vp;
	ap.a_type = type;
	ap.a_aclp = aclp;
	ap.a_cred = cred;
	ap.a_td = td;

	DO_OPS(ops, error, &ap, vop_setacl);
	return(error);
}

int
vop_aclcheck(struct vop_ops *ops, struct vnode *vp, acl_type_t type,
	struct acl *aclp, struct ucred *cred, struct thread *td)
{
	struct vop_aclcheck_args ap;
	int error;

	ap.a_head.a_desc = &vop_aclcheck_desc;
	ap.a_head.a_ops = ops;
	ap.a_vp = vp;
	ap.a_type = type;
	ap.a_aclp = aclp;
	ap.a_cred = cred;
	ap.a_td = td;

	DO_OPS(ops, error, &ap, vop_aclcheck);
	return(error);
}

int
vop_getextattr(struct vop_ops *ops, struct vnode *vp, char *name, 
	struct uio *uio, struct ucred *cred, struct thread *td)
{
	struct vop_getextattr_args ap;
	int error;

	ap.a_head.a_desc = &vop_getextattr_desc;
	ap.a_head.a_ops = ops;
	ap.a_vp = vp;
	ap.a_name = name;
	ap.a_uio = uio;
	ap.a_cred = cred;
	ap.a_td = td;

	DO_OPS(ops, error, &ap, vop_getextattr);
	return(error);
}

int
vop_setextattr(struct vop_ops *ops, struct vnode *vp, char *name, 
	struct uio *uio, struct ucred *cred, struct thread *td)
{
	struct vop_setextattr_args ap;
	int error;

	ap.a_head.a_desc = &vop_setextattr_desc;
	ap.a_head.a_ops = ops;
	ap.a_vp = vp;
	ap.a_name = name;
	ap.a_uio = uio;
	ap.a_cred = cred;
	ap.a_td = td;

	DO_OPS(ops, error, &ap, vop_setextattr);
	return(error);
}

int
vop_createvobject(struct vop_ops *ops, struct vnode *vp, struct thread *td)
{
	struct vop_createvobject_args ap;
	int error;

	ap.a_head.a_desc = &vop_createvobject_desc;
	ap.a_head.a_ops = ops;
	ap.a_vp = vp;
	ap.a_td = td;

	DO_OPS(ops, error, &ap, vop_createvobject);
	return(error);
}

int
vop_destroyvobject(struct vop_ops *ops, struct vnode *vp)
{
	struct vop_destroyvobject_args ap;
	int error;

	ap.a_head.a_desc = &vop_destroyvobject_desc;
	ap.a_head.a_ops = ops;
	ap.a_vp = vp;

	DO_OPS(ops, error, &ap, vop_destroyvobject);
	return(error);
}

int
vop_getvobject(struct vop_ops *ops, struct vnode *vp, struct vm_object **objpp)
{
	struct vop_getvobject_args ap;
	int error;

	ap.a_head.a_desc = &vop_getvobject_desc;
	ap.a_head.a_ops = ops;
	ap.a_vp = vp;
	ap.a_objpp = objpp;

	DO_OPS(ops, error, &ap, vop_getvobject);
	return(error);
}

int
vop_vfsset(struct vop_ops *ops, int op, const char *opstr)
{
	struct vop_vfsset_args ap;
	int error;

	ap.a_head.a_desc = &vop_vfsset_desc;
	ap.a_head.a_ops = ops;
	ap.a_op = op;
	ap.a_opstr = opstr;

	DO_OPS(ops, error, &ap, vop_vfsset);
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
 */
int
vop_vnoperate_ap(struct vop_generic_args *ap)
{
	struct vop_ops *ops;
	int error;

	ops = ap->a_ops;
	if (ops->vv_flags & VVF_HOOK_MASK) {
		if (ops->vv_flags & VVF_JOURNAL_HOOK)
			error = VOCALL(ops->vv_jops, ap);
		else
			error = 0;
		if (error == 0)
			error = VOCALL(ops, ap);
	} else {
		error = VOCALL(ops, ap);
	}
	return (error);
}

int
vop_islocked_ap(struct vop_islocked_args *ap)
{
	int error;

	DO_OPS(ap->a_head.a_ops, error, ap, vop_islocked);
	return(error);
}

int
vop_lookup_ap(struct vop_lookup_args *ap)
{
	int error;

	DO_OPS(ap->a_head.a_ops, error, ap, vop_lookup);
	return(error);
}

int
vop_cachedlookup_ap(struct vop_cachedlookup_args *ap)
{
	int error;

	DO_OPS(ap->a_head.a_ops, error, ap, vop_cachedlookup);
	return(error);
}

int
vop_create_ap(struct vop_create_args *ap)
{
	int error;

	DO_OPS(ap->a_head.a_ops, error, ap, vop_create);
	return(error);
}

int
vop_whiteout_ap(struct vop_whiteout_args *ap)
{
	int error;

	DO_OPS(ap->a_head.a_ops, error, ap, vop_whiteout);
	return(error);
}

int
vop_mknod_ap(struct vop_mknod_args *ap)
{
	int error;

	DO_OPS(ap->a_head.a_ops, error, ap, vop_mknod);
	return(error);
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
vop_lease_ap(struct vop_lease_args *ap)
{
	int error;

	DO_OPS(ap->a_head.a_ops, error, ap, vop_lease);
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
vop_revoke_ap(struct vop_revoke_args *ap)
{
	int error;

	DO_OPS(ap->a_head.a_ops, error, ap, vop_revoke);
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
vop_remove_ap(struct vop_remove_args *ap)
{
	int error;

	DO_OPS(ap->a_head.a_ops, error, ap, vop_remove);
	return(error);
}

int
vop_link_ap(struct vop_link_args *ap)
{
	int error;

	DO_OPS(ap->a_head.a_ops, error, ap, vop_link);
	return(error);
}

int
vop_rename_ap(struct vop_rename_args *ap)
{
	int error;

	DO_OPS(ap->a_head.a_ops, error, ap, vop_rename);
	return(error);
}

int
vop_mkdir_ap(struct vop_mkdir_args *ap)
{
	int error;

	DO_OPS(ap->a_head.a_ops, error, ap, vop_mkdir);
	return(error);
}

int
vop_rmdir_ap(struct vop_rmdir_args *ap)
{
	int error;

	DO_OPS(ap->a_head.a_ops, error, ap, vop_rmdir);
	return(error);
}

int
vop_symlink_ap(struct vop_symlink_args *ap)
{
	int error;

	DO_OPS(ap->a_head.a_ops, error, ap, vop_symlink);
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
vop_lock_ap(struct vop_lock_args *ap)
{
	int error;

	DO_OPS(ap->a_head.a_ops, error, ap, vop_lock);
	return(error);
}

int
vop_unlock_ap(struct vop_unlock_args *ap)
{
	int error;

	DO_OPS(ap->a_head.a_ops, error, ap, vop_unlock);
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
vop_bwrite_ap(struct vop_bwrite_args *ap)
{
	int error;

	DO_OPS(ap->a_head.a_ops, error, ap, vop_bwrite);
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
vop_createvobject_ap(struct vop_createvobject_args *ap)
{
	int error;

	DO_OPS(ap->a_head.a_ops, error, ap, vop_createvobject);
	return(error);
}

int
vop_destroyvobject_ap(struct vop_destroyvobject_args *ap)
{
	int error;

	DO_OPS(ap->a_head.a_ops, error, ap, vop_destroyvobject);
	return(error);
}

int
vop_getvobject_ap(struct vop_getvobject_args *ap)
{
	int error;

	DO_OPS(ap->a_head.a_ops, error, ap, vop_getvobject);
	return(error);
}

int
vop_vfsset_ap(struct vop_vfsset_args *ap)
{
	int error;

	DO_OPS(ap->a_head.a_ops, error, ap, vop_vfsset);
	return(error);
}

