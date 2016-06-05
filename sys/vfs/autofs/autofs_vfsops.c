/*-
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

extern struct autofs_softc	*autofs_softc;

static int
autofs_mount(struct mount *mp, char *mntpt, caddr_t data, struct ucred *cred)
{
	struct autofs_mount_info info;
	struct autofs_mount *amp;
	struct statfs *sbp = &mp->mnt_stat;
	char buf[MAXPATHLEN];
	int error;

	if (mp->mnt_flag & MNT_UPDATE) {
		autofs_flush(VFSTOAUTOFS(mp));
		return (0);
	}

	error = copyin(data, &info, sizeof(info));
	if (error)
		return (error);

	memset(sbp->f_mntfromname, 0, sizeof(sbp->f_mntfromname));
	error = copyinstr(info.from, sbp->f_mntfromname,
	    sizeof(sbp->f_mntfromname), NULL);
	if (error)
		return (error);

	memset(sbp->f_mntonname, 0, sizeof(sbp->f_mntonname));
	error = copyinstr(mntpt, sbp->f_mntonname,
	    sizeof(sbp->f_mntonname), NULL);
	if (error)
		return (error);

	amp = kmalloc(sizeof(*amp), M_AUTOFS, M_WAITOK | M_ZERO);
	mp->mnt_data = (qaddr_t)amp;
	amp->am_mp = mp;
	strlcpy(amp->am_from, sbp->f_mntfromname, sizeof(amp->am_from));
	strlcpy(amp->am_on, sbp->f_mntonname, sizeof(amp->am_on));

	memset(buf, 0, sizeof(buf));
	error = copyinstr(info.master_options, buf, sizeof(buf), NULL);
	if (error)
		goto fail;
	strlcpy(amp->am_options, buf, sizeof(amp->am_options));

	memset(buf, 0, sizeof(buf));
	error = copyinstr(info.master_prefix, buf, sizeof(buf), NULL);
	if (error)
		goto fail;
	strlcpy(amp->am_prefix, buf, sizeof(amp->am_prefix));

	lockinit(&amp->am_lock, "autofsmnlk", 0, 0);
	amp->am_last_ino = AUTOFS_ROOTINO;

	vfs_getnewfsid(mp);
	vfs_add_vnodeops(mp, &autofs_vnode_vops, &mp->mnt_vn_norm_ops);

	lockmgr(&amp->am_lock, LK_EXCLUSIVE);
	error = autofs_node_new(NULL, amp, ".", -1, &amp->am_root);
	lockmgr(&amp->am_lock, LK_RELEASE);
	KKASSERT(error == 0);
	KKASSERT(amp->am_root->an_ino == AUTOFS_ROOTINO);

	autofs_statfs(mp, sbp, cred);

	return (0);

fail:
	kfree(amp, M_AUTOFS);
	return (error);
}

static int
autofs_unmount(struct mount *mp, int mntflags)
{
	struct autofs_mount *amp = VFSTOAUTOFS(mp);
	struct autofs_node *anp;
	struct autofs_request *ar;
	int error, flags, dummy;
	bool found;

	flags = 0;
	if (mntflags & MNT_FORCE)
		flags |= FORCECLOSE;
	error = vflush(mp, 0, flags);
	if (error) {
		AUTOFS_WARN("vflush failed with error %d", error);
		return (error);
	}

	/*
	 * All vnodes are gone, and new one will not appear - so,
	 * no new triggerings.
	 */
	for (;;) {
		found = false;
		lockmgr(&autofs_softc->sc_lock, LK_EXCLUSIVE);
		TAILQ_FOREACH(ar, &autofs_softc->sc_requests, ar_next) {
			if (ar->ar_mount != amp)
				continue;
			ar->ar_error = ENXIO;
			ar->ar_done = true;
			ar->ar_in_progress = false;
			found = true;
		}
		if (found == false) {
			lockmgr(&autofs_softc->sc_lock, LK_RELEASE);
			break;
		}

		cv_broadcast(&autofs_softc->sc_cv);
		lockmgr(&autofs_softc->sc_lock, LK_RELEASE);

		tsleep(&dummy, 0, "autofs_umount", hz);
	}

	lockmgr(&amp->am_lock, LK_EXCLUSIVE);
	while (!RB_EMPTY(&amp->am_root->an_children)) {
		anp = RB_MIN(autofs_node_tree, &amp->am_root->an_children);
		autofs_node_delete(anp);
	}
	autofs_node_delete(amp->am_root);
	mp->mnt_data = NULL;
	lockmgr(&amp->am_lock, LK_RELEASE);

	lockuninit(&amp->am_lock);

	kfree(amp, M_AUTOFS);

	return (0);
}

static int
autofs_root(struct mount *mp, struct vnode **vpp)
{
	struct autofs_mount *amp = VFSTOAUTOFS(mp);
	int error;

	if (amp->am_root == NULL) {
		AUTOFS_FATAL("called without root node %p", mp);
		print_backtrace(-1);
		*vpp = NULL;
		error = EINVAL;
	} else {
		error = autofs_node_vn(amp->am_root, mp, LK_EXCLUSIVE, vpp);
		(*vpp)->v_flag |= VROOT;
		KKASSERT((*vpp)->v_type == VDIR);
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
	.vfs_mount =		autofs_mount,
	.vfs_unmount =		autofs_unmount,
	.vfs_root =		autofs_root,
	.vfs_statfs =		autofs_statfs,
	.vfs_statvfs =		autofs_statvfs,
	.vfs_init =		autofs_init,
	.vfs_uninit =		autofs_uninit,
#if 0
	.vfs_vptofh =		NULL,
	.vfs_fhtovp =		NULL,
	.vfs_checkexp =		NULL,
#endif
};

VFS_SET(autofs_vfsops, autofs, VFCF_SYNTHETIC | VFCF_NETWORK);
MODULE_VERSION(autofs, 1);
