/*
 * Copyright (c) 2003-2020 The DragonFly Project.  All rights reserved.
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
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/uio.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>
#include <sys/mount.h>
#include <sys/vnode.h>
#include <sys/malloc.h>
#include <sys/sysmsg.h>
#include <sys/spinlock.h>
#include <sys/proc.h>
#include <sys/nlookup.h>
#include <sys/filedesc.h>
#include <sys/fnv_hash.h>
#include <sys/globaldata.h>
#include <sys/kern_syscall.h>
#include <sys/dirent.h>
#include <ddb/ddb.h>

#include <sys/spinlock2.h>

#define MAX_RECURSION_DEPTH	64

/*
 * Random lookups in the cache are accomplished with a hash table using
 * a hash key of (nc_src_vp, name).  Each hash chain has its own spin lock,
 * but we use the ncp->update counter trick to avoid acquiring any
 * contestable spin-locks during a lookup.
 *
 * Negative entries may exist and correspond to resolved namecache
 * structures where nc_vp is NULL.  In a negative entry, NCF_WHITEOUT
 * will be set if the entry corresponds to a whited-out directory entry
 * (verses simply not finding the entry at all).  pcpu_ncache[n].neg_list
 * is locked via pcpu_ncache[n].neg_spin;
 *
 * MPSAFE RULES:
 *
 * (1) ncp's typically have at least a nc_refs of 1, and usually 2.  One
 *     is applicable to direct lookups via the hash table nchpp or via
 *     nc_list (the two are added or removed together).  Removal of the ncp
 *     from the hash table drops this reference.  The second is applicable
 *     to vp->v_namecache linkages (or negative list linkages), and removal
 *     of the ncp from these lists drops this reference.
 *
 *     On the 1->0 transition of nc_refs the ncp can no longer be referenced
 *     and must be destroyed.  No other thread should have access to it at
 *     this point so it can be safely locked and freed without any deadlock
 *     fears.
 *
 *     The 1->0 transition can occur at almost any juncture and so cache_drop()
 *     deals with it directly.
 *
 * (2) Once the 1->0 transition occurs, the entity that caused the transition
 *     will be responsible for destroying the ncp.  The ncp cannot be on any
 *     list or hash at this time, or be held by anyone other than the caller
 *     responsible for the transition.
 *
 * (3) A ncp must be locked in order to modify it.
 *
 * (5) ncp locks are ordered, child-to-parent.  Child first, then parent.
 *     This may seem backwards but forward-scans use the hash table and thus
 *     can hold the parent unlocked while traversing downward.  Deletions,
 *     on the other-hand, tend to propagate bottom-up since the ref on the
 *     is dropped as the children go away.
 *
 * (6) Both parent and child must be locked in order to enter the child onto
 *     the parent's nc_list.
 */

/*
 * Structures associated with name cacheing.
 */
#define NCHHASH(hash)		(&nchashtbl[(hash) & nchash])
#define MINNEG			1024
#define MINPOS			1024
#define NCMOUNT_NUMCACHE	(16384)	/* power of 2 */
#define NCMOUNT_SET		(8)	/* power of 2 */

MALLOC_DEFINE(M_VFSCACHE, "vfscache", "VFS name cache entries");

TAILQ_HEAD(nchash_list, namecache);

/*
 * Don't cachealign, but at least pad to 32 bytes so entries
 * don't cross a cache line.
 */
struct nchash_head {
       struct nchash_list list;	/* 16 bytes */
       struct spinlock	spin;	/* 8 bytes */
       long	pad01;		/* 8 bytes */
};

struct ncmount_cache {
	struct spinlock	spin;
	struct namecache *ncp;
	struct mount *mp;
	struct mount *mp_target;
	int isneg;
	int ticks;
	int updating;
	int unused01;
};

struct pcpu_ncache {
	struct spinlock		umount_spin;	/* cache_findmount/interlock */
	struct spinlock		neg_spin;	/* for neg_list and neg_count */
	struct namecache_list	neg_list;
	long			neg_count;
	long			vfscache_negs;
	long			vfscache_count;
	long			vfscache_leafs;
	long			numdefered;
} __cachealign;

__read_mostly static struct nchash_head	*nchashtbl;
__read_mostly static struct pcpu_ncache	*pcpu_ncache;
static struct ncmount_cache	ncmount_cache[NCMOUNT_NUMCACHE];

/*
 * ncvp_debug - debug cache_fromvp().  This is used by the NFS server
 * to create the namecache infrastructure leading to a dangling vnode.
 *
 * 0	Only errors are reported
 * 1	Successes are reported
 * 2	Successes + the whole directory scan is reported
 * 3	Force the directory scan code run as if the parent vnode did not
 *	have a namecache record, even if it does have one.
 */
__read_mostly static int	ncvp_debug;
SYSCTL_INT(_debug, OID_AUTO, ncvp_debug, CTLFLAG_RW, &ncvp_debug, 0,
    "Namecache debug level (0-3)");

__read_mostly static u_long nchash;		/* size of hash table */
SYSCTL_ULONG(_debug, OID_AUTO, nchash, CTLFLAG_RD, &nchash, 0,
    "Size of namecache hash table");

__read_mostly static int ncnegflush = 10;	/* burst for negative flush */
SYSCTL_INT(_debug, OID_AUTO, ncnegflush, CTLFLAG_RW, &ncnegflush, 0,
    "Batch flush negative entries");

__read_mostly static int ncposflush = 10;	/* burst for positive flush */
SYSCTL_INT(_debug, OID_AUTO, ncposflush, CTLFLAG_RW, &ncposflush, 0,
    "Batch flush positive entries");

__read_mostly static int ncnegfactor = 16;	/* ratio of negative entries */
SYSCTL_INT(_debug, OID_AUTO, ncnegfactor, CTLFLAG_RW, &ncnegfactor, 0,
    "Ratio of namecache negative entries");

__read_mostly static int nclockwarn;	/* warn on locked entries in ticks */
SYSCTL_INT(_debug, OID_AUTO, nclockwarn, CTLFLAG_RW, &nclockwarn, 0,
    "Warn on locked namecache entries in ticks");

__read_mostly static int ncposlimit;	/* number of cache entries allocated */
SYSCTL_INT(_debug, OID_AUTO, ncposlimit, CTLFLAG_RW, &ncposlimit, 0,
    "Number of cache entries allocated");

__read_mostly static int ncp_shared_lock_disable = 0;
SYSCTL_INT(_debug, OID_AUTO, ncp_shared_lock_disable, CTLFLAG_RW,
	   &ncp_shared_lock_disable, 0, "Disable shared namecache locks");

SYSCTL_INT(_debug, OID_AUTO, vnsize, CTLFLAG_RD, 0, sizeof(struct vnode),
    "sizeof(struct vnode)");
SYSCTL_INT(_debug, OID_AUTO, ncsize, CTLFLAG_RD, 0, sizeof(struct namecache),
    "sizeof(struct namecache)");

__read_mostly static int ncmount_cache_enable = 1;
SYSCTL_INT(_debug, OID_AUTO, ncmount_cache_enable, CTLFLAG_RW,
	   &ncmount_cache_enable, 0, "mount point cache");

static __inline void _cache_drop(struct namecache *ncp);
static int cache_resolve_mp(struct mount *mp);
static int cache_findmount_callback(struct mount *mp, void *data);
static void _cache_setunresolved(struct namecache *ncp);
static void _cache_cleanneg(long count);
static void _cache_cleanpos(long count);
static void _cache_cleandefered(void);
static void _cache_unlink(struct namecache *ncp);

/*
 * The new name cache statistics (these are rolled up globals and not
 * modified in the critical path, see struct pcpu_ncache).
 */
SYSCTL_NODE(_vfs, OID_AUTO, cache, CTLFLAG_RW, 0, "Name cache statistics");
static long vfscache_negs;
SYSCTL_LONG(_vfs_cache, OID_AUTO, numneg, CTLFLAG_RD, &vfscache_negs, 0,
    "Number of negative namecache entries");
static long vfscache_count;
SYSCTL_LONG(_vfs_cache, OID_AUTO, numcache, CTLFLAG_RD, &vfscache_count, 0,
    "Number of namecaches entries");
static long vfscache_leafs;
SYSCTL_LONG(_vfs_cache, OID_AUTO, numleafs, CTLFLAG_RD, &vfscache_leafs, 0,
    "Number of namecaches entries");
static long	numdefered;
SYSCTL_LONG(_debug, OID_AUTO, numdefered, CTLFLAG_RD, &numdefered, 0,
    "Number of cache entries allocated");


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
 * Cache mount points and namecache records in order to avoid unnecessary
 * atomic ops on mnt_refs and ncp->refs.  This improves concurrent SMP
 * performance and is particularly important on multi-socket systems to
 * reduce cache-line ping-ponging.
 *
 * Try to keep the pcpu structure within one cache line (~64 bytes).
 */
#define MNTCACHE_COUNT	32	/* power of 2, multiple of SET */
#define MNTCACHE_SET	8	/* set associativity */

struct mntcache_elm {
	struct namecache *ncp;
	struct mount	 *mp;
	int	ticks;
	int	unused01;
};

struct mntcache {
	struct mntcache_elm array[MNTCACHE_COUNT];
} __cachealign;

static struct mntcache	pcpu_mntcache[MAXCPU];

static __inline
struct mntcache_elm *
_cache_mntcache_hash(void *ptr)
{
	struct mntcache_elm *elm;
	int hv;

	hv = iscsi_crc32(&ptr, sizeof(ptr)) & (MNTCACHE_COUNT - 1);
	elm = &pcpu_mntcache[mycpu->gd_cpuid].array[hv & ~(MNTCACHE_SET - 1)];

	return elm;
}

static
void
_cache_mntref(struct mount *mp)
{
	struct mntcache_elm *elm;
	struct mount *mpr;
	int i;

	elm = _cache_mntcache_hash(mp);
	for (i = 0; i < MNTCACHE_SET; ++i) {
		if (elm->mp == mp) {
			mpr = atomic_swap_ptr((void *)&elm->mp, NULL);
			if (__predict_true(mpr == mp))
				return;
			if (mpr)
				atomic_add_int(&mpr->mnt_refs, -1);
		}
		++elm;
	}
	atomic_add_int(&mp->mnt_refs, 1);
}

static
void
_cache_mntrel(struct mount *mp)
{
	struct mntcache_elm *elm;
	struct mntcache_elm *best;
	struct mount *mpr;
	int delta1;
	int delta2;
	int i;

	elm = _cache_mntcache_hash(mp);
	best = elm;
	for (i = 0; i < MNTCACHE_SET; ++i) {
		if (elm->mp == NULL) {
			mpr = atomic_swap_ptr((void *)&elm->mp, mp);
			if (__predict_false(mpr != NULL)) {
				atomic_add_int(&mpr->mnt_refs, -1);
			}
			elm->ticks = ticks;
			return;
		}
		delta1 = ticks - best->ticks;
		delta2 = ticks - elm->ticks;
		if (delta2 > delta1 || delta1 < -1 || delta2 < -1)
			best = elm;
		++elm;
	}
	mpr = atomic_swap_ptr((void *)&best->mp, mp);
	best->ticks = ticks;
	if (mpr)
		atomic_add_int(&mpr->mnt_refs, -1);
}

/*
 * Clears all cached mount points on all cpus.  This routine should only
 * be called when we are waiting for a mount to clear, e.g. so we can
 * unmount.
 */
void
cache_clearmntcache(struct mount *target __unused)
{
	int n;

	for (n = 0; n < ncpus; ++n) {
		struct mntcache *cache = &pcpu_mntcache[n];
		struct mntcache_elm *elm;
		struct namecache *ncp;
		struct mount *mp;
		int i;

		for (i = 0; i < MNTCACHE_COUNT; ++i) {
			elm = &cache->array[i];
			if (elm->mp) {
				mp = atomic_swap_ptr((void *)&elm->mp, NULL);
				if (mp)
					atomic_add_int(&mp->mnt_refs, -1);
			}
			if (elm->ncp) {
				ncp = atomic_swap_ptr((void *)&elm->ncp, NULL);
				if (ncp)
					_cache_drop(ncp);
			}
		}
	}
}

/*
 * Namespace locking.  The caller must already hold a reference to the
 * namecache structure in order to lock/unlock it.  The controlling entity
 * in a 1->0 transition does not need to lock the ncp to dispose of it,
 * as nobody else will have visiblity to it at that point.
 *
 * Note that holding a locked namecache structure prevents other threads
 * from making namespace changes (e.g. deleting or creating), prevents
 * vnode association state changes by other threads, and prevents the
 * namecache entry from being resolved or unresolved by other threads.
 *
 * An exclusive lock owner has full authority to associate/disassociate
 * vnodes and resolve/unresolve the locked ncp.
 *
 * A shared lock owner only has authority to acquire the underlying vnode,
 * if any.
 *
 * The primary lock field is nc_lockstatus.  nc_locktd is set after the
 * fact (when locking) or cleared prior to unlocking.
 *
 * WARNING!  Holding a locked ncp will prevent a vnode from being destroyed
 *	     or recycled, but it does NOT help you if the vnode had already
 *	     initiated a recyclement.  If this is important, use cache_get()
 *	     rather then cache_lock() (and deal with the differences in the
 *	     way the refs counter is handled).  Or, alternatively, make an
 *	     unconditional call to cache_validate() or cache_resolve()
 *	     after cache_lock() returns.
 */
static __inline
void
_cache_lock(struct namecache *ncp)
{
	int didwarn = 0;
	int error;

	error = lockmgr(&ncp->nc_lock, LK_EXCLUSIVE);
	while (__predict_false(error == EWOULDBLOCK)) {
		if (didwarn == 0) {
			didwarn = ticks - nclockwarn;
			kprintf("[diagnostic] cache_lock: "
				"%s blocked on %p "
				"\"%*.*s\"\n",
				curthread->td_comm, ncp,
				ncp->nc_nlen, ncp->nc_nlen,
				ncp->nc_name);
		}
		error = lockmgr(&ncp->nc_lock, LK_EXCLUSIVE | LK_TIMELOCK);
	}
	if (__predict_false(didwarn)) {
		kprintf("[diagnostic] cache_lock: "
			"%s unblocked %*.*s after %d secs\n",
			curthread->td_comm,
			ncp->nc_nlen, ncp->nc_nlen, ncp->nc_name,
			(int)(ticks - didwarn) / hz);
	}
}

/*
 * Release a previously acquired lock.
 *
 * A concurrent shared-lock acquisition or acquisition/release can
 * race bit 31 so only drop the ncp if bit 31 was set.
 */
static __inline
void
_cache_unlock(struct namecache *ncp)
{
	lockmgr(&ncp->nc_lock, LK_RELEASE);
}

/*
 * Lock ncp exclusively, non-blocking.  Return 0 on success.
 */
static __inline
int
_cache_lock_nonblock(struct namecache *ncp)
{
	int error;

	error = lockmgr(&ncp->nc_lock, LK_EXCLUSIVE | LK_NOWAIT);
	if (__predict_false(error != 0)) {
		return(EWOULDBLOCK);
	}
	return 0;
}

/*
 * This is a special form of _cache_lock() which only succeeds if
 * it can get a pristine, non-recursive lock.  The caller must have
 * already ref'd the ncp.
 *
 * On success the ncp will be locked, on failure it will not.  The
 * ref count does not change either way.
 *
 * We want _cache_lock_special() (on success) to return a definitively
 * usable vnode or a definitively unresolved ncp.
 */
static __inline
int
_cache_lock_special(struct namecache *ncp)
{
	if (_cache_lock_nonblock(ncp) == 0) {
		if (lockmgr_oneexcl(&ncp->nc_lock)) {
			if (ncp->nc_vp && (ncp->nc_vp->v_flag & VRECLAIMED))
				_cache_setunresolved(ncp);
			return 0;
		}
		_cache_unlock(ncp);
	}
	return EWOULDBLOCK;
}

/*
 * Shared lock, guarantees vp held
 *
 * The shared lock holds vp on the 0->1 transition.  It is possible to race
 * another shared lock release, preventing the other release from dropping
 * the vnode and clearing bit 31.
 *
 * If it is not set then we are responsible for setting it, and this
 * responsibility does not race with anyone else.
 */
static __inline
void
_cache_lock_shared(struct namecache *ncp)
{
	int didwarn = 0;
	int error;

	error = lockmgr(&ncp->nc_lock, LK_SHARED | LK_TIMELOCK);
	while (__predict_false(error == EWOULDBLOCK)) {
		if (didwarn == 0) {
			didwarn = ticks - nclockwarn;
			kprintf("[diagnostic] cache_lock_shared: "
				"%s blocked on %p "
				"\"%*.*s\"\n",
				curthread->td_comm, ncp,
				ncp->nc_nlen, ncp->nc_nlen,
				ncp->nc_name);
		}
		error = lockmgr(&ncp->nc_lock, LK_SHARED | LK_TIMELOCK);
	}
	if (__predict_false(didwarn)) {
		kprintf("[diagnostic] cache_lock_shared: "
			"%s unblocked %*.*s after %d secs\n",
			curthread->td_comm,
			ncp->nc_nlen, ncp->nc_nlen, ncp->nc_name,
			(int)(ticks - didwarn) / hz);
	}
}

/*
 * Shared lock, guarantees vp held.  Non-blocking.  Returns 0 on success
 */
static __inline
int
_cache_lock_shared_nonblock(struct namecache *ncp)
{
	int error;

	error = lockmgr(&ncp->nc_lock, LK_SHARED | LK_NOWAIT);
	if (__predict_false(error != 0)) {
		return(EWOULDBLOCK);
	}
	return 0;
}

/*
 * This function tries to get a shared lock but will back-off to an
 * exclusive lock if:
 *
 * (1) Some other thread is trying to obtain an exclusive lock
 *     (to prevent the exclusive requester from getting livelocked out
 *     by many shared locks).
 *
 * (2) The current thread already owns an exclusive lock (to avoid
 *     deadlocking).
 *
 * WARNING! On machines with lots of cores we really want to try hard to
 *	    get a shared lock or concurrent path lookups can chain-react
 *	    into a very high-latency exclusive lock.
 *
 *	    This is very evident in dsynth's initial scans.
 */
static __inline
int
_cache_lock_shared_special(struct namecache *ncp)
{
	/*
	 * Only honor a successful shared lock (returning 0) if there is
	 * no exclusive request pending and the vnode, if present, is not
	 * in a reclaimed state.
	 */
	if (_cache_lock_shared_nonblock(ncp) == 0) {
		if (__predict_true(!lockmgr_exclpending(&ncp->nc_lock))) {
			if (ncp->nc_vp == NULL ||
			    (ncp->nc_vp->v_flag & VRECLAIMED) == 0) {
				return(0);
			}
		}
		_cache_unlock(ncp);
		return(EWOULDBLOCK);
	}

	/*
	 * Non-blocking shared lock failed.  If we already own the exclusive
	 * lock just acquire another exclusive lock (instead of deadlocking).
	 * Otherwise acquire a shared lock.
	 */
	if (lockstatus(&ncp->nc_lock, curthread) == LK_EXCLUSIVE) {
		_cache_lock(ncp);
		return(0);
	}
	_cache_lock_shared(ncp);
	return(0);
}

static __inline
int
_cache_lockstatus(struct namecache *ncp)
{
	int status;

	status = lockstatus(&ncp->nc_lock, curthread);
	if (status == 0 || status == LK_EXCLOTHER)
		status = -1;
	return status;
}

/*
 * cache_hold() and cache_drop() prevent the premature deletion of a
 * namecache entry but do not prevent operations (such as zapping) on
 * that namecache entry.
 *
 * This routine may only be called from outside this source module if
 * nc_refs is already deterministically at least 1, such as being
 * associated with e.g. a process, file descriptor, or some other entity.
 *
 * Only the above situations, similar situations within this module where
 * the ref count is deterministically at least 1, or when the ncp is found
 * via the nchpp (hash table) lookup, can bump nc_refs.
 *
 * Very specifically, a ncp found via nc_list CANNOT bump nc_refs.  It
 * can still be removed from the nc_list, however, as long as the caller
 * can acquire its lock (in the wrong order).
 *
 * This is a rare case where callers are allowed to hold a spinlock,
 * so we can't ourselves.
 */
static __inline
struct namecache *
_cache_hold(struct namecache *ncp)
{
	KKASSERT(ncp->nc_refs > 0);
	atomic_add_int(&ncp->nc_refs, 1);

	return(ncp);
}

/*
 * Drop a cache entry.
 *
 * The 1->0 transition is special and requires the caller to destroy the
 * entry.  It means that the ncp is no longer on a nchpp list (since that
 * would mean there was stilla ref).  The ncp could still be on a nc_list
 * but will not have any child of its own, again because nc_refs is now 0
 * and children would have a ref to their parent.
 *
 * Once the 1->0 transition is made, nc_refs cannot be incremented again.
 */
static __inline
void
_cache_drop(struct namecache *ncp)
{
	if (atomic_fetchadd_int(&ncp->nc_refs, -1) == 1) {
		/*
		 * Executed unlocked (no need to lock on last drop)
		 */
		_cache_setunresolved(ncp);

		/*
		 * Scrap it.
		 */
		ncp->nc_refs = -1;	/* safety */
		if (ncp->nc_name)
			kfree(ncp->nc_name, M_VFSCACHE);
		kfree(ncp, M_VFSCACHE);
	}
}

/*
 * Link a new namecache entry to its parent and to the hash table.  Be
 * careful to avoid races if vhold() blocks in the future.
 *
 * Both ncp and par must be referenced and locked.  The reference is
 * transfered to the nchpp (and, most notably, NOT to the parent list).
 *
 * NOTE: The hash table spinlock is held across this call, we can't do
 *	 anything fancy.
 */
static void
_cache_link_parent(struct namecache *ncp, struct namecache *par,
		   struct nchash_head *nchpp)
{
	struct pcpu_ncache *pn = &pcpu_ncache[mycpu->gd_cpuid];

	KKASSERT(ncp->nc_parent == NULL);
	ncp->nc_parent = par;
	ncp->nc_head = nchpp;

	/*
	 * Set inheritance flags.  Note that the parent flags may be
	 * stale due to getattr potentially not having been run yet
	 * (it gets run during nlookup()'s).
	 */
	ncp->nc_flag &= ~(NCF_SF_PNOCACHE | NCF_UF_PCACHE);
	if (par->nc_flag & (NCF_SF_NOCACHE | NCF_SF_PNOCACHE))
		ncp->nc_flag |= NCF_SF_PNOCACHE;
	if (par->nc_flag & (NCF_UF_CACHE | NCF_UF_PCACHE))
		ncp->nc_flag |= NCF_UF_PCACHE;

	/*
	 * Add to hash table and parent, adjust accounting
	 */
	TAILQ_INSERT_HEAD(&nchpp->list, ncp, nc_hash);
	atomic_add_long(&pn->vfscache_count, 1);
	if (TAILQ_EMPTY(&ncp->nc_list))
		atomic_add_long(&pn->vfscache_leafs, 1);

	if (TAILQ_EMPTY(&par->nc_list)) {
		TAILQ_INSERT_HEAD(&par->nc_list, ncp, nc_entry);
		atomic_add_long(&pn->vfscache_leafs, -1);
		/*
		 * Any vp associated with an ncp which has children must
		 * be held to prevent it from being recycled.
		 */
		if (par->nc_vp)
			vhold(par->nc_vp);
	} else {
		TAILQ_INSERT_HEAD(&par->nc_list, ncp, nc_entry);
	}
	_cache_hold(par);	/* add nc_parent ref */
}

/*
 * Remove the parent and hash associations from a namecache structure.
 * Drop the ref-count on the parent.  The caller receives the ref
 * from the ncp's nchpp linkage that was removed and may forward that
 * ref to a new linkage.

 * The caller usually holds an additional ref * on the ncp so the unlink
 * cannot be the final drop.  XXX should not be necessary now since the
 * caller receives the ref from the nchpp linkage, assuming the ncp
 * was linked in the first place.
 *
 * ncp must be locked, which means that there won't be any nc_parent
 * removal races.  This routine will acquire a temporary lock on
 * the parent as well as the appropriate hash chain.
 */
static void
_cache_unlink_parent(struct namecache *ncp)
{
	struct pcpu_ncache *pn = &pcpu_ncache[mycpu->gd_cpuid];
	struct namecache *par;
	struct vnode *dropvp;
	struct nchash_head *nchpp;

	if ((par = ncp->nc_parent) != NULL) {
		cpu_ccfence();
		KKASSERT(ncp->nc_parent == par);

		/* don't add a ref, we drop the nchpp ref later */
		_cache_lock(par);
		nchpp = ncp->nc_head;
		spin_lock(&nchpp->spin);

		/*
		 * Remove from hash table and parent, adjust accounting
		 */
		TAILQ_REMOVE(&ncp->nc_head->list, ncp, nc_hash);
		TAILQ_REMOVE(&par->nc_list, ncp, nc_entry);
		atomic_add_long(&pn->vfscache_count, -1);
		if (TAILQ_EMPTY(&ncp->nc_list))
			atomic_add_long(&pn->vfscache_leafs, -1);

		dropvp = NULL;
		if (TAILQ_EMPTY(&par->nc_list)) {
			atomic_add_long(&pn->vfscache_leafs, 1);
			if (par->nc_vp)
				dropvp = par->nc_vp;
		}
		ncp->nc_parent = NULL;
		ncp->nc_head = NULL;
		spin_unlock(&nchpp->spin);
		_cache_unlock(par);
		_cache_drop(par);	/* drop nc_parent ref */

		/*
		 * We can only safely vdrop with no spinlocks held.
		 */
		if (dropvp)
			vdrop(dropvp);
	}
}

/*
 * Allocate a new namecache structure.  Most of the code does not require
 * zero-termination of the string but it makes vop_compat_ncreate() easier.
 *
 * The returned ncp will be locked and referenced.  The ref is generally meant
 * to be transfered to the nchpp linkage.
 */
static struct namecache *
cache_alloc(int nlen)
{
	struct namecache *ncp;

	ncp = kmalloc(sizeof(*ncp), M_VFSCACHE, M_WAITOK|M_ZERO);
	if (nlen)
		ncp->nc_name = kmalloc(nlen + 1, M_VFSCACHE, M_WAITOK);
	ncp->nc_nlen = nlen;
	ncp->nc_flag = NCF_UNRESOLVED;
	ncp->nc_error = ENOTCONN;	/* needs to be resolved */
	ncp->nc_refs = 1;
	TAILQ_INIT(&ncp->nc_list);
	lockinit(&ncp->nc_lock, "ncplk", hz, LK_CANRECURSE);
	lockmgr(&ncp->nc_lock, LK_EXCLUSIVE);

	return(ncp);
}

/*
 * Can only be called for the case where the ncp has never been
 * associated with anything (so no spinlocks are needed).
 */
static void
_cache_free(struct namecache *ncp)
{
	KKASSERT(ncp->nc_refs == 1);
	if (ncp->nc_name)
		kfree(ncp->nc_name, M_VFSCACHE);
	kfree(ncp, M_VFSCACHE);
}

/*
 * [re]initialize a nchandle.
 */
void
cache_zero(struct nchandle *nch)
{
	nch->ncp = NULL;
	nch->mount = NULL;
}

/*
 * Ref and deref a nchandle structure (ncp + mp)
 *
 * The caller must specify a stable ncp pointer, typically meaning the
 * ncp is already referenced but this can also occur indirectly through
 * e.g. holding a lock on a direct child.
 *
 * WARNING: Caller may hold an unrelated read spinlock, which means we can't
 *	    use read spinlocks here.
 */
struct nchandle *
cache_hold(struct nchandle *nch)
{
	_cache_hold(nch->ncp);
	_cache_mntref(nch->mount);
	return(nch);
}

/*
 * Create a copy of a namecache handle for an already-referenced
 * entry.
 */
void
cache_copy(struct nchandle *nch, struct nchandle *target)
{
	struct namecache *ncp;
	struct mount *mp;
	struct mntcache_elm *elm;
	struct namecache *ncpr;
	int i;

	ncp = nch->ncp;
	mp = nch->mount;
	target->ncp = ncp;
	target->mount = mp;

	elm = _cache_mntcache_hash(ncp);
	for (i = 0; i < MNTCACHE_SET; ++i) {
		if (elm->ncp == ncp) {
			ncpr = atomic_swap_ptr((void *)&elm->ncp, NULL);
			if (ncpr == ncp) {
				_cache_mntref(mp);
				return;
			}
			if (ncpr)
				_cache_drop(ncpr);
		}
		++elm;
	}
	if (ncp)
		_cache_hold(ncp);
	_cache_mntref(mp);
}

/*
 * Drop the nchandle, but try to cache the ref to avoid global atomic
 * ops.  This is typically done on the system root and jail root nchandles.
 */
void
cache_drop_and_cache(struct nchandle *nch, int elmno)
{
	struct mntcache_elm *elm;
	struct mntcache_elm *best;
	struct namecache *ncpr;
	int delta1;
	int delta2;
	int i;

	if (elmno > 4) {
		if (nch->ncp) {
			_cache_drop(nch->ncp);
			nch->ncp = NULL;
		}
		if (nch->mount) {
			_cache_mntrel(nch->mount);
			nch->mount = NULL;
		}
		return;
	}

	elm = _cache_mntcache_hash(nch->ncp);
	best = elm;
	for (i = 0; i < MNTCACHE_SET; ++i) {
		if (elm->ncp == NULL) {
			ncpr = atomic_swap_ptr((void *)&elm->ncp, nch->ncp);
			_cache_mntrel(nch->mount);
			elm->ticks = ticks;
			nch->mount = NULL;
			nch->ncp = NULL;
			if (ncpr)
				_cache_drop(ncpr);
			return;
		}
		delta1 = ticks - best->ticks;
		delta2 = ticks - elm->ticks;
		if (delta2 > delta1 || delta1 < -1 || delta2 < -1)
			best = elm;
		++elm;
	}
	ncpr = atomic_swap_ptr((void *)&best->ncp, nch->ncp);
	_cache_mntrel(nch->mount);
	best->ticks = ticks;
	nch->mount = NULL;
	nch->ncp = NULL;
	if (ncpr)
		_cache_drop(ncpr);
}

void
cache_changemount(struct nchandle *nch, struct mount *mp)
{
	_cache_mntref(mp);
	_cache_mntrel(nch->mount);
	nch->mount = mp;
}

void
cache_drop(struct nchandle *nch)
{
	_cache_mntrel(nch->mount);
	_cache_drop(nch->ncp);
	nch->ncp = NULL;
	nch->mount = NULL;
}

int
cache_lockstatus(struct nchandle *nch)
{
	return(_cache_lockstatus(nch->ncp));
}

void
cache_lock(struct nchandle *nch)
{
	_cache_lock(nch->ncp);
}

void
cache_lock_maybe_shared(struct nchandle *nch, int excl)
{
	struct namecache *ncp = nch->ncp;

	if (ncp_shared_lock_disable || excl ||
	    (ncp->nc_flag & NCF_UNRESOLVED)) {
		_cache_lock(ncp);
	} else {
		_cache_lock_shared(ncp);
		if ((ncp->nc_flag & NCF_UNRESOLVED) == 0) {
			if (ncp->nc_vp && (ncp->nc_vp->v_flag & VRECLAIMED)) {
				_cache_unlock(ncp);
				_cache_lock(ncp);
			}
		} else {
			_cache_unlock(ncp);
			_cache_lock(ncp);
		}
	}
}

/*
 * Lock fncpd, fncp, tncpd, and tncp.  tncp is already locked but may
 * have to be cycled to avoid deadlocks.  Make sure all four are resolved.
 *
 * The caller is responsible for checking the validity upon return as
 * the records may have been flagged DESTROYED in the interim.
 *
 * Namecache lock ordering is leaf first, then parent.  However, complex
 * interactions may occur between the source and target because there is
 * no ordering guarantee between (fncpd, fncp) and (tncpd and tncp).
 */
void
cache_lock4_tondlocked(struct nchandle *fncpd, struct nchandle *fncp,
		       struct nchandle *tncpd, struct nchandle *tncp,
		       struct ucred *fcred, struct ucred *tcred)
{
	int tlocked = 1;

	/*
	 * Lock tncp and tncpd
	 *
	 * NOTE: Because these ncps are not locked to begin with, it is
	 *	 possible for other rename races to cause the normal lock
	 *	 order assumptions to fail.
	 *
	 * NOTE: Lock ordering assumptions are valid if a leaf's parent
	 *	 matches after the leaf has been locked.  However, ordering
	 *	 between the 'from' and the 'to' is not and an overlapping
	 *	 lock order reversal is still possible.
	 */
again:
	if (__predict_false(tlocked == 0)) {
		cache_lock(tncp);
	}
	if (__predict_false(cache_lock_nonblock(tncpd) != 0)) {
		cache_unlock(tncp);
		cache_lock(tncpd); cache_unlock(tncpd); /* cycle */
		tlocked = 0;
		goto again;
	}

	/*
	 * Lock fncp and fncpd
	 *
	 * NOTE: Because these ncps are not locked to begin with, it is
	 *	 possible for other rename races to cause the normal lock
	 *	 order assumptions to fail.
	 *
	 * NOTE: Lock ordering assumptions are valid if a leaf's parent
	 *	 matches after the leaf has been locked.  However, ordering
	 *	 between the 'from' and the 'to' is not and an overlapping
	 *	 lock order reversal is still possible.
	 */
	if (__predict_false(cache_lock_nonblock(fncp) != 0)) {
		cache_unlock(tncpd);
		cache_unlock(tncp);
		cache_lock(fncp); cache_unlock(fncp); /* cycle */
		tlocked = 0;
		goto again;
	}
	if (__predict_false(cache_lock_nonblock(fncpd) != 0)) {
		cache_unlock(fncp);
		cache_unlock(tncpd);
		cache_unlock(tncp);
		cache_lock(fncpd); cache_unlock(fncpd); /* cycle */
		tlocked = 0;
		goto again;
	}
	if (__predict_true((fncpd->ncp->nc_flag & NCF_DESTROYED) == 0))
		cache_resolve(fncpd, fcred);
	if (__predict_true((tncpd->ncp->nc_flag & NCF_DESTROYED) == 0))
		cache_resolve(tncpd, tcred);
	if (__predict_true((fncp->ncp->nc_flag & NCF_DESTROYED) == 0))
		cache_resolve(fncp, fcred);
	if (__predict_true((tncp->ncp->nc_flag & NCF_DESTROYED) == 0))
		cache_resolve(tncp, tcred);
}

int
cache_lock_nonblock(struct nchandle *nch)
{
	return(_cache_lock_nonblock(nch->ncp));
}

void
cache_unlock(struct nchandle *nch)
{
	_cache_unlock(nch->ncp);
}

/*
 * ref-and-lock, unlock-and-deref functions.
 *
 * This function is primarily used by nlookup.  Even though cache_lock
 * holds the vnode, it is possible that the vnode may have already
 * initiated a recyclement.
 *
 * We want cache_get() to return a definitively usable vnode or a
 * definitively unresolved ncp.
 */
static
struct namecache *
_cache_get(struct namecache *ncp)
{
	_cache_hold(ncp);
	_cache_lock(ncp);
	if (ncp->nc_vp && (ncp->nc_vp->v_flag & VRECLAIMED))
		_cache_setunresolved(ncp);
	return(ncp);
}

/*
 * Attempt to obtain a shared lock on the ncp.  A shared lock will only
 * be obtained if the ncp is resolved and the vnode (if not ENOENT) is
 * valid.  Otherwise an exclusive lock will be acquired instead.
 */
static
struct namecache *
_cache_get_maybe_shared(struct namecache *ncp, int excl)
{
	if (ncp_shared_lock_disable || excl ||
	    (ncp->nc_flag & NCF_UNRESOLVED)) {
		return(_cache_get(ncp));
	}
	_cache_hold(ncp);
	_cache_lock_shared(ncp);
	if ((ncp->nc_flag & NCF_UNRESOLVED) == 0) {
		if (ncp->nc_vp && (ncp->nc_vp->v_flag & VRECLAIMED)) {
			_cache_unlock(ncp);
			ncp = _cache_get(ncp);
			_cache_drop(ncp);
		}
	} else {
		_cache_unlock(ncp);
		ncp = _cache_get(ncp);
		_cache_drop(ncp);
	}
	return(ncp);
}

/*
 * NOTE: The same nchandle can be passed for both arguments.
 */
void
cache_get(struct nchandle *nch, struct nchandle *target)
{
	KKASSERT(nch->ncp->nc_refs > 0);
	target->mount = nch->mount;
	target->ncp = _cache_get(nch->ncp);
	_cache_mntref(target->mount);
}

void
cache_get_maybe_shared(struct nchandle *nch, struct nchandle *target, int excl)
{
	KKASSERT(nch->ncp->nc_refs > 0);
	target->mount = nch->mount;
	target->ncp = _cache_get_maybe_shared(nch->ncp, excl);
	_cache_mntref(target->mount);
}

/*
 * Release a held and locked ncp
 */
static __inline
void
_cache_put(struct namecache *ncp)
{
	_cache_unlock(ncp);
	_cache_drop(ncp);
}

void
cache_put(struct nchandle *nch)
{
	_cache_mntrel(nch->mount);
	_cache_put(nch->ncp);
	nch->ncp = NULL;
	nch->mount = NULL;
}

/*
 * Resolve an unresolved ncp by associating a vnode with it.  If the
 * vnode is NULL, a negative cache entry is created.
 *
 * The ncp should be locked on entry and will remain locked on return.
 */
static
void
_cache_setvp(struct mount *mp, struct namecache *ncp, struct vnode *vp)
{
	KKASSERT((ncp->nc_flag & NCF_UNRESOLVED) &&
		 (_cache_lockstatus(ncp) == LK_EXCLUSIVE) &&
		 ncp->nc_vp == NULL);

	if (vp) {
		/*
		 * Any vp associated with an ncp which has children must
		 * be held.  Any vp associated with a locked ncp must be held.
		 */
		if (!TAILQ_EMPTY(&ncp->nc_list))
			vhold(vp);
		spin_lock(&vp->v_spin);
		ncp->nc_vp = vp;
		TAILQ_INSERT_HEAD(&vp->v_namecache, ncp, nc_vnode);
		++vp->v_namecache_count;
		_cache_hold(ncp);		/* v_namecache assoc */
		spin_unlock(&vp->v_spin);
		vhold(vp);			/* nc_vp */

		/*
		 * Set auxiliary flags
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

		ncp->nc_error = 0;

		/*
		 * XXX: this is a hack to work-around the lack of a real pfs vfs
		 * implementation
		 */
		if (mp) {
			if (strncmp(mp->mnt_stat.f_fstypename, "null", 5) == 0)
				vp->v_pfsmp = mp;
		}
	} else {
		/*
		 * When creating a negative cache hit we set the
		 * namecache_gen.  A later resolve will clean out the
		 * negative cache hit if the mount point's namecache_gen
		 * has changed.  Used by devfs, could also be used by
		 * other remote FSs.
		 */
		struct pcpu_ncache *pn = &pcpu_ncache[mycpu->gd_cpuid];

		ncp->nc_vp = NULL;
		ncp->nc_negcpu = mycpu->gd_cpuid;
		spin_lock(&pn->neg_spin);
		TAILQ_INSERT_TAIL(&pn->neg_list, ncp, nc_vnode);
		_cache_hold(ncp);	/* neg_list assoc */
		++pn->neg_count;
		spin_unlock(&pn->neg_spin);
		atomic_add_long(&pn->vfscache_negs, 1);

		ncp->nc_error = ENOENT;
		if (mp)
			VFS_NCPGEN_SET(mp, ncp);
	}
	ncp->nc_flag &= ~(NCF_UNRESOLVED | NCF_DEFEREDZAP);
}

void
cache_setvp(struct nchandle *nch, struct vnode *vp)
{
	_cache_setvp(nch->mount, nch->ncp, vp);
}

/*
 * Used for NFS
 */
void
cache_settimeout(struct nchandle *nch, int nticks)
{
	struct namecache *ncp = nch->ncp;

	if ((ncp->nc_timeout = ticks + nticks) == 0)
		ncp->nc_timeout = 1;
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
 *
 */
static
void
_cache_setunresolved(struct namecache *ncp)
{
	struct vnode *vp;

	if ((ncp->nc_flag & NCF_UNRESOLVED) == 0) {
		ncp->nc_flag |= NCF_UNRESOLVED;
		ncp->nc_timeout = 0;
		ncp->nc_error = ENOTCONN;
		if ((vp = ncp->nc_vp) != NULL) {
			spin_lock(&vp->v_spin);
			ncp->nc_vp = NULL;
			TAILQ_REMOVE(&vp->v_namecache, ncp, nc_vnode);
			--vp->v_namecache_count;
			spin_unlock(&vp->v_spin);

			/*
			 * Any vp associated with an ncp with children is
			 * held by that ncp.  Any vp associated with  ncp
			 * is held by that ncp.  These conditions must be
			 * undone when the vp is cleared out from the ncp.
			 */
			if (!TAILQ_EMPTY(&ncp->nc_list))
				vdrop(vp);
			vdrop(vp);
		} else {
			struct pcpu_ncache *pn;

			pn = &pcpu_ncache[ncp->nc_negcpu];

			atomic_add_long(&pn->vfscache_negs, -1);
			spin_lock(&pn->neg_spin);
			TAILQ_REMOVE(&pn->neg_list, ncp, nc_vnode);
			--pn->neg_count;
			spin_unlock(&pn->neg_spin);
		}
		ncp->nc_flag &= ~(NCF_WHITEOUT|NCF_ISDIR|NCF_ISSYMLINK);
		_cache_drop(ncp);	/* from v_namecache or neg_list */
	}
}

/*
 * The cache_nresolve() code calls this function to automatically
 * set a resolved cache element to unresolved if it has timed out
 * or if it is a negative cache hit and the mount point namecache_gen
 * has changed.
 */
static __inline int
_cache_auto_unresolve_test(struct mount *mp, struct namecache *ncp)
{
	/*
	 * Try to zap entries that have timed out.  We have
	 * to be careful here because locked leafs may depend
	 * on the vnode remaining intact in a parent, so only
	 * do this under very specific conditions.
	 */
	if (ncp->nc_timeout && (int)(ncp->nc_timeout - ticks) < 0 &&
	    TAILQ_EMPTY(&ncp->nc_list)) {
		return 1;
	}

	/*
	 * If a resolved negative cache hit is invalid due to
	 * the mount's namecache generation being bumped, zap it.
	 */
	if (ncp->nc_vp == NULL && VFS_NCPGEN_TEST(mp, ncp)) {
		return 1;
	}

	/*
	 * Otherwise we are good
	 */
	return 0;
}

static __inline void
_cache_auto_unresolve(struct mount *mp, struct namecache *ncp)
{
	/*
	 * Already in an unresolved state, nothing to do.
	 */
	if ((ncp->nc_flag & NCF_UNRESOLVED) == 0) {
		if (_cache_auto_unresolve_test(mp, ncp))
			_cache_setunresolved(ncp);
	}
}

void
cache_setunresolved(struct nchandle *nch)
{
	_cache_setunresolved(nch->ncp);
}

/*
 * Determine if we can clear NCF_ISMOUNTPT by scanning the mountlist
 * looking for matches.  This flag tells the lookup code when it must
 * check for a mount linkage and also prevents the directories in question
 * from being deleted or renamed.
 */
static
int
cache_clrmountpt_callback(struct mount *mp, void *data)
{
	struct nchandle *nch = data;

	if (mp->mnt_ncmounton.ncp == nch->ncp)
		return(1);
	if (mp->mnt_ncmountpt.ncp == nch->ncp)
		return(1);
	return(0);
}

/*
 * Clear NCF_ISMOUNTPT on nch->ncp if it is no longer associated
 * with a mount point.
 */
void
cache_clrmountpt(struct nchandle *nch)
{
	int count;

	count = mountlist_scan(cache_clrmountpt_callback, nch,
			       MNTSCAN_FORWARD | MNTSCAN_NOBUSY |
			       MNTSCAN_NOUNLOCK);
	if (count == 0)
		nch->ncp->nc_flag &= ~NCF_ISMOUNTPT;
}

/*
 * Invalidate portions of the namecache topology given a starting entry.
 * The passed ncp is set to an unresolved state and:
 *
 * The passed ncp must be referenced and locked.  The routine may unlock
 * and relock ncp several times, and will recheck the children and loop
 * to catch races.  When done the passed ncp will be returned with the
 * reference and lock intact.
 *
 * CINV_DESTROY		- Set a flag in the passed ncp entry indicating
 *			  that the physical underlying nodes have been 
 *			  destroyed... as in deleted.  For example, when
 *			  a directory is removed.  This will cause record
 *			  lookups on the name to no longer be able to find
 *			  the record and tells the resolver to return failure
 *			  rather then trying to resolve through the parent.
 *
 *			  The topology itself, including ncp->nc_name,
 *			  remains intact.
 *
 *			  This only applies to the passed ncp, if CINV_CHILDREN
 *			  is specified the children are not flagged.
 *
 * CINV_CHILDREN	- Set all children (recursively) to an unresolved
 *			  state as well.
 *
 *			  Note that this will also have the side effect of
 *			  cleaning out any unreferenced nodes in the topology
 *			  from the leaves up as the recursion backs out.
 *
 * Note that the topology for any referenced nodes remains intact, but
 * the nodes will be marked as having been destroyed and will be set
 * to an unresolved state.
 *
 * It is possible for cache_inval() to race a cache_resolve(), meaning that
 * the namecache entry may not actually be invalidated on return if it was
 * revalidated while recursing down into its children.  This code guarentees
 * that the node(s) will go through an invalidation cycle, but does not 
 * guarentee that they will remain in an invalidated state. 
 *
 * Returns non-zero if a revalidation was detected during the invalidation
 * recursion, zero otherwise.  Note that since only the original ncp is
 * locked the revalidation ultimately can only indicate that the original ncp
 * *MIGHT* no have been reresolved.
 *
 * DEEP RECURSION HANDLING - If a recursive invalidation recurses deeply we
 * have to avoid blowing out the kernel stack.  We do this by saving the
 * deep namecache node and aborting the recursion, then re-recursing at that
 * node using a depth-first algorithm in order to allow multiple deep
 * recursions to chain through each other, then we restart the invalidation
 * from scratch.
 */

struct cinvtrack {
	struct namecache *resume_ncp;
	int depth;
};

static int _cache_inval_internal(struct namecache *, int, struct cinvtrack *);

static
int
_cache_inval(struct namecache *ncp, int flags)
{
	struct cinvtrack track;
	struct namecache *ncp2;
	int r;

	track.depth = 0;
	track.resume_ncp = NULL;

	for (;;) {
		r = _cache_inval_internal(ncp, flags, &track);
		if (track.resume_ncp == NULL)
			break;
		_cache_unlock(ncp);
		while ((ncp2 = track.resume_ncp) != NULL) {
			track.resume_ncp = NULL;
			_cache_lock(ncp2);
			_cache_inval_internal(ncp2, flags & ~CINV_DESTROY,
					     &track);
			/*_cache_put(ncp2);*/
			cache_zap(ncp2);
		}
		_cache_lock(ncp);
	}
	return(r);
}

int
cache_inval(struct nchandle *nch, int flags)
{
	return(_cache_inval(nch->ncp, flags));
}

/*
 * Helper for _cache_inval().  The passed ncp is refd and locked and
 * remains that way on return, but may be unlocked/relocked multiple
 * times by the routine.
 */
static int
_cache_inval_internal(struct namecache *ncp, int flags, struct cinvtrack *track)
{
	struct namecache *nextkid;
	int rcnt = 0;

	KKASSERT(_cache_lockstatus(ncp) == LK_EXCLUSIVE);

	_cache_setunresolved(ncp);
	if (flags & CINV_DESTROY) {
		ncp->nc_flag |= NCF_DESTROYED;
		++ncp->nc_generation;
	}

	while ((flags & CINV_CHILDREN) &&
	       (nextkid = TAILQ_FIRST(&ncp->nc_list)) != NULL
	) {
		struct namecache *kid;
		int restart;

		restart = 0;
		_cache_hold(nextkid);
		if (++track->depth > MAX_RECURSION_DEPTH) {
			track->resume_ncp = ncp;
			_cache_hold(ncp);
			++rcnt;
		}
		while ((kid = nextkid) != NULL) {
			/*
			 * Parent (ncp) must be locked for the iteration.
			 */
			nextkid = NULL;
			if (kid->nc_parent != ncp) {
				_cache_drop(kid);
				kprintf("cache_inval_internal restartA %s\n",
					ncp->nc_name);
				restart = 1;
				break;
			}
			if ((nextkid = TAILQ_NEXT(kid, nc_entry)) != NULL)
				_cache_hold(nextkid);

			/*
			 * Parent unlocked for this section to avoid
			 * deadlocks.  Then lock the kid and check for
			 * races.
			 */
			_cache_unlock(ncp);
			if (track->resume_ncp) {
				_cache_drop(kid);
				_cache_lock(ncp);
				break;
			}
			_cache_lock(kid);
			if (kid->nc_parent != ncp) {
				kprintf("cache_inval_internal "
					"restartB %s\n",
					ncp->nc_name);
				restart = 1;
				_cache_unlock(kid);
				_cache_drop(kid);
				_cache_lock(ncp);
				break;
			}
			if ((kid->nc_flag & NCF_UNRESOLVED) == 0 ||
			    TAILQ_FIRST(&kid->nc_list)
			) {

				rcnt += _cache_inval_internal(kid,
						flags & ~CINV_DESTROY, track);
				/*_cache_unlock(kid);*/
				/*_cache_drop(kid);*/
				cache_zap(kid);
			} else {
				cache_zap(kid);
			}

			/*
			 * Relock parent to continue scan
			 */
			_cache_lock(ncp);
		}
		if (nextkid)
			_cache_drop(nextkid);
		--track->depth;
		if (restart == 0)
			break;
	}

	/*
	 * Someone could have gotten in there while ncp was unlocked,
	 * retry if so.
	 */
	if ((ncp->nc_flag & NCF_UNRESOLVED) == 0)
		++rcnt;
	return (rcnt);
}

/*
 * Invalidate a vnode's namecache associations.  To avoid races against
 * the resolver we do not invalidate a node which we previously invalidated
 * but which was then re-resolved while we were in the invalidation loop.
 *
 * Returns non-zero if any namecache entries remain after the invalidation
 * loop completed.
 *
 * NOTE: Unlike the namecache topology which guarentees that ncp's will not
 *	 be ripped out of the topology while held, the vnode's v_namecache
 *	 list has no such restriction.  NCP's can be ripped out of the list
 *	 at virtually any time if not locked, even if held.
 *
 *	 In addition, the v_namecache list itself must be locked via
 *	 the vnode's spinlock.
 */
int
cache_inval_vp(struct vnode *vp, int flags)
{
	struct namecache *ncp;
	struct namecache *next;

restart:
	spin_lock(&vp->v_spin);
	ncp = TAILQ_FIRST(&vp->v_namecache);
	if (ncp)
		_cache_hold(ncp);
	while (ncp) {
		/* loop entered with ncp held and vp spin-locked */
		if ((next = TAILQ_NEXT(ncp, nc_vnode)) != NULL)
			_cache_hold(next);
		spin_unlock(&vp->v_spin);
		_cache_lock(ncp);
		if (ncp->nc_vp != vp) {
			kprintf("Warning: cache_inval_vp: race-A detected on "
				"%s\n", ncp->nc_name);
			_cache_put(ncp);
			if (next)
				_cache_drop(next);
			goto restart;
		}
		_cache_inval(ncp, flags);
		_cache_put(ncp);		/* also releases reference */
		ncp = next;
		spin_lock(&vp->v_spin);
		if (ncp && ncp->nc_vp != vp) {
			spin_unlock(&vp->v_spin);
			kprintf("Warning: cache_inval_vp: race-B detected on "
				"%s\n", ncp->nc_name);
			_cache_drop(ncp);
			goto restart;
		}
	}
	spin_unlock(&vp->v_spin);
	return(TAILQ_FIRST(&vp->v_namecache) != NULL);
}

/*
 * This routine is used instead of the normal cache_inval_vp() when we
 * are trying to recycle otherwise good vnodes.
 *
 * Return 0 on success, non-zero if not all namecache records could be
 * disassociated from the vnode (for various reasons).
 */
int
cache_inval_vp_nonblock(struct vnode *vp)
{
	struct namecache *ncp;
	struct namecache *next;

	spin_lock(&vp->v_spin);
	ncp = TAILQ_FIRST(&vp->v_namecache);
	if (ncp)
		_cache_hold(ncp);
	while (ncp) {
		/* loop entered with ncp held */
		if ((next = TAILQ_NEXT(ncp, nc_vnode)) != NULL)
			_cache_hold(next);
		spin_unlock(&vp->v_spin);
		if (_cache_lock_nonblock(ncp)) {
			_cache_drop(ncp);
			if (next)
				_cache_drop(next);
			goto done;
		}
		if (ncp->nc_vp != vp) {
			kprintf("Warning: cache_inval_vp: race-A detected on "
				"%s\n", ncp->nc_name);
			_cache_put(ncp);
			if (next)
				_cache_drop(next);
			goto done;
		}
		_cache_inval(ncp, 0);
		_cache_put(ncp);		/* also releases reference */
		ncp = next;
		spin_lock(&vp->v_spin);
		if (ncp && ncp->nc_vp != vp) {
			spin_unlock(&vp->v_spin);
			kprintf("Warning: cache_inval_vp: race-B detected on "
				"%s\n", ncp->nc_name);
			_cache_drop(ncp);
			goto done;
		}
	}
	spin_unlock(&vp->v_spin);
done:
	return(TAILQ_FIRST(&vp->v_namecache) != NULL);
}

/*
 * Clears the universal directory search 'ok' flag.  This flag allows
 * nlookup() to bypass normal vnode checks.  This flag is a cached flag
 * so clearing it simply forces revalidation.
 */
void
cache_inval_wxok(struct vnode *vp)
{
	struct namecache *ncp;

	spin_lock(&vp->v_spin);
	TAILQ_FOREACH(ncp, &vp->v_namecache, nc_vnode) {
		if (ncp->nc_flag & (NCF_WXOK | NCF_NOTX))
			atomic_clear_short(&ncp->nc_flag, NCF_WXOK | NCF_NOTX);
	}
	spin_unlock(&vp->v_spin);
}

/*
 * The source ncp has been renamed to the target ncp.  All elements have been
 * locked, including the parent ncp's.
 *
 * The target ncp is destroyed (as a normal rename-over would destroy the
 * target file or directory).
 *
 * Because there may be references to the source ncp we cannot copy its
 * contents to the target.  Instead the source ncp is relinked as the target
 * and the target ncp is removed from the namecache topology.
 */
void
cache_rename(struct nchandle *fnch, struct nchandle *tnch)
{
	struct namecache *fncp = fnch->ncp;
	struct namecache *tncp = tnch->ncp;
	struct namecache *tncp_par;
	struct nchash_head *nchpp;
	u_int32_t hash;
	char *oname;
	char *nname;

	++fncp->nc_generation;
	++tncp->nc_generation;
	if (tncp->nc_nlen) {
		nname = kmalloc(tncp->nc_nlen + 1, M_VFSCACHE, M_WAITOK);
		bcopy(tncp->nc_name, nname, tncp->nc_nlen);
		nname[tncp->nc_nlen] = 0;
	} else {
		nname = NULL;
	}

	/*
	 * Rename fncp (unlink)
	 */
	_cache_unlink_parent(fncp);
	oname = fncp->nc_name;
	fncp->nc_name = nname;
	fncp->nc_nlen = tncp->nc_nlen;
	if (oname)
		kfree(oname, M_VFSCACHE);

	tncp_par = tncp->nc_parent;
	KKASSERT(tncp_par->nc_lock.lk_lockholder == curthread);

	/*
	 * Rename fncp (relink)
	 */
	hash = fnv_32_buf(fncp->nc_name, fncp->nc_nlen, FNV1_32_INIT);
	hash = fnv_32_buf(&tncp_par, sizeof(tncp_par), hash);
	nchpp = NCHHASH(hash);

	spin_lock(&nchpp->spin);
	_cache_link_parent(fncp, tncp_par, nchpp);
	spin_unlock(&nchpp->spin);

	/*
	 * Get rid of the overwritten tncp (unlink)
	 */
	_cache_unlink(tncp);
}

/*
 * Perform actions consistent with unlinking a file.  The passed-in ncp
 * must be locked.
 *
 * The ncp is marked DESTROYED so it no longer shows up in searches,
 * and will be physically deleted when the vnode goes away.
 *
 * If the related vnode has no refs then we cycle it through vget()/vput()
 * to (possibly if we don't have a ref race) trigger a deactivation,
 * allowing the VFS to trivially detect and recycle the deleted vnode
 * via VOP_INACTIVE().
 *
 * NOTE: _cache_rename() will automatically call _cache_unlink() on the
 *	 target ncp.
 */
void
cache_unlink(struct nchandle *nch)
{
	_cache_unlink(nch->ncp);
}

static void
_cache_unlink(struct namecache *ncp)
{
	struct vnode *vp;

	/*
	 * Causes lookups to fail and allows another ncp with the same
	 * name to be created under ncp->nc_parent.
	 */
	ncp->nc_flag |= NCF_DESTROYED;
	++ncp->nc_generation;

	/*
	 * Attempt to trigger a deactivation.  Set VREF_FINALIZE to
	 * force action on the 1->0 transition.
	 */
	if ((ncp->nc_flag & NCF_UNRESOLVED) == 0 &&
	    (vp = ncp->nc_vp) != NULL) {
		atomic_set_int(&vp->v_refcnt, VREF_FINALIZE);
		if (VREFCNT(vp) <= 0) {
			if (vget(vp, LK_SHARED) == 0)
				vput(vp);
		}
	}
}

/*
 * Return non-zero if the nch might be associated with an open and/or mmap()'d
 * file.  The easy solution is to just return non-zero if the vnode has refs.
 * Used to interlock hammer2 reclaims (VREF_FINALIZE should already be set to
 * force the reclaim).
 */
int
cache_isopen(struct nchandle *nch)
{
	struct vnode *vp;
	struct namecache *ncp = nch->ncp;

	if ((ncp->nc_flag & NCF_UNRESOLVED) == 0 &&
	    (vp = ncp->nc_vp) != NULL &&
	    VREFCNT(vp)) {
		return 1;
	}
	return 0;
}


/*
 * vget the vnode associated with the namecache entry.  Resolve the namecache
 * entry if necessary.  The passed ncp must be referenced and locked.  If
 * the ncp is resolved it might be locked shared.
 *
 * lk_type may be LK_SHARED, LK_EXCLUSIVE.  A ref'd, possibly locked
 * (depending on the passed lk_type) will be returned in *vpp with an error
 * of 0, or NULL will be returned in *vpp with a non-0 error code.  The
 * most typical error is ENOENT, meaning that the ncp represents a negative
 * cache hit and there is no vnode to retrieve, but other errors can occur
 * too.
 *
 * The vget() can race a reclaim.  If this occurs we re-resolve the
 * namecache entry.
 *
 * There are numerous places in the kernel where vget() is called on a
 * vnode while one or more of its namecache entries is locked.  Releasing
 * a vnode never deadlocks against locked namecache entries (the vnode
 * will not get recycled while referenced ncp's exist).  This means we
 * can safely acquire the vnode.  In fact, we MUST NOT release the ncp
 * lock when acquiring the vp lock or we might cause a deadlock.
 *
 * NOTE: The passed-in ncp must be locked exclusively if it is initially
 *	 unresolved.  If a reclaim race occurs the passed-in ncp will be
 *	 relocked exclusively before being re-resolved.
 */
int
cache_vget(struct nchandle *nch, struct ucred *cred,
	   int lk_type, struct vnode **vpp)
{
	struct namecache *ncp;
	struct vnode *vp;
	int error;

	ncp = nch->ncp;
again:
	vp = NULL;
	if (ncp->nc_flag & NCF_UNRESOLVED)
		error = cache_resolve(nch, cred);
	else
		error = 0;

	if (error == 0 && (vp = ncp->nc_vp) != NULL) {
		error = vget(vp, lk_type);
		if (error) {
			/*
			 * VRECLAIM race
			 *
			 * The ncp may have been locked shared, we must relock
			 * it exclusively before we can set it to unresolved.
			 */
			if (error == ENOENT) {
				kprintf("Warning: vnode reclaim race detected "
					"in cache_vget on %p (%s)\n",
					vp, ncp->nc_name);
				_cache_unlock(ncp);
				_cache_lock(ncp);
				_cache_setunresolved(ncp);
				goto again;
			}

			/*
			 * Not a reclaim race, some other error.
			 */
			KKASSERT(ncp->nc_vp == vp);
			vp = NULL;
		} else {
			KKASSERT(ncp->nc_vp == vp);
			KKASSERT((vp->v_flag & VRECLAIMED) == 0);
		}
	}
	if (error == 0 && vp == NULL)
		error = ENOENT;
	*vpp = vp;
	return(error);
}

/*
 * Similar to cache_vget() but only acquires a ref on the vnode.  The vnode
 * is already held by virtuue of the ncp being locked, but it might not be
 * referenced and while it is not referenced it can transition into the
 * VRECLAIMED state.
 *
 * NOTE: The passed-in ncp must be locked exclusively if it is initially
 *	 unresolved.  If a reclaim race occurs the passed-in ncp will be
 *	 relocked exclusively before being re-resolved.
 *
 * NOTE: At the moment we have to issue a vget() on the vnode, even though
 *	 we are going to immediately release the lock, in order to resolve
 *	 potential reclamation races.  Once we have a solid vnode ref that
 *	 was (at some point) interlocked via a vget(), the vnode will not
 *	 be reclaimed.
 *
 * NOTE: vhold counts (v_auxrefs) do not prevent reclamation.
 */
int
cache_vref(struct nchandle *nch, struct ucred *cred, struct vnode **vpp)
{
	struct namecache *ncp;
	struct vnode *vp;
	int error;
	int v;

	ncp = nch->ncp;
again:
	vp = NULL;
	if (ncp->nc_flag & NCF_UNRESOLVED)
		error = cache_resolve(nch, cred);
	else
		error = 0;

	while (error == 0 && (vp = ncp->nc_vp) != NULL) {
		/*
		 * Try a lockless ref of the vnode.  VRECLAIMED transitions
		 * use the vx_lock state and update-counter mechanism so we
		 * can detect if one is in-progress or occurred.
		 *
		 * If we can successfully ref the vnode and interlock against
		 * the update-counter mechanism, and VRECLAIMED is found to
		 * not be set after that, we should be good.
		 */
		v = spin_access_start_only(&vp->v_spin);
		if (__predict_true(spin_access_check_inprog(v) == 0)) {
			vref_special(vp);
			if (__predict_false(
				    spin_access_end_only(&vp->v_spin, v))) {
				vrele(vp);
				continue;
			}
			if (__predict_true((vp->v_flag & VRECLAIMED) == 0)) {
				break;
			}
			vrele(vp);
			kprintf("CACHE_VREF: IN-RECLAIM\n");
		}

		/*
		 * Do it the slow way
		 */
		error = vget(vp, LK_SHARED);
		if (error) {
			/*
			 * VRECLAIM race
			 */
			if (error == ENOENT) {
				kprintf("Warning: vnode reclaim race detected "
					"in cache_vget on %p (%s)\n",
					vp, ncp->nc_name);
				_cache_unlock(ncp);
				_cache_lock(ncp);
				_cache_setunresolved(ncp);
				goto again;
			}

			/*
			 * Not a reclaim race, some other error.
			 */
			KKASSERT(ncp->nc_vp == vp);
			vp = NULL;
		} else {
			KKASSERT(ncp->nc_vp == vp);
			KKASSERT((vp->v_flag & VRECLAIMED) == 0);
			/* caller does not want a lock */
			vn_unlock(vp);
		}
		break;
	}
	if (error == 0 && vp == NULL)
		error = ENOENT;
	*vpp = vp;

	return(error);
}

/*
 * Return a referenced vnode representing the parent directory of
 * ncp.
 *
 * Because the caller has locked the ncp it should not be possible for
 * the parent ncp to go away.  However, the parent can unresolve its
 * dvp at any time so we must be able to acquire a lock on the parent
 * to safely access nc_vp.
 *
 * We have to leave par unlocked when vget()ing dvp to avoid a deadlock,
 * so use vhold()/vdrop() while holding the lock to prevent dvp from
 * getting destroyed.
 *
 * NOTE: vhold() is allowed when dvp has 0 refs if we hold a
 *	 lock on the ncp in question..
 */
struct vnode *
cache_dvpref(struct namecache *ncp)
{
	struct namecache *par;
	struct vnode *dvp;

	dvp = NULL;
	if ((par = ncp->nc_parent) != NULL) {
		_cache_hold(par);
		_cache_lock(par);
		if ((par->nc_flag & NCF_UNRESOLVED) == 0) {
			if ((dvp = par->nc_vp) != NULL)
				vhold(dvp);
		}
		_cache_unlock(par);
		if (dvp) {
			if (vget(dvp, LK_SHARED) == 0) {
				vn_unlock(dvp);
				vdrop(dvp);
				/* return refd, unlocked dvp */
			} else {
				vdrop(dvp);
				dvp = NULL;
			}
		}
		_cache_drop(par);
	}
	return(dvp);
}

/*
 * Convert a directory vnode to a namecache record without any other 
 * knowledge of the topology.  This ONLY works with directory vnodes and
 * is ONLY used by the NFS server.  dvp must be refd but unlocked, and the
 * returned ncp (if not NULL) will be held and unlocked.
 *
 * If 'makeit' is 0 and dvp has no existing namecache record, NULL is returned.
 * If 'makeit' is 1 we attempt to track-down and create the namecache topology
 * for dvp.  This will fail only if the directory has been deleted out from
 * under the caller.  
 *
 * Callers must always check for a NULL return no matter the value of 'makeit'.
 *
 * To avoid underflowing the kernel stack each recursive call increments
 * the makeit variable.
 */

static int cache_inefficient_scan(struct nchandle *nch, struct ucred *cred,
				  struct vnode *dvp, char *fakename);
static int cache_fromdvp_try(struct vnode *dvp, struct ucred *cred, 
				  struct vnode **saved_dvp);

int
cache_fromdvp(struct vnode *dvp, struct ucred *cred, int makeit,
	      struct nchandle *nch)
{
	struct vnode *saved_dvp;
	struct vnode *pvp;
	char *fakename;
	int error;

	nch->ncp = NULL;
	nch->mount = dvp->v_mount;
	saved_dvp = NULL;
	fakename = NULL;

	/*
	 * Handle the makeit == 0 degenerate case
	 */
	if (makeit == 0) {
		spin_lock_shared(&dvp->v_spin);
		nch->ncp = TAILQ_FIRST(&dvp->v_namecache);
		if (nch->ncp)
			cache_hold(nch);
		spin_unlock_shared(&dvp->v_spin);
	}

	/*
	 * Loop until resolution, inside code will break out on error.
	 */
	while (makeit) {
		/*
		 * Break out if we successfully acquire a working ncp.
		 */
		spin_lock_shared(&dvp->v_spin);
		nch->ncp = TAILQ_FIRST(&dvp->v_namecache);
		if (nch->ncp) {
			cache_hold(nch);
			spin_unlock_shared(&dvp->v_spin);
			break;
		}
		spin_unlock_shared(&dvp->v_spin);

		/*
		 * If dvp is the root of its filesystem it should already
		 * have a namecache pointer associated with it as a side 
		 * effect of the mount, but it may have been disassociated.
		 */
		if (dvp->v_flag & VROOT) {
			nch->ncp = _cache_get(nch->mount->mnt_ncmountpt.ncp);
			error = cache_resolve_mp(nch->mount);
			_cache_put(nch->ncp);
			if (ncvp_debug) {
				kprintf("cache_fromdvp: resolve root of mount %p error %d", 
					dvp->v_mount, error);
			}
			if (error) {
				if (ncvp_debug)
					kprintf(" failed\n");
				nch->ncp = NULL;
				break;
			}
			if (ncvp_debug)
				kprintf(" succeeded\n");
			continue;
		}

		/*
		 * If we are recursed too deeply resort to an O(n^2)
		 * algorithm to resolve the namecache topology.  The
		 * resolved pvp is left referenced in saved_dvp to
		 * prevent the tree from being destroyed while we loop.
		 */
		if (makeit > 20) {
			error = cache_fromdvp_try(dvp, cred, &saved_dvp);
			if (error) {
				kprintf("lookupdotdot(longpath) failed %d "
				       "dvp %p\n", error, dvp);
				nch->ncp = NULL;
				break;
			}
			continue;
		}

		/*
		 * Get the parent directory and resolve its ncp.
		 */
		if (fakename) {
			kfree(fakename, M_TEMP);
			fakename = NULL;
		}
		error = vop_nlookupdotdot(*dvp->v_ops, dvp, &pvp, cred,
					  &fakename);
		if (error) {
			kprintf("lookupdotdot failed %d dvp %p\n", error, dvp);
			break;
		}
		vn_unlock(pvp);

		/*
		 * Reuse makeit as a recursion depth counter.  On success
		 * nch will be fully referenced.
		 */
		cache_fromdvp(pvp, cred, makeit + 1, nch);
		vrele(pvp);
		if (nch->ncp == NULL)
			break;

		/*
		 * Do an inefficient scan of pvp (embodied by ncp) to look
		 * for dvp.  This will create a namecache record for dvp on
		 * success.  We loop up to recheck on success.
		 *
		 * ncp and dvp are both held but not locked.
		 */
		error = cache_inefficient_scan(nch, cred, dvp, fakename);
		if (error) {
			kprintf("cache_fromdvp: scan %p (%s) failed on dvp=%p\n",
				pvp, nch->ncp->nc_name, dvp);
			cache_drop(nch);
			/* nch was NULLed out, reload mount */
			nch->mount = dvp->v_mount;
			break;
		}
		if (ncvp_debug) {
			kprintf("cache_fromdvp: scan %p (%s) succeeded\n",
				pvp, nch->ncp->nc_name);
		}
		cache_drop(nch);
		/* nch was NULLed out, reload mount */
		nch->mount = dvp->v_mount;
	}

	/*
	 * If nch->ncp is non-NULL it will have been held already.
	 */
	if (fakename)
		kfree(fakename, M_TEMP);
	if (saved_dvp)
		vrele(saved_dvp);
	if (nch->ncp)
		return (0);
	return (EINVAL);
}

/*
 * Go up the chain of parent directories until we find something
 * we can resolve into the namecache.  This is very inefficient.
 */
static
int
cache_fromdvp_try(struct vnode *dvp, struct ucred *cred,
		  struct vnode **saved_dvp)
{
	struct nchandle nch;
	struct vnode *pvp;
	int error;
	static time_t last_fromdvp_report;
	char *fakename;

	/*
	 * Loop getting the parent directory vnode until we get something we
	 * can resolve in the namecache.
	 */
	vref(dvp);
	nch.mount = dvp->v_mount;
	nch.ncp = NULL;
	fakename = NULL;

	for (;;) {
		if (fakename) {
			kfree(fakename, M_TEMP);
			fakename = NULL;
		}
		error = vop_nlookupdotdot(*dvp->v_ops, dvp, &pvp, cred,
					  &fakename);
		if (error) {
			vrele(dvp);
			break;
		}
		vn_unlock(pvp);
		spin_lock_shared(&pvp->v_spin);
		if ((nch.ncp = TAILQ_FIRST(&pvp->v_namecache)) != NULL) {
			_cache_hold(nch.ncp);
			spin_unlock_shared(&pvp->v_spin);
			vrele(pvp);
			break;
		}
		spin_unlock_shared(&pvp->v_spin);
		if (pvp->v_flag & VROOT) {
			nch.ncp = _cache_get(pvp->v_mount->mnt_ncmountpt.ncp);
			error = cache_resolve_mp(nch.mount);
			_cache_unlock(nch.ncp);
			vrele(pvp);
			if (error) {
				_cache_drop(nch.ncp);
				nch.ncp = NULL;
				vrele(dvp);
			}
			break;
		}
		vrele(dvp);
		dvp = pvp;
	}
	if (error == 0) {
		if (last_fromdvp_report != time_uptime) {
			last_fromdvp_report = time_uptime;
			kprintf("Warning: extremely inefficient path "
				"resolution on %s\n",
				nch.ncp->nc_name);
		}
		error = cache_inefficient_scan(&nch, cred, dvp, fakename);

		/*
		 * Hopefully dvp now has a namecache record associated with
		 * it.  Leave it referenced to prevent the kernel from
		 * recycling the vnode.  Otherwise extremely long directory
		 * paths could result in endless recycling.
		 */
		if (*saved_dvp)
		    vrele(*saved_dvp);
		*saved_dvp = dvp;
		_cache_drop(nch.ncp);
	}
	if (fakename)
		kfree(fakename, M_TEMP);
	return (error);
}

/*
 * Do an inefficient scan of the directory represented by ncp looking for
 * the directory vnode dvp.  ncp must be held but not locked on entry and
 * will be held on return.  dvp must be refd but not locked on entry and
 * will remain refd on return.
 *
 * Why do this at all?  Well, due to its stateless nature the NFS server
 * converts file handles directly to vnodes without necessarily going through
 * the namecache ops that would otherwise create the namecache topology
 * leading to the vnode.  We could either (1) Change the namecache algorithms
 * to allow disconnect namecache records that are re-merged opportunistically,
 * or (2) Make the NFS server backtrack and scan to recover a connected
 * namecache topology in order to then be able to issue new API lookups.
 *
 * It turns out that (1) is a huge mess.  It takes a nice clean set of 
 * namecache algorithms and introduces a lot of complication in every subsystem
 * that calls into the namecache to deal with the re-merge case, especially
 * since we are using the namecache to placehold negative lookups and the
 * vnode might not be immediately assigned. (2) is certainly far less
 * efficient then (1), but since we are only talking about directories here
 * (which are likely to remain cached), the case does not actually run all
 * that often and has the supreme advantage of not polluting the namecache
 * algorithms.
 *
 * If a fakename is supplied just construct a namecache entry using the
 * fake name.
 */
static int
cache_inefficient_scan(struct nchandle *nch, struct ucred *cred, 
		       struct vnode *dvp, char *fakename)
{
	struct nlcomponent nlc;
	struct nchandle rncp;
	struct dirent *den;
	struct vnode *pvp;
	struct vattr vat;
	struct iovec iov;
	struct uio uio;
	int blksize;
	int eofflag;
	int bytes;
	char *rbuf;
	int error;

	vat.va_blocksize = 0;
	if ((error = VOP_GETATTR(dvp, &vat)) != 0)
		return (error);
	cache_lock(nch);
	error = cache_vref(nch, cred, &pvp);
	cache_unlock(nch);
	if (error)
		return (error);
	if (ncvp_debug) {
		kprintf("inefficient_scan of (%p,%s): directory iosize %ld "
			"vattr fileid = %lld\n",
			nch->ncp, nch->ncp->nc_name,
			vat.va_blocksize,
			(long long)vat.va_fileid);
	}

	/*
	 * Use the supplied fakename if not NULL.  Fake names are typically
	 * not in the actual filesystem hierarchy.  This is used by HAMMER
	 * to glue @@timestamp recursions together.
	 */
	if (fakename) {
		nlc.nlc_nameptr = fakename;
		nlc.nlc_namelen = strlen(fakename);
		rncp = cache_nlookup(nch, &nlc);
		goto done;
	}

	if ((blksize = vat.va_blocksize) == 0)
		blksize = DEV_BSIZE;
	rbuf = kmalloc(blksize, M_TEMP, M_WAITOK);
	rncp.ncp = NULL;

	eofflag = 0;
	uio.uio_offset = 0;
again:
	iov.iov_base = rbuf;
	iov.iov_len = blksize;
	uio.uio_iov = &iov;
	uio.uio_iovcnt = 1;
	uio.uio_resid = blksize;
	uio.uio_segflg = UIO_SYSSPACE;
	uio.uio_rw = UIO_READ;
	uio.uio_td = curthread;

	if (ncvp_debug >= 2)
		kprintf("cache_inefficient_scan: readdir @ %08x\n", (int)uio.uio_offset);
	error = VOP_READDIR(pvp, &uio, cred, &eofflag, NULL, NULL);
	if (error == 0) {
		den = (struct dirent *)rbuf;
		bytes = blksize - uio.uio_resid;

		while (bytes > 0) {
			if (ncvp_debug >= 2) {
				kprintf("cache_inefficient_scan: %*.*s\n",
					den->d_namlen, den->d_namlen, 
					den->d_name);
			}
			if (den->d_type != DT_WHT &&
			    den->d_ino == vat.va_fileid) {
				if (ncvp_debug) {
					kprintf("cache_inefficient_scan: "
					       "MATCHED inode %lld path %s/%*.*s\n",
					       (long long)vat.va_fileid,
					       nch->ncp->nc_name,
					       den->d_namlen, den->d_namlen,
					       den->d_name);
				}
				nlc.nlc_nameptr = den->d_name;
				nlc.nlc_namelen = den->d_namlen;
				rncp = cache_nlookup(nch, &nlc);
				KKASSERT(rncp.ncp != NULL);
				break;
			}
			bytes -= _DIRENT_DIRSIZ(den);
			den = _DIRENT_NEXT(den);
		}
		if (rncp.ncp == NULL && eofflag == 0 && uio.uio_resid != blksize)
			goto again;
	}
	kfree(rbuf, M_TEMP);
done:
	vrele(pvp);
	if (rncp.ncp) {
		if (rncp.ncp->nc_flag & NCF_UNRESOLVED) {
			_cache_setvp(rncp.mount, rncp.ncp, dvp);
			if (ncvp_debug >= 2) {
				kprintf("cache_inefficient_scan: setvp %s/%s = %p\n",
					nch->ncp->nc_name, rncp.ncp->nc_name, dvp);
			}
		} else {
			if (ncvp_debug >= 2) {
				kprintf("cache_inefficient_scan: setvp %s/%s already set %p/%p\n", 
					nch->ncp->nc_name, rncp.ncp->nc_name, dvp,
					rncp.ncp->nc_vp);
			}
		}
		if (rncp.ncp->nc_vp == NULL)
			error = rncp.ncp->nc_error;
		/* 
		 * Release rncp after a successful nlookup.  rncp was fully
		 * referenced.
		 */
		cache_put(&rncp);
	} else {
		kprintf("cache_inefficient_scan: dvp %p NOT FOUND in %s\n",
			dvp, nch->ncp->nc_name);
		error = ENOENT;
	}
	return (error);
}

/*
 * This function must be called with the ncp held and locked and will unlock
 * and drop it during zapping.
 *
 * Zap a namecache entry.  The ncp is unconditionally set to an unresolved
 * state, which disassociates it from its vnode or pcpu_ncache[n].neg_list
 * and removes the related reference.  If the ncp can be removed, and the
 * parent can be zapped non-blocking, this function loops up.
 *
 * There will be one ref from the caller (which we now own).  The only
 * remaining autonomous refs to the ncp will then be due to nc_parent->nc_list,
 * so possibly 2 refs left.  Taking this into account, if there are no
 * additional refs and no children, the ncp will be removed from the topology
 * and destroyed.
 *
 * References and/or children may exist if the ncp is in the middle of the
 * topology, preventing the ncp from being destroyed.
 *
 * If nonblock is non-zero and the parent ncp cannot be locked we give up.
 *
 * This function may return a held (but NOT locked) parent node which the
 * caller must drop in a loop.  Looping is one way to avoid unbounded recursion
 * due to deep namecache trees.
 *
 * WARNING!  For MPSAFE operation this routine must acquire up to three
 *	     spin locks to be able to safely test nc_refs.  Lock order is
 *	     very important.
 *
 *	     hash spinlock if on hash list
 *	     parent spinlock if child of parent
 *	     (the ncp is unresolved so there is no vnode association)
 */
static void
cache_zap(struct namecache *ncp)
{
	struct namecache *par;
	struct vnode *dropvp;
	struct nchash_head *nchpp;
	int refcmp;
	int nonblock = 1;	/* XXX cleanup */

again:
	/*
	 * Disassociate the vnode or negative cache ref and set NCF_UNRESOLVED.
	 * This gets rid of any vp->v_namecache list or negative list and
	 * the related ref.
	 */
	_cache_setunresolved(ncp);

	/*
	 * Try to scrap the entry and possibly tail-recurse on its parent.
	 * We only scrap unref'd (other then our ref) unresolved entries,
	 * we do not scrap 'live' entries.
	 *
	 * If nc_parent is non NULL we expect 2 references, else just 1.
	 * If there are more, someone else also holds the ncp and we cannot
	 * destroy it.
	 */
	KKASSERT(ncp->nc_flag & NCF_UNRESOLVED);
	KKASSERT(ncp->nc_refs > 0);

	/*
	 * If the ncp is linked to its parent it will also be in the hash
	 * table.  We have to be able to lock the parent and the hash table.
	 *
	 * Acquire locks.  Note that the parent can't go away while we hold
	 * a child locked.  If nc_parent is present, expect 2 refs instead
	 * of 1.
	 */
	nchpp = NULL;
	if ((par = ncp->nc_parent) != NULL) {
		if (nonblock) {
			if (_cache_lock_nonblock(par)) {
				/* lock failed */
				ncp->nc_flag |= NCF_DEFEREDZAP;
				atomic_add_long(
				    &pcpu_ncache[mycpu->gd_cpuid].numdefered,
				    1);
				_cache_unlock(ncp);
				_cache_drop(ncp);	/* caller's ref */
				return;
			}
			_cache_hold(par);
		} else {
			_cache_hold(par);
			_cache_lock(par);
		}
		nchpp = ncp->nc_head;
		spin_lock(&nchpp->spin);
	}

	/*
	 * With the parent and nchpp locked, and the vnode removed
	 * (no vp->v_namecache), we expect 1 or 2 refs.  If there are
	 * more someone else has a ref and we cannot zap the entry.
	 *
	 * one for our hold
	 * one for our parent link (parent also has one from the linkage)
	 */
	if (par)
		refcmp = 2;
	else
		refcmp = 1;

	/*
	 * On failure undo the work we've done so far and drop the
	 * caller's ref and ncp.
	 */
	if (ncp->nc_refs != refcmp || TAILQ_FIRST(&ncp->nc_list)) {
		if (par) {
			spin_unlock(&nchpp->spin);
			_cache_put(par);
		}
		_cache_unlock(ncp);
		_cache_drop(ncp);
		return;
	}

	/*
	 * We own all the refs and with the spinlocks held no further
	 * refs can be acquired by others.
	 *
	 * Remove us from the hash list and parent list.  We have to
	 * drop a ref on the parent's vp if the parent's list becomes
	 * empty.
	 */
	dropvp = NULL;
	if (par) {
		struct pcpu_ncache *pn = &pcpu_ncache[mycpu->gd_cpuid];

		KKASSERT(nchpp == ncp->nc_head);
		TAILQ_REMOVE(&ncp->nc_head->list, ncp, nc_hash);
		TAILQ_REMOVE(&par->nc_list, ncp, nc_entry);
		atomic_add_long(&pn->vfscache_count, -1);
		if (TAILQ_EMPTY(&ncp->nc_list))
			atomic_add_long(&pn->vfscache_leafs, -1);

		if (TAILQ_EMPTY(&par->nc_list)) {
			atomic_add_long(&pn->vfscache_leafs, 1);
			if (par->nc_vp)
				dropvp = par->nc_vp;
		}
		ncp->nc_parent = NULL;
		ncp->nc_head = NULL;
		spin_unlock(&nchpp->spin);
		_cache_drop(par);	/* removal of ncp from par->nc_list */
		/*_cache_unlock(par);*/
	} else {
		KKASSERT(ncp->nc_head == NULL);
	}

	/*
	 * ncp should not have picked up any refs.  Physically
	 * destroy the ncp.
	 */
	if (ncp->nc_refs != refcmp) {
		panic("cache_zap: %p bad refs %d (expected %d)\n",
			ncp, ncp->nc_refs, refcmp);
	}
	/* _cache_unlock(ncp) not required */
	ncp->nc_refs = -1;	/* safety */
	if (ncp->nc_name)
		kfree(ncp->nc_name, M_VFSCACHE);
	kfree(ncp, M_VFSCACHE);

	/*
	 * Delayed drop (we had to release our spinlocks)
	 */
	if (dropvp)
		vdrop(dropvp);

	/*
	 * Loop up if we can recursively clean out the parent.
	 */
	if (par) {
		refcmp = 1;		/* ref on parent */
		if (par->nc_parent)	/* par->par */
			++refcmp;
		par->nc_flag &= ~NCF_DEFEREDZAP;
		if ((par->nc_flag & NCF_UNRESOLVED) &&
		    par->nc_refs == refcmp &&
		    TAILQ_EMPTY(&par->nc_list)) {
			ncp = par;
			goto again;
		}
		_cache_unlock(par);
		_cache_drop(par);
	}
}

/*
 * Clean up dangling negative cache and defered-drop entries in the
 * namecache.
 *
 * This routine is called in the critical path and also called from
 * vnlru().  When called from vnlru we use a lower limit to try to
 * deal with the negative cache before the critical path has to start
 * dealing with it.
 */
typedef enum { CHI_LOW, CHI_HIGH } cache_hs_t;

static cache_hs_t neg_cache_hysteresis_state[2] = { CHI_LOW, CHI_LOW };
static cache_hs_t pos_cache_hysteresis_state[2] = { CHI_LOW, CHI_LOW };

void
cache_hysteresis(int critpath)
{
	long poslimit;
	long neglimit = maxvnodes / ncnegfactor;
	long xnumcache = vfscache_leafs;

	if (critpath == 0)
		neglimit = neglimit * 8 / 10;

	/*
	 * Don't cache too many negative hits.  We use hysteresis to reduce
	 * the impact on the critical path.
	 */
	switch(neg_cache_hysteresis_state[critpath]) {
	case CHI_LOW:
		if (vfscache_negs > MINNEG && vfscache_negs > neglimit) {
			if (critpath)
				_cache_cleanneg(ncnegflush);
			else
				_cache_cleanneg(ncnegflush +
						vfscache_negs - neglimit);
			neg_cache_hysteresis_state[critpath] = CHI_HIGH;
		}
		break;
	case CHI_HIGH:
		if (vfscache_negs > MINNEG * 9 / 10 &&
		    vfscache_negs * 9 / 10 > neglimit
		) {
			if (critpath)
				_cache_cleanneg(ncnegflush);
			else
				_cache_cleanneg(ncnegflush +
						vfscache_negs * 9 / 10 -
						neglimit);
		} else {
			neg_cache_hysteresis_state[critpath] = CHI_LOW;
		}
		break;
	}

	/*
	 * Don't cache too many positive hits.  We use hysteresis to reduce
	 * the impact on the critical path.
	 *
	 * Excessive positive hits can accumulate due to large numbers of
	 * hardlinks (the vnode cache will not prevent hl ncps from growing
	 * into infinity).
	 */
	if ((poslimit = ncposlimit) == 0)
		poslimit = maxvnodes * 2;
	if (critpath == 0)
		poslimit = poslimit * 8 / 10;

	switch(pos_cache_hysteresis_state[critpath]) {
	case CHI_LOW:
		if (xnumcache > poslimit && xnumcache > MINPOS) {
			if (critpath)
				_cache_cleanpos(ncposflush);
			else
				_cache_cleanpos(ncposflush +
						xnumcache - poslimit);
			pos_cache_hysteresis_state[critpath] = CHI_HIGH;
		}
		break;
	case CHI_HIGH:
		if (xnumcache > poslimit * 5 / 6 && xnumcache > MINPOS) {
			if (critpath)
				_cache_cleanpos(ncposflush);
			else
				_cache_cleanpos(ncposflush +
						xnumcache - poslimit * 5 / 6);
		} else {
			pos_cache_hysteresis_state[critpath] = CHI_LOW;
		}
		break;
	}

	/*
	 * Clean out dangling defered-zap ncps which could not be cleanly
	 * dropped if too many build up.  Note that numdefered is
	 * heuristical.  Make sure we are real-time for the current cpu,
	 * plus the global rollup.
	 */
	if (pcpu_ncache[mycpu->gd_cpuid].numdefered + numdefered > neglimit) {
		_cache_cleandefered();
	}
}

/*
 * NEW NAMECACHE LOOKUP API
 *
 * Lookup an entry in the namecache.  The passed par_nch must be referenced
 * and unlocked.  A referenced and locked nchandle with a non-NULL nch.ncp
 * is ALWAYS returned, eve if the supplied component is illegal.
 *
 * The resulting namecache entry should be returned to the system with
 * cache_put() or cache_unlock() + cache_drop().
 *
 * namecache locks are recursive but care must be taken to avoid lock order
 * reversals (hence why the passed par_nch must be unlocked).  Locking
 * rules are to order for parent traversals, not for child traversals.
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
 * the filesystem lookup requires a resolved directory vnode the caller is
 * responsible for resolving the namecache chain top-down.  This API 
 * specifically allows whole chains to be created in an unresolved state.
 */
struct nchandle
cache_nlookup(struct nchandle *par_nch, struct nlcomponent *nlc)
{
	struct nchandle nch;
	struct namecache *ncp;
	struct namecache *new_ncp;
	struct namecache *rep_ncp;	/* reuse a destroyed ncp */
	struct nchash_head *nchpp;
	struct mount *mp;
	u_int32_t hash;
	globaldata_t gd;
	int par_locked;
	int use_excl;

	gd = mycpu;
	mp = par_nch->mount;
	par_locked = 0;

	/*
	 * This is a good time to call it, no ncp's are locked by
	 * the caller or us.
	 */
	cache_hysteresis(1);

	/*
	 * Try to locate an existing entry
	 */
	hash = fnv_32_buf(nlc->nlc_nameptr, nlc->nlc_namelen, FNV1_32_INIT);
	hash = fnv_32_buf(&par_nch->ncp, sizeof(par_nch->ncp), hash);
	new_ncp = NULL;
	use_excl = 0;
	nchpp = NCHHASH(hash);
restart:
	rep_ncp = NULL;
	if (use_excl)
		spin_lock(&nchpp->spin);
	else
		spin_lock_shared(&nchpp->spin);

	TAILQ_FOREACH(ncp, &nchpp->list, nc_hash) {
		/*
		 * Break out if we find a matching entry.  Note that
		 * UNRESOLVED entries may match, but DESTROYED entries
		 * do not.
		 *
		 * We may be able to reuse DESTROYED entries that we come
		 * across, even if the name does not match, as long as
		 * nc_nlen is correct and the only hold ref is from the nchpp
		 * list itself.
		 */
		if (ncp->nc_parent == par_nch->ncp &&
		    ncp->nc_nlen == nlc->nlc_namelen) {
			if (ncp->nc_flag & NCF_DESTROYED) {
				if (ncp->nc_refs == 1 && rep_ncp == NULL)
					rep_ncp = ncp;
				continue;
			}
			if (bcmp(ncp->nc_name, nlc->nlc_nameptr, ncp->nc_nlen))
				continue;
			_cache_hold(ncp);
			if (use_excl)
				spin_unlock(&nchpp->spin);
			else
				spin_unlock_shared(&nchpp->spin);
			if (par_locked) {
				_cache_unlock(par_nch->ncp);
				par_locked = 0;
			}
			if (_cache_lock_special(ncp) == 0) {
				/*
				 * Successfully locked but we must re-test
				 * conditions that might have changed since
				 * we did not have the lock before.
				 */
				if (ncp->nc_parent != par_nch->ncp ||
				    ncp->nc_nlen != nlc->nlc_namelen ||
				    bcmp(ncp->nc_name, nlc->nlc_nameptr,
					 ncp->nc_nlen) ||
				    (ncp->nc_flag & NCF_DESTROYED)) {
					_cache_put(ncp);
					goto restart;
				}
				_cache_auto_unresolve(mp, ncp);
				if (new_ncp) {
					_cache_free(new_ncp);
					new_ncp = NULL; /* safety */
				}
				goto found;
			}
			_cache_get(ncp);	/* cycle the lock to block */
			_cache_put(ncp);
			_cache_drop(ncp);
			goto restart;
		}
	}

	/*
	 * We failed to locate the entry, try to resurrect a destroyed
	 * entry that we did find that is already correctly linked into
	 * nchpp and the parent.  We must re-test conditions after
	 * successfully locking rep_ncp.
	 *
	 * This case can occur under heavy loads due to not being able
	 * to safely lock the parent in cache_zap().  Nominally a repeated
	 * create/unlink load, but only the namelen needs to match.
	 *
	 * An exclusive lock on the nchpp is required to process this case,
	 * otherwise a race can cause duplicate entries to be created with
	 * one cpu reusing a DESTROYED ncp while another creates a new_ncp.
	 */
	if (rep_ncp && use_excl) {
		if (_cache_lock_nonblock(rep_ncp) == 0) {
			_cache_hold(rep_ncp);
			if (rep_ncp->nc_parent == par_nch->ncp &&
			    rep_ncp->nc_nlen == nlc->nlc_namelen &&
			    (rep_ncp->nc_flag & NCF_DESTROYED) &&
			    rep_ncp->nc_refs == 2) {
				/*
				 * Update nc_name.
				 */
				ncp = rep_ncp;
				bcopy(nlc->nlc_nameptr, ncp->nc_name,
				      nlc->nlc_namelen);

				/*
				 * This takes some care.  We must clear the
				 * NCF_DESTROYED flag before unlocking the
				 * hash chain so other concurrent searches
				 * do not skip this element.
				 *
				 * We must also unlock the hash chain before
				 * unresolving the ncp to avoid deadlocks.
				 * We hold the lock on the ncp so we can safely
				 * reinitialize nc_flag after that.
				 */
				ncp->nc_flag &= ~NCF_DESTROYED;
				spin_unlock(&nchpp->spin);	/* use_excl */

				_cache_setunresolved(ncp);
				ncp->nc_flag = NCF_UNRESOLVED;
				ncp->nc_error = ENOTCONN;
				if (par_locked) {
					_cache_unlock(par_nch->ncp);
					par_locked = 0;
				}
				if (new_ncp) {
					_cache_free(new_ncp);
					new_ncp = NULL; /* safety */
				}
				goto found;
			}
			_cache_put(rep_ncp);
		}
	}

	/*
	 * Otherwise create a new entry and add it to the cache.  The parent
	 * ncp must also be locked so we can link into it.
	 *
	 * We have to relookup after possibly blocking in kmalloc or
	 * when locking par_nch.
	 *
	 * NOTE: nlc_namelen can be 0 and nlc_nameptr NULL as a special
	 *	 mount case, in which case nc_name will be NULL.
	 *
	 * NOTE: In the rep_ncp != NULL case we are trying to reuse
	 *	 a DESTROYED entry, but didn't have an exclusive lock.
	 *	 In this situation we do not create a new_ncp.
	 */
	if (new_ncp == NULL) {
		if (use_excl)
			spin_unlock(&nchpp->spin);
		else
			spin_unlock_shared(&nchpp->spin);
		if (rep_ncp == NULL) {
			new_ncp = cache_alloc(nlc->nlc_namelen);
			if (nlc->nlc_namelen) {
				bcopy(nlc->nlc_nameptr, new_ncp->nc_name,
				      nlc->nlc_namelen);
				new_ncp->nc_name[nlc->nlc_namelen] = 0;
			}
		}
		use_excl = 1;
		goto restart;
	}

	/*
	 * NOTE! The spinlock is held exclusively here because new_ncp
	 *	 is non-NULL.
	 */
	if (par_locked == 0) {
		spin_unlock(&nchpp->spin);
		_cache_lock(par_nch->ncp);
		par_locked = 1;
		goto restart;
	}

	/*
	 * Link to parent (requires another ref, the one already in new_ncp
	 * is what we wil lreturn).
	 *
	 * WARNING!  We still hold the spinlock.  We have to set the hash
	 *	     table entry atomically.
	 */
	ncp = new_ncp;
	++ncp->nc_refs;
	_cache_link_parent(ncp, par_nch->ncp, nchpp);
	spin_unlock(&nchpp->spin);
	_cache_unlock(par_nch->ncp);
	/* par_locked = 0 - not used */
found:
	/*
	 * stats and namecache size management
	 */
	if (ncp->nc_flag & NCF_UNRESOLVED)
		++gd->gd_nchstats->ncs_miss;
	else if (ncp->nc_vp)
		++gd->gd_nchstats->ncs_goodhits;
	else
		++gd->gd_nchstats->ncs_neghits;
	nch.mount = mp;
	nch.ncp = ncp;
	_cache_mntref(nch.mount);

	return(nch);
}

/*
 * Attempt to lookup a namecache entry and return with a shared namecache
 * lock.  This operates non-blocking.  EWOULDBLOCK is returned if excl is
 * set or we are unable to lock.
 */
int
cache_nlookup_maybe_shared(struct nchandle *par_nch,
			   struct nlcomponent *nlc,
			   int excl, struct nchandle *res_nch)
{
	struct namecache *ncp;
	struct nchash_head *nchpp;
	struct mount *mp;
	u_int32_t hash;
	globaldata_t gd;

	/*
	 * If exclusive requested or shared namecache locks are disabled,
	 * return failure.
	 */
	if (ncp_shared_lock_disable || excl)
		return(EWOULDBLOCK);

	gd = mycpu;
	mp = par_nch->mount;

	/*
	 * This is a good time to call it, no ncp's are locked by
	 * the caller or us.
	 */
	cache_hysteresis(1);

	/*
	 * Try to locate an existing entry
	 */
	hash = fnv_32_buf(nlc->nlc_nameptr, nlc->nlc_namelen, FNV1_32_INIT);
	hash = fnv_32_buf(&par_nch->ncp, sizeof(par_nch->ncp), hash);
	nchpp = NCHHASH(hash);

	spin_lock_shared(&nchpp->spin);

	TAILQ_FOREACH(ncp, &nchpp->list, nc_hash) {
		/*
		 * Break out if we find a matching entry.  Note that
		 * UNRESOLVED entries may match, but DESTROYED entries
		 * do not.
		 */
		if (ncp->nc_parent == par_nch->ncp &&
		    ncp->nc_nlen == nlc->nlc_namelen &&
		    bcmp(ncp->nc_name, nlc->nlc_nameptr, ncp->nc_nlen) == 0 &&
		    (ncp->nc_flag & NCF_DESTROYED) == 0
		) {
			_cache_hold(ncp);
			spin_unlock_shared(&nchpp->spin);

			if (_cache_lock_shared_special(ncp) == 0) {
				if (ncp->nc_parent == par_nch->ncp &&
				    ncp->nc_nlen == nlc->nlc_namelen &&
				    bcmp(ncp->nc_name, nlc->nlc_nameptr,
					 ncp->nc_nlen) == 0 &&
				    (ncp->nc_flag & NCF_DESTROYED) == 0 &&
				    (ncp->nc_flag & NCF_UNRESOLVED) == 0 &&
				    _cache_auto_unresolve_test(mp, ncp) == 0) {
					goto found;
				}
				_cache_unlock(ncp);
			}
			_cache_drop(ncp);
			return(EWOULDBLOCK);
		}
	}

	/*
	 * Failure
	 */
	spin_unlock_shared(&nchpp->spin);
	return(EWOULDBLOCK);

	/*
	 * Success
	 *
	 * Note that nc_error might be non-zero (e.g ENOENT).
	 */
found:
	res_nch->mount = mp;
	res_nch->ncp = ncp;
	++gd->gd_nchstats->ncs_goodhits;
	_cache_mntref(res_nch->mount);

	KKASSERT(ncp->nc_error != EWOULDBLOCK);
	return(ncp->nc_error);
}

/*
 * This is a non-blocking verison of cache_nlookup() used by
 * nfs_readdirplusrpc_uio().  It can fail for any reason and
 * will return nch.ncp == NULL in that case.
 */
struct nchandle
cache_nlookup_nonblock(struct nchandle *par_nch, struct nlcomponent *nlc)
{
	struct nchandle nch;
	struct namecache *ncp;
	struct namecache *new_ncp;
	struct nchash_head *nchpp;
	struct mount *mp;
	u_int32_t hash;
	globaldata_t gd;
	int par_locked;

	gd = mycpu;
	mp = par_nch->mount;
	par_locked = 0;

	/*
	 * Try to locate an existing entry
	 */
	hash = fnv_32_buf(nlc->nlc_nameptr, nlc->nlc_namelen, FNV1_32_INIT);
	hash = fnv_32_buf(&par_nch->ncp, sizeof(par_nch->ncp), hash);
	new_ncp = NULL;
	nchpp = NCHHASH(hash);
restart:
	spin_lock(&nchpp->spin);
	TAILQ_FOREACH(ncp, &nchpp->list, nc_hash) {
		/*
		 * Break out if we find a matching entry.  Note that
		 * UNRESOLVED entries may match, but DESTROYED entries
		 * do not.
		 */
		if (ncp->nc_parent == par_nch->ncp &&
		    ncp->nc_nlen == nlc->nlc_namelen &&
		    bcmp(ncp->nc_name, nlc->nlc_nameptr, ncp->nc_nlen) == 0 &&
		    (ncp->nc_flag & NCF_DESTROYED) == 0
		) {
			_cache_hold(ncp);
			spin_unlock(&nchpp->spin);
			if (par_locked) {
				_cache_unlock(par_nch->ncp);
				par_locked = 0;
			}
			if (_cache_lock_special(ncp) == 0) {
				if (ncp->nc_parent != par_nch->ncp ||
				    ncp->nc_nlen != nlc->nlc_namelen ||
				    bcmp(ncp->nc_name, nlc->nlc_nameptr, ncp->nc_nlen) ||
				    (ncp->nc_flag & NCF_DESTROYED)) {
					kprintf("cache_lookup_nonblock: "
						"ncp-race %p %*.*s\n",
						ncp,
						nlc->nlc_namelen,
						nlc->nlc_namelen,
						nlc->nlc_nameptr);
					_cache_unlock(ncp);
					_cache_drop(ncp);
					goto failed;
				}
				_cache_auto_unresolve(mp, ncp);
				if (new_ncp) {
					_cache_free(new_ncp);
					new_ncp = NULL;
				}
				goto found;
			}
			_cache_drop(ncp);
			goto failed;
		}
	}

	/*
	 * We failed to locate an entry, create a new entry and add it to
	 * the cache.  The parent ncp must also be locked so we
	 * can link into it.
	 *
	 * We have to relookup after possibly blocking in kmalloc or
	 * when locking par_nch.
	 *
	 * NOTE: nlc_namelen can be 0 and nlc_nameptr NULL as a special
	 *	 mount case, in which case nc_name will be NULL.
	 */
	if (new_ncp == NULL) {
		spin_unlock(&nchpp->spin);
		new_ncp = cache_alloc(nlc->nlc_namelen);
		if (nlc->nlc_namelen) {
			bcopy(nlc->nlc_nameptr, new_ncp->nc_name,
			      nlc->nlc_namelen);
			new_ncp->nc_name[nlc->nlc_namelen] = 0;
		}
		goto restart;
	}
	if (par_locked == 0) {
		spin_unlock(&nchpp->spin);
		if (_cache_lock_nonblock(par_nch->ncp) == 0) {
			par_locked = 1;
			goto restart;
		}
		goto failed;
	}

	/*
	 * Link to parent (requires another ref, the one already in new_ncp
	 * is what we wil lreturn).
	 *
	 * WARNING!  We still hold the spinlock.  We have to set the hash
	 *	     table entry atomically.
	 */
	ncp = new_ncp;
	++ncp->nc_refs;
	_cache_link_parent(ncp, par_nch->ncp, nchpp);
	spin_unlock(&nchpp->spin);
	_cache_unlock(par_nch->ncp);
	/* par_locked = 0 - not used */
found:
	/*
	 * stats and namecache size management
	 */
	if (ncp->nc_flag & NCF_UNRESOLVED)
		++gd->gd_nchstats->ncs_miss;
	else if (ncp->nc_vp)
		++gd->gd_nchstats->ncs_goodhits;
	else
		++gd->gd_nchstats->ncs_neghits;
	nch.mount = mp;
	nch.ncp = ncp;
	_cache_mntref(nch.mount);

	return(nch);
failed:
	if (new_ncp) {
		_cache_free(new_ncp);
		new_ncp = NULL;
	}
	nch.mount = NULL;
	nch.ncp = NULL;
	return(nch);
}

/*
 * This version is non-locking.  The caller must validate the result
 * for parent-to-child continuity.
 *
 * It can fail for any reason and will return nch.ncp == NULL in that case.
 */
struct nchandle
cache_nlookup_nonlocked(struct nchandle *par_nch, struct nlcomponent *nlc)
{
	struct nchandle nch;
	struct namecache *ncp;
	struct nchash_head *nchpp;
	struct mount *mp;
	u_int32_t hash;
	globaldata_t gd;

	gd = mycpu;
	mp = par_nch->mount;

	/*
	 * Try to locate an existing entry
	 */
	hash = fnv_32_buf(nlc->nlc_nameptr, nlc->nlc_namelen, FNV1_32_INIT);
	hash = fnv_32_buf(&par_nch->ncp, sizeof(par_nch->ncp), hash);
	nchpp = NCHHASH(hash);

	spin_lock_shared(&nchpp->spin);
	TAILQ_FOREACH(ncp, &nchpp->list, nc_hash) {
		/*
		 * Break out if we find a matching entry.  Note that
		 * UNRESOLVED entries may match, but DESTROYED entries
		 * do not.
		 *
		 * Resolved NFS entries which have timed out fail so the
		 * caller can rerun with normal locking.
		 */
		if (ncp->nc_parent == par_nch->ncp &&
		    ncp->nc_nlen == nlc->nlc_namelen &&
		    bcmp(ncp->nc_name, nlc->nlc_nameptr, ncp->nc_nlen) == 0 &&
		    (ncp->nc_flag & NCF_DESTROYED) == 0
		) {
			if (_cache_auto_unresolve_test(par_nch->mount, ncp))
				break;
			_cache_hold(ncp);
			spin_unlock_shared(&nchpp->spin);
			goto found;
		}
	}
	spin_unlock_shared(&nchpp->spin);
	nch.mount = NULL;
	nch.ncp = NULL;
	return nch;
found:
	/*
	 * stats and namecache size management
	 */
	if (ncp->nc_flag & NCF_UNRESOLVED)
		++gd->gd_nchstats->ncs_miss;
	else if (ncp->nc_vp)
		++gd->gd_nchstats->ncs_goodhits;
	else
		++gd->gd_nchstats->ncs_neghits;
	nch.mount = mp;
	nch.ncp = ncp;
	_cache_mntref(nch.mount);

	return(nch);
}

/*
 * The namecache entry is marked as being used as a mount point. 
 * Locate the mount if it is visible to the caller.  The DragonFly
 * mount system allows arbitrary loops in the topology and disentangles
 * those loops by matching against (mp, ncp) rather than just (ncp).
 * This means any given ncp can dive any number of mounts, depending
 * on the relative mount (e.g. nullfs) the caller is at in the topology.
 *
 * We use a very simple frontend cache to reduce SMP conflicts,
 * which we have to do because the mountlist scan needs an exclusive
 * lock around its ripout info list.  Not to mention that there might
 * be a lot of mounts.
 *
 * Because all mounts can potentially be accessed by all cpus, break the cpu's
 * down a bit to allow some contention rather than making the cache
 * excessively huge.
 *
 * The hash table is split into per-cpu areas, is 4-way set-associative.
 */
struct findmount_info {
	struct mount *result;
	struct mount *nch_mount;
	struct namecache *nch_ncp;
};

static __inline
struct ncmount_cache *
ncmount_cache_lookup4(struct mount *mp, struct namecache *ncp)
{
	uint32_t hash;

	hash = iscsi_crc32(&mp, sizeof(mp));
	hash = iscsi_crc32_ext(&ncp, sizeof(ncp), hash);
	hash ^= hash >> 16;
	hash = hash & ((NCMOUNT_NUMCACHE - 1) & ~(NCMOUNT_SET - 1));

	return (&ncmount_cache[hash]);
}

static
struct ncmount_cache *
ncmount_cache_lookup(struct mount *mp, struct namecache *ncp)
{
	struct ncmount_cache *ncc;
	struct ncmount_cache *best;
	int delta;
	int best_delta;
	int i;

	ncc = ncmount_cache_lookup4(mp, ncp);

	/*
	 * NOTE: When checking for a ticks overflow implement a slop of
	 *	 2 ticks just to be safe, because ticks is accessed
	 *	 non-atomically one CPU can increment it while another
	 *	 is still using the old value.
	 */
	if (ncc->ncp == ncp && ncc->mp == mp)	/* 0 */
		return ncc;
	delta = (int)(ticks - ncc->ticks);	/* beware GCC opts */
	if (delta < -2)				/* overflow reset */
		ncc->ticks = ticks;
	best = ncc;
	best_delta = delta;

	for (i = 1; i < NCMOUNT_SET; ++i) {	/* 1, 2, 3 */
		++ncc;
		if (ncc->ncp == ncp && ncc->mp == mp)
			return ncc;
		delta = (int)(ticks - ncc->ticks);
		if (delta < -2)
			ncc->ticks = ticks;
		if (delta > best_delta) {
			best_delta = delta;
			best = ncc;
		}
	}
	return best;
}

/*
 * pcpu-optimized mount search.  Locate the recursive mountpoint, avoid
 * doing an expensive mountlist_scan*() if possible.
 *
 * (mp, ncp) -> mountonpt.k
 *
 * Returns a referenced mount pointer or NULL
 *
 * General SMP operation uses a per-cpu umount_spin to interlock unmount
 * operations (that is, where the mp_target can be freed out from under us).
 *
 * Lookups use the ncc->updating counter to validate the contents in order
 * to avoid having to obtain the per cache-element spin-lock.  In addition,
 * the ticks field is only updated when it changes.  However, if our per-cpu
 * lock fails due to an unmount-in-progress, we fall-back to the
 * cache-element's spin-lock.
 */
struct mount *
cache_findmount(struct nchandle *nch)
{
	struct findmount_info info;
	struct ncmount_cache *ncc;
	struct ncmount_cache ncc_copy;
	struct mount *target;
	struct pcpu_ncache *pcpu;
	struct spinlock *spinlk;
	int update;

	pcpu = pcpu_ncache;
	if (ncmount_cache_enable == 0 || pcpu == NULL) {
		ncc = NULL;
		goto skip;
	}
	pcpu += mycpu->gd_cpuid;

again:
	ncc = ncmount_cache_lookup(nch->mount, nch->ncp);
	if (ncc->ncp == nch->ncp && ncc->mp == nch->mount) {
found:
		/*
		 * This is a bit messy for now because we do not yet have
		 * safe disposal of mount structures.  We have to ref
		 * ncc->mp_target but the 'update' counter only tell us
		 * whether the cache has changed after the fact.
		 *
		 * For now get a per-cpu spinlock that will only contend
		 * against umount's.  This is the best path.  If it fails,
		 * instead of waiting on the umount we fall-back to a
		 * shared ncc->spin lock, which will generally only cost a
		 * cache ping-pong.
		 */
		update = ncc->updating;
		if (__predict_true(spin_trylock(&pcpu->umount_spin))) {
			spinlk = &pcpu->umount_spin;
		} else {
			spinlk = &ncc->spin;
			spin_lock_shared(spinlk);
		}
		if (update & 1) {		/* update in progress */
			spin_unlock_any(spinlk);
			goto skip;
		}
		ncc_copy = *ncc;
		cpu_lfence();
		if (ncc->updating != update) {	/* content changed */
			spin_unlock_any(spinlk);
			goto again;
		}
		if (ncc_copy.ncp != nch->ncp || ncc_copy.mp != nch->mount) {
			spin_unlock_any(spinlk);
			goto again;
		}
		if (ncc_copy.isneg == 0) {
			target = ncc_copy.mp_target;
			if (target->mnt_ncmounton.mount == nch->mount &&
			    target->mnt_ncmounton.ncp == nch->ncp) {
				/*
				 * Cache hit (positive) (avoid dirtying
				 * the cache line if possible)
				 */
				if (ncc->ticks != (int)ticks)
					ncc->ticks = (int)ticks;
				_cache_mntref(target);
			}
		} else {
			/*
			 * Cache hit (negative) (avoid dirtying
			 * the cache line if possible)
			 */
			if (ncc->ticks != (int)ticks)
				ncc->ticks = (int)ticks;
			target = NULL;
		}
		spin_unlock_any(spinlk);

		return target;
	}
skip:

	/*
	 * Slow
	 */
	info.result = NULL;
	info.nch_mount = nch->mount;
	info.nch_ncp = nch->ncp;
	mountlist_scan(cache_findmount_callback, &info,
		       MNTSCAN_FORWARD | MNTSCAN_NOBUSY | MNTSCAN_NOUNLOCK);

	/*
	 * To reduce multi-re-entry on the cache, relookup in the cache.
	 * This can still race, obviously, but that's ok.
	 */
	ncc = ncmount_cache_lookup(nch->mount, nch->ncp);
	if (ncc->ncp == nch->ncp && ncc->mp == nch->mount) {
		if (info.result)
			atomic_add_int(&info.result->mnt_refs, -1);
		goto found;
	}

	/*
	 * Cache the result.
	 */
	if ((info.result == NULL ||
	    (info.result->mnt_kern_flag & MNTK_UNMOUNT) == 0)) {
		spin_lock(&ncc->spin);
		atomic_add_int_nonlocked(&ncc->updating, 1);
		cpu_sfence();
		KKASSERT(ncc->updating & 1);
		if (ncc->mp != nch->mount) {
			if (ncc->mp)
				atomic_add_int(&ncc->mp->mnt_refs, -1);
			atomic_add_int(&nch->mount->mnt_refs, 1);
			ncc->mp = nch->mount;
		}
		ncc->ncp = nch->ncp;	/* ptr compares only, not refd*/
		ncc->ticks = (int)ticks;

		if (info.result) {
			ncc->isneg = 0;
			if (ncc->mp_target != info.result) {
				if (ncc->mp_target)
					atomic_add_int(&ncc->mp_target->mnt_refs, -1);
				ncc->mp_target = info.result;
				atomic_add_int(&info.result->mnt_refs, 1);
			}
		} else {
			ncc->isneg = 1;
			if (ncc->mp_target) {
				atomic_add_int(&ncc->mp_target->mnt_refs, -1);
				ncc->mp_target = NULL;
			}
		}
		cpu_sfence();
		atomic_add_int_nonlocked(&ncc->updating, 1);
		spin_unlock(&ncc->spin);
	}
	return(info.result);
}

static
int
cache_findmount_callback(struct mount *mp, void *data)
{
	struct findmount_info *info = data;

	/*
	 * Check the mount's mounted-on point against the passed nch.
	 */
	if (mp->mnt_ncmounton.mount == info->nch_mount &&
	    mp->mnt_ncmounton.ncp == info->nch_ncp
	) {
	    info->result = mp;
	    _cache_mntref(mp);
	    return(-1);
	}
	return(0);
}

void
cache_dropmount(struct mount *mp)
{
	_cache_mntrel(mp);
}

/*
 * mp is being mounted, scrap entries matching mp->mnt_ncmounton (positive
 * or negative).
 *
 * A full scan is not required, but for now just do it anyway.
 */
void
cache_ismounting(struct mount *mp)
{
	struct ncmount_cache *ncc;
	struct mount *ncc_mp;
	int i;

	if (pcpu_ncache == NULL)
		return;

	for (i = 0; i < NCMOUNT_NUMCACHE; ++i) {
		ncc = &ncmount_cache[i];
		if (ncc->mp != mp->mnt_ncmounton.mount ||
		    ncc->ncp != mp->mnt_ncmounton.ncp) {
			continue;
		}
		spin_lock(&ncc->spin);
		atomic_add_int_nonlocked(&ncc->updating, 1);
		cpu_sfence();
		KKASSERT(ncc->updating & 1);
		if (ncc->mp != mp->mnt_ncmounton.mount ||
		    ncc->ncp != mp->mnt_ncmounton.ncp) {
			cpu_sfence();
			++ncc->updating;
			spin_unlock(&ncc->spin);
			continue;
		}
		ncc_mp = ncc->mp;
		ncc->ncp = NULL;
		ncc->mp = NULL;
		if (ncc_mp)
			atomic_add_int(&ncc_mp->mnt_refs, -1);
		ncc_mp = ncc->mp_target;
		ncc->mp_target = NULL;
		if (ncc_mp)
			atomic_add_int(&ncc_mp->mnt_refs, -1);
		ncc->ticks = (int)ticks - hz * 120;

		cpu_sfence();
		atomic_add_int_nonlocked(&ncc->updating, 1);
		spin_unlock(&ncc->spin);
	}

	/*
	 * Pre-cache the mount point
	 */
	ncc = ncmount_cache_lookup(mp->mnt_ncmounton.mount,
				   mp->mnt_ncmounton.ncp);

	spin_lock(&ncc->spin);
	atomic_add_int_nonlocked(&ncc->updating, 1);
	cpu_sfence();
	KKASSERT(ncc->updating & 1);

	if (ncc->mp)
		atomic_add_int(&ncc->mp->mnt_refs, -1);
	atomic_add_int(&mp->mnt_ncmounton.mount->mnt_refs, 1);
	ncc->mp = mp->mnt_ncmounton.mount;
	ncc->ncp = mp->mnt_ncmounton.ncp;	/* ptr compares only */
	ncc->ticks = (int)ticks;

	ncc->isneg = 0;
	if (ncc->mp_target != mp) {
		if (ncc->mp_target)
			atomic_add_int(&ncc->mp_target->mnt_refs, -1);
		ncc->mp_target = mp;
		atomic_add_int(&mp->mnt_refs, 1);
	}
	cpu_sfence();
	atomic_add_int_nonlocked(&ncc->updating, 1);
	spin_unlock(&ncc->spin);
}

/*
 * Scrap any ncmount_cache entries related to mp.  Not only do we need to
 * scrap entries matching mp->mnt_ncmounton, but we also need to scrap any
 * negative hits involving (mp, <any>).
 *
 * A full scan is required.
 */
void
cache_unmounting(struct mount *mp)
{
	struct ncmount_cache *ncc;
	struct pcpu_ncache *pcpu;
	struct mount *ncc_mp;
	int i;

	pcpu = pcpu_ncache;
	if (pcpu == NULL)
		return;

	for (i = 0; i < ncpus; ++i)
		spin_lock(&pcpu[i].umount_spin);

	for (i = 0; i < NCMOUNT_NUMCACHE; ++i) {
		ncc = &ncmount_cache[i];
		if (ncc->mp != mp && ncc->mp_target != mp)
			continue;
		spin_lock(&ncc->spin);
		atomic_add_int_nonlocked(&ncc->updating, 1);
		cpu_sfence();

		if (ncc->mp != mp && ncc->mp_target != mp) {
			atomic_add_int_nonlocked(&ncc->updating, 1);
			cpu_sfence();
			spin_unlock(&ncc->spin);
			continue;
		}
		ncc_mp = ncc->mp;
		ncc->ncp = NULL;
		ncc->mp = NULL;
		if (ncc_mp)
			atomic_add_int(&ncc_mp->mnt_refs, -1);
		ncc_mp = ncc->mp_target;
		ncc->mp_target = NULL;
		if (ncc_mp)
			atomic_add_int(&ncc_mp->mnt_refs, -1);
		ncc->ticks = (int)ticks - hz * 120;

		cpu_sfence();
		atomic_add_int_nonlocked(&ncc->updating, 1);
		spin_unlock(&ncc->spin);
	}

	for (i = 0; i < ncpus; ++i)
		spin_unlock(&pcpu[i].umount_spin);
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
 *
 * However, if a vnode was in the middle of a recyclement when the NCP
 * got locked, ncp->nc_vp might point to a vnode that is about to become
 * invalid.  cache_resolve() handles this case by unresolving the entry
 * and then re-resolving it.
 *
 * Note that successful resolution does not necessarily return an error
 * code of 0.  If the ncp resolves to a negative cache hit then ENOENT
 * will be returned.
 */
int
cache_resolve(struct nchandle *nch, struct ucred *cred)
{
	struct namecache *par_tmp;
	struct namecache *par;
	struct namecache *ncp;
	struct nchandle nctmp;
	struct mount *mp;
	struct vnode *dvp;
	int error;

	ncp = nch->ncp;
	mp = nch->mount;
	KKASSERT(_cache_lockstatus(ncp) == LK_EXCLUSIVE);
restart:
	/*
	 * If the ncp is already resolved we have nothing to do.  However,
	 * we do want to guarentee that a usable vnode is returned when
	 * a vnode is present, so make sure it hasn't been reclaimed.
	 */
	if ((ncp->nc_flag & NCF_UNRESOLVED) == 0) {
		if (ncp->nc_vp && (ncp->nc_vp->v_flag & VRECLAIMED))
			_cache_setunresolved(ncp);
		if ((ncp->nc_flag & NCF_UNRESOLVED) == 0)
			return (ncp->nc_error);
	}

	/*
	 * If the ncp was destroyed it will never resolve again.  This
	 * can basically only happen when someone is chdir'd into an
	 * empty directory which is then rmdir'd.  We want to catch this
	 * here and not dive the VFS because the VFS might actually
	 * have a way to re-resolve the disconnected ncp, which will
	 * result in inconsistencies in the cdir/nch for proc->p_fd.
	 */
	if (ncp->nc_flag & NCF_DESTROYED)
		return(EINVAL);

	/*
	 * Mount points need special handling because the parent does not
	 * belong to the same filesystem as the ncp.
	 */
	if (ncp == mp->mnt_ncmountpt.ncp)
		return (cache_resolve_mp(mp));

	/*
	 * We expect an unbroken chain of ncps to at least the mount point,
	 * and even all the way to root (but this code doesn't have to go
	 * past the mount point).
	 */
	if (ncp->nc_parent == NULL) {
		kprintf("EXDEV case 1 %p %*.*s\n", ncp,
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
	 *	- due to an rmdir (parent will be marked DESTROYED)
	 *
	 * When this occurs we have to track the chain backwards and resolve
	 * it, looping until the resolver catches up to the current node.  We
	 * could recurse here but we might run ourselves out of kernel stack
	 * so we do it in a more painful manner.  This situation really should
	 * not occur all that often, or if it does not have to go back too
	 * many nodes to resolve the ncp.
	 */
	while ((dvp = cache_dvpref(ncp)) == NULL) {
		/*
		 * This case can occur if a process is CD'd into a
		 * directory which is then rmdir'd.  If the parent is marked
		 * destroyed there is no point trying to resolve it.
		 */
		if (ncp->nc_parent->nc_flag & NCF_DESTROYED)
			return(ENOENT);
		par = ncp->nc_parent;
		_cache_hold(par);
		_cache_lock(par);
		while ((par_tmp = par->nc_parent) != NULL &&
		       par_tmp->nc_vp == NULL) {
			_cache_hold(par_tmp);
			_cache_lock(par_tmp);
			_cache_put(par);
			par = par_tmp;
		}
		if (par->nc_parent == NULL) {
			kprintf("EXDEV case 2 %*.*s\n",
				par->nc_nlen, par->nc_nlen, par->nc_name);
			_cache_put(par);
			return (EXDEV);
		}
		/*
		 * The parent is not set in stone, ref and lock it to prevent
		 * it from disappearing.  Also note that due to renames it
		 * is possible for our ncp to move and for par to no longer
		 * be one of its parents.  We resolve it anyway, the loop 
		 * will handle any moves.
		 */
		_cache_get(par);	/* additional hold/lock */
		_cache_put(par);	/* from earlier hold/lock */
		if (par == nch->mount->mnt_ncmountpt.ncp) {
			cache_resolve_mp(nch->mount);
		} else if ((dvp = cache_dvpref(par)) == NULL) {
			kprintf("[diagnostic] cache_resolve: raced on %*.*s\n",
				par->nc_nlen, par->nc_nlen, par->nc_name);
			_cache_put(par);
			continue;
		} else {
			if (par->nc_flag & NCF_UNRESOLVED) {
				nctmp.mount = mp;
				nctmp.ncp = par;
				par->nc_error = VOP_NRESOLVE(&nctmp, dvp, cred);
			}
			vrele(dvp);
		}
		if ((error = par->nc_error) != 0) {
			if (par->nc_error != EAGAIN) {
				kprintf("EXDEV case 3 %*.*s error %d\n",
				    par->nc_nlen, par->nc_nlen, par->nc_name,
				    par->nc_error);
				_cache_put(par);
				return(error);
			}
			kprintf("[diagnostic] cache_resolve: EAGAIN par %p %*.*s\n",
				par, par->nc_nlen, par->nc_nlen, par->nc_name);
		}
		_cache_put(par);
		/* loop */
	}

	/*
	 * Call VOP_NRESOLVE() to get the vp, then scan for any disconnected
	 * ncp's and reattach them.  If this occurs the original ncp is marked
	 * EAGAIN to force a relookup.
	 *
	 * NOTE: in order to call VOP_NRESOLVE(), the parent of the passed
	 * ncp must already be resolved.
	 */
	if (dvp) {
		nctmp.mount = mp;
		nctmp.ncp = ncp;
		ncp->nc_error = VOP_NRESOLVE(&nctmp, dvp, cred);
		vrele(dvp);
	} else {
		ncp->nc_error = EPERM;
	}
	if (ncp->nc_error == EAGAIN) {
		kprintf("[diagnostic] cache_resolve: EAGAIN ncp %p %*.*s\n",
			ncp, ncp->nc_nlen, ncp->nc_nlen, ncp->nc_name);
		goto restart;
	}
	return(ncp->nc_error);
}

/*
 * Resolve the ncp associated with a mount point.  Such ncp's almost always
 * remain resolved and this routine is rarely called.  NFS MPs tends to force
 * re-resolution more often due to its mac-truck-smash-the-namecache
 * method of tracking namespace changes.
 *
 * The semantics for this call is that the passed ncp must be locked on
 * entry and will be locked on return.  However, if we actually have to
 * resolve the mount point we temporarily unlock the entry in order to
 * avoid race-to-root deadlocks due to e.g. dead NFS mounts.  Because of
 * the unlock we have to recheck the flags after we relock.
 */
static int
cache_resolve_mp(struct mount *mp)
{
	struct namecache *ncp = mp->mnt_ncmountpt.ncp;
	struct vnode *vp;
	int error;

	KKASSERT(mp != NULL);

	/*
	 * If the ncp is already resolved we have nothing to do.  However,
	 * we do want to guarentee that a usable vnode is returned when
	 * a vnode is present, so make sure it hasn't been reclaimed.
	 */
	if ((ncp->nc_flag & NCF_UNRESOLVED) == 0) {
		if (ncp->nc_vp && (ncp->nc_vp->v_flag & VRECLAIMED))
			_cache_setunresolved(ncp);
	}

	if (ncp->nc_flag & NCF_UNRESOLVED) {
		_cache_unlock(ncp);
		while (vfs_busy(mp, 0))
			;
		error = VFS_ROOT(mp, &vp);
		_cache_lock(ncp);

		/*
		 * recheck the ncp state after relocking.
		 */
		if (ncp->nc_flag & NCF_UNRESOLVED) {
			ncp->nc_error = error;
			if (error == 0) {
				_cache_setvp(mp, ncp, vp);
				vput(vp);
			} else {
				kprintf("[diagnostic] cache_resolve_mp: failed"
					" to resolve mount %p err=%d ncp=%p\n",
					mp, error, ncp);
				_cache_setvp(mp, ncp, NULL);
			}
		} else if (error == 0) {
			vput(vp);
		}
		vfs_unbusy(mp);
	}
	return(ncp->nc_error);
}

/*
 * Clean out negative cache entries when too many have accumulated.
 */
static void
_cache_cleanneg(long count)
{
	struct pcpu_ncache *pn;
	struct namecache *ncp;
	static uint32_t neg_rover;
	uint32_t n;
	long vnegs;

	n = neg_rover++;	/* SMP heuristical, race ok */
	cpu_ccfence();
	n = n % (uint32_t)ncpus;

	/*
	 * Normalize vfscache_negs and count.  count is sometimes based
	 * on vfscache_negs.  vfscache_negs is heuristical and can sometimes
	 * have crazy values.
	 */
	vnegs = vfscache_negs;
	cpu_ccfence();
	if (vnegs <= MINNEG)
		vnegs = MINNEG;
	if (count < 1)
		count = 1;

	pn = &pcpu_ncache[n];
	spin_lock(&pn->neg_spin);
	count = pn->neg_count * count / vnegs + 1;
	spin_unlock(&pn->neg_spin);

	/*
	 * Attempt to clean out the specified number of negative cache
	 * entries.
	 */
	while (count > 0) {
		spin_lock(&pn->neg_spin);
		ncp = TAILQ_FIRST(&pn->neg_list);
		if (ncp == NULL) {
			spin_unlock(&pn->neg_spin);
			break;
		}
		TAILQ_REMOVE(&pn->neg_list, ncp, nc_vnode);
		TAILQ_INSERT_TAIL(&pn->neg_list, ncp, nc_vnode);
		_cache_hold(ncp);
		spin_unlock(&pn->neg_spin);

		/*
		 * This can race, so we must re-check that the ncp
		 * is on the ncneg.list after successfully locking it.
		 */
		if (_cache_lock_special(ncp) == 0) {
			if (ncp->nc_vp == NULL &&
			    (ncp->nc_flag & NCF_UNRESOLVED) == 0) {
				cache_zap(ncp);
			} else {
				_cache_unlock(ncp);
				_cache_drop(ncp);
			}
		} else {
			_cache_drop(ncp);
		}
		--count;
	}
}

/*
 * Clean out positive cache entries when too many have accumulated.
 */
static void
_cache_cleanpos(long count)
{
	static volatile int rover;
	struct nchash_head *nchpp;
	struct namecache *ncp;
	int rover_copy;

	/*
	 * Attempt to clean out the specified number of negative cache
	 * entries.
	 */
	while (count > 0) {
		rover_copy = ++rover;	/* MPSAFEENOUGH */
		cpu_ccfence();
		nchpp = NCHHASH(rover_copy);

		if (TAILQ_FIRST(&nchpp->list) == NULL) {
			--count;
			continue;
		}

		/*
		 * Cycle ncp on list, ignore and do not move DUMMY
		 * ncps.  These are temporary list iterators.
		 *
		 * We must cycle the ncp to the end of the list to
		 * ensure that all ncp's have an equal chance of
		 * being removed.
		 */
		spin_lock(&nchpp->spin);
		ncp = TAILQ_FIRST(&nchpp->list);
		while (ncp && (ncp->nc_flag & NCF_DUMMY))
			ncp = TAILQ_NEXT(ncp, nc_hash);
		if (ncp) {
			TAILQ_REMOVE(&nchpp->list, ncp, nc_hash);
			TAILQ_INSERT_TAIL(&nchpp->list, ncp, nc_hash);
			_cache_hold(ncp);
		}
		spin_unlock(&nchpp->spin);

		if (ncp) {
			if (_cache_lock_special(ncp) == 0) {
				cache_zap(ncp);
			} else {
				_cache_drop(ncp);
			}
		}
		--count;
	}
}

/*
 * This is a kitchen sink function to clean out ncps which we
 * tried to zap from cache_drop() but failed because we were
 * unable to acquire the parent lock.
 *
 * Such entries can also be removed via cache_inval_vp(), such
 * as when unmounting.
 */
static void
_cache_cleandefered(void)
{
	struct nchash_head *nchpp;
	struct namecache *ncp;
	struct namecache dummy;
	int i;

	/*
	 * Create a list iterator.  DUMMY indicates that this is a list
	 * iterator, DESTROYED prevents matches by lookup functions.
	 */
	numdefered = 0;
	pcpu_ncache[mycpu->gd_cpuid].numdefered = 0;
	bzero(&dummy, sizeof(dummy));
	dummy.nc_flag = NCF_DESTROYED | NCF_DUMMY;
	dummy.nc_refs = 1;

	for (i = 0; i <= nchash; ++i) {
		nchpp = &nchashtbl[i];

		spin_lock(&nchpp->spin);
		TAILQ_INSERT_HEAD(&nchpp->list, &dummy, nc_hash);
		ncp = &dummy;
		while ((ncp = TAILQ_NEXT(ncp, nc_hash)) != NULL) {
			if ((ncp->nc_flag & NCF_DEFEREDZAP) == 0)
				continue;
			TAILQ_REMOVE(&nchpp->list, &dummy, nc_hash);
			TAILQ_INSERT_AFTER(&nchpp->list, ncp, &dummy, nc_hash);
			_cache_hold(ncp);
			spin_unlock(&nchpp->spin);
			if (_cache_lock_nonblock(ncp) == 0) {
				ncp->nc_flag &= ~NCF_DEFEREDZAP;
				_cache_unlock(ncp);
			}
			_cache_drop(ncp);
			spin_lock(&nchpp->spin);
			ncp = &dummy;
		}
		TAILQ_REMOVE(&nchpp->list, &dummy, nc_hash);
		spin_unlock(&nchpp->spin);
	}
}

/*
 * Name cache initialization, from vfsinit() when we are booting
 */
void
nchinit(void)
{
	struct pcpu_ncache *pn;
	globaldata_t gd;
	int i;

	/*
	 * Per-cpu accounting and negative hit list
	 */
	pcpu_ncache = kmalloc(sizeof(*pcpu_ncache) * ncpus,
			      M_VFSCACHE, M_WAITOK|M_ZERO);
	for (i = 0; i < ncpus; ++i) {
		pn = &pcpu_ncache[i];
		TAILQ_INIT(&pn->neg_list);
		spin_init(&pn->neg_spin, "ncneg");
		spin_init(&pn->umount_spin, "ncumm");
	}

	/*
	 * Initialise per-cpu namecache effectiveness statistics.
	 */
	for (i = 0; i < ncpus; ++i) {
		gd = globaldata_find(i);
		gd->gd_nchstats = &nchstats[i];
	}

	/*
	 * Create a generous namecache hash table
	 */
	nchashtbl = hashinit_ext(vfs_inodehashsize(),
				 sizeof(struct nchash_head),
				 M_VFSCACHE, &nchash);
	for (i = 0; i <= (int)nchash; ++i) {
		TAILQ_INIT(&nchashtbl[i].list);
		spin_init(&nchashtbl[i].spin, "nchinit_hash");
	}
	for (i = 0; i < NCMOUNT_NUMCACHE; ++i)
		spin_init(&ncmount_cache[i].spin, "nchinit_cache");
	nclockwarn = 5 * hz;
}

/*
 * Called from start_init() to bootstrap the root filesystem.  Returns
 * a referenced, unlocked namecache record.
 */
void
cache_allocroot(struct nchandle *nch, struct mount *mp, struct vnode *vp)
{
	nch->ncp = cache_alloc(0);
	nch->mount = mp;
	_cache_mntref(mp);
	if (vp)
		_cache_setvp(nch->mount, nch->ncp, vp);
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
vfs_cache_setroot(struct vnode *nvp, struct nchandle *nch)
{
	struct vnode *ovp;
	struct nchandle onch;

	ovp = rootvnode;
	onch = rootnch;
	rootvnode = nvp;
	if (nch)
		rootnch = *nch;
	else
		cache_zero(&rootnch);
	if (ovp)
		vrele(ovp);
	if (onch.ncp)
		cache_drop(&onch);
}

/*
 * XXX OLD API COMPAT FUNCTION.  This really messes up the new namecache
 * topology and is being removed as quickly as possible.  The new VOP_N*()
 * API calls are required to make specific adjustments using the supplied
 * ncp pointers rather then just bogusly purging random vnodes.
 *
 * Invalidate all namecache entries to a particular vnode as well as 
 * any direct children of that vnode in the namecache.  This is a 
 * 'catch all' purge used by filesystems that do not know any better.
 *
 * Note that the linkage between the vnode and its namecache entries will
 * be removed, but the namecache entries themselves might stay put due to
 * active references from elsewhere in the system or due to the existance of
 * the children.   The namecache topology is left intact even if we do not
 * know what the vnode association is.  Such entries will be marked
 * NCF_UNRESOLVED.
 */
void
cache_purge(struct vnode *vp)
{
	cache_inval_vp(vp, CINV_DESTROY | CINV_CHILDREN);
}

__read_mostly static int disablecwd;
SYSCTL_INT(_debug, OID_AUTO, disablecwd, CTLFLAG_RW, &disablecwd, 0,
    "Disable getcwd");

/*
 * MPALMOSTSAFE
 */
int
sys___getcwd(struct sysmsg *sysmsg, const struct __getcwd_args *uap)
{
	u_int buflen;
	int error;
	char *buf;
	char *bp;

	if (disablecwd)
		return (ENODEV);

	buflen = uap->buflen;
	if (buflen == 0)
		return (EINVAL);
	if (buflen > MAXPATHLEN)
		buflen = MAXPATHLEN;

	buf = kmalloc(buflen, M_TEMP, M_WAITOK);
	bp = kern_getcwd(buf, buflen, &error);
	if (error == 0)
		error = copyout(bp, uap->buf, strlen(bp) + 1);
	kfree(buf, M_TEMP);
	return (error);
}

char *
kern_getcwd(char *buf, size_t buflen, int *error)
{
	struct proc *p = curproc;
	char *bp;
	int i, slash_prefixed;
	struct filedesc *fdp;
	struct nchandle nch;
	struct namecache *ncp;

	bp = buf;
	bp += buflen - 1;
	*bp = '\0';
	fdp = p->p_fd;
	slash_prefixed = 0;

	nch = fdp->fd_ncdir;
	ncp = nch.ncp;
	if (ncp)
		_cache_hold(ncp);

	while (ncp && (ncp != fdp->fd_nrdir.ncp ||
	       nch.mount != fdp->fd_nrdir.mount)
	) {
		/*
		 * While traversing upwards if we encounter the root
		 * of the current mount we have to skip to the mount point
		 * in the underlying filesystem.
		 */
		if (ncp == nch.mount->mnt_ncmountpt.ncp) {
			nch = nch.mount->mnt_ncmounton;
			_cache_drop(ncp);
			ncp = nch.ncp;
			if (ncp)
				_cache_hold(ncp);
			continue;
		}

		/*
		 * Prepend the path segment
		 */
		for (i = ncp->nc_nlen - 1; i >= 0; i--) {
			if (bp == buf) {
				*error = ERANGE;
				bp = NULL;
				goto done;
			}
			*--bp = ncp->nc_name[i];
		}
		if (bp == buf) {
			*error = ERANGE;
			bp = NULL;
			goto done;
		}
		*--bp = '/';
		slash_prefixed = 1;

		/*
		 * Go up a directory.  This isn't a mount point so we don't
		 * have to check again.
		 */
		while ((nch.ncp = ncp->nc_parent) != NULL) {
			if (ncp_shared_lock_disable)
				_cache_lock(ncp);
			else
				_cache_lock_shared(ncp);
			if (nch.ncp != ncp->nc_parent) {
				_cache_unlock(ncp);
				continue;
			}
			_cache_hold(nch.ncp);
			_cache_unlock(ncp);
			break;
		}
		_cache_drop(ncp);
		ncp = nch.ncp;
	}
	if (ncp == NULL) {
		*error = ENOENT;
		bp = NULL;
		goto done;
	}
	if (!slash_prefixed) {
		if (bp == buf) {
			*error = ERANGE;
			bp = NULL;
			goto done;
		}
		*--bp = '/';
	}
	*error = 0;
done:
	if (ncp)
		_cache_drop(ncp);
	return (bp);
}

/*
 * Thus begins the fullpath magic.
 *
 * The passed nchp is referenced but not locked.
 */
__read_mostly static int disablefullpath;
SYSCTL_INT(_debug, OID_AUTO, disablefullpath, CTLFLAG_RW,
    &disablefullpath, 0,
    "Disable fullpath lookups");

int
cache_fullpath(struct proc *p, struct nchandle *nchp, struct nchandle *nchbase,
	       char **retbuf, char **freebuf, int guess)
{
	struct nchandle fd_nrdir;
	struct nchandle nch;
	struct namecache *ncp;
	struct mount *mp, *new_mp;
	char *bp, *buf;
	int slash_prefixed;
	int error = 0;
	int i;

	*retbuf = NULL; 
	*freebuf = NULL;

	buf = kmalloc(MAXPATHLEN, M_TEMP, M_WAITOK);
	bp = buf + MAXPATHLEN - 1;
	*bp = '\0';
	if (nchbase)
		fd_nrdir = *nchbase;
	else if (p != NULL)
		fd_nrdir = p->p_fd->fd_nrdir;
	else
		fd_nrdir = rootnch;
	slash_prefixed = 0;
	nch = *nchp;
	ncp = nch.ncp;
	if (ncp)
		_cache_hold(ncp);
	mp = nch.mount;

	while (ncp && (ncp != fd_nrdir.ncp || mp != fd_nrdir.mount)) {
		new_mp = NULL;

		/*
		 * If we are asked to guess the upwards path, we do so whenever
		 * we encounter an ncp marked as a mountpoint. We try to find
		 * the actual mountpoint by finding the mountpoint with this
		 * ncp.
		 */
		if (guess && (ncp->nc_flag & NCF_ISMOUNTPT)) {
			new_mp = mount_get_by_nc(ncp);
		}
		/*
		 * While traversing upwards if we encounter the root
		 * of the current mount we have to skip to the mount point.
		 */
		if (ncp == mp->mnt_ncmountpt.ncp) {
			new_mp = mp;
		}
		if (new_mp) {
			nch = new_mp->mnt_ncmounton;
			_cache_drop(ncp);
			ncp = nch.ncp;
			if (ncp)
				_cache_hold(ncp);
			mp = nch.mount;
			continue;
		}

		/*
		 * Prepend the path segment
		 */
		for (i = ncp->nc_nlen - 1; i >= 0; i--) {
			if (bp == buf) {
				kfree(buf, M_TEMP);
				error = ENOMEM;
				goto done;
			}
			*--bp = ncp->nc_name[i];
		}
		if (bp == buf) {
			kfree(buf, M_TEMP);
			error = ENOMEM;
			goto done;
		}
		*--bp = '/';
		slash_prefixed = 1;

		/*
		 * Go up a directory.  This isn't a mount point so we don't
		 * have to check again.
		 *
		 * We can only safely access nc_parent with ncp held locked.
		 */
		while ((nch.ncp = ncp->nc_parent) != NULL) {
			_cache_lock_shared(ncp);
			if (nch.ncp != ncp->nc_parent) {
				_cache_unlock(ncp);
				continue;
			}
			_cache_hold(nch.ncp);
			_cache_unlock(ncp);
			break;
		}
		_cache_drop(ncp);
		ncp = nch.ncp;
	}
	if (ncp == NULL) {
		kfree(buf, M_TEMP);
		error = ENOENT;
		goto done;
	}

	if (!slash_prefixed) {
		if (bp == buf) {
			kfree(buf, M_TEMP);
			error = ENOMEM;
			goto done;
		}
		*--bp = '/';
	}
	*retbuf = bp; 
	*freebuf = buf;
	error = 0;
done:
	if (ncp)
		_cache_drop(ncp);
	return(error);
}

int
vn_fullpath(struct proc *p, struct vnode *vn, char **retbuf,
	    char **freebuf, int guess)
{
	struct namecache *ncp;
	struct nchandle nch;
	int error;

	*freebuf = NULL;
	if (disablefullpath)
		return (ENODEV);

	if (p == NULL)
		return (EINVAL);

	/* vn is NULL, client wants us to use p->p_textvp */
	if (vn == NULL) {
		if ((vn = p->p_textvp) == NULL)
			return (EINVAL);
	}
	spin_lock_shared(&vn->v_spin);
	TAILQ_FOREACH(ncp, &vn->v_namecache, nc_vnode) {
		if (ncp->nc_nlen)
			break;
	}
	if (ncp == NULL) {
		spin_unlock_shared(&vn->v_spin);
		return (EINVAL);
	}
	_cache_hold(ncp);
	spin_unlock_shared(&vn->v_spin);

	nch.ncp = ncp;
	nch.mount = vn->v_mount;
	error = cache_fullpath(p, &nch, NULL, retbuf, freebuf, guess);
	_cache_drop(ncp);
	return (error);
}

void
vfscache_rollup_cpu(struct globaldata *gd)
{
	struct pcpu_ncache *pn;
	long count;

	if (pcpu_ncache == NULL)
		return;
	pn = &pcpu_ncache[gd->gd_cpuid];

	if (pn->vfscache_count) {
		count = atomic_swap_long(&pn->vfscache_count, 0);
		atomic_add_long(&vfscache_count, count);
	}
	if (pn->vfscache_leafs) {
		count = atomic_swap_long(&pn->vfscache_leafs, 0);
		atomic_add_long(&vfscache_leafs, count);
	}
	if (pn->vfscache_negs) {
		count = atomic_swap_long(&pn->vfscache_negs, 0);
		atomic_add_long(&vfscache_negs, count);
	}
	if (pn->numdefered) {
		count = atomic_swap_long(&pn->numdefered, 0);
		atomic_add_long(&numdefered, count);
	}
}
