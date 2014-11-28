/*
 * (MPSAFE)
 *
 * Copyright (c) 1991 Regents of the University of California.
 * Copyright (c) 1994 John S. Dyson
 * Copyright (c) 1994 David Greenman
 * Copyright (c) 2003 Peter Wemm
 * Copyright (c) 2005-2008 Alan L. Cox <alc@cs.rice.edu>
 * Copyright (c) 2008, 2009 The DragonFly Project.
 * Copyright (c) 2008, 2009 Jordan Gordeev.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * the Systems Programming Group of the University of Utah Computer
 * Science Department and William Jolitz of UUNET Technologies Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
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
 *	from:	@(#)pmap.c	7.7 (Berkeley)	5/12/91
 * $FreeBSD: src/sys/i386/i386/pmap.c,v 1.250.2.18 2002/03/06 22:48:53 silby Exp $
 */

/*
 * Manages physical address maps.
 */

#if JG
#include "opt_pmap.h"
#endif
#include "opt_msgbuf.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/msgbuf.h>
#include <sys/vmmeter.h>
#include <sys/mman.h>
#include <sys/vmspace.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <sys/sysctl.h>
#include <sys/lock.h>
#include <vm/vm_kern.h>
#include <vm/vm_page.h>
#include <vm/vm_map.h>
#include <vm/vm_object.h>
#include <vm/vm_extern.h>
#include <vm/vm_pageout.h>
#include <vm/vm_pager.h>
#include <vm/vm_zone.h>

#include <sys/user.h>
#include <sys/thread2.h>
#include <sys/sysref2.h>
#include <sys/spinlock2.h>
#include <vm/vm_page2.h>

#include <machine/cputypes.h>
#include <machine/md_var.h>
#include <machine/specialreg.h>
#include <machine/smp.h>
#include <machine/globaldata.h>
#include <machine/pmap.h>
#include <machine/pmap_inval.h>

#include <ddb/ddb.h>

#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <pthread.h>

#define PMAP_KEEP_PDIRS
#ifndef PMAP_SHPGPERPROC
#define PMAP_SHPGPERPROC 1000
#endif

#if defined(DIAGNOSTIC)
#define PMAP_DIAGNOSTIC
#endif

#define MINPV 2048

#if !defined(PMAP_DIAGNOSTIC)
#define PMAP_INLINE __inline
#else
#define PMAP_INLINE
#endif

/*
 * Get PDEs and PTEs for user/kernel address space
 */
static pd_entry_t *pmap_pde(pmap_t pmap, vm_offset_t va);
#define pdir_pde(m, v) (m[(vm_offset_t)(v) >> PDRSHIFT])

#define pmap_pde_v(pte)		((*(pd_entry_t *)pte & VPTE_V) != 0)
#define pmap_pte_w(pte)		((*(pt_entry_t *)pte & VPTE_WIRED) != 0)
#define pmap_pte_m(pte)		((*(pt_entry_t *)pte & VPTE_M) != 0)
#define pmap_pte_u(pte)		((*(pt_entry_t *)pte & VPTE_A) != 0)
#define pmap_pte_v(pte)		((*(pt_entry_t *)pte & VPTE_V) != 0)

/*
 * Given a map and a machine independent protection code,
 * convert to a vax protection code.
 */
#define pte_prot(m, p)		\
	(protection_codes[p & (VM_PROT_READ|VM_PROT_WRITE|VM_PROT_EXECUTE)])
static int protection_codes[8];

struct pmap kernel_pmap;
static TAILQ_HEAD(,pmap)	pmap_list = TAILQ_HEAD_INITIALIZER(pmap_list);

static boolean_t pmap_initialized = FALSE;	/* Has pmap_init completed? */

static vm_object_t kptobj;

static int nkpt;

static uint64_t	KPDphys;	/* phys addr of kernel level 2 */
uint64_t		KPDPphys;	/* phys addr of kernel level 3 */
uint64_t		KPML4phys;	/* phys addr of kernel level 4 */

extern int vmm_enabled;
extern void *vkernel_stack;

/*
 * Data for the pv entry allocation mechanism
 */
static vm_zone_t pvzone;
static struct vm_zone pvzone_store;
static struct vm_object pvzone_obj;
static int pv_entry_count=0, pv_entry_max=0, pv_entry_high_water=0;
static int pmap_pagedaemon_waken = 0;
static struct pv_entry *pvinit;

/*
 * All those kernel PT submaps that BSD is so fond of
 */
pt_entry_t *CMAP1 = NULL, *ptmmap;
caddr_t CADDR1 = NULL;
static pt_entry_t *msgbufmap;

uint64_t KPTphys;

static PMAP_INLINE void	free_pv_entry (pv_entry_t pv);
static pv_entry_t get_pv_entry (void);
static void	i386_protection_init (void);
static __inline void	pmap_clearbit (vm_page_t m, int bit);

static void	pmap_remove_all (vm_page_t m);
static int pmap_remove_pte (struct pmap *pmap, pt_entry_t *ptq,
				vm_offset_t sva);
static void pmap_remove_page (struct pmap *pmap, vm_offset_t va);
static int pmap_remove_entry (struct pmap *pmap, vm_page_t m,
				vm_offset_t va);
static boolean_t pmap_testbit (vm_page_t m, int bit);
static void pmap_insert_entry (pmap_t pmap, vm_offset_t va,
		vm_page_t mpte, vm_page_t m);

static vm_page_t pmap_allocpte (pmap_t pmap, vm_offset_t va);

static int pmap_release_free_page (pmap_t pmap, vm_page_t p);
static vm_page_t _pmap_allocpte (pmap_t pmap, vm_pindex_t ptepindex);
#if JGPMAP32
static pt_entry_t * pmap_pte_quick (pmap_t pmap, vm_offset_t va);
#endif
static vm_page_t pmap_page_lookup (vm_object_t object, vm_pindex_t pindex);
static int pmap_unuse_pt (pmap_t, vm_offset_t, vm_page_t);

/*
 * pmap_pte_quick:
 *
 *	Super fast pmap_pte routine best used when scanning the pv lists.
 *	This eliminates many course-grained invltlb calls.  Note that many of
 *	the pv list scans are across different pmaps and it is very wasteful
 *	to do an entire invltlb when checking a single mapping.
 *
 *	Should only be called while in a critical section.
 */
#if JGPMAP32
static __inline pt_entry_t *pmap_pte(pmap_t pmap, vm_offset_t va);

static pt_entry_t *
pmap_pte_quick(pmap_t pmap, vm_offset_t va)
{
	return pmap_pte(pmap, va);
}
#endif

/* Return a non-clipped PD index for a given VA */
static __inline vm_pindex_t
pmap_pde_pindex(vm_offset_t va)
{
	return va >> PDRSHIFT;
}

/* Return various clipped indexes for a given VA */
static __inline vm_pindex_t
pmap_pte_index(vm_offset_t va)
{

	return ((va >> PAGE_SHIFT) & ((1ul << NPTEPGSHIFT) - 1));
}

static __inline vm_pindex_t
pmap_pde_index(vm_offset_t va)
{

	return ((va >> PDRSHIFT) & ((1ul << NPDEPGSHIFT) - 1));
}

static __inline vm_pindex_t
pmap_pdpe_index(vm_offset_t va)
{

	return ((va >> PDPSHIFT) & ((1ul << NPDPEPGSHIFT) - 1));
}

static __inline vm_pindex_t
pmap_pml4e_index(vm_offset_t va)
{

	return ((va >> PML4SHIFT) & ((1ul << NPML4EPGSHIFT) - 1));
}

/* Return a pointer to the PML4 slot that corresponds to a VA */
static __inline pml4_entry_t *
pmap_pml4e(pmap_t pmap, vm_offset_t va)
{

	return (&pmap->pm_pml4[pmap_pml4e_index(va)]);
}

/* Return a pointer to the PDP slot that corresponds to a VA */
static __inline pdp_entry_t *
pmap_pml4e_to_pdpe(pml4_entry_t *pml4e, vm_offset_t va)
{
	pdp_entry_t *pdpe;

	pdpe = (pdp_entry_t *)PHYS_TO_DMAP(*pml4e & VPTE_FRAME);
	return (&pdpe[pmap_pdpe_index(va)]);
}

/* Return a pointer to the PDP slot that corresponds to a VA */
static __inline pdp_entry_t *
pmap_pdpe(pmap_t pmap, vm_offset_t va)
{
	pml4_entry_t *pml4e;

	pml4e = pmap_pml4e(pmap, va);
	if ((*pml4e & VPTE_V) == 0)
		return NULL;
	return (pmap_pml4e_to_pdpe(pml4e, va));
}

/* Return a pointer to the PD slot that corresponds to a VA */
static __inline pd_entry_t *
pmap_pdpe_to_pde(pdp_entry_t *pdpe, vm_offset_t va)
{
	pd_entry_t *pde;

	pde = (pd_entry_t *)PHYS_TO_DMAP(*pdpe & VPTE_FRAME);
	return (&pde[pmap_pde_index(va)]);
}

/* Return a pointer to the PD slot that corresponds to a VA */
static __inline pd_entry_t *
pmap_pde(pmap_t pmap, vm_offset_t va)
{
	pdp_entry_t *pdpe;

	pdpe = pmap_pdpe(pmap, va);
	if (pdpe == NULL || (*pdpe & VPTE_V) == 0)
		 return NULL;
	return (pmap_pdpe_to_pde(pdpe, va));
}

/* Return a pointer to the PT slot that corresponds to a VA */
static __inline pt_entry_t *
pmap_pde_to_pte(pd_entry_t *pde, vm_offset_t va)
{
	pt_entry_t *pte;

	pte = (pt_entry_t *)PHYS_TO_DMAP(*pde & VPTE_FRAME);
	return (&pte[pmap_pte_index(va)]);
}

/* Return a pointer to the PT slot that corresponds to a VA */
static __inline pt_entry_t *
pmap_pte(pmap_t pmap, vm_offset_t va)
{
	pd_entry_t *pde;

	pde = pmap_pde(pmap, va);
	if (pde == NULL || (*pde & VPTE_V) == 0)
		return NULL;
	if ((*pde & VPTE_PS) != 0)	/* compat with i386 pmap_pte() */
		return ((pt_entry_t *)pde);
	return (pmap_pde_to_pte(pde, va));
}


#if JGV
PMAP_INLINE pt_entry_t *
vtopte(vm_offset_t va)
{
	uint64_t mask = ((1ul << (NPTEPGSHIFT + NPDEPGSHIFT +
				  NPDPEPGSHIFT + NPML4EPGSHIFT)) - 1);

	return (PTmap + ((va >> PAGE_SHIFT) & mask));
}

static __inline pd_entry_t *
vtopde(vm_offset_t va)
{
	uint64_t mask = ((1ul << (NPDEPGSHIFT + NPDPEPGSHIFT +
				  NPML4EPGSHIFT)) - 1);

	return (PDmap + ((va >> PDRSHIFT) & mask));
}
#else
static PMAP_INLINE pt_entry_t *
vtopte(vm_offset_t va)
{
	pt_entry_t *x;
	x = pmap_pte(&kernel_pmap, va);
	assert(x != NULL);
	return x;
}

static __inline pd_entry_t *
vtopde(vm_offset_t va)
{
	pd_entry_t *x;
	x = pmap_pde(&kernel_pmap, va);
	assert(x != NULL);
	return x;
}
#endif

static uint64_t
allocpages(vm_paddr_t *firstaddr, int n)
{
	uint64_t ret;

	ret = *firstaddr;
#if JGV
	bzero((void *)ret, n * PAGE_SIZE);
#endif
	*firstaddr += n * PAGE_SIZE;
	return (ret);
}

static void
create_dmap_vmm(vm_paddr_t *firstaddr)
{
	void *stack_addr;
	int pml4_stack_index;
	int pdp_stack_index;
	int pd_stack_index;
	long i,j;
	int regs[4];
	int amd_feature;

	uint64_t KPDP_DMAP_phys = allocpages(firstaddr, NDMPML4E);
	uint64_t KPDP_VSTACK_phys = allocpages(firstaddr, 1);
	uint64_t KPD_VSTACK_phys = allocpages(firstaddr, 1);

	pml4_entry_t *KPML4virt = (pml4_entry_t *)PHYS_TO_DMAP(KPML4phys);
	pdp_entry_t *KPDP_DMAP_virt = (pdp_entry_t *)PHYS_TO_DMAP(KPDP_DMAP_phys);
	pdp_entry_t *KPDP_VSTACK_virt = (pdp_entry_t *)PHYS_TO_DMAP(KPDP_VSTACK_phys);
	pd_entry_t *KPD_VSTACK_virt = (pd_entry_t *)PHYS_TO_DMAP(KPD_VSTACK_phys);

	bzero(KPDP_DMAP_virt, NDMPML4E * PAGE_SIZE);
	bzero(KPDP_VSTACK_virt, 1 * PAGE_SIZE);
	bzero(KPD_VSTACK_virt, 1 * PAGE_SIZE);

	do_cpuid(0x80000001, regs);
	amd_feature = regs[3];

	/* Build the mappings for the first 512GB */
	if (amd_feature & AMDID_PAGE1GB) {
		/* In pages of 1 GB, if supported */
		for (i = 0; i < NPDPEPG; i++) {
			KPDP_DMAP_virt[i] = ((uint64_t)i << PDPSHIFT);
			KPDP_DMAP_virt[i] |= VPTE_RW | VPTE_V | VPTE_PS | VPTE_U;
		}
	} else {
		/* In page of 2MB, otherwise */
		for (i = 0; i < NPDPEPG; i++) {
			uint64_t KPD_DMAP_phys = allocpages(firstaddr, 1);
			pd_entry_t *KPD_DMAP_virt = (pd_entry_t *)PHYS_TO_DMAP(KPD_DMAP_phys);

			bzero(KPD_DMAP_virt, PAGE_SIZE);

			KPDP_DMAP_virt[i] = KPD_DMAP_phys;
			KPDP_DMAP_virt[i] |= VPTE_RW | VPTE_V | VPTE_U;

			/* For each PD, we have to allocate NPTEPG PT */
			for (j = 0; j < NPTEPG; j++) {
				KPD_DMAP_virt[j] = (i << PDPSHIFT) | (j << PDRSHIFT);
				KPD_DMAP_virt[j] |= VPTE_RW | VPTE_V | VPTE_PS | VPTE_U;
			}
		}
	}

	/* DMAP for the first 512G */
	KPML4virt[0] = KPDP_DMAP_phys;
	KPML4virt[0] |= VPTE_RW | VPTE_V | VPTE_U;

	/* create a 2 MB map of the new stack */
	pml4_stack_index = (uint64_t)&stack_addr >> PML4SHIFT;
	KPML4virt[pml4_stack_index] = KPDP_VSTACK_phys;
	KPML4virt[pml4_stack_index] |= VPTE_RW | VPTE_V | VPTE_U;

	pdp_stack_index = ((uint64_t)&stack_addr & PML4MASK) >> PDPSHIFT;
	KPDP_VSTACK_virt[pdp_stack_index] = KPD_VSTACK_phys;
	KPDP_VSTACK_virt[pdp_stack_index] |= VPTE_RW | VPTE_V | VPTE_U;

	pd_stack_index = ((uint64_t)&stack_addr & PDPMASK) >> PDRSHIFT;
	KPD_VSTACK_virt[pd_stack_index] = (uint64_t) vkernel_stack;
	KPD_VSTACK_virt[pd_stack_index] |= VPTE_RW | VPTE_V | VPTE_U | VPTE_PS;
}

static void
create_pagetables(vm_paddr_t *firstaddr, int64_t ptov_offset)
{
	int i;
	pml4_entry_t *KPML4virt;
	pdp_entry_t *KPDPvirt;
	pd_entry_t *KPDvirt;
	pt_entry_t *KPTvirt;
	int kpml4i = pmap_pml4e_index(ptov_offset);
	int kpdpi = pmap_pdpe_index(ptov_offset);
	int kpdi = pmap_pde_index(ptov_offset);

	/*
         * Calculate NKPT - number of kernel page tables.  We have to
         * accomodoate prealloction of the vm_page_array, dump bitmap,
         * MSGBUF_SIZE, and other stuff.  Be generous.
         *
         * Maxmem is in pages.
         */
        nkpt = (Maxmem * (sizeof(struct vm_page) * 2) + MSGBUF_SIZE) / NBPDR;
	/*
	 * Allocate pages
	 */
	KPML4phys = allocpages(firstaddr, 1);
	KPDPphys = allocpages(firstaddr, NKPML4E);
	KPDphys = allocpages(firstaddr, NKPDPE);
	KPTphys = allocpages(firstaddr, nkpt);

	KPML4virt = (pml4_entry_t *)PHYS_TO_DMAP(KPML4phys);
	KPDPvirt = (pdp_entry_t *)PHYS_TO_DMAP(KPDPphys);
	KPDvirt = (pd_entry_t *)PHYS_TO_DMAP(KPDphys);
	KPTvirt = (pt_entry_t *)PHYS_TO_DMAP(KPTphys);

	bzero(KPML4virt, 1 * PAGE_SIZE);
	bzero(KPDPvirt, NKPML4E * PAGE_SIZE);
	bzero(KPDvirt, NKPDPE * PAGE_SIZE);
	bzero(KPTvirt, nkpt * PAGE_SIZE);

	/* Now map the page tables at their location within PTmap */
	for (i = 0; i < nkpt; i++) {
		KPDvirt[i + kpdi] = KPTphys + (i << PAGE_SHIFT);
		KPDvirt[i + kpdi] |= VPTE_RW | VPTE_V | VPTE_U;
	}

	/* And connect up the PD to the PDP */
	for (i = 0; i < NKPDPE; i++) {
		KPDPvirt[i + kpdpi] = KPDphys + (i << PAGE_SHIFT);
		KPDPvirt[i + kpdpi] |= VPTE_RW | VPTE_V | VPTE_U;
	}

	/* And recursively map PML4 to itself in order to get PTmap */
	KPML4virt[PML4PML4I] = KPML4phys;
	KPML4virt[PML4PML4I] |= VPTE_RW | VPTE_V | VPTE_U;

	/* Connect the KVA slot up to the PML4 */
	KPML4virt[kpml4i] = KPDPphys;
	KPML4virt[kpml4i] |= VPTE_RW | VPTE_V | VPTE_U;
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
 *	Bootstrap the system enough to run with virtual memory.
 *
 *	On the i386 this is called after mapping has already been enabled
 *	and just syncs the pmap module with what has already been done.
 *	[We can't call it easily with mapping off since the kernel is not
 *	mapped with PA == VA, hence we would have to relocate every address
 *	from the linked base (virtual) address "KERNBASE" to the actual
 *	(physical) address starting relative to 0]
 */
void
pmap_bootstrap(vm_paddr_t *firstaddr, int64_t ptov_offset)
{
	vm_offset_t va;
	pt_entry_t *pte;

	/*
	 * Create an initial set of page tables to run the kernel in.
	 */
	create_pagetables(firstaddr, ptov_offset);

	/* Create the DMAP for the VMM */
	if(vmm_enabled) {
		create_dmap_vmm(firstaddr);
	}

	virtual_start = KvaStart;
	virtual_end = KvaEnd;

	/*
	 * Initialize protection array.
	 */
	i386_protection_init();

	/*
	 * The kernel's pmap is statically allocated so we don't have to use
	 * pmap_create, which is unlikely to work correctly at this part of
	 * the boot sequence (XXX and which no longer exists).
	 *
	 * The kernel_pmap's pm_pteobj is used only for locking and not
	 * for mmu pages.
	 */
	kernel_pmap.pm_pml4 = (pml4_entry_t *)PHYS_TO_DMAP(KPML4phys);
	kernel_pmap.pm_count = 1;
	/* don't allow deactivation */
	CPUMASK_ASSALLONES(kernel_pmap.pm_active);
	kernel_pmap.pm_pteobj = NULL;	/* see pmap_init */
	TAILQ_INIT(&kernel_pmap.pm_pvlist);
	TAILQ_INIT(&kernel_pmap.pm_pvlist_free);
	lwkt_token_init(&kernel_pmap.pm_token, "kpmap_tok");
	spin_init(&kernel_pmap.pm_spin, "pmapbootstrap");

	/*
	 * Reserve some special page table entries/VA space for temporary
	 * mapping of pages.
	 */
#define	SYSMAP(c, p, v, n)	\
	v = (c)va; va += ((n)*PAGE_SIZE); p = pte; pte += (n);

	va = virtual_start;
	pte = pmap_pte(&kernel_pmap, va);
	/*
	 * CMAP1/CMAP2 are used for zeroing and copying pages.
	 */
	SYSMAP(caddr_t, CMAP1, CADDR1, 1)

#if JGV
	/*
	 * Crashdump maps.
	 */
	SYSMAP(caddr_t, pt_crashdumpmap, crashdumpmap, MAXDUMPPGS);
#endif

	/*
	 * ptvmmap is used for reading arbitrary physical pages via
	 * /dev/mem.
	 */
	SYSMAP(caddr_t, ptmmap, ptvmmap, 1)

	/*
	 * msgbufp is used to map the system message buffer.
	 * XXX msgbufmap is not used.
	 */
	SYSMAP(struct msgbuf *, msgbufmap, msgbufp,
	       atop(round_page(MSGBUF_SIZE)))

	virtual_start = va;

	*CMAP1 = 0;
	/* Not ready to do an invltlb yet for VMM*/
	if (!vmm_enabled)
		cpu_invltlb();

}

/*
 *	Initialize the pmap module.
 *	Called by vm_init, to initialize any structures that the pmap
 *	system needs to map virtual memory.
 *	pmap_init has been enhanced to support in a fairly consistant
 *	way, discontiguous physical memory.
 */
void
pmap_init(void)
{
	int i;
	int initial_pvs;

	/*
	 * object for kernel page table pages
	 */
	/* JG I think the number can be arbitrary */
	kptobj = vm_object_allocate(OBJT_DEFAULT, 5);
	kernel_pmap.pm_pteobj = kptobj;

	/*
	 * Allocate memory for random pmap data structures.  Includes the
	 * pv_head_table.
	 */
	for(i = 0; i < vm_page_array_size; i++) {
		vm_page_t m;

		m = &vm_page_array[i];
		TAILQ_INIT(&m->md.pv_list);
		m->md.pv_list_count = 0;
	}

	/*
	 * init the pv free list
	 */
	initial_pvs = vm_page_array_size;
	if (initial_pvs < MINPV)
		initial_pvs = MINPV;
	pvzone = &pvzone_store;
	pvinit = (struct pv_entry *) kmem_alloc(&kernel_map,
		initial_pvs * sizeof (struct pv_entry));
	zbootinit(pvzone, "PV ENTRY", sizeof (struct pv_entry), pvinit,
		initial_pvs);

	/*
	 * Now it is safe to enable pv_table recording.
	 */
	pmap_initialized = TRUE;
}

/*
 * Initialize the address space (zone) for the pv_entries.  Set a
 * high water mark so that the system can recover from excessive
 * numbers of pv entries.
 */
void
pmap_init2(void)
{
	int shpgperproc = PMAP_SHPGPERPROC;

	TUNABLE_INT_FETCH("vm.pmap.shpgperproc", &shpgperproc);
	pv_entry_max = shpgperproc * maxproc + vm_page_array_size;
	TUNABLE_INT_FETCH("vm.pmap.pv_entries", &pv_entry_max);
	pv_entry_high_water = 9 * (pv_entry_max / 10);
	zinitna(pvzone, &pvzone_obj, NULL, 0, pv_entry_max, ZONE_INTERRUPT, 1);
}


/***************************************************
 * Low level helper routines.....
 ***************************************************/

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

/*
 * Extract the physical page address associated with the map/VA pair.
 *
 * No requirements.
 */
vm_paddr_t
pmap_extract(pmap_t pmap, vm_offset_t va)
{
	vm_paddr_t rtval;
	pt_entry_t *pte;
	pd_entry_t pde, *pdep;

	lwkt_gettoken(&vm_token);
	rtval = 0;
	pdep = pmap_pde(pmap, va);
	if (pdep != NULL) {
		pde = *pdep;
		if (pde) {
			if ((pde & VPTE_PS) != 0) {
				/* JGV */
				rtval = (pde & PG_PS_FRAME) | (va & PDRMASK);
			} else {
				pte = pmap_pde_to_pte(pdep, va);
				rtval = (*pte & VPTE_FRAME) | (va & PAGE_MASK);
			}
		}
	}
	lwkt_reltoken(&vm_token);
	return rtval;
}

/*
 * Similar to extract but checks protections, SMP-friendly short-cut for
 * vm_fault_page[_quick]().
 */
vm_page_t
pmap_fault_page_quick(pmap_t pmap __unused, vm_offset_t vaddr __unused,
		      vm_prot_t prot __unused)
{
	return(NULL);
}

/*
 *	Routine:	pmap_kextract
 *	Function:
 *		Extract the physical page address associated
 *		kernel virtual address.
 */
vm_paddr_t
pmap_kextract(vm_offset_t va)
{
	pd_entry_t pde;
	vm_paddr_t pa;

	KKASSERT(va >= KvaStart && va < KvaEnd);

	/*
	 * The DMAP region is not included in [KvaStart, KvaEnd)
	 */
#if 0
	if (va >= DMAP_MIN_ADDRESS && va < DMAP_MAX_ADDRESS) {
		pa = DMAP_TO_PHYS(va);
	} else {
#endif
		pde = *vtopde(va);
		if (pde & VPTE_PS) {
			/* JGV */
			pa = (pde & PG_PS_FRAME) | (va & PDRMASK);
		} else {
			/*
			 * Beware of a concurrent promotion that changes the
			 * PDE at this point!  For example, vtopte() must not
			 * be used to access the PTE because it would use the
			 * new PDE.  It is, however, safe to use the old PDE
			 * because the page table page is preserved by the
			 * promotion.
			 */
			pa = *pmap_pde_to_pte(&pde, va);
			pa = (pa & VPTE_FRAME) | (va & PAGE_MASK);
		}
#if 0
	}
#endif
	return pa;
}

/***************************************************
 * Low level mapping routines.....
 ***************************************************/

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
	pt_entry_t *pte;
	pt_entry_t npte;

	KKASSERT(va >= KvaStart && va < KvaEnd);
	npte = pa | VPTE_RW | VPTE_V | VPTE_U;
	pte = vtopte(va);
	if (*pte & VPTE_V)
		pmap_inval_pte(pte, &kernel_pmap, va);
	*pte = npte;
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
	pt_entry_t *pte;
	pt_entry_t npte;

	KKASSERT(va >= KvaStart && va < KvaEnd);

	npte = (vpte_t)pa | VPTE_RW | VPTE_V | VPTE_U;
	pte = vtopte(va);

	if (*pte & VPTE_V)
		pmap_inval_pte_quick(pte, &kernel_pmap, va);
	*pte = npte;
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
	cpu_invlpg((void *)va);
}

/*
 * Remove an unmanaged mapping created with pmap_kenter*().
 */
void
pmap_kremove(vm_offset_t va)
{
	pt_entry_t *pte;

	KKASSERT(va >= KvaStart && va < KvaEnd);

	pte = vtopte(va);
	if (*pte & VPTE_V)
		pmap_inval_pte(pte, &kernel_pmap, va);
	*pte = 0;
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
	pt_entry_t *pte;

	KKASSERT(va >= KvaStart && va < KvaEnd);

	pte = vtopte(va);
	if (*pte & VPTE_V)
		pmap_inval_pte(pte, &kernel_pmap, va); /* NOT _quick */
	*pte = 0;
}

/*
 *	Used to map a range of physical addresses into kernel
 *	virtual address space.
 *
 *	For now, VM is already on, we only need to map the
 *	specified memory.
 */
vm_offset_t
pmap_map(vm_offset_t *virtp, vm_paddr_t start, vm_paddr_t end, int prot)
{
	return PHYS_TO_DMAP(start);
}


/*
 * Map a set of unmanaged VM pages into KVM.
 */
void
pmap_qenter(vm_offset_t va, vm_page_t *m, int count)
{
	vm_offset_t end_va;

	end_va = va + count * PAGE_SIZE;
	KKASSERT(va >= KvaStart && end_va < KvaEnd);

	while (va < end_va) {
		pt_entry_t *pte;

		pte = vtopte(va);
		if (*pte & VPTE_V)
			pmap_inval_pte(pte, &kernel_pmap, va);
		*pte = VM_PAGE_TO_PHYS(*m) | VPTE_RW | VPTE_V | VPTE_U;
		va += PAGE_SIZE;
		m++;
	}
}

/*
 * Undo the effects of pmap_qenter*().
 */
void
pmap_qremove(vm_offset_t va, int count)
{
	vm_offset_t end_va;

	end_va = va + count * PAGE_SIZE;
	KKASSERT(va >= KvaStart && end_va < KvaEnd);

	while (va < end_va) {
		pt_entry_t *pte;

		pte = vtopte(va);
		if (*pte & VPTE_V)
			pmap_inval_pte(pte, &kernel_pmap, va);
		*pte = 0;
		va += PAGE_SIZE;
	}
}

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
 * Create a new thread and optionally associate it with a (new) process.
 * NOTE! the new thread's cpu may not equal the current cpu.
 */
void
pmap_init_thread(thread_t td)
{
	/* enforce pcb placement */
	td->td_pcb = (struct pcb *)(td->td_kstack + td->td_kstack_size) - 1;
	td->td_savefpu = &td->td_pcb->pcb_save;
	td->td_sp = (char *)td->td_pcb - 16; /* JG is -16 needed on x86_64? */
}

/*
 * This routine directly affects the fork perf for a process.
 */
void
pmap_init_proc(struct proc *p)
{
}

/***************************************************
 * Page table page management routines.....
 ***************************************************/

static __inline int pmap_unwire_pte_hold(pmap_t pmap, vm_offset_t va,
			vm_page_t m);

/*
 * This routine unholds page table pages, and if the hold count
 * drops to zero, then it decrements the wire count.
 *
 * We must recheck that this is the last hold reference after busy-sleeping
 * on the page.
 */
static int
_pmap_unwire_pte_hold(pmap_t pmap, vm_offset_t va, vm_page_t m)
{
	vm_page_busy_wait(m, FALSE, "pmuwpt");
	KASSERT(m->queue == PQ_NONE,
		("_pmap_unwire_pte_hold: %p->queue != PQ_NONE", m));

	if (m->hold_count == 1) {
		/*
		 * Unmap the page table page.
		 */
		//abort(); /* JG */
		/* pmap_inval_add(info, pmap, -1); */

		if (m->pindex >= (NUPDE + NUPDPE)) {
			/* PDP page */
			pml4_entry_t *pml4;
			pml4 = pmap_pml4e(pmap, va);
			*pml4 = 0;
		} else if (m->pindex >= NUPDE) {
			/* PD page */
			pdp_entry_t *pdp;
			pdp = pmap_pdpe(pmap, va);
			*pdp = 0;
		} else {
			/* PT page */
			pd_entry_t *pd;
			pd = pmap_pde(pmap, va);
			*pd = 0;
		}

		KKASSERT(pmap->pm_stats.resident_count > 0);
		--pmap->pm_stats.resident_count;

		if (pmap->pm_ptphint == m)
			pmap->pm_ptphint = NULL;

		if (m->pindex < NUPDE) {
			/* We just released a PT, unhold the matching PD */
			vm_page_t pdpg;

			pdpg = PHYS_TO_VM_PAGE(*pmap_pdpe(pmap, va) & VPTE_FRAME);
			pmap_unwire_pte_hold(pmap, va, pdpg);
		}
		if (m->pindex >= NUPDE && m->pindex < (NUPDE + NUPDPE)) {
			/* We just released a PD, unhold the matching PDP */
			vm_page_t pdppg;

			pdppg = PHYS_TO_VM_PAGE(*pmap_pml4e(pmap, va) & VPTE_FRAME);
			pmap_unwire_pte_hold(pmap, va, pdppg);
		}

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
	} else {
		KKASSERT(m->hold_count > 1);
		vm_page_unhold(m);
		vm_page_wakeup(m);
		return 0;
	}
}

static __inline int
pmap_unwire_pte_hold(pmap_t pmap, vm_offset_t va, vm_page_t m)
{
	KKASSERT(m->hold_count > 0);
	if (m->hold_count > 1) {
		vm_page_unhold(m);
		return 0;
	} else {
		return _pmap_unwire_pte_hold(pmap, va, m);
	}
}

/*
 * After removing a page table entry, this routine is used to
 * conditionally free the page, and manage the hold/wire counts.
 */
static int
pmap_unuse_pt(pmap_t pmap, vm_offset_t va, vm_page_t mpte)
{
	/* JG Use FreeBSD/amd64 or FreeBSD/i386 ptepde approaches? */
	vm_pindex_t ptepindex;

	ASSERT_LWKT_TOKEN_HELD(vm_object_token(pmap->pm_pteobj));

	if (mpte == NULL) {
		/*
		 * page table pages in the kernel_pmap are not managed.
		 */
		if (pmap == &kernel_pmap)
			return(0);
		ptepindex = pmap_pde_pindex(va);
		if (pmap->pm_ptphint &&
			(pmap->pm_ptphint->pindex == ptepindex)) {
			mpte = pmap->pm_ptphint;
		} else {
			mpte = pmap_page_lookup(pmap->pm_pteobj, ptepindex);
			pmap->pm_ptphint = mpte;
			vm_page_wakeup(mpte);
		}
	}

	return pmap_unwire_pte_hold(pmap, va, mpte);
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

/*
 * Initialize a preallocated and zeroed pmap structure,
 * such as one in a vmspace structure.
 */
void
pmap_pinit(struct pmap *pmap)
{
	vm_page_t ptdpg;

	/*
	 * No need to allocate page table space yet but we do need a valid
	 * page directory table.
	 */
	if (pmap->pm_pml4 == NULL) {
		pmap->pm_pml4 =
		    (pml4_entry_t *)kmem_alloc_pageable(&kernel_map, PAGE_SIZE);
	}

	/*
	 * Allocate an object for the ptes
	 */
	if (pmap->pm_pteobj == NULL)
		pmap->pm_pteobj = vm_object_allocate(OBJT_DEFAULT, NUPDE + NUPDPE + PML4PML4I + 1);

	/*
	 * Allocate the page directory page, unless we already have
	 * one cached.  If we used the cached page the wire_count will
	 * already be set appropriately.
	 */
	if ((ptdpg = pmap->pm_pdirm) == NULL) {
		ptdpg = vm_page_grab(pmap->pm_pteobj,
				     NUPDE + NUPDPE + PML4PML4I,
				     VM_ALLOC_NORMAL | VM_ALLOC_RETRY |
				     VM_ALLOC_ZERO);
		pmap->pm_pdirm = ptdpg;
		vm_page_flag_clear(ptdpg, PG_MAPPED);
		vm_page_wire(ptdpg);
		vm_page_wakeup(ptdpg);
		pmap_kenter((vm_offset_t)pmap->pm_pml4, VM_PAGE_TO_PHYS(ptdpg));
	}
	pmap->pm_count = 1;
	CPUMASK_ASSZERO(pmap->pm_active);
	pmap->pm_ptphint = NULL;
	TAILQ_INIT(&pmap->pm_pvlist);
	TAILQ_INIT(&pmap->pm_pvlist_free);
	spin_init(&pmap->pm_spin, "pmapinit");
	lwkt_token_init(&pmap->pm_token, "pmap_tok");
	bzero(&pmap->pm_stats, sizeof pmap->pm_stats);
	pmap->pm_stats.resident_count = 1;
}

/*
 * Clean up a pmap structure so it can be physically freed.  This routine
 * is called by the vmspace dtor function.  A great deal of pmap data is
 * left passively mapped to improve vmspace management so we have a bit
 * of cleanup work to do here.
 *
 * No requirements.
 */
void
pmap_puninit(pmap_t pmap)
{
	vm_page_t p;

	KKASSERT(CPUMASK_TESTZERO(pmap->pm_active));
	if ((p = pmap->pm_pdirm) != NULL) {
		KKASSERT(pmap->pm_pml4 != NULL);
		pmap_kremove((vm_offset_t)pmap->pm_pml4);
		vm_page_busy_wait(p, FALSE, "pgpun");
		p->wire_count--;
		atomic_add_int(&vmstats.v_wire_count, -1);
		vm_page_free_zero(p);
		pmap->pm_pdirm = NULL;
	}
	if (pmap->pm_pml4) {
		kmem_free(&kernel_map, (vm_offset_t)pmap->pm_pml4, PAGE_SIZE);
		pmap->pm_pml4 = NULL;
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
 * Attempt to release and free a vm_page in a pmap.  Returns 1 on success,
 * 0 on failure (if the procedure had to sleep).
 *
 * When asked to remove the page directory page itself, we actually just
 * leave it cached so we do not have to incur the SMP inval overhead of
 * removing the kernel mapping.  pmap_puninit() will take care of it.
 */
static int
pmap_release_free_page(struct pmap *pmap, vm_page_t p)
{
	/*
	 * This code optimizes the case of freeing non-busy
	 * page-table pages.  Those pages are zero now, and
	 * might as well be placed directly into the zero queue.
	 */
	if (vm_page_busy_try(p, FALSE)) {
		vm_page_sleep_busy(p, FALSE, "pmaprl");
		return 0;
	}

	/*
	 * Remove the page table page from the processes address space.
	 */
	if (p->pindex == NUPDE + NUPDPE + PML4PML4I) {
		/*
		 * We are the pml4 table itself.
		 */
		/* XXX anything to do here? */
	} else if (p->pindex >= (NUPDE + NUPDPE)) {
		/*
		 * We are a PDP page.
		 * We look for the PML4 entry that points to us.
		 */
		vm_page_t m4 = vm_page_lookup(pmap->pm_pteobj, NUPDE + NUPDPE + PML4PML4I);
		KKASSERT(m4 != NULL);
		pml4_entry_t *pml4 = (pml4_entry_t *)PHYS_TO_DMAP(VM_PAGE_TO_PHYS(m4));
		int idx = (p->pindex - (NUPDE + NUPDPE)) % NPML4EPG;
		KKASSERT(pml4[idx] != 0);
		pml4[idx] = 0;
		m4->hold_count--;
		/* JG What about wire_count? */
	} else if (p->pindex >= NUPDE) {
		/*
		 * We are a PD page.
		 * We look for the PDP entry that points to us.
		 */
		vm_page_t m3 = vm_page_lookup(pmap->pm_pteobj, NUPDE + NUPDPE + (p->pindex - NUPDE) / NPDPEPG);
		KKASSERT(m3 != NULL);
		pdp_entry_t *pdp = (pdp_entry_t *)PHYS_TO_DMAP(VM_PAGE_TO_PHYS(m3));
		int idx = (p->pindex - NUPDE) % NPDPEPG;
		KKASSERT(pdp[idx] != 0);
		pdp[idx] = 0;
		m3->hold_count--;
		/* JG What about wire_count? */
	} else {
		/* We are a PT page.
		 * We look for the PD entry that points to us.
		 */
		vm_page_t m2 = vm_page_lookup(pmap->pm_pteobj, NUPDE + p->pindex / NPDEPG);
		KKASSERT(m2 != NULL);
		pd_entry_t *pd = (pd_entry_t *)PHYS_TO_DMAP(VM_PAGE_TO_PHYS(m2));
		int idx = p->pindex % NPDEPG;
		pd[idx] = 0;
		m2->hold_count--;
		/* JG What about wire_count? */
	}
	KKASSERT(pmap->pm_stats.resident_count > 0);
	--pmap->pm_stats.resident_count;

	if (p->hold_count)  {
		panic("pmap_release: freeing held pt page "
		      "pmap=%p pg=%p dmap=%p pi=%ld {%ld,%ld,%ld}",
		      pmap, p, (void *)PHYS_TO_DMAP(VM_PAGE_TO_PHYS(p)),
		      p->pindex, NUPDE, NUPDPE, PML4PML4I);
	}
	if (pmap->pm_ptphint && (pmap->pm_ptphint->pindex == p->pindex))
		pmap->pm_ptphint = NULL;

	/*
	 * We leave the top-level page table page cached, wired, and mapped in
	 * the pmap until the dtor function (pmap_puninit()) gets called.
	 * However, still clean it up so we can set PG_ZERO.
	 */
	if (p->pindex == NUPDE + NUPDPE + PML4PML4I) {
		bzero(pmap->pm_pml4, PAGE_SIZE);
		vm_page_flag_set(p, PG_ZERO);
		vm_page_wakeup(p);
	} else {
		abort();
		p->wire_count--;
		atomic_add_int(&vmstats.v_wire_count, -1);
		/* JG eventually revert to using vm_page_free_zero() */
		vm_page_free(p);
	}
	return 1;
}

/*
 * this routine is called if the page table page is not
 * mapped correctly.
 */
static vm_page_t
_pmap_allocpte(pmap_t pmap, vm_pindex_t ptepindex)
{
	vm_page_t m, pdppg, pdpg;

	/*
	 * Find or fabricate a new pagetable page.  Handle allocation
	 * races by checking m->valid.
	 */
	m = vm_page_grab(pmap->pm_pteobj, ptepindex,
			 VM_ALLOC_NORMAL | VM_ALLOC_ZERO | VM_ALLOC_RETRY);

	KASSERT(m->queue == PQ_NONE,
		("_pmap_allocpte: %p->queue != PQ_NONE", m));

	/*
	 * Increment the hold count for the page we will be returning to
	 * the caller.
	 */
	m->hold_count++;
	vm_page_wire(m);

	/*
	 * Map the pagetable page into the process address space, if
	 * it isn't already there.
	 */
	++pmap->pm_stats.resident_count;

	if (ptepindex >= (NUPDE + NUPDPE)) {
		pml4_entry_t *pml4;
		vm_pindex_t pml4index;

		/* Wire up a new PDP page */
		pml4index = ptepindex - (NUPDE + NUPDPE);
		pml4 = &pmap->pm_pml4[pml4index];
		*pml4 = VM_PAGE_TO_PHYS(m) |
		    VPTE_RW | VPTE_V | VPTE_U |
		    VPTE_A | VPTE_M;
	} else if (ptepindex >= NUPDE) {
		vm_pindex_t pml4index;
		vm_pindex_t pdpindex;
		pml4_entry_t *pml4;
		pdp_entry_t *pdp;

		/* Wire up a new PD page */
		pdpindex = ptepindex - NUPDE;
		pml4index = pdpindex >> NPML4EPGSHIFT;

		pml4 = &pmap->pm_pml4[pml4index];
		if ((*pml4 & VPTE_V) == 0) {
			/* Have to allocate a new PDP page, recurse */
			if (_pmap_allocpte(pmap, NUPDE + NUPDPE + pml4index)
			     == NULL) {
				--m->wire_count;
				vm_page_free(m);
				return (NULL);
			}
		} else {
			/* Add reference to the PDP page */
			pdppg = PHYS_TO_VM_PAGE(*pml4 & VPTE_FRAME);
			pdppg->hold_count++;
		}
		pdp = (pdp_entry_t *)PHYS_TO_DMAP(*pml4 & VPTE_FRAME);

		/* Now find the pdp page */
		pdp = &pdp[pdpindex & ((1ul << NPDPEPGSHIFT) - 1)];
		KKASSERT(*pdp == 0);	/* JG DEBUG64 */
		*pdp = VM_PAGE_TO_PHYS(m) | VPTE_RW | VPTE_V | VPTE_U |
		       VPTE_A | VPTE_M;
	} else {
		vm_pindex_t pml4index;
		vm_pindex_t pdpindex;
		pml4_entry_t *pml4;
		pdp_entry_t *pdp;
		pd_entry_t *pd;

		/* Wire up a new PT page */
		pdpindex = ptepindex >> NPDPEPGSHIFT;
		pml4index = pdpindex >> NPML4EPGSHIFT;

		/* First, find the pdp and check that its valid. */
		pml4 = &pmap->pm_pml4[pml4index];
		if ((*pml4 & VPTE_V) == 0) {
			/* We miss a PDP page. We ultimately need a PD page.
			 * Recursively allocating a PD page will allocate
			 * the missing PDP page and will also allocate
			 * the PD page we need.
			 */
			/* Have to allocate a new PD page, recurse */
			if (_pmap_allocpte(pmap, NUPDE + pdpindex)
			     == NULL) {
				--m->wire_count;
				vm_page_free(m);
				return (NULL);
			}
			pdp = (pdp_entry_t *)PHYS_TO_DMAP(*pml4 & VPTE_FRAME);
			pdp = &pdp[pdpindex & ((1ul << NPDPEPGSHIFT) - 1)];
		} else {
			pdp = (pdp_entry_t *)PHYS_TO_DMAP(*pml4 & VPTE_FRAME);
			pdp = &pdp[pdpindex & ((1ul << NPDPEPGSHIFT) - 1)];
			if ((*pdp & VPTE_V) == 0) {
				/* Have to allocate a new PD page, recurse */
				if (_pmap_allocpte(pmap, NUPDE + pdpindex)
				     == NULL) {
					--m->wire_count;
					vm_page_free(m);
					return (NULL);
				}
			} else {
				/* Add reference to the PD page */
				pdpg = PHYS_TO_VM_PAGE(*pdp & VPTE_FRAME);
				pdpg->hold_count++;
			}
		}
		pd = (pd_entry_t *)PHYS_TO_DMAP(*pdp & VPTE_FRAME);

		/* Now we know where the page directory page is */
		pd = &pd[ptepindex & ((1ul << NPDEPGSHIFT) - 1)];
		KKASSERT(*pd == 0);	/* JG DEBUG64 */
		*pd = VM_PAGE_TO_PHYS(m) | VPTE_RW | VPTE_V | VPTE_U |
		      VPTE_A | VPTE_M;
	}

	/*
	 * Set the page table hint
	 */
	pmap->pm_ptphint = m;
	vm_page_flag_set(m, PG_MAPPED);
	vm_page_wakeup(m);

	return m;
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
	vm_pindex_t ptepindex;
	pd_entry_t *pd;
	vm_page_t m;

	ASSERT_LWKT_TOKEN_HELD(vm_object_token(pmap->pm_pteobj));

	/*
	 * Calculate pagetable page index
	 */
	ptepindex = pmap_pde_pindex(va);

	/*
	 * Get the page directory entry
	 */
	pd = pmap_pde(pmap, va);

	/*
	 * This supports switching from a 2MB page to a
	 * normal 4K page.
	 */
	if (pd != NULL && (*pd & (VPTE_PS | VPTE_V)) == (VPTE_PS | VPTE_V)) {
		panic("no promotion/demotion yet");
		*pd = 0;
		pd = NULL;
		/*cpu_invltlb();*/
		/*smp_invltlb();*/
	}

	/*
	 * If the page table page is mapped, we just increment the
	 * hold count, and activate it.
	 */
	if (pd != NULL && (*pd & VPTE_V) != 0) {
		/* YYY hint is used here on i386 */
		m = pmap_page_lookup(pmap->pm_pteobj, ptepindex);
		pmap->pm_ptphint = m;
		vm_page_hold(m);
		vm_page_wakeup(m);
		return m;
	}
	/*
	 * Here if the pte page isn't mapped, or if it has been deallocated.
	 */
	return _pmap_allocpte(pmap, ptepindex);
}


/***************************************************
 * Pmap allocation/deallocation routines.
 ***************************************************/

/*
 * Release any resources held by the given physical map.
 * Called when a pmap initialized by pmap_pinit is being released.
 * Should only be called if the map contains no valid mappings.
 *
 * Caller must hold pmap->pm_token
 */
static int pmap_release_callback(struct vm_page *p, void *data);

void
pmap_release(struct pmap *pmap)
{
	vm_object_t object = pmap->pm_pteobj;
	struct rb_vm_page_scan_info info;

	KKASSERT(pmap != &kernel_pmap);

	lwkt_gettoken(&vm_token);
#if defined(DIAGNOSTIC)
	if (object->ref_count != 1)
		panic("pmap_release: pteobj reference count != 1");
#endif

	info.pmap = pmap;
	info.object = object;

	KASSERT(CPUMASK_TESTZERO(pmap->pm_active),
		("pmap %p still active! %016jx",
		pmap,
		(uintmax_t)CPUMASK_LOWMASK(pmap->pm_active)));

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
	lwkt_reltoken(&vm_token);
}

static int
pmap_release_callback(struct vm_page *p, void *data)
{
	struct rb_vm_page_scan_info *info = data;

	if (p->pindex == NUPDE + NUPDPE + PML4PML4I) {
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
 * Grow the number of kernel page table entries, if needed.
 *
 * No requirements.
 */
void
pmap_growkernel(vm_offset_t kstart, vm_offset_t kend)
{
	vm_offset_t addr;
	vm_paddr_t paddr;
	vm_offset_t ptppaddr;
	vm_page_t nkpg;
	pd_entry_t *pde, newpdir;
	pdp_entry_t newpdp;

	addr = kend;

	vm_object_hold(kptobj);
	if (kernel_vm_end == 0) {
		kernel_vm_end = KvaStart;
		nkpt = 0;
		while ((*pmap_pde(&kernel_pmap, kernel_vm_end) & VPTE_V) != 0) {
			kernel_vm_end = (kernel_vm_end + PAGE_SIZE * NPTEPG) & ~(PAGE_SIZE * NPTEPG - 1);
			nkpt++;
			if (kernel_vm_end - 1 >= kernel_map.max_offset) {
				kernel_vm_end = kernel_map.max_offset;
				break;
			}
		}
	}
	addr = roundup2(addr, PAGE_SIZE * NPTEPG);
	if (addr - 1 >= kernel_map.max_offset)
		addr = kernel_map.max_offset;
	while (kernel_vm_end < addr) {
		pde = pmap_pde(&kernel_pmap, kernel_vm_end);
		if (pde == NULL) {
			/* We need a new PDP entry */
			nkpg = vm_page_alloc(kptobj, nkpt,
			                     VM_ALLOC_NORMAL | VM_ALLOC_SYSTEM
					     | VM_ALLOC_INTERRUPT);
			if (nkpg == NULL) {
				panic("pmap_growkernel: no memory to "
				      "grow kernel");
			}
			paddr = VM_PAGE_TO_PHYS(nkpg);
			if ((nkpg->flags & PG_ZERO) == 0)
				pmap_zero_page(paddr);
			vm_page_flag_clear(nkpg, PG_ZERO);
			newpdp = (pdp_entry_t)(paddr |
			    VPTE_V | VPTE_RW | VPTE_U |
			    VPTE_A | VPTE_M);
			*pmap_pdpe(&kernel_pmap, kernel_vm_end) = newpdp;
			nkpt++;
			continue; /* try again */
		}
		if ((*pde & VPTE_V) != 0) {
			kernel_vm_end = (kernel_vm_end + PAGE_SIZE * NPTEPG) &
					~(PAGE_SIZE * NPTEPG - 1);
			if (kernel_vm_end - 1 >= kernel_map.max_offset) {
				kernel_vm_end = kernel_map.max_offset;
				break;
			}
			continue;
		}

		/*
		 * This index is bogus, but out of the way
		 */
		nkpg = vm_page_alloc(kptobj, nkpt,
				     VM_ALLOC_NORMAL |
				     VM_ALLOC_SYSTEM |
				     VM_ALLOC_INTERRUPT);
		if (nkpg == NULL)
			panic("pmap_growkernel: no memory to grow kernel");

		vm_page_wire(nkpg);
		ptppaddr = VM_PAGE_TO_PHYS(nkpg);
		pmap_zero_page(ptppaddr);
		vm_page_flag_clear(nkpg, PG_ZERO);
		newpdir = (pd_entry_t)(ptppaddr |
		    VPTE_V | VPTE_RW | VPTE_U |
		    VPTE_A | VPTE_M);
		*pmap_pde(&kernel_pmap, kernel_vm_end) = newpdir;
		nkpt++;

		kernel_vm_end = (kernel_vm_end + PAGE_SIZE * NPTEPG) &
				~(PAGE_SIZE * NPTEPG - 1);
		if (kernel_vm_end - 1 >= kernel_map.max_offset) {
			kernel_vm_end = kernel_map.max_offset;
			break;
		}
	}
	vm_object_drop(kptobj);
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
	vpte_t vpte;

	/*
	 * If VMM enable, don't do nothing, we
	 * are able to use real page tables
	 */
	if (vmm_enabled)
		return;

#define USER_SIZE	(VM_MAX_USER_ADDRESS - VM_MIN_USER_ADDRESS)

	if (vmspace_create(&vm->vm_pmap, 0, NULL) < 0)
		panic("vmspace_create() failed");

	rp = vmspace_mmap(&vm->vm_pmap, VM_MIN_USER_ADDRESS, USER_SIZE,
			  PROT_READ|PROT_WRITE,
			  MAP_FILE|MAP_SHARED|MAP_VPAGETABLE|MAP_FIXED,
			  MemImageFd, 0);
	if (rp == MAP_FAILED)
		panic("vmspace_mmap: failed");
	vmspace_mcontrol(&vm->vm_pmap, VM_MIN_USER_ADDRESS, USER_SIZE,
			 MADV_NOSYNC, 0);
	vpte = VM_PAGE_TO_PHYS(vmspace_pmap(vm)->pm_pdirm) | VPTE_RW | VPTE_V | VPTE_U;
	r = vmspace_mcontrol(&vm->vm_pmap, VM_MIN_USER_ADDRESS, USER_SIZE,
			     MADV_SETMAP, vpte);
	if (r < 0)
		panic("vmspace_mcontrol: failed");
}

void
cpu_vmspace_free(struct vmspace *vm)
{
	/*
	 * If VMM enable, don't do nothing, we
	 * are able to use real page tables
	 */
	if (vmm_enabled)
		return;

	if (vmspace_destroy(&vm->vm_pmap) < 0)
		panic("vmspace_destroy() failed");
}

/***************************************************
* page management routines.
 ***************************************************/

/*
 * free the pv_entry back to the free list.  This function may be
 * called from an interrupt.
 */
static __inline void
free_pv_entry(pv_entry_t pv)
{
	pv_entry_count--;
	KKASSERT(pv_entry_count >= 0);
	zfree(pvzone, pv);
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
		wakeup(&vm_pages_needed);
	}
	return zalloc(pvzone);
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
		kprintf("pmap_collect: collecting pv entries -- "
			"suggest increasing PMAP_SHPGPERPROC\n");
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
 * caller must hold vm_token.
 */
static int
pmap_remove_entry(struct pmap *pmap, vm_page_t m, vm_offset_t va)
{
	pv_entry_t pv;
	int rtval;

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
	/* JGXXX When can 'pv' be NULL? */
	if (pv) {
		TAILQ_REMOVE(&m->md.pv_list, pv, pv_list);
		m->md.pv_list_count--;
		atomic_add_int(&m->object->agg_pv_list_count, -1);
		KKASSERT(m->md.pv_list_count >= 0);
		if (TAILQ_EMPTY(&m->md.pv_list))
			vm_page_flag_clear(m, PG_MAPPED | PG_WRITEABLE);
		TAILQ_REMOVE(&pmap->pm_pvlist, pv, pv_plist);
		++pmap->pm_generation;
		KKASSERT(pmap->pm_pteobj != NULL);
		vm_object_hold(pmap->pm_pteobj);
		rtval = pmap_unuse_pt(pmap, va, pv->pv_ptem);
		vm_object_drop(pmap->pm_pteobj);
		free_pv_entry(pv);
	}
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
	atomic_add_int(&m->object->agg_pv_list_count, 1);

	crit_exit();
}

/*
 * pmap_remove_pte: do the things to unmap a page in a process
 */
static int
pmap_remove_pte(struct pmap *pmap, pt_entry_t *ptq, vm_offset_t va)
{
	pt_entry_t oldpte;
	vm_page_t m;

	oldpte = pmap_inval_loadandclear(ptq, pmap, va);
	if (oldpte & VPTE_WIRED)
		--pmap->pm_stats.wired_count;
	KKASSERT(pmap->pm_stats.wired_count >= 0);

#if 0
	/*
	 * Machines that don't support invlpg, also don't support
	 * PG_G.  XXX PG_G is disabled for SMP so don't worry about
	 * the SMP case.
	 */
	if (oldpte & PG_G)
		cpu_invlpg((void *)va);
#endif
	KKASSERT(pmap->pm_stats.resident_count > 0);
	--pmap->pm_stats.resident_count;
	if (oldpte & VPTE_MANAGED) {
		m = PHYS_TO_VM_PAGE(oldpte);
		if (oldpte & VPTE_M) {
#if defined(PMAP_DIAGNOSTIC)
			if (pmap_nw_modified(oldpte)) {
				kprintf("pmap_remove: modified page not "
					"writable: va: 0x%lx, pte: 0x%lx\n",
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
	pt_entry_t *pte;

	pte = pmap_pte(pmap, va);
	if (pte == NULL)
		return;
	if ((*pte & VPTE_V) == 0)
		return;
	pmap_remove_pte(pmap, pte, va);
}

/*
 * Remove the given range of addresses from the specified map.
 *
 * It is assumed that the start and end are properly rounded to
 * the page size.
 *
 * This function may not be called from an interrupt if the pmap is
 * not kernel_pmap.
 *
 * No requirements.
 */
void
pmap_remove(struct pmap *pmap, vm_offset_t sva, vm_offset_t eva)
{
	vm_offset_t va_next;
	pml4_entry_t *pml4e;
	pdp_entry_t *pdpe;
	pd_entry_t ptpaddr, *pde;
	pt_entry_t *pte;

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
	if (sva + PAGE_SIZE == eva) {
		pde = pmap_pde(pmap, sva);
		if (pde && (*pde & VPTE_PS) == 0) {
			pmap_remove_page(pmap, sva);
			lwkt_reltoken(&vm_token);
			vm_object_drop(pmap->pm_pteobj);
			return;
		}
	}

	for (; sva < eva; sva = va_next) {
		pml4e = pmap_pml4e(pmap, sva);
		if ((*pml4e & VPTE_V) == 0) {
			va_next = (sva + NBPML4) & ~PML4MASK;
			if (va_next < sva)
				va_next = eva;
			continue;
		}

		pdpe = pmap_pml4e_to_pdpe(pml4e, sva);
		if ((*pdpe & VPTE_V) == 0) {
			va_next = (sva + NBPDP) & ~PDPMASK;
			if (va_next < sva)
				va_next = eva;
			continue;
		}

		/*
		 * Calculate index for next page table.
		 */
		va_next = (sva + NBPDR) & ~PDRMASK;
		if (va_next < sva)
			va_next = eva;

		pde = pmap_pdpe_to_pde(pdpe, sva);
		ptpaddr = *pde;

		/*
		 * Weed out invalid mappings.
		 */
		if (ptpaddr == 0)
			continue;

		/*
		 * Check for large page.
		 */
		if ((ptpaddr & VPTE_PS) != 0) {
			/* JG FreeBSD has more complex treatment here */
			KKASSERT(*pde != 0);
			pmap_inval_pde(pde, pmap, sva);
			pmap->pm_stats.resident_count -= NBPDR / PAGE_SIZE;
			continue;
		}

		/*
		 * Limit our scan to either the end of the va represented
		 * by the current page table page, or to the end of the
		 * range being removed.
		 */
		if (va_next > eva)
			va_next = eva;

		/*
		 * NOTE: pmap_remove_pte() can block.
		 */
		for (pte = pmap_pde_to_pte(pde, sva); sva != va_next; pte++,
		    sva += PAGE_SIZE) {
			if (*pte == 0)
				continue;
			if (pmap_remove_pte(pmap, pte, sva))
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
	pt_entry_t *pte, tpte;
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
			pv->pv_pmap->pm_stats.wired_count--;
		KKASSERT(pv->pv_pmap->pm_stats.wired_count >= 0);

		if (tpte & VPTE_A)
			vm_page_flag_set(m, PG_REFERENCED);

		/*
		 * Update the vm_page_t clean and reference bits.
		 */
		if (tpte & VPTE_M) {
#if defined(PMAP_DIAGNOSTIC)
			if (pmap_nw_modified(tpte)) {
				kprintf(
	"pmap_remove_all: modified page not writable: va: 0x%lx, pte: 0x%lx\n",
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
		KKASSERT(m->md.pv_list_count >= 0);
		if (TAILQ_EMPTY(&m->md.pv_list))
			vm_page_flag_clear(m, PG_MAPPED | PG_WRITEABLE);
		vm_object_hold(pv->pv_pmap->pm_pteobj);
		pmap_unuse_pt(pv->pv_pmap, pv->pv_va, pv->pv_ptem);
		vm_object_drop(pv->pv_pmap->pm_pteobj);
		free_pv_entry(pv);
	}
	KKASSERT((m->flags & (PG_MAPPED|PG_WRITEABLE)) == 0);
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
	vm_offset_t va_next;
	pml4_entry_t *pml4e;
	pdp_entry_t *pdpe;
	pd_entry_t ptpaddr, *pde;
	pt_entry_t *pte;

	/* JG review for NX */

	if (pmap == NULL)
		return;

	if ((prot & VM_PROT_READ) == VM_PROT_NONE) {
		pmap_remove(pmap, sva, eva);
		return;
	}

	if (prot & VM_PROT_WRITE)
		return;

	lwkt_gettoken(&vm_token);

	for (; sva < eva; sva = va_next) {

		pml4e = pmap_pml4e(pmap, sva);
		if ((*pml4e & VPTE_V) == 0) {
			va_next = (sva + NBPML4) & ~PML4MASK;
			if (va_next < sva)
				va_next = eva;
			continue;
		}

		pdpe = pmap_pml4e_to_pdpe(pml4e, sva);
		if ((*pdpe & VPTE_V) == 0) {
			va_next = (sva + NBPDP) & ~PDPMASK;
			if (va_next < sva)
				va_next = eva;
			continue;
		}

		va_next = (sva + NBPDR) & ~PDRMASK;
		if (va_next < sva)
			va_next = eva;

		pde = pmap_pdpe_to_pde(pdpe, sva);
		ptpaddr = *pde;

		/*
		 * Check for large page.
		 */
		if ((ptpaddr & VPTE_PS) != 0) {
			/* JG correct? */
			pmap_clean_pde(pde, pmap, sva);
			pmap->pm_stats.resident_count -= NBPDR / PAGE_SIZE;
			continue;
		}

		/*
		 * Weed out invalid mappings. Note: we assume that the page
		 * directory table is always allocated, and in kernel virtual.
		 */
		if (ptpaddr == 0)
			continue;

		if (va_next > eva)
			va_next = eva;

		for (pte = pmap_pde_to_pte(pde, sva); sva != va_next; pte++,
		    sva += PAGE_SIZE) {
			pt_entry_t pbits;
			vm_page_t m;

			/*
			 * Clean managed pages and also check the accessed
			 * bit.  Just remove write perms for unmanaged
			 * pages.  Be careful of races, turning off write
			 * access will force a fault rather then setting
			 * the modified bit at an unexpected time.
			 */
			if (*pte & VPTE_MANAGED) {
				pbits = pmap_clean_pte(pte, pmap, sva);
				m = NULL;
				if (pbits & VPTE_A) {
					m = PHYS_TO_VM_PAGE(pbits & VPTE_FRAME);
					vm_page_flag_set(m, PG_REFERENCED);
					atomic_clear_long(pte, VPTE_A);
				}
				if (pbits & VPTE_M) {
					if (pmap_track_modified(pmap, sva)) {
						if (m == NULL)
							m = PHYS_TO_VM_PAGE(pbits & VPTE_FRAME);
						vm_page_dirty(m);
					}
				}
			} else {
				pbits = pmap_setro_pte(pte, pmap, sva);
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
	pd_entry_t *pde;
	pt_entry_t *pte;
	vm_paddr_t opa;
	pt_entry_t origpte, newpte;
	vm_page_t mpte;

	if (pmap == NULL)
		return;

	va = trunc_page(va);

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

	pde = pmap_pde(pmap, va);
	if (pde != NULL && (*pde & VPTE_V) != 0) {
		if ((*pde & VPTE_PS) != 0)
			panic("pmap_enter: attempted pmap_enter on 2MB page");
		pte = pmap_pde_to_pte(pde, va);
	} else {
		panic("pmap_enter: invalid page directory va=%#lx", va);
	}

	KKASSERT(pte != NULL);
	/*
	 * Deal with races on the original mapping (though don't worry
	 * about VPTE_A races) by cleaning it.  This will force a fault
	 * if an attempt is made to write to the page.
	 */
	pa = VM_PAGE_TO_PHYS(m);
	origpte = pmap_clean_pte(pte, pmap, va);
	opa = origpte & VPTE_FRAME;

	if (origpte & VPTE_PS)
		panic("pmap_enter: attempted pmap_enter on 2MB page");

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
	if (opa) {
		int err;
		err = pmap_remove_pte(pmap, pte, va);
		if (err)
			panic("pmap_enter: pte vanished, va: 0x%lx", va);
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
	newpte = (pt_entry_t) (pa | pte_prot(pmap, prot) | VPTE_V | VPTE_U);

	if (wired)
		newpte |= VPTE_WIRED;
//	if (pmap != &kernel_pmap)
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
	if ((origpte & ~(VPTE_RW|VPTE_M|VPTE_A)) != newpte) {
		*pte = newpte | VPTE_A;
		if (newpte & VPTE_RW)
			vm_page_flag_set(m, PG_WRITEABLE);
	}
	KKASSERT((newpte & VPTE_MANAGED) == 0 || (m->flags & PG_MAPPED));
	lwkt_reltoken(&vm_token);
	vm_object_drop(pmap->pm_pteobj);
}

/*
 * This code works like pmap_enter() but assumes VM_PROT_READ and not-wired.
 *
 * Currently this routine may only be used on user pmaps, not kernel_pmap.
 *
 * No requirements.
 */
void
pmap_enter_quick(pmap_t pmap, vm_offset_t va, vm_page_t m)
{
	pt_entry_t *pte;
	vm_paddr_t pa;
	vm_page_t mpte;
	vm_pindex_t ptepindex;
	pd_entry_t *ptepa;

	KKASSERT(pmap != &kernel_pmap);

	KKASSERT(va >= VM_MIN_USER_ADDRESS && va < VM_MAX_USER_ADDRESS);

	/*
	 * Calculate pagetable page index
	 */
	ptepindex = pmap_pde_pindex(va);

	vm_object_hold(pmap->pm_pteobj);
	lwkt_gettoken(&vm_token);

	do {
		/*
		 * Get the page directory entry
		 */
		ptepa = pmap_pde(pmap, va);

		/*
		 * If the page table page is mapped, we just increment
		 * the hold count, and activate it.
		 */
		if (ptepa && (*ptepa & VPTE_V) != 0) {
			if (*ptepa & VPTE_PS)
				panic("pmap_enter_quick: unexpected mapping into 2MB page");
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
	if (*pte & VPTE_V) {
		KKASSERT(mpte != NULL);
		pmap_unwire_pte_hold(pmap, va, mpte);
		pa = VM_PAGE_TO_PHYS(m);
		KKASSERT(((*pte ^ pa) & VPTE_FRAME) == 0);
		lwkt_reltoken(&vm_token);
		vm_object_drop(pmap->pm_pteobj);
		return;
	}

	/*
	 * Enter on the PV list if part of our managed memory
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
	vm_size_t psize;

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

	psize = x86_64_btop(size);

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
				 info->addr + x86_64_ptob(rel_index), p);
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
	pt_entry_t *pte;
	pd_entry_t *pde;
	int ret;

	lwkt_gettoken(&vm_token);
	pde = pmap_pde(pmap, addr);
	if (pde == NULL || *pde == 0) {
		ret = 0;
	} else {
		pte = pmap_pde_to_pte(pde, addr);
		ret = (*pte) ? 0 : 1;
	}
	lwkt_reltoken(&vm_token);
	return (ret);
}

/*
 * Change the wiring attribute for a map/virtual-address pair.
 *
 * The mapping must already exist in the pmap.
 * No other requirements.
 */
void
pmap_change_wiring(pmap_t pmap, vm_offset_t va, boolean_t wired,
		   vm_map_entry_t entry __unused)
{
	pt_entry_t *pte;

	if (pmap == NULL)
		return;

	lwkt_gettoken(&vm_token);
	pte = pmap_pte(pmap, va);

	if (wired && !pmap_pte_w(pte))
		pmap->pm_stats.wired_count++;
	else if (!wired && pmap_pte_w(pte))
		pmap->pm_stats.wired_count--;

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
	/*
	 * XXX BUGGY.  Amoung other things srcmpte is assumed to remain
	 * valid through blocking calls, and that's just not going to
	 * be the case.
	 *
	 * FIXME!
	 */
	return;
}

/*
 * pmap_zero_page:
 *
 *	Zero the specified physical page.
 *
 *	This function may be called from an interrupt and no locking is
 *	required.
 */
void
pmap_zero_page(vm_paddr_t phys)
{
	vm_offset_t va = PHYS_TO_DMAP(phys);

	bzero((void *)va, PAGE_SIZE);
}

/*
 * pmap_page_assertzero:
 *
 *	Assert that a page is empty, panic if it isn't.
 */
void
pmap_page_assertzero(vm_paddr_t phys)
{
	int i;

	crit_enter();
	vm_offset_t virt = PHYS_TO_DMAP(phys);

	for (i = 0; i < PAGE_SIZE; i += sizeof(int)) {
	    if (*(int *)((char *)virt + i) != 0) {
		panic("pmap_page_assertzero() @ %p not zero!",
		    (void *)virt);
	    }
	}
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
	crit_enter();
	vm_offset_t virt = PHYS_TO_DMAP(phys);
	bzero((char *)virt + off, size);
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
	vm_offset_t src_virt, dst_virt;

	crit_enter();
	src_virt = PHYS_TO_DMAP(src);
	dst_virt = PHYS_TO_DMAP(dst);
	bcopy((void *)src_virt, (void *)dst_virt, PAGE_SIZE);
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
	vm_offset_t src_virt, dst_virt;

	crit_enter();
	src_virt = PHYS_TO_DMAP(src);
	dst_virt = PHYS_TO_DMAP(dst);
	bcopy((char *)src_virt + (src & PAGE_MASK),
	      (char *)dst_virt + (dst & PAGE_MASK),
	      bytes);
	crit_exit();
}

/*
 * Returns true if the pmap's pv is one of the first 16 pvs linked to
 * from this page.  This count may be changed upwards or downwards
 * in the future; it is only necessary that true be returned for a small
 * subset of pmaps for proper page aging.
 *
 * No other requirements.
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
 * Remove all pages from specified address space this aids process
 * exit speeds.  Also, this code is special cased for current
 * process only, but can have the more generic (and slightly slower)
 * mode enabled.  This is much faster than pmap_remove in the case
 * of running down an entire address space.
 *
 * No other requirements.
 */
void
pmap_remove_pages(pmap_t pmap, vm_offset_t sva, vm_offset_t eva)
{
	pt_entry_t *pte, tpte;
	pv_entry_t pv, npv;
	vm_page_t m;
	int save_generation;

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

		m = PHYS_TO_VM_PAGE(tpte & VPTE_FRAME);

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
		if (TAILQ_EMPTY(&m->md.pv_list))
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
 */
static boolean_t
pmap_testbit(vm_page_t m, int bit)
{
	pv_entry_t pv;
	pt_entry_t *pte;

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
		if (pv->pv_pmap == NULL) {
			kprintf("Null pmap (tb) at va: 0x%lx\n", pv->pv_va);
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
 */
static __inline void
pmap_clearbit(vm_page_t m, int bit)
{
	pv_entry_t pv;
	pt_entry_t *pte;
	pt_entry_t pbits;

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
		if (bit == VPTE_RW) {
			if (!pmap_track_modified(pv->pv_pmap, pv->pv_va))
				continue;
		}

#if defined(PMAP_DIAGNOSTIC)
		if (pv->pv_pmap == NULL) {
			kprintf("Null pmap (cb) at va: 0x%lx\n", pv->pv_va);
			continue;
		}
#endif

		/*
		 * Careful here.  We can use a locked bus instruction to
		 * clear VPTE_A or VPTE_M safely but we need to synchronize
		 * with the target cpus when we mess with VPTE_RW.
		 *
		 * On virtual kernels we must force a new fault-on-write
		 * in the real kernel if we clear the Modify bit ourselves,
		 * otherwise the real kernel will not get a new fault and
		 * will never set our Modify bit again.
		 */
		pte = pmap_pte(pv->pv_pmap, pv->pv_va);
		if (*pte & bit) {
			if (bit == VPTE_RW) {
				/*
				 * We must also clear VPTE_M when clearing
				 * VPTE_RW
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
			} else if ((bit & (VPTE_RW|VPTE_M)) == (VPTE_RW|VPTE_M)) {
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
 * No other requirements.
 */
void
pmap_page_protect(vm_page_t m, vm_prot_t prot)
{
	/* JG NX support? */
	if ((prot & VM_PROT_WRITE) == 0) {
		lwkt_gettoken(&vm_token);
		if (prot & (VM_PROT_READ | VM_PROT_EXECUTE)) {
			pmap_clearbit(m, VPTE_RW);
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
	return (x86_64_ptob(ppn));
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
 * No other requirements.
 */
int
pmap_ts_referenced(vm_page_t m)
{
	pv_entry_t pv, pvf, pvn;
	pt_entry_t *pte;
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
 * No other requirements.
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
 * No other requirements.
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
 * No other requirements.
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
			*kp |= 0; /* if it's VALID is readeable */
		if (prot & VM_PROT_WRITE)
			*kp |= VPTE_RW;
		if (prot & VM_PROT_EXECUTE)
			*kp |= 0; /* if it's VALID is executable */
		++kp;
	}
}

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
 * No other requirements.
 */
int
pmap_mincore(pmap_t pmap, vm_offset_t addr)
{
	pt_entry_t *ptep, pte;
	vm_page_t m;
	int val = 0;

	lwkt_gettoken(&vm_token);
	ptep = pmap_pte(pmap, addr);

	if (ptep && (pte = *ptep) != 0) {
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
 * Replace p->p_vmspace with a new one.  If adjrefs is non-zero the new
 * vmspace will be ref'd and the old one will be deref'd.
 *
 * Caller must hold vmspace->vm_map.token for oldvm and newvm
 */
void
pmap_replacevm(struct proc *p, struct vmspace *newvm, int adjrefs)
{
	struct vmspace *oldvm;
	struct lwp *lp;

	crit_enter();
	oldvm = p->p_vmspace;
	if (oldvm != newvm) {
		if (adjrefs)
			vmspace_ref(newvm);
		p->p_vmspace = newvm;
		KKASSERT(p->p_nthreads == 1);
		lp = RB_ROOT(&p->p_lwp_tree);
		pmap_setlwpvm(lp, newvm);
		if (adjrefs)
			vmspace_rel(oldvm);
	}
	crit_exit();
}

/*
 * Set the vmspace for a LWP.  The vmspace is almost universally set the
 * same as the process vmspace, but virtual kernels need to swap out contexts
 * on a per-lwp basis.
 */
void
pmap_setlwpvm(struct lwp *lp, struct vmspace *newvm)
{
	struct vmspace *oldvm;
	struct pmap *pmap;

	oldvm = lp->lwp_vmspace;
	if (oldvm != newvm) {
		crit_enter();
		lp->lwp_vmspace = newvm;
		if (curthread->td_lwp == lp) {
			pmap = vmspace_pmap(newvm);
			ATOMIC_CPUMASK_ORBIT(pmap->pm_active, mycpu->gd_cpuid);
			if (pmap->pm_active_lock & CPULOCK_EXCL)
				pmap_interlock_wait(newvm);
#if defined(SWTCH_OPTIM_STATS)
			tlb_flush_count++;
#endif
			pmap = vmspace_pmap(oldvm);
			ATOMIC_CPUMASK_NANDBIT(pmap->pm_active,
					       mycpu->gd_cpuid);
		}
		crit_exit();
	}
}

/*
 * The swtch code tried to switch in a heavy weight process whos pmap
 * is locked by another cpu.  We have to wait for the lock to clear before
 * the pmap can be used.
 */
void
pmap_interlock_wait (struct vmspace *vm)
{
	pmap_t pmap = vmspace_pmap(vm);

	if (pmap->pm_active_lock & CPULOCK_EXCL) {
		crit_enter();
		while (pmap->pm_active_lock & CPULOCK_EXCL) {
			cpu_ccfence();
			pthread_yield();
		}
		crit_exit();
	}
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

/*
 * Used by kmalloc/kfree, page already exists at va
 */
vm_page_t
pmap_kvtom(vm_offset_t va)
{
	vpte_t *ptep;

	KKASSERT(va >= KvaStart && va < KvaEnd);
	ptep = vtopte(va);
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
