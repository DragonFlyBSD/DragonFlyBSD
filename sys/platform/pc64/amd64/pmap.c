/*
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
 * $DragonFly: src/sys/platform/pc64/amd64/pmap.c,v 1.3 2008/08/29 17:07:10 dillon Exp $
 */

/*
 *	Manages physical address maps.
 *
 *	In addition to hardware address maps, this
 *	module is called upon to provide software-use-only
 *	maps which may or may not be stored in the same
 *	form as hardware maps.  These pseudo-maps are
 *	used to store intermediate results from copy
 *	operations to and from address spaces.
 *
 *	Since the information managed by this module is
 *	also stored by the logical address mapping module,
 *	this module may throw away valid virtual-to-physical
 *	mappings at almost any time.  However, invalidations
 *	of virtual-to-physical mappings must be done as
 *	requested.
 *
 *	In order to cope with hardware architectures which
 *	make virtual-to-physical map invalidates expensive,
 *	this module may delay invalidate or reduced protection
 *	operations until such time as they are actually
 *	necessary.  This module is given full information as
 *	to which processors are currently using which maps,
 *	and to when physical maps must be made correct.
 */

#if JG
#include "opt_disable_pse.h"
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

#include <machine/cputypes.h>
#include <machine/md_var.h>
#include <machine/specialreg.h>
#include <machine/smp.h>
#include <machine_base/apic/apicreg.h>
#include <machine/globaldata.h>
#include <machine/pmap.h>
#include <machine/pmap_inval.h>

#include <ddb/ddb.h>

#define PMAP_KEEP_PDIRS
#ifndef PMAP_SHPGPERPROC
#define PMAP_SHPGPERPROC 200
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

/* JGPMAP32 */
#define PTDPTDI 0

#define	READY0
#define	READY1
#define	READY2
#define	READY3
#define	READY4
#define	READY5

/*
 * Get PDEs and PTEs for user/kernel address space
 */
#if JGPMAP32
#define	pmap_pde(m, v)	(&((m)->pm_pdir[(vm_offset_t)(v) >> PDRSHIFT]))
#endif
static pd_entry_t *pmap_pde(pmap_t pmap, vm_offset_t va);
#define pdir_pde(m, v) (m[(vm_offset_t)(v) >> PDRSHIFT])

#define pmap_pde_v(pte)		((*(pd_entry_t *)pte & PG_V) != 0)
#define pmap_pte_w(pte)		((*(pt_entry_t *)pte & PG_W) != 0)
#define pmap_pte_m(pte)		((*(pt_entry_t *)pte & PG_M) != 0)
#define pmap_pte_u(pte)		((*(pt_entry_t *)pte & PG_A) != 0)
#define pmap_pte_v(pte)		((*(pt_entry_t *)pte & PG_V) != 0)


/*
 * Given a map and a machine independent protection code,
 * convert to a vax protection code.
 */
#define pte_prot(m, p)		\
	(protection_codes[p & (VM_PROT_READ|VM_PROT_WRITE|VM_PROT_EXECUTE)])
static int protection_codes[8];

struct pmap kernel_pmap;
static TAILQ_HEAD(,pmap)	pmap_list = TAILQ_HEAD_INITIALIZER(pmap_list);

vm_paddr_t avail_start;		/* PA of first available physical page */
vm_paddr_t avail_end;		/* PA of last available physical page */
vm_offset_t virtual_start;	/* VA of first avail page (after kernel bss) */
vm_offset_t virtual_end;	/* VA of last avail page (end of kernel AS) */
vm_offset_t KvaStart;		/* VA start of KVA space */
vm_offset_t KvaEnd;		/* VA end of KVA space (non-inclusive) */
vm_offset_t KvaSize;		/* max size of kernel virtual address space */
static boolean_t pmap_initialized = FALSE;	/* Has pmap_init completed? */
static int pgeflag;		/* PG_G or-in */
static int pseflag;		/* PG_PS or-in */

static vm_object_t kptobj;

static int ndmpdp;
static vm_paddr_t dmaplimit;
static int nkpt;
vm_offset_t kernel_vm_end;

static uint64_t	KPDphys;	/* phys addr of kernel level 2 */
uint64_t		KPDPphys;	/* phys addr of kernel level 3 */
uint64_t		KPML4phys;	/* phys addr of kernel level 4 */

static uint64_t	DMPDphys;	/* phys addr of direct mapped level 2 */
static uint64_t	DMPDPphys;	/* phys addr of direct mapped level 3 */

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
pt_entry_t *CMAP1 = 0, *ptmmap;
caddr_t CADDR1 = 0, ptvmmap = 0;
static pt_entry_t *msgbufmap;
struct msgbuf *msgbufp=0;

/*
 * Crashdump maps.
 */
static pt_entry_t *pt_crashdumpmap;
static caddr_t crashdumpmap;

extern uint64_t KPTphys;
extern pt_entry_t *SMPpt;
extern uint64_t SMPptpa;

#define DISABLE_PSE

static PMAP_INLINE void	free_pv_entry (pv_entry_t pv);
static pv_entry_t get_pv_entry (void);
static void	i386_protection_init (void);
static __inline void	pmap_clearbit (vm_page_t m, int bit);

static void	pmap_remove_all (vm_page_t m);
static void	pmap_enter_quick (pmap_t pmap, vm_offset_t va, vm_page_t m);
static int pmap_remove_pte (struct pmap *pmap, pt_entry_t *ptq, 
				vm_offset_t sva, pmap_inval_info_t info);
static void pmap_remove_page (struct pmap *pmap, 
				vm_offset_t va, pmap_inval_info_t info);
static int pmap_remove_entry (struct pmap *pmap, vm_page_t m,
				vm_offset_t va, pmap_inval_info_t info);
static boolean_t pmap_testbit (vm_page_t m, int bit);
static void pmap_insert_entry (pmap_t pmap, vm_offset_t va,
		vm_page_t mpte, vm_page_t m);

static vm_page_t pmap_allocpte (pmap_t pmap, vm_offset_t va);

static int pmap_release_free_page (pmap_t pmap, vm_page_t p);
static vm_page_t _pmap_allocpte (pmap_t pmap, vm_pindex_t ptepindex);
static pt_entry_t * pmap_pte_quick (pmap_t pmap, vm_offset_t va);
static vm_page_t pmap_page_lookup (vm_object_t object, vm_pindex_t pindex);
static int pmap_unwire_pte_hold(pmap_t pmap, vm_offset_t va, vm_page_t m,
                pmap_inval_info_t info);
static int pmap_unuse_pt (pmap_t, vm_offset_t, vm_page_t, pmap_inval_info_t);
static vm_offset_t pmap_kmem_choose(vm_offset_t addr);

static unsigned pdir4mb;

/*
 * Move the kernel virtual free pointer to the next
 * 2MB.  This is used to help improve performance
 * by using a large (2MB) page for much of the kernel
 * (.text, .data, .bss)
 */
static vm_offset_t
pmap_kmem_choose(vm_offset_t addr)
READY2
{
	vm_offset_t newaddr = addr;

	newaddr = (addr + (NBPDR - 1)) & ~(NBPDR - 1);
	return newaddr;
}

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
static __inline pt_entry_t *pmap_pte(pmap_t pmap, vm_offset_t va);

static pt_entry_t * 
pmap_pte_quick(pmap_t pmap, vm_offset_t va)
READY0
{
	return pmap_pte(pmap, va);
}

/* Return a non-clipped PD index for a given VA */
static __inline vm_pindex_t
pmap_pde_pindex(vm_offset_t va)
READY1
{
	return va >> PDRSHIFT;
}

/* Return various clipped indexes for a given VA */
static __inline vm_pindex_t
pmap_pte_index(vm_offset_t va)
READY1
{

	return ((va >> PAGE_SHIFT) & ((1ul << NPTEPGSHIFT) - 1));
}

static __inline vm_pindex_t
pmap_pde_index(vm_offset_t va)
READY1
{

	return ((va >> PDRSHIFT) & ((1ul << NPDEPGSHIFT) - 1));
}

static __inline vm_pindex_t
pmap_pdpe_index(vm_offset_t va)
READY1
{

	return ((va >> PDPSHIFT) & ((1ul << NPDPEPGSHIFT) - 1));
}

static __inline vm_pindex_t
pmap_pml4e_index(vm_offset_t va)
READY1
{

	return ((va >> PML4SHIFT) & ((1ul << NPML4EPGSHIFT) - 1));
}

/* Return a pointer to the PML4 slot that corresponds to a VA */
static __inline pml4_entry_t *
pmap_pml4e(pmap_t pmap, vm_offset_t va)
READY1
{

	return (&pmap->pm_pml4[pmap_pml4e_index(va)]);
}

/* Return a pointer to the PDP slot that corresponds to a VA */
static __inline pdp_entry_t *
pmap_pml4e_to_pdpe(pml4_entry_t *pml4e, vm_offset_t va)
READY1
{
	pdp_entry_t *pdpe;

	pdpe = (pdp_entry_t *)PHYS_TO_DMAP(*pml4e & PG_FRAME);
	return (&pdpe[pmap_pdpe_index(va)]);
}

/* Return a pointer to the PDP slot that corresponds to a VA */
static __inline pdp_entry_t *
pmap_pdpe(pmap_t pmap, vm_offset_t va)
READY1
{
	pml4_entry_t *pml4e;

	pml4e = pmap_pml4e(pmap, va);
	if ((*pml4e & PG_V) == 0)
		return NULL;
	return (pmap_pml4e_to_pdpe(pml4e, va));
}

/* Return a pointer to the PD slot that corresponds to a VA */
static __inline pd_entry_t *
pmap_pdpe_to_pde(pdp_entry_t *pdpe, vm_offset_t va)
READY1
{
	pd_entry_t *pde;

	pde = (pd_entry_t *)PHYS_TO_DMAP(*pdpe & PG_FRAME);
	return (&pde[pmap_pde_index(va)]);
}

/* Return a pointer to the PD slot that corresponds to a VA */
static __inline pd_entry_t *
pmap_pde(pmap_t pmap, vm_offset_t va)
READY1
{
	pdp_entry_t *pdpe;

	pdpe = pmap_pdpe(pmap, va);
	if (pdpe == NULL || (*pdpe & PG_V) == 0)
		 return NULL;
	return (pmap_pdpe_to_pde(pdpe, va));
}

/* Return a pointer to the PT slot that corresponds to a VA */
static __inline pt_entry_t *
pmap_pde_to_pte(pd_entry_t *pde, vm_offset_t va)
READY1
{
	pt_entry_t *pte;

	pte = (pt_entry_t *)PHYS_TO_DMAP(*pde & PG_FRAME);
	return (&pte[pmap_pte_index(va)]);
}

/* Return a pointer to the PT slot that corresponds to a VA */
static __inline pt_entry_t *
pmap_pte(pmap_t pmap, vm_offset_t va)
READY1
{
	pd_entry_t *pde;

	pde = pmap_pde(pmap, va);
	if (pde == NULL || (*pde & PG_V) == 0)
		return NULL;
	if ((*pde & PG_PS) != 0)	/* compat with i386 pmap_pte() */
		return ((pt_entry_t *)pde);
	return (pmap_pde_to_pte(pde, va));
}


PMAP_INLINE pt_entry_t *
vtopte(vm_offset_t va)
READY1
{
	uint64_t mask = ((1ul << (NPTEPGSHIFT + NPDEPGSHIFT + NPDPEPGSHIFT + NPML4EPGSHIFT)) - 1);

	return (PTmap + ((va >> PAGE_SHIFT) & mask));
}

static __inline pd_entry_t *
vtopde(vm_offset_t va)
READY1
{
	uint64_t mask = ((1ul << (NPDEPGSHIFT + NPDPEPGSHIFT + NPML4EPGSHIFT)) - 1);

	return (PDmap + ((va >> PDRSHIFT) & mask));
}

static uint64_t
allocpages(vm_paddr_t *firstaddr, int n)
READY1
{
	uint64_t ret;

	ret = *firstaddr;
	bzero((void *)ret, n * PAGE_SIZE);
	*firstaddr += n * PAGE_SIZE;
	return (ret);
}

void
create_pagetables(vm_paddr_t *firstaddr)
READY0
{
	int i;
	int count;
	uint64_t cpu0pp, cpu0idlestk;
	int idlestk_page_offset = offsetof(struct privatespace, idlestack) / PAGE_SIZE;

	/* we are running (mostly) V=P at this point */

	/* Allocate pages */
	KPTphys = allocpages(firstaddr, NKPT);
	KPML4phys = allocpages(firstaddr, 1);
	KPDPphys = allocpages(firstaddr, NKPML4E);
	KPDphys = allocpages(firstaddr, NKPDPE);

	ndmpdp = (ptoa(Maxmem) + NBPDP - 1) >> PDPSHIFT;
	if (ndmpdp < 4)		/* Minimum 4GB of dirmap */
		ndmpdp = 4;
	DMPDPphys = allocpages(firstaddr, NDMPML4E);
	if ((amd_feature & AMDID_PAGE1GB) == 0)
		DMPDphys = allocpages(firstaddr, ndmpdp);
	dmaplimit = (vm_paddr_t)ndmpdp << PDPSHIFT;

	/* Fill in the underlying page table pages */
	/* Read-only from zero to physfree */
	/* XXX not fully used, underneath 2M pages */
	for (i = 0; (i << PAGE_SHIFT) < *firstaddr; i++) {
		((pt_entry_t *)KPTphys)[i] = i << PAGE_SHIFT;
		((pt_entry_t *)KPTphys)[i] |= PG_RW | PG_V | PG_G;
	}

	/* Now map the page tables at their location within PTmap */
	for (i = 0; i < NKPT; i++) {
		((pd_entry_t *)KPDphys)[i] = KPTphys + (i << PAGE_SHIFT);
		((pd_entry_t *)KPDphys)[i] |= PG_RW | PG_V;
	}

	/* Map from zero to end of allocations under 2M pages */
	/* This replaces some of the KPTphys entries above */
	for (i = 0; (i << PDRSHIFT) < *firstaddr; i++) {
		((pd_entry_t *)KPDphys)[i] = i << PDRSHIFT;
		((pd_entry_t *)KPDphys)[i] |= PG_RW | PG_V | PG_PS | PG_G;
	}

	/* And connect up the PD to the PDP */
	for (i = 0; i < NKPDPE; i++) {
		((pdp_entry_t *)KPDPphys)[i + KPDPI] = KPDphys +
		    (i << PAGE_SHIFT);
		((pdp_entry_t *)KPDPphys)[i + KPDPI] |= PG_RW | PG_V | PG_U;
	}

	/* Now set up the direct map space using either 2MB or 1GB pages */
	/* Preset PG_M and PG_A because demotion expects it */
	if ((amd_feature & AMDID_PAGE1GB) == 0) {
		for (i = 0; i < NPDEPG * ndmpdp; i++) {
			((pd_entry_t *)DMPDphys)[i] = (vm_paddr_t)i << PDRSHIFT;
			((pd_entry_t *)DMPDphys)[i] |= PG_RW | PG_V | PG_PS |
			    PG_G | PG_M | PG_A;
		}
		/* And the direct map space's PDP */
		for (i = 0; i < ndmpdp; i++) {
			((pdp_entry_t *)DMPDPphys)[i] = DMPDphys +
			    (i << PAGE_SHIFT);
			((pdp_entry_t *)DMPDPphys)[i] |= PG_RW | PG_V | PG_U;
		}
	} else {
		for (i = 0; i < ndmpdp; i++) {
			((pdp_entry_t *)DMPDPphys)[i] =
			    (vm_paddr_t)i << PDPSHIFT;
			((pdp_entry_t *)DMPDPphys)[i] |= PG_RW | PG_V | PG_PS |
			    PG_G | PG_M | PG_A;
		}
	}

	/* And recursively map PML4 to itself in order to get PTmap */
	((pdp_entry_t *)KPML4phys)[PML4PML4I] = KPML4phys;
	((pdp_entry_t *)KPML4phys)[PML4PML4I] |= PG_RW | PG_V | PG_U;

	/* Connect the Direct Map slot up to the PML4 */
	((pdp_entry_t *)KPML4phys)[DMPML4I] = DMPDPphys;
	((pdp_entry_t *)KPML4phys)[DMPML4I] |= PG_RW | PG_V | PG_U;

	/* Connect the KVA slot up to the PML4 */
	((pdp_entry_t *)KPML4phys)[KPML4I] = KPDPphys;
	((pdp_entry_t *)KPML4phys)[KPML4I] |= PG_RW | PG_V | PG_U;
#if JGPMAP32
	common_lvl4_phys = allocpages(firstaddr, 1);	/* 512 512G mappings */
	common_lvl3_phys = allocpages(firstaddr, 1);	/* 512 1G mappings */
	KPTphys = allocpages(firstaddr, NKPT);		/* kernel page table */
	IdlePTD = allocpages(firstaddr, 1);		/* kernel page dir */
	cpu0pp = allocpages(firstaddr, MDGLOBALDATA_BASEALLOC_PAGES);
	cpu0idlestk = allocpages(firstaddr, UPAGES);
	SMPptpa = allocpages(firstaddr, 1);
	SMPpt = (void *)(SMPptpa + KERNBASE);


	/*
	 * Load kernel page table with kernel memory mappings
	 */
	for (i = 0; (i << PAGE_SHIFT) < *firstaddr; i++) {
		((pt_entry_t *)KPTphys)[i] = i << PAGE_SHIFT;
		((pt_entry_t *)KPTphys)[i] |= PG_RW | PG_V;
	}

#ifndef JG
	for (i = 0; i < NKPT; i++) {
		((pd_entry_t *)IdlePTD)[i] = KPTphys + (i << PAGE_SHIFT);
		((pd_entry_t *)IdlePTD)[i] |= PG_RW | PG_V;
	}
#endif

	/*
	 * Set up the kernel page table itself.
	 */
	for (i = 0; i < NKPT; i++) {
		((pd_entry_t *)IdlePTD)[KPTDI + i] = KPTphys + (i << PAGE_SHIFT);
		((pd_entry_t *)IdlePTD)[KPTDI + i] |= PG_RW | PG_V;
	}

#ifndef JG
	count = ISA_HOLE_LENGTH >> PAGE_SHIFT;
	for (i = 0; i < count; i++) {
		((pt_entry_t *)KPTphys)[amd64_btop(ISA_HOLE_START) + i] = \
			(ISA_HOLE_START + i * PAGE_SIZE) | PG_RW | PG_V;
	}
#endif

	/*
	 * Self-mapping
	 */
	((pd_entry_t *)IdlePTD)[PTDPTDI] = (pd_entry_t)IdlePTD | PG_RW | PG_V;

	/*
	 * Map CPU_prvspace[0].mdglobaldata
	 */
	for (i = 0; i < MDGLOBALDATA_BASEALLOC_PAGES; i++) {
		((pt_entry_t *)SMPptpa)[i] = \
			(cpu0pp + i * PAGE_SIZE) | PG_RW | PG_V;
	}

	/*
	 * Map CPU_prvspace[0].idlestack
	 */
	for (i = 0; i < UPAGES; i++) {
		((pt_entry_t *)SMPptpa)[idlestk_page_offset + i] = \
			(cpu0idlestk + i * PAGE_SIZE) | PG_RW | PG_V;
	}

	/*
	 * Link SMPpt.
	 */
	((pd_entry_t *)IdlePTD)[MPPTDI] = SMPptpa | PG_RW | PG_V;

	/*
	 * PML4 maps level 3
	 */
	((pml4_entry_t *)common_lvl4_phys)[LINKPML4I] = common_lvl3_phys | PG_RW | PG_V | PG_U;

	/*
	 * location of "virtual CR3" - a PDP entry that is loaded
	 * with a PD physical address (+ page attributes).
	 * Matt: location of user page directory entry (representing 1G)
	 */
	link_pdpe = &((pdp_entry_t *)common_lvl3_phys)[LINKPDPI];
#endif	/* JGPMAP32 */
}

READY0
void
init_paging(vm_paddr_t *firstaddr) {
	create_pagetables(firstaddr);

#if JGPMAP32
	/* switch to the newly created page table */
	*link_pdpe = IdlePTD | PG_RW | PG_V | PG_U;
	load_cr3(common_lvl4_phys);
	link_pdpe = (void *)((char *)link_pdpe + KERNBASE);

	KvaStart = (vm_offset_t)VADDR(PTDPTDI, 0);
	KvaEnd = (vm_offset_t)VADDR(APTDPTDI, 0);
	KvaSize = KvaEnd - KvaStart;
#endif
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
pmap_bootstrap(vm_paddr_t *firstaddr)
READY0
{
	vm_offset_t va;
	pt_entry_t *pte;
	struct mdglobaldata *gd;
	int i;
	int pg;

	KvaStart = VM_MIN_KERNEL_ADDRESS;
	KvaEnd = VM_MAX_KERNEL_ADDRESS;
	KvaSize = KvaEnd - KvaStart;

	avail_start = *firstaddr;

	/*
	 * Create an initial set of page tables to run the kernel in.
	 */
	create_pagetables(firstaddr);

	virtual_start = (vm_offset_t) PTOV_OFFSET + *firstaddr;
	virtual_start = pmap_kmem_choose(virtual_start);

	virtual_end = VM_MAX_KERNEL_ADDRESS;

	/* XXX do %cr0 as well */
	load_cr4(rcr4() | CR4_PGE | CR4_PSE);
	load_cr3(KPML4phys);

	/*
	 * Initialize protection array.
	 */
	i386_protection_init();

	/*
	 * The kernel's pmap is statically allocated so we don't have to use
	 * pmap_create, which is unlikely to work correctly at this part of
	 * the boot sequence (XXX and which no longer exists).
	 */
#if JGPMAP32
	kernel_pmap.pm_pdir = (pd_entry_t *)(PTOV_OFFSET + (uint64_t)IdlePTD);
#endif
	kernel_pmap.pm_pml4 = (pdp_entry_t *) (PTOV_OFFSET + KPML4phys);
	kernel_pmap.pm_count = 1;
	kernel_pmap.pm_active = (cpumask_t)-1;	/* don't allow deactivation */
	TAILQ_INIT(&kernel_pmap.pm_pvlist);
	nkpt = NKPT;

	/*
	 * Reserve some special page table entries/VA space for temporary
	 * mapping of pages.
	 */
#define	SYSMAP(c, p, v, n)	\
	v = (c)va; va += ((n)*PAGE_SIZE); p = pte; pte += (n);

	va = virtual_start;
#ifdef JG
	pte = (pt_entry_t *) pmap_pte(&kernel_pmap, va);
#else
	pte = vtopte(va);
#endif

	/*
	 * CMAP1/CMAP2 are used for zeroing and copying pages.
	 */
	SYSMAP(caddr_t, CMAP1, CADDR1, 1)

	/*
	 * Crashdump maps.
	 */
	SYSMAP(caddr_t, pt_crashdumpmap, crashdumpmap, MAXDUMPPGS);

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
#if JGPMAP32
	for (i = 0; i < NKPT; i++)
		PTD[i] = 0;
#endif

	/*
	 * PG_G is terribly broken on SMP because we IPI invltlb's in some
	 * cases rather then invl1pg.  Actually, I don't even know why it
	 * works under UP because self-referential page table mappings
	 */
#ifdef SMP
	pgeflag = 0;
#else
	if (cpu_feature & CPUID_PGE)
		pgeflag = PG_G;
#endif
	
/*
 * Initialize the 4MB page size flag
 */
	pseflag = 0;
/*
 * The 4MB page version of the initial
 * kernel page mapping.
 */
	pdir4mb = 0;

#if !defined(DISABLE_PSE)
	if (cpu_feature & CPUID_PSE) {
		pt_entry_t ptditmp;
		/*
		 * Note that we have enabled PSE mode
		 */
		pseflag = PG_PS;
		ptditmp = *(PTmap + amd64_btop(KERNBASE));
		ptditmp &= ~(NBPDR - 1);
		ptditmp |= PG_V | PG_RW | PG_PS | PG_U | pgeflag;
		pdir4mb = ptditmp;

#ifndef SMP
		/*
		 * Enable the PSE mode.  If we are SMP we can't do this
		 * now because the APs will not be able to use it when
		 * they boot up.
		 */
		load_cr4(rcr4() | CR4_PSE);

		/*
		 * We can do the mapping here for the single processor
		 * case.  We simply ignore the old page table page from
		 * now on.
		 */
		/*
		 * For SMP, we still need 4K pages to bootstrap APs,
		 * PSE will be enabled as soon as all APs are up.
		 */
		PTD[KPTDI] = (pd_entry_t)ptditmp;
#if JGPMAP32
		kernel_pmap.pm_pdir[KPTDI] = (pd_entry_t)ptditmp;
#endif
		cpu_invltlb();
#endif
	}
#endif
#ifdef SMP
	if (cpu_apic_address == 0)
		panic("pmap_bootstrap: no local apic!");

	/* local apic is mapped on last page */
	SMPpt[NPTEPG - 1] = (pt_entry_t)(PG_V | PG_RW | PG_N | pgeflag |
	    (cpu_apic_address & PG_FRAME));
#endif

	/*
	 * We need to finish setting up the globaldata page for the BSP.
	 * locore has already populated the page table for the mdglobaldata
	 * portion.
	 */
	pg = MDGLOBALDATA_BASEALLOC_PAGES;
	gd = &CPU_prvspace[0].mdglobaldata;
	gd->gd_CMAP1 = &SMPpt[pg + 0];
	gd->gd_CMAP2 = &SMPpt[pg + 1];
	gd->gd_CMAP3 = &SMPpt[pg + 2];
	gd->gd_PMAP1 = &SMPpt[pg + 3];
	gd->gd_CADDR1 = CPU_prvspace[0].CPAGE1;
	gd->gd_CADDR2 = CPU_prvspace[0].CPAGE2;
	gd->gd_CADDR3 = CPU_prvspace[0].CPAGE3;
	gd->gd_PADDR1 = (pt_entry_t *)CPU_prvspace[0].PPAGE1;

	cpu_invltlb();
}

#ifdef SMP
/*
 * Set 4mb pdir for mp startup
 */
void
pmap_set_opt(void)
READY0
{
	if (pseflag && (cpu_feature & CPUID_PSE)) {
		load_cr4(rcr4() | CR4_PSE);
		if (pdir4mb && mycpu->gd_cpuid == 0) {	/* only on BSP */
#if JGPMAP32
			kernel_pmap.pm_pdir[KPTDI] =
			    PTD[KPTDI] = (pd_entry_t)pdir4mb;
#endif
			cpu_invltlb();
		}
	}
}
#endif

/*
 *	Initialize the pmap module.
 *	Called by vm_init, to initialize any structures that the pmap
 *	system needs to map virtual memory.
 *	pmap_init has been enhanced to support in a fairly consistant
 *	way, discontiguous physical memory.
 */
void
pmap_init(void)
READY0
{
	int i;
	int initial_pvs;

	/*
	 * object for kernel page table pages
	 */
	/* JG I think the number can be arbitrary */
	kptobj = vm_object_allocate(OBJT_DEFAULT, 5);

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
READY0
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

#if defined(PMAP_DIAGNOSTIC)

/*
 * This code checks for non-writeable/modified pages.
 * This should be an invalid condition.
 */
static int
pmap_nw_modified(pt_entry_t pte)
READY1
{
	if ((pte & (PG_M|PG_RW)) == PG_M)
		return 1;
	else
		return 0;
}
#endif


/*
 * this routine defines the region(s) of memory that should
 * not be tested for the modified bit.
 */
static PMAP_INLINE int
pmap_track_modified(vm_offset_t va)
READY0
{
	if ((va < clean_sva) || (va >= clean_eva)) 
		return 1;
	else
		return 0;
}

/*
 * pmap_extract:
 *
 *	Extract the physical page address associated with the map/VA pair.
 *
 *	This function may not be called from an interrupt if the pmap is
 *	not kernel_pmap.
 */
vm_paddr_t 
pmap_extract(pmap_t pmap, vm_offset_t va)
READY1
{
	vm_paddr_t rtval;
	pt_entry_t *pte;
	pd_entry_t pde, *pdep;

	rtval = 0;
	pdep = pmap_pde(pmap, va);
	if (pdep != NULL) {
		pde = *pdep;
		if (pde) {
			if ((pde & PG_PS) != 0) {
				rtval = (pde & PG_PS_FRAME) | (va & PDRMASK);
			} else {
				pte = pmap_pde_to_pte(pdep, va);
				rtval = (*pte & PG_FRAME) | (va & PAGE_MASK);
			}
		}
	}
	return rtval;
}

/*
 *	Routine:	pmap_kextract
 *	Function:
 *		Extract the physical page address associated
 *		kernel virtual address.
 */
vm_paddr_t
pmap_kextract(vm_offset_t va)
READY1
{
	pd_entry_t pde;
	vm_paddr_t pa;

	if (va >= DMAP_MIN_ADDRESS && va < DMAP_MAX_ADDRESS) {
		pa = DMAP_TO_PHYS(va);
	} else {
		pde = *vtopde(va);
		if (pde & PG_PS) {
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
			pa = (pa & PG_FRAME) | (va & PAGE_MASK);
		}
	}
	return pa;
}

/***************************************************
 * Low level mapping routines.....
 ***************************************************/

/*
 * Routine: pmap_kenter
 * Function:
 *  	Add a wired page to the KVA
 *  	NOTE! note that in order for the mapping to take effect -- you
 *  	should do an invltlb after doing the pmap_kenter().
 */
void 
pmap_kenter(vm_offset_t va, vm_paddr_t pa)
READY1
{
	pt_entry_t *pte;
	pt_entry_t npte;
	pmap_inval_info info;

	pmap_inval_init(&info);
	npte = pa | PG_RW | PG_V | pgeflag;
	pte = vtopte(va);
	pmap_inval_add(&info, &kernel_pmap, va);
	*pte = npte;
	pmap_inval_flush(&info);
}

/*
 * Routine: pmap_kenter_quick
 * Function:
 *  	Similar to pmap_kenter(), except we only invalidate the
 *  	mapping on the current CPU.
 */
void
pmap_kenter_quick(vm_offset_t va, vm_paddr_t pa)
READY1
{
	pt_entry_t *pte;
	pt_entry_t npte;

	npte = pa | PG_RW | PG_V | pgeflag;
	pte = vtopte(va);
	*pte = npte;
	cpu_invlpg((void *)va);
}

void
pmap_kenter_sync(vm_offset_t va)
READY1
{
	pmap_inval_info info;

	pmap_inval_init(&info);
	pmap_inval_add(&info, &kernel_pmap, va);
	pmap_inval_flush(&info);
}

void
pmap_kenter_sync_quick(vm_offset_t va)
READY1
{
	cpu_invlpg((void *)va);
}

/*
 * remove a page from the kernel pagetables
 */
void
pmap_kremove(vm_offset_t va)
READY1
{
	pt_entry_t *pte;
	pmap_inval_info info;

	pmap_inval_init(&info);
	pte = vtopte(va);
	pmap_inval_add(&info, &kernel_pmap, va);
	*pte = 0;
	pmap_inval_flush(&info);
}

void
pmap_kremove_quick(vm_offset_t va)
READY1
{
	pt_entry_t *pte;
	pte = vtopte(va);
	*pte = 0;
	cpu_invlpg((void *)va);
}

/*
 * XXX these need to be recoded.  They are not used in any critical path.
 */
void
pmap_kmodify_rw(vm_offset_t va)
READY1
{
	*vtopte(va) |= PG_RW;
	cpu_invlpg((void *)va);
}

void
pmap_kmodify_nc(vm_offset_t va)
READY1
{
	*vtopte(va) |= PG_N;
	cpu_invlpg((void *)va);
}

/*
 *	Used to map a range of physical addresses into kernel
 *	virtual address space.
 *
 *	For now, VM is already on, we only need to map the
 *	specified memory.
 */
vm_offset_t
pmap_map(vm_offset_t virt, vm_paddr_t start, vm_paddr_t end, int prot)
READY1
{
	/*
	 * JG Are callers prepared to get an address in the DMAP,
	 * instead of the passed-in virt?
	 */
	while (start < end) {
		pmap_kenter(virt, start);
		virt += PAGE_SIZE;
		start += PAGE_SIZE;
	}
	return (virt);
}


/*
 * Add a list of wired pages to the kva
 * this routine is only used for temporary
 * kernel mappings that do not need to have
 * page modification or references recorded.
 * Note that old mappings are simply written
 * over.  The page *must* be wired.
 */
void
pmap_qenter(vm_offset_t va, vm_page_t *m, int count)
READY1
{
	vm_offset_t end_va;

	end_va = va + count * PAGE_SIZE;
		
	while (va < end_va) {
		pt_entry_t *pte;

		pte = vtopte(va);
		*pte = VM_PAGE_TO_PHYS(*m) | PG_RW | PG_V | pgeflag;
		cpu_invlpg((void *)va);
		va += PAGE_SIZE;
		m++;
	}
#ifdef SMP
	smp_invltlb();	/* XXX */
#endif
}

void
pmap_qenter2(vm_offset_t va, vm_page_t *m, int count, cpumask_t *mask)
READY1
{
	vm_offset_t end_va;
	cpumask_t cmask = mycpu->gd_cpumask;

	end_va = va + count * PAGE_SIZE;

	while (va < end_va) {
		pt_entry_t *pte;
		pt_entry_t pteval;

		/*
		 * Install the new PTE.  If the pte changed from the prior
		 * mapping we must reset the cpu mask and invalidate the page.
		 * If the pte is the same but we have not seen it on the
		 * current cpu, invlpg the existing mapping.  Otherwise the
		 * entry is optimal and no invalidation is required.
		 */
		pte = vtopte(va);
		pteval = VM_PAGE_TO_PHYS(*m) | PG_A | PG_RW | PG_V | pgeflag;
		if (*pte != pteval) {
			*mask = 0;
			*pte = pteval;
			cpu_invlpg((void *)va);
		} else if ((*mask & cmask) == 0) {
			cpu_invlpg((void *)va);
		}
		va += PAGE_SIZE;
		m++;
	}
	*mask |= cmask;
}

/*
 * this routine jerks page mappings from the
 * kernel -- it is meant only for temporary mappings.
 */
void
pmap_qremove(vm_offset_t va, int count)
READY1
{
	vm_offset_t end_va;

	end_va = va + count * PAGE_SIZE;

	while (va < end_va) {
		pt_entry_t *pte;

		pte = vtopte(va);
		*pte = 0;
		cpu_invlpg((void *)va);
		va += PAGE_SIZE;
	}
#ifdef SMP
	smp_invltlb();
#endif
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
READY1
{
	vm_page_t m;

	do {
		m = vm_page_lookup(object, pindex);
	} while (m && vm_page_sleep_busy(m, FALSE, "pplookp"));

	return(m);
}

/*
 * Create a new thread and optionally associate it with a (new) process.
 * NOTE! the new thread's cpu may not equal the current cpu.
 */
void
pmap_init_thread(thread_t td)
READY1
{
	/* enforce pcb placement */
	td->td_pcb = (struct pcb *)(td->td_kstack + td->td_kstack_size) - 1;
	td->td_savefpu = &td->td_pcb->pcb_save;
	td->td_sp = (char *)td->td_pcb - 16; /* JG is -16 needed on amd64? */
}

/*
 * This routine directly affects the fork perf for a process.
 */
void
pmap_init_proc(struct proc *p)
READY1
{
}

/*
 * Dispose the UPAGES for a process that has exited.
 * This routine directly impacts the exit perf of a process.
 */
void
pmap_dispose_proc(struct proc *p)
READY1
{
	KASSERT(p->p_lock == 0, ("attempt to dispose referenced proc! %p", p));
}

/***************************************************
 * Page table page management routines.....
 ***************************************************/

/*
 * This routine unholds page table pages, and if the hold count
 * drops to zero, then it decrements the wire count.
 */
static int 
_pmap_unwire_pte_hold(pmap_t pmap, vm_offset_t va, vm_page_t m, pmap_inval_info_t info) 
READY1
{
	/* 
	 * Wait until we can busy the page ourselves.  We cannot have
	 * any active flushes if we block.
	 */
	if (m->flags & PG_BUSY) {
		pmap_inval_flush(info);
		while (vm_page_sleep_busy(m, FALSE, "pmuwpt"))
			;
	}
	KASSERT(m->queue == PQ_NONE,
		("_pmap_unwire_pte_hold: %p->queue != PQ_NONE", m));

	if (m->hold_count == 1) {
		/*
		 * Unmap the page table page
		 */
		vm_page_busy(m);
		pmap_inval_add(info, pmap, -1);

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

#if JG
		if (m->pindex < NUPDE) {
			/* We just released a PT, unhold the matching PD */
			vm_page_t pdpg;
	
			pdpg = PHYS_TO_VM_PAGE(*pmap_pdpe(pmap, va) & PG_FRAME);
			pmap_unwire_pte_hold(pmap, va, pdpg, info);
		}
		if (m->pindex >= NUPDE && m->pindex < (NUPDE + NUPDPE)) {
			/* We just released a PD, unhold the matching PDP */
			vm_page_t pdppg;
	
			pdppg = PHYS_TO_VM_PAGE(*pmap_pml4e(pmap, va) & PG_FRAME);
			pmap_unwire_pte_hold(pmap, va, pdppg, info);
		}
#endif

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
		--vmstats.v_wire_count;
		vm_page_flag_clear(m, PG_MAPPED | PG_WRITEABLE);
		vm_page_flash(m);
		vm_page_free_zero(m);
		return 1;
	} else {
		KKASSERT(m->hold_count > 1);
		vm_page_unhold(m);
		return 0;
	}
}

static PMAP_INLINE int
pmap_unwire_pte_hold(pmap_t pmap, vm_offset_t va, vm_page_t m, pmap_inval_info_t info)
READY1
{
	KKASSERT(m->hold_count > 0);
	if (m->hold_count > 1) {
		vm_page_unhold(m);
		return 0;
	} else {
		return _pmap_unwire_pte_hold(pmap, va, m, info);
	}
}

/*
 * After removing a page table entry, this routine is used to
 * conditionally free the page, and manage the hold/wire counts.
 */
static int
pmap_unuse_pt(pmap_t pmap, vm_offset_t va, vm_page_t mpte,
		pmap_inval_info_t info)
READY1
{
	/* JG Use FreeBSD/amd64 or FreeBSD/i386 ptepde approaches? */
	vm_pindex_t ptepindex;
	if (va >= VM_MAX_USER_ADDRESS)
		return 0;

	if (mpte == NULL) {
		ptepindex = pmap_pde_pindex(va);
#if JGHINT
		if (pmap->pm_ptphint &&
			(pmap->pm_ptphint->pindex == ptepindex)) {
			mpte = pmap->pm_ptphint;
		} else {
#endif
			pmap_inval_flush(info);
			mpte = pmap_page_lookup(pmap->pm_pteobj, ptepindex);
			pmap->pm_ptphint = mpte;
#if JGHINT
		}
#endif
	}

	return pmap_unwire_pte_hold(pmap, va, mpte, info);
}

/*
 * Initialize pmap0/vmspace0.  This pmap is not added to pmap_list because
 * it, and IdlePTD, represents the template used to update all other pmaps.
 *
 * On architectures where the kernel pmap is not integrated into the user
 * process pmap, this pmap represents the process pmap, not the kernel pmap.
 * kernel_pmap should be used to directly access the kernel_pmap.
 */
void
pmap_pinit0(struct pmap *pmap)
READY1
{
#if JGPMAP32
	pmap->pm_pdir =
		(pd_entry_t *)kmem_alloc_pageable(&kernel_map, PAGE_SIZE);
	pmap_kenter((vm_offset_t)pmap->pm_pdir, (vm_offset_t) IdlePTD);
#endif
	pmap->pm_pml4 = (pml4_entry_t *)(PTOV_OFFSET + KPML4phys);
	pmap->pm_count = 1;
	pmap->pm_active = 0;
	pmap->pm_ptphint = NULL;
	TAILQ_INIT(&pmap->pm_pvlist);
	bzero(&pmap->pm_stats, sizeof pmap->pm_stats);
}

/*
 * Initialize a preallocated and zeroed pmap structure,
 * such as one in a vmspace structure.
 */
void
pmap_pinit(struct pmap *pmap)
READY1
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
		pmap->pm_pteobj = vm_object_allocate(OBJT_DEFAULT, PML4PML4I + 1);

	/*
	 * Allocate the page directory page, unless we already have
	 * one cached.  If we used the cached page the wire_count will
	 * already be set appropriately.
	 */
	if ((ptdpg = pmap->pm_pdirm) == NULL) {
		ptdpg = vm_page_grab(pmap->pm_pteobj, PML4PML4I,
				     VM_ALLOC_NORMAL | VM_ALLOC_RETRY);
		pmap->pm_pdirm = ptdpg;
		vm_page_flag_clear(ptdpg, PG_MAPPED | PG_BUSY);
		ptdpg->valid = VM_PAGE_BITS_ALL;
		ptdpg->wire_count = 1;
		++vmstats.v_wire_count;
		pmap_kenter((vm_offset_t)pmap->pm_pml4, VM_PAGE_TO_PHYS(ptdpg));
	}
	if ((ptdpg->flags & PG_ZERO) == 0)
		bzero(pmap->pm_pml4, PAGE_SIZE);

	pmap->pm_pml4[KPML4I] = KPDPphys | PG_RW | PG_V | PG_U;
	pmap->pm_pml4[DMPML4I] = DMPDPphys | PG_RW | PG_V | PG_U;

	/* install self-referential address mapping entry */
	pmap->pm_pml4[PML4PML4I] = VM_PAGE_TO_PHYS(ptdpg) | PG_V | PG_RW | PG_A | PG_M;

	pmap->pm_count = 1;
	pmap->pm_active = 0;
	pmap->pm_ptphint = NULL;
	TAILQ_INIT(&pmap->pm_pvlist);
	bzero(&pmap->pm_stats, sizeof pmap->pm_stats);
	pmap->pm_stats.resident_count = 1;
}

/*
 * Clean up a pmap structure so it can be physically freed.  This routine
 * is called by the vmspace dtor function.  A great deal of pmap data is
 * left passively mapped to improve vmspace management so we have a bit
 * of cleanup work to do here.
 */
void
pmap_puninit(pmap_t pmap)
READY1
{
	vm_page_t p;

	KKASSERT(pmap->pm_active == 0);
	if ((p = pmap->pm_pdirm) != NULL) {
		KKASSERT(pmap->pm_pml4 != NULL);
		KKASSERT(pmap->pm_pml4 != (PTOV_OFFSET + KPML4phys));
		pmap_kremove((vm_offset_t)pmap->pm_pml4);
		p->wire_count--;
		vmstats.v_wire_count--;
		KKASSERT((p->flags & PG_BUSY) == 0);
		vm_page_busy(p);
		vm_page_free_zero(p);
		pmap->pm_pdirm = NULL;
	}
	if (pmap->pm_pml4) {
		KKASSERT(pmap->pm_pml4 != (PTOV_OFFSET + KPML4phys));
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
 */
void
pmap_pinit2(struct pmap *pmap)
READY0
{
	crit_enter();
	TAILQ_INSERT_TAIL(&pmap_list, pmap, pm_pmnode);
	/* XXX copies current process, does not fill in MPPTDI */
#if JGPMAP32
	bcopy(PTD + KPTDI, pmap->pm_pdir + KPTDI, nkpt * PTESIZE);
#endif
	crit_exit();
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
READY1
{
	pml4_entry_t *pml4 = pmap->pm_pml4;
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
	/* JG XXX we need to turn 'pindex' into a page table level
	 * (PML4, PDP, PD, PT) and index within the page table page
	 */
#if JGPMAP32
	pde[p->pindex] = 0;
#endif
	KKASSERT(pmap->pm_stats.resident_count > 0);
	--pmap->pm_stats.resident_count;

	if (p->hold_count)  {
		panic("pmap_release: freeing held page table page");
	}
	if (pmap->pm_ptphint && (pmap->pm_ptphint->pindex == p->pindex))
		pmap->pm_ptphint = NULL;

	p->wire_count--;
	vmstats.v_wire_count--;
	vm_page_free_zero(p);
	return 1;
}

/*
 * this routine is called if the page table page is not
 * mapped correctly.
 */
static vm_page_t
_pmap_allocpte(pmap_t pmap, vm_pindex_t ptepindex)
READY1
{
	vm_page_t m, pdppg, pdpg;

	/*
	 * Find or fabricate a new pagetable page
	 */
	m = vm_page_grab(pmap->pm_pteobj, ptepindex,
			VM_ALLOC_NORMAL | VM_ALLOC_ZERO | VM_ALLOC_RETRY);


	if ((m->flags & PG_ZERO) == 0) {
		pmap_zero_page(VM_PAGE_TO_PHYS(m));
	}

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
#if JGPMAP32
	if ((ptepa = pmap->pm_pdir[ptepindex]) != 0) {
		KKASSERT((ptepa & PG_FRAME) == VM_PAGE_TO_PHYS(m));
		vm_page_wakeup(m);
		return(m);
	}
#endif

	if (m->wire_count == 0)
		vmstats.v_wire_count++;
	m->wire_count++;


	/*
	 * Map the pagetable page into the process address space, if
	 * it isn't already there.
	 */

	++pmap->pm_stats.resident_count;

#if JGPMAP32
	ptepa = VM_PAGE_TO_PHYS(m);
	pmap->pm_pdir[ptepindex] =
		(pd_entry_t) (ptepa | PG_U | PG_RW | PG_V | PG_A | PG_M);
#endif
	if (ptepindex >= (NUPDE + NUPDPE)) {
		pml4_entry_t *pml4;
		vm_pindex_t pml4index;

		/* Wire up a new PDP page */
		pml4index = ptepindex - (NUPDE + NUPDPE);
		pml4 = &pmap->pm_pml4[pml4index];
		*pml4 = VM_PAGE_TO_PHYS(m) | PG_U | PG_RW | PG_V | PG_A | PG_M;

	} else if (ptepindex >= NUPDE) {
		vm_pindex_t pml4index;
		vm_pindex_t pdpindex;
		pml4_entry_t *pml4;
		pdp_entry_t *pdp;

		/* Wire up a new PD page */
		pdpindex = ptepindex - NUPDE;
		pml4index = pdpindex >> NPML4EPGSHIFT;

		pml4 = &pmap->pm_pml4[pml4index];
		if ((*pml4 & PG_V) == 0) {
			/* Have to allocate a new PDP page, recurse */
			if (_pmap_allocpte(pmap, NUPDE + NUPDPE + pml4index)
			     == NULL) {
				--m->wire_count;
				vm_page_free(m);
				return (NULL);
			}
		} else {
			/* Add reference to the PDP page */
			pdppg = PHYS_TO_VM_PAGE(*pml4 & PG_FRAME);
			pdppg->wire_count++;
		}
		pdp = (pdp_entry_t *)PHYS_TO_DMAP(*pml4 & PG_FRAME);

		/* Now find the pdp page */
		pdp = &pdp[pdpindex & ((1ul << NPDPEPGSHIFT) - 1)];
		*pdp = VM_PAGE_TO_PHYS(m) | PG_U | PG_RW | PG_V | PG_A | PG_M;

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
		if ((*pml4 & PG_V) == 0) {
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
			pdp = (pdp_entry_t *)PHYS_TO_DMAP(*pml4 & PG_FRAME);
			pdp = &pdp[pdpindex & ((1ul << NPDPEPGSHIFT) - 1)];
		} else {
			pdp = (pdp_entry_t *)PHYS_TO_DMAP(*pml4 & PG_FRAME);
			pdp = &pdp[pdpindex & ((1ul << NPDPEPGSHIFT) - 1)];
			if ((*pdp & PG_V) == 0) {
				/* Have to allocate a new PD page, recurse */
				if (_pmap_allocpte(pmap, NUPDE + pdpindex)
				     == NULL) {
					--m->wire_count;
					vm_page_free(m);
					return (NULL);
				}
			} else {
				/* Add reference to the PD page */
				pdpg = PHYS_TO_VM_PAGE(*pdp & PG_FRAME);
				pdpg->wire_count++;
			}
		}
		pd = (pd_entry_t *)PHYS_TO_DMAP(*pdp & PG_FRAME);

		/* Now we know where the page directory page is */
		pd = &pd[ptepindex & ((1ul << NPDEPGSHIFT) - 1)];
		*pd = VM_PAGE_TO_PHYS(m) | PG_U | PG_RW | PG_V | PG_A | PG_M;
	}


	/*
	 * Set the page table hint
	 */
	pmap->pm_ptphint = m;

	m->valid = VM_PAGE_BITS_ALL;
	vm_page_flag_clear(m, PG_ZERO);
	vm_page_flag_set(m, PG_MAPPED);
	vm_page_wakeup(m);

	return m;
}

static vm_page_t
pmap_allocpte(pmap_t pmap, vm_offset_t va)
READY1
{
	vm_pindex_t ptepindex;
	pd_entry_t *pd;
	vm_page_t m;

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
	if (pd != NULL && (*pd & (PG_PS | PG_V)) == (PG_PS | PG_V)) {
		*pd = 0;
		pd = NULL;
		cpu_invltlb();
		smp_invltlb();
	}

	/*
	 * If the page table page is mapped, we just increment the
	 * hold count, and activate it.
	 */
	if (pd != NULL && (*pd & PG_V) != 0) {
		/* YYY hint is used here on i386 */
		m = pmap_page_lookup( pmap->pm_pteobj, ptepindex);
		pmap->pm_ptphint = m;
		m->hold_count++;
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
 */
static int pmap_release_callback(struct vm_page *p, void *data);

void
pmap_release(struct pmap *pmap)
READY1
{
	vm_object_t object = pmap->pm_pteobj;
	struct rb_vm_page_scan_info info;

	KASSERT(pmap->pm_active == 0, ("pmap still active! %08x", pmap->pm_active));
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
READY1
{
	struct rb_vm_page_scan_info *info = data;

	if (p->pindex == PML4PML4I) {
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
 */

void
pmap_growkernel(vm_offset_t addr)
READY1
{
	vm_paddr_t paddr;
	struct pmap *pmap;
	vm_offset_t ptppaddr;
	vm_page_t nkpg;
	pd_entry_t *pde, newpdir;
	pdp_entry_t newpdp;

	crit_enter();
	if (kernel_vm_end == 0) {
		kernel_vm_end = KERNBASE;
		nkpt = 0;
		while ((*pmap_pde(&kernel_pmap, kernel_vm_end) & PG_V) != 0) {
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
			if (nkpg == NULL)
				panic("pmap_growkernel: no memory to grow kernel");
			if ((nkpg->flags & PG_ZERO) == 0)
				pmap_zero_page(nkpg);
			paddr = VM_PAGE_TO_PHYS(nkpg);
			newpdp = (pdp_entry_t)
				(paddr | PG_V | PG_RW | PG_A | PG_M);
			*pmap_pdpe(&kernel_pmap, kernel_vm_end) = newpdp;
			continue; /* try again */
		}
		if ((*pde & PG_V) != 0) {
			kernel_vm_end = (kernel_vm_end + PAGE_SIZE * NPTEPG) & ~(PAGE_SIZE * NPTEPG - 1);
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
			VM_ALLOC_NORMAL | VM_ALLOC_SYSTEM | VM_ALLOC_INTERRUPT);
		if (nkpg == NULL)
			panic("pmap_growkernel: no memory to grow kernel");

		vm_page_wire(nkpg);
		ptppaddr = VM_PAGE_TO_PHYS(nkpg);
		pmap_zero_page(ptppaddr);
		newpdir = (pd_entry_t) (ptppaddr | PG_V | PG_RW | PG_A | PG_M);
		*pmap_pde(&kernel_pmap, kernel_vm_end) = newpdir;
		nkpt++;

		kernel_vm_end = (kernel_vm_end + PAGE_SIZE * NPTEPG) & ~(PAGE_SIZE * NPTEPG - 1);
		if (kernel_vm_end - 1 >= kernel_map.max_offset) {
			kernel_vm_end = kernel_map.max_offset;
			break;                       
		}
	}
	crit_exit();
}

/*
 *	Retire the given physical map from service.
 *	Should only be called if the map contains
 *	no valid mappings.
 */
void
pmap_destroy(pmap_t pmap)
READY0
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
 *	Add a reference to the specified pmap.
 */
void
pmap_reference(pmap_t pmap)
READY2
{
	if (pmap != NULL) {
		pmap->pm_count++;
	}
}

/***************************************************
* page management routines.
 ***************************************************/

/*
 * free the pv_entry back to the free list.  This function may be
 * called from an interrupt.
 */
static PMAP_INLINE void
free_pv_entry(pv_entry_t pv)
READY2
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
READY2
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
 */
void
pmap_collect(void)
READY0
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
READY1
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

	rtval = 0;
	/* JGXXX When can 'pv' be NULL? */
	if (pv) {
		TAILQ_REMOVE(&m->md.pv_list, pv, pv_list);
		m->md.pv_list_count--;
		KKASSERT(m->md.pv_list_count >= 0);
		if (TAILQ_EMPTY(&m->md.pv_list))
			vm_page_flag_clear(m, PG_MAPPED | PG_WRITEABLE);
		TAILQ_REMOVE(&pmap->pm_pvlist, pv, pv_plist);
		++pmap->pm_generation;
		rtval = pmap_unuse_pt(pmap, va, pv->pv_ptem, info);
		free_pv_entry(pv);
	}
	crit_exit();
	return rtval;
}

/*
 * Create a pv entry for page at pa for
 * (pmap, va).
 */
static void
pmap_insert_entry(pmap_t pmap, vm_offset_t va, vm_page_t mpte, vm_page_t m)
READY1
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
pmap_remove_pte(struct pmap *pmap, pt_entry_t *ptq, vm_offset_t va,
	pmap_inval_info_t info)
READY1
{
	pt_entry_t oldpte;
	vm_page_t m;

	pmap_inval_add(info, pmap, va);
	oldpte = pte_load_clear(ptq);
	if (oldpte & PG_W)
		pmap->pm_stats.wired_count -= 1;
	/*
	 * Machines that don't support invlpg, also don't support
	 * PG_G.  XXX PG_G is disabled for SMP so don't worry about
	 * the SMP case.
	 */
	if (oldpte & PG_G)
		cpu_invlpg((void *)va);
	KKASSERT(pmap->pm_stats.resident_count > 0);
	--pmap->pm_stats.resident_count;
	if (oldpte & PG_MANAGED) {
		m = PHYS_TO_VM_PAGE(oldpte);
		if (oldpte & PG_M) {
#if defined(PMAP_DIAGNOSTIC)
			if (pmap_nw_modified((pt_entry_t) oldpte)) {
				kprintf(
	"pmap_remove: modified page not writable: va: 0x%lx, pte: 0x%lx\n",
				    va, oldpte);
			}
#endif
			if (pmap_track_modified(va))
				vm_page_dirty(m);
		}
		if (oldpte & PG_A)
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
READY1
{
	pt_entry_t *pte;

	pte = pmap_pte(pmap, va);
	if (pte == NULL)
		return;
	if ((*pte & PG_V) == 0)
		return;
	pmap_remove_pte(pmap, pte, va, info);
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
READY1
{
	vm_offset_t va_next;
	pml4_entry_t *pml4e;
	pdp_entry_t *pdpe;
	pd_entry_t ptpaddr, *pde;
	pt_entry_t *pte;
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
	if (sva + PAGE_SIZE == eva) {
		pde = pmap_pde(pmap, sva);
		if (pde && (*pde & PG_PS) == 0) {
			pmap_remove_page(pmap, sva, &info);
			pmap_inval_flush(&info);
			return;
		}
	}

	for (; sva < eva; sva = va_next) {
		pml4e = pmap_pml4e(pmap, sva);
		if ((*pml4e & PG_V) == 0) {
			va_next = (sva + NBPML4) & ~PML4MASK;
			if (va_next < sva)
				va_next = eva;
			continue;
		}

		pdpe = pmap_pml4e_to_pdpe(pml4e, sva);
		if ((*pdpe & PG_V) == 0) {
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
		if ((ptpaddr & PG_PS) != 0) {
			/* JG FreeBSD has more complex treatment here */
			pmap_inval_add(&info, pmap, -1);
			*pde = 0;
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
			if (pmap_remove_pte(pmap, pte, sva, &info))
				break;
		}
	}
	pmap_inval_flush(&info);
}

/*
 * pmap_remove_all:
 *
 *	Removes this physical page from all physical maps in which it resides.
 *	Reflects back modify bits to the pager.
 *
 *	This routine may not be called from an interrupt.
 */

static void
pmap_remove_all(vm_page_t m)
READY1
{
	struct pmap_inval_info info;
	pt_entry_t *pte, tpte;
	pv_entry_t pv;

	if (!pmap_initialized || (m->flags & PG_FICTITIOUS))
		return;

	pmap_inval_init(&info);
	crit_enter();
	while ((pv = TAILQ_FIRST(&m->md.pv_list)) != NULL) {
		KKASSERT(pv->pv_pmap->pm_stats.resident_count > 0);
		--pv->pv_pmap->pm_stats.resident_count;

		pte = pmap_pte_quick(pv->pv_pmap, pv->pv_va);
		pmap_inval_add(&info, pv->pv_pmap, pv->pv_va);
		tpte = pte_load_clear(pte);

		if (tpte & PG_W)
			pv->pv_pmap->pm_stats.wired_count--;

		if (tpte & PG_A)
			vm_page_flag_set(m, PG_REFERENCED);

		/*
		 * Update the vm_page_t clean and reference bits.
		 */
		if (tpte & PG_M) {
#if defined(PMAP_DIAGNOSTIC)
			if (pmap_nw_modified(tpte)) {
				kprintf(
	"pmap_remove_all: modified page not writable: va: 0x%lx, pte: 0x%lx\n",
				    pv->pv_va, tpte);
			}
#endif
			if (pmap_track_modified(pv->pv_va))
				vm_page_dirty(m);
		}
		TAILQ_REMOVE(&m->md.pv_list, pv, pv_list);
		TAILQ_REMOVE(&pv->pv_pmap->pm_pvlist, pv, pv_plist);
		++pv->pv_pmap->pm_generation;
		m->md.pv_list_count--;
		KKASSERT(m->md.pv_list_count >= 0);
		if (TAILQ_EMPTY(&m->md.pv_list))
			vm_page_flag_clear(m, PG_MAPPED | PG_WRITEABLE);
		pmap_unuse_pt(pv->pv_pmap, pv->pv_va, pv->pv_ptem, &info);
		free_pv_entry(pv);
	}
	crit_exit();
	KKASSERT((m->flags & (PG_MAPPED|PG_WRITEABLE)) == 0);
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
READY1
{
	vm_offset_t va_next;
	pml4_entry_t *pml4e;
	pdp_entry_t *pdpe;
	pd_entry_t ptpaddr, *pde;
	pt_entry_t *pte;
	pmap_inval_info info;

	/* JG review for NX */

	if (pmap == NULL)
		return;

	if ((prot & VM_PROT_READ) == VM_PROT_NONE) {
		pmap_remove(pmap, sva, eva);
		return;
	}

	if (prot & VM_PROT_WRITE)
		return;

	pmap_inval_init(&info);

	for (; sva < eva; sva = va_next) {

		pml4e = pmap_pml4e(pmap, sva);
		if ((*pml4e & PG_V) == 0) {
			va_next = (sva + NBPML4) & ~PML4MASK;
			if (va_next < sva)
				va_next = eva;
			continue;
		}

		pdpe = pmap_pml4e_to_pdpe(pml4e, sva);
		if ((*pdpe & PG_V) == 0) {
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
		if ((ptpaddr & PG_PS) != 0) {
			pmap_inval_add(&info, pmap, -1);
			*pde &= ~(PG_M|PG_RW);
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
			pt_entry_t obits, pbits;
			vm_page_t m;

			/*
			 * XXX non-optimal.  Note also that there can be
			 * no pmap_inval_flush() calls until after we modify
			 * ptbase[sindex] (or otherwise we have to do another
			 * pmap_inval_add() call).
			 */
			pmap_inval_add(&info, pmap, sva);
			obits = pbits = *pte;
			if ((pbits & PG_V) == 0)
				continue;
			if (pbits & PG_MANAGED) {
				m = NULL;
				if (pbits & PG_A) {
					m = PHYS_TO_VM_PAGE(pbits & PG_FRAME);
					vm_page_flag_set(m, PG_REFERENCED);
					pbits &= ~PG_A;
				}
				if (pbits & PG_M) {
					if (pmap_track_modified(sva)) {
						if (m == NULL)
							KKASSERT(pbits == (pbits & PG_FRAME));
							m = PHYS_TO_VM_PAGE(pbits);
						vm_page_dirty(m);
						pbits &= ~PG_M;
					}
				}
			}

			pbits &= ~PG_RW;

			if (pbits != obits) {
				*pte = pbits;
			}
		}
	}
	pmap_inval_flush(&info);
}

/*
 *	Insert the given physical page (p) at
 *	the specified virtual address (v) in the
 *	target physical map with the protection requested.
 *
 *	If specified, the page will be wired down, meaning
 *	that the related pte can not be reclaimed.
 *
 *	NB:  This is the only routine which MAY NOT lazy-evaluate
 *	or lose information.  That is, this routine must actually
 *	insert this page into the given map NOW.
 */
void
pmap_enter(pmap_t pmap, vm_offset_t va, vm_page_t m, vm_prot_t prot,
	   boolean_t wired)
READY1
{
	vm_paddr_t pa;
	pd_entry_t *pde;
	pt_entry_t *pte;
	vm_paddr_t opa;
	pt_entry_t origpte, newpte;
	vm_page_t mpte;
	pmap_inval_info info;

	if (pmap == NULL)
		return;

	va = trunc_page(va);
#ifdef PMAP_DIAGNOSTIC
	if (va >= KvaEnd)
		panic("pmap_enter: toobig");
	if ((va >= UPT_MIN_ADDRESS) && (va < UPT_MAX_ADDRESS))
		panic("pmap_enter: invalid to pmap_enter page table pages (va: 0x%lx)", va);
#endif
	if (va < UPT_MAX_ADDRESS && pmap == &kernel_pmap) {
		kprintf("Warning: pmap_enter called on UVA with kernel_pmap\n");
#ifdef DDB
		db_print_backtrace();
#endif
	}
	if (va >= UPT_MAX_ADDRESS && pmap != &kernel_pmap) {
		kprintf("Warning: pmap_enter called on KVA without kernel_pmap\n");
#ifdef DDB
		db_print_backtrace();
#endif
	}

	/*
	 * In the case that a page table page is not
	 * resident, we are creating it here.
	 */
	if (va < VM_MAX_USER_ADDRESS)
		mpte = pmap_allocpte(pmap, va);
	else
		mpte = NULL;

	pmap_inval_init(&info);
	pde = pmap_pde(pmap, va);
	if (pde != NULL && (*pde & PG_V) != 0) {
		if ((*pde & PG_PS) != 0)
			panic("pmap_enter: attempted pmap_enter on 2MB page");
		pte = pmap_pde_to_pte(pde, va);
	} else
		panic("pmap_enter: invalid page directory va=%#lx", va);

	KKASSERT(pte != NULL);
	pa = VM_PAGE_TO_PHYS(m);
	KKASSERT(pa == (pa & PG_FRAME));
	origpte = *pte;
	opa = origpte & PG_FRAME;

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
		if (wired && ((origpte & PG_W) == 0))
			pmap->pm_stats.wired_count++;
		else if (!wired && (origpte & PG_W))
			pmap->pm_stats.wired_count--;

#if defined(PMAP_DIAGNOSTIC)
		if (pmap_nw_modified(origpte)) {
			kprintf(
	"pmap_enter: modified page not writable: va: 0x%lx, pte: 0x%lx\n",
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
			if ((origpte & PG_M) && pmap_track_modified(va)) {
				vm_page_t om;
				om = PHYS_TO_VM_PAGE(opa);
				vm_page_dirty(om);
			}
			pa |= PG_MANAGED;
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
		err = pmap_remove_pte(pmap, pte, va, &info);
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
		pa |= PG_MANAGED;
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
	newpte = (pt_entry_t) (pa | pte_prot(pmap, prot) | PG_V);

	if (wired)
		newpte |= PG_W;
	if (va < VM_MAX_USER_ADDRESS)
		newpte |= PG_U;
	if (pmap == &kernel_pmap)
		newpte |= pgeflag;

	/*
	 * if the mapping or permission bits are different, we need
	 * to update the pte.
	 */
	if ((origpte & ~(PG_M|PG_A)) != newpte) {
		pmap_inval_add(&info, pmap, va);
		*pte = newpte | PG_A;
		if (newpte & PG_RW)
			vm_page_flag_set(m, PG_WRITEABLE);
	}
	KKASSERT((newpte & PG_MANAGED) == 0 || (m->flags & PG_MAPPED));
	pmap_inval_flush(&info);
}

/*
 * This code works like pmap_enter() but assumes VM_PROT_READ and not-wired.
 * This code also assumes that the pmap has no pre-existing entry for this
 * VA.
 *
 * This code currently may only be used on user pmaps, not kernel_pmap.
 */
static void
pmap_enter_quick(pmap_t pmap, vm_offset_t va, vm_page_t m)
READY1
{
	pt_entry_t *pte;
	vm_paddr_t pa;
	vm_page_t mpte;
	vm_pindex_t ptepindex;
	pd_entry_t *ptepa;
	pmap_inval_info info;

	pmap_inval_init(&info);

	if (va < UPT_MAX_ADDRESS && pmap == &kernel_pmap) {
		kprintf("Warning: pmap_enter_quick called on UVA with kernel_pmap\n");
#ifdef DDB
		db_print_backtrace();
#endif
	}
	if (va >= UPT_MAX_ADDRESS && pmap != &kernel_pmap) {
		kprintf("Warning: pmap_enter_quick called on KVA without kernel_pmap\n");
#ifdef DDB
		db_print_backtrace();
#endif
	}

	KKASSERT(va < UPT_MIN_ADDRESS);	/* assert used on user pmaps only */

	/*
	 * Calculate the page table page (mpte), allocating it if necessary.
	 *
	 * A held page table page (mpte), or NULL, is passed onto the
	 * section following.
	 */
	if (va < VM_MAX_USER_ADDRESS) {
		/*
		 * Calculate pagetable page index
		 */
		ptepindex = pmap_pde_pindex(va);

		do {
			/*
			 * Get the page directory entry
			 */
			ptepa = pmap_pde(pmap, va);

			/*
			 * If the page table page is mapped, we just increment
			 * the hold count, and activate it.
			 */
			if (ptepa && (*ptepa & PG_V) != 0) {
				if (*ptepa & PG_PS)
					panic("pmap_enter_quick: unexpected mapping into 2MB page");
//				if (pmap->pm_ptphint &&
//				    (pmap->pm_ptphint->pindex == ptepindex)) {
//					mpte = pmap->pm_ptphint;
//				} else {
					mpte = pmap_page_lookup( pmap->pm_pteobj, ptepindex);
					pmap->pm_ptphint = mpte;
//				}
				if (mpte)
					mpte->hold_count++;
			} else {
				mpte = _pmap_allocpte(pmap, ptepindex);
			}
		} while (mpte == NULL);
	} else {
		mpte = NULL;
		/* this code path is not yet used */
	}

	/*
	 * With a valid (and held) page directory page, we can just use
	 * vtopte() to get to the pte.  If the pte is already present
	 * we do not disturb it.
	 */
	pte = vtopte(va);
	if (*pte & PG_V) {
		if (mpte)
			pmap_unwire_pte_hold(pmap, va, mpte, &info);
		pa = VM_PAGE_TO_PHYS(m);
		KKASSERT(((*pte ^ pa) & PG_FRAME) == 0);
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
		*pte = pa | PG_V | PG_U;
	else
		*pte = pa | PG_V | PG_U | PG_MANAGED;
/*	pmap_inval_add(&info, pmap, va); shouldn't be needed inval->valid */
	pmap_inval_flush(&info);
}

/*
 * Make a temporary mapping for a physical address.  This is only intended
 * to be used for panic dumps.
 */
/* JG Needed on amd64? */
void *
pmap_kenter_temporary(vm_paddr_t pa, int i)
READY2
{
	pmap_kenter((vm_offset_t)crashdumpmap + (i * PAGE_SIZE), pa);
	return ((void *)crashdumpmap);
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
READY1
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

	psize = amd64_btop(size);

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
READY1
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
		pmap_enter_quick(info->pmap,
				 info->addr + amd64_ptob(rel_index), p);
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
READY0
{
	int i;
	vm_offset_t starta;
	vm_offset_t addr;
	vm_pindex_t pindex;
	vm_page_t m;
	vm_object_t object;
	struct lwp *lp;

	/*
	 * We do not currently prefault mappings that use virtual page
	 * tables.  We do not prefault foreign pmaps.
	 */
	if (entry->maptype == VM_MAPTYPE_VPAGETABLE)
		return;
	lp = curthread->td_lwp;
	if (lp == NULL || (pmap != vmspace_pmap(lp->lwp_vmspace)))
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
	crit_enter();
	for (i = 0; i < PAGEORDER_SIZE; i++) {
		vm_object_t lobject;
		pt_entry_t *pte;

		addr = addra + pmap_prefault_pageorder[i];
		if (addr > addra + (PFFOR * PAGE_SIZE))
			addr = 0;

		if (addr < starta || addr >= entry->end)
			continue;

		if ((*pmap_pde(pmap, addr)) == 0)
			continue;

		pte = vtopte(addr);
		if (*pte)
			continue;

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

		if (((m->valid & VM_PAGE_BITS_ALL) == VM_PAGE_BITS_ALL) &&
			(m->busy == 0) &&
		    (m->flags & (PG_BUSY | PG_FICTITIOUS)) == 0) {

			if ((m->queue - m->pc) == PQ_CACHE) {
				vm_page_deactivate(m);
			}
			vm_page_busy(m);
			pmap_enter_quick(pmap, addr, m);
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
READY0
{
	pt_entry_t *pte;

	if (pmap == NULL)
		return;

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
#ifdef SMP
	if (wired)
		atomic_set_int(pte, PG_W);
	else
		atomic_clear_int(pte, PG_W);
#else
	if (wired)
		atomic_set_int_nonlocked(pte, PG_W);
	else
		atomic_clear_int_nonlocked(pte, PG_W);
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
READY0
{
	pmap_inval_info info;
	vm_offset_t addr;
	vm_offset_t end_addr = src_addr + len;
	vm_offset_t pdnxt;
	pd_entry_t src_frame, dst_frame;
	vm_page_t m;

	if (dst_addr != src_addr)
		return;
	/*
	 * XXX BUGGY.  Amoung other things srcmpte is assumed to remain
	 * valid through blocking calls, and that's just not going to
	 * be the case.
	 *
	 * FIXME!
	 */
	return;

#if JGPMAP32
	src_frame = src_pmap->pm_pdir[PTDPTDI] & PG_FRAME;
	if (src_frame != (PTDpde & PG_FRAME)) {
		return;
	}

	dst_frame = dst_pmap->pm_pdir[PTDPTDI] & PG_FRAME;
	if (dst_frame != (APTDpde & PG_FRAME)) {
		APTDpde = (pd_entry_t) (dst_frame | PG_RW | PG_V);
		/* The page directory is not shared between CPUs */
		cpu_invltlb();
	}
#endif
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
		pt_entry_t *src_pte, *dst_pte;
		vm_page_t dstmpte, srcmpte;
		vm_offset_t srcptepaddr;
		vm_pindex_t ptepindex;

		if (addr >= UPT_MIN_ADDRESS)
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

#if JGPMAP32
		srcptepaddr = (vm_offset_t) src_pmap->pm_pdir[ptepindex];
#endif
		if (srcptepaddr == 0)
			continue;
			
		if (srcptepaddr & PG_PS) {
#if JGPMAP32
			if (dst_pmap->pm_pdir[ptepindex] == 0) {
				dst_pmap->pm_pdir[ptepindex] = (pd_entry_t) srcptepaddr;
				dst_pmap->pm_stats.resident_count += NBPDR / PAGE_SIZE;
			}
#endif
			continue;
		}

		srcmpte = vm_page_lookup(src_pmap->pm_pteobj, ptepindex);
		if ((srcmpte == NULL) || (srcmpte->hold_count == 0) ||
		    (srcmpte->flags & PG_BUSY)) {
			continue;
		}

		if (pdnxt > end_addr)
			pdnxt = end_addr;

		src_pte = vtopte(addr);
#if JGPMAP32
		dst_pte = avtopte(addr);
#endif
		while (addr < pdnxt) {
			pt_entry_t ptetemp;

			ptetemp = *src_pte;
			/*
			 * we only virtual copy managed pages
			 */
			if ((ptetemp & PG_MANAGED) != 0) {
				/*
				 * We have to check after allocpte for the
				 * pte still being around...  allocpte can
				 * block.
				 *
				 * pmap_allocpte() can block.  If we lose
				 * our page directory mappings we stop.
				 */
				dstmpte = pmap_allocpte(dst_pmap, addr);

#if JGPMAP32
				if (src_frame != (PTDpde & PG_FRAME) ||
				    dst_frame != (APTDpde & PG_FRAME)
				) {
					kprintf("WARNING: pmap_copy: detected and corrected race\n");
					pmap_unwire_pte_hold(dst_pmap, dstmpte, &info);
					goto failed;
				} else if ((*dst_pte == 0) &&
					   (ptetemp = *src_pte) != 0 &&
					   (ptetemp & PG_MANAGED)) {
					/*
					 * Clear the modified and
					 * accessed (referenced) bits
					 * during the copy.
					 */
					m = PHYS_TO_VM_PAGE(ptetemp);
					*dst_pte = ptetemp & ~(PG_M | PG_A);
					++dst_pmap->pm_stats.resident_count;
					pmap_insert_entry(dst_pmap, addr,
						dstmpte, m);
					KKASSERT(m->flags & PG_MAPPED);
	 			} else {
					kprintf("WARNING: pmap_copy: dst_pte race detected and corrected\n");
					pmap_unwire_pte_hold(dst_pmap, dstmpte, &info);
					goto failed;
				}
#endif
				if (dstmpte->hold_count >= srcmpte->hold_count)
					break;
			}
			addr += PAGE_SIZE;
			src_pte++;
			dst_pte++;
		}
	}
failed:
	crit_exit();
	pmap_inval_flush(&info);
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
READY1
{
	vm_offset_t va = PHYS_TO_DMAP(phys);

	pagezero((void *)va);
}

/*
 * pmap_page_assertzero:
 *
 *	Assert that a page is empty, panic if it isn't.
 */
void
pmap_page_assertzero(vm_paddr_t phys)
READY1
{
	struct mdglobaldata *gd = mdcpu;
	int i;

	crit_enter();
	vm_offset_t virt = PHYS_TO_DMAP(phys);

	for (i = 0; i < PAGE_SIZE; i += sizeof(int)) {
	    if (*(int *)((char *)virt + i) != 0) {
		panic("pmap_page_assertzero() @ %p not zero!\n",
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
READY1
{
	struct mdglobaldata *gd = mdcpu;

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
READY1
{
	vm_offset_t src_virt, dst_virt;

	crit_enter();
	src_virt = PHYS_TO_DMAP(src);
	dst_virt = PHYS_TO_DMAP(dst);
	bcopy(src_virt, dst_virt, PAGE_SIZE);
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
READY1
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
 * Returns true if the pmap's pv is one of the first
 * 16 pvs linked to from this page.  This count may
 * be changed upwards or downwards in the future; it
 * is only necessary that true be returned for a small
 * subset of pmaps for proper page aging.
 */
boolean_t
pmap_page_exists_quick(pmap_t pmap, vm_page_t m)
READY2
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
READY1
{
	struct lwp *lp;
	pt_entry_t *pte, tpte;
	pv_entry_t pv, npv;
	vm_page_t m;
	pmap_inval_info info;
	int iscurrentpmap;
	int save_generation;

	lp = curthread->td_lwp;
	if (lp && pmap == vmspace_pmap(lp->lwp_vmspace))
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

		KKASSERT(pmap == pv->pv_pmap);

		if (iscurrentpmap)
			pte = vtopte(pv->pv_va);
		else
			pte = pmap_pte_quick(pmap, pv->pv_va);
		if (pmap->pm_active)
			pmap_inval_add(&info, pmap, pv->pv_va);

		/*
		 * We cannot remove wired pages from a process' mapping
		 * at this time
		 */
		if (*pte & PG_W) {
			npv = TAILQ_NEXT(pv, pv_plist);
			continue;
		}
		tpte = pte_load_clear(pte);

		m = PHYS_TO_VM_PAGE(tpte & PG_FRAME);

		KASSERT(m < &vm_page_array[vm_page_array_size],
			("pmap_remove_pages: bad tpte %lx", tpte));

		KKASSERT(pmap->pm_stats.resident_count > 0);
		--pmap->pm_stats.resident_count;

		/*
		 * Update the vm_page_t clean and reference bits.
		 */
		if (tpte & PG_M) {
			vm_page_dirty(m);
		}

		npv = TAILQ_NEXT(pv, pv_plist);
		TAILQ_REMOVE(&pmap->pm_pvlist, pv, pv_plist);
		save_generation = ++pmap->pm_generation;

		m->md.pv_list_count--;
		TAILQ_REMOVE(&m->md.pv_list, pv, pv_list);
		if (TAILQ_EMPTY(&m->md.pv_list))
			vm_page_flag_clear(m, PG_MAPPED | PG_WRITEABLE);

		pmap_unuse_pt(pmap, pv->pv_va, pv->pv_ptem, &info);
		free_pv_entry(pv);

		/*
		 * Restart the scan if we blocked during the unuse or free
		 * calls and other removals were made.
		 */
		if (save_generation != pmap->pm_generation) {
			kprintf("Warning: pmap_remove_pages race-A avoided\n");
			pv = TAILQ_FIRST(&pmap->pm_pvlist);
		}
	}
	pmap_inval_flush(&info);
	crit_exit();
}

/*
 * pmap_testbit tests bits in pte's
 * note that the testbit/clearbit routines are inline,
 * and a lot of things compile-time evaluate.
 */
static boolean_t
pmap_testbit(vm_page_t m, int bit)
READY1
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
		if (bit & (PG_A|PG_M)) {
			if (!pmap_track_modified(pv->pv_va))
				continue;
		}

#if defined(PMAP_DIAGNOSTIC)
		if (pv->pv_pmap == NULL) {
			kprintf("Null pmap (tb) at va: 0x%lx\n", pv->pv_va);
			continue;
		}
#endif
		pte = pmap_pte_quick(pv->pv_pmap, pv->pv_va);
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
pmap_clearbit(vm_page_t m, int bit)
READY1
{
	struct pmap_inval_info info;
	pv_entry_t pv;
	pt_entry_t *pte;
	pt_entry_t pbits;

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
		if (bit == PG_RW) {
			if (!pmap_track_modified(pv->pv_va))
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
		 * clear PG_A or PG_M safely but we need to synchronize
		 * with the target cpus when we mess with PG_RW.
		 *
		 * We do not have to force synchronization when clearing
		 * PG_M even for PTEs generated via virtual memory maps,
		 * because the virtual kernel will invalidate the pmap
		 * entry when/if it needs to resynchronize the Modify bit.
		 */
		if (bit & PG_RW)
			pmap_inval_add(&info, pv->pv_pmap, pv->pv_va);
		pte = pmap_pte_quick(pv->pv_pmap, pv->pv_va);
again:
		pbits = *pte;
		if (pbits & bit) {
			if (bit == PG_RW) {
				if (pbits & PG_M) {
					vm_page_dirty(m);
					atomic_clear_long(pte, PG_M|PG_RW);
				} else {
					/*
					 * The cpu may be trying to set PG_M
					 * simultaniously with our clearing
					 * of PG_RW.
					 */
					if (!atomic_cmpset_long(pte, pbits,
							       pbits & ~PG_RW))
						goto again;
				}
			} else if (bit == PG_M) {
				/*
				 * We could also clear PG_RW here to force
				 * a fault on write to redetect PG_M for
				 * virtual kernels, but it isn't necessary
				 * since virtual kernels invalidate the pte 
				 * when they clear the VPTE_M bit in their
				 * virtual page tables.
				 */
				atomic_clear_long(pte, PG_M);
			} else {
				atomic_clear_long(pte, bit);
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
READY1
{
	/* JG NX support? */
	if ((prot & VM_PROT_WRITE) == 0) {
		if (prot & (VM_PROT_READ | VM_PROT_EXECUTE)) {
			pmap_clearbit(m, PG_RW);
			vm_page_flag_clear(m, PG_WRITEABLE);
		} else {
			pmap_remove_all(m);
		}
	}
}

vm_paddr_t
pmap_phys_address(vm_pindex_t ppn)
READY2
{
	return (amd64_ptob(ppn));
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
READY1
{
	pv_entry_t pv, pvf, pvn;
	pt_entry_t *pte;
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

			pte = pmap_pte_quick(pv->pv_pmap, pv->pv_va);

			if (pte && (*pte & PG_A)) {
#ifdef SMP
				atomic_clear_long(pte, PG_A);
#else
				atomic_clear_long_nonlocked(pte, PG_A);
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
READY2
{
	return pmap_testbit(m, PG_M);
}

/*
 *	Clear the modify bits on the specified physical page.
 */
void
pmap_clear_modify(vm_page_t m)
READY2
{
	pmap_clearbit(m, PG_M);
}

/*
 *	pmap_clear_reference:
 *
 *	Clear the reference bit on the specified physical page.
 */
void
pmap_clear_reference(vm_page_t m)
READY2
{
	pmap_clearbit(m, PG_A);
}

/*
 * Miscellaneous support routines follow
 */

static void
i386_protection_init(void)
READY0
{
	int *kp, prot;

	/* JG NX support may go here; No VM_PROT_EXECUTE ==> set NX bit  */
	kp = protection_codes;
	for (prot = 0; prot < 8; prot++) {
		switch (prot) {
		case VM_PROT_NONE | VM_PROT_NONE | VM_PROT_NONE:
			/*
			 * Read access is also 0. There isn't any execute bit,
			 * so just make it readable.
			 */
		case VM_PROT_READ | VM_PROT_NONE | VM_PROT_NONE:
		case VM_PROT_READ | VM_PROT_NONE | VM_PROT_EXECUTE:
		case VM_PROT_NONE | VM_PROT_NONE | VM_PROT_EXECUTE:
			*kp++ = 0;
			break;
		case VM_PROT_NONE | VM_PROT_WRITE | VM_PROT_NONE:
		case VM_PROT_NONE | VM_PROT_WRITE | VM_PROT_EXECUTE:
		case VM_PROT_READ | VM_PROT_WRITE | VM_PROT_NONE:
		case VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXECUTE:
			*kp++ = PG_RW;
			break;
		}
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
READY1
{
	vm_offset_t va, tmpva, offset;
	pt_entry_t *pte;

	offset = pa & PAGE_MASK;
	size = roundup(offset + size, PAGE_SIZE);

	va = kmem_alloc_nofault(&kernel_map, size);
	if (va == 0)
		panic("pmap_mapdev: Couldn't alloc kernel virtual memory");

	pa = pa & ~PAGE_MASK;
	for (tmpva = va; size > 0;) {
		pte = vtopte(tmpva);
		*pte = pa | PG_RW | PG_V; /* | pgeflag; */
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
READY1
{
	vm_offset_t base, offset;

	base = va & ~PAGE_MASK;
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
READY0
{
	pt_entry_t *ptep, pte;
	vm_page_t m;
	int val = 0;
	
	ptep = pmap_pte(pmap, addr);
	if (ptep == 0) {
		return 0;
	}

	if ((pte = *ptep) != 0) {
		vm_offset_t pa;

		val = MINCORE_INCORE;
		if ((pte & PG_MANAGED) == 0)
			return val;

		pa = pte & PG_FRAME;

		m = PHYS_TO_VM_PAGE(pa);

		/*
		 * Modified by us
		 */
		if (pte & PG_M)
			val |= MINCORE_MODIFIED|MINCORE_MODIFIED_OTHER;
		/*
		 * Modified by someone
		 */
		else if (m->dirty || pmap_is_modified(m))
			val |= MINCORE_MODIFIED_OTHER;
		/*
		 * Referenced by us
		 */
		if (pte & PG_A)
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

/*
 * Replace p->p_vmspace with a new one.  If adjrefs is non-zero the new
 * vmspace will be ref'd and the old one will be deref'd.
 *
 * The vmspace for all lwps associated with the process will be adjusted
 * and cr3 will be reloaded if any lwp is the current lwp.
 */
void
pmap_replacevm(struct proc *p, struct vmspace *newvm, int adjrefs)
READY2
{
	struct vmspace *oldvm;
	struct lwp *lp;

	crit_enter();
	oldvm = p->p_vmspace;
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

/*
 * Set the vmspace for a LWP.  The vmspace is almost universally set the
 * same as the process vmspace, but virtual kernels need to swap out contexts
 * on a per-lwp basis.
 */
void
pmap_setlwpvm(struct lwp *lp, struct vmspace *newvm)
READY1
{
	struct vmspace *oldvm;
	struct pmap *pmap;

	crit_enter();
	oldvm = lp->lwp_vmspace;

	if (oldvm != newvm) {
		lp->lwp_vmspace = newvm;
		if (curthread->td_lwp == lp) {
			pmap = vmspace_pmap(newvm);
#if defined(SMP)
			atomic_set_int(&pmap->pm_active, 1 << mycpu->gd_cpuid);
#else
			pmap->pm_active |= 1;
#endif
#if defined(SWTCH_OPTIM_STATS)
			tlb_flush_count++;
#endif
			curthread->td_pcb->pcb_cr3 = vtophys(pmap->pm_pml4);
			load_cr3(curthread->td_pcb->pcb_cr3);
			pmap = vmspace_pmap(oldvm);
#if defined(SMP)
			atomic_clear_int(&pmap->pm_active,
					  1 << mycpu->gd_cpuid);
#else
			pmap->pm_active &= ~1;
#endif
		}
	}
	crit_exit();
}

vm_offset_t
pmap_addr_hint(vm_object_t obj, vm_offset_t addr, vm_size_t size)
READY0
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
READY0
{
	vm_offset_t va;
	unsigned i, j;
	pt_entry_t *ptep;

	if (pm == &kernel_pmap)
		return;
	crit_enter();
	for (i = 0; i < NPDEPG; i++) {
#if JGPMAP32
		if (pm->pm_pdir[i]) {
			for (j = 0; j < NPTEPG; j++) {
				va = (i << PDRSHIFT) + (j << PAGE_SHIFT);
				if (pm == &kernel_pmap && va < KERNBASE)
					continue;
				if (pm != &kernel_pmap && va > UPT_MAX_ADDRESS)
					continue;
				ptep = pmap_pte_quick(pm, va);
				if (pmap_pte_v(ptep))
					kprintf("%lx:%lx ", va, *ptep);
			};
		}
#endif
	}
	crit_exit();

}

void
pmap_pvdump(vm_paddr_t pa)
READY0
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
