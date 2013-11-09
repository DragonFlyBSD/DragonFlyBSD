/*
 * Copyright (c) 2003,2004,2009 The DragonFly Project.  All rights reserved.
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
#include <sys/kernel.h>
#include <sys/sysctl.h>
#include <sys/mount.h>
#include <sys/vnode.h>
#include <sys/malloc.h>
#include <sys/sysproto.h>
#include <sys/spinlock.h>
#include <sys/proc.h>
#include <sys/namei.h>
#include <sys/nlookup.h>
#include <sys/filedesc.h>
#include <sys/fnv_hash.h>
#include <sys/globaldata.h>
#include <sys/kern_syscall.h>
#include <sys/dirent.h>
#include <ddb/ddb.h>

#include <sys/sysref2.h>
#include <sys/spinlock2.h>
#include <sys/mplock2.h>

#define MAX_RECURSION_DEPTH	64

/*
 * Random lookups in the cache are accomplished with a hash table using
 * a hash key of (nc_src_vp, name).  Each hash chain has its own spin lock.
 *
 * Negative entries may exist and correspond to resolved namecache
 * structures where nc_vp is NULL.  In a negative entry, NCF_WHITEOUT
 * will be set if the entry corresponds to a whited-out directory entry
 * (verses simply not finding the entry at all).   ncneglist is locked
 * with a global spinlock (ncspin).
 *
 * MPSAFE RULES:
 *
 * (1) A ncp must be referenced before it can be locked.
 *
 * (2) A ncp must be locked in order to modify it.
 *
 * (3) ncp locks are always ordered child -> parent.  That may seem
 *     backwards but forward scans use the hash table and thus can hold
 *     the parent unlocked when traversing downward.
 *
 *     This allows insert/rename/delete/dot-dot and other operations
 *     to use ncp->nc_parent links.
 *
 *     This also prevents a locked up e.g. NFS node from creating a
 *     chain reaction all the way back to the root vnode / namecache.
 *
 * (4) parent linkages require both the parent and child to be locked.
 */

/*
 * Structures associated with name cacheing.
 */
#define NCHHASH(hash)		(&nchashtbl[(hash) & nchash])
#define MINNEG			1024
#define MINPOS			1024
#define NCMOUNT_NUMCACHE	1009	/* prime number */

MALLOC_DEFINE(M_VFSCACHE, "vfscache", "VFS name cache entries");

LIST_HEAD(nchash_list, namecache);

struct nchash_head {
       struct nchash_list list;
       struct spinlock	spin;
};

struct ncmount_cache {
	struct spinlock	spin;
	struct namecache *ncp;
	struct mount *mp;
	int isneg;		/* if != 0 mp is originator and not target */
};

static struct nchash_head	*nchashtbl;
static struct namecache_list	ncneglist;
static struct spinlock		ncspin;
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
static int	ncvp_debug;
SYSCTL_INT(_debug, OID_AUTO, ncvp_debug, CTLFLAG_RW, &ncvp_debug, 0,
    "Namecache debug level (0-3)");

static u_long	nchash;			/* size of hash table */
SYSCTL_ULONG(_debug, OID_AUTO, nchash, CTLFLAG_RD, &nchash, 0,
    "Size of namecache hash table");

static int	ncnegflush = 10;	/* burst for negative flush */
SYSCTL_INT(_debug, OID_AUTO, ncnegflush, CTLFLAG_RW, &ncnegflush, 0,
    "Batch flush negative entries");

static int	ncposflush = 10;	/* burst for positive flush */
SYSCTL_INT(_debug, OID_AUTO, ncposflush, CTLFLAG_RW, &ncposflush, 0,
    "Batch flush positive entries");

static int	ncnegfactor = 16;	/* ratio of negative entries */
SYSCTL_INT(_debug, OID_AUTO, ncnegfactor, CTLFLAG_RW, &ncnegfactor, 0,
    "Ratio of namecache negative entries");

static int	nclockwarn;		/* warn on locked entries in ticks */
SYSCTL_INT(_debug, OID_AUTO, nclockwarn, CTLFLAG_RW, &nclockwarn, 0,
    "Warn on locked namecache entries in ticks");

static int	numdefered;		/* number of cache entries allocated */
SYSCTL_INT(_debug, OID_AUTO, numdefered, CTLFLAG_RD, &numdefered, 0,
    "Number of cache entries allocated");

static int	ncposlimit;		/* number of cache entries allocated */
SYSCTL_INT(_debug, OID_AUTO, ncposlimit, CTLFLAG_RW, &ncposlimit, 0,
    "Number of cache entries allocated");

static int	ncp_shared_lock_disable = 0;
SYSCTL_INT(_debug, OID_AUTO, ncp_shared_lock_disable, CTLFLAG_RW,
	   &ncp_shared_lock_disable, 0, "Disable shared namecache locks");

SYSCTL_INT(_debug, OID_AUTO, vnsize, CTLFLAG_RD, 0, sizeof(struct vnode),
    "sizeof(struct vnode)");
SYSCTL_INT(_debug, OID_AUTO, ncsize, CTLFLAG_RD, 0, sizeof(struct namecache),
    "sizeof(struct namecache)");

static int	ncmount_cache_enable = 1;
SYSCTL_INT(_debug, OID_AUTO, ncmount_cache_enable, CTLFLAG_RW,
	   &ncmount_cache_enable, 0, "mount point cache");
static long	ncmount_cache_hit;
SYSCTL_LONG(_debug, OID_AUTO, ncmount_cache_hit, CTLFLAG_RW,
	    &ncmount_cache_hit, 0, "mpcache hits");
static long	ncmount_cache_miss;
SYSCTL_LONG(_debug, OID_AUTO, ncmount_cache_miss, CTLFLAG_RW,
	    &ncmount_cache_miss, 0, "mpcache misses");
static long	ncmount_cache_overwrite;
SYSCTL_LONG(_debug, OID_AUTO, ncmount_cache_overwrite, CTLFLAG_RW,
	    &ncmount_cache_overwrite, 0, "mpcache entry overwrites");

static int cache_resolve_mp(struct mount *mp);
static struct vnode *cache_dvpref(struct namecache *ncp);
static void _cache_lock(struct namecache *ncp);
static void _cache_setunresolved(struct namecache *ncp);
static void _cache_cleanneg(int count);
static void _cache_cleanpos(int count);
static void _cache_cleandefered(void);
static void _cache_unlink(struct namecache *ncp);

/*
 * The new name cache statistics
 */
SYSCTL_NODE(_vfs, OID_AUTO, cache, CTLFLAG_RW, 0, "Name cache statistics");
static int numneg;
SYSCTL_INT(_vfs_cache, OID_AUTO, numneg, CTLFLAG_RD, &numneg, 0,
    "Number of negative namecache entries");
static int numcache;
SYSCTL_INT(_vfs_cache, OID_AUTO, numcache, CTLFLAG_RD, &numcache, 0,
    "Number of namecaches entries");
static u_long numcalls;
SYSCTL_ULONG(_vfs_cache, OID_AUTO, numcalls, CTLFLAG_RD, &numcalls, 0,
    "Number of namecache lookups");
static u_long numchecks;
SYSCTL_ULONG(_vfs_cache, OID_AUTO, numchecks, CTLFLAG_RD, &numchecks, 0,
    "Number of checked entries in namecache lookups");

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

static struct namecache *cache_zap(struct namecache *ncp, int nonblock);

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
static
void
_cache_lock(struct namecache *ncp)
{
	thread_t td;
	int didwarn;
	int begticks;
	int error;
	u_int count;

	KKASSERT(ncp->nc_refs != 0);
	didwarn = 0;
	begticks = 0;
	td = curthread;

	for (;;) {
		count = ncp->nc_lockstatus;
		cpu_ccfence();

		if ((count & ~(NC_EXLOCK_REQ|NC_SHLOCK_REQ)) == 0) {
			if (atomic_cmpset_int(&ncp->nc_lockstatus,
					      count, count + 1)) {
				/*
				 * The vp associated with a locked ncp must
				 * be held to prevent it from being recycled.
				 *
				 * WARNING!  If VRECLAIMED is set the vnode
				 * could already be in the middle of a recycle.
				 * Callers must use cache_vref() or
				 * cache_vget() on the locked ncp to
				 * validate the vp or set the cache entry
				 * to unresolved.
				 *
				 * NOTE! vhold() is allowed if we hold a
				 *	 lock on the ncp (which we do).
				 */
				ncp->nc_locktd = td;
				if (ncp->nc_vp)
					vhold(ncp->nc_vp);
				break;
			}
			/* cmpset failed */
			continue;
		}
		if (ncp->nc_locktd == td) {
			KKASSERT((count & NC_SHLOCK_FLAG) == 0);
			if (atomic_cmpset_int(&ncp->nc_lockstatus,
					      count, count + 1)) {
				break;
			}
			/* cmpset failed */
			continue;
		}
		tsleep_interlock(&ncp->nc_locktd, 0);
		if (atomic_cmpset_int(&ncp->nc_lockstatus, count,
				      count | NC_EXLOCK_REQ) == 0) {
			/* cmpset failed */
			continue;
		}
		if (begticks == 0)
			begticks = ticks;
		error = tsleep(&ncp->nc_locktd, PINTERLOCKED,
			       "clock", nclockwarn);
		if (error == EWOULDBLOCK) {
			if (didwarn == 0) {
				didwarn = ticks;
				kprintf("[diagnostic] cache_lock: "
					"blocked on %p %08x",
					ncp, count);
				kprintf(" \"%*.*s\"\n",
					ncp->nc_nlen, ncp->nc_nlen,
					ncp->nc_name);
			}
		}
		/* loop */
	}
	if (didwarn) {
		kprintf("[diagnostic] cache_lock: unblocked %*.*s after "
			"%d secs\n",
			ncp->nc_nlen, ncp->nc_nlen, ncp->nc_name,
			(int)(ticks + (hz / 2) - begticks) / hz);
	}
}

/*
 * The shared lock works similarly to the exclusive lock except
 * nc_locktd is left NULL and we need an interlock (VHOLD) to
 * prevent vhold() races, since the moment our cmpset_int succeeds
 * another cpu can come in and get its own shared lock.
 *
 * A critical section is needed to prevent interruption during the
 * VHOLD interlock.
 */
static
void
_cache_lock_shared(struct namecache *ncp)
{
	int didwarn;
	int error;
	u_int count;
	u_int optreq = NC_EXLOCK_REQ;

	KKASSERT(ncp->nc_refs != 0);
	didwarn = 0;

	for (;;) {
		count = ncp->nc_lockstatus;
		cpu_ccfence();

		if ((count & ~NC_SHLOCK_REQ) == 0) {
			crit_enter();
			if (atomic_cmpset_int(&ncp->nc_lockstatus,
				      count,
				      (count + 1) | NC_SHLOCK_FLAG |
						    NC_SHLOCK_VHOLD)) {
				/*
				 * The vp associated with a locked ncp must
				 * be held to prevent it from being recycled.
				 *
				 * WARNING!  If VRECLAIMED is set the vnode
				 * could already be in the middle of a recycle.
				 * Callers must use cache_vref() or
				 * cache_vget() on the locked ncp to
				 * validate the vp or set the cache entry
				 * to unresolved.
				 *
				 * NOTE! vhold() is allowed if we hold a
				 *	 lock on the ncp (which we do).
				 */
				if (ncp->nc_vp)
					vhold(ncp->nc_vp);
				atomic_clear_int(&ncp->nc_lockstatus,
						 NC_SHLOCK_VHOLD);
				crit_exit();
				break;
			}
			/* cmpset failed */
			crit_exit();
			continue;
		}

		/*
		 * If already held shared we can just bump the count, but
		 * only allow this if nobody is trying to get the lock
		 * exclusively.  If we are blocking too long ignore excl
		 * requests (which can race/deadlock us).
		 *
		 * VHOLD is a bit of a hack.  Even though we successfully
		 * added another shared ref, the cpu that got the first
		 * shared ref might not yet have held the vnode.
		 */
		if ((count & (optreq|NC_SHLOCK_FLAG)) == NC_SHLOCK_FLAG) {
			KKASSERT((count & ~(NC_EXLOCK_REQ |
					    NC_SHLOCK_REQ |
					    NC_SHLOCK_FLAG)) > 0);
			if (atomic_cmpset_int(&ncp->nc_lockstatus,
					      count, count + 1)) {
				while (ncp->nc_lockstatus & NC_SHLOCK_VHOLD)
					cpu_pause();
				break;
			}
			continue;
		}
		tsleep_interlock(ncp, 0);
		if (atomic_cmpset_int(&ncp->nc_lockstatus, count,
				      count | NC_SHLOCK_REQ) == 0) {
			/* cmpset failed */
			continue;
		}
		error = tsleep(ncp, PINTERLOCKED, "clocksh", nclockwarn);
		if (error == EWOULDBLOCK) {
			optreq = 0;
			if (didwarn == 0) {
				didwarn = ticks;
				kprintf("[diagnostic] cache_lock_shared: "
					"blocked on %p %08x",
					ncp, count);
				kprintf(" \"%*.*s\"\n",
					ncp->nc_nlen, ncp->nc_nlen,
					ncp->nc_name);
			}
		}
		/* loop */
	}
	if (didwarn) {
		kprintf("[diagnostic] cache_lock_shared: "
			"unblocked %*.*s after %d secs\n",
			ncp->nc_nlen, ncp->nc_nlen, ncp->nc_name,
			(int)(ticks - didwarn) / hz);
	}
}

/*
 * NOTE: nc_refs may be zero if the ncp is interlocked by circumstance,
 *	 such as the case where one of its children is locked.
 */
static
int
_cache_lock_nonblock(struct namecache *ncp)
{
	thread_t td;
	u_int count;

	td = curthread;

	for (;;) {
		count = ncp->nc_lockstatus;

		if ((count & ~(NC_EXLOCK_REQ|NC_SHLOCK_REQ)) == 0) {
			if (atomic_cmpset_int(&ncp->nc_lockstatus,
					      count, count + 1)) {
				/*
				 * The vp associated with a locked ncp must
				 * be held to prevent it from being recycled.
				 *
				 * WARNING!  If VRECLAIMED is set the vnode
				 * could already be in the middle of a recycle.
				 * Callers must use cache_vref() or
				 * cache_vget() on the locked ncp to
				 * validate the vp or set the cache entry
				 * to unresolved.
				 *
				 * NOTE! vhold() is allowed if we hold a
				 *	 lock on the ncp (which we do).
				 */
				ncp->nc_locktd = td;
				if (ncp->nc_vp)
					vhold(ncp->nc_vp);
				break;
			}
			/* cmpset failed */
			continue;
		}
		if (ncp->nc_locktd == td) {
			if (atomic_cmpset_int(&ncp->nc_lockstatus,
					      count, count + 1)) {
				break;
			}
			/* cmpset failed */
			continue;
		}
		return(EWOULDBLOCK);
	}
	return(0);
}

/*
 * The shared lock works similarly to the exclusive lock except
 * nc_locktd is left NULL and we need an interlock (VHOLD) to
 * prevent vhold() races, since the moment our cmpset_int succeeds
 * another cpu can come in and get its own shared lock.
 *
 * A critical section is needed to prevent interruption during the
 * VHOLD interlock.
 */
static
int
_cache_lock_shared_nonblock(struct namecache *ncp)
{
	u_int count;

	for (;;) {
		count = ncp->nc_lockstatus;

		if ((count & ~NC_SHLOCK_REQ) == 0) {
			crit_enter();
			if (atomic_cmpset_int(&ncp->nc_lockstatus,
				      count,
				      (count + 1) | NC_SHLOCK_FLAG |
						    NC_SHLOCK_VHOLD)) {
				/*
				 * The vp associated with a locked ncp must
				 * be held to prevent it from being recycled.
				 *
				 * WARNING!  If VRECLAIMED is set the vnode
				 * could already be in the middle of a recycle.
				 * Callers must use cache_vref() or
				 * cache_vget() on the locked ncp to
				 * validate the vp or set the cache entry
				 * to unresolved.
				 *
				 * NOTE! vhold() is allowed if we hold a
				 *	 lock on the ncp (which we do).
				 */
				if (ncp->nc_vp)
					vhold(ncp->nc_vp);
				atomic_clear_int(&ncp->nc_lockstatus,
						 NC_SHLOCK_VHOLD);
				crit_exit();
				break;
			}
			/* cmpset failed */
			crit_exit();
			continue;
		}

		/*
		 * If already held shared we can just bump the count, but
		 * only allow this if nobody is trying to get the lock
		 * exclusively.
		 *
		 * VHOLD is a bit of a hack.  Even though we successfully
		 * added another shared ref, the cpu that got the first
		 * shared ref might not yet have held the vnode.
		 */
		if ((count & (NC_EXLOCK_REQ|NC_SHLOCK_FLAG)) ==
		    NC_SHLOCK_FLAG) {
			KKASSERT((count & ~(NC_EXLOCK_REQ |
					    NC_SHLOCK_REQ |
					    NC_SHLOCK_FLAG)) > 0);
			if (atomic_cmpset_int(&ncp->nc_lockstatus,
					      count, count + 1)) {
				while (ncp->nc_lockstatus & NC_SHLOCK_VHOLD)
					cpu_pause();
				break;
			}
			continue;
		}
		return(EWOULDBLOCK);
	}
	return(0);
}

/*
 * Helper function
 *
 * NOTE: nc_refs can be 0 (degenerate case during _cache_drop).
 *
 *	 nc_locktd must be NULLed out prior to nc_lockstatus getting cleared.
 */
static
void
_cache_unlock(struct namecache *ncp)
{
	thread_t td __debugvar = curthread;
	u_int count;
	u_int ncount;
	struct vnode *dropvp;

	KKASSERT(ncp->nc_refs >= 0);
	KKASSERT((ncp->nc_lockstatus & ~(NC_EXLOCK_REQ|NC_SHLOCK_REQ)) > 0);
	KKASSERT((ncp->nc_lockstatus & NC_SHLOCK_FLAG) || ncp->nc_locktd == td);

	count = ncp->nc_lockstatus;
	cpu_ccfence();

	/*
	 * Clear nc_locktd prior to the atomic op (excl lock only)
	 */
	if ((count & ~(NC_EXLOCK_REQ|NC_SHLOCK_REQ)) == 1)
		ncp->nc_locktd = NULL;
	dropvp = NULL;

	for (;;) {
		if ((count &
		     ~(NC_EXLOCK_REQ|NC_SHLOCK_REQ|NC_SHLOCK_FLAG)) == 1) {
			dropvp = ncp->nc_vp;
			if (count & NC_EXLOCK_REQ)
				ncount = count & NC_SHLOCK_REQ; /* cnt->0 */
			else
				ncount = 0;

			if (atomic_cmpset_int(&ncp->nc_lockstatus,
					      count, ncount)) {
				if (count & NC_EXLOCK_REQ)
					wakeup(&ncp->nc_locktd);
				else if (count & NC_SHLOCK_REQ)
					wakeup(ncp);
				break;
			}
			dropvp = NULL;
		} else {
			KKASSERT((count & NC_SHLOCK_VHOLD) == 0);
			KKASSERT((count & ~(NC_EXLOCK_REQ |
					    NC_SHLOCK_REQ |
					    NC_SHLOCK_FLAG)) > 1);
			if (atomic_cmpset_int(&ncp->nc_lockstatus,
					      count, count - 1)) {
				break;
			}
		}
		count = ncp->nc_lockstatus;
		cpu_ccfence();
	}

	/*
	 * Don't actually drop the vp until we successfully clean out
	 * the lock, otherwise we may race another shared lock.
	 */
	if (dropvp)
		vdrop(dropvp);
}

static
int
_cache_lockstatus(struct namecache *ncp)
{
	if (ncp->nc_locktd == curthread)
		return(LK_EXCLUSIVE);
	if (ncp->nc_lockstatus & NC_SHLOCK_FLAG)
		return(LK_SHARED);
	return(-1);
}

/*
 * cache_hold() and cache_drop() prevent the premature deletion of a
 * namecache entry but do not prevent operations (such as zapping) on
 * that namecache entry.
 *
 * This routine may only be called from outside this source module if
 * nc_refs is already at least 1.
 *
 * This is a rare case where callers are allowed to hold a spinlock,
 * so we can't ourselves.
 */
static __inline
struct namecache *
_cache_hold(struct namecache *ncp)
{
	atomic_add_int(&ncp->nc_refs, 1);
	return(ncp);
}

/*
 * Drop a cache entry, taking care to deal with races.
 *
 * For potential 1->0 transitions we must hold the ncp lock to safely
 * test its flags.  An unresolved entry with no children must be zapped
 * to avoid leaks.
 *
 * The call to cache_zap() itself will handle all remaining races and
 * will decrement the ncp's refs regardless.  If we are resolved or
 * have children nc_refs can safely be dropped to 0 without having to
 * zap the entry.
 *
 * NOTE: cache_zap() will re-check nc_refs and nc_list in a MPSAFE fashion.
 *
 * NOTE: cache_zap() may return a non-NULL referenced parent which must
 *	 be dropped in a loop.
 */
static __inline
void
_cache_drop(struct namecache *ncp)
{
	int refs;

	while (ncp) {
		KKASSERT(ncp->nc_refs > 0);
		refs = ncp->nc_refs;

		if (refs == 1) {
			if (_cache_lock_nonblock(ncp) == 0) {
				ncp->nc_flag &= ~NCF_DEFEREDZAP;
				if ((ncp->nc_flag & NCF_UNRESOLVED) &&
				    TAILQ_EMPTY(&ncp->nc_list)) {
					ncp = cache_zap(ncp, 1);
					continue;
				}
				if (atomic_cmpset_int(&ncp->nc_refs, 1, 0)) {
					_cache_unlock(ncp);
					break;
				}
				_cache_unlock(ncp);
			}
		} else {
			if (atomic_cmpset_int(&ncp->nc_refs, refs, refs - 1))
				break;
		}
		cpu_pause();
	}
}

/*
 * Link a new namecache entry to its parent and to the hash table.  Be
 * careful to avoid races if vhold() blocks in the future.
 *
 * Both ncp and par must be referenced and locked.
 *
 * NOTE: The hash table spinlock is held during this call, we can't do
 *	 anything fancy.
 */
static void
_cache_link_parent(struct namecache *ncp, struct namecache *par,
		   struct nchash_head *nchpp)
{
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

	LIST_INSERT_HEAD(&nchpp->list, ncp, nc_hash);

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
 * Remove the parent and hash associations from a namecache structure.
 * If this is the last child of the parent the cache_drop(par) will
 * attempt to recursively zap the parent.
 *
 * ncp must be locked.  This routine will acquire a temporary lock on
 * the parent as wlel as the appropriate hash chain.
 */
static void
_cache_unlink_parent(struct namecache *ncp)
{
	struct namecache *par;
	struct vnode *dropvp;

	if ((par = ncp->nc_parent) != NULL) {
		KKASSERT(ncp->nc_parent == par);
		_cache_hold(par);
		_cache_lock(par);
		spin_lock(&ncp->nc_head->spin);
		LIST_REMOVE(ncp, nc_hash);
		TAILQ_REMOVE(&par->nc_list, ncp, nc_entry);
		dropvp = NULL;
		if (par->nc_vp && TAILQ_EMPTY(&par->nc_list))
			dropvp = par->nc_vp;
		spin_unlock(&ncp->nc_head->spin);
		ncp->nc_parent = NULL;
		ncp->nc_head = NULL;
		_cache_unlock(par);
		_cache_drop(par);

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
	_cache_lock(ncp);
	return(ncp);
}

/*
 * Can only be called for the case where the ncp has never been
 * associated with anything (so no spinlocks are needed).
 */
static void
_cache_free(struct namecache *ncp)
{
	KKASSERT(ncp->nc_refs == 1 && ncp->nc_lockstatus == 1);
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
 * Ref and deref a namecache structure.
 *
 * The caller must specify a stable ncp pointer, typically meaning the
 * ncp is already referenced but this can also occur indirectly through
 * e.g. holding a lock on a direct child.
 *
 * WARNING: Caller may hold an unrelated read spinlock, which means we can't
 *	    use read spinlocks here.
 *
 * MPSAFE if nch is
 */
struct nchandle *
cache_hold(struct nchandle *nch)
{
	_cache_hold(nch->ncp);
	atomic_add_int(&nch->mount->mnt_refs, 1);
	return(nch);
}

/*
 * Create a copy of a namecache handle for an already-referenced
 * entry.
 *
 * MPSAFE if nch is
 */
void
cache_copy(struct nchandle *nch, struct nchandle *target)
{
	*target = *nch;
	if (target->ncp)
		_cache_hold(target->ncp);
	atomic_add_int(&nch->mount->mnt_refs, 1);
}

/*
 * MPSAFE if nch is
 */
void
cache_changemount(struct nchandle *nch, struct mount *mp)
{
	atomic_add_int(&nch->mount->mnt_refs, -1);
	nch->mount = mp;
	atomic_add_int(&nch->mount->mnt_refs, 1);
}

void
cache_drop(struct nchandle *nch)
{
	atomic_add_int(&nch->mount->mnt_refs, -1);
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
 * Relock nch1 given an unlocked nch1 and a locked nch2.  The caller
 * is responsible for checking both for validity on return as they
 * may have become invalid.
 *
 * We have to deal with potential deadlocks here, just ping pong
 * the lock until we get it (we will always block somewhere when
 * looping so this is not cpu-intensive).
 *
 * which = 0	nch1 not locked, nch2 is locked
 * which = 1	nch1 is locked, nch2 is not locked
 */
void
cache_relock(struct nchandle *nch1, struct ucred *cred1,
	     struct nchandle *nch2, struct ucred *cred2)
{
	int which;

	which = 0;

	for (;;) {
		if (which == 0) {
			if (cache_lock_nonblock(nch1) == 0) {
				cache_resolve(nch1, cred1);
				break;
			}
			cache_unlock(nch2);
			cache_lock(nch1);
			cache_resolve(nch1, cred1);
			which = 1;
		} else {
			if (cache_lock_nonblock(nch2) == 0) {
				cache_resolve(nch2, cred2);
				break;
			}
			cache_unlock(nch1);
			cache_lock(nch2);
			cache_resolve(nch2, cred2);
			which = 0;
		}
	}
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
static int
_cache_lock_special(struct namecache *ncp)
{
	if (_cache_lock_nonblock(ncp) == 0) {
		if ((ncp->nc_lockstatus &
		     ~(NC_EXLOCK_REQ|NC_SHLOCK_REQ)) == 1) {
			if (ncp->nc_vp && (ncp->nc_vp->v_flag & VRECLAIMED))
				_cache_setunresolved(ncp);
			return(0);
		}
		_cache_unlock(ncp);
	}
	return(EWOULDBLOCK);
}

/*
 * This function tries to get a shared lock but will back-off to an exclusive
 * lock if:
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
 */
static int
_cache_lock_shared_special(struct namecache *ncp)
{
	/*
	 * Only honor a successful shared lock (returning 0) if there is
	 * no exclusive request pending and the vnode, if present, is not
	 * in a reclaimed state.
	 */
	if (_cache_lock_shared_nonblock(ncp) == 0) {
		if ((ncp->nc_lockstatus & NC_EXLOCK_REQ) == 0) {
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
	if (ncp->nc_locktd == curthread) {
		_cache_lock(ncp);
		return(0);
	}
	_cache_lock_shared(ncp);
	return(0);
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
	atomic_add_int(&target->mount->mnt_refs, 1);
}

void
cache_get_maybe_shared(struct nchandle *nch, struct nchandle *target, int excl)
{
	KKASSERT(nch->ncp->nc_refs > 0);
	target->mount = nch->mount;
	target->ncp = _cache_get_maybe_shared(nch->ncp, excl);
	atomic_add_int(&target->mount->mnt_refs, 1);
}

/*
 *
 */
static __inline
void
_cache_put(struct namecache *ncp)
{
	_cache_unlock(ncp);
	_cache_drop(ncp);
}

/*
 *
 */
void
cache_put(struct nchandle *nch)
{
	atomic_add_int(&nch->mount->mnt_refs, -1);
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
	KKASSERT(ncp->nc_flag & NCF_UNRESOLVED);
	KKASSERT(_cache_lockstatus(ncp) == LK_EXCLUSIVE);

	if (vp != NULL) {
		/*
		 * Any vp associated with an ncp which has children must
		 * be held.  Any vp associated with a locked ncp must be held.
		 */
		if (!TAILQ_EMPTY(&ncp->nc_list))
			vhold(vp);
		spin_lock(&vp->v_spin);
		ncp->nc_vp = vp;
		TAILQ_INSERT_HEAD(&vp->v_namecache, ncp, nc_vnode);
		spin_unlock(&vp->v_spin);
		if (ncp->nc_lockstatus & ~(NC_EXLOCK_REQ|NC_SHLOCK_REQ))
			vhold(vp);

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
		atomic_add_int(&numcache, 1);
		ncp->nc_error = 0;
		/* XXX: this is a hack to work-around the lack of a real pfs vfs
		 * implementation*/
		if (mp != NULL)
			if (strncmp(mp->mnt_stat.f_fstypename, "null", 5) == 0)
				vp->v_pfsmp = mp;
	} else {
		/*
		 * When creating a negative cache hit we set the
		 * namecache_gen.  A later resolve will clean out the
		 * negative cache hit if the mount point's namecache_gen
		 * has changed.  Used by devfs, could also be used by
		 * other remote FSs.
		 */
		ncp->nc_vp = NULL;
		spin_lock(&ncspin);
		TAILQ_INSERT_TAIL(&ncneglist, ncp, nc_vnode);
		++numneg;
		spin_unlock(&ncspin);
		ncp->nc_error = ENOENT;
		if (mp)
			VFS_NCPGEN_SET(mp, ncp);
	}
	ncp->nc_flag &= ~(NCF_UNRESOLVED | NCF_DEFEREDZAP);
}

/*
 *
 */
void
cache_setvp(struct nchandle *nch, struct vnode *vp)
{
	_cache_setvp(nch->mount, nch->ncp, vp);
}

/*
 *
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
			atomic_add_int(&numcache, -1);
			spin_lock(&vp->v_spin);
			ncp->nc_vp = NULL;
			TAILQ_REMOVE(&vp->v_namecache, ncp, nc_vnode);
			spin_unlock(&vp->v_spin);

			/*
			 * Any vp associated with an ncp with children is
			 * held by that ncp.  Any vp associated with a locked
			 * ncp is held by that ncp.  These conditions must be
			 * undone when the vp is cleared out from the ncp.
			 */
			if (!TAILQ_EMPTY(&ncp->nc_list))
				vdrop(vp);
			if (ncp->nc_lockstatus & ~(NC_EXLOCK_REQ|NC_SHLOCK_REQ))
				vdrop(vp);
		} else {
			spin_lock(&ncspin);
			TAILQ_REMOVE(&ncneglist, ncp, nc_vnode);
			--numneg;
			spin_unlock(&ncspin);
		}
		ncp->nc_flag &= ~(NCF_WHITEOUT|NCF_ISDIR|NCF_ISSYMLINK);
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

/*
 *
 */
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
 *
 */
void
cache_clrmountpt(struct nchandle *nch)
{
	int count;

	count = mountlist_scan(cache_clrmountpt_callback, nch,
			       MNTSCAN_FORWARD|MNTSCAN_NOBUSY);
	if (count == 0)
		nch->ncp->nc_flag &= ~NCF_ISMOUNTPT;
}

/*
 * Invalidate portions of the namecache topology given a starting entry.
 * The passed ncp is set to an unresolved state and:
 *
 * The passed ncp must be referencxed and locked.  The routine may unlock
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
		kprintf("Warning: deep namecache recursion at %s\n",
			ncp->nc_name);
		_cache_unlock(ncp);
		while ((ncp2 = track.resume_ncp) != NULL) {
			track.resume_ncp = NULL;
			_cache_lock(ncp2);
			_cache_inval_internal(ncp2, flags & ~CINV_DESTROY,
					     &track);
			_cache_put(ncp2);
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
	struct namecache *kid;
	struct namecache *nextkid;
	int rcnt = 0;

	KKASSERT(_cache_lockstatus(ncp) == LK_EXCLUSIVE);

	_cache_setunresolved(ncp);
	if (flags & CINV_DESTROY)
		ncp->nc_flag |= NCF_DESTROYED;
	if ((flags & CINV_CHILDREN) && 
	    (kid = TAILQ_FIRST(&ncp->nc_list)) != NULL
	) {
		_cache_hold(kid);
		if (++track->depth > MAX_RECURSION_DEPTH) {
			track->resume_ncp = ncp;
			_cache_hold(ncp);
			++rcnt;
		}
		_cache_unlock(ncp);
		while (kid) {
			if (track->resume_ncp) {
				_cache_drop(kid);
				break;
			}
			if ((nextkid = TAILQ_NEXT(kid, nc_entry)) != NULL)
				_cache_hold(nextkid);
			if ((kid->nc_flag & NCF_UNRESOLVED) == 0 ||
			    TAILQ_FIRST(&kid->nc_list)
			) {
				_cache_lock(kid);
				rcnt += _cache_inval_internal(kid, flags & ~CINV_DESTROY, track);
				_cache_unlock(kid);
			}
			_cache_drop(kid);
			kid = nextkid;
		}
		--track->depth;
		_cache_lock(ncp);
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
 * The source ncp has been renamed to the target ncp.  Both fncp and tncp
 * must be locked.  The target ncp is destroyed (as a normal rename-over
 * would destroy the target file or directory).
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
	_cache_hold(tncp_par);
	_cache_lock(tncp_par);

	/*
	 * Rename fncp (relink)
	 */
	hash = fnv_32_buf(fncp->nc_name, fncp->nc_nlen, FNV1_32_INIT);
	hash = fnv_32_buf(&tncp_par, sizeof(tncp_par), hash);
	nchpp = NCHHASH(hash);

	spin_lock(&nchpp->spin);
	_cache_link_parent(fncp, tncp_par, nchpp);
	spin_unlock(&nchpp->spin);

	_cache_put(tncp_par);

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

	/*
	 * Attempt to trigger a deactivation.  Set VAUX_FINALIZE to
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
 * Similar to cache_vget() but only acquires a ref on the vnode.
 *
 * NOTE: The passed-in ncp must be locked exclusively if it is initially
 *	 unresolved.  If a reclaim race occurs the passed-in ncp will be
 *	 relocked exclusively before being re-resolved.
 */
int
cache_vref(struct nchandle *nch, struct ucred *cred, struct vnode **vpp)
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
static struct vnode *
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
		kprintf("inefficient_scan: directory iosize %ld "
			"vattr fileid = %lld\n",
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
 * Zap a namecache entry.  The ncp is unconditionally set to an unresolved
 * state, which disassociates it from its vnode or ncneglist.
 *
 * Then, if there are no additional references to the ncp and no children,
 * the ncp is removed from the topology and destroyed.
 *
 * References and/or children may exist if the ncp is in the middle of the
 * topology, preventing the ncp from being destroyed.
 *
 * This function must be called with the ncp held and locked and will unlock
 * and drop it during zapping.
 *
 * If nonblock is non-zero and the parent ncp cannot be locked we give up.
 * This case can occur in the cache_drop() path.
 *
 * This function may returned a held (but NOT locked) parent node which the
 * caller must drop.  We do this so _cache_drop() can loop, to avoid
 * blowing out the kernel stack.
 *
 * WARNING!  For MPSAFE operation this routine must acquire up to three
 *	     spin locks to be able to safely test nc_refs.  Lock order is
 *	     very important.
 *
 *	     hash spinlock if on hash list
 *	     parent spinlock if child of parent
 *	     (the ncp is unresolved so there is no vnode association)
 */
static struct namecache *
cache_zap(struct namecache *ncp, int nonblock)
{
	struct namecache *par;
	struct vnode *dropvp;
	int refs;

	/*
	 * Disassociate the vnode or negative cache ref and set NCF_UNRESOLVED.
	 */
	_cache_setunresolved(ncp);

	/*
	 * Try to scrap the entry and possibly tail-recurse on its parent.
	 * We only scrap unref'd (other then our ref) unresolved entries,
	 * we do not scrap 'live' entries.
	 *
	 * Note that once the spinlocks are acquired if nc_refs == 1 no
	 * other references are possible.  If it isn't, however, we have
	 * to decrement but also be sure to avoid a 1->0 transition.
	 */
	KKASSERT(ncp->nc_flag & NCF_UNRESOLVED);
	KKASSERT(ncp->nc_refs > 0);

	/*
	 * Acquire locks.  Note that the parent can't go away while we hold
	 * a child locked.
	 */
	if ((par = ncp->nc_parent) != NULL) {
		if (nonblock) {
			for (;;) {
				if (_cache_lock_nonblock(par) == 0)
					break;
				refs = ncp->nc_refs;
				ncp->nc_flag |= NCF_DEFEREDZAP;
				++numdefered;	/* MP race ok */
				if (atomic_cmpset_int(&ncp->nc_refs,
						      refs, refs - 1)) {
					_cache_unlock(ncp);
					return(NULL);
				}
				cpu_pause();
			}
			_cache_hold(par);
		} else {
			_cache_hold(par);
			_cache_lock(par);
		}
		spin_lock(&ncp->nc_head->spin);
	}

	/*
	 * If someone other then us has a ref or we have children
	 * we cannot zap the entry.  The 1->0 transition and any
	 * further list operation is protected by the spinlocks
	 * we have acquired but other transitions are not.
	 */
	for (;;) {
		refs = ncp->nc_refs;
		if (refs == 1 && TAILQ_EMPTY(&ncp->nc_list))
			break;
		if (atomic_cmpset_int(&ncp->nc_refs, refs, refs - 1)) {
			if (par) {
				spin_unlock(&ncp->nc_head->spin);
				_cache_put(par);
			}
			_cache_unlock(ncp);
			return(NULL);
		}
		cpu_pause();
	}

	/*
	 * We are the only ref and with the spinlocks held no further
	 * refs can be acquired by others.
	 *
	 * Remove us from the hash list and parent list.  We have to
	 * drop a ref on the parent's vp if the parent's list becomes
	 * empty.
	 */
	dropvp = NULL;
	if (par) {
		struct nchash_head *nchpp = ncp->nc_head;

		KKASSERT(nchpp != NULL);
		LIST_REMOVE(ncp, nc_hash);
		TAILQ_REMOVE(&par->nc_list, ncp, nc_entry);
		if (par->nc_vp && TAILQ_EMPTY(&par->nc_list))
			dropvp = par->nc_vp;
		ncp->nc_head = NULL;
		ncp->nc_parent = NULL;
		spin_unlock(&nchpp->spin);
		_cache_unlock(par);
	} else {
		KKASSERT(ncp->nc_head == NULL);
	}

	/*
	 * ncp should not have picked up any refs.  Physically
	 * destroy the ncp.
	 */
	KKASSERT(ncp->nc_refs == 1);
	/* _cache_unlock(ncp) not required */
	ncp->nc_refs = -1;	/* safety */
	if (ncp->nc_name)
		kfree(ncp->nc_name, M_VFSCACHE);
	kfree(ncp, M_VFSCACHE);

	/*
	 * Delayed drop (we had to release our spinlocks)
	 *
	 * The refed parent (if not  NULL) must be dropped.  The
	 * caller is responsible for looping.
	 */
	if (dropvp)
		vdrop(dropvp);
	return(par);
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
	int poslimit;
	int neglimit = desiredvnodes / ncnegfactor;
	int xnumcache = numcache;

	if (critpath == 0)
		neglimit = neglimit * 8 / 10;

	/*
	 * Don't cache too many negative hits.  We use hysteresis to reduce
	 * the impact on the critical path.
	 */
	switch(neg_cache_hysteresis_state[critpath]) {
	case CHI_LOW:
		if (numneg > MINNEG && numneg > neglimit) {
			if (critpath)
				_cache_cleanneg(ncnegflush);
			else
				_cache_cleanneg(ncnegflush +
						numneg - neglimit);
			neg_cache_hysteresis_state[critpath] = CHI_HIGH;
		}
		break;
	case CHI_HIGH:
		if (numneg > MINNEG * 9 / 10 && 
		    numneg * 9 / 10 > neglimit
		) {
			if (critpath)
				_cache_cleanneg(ncnegflush);
			else
				_cache_cleanneg(ncnegflush +
						numneg * 9 / 10 - neglimit);
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
		poslimit = desiredvnodes * 2;
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
	 * Clean out dangling defered-zap ncps which could not
	 * be cleanly dropped if too many build up.  Note
	 * that numdefered is not an exact number as such ncps
	 * can be reused and the counter is not handled in a MP
	 * safe manner by design.
	 */
	if (numdefered > neglimit) {
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
	struct nchash_head *nchpp;
	struct mount *mp;
	u_int32_t hash;
	globaldata_t gd;
	int par_locked;

	numcalls++;
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
	nchpp = NCHHASH(hash);
restart:
	if (new_ncp)
		spin_lock(&nchpp->spin);
	else
		spin_lock_shared(&nchpp->spin);

	LIST_FOREACH(ncp, &nchpp->list, nc_hash) {
		numchecks++;

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
			if (new_ncp)
				spin_unlock(&nchpp->spin);
			else
				spin_unlock_shared(&nchpp->spin);
			if (par_locked) {
				_cache_unlock(par_nch->ncp);
				par_locked = 0;
			}
			if (_cache_lock_special(ncp) == 0) {
				_cache_auto_unresolve(mp, ncp);
				if (new_ncp)
					_cache_free(new_ncp);
				goto found;
			}
			_cache_get(ncp);
			_cache_put(ncp);
			_cache_drop(ncp);
			goto restart;
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
		spin_unlock_shared(&nchpp->spin);
		new_ncp = cache_alloc(nlc->nlc_namelen);
		if (nlc->nlc_namelen) {
			bcopy(nlc->nlc_nameptr, new_ncp->nc_name,
			      nlc->nlc_namelen);
			new_ncp->nc_name[nlc->nlc_namelen] = 0;
		}
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
	 * WARNING!  We still hold the spinlock.  We have to set the hash
	 *	     table entry atomically.
	 */
	ncp = new_ncp;
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
	atomic_add_int(&nch.mount->mnt_refs, 1);
	return(nch);
}

/*
 * Attempt to lookup a namecache entry and return with a shared namecache
 * lock.
 */
int
cache_nlookup_maybe_shared(struct nchandle *par_nch, struct nlcomponent *nlc,
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

	numcalls++;
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

	LIST_FOREACH(ncp, &nchpp->list, nc_hash) {
		numchecks++;

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
				if ((ncp->nc_flag & NCF_UNRESOLVED) == 0 &&
				    (ncp->nc_flag & NCF_DESTROYED) == 0 &&
				    _cache_auto_unresolve_test(mp, ncp) == 0) {
					goto found;
				}
				_cache_unlock(ncp);
			}
			_cache_drop(ncp);
			spin_lock_shared(&nchpp->spin);
			break;
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
	atomic_add_int(&res_nch->mount->mnt_refs, 1);

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

	numcalls++;
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
	LIST_FOREACH(ncp, &nchpp->list, nc_hash) {
		numchecks++;

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
	 * WARNING!  We still hold the spinlock.  We have to set the hash
	 *	     table entry atomically.
	 */
	ncp = new_ncp;
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
	atomic_add_int(&nch.mount->mnt_refs, 1);
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
 */
struct findmount_info {
	struct mount *result;
	struct mount *nch_mount;
	struct namecache *nch_ncp;
};

static
struct ncmount_cache *
ncmount_cache_lookup(struct mount *mp, struct namecache *ncp)
{
	int hash;

	hash = ((int)(intptr_t)mp / sizeof(*mp)) ^
	       ((int)(intptr_t)ncp / sizeof(*ncp));
	hash = (hash & 0x7FFFFFFF) % NCMOUNT_NUMCACHE;
	return (&ncmount_cache[hash]);
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
	    atomic_add_int(&mp->mnt_refs, 1);
	    return(-1);
	}
	return(0);
}

struct mount *
cache_findmount(struct nchandle *nch)
{
	struct findmount_info info;
	struct ncmount_cache *ncc;
	struct mount *mp;

	/*
	 * Fast
	 */
	if (ncmount_cache_enable == 0) {
		ncc = NULL;
		goto skip;
	}
	ncc = ncmount_cache_lookup(nch->mount, nch->ncp);
	if (ncc->ncp == nch->ncp) {
		spin_lock_shared(&ncc->spin);
		if (ncc->isneg == 0 &&
		    ncc->ncp == nch->ncp && (mp = ncc->mp) != NULL) {
			if (mp->mnt_ncmounton.mount == nch->mount &&
			    mp->mnt_ncmounton.ncp == nch->ncp) {
				/*
				 * Cache hit (positive)
				 */
				atomic_add_int(&mp->mnt_refs, 1);
				spin_unlock_shared(&ncc->spin);
				++ncmount_cache_hit;
				return(mp);
			}
			/* else cache miss */
		}
		if (ncc->isneg &&
		    ncc->ncp == nch->ncp && ncc->mp == nch->mount) {
			/*
			 * Cache hit (negative)
			 */
			spin_unlock_shared(&ncc->spin);
			++ncmount_cache_hit;
			return(NULL);
		}
		spin_unlock_shared(&ncc->spin);
	}
skip:

	/*
	 * Slow
	 */
	info.result = NULL;
	info.nch_mount = nch->mount;
	info.nch_ncp = nch->ncp;
	mountlist_scan(cache_findmount_callback, &info,
			       MNTSCAN_FORWARD|MNTSCAN_NOBUSY);

	/*
	 * Cache the result.
	 *
	 * Negative lookups: We cache the originating {ncp,mp}. (mp) is
	 *		     only used for pointer comparisons and is not
	 *		     referenced (otherwise there would be dangling
	 *		     refs).
	 *
	 * Positive lookups: We cache the originating {ncp} and the target
	 *		     (mp).  (mp) is referenced.
	 *
	 * Indeterminant:    If the match is undergoing an unmount we do
	 *		     not cache it to avoid racing cache_unmounting(),
	 *		     but still return the match.
	 */
	if (ncc) {
		spin_lock(&ncc->spin);
		if (info.result == NULL) {
			if (ncc->isneg == 0 && ncc->mp)
				atomic_add_int(&ncc->mp->mnt_refs, -1);
			ncc->ncp = nch->ncp;
			ncc->mp = nch->mount;
			ncc->isneg = 1;
			spin_unlock(&ncc->spin);
			++ncmount_cache_overwrite;
		} else if ((info.result->mnt_kern_flag & MNTK_UNMOUNT) == 0) {
			if (ncc->isneg == 0 && ncc->mp)
				atomic_add_int(&ncc->mp->mnt_refs, -1);
			atomic_add_int(&info.result->mnt_refs, 1);
			ncc->ncp = nch->ncp;
			ncc->mp = info.result;
			ncc->isneg = 0;
			spin_unlock(&ncc->spin);
			++ncmount_cache_overwrite;
		} else {
			spin_unlock(&ncc->spin);
		}
		++ncmount_cache_miss;
	}
	return(info.result);
}

void
cache_dropmount(struct mount *mp)
{
	atomic_add_int(&mp->mnt_refs, -1);
}

void
cache_ismounting(struct mount *mp)
{
	struct nchandle *nch = &mp->mnt_ncmounton;
	struct ncmount_cache *ncc;

	ncc = ncmount_cache_lookup(nch->mount, nch->ncp);
	if (ncc->isneg &&
	    ncc->ncp == nch->ncp && ncc->mp == nch->mount) {
		spin_lock(&ncc->spin);
		if (ncc->isneg &&
		    ncc->ncp == nch->ncp && ncc->mp == nch->mount) {
			ncc->ncp = NULL;
			ncc->mp = NULL;
		}
		spin_unlock(&ncc->spin);
	}
}

void
cache_unmounting(struct mount *mp)
{
	struct nchandle *nch = &mp->mnt_ncmounton;
	struct ncmount_cache *ncc;

	ncc = ncmount_cache_lookup(nch->mount, nch->ncp);
	if (ncc->isneg == 0 &&
	    ncc->ncp == nch->ncp && ncc->mp == mp) {
		spin_lock(&ncc->spin);
		if (ncc->isneg == 0 &&
		    ncc->ncp == nch->ncp && ncc->mp == mp) {
			atomic_add_int(&mp->mnt_refs, -1);
			ncc->ncp = NULL;
			ncc->mp = NULL;
		}
		spin_unlock(&ncc->spin);
	}
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
	if (ncp->nc_flag & NCF_DESTROYED) {
		kprintf("Warning: cache_resolve: ncp '%s' was unlinked\n",
			ncp->nc_name);
		return(EINVAL);
	}

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
		kprintf("[diagnostic] cache_resolve: had to recurse on %*.*s\n",
			par->nc_nlen, par->nc_nlen, par->nc_name);
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
			kprintf("[diagnostic] cache_resolve: raced on %*.*s\n", par->nc_nlen, par->nc_nlen, par->nc_name);
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
_cache_cleanneg(int count)
{
	struct namecache *ncp;

	/*
	 * Attempt to clean out the specified number of negative cache
	 * entries.
	 */
	while (count) {
		spin_lock(&ncspin);
		ncp = TAILQ_FIRST(&ncneglist);
		if (ncp == NULL) {
			spin_unlock(&ncspin);
			break;
		}
		TAILQ_REMOVE(&ncneglist, ncp, nc_vnode);
		TAILQ_INSERT_TAIL(&ncneglist, ncp, nc_vnode);
		_cache_hold(ncp);
		spin_unlock(&ncspin);

		/*
		 * This can race, so we must re-check that the ncp
		 * is on the ncneglist after successfully locking it.
		 */
		if (_cache_lock_special(ncp) == 0) {
			if (ncp->nc_vp == NULL &&
			    (ncp->nc_flag & NCF_UNRESOLVED) == 0) {
				ncp = cache_zap(ncp, 1);
				if (ncp)
					_cache_drop(ncp);
			} else {
				kprintf("cache_cleanneg: race avoided\n");
				_cache_unlock(ncp);
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
_cache_cleanpos(int count)
{
	static volatile int rover;
	struct nchash_head *nchpp;
	struct namecache *ncp;
	int rover_copy;

	/*
	 * Attempt to clean out the specified number of negative cache
	 * entries.
	 */
	while (count) {
		rover_copy = ++rover;	/* MPSAFEENOUGH */
		cpu_ccfence();
		nchpp = NCHHASH(rover_copy);

		spin_lock_shared(&nchpp->spin);
		ncp = LIST_FIRST(&nchpp->list);
		while (ncp && (ncp->nc_flag & NCF_DESTROYED))
			ncp = LIST_NEXT(ncp, nc_hash);
		if (ncp)
			_cache_hold(ncp);
		spin_unlock_shared(&nchpp->spin);

		if (ncp) {
			if (_cache_lock_special(ncp) == 0) {
				ncp = cache_zap(ncp, 1);
				if (ncp)
					_cache_drop(ncp);
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

	numdefered = 0;
	bzero(&dummy, sizeof(dummy));
	dummy.nc_flag = NCF_DESTROYED;
	dummy.nc_refs = 1;

	for (i = 0; i <= nchash; ++i) {
		nchpp = &nchashtbl[i];

		spin_lock(&nchpp->spin);
		LIST_INSERT_HEAD(&nchpp->list, &dummy, nc_hash);
		ncp = &dummy;
		while ((ncp = LIST_NEXT(ncp, nc_hash)) != NULL) {
			if ((ncp->nc_flag & NCF_DEFEREDZAP) == 0)
				continue;
			LIST_REMOVE(&dummy, nc_hash);
			LIST_INSERT_AFTER(ncp, &dummy, nc_hash);
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
		LIST_REMOVE(&dummy, nc_hash);
		spin_unlock(&nchpp->spin);
	}
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
	spin_init(&ncspin);
	nchashtbl = hashinit_ext(desiredvnodes / 2,
				 sizeof(struct nchash_head),
				 M_VFSCACHE, &nchash);
	for (i = 0; i <= (int)nchash; ++i) {
		LIST_INIT(&nchashtbl[i].list);
		spin_init(&nchashtbl[i].spin);
	}
	for (i = 0; i < NCMOUNT_NUMCACHE; ++i)
		spin_init(&ncmount_cache[i].spin);
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
	atomic_add_int(&mp->mnt_refs, 1);
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

/*
 * Flush all entries referencing a particular filesystem.
 *
 * Since we need to check it anyway, we will flush all the invalid
 * entries at the same time.
 */
#if 0

void
cache_purgevfs(struct mount *mp)
{
	struct nchash_head *nchpp;
	struct namecache *ncp, *nnp;

	/*
	 * Scan hash tables for applicable entries.
	 */
	for (nchpp = &nchashtbl[nchash]; nchpp >= nchashtbl; nchpp--) {
		spin_lock_wr(&nchpp->spin); XXX
		ncp = LIST_FIRST(&nchpp->list);
		if (ncp)
			_cache_hold(ncp);
		while (ncp) {
			nnp = LIST_NEXT(ncp, nc_hash);
			if (nnp)
				_cache_hold(nnp);
			if (ncp->nc_mount == mp) {
				_cache_lock(ncp);
				ncp = cache_zap(ncp, 0);
				if (ncp)
					_cache_drop(ncp);
			} else {
				_cache_drop(ncp);
			}
			ncp = nnp;
		}
		spin_unlock_wr(&nchpp->spin); XXX
	}
}

#endif

static int disablecwd;
SYSCTL_INT(_debug, OID_AUTO, disablecwd, CTLFLAG_RW, &disablecwd, 0,
    "Disable getcwd");

static u_long numcwdcalls;
SYSCTL_ULONG(_vfs_cache, OID_AUTO, numcwdcalls, CTLFLAG_RD, &numcwdcalls, 0,
    "Number of current directory resolution calls");
static u_long numcwdfailnf;
SYSCTL_ULONG(_vfs_cache, OID_AUTO, numcwdfailnf, CTLFLAG_RD, &numcwdfailnf, 0,
    "Number of current directory failures due to lack of file");
static u_long numcwdfailsz;
SYSCTL_ULONG(_vfs_cache, OID_AUTO, numcwdfailsz, CTLFLAG_RD, &numcwdfailsz, 0,
    "Number of current directory failures due to large result");
static u_long numcwdfound;
SYSCTL_ULONG(_vfs_cache, OID_AUTO, numcwdfound, CTLFLAG_RD, &numcwdfound, 0,
    "Number of current directory resolution successes");

/*
 * MPALMOSTSAFE
 */
int
sys___getcwd(struct __getcwd_args *uap)
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

	numcwdcalls++;
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
				numcwdfailsz++;
				*error = ERANGE;
				bp = NULL;
				goto done;
			}
			*--bp = ncp->nc_name[i];
		}
		if (bp == buf) {
			numcwdfailsz++;
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
		numcwdfailnf++;
		*error = ENOENT;
		bp = NULL;
		goto done;
	}
	if (!slash_prefixed) {
		if (bp == buf) {
			numcwdfailsz++;
			*error = ERANGE;
			bp = NULL;
			goto done;
		}
		*--bp = '/';
	}
	numcwdfound++;
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
static int disablefullpath;
SYSCTL_INT(_debug, OID_AUTO, disablefullpath, CTLFLAG_RW,
    &disablefullpath, 0,
    "Disable fullpath lookups");

static u_int numfullpathcalls;
SYSCTL_UINT(_vfs_cache, OID_AUTO, numfullpathcalls, CTLFLAG_RD,
    &numfullpathcalls, 0,
    "Number of full path resolutions in progress");
static u_int numfullpathfailnf;
SYSCTL_UINT(_vfs_cache, OID_AUTO, numfullpathfailnf, CTLFLAG_RD,
    &numfullpathfailnf, 0,
    "Number of full path resolution failures due to lack of file");
static u_int numfullpathfailsz;
SYSCTL_UINT(_vfs_cache, OID_AUTO, numfullpathfailsz, CTLFLAG_RD,
    &numfullpathfailsz, 0,
    "Number of full path resolution failures due to insufficient memory");
static u_int numfullpathfound;
SYSCTL_UINT(_vfs_cache, OID_AUTO, numfullpathfound, CTLFLAG_RD,
    &numfullpathfound, 0,
    "Number of full path resolution successes");

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

	atomic_add_int(&numfullpathcalls, -1);

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
				numfullpathfailsz++;
				kfree(buf, M_TEMP);
				error = ENOMEM;
				goto done;
			}
			*--bp = ncp->nc_name[i];
		}
		if (bp == buf) {
			numfullpathfailsz++;
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
			_cache_lock(ncp);
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
		numfullpathfailnf++;
		kfree(buf, M_TEMP);
		error = ENOENT;
		goto done;
	}

	if (!slash_prefixed) {
		if (bp == buf) {
			numfullpathfailsz++;
			kfree(buf, M_TEMP);
			error = ENOMEM;
			goto done;
		}
		*--bp = '/';
	}
	numfullpathfound++;
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
	atomic_add_int(&numfullpathcalls, 1);
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

	atomic_add_int(&numfullpathcalls, -1);
	nch.ncp = ncp;
	nch.mount = vn->v_mount;
	error = cache_fullpath(p, &nch, NULL, retbuf, freebuf, guess);
	_cache_drop(ncp);
	return (error);
}
