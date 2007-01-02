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
 * $DragonFly: src/sys/platform/vkernel/platform/pmap.c,v 1.1 2007/01/02 04:24:26 dillon Exp $
 */

#include <sys/types.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/vkernel.h>
#include <sys/proc.h>
#include <sys/thread.h>
#include <sys/user.h>

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

#include <assert.h>

struct pmap kernel_pmap;

static struct vm_zone pvzone;
static struct vm_object pvzone_obj;
static TAILQ_HEAD(,pmap) pmap_list = TAILQ_HEAD_INITIALIZER(pmap_list);
static int pv_entry_count;
static int pv_entry_max;
static int pv_entry_high_water;
static int pmap_pagedaemon_waken;
static boolean_t pmap_initialized = FALSE;
static int protection_codes[8];

static void i386_protection_init(void);
static void pmap_remove_all(vm_page_t m);
static int pmap_release_free_page(struct pmap *pmap, vm_page_t p);

#define MINPV	2048
#ifndef PMAP_SHPGPERPROC
#define PMAP_SHPGPERPROC 200
#endif

#define pmap_pde(m, v)	(&((m)->pm_pdir[(vm_offset_t)(v) >> PDRSHIFT]))

#define pte_prot(m, p) \
	(protection_codes[p & (VM_PROT_READ|VM_PROT_WRITE|VM_PROT_EXECUTE)])

void
pmap_init(void)
{
	int i;
	struct pv_entry *pvinit;

	for (i = 0; i < vm_page_array_size; i++) {
		vm_page_t m;

		m = &vm_page_array[i];
		TAILQ_INIT(&m->md.pv_list);
		m->md.pv_list_count = 0;
	}

	i = vm_page_array_size;
	if (i < MINPV)
		i = MINPV;
	pvinit = (struct pv_entry *)kmem_alloc(&kernel_map, i*sizeof(*pvinit));
	zbootinit(&pvzone, "PV ENTRY", sizeof(*pvinit), pvinit, i);
	pmap_initialized = TRUE;
}

void
pmap_init2(void)
{
	int shpgperproc = PMAP_SHPGPERPROC;

	TUNABLE_INT_FETCH("vm.pmap.shpgperproc", &shpgperproc);
	pv_entry_max = shpgperproc * maxproc + vm_page_array_size;
	TUNABLE_INT_FETCH("vm.pmap.pv_entries", &pv_entry_max);
	pv_entry_high_water = 9 * (pv_entry_max / 10);
	zinitna(&pvzone, &pvzone_obj, NULL, 0, pv_entry_max, ZONE_INTERRUPT, 1);
}

/*
 * Bootstrap the kernel_pmap so it can be used with pmap_enter().  
 *
 * Page table pages are not managed like they are in normal pmaps, so
 * no pteobj is needed.
 */
void
pmap_bootstrap(void)
{
	vm_pindex_t i = (((vm_offset_t)KernelPTD - KvaStart) >> PAGE_SHIFT);

	kernel_pmap.pm_pdir = KernelPTD;
	kernel_pmap.pm_pdirpte = KernelPTA[i];
	kernel_pmap.pm_count = 1;
	kernel_pmap.pm_active = (cpumask_t)-1;
	TAILQ_INIT(&kernel_pmap.pm_pvlist);
	i386_protection_init();
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
	pmap_pinit(pmap);
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
	vm_page_t ptdpg;
	int npages;

	/*
	 * No need to allocate page table space yet but we do need a valid
	 * page directory table.
	 */
	if (pmap->pm_pdir == NULL) {
		pmap->pm_pdir =
		    (pd_entry_t *)kmem_alloc_pageable(&kernel_map, PAGE_SIZE);
	}

	/*
	 * allocate object for the pte array and page directory
	 */
	npages = VPTE_PAGETABLE_SIZE +
		 (VM_MAX_USER_ADDRESS / PAGE_SIZE) * sizeof(vpte_t);
	npages = (npages + PAGE_MASK) / PAGE_SIZE;

	if (pmap->pm_pteobj == NULL)
		pmap->pm_pteobj = vm_object_allocate(OBJT_DEFAULT, npages);
	pmap->pm_pdindex = npages - 1;

	/*
	 * allocate the page directory page
	 */
	ptdpg = vm_page_grab(pmap->pm_pteobj, pmap->pm_pdindex,
			     VM_ALLOC_NORMAL | VM_ALLOC_RETRY);

	ptdpg->wire_count = 1;
	++vmstats.v_wire_count;

	/* not usually mapped */
	vm_page_flag_clear(ptdpg, PG_MAPPED | PG_BUSY);
	ptdpg->valid = VM_PAGE_BITS_ALL;

	pmap_kenter((vm_offset_t)pmap->pm_pdir, VM_PAGE_TO_PHYS(ptdpg));
	if ((ptdpg->flags & PG_ZERO) == 0)
		bzero(pmap->pm_pdir, PAGE_SIZE);

	pmap->pm_count = 1;
	pmap->pm_active = 0;
	pmap->pm_ptphint = NULL;
	TAILQ_INIT(&pmap->pm_pvlist);
	bzero(&pmap->pm_stats, sizeof pmap->pm_stats);
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
	crit_enter();
	TAILQ_INSERT_TAIL(&pmap_list, pmap, pm_pmnode);
	crit_exit();
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
	vm_object_t object = pmap->pm_pteobj;
	struct rb_vm_page_scan_info info;

	KKASSERT(pmap != &kernel_pmap);

#if defined(DIAGNOSTIC)
	if (object->ref_count != 1)
		panic("pmap_release: pteobj reference count != 1");
#endif
	
	info.pmap = pmap;
	info.object = object;
	crit_enter();
	TAILQ_REMOVE(&pmap_list, pmap, pm_pmnode);
	crit_exit();

	do {
		crit_enter();
		info.error = 0;
		info.mpte = NULL;
		info.limit = object->generation;

		vm_page_rb_tree_RB_SCAN(&object->rb_memq, NULL, 
				        pmap_release_callback, &info);
		if (info.error == 0 && info.mpte) {
			if (!pmap_release_free_page(pmap, info.mpte))
				info.error = 1;
		}
		crit_exit();
	} while (info.error);
}

static int
pmap_release_callback(struct vm_page *p, void *data)
{
	struct rb_vm_page_scan_info *info = data;

	if (p->pindex == info->pmap->pm_pdindex) {
		info->mpte = p;
		return(0);
	}
	if (!pmap_release_free_page(info->pmap, p)) {
		info->error = 1;
		return(-1);
	}
	if (info->object->generation != info->limit) {
		info->error = 1;
		return(-1);
	}
	return(0);
}

/*
 * Retire the given physical map from service.  Should only be called if
 * the map contains no valid mappings.
 */
void
pmap_destroy(pmap_t pmap)
{
	int count;

	if (pmap == NULL)
		return;

	count = --pmap->pm_count;
	if (count == 0) {
		pmap_release(pmap);
		panic("destroying a pmap is not yet implemented");
	}
}

/*
 * Add a reference to the specified pmap.
 */
void
pmap_reference(pmap_t pmap)
{
	if (pmap != NULL) {
		pmap->pm_count++;
	}
}

/************************************************************************
 *	    Procedures which operate directly on the kernel PMAP 	*
 ************************************************************************/

/*
 * This maps the requested page table and gives us access to it.
 */
static vpte_t *
get_ptbase(struct pmap *pmap)
{
	struct mdglobaldata *gd = mdcpu;

	if (pmap == &kernel_pmap) {
		return(KernelPTA);
	} else if (pmap->pm_pdir == gd->gd_PT1pdir) {
		return(gd->gd_PT1map);
	} else if (pmap->pm_pdir == gd->gd_PT2pdir) {
		return(gd->gd_PT2map);
	}

	/*
	 * Otherwise choose one or the other and map the page table
	 * in the KVA space reserved for it.
	 */
	KKASSERT(gd->mi.gd_intr_nesting_level == 0 &&
		 (gd->mi.gd_curthread->td_flags & TDF_INTTHREAD) == 0);

	if ((gd->gd_PTflip = 1 - gd->gd_PTflip) == 0) {
		gd->gd_PT1pdir = pmap->pm_pdir;
		*gd->gd_PT1pde = pmap->pm_pdirpte;
		madvise(gd->gd_PT1map, SEG_SIZE, MADV_INVAL);
		return(gd->gd_PT1map);
	} else {
		gd->gd_PT2pdir = pmap->pm_pdir;
		*gd->gd_PT2pde = pmap->pm_pdirpte;
		madvise(gd->gd_PT2map, SEG_SIZE, MADV_INVAL);
		return(gd->gd_PT2map);
	}
}

static vpte_t *
get_ptbase1(struct pmap *pmap)
{
	struct mdglobaldata *gd = mdcpu;

	if (pmap == &kernel_pmap) {
		return(KernelPTA);
	} else if (pmap->pm_pdir == gd->gd_PT1pdir) {
		return(gd->gd_PT1map);
	}
	KKASSERT(gd->mi.gd_intr_nesting_level == 0 &&
		 (gd->mi.gd_curthread->td_flags & TDF_INTTHREAD) == 0);
	gd->gd_PT1pdir = pmap->pm_pdir;
	*gd->gd_PT1pde = pmap->pm_pdirpte;
	madvise(gd->gd_PT1map, SEG_SIZE, MADV_INVAL);
	return(gd->gd_PT1map);
}

static vpte_t *
get_ptbase2(struct pmap *pmap)
{
	struct mdglobaldata *gd = mdcpu;

	if (pmap == &kernel_pmap) {
		return(KernelPTA);
	} else if (pmap->pm_pdir == gd->gd_PT2pdir) {
		return(gd->gd_PT2map);
	}
	KKASSERT(gd->mi.gd_intr_nesting_level == 0 &&
		 (gd->mi.gd_curthread->td_flags & TDF_INTTHREAD) == 0);
	gd->gd_PT2pdir = pmap->pm_pdir;
	*gd->gd_PT2pde = pmap->pm_pdirpte;
	madvise(gd->gd_PT2map, SEG_SIZE, MADV_INVAL);
	return(gd->gd_PT2map);
}

/*
 * Return a pointer to the page table entry for the specified va in the
 * specified pmap.  NULL is returned if there is no valid page table page
 * for the VA.
 */
static __inline vpte_t *
pmap_pte(struct pmap *pmap, vm_offset_t va)
{
	vpte_t *ptep;

	ptep = pmap->pm_pdir[va >> PAGE_SHIFT];
	if (*ptep & VPTE_PS)
		return(ptep);
	if (*ptep)
		return (get_ptbase(pmap) + (va >> PAGE_SHIFT));
	return(NULL);
}


/*
 * Enter a mapping into kernel_pmap.  Mappings created in this fashion
 * are not managed.
 */
void
pmap_kenter(vm_offset_t va, vm_paddr_t pa)
{
	vpte_t *ptep;
	vpte_t npte;
#ifdef SMP
	pmap_inval_info info;
#endif

	KKASSERT(va >= KvaStart && va < KvaEnd);
	npte = (vpte_t)pa | VPTE_R | VPTE_W | VPTE_V;
	ptep = KernelPTA + ((va - KvaStart) >> PAGE_SHIFT);
	if (*ptep & VPTE_V) {
#ifdef SMP
		pmap_inval_init(&info);
		pmap_inval_add(&info, &kernel_pmap, va);
#endif
		*ptep = npte;
#ifdef SMP
		pmap_inval_flush(&info);
#else
		madvise((void *)va, PAGE_SIZE, MADV_INVAL);
#endif
	} else {
		*ptep = npte;
	}
}

/*
 * Enter a mapping into kernel_pmap without any SMP interactions.
 * 
 * Mappings created in this fashion are not managed.
 */
void
pmap_kenter_quick(vm_offset_t va, vm_paddr_t pa)
{
	vpte_t *ptep;
	vpte_t npte;

	KKASSERT(va >= KvaStart && va < KvaEnd);

	npte = (vpte_t)pa | VPTE_R | VPTE_W | VPTE_V;
	ptep = KernelPTA + ((va - KvaStart) >> PAGE_SHIFT);
	if (*ptep & VPTE_V) {
		*ptep = npte;
		madvise((void *)va, PAGE_SIZE, MADV_INVAL);
	} else {
		*ptep = npte;
	}
}

/*
 * Make a temporary mapping for a physical address.  This is only intended
 * to be used for panic dumps.
 */
void *
pmap_kenter_temporary(vm_paddr_t pa, int i)
{
	pmap_kenter(crashdumpmap + (i * PAGE_SIZE), pa);
	return ((void *)crashdumpmap);
}

/*
 * Remove an unmanaged mapping created with pmap_kenter*().
 */
void
pmap_kremove(vm_offset_t va)
{
	vpte_t *ptep;
#ifdef SMP
	pmap_inval_info info;
#endif

	KKASSERT(va >= KvaStart && va < KvaEnd);

	ptep = KernelPTA + ((va - KvaStart) >> PAGE_SHIFT);
	if (*ptep & VPTE_V) {
#ifdef SMP
		pmap_inval_init(&info);
		pmap_inval_add(&info, &kernel_pmap, va);
#endif
		*ptep = 0;
#ifdef SMP
		pmap_inval_flush(&info);
#else
		madvise((void *)va, PAGE_SIZE, MADV_INVAL);
#endif
	} else {
		*ptep = 0;
	}

}

/*
 * Remove an unmanaged mapping created with pmap_kenter*() without
 * going through any SMP interactions.
 */
void
pmap_kremove_quick(vm_offset_t va)
{
	vpte_t *ptep;

	KKASSERT(va >= KvaStart && va < KvaEnd);

	ptep = KernelPTA + ((va - KvaStart) >> PAGE_SHIFT);
	if (*ptep & VPTE_V) {
		*ptep = 0;
		madvise((void *)va, PAGE_SIZE, MADV_INVAL);
	} else {
		*ptep = 0;
	}
}

/*
 * Extract the physical address from the kernel_pmap that is associated
 * with the specified virtual address.
 */
vm_paddr_t
pmap_kextract(vm_offset_t va)
{
	vpte_t *ptep;
        vm_paddr_t pa;

	KKASSERT(va >= KvaStart && va < KvaEnd);

	ptep = KernelPTA + ((va - KvaStart) >> PAGE_SHIFT);
	pa = (vm_paddr_t)(*ptep & VPTE_FRAME) | (va & PAGE_MASK);
	return(pa);
}

/*
 * Map a set of unmanaged VM pages into KVM.
 */
void
pmap_qenter(vm_offset_t va, struct vm_page **m, int count)
{
	KKASSERT(va >= KvaStart && va + count * PAGE_SIZE < KvaEnd);
	while (count) {
		vpte_t *ptep;

		ptep = KernelPTA + ((va - KvaStart) >> PAGE_SHIFT);
		if (*ptep & VPTE_V)
			madvise((void *)va, PAGE_SIZE, MADV_INVAL);
		*ptep = (vpte_t)(*m)->phys_addr | VPTE_R | VPTE_W | VPTE_V;
		--count;
		++m;
		va += PAGE_SIZE;
	}
#ifdef SMP
	XXX
	smp_invltlb();
#endif
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
	cpumask_t cmask = mycpu->gd_cpumask;

	KKASSERT(va >= KvaStart && va + count * PAGE_SIZE < KvaEnd);
	while (count) {
		vpte_t *ptep;
		vpte_t npte;

		ptep = KernelPTA + ((va - KvaStart) >> PAGE_SHIFT);
		npte = (vpte_t)(*m)->phys_addr | VPTE_R | VPTE_W | VPTE_V;
		if (*ptep != npte) {
			*mask = 0;
			*ptep = npte;
			madvise((void *)va, PAGE_SIZE, MADV_INVAL);
		} else if ((*mask & cmask) == 0) {
			madvise((void *)va, PAGE_SIZE, MADV_INVAL);
		}
		--count;
		++m;
		va += PAGE_SIZE;
	}
	*mask |= cmask;
}

/*
 * Undo the effects of pmap_qenter*().
 */
void
pmap_qremove(vm_offset_t va, int count)
{
	KKASSERT(va >= KvaStart && va + count * PAGE_SIZE < KvaEnd);
	while (count) {
		vpte_t *ptep;

		ptep = KernelPTA + ((va - KvaStart) >> PAGE_SHIFT);
		if (*ptep & VPTE_V)
			madvise((void *)va, PAGE_SIZE, MADV_INVAL);
		*ptep = 0;
		--count;
		va += PAGE_SIZE;
	}
#ifdef SMP
	XXX
	smp_invltlb();
#endif
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
	/* enforce pcb placement */
	td->td_pcb = (struct pcb *)(td->td_kstack + td->td_kstack_size) - 1;
	td->td_savefpu = &td->td_pcb->pcb_save;
	td->td_sp = (char *)td->td_pcb - 16;
}

/*
 * Initialize MD portions of a process structure. XXX this aint MD
 */
void
pmap_init_proc(struct proc *p, struct thread *td)
{
	p->p_addr = (void *)td->td_kstack;
	p->p_thread = td;
	td->td_proc = p;
	td->td_lwp = &p->p_lwp;
	td->td_switch = cpu_heavy_switch;
#ifdef SMP
	KKASSERT(td->td_mpcount == 1);
#endif
	bzero(p->p_addr, sizeof(*p->p_addr));
}

/*
 * Destroy the UPAGES for a process that has exited and disassociate
 * the process from its thread.
 */
struct thread *
pmap_dispose_proc(struct proc *p)
{
	struct thread *td;

	KASSERT(p->p_lock == 0, ("attempt to dispose referenced proc! %p", p));

	if ((td = p->p_thread) != NULL) {
		p->p_thread = NULL;
		td->td_proc = NULL;
	}
	p->p_addr = NULL;
	return(td);
}

/*
 * We pre-allocate all page table pages for kernel virtual memory so
 * this routine will only be called if KVM has been exhausted.
 */
void
pmap_growkernel(vm_offset_t size)
{
	panic("KVM exhausted");
}

/*
 * The modification bit is not tracked for any pages in this range. XXX
 * such pages in this maps should always use pmap_k*() functions and not
 * be managed anyhow.
 */
static int
pmap_track_modified(vm_offset_t va)
{
	if ((va < clean_sva) || (va >= clean_eva))
		return 1;
	else
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
	vm_page_t m;
			 
retry:
	m = vm_page_lookup(object, pindex);
	if (m && vm_page_sleep_busy(m, FALSE, "pplookp"))
		goto retry;
	return(m);
}

/*
 * This routine unholds page table pages, and if the hold count
 * drops to zero, then it decrements the wire count.
 */
static int 
_pmap_unwire_pte_hold(pmap_t pmap, vm_page_t m, pmap_inval_info_t info) 
{
	pmap_inval_flush(info);
	while (vm_page_sleep_busy(m, FALSE, "pmuwpt"))
		;

	if (m->hold_count == 0) {
		/*
		 * unmap the page table page
		 */
		pmap_inval_add(info, pmap, -1);
		pmap->pm_pdir[m->pindex] = 0;
		--pmap->pm_stats.resident_count;

		if (pmap->pm_ptphint == m)
			pmap->pm_ptphint = NULL;

		/*
		 * If the page is finally unwired, simply free it.
		 */
		--m->wire_count;
		if (m->wire_count == 0) {
			vm_page_flash(m);
			vm_page_busy(m);
			vm_page_free_zero(m);
			--vmstats.v_wire_count;
		}
		return 1;
	}
	return 0;
}

static __inline int
pmap_unwire_pte_hold(pmap_t pmap, vm_page_t m, pmap_inval_info_t info)
{
	vm_page_unhold(m);
	if (m->hold_count == 0)
		return _pmap_unwire_pte_hold(pmap, m, info);
	else
		return 0;
}

/*
 * After removing a page table entry, this routine is used to
 * conditionally free the page, and manage the hold/wire counts.
 */
static int
pmap_unuse_pt(pmap_t pmap, vm_offset_t va, vm_page_t mpte,
		pmap_inval_info_t info)
{
	unsigned ptepindex;

	if (mpte == NULL) {
		/*
		 * page table pages in the kernel_pmap are not managed.
		 */
		if (pmap == &kernel_pmap)
			return(0);
		ptepindex = (va >> PDRSHIFT);
		if (pmap->pm_ptphint &&
			(pmap->pm_ptphint->pindex == ptepindex)) {
			mpte = pmap->pm_ptphint;
		} else {
			pmap_inval_flush(info);
			mpte = pmap_page_lookup( pmap->pm_pteobj, ptepindex);
			pmap->pm_ptphint = mpte;
		}
	}
	return pmap_unwire_pte_hold(pmap, mpte, info);
}

/*
 * Attempt to release and free an vm_page in a pmap.  Returns 1 on success,
 * 0 on failure (if the procedure had to sleep).
 */
static int
pmap_release_free_page(struct pmap *pmap, vm_page_t p)
{
	vpte_t *pde = pmap->pm_pdir;
	/*
	 * This code optimizes the case of freeing non-busy
	 * page-table pages.  Those pages are zero now, and
	 * might as well be placed directly into the zero queue.
	 */
	if (vm_page_sleep_busy(p, FALSE, "pmaprl"))
		return 0;

	vm_page_busy(p);

	/*
	 * Remove the page table page from the processes address space.
	 */
	pde[p->pindex] = 0;
	pmap->pm_stats.resident_count--;

	if (p->hold_count)  {
		panic("pmap_release: freeing held page table page");
	}
	/*
	 * Page directory pages need to have the kernel stuff cleared, so
	 * they can go into the zero queue also.
	 *
	 * In virtual kernels there is no 'kernel stuff'.  For the moment
	 * I just make sure the whole thing has been zero'd even though
	 * it should already be completely zero'd.
	 */
	if (p->pindex == pmap->pm_pdindex) {
		bzero(pde, VPTE_PAGETABLE_SIZE);
		pmap_kremove((vm_offset_t)pmap->pm_pdir);
	}

	/*
	 * Clear the matching hint
	 */
	if (pmap->pm_ptphint && (pmap->pm_ptphint->pindex == p->pindex))
		pmap->pm_ptphint = NULL;

	/*
	 * And throw the page away.  The page is completely zero'd out so
	 * optimize the free call.
	 */
	p->wire_count--;
	vmstats.v_wire_count--;
	vm_page_free_zero(p);
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
	vm_paddr_t ptepa;
	vm_page_t m;

	/*
	 * Find or fabricate a new pagetable page
	 */
	m = vm_page_grab(pmap->pm_pteobj, ptepindex,
			 VM_ALLOC_NORMAL | VM_ALLOC_ZERO | VM_ALLOC_RETRY);

	KASSERT(m->queue == PQ_NONE,
		("_pmap_allocpte: %p->queue != PQ_NONE", m));

	if (m->wire_count == 0)
		vmstats.v_wire_count++;
	m->wire_count++;

	/*
	 * Increment the hold count for the page table page
	 * (denoting a new mapping.)
	 */
	m->hold_count++;

	/*
	 * Map the pagetable page into the process address space, if
	 * it isn't already there.
	 */
	pmap->pm_stats.resident_count++;

	ptepa = VM_PAGE_TO_PHYS(m);
	pmap->pm_pdir[ptepindex] = (vpte_t)ptepa | VPTE_R | VPTE_W | VPTE_V |
				   VPTE_A | VPTE_M;

	/*
	 * We are likely about to access this page table page, so set the
	 * page table hint to reduce overhead.
	 */
	pmap->pm_ptphint = m;

	/*
	 * Try to use the new mapping, but if we cannot, then
	 * do it with the routine that maps the page explicitly.
	 */
	if ((m->flags & PG_ZERO) == 0)
		pmap_zero_page(ptepa);

	m->valid = VM_PAGE_BITS_ALL;
	vm_page_flag_clear(m, PG_ZERO);
	vm_page_flag_set(m, PG_MAPPED);
	vm_page_wakeup(m);

	return (m);
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
	unsigned ptepindex;
	vm_offset_t ptepa;
	vm_page_t m;

	/*
	 * Calculate pagetable page index
	 */
	ptepindex = va >> PDRSHIFT;

	/*
	 * Get the page directory entry
	 */
	ptepa = (vm_offset_t) pmap->pm_pdir[ptepindex];

	/*
	 * This supports switching from a 4MB page to a
	 * normal 4K page.
	 */
	if (ptepa & VPTE_PS) {
		pmap->pm_pdir[ptepindex] = 0;
		ptepa = 0;
		cpu_invltlb();
		smp_invltlb();
	}

	/*
	 * If the page table page is mapped, we just increment the
	 * hold count, and activate it.
	 */
	if (ptepa) {
		/*
		 * In order to get the page table page, try the
		 * hint first.
		 */
		if (pmap->pm_ptphint &&
			(pmap->pm_ptphint->pindex == ptepindex)) {
			m = pmap->pm_ptphint;
		} else {
			m = pmap_page_lookup( pmap->pm_pteobj, ptepindex);
			pmap->pm_ptphint = m;
		}
		m->hold_count++;
		return m;
	}
	/*
	 * Here if the pte page isn't mapped, or if it has been deallocated.
	 */
	return _pmap_allocpte(pmap, ptepindex);
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
	pv_entry_count--;
	zfree(&pvzone, pv);
}

/*
 * get a new pv_entry, allocating a block from the system
 * when needed.  This function may be called from an interrupt.
 */
static pv_entry_t
get_pv_entry(void)
{
	pv_entry_count++;
	if (pv_entry_high_water &&
		(pv_entry_count > pv_entry_high_water) &&
		(pmap_pagedaemon_waken == 0)) {
		pmap_pagedaemon_waken = 1;
		wakeup (&vm_pages_needed);
	}
	return zalloc(&pvzone);
}

/*
 * This routine is very drastic, but can save the system
 * in a pinch.
 */
void
pmap_collect(void)
{
	int i;
	vm_page_t m;
	static int warningdone=0;

	if (pmap_pagedaemon_waken == 0)
		return;

	if (warningdone < 5) {
		kprintf("pmap_collect: collecting pv entries -- suggest increasing PMAP_SHPGPERPROC\n");
		warningdone++;
	}

	for(i = 0; i < vm_page_array_size; i++) {
		m = &vm_page_array[i];
		if (m->wire_count || m->hold_count || m->busy ||
		    (m->flags & PG_BUSY))
			continue;
		pmap_remove_all(m);
	}
	pmap_pagedaemon_waken = 0;
}
	
/*
 * If it is the first entry on the list, it is actually
 * in the header and we must copy the following entry up
 * to the header.  Otherwise we must search the list for
 * the entry.  In either case we free the now unused entry.
 */
static int
pmap_remove_entry(struct pmap *pmap, vm_page_t m, 
		  vm_offset_t va, pmap_inval_info_t info)
{
	pv_entry_t pv;
	int rtval;

	crit_enter();
	if (m->md.pv_list_count < pmap->pm_stats.resident_count) {
		TAILQ_FOREACH(pv, &m->md.pv_list, pv_list) {
			if (pmap == pv->pv_pmap && va == pv->pv_va) 
				break;
		}
	} else {
		TAILQ_FOREACH(pv, &pmap->pm_pvlist, pv_plist) {
			if (va == pv->pv_va) 
				break;
		}
	}

	/*
	 * Note that pv_ptem is NULL if the page table page itself is not
	 * managed, even if the page being removed IS managed.
	 */
	rtval = 0;
	if (pv) {
		rtval = pmap_unuse_pt(pmap, va, pv->pv_ptem, info);
		TAILQ_REMOVE(&m->md.pv_list, pv, pv_list);
		m->md.pv_list_count--;
		if (TAILQ_FIRST(&m->md.pv_list) == NULL)
			vm_page_flag_clear(m, PG_MAPPED | PG_WRITEABLE);
		TAILQ_REMOVE(&pmap->pm_pvlist, pv, pv_plist);
		free_pv_entry(pv);
	}
	crit_exit();
	return rtval;
}

/*
 * Create a pv entry for page at pa for (pmap, va).  If the page table page
 * holding the VA is managed, mpte will be non-NULL.
 */
static void
pmap_insert_entry(pmap_t pmap, vm_offset_t va, vm_page_t mpte, vm_page_t m)
{
	pv_entry_t pv;

	crit_enter();
	pv = get_pv_entry();
	pv->pv_va = va;
	pv->pv_pmap = pmap;
	pv->pv_ptem = mpte;

	TAILQ_INSERT_TAIL(&pmap->pm_pvlist, pv, pv_plist);
	TAILQ_INSERT_TAIL(&m->md.pv_list, pv, pv_list);
	m->md.pv_list_count++;

	crit_exit();
}

/*
 * pmap_remove_pte: do the things to unmap a page in a process
 */
static int
pmap_remove_pte(struct pmap *pmap, vpte_t *ptq, vm_offset_t va,
	pmap_inval_info_t info)
{
	vpte_t oldpte;
	vm_page_t m;

	pmap_inval_add(info, pmap, va);
	oldpte = loadandclear(ptq);
	if (oldpte & VPTE_W)
		pmap->pm_stats.wired_count -= 1;
	/*
	 * Machines that don't support invlpg, also don't support
	 * VPTE_G.  XXX VPTE_G is disabled for SMP so don't worry about
	 * the SMP case.
	 */
	if (oldpte & VPTE_G)
		cpu_invlpg((void *)va);
	pmap->pm_stats.resident_count -= 1;
	if (oldpte & PG_MANAGED) {
		m = PHYS_TO_VM_PAGE(oldpte);
		if (oldpte & VPTE_M) {
#if defined(PMAP_DIAGNOSTIC)
			if (pmap_nw_modified((pt_entry_t) oldpte)) {
				kprintf(
	"pmap_remove: modified page not writable: va: 0x%x, pte: 0x%x\n",
				    va, oldpte);
			}
#endif
			if (pmap_track_modified(va))
				vm_page_dirty(m);
		}
		if (oldpte & VPTE_A)
			vm_page_flag_set(m, PG_REFERENCED);
		return pmap_remove_entry(pmap, m, va, info);
	} else {
		return pmap_unuse_pt(pmap, va, NULL, info);
	}

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
pmap_remove_page(struct pmap *pmap, vm_offset_t va, pmap_inval_info_t info)
{
	vpte_t *ptq;

	/*
	 * if there is no pte for this address, just skip it!!!  Otherwise
	 * get a local va for mappings for this pmap and remove the entry.
	 */
	if (*pmap_pde(pmap, va) != 0) {
		ptq = get_ptbase(pmap) + (va >> PAGE_SHIFT);
		if (*ptq) {
			pmap_remove_pte(pmap, ptq, va, info);
		}
	}
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
	vpte_t *ptbase;
	vm_offset_t pdnxt;
	vm_offset_t ptpaddr;
	vm_offset_t sindex, eindex;
	struct pmap_inval_info info;

	if (pmap == NULL)
		return;

	if (pmap->pm_stats.resident_count == 0)
		return;

	pmap_inval_init(&info);

	/*
	 * special handling of removing one page.  a very
	 * common operation and easy to short circuit some
	 * code.
	 */
	if (((sva + PAGE_SIZE) == eva) && 
		((pmap->pm_pdir[(sva >> PDRSHIFT)] & VPTE_PS) == 0)) {
		pmap_remove_page(pmap, sva, &info);
		pmap_inval_flush(&info);
		return;
	}

	/*
	 * Get a local virtual address for the mappings that are being
	 * worked with.
	 */
	ptbase = get_ptbase(pmap);

	sindex = (sva >> PAGE_SHIFT);
	eindex = (eva >> PAGE_SHIFT);

	for (; sindex < eindex; sindex = pdnxt) {
		vpte_t pdirindex;

		/*
		 * Calculate index for next page table.
		 */
		pdnxt = ((sindex + NPTEPG) & ~(NPTEPG - 1));
		if (pmap->pm_stats.resident_count == 0)
			break;

		pdirindex = sindex / NPDEPG;
		if (((ptpaddr = pmap->pm_pdir[pdirindex]) & VPTE_PS) != 0) {
			pmap_inval_add(&info, pmap, -1);
			pmap->pm_pdir[pdirindex] = 0;
			pmap->pm_stats.resident_count -= NBPDR / PAGE_SIZE;
			continue;
		}

		/*
		 * Weed out invalid mappings. Note: we assume that the page
		 * directory table is always allocated, and in kernel virtual.
		 */
		if (ptpaddr == 0)
			continue;

		/*
		 * Limit our scan to either the end of the va represented
		 * by the current page table page, or to the end of the
		 * range being removed.
		 */
		if (pdnxt > eindex) {
			pdnxt = eindex;
		}

		for (; sindex != pdnxt; sindex++) {
			vm_offset_t va;
			if (ptbase[sindex] == 0)
				continue;
			va = i386_ptob(sindex);
			if (pmap_remove_pte(pmap, ptbase + sindex, va, &info))
				break;
		}
	}
	pmap_inval_flush(&info);
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
	struct pmap_inval_info info;
	vpte_t *pte, tpte;
	pv_entry_t pv;

#if defined(PMAP_DIAGNOSTIC)
	/*
	 * XXX this makes pmap_page_protect(NONE) illegal for non-managed
	 * pages!
	 */
	if (!pmap_initialized || (m->flags & PG_FICTITIOUS)) {
		panic("pmap_page_protect: illegal for unmanaged page, va: 0x%08llx", (long long)VM_PAGE_TO_PHYS(m));
	}
#endif

	pmap_inval_init(&info);
	crit_enter();
	while ((pv = TAILQ_FIRST(&m->md.pv_list)) != NULL) {
		pv->pv_pmap->pm_stats.resident_count--;

		pte = pmap_pte(pv->pv_pmap, pv->pv_va);
		pmap_inval_add(&info, pv->pv_pmap, pv->pv_va);

		tpte = loadandclear(pte);
		if (tpte & VPTE_W)
			pv->pv_pmap->pm_stats.wired_count--;

		if (tpte & VPTE_A)
			vm_page_flag_set(m, PG_REFERENCED);

		/*
		 * Update the vm_page_t clean and reference bits.
		 */
		if (tpte & VPTE_M) {
#if defined(PMAP_DIAGNOSTIC)
			if (pmap_nw_modified((pt_entry_t) tpte)) {
				kprintf(
	"pmap_remove_all: modified page not writable: va: 0x%x, pte: 0x%x\n",
				    pv->pv_va, tpte);
			}
#endif
			if (pmap_track_modified(pv->pv_va))
				vm_page_dirty(m);
		}
		TAILQ_REMOVE(&pv->pv_pmap->pm_pvlist, pv, pv_plist);
		TAILQ_REMOVE(&m->md.pv_list, pv, pv_list);
		m->md.pv_list_count--;
		pmap_unuse_pt(pv->pv_pmap, pv->pv_va, pv->pv_ptem, &info);
		free_pv_entry(pv);
	}

	vm_page_flag_clear(m, PG_MAPPED | PG_WRITEABLE);
	crit_exit();
	pmap_inval_flush(&info);
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
	vpte_t *ptbase;
	vm_offset_t pdnxt, ptpaddr;
	vm_pindex_t sindex, eindex;
	pmap_inval_info info;

	if (pmap == NULL)
		return;

	if ((prot & VM_PROT_READ) == VM_PROT_NONE) {
		pmap_remove(pmap, sva, eva);
		return;
	}

	if (prot & VM_PROT_WRITE)
		return;

	pmap_inval_init(&info);

	ptbase = get_ptbase(pmap);

	sindex = (sva >> PAGE_SHIFT);
	eindex = (eva >> PAGE_SHIFT);

	for (; sindex < eindex; sindex = pdnxt) {

		unsigned pdirindex;

		pdnxt = ((sindex + NPTEPG) & ~(NPTEPG - 1));

		pdirindex = sindex / NPDEPG;
		if (((ptpaddr = pmap->pm_pdir[pdirindex]) & VPTE_PS) != 0) {
			pmap_inval_add(&info, pmap, -1);
			pmap->pm_pdir[pdirindex] &= ~(VPTE_M|VPTE_W);
			pmap->pm_stats.resident_count -= NBPDR / PAGE_SIZE;
			continue;
		}

		/*
		 * Weed out invalid mappings. Note: we assume that the page
		 * directory table is always allocated, and in kernel virtual.
		 */
		if (ptpaddr == 0)
			continue;

		if (pdnxt > eindex) {
			pdnxt = eindex;
		}

		for (; sindex != pdnxt; sindex++) {

			unsigned pbits;
			vm_page_t m;

			/* XXX this isn't optimal */
			pmap_inval_add(&info, pmap, i386_ptob(sindex));
			pbits = ptbase[sindex];

			if (pbits & PG_MANAGED) {
				m = NULL;
				if (pbits & VPTE_A) {
					m = PHYS_TO_VM_PAGE(pbits);
					vm_page_flag_set(m, PG_REFERENCED);
					pbits &= ~VPTE_A;
				}
				if (pbits & VPTE_M) {
					if (pmap_track_modified(i386_ptob(sindex))) {
						if (m == NULL)
							m = PHYS_TO_VM_PAGE(pbits);
						vm_page_dirty(m);
						pbits &= ~VPTE_M;
					}
				}
			}

			pbits &= ~VPTE_W;

			if (pbits != ptbase[sindex]) {
				ptbase[sindex] = pbits;
			}
		}
	}
	pmap_inval_flush(&info);
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
	vm_paddr_t pa;
	vpte_t *pte;
	vm_paddr_t opa;
	vm_offset_t origpte, newpte;
	vm_page_t mpte;
	pmap_inval_info info;

	if (pmap == NULL)
		return;

	va &= VPTE_FRAME;

	/*
	 * Get the page table page.   The kernel_pmap's page table pages
	 * are preallocated and have no associated vm_page_t.
	 */
	if (pmap == &kernel_pmap)
		mpte = NULL;
	else
		mpte = pmap_allocpte(pmap, va);

	pmap_inval_init(&info);
	pte = pmap_pte(pmap, va);

	/*
	 * Page Directory table entry not valid, we need a new PT page
	 * and pmap_allocpte() didn't give us one.  Oops!
	 */
	if (pte == NULL) {
		panic("pmap_enter: invalid page directory pmap=%p, va=0x%p\n",
		      pmap, (void *)va);
	}

	pa = VM_PAGE_TO_PHYS(m) & VPTE_FRAME;
	pmap_inval_add(&info, pmap, va); /* XXX non-optimal */
	origpte = *pte;
	opa = origpte & VPTE_FRAME;

	if (origpte & VPTE_PS)
		panic("pmap_enter: attempted pmap_enter on 4MB page");

	/*
	 * Mapping has not changed, must be protection or wiring change.
	 */
	if (origpte && (opa == pa)) {
		/*
		 * Wiring change, just update stats. We don't worry about
		 * wiring PT pages as they remain resident as long as there
		 * are valid mappings in them. Hence, if a user page is wired,
		 * the PT page will be also.
		 */
		if (wired && ((origpte & VPTE_W) == 0))
			pmap->pm_stats.wired_count++;
		else if (!wired && (origpte & VPTE_W))
			pmap->pm_stats.wired_count--;

#if defined(PMAP_DIAGNOSTIC)
		if (pmap_nw_modified((pt_entry_t) origpte)) {
			kprintf(
	"pmap_enter: modified page not writable: va: 0x%x, pte: 0x%x\n",
			    va, origpte);
		}
#endif

		/*
		 * Remove the extra pte reference.  Note that we cannot
		 * optimize the RO->RW case because we have adjusted the
		 * wiring count above and may need to adjust the wiring
		 * bits below.
		 */
		if (mpte)
			mpte->hold_count--;

		/*
		 * We might be turning off write access to the page,
		 * so we go ahead and sense modify status.
		 */
		if (origpte & PG_MANAGED) {
			if ((origpte & VPTE_M) && pmap_track_modified(va)) {
				vm_page_t om;
				om = PHYS_TO_VM_PAGE(opa);
				vm_page_dirty(om);
			}
			pa |= PG_MANAGED;
		}
		goto validate;
	} 
	/*
	 * Mapping has changed, invalidate old range and fall through to
	 * handle validating new mapping.
	 */
	if (opa) {
		int err;
		err = pmap_remove_pte(pmap, pte, va, &info);
		if (err)
			panic("pmap_enter: pte vanished, va: 0x%x", va);
	}

	/*
	 * Enter on the PV list if part of our managed memory. Note that we
	 * raise IPL while manipulating pv_table since pmap_enter can be
	 * called at interrupt time.
	 */
	if (pmap_initialized && 
	    (m->flags & (PG_FICTITIOUS|PG_UNMANAGED)) == 0) {
		pmap_insert_entry(pmap, va, mpte, m);
		pa |= PG_MANAGED;
	}

	/*
	 * Increment counters
	 */
	pmap->pm_stats.resident_count++;
	if (wired)
		pmap->pm_stats.wired_count++;

validate:
	/*
	 * Now validate mapping with desired protection/wiring.
	 */
	newpte = (vm_offset_t) (pa | pte_prot(pmap, prot) | VPTE_V);

	if (wired)
		newpte |= VPTE_W;
	newpte |= VPTE_U;

	/*
	 * if the mapping or permission bits are different, we need
	 * to update the pte.
	 */
	if ((origpte & ~(VPTE_M|VPTE_A)) != newpte) {
		*pte = newpte | VPTE_A;
	}
	pmap_inval_flush(&info);
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
	vpte_t *pte;
	vm_paddr_t pa;
	pmap_inval_info info;

	KKASSERT(pmap != &kernel_pmap);
	pmap_inval_init(&info);

	KKASSERT(va >= VM_MIN_USER_ADDRESS && va < VM_MAX_USER_ADDRESS);

	/*
	 * Instantiate the page table page if required
	 */
	unsigned ptepindex;
	vm_offset_t ptepa;

	/*
	 * Calculate pagetable page index
	 */
	ptepindex = va >> PDRSHIFT;
	if (mpte && (mpte->pindex == ptepindex)) {
		mpte->hold_count++;
	} else {
retry:
		/*
		 * Get the page directory entry
		 */
		ptepa = (vm_offset_t) pmap->pm_pdir[ptepindex];

		/*
		 * If the page table page is mapped, we just increment
		 * the hold count, and activate it.
		 */
		if (ptepa) {
			if (ptepa & VPTE_PS)
				panic("pmap_enter_quick: unexpected mapping into 4MB page");
			if (pmap->pm_ptphint &&
				(pmap->pm_ptphint->pindex == ptepindex)) {
				mpte = pmap->pm_ptphint;
			} else {
				mpte = pmap_page_lookup( pmap->pm_pteobj, ptepindex);
				pmap->pm_ptphint = mpte;
			}
			if (mpte == NULL)
				goto retry;
			mpte->hold_count++;
		} else {
			mpte = _pmap_allocpte(pmap, ptepindex);
		}
	}

	/*
	 * Ok, now that the page table page has been validated, get the pte.
	 * If the pte is already mapped undo mpte's hold_count and
	 * just return.
	 */
	pte = pmap_pte(pmap, va);
	if (*pte) {
		if (mpte)
			pmap_unwire_pte_hold(pmap, mpte, &info);
		return 0;
	}

	/*
	 * Enter on the PV list if part of our managed memory. Note that we
	 * raise IPL while manipulating pv_table since pmap_enter can be
	 * called at interrupt time.
	 */
	if ((m->flags & (PG_FICTITIOUS|PG_UNMANAGED)) == 0)
		pmap_insert_entry(pmap, va, mpte, m);

	/*
	 * Increment counters
	 */
	pmap->pm_stats.resident_count++;

	pa = VM_PAGE_TO_PHYS(m);

	/*
	 * Now validate mapping with RO protection
	 */
	if (m->flags & (PG_FICTITIOUS|PG_UNMANAGED))
		*pte = pa | VPTE_V | VPTE_U;
	else
		*pte = pa | VPTE_V | VPTE_U | VPTE_MANAGED;

	return mpte;
}

#define MAX_INIT_PT (96)

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
	struct rb_vm_page_scan_info info;
	int psize;

	/*
	 * We can't preinit if read access isn't set or there is no pmap
	 * or object.
	 */
	if ((prot & VM_PROT_READ) == 0 || pmap == NULL || object == NULL)
		return;

	/*
	 * We can't preinit if the pmap is not the current pmap
	 */
	if (curproc == NULL || pmap != vmspace_pmap(curproc->p_vmspace))
		return;

	psize = size >> PAGE_SHIFT;

	if ((object->type != OBJT_VNODE) ||
		((limit & MAP_PREFAULT_PARTIAL) && (psize > MAX_INIT_PT) &&
			(object->resident_page_count > MAX_INIT_PT))) {
		return;
	}

	if (psize + pindex > object->size) {
		if (object->size < pindex)
			return;		  
		psize = object->size - pindex;
	}

	if (psize == 0)
		return;

	/*
	 * Use a red-black scan to traverse the requested range and load
	 * any valid pages found into the pmap.
	 *
	 * We cannot safely scan the object's memq unless we are in a
	 * critical section since interrupts can remove pages from objects.
	 */
	info.start_pindex = pindex;
	info.end_pindex = pindex + psize - 1;
	info.limit = limit;
	info.mpte = NULL;
	info.addr = addr;
	info.pmap = pmap;

	crit_enter();
	vm_page_rb_tree_RB_SCAN(&object->rb_memq, rb_vm_page_scancmp,
				pmap_object_init_pt_callback, &info);
	crit_exit();
}

static
int
pmap_object_init_pt_callback(vm_page_t p, void *data)
{
	struct rb_vm_page_scan_info *info = data;
	vm_pindex_t rel_index;
	/*
	 * don't allow an madvise to blow away our really
	 * free pages allocating pv entries.
	 */
	if ((info->limit & MAP_PREFAULT_MADVISE) &&
		vmstats.v_free_count < vmstats.v_free_reserved) {
		    return(-1);
	}
	if (((p->valid & VM_PAGE_BITS_ALL) == VM_PAGE_BITS_ALL) &&
	    (p->busy == 0) && (p->flags & (PG_BUSY | PG_FICTITIOUS)) == 0) {
		if ((p->queue - p->pc) == PQ_CACHE)
			vm_page_deactivate(p);
		vm_page_busy(p);
		rel_index = p->pindex - info->start_pindex;
		info->mpte = pmap_enter_quick(info->pmap,
					      info->addr + i386_ptob(rel_index),
					      p, info->mpte);
		vm_page_flag_set(p, PG_MAPPED);
		vm_page_wakeup(p);
	}
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
	int i;
	vm_offset_t starta;
	vm_offset_t addr;
	vm_pindex_t pindex;
	vm_page_t m, mpte;
	vm_object_t object;

	/*
	 * We do not currently prefault mappings that use virtual page
	 * tables.  We do not prefault foreign pmaps.
	 */
	if (entry->maptype == VM_MAPTYPE_VPAGETABLE)
		return;
	if (curproc == NULL || (pmap != vmspace_pmap(curproc->p_vmspace)))
		return;

	object = entry->object.vm_object;

	starta = addra - PFBAK * PAGE_SIZE;
	if (starta < entry->start)
		starta = entry->start;
	else if (starta > addra)
		starta = 0;

	/*
	 * critical section protection is required to maintain the 
	 * page/object association, interrupts can free pages and remove 
	 * them from their objects.
	 */
	mpte = NULL;
	crit_enter();
	for (i = 0; i < PAGEORDER_SIZE; i++) {
		vm_object_t lobject;
		vpte_t *pte;

		addr = addra + pmap_prefault_pageorder[i];
		if (addr > addra + (PFFOR * PAGE_SIZE))
			addr = 0;

		if (addr < starta || addr >= entry->end)
			continue;

		/*
		 * Make sure the page table page already exists
		 */
		if ((*pmap_pde(pmap, addr)) == NULL) 
			continue;

		/*
		 * Get a pointer to the pte and make sure that no valid page
		 * has been mapped.
		 */
		pte = get_ptbase(pmap) + (addr >> PAGE_SHIFT);
		if (*pte)
			continue;

		/*
		 * Get the page to be mapped
		 */
		pindex = ((addr - entry->start) + entry->offset) >> PAGE_SHIFT;
		lobject = object;

		for (m = vm_page_lookup(lobject, pindex);
		    (!m && (lobject->type == OBJT_DEFAULT) &&
		     (lobject->backing_object));
		    lobject = lobject->backing_object
		) {
			if (lobject->backing_object_offset & PAGE_MASK)
				break;
			pindex += (lobject->backing_object_offset >> PAGE_SHIFT);
			m = vm_page_lookup(lobject->backing_object, pindex);
		}

		/*
		 * give-up when a page is not in memory
		 */
		if (m == NULL)
			break;

		/*
		 * If everything meets the requirements for pmap_enter_quick(),
		 * then enter the page.
		 */

		if (((m->valid & VM_PAGE_BITS_ALL) == VM_PAGE_BITS_ALL) &&
			(m->busy == 0) &&
		    (m->flags & (PG_BUSY | PG_FICTITIOUS)) == 0) {

			if ((m->queue - m->pc) == PQ_CACHE) {
				vm_page_deactivate(m);
			}
			vm_page_busy(m);
			mpte = pmap_enter_quick(pmap, addr, m, mpte);
			vm_page_flag_set(m, PG_MAPPED);
			vm_page_wakeup(m);
		}
	}
	crit_exit();
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
	vpte_t *pte;

	if (pmap == NULL)
		return;

	pte = get_ptbase(pmap) + (va >> PAGE_SHIFT);

	if (wired && (*pte & VPTE_W) == 0)
		pmap->pm_stats.wired_count++;
	else if (!wired && (*pte & VPTE_W))
		pmap->pm_stats.wired_count--;

	/*
	 * Wiring is not a hardware characteristic so there is no need to
	 * invalidate TLB.  However, in an SMP environment we must use
	 * a locked bus cycle to update the pte (if we are not using 
	 * the pmap_inval_*() API that is)... it's ok to do this for simple
	 * wiring changes.
	 */
#ifdef SMP
	if (wired)
		atomic_set_int(pte, VPTE_W);
	else
		atomic_clear_int(pte, VPTE_W);
#else
	if (wired)
		atomic_set_int_nonlocked(pte, VPTE_W);
	else
		atomic_clear_int_nonlocked(pte, VPTE_W);
#endif
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
	pmap_inval_info info;
	vm_offset_t addr;
	vm_offset_t end_addr = src_addr + len;
	vm_offset_t pdnxt;
	vpte_t *src_frame;
	vpte_t *dst_frame;
	vm_page_t m;

	if (dst_addr != src_addr)
		return;
	if (dst_pmap->pm_pdir == NULL)
		return;
	if (src_pmap->pm_pdir == NULL)
		return;

	src_frame = get_ptbase1(src_pmap);
	dst_frame = get_ptbase2(dst_pmap);

	pmap_inval_init(&info);
	pmap_inval_add(&info, dst_pmap, -1);
	pmap_inval_add(&info, src_pmap, -1);

	/*
	 * critical section protection is required to maintain the page/object
	 * association, interrupts can free pages and remove them from 
	 * their objects.
	 */
	crit_enter();
	for (addr = src_addr; addr < end_addr; addr = pdnxt) {
		vpte_t *src_pte, *dst_pte;
		vm_page_t dstmpte, srcmpte;
		vm_offset_t srcptepaddr;
		unsigned ptepindex;

		if (addr >= VM_MAX_USER_ADDRESS)
			panic("pmap_copy: invalid to pmap_copy page tables\n");

		/*
		 * Don't let optional prefaulting of pages make us go
		 * way below the low water mark of free pages or way
		 * above high water mark of used pv entries.
		 */
		if (vmstats.v_free_count < vmstats.v_free_reserved ||
		    pv_entry_count > pv_entry_high_water)
			break;
		
		pdnxt = ((addr + PAGE_SIZE*NPTEPG) & ~(PAGE_SIZE*NPTEPG - 1));
		ptepindex = addr >> PDRSHIFT;

		srcptepaddr = (vm_offset_t) src_pmap->pm_pdir[ptepindex];
		if (srcptepaddr == 0)
			continue;
			
		if (srcptepaddr & VPTE_PS) {
			if (dst_pmap->pm_pdir[ptepindex] == 0) {
				dst_pmap->pm_pdir[ptepindex] = (pd_entry_t) srcptepaddr;
				dst_pmap->pm_stats.resident_count += NBPDR / PAGE_SIZE;
			}
			continue;
		}

		srcmpte = vm_page_lookup(src_pmap->pm_pteobj, ptepindex);
		if ((srcmpte == NULL) ||
			(srcmpte->hold_count == 0) || (srcmpte->flags & PG_BUSY))
			continue;

		if (pdnxt > end_addr)
			pdnxt = end_addr;

		src_pte = src_frame + (addr >> PAGE_SHIFT);
		dst_pte = dst_frame + (addr >> PAGE_SHIFT);
		while (addr < pdnxt) {
			vpte_t ptetemp;
			ptetemp = *src_pte;
			/*
			 * we only virtual copy managed pages
			 */
			if ((ptetemp & PG_MANAGED) != 0) {
				/*
				 * We have to check after allocpte for the
				 * pte still being around...  allocpte can
				 * block.
				 */
				dstmpte = pmap_allocpte(dst_pmap, addr);
				if ((*dst_pte == 0) && (ptetemp = *src_pte)) {
					/*
					 * Clear the modified and
					 * accessed (referenced) bits
					 * during the copy.
					 */
					m = PHYS_TO_VM_PAGE(ptetemp);
					*dst_pte = ptetemp & ~(VPTE_M | VPTE_A);
					dst_pmap->pm_stats.resident_count++;
					pmap_insert_entry(dst_pmap, addr,
						dstmpte, m);
	 			} else {
					pmap_unwire_pte_hold(dst_pmap, dstmpte, &info);
				}
				if (dstmpte->hold_count >= srcmpte->hold_count)
					break;
			}
			addr += PAGE_SIZE;
			src_pte++;
			dst_pte++;
		}
	}
	crit_exit();
	pmap_inval_flush(&info);
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
	struct mdglobaldata *gd = mdcpu;

	crit_enter();
	if (*(int *)gd->gd_CMAP3)
		panic("pmap_zero_page: CMAP3 busy");
	*(int *)gd->gd_CMAP3 =
		    VPTE_V | VPTE_W | (phys & VPTE_FRAME) | VPTE_A | VPTE_M;
	cpu_invlpg(gd->gd_CADDR3);

	bzero(gd->gd_CADDR3, PAGE_SIZE);
	*(int *) gd->gd_CMAP3 = 0;
	crit_exit();
}

/*
 * pmap_page_assertzero:
 *
 *	Assert that a page is empty, panic if it isn't.
 */
void
pmap_page_assertzero(vm_paddr_t phys)
{
	struct mdglobaldata *gd = mdcpu;
	int i;

	crit_enter();
	if (*(int *)gd->gd_CMAP3)
		panic("pmap_zero_page: CMAP3 busy");
	*(int *)gd->gd_CMAP3 =
		    VPTE_V | VPTE_R | VPTE_W | (phys & VPTE_FRAME) | VPTE_A | VPTE_M;
	cpu_invlpg(gd->gd_CADDR3);
	for (i = 0; i < PAGE_SIZE; i += 4) {
	    if (*(int *)((char *)gd->gd_CADDR3 + i) != 0) {
		panic("pmap_page_assertzero() @ %p not zero!\n",
		    (void *)gd->gd_CADDR3);
	    }
	}
	*(int *) gd->gd_CMAP3 = 0;
	crit_exit();
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
	struct mdglobaldata *gd = mdcpu;

	crit_enter();
	if (*(int *) gd->gd_CMAP3)
		panic("pmap_zero_page: CMAP3 busy");
	*(int *) gd->gd_CMAP3 = VPTE_V | VPTE_R | VPTE_W | (phys & VPTE_FRAME) | VPTE_A | VPTE_M;
	cpu_invlpg(gd->gd_CADDR3);

	bzero((char *)gd->gd_CADDR3 + off, size);
	*(int *) gd->gd_CMAP3 = 0;
	crit_exit();
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
	struct mdglobaldata *gd = mdcpu;

	crit_enter();
	if (*(int *) gd->gd_CMAP1)
		panic("pmap_copy_page: CMAP1 busy");
	if (*(int *) gd->gd_CMAP2)
		panic("pmap_copy_page: CMAP2 busy");

	*(int *) gd->gd_CMAP1 = VPTE_V | (src & PG_FRAME) | PG_A;
	*(int *) gd->gd_CMAP2 = VPTE_V | VPTE_R | VPTE_W | (dst & VPTE_FRAME) | VPTE_A | VPTE_M;

	cpu_invlpg(gd->gd_CADDR1);
	cpu_invlpg(gd->gd_CADDR2);

	bcopy(gd->gd_CADDR1, gd->gd_CADDR2, PAGE_SIZE);

	*(int *) gd->gd_CMAP1 = 0;
	*(int *) gd->gd_CMAP2 = 0;
	crit_exit();
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
	struct mdglobaldata *gd = mdcpu;

	crit_enter();
	if (*(int *) gd->gd_CMAP1)
		panic("pmap_copy_page: CMAP1 busy");
	if (*(int *) gd->gd_CMAP2)
		panic("pmap_copy_page: CMAP2 busy");

	*(int *) gd->gd_CMAP1 = VPTE_V | (src & VPTE_FRAME) | VPTE_A;
	*(int *) gd->gd_CMAP2 = VPTE_V | VPTE_R | VPTE_W | (dst & VPTE_FRAME) | VPTE_A | VPTE_M;

	cpu_invlpg(gd->gd_CADDR1);
	cpu_invlpg(gd->gd_CADDR2);

	bcopy((char *)gd->gd_CADDR1 + (src & PAGE_MASK),
	      (char *)gd->gd_CADDR2 + (dst & PAGE_MASK),
	      bytes);

	*(int *) gd->gd_CMAP1 = 0;
	*(int *) gd->gd_CMAP2 = 0;
	crit_exit();
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
	pv_entry_t pv;
	int loops = 0;

	if (!pmap_initialized || (m->flags & PG_FICTITIOUS))
		return FALSE;

	crit_enter();

	TAILQ_FOREACH(pv, &m->md.pv_list, pv_list) {
		if (pv->pv_pmap == pmap) {
			crit_exit();
			return TRUE;
		}
		loops++;
		if (loops >= 16)
			break;
	}
	crit_exit();
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
	vpte_t *pte, tpte;
	pv_entry_t pv, npv;
	vm_page_t m;
	pmap_inval_info info;
	int iscurrentpmap;

	if (curproc && pmap == vmspace_pmap(curproc->p_vmspace))
		iscurrentpmap = 1;
	else
		iscurrentpmap = 0;

	pmap_inval_init(&info);
	crit_enter();
	for (pv = TAILQ_FIRST(&pmap->pm_pvlist); pv; pv = npv) {
		if (pv->pv_va >= eva || pv->pv_va < sva) {
			npv = TAILQ_NEXT(pv, pv_plist);
			continue;
		}

		pte = pmap_pte(pv->pv_pmap, pv->pv_va);
		if (pmap->pm_active)
			pmap_inval_add(&info, pv->pv_pmap, pv->pv_va);
		tpte = *pte;

		/*
		 * We cannot remove wired pages from a process' mapping
		 * at this time
		 */
		if (tpte & VPTE_W) {
			npv = TAILQ_NEXT(pv, pv_plist);
			continue;
		}
		*pte = 0;

		m = PHYS_TO_VM_PAGE(tpte);

		KASSERT(m < &vm_page_array[vm_page_array_size],
			("pmap_remove_pages: bad tpte %x", tpte));

		pv->pv_pmap->pm_stats.resident_count--;

		/*
		 * Update the vm_page_t clean and reference bits.
		 */
		if (tpte & VPTE_M) {
			vm_page_dirty(m);
		}


		npv = TAILQ_NEXT(pv, pv_plist);
		TAILQ_REMOVE(&pv->pv_pmap->pm_pvlist, pv, pv_plist);

		m->md.pv_list_count--;
		TAILQ_REMOVE(&m->md.pv_list, pv, pv_list);
		if (TAILQ_FIRST(&m->md.pv_list) == NULL) {
			vm_page_flag_clear(m, PG_MAPPED | PG_WRITEABLE);
		}

		pmap_unuse_pt(pv->pv_pmap, pv->pv_va, pv->pv_ptem, &info);
		free_pv_entry(pv);
	}
	pmap_inval_flush(&info);
	crit_exit();
}

/*
 * pmap_testbit tests bits in pte's
 * note that the testbit/changebit routines are inline,
 * and a lot of things compile-time evaluate.
 */
static boolean_t
pmap_testbit(vm_page_t m, int bit)
{
	pv_entry_t pv;
	vpte_t *pte;

	if (!pmap_initialized || (m->flags & PG_FICTITIOUS))
		return FALSE;

	if (TAILQ_FIRST(&m->md.pv_list) == NULL)
		return FALSE;

	crit_enter();

	TAILQ_FOREACH(pv, &m->md.pv_list, pv_list) {
		/*
		 * if the bit being tested is the modified bit, then
		 * mark clean_map and ptes as never
		 * modified.
		 */
		if (bit & (VPTE_A|VPTE_M)) {
			if (!pmap_track_modified(pv->pv_va))
				continue;
		}

#if defined(PMAP_DIAGNOSTIC)
		if (!pv->pv_pmap) {
			kprintf("Null pmap (tb) at va: 0x%x\n", pv->pv_va);
			continue;
		}
#endif
		pte = pmap_pte(pv->pv_pmap, pv->pv_va);
		if (*pte & bit) {
			crit_exit();
			return TRUE;
		}
	}
	crit_exit();
	return (FALSE);
}

/*
 * this routine is used to modify bits in ptes
 */
static __inline void
pmap_changebit(vm_page_t m, int bit, boolean_t setem)
{
	struct pmap_inval_info info;
	pv_entry_t pv;
	vpte_t *pte;

	if (!pmap_initialized || (m->flags & PG_FICTITIOUS))
		return;

	pmap_inval_init(&info);
	crit_enter();

	/*
	 * Loop over all current mappings setting/clearing as appropos If
	 * setting RO do we need to clear the VAC?
	 */
	TAILQ_FOREACH(pv, &m->md.pv_list, pv_list) {
		/*
		 * don't write protect pager mappings
		 */
		if (!setem && (bit == VPTE_W)) {
			if (!pmap_track_modified(pv->pv_va))
				continue;
		}

#if defined(PMAP_DIAGNOSTIC)
		if (!pv->pv_pmap) {
			kprintf("Null pmap (cb) at va: 0x%x\n", pv->pv_va);
			continue;
		}
#endif

		/*
		 * Careful here.  We can use a locked bus instruction to
		 * clear VPTE_A or VPTE_M safely but we need to synchronize
		 * with the target cpus when we mess with VPTE_W.
		 */
		pte = pmap_pte(pv->pv_pmap, pv->pv_va);
		if (bit == VPTE_W)
			pmap_inval_add(&info, pv->pv_pmap, pv->pv_va);

		if (setem) {
#ifdef SMP
			atomic_set_int(pte, bit);
#else
			atomic_set_int_nonlocked(pte, bit);
#endif
		} else {
			vm_offset_t pbits = *(vm_offset_t *)pte;
			if (pbits & bit) {
				if (bit == VPTE_W) {
					if (pbits & VPTE_M) {
						vm_page_dirty(m);
					}
#ifdef SMP
					atomic_clear_int(pte, VPTE_M|VPTE_W);
#else
					atomic_clear_int_nonlocked(pte, VPTE_M|VPTE_W);
#endif
				} else {
#ifdef SMP
					atomic_clear_int(pte, bit);
#else
					atomic_clear_int_nonlocked(pte, bit);
#endif
				}
			}
		}
	}
	pmap_inval_flush(&info);
	crit_exit();
}

/*
 *      pmap_page_protect:
 *
 *      Lower the permission for all mappings to a given page.
 */
void
pmap_page_protect(vm_page_t m, vm_prot_t prot)
{
	if ((prot & VM_PROT_WRITE) == 0) {
		if (prot & (VM_PROT_READ | VM_PROT_EXECUTE)) {
			pmap_changebit(m, VPTE_W, FALSE);
		} else {
			pmap_remove_all(m);
		}
	}
}

vm_paddr_t
pmap_phys_address(int ppn)
{
	return (i386_ptob(ppn));
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
	pv_entry_t pv, pvf, pvn;
	vpte_t *pte;
	int rtval = 0;

	if (!pmap_initialized || (m->flags & PG_FICTITIOUS))
		return (rtval);

	crit_enter();

	if ((pv = TAILQ_FIRST(&m->md.pv_list)) != NULL) {

		pvf = pv;

		do {
			pvn = TAILQ_NEXT(pv, pv_list);

			TAILQ_REMOVE(&m->md.pv_list, pv, pv_list);

			TAILQ_INSERT_TAIL(&m->md.pv_list, pv, pv_list);

			if (!pmap_track_modified(pv->pv_va))
				continue;

			pte = pmap_pte(pv->pv_pmap, pv->pv_va);

			if (pte && (*pte & VPTE_A)) {
#ifdef SMP
				atomic_clear_int(pte, VPTE_A);
#else
				atomic_clear_int_nonlocked(pte, VPTE_A);
#endif
				rtval++;
				if (rtval > 4) {
					break;
				}
			}
		} while ((pv = pvn) != NULL && pv != pvf);
	}
	crit_exit();

	return (rtval);
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
	return pmap_testbit(m, VPTE_M);
}

/*
 *	Clear the modify bits on the specified physical page.
 */
void
pmap_clear_modify(vm_page_t m)
{
	pmap_changebit(m, VPTE_M, FALSE);
}

/*
 *	pmap_clear_reference:
 *
 *	Clear the reference bit on the specified physical page.
 */
void
pmap_clear_reference(vm_page_t m)
{
	pmap_changebit(m, VPTE_A, FALSE);
}

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

/*
 * perform the pmap work for mincore
 */
int
pmap_mincore(pmap_t pmap, vm_offset_t addr)
{
	vpte_t *ptep, pte;
	vm_page_t m;
	int val = 0;
	
	ptep = pmap_pte(pmap, addr);
	if (ptep == 0) {
		return 0;
	}

	if ((pte = *ptep) != 0) {
		vm_offset_t pa;

		val = MINCORE_INCORE;
		if ((pte & VPTE_MANAGED) == 0)
			return val;

		pa = pte & VPTE_FRAME;

		m = PHYS_TO_VM_PAGE(pa);

		/*
		 * Modified by us
		 */
		if (pte & VPTE_M)
			val |= MINCORE_MODIFIED|MINCORE_MODIFIED_OTHER;
		/*
		 * Modified by someone
		 */
		else if (m->dirty || pmap_is_modified(m))
			val |= MINCORE_MODIFIED_OTHER;
		/*
		 * Referenced by us
		 */
		if (pte & VPTE_A)
			val |= MINCORE_REFERENCED|MINCORE_REFERENCED_OTHER;

		/*
		 * Referenced by someone
		 */
		else if ((m->flags & PG_REFERENCED) || pmap_ts_referenced(m)) {
			val |= MINCORE_REFERENCED_OTHER;
			vm_page_flag_set(m, PG_REFERENCED);
		}
	} 
	return val;
}

void
pmap_activate(struct proc *p)
{
	pmap_t	pmap;

	pmap = vmspace_pmap(p->p_vmspace);
#if defined(SMP)
	atomic_set_int(&pmap->pm_active, 1 << mycpu->gd_cpuid);
#else
	pmap->pm_active |= 1;
#endif
#if defined(SWTCH_OPTIM_STATS)
	tlb_flush_count++;
#endif
	p->p_thread->td_pcb->pcb_cr3 = vtophys(pmap->pm_pdir);
	load_cr3(p->p_thread->td_pcb->pcb_cr3);
}

void
pmap_deactivate(struct proc *p)
{
	pmap_t	pmap;

	pmap = vmspace_pmap(p->p_vmspace);
#if defined(SMP)
	atomic_clear_int(&pmap->pm_active, 1 << mycpu->gd_cpuid);
#else
	pmap->pm_active &= ~1;
#endif
	/*
	 * XXX - note we do not adjust %cr3.  The caller is expected to
	 * activate a new pmap or do a thread-exit.
	 */
}

vm_offset_t
pmap_addr_hint(vm_object_t obj, vm_offset_t addr, vm_size_t size)
{

	if ((obj == NULL) || (size < NBPDR) || (obj->type != OBJT_DEVICE)) {
		return addr;
	}

	addr = (addr + (NBPDR - 1)) & ~(NBPDR - 1);
	return addr;
}


#if defined(DEBUG)

static void	pads (pmap_t pm);
void		pmap_pvdump (vm_paddr_t pa);

/* print address space of pmap*/
static void
pads(pmap_t pm)
{
	vm_offset_t va;
	int i, j;
	vpte_t *ptep;

	if (pm == &kernel_pmap)
		return;
	for (i = 0; i < 1024; i++)
		if (pm->pm_pdir[i])
			for (j = 0; j < 1024; j++) {
				va = (i << PDRSHIFT) + (j << PAGE_SHIFT);
				if (pm == &kernel_pmap && va < KERNBASE)
					continue;
				if (pm != &kernel_pmap && va > UPT_MAX_ADDRESS)
					continue;
				ptep = pmap_pte(pm, va);
				if (ptep && (*ptep & VPTE_V)) {
					kprintf("%p:%x ",
						(void *)va, (unsigned)*ptep);
				}
			};

}

void
pmap_pvdump(vm_paddr_t pa)
{
	pv_entry_t pv;
	vm_page_t m;

	kprintf("pa %08llx", (long long)pa);
	m = PHYS_TO_VM_PAGE(pa);
	TAILQ_FOREACH(pv, &m->md.pv_list, pv_list) {
#ifdef used_to_be
		kprintf(" -> pmap %p, va %x, flags %x",
		    (void *)pv->pv_pmap, pv->pv_va, pv->pv_flags);
#endif
		kprintf(" -> pmap %p, va %x", (void *)pv->pv_pmap, pv->pv_va);
		pads(pv->pv_pmap);
	}
	kprintf(" ");
}
#endif
