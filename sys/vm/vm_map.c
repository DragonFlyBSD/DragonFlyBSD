/*
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 2003-2019 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * The Mach Operating System project at Carnegie-Mellon University.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
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
#include <sys/objcache.h>
#include <sys/kern_syscall.h>

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

#include <sys/random.h>
#include <sys/sysctl.h>
#include <sys/spinlock.h>

#include <sys/thread2.h>
#include <sys/spinlock2.h>

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
static boolean_t vmspace_ctor(void *obj, void *privdata, int ocflags);
static void vmspace_dtor(void *obj, void *privdata);
static void vmspace_terminate(struct vmspace *vm, int final);

MALLOC_DEFINE(M_VMSPACE, "vmspace", "vmspace objcache backingstore");
MALLOC_DEFINE(M_MAP_BACKING, "map_backing", "vm_map_backing to entry");
static struct objcache *vmspace_cache;

/*
 * per-cpu page table cross mappings are initialized in early boot
 * and might require a considerable number of vm_map_entry structures.
 */
#define MAPENTRYBSP_CACHE	(MAXCPU+1)
#define MAPENTRYAP_CACHE	8

/*
 * Partioning threaded programs with large anonymous memory areas can
 * improve concurrent fault performance.
 */
#define MAP_ENTRY_PARTITION_SIZE	((vm_offset_t)(32 * 1024 * 1024))
#define MAP_ENTRY_PARTITION_MASK	(MAP_ENTRY_PARTITION_SIZE - 1)

#define VM_MAP_ENTRY_WITHIN_PARTITION(entry)	\
	((((entry)->ba.start ^ (entry)->ba.end) & ~MAP_ENTRY_PARTITION_MASK) == 0)

static struct vm_zone mapentzone_store;
__read_mostly static vm_zone_t mapentzone;

static struct vm_map_entry map_entry_init[MAX_MAPENT];
static struct vm_map_entry cpu_map_entry_init_bsp[MAPENTRYBSP_CACHE];
static struct vm_map_entry cpu_map_entry_init_ap[MAXCPU][MAPENTRYAP_CACHE];

__read_mostly static int randomize_mmap;
SYSCTL_INT(_vm, OID_AUTO, randomize_mmap, CTLFLAG_RW, &randomize_mmap, 0,
    "Randomize mmap offsets");
__read_mostly static int vm_map_relock_enable = 1;
SYSCTL_INT(_vm, OID_AUTO, map_relock_enable, CTLFLAG_RW,
	   &vm_map_relock_enable, 0, "insert pop pgtable optimization");
__read_mostly static int vm_map_partition_enable = 1;
SYSCTL_INT(_vm, OID_AUTO, map_partition_enable, CTLFLAG_RW,
	   &vm_map_partition_enable, 0, "Break up larger vm_map_entry's");
__read_mostly static int vm_map_backing_limit = 5;
SYSCTL_INT(_vm, OID_AUTO, map_backing_limit, CTLFLAG_RW,
	   &vm_map_backing_limit, 0, "ba.backing_ba link depth");
__read_mostly static int vm_map_backing_shadow_test = 1;
SYSCTL_INT(_vm, OID_AUTO, map_backing_shadow_test, CTLFLAG_RW,
	   &vm_map_backing_shadow_test, 0, "ba.object shadow test");

static void vmspace_drop_notoken(struct vmspace *vm);
static void vm_map_entry_shadow(vm_map_entry_t entry);
static vm_map_entry_t vm_map_entry_create(int *);
static void vm_map_entry_dispose (vm_map_t map, vm_map_entry_t entry, int *);
static void vm_map_entry_dispose_ba (vm_map_entry_t entry, vm_map_backing_t ba);
static void vm_map_backing_replicated(vm_map_t map,
		vm_map_entry_t entry, int flags);
static void vm_map_backing_adjust_start(vm_map_entry_t entry,
		vm_ooffset_t start);
static void vm_map_backing_adjust_end(vm_map_entry_t entry,
		vm_ooffset_t end);
static void vm_map_backing_attach (vm_map_entry_t entry, vm_map_backing_t ba);
static void vm_map_backing_detach (vm_map_entry_t entry, vm_map_backing_t ba);
static void _vm_map_clip_end (vm_map_t, vm_map_entry_t, vm_offset_t, int *);
static void _vm_map_clip_start (vm_map_t, vm_map_entry_t, vm_offset_t, int *);
static void vm_map_entry_delete (vm_map_t, vm_map_entry_t, int *);
static void vm_map_entry_unwire (vm_map_t, vm_map_entry_t);
static void vm_map_copy_entry (vm_map_t, vm_map_t, vm_map_entry_t,
		vm_map_entry_t);
static void vm_map_unclip_range (vm_map_t map, vm_map_entry_t start_entry,
		vm_offset_t start, vm_offset_t end, int *countp, int flags);
static void vm_map_entry_partition(vm_map_t map, vm_map_entry_t entry,
		vm_offset_t vaddr, int *countp);

#define MAP_BACK_CLIPPED	0x0001
#define MAP_BACK_BASEOBJREFD	0x0002

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
	vmspace_cache = objcache_create_mbacked(M_VMSPACE,
						sizeof(struct vmspace),
						0, ncpus * 4,
						vmspace_ctor, vmspace_dtor,
						NULL);
	zinitna(mapentzone, NULL, 0, 0, ZONE_USE_RESERVE | ZONE_SPECIAL);
	pmap_init2();
	vm_object_init2();
}

/*
 * objcache support.  We leave the pmap root cached as long as possible
 * for performance reasons.
 */
static
boolean_t
vmspace_ctor(void *obj, void *privdata, int ocflags)
{
	struct vmspace *vm = obj;

	bzero(vm, sizeof(*vm));
	vm->vm_refcnt = VM_REF_DELETED;

	return 1;
}

static
void
vmspace_dtor(void *obj, void *privdata)
{
	struct vmspace *vm = obj;

	KKASSERT(vm->vm_refcnt == VM_REF_DELETED);
	pmap_puninit(vmspace_pmap(vm));
}

/*
 * Red black tree functions
 *
 * The caller must hold the related map lock.
 */
static int rb_vm_map_compare(vm_map_entry_t a, vm_map_entry_t b);
RB_GENERATE(vm_map_rb_tree, vm_map_entry, rb_entry, rb_vm_map_compare);

/* a->ba.start is address, and the only field which must be initialized */
static int
rb_vm_map_compare(vm_map_entry_t a, vm_map_entry_t b)
{
	if (a->ba.start < b->ba.start)
		return(-1);
	else if (a->ba.start > b->ba.start)
		return(1);
	return(0);
}

/*
 * Initialize vmspace ref/hold counts vmspace0.  There is a holdcnt for
 * every refcnt.
 */
void
vmspace_initrefs(struct vmspace *vm)
{
	vm->vm_refcnt = 1;
	vm->vm_holdcnt = 1;
}

/*
 * Allocate a vmspace structure, including a vm_map and pmap.
 * Initialize numerous fields.  While the initial allocation is zerod,
 * subsequence reuse from the objcache leaves elements of the structure
 * intact (particularly the pmap), so portions must be zerod.
 *
 * Returns a referenced vmspace.
 *
 * No requirements.
 */
struct vmspace *
vmspace_alloc(vm_offset_t min, vm_offset_t max)
{
	struct vmspace *vm;

	vm = objcache_get(vmspace_cache, M_WAITOK);

	bzero(&vm->vm_startcopy,
	      (char *)&vm->vm_endcopy - (char *)&vm->vm_startcopy);
	vm_map_init(&vm->vm_map, min, max, NULL);	/* initializes token */

	/*
	 * NOTE: hold to acquires token for safety.
	 *
	 * On return vmspace is referenced (refs=1, hold=1).  That is,
	 * each refcnt also has a holdcnt.  There can be additional holds
	 * (holdcnt) above and beyond the refcnt.  Finalization is handled in
	 * two stages, one on refs 1->0, and the the second on hold 1->0.
	 */
	KKASSERT(vm->vm_holdcnt == 0);
	KKASSERT(vm->vm_refcnt == VM_REF_DELETED);
	vmspace_initrefs(vm);
	vmspace_hold(vm);
	pmap_pinit(vmspace_pmap(vm));		/* (some fields reused) */
	vm->vm_map.pmap = vmspace_pmap(vm);	/* XXX */
	vm->vm_shm = NULL;
	vm->vm_flags = 0;
	cpu_vmspace_alloc(vm);
	vmspace_drop(vm);

	return (vm);
}

/*
 * NOTE: Can return 0 if the vmspace is exiting.
 */
int
vmspace_getrefs(struct vmspace *vm)
{
	int32_t n;

	n = vm->vm_refcnt;
	cpu_ccfence();
	if (n & VM_REF_DELETED)
		n = -1;
	return n;
}

void
vmspace_hold(struct vmspace *vm)
{
	atomic_add_int(&vm->vm_holdcnt, 1);
	lwkt_gettoken(&vm->vm_map.token);
}

/*
 * Drop with final termination interlock.
 */
void
vmspace_drop(struct vmspace *vm)
{
	lwkt_reltoken(&vm->vm_map.token);
	vmspace_drop_notoken(vm);
}

static void
vmspace_drop_notoken(struct vmspace *vm)
{
	if (atomic_fetchadd_int(&vm->vm_holdcnt, -1) == 1) {
		if (vm->vm_refcnt & VM_REF_DELETED)
			vmspace_terminate(vm, 1);
	}
}

/*
 * A vmspace object must not be in a terminated state to be able to obtain
 * additional refs on it.
 *
 * These are official references to the vmspace, the count is used to check
 * for vmspace sharing.  Foreign accessors should use 'hold' and not 'ref'.
 *
 * XXX we need to combine hold & ref together into one 64-bit field to allow
 * holds to prevent stage-1 termination.
 */
void
vmspace_ref(struct vmspace *vm)
{
	uint32_t n;

	atomic_add_int(&vm->vm_holdcnt, 1);
	n = atomic_fetchadd_int(&vm->vm_refcnt, 1);
	KKASSERT((n & VM_REF_DELETED) == 0);
}

/*
 * Release a ref on the vmspace.  On the 1->0 transition we do stage-1
 * termination of the vmspace.  Then, on the final drop of the hold we
 * will do stage-2 final termination.
 */
void
vmspace_rel(struct vmspace *vm)
{
	uint32_t n;

	/*
	 * Drop refs.  Each ref also has a hold which is also dropped.
	 *
	 * When refs hits 0 compete to get the VM_REF_DELETED flag (hold
	 * prevent finalization) to start termination processing.
	 * Finalization occurs when the last hold count drops to 0.
	 */
	n = atomic_fetchadd_int(&vm->vm_refcnt, -1) - 1;
	while (n == 0) {
		if (atomic_cmpset_int(&vm->vm_refcnt, 0, VM_REF_DELETED)) {
			vmspace_terminate(vm, 0);
			break;
		}
		n = vm->vm_refcnt;
		cpu_ccfence();
	}
	vmspace_drop_notoken(vm);
}

/*
 * This is called during exit indicating that the vmspace is no
 * longer in used by an exiting process, but the process has not yet
 * been reaped.
 *
 * We drop refs, allowing for stage-1 termination, but maintain a holdcnt
 * to prevent stage-2 until the process is reaped.  Note hte order of
 * operation, we must hold first.
 *
 * No requirements.
 */
void
vmspace_relexit(struct vmspace *vm)
{
	atomic_add_int(&vm->vm_holdcnt, 1);
	vmspace_rel(vm);
}

/*
 * Called during reap to disconnect the remainder of the vmspace from
 * the process.  On the hold drop the vmspace termination is finalized.
 *
 * No requirements.
 */
void
vmspace_exitfree(struct proc *p)
{
	struct vmspace *vm;

	vm = p->p_vmspace;
	p->p_vmspace = NULL;
	vmspace_drop_notoken(vm);
}

/*
 * Called in two cases:
 *
 * (1) When the last refcnt is dropped and the vmspace becomes inactive,
 *     called with final == 0.  refcnt will be (u_int)-1 at this point,
 *     and holdcnt will still be non-zero.
 *
 * (2) When holdcnt becomes 0, called with final == 1.  There should no
 *     longer be anyone with access to the vmspace.
 *
 * VMSPACE_EXIT1 flags the primary deactivation
 * VMSPACE_EXIT2 flags the last reap
 */
static void
vmspace_terminate(struct vmspace *vm, int final)
{
	int count;

	lwkt_gettoken(&vm->vm_map.token);
	if (final == 0) {
		KKASSERT((vm->vm_flags & VMSPACE_EXIT1) == 0);
		vm->vm_flags |= VMSPACE_EXIT1;

		/*
		 * Get rid of most of the resources.  Leave the kernel pmap
		 * intact.
		 *
		 * If the pmap does not contain wired pages we can bulk-delete
		 * the pmap as a performance optimization before removing the
		 * related mappings.
		 *
		 * If the pmap contains wired pages we cannot do this
		 * pre-optimization because currently vm_fault_unwire()
		 * expects the pmap pages to exist and will not decrement
		 * p->wire_count if they do not.
		 */
		shmexit(vm);
		if (vmspace_pmap(vm)->pm_stats.wired_count) {
			vm_map_remove(&vm->vm_map, VM_MIN_USER_ADDRESS,
				      VM_MAX_USER_ADDRESS);
			pmap_remove_pages(vmspace_pmap(vm), VM_MIN_USER_ADDRESS,
					  VM_MAX_USER_ADDRESS);
		} else {
			pmap_remove_pages(vmspace_pmap(vm), VM_MIN_USER_ADDRESS,
					  VM_MAX_USER_ADDRESS);
			vm_map_remove(&vm->vm_map, VM_MIN_USER_ADDRESS,
				      VM_MAX_USER_ADDRESS);
		}
		lwkt_reltoken(&vm->vm_map.token);
	} else {
		KKASSERT((vm->vm_flags & VMSPACE_EXIT1) != 0);
		KKASSERT((vm->vm_flags & VMSPACE_EXIT2) == 0);

		/*
		 * Get rid of remaining basic resources.
		 */
		vm->vm_flags |= VMSPACE_EXIT2;
		shmexit(vm);

		count = vm_map_entry_reserve(MAP_RESERVE_COUNT);
		vm_map_lock(&vm->vm_map);
		cpu_vmspace_free(vm);

		/*
		 * Lock the map, to wait out all other references to it.
		 * Delete all of the mappings and pages they hold, then call
		 * the pmap module to reclaim anything left.
		 */
		vm_map_delete(&vm->vm_map,
			      vm_map_min(&vm->vm_map),
			      vm_map_max(&vm->vm_map),
			      &count);
		vm_map_unlock(&vm->vm_map);
		vm_map_entry_release(count);

		pmap_release(vmspace_pmap(vm));
		lwkt_reltoken(&vm->vm_map.token);
		objcache_put(vmspace_cache, vm);
	}
}

/*
 * Swap useage is determined by taking the proportional swap used by
 * VM objects backing the VM map.  To make up for fractional losses,
 * if the VM object has any swap use at all the associated map entries
 * count for at least 1 swap page.
 *
 * No requirements.
 */
vm_offset_t
vmspace_swap_count(struct vmspace *vm)
{
	vm_map_t map = &vm->vm_map;
	vm_map_entry_t cur;
	vm_object_t object;
	vm_offset_t count = 0;
	vm_offset_t n;

	vmspace_hold(vm);

	RB_FOREACH(cur, vm_map_rb_tree, &map->rb_root) {
		switch(cur->maptype) {
		case VM_MAPTYPE_NORMAL:
		case VM_MAPTYPE_VPAGETABLE:
			if ((object = cur->ba.object) == NULL)
				break;
			if (object->swblock_count) {
				n = (cur->ba.end - cur->ba.start) / PAGE_SIZE;
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
vm_offset_t
vmspace_anonymous_count(struct vmspace *vm)
{
	vm_map_t map = &vm->vm_map;
	vm_map_entry_t cur;
	vm_object_t object;
	vm_offset_t count = 0;

	vmspace_hold(vm);
	RB_FOREACH(cur, vm_map_rb_tree, &map->rb_root) {
		switch(cur->maptype) {
		case VM_MAPTYPE_NORMAL:
		case VM_MAPTYPE_VPAGETABLE:
			if ((object = cur->ba.object) == NULL)
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
 * Initialize an existing vm_map structure such as that in the vmspace
 * structure.  The pmap is initialized elsewhere.
 *
 * No requirements.
 */
void
vm_map_init(struct vm_map *map, vm_offset_t min_addr, vm_offset_t max_addr,
	    pmap_t pmap)
{
	RB_INIT(&map->rb_root);
	spin_init(&map->ilock_spin, "ilock");
	map->ilock_base = NULL;
	map->nentries = 0;
	map->size = 0;
	map->system_map = 0;
	vm_map_min(map) = min_addr;
	vm_map_max(map) = max_addr;
	map->pmap = pmap;
	map->timestamp = 0;
	map->flags = 0;
	bzero(&map->freehint, sizeof(map->freehint));
	lwkt_token_init(&map->token, "vm_map");
	lockinit(&map->lock, "vm_maplk", (hz + 9) / 10, 0);
}

/*
 * Find the first possible free address for the specified request length.
 * Returns 0 if we don't have one cached.
 */
static
vm_offset_t
vm_map_freehint_find(vm_map_t map, vm_size_t length, vm_size_t align)
{
	vm_map_freehint_t *scan;

	scan = &map->freehint[0];
	while (scan < &map->freehint[VM_MAP_FFCOUNT]) {
		if (scan->length == length && scan->align == align)
			return(scan->start);
		++scan;
	}
	return 0;
}

/*
 * Unconditionally set the freehint.  Called by vm_map_findspace() after
 * it finds an address.  This will help us iterate optimally on the next
 * similar findspace.
 */
static
void
vm_map_freehint_update(vm_map_t map, vm_offset_t start,
		       vm_size_t length, vm_size_t align)
{
	vm_map_freehint_t *scan;

	scan = &map->freehint[0];
	while (scan < &map->freehint[VM_MAP_FFCOUNT]) {
		if (scan->length == length && scan->align == align) {
			scan->start = start;
			return;
		}
		++scan;
	}
	scan = &map->freehint[map->freehint_newindex & VM_MAP_FFMASK];
	scan->start = start;
	scan->align = align;
	scan->length = length;
	++map->freehint_newindex;
}

/*
 * Update any existing freehints (for any alignment), for the hole we just
 * added.
 */
static
void
vm_map_freehint_hole(vm_map_t map, vm_offset_t start, vm_size_t length)
{
	vm_map_freehint_t *scan;

	scan = &map->freehint[0];
	while (scan < &map->freehint[VM_MAP_FFCOUNT]) {
		if (scan->length <= length && scan->start > start)
			scan->start = start;
		++scan;
	}
}

/*
 * This function handles MAP_ENTRY_NEEDS_COPY by inserting a fronting
 * object in the entry for COW faults.
 *
 * The entire chain including entry->ba (prior to inserting the fronting
 * object) essentially becomes set in stone... elements of it can be paged
 * in or out, but cannot be further modified.
 *
 * NOTE: If we do not optimize the backing chain then a unique copy is not
 *	 needed.  Note, however, that because portions of the chain are
 *	 shared across pmaps we cannot make any changes to the vm_map_backing
 *	 elements themselves.
 *
 * If the map segment is governed by a virtual page table then it is
 * possible to address offsets beyond the mapped area.  Just allocate
 * a maximally sized object for this case.
 *
 * If addref is non-zero an additional reference is added to the returned
 * entry.  This mechanic exists because the additional reference might have
 * to be added atomically and not after return to prevent a premature
 * collapse.  XXX currently there is no collapse code.
 *
 * The vm_map must be exclusively locked.
 * No other requirements.
 */
static
void
vm_map_entry_shadow(vm_map_entry_t entry)
{
	vm_map_backing_t ba;
	vm_size_t length;
	vm_object_t source;
	vm_object_t result;

	if (entry->maptype == VM_MAPTYPE_VPAGETABLE)
		length = 0x7FFFFFFF;
	else
		length = atop(entry->ba.end - entry->ba.start);

	/*
	 * Don't create the new object if the old object isn't shared.
	 * This case occurs quite often when programs fork/exec/wait.
	 *
	 * Caller ensures source exists (all backing_ba's must have objects),
	 * typically indirectly by virtue of the NEEDS_COPY flag being set.
	 * We have a ref on source by virtue of the entry and do not need
	 * to lock it to do this test.
	 */
	source = entry->ba.object;
	KKASSERT(source);

	if (source->type != OBJT_VNODE) {
		if (source->ref_count == 1 &&
		    source->handle == NULL &&
		    (source->type == OBJT_DEFAULT ||
		     source->type == OBJT_SWAP)) {
			goto done;
		}
	}
	ba = kmalloc(sizeof(*ba), M_MAP_BACKING, M_INTWAIT); /* copied later */
	vm_object_hold_shared(source);

	/*
	 * Once it becomes part of a backing_ba chain it can wind up anywhere,
	 * drop the ONEMAPPING flag now.
	 */
	vm_object_clear_flag(source, OBJ_ONEMAPPING);

	/*
	 * Allocate a new object with the given length.  The new object
	 * is returned referenced but we may have to add another one.
	 * If we are adding a second reference we must clear OBJ_ONEMAPPING.
	 * (typically because the caller is about to clone a vm_map_entry).
	 *
	 * The source object currently has an extra reference to prevent
	 * collapses into it while we mess with its shadow list, which
	 * we will remove later in this routine.
	 *
	 * The target object may require a second reference if asked for one
	 * by the caller.
	 */
	result = vm_object_allocate_hold(OBJT_DEFAULT, length);
	if (result == NULL)
		panic("vm_object_shadow: no object for shadowing");

	/*
	 * The new object shadows the source object.
	 *
	 * Try to optimize the result object's page color when shadowing
	 * in order to maintain page coloring consistency in the combined
	 * shadowed object.
	 *
	 * The source object is moved to ba, retaining its existing ref-count.
	 * No additional ref is needed.
	 *
	 * SHADOWING IS NOT APPLICABLE TO OBJT_VNODE OBJECTS
	 */
	vm_map_backing_detach(entry, &entry->ba);
	*ba = entry->ba;		/* previous ba */
	entry->ba.object = result;	/* new ba (at head of entry) */
	entry->ba.backing_ba = ba;
	entry->ba.backing_count = ba->backing_count + 1;
	entry->ba.offset = 0;

	/* cpu localization twist */
	result->pg_color = vm_quickcolor();

	vm_map_backing_attach(entry, &entry->ba);
	vm_map_backing_attach(entry, ba);

	/*
	 * Adjust the return storage.  Drop the ref on source before
	 * returning.
	 */
	vm_object_drop(result);
	vm_object_drop(source);
done:
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

	/*
	 * ba.offset is NOT cumulatively added in the backing_ba scan like
	 * it was in the old object chain, so we can assign whatever offset
	 * we like to the new object.
	 *
	 * For now assign a value of 0 to make debugging object sizes
	 * easier.
	 */
	entry->ba.offset = 0;

	if (entry->maptype == VM_MAPTYPE_VPAGETABLE) {
		/* XXX */
		obj = vm_object_allocate(OBJT_DEFAULT, 0x7FFFFFFF);
	} else {
		obj = vm_object_allocate(OBJT_DEFAULT,
					 atop(entry->ba.end - entry->ba.start) +
					 entry->ba.offset);
	}
	entry->ba.object = obj;
	vm_map_backing_attach(entry, &entry->ba);
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
 *
 * WARNING! Take care not to have too-big a static/BSS structure here
 *	    as MAXCPU can be 256+, otherwise the loader's 64MB heap
 *	    can get blown out by the kernel plus the initrd image.
 */
void
vm_map_entry_reserve_cpu_init(globaldata_t gd)
{
	vm_map_entry_t entry;
	int count;
	int i;

	atomic_add_int(&gd->gd_vme_avail, -MAP_RESERVE_COUNT * 2);
	if (gd->gd_cpuid == 0) {
		entry = &cpu_map_entry_init_bsp[0];
		count = MAPENTRYBSP_CACHE;
	} else {
		entry = &cpu_map_entry_init_ap[gd->gd_cpuid][0];
		count = MAPENTRYAP_CACHE;
	}
	for (i = 0; i < count; ++i, ++entry) {
		MAPENT_FREELIST(entry) = gd->gd_vme_base;
		gd->gd_vme_base = entry;
	}
}

/*
 * Reserves vm_map_entry structures so code later-on can manipulate
 * map_entry structures within a locked map without blocking trying
 * to allocate a new vm_map_entry.
 *
 * No requirements.
 *
 * WARNING!  We must not decrement gd_vme_avail until after we have
 *	     ensured that sufficient entries exist, otherwise we can
 *	     get into an endless call recursion in the zalloc code
 *	     itself.
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
	 * Use a critical section to protect against VM faults.  It might
	 * not be needed, but we have to be careful here.
	 */
	if (gd->gd_vme_avail < count) {
		crit_enter();
		while (gd->gd_vme_avail < count) {
			entry = zalloc(mapentzone);
			MAPENT_FREELIST(entry) = gd->gd_vme_base;
			gd->gd_vme_base = entry;
			atomic_add_int(&gd->gd_vme_avail, 1);
		}
		crit_exit();
	}
	atomic_add_int(&gd->gd_vme_avail, -count);

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
	vm_map_entry_t efree;

	count = atomic_fetchadd_int(&gd->gd_vme_avail, count) + count;
	if (gd->gd_vme_avail > MAP_RESERVE_SLOP) {
		efree = NULL;
		crit_enter();
		while (gd->gd_vme_avail > MAP_RESERVE_HYST) {
			entry = gd->gd_vme_base;
			KKASSERT(entry != NULL);
			gd->gd_vme_base = MAPENT_FREELIST(entry);
			atomic_add_int(&gd->gd_vme_avail, -1);
			MAPENT_FREELIST(entry) = efree;
			efree = entry;
		}
		crit_exit();
		while ((entry = efree) != NULL) {
			efree = MAPENT_FREELIST(efree);
			zfree(mapentzone, entry);
		}
	}
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

	atomic_add_int(&gd->gd_vme_avail, -count);
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

	atomic_add_int(&gd->gd_vme_avail, count);
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
vm_map_entry_create(int *countp)
{
	struct globaldata *gd = mycpu;
	vm_map_entry_t entry;

	KKASSERT(*countp > 0);
	--*countp;
	crit_enter();
	entry = gd->gd_vme_base;
	KASSERT(entry != NULL, ("gd_vme_base NULL! count %d", *countp));
	gd->gd_vme_base = MAPENT_FREELIST(entry);
	crit_exit();

	return(entry);
}

/*
 * Attach and detach backing store elements
 */
static void
vm_map_backing_attach(vm_map_entry_t entry, vm_map_backing_t ba)
{
	vm_object_t obj;

	switch(entry->maptype) {
	case VM_MAPTYPE_VPAGETABLE:
	case VM_MAPTYPE_NORMAL:
		obj = ba->object;
		lockmgr(&obj->backing_lk, LK_EXCLUSIVE);
		TAILQ_INSERT_TAIL(&obj->backing_list, ba, entry);
		lockmgr(&obj->backing_lk, LK_RELEASE);
		break;
	case VM_MAPTYPE_UKSMAP:
		ba->uksmap(ba, UKSMAPOP_ADD, entry->aux.dev, NULL);
		break;
	}
}

static void
vm_map_backing_detach(vm_map_entry_t entry, vm_map_backing_t ba)
{
	vm_object_t obj;

	switch(entry->maptype) {
	case VM_MAPTYPE_VPAGETABLE:
	case VM_MAPTYPE_NORMAL:
		obj = ba->object;
		lockmgr(&obj->backing_lk, LK_EXCLUSIVE);
		TAILQ_REMOVE(&obj->backing_list, ba, entry);
		lockmgr(&obj->backing_lk, LK_RELEASE);
		break;
	case VM_MAPTYPE_UKSMAP:
		ba->uksmap(ba, UKSMAPOP_REM, entry->aux.dev, NULL);
		break;
	}
}

/*
 * Dispose of the dynamically allocated backing_ba chain associated
 * with a vm_map_entry.
 *
 * We decrement the (possibly shared) element and kfree() on the
 * 1->0 transition.  We only iterate to the next backing_ba when
 * the previous one went through a 1->0 transition.
 *
 * These can only be normal vm_object based backings.
 */
static void
vm_map_entry_dispose_ba(vm_map_entry_t entry, vm_map_backing_t ba)
{
	vm_map_backing_t next;

	while (ba) {
		if (ba->map_object) {
			vm_map_backing_detach(entry, ba);
			vm_object_deallocate(ba->object);
		}
		next = ba->backing_ba;
		kfree(ba, M_MAP_BACKING);
		ba = next;
	}
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

	/*
	 * Dispose of the base object and the backing link.
	 */
	switch(entry->maptype) {
	case VM_MAPTYPE_NORMAL:
	case VM_MAPTYPE_VPAGETABLE:
		if (entry->ba.map_object) {
			vm_map_backing_detach(entry, &entry->ba);
			vm_object_deallocate(entry->ba.object);
		}
		break;
	case VM_MAPTYPE_SUBMAP:
		break;
	case VM_MAPTYPE_UKSMAP:
		vm_map_backing_detach(entry, &entry->ba);
		break;
	default:
		break;
	}
	vm_map_entry_dispose_ba(entry, entry->ba.backing_ba);

	/*
	 * Cleanup for safety.
	 */
	entry->ba.backing_ba = NULL;
	entry->ba.object = NULL;
	entry->ba.offset = 0;

	++*countp;
	crit_enter();
	MAPENT_FREELIST(entry) = gd->gd_vme_base;
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
vm_map_entry_link(vm_map_t map, vm_map_entry_t entry)
{
	ASSERT_VM_MAP_LOCKED(map);

	map->nentries++;
	if (vm_map_rb_tree_RB_INSERT(&map->rb_root, entry))
		panic("vm_map_entry_link: dup addr map %p ent %p", map, entry);
}

static __inline void
vm_map_entry_unlink(vm_map_t map,
		    vm_map_entry_t entry)
{
	ASSERT_VM_MAP_LOCKED(map);

	if (entry->eflags & MAP_ENTRY_IN_TRANSITION) {
		panic("vm_map_entry_unlink: attempt to mess with "
		      "locked entry! %p", entry);
	}
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

	/*
	 * Locate the record from the top of the tree.  'last' tracks the
	 * closest prior record and is returned if no match is found, which
	 * in binary tree terms means tracking the most recent right-branch
	 * taken.  If there is no prior record, *entry is set to NULL.
	 */
	last = NULL;
	tmp = RB_ROOT(&map->rb_root);

	while (tmp) {
		if (address >= tmp->ba.start) {
			if (address < tmp->ba.end) {
				*entry = tmp;
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
 * making call to account for the new entry.  XXX API is a bit messy.
 */
int
vm_map_insert(vm_map_t map, int *countp,
	      void *map_object, void *map_aux,
	      vm_ooffset_t offset, void *aux_info,
	      vm_offset_t start, vm_offset_t end,
	      vm_maptype_t maptype, vm_subsys_t id,
	      vm_prot_t prot, vm_prot_t max, int cow)
{
	vm_map_entry_t new_entry;
	vm_map_entry_t prev_entry;
	vm_map_entry_t next;
	vm_map_entry_t temp_entry;
	vm_eflags_t protoeflags;
	vm_object_t object;
	int must_drop = 0;

	if (maptype == VM_MAPTYPE_UKSMAP)
		object = NULL;
	else
		object = map_object;

	ASSERT_VM_MAP_LOCKED(map);
	if (object)
		ASSERT_LWKT_TOKEN_HELD(vm_object_token(object));

	/*
	 * Check that the start and end points are not bogus.
	 */
	if ((start < vm_map_min(map)) || (end > vm_map_max(map)) ||
	    (start >= end)) {
		return (KERN_INVALID_ADDRESS);
	}

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
	if (prev_entry)
		next = vm_map_rb_tree_RB_NEXT(prev_entry);
	else
		next = RB_MIN(vm_map_rb_tree, &map->rb_root);
	if (next && next->ba.start < end)
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
		;
	} else if (prev_entry &&
		 (prev_entry->eflags == protoeflags) &&
		 (prev_entry->ba.end == start) &&
		 (prev_entry->wired_count == 0) &&
		 (prev_entry->id == id) &&
		 prev_entry->maptype == maptype &&
		 maptype == VM_MAPTYPE_NORMAL &&
		 prev_entry->ba.backing_ba == NULL &&	/* not backed */
		 ((prev_entry->ba.object == NULL) ||
		  vm_object_coalesce(prev_entry->ba.object,
				     OFF_TO_IDX(prev_entry->ba.offset),
				     (vm_size_t)(prev_entry->ba.end - prev_entry->ba.start),
				     (vm_size_t)(end - prev_entry->ba.end)))) {
		/*
		 * We were able to extend the object.  Determine if we
		 * can extend the previous map entry to include the 
		 * new range as well.
		 */
		if ((prev_entry->inheritance == VM_INHERIT_DEFAULT) &&
		    (prev_entry->protection == prot) &&
		    (prev_entry->max_protection == max)) {
			map->size += (end - prev_entry->ba.end);
			vm_map_backing_adjust_end(prev_entry, end);
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
		object = prev_entry->ba.object;
		offset = prev_entry->ba.offset +
			(prev_entry->ba.end - prev_entry->ba.start);
		if (object) {
			vm_object_hold(object);
			vm_object_lock_swap(); /* map->token order */
			vm_object_reference_locked(object);
			map_object = object;
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
	new_entry = vm_map_entry_create(countp);
	new_entry->ba.pmap = map->pmap;
	new_entry->ba.start = start;
	new_entry->ba.end = end;
	new_entry->id = id;

	new_entry->maptype = maptype;
	new_entry->eflags = protoeflags;
	new_entry->aux.master_pde = 0;		/* in case size is different */
	new_entry->aux.map_aux = map_aux;
	new_entry->ba.map_object = map_object;
	new_entry->ba.backing_ba = NULL;
	new_entry->ba.backing_count = 0;
	new_entry->ba.offset = offset;
	new_entry->ba.aux_info = aux_info;
	new_entry->ba.flags = 0;
	new_entry->ba.pmap = map->pmap;

	new_entry->inheritance = VM_INHERIT_DEFAULT;
	new_entry->protection = prot;
	new_entry->max_protection = max;
	new_entry->wired_count = 0;

	/*
	 * Insert the new entry into the list
	 */
	vm_map_backing_replicated(map, new_entry, MAP_BACK_BASEOBJREFD);
	vm_map_entry_link(map, new_entry);
	map->size += new_entry->ba.end - new_entry->ba.start;

	/*
	 * Don't worry about updating freehint[] when inserting, allow
	 * addresses to be lower than the actual first free spot.
	 */
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
	    maptype != VM_MAPTYPE_VPAGETABLE &&
	    maptype != VM_MAPTYPE_UKSMAP) {
		int dorelock = 0;
		if (vm_map_relock_enable && (cow & MAP_PREFAULT_RELOCK)) {
			dorelock = 1;
			vm_object_lock_swap();
			vm_object_drop(object);
		}
		pmap_object_init_pt(map->pmap, new_entry,
				    new_entry->ba.start,
				    new_entry->ba.end - new_entry->ba.start,
				    cow & MAP_PREFAULT_PARTIAL);
		if (dorelock) {
			vm_object_hold(object);
			vm_object_lock_swap();
		}
	}
	lwkt_reltoken(&map->token);
	if (must_drop)
		vm_object_drop(object);

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
	vm_map_entry_t entry;
	vm_map_entry_t tmp;
	vm_offset_t hole_start;
	vm_offset_t end;
	vm_offset_t align_mask;

	if (start < vm_map_min(map))
		start = vm_map_min(map);
	if (start > vm_map_max(map))
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
	 * Use freehint to adjust the start point, hopefully reducing
	 * the iteration to O(1).
	 */
	hole_start = vm_map_freehint_find(map, length, align);
	if (start < hole_start)
		start = hole_start;
	if (vm_map_lookup_entry(map, start, &tmp))
		start = tmp->ba.end;
	entry = tmp;	/* may be NULL */

	/*
	 * Look through the rest of the map, trying to fit a new region in the
	 * gap between existing regions, or after the very last region.
	 */
	for (;;) {
		/*
		 * Adjust the proposed start by the requested alignment,
		 * be sure that we didn't wrap the address.
		 */
		if (align_mask == (vm_offset_t)-1)
			end = roundup(start, align);
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
		if (end > vm_map_max(map) || end < start)
			return (1);

		/*
		 * Locate the next entry, we can stop if this is the
		 * last entry (we know we are in-bounds so that would
		 * be a sucess).
		 */
		if (entry)
			entry = vm_map_rb_tree_RB_NEXT(entry);
		else
			entry = RB_MIN(vm_map_rb_tree, &map->rb_root);
		if (entry == NULL)
			break;

		/*
		 * Determine if the proposed area would overlap the
		 * next entry.
		 *
		 * When matching against a STACK entry, only allow the
		 * memory map to intrude on the ungrown portion of the
		 * STACK entry when MAP_TRYFIXED is set.
		 */
		if (entry->ba.start >= end) {
			if ((entry->eflags & MAP_ENTRY_STACK) == 0)
				break;
			if (flags & MAP_TRYFIXED)
				break;
			if (entry->ba.start - entry->aux.avail_ssize >= end)
				break;
		}
		start = entry->ba.end;
	}

	/*
	 * Update the freehint
	 */
	vm_map_freehint_update(map, start, length, align);

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
vm_map_find(vm_map_t map, void *map_object, void *map_aux,
	    vm_ooffset_t offset, vm_offset_t *addr,
	    vm_size_t length, vm_size_t align, boolean_t fitit,
	    vm_maptype_t maptype, vm_subsys_t id,
	    vm_prot_t prot, vm_prot_t max, int cow)
{
	vm_offset_t start;
	vm_object_t object;
	void *aux_info;
	int result;
	int count;

	/*
	 * Certain UKSMAPs may need aux_info.
	 *
	 * (map_object is the callback function, aux_info is the process
	 *  or thread, if necessary).
	 */
	aux_info = NULL;
	if (maptype == VM_MAPTYPE_UKSMAP) {
		KKASSERT(map_aux != NULL && map_object != NULL);

		switch(minor(((struct cdev *)map_aux))) {
		case 5:
			/*
			 * /dev/upmap
			 */
			aux_info = curproc;
			break;
		case 6:
			/*
			 * /dev/kpmap
			 */
			break;
		case 7:
			/*
			 * /dev/lpmap
			 */
			aux_info = curthread->td_lwp;
			break;
		}
		object = NULL;
	} else {
		object = map_object;
	}

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
	result = vm_map_insert(map, &count,
			       map_object, map_aux,
			       offset, aux_info,
			       start, start + length,
			       maptype, id, prot, max, cow);
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
	if (entry->maptype == VM_MAPTYPE_UKSMAP)
		return;

	prev = vm_map_rb_tree_RB_PREV(entry);
	if (prev) {
		prevsize = prev->ba.end - prev->ba.start;
		if ( (prev->ba.end == entry->ba.start) &&
		     (prev->maptype == entry->maptype) &&
		     (prev->ba.object == entry->ba.object) &&
		     (prev->ba.backing_ba == entry->ba.backing_ba) &&
		     (!prev->ba.object ||
			(prev->ba.offset + prevsize == entry->ba.offset)) &&
		     (prev->eflags == entry->eflags) &&
		     (prev->protection == entry->protection) &&
		     (prev->max_protection == entry->max_protection) &&
		     (prev->inheritance == entry->inheritance) &&
		     (prev->id == entry->id) &&
		     (prev->wired_count == entry->wired_count)) {
			/*
			 * NOTE: order important.  Unlink before gumming up
			 *	 the RBTREE w/adjust, adjust before disposal
			 *	 of prior entry, to avoid pmap snafus.
			 */
			vm_map_entry_unlink(map, prev);
			vm_map_backing_adjust_start(entry, prev->ba.start);
			if (entry->ba.object == NULL)
				entry->ba.offset = 0;
			vm_map_entry_dispose(map, prev, countp);
		}
	}

	next = vm_map_rb_tree_RB_NEXT(entry);
	if (next) {
		esize = entry->ba.end - entry->ba.start;
		if ((entry->ba.end == next->ba.start) &&
		    (next->maptype == entry->maptype) &&
		    (next->ba.object == entry->ba.object) &&
		     (prev->ba.backing_ba == entry->ba.backing_ba) &&
		     (!entry->ba.object ||
			(entry->ba.offset + esize == next->ba.offset)) &&
		    (next->eflags == entry->eflags) &&
		    (next->protection == entry->protection) &&
		    (next->max_protection == entry->max_protection) &&
		    (next->inheritance == entry->inheritance) &&
		    (next->id == entry->id) &&
		    (next->wired_count == entry->wired_count)) {
			/*
			 * NOTE: order important.  Unlink before gumming up
			 *	 the RBTREE w/adjust, adjust before disposal
			 *	 of prior entry, to avoid pmap snafus.
			 */
			vm_map_entry_unlink(map, next);
			vm_map_backing_adjust_end(entry, next->ba.end);
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
	if (startaddr > entry->ba.start)				\
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
	if (entry->ba.object == NULL && !map->system_map &&
	    VM_MAP_ENTRY_WITHIN_PARTITION(entry)) {
		vm_map_entry_allocate_object(entry);
	}

	/*
	 * NOTE: The replicated function will adjust start, end, and offset
	 *	 for the remainder of the backing_ba linkages.  We must fixup
	 *	 the embedded ba.
	 */
	new_entry = vm_map_entry_create(countp);
	*new_entry = *entry;
	new_entry->ba.end = start;

	/*
	 * Ordering is important, make sure the new entry is replicated
	 * before we cut the exiting entry.
	 */
	vm_map_backing_replicated(map, new_entry, MAP_BACK_CLIPPED);
	vm_map_backing_adjust_start(entry, start);
	vm_map_entry_link(map, new_entry);
}

/*
 * Asserts that the given entry ends at or before the specified address.
 * If necessary, it splits the entry into two.
 *
 * The map must be exclusively locked.
 */
#define vm_map_clip_end(map, entry, endaddr, countp)		\
{								\
	if (endaddr < entry->ba.end)				\
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

	if (entry->ba.object == NULL && !map->system_map &&
	    VM_MAP_ENTRY_WITHIN_PARTITION(entry)) {
		vm_map_entry_allocate_object(entry);
	}

	/*
	 * Create a new entry and insert it AFTER the specified entry
	 *
	 * NOTE: The replicated function will adjust start, end, and offset
	 *	 for the remainder of the backing_ba linkages.  We must fixup
	 *	 the embedded ba.
	 */
	new_entry = vm_map_entry_create(countp);
	*new_entry = *entry;
	new_entry->ba.start = end;
	new_entry->ba.offset += (new_entry->ba.start - entry->ba.start);

	/*
	 * Ordering is important, make sure the new entry is replicated
	 * before we cut the exiting entry.
	 */
	vm_map_backing_replicated(map, new_entry, MAP_BACK_CLIPPED);
	vm_map_backing_adjust_end(entry, end);
	vm_map_entry_link(map, new_entry);
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
vm_map_transition_wait(vm_map_t map, int relock)
{
	tsleep_interlock(map, 0);
	vm_map_unlock(map);
	tsleep(map, PINTERLOCKED, "vment", 0);
	if (relock)
		vm_map_lock(map);
}

/*
 * When we do blocking operations with the map lock held it is
 * possible that a clip might have occured on our in-transit entry,
 * requiring an adjustment to the entry in our loop.  These macros
 * help the pageable and clip_range code deal with the case.  The
 * conditional costs virtually nothing if no clipping has occured.
 */

#define CLIP_CHECK_BACK(entry, save_start)			\
    do {							\
	    while (entry->ba.start != save_start) {		\
		    entry = vm_map_rb_tree_RB_PREV(entry);	\
		    KASSERT(entry, ("bad entry clip")); 	\
	    }							\
    } while(0)

#define CLIP_CHECK_FWD(entry, save_end)				\
    do {							\
	    while (entry->ba.end != save_end) {			\
		    entry = vm_map_rb_tree_RB_NEXT(entry);	\
		    KASSERT(entry, ("bad entry clip")); 	\
	    }							\
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
	vm_map_entry_t next;

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
		vm_map_transition_wait(map, 1);
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
	for (;;) {
		next = vm_map_rb_tree_RB_NEXT(entry);
		if (next == NULL || next->ba.start >= end)
			break;
		if (flags & MAP_CLIP_NO_HOLES) {
			if (next->ba.start > entry->ba.end) {
				vm_map_unclip_range(map, start_entry,
					start, entry->ba.end, countp, flags);
				return(NULL);
			}
		}

		if (next->eflags & MAP_ENTRY_IN_TRANSITION) {
			vm_offset_t save_end = entry->ba.end;
			next->eflags |= MAP_ENTRY_NEEDS_WAKEUP;
			++mycpu->gd_cnt.v_intrans_coll;
			++mycpu->gd_cnt.v_intrans_wait;
			vm_map_transition_wait(map, 1);

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
		if (entry->ba.end != end) {
			vm_map_unclip_range(map, start_entry,
				start, entry->ba.end, countp, flags);
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

	KASSERT(entry->ba.start == start, ("unclip_range: illegal base entry"));
	while (entry && entry->ba.start < end) {
		KASSERT(entry->eflags & MAP_ENTRY_IN_TRANSITION,
			("in-transition flag not set during unclip on: %p",
			entry));
		KASSERT(entry->ba.end <= end,
			("unclip_range: tail wasn't clipped"));
		entry->eflags &= ~MAP_ENTRY_IN_TRANSITION;
		if (entry->eflags & MAP_ENTRY_NEEDS_WAKEUP) {
			entry->eflags &= ~MAP_ENTRY_NEEDS_WAKEUP;
			wakeup(map);
		}
		entry = vm_map_rb_tree_RB_NEXT(entry);
	}

	/*
	 * Simplification does not block so there is no restart case.
	 */
	entry = start_entry;
	while (entry && entry->ba.start < end) {
		vm_map_simplify_entry(map, entry, countp);
		entry = vm_map_rb_tree_RB_NEXT(entry);
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
	} else if (entry) {
		entry = vm_map_rb_tree_RB_NEXT(entry);
	} else {
		entry = RB_MIN(vm_map_rb_tree, &map->rb_root);
	}

	vm_map_clip_end(map, entry, end, &count);

	if ((entry->ba.start == start) && (entry->ba.end == end) &&
	    ((entry->eflags & MAP_ENTRY_COW) == 0) &&
	    (entry->ba.object == NULL)) {
		entry->ba.sub_map = submap;
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
	} else if (entry) {
		entry = vm_map_rb_tree_RB_NEXT(entry);
	} else {
		entry = RB_MIN(vm_map_rb_tree, &map->rb_root);
	}

	/*
	 * Make a first pass to check for protection violations.
	 */
	current = entry;
	while (current && current->ba.start < end) {
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

		/*
		 * When making a SHARED+RW file mmap writable, update
		 * v_lastwrite_ts.
		 */
		if (new_prot & PROT_WRITE &&
		    (current->eflags & MAP_ENTRY_NEEDS_COPY) == 0 &&
		    (current->maptype == VM_MAPTYPE_NORMAL ||
		     current->maptype == VM_MAPTYPE_VPAGETABLE) &&
		    current->ba.object &&
		    current->ba.object->type == OBJT_VNODE) {
			struct vnode *vp;

			vp = current->ba.object->handle;
			if (vp && vn_lock(vp, LK_EXCLUSIVE | LK_RETRY | LK_NOWAIT) == 0) {
				vfs_timestamp(&vp->v_lastwrite_ts);
				vsetflags(vp, VLASTWRITETS);
				vn_unlock(vp);
			}
		}
		current = vm_map_rb_tree_RB_NEXT(current);
	}

	/*
	 * Go back and fix up protections. [Note that clipping is not
	 * necessary the second time.]
	 */
	current = entry;

	while (current && current->ba.start < end) {
		vm_prot_t old_prot;

		vm_map_clip_end(map, current, end, &count);

		old_prot = current->protection;
		if (set_max) {
			current->max_protection = new_prot;
			current->protection = new_prot & old_prot;
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

			pmap_protect(map->pmap, current->ba.start,
			    current->ba.end,
			    current->protection & MASK(current));
#undef	MASK
		}

		vm_map_simplify_entry(map, current, &count);

		current = vm_map_rb_tree_RB_NEXT(current);
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
		modify_map = 1;
		vm_map_lock(map);
		break;
	case MADV_INVAL:
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
	} else if (entry) {
		entry = vm_map_rb_tree_RB_NEXT(entry);
	} else {
		entry = RB_MIN(vm_map_rb_tree, &map->rb_root);
	}

	if (modify_map) {
		/*
		 * madvise behaviors that are implemented in the vm_map_entry.
		 *
		 * We clip the vm_map_entry so that behavioral changes are
		 * limited to the specified address range.
		 */
		for (current = entry;
		     current && current->ba.start < end;
		     current = vm_map_rb_tree_RB_NEXT(current)) {
			/*
			 * Ignore submaps
			 */
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
					    current->ba.start, current->ba.end);
				break;
			case MADV_INVAL:
				/*
				 * Invalidate the related pmap entries, used
				 * to flush portions of the real kernel's
				 * pmap when the caller has removed or
				 * modified existing mappings in a virtual
				 * page table.
				 *
				 * (exclusive locked map version does not
				 * need the range interlock).
				 */
				pmap_remove(map->pmap,
					    current->ba.start, current->ba.end);
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
		vm_pindex_t delta;

		/*
		 * madvise behaviors that are implemented in the underlying
		 * vm_object.
		 *
		 * Since we don't clip the vm_map_entry, we have to clip
		 * the vm_object pindex and count.
		 *
		 * NOTE!  These functions are only supported on normal maps,
		 *	  except MADV_INVAL which is also supported on
		 *	  virtual page tables.
		 *
		 * NOTE!  These functions only apply to the top-most object.
		 *	  It is not applicable to backing objects.
		 */
		for (current = entry;
		     current && current->ba.start < end;
		     current = vm_map_rb_tree_RB_NEXT(current)) {
			vm_offset_t useStart;

			if (current->maptype != VM_MAPTYPE_NORMAL &&
			    (current->maptype != VM_MAPTYPE_VPAGETABLE ||
			     behav != MADV_INVAL)) {
				continue;
			}

			pindex = OFF_TO_IDX(current->ba.offset);
			delta = atop(current->ba.end - current->ba.start);
			useStart = current->ba.start;

			if (current->ba.start < start) {
				pindex += atop(start - current->ba.start);
				delta -= atop(start - current->ba.start);
				useStart = start;
			}
			if (current->ba.end > end)
				delta -= atop(current->ba.end - end);

			if ((vm_spindex_t)delta <= 0)
				continue;

			if (behav == MADV_INVAL) {
				/*
				 * Invalidate the related pmap entries, used
				 * to flush portions of the real kernel's
				 * pmap when the caller has removed or
				 * modified existing mappings in a virtual
				 * page table.
				 *
				 * (shared locked map version needs the
				 * interlock, see vm_fault()).
				 */
				struct vm_map_ilock ilock;

				KASSERT(useStart >= VM_MIN_USER_ADDRESS &&
					    useStart + ptoa(delta) <=
					    VM_MAX_USER_ADDRESS,
					 ("Bad range %016jx-%016jx (%016jx)",
					 useStart, useStart + ptoa(delta),
					 delta));
				vm_map_interlock(map, &ilock,
						 useStart,
						 useStart + ptoa(delta));
				pmap_remove(map->pmap,
					    useStart,
					    useStart + ptoa(delta));
				vm_map_deinterlock(map, &ilock);
			} else {
				vm_object_madvise(current->ba.object,
						  pindex, delta, behav);
			}

			/*
			 * Try to populate the page table.  Mappings governed
			 * by virtual page tables cannot be pre-populated
			 * without a lot of work so don't try.
			 */
			if (behav == MADV_WILLNEED &&
			    current->maptype != VM_MAPTYPE_VPAGETABLE) {
				pmap_object_init_pt(
				    map->pmap, current,
				    useStart,
				    (delta << PAGE_SHIFT),
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
	} else if (temp_entry) {
		entry = vm_map_rb_tree_RB_NEXT(temp_entry);
	} else {
		entry = RB_MIN(vm_map_rb_tree, &map->rb_root);
	}

	while (entry && entry->ba.start < end) {
		vm_map_clip_end(map, entry, end, &count);

		entry->inheritance = new_inheritance;

		vm_map_simplify_entry(map, entry, &count);

		entry = vm_map_rb_tree_RB_NEXT(entry);
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
		while (entry && entry->ba.start < end) {
			vm_offset_t save_start;
			vm_offset_t save_end;

			/*
			 * Already user wired or hard wired (trivial cases)
			 */
			if (entry->eflags & MAP_ENTRY_USER_WIRED) {
				entry = vm_map_rb_tree_RB_NEXT(entry);
				continue;
			}
			if (entry->wired_count != 0) {
				entry->wired_count++;
				entry->eflags |= MAP_ENTRY_USER_WIRED;
				entry = vm_map_rb_tree_RB_NEXT(entry);
				continue;
			}

			/*
			 * A new wiring requires instantiation of appropriate
			 * management structures and the faulting in of the
			 * page.
			 */
			if (entry->maptype == VM_MAPTYPE_NORMAL ||
			    entry->maptype == VM_MAPTYPE_VPAGETABLE) {
				int copyflag = entry->eflags &
					       MAP_ENTRY_NEEDS_COPY;
				if (copyflag && ((entry->protection &
						  VM_PROT_WRITE) != 0)) {
					vm_map_entry_shadow(entry);
				} else if (entry->ba.object == NULL &&
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
			save_start = entry->ba.start;
			save_end = entry->ba.end;
			rv = vm_fault_wire(map, entry, TRUE, 0);
			if (rv) {
				CLIP_CHECK_BACK(entry, save_start);
				for (;;) {
					KASSERT(entry->wired_count == 1, ("bad wired_count on entry"));
					entry->eflags &= ~MAP_ENTRY_USER_WIRED;
					entry->wired_count = 0;
					if (entry->ba.end == save_end)
						break;
					entry = vm_map_rb_tree_RB_NEXT(entry);
					KASSERT(entry,
					     ("bad entry clip during backout"));
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
			entry = vm_map_rb_tree_RB_NEXT(entry);
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
		while (entry && entry->ba.start < end) {
			if ((entry->eflags & MAP_ENTRY_USER_WIRED) == 0) {
				rv = KERN_INVALID_ARGUMENT;
				goto done;
			}
			KASSERT(entry->wired_count != 0,
				("wired count was 0 with USER_WIRED set! %p",
				 entry));
			entry = vm_map_rb_tree_RB_NEXT(entry);
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
		while (entry && entry->ba.start < end) {
			KASSERT(entry->eflags & MAP_ENTRY_USER_WIRED,
				("expected USER_WIRED on entry %p", entry));
			entry->eflags &= ~MAP_ENTRY_USER_WIRED;
			entry->wired_count--;
			if (entry->wired_count == 0)
				vm_fault_unwire(map, entry);
			entry = vm_map_rb_tree_RB_NEXT(entry);
		}
	}
done:
	vm_map_unclip_range(map, start_entry, start, real_end, &count,
		MAP_CLIP_NO_HOLES);
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
		while (entry && entry->ba.start < end) {
			/*
			 * Trivial case if the entry is already wired
			 */
			if (entry->wired_count) {
				entry->wired_count++;
				entry = vm_map_rb_tree_RB_NEXT(entry);
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
			if (entry->maptype == VM_MAPTYPE_NORMAL ||
			    entry->maptype == VM_MAPTYPE_VPAGETABLE) {
				int copyflag = entry->eflags &
					       MAP_ENTRY_NEEDS_COPY;
				if (copyflag && ((entry->protection &
						  VM_PROT_WRITE) != 0)) {
					vm_map_entry_shadow(entry);
				} else if (entry->ba.object == NULL &&
					   !map->system_map) {
					vm_map_entry_allocate_object(entry);
				}
			}
			entry->wired_count++;
			entry = vm_map_rb_tree_RB_NEXT(entry);
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
		while (entry && entry->ba.start < end) {
			/*
			 * If vm_fault_wire fails for any page we need to undo
			 * what has been done.  We decrement the wiring count
			 * for those pages which have not yet been wired (now)
			 * and unwire those that have (later).
			 */
			vm_offset_t save_start = entry->ba.start;
			vm_offset_t save_end = entry->ba.end;

			if (entry->wired_count == 1)
				rv = vm_fault_wire(map, entry, FALSE, kmflags);
			if (rv) {
				CLIP_CHECK_BACK(entry, save_start);
				for (;;) {
					KASSERT(entry->wired_count == 1,
					  ("wired_count changed unexpectedly"));
					entry->wired_count = 0;
					if (entry->ba.end == save_end)
						break;
					entry = vm_map_rb_tree_RB_NEXT(entry);
					KASSERT(entry,
					  ("bad entry clip during backout"));
				}
				end = save_start;
				break;
			}
			CLIP_CHECK_FWD(entry, save_end);
			entry = vm_map_rb_tree_RB_NEXT(entry);
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
		while (entry && entry->ba.start < end) {
			if (entry->wired_count == 0) {
				rv = KERN_INVALID_ARGUMENT;
				goto done;
			}
			entry = vm_map_rb_tree_RB_NEXT(entry);
		}

		/*
		 * Now decrement the wiring count for each region. If a region
		 * becomes completely unwired, unwire its physical pages and
		 * mappings.
		 */
		entry = start_entry;
		while (entry && entry->ba.start < end) {
			entry->wired_count--;
			if (entry->wired_count == 0)
				vm_fault_unwire(map, entry);
			entry = vm_map_rb_tree_RB_NEXT(entry);
		}
	}
done:
	vm_map_unclip_range(map, start_entry, start, real_end,
			    &count, MAP_CLIP_NO_HOLES);
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
	scan = entry;
	while (scan && scan->ba.start < addr + size) {
		KKASSERT(scan->wired_count == 0);
		scan->wired_count = 1;
		scan = vm_map_rb_tree_RB_NEXT(scan);
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
	vm_map_entry_t next;
	vm_map_entry_t entry;
	vm_map_backing_t ba;
	vm_size_t size;
	vm_object_t object;
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
	current = entry;
	while (current && current->ba.start < end) {
		if (current->maptype == VM_MAPTYPE_SUBMAP) {
			lwkt_reltoken(&map->token);
			vm_map_unlock_read(map);
			return (KERN_INVALID_ARGUMENT);
		}
		next = vm_map_rb_tree_RB_NEXT(current);
		if (end > current->ba.end &&
		    (next == NULL ||
		     current->ba.end != next->ba.start)) {
			lwkt_reltoken(&map->token);
			vm_map_unlock_read(map);
			return (KERN_INVALID_ADDRESS);
		}
		current = next;
	}

	if (invalidate)
		pmap_remove(vm_map_pmap(map), start, end);

	/*
	 * Make a second pass, cleaning/uncaching pages from the indicated
	 * objects as we go.
	 */
	current = entry;
	while (current && current->ba.start < end) {
		offset = current->ba.offset + (start - current->ba.start);
		size = (end <= current->ba.end ? end : current->ba.end) - start;

		switch(current->maptype) {
		case VM_MAPTYPE_SUBMAP:
		{
			vm_map_t smap;
			vm_map_entry_t tentry;
			vm_size_t tsize;

			smap = current->ba.sub_map;
			vm_map_lock_read(smap);
			vm_map_lookup_entry(smap, offset, &tentry);
			if (tentry == NULL) {
				tsize = vm_map_max(smap) - offset;
				ba = NULL;
				offset = 0 + (offset - vm_map_min(smap));
			} else {
				tsize = tentry->ba.end - offset;
				ba = &tentry->ba;
				offset = tentry->ba.offset +
					 (offset - tentry->ba.start);
			}
			vm_map_unlock_read(smap);
			if (tsize < size)
				size = tsize;
			break;
		}
		case VM_MAPTYPE_NORMAL:
		case VM_MAPTYPE_VPAGETABLE:
			ba = &current->ba;
			break;
		default:
			ba = NULL;
			break;
		}
		if (ba) {
			object = ba->object;
			if (object)
				vm_object_hold(object);
		} else {
			object = NULL;
		}

		/*
		 * Note that there is absolutely no sense in writing out
		 * anonymous objects, so we track down the vnode object
		 * to write out.
		 * We invalidate (remove) all pages from the address space
		 * anyway, for semantic correctness.
		 *
		 * note: certain anonymous maps, such as MAP_NOSYNC maps,
		 * may start out with a NULL object.
		 *
		 * XXX do we really want to stop at the first backing store
		 * here if there are more? XXX
		 */
		if (ba) {
			vm_object_t tobj;

			tobj = object;
			while (ba->backing_ba != NULL) {
				offset -= ba->offset;
				ba = ba->backing_ba;
				offset += ba->offset;
				tobj = ba->object;
				if (tobj->size < OFF_TO_IDX(offset + size))
					size = IDX_TO_OFF(tobj->size) - offset;
				break; /* XXX this break is not correct */
			}
			if (object != tobj) {
				if (object)
					vm_object_drop(object);
				object = tobj;
				vm_object_hold(object);
			}
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
		current = vm_map_rb_tree_RB_NEXT(current);
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
	map->size -= entry->ba.end - entry->ba.start;
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
	vm_offset_t hole_start;

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
	 * Adjust freehint[] for either the clip case or the extension case.
	 *
	 * GGG see other GGG comment.
	 */
	if (vm_map_lookup_entry(map, start, &first_entry)) {
		entry = first_entry;
		vm_map_clip_start(map, entry, start, countp);
		hole_start = start;
	} else {
		if (first_entry) {
			entry = vm_map_rb_tree_RB_NEXT(first_entry);
			if (entry == NULL)
				hole_start = first_entry->ba.start;
			else
				hole_start = first_entry->ba.end;
		} else {
			entry = RB_MIN(vm_map_rb_tree, &map->rb_root);
			if (entry == NULL)
				hole_start = vm_map_min(map);
			else
				hole_start = vm_map_max(map);
		}
	}

	/*
	 * Step through all entries in this region
	 */
	while (entry && entry->ba.start < end) {
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
			start = entry->ba.start;
			++mycpu->gd_cnt.v_intrans_coll;
			++mycpu->gd_cnt.v_intrans_wait;
			vm_map_transition_wait(map, 1);
			goto again;
		}
		vm_map_clip_end(map, entry, end, countp);

		s = entry->ba.start;
		e = entry->ba.end;
		next = vm_map_rb_tree_RB_NEXT(entry);

		offidxstart = OFF_TO_IDX(entry->ba.offset);
		count = OFF_TO_IDX(e - s);

		switch(entry->maptype) {
		case VM_MAPTYPE_NORMAL:
		case VM_MAPTYPE_VPAGETABLE:
		case VM_MAPTYPE_SUBMAP:
			object = entry->ba.object;
			break;
		default:
			object = NULL;
			break;
		}

		/*
		 * Unwire before removing addresses from the pmap; otherwise,
		 * unwiring will put the entries back in the pmap.
		 *
		 * Generally speaking, doing a bulk pmap_remove() before
		 * removing the pages from the VM object is better at
		 * reducing unnecessary IPIs.  The pmap code is now optimized
		 * to not blindly iterate the range when pt and pd pages
		 * are missing.
		 */
		if (entry->wired_count != 0)
			vm_map_entry_unwire(map, entry);

		offidxend = offidxstart + count;

		if (object == &kernel_object) {
			pmap_remove(map->pmap, s, e);
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
			pmap_remove(map->pmap, s, e);

			if (object != NULL &&
			    object->ref_count != 1 &&
			    (object->flags & (OBJ_NOSPLIT|OBJ_ONEMAPPING)) ==
			     OBJ_ONEMAPPING &&
			    (object->type == OBJT_DEFAULT ||
			     object->type == OBJT_SWAP)) {
				/*
				 * When ONEMAPPING is set we can destroy the
				 * pages underlying the entry's range.
				 */
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
			vm_object_drop(object);
		} else if (entry->maptype == VM_MAPTYPE_UKSMAP) {
			pmap_remove(map->pmap, s, e);
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

	/*
	 * We either reached the end and use vm_map_max as the end
	 * address, or we didn't and we use the next entry as the
	 * end address.
	 */
	if (entry == NULL) {
		vm_map_freehint_hole(map, hole_start,
				     vm_map_max(map) - hole_start);
	} else {
		vm_map_freehint_hole(map, hole_start,
				     entry->ba.start - hole_start);
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
		if (entry == NULL) {
			result = FALSE;
			break;
		}

		/*
		 * No holes allowed!
		 */

		if (start < entry->ba.start) {
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
		start = entry->ba.end;
		entry = vm_map_rb_tree_RB_NEXT(entry);
	}
	if (have_lock == FALSE)
		vm_map_unlock_read(map);
	return (result);
}

/*
 * vm_map_backing structures are not shared across forks and must be
 * replicated.
 *
 * Generally speaking we must reallocate the backing_ba sequence and
 * also adjust it for any changes made to the base entry->ba.start and
 * entry->ba.end.  The first ba in the chain is of course &entry->ba,
 * so we only need to adjust subsequent ba's start, end, and offset.
 *
 * MAP_BACK_CLIPPED	- Called as part of a clipping replication.
 *			  Do not clear OBJ_ONEMAPPING.
 *
 * MAP_BACK_BASEOBJREFD - Called from vm_map_insert().  The base object
 *			  has already been referenced.
 */
static
void
vm_map_backing_replicated(vm_map_t map, vm_map_entry_t entry, int flags)
{
	vm_map_backing_t ba;
	vm_map_backing_t nba;
	vm_object_t object;

	ba = &entry->ba;
	for (;;) {
		ba->pmap = map->pmap;

		if (ba->map_object) {
			switch(entry->maptype) {
			case VM_MAPTYPE_VPAGETABLE:
			case VM_MAPTYPE_NORMAL:
				object = ba->object;
				if (ba != &entry->ba ||
				    (flags & MAP_BACK_BASEOBJREFD) == 0) {
					vm_object_reference_quick(object);
				}
				vm_map_backing_attach(entry, ba);
				if ((flags & MAP_BACK_CLIPPED) == 0 &&
				    object->ref_count > 1) {
					vm_object_clear_flag(object,
							     OBJ_ONEMAPPING);
				}
				break;
			case VM_MAPTYPE_UKSMAP:
				vm_map_backing_attach(entry, ba);
				break;
			default:
				break;
			}
		}
		if (ba->backing_ba == NULL)
			break;

		/*
		 * NOTE: The aux_info field is retained.
		 */
		nba = kmalloc(sizeof(*nba), M_MAP_BACKING, M_INTWAIT);
		*nba = *ba->backing_ba;
		nba->offset += (ba->start - nba->start);  /* += (new - old) */
		nba->start = ba->start;
		nba->end = ba->end;
		ba->backing_ba = nba;
		ba = nba;
		/* pmap is replaced at the top of the loop */
	}
}

static
void
vm_map_backing_adjust_start(vm_map_entry_t entry, vm_ooffset_t start)
{
	vm_map_backing_t ba;

	if (entry->maptype == VM_MAPTYPE_VPAGETABLE ||
	    entry->maptype == VM_MAPTYPE_NORMAL) {
		for (ba = &entry->ba; ba; ba = ba->backing_ba) {
			if (ba->object) {
				lockmgr(&ba->object->backing_lk, LK_EXCLUSIVE);
				ba->offset += (start - ba->start);
				ba->start = start;
				lockmgr(&ba->object->backing_lk, LK_RELEASE);
			} else {
				ba->offset += (start - ba->start);
				ba->start = start;
			}
		}
	} else {
		/* not an object and can't be shadowed */
	}
}

static
void
vm_map_backing_adjust_end(vm_map_entry_t entry, vm_ooffset_t end)
{
	vm_map_backing_t ba;

	if (entry->maptype == VM_MAPTYPE_VPAGETABLE ||
	    entry->maptype == VM_MAPTYPE_NORMAL) {
		for (ba = &entry->ba; ba; ba = ba->backing_ba) {
			if (ba->object) {
				lockmgr(&ba->object->backing_lk, LK_EXCLUSIVE);
				ba->end = end;
				lockmgr(&ba->object->backing_lk, LK_RELEASE);
			} else {
				ba->end = end;
			}
		}
	} else {
		/* not an object and can't be shadowed */
	}
}

/*
 * Handles the dirty work of making src_entry and dst_entry copy-on-write
 * after src_entry has been cloned to dst_entry.  For normal entries only.
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
	vm_object_t obj;

	KKASSERT(dst_entry->maptype == VM_MAPTYPE_NORMAL ||
		 dst_entry->maptype == VM_MAPTYPE_VPAGETABLE);

	if (src_entry->wired_count &&
	    src_entry->maptype != VM_MAPTYPE_VPAGETABLE) {
		/*
		 * Of course, wired down pages can't be set copy-on-write.
		 * Cause wired pages to be copied into the new map by
		 * simulating faults (the new pages are pageable)
		 *
		 * Scrap ba.object (its ref-count has not yet been adjusted
		 * so we can just NULL out the field).  Remove the backing
		 * store.
		 *
		 * Then call vm_fault_copy_entry() to create a new object
		 * in dst_entry and copy the wired pages from src to dst.
		 *
		 * The fault-copy code doesn't work with virtual page
		 * tables.
		 *
		 * NOTE: obj is not actually an object for all MAPTYPEs,
		 *	 just test against NULL.
		 */
		if (dst_entry->ba.map_object != NULL) {
			vm_map_backing_detach(dst_entry, &dst_entry->ba);
			dst_entry->ba.map_object = NULL;
			vm_map_entry_dispose_ba(dst_entry,
						dst_entry->ba.backing_ba);
			dst_entry->ba.backing_ba = NULL;
			dst_entry->ba.backing_count = 0;
		}
		vm_fault_copy_entry(dst_map, src_map, dst_entry, src_entry);
	} else {
		if ((src_entry->eflags & MAP_ENTRY_NEEDS_COPY) == 0) {
			/*
			 * If the source entry is not already marked NEEDS_COPY
			 * we need to write-protect the PTEs.
			 */
			pmap_protect(src_map->pmap,
				     src_entry->ba.start,
				     src_entry->ba.end,
				     src_entry->protection & ~VM_PROT_WRITE);
		}

		/*
		 * dst_entry.ba_object might be stale.  Update it (its
		 * ref-count has not yet been updated so just overwrite
		 * the field).
		 *
		 * If there is no object then we are golden.  Also, in
		 * this situation if there are no backing_ba linkages then
		 * we can set ba.offset to whatever we want.  For now we
		 * set the offset for 0 for make debugging object sizes
		 * easier.
		 */
		obj = src_entry->ba.object;

		if (obj) {
			src_entry->eflags |= (MAP_ENTRY_COW |
					      MAP_ENTRY_NEEDS_COPY);
			dst_entry->eflags |= (MAP_ENTRY_COW |
					      MAP_ENTRY_NEEDS_COPY);
			KKASSERT(dst_entry->ba.offset == src_entry->ba.offset);
		} else {
			dst_entry->ba.offset = 0;
		}

		/*
		 * Normal, allow the backing_ba link depth to
		 * increase.
		 */
		pmap_copy(dst_map->pmap, src_map->pmap,
			  dst_entry->ba.start,
			  dst_entry->ba.end - dst_entry->ba.start,
			  src_entry->ba.start);
	}
}

/*
 * Create a vmspace for a new process and its related vm_map based on an
 * existing vmspace.  The new map inherits information from the old map
 * according to inheritance settings.
 *
 * The source map must not be locked.
 * No requirements.
 */
static void vmspace_fork_normal_entry(vm_map_t old_map, vm_map_t new_map,
			  vm_map_entry_t old_entry, int *countp);
static void vmspace_fork_uksmap_entry(struct proc *p2, struct lwp *lp2,
			  vm_map_t old_map, vm_map_t new_map,
			  vm_map_entry_t old_entry, int *countp);

struct vmspace *
vmspace_fork(struct vmspace *vm1, struct proc *p2, struct lwp *lp2)
{
	struct vmspace *vm2;
	vm_map_t old_map = &vm1->vm_map;
	vm_map_t new_map;
	vm_map_entry_t old_entry;
	int count;

	lwkt_gettoken(&vm1->vm_map.token);
	vm_map_lock(old_map);

	vm2 = vmspace_alloc(vm_map_min(old_map), vm_map_max(old_map));
	lwkt_gettoken(&vm2->vm_map.token);

	/*
	 * We must bump the timestamp to force any concurrent fault
	 * to retry.
	 */
	bcopy(&vm1->vm_startcopy, &vm2->vm_startcopy,
	      (caddr_t)&vm1->vm_endcopy - (caddr_t)&vm1->vm_startcopy);
	new_map = &vm2->vm_map;	/* XXX */
	new_map->timestamp = 1;

	vm_map_lock(new_map);

	count = old_map->nentries;
	count = vm_map_entry_reserve(count + MAP_RESERVE_COUNT);

	RB_FOREACH(old_entry, vm_map_rb_tree, &old_map->rb_root) {
		switch(old_entry->maptype) {
		case VM_MAPTYPE_SUBMAP:
			panic("vm_map_fork: encountered a submap");
			break;
		case VM_MAPTYPE_UKSMAP:
			vmspace_fork_uksmap_entry(p2, lp2,
						  old_map, new_map,
						  old_entry, &count);
			break;
		case VM_MAPTYPE_NORMAL:
		case VM_MAPTYPE_VPAGETABLE:
			vmspace_fork_normal_entry(old_map, new_map,
						  old_entry, &count);
			break;
		}
	}

	new_map->size = old_map->size;
	vm_map_unlock(new_map);
	vm_map_unlock(old_map);
	vm_map_entry_release(count);

	lwkt_reltoken(&vm2->vm_map.token);
	lwkt_reltoken(&vm1->vm_map.token);

	return (vm2);
}

static
void
vmspace_fork_normal_entry(vm_map_t old_map, vm_map_t new_map,
			  vm_map_entry_t old_entry, int *countp)
{
	vm_map_entry_t new_entry;
	vm_map_backing_t ba;
	vm_object_t object;

	/*
	 * If the backing_ba link list gets too long then fault it
	 * all into the head object and dispose of the list.  We do
	 * this in old_entry prior to cloning in order to benefit both
	 * parent and child.
	 *
	 * We can test our fronting object's size against its
	 * resident_page_count for a really cheap (but probably not perfect)
	 * all-shadowed test, allowing us to disconnect the backing_ba
	 * link list early.
	 *
	 * XXX Currently doesn't work for VPAGETABLEs (the entire object
	 *     would have to be copied).
	 */
	object = old_entry->ba.object;
	if (old_entry->ba.backing_ba &&
	    old_entry->maptype != VM_MAPTYPE_VPAGETABLE &&
	    (old_entry->ba.backing_count >= vm_map_backing_limit ||
	     (vm_map_backing_shadow_test && object &&
	      object->size == object->resident_page_count))) {
		/*
		 * If there are too many backing_ba linkages we
		 * collapse everything into the head
		 *
		 * This will also remove all the pte's.
		 */
		if (old_entry->eflags & MAP_ENTRY_NEEDS_COPY)
			vm_map_entry_shadow(old_entry);
		if (object == NULL)
			vm_map_entry_allocate_object(old_entry);
		if (vm_fault_collapse(old_map, old_entry) == KERN_SUCCESS) {
			ba = old_entry->ba.backing_ba;
			old_entry->ba.backing_ba = NULL;
			old_entry->ba.backing_count = 0;
			vm_map_entry_dispose_ba(old_entry, ba);
		}
	}
	object = NULL;	/* object variable is now invalid */

	/*
	 * Fork the entry
	 */
	switch (old_entry->inheritance) {
	case VM_INHERIT_NONE:
		break;
	case VM_INHERIT_SHARE:
		/*
		 * Clone the entry as a shared entry.  This will look like
		 * shared memory across the old and the new process.  We must
		 * ensure that the object is allocated.
		 */
		if (old_entry->ba.object == NULL)
			vm_map_entry_allocate_object(old_entry);

		if (old_entry->eflags & MAP_ENTRY_NEEDS_COPY) {
			/*
			 * Create the fronting vm_map_backing for
			 * an entry which needs a copy, plus an extra
			 * ref because we are going to duplicate it
			 * in the fork.
			 *
			 * The call to vm_map_entry_shadow() will also clear
			 * OBJ_ONEMAPPING.
			 *
			 * XXX no more collapse.  Still need extra ref
			 * for the fork.
			 */
			vm_map_entry_shadow(old_entry);
		} else if (old_entry->ba.object) {
			object = old_entry->ba.object;
		}

		/*
		 * Clone the entry.  We've already bumped the ref on
		 * the vm_object for our new entry.
		 */
		new_entry = vm_map_entry_create(countp);
		*new_entry = *old_entry;

		new_entry->eflags &= ~MAP_ENTRY_USER_WIRED;
		new_entry->wired_count = 0;

		/*
		 * Replicate and index the vm_map_backing.  Don't share
		 * the vm_map_backing across vm_map's (only across clips).
		 *
		 * Insert the entry into the new map -- we know we're
		 * inserting at the end of the new map.
		 */
		vm_map_backing_replicated(new_map, new_entry, 0);
		vm_map_entry_link(new_map, new_entry);

		/*
		 * Update the physical map
		 */
		pmap_copy(new_map->pmap, old_map->pmap,
			  new_entry->ba.start,
			  (old_entry->ba.end - old_entry->ba.start),
			  old_entry->ba.start);
		break;
	case VM_INHERIT_COPY:
		/*
		 * Clone the entry and link the copy into the new map.
		 *
		 * Note that ref-counting adjustment for old_entry->ba.object
		 * (if it isn't a special map that is) is handled by
		 * vm_map_copy_entry().
		 */
		new_entry = vm_map_entry_create(countp);
		*new_entry = *old_entry;

		new_entry->eflags &= ~MAP_ENTRY_USER_WIRED;
		new_entry->wired_count = 0;

		vm_map_backing_replicated(new_map, new_entry, 0);
		vm_map_entry_link(new_map, new_entry);

		/*
		 * This does the actual dirty work of making both entries
		 * copy-on-write, and will also handle the fronting object.
		 */
		vm_map_copy_entry(old_map, new_map, old_entry, new_entry);
		break;
	}
}

/*
 * When forking user-kernel shared maps, the map might change in the
 * child so do not try to copy the underlying pmap entries.
 */
static
void
vmspace_fork_uksmap_entry(struct proc *p2, struct lwp *lp2,
			  vm_map_t old_map, vm_map_t new_map,
			  vm_map_entry_t old_entry, int *countp)
{
	vm_map_entry_t new_entry;

	/*
	 * Do not fork lpmap entries whos TIDs do not match lp2's tid.
	 *
	 * XXX if p2 is NULL and lp2 is non-NULL, we retain the lpmap entry
	 * (this is for e.g. resident'ing vmspace's) but set the field
	 * to NULL.  Upon restore it should be restored. XXX NOT IMPL YET
	 */
	if (old_entry->aux.dev) {
		switch(minor(old_entry->aux.dev)) {
		case 5:
			break;
		case 6:
			break;
		case 7:
			if (lp2 == NULL)
				return;
			if (old_entry->ba.aux_info == NULL)
				return;
			if (((struct lwp *)old_entry->ba.aux_info)->lwp_tid !=
			    lp2->lwp_tid)
				return;
			break;
		}
	}

	new_entry = vm_map_entry_create(countp);
	*new_entry = *old_entry;

	new_entry->eflags &= ~MAP_ENTRY_USER_WIRED;
	new_entry->wired_count = 0;
	KKASSERT(new_entry->ba.backing_ba == NULL);

	if (new_entry->aux.dev) {
		switch(minor(new_entry->aux.dev)) {
		case 5:
			/*
			 * upmap
			 */
			new_entry->ba.aux_info = p2;
			break;
		case 6:
			/*
			 * kpmap
			 */
			new_entry->ba.aux_info = NULL;
			break;
		case 7:
			/*
			 * lpmap
			 */
			new_entry->ba.aux_info = lp2;
			break;
		}
	} else {
		new_entry->ba.aux_info = NULL;
	}

	vm_map_backing_replicated(new_map, new_entry, 0);

	vm_map_entry_link(new_map, new_entry);
}

/*
 * Create an auto-grow stack entry
 *
 * No requirements.
 */
int
vm_map_stack (vm_map_t map, vm_offset_t *addrbos, vm_size_t max_ssize,
	      int flags, vm_prot_t prot, vm_prot_t max, int cow)
{
	vm_map_entry_t	prev_entry;
	vm_map_entry_t	next;
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
		if (vm_map_findspace(map, *addrbos, max_ssize, 1,
				     flags, &tmpaddr)) {
			vm_map_unlock(map);
			vm_map_entry_release(count);
			return (KERN_NO_SPACE);
		}
		*addrbos = tmpaddr;
	}

	/* If addr is already mapped, no go */
	if (vm_map_lookup_entry(map, *addrbos, &prev_entry)) {
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
	if (prev_entry)
		next = vm_map_rb_tree_RB_NEXT(prev_entry);
	else
		next = RB_MIN(vm_map_rb_tree, &map->rb_root);

	if (next && next->ba.start < *addrbos + max_ssize) {
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
			   NULL, NULL,
			   0, NULL,
			   *addrbos + max_ssize - init_ssize,
	                   *addrbos + max_ssize,
			   VM_MAPTYPE_NORMAL,
			   VM_SUBSYS_STACK, prot, max, cow);

	/* Now set the avail_ssize amount */
	if (rv == KERN_SUCCESS) {
		if (prev_entry)
			next = vm_map_rb_tree_RB_NEXT(prev_entry);
		else
			next = RB_MIN(vm_map_rb_tree, &map->rb_root);
		if (prev_entry != NULL) {
			vm_map_clip_end(map,
					prev_entry,
					*addrbos + max_ssize - init_ssize,
					&count);
		}
		if (next->ba.end   != *addrbos + max_ssize ||
		    next->ba.start != *addrbos + max_ssize - init_ssize){
			panic ("Bad entry start/end for new stack entry");
		} else {
			next->aux.avail_ssize = max_ssize - init_ssize;
		}
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
vm_map_growstack (vm_map_t map, vm_offset_t addr)
{
	vm_map_entry_t prev_entry;
	vm_map_entry_t stack_entry;
	vm_map_entry_t next;
	struct vmspace *vm;
	struct lwp *lp;
	struct proc *p;
	vm_offset_t    end;
	int grow_amount;
	int rv = KERN_SUCCESS;
	int is_procstack;
	int use_read_lock = 1;
	int count;

	/*
	 * Find the vm
	 */
	lp = curthread->td_lwp;
	p = curthread->td_proc;
	KKASSERT(lp != NULL);
	vm = lp->lwp_vmspace;

	/*
	 * Growstack is only allowed on the current process.  We disallow
	 * other use cases, e.g. trying to access memory via procfs that
	 * the stack hasn't grown into.
	 */
	if (map != &vm->vm_map) {
		return KERN_FAILURE;
	}

	count = vm_map_entry_reserve(MAP_RESERVE_COUNT);
Retry:
	if (use_read_lock)
		vm_map_lock_read(map);
	else
		vm_map_lock(map);

	/*
	 * If addr is already in the entry range, no need to grow.
	 * prev_entry returns NULL if addr is at the head.
	 */
	if (vm_map_lookup_entry(map, addr, &prev_entry))
		goto done;
	if (prev_entry)
		stack_entry = vm_map_rb_tree_RB_NEXT(prev_entry);
	else
		stack_entry = RB_MIN(vm_map_rb_tree, &map->rb_root);

	if (stack_entry == NULL)
		goto done;
	if (prev_entry == NULL)
		end = stack_entry->ba.start - stack_entry->aux.avail_ssize;
	else
		end = prev_entry->ba.end;

	/*
	 * This next test mimics the old grow function in vm_machdep.c.
	 * It really doesn't quite make sense, but we do it anyway
	 * for compatibility.
	 *
	 * If not growable stack, return success.  This signals the
	 * caller to proceed as he would normally with normal vm.
	 */
	if (stack_entry->aux.avail_ssize < 1 ||
	    addr >= stack_entry->ba.start ||
	    addr <  stack_entry->ba.start - stack_entry->aux.avail_ssize) {
		goto done;
	} 
	
	/* Find the minimum grow amount */
	grow_amount = roundup (stack_entry->ba.start - addr, PAGE_SIZE);
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
	if (grow_amount > stack_entry->ba.start - end) {
		if (use_read_lock && vm_map_lock_upgrade(map)) {
			/* lost lock */
			use_read_lock = 0;
			goto Retry;
		}
		use_read_lock = 0;
		stack_entry->aux.avail_ssize = stack_entry->ba.start - end;
		rv = KERN_NO_SPACE;
		goto done;
	}

	is_procstack = addr >= (vm_offset_t)vm->vm_maxsaddr;

	/* If this is the main process stack, see if we're over the 
	 * stack limit.
	 */
	if (is_procstack && (vm->vm_ssize + grow_amount >
			     p->p_rlimit[RLIMIT_STACK].rlim_cur)) {
		rv = KERN_NO_SPACE;
		goto done;
	}

	/* Round up the grow amount modulo SGROWSIZ */
	grow_amount = roundup (grow_amount, sgrowsiz);
	if (grow_amount > stack_entry->aux.avail_ssize) {
		grow_amount = stack_entry->aux.avail_ssize;
	}
	if (is_procstack && (vm->vm_ssize + grow_amount >
	                     p->p_rlimit[RLIMIT_STACK].rlim_cur)) {
		grow_amount = p->p_rlimit[RLIMIT_STACK].rlim_cur - vm->vm_ssize;
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
	addr = stack_entry->ba.start - grow_amount;

	/* If this puts us into the previous entry, cut back our growth
	 * to the available space.  Also, see the note above.
	 */
	if (addr < end) {
		stack_entry->aux.avail_ssize = stack_entry->ba.start - end;
		addr = end;
	}

	rv = vm_map_insert(map, &count,
			   NULL, NULL,
			   0, NULL,
			   addr, stack_entry->ba.start,
			   VM_MAPTYPE_NORMAL,
			   VM_SUBSYS_STACK, VM_PROT_ALL, VM_PROT_ALL, 0);

	/* Adjust the available stack space by the amount we grew. */
	if (rv == KERN_SUCCESS) {
		if (prev_entry) {
			vm_map_clip_end(map, prev_entry, addr, &count);
			next = vm_map_rb_tree_RB_NEXT(prev_entry);
		} else {
			next = RB_MIN(vm_map_rb_tree, &map->rb_root);
		}
		if (next->ba.end != stack_entry->ba.start  ||
		    next->ba.start != addr) {
			panic ("Bad stack grow start/end in new stack entry");
		} else {
			next->aux.avail_ssize =
				stack_entry->aux.avail_ssize -
				(next->ba.end - next->ba.start);
			if (is_procstack) {
				vm->vm_ssize += next->ba.end -
						next->ba.start;
			}
		}

		if (map->flags & MAP_WIREFUTURE)
			vm_map_unwire(map, next->ba.start, next->ba.end, FALSE);
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
		newvmspace = vmspace_fork(vmcopy, NULL, NULL);
		lwkt_gettoken(&newvmspace->vm_map.token);
	} else {
		newvmspace = vmspace_alloc(vm_map_min(map), vm_map_max(map));
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
	vmspace_rel(oldvmspace);
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
	if (vmspace_getrefs(oldvmspace) == 1) {
		lwkt_reltoken(&oldvmspace->vm_map.token);
		return;
	}
	newvmspace = vmspace_fork(oldvmspace, NULL, NULL);
	lwkt_gettoken(&newvmspace->vm_map.token);
	pmap_pinit2(vmspace_pmap(newvmspace));
	pmap_replacevm(p, newvmspace, 0);
	lwkt_reltoken(&newvmspace->vm_map.token);
	lwkt_reltoken(&oldvmspace->vm_map.token);
	vmspace_rel(oldvmspace);
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
	struct rlimit limit;
	rlim_t dsiz;

	/*
	 * Acquire datasize limit for mmap() operation,
	 * calculate nearest power of 2.
	 */
	if (kern_getrlimit(RLIMIT_DATA, &limit))
		limit.rlim_cur = maxdsiz;
	dsiz = limit.rlim_cur;

	if (!randomize_mmap || addr != 0) {
		/*
		 * Set a reasonable start point for the hint if it was
		 * not specified or if it falls within the heap space.
		 * Hinted mmap()s do not allocate out of the heap space.
		 */
		if (addr == 0 ||
		    (addr >= round_page((vm_offset_t)vms->vm_taddr) &&
		     addr < round_page((vm_offset_t)vms->vm_daddr + dsiz))) {
			addr = round_page((vm_offset_t)vms->vm_daddr + dsiz);
		}

		return addr;
	}

	/*
	 * randomize_mmap && addr == 0.  For now randomize the
	 * address within a dsiz range beyond the data limit.
	 */
	addr = (vm_offset_t)vms->vm_daddr + dsiz;
	if (dsiz)
		addr += (karc4random64() & 0x7FFFFFFFFFFFFFFFLU) % dsiz;
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
	      struct vm_map_backing **bap,	/* OUT */
	      vm_pindex_t *pindex,		/* OUT */
	      vm_pindex_t *pcount,		/* OUT */
	      vm_prot_t *out_prot,		/* OUT */
	      int *wflags)			/* OUT */
{
	vm_map_entry_t entry;
	vm_map_t map = *var_map;
	vm_prot_t prot;
	vm_prot_t fault_type = fault_typea;
	int use_read_lock = 1;
	int rv = KERN_SUCCESS;
	int count;
	thread_t td = curthread;

	/*
	 * vm_map_entry_reserve() implements an important mitigation
	 * against mmap() span running the kernel out of vm_map_entry
	 * structures, but it can also cause an infinite call recursion.
	 * Use td_nest_count to prevent an infinite recursion (allows
	 * the vm_map code to dig into the pcpu vm_map_entry reserve).
	 */
	count = 0;
	if (td->td_nest_count == 0) {
		++td->td_nest_count;
		count = vm_map_entry_reserve(MAP_RESERVE_COUNT);
		--td->td_nest_count;
	}
RetryLookup:
	if (use_read_lock)
		vm_map_lock_read(map);
	else
		vm_map_lock(map);

	/*
	 * Always do a full lookup.  The hint doesn't get us much anymore
	 * now that the map is RB'd.
	 */
	cpu_ccfence();
	*out_entry = NULL;
	*bap = NULL;

	{
		vm_map_entry_t tmp_entry;

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

		*var_map = map = entry->ba.sub_map;
		if (use_read_lock)
			vm_map_unlock_read(old_map);
		else
			vm_map_unlock(old_map);
		use_read_lock = 1;
		goto RetryLookup;
	}

	/*
	 * Check whether this task is allowed to have this page.
	 * Note the special case for MAP_ENTRY_COW pages with an override.
	 * This is to implement a forced COW for debuggers.
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
	*wflags = 0;
	if (entry->wired_count) {
		*wflags |= FW_WIRED;
		prot = fault_type = entry->protection;
	}

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
	 * Only NORMAL and VPAGETABLE maps are object-based.  UKSMAPs are not.
	 */
	if (entry->maptype != VM_MAPTYPE_NORMAL &&
	    entry->maptype != VM_MAPTYPE_VPAGETABLE) {
		*bap = NULL;
		goto skip;
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
			 * Not allowed if TDF_NOFAULT is set as the shadowing
			 * operation can deadlock against the faulting
			 * function due to the copy-on-write.
			 */
			if (curthread->td_flags & TDF_NOFAULT) {
				rv = KERN_FAILURE_NOFAULT;
				goto done;
			}

			/*
			 * Make a new vm_map_backing + object, and place it
			 * in the object chain.  Note that no new references
			 * have appeared -- one just moved from the map to
			 * the new object.
			 */
			if (use_read_lock && vm_map_lock_upgrade(map)) {
				/* lost lock */
				use_read_lock = 0;
				goto RetryLookup;
			}
			use_read_lock = 0;
			vm_map_entry_shadow(entry);
			*wflags |= FW_DIDCOW;
		} else {
			/*
			 * We're attempting to read a copy-on-write page --
			 * don't allow writes.
			 */
			prot &= ~VM_PROT_WRITE;
		}
	}

	/*
	 * Create an object if necessary.  This code also handles
	 * partitioning large entries to improve vm_fault performance.
	 */
	if (entry->ba.object == NULL && !map->system_map) {
		if (use_read_lock && vm_map_lock_upgrade(map))  {
			/* lost lock */
			use_read_lock = 0;
			goto RetryLookup;
		}
		use_read_lock = 0;

		/*
		 * Partition large entries, giving each its own VM object,
		 * to improve concurrent fault performance.  This is only
		 * applicable to userspace.
		 */
		if (map != &kernel_map &&
		    entry->maptype == VM_MAPTYPE_NORMAL &&
		    ((entry->ba.start ^ entry->ba.end) &
		     ~MAP_ENTRY_PARTITION_MASK) &&
		    vm_map_partition_enable) {
			if (entry->eflags & MAP_ENTRY_IN_TRANSITION) {
				entry->eflags |= MAP_ENTRY_NEEDS_WAKEUP;
				++mycpu->gd_cnt.v_intrans_coll;
				++mycpu->gd_cnt.v_intrans_wait;
				vm_map_transition_wait(map, 0);
				goto RetryLookup;
			}
			vm_map_entry_partition(map, entry, vaddr, &count);
		}
		vm_map_entry_allocate_object(entry);
	}

	/*
	 * Return the object/offset from this entry.  If the entry was
	 * copy-on-write or empty, it has been fixed up.
	 */
	*bap = &entry->ba;

skip:
	*pindex = OFF_TO_IDX((vaddr - entry->ba.start) + entry->ba.offset);
	*pcount = OFF_TO_IDX(entry->ba.end - trunc_page(vaddr));

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
	if (count > 0)
		vm_map_entry_release(count);

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

static void
vm_map_entry_partition(vm_map_t map, vm_map_entry_t entry,
		       vm_offset_t vaddr, int *countp)
{
	vaddr &= ~MAP_ENTRY_PARTITION_MASK;
	vm_map_clip_start(map, entry, vaddr, countp);
	vaddr += MAP_ENTRY_PARTITION_SIZE;
	vm_map_clip_end(map, entry, vaddr, countp);
}

/*
 * Quick hack, needs some help to make it more SMP friendly.
 */
void
vm_map_interlock(vm_map_t map, struct vm_map_ilock *ilock,
		 vm_offset_t ran_beg, vm_offset_t ran_end)
{
	struct vm_map_ilock *scan;

	ilock->ran_beg = ran_beg;
	ilock->ran_end = ran_end;
	ilock->flags = 0;

	spin_lock(&map->ilock_spin);
restart:
	for (scan = map->ilock_base; scan; scan = scan->next) {
		if (ran_end > scan->ran_beg && ran_beg < scan->ran_end) {
			scan->flags |= ILOCK_WAITING;
			ssleep(scan, &map->ilock_spin, 0, "ilock", 0);
			goto restart;
		}
	}
	ilock->next = map->ilock_base;
	map->ilock_base = ilock;
	spin_unlock(&map->ilock_spin);
}

void
vm_map_deinterlock(vm_map_t map, struct  vm_map_ilock *ilock)
{
	struct vm_map_ilock *scan;
	struct vm_map_ilock **scanp;

	spin_lock(&map->ilock_spin);
	scanp = &map->ilock_base;
	while ((scan = *scanp) != NULL) {
		if (scan == ilock) {
			*scanp = ilock->next;
			spin_unlock(&map->ilock_spin);
			if (ilock->flags & ILOCK_WAITING)
				wakeup(ilock);
			return;
		}
		scanp = &scan->next;
	}
	spin_unlock(&map->ilock_spin);
	panic("vm_map_deinterlock: missing ilock!");
}

#include "opt_ddb.h"
#ifdef DDB
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
	RB_FOREACH(entry, vm_map_rb_tree, &map->rb_root) {
		db_iprintf("map entry %p: start=%p, end=%p\n",
		    (void *)entry,
		    (void *)entry->ba.start, (void *)entry->ba.end);
		nlines++;
		{
			static char *inheritance_name[4] =
			{"share", "copy", "none", "donate_copy"};

			db_iprintf(" prot=%x/%x/%s",
			    entry->protection,
			    entry->max_protection,
			    inheritance_name[(int)(unsigned char)
						entry->inheritance]);
			if (entry->wired_count != 0)
				db_printf(", wired");
		}
		switch(entry->maptype) {
		case VM_MAPTYPE_SUBMAP:
			/* XXX no %qd in kernel.  Truncate entry->ba.offset. */
			db_printf(", share=%p, offset=0x%lx\n",
			    (void *)entry->ba.sub_map,
			    (long)entry->ba.offset);
			nlines++;

			db_indent += 2;
			vm_map_print((db_expr_t)(intptr_t)entry->ba.sub_map,
				     full, 0, NULL);
			db_indent -= 2;
			break;
		case VM_MAPTYPE_NORMAL:
		case VM_MAPTYPE_VPAGETABLE:
			/* XXX no %qd in kernel.  Truncate entry->ba.offset. */
			db_printf(", object=%p, offset=0x%lx",
			    (void *)entry->ba.object,
			    (long)entry->ba.offset);
			if (entry->eflags & MAP_ENTRY_COW)
				db_printf(", copy (%s)",
				    (entry->eflags & MAP_ENTRY_NEEDS_COPY) ? "needed" : "done");
			db_printf("\n");
			nlines++;

			if (entry->ba.object) {
				db_indent += 2;
				vm_object_print((db_expr_t)(intptr_t)
						entry->ba.object,
						full, 0, NULL);
				nlines += 4;
				db_indent -= 2;
			}
			break;
		case VM_MAPTYPE_UKSMAP:
			db_printf(", uksmap=%p, offset=0x%lx",
			    (void *)entry->ba.uksmap,
			    (long)entry->ba.offset);
			if (entry->eflags & MAP_ENTRY_COW)
				db_printf(", copy (%s)",
				    (entry->eflags & MAP_ENTRY_NEEDS_COPY) ? "needed" : "done");
			db_printf("\n");
			nlines++;
			break;
		default:
			break;
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
