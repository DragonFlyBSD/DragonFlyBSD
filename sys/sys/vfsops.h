/*
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
 * $DragonFly: src/sys/sys/vfsops.h,v 1.6 2004/09/28 00:25:31 dillon Exp $
 */

/*
 * The vop_ops structure vectors all access to a filesystem.  It contains a
 * fixed set of vectors which are 'compiled' by the vnodeopv_entry_desc
 * array that is typically declared in "vfs/blah/blah_vnops.c".
 *
 * In DragonFly the ultimate goal is to thread the VFS, which means that
 * the dispatch functions will eventually be called from the context of 
 * a management thread rather then directly called by a process.  This
 * requires us to divorce direct process dependancies (in particular ioctl
 * and UIO's).  In addition, it is our intention to implement kernel
 * level cache management and coherency in the vop_*() interfacing
 * layer.
 *
 * The number of management threads will depend on the VFS.  The idea is
 * to give a high performance VFS such as UFS at least one thread per cpu,
 * and a low performance VFS such as CD9660 perhaps just one thread period.
 * Blocking issues within the management thread are up to the VFS itself,
 * but DragonFly will introduce a layer above the VFS to handle cacheable
 * requests without having to enter the VFS (e.g. by making attribute
 * and VM object information a permanent fixture of the vnode structure
 * and accessing them directly).
 *
 * THE VOPOPS VECTORS SHOULD NEVER BE CALLED DIRECTLY!  Instead always use
 * the kernel helper procedures vop_*() to call a vopop.  The kernel
 * helper procedure will be responsible for handling the DragonFly messaging
 * conversion when it occurs.
 */

#ifndef _SYS_VFSOPS_H_
#define	_SYS_VFSOPS_H_

#ifndef _SYS_ACL_H_
#include <sys/acl.h>
#endif

struct vnode;
struct thread;
struct namecache;
struct componentname;
struct vattr;
struct ucred;
struct uio;
struct knote;
struct vm_object;
struct vm_page;
struct vfscache;

struct vop_generic_args {
	struct vnodeop_desc *a_desc;	/* command descriptor for the call */
	struct vop_ops *a_ops;		/* operations vector for the call */
	int a_reserved[4];
};

struct vop_islocked_args {
	struct vop_generic_args a_head;
	struct vnode *a_vp;
	struct thread *a_td;
};

struct vop_resolve_args {
	struct vop_generic_args a_head;
	struct namecache *a_ncp;
	struct componentname *a_cnp;
};

struct vop_lookup_args {
	struct vop_generic_args a_head;
	struct vnode *a_dvp;
	struct vnode **a_vpp;
	struct componentname *a_cnp;
};

struct vop_cachedlookup_args {
	struct vop_generic_args a_head;
	struct vnode *a_dvp;
	struct vnode **a_vpp;
	struct componentname *a_cnp;
};

struct vop_create_args {
	struct vop_generic_args a_head;
	struct vnode *a_dvp;
	struct namecache *a_par;
	struct vnode **a_vpp;
	struct componentname *a_cnp;
	struct vattr *a_vap;
};

struct vop_whiteout_args {
	struct vop_generic_args a_head;
	struct vnode *a_dvp;
	struct namecache *a_par;
	struct componentname *a_cnp;
	int a_flags;
};

struct vop_mknod_args {
	struct vop_generic_args a_head;
	struct vnode *a_dvp;
	struct namecache *a_par;
	struct vnode **a_vpp;
	struct componentname *a_cnp;
	struct vattr *a_vap;
};

struct vop_open_args {
	struct vop_generic_args a_head;
	struct vnode *a_vp;
	int a_mode;
	struct ucred *a_cred;
	struct thread *a_td;
};

struct vop_close_args {
	struct vop_generic_args a_head;
	struct vnode *a_vp;
	int a_fflag;
	struct thread *a_td;
};

struct vop_access_args {
	struct vop_generic_args a_head;
	struct vnode *a_vp;
	int a_mode;
	struct ucred *a_cred;
	struct thread *a_td;
};

struct vop_getattr_args {
	struct vop_generic_args a_head;
	struct vnode *a_vp;
	struct vattr *a_vap;
	struct thread *a_td;
};

struct vop_setattr_args {
	struct vop_generic_args a_head;
	struct vnode *a_vp;
	struct vattr *a_vap;
	struct ucred *a_cred;
	struct thread *a_td;
};

struct vop_read_args {
	struct vop_generic_args a_head;
	struct vnode *a_vp;
	struct uio *a_uio;
	int a_ioflag;
	struct ucred *a_cred;
};

struct vop_write_args {
	struct vop_generic_args a_head;
	struct vnode *a_vp;
	struct uio *a_uio;
	int a_ioflag;
	struct ucred *a_cred;
};

struct vop_lease_args {
	struct vop_generic_args a_head;
	struct vnode *a_vp;
	struct thread *a_td;
	struct ucred *a_cred;
	int a_flag;
};

struct vop_ioctl_args {
	struct vop_generic_args a_head;
	struct vnode *a_vp;
	u_long a_command;
	caddr_t a_data;
	int a_fflag;
	struct ucred *a_cred;
	struct thread *a_td;
};

struct vop_poll_args {
	struct vop_generic_args a_head;
	struct vnode *a_vp;
	int a_events;
	struct ucred *a_cred;
	struct thread *a_td;
};

struct vop_kqfilter_args {
	struct vop_generic_args a_head;
	struct vnode *a_vp;
	struct knote *a_kn;
};

struct vop_revoke_args {
	struct vop_generic_args a_head;
	struct vnode *a_vp;
	int a_flags;
};

struct vop_mmap_args {
	struct vop_generic_args a_head;
	struct vnode *a_vp;
	int a_fflags;
	struct ucred *a_cred;
	struct thread *a_td;
};

struct vop_fsync_args {
	struct vop_generic_args a_head;
	struct vnode *a_vp;
	int a_waitfor;
	struct thread *a_td;
};

struct vop_remove_args {
	struct vop_generic_args a_head;
	struct vnode *a_dvp;
	struct namecache *a_par;
	struct vnode *a_vp;
	struct componentname *a_cnp;
};

struct vop_link_args {
	struct vop_generic_args a_head;
	struct vnode *a_tdvp;
	struct namecache *a_par;
	struct vnode *a_vp;
	struct componentname *a_cnp;
};

struct vop_rename_args {
	struct vop_generic_args a_head;
	struct vnode *a_fdvp;
	struct namecache *a_fpar;
	struct vnode *a_fvp;
	struct componentname *a_fcnp;
	struct vnode *a_tdvp;
	struct namecache *a_tpar;
	struct vnode *a_tvp;
	struct componentname *a_tcnp;
};

struct vop_mkdir_args {
	struct vop_generic_args a_head;
	struct vnode *a_dvp;
	struct namecache *a_par;
	struct vnode **a_vpp;
	struct componentname *a_cnp;
	struct vattr *a_vap;
};

struct vop_rmdir_args {
	struct vop_generic_args a_head;
	struct vnode *a_dvp;
	struct namecache *a_par;
	struct vnode *a_vp;
	struct componentname *a_cnp;
};

struct vop_symlink_args {
	struct vop_generic_args a_head;
	struct vnode *a_dvp;
	struct namecache *a_par;
	struct vnode **a_vpp;
	struct componentname *a_cnp;
	struct vattr *a_vap;
	char *a_target;
};

struct vop_readdir_args {
	struct vop_generic_args a_head;
	struct vnode *a_vp;
	struct uio *a_uio;
	struct ucred *a_cred;
	int *a_eofflag;
	int *a_ncookies;
	u_long **a_cookies;
};

struct vop_readlink_args {
	struct vop_generic_args a_head;
	struct vnode *a_vp;
	struct uio *a_uio;
	struct ucred *a_cred;
};

struct vop_inactive_args {
	struct vop_generic_args a_head;
	struct vnode *a_vp;
	struct thread *a_td;
};

struct vop_reclaim_args {
	struct vop_generic_args a_head;
	struct vnode *a_vp;
	struct thread *a_td;
};

struct vop_lock_args {
	struct vop_generic_args a_head;
	struct vnode *a_vp;
	struct lwkt_tokref *a_vlock;
	int a_flags;
	struct thread *a_td;
};

struct vop_unlock_args {
	struct vop_generic_args a_head;
	struct vnode *a_vp;
	struct lwkt_tokref *a_vlock;
	int a_flags;
	struct thread *a_td;
};

struct vop_bmap_args {
	struct vop_generic_args a_head;
	struct vnode *a_vp;
	daddr_t a_bn;
	struct vnode **a_vpp;
	daddr_t *a_bnp;
	int *a_runp;
	int *a_runb;
};

struct vop_strategy_args {
	struct vop_generic_args a_head;
	struct vnode *a_vp;
	struct buf *a_bp;
};

struct vop_print_args {
	struct vop_generic_args a_head;
	struct vnode *a_vp;
};

struct vop_pathconf_args {
	struct vop_generic_args a_head;
	struct vnode *a_vp;
	int a_name;
	register_t *a_retval;
};

struct vop_advlock_args {
	struct vop_generic_args a_head;
	struct vnode *a_vp;
	caddr_t a_id;
	int a_op;
	struct flock *a_fl;
	int a_flags;
};

struct vop_balloc_args {
	struct vop_generic_args a_head;
	struct vnode *a_vp;
	off_t a_startoffset;
	int a_size;
	struct ucred *a_cred;
	int a_flags;
	struct buf **a_bpp;
};

struct vop_reallocblks_args {
	struct vop_generic_args a_head;
	struct vnode *a_vp;
	struct cluster_save *a_buflist;
};

struct vop_getpages_args {
	struct vop_generic_args a_head;
	struct vnode *a_vp;
	struct vm_page **a_m;
	int a_count;
	int a_reqpage;
	vm_ooffset_t a_offset;
};

struct vop_putpages_args {
	struct vop_generic_args a_head;
	struct vnode *a_vp;
	struct vm_page **a_m;
	int a_count;
	int a_sync;
	int *a_rtvals;
	vm_ooffset_t a_offset;
};

struct vop_freeblks_args {
	struct vop_generic_args a_head;
	struct vnode *a_vp;
	daddr_t a_addr;
	daddr_t a_length;
};

struct vop_bwrite_args {
	struct vop_generic_args a_head;
	struct vnode *a_vp;
	struct buf *a_bp;
};

struct vop_getacl_args {
	struct vop_generic_args a_head;
	struct vnode *a_vp;
	acl_type_t a_type;
	struct acl *a_aclp;
	struct ucred *a_cred;
	struct thread *a_td;
};

struct vop_setacl_args {
	struct vop_generic_args a_head;
	struct vnode *a_vp;
	acl_type_t a_type;
	struct acl *a_aclp;
	struct ucred *a_cred;
	struct thread *a_td;
};

struct vop_aclcheck_args {
	struct vop_generic_args a_head;
	struct vnode *a_vp;
	acl_type_t a_type;
	struct acl *a_aclp;
	struct ucred *a_cred;
	struct thread *a_td;
};

struct vop_getextattr_args {
	struct vop_generic_args a_head;
	struct vnode *a_vp;
	char *a_name;
	struct uio *a_uio;
	struct ucred *a_cred;
	struct thread *a_td;
};

struct vop_setextattr_args {
	struct vop_generic_args a_head;
	struct vnode *a_vp;
	char *a_name;
	struct uio *a_uio;
	struct ucred *a_cred;
	struct thread *a_td;
};

struct vop_createvobject_args {
	struct vop_generic_args a_head;
	struct vnode *a_vp;
	struct thread *a_td;
};

struct vop_destroyvobject_args {
	struct vop_generic_args a_head;
	struct vnode *a_vp;
};

struct vop_getvobject_args {
	struct vop_generic_args a_head;
	struct vnode *a_vp;
	struct vm_object **a_objpp;
};

struct vop_vfsset_args {
	struct vop_generic_args a_head;
	int a_op;
	const char *a_opstr;
};

/*
 * This structure is the post-compiled VOP operations vector.  vop_ops are
 * typically per-mount entities.  The first section is used by our vop_*()
 * function wrappers to implement hooks for per-mount management functions
 * such as journaling and cache coherency protocols.  The second section is
 * the function dispatch for the VFSs.  The functions are supposed to run
 * in the context of the VFS's thread (if it has one) and should not be 
 * directly called from random kernel code.  Note that VOCALL()s are direct
 * calls used for chaining vop_ops structures from a VFS context.
 */
struct vop_ops {
	struct vop_ops	*vv_new;	/* vfs_recalc_vnodeops() only */
	struct mount	*vv_mount;	/* may be NULL */
	int		vv_refs;
	int		vv_flags;	/* see VVF_* flags below */

	struct vop_ops	*vv_jops;	/* VVF_JOURNAL_HOOK */
	struct vop_ops	*vv_cops;	/* VVF_CCOHERENCY_HOOK */

	/* XXX Journaling control */
	/* XXX Cache Coherency functions (local/remote/clustered) */
	/* XXX Other */
	int		vv_reserved[62]; /* (temporary) reduce recompile pain */

#define vop_ops_first_field	vop_default
	int	(*vop_default)(struct vop_generic_args *);
	int	(*vop_islocked)(struct vop_islocked_args *);
	int	(*vop_resolve)(struct vop_resolve_args *);
	int	(*vop_lookup)(struct vop_lookup_args *);
	int	(*vop_cachedlookup)(struct vop_cachedlookup_args *);
	int	(*vop_create)(struct vop_create_args *);
	int	(*vop_whiteout)(struct vop_whiteout_args *);
	int	(*vop_mknod)(struct vop_mknod_args *);
	int	(*vop_open)(struct vop_open_args *);
	int	(*vop_close)(struct vop_close_args *);
	int	(*vop_access)(struct vop_access_args *);
	int	(*vop_getattr)(struct vop_getattr_args *);
	int	(*vop_setattr)(struct vop_setattr_args *);
	int	(*vop_read)(struct vop_read_args *);
	int	(*vop_write)(struct vop_write_args *);
	int	(*vop_lease)(struct vop_lease_args *);
	int	(*vop_ioctl)(struct vop_ioctl_args *);
	int	(*vop_poll)(struct vop_poll_args *);
	int	(*vop_kqfilter)(struct vop_kqfilter_args *);
	int	(*vop_revoke)(struct vop_revoke_args *);
	int	(*vop_mmap)(struct vop_mmap_args *);
	int	(*vop_fsync)(struct vop_fsync_args *);
	int	(*vop_remove)(struct vop_remove_args *);
	int	(*vop_link)(struct vop_link_args *);
	int	(*vop_rename)(struct vop_rename_args *);
	int	(*vop_mkdir)(struct vop_mkdir_args *);
	int	(*vop_rmdir)(struct vop_rmdir_args *);
	int	(*vop_symlink)(struct vop_symlink_args *);
	int	(*vop_readdir)(struct vop_readdir_args *);
	int	(*vop_readlink)(struct vop_readlink_args *);
	int	(*vop_inactive)(struct vop_inactive_args *);
	int	(*vop_reclaim)(struct vop_reclaim_args *);
	int	(*vop_lock)(struct vop_lock_args *);
	int	(*vop_unlock)(struct vop_unlock_args *);
	int	(*vop_bmap)(struct vop_bmap_args *);
	int	(*vop_strategy)(struct vop_strategy_args *);
	int	(*vop_print)(struct vop_print_args *);
	int	(*vop_pathconf)(struct vop_pathconf_args *);
	int	(*vop_advlock)(struct vop_advlock_args *);
	int	(*vop_balloc)(struct vop_balloc_args *);
	int	(*vop_reallocblks)(struct vop_reallocblks_args *);
	int	(*vop_getpages)(struct vop_getpages_args *);
	int	(*vop_putpages)(struct vop_putpages_args *);
	int	(*vop_freeblks)(struct vop_freeblks_args *);
	int	(*vop_bwrite)(struct vop_bwrite_args *);
	int	(*vop_getacl)(struct vop_getacl_args *);
	int	(*vop_setacl)(struct vop_setacl_args *);
	int	(*vop_aclcheck)(struct vop_aclcheck_args *);
	int	(*vop_getextattr)(struct vop_getextattr_args *);
	int	(*vop_setextattr)(struct vop_setextattr_args *);
	int	(*vop_createvobject)(struct vop_createvobject_args *);
	int	(*vop_destroyvobject)(struct vop_destroyvobject_args *);
	int	(*vop_getvobject)(struct vop_getvobject_args *);
	int	(*vop_vfsset)(struct vop_vfsset_args *);
#define vop_ops_last_field	vop_vfsset
};

#define VVF_JOURNAL_HOOK	0x0001		/* FUTURE */
#define VVF_CCOHERENCY_HOOK	0x0002		/* FUTURE */
#define VVF_UNUSED_04		0x0004
#define VVF_UNUSED_08		0x0008
#define VVF_NOATIME		0x0010		/* FUTURE */
#define VVF_RDONLY		0x0020		/* FUTURE */
#define VVF_NOCLUSTER		0x0040		/* FUTURE */
#define VVF_SUIDDIR		0x0080		/* FUTURE */

#define VVF_HOOK_MASK		(VVF_JOURNAL_HOOK|VVF_CCOHERENCY_HOOK)

/*
 * vop_vfsset() operations
 */
#define VFSSET_DETACH		0
#define VFSSET_ATTACH		1

/*
 * Kernel VOP arguments union, suitable for malloc / embedding in other
 * structures.  The vop_args_union can hold any VOP call argument structure.
 * Note that vu_head is broken out.
 */
union vop_args_union {
	struct vop_generic_args vu_head;
	struct vop_generic_args vu_default;
	struct vop_islocked_args vu_islocked;
	struct vop_resolve_args vu_resolve;
	struct vop_lookup_args vu_lookup;
	struct vop_cachedlookup_args vu_cachedlookup;
	struct vop_create_args vu_create;
	struct vop_whiteout_args vu_whiteout;
	struct vop_mknod_args vu_mknod;
	struct vop_open_args vu_open;
	struct vop_close_args vu_close;
	struct vop_access_args vu_access;
	struct vop_getattr_args vu_getattr;
	struct vop_setattr_args vu_setattr;
	struct vop_read_args vu_read;
	struct vop_write_args vu_write;
	struct vop_lease_args vu_lease;
	struct vop_ioctl_args vu_ioctl;
	struct vop_poll_args vu_poll;
	struct vop_kqfilter_args vu_kqfilter;
	struct vop_revoke_args vu_revoke;
	struct vop_mmap_args vu_mmap;
	struct vop_fsync_args vu_fsync;
	struct vop_remove_args vu_remove;
	struct vop_link_args vu_link;
	struct vop_rename_args vu_rename;
	struct vop_mkdir_args vu_mkdir;
	struct vop_rmdir_args vu_rmdir;
	struct vop_symlink_args vu_symlink;
	struct vop_readdir_args vu_readdir;
	struct vop_readlink_args vu_readlink;
	struct vop_inactive_args vu_inactive;
	struct vop_reclaim_args vu_reclaim;
	struct vop_lock_args vu_lock;
	struct vop_unlock_args vu_unlock;
	struct vop_bmap_args vu_bmap;
	struct vop_strategy_args vu_strategy;
	struct vop_print_args vu_print;
	struct vop_pathconf_args vu_pathconf;
	struct vop_advlock_args vu_advlock;
	struct vop_balloc_args vu_balloc;
	struct vop_reallocblks_args vu_reallocblks;
	struct vop_getpages_args vu_getpages;
	struct vop_putpages_args vu_putpages;
	struct vop_freeblks_args vu_freeblks;
	struct vop_bwrite_args vu_bwrite;
	struct vop_getacl_args vu_getacl;
	struct vop_setacl_args vu_setacl;
	struct vop_aclcheck_args vu_aclcheck;
	struct vop_getextattr_args vu_getextattr;
	struct vop_setextattr_args vu_setextattr;
	struct vop_createvobject_args vu_createvobject;
	struct vop_destroyvobject_args vu_destroyvobject;
	struct vop_getvobject_args vu_getvobject;
	struct vop_vfsset_args vu_vfsset;
};

#ifdef _KERNEL

/*
 * Kernel VOP call wrappers.  These wrappers are responsible for wrapping
 * the arguments in the appropriate VOP arguments structure, sending the
 * message, and waiting for a reply.  All kernel and VFS code should generally
 * call these wrappers rather then attempt to call the operations vector
 * routine directly in order to allow DragonFly to properly wrap the operation
 * in a message and dispatch it to the correct thread.
 */
int vop_islocked(struct vop_ops *ops, struct vnode *vp, struct thread *td);
int vop_resolve(struct vop_ops *ops, struct namecache *ncp);
int vop_lookup(struct vop_ops *ops, struct vnode *dvp, 
		struct vnode **vpp, struct componentname *cnp);
int vop_cachedlookup(struct vop_ops *ops, struct vnode *dvp, 
		struct vnode **vpp, struct componentname *cnp);
int vop_create(struct vop_ops *ops, struct vnode *dvp, struct namecache *par,
		struct vnode **vpp, struct componentname *cnp,
		struct vattr *vap);
int vop_whiteout(struct vop_ops *ops, struct vnode *dvp, struct namecache *par,
		struct componentname *cnp, int flags);
int vop_mknod(struct vop_ops *ops, struct vnode *dvp, struct namecache *par,
		struct vnode **vpp, struct componentname *cnp,
		struct vattr *vap);
int vop_open(struct vop_ops *ops, struct vnode *vp, int mode,
		struct ucred *cred, struct thread *td);
int vop_close(struct vop_ops *ops, struct vnode *vp,
		int fflag, struct thread *td);
int vop_access(struct vop_ops *ops, struct vnode *vp, int mode,
		struct ucred *cred, struct thread *td);
int vop_getattr(struct vop_ops *ops, struct vnode *vp, struct vattr *vap,
		struct thread *td);
int vop_setattr(struct vop_ops *ops, struct vnode *vp, struct vattr *vap,
		struct ucred *cred, struct thread *td);
int vop_read(struct vop_ops *ops, struct vnode *vp, struct uio *uio,
		int ioflag, struct ucred *cred);
int vop_write(struct vop_ops *ops, struct vnode *vp, struct uio *uio,
		int ioflag, struct ucred *cred);
int vop_lease(struct vop_ops *ops, struct vnode *vp, struct thread *td,
		struct ucred *cred, int flag);
int vop_ioctl(struct vop_ops *ops, struct vnode *vp, u_long command,
		caddr_t data, int fflag,
		struct ucred *cred, struct thread *td);
int vop_poll(struct vop_ops *ops, struct vnode *vp, int events,
		struct ucred *cred, struct thread *td);
int vop_kqfilter(struct vop_ops *ops, struct vnode *vp, struct knote *kn);
int vop_revoke(struct vop_ops *ops, struct vnode *vp, int flags);
int vop_mmap(struct vop_ops *ops, struct vnode *vp, int fflags,
		struct ucred *cred, struct thread *td);
int vop_fsync(struct vop_ops *ops, struct vnode *vp, int waitfor,
		struct thread *td);
int vop_remove(struct vop_ops *ops, struct vnode *dvp, struct namecache *par,
		struct vnode *vp, struct componentname *cnp);
int vop_link(struct vop_ops *ops, struct vnode *tdvp, struct namecache *par,
		struct vnode *vp, struct componentname *cnp);
int vop_rename(struct vop_ops *ops, struct vnode *fdvp, struct namecache *fpar,
		struct vnode *fvp, struct componentname *fcnp,
		struct vnode *tdvp, struct namecache *tpar,
		struct vnode *tvp, struct componentname *tcnp);
int vop_mkdir(struct vop_ops *ops, struct vnode *dvp, struct namecache *par,
		struct vnode **vpp, struct componentname *cnp,
		struct vattr *vap);
int vop_rmdir(struct vop_ops *ops, struct vnode *dvp, struct namecache *par,
		struct vnode *vp, struct componentname *cnp);
int vop_symlink(struct vop_ops *ops, struct vnode *dvp, struct namecache *par,
		struct vnode **vpp, struct componentname *cnp,
		struct vattr *vap, char *target);
int vop_readdir(struct vop_ops *ops, struct vnode *vp, struct uio *uio,
		struct ucred *cred, int *eofflag, 
		int *ncookies, u_long **cookies);
int vop_readlink(struct vop_ops *ops, struct vnode *vp, struct uio *uio,
		struct ucred *cred);
int vop_inactive(struct vop_ops *ops, struct vnode *vp, struct thread *td);
int vop_reclaim(struct vop_ops *ops, struct vnode *vp, struct thread *td);
int vop_lock(struct vop_ops *ops, struct vnode *vp,
		struct lwkt_tokref *vlock, int flags, struct thread *td);
int vop_unlock(struct vop_ops *ops, struct vnode *vp,
		struct lwkt_tokref *vlock, int flags, struct thread *td);
int vop_bmap(struct vop_ops *ops, struct vnode *vp, daddr_t bn,
		struct vnode **vpp, daddr_t *bnp, int *runp, int *runb);
int vop_strategy(struct vop_ops *ops, struct vnode *vp, struct buf *bp);
int vop_print(struct vop_ops *ops, struct vnode *vp);
int vop_pathconf(struct vop_ops *ops, struct vnode *vp, int name,
		register_t *retval);
int vop_advlock(struct vop_ops *ops, struct vnode *vp, caddr_t id, int op,
		struct flock *fl, int flags);
int vop_balloc(struct vop_ops *ops, struct vnode *vp, off_t startoffset,
		int size, struct ucred *cred, int flags,
		struct buf **bpp);
int vop_reallocblks(struct vop_ops *ops, struct vnode *vp,
		struct cluster_save *buflist);
int vop_getpages(struct vop_ops *ops, struct vnode *vp, struct vm_page **m,
		int count, int reqpage, vm_ooffset_t offset);
int vop_putpages(struct vop_ops *ops, struct vnode *vp, struct vm_page **m,
		int count, int sync, int *rtvals, vm_ooffset_t offset);
int vop_freeblks(struct vop_ops *ops, struct vnode *vp,
		daddr_t addr, daddr_t length);
int vop_bwrite(struct vop_ops *ops, struct vnode *vp, struct buf *bp);
int vop_getacl(struct vop_ops *ops, struct vnode *vp, acl_type_t type,
		struct acl *aclp, struct ucred *cred, struct thread *td);
int vop_setacl(struct vop_ops *ops, struct vnode *vp, acl_type_t type,
		struct acl *aclp, struct ucred *cred, struct thread *td);
int vop_aclcheck(struct vop_ops *ops, struct vnode *vp, acl_type_t type,
		struct acl *aclp, struct ucred *cred, struct thread *td);
int vop_getextattr(struct vop_ops *ops, struct vnode *vp, char *name, 
		struct uio *uio, struct ucred *cred, struct thread *td);
int vop_setextattr(struct vop_ops *ops, struct vnode *vp, char *name, 
		struct uio *uio, struct ucred *cred, struct thread *td);
int vop_createvobject(struct vop_ops *ops,
		struct vnode *vp, struct thread *td);
int vop_destroyvobject(struct vop_ops *ops, struct vnode *vp);
int vop_getvobject(struct vop_ops *ops,
		struct vnode *vp, struct vm_object **objpp);
int vop_vfsset(struct vop_ops *ops, int op, const char *opstr);

/*
 * Kernel VOP forwarding wrappers.  These are called when a VFS such as
 * nullfs or unionfs needs to push down into another VFS, changing the 
 * a_ops pointer and consequentially necessitating additional 
 * cache management.
 *
 * Note that this is different from vop_ops chaining within the same
 * filesystem.  When a filesystem chains a vop_ops it just uses VOCALLs.
 */
int vop_vnoperate_ap(struct vop_generic_args *ap);
int vop_islocked_ap(struct vop_islocked_args *ap);
int vop_resolve_ap(struct vop_resolve_args *ap);
int vop_lookup_ap(struct vop_lookup_args *ap);
int vop_cachedlookup_ap(struct vop_cachedlookup_args *ap);
int vop_create_ap(struct vop_create_args *ap);
int vop_whiteout_ap(struct vop_whiteout_args *ap);
int vop_mknod_ap(struct vop_mknod_args *ap);
int vop_open_ap(struct vop_open_args *ap);
int vop_close_ap(struct vop_close_args *ap);
int vop_access_ap(struct vop_access_args *ap);
int vop_getattr_ap(struct vop_getattr_args *ap);
int vop_setattr_ap(struct vop_setattr_args *ap);
int vop_read_ap(struct vop_read_args *ap);
int vop_write_ap(struct vop_write_args *ap);
int vop_lease_ap(struct vop_lease_args *ap);
int vop_ioctl_ap(struct vop_ioctl_args *ap);
int vop_poll_ap(struct vop_poll_args *ap);
int vop_kqfilter_ap(struct vop_kqfilter_args *ap);
int vop_revoke_ap(struct vop_revoke_args *ap);
int vop_mmap_ap(struct vop_mmap_args *ap);
int vop_fsync_ap(struct vop_fsync_args *ap);
int vop_remove_ap(struct vop_remove_args *ap);
int vop_link_ap(struct vop_link_args *ap);
int vop_rename_ap(struct vop_rename_args *ap);
int vop_mkdir_ap(struct vop_mkdir_args *ap);
int vop_rmdir_ap(struct vop_rmdir_args *ap);
int vop_symlink_ap(struct vop_symlink_args *ap);
int vop_readdir_ap(struct vop_readdir_args *ap);
int vop_readlink_ap(struct vop_readlink_args *ap);
int vop_inactive_ap(struct vop_inactive_args *ap);
int vop_reclaim_ap(struct vop_reclaim_args *ap);
int vop_lock_ap(struct vop_lock_args *ap);
int vop_unlock_ap(struct vop_unlock_args *ap);
int vop_bmap_ap(struct vop_bmap_args *ap);
int vop_strategy_ap(struct vop_strategy_args *ap);
int vop_print_ap(struct vop_print_args *ap);
int vop_pathconf_ap(struct vop_pathconf_args *ap);
int vop_advlock_ap(struct vop_advlock_args *ap);
int vop_balloc_ap(struct vop_balloc_args *ap);
int vop_reallocblks_ap(struct vop_reallocblks_args *ap);
int vop_getpages_ap(struct vop_getpages_args *ap);
int vop_putpages_ap(struct vop_putpages_args *ap);
int vop_freeblks_ap(struct vop_freeblks_args *ap);
int vop_bwrite_ap(struct vop_bwrite_args *ap);
int vop_getacl_ap(struct vop_getacl_args *ap);
int vop_setacl_ap(struct vop_setacl_args *ap);
int vop_aclcheck_ap(struct vop_aclcheck_args *ap);
int vop_getextattr_ap(struct vop_getextattr_args *ap);
int vop_setextattr_ap(struct vop_setextattr_args *ap);
int vop_createvobject_ap(struct vop_createvobject_args *ap);
int vop_destroyvobject_ap(struct vop_destroyvobject_args *ap);
int vop_getvobject_ap(struct vop_getvobject_args *ap);
int vop_vfsset_ap(struct vop_vfsset_args *ap);

/*
 * VOP operations descriptor.  This is used by the vop_ops compiler
 * to convert VFS vector arrays (typically in vfs/blah/blah_vnops.c)
 * into a vop_ops operations vector.
 */
extern struct vnodeop_desc vop_default_desc;
extern struct vnodeop_desc vop_islocked_desc;
extern struct vnodeop_desc vop_resolve_desc;
extern struct vnodeop_desc vop_lookup_desc;
extern struct vnodeop_desc vop_cachedlookup_desc;
extern struct vnodeop_desc vop_create_desc;
extern struct vnodeop_desc vop_whiteout_desc;
extern struct vnodeop_desc vop_mknod_desc;
extern struct vnodeop_desc vop_open_desc;
extern struct vnodeop_desc vop_close_desc;
extern struct vnodeop_desc vop_access_desc;
extern struct vnodeop_desc vop_getattr_desc;
extern struct vnodeop_desc vop_setattr_desc;
extern struct vnodeop_desc vop_read_desc;
extern struct vnodeop_desc vop_write_desc;
extern struct vnodeop_desc vop_lease_desc;
extern struct vnodeop_desc vop_ioctl_desc;
extern struct vnodeop_desc vop_poll_desc;
extern struct vnodeop_desc vop_kqfilter_desc;
extern struct vnodeop_desc vop_revoke_desc;
extern struct vnodeop_desc vop_mmap_desc;
extern struct vnodeop_desc vop_fsync_desc;
extern struct vnodeop_desc vop_remove_desc;
extern struct vnodeop_desc vop_link_desc;
extern struct vnodeop_desc vop_rename_desc;
extern struct vnodeop_desc vop_mkdir_desc;
extern struct vnodeop_desc vop_rmdir_desc;
extern struct vnodeop_desc vop_symlink_desc;
extern struct vnodeop_desc vop_readdir_desc;
extern struct vnodeop_desc vop_readlink_desc;
extern struct vnodeop_desc vop_inactive_desc;
extern struct vnodeop_desc vop_reclaim_desc;
extern struct vnodeop_desc vop_lock_desc;
extern struct vnodeop_desc vop_unlock_desc;
extern struct vnodeop_desc vop_bmap_desc;
extern struct vnodeop_desc vop_strategy_desc;
extern struct vnodeop_desc vop_print_desc;
extern struct vnodeop_desc vop_pathconf_desc;
extern struct vnodeop_desc vop_advlock_desc;
extern struct vnodeop_desc vop_balloc_desc;
extern struct vnodeop_desc vop_reallocblks_desc;
extern struct vnodeop_desc vop_getpages_desc;
extern struct vnodeop_desc vop_putpages_desc;
extern struct vnodeop_desc vop_freeblks_desc;
extern struct vnodeop_desc vop_bwrite_desc;
extern struct vnodeop_desc vop_getacl_desc;
extern struct vnodeop_desc vop_setacl_desc;
extern struct vnodeop_desc vop_aclcheck_desc;
extern struct vnodeop_desc vop_getextattr_desc;
extern struct vnodeop_desc vop_setextattr_desc;
extern struct vnodeop_desc vop_createvobject_desc;
extern struct vnodeop_desc vop_destroyvobject_desc;
extern struct vnodeop_desc vop_getvobject_desc;
extern struct vnodeop_desc vop_vfsset_desc;

#endif

/*
 * VOP_ macro compatibility.  Remove these as we get rid of the VOP macros.
 * We have to convert the VOP macros into the vop_*() version that requires
 * the appropriate v_ops structure pointer for the first arg.
 */
#define VOP_ISLOCKED(vp, td)				\
	vop_islocked((vp)->v_ops, vp, td)
#define VOP_LOOKUP(dvp, vpp, cnp)			\
	vop_lookup((dvp)->v_ops, dvp, vpp, cnp)
#define VOP_CACHEDLOOKUP(dvp, vpp, cnp)	\
	vop_cachedlookup((dvp)->v_ops, dvp, vpp, cnp)
#define VOP_CREATE(dvp, par, vpp, cnp, vap)		\
	vop_create((dvp)->v_ops, dvp, par, vpp, cnp, vap)
#define VOP_WHITEOUT(dvp, par, cnp, flags)		\
	vop_whiteout((dvp)->v_ops, dvp, par, cnp, flags)
#define VOP_MKNOD(dvp, par, vpp, cnp, vap)		\
	vop_mknod((dvp)->v_ops, dvp, par, vpp, cnp, vap)
#define VOP_OPEN(vp, mode, cred, td)			\
	vop_open((vp)->v_ops, vp, mode, cred, td)
#define VOP_CLOSE(vp, fflag, td)			\
	vop_close((vp)->v_ops, vp, fflag, td)
#define VOP_ACCESS(vp, mode, cred, td)			\
	vop_access((vp)->v_ops, vp, mode, cred, td)
#define VOP_GETATTR(vp, vap, td)			\
	vop_getattr((vp)->v_ops, vp, vap, td)
#define VOP_SETATTR(vp, vap, cred, td)			\
	vop_setattr((vp)->v_ops, vp, vap, cred, td)
#define VOP_READ(vp, uio, ioflag, cred)			\
	vop_read((vp)->v_ops, vp, uio, ioflag, cred)
#define VOP_WRITE(vp, uio, ioflag, cred)		\
	vop_write((vp)->v_ops, vp, uio, ioflag, cred)
#define VOP_LEASE(vp, td, cred, flag)			\
	vop_lease((vp)->v_ops, vp, td, cred, flag)
#define VOP_IOCTL(vp, command, data, fflag, cred, td)	\
	vop_ioctl((vp)->v_ops, vp, command, data, fflag, cred, td)
#define VOP_POLL(vp, events, cred, td)			\
	vop_poll((vp)->v_ops, vp, events, cred, td)
#define VOP_KQFILTER(vp, kn)				\
	vop_kqfilter((vp)->v_ops, vp, kn)
#define VOP_REVOKE(vp, flags)				\
	vop_revoke((vp)->v_ops, vp, flags)
#define VOP_MMAP(vp, fflags, cred, td)			\
	vop_mmap((vp)->v_ops, vp, fflags, cred, td)
#define VOP_FSYNC(vp, waitfor, td)			\
	vop_fsync((vp)->v_ops, vp, waitfor, td)
#define VOP_REMOVE(dvp, par, vp, cnp)			\
	vop_remove((dvp)->v_ops, dvp, par, vp, cnp)
#define VOP_LINK(tdvp, par, vp, cnp)			\
	vop_link((tdvp)->v_ops, tdvp, par, vp, cnp)
#define VOP_RENAME(fdvp, fpar, fvp, fcnp, tdvp, tpar, tvp, tcnp)	\
	vop_rename((fdvp)->v_ops, fdvp, fpar, fvp, fcnp, tdvp, tpar, tvp, tcnp)
#define VOP_MKDIR(dvp, par, vpp, cnp, vap)		\
	vop_mkdir((dvp)->v_ops, dvp, par, vpp, cnp, vap)
#define VOP_RMDIR(dvp, par, vp, cnp)			\
	vop_rmdir((dvp)->v_ops, dvp, par, vp, cnp)
#define VOP_SYMLINK(dvp, par, vpp, cnp, vap, target)	\
	vop_symlink((dvp)->v_ops, dvp, par, vpp, cnp, vap, target)
#define VOP_READDIR(vp, uio, cred, eofflag, ncookies, cookies)		\
	vop_readdir((vp)->v_ops, vp, uio, cred, eofflag, ncookies, cookies)
#define VOP_READLINK(vp, uio, cred)			\
	vop_readlink((vp)->v_ops, vp, uio, cred)
#define VOP_INACTIVE(vp, td)				\
	vop_inactive((vp)->v_ops, vp, td)
#define VOP_RECLAIM(vp, td)				\
	vop_reclaim((vp)->v_ops, vp, td)
#define VOP_LOCK(vp, vlock, flags, td)			\
	vop_lock((vp)->v_ops, vp, vlock, flags, td)
#define VOP_UNLOCK(vp, vlock, flags, td)		\
	vop_unlock((vp)->v_ops, vp, vlock, flags, td)
#define VOP_BMAP(vp, bn, vpp, bnp, runp, runb)		\
	vop_bmap((vp)->v_ops, vp, bn, vpp, bnp, runp, runb)
#define VOP_STRATEGY(vp, bp)				\
	vop_strategy((vp)->v_ops, vp, bp)
#define VOP_PRINT(vp)					\
	vop_print((vp)->v_ops, vp)
#define VOP_PATHCONF(vp, name, retval)			\
	vop_pathconf((vp)->v_ops, vp, name, retval)
#define VOP_ADVLOCK(vp, id, op, fl, flags)		\
	vop_advlock((vp)->v_ops, vp, id, op, fl, flags)
#define VOP_BALLOC(vp, offset, size, cred, flags, bpp)	\
	vop_balloc((vp)->v_ops, vp, offset, size, cred, flags, bpp)
#define VOP_REALLOCBLKS(vp, buflist)			\
	vop_reallocblks((vp)->v_ops, vp, buflist)
#define VOP_GETPAGES(vp, m, count, reqpage, off)	\
	vop_getpages((vp)->v_ops, vp, m, count, reqpage, off)
#define VOP_PUTPAGES(vp, m, count, sync, rtvals, off)	\
	vop_putpages((vp)->v_ops, vp, m, count, sync, rtvals, off)
#define VOP_FREEBLKS(vp, addr, length)			\
	vop_freeblks((vp)->v_ops, vp, addr, length)
#define VOP_BWRITE(vp, bp)				\
	vop_bwrite((vp)->v_ops, vp, bp)
#define VOP_GETACL(vp, type, aclp, cred, td)		\
	vop_getacl((vp)->v_ops, vp, type, aclp, cred, td)
#define VOP_SETACL(vp, type, aclp, cred, td)		\
	vop_setacl((vp)->v_ops, vp, type, aclp, cred, td)
#define VOP_ACLCHECK(vp, type, aclp, cred, td)		\
	vop_aclcheck((vp)->v_ops, vp, type, aclp, cred, td)
#define VOP_GETEXTATTR(vp, name, uio, cred, td)		\
	vop_getextattr((vp)->v_ops, vp, name, uio, cred, td)
#define VOP_SETEXTATTR(vp, name, uio, cred, td)		\
	vop_setextattr((vp)->v_ops, vp, name, uio, cred, td)
#define VOP_CREATEVOBJECT(vp, td)			\
	vop_createvobject((vp)->v_ops, vp, td)
#define VOP_DESTROYVOBJECT(vp)				\
	vop_destroyvobject((vp)->v_ops, vp)
#define VOP_GETVOBJECT(vp, objpp)			\
	vop_getvobject((vp)->v_ops, vp, objpp)
/* no VOP_VFSSET() */

#endif

