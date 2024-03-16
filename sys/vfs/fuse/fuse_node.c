/*-
 * Copyright (c) 2019 Tomohiro Kusumi <tkusumi@netbsd.org>
 * Copyright (c) 2019 The DragonFly Project
 * All rights reserved.
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
 */

#include "fuse.h"

static MALLOC_DEFINE(M_FUSE_NODE, "fuse_node", "FUSE node");

static struct objcache *fuse_node_objcache = NULL;
static struct objcache_malloc_args fuse_node_args = {
	sizeof(struct fuse_node), M_FUSE_NODE,
};

static int
fuse_node_cmp(struct fuse_node *p1, struct fuse_node *p2)
{
	if (p1->ino < p2->ino)
		return -1;
	if (p1->ino > p2->ino)
		return 1;
	return 0;
}

RB_PROTOTYPE2(fuse_node_tree, fuse_node, entry, fuse_node_cmp, uint64_t);
RB_GENERATE2(fuse_node_tree, fuse_node, node_entry, fuse_node_cmp,
		uint64_t, ino);

void
fuse_node_new(struct fuse_mount *fmp, uint64_t ino, enum vtype vtyp,
	      struct fuse_node **fnpp)
{
	struct fuse_node *fnp;

	fnp = objcache_get(fuse_node_objcache, M_WAITOK);
	bzero(fnp, sizeof(*fnp));

	fnp->fmp = fmp;

	mtx_init(&fnp->node_lock, "fuse_node_lock");

	fnp->ino = ino;
	fnp->type = vtyp;
	fnp->size = 0;
	fnp->nlookup = 0;
	fnp->fh = 0;
	fnp->closed = false;

	if (RB_INSERT(fuse_node_tree, &fmp->node_head, fnp)) {
		panic("fuse_node_new: cannot insert %p\n", fnp);
	}

	*fnpp = fnp;
}

void
fuse_node_free(struct fuse_mount *fmp, struct fuse_node *fnp)
{
	fuse_dbg("free ino=%ju\n", fnp->ino);

	mtx_lock(&fmp->ino_lock);
	RB_REMOVE(fuse_node_tree, &fmp->node_head, fnp);
	mtx_unlock(&fmp->ino_lock);

	objcache_put(fuse_node_objcache, fnp);
}

/*
 * Allocate or find the fuse node for the specified inode number and assign
 * its vnode.
 */
int
fuse_alloc_node(struct fuse_mount *fmp, struct fuse_node *dfnp,
	        uint64_t ino, enum vtype vtyp, struct vnode **vpp)
{
	struct fuse_node *fnp;
	int error;
	int allocated = 0;

	KKASSERT(dfnp->type == VDIR);
	if (vtyp == VBLK || vtyp == VCHR || vtyp == VFIFO)
		return EINVAL;

	mtx_lock(&fmp->ino_lock);
	fnp = RB_LOOKUP(fuse_node_tree, &fmp->node_head, ino);
	if (fnp == NULL) {
		fuse_node_new(fmp, ino, vtyp, &fnp);
		allocated = 1;
	}
	mtx_unlock(&fmp->ino_lock);

	error = fuse_node_vn(fnp, vpp);
	if (error) {
		if (allocated)
			fuse_node_free(fmp, fnp);
	}
	return error;
}

/*
 * Returns exclusively locked vp
 */
int
fuse_node_vn(struct fuse_node *fnp, struct vnode **vpp)
{
	struct mount *mp = fnp->fmp->mp;
	struct vnode *vp;
	struct vnode *newvp;
	int error;

	newvp = NULL;
retry:
	error = 0;
	if (fnp->vp == NULL && newvp == NULL) {
		error = getnewvnode(VT_FUSE, mp, &newvp,
				    VLKTIMEOUT, LK_CANRECURSE);
		if (error)
			return error;
	}

	mtx_lock(&fnp->node_lock);

	/*
	 * Check case where vp is already assigned
	 */
	vp = fnp->vp;
	if (vp) {
		vhold(vp);
		mtx_unlock(&fnp->node_lock);
		error = vget(vp, LK_EXCLUSIVE | LK_RETRY);
		vdrop(vp);

		if (error)
			goto retry;
		if (fnp->vp != vp) {
			vput(vp);
			goto retry;
		}

		*vpp = vp;

		if (newvp) {
			newvp->v_type = VBAD;
			vx_put(newvp);
		}

		return 0;
	}

	/*
	 * Assign new vp, release the node lock
	 */
	if (newvp == NULL) {
		mtx_unlock(&fnp->node_lock);
		goto retry;
	}

	fnp->vp = newvp;
	mtx_unlock(&fnp->node_lock);
	vp = newvp;

	/*
	 * Finish setting up vp (vp is held exclusively + vx)
	 */
	vp->v_type = fnp->type;
	vp->v_data = fnp;

	switch (vp->v_type) {
	case VREG:
		vinitvmio(vp, fnp->size, FUSE_BLKSIZE, -1);
		break;
	case VDIR:
		break;
	case VBLK:
	case VCHR:
		KKASSERT(0);
		vp->v_ops = &mp->mnt_vn_spec_ops;
		addaliasu(vp, umajor(0), uminor(0)); /* XXX CUSE */
		break;
	case VLNK:
		break;
	case VSOCK:
		break;
	case VFIFO:
		KKASSERT(0);
	case VDATABASE:
		break;
	default:
		KKASSERT(0);
	}

	vx_downgrade(vp);	/* VX to normal, is still exclusive */

	*vpp = vp;

	return error;
}

int
fuse_node_truncate(struct fuse_node *fnp, size_t oldsize, size_t newsize)
{
	struct vnode *vp = fnp->vp;
	int error;

	fuse_dbg("ino=%ju update size %ju -> %ju\n",
	    fnp->ino, oldsize, newsize);

	fnp->attr.va_size = fnp->size = newsize;

	if (newsize < oldsize)
		error = nvtruncbuf(vp, newsize, FUSE_BLKSIZE, -1, 0);
	else
		error = nvextendbuf(vp, oldsize, newsize, FUSE_BLKSIZE,
		    FUSE_BLKSIZE, -1, -1, 0);
	return error;
}

void
fuse_node_init(void)
{
	fuse_node_objcache = objcache_create("fuse_node", 0, 0,
	    NULL, NULL, NULL,
	    objcache_malloc_alloc_zero, objcache_malloc_free, &fuse_node_args);
}

void
fuse_node_cleanup(void)
{
	objcache_destroy(fuse_node_objcache);
}
