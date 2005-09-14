/*
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 *	@(#)dead_vnops.c	8.1 (Berkeley) 6/10/93
 * $FreeBSD: src/sys/miscfs/deadfs/dead_vnops.c,v 1.26 1999/08/28 00:46:42 peter Exp $
 * $DragonFly: src/sys/vfs/deadfs/dead_vnops.c,v 1.13 2005/09/14 01:13:24 dillon Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/vnode.h>
#include <sys/buf.h>
#include <sys/poll.h>

/*
 * Prototypes for dead operations on vnodes.
 */
static int	dead_badop (void);
static int	dead_bmap (struct vop_bmap_args *);
static int	dead_ioctl (struct vop_ioctl_args *);
static int	dead_lock (struct vop_lock_args *);
static int	dead_lookup (struct vop_old_lookup_args *);
static int	dead_open (struct vop_open_args *);
static int	dead_poll (struct vop_poll_args *);
static int	dead_print (struct vop_print_args *);
static int	dead_read (struct vop_read_args *);
static int	dead_write (struct vop_write_args *);

struct vop_ops *dead_vnode_vops;
static struct vnodeopv_entry_desc dead_vnodeop_entries[] = {
	{ &vop_default_desc,		vop_defaultop },
	{ &vop_access_desc,		vop_ebadf },
	{ &vop_advlock_desc,		vop_ebadf },
	{ &vop_bmap_desc,		(vnodeopv_entry_t) dead_bmap },
	{ &vop_old_create_desc,		(vnodeopv_entry_t) dead_badop },
	{ &vop_getattr_desc,		vop_ebadf },
	{ &vop_inactive_desc,		vop_null },
	{ &vop_ioctl_desc,		(vnodeopv_entry_t) dead_ioctl },
	{ &vop_old_link_desc,		(vnodeopv_entry_t) dead_badop },
	{ &vop_lock_desc,		(vnodeopv_entry_t) dead_lock },
	{ &vop_old_lookup_desc,		(vnodeopv_entry_t) dead_lookup },
	{ &vop_old_mkdir_desc,		(vnodeopv_entry_t) dead_badop },
	{ &vop_old_mknod_desc,		(vnodeopv_entry_t) dead_badop },
	{ &vop_mmap_desc,		(vnodeopv_entry_t) dead_badop },
	{ &vop_open_desc,		(vnodeopv_entry_t) dead_open },
	{ &vop_pathconf_desc,		vop_ebadf },	/* per pathconf(2) */
	{ &vop_poll_desc,		(vnodeopv_entry_t) dead_poll },
	{ &vop_print_desc,		(vnodeopv_entry_t) dead_print },
	{ &vop_read_desc,		(vnodeopv_entry_t) dead_read },
	{ &vop_readdir_desc,		vop_ebadf },
	{ &vop_readlink_desc,		vop_ebadf },
	{ &vop_reclaim_desc,		vop_null },
	{ &vop_old_remove_desc,		(vnodeopv_entry_t) dead_badop },
	{ &vop_old_rename_desc,		(vnodeopv_entry_t) dead_badop },
	{ &vop_old_rmdir_desc,		(vnodeopv_entry_t) dead_badop },
	{ &vop_setattr_desc,		vop_ebadf },
	{ &vop_old_symlink_desc,	(vnodeopv_entry_t) dead_badop },
	{ &vop_write_desc,		(vnodeopv_entry_t) dead_write },
	{ NULL, NULL }
};
static struct vnodeopv_desc dead_vnodeop_opv_desc =
	{ &dead_vnode_vops, dead_vnodeop_entries };

VNODEOP_SET(dead_vnodeop_opv_desc);

/*
 * Trivial lookup routine that always fails.
 *
 * dead_lookup(struct vnode *a_dvp, struct vnode **a_vpp,
 *	       struct componentname *a_cnp)
 */
/* ARGSUSED */
static int
dead_lookup(struct vop_old_lookup_args *ap)
{
	*ap->a_vpp = NULL;
	return (ENOTDIR);
}

/*
 * Open always fails as if device did not exist.
 *
 * dead_open(struct vnode *a_vp, int a_mode, struct ucred *a_cred,
 *	     struct proc *a_p)
 */
/* ARGSUSED */
static int
dead_open(struct vop_open_args *ap)
{
	return (ENXIO);
}

/*
 * Vnode op for read
 *
 * dead_read(struct vnode *a_vp, struct uio *a_uio, int a_ioflag,
 *	     struct ucred *a_cred)
 */
/* ARGSUSED */
static int
dead_read(struct vop_read_args *ap)
{
	/*
	 * Return EOF for tty devices, EIO for others
	 */
	if ((ap->a_vp->v_flag & VISTTY) == 0)
		return (EIO);
	return (0);
}

/*
 * Vnode op for write
 *
 * dead_write(struct vnode *a_vp, struct uio *a_uio, int a_ioflag,
 *	      struct ucred *a_cred)
 */
/* ARGSUSED */
static int
dead_write(struct vop_write_args *ap)
{
	return (EIO);
}

/*
 * Device ioctl operation.
 *
 * dead_ioctl(struct vnode *a_vp, int a_command, caddr_t a_data, int a_fflag,
 *	      struct ucred *a_cred, struct proc *a_p)
 */
/* ARGSUSED */
static int
dead_ioctl(struct vop_ioctl_args *ap)
{
	return (ENOTTY);
}

/*
 * Wait until the vnode has finished changing state.
 *
 * dead_lock(struct vnode *a_vp, int a_flags, struct proc *a_p)
 */
static int
dead_lock(struct vop_lock_args *ap)
{
	return (0);
}

/*
 * Wait until the vnode has finished changing state.
 *
 * dead_bmap(struct vnode *a_vp, daddr_t a_bn, struct vnode **a_vpp,
 *	     daddr_t *a_bnp, int *a_runp, int *a_runb)
 */
static int
dead_bmap(struct vop_bmap_args *ap)
{
	return (EIO);
}

/*
 * Print out the contents of a dead vnode.
 *
 * dead_print(struct vnode *a_vp)
 */
/* ARGSUSED */
static int
dead_print(struct vop_print_args *ap)
{
	printf("tag VT_NON, dead vnode\n");
	return (0);
}

/*
 * Empty vnode bad operation
 */
static int
dead_badop(void)
{
	panic("dead_badop called");
	/* NOTREACHED */
}

/*
 * Trivial poll routine that always returns POLLHUP.
 * This is necessary so that a process which is polling a file
 * gets notified when that file is revoke()d.
 */
static int
dead_poll(struct vop_poll_args *ap)
{
	return (POLLHUP);
}
