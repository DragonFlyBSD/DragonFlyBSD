/*
 * Copyright (c) 2006 The DragonFly Project.  All rights reserved.
 * Copyright (c) 1991 Regents of the University of California.
 * All rights reserved.
 * Copyright (c) 1994 John S. Dyson
 * All rights reserved.
 * Copyright (c) 1994 David Greenman
 * All rights reserved.
 * Copyright (c) 2004-2006 Matthew Dillon
 * All rights reserved.
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
 * from:   @(#)pmap.c      7.7 (Berkeley)  5/12/91
 * $FreeBSD: src/sys/i386/i386/pmap.c,v 1.250.2.18 2002/03/06 22:48:53 silby Exp $
 * $DragonFly: src/sys/platform/pc64/amd64/pmap.c,v 1.1 2007/09/23 04:29:31 yanyh Exp $
 * $DragonFly: src/sys/platform/pc64/amd64/pmap.c,v 1.1 2007/09/23 04:29:31 yanyh Exp $
 */
/*
 * NOTE: PMAP_INVAL_ADD: In pc32 this function is called prior to adjusting
 * the PTE in the page table, because a cpu synchronization might be required.
 * The actual invalidation is delayed until the following call or flush.  In
 * the VKERNEL build this function is called prior to adjusting the PTE and
 * invalidates the table synchronously (not delayed), and is not SMP safe
 * as a consequence.
 */

#include <sys/types.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/proc.h>
#include <sys/thread.h>
#include <sys/user.h>
#include <sys/vmspace.h>

#include <vm/pmap.h>
#include <vm/vm_page.h>
#include <vm/vm_extern.h>
#include <vm/vm_kern.h>
#include <vm/vm_object.h>
#include <vm/vm_zone.h>
#include <vm/vm_pageout.h>

#include <machine/md_var.h>
#include <machine/pcb.h>
#include <machine/pmap_inval.h>
#include <machine/globaldata.h>

struct pmap kernel_pmap;

void
pmap_init(void)
{
}

void
pmap_init2(void)
{
}

/*
 * Bootstrap the kernel_pmap so it can be used with pmap_enter().  
 *
 * NOTE! pm_pdir for the kernel pmap is offset so VA's translate
 * directly into PTD indexes (PTA is also offset for the same reason).
 * This is necessary because, for now, KVA is not mapped at address 0.
 *
 * Page table pages are not managed like they are in normal pmaps, so
 * no pteobj is needed.
 */
void
pmap_bootstrap(vm_paddr_t firstaddr, vm_paddr_t loadaddr)
{
}

/*
 * Initialize pmap0/vmspace0 .  Since process 0 never enters user mode we
 * just dummy it up so it works well enough for fork().
 *
 * In DragonFly, process pmaps may only be used to manipulate user address
 * space, never kernel address space.
 */
void
pmap_pinit0(struct pmap *pmap)
{
}

/************************************************************************
 *	        Procedures to manage whole physical maps		*
 ************************************************************************
 *
 * Initialize a preallocated and zeroed pmap structure,
 * such as one in a vmspace structure.
 */
void
pmap_pinit(struct pmap *pmap)
{
}

/*
 * Clean up a pmap structure so it can be physically freed
 */
void
pmap_puninit(pmap_t pmap)
{
}


/*
 * Wire in kernel global address entries.  To avoid a race condition
 * between pmap initialization and pmap_growkernel, this procedure
 * adds the pmap to the master list (which growkernel scans to update),
 * then copies the template.
 *
 * In a virtual kernel there are no kernel global address entries.
 */
void
pmap_pinit2(struct pmap *pmap)
{
}

/*
 * Release all resources held by the given physical map.
 *
 * Should only be called if the map contains no valid mappings.
 */
static int pmap_release_callback(struct vm_page *p, void *data);

void
pmap_release(struct pmap *pmap)
{
}

static int
pmap_release_callback(struct vm_page *p, void *data)
{
	return(0);
}

/*
 * Retire the given physical map from service.  Should only be called if
 * the map contains no valid mappings.
 */
void
pmap_destroy(pmap_t pmap)
{
}

/*
 * Add a reference to the specified pmap.
 */
void
pmap_reference(pmap_t pmap)
{
}

/************************************************************************
 *	   		VMSPACE MANAGEMENT				*
 ************************************************************************
 *
 * The VMSPACE management we do in our virtual kernel must be reflected
 * in the real kernel.  This is accomplished by making vmspace system
 * calls to the real kernel.
 */
void
cpu_vmspace_alloc(struct vmspace *vm)
{
}

void
cpu_vmspace_free(struct vmspace *vm)
{
}

/************************************************************************
 *	    Procedures which operate directly on the kernel PMAP 	*
 ************************************************************************/

/*
 * This maps the requested page table and gives us access to it.
 */
static vpte_t *
get_ptbase(struct pmap *pmap, vm_offset_t va)
{
	return NULL;
}

static vpte_t *
get_ptbase1(struct pmap *pmap, vm_offset_t va)
{
	return NULL;
}

static vpte_t *
get_ptbase2(struct pmap *pmap, vm_offset_t va)
{
	return NULL;
}

/*
 * When removing a page directory the related VA range in the self-mapping
 * of the page table must be invalidated.
 */
static void
inval_ptbase_pagedir(pmap_t pmap, vm_pindex_t pindex)
{
}

/*
 * Enter a mapping into kernel_pmap.  Mappings created in this fashion
 * are not managed.  Mappings must be immediately accessible on all cpus.
 *
 * Call pmap_inval_pte() to invalidate the virtual pte and clean out the
 * real pmap and handle related races before storing the new vpte.
 */
void
pmap_kenter(vm_offset_t va, vm_paddr_t pa)
{
}

/*
 * Synchronize a kvm mapping originally made for the private use on
 * some other cpu so it can be used on all cpus.
 *
 * XXX add MADV_RESYNC to improve performance.
 */
void
pmap_kenter_sync(vm_offset_t va)
{
}

/*
 * Synchronize a kvm mapping originally made for the private use on
 * some other cpu so it can be used on our cpu.  Turns out to be the
 * same madvise() call, because we have to sync the real pmaps anyway.
 *
 * XXX add MADV_RESYNC to improve performance.
 */
void
pmap_kenter_sync_quick(vm_offset_t va)
{
}

#if 0
/*
 * Make a previously read-only kernel mapping R+W (not implemented by
 * virtual kernels).
 */
void
pmap_kmodify_rw(vm_offset_t va)
{
        *pmap_kpte(va) |= VPTE_R | VPTE_W;
	madvise((void *)va, PAGE_SIZE, MADV_INVAL);
}

/*
 * Make a kernel mapping non-cacheable (not applicable to virtual kernels)
 */
void
pmap_kmodify_nc(vm_offset_t va)
{
        *pmap_kpte(va) |= VPTE_N;
	madvise((void *)va, PAGE_SIZE, MADV_INVAL);
}

#endif

/*
 * Map a contiguous range of physical memory to a KVM
 */
vm_offset_t
pmap_map(vm_offset_t virt, vm_paddr_t start, vm_paddr_t end, int prot)
{
	return (NULL);
}

/*
 * Enter an unmanaged KVA mapping for the private use of the current
 * cpu only.  pmap_kenter_sync() may be called to make the mapping usable
 * by other cpus.
 *
 * It is illegal for the mapping to be accessed by other cpus unleess
 * pmap_kenter_sync*() is called.
 */
void
pmap_kenter_quick(vm_offset_t va, vm_paddr_t pa)
{
}

/*
 * Make a temporary mapping for a physical address.  This is only intended
 * to be used for panic dumps.
 */
void *
pmap_kenter_temporary(vm_paddr_t pa, int i)
{
	return (NULL);
}

/*
 * Remove an unmanaged mapping created with pmap_kenter*().
 */
void
pmap_kremove(vm_offset_t va)
{
}

/*
 * Remove an unmanaged mapping created with pmap_kenter*() but synchronize
 * only with this cpu.
 *
 * Unfortunately because we optimize new entries by testing VPTE_V later
 * on, we actually still have to synchronize with all the cpus.  XXX maybe
 * store a junk value and test against 0 in the other places instead?
 */
void
pmap_kremove_quick(vm_offset_t va)
{
}

/*
 * Map a set of unmanaged VM pages into KVM.
 */
void
pmap_qenter(vm_offset_t va, struct vm_page **m, int count)
{
}

/*
 * Map a set of VM pages to kernel virtual memory.  If a mapping changes
 * clear the supplied mask.  The caller handles any SMP interactions.
 * The mask is used to provide the caller with hints on what SMP interactions
 * might be needed.
 */
void
pmap_qenter2(vm_offset_t va, struct vm_page **m, int count, cpumask_t *mask)
{
}

/*
 * Undo the effects of pmap_qenter*().
 */
void
pmap_qremove(vm_offset_t va, int count)
{
}

/************************************************************************
 *	  Misc support glue called by machine independant code		*
 ************************************************************************
 *
 * These routines are called by machine independant code to operate on
 * certain machine-dependant aspects of processes, threads, and pmaps.
 */

/*
 * Initialize MD portions of the thread structure.
 */
void
pmap_init_thread(thread_t td)
{
}

/*
 * This routine directly affects the fork perf for a process.
 */
void
pmap_init_proc(struct proc *p)
{
}

/*
 * Destroy the UPAGES for a process that has exited and disassociate
 * the process from its thread.
 */
void
pmap_dispose_proc(struct proc *p)
{
}

/*
 * We pre-allocate all page table pages for kernel virtual memory so
 * this routine will only be called if KVM has been exhausted.
 */
void
pmap_growkernel(vm_offset_t addr)
{
}

/*
 * The modification bit is not tracked for any pages in this range. XXX
 * such pages in this maps should always use pmap_k*() functions and not
 * be managed anyhow.
 *
 * XXX User and kernel address spaces are independant for virtual kernels,
 * this function only applies to the kernel pmap.
 */
static int
pmap_track_modified(pmap_t pmap, vm_offset_t va)
{
		return 0;
}

/************************************************************************
 *	    Procedures supporting managed page table pages		*
 ************************************************************************
 *
 * These procedures are used to track managed page table pages.  These pages
 * use the page table page's vm_page_t to track PTEs in the page.  The
 * page table pages themselves are arranged in a VM object, pmap->pm_pteobj.
 *
 * This allows the system to throw away page table pages for user processes
 * at will and reinstantiate them on demand.
 */

/*
 * This routine works like vm_page_lookup() but also blocks as long as the
 * page is busy.  This routine does not busy the page it returns.
 *
 * Unless the caller is managing objects whos pages are in a known state,
 * the call should be made with a critical section held so the page's object
 * association remains valid on return.
 */
static vm_page_t
pmap_page_lookup(vm_object_t object, vm_pindex_t pindex)
{
	return(NULL);
}

/*
 * This routine unholds page table pages, and if the hold count
 * drops to zero, then it decrements the wire count.
 */
static int 
_pmap_unwire_pte_hold(pmap_t pmap, vm_page_t m) 
{
	return 0;
}

static __inline int
pmap_unwire_pte_hold(pmap_t pmap, vm_page_t m)
{
	return 0;
}

/*
 * After removing a page table entry, this routine is used to
 * conditionally free the page, and manage the hold/wire counts.
 */
static int
pmap_unuse_pt(pmap_t pmap, vm_offset_t va, vm_page_t mpte)
{
	return 0;
}

/*
 * Attempt to release and free an vm_page in a pmap.  Returns 1 on success,
 * 0 on failure (if the procedure had to sleep).
 */
static int
pmap_release_free_page(struct pmap *pmap, vm_page_t p)
{
	return 1;
}

/*
 * This routine is called if the page table page is not mapped in the page
 * table directory.
 *
 * The routine is broken up into two parts for readability.
 */
static vm_page_t
_pmap_allocpte(pmap_t pmap, unsigned ptepindex)
{
	return (NULL);
}

/*
 * Determine the page table page required to access the VA in the pmap
 * and allocate it if necessary.  Return a held vm_page_t for the page.
 *
 * Only used with user pmaps.
 */
static vm_page_t
pmap_allocpte(pmap_t pmap, vm_offset_t va)
{
	return NULL;
}

/************************************************************************
 *			Managed pages in pmaps				*
 ************************************************************************
 *
 * All pages entered into user pmaps and some pages entered into the kernel
 * pmap are managed, meaning that pmap_protect() and other related management
 * functions work on these pages.
 */

/*
 * free the pv_entry back to the free list.  This function may be
 * called from an interrupt.
 */
static __inline void
free_pv_entry(pv_entry_t pv)
{
}

/*
 * get a new pv_entry, allocating a block from the system
 * when needed.  This function may be called from an interrupt.
 */
static pv_entry_t
get_pv_entry(void)
{
	return NULL;
}

/*
 * This routine is very drastic, but can save the system
 * in a pinch.
 */
void
pmap_collect(void)
{
}
	
/*
 * If it is the first entry on the list, it is actually
 * in the header and we must copy the following entry up
 * to the header.  Otherwise we must search the list for
 * the entry.  In either case we free the now unused entry.
 */
static int
pmap_remove_entry(struct pmap *pmap, vm_page_t m, vm_offset_t va)
{
	return 0;
}

/*
 * Create a pv entry for page at pa for (pmap, va).  If the page table page
 * holding the VA is managed, mpte will be non-NULL.
 */
static void
pmap_insert_entry(pmap_t pmap, vm_offset_t va, vm_page_t mpte, vm_page_t m)
{
}

/*
 * pmap_remove_pte: do the things to unmap a page in a process
 */
static int
pmap_remove_pte(struct pmap *pmap, vpte_t *ptq, vm_offset_t va)
{
	return 0;
}

/*
 * pmap_remove_page:
 *
 *	Remove a single page from a process address space.
 *
 *	This function may not be called from an interrupt if the pmap is
 *	not kernel_pmap.
 */
static void
pmap_remove_page(struct pmap *pmap, vm_offset_t va)
{
}

/*
 * pmap_remove:
 *
 *	Remove the given range of addresses from the specified map.
 *
 *	It is assumed that the start and end are properly
 *	rounded to the page size.
 *
 *	This function may not be called from an interrupt if the pmap is
 *	not kernel_pmap.
 */
void
pmap_remove(struct pmap *pmap, vm_offset_t sva, vm_offset_t eva)
{
}

/*
 * pmap_remove_all:
 *
 * Removes this physical page from all physical maps in which it resides.
 * Reflects back modify bits to the pager.
 *
 * This routine may not be called from an interrupt.
 */
static void
pmap_remove_all(vm_page_t m)
{
}

/*
 * pmap_protect:
 *
 *	Set the physical protection on the specified range of this map
 *	as requested.
 *
 *	This function may not be called from an interrupt if the map is
 *	not the kernel_pmap.
 */
void
pmap_protect(pmap_t pmap, vm_offset_t sva, vm_offset_t eva, vm_prot_t prot)
{
}

/*
 * Enter a managed page into a pmap.  If the page is not wired related pmap
 * data can be destroyed at any time for later demand-operation.
 *
 * Insert the vm_page (m) at virtual address (v) in (pmap), with the
 * specified protection, and wire the mapping if requested.
 *
 * NOTE: This routine may not lazy-evaluate or lose information.  The
 * page must actually be inserted into the given map NOW.
 *
 * NOTE: When entering a page at a KVA address, the pmap must be the
 * kernel_pmap.
 */
void
pmap_enter(pmap_t pmap, vm_offset_t va, vm_page_t m, vm_prot_t prot,
	   boolean_t wired)
{
}

/*
 * This is a quick version of pmap_enter().  It is used only under the 
 * following conditions:
 *
 * (1) The pmap is not the kernel_pmap
 * (2) The page is not to be wired into the map
 * (3) The page is to mapped read-only in the pmap (initially that is)
 * (4) The calling procedure is responsible for flushing the TLB
 * (5) The page is always managed
 * (6) There is no prior mapping at the VA
 */

static vm_page_t
pmap_enter_quick(pmap_t pmap, vm_offset_t va, vm_page_t m, vm_page_t mpte)
{
	return NULL;
}

/*
 * Extract the physical address for the translation at the specified
 * virtual address in the pmap.
 */
vm_paddr_t
pmap_extract(pmap_t pmap, vm_offset_t va)
{
	return(0);
}

/*
 * This routine preloads the ptes for a given object into the specified pmap.
 * This eliminates the blast of soft faults on process startup and
 * immediately after an mmap.
 */
static int pmap_object_init_pt_callback(vm_page_t p, void *data);

void
pmap_object_init_pt(pmap_t pmap, vm_offset_t addr, vm_prot_t prot,
		    vm_object_t object, vm_pindex_t pindex, 
		    vm_size_t size, int limit)
{
}

static
int
pmap_object_init_pt_callback(vm_page_t p, void *data)
{
	return(0);
}

/*
 * pmap_prefault provides a quick way of clustering pagefaults into a
 * processes address space.  It is a "cousin" of pmap_object_init_pt, 
 * except it runs at page fault time instead of mmap time.
 */
#define PFBAK 4
#define PFFOR 4
#define PAGEORDER_SIZE (PFBAK+PFFOR)

static int pmap_prefault_pageorder[] = {
	-PAGE_SIZE, PAGE_SIZE,
	-2 * PAGE_SIZE, 2 * PAGE_SIZE,
	-3 * PAGE_SIZE, 3 * PAGE_SIZE,
	-4 * PAGE_SIZE, 4 * PAGE_SIZE
};

void
pmap_prefault(pmap_t pmap, vm_offset_t addra, vm_map_entry_t entry)
{
}

/*
 *	Routine:	pmap_change_wiring
 *	Function:	Change the wiring attribute for a map/virtual-address
 *			pair.
 *	In/out conditions:
 *			The mapping must already exist in the pmap.
 */
void
pmap_change_wiring(pmap_t pmap, vm_offset_t va, boolean_t wired)
{
}

/*
 *	Copy the range specified by src_addr/len
 *	from the source map to the range dst_addr/len
 *	in the destination map.
 *
 *	This routine is only advisory and need not do anything.
 */
void
pmap_copy(pmap_t dst_pmap, pmap_t src_pmap, vm_offset_t dst_addr, 
	vm_size_t len, vm_offset_t src_addr)
{
}	

/*
 * pmap_zero_page:
 *
 *	Zero the specified PA by mapping the page into KVM and clearing its
 *	contents.
 *
 *	This function may be called from an interrupt and no locking is
 *	required.
 */
void
pmap_zero_page(vm_paddr_t phys)
{
}

/*
 * pmap_page_assertzero:
 *
 *	Assert that a page is empty, panic if it isn't.
 */
void
pmap_page_assertzero(vm_paddr_t phys)
{
}

/*
 * pmap_zero_page:
 *
 *	Zero part of a physical page by mapping it into memory and clearing
 *	its contents with bzero.
 *
 *	off and size may not cover an area beyond a single hardware page.
 */
void
pmap_zero_page_area(vm_paddr_t phys, int off, int size)
{
}

/*
 * pmap_copy_page:
 *
 *	Copy the physical page from the source PA to the target PA.
 *	This function may be called from an interrupt.  No locking
 *	is required.
 */
void
pmap_copy_page(vm_paddr_t src, vm_paddr_t dst)
{
}

/*
 * pmap_copy_page_frag:
 *
 *	Copy the physical page from the source PA to the target PA.
 *	This function may be called from an interrupt.  No locking
 *	is required.
 */
void
pmap_copy_page_frag(vm_paddr_t src, vm_paddr_t dst, size_t bytes)
{
}

/*
 * Returns true if the pmap's pv is one of the first
 * 16 pvs linked to from this page.  This count may
 * be changed upwards or downwards in the future; it
 * is only necessary that true be returned for a small
 * subset of pmaps for proper page aging.
 */
boolean_t
pmap_page_exists_quick(pmap_t pmap, vm_page_t m)
{
	return (FALSE);
}

/*
 * Remove all pages from specified address space
 * this aids process exit speeds.  Also, this code
 * is special cased for current process only, but
 * can have the more generic (and slightly slower)
 * mode enabled.  This is much faster than pmap_remove
 * in the case of running down an entire address space.
 */
void
pmap_remove_pages(pmap_t pmap, vm_offset_t sva, vm_offset_t eva)
{
}

/*
 * pmap_testbit tests bits in active mappings of a VM page.
 */
static boolean_t
pmap_testbit(vm_page_t m, int bit)
{
	return (FALSE);
}

/*
 * This routine is used to clear bits in ptes.  Certain bits require special
 * handling, in particular (on virtual kernels) the VPTE_M (modify) bit.
 *
 * This routine is only called with certain VPTE_* bit combinations.
 */
static __inline void
pmap_clearbit(vm_page_t m, int bit)
{
}

/*
 *      pmap_page_protect:
 *
 *      Lower the permission for all mappings to a given page.
 */
void
pmap_page_protect(vm_page_t m, vm_prot_t prot)
{
}

vm_paddr_t
pmap_phys_address(int ppn)
{
	return NULL;
}

/*
 *	pmap_ts_referenced:
 *
 *	Return a count of reference bits for a page, clearing those bits.
 *	It is not necessary for every reference bit to be cleared, but it
 *	is necessary that 0 only be returned when there are truly no
 *	reference bits set.
 *
 *	XXX: The exact number of bits to check and clear is a matter that
 *	should be tested and standardized at some point in the future for
 *	optimal aging of shared pages.
 */
int
pmap_ts_referenced(vm_page_t m)
{
	return (0);
}

/*
 *	pmap_is_modified:
 *
 *	Return whether or not the specified physical page was modified
 *	in any physical maps.
 */
boolean_t
pmap_is_modified(vm_page_t m)
{
	return NULL;
}

/*
 *	Clear the modify bits on the specified physical page.
 */
void
pmap_clear_modify(vm_page_t m)
{
}

/*
 *	pmap_clear_reference:
 *
 *	Clear the reference bit on the specified physical page.
 */
void
pmap_clear_reference(vm_page_t m)
{
}

#if 0
/*
 * Miscellaneous support routines follow
 */

static void
i386_protection_init(void)
{
	int *kp, prot;

	kp = protection_codes;
	for (prot = 0; prot < 8; prot++) {
		if (prot & VM_PROT_READ)
			*kp |= VPTE_R;
		if (prot & VM_PROT_WRITE)
			*kp |= VPTE_W;
		if (prot & VM_PROT_EXECUTE)
			*kp |= VPTE_X;
		++kp;
	}
}

/*
 * Map a set of physical memory pages into the kernel virtual
 * address space. Return a pointer to where it is mapped. This
 * routine is intended to be used for mapping device memory,
 * NOT real memory.
 *
 * NOTE: we can't use pgeflag unless we invalidate the pages one at
 * a time.
 */
void *
pmap_mapdev(vm_paddr_t pa, vm_size_t size)
{
	vm_offset_t va, tmpva, offset;
	vpte_t *pte;

	offset = pa & PAGE_MASK;
	size = roundup(offset + size, PAGE_SIZE);

	va = kmem_alloc_nofault(&kernel_map, size);
	if (!va)
		panic("pmap_mapdev: Couldn't alloc kernel virtual memory");

	pa = pa & VPTE_FRAME;
	for (tmpva = va; size > 0;) {
		pte = KernelPTA + (tmpva >> PAGE_SHIFT);
		*pte = pa | VPTE_R | VPTE_W | VPTE_V; /* | pgeflag; */
		size -= PAGE_SIZE;
		tmpva += PAGE_SIZE;
		pa += PAGE_SIZE;
	}
	cpu_invltlb();
	smp_invltlb();

	return ((void *)(va + offset));
}

void
pmap_unmapdev(vm_offset_t va, vm_size_t size)
{
	vm_offset_t base, offset;

	base = va & VPTE_FRAME;
	offset = va & PAGE_MASK;
	size = roundup(offset + size, PAGE_SIZE);
	pmap_qremove(va, size >> PAGE_SHIFT);
	kmem_free(&kernel_map, base, size);
}

#endif

/*
 * perform the pmap work for mincore
 */
int
pmap_mincore(pmap_t pmap, vm_offset_t addr)
{
	return 0;
}

void
pmap_replacevm(struct proc *p, struct vmspace *newvm, int adjrefs)
{
}

void
pmap_setlwpvm(struct lwp *lp, struct vmspace *newvm)
{
}


vm_offset_t
pmap_addr_hint(vm_object_t obj, vm_offset_t addr, vm_size_t size)
{
	return NULL;
}

