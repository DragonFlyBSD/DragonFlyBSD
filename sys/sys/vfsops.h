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
 * $DragonFly: src/sys/sys/vfsops.h,v 1.32 2008/06/19 23:27:36 dillon Exp $
 */

/*
 * The vop_ops structure vectors all access to a filesystem.  It contains a
 * fixed set of vectors.
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
#ifndef _SYS_BUF_H_
#include <sys/buf.h>	/* buf_cmd_t */
#endif
#ifndef _SYS_FCNTL_H_
#include <sys/fcntl.h>	/* AT_EACCESS */
#endif

struct syslink_desc;
struct vnode;
struct thread;
struct nchandle;
struct componentname;
struct vattr;
struct ucred;
struct uio;
struct file;
struct knote;
struct vm_object;
struct vm_page;
struct vfscache;

struct vop_generic_args {
	struct syslink_desc *a_desc;	/* command descriptor for the call */
	struct vop_ops *a_ops;		/* operations vector for the call */
	int a_reserved[4];
};

struct vop_old_lookup_args {
	struct vop_generic_args a_head;
	struct vnode *a_dvp;
	struct vnode **a_vpp;
	struct componentname *a_cnp;
};

struct vop_old_create_args {
	struct vop_generic_args a_head;
	struct vnode *a_dvp;
	struct vnode **a_vpp;
	struct componentname *a_cnp;
	struct vattr *a_vap;
};

struct vop_old_whiteout_args {
	struct vop_generic_args a_head;
	struct vnode *a_dvp;
	struct componentname *a_cnp;
	int a_flags;
};

struct vop_old_mknod_args {
	struct vop_generic_args a_head;
	struct vnode *a_dvp;
	struct vnode **a_vpp;
	struct componentname *a_cnp;
	struct vattr *a_vap;
};

struct vop_open_args {
	struct vop_generic_args a_head;
	struct vnode *a_vp;
	int a_mode;
	struct ucred *a_cred;
	struct file *a_fp;		/* optional fp for fileops override */
};

struct vop_close_args {
	struct vop_generic_args a_head;
	struct vnode *a_vp;
	int a_fflag;
	struct file *a_fp;		/* optional fp for fileops override */
};

struct vop_access_args {
	struct vop_generic_args a_head;
	struct vnode *a_vp;
	int a_mode;				/* V* bitmask */
	int a_flags;				/* AT_* bitmask */
	struct ucred *a_cred;
};

struct vop_getattr_args {
	struct vop_generic_args a_head;
	struct vnode *a_vp;
	struct vattr *a_vap;
};

struct vop_setattr_args {
	struct vop_generic_args a_head;
	struct vnode *a_vp;
	struct vattr *a_vap;
	struct ucred *a_cred;
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

struct vop_ioctl_args {
	struct vop_generic_args a_head;
	struct vnode *a_vp;
	u_long a_command;
	caddr_t a_data;
	int a_fflag;
	struct ucred *a_cred;
	struct sysmsg *a_sysmsg;
};

struct vop_poll_args {
	struct vop_generic_args a_head;
	struct vnode *a_vp;
	int a_events;
	struct ucred *a_cred;
};

struct vop_kqfilter_args {
	struct vop_generic_args a_head;
	struct vnode *a_vp;
	struct knote *a_kn;
};

struct vop_mmap_args {
	struct vop_generic_args a_head;
	struct vnode *a_vp;
	int a_fflags;
	struct ucred *a_cred;
};

struct vop_fsync_args {
	struct vop_generic_args a_head;
	struct vnode *a_vp;
	int a_waitfor;
	int a_flags;
};

struct vop_old_remove_args {
	struct vop_generic_args a_head;
	struct vnode *a_dvp;
	struct vnode *a_vp;
	struct componentname *a_cnp;
};

struct vop_old_link_args {
	struct vop_generic_args a_head;
	struct vnode *a_tdvp;
	struct vnode *a_vp;
	struct componentname *a_cnp;
};

struct vop_old_rename_args {
	struct vop_generic_args a_head;
	struct vnode *a_fdvp;
	struct vnode *a_fvp;
	struct componentname *a_fcnp;
	struct vnode *a_tdvp;
	struct vnode *a_tvp;
	struct componentname *a_tcnp;
};

struct vop_old_mkdir_args {
	struct vop_generic_args a_head;
	struct vnode *a_dvp;
	struct vnode **a_vpp;
	struct componentname *a_cnp;
	struct vattr *a_vap;
};

struct vop_old_rmdir_args {
	struct vop_generic_args a_head;
	struct vnode *a_dvp;
	struct vnode *a_vp;
	struct componentname *a_cnp;
};

struct vop_old_symlink_args {
	struct vop_generic_args a_head;
	struct vnode *a_dvp;
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
	off_t **a_cookies;
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
};

struct vop_reclaim_args {
	struct vop_generic_args a_head;
	struct vnode *a_vp;
};

struct vop_bmap_args {
	struct vop_generic_args a_head;
	struct vnode *a_vp;
	off_t a_loffset;
	off_t *a_doffsetp;
	int *a_runp;
	int *a_runb;
	buf_cmd_t a_cmd;	/* BUF_CMD_READ, BUF_CMD_WRITE, etc */
};

struct vop_strategy_args {
	struct vop_generic_args a_head;
	struct vnode *a_vp;
	struct bio *a_bio;
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
	int a_seqaccess;
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
	off_t a_offset;
	int a_length;
};

struct vop_getacl_args {
	struct vop_generic_args a_head;
	struct vnode *a_vp;
	acl_type_t a_type;
	struct acl *a_aclp;
	struct ucred *a_cred;
};

struct vop_setacl_args {
	struct vop_generic_args a_head;
	struct vnode *a_vp;
	acl_type_t a_type;
	struct acl *a_aclp;
	struct ucred *a_cred;
};

struct vop_aclcheck_args {
	struct vop_generic_args a_head;
	struct vnode *a_vp;
	acl_type_t a_type;
	struct acl *a_aclp;
	struct ucred *a_cred;
};

struct vop_getextattr_args {
	struct vop_generic_args a_head;
	struct vnode *a_vp;
	int a_attrnamespace;
	char *a_attrname;
	struct uio *a_uio;
	struct ucred *a_cred;
};

struct vop_setextattr_args {
	struct vop_generic_args a_head;
	struct vnode *a_vp;
	int a_attrnamespace;
	char *a_attrname;
	struct uio *a_uio;
	struct ucred *a_cred;
};

struct vop_mountctl_args {
	struct vop_generic_args a_head;
	int a_op;
	struct file *a_fp;
	const void *a_ctl;
	int a_ctllen;
	void *a_buf;
	int a_buflen;
	int *a_res;
	struct vnode *a_vp;
};

struct vop_markatime_args {
	struct vop_generic_args a_head;
	int a_op;
	struct vnode *a_vp;
	struct ucred *a_cred;
};

/*
 * NEW API
 */

/*
 * Warning: a_dvp is only held, not ref'd.  The target must still vget()
 * it.
 */
struct vop_nresolve_args {
	struct vop_generic_args a_head;
	struct nchandle *a_nch;
	struct vnode *a_dvp;
	struct ucred *a_cred;
};

struct vop_nlookupdotdot_args {
	struct vop_generic_args a_head;
	struct vnode *a_dvp;
	struct vnode **a_vpp;
	struct ucred *a_cred;
	char **a_fakename;
};

/*
 * Warning: a_dvp is only held, not ref'd.  The target must still vget()
 * it.
 */
struct vop_ncreate_args {
	struct vop_generic_args a_head;
	struct nchandle *a_nch;		/* locked namespace */
	struct vnode *a_dvp;		/* held directory vnode */
	struct vnode **a_vpp;		/* returned refd & locked */
	struct ucred *a_cred;
	struct vattr *a_vap;
};

/*
 * Warning: a_dvp is only held, not ref'd.  The target must still vget()
 * it.
 */
struct vop_nmkdir_args {
	struct vop_generic_args a_head;
	struct nchandle *a_nch;		/* locked namespace */
	struct vnode *a_dvp;
	struct vnode **a_vpp;			/* returned refd & locked */
	struct ucred *a_cred;
	struct vattr *a_vap;
};

/*
 * Warning: a_dvp is only held, not ref'd.  The target must still vget()
 * it.
 */
struct vop_nmknod_args {
	struct vop_generic_args a_head;
	struct nchandle *a_nch;
	struct vnode *a_dvp;
	struct vnode **a_vpp;
	struct ucred *a_cred;
	struct vattr *a_vap;
};

/*
 * Warning: a_dvp is only held, not ref'd.  The target must still vget()
 * it.
 */
struct vop_nlink_args {
	struct vop_generic_args a_head;
	struct nchandle *a_nch;
	struct vnode *a_dvp;
	struct vnode *a_vp;
	struct ucred *a_cred;
};

/*
 * Warning: a_dvp is only held, not ref'd.  The target must still vget()
 * it.
 */
struct vop_nsymlink_args {
	struct vop_generic_args a_head;
	struct nchandle *a_nch;
	struct vnode *a_dvp;
	struct vnode **a_vpp;
	struct ucred *a_cred;
	struct vattr *a_vap;
	char *a_target;
};

/*
 * Warning: a_dvp is only held, not ref'd.  The target must still vget()
 * it.
 */
struct vop_nwhiteout_args {
	struct vop_generic_args a_head;
	struct nchandle *a_nch;
	struct vnode *a_dvp;
	struct ucred *a_cred;
	int a_flags;
};

/*
 * Warning: a_dvp is only held, not ref'd.  The target must still vget()
 * it.
 */
struct vop_nremove_args {
	struct vop_generic_args a_head;
	struct nchandle *a_nch;		/* locked namespace */
	struct vnode *a_dvp;
	struct ucred *a_cred;
};

/*
 * Warning: a_dvp is only held, not ref'd.  The target must still vget()
 * it.
 */
struct vop_nrmdir_args {
	struct vop_generic_args a_head;
	struct nchandle *a_nch;		/* locked namespace */
	struct vnode *a_dvp;
	struct ucred *a_cred;
};

/*
 * Warning: a_fdvp and a_tdvp are only held, not ref'd.  The target must
 * still vget() it.
 */
struct vop_nrename_args {
	struct vop_generic_args a_head;
	struct nchandle *a_fnch;		/* locked namespace / from */
	struct nchandle *a_tnch;		/* locked namespace / to */
	struct vnode *a_fdvp;
	struct vnode *a_tdvp;
	struct ucred *a_cred;
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
	struct {
		struct mount	*vv_mount;
	} head;

#define vop_ops_first_field	vop_default
	int	(*vop_default)(struct vop_generic_args *);
	int	(*vop_unused11)(void *);
	int	(*vop_old_lookup)(struct vop_old_lookup_args *);
	int	(*vop_unused03)(void *);
	int	(*vop_old_create)(struct vop_old_create_args *);
	int	(*vop_old_whiteout)(struct vop_old_whiteout_args *);
	int	(*vop_old_mknod)(struct vop_old_mknod_args *);
	int	(*vop_open)(struct vop_open_args *);
	int	(*vop_close)(struct vop_close_args *);
	int	(*vop_access)(struct vop_access_args *);
	int	(*vop_getattr)(struct vop_getattr_args *);
	int	(*vop_setattr)(struct vop_setattr_args *);
	int	(*vop_read)(struct vop_read_args *);
	int	(*vop_write)(struct vop_write_args *);
	int	(*vop_unused04)(void *);
	int	(*vop_ioctl)(struct vop_ioctl_args *);
	int	(*vop_poll)(struct vop_poll_args *);
	int	(*vop_kqfilter)(struct vop_kqfilter_args *);
	int	(*vop_unused01)(void *);	/* was vop_revoke */
	int	(*vop_mmap)(struct vop_mmap_args *);
	int	(*vop_fsync)(struct vop_fsync_args *);
	int	(*vop_old_remove)(struct vop_old_remove_args *);
	int	(*vop_old_link)(struct vop_old_link_args *);
	int	(*vop_old_rename)(struct vop_old_rename_args *);
	int	(*vop_old_mkdir)(struct vop_old_mkdir_args *);
	int	(*vop_old_rmdir)(struct vop_old_rmdir_args *);
	int	(*vop_old_symlink)(struct vop_old_symlink_args *);
	int	(*vop_readdir)(struct vop_readdir_args *);
	int	(*vop_readlink)(struct vop_readlink_args *);
	int	(*vop_inactive)(struct vop_inactive_args *);
	int	(*vop_reclaim)(struct vop_reclaim_args *);
	int	(*vop_unused09)(void *);
	int	(*vop_unused10)(void *);
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
	int	(*vop_unused05)(void *);
	int	(*vop_getacl)(struct vop_getacl_args *);
	int	(*vop_setacl)(struct vop_setacl_args *);
	int	(*vop_aclcheck)(struct vop_aclcheck_args *);
	int	(*vop_getextattr)(struct vop_getextattr_args *);
	int	(*vop_setextattr)(struct vop_setextattr_args *);
	int	(*vop_unused06)(void *);
	int	(*vop_unused07)(void *);
	int	(*vop_unused08)(void *);
	int	(*vop_mountctl)(struct vop_mountctl_args *);
	int	(*vop_markatime)(struct vop_markatime_args *);

	int	(*vop_nresolve)(struct vop_nresolve_args *);
	int	(*vop_nlookupdotdot)(struct vop_nlookupdotdot_args *);
	int	(*vop_ncreate)(struct vop_ncreate_args *);
	int	(*vop_nmkdir)(struct vop_nmkdir_args *);
	int	(*vop_nmknod)(struct vop_nmknod_args *);
	int	(*vop_nlink)(struct vop_nlink_args *);
	int	(*vop_nsymlink)(struct vop_nsymlink_args *);
	int	(*vop_nwhiteout)(struct vop_nwhiteout_args *);
	int	(*vop_nremove)(struct vop_nremove_args *);
	int	(*vop_nrmdir)(struct vop_nrmdir_args *);
	int	(*vop_nrename)(struct vop_nrename_args *);
#define vop_ops_last_field	vop_nrename
};

/*
 * vop_mountctl() operations
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
	struct vop_old_lookup_args vu_lookup;
	struct vop_old_create_args vu_create;
	struct vop_old_whiteout_args vu_whiteout;
	struct vop_old_mknod_args vu_mknod;
	struct vop_open_args vu_open;
	struct vop_close_args vu_close;
	struct vop_access_args vu_access;
	struct vop_getattr_args vu_getattr;
	struct vop_setattr_args vu_setattr;
	struct vop_read_args vu_read;
	struct vop_write_args vu_write;
	struct vop_ioctl_args vu_ioctl;
	struct vop_poll_args vu_poll;
	struct vop_kqfilter_args vu_kqfilter;
	struct vop_mmap_args vu_mmap;
	struct vop_fsync_args vu_fsync;
	struct vop_old_remove_args vu_remove;
	struct vop_old_link_args vu_link;
	struct vop_old_rename_args vu_rename;
	struct vop_old_mkdir_args vu_mkdir;
	struct vop_old_rmdir_args vu_rmdir;
	struct vop_old_symlink_args vu_symlink;
	struct vop_readdir_args vu_readdir;
	struct vop_readlink_args vu_readlink;
	struct vop_inactive_args vu_inactive;
	struct vop_reclaim_args vu_reclaim;
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
	struct vop_getacl_args vu_getacl;
	struct vop_setacl_args vu_setacl;
	struct vop_aclcheck_args vu_aclcheck;
	struct vop_getextattr_args vu_getextattr;
	struct vop_setextattr_args vu_setextattr;
	struct vop_mountctl_args vu_mountctl;
	struct vop_markatime_args vu_markatime;

	struct vop_nresolve_args vu_nresolve;
	struct vop_nlookupdotdot_args vu_nlookupdotdot;
	struct vop_ncreate_args vu_ncreate;
	struct vop_nmkdir_args vu_nmkdir;
	struct vop_nmknod_args vu_nmknod;
	struct vop_nlink_args vu_nlink;
	struct vop_nsymlink_args vu_nsymlink;
	struct vop_nwhiteout_args vu_nwhiteout;
	struct vop_nremove_args vu_nremove;
	struct vop_nrmdir_args vu_nrmdir;
	struct vop_nrename_args vu_nrename;
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
int vop_old_lookup(struct vop_ops *ops, struct vnode *dvp, 
		struct vnode **vpp, struct componentname *cnp);
int vop_old_create(struct vop_ops *ops, struct vnode *dvp,
		struct vnode **vpp, struct componentname *cnp,
		struct vattr *vap);
int vop_old_whiteout(struct vop_ops *ops, struct vnode *dvp, 
		struct componentname *cnp, int flags);
int vop_old_mknod(struct vop_ops *ops, struct vnode *dvp, 
		struct vnode **vpp, struct componentname *cnp,
		struct vattr *vap);
int vop_open(struct vop_ops *ops, struct vnode *vp, int mode,
		struct ucred *cred, struct file *file);
int vop_close(struct vop_ops *ops, struct vnode *vp, int fflag,
		struct file *file);
int vop_access(struct vop_ops *ops, struct vnode *vp, int mode, int flags,
		struct ucred *cred);
int vop_getattr(struct vop_ops *ops, struct vnode *vp, struct vattr *vap);
int vop_setattr(struct vop_ops *ops, struct vnode *vp, struct vattr *vap,
		struct ucred *cred);
int vop_read(struct vop_ops *ops, struct vnode *vp, struct uio *uio,
		int ioflag, struct ucred *cred);
int vop_write(struct vop_ops *ops, struct vnode *vp, struct uio *uio,
		int ioflag, struct ucred *cred);
int vop_ioctl(struct vop_ops *ops, struct vnode *vp, u_long command,
		caddr_t data, int fflag, struct ucred *cred,
		struct sysmsg *msg);
int vop_poll(struct vop_ops *ops, struct vnode *vp, int events,
		struct ucred *cred);
int vop_kqfilter(struct vop_ops *ops, struct vnode *vp, struct knote *kn);
int vop_mmap(struct vop_ops *ops, struct vnode *vp, int fflags,
		struct ucred *cred);
int vop_fsync(struct vop_ops *ops, struct vnode *vp, int waitfor, int flags);
int vop_old_remove(struct vop_ops *ops, struct vnode *dvp,
		struct vnode *vp, struct componentname *cnp);
int vop_old_link(struct vop_ops *ops, struct vnode *tdvp,
		struct vnode *vp, struct componentname *cnp);
int vop_old_rename(struct vop_ops *ops, struct vnode *fdvp,
		struct vnode *fvp, struct componentname *fcnp,
		struct vnode *tdvp, struct vnode *tvp,
		struct componentname *tcnp);
int vop_old_mkdir(struct vop_ops *ops, struct vnode *dvp,
		struct vnode **vpp, struct componentname *cnp,
		struct vattr *vap);
int vop_old_rmdir(struct vop_ops *ops, struct vnode *dvp,
		struct vnode *vp, struct componentname *cnp);
int vop_old_symlink(struct vop_ops *ops, struct vnode *dvp,
		struct vnode **vpp, struct componentname *cnp,
		struct vattr *vap, char *target);
int vop_readdir(struct vop_ops *ops, struct vnode *vp, struct uio *uio,
		struct ucred *cred, int *eofflag, 
		int *ncookies, off_t **cookies);
int vop_readlink(struct vop_ops *ops, struct vnode *vp, struct uio *uio,
		struct ucred *cred);
int vop_inactive(struct vop_ops *ops, struct vnode *vp);
int vop_reclaim(struct vop_ops *ops, struct vnode *vp);
int vop_bmap(struct vop_ops *ops, struct vnode *vp, off_t loffset,
		off_t *doffsetp, int *runp, int *runb, buf_cmd_t cmd);
int vop_strategy(struct vop_ops *ops, struct vnode *vp, struct bio *bio);
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
		int count, int reqpage, vm_ooffset_t offset, int seqaccess);
int vop_putpages(struct vop_ops *ops, struct vnode *vp, struct vm_page **m,
		int count, int sync, int *rtvals, vm_ooffset_t offset);
int vop_freeblks(struct vop_ops *ops, struct vnode *vp,
		off_t offset, int length);
int vop_getacl(struct vop_ops *ops, struct vnode *vp, acl_type_t type,
		struct acl *aclp, struct ucred *cred);
int vop_setacl(struct vop_ops *ops, struct vnode *vp, acl_type_t type,
		struct acl *aclp, struct ucred *cred);
int vop_aclcheck(struct vop_ops *ops, struct vnode *vp, acl_type_t type,
		struct acl *aclp, struct ucred *cred);

int vop_getextattr(struct vop_ops *ops, struct vnode *vp, int attrnamespace,
		char *attrname, struct uio *uio, struct ucred *cred);
int vop_setextattr(struct vop_ops *ops, struct vnode *vp, int attrnamespace,
		char *attrname, struct uio *uio, struct ucred *cred);
int vop_mountctl(struct vop_ops *ops, struct vnode *vp, int op,
		struct file *fp, const void *ctl, int ctllen,
		void *buf, int buflen, int *res);
int vop_markatime(struct vop_ops *ops, struct vnode *vp, struct ucred *cred);
int vop_nresolve(struct vop_ops *ops, struct nchandle *nch,
		struct vnode *dvp, struct ucred *cred);
int vop_nlookupdotdot(struct vop_ops *ops, struct vnode *dvp,
		struct vnode **vpp, struct ucred *cred, char **fakename);
int vop_ncreate(struct vop_ops *ops, struct nchandle *nch, struct vnode *dvp,
		struct vnode **vpp, struct ucred *cred, struct vattr *vap);
int vop_nmkdir(struct vop_ops *ops, struct nchandle *nch, struct vnode *dvp,
		struct vnode **vpp, struct ucred *cred, struct vattr *vap);
int vop_nmknod(struct vop_ops *ops, struct nchandle *nch, struct vnode *dvp,
		struct vnode **vpp, struct ucred *cred, struct vattr *vap);
int vop_nlink(struct vop_ops *ops, struct nchandle *nch, struct vnode *dvp,
		struct vnode *vp, struct ucred *cred);
int vop_nsymlink(struct vop_ops *ops, struct nchandle *nch, struct vnode *dvp,
		struct vnode **vpp, struct ucred *cred,
		struct vattr *vap, char *target);
int vop_nwhiteout(struct vop_ops *ops, struct nchandle *nch, struct vnode *dvp,
		struct ucred *cred, int flags);
int vop_nremove(struct vop_ops *ops, struct nchandle *nch, struct vnode *dvp,
		struct ucred *cred);
int vop_nrmdir(struct vop_ops *ops, struct nchandle *nch, struct vnode *dvp,
		struct ucred *cred);
int vop_nrename(struct vop_ops *ops,
		struct nchandle *fnch, struct nchandle *tnch,
		struct vnode *fdvp, struct vnode *tdvp,
		struct ucred *cred);

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
int vop_cache_operate_ap(struct vop_generic_args *ap);
int vop_journal_operate_ap(struct vop_generic_args *ap);
int vop_old_lookup_ap(struct vop_old_lookup_args *ap);
int vop_old_create_ap(struct vop_old_create_args *ap);
int vop_old_whiteout_ap(struct vop_old_whiteout_args *ap);
int vop_old_mknod_ap(struct vop_old_mknod_args *ap);
int vop_open_ap(struct vop_open_args *ap);
int vop_close_ap(struct vop_close_args *ap);
int vop_access_ap(struct vop_access_args *ap);
int vop_getattr_ap(struct vop_getattr_args *ap);
int vop_setattr_ap(struct vop_setattr_args *ap);
int vop_read_ap(struct vop_read_args *ap);
int vop_write_ap(struct vop_write_args *ap);
int vop_ioctl_ap(struct vop_ioctl_args *ap);
int vop_poll_ap(struct vop_poll_args *ap);
int vop_kqfilter_ap(struct vop_kqfilter_args *ap);
int vop_mmap_ap(struct vop_mmap_args *ap);
int vop_fsync_ap(struct vop_fsync_args *ap);
int vop_old_remove_ap(struct vop_old_remove_args *ap);
int vop_old_link_ap(struct vop_old_link_args *ap);
int vop_old_rename_ap(struct vop_old_rename_args *ap);
int vop_old_mkdir_ap(struct vop_old_mkdir_args *ap);
int vop_old_rmdir_ap(struct vop_old_rmdir_args *ap);
int vop_old_symlink_ap(struct vop_old_symlink_args *ap);
int vop_readdir_ap(struct vop_readdir_args *ap);
int vop_readlink_ap(struct vop_readlink_args *ap);
int vop_inactive_ap(struct vop_inactive_args *ap);
int vop_reclaim_ap(struct vop_reclaim_args *ap);
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
int vop_getacl_ap(struct vop_getacl_args *ap);
int vop_setacl_ap(struct vop_setacl_args *ap);
int vop_aclcheck_ap(struct vop_aclcheck_args *ap);
int vop_getextattr_ap(struct vop_getextattr_args *ap);
int vop_setextattr_ap(struct vop_setextattr_args *ap);
int vop_mountctl_ap(struct vop_mountctl_args *ap);
int vop_markatime_ap(struct vop_markatime_args *ap);

int vop_nresolve_ap(struct vop_nresolve_args *ap);
int vop_nlookupdotdot_ap(struct vop_nlookupdotdot_args *ap);
int vop_ncreate_ap(struct vop_ncreate_args *ap);
int vop_nmkdir_ap(struct vop_nmkdir_args *ap);
int vop_nmknod_ap(struct vop_nmknod_args *ap);
int vop_nlink_ap(struct vop_nlink_args *ap);
int vop_nsymlink_ap(struct vop_nsymlink_args *ap);
int vop_nwhiteout_ap(struct vop_nwhiteout_args *ap);
int vop_nremove_ap(struct vop_nremove_args *ap);
int vop_nrmdir_ap(struct vop_nrmdir_args *ap);
int vop_nrename_ap(struct vop_nrename_args *ap);

/*
 * VOP operations descriptor.  This is used by the vop_ops compiler
 * to convert VFS vector arrays (typically in vfs/blah/blah_vnops.c)
 * into a vop_ops operations vector.
 */
extern struct syslink_desc vop_default_desc;
extern struct syslink_desc vop_old_lookup_desc;
extern struct syslink_desc vop_old_create_desc;
extern struct syslink_desc vop_old_whiteout_desc;
extern struct syslink_desc vop_old_mknod_desc;
extern struct syslink_desc vop_open_desc;
extern struct syslink_desc vop_close_desc;
extern struct syslink_desc vop_access_desc;
extern struct syslink_desc vop_getattr_desc;
extern struct syslink_desc vop_setattr_desc;
extern struct syslink_desc vop_read_desc;
extern struct syslink_desc vop_write_desc;
extern struct syslink_desc vop_ioctl_desc;
extern struct syslink_desc vop_poll_desc;
extern struct syslink_desc vop_kqfilter_desc;
extern struct syslink_desc vop_mmap_desc;
extern struct syslink_desc vop_fsync_desc;
extern struct syslink_desc vop_old_remove_desc;
extern struct syslink_desc vop_old_link_desc;
extern struct syslink_desc vop_old_rename_desc;
extern struct syslink_desc vop_old_mkdir_desc;
extern struct syslink_desc vop_old_rmdir_desc;
extern struct syslink_desc vop_old_symlink_desc;
extern struct syslink_desc vop_readdir_desc;
extern struct syslink_desc vop_readlink_desc;
extern struct syslink_desc vop_inactive_desc;
extern struct syslink_desc vop_reclaim_desc;
extern struct syslink_desc vop_bmap_desc;
extern struct syslink_desc vop_strategy_desc;
extern struct syslink_desc vop_print_desc;
extern struct syslink_desc vop_pathconf_desc;
extern struct syslink_desc vop_advlock_desc;
extern struct syslink_desc vop_balloc_desc;
extern struct syslink_desc vop_reallocblks_desc;
extern struct syslink_desc vop_getpages_desc;
extern struct syslink_desc vop_putpages_desc;
extern struct syslink_desc vop_freeblks_desc;
extern struct syslink_desc vop_getacl_desc;
extern struct syslink_desc vop_setacl_desc;
extern struct syslink_desc vop_aclcheck_desc;
extern struct syslink_desc vop_getextattr_desc;
extern struct syslink_desc vop_setextattr_desc;
extern struct syslink_desc vop_mountctl_desc;
extern struct syslink_desc vop_markatime_desc;

extern struct syslink_desc vop_nresolve_desc;
extern struct syslink_desc vop_nlookupdotdot_desc;
extern struct syslink_desc vop_ncreate_desc;
extern struct syslink_desc vop_nmkdir_desc;
extern struct syslink_desc vop_nmknod_desc;
extern struct syslink_desc vop_nlink_desc;
extern struct syslink_desc vop_nsymlink_desc;
extern struct syslink_desc vop_nwhiteout_desc;
extern struct syslink_desc vop_nremove_desc;
extern struct syslink_desc vop_nrmdir_desc;
extern struct syslink_desc vop_nrename_desc;

#endif

/*
 * VOP_*() convenience macros extract the operations vector and make the
 * vop_*() call.
 */
#define VOP_OPEN(vp, mode, cred, fp)			\
	vop_open(*(vp)->v_ops, vp, mode, cred, fp)
#define VOP_CLOSE(vp, fflag, fp)			\
	vop_close(*(vp)->v_ops, vp, fflag, fp)
#define VOP_ACCESS(vp, mode, cred)			\
	vop_access(*(vp)->v_ops, vp, mode, 0, cred)
#define VOP_EACCESS(vp, mode, cred)			\
	vop_access(*(vp)->v_ops, vp, mode, AT_EACCESS, cred)
#define VOP_ACCESS_FLAGS(vp, mode, flags, cred)		\
	vop_access(*(vp)->v_ops, vp, mode, flags, cred)
#define VOP_GETATTR(vp, vap)				\
	vop_getattr(*(vp)->v_ops, vp, vap)
#define VOP_SETATTR(vp, vap, cred)			\
	vop_setattr(*(vp)->v_ops, vp, vap, cred)
#define VOP_READ(vp, uio, ioflag, cred)			\
	vop_read(*(vp)->v_ops, vp, uio, ioflag, cred)
#define VOP_WRITE(vp, uio, ioflag, cred)		\
	vop_write(*(vp)->v_ops, vp, uio, ioflag, cred)
#define VOP_IOCTL(vp, command, data, fflag, cred, msg)	\
	vop_ioctl(*(vp)->v_ops, vp, command, data, fflag, cred, msg)
#define VOP_POLL(vp, events, cred)			\
	vop_poll(*(vp)->v_ops, vp, events, cred)
#define VOP_KQFILTER(vp, kn)				\
	vop_kqfilter(*(vp)->v_ops, vp, kn)
#define VOP_MMAP(vp, fflags, cred)			\
	vop_mmap(*(vp)->v_ops, vp, fflags, cred)
#define VOP_FSYNC(vp, waitfor, flags)			\
	vop_fsync(*(vp)->v_ops, vp, waitfor, flags)
#define VOP_READDIR(vp, uio, cred, eofflag, ncookies, cookies)		\
	vop_readdir(*(vp)->v_ops, vp, uio, cred, eofflag, ncookies, cookies)
#define VOP_READLINK(vp, uio, cred)			\
	vop_readlink(*(vp)->v_ops, vp, uio, cred)
#define VOP_INACTIVE(vp)				\
	vop_inactive(*(vp)->v_ops, vp)
#define VOP_RECLAIM(vp)					\
	vop_reclaim(*(vp)->v_ops, vp)
#define VOP_BMAP(vp, loff, doffp, runp, runb, cmd)	\
	vop_bmap(*(vp)->v_ops, vp, loff, doffp, runp, runb, cmd)
#define VOP_PRINT(vp)					\
	vop_print(*(vp)->v_ops, vp)
#define VOP_PATHCONF(vp, name, retval)			\
	vop_pathconf(*(vp)->v_ops, vp, name, retval)
#define VOP_ADVLOCK(vp, id, op, fl, flags)		\
	vop_advlock(*(vp)->v_ops, vp, id, op, fl, flags)
#define VOP_BALLOC(vp, offset, size, cred, flags, bpp)	\
	vop_balloc(*(vp)->v_ops, vp, offset, size, cred, flags, bpp)
#define VOP_REALLOCBLKS(vp, buflist)			\
	vop_reallocblks(*(vp)->v_ops, vp, buflist)
#define VOP_GETPAGES(vp, m, count, reqpage, off, seqaccess)		\
	vop_getpages(*(vp)->v_ops, vp, m, count, reqpage, off, seqaccess)
#define VOP_PUTPAGES(vp, m, count, sync, rtvals, off)	\
	vop_putpages(*(vp)->v_ops, vp, m, count, sync, rtvals, off)
#define VOP_FREEBLKS(vp, offset, length)		\
	vop_freeblks(*(vp)->v_ops, vp, offset, length)
#define VOP_GETACL(vp, type, aclp, cred)		\
	vop_getacl(*(vp)->v_ops, vp, type, aclp, cred)
#define VOP_SETACL(vp, type, aclp, cred)		\
	vop_setacl(*(vp)->v_ops, vp, type, aclp, cred)
#define VOP_ACLCHECK(vp, type, aclp, cred)		\
	vop_aclcheck(*(vp)->v_ops, vp, type, aclp, cred)
#define VOP_GETEXTATTR(vp, attrnamespace, attrname, uio, cred) \
	vop_getextattr(*(vp)->v_ops, vp, attrnamespace, attrname, uio, cred)
#define VOP_SETEXTATTR(vp, attrnamespace, attrname, uio, cred)	\
	vop_setextattr(*(vp)->v_ops, vp, attrnamespace, attrname, uio, cred)
#define VOP_MARKATIME(vp, cred)			\
	vop_markatime(*(vp)->v_ops, vp, cred)
/* no VOP_VFSSET() */
/* VOP_STRATEGY - does not exist, use vn_strategy() */

/*
 * 'OLD' VOP calls.  These calls may only be made by the new API
 * compatibility functions in kern/vfs_default.c.  Attempting to
 * call these functions directly will confuse the namecache.  These
 * calls are deprecated and being removed from the system and from
 * the VFS's.
 */
#define VOP_OLD_LOOKUP(dvp, vpp, cnp)			\
	vop_old_lookup(*(dvp)->v_ops, dvp, vpp, cnp)
#define VOP_OLD_CREATE(dvp, vpp, cnp, vap)		\
	vop_old_create(*(dvp)->v_ops, dvp, vpp, cnp, vap)
#define VOP_OLD_MKDIR(dvp, vpp, cnp, vap)		\
	vop_old_mkdir(*(dvp)->v_ops, dvp, vpp, cnp, vap)
#define VOP_OLD_MKNOD(dvp, vpp, cnp, vap)		\
	vop_old_mknod(*(dvp)->v_ops, dvp, vpp, cnp, vap)
#define VOP_OLD_LINK(tdvp, vp, cnp)			\
	vop_old_link(*(tdvp)->v_ops, tdvp, vp, cnp)
#define VOP_OLD_SYMLINK(dvp, vpp, cnp, vap, target)	\
	vop_old_symlink(*(dvp)->v_ops, dvp, vpp, cnp, vap, target)
#define VOP_OLD_WHITEOUT(dvp, cnp, flags)		\
	vop_old_whiteout(*(dvp)->v_ops, dvp, cnp, flags)
#define VOP_OLD_RENAME(fdvp, fvp, fcnp, tdvp, tvp, tcnp) \
	vop_old_rename(*(fdvp)->v_ops, fdvp, fvp, fcnp, tdvp, tvp, tcnp)
#define VOP_OLD_RMDIR(dvp, vp, cnp)			\
	vop_old_rmdir(*(dvp)->v_ops, dvp, vp, cnp)
#define VOP_OLD_REMOVE(dvp, vp, cnp)			\
	vop_old_remove(*(dvp)->v_ops, dvp, vp, cnp)

/*
 * 'NEW' VOP calls.  These calls use namespaces as an operational basis
 * rather then vnodes and replace the OLD calls.   Eventually all VFS's
 * will support these calls.  Those that do not fall through to compatibility
 * code in kern/vfs_default which does the magic required to call the old
 * routines.
 */
#define VOP_NRESOLVE(nch, dvp, cred)			\
	vop_nresolve((nch)->mount->mnt_vn_use_ops, nch, dvp, cred)
#define VOP_NLOOKUPDOTDOT(dvp, vpp, cred, fakename)	\
	vop_nlookupdotdot(*(dvp)->v_ops, dvp, vpp, cred, fakename)
#define VOP_NCREATE(nch, dvp, vpp, cred, vap)		\
	vop_ncreate((nch)->mount->mnt_vn_use_ops, nch, dvp, vpp, cred, vap)
#define VOP_NMKDIR(nch, dvp, vpp, cred, vap)		\
	vop_nmkdir((nch)->mount->mnt_vn_use_ops, nch, dvp, vpp, cred, vap)
#define VOP_NMKNOD(nch, dvp, vpp, cred, vap)		\
	vop_nmknod((nch)->mount->mnt_vn_use_ops, nch, dvp, vpp, cred, vap)
#define VOP_NLINK(nch, dvp, vp, cred)			\
	vop_nlink((nch)->mount->mnt_vn_use_ops, nch, dvp, vp, cred)
#define VOP_NSYMLINK(nch, dvp, vpp, cred, vap, target)	\
	vop_nsymlink((nch)->mount->mnt_vn_use_ops, nch, dvp, vpp, cred, vap, target)
#define VOP_NWHITEOUT(nch, dvp, cred, flags)		\
	vop_nwhiteout((nch)->mount->mnt_vn_use_ops, nch, dvp, cred, flags)
#define VOP_NRENAME(fnch, tnch, fdvp, tdvp, cred)		\
	vop_nrename((fnch)->mount->mnt_vn_use_ops, fnch, tnch, fdvp, tdvp, cred)
#define VOP_NRMDIR(nch, dvp, cred)			\
	vop_nrmdir((nch)->mount->mnt_vn_use_ops, nch, dvp, cred)
#define VOP_NREMOVE(nch, dvp, cred)			\
	vop_nremove((nch)->mount->mnt_vn_use_ops, nch, dvp, cred)

#endif

