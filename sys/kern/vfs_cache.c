/*
 * Copyright (c) 1989, 1993, 1995
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 2003 Matthew Dillon <dillon@backplane.com>,
 *	All rights reserved.
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
 * $DragonFly: src/sys/kern/vfs_cache.c,v 1.12 2003/10/18 05:53:57 dillon Exp $
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
#include <sys/filedesc.h>
#include <sys/fnv_hash.h>

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

static LIST_HEAD(nchashhead, namecache) *nchashtbl;	/* Hash Table */
static struct namecache_list	ncneglist;		/* instead of vnode */
static struct namecache		rootnamecache;		/* Dummy node */

static int	nczapcheck;		/* panic on bad release */
SYSCTL_INT(_debug, OID_AUTO, nczapcheck, CTLFLAG_RW, &nczapcheck, 0, "");

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

struct	nchstats nchstats;		/* cache effectiveness statistics */

SYSCTL_INT(_debug, OID_AUTO, vnsize, CTLFLAG_RD, 0, sizeof(struct vnode), "");
SYSCTL_INT(_debug, OID_AUTO, ncsize, CTLFLAG_RD, 0, sizeof(struct namecache), "");

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


static void cache_zap(struct namecache *ncp);

MALLOC_DEFINE(M_VFSCACHE, "vfscache", "VFS name cache entries");

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

static __inline
void
_cache_drop(struct namecache *ncp)
{
	KKASSERT(ncp->nc_refs > 0);
	if (ncp->nc_refs == 1 && 
	    (ncp->nc_flag & NCF_UNRESOLVED) && 
	    TAILQ_EMPTY(&ncp->nc_list)
	) {
		cache_zap(ncp);
	} else {
		--ncp->nc_refs;
	}
}

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
 * Try to destroy a namecache entry.  The entry is disassociated from its
 * vnode or ncneglist and reverted to an UNRESOLVED state.
 *
 * Then, if there are no additional references to the ncp and we can
 * successfully delete the children, the entry is also removed from the
 * namecache hashlist / topology.
 *
 * References or undeletable children will prevent the entry from being
 * removed from the topology.  The entry may be revalidated (typically
 * by cache_enter()) at a later time.  Children remain because:
 *
 *	+ we have tried to delete a node rather then a leaf in the topology.
 *	+ the presence of negative entries (we try to scrap these).
 *	+ an entry or child has a non-zero ref count and cannot be scrapped.
 *
 * This function must be called with the ncp held and will drop the ref
 * count during zapping.
 */
static void
cache_zap(struct namecache *ncp)
{
	struct namecache *par;
	struct vnode *vp;

	/*
	 * Disassociate the vnode or negative cache ref and set NCF_UNRESOLVED.
	 */
	if ((ncp->nc_flag & NCF_UNRESOLVED) == 0) {
		ncp->nc_flag |= NCF_UNRESOLVED;
		++numunres;
		if ((vp = ncp->nc_vp) != NULL) {
			--numcache;
			ncp->nc_vp = NULL;	/* safety */
			TAILQ_REMOVE(&vp->v_namecache, ncp, nc_vnode);
			if (!TAILQ_EMPTY(&ncp->nc_list))
				vdrop(vp);
		} else {
			TAILQ_REMOVE(&ncneglist, ncp, nc_vnode);
			--numneg;
		}
	}

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
		 * Ok, we can completely destroy and free this entry.  Sanity
		 * check it against our static rootnamecache structure,
		 * then remove it from the hash.
		 */
		KKASSERT(ncp != &rootnamecache);

		if (ncp->nc_flag & NCF_HASHED) {
			ncp->nc_flag &= ~NCF_HASHED;
			LIST_REMOVE(ncp, nc_hash);
		}

		/*
		 * Unlink from its parent and free, then loop on the
		 * parent.  XXX temp hack, in stage-3 parent is never NULL
		 */
		if ((par = ncp->nc_parent) != NULL) {
			par = cache_hold(par);
			TAILQ_REMOVE(&par->nc_list, ncp, nc_entry);
			if (par->nc_vp && TAILQ_EMPTY(&par->nc_list))
				vdrop(par->nc_vp);
		}
		--numunres;
		ncp->nc_refs = -1;	/* safety */
		ncp->nc_parent = NULL;	/* safety */
		if (ncp->nc_name)
			free(ncp->nc_name, M_VFSCACHE);
		free(ncp, M_VFSCACHE);
		ncp = par;
		if (par == NULL)	/* temp hack */
			return;		/* temp hack */
	}
done:
	--ncp->nc_refs;
}

/*
 * Lookup an entry in the cache
 *
 * Lookup is called with dvp pointing to the directory to search,
 * cnp pointing to the name of the entry being sought. 
 *
 * If the lookup succeeds, the vnode is returned in *vpp, and a
 * status of -1 is returned.
 *
 * If the lookup determines that the name does not exist (negative cacheing),
 * a status of ENOENT is returned. 
 *
 * If the lookup fails, a status of zero is returned.
 *
 * Note that UNRESOLVED entries are ignored.  They are not negative cache
 * entries.
 */
int
cache_lookup(struct vnode *dvp, struct namecache *par, struct vnode **vpp,
		struct namecache **ncpp, struct componentname *cnp)
{
	struct namecache *ncp;
	u_int32_t hash;

	numcalls++;

	if (cnp->cn_nameptr[0] == '.') {
		if (cnp->cn_namelen == 1) {
			*vpp = dvp;
			dothits++;
			numposhits++;	/* include in total statistics */
			return (-1);
		}
		if (cnp->cn_namelen == 2 && cnp->cn_nameptr[1] == '.') {
			dotdothits++;
			numposhits++;	/* include in total statistics */
			if (dvp->v_dd->v_id != dvp->v_ddid ||
			    (cnp->cn_flags & CNP_MAKEENTRY) == 0) {
				dvp->v_ddid = 0;
				return (0);
			}
			*vpp = dvp->v_dd;
			return (-1);
		}
	}

	hash = fnv_32_buf(cnp->cn_nameptr, cnp->cn_namelen, FNV1_32_INIT);
	hash = fnv_32_buf(&dvp->v_id, sizeof(dvp->v_id), hash);
	LIST_FOREACH(ncp, (NCHHASH(hash)), nc_hash) {
		numchecks++;
		if ((ncp->nc_flag & NCF_UNRESOLVED) == 0 &&
		    /* ncp->nc_parent == par instead of dvp STAGE-3 */
		    ncp->nc_dvp_data == (uintptr_t)dvp && /* STAGE-2 only */
		    ncp->nc_dvp_id == dvp->v_id && /* STAGE-2 only */
		    ncp->nc_nlen == cnp->cn_namelen &&
		    bcmp(ncp->nc_name, cnp->cn_nameptr, ncp->nc_nlen) == 0
		) {
			cache_hold(ncp);
			break;
		}
	}

	/* We failed to find an entry */
	if (ncp == NULL) {
		if ((cnp->cn_flags & CNP_MAKEENTRY) == 0) {
			nummisszap++;
		} else {
			nummiss++;
		}
		nchstats.ncs_miss++;
		return (0);
	}

	/* We don't want to have an entry, so dump it */
	if ((cnp->cn_flags & CNP_MAKEENTRY) == 0) {
		numposzaps++;
		nchstats.ncs_badhits++;
		cache_zap(ncp);
		return (0);
	}

	/* We found a "positive" match, return the vnode */
	if (ncp->nc_vp) {
		numposhits++;
		nchstats.ncs_goodhits++;
		*vpp = ncp->nc_vp;
		cache_drop(ncp);
		return (-1);
	}

	/* We found a negative match, and want to create it, so purge */
	if (cnp->cn_nameiop == NAMEI_CREATE) {
		numnegzaps++;
		nchstats.ncs_badhits++;
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
	nchstats.ncs_neghits++;
	if (ncp->nc_flag & NCF_WHITEOUT)
		cnp->cn_flags |= CNP_ISWHITEOUT;
	cache_drop(ncp);
	return (ENOENT);
}

/*
 * Generate a special linkage between the mount point and the root of the 
 * mounted filesystem in order to maintain the namecache topology across
 * a mount point.  The special linkage has a 0-length name component
 * and sets NCF_MOUNTPT.
 */
void
cache_mount(struct vnode *dvp, struct vnode *tvp)
{
	struct namecache *ncp;
	struct namecache *par;
	struct nchashhead *nchpp;
	u_int32_t hash;

	/*
	 * If a linkage already exists we do not have to do anything.
	 */
	hash = fnv_32_buf("", 0, FNV1_32_INIT);
	hash = fnv_32_buf(&dvp->v_id, sizeof(dvp->v_id), hash);
	LIST_FOREACH(ncp, (NCHHASH(hash)), nc_hash) {
		numchecks++;
		if (ncp->nc_vp == tvp &&
		    ncp->nc_nlen == 0 &&
		    /* ncp->nc_parent == par STAGE-3 par instad of dvp */
		    ncp->nc_dvp_data == (uintptr_t)dvp && /* STAGE-2 only */
		    ncp->nc_dvp_id == dvp->v_id /* STAGE-2 only */
		) {
			return;
		}
	}

	/*
	 * STAGE-2 par can be NULL
	 * STAGE-3 par is passed as argument and cannot be NULL
	 */
	par = TAILQ_FIRST(&dvp->v_namecache);

	/*
	 * Otherwise create a new linkage.
	 */
	ncp = malloc(sizeof(*ncp), M_VFSCACHE, M_WAITOK | M_ZERO);

	TAILQ_INIT(&ncp->nc_list);
	ncp->nc_flag = NCF_MOUNTPT;
	if (par != NULL) {
		/* STAGE-3 par never NULL */
		ncp->nc_parent = par;
		if (TAILQ_EMPTY(&par->nc_list)) {
			if (par->nc_vp)
				vhold(par->nc_vp);
		}
		TAILQ_INSERT_HEAD(&par->nc_list, ncp, nc_entry);
	}
	ncp->nc_dvp_data = (uintptr_t)dvp; /* STAGE-2 ONLY */
	ncp->nc_dvp_id = dvp->v_id; 	/* STAGE-2 ONLY */

	/*
	 * Linkup the target vnode.  The target vnode is NULL if this is
	 * to be a negative cache entry.
	 */
	++numcache;
	ncp->nc_vp = tvp;
	TAILQ_INSERT_HEAD(&tvp->v_namecache, ncp, nc_vnode);
#if 0
	if (tvp->v_type == VDIR) {
		vp->v_dd = dvp;
		vp->v_ddid = dvp->v_id;
	}
#endif

	/*
	 * Hash table
	 */
	hash = fnv_32_buf("", 0, FNV1_32_INIT);
	hash = fnv_32_buf(&ncp->nc_dvp_id, sizeof(ncp->nc_dvp_id), hash);
	nchpp = NCHHASH(hash);
	LIST_INSERT_HEAD(nchpp, ncp, nc_hash);

	ncp->nc_flag |= NCF_HASHED;
}

/*
 * Add an entry to the cache.
 */
void
cache_enter(struct vnode *dvp, struct namecache *par, struct vnode *vp, struct componentname *cnp)
{
	struct namecache *ncp;
	struct nchashhead *nchpp;
	u_int32_t hash;

	/* YYY use par */

	/*
	 * "." and ".." are degenerate cases, they are not added to the
	 * cache.
	 */
	if (cnp->cn_nameptr[0] == '.') {
		if (cnp->cn_namelen == 1) {
			return;
		}
		if (cnp->cn_namelen == 2 && cnp->cn_nameptr[1] == '.') {
			if (vp) {
				dvp->v_dd = vp;
				dvp->v_ddid = vp->v_id;
			} else {
				dvp->v_dd = dvp;
				dvp->v_ddid = 0;
			}
			return;
		}
	}

	if (nczapcheck)
	    printf("ENTER '%*.*s' %p ", (int)cnp->cn_namelen, (int)cnp->cn_namelen, cnp->cn_nameptr, vp);

	/*
	 * Locate other entries associated with this vnode and zap them,
	 * because the purge code may not be able to find them due to
	 * the topology not yet being consistent.  This is a temporary
	 * hack.
	 */
	if (vp) {
again:
		TAILQ_FOREACH(ncp, &vp->v_namecache, nc_vnode) {
			if ((ncp->nc_flag & NCF_UNRESOLVED) == 0) {
				cache_zap(cache_hold(ncp));
				goto again;
			}
		}
	}

	hash = fnv_32_buf(cnp->cn_nameptr, cnp->cn_namelen, FNV1_32_INIT);
	hash = fnv_32_buf(&dvp->v_id, sizeof(dvp->v_id), hash);

	ncp = malloc(sizeof(*ncp), M_VFSCACHE, M_WAITOK | M_ZERO);
	ncp->nc_name = malloc(cnp->cn_namelen, M_VFSCACHE, M_WAITOK);
	TAILQ_INIT(&ncp->nc_list);
	if (nczapcheck)
	    printf("alloc\n");

	/*
	 * Linkup the parent pointer, bump the parent vnode's hold
	 * count when we go from 0->1 children.  
	 *
	 * STAGE-2 par may be NULL
	 * STAGE-3 par may not be NULL, nc_dvp_* removed
	 */
	par = TAILQ_FIRST(&dvp->v_namecache);
	if (par != NULL) {
		ncp->nc_parent = par;
		if (TAILQ_EMPTY(&par->nc_list)) {
			if (par->nc_vp)
				vhold(par->nc_vp);
		}
		TAILQ_INSERT_HEAD(&par->nc_list, ncp, nc_entry);
	}
	ncp->nc_dvp_data = (uintptr_t)dvp;
	ncp->nc_dvp_id = dvp->v_id;

	/*
	 * Add to the hash table
	 */
	ncp->nc_nlen = cnp->cn_namelen;
	bcopy(cnp->cn_nameptr, ncp->nc_name, cnp->cn_namelen);
	nchpp = NCHHASH(hash);
	LIST_INSERT_HEAD(nchpp, ncp, nc_hash);

	ncp->nc_flag |= NCF_HASHED;

	/*
	 * Linkup the target vnode.  The target vnode is NULL if this is
	 * to be a negative cache entry.
	 */
	ncp->nc_vp = vp;
	if (vp == NULL) {
		++numneg;
		TAILQ_INSERT_HEAD(&ncneglist, ncp, nc_vnode);
		ncp->nc_flag &= ~NCF_WHITEOUT;
		if (cnp->cn_flags & CNP_ISWHITEOUT)
			ncp->nc_flag |= NCF_WHITEOUT;
	} else {
		++numcache;
		if (!TAILQ_EMPTY(&ncp->nc_list))
			vhold(vp);
		TAILQ_INSERT_HEAD(&vp->v_namecache, ncp, nc_vnode);
		if (vp->v_type == VDIR) {
			vp->v_dd = dvp;
			vp->v_ddid = dvp->v_id;
		}
	}

	/*
	 * Don't cache too many negative hits
	 */
	if (numneg > MINNEG && numneg * ncnegfactor > numcache) {
		ncp = TAILQ_FIRST(&ncneglist);
		KKASSERT(ncp != NULL);
		cache_zap(cache_hold(ncp));
	}
}

/*
 * Name cache initialization, from vfs_init() when we are booting
 *
 * rootnamecache is initialized such that it cannot be recursively deleted.
 */
void
nchinit(void)
{
	TAILQ_INIT(&ncneglist);
	nchashtbl = hashinit(desiredvnodes*2, M_VFSCACHE, &nchash);
	TAILQ_INIT(&rootnamecache.nc_list);
	rootnamecache.nc_flag |= NCF_HASHED | NCF_ROOT | NCF_UNRESOLVED;
	rootnamecache.nc_refs = 1;
}

/*
 * vfs_cache_setroot()
 *
 *	Create an association between the root of our namecache and
 *	the root vnode.  This routine may be called several times during
 *	booting.
 */
void
vfs_cache_setroot(struct vnode *nvp)
{
	KKASSERT(rootnamecache.nc_refs > 0);	/* don't accidently free */
	cache_zap(cache_hold(&rootnamecache));

	rootnamecache.nc_vp = nvp;
	rootnamecache.nc_flag &= ~NCF_UNRESOLVED;
	if (nvp) {
		++numcache;
		if (!TAILQ_EMPTY(&rootnamecache.nc_list))
			vhold(nvp);
		TAILQ_INSERT_HEAD(&nvp->v_namecache, &rootnamecache, nc_vnode);
	} else {
		++numneg;
		TAILQ_INSERT_HEAD(&ncneglist, &rootnamecache, nc_vnode);
		rootnamecache.nc_flag &= ~NCF_WHITEOUT;
	}
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
	struct namecache *ncp;
	struct namecache *scan;

	/*
	 * Disassociate the vnode from its namecache entries along with
	 * (for historical reasons) any direct children.
	 */
	while ((ncp = TAILQ_FIRST(&vp->v_namecache)) != NULL) {
		cache_hold(ncp);

restart: /* YYY hack, fix me */
		TAILQ_FOREACH(scan, &ncp->nc_list, nc_entry) {
			if ((scan->nc_flag & NCF_UNRESOLVED) == 0) {
				cache_zap(cache_hold(scan));
				goto restart;
			}
		}
		cache_zap(ncp);
	}

	/*
	 * Calculate a new unique id for ".." handling
	 */
	do {
		nextid++;
	} while (nextid == vp->v_id || nextid == 0);
	vp->v_id = nextid;
	vp->v_dd = vp;
	vp->v_ddid = 0;
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
			if (ncp->nc_vp && ncp->nc_vp->v_mount == mp)
				cache_zap(ncp);
			else
				cache_drop(ncp);
			ncp = nnp;
		}
	}
}

/*
 * cache_leaf_test()
 *
 *	Test whether the vnode is at a leaf in the nameicache tree.
 *
 *	Returns 0 if it is a leaf, -1 if it isn't.
 */
int
cache_leaf_test(struct vnode *vp)
{
	struct namecache *scan;
	struct namecache *ncp;

	TAILQ_FOREACH(scan, &vp->v_namecache, nc_vnode) {
		TAILQ_FOREACH(ncp, &scan->nc_list, nc_entry) {
			/* YYY && ncp->nc_vp->v_type == VDIR ? */
			if (ncp->nc_vp != NULL)
				return(-1);
		}
	}
	return(0);
}

/*
 * Perform canonical checks and cache lookup and pass on to filesystem
 * through the vop_cachedlookup only if needed.
 *
 * vop_lookup_args {
 *	struct vnode a_dvp;
 *	struct namecache *a_ncp;
 *	struct vnode **a_vpp;
 *	struct namecache **a_ncpp;
 *	struct componentname *a_cnp;
 * }
 */
int
vfs_cache_lookup(struct vop_lookup_args *ap)
{
	struct vnode *dvp, *vp;
	int lockparent;
	int error;
	struct namecache *par = ap->a_par;
	struct vnode **vpp = ap->a_vpp;
	struct namecache **ncpp = ap->a_ncpp;
	struct componentname *cnp = ap->a_cnp;
	struct ucred *cred = cnp->cn_cred;
	int flags = cnp->cn_flags;
	struct thread *td = cnp->cn_td;
	u_long vpid;	/* capability number of vnode */

	*vpp = NULL;
	if (ncpp)
		*ncpp = NULL;
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

	error = cache_lookup(dvp, par, vpp, ncpp, cnp);

	if (!error) 
		return (VOP_CACHEDLOOKUP(dvp, par, vpp, ncpp, cnp));

	if (error == ENOENT)
		return (error);

	vp = *vpp;
	vpid = vp->v_id;
	cnp->cn_flags &= ~CNP_PDIRUNLOCK;
	if (dvp == vp) {   /* lookup on "." */
		VREF(vp);
		error = 0;
	} else if (flags & CNP_ISDOTDOT) {
		VOP_UNLOCK(dvp, 0, td);
		cnp->cn_flags |= CNP_PDIRUNLOCK;
		error = vget(vp, LK_EXCLUSIVE, td);
		if (!error && lockparent && (flags & CNP_ISLASTCN)) {
			if ((error = vn_lock(dvp, LK_EXCLUSIVE, td)) == 0)
				cnp->cn_flags &= ~CNP_PDIRUNLOCK;
		}
	} else {
		error = vget(vp, LK_EXCLUSIVE, td);
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
	return (VOP_CACHEDLOOKUP(dvp, par, vpp, ncpp, cnp));
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
	struct proc *p = curproc;
	char *bp, *buf;
	int error, i, slash_prefixed;
	struct filedesc *fdp;
	struct namecache *ncp;
	struct vnode *vp;

	numcwdcalls++;
	if (disablecwd)
		return (ENODEV);
	if (uap->buflen < 2)
		return (EINVAL);
	if (uap->buflen > MAXPATHLEN)
		uap->buflen = MAXPATHLEN;
	buf = bp = malloc(uap->buflen, M_TEMP, M_WAITOK);
	bp += uap->buflen - 1;
	*bp = '\0';
	fdp = p->p_fd;
	slash_prefixed = 0;
	for (vp = fdp->fd_cdir; vp != fdp->fd_rdir && vp != rootvnode;) {
		if (vp->v_flag & VROOT) {
			if (vp->v_mount == NULL) {	/* forced unmount */
				free(buf, M_TEMP);
				return (EBADF);
			}
			vp = vp->v_mount->mnt_vnodecovered;
			continue;
		}
		if (vp->v_dd->v_id != vp->v_ddid) {
			numcwdfail1++;
			free(buf, M_TEMP);
			return (ENOTDIR);
		}
		TAILQ_FOREACH(ncp, &vp->v_namecache, nc_vnode) {
			/* ncp->nc_parent == par STAGE-3 */
			if (ncp->nc_dvp_data == (uintptr_t)vp->v_dd &&
			    ncp->nc_dvp_id == vp->v_ddid) {
				break;
			}
		}
		if (ncp == NULL) {
			numcwdfail2++;
			free(buf, M_TEMP);
			return (ENOENT);
		}
		for (i = ncp->nc_nlen - 1; i >= 0; i--) {
			if (bp == buf) {
				numcwdfail4++;
				free(buf, M_TEMP);
				return (ENOMEM);
			}
			*--bp = ncp->nc_name[i];
		}
		if (bp == buf) {
			numcwdfail4++;
			free(buf, M_TEMP);
			return (ENOMEM);
		}
		*--bp = '/';
		slash_prefixed = 1;
		vp = vp->v_dd;
	}
	if (!slash_prefixed) {
		if (bp == buf) {
			numcwdfail4++;
			free(buf, M_TEMP);
			return (ENOMEM);
		}
		*--bp = '/';
	}
	numcwdfound++;
	error = copyout(bp, uap->buf, strlen(bp) + 1);
	free(buf, M_TEMP);
	return (error);
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
textvp_fullpath(struct proc *p, char **retbuf, char **retfreebuf) 
{
	char *bp, *buf;
	int i, slash_prefixed;
	struct filedesc *fdp;
	struct namecache *ncp;
	struct vnode *vp, *textvp;

	numfullpathcalls++;
	if (disablefullpath)
		return (ENODEV);
	textvp = p->p_textvp;
	if (textvp == NULL)
		return (EINVAL);
	buf = malloc(MAXPATHLEN, M_TEMP, M_WAITOK);
	bp = buf + MAXPATHLEN - 1;
	*bp = '\0';
	fdp = p->p_fd;
	slash_prefixed = 0;
	for (vp = textvp; vp != fdp->fd_rdir && vp != rootvnode;) {
		if (vp->v_flag & VROOT) {
			if (vp->v_mount == NULL) {	/* forced unmount */
				free(buf, M_TEMP);
				return (EBADF);
			}
			vp = vp->v_mount->mnt_vnodecovered;
			continue;
		}
		if (vp != textvp && vp->v_dd->v_id != vp->v_ddid) {
			numfullpathfail1++;
			free(buf, M_TEMP);
			return (ENOTDIR);
		}
		TAILQ_FOREACH(ncp, &vp->v_namecache, nc_vnode) {
			if (vp == textvp)
				break;
			/* ncp->nc_parent == par STAGE-3 */
			if (ncp->nc_dvp_data == (uintptr_t)vp->v_dd &&
			    ncp->nc_dvp_id == vp->v_ddid) {
				break;
			}
		}
		if (ncp == NULL) {
			numfullpathfail2++;
			free(buf, M_TEMP);
			return (ENOENT);
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
		vp = vp->v_dd;
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
	*retfreebuf = buf;
	return (0);
}

