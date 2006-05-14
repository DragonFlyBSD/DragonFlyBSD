/*
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * John Heidemann of the UCLA Ficus project.
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
 *	@(#)null_vnops.c	8.6 (Berkeley) 5/27/95
 *
 * Ancestors:
 *	@(#)lofs_vnops.c	1.2 (Berkeley) 6/18/92
 * $FreeBSD: src/sys/miscfs/nullfs/null_vnops.c,v 1.38.2.6 2002/07/31 00:32:28 semenu Exp $
 * $DragonFly: src/sys/vfs/nullfs/null_vnops.c,v 1.26 2006/05/14 18:07:29 swildner Exp $
 *	...and...
 *	@(#)null_vnodeops.c 1.20 92/07/07 UCLA Ficus project
 *
 * $FreeBSD: src/sys/miscfs/nullfs/null_vnops.c,v 1.38.2.6 2002/07/31 00:32:28 semenu Exp $
 */

/*
 * Null Layer
 *
 * (See mount_null(8) for more information.)
 *
 * The null layer duplicates a portion of the file system
 * name space under a new name.  In this respect, it is
 * similar to the loopback file system.  It differs from
 * the loopback fs in two respects:  it is implemented using
 * a stackable layers techniques, and its "null-node"s stack above
 * all lower-layer vnodes, not just over directory vnodes.
 *
 * The null layer has two purposes.  First, it serves as a demonstration
 * of layering by proving a layer which does nothing.  (It actually
 * does everything the loopback file system does, which is slightly
 * more than nothing.)  Second, the null layer can serve as a prototype
 * layer.  Since it provides all necessary layer framework,
 * new file system layers can be created very easily be starting
 * with a null layer.
 *
 * The remainder of this man page examines the null layer as a basis
 * for constructing new layers.
 *
 *
 * INSTANTIATING NEW NULL LAYERS
 *
 * New null layers are created with mount_null(8).
 * Mount_null(8) takes two arguments, the pathname
 * of the lower vfs (target-pn) and the pathname where the null
 * layer will appear in the namespace (alias-pn).  After
 * the null layer is put into place, the contents
 * of target-pn subtree will be aliased under alias-pn.
 *
 *
 * OPERATION OF A NULL LAYER
 *
 * The null layer is the minimum file system layer,
 * simply bypassing all possible operations to the lower layer
 * for processing there.  The majority of its activity used to center
 * on a so-called bypass routine, through which nullfs vnodes
 * passed on operation to their underlying peer.
 *
 * However, with the current implementation nullfs doesn't have any private
 * vnodes, it rather relies on DragonFly's namecache API. That gives a much
 * more lightweight null layer, as namecache structures are pure data, with
 * no private operations, so there is no need of subtle dispatching routines.
 *
 * Unlike the old code, this implementation is not a general skeleton overlay
 * filesystem: to get more comprehensive overlaying, we will need vnode
 * operation dispatch. Other overlay filesystems, like unionfs might be
 * able to get on with a hybrid solution: overlay some vnodes, and rely
 * on namecache API for the rest.
 */
 
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>
#include <sys/vnode.h>
#include <sys/mount.h>
#include <sys/proc.h>
#include <sys/namei.h>
#include <sys/malloc.h>
#include <sys/buf.h>
#include "null.h"

static int	null_nresolve(struct vop_nresolve_args *ap);
static int	null_ncreate(struct vop_ncreate_args *ap);
static int	null_nmkdir(struct vop_nmkdir_args *ap);
static int	null_nmknod(struct vop_nmknod_args *ap);
static int	null_nlink(struct vop_nlink_args *ap);
static int	null_nsymlink(struct vop_nsymlink_args *ap);
static int	null_nwhiteout(struct vop_nwhiteout_args *ap);
static int	null_nremove(struct vop_nremove_args *ap);
static int	null_nrmdir(struct vop_nrmdir_args *ap);
static int	null_nrename(struct vop_nrename_args *ap);

static int
null_nresolve(struct vop_nresolve_args *ap)
{
	ap->a_head.a_ops = MOUNTTONULLMOUNT(ap->a_ncp->nc_mount)->nullm_vfs->mnt_vn_norm_ops;

	return vop_nresolve_ap(ap);
}

static int
null_ncreate(struct vop_ncreate_args *ap)
{
	ap->a_head.a_ops = MOUNTTONULLMOUNT(ap->a_ncp->nc_mount)->nullm_vfs->mnt_vn_norm_ops;

	return vop_ncreate_ap(ap);
}

static int
null_nmkdir(struct vop_nmkdir_args *ap)
{
	ap->a_head.a_ops = MOUNTTONULLMOUNT(ap->a_ncp->nc_mount)->nullm_vfs->mnt_vn_norm_ops;

	return vop_nmkdir_ap(ap);
}

static int
null_nmknod(struct vop_nmknod_args *ap)
{
	ap->a_head.a_ops = MOUNTTONULLMOUNT(ap->a_ncp->nc_mount)->nullm_vfs->mnt_vn_norm_ops;

	return vop_nmknod_ap(ap);
}

static int
null_nlink(struct vop_nlink_args *ap)
{
	ap->a_head.a_ops = MOUNTTONULLMOUNT(ap->a_ncp->nc_mount)->nullm_vfs->mnt_vn_norm_ops;

	return vop_nlink_ap(ap);
}

static int
null_nsymlink(struct vop_nsymlink_args *ap)
{
	ap->a_head.a_ops = MOUNTTONULLMOUNT(ap->a_ncp->nc_mount)->nullm_vfs->mnt_vn_norm_ops;

	return vop_nsymlink_ap(ap);
}

static int
null_nwhiteout(struct vop_nwhiteout_args *ap)
{
	ap->a_head.a_ops = MOUNTTONULLMOUNT(ap->a_ncp->nc_mount)->nullm_vfs->mnt_vn_norm_ops;

	return vop_nwhiteout_ap(ap);
}

static int
null_nremove(struct vop_nremove_args *ap)
{
	ap->a_head.a_ops = MOUNTTONULLMOUNT(ap->a_ncp->nc_mount)->nullm_vfs->mnt_vn_norm_ops;

	return vop_nremove_ap(ap);
}

static int
null_nrmdir(struct vop_nrmdir_args *ap)
{
	ap->a_head.a_ops = MOUNTTONULLMOUNT(ap->a_ncp->nc_mount)->nullm_vfs->mnt_vn_norm_ops;

	return vop_nrmdir_ap(ap);
}

static int
null_nrename(struct vop_nrename_args *ap)
{
	struct mount *lmp;

	lmp = MOUNTTONULLMOUNT(ap->a_fncp->nc_mount)->nullm_vfs;
	if (lmp != MOUNTTONULLMOUNT(ap->a_tncp->nc_mount)->nullm_vfs)
		return (EINVAL);

	ap->a_head.a_ops = lmp->mnt_vn_norm_ops;

	return vop_nrename_ap(ap);
}

/*
 * Global vfs data structures
 */
struct vnodeopv_entry_desc null_vnodeop_entries[] = {
	{ &vop_nresolve_desc,		(vnodeopv_entry_t) null_nresolve },
	{ &vop_ncreate_desc,		(vnodeopv_entry_t) null_ncreate },
	{ &vop_nmkdir_desc,		(vnodeopv_entry_t) null_nmkdir },
	{ &vop_nmknod_desc,		(vnodeopv_entry_t) null_nmknod },
	{ &vop_nlink_desc,		(vnodeopv_entry_t) null_nlink },
	{ &vop_nsymlink_desc,		(vnodeopv_entry_t) null_nsymlink },
	{ &vop_nwhiteout_desc,		(vnodeopv_entry_t) null_nwhiteout },
	{ &vop_nremove_desc,		(vnodeopv_entry_t) null_nremove },
	{ &vop_nrmdir_desc,		(vnodeopv_entry_t) null_nrmdir },
	{ &vop_nrename_desc,		(vnodeopv_entry_t) null_nrename },
	{ NULL, NULL }
};

