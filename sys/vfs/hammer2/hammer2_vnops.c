/*
 * Copyright (c) 2011-2012 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@dragonflybsd.org>
 * by Venkatesh Srinivas <vsrinivas@dragonflybsd.org>
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
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/fcntl.h>
#include <sys/buf.h>
#include <sys/proc.h>
#include <sys/namei.h>
#include <sys/mount.h>
#include <sys/vnode.h>
#include <sys/mountctl.h>

#include "hammer2.h"

/*
 * Last reference to a vnode is going away but it is still cached.
 */
static
int
hammer2_vop_inactive(struct vop_inactive_args *ap)
{
	struct vnode *vp;
	struct hammer2_inode *ip;
#if 0
	struct hammer2_mount *hmp;
#endif

	kprintf("hammer2_inactive\n");

	vp = ap->a_vp;
	ip = VTOI(vp);

	/*
	 * Degenerate case
	 */
	if (ip == NULL) {
		vrecycle(vp);
		return (0);
	}

	return (0);
}

/*
 * Reclaim a vnode so that it can be reused; after the inode is
 * disassociated, the filesystem must manage it alone.
 */
static
int
hammer2_vop_reclaim(struct vop_reclaim_args *ap)
{
	struct vnode *vp;
	struct hammer2_inode *ip;
	struct hammer2_mount *hmp;

	kprintf("hammer2_reclaim\n");
	vp = ap->a_vp;
	ip = VTOI(vp);
	hmp = ip->hmp;

	hammer2_inode_lock_ex(ip);
	vp->v_data = NULL;
	ip->vp = NULL;
	hammer2_inode_unlock_ex(ip);

	/*
	 * XXX handle background sync when ip dirty, kernel will no longer
	 * notify us regarding this inode because there is no longer a
	 * vnode attached to it.
	 */

	return (0);
}

static
int
hammer2_vop_fsync(struct vop_fsync_args *ap)
{
	kprintf("hammer2_fsync\n");
	return (EOPNOTSUPP);
}

static
int
hammer2_vop_access(struct vop_access_args *ap)
{
	kprintf("hammer2_access\n");
	return (0);
}

static
int
hammer2_vop_getattr(struct vop_getattr_args *ap)
{
	struct vnode *vp;
	struct vattr *vap;
	struct hammer2_inode *ip;

	vp = ap->a_vp;
	vap = ap->a_vap;

	kprintf("hammer2_getattr\n");

	ip = VTOI(vp);
	hammer2_inode_lock_sh(ip);

	vap->va_type = vp->v_type;
	vap->va_mode = 0777;
	vap->va_nlink = 1;
	vap->va_uid = 0;
	vap->va_gid = 0;
	vap->va_size = 0;
	vap->va_blocksize = HAMMER2_PBUFSIZE;
	vap->va_flags = 0;

	hammer2_inode_unlock_sh(ip);

	return (0);
}

static
int
hammer2_vop_readdir(struct vop_readdir_args *ap)
{
	kprintf("hammer2_readdir\n");
	return (EOPNOTSUPP);
}

static
int
hammer2_vop_read(struct vop_read_args *ap)
{
	return (EOPNOTSUPP);
}

static
int
hammer2_vop_write(struct vop_write_args *ap)
{
	return (EOPNOTSUPP);
}

static
int
hammer2_vop_nresolve(struct vop_nresolve_args *ap)
{
	kprintf("hammer2_nresolve\n");
	return (EOPNOTSUPP);
}

static
int
hammer2_vop_bmap(struct vop_bmap_args *ap)
{
	kprintf("hammer2_bmap\n");
	return (EOPNOTSUPP);
}

static
int
hammer2_vop_open(struct vop_open_args *ap)
{
	kprintf("hammer2_open\n");
	return vop_stdopen(ap);
}

static
int
hammer2_vop_strategy(struct vop_strategy_args *ap)
{
	struct vnode *vp;
	struct bio *biop;
	struct buf *bp;
	struct hammer2_inode *ip;
	int error;

	vp = ap->a_vp;
	biop = ap->a_bio;
	bp = biop->bio_buf;
	ip = VTOI(vp);

	switch(bp->b_cmd) {
	case (BUF_CMD_READ):
	case (BUF_CMD_WRITE):
	default:
		bp->b_error = error = EINVAL;
		bp->b_flags |= B_ERROR;
		biodone(biop);
		break;
	}

	return (error);
}

static
int 
hammer2_vop_mountctl(struct vop_mountctl_args *ap)
{
	struct mount *mp;
	struct hammer2_mount *hmp;
	int rc;

	switch (ap->a_op) {
	case (MOUNTCTL_SET_EXPORT):
		mp = ap->a_head.a_ops->head.vv_mount;
		hmp = MPTOH2(mp);

		if (ap->a_ctllen != sizeof(struct export_args))
			rc = (EINVAL);
		else
			rc = vfs_export(mp, &hmp->export,
					(const struct export_args *)ap->a_ctl);
		break;
	default:
		rc = vop_stdmountctl(ap);
		break;
	}
	return (rc);
}

struct vop_ops hammer2_vnode_vops = {
	.vop_default	= vop_defaultop,
	.vop_fsync	= hammer2_vop_fsync,
	.vop_getpages	= vop_stdgetpages,
	.vop_putpages	= vop_stdputpages,
	.vop_access	= hammer2_vop_access,
	.vop_getattr	= hammer2_vop_getattr,
	.vop_readdir	= hammer2_vop_readdir,
	.vop_read	= hammer2_vop_read,
	.vop_write	= hammer2_vop_write,
	.vop_open	= hammer2_vop_open,
	.vop_inactive	= hammer2_vop_inactive,
	.vop_reclaim 	= hammer2_vop_reclaim,
	.vop_nresolve	= hammer2_vop_nresolve,
	.vop_mountctl	= hammer2_vop_mountctl,
	.vop_bmap	= hammer2_vop_bmap,
	.vop_strategy	= hammer2_vop_strategy,
};

struct vop_ops hammer2_spec_vops = {

};

struct vop_ops hammer2_fifo_vops = {

};
