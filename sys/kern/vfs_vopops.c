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
 * $DragonFly: src/sys/kern/vfs_vopops.c,v 1.1 2004/08/13 17:51:09 dillon Exp $
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
#include <sys/vopops.h>

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
	 VDESC_VP2_WILLRELE|VDESC_VP3_WILLRELE,
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

int
vop_islocked(struct vnode *vp, struct thread *td)
{
	struct vop_islocked_args ap;

	ap.a_head.a_desc = &vop_islocked_desc;
	ap.a_vp = vp;
	ap.a_td = td;
	return(vp->v_vops->vop_islocked(&ap));
}

int
vop_lookup(struct vnode *dvp, struct namecache *par,
		struct vnode **vpp, struct namecache **ncpp,
		struct componentname *cnp)
{
	struct vop_lookup_args ap;

	ap.a_head.a_desc = &vop_lookup_desc;
	ap.a_dvp = dvp;
	ap.a_par = par;
	ap.a_vpp = vpp;
	ap.a_ncpp = ncpp;
	ap.a_cnp = cnp;
	return(dvp->v_vops->vop_lookup(&ap));
}

int
vop_cachedlookup(struct vnode *dvp, struct namecache *par,
		struct vnode **vpp, struct namecache **ncpp,
		struct componentname *cnp)
{
	struct vop_cachedlookup_args ap;

	ap.a_head.a_desc = &vop_cachedlookup_desc;
	ap.a_dvp = dvp;
	ap.a_par = par;
	ap.a_vpp = vpp;
	ap.a_ncpp = ncpp;
	ap.a_cnp = cnp;
	return(dvp->v_vops->vop_cachedlookup(&ap));
}

int
vop_create(struct vnode *dvp, struct namecache *par,
		struct vnode **vpp, struct componentname *cnp,
		struct vattr *vap)
{
	struct vop_create_args ap;

	ap.a_head.a_desc = &vop_create_desc;
	ap.a_dvp = dvp;
	ap.a_par = par;
	ap.a_vpp = vpp;
	ap.a_cnp = cnp;
	ap.a_vap = vap;
	return(dvp->v_vops->vop_create(&ap));
}

int
vop_whiteout(struct vnode *dvp, struct namecache *par,
		struct componentname *cnp, int flags)
{
	struct vop_whiteout_args ap;

	ap.a_head.a_desc = &vop_whiteout_desc;
	ap.a_dvp = dvp;
	ap.a_par = par;
	ap.a_cnp = cnp;
	ap.a_flags = flags;
	return(dvp->v_vops->vop_whiteout(&ap));
}

int
vop_mknod(struct vnode *dvp, struct namecache *par,
		struct vnode **vpp, struct componentname *cnp,
		struct vattr *vap)
{
	struct vop_mknod_args ap;

	ap.a_head.a_desc = &vop_mknod_desc;
	ap.a_dvp = dvp;
	ap.a_par = par;
	ap.a_vpp = vpp;
	ap.a_cnp = cnp;
	ap.a_vap = vap;
	return(dvp->v_vops->vop_mknod(&ap));
}

int
vop_open(struct vnode *vp, int mode, struct ucred *cred,
		struct thread *td)
{
	struct vop_open_args ap;

	ap.a_head.a_desc = &vop_open_desc;
	ap.a_vp = vp;
	ap.a_mode = mode;
	ap.a_cred = cred;
	ap.a_td = td;
	return(vp->v_vops->vop_open(&ap));
}

int
vop_close(struct vnode *vp, int fflag, struct thread *td)
{
	struct vop_close_args ap;

	ap.a_head.a_desc = &vop_close_desc;
	ap.a_vp = vp;
	ap.a_fflag = fflag;
	ap.a_td = td;
	return(vp->v_vops->vop_close(&ap));
}

int
vop_access(struct vnode *vp, int mode, struct ucred *cred,
		struct thread *td)
{
	struct vop_access_args ap;

	ap.a_head.a_desc = &vop_access_desc;
	ap.a_vp = vp;
	ap.a_mode = mode;
	ap.a_cred = cred;
	ap.a_td = td;
	return(vp->v_vops->vop_access(&ap));
}

int
vop_getattr(struct vnode *vp, struct vattr *vap,
		struct thread *td)
{
	struct vop_getattr_args ap;

	ap.a_head.a_desc = &vop_getattr_desc;
	ap.a_vp = vp;
	ap.a_vap = vap;
	ap.a_td = td;
	return(vp->v_vops->vop_getattr(&ap));
}

int
vop_setattr(struct vnode *vp, struct vattr *vap,
		struct ucred *cred, struct thread *td)
{
	struct vop_setattr_args ap;

	ap.a_head.a_desc = &vop_setattr_desc;
	ap.a_vp = vp;
	ap.a_vap = vap;
	ap.a_cred = cred;
	ap.a_td = td;
	return(vp->v_vops->vop_setattr(&ap));
}

int
vop_read(struct vnode *vp, struct uio *uio, int ioflag,
		struct ucred *cred)
{
	struct vop_read_args ap;

	ap.a_head.a_desc = &vop_read_desc;
	ap.a_vp = vp;
	ap.a_uio = uio;
	ap.a_ioflag = ioflag;
	ap.a_cred = cred;
	return(vp->v_vops->vop_read(&ap));
}

int
vop_write(struct vnode *vp, struct uio *uio, int ioflag,
		struct ucred *cred)
{
	struct vop_write_args ap;

	ap.a_head.a_desc = &vop_write_desc;
	ap.a_vp = vp;
	ap.a_uio = uio;
	ap.a_ioflag = ioflag;
	ap.a_cred = cred;
	return(vp->v_vops->vop_write(&ap));
}

int
vop_lease(struct vnode *vp, struct thread *td,
		struct ucred *cred, int flag)
{
	struct vop_lease_args ap;

	ap.a_head.a_desc = &vop_lease_desc;
	ap.a_vp = vp;
	ap.a_td = td;
	ap.a_cred = cred;
	ap.a_flag = flag;
	return(vp->v_vops->vop_lease(&ap));
}

int
vop_ioctl(struct vnode *vp, u_long command, caddr_t data,
		int fflag, struct ucred *cred,
		struct thread *td)
{
	struct vop_ioctl_args ap;

	ap.a_head.a_desc = &vop_ioctl_desc;
	ap.a_vp = vp;
	ap.a_command = command;
	ap.a_data = data;
	ap.a_fflag = fflag;
	ap.a_cred = cred;
	ap.a_td = td;
	return(vp->v_vops->vop_ioctl(&ap));
}

int
vop_poll(struct vnode *vp, int events, struct ucred *cred,
		struct thread *td)
{
	struct vop_poll_args ap;

	ap.a_head.a_desc = &vop_poll_desc;
	ap.a_vp = vp;
	ap.a_events = events;
	ap.a_cred = cred;
	ap.a_td = td;
	return(vp->v_vops->vop_poll(&ap));
}

int
vop_kqfilter(struct vnode *vp, struct knote *kn)
{
	struct vop_kqfilter_args ap;

	ap.a_head.a_desc = &vop_kqfilter_desc;
	ap.a_vp = vp;
	ap.a_kn = kn;
	return(vp->v_vops->vop_kqfilter(&ap));
}

int
vop_revoke(struct vnode *vp, int flags)
{
	struct vop_revoke_args ap;

	ap.a_head.a_desc = &vop_revoke_desc;
	ap.a_vp = vp;
	ap.a_flags = flags;
	return(vp->v_vops->vop_revoke(&ap));
}

int
vop_mmap(struct vnode *vp, int fflags, struct ucred *cred,
		struct thread *td)
{
	struct vop_mmap_args ap;

	ap.a_head.a_desc = &vop_mmap_desc;
	ap.a_vp = vp;
	ap.a_fflags = fflags;
	ap.a_cred = cred;
	ap.a_td = td;
	return(vp->v_vops->vop_mmap(&ap));
}

int
vop_fsync(struct vnode *vp, int waitfor, struct thread *td)
{
	struct vop_fsync_args ap;

	ap.a_head.a_desc = &vop_fsync_desc;
	ap.a_vp = vp;
	ap.a_waitfor = waitfor;
	ap.a_td = td;
	return(vp->v_vops->vop_fsync(&ap));
}

int
vop_remove(struct vnode *dvp, struct namecache *par,
		struct vnode *vp, struct componentname *cnp)
{
	struct vop_remove_args ap;

	ap.a_head.a_desc = &vop_remove_desc;
	ap.a_dvp = dvp;
	ap.a_par = par;
	ap.a_vp = vp;
	ap.a_cnp = cnp;
	return(dvp->v_vops->vop_remove(&ap));
}

int
vop_link(struct vnode *tdvp, struct namecache *par,
		struct vnode *vp, struct componentname *cnp)
{
	struct vop_link_args ap;

	ap.a_head.a_desc = &vop_link_desc;
	ap.a_tdvp = tdvp;
	ap.a_par = par;
	ap.a_vp = vp;
	ap.a_cnp = cnp;
	return(tdvp->v_vops->vop_link(&ap));
}

int
vop_rename(struct vnode *fdvp, struct namecache *fpar,
		struct vnode *fvp, struct componentname *fcnp,
		struct vnode *tdvp, struct namecache *tpar,
		struct vnode *tvp, struct componentname *tcnp)
{
	struct vop_rename_args ap;

	ap.a_head.a_desc = &vop_rename_desc;
	ap.a_fdvp = fdvp;
	ap.a_fpar = fpar;
	ap.a_fvp = fvp;
	ap.a_fcnp = fcnp;
	ap.a_tdvp = tdvp;
	ap.a_tpar = tpar;
	ap.a_tvp = tvp;
	ap.a_tcnp = tcnp;
	return(fdvp->v_vops->vop_rename(&ap));
}

int
vop_mkdir(struct vnode *dvp, struct namecache *par,
		struct vnode **vpp, struct componentname *cnp,
		struct vattr *vap)
{
	struct vop_mkdir_args ap;

	ap.a_head.a_desc = &vop_mkdir_desc;
	ap.a_dvp = dvp;
	ap.a_par = par;
	ap.a_vpp = vpp;
	ap.a_cnp = cnp;
	ap.a_vap = vap;
	return(dvp->v_vops->vop_mkdir(&ap));
}

int
vop_rmdir(struct vnode *dvp, struct namecache *par,
		struct vnode *vp, struct componentname *cnp)
{
	struct vop_rmdir_args ap;

	ap.a_head.a_desc = &vop_rmdir_desc;
	ap.a_dvp = dvp;
	ap.a_par = par;
	ap.a_vp = vp;
	ap.a_cnp = cnp;
	return(dvp->v_vops->vop_rmdir(&ap));
}

int
vop_symlink(struct vnode *dvp, struct namecache *par,
		struct vnode **vpp, struct componentname *cnp,
		struct vattr *vap, char *target)
{
	struct vop_symlink_args ap;

	ap.a_head.a_desc = &vop_symlink_desc;
	ap.a_dvp = dvp;
	ap.a_par = par;
	ap.a_vpp = vpp;
	ap.a_cnp = cnp;
	ap.a_vap = vap;
	ap.a_target = target;
	return(dvp->v_vops->vop_symlink(&ap));
}

int
vop_readdir(struct vnode *vp, struct uio *uio,
		struct ucred *cred, int *eofflag, 
		int *ncookies, u_long **cookies)
{
	struct vop_readdir_args ap;

	ap.a_head.a_desc = &vop_readdir_desc;
	ap.a_vp = vp;
	ap.a_uio = uio;
	ap.a_cred = cred;
	ap.a_eofflag = eofflag;
	ap.a_ncookies = ncookies;
	ap.a_cookies = cookies;
	return(vp->v_vops->vop_readdir(&ap));
}

int
vop_readlink(struct vnode *vp, struct uio *uio,
		struct ucred *cred)
{
	struct vop_readlink_args ap;

	ap.a_head.a_desc = &vop_readlink_desc;
	ap.a_vp = vp;
	ap.a_uio = uio;
	ap.a_cred = cred;
	return(vp->v_vops->vop_readlink(&ap));
}

int
vop_inactive(struct vnode *vp, struct thread *td)
{
	struct vop_inactive_args ap;

	ap.a_head.a_desc = &vop_inactive_desc;
	ap.a_vp = vp;
	ap.a_td = td;
	return(vp->v_vops->vop_inactive(&ap));
}

int
vop_reclaim(struct vnode *vp, struct thread *td)
{
	struct vop_reclaim_args ap;

	ap.a_head.a_desc = &vop_reclaim_desc;
	ap.a_vp = vp;
	ap.a_td = td;
	return(vp->v_vops->vop_reclaim(&ap));
}

int
vop_lock(struct vnode *vp, struct lwkt_tokref *vlock,
		int flags, struct thread *td)
{
	struct vop_lock_args ap;

	ap.a_head.a_desc = &vop_lock_desc;
	ap.a_vp = vp;
	ap.a_vlock = vlock;
	ap.a_flags = flags;
	ap.a_td = td;
	return(vp->v_vops->vop_lock(&ap));
}

int
vop_unlock(struct vnode *vp, struct lwkt_tokref *vlock,
		int flags, struct thread *td)
{
	struct vop_unlock_args ap;

	ap.a_head.a_desc = &vop_unlock_desc;
	ap.a_vp = vp;
	ap.a_vlock = vlock;
	ap.a_flags = flags;
	ap.a_td = td;
	return(vp->v_vops->vop_unlock(&ap));
}

int
vop_bmap(struct vnode *vp, daddr_t bn, struct vnode **vpp,
		daddr_t *bnp, int *runp, int *runb)
{
	struct vop_bmap_args ap;

	ap.a_head.a_desc = &vop_bmap_desc;
	ap.a_vp = vp;
	ap.a_bn = bn;
	ap.a_vpp = vpp;
	ap.a_bnp = bnp;
	ap.a_runp = runp;
	ap.a_runb = runb;
	return(vp->v_vops->vop_bmap(&ap));
}

int
vop_strategy(struct vnode *vp, struct buf *bp)
{
	struct vop_strategy_args ap;

	ap.a_head.a_desc = &vop_strategy_desc;
	ap.a_vp = vp;
	ap.a_bp = bp;
	return(vp->v_vops->vop_strategy(&ap));
}

int
vop_print(struct vnode *vp)
{
	struct vop_print_args ap;

	ap.a_head.a_desc = &vop_print_desc;
	ap.a_vp = vp;
	return(vp->v_vops->vop_print(&ap));
}

int
vop_pathconf(struct vnode *vp, int name,
		register_t *retval)
{
	struct vop_pathconf_args ap;

	ap.a_head.a_desc = &vop_pathconf_desc;
	ap.a_vp = vp;
	ap.a_name = name;
	ap.a_retval = retval;
	return(vp->v_vops->vop_pathconf(&ap));
}

int
vop_advlock(struct vnode *vp, caddr_t id, int op,
		struct flock *fl, int flags)
{
	struct vop_advlock_args ap;

	ap.a_head.a_desc = &vop_advlock_desc;
	ap.a_vp = vp;
	ap.a_id = id;
	ap.a_op = op;
	ap.a_fl = fl;
	ap.a_flags = flags;
	return(vp->v_vops->vop_advlock(&ap));
}

int
vop_balloc(struct vnode *vp, off_t startoffset,
		int size, struct ucred *cred, int flags,
		struct buf **bpp)
{
	struct vop_balloc_args ap;

	ap.a_head.a_desc = &vop_balloc_desc;
	ap.a_vp = vp;
	ap.a_startoffset = startoffset;
	ap.a_size = size;
	ap.a_cred = cred;
	ap.a_flags = flags;
	ap.a_bpp = bpp;
	return(vp->v_vops->vop_balloc(&ap));
}

int
vop_reallocblks(struct vnode *vp,
		struct cluster_save *buflist)
{
	struct vop_reallocblks_args ap;

	ap.a_head.a_desc = &vop_reallocblks_desc;
	ap.a_vp = vp;
	ap.a_buflist = buflist;
	return(vp->v_vops->vop_reallocblks(&ap));
}

int
vop_getpages(struct vnode *vp, vm_page_t *m, int count,
		int reqpage, vm_ooffset_t offset)
{
	struct vop_getpages_args ap;

	ap.a_head.a_desc = &vop_getpages_desc;
	ap.a_vp = vp;
	ap.a_m = m;
	ap.a_count = count;
	ap.a_reqpage = reqpage;
	ap.a_offset = offset;
	return(vp->v_vops->vop_getpages(&ap));
}

int
vop_putpages(struct vnode *vp, vm_page_t *m, int count,
		int sync, int *rtvals,
		vm_ooffset_t offset)
{
	struct vop_putpages_args ap;

	ap.a_head.a_desc = &vop_putpages_desc;
	ap.a_vp = vp;
	ap.a_m = m;
	ap.a_count = count;
	ap.a_sync = sync;
	ap.a_rtvals = rtvals;
	ap.a_offset = offset;
	return(vp->v_vops->vop_putpages(&ap));
}

int
vop_freeblks(struct vnode *vp, daddr_t addr,
		daddr_t length)
{
	struct vop_freeblks_args ap;

	ap.a_head.a_desc = &vop_freeblks_desc;
	ap.a_vp = vp;
	ap.a_addr = addr;
	ap.a_length = length;
	return(vp->v_vops->vop_freeblks(&ap));
}

int
vop_bwrite(struct vnode *vp, struct buf *bp)
{
	struct vop_bwrite_args ap;

	ap.a_head.a_desc = &vop_bwrite_desc;
	ap.a_vp = vp;
	ap.a_bp = bp;
	return(vp->v_vops->vop_bwrite(&ap));
}

int
vop_getacl(struct vnode *vp, acl_type_t type,
		struct acl *aclp, struct ucred *cred,
		struct thread *td)
{
	struct vop_getacl_args ap;

	ap.a_head.a_desc = &vop_getacl_desc;
	ap.a_vp = vp;
	ap.a_type = type;
	ap.a_aclp = aclp;
	ap.a_cred = cred;
	ap.a_td = td;
	return(vp->v_vops->vop_getacl(&ap));
}

int
vop_setacl(struct vnode *vp, acl_type_t type,
		struct acl *aclp, struct ucred *cred,
		struct thread *td)
{
	struct vop_setacl_args ap;

	ap.a_head.a_desc = &vop_setacl_desc;
	ap.a_vp = vp;
	ap.a_type = type;
	ap.a_aclp = aclp;
	ap.a_cred = cred;
	ap.a_td = td;
	return(vp->v_vops->vop_setacl(&ap));
}

int
vop_aclcheck(struct vnode *vp, acl_type_t type,
		struct acl *aclp, struct ucred *cred,
		struct thread *td)
{
	struct vop_aclcheck_args ap;

	ap.a_head.a_desc = &vop_aclcheck_desc;
	ap.a_vp = vp;
	ap.a_type = type;
	ap.a_aclp = aclp;
	ap.a_cred = cred;
	ap.a_td = td;
	return(vp->v_vops->vop_aclcheck(&ap));
}

int
vop_getextattr(struct vnode *vp, char *name, 
		struct uio *uio, struct ucred *cred,
		struct thread *td)
{
	struct vop_getextattr_args ap;

	ap.a_head.a_desc = &vop_getextattr_desc;
	ap.a_vp = vp;
	ap.a_name = name;
	ap.a_uio = uio;
	ap.a_cred = cred;
	ap.a_td = td;
	return(vp->v_vops->vop_getextattr(&ap));
}

int
vop_setextattr(struct vnode *vp, char *name, 
		struct uio *uio, struct ucred *cred,
		struct thread *td)
{
	struct vop_setextattr_args ap;

	ap.a_head.a_desc = &vop_setextattr_desc;
	ap.a_vp = vp;
	ap.a_name = name;
	ap.a_uio = uio;
	ap.a_cred = cred;
	ap.a_td = td;
	return(vp->v_vops->vop_setextattr(&ap));
}

int
vop_createvobject(struct vnode *vp, struct thread *td)
{
	struct vop_createvobject_args ap;

	ap.a_head.a_desc = &vop_createvobject_desc;
	ap.a_vp = vp;
	ap.a_td = td;
	return(vp->v_vops->vop_createvobject(&ap));
}

int
vop_destroyvobject(struct vnode *vp)
{
	struct vop_destroyvobject_args ap;

	ap.a_head.a_desc = &vop_destroyvobject_desc;
	ap.a_vp = vp;
	return(vp->v_vops->vop_destroyvobject(&ap));
}

int
vop_getvobject(struct vnode *vp, struct vm_object **objpp)
{
	struct vop_getvobject_args ap;

	ap.a_head.a_desc = &vop_getvobject_desc;
	ap.a_vp = vp;
	ap.a_objpp = objpp;
	return(vp->v_vops->vop_getvobject(&ap));
}

