/*
 * (MPSAFE)
 *
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
#include <sys/vkernel.h>
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

#include <sys/sysref2.h>
#include <sys/spinlock2.h>

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
 * Typically used to initialize a fictitious page by vm/device_pager.c
 */
void
pmap_page_init(struct vm_page *m)
{
	vm_page_init(m);
	TAILQ_INIT(&m->md.pv_list);
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
pmap_bootstrap(void)
{
	vm_pindex_t i = (vm_offset_t)KernelPTD >> PAGE_SHIFT;

	/*
	 * The kernel_pmap's pm_pteobj is used only for locking and not
	 * for mmu pages.
	 */
	kernel_pmap.pm_pdir = KernelPTD - (KvaStart >> SEG_SHIFT);
	kernel_pmap.pm_pdirpte = KernelPTA[i];
	kernel_pmap.pm_count = 1;
	kernel_pmap.pm_active = (cpumask_t)-1 & ~CPUMASK_LOCK;
	kernel_pmap.pm_pteobj = &kernel_object;
	TAILQ_INIT(&kernel_pmap.pm_pvlist);
	TAILQ_INIT(&kernel_pmap.pm_pvlist_free);
	spin_init(&kernel_pmap.pm_spin);
	lwkt_token_init(&kernel_pmap.pm_token, "kpmap_tok");
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
		    (vpte_t *)kmem_alloc_pageable(&kernel_map, PAGE_SIZE);
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
			     VM_ALLOC_NORMAL | VM_ALLOC_RETRY | VM_ALLOC_ZERO);
	vm_page_wire(ptdpg);

	/* not usually mapped */
	vm_page_flag_clear(ptdpg, PG_MAPPED);
	vm_page_wakeup(ptdpg);

	pmap_kenter((vm_offset_t)pmap->pm_pdir, VM_PAGE_TO_PHYS(ptdpg));
	pmap->pm_pdirpte = KernelPTA[(vm_offset_t)pmap->pm_pdir >> PAGE_SHIFT];

	pmap->pm_count = 1;
	pmap->pm_active = 0;
	pmap->pm_ptphint = NULL;
	pmap->pm_cpucachemask = 0;
	TAILQ_INIT(&pmap->pm_pvlist);
	TAILQ_INIT(&pmap->pm_pvlist_free);
	spin_init(&pmap->pm_spin);
	lwkt_token_init(&pmap->pm_token, "pmap_tok");
	bzero(&pmap->pm_stats, sizeof pmap->pm_stats);
	pmap->pm_stats.resident_count = 1;
}

/*
 * Clean up a pmap structure so it can be physically freed
 *
 * No requirements.
 */
void
pmap_puninit(pmap_t pmap)
{
	if (pmap->pm_pdir) {
		kmem_free(&kernel_map, (vm_offset_t)pmap->pm_pdir, PAGE_SIZE);
		pmap->pm_pdir = NULL;
	}
	if (pmap->pm_pteobj) {
		vm_object_deallocate(pmap->pm_pteobj);
		pmap->pm_pteobj = NULL;
	}
}


/*
 * Wire in kernel global address entries.  To avoid a race condition
 * between pmap initialization and pmap_growkernel, this procedure
 * adds the pmap to the master list (which growkernel scans to update),
 * then copies the template.
 *
 * In a virtual kernel there are no kernel global address entries.
 *
 * No requirements.
 */
void
pmap_pinit2(struct pmap *pmap)
{
	spin_lock(&pmap_spin);
	TAILQ_INSERT_TAIL(&pmap_list, pmap, pm_pmnode);
	spin_unlock(&pmap_spin);
}

/*
 * Release all resources held by the given physical map.
 *
 * Should only be called if the map contains no valid mappings.
 *
 * Caller must hold pmap->pm_token
 */
static int pmap_release_callback(struct vm_page *p, void *data);

void
pmap_release(struct pmap *pmap)
{
	struct mdglobaldata *gd = mdcpu;
	vm_object_t object = pmap->pm_pteobj;
	struct rb_vm_page_scan_info info;

	KKASSERT(pmap != &kernel_pmap);

#if defined(DIAGNOSTIC)
	if (object->ref_count != 1)
		panic("pmap_release: pteobj reference count != 1");
#endif
	/*
	 * Once we destroy the page table, the mapping becomes invalid.
	 * Don't waste time doing a madvise to invalidate the mapping, just
	 * set cpucachemask to 0.
	 */
	if (pmap->pm_pdir == gd->gd_PT1pdir) {
		gd->gd_PT1pdir = NULL;
		*gd->gd_PT1pde = 0;
		/* madvise(gd->gd_PT1map, SEG_SIZE, MADV_INVAL); */
	}
	if (pmap->pm_pdir == gd->gd_PT2pdir) {
		gd->gd_PT2pdir = NULL;
		*gd->gd_PT2pde = 0;
		/* madvise(gd->gd_PT2map, SEG_SIZE, MADV_INVAL); */
	}
	if (pmap->pm_pdir == gd->gd_PT3pdir) {
		gd->gd_PT3pdir = NULL;
		*gd->gd_PT3pde = 0;
		/* madvise(gd->gd_PT3map, SEG_SIZE, MADV_INVAL); */
	}
	
	info.pmap = pmap;
	info.object = object;

	spin_lock(&pmap_spin);
	TAILQ_REMOVE(&pmap_list, pmap, pm_pmnode);
	spin_unlock(&pmap_spin);

	vm_object_hold(object);
	do {
		info.error = 0;
		info.mpte = NULL;
		info.limit = object->generation;

		vm_page_rb_tree_RB_SCAN(&object->rb_memq, NULL, 
				        pmap_release_callback, &info);
		if (info.error == 0 && info.mpte) {
			if (!pmap_release_free_page(pmap, info.mpte))
				info.error = 1;
		}
	} while (info.error);
	vm_object_drop(object);

	/*
	 * Leave the KVA reservation for pm_pdir cached for later reuse.
	 */
	pmap->pm_pdirpte = 0;
	pmap->pm_cpucachemask = 0;
}

/*
 * Callback to release a page table page backing a directory
 * entry.
 */
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
 * Add a reference to the specified pmap.
 *
 * No requirements.
 */
void
pmap_reference(pmap_t pmap)
{
	if (pmap) {
		lwkt_gettoken(&vm_token);
		++pmap->pm_count;
		lwkt_reltoken(&vm_token);
	}
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
	int r;
	void *rp;

#define LAST_EXTENT	(VM_MAX_USER_ADDRESS - 0x80000000)

	if (vmspace_create(&vm->vm_pmap, 0, NULL) < 0)
		panic("vmspace_create() failed");

	rp = vmspace_mmap(&vm->vm_pmap, (void *)0x00000000, 0x40000000,
			  PROT_READ|PROT_WRITE,
			  MAP_FILE|MAP_SHARED|MAP_VPAGETABLE|MAP_FIXED,
			  MemImageFd, 0);
	if (rp == MAP_FAILED)
		panic("vmspace_mmap: failed1");
	vmspace_mcontrol(&vm->vm_pmap, (void *)0x00000000, 0x40000000,
			 MADV_NOSYNC, 0);
	rp = vmspace_mmap(&vm->vm_pmap, (void *)0x40000000, 0x40000000,
			  PROT_READ|PROT_WRITE,
			  MAP_FILE|MAP_SHARED|MAP_VPAGETABLE|MAP_FIXED,
			  MemImageFd, 0x40000000);
	if (rp == MAP_FAILED)
		panic("vmspace_mmap: failed2");
	vmspace_mcontrol(&vm->vm_pmap, (void *)0x40000000, 0x40000000,
			 MADV_NOSYNC, 0);
	rp = vmspace_mmap(&vm->vm_pmap, (void *)0x80000000, LAST_EXTENT,
			  PROT_READ|PROT_WRITE,
			  MAP_FILE|MAP_SHARED|MAP_VPAGETABLE|MAP_FIXED,
			  MemImageFd, 0x80000000);
	vmspace_mcontrol(&vm->vm_pmap, (void *)0x80000000, LAST_EXTENT,
			 MADV_NOSYNC, 0);
	if (rp == MAP_FAILED)
		panic("vmspace_mmap: failed3");

	r = vmspace_mcontrol(&vm->vm_pmap, (void *)0x00000000, 0x40000000, 
			     MADV_SETMAP, vmspace_pmap(vm)->pm_pdirpte);
	if (r < 0)
		panic("vmspace_mcontrol: failed1");
	r = vmspace_mcontrol(&vm->vm_pmap, (void *)0x40000000, 0x40000000,
			     MADV_SETMAP, vmspace_pmap(vm)->pm_pdirpte);
	if (r < 0)
		panic("vmspace_mcontrol: failed2");
	r = vmspace_mcontrol(&vm->vm_pmap, (void *)0x80000000, LAST_EXTENT,
			     MADV_SETMAP, vmspace_pmap(vm)->pm_pdirpte);
	if (r < 0)
		panic("vmspace_mcontrol: failed3");
}

void
cpu_vmspace_free(struct vmspace *vm)
{
	if (vmspace_destroy(&vm->vm_pmap) < 0)
		panic("vmspace_destroy() failed");
}

/************************************************************************
 *	    Procedures which operate directly on the kernel PMAP 	*
 ************************************************************************/

/*
 * This maps the requested page table and gives us access to it.
 *
 * This routine can be called from a potentially preempting interrupt
 * thread or from a normal thread.
 */
static vpte_t *
get_ptbase(struct pmap *pmap, vm_offset_t va)
{
	struct mdglobaldata *gd = mdcpu;

	if (pmap == &kernel_pmap) {
		KKASSERT(va >= KvaStart && va < KvaEnd);
		return(KernelPTA + (va >> PAGE_SHIFT));
	} else if (pmap->pm_pdir == gd->gd_PT1pdir) {
		if ((pmap->pm_cpucachemask & gd->mi.gd_cpumask) == 0) {
			*gd->gd_PT1pde = pmap->pm_pdirpte;
			madvise(gd->gd_PT1map, SEG_SIZE, MADV_INVAL);
			atomic_set_cpumask(&pmap->pm_cpucachemask,
					   gd->mi.gd_cpumask);
		}
		return(gd->gd_PT1map + (va >> PAGE_SHIFT));
	} else if (pmap->pm_pdir == gd->gd_PT2pdir) {
		if ((pmap->pm_cpucachemask & gd->mi.gd_cpumask) == 0) {
			*gd->gd_PT2pde = pmap->pm_pdirpte;
			madvise(gd->gd_PT2map, SEG_SIZE, MADV_INVAL);
			atomic_set_cpumask(&pmap->pm_cpucachemask,
					   gd->mi.gd_cpumask);
		}
		return(gd->gd_PT2map + (va >> PAGE_SHIFT));
	}

	/*
	 * If we aren't running from a potentially preempting interrupt,
	 * load a new page table directory into the page table cache
	 */
	if (gd->mi.gd_intr_nesting_level == 0 &&
	    (gd->mi.gd_curthread->td_flags & TDF_INTTHREAD) == 0) {
		/*
		 * Choose one or the other and map the page table
		 * in the KVA space reserved for it.
		 */
		if ((gd->gd_PTflip = 1 - gd->gd_PTflip) == 0) {
			gd->gd_PT1pdir = pmap->pm_pdir;
			*gd->gd_PT1pde = pmap->pm_pdirpte;
			madvise(gd->gd_PT1map, SEG_SIZE, MADV_INVAL);
			atomic_set_cpumask(&pmap->pm_cpucachemask,
					   gd->mi.gd_cpumask);
			return(gd->gd_PT1map + (va >> PAGE_SHIFT));
		} else {
			gd->gd_PT2pdir = pmap->pm_pdir;
			*gd->gd_PT2pde = pmap->pm_pdirpte;
			madvise(gd->gd_PT2map, SEG_SIZE, MADV_INVAL);
			atomic_set_cpumask(&pmap->pm_cpucachemask,
					   gd->mi.gd_cpumask);
			return(gd->gd_PT2map + (va >> PAGE_SHIFT));
		}
	}

	/*
	 * If we are running from a preempting interrupt use a private
	 * map.  The caller must be in a critical section.
	 */
	KKASSERT(IN_CRITICAL_SECT(curthread));
	if (pmap->pm_pdir == gd->gd_PT3pdir) {
		if ((pmap->pm_cpucachemask & gd->mi.gd_cpumask) == 0) {
			*gd->gd_PT3pde = pmap->pm_pdirpte;
			madvise(gd->gd_PT3map, SEG_SIZE, MADV_INVAL);
			atomic_set_cpumask(&pmap->pm_cpucachemask,
					   gd->mi.gd_cpumask);
		}
	} else {
		gd->gd_PT3pdir = pmap->pm_pdir;
		*gd->gd_PT3pde = pmap->pm_pdirpte;
		madvise(gd->gd_PT3map, SEG_SIZE, MADV_INVAL);
		atomic_set_cpumask(&pmap->pm_cpucachemask,
				   gd->mi.gd_cpumask);
	}
	return(gd->gd_PT3map + (va >> PAGE_SHIFT));
}

static vpte_t *
get_ptbase1(struct pmap *pmap, vm_offset_t va)
{
	struct mdglobaldata *gd = mdcpu;

	if (pmap == &kernel_pmap) {
		KKASSERT(va >= KvaStart && va < KvaEnd);
		return(KernelPTA + (va >> PAGE_SHIFT));
	} else if (pmap->pm_pdir == gd->gd_PT1pdir) {
		if ((pmap->pm_cpucachemask & gd->mi.gd_cpumask) == 0) {
			*gd->gd_PT1pde = pmap->pm_pdirpte;
			madvise(gd->gd_PT1map, SEG_SIZE, MADV_INVAL);
			atomic_set_cpumask(&pmap->pm_cpucachemask,
					   gd->mi.gd_cpumask);
		}
		return(gd->gd_PT1map + (va >> PAGE_SHIFT));
	}
	KKASSERT(gd->mi.gd_intr_nesting_level == 0 &&
		 (gd->mi.gd_curthread->td_flags & TDF_INTTHREAD) == 0);
	gd->gd_PT1pdir = pmap->pm_pdir;
	*gd->gd_PT1pde = pmap->pm_pdirpte;
	madvise(gd->gd_PT1map, SEG_SIZE, MADV_INVAL);
	return(gd->gd_PT1map + (va >> PAGE_SHIFT));
}

static vpte_t *
get_ptbase2(struct pmap *pmap, vm_offset_t va)
{
	struct mdglobaldata *gd = mdcpu;

	if (pmap == &kernel_pmap) {
		KKASSERT(va >= KvaStart && va < KvaEnd);
		return(KernelPTA + (va >> PAGE_SHIFT));
	} else if (pmap->pm_pdir == gd->gd_PT2pdir) {
		if ((pmap->pm_cpucachemask & gd->mi.gd_cpumask) == 0) {
			*gd->gd_PT2pde = pmap->pm_pdirpte;
			madvise(gd->gd_PT2map, SEG_SIZE, MADV_INVAL);
			atomic_set_cpumask(&pmap->pm_cpucachemask,
					   gd->mi.gd_cpumask);
		}
		return(gd->gd_PT2map + (va >> PAGE_SHIFT));
	}
	KKASSERT(gd->mi.gd_intr_nesting_level == 0 &&
		 (gd->mi.gd_curthread->td_flags & TDF_INTTHREAD) == 0);
	gd->gd_PT2pdir = pmap->pm_pdir;
	*gd->gd_PT2pde = pmap->pm_pdirpte;
	madvise(gd->gd_PT2map, SEG_SIZE, MADV_INVAL);
	return(gd->gd_PT2map + (va >> PAGE_SHIFT));
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

	ptep = &pmap->pm_pdir[va >> SEG_SHIFT];
	if (*ptep & VPTE_PS)
		return(ptep);
	if (*ptep)
		return (get_ptbase(pmap, va));
	return(NULL);
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
	vpte_t *ptep;
	vpte_t npte;

	KKASSERT(va >= KvaStart && va < KvaEnd);
	npte = (vpte_t)pa | VPTE_R | VPTE_W | VPTE_V;
	ptep = KernelPTA + (va >> PAGE_SHIFT);
	if (*ptep & VPTE_V)
		pmap_inval_pte(ptep, &kernel_pmap, va);
	*ptep = npte;
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
	madvise((void *)va, PAGE_SIZE, MADV_INVAL);
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
	madvise((void *)va, PAGE_SIZE, MADV_INVAL);
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
pmap_map(vm_offset_t *virtp, vm_paddr_t start, vm_paddr_t end, int prot)
{
	vm_offset_t	sva, virt;

	sva = virt = *virtp;
	while (start < end) {
		pmap_kenter(virt, start);
		virt += PAGE_SIZE;
		start += PAGE_SIZE;
	}
	*virtp = virt;
	return (sva);
}

vpte_t *
pmap_kpte(vm_offset_t va)
{
	vpte_t *ptep;

	KKASSERT(va >= KvaStart && va < KvaEnd);
	ptep = KernelPTA + (va >> PAGE_SHIFT);
	return(ptep);
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
	vpte_t *ptep;
	vpte_t npte;

	KKASSERT(va >= KvaStart && va < KvaEnd);

	npte = (vpte_t)pa | VPTE_R | VPTE_W | VPTE_V;
	ptep = KernelPTA + (va >> PAGE_SHIFT);
	if (*ptep & VPTE_V)
		pmap_inval_pte_quick(ptep, &kernel_pmap, va);
	*ptep = npte;
}

/*
 * Make a temporary mapping for a physical address.  This is only intended
 * to be used for panic dumps.
 *
 * The caller is responsible for calling smp_invltlb().
 */
void *
pmap_kenter_temporary(vm_paddr_t pa, long i)
{
	pmap_kenter_quick(crashdumpmap + (i * PAGE_SIZE), pa);
	return ((void *)crashdumpmap);
}

/*
 * Remove an unmanaged mapping created with pmap_kenter*().
 */
void
pmap_kremove(vm_offset_t va)
{
	vpte_t *ptep;

	KKASSERT(va >= KvaStart && va < KvaEnd);

	ptep = KernelPTA + (va >> PAGE_SHIFT);
	if (*ptep & VPTE_V)
		pmap_inval_pte(ptep, &kernel_pmap, va);
	*ptep = 0;
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
	vpte_t *ptep;

	KKASSERT(va >= KvaStart && va < KvaEnd);

	ptep = KernelPTA + (va >> PAGE_SHIFT);
	if (*ptep & VPTE_V)
		pmap_inval_pte(ptep, &kernel_pmap, va); /* NOT _quick */
	*ptep = 0;
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

	ptep = KernelPTA + (va >> PAGE_SHIFT);
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

		ptep = KernelPTA + (va >> PAGE_SHIFT);
		if (*ptep & VPTE_V)
			pmap_inval_pte(ptep, &kernel_pmap, va);
		*ptep = (vpte_t)(*m)->phys_addr | VPTE_R | VPTE_W | VPTE_V;
		--count;
		++m;
		va += PAGE_SIZE;
	}
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

		ptep = KernelPTA + (va >> PAGE_SHIFT);
		if (*ptep & VPTE_V)
			pmap_inval_pte(ptep, &kernel_pmap, va);
		*ptep = 0;
		--count;
		va += PAGE_SIZE;
	}
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
 * This routine directly affects the fork perf for a process.
 */
void
pmap_init_proc(struct proc *p)
{
}

/*
 * We pre-allocate all page table pages for kernel virtual memory so
 * this routine will only be called if KVM has been exhausted.
 *
 * No requirements.
 */
void
pmap_growkernel(vm_offset_t kstart, vm_offset_t kend)
{
	vm_offset_t addr;

	addr = (kend + PAGE_SIZE * NPTEPG) & ~(PAGE_SIZE * NPTEPG - 1);

	lwkt_gettoken(&vm_token);
	if (addr > virtual_end - SEG_SIZE)
		panic("KVM exhausted");
	kernel_vm_end = addr;
	lwkt_reltoken(&vm_token);
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
	if (pmap != &kernel_pmap)
		return 1;
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
			 
	ASSERT_LWKT_TOKEN_HELD(vm_object_token(object));
	m = vm_page_lookup_busy_wait(object, pindex, FALSE, "pplookp");

	return(m);
}

/*
 * This routine unholds page table pages, and if the hold count
 * drops to zero, then it decrements the wire count.
 *
 * We must recheck that this is the last hold reference after busy-sleeping
 * on the page.
 */
static int 
_pmap_unwire_pte_hold(pmap_t pmap, vm_page_t m) 
{
	vm_page_busy_wait(m, FALSE, "pmuwpt");
	KASSERT(m->queue == PQ_NONE,
		("_pmap_unwire_pte_hold: %p->queue != PQ_NONE", m));

	if (m->hold_count == 1) {
		/*
		 * Unmap the page table page.  
		 */
		KKASSERT(pmap->pm_pdir[m->pindex] != 0);
		pmap_inval_pde(&pmap->pm_pdir[m->pindex], pmap, 
				(vm_offset_t)m->pindex << SEG_SHIFT);
		KKASSERT(pmap->pm_stats.resident_count > 0);
		--pmap->pm_stats.resident_count;

		if (pmap->pm_ptphint == m)
			pmap->pm_ptphint = NULL;

		/*
		 * This was our last hold, the page had better be unwired
		 * after we decrement wire_count.
		 *
		 * FUTURE NOTE: shared page directory page could result in
		 * multiple wire counts.
		 */
		vm_page_unhold(m);
		--m->wire_count;
		KKASSERT(m->wire_count == 0);
		atomic_add_int(&vmstats.v_wire_count, -1);
		vm_page_flag_clear(m, PG_MAPPED | PG_WRITEABLE);
		vm_page_flash(m);
		vm_page_free_zero(m);
		return 1;
	}
	KKASSERT(m->hold_count > 1);
	vm_page_unhold(m);
	vm_page_wakeup(m);

	return 0;
}

static __inline int
pmap_unwire_pte_hold(pmap_t pmap, vm_page_t m)
{
	KKASSERT(m->hold_count > 0);
	if (m->hold_count > 1) {
		vm_page_unhold(m);
		return 0;
	} else {
		return _pmap_unwire_pte_hold(pmap, m);
	}
}

/*
 * After removing a page table entry, this routine is used to
 * conditionally free the page, and manage the hold/wire counts.
 */
static int
pmap_unuse_pt(pmap_t pmap, vm_offset_t va, vm_page_t mpte)
{
	unsigned ptepindex;

	ASSERT_LWKT_TOKEN_HELD(vm_object_token(pmap->pm_pteobj));

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
			mpte = pmap_page_lookup(pmap->pm_pteobj, ptepindex);
			pmap->pm_ptphint = mpte;
			vm_page_wakeup(mpte);
		}
	}
	return pmap_unwire_pte_hold(pmap, mpte);
}

/*
 * Attempt to release and free the vm_page backing a page directory page
 * in a pmap.  Returns 1 on success, 0 on failure (if the procedure had
 * to sleep).
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
	if (vm_page_busy_try(p, FALSE)) {
		vm_page_sleep_busy(p, FALSE, "pmaprl");
		return 0;
	}
	KKASSERT(pmap->pm_stats.resident_count > 0);
	--pmap->pm_stats.resident_count;

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
	 *
	 * pmaps for vkernels do not self-map because they do not share
	 * their address space with the vkernel.  Clearing of pde[] thus
	 * only applies to page table pages and not to the page directory
	 * page.
	 */
	if (p->pindex == pmap->pm_pdindex) {
		bzero(pde, VPTE_PAGETABLE_SIZE);
		pmap_kremove((vm_offset_t)pmap->pm_pdir);
	} else {
		KKASSERT(pde[p->pindex] != 0);
		pmap_inval_pde(&pde[p->pindex], pmap, 
				(vm_offset_t)p->pindex << SEG_SHIFT);
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
	atomic_add_int(&vmstats.v_wire_count, -1);
	vm_page_free_zero(p);
	return 1;
}

/*
 * This routine is called if the page table page is not mapped in the page
 * table directory.
 *
 * The routine is broken up into two parts for readability.
 *
 * It must return a held mpte and map the page directory page as required.
 * Because vm_page_grab() can block, we must re-check pm_pdir[ptepindex]
 */
static vm_page_t
_pmap_allocpte(pmap_t pmap, unsigned ptepindex)
{
	vm_paddr_t ptepa;
	vm_page_t m;

	/*
	 * Find or fabricate a new pagetable page.  A busied page will be
	 * returned.  This call may block.
	 */
	m = vm_page_grab(pmap->pm_pteobj, ptepindex,
			 VM_ALLOC_NORMAL | VM_ALLOC_ZERO | VM_ALLOC_RETRY);
	vm_page_flag_set(m, PG_MAPPED);

	KASSERT(m->queue == PQ_NONE,
		("_pmap_allocpte: %p->queue != PQ_NONE", m));

	/*
	 * Increment the hold count for the page we will be returning to
	 * the caller.
	 */
	m->hold_count++;

	/*
	 * It is possible that someone else got in and mapped by the page
	 * directory page while we were blocked, if so just unbusy and
	 * return the held page.
	 */
	if ((ptepa = pmap->pm_pdir[ptepindex]) != 0) {
		KKASSERT((ptepa & VPTE_FRAME) == VM_PAGE_TO_PHYS(m));
		vm_page_wakeup(m);
		return(m);
	}
	vm_page_wire(m);

	/*
	 * Map the pagetable page into the process address space, if
	 * it isn't already there.
	 */
	++pmap->pm_stats.resident_count;

	ptepa = VM_PAGE_TO_PHYS(m);
	pmap->pm_pdir[ptepindex] = (vpte_t)ptepa | VPTE_R | VPTE_W | VPTE_V |
				   VPTE_A | VPTE_M;

	/*
	 * We are likely about to access this page table page, so set the
	 * page table hint to reduce overhead.
	 */
	pmap->pm_ptphint = m;

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

	ASSERT_LWKT_TOKEN_HELD(vm_object_token(pmap->pm_pteobj));

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
		KKASSERT(pmap->pm_pdir[ptepindex] != 0);
		pmap_inval_pde(&pmap->pm_pdir[ptepindex], pmap,
			       (vm_offset_t)ptepindex << SEG_SHIFT);
		ptepa = 0;
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
			m = pmap_page_lookup(pmap->pm_pteobj, ptepindex);
			pmap->pm_ptphint = m;
			vm_page_wakeup(m);
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
 *
 * No requirements.
 */
void
pmap_collect(void)
{
	int i;
	vm_page_t m;
	static int warningdone=0;

	if (pmap_pagedaemon_waken == 0)
		return;
	lwkt_gettoken(&vm_token);
	pmap_pagedaemon_waken = 0;

	if (warningdone < 5) {
		kprintf("pmap_collect: collecting pv entries -- suggest increasing PMAP_SHPGPERPROC\n");
		warningdone++;
	}

	for (i = 0; i < vm_page_array_size; i++) {
		m = &vm_page_array[i];
		if (m->wire_count || m->hold_count)
			continue;
		if (vm_page_busy_try(m, TRUE) == 0) {
			if (m->wire_count == 0 && m->hold_count == 0) {
				pmap_remove_all(m);
			}
			vm_page_wakeup(m);
		}
	}
	lwkt_reltoken(&vm_token);
}
	
/*
 * If it is the first entry on the list, it is actually
 * in the header and we must copy the following entry up
 * to the header.  Otherwise we must search the list for
 * the entry.  In either case we free the now unused entry.
 *
 * caller must hold vm_token
 */
static int
pmap_remove_entry(struct pmap *pmap, vm_page_t m, vm_offset_t va)
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

	TAILQ_REMOVE(&m->md.pv_list, pv, pv_list);
	m->md.pv_list_count--;
	atomic_add_int(&m->object->agg_pv_list_count, -1);
	TAILQ_REMOVE(&pmap->pm_pvlist, pv, pv_plist);
	if (TAILQ_EMPTY(&m->md.pv_list))
		vm_page_flag_clear(m, PG_MAPPED | PG_WRITEABLE);
	++pmap->pm_generation;
	vm_object_hold(pmap->pm_pteobj);
	rtval = pmap_unuse_pt(pmap, va, pv->pv_ptem);
	vm_object_drop(pmap->pm_pteobj);
	free_pv_entry(pv);

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
	++pmap->pm_generation;
	m->md.pv_list_count++;
	atomic_add_int(&m->object->agg_pv_list_count, 1);

	crit_exit();
}

/*
 * pmap_remove_pte: do the things to unmap a page in a process
 */
static int
pmap_remove_pte(struct pmap *pmap, vpte_t *ptq, vm_offset_t va)
{
	vpte_t oldpte;
	vm_page_t m;

	oldpte = pmap_inval_loadandclear(ptq, pmap, va);
	if (oldpte & VPTE_WIRED)
		--pmap->pm_stats.wired_count;
	KKASSERT(pmap->pm_stats.wired_count >= 0);

#if 0
	/*
	 * Machines that don't support invlpg, also don't support
	 * VPTE_G.  XXX VPTE_G is disabled for SMP so don't worry about
	 * the SMP case.
	 */
	if (oldpte & VPTE_G)
		madvise((void *)va, PAGE_SIZE, MADV_INVAL);
#endif
	KKASSERT(pmap->pm_stats.resident_count > 0);
	--pmap->pm_stats.resident_count;
	if (oldpte & VPTE_MANAGED) {
		m = PHYS_TO_VM_PAGE(oldpte);
		if (oldpte & VPTE_M) {
#if defined(PMAP_DIAGNOSTIC)
			if (pmap_nw_modified((pt_entry_t) oldpte)) {
				kprintf(
	"pmap_remove: modified page not writable: va: 0x%x, pte: 0x%x\n",
				    va, oldpte);
			}
#endif
			if (pmap_track_modified(pmap, va))
				vm_page_dirty(m);
		}
		if (oldpte & VPTE_A)
			vm_page_flag_set(m, PG_REFERENCED);
		return pmap_remove_entry(pmap, m, va);
	} else {
		return pmap_unuse_pt(pmap, va, NULL);
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
pmap_remove_page(struct pmap *pmap, vm_offset_t va)
{
	vpte_t *ptq;

	/*
	 * if there is no pte for this address, just skip it!!!  Otherwise
	 * get a local va for mappings for this pmap and remove the entry.
	 */
	if (*pmap_pde(pmap, va) != 0) {
		ptq = get_ptbase(pmap, va);
		if (*ptq) {
			pmap_remove_pte(pmap, ptq, va);
		}
	}
}

/*
 * Remove the given range of addresses from the specified map.
 *
 * It is assumed that the start and end are properly rounded to the
 * page size.
 *
 * This function may not be called from an interrupt if the pmap is
 * not kernel_pmap.
 *
 * No requirements.
 */
void
pmap_remove(struct pmap *pmap, vm_offset_t sva, vm_offset_t eva)
{
	vpte_t *ptbase;
	vm_offset_t pdnxt;
	vm_offset_t ptpaddr;
	vm_pindex_t sindex, eindex;

	if (pmap == NULL)
		return;

	vm_object_hold(pmap->pm_pteobj);
	lwkt_gettoken(&vm_token);
 	KKASSERT(pmap->pm_stats.resident_count >= 0);
	if (pmap->pm_stats.resident_count == 0) {
		lwkt_reltoken(&vm_token);
		vm_object_drop(pmap->pm_pteobj);
		return;
	}

	/*
	 * special handling of removing one page.  a very
	 * common operation and easy to short circuit some
	 * code.
	 */
	if (((sva + PAGE_SIZE) == eva) && 
		((pmap->pm_pdir[(sva >> PDRSHIFT)] & VPTE_PS) == 0)) {
		pmap_remove_page(pmap, sva);
		lwkt_reltoken(&vm_token);
		vm_object_drop(pmap->pm_pteobj);
		return;
	}

	/*
	 * Get a local virtual address for the mappings that are being
	 * worked with.
	 *
	 * XXX this is really messy because the kernel pmap is not relative
	 * to address 0
	 */
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
			KKASSERT(pmap->pm_pdir[pdirindex] != 0);
			pmap->pm_stats.resident_count -= NBPDR / PAGE_SIZE;
			pmap_inval_pde(&pmap->pm_pdir[pdirindex], pmap,
				(vm_offset_t)pdirindex << SEG_SHIFT);
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
		if (pdnxt > eindex)
			pdnxt = eindex;

		/*
		 * NOTE: pmap_remove_pte() can block.
		 */
		for (; sindex != pdnxt; sindex++) {
			vm_offset_t va;

			ptbase = get_ptbase(pmap, sindex << PAGE_SHIFT);
			if (*ptbase == 0)
				continue;
			va = i386_ptob(sindex);
			if (pmap_remove_pte(pmap, ptbase, va))
				break;
		}
	}
	lwkt_reltoken(&vm_token);
	vm_object_drop(pmap->pm_pteobj);
}

/*
 * Removes this physical page from all physical maps in which it resides.
 * Reflects back modify bits to the pager.
 *
 * This routine may not be called from an interrupt.
 *
 * No requirements.
 */
static void
pmap_remove_all(vm_page_t m)
{
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

	lwkt_gettoken(&vm_token);
	while ((pv = TAILQ_FIRST(&m->md.pv_list)) != NULL) {
		KKASSERT(pv->pv_pmap->pm_stats.resident_count > 0);
		--pv->pv_pmap->pm_stats.resident_count;

		pte = pmap_pte(pv->pv_pmap, pv->pv_va);
		KKASSERT(pte != NULL);

		tpte = pmap_inval_loadandclear(pte, pv->pv_pmap, pv->pv_va);
		if (tpte & VPTE_WIRED)
			--pv->pv_pmap->pm_stats.wired_count;
		KKASSERT(pv->pv_pmap->pm_stats.wired_count >= 0);

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
			if (pmap_track_modified(pv->pv_pmap, pv->pv_va))
				vm_page_dirty(m);
		}
		TAILQ_REMOVE(&m->md.pv_list, pv, pv_list);
		TAILQ_REMOVE(&pv->pv_pmap->pm_pvlist, pv, pv_plist);
		++pv->pv_pmap->pm_generation;
		m->md.pv_list_count--;
		atomic_add_int(&m->object->agg_pv_list_count, -1);
		if (TAILQ_EMPTY(&m->md.pv_list))
			vm_page_flag_clear(m, PG_MAPPED | PG_WRITEABLE);
		vm_object_hold(pv->pv_pmap->pm_pteobj);
		pmap_unuse_pt(pv->pv_pmap, pv->pv_va, pv->pv_ptem);
		vm_object_drop(pv->pv_pmap->pm_pteobj);
		free_pv_entry(pv);
	}
	KKASSERT((m->flags & (PG_MAPPED | PG_WRITEABLE)) == 0);
	lwkt_reltoken(&vm_token);
}

/*
 * Set the physical protection on the specified range of this map
 * as requested.
 *
 * This function may not be called from an interrupt if the map is
 * not the kernel_pmap.
 *
 * No requirements.
 */
void
pmap_protect(pmap_t pmap, vm_offset_t sva, vm_offset_t eva, vm_prot_t prot)
{
	vpte_t *ptbase;
	vpte_t *ptep;
	vm_offset_t pdnxt, ptpaddr;
	vm_pindex_t sindex, eindex;
	vm_pindex_t sbase;

	if (pmap == NULL)
		return;

	if ((prot & VM_PROT_READ) == VM_PROT_NONE) {
		pmap_remove(pmap, sva, eva);
		return;
	}

	if (prot & VM_PROT_WRITE)
		return;

	lwkt_gettoken(&vm_token);
	ptbase = get_ptbase(pmap, sva);

	sindex = (sva >> PAGE_SHIFT);
	eindex = (eva >> PAGE_SHIFT);
	sbase = sindex;

	for (; sindex < eindex; sindex = pdnxt) {

		unsigned pdirindex;

		pdnxt = ((sindex + NPTEPG) & ~(NPTEPG - 1));

		pdirindex = sindex / NPDEPG;

		/*
		 * Clear the modified and writable bits for a 4m page.
		 * Throw away the modified bit (?)
		 */
		if (((ptpaddr = pmap->pm_pdir[pdirindex]) & VPTE_PS) != 0) {
			pmap_clean_pde(&pmap->pm_pdir[pdirindex], pmap,
					(vm_offset_t)pdirindex << SEG_SHIFT);
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
			vpte_t pbits;
			vm_page_t m;

			/*
			 * Clean managed pages and also check the accessed
			 * bit.  Just remove write perms for unmanaged
			 * pages.  Be careful of races, turning off write
			 * access will force a fault rather then setting
			 * the modified bit at an unexpected time.
			 */
			ptep = &ptbase[sindex - sbase];
			if (*ptep & VPTE_MANAGED) {
				pbits = pmap_clean_pte(ptep, pmap,
						       i386_ptob(sindex));
				m = NULL;
				if (pbits & VPTE_A) {
					m = PHYS_TO_VM_PAGE(pbits);
					vm_page_flag_set(m, PG_REFERENCED);
					atomic_clear_long(ptep, VPTE_A);
				}
				if (pbits & VPTE_M) {
					if (pmap_track_modified(pmap, i386_ptob(sindex))) {
						if (m == NULL)
							m = PHYS_TO_VM_PAGE(pbits);
						vm_page_dirty(m);
					}
				}
			} else {
				pbits = pmap_setro_pte(ptep, pmap,
						       i386_ptob(sindex));
			}
		}
	}
	lwkt_reltoken(&vm_token);
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
 *
 * No requirements.
 */
void
pmap_enter(pmap_t pmap, vm_offset_t va, vm_page_t m, vm_prot_t prot,
	   boolean_t wired, vm_map_entry_t entry __unused)
{
	vm_paddr_t pa;
	vpte_t *pte;
	vm_paddr_t opa;
	vpte_t origpte, newpte;
	vm_page_t mpte;

	if (pmap == NULL)
		return;

	va &= VPTE_FRAME;

	vm_object_hold(pmap->pm_pteobj);
	lwkt_gettoken(&vm_token);

	/*
	 * Get the page table page.   The kernel_pmap's page table pages
	 * are preallocated and have no associated vm_page_t.
	 */
	if (pmap == &kernel_pmap)
		mpte = NULL;
	else
		mpte = pmap_allocpte(pmap, va);

	pte = pmap_pte(pmap, va);

	/*
	 * Page Directory table entry not valid, we need a new PT page
	 * and pmap_allocpte() didn't give us one.  Oops!
	 */
	if (pte == NULL) {
		panic("pmap_enter: invalid page directory pmap=%p, va=0x%p",
		      pmap, (void *)va);
	}

	/*
	 * Deal with races on the original mapping (though don't worry
	 * about VPTE_A races) by cleaning it.  This will force a fault
	 * if an attempt is made to write to the page.
	 */
	pa = VM_PAGE_TO_PHYS(m) & VPTE_FRAME;
	origpte = pmap_clean_pte(pte, pmap, va);
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
		if (wired && ((origpte & VPTE_WIRED) == 0))
			++pmap->pm_stats.wired_count;
		else if (!wired && (origpte & VPTE_WIRED))
			--pmap->pm_stats.wired_count;
		KKASSERT(pmap->pm_stats.wired_count >= 0);

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
		if (origpte & VPTE_MANAGED) {
			if ((origpte & VPTE_M) &&
			    pmap_track_modified(pmap, va)) {
				vm_page_t om;
				om = PHYS_TO_VM_PAGE(opa);
				vm_page_dirty(om);
			}
			pa |= VPTE_MANAGED;
			KKASSERT(m->flags & PG_MAPPED);
		}
		goto validate;
	} 
	/*
	 * Mapping has changed, invalidate old range and fall through to
	 * handle validating new mapping.
	 */
	while (opa) {
		int err;
		err = pmap_remove_pte(pmap, pte, va);
		if (err)
			panic("pmap_enter: pte vanished, va: %p", (void *)va);
		pte = pmap_pte(pmap, va);
		origpte = pmap_clean_pte(pte, pmap, va);
		opa = origpte & VPTE_FRAME;
		if (opa) {
			kprintf("pmap_enter: Warning, raced pmap %p va %p\n",
				pmap, (void *)va);
		}
	}

	/*
	 * Enter on the PV list if part of our managed memory. Note that we
	 * raise IPL while manipulating pv_table since pmap_enter can be
	 * called at interrupt time.
	 */
	if (pmap_initialized && 
	    (m->flags & (PG_FICTITIOUS|PG_UNMANAGED)) == 0) {
		pmap_insert_entry(pmap, va, mpte, m);
		pa |= VPTE_MANAGED;
		vm_page_flag_set(m, PG_MAPPED);
	}

	/*
	 * Increment counters
	 */
	++pmap->pm_stats.resident_count;
	if (wired)
		pmap->pm_stats.wired_count++;

validate:
	/*
	 * Now validate mapping with desired protection/wiring.
	 */
	newpte = (vm_offset_t) (pa | pte_prot(pmap, prot) | VPTE_V);

	if (wired)
		newpte |= VPTE_WIRED;
	if (pmap != &kernel_pmap)
		newpte |= VPTE_U;

	/*
	 * If the mapping or permission bits are different from the
	 * (now cleaned) original pte, an update is needed.  We've
	 * already downgraded or invalidated the page so all we have
	 * to do now is update the bits.
	 *
	 * XXX should we synchronize RO->RW changes to avoid another
	 * fault?
	 */
	if ((origpte & ~(VPTE_W|VPTE_M|VPTE_A)) != newpte) {
		*pte = newpte | VPTE_A;
		if (newpte & VPTE_W)
			vm_page_flag_set(m, PG_WRITEABLE);
	}
	KKASSERT((newpte & VPTE_MANAGED) == 0 || m->flags & PG_MAPPED);
	lwkt_reltoken(&vm_token);
	vm_object_drop(pmap->pm_pteobj);
}

/*
 * This code works like pmap_enter() but assumes VM_PROT_READ and not-wired.
 *
 * Currently this routine may only be used on user pmaps, not kernel_pmap.
 */
void
pmap_enter_quick(pmap_t pmap, vm_offset_t va, vm_page_t m)
{
	vpte_t *pte;
	vm_paddr_t pa;
	vm_page_t mpte;
	unsigned ptepindex;
	vm_offset_t ptepa;

	KKASSERT(pmap != &kernel_pmap);

	KKASSERT(va >= VM_MIN_USER_ADDRESS && va < VM_MAX_USER_ADDRESS);

	/*
	 * Calculate pagetable page (mpte), allocating it if necessary.
	 *
	 * A held page table page (mpte), or NULL, is passed onto the 
	 * section following.
	 */
	ptepindex = va >> PDRSHIFT;

	vm_object_hold(pmap->pm_pteobj);
	lwkt_gettoken(&vm_token);

	do {
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
				vm_page_wakeup(mpte);
			}
			if (mpte)
				mpte->hold_count++;
		} else {
			mpte = _pmap_allocpte(pmap, ptepindex);
		}
	} while (mpte == NULL);

	/*
	 * Ok, now that the page table page has been validated, get the pte.
	 * If the pte is already mapped undo mpte's hold_count and
	 * just return.
	 */
	pte = pmap_pte(pmap, va);
	if (*pte) {
		pmap_unwire_pte_hold(pmap, mpte);
		lwkt_reltoken(&vm_token);
		vm_object_drop(pmap->pm_pteobj);
		return;
	}

	/*
	 * Enter on the PV list if part of our managed memory. Note that we
	 * raise IPL while manipulating pv_table since pmap_enter can be
	 * called at interrupt time.
	 */
	if ((m->flags & (PG_FICTITIOUS|PG_UNMANAGED)) == 0) {
		pmap_insert_entry(pmap, va, mpte, m);
		vm_page_flag_set(m, PG_MAPPED);
	}

	/*
	 * Increment counters
	 */
	++pmap->pm_stats.resident_count;

	pa = VM_PAGE_TO_PHYS(m);

	/*
	 * Now validate mapping with RO protection
	 */
	if (m->flags & (PG_FICTITIOUS|PG_UNMANAGED))
		*pte = (vpte_t)pa | VPTE_V | VPTE_U;
	else
		*pte = (vpte_t)pa | VPTE_V | VPTE_U | VPTE_MANAGED;
	/*pmap_inval_add(&info, pmap, va); shouldn't be needed 0->valid */
	/*pmap_inval_flush(&info); don't need for vkernel */
	lwkt_reltoken(&vm_token);
	vm_object_drop(pmap->pm_pteobj);
}

/*
 * Extract the physical address for the translation at the specified
 * virtual address in the pmap.
 *
 * The caller must hold vm_token if non-blocking operation is desired.
 * No requirements.
 */
vm_paddr_t
pmap_extract(pmap_t pmap, vm_offset_t va)
{
	vm_paddr_t rtval;
	vpte_t pte;

	lwkt_gettoken(&vm_token);
	if (pmap && (pte = pmap->pm_pdir[va >> SEG_SHIFT]) != 0) {
		if (pte & VPTE_PS) {
			rtval = pte & ~((vpte_t)(1 << SEG_SHIFT) - 1);
			rtval |= va & SEG_MASK;
		} else {
			pte = *get_ptbase(pmap, va);
			rtval = (pte & VPTE_FRAME) | (va & PAGE_MASK);
		}
	} else {
		rtval = 0;
	}
	lwkt_reltoken(&vm_token);
	return(rtval);
}

#define MAX_INIT_PT (96)

/*
 * This routine preloads the ptes for a given object into the specified pmap.
 * This eliminates the blast of soft faults on process startup and
 * immediately after an mmap.
 *
 * No requirements.
 */
static int pmap_object_init_pt_callback(vm_page_t p, void *data);

void
pmap_object_init_pt(pmap_t pmap, vm_offset_t addr, vm_prot_t prot,
		    vm_object_t object, vm_pindex_t pindex, 
		    vm_size_t size, int limit)
{
	struct rb_vm_page_scan_info info;
	struct lwp *lp;
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
	lp = curthread->td_lwp;
	if (lp == NULL || pmap != vmspace_pmap(lp->lwp_vmspace))
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

	vm_object_hold_shared(object);
	vm_page_rb_tree_RB_SCAN(&object->rb_memq, rb_vm_page_scancmp,
				pmap_object_init_pt_callback, &info);
	vm_object_drop(object);
}

/*
 * The caller must hold vm_token.
 */
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

	/*
	 * Ignore list markers and ignore pages we cannot instantly
	 * busy (while holding the object token).
	 */
	if (p->flags & PG_MARKER)
		return 0;
	if (vm_page_busy_try(p, TRUE))
		return 0;
	if (((p->valid & VM_PAGE_BITS_ALL) == VM_PAGE_BITS_ALL) &&
	    (p->flags & PG_FICTITIOUS) == 0) {
		if ((p->queue - p->pc) == PQ_CACHE)
			vm_page_deactivate(p);
		rel_index = p->pindex - info->start_pindex;
		pmap_enter_quick(info->pmap,
				 info->addr + i386_ptob(rel_index), p);
	}
	vm_page_wakeup(p);
	return(0);
}

/*
 * Return TRUE if the pmap is in shape to trivially
 * pre-fault the specified address.
 *
 * Returns FALSE if it would be non-trivial or if a
 * pte is already loaded into the slot.
 *
 * No requirements.
 */
int
pmap_prefault_ok(pmap_t pmap, vm_offset_t addr)
{
	vpte_t *pte;
	int ret;

	lwkt_gettoken(&vm_token);
	if ((*pmap_pde(pmap, addr)) == 0) {
		ret = 0;
	} else {
		pte = get_ptbase(pmap, addr);
		ret = (*pte) ? 0 : 1;
	}
	lwkt_reltoken(&vm_token);
	return (ret);
}

/*
 * Change the wiring attribute for a map/virtual-address pair.
 * The mapping must already exist in the pmap.
 *
 * No other requirements.
 */
void
pmap_change_wiring(pmap_t pmap, vm_offset_t va, boolean_t wired,
		   vm_map_entry_t entry __unused)
{
	vpte_t *pte;

	if (pmap == NULL)
		return;

	lwkt_gettoken(&vm_token);
	pte = get_ptbase(pmap, va);

	if (wired && (*pte & VPTE_WIRED) == 0)
		++pmap->pm_stats.wired_count;
	else if (!wired && (*pte & VPTE_WIRED))
		--pmap->pm_stats.wired_count;
	KKASSERT(pmap->pm_stats.wired_count >= 0);

	/*
	 * Wiring is not a hardware characteristic so there is no need to
	 * invalidate TLB.  However, in an SMP environment we must use
	 * a locked bus cycle to update the pte (if we are not using 
	 * the pmap_inval_*() API that is)... it's ok to do this for simple
	 * wiring changes.
	 */
	if (wired)
		atomic_set_long(pte, VPTE_WIRED);
	else
		atomic_clear_long(pte, VPTE_WIRED);
	lwkt_reltoken(&vm_token);
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
	vm_offset_t addr;
	vm_offset_t end_addr = src_addr + len;
	vm_offset_t pdnxt;
	vpte_t *src_frame;
	vpte_t *dst_frame;
	vm_page_t m;

        /*
         * XXX BUGGY.  Amoung other things srcmpte is assumed to remain
         * valid through blocking calls, and that's just not going to
         * be the case.
         *
         * FIXME!
         */
	return;

	if (dst_addr != src_addr)
		return;
	if (dst_pmap->pm_pdir == NULL)
		return;
	if (src_pmap->pm_pdir == NULL)
		return;

	lwkt_gettoken(&vm_token);

	src_frame = get_ptbase1(src_pmap, src_addr);
	dst_frame = get_ptbase2(dst_pmap, src_addr);

	/*
	 * critical section protection is required to maintain the page/object
	 * association, interrupts can free pages and remove them from 
	 * their objects.
	 */
	for (addr = src_addr; addr < end_addr; addr = pdnxt) {
		vpte_t *src_pte, *dst_pte;
		vm_page_t dstmpte, srcmpte;
		vm_offset_t srcptepaddr;
		unsigned ptepindex;

		if (addr >= VM_MAX_USER_ADDRESS)
			panic("pmap_copy: invalid to pmap_copy page tables");

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
				dst_pmap->pm_pdir[ptepindex] = (vpte_t)srcptepaddr;
				dst_pmap->pm_stats.resident_count += NBPDR / PAGE_SIZE;
			}
			continue;
		}

		srcmpte = vm_page_lookup(src_pmap->pm_pteobj, ptepindex);
		if ((srcmpte == NULL) || (srcmpte->hold_count == 0) ||
		    (srcmpte->flags & PG_BUSY)) {
			continue;
		}

		if (pdnxt > end_addr)
			pdnxt = end_addr;

		src_pte = src_frame + ((addr - src_addr) >> PAGE_SHIFT);
		dst_pte = dst_frame + ((addr - src_addr) >> PAGE_SHIFT);
		while (addr < pdnxt) {
			vpte_t ptetemp;

			ptetemp = *src_pte;
			/*
			 * we only virtual copy managed pages
			 */
			if ((ptetemp & VPTE_MANAGED) != 0) {
				/*
				 * We have to check after allocpte for the
				 * pte still being around...  allocpte can
				 * block.
				 *
				 * pmap_allocpte can block, unfortunately
				 * we have to reload the tables.
				 */
				dstmpte = pmap_allocpte(dst_pmap, addr);
				src_frame = get_ptbase1(src_pmap, src_addr);
				dst_frame = get_ptbase2(dst_pmap, src_addr);

				if ((*dst_pte == 0) && (ptetemp = *src_pte) &&
				    (ptetemp & VPTE_MANAGED) != 0) {
					/*
					 * Clear the modified and accessed
					 * (referenced) bits during the copy.
					 *
					 * We do not have to clear the write
					 * bit to force a fault-on-modify
					 * because the real kernel's target
					 * pmap is empty and will fault anyway.
					 */
					m = PHYS_TO_VM_PAGE(ptetemp);
					*dst_pte = ptetemp & ~(VPTE_M | VPTE_A);
					++dst_pmap->pm_stats.resident_count;
					pmap_insert_entry(dst_pmap, addr,
						dstmpte, m);
					KKASSERT(m->flags & PG_MAPPED);
	 			} else {
					pmap_unwire_pte_hold(dst_pmap, dstmpte);
				}
				if (dstmpte->hold_count >= srcmpte->hold_count)
					break;
			}
			addr += PAGE_SIZE;
			src_pte++;
			dst_pte++;
		}
	}
	lwkt_reltoken(&vm_token);
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
	if (*gd->gd_CMAP3)
		panic("pmap_zero_page: CMAP3 busy");
	*gd->gd_CMAP3 = VPTE_V | VPTE_R | VPTE_W | (phys & VPTE_FRAME) | VPTE_A | VPTE_M;
	madvise(gd->gd_CADDR3, PAGE_SIZE, MADV_INVAL);

	bzero(gd->gd_CADDR3, PAGE_SIZE);
	*gd->gd_CMAP3 = 0;
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
	if (*gd->gd_CMAP3)
		panic("pmap_zero_page: CMAP3 busy");
	*gd->gd_CMAP3 = VPTE_V | VPTE_R | VPTE_W |
			(phys & VPTE_FRAME) | VPTE_A | VPTE_M;
	madvise(gd->gd_CADDR3, PAGE_SIZE, MADV_INVAL);
	for (i = 0; i < PAGE_SIZE; i += 4) {
	    if (*(int *)((char *)gd->gd_CADDR3 + i) != 0) {
		panic("pmap_page_assertzero() @ %p not zero!",
		    (void *)gd->gd_CADDR3);
	    }
	}
	*gd->gd_CMAP3 = 0;
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
	if (*gd->gd_CMAP3)
		panic("pmap_zero_page: CMAP3 busy");
	*gd->gd_CMAP3 = VPTE_V | VPTE_R | VPTE_W |
			(phys & VPTE_FRAME) | VPTE_A | VPTE_M;
	madvise(gd->gd_CADDR3, PAGE_SIZE, MADV_INVAL);

	bzero((char *)gd->gd_CADDR3 + off, size);
	*gd->gd_CMAP3 = 0;
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

	*(int *) gd->gd_CMAP1 = VPTE_V | VPTE_R | (src & PG_FRAME) | VPTE_A;
	*(int *) gd->gd_CMAP2 = VPTE_V | VPTE_R | VPTE_W | (dst & VPTE_FRAME) | VPTE_A | VPTE_M;

	madvise(gd->gd_CADDR1, PAGE_SIZE, MADV_INVAL);
	madvise(gd->gd_CADDR2, PAGE_SIZE, MADV_INVAL);

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

	madvise(gd->gd_CADDR1, PAGE_SIZE, MADV_INVAL);
	madvise(gd->gd_CADDR2, PAGE_SIZE, MADV_INVAL);

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
 *
 * No requirements.
 */
boolean_t
pmap_page_exists_quick(pmap_t pmap, vm_page_t m)
{
	pv_entry_t pv;
	int loops = 0;

	if (!pmap_initialized || (m->flags & PG_FICTITIOUS))
		return FALSE;

	crit_enter();
	lwkt_gettoken(&vm_token);

	TAILQ_FOREACH(pv, &m->md.pv_list, pv_list) {
		if (pv->pv_pmap == pmap) {
			lwkt_reltoken(&vm_token);
			crit_exit();
			return TRUE;
		}
		loops++;
		if (loops >= 16)
			break;
	}
	lwkt_reltoken(&vm_token);
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
 *
 * No requirements.
 */
void
pmap_remove_pages(pmap_t pmap, vm_offset_t sva, vm_offset_t eva)
{
	vpte_t *pte, tpte;
	pv_entry_t pv, npv;
	vm_page_t m;
	int32_t save_generation;

	if (pmap->pm_pteobj)
		vm_object_hold(pmap->pm_pteobj);
	lwkt_gettoken(&vm_token);
	for (pv = TAILQ_FIRST(&pmap->pm_pvlist); pv; pv = npv) {
		if (pv->pv_va >= eva || pv->pv_va < sva) {
			npv = TAILQ_NEXT(pv, pv_plist);
			continue;
		}

		KKASSERT(pmap == pv->pv_pmap);

		pte = pmap_pte(pmap, pv->pv_va);

		/*
		 * We cannot remove wired pages from a process' mapping
		 * at this time
		 */
		if (*pte & VPTE_WIRED) {
			npv = TAILQ_NEXT(pv, pv_plist);
			continue;
		}
		tpte = pmap_inval_loadandclear(pte, pmap, pv->pv_va);

		m = PHYS_TO_VM_PAGE(tpte);

		KASSERT(m < &vm_page_array[vm_page_array_size],
			("pmap_remove_pages: bad tpte %lx", tpte));

		KKASSERT(pmap->pm_stats.resident_count > 0);
		--pmap->pm_stats.resident_count;

		/*
		 * Update the vm_page_t clean and reference bits.
		 */
		if (tpte & VPTE_M) {
			vm_page_dirty(m);
		}

		npv = TAILQ_NEXT(pv, pv_plist);
		TAILQ_REMOVE(&pmap->pm_pvlist, pv, pv_plist);
		save_generation = ++pmap->pm_generation;

		m->md.pv_list_count--;
		atomic_add_int(&m->object->agg_pv_list_count, -1);
		TAILQ_REMOVE(&m->md.pv_list, pv, pv_list);
		if (TAILQ_FIRST(&m->md.pv_list) == NULL)
			vm_page_flag_clear(m, PG_MAPPED | PG_WRITEABLE);

		pmap_unuse_pt(pmap, pv->pv_va, pv->pv_ptem);
		free_pv_entry(pv);

		/*
		 * Restart the scan if we blocked during the unuse or free
		 * calls and other removals were made.
		 */
		if (save_generation != pmap->pm_generation) {
			kprintf("Warning: pmap_remove_pages race-A avoided\n");
			npv = TAILQ_FIRST(&pmap->pm_pvlist);
		}
	}
	lwkt_reltoken(&vm_token);
	if (pmap->pm_pteobj)
		vm_object_drop(pmap->pm_pteobj);
}

/*
 * pmap_testbit tests bits in active mappings of a VM page.
 *
 * The caller must hold vm_token
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
			if (!pmap_track_modified(pv->pv_pmap, pv->pv_va))
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
 * This routine is used to clear bits in ptes.  Certain bits require special
 * handling, in particular (on virtual kernels) the VPTE_M (modify) bit.
 *
 * This routine is only called with certain VPTE_* bit combinations.
 *
 * The caller must hold vm_token
 */
static __inline void
pmap_clearbit(vm_page_t m, int bit)
{
	pv_entry_t pv;
	vpte_t *pte;
	vpte_t pbits;

	if (!pmap_initialized || (m->flags & PG_FICTITIOUS))
		return;

	crit_enter();

	/*
	 * Loop over all current mappings setting/clearing as appropos If
	 * setting RO do we need to clear the VAC?
	 */
	TAILQ_FOREACH(pv, &m->md.pv_list, pv_list) {
		/*
		 * don't write protect pager mappings
		 */
		if (bit == VPTE_W) {
			if (!pmap_track_modified(pv->pv_pmap, pv->pv_va))
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
		 *
		 * On virtual kernels we must force a new fault-on-write
		 * in the real kernel if we clear the Modify bit ourselves,
		 * otherwise the real kernel will not get a new fault and
		 * will never set our Modify bit again. 
		 */
		pte = pmap_pte(pv->pv_pmap, pv->pv_va);
		if (*pte & bit) {
			if (bit == VPTE_W) {
				/*
				 * We must also clear VPTE_M when clearing
				 * VPTE_W
				 */
				pbits = pmap_clean_pte(pte, pv->pv_pmap,
						       pv->pv_va);
				if (pbits & VPTE_M)
					vm_page_dirty(m);
			} else if (bit == VPTE_M) {
				/*
				 * We do not have to make the page read-only
				 * when clearing the Modify bit.  The real
				 * kernel will make the real PTE read-only
				 * or otherwise detect the write and set
				 * our VPTE_M again simply by us invalidating
				 * the real kernel VA for the pmap (as we did
				 * above).  This allows the real kernel to
				 * handle the write fault without forwarding
				 * the fault to us.
				 */
				atomic_clear_long(pte, VPTE_M);
			} else if ((bit & (VPTE_W|VPTE_M)) == (VPTE_W|VPTE_M)) {
				/*
				 * We've been asked to clear W & M, I guess
				 * the caller doesn't want us to update
				 * the dirty status of the VM page.
				 */
				pmap_clean_pte(pte, pv->pv_pmap, pv->pv_va);
			} else {
				/*
				 * We've been asked to clear bits that do
				 * not interact with hardware.
				 */
				atomic_clear_long(pte, bit);
			}
		}
	}
	crit_exit();
}

/*
 * Lower the permission for all mappings to a given page.
 *
 * No requirements.
 */
void
pmap_page_protect(vm_page_t m, vm_prot_t prot)
{
	if ((prot & VM_PROT_WRITE) == 0) {
		lwkt_gettoken(&vm_token);
		if (prot & (VM_PROT_READ | VM_PROT_EXECUTE)) {
			pmap_clearbit(m, VPTE_W);
			vm_page_flag_clear(m, PG_WRITEABLE);
		} else {
			pmap_remove_all(m);
		}
		lwkt_reltoken(&vm_token);
	}
}

vm_paddr_t
pmap_phys_address(vm_pindex_t ppn)
{
	return (i386_ptob(ppn));
}

/*
 * Return a count of reference bits for a page, clearing those bits.
 * It is not necessary for every reference bit to be cleared, but it
 * is necessary that 0 only be returned when there are truly no
 * reference bits set.
 *
 * XXX: The exact number of bits to check and clear is a matter that
 * should be tested and standardized at some point in the future for
 * optimal aging of shared pages.
 *
 * No requirements.
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
	lwkt_gettoken(&vm_token);

	if ((pv = TAILQ_FIRST(&m->md.pv_list)) != NULL) {

		pvf = pv;

		do {
			pvn = TAILQ_NEXT(pv, pv_list);

			TAILQ_REMOVE(&m->md.pv_list, pv, pv_list);

			TAILQ_INSERT_TAIL(&m->md.pv_list, pv, pv_list);

			if (!pmap_track_modified(pv->pv_pmap, pv->pv_va))
				continue;

			pte = pmap_pte(pv->pv_pmap, pv->pv_va);

			if (pte && (*pte & VPTE_A)) {
				atomic_clear_long(pte, VPTE_A);
				rtval++;
				if (rtval > 4) {
					break;
				}
			}
		} while ((pv = pvn) != NULL && pv != pvf);
	}
	lwkt_reltoken(&vm_token);
	crit_exit();

	return (rtval);
}

/*
 * Return whether or not the specified physical page was modified
 * in any physical maps.
 *
 * No requirements.
 */
boolean_t
pmap_is_modified(vm_page_t m)
{
	boolean_t res;

	lwkt_gettoken(&vm_token);
	res = pmap_testbit(m, VPTE_M);
	lwkt_reltoken(&vm_token);
	return (res);
}

/*
 * Clear the modify bits on the specified physical page.
 *
 * No requirements.
 */
void
pmap_clear_modify(vm_page_t m)
{
	lwkt_gettoken(&vm_token);
	pmap_clearbit(m, VPTE_M);
	lwkt_reltoken(&vm_token);
}

/*
 * Clear the reference bit on the specified physical page.
 *
 * No requirements.
 */
void
pmap_clear_reference(vm_page_t m)
{
	lwkt_gettoken(&vm_token);
	pmap_clearbit(m, VPTE_A);
	lwkt_reltoken(&vm_token);
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

#if 0

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

	va = kmem_alloc_nofault(&kernel_map, size, PAGE_SIZE);
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
 * Sets the memory attribute for the specified page.
 */
void
pmap_page_set_memattr(vm_page_t m, vm_memattr_t ma)
{
	/* This is a vkernel, do nothing */
}

/*
 * Change the PAT attribute on an existing kernel memory map.  Caller
 * must ensure that the virtual memory in question is not accessed
 * during the adjustment.
 */
void
pmap_change_attr(vm_offset_t va, vm_size_t count, int mode)
{
	/* This is a vkernel, do nothing */
}

/*
 * Perform the pmap work for mincore
 *
 * No requirements.
 */
int
pmap_mincore(pmap_t pmap, vm_offset_t addr)
{
	vpte_t *ptep, pte;
	vm_page_t m;
	int val = 0;

	lwkt_gettoken(&vm_token);
	
	ptep = pmap_pte(pmap, addr);
	if (ptep == NULL) {
		lwkt_reltoken(&vm_token);
		return 0;
	}

	if ((pte = *ptep) != 0) {
		vm_paddr_t pa;

		val = MINCORE_INCORE;
		if ((pte & VPTE_MANAGED) == 0)
			goto done;

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
done:
	lwkt_reltoken(&vm_token);
	return val;
}

/*
 * Caller must hold vmspace->vm_map.token for oldvm and newvm
 */
void
pmap_replacevm(struct proc *p, struct vmspace *newvm, int adjrefs)
{
	struct vmspace *oldvm;
	struct lwp *lp;

	oldvm = p->p_vmspace;
	crit_enter();
	if (oldvm != newvm) {
		p->p_vmspace = newvm;
		KKASSERT(p->p_nthreads == 1);
		lp = RB_ROOT(&p->p_lwp_tree);
		pmap_setlwpvm(lp, newvm);
		if (adjrefs) {
			sysref_get(&newvm->vm_sysref);
			sysref_put(&oldvm->vm_sysref);
		}
	}
	crit_exit();
}

void
pmap_setlwpvm(struct lwp *lp, struct vmspace *newvm)
{
	struct vmspace *oldvm;
	struct pmap *pmap;

	crit_enter();
	oldvm = lp->lwp_vmspace;

	if (oldvm != newvm) {
		lp->lwp_vmspace = newvm;
		if (curthread->td_lwp == lp) {
			pmap = vmspace_pmap(newvm);
			atomic_set_cpumask(&pmap->pm_active, mycpu->gd_cpumask);
#if defined(SWTCH_OPTIM_STATS)
			tlb_flush_count++;
#endif
			pmap = vmspace_pmap(oldvm);
			atomic_clear_cpumask(&pmap->pm_active, mycpu->gd_cpumask);
		}
	}
	crit_exit();
}


vm_offset_t
pmap_addr_hint(vm_object_t obj, vm_offset_t addr, vm_size_t size)
{

	if ((obj == NULL) || (size < NBPDR) ||
	    ((obj->type != OBJT_DEVICE) && (obj->type != OBJT_MGTDEVICE))) {
		return addr;
	}

	addr = (addr + (NBPDR - 1)) & ~(NBPDR - 1);
	return addr;
}

/*
 * Used by kmalloc/kfree, page already exists at va
 */
vm_page_t
pmap_kvtom(vm_offset_t va)
{
	vpte_t *ptep;

	KKASSERT(va >= KvaStart && va < KvaEnd);
	ptep = KernelPTA + (va >> PAGE_SHIFT);
	return(PHYS_TO_VM_PAGE(*ptep & PG_FRAME));
}

void
pmap_object_init(vm_object_t object)
{
	/* empty */
}

void
pmap_object_free(vm_object_t object)
{
	/* empty */
}
