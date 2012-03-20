/*	$NetBSD: puffs_node.c,v 1.19 2011/06/30 20:09:41 wiz Exp $	*/

/*
 * Copyright (c) 2005, 2006, 2007  Antti Kantee.  All Rights Reserved.
 *
 * Development of this software was supported by the
 * Google Summer of Code program, the Ulla Tuominen Foundation
 * and the Finnish Cultural Foundation.
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
#include <sys/fnv_hash.h>
#include <sys/malloc.h>
#include <sys/mount.h>
#include <sys/namei.h>
#include <sys/vnode.h>

#include <vfs/puffs/puffs_msgif.h>
#include <vfs/puffs/puffs_sys.h>

static __inline struct puffs_node_hashlist
	*puffs_cookie2hashlist(struct puffs_mount *, puffs_cookie_t);
static struct puffs_node *puffs_cookie2pnode(struct puffs_mount *,
					     puffs_cookie_t);

/*
 * Grab a vnode, intialize all the puffs-dependent stuff.
 */
int
puffs_getvnode(struct mount *mp, puffs_cookie_t ck, enum vtype type,
	voff_t vsize, struct vnode **vpp)
{
	struct puffs_mount *pmp;
	struct puffs_newcookie *pnc;
	struct vnode *vp;
	struct puffs_node *pnode;
	struct puffs_node_hashlist *plist;
	int error;

	pmp = MPTOPUFFSMP(mp);

	error = EPROTO;
	if (type <= VNON || type >= VBAD) {
		puffs_senderr(pmp, PUFFS_ERR_MAKENODE, EINVAL,
		    "bad node type", ck);
		goto bad;
	}
	if (type == VBLK || type == VCHR) {
		puffs_senderr(pmp, PUFFS_ERR_MAKENODE, EINVAL,
		    "device nodes are not supported", ck);
		goto bad;
	}
	if (vsize == VNOVAL) {
		puffs_senderr(pmp, PUFFS_ERR_MAKENODE, EINVAL,
		    "VNOVAL is not a valid size", ck);
		goto bad;
	}

	/* XXX Add VT_PUFFS */
	error = getnewvnode(VT_SYNTH, mp, &vp, 0, 0);
	if (error) {
		goto bad;
	}
	vp->v_type = type;

	/*
	 * Creation should not fail after this point.  Or if it does,
	 * care must be taken so that VOP_INACTIVE() isn't called.
	 */

	/* dances based on vnode type. almost ufs_vinit(), but not quite */
	switch (type) {
	case VFIFO:
		vp->v_ops = &mp->mnt_vn_fifo_ops;
		break;

	case VREG:
		if (PUFFS_USE_PAGECACHE(pmp))
			vinitvmio(vp, vsize, mp->mnt_stat.f_iosize, -1);
		break;

	case VDIR:
	case VLNK:
	case VSOCK:
		break;
	default:
		panic("puffs_getvnode: invalid vtype %d", type);
	}

	pnode = kmalloc(sizeof(struct puffs_node), M_PUFFS, M_ZERO | M_WAITOK);

	pnode->pn_cookie = ck;
	pnode->pn_refcount = 1;

	/* insert cookie on list, take off of interlock list */
	lockinit(&pnode->pn_mtx, "puffs pn_mtx", 0, 0);
#ifdef XXXDF
	selinit(&pnode->pn_sel);
	knlist_init();
#endif
	plist = puffs_cookie2hashlist(pmp, ck);
	lockmgr(&pmp->pmp_lock, LK_EXCLUSIVE);
	LIST_INSERT_HEAD(plist, pnode, pn_hashent);
	if (ck != pmp->pmp_root_cookie) {
		LIST_FOREACH(pnc, &pmp->pmp_newcookie, pnc_entries) {
			if (pnc->pnc_cookie == ck) {
				LIST_REMOVE(pnc, pnc_entries);
				kfree(pnc, M_PUFFS);
				break;
			}
		}
		KKASSERT(pnc != NULL);
	}
	lockmgr(&pmp->pmp_lock, LK_RELEASE);

	vp->v_data = pnode;
	vp->v_type = type;
	pnode->pn_vp = vp;
	pnode->pn_serversize = vsize;

	*vpp = vp;

	DPRINTF(("new vnode at %p, pnode %p, cookie %p\n", vp,
	    pnode, pnode->pn_cookie));

	return 0;

 bad:
	/* remove staging cookie from list */
	if (ck != pmp->pmp_root_cookie) {
		lockmgr(&pmp->pmp_lock, LK_EXCLUSIVE);
		LIST_FOREACH(pnc, &pmp->pmp_newcookie, pnc_entries) {
			if (pnc->pnc_cookie == ck) {
				LIST_REMOVE(pnc, pnc_entries);
				kfree(pnc, M_PUFFS);
				break;
			}
		}
		KKASSERT(pnc != NULL);
		lockmgr(&pmp->pmp_lock, LK_RELEASE);
	}

	return error;
}

/* new node creating for creative vop ops (create, symlink, mkdir, mknod) */
int
puffs_newnode(struct mount *mp, struct vnode *dvp, struct vnode **vpp,
	puffs_cookie_t ck, enum vtype type)
{
	struct puffs_mount *pmp = MPTOPUFFSMP(mp);
	struct puffs_newcookie *pnc;
	struct vnode *vp;
	int error;

	/* userspace probably has this as a NULL op */
	if (ck == NULL) {
		error = EOPNOTSUPP;
		return error;
	}

	/*
	 * Check for previous node with the same designation.
	 * Explicitly check the root node cookie, since it might be
	 * reclaimed from the kernel when this check is made.
	 */
	lockmgr(&pmp->pmp_lock, LK_EXCLUSIVE);
	if (ck == pmp->pmp_root_cookie
	    || puffs_cookie2pnode(pmp, ck) != NULL) {
		lockmgr(&pmp->pmp_lock, LK_RELEASE);
		puffs_senderr(pmp, PUFFS_ERR_MAKENODE, EEXIST,
		    "cookie exists", ck);
		return EPROTO;
	}

	LIST_FOREACH(pnc, &pmp->pmp_newcookie, pnc_entries) {
		if (pnc->pnc_cookie == ck) {
			lockmgr(&pmp->pmp_lock, LK_RELEASE);
			puffs_senderr(pmp, PUFFS_ERR_MAKENODE, EEXIST,
			    "newcookie exists", ck);
			return EPROTO;
		}
	}
	pnc = kmalloc(sizeof(struct puffs_newcookie), M_PUFFS, M_WAITOK);
	pnc->pnc_cookie = ck;
	LIST_INSERT_HEAD(&pmp->pmp_newcookie, pnc, pnc_entries);
	lockmgr(&pmp->pmp_lock, LK_RELEASE);

	error = puffs_getvnode(dvp->v_mount, ck, type, 0, &vp);
	if (error)
		return error;
	*vpp = vp;

	return 0;
}

void
puffs_putvnode(struct vnode *vp)
{
	struct puffs_mount *pmp;
	struct puffs_node *pnode;

	pmp = VPTOPUFFSMP(vp);
	pnode = VPTOPP(vp);

#ifdef DIAGNOSTIC
	if (vp->v_tag != VT_SYNTH)
		panic("puffs_putvnode: %p not a puffs vnode", vp);
#endif

	puffs_releasenode(pnode);
	vp->v_data = NULL;

	return;
}

static __inline struct puffs_node_hashlist *
puffs_cookie2hashlist(struct puffs_mount *pmp, puffs_cookie_t ck)
{
	uint32_t hash;

	hash = fnv_32_buf(&ck, sizeof(void *), FNV1_32_INIT);
	return &pmp->pmp_pnodehash[hash % pmp->pmp_npnodehash];
}

/*
 * Translate cookie to puffs_node.  Caller must hold pmp_lock
 * and it will be held upon return.
 */
static struct puffs_node *
puffs_cookie2pnode(struct puffs_mount *pmp, puffs_cookie_t ck)
{
	struct puffs_node_hashlist *plist;
	struct puffs_node *pnode;

	plist = puffs_cookie2hashlist(pmp, ck);
	LIST_FOREACH(pnode, plist, pn_hashent) {
		if (pnode->pn_cookie == ck)
			break;
	}

	return pnode;
}

/*
 * Make sure root vnode exists and reference it.  Does NOT lock.
 */
static int
puffs_makeroot(struct puffs_mount *pmp)
{
	struct vnode *vp;
	int rv;

	/*
	 * pmp_lock must be held if vref()'ing or vrele()'ing the
	 * root vnode.  the latter is controlled by puffs_inactive().
	 *
	 * pmp_root is set here and cleared in puffs_reclaim().
	 */
 retry:
	vp = pmp->pmp_root;
	if (vp) {
		if (vget(vp, LK_EXCLUSIVE) == 0)
			return 0;
	}

	/*
	 * So, didn't have the magic root vnode available.
	 * No matter, grab another and stuff it with the cookie.
	 */
	if ((rv = puffs_getvnode(pmp->pmp_mp, pmp->pmp_root_cookie,
	    pmp->pmp_root_vtype, pmp->pmp_root_vsize, &vp)))
		return rv;

	/*
	 * Someone magically managed to race us into puffs_getvnode?
	 * Put our previous new vnode back and retry.
	 */
	lockmgr(&pmp->pmp_lock, LK_EXCLUSIVE);
	if (pmp->pmp_root) {
		struct puffs_node *pnode = VPTOPP(vp);

		LIST_REMOVE(pnode, pn_hashent);
		lockmgr(&pmp->pmp_lock, LK_RELEASE);
		puffs_putvnode(vp);
		goto retry;
	}

	/* store cache */
	vsetflags(vp, VROOT);
	pmp->pmp_root = vp;
	vref(vp);
	lockmgr(&pmp->pmp_lock, LK_RELEASE);

	return 0;
}

/*
 * Locate the in-kernel vnode based on the cookie received given
 * from userspace.  Returns a vnode, if found, NULL otherwise.
 * The parameter "lock" control whether to lock the possible or
 * not.  Locking always might cause us to lock against ourselves
 * in situations where we want the vnode but don't care for the
 * vnode lock, e.g. file server issued putpages.
 */
int
puffs_cookie2vnode(struct puffs_mount *pmp, puffs_cookie_t ck,
	int willcreate, struct vnode **vpp)
{
	struct puffs_node *pnode;
	struct puffs_newcookie *pnc;
	struct vnode *vp;
	int rv;

	/*
	 * Handle root in a special manner, since we want to make sure
	 * pmp_root is properly set.
	 */
	if (ck == pmp->pmp_root_cookie) {
		if ((rv = puffs_makeroot(pmp)))
			return rv;
		*vpp = pmp->pmp_root;
		return 0;
	}

	lockmgr(&pmp->pmp_lock, LK_EXCLUSIVE);
	pnode = puffs_cookie2pnode(pmp, ck);
	if (pnode == NULL) {
		if (willcreate) {
			pnc = kmalloc(sizeof(struct puffs_newcookie),
			    M_PUFFS, M_WAITOK);
			pnc->pnc_cookie = ck;
			LIST_INSERT_HEAD(&pmp->pmp_newcookie, pnc, pnc_entries);
		}
		lockmgr(&pmp->pmp_lock, LK_RELEASE);
		return PUFFS_NOSUCHCOOKIE;
	}
	vp = pnode->pn_vp;
	vhold(vp);
	lockmgr(&pmp->pmp_lock, LK_RELEASE);

	rv = vget(vp, LK_EXCLUSIVE);
	vdrop(vp);
	if (rv)
		return rv;

	*vpp = vp;
	return 0;
}

void
puffs_updatenode(struct puffs_node *pn, int flags)
{
	struct timespec ts;

	if (flags == 0)
		return;

	nanotime(&ts);

	if (flags & PUFFS_UPDATEATIME) {
		pn->pn_mc_atime = ts;
		pn->pn_stat |= PNODE_METACACHE_ATIME;
	}
	if (flags & PUFFS_UPDATECTIME) {
		pn->pn_mc_ctime = ts;
		pn->pn_stat |= PNODE_METACACHE_CTIME;
	}
	if (flags & PUFFS_UPDATEMTIME) {
		pn->pn_mc_mtime = ts;
		pn->pn_stat |= PNODE_METACACHE_MTIME;
	}
}

int
puffs_meta_setsize(struct vnode *vp, off_t nsize, int trivial)
{
	struct puffs_node *pn = VPTOPP(vp);
	struct puffs_mount *pmp = VPTOPUFFSMP(vp);
	int biosize = vp->v_mount->mnt_stat.f_iosize;
	off_t osize;
	int error;

	osize = puffs_meta_getsize(vp);
	pn->pn_mc_size = nsize;
	if (pn->pn_serversize != nsize)
		pn->pn_stat |= PNODE_METACACHE_SIZE;
	else
		pn->pn_stat &= ~PNODE_METACACHE_SIZE;

	if (!PUFFS_USE_PAGECACHE(pmp))
		return 0;

	if (nsize < osize) {
		error = nvtruncbuf(vp, nsize, biosize, -1, 0);
	} else {
		error = nvextendbuf(vp, osize, nsize,
				    biosize, biosize, -1, -1,
				    trivial);
	}

	return error;
}

/*
 * Add reference to node.
 *  mutex held on entry and return
 */
void
puffs_referencenode(struct puffs_node *pn)
{

	KKASSERT(lockstatus(&pn->pn_mtx, curthread) == LK_EXCLUSIVE);
	pn->pn_refcount++;
}

/*
 * Release pnode structure which dealing with references to the
 * puffs_node instead of the vnode.  Can't use vref()/vrele() on
 * the vnode there, since that causes the lovely VOP_INACTIVE(),
 * which in turn causes the lovely deadlock when called by the one
 * who is supposed to handle it.
 */
void
puffs_releasenode(struct puffs_node *pn)
{

	lockmgr(&pn->pn_mtx, LK_EXCLUSIVE);
	if (--pn->pn_refcount == 0) {
		lockmgr(&pn->pn_mtx, LK_RELEASE);
		lockuninit(&pn->pn_mtx);
#ifdef XXXDF
		seldestroy(&pn->pn_sel);
#endif
		kfree(pn, M_PUFFS);
	} else {
		lockmgr(&pn->pn_mtx, LK_RELEASE);
	}
}
