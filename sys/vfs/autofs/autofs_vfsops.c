/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2016 Tomohiro Kusumi <tkusumi@netbsd.org>
 * Copyright (c) 2016 The DragonFly Project
 * Copyright (c) 2014 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Edward Tomasz Napierala under sponsorship
 * from the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/stat.h>

#include "autofs.h"
#include "autofs_mount.h"

static int	autofs_statfs(struct mount *mp, struct statfs *sbp,
		    struct ucred *cred);

static struct objcache_malloc_args autofs_request_args = {
	sizeof(struct autofs_request), M_AUTOFS,
};
static struct objcache_malloc_args autofs_node_args = {
	sizeof(struct autofs_node), M_AUTOFS,
};

static int
autofs_init(struct vfsconf *vfsp)
{
	KASSERT(autofs_softc == NULL,
	    ("softc %p, should be NULL", autofs_softc));

	autofs_softc = kmalloc(sizeof(*autofs_softc), M_AUTOFS,
	    M_WAITOK | M_ZERO);

	autofs_request_objcache = objcache_create("autofs_request", 0, 0,
		NULL, NULL, NULL,
		objcache_malloc_alloc_zero, objcache_malloc_free,
		&autofs_request_args);

	autofs_node_objcache = objcache_create("autofs_node", 0, 0,
		NULL, NULL, NULL,
		objcache_malloc_alloc_zero, objcache_malloc_free,
		&autofs_node_args);

	TAILQ_INIT(&autofs_softc->sc_requests);
	cv_init(&autofs_softc->sc_cv, "autofscv");
	mtx_init(&autofs_softc->sc_lock, "autofssclk");
	autofs_softc->sc_dev_opened = false;

	autofs_softc->sc_cdev = make_dev(&autofs_ops, 0, UID_ROOT, GID_OPERATOR,
	    0640, "autofs");
	if (autofs_softc->sc_cdev == NULL) {
		AUTOFS_WARN("failed to create device node");
		objcache_destroy(autofs_request_objcache);
		objcache_destroy(autofs_node_objcache);
		kfree(autofs_softc, M_AUTOFS);
		return (ENODEV);
	}
	autofs_softc->sc_cdev->si_drv1 = autofs_softc;

	return (0);
}

static int
autofs_uninit(struct vfsconf *vfsp)
{
	mtx_lock_ex_quick(&autofs_softc->sc_lock);
	if (autofs_softc->sc_dev_opened) {
		mtx_unlock_ex(&autofs_softc->sc_lock);
		return (EBUSY);
	}

	if (autofs_softc->sc_cdev != NULL)
		destroy_dev(autofs_softc->sc_cdev);

	objcache_destroy(autofs_request_objcache);
	objcache_destroy(autofs_node_objcache);

	mtx_unlock_ex(&autofs_softc->sc_lock);

	kfree(autofs_softc, M_AUTOFS); /* race with open */
	autofs_softc = NULL;

	return (0);
}

static int
autofs_mount(struct mount *mp, char *mntpt, caddr_t data, struct ucred *cred)
{
	struct autofs_mount_info info;
	struct autofs_mount *amp;
	struct statfs *sbp = &mp->mnt_stat;
	int error;

	if (mp->mnt_flag & MNT_UPDATE) {
		autofs_flush(VFSTOAUTOFS(mp));
		return (0);
	}

	error = copyin(data, &info, sizeof(info));
	if (error)
		return (error);

	/*
	 * Copy-in ->f_mntfromname string.
	 */
	memset(sbp->f_mntfromname, 0, sizeof(sbp->f_mntfromname));
	error = copyinstr(info.from, sbp->f_mntfromname,
	    sizeof(sbp->f_mntfromname), NULL);
	if (error)
		return (error);
	/*
	 * Copy-in ->f_mntonname string.
	 */
	memset(sbp->f_mntonname, 0, sizeof(sbp->f_mntonname));
	error = copyinstr(mntpt, sbp->f_mntonname, sizeof(sbp->f_mntonname),
	    NULL);
	if (error)
		return (error);

	/*
	 * Allocate the autofs mount.
	 */
	amp = kmalloc(sizeof(*amp), M_AUTOFS, M_WAITOK | M_ZERO);
	mp->mnt_data = (qaddr_t)amp;
	strlcpy(amp->am_from, sbp->f_mntfromname, sizeof(amp->am_from));
	strlcpy(amp->am_on, sbp->f_mntonname, sizeof(amp->am_on));

	/*
	 * Copy-in master_options string.
	 */
	error = copyinstr(info.master_options, amp->am_options,
	    sizeof(amp->am_options), NULL);
	if (error)
		goto fail;
	/*
	 * Copy-in master_prefix string.
	 */
	error = copyinstr(info.master_prefix, amp->am_prefix,
	    sizeof(amp->am_prefix), NULL);
	if (error)
		goto fail;

	/*
	 * Initialize the autofs mount.
	 */
	mtx_init(&amp->am_lock, "autofsmnlk");
	amp->am_last_ino = AUTOFS_ROOTINO;

	mtx_lock_ex_quick(&amp->am_lock);
	error = autofs_node_new(NULL, amp, ".", -1, &amp->am_root);
	mtx_unlock_ex(&amp->am_lock);
	KKASSERT(error == 0);
	KKASSERT(amp->am_root->an_ino == AUTOFS_ROOTINO);

	vfs_getnewfsid(mp);
	vfs_add_vnodeops(mp, &autofs_vnode_vops, &mp->mnt_vn_norm_ops);

	VFS_STATFS(mp, &mp->mnt_stat, cred);

	return (0);

fail:
	kfree(amp, M_AUTOFS);
	return (error);
}

static int
autofs_unmount(struct mount *mp, int mntflags)
{
	struct autofs_mount *amp = VFSTOAUTOFS(mp);
	int error, flags;

	flags = 0;
	if (mntflags & MNT_FORCE)
		flags |= FORCECLOSE;
	error = vflush(mp, 0, flags);
	if (error) {
		AUTOFS_DEBUG("vflush failed with error %d", error);
		return (error);
	}

	/*
	 * All vnodes are gone, and new one will not appear - so,
	 * no new triggerings.
	 */
	for (;;) {
		struct autofs_request *ar;
		int dummy;
		bool found;

		found = false;
		mtx_lock_ex_quick(&autofs_softc->sc_lock);
		TAILQ_FOREACH(ar, &autofs_softc->sc_requests, ar_next) {
			if (ar->ar_mount != amp)
				continue;
			ar->ar_error = ENXIO;
			ar->ar_done = true;
			ar->ar_in_progress = false;
			found = true;
		}
		if (found == false) {
			mtx_unlock_ex(&autofs_softc->sc_lock);
			break;
		}

		cv_broadcast(&autofs_softc->sc_cv);
		mtx_unlock_ex(&autofs_softc->sc_lock);

		tsleep(&dummy, 0, "autofs_umount", hz);
	}

	mtx_lock_ex_quick(&amp->am_lock);
	while (!RB_EMPTY(&amp->am_root->an_children)) {
		struct autofs_node *anp;
		/*
		 * Force delete all nodes when more than one level of
		 * directories are created via indirect map. Autofs doesn't
		 * support rmdir(2), thus this is the only way to get out.
		 */
		anp = RB_MIN(autofs_node_tree, &amp->am_root->an_children);
		while (!RB_EMPTY(&anp->an_children))
			anp = RB_MIN(autofs_node_tree, &anp->an_children);
		autofs_node_delete(anp);
	}
	autofs_node_delete(amp->am_root);
	mp->mnt_data = NULL;
	mtx_unlock_ex(&amp->am_lock);

	mtx_uninit(&amp->am_lock);

	kfree(amp, M_AUTOFS);

	return (0);
}

static int
autofs_root(struct mount *mp, struct vnode **vpp)
{
	struct autofs_mount *amp = VFSTOAUTOFS(mp);
	int error;

	KASSERT(amp->am_root, ("no root node"));

	error = autofs_node_vn(amp->am_root, mp, LK_EXCLUSIVE, vpp);
	if (error == 0) {
		struct vnode *vp = *vpp;
		vp->v_flag |= VROOT;
		KKASSERT(vp->v_type == VDIR);
	}

	return (error);
}

static int
autofs_statfs(struct mount *mp, struct statfs *sbp, struct ucred *cred)
{
	sbp->f_bsize = S_BLKSIZE;
	sbp->f_iosize = 0;
	sbp->f_blocks = 0;
	sbp->f_bfree = 0;
	sbp->f_bavail = 0;
	sbp->f_files = 0;
	sbp->f_ffree = 0;

	return (0);
}

static int
autofs_statvfs(struct mount *mp, struct statvfs *sbp, struct ucred *cred)
{
	sbp->f_bsize = S_BLKSIZE;
	sbp->f_frsize = 0;
	sbp->f_blocks = 0;
	sbp->f_bfree = 0;
	sbp->f_bavail = 0;
	sbp->f_files = 0;
	sbp->f_ffree = 0;

	return (0);
}

static struct vfsops autofs_vfsops = {
	.vfs_flags =		0,
	.vfs_mount =		autofs_mount,
	.vfs_unmount =		autofs_unmount,
	.vfs_root =		autofs_root,
	.vfs_statfs =		autofs_statfs,
	.vfs_statvfs =		autofs_statvfs,
	.vfs_init =		autofs_init,
	.vfs_uninit =		autofs_uninit,
};

VFS_SET(autofs_vfsops, autofs, VFCF_SYNTHETIC | VFCF_MPSAFE);
MODULE_VERSION(autofs, 1);
