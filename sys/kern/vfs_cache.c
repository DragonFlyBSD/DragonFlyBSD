/*
 * Copyright (c) 2003,2004 The DragonFly Project.  All rights reserved.
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
 * Copyright (c) 1989, 1993, 1995
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Poul-Henning Kamp of the FreeBSD Project.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
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
 *	@(#)vfs_cache.c	8.5 (Berkeley) 3/22/95
 * $FreeBSD: src/sys/kern/vfs_cache.c,v 1.42.2.6 2001/10/05 20:07:03 dillon Exp $
 * $DragonFly: src/sys/kern/vfs_cache.c,v 1.40 2004/10/22 17:59:59 dillon Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>
#include <sys/mount.h>
#include <sys/vnode.h>
#include <sys/malloc.h>
#include <sys/sysproto.h>
#include <sys/proc.h>
#include <sys/namei.h>
#include <sys/nlookup.h>
#include <sys/filedesc.h>
#include <sys/fnv_hash.h>
#include <sys/globaldata.h>
#include <sys/kern_syscall.h>
#include <ddb/ddb.h>

/*
 * Random lookups in the cache are accomplished with a hash table using
 * a hash key of (nc_src_vp, name).
 *
 * Negative entries may exist and correspond to structures where nc_vp
 * is NULL.  In a negative entry, NCF_WHITEOUT will be set if the entry
 * corresponds to a whited-out directory entry (verses simply not finding the
 * entry at all).
 *
 * Upon reaching the last segment of a path, if the reference is for DELETE,
 * or NOCACHE is set (rewrite), and the name is located in the cache, it
 * will be dropped.
 */

/*
 * Structures associated with name cacheing.
 */
#define NCHHASH(hash)	(&nchashtbl[(hash) & nchash])
#define MINNEG		1024

MALLOC_DEFINE(M_VFSCACHE, "vfscache", "VFS name cache entries");

static LIST_HEAD(nchashhead, namecache) *nchashtbl;	/* Hash Table */
static struct namecache_list	ncneglist;		/* instead of vnode */

static u_long	nchash;			/* size of hash table */
SYSCTL_ULONG(_debug, OID_AUTO, nchash, CTLFLAG_RD, &nchash, 0, "");

static u_long	ncnegfactor = 16;	/* ratio of negative entries */
SYSCTL_ULONG(_debug, OID_AUTO, ncnegfactor, CTLFLAG_RW, &ncnegfactor, 0, "");

static u_long	numneg;		/* number of cache entries allocated */
SYSCTL_ULONG(_debug, OID_AUTO, numneg, CTLFLAG_RD, &numneg, 0, "");

static u_long	numcache;		/* number of cache entries allocated */
SYSCTL_ULONG(_debug, OID_AUTO, numcache, CTLFLAG_RD, &numcache, 0, "");

static u_long	numunres;		/* number of unresolved entries */
SYSCTL_ULONG(_debug, OID_AUTO, numunres, CTLFLAG_RD, &numunres, 0, "");

SYSCTL_INT(_debug, OID_AUTO, vnsize, CTLFLAG_RD, 0, sizeof(struct vnode), "");
SYSCTL_INT(_debug, OID_AUTO, ncsize, CTLFLAG_RD, 0, sizeof(struct namecache), "");

static int cache_resolve_mp(struct namecache *ncp);
static void cache_rehash(struct namecache *ncp);

/*
 * The new name cache statistics
 */
SYSCTL_NODE(_vfs, OID_AUTO, cache, CTLFLAG_RW, 0, "Name cache statistics");
#define STATNODE(mode, name, var) \
	SYSCTL_ULONG(_vfs_cache, OID_AUTO, name, mode, var, 0, "");
STATNODE(CTLFLAG_RD, numneg, &numneg);
STATNODE(CTLFLAG_RD, numcache, &numcache);
static u_long numcalls; STATNODE(CTLFLAG_RD, numcalls, &numcalls);
static u_long dothits; STATNODE(CTLFLAG_RD, dothits, &dothits);
static u_long dotdothits; STATNODE(CTLFLAG_RD, dotdothits, &dotdothits);
static u_long numchecks; STATNODE(CTLFLAG_RD, numchecks, &numchecks);
static u_long nummiss; STATNODE(CTLFLAG_RD, nummiss, &nummiss);
static u_long nummisszap; STATNODE(CTLFLAG_RD, nummisszap, &nummisszap);
static u_long numposzaps; STATNODE(CTLFLAG_RD, numposzaps, &numposzaps);
static u_long numposhits; STATNODE(CTLFLAG_RD, numposhits, &numposhits);
static u_long numnegzaps; STATNODE(CTLFLAG_RD, numnegzaps, &numnegzaps);
static u_long numneghits; STATNODE(CTLFLAG_RD, numneghits, &numneghits);

struct nchstats nchstats[SMP_MAXCPU];
/*
 * Export VFS cache effectiveness statistics to user-land.
 *
 * The statistics are left for aggregation to user-land so
 * neat things can be achieved, like observing per-CPU cache
 * distribution.
 */
static int
sysctl_nchstats(SYSCTL_HANDLER_ARGS)
{
	struct globaldata *gd;
	int i, error;

	error = 0;
	for (i = 0; i < ncpus; ++i) {
		gd = globaldata_find(i);
		if ((error = SYSCTL_OUT(req, (void *)&(*gd->gd_nchstats),
			sizeof(struct nchstats))))
			break;
	}

	return (error);
}
SYSCTL_PROC(_vfs_cache, OID_AUTO, nchstats, CTLTYPE_OPAQUE|CTLFLAG_RD,
  0, 0, sysctl_nchstats, "S,nchstats", "VFS cache effectiveness statistics");

static void cache_zap(struct namecache *ncp);

/*
 * cache_hold() and cache_drop() prevent the premature deletion of a
 * namecache entry but do not prevent operations (such as zapping) on
 * that namecache entry.
 */
static __inline
struct namecache *
_cache_hold(struct namecache *ncp)
{
	++ncp->nc_refs;
	return(ncp);
}

/*
 * When dropping an entry, if only one ref remains and the entry has not
 * been resolved, zap it.  Since the one reference is being dropped the
 * entry had better not be locked.
 */
static __inline
void
_cache_drop(struct namecache *ncp)
{
	KKASSERT(ncp->nc_refs > 0);
	if (ncp->nc_refs == 1 && 
	    (ncp->nc_flag & NCF_UNRESOLVED) && 
	    TAILQ_EMPTY(&ncp->nc_list)
	) {
		KKASSERT(ncp->nc_exlocks == 0);
		cache_lock(ncp);
		cache_zap(ncp);
	} else {
		--ncp->nc_refs;
	}
}

/*
 * Link a new namecache entry to its parent.  Be careful to avoid races
 * if vhold() blocks in the future.
 *
 * If we are creating a child under an oldapi parent we must mark the
 * child as being an oldapi entry as well.
 */
static void
cache_link_parent(struct namecache *ncp, struct namecache *par)
{
	KKASSERT(ncp->nc_parent == NULL);
	ncp->nc_parent = par;
	if (TAILQ_EMPTY(&par->nc_list)) {
		TAILQ_INSERT_HEAD(&par->nc_list, ncp, nc_entry);
		/*
		 * Any vp associated with an ncp which has children must
		 * be held to prevent it from being recycled.
		 */
		if (par->nc_vp)
			vhold(par->nc_vp);
	} else {
		TAILQ_INSERT_HEAD(&par->nc_list, ncp, nc_entry);
	}
}

/*
 * Remove the parent association from a namecache structure.  If this is
 * the last child of the parent the cache_drop(par) will attempt to
 * recursively zap the parent.
 */
static void
cache_unlink_parent(struct namecache *ncp)
{
	struct namecache *par;

	if ((par = ncp->nc_parent) != NULL) {
		ncp->nc_parent = NULL;
		par = cache_hold(par);
		TAILQ_REMOVE(&par->nc_list, ncp, nc_entry);
		if (par->nc_vp && TAILQ_EMPTY(&par->nc_list))
			vdrop(par->nc_vp);
		cache_drop(par);
	}
}

/*
 * Allocate a new namecache structure.
 */
static struct namecache *
cache_alloc(int nlen)
{
	struct namecache *ncp;

	ncp = malloc(sizeof(*ncp), M_VFSCACHE, M_WAITOK|M_ZERO);
	if (nlen)
		ncp->nc_name = malloc(nlen, M_VFSCACHE, M_WAITOK);
	ncp->nc_nlen = nlen;
	ncp->nc_flag = NCF_UNRESOLVED;
	ncp->nc_error = ENOTCONN;	/* needs to be resolved */
	ncp->nc_refs = 1;
	TAILQ_INIT(&ncp->nc_list);
	cache_lock(ncp);
	return(ncp);
}

static void
cache_free(struct namecache *ncp)
{
	KKASSERT(ncp->nc_refs == 1 && ncp->nc_exlocks == 1);
	if (ncp->nc_name)
		free(ncp->nc_name, M_VFSCACHE);
	free(ncp, M_VFSCACHE);
}

/*
 * Ref and deref a namecache structure.
 */
struct namecache *
cache_hold(struct namecache *ncp)
{
	return(_cache_hold(ncp));
}

void
cache_drop(struct namecache *ncp)
{
	_cache_drop(ncp);
}

/*
 * Namespace locking.  The caller must already hold a reference to the
 * namecache structure in order to lock/unlock it.  This function prevents
 * the namespace from being created or destroyed by accessors other then
 * the lock holder.
 *
 * Note that holding a locked namecache structure prevents other threads
 * from making namespace changes (e.g. deleting or creating), prevents
 * vnode association state changes by other threads, and prevents the
 * namecache entry from being resolved or unresolved by other threads.
 *
 * The lock owner has full authority to associate/disassociate vnodes
 * and resolve/unresolve the locked ncp.
 *
 * In particular, if a vnode is associated with a locked cache entry
 * that vnode will *NOT* be recycled.  We accomplish this by vhold()ing the
 * vnode.  XXX we should find a more efficient way to prevent the vnode
 * from being recycled, but remember that any given vnode may have multiple
 * namecache associations (think hardlinks).
 */
void
cache_lock(struct namecache *ncp)
{
	thread_t td;
	int didwarn;

	KKASSERT(ncp->nc_refs != 0);
	didwarn = 0;
	td = curthread;

	for (;;) {
		if (ncp->nc_exlocks == 0) {
			ncp->nc_exlocks = 1;
			ncp->nc_locktd = td;
			/* 
			 * The vp associated with a locked ncp must be held
			 * to prevent it from being recycled (which would
			 * cause the ncp to become unresolved).
			 *
			 * XXX loop on race for later MPSAFE work.
			 */
			if (ncp->nc_vp)
				vhold(ncp->nc_vp);
			break;
		}
		if (ncp->nc_locktd == td) {
			++ncp->nc_exlocks;
			break;
		}
		ncp->nc_flag |= NCF_LOCKREQ;
		if (tsleep(ncp, 0, "clock", hz) == EWOULDBLOCK) {
			if (didwarn)
				continue;
			didwarn = 1;
			printf("[diagnostic] cache_lock: blocked on %p", ncp);
			if ((ncp->nc_flag & NCF_MOUNTPT) && ncp->nc_mount)
			    printf(" [MOUNTPT %s]\n", ncp->nc_mount->mnt_stat.f_mntonname);
			else
			    printf(" \"%*.*s\"\n",
				ncp->nc_nlen, ncp->nc_nlen,
				ncp->nc_name);
		}
	}

	if (didwarn == 1) {
		printf("[diagnostic] cache_lock: unblocked %*.*s\n",
			ncp->nc_nlen, ncp->nc_nlen, ncp->nc_name);
	}
}

void
cache_unlock(struct namecache *ncp)
{
	thread_t td = curthread;

	KKASSERT(ncp->nc_refs > 0);
	KKASSERT(ncp->nc_exlocks > 0);
	KKASSERT(ncp->nc_locktd == td);
	if (--ncp->nc_exlocks == 0) {
		if (ncp->nc_vp)
			vdrop(ncp->nc_vp);
		ncp->nc_locktd = NULL;
		if (ncp->nc_flag & NCF_LOCKREQ) {
			ncp->nc_flag &= ~NCF_LOCKREQ;
			wakeup_one(ncp);
		}
	}
}

/*
 * ref-and-lock, unlock-and-deref functions.
 */
struct namecache *
cache_get(struct namecache *ncp)
{
	_cache_hold(ncp);
	cache_lock(ncp);
	return(ncp);
}

int
cache_get_nonblock(struct namecache *ncp)
{
	/* XXX MP */
	if (ncp->nc_exlocks == 0 || ncp->nc_locktd == curthread) {
		_cache_hold(ncp);
		cache_lock(ncp);
		return(0);
	}
	return(EWOULDBLOCK);
}

void
cache_put(struct namecache *ncp)
{
	cache_unlock(ncp);
	_cache_drop(ncp);
}

/*
 * Resolve an unresolved ncp by associating a vnode with it.  If the
 * vnode is NULL, a negative cache entry is created.
 *
 * The ncp should be locked on entry and will remain locked on return.
 */
void
cache_setvp(struct namecache *ncp, struct vnode *vp)
{
	KKASSERT(ncp->nc_flag & NCF_UNRESOLVED);
	ncp->nc_vp = vp;
	if (vp != NULL) {
		/*
		 * Any vp associated with an ncp which has children must
		 * be held.  Any vp associated with a locked ncp must be held.
		 */
		if (!TAILQ_EMPTY(&ncp->nc_list))
			vhold(vp);
		TAILQ_INSERT_HEAD(&vp->v_namecache, ncp, nc_vnode);
		if (ncp->nc_exlocks)
			vhold(vp);

		/*
		 * Set auxillary flags
		 */
		switch(vp->v_type) {
		case VDIR:
			ncp->nc_flag |= NCF_ISDIR;
			break;
		case VLNK:
			ncp->nc_flag |= NCF_ISSYMLINK;
			/* XXX cache the contents of the symlink */
			break;
		default:
			break;
		}
		++numcache;
		ncp->nc_error = 0;
	} else {
		TAILQ_INSERT_TAIL(&ncneglist, ncp, nc_vnode);
		++numneg;
		ncp->nc_error = ENOENT;
	}
	ncp->nc_flag &= ~NCF_UNRESOLVED;
}

/*
 * Disassociate the vnode or negative-cache association and mark a
 * namecache entry as unresolved again.  Note that the ncp is still
 * left in the hash table and still linked to its parent.
 *
 * The ncp should be locked and refd on entry and will remain locked and refd
 * on return.
 *
 * This routine is normally never called on a directory containing children.
 * However, NFS often does just that in its rename() code as a cop-out to
 * avoid complex namespace operations.  This disconnects a directory vnode
 * from its namecache and can cause the OLDAPI and NEWAPI to get out of
 * sync.
 */
void
cache_setunresolved(struct namecache *ncp)
{
	struct vnode *vp;

	if ((ncp->nc_flag & NCF_UNRESOLVED) == 0) {
		ncp->nc_flag |= NCF_UNRESOLVED;
		ncp->nc_flag &= ~(NCF_WHITEOUT|NCF_ISDIR|NCF_ISSYMLINK);
		ncp->nc_error = ENOTCONN;
		++numunres;
		if ((vp = ncp->nc_vp) != NULL) {
			--numcache;
			ncp->nc_vp = NULL;	/* safety */
			TAILQ_REMOVE(&vp->v_namecache, ncp, nc_vnode);

			/*
			 * Any vp associated with an ncp with children is
			 * held by that ncp.  Any vp associated with a locked
			 * ncp is held by that ncp.  These conditions must be
			 * undone when the vp is cleared out from the ncp.
			 */
			if (!TAILQ_EMPTY(&ncp->nc_list))
				vdrop(vp);
			if (ncp->nc_exlocks)
				vdrop(vp);
		} else {
			TAILQ_REMOVE(&ncneglist, ncp, nc_vnode);
			--numneg;
		}

#if 0
		if (TAILQ_FIRST(&ncp->nc_list)) {
			db_print_backtrace();
			printf("[diagnostic] cache_setunresolved() called on directory with children: %p %*.*s\n", ncp, ncp->nc_nlen, ncp->nc_nlen, ncp->nc_name);
		}
#endif
	}
}

/*
 * Invalidate portions of a namecache entry.  The passed ncp should be
 * referenced and locked but we might not adhere to that rule during the
 * old api -> new api transition period.
 *
 * CINV_PARENT		- disconnect the ncp from its parent
 * CINV_SELF		- same as cache_setunresolved(ncp)
 * CINV_CHILDREN	- disconnect children of the ncp from the ncp
 */
void
cache_inval(struct namecache *ncp, int flags)
{
	struct namecache *kid;
	struct namecache *nextkid;

	if (flags & CINV_SELF)
		cache_setunresolved(ncp);
	if (flags & CINV_PARENT) {
		ncp->nc_flag |= NCF_REVALPARENT;
		cache_unlink_parent(ncp);
	}

	/*
	 * TEMPORARY XX old-api / rename handling.  Any unresolved or
	 * negative cache-hit children with a ref count of 0 must be
	 * recursively destroyed or this disconnection from our parent,
	 * or the childrens disconnection from us, may leave them dangling
	 * forever.
	 *
	 * In the new API it won't be possible to unlink in the middle of
	 * the topology and we will have a cache_rename() to physically
	 * move a subtree from one place to another.
	 */
	if (flags & (CINV_PARENT|CINV_CHILDREN)) {
		if ((kid = TAILQ_FIRST(&ncp->nc_list)) != NULL)
			cache_hold(kid);
		while (kid) {
			if ((nextkid = TAILQ_NEXT(kid, nc_entry)) != NULL)
				cache_hold(nextkid);
			if (kid->nc_refs == 0 &&
			    ((kid->nc_flag & NCF_UNRESOLVED) || 
			     kid->nc_vp == NULL)
			) {
				cache_inval(kid, CINV_PARENT);
			}
			cache_drop(kid);
			kid = nextkid;
		}
	}

	/*
	 * TEMPORARY XXX old-api / rename handling.
	 */
	if (flags & CINV_CHILDREN) {
		while ((kid = TAILQ_FIRST(&ncp->nc_list)) != NULL) {
			kid->nc_flag |= NCF_REVALPARENT;
			cache_hold(kid);
			cache_unlink_parent(kid);
			cache_drop(kid);
		}
	}
}

void
cache_inval_vp(struct vnode *vp, int flags)
{
	struct namecache *ncp;

	if (flags & CINV_SELF) {
		while ((ncp = TAILQ_FIRST(&vp->v_namecache)) != NULL) {
			cache_hold(ncp);
			KKASSERT((ncp->nc_flag & NCF_UNRESOLVED) == 0);
			cache_inval(ncp, flags);
			cache_drop(ncp);
		}
	} else {
		TAILQ_FOREACH(ncp, &vp->v_namecache, nc_vnode) {
			cache_hold(ncp);
			cache_inval(ncp, flags);
			cache_drop(ncp);
		}
	}
}

/*
 * vget the vnode associated with the namecache entry.  Resolve the namecache
 * entry if necessary and deal with namecache/vp races.  The passed ncp must
 * be referenced and may be locked.  The ncp's ref/locking state is not 
 * effected by this call.
 *
 * lk_type may be LK_SHARED, LK_EXCLUSIVE.  A ref'd, possibly locked
 * (depending on the passed lk_type) will be returned in *vpp with an error
 * of 0, or NULL will be returned in *vpp with a non-0 error code.  The
 * most typical error is ENOENT, meaning that the ncp represents a negative
 * cache hit and there is no vnode to retrieve, but other errors can occur
 * too.
 *
 * The main race we have to deal with are namecache zaps.  The ncp itself
 * will not disappear since it is referenced, and it turns out that the
 * validity of the vp pointer can be checked simply by rechecking the
 * contents of ncp->nc_vp.
 */
int
cache_vget(struct namecache *ncp, struct ucred *cred,
	   int lk_type, struct vnode **vpp)
{
	struct vnode *vp;
	int error;

again:
	vp = NULL;
	if (ncp->nc_flag & NCF_UNRESOLVED) {
		cache_lock(ncp);
		error = cache_resolve(ncp, cred);
		cache_unlock(ncp);
	} else {
		error = 0;
	}
	if (error == 0 && (vp = ncp->nc_vp) != NULL) {
		error = vget(vp, lk_type, curthread);
		if (error) {
			if (vp != ncp->nc_vp)	/* handle cache_zap race */
				goto again;
			vp = NULL;
		} else if (vp != ncp->nc_vp) {	/* handle cache_zap race */
			vput(vp);
			goto again;
		}
	}
	if (error == 0 && vp == NULL)
		error = ENOENT;
	*vpp = vp;
	return(error);
}

int
cache_vref(struct namecache *ncp, struct ucred *cred, struct vnode **vpp)
{
	struct vnode *vp;
	int error;

again:
	vp = NULL;
	if (ncp->nc_flag & NCF_UNRESOLVED) {
		cache_lock(ncp);
		error = cache_resolve(ncp, cred);
		cache_unlock(ncp);
	} else {
		error = 0;
	}
	if (error == 0 && (vp = ncp->nc_vp) != NULL) {
		vref(vp);
		if (vp != ncp->nc_vp) {		/* handle cache_zap race */
			vrele(vp);
			goto again;
		}
	}
	if (error == 0 && vp == NULL)
		error = ENOENT;
	*vpp = vp;
	return(error);
}

/*
 * Zap a namecache entry.  The ncp is unconditionally set to an unresolved
 * state, which disassociates it from its vnode or ncneglist.
 *
 * Then, if there are no additional references to the ncp and no children,
 * the ncp is removed from the topology and destroyed.  This function will
 * also run through the nc_parent chain and destroy parent ncps if possible.
 * As a side benefit, it turns out the only conditions that allow running
 * up the chain are also the conditions to ensure no deadlock will occur.
 *
 * References and/or children may exist if the ncp is in the middle of the
 * topology, preventing the ncp from being destroyed.
 *
 * This function must be called with the ncp held and locked and will unlock
 * and drop it during zapping.
 */
static void
cache_zap(struct namecache *ncp)
{
	struct namecache *par;

	/*
	 * Disassociate the vnode or negative cache ref and set NCF_UNRESOLVED.
	 */
	cache_setunresolved(ncp);

	/*
	 * Try to scrap the entry and possibly tail-recurse on its parent.
	 * We only scrap unref'd (other then our ref) unresolved entries,
	 * we do not scrap 'live' entries.
	 */
	while (ncp->nc_flag & NCF_UNRESOLVED) {
		/*
		 * Someone other then us has a ref, stop.
		 */
		if (ncp->nc_refs > 1)
			goto done;

		/*
		 * We have children, stop.
		 */
		if (!TAILQ_EMPTY(&ncp->nc_list))
			goto done;

		/*
		 * Remove ncp from the topology: hash table and parent linkage.
		 */
		if (ncp->nc_flag & NCF_HASHED) {
			ncp->nc_flag &= ~NCF_HASHED;
			LIST_REMOVE(ncp, nc_hash);
		}
		if ((par = ncp->nc_parent) != NULL) {
			par = cache_hold(par);
			TAILQ_REMOVE(&par->nc_list, ncp, nc_entry);
			ncp->nc_parent = NULL;
			if (par->nc_vp && TAILQ_EMPTY(&par->nc_list))
				vdrop(par->nc_vp);
		}

		/*
		 * ncp should not have picked up any refs.  Physically
		 * destroy the ncp.
		 */
		KKASSERT(ncp->nc_refs == 1);
		--numunres;
		/* cache_unlock(ncp) not required */
		ncp->nc_refs = -1;	/* safety */
		if (ncp->nc_name)
			free(ncp->nc_name, M_VFSCACHE);
		free(ncp, M_VFSCACHE);

		/*
		 * Loop on the parent (it may be NULL).  Only bother looping
		 * if the parent has a single ref (ours), which also means
		 * we can lock it trivially.
		 */
		ncp = par;
		if (ncp == NULL)
			return;
		if (ncp->nc_refs != 1) {
			cache_drop(ncp);
			return;
		}
		KKASSERT(par->nc_exlocks == 0);
		cache_lock(ncp);
	}
done:
	cache_unlock(ncp);
	--ncp->nc_refs;
}

static enum { CHI_LOW, CHI_HIGH } cache_hysteresis_state = CHI_LOW;

static __inline
void
cache_hysteresis(void)
{
	/*
	 * Don't cache too many negative hits.  We use hysteresis to reduce
	 * the impact on the critical path.
	 */
	switch(cache_hysteresis_state) {
	case CHI_LOW:
		if (numneg > MINNEG && numneg * ncnegfactor > numcache) {
			cache_cleanneg(10);
			cache_hysteresis_state = CHI_HIGH;
		}
		break;
	case CHI_HIGH:
		if (numneg > MINNEG * 9 / 10 && 
		    numneg * ncnegfactor * 9 / 10 > numcache
		) {
			cache_cleanneg(10);
		} else {
			cache_hysteresis_state = CHI_LOW;
		}
		break;
	}
}

/*
 * NEW NAMECACHE LOOKUP API
 *
 * Lookup an entry in the cache.  A locked, referenced, non-NULL 
 * entry is *always* returned, even if the supplied component is illegal.
 * The returned namecache entry should be returned to the system with
 * cache_put() or cache_unlock() + cache_drop().
 *
 * namecache locks are recursive but care must be taken to avoid lock order
 * reversals.
 *
 * Nobody else will be able to manipulate the associated namespace (e.g.
 * create, delete, rename, rename-target) until the caller unlocks the
 * entry.
 *
 * The returned entry will be in one of three states:  positive hit (non-null
 * vnode), negative hit (null vnode), or unresolved (NCF_UNRESOLVED is set).
 * Unresolved entries must be resolved through the filesystem to associate the
 * vnode and/or determine whether a positive or negative hit has occured.
 *
 * It is not necessary to lock a directory in order to lock namespace under
 * that directory.  In fact, it is explicitly not allowed to do that.  A
 * directory is typically only locked when being created, renamed, or
 * destroyed.
 *
 * The directory (par) may be unresolved, in which case any returned child
 * will likely also be marked unresolved.  Likely but not guarenteed.  Since
 * the filesystem VOP_NEWLOOKUP() requires a resolved directory vnode the
 * caller is responsible for resolving the namecache chain top-down.  This API 
 * specifically allows whole chains to be created in an unresolved state.
 */
struct namecache *
cache_nlookup(struct namecache *par, struct nlcomponent *nlc)
{
	struct namecache *ncp;
	struct namecache *new_ncp;
	struct nchashhead *nchpp;
	u_int32_t hash;
	globaldata_t gd;

	numcalls++;
	gd = mycpu;

	/*
	 * Try to locate an existing entry
	 */
	hash = fnv_32_buf(nlc->nlc_nameptr, nlc->nlc_namelen, FNV1_32_INIT);
	hash = fnv_32_buf(&par, sizeof(par), hash);
	new_ncp = NULL;
restart:
	LIST_FOREACH(ncp, (NCHHASH(hash)), nc_hash) {
		numchecks++;

		/*
		 * Zap entries that have timed out.
		 */
		if (ncp->nc_timeout && 
		    (int)(ncp->nc_timeout - ticks) < 0 &&
		    (ncp->nc_flag & NCF_UNRESOLVED) == 0 &&
		    ncp->nc_exlocks == 0
		) {
			cache_zap(cache_get(ncp));
			goto restart;
		}

		/*
		 * Break out if we find a matching entry.  Note that
		 * UNRESOLVED entries may match.
		 */
		if (ncp->nc_parent == par &&
		    ncp->nc_nlen == nlc->nlc_namelen &&
		    bcmp(ncp->nc_name, nlc->nlc_nameptr, ncp->nc_nlen) == 0
		) {
			if (cache_get_nonblock(ncp) == 0) {
				if (new_ncp)
					cache_free(new_ncp);
				goto found;
			}
			cache_get(ncp);
			cache_put(ncp);
			goto restart;
		}
	}

	/*
	 * We failed to locate an entry, create a new entry and add it to
	 * the cache.  We have to relookup after possibly blocking in
	 * malloc.
	 */
	if (new_ncp == NULL) {
		new_ncp = cache_alloc(nlc->nlc_namelen);
		goto restart;
	}

	ncp = new_ncp;

	/*
	 * Initialize as a new UNRESOLVED entry, lock (non-blocking),
	 * and link to the parent.
	 */
	bcopy(nlc->nlc_nameptr, ncp->nc_name, nlc->nlc_namelen);
	nchpp = NCHHASH(hash);
	LIST_INSERT_HEAD(nchpp, ncp, nc_hash);
	ncp->nc_flag |= NCF_HASHED;
	cache_link_parent(ncp, par);
found:
	cache_hysteresis();
	return(ncp);
}

/*
 * Resolve an unresolved namecache entry, generally by looking it up.
 * The passed ncp must be locked and refd. 
 *
 * Theoretically since a vnode cannot be recycled while held, and since
 * the nc_parent chain holds its vnode as long as children exist, the
 * direct parent of the cache entry we are trying to resolve should
 * have a valid vnode.  If not then generate an error that we can 
 * determine is related to a resolver bug.
 */
int
cache_resolve(struct namecache *ncp, struct ucred *cred)
{
	struct namecache *par;
	struct namecache *scan;
	int error;

restart:
	/*
	 * If the ncp is already resolved we have nothing to do.
	 */
	if ((ncp->nc_flag & NCF_UNRESOLVED) == 0)
		return (ncp->nc_error);

	/*
	 * Mount points need special handling because the parent does not
	 * belong to the same filesystem as the ncp.
	 */
	if (ncp->nc_flag & NCF_MOUNTPT)
		return (cache_resolve_mp(ncp));

	/*
	 * We expect an unbroken chain of ncps to at least the mount point,
	 * and even all the way to root (but this code doesn't have to go
	 * past the mount point).
	 */
	if (ncp->nc_parent == NULL) {
		printf("EXDEV case 1 %p %*.*s\n", ncp,
			ncp->nc_nlen, ncp->nc_nlen, ncp->nc_name);
		ncp->nc_error = EXDEV;
		return(ncp->nc_error);
	}

	/*
	 * The vp's of the parent directories in the chain are held via vhold()
	 * due to the existance of the child, and should not disappear. 
	 * However, there are cases where they can disappear:
	 *
	 *	- due to filesystem I/O errors.
	 *	- due to NFS being stupid about tracking the namespace and
	 *	  destroys the namespace for entire directories quite often.
	 *	- due to forced unmounts.
	 *
	 * When this occurs we have to track the chain backwards and resolve
	 * it, looping until the resolver catches up to the current node.  We
	 * could recurse here but we might run ourselves out of kernel stack
	 * so we do it in a more painful manner.  This situation really should
	 * not occur all that often, or if it does not have to go back too
	 * many nodes to resolve the ncp.
	 */
	while (ncp->nc_parent->nc_vp == NULL) {
		par = ncp->nc_parent;
		while (par->nc_parent && par->nc_parent->nc_vp == NULL)
			par = par->nc_parent;
		if (par->nc_parent == NULL) {
			printf("EXDEV case 2 %*.*s\n",
				par->nc_nlen, par->nc_nlen, par->nc_name);
			return (EXDEV);
		}
		printf("[diagnostic] cache_resolve: had to recurse on %*.*s\n",
			par->nc_nlen, par->nc_nlen, par->nc_name);
		/*
		 * The parent is not set in stone, ref and lock it to prevent
		 * it from disappearing.  Also note that due to renames it
		 * is possible for our ncp to move and for par to no longer
		 * be one of its parents.  We resolve it anyway, the loop 
		 * will handle any moves.
		 */
		cache_get(par);
		if (par->nc_flag & NCF_MOUNTPT) {
			cache_resolve_mp(par);
		} else if (par->nc_parent->nc_vp == NULL) {
			printf("[diagnostic] cache_resolve: raced on %*.*s\n", par->nc_nlen, par->nc_nlen, par->nc_name);
			cache_put(par);
			continue;
		} else {
			par->nc_error = 
			    vop_resolve(par->nc_parent->nc_vp->v_ops, par, cred);
		}
		if ((error = par->nc_error) != 0) {
			if (par->nc_error != EAGAIN) {
				printf("EXDEV case 3 %*.*s error %d\n",
				    par->nc_nlen, par->nc_nlen, par->nc_name,
				    par->nc_error);
				cache_put(par);
				return(error);
			}
			printf("[diagnostic] cache_resolve: EAGAIN par %p %*.*s\n",
				par, par->nc_nlen, par->nc_nlen, par->nc_name);
		}
		cache_put(par);
		/* loop */
	}

	/*
	 * Call vop_resolve() to get the vp, then scan for any disconnected
	 * ncp's and reattach them.  If this occurs the original ncp is marked
	 * EAGAIN to force a relookup.
	 */
	KKASSERT((ncp->nc_flag & NCF_MOUNTPT) == 0);
	ncp->nc_error = vop_resolve(ncp->nc_parent->nc_vp->v_ops, ncp, cred);
	if (ncp->nc_error == EAGAIN) {
		printf("[diagnostic] cache_resolve: EAGAIN ncp %p %*.*s\n",
			ncp, ncp->nc_nlen, ncp->nc_nlen, ncp->nc_name);
		goto restart;
	}
	if (ncp->nc_error == 0) {
		TAILQ_FOREACH(scan, &ncp->nc_vp->v_namecache, nc_vnode) {
			if (scan != ncp && (scan->nc_flag & NCF_REVALPARENT)) {
				cache_hold(scan);
				cache_link_parent(scan, ncp->nc_parent);
				cache_unlink_parent(ncp);
				scan->nc_flag &= ~NCF_REVALPARENT;
				ncp->nc_error = EAGAIN;
				if (scan->nc_flag & NCF_HASHED)
					cache_rehash(scan);
				printf("[diagnostic] cache_resolve: relinked %*.*s\n", scan->nc_nlen, scan->nc_nlen, scan->nc_name);
				cache_drop(scan);
				break;
			}
		}
	}
	return(ncp->nc_error);
}

/*
 * Resolve the ncp associated with a mount point.  Such ncp's almost always
 * remain resolved and this routine is rarely called.  NFS MPs tends to force
 * re-resolution more often due to its mac-truck-smash-the-namecache
 * method of tracking namespace changes.
 *
 * The passed ncp must be locked.
 */
static int
cache_resolve_mp(struct namecache *ncp)
{
	struct vnode *vp;
	struct mount *mp = ncp->nc_mount;

	KKASSERT(mp != NULL);
	if (ncp->nc_flag & NCF_UNRESOLVED) {
		while (vfs_busy(mp, 0, NULL, curthread))
			;
		ncp->nc_error = VFS_ROOT(mp, &vp);
		if (ncp->nc_error == 0) {
			cache_setvp(ncp, vp);
			vput(vp);
		} else {
			printf("[diagnostic] cache_resolve_mp: failed to resolve mount %p\n", mp);
			cache_setvp(ncp, NULL);
		}
		vfs_unbusy(mp, curthread);
	}
	return(ncp->nc_error);
}

/*
 * Lookup an entry in the cache.
 *
 * XXX OLD API ROUTINE!  WHEN ALL VFSs HAVE BEEN CLEANED UP THIS PROCEDURE
 * WILL BE REMOVED.  NOTE: even though this is an old api function it had
 * to be modified to vref() the returned vnode (whereas in 4.x an unreferenced
 * vnode was returned).  This is necessary because our namecache structure
 * manipulation can cause the vnode to be recycled if it isn't refd.
 *
 * Lookup is called with dvp pointing to the directory to search,
 * cnp pointing to the name of the entry being sought. 
 *
 * If the lookup succeeds, a REFd but unlocked vnode is returned in *vpp,
 * and a status of -1 is returned.
 *
 * If the lookup determines that the name does not exist (negative cacheing),
 * a status of ENOENT is returned. 
 *
 * If the lookup fails, a status of zero is returned.
 *
 * Matching UNRESOLVED entries are resolved.
 *
 * HACKS: we create dummy nodes for parents
 */
int
cache_lookup(struct vnode *dvp, struct vnode **vpp, struct componentname *cnp)
{
	struct namecache *ncp;
	struct namecache *par;
	struct namecache *bpar;
	u_int32_t hash;
	globaldata_t gd = mycpu;

	numcalls++;
	*vpp = NULL;

	/*
	 * Obtain the namecache entry associated with dvp.  If there is no
	 * entry then assume a miss.
	 */
	if ((par = TAILQ_FIRST(&dvp->v_namecache)) == NULL) {
		if ((cnp->cn_flags & CNP_MAKEENTRY) == 0) {
			nummisszap++;
		} else {
			nummiss++;
		}
		gd->gd_nchstats->ncs_miss++;
		return (0);
	}

	/*
	 * Deal with "." and "..".   Note that if the namecache is disjoint,
	 * we won't find a vnode for ".." and we return a miss.
	 */
	if (cnp->cn_nameptr[0] == '.') {
		if (cnp->cn_namelen == 1) {
			*vpp = dvp;
			vref(*vpp);
			dothits++;
			numposhits++;	/* include in total statistics */
			return (-1);
		}
		if (cnp->cn_namelen == 2 && cnp->cn_nameptr[1] == '.') {
			if ((cnp->cn_flags & CNP_MAKEENTRY) == 0) {
				dotdothits++;
				numposhits++;
				return (0);
			}
			if (par->nc_parent == NULL ||
			    par->nc_parent->nc_vp == NULL) {
				nummiss++;
				gd->gd_nchstats->ncs_miss++;
				return (0);
			}
			*vpp = par->nc_parent->nc_vp;
			vref(*vpp);
			dotdothits++;
			numposhits++;	/* include in total statistics */
			return (-1);
		}
	}

	/*
	 * Try to locate an existing entry
	 */
	cache_hold(par);
	hash = fnv_32_buf(cnp->cn_nameptr, cnp->cn_namelen, FNV1_32_INIT);
	bpar = par;
	hash = fnv_32_buf(&bpar, sizeof(bpar), hash);
restart:
	LIST_FOREACH(ncp, (NCHHASH(hash)), nc_hash) {
		numchecks++;

		/*
		 * Zap entries that have timed out.  Don't do anything if
		 * the entry is in an unresolved state or is held locked.
		 */
		if (ncp->nc_timeout && 
		    (int)(ncp->nc_timeout - ticks) < 0 &&
		    (ncp->nc_flag & NCF_UNRESOLVED) == 0 &&
		    ncp->nc_exlocks == 0
		) {
			cache_zap(cache_get(ncp));
			goto restart;
		}

		/*
		 * Break out if we find a matching entry.
		 */
		if (ncp->nc_parent == par &&
		    ncp->nc_nlen == cnp->cn_namelen &&
		    bcmp(ncp->nc_name, cnp->cn_nameptr, ncp->nc_nlen) == 0
		) {
			if (cache_get_nonblock(ncp) == 0)
				break;
			cache_get(ncp);
			cache_put(ncp);
			goto restart;
		}
	}
	cache_drop(par);

	/*
	 * We found an entry but it is unresolved, act the same as if we
	 * failed to locate the entry.  cache_enter() will do the right
	 * thing.
	 */
	if (ncp && (ncp->nc_flag & NCF_UNRESOLVED)) {
		cache_put(ncp);
		ncp = NULL;
	}

	/*
	 * If we failed to locate an entry, return 0 (indicates failure).
	 */
	if (ncp == NULL) {
		if ((cnp->cn_flags & CNP_MAKEENTRY) == 0) {
			nummisszap++;
		} else {
			nummiss++;
		}
		gd->gd_nchstats->ncs_miss++;
		return (0);
	}

	/*
	 * If we found an entry, but we don't want to have one, we just
	 * return.  The old API tried to zap the entry in the vfs_lookup()
	 * phase but this is too early to know whether the operation
	 * will have succeeded or not.  The new API zaps it after the
	 * operation has succeeded, not here.
	 *
	 * At the same time, the old api's rename() function uses the
	 * old api lookup to clear out any negative cache hit on the
	 * target name.  We still have to do that.
	 */
	if ((cnp->cn_flags & CNP_MAKEENTRY) == 0) {
		if (cnp->cn_nameiop == NAMEI_RENAME && ncp->nc_vp == NULL)
			cache_zap(ncp);
		else
			cache_put(ncp);
		return (0);
	}

	/*
	 * If the vnode is not NULL then return the positive match.
	 */
	if (ncp->nc_vp) {
		numposhits++;
		gd->gd_nchstats->ncs_goodhits++;
		*vpp = ncp->nc_vp;
		vref(*vpp);
		cache_put(ncp);
		return (-1);
	}

	/*
	 * If the vnode is NULL we found a negative match.  If we want to
	 * create it, purge the negative match and return failure (as if
	 * we hadn't found a match in the first place).
	 */
	if (cnp->cn_nameiop == NAMEI_CREATE) {
		numnegzaps++;
		gd->gd_nchstats->ncs_badhits++;
		cache_zap(ncp);
		return (0);
	}

	numneghits++;

	/*
	 * We found a "negative" match, ENOENT notifies client of this match.
	 * The nc_flag field records whether this is a whiteout.  Since there
	 * is no vnode we can use the vnode tailq link field with ncneglist.
	 */
	TAILQ_REMOVE(&ncneglist, ncp, nc_vnode);
	TAILQ_INSERT_TAIL(&ncneglist, ncp, nc_vnode);
	gd->gd_nchstats->ncs_neghits++;
	if (ncp->nc_flag & NCF_WHITEOUT)
		cnp->cn_flags |= CNP_ISWHITEOUT;
	cache_put(ncp);
	return (ENOENT);
}

/*
 * Add an entry to the cache.  (OLD API)
 *
 * XXX OLD API ROUTINE!  WHEN ALL VFSs HAVE BEEN CLEANED UP THIS PROCEDURE
 * WILL BE REMOVED.
 *
 * Generally speaking this is 'optional'.  It's ok to do nothing at all.
 * The only reason I don't just return is to try to set nc_timeout if
 * requested.
 */
void
cache_enter(struct vnode *dvp, struct vnode *vp, struct componentname *cnp)
{
	struct namecache *par;
	struct namecache *ncp;
	struct namecache *new_ncp;
	struct namecache *bpar;
	struct nchashhead *nchpp;
	u_int32_t hash;

	/*
	 * If the directory has no namecache entry we bail.  This will result
	 * in a lot of misses but frankly we don't have much of a choice if
	 * we want to be compatible with the new api's storage scheme.
	 */
	if ((ncp = TAILQ_FIRST(&dvp->v_namecache)) == NULL)
		return;
	cache_hold(ncp);

	/*
	 * This may be a bit confusing.  "." and ".." are 'virtual' entries.
	 * We do not actually create a namecache entry representing either.
	 * However, the ".." case is used to linkup a potentially disjoint
	 * directory with its parent, to disconnect a directory from its
	 * parent, or to change an existing linkage that may no longer be
	 * correct (as might occur when a subdirectory is renamed).
	 */

	if (cnp->cn_namelen == 1 && cnp->cn_nameptr[0] == '.') {
		cache_drop(ncp);
		return;
	}
	if (cnp->cn_namelen == 2 && cnp->cn_nameptr[0] == '.' &&
	    cnp->cn_nameptr[1] == '.'
	) {
		cache_drop(ncp);
		return;
	}

	/*
	 * Ok, no special cases, ncp is actually the parent directory so
	 * assign it to par.  Note that it is held.
	 */
	par = ncp;

	/*
	 * Try to find a match in the hash table, allocate a new entry if
	 * we can't.  We have to retry the loop after any potential blocking
	 * situation.
	 */
	bpar = par;
	hash = fnv_32_buf(cnp->cn_nameptr, cnp->cn_namelen, FNV1_32_INIT);
	hash = fnv_32_buf(&bpar, sizeof(bpar), hash);

	new_ncp = NULL;
againagain:
	LIST_FOREACH(ncp, (NCHHASH(hash)), nc_hash) {
		numchecks++;

		/*
		 * Break out if we find a matching entry.  Because cache_enter
		 * is called with one or more vnodes potentially locked, we
		 * cannot block trying to get the ncp lock (or we might 
		 * deadlock).
		 */
		if (ncp->nc_parent == par &&
		    ncp->nc_nlen == cnp->cn_namelen &&
		    bcmp(ncp->nc_name, cnp->cn_nameptr, ncp->nc_nlen) == 0
		) {
			if (cache_get_nonblock(ncp) != 0) {
				printf("[diagnostic] cache_enter: avoided race on %p %*.*s\n", ncp, ncp->nc_nlen, ncp->nc_nlen, ncp->nc_name);
				cache_drop(par);
				return;
			}
			break;
		}
	}
	if (ncp == NULL) {
		if (new_ncp == NULL) {
			new_ncp = cache_alloc(cnp->cn_namelen);
			goto againagain;
		}
		ncp = new_ncp;
		bcopy(cnp->cn_nameptr, ncp->nc_name, cnp->cn_namelen);
		nchpp = NCHHASH(hash);
		LIST_INSERT_HEAD(nchpp, ncp, nc_hash);
		ncp->nc_flag |= NCF_HASHED;
		cache_link_parent(ncp, par);
	} else if (new_ncp) {
		cache_free(new_ncp);
	}
	cache_drop(par);

	/*
	 * Avoid side effects if we are simply re-entering the same
	 * information.
	 */
	if ((ncp->nc_flag & NCF_UNRESOLVED) == 0 && ncp->nc_vp == vp) {
		ncp->nc_error = vp ? 0 : ENOENT;
	} else {
		cache_setunresolved(ncp);
		cache_setvp(ncp, vp);
	}

	/*
	 * Set a timeout
	 */
	if (cnp->cn_flags & CNP_CACHETIMEOUT) {
		if ((ncp->nc_timeout = ticks + cnp->cn_timeout) == 0)
			ncp->nc_timeout = 1;
	}

	/*
	 * If the target vnode is NULL if this is to be a negative cache
	 * entry.
	 */
	if (vp == NULL) {
		ncp->nc_flag &= ~NCF_WHITEOUT;
		if (cnp->cn_flags & CNP_ISWHITEOUT)
			ncp->nc_flag |= NCF_WHITEOUT;
	}
	cache_put(ncp);
	cache_hysteresis();
}

void
cache_cleanneg(int count)
{
	struct namecache *ncp;

	/*
	 * Automode from the vnlru proc - clean out 10% of the negative cache
	 * entries.
	 */
	if (count == 0)
		count = numneg / 10 + 1;

	/*
	 * Attempt to clean out the specified number of negative cache
	 * entries.
	 */
	while (count) {
		ncp = TAILQ_FIRST(&ncneglist);
		if (ncp == NULL) {
			KKASSERT(numneg == 0);
			break;
		}
		TAILQ_REMOVE(&ncneglist, ncp, nc_vnode);
		TAILQ_INSERT_TAIL(&ncneglist, ncp, nc_vnode);
		if (cache_get_nonblock(ncp) == 0)
			cache_zap(ncp);
		--count;
	}
}

static void
cache_rehash(struct namecache *ncp)
{
	struct nchashhead *nchpp;
	u_int32_t hash;

	if (ncp->nc_flag & NCF_HASHED) {
		ncp->nc_flag &= ~NCF_HASHED;
		LIST_REMOVE(ncp, nc_hash);
	}
	hash = fnv_32_buf(ncp->nc_name, ncp->nc_nlen, FNV1_32_INIT);
	hash = fnv_32_buf(&ncp->nc_parent, sizeof(ncp->nc_parent), hash);
	nchpp = NCHHASH(hash);
	LIST_INSERT_HEAD(nchpp, ncp, nc_hash);
	ncp->nc_flag |= NCF_HASHED;
}


/*
 * Name cache initialization, from vfsinit() when we are booting
 */
void
nchinit(void)
{
	int i;
	globaldata_t gd;

	/* initialise per-cpu namecache effectiveness statistics. */
	for (i = 0; i < ncpus; ++i) {
		gd = globaldata_find(i);
		gd->gd_nchstats = &nchstats[i];
	}
	
	TAILQ_INIT(&ncneglist);
	nchashtbl = hashinit(desiredvnodes*2, M_VFSCACHE, &nchash);
}

/*
 * Called from start_init() to bootstrap the root filesystem.  Returns
 * a referenced, unlocked namecache record.
 */
struct namecache *
cache_allocroot(struct mount *mp, struct vnode *vp)
{
	struct namecache *ncp = cache_alloc(0);

	ncp->nc_flag |= NCF_MOUNTPT | NCF_ROOT;
	ncp->nc_mount = mp;
	cache_setvp(ncp, vp);
	return(ncp);
}

/*
 * vfs_cache_setroot()
 *
 *	Create an association between the root of our namecache and
 *	the root vnode.  This routine may be called several times during
 *	booting.
 *
 *	If the caller intends to save the returned namecache pointer somewhere
 *	it must cache_hold() it.
 */
void
vfs_cache_setroot(struct vnode *nvp, struct namecache *ncp)
{
	struct vnode *ovp;
	struct namecache *oncp;

	ovp = rootvnode;
	oncp = rootncp;
	rootvnode = nvp;
	rootncp = ncp;

	if (ovp)
		vrele(ovp);
	if (oncp)
		cache_drop(oncp);
}

/*
 * Invalidate all namecache entries to a particular vnode as well as 
 * any direct children of that vnode in the namecache.  This is a 
 * 'catch all' purge used by filesystems that do not know any better.
 *
 * A new vnode v_id is generated.  Note that no vnode will ever have a
 * v_id of 0.
 *
 * Note that the linkage between the vnode and its namecache entries will
 * be removed, but the namecache entries themselves might stay put due to
 * active references from elsewhere in the system or due to the existance of
 * the children.   The namecache topology is left intact even if we do not
 * know what the vnode association is.  Such entries will be marked
 * NCF_UNRESOLVED.
 *
 * XXX: Only time and the size of v_id prevents this from failing:
 * XXX: In theory we should hunt down all (struct vnode*, v_id)
 * XXX: soft references and nuke them, at least on the global
 * XXX: v_id wraparound.  The period of resistance can be extended
 * XXX: by incrementing each vnodes v_id individually instead of
 * XXX: using the global v_id.
 */
void
cache_purge(struct vnode *vp)
{
	static u_long nextid;

	cache_inval_vp(vp, CINV_PARENT | CINV_SELF | CINV_CHILDREN);

	/*
	 * Calculate a new unique id for ".." handling
	 */
	do {
		nextid++;
	} while (nextid == vp->v_id || nextid == 0);
	vp->v_id = nextid;
}

/*
 * Flush all entries referencing a particular filesystem.
 *
 * Since we need to check it anyway, we will flush all the invalid
 * entries at the same time.
 */
void
cache_purgevfs(struct mount *mp)
{
	struct nchashhead *nchpp;
	struct namecache *ncp, *nnp;

	/*
	 * Scan hash tables for applicable entries.
	 */
	for (nchpp = &nchashtbl[nchash]; nchpp >= nchashtbl; nchpp--) {
		ncp = LIST_FIRST(nchpp);
		if (ncp)
			cache_hold(ncp);
		while (ncp) {
			nnp = LIST_NEXT(ncp, nc_hash);
			if (nnp)
				cache_hold(nnp);
			if (ncp->nc_vp && ncp->nc_vp->v_mount == mp) {
				cache_lock(ncp);
				cache_zap(ncp);
			} else {
				cache_drop(ncp);
			}
			ncp = nnp;
		}
	}
}

/*
 * Perform canonical checks and cache lookup and pass on to filesystem
 * through the vop_cachedlookup only if needed.
 *
 * vop_lookup_args {
 *	struct vnode a_dvp;
 *	struct vnode **a_vpp;
 *	struct componentname *a_cnp;
 * }
 */
int
vfs_cache_lookup(struct vop_lookup_args *ap)
{
	struct vnode *dvp, *vp;
	int lockparent;
	int error;
	struct vnode **vpp = ap->a_vpp;
	struct componentname *cnp = ap->a_cnp;
	struct ucred *cred = cnp->cn_cred;
	int flags = cnp->cn_flags;
	struct thread *td = cnp->cn_td;
	u_long vpid;	/* capability number of vnode */

	*vpp = NULL;
	dvp = ap->a_dvp;
	lockparent = flags & CNP_LOCKPARENT;

	if (dvp->v_type != VDIR)
                return (ENOTDIR);

	if ((flags & CNP_ISLASTCN) && (dvp->v_mount->mnt_flag & MNT_RDONLY) &&
	    (cnp->cn_nameiop == NAMEI_DELETE || cnp->cn_nameiop == NAMEI_RENAME)) {
		return (EROFS);
	}

	error = VOP_ACCESS(dvp, VEXEC, cred, td);

	if (error)
		return (error);

	error = cache_lookup(dvp, vpp, cnp);

	/*
	 * failure if error == 0, do a physical lookup
	 */
	if (!error) 
		return (VOP_CACHEDLOOKUP(dvp, vpp, cnp));

	if (error == ENOENT)
		return (error);

	vp = *vpp;
	vpid = vp->v_id;
	cnp->cn_flags &= ~CNP_PDIRUNLOCK;
	if (dvp == vp) {   /* lookup on "." */
		/* already ref'd from cache_lookup() */
		error = 0;
	} else if (flags & CNP_ISDOTDOT) {
		VOP_UNLOCK(dvp, 0, td);
		cnp->cn_flags |= CNP_PDIRUNLOCK;
		error = vget(vp, LK_EXCLUSIVE, td);
		vrele(vp);
		if (!error && lockparent && (flags & CNP_ISLASTCN)) {
			if ((error = vn_lock(dvp, LK_EXCLUSIVE, td)) == 0)
				cnp->cn_flags &= ~CNP_PDIRUNLOCK;
		}
	} else {
		error = vget(vp, LK_EXCLUSIVE, td);
		vrele(vp);
		if (!lockparent || error || !(flags & CNP_ISLASTCN)) {
			VOP_UNLOCK(dvp, 0, td);
			cnp->cn_flags |= CNP_PDIRUNLOCK;
		}
	}
	/*
	 * Check that the capability number did not change
	 * while we were waiting for the lock.
	 */
	if (!error) {
		if (vpid == vp->v_id)
			return (0);
		vput(vp);
		if (lockparent && dvp != vp && (flags & CNP_ISLASTCN)) {
			VOP_UNLOCK(dvp, 0, td);
			cnp->cn_flags |= CNP_PDIRUNLOCK;
		}
	}
	if (cnp->cn_flags & CNP_PDIRUNLOCK) {
		error = vn_lock(dvp, LK_EXCLUSIVE, td);
		if (error)
			return (error);
		cnp->cn_flags &= ~CNP_PDIRUNLOCK;
	}
	return (VOP_CACHEDLOOKUP(dvp, vpp, cnp));
}

static int disablecwd;
SYSCTL_INT(_debug, OID_AUTO, disablecwd, CTLFLAG_RW, &disablecwd, 0, "");

static u_long numcwdcalls; STATNODE(CTLFLAG_RD, numcwdcalls, &numcwdcalls);
static u_long numcwdfail1; STATNODE(CTLFLAG_RD, numcwdfail1, &numcwdfail1);
static u_long numcwdfail2; STATNODE(CTLFLAG_RD, numcwdfail2, &numcwdfail2);
static u_long numcwdfail3; STATNODE(CTLFLAG_RD, numcwdfail3, &numcwdfail3);
static u_long numcwdfail4; STATNODE(CTLFLAG_RD, numcwdfail4, &numcwdfail4);
static u_long numcwdfound; STATNODE(CTLFLAG_RD, numcwdfound, &numcwdfound);

int
__getcwd(struct __getcwd_args *uap)
{
	int buflen;
	int error;
	char *buf;
	char *bp;

	if (disablecwd)
		return (ENODEV);

	buflen = uap->buflen;
	if (buflen < 2)
		return (EINVAL);
	if (buflen > MAXPATHLEN)
		buflen = MAXPATHLEN;

	buf = malloc(buflen, M_TEMP, M_WAITOK);
	bp = kern_getcwd(buf, buflen, &error);
	if (error == 0)
		error = copyout(bp, uap->buf, strlen(bp) + 1);
	free(buf, M_TEMP);
	return (error);
}

char *
kern_getcwd(char *buf, size_t buflen, int *error)
{
	struct proc *p = curproc;
	char *bp;
	int i, slash_prefixed;
	struct filedesc *fdp;
	struct namecache *ncp;

	numcwdcalls++;
	bp = buf;
	bp += buflen - 1;
	*bp = '\0';
	fdp = p->p_fd;
	slash_prefixed = 0;

	ncp = fdp->fd_ncdir;
	while (ncp && ncp != fdp->fd_nrdir && (ncp->nc_flag & NCF_ROOT) == 0) {
		if (ncp->nc_flag & NCF_MOUNTPT) {
			if (ncp->nc_mount == NULL) {
				*error = EBADF;		/* forced unmount? */
				return(NULL);
			}
			ncp = ncp->nc_parent;
			continue;
		}
		for (i = ncp->nc_nlen - 1; i >= 0; i--) {
			if (bp == buf) {
				numcwdfail4++;
				*error = ENOMEM;
				return(NULL);
			}
			*--bp = ncp->nc_name[i];
		}
		if (bp == buf) {
			numcwdfail4++;
			*error = ENOMEM;
			return(NULL);
		}
		*--bp = '/';
		slash_prefixed = 1;
		ncp = ncp->nc_parent;
	}
	if (ncp == NULL) {
		numcwdfail2++;
		*error = ENOENT;
		return(NULL);
	}
	if (!slash_prefixed) {
		if (bp == buf) {
			numcwdfail4++;
			*error = ENOMEM;
			return(NULL);
		}
		*--bp = '/';
	}
	numcwdfound++;
	*error = 0;
	return (bp);
}

/*
 * Thus begins the fullpath magic.
 */

#undef STATNODE
#define STATNODE(name)							\
	static u_int name;						\
	SYSCTL_UINT(_vfs_cache, OID_AUTO, name, CTLFLAG_RD, &name, 0, "")

static int disablefullpath;
SYSCTL_INT(_debug, OID_AUTO, disablefullpath, CTLFLAG_RW,
    &disablefullpath, 0, "");

STATNODE(numfullpathcalls);
STATNODE(numfullpathfail1);
STATNODE(numfullpathfail2);
STATNODE(numfullpathfail3);
STATNODE(numfullpathfail4);
STATNODE(numfullpathfound);

int
vn_fullpath(struct proc *p, struct vnode *vn, char **retbuf, char **freebuf) 
{
	char *bp, *buf;
	int i, slash_prefixed;
	struct filedesc *fdp;
	struct namecache *ncp;

	numfullpathcalls++;
	if (disablefullpath)
		return (ENODEV);

	if (p == NULL)
		return (EINVAL);

	/* vn is NULL, client wants us to use p->p_textvp */
	if (vn == NULL) {
		if ((vn = p->p_textvp) == NULL)
			return (EINVAL);
	}
	ncp = TAILQ_FIRST(&vn->v_namecache);
	if (ncp == NULL)
		return (EINVAL);

	buf = malloc(MAXPATHLEN, M_TEMP, M_WAITOK);
	bp = buf + MAXPATHLEN - 1;
	*bp = '\0';
	fdp = p->p_fd;
	slash_prefixed = 0;
	while (ncp && ncp != fdp->fd_nrdir && (ncp->nc_flag & NCF_ROOT) == 0) {
		if (ncp->nc_flag & NCF_MOUNTPT) {
			if (ncp->nc_mount == NULL) {
				free(buf, M_TEMP);
				return(EBADF);
			}
			ncp = ncp->nc_parent;
			continue;
		}
		for (i = ncp->nc_nlen - 1; i >= 0; i--) {
			if (bp == buf) {
				numfullpathfail4++;
				free(buf, M_TEMP);
				return (ENOMEM);
			}
			*--bp = ncp->nc_name[i];
		}
		if (bp == buf) {
			numfullpathfail4++;
			free(buf, M_TEMP);
			return (ENOMEM);
		}
		*--bp = '/';
		slash_prefixed = 1;
		ncp = ncp->nc_parent;
	}
	if (ncp == NULL) {
		numfullpathfail2++;
		free(buf, M_TEMP);
		return (ENOENT);
	}
	if (!slash_prefixed) {
		if (bp == buf) {
			numfullpathfail4++;
			free(buf, M_TEMP);
			return (ENOMEM);
		}
		*--bp = '/';
	}
	numfullpathfound++;
	*retbuf = bp; 
	*freebuf = buf;
	return (0);
}

