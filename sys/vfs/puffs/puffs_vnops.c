/*	$NetBSD: puffs_vnops.c,v 1.154 2011/07/04 08:07:30 manu Exp $	*/

/*
 * Copyright (c) 2005, 2006, 2007  Antti Kantee.  All Rights Reserved.
 *
 * Development of this software was supported by the
 * Google Summer of Code program and the Ulla Tuominen Foundation.
 * The Google SoC project was mentored by Bill Studenmund.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/buf.h>
#include <sys/lockf.h>
#include <sys/malloc.h>
#include <sys/mount.h>
#include <sys/namei.h>
#include <sys/vnode.h>
#include <sys/proc.h>

#include <vfs/fifofs/fifo.h>

#include <vfs/puffs/puffs_msgif.h>
#include <vfs/puffs/puffs_sys.h>

int (**puffs_vnodeop_p)(void *);

#define ERROUT(err)							\
do {									\
	error = err;							\
	goto out;							\
} while (/*CONSTCOND*/0)

static int callremove(struct puffs_mount *, puffs_cookie_t, puffs_cookie_t,
			    struct namecache *, struct ucred *);
static int callrmdir(struct puffs_mount *, puffs_cookie_t, puffs_cookie_t,
			   struct namecache *, struct ucred *);
static void callinactive(struct puffs_mount *, puffs_cookie_t, int);
static void callreclaim(struct puffs_mount *, puffs_cookie_t);
static int  flushvncache(struct vnode *, int);


#define PUFFS_ABORT_LOOKUP	1
#define PUFFS_ABORT_CREATE	2
#define PUFFS_ABORT_MKNOD	3
#define PUFFS_ABORT_MKDIR	4
#define PUFFS_ABORT_SYMLINK	5

/*
 * Press the pani^Wabort button!  Kernel resource allocation failed.
 */
static void
puffs_abortbutton(struct puffs_mount *pmp, int what,
	puffs_cookie_t dck, puffs_cookie_t ck,
	struct namecache *ncp, struct ucred *cred)
{

	switch (what) {
	case PUFFS_ABORT_CREATE:
	case PUFFS_ABORT_MKNOD:
	case PUFFS_ABORT_SYMLINK:
		callremove(pmp, dck, ck, ncp, cred);
		break;
	case PUFFS_ABORT_MKDIR:
		callrmdir(pmp, dck, ck, ncp, cred);
		break;
	}

	callinactive(pmp, ck, 0);
	callreclaim(pmp, ck);
}

/*
 * Begin vnode operations.
 *
 * A word from the keymaster about locks: generally we don't want
 * to use the vnode locks at all: it creates an ugly dependency between
 * the userlandia file server and the kernel.  But we'll play along with
 * the kernel vnode locks for now.  However, even currently we attempt
 * to release locks as early as possible.  This is possible for some
 * operations which a) don't need a locked vnode after the userspace op
 * and b) return with the vnode unlocked.  Theoretically we could
 * unlock-do op-lock for others and order the graph in userspace, but I
 * don't want to think of the consequences for the time being.
 */
static int
puffs_vnop_lookup(struct vop_nresolve_args *ap)
{
	PUFFS_MSG_VARS(vn, lookup);
	struct puffs_mount *pmp = MPTOPUFFSMP(ap->a_dvp->v_mount);
	struct nchandle *nch = ap->a_nch;
	struct namecache *ncp = nch->ncp;
	struct ucred *cred = ap->a_cred;
	struct vnode *vp = NULL, *dvp = ap->a_dvp;
	struct puffs_node *dpn;
	int error;

	DPRINTF(("puffs_lookup: \"%s\", parent vnode %p\n",
	    ncp->nc_name, dvp));

	PUFFS_MSG_ALLOC(vn, lookup);
	puffs_makecn(&lookup_msg->pvnr_cn, &lookup_msg->pvnr_cn_cred,
	    ncp, cred);

	puffs_msg_setinfo(park_lookup, PUFFSOP_VN,
	    PUFFS_VN_LOOKUP, VPTOPNC(dvp));
	PUFFS_MSG_ENQUEUEWAIT2(pmp, park_lookup, dvp->v_data, NULL, error);
	DPRINTF(("puffs_lookup: return of the userspace, part %d\n", error));
	if (error) {
		error = checkerr(pmp, error, __func__);
		if (error == ENOENT)
			cache_setvp(nch, NULL);
		goto out;
	}

	/*
	 * Check that we don't get our parent node back, that would cause
	 * a pretty obvious deadlock.
	 */
	dpn = VPTOPP(dvp);
	if (lookup_msg->pvnr_newnode == dpn->pn_cookie) {
		puffs_senderr(pmp, PUFFS_ERR_LOOKUP, EINVAL,
		    "lookup produced parent cookie", lookup_msg->pvnr_newnode);
		error = EPROTO;
		goto out;
	}

	error = puffs_cookie2vnode(pmp, lookup_msg->pvnr_newnode, 1, &vp);
	if (error == PUFFS_NOSUCHCOOKIE) {
		error = puffs_getvnode(dvp->v_mount,
		    lookup_msg->pvnr_newnode, lookup_msg->pvnr_vtype,
		    lookup_msg->pvnr_size, &vp);
		if (error) {
			puffs_abortbutton(pmp, PUFFS_ABORT_LOOKUP, VPTOPNC(dvp),
			    lookup_msg->pvnr_newnode, ncp, cred);
			goto out;
		}
	} else if (error) {
		puffs_abortbutton(pmp, PUFFS_ABORT_LOOKUP, VPTOPNC(dvp),
		    lookup_msg->pvnr_newnode, ncp, cred);
		goto out;
	}

 out:
	if (!error && vp != NULL) {
		vn_unlock(vp);
		cache_setvp(nch, vp);
		vrele(vp);
	}
	DPRINTF(("puffs_lookup: returning %d\n", error));
	PUFFS_MSG_RELEASE(lookup);
	return error;
}

static int
puffs_vnop_lookupdotdot(struct vop_nlookupdotdot_args *ap)
{
	PUFFS_MSG_VARS(vn, lookupdotdot);
	struct puffs_mount *pmp = MPTOPUFFSMP(ap->a_dvp->v_mount);
	struct ucred *cred = ap->a_cred;
	struct vnode *vp, *dvp = ap->a_dvp;
	struct puffs_node *dpn;
	int error;

	*ap->a_vpp = NULL;

	DPRINTF(("puffs_lookupdotdot: vnode %p\n", dvp));

	PUFFS_MSG_ALLOC(vn, lookupdotdot);
	puffs_credcvt(&lookupdotdot_msg->pvnr_cred, cred);

	puffs_msg_setinfo(park_lookupdotdot, PUFFSOP_VN,
	    PUFFS_VN_LOOKUPDOTDOT, VPTOPNC(dvp));
	PUFFS_MSG_ENQUEUEWAIT2(pmp, park_lookupdotdot, dvp->v_data, NULL,
	    error);
	DPRINTF(("puffs_lookupdotdot: return of the userspace, part %d\n",
	    error));
	if (error) {
		error = checkerr(pmp, error, __func__);
		goto out;
	}

	/*
	 * Check that we don't get our node back, that would cause
	 * a pretty obvious deadlock.
	 */
	dpn = VPTOPP(dvp);
	if (lookupdotdot_msg->pvnr_newnode == dpn->pn_cookie) {
		puffs_senderr(pmp, PUFFS_ERR_LOOKUP, EINVAL,
		    "lookupdotdot produced the same cookie",
		    lookupdotdot_msg->pvnr_newnode);
		error = EPROTO;
		goto out;
	}

	error = puffs_cookie2vnode(pmp, lookupdotdot_msg->pvnr_newnode,
	    1, &vp);
	if (error == PUFFS_NOSUCHCOOKIE) {
		error = puffs_getvnode(dvp->v_mount,
		    lookupdotdot_msg->pvnr_newnode, VDIR, 0, &vp);
		if (error) {
			puffs_abortbutton(pmp, PUFFS_ABORT_LOOKUP, VPTOPNC(dvp),
			    lookupdotdot_msg->pvnr_newnode, NULL, cred);
			goto out;
		}
	} else if (error) {
		puffs_abortbutton(pmp, PUFFS_ABORT_LOOKUP, VPTOPNC(dvp),
		    lookupdotdot_msg->pvnr_newnode, NULL, cred);
		goto out;
	}

	*ap->a_vpp = vp;
	vn_unlock(vp);

 out:
	DPRINTF(("puffs_lookupdotdot: returning %d %p\n", error, *ap->a_vpp));
	PUFFS_MSG_RELEASE(lookupdotdot);
	return error;
}

static int
puffs_vnop_create(struct vop_ncreate_args *ap)
{
	PUFFS_MSG_VARS(vn, create);
	struct vnode *dvp = ap->a_dvp;
	struct vattr *vap = ap->a_vap;
	struct puffs_node *dpn = VPTOPP(dvp);
	struct nchandle *nch = ap->a_nch;
	struct namecache *ncp = nch->ncp;
	struct ucred *cred = ap->a_cred;
	struct mount *mp = dvp->v_mount;
	struct puffs_mount *pmp = MPTOPUFFSMP(mp);
	int error;

	if (!EXISTSOP(pmp, CREATE))
		return EOPNOTSUPP;

	DPRINTF(("puffs_create: dvp %p, name: %s\n",
	    dvp, ncp->nc_name));

	if (vap->va_type != VREG && vap->va_type != VSOCK)
		return EINVAL;

	if ((error = vget(dvp, LK_EXCLUSIVE)) != 0) {
		DPRINTF(("puffs_vnop_create: EAGAIN on ncp %p %s\n",
		    ncp, ncp->nc_name));
		return EAGAIN;
	}

	PUFFS_MSG_ALLOC(vn, create);
	puffs_makecn(&create_msg->pvnr_cn, &create_msg->pvnr_cn_cred,
	    ncp, cred);
	create_msg->pvnr_va = *ap->a_vap;
	puffs_msg_setinfo(park_create, PUFFSOP_VN,
	    PUFFS_VN_CREATE, VPTOPNC(dvp));
	PUFFS_MSG_ENQUEUEWAIT2(pmp, park_create, dvp->v_data, NULL, error);

	error = checkerr(pmp, error, __func__);
	if (error)
		goto out;

	error = puffs_newnode(mp, dvp, ap->a_vpp,
	    create_msg->pvnr_newnode, vap->va_type);
	if (error)
		puffs_abortbutton(pmp, PUFFS_ABORT_CREATE, dpn->pn_cookie,
		    create_msg->pvnr_newnode, ncp, cred);

 out:
	DPRINTF(("puffs_create: return %d\n", error));
	vput(dvp);
	if (!error) {
		cache_setunresolved(nch);
		cache_setvp(nch, *ap->a_vpp);
	}
	PUFFS_MSG_RELEASE(create);
	return error;
}

static int
puffs_vnop_mknod(struct vop_nmknod_args *ap)
{
	PUFFS_MSG_VARS(vn, mknod);
	struct vnode *dvp = ap->a_dvp;
	struct vattr *vap = ap->a_vap;
	struct puffs_node *dpn = VPTOPP(dvp);
	struct nchandle *nch = ap->a_nch;
	struct namecache *ncp = nch->ncp;
	struct ucred *cred = ap->a_cred;
	struct mount *mp = dvp->v_mount;
	struct puffs_mount *pmp = MPTOPUFFSMP(mp);
	int error;

	if (!EXISTSOP(pmp, MKNOD))
		return EOPNOTSUPP;

	DPRINTF(("puffs_mknod: dvp %p, name: %s\n",
	    dvp, ncp->nc_name));

	if (vap->va_type != VFIFO)
		return EINVAL;

	if ((error = vget(dvp, LK_EXCLUSIVE)) != 0) {
		DPRINTF(("puffs_vnop_mknod: EAGAIN on ncp %p %s\n",
		    ncp, ncp->nc_name));
		return EAGAIN;
	}

	PUFFS_MSG_ALLOC(vn, mknod);
	puffs_makecn(&mknod_msg->pvnr_cn, &mknod_msg->pvnr_cn_cred,
	    ncp, cred);
	mknod_msg->pvnr_va = *ap->a_vap;
	puffs_msg_setinfo(park_mknod, PUFFSOP_VN,
	    PUFFS_VN_MKNOD, VPTOPNC(dvp));

	PUFFS_MSG_ENQUEUEWAIT2(pmp, park_mknod, dvp->v_data, NULL, error);

	error = checkerr(pmp, error, __func__);
	if (error)
		goto out;

	error = puffs_newnode(mp, dvp, ap->a_vpp,
	    mknod_msg->pvnr_newnode, vap->va_type);
	if (error)
		puffs_abortbutton(pmp, PUFFS_ABORT_MKNOD, dpn->pn_cookie,
		    mknod_msg->pvnr_newnode, ncp, cred);

 out:
	vput(dvp);
	if (!error) {
		cache_setunresolved(nch);
		cache_setvp(nch, *ap->a_vpp);
	}
	PUFFS_MSG_RELEASE(mknod);
	return error;
}

static int
puffs_vnop_open(struct vop_open_args *ap)
{
	PUFFS_MSG_VARS(vn, open);
	struct vnode *vp = ap->a_vp;
	struct puffs_mount *pmp = MPTOPUFFSMP(vp->v_mount);
	int mode = ap->a_mode;
	int error;

	DPRINTF(("puffs_open: vp %p, mode 0x%x\n", vp, mode));

	if (vp->v_type == VREG && mode & FWRITE && !EXISTSOP(pmp, WRITE))
		ERROUT(EROFS);

	if (!EXISTSOP(pmp, OPEN))
		ERROUT(0);

	PUFFS_MSG_ALLOC(vn, open);
	open_msg->pvnr_mode = mode;
	puffs_credcvt(&open_msg->pvnr_cred, ap->a_cred);
	puffs_msg_setinfo(park_open, PUFFSOP_VN,
	    PUFFS_VN_OPEN, VPTOPNC(vp));

	PUFFS_MSG_ENQUEUEWAIT2(pmp, park_open, vp->v_data, NULL, error);
	error = checkerr(pmp, error, __func__);

 out:
	DPRINTF(("puffs_open: returning %d\n", error));
	PUFFS_MSG_RELEASE(open);
	if (error)
		return error;
	return vop_stdopen(ap);
}

static int
puffs_vnop_close(struct vop_close_args *ap)
{
	PUFFS_MSG_VARS(vn, close);
	struct vnode *vp = ap->a_vp;
	struct puffs_mount *pmp = MPTOPUFFSMP(vp->v_mount);

	vn_lock(vp, LK_UPGRADE | LK_RETRY);

	if (!EXISTSOP(pmp, CLOSE))
		return vop_stdclose(ap);

	PUFFS_MSG_ALLOC(vn, close);
	puffs_msg_setfaf(park_close);
	close_msg->pvnr_fflag = ap->a_fflag;
	puffs_msg_setinfo(park_close, PUFFSOP_VN,
	    PUFFS_VN_CLOSE, VPTOPNC(vp));

	puffs_msg_enqueue(pmp, park_close);
	PUFFS_MSG_RELEASE(close);
	return vop_stdclose(ap);
}

static int
puffs_vnop_access(struct vop_access_args *ap)
{
	PUFFS_MSG_VARS(vn, access);
	struct vnode *vp = ap->a_vp;
	struct puffs_mount *pmp = MPTOPUFFSMP(vp->v_mount);
	int mode = ap->a_mode;
	int error;

	if (mode & VWRITE) {
		switch (vp->v_type) {
		case VDIR:
		case VLNK:
		case VREG:
			if ((vp->v_mount->mnt_flag & MNT_RDONLY)
			    || !EXISTSOP(pmp, WRITE))
				return EROFS;
			break;
		default:
			break;
		}
	}

	if (!EXISTSOP(pmp, ACCESS))
		return 0;

	PUFFS_MSG_ALLOC(vn, access);
	access_msg->pvnr_mode = ap->a_mode;
	puffs_credcvt(&access_msg->pvnr_cred, ap->a_cred);
	puffs_msg_setinfo(park_access, PUFFSOP_VN,
	    PUFFS_VN_ACCESS, VPTOPNC(vp));

	PUFFS_MSG_ENQUEUEWAIT2(pmp, park_access, vp->v_data, NULL, error);
	error = checkerr(pmp, error, __func__);
	PUFFS_MSG_RELEASE(access);

	return error;
}

static int
puffs_vnop_getattr(struct vop_getattr_args *ap)
{
	PUFFS_MSG_VARS(vn, getattr);
	struct vnode *vp = ap->a_vp;
	struct mount *mp = vp->v_mount;
	struct puffs_mount *pmp = MPTOPUFFSMP(mp);
	struct vattr *vap, *rvap;
	struct puffs_node *pn = VPTOPP(vp);
	int error = 0;

	if (vp->v_type == VBLK || vp->v_type == VCHR)
		return ENOTSUP;

	vap = ap->a_vap;

	PUFFS_MSG_ALLOC(vn, getattr);
	vattr_null(&getattr_msg->pvnr_va);
	puffs_credcvt(&getattr_msg->pvnr_cred, curproc->p_ucred);
	puffs_msg_setinfo(park_getattr, PUFFSOP_VN,
	    PUFFS_VN_GETATTR, VPTOPNC(vp));

	PUFFS_MSG_ENQUEUEWAIT2(pmp, park_getattr, vp->v_data, NULL, error);
	error = checkerr(pmp, error, __func__);
	if (error)
		goto out;

	rvap = &getattr_msg->pvnr_va;

	(void) memcpy(vap, rvap, sizeof(struct vattr));
	vap->va_fsid = mp->mnt_stat.f_fsid.val[0];

	if (pn->pn_stat & PNODE_METACACHE_ATIME)
		vap->va_atime = pn->pn_mc_atime;
	if (pn->pn_stat & PNODE_METACACHE_CTIME)
		vap->va_ctime = pn->pn_mc_ctime;
	if (pn->pn_stat & PNODE_METACACHE_MTIME)
		vap->va_mtime = pn->pn_mc_mtime;
	if (pn->pn_stat & PNODE_METACACHE_SIZE) {
		vap->va_size = pn->pn_mc_size;
	} else {
		if (rvap->va_size != VNOVAL
		    && vp->v_type != VBLK && vp->v_type != VCHR) {
			pn->pn_serversize = rvap->va_size;
			if (vp->v_type == VREG)
				puffs_meta_setsize(vp, rvap->va_size, 0);
		}
	}

 out:
	PUFFS_MSG_RELEASE(getattr);
	return error;
}

#define SETATTR_CHSIZE	0x01
#define SETATTR_ASYNC	0x02
static int
dosetattr(struct vnode *vp, struct vattr *vap, struct ucred *cred, int flags)
{
	PUFFS_MSG_VARS(vn, setattr);
	struct puffs_mount *pmp = MPTOPUFFSMP(vp->v_mount);
	struct puffs_node *pn = VPTOPP(vp);
	int error = 0;

	if ((vp->v_mount->mnt_flag & MNT_RDONLY) &&
	    (vap->va_uid != (uid_t)VNOVAL || vap->va_gid != (gid_t)VNOVAL
	    || vap->va_atime.tv_sec != VNOVAL || vap->va_mtime.tv_sec != VNOVAL
	    || vap->va_mode != (mode_t)VNOVAL))
		return EROFS;

	if ((vp->v_mount->mnt_flag & MNT_RDONLY)
	    && vp->v_type == VREG && vap->va_size != VNOVAL)
		return EROFS;

	/*
	 * Flush metacache first.  If we are called with some explicit
	 * parameters, treat them as information overriding metacache
	 * information.
	 */
	if (pn->pn_stat & PNODE_METACACHE_MASK) {
		if ((pn->pn_stat & PNODE_METACACHE_ATIME)
		    && vap->va_atime.tv_sec == VNOVAL)
			vap->va_atime = pn->pn_mc_atime;
		if ((pn->pn_stat & PNODE_METACACHE_CTIME)
		    && vap->va_ctime.tv_sec == VNOVAL)
			vap->va_ctime = pn->pn_mc_ctime;
		if ((pn->pn_stat & PNODE_METACACHE_MTIME)
		    && vap->va_mtime.tv_sec == VNOVAL)
			vap->va_mtime = pn->pn_mc_mtime;
		if ((pn->pn_stat & PNODE_METACACHE_SIZE)
		    && vap->va_size == VNOVAL)
			vap->va_size = pn->pn_mc_size;

		pn->pn_stat &= ~PNODE_METACACHE_MASK;
	}

	PUFFS_MSG_ALLOC(vn, setattr);
	(void)memcpy(&setattr_msg->pvnr_va, vap, sizeof(struct vattr));
	puffs_credcvt(&setattr_msg->pvnr_cred, cred);
	puffs_msg_setinfo(park_setattr, PUFFSOP_VN,
	    PUFFS_VN_SETATTR, VPTOPNC(vp));
	if (flags & SETATTR_ASYNC)
		puffs_msg_setfaf(park_setattr);

	puffs_msg_enqueue(pmp, park_setattr);
	if ((flags & SETATTR_ASYNC) == 0)
		error = puffs_msg_wait2(pmp, park_setattr, vp->v_data, NULL);
	PUFFS_MSG_RELEASE(setattr);
	if ((flags & SETATTR_ASYNC) == 0) {
		error = checkerr(pmp, error, __func__);
		if (error)
			return error;
	} else {
		error = 0;
	}

	if (vap->va_size != VNOVAL) {
		pn->pn_serversize = vap->va_size;
		if (flags & SETATTR_CHSIZE)
			puffs_meta_setsize(vp, vap->va_size, 0);
	}

	return 0;
}

static int
puffs_vnop_setattr(struct vop_setattr_args *ap)
{
	return dosetattr(ap->a_vp, ap->a_vap, ap->a_cred, SETATTR_CHSIZE);
}

static __inline int
doinact(struct puffs_mount *pmp, int iaflag)
{

	if (EXISTSOP(pmp, INACTIVE))
		if (pmp->pmp_flags & PUFFS_KFLAG_IAONDEMAND)
			if (iaflag || ALLOPS(pmp))
				return 1;
			else
				return 0;
		else
			return 1;
	else
		return 0;
}

static void
callinactive(struct puffs_mount *pmp, puffs_cookie_t ck, int iaflag)
{
	int error;
	PUFFS_MSG_VARS(vn, inactive);

	if (doinact(pmp, iaflag)) {
		PUFFS_MSG_ALLOC(vn, inactive);
		puffs_msg_setinfo(park_inactive, PUFFSOP_VN,
		    PUFFS_VN_INACTIVE, ck);

		PUFFS_MSG_ENQUEUEWAIT(pmp, park_inactive, error);
		PUFFS_MSG_RELEASE(inactive);
	}
}

/* XXX: callinactive can't setback */
static int
puffs_vnop_inactive(struct vop_inactive_args *ap)
{
	PUFFS_MSG_VARS(vn, inactive);
	struct vnode *vp = ap->a_vp;
	struct puffs_mount *pmp = MPTOPUFFSMP(vp->v_mount);
	struct puffs_node *pnode = VPTOPP(vp);

	flushvncache(vp, MNT_NOWAIT);

	if (doinact(pmp, pnode->pn_stat & PNODE_DOINACT)) {
		/*
		 * do not wait for reply from userspace, otherwise it may
		 * deadlock.
		 */

		PUFFS_MSG_ALLOC(vn, inactive);
		puffs_msg_setfaf(park_inactive);
		puffs_msg_setinfo(park_inactive, PUFFSOP_VN,
		    PUFFS_VN_INACTIVE, VPTOPNC(vp));

		puffs_msg_enqueue(pmp, park_inactive);
		PUFFS_MSG_RELEASE(inactive);
	}
	pnode->pn_stat &= ~PNODE_DOINACT;

	/*
	 * file server thinks it's gone?  then don't be afraid care,
	 * node's life was already all it would ever be
	 */
	if (pnode->pn_stat & PNODE_NOREFS) {
		pnode->pn_stat |= PNODE_DYING;
		vrecycle(vp);
	}

	return 0;
}

static void
callreclaim(struct puffs_mount *pmp, puffs_cookie_t ck)
{
	PUFFS_MSG_VARS(vn, reclaim);

	if (!EXISTSOP(pmp, RECLAIM))
		return;

	PUFFS_MSG_ALLOC(vn, reclaim);
	puffs_msg_setfaf(park_reclaim);
	puffs_msg_setinfo(park_reclaim, PUFFSOP_VN, PUFFS_VN_RECLAIM, ck);

	puffs_msg_enqueue(pmp, park_reclaim);
	PUFFS_MSG_RELEASE(reclaim);
}

/*
 * always FAF, we don't really care if the server wants to fail to
 * reclaim the node or not
 */
static int
puffs_vnop_reclaim(struct vop_reclaim_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct puffs_mount *pmp = MPTOPUFFSMP(vp->v_mount);
	struct puffs_node *pnode = VPTOPP(vp);
	boolean_t notifyserver = TRUE;

	vinvalbuf(vp, V_SAVE, 0, 0);

	/*
	 * first things first: check if someone is trying to reclaim the
	 * root vnode.  do not allow that to travel to userspace.
	 * Note that we don't need to take the lock similarly to
	 * puffs_root(), since there is only one of us.
	 */
	if (vp->v_flag & VROOT) {
		lockmgr(&pmp->pmp_lock, LK_EXCLUSIVE);
		KKASSERT(pmp->pmp_root != NULL);
		pmp->pmp_root = NULL;
		lockmgr(&pmp->pmp_lock, LK_RELEASE);
		notifyserver = FALSE;
	}

	/*
	 * purge info from kernel before issueing FAF, since we
	 * don't really know when we'll get around to it after
	 * that and someone might race us into node creation
	 */
	lockmgr(&pmp->pmp_lock, LK_EXCLUSIVE);
	LIST_REMOVE(pnode, pn_hashent);
	lockmgr(&pmp->pmp_lock, LK_RELEASE);

	if (notifyserver)
		callreclaim(MPTOPUFFSMP(vp->v_mount), VPTOPNC(vp));

	puffs_putvnode(vp);
	vp->v_data = NULL;

	return 0;
}

#define CSIZE sizeof(**ap->a_cookies)
static int
puffs_vnop_readdir(struct vop_readdir_args *ap)
{
	PUFFS_MSG_VARS(vn, readdir);
	struct vnode *vp = ap->a_vp;
	struct puffs_mount *pmp = MPTOPUFFSMP(vp->v_mount);
	size_t argsize, tomove, cookiemem, cookiesmax;
	struct uio *uio = ap->a_uio;
	size_t howmuch, resid;
	int error;

	if (!EXISTSOP(pmp, READDIR))
		return EOPNOTSUPP;

	/*
	 * ok, so we need: resid + cookiemem = maxreq
	 * => resid + cookiesize * (resid/minsize) = maxreq
	 * => resid + cookiesize/minsize * resid = maxreq
	 * => (cookiesize/minsize + 1) * resid = maxreq
	 * => resid = maxreq / (cookiesize/minsize + 1)
	 *
	 * Since cookiesize <= minsize and we're not very big on floats,
	 * we approximate that to be 1.  Therefore:
	 *
	 * resid = maxreq / 2;
	 *
	 * Well, at least we didn't have to use differential equations
	 * or the Gram-Schmidt process.
	 *
	 * (yes, I'm very afraid of this)
	 */
	KKASSERT(CSIZE <= _DIRENT_RECLEN(1));

	if (ap->a_cookies) {
		KKASSERT(ap->a_ncookies != NULL);
		if (pmp->pmp_args.pa_fhsize == 0)
			return EOPNOTSUPP;
		resid = PUFFS_TOMOVE(uio->uio_resid, pmp) / 2;
		cookiesmax = resid/_DIRENT_RECLEN(1);
		cookiemem = ALIGN(cookiesmax*CSIZE); /* play safe */
	} else {
		resid = PUFFS_TOMOVE(uio->uio_resid, pmp);
		cookiesmax = 0;
		cookiemem = 0;
	}

	argsize = sizeof(struct puffs_vnmsg_readdir);
	tomove = resid + cookiemem;
	puffs_msgmem_alloc(argsize + tomove, &park_readdir,
	    (void *)&readdir_msg, 1);

	puffs_credcvt(&readdir_msg->pvnr_cred, ap->a_cred);
	readdir_msg->pvnr_offset = uio->uio_offset;
	readdir_msg->pvnr_resid = resid;
	readdir_msg->pvnr_ncookies = cookiesmax;
	readdir_msg->pvnr_eofflag = 0;
	readdir_msg->pvnr_dentoff = cookiemem;
	puffs_msg_setinfo(park_readdir, PUFFSOP_VN,
	    PUFFS_VN_READDIR, VPTOPNC(vp));
	puffs_msg_setdelta(park_readdir, tomove);

	PUFFS_MSG_ENQUEUEWAIT2(pmp, park_readdir, vp->v_data, NULL, error);
	error = checkerr(pmp, error, __func__);
	if (error)
		goto out;

	/* userspace is cheating? */
	if (readdir_msg->pvnr_resid > resid) {
		puffs_senderr(pmp, PUFFS_ERR_READDIR, E2BIG,
		    "resid grew", VPTOPNC(vp));
		ERROUT(EPROTO);
	}
	if (readdir_msg->pvnr_ncookies > cookiesmax) {
		puffs_senderr(pmp, PUFFS_ERR_READDIR, E2BIG,
		    "too many cookies", VPTOPNC(vp));
		ERROUT(EPROTO);
	}

	/* check eof */
	if (readdir_msg->pvnr_eofflag)
		*ap->a_eofflag = 1;

	/* bouncy-wouncy with the directory data */
	howmuch = resid - readdir_msg->pvnr_resid;

	/* force eof if no data was returned (getcwd() needs this) */
	if (howmuch == 0) {
		*ap->a_eofflag = 1;
		goto out;
	}

	error = uiomove(readdir_msg->pvnr_data + cookiemem, howmuch, uio);
	if (error)
		goto out;

	/* provide cookies to caller if so desired */
	if (ap->a_cookies) {
		*ap->a_cookies = kmalloc(readdir_msg->pvnr_ncookies*CSIZE,
		    M_TEMP, M_WAITOK);
		*ap->a_ncookies = readdir_msg->pvnr_ncookies;
		memcpy(*ap->a_cookies, readdir_msg->pvnr_data,
		    *ap->a_ncookies*CSIZE);
	}

	/* next readdir starts here */
	uio->uio_offset = readdir_msg->pvnr_offset;

 out:
	puffs_msgmem_release(park_readdir);
	return error;
}
#undef CSIZE

static int
flushvncache(struct vnode *vp, int waitfor)
{
	struct puffs_node *pn = VPTOPP(vp);
	struct vattr va;
	int error = 0;

	/* flush out information from our metacache, see vop_setattr */
	if (pn->pn_stat & PNODE_METACACHE_MASK
	    && (pn->pn_stat & PNODE_DYING) == 0) {
		vattr_null(&va);
		error = dosetattr(vp, &va, FSCRED, SETATTR_CHSIZE |
		    (waitfor == MNT_NOWAIT ? 0 : SETATTR_ASYNC));
		if (error)
			return error;
	}

	/*
	 * flush pages to avoid being overly dirty
	 */
	vfsync(vp, waitfor, 0, NULL, NULL);

	return error;
}

static int
puffs_vnop_fsync(struct vop_fsync_args *ap)
{
	PUFFS_MSG_VARS(vn, fsync);
	struct vnode *vp = ap->a_vp;
	int waitfor = ap->a_waitfor;
	struct puffs_mount *pmp = MPTOPUFFSMP(vp->v_mount);
	struct puffs_node *pn = VPTOPP(vp);
	int error, dofaf;

	error = flushvncache(vp, waitfor);
	if (error)
		return error;

	/*
	 * HELLO!  We exit already here if the user server does not
	 * support fsync OR if we should call fsync for a node which
	 * has references neither in the kernel or the fs server.
	 * Otherwise we continue to issue fsync() forward.
	 */
	if (!EXISTSOP(pmp, FSYNC) || (pn->pn_stat & PNODE_DYING))
		return 0;

	dofaf = (waitfor & MNT_WAIT) == 0 || (waitfor & MNT_LAZY) != 0;

	PUFFS_MSG_ALLOC(vn, fsync);
	if (dofaf)
		puffs_msg_setfaf(park_fsync);

	fsync_msg->pvnr_flags = ap->a_flags;
	puffs_msg_setinfo(park_fsync, PUFFSOP_VN,
	    PUFFS_VN_FSYNC, VPTOPNC(vp));

	PUFFS_MSG_ENQUEUEWAIT2(pmp, park_fsync, vp->v_data, NULL, error);
	PUFFS_MSG_RELEASE(fsync);

	error = checkerr(pmp, error, __func__);

	return error;
}

static int
callremove(struct puffs_mount *pmp, puffs_cookie_t dck, puffs_cookie_t ck,
	struct namecache *ncp, struct ucred *cred)
{
	PUFFS_MSG_VARS(vn, remove);
	int error;

	PUFFS_MSG_ALLOC(vn, remove);
	remove_msg->pvnr_cookie_targ = ck;
	puffs_makecn(&remove_msg->pvnr_cn, &remove_msg->pvnr_cn_cred,
	    ncp, cred);
	puffs_msg_setinfo(park_remove, PUFFSOP_VN, PUFFS_VN_REMOVE, dck);

	PUFFS_MSG_ENQUEUEWAIT(pmp, park_remove, error);
	PUFFS_MSG_RELEASE(remove);

	return checkerr(pmp, error, __func__);
}

/*
 * XXX: can't use callremove now because can't catch setbacks with
 * it due to lack of a pnode argument.
 */
static int
puffs_vnop_remove(struct vop_nremove_args *ap)
{
	PUFFS_MSG_VARS(vn, remove);
	struct vnode *dvp = ap->a_dvp;
	struct vnode *vp;
	struct puffs_node *dpn = VPTOPP(dvp);
	struct puffs_node *pn;
	struct nchandle *nch = ap->a_nch;
	struct namecache *ncp = nch->ncp;
	struct ucred *cred = ap->a_cred;
	struct mount *mp = dvp->v_mount;
	struct puffs_mount *pmp = MPTOPUFFSMP(mp);
	int error;

	if (!EXISTSOP(pmp, REMOVE))
		return EOPNOTSUPP;

	error = vget(dvp, LK_EXCLUSIVE);
	if (error != 0) {
		DPRINTF(("puffs_vnop_remove: EAGAIN on parent vnode %p %s\n",
		    dvp, ncp->nc_name));
		return EAGAIN;
	}

	error = cache_vget(nch, cred, LK_EXCLUSIVE, &vp);
	if (error != 0) {
		DPRINTF(("puffs_vnop_remove: cache_vget error: %p %s\n",
		    dvp, ncp->nc_name));
		return EAGAIN;
	}
	if (vp->v_type == VDIR) {
		error = EISDIR;
		goto out;
	}

	pn = VPTOPP(vp);
	PUFFS_MSG_ALLOC(vn, remove);
	remove_msg->pvnr_cookie_targ = VPTOPNC(vp);
	puffs_makecn(&remove_msg->pvnr_cn, &remove_msg->pvnr_cn_cred,
	    ncp, cred);
	puffs_msg_setinfo(park_remove, PUFFSOP_VN,
	    PUFFS_VN_REMOVE, VPTOPNC(dvp));

	puffs_msg_enqueue(pmp, park_remove);
	error = puffs_msg_wait2(pmp, park_remove, dpn, pn);

	PUFFS_MSG_RELEASE(remove);

	error = checkerr(pmp, error, __func__);

 out:
	vput(dvp);
	vn_unlock(vp);
	if (error == 0)
		cache_unlink(nch);
	vrele(vp);
	return error;
}

static int
puffs_vnop_mkdir(struct vop_nmkdir_args *ap)
{
	PUFFS_MSG_VARS(vn, mkdir);
	struct vnode *dvp = ap->a_dvp;
	struct puffs_node *dpn = VPTOPP(dvp);
	struct nchandle *nch = ap->a_nch;
	struct namecache *ncp = nch->ncp;
	struct ucred *cred = ap->a_cred;
	struct mount *mp = dvp->v_mount;
	struct puffs_mount *pmp = MPTOPUFFSMP(mp);
	int error;

	if (!EXISTSOP(pmp, MKDIR))
		return EOPNOTSUPP;

	if ((error = vget(dvp, LK_EXCLUSIVE)) != 0) {
		DPRINTF(("puffs_vnop_mkdir: EAGAIN on ncp %p %s\n",
		    ncp, ncp->nc_name));
		return EAGAIN;
	}

	PUFFS_MSG_ALLOC(vn, mkdir);
	puffs_makecn(&mkdir_msg->pvnr_cn, &mkdir_msg->pvnr_cn_cred,
	    ncp, cred);
	mkdir_msg->pvnr_va = *ap->a_vap;
	puffs_msg_setinfo(park_mkdir, PUFFSOP_VN,
	    PUFFS_VN_MKDIR, VPTOPNC(dvp));

	PUFFS_MSG_ENQUEUEWAIT2(pmp, park_mkdir, dvp->v_data, NULL, error);

	error = checkerr(pmp, error, __func__);
	if (error)
		goto out;

	error = puffs_newnode(mp, dvp, ap->a_vpp,
	    mkdir_msg->pvnr_newnode, VDIR);
	if (error)
		puffs_abortbutton(pmp, PUFFS_ABORT_MKDIR, dpn->pn_cookie,
		    mkdir_msg->pvnr_newnode, ncp, cred);

 out:
	vput(dvp);
	if (!error) {
		cache_setunresolved(nch);
		cache_setvp(nch, *ap->a_vpp);
	}
	PUFFS_MSG_RELEASE(mkdir);
	return error;
}

static int
callrmdir(struct puffs_mount *pmp, puffs_cookie_t dck, puffs_cookie_t ck,
	struct namecache *ncp, struct ucred *cred)
{
	PUFFS_MSG_VARS(vn, rmdir);
	int error;

	PUFFS_MSG_ALLOC(vn, rmdir);
	rmdir_msg->pvnr_cookie_targ = ck;
	puffs_makecn(&rmdir_msg->pvnr_cn, &rmdir_msg->pvnr_cn_cred,
	    ncp, cred);
	puffs_msg_setinfo(park_rmdir, PUFFSOP_VN, PUFFS_VN_RMDIR, dck);

	PUFFS_MSG_ENQUEUEWAIT(pmp, park_rmdir, error);
	PUFFS_MSG_RELEASE(rmdir);

	return checkerr(pmp, error, __func__);
}

static int
puffs_vnop_rmdir(struct vop_nrmdir_args *ap)
{
	PUFFS_MSG_VARS(vn, rmdir);
	struct vnode *dvp = ap->a_dvp;
	struct vnode *vp;
	struct puffs_node *dpn = VPTOPP(dvp);
	struct puffs_node *pn;
	struct puffs_mount *pmp = MPTOPUFFSMP(dvp->v_mount);
	struct nchandle *nch = ap->a_nch;
	struct namecache *ncp = nch->ncp;
	struct ucred *cred = ap->a_cred;
	int error;

	if (!EXISTSOP(pmp, RMDIR))
		return EOPNOTSUPP;

	error = vget(dvp, LK_EXCLUSIVE);
	if (error != 0) {
		DPRINTF(("puffs_vnop_rmdir: EAGAIN on parent vnode %p %s\n",
		    dvp, ncp->nc_name));
		return EAGAIN;
	}
	error = cache_vget(nch, cred, LK_EXCLUSIVE, &vp);
	if (error != 0) {
		DPRINTF(("puffs_vnop_rmdir: cache_vget error: %p %s\n",
		    dvp, ncp->nc_name));
		return EAGAIN;
	}
	if (vp->v_type != VDIR) {
		error = ENOTDIR;
		goto out;
	}

	pn = VPTOPP(vp);
	PUFFS_MSG_ALLOC(vn, rmdir);
	rmdir_msg->pvnr_cookie_targ = VPTOPNC(vp);
	puffs_makecn(&rmdir_msg->pvnr_cn, &rmdir_msg->pvnr_cn_cred,
	    ncp, cred);
	puffs_msg_setinfo(park_rmdir, PUFFSOP_VN,
	    PUFFS_VN_RMDIR, VPTOPNC(dvp));

	puffs_msg_enqueue(pmp, park_rmdir);
	error = puffs_msg_wait2(pmp, park_rmdir, dpn, pn);

	PUFFS_MSG_RELEASE(rmdir);

	error = checkerr(pmp, error, __func__);

 out:
	vput(dvp);
	vn_unlock(vp);
	if (error == 0)
		cache_unlink(nch);
	vrele(vp);
	return error;
}

static int
puffs_vnop_link(struct vop_nlink_args *ap)
{
	PUFFS_MSG_VARS(vn, link);
	struct vnode *dvp = ap->a_dvp;
	struct vnode *vp = ap->a_vp;
	struct puffs_node *dpn = VPTOPP(dvp);
	struct puffs_node *pn = VPTOPP(vp);
	struct puffs_mount *pmp = MPTOPUFFSMP(dvp->v_mount);
	struct nchandle *nch = ap->a_nch;
	struct namecache *ncp = nch->ncp;
	struct ucred *cred = ap->a_cred;
	int error;

	if (!EXISTSOP(pmp, LINK))
		return EOPNOTSUPP;

	if (vp->v_mount != dvp->v_mount)
		return EXDEV;

	if ((error = vget(dvp, LK_EXCLUSIVE)) != 0) {
		DPRINTF(("puffs_vnop_link: EAGAIN on ncp %p %s\n",
		    ncp, ncp->nc_name));
		return EAGAIN;
	}

	PUFFS_MSG_ALLOC(vn, link);
	link_msg->pvnr_cookie_targ = VPTOPNC(vp);
	puffs_makecn(&link_msg->pvnr_cn, &link_msg->pvnr_cn_cred,
	    ncp, cred);
	puffs_msg_setinfo(park_link, PUFFSOP_VN,
	    PUFFS_VN_LINK, VPTOPNC(dvp));

	puffs_msg_enqueue(pmp, park_link);
	error = puffs_msg_wait2(pmp, park_link, dpn, pn);

	PUFFS_MSG_RELEASE(link);

	error = checkerr(pmp, error, __func__);

	/*
	 * XXX: stay in touch with the cache.  I don't like this, but
	 * don't have a better solution either.  See also puffs_rename().
	 */
	if (error == 0) {
		puffs_updatenode(pn, PUFFS_UPDATECTIME);
	}

	vput(dvp);
	if (error == 0) {
		cache_setunresolved(nch);
		cache_setvp(nch, vp);
	}
	return error;
}

static int
puffs_vnop_symlink(struct vop_nsymlink_args *ap)
{
	PUFFS_MSG_VARS(vn, symlink);
	struct vnode *dvp = ap->a_dvp;
	struct puffs_node *dpn = VPTOPP(dvp);
	struct mount *mp = dvp->v_mount;
	struct puffs_mount *pmp = MPTOPUFFSMP(dvp->v_mount);
	struct nchandle *nch = ap->a_nch;
	struct namecache *ncp = nch->ncp;
	struct ucred *cred = ap->a_cred;
	int error;

	if (!EXISTSOP(pmp, SYMLINK))
		return EOPNOTSUPP;

	if ((error = vget(dvp, LK_EXCLUSIVE)) != 0) {
		DPRINTF(("puffs_vnop_symlink: EAGAIN on ncp %p %s\n",
		    ncp, ncp->nc_name));
		return EAGAIN;
	}

	*ap->a_vpp = NULL;

	PUFFS_MSG_ALLOC(vn, symlink);
	puffs_makecn(&symlink_msg->pvnr_cn, &symlink_msg->pvnr_cn_cred,
		ncp, cred);
	symlink_msg->pvnr_va = *ap->a_vap;
	(void)strlcpy(symlink_msg->pvnr_link, ap->a_target,
	    sizeof(symlink_msg->pvnr_link));
	puffs_msg_setinfo(park_symlink, PUFFSOP_VN,
	    PUFFS_VN_SYMLINK, VPTOPNC(dvp));

	PUFFS_MSG_ENQUEUEWAIT2(pmp, park_symlink, dvp->v_data, NULL, error);

	error = checkerr(pmp, error, __func__);
	if (error)
		goto out;

	error = puffs_newnode(mp, dvp, ap->a_vpp,
	    symlink_msg->pvnr_newnode, VLNK);
	if (error)
		puffs_abortbutton(pmp, PUFFS_ABORT_SYMLINK, dpn->pn_cookie,
		    symlink_msg->pvnr_newnode, ncp, cred);

 out:
	vput(dvp);
	PUFFS_MSG_RELEASE(symlink);
	if (!error) {
		cache_setunresolved(nch);
		cache_setvp(nch, *ap->a_vpp);
	}
	return error;
}

static int
puffs_vnop_readlink(struct vop_readlink_args *ap)
{
	PUFFS_MSG_VARS(vn, readlink);
	struct vnode *vp = ap->a_vp;
	struct puffs_mount *pmp = MPTOPUFFSMP(ap->a_vp->v_mount);
	size_t linklen;
	int error;

	if (!EXISTSOP(pmp, READLINK))
		return EOPNOTSUPP;

	PUFFS_MSG_ALLOC(vn, readlink);
	puffs_credcvt(&readlink_msg->pvnr_cred, ap->a_cred);
	linklen = sizeof(readlink_msg->pvnr_link);
	readlink_msg->pvnr_linklen = linklen;
	puffs_msg_setinfo(park_readlink, PUFFSOP_VN,
	    PUFFS_VN_READLINK, VPTOPNC(vp));

	PUFFS_MSG_ENQUEUEWAIT2(pmp, park_readlink, vp->v_data, NULL, error);
	error = checkerr(pmp, error, __func__);
	if (error)
		goto out;

	/* bad bad user file server */
	if (readlink_msg->pvnr_linklen > linklen) {
		puffs_senderr(pmp, PUFFS_ERR_READLINK, E2BIG,
		    "linklen too big", VPTOPNC(ap->a_vp));
		error = EPROTO;
		goto out;
	}

	error = uiomove(readlink_msg->pvnr_link, readlink_msg->pvnr_linklen,
	    ap->a_uio);
 out:
	PUFFS_MSG_RELEASE(readlink);
	return error;
}

static int
puffs_vnop_rename(struct vop_nrename_args *ap)
{
	PUFFS_MSG_VARS(vn, rename);
	struct nchandle *fnch = ap->a_fnch;
	struct nchandle *tnch = ap->a_tnch;
	struct vnode *fdvp = ap->a_fdvp;
	struct vnode *fvp = fnch->ncp->nc_vp;
	struct vnode *tdvp = ap->a_tdvp;
	struct vnode *tvp = tnch->ncp->nc_vp;
	struct ucred *cred = ap->a_cred;
	struct puffs_mount *pmp = MPTOPUFFSMP(fdvp->v_mount);
	int error;

	if (!EXISTSOP(pmp, RENAME))
		return EOPNOTSUPP;

	error = vget(tdvp, LK_EXCLUSIVE);
	if (error != 0) {
		DPRINTF(("puffs_vnop_rename: EAGAIN on tdvp vnode %p %s\n",
		    tdvp, tnch->ncp->nc_name));
		return EAGAIN;
	}
	if (tvp != NULL) {
		error = vget(tvp, LK_EXCLUSIVE);
		if (error != 0) {
			DPRINTF(("puffs_vnop_rename: EAGAIN on tvp vnode %p %s\n",
			    tvp, tnch->ncp->nc_name));
			vput(tdvp);
			return EAGAIN;
		}
	}

	if ((fvp->v_mount != tdvp->v_mount) ||
	    (tvp && (fvp->v_mount != tvp->v_mount))) {
		error = EXDEV;
		goto out;
	}

	if (tvp) {
		if (fvp->v_type == VDIR && tvp->v_type != VDIR) {
			error = ENOTDIR;
			goto out;
		} else if (fvp->v_type != VDIR && tvp->v_type == VDIR) {
			error = EISDIR;
			goto out;
		}
	}

	PUFFS_MSG_ALLOC(vn, rename);
	rename_msg->pvnr_cookie_src = VPTOPNC(fvp);
	rename_msg->pvnr_cookie_targdir = VPTOPNC(tdvp);
	if (tvp)
		rename_msg->pvnr_cookie_targ = VPTOPNC(tvp);
	else
		rename_msg->pvnr_cookie_targ = NULL;
	puffs_makecn(&rename_msg->pvnr_cn_src, &rename_msg->pvnr_cn_src_cred,
	    fnch->ncp, cred);
	puffs_makecn(&rename_msg->pvnr_cn_targ, &rename_msg->pvnr_cn_targ_cred,
	    tnch->ncp, cred);
	puffs_msg_setinfo(park_rename, PUFFSOP_VN,
	    PUFFS_VN_RENAME, VPTOPNC(fdvp));

	PUFFS_MSG_ENQUEUEWAIT2(pmp, park_rename, fdvp->v_data, NULL, error);
	PUFFS_MSG_RELEASE(rename);
	error = checkerr(pmp, error, __func__);

	if (error == 0)
		puffs_updatenode(VPTOPP(fvp), PUFFS_UPDATECTIME);

 out:
	if (tvp != NULL)
		vn_unlock(tvp);
	if (tdvp != tvp)
		vn_unlock(tdvp);
	if (error == 0)
		cache_rename(fnch, tnch);
	if (tvp != NULL)
		vrele(tvp);
	vrele(tdvp);

	return error;
}

static int
puffs_vnop_read(struct vop_read_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct uio *uio = ap->a_uio;
	int ioflag = ap->a_ioflag;
	struct ucred * cred = ap->a_cred;
	struct puffs_mount *pmp = MPTOPUFFSMP(vp->v_mount);
	int error;

	if (!EXISTSOP(pmp, READ))
		return EOPNOTSUPP;

	if (vp->v_type == VDIR)
		return EISDIR;
	else if (vp->v_type != VREG)
		return EINVAL;

	if (PUFFS_USE_PAGECACHE(pmp))
		error = puffs_bioread(vp, uio, ioflag, cred);
	else
		error = puffs_directread(vp, uio, ioflag, cred);

	return error;
}

static int
puffs_vnop_write(struct vop_write_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct uio *uio = ap->a_uio;
	int ioflag = ap->a_ioflag;
	struct ucred * cred = ap->a_cred;
	struct puffs_mount *pmp = MPTOPUFFSMP(vp->v_mount);
	int error;

	if (!EXISTSOP(pmp, WRITE))
		return EOPNOTSUPP;

	if (vp->v_type == VDIR)
		return EISDIR;
	else if (vp->v_type != VREG)
		return EINVAL;

	if (PUFFS_USE_PAGECACHE(pmp))
		error = puffs_biowrite(vp, uio, ioflag, cred);
	else
		error = puffs_directwrite(vp, uio, ioflag, cred);

	return error;
}

static int
puffs_vnop_print(struct vop_print_args *ap)
{
	PUFFS_MSG_VARS(vn, print);
	struct vnode *vp = ap->a_vp;
	struct puffs_mount *pmp = MPTOPUFFSMP(vp->v_mount);
	struct puffs_node *pn = VPTOPP(vp);
	int error;

	/* kernel portion */
	kprintf("tag VT_PUFFS, vnode %p, puffs node: %p,\n"
	    "\tuserspace cookie: %p", vp, pn, pn->pn_cookie);
	if (vp->v_type == VFIFO)
		fifo_printinfo(vp);
	kprintf("\n");

	/* userspace portion */
	if (EXISTSOP(pmp, PRINT)) {
		PUFFS_MSG_ALLOC(vn, print);
		puffs_msg_setinfo(park_print, PUFFSOP_VN,
		    PUFFS_VN_PRINT, VPTOPNC(vp));
		PUFFS_MSG_ENQUEUEWAIT2(pmp, park_print, vp->v_data,
		    NULL, error);
		PUFFS_MSG_RELEASE(print);
	}

	return 0;
}

static int
puffs_vnop_pathconf(struct vop_pathconf_args *ap)
{
	PUFFS_MSG_VARS(vn, pathconf);
	struct vnode *vp = ap->a_vp;
	struct puffs_mount *pmp = MPTOPUFFSMP(vp->v_mount);
	int error;

	if (!EXISTSOP(pmp, PATHCONF))
		return EOPNOTSUPP;

	PUFFS_MSG_ALLOC(vn, pathconf);
	pathconf_msg->pvnr_name = ap->a_name;
	puffs_msg_setinfo(park_pathconf, PUFFSOP_VN,
	    PUFFS_VN_PATHCONF, VPTOPNC(vp));
	PUFFS_MSG_ENQUEUEWAIT2(pmp, park_pathconf, vp->v_data, NULL, error);
	error = checkerr(pmp, error, __func__);
	if (!error)
		*ap->a_retval = pathconf_msg->pvnr_retval;
	PUFFS_MSG_RELEASE(pathconf);

	return error;
}

static int
puffs_vnop_advlock(struct vop_advlock_args *ap)
{
	PUFFS_MSG_VARS(vn, advlock);
	struct vnode *vp = ap->a_vp;
	struct puffs_node *pn = VPTOPP(vp);
	struct puffs_mount *pmp = MPTOPUFFSMP(vp->v_mount);
	int error;

	if (!EXISTSOP(pmp, ADVLOCK))
		return lf_advlock(ap, &pn->pn_lockf, vp->v_filesize);

	PUFFS_MSG_ALLOC(vn, advlock);
	(void)memcpy(&advlock_msg->pvnr_fl, ap->a_fl,
		     sizeof(advlock_msg->pvnr_fl));
	advlock_msg->pvnr_id = ap->a_id;
	advlock_msg->pvnr_op = ap->a_op;
	advlock_msg->pvnr_flags = ap->a_flags;
	puffs_msg_setinfo(park_advlock, PUFFSOP_VN,
	    PUFFS_VN_ADVLOCK, VPTOPNC(vp));
	PUFFS_MSG_ENQUEUEWAIT2(pmp, park_advlock, vp->v_data, NULL, error);
	error = checkerr(pmp, error, __func__);
	PUFFS_MSG_RELEASE(advlock);

	return error;
}

static int
puffs_vnop_bmap(struct vop_bmap_args *ap)
{
	if (ap->a_doffsetp != NULL)
		*ap->a_doffsetp = ap->a_loffset;
	if (ap->a_runp != NULL)
		*ap->a_runp = 0;
	if (ap->a_runb != NULL)
		*ap->a_runb = 0;
	return (0);
}

static int
puffs_vnop_mmap(struct vop_mmap_args *ap)
{
	return EINVAL;
}


static int
puffs_vnop_strategy(struct vop_strategy_args *ap)
{
	return puffs_doio(ap->a_vp, ap->a_bio, curthread);
}

struct vop_ops puffs_fifo_vops = {
	.vop_default =			fifo_vnoperate,
	.vop_access =			puffs_vnop_access,
	.vop_getattr =			puffs_vnop_getattr,
	.vop_setattr =			puffs_vnop_setattr,
	.vop_inactive =			puffs_vnop_inactive,
	.vop_reclaim =			puffs_vnop_reclaim,
	.vop_print =			puffs_vnop_print,
};

struct vop_ops puffs_vnode_vops = {
	.vop_default =			vop_defaultop,
	.vop_nresolve =			puffs_vnop_lookup,
	.vop_nlookupdotdot =		puffs_vnop_lookupdotdot,
	.vop_ncreate =			puffs_vnop_create,
	.vop_nmkdir =			puffs_vnop_mkdir,
	.vop_nrmdir =			puffs_vnop_rmdir,
	.vop_nremove =			puffs_vnop_remove,
	.vop_nrename =			puffs_vnop_rename,
	.vop_nlink =			puffs_vnop_link,
	.vop_nsymlink =			puffs_vnop_symlink,
	.vop_nmknod =			puffs_vnop_mknod,
	.vop_access =			puffs_vnop_access,
	.vop_getattr =			puffs_vnop_getattr,
	.vop_setattr =			puffs_vnop_setattr,
	.vop_readdir =			puffs_vnop_readdir,
	.vop_open =			puffs_vnop_open,
	.vop_close =			puffs_vnop_close,
	.vop_read =			puffs_vnop_read,
	.vop_write =			puffs_vnop_write,
	.vop_readlink =			puffs_vnop_readlink,
	.vop_advlock =			puffs_vnop_advlock,
	.vop_bmap =			puffs_vnop_bmap,
	.vop_mmap =			puffs_vnop_mmap,
	.vop_strategy =			puffs_vnop_strategy,
	.vop_getpages =			vop_stdgetpages,
	.vop_putpages =			vop_stdputpages,
	.vop_fsync = 			puffs_vnop_fsync,
	.vop_inactive =			puffs_vnop_inactive,
	.vop_reclaim =			puffs_vnop_reclaim,
	.vop_pathconf =			puffs_vnop_pathconf,
	.vop_print =			puffs_vnop_print,
};
