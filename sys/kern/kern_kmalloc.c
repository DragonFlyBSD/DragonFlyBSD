/*
 * KERN_KMALLOC.C	- Kernel memory allocator
 *
 * Copyright (c) 2021 The DragonFly Project, All rights reserved.
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
 * This module implements the kmalloc_obj allocator.  This is a type-stable
 * allocator that uses the same base structures (e.g. malloc_type) plus
 * some extensions to efficiently implement single-type zones.
 *
 * All memory management is zone based.  When a zone is destroyed, all of
 * its memory is returned to the system with no fragmentation.
 *
 * A mini-slab allocator hangs directly off the zone structure (malloc_type).
 * Since the object zones are single-size-only, the slab allocator is very
 * simple and currently utilizes just two per-zone/per-cpu slabs (active and
 * alternate) before kicking up to the per-zone cache.  Beyond that we just
 * have the per-cpu globaldata-based 'free slab' cache to avoid unnecessary
 * kernel_map mappings and unmappings.
 *
 * The advantage of this that zones don't stomp over each other and cause
 * excessive fragmentation in the slabs.  For example, when you umount a
 * large tmpfs filesystem, most of its memory (all of its kmalloc_obj memory)
 * is returned to the system.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/slaballoc.h>
#include <sys/mbuf.h>
#include <sys/vmmeter.h>
#include <sys/spinlock.h>
#include <sys/lock.h>
#include <sys/thread.h>
#include <sys/globaldata.h>
#include <sys/sysctl.h>
#include <sys/ktr.h>
#include <sys/malloc.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/vm_kern.h>
#include <vm/vm_extern.h>
#include <vm/vm_object.h>
#include <vm/pmap.h>
#include <vm/vm_map.h>
#include <vm/vm_page.h>
#include <vm/vm_pageout.h>

#include <machine/cpu.h>

#include <sys/spinlock2.h>
#include <sys/thread2.h>
#include <sys/exislock2.h>
#include <vm/vm_page2.h>

#define MEMORY_STRING	"ptr=%p type=%p size=%lu flags=%04x"
#define MEMORY_ARGS	void *ptr, void *type, unsigned long size, int flags

#if !defined(KTR_MEMORY)
#define KTR_MEMORY	KTR_ALL
#endif
KTR_INFO_MASTER(mem_obj);
KTR_INFO(KTR_MEMORY, mem_obj, malloc_beg, 0, "kmalloc_obj begin");
KTR_INFO(KTR_MEMORY, mem_obj, malloc_end, 1, MEMORY_STRING, MEMORY_ARGS);
#if 0
KTR_INFO(KTR_MEMORY, mem_obj, free_zero, 2, MEMORY_STRING, MEMORY_ARGS);
KTR_INFO(KTR_MEMORY, mem_obj, free_ovsz, 3, MEMORY_STRING, MEMORY_ARGS);
KTR_INFO(KTR_MEMORY, mem_obj, free_ovsz_delayed, 4, MEMORY_STRING, MEMORY_ARGS);
KTR_INFO(KTR_MEMORY, mem_obj, free_chunk, 5, MEMORY_STRING, MEMORY_ARGS);
KTR_INFO(KTR_MEMORY, mem_obj, free_request, 6, MEMORY_STRING, MEMORY_ARGS);
KTR_INFO(KTR_MEMORY, mem_obj, free_rem_beg, 7, MEMORY_STRING, MEMORY_ARGS);
KTR_INFO(KTR_MEMORY, mem_obj, free_rem_end, 8, MEMORY_STRING, MEMORY_ARGS);
#endif
KTR_INFO(KTR_MEMORY, mem_obj, free_beg, 9, "kfree_obj begin");
KTR_INFO(KTR_MEMORY, mem_obj, free_end, 10, "kfree_obj end");

#define logmemory(name, ptr, type, size, flags)				\
	KTR_LOG(mem_obj_ ## name, ptr, type, size, flags)
#define logmemory_quick(name)						\
	KTR_LOG(mem_obj_ ## name)

__read_frequently static int KMGDMaxFreeSlabs = KMGD_MAXFREESLABS;
SYSCTL_INT(_kern, OID_AUTO, kzone_cache, CTLFLAG_RW, &KMGDMaxFreeSlabs, 0, "");
__read_frequently static int kzone_bretire = 4;
SYSCTL_INT(_kern, OID_AUTO, kzone_bretire, CTLFLAG_RW, &kzone_bretire, 0, "");
__read_frequently static int kzone_debug;
SYSCTL_INT(_kern, OID_AUTO, kzone_debug, CTLFLAG_RW, &kzone_debug, 0, "");

__read_frequently struct kmalloc_slab kslab_dummy;

static void malloc_slab_destroy(struct malloc_type *type,
			struct kmalloc_slab **slabp);

/*
 * Cache a chain of slabs onto their respective cpu slab caches.  Any slabs
 * which we cannot cache will be returned.
 *
 * free_slabs	     - Current structure may only be accessed by current cpu
 * remote_free_slabs - Only atomic swap operations are allowed.
 * free_count	     - Only atomic operations are allowed.
 *
 * If the count is sufficient to cache the entire list, NULL is returned.
 * Otherwise the portion that was not cached is returned.
 */
static __noinline
struct kmalloc_slab *
gslab_cache(struct kmalloc_slab *slab)
{
	struct kmalloc_slab *save;
	struct kmalloc_slab *next;
	struct kmalloc_slab *res;
	struct kmalloc_slab **resp;
	struct kmalloc_slab **slabp;
	globaldata_t rgd;
	size_t count;
	int cpuid;

	res = NULL;
	resp = &res;
	KKASSERT(((uintptr_t)slab & KMALLOC_SLAB_MASK) == 0);

	/*
	 * Given the slab list, get the cpuid and clip off as many matching
	 * elements as fits in the cache.
	 */
	while (slab) {
		cpuid = slab->orig_cpuid;
		rgd = globaldata_find(cpuid);

		KKASSERT(((uintptr_t)slab & KMALLOC_SLAB_MASK) == 0);
		/*
		 * Doesn't fit in cache, put on return list.
		 */
		if (rgd->gd_kmslab.free_count >= KMGDMaxFreeSlabs) {
			*resp = slab;
			resp = &slab->next;
			slab = slab->next;
			continue;
		}

		/*
		 * Collect.  We aren't required to match-up the original cpu
		 * with the disposal cpu, but its a good idea to retain
		 * memory locality.
		 *
		 * The slabs we collect are going into the global cache,
		 * remove the type association.
		 */
		KKASSERT(((uintptr_t)slab & KMALLOC_SLAB_MASK) == 0);
		slabp = &slab->next;
		count = 1;
		slab->type = NULL;

		while ((next = *slabp) != NULL &&
		       next->orig_cpuid == cpuid &&
		       rgd->gd_kmslab.free_count + count < KMGDMaxFreeSlabs)
	        {
			KKASSERT(((uintptr_t)next & KMALLOC_SLAB_MASK) == 0);
			next->type = NULL;
			++count;
			slabp = &next->next;
		}

		/*
		 * Safety, unhook before next, next is not included in the
		 * list starting with slab that is being pre-pended
		 * to remote_free_slabs.
		 */
		*slabp = NULL;

		/*
		 * Now atomically pre-pend slab...*slabp to remote_free_slabs.
		 * Pump the count first (its ok if the actual chain length
		 * races the count update).
		 *
		 * NOTE: In the loop, (save) is updated by fcmpset.
		 */
		atomic_add_long(&rgd->gd_kmslab.free_count, count);
		save = rgd->gd_kmslab.remote_free_slabs;
		for (;;) {
			KKASSERT(((uintptr_t)save & KMALLOC_SLAB_MASK) == 0);
			*slabp = save;	/* end of slab list chain to... */
			cpu_ccfence();
			if (atomic_fcmpset_ptr(
				&rgd->gd_kmslab.remote_free_slabs,
				&save, slab))
			{
				break;
			}
		}

		/*
		 * Setup for next loop
		 */
		slab = next;
	}

	/*
	 * Terminate the result list and return it
	 */
	*resp = NULL;

	return res;
}

/*
 * May only be called on current cpu.  Pull a free slab from the
 * pcpu cache.  If we run out, move any slabs that have built-up
 * from remote cpus.
 *
 * We are only allowed to swap the remote_free_slabs head, we cannot
 * manipulate any next pointers while structures are sitting on that list.
 */
static __inline
struct kmalloc_slab *
gslab_alloc(globaldata_t gd)
{
	struct kmalloc_slab *slab;

	slab = gd->gd_kmslab.free_slabs;
	if (slab == NULL) {
		slab = atomic_swap_ptr(
			(volatile void **)&gd->gd_kmslab.remote_free_slabs,
			NULL);
		KKASSERT(((uintptr_t)slab & KMALLOC_SLAB_MASK) == 0);
	}
	if (slab) {
		gd->gd_kmslab.free_slabs = slab->next;
		slab->next = NULL;
		atomic_add_long(&gd->gd_kmslab.free_count, -1);
		KKASSERT(((uintptr_t)slab & KMALLOC_SLAB_MASK) == 0);
	}
	return slab;
}

void
malloc_mgt_init(struct malloc_type *type __unused,
		struct kmalloc_mgt *mgt, size_t size)
{
	size_t offset;
	size_t count;

	bzero(mgt, sizeof(*mgt));
	spin_init(&mgt->spin, "kmmgt");

	/*
	 * Allows us to avoid a conditional.  The dummy slabs are empty
	 * and have no objects.
	 */
	mgt->active = &kslab_dummy;
	mgt->alternate = &kslab_dummy;
	mgt->empty_tailp = &mgt->empty;

	/*
	 * Figure out the count by taking into account the size of the fobjs[]
	 * array by adding it to the object size.  This initial calculation
	 * ignores alignment edge-cases that might require the count to be
	 * reduced.
	 */
	offset = offsetof(struct kmalloc_slab, fobjs[0]);
	count = (KMALLOC_SLAB_SIZE - offset) / (size + sizeof(void *));

	/*
	 * Recalculate the offset of the first object, this time including
	 * the required alignment.  (size) should already be aligned.  This
	 * may push the last object beyond the slab so check and loop with
	 * a reduced count as necessary.
	 *
	 * Ok, theoretically the count should not actually change since the
	 * division above rounds-down (that is, any mis-alignment is already
	 * not included in the count calculation).  But I'm not going to take
	 * any chances and check anyway as a safety in case some programmer
	 * changes the code above later.  This is not a time-critical code
	 * path.
	 */
	offset = offsetof(struct kmalloc_slab, fobjs[count]);
	offset = __VM_CACHELINE_ALIGN(offset);

	while (offset + size * count > KMALLOC_SLAB_SIZE) {
		--count;
		offset = offsetof(struct kmalloc_slab, fobjs[count]);
		offset = __VM_CACHELINE_ALIGN(offset);
		KKASSERT (offset + size * count <= KMALLOC_SLAB_SIZE);
	}

	mgt->slab_offset = offset;
	mgt->slab_count	 = count;
}

void
malloc_mgt_relocate(struct kmalloc_mgt *src, struct kmalloc_mgt *dst)
{
	struct kmalloc_slab **slabp;

	spin_init(&dst->spin, "kmmgt");
	slabp = &dst->empty;

	while (*slabp) {
		slabp = &(*slabp)->next;
	}
	dst->empty_tailp = slabp;
}

void
malloc_mgt_uninit(struct malloc_type *type, struct kmalloc_mgt *mgt)
{
	if (mgt->active != &kslab_dummy)
		malloc_slab_destroy(type, &mgt->active);
	mgt->active = NULL;

	if (mgt->alternate != &kslab_dummy)
		malloc_slab_destroy(type, &mgt->alternate);
	mgt->alternate = NULL;

	malloc_slab_destroy(type, &mgt->partial);
	malloc_slab_destroy(type, &mgt->full);
	malloc_slab_destroy(type, &mgt->empty);
	mgt->npartial = 0;
	mgt->nfull = 0;
	mgt->nempty = 0;
	mgt->empty_tailp = &mgt->empty;

	spin_uninit(&mgt->spin);
}

/*
 * Destroy a list of slabs.  Attempt to cache the slabs on the specified
 * (possibly remote) cpu.  This allows slabs that were operating on a
 * particular cpu to be disposed of back to that same cpu.
 */
static void
malloc_slab_destroy(struct malloc_type *type, struct kmalloc_slab **slabp)
{
	struct kmalloc_slab *slab;
	struct kmalloc_slab *base;
	struct kmalloc_slab **basep;
	size_t delta;

	if (*slabp == NULL)
		return;

	/*
	 * Collect all slabs that can actually be destroyed, complain
	 * about the rest.
	 */
	base = NULL;
	basep = &base;
	while ((slab = *slabp) != NULL) {
		KKASSERT(((uintptr_t)slab & KMALLOC_SLAB_MASK) == 0);

		delta = slab->findex - slab->aindex;
		if (delta == slab->ncount) {
			*slabp = slab->next;	/* unlink */
			*basep = slab;		/* link into base list */
			basep = &slab->next;
		} else {
			kprintf("%s: slab %p %zd objects "
				"were still allocated\n",
				type->ks_shortdesc, slab,
				slab->ncount - delta);
			/* leave link intact and iterate */
			slabp = &slab->next;
		}
	}

	/*
	 * Terminate the base list of slabs that can be destroyed,
	 * then cache as many of them as possible.
	 */
	*basep = NULL;
	if (base == NULL)
		return;
	base = gslab_cache(base);

	/*
	 * Destroy the remainder
	 */
	while ((slab = base) != NULL) {
		base = slab->next;
		slab->next = (void *)(uintptr_t)-1;
		kmem_slab_free(slab, KMALLOC_SLAB_SIZE);
	}
}

/*
 * Objects can be freed to an empty slab at any time, causing it to no
 * longer be empty.  To improve performance, we do not try to pro-actively
 * move such slabs to the appropriate partial or full list upon kfree_obj().
 * Instead, a poller comes along and tests the slabs on the empty list
 * periodically, and moves slabs that are no longer empty to the appropriate
 * list.
 *
 * --
 *
 * Poll a limited number of slabs on the empty list and move them
 * to the appropriate full or partial list.  Slabs left on the empty
 * list are rotated to the tail.
 *
 * If gcache is non-zero this function will try to place full slabs into
 * the globaldata cache, if it isn't already too full.
 *
 * The mgt is spin-locked
 *
 * Returns non-zero if the ggm updates possibly made slabs available for
 * allocation.
 */
static int
malloc_mgt_poll_empty_locked(struct kmalloc_mgt *ggm, int count)
{
	struct kmalloc_slab *marker;
	struct kmalloc_slab *slab;
	size_t delta;
	int got_something;

	if (ggm->empty == NULL)
		return 0;

	got_something = 0;
	marker = ggm->empty;

	while (count-- && (slab = ggm->empty) != NULL) {
		/*
		 * Unlink from empty
		 */
		ggm->empty = slab->next;
		slab->next = NULL;
		--ggm->nempty;
		if (ggm->empty_tailp == &slab->next)
			ggm->empty_tailp = &ggm->empty;

		/*
		 * Check partial, full, and empty.  We rotate
		 * empty entries to the end of the empty list.
		 *
		 * NOTE: For a fully-freeable slab we also have
		 *	 to check xindex.
		 */
		delta = slab->findex - slab->aindex;
		if (delta == slab->ncount) {
			/*
			 * Stuff into the full list.  This requires setting
			 * the exis sequence number via exis_terminate().
			 */
			KKASSERT(slab->next == NULL);
			exis_terminate(&slab->exis);
			slab->next = ggm->full;
			ggm->full = slab;
			got_something = 1;
			++ggm->nfull;
		} else if (delta) {
			/*
			 * Partially full
			 */
			KKASSERT(slab->next == NULL);
			slab->next = ggm->partial;
			ggm->partial = slab;
			got_something = 1;
			++ggm->npartial;
		} else {
			/*
			 * Empty
			 */
			KKASSERT(slab->next == NULL);
			*ggm->empty_tailp = slab;
			ggm->empty_tailp = &slab->next;
			++ggm->nempty;
			if (ggm->empty == marker)
				break;
		}
	}
	return got_something;
}

/*
 * Called once a second with the zone interlocked against destruction.
 *
 * Returns non-zero to tell the caller to iterate to the next type,
 * else the caller should stay on the current type.
 */
int
malloc_mgt_poll(struct malloc_type *type)
{
	struct kmalloc_mgt *ggm;
	struct kmalloc_slab *slab;
	struct kmalloc_slab **slabp;
	struct kmalloc_slab *base;
	struct kmalloc_slab **basep;
	size_t delta;
	int donext;
	int count;
	int retired;

	if ((type->ks_flags & KSF_OBJSIZE) == 0)
		return 1;

	/*
	 * Check the partial, full, and empty lists for full freeable slabs
	 * in excess of desired caching count.
	 */
	ggm = &type->ks_mgt;
	spin_lock(&ggm->spin);

	/*
	 * Move empty slabs to partial or full as appropriate.  We
	 * don't bother checking partial slabs to see if they are full
	 * for now.
	 */
	malloc_mgt_poll_empty_locked(ggm, 16);

	/*
	 * Ok, cleanout some of the full mags from the full list
	 */
	base = NULL;
	basep = &base;
	count = ggm->nfull;
	retired = 0;
	cpu_ccfence();

	if (count > KMALLOC_MAXFREEMAGS) {
		slabp = &ggm->full;
		count -= KMALLOC_MAXFREEMAGS;
		if (count > 16)
			count = 16;

		while (count && (slab = *slabp) != NULL) {
			delta = slab->findex - slab->aindex;
			if (delta == slab->ncount &&
			    slab->xindex == slab->findex &&
			    exis_freeable(&slab->exis))
			{
				/*
				 * (1) No allocated entries in the structure,
				 *     this should always be the case from the
				 *     full list.
				 *
				 * (2) kfree_obj() has fully completed.  Just
				 *     checking findex is not sufficient since
				 *     it is incremented to reserve the slot
				 *     before the element is loaded into it.
				 *
				 * (3) The slab has been on the full list for
				 *     a sufficient number of EXIS
				 *     pseudo_ticks, for type-safety.
				 */
				*slabp = slab->next;
				*basep = slab;
				basep = &slab->next;
				--ggm->nfull;
				++ggm->gcache_count;
				if (++retired == kzone_bretire)
					break;
			} else {
				slabp = &slab->next;
			}
			--count;
		}
		*basep = NULL;	/* terminate the retirement list */
		donext = (*slabp == NULL);
	} else {
		donext = 1;
	}
	spin_unlock(&ggm->spin);

	/*
	 * Clean out any slabs that we couldn't stow in the globaldata cache.
	 */
	if (retired) {
		if (kzone_debug) {
			kprintf("kmalloc_poll: %s retire %d\n",
				type->ks_shortdesc, retired);
		}
		base = gslab_cache(base);
		while ((slab = base) != NULL) {
			base = base->next;
			slab->next = NULL;
			kmem_slab_free(slab, KMALLOC_SLAB_SIZE);
		}
	}

	return donext;
}

/*
 * Optional bitmap double-free check.  This is typically turned on by
 * default for safety (sys/_malloc.h)
 */
#ifdef KMALLOC_CHECK_DOUBLE_FREE

static __inline void
bmap_set(struct kmalloc_slab *slab, void *obj)
{
	uint64_t *ptr;
	uint64_t mask;
	size_t i = (((uintptr_t)obj & KMALLOC_SLAB_MASK) - slab->offset) /
		   slab->objsize;

	ptr = &slab->bmap[i >> 6];
	mask = (uint64_t)1U << (i & 63);
	KKASSERT(i < slab->ncount && (*ptr & mask) == 0);
	atomic_set_64(ptr, mask);
}

static __inline void
bmap_clr(struct kmalloc_slab *slab, void *obj)
{
	uint64_t *ptr;
	uint64_t mask;
	size_t i = (((uintptr_t)obj & KMALLOC_SLAB_MASK) - slab->offset) /
		   slab->objsize;

	ptr = &slab->bmap[i >> 6];
	mask = (uint64_t)1U << (i & 63);
	KKASSERT(i < slab->ncount && (*ptr & mask) != 0);
	atomic_clear_64(ptr, mask);
}

#endif

/*
 * Cleanup a mgt structure.
 *
 * Always called from the current cpu, so we can manipulate the various
 * lists freely.
 *
 * WARNING: findex can race, fobjs[n] is updated after findex is incremented,
 *	    and 'full'
 */
#if 0
static void
mgt_cleanup(struct kmalloc_mgt *mgt)
{
#if 0
	struct kmalloc_slab **slabp;
	struct kmalloc_slab *slab;
	size_t delta;
	size_t total;
#endif
}
#endif

#ifdef SLAB_DEBUG
void *
_kmalloc_obj_debug(unsigned long size, struct malloc_type *type, int flags,
	      const char *file, int line)
#else
void *
_kmalloc_obj(unsigned long size, struct malloc_type *type, int flags)
#endif
{
	struct kmalloc_slab *slab;
	struct kmalloc_use *use;
	struct kmalloc_mgt *mgt;
	struct kmalloc_mgt *ggm;
	globaldata_t gd;
	void *obj;
	size_t delta;

	/*
	 * Check limits
	 */
	while (__predict_false(type->ks_loosememuse >= type->ks_limit)) {
		long ttl;
		int n;

		for (n = ttl = 0; n < ncpus; ++n)
			ttl += type->ks_use[n].memuse;
		type->ks_loosememuse = ttl;	/* not MP synchronized */
		if ((ssize_t)ttl < 0)		/* deal with occassional race */
			ttl = 0;
		if (ttl >= type->ks_limit) {
			if (flags & M_NULLOK)
				return(NULL);
			panic("%s: malloc limit exceeded", type->ks_shortdesc);
		}
	}

	/*
	 * Setup
	 */
	crit_enter();
	logmemory_quick(malloc_beg);
	KKASSERT(size == type->ks_objsize);
	gd = mycpu;
	use = &type->ks_use[gd->gd_cpuid];

retry:
	/*
	 * Check active
	 *
	 * NOTE: obj can be NULL if racing a _kfree_obj().
	 */
	mgt = &use->mgt;
	slab = mgt->active;			/* Might be dummy */
	delta = slab->findex - slab->aindex;
	if (__predict_true(delta != 0)) {	/* Cannot be dummy */
		size_t i;

		i = slab->aindex % slab->ncount;
		obj = slab->fobjs[i];
		if (__predict_true(obj != NULL)) {
			slab->fobjs[i] = NULL;
			++slab->aindex;
#ifdef KMALLOC_CHECK_DOUBLE_FREE
			bmap_set(slab, obj);
#endif
			goto found;
		}
	}

	/*
	 * Check alternate.  If we find something, swap it with
	 * the active.
	 *
	 * NOTE: It is possible for exhausted slabs to recover entries
	 *	 via _kfree_obj(), so we just keep swapping until both
	 *	 are empty.
	 *
	 * NOTE: obj can be NULL if racing a _kfree_obj().
	 */
	slab = mgt->alternate;			/* Might be dummy */
	delta = slab->findex - slab->aindex;
	if (__predict_true(delta != 0)) {	/* Cannot be dummy */
		size_t i;

		mgt->alternate = mgt->active;
		mgt->active = slab;
		i = slab->aindex % slab->ncount;
		obj = slab->fobjs[i];
		if (__predict_true(obj != NULL)) {
			slab->fobjs[i] = NULL;
			++slab->aindex;
#ifdef KMALLOC_CHECK_DOUBLE_FREE
			bmap_set(slab, obj);
#endif
			goto found;
		}
	}

	/*
	 * Rotate a slab from the global mgt into the pcpu mgt.
	 *
	 *	G(partial, full) -> active -> alternate -> G(empty)
	 *
	 * We try to exhaust partials first to reduce fragmentation, then
	 * dig into the fulls.
	 */
	ggm = &type->ks_mgt;
	spin_lock(&ggm->spin);

rerotate:
	if (ggm->partial) {
		slab = mgt->alternate;		/* Might be dummy */
		mgt->alternate = mgt->active;	/* Might be dummy */
		mgt->active = ggm->partial;
		ggm->partial = ggm->partial->next;
		mgt->active->next = NULL;
		--ggm->npartial;
		if (slab != &kslab_dummy) {
			KKASSERT(slab->next == NULL);
			*ggm->empty_tailp = slab;
			ggm->empty_tailp = &slab->next;
			++ggm->nempty;
		}
		spin_unlock(&ggm->spin);
		goto retry;
	}

	if (ggm->full) {
		slab = mgt->alternate;		/* Might be dummy */
		mgt->alternate = mgt->active;	/* Might be dummy */
		mgt->active = ggm->full;
		ggm->full = ggm->full->next;
		mgt->active->next = NULL;
		--ggm->nfull;
		exis_setlive(&mgt->active->exis);
		if (slab != &kslab_dummy) {
			KKASSERT(slab->next == NULL);
			*ggm->empty_tailp = slab;
			ggm->empty_tailp = &slab->next;
			++ggm->nempty;
		}
		spin_unlock(&ggm->spin);
		goto retry;
	}

	/*
	 * We couldn't find anything, scan a limited number of empty entries
	 * looking for something with objects.  This will also free excess
	 * full lists that meet requirements.
	 */
	if (malloc_mgt_poll_empty_locked(ggm, 16))
		goto rerotate;

	/*
	 * Absolutely nothing is available, allocate a new slab and
	 * rotate it in.
	 *
	 * Try to get a slab from the global pcpu slab cache (very cheap).
	 * If that fails, allocate a new slab (very expensive).
	 */
	spin_unlock(&ggm->spin);

	if (gd->gd_kmslab.free_count == 0 || (slab = gslab_alloc(gd)) == NULL) {
		slab = kmem_slab_alloc(KMALLOC_SLAB_SIZE, KMALLOC_SLAB_SIZE,
				       M_WAITOK);
	}

	bzero(slab, sizeof(*slab));
	KKASSERT(offsetof(struct kmalloc_slab, fobjs[use->mgt.slab_count]) <=
		 use->mgt.slab_offset);

	obj = (char *)slab + use->mgt.slab_offset;
	slab->type = type;
	slab->orig_cpuid = gd->gd_cpuid;
	slab->ncount = use->mgt.slab_count;
	slab->offset = use->mgt.slab_offset;
	slab->objsize = type->ks_objsize;
	slab->aindex = 0;
	slab->findex = slab->ncount;
	slab->xindex = slab->ncount;
	for (delta = 0; delta < slab->ncount; ++delta) {
		slab->fobjs[delta] = obj;
		obj = (char *)obj + type->ks_objsize;
	}

	/*
	 * Sanity check, assert that the last byte of last object is still
	 * in the slab.
	 */
#if 0
	KKASSERT(((((uintptr_t)obj - 1) ^ (uintptr_t)slab) &
		  ~KMALLOC_SLAB_MASK) == 0);
#endif
	KASSERT(((((uintptr_t)obj - 1) ^ (uintptr_t)slab) &
		  ~KMALLOC_SLAB_MASK) == 0, ("SLAB %p ncount %zd objsize %zd obj=%p\n", slab, slab->ncount, slab->objsize, obj));
	slab->magic = KMALLOC_SLAB_MAGIC;
	spin_init(&slab->spin, "kmslb");

	/*
	 * Rotate it in, then retry.
	 *
	 *	(NEW)slab -> active -> alternate -> G(empty)
	 */
	spin_lock(&ggm->spin);
	if (mgt->alternate != &kslab_dummy) {
		struct kmalloc_slab *slab_tmp;

		slab_tmp = mgt->alternate;
		slab_tmp->next = NULL;
		*ggm->empty_tailp = slab_tmp;
		ggm->empty_tailp = &slab_tmp->next;
		++ggm->nempty;
	}
	mgt->alternate = mgt->active;		/* Might be dummy */
	mgt->active = slab;
	spin_unlock(&ggm->spin);

	goto retry;

	/*
	 * Found object, adjust statistics and return
	 */
found:
	++use->inuse;
	++use->calls;
	use->memuse += size;
	use->loosememuse += size;
	if (__predict_false(use->loosememuse >= KMALLOC_LOOSE_SIZE)) {
	    /* not MP synchronized */
	    type->ks_loosememuse += use->loosememuse;
	    use->loosememuse = 0;
	}

	/*
	 * Handle remaining flags.  M_ZERO is typically not set because
	 * the inline macro deals with zeroing for constant sizes.
	 */
	if (__predict_false(flags & M_ZERO))
	    bzero(obj, size);

	crit_exit();
	logmemory(malloc_end, NULL, type, size, flags);

	return(obj);
}

/*
 * Free a type-stable object.  We have the base structure and can
 * calculate the slab, but from this direction we don't know which
 * mgt structure or list the slab might be on.
 */
void
_kfree_obj(void *obj, struct malloc_type *type)
{
	struct kmalloc_slab *slab;
	struct kmalloc_use *use;
	globaldata_t gd;
	size_t	delta;
	size_t	i;

	logmemory_quick(free_beg);
	gd = mycpu;

	/*
	 * Calculate the slab from the pointer
	 */
	slab = (void *)((uintptr_t)obj & ~KMALLOC_SLAB_MASK);
	delta = slab->findex - slab->aindex;
	KKASSERT(slab->magic == KMALLOC_SLAB_MAGIC && delta != slab->ncount);

	/*
	 * We can only safely adjust the statistics for the current cpu.
	 * Don't try to track down the original cpu.  The statistics will
	 * be collected and fixed up by vmstat -m  (etc).
	 */
	use = &slab->type->ks_use[gd->gd_cpuid];
	--use->inuse;
	use->memuse -= slab->objsize;

	/*
	 * There MUST be free space in the slab since we are returning
	 * the obj to the same slab it was allocated from.
	 */
	i = atomic_fetchadd_long(&slab->findex, 1);
	i = i % slab->ncount;
	if (slab->fobjs[i] != NULL) {
		kprintf("_kfree_obj failure %zd/%zd/%zd\n",
			slab->aindex, slab->findex, slab->ncount);
	}
#ifdef KMALLOC_CHECK_DOUBLE_FREE
	bmap_clr(slab, obj);
#endif
	KKASSERT(slab->fobjs[i] == NULL);
	slab->fobjs[i] = obj;
	atomic_add_long(&slab->xindex, 1);	/* synchronizer */

	logmemory_quick(free_end);
}
