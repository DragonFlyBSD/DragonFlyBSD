/*
 * (MPSAFE)
 *
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * The Mach Operating System project at Carnegie-Mellon University.
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
 *	from: @(#)vm_map.c	8.3 (Berkeley) 1/12/94
 *
 *
 * Copyright (c) 1987, 1990 Carnegie-Mellon University.
 * All rights reserved.
 *
 * Authors: Avadis Tevanian, Jr., Michael Wayne Young
 *
 * Permission to use, copy, modify and distribute this software and
 * its documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND
 * FOR ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie the
 * rights to redistribute these changes.
 *
 * $FreeBSD: src/sys/vm/vm_map.c,v 1.187.2.19 2003/05/27 00:47:02 alc Exp $
 */

/*
 *	Virtual memory mapping module.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/serialize.h>
#include <sys/lock.h>
#include <sys/vmmeter.h>
#include <sys/mman.h>
#include <sys/vnode.h>
#include <sys/resourcevar.h>
#include <sys/shm.h>
#include <sys/tree.h>
#include <sys/malloc.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/pmap.h>
#include <vm/vm_map.h>
#include <vm/vm_page.h>
#include <vm/vm_object.h>
#include <vm/vm_pager.h>
#include <vm/vm_kern.h>
#include <vm/vm_extern.h>
#include <vm/swap_pager.h>
#include <vm/vm_zone.h>

#include <sys/thread2.h>
#include <sys/sysref2.h>
#include <sys/random.h>
#include <sys/sysctl.h>

/*
 * Virtual memory maps provide for the mapping, protection, and sharing
 * of virtual memory objects.  In addition, this module provides for an
 * efficient virtual copy of memory from one map to another.
 *
 * Synchronization is required prior to most operations.
 *
 * Maps consist of an ordered doubly-linked list of simple entries.
 * A hint and a RB tree is used to speed-up lookups.
 *
 * Callers looking to modify maps specify start/end addresses which cause
 * the related map entry to be clipped if necessary, and then later
 * recombined if the pieces remained compatible.
 *
 * Virtual copy operations are performed by copying VM object references
 * from one map to another, and then marking both regions as copy-on-write.
 */
static void vmspace_terminate(struct vmspace *vm);
static void vmspace_lock(struct vmspace *vm);
static void vmspace_unlock(struct vmspace *vm);
static void vmspace_dtor(void *obj, void *private);

MALLOC_DEFINE(M_VMSPACE, "vmspace", "vmspace objcache backingstore");

struct sysref_class vmspace_sysref_class = {
	.name =		"vmspace",
	.mtype =	M_VMSPACE,
	.proto =	SYSREF_PROTO_VMSPACE,
	.offset =	offsetof(struct vmspace, vm_sysref),
	.objsize =	sizeof(struct vmspace),
	.nom_cache =	32,
	.flags = SRC_MANAGEDINIT,
	.dtor = vmspace_dtor,
	.ops = {
		.terminate = (sysref_terminate_func_t)vmspace_terminate,
		.lock = (sysref_lock_func_t)vmspace_lock,
		.unlock = (sysref_lock_func_t)vmspace_unlock
	}
};

/*
 * per-cpu page table cross mappings are initialized in early boot
 * and might require a considerable number of vm_map_entry structures.
 */
#define VMEPERCPU	(MAXCPU+1)

static struct vm_zone mapentzone_store, mapzone_store;
static vm_zone_t mapentzone, mapzone;
static struct vm_object mapentobj, mapobj;

static struct vm_map_entry map_entry_init[MAX_MAPENT];
static struct vm_map_entry cpu_map_entry_init[MAXCPU][VMEPERCPU];
static struct vm_map map_init[MAX_KMAP];

static int randomize_mmap;
SYSCTL_INT(_vm, OID_AUTO, randomize_mmap, CTLFLAG_RW, &randomize_mmap, 0,
    "Randomize mmap offsets");
static int vm_map_relock_enable = 1;
SYSCTL_INT(_vm, OID_AUTO, map_relock_enable, CTLFLAG_RW,
	   &vm_map_relock_enable, 0, "Randomize mmap offsets");

static void vm_map_entry_shadow(vm_map_entry_t entry, int addref);
static vm_map_entry_t vm_map_entry_create(vm_map_t map, int *);
static void vm_map_entry_dispose (vm_map_t map, vm_map_entry_t entry, int *);
static void _vm_map_clip_end (vm_map_t, vm_map_entry_t, vm_offset_t, int *);
static void _vm_map_clip_start (vm_map_t, vm_map_entry_t, vm_offset_t, int *);
static void vm_map_entry_delete (vm_map_t, vm_map_entry_t, int *);
static void vm_map_entry_unwire (vm_map_t, vm_map_entry_t);
static void vm_map_copy_entry (vm_map_t, vm_map_t, vm_map_entry_t,
		vm_map_entry_t);
static void vm_map_unclip_range (vm_map_t map, vm_map_entry_t start_entry, vm_offset_t start, vm_offset_t end, int *count, int flags);

/*
 * Initialize the vm_map module.  Must be called before any other vm_map
 * routines.
 *
 * Map and entry structures are allocated from the general purpose
 * memory pool with some exceptions:
 *
 *	- The kernel map is allocated statically.
 *	- Initial kernel map entries are allocated out of a static pool.
 *	- We must set ZONE_SPECIAL here or the early boot code can get
 *	  stuck if there are >63 cores.
 *
 *	These restrictions are necessary since malloc() uses the
 *	maps and requires map entries.
 *
 * Called from the low level boot code only.
 */
void
vm_map_startup(void)
{
	mapzone = &mapzone_store;
	zbootinit(mapzone, "MAP", sizeof (struct vm_map),
		map_init, MAX_KMAP);
	mapentzone = &mapentzone_store;
	zbootinit(mapentzone, "MAP ENTRY", sizeof (struct vm_map_entry),
		  map_entry_init, MAX_MAPENT);
	mapentzone_store.zflags |= ZONE_SPECIAL;
}

/*
 * Called prior to any vmspace allocations.
 *
 * Called from the low level boot code only.
 */
void
vm_init2(void) 
{
	zinitna(mapentzone, &mapentobj, NULL, 0, 0, 
		ZONE_USE_RESERVE | ZONE_SPECIAL, 1);
	zinitna(mapzone, &mapobj, NULL, 0, 0, 0, 1);
	pmap_init2();
	vm_object_init2();
}


/*
 * Red black tree functions
 *
 * The caller must hold the related map lock.
 */
static int rb_vm_map_compare(vm_map_entry_t a, vm_map_entry_t b);
RB_GENERATE(vm_map_rb_tree, vm_map_entry, rb_entry, rb_vm_map_compare);

/* a->start is address, and the only field has to be initialized */
static int
rb_vm_map_compare(vm_map_entry_t a, vm_map_entry_t b)
{
	if (a->start < b->start)
		return(-1);
	else if (a->start > b->start)
		return(1);
	return(0);
}

/*
 * Allocate a vmspace structure, including a vm_map and pmap.
 * Initialize numerous fields.  While the initial allocation is zerod,
 * subsequence reuse from the objcache leaves elements of the structure
 * intact (particularly the pmap), so portions must be zerod.
 *
 * The structure is not considered activated until we call sysref_activate().
 *
 * No requirements.
 */
struct vmspace *
vmspace_alloc(vm_offset_t min, vm_offset_t max)
{
	struct vmspace *vm;

	vm = sysref_alloc(&vmspace_sysref_class);
	bzero(&vm->vm_startcopy,
	      (char *)&vm->vm_endcopy - (char *)&vm->vm_startcopy);
	vm_map_init(&vm->vm_map, min, max, NULL);	/* initializes token */

	/*
	 * Use a hold to prevent any additional racing hold from terminating
	 * the vmspace before we manage to activate it.  This also acquires
	 * the token for safety.
	 */
	KKASSERT(vm->vm_holdcount == 0);
	KKASSERT(vm->vm_exitingcnt == 0);
	vmspace_hold(vm);
	pmap_pinit(vmspace_pmap(vm));		/* (some fields reused) */
	vm->vm_map.pmap = vmspace_pmap(vm);		/* XXX */
	vm->vm_shm = NULL;
	vm->vm_flags = 0;
	cpu_vmspace_alloc(vm);
	sysref_activate(&vm->vm_sysref);
	vmspace_drop(vm);

	return (vm);
}

/*
 * Free a primary reference to a vmspace.  This can trigger a
 * stage-1 termination.
 */
void
vmspace_free(struct vmspace *vm)
{
	/*
	 * We want all finalization to occur via vmspace_drop() so we
	 * need to hold the vm around the put.
	 */
	vmspace_hold(vm);
	sysref_put(&vm->vm_sysref);
	vmspace_drop(vm);
}

void
vmspace_ref(struct vmspace *vm)
{
	sysref_get(&vm->vm_sysref);
}

void
vmspace_hold(struct vmspace *vm)
{
	refcount_acquire(&vm->vm_holdcount);
	lwkt_gettoken(&vm->vm_map.token);
}

void
vmspace_drop(struct vmspace *vm)
{
	lwkt_reltoken(&vm->vm_map.token);
	if (refcount_release(&vm->vm_holdcount)) {
		if (vm->vm_exitingcnt == 0 &&
		    sysref_isinactive(&vm->vm_sysref)) {
			vmspace_terminate(vm);
		}
	}
}

/*
 * dtor function - Some elements of the pmap are retained in the
 * free-cached vmspaces to improve performance.  We have to clean them up
 * here before returning the vmspace to the memory pool.
 *
 * No requirements.
 */
static void
vmspace_dtor(void *obj, void *private)
{
	struct vmspace *vm = obj;

	pmap_puninit(vmspace_pmap(vm));
}

/*
 * Called in three cases:
 *
 * (1) When the last sysref is dropped and the vmspace becomes inactive.
 *     (holdcount will not be 0 because the vmspace is held through the op)
 *
 * (2) When exitingcount becomes 0 on the last reap
 *     (holdcount will not be 0 because the vmspace is held through the op)
 *
 * (3) When the holdcount becomes 0 in addition to the above two
 *
 * sysref will not scrap the object until we call sysref_put() once more
 * after the last ref has been dropped.
 *
 * VMSPACE_EXIT1 flags the primary deactivation
 * VMSPACE_EXIT2 flags the last reap
 */
static void
vmspace_terminate(struct vmspace *vm)
{
	int count;

	/*
	 *
	 */
	lwkt_gettoken(&vm->vm_map.token);
	if ((vm->vm_flags & VMSPACE_EXIT1) == 0) {
		vm->vm_flags |= VMSPACE_EXIT1;
		shmexit(vm);
		pmap_remove_pages(vmspace_pmap(vm), VM_MIN_USER_ADDRESS,
				  VM_MAX_USER_ADDRESS);
		vm_map_remove(&vm->vm_map, VM_MIN_USER_ADDRESS,
			      VM_MAX_USER_ADDRESS);
	}
	if ((vm->vm_flags & VMSPACE_EXIT2) == 0 && vm->vm_exitingcnt == 0) {
		vm->vm_flags |= VMSPACE_EXIT2;
		cpu_vmspace_free(vm);
		shmexit(vm);

		/*
		 * Lock the map, to wait out all other references to it.
		 * Delete all of the mappings and pages they hold, then call
		 * the pmap module to reclaim anything left.
		 */
		count = vm_map_entry_reserve(MAP_RESERVE_COUNT);
		vm_map_lock(&vm->vm_map);
		vm_map_delete(&vm->vm_map, vm->vm_map.min_offset,
			      vm->vm_map.max_offset, &count);
		vm_map_unlock(&vm->vm_map);
		vm_map_entry_release(count);

		lwkt_gettoken(&vmspace_pmap(vm)->pm_token);
		pmap_release(vmspace_pmap(vm));
		lwkt_reltoken(&vmspace_pmap(vm)->pm_token);
	}

	lwkt_reltoken(&vm->vm_map.token);
	if (vm->vm_exitingcnt == 0 && vm->vm_holdcount == 0) {
		KKASSERT(vm->vm_flags & VMSPACE_EXIT1);
		KKASSERT(vm->vm_flags & VMSPACE_EXIT2);
		sysref_put(&vm->vm_sysref);
	}
}

/*
 * vmspaces are not currently locked.
 */
static void
vmspace_lock(struct vmspace *vm __unused)
{
}

static void
vmspace_unlock(struct vmspace *vm __unused)
{
}

/*
 * This is called during exit indicating that the vmspace is no
 * longer in used by an exiting process, but the process has not yet
 * been reaped.
 *
 * No requirements.
 */
void
vmspace_exitbump(struct vmspace *vm)
{
	vmspace_hold(vm);
	++vm->vm_exitingcnt;
	vmspace_drop(vm);	/* handles termination sequencing */
}

/*
 * Decrement the exitingcnt and issue the stage-2 termination if it becomes
 * zero and the stage1 termination has already occured.
 *
 * No requirements.
 */
void
vmspace_exitfree(struct proc *p)
{
	struct vmspace *vm;

	vm = p->p_vmspace;
	p->p_vmspace = NULL;
	vmspace_hold(vm);
	KKASSERT(vm->vm_exitingcnt > 0);
	if (--vm->vm_exitingcnt == 0 && sysref_isinactive(&vm->vm_sysref))
		vmspace_terminate(vm);
	vmspace_drop(vm);	/* handles termination sequencing */
}

/*
 * Swap useage is determined by taking the proportional swap used by
 * VM objects backing the VM map.  To make up for fractional losses,
 * if the VM object has any swap use at all the associated map entries
 * count for at least 1 swap page.
 *
 * No requirements.
 */
int
vmspace_swap_count(struct vmspace *vm)
{
	vm_map_t map = &vm->vm_map;
	vm_map_entry_t cur;
	vm_object_t object;
	int count = 0;
	int n;

	vmspace_hold(vm);
	for (cur = map->header.next; cur != &map->header; cur = cur->next) {
		switch(cur->maptype) {
		case VM_MAPTYPE_NORMAL:
		case VM_MAPTYPE_VPAGETABLE:
			if ((object = cur->object.vm_object) == NULL)
				break;
			if (object->swblock_count) {
				n = (cur->end - cur->start) / PAGE_SIZE;
				count += object->swblock_count *
				    SWAP_META_PAGES * n / object->size + 1;
			}
			break;
		default:
			break;
		}
	}
	vmspace_drop(vm);

	return(count);
}

/*
 * Calculate the approximate number of anonymous pages in use by
 * this vmspace.  To make up for fractional losses, we count each
 * VM object as having at least 1 anonymous page.
 *
 * No requirements.
 */
int
vmspace_anonymous_count(struct vmspace *vm)
{
	vm_map_t map = &vm->vm_map;
	vm_map_entry_t cur;
	vm_object_t object;
	int count = 0;

	vmspace_hold(vm);
	for (cur = map->header.next; cur != &map->header; cur = cur->next) {
		switch(cur->maptype) {
		case VM_MAPTYPE_NORMAL:
		case VM_MAPTYPE_VPAGETABLE:
			if ((object = cur->object.vm_object) == NULL)
				break;
			if (object->type != OBJT_DEFAULT &&
			    object->type != OBJT_SWAP) {
				break;
			}
			count += object->resident_page_count;
			break;
		default:
			break;
		}
	}
	vmspace_drop(vm);

	return(count);
}

/*
 * Creates and returns a new empty VM map with the given physical map
 * structure, and having the given lower and upper address bounds.
 *
 * No requirements.
 */
vm_map_t
vm_map_create(vm_map_t result, pmap_t pmap, vm_offset_t min, vm_offset_t max)
{
	if (result == NULL)
		result = zalloc(mapzone);
	vm_map_init(result, min, max, pmap);
	return (result);
}

/*
 * Initialize an existing vm_map structure such as that in the vmspace
 * structure.  The pmap is initialized elsewhere.
 *
 * No requirements.
 */
void
vm_map_init(struct vm_map *map, vm_offset_t min, vm_offset_t max, pmap_t pmap)
{
	map->header.next = map->header.prev = &map->header;
	RB_INIT(&map->rb_root);
	map->nentries = 0;
	map->size = 0;
	map->system_map = 0;
	map->min_offset = min;
	map->max_offset = max;
	map->pmap = pmap;
	map->first_free = &map->header;
	map->hint = &map->header;
	map->timestamp = 0;
	map->flags = 0;
	lwkt_token_init(&map->token, "vm_map");
	lockinit(&map->lock, "vm_maplk", (hz + 9) / 10, 0);
	TUNABLE_INT("vm.cache_vmspaces", &vmspace_sysref_class.nom_cache);
}

/*
 * Shadow the vm_map_entry's object.  This typically needs to be done when
 * a write fault is taken on an entry which had previously been cloned by
 * fork().  The shared object (which might be NULL) must become private so
 * we add a shadow layer above it.
 *
 * Object allocation for anonymous mappings is defered as long as possible.
 * When creating a shadow, however, the underlying object must be instantiated
 * so it can be shared.
 *
 * If the map segment is governed by a virtual page table then it is
 * possible to address offsets beyond the mapped area.  Just allocate
 * a maximally sized object for this case.
 *
 * The vm_map must be exclusively locked.
 * No other requirements.
 */
static
void
vm_map_entry_shadow(vm_map_entry_t entry, int addref)
{
	if (entry->maptype == VM_MAPTYPE_VPAGETABLE) {
		vm_object_shadow(&entry->object.vm_object, &entry->offset,
				 0x7FFFFFFF, addref);	/* XXX */
	} else {
		vm_object_shadow(&entry->object.vm_object, &entry->offset,
				 atop(entry->end - entry->start), addref);
	}
	entry->eflags &= ~MAP_ENTRY_NEEDS_COPY;
}

/*
 * Allocate an object for a vm_map_entry.
 *
 * Object allocation for anonymous mappings is defered as long as possible.
 * This function is called when we can defer no longer, generally when a map
 * entry might be split or forked or takes a page fault.
 *
 * If the map segment is governed by a virtual page table then it is
 * possible to address offsets beyond the mapped area.  Just allocate
 * a maximally sized object for this case.
 *
 * The vm_map must be exclusively locked.
 * No other requirements.
 */
void 
vm_map_entry_allocate_object(vm_map_entry_t entry)
{
	vm_object_t obj;

	if (entry->maptype == VM_MAPTYPE_VPAGETABLE) {
		obj = vm_object_allocate(OBJT_DEFAULT, 0x7FFFFFFF); /* XXX */
	} else {
		obj = vm_object_allocate(OBJT_DEFAULT,
					 atop(entry->end - entry->start));
	}
	entry->object.vm_object = obj;
	entry->offset = 0;
}

/*
 * Set an initial negative count so the first attempt to reserve
 * space preloads a bunch of vm_map_entry's for this cpu.  Also
 * pre-allocate 2 vm_map_entries which will be needed by zalloc() to
 * map a new page for vm_map_entry structures.  SMP systems are
 * particularly sensitive.
 *
 * This routine is called in early boot so we cannot just call
 * vm_map_entry_reserve().
 *
 * Called from the low level boot code only (for each cpu)
 */
void
vm_map_entry_reserve_cpu_init(globaldata_t gd)
{
	vm_map_entry_t entry;
	int i;

	gd->gd_vme_avail -= MAP_RESERVE_COUNT * 2;
	entry = &cpu_map_entry_init[gd->gd_cpuid][0];
	for (i = 0; i < VMEPERCPU; ++i, ++entry) {
		entry->next = gd->gd_vme_base;
		gd->gd_vme_base = entry;
	}
}

/*
 * Reserves vm_map_entry structures so code later on can manipulate
 * map_entry structures within a locked map without blocking trying
 * to allocate a new vm_map_entry.
 *
 * No requirements.
 */
int
vm_map_entry_reserve(int count)
{
	struct globaldata *gd = mycpu;
	vm_map_entry_t entry;

	/*
	 * Make sure we have enough structures in gd_vme_base to handle
	 * the reservation request.
	 *
	 * The critical section protects access to the per-cpu gd.
	 */
	crit_enter();
	while (gd->gd_vme_avail < count) {
		entry = zalloc(mapentzone);
		entry->next = gd->gd_vme_base;
		gd->gd_vme_base = entry;
		++gd->gd_vme_avail;
	}
	gd->gd_vme_avail -= count;
	crit_exit();

	return(count);
}

/*
 * Releases previously reserved vm_map_entry structures that were not
 * used.  If we have too much junk in our per-cpu cache clean some of
 * it out.
 *
 * No requirements.
 */
void
vm_map_entry_release(int count)
{
	struct globaldata *gd = mycpu;
	vm_map_entry_t entry;

	crit_enter();
	gd->gd_vme_avail += count;
	while (gd->gd_vme_avail > MAP_RESERVE_SLOP) {
		entry = gd->gd_vme_base;
		KKASSERT(entry != NULL);
		gd->gd_vme_base = entry->next;
		--gd->gd_vme_avail;
		crit_exit();
		zfree(mapentzone, entry);
		crit_enter();
	}
	crit_exit();
}

/*
 * Reserve map entry structures for use in kernel_map itself.  These
 * entries have *ALREADY* been reserved on a per-cpu basis when the map
 * was inited.  This function is used by zalloc() to avoid a recursion
 * when zalloc() itself needs to allocate additional kernel memory.
 *
 * This function works like the normal reserve but does not load the
 * vm_map_entry cache (because that would result in an infinite
 * recursion).  Note that gd_vme_avail may go negative.  This is expected.
 *
 * Any caller of this function must be sure to renormalize after
 * potentially eating entries to ensure that the reserve supply
 * remains intact.
 *
 * No requirements.
 */
int
vm_map_entry_kreserve(int count)
{
	struct globaldata *gd = mycpu;

	crit_enter();
	gd->gd_vme_avail -= count;
	crit_exit();
	KASSERT(gd->gd_vme_base != NULL,
		("no reserved entries left, gd_vme_avail = %d",
		gd->gd_vme_avail));
	return(count);
}

/*
 * Release previously reserved map entries for kernel_map.  We do not
 * attempt to clean up like the normal release function as this would
 * cause an unnecessary (but probably not fatal) deep procedure call.
 *
 * No requirements.
 */
void
vm_map_entry_krelease(int count)
{
	struct globaldata *gd = mycpu;

	crit_enter();
	gd->gd_vme_avail += count;
	crit_exit();
}

/*
 * Allocates a VM map entry for insertion.  No entry fields are filled in.
 *
 * The entries should have previously been reserved.  The reservation count
 * is tracked in (*countp).
 *
 * No requirements.
 */
static vm_map_entry_t
vm_map_entry_create(vm_map_t map, int *countp)
{
	struct globaldata *gd = mycpu;
	vm_map_entry_t entry;

	KKASSERT(*countp > 0);
	--*countp;
	crit_enter();
	entry = gd->gd_vme_base;
	KASSERT(entry != NULL, ("gd_vme_base NULL! count %d", *countp));
	gd->gd_vme_base = entry->next;
	crit_exit();

	return(entry);
}

/*
 * Dispose of a vm_map_entry that is no longer being referenced.
 *
 * No requirements.
 */
static void
vm_map_entry_dispose(vm_map_t map, vm_map_entry_t entry, int *countp)
{
	struct globaldata *gd = mycpu;

	KKASSERT(map->hint != entry);
	KKASSERT(map->first_free != entry);

	++*countp;
	crit_enter();
	entry->next = gd->gd_vme_base;
	gd->gd_vme_base = entry;
	crit_exit();
}


/*
 * Insert/remove entries from maps.
 *
 * The related map must be exclusively locked.
 * The caller must hold map->token
 * No other requirements.
 */
static __inline void
vm_map_entry_link(vm_map_t map,
		  vm_map_entry_t after_where,
		  vm_map_entry_t entry)
{
	ASSERT_VM_MAP_LOCKED(map);

	map->nentries++;
	entry->prev = after_where;
	entry->next = after_where->next;
	entry->next->prev = entry;
	after_where->next = entry;
	if (vm_map_rb_tree_RB_INSERT(&map->rb_root, entry))
		panic("vm_map_entry_link: dup addr map %p ent %p", map, entry);
}

static __inline void
vm_map_entry_unlink(vm_map_t map,
		    vm_map_entry_t entry)
{
	vm_map_entry_t prev;
	vm_map_entry_t next;

	ASSERT_VM_MAP_LOCKED(map);

	if (entry->eflags & MAP_ENTRY_IN_TRANSITION) {
		panic("vm_map_entry_unlink: attempt to mess with "
		      "locked entry! %p", entry);
	}
	prev = entry->prev;
	next = entry->next;
	next->prev = prev;
	prev->next = next;
	vm_map_rb_tree_RB_REMOVE(&map->rb_root, entry);
	map->nentries--;
}

/*
 * Finds the map entry containing (or immediately preceding) the specified
 * address in the given map.  The entry is returned in (*entry).
 *
 * The boolean result indicates whether the address is actually contained
 * in the map.
 *
 * The related map must be locked.
 * No other requirements.
 */
boolean_t
vm_map_lookup_entry(vm_map_t map, vm_offset_t address, vm_map_entry_t *entry)
{
	vm_map_entry_t tmp;
	vm_map_entry_t last;

	ASSERT_VM_MAP_LOCKED(map);
#if 0
	/*
	 * XXX TEMPORARILY DISABLED.  For some reason our attempt to revive
	 * the hint code with the red-black lookup meets with system crashes
	 * and lockups.  We do not yet know why.
	 *
	 * It is possible that the problem is related to the setting
	 * of the hint during map_entry deletion, in the code specified
	 * at the GGG comment later on in this file.
	 *
	 * YYY More likely it's because this function can be called with
	 * a shared lock on the map, resulting in map->hint updates possibly
	 * racing.  Fixed now but untested.
	 */
	/*
	 * Quickly check the cached hint, there's a good chance of a match.
	 */
	tmp = map->hint;
	cpu_ccfence();
	if (tmp != &map->header) {
		if (address >= tmp->start && address < tmp->end) {
			*entry = tmp;
			return(TRUE);
		}
	}
#endif

	/*
	 * Locate the record from the top of the tree.  'last' tracks the
	 * closest prior record and is returned if no match is found, which
	 * in binary tree terms means tracking the most recent right-branch
	 * taken.  If there is no prior record, &map->header is returned.
	 */
	last = &map->header;
	tmp = RB_ROOT(&map->rb_root);

	while (tmp) {
		if (address >= tmp->start) {
			if (address < tmp->end) {
				*entry = tmp;
				map->hint = tmp;
				return(TRUE);
			}
			last = tmp;
			tmp = RB_RIGHT(tmp, rb_entry);
		} else {
			tmp = RB_LEFT(tmp, rb_entry);
		}
	}
	*entry = last;
	return (FALSE);
}

/*
 * Inserts the given whole VM object into the target map at the specified
 * address range.  The object's size should match that of the address range.
 *
 * The map must be exclusively locked.
 * The object must be held.
 * The caller must have reserved sufficient vm_map_entry structures.
 *
 * If object is non-NULL, ref count must be bumped by caller prior to
 * making call to account for the new entry.
 */
int
vm_map_insert(vm_map_t map, int *countp,
	      vm_object_t object, vm_ooffset_t offset,
	      vm_offset_t start, vm_offset_t end,
	      vm_maptype_t maptype,
	      vm_prot_t prot, vm_prot_t max,
	      int cow)
{
	vm_map_entry_t new_entry;
	vm_map_entry_t prev_entry;
	vm_map_entry_t temp_entry;
	vm_eflags_t protoeflags;
	int must_drop = 0;

	ASSERT_VM_MAP_LOCKED(map);
	if (object)
		ASSERT_LWKT_TOKEN_HELD(vm_object_token(object));

	/*
	 * Check that the start and end points are not bogus.
	 */
	if ((start < map->min_offset) || (end > map->max_offset) ||
	    (start >= end))
		return (KERN_INVALID_ADDRESS);

	/*
	 * Find the entry prior to the proposed starting address; if it's part
	 * of an existing entry, this range is bogus.
	 */
	if (vm_map_lookup_entry(map, start, &temp_entry))
		return (KERN_NO_SPACE);

	prev_entry = temp_entry;

	/*
	 * Assert that the next entry doesn't overlap the end point.
	 */

	if ((prev_entry->next != &map->header) &&
	    (prev_entry->next->start < end))
		return (KERN_NO_SPACE);

	protoeflags = 0;

	if (cow & MAP_COPY_ON_WRITE)
		protoeflags |= MAP_ENTRY_COW|MAP_ENTRY_NEEDS_COPY;

	if (cow & MAP_NOFAULT) {
		protoeflags |= MAP_ENTRY_NOFAULT;

		KASSERT(object == NULL,
			("vm_map_insert: paradoxical MAP_NOFAULT request"));
	}
	if (cow & MAP_DISABLE_SYNCER)
		protoeflags |= MAP_ENTRY_NOSYNC;
	if (cow & MAP_DISABLE_COREDUMP)
		protoeflags |= MAP_ENTRY_NOCOREDUMP;
	if (cow & MAP_IS_STACK)
		protoeflags |= MAP_ENTRY_STACK;
	if (cow & MAP_IS_KSTACK)
		protoeflags |= MAP_ENTRY_KSTACK;

	lwkt_gettoken(&map->token);

	if (object) {
		/*
		 * When object is non-NULL, it could be shared with another
		 * process.  We have to set or clear OBJ_ONEMAPPING 
		 * appropriately.
		 *
		 * NOTE: This flag is only applicable to DEFAULT and SWAP
		 *	 objects and will already be clear in other types
		 *	 of objects, so a shared object lock is ok for
		 *	 VNODE objects.
		 */
		if ((object->ref_count > 1) || (object->shadow_count != 0)) {
			vm_object_clear_flag(object, OBJ_ONEMAPPING);
		}
	}
	else if ((prev_entry != &map->header) &&
		 (prev_entry->eflags == protoeflags) &&
		 (prev_entry->end == start) &&
		 (prev_entry->wired_count == 0) &&
		 prev_entry->maptype == maptype &&
		 ((prev_entry->object.vm_object == NULL) ||
		  vm_object_coalesce(prev_entry->object.vm_object,
				     OFF_TO_IDX(prev_entry->offset),
				     (vm_size_t)(prev_entry->end - prev_entry->start),
				     (vm_size_t)(end - prev_entry->end)))) {
		/*
		 * We were able to extend the object.  Determine if we
		 * can extend the previous map entry to include the 
		 * new range as well.
		 */
		if ((prev_entry->inheritance == VM_INHERIT_DEFAULT) &&
		    (prev_entry->protection == prot) &&
		    (prev_entry->max_protection == max)) {
			map->size += (end - prev_entry->end);
			prev_entry->end = end;
			vm_map_simplify_entry(map, prev_entry, countp);
			lwkt_reltoken(&map->token);
			return (KERN_SUCCESS);
		}

		/*
		 * If we can extend the object but cannot extend the
		 * map entry, we have to create a new map entry.  We
		 * must bump the ref count on the extended object to
		 * account for it.  object may be NULL.
		 */
		object = prev_entry->object.vm_object;
		offset = prev_entry->offset +
			(prev_entry->end - prev_entry->start);
		if (object) {
			vm_object_hold(object);
			vm_object_chain_wait(object, 0);
			vm_object_reference_locked(object);
			must_drop = 1;
		}
	}

	/*
	 * NOTE: if conditionals fail, object can be NULL here.  This occurs
	 * in things like the buffer map where we manage kva but do not manage
	 * backing objects.
	 */

	/*
	 * Create a new entry
	 */

	new_entry = vm_map_entry_create(map, countp);
	new_entry->start = start;
	new_entry->end = end;

	new_entry->maptype = maptype;
	new_entry->eflags = protoeflags;
	new_entry->object.vm_object = object;
	new_entry->offset = offset;
	new_entry->aux.master_pde = 0;

	new_entry->inheritance = VM_INHERIT_DEFAULT;
	new_entry->protection = prot;
	new_entry->max_protection = max;
	new_entry->wired_count = 0;

	/*
	 * Insert the new entry into the list
	 */

	vm_map_entry_link(map, prev_entry, new_entry);
	map->size += new_entry->end - new_entry->start;

	/*
	 * Update the free space hint.  Entries cannot overlap.
	 * An exact comparison is needed to avoid matching
	 * against the map->header.
	 */
	if ((map->first_free == prev_entry) &&
	    (prev_entry->end == new_entry->start)) {
		map->first_free = new_entry;
	}

#if 0
	/*
	 * Temporarily removed to avoid MAP_STACK panic, due to
	 * MAP_STACK being a huge hack.  Will be added back in
	 * when MAP_STACK (and the user stack mapping) is fixed.
	 */
	/*
	 * It may be possible to simplify the entry
	 */
	vm_map_simplify_entry(map, new_entry, countp);
#endif

	/*
	 * Try to pre-populate the page table.  Mappings governed by virtual
	 * page tables cannot be prepopulated without a lot of work, so
	 * don't try.
	 */
	if ((cow & (MAP_PREFAULT|MAP_PREFAULT_PARTIAL)) &&
	    maptype != VM_MAPTYPE_VPAGETABLE) {
		int dorelock = 0;
		if (vm_map_relock_enable && (cow & MAP_PREFAULT_RELOCK)) {
			dorelock = 1;
			vm_object_lock_swap();
			vm_object_drop(object);
		}
		pmap_object_init_pt(map->pmap, start, prot,
				    object, OFF_TO_IDX(offset), end - start,
				    cow & MAP_PREFAULT_PARTIAL);
		if (dorelock) {
			vm_object_hold(object);
			vm_object_lock_swap();
		}
	}
	if (must_drop)
		vm_object_drop(object);

	lwkt_reltoken(&map->token);
	return (KERN_SUCCESS);
}

/*
 * Find sufficient space for `length' bytes in the given map, starting at
 * `start'.  Returns 0 on success, 1 on no space.
 *
 * This function will returned an arbitrarily aligned pointer.  If no
 * particular alignment is required you should pass align as 1.  Note that
 * the map may return PAGE_SIZE aligned pointers if all the lengths used in
 * the map are a multiple of PAGE_SIZE, even if you pass a smaller align
 * argument.
 *
 * 'align' should be a power of 2 but is not required to be.
 *
 * The map must be exclusively locked.
 * No other requirements.
 */
int
vm_map_findspace(vm_map_t map, vm_offset_t start, vm_size_t length,
		 vm_size_t align, int flags, vm_offset_t *addr)
{
	vm_map_entry_t entry, next;
	vm_offset_t end;
	vm_offset_t align_mask;

	if (start < map->min_offset)
		start = map->min_offset;
	if (start > map->max_offset)
		return (1);

	/*
	 * If the alignment is not a power of 2 we will have to use
	 * a mod/division, set align_mask to a special value.
	 */
	if ((align | (align - 1)) + 1 != (align << 1))
		align_mask = (vm_offset_t)-1;
	else
		align_mask = align - 1;

	/*
	 * Look for the first possible address; if there's already something
	 * at this address, we have to start after it.
	 */
	if (start == map->min_offset) {
		if ((entry = map->first_free) != &map->header)
			start = entry->end;
	} else {
		vm_map_entry_t tmp;

		if (vm_map_lookup_entry(map, start, &tmp))
			start = tmp->end;
		entry = tmp;
	}

	/*
	 * Look through the rest of the map, trying to fit a new region in the
	 * gap between existing regions, or after the very last region.
	 */
	for (;; start = (entry = next)->end) {
		/*
		 * Adjust the proposed start by the requested alignment,
		 * be sure that we didn't wrap the address.
		 */
		if (align_mask == (vm_offset_t)-1)
			end = ((start + align - 1) / align) * align;
		else
			end = (start + align_mask) & ~align_mask;
		if (end < start)
			return (1);
		start = end;
		/*
		 * Find the end of the proposed new region.  Be sure we didn't
		 * go beyond the end of the map, or wrap around the address.
		 * Then check to see if this is the last entry or if the 
		 * proposed end fits in the gap between this and the next
		 * entry.
		 */
		end = start + length;
		if (end > map->max_offset || end < start)
			return (1);
		next = entry->next;

		/*
		 * If the next entry's start address is beyond the desired
		 * end address we may have found a good entry.
		 *
		 * If the next entry is a stack mapping we do not map into
		 * the stack's reserved space.
		 *
		 * XXX continue to allow mapping into the stack's reserved
		 * space if doing a MAP_STACK mapping inside a MAP_STACK
		 * mapping, for backwards compatibility.  But the caller
		 * really should use MAP_STACK | MAP_TRYFIXED if they
		 * want to do that.
		 */
		if (next == &map->header)
			break;
		if (next->start >= end) {
			if ((next->eflags & MAP_ENTRY_STACK) == 0)
				break;
			if (flags & MAP_STACK)
				break;
			if (next->start - next->aux.avail_ssize >= end)
				break;
		}
	}
	map->hint = entry;

	/*
	 * Grow the kernel_map if necessary.  pmap_growkernel() will panic
	 * if it fails.  The kernel_map is locked and nothing can steal
	 * our address space if pmap_growkernel() blocks.
	 *
	 * NOTE: This may be unconditionally called for kldload areas on
	 *	 x86_64 because these do not bump kernel_vm_end (which would
	 *	 fill 128G worth of page tables!).  Therefore we must not
	 *	 retry.
	 */
	if (map == &kernel_map) {
		vm_offset_t kstop;

		kstop = round_page(start + length);
		if (kstop > kernel_vm_end)
			pmap_growkernel(start, kstop);
	}
	*addr = start;
	return (0);
}

/*
 * vm_map_find finds an unallocated region in the target address map with
 * the given length and allocates it.  The search is defined to be first-fit
 * from the specified address; the region found is returned in the same
 * parameter.
 *
 * If object is non-NULL, ref count must be bumped by caller
 * prior to making call to account for the new entry.
 *
 * No requirements.  This function will lock the map temporarily.
 */
int
vm_map_find(vm_map_t map, vm_object_t object, vm_ooffset_t offset,
	    vm_offset_t *addr,	vm_size_t length, vm_size_t align,
	    boolean_t fitit,
	    vm_maptype_t maptype,
	    vm_prot_t prot, vm_prot_t max,
	    int cow)
{
	vm_offset_t start;
	int result;
	int count;

	start = *addr;

	count = vm_map_entry_reserve(MAP_RESERVE_COUNT);
	vm_map_lock(map);
	if (object)
		vm_object_hold_shared(object);
	if (fitit) {
		if (vm_map_findspace(map, start, length, align, 0, addr)) {
			if (object)
				vm_object_drop(object);
			vm_map_unlock(map);
			vm_map_entry_release(count);
			return (KERN_NO_SPACE);
		}
		start = *addr;
	}
	result = vm_map_insert(map, &count, object, offset,
			       start, start + length,
			       maptype,
			       prot, max,
			       cow);
	if (object)
		vm_object_drop(object);
	vm_map_unlock(map);
	vm_map_entry_release(count);

	return (result);
}

/*
 * Simplify the given map entry by merging with either neighbor.  This
 * routine also has the ability to merge with both neighbors.
 *
 * This routine guarentees that the passed entry remains valid (though
 * possibly extended).  When merging, this routine may delete one or
 * both neighbors.  No action is taken on entries which have their
 * in-transition flag set.
 *
 * The map must be exclusively locked.
 */
void
vm_map_simplify_entry(vm_map_t map, vm_map_entry_t entry, int *countp)
{
	vm_map_entry_t next, prev;
	vm_size_t prevsize, esize;

	if (entry->eflags & MAP_ENTRY_IN_TRANSITION) {
		++mycpu->gd_cnt.v_intrans_coll;
		return;
	}

	if (entry->maptype == VM_MAPTYPE_SUBMAP)
		return;

	prev = entry->prev;
	if (prev != &map->header) {
		prevsize = prev->end - prev->start;
		if ( (prev->end == entry->start) &&
		     (prev->maptype == entry->maptype) &&
		     (prev->object.vm_object == entry->object.vm_object) &&
		     (!prev->object.vm_object ||
			(prev->offset + prevsize == entry->offset)) &&
		     (prev->eflags == entry->eflags) &&
		     (prev->protection == entry->protection) &&
		     (prev->max_protection == entry->max_protection) &&
		     (prev->inheritance == entry->inheritance) &&
		     (prev->wired_count == entry->wired_count)) {
			if (map->first_free == prev)
				map->first_free = entry;
			if (map->hint == prev)
				map->hint = entry;
			vm_map_entry_unlink(map, prev);
			entry->start = prev->start;
			entry->offset = prev->offset;
			if (prev->object.vm_object)
				vm_object_deallocate(prev->object.vm_object);
			vm_map_entry_dispose(map, prev, countp);
		}
	}

	next = entry->next;
	if (next != &map->header) {
		esize = entry->end - entry->start;
		if ((entry->end == next->start) &&
		    (next->maptype == entry->maptype) &&
		    (next->object.vm_object == entry->object.vm_object) &&
		     (!entry->object.vm_object ||
			(entry->offset + esize == next->offset)) &&
		    (next->eflags == entry->eflags) &&
		    (next->protection == entry->protection) &&
		    (next->max_protection == entry->max_protection) &&
		    (next->inheritance == entry->inheritance) &&
		    (next->wired_count == entry->wired_count)) {
			if (map->first_free == next)
				map->first_free = entry;
			if (map->hint == next)
				map->hint = entry;
			vm_map_entry_unlink(map, next);
			entry->end = next->end;
			if (next->object.vm_object)
				vm_object_deallocate(next->object.vm_object);
			vm_map_entry_dispose(map, next, countp);
	        }
	}
}

/*
 * Asserts that the given entry begins at or after the specified address.
 * If necessary, it splits the entry into two.
 */
#define vm_map_clip_start(map, entry, startaddr, countp)		\
{									\
	if (startaddr > entry->start)					\
		_vm_map_clip_start(map, entry, startaddr, countp);	\
}

/*
 * This routine is called only when it is known that the entry must be split.
 *
 * The map must be exclusively locked.
 */
static void
_vm_map_clip_start(vm_map_t map, vm_map_entry_t entry, vm_offset_t start,
		   int *countp)
{
	vm_map_entry_t new_entry;

	/*
	 * Split off the front portion -- note that we must insert the new
	 * entry BEFORE this one, so that this entry has the specified
	 * starting address.
	 */

	vm_map_simplify_entry(map, entry, countp);

	/*
	 * If there is no object backing this entry, we might as well create
	 * one now.  If we defer it, an object can get created after the map
	 * is clipped, and individual objects will be created for the split-up
	 * map.  This is a bit of a hack, but is also about the best place to
	 * put this improvement.
	 */
	if (entry->object.vm_object == NULL && !map->system_map) {
		vm_map_entry_allocate_object(entry);
	}

	new_entry = vm_map_entry_create(map, countp);
	*new_entry = *entry;

	new_entry->end = start;
	entry->offset += (start - entry->start);
	entry->start = start;

	vm_map_entry_link(map, entry->prev, new_entry);

	switch(entry->maptype) {
	case VM_MAPTYPE_NORMAL:
	case VM_MAPTYPE_VPAGETABLE:
		if (new_entry->object.vm_object) {
			vm_object_hold(new_entry->object.vm_object);
			vm_object_chain_wait(new_entry->object.vm_object, 0);
			vm_object_reference_locked(new_entry->object.vm_object);
			vm_object_drop(new_entry->object.vm_object);
		}
		break;
	default:
		break;
	}
}

/*
 * Asserts that the given entry ends at or before the specified address.
 * If necessary, it splits the entry into two.
 *
 * The map must be exclusively locked.
 */
#define vm_map_clip_end(map, entry, endaddr, countp)		\
{								\
	if (endaddr < entry->end)				\
		_vm_map_clip_end(map, entry, endaddr, countp);	\
}

/*
 * This routine is called only when it is known that the entry must be split.
 *
 * The map must be exclusively locked.
 */
static void
_vm_map_clip_end(vm_map_t map, vm_map_entry_t entry, vm_offset_t end,
		 int *countp)
{
	vm_map_entry_t new_entry;

	/*
	 * If there is no object backing this entry, we might as well create
	 * one now.  If we defer it, an object can get created after the map
	 * is clipped, and individual objects will be created for the split-up
	 * map.  This is a bit of a hack, but is also about the best place to
	 * put this improvement.
	 */

	if (entry->object.vm_object == NULL && !map->system_map) {
		vm_map_entry_allocate_object(entry);
	}

	/*
	 * Create a new entry and insert it AFTER the specified entry
	 */

	new_entry = vm_map_entry_create(map, countp);
	*new_entry = *entry;

	new_entry->start = entry->end = end;
	new_entry->offset += (end - entry->start);

	vm_map_entry_link(map, entry, new_entry);

	switch(entry->maptype) {
	case VM_MAPTYPE_NORMAL:
	case VM_MAPTYPE_VPAGETABLE:
		if (new_entry->object.vm_object) {
			vm_object_hold(new_entry->object.vm_object);
			vm_object_chain_wait(new_entry->object.vm_object, 0);
			vm_object_reference_locked(new_entry->object.vm_object);
			vm_object_drop(new_entry->object.vm_object);
		}
		break;
	default:
		break;
	}
}

/*
 * Asserts that the starting and ending region addresses fall within the
 * valid range for the map.
 */
#define	VM_MAP_RANGE_CHECK(map, start, end)	\
{						\
	if (start < vm_map_min(map))		\
		start = vm_map_min(map);	\
	if (end > vm_map_max(map))		\
		end = vm_map_max(map);		\
	if (start > end)			\
		start = end;			\
}

/*
 * Used to block when an in-transition collison occurs.  The map
 * is unlocked for the sleep and relocked before the return.
 */
void
vm_map_transition_wait(vm_map_t map)
{
	tsleep_interlock(map, 0);
	vm_map_unlock(map);
	tsleep(map, PINTERLOCKED, "vment", 0);
	vm_map_lock(map);
}

/*
 * When we do blocking operations with the map lock held it is
 * possible that a clip might have occured on our in-transit entry,
 * requiring an adjustment to the entry in our loop.  These macros
 * help the pageable and clip_range code deal with the case.  The
 * conditional costs virtually nothing if no clipping has occured.
 */

#define CLIP_CHECK_BACK(entry, save_start)		\
    do {						\
	    while (entry->start != save_start) {	\
		    entry = entry->prev;		\
		    KASSERT(entry != &map->header, ("bad entry clip")); \
	    }						\
    } while(0)

#define CLIP_CHECK_FWD(entry, save_end)			\
    do {						\
	    while (entry->end != save_end) {		\
		    entry = entry->next;		\
		    KASSERT(entry != &map->header, ("bad entry clip")); \
	    }						\
    } while(0)


/*
 * Clip the specified range and return the base entry.  The
 * range may cover several entries starting at the returned base
 * and the first and last entry in the covering sequence will be
 * properly clipped to the requested start and end address.
 *
 * If no holes are allowed you should pass the MAP_CLIP_NO_HOLES
 * flag.
 *
 * The MAP_ENTRY_IN_TRANSITION flag will be set for the entries
 * covered by the requested range.
 *
 * The map must be exclusively locked on entry and will remain locked
 * on return. If no range exists or the range contains holes and you
 * specified that no holes were allowed, NULL will be returned.  This
 * routine may temporarily unlock the map in order avoid a deadlock when
 * sleeping.
 */
static
vm_map_entry_t
vm_map_clip_range(vm_map_t map, vm_offset_t start, vm_offset_t end, 
		  int *countp, int flags)
{
	vm_map_entry_t start_entry;
	vm_map_entry_t entry;

	/*
	 * Locate the entry and effect initial clipping.  The in-transition
	 * case does not occur very often so do not try to optimize it.
	 */
again:
	if (vm_map_lookup_entry(map, start, &start_entry) == FALSE)
		return (NULL);
	entry = start_entry;
	if (entry->eflags & MAP_ENTRY_IN_TRANSITION) {
		entry->eflags |= MAP_ENTRY_NEEDS_WAKEUP;
		++mycpu->gd_cnt.v_intrans_coll;
		++mycpu->gd_cnt.v_intrans_wait;
		vm_map_transition_wait(map);
		/*
		 * entry and/or start_entry may have been clipped while
		 * we slept, or may have gone away entirely.  We have
		 * to restart from the lookup.
		 */
		goto again;
	}

	/*
	 * Since we hold an exclusive map lock we do not have to restart
	 * after clipping, even though clipping may block in zalloc.
	 */
	vm_map_clip_start(map, entry, start, countp);
	vm_map_clip_end(map, entry, end, countp);
	entry->eflags |= MAP_ENTRY_IN_TRANSITION;

	/*
	 * Scan entries covered by the range.  When working on the next
	 * entry a restart need only re-loop on the current entry which
	 * we have already locked, since 'next' may have changed.  Also,
	 * even though entry is safe, it may have been clipped so we
	 * have to iterate forwards through the clip after sleeping.
	 */
	while (entry->next != &map->header && entry->next->start < end) {
		vm_map_entry_t next = entry->next;

		if (flags & MAP_CLIP_NO_HOLES) {
			if (next->start > entry->end) {
				vm_map_unclip_range(map, start_entry,
					start, entry->end, countp, flags);
				return(NULL);
			}
		}

		if (next->eflags & MAP_ENTRY_IN_TRANSITION) {
			vm_offset_t save_end = entry->end;
			next->eflags |= MAP_ENTRY_NEEDS_WAKEUP;
			++mycpu->gd_cnt.v_intrans_coll;
			++mycpu->gd_cnt.v_intrans_wait;
			vm_map_transition_wait(map);

			/*
			 * clips might have occured while we blocked.
			 */
			CLIP_CHECK_FWD(entry, save_end);
			CLIP_CHECK_BACK(start_entry, start);
			continue;
		}
		/*
		 * No restart necessary even though clip_end may block, we
		 * are holding the map lock.
		 */
		vm_map_clip_end(map, next, end, countp);
		next->eflags |= MAP_ENTRY_IN_TRANSITION;
		entry = next;
	}
	if (flags & MAP_CLIP_NO_HOLES) {
		if (entry->end != end) {
			vm_map_unclip_range(map, start_entry,
				start, entry->end, countp, flags);
			return(NULL);
		}
	}
	return(start_entry);
}

/*
 * Undo the effect of vm_map_clip_range().  You should pass the same
 * flags and the same range that you passed to vm_map_clip_range().
 * This code will clear the in-transition flag on the entries and
 * wake up anyone waiting.  This code will also simplify the sequence
 * and attempt to merge it with entries before and after the sequence.
 *
 * The map must be locked on entry and will remain locked on return.
 *
 * Note that you should also pass the start_entry returned by
 * vm_map_clip_range().  However, if you block between the two calls
 * with the map unlocked please be aware that the start_entry may
 * have been clipped and you may need to scan it backwards to find
 * the entry corresponding with the original start address.  You are
 * responsible for this, vm_map_unclip_range() expects the correct
 * start_entry to be passed to it and will KASSERT otherwise.
 */
static
void
vm_map_unclip_range(vm_map_t map, vm_map_entry_t start_entry,
		    vm_offset_t start, vm_offset_t end,
		    int *countp, int flags)
{
	vm_map_entry_t entry;

	entry = start_entry;

	KASSERT(entry->start == start, ("unclip_range: illegal base entry"));
	while (entry != &map->header && entry->start < end) {
		KASSERT(entry->eflags & MAP_ENTRY_IN_TRANSITION,
			("in-transition flag not set during unclip on: %p",
			entry));
		KASSERT(entry->end <= end,
			("unclip_range: tail wasn't clipped"));
		entry->eflags &= ~MAP_ENTRY_IN_TRANSITION;
		if (entry->eflags & MAP_ENTRY_NEEDS_WAKEUP) {
			entry->eflags &= ~MAP_ENTRY_NEEDS_WAKEUP;
			wakeup(map);
		}
		entry = entry->next;
	}

	/*
	 * Simplification does not block so there is no restart case.
	 */
	entry = start_entry;
	while (entry != &map->header && entry->start < end) {
		vm_map_simplify_entry(map, entry, countp);
		entry = entry->next;
	}
}

/*
 * Mark the given range as handled by a subordinate map.
 *
 * This range must have been created with vm_map_find(), and no other
 * operations may have been performed on this range prior to calling
 * vm_map_submap().
 *
 * Submappings cannot be removed.
 *
 * No requirements.
 */
int
vm_map_submap(vm_map_t map, vm_offset_t start, vm_offset_t end, vm_map_t submap)
{
	vm_map_entry_t entry;
	int result = KERN_INVALID_ARGUMENT;
	int count;

	count = vm_map_entry_reserve(MAP_RESERVE_COUNT);
	vm_map_lock(map);

	VM_MAP_RANGE_CHECK(map, start, end);

	if (vm_map_lookup_entry(map, start, &entry)) {
		vm_map_clip_start(map, entry, start, &count);
	} else {
		entry = entry->next;
	}

	vm_map_clip_end(map, entry, end, &count);

	if ((entry->start == start) && (entry->end == end) &&
	    ((entry->eflags & MAP_ENTRY_COW) == 0) &&
	    (entry->object.vm_object == NULL)) {
		entry->object.sub_map = submap;
		entry->maptype = VM_MAPTYPE_SUBMAP;
		result = KERN_SUCCESS;
	}
	vm_map_unlock(map);
	vm_map_entry_release(count);

	return (result);
}

/*
 * Sets the protection of the specified address region in the target map. 
 * If "set_max" is specified, the maximum protection is to be set;
 * otherwise, only the current protection is affected.
 *
 * The protection is not applicable to submaps, but is applicable to normal
 * maps and maps governed by virtual page tables.  For example, when operating
 * on a virtual page table our protection basically controls how COW occurs
 * on the backing object, whereas the virtual page table abstraction itself
 * is an abstraction for userland.
 *
 * No requirements.
 */
int
vm_map_protect(vm_map_t map, vm_offset_t start, vm_offset_t end,
	       vm_prot_t new_prot, boolean_t set_max)
{
	vm_map_entry_t current;
	vm_map_entry_t entry;
	int count;

	count = vm_map_entry_reserve(MAP_RESERVE_COUNT);
	vm_map_lock(map);

	VM_MAP_RANGE_CHECK(map, start, end);

	if (vm_map_lookup_entry(map, start, &entry)) {
		vm_map_clip_start(map, entry, start, &count);
	} else {
		entry = entry->next;
	}

	/*
	 * Make a first pass to check for protection violations.
	 */
	current = entry;
	while ((current != &map->header) && (current->start < end)) {
		if (current->maptype == VM_MAPTYPE_SUBMAP) {
			vm_map_unlock(map);
			vm_map_entry_release(count);
			return (KERN_INVALID_ARGUMENT);
		}
		if ((new_prot & current->max_protection) != new_prot) {
			vm_map_unlock(map);
			vm_map_entry_release(count);
			return (KERN_PROTECTION_FAILURE);
		}
		current = current->next;
	}

	/*
	 * Go back and fix up protections. [Note that clipping is not
	 * necessary the second time.]
	 */
	current = entry;

	while ((current != &map->header) && (current->start < end)) {
		vm_prot_t old_prot;

		vm_map_clip_end(map, current, end, &count);

		old_prot = current->protection;
		if (set_max) {
			current->protection =
			    (current->max_protection = new_prot) &
			    old_prot;
		} else {
			current->protection = new_prot;
		}

		/*
		 * Update physical map if necessary. Worry about copy-on-write
		 * here -- CHECK THIS XXX
		 */

		if (current->protection != old_prot) {
#define MASK(entry)	(((entry)->eflags & MAP_ENTRY_COW) ? ~VM_PROT_WRITE : \
							VM_PROT_ALL)

			pmap_protect(map->pmap, current->start,
			    current->end,
			    current->protection & MASK(current));
#undef	MASK
		}

		vm_map_simplify_entry(map, current, &count);

		current = current->next;
	}

	vm_map_unlock(map);
	vm_map_entry_release(count);
	return (KERN_SUCCESS);
}

/*
 * This routine traverses a processes map handling the madvise
 * system call.  Advisories are classified as either those effecting
 * the vm_map_entry structure, or those effecting the underlying
 * objects.
 *
 * The <value> argument is used for extended madvise calls.
 *
 * No requirements.
 */
int
vm_map_madvise(vm_map_t map, vm_offset_t start, vm_offset_t end,
	       int behav, off_t value)
{
	vm_map_entry_t current, entry;
	int modify_map = 0;
	int error = 0;
	int count;

	/*
	 * Some madvise calls directly modify the vm_map_entry, in which case
	 * we need to use an exclusive lock on the map and we need to perform 
	 * various clipping operations.  Otherwise we only need a read-lock
	 * on the map.
	 */

	count = vm_map_entry_reserve(MAP_RESERVE_COUNT);

	switch(behav) {
	case MADV_NORMAL:
	case MADV_SEQUENTIAL:
	case MADV_RANDOM:
	case MADV_NOSYNC:
	case MADV_AUTOSYNC:
	case MADV_NOCORE:
	case MADV_CORE:
	case MADV_SETMAP:
	case MADV_INVAL:
		modify_map = 1;
		vm_map_lock(map);
		break;
	case MADV_WILLNEED:
	case MADV_DONTNEED:
	case MADV_FREE:
		vm_map_lock_read(map);
		break;
	default:
		vm_map_entry_release(count);
		return (EINVAL);
	}

	/*
	 * Locate starting entry and clip if necessary.
	 */

	VM_MAP_RANGE_CHECK(map, start, end);

	if (vm_map_lookup_entry(map, start, &entry)) {
		if (modify_map)
			vm_map_clip_start(map, entry, start, &count);
	} else {
		entry = entry->next;
	}

	if (modify_map) {
		/*
		 * madvise behaviors that are implemented in the vm_map_entry.
		 *
		 * We clip the vm_map_entry so that behavioral changes are
		 * limited to the specified address range.
		 */
		for (current = entry;
		     (current != &map->header) && (current->start < end);
		     current = current->next
		) {
			if (current->maptype == VM_MAPTYPE_SUBMAP)
				continue;

			vm_map_clip_end(map, current, end, &count);

			switch (behav) {
			case MADV_NORMAL:
				vm_map_entry_set_behavior(current, MAP_ENTRY_BEHAV_NORMAL);
				break;
			case MADV_SEQUENTIAL:
				vm_map_entry_set_behavior(current, MAP_ENTRY_BEHAV_SEQUENTIAL);
				break;
			case MADV_RANDOM:
				vm_map_entry_set_behavior(current, MAP_ENTRY_BEHAV_RANDOM);
				break;
			case MADV_NOSYNC:
				current->eflags |= MAP_ENTRY_NOSYNC;
				break;
			case MADV_AUTOSYNC:
				current->eflags &= ~MAP_ENTRY_NOSYNC;
				break;
			case MADV_NOCORE:
				current->eflags |= MAP_ENTRY_NOCOREDUMP;
				break;
			case MADV_CORE:
				current->eflags &= ~MAP_ENTRY_NOCOREDUMP;
				break;
			case MADV_INVAL:
				/*
				 * Invalidate the related pmap entries, used
				 * to flush portions of the real kernel's
				 * pmap when the caller has removed or
				 * modified existing mappings in a virtual
				 * page table.
				 */
				pmap_remove(map->pmap,
					    current->start, current->end);
				break;
			case MADV_SETMAP:
				/*
				 * Set the page directory page for a map
				 * governed by a virtual page table.  Mark
				 * the entry as being governed by a virtual
				 * page table if it is not.
				 *
				 * XXX the page directory page is stored
				 * in the avail_ssize field if the map_entry.
				 *
				 * XXX the map simplification code does not
				 * compare this field so weird things may
				 * happen if you do not apply this function
				 * to the entire mapping governed by the
				 * virtual page table.
				 */
				if (current->maptype != VM_MAPTYPE_VPAGETABLE) {
					error = EINVAL;
					break;
				}
				current->aux.master_pde = value;
				pmap_remove(map->pmap,
					    current->start, current->end);
				break;
			default:
				error = EINVAL;
				break;
			}
			vm_map_simplify_entry(map, current, &count);
		}
		vm_map_unlock(map);
	} else {
		vm_pindex_t pindex;
		int count;

		/*
		 * madvise behaviors that are implemented in the underlying
		 * vm_object.
		 *
		 * Since we don't clip the vm_map_entry, we have to clip
		 * the vm_object pindex and count.
		 *
		 * NOTE!  We currently do not support these functions on
		 * virtual page tables.
		 */
		for (current = entry;
		     (current != &map->header) && (current->start < end);
		     current = current->next
		) {
			vm_offset_t useStart;

			if (current->maptype != VM_MAPTYPE_NORMAL)
				continue;

			pindex = OFF_TO_IDX(current->offset);
			count = atop(current->end - current->start);
			useStart = current->start;

			if (current->start < start) {
				pindex += atop(start - current->start);
				count -= atop(start - current->start);
				useStart = start;
			}
			if (current->end > end)
				count -= atop(current->end - end);

			if (count <= 0)
				continue;

			vm_object_madvise(current->object.vm_object,
					  pindex, count, behav);

			/*
			 * Try to populate the page table.  Mappings governed
			 * by virtual page tables cannot be pre-populated
			 * without a lot of work so don't try.
			 */
			if (behav == MADV_WILLNEED &&
			    current->maptype != VM_MAPTYPE_VPAGETABLE) {
				pmap_object_init_pt(
				    map->pmap, 
				    useStart,
				    current->protection,
				    current->object.vm_object,
				    pindex, 
				    (count << PAGE_SHIFT),
				    MAP_PREFAULT_MADVISE
				);
			}
		}
		vm_map_unlock_read(map);
	}
	vm_map_entry_release(count);
	return(error);
}	


/*
 * Sets the inheritance of the specified address range in the target map.
 * Inheritance affects how the map will be shared with child maps at the
 * time of vm_map_fork.
 */
int
vm_map_inherit(vm_map_t map, vm_offset_t start, vm_offset_t end,
	       vm_inherit_t new_inheritance)
{
	vm_map_entry_t entry;
	vm_map_entry_t temp_entry;
	int count;

	switch (new_inheritance) {
	case VM_INHERIT_NONE:
	case VM_INHERIT_COPY:
	case VM_INHERIT_SHARE:
		break;
	default:
		return (KERN_INVALID_ARGUMENT);
	}

	count = vm_map_entry_reserve(MAP_RESERVE_COUNT);
	vm_map_lock(map);

	VM_MAP_RANGE_CHECK(map, start, end);

	if (vm_map_lookup_entry(map, start, &temp_entry)) {
		entry = temp_entry;
		vm_map_clip_start(map, entry, start, &count);
	} else
		entry = temp_entry->next;

	while ((entry != &map->header) && (entry->start < end)) {
		vm_map_clip_end(map, entry, end, &count);

		entry->inheritance = new_inheritance;

		vm_map_simplify_entry(map, entry, &count);

		entry = entry->next;
	}
	vm_map_unlock(map);
	vm_map_entry_release(count);
	return (KERN_SUCCESS);
}

/*
 * Implement the semantics of mlock
 */
int
vm_map_unwire(vm_map_t map, vm_offset_t start, vm_offset_t real_end,
	      boolean_t new_pageable)
{
	vm_map_entry_t entry;
	vm_map_entry_t start_entry;
	vm_offset_t end;
	int rv = KERN_SUCCESS;
	int count;

	count = vm_map_entry_reserve(MAP_RESERVE_COUNT);
	vm_map_lock(map);
	VM_MAP_RANGE_CHECK(map, start, real_end);
	end = real_end;

	start_entry = vm_map_clip_range(map, start, end, &count,
					MAP_CLIP_NO_HOLES);
	if (start_entry == NULL) {
		vm_map_unlock(map);
		vm_map_entry_release(count);
		return (KERN_INVALID_ADDRESS);
	}

	if (new_pageable == 0) {
		entry = start_entry;
		while ((entry != &map->header) && (entry->start < end)) {
			vm_offset_t save_start;
			vm_offset_t save_end;

			/*
			 * Already user wired or hard wired (trivial cases)
			 */
			if (entry->eflags & MAP_ENTRY_USER_WIRED) {
				entry = entry->next;
				continue;
			}
			if (entry->wired_count != 0) {
				entry->wired_count++;
				entry->eflags |= MAP_ENTRY_USER_WIRED;
				entry = entry->next;
				continue;
			}

			/*
			 * A new wiring requires instantiation of appropriate
			 * management structures and the faulting in of the
			 * page.
			 */
			if (entry->maptype != VM_MAPTYPE_SUBMAP) {
				int copyflag = entry->eflags &
					       MAP_ENTRY_NEEDS_COPY;
				if (copyflag && ((entry->protection &
						  VM_PROT_WRITE) != 0)) {
					vm_map_entry_shadow(entry, 0);
				} else if (entry->object.vm_object == NULL &&
					   !map->system_map) {
					vm_map_entry_allocate_object(entry);
				}
			}
			entry->wired_count++;
			entry->eflags |= MAP_ENTRY_USER_WIRED;

			/*
			 * Now fault in the area.  Note that vm_fault_wire()
			 * may release the map lock temporarily, it will be
			 * relocked on return.  The in-transition
			 * flag protects the entries. 
			 */
			save_start = entry->start;
			save_end = entry->end;
			rv = vm_fault_wire(map, entry, TRUE, 0);
			if (rv) {
				CLIP_CHECK_BACK(entry, save_start);
				for (;;) {
					KASSERT(entry->wired_count == 1, ("bad wired_count on entry"));
					entry->eflags &= ~MAP_ENTRY_USER_WIRED;
					entry->wired_count = 0;
					if (entry->end == save_end)
						break;
					entry = entry->next;
					KASSERT(entry != &map->header, ("bad entry clip during backout"));
				}
				end = save_start;	/* unwire the rest */
				break;
			}
			/*
			 * note that even though the entry might have been
			 * clipped, the USER_WIRED flag we set prevents
			 * duplication so we do not have to do a 
			 * clip check.
			 */
			entry = entry->next;
		}

		/*
		 * If we failed fall through to the unwiring section to
		 * unwire what we had wired so far.  'end' has already
		 * been adjusted.
		 */
		if (rv)
			new_pageable = 1;

		/*
		 * start_entry might have been clipped if we unlocked the
		 * map and blocked.  No matter how clipped it has gotten
		 * there should be a fragment that is on our start boundary.
		 */
		CLIP_CHECK_BACK(start_entry, start);
	}

	/*
	 * Deal with the unwiring case.
	 */
	if (new_pageable) {
		/*
		 * This is the unwiring case.  We must first ensure that the
		 * range to be unwired is really wired down.  We know there
		 * are no holes.
		 */
		entry = start_entry;
		while ((entry != &map->header) && (entry->start < end)) {
			if ((entry->eflags & MAP_ENTRY_USER_WIRED) == 0) {
				rv = KERN_INVALID_ARGUMENT;
				goto done;
			}
			KASSERT(entry->wired_count != 0, ("wired count was 0 with USER_WIRED set! %p", entry));
			entry = entry->next;
		}

		/*
		 * Now decrement the wiring count for each region. If a region
		 * becomes completely unwired, unwire its physical pages and
		 * mappings.
		 */
		/*
		 * The map entries are processed in a loop, checking to
		 * make sure the entry is wired and asserting it has a wired
		 * count. However, another loop was inserted more-or-less in
		 * the middle of the unwiring path. This loop picks up the
		 * "entry" loop variable from the first loop without first
		 * setting it to start_entry. Naturally, the secound loop
		 * is never entered and the pages backing the entries are
		 * never unwired. This can lead to a leak of wired pages.
		 */
		entry = start_entry;
		while ((entry != &map->header) && (entry->start < end)) {
			KASSERT(entry->eflags & MAP_ENTRY_USER_WIRED,
				("expected USER_WIRED on entry %p", entry));
			entry->eflags &= ~MAP_ENTRY_USER_WIRED;
			entry->wired_count--;
			if (entry->wired_count == 0)
				vm_fault_unwire(map, entry);
			entry = entry->next;
		}
	}
done:
	vm_map_unclip_range(map, start_entry, start, real_end, &count,
		MAP_CLIP_NO_HOLES);
	map->timestamp++;
	vm_map_unlock(map);
	vm_map_entry_release(count);
	return (rv);
}

/*
 * Sets the pageability of the specified address range in the target map.
 * Regions specified as not pageable require locked-down physical
 * memory and physical page maps.
 *
 * The map must not be locked, but a reference must remain to the map
 * throughout the call.
 *
 * This function may be called via the zalloc path and must properly
 * reserve map entries for kernel_map.
 *
 * No requirements.
 */
int
vm_map_wire(vm_map_t map, vm_offset_t start, vm_offset_t real_end, int kmflags)
{
	vm_map_entry_t entry;
	vm_map_entry_t start_entry;
	vm_offset_t end;
	int rv = KERN_SUCCESS;
	int count;

	if (kmflags & KM_KRESERVE)
		count = vm_map_entry_kreserve(MAP_RESERVE_COUNT);
	else
		count = vm_map_entry_reserve(MAP_RESERVE_COUNT);
	vm_map_lock(map);
	VM_MAP_RANGE_CHECK(map, start, real_end);
	end = real_end;

	start_entry = vm_map_clip_range(map, start, end, &count,
					MAP_CLIP_NO_HOLES);
	if (start_entry == NULL) {
		vm_map_unlock(map);
		rv = KERN_INVALID_ADDRESS;
		goto failure;
	}
	if ((kmflags & KM_PAGEABLE) == 0) {
		/*
		 * Wiring.  
		 *
		 * 1.  Holding the write lock, we create any shadow or zero-fill
		 * objects that need to be created. Then we clip each map
		 * entry to the region to be wired and increment its wiring
		 * count.  We create objects before clipping the map entries
		 * to avoid object proliferation.
		 *
		 * 2.  We downgrade to a read lock, and call vm_fault_wire to
		 * fault in the pages for any newly wired area (wired_count is
		 * 1).
		 *
		 * Downgrading to a read lock for vm_fault_wire avoids a 
		 * possible deadlock with another process that may have faulted
		 * on one of the pages to be wired (it would mark the page busy,
		 * blocking us, then in turn block on the map lock that we
		 * hold).  Because of problems in the recursive lock package,
		 * we cannot upgrade to a write lock in vm_map_lookup.  Thus,
		 * any actions that require the write lock must be done
		 * beforehand.  Because we keep the read lock on the map, the
		 * copy-on-write status of the entries we modify here cannot
		 * change.
		 */
		entry = start_entry;
		while ((entry != &map->header) && (entry->start < end)) {
			/*
			 * Trivial case if the entry is already wired
			 */
			if (entry->wired_count) {
				entry->wired_count++;
				entry = entry->next;
				continue;
			}

			/*
			 * The entry is being newly wired, we have to setup
			 * appropriate management structures.  A shadow 
			 * object is required for a copy-on-write region,
			 * or a normal object for a zero-fill region.  We
			 * do not have to do this for entries that point to sub
			 * maps because we won't hold the lock on the sub map.
			 */
			if (entry->maptype != VM_MAPTYPE_SUBMAP) {
				int copyflag = entry->eflags &
					       MAP_ENTRY_NEEDS_COPY;
				if (copyflag && ((entry->protection &
						  VM_PROT_WRITE) != 0)) {
					vm_map_entry_shadow(entry, 0);
				} else if (entry->object.vm_object == NULL &&
					   !map->system_map) {
					vm_map_entry_allocate_object(entry);
				}
			}

			entry->wired_count++;
			entry = entry->next;
		}

		/*
		 * Pass 2.
		 */

		/*
		 * HACK HACK HACK HACK
		 *
		 * vm_fault_wire() temporarily unlocks the map to avoid
		 * deadlocks.  The in-transition flag from vm_map_clip_range
		 * call should protect us from changes while the map is
		 * unlocked.  T
		 *
		 * NOTE: Previously this comment stated that clipping might
		 *	 still occur while the entry is unlocked, but from
		 *	 what I can tell it actually cannot.
		 *
		 *	 It is unclear whether the CLIP_CHECK_*() calls
		 *	 are still needed but we keep them in anyway.
		 *
		 * HACK HACK HACK HACK
		 */

		entry = start_entry;
		while (entry != &map->header && entry->start < end) {
			/*
			 * If vm_fault_wire fails for any page we need to undo
			 * what has been done.  We decrement the wiring count
			 * for those pages which have not yet been wired (now)
			 * and unwire those that have (later).
			 */
			vm_offset_t save_start = entry->start;
			vm_offset_t save_end = entry->end;

			if (entry->wired_count == 1)
				rv = vm_fault_wire(map, entry, FALSE, kmflags);
			if (rv) {
				CLIP_CHECK_BACK(entry, save_start);
				for (;;) {
					KASSERT(entry->wired_count == 1, ("wired_count changed unexpectedly"));
					entry->wired_count = 0;
					if (entry->end == save_end)
						break;
					entry = entry->next;
					KASSERT(entry != &map->header, ("bad entry clip during backout"));
				}
				end = save_start;
				break;
			}
			CLIP_CHECK_FWD(entry, save_end);
			entry = entry->next;
		}

		/*
		 * If a failure occured undo everything by falling through
		 * to the unwiring code.  'end' has already been adjusted
		 * appropriately.
		 */
		if (rv)
			kmflags |= KM_PAGEABLE;

		/*
		 * start_entry is still IN_TRANSITION but may have been 
		 * clipped since vm_fault_wire() unlocks and relocks the
		 * map.  No matter how clipped it has gotten there should
		 * be a fragment that is on our start boundary.
		 */
		CLIP_CHECK_BACK(start_entry, start);
	}

	if (kmflags & KM_PAGEABLE) {
		/*
		 * This is the unwiring case.  We must first ensure that the
		 * range to be unwired is really wired down.  We know there
		 * are no holes.
		 */
		entry = start_entry;
		while ((entry != &map->header) && (entry->start < end)) {
			if (entry->wired_count == 0) {
				rv = KERN_INVALID_ARGUMENT;
				goto done;
			}
			entry = entry->next;
		}

		/*
		 * Now decrement the wiring count for each region. If a region
		 * becomes completely unwired, unwire its physical pages and
		 * mappings.
		 */
		entry = start_entry;
		while ((entry != &map->header) && (entry->start < end)) {
			entry->wired_count--;
			if (entry->wired_count == 0)
				vm_fault_unwire(map, entry);
			entry = entry->next;
		}
	}
done:
	vm_map_unclip_range(map, start_entry, start, real_end,
			    &count, MAP_CLIP_NO_HOLES);
	map->timestamp++;
	vm_map_unlock(map);
failure:
	if (kmflags & KM_KRESERVE)
		vm_map_entry_krelease(count);
	else
		vm_map_entry_release(count);
	return (rv);
}

/*
 * Mark a newly allocated address range as wired but do not fault in
 * the pages.  The caller is expected to load the pages into the object.
 *
 * The map must be locked on entry and will remain locked on return.
 * No other requirements.
 */
void
vm_map_set_wired_quick(vm_map_t map, vm_offset_t addr, vm_size_t size,
		       int *countp)
{
	vm_map_entry_t scan;
	vm_map_entry_t entry;

	entry = vm_map_clip_range(map, addr, addr + size,
				  countp, MAP_CLIP_NO_HOLES);
	for (scan = entry;
	     scan != &map->header && scan->start < addr + size;
	     scan = scan->next) {
	    KKASSERT(scan->wired_count == 0);
	    scan->wired_count = 1;
	}
	vm_map_unclip_range(map, entry, addr, addr + size,
			    countp, MAP_CLIP_NO_HOLES);
}

/*
 * Push any dirty cached pages in the address range to their pager.
 * If syncio is TRUE, dirty pages are written synchronously.
 * If invalidate is TRUE, any cached pages are freed as well.
 *
 * This routine is called by sys_msync()
 *
 * Returns an error if any part of the specified range is not mapped.
 *
 * No requirements.
 */
int
vm_map_clean(vm_map_t map, vm_offset_t start, vm_offset_t end,
	     boolean_t syncio, boolean_t invalidate)
{
	vm_map_entry_t current;
	vm_map_entry_t entry;
	vm_size_t size;
	vm_object_t object;
	vm_object_t tobj;
	vm_ooffset_t offset;

	vm_map_lock_read(map);
	VM_MAP_RANGE_CHECK(map, start, end);
	if (!vm_map_lookup_entry(map, start, &entry)) {
		vm_map_unlock_read(map);
		return (KERN_INVALID_ADDRESS);
	}
	lwkt_gettoken(&map->token);

	/*
	 * Make a first pass to check for holes.
	 */
	for (current = entry; current->start < end; current = current->next) {
		if (current->maptype == VM_MAPTYPE_SUBMAP) {
			lwkt_reltoken(&map->token);
			vm_map_unlock_read(map);
			return (KERN_INVALID_ARGUMENT);
		}
		if (end > current->end &&
		    (current->next == &map->header ||
			current->end != current->next->start)) {
			lwkt_reltoken(&map->token);
			vm_map_unlock_read(map);
			return (KERN_INVALID_ADDRESS);
		}
	}

	if (invalidate)
		pmap_remove(vm_map_pmap(map), start, end);

	/*
	 * Make a second pass, cleaning/uncaching pages from the indicated
	 * objects as we go.
	 */
	for (current = entry; current->start < end; current = current->next) {
		offset = current->offset + (start - current->start);
		size = (end <= current->end ? end : current->end) - start;
		if (current->maptype == VM_MAPTYPE_SUBMAP) {
			vm_map_t smap;
			vm_map_entry_t tentry;
			vm_size_t tsize;

			smap = current->object.sub_map;
			vm_map_lock_read(smap);
			vm_map_lookup_entry(smap, offset, &tentry);
			tsize = tentry->end - offset;
			if (tsize < size)
				size = tsize;
			object = tentry->object.vm_object;
			offset = tentry->offset + (offset - tentry->start);
			vm_map_unlock_read(smap);
		} else {
			object = current->object.vm_object;
		}

		if (object)
			vm_object_hold(object);

		/*
		 * Note that there is absolutely no sense in writing out
		 * anonymous objects, so we track down the vnode object
		 * to write out.
		 * We invalidate (remove) all pages from the address space
		 * anyway, for semantic correctness.
		 *
		 * note: certain anonymous maps, such as MAP_NOSYNC maps,
		 * may start out with a NULL object.
		 */
		while (object && (tobj = object->backing_object) != NULL) {
			vm_object_hold(tobj);
			if (tobj == object->backing_object) {
				vm_object_lock_swap();
				offset += object->backing_object_offset;
				vm_object_drop(object);
				object = tobj;
				if (object->size < OFF_TO_IDX(offset + size))
					size = IDX_TO_OFF(object->size) -
					       offset;
				break;
			}
			vm_object_drop(tobj);
		}
		if (object && (object->type == OBJT_VNODE) && 
		    (current->protection & VM_PROT_WRITE) &&
		    (object->flags & OBJ_NOMSYNC) == 0) {
			/*
			 * Flush pages if writing is allowed, invalidate them
			 * if invalidation requested.  Pages undergoing I/O
			 * will be ignored by vm_object_page_remove().
			 *
			 * We cannot lock the vnode and then wait for paging
			 * to complete without deadlocking against vm_fault.
			 * Instead we simply call vm_object_page_remove() and
			 * allow it to block internally on a page-by-page 
			 * basis when it encounters pages undergoing async 
			 * I/O.
			 */
			int flags;

			/* no chain wait needed for vnode objects */
			vm_object_reference_locked(object);
			vn_lock(object->handle, LK_EXCLUSIVE | LK_RETRY);
			flags = (syncio || invalidate) ? OBJPC_SYNC : 0;
			flags |= invalidate ? OBJPC_INVAL : 0;

			/*
			 * When operating on a virtual page table just
			 * flush the whole object.  XXX we probably ought
			 * to 
			 */
			switch(current->maptype) {
			case VM_MAPTYPE_NORMAL:
				vm_object_page_clean(object,
				    OFF_TO_IDX(offset),
				    OFF_TO_IDX(offset + size + PAGE_MASK),
				    flags);
				break;
			case VM_MAPTYPE_VPAGETABLE:
				vm_object_page_clean(object, 0, 0, flags);
				break;
			}
			vn_unlock(((struct vnode *)object->handle));
			vm_object_deallocate_locked(object);
		}
		if (object && invalidate &&
		   ((object->type == OBJT_VNODE) ||
		    (object->type == OBJT_DEVICE) ||
		    (object->type == OBJT_MGTDEVICE))) {
			int clean_only = 
				((object->type == OBJT_DEVICE) ||
				(object->type == OBJT_MGTDEVICE)) ? FALSE : TRUE;
			/* no chain wait needed for vnode/device objects */
			vm_object_reference_locked(object);
			switch(current->maptype) {
			case VM_MAPTYPE_NORMAL:
				vm_object_page_remove(object,
				    OFF_TO_IDX(offset),
				    OFF_TO_IDX(offset + size + PAGE_MASK),
				    clean_only);
				break;
			case VM_MAPTYPE_VPAGETABLE:
				vm_object_page_remove(object, 0, 0, clean_only);
				break;
			}
			vm_object_deallocate_locked(object);
		}
		start += size;
		if (object)
			vm_object_drop(object);
	}

	lwkt_reltoken(&map->token);
	vm_map_unlock_read(map);

	return (KERN_SUCCESS);
}

/*
 * Make the region specified by this entry pageable.
 *
 * The vm_map must be exclusively locked.
 */
static void 
vm_map_entry_unwire(vm_map_t map, vm_map_entry_t entry)
{
	entry->eflags &= ~MAP_ENTRY_USER_WIRED;
	entry->wired_count = 0;
	vm_fault_unwire(map, entry);
}

/*
 * Deallocate the given entry from the target map.
 *
 * The vm_map must be exclusively locked.
 */
static void
vm_map_entry_delete(vm_map_t map, vm_map_entry_t entry, int *countp)
{
	vm_map_entry_unlink(map, entry);
	map->size -= entry->end - entry->start;

	switch(entry->maptype) {
	case VM_MAPTYPE_NORMAL:
	case VM_MAPTYPE_VPAGETABLE:
		vm_object_deallocate(entry->object.vm_object);
		break;
	default:
		break;
	}

	vm_map_entry_dispose(map, entry, countp);
}

/*
 * Deallocates the given address range from the target map.
 *
 * The vm_map must be exclusively locked.
 */
int
vm_map_delete(vm_map_t map, vm_offset_t start, vm_offset_t end, int *countp)
{
	vm_object_t object;
	vm_map_entry_t entry;
	vm_map_entry_t first_entry;

	ASSERT_VM_MAP_LOCKED(map);
	lwkt_gettoken(&map->token);
again:
	/*
	 * Find the start of the region, and clip it.  Set entry to point
	 * at the first record containing the requested address or, if no
	 * such record exists, the next record with a greater address.  The
	 * loop will run from this point until a record beyond the termination
	 * address is encountered.
	 *
	 * map->hint must be adjusted to not point to anything we delete,
	 * so set it to the entry prior to the one being deleted.
	 *
	 * GGG see other GGG comment.
	 */
	if (vm_map_lookup_entry(map, start, &first_entry)) {
		entry = first_entry;
		vm_map_clip_start(map, entry, start, countp);
		map->hint = entry->prev;	/* possible problem XXX */
	} else {
		map->hint = first_entry;	/* possible problem XXX */
		entry = first_entry->next;
	}

	/*
	 * If a hole opens up prior to the current first_free then
	 * adjust first_free.  As with map->hint, map->first_free
	 * cannot be left set to anything we might delete.
	 */
	if (entry == &map->header) {
		map->first_free = &map->header;
	} else if (map->first_free->start >= start) {
		map->first_free = entry->prev;
	}

	/*
	 * Step through all entries in this region
	 */
	while ((entry != &map->header) && (entry->start < end)) {
		vm_map_entry_t next;
		vm_offset_t s, e;
		vm_pindex_t offidxstart, offidxend, count;

		/*
		 * If we hit an in-transition entry we have to sleep and
		 * retry.  It's easier (and not really slower) to just retry
		 * since this case occurs so rarely and the hint is already
		 * pointing at the right place.  We have to reset the
		 * start offset so as not to accidently delete an entry
		 * another process just created in vacated space.
		 */
		if (entry->eflags & MAP_ENTRY_IN_TRANSITION) {
			entry->eflags |= MAP_ENTRY_NEEDS_WAKEUP;
			start = entry->start;
			++mycpu->gd_cnt.v_intrans_coll;
			++mycpu->gd_cnt.v_intrans_wait;
			vm_map_transition_wait(map);
			goto again;
		}
		vm_map_clip_end(map, entry, end, countp);

		s = entry->start;
		e = entry->end;
		next = entry->next;

		offidxstart = OFF_TO_IDX(entry->offset);
		count = OFF_TO_IDX(e - s);
		object = entry->object.vm_object;

		/*
		 * Unwire before removing addresses from the pmap; otherwise,
		 * unwiring will put the entries back in the pmap.
		 */
		if (entry->wired_count != 0)
			vm_map_entry_unwire(map, entry);

		offidxend = offidxstart + count;

		if (object == &kernel_object) {
			vm_object_hold(object);
			vm_object_page_remove(object, offidxstart,
					      offidxend, FALSE);
			vm_object_drop(object);
		} else if (object && object->type != OBJT_DEFAULT &&
			   object->type != OBJT_SWAP) {
			/*
			 * vnode object routines cannot be chain-locked,
			 * but since we aren't removing pages from the
			 * object here we can use a shared hold.
			 */
			vm_object_hold_shared(object);
			pmap_remove(map->pmap, s, e);
			vm_object_drop(object);
		} else if (object) {
			vm_object_hold(object);
			vm_object_chain_acquire(object, 0);
			pmap_remove(map->pmap, s, e);

			if (object != NULL &&
			    object->ref_count != 1 &&
			    (object->flags & (OBJ_NOSPLIT|OBJ_ONEMAPPING)) ==
			     OBJ_ONEMAPPING &&
			    (object->type == OBJT_DEFAULT ||
			     object->type == OBJT_SWAP)) {
				vm_object_collapse(object, NULL);
				vm_object_page_remove(object, offidxstart,
						      offidxend, FALSE);
				if (object->type == OBJT_SWAP) {
					swap_pager_freespace(object,
							     offidxstart,
							     count);
				}
				if (offidxend >= object->size &&
				    offidxstart < object->size) {
					object->size = offidxstart;
				}
			}
			vm_object_chain_release(object);
			vm_object_drop(object);
		}

		/*
		 * Delete the entry (which may delete the object) only after
		 * removing all pmap entries pointing to its pages.
		 * (Otherwise, its page frames may be reallocated, and any
		 * modify bits will be set in the wrong object!)
		 */
		vm_map_entry_delete(map, entry, countp);
		entry = next;
	}
	lwkt_reltoken(&map->token);
	return (KERN_SUCCESS);
}

/*
 * Remove the given address range from the target map.
 * This is the exported form of vm_map_delete.
 *
 * No requirements.
 */
int
vm_map_remove(vm_map_t map, vm_offset_t start, vm_offset_t end)
{
	int result;
	int count;

	count = vm_map_entry_reserve(MAP_RESERVE_COUNT);
	vm_map_lock(map);
	VM_MAP_RANGE_CHECK(map, start, end);
	result = vm_map_delete(map, start, end, &count);
	vm_map_unlock(map);
	vm_map_entry_release(count);

	return (result);
}

/*
 * Assert that the target map allows the specified privilege on the
 * entire address region given.  The entire region must be allocated.
 *
 * The caller must specify whether the vm_map is already locked or not.
 */
boolean_t
vm_map_check_protection(vm_map_t map, vm_offset_t start, vm_offset_t end,
			vm_prot_t protection, boolean_t have_lock)
{
	vm_map_entry_t entry;
	vm_map_entry_t tmp_entry;
	boolean_t result;

	if (have_lock == FALSE)
		vm_map_lock_read(map);

	if (!vm_map_lookup_entry(map, start, &tmp_entry)) {
		if (have_lock == FALSE)
			vm_map_unlock_read(map);
		return (FALSE);
	}
	entry = tmp_entry;

	result = TRUE;
	while (start < end) {
		if (entry == &map->header) {
			result = FALSE;
			break;
		}
		/*
		 * No holes allowed!
		 */

		if (start < entry->start) {
			result = FALSE;
			break;
		}
		/*
		 * Check protection associated with entry.
		 */

		if ((entry->protection & protection) != protection) {
			result = FALSE;
			break;
		}
		/* go to next entry */

		start = entry->end;
		entry = entry->next;
	}
	if (have_lock == FALSE)
		vm_map_unlock_read(map);
	return (result);
}

/*
 * If appropriate this function shadows the original object with a new object
 * and moves the VM pages from the original object to the new object.
 * The original object will also be collapsed, if possible.
 *
 * We can only do this for normal memory objects with a single mapping, and
 * it only makes sense to do it if there are 2 or more refs on the original
 * object.  i.e. typically a memory object that has been extended into
 * multiple vm_map_entry's with non-overlapping ranges.
 *
 * This makes it easier to remove unused pages and keeps object inheritance
 * from being a negative impact on memory usage.
 *
 * On return the (possibly new) entry->object.vm_object will have an
 * additional ref on it for the caller to dispose of (usually by cloning
 * the vm_map_entry).  The additional ref had to be done in this routine
 * to avoid racing a collapse.  The object's ONEMAPPING flag will also be
 * cleared.
 *
 * The vm_map must be locked and its token held.
 */
static void
vm_map_split(vm_map_entry_t entry)
{
	/* OPTIMIZED */
	vm_object_t oobject, nobject, bobject;
	vm_offset_t s, e;
	vm_page_t m;
	vm_pindex_t offidxstart, offidxend, idx;
	vm_size_t size;
	vm_ooffset_t offset;
	int useshadowlist;

	/*
	 * Optimize away object locks for vnode objects.  Important exit/exec
	 * critical path.
	 *
	 * OBJ_ONEMAPPING doesn't apply to vnode objects but clear the flag
	 * anyway.
	 */
	oobject = entry->object.vm_object;
	if (oobject->type != OBJT_DEFAULT && oobject->type != OBJT_SWAP) {
		vm_object_reference_quick(oobject);
		vm_object_clear_flag(oobject, OBJ_ONEMAPPING);
		return;
	}

	/*
	 * Setup.  Chain lock the original object throughout the entire
	 * routine to prevent new page faults from occuring.
	 *
	 * XXX can madvise WILLNEED interfere with us too?
	 */
	vm_object_hold(oobject);
	vm_object_chain_acquire(oobject, 0);

	/*
	 * Original object cannot be split?  Might have also changed state.
	 */
	if (oobject->handle == NULL || (oobject->type != OBJT_DEFAULT &&
					oobject->type != OBJT_SWAP)) {
		vm_object_chain_release(oobject);
		vm_object_reference_locked(oobject);
		vm_object_clear_flag(oobject, OBJ_ONEMAPPING);
		vm_object_drop(oobject);
		return;
	}

	/*
	 * Collapse original object with its backing store as an
	 * optimization to reduce chain lengths when possible.
	 *
	 * If ref_count <= 1 there aren't other non-overlapping vm_map_entry's
	 * for oobject, so there's no point collapsing it.
	 *
	 * Then re-check whether the object can be split.
	 */
	vm_object_collapse(oobject, NULL);

	if (oobject->ref_count <= 1 ||
	    (oobject->type != OBJT_DEFAULT && oobject->type != OBJT_SWAP) ||
	    (oobject->flags & (OBJ_NOSPLIT|OBJ_ONEMAPPING)) != OBJ_ONEMAPPING) {
		vm_object_chain_release(oobject);
		vm_object_reference_locked(oobject);
		vm_object_clear_flag(oobject, OBJ_ONEMAPPING);
		vm_object_drop(oobject);
		return;
	}

	/*
	 * Acquire the chain lock on the backing object.
	 *
	 * Give bobject an additional ref count for when it will be shadowed
	 * by nobject.
	 */
	useshadowlist = 0;
	if ((bobject = oobject->backing_object) != NULL) {
		if (bobject->type != OBJT_VNODE) {
			useshadowlist = 1;
			vm_object_hold(bobject);
			vm_object_chain_wait(bobject, 0);
			vm_object_reference_locked(bobject);
			vm_object_chain_acquire(bobject, 0);
			KKASSERT(bobject->backing_object == bobject);
			KKASSERT((bobject->flags & OBJ_DEAD) == 0);
		} else {
			vm_object_reference_quick(bobject);
		}
	}

	/*
	 * Calculate the object page range and allocate the new object.
	 */
	offset = entry->offset;
	s = entry->start;
	e = entry->end;

	offidxstart = OFF_TO_IDX(offset);
	offidxend = offidxstart + OFF_TO_IDX(e - s);
	size = offidxend - offidxstart;

	switch(oobject->type) {
	case OBJT_DEFAULT:
		nobject = default_pager_alloc(NULL, IDX_TO_OFF(size),
					      VM_PROT_ALL, 0);
		break;
	case OBJT_SWAP:
		nobject = swap_pager_alloc(NULL, IDX_TO_OFF(size),
					   VM_PROT_ALL, 0);
		break;
	default:
		/* not reached */
		nobject = NULL;
		KKASSERT(0);
	}

	if (nobject == NULL) {
		if (bobject) {
			if (useshadowlist) {
				vm_object_chain_release(bobject);
				vm_object_deallocate(bobject);
				vm_object_drop(bobject);
			} else {
				vm_object_deallocate(bobject);
			}
		}
		vm_object_chain_release(oobject);
		vm_object_reference_locked(oobject);
		vm_object_clear_flag(oobject, OBJ_ONEMAPPING);
		vm_object_drop(oobject);
		return;
	}

	/*
	 * The new object will replace entry->object.vm_object so it needs
	 * a second reference (the caller expects an additional ref).
	 */
	vm_object_hold(nobject);
	vm_object_reference_locked(nobject);
	vm_object_chain_acquire(nobject, 0);

	/*
	 * nobject shadows bobject (oobject already shadows bobject).
	 */
	if (bobject) {
		nobject->backing_object_offset =
		    oobject->backing_object_offset + IDX_TO_OFF(offidxstart);
		nobject->backing_object = bobject;
		if (useshadowlist) {
			bobject->shadow_count++;
			bobject->generation++;
			LIST_INSERT_HEAD(&bobject->shadow_head,
					 nobject, shadow_list);
			vm_object_clear_flag(bobject, OBJ_ONEMAPPING); /*XXX*/
			vm_object_chain_release(bobject);
			vm_object_drop(bobject);
			vm_object_set_flag(nobject, OBJ_ONSHADOW);
		}
	}

	/*
	 * Move the VM pages from oobject to nobject
	 */
	for (idx = 0; idx < size; idx++) {
		vm_page_t m;

		m = vm_page_lookup_busy_wait(oobject, offidxstart + idx,
					     TRUE, "vmpg");
		if (m == NULL)
			continue;

		/*
		 * We must wait for pending I/O to complete before we can
		 * rename the page.
		 *
		 * We do not have to VM_PROT_NONE the page as mappings should
		 * not be changed by this operation.
		 *
		 * NOTE: The act of renaming a page updates chaingen for both
		 *	 objects.
		 */
		vm_page_rename(m, nobject, idx);
		/* page automatically made dirty by rename and cache handled */
		/* page remains busy */
	}

	if (oobject->type == OBJT_SWAP) {
		vm_object_pip_add(oobject, 1);
		/*
		 * copy oobject pages into nobject and destroy unneeded
		 * pages in shadow object.
		 */
		swap_pager_copy(oobject, nobject, offidxstart, 0);
		vm_object_pip_wakeup(oobject);
	}

	/*
	 * Wakeup the pages we played with.  No spl protection is needed
	 * for a simple wakeup.
	 */
	for (idx = 0; idx < size; idx++) {
		m = vm_page_lookup(nobject, idx);
		if (m) {
			KKASSERT(m->flags & PG_BUSY);
			vm_page_wakeup(m);
		}
	}
	entry->object.vm_object = nobject;
	entry->offset = 0LL;

	/*
	 * Cleanup
	 *
	 * NOTE: There is no need to remove OBJ_ONEMAPPING from oobject, the
	 *	 related pages were moved and are no longer applicable to the
	 *	 original object.
	 *
	 * NOTE: Deallocate oobject (due to its entry->object.vm_object being
	 *	 replaced by nobject).
	 */
	vm_object_chain_release(nobject);
	vm_object_drop(nobject);
	if (bobject && useshadowlist) {
		vm_object_chain_release(bobject);
		vm_object_drop(bobject);
	}
	vm_object_chain_release(oobject);
	/*vm_object_clear_flag(oobject, OBJ_ONEMAPPING);*/
	vm_object_deallocate_locked(oobject);
	vm_object_drop(oobject);
}

/*
 * Copies the contents of the source entry to the destination
 * entry.  The entries *must* be aligned properly.
 *
 * The vm_maps must be exclusively locked.
 * The vm_map's token must be held.
 *
 * Because the maps are locked no faults can be in progress during the
 * operation.
 */
static void
vm_map_copy_entry(vm_map_t src_map, vm_map_t dst_map,
		  vm_map_entry_t src_entry, vm_map_entry_t dst_entry)
{
	vm_object_t src_object;

	if (dst_entry->maptype == VM_MAPTYPE_SUBMAP)
		return;
	if (src_entry->maptype == VM_MAPTYPE_SUBMAP)
		return;

	if (src_entry->wired_count == 0) {
		/*
		 * If the source entry is marked needs_copy, it is already
		 * write-protected.
		 */
		if ((src_entry->eflags & MAP_ENTRY_NEEDS_COPY) == 0) {
			pmap_protect(src_map->pmap,
			    src_entry->start,
			    src_entry->end,
			    src_entry->protection & ~VM_PROT_WRITE);
		}

		/*
		 * Make a copy of the object.
		 *
		 * The object must be locked prior to checking the object type
		 * and for the call to vm_object_collapse() and vm_map_split().
		 * We cannot use *_hold() here because the split code will
		 * probably try to destroy the object.  The lock is a pool
		 * token and doesn't care.
		 *
		 * We must bump src_map->timestamp when setting
		 * MAP_ENTRY_NEEDS_COPY to force any concurrent fault
		 * to retry, otherwise the concurrent fault might improperly
		 * install a RW pte when its supposed to be a RO(COW) pte.
		 * This race can occur because a vnode-backed fault may have
		 * to temporarily release the map lock.
		 */
		if (src_entry->object.vm_object != NULL) {
			vm_map_split(src_entry);
			src_object = src_entry->object.vm_object;
			dst_entry->object.vm_object = src_object;
			src_entry->eflags |= (MAP_ENTRY_COW |
					      MAP_ENTRY_NEEDS_COPY);
			dst_entry->eflags |= (MAP_ENTRY_COW |
					      MAP_ENTRY_NEEDS_COPY);
			dst_entry->offset = src_entry->offset;
			++src_map->timestamp;
		} else {
			dst_entry->object.vm_object = NULL;
			dst_entry->offset = 0;
		}

		pmap_copy(dst_map->pmap, src_map->pmap, dst_entry->start,
		    dst_entry->end - dst_entry->start, src_entry->start);
	} else {
		/*
		 * Of course, wired down pages can't be set copy-on-write.
		 * Cause wired pages to be copied into the new map by
		 * simulating faults (the new pages are pageable)
		 */
		vm_fault_copy_entry(dst_map, src_map, dst_entry, src_entry);
	}
}

/*
 * vmspace_fork:
 * Create a new process vmspace structure and vm_map
 * based on those of an existing process.  The new map
 * is based on the old map, according to the inheritance
 * values on the regions in that map.
 *
 * The source map must not be locked.
 * No requirements.
 */
struct vmspace *
vmspace_fork(struct vmspace *vm1)
{
	struct vmspace *vm2;
	vm_map_t old_map = &vm1->vm_map;
	vm_map_t new_map;
	vm_map_entry_t old_entry;
	vm_map_entry_t new_entry;
	vm_object_t object;
	int count;

	lwkt_gettoken(&vm1->vm_map.token);
	vm_map_lock(old_map);

	vm2 = vmspace_alloc(old_map->min_offset, old_map->max_offset);
	lwkt_gettoken(&vm2->vm_map.token);
	bcopy(&vm1->vm_startcopy, &vm2->vm_startcopy,
	    (caddr_t)&vm1->vm_endcopy - (caddr_t)&vm1->vm_startcopy);
	new_map = &vm2->vm_map;	/* XXX */
	new_map->timestamp = 1;

	vm_map_lock(new_map);

	count = 0;
	old_entry = old_map->header.next;
	while (old_entry != &old_map->header) {
		++count;
		old_entry = old_entry->next;
	}

	count = vm_map_entry_reserve(count + MAP_RESERVE_COUNT);

	old_entry = old_map->header.next;
	while (old_entry != &old_map->header) {
		if (old_entry->maptype == VM_MAPTYPE_SUBMAP)
			panic("vm_map_fork: encountered a submap");

		switch (old_entry->inheritance) {
		case VM_INHERIT_NONE:
			break;
		case VM_INHERIT_SHARE:
			/*
			 * Clone the entry, creating the shared object if
			 * necessary.
			 */
			if (old_entry->object.vm_object == NULL)
				vm_map_entry_allocate_object(old_entry);

			if (old_entry->eflags & MAP_ENTRY_NEEDS_COPY) {
				/*
				 * Shadow a map_entry which needs a copy,
				 * replacing its object with a new object
				 * that points to the old one.  Ask the
				 * shadow code to automatically add an
				 * additional ref.  We can't do it afterwords
				 * because we might race a collapse.  The call
				 * to vm_map_entry_shadow() will also clear
				 * OBJ_ONEMAPPING.
				 */
				vm_map_entry_shadow(old_entry, 1);
			} else if (old_entry->object.vm_object) {
				/*
				 * We will make a shared copy of the object,
				 * and must clear OBJ_ONEMAPPING.
				 *
				 * Optimize vnode objects.  OBJ_ONEMAPPING
				 * is non-applicable but clear it anyway,
				 * and its terminal so we don'th ave to deal
				 * with chains.  Reduces SMP conflicts.
				 *
				 * XXX assert that object.vm_object != NULL
				 *     since we allocate it above.
				 */
				object = old_entry->object.vm_object;
				if (object->type == OBJT_VNODE) {
					vm_object_reference_quick(object);
					vm_object_clear_flag(object,
							     OBJ_ONEMAPPING);
				} else {
					vm_object_hold(object);
					vm_object_chain_wait(object, 0);
					vm_object_reference_locked(object);
					vm_object_clear_flag(object,
							     OBJ_ONEMAPPING);
					vm_object_drop(object);
				}
			}

			/*
			 * Clone the entry.  We've already bumped the ref on
			 * any vm_object.
			 */
			new_entry = vm_map_entry_create(new_map, &count);
			*new_entry = *old_entry;
			new_entry->eflags &= ~MAP_ENTRY_USER_WIRED;
			new_entry->wired_count = 0;

			/*
			 * Insert the entry into the new map -- we know we're
			 * inserting at the end of the new map.
			 */

			vm_map_entry_link(new_map, new_map->header.prev,
					  new_entry);

			/*
			 * Update the physical map
			 */
			pmap_copy(new_map->pmap, old_map->pmap,
				  new_entry->start,
				  (old_entry->end - old_entry->start),
				  old_entry->start);
			break;
		case VM_INHERIT_COPY:
			/*
			 * Clone the entry and link into the map.
			 */
			new_entry = vm_map_entry_create(new_map, &count);
			*new_entry = *old_entry;
			new_entry->eflags &= ~MAP_ENTRY_USER_WIRED;
			new_entry->wired_count = 0;
			new_entry->object.vm_object = NULL;
			vm_map_entry_link(new_map, new_map->header.prev,
					  new_entry);
			vm_map_copy_entry(old_map, new_map, old_entry,
					  new_entry);
			break;
		}
		old_entry = old_entry->next;
	}

	new_map->size = old_map->size;
	vm_map_unlock(old_map);
	vm_map_unlock(new_map);
	vm_map_entry_release(count);

	lwkt_reltoken(&vm2->vm_map.token);
	lwkt_reltoken(&vm1->vm_map.token);

	return (vm2);
}

/*
 * Create an auto-grow stack entry
 *
 * No requirements.
 */
int
vm_map_stack (vm_map_t map, vm_offset_t addrbos, vm_size_t max_ssize,
	      int flags, vm_prot_t prot, vm_prot_t max, int cow)
{
	vm_map_entry_t	prev_entry;
	vm_map_entry_t	new_stack_entry;
	vm_size_t	init_ssize;
	int		rv;
	int		count;
	vm_offset_t	tmpaddr;

	cow |= MAP_IS_STACK;

	if (max_ssize < sgrowsiz)
		init_ssize = max_ssize;
	else
		init_ssize = sgrowsiz;

	count = vm_map_entry_reserve(MAP_RESERVE_COUNT);
	vm_map_lock(map);

	/*
	 * Find space for the mapping
	 */
	if ((flags & (MAP_FIXED | MAP_TRYFIXED)) == 0) {
		if (vm_map_findspace(map, addrbos, max_ssize, 1,
				     flags, &tmpaddr)) {
			vm_map_unlock(map);
			vm_map_entry_release(count);
			return (KERN_NO_SPACE);
		}
		addrbos = tmpaddr;
	}

	/* If addr is already mapped, no go */
	if (vm_map_lookup_entry(map, addrbos, &prev_entry)) {
		vm_map_unlock(map);
		vm_map_entry_release(count);
		return (KERN_NO_SPACE);
	}

#if 0
	/* XXX already handled by kern_mmap() */
	/* If we would blow our VMEM resource limit, no go */
	if (map->size + init_ssize >
	    curproc->p_rlimit[RLIMIT_VMEM].rlim_cur) {
		vm_map_unlock(map);
		vm_map_entry_release(count);
		return (KERN_NO_SPACE);
	}
#endif

	/*
	 * If we can't accomodate max_ssize in the current mapping,
	 * no go.  However, we need to be aware that subsequent user
	 * mappings might map into the space we have reserved for
	 * stack, and currently this space is not protected.  
	 * 
	 * Hopefully we will at least detect this condition 
	 * when we try to grow the stack.
	 */
	if ((prev_entry->next != &map->header) &&
	    (prev_entry->next->start < addrbos + max_ssize)) {
		vm_map_unlock(map);
		vm_map_entry_release(count);
		return (KERN_NO_SPACE);
	}

	/*
	 * We initially map a stack of only init_ssize.  We will
	 * grow as needed later.  Since this is to be a grow 
	 * down stack, we map at the top of the range.
	 *
	 * Note: we would normally expect prot and max to be
	 * VM_PROT_ALL, and cow to be 0.  Possibly we should
	 * eliminate these as input parameters, and just
	 * pass these values here in the insert call.
	 */
	rv = vm_map_insert(map, &count,
			   NULL, 0, addrbos + max_ssize - init_ssize,
	                   addrbos + max_ssize,
			   VM_MAPTYPE_NORMAL,
			   prot, max,
			   cow);

	/* Now set the avail_ssize amount */
	if (rv == KERN_SUCCESS) {
		if (prev_entry != &map->header)
			vm_map_clip_end(map, prev_entry, addrbos + max_ssize - init_ssize, &count);
		new_stack_entry = prev_entry->next;
		if (new_stack_entry->end   != addrbos + max_ssize ||
		    new_stack_entry->start != addrbos + max_ssize - init_ssize)
			panic ("Bad entry start/end for new stack entry");
		else 
			new_stack_entry->aux.avail_ssize = max_ssize - init_ssize;
	}

	vm_map_unlock(map);
	vm_map_entry_release(count);
	return (rv);
}

/*
 * Attempts to grow a vm stack entry.  Returns KERN_SUCCESS if the
 * desired address is already mapped, or if we successfully grow
 * the stack.  Also returns KERN_SUCCESS if addr is outside the
 * stack range (this is strange, but preserves compatibility with
 * the grow function in vm_machdep.c).
 *
 * No requirements.
 */
int
vm_map_growstack (struct proc *p, vm_offset_t addr)
{
	vm_map_entry_t prev_entry;
	vm_map_entry_t stack_entry;
	vm_map_entry_t new_stack_entry;
	struct vmspace *vm = p->p_vmspace;
	vm_map_t map = &vm->vm_map;
	vm_offset_t    end;
	int grow_amount;
	int rv = KERN_SUCCESS;
	int is_procstack;
	int use_read_lock = 1;
	int count;

	count = vm_map_entry_reserve(MAP_RESERVE_COUNT);
Retry:
	if (use_read_lock)
		vm_map_lock_read(map);
	else
		vm_map_lock(map);

	/* If addr is already in the entry range, no need to grow.*/
	if (vm_map_lookup_entry(map, addr, &prev_entry))
		goto done;

	if ((stack_entry = prev_entry->next) == &map->header)
		goto done;
	if (prev_entry == &map->header) 
		end = stack_entry->start - stack_entry->aux.avail_ssize;
	else
		end = prev_entry->end;

	/*
	 * This next test mimics the old grow function in vm_machdep.c.
	 * It really doesn't quite make sense, but we do it anyway
	 * for compatibility.
	 *
	 * If not growable stack, return success.  This signals the
	 * caller to proceed as he would normally with normal vm.
	 */
	if (stack_entry->aux.avail_ssize < 1 ||
	    addr >= stack_entry->start ||
	    addr <  stack_entry->start - stack_entry->aux.avail_ssize) {
		goto done;
	} 
	
	/* Find the minimum grow amount */
	grow_amount = roundup (stack_entry->start - addr, PAGE_SIZE);
	if (grow_amount > stack_entry->aux.avail_ssize) {
		rv = KERN_NO_SPACE;
		goto done;
	}

	/*
	 * If there is no longer enough space between the entries
	 * nogo, and adjust the available space.  Note: this 
	 * should only happen if the user has mapped into the
	 * stack area after the stack was created, and is
	 * probably an error.
	 *
	 * This also effectively destroys any guard page the user
	 * might have intended by limiting the stack size.
	 */
	if (grow_amount > stack_entry->start - end) {
		if (use_read_lock && vm_map_lock_upgrade(map)) {
			/* lost lock */
			use_read_lock = 0;
			goto Retry;
		}
		use_read_lock = 0;
		stack_entry->aux.avail_ssize = stack_entry->start - end;
		rv = KERN_NO_SPACE;
		goto done;
	}

	is_procstack = addr >= (vm_offset_t)vm->vm_maxsaddr;

	/* If this is the main process stack, see if we're over the 
	 * stack limit.
	 */
	if (is_procstack && (ctob(vm->vm_ssize) + grow_amount >
			     p->p_rlimit[RLIMIT_STACK].rlim_cur)) {
		rv = KERN_NO_SPACE;
		goto done;
	}

	/* Round up the grow amount modulo SGROWSIZ */
	grow_amount = roundup (grow_amount, sgrowsiz);
	if (grow_amount > stack_entry->aux.avail_ssize) {
		grow_amount = stack_entry->aux.avail_ssize;
	}
	if (is_procstack && (ctob(vm->vm_ssize) + grow_amount >
	                     p->p_rlimit[RLIMIT_STACK].rlim_cur)) {
		grow_amount = p->p_rlimit[RLIMIT_STACK].rlim_cur -
		              ctob(vm->vm_ssize);
	}

	/* If we would blow our VMEM resource limit, no go */
	if (map->size + grow_amount > p->p_rlimit[RLIMIT_VMEM].rlim_cur) {
		rv = KERN_NO_SPACE;
		goto done;
	}

	if (use_read_lock && vm_map_lock_upgrade(map)) {
		/* lost lock */
		use_read_lock = 0;
		goto Retry;
	}
	use_read_lock = 0;

	/* Get the preliminary new entry start value */
	addr = stack_entry->start - grow_amount;

	/* If this puts us into the previous entry, cut back our growth
	 * to the available space.  Also, see the note above.
	 */
	if (addr < end) {
		stack_entry->aux.avail_ssize = stack_entry->start - end;
		addr = end;
	}

	rv = vm_map_insert(map, &count,
			   NULL, 0, addr, stack_entry->start,
			   VM_MAPTYPE_NORMAL,
			   VM_PROT_ALL, VM_PROT_ALL,
			   0);

	/* Adjust the available stack space by the amount we grew. */
	if (rv == KERN_SUCCESS) {
		if (prev_entry != &map->header)
			vm_map_clip_end(map, prev_entry, addr, &count);
		new_stack_entry = prev_entry->next;
		if (new_stack_entry->end   != stack_entry->start  ||
		    new_stack_entry->start != addr)
			panic ("Bad stack grow start/end in new stack entry");
		else {
			new_stack_entry->aux.avail_ssize =
				stack_entry->aux.avail_ssize -
				(new_stack_entry->end - new_stack_entry->start);
			if (is_procstack)
				vm->vm_ssize += btoc(new_stack_entry->end -
						     new_stack_entry->start);
		}

		if (map->flags & MAP_WIREFUTURE)
			vm_map_unwire(map, new_stack_entry->start,
				      new_stack_entry->end, FALSE);
	}

done:
	if (use_read_lock)
		vm_map_unlock_read(map);
	else
		vm_map_unlock(map);
	vm_map_entry_release(count);
	return (rv);
}

/*
 * Unshare the specified VM space for exec.  If other processes are
 * mapped to it, then create a new one.  The new vmspace is null.
 *
 * No requirements.
 */
void
vmspace_exec(struct proc *p, struct vmspace *vmcopy) 
{
	struct vmspace *oldvmspace = p->p_vmspace;
	struct vmspace *newvmspace;
	vm_map_t map = &p->p_vmspace->vm_map;

	/*
	 * If we are execing a resident vmspace we fork it, otherwise
	 * we create a new vmspace.  Note that exitingcnt is not
	 * copied to the new vmspace.
	 */
	lwkt_gettoken(&oldvmspace->vm_map.token);
	if (vmcopy)  {
		newvmspace = vmspace_fork(vmcopy);
		lwkt_gettoken(&newvmspace->vm_map.token);
	} else {
		newvmspace = vmspace_alloc(map->min_offset, map->max_offset);
		lwkt_gettoken(&newvmspace->vm_map.token);
		bcopy(&oldvmspace->vm_startcopy, &newvmspace->vm_startcopy,
		      (caddr_t)&oldvmspace->vm_endcopy -
		       (caddr_t)&oldvmspace->vm_startcopy);
	}

	/*
	 * Finish initializing the vmspace before assigning it
	 * to the process.  The vmspace will become the current vmspace
	 * if p == curproc.
	 */
	pmap_pinit2(vmspace_pmap(newvmspace));
	pmap_replacevm(p, newvmspace, 0);
	lwkt_reltoken(&newvmspace->vm_map.token);
	lwkt_reltoken(&oldvmspace->vm_map.token);
	vmspace_free(oldvmspace);
}

/*
 * Unshare the specified VM space for forcing COW.  This
 * is called by rfork, for the (RFMEM|RFPROC) == 0 case.
 */
void
vmspace_unshare(struct proc *p) 
{
	struct vmspace *oldvmspace = p->p_vmspace;
	struct vmspace *newvmspace;

	lwkt_gettoken(&oldvmspace->vm_map.token);
	if (oldvmspace->vm_sysref.refcnt == 1) {
		lwkt_reltoken(&oldvmspace->vm_map.token);
		return;
	}
	newvmspace = vmspace_fork(oldvmspace);
	lwkt_gettoken(&newvmspace->vm_map.token);
	pmap_pinit2(vmspace_pmap(newvmspace));
	pmap_replacevm(p, newvmspace, 0);
	lwkt_reltoken(&newvmspace->vm_map.token);
	lwkt_reltoken(&oldvmspace->vm_map.token);
	vmspace_free(oldvmspace);
}

/*
 * vm_map_hint: return the beginning of the best area suitable for
 * creating a new mapping with "prot" protection.
 *
 * No requirements.
 */
vm_offset_t
vm_map_hint(struct proc *p, vm_offset_t addr, vm_prot_t prot)
{
	struct vmspace *vms = p->p_vmspace;

	if (!randomize_mmap || addr != 0) {
		/*
		 * Set a reasonable start point for the hint if it was
		 * not specified or if it falls within the heap space.
		 * Hinted mmap()s do not allocate out of the heap space.
		 */
		if (addr == 0 ||
		    (addr >= round_page((vm_offset_t)vms->vm_taddr) &&
		     addr < round_page((vm_offset_t)vms->vm_daddr + maxdsiz))) {
			addr = round_page((vm_offset_t)vms->vm_daddr + maxdsiz);
		}

		return addr;
	}

#ifdef notyet
#ifdef __i386__
	/*
	 * If executable skip first two pages, otherwise start
	 * after data + heap region.
	 */
	if ((prot & VM_PROT_EXECUTE) &&
	    ((vm_offset_t)vms->vm_daddr >= I386_MAX_EXE_ADDR)) {
		addr = (PAGE_SIZE * 2) +
		    (karc4random() & (I386_MAX_EXE_ADDR / 2 - 1));
		return (round_page(addr));
	}
#endif /* __i386__ */
#endif /* notyet */

	addr = (vm_offset_t)vms->vm_daddr + MAXDSIZ;
	addr += karc4random() & (MIN((256 * 1024 * 1024), MAXDSIZ) - 1);

	return (round_page(addr));
}

/*
 * Finds the VM object, offset, and protection for a given virtual address
 * in the specified map, assuming a page fault of the type specified.
 *
 * Leaves the map in question locked for read; return values are guaranteed
 * until a vm_map_lookup_done call is performed.  Note that the map argument
 * is in/out; the returned map must be used in the call to vm_map_lookup_done.
 *
 * A handle (out_entry) is returned for use in vm_map_lookup_done, to make
 * that fast.
 *
 * If a lookup is requested with "write protection" specified, the map may
 * be changed to perform virtual copying operations, although the data
 * referenced will remain the same.
 *
 * No requirements.
 */
int
vm_map_lookup(vm_map_t *var_map,		/* IN/OUT */
	      vm_offset_t vaddr,
	      vm_prot_t fault_typea,
	      vm_map_entry_t *out_entry,	/* OUT */
	      vm_object_t *object,		/* OUT */
	      vm_pindex_t *pindex,		/* OUT */
	      vm_prot_t *out_prot,		/* OUT */
	      boolean_t *wired)			/* OUT */
{
	vm_map_entry_t entry;
	vm_map_t map = *var_map;
	vm_prot_t prot;
	vm_prot_t fault_type = fault_typea;
	int use_read_lock = 1;
	int rv = KERN_SUCCESS;

RetryLookup:
	if (use_read_lock)
		vm_map_lock_read(map);
	else
		vm_map_lock(map);

	/*
	 * If the map has an interesting hint, try it before calling full
	 * blown lookup routine.
	 */
	entry = map->hint;
	cpu_ccfence();
	*out_entry = entry;
	*object = NULL;

	if ((entry == &map->header) ||
	    (vaddr < entry->start) || (vaddr >= entry->end)) {
		vm_map_entry_t tmp_entry;

		/*
		 * Entry was either not a valid hint, or the vaddr was not
		 * contained in the entry, so do a full lookup.
		 */
		if (!vm_map_lookup_entry(map, vaddr, &tmp_entry)) {
			rv = KERN_INVALID_ADDRESS;
			goto done;
		}

		entry = tmp_entry;
		*out_entry = entry;
	}
	
	/*
	 * Handle submaps.
	 */
	if (entry->maptype == VM_MAPTYPE_SUBMAP) {
		vm_map_t old_map = map;

		*var_map = map = entry->object.sub_map;
		if (use_read_lock)
			vm_map_unlock_read(old_map);
		else
			vm_map_unlock(old_map);
		use_read_lock = 1;
		goto RetryLookup;
	}

	/*
	 * Check whether this task is allowed to have this page.
	 * Note the special case for MAP_ENTRY_COW
	 * pages with an override.  This is to implement a forced
	 * COW for debuggers.
	 */

	if (fault_type & VM_PROT_OVERRIDE_WRITE)
		prot = entry->max_protection;
	else
		prot = entry->protection;

	fault_type &= (VM_PROT_READ|VM_PROT_WRITE|VM_PROT_EXECUTE);
	if ((fault_type & prot) != fault_type) {
		rv = KERN_PROTECTION_FAILURE;
		goto done;
	}

	if ((entry->eflags & MAP_ENTRY_USER_WIRED) &&
	    (entry->eflags & MAP_ENTRY_COW) &&
	    (fault_type & VM_PROT_WRITE) &&
	    (fault_typea & VM_PROT_OVERRIDE_WRITE) == 0) {
		rv = KERN_PROTECTION_FAILURE;
		goto done;
	}

	/*
	 * If this page is not pageable, we have to get it for all possible
	 * accesses.
	 */
	*wired = (entry->wired_count != 0);
	if (*wired)
		prot = fault_type = entry->protection;

	/*
	 * Virtual page tables may need to update the accessed (A) bit
	 * in a page table entry.  Upgrade the fault to a write fault for
	 * that case if the map will support it.  If the map does not support
	 * it the page table entry simply will not be updated.
	 */
	if (entry->maptype == VM_MAPTYPE_VPAGETABLE) {
		if (prot & VM_PROT_WRITE)
			fault_type |= VM_PROT_WRITE;
	}

	if (curthread->td_lwp && curthread->td_lwp->lwp_vmspace &&
	    pmap_emulate_ad_bits(&curthread->td_lwp->lwp_vmspace->vm_pmap)) {
		if ((prot & VM_PROT_WRITE) == 0)
			fault_type |= VM_PROT_WRITE;
	}

	/*
	 * If the entry was copy-on-write, we either ...
	 */
	if (entry->eflags & MAP_ENTRY_NEEDS_COPY) {
		/*
		 * If we want to write the page, we may as well handle that
		 * now since we've got the map locked.
		 *
		 * If we don't need to write the page, we just demote the
		 * permissions allowed.
		 */

		if (fault_type & VM_PROT_WRITE) {
			/*
			 * Make a new object, and place it in the object
			 * chain.  Note that no new references have appeared
			 * -- one just moved from the map to the new
			 * object.
			 */

			if (use_read_lock && vm_map_lock_upgrade(map)) {
				/* lost lock */
				use_read_lock = 0;
				goto RetryLookup;
			}
			use_read_lock = 0;

			vm_map_entry_shadow(entry, 0);
		} else {
			/*
			 * We're attempting to read a copy-on-write page --
			 * don't allow writes.
			 */

			prot &= ~VM_PROT_WRITE;
		}
	}

	/*
	 * Create an object if necessary.
	 */
	if (entry->object.vm_object == NULL && !map->system_map) {
		if (use_read_lock && vm_map_lock_upgrade(map))  {
			/* lost lock */
			use_read_lock = 0;
			goto RetryLookup;
		}
		use_read_lock = 0;
		vm_map_entry_allocate_object(entry);
	}

	/*
	 * Return the object/offset from this entry.  If the entry was
	 * copy-on-write or empty, it has been fixed up.
	 */

	*pindex = OFF_TO_IDX((vaddr - entry->start) + entry->offset);
	*object = entry->object.vm_object;

	/*
	 * Return whether this is the only map sharing this data.  On
	 * success we return with a read lock held on the map.  On failure
	 * we return with the map unlocked.
	 */
	*out_prot = prot;
done:
	if (rv == KERN_SUCCESS) {
		if (use_read_lock == 0)
			vm_map_lock_downgrade(map);
	} else if (use_read_lock) {
		vm_map_unlock_read(map);
	} else {
		vm_map_unlock(map);
	}
	return (rv);
}

/*
 * Releases locks acquired by a vm_map_lookup()
 * (according to the handle returned by that lookup).
 *
 * No other requirements.
 */
void
vm_map_lookup_done(vm_map_t map, vm_map_entry_t entry, int count)
{
	/*
	 * Unlock the main-level map
	 */
	vm_map_unlock_read(map);
	if (count)
		vm_map_entry_release(count);
}

#include "opt_ddb.h"
#ifdef DDB
#include <sys/kernel.h>

#include <ddb/ddb.h>

/*
 * Debugging only
 */
DB_SHOW_COMMAND(map, vm_map_print)
{
	static int nlines;
	/* XXX convert args. */
	vm_map_t map = (vm_map_t)addr;
	boolean_t full = have_addr;

	vm_map_entry_t entry;

	db_iprintf("Task map %p: pmap=%p, nentries=%d, version=%u\n",
	    (void *)map,
	    (void *)map->pmap, map->nentries, map->timestamp);
	nlines++;

	if (!full && db_indent)
		return;

	db_indent += 2;
	for (entry = map->header.next; entry != &map->header;
	    entry = entry->next) {
		db_iprintf("map entry %p: start=%p, end=%p\n",
		    (void *)entry, (void *)entry->start, (void *)entry->end);
		nlines++;
		{
			static char *inheritance_name[4] =
			{"share", "copy", "none", "donate_copy"};

			db_iprintf(" prot=%x/%x/%s",
			    entry->protection,
			    entry->max_protection,
			    inheritance_name[(int)(unsigned char)entry->inheritance]);
			if (entry->wired_count != 0)
				db_printf(", wired");
		}
		if (entry->maptype == VM_MAPTYPE_SUBMAP) {
			/* XXX no %qd in kernel.  Truncate entry->offset. */
			db_printf(", share=%p, offset=0x%lx\n",
			    (void *)entry->object.sub_map,
			    (long)entry->offset);
			nlines++;
			if ((entry->prev == &map->header) ||
			    (entry->prev->object.sub_map !=
				entry->object.sub_map)) {
				db_indent += 2;
				vm_map_print((db_expr_t)(intptr_t)
					     entry->object.sub_map,
					     full, 0, NULL);
				db_indent -= 2;
			}
		} else {
			/* XXX no %qd in kernel.  Truncate entry->offset. */
			db_printf(", object=%p, offset=0x%lx",
			    (void *)entry->object.vm_object,
			    (long)entry->offset);
			if (entry->eflags & MAP_ENTRY_COW)
				db_printf(", copy (%s)",
				    (entry->eflags & MAP_ENTRY_NEEDS_COPY) ? "needed" : "done");
			db_printf("\n");
			nlines++;

			if ((entry->prev == &map->header) ||
			    (entry->prev->object.vm_object !=
				entry->object.vm_object)) {
				db_indent += 2;
				vm_object_print((db_expr_t)(intptr_t)
						entry->object.vm_object,
						full, 0, NULL);
				nlines += 4;
				db_indent -= 2;
			}
		}
	}
	db_indent -= 2;
	if (db_indent == 0)
		nlines = 0;
}

/*
 * Debugging only
 */
DB_SHOW_COMMAND(procvm, procvm)
{
	struct proc *p;

	if (have_addr) {
		p = (struct proc *) addr;
	} else {
		p = curproc;
	}

	db_printf("p = %p, vmspace = %p, map = %p, pmap = %p\n",
	    (void *)p, (void *)p->p_vmspace, (void *)&p->p_vmspace->vm_map,
	    (void *)vmspace_pmap(p->p_vmspace));

	vm_map_print((db_expr_t)(intptr_t)&p->p_vmspace->vm_map, 1, 0, NULL);
}

#endif /* DDB */
