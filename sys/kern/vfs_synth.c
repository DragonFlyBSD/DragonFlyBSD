/*
 * Copyright (c) 2010 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Alex Hornung <ahornung (at) gmail.com>
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
#include <sys/vnode.h>
#include <sys/lock.h>
#include <sys/disk.h>
#include <sys/mount.h>
#include <sys/namecache.h>
#include <sys/nlookup.h>
#include <sys/proc.h>
#include <sys/thread2.h>
#include <sys/devfs.h>

struct mount	*synth_mp;
struct vnode	*synth_vp;
static int	synth_inited = 0;
static int	synth_synced = 0;

struct vnode *
getsynthvnode(const char *devname)
{
	struct vnode *vp;
	struct nchandle nch;
	struct nlookupdata nd;
	struct ucred *cred = proc0.p_ucred;
	int error;

	KKASSERT(synth_inited != 0);
	KKASSERT(synth_mp != NULL);
	KKASSERT(synth_mp->mnt_ncmountpt.mount != NULL);

	/* Sync devfs/disks twice to make sure all devices are around */
	if (synth_synced < 2) {
		sync_devs();
		++synth_synced;
	}

	error = nlookup_init_root(&nd,
	     devname, UIO_SYSSPACE, NLC_FOLLOW,
	     cred, &synth_mp->mnt_ncmountpt,
	     &synth_mp->mnt_ncmountpt);

	if (error) {
		panic("synth: nlookup_init_root failed with %d", error);
		/* NOTREACHED */
	}

	error = nlookup(&nd);
	if (error == 0) {
		if (nd.nl_nch.ncp->nc_vp == NULL) {
			kprintf("synth: nc_vp == NULL\n");
			return (NULL);
		}
		nch = nd.nl_nch;
		cache_zero(&nd.nl_nch);
	}

	nlookup_done(&nd);
	if (error) {
		if (error != ENOENT) { /* Don't bother warning about ENOENT */
			kprintf("synth: nlookup of %s failed with %d\n",
			    devname, error);
		}
		return (NULL);
	}

	vp = nch.ncp->nc_vp;
	/* A VX locked & refd vnode must be returned. */
	error = vget(vp, LK_EXCLUSIVE);
	cache_unlock(&nch);

	if (error) {
		kprintf("synth: could not vget vnode\n");
		return (NULL);
	}

	return (vp);
}

static void
synthinit(void *arg __unused)
{
	int error;

	if ((error = vfs_rootmountalloc("devfs", "dummy", &synth_mp))) {
		panic("synth: vfs_rootmountalloc failed with %d", error);
		/* NOTREACHED */
	}
	if ((error = VFS_MOUNT(synth_mp, NULL, NULL, proc0.p_ucred))) {
		panic("synth: vfs_mount failed with %d", error);
		/* NOTREACHED */
	}
	if ((error = VFS_ROOT(synth_mp, &synth_vp))) {
		panic("synth: vfs_root failed with %d", error);
		/* NOTREACHED */
	}
	cache_allocroot(&synth_mp->mnt_ncmountpt, synth_mp, synth_vp);
	cache_unlock(&synth_mp->mnt_ncmountpt);	/* leave ref intact */
	vput(synth_vp);

	synth_inited = 1;
}

SYSINIT(synthinit, SI_SUB_VFS, SI_ORDER_ANY, synthinit, NULL);
