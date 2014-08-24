/*
 * Copyright (c) 2007 The DragonFly Project.  All rights reserved.
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
 */
/*
 * System resource control module for all cluster-addressable system resource
 * structures.
 *
 * This module implements the core ref counting, sysid registration, and
 * objcache-backed allocation mechanism for all major system resource
 * structures.
 *
 * sysid registrations operate via the objcache ctor/dtor mechanism and
 * sysids will be reused if the resource is not explicitly accessed via
 * its sysid.  This removes all RB tree handling overhead from the critical
 * path for locally used resources.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/tree.h>
#include <sys/spinlock.h>
#include <machine/atomic.h>
#include <machine/cpufunc.h>

#include <sys/spinlock2.h>
#include <sys/sysref2.h>

static boolean_t sysref_ctor(void *data, void *privdata, int ocflags);
static void sysref_dtor(void *data, void *privdata);

/*
 * Red-Black tree support
 */
static int rb_sysref_compare(struct sysref *sr1, struct sysref *sr2);
RB_GENERATE2(sysref_rb_tree, sysref, rbnode, rb_sysref_compare, sysid_t, sysid);

static struct srpercpu {
	struct sysref_rb_tree rbtree;
	struct spinlock spin;
} sysref_array[MAXCPU];

static void
sysrefbootinit(void *dummy __unused)
{
	struct srpercpu *sa;
	int i;

	for (i = 0; i < ncpus; ++i) {
		sa = &sysref_array[i];
		spin_init(&sa->spin, "sysrefbootinit");
		RB_INIT(&sa->rbtree);
	}
}

SYSINIT(sysref, SI_BOOT2_MACHDEP, SI_ORDER_ANY, sysrefbootinit, NULL);

static
int
rb_sysref_compare(struct sysref *sr1, struct sysref *sr2)
{
	if (sr1->sysid < sr2->sysid)
		return(-1);
	if (sr1->sysid > sr2->sysid)
		return(1);
	return(0);
}

/*
 * Manual initialization of a resource structure's sysref, only used during
 * booting to set up certain statically declared resources which cannot
 * be deallocated.
 */
void
sysref_init(struct sysref *sr, struct sysref_class *srclass)
{
	struct srpercpu *sa;
	globaldata_t gd;

	gd = mycpu;
	crit_enter_gd(gd);
	gd->gd_sysid_alloc += ncpus_fit; /* next unique sysid */
	sr->sysid = gd->gd_sysid_alloc;
	KKASSERT(((int)sr->sysid & ncpus_fit_mask) == gd->gd_cpuid);
	sr->refcnt = -0x40000000;
	sr->flags = 0;
	sr->srclass = srclass;

	sa = &sysref_array[gd->gd_cpuid];
	spin_lock(&sa->spin);
	sysref_rb_tree_RB_INSERT(&sa->rbtree, sr);
	spin_unlock(&sa->spin);
	crit_exit_gd(gd);
}

/*
 * Allocate a resource structure of the specified class, initialize a
 * sysid and add the resource to the RB tree.  The caller must complete
 * initialization of the resource and call sysref_activate() to activate it.
 */
void *
sysref_alloc(struct sysref_class *srclass)
{
	struct sysref *sr;
	char *data;
	int n;

	/*
	 * Create the object cache backing store.
	 */
	if (srclass->oc == NULL) {
		KKASSERT(srclass->mtype != NULL);
		srclass->oc = objcache_create_mbacked(
				srclass->mtype, srclass->objsize, 
				0, srclass->nom_cache,
				sysref_ctor, sysref_dtor, srclass);
	}

	/*
	 * Allocate the resource.
	 */
	data = objcache_get(srclass->oc, M_WAITOK);
	sr = (struct sysref *)(data + srclass->offset);
	KKASSERT(sr->flags & SRF_PUTAWAY);
	sr->flags &= ~SRF_PUTAWAY;

	/*
	 * Refcnt isn't touched while it is zero.  The objcache ctor
	 * function has already allocated a sysid and emplaced the
	 * structure in the RB tree.
	 */
	KKASSERT(sr->refcnt == 0);
	sr->refcnt = -0x40000000;

	/*
	 * Clean out the structure unless the caller wants to deal with
	 * it (e.g. like the vmspace code).
	 */
	if ((srclass->flags & SRC_MANAGEDINIT) == 0) {
		if (srclass->offset != 0)
			bzero(data, srclass->offset);
		n = srclass->offset + sizeof(struct sysref);
		KKASSERT(n <= srclass->objsize);
		if (n != srclass->objsize)
			bzero(data + n, srclass->objsize - n);
	}
	return(data);
}

/*
 * Object cache backing store ctor function.
 *
 * This allocates the sysid and associates the structure with the
 * red-black tree, allowing it to be looked up.  The actual resource
 * structure has NOT yet been allocated so it is marked free.
 *
 * If the sysid is not used to access the resource, we will just
 * allow the sysid to be reused when the resource structure is reused,
 * allowing the RB tree operation to be 'cached'.  This results in
 * virtually no performance penalty for using the sysref facility.
 */
static
boolean_t
sysref_ctor(void *data, void *privdata, int ocflags)
{
	globaldata_t gd;
	struct srpercpu *sa;
	struct sysref_class *srclass = privdata;
	struct sysref *sr = (void *)((char *)data + srclass->offset);

	/*
	 * Resource structures need to be cleared when allocating from
	 * malloc backing store.  This is different from the zeroing
	 * that we do in sysref_alloc().
	 */
	bzero(data, srclass->objsize);

	/*
	 * Resources managed by our objcache do the sysid and RB tree
	 * handling in the objcache ctor/dtor, so we can reuse the
	 * structure without re-treeing it over and over again.
	 */
	gd = mycpu;
	crit_enter_gd(gd);
	gd->gd_sysid_alloc += ncpus_fit; /* next unique sysid */
	sr->sysid = gd->gd_sysid_alloc;
	KKASSERT(((int)sr->sysid & ncpus_fit_mask) == gd->gd_cpuid);
	/* sr->refcnt= 0; already zero */
	sr->flags = SRF_ALLOCATED | SRF_PUTAWAY;
	sr->srclass = srclass;

	sa = &sysref_array[gd->gd_cpuid];
	spin_lock(&sa->spin);
	sysref_rb_tree_RB_INSERT(&sa->rbtree, sr);
	spin_unlock(&sa->spin);
	crit_exit_gd(gd);

	/*
	 * Execute the class's ctor function, if any.  NOTE: The class
	 * should not try to zero out the structure, we've already handled
	 * that and preinitialized the sysref.
	 *
	 * XXX ignores return value for now
	 */
	if (srclass->ctor)
		srclass->ctor(data, privdata, ocflags);
	return TRUE;
}

/*
 * Object cache destructor, allowing the structure to be returned
 * to the system memory pool.  The resource structure must be
 * removed from the RB tree.  All other references have already
 * been destroyed and the RB tree will not create any new references
 * to the structure in its current state.
 */
static
void
sysref_dtor(void *data, void *privdata)
{
	struct srpercpu *sa;
	struct sysref_class *srclass = privdata;
	struct sysref *sr = (void *)((char *)data + srclass->offset);

	KKASSERT(sr->refcnt == 0);
	sa = &sysref_array[(int)sr->sysid & ncpus_fit_mask];
	spin_lock(&sa->spin);
	sysref_rb_tree_RB_REMOVE(&sa->rbtree, sr);
	spin_unlock(&sa->spin);
	if (srclass->dtor)
		srclass->dtor(data, privdata);
}

/*
 * Activate or reactivate a resource. 0x40000001 is added to the ref count
 * so -0x40000000 (during initialization) will translate to a ref count of 1.
 * Any references made during initialization will translate to additional
 * positive ref counts.
 *
 * MPSAFE
 */
void
sysref_activate(struct sysref *sr)
{
	int count;

	for (;;) {
		count = sr->refcnt;
		KASSERT(count < 0 && count + 0x40000001 > 0,
			("sysref_activate: bad count %08x", count));
		if (atomic_cmpset_int(&sr->refcnt, count, count + 0x40000001))
			break;
		cpu_pause();
	}
}

/*
 * Release a reference under special circumstances.  This call is made
 * from the sysref_put() inline from sys/sysref2.h for any 1->0 transitions,
 * negative->negative 'termination in progress' transitions, and when the
 * cmpset instruction fails during a normal transition.
 *
 * This function is called from the sysref_put() inline in sys/sysref2.h,
 * but handles all cases regardless.
 */
void
_sysref_put(struct sysref *sr)
{
	int count;
	void *data;

	KKASSERT((sr->flags & SRF_PUTAWAY) == 0);

	for (;;) {
		count = sr->refcnt;
		if (count > 1) {
			/*
			 * release 1 count, nominal case, active resource
			 * structure, no other action required.
			 */
			if (atomic_cmpset_int(&sr->refcnt, count, count - 1))
				break;
		} else if (count == 1) {
			/*
			 * 1->0 transitions transition to -0x40000000 instead,
			 * placing the resource structure into a termination-
			 * in-progress state.  The termination function is
			 * then called.
			 */
			data = (char *)sr - sr->srclass->offset;
			sr->srclass->ops.lock(data);
			if (atomic_cmpset_int(&sr->refcnt, count, -0x40000000)) {
				sr->srclass->ops.terminate(data);
				break;
			}
			sr->srclass->ops.unlock(data);
		} else if (count > -0x40000000) {
			/*
			 * release 1 count, nominal case, resource undergoing
			 * termination.  The Resource can be ref'd and
			 * deref'd while undergoing termination.
			 */
			if (atomic_cmpset_int(&sr->refcnt, count, count - 1))
				break;
		} else {
			/*
			 * Final release, set refcnt to 0.
			 * Resource must have been allocated.
			 *
			 * If SRF_SYSIDUSED is not set just objcache_put() the
			 * resource, otherwise objcache_dtor() the resource.
			 */
			KKASSERT(count == -0x40000000);
			if (atomic_cmpset_int(&sr->refcnt, count, 0)) {
				KKASSERT(sr->flags & SRF_ALLOCATED);
				sr->flags |= SRF_PUTAWAY;
				data = (char *)sr - sr->srclass->offset;
				if (sr->flags & SRF_SYSIDUSED)
					objcache_dtor(sr->srclass->oc, data);
				else
					objcache_put(sr->srclass->oc, data);
				break;
			}
		}
		/* loop until the cmpset succeeds */
		cpu_pause();
	}
}

sysid_t
allocsysid(void)
{
	globaldata_t gd = mycpu;
	sysid_t sysid;

	crit_enter_gd(gd);
	gd->gd_sysid_alloc += ncpus_fit;
	sysid = gd->gd_sysid_alloc;
	crit_exit_gd(gd);
	return(sysid);
}

