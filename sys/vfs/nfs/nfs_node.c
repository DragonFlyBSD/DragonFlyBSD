/*
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Rick Macklem at The University of Guelph.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
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
 *	@(#)nfs_node.c	8.6 (Berkeley) 5/22/95
 * $FreeBSD: src/sys/nfs/nfs_node.c,v 1.36.2.3 2002/01/05 22:25:04 dillon Exp $
 */


#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/mount.h>
#include <sys/namei.h>
#include <sys/vnode.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/fnv_hash.h>
#include <sys/objcache.h>

#include "rpcv2.h"
#include "nfsproto.h"
#include "nfs.h"
#include "nfsmount.h"
#include "nfsnode.h"

static MALLOC_DEFINE(M_NFSNODE, "NFS node", "NFS node");

static struct objcache *nfsnode_objcache;
static LIST_HEAD(nfsnodehashhead, nfsnode) *nfsnodehashtbl;
static u_long nfsnodehash;
static lwkt_token nfsnhash_token = LWKT_TOKEN_INITIALIZER(nfsnhash_token);
static struct lock nfsnhash_lock;

#define TRUE	1
#define	FALSE	0

#define NFSNOHASH(fhsum)	(&nfsnodehashtbl[(fhsum) & nfsnodehash])

/*
 * Initialize hash links for nfsnodes
 * and build nfsnode free list.
 */
void
nfs_nhinit(void)
{
	nfsnode_objcache = objcache_create_simple(M_NFSNODE, sizeof(struct nfsnode));
	nfsnodehashtbl = hashinit(desiredvnodes, M_NFSHASH, &nfsnodehash);
	lockinit(&nfsnhash_lock, "nfsnht", 0, 0);
}

/*
 * Look up a vnode/nfsnode by file handle.
 * Callers must check for mount points!!
 * In all cases, a pointer to a
 * nfsnode structure is returned.
 */

int
nfs_nget(struct mount *mntp, nfsfh_t *fhp, int fhsize, struct nfsnode **npp,
	 struct vnode *notvp)
{
	struct nfsnode *np, *np2;
	struct nfsnodehashhead *nhpp;
	struct vnode *vp;
	int error;
	int lkflags;
	struct nfsmount *nmp;

	/*
	 * Calculate nfs mount point and figure out whether the rslock should
	 * be interruptable or not.
	 */
	nmp = VFSTONFS(mntp);
	if (nmp->nm_flag & NFSMNT_INT)
		lkflags = LK_PCATCH;
	else
		lkflags = 0;

	lwkt_gettoken(&nfsnhash_token);

retry:
	nhpp = NFSNOHASH(fnv_32_buf(fhp->fh_bytes, fhsize, FNV1_32_INIT));
loop:
	LIST_FOREACH(np, nhpp, n_hash) {
		if (mntp != NFSTOV(np)->v_mount || np->n_fhsize != fhsize ||
		    bcmp((caddr_t)fhp, (caddr_t)np->n_fhp, fhsize)) {
			continue;
		}
		vp = NFSTOV(np);
		if (vp == notvp) {
			kprintf("nfs warning: client-client collision "
				"during rename/link/softlink\n");
			*npp = NULL;
			lwkt_reltoken(&nfsnhash_token);
			return (ESTALE);
		}
		if (vget(vp, LK_EXCLUSIVE))
			goto loop;
		LIST_FOREACH(np, nhpp, n_hash) {
			if (mntp == NFSTOV(np)->v_mount &&
			    np->n_fhsize == fhsize &&
			    bcmp((caddr_t)fhp, (caddr_t)np->n_fhp, fhsize) == 0
			) {
				break;
			}
		}
		if (np == NULL || NFSTOV(np) != vp) {
			vput(vp);
			goto loop;
		}
		*npp = np;
		lwkt_reltoken(&nfsnhash_token);
		return(0);
	}

	/*
	 * Obtain a lock to prevent a race condition if the getnewvnode()
	 * or MALLOC() below happens to block.
	 */
	if (lockmgr(&nfsnhash_lock, LK_EXCLUSIVE | LK_SLEEPFAIL))
		goto loop;

	/*
	 * Allocate before getnewvnode since doing so afterward
	 * might cause a bogus v_data pointer to get dereferenced
	 * elsewhere if objcache should block.
	 */
	np = objcache_get(nfsnode_objcache, M_WAITOK);
		
	error = getnewvnode(VT_NFS, mntp, &vp, 0, 0);
	if (error) {
		lockmgr(&nfsnhash_lock, LK_RELEASE);
		*npp = NULL;
		objcache_put(nfsnode_objcache, np);
		lwkt_reltoken(&nfsnhash_token);
		return (error);
	}

	/*
	 * Initialize most of (np).
	 */
	bzero(np, sizeof (*np));
	if (fhsize > NFS_SMALLFH) {
		np->n_fhp = kmalloc(fhsize, M_NFSBIGFH, M_WAITOK);
	} else {
		np->n_fhp = &np->n_fh;
	}
	bcopy((caddr_t)fhp, (caddr_t)np->n_fhp, fhsize);
	np->n_fhsize = fhsize;
	lockinit(&np->n_rslock, "nfrslk", 0, lkflags);

	/*
	 * Validate that we did not race another nfs_nget() due to blocking
	 * here and there.
	 */
	for (np2 = nhpp->lh_first; np2 != NULL; np2 = np2->n_hash.le_next) {
		if (mntp != NFSTOV(np2)->v_mount || np2->n_fhsize != fhsize ||
		    bcmp((caddr_t)fhp, (caddr_t)np2->n_fhp, fhsize)) {
			continue;
		}
		vx_put(vp);
		lockmgr(&nfsnhash_lock, LK_RELEASE);

		if (np->n_fhsize > NFS_SMALLFH)
			kfree((caddr_t)np->n_fhp, M_NFSBIGFH);
		np->n_fhp = NULL;
		objcache_put(nfsnode_objcache, np);
		goto retry;
	}

	/*
	 * Finish connecting up (np, vp) and insert the nfsnode in the
	 * hash for its new file handle.
	 *
	 * nvp is locked & refd so effectively so is np.
	 */
	np->n_vnode = vp;
	vp->v_data = np;
	LIST_INSERT_HEAD(nhpp, np, n_hash);
	*npp = np;
	lockmgr(&nfsnhash_lock, LK_RELEASE);
	lwkt_reltoken(&nfsnhash_token);

	return (0);
}

/*
 * Nonblocking version of nfs_nget()
 */
int
nfs_nget_nonblock(struct mount *mntp, nfsfh_t *fhp, int fhsize,
		  struct nfsnode **npp, struct vnode *notvp)
{
	struct nfsnode *np, *np2;
	struct nfsnodehashhead *nhpp;
	struct vnode *vp;
	int error;
	int lkflags;
	struct nfsmount *nmp;

	/*
	 * Calculate nfs mount point and figure out whether the rslock should
	 * be interruptable or not.
	 */
	nmp = VFSTONFS(mntp);
	if (nmp->nm_flag & NFSMNT_INT)
		lkflags = LK_PCATCH;
	else
		lkflags = 0;
	vp = NULL;
	*npp = NULL;

	lwkt_gettoken(&nfsnhash_token);

retry:
	nhpp = NFSNOHASH(fnv_32_buf(fhp->fh_bytes, fhsize, FNV1_32_INIT));
loop:
	LIST_FOREACH(np, nhpp, n_hash) {
		if (mntp != NFSTOV(np)->v_mount || np->n_fhsize != fhsize ||
		    bcmp((caddr_t)fhp, (caddr_t)np->n_fhp, fhsize)) {
			continue;
		}
		if (vp == NULL) {
			vp = NFSTOV(np);
			if (vp == notvp) {
				kprintf("nfs warning: client-client collision "
					"during rename/link/softlink\n");
				error = ESTALE;
				goto fail;
			}
			if (vget(vp, LK_EXCLUSIVE | LK_NOWAIT)) {
				error = EWOULDBLOCK;
				goto fail;
			}
			goto loop;
		}
		if (NFSTOV(np) != vp) {
			vput(vp);
			goto loop;
		}
		*npp = np;
		lwkt_reltoken(&nfsnhash_token);
		return(0);
	}

	/*
	 * Not found.  If we raced and had acquired a vp we have to release
	 * it here.
	 */
	if (vp) {
		vput(vp);
		vp = NULL;
	}

	/*
	 * Obtain a lock to prevent a race condition if the getnewvnode()
	 * or MALLOC() below happens to block.
	 */
	if (lockmgr(&nfsnhash_lock, LK_EXCLUSIVE | LK_SLEEPFAIL))
		goto loop;

	/*
	 * Entry not found, allocate a new entry.
	 *
	 * Allocate before getnewvnode since doing so afterward
	 * might cause a bogus v_data pointer to get dereferenced
	 * elsewhere if objcache should block.
	 */
	np = objcache_get(nfsnode_objcache, M_WAITOK);

	error = getnewvnode(VT_NFS, mntp, &vp, 0, 0);
	if (error) {
		lockmgr(&nfsnhash_lock, LK_RELEASE);
		objcache_put(nfsnode_objcache, np);
		lwkt_reltoken(&nfsnhash_token);
		return (error);
	}

	/*
	 * Initialize most of (np).
	 */
	bzero(np, sizeof (*np));
	if (fhsize > NFS_SMALLFH) {
		np->n_fhp = kmalloc(fhsize, M_NFSBIGFH, M_WAITOK);
	} else {
		np->n_fhp = &np->n_fh;
	}
	bcopy((caddr_t)fhp, (caddr_t)np->n_fhp, fhsize);
	np->n_fhsize = fhsize;
	lockinit(&np->n_rslock, "nfrslk", 0, lkflags);

	/*
	 * Validate that we did not race another nfs_nget() due to blocking
	 * here and there.
	 */
	for (np2 = nhpp->lh_first; np2 != NULL; np2 = np2->n_hash.le_next) {
		if (mntp != NFSTOV(np2)->v_mount || np2->n_fhsize != fhsize ||
		    bcmp((caddr_t)fhp, (caddr_t)np2->n_fhp, fhsize)) {
			continue;
		}
		vx_put(vp);
		lockmgr(&nfsnhash_lock, LK_RELEASE);

		if (np->n_fhsize > NFS_SMALLFH)
			kfree((caddr_t)np->n_fhp, M_NFSBIGFH);
		np->n_fhp = NULL;
		objcache_put(nfsnode_objcache, np);

		/*
		 * vp state is retained on retry/loop so we must NULL it
		 * out here or fireworks may ensue.
		 */
		vp = NULL;
		goto retry;
	}

	/*
	 * Finish connecting up (np, vp) and insert the nfsnode in the
	 * hash for its new file handle.
	 *
	 * nvp is locked & refd so effectively so is np.
	 */
	np->n_vnode = vp;
	vp->v_data = np;
	LIST_INSERT_HEAD(nhpp, np, n_hash);

	/*
	 * nvp is locked & refd so effectively so is np.
	 */
	*npp = np;
	error = 0;
	lockmgr(&nfsnhash_lock, LK_RELEASE);
fail:
	lwkt_reltoken(&nfsnhash_token);
	return (error);
}

/*
 * nfs_inactive(struct vnode *a_vp)
 *
 * NOTE: the passed vnode is locked but not referenced.  On return the
 * vnode must be unlocked and not referenced.
 */
int
nfs_inactive(struct vop_inactive_args *ap)
{
	struct nfsmount *nmp = VFSTONFS(ap->a_vp->v_mount);
	struct nfsnode *np;
	struct sillyrename *sp;

	lwkt_gettoken(&nmp->nm_token);

	np = VTONFS(ap->a_vp);
	if (prtactive && VREFCNT(ap->a_vp) > 1)
		vprint("nfs_inactive: pushing active", ap->a_vp);
	if (ap->a_vp->v_type != VDIR) {
		sp = np->n_sillyrename;
		np->n_sillyrename = NULL;
	} else {
		sp = NULL;
	}
	if (sp) {
		/*
		 * We need a reference to keep the vnode from being
		 * recycled by getnewvnode while we do the I/O
		 * associated with discarding the buffers.  The vnode
		 * is already locked.
		 */
		nfs_vinvalbuf(ap->a_vp, 0, 1);

		/*
		 * Remove the silly file that was rename'd earlier
		 */
		nfs_removeit(sp);
		crfree(sp->s_cred);
		vrele(sp->s_dvp);
		kfree((caddr_t)sp, M_NFSREQ);
	}

	np->n_flag &= ~(NWRITEERR | NACC | NUPD | NCHG | NLOCKED | NWANTED);
	lwkt_reltoken(&nmp->nm_token);

	return (0);
}

/*
 * Reclaim an nfsnode so that it can be used for other purposes.
 *
 * There should be no direct references to the related nfs node
 * since nobody is holding the vnode any more, other than hash
 * lookups which are interlocked against nfsnhash_token and vget().
 *
 * nfs_reclaim(struct vnode *a_vp)
 */
int
nfs_reclaim(struct vop_reclaim_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct nfsnode *np = VTONFS(vp);
	struct nfsdmap *dp, *dp2;
/*	struct nfsmount *nmp = VFSTONFS(vp->v_mount);*/

	if (prtactive && VREFCNT(vp) > 1)
		vprint("nfs_reclaim: pushing active", vp);


	/*
	 * Remove from hash table and remove the cross links.
	 *
	 * NOTE: Other NFS code may look up a np and vget() the
	 *	 related vnode, then will check np->n_vnode.
	 *	 We must clear np->n_vnode here to ensure that all
	 *	 possible races are dealt with.
	 */
	lwkt_gettoken(&nfsnhash_token);
	KKASSERT(np->n_vnode == vp);
	if (np->n_hash.le_prev != NULL)
		LIST_REMOVE(np, n_hash);
	np->n_vnode = NULL;
	vp->v_data = NULL;
	lwkt_reltoken(&nfsnhash_token);

	/*
	 * Free up any directory cookie structures and
	 * large file handle structures that might be associated with
	 * this nfs node.
	 */
	if (vp->v_type == VDIR) {
		dp = np->n_cookies.lh_first;
		while (dp) {
			dp2 = dp;
			dp = dp->ndm_list.le_next;
			kfree((caddr_t)dp2, M_NFSDIROFF);
		}
	}
	if (np->n_fhsize > NFS_SMALLFH) {
		kfree((caddr_t)np->n_fhp, M_NFSBIGFH);
	}
	if (np->n_rucred) {
		crfree(np->n_rucred);
		np->n_rucred = NULL;
	}
	if (np->n_wucred) {
		crfree(np->n_wucred);
		np->n_wucred = NULL;
	}
	objcache_put(nfsnode_objcache, np);

	return (0);
}
