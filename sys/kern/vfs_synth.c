/*
 * Copyright (c) 2007 The DragonFly Project.  All rights reserved.
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
 * $DragonFly: src/sys/kern/vfs_synth.c,v 1.1 2007/07/30 08:02:38 dillon Exp $
 */

/*
 * Synthesize vnodes for devices.  Devices have certain requirements and
 * limitations with regards to opening and closing, and physical I/O
 * limits.  This module allows you to synthesize a specfs-backed vnode
 * for a device.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/vnode.h>
#include <sys/lock.h>
#include <sys/disk.h>
#include <sys/mount.h>

#include <sys/thread2.h>

static struct mount *synth_mount;

/*
 * getsynthvnode() - return a vnode representing a device.
 *
 * The caller must VOP_OPEN() the vnode as appropriate before using it for
 * I/O, and VOP_CLOSE() it when finished.
 *
 * The returned vnode will be referenced and locked.  The caller must
 * vrele() the vnode when finished with it.
 */
struct vnode *
getsynthvnode(const char *devname)
{
	struct vnode *vp;
	int error;
	cdev_t dev;

	dev = disk_locate(devname);
	if (dev == NULL)
		return(NULL);

	error = getnewvnode(VT_SYNTH, synth_mount, &vp, 0, 0);
	if (error)
		panic("getsynthvnode: unable to get new vnode");
	vp->v_type = VCHR;
	addaliasu(vp, dev->si_umajor, dev->si_uminor);
	return(vp);
}

/*
 * VOP support - use specfs and dummy up the mount.
 */

static int synth_inactive(struct vop_inactive_args *ap);
static int synth_reclaim(struct vop_reclaim_args *ap);

struct vop_ops synth_vnode_vops = {
	.vop_default		= spec_vnoperate,
	.vop_inactive		= synth_inactive,
	.vop_reclaim		= synth_reclaim
};

VNODEOP_SET(synth_vnode_vops);

static
int
synth_inactive(struct vop_inactive_args *ap)
{
	vrecycle(ap->a_vp);
	return (0);
}

static
int
synth_reclaim(struct vop_reclaim_args *ap)
{
	ap->a_vp->v_data = NULL;
	return(0);
}

/*
 * Create a dummy mount structure and VFS.  This VFS is not currently
 * mountable.
 */
static int synth_vfs_mount(struct mount *, char *, caddr_t, struct ucred *);
static int synth_vfs_unmount(struct mount *, int mntflags);
static int synth_vfs_root(struct mount *mp, struct vnode **vpp);

static struct vfsops synth_vfsops = {
	.vfs_mount	= synth_vfs_mount,
	.vfs_root	= synth_vfs_root,
	.vfs_unmount	= synth_vfs_unmount
};

static struct vfsconf synth_vfsconf = {
	.vfc_vfsops = &synth_vfsops,
	.vfc_name = { "synth" },
	.vfc_typenum = VT_SYNTH
};

static
int
synth_vfs_mount(struct mount *mp, char *path, caddr_t data, struct ucred *cred)
{
	return (EINVAL);
}

static
int
synth_vfs_unmount(struct mount *mp, int mntflags)
{
	return (0);
}

static
int
synth_vfs_root(struct mount *mp, struct vnode **vpp)
{
	*vpp = NULL;
	return (EINVAL);
}

/*
 * We have to register our VFS and create our dummy mount structure before
 * devices configure or vinum will not be able to configure at boot.  The
 * standard usage via VFS_SET() is registered too late.
 */
static
void
synthinit(void *arg __unused)
{
	int error;

	error = vfs_register(&synth_vfsconf);
	KKASSERT(error == 0);
	error = vfs_rootmountalloc("synth", "dummy", &synth_mount);
	KKASSERT(error == 0);
	synth_mount->mnt_vn_use_ops = &synth_vnode_vops;
}

SYSINIT(synthinit, SI_SUB_CREATE_INIT, SI_ORDER_ANY, synthinit, NULL)

