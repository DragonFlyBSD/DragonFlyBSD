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
 *	@(#)dead_vnops.c	8.1 (Berkeley) 6/10/93
 * $FreeBSD: src/sys/miscfs/deadfs/dead_vnops.c,v 1.26 1999/08/28 00:46:42 peter Exp $
 * $DragonFly: src/sys/vfs/deadfs/dead_vnops.c,v 1.20 2007/08/13 17:31:56 dillon Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/vnode.h>
#include <sys/fcntl.h>
#include <sys/buf.h>

/*
 * Prototypes for dead operations on vnodes.
 */
static int	dead_bmap (struct vop_bmap_args *);
static int	dead_ioctl (struct vop_ioctl_args *);
static int	dead_lookup (struct vop_old_lookup_args *);
static int	dead_open (struct vop_open_args *);
static int	dead_close (struct vop_close_args *);
static int	dead_print (struct vop_print_args *);
static int	dead_read (struct vop_read_args *);
static int	dead_write (struct vop_write_args *);

struct vop_ops dead_vnode_vops = {
	.vop_default =		vop_defaultop,
	.vop_access =		VOP_EBADF,
	.vop_advlock =		VOP_EBADF,
	.vop_bmap =		dead_bmap,
	.vop_old_create =	VOP_PANIC,
	.vop_getattr =		VOP_EBADF,
	.vop_inactive =		VOP_NULL,
	.vop_ioctl =		dead_ioctl,
	.vop_old_link =		VOP_PANIC,
	.vop_old_lookup =	dead_lookup,
	.vop_old_mkdir =	VOP_PANIC,
	.vop_old_mknod =	VOP_PANIC,
	.vop_mmap =		VOP_PANIC,
	.vop_open =		dead_open,
	.vop_close =		dead_close,
	.vop_pathconf =		VOP_EBADF,	/* per pathconf(2) */
	.vop_print =		dead_print,
	.vop_read =		dead_read,
	.vop_readdir =		VOP_EBADF,
	.vop_readlink =		VOP_EBADF,
	.vop_reclaim =		VOP_NULL,
	.vop_old_remove =	VOP_PANIC,
	.vop_old_rename =	VOP_PANIC,
	.vop_old_rmdir =	VOP_PANIC,
	.vop_setattr =		VOP_EBADF,
	.vop_old_symlink =	VOP_PANIC,
	.vop_write =		dead_write
};

struct vop_ops *dead_vnode_vops_p = &dead_vnode_vops;

VNODEOP_SET(dead_vnode_vops);

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
 * Close always succeeds, and does not warn or panic if v_opencount or
 * v_writecount is incorrect, because a forced unmount or revocation
 * might have closed the file out from under the descriptor.
 */
static int
dead_close(struct vop_close_args *ap)
{
	struct vnode *vp = ap->a_vp;

	vn_lock(vp, LK_UPGRADE | LK_RETRY);	/* safety */
	if (vp->v_opencount > 0) {
		if ((ap->a_fflag & FWRITE) && vp->v_writecount > 0)
			atomic_add_int(&vp->v_writecount, -1);
		atomic_add_int(&vp->v_opencount, -1);
	}
	return (0);
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
 * dead_bmap(struct vnode *a_vp, off_t a_loffset, 
 *	     off_t *a_doffsetp, int *a_runp, int *a_runb)
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
	kprintf("tag VT_NON, dead vnode\n");
	return (0);
}
