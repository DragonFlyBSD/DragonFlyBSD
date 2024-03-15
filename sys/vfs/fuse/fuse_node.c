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

static MALLOC_DEFINE(M_FUSE_DENT, "fuse_dent", "FUSE dent");

static struct objcache *fuse_dent_objcache = NULL;
static struct objcache_malloc_args fuse_dent_args = {
	sizeof(struct fuse_dent), M_FUSE_DENT,
};

static int
fuse_dent_cmp(struct fuse_dent *p1, struct fuse_dent *p2)
{
	return strcmp(p1->name, p2->name);
}

RB_PROTOTYPE_STATIC(fuse_dent_tree, fuse_dent, entry, fuse_dent_cmp);
RB_GENERATE_STATIC(fuse_dent_tree, fuse_dent, dent_entry, fuse_dent_cmp);

void
fuse_node_new(struct fuse_mount *fmp, uint64_t ino, enum vtype vtyp,
	      struct fuse_node **fnpp)
{
	struct fuse_node *fnp;

	fnp = objcache_get(fuse_node_objcache, M_WAITOK);
	bzero(fnp, sizeof(*fnp));

	fnp->fmp = fmp;

	mtx_init(&fnp->node_lock, "fuse_node_lock");
	RB_INIT(&fnp->dent_head);

	fnp->ino = ino;
	fnp->type = vtyp;
	fnp->nlink = 0;
	fnp->size = 0;
	fnp->nlookup = 0;
	fnp->fh = 0;
	fnp->closed = false;

	*fnpp = fnp;
}

void
fuse_node_free(struct fuse_node *fnp)
{
	struct fuse_node *dfnp = fnp->pfnp;
	struct fuse_dent *fep;
	struct fuse_dent *fep_temp;

	fuse_dbg("free ino=%ju\n", fnp->ino);

	if (dfnp) {
		KKASSERT(dfnp->type == VDIR);
		mtx_lock(&dfnp->node_lock);
		RB_FOREACH_SAFE(fep, fuse_dent_tree,
				&dfnp->dent_head, fep_temp)
		{
			if (fep->fnp == fnp) {
				fuse_dent_detach(dfnp, fep);
				fuse_dent_free(fep);
				break;
			}
		}
		mtx_unlock(&dfnp->node_lock);
	}

	mtx_lock(&fnp->node_lock);
	if (fnp->type == VDIR) {
		while ((fep = RB_ROOT(&fnp->dent_head))) {
			fuse_dent_detach(fnp, fep);
			fuse_dent_free(fep);
		}
	}
	if (fnp->nlink == 0) {
		fnp->nlink = -123; /* debug */
		mtx_unlock(&fnp->node_lock);
		objcache_put(fuse_node_objcache, fnp);
	} else {
		kprintf("fuse: fnp %p freed while still attached!\n", fnp);
		mtx_unlock(&fnp->node_lock);
	}
}

void
fuse_dent_new(struct fuse_node *fnp, const char *name, int namelen,
    struct fuse_dent **fepp)
{
	struct fuse_dent *fep;

	fep = objcache_get(fuse_dent_objcache, M_WAITOK);
	bzero(fep, sizeof(*fep));

	if (namelen >= 0)
		fep->name = kstrndup(name, namelen, M_TEMP);
	else
		fep->name = kstrdup(name, M_TEMP);
	KKASSERT(fep->name);
	fep->fnp = fnp;

	*fepp = fep;
	KKASSERT(*fepp);
}

void
fuse_dent_free(struct fuse_dent *fep)
{
	fuse_dbg("free dent=\"%s\"\n", fep->name);

	if (fep->name) {
		kfree(fep->name, M_TEMP);
		fep->name = NULL;
	}

	fep->fnp = NULL;
	objcache_put(fuse_dent_objcache, fep);
}

void
fuse_dent_attach(struct fuse_node *dfnp, struct fuse_dent *fep)
{
	KKASSERT(dfnp);
	KKASSERT(dfnp->type == VDIR);
	KKASSERT(mtx_islocked_ex(&dfnp->node_lock));

	RB_INSERT(fuse_dent_tree, &dfnp->dent_head, fep);
	fep->fnp->pfnp = dfnp;
	++fep->fnp->nlink;
}

void
fuse_dent_detach(struct fuse_node *dfnp, struct fuse_dent *fep)
{
	KKASSERT(dfnp);
	KKASSERT(dfnp->type == VDIR);
	KKASSERT(mtx_islocked_ex(&dfnp->node_lock));

	RB_REMOVE(fuse_dent_tree, &dfnp->dent_head, fep);
	fep->fnp->pfnp = NULL;
	--fep->fnp->nlink;
}

int
fuse_dent_find(struct fuse_node *dfnp, const char *name, int namelen,
    struct fuse_dent **fepp)
{
	struct fuse_dent *fep, find;
	int error;

	if (namelen >= 0)
		find.name = kstrndup(name, namelen, M_TEMP);
	else
		find.name = kstrdup(name, M_TEMP);
	KKASSERT(find.name);

	fep = RB_FIND(fuse_dent_tree, &dfnp->dent_head, &find);
	if (fep) {
		error = 0;
		if (fepp)
			*fepp = fep;
	} else {
		error = ENOENT;
		fuse_dbg("dent=\"%s\" not found\n", find.name);
	}

	kfree(find.name, M_TEMP);

	return error;
}

int
fuse_alloc_node(struct fuse_node *dfnp, uint64_t ino,
		const char *name, int namelen,
		enum vtype vtyp, struct vnode **vpp)
{
	struct fuse_node *fnp = NULL;
	struct fuse_dent *fep = NULL;
	int error;

	KKASSERT(dfnp->type == VDIR);
	if (vtyp == VBLK || vtyp == VCHR || vtyp == VFIFO)
		return EINVAL;

	mtx_lock(&dfnp->node_lock);
	error = fuse_dent_find(dfnp, name, namelen, &fep);
	if (error == 0) {
		mtx_unlock(&dfnp->node_lock);
		return EEXIST;
	}
	if (error == ENOENT) {
		fuse_node_new(dfnp->fmp, ino, vtyp, &fnp);
		mtx_lock(&fnp->node_lock);
		fuse_dent_new(fnp, name, namelen, &fep);
		fuse_dent_attach(dfnp, fep);
		mtx_unlock(&fnp->node_lock);
		error = 0;
	}
	mtx_unlock(&dfnp->node_lock);

	error = fuse_node_vn(fnp, vpp);
	if (error) {
		mtx_lock(&dfnp->node_lock);
		fuse_dent_detach(dfnp, fep);
		fuse_dent_free(fep);
		mtx_unlock(&dfnp->node_lock);
		fuse_node_free(fnp);
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

	fuse_dent_objcache = objcache_create("fuse_dent", 0, 0,
	    NULL, NULL, NULL,
	    objcache_malloc_alloc_zero, objcache_malloc_free, &fuse_dent_args);
}

void
fuse_node_cleanup(void)
{
	objcache_destroy(fuse_node_objcache);
	objcache_destroy(fuse_dent_objcache);
}
