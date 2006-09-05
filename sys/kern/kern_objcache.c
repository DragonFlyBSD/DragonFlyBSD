/*
 * Copyright (c) 2005 Jeffrey M. Hsu.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Jeffrey M. Hsu.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
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
 * $DragonFly: src/sys/kern/kern_objcache.c,v 1.10 2006/09/05 00:55:45 dillon Exp $
 */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/callout.h>
#include <sys/globaldata.h>
#include <sys/malloc.h>
#include <sys/queue.h>
#include <sys/objcache.h>
#include <sys/thread.h>
#include <sys/thread2.h>

static MALLOC_DEFINE(M_OBJCACHE, "objcache", "Object Cache");
static MALLOC_DEFINE(M_OBJMAG, "objcache magazine", "Object Cache Magazine");

#define	INITIAL_MAG_CAPACITY	256

struct magazine {
	int			 rounds;
	int			 capacity;
	SLIST_ENTRY(magazine)	 nextmagazine;
	void			*objects[];
};

SLIST_HEAD(magazinelist, magazine);

/*
 * per-cluster cache of magazines
 * All fields in this structure are protected by the token.
 */
struct magazinedepot {
	/*
	 * The per-cpu object caches only exchanges completely full or
	 * completely empty magazines with the depot layer, so only have
	 * to cache these two types of magazines.
	 */
	struct magazinelist	fullmagazines;
	struct magazinelist	emptymagazines;
	int			magcapacity;

	/* protect this structure */
	struct lwkt_token	token;

	/* magazines not yet allocated towards limit */
	int			unallocated_objects;

	/* infrequently used fields */
	int			waiting;	/* waiting for another cpu to
						 * return a full magazine to
						 * the depot */
	int			contested;	/* depot contention count */
};

/*
 * per-cpu object cache
 * All fields in this structure are protected by crit_enter().
 */
struct percpu_objcache {
	struct magazine	*loaded_magazine;	/* active magazine */
	struct magazine	*previous_magazine;	/* backup magazine */

	/* statistics */
	int		gets_cumulative;	/* total calls to get */
	int		gets_null;		/* objcache_get returned NULL */
	int		puts_cumulative;	/* total calls to put */
	int		puts_othercluster;	/* returned to other cluster */

	/* infrequently used fields */
	int		waiting;	/* waiting for a thread on this cpu to
					 * return an obj to the per-cpu cache */
};

/* only until we have NUMA cluster topology information XXX */
#define MAXCLUSTERS 1
#define myclusterid 0
#define CLUSTER_OF(obj) 0

/*
 * Two-level object cache consisting of NUMA cluster-level depots of
 * fully loaded or completely empty magazines and cpu-level caches of
 * individual objects.
 */
struct objcache {
	char			*name;

	/* object constructor and destructor from blank storage */
	objcache_ctor_fn	*ctor;
	objcache_dtor_fn	*dtor;
	void			*private;

	/* interface to underlying allocator */
	objcache_alloc_fn	*alloc;
	objcache_free_fn	*free;
	void			*allocator_args;

	SLIST_ENTRY(objcache)	oc_next;

	/* NUMA-cluster level caches */
	struct magazinedepot	depot[MAXCLUSTERS];

	struct percpu_objcache	cache_percpu[];		/* per-cpu caches */
};

static struct lwkt_token objcachelist_token;
static SLIST_HEAD(objcachelist, objcache) allobjcaches;

static struct magazine *
mag_alloc(int capacity)
{
	struct magazine *mag;

	mag = malloc(__offsetof(struct magazine, objects[capacity]),
			M_OBJMAG, M_INTWAIT | M_ZERO);
	mag->capacity = capacity;
	mag->rounds = 0;
	return (mag);
}

/*
 * Create an object cache.
 */
struct objcache *
objcache_create(const char *name, int cluster_limit, int mag_capacity,
		objcache_ctor_fn *ctor, objcache_dtor_fn *dtor, void *private,
		objcache_alloc_fn *alloc, objcache_free_fn *free,
		void *allocator_args)
{
	struct objcache *oc;
	struct magazinedepot *depot;
	lwkt_tokref olock;
	int cpuid;

	/* allocate object cache structure */
	oc = malloc(__offsetof(struct objcache, cache_percpu[ncpus]),
		    M_OBJCACHE, M_WAITOK | M_ZERO);
	oc->name = kstrdup(name, M_TEMP);
	oc->ctor = ctor;
	oc->dtor = dtor;
	oc->private = private;
	oc->free = free;
	oc->allocator_args = allocator_args;

	/* initialize depots */
	depot = &oc->depot[0];

	lwkt_token_init(&depot->token);
	SLIST_INIT(&depot->fullmagazines);
	SLIST_INIT(&depot->emptymagazines);

	if (mag_capacity == 0)
		mag_capacity = INITIAL_MAG_CAPACITY;
	depot->magcapacity = mag_capacity;

	/*
	 * The cluster_limit must be sufficient to have three magazines per
	 * cpu.
	 */
	if (cluster_limit == 0) {
		depot->unallocated_objects = -1;
	} else {
		if (cluster_limit < mag_capacity * ncpus * 3)
			cluster_limit = mag_capacity * ncpus * 3;
		depot->unallocated_objects = cluster_limit;
	}
	oc->alloc = alloc;

	/* initialize per-cpu caches */
	for (cpuid = 0; cpuid < ncpus; cpuid++) {
		struct percpu_objcache *cache_percpu = &oc->cache_percpu[cpuid];

		cache_percpu->loaded_magazine = mag_alloc(mag_capacity);
		cache_percpu->previous_magazine = mag_alloc(mag_capacity);
	}
	lwkt_gettoken(&olock, &objcachelist_token);
	SLIST_INSERT_HEAD(&allobjcaches, oc, oc_next);
	lwkt_reltoken(&olock);

	return (oc);
}

struct objcache *
objcache_create_simple(malloc_type_t mtype, size_t objsize)
{
	struct objcache_malloc_args *margs;
	struct objcache *oc;

	margs = kmalloc(sizeof(*margs), M_OBJCACHE, M_WAITOK|M_ZERO);
	margs->objsize = objsize;
	margs->mtype = mtype;
	oc = objcache_create(mtype->ks_shortdesc, 0, 0,
			     null_ctor, null_dtor, NULL,
			     objcache_malloc_alloc, objcache_malloc_free,
			     margs);
	return (oc);
}

#define MAGAZINE_EMPTY(mag)	(mag->rounds == 0)
#define MAGAZINE_NOTEMPTY(mag)	(mag->rounds != 0)
#define MAGAZINE_FULL(mag)	(mag->rounds == mag->capacity)

#define	swap(x, y)	({ struct magazine *t = x; x = y; y = t; })

/*
 * Get an object from the object cache.
 *
 * WARNING!  ocflags are only used when we have to go to the underlying
 * allocator, so we cannot depend on flags such as M_ZERO.
 */
void *
objcache_get(struct objcache *oc, int ocflags)
{
	struct percpu_objcache *cpucache = &oc->cache_percpu[mycpuid];
	struct magazine *loadedmag;
	struct magazine *emptymag;
	void *obj;
	struct magazinedepot *depot;
	lwkt_tokref ilock;

	crit_enter();
	++cpucache->gets_cumulative;

retry:
	/*
	 * Loaded magazine has an object.  This is the hot path.
	 * It is lock-free and uses a critical section to block
	 * out interrupt handlers on the same processor.
	 */
	loadedmag = cpucache->loaded_magazine;
	if (MAGAZINE_NOTEMPTY(loadedmag)) {
		obj = loadedmag->objects[--loadedmag->rounds];
		crit_exit();
		return (obj);
	}

	/* Previous magazine has an object. */
	if (MAGAZINE_NOTEMPTY(cpucache->previous_magazine)) {
		swap(cpucache->loaded_magazine, cpucache->previous_magazine);
		loadedmag = cpucache->loaded_magazine;
		obj = loadedmag->objects[--loadedmag->rounds];
		crit_exit();
		return (obj);
	}

	/*
	 * Both magazines empty.  Get a full magazine from the depot and
	 * move one of the empty ones to the depot.
	 *
	 * Obtain the depot token.
	 */
	depot = &oc->depot[myclusterid];
	lwkt_gettoken(&ilock, &depot->token);

	/*
	 * We might have blocked obtaining the token, we must recheck
	 * the cpucache before potentially falling through to the blocking
	 * code or we might deadlock the tsleep() on a low-memory machine.
	 */
	if (MAGAZINE_NOTEMPTY(cpucache->loaded_magazine) ||
	    MAGAZINE_NOTEMPTY(cpucache->previous_magazine)
	) {
		lwkt_reltoken(&ilock);
		goto retry;
	}

	/* Check if depot has a full magazine. */
	if (!SLIST_EMPTY(&depot->fullmagazines)) {
		emptymag = cpucache->previous_magazine;
		cpucache->previous_magazine = cpucache->loaded_magazine;
		cpucache->loaded_magazine = SLIST_FIRST(&depot->fullmagazines);
		SLIST_REMOVE_HEAD(&depot->fullmagazines, nextmagazine);

		/*
		 * Return emptymag to the depot.
		 */
		KKASSERT(MAGAZINE_EMPTY(emptymag));
		SLIST_INSERT_HEAD(&depot->emptymagazines,
				  emptymag, nextmagazine);
		lwkt_reltoken(&ilock);
		goto retry;
	}

	/*
	 * The depot does not have any non-empty magazines.  If we have
	 * not hit our object limit we can allocate a new object using
	 * the back-end allocator.
	 *
	 * note: unallocated_objects can be initialized to -1, which has
	 * the effect of removing any allocation limits.
	 */
	if (depot->unallocated_objects) {
		--depot->unallocated_objects;
		lwkt_reltoken(&ilock);
		crit_exit();

		obj = oc->alloc(oc->allocator_args, ocflags);
		if (obj) {
			if (oc->ctor(obj, oc->private, ocflags))
				return (obj);
			oc->free(obj, oc->allocator_args);
			lwkt_gettoken(&ilock, &depot->token);
			++depot->unallocated_objects;
			if (depot->waiting)
				wakeup(depot);
			lwkt_reltoken(&ilock);
			obj = NULL;
		}
		if (obj == NULL) {
			crit_enter();
			/*
			 * makes debugging easier when gets_cumulative does
			 * not include gets_null.
			 */
			++cpucache->gets_null;
			--cpucache->gets_cumulative;
			crit_exit();
		}
		return(obj);
	}

	/*
	 * Otherwise block if allowed to.
	 */
	if ((ocflags & (M_WAITOK|M_NULLOK)) == M_WAITOK) {
		++cpucache->waiting;
		++depot->waiting;
		tsleep(depot, 0, "objcache_get", 0);
		--cpucache->waiting;
		--depot->waiting;
		lwkt_reltoken(&ilock);
		goto retry;
	}

	/*
	 * Otherwise fail
	 */
	++cpucache->gets_null;
	--cpucache->gets_cumulative;
	crit_exit();
	lwkt_reltoken(&ilock);
	return (NULL);
}

/*
 * Wrapper for malloc allocation routines.
 */
void *
objcache_malloc_alloc(void *allocator_args, int ocflags)
{
	struct objcache_malloc_args *alloc_args = allocator_args;

	return (kmalloc(alloc_args->objsize, alloc_args->mtype,
		       ocflags & OC_MFLAGS));
}

void
objcache_malloc_free(void *obj, void *allocator_args)
{
	struct objcache_malloc_args *alloc_args = allocator_args;

	kfree(obj, alloc_args->mtype);
}

/*
 * Wrapper for allocation policies that pre-allocate at initialization time
 * and don't do run-time allocation.
 */
void *
objcache_nop_alloc(void *allocator_args, int ocflags)
{
	return (NULL);
}

void
objcache_nop_free(void *obj, void *allocator_args)
{
}

/*
 * Return an object to the object cache.
 */
void
objcache_put(struct objcache *oc, void *obj)
{
	struct percpu_objcache *cpucache = &oc->cache_percpu[mycpuid];
	struct magazine *loadedmag;
	struct magazinedepot *depot;
	lwkt_tokref ilock;

	crit_enter();
	++cpucache->puts_cumulative;

	if (CLUSTER_OF(obj) != myclusterid) {
#ifdef notyet
		/* use lazy IPI to send object to owning cluster XXX todo */
		++cpucache->puts_othercluster;
		crit_exit();
		return;
#endif
	}

retry:
	/*
	 * Free slot available in loaded magazine.  This is the hot path.
	 * It is lock-free and uses a critical section to block out interrupt
	 * handlers on the same processor.
	 */
	loadedmag = cpucache->loaded_magazine;
	if (!MAGAZINE_FULL(loadedmag)) {
		loadedmag->objects[loadedmag->rounds++] = obj;
		if (cpucache->waiting)
			wakeup_mycpu(&oc->depot[myclusterid]);
		crit_exit();
		return;
	}

	/*
	 * Current magazine full, but previous magazine has room.  XXX
	 */
	if (!MAGAZINE_FULL(cpucache->previous_magazine)) {
		swap(cpucache->loaded_magazine, cpucache->previous_magazine);
		loadedmag = cpucache->loaded_magazine;
		loadedmag->objects[loadedmag->rounds++] = obj;
		if (cpucache->waiting)
			wakeup_mycpu(&oc->depot[myclusterid]);
		crit_exit();
		return;
	}

	/*
	 * Both magazines full.  Get an empty magazine from the depot and
	 * move a full loaded magazine to the depot.  Even though the
	 * magazine may wind up with space available after we block on
	 * the token, we still cycle it through to avoid the non-optimal
	 * corner-case.
	 *
	 * Obtain the depot token.
	 */
	depot = &oc->depot[myclusterid];
	lwkt_gettoken(&ilock, &depot->token);

	/*
	 * If an empty magazine is available in the depot, cycle it
	 * through and retry.
	 */
	if (!SLIST_EMPTY(&depot->emptymagazines)) {
		loadedmag = cpucache->previous_magazine;
		cpucache->previous_magazine = cpucache->loaded_magazine;
		cpucache->loaded_magazine = SLIST_FIRST(&depot->emptymagazines);
		SLIST_REMOVE_HEAD(&depot->emptymagazines, nextmagazine);

		/*
		 * Return loadedmag to the depot.  Due to blocking it may
		 * not be entirely full and could even be empty.
		 */
		if (MAGAZINE_EMPTY(loadedmag)) {
			SLIST_INSERT_HEAD(&depot->emptymagazines,
					  loadedmag, nextmagazine);
		} else {
			SLIST_INSERT_HEAD(&depot->fullmagazines,
					  loadedmag, nextmagazine);
			if (depot->waiting)
				wakeup(depot);
		}
		lwkt_reltoken(&ilock);
		goto retry;
	}

	/*
	 * An empty mag is not available.  This is a corner case which can
	 * occur due to cpus holding partially full magazines.  Do not try
	 * to allocate a mag, just free the object.
	 */
	++depot->unallocated_objects;
	if (depot->waiting)
		wakeup(depot);
	lwkt_reltoken(&ilock);
	crit_exit();
	oc->dtor(obj, oc->private);
	oc->free(obj, oc->allocator_args);
}

/*
 * The object is being put back into the cache, but the caller has
 * indicated that the object is not in any shape to be reused and should
 * be dtor'd immediately.
 */
void
objcache_dtor(struct objcache *oc, void *obj)
{
	struct magazinedepot *depot;
	lwkt_tokref ilock;

	depot = &oc->depot[myclusterid];
	lwkt_gettoken(&ilock, &depot->token);
	++depot->unallocated_objects;
	if (depot->waiting)
		wakeup(depot);
	lwkt_reltoken(&ilock);
	oc->dtor(obj, oc->private);
	oc->free(obj, oc->allocator_args);
}

/*
 * Utility routine for objects that don't require any de-construction.
 */
void
null_dtor(void *obj, void *private)
{
	/* do nothing */
}

boolean_t
null_ctor(void *obj, void *private, int ocflags)
{
	return TRUE;
}

/*
 * De-construct and de-allocate objects in a magazine.
 * Returns the number of objects freed.
 * Does not de-allocate the magazine itself.
 */
static int
mag_purge(struct objcache *oc, struct magazine *mag)
{
	int ndeleted;
	void *obj;

	ndeleted = 0;
	crit_enter();
	while (mag->rounds) {
		obj = mag->objects[--mag->rounds];
		crit_exit();
		oc->dtor(obj, oc->private);
		oc->free(obj, oc->allocator_args);
		++ndeleted;
		crit_enter();
	}
	crit_exit();
	return(ndeleted);
}

/*
 * De-allocate all magazines in a magazine list.
 * Returns number of objects de-allocated.
 */
static int
maglist_purge(struct objcache *oc, struct magazinelist *maglist,
	      boolean_t purgeall)
{
	struct magazine *mag;
	int ndeleted = 0;

	/* can't use SLIST_FOREACH because blocking releases the depot token */
	while ((mag = SLIST_FIRST(maglist))) {
		SLIST_REMOVE_HEAD(maglist, nextmagazine);
		ndeleted += mag_purge(oc, mag);		/* could block! */
		kfree(mag, M_OBJMAG);			/* could block! */
		if (!purgeall && ndeleted > 0)
			break;
	}
	return (ndeleted);
}

/*
 * De-allocates all magazines on the full and empty magazine lists.
 */
static void
depot_purge(struct magazinedepot *depot, struct objcache *oc)
{
	depot->unallocated_objects += 
		maglist_purge(oc, &depot->fullmagazines, TRUE);
	depot->unallocated_objects +=
		maglist_purge(oc, &depot->emptymagazines, TRUE);
	if (depot->unallocated_objects && depot->waiting)
		wakeup(depot);
}

#ifdef notneeded
void
objcache_reclaim(struct objcache *oc)
{
	struct percpu_objcache *cache_percpu = &oc->cache_percpu[myclusterid];
	struct magazinedepot *depot = &oc->depot[myclusterid];

	mag_purge(oc, cache_percpu->loaded_magazine);
	mag_purge(oc, cache_percpu->previous_magazine);

	/* XXX need depot token */
	depot_purge(depot, oc);
}
#endif

/*
 * Try to free up some memory.  Return as soon as some free memory found.
 * For each object cache on the reclaim list, first try the current per-cpu
 * cache, then the full magazine depot.
 */
boolean_t
objcache_reclaimlist(struct objcache *oclist[], int nlist, int ocflags)
{
	struct objcache *oc;
	struct percpu_objcache *cpucache;
	struct magazinedepot *depot;
	lwkt_tokref ilock;
	int i, ndel;

	for (i = 0; i < nlist; i++) {
		oc = oclist[i];
		cpucache = &oc->cache_percpu[mycpuid];
		depot = &oc->depot[myclusterid];

		crit_enter();
		if ((ndel = mag_purge(oc, cpucache->loaded_magazine)) > 0 ||
		    (ndel = mag_purge(oc, cpucache->previous_magazine)) > 0) {
			crit_exit();
			lwkt_gettoken(&ilock, &depot->token);
			depot->unallocated_objects += ndel;
			if (depot->unallocated_objects && depot->waiting)
				wakeup(depot);
			lwkt_reltoken(&ilock);
			return (TRUE);
		}
		crit_exit();
		lwkt_gettoken(&ilock, &depot->token);
		if ((ndel =
		     maglist_purge(oc, &depot->fullmagazines, FALSE)) > 0) {
			depot->unallocated_objects += ndel;
			if (depot->unallocated_objects && depot->waiting)
				wakeup(depot);
			lwkt_reltoken(&ilock);
			return (TRUE);
		}
		lwkt_reltoken(&ilock);
	}
	return (FALSE);
}

/*
 * Destroy an object cache.  Must have no existing references.
 * XXX Not clear this is a useful API function.
 */
void
objcache_destroy(struct objcache *oc)
{
	struct percpu_objcache *cache_percpu;
	int clusterid, cpuid;

	/* XXX need depot token? */
	for (clusterid = 0; clusterid < MAXCLUSTERS; clusterid++)
		depot_purge(&oc->depot[clusterid], oc);

	for (cpuid = 0; cpuid < ncpus; cpuid++) {
		cache_percpu = &oc->cache_percpu[cpuid];

		mag_purge(oc, cache_percpu->loaded_magazine);
		kfree(cache_percpu->loaded_magazine, M_OBJMAG);

		mag_purge(oc, cache_percpu->previous_magazine);
		kfree(cache_percpu->previous_magazine, M_OBJMAG);
	}

	kfree(oc->name, M_TEMP);
	kfree(oc, M_OBJCACHE);
}

#if 0
/*
 * Populate the per-cluster depot with elements from a linear block
 * of memory.  Must be called for individually for each cluster.
 * Populated depots should not be destroyed.
 */
void
objcache_populate_linear(struct objcache *oc, void *base, int nelts, int size)
{
	char *p = base;
	char *end = (char *)base + (nelts * size);
	struct magazinedepot *depot = &oc->depot[myclusterid];
	lwkt_tokref ilock;
	struct magazine sentinelfullmag = { 0, 0 };
	struct magazine *emptymag = &sentinelfullmag;

	lwkt_gettoken(&ilock, &depot->token);
	while (p < end) {
		if (MAGAZINE_FULL(emptymag)) {
			emptymag = mag_alloc(depot->magcapacity);
			SLIST_INSERT_HEAD(&depot->fullmagazines, emptymag,
					  nextmagazine);
		}
		emptymag->objects[emptymag->rounds++] = p;
		p += size;
	}
	depot->unallocated_objects += nelts;
	if (depot->unallocated_objects && depot->waiting)
		wakeup(depot);
	lwkt_reltoken(&ilock);
}
#endif

#if 0
/*
 * Check depot contention once a minute.
 * 2 contested locks per second allowed.
 */
static int objcache_rebalance_period;
static const int objcache_contention_rate = 120;
static struct callout objcache_callout;

#define MAXMAGSIZE 512

/*
 * Check depot contention and increase magazine size if necessary.
 */
static void
objcache_timer(void *dummy)
{
	struct objcache *oc;
	struct magazinedepot *depot;
	lwkt_tokref olock, dlock;

	lwkt_gettoken(&olock, &objcachelist_token);
	SLIST_FOREACH(oc, &allobjcaches, oc_next) {
		depot = &oc->depot[myclusterid];
		if (depot->magcapacity < MAXMAGSIZE) {
			if (depot->contested > objcache_contention_rate) {
				lwkt_gettoken(&dlock, &depot->token);
				depot_purge(depot, oc);
				depot->magcapacity *= 2;
				lwkt_reltoken(&dlock);
				printf("objcache_timer: increasing cache %s"
				       " magsize to %d, contested %d times\n",
				    oc->name, depot->magcapacity,
				    depot->contested);
			}
			depot->contested = 0;
		}
	}
	lwkt_reltoken(&olock);

	callout_reset(&objcache_callout, objcache_rebalance_period,
		      objcache_timer, NULL);
}

#endif

static void
objcache_init(void)
{
	lwkt_token_init(&objcachelist_token);
#if 0
	callout_init(&objcache_callout);
	objcache_rebalance_period = 60 * hz;
	callout_reset(&objcache_callout, objcache_rebalance_period,
		      objcache_timer, NULL);
#endif
}
SYSINIT(objcache, SI_SUB_CPU, SI_ORDER_ANY, objcache_init, 0);
