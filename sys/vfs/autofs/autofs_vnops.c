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
#include <sys/dirent.h>
#include <sys/namei.h>
#include <sys/nlookup.h>
#include <sys/mountctl.h>

#include "autofs.h"

static int	autofs_trigger_vn(struct vnode *vp, const char *path,
		    int pathlen, struct vnode **newvp);

extern struct autofs_softc	*autofs_softc;

static __inline int
autofs_trigger_vn_dir(struct vnode *vp, struct vnode **newvp)
{
	return (autofs_trigger_vn(vp, "", 0, newvp));
}

static __inline size_t
autofs_dirent_reclen(const char *name)
{
	return (_DIRENT_RECLEN(strlen(name)));
}

static int
test_fs_root(struct vnode *vp)
{
	int error;

	if ((error = vget(vp, LK_SHARED)) != 0) {
		AUTOFS_WARN("vget failed with error %d", error);
		return (1);
	}

	if (((vp->v_flag & VROOT) == 0) || (vp->v_tag == VT_AUTOFS)) {
		vput(vp);
		return (1);
	}

	return (0);
}

static int
nlookup_fs_root(struct autofs_node *anp, struct vnode **vpp)
{
	struct vnode *vp;
	struct nlookupdata nd;
	char *path;
	int error;

	path = autofs_path(anp);

	error = nlookup_init(&nd, path, UIO_SYSSPACE, NLC_FOLLOW);
	if (error == 0) {
		error = nlookup(&nd);
		if (error == 0) {
			vp = nd.nl_nch.ncp->nc_vp;
			error = test_fs_root(vp);
			if (error == 0)
				*vpp = vp;
		}
	}
	nlookup_done(&nd);
	kfree(path, M_AUTOFS);

	return (error);
}

static int
autofs_access(struct vop_access_args *ap)
{
	/*
	 * Nothing to do here; the only kind of access control
	 * needed is in autofs_mkdir().
	 */
	return (0);
}

static int
autofs_getattr(struct vop_getattr_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct vattr *vap = ap->a_vap;
	struct autofs_node *anp = VTOI(vp);

	KASSERT(vp->v_type == VDIR, ("!VDIR"));

	/*
	 * The reason we must do this is that some tree-walking software,
	 * namely fts(3), assumes that stat(".") results will not change
	 * between chdir("subdir") and chdir(".."), and fails with ENOENT
	 * otherwise.
	 * XXX: Not supported on DragonFly.
	 */
	if (autofs_mount_on_stat)
		AUTOFS_WARN("vfs.autofs.mount_on_stat is not supported");

	vap->va_type = VDIR;
	vap->va_mode = 0755;
	vap->va_nlink = 3; /* XXX: FreeBSD had it like this */
	vap->va_uid = 0;
	vap->va_gid = 0;
	vap->va_fsid = vp->v_mount->mnt_stat.f_fsid.val[0];
	vap->va_fileid = anp->an_ino;
	vap->va_size = S_BLKSIZE;
	vap->va_blocksize = S_BLKSIZE;
	vap->va_mtime = anp->an_ctime;
	vap->va_atime = anp->an_ctime;
	vap->va_ctime = anp->an_ctime;
	vap->va_gen = 0;
	vap->va_flags = 0;
	vap->va_rmajor = 0;
	vap->va_rminor = 0;
	vap->va_bytes = S_BLKSIZE;
	vap->va_filerev = 0;
	vap->va_spare = 0;

	return (0);
}

static int
autofs_trigger_vn(struct vnode *vp, const char *path, int pathlen,
    struct vnode **newvp)
{
	struct autofs_node *anp = VTOI(vp);
	struct vnode *nvp = NULL;
	int error;

	KKASSERT(!vn_islocked(vp));

	if (test_fs_root(vp) == 0)
		goto mounted;

	/*
	 * Don't remove this.  Without having this extra nlookup,
	 * automountd tries to mount the target filesystem twice
	 * and the second attempt to mount returns an error.
	 */
	if (nlookup_fs_root(anp, &nvp) == 0)
		goto mounted;

	lockmgr(&autofs_softc->sc_lock, LK_EXCLUSIVE);
	error = autofs_trigger(anp, path, pathlen);
	lockmgr(&autofs_softc->sc_lock, LK_RELEASE);

	if (error)
		return (error);

	if (nlookup_fs_root(anp, &nvp))
		return (0);

	/*
	 * If the operation that succeeded was mount, then mark
	 * the node as non-cached.  Otherwise, if someone unmounts
	 * the filesystem before the cache times out, we will fail
	 * to trigger.
	 */
	autofs_node_uncache(anp);
mounted:
	*newvp = nvp;
	KKASSERT(vn_islocked(*newvp));

	return (0);
}

static int
autofs_nresolve(struct vop_nresolve_args *ap)
{
	struct vnode *vp = NULL;
	struct vnode *dvp = ap->a_dvp;
	struct nchandle *nch = ap->a_nch;
	struct namecache *ncp = nch->ncp;
	struct autofs_mount *amp = VFSTOAUTOFS(dvp->v_mount);
	struct autofs_node *anp = VTOI(dvp);
	struct autofs_node *child = NULL;
	int error;

	if (autofs_cached(anp, ncp->nc_name, ncp->nc_nlen) == false &&
	    autofs_ignore_thread() == false) {
		struct vnode *newvp = NULL;

		cache_hold(nch);
		cache_unlock(nch);
		error = autofs_trigger_vn(dvp,
		    ncp->nc_name, ncp->nc_nlen, &newvp);
		cache_lock(nch);
		cache_drop(nch);

		if (error)
			return (error);
		if (newvp != NULL) {
			KKASSERT(newvp->v_tag != VT_AUTOFS);
			vput(newvp);
			return (ESTALE);
		}
		return (0);
	}

	lockmgr(&amp->am_lock, LK_SHARED);
	error = autofs_node_find(anp, ncp->nc_name, ncp->nc_nlen, &child);
	lockmgr(&amp->am_lock, LK_RELEASE);

	if (error) {
		cache_setvp(nch, NULL);
		return (0);
	}

	error = autofs_node_vn(child, dvp->v_mount, LK_EXCLUSIVE, &vp);
	if (error == 0) {
		KKASSERT(vn_islocked(vp));
		vn_unlock(vp);
		cache_setvp(nch, vp);
		vrele(vp);
		return (0);
	}

	return (error);
}

static int
autofs_nmkdir(struct vop_nmkdir_args *ap)
{
	struct vnode *vp = NULL;
	struct vnode *dvp = ap->a_dvp;
	struct nchandle *nch = ap->a_nch;
	struct namecache *ncp = nch->ncp;
	struct autofs_mount *amp = VFSTOAUTOFS(dvp->v_mount);
	struct autofs_node *anp = VTOI(dvp);
	struct autofs_node *child = NULL;
	int error;

	/*
	 * Do not allow mkdir() if the calling thread is not
	 * automountd(8) descendant.
	 */
	if (autofs_ignore_thread() == false)
		return (EPERM);

	lockmgr(&amp->am_lock, LK_EXCLUSIVE);
	error = autofs_node_new(anp, amp, ncp->nc_name, ncp->nc_nlen, &child);
	lockmgr(&amp->am_lock, LK_RELEASE);
	KKASSERT(error == 0);

	error = autofs_node_vn(child, dvp->v_mount, LK_EXCLUSIVE, &vp);
	if (error == 0) {
		KKASSERT(vn_islocked(vp));
		cache_setunresolved(nch);
		cache_setvp(nch, vp);
		*(ap->a_vpp) = vp;
		return (0);
	}

	return (error);
}

static int
autofs_readdir_one(struct uio *uio, const char *name, ino_t ino,
    size_t *reclenp)
{
	int error = 0;

	if (reclenp != NULL)
		*reclenp = autofs_dirent_reclen(name);

	if (vop_write_dirent(&error, uio, ino, DT_DIR, strlen(name), name))
		return (EINVAL);

	return (error);
}

static int
autofs_readdir(struct vop_readdir_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct autofs_mount *amp = VFSTOAUTOFS(vp->v_mount);
	struct autofs_node *anp = VTOI(vp);
	struct autofs_node *child;
	struct uio *uio = ap->a_uio;
	ssize_t initial_resid = ap->a_uio->uio_resid;
	size_t reclen, reclens;
	int error;

	KASSERT(vp->v_type == VDIR, ("!VDIR"));

	if (autofs_cached(anp, NULL, 0) == false &&
	    autofs_ignore_thread() == false) {
		struct vnode *newvp = NULL;
		error = autofs_trigger_vn_dir(vp, &newvp);
		if (error)
			return (error);
		if (newvp != NULL) {
			KKASSERT(newvp->v_tag != VT_AUTOFS);
			vn_unlock(newvp);
			error = VOP_READDIR(newvp, ap->a_uio, ap->a_cred,
			    ap->a_eofflag, ap->a_ncookies, ap->a_cookies);
			vrele(newvp);
			return (error);
		}
		/* FALLTHROUGH */
	}

	if (uio->uio_offset < 0)
		return (EINVAL);

	if (ap->a_eofflag != NULL)
		*ap->a_eofflag = FALSE;

	/*
	 * Write out the directory entry for ".".
	 */
	if (uio->uio_offset == 0) {
		error = autofs_readdir_one(uio, ".", anp->an_ino, &reclen);
		if (error)
			goto out;
	}
	reclens = autofs_dirent_reclen(".");

	/*
	 * Write out the directory entry for "..".
	 */
	if (uio->uio_offset <= reclens) {
		if (uio->uio_offset != reclens)
			return (EINVAL);
		error = autofs_readdir_one(uio, "..",
		    (anp->an_parent ? anp->an_parent->an_ino : anp->an_ino),
		    &reclen);
		if (error)
			goto out;
	}
	reclens += autofs_dirent_reclen("..");

	/*
	 * Write out the directory entries for subdirectories.
	 */
	lockmgr(&amp->am_lock, LK_SHARED);
	RB_FOREACH(child, autofs_node_tree, &anp->an_children) {
		/*
		 * Check the offset to skip entries returned by previous
		 * calls to getdents().
		 */
		if (uio->uio_offset > reclens) {
			reclens += autofs_dirent_reclen(child->an_name);
			continue;
		}

		/*
		 * Prevent seeking into the middle of dirent.
		 */
		if (uio->uio_offset != reclens) {
			lockmgr(&amp->am_lock, LK_RELEASE);
			return (EINVAL);
		}

		error = autofs_readdir_one(uio, child->an_name,
		    child->an_ino, &reclen);
		reclens += reclen;
		if (error) {
			lockmgr(&amp->am_lock, LK_RELEASE);
			goto out;
		}
	}
	lockmgr(&amp->am_lock, LK_RELEASE);

	if (ap->a_eofflag != NULL)
		*ap->a_eofflag = TRUE;

	return (0);
out:
	/*
	 * Return error if the initial buffer was too small to do anything.
	 */
	if (uio->uio_resid == initial_resid)
		return (error);

	/*
	 * Don't return an error if we managed to copy out some entries.
	 */
	if (uio->uio_resid < reclen)
		return (0);

	return (error);
}

static int
autofs_reclaim(struct vop_reclaim_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct autofs_node *anp = VTOI(vp);

	/*
	 * We do not free autofs_node here; instead we are
	 * destroying them in autofs_node_delete().
	 */
	lockmgr(&anp->an_vnode_lock, LK_EXCLUSIVE);
	anp->an_vnode = NULL;
	vp->v_data = NULL;
	lockmgr(&anp->an_vnode_lock, LK_RELEASE);

	return (0);
}

static int
autofs_mountctl(struct vop_mountctl_args *ap)
{
	struct mount *mp;
#if 0
	struct autofs_mount *amp;
#endif
	int res;

	mp = ap->a_head.a_ops->head.vv_mount;
	lwkt_gettoken(&mp->mnt_token);

	switch (ap->a_op) {
#if 0
	case MOUNTCTL_SET_EXPORT:
		amp = (struct autofs_mount*)mp->mnt_data;
		if (ap->a_ctllen != sizeof(struct export_args))
			res = (EINVAL);
		else
			res = vfs_export(mp, &amp->am_export,
			    (const struct export_args*)ap->a_ctl);
		break;
#endif
	default:
		res = vop_stdmountctl(ap);
		break;
	}

	lwkt_reltoken(&mp->mnt_token);
	return (res);
}

static int
autofs_print(struct vop_print_args *ap)
{
	struct autofs_node *anp = VTOI(ap->a_vp);

	kprintf("tag VT_AUTOFS, node %p, ino %jd, name %s, cached %d, retries %d, wildcards %d\n",
	    anp, (intmax_t)anp->an_ino, anp->an_name, anp->an_cached, anp->an_retries, anp->an_wildcards);

	return (0);
}

struct vop_ops autofs_vnode_vops = {
	.vop_default =		vop_defaultop,
	.vop_getpages =		vop_stdgetpages,
	.vop_putpages =		vop_stdputpages,
	.vop_access =		autofs_access,
	.vop_getattr =		autofs_getattr,
	.vop_nresolve =		autofs_nresolve,
	.vop_nmkdir =		autofs_nmkdir,
	.vop_readdir =		autofs_readdir,
	.vop_reclaim =		autofs_reclaim,
	.vop_mountctl =		autofs_mountctl,
	.vop_print =		autofs_print,
#if 0
	.vop_nlookupdotdot =	NULL,
#endif
};

int
autofs_node_new(struct autofs_node *parent, struct autofs_mount *amp,
    const char *name, int namelen, struct autofs_node **anpp)
{
	struct autofs_node *anp;

	AUTOFS_ASSERT_XLOCKED(amp);

	if (parent != NULL) {
		AUTOFS_ASSERT_XLOCKED(parent->an_mount);
		KASSERT(autofs_node_find(parent, name, namelen, NULL) == ENOENT,
		    ("node \"%s\" already exists", name));
	}

	anp = objcache_get(autofs_node_objcache, M_WAITOK);
	if (namelen >= 0)
		anp->an_name = kstrndup(name, namelen, M_AUTOFS);
	else
		anp->an_name = kstrdup(name, M_AUTOFS);
	anp->an_ino = amp->am_last_ino++;
	callout_init(&anp->an_callout);
	lockinit(&anp->an_vnode_lock, "autofsvnlk", 0, 0);
	getnanotime(&anp->an_ctime);
	anp->an_parent = parent;
	anp->an_mount = amp;
	anp->an_vnode = NULL;
	anp->an_cached = false;
	anp->an_wildcards = false;
	anp->an_retries = 0;
	if (parent != NULL)
		RB_INSERT(autofs_node_tree, &parent->an_children, anp);
	RB_INIT(&anp->an_children);

	*anpp = anp;

	return (0);
}

int
autofs_node_find(struct autofs_node *parent, const char *name,
    int namelen, struct autofs_node **anpp)
{
	struct autofs_node *anp, find;
	int error;

	AUTOFS_ASSERT_LOCKED(parent->an_mount);

	if (namelen >= 0)
		find.an_name = kstrndup(name, namelen, M_AUTOFS);
	else
		find.an_name = kstrdup(name, M_AUTOFS);

	anp = RB_FIND(autofs_node_tree, &parent->an_children, &find);
	if (anp != NULL) {
		error = 0;
		if (anpp != NULL)
			*anpp = anp;
	} else {
		error = ENOENT;
	}

	kfree(find.an_name, M_AUTOFS);

	return (error);
}

void
autofs_node_delete(struct autofs_node *anp)
{
	AUTOFS_ASSERT_XLOCKED(anp->an_mount);
	KASSERT(RB_EMPTY(&anp->an_children), ("have children"));

	callout_drain(&anp->an_callout);

	if (anp->an_parent != NULL)
		RB_REMOVE(autofs_node_tree, &anp->an_parent->an_children, anp);

	lockuninit(&anp->an_vnode_lock);
	kfree(anp->an_name, M_AUTOFS);
	objcache_put(autofs_node_objcache, anp);
}

int
autofs_node_vn(struct autofs_node *anp, struct mount *mp, int flags,
    struct vnode **vpp)
{
	struct vnode *vp = NULL;
	int error;
retry:
	AUTOFS_ASSERT_UNLOCKED(anp->an_mount);
	lockmgr(&anp->an_vnode_lock, LK_EXCLUSIVE);

	vp = anp->an_vnode;
	if (vp != NULL) {
		vhold(vp);
		lockmgr(&anp->an_vnode_lock, LK_RELEASE);

		error = vget(vp, flags | LK_RETRY);
		if (error) {
			AUTOFS_WARN("vget failed with error %d", error);
			vdrop(vp);
			goto retry;
		}
		vdrop(vp);
		*vpp = vp;
		return (0);
	}

	lockmgr(&anp->an_vnode_lock, LK_RELEASE);

	error = getnewvnode(VT_AUTOFS, mp, &vp, VLKTIMEOUT, LK_CANRECURSE);
	if (error)
		return (error);
	vp->v_type = VDIR;
	vp->v_data = anp;

	KASSERT(anp->an_vnode == NULL, ("lost race"));
	anp->an_vnode = vp;
	*vpp = vp;

	return (0);
}
