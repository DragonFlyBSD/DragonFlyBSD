/*
 * Copyright (c) 1991 Regents of the University of California.
 * Copyright (c) 1994 John S. Dyson
 * Copyright (c) 1994 David Greenman
 * Copyright (c) 2003 Peter Wemm
 * Copyright (c) 2005-2008 Alan L. Cox <alc@cs.rice.edu>
 * Copyright (c) 2008, 2009 The DragonFly Project.
 * Copyright (c) 2008, 2009 Jordan Gordeev.
 * Copyright (c) 2011-2012 Matthew Dillon
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
 */
/*
 * Manage physical address maps for x86-64 systems.
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
#include <sys/spinlock2.h>
#include <vm/vm_page2.h>

#include <machine/cputypes.h>
#include <machine/md_var.h>
#include <machine/specialreg.h>
#include <machine/smp.h>
#include <machine_base/apic/apicreg.h>
#include <machine/globaldata.h>
#include <machine/pmap.h>
#include <machine/pmap_inval.h>
#include <machine/inttypes.h>

#include <ddb/ddb.h>

#define PMAP_KEEP_PDIRS
#ifndef PMAP_SHPGPERPROC
#define PMAP_SHPGPERPROC 2000
#endif

#if defined(DIAGNOSTIC)
#define PMAP_DIAGNOSTIC
#endif

#define MINPV 2048

/*
 * pmap debugging will report who owns a pv lock when blocking.
 */
#ifdef PMAP_DEBUG

#define PMAP_DEBUG_DECL		,const char *func, int lineno
#define PMAP_DEBUG_ARGS		, __func__, __LINE__
#define PMAP_DEBUG_COPY		, func, lineno

#define pv_get(pmap, pindex)		_pv_get(pmap, pindex		\
							PMAP_DEBUG_ARGS)
#define pv_lock(pv)			_pv_lock(pv			\
							PMAP_DEBUG_ARGS)
#define pv_hold_try(pv)			_pv_hold_try(pv			\
							PMAP_DEBUG_ARGS)
#define pv_alloc(pmap, pindex, isnewp)	_pv_alloc(pmap, pindex, isnewp	\
							PMAP_DEBUG_ARGS)

#else

#define PMAP_DEBUG_DECL
#define PMAP_DEBUG_ARGS
#define PMAP_DEBUG_COPY

#define pv_get(pmap, pindex)		_pv_get(pmap, pindex)
#define pv_lock(pv)			_pv_lock(pv)
#define pv_hold_try(pv)			_pv_hold_try(pv)
#define pv_alloc(pmap, pindex, isnewp)	_pv_alloc(pmap, pindex, isnewp)

#endif

/*
 * Get PDEs and PTEs for user/kernel address space
 */
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

MALLOC_DEFINE(M_OBJPMAP, "objpmap", "pmaps associated with VM objects");

vm_paddr_t avail_start;		/* PA of first available physical page */
vm_paddr_t avail_end;		/* PA of last available physical page */
vm_offset_t virtual2_start;	/* cutout free area prior to kernel start */
vm_offset_t virtual2_end;
vm_offset_t virtual_start;	/* VA of first avail page (after kernel bss) */
vm_offset_t virtual_end;	/* VA of last avail page (end of kernel AS) */
vm_offset_t KvaStart;		/* VA start of KVA space */
vm_offset_t KvaEnd;		/* VA end of KVA space (non-inclusive) */
vm_offset_t KvaSize;		/* max size of kernel virtual address space */
static boolean_t pmap_initialized = FALSE;	/* Has pmap_init completed? */
static int pgeflag;		/* PG_G or-in */
static int pseflag;		/* PG_PS or-in */
uint64_t PatMsr;

static int ndmpdp;
static vm_paddr_t dmaplimit;
static int nkpt;
vm_offset_t kernel_vm_end = VM_MIN_KERNEL_ADDRESS;

#define PAT_INDEX_SIZE  8
static pt_entry_t pat_pte_index[PAT_INDEX_SIZE];	/* PAT -> PG_ bits */
/*static pt_entry_t pat_pde_index[PAT_INDEX_SIZE];*/	/* PAT -> PG_ bits */

static uint64_t KPTbase;
static uint64_t KPTphys;
static uint64_t	KPDphys;	/* phys addr of kernel level 2 */
static uint64_t	KPDbase;	/* phys addr of kernel level 2 @ KERNBASE */
uint64_t KPDPphys;	/* phys addr of kernel level 3 */
uint64_t KPML4phys;	/* phys addr of kernel level 4 */

static uint64_t	DMPDphys;	/* phys addr of direct mapped level 2 */
static uint64_t	DMPDPphys;	/* phys addr of direct mapped level 3 */

/*
 * Data for the pv entry allocation mechanism
 */
static vm_zone_t pvzone;
static struct vm_zone pvzone_store;
static struct vm_object pvzone_obj;
static int pv_entry_max=0, pv_entry_high_water=0;
static int pmap_pagedaemon_waken = 0;
static struct pv_entry *pvinit;

/*
 * All those kernel PT submaps that BSD is so fond of
 */
pt_entry_t *CMAP1 = NULL, *ptmmap;
caddr_t CADDR1 = NULL, ptvmmap = NULL;
static pt_entry_t *msgbufmap;
struct msgbuf *msgbufp=NULL;

/*
 * Crashdump maps.
 */
static pt_entry_t *pt_crashdumpmap;
static caddr_t crashdumpmap;

static int pmap_yield_count = 64;
SYSCTL_INT(_machdep, OID_AUTO, pmap_yield_count, CTLFLAG_RW,
    &pmap_yield_count, 0, "Yield during init_pt/release");
static int pmap_mmu_optimize = 0;
SYSCTL_INT(_machdep, OID_AUTO, pmap_mmu_optimize, CTLFLAG_RW,
    &pmap_mmu_optimize, 0, "Share page table pages when possible");

#define DISABLE_PSE

static void pv_hold(pv_entry_t pv);
static int _pv_hold_try(pv_entry_t pv
				PMAP_DEBUG_DECL);
static void pv_drop(pv_entry_t pv);
static void _pv_lock(pv_entry_t pv
				PMAP_DEBUG_DECL);
static void pv_unlock(pv_entry_t pv);
static pv_entry_t _pv_alloc(pmap_t pmap, vm_pindex_t pindex, int *isnew
				PMAP_DEBUG_DECL);
static pv_entry_t _pv_get(pmap_t pmap, vm_pindex_t pindex
				PMAP_DEBUG_DECL);
static pv_entry_t pv_get_try(pmap_t pmap, vm_pindex_t pindex, int *errorp);
static pv_entry_t pv_find(pmap_t pmap, vm_pindex_t pindex);
static void pv_put(pv_entry_t pv);
static void pv_free(pv_entry_t pv);
static void *pv_pte_lookup(pv_entry_t pv, vm_pindex_t pindex);
static pv_entry_t pmap_allocpte(pmap_t pmap, vm_pindex_t ptepindex,
		      pv_entry_t *pvpp);
static pv_entry_t pmap_allocpte_seg(pmap_t pmap, vm_pindex_t ptepindex,
		      pv_entry_t *pvpp, vm_map_entry_t entry, vm_offset_t va);
static void pmap_remove_pv_pte(pv_entry_t pv, pv_entry_t pvp,
		      struct pmap_inval_info *info);
static vm_page_t pmap_remove_pv_page(pv_entry_t pv);
static int pmap_release_pv(pv_entry_t pv, pv_entry_t pvp);

struct pmap_scan_info;
static void pmap_remove_callback(pmap_t pmap, struct pmap_scan_info *info,
		      pv_entry_t pte_pv, pv_entry_t pt_pv, int sharept,
		      vm_offset_t va, pt_entry_t *ptep, void *arg __unused);
static void pmap_protect_callback(pmap_t pmap, struct pmap_scan_info *info,
		      pv_entry_t pte_pv, pv_entry_t pt_pv, int sharept,
		      vm_offset_t va, pt_entry_t *ptep, void *arg __unused);

static void i386_protection_init (void);
static void create_pagetables(vm_paddr_t *firstaddr);
static void pmap_remove_all (vm_page_t m);
static boolean_t pmap_testbit (vm_page_t m, int bit);

static pt_entry_t * pmap_pte_quick (pmap_t pmap, vm_offset_t va);
static vm_offset_t pmap_kmem_choose(vm_offset_t addr);

static unsigned pdir4mb;

static int
pv_entry_compare(pv_entry_t pv1, pv_entry_t pv2)
{
	if (pv1->pv_pindex < pv2->pv_pindex)
		return(-1);
	if (pv1->pv_pindex > pv2->pv_pindex)
		return(1);
	return(0);
}

RB_GENERATE2(pv_entry_rb_tree, pv_entry, pv_entry,
             pv_entry_compare, vm_pindex_t, pv_pindex);

/*
 * Move the kernel virtual free pointer to the next
 * 2MB.  This is used to help improve performance
 * by using a large (2MB) page for much of the kernel
 * (.text, .data, .bss)
 */
static
vm_offset_t
pmap_kmem_choose(vm_offset_t addr)
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
 */
static __inline pt_entry_t *pmap_pte(pmap_t pmap, vm_offset_t va);

static
pt_entry_t *
pmap_pte_quick(pmap_t pmap, vm_offset_t va)
{
	return pmap_pte(pmap, va);
}

/*
 * Returns the pindex of a page table entry (representing a terminal page).
 * There are NUPTE_TOTAL page table entries possible (a huge number)
 *
 * x86-64 has a 48-bit address space, where bit 47 is sign-extended out.
 * We want to properly translate negative KVAs.
 */
static __inline
vm_pindex_t
pmap_pte_pindex(vm_offset_t va)
{
	return ((va >> PAGE_SHIFT) & (NUPTE_TOTAL - 1));
}

/*
 * Returns the pindex of a page table.
 */
static __inline
vm_pindex_t
pmap_pt_pindex(vm_offset_t va)
{
	return (NUPTE_TOTAL + ((va >> PDRSHIFT) & (NUPT_TOTAL - 1)));
}

/*
 * Returns the pindex of a page directory.
 */
static __inline
vm_pindex_t
pmap_pd_pindex(vm_offset_t va)
{
	return (NUPTE_TOTAL + NUPT_TOTAL +
		((va >> PDPSHIFT) & (NUPD_TOTAL - 1)));
}

static __inline
vm_pindex_t
pmap_pdp_pindex(vm_offset_t va)
{
	return (NUPTE_TOTAL + NUPT_TOTAL + NUPD_TOTAL +
		((va >> PML4SHIFT) & (NUPDP_TOTAL - 1)));
}

static __inline
vm_pindex_t
pmap_pml4_pindex(void)
{
	return (NUPTE_TOTAL + NUPT_TOTAL + NUPD_TOTAL + NUPDP_TOTAL);
}

/*
 * Return various clipped indexes for a given VA
 *
 * Returns the index of a pte in a page table, representing a terminal
 * page.
 */
static __inline
vm_pindex_t
pmap_pte_index(vm_offset_t va)
{
	return ((va >> PAGE_SHIFT) & ((1ul << NPTEPGSHIFT) - 1));
}

/*
 * Returns the index of a pt in a page directory, representing a page
 * table.
 */
static __inline
vm_pindex_t
pmap_pt_index(vm_offset_t va)
{
	return ((va >> PDRSHIFT) & ((1ul << NPDEPGSHIFT) - 1));
}

/*
 * Returns the index of a pd in a page directory page, representing a page
 * directory.
 */
static __inline
vm_pindex_t
pmap_pd_index(vm_offset_t va)
{
	return ((va >> PDPSHIFT) & ((1ul << NPDPEPGSHIFT) - 1));
}

/*
 * Returns the index of a pdp in the pml4 table, representing a page
 * directory page.
 */
static __inline
vm_pindex_t
pmap_pdp_index(vm_offset_t va)
{
	return ((va >> PML4SHIFT) & ((1ul << NPML4EPGSHIFT) - 1));
}

/*
 * Generic procedure to index a pte from a pt, pd, or pdp.
 *
 * NOTE: Normally passed pindex as pmap_xx_index().  pmap_xx_pindex() is NOT
 *	 a page table page index but is instead of PV lookup index.
 */
static
void *
pv_pte_lookup(pv_entry_t pv, vm_pindex_t pindex)
{
	pt_entry_t *pte;

	pte = (pt_entry_t *)PHYS_TO_DMAP(VM_PAGE_TO_PHYS(pv->pv_m));
	return(&pte[pindex]);
}

/*
 * Return pointer to PDP slot in the PML4
 */
static __inline
pml4_entry_t *
pmap_pdp(pmap_t pmap, vm_offset_t va)
{
	return (&pmap->pm_pml4[pmap_pdp_index(va)]);
}

/*
 * Return pointer to PD slot in the PDP given a pointer to the PDP
 */
static __inline
pdp_entry_t *
pmap_pdp_to_pd(pml4_entry_t pdp_pte, vm_offset_t va)
{
	pdp_entry_t *pd;

	pd = (pdp_entry_t *)PHYS_TO_DMAP(pdp_pte & PG_FRAME);
	return (&pd[pmap_pd_index(va)]);
}

/*
 * Return pointer to PD slot in the PDP.
 */
static __inline
pdp_entry_t *
pmap_pd(pmap_t pmap, vm_offset_t va)
{
	pml4_entry_t *pdp;

	pdp = pmap_pdp(pmap, va);
	if ((*pdp & PG_V) == 0)
		return NULL;
	return (pmap_pdp_to_pd(*pdp, va));
}

/*
 * Return pointer to PT slot in the PD given a pointer to the PD
 */
static __inline
pd_entry_t *
pmap_pd_to_pt(pdp_entry_t pd_pte, vm_offset_t va)
{
	pd_entry_t *pt;

	pt = (pd_entry_t *)PHYS_TO_DMAP(pd_pte & PG_FRAME);
	return (&pt[pmap_pt_index(va)]);
}

/*
 * Return pointer to PT slot in the PD
 *
 * SIMPLE PMAP NOTE: Simple pmaps (embedded in objects) do not have PDPs,
 *		     so we cannot lookup the PD via the PDP.  Instead we
 *		     must look it up via the pmap.
 */
static __inline
pd_entry_t *
pmap_pt(pmap_t pmap, vm_offset_t va)
{
	pdp_entry_t *pd;
	pv_entry_t pv;
	vm_pindex_t pd_pindex;

	if (pmap->pm_flags & PMAP_FLAG_SIMPLE) {
		pd_pindex = pmap_pd_pindex(va);
		spin_lock(&pmap->pm_spin);
		pv = pv_entry_rb_tree_RB_LOOKUP(&pmap->pm_pvroot, pd_pindex);
		spin_unlock(&pmap->pm_spin);
		if (pv == NULL || pv->pv_m == NULL)
			return NULL;
		return (pmap_pd_to_pt(VM_PAGE_TO_PHYS(pv->pv_m), va));
	} else {
		pd = pmap_pd(pmap, va);
		if (pd == NULL || (*pd & PG_V) == 0)
			 return NULL;
		return (pmap_pd_to_pt(*pd, va));
	}
}

/*
 * Return pointer to PTE slot in the PT given a pointer to the PT
 */
static __inline
pt_entry_t *
pmap_pt_to_pte(pd_entry_t pt_pte, vm_offset_t va)
{
	pt_entry_t *pte;

	pte = (pt_entry_t *)PHYS_TO_DMAP(pt_pte & PG_FRAME);
	return (&pte[pmap_pte_index(va)]);
}

/*
 * Return pointer to PTE slot in the PT
 */
static __inline
pt_entry_t *
pmap_pte(pmap_t pmap, vm_offset_t va)
{
	pd_entry_t *pt;

	pt = pmap_pt(pmap, va);
	if (pt == NULL || (*pt & PG_V) == 0)
		 return NULL;
	if ((*pt & PG_PS) != 0)
		return ((pt_entry_t *)pt);
	return (pmap_pt_to_pte(*pt, va));
}

/*
 * Of all the layers (PTE, PT, PD, PDP, PML4) the best one to cache is
 * the PT layer.  This will speed up core pmap operations considerably.
 */
static __inline
void
pv_cache(pv_entry_t pv, vm_pindex_t pindex)
{
	if (pindex >= pmap_pt_pindex(0) && pindex <= pmap_pd_pindex(0))
		pv->pv_pmap->pm_pvhint = pv;
}


/*
 * KVM - return address of PT slot in PD
 */
static __inline
pd_entry_t *
vtopt(vm_offset_t va)
{
	uint64_t mask = ((1ul << (NPDEPGSHIFT + NPDPEPGSHIFT +
				  NPML4EPGSHIFT)) - 1);

	return (PDmap + ((va >> PDRSHIFT) & mask));
}

/*
 * KVM - return address of PTE slot in PT
 */
static __inline
pt_entry_t *
vtopte(vm_offset_t va)
{
	uint64_t mask = ((1ul << (NPTEPGSHIFT + NPDEPGSHIFT +
				  NPDPEPGSHIFT + NPML4EPGSHIFT)) - 1);

	return (PTmap + ((va >> PAGE_SHIFT) & mask));
}

static uint64_t
allocpages(vm_paddr_t *firstaddr, long n)
{
	uint64_t ret;

	ret = *firstaddr;
	bzero((void *)ret, n * PAGE_SIZE);
	*firstaddr += n * PAGE_SIZE;
	return (ret);
}

static
void
create_pagetables(vm_paddr_t *firstaddr)
{
	long i;		/* must be 64 bits */
	long nkpt_base;
	long nkpt_phys;
	int j;

	/*
	 * We are running (mostly) V=P at this point
	 *
	 * Calculate NKPT - number of kernel page tables.  We have to
	 * accomodoate prealloction of the vm_page_array, dump bitmap,
	 * MSGBUF_SIZE, and other stuff.  Be generous.
	 *
	 * Maxmem is in pages.
	 *
	 * ndmpdp is the number of 1GB pages we wish to map.
	 */
	ndmpdp = (ptoa(Maxmem) + NBPDP - 1) >> PDPSHIFT;
	if (ndmpdp < 4)		/* Minimum 4GB of dirmap */
		ndmpdp = 4;
	KKASSERT(ndmpdp <= NKPDPE * NPDEPG);

	/*
	 * Starting at the beginning of kvm (not KERNBASE).
	 */
	nkpt_phys = (Maxmem * sizeof(struct vm_page) + NBPDR - 1) / NBPDR;
	nkpt_phys += (Maxmem * sizeof(struct pv_entry) + NBPDR - 1) / NBPDR;
	nkpt_phys += ((nkpt + nkpt + 1 + NKPML4E + NKPDPE + NDMPML4E +
		       ndmpdp) + 511) / 512;
	nkpt_phys += 128;

	/*
	 * Starting at KERNBASE - map 2G worth of page table pages.
	 * KERNBASE is offset -2G from the end of kvm.
	 */
	nkpt_base = (NPDPEPG - KPDPI) * NPTEPG;	/* typically 2 x 512 */

	/*
	 * Allocate pages
	 */
	KPTbase = allocpages(firstaddr, nkpt_base);
	KPTphys = allocpages(firstaddr, nkpt_phys);
	KPML4phys = allocpages(firstaddr, 1);
	KPDPphys = allocpages(firstaddr, NKPML4E);
	KPDphys = allocpages(firstaddr, NKPDPE);

	/*
	 * Calculate the page directory base for KERNBASE,
	 * that is where we start populating the page table pages.
	 * Basically this is the end - 2.
	 */
	KPDbase = KPDphys + ((NKPDPE - (NPDPEPG - KPDPI)) << PAGE_SHIFT);

	DMPDPphys = allocpages(firstaddr, NDMPML4E);
	if ((amd_feature & AMDID_PAGE1GB) == 0)
		DMPDphys = allocpages(firstaddr, ndmpdp);
	dmaplimit = (vm_paddr_t)ndmpdp << PDPSHIFT;

	/*
	 * Fill in the underlying page table pages for the area around
	 * KERNBASE.  This remaps low physical memory to KERNBASE.
	 *
	 * Read-only from zero to physfree
	 * XXX not fully used, underneath 2M pages
	 */
	for (i = 0; (i << PAGE_SHIFT) < *firstaddr; i++) {
		((pt_entry_t *)KPTbase)[i] = i << PAGE_SHIFT;
		((pt_entry_t *)KPTbase)[i] |= PG_RW | PG_V | PG_G;
	}

	/*
	 * Now map the initial kernel page tables.  One block of page
	 * tables is placed at the beginning of kernel virtual memory,
	 * and another block is placed at KERNBASE to map the kernel binary,
	 * data, bss, and initial pre-allocations.
	 */
	for (i = 0; i < nkpt_base; i++) {
		((pd_entry_t *)KPDbase)[i] = KPTbase + (i << PAGE_SHIFT);
		((pd_entry_t *)KPDbase)[i] |= PG_RW | PG_V;
	}
	for (i = 0; i < nkpt_phys; i++) {
		((pd_entry_t *)KPDphys)[i] = KPTphys + (i << PAGE_SHIFT);
		((pd_entry_t *)KPDphys)[i] |= PG_RW | PG_V;
	}

	/*
	 * Map from zero to end of allocations using 2M pages as an
	 * optimization.  This will bypass some of the KPTBase pages
	 * above in the KERNBASE area.
	 */
	for (i = 0; (i << PDRSHIFT) < *firstaddr; i++) {
		((pd_entry_t *)KPDbase)[i] = i << PDRSHIFT;
		((pd_entry_t *)KPDbase)[i] |= PG_RW | PG_V | PG_PS | PG_G;
	}

	/*
	 * And connect up the PD to the PDP.  The kernel pmap is expected
	 * to pre-populate all of its PDs.  See NKPDPE in vmparam.h.
	 */
	for (i = 0; i < NKPDPE; i++) {
		((pdp_entry_t *)KPDPphys)[NPDPEPG - NKPDPE + i] =
				KPDphys + (i << PAGE_SHIFT);
		((pdp_entry_t *)KPDPphys)[NPDPEPG - NKPDPE + i] |=
				PG_RW | PG_V | PG_U;
	}

	/*
	 * Now set up the direct map space using either 2MB or 1GB pages
	 * Preset PG_M and PG_A because demotion expects it.
	 *
	 * When filling in entries in the PD pages make sure any excess
	 * entries are set to zero as we allocated enough PD pages
	 */
	if ((amd_feature & AMDID_PAGE1GB) == 0) {
		for (i = 0; i < NPDEPG * ndmpdp; i++) {
			((pd_entry_t *)DMPDphys)[i] = i << PDRSHIFT;
			((pd_entry_t *)DMPDphys)[i] |= PG_RW | PG_V | PG_PS |
						       PG_G | PG_M | PG_A;
		}

		/*
		 * And the direct map space's PDP
		 */
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

	/*
	 * Connect the Direct Map slots up to the PML4
	 */
	for (j = 0; j < NDMPML4E; ++j) {
		((pdp_entry_t *)KPML4phys)[DMPML4I + j] =
			(DMPDPphys + ((vm_paddr_t)j << PML4SHIFT)) |
			PG_RW | PG_V | PG_U;
	}

	/*
	 * Connect the KVA slot up to the PML4
	 */
	((pdp_entry_t *)KPML4phys)[KPML4I] = KPDPphys;
	((pdp_entry_t *)KPML4phys)[KPML4I] |= PG_RW | PG_V | PG_U;
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
{
	vm_offset_t va;
	pt_entry_t *pte;

	KvaStart = VM_MIN_KERNEL_ADDRESS;
	KvaEnd = VM_MAX_KERNEL_ADDRESS;
	KvaSize = KvaEnd - KvaStart;

	avail_start = *firstaddr;

	/*
	 * Create an initial set of page tables to run the kernel in.
	 */
	create_pagetables(firstaddr);

	virtual2_start = KvaStart;
	virtual2_end = PTOV_OFFSET;

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
	kernel_pmap.pm_pml4 = (pdp_entry_t *) (PTOV_OFFSET + KPML4phys);
	kernel_pmap.pm_count = 1;
	kernel_pmap.pm_active = (cpumask_t)-1 & ~CPUMASK_LOCK;
	RB_INIT(&kernel_pmap.pm_pvroot);
	spin_init(&kernel_pmap.pm_spin);
	lwkt_token_init(&kernel_pmap.pm_token, "kpmap_tok");

	/*
	 * Reserve some special page table entries/VA space for temporary
	 * mapping of pages.
	 */
#define	SYSMAP(c, p, v, n)	\
	v = (c)va; va += ((n)*PAGE_SIZE); p = pte; pte += (n);

	va = virtual_start;
	pte = vtopte(va);

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

	/*
	 * PG_G is terribly broken on SMP because we IPI invltlb's in some
	 * cases rather then invl1pg.  Actually, I don't even know why it
	 * works under UP because self-referential page table mappings
	 */
	pgeflag = 0;

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
		ptditmp = *(PTmap + x86_64_btop(KERNBASE));
		ptditmp &= ~(NBPDR - 1);
		ptditmp |= PG_V | PG_RW | PG_PS | PG_U | pgeflag;
		pdir4mb = ptditmp;
	}
#endif
	cpu_invltlb();

	/* Initialize the PAT MSR */
	pmap_init_pat();
}

/*
 * Setup the PAT MSR.
 */
void
pmap_init_pat(void)
{
	uint64_t pat_msr;
	u_long cr0, cr4;

	/*
	 * Default values mapping PATi,PCD,PWT bits at system reset.
	 * The default values effectively ignore the PATi bit by
	 * repeating the encodings for 0-3 in 4-7, and map the PCD
	 * and PWT bit combinations to the expected PAT types.
	 */
	pat_msr = PAT_VALUE(0, PAT_WRITE_BACK) |	/* 000 */
		  PAT_VALUE(1, PAT_WRITE_THROUGH) |	/* 001 */
		  PAT_VALUE(2, PAT_UNCACHED) |		/* 010 */
		  PAT_VALUE(3, PAT_UNCACHEABLE) |	/* 011 */
		  PAT_VALUE(4, PAT_WRITE_BACK) |	/* 100 */
		  PAT_VALUE(5, PAT_WRITE_THROUGH) |	/* 101 */
		  PAT_VALUE(6, PAT_UNCACHED) |		/* 110 */
		  PAT_VALUE(7, PAT_UNCACHEABLE);	/* 111 */
	pat_pte_index[PAT_WRITE_BACK]	= 0;
	pat_pte_index[PAT_WRITE_THROUGH]= 0         | PG_NC_PWT;
	pat_pte_index[PAT_UNCACHED]	= PG_NC_PCD;
	pat_pte_index[PAT_UNCACHEABLE]	= PG_NC_PCD | PG_NC_PWT;
	pat_pte_index[PAT_WRITE_PROTECTED] = pat_pte_index[PAT_UNCACHEABLE];
	pat_pte_index[PAT_WRITE_COMBINING] = pat_pte_index[PAT_UNCACHEABLE];

	if (cpu_feature & CPUID_PAT) {
		/*
		 * If we support the PAT then set-up entries for
		 * WRITE_PROTECTED and WRITE_COMBINING using bit patterns
		 * 4 and 5.
		 */
		pat_msr = (pat_msr & ~PAT_MASK(4)) |
			  PAT_VALUE(4, PAT_WRITE_PROTECTED);
		pat_msr = (pat_msr & ~PAT_MASK(5)) |
			  PAT_VALUE(5, PAT_WRITE_COMBINING);
		pat_pte_index[PAT_WRITE_PROTECTED] = PG_PTE_PAT | 0;
		pat_pte_index[PAT_WRITE_COMBINING] = PG_PTE_PAT | PG_NC_PWT;

		/*
		 * Then enable the PAT
		 */

		/* Disable PGE. */
		cr4 = rcr4();
		load_cr4(cr4 & ~CR4_PGE);

		/* Disable caches (CD = 1, NW = 0). */
		cr0 = rcr0();
		load_cr0((cr0 & ~CR0_NW) | CR0_CD);

		/* Flushes caches and TLBs. */
		wbinvd();
		cpu_invltlb();

		/* Update PAT and index table. */
		wrmsr(MSR_PAT, pat_msr);

		/* Flush caches and TLBs again. */
		wbinvd();
		cpu_invltlb();

		/* Restore caches and PGE. */
		load_cr0(cr0);
		load_cr4(cr4);
		PatMsr = pat_msr;
	}
}

/*
 * Set 4mb pdir for mp startup
 */
void
pmap_set_opt(void)
{
	if (pseflag && (cpu_feature & CPUID_PSE)) {
		load_cr4(rcr4() | CR4_PSE);
		if (pdir4mb && mycpu->gd_cpuid == 0) {	/* only on BSP */
			cpu_invltlb();
		}
	}
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
	 * Allocate memory for random pmap data structures.  Includes the
	 * pv_head_table.
	 */

	for (i = 0; i < vm_page_array_size; i++) {
		vm_page_t m;

		m = &vm_page_array[i];
		TAILQ_INIT(&m->md.pv_list);
	}

	/*
	 * init the pv free list
	 */
	initial_pvs = vm_page_array_size;
	if (initial_pvs < MINPV)
		initial_pvs = MINPV;
	pvzone = &pvzone_store;
	pvinit = (void *)kmem_alloc(&kernel_map,
				    initial_pvs * sizeof (struct pv_entry));
	zbootinit(pvzone, "PV ENTRY", sizeof (struct pv_entry),
		  pvinit, initial_pvs);

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
	int entry_max;

	TUNABLE_INT_FETCH("vm.pmap.shpgperproc", &shpgperproc);
	pv_entry_max = shpgperproc * maxproc + vm_page_array_size;
	TUNABLE_INT_FETCH("vm.pmap.pv_entries", &pv_entry_max);
	pv_entry_high_water = 9 * (pv_entry_max / 10);

	/*
	 * Subtract out pages already installed in the zone (hack)
	 */
	entry_max = pv_entry_max - vm_page_array_size;
	if (entry_max <= 0)
		entry_max = 1;

	zinitna(pvzone, &pvzone_obj, NULL, 0, entry_max, ZONE_INTERRUPT, 1);
}


/***************************************************
 * Low level helper routines.....
 ***************************************************/

/*
 * this routine defines the region(s) of memory that should
 * not be tested for the modified bit.
 */
static __inline
int
pmap_track_modified(vm_pindex_t pindex)
{
	vm_offset_t va = (vm_offset_t)pindex << PAGE_SHIFT;
	if ((va < clean_sva) || (va >= clean_eva)) 
		return 1;
	else
		return 0;
}

/*
 * Extract the physical page address associated with the map/VA pair.
 * The page must be wired for this to work reliably.
 *
 * XXX for the moment we're using pv_find() instead of pv_get(), as
 *     callers might be expecting non-blocking operation.
 */
vm_paddr_t 
pmap_extract(pmap_t pmap, vm_offset_t va)
{
	vm_paddr_t rtval;
	pv_entry_t pt_pv;
	pt_entry_t *ptep;

	rtval = 0;
	if (va >= VM_MAX_USER_ADDRESS) {
		/*
		 * Kernel page directories might be direct-mapped and
		 * there is typically no PV tracking of pte's
		 */
		pd_entry_t *pt;

		pt = pmap_pt(pmap, va);
		if (pt && (*pt & PG_V)) {
			if (*pt & PG_PS) {
				rtval = *pt & PG_PS_FRAME;
				rtval |= va & PDRMASK;
			} else {
				ptep = pmap_pt_to_pte(*pt, va);
				if (*pt & PG_V) {
					rtval = *ptep & PG_FRAME;
					rtval |= va & PAGE_MASK;
				}
			}
		}
	} else {
		/*
		 * User pages currently do not direct-map the page directory
		 * and some pages might not used managed PVs.  But all PT's
		 * will have a PV.
		 */
		pt_pv = pv_find(pmap, pmap_pt_pindex(va));
		if (pt_pv) {
			ptep = pv_pte_lookup(pt_pv, pmap_pte_index(va));
			if (*ptep & PG_V) {
				rtval = *ptep & PG_FRAME;
				rtval |= va & PAGE_MASK;
			}
			pv_drop(pt_pv);
		}
	}
	return rtval;
}

/*
 * Extract the physical page address associated kernel virtual address.
 */
vm_paddr_t
pmap_kextract(vm_offset_t va)
{
	pd_entry_t pt;		/* pt entry in pd */
	vm_paddr_t pa;

	if (va >= DMAP_MIN_ADDRESS && va < DMAP_MAX_ADDRESS) {
		pa = DMAP_TO_PHYS(va);
	} else {
		pt = *vtopt(va);
		if (pt & PG_PS) {
			pa = (pt & PG_PS_FRAME) | (va & PDRMASK);
		} else {
			/*
			 * Beware of a concurrent promotion that changes the
			 * PDE at this point!  For example, vtopte() must not
			 * be used to access the PTE because it would use the
			 * new PDE.  It is, however, safe to use the old PDE
			 * because the page table page is preserved by the
			 * promotion.
			 */
			pa = *pmap_pt_to_pte(pt, va);
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
{
	pt_entry_t *pte;
	pt_entry_t npte;
	pmap_inval_info info;

	pmap_inval_init(&info);				/* XXX remove */
	npte = pa | PG_RW | PG_V | pgeflag;
	pte = vtopte(va);
	pmap_inval_interlock(&info, &kernel_pmap, va);	/* XXX remove */
	*pte = npte;
	pmap_inval_deinterlock(&info, &kernel_pmap);	/* XXX remove */
	pmap_inval_done(&info);				/* XXX remove */
}

/*
 * Routine: pmap_kenter_quick
 * Function:
 *  	Similar to pmap_kenter(), except we only invalidate the
 *  	mapping on the current CPU.
 */
void
pmap_kenter_quick(vm_offset_t va, vm_paddr_t pa)
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
{
	pmap_inval_info info;

	pmap_inval_init(&info);
	pmap_inval_interlock(&info, &kernel_pmap, va);
	pmap_inval_deinterlock(&info, &kernel_pmap);
	pmap_inval_done(&info);
}

void
pmap_kenter_sync_quick(vm_offset_t va)
{
	cpu_invlpg((void *)va);
}

/*
 * remove a page from the kernel pagetables
 */
void
pmap_kremove(vm_offset_t va)
{
	pt_entry_t *pte;
	pmap_inval_info info;

	pmap_inval_init(&info);
	pte = vtopte(va);
	pmap_inval_interlock(&info, &kernel_pmap, va);
	(void)pte_load_clear(pte);
	pmap_inval_deinterlock(&info, &kernel_pmap);
	pmap_inval_done(&info);
}

void
pmap_kremove_quick(vm_offset_t va)
{
	pt_entry_t *pte;
	pte = vtopte(va);
	(void)pte_load_clear(pte);
	cpu_invlpg((void *)va);
}

/*
 * XXX these need to be recoded.  They are not used in any critical path.
 */
void
pmap_kmodify_rw(vm_offset_t va)
{
	atomic_set_long(vtopte(va), PG_RW);
	cpu_invlpg((void *)va);
}

void
pmap_kmodify_nc(vm_offset_t va)
{
	atomic_set_long(vtopte(va), PG_N);
	cpu_invlpg((void *)va);
}

/*
 * Used to map a range of physical addresses into kernel virtual
 * address space during the low level boot, typically to map the
 * dump bitmap, message buffer, and vm_page_array.
 *
 * These mappings are typically made at some pointer after the end of the
 * kernel text+data.
 *
 * We could return PHYS_TO_DMAP(start) here and not allocate any
 * via (*virtp), but then kmem from userland and kernel dumps won't
 * have access to the related pointers.
 */
vm_offset_t
pmap_map(vm_offset_t *virtp, vm_paddr_t start, vm_paddr_t end, int prot)
{
	vm_offset_t va;
	vm_offset_t va_start;

	/*return PHYS_TO_DMAP(start);*/

	va_start = *virtp;
	va = va_start;

	while (start < end) {
		pmap_kenter_quick(va, start);
		va += PAGE_SIZE;
		start += PAGE_SIZE;
	}
	*virtp = va;
	return va_start;
}

void
pmap_invalidate_cache_range(vm_offset_t sva, vm_offset_t eva)
{
	KASSERT((sva & PAGE_MASK) == 0,
	    ("pmap_invalidate_cache_range: sva not page-aligned"));
	KASSERT((eva & PAGE_MASK) == 0,
	    ("pmap_invalidate_cache_range: eva not page-aligned"));

	if (cpu_feature & CPUID_SS) {
		; /* If "Self Snoop" is supported, do nothing. */
	} else {
		/* Globally invalidate caches */
		cpu_wbinvd_on_all_cpus();
	}
}
void
pmap_invalidate_range(pmap_t pmap, vm_offset_t sva, vm_offset_t eva)
{
	smp_invlpg_range(pmap->pm_active, sva, eva);
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
{
	vm_offset_t end_va;

	end_va = va + count * PAGE_SIZE;
		
	while (va < end_va) {
		pt_entry_t *pte;

		pte = vtopte(va);
		*pte = VM_PAGE_TO_PHYS(*m) | PG_RW | PG_V |
			pat_pte_index[(*m)->pat_mode] | pgeflag;
		cpu_invlpg((void *)va);
		va += PAGE_SIZE;
		m++;
	}
	smp_invltlb();
}

/*
 * This routine jerks page mappings from the
 * kernel -- it is meant only for temporary mappings.
 *
 * MPSAFE, INTERRUPT SAFE (cluster callback)
 */
void
pmap_qremove(vm_offset_t va, int count)
{
	vm_offset_t end_va;

	end_va = va + count * PAGE_SIZE;

	while (va < end_va) {
		pt_entry_t *pte;

		pte = vtopte(va);
		(void)pte_load_clear(pte);
		cpu_invlpg((void *)va);
		va += PAGE_SIZE;
	}
	smp_invltlb();
}

/*
 * Create a new thread and optionally associate it with a (new) process.
 * NOTE! the new thread's cpu may not equal the current cpu.
 */
void
pmap_init_thread(thread_t td)
{
	/* enforce pcb placement & alignment */
	td->td_pcb = (struct pcb *)(td->td_kstack + td->td_kstack_size) - 1;
	td->td_pcb = (struct pcb *)((intptr_t)td->td_pcb & ~(intptr_t)0xF);
	td->td_savefpu = &td->td_pcb->pcb_save;
	td->td_sp = (char *)td->td_pcb;	/* no -16 */
}

/*
 * This routine directly affects the fork perf for a process.
 */
void
pmap_init_proc(struct proc *p)
{
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
{
	pmap->pm_pml4 = (pml4_entry_t *)(PTOV_OFFSET + KPML4phys);
	pmap->pm_count = 1;
	pmap->pm_active = 0;
	pmap->pm_pvhint = NULL;
	RB_INIT(&pmap->pm_pvroot);
	spin_init(&pmap->pm_spin);
	lwkt_token_init(&pmap->pm_token, "pmap_tok");
	bzero(&pmap->pm_stats, sizeof pmap->pm_stats);
}

/*
 * Initialize a preallocated and zeroed pmap structure,
 * such as one in a vmspace structure.
 */
static void
pmap_pinit_simple(struct pmap *pmap)
{
	/*
	 * Misc initialization
	 */
	pmap->pm_count = 1;
	pmap->pm_active = 0;
	pmap->pm_pvhint = NULL;
	pmap->pm_flags = PMAP_FLAG_SIMPLE;

	/*
	 * Don't blow up locks/tokens on re-use (XXX fix/use drop code
	 * for this).
	 */
	if (pmap->pm_pmlpv == NULL) {
		RB_INIT(&pmap->pm_pvroot);
		bzero(&pmap->pm_stats, sizeof pmap->pm_stats);
		spin_init(&pmap->pm_spin);
		lwkt_token_init(&pmap->pm_token, "pmap_tok");
	}
}

void
pmap_pinit(struct pmap *pmap)
{
	pv_entry_t pv;
	int j;

	pmap_pinit_simple(pmap);
	pmap->pm_flags &= ~PMAP_FLAG_SIMPLE;

	/*
	 * No need to allocate page table space yet but we do need a valid
	 * page directory table.
	 */
	if (pmap->pm_pml4 == NULL) {
		pmap->pm_pml4 =
		    (pml4_entry_t *)kmem_alloc_pageable(&kernel_map, PAGE_SIZE);
	}

	/*
	 * Allocate the page directory page, which wires it even though
	 * it isn't being entered into some higher level page table (it
	 * being the highest level).  If one is already cached we don't
	 * have to do anything.
	 */
	if ((pv = pmap->pm_pmlpv) == NULL) {
		pv = pmap_allocpte(pmap, pmap_pml4_pindex(), NULL);
		pmap->pm_pmlpv = pv;
		pmap_kenter((vm_offset_t)pmap->pm_pml4,
			    VM_PAGE_TO_PHYS(pv->pv_m));
		pv_put(pv);

		/*
		 * Install DMAP and KMAP.
		 */
		for (j = 0; j < NDMPML4E; ++j) {
			pmap->pm_pml4[DMPML4I + j] =
				(DMPDPphys + ((vm_paddr_t)j << PML4SHIFT)) |
				PG_RW | PG_V | PG_U;
		}
		pmap->pm_pml4[KPML4I] = KPDPphys | PG_RW | PG_V | PG_U;

		/*
		 * install self-referential address mapping entry
		 */
		pmap->pm_pml4[PML4PML4I] = VM_PAGE_TO_PHYS(pv->pv_m) |
					   PG_V | PG_RW | PG_A | PG_M;
	} else {
		KKASSERT(pv->pv_m->flags & PG_MAPPED);
		KKASSERT(pv->pv_m->flags & PG_WRITEABLE);
	}
	KKASSERT(pmap->pm_pml4[255] == 0);
	KKASSERT(RB_ROOT(&pmap->pm_pvroot) == pv);
	KKASSERT(pv->pv_entry.rbe_left == NULL);
	KKASSERT(pv->pv_entry.rbe_right == NULL);
}

/*
 * Clean up a pmap structure so it can be physically freed.  This routine
 * is called by the vmspace dtor function.  A great deal of pmap data is
 * left passively mapped to improve vmspace management so we have a bit
 * of cleanup work to do here.
 */
void
pmap_puninit(pmap_t pmap)
{
	pv_entry_t pv;
	vm_page_t p;

	KKASSERT(pmap->pm_active == 0);
	if ((pv = pmap->pm_pmlpv) != NULL) {
		if (pv_hold_try(pv) == 0)
			pv_lock(pv);
		p = pmap_remove_pv_page(pv);
		pv_free(pv);
		pmap_kremove((vm_offset_t)pmap->pm_pml4);
		vm_page_busy_wait(p, FALSE, "pgpun");
		KKASSERT(p->flags & (PG_FICTITIOUS|PG_UNMANAGED));
		vm_page_unwire(p, 0);
		vm_page_flag_clear(p, PG_MAPPED | PG_WRITEABLE);

		/*
		 * XXX eventually clean out PML4 static entries and
		 * use vm_page_free_zero()
		 */
		vm_page_free(p);
		pmap->pm_pmlpv = NULL;
	}
	if (pmap->pm_pml4) {
		KKASSERT(pmap->pm_pml4 != (void *)(PTOV_OFFSET + KPML4phys));
		kmem_free(&kernel_map, (vm_offset_t)pmap->pm_pml4, PAGE_SIZE);
		pmap->pm_pml4 = NULL;
	}
	KKASSERT(pmap->pm_stats.resident_count == 0);
	KKASSERT(pmap->pm_stats.wired_count == 0);
}

/*
 * Wire in kernel global address entries.  To avoid a race condition
 * between pmap initialization and pmap_growkernel, this procedure
 * adds the pmap to the master list (which growkernel scans to update),
 * then copies the template.
 */
void
pmap_pinit2(struct pmap *pmap)
{
	spin_lock(&pmap_spin);
	TAILQ_INSERT_TAIL(&pmap_list, pmap, pm_pmnode);
	spin_unlock(&pmap_spin);
}

/*
 * This routine is called when various levels in the page table need to
 * be populated.  This routine cannot fail.
 *
 * This function returns two locked pv_entry's, one representing the
 * requested pv and one representing the requested pv's parent pv.  If
 * the pv did not previously exist it will be mapped into its parent
 * and wired, otherwise no additional wire count will be added.
 */
static
pv_entry_t
pmap_allocpte(pmap_t pmap, vm_pindex_t ptepindex, pv_entry_t *pvpp)
{
	pt_entry_t *ptep;
	pv_entry_t pv;
	pv_entry_t pvp;
	vm_pindex_t pt_pindex;
	vm_page_t m;
	int isnew;
	int ispt;

	/*
	 * If the pv already exists and we aren't being asked for the
	 * parent page table page we can just return it.  A locked+held pv
	 * is returned.
	 */
	ispt = 0;
	pv = pv_alloc(pmap, ptepindex, &isnew);
	if (isnew == 0 && pvpp == NULL)
		return(pv);

	/*
	 * This is a new PV, we have to resolve its parent page table and
	 * add an additional wiring to the page if necessary.
	 */

	/*
	 * Special case terminal PVs.  These are not page table pages so
	 * no vm_page is allocated (the caller supplied the vm_page).  If
	 * pvpp is non-NULL we are being asked to also removed the pt_pv
	 * for this pv.
	 *
	 * Note that pt_pv's are only returned for user VAs. We assert that
	 * a pt_pv is not being requested for kernel VAs.
	 */
	if (ptepindex < pmap_pt_pindex(0)) {
		if (ptepindex >= NUPTE_USER)
			KKASSERT(pvpp == NULL);
		else
			KKASSERT(pvpp != NULL);
		if (pvpp) {
			pt_pindex = NUPTE_TOTAL + (ptepindex >> NPTEPGSHIFT);
			pvp = pmap_allocpte(pmap, pt_pindex, NULL);
			if (isnew)
				vm_page_wire_quick(pvp->pv_m);
			*pvpp = pvp;
		} else {
			pvp = NULL;
		}
		return(pv);
	}

	/*
	 * Non-terminal PVs allocate a VM page to represent the page table,
	 * so we have to resolve pvp and calculate ptepindex for the pvp
	 * and then for the page table entry index in the pvp for
	 * fall-through.
	 */
	if (ptepindex < pmap_pd_pindex(0)) {
		/*
		 * pv is PT, pvp is PD
		 */
		ptepindex = (ptepindex - pmap_pt_pindex(0)) >> NPDEPGSHIFT;
		ptepindex += NUPTE_TOTAL + NUPT_TOTAL;
		pvp = pmap_allocpte(pmap, ptepindex, NULL);
		if (!isnew)
			goto notnew;

		/*
		 * PT index in PD
		 */
		ptepindex = pv->pv_pindex - pmap_pt_pindex(0);
		ptepindex &= ((1ul << NPDEPGSHIFT) - 1);
		ispt = 1;
	} else if (ptepindex < pmap_pdp_pindex(0)) {
		/*
		 * pv is PD, pvp is PDP
		 *
		 * SIMPLE PMAP NOTE: Simple pmaps do not allocate above
		 *		     the PD.
		 */
		ptepindex = (ptepindex - pmap_pd_pindex(0)) >> NPDPEPGSHIFT;
		ptepindex += NUPTE_TOTAL + NUPT_TOTAL + NUPD_TOTAL;

		if (pmap->pm_flags & PMAP_FLAG_SIMPLE) {
			KKASSERT(pvpp == NULL);
			pvp = NULL;
		} else {
			pvp = pmap_allocpte(pmap, ptepindex, NULL);
		}
		if (!isnew)
			goto notnew;

		/*
		 * PD index in PDP
		 */
		ptepindex = pv->pv_pindex - pmap_pd_pindex(0);
		ptepindex &= ((1ul << NPDPEPGSHIFT) - 1);
	} else if (ptepindex < pmap_pml4_pindex()) {
		/*
		 * pv is PDP, pvp is the root pml4 table
		 */
		pvp = pmap_allocpte(pmap, pmap_pml4_pindex(), NULL);
		if (!isnew)
			goto notnew;

		/*
		 * PDP index in PML4
		 */
		ptepindex = pv->pv_pindex - pmap_pdp_pindex(0);
		ptepindex &= ((1ul << NPML4EPGSHIFT) - 1);
	} else {
		/*
		 * pv represents the top-level PML4, there is no parent.
		 */
		pvp = NULL;
		if (!isnew)
			goto notnew;
	}

	/*
	 * This code is only reached if isnew is TRUE and this is not a
	 * terminal PV.  We need to allocate a vm_page for the page table
	 * at this level and enter it into the parent page table.
	 *
	 * page table pages are marked PG_WRITEABLE and PG_MAPPED.
	 */
	for (;;) {
		m = vm_page_alloc(NULL, pv->pv_pindex,
				  VM_ALLOC_NORMAL | VM_ALLOC_SYSTEM |
				  VM_ALLOC_INTERRUPT);
		if (m)
			break;
		vm_wait(0);
	}
	vm_page_spin_lock(m);
	TAILQ_INSERT_TAIL(&m->md.pv_list, pv, pv_list);
	pv->pv_m = m;
	vm_page_flag_set(m, PG_MAPPED | PG_WRITEABLE);
	vm_page_spin_unlock(m);
	vm_page_unmanage(m);	/* m must be spinunlocked */

	if ((m->flags & PG_ZERO) == 0) {
		pmap_zero_page(VM_PAGE_TO_PHYS(m));
	}
#ifdef PMAP_DEBUG
	else {
		pmap_page_assertzero(VM_PAGE_TO_PHYS(m));
	}
#endif
	m->valid = VM_PAGE_BITS_ALL;
	vm_page_flag_clear(m, PG_ZERO);
	vm_page_wire(m);	/* wire for mapping in parent */

	/*
	 * Wire the page into pvp, bump the wire-count for pvp's page table
	 * page.  Bump the resident_count for the pmap.  There is no pvp
	 * for the top level, address the pm_pml4[] array directly.
	 *
	 * If the caller wants the parent we return it, otherwise
	 * we just put it away.
	 *
	 * No interlock is needed for pte 0 -> non-zero.
	 *
	 * In the situation where *ptep is valid we might have an unmanaged
	 * page table page shared from another page table which we need to
	 * unshare before installing our private page table page.
	 */
	if (pvp) {
		ptep = pv_pte_lookup(pvp, ptepindex);
		if (*ptep & PG_V) {
			pt_entry_t pte;
			pmap_inval_info info;

			if (ispt == 0) {
				panic("pmap_allocpte: unexpected pte %p/%d",
				      pvp, (int)ptepindex);
			}
			pmap_inval_init(&info);
			pmap_inval_interlock(&info, pmap, (vm_offset_t)-1);
			pte = pte_load_clear(ptep);
			pmap_inval_deinterlock(&info, pmap);
			pmap_inval_done(&info);
			if (vm_page_unwire_quick(
					PHYS_TO_VM_PAGE(pte & PG_FRAME))) {
				panic("pmap_allocpte: shared pgtable "
				      "pg bad wirecount");
			}
			atomic_add_long(&pmap->pm_stats.resident_count, -1);
		} else {
			vm_page_wire_quick(pvp->pv_m);
		}
		*ptep = VM_PAGE_TO_PHYS(m) | (PG_U | PG_RW | PG_V |
					      PG_A | PG_M);
	}
	vm_page_wakeup(m);
notnew:
	if (pvpp)
		*pvpp = pvp;
	else if (pvp)
		pv_put(pvp);
	return (pv);
}

/*
 * This version of pmap_allocpte() checks for possible segment optimizations
 * that would allow page-table sharing.  It can be called for terminal
 * page or page table page ptepindex's.
 *
 * The function is called with page table page ptepindex's for fictitious
 * and unmanaged terminal pages.  That is, we don't want to allocate a
 * terminal pv, we just want the pt_pv.  pvpp is usually passed as NULL
 * for this case.
 *
 * This function can return a pv and *pvpp associated with the passed in pmap
 * OR a pv and *pvpp associated with the shared pmap.  In the latter case
 * an unmanaged page table page will be entered into the pass in pmap.
 */
static
pv_entry_t
pmap_allocpte_seg(pmap_t pmap, vm_pindex_t ptepindex, pv_entry_t *pvpp,
		  vm_map_entry_t entry, vm_offset_t va)
{
	struct pmap_inval_info info;
	vm_object_t object;
	pmap_t obpmap;
	pmap_t *obpmapp;
	vm_offset_t b;
	pv_entry_t pte_pv;	/* in original or shared pmap */
	pv_entry_t pt_pv;	/* in original or shared pmap */
	pv_entry_t proc_pd_pv;	/* in original pmap */
	pv_entry_t proc_pt_pv;	/* in original pmap */
	pv_entry_t xpv;		/* PT in shared pmap */
	pd_entry_t *pt;		/* PT entry in PD of original pmap */
	pd_entry_t opte;	/* contents of *pt */
	pd_entry_t npte;	/* contents of *pt */
	vm_page_t m;

retry:
	/*
	 * Basic tests, require a non-NULL vm_map_entry, require proper
	 * alignment and type for the vm_map_entry, require that the
	 * underlying object already be allocated.
	 *
	 * We currently allow any type of object to use this optimization.
	 * The object itself does NOT have to be sized to a multiple of the
	 * segment size, but the memory mapping does.
	 */
	if (entry == NULL ||
	    pmap_mmu_optimize == 0 ||			/* not enabled */
	    ptepindex >= pmap_pd_pindex(0) ||		/* not terminal */
	    entry->inheritance != VM_INHERIT_SHARE ||	/* not shared */
	    entry->maptype != VM_MAPTYPE_NORMAL ||	/* weird map type */
	    entry->object.vm_object == NULL ||		/* needs VM object */
	    (entry->offset & SEG_MASK) ||		/* must be aligned */
	    (entry->start & SEG_MASK)) {
		return(pmap_allocpte(pmap, ptepindex, pvpp));
	}

	/*
	 * Make sure the full segment can be represented.
	 */
	b = va & ~(vm_offset_t)SEG_MASK;
	if (b < entry->start && b + SEG_SIZE > entry->end)
		return(pmap_allocpte(pmap, ptepindex, pvpp));

	/*
	 * If the full segment can be represented dive the VM object's
	 * shared pmap, allocating as required.
	 */
	object = entry->object.vm_object;

	if (entry->protection & VM_PROT_WRITE)
		obpmapp = &object->md.pmap_rw;
	else
		obpmapp = &object->md.pmap_ro;

	/*
	 * We allocate what appears to be a normal pmap but because portions
	 * of this pmap are shared with other unrelated pmaps we have to
	 * set pm_active to point to all cpus.
	 *
	 * XXX Currently using pmap_spin to interlock the update, can't use
	 *     vm_object_hold/drop because the token might already be held
	 *     shared OR exclusive and we don't know.
	 */
	while ((obpmap = *obpmapp) == NULL) {
		obpmap = kmalloc(sizeof(*obpmap), M_OBJPMAP, M_WAITOK|M_ZERO);
		pmap_pinit_simple(obpmap);
		pmap_pinit2(obpmap);
		spin_lock(&pmap_spin);
		if (*obpmapp != NULL) {
			/*
			 * Handle race
			 */
			spin_unlock(&pmap_spin);
			pmap_release(obpmap);
			pmap_puninit(obpmap);
			kfree(obpmap, M_OBJPMAP);
		} else {
			obpmap->pm_active = smp_active_mask;
			*obpmapp = obpmap;
			spin_unlock(&pmap_spin);
		}
	}

	/*
	 * Layering is: PTE, PT, PD, PDP, PML4.  We have to return the
	 * pte/pt using the shared pmap from the object but also adjust
	 * the process pmap's page table page as a side effect.
	 */

	/*
	 * Resolve the terminal PTE and PT in the shared pmap.  This is what
	 * we will return.  This is true if ptepindex represents a terminal
	 * page, otherwise pte_pv is actually the PT and pt_pv is actually
	 * the PD.
	 */
	pt_pv = NULL;
	pte_pv = pmap_allocpte(obpmap, ptepindex, &pt_pv);
	if (ptepindex >= pmap_pt_pindex(0))
		xpv = pte_pv;
	else
		xpv = pt_pv;

	/*
	 * Resolve the PD in the process pmap so we can properly share the
	 * page table page.  Lock order is bottom-up (leaf first)!
	 *
	 * NOTE: proc_pt_pv can be NULL.
	 */
	proc_pt_pv = pv_get(pmap, pmap_pt_pindex(b));
	proc_pd_pv = pmap_allocpte(pmap, pmap_pd_pindex(b), NULL);

	/*
	 * xpv is the page table page pv from the shared object
	 * (for convenience).
	 *
	 * Calculate the pte value for the PT to load into the process PD.
	 * If we have to change it we must properly dispose of the previous
	 * entry.
	 */
	pt = pv_pte_lookup(proc_pd_pv, pmap_pt_index(b));
	npte = VM_PAGE_TO_PHYS(xpv->pv_m) |
	       (PG_U | PG_RW | PG_V | PG_A | PG_M);

	/*
	 * Dispose of previous page table page if it was local to the
	 * process pmap.  If the old pt is not empty we cannot dispose of it
	 * until we clean it out.  This case should not arise very often so
	 * it is not optimized.
	 */
	if (proc_pt_pv) {
		if (proc_pt_pv->pv_m->wire_count != 1) {
			pv_put(proc_pd_pv);
			pv_put(proc_pt_pv);
			pv_put(pt_pv);
			pv_put(pte_pv);
			pmap_remove(pmap,
				    va & ~(vm_offset_t)SEG_MASK,
				    (va + SEG_SIZE) & ~(vm_offset_t)SEG_MASK);
			goto retry;
		}
		pmap_release_pv(proc_pt_pv, proc_pd_pv);
		proc_pt_pv = NULL;
		/* relookup */
		pt = pv_pte_lookup(proc_pd_pv, pmap_pt_index(b));
	}

	/*
	 * Handle remaining cases.
	 */
	if (*pt == 0) {
		*pt = npte;
		vm_page_wire_quick(xpv->pv_m);
		vm_page_wire_quick(proc_pd_pv->pv_m);
		atomic_add_long(&pmap->pm_stats.resident_count, 1);
	} else if (*pt != npte) {
		pmap_inval_init(&info);
		pmap_inval_interlock(&info, pmap, (vm_offset_t)-1);

		opte = pte_load_clear(pt);
		KKASSERT(opte && opte != npte);

		*pt = npte;
		vm_page_wire_quick(xpv->pv_m);	/* pgtable pg that is npte */

		/*
		 * Clean up opte, bump the wire_count for the process
		 * PD page representing the new entry if it was
		 * previously empty.
		 *
		 * If the entry was not previously empty and we have
		 * a PT in the proc pmap then opte must match that
		 * pt.  The proc pt must be retired (this is done
		 * later on in this procedure).
		 *
		 * NOTE: replacing valid pte, wire_count on proc_pd_pv
		 * stays the same.
		 */
		KKASSERT(opte & PG_V);
		m = PHYS_TO_VM_PAGE(opte & PG_FRAME);
		if (vm_page_unwire_quick(m)) {
			panic("pmap_allocpte_seg: "
			      "bad wire count %p",
			      m);
		}

		pmap_inval_deinterlock(&info, pmap);
		pmap_inval_done(&info);
	}

	/*
	 * The existing process page table was replaced and must be destroyed
	 * here.
	 */
	if (proc_pd_pv)
		pv_put(proc_pd_pv);
	if (pvpp)
		*pvpp = pt_pv;
	else
		pv_put(pt_pv);

	return (pte_pv);
}

/*
 * Release any resources held by the given physical map.
 *
 * Called when a pmap initialized by pmap_pinit is being released.  Should
 * only be called if the map contains no valid mappings.
 *
 * Caller must hold pmap->pm_token
 */
struct pmap_release_info {
	pmap_t	pmap;
	int	retry;
};

static int pmap_release_callback(pv_entry_t pv, void *data);

void
pmap_release(struct pmap *pmap)
{
	struct pmap_release_info info;

	KASSERT(pmap->pm_active == 0,
		("pmap still active! %016jx", (uintmax_t)pmap->pm_active));

	spin_lock(&pmap_spin);
	TAILQ_REMOVE(&pmap_list, pmap, pm_pmnode);
	spin_unlock(&pmap_spin);

	/*
	 * Pull pv's off the RB tree in order from low to high and release
	 * each page.
	 */
	info.pmap = pmap;
	do {
		info.retry = 0;
		spin_lock(&pmap->pm_spin);
		RB_SCAN(pv_entry_rb_tree, &pmap->pm_pvroot, NULL,
			pmap_release_callback, &info);
		spin_unlock(&pmap->pm_spin);
	} while (info.retry);


	/*
	 * One resident page (the pml4 page) should remain.
	 * No wired pages should remain.
	 */
	KKASSERT(pmap->pm_stats.resident_count ==
		 ((pmap->pm_flags & PMAP_FLAG_SIMPLE) ? 0 : 1));

	KKASSERT(pmap->pm_stats.wired_count == 0);
}

static int
pmap_release_callback(pv_entry_t pv, void *data)
{
	struct pmap_release_info *info = data;
	pmap_t pmap = info->pmap;
	int r;

	if (pv_hold_try(pv)) {
		spin_unlock(&pmap->pm_spin);
	} else {
		spin_unlock(&pmap->pm_spin);
		pv_lock(pv);
		if (pv->pv_pmap != pmap) {
			pv_put(pv);
			spin_lock(&pmap->pm_spin);
			info->retry = 1;
			return(-1);
		}
	}
	r = pmap_release_pv(pv, NULL);
	spin_lock(&pmap->pm_spin);
	return(r);
}

/*
 * Called with held (i.e. also locked) pv.  This function will dispose of
 * the lock along with the pv.
 *
 * If the caller already holds the locked parent page table for pv it
 * must pass it as pvp, allowing us to avoid a deadlock, else it can
 * pass NULL for pvp.
 */
static int
pmap_release_pv(pv_entry_t pv, pv_entry_t pvp)
{
	vm_page_t p;

	/*
	 * The pmap is currently not spinlocked, pv is held+locked.
	 * Remove the pv's page from its parent's page table.  The
	 * parent's page table page's wire_count will be decremented.
	 */
	pmap_remove_pv_pte(pv, pvp, NULL);

	/*
	 * Terminal pvs are unhooked from their vm_pages.  Because
	 * terminal pages aren't page table pages they aren't wired
	 * by us, so we have to be sure not to unwire them either.
	 */
	if (pv->pv_pindex < pmap_pt_pindex(0)) {
		pmap_remove_pv_page(pv);
		goto skip;
	}

	/*
	 * We leave the top-level page table page cached, wired, and
	 * mapped in the pmap until the dtor function (pmap_puninit())
	 * gets called.
	 *
	 * Since we are leaving the top-level pv intact we need
	 * to break out of what would otherwise be an infinite loop.
	 */
	if (pv->pv_pindex == pmap_pml4_pindex()) {
		pv_put(pv);
		return(-1);
	}

	/*
	 * For page table pages (other than the top-level page),
	 * remove and free the vm_page.  The representitive mapping
	 * removed above by pmap_remove_pv_pte() did not undo the
	 * last wire_count so we have to do that as well.
	 */
	p = pmap_remove_pv_page(pv);
	vm_page_busy_wait(p, FALSE, "pmaprl");
	if (p->wire_count != 1) {
		kprintf("p->wire_count was %016lx %d\n",
			pv->pv_pindex, p->wire_count);
	}
	KKASSERT(p->wire_count == 1);
	KKASSERT(p->flags & PG_UNMANAGED);

	vm_page_unwire(p, 0);
	KKASSERT(p->wire_count == 0);

	/*
	 * Theoretically this page, if not the pml4 page, should contain
	 * all-zeros.  But its just too dangerous to mark it PG_ZERO.  Free
	 * normally.
	 */
	vm_page_free(p);
skip:
	pv_free(pv);
	return 0;
}

/*
 * This function will remove the pte associated with a pv from its parent.
 * Terminal pv's are supported.  The removal will be interlocked if info
 * is non-NULL.  The caller must dispose of pv instead of just unlocking
 * it.
 *
 * The wire count will be dropped on the parent page table.  The wire
 * count on the page being removed (pv->pv_m) from the parent page table
 * is NOT touched.  Note that terminal pages will not have any additional
 * wire counts while page table pages will have at least one representing
 * the mapping, plus others representing sub-mappings.
 *
 * NOTE: Cannot be called on kernel page table pages, only KVM terminal
 *	 pages and user page table and terminal pages.
 *
 * The pv must be locked.
 *
 * XXX must lock parent pv's if they exist to remove pte XXX
 */
static
void
pmap_remove_pv_pte(pv_entry_t pv, pv_entry_t pvp, struct pmap_inval_info *info)
{
	vm_pindex_t ptepindex = pv->pv_pindex;
	pmap_t pmap = pv->pv_pmap;
	vm_page_t p;
	int gotpvp = 0;

	KKASSERT(pmap);

	if (ptepindex == pmap_pml4_pindex()) {
		/*
		 * We are the top level pml4 table, there is no parent.
		 */
		p = pmap->pm_pmlpv->pv_m;
	} else if (ptepindex >= pmap_pdp_pindex(0)) {
		/*
		 * Remove a PDP page from the pml4e.  This can only occur
		 * with user page tables.  We do not have to lock the
		 * pml4 PV so just ignore pvp.
		 */
		vm_pindex_t pml4_pindex;
		vm_pindex_t pdp_index;
		pml4_entry_t *pdp;

		pdp_index = ptepindex - pmap_pdp_pindex(0);
		if (pvp == NULL) {
			pml4_pindex = pmap_pml4_pindex();
			pvp = pv_get(pv->pv_pmap, pml4_pindex);
			KKASSERT(pvp);
			gotpvp = 1;
		}
		pdp = &pmap->pm_pml4[pdp_index & ((1ul << NPML4EPGSHIFT) - 1)];
		KKASSERT((*pdp & PG_V) != 0);
		p = PHYS_TO_VM_PAGE(*pdp & PG_FRAME);
		*pdp = 0;
		KKASSERT(info == NULL);
	} else if (ptepindex >= pmap_pd_pindex(0)) {
		/*
		 * Remove a PD page from the pdp
		 *
		 * SIMPLE PMAP NOTE: Non-existant pvp's are ok in the case
		 *		     of a simple pmap because it stops at
		 *		     the PD page.
		 */
		vm_pindex_t pdp_pindex;
		vm_pindex_t pd_index;
		pdp_entry_t *pd;

		pd_index = ptepindex - pmap_pd_pindex(0);

		if (pvp == NULL) {
			pdp_pindex = NUPTE_TOTAL + NUPT_TOTAL + NUPD_TOTAL +
				     (pd_index >> NPML4EPGSHIFT);
			pvp = pv_get(pv->pv_pmap, pdp_pindex);
			if (pvp)
				gotpvp = 1;
		}
		if (pvp) {
			pd = pv_pte_lookup(pvp, pd_index &
						((1ul << NPDPEPGSHIFT) - 1));
			KKASSERT((*pd & PG_V) != 0);
			p = PHYS_TO_VM_PAGE(*pd & PG_FRAME);
			*pd = 0;
		} else {
			KKASSERT(pmap->pm_flags & PMAP_FLAG_SIMPLE);
			p = pv->pv_m;		/* degenerate test later */
		}
		KKASSERT(info == NULL);
	} else if (ptepindex >= pmap_pt_pindex(0)) {
		/*
		 *  Remove a PT page from the pd
		 */
		vm_pindex_t pd_pindex;
		vm_pindex_t pt_index;
		pd_entry_t *pt;

		pt_index = ptepindex - pmap_pt_pindex(0);

		if (pvp == NULL) {
			pd_pindex = NUPTE_TOTAL + NUPT_TOTAL +
				    (pt_index >> NPDPEPGSHIFT);
			pvp = pv_get(pv->pv_pmap, pd_pindex);
			KKASSERT(pvp);
			gotpvp = 1;
		}
		pt = pv_pte_lookup(pvp, pt_index & ((1ul << NPDPEPGSHIFT) - 1));
		KKASSERT((*pt & PG_V) != 0);
		p = PHYS_TO_VM_PAGE(*pt & PG_FRAME);
		*pt = 0;
		KKASSERT(info == NULL);
	} else {
		/*
		 * Remove a PTE from the PT page
		 *
		 * NOTE: pv's must be locked bottom-up to avoid deadlocking.
		 *	 pv is a pte_pv so we can safely lock pt_pv.
		 */
		vm_pindex_t pt_pindex;
		pt_entry_t *ptep;
		pt_entry_t pte;
		vm_offset_t va;

		pt_pindex = ptepindex >> NPTEPGSHIFT;
		va = (vm_offset_t)ptepindex << PAGE_SHIFT;

		if (ptepindex >= NUPTE_USER) {
			ptep = vtopte(ptepindex << PAGE_SHIFT);
			KKASSERT(pvp == NULL);
		} else {
			if (pvp == NULL) {
				pt_pindex = NUPTE_TOTAL +
					    (ptepindex >> NPDPEPGSHIFT);
				pvp = pv_get(pv->pv_pmap, pt_pindex);
				KKASSERT(pvp);
				gotpvp = 1;
			}
			ptep = pv_pte_lookup(pvp, ptepindex &
						  ((1ul << NPDPEPGSHIFT) - 1));
		}

		if (info)
			pmap_inval_interlock(info, pmap, va);
		pte = pte_load_clear(ptep);
		if (info)
			pmap_inval_deinterlock(info, pmap);
		else
			cpu_invlpg((void *)va);

		/*
		 * Now update the vm_page_t
		 */
		if ((pte & (PG_MANAGED|PG_V)) != (PG_MANAGED|PG_V)) {
			kprintf("remove_pte badpte %016lx %016lx %d\n",
				pte, pv->pv_pindex,
				pv->pv_pindex < pmap_pt_pindex(0));
		}
		/*KKASSERT((pte & (PG_MANAGED|PG_V)) == (PG_MANAGED|PG_V));*/
		p = PHYS_TO_VM_PAGE(pte & PG_FRAME);

		if (pte & PG_M) {
			if (pmap_track_modified(ptepindex))
				vm_page_dirty(p);
		}
		if (pte & PG_A) {
			vm_page_flag_set(p, PG_REFERENCED);
		}
		if (pte & PG_W)
			atomic_add_long(&pmap->pm_stats.wired_count, -1);
		if (pte & PG_G)
			cpu_invlpg((void *)va);
	}

	/*
	 * Unwire the parent page table page.  The wire_count cannot go below
	 * 1 here because the parent page table page is itself still mapped.
	 *
	 * XXX remove the assertions later.
	 */
	KKASSERT(pv->pv_m == p);
	if (pvp && vm_page_unwire_quick(pvp->pv_m))
		panic("pmap_remove_pv_pte: Insufficient wire_count");

	if (gotpvp)
		pv_put(pvp);
}

static
vm_page_t
pmap_remove_pv_page(pv_entry_t pv)
{
	vm_page_t m;

	m = pv->pv_m;
	KKASSERT(m);
	vm_page_spin_lock(m);
	pv->pv_m = NULL;
	TAILQ_REMOVE(&m->md.pv_list, pv, pv_list);
	/*
	if (m->object)
		atomic_add_int(&m->object->agg_pv_list_count, -1);
	*/
	if (TAILQ_EMPTY(&m->md.pv_list))
		vm_page_flag_clear(m, PG_MAPPED | PG_WRITEABLE);
	vm_page_spin_unlock(m);
	return(m);
}

/*
 * Grow the number of kernel page table entries, if needed.
 *
 * This routine is always called to validate any address space
 * beyond KERNBASE (for kldloads).  kernel_vm_end only governs the address
 * space below KERNBASE.
 */
void
pmap_growkernel(vm_offset_t kstart, vm_offset_t kend)
{
	vm_paddr_t paddr;
	vm_offset_t ptppaddr;
	vm_page_t nkpg;
	pd_entry_t *pt, newpt;
	pdp_entry_t newpd;
	int update_kernel_vm_end;

	/*
	 * bootstrap kernel_vm_end on first real VM use
	 */
	if (kernel_vm_end == 0) {
		kernel_vm_end = VM_MIN_KERNEL_ADDRESS;
		nkpt = 0;
		while ((*pmap_pt(&kernel_pmap, kernel_vm_end) & PG_V) != 0) {
			kernel_vm_end = (kernel_vm_end + PAGE_SIZE * NPTEPG) &
					~(PAGE_SIZE * NPTEPG - 1);
			nkpt++;
			if (kernel_vm_end - 1 >= kernel_map.max_offset) {
				kernel_vm_end = kernel_map.max_offset;
				break;                       
			}
		}
	}

	/*
	 * Fill in the gaps.  kernel_vm_end is only adjusted for ranges
	 * below KERNBASE.  Ranges above KERNBASE are kldloaded and we
	 * do not want to force-fill 128G worth of page tables.
	 */
	if (kstart < KERNBASE) {
		if (kstart > kernel_vm_end)
			kstart = kernel_vm_end;
		KKASSERT(kend <= KERNBASE);
		update_kernel_vm_end = 1;
	} else {
		update_kernel_vm_end = 0;
	}

	kstart = rounddown2(kstart, PAGE_SIZE * NPTEPG);
	kend = roundup2(kend, PAGE_SIZE * NPTEPG);

	if (kend - 1 >= kernel_map.max_offset)
		kend = kernel_map.max_offset;

	while (kstart < kend) {
		pt = pmap_pt(&kernel_pmap, kstart);
		if (pt == NULL) {
			/* We need a new PDP entry */
			nkpg = vm_page_alloc(NULL, nkpt,
			                     VM_ALLOC_NORMAL |
					     VM_ALLOC_SYSTEM |
					     VM_ALLOC_INTERRUPT);
			if (nkpg == NULL) {
				panic("pmap_growkernel: no memory to grow "
				      "kernel");
			}
			paddr = VM_PAGE_TO_PHYS(nkpg);
			if ((nkpg->flags & PG_ZERO) == 0)
				pmap_zero_page(paddr);
			vm_page_flag_clear(nkpg, PG_ZERO);
			newpd = (pdp_entry_t)
				(paddr | PG_V | PG_RW | PG_A | PG_M);
			*pmap_pd(&kernel_pmap, kstart) = newpd;
			nkpt++;
			continue; /* try again */
		}
		if ((*pt & PG_V) != 0) {
			kstart = (kstart + PAGE_SIZE * NPTEPG) &
				 ~(PAGE_SIZE * NPTEPG - 1);
			if (kstart - 1 >= kernel_map.max_offset) {
				kstart = kernel_map.max_offset;
				break;                       
			}
			continue;
		}

		/*
		 * This index is bogus, but out of the way
		 */
		nkpg = vm_page_alloc(NULL, nkpt,
				     VM_ALLOC_NORMAL |
				     VM_ALLOC_SYSTEM |
				     VM_ALLOC_INTERRUPT);
		if (nkpg == NULL)
			panic("pmap_growkernel: no memory to grow kernel");

		vm_page_wire(nkpg);
		ptppaddr = VM_PAGE_TO_PHYS(nkpg);
		pmap_zero_page(ptppaddr);
		vm_page_flag_clear(nkpg, PG_ZERO);
		newpt = (pd_entry_t) (ptppaddr | PG_V | PG_RW | PG_A | PG_M);
		*pmap_pt(&kernel_pmap, kstart) = newpt;
		nkpt++;

		kstart = (kstart + PAGE_SIZE * NPTEPG) &
			  ~(PAGE_SIZE * NPTEPG - 1);

		if (kstart - 1 >= kernel_map.max_offset) {
			kstart = kernel_map.max_offset;
			break;                       
		}
	}

	/*
	 * Only update kernel_vm_end for areas below KERNBASE.
	 */
	if (update_kernel_vm_end && kernel_vm_end < kstart)
		kernel_vm_end = kstart;
}

/*
 *	Add a reference to the specified pmap.
 */
void
pmap_reference(pmap_t pmap)
{
	if (pmap != NULL) {
		lwkt_gettoken(&pmap->pm_token);
		++pmap->pm_count;
		lwkt_reltoken(&pmap->pm_token);
	}
}

/***************************************************
 * page management routines.
 ***************************************************/

/*
 * Hold a pv without locking it
 */
static void
pv_hold(pv_entry_t pv)
{
	u_int count;

	if (atomic_cmpset_int(&pv->pv_hold, 0, 1))
		return;

	for (;;) {
		count = pv->pv_hold;
		cpu_ccfence();
		if (atomic_cmpset_int(&pv->pv_hold, count, count + 1))
			return;
		/* retry */
	}
}

/*
 * Hold a pv_entry, preventing its destruction.  TRUE is returned if the pv
 * was successfully locked, FALSE if it wasn't.  The caller must dispose of
 * the pv properly.
 *
 * Either the pmap->pm_spin or the related vm_page_spin (if traversing a
 * pv list via its page) must be held by the caller.
 */
static int
_pv_hold_try(pv_entry_t pv PMAP_DEBUG_DECL)
{
	u_int count;

	if (atomic_cmpset_int(&pv->pv_hold, 0, PV_HOLD_LOCKED | 1)) {
#ifdef PMAP_DEBUG
		pv->pv_func = func;
		pv->pv_line = lineno;
#endif
		return TRUE;
	}

	for (;;) {
		count = pv->pv_hold;
		cpu_ccfence();
		if ((count & PV_HOLD_LOCKED) == 0) {
			if (atomic_cmpset_int(&pv->pv_hold, count,
					      (count + 1) | PV_HOLD_LOCKED)) {
#ifdef PMAP_DEBUG
				pv->pv_func = func;
				pv->pv_line = lineno;
#endif
				return TRUE;
			}
		} else {
			if (atomic_cmpset_int(&pv->pv_hold, count, count + 1))
				return FALSE;
		}
		/* retry */
	}
}

/*
 * Drop a previously held pv_entry which could not be locked, allowing its
 * destruction.
 *
 * Must not be called with a spinlock held as we might zfree() the pv if it
 * is no longer associated with a pmap and this was the last hold count.
 */
static void
pv_drop(pv_entry_t pv)
{
	u_int count;

	if (atomic_cmpset_int(&pv->pv_hold, 1, 0)) {
		if (pv->pv_pmap == NULL)
			zfree(pvzone, pv);
		return;
	}

	for (;;) {
		count = pv->pv_hold;
		cpu_ccfence();
		KKASSERT((count & PV_HOLD_MASK) > 0);
		KKASSERT((count & (PV_HOLD_LOCKED | PV_HOLD_MASK)) !=
			 (PV_HOLD_LOCKED | 1));
		if (atomic_cmpset_int(&pv->pv_hold, count, count - 1)) {
			if (count == 1 && pv->pv_pmap == NULL)
				zfree(pvzone, pv);
			return;
		}
		/* retry */
	}
}

/*
 * Find or allocate the requested PV entry, returning a locked pv
 */
static
pv_entry_t
_pv_alloc(pmap_t pmap, vm_pindex_t pindex, int *isnew PMAP_DEBUG_DECL)
{
	pv_entry_t pv;
	pv_entry_t pnew = NULL;

	spin_lock(&pmap->pm_spin);
	for (;;) {
		if ((pv = pmap->pm_pvhint) == NULL || pv->pv_pindex != pindex) {
			pv = pv_entry_rb_tree_RB_LOOKUP(&pmap->pm_pvroot,
							pindex);
		}
		if (pv == NULL) {
			if (pnew == NULL) {
				spin_unlock(&pmap->pm_spin);
				pnew = zalloc(pvzone);
				spin_lock(&pmap->pm_spin);
				continue;
			}
			pnew->pv_pmap = pmap;
			pnew->pv_pindex = pindex;
			pnew->pv_hold = PV_HOLD_LOCKED | 1;
#ifdef PMAP_DEBUG
			pnew->pv_func = func;
			pnew->pv_line = lineno;
#endif
			pv_entry_rb_tree_RB_INSERT(&pmap->pm_pvroot, pnew);
			atomic_add_long(&pmap->pm_stats.resident_count, 1);
			spin_unlock(&pmap->pm_spin);
			*isnew = 1;
			return(pnew);
		}
		if (pnew) {
			spin_unlock(&pmap->pm_spin);
			zfree(pvzone, pnew);
			pnew = NULL;
			spin_lock(&pmap->pm_spin);
			continue;
		}
		if (_pv_hold_try(pv PMAP_DEBUG_COPY)) {
			spin_unlock(&pmap->pm_spin);
			*isnew = 0;
			return(pv);
		}
		spin_unlock(&pmap->pm_spin);
		_pv_lock(pv PMAP_DEBUG_COPY);
		if (pv->pv_pmap == pmap && pv->pv_pindex == pindex) {
			*isnew = 0;
			return(pv);
		}
		pv_put(pv);
		spin_lock(&pmap->pm_spin);
	}


}

/*
 * Find the requested PV entry, returning a locked+held pv or NULL
 */
static
pv_entry_t
_pv_get(pmap_t pmap, vm_pindex_t pindex PMAP_DEBUG_DECL)
{
	pv_entry_t pv;

	spin_lock(&pmap->pm_spin);
	for (;;) {
		/*
		 * Shortcut cache
		 */
		if ((pv = pmap->pm_pvhint) == NULL || pv->pv_pindex != pindex) {
			pv = pv_entry_rb_tree_RB_LOOKUP(&pmap->pm_pvroot,
							pindex);
		}
		if (pv == NULL) {
			spin_unlock(&pmap->pm_spin);
			return NULL;
		}
		if (_pv_hold_try(pv PMAP_DEBUG_COPY)) {
			pv_cache(pv, pindex);
			spin_unlock(&pmap->pm_spin);
			return(pv);
		}
		spin_unlock(&pmap->pm_spin);
		_pv_lock(pv PMAP_DEBUG_COPY);
		if (pv->pv_pmap == pmap && pv->pv_pindex == pindex)
			return(pv);
		pv_put(pv);
		spin_lock(&pmap->pm_spin);
	}
}

/*
 * Lookup, hold, and attempt to lock (pmap,pindex).
 *
 * If the entry does not exist NULL is returned and *errorp is set to 0
 *
 * If the entry exists and could be successfully locked it is returned and
 * errorp is set to 0.
 *
 * If the entry exists but could NOT be successfully locked it is returned
 * held and *errorp is set to 1.
 */
static
pv_entry_t
pv_get_try(pmap_t pmap, vm_pindex_t pindex, int *errorp)
{
	pv_entry_t pv;

	spin_lock(&pmap->pm_spin);
	if ((pv = pmap->pm_pvhint) == NULL || pv->pv_pindex != pindex)
		pv = pv_entry_rb_tree_RB_LOOKUP(&pmap->pm_pvroot, pindex);
	if (pv == NULL) {
		spin_unlock(&pmap->pm_spin);
		*errorp = 0;
		return NULL;
	}
	if (pv_hold_try(pv)) {
		pv_cache(pv, pindex);
		spin_unlock(&pmap->pm_spin);
		*errorp = 0;
		return(pv);	/* lock succeeded */
	}
	spin_unlock(&pmap->pm_spin);
	*errorp = 1;
	return (pv);		/* lock failed */
}

/*
 * Find the requested PV entry, returning a held pv or NULL
 */
static
pv_entry_t
pv_find(pmap_t pmap, vm_pindex_t pindex)
{
	pv_entry_t pv;

	spin_lock(&pmap->pm_spin);

	if ((pv = pmap->pm_pvhint) == NULL || pv->pv_pindex != pindex)
		pv = pv_entry_rb_tree_RB_LOOKUP(&pmap->pm_pvroot, pindex);
	if (pv == NULL) {
		spin_unlock(&pmap->pm_spin);
		return NULL;
	}
	pv_hold(pv);
	pv_cache(pv, pindex);
	spin_unlock(&pmap->pm_spin);
	return(pv);
}

/*
 * Lock a held pv, keeping the hold count
 */
static
void
_pv_lock(pv_entry_t pv PMAP_DEBUG_DECL)
{
	u_int count;

	for (;;) {
		count = pv->pv_hold;
		cpu_ccfence();
		if ((count & PV_HOLD_LOCKED) == 0) {
			if (atomic_cmpset_int(&pv->pv_hold, count,
					      count | PV_HOLD_LOCKED)) {
#ifdef PMAP_DEBUG
				pv->pv_func = func;
				pv->pv_line = lineno;
#endif
				return;
			}
			continue;
		}
		tsleep_interlock(pv, 0);
		if (atomic_cmpset_int(&pv->pv_hold, count,
				      count | PV_HOLD_WAITING)) {
#ifdef PMAP_DEBUG
			kprintf("pv waiting on %s:%d\n",
					pv->pv_func, pv->pv_line);
#endif
			tsleep(pv, PINTERLOCKED, "pvwait", hz);
		}
		/* retry */
	}
}

/*
 * Unlock a held and locked pv, keeping the hold count.
 */
static
void
pv_unlock(pv_entry_t pv)
{
	u_int count;

	if (atomic_cmpset_int(&pv->pv_hold, PV_HOLD_LOCKED | 1, 1))
		return;

	for (;;) {
		count = pv->pv_hold;
		cpu_ccfence();
		KKASSERT((count & (PV_HOLD_LOCKED|PV_HOLD_MASK)) >=
			 (PV_HOLD_LOCKED | 1));
		if (atomic_cmpset_int(&pv->pv_hold, count,
				      count &
				      ~(PV_HOLD_LOCKED | PV_HOLD_WAITING))) {
			if (count & PV_HOLD_WAITING)
				wakeup(pv);
			break;
		}
	}
}

/*
 * Unlock and drop a pv.  If the pv is no longer associated with a pmap
 * and the hold count drops to zero we will free it.
 *
 * Caller should not hold any spin locks.  We are protected from hold races
 * by virtue of holds only occuring only with a pmap_spin or vm_page_spin
 * lock held.  A pv cannot be located otherwise.
 */
static
void
pv_put(pv_entry_t pv)
{
	if (atomic_cmpset_int(&pv->pv_hold, PV_HOLD_LOCKED | 1, 0)) {
		if (pv->pv_pmap == NULL)
			zfree(pvzone, pv);
		return;
	}
	pv_unlock(pv);
	pv_drop(pv);
}

/*
 * Unlock, drop, and free a pv, destroying it.  The pv is removed from its
 * pmap.  Any pte operations must have already been completed.
 */
static
void
pv_free(pv_entry_t pv)
{
	pmap_t pmap;

	KKASSERT(pv->pv_m == NULL);
	if ((pmap = pv->pv_pmap) != NULL) {
		spin_lock(&pmap->pm_spin);
		pv_entry_rb_tree_RB_REMOVE(&pmap->pm_pvroot, pv);
		if (pmap->pm_pvhint == pv)
			pmap->pm_pvhint = NULL;
		atomic_add_long(&pmap->pm_stats.resident_count, -1);
		pv->pv_pmap = NULL;
		pv->pv_pindex = 0;
		spin_unlock(&pmap->pm_spin);
	}
	pv_put(pv);
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
}

/*
 * Scan the pmap for active page table entries and issue a callback.
 * The callback must dispose of pte_pv, whos PTE entry is at *ptep in
 * its parent page table.
 *
 * pte_pv will be NULL if the page or page table is unmanaged.
 * pt_pv will point to the page table page containing the pte for the page.
 *
 * NOTE! If we come across an unmanaged page TABLE (verses an unmanaged page),
 *	 we pass a NULL pte_pv and we pass a pt_pv pointing to the passed
 *	 process pmap's PD and page to the callback function.  This can be
 *	 confusing because the pt_pv is really a pd_pv, and the target page
 *	 table page is simply aliased by the pmap and not owned by it.
 *
 * It is assumed that the start and end are properly rounded to the page size.
 *
 * It is assumed that PD pages and above are managed and thus in the RB tree,
 * allowing us to use RB_SCAN from the PD pages down for ranged scans.
 */
struct pmap_scan_info {
	struct pmap *pmap;
	vm_offset_t sva;
	vm_offset_t eva;
	vm_pindex_t sva_pd_pindex;
	vm_pindex_t eva_pd_pindex;
	void (*func)(pmap_t, struct pmap_scan_info *,
		     pv_entry_t, pv_entry_t, int, vm_offset_t,
		     pt_entry_t *, void *);
	void *arg;
	int doinval;
	struct pmap_inval_info inval;
};

static int pmap_scan_cmp(pv_entry_t pv, void *data);
static int pmap_scan_callback(pv_entry_t pv, void *data);

static void
pmap_scan(struct pmap_scan_info *info)
{
	struct pmap *pmap = info->pmap;
	pv_entry_t pd_pv;	/* A page directory PV */
	pv_entry_t pt_pv;	/* A page table PV */
	pv_entry_t pte_pv;	/* A page table entry PV */
	pt_entry_t *ptep;
	struct pv_entry dummy_pv;

	if (pmap == NULL)
		return;

	/*
	 * Hold the token for stability; if the pmap is empty we have nothing
	 * to do.
	 */
	lwkt_gettoken(&pmap->pm_token);
#if 0
	if (pmap->pm_stats.resident_count == 0) {
		lwkt_reltoken(&pmap->pm_token);
		return;
	}
#endif

	pmap_inval_init(&info->inval);

	/*
	 * Special handling for scanning one page, which is a very common
	 * operation (it is?).
	 *
	 * NOTE: Locks must be ordered bottom-up. pte,pt,pd,pdp,pml4
	 */
	if (info->sva + PAGE_SIZE == info->eva) {
		if (info->sva >= VM_MAX_USER_ADDRESS) {
			/*
			 * Kernel mappings do not track wire counts on
			 * page table pages and only maintain pd_pv and
			 * pte_pv levels so pmap_scan() works.
			 */
			pt_pv = NULL;
			pte_pv = pv_get(pmap, pmap_pte_pindex(info->sva));
			ptep = vtopte(info->sva);
		} else {
			/*
			 * User pages which are unmanaged will not have a
			 * pte_pv.  User page table pages which are unmanaged
			 * (shared from elsewhere) will also not have a pt_pv.
			 * The func() callback will pass both pte_pv and pt_pv
			 * as NULL in that case.
			 */
			pte_pv = pv_get(pmap, pmap_pte_pindex(info->sva));
			pt_pv = pv_get(pmap, pmap_pt_pindex(info->sva));
			if (pt_pv == NULL) {
				KKASSERT(pte_pv == NULL);
				pd_pv = pv_get(pmap, pmap_pd_pindex(info->sva));
				if (pd_pv) {
					ptep = pv_pte_lookup(pd_pv,
						    pmap_pt_index(info->sva));
					if (*ptep) {
						info->func(pmap, info,
						     NULL, pd_pv, 1,
						     info->sva, ptep,
						     info->arg);
					}
					pv_put(pd_pv);
				}
				goto fast_skip;
			}
			ptep = pv_pte_lookup(pt_pv, pmap_pte_index(info->sva));
		}
		if (*ptep == 0) {
			/*
			 * Unlike the pv_find() case below we actually
			 * acquired a locked pv in this case so any
			 * race should have been resolved.  It is expected
			 * to not exist.
			 */
			KKASSERT(pte_pv == NULL);
		} else if (pte_pv) {
			KASSERT((*ptep & (PG_MANAGED|PG_V)) == (PG_MANAGED|
								PG_V),
				("bad *ptep %016lx sva %016lx pte_pv %p",
				*ptep, info->sva, pte_pv));
			info->func(pmap, info, pte_pv, pt_pv, 0,
				   info->sva, ptep, info->arg);
		} else {
			KASSERT((*ptep & (PG_MANAGED|PG_V)) == PG_V,
				("bad *ptep %016lx sva %016lx pte_pv NULL",
				*ptep, info->sva));
			info->func(pmap, info, NULL, pt_pv, 0,
				   info->sva, ptep, info->arg);
		}
		if (pt_pv)
			pv_put(pt_pv);
fast_skip:
		pmap_inval_done(&info->inval);
		lwkt_reltoken(&pmap->pm_token);
		return;
	}

	/*
	 * Nominal scan case, RB_SCAN() for PD pages and iterate from
	 * there.
	 */
	info->sva_pd_pindex = pmap_pd_pindex(info->sva);
	info->eva_pd_pindex = pmap_pd_pindex(info->eva + NBPDP - 1);

	if (info->sva >= VM_MAX_USER_ADDRESS) {
		/*
		 * The kernel does not currently maintain any pv_entry's for
		 * higher-level page tables.
		 */
		bzero(&dummy_pv, sizeof(dummy_pv));
		dummy_pv.pv_pindex = info->sva_pd_pindex;
		spin_lock(&pmap->pm_spin);
		while (dummy_pv.pv_pindex < info->eva_pd_pindex) {
			pmap_scan_callback(&dummy_pv, info);
			++dummy_pv.pv_pindex;
		}
		spin_unlock(&pmap->pm_spin);
	} else {
		/*
		 * User page tables maintain local PML4, PDP, and PD
		 * pv_entry's at the very least.  PT pv's might be
		 * unmanaged and thus not exist.  PTE pv's might be
		 * unmanaged and thus not exist.
		 */
		spin_lock(&pmap->pm_spin);
		pv_entry_rb_tree_RB_SCAN(&pmap->pm_pvroot,
			pmap_scan_cmp, pmap_scan_callback, info);
		spin_unlock(&pmap->pm_spin);
	}
	pmap_inval_done(&info->inval);
	lwkt_reltoken(&pmap->pm_token);
}

/*
 * WARNING! pmap->pm_spin held
 */
static int
pmap_scan_cmp(pv_entry_t pv, void *data)
{
	struct pmap_scan_info *info = data;
	if (pv->pv_pindex < info->sva_pd_pindex)
		return(-1);
	if (pv->pv_pindex >= info->eva_pd_pindex)
		return(1);
	return(0);
}

/*
 * WARNING! pmap->pm_spin held
 */
static int
pmap_scan_callback(pv_entry_t pv, void *data)
{
	struct pmap_scan_info *info = data;
	struct pmap *pmap = info->pmap;
	pv_entry_t pd_pv;	/* A page directory PV */
	pv_entry_t pt_pv;	/* A page table PV */
	pv_entry_t pte_pv;	/* A page table entry PV */
	pt_entry_t *ptep;
	vm_offset_t sva;
	vm_offset_t eva;
	vm_offset_t va_next;
	vm_pindex_t pd_pindex;
	int error;

	/*
	 * Pull the PD pindex from the pv before releasing the spinlock.
	 *
	 * WARNING: pv is faked for kernel pmap scans.
	 */
	pd_pindex = pv->pv_pindex;
	spin_unlock(&pmap->pm_spin);
	pv = NULL;	/* invalid after spinlock unlocked */

	/*
	 * Calculate the page range within the PD.  SIMPLE pmaps are
	 * direct-mapped for the entire 2^64 address space.  Normal pmaps
	 * reflect the user and kernel address space which requires
	 * cannonicalization w/regards to converting pd_pindex's back
	 * into addresses.
	 */
	sva = (pd_pindex - NUPTE_TOTAL - NUPT_TOTAL) << PDPSHIFT;
	if ((pmap->pm_flags & PMAP_FLAG_SIMPLE) == 0 &&
	    (sva & PML4_SIGNMASK)) {
		sva |= PML4_SIGNMASK;
	}
	eva = sva + NBPDP;	/* can overflow */
	if (sva < info->sva)
		sva = info->sva;
	if (eva < info->sva || eva > info->eva)
		eva = info->eva;

	/*
	 * NOTE: kernel mappings do not track page table pages, only
	 * 	 terminal pages.
	 *
	 * NOTE: Locks must be ordered bottom-up. pte,pt,pd,pdp,pml4.
	 *	 However, for the scan to be efficient we try to
	 *	 cache items top-down.
	 */
	pd_pv = NULL;
	pt_pv = NULL;

	for (; sva < eva; sva = va_next) {
		if (sva >= VM_MAX_USER_ADDRESS) {
			if (pt_pv) {
				pv_put(pt_pv);
				pt_pv = NULL;
			}
			goto kernel_skip;
		}

		/*
		 * PD cache (degenerate case if we skip).  It is possible
		 * for the PD to not exist due to races.  This is ok.
		 */
		if (pd_pv == NULL) {
			pd_pv = pv_get(pmap, pmap_pd_pindex(sva));
		} else if (pd_pv->pv_pindex != pmap_pd_pindex(sva)) {
			pv_put(pd_pv);
			pd_pv = pv_get(pmap, pmap_pd_pindex(sva));
		}
		if (pd_pv == NULL) {
			va_next = (sva + NBPDP) & ~PDPMASK;
			if (va_next < sva)
				va_next = eva;
			continue;
		}

		/*
		 * PT cache
		 */
		if (pt_pv == NULL) {
			if (pd_pv) {
				pv_put(pd_pv);
				pd_pv = NULL;
			}
			pt_pv = pv_get(pmap, pmap_pt_pindex(sva));
		} else if (pt_pv->pv_pindex != pmap_pt_pindex(sva)) {
			if (pd_pv) {
				pv_put(pd_pv);
				pd_pv = NULL;
			}
			pv_put(pt_pv);
			pt_pv = pv_get(pmap, pmap_pt_pindex(sva));
		}

		/*
		 * If pt_pv is NULL we either have an shared page table
		 * page and must issue a callback specific to that case,
		 * or there is no page table page.
		 *
		 * Either way we can skip the page table page.
		 */
		if (pt_pv == NULL) {
			/*
			 * Possible unmanaged (shared from another pmap)
			 * page table page.
			 */
			if (pd_pv == NULL)
				pd_pv = pv_get(pmap, pmap_pd_pindex(sva));
			KKASSERT(pd_pv != NULL);
			ptep = pv_pte_lookup(pd_pv, pmap_pt_index(sva));
			if (*ptep & PG_V) {
				info->func(pmap, info, NULL, pd_pv, 1,
					   sva, ptep, info->arg);
			}

			/*
			 * Done, move to next page table page.
			 */
			va_next = (sva + NBPDR) & ~PDRMASK;
			if (va_next < sva)
				va_next = eva;
			continue;
		}

		/*
		 * From this point in the loop testing pt_pv for non-NULL
		 * means we are in UVM, else if it is NULL we are in KVM.
		 *
		 * Limit our scan to either the end of the va represented
		 * by the current page table page, or to the end of the
		 * range being removed.
		 */
kernel_skip:
		va_next = (sva + NBPDR) & ~PDRMASK;
		if (va_next < sva)
			va_next = eva;
		if (va_next > eva)
			va_next = eva;

		/*
		 * Scan the page table for pages.  Some pages may not be
		 * managed (might not have a pv_entry).
		 *
		 * There is no page table management for kernel pages so
		 * pt_pv will be NULL in that case, but otherwise pt_pv
		 * is non-NULL, locked, and referenced.
		 */

		/*
		 * At this point a non-NULL pt_pv means a UVA, and a NULL
		 * pt_pv means a KVA.
		 */
		if (pt_pv)
			ptep = pv_pte_lookup(pt_pv, pmap_pte_index(sva));
		else
			ptep = vtopte(sva);

		while (sva < va_next) {
			/*
			 * Acquire the related pte_pv, if any.  If *ptep == 0
			 * the related pte_pv should not exist, but if *ptep
			 * is not zero the pte_pv may or may not exist (e.g.
			 * will not exist for an unmanaged page).
			 *
			 * However a multitude of races are possible here.
			 *
			 * In addition, the (pt_pv, pte_pv) lock order is
			 * backwards, so we have to be careful in aquiring
			 * a properly locked pte_pv.
			 */
			if (pt_pv) {
				pte_pv = pv_get_try(pmap, pmap_pte_pindex(sva),
						    &error);
				if (error) {
					if (pd_pv) {
						pv_put(pd_pv);
						pd_pv = NULL;
					}
					pv_put(pt_pv);	 /* must be non-NULL */
					pt_pv = NULL;
					pv_lock(pte_pv); /* safe to block now */
					pv_put(pte_pv);
					pte_pv = NULL;
					pt_pv = pv_get(pmap,
						       pmap_pt_pindex(sva));
					/*
					 * pt_pv reloaded, need new ptep
					 */
					KKASSERT(pt_pv != NULL);
					ptep = pv_pte_lookup(pt_pv,
							pmap_pte_index(sva));
					continue;
				}
			} else {
				pte_pv = pv_get(pmap, pmap_pte_pindex(sva));
			}

			/*
			 * Ok, if *ptep == 0 we had better NOT have a pte_pv.
			 */
			if (*ptep == 0) {
				if (pte_pv) {
					kprintf("Unexpected non-NULL pte_pv "
						"%p pt_pv %p *ptep = %016lx\n",
						pte_pv, pt_pv, *ptep);
					panic("Unexpected non-NULL pte_pv");
				}
				sva += PAGE_SIZE;
				++ptep;
				continue;
			}

			/*
			 * Ready for the callback.  The locked pte_pv (if any)
			 * is consumed by the callback.  pte_pv will exist if
			 *  the page is managed, and will not exist if it
			 * isn't.
			 */
			if (pte_pv) {
				KASSERT((*ptep & (PG_MANAGED|PG_V)) ==
					 (PG_MANAGED|PG_V),
					("bad *ptep %016lx sva %016lx "
					 "pte_pv %p",
					 *ptep, sva, pte_pv));
				info->func(pmap, info, pte_pv, pt_pv, 0,
					   sva, ptep, info->arg);
			} else {
				KASSERT((*ptep & (PG_MANAGED|PG_V)) ==
					 PG_V,
					("bad *ptep %016lx sva %016lx "
					 "pte_pv NULL",
					 *ptep, sva));
				info->func(pmap, info, NULL, pt_pv, 0,
					   sva, ptep, info->arg);
			}
			pte_pv = NULL;
			sva += PAGE_SIZE;
			++ptep;
		}
		lwkt_yield();
	}
	if (pd_pv) {
		pv_put(pd_pv);
		pd_pv = NULL;
	}
	if (pt_pv) {
		pv_put(pt_pv);
		pt_pv = NULL;
	}
	lwkt_yield();

	/*
	 * Relock before returning.
	 */
	spin_lock(&pmap->pm_spin);
	return (0);
}

void
pmap_remove(struct pmap *pmap, vm_offset_t sva, vm_offset_t eva)
{
	struct pmap_scan_info info;

	info.pmap = pmap;
	info.sva = sva;
	info.eva = eva;
	info.func = pmap_remove_callback;
	info.arg = NULL;
	info.doinval = 1;	/* normal remove requires pmap inval */
	pmap_scan(&info);
}

static void
pmap_remove_noinval(struct pmap *pmap, vm_offset_t sva, vm_offset_t eva)
{
	struct pmap_scan_info info;

	info.pmap = pmap;
	info.sva = sva;
	info.eva = eva;
	info.func = pmap_remove_callback;
	info.arg = NULL;
	info.doinval = 0;	/* normal remove requires pmap inval */
	pmap_scan(&info);
}

static void
pmap_remove_callback(pmap_t pmap, struct pmap_scan_info *info,
		     pv_entry_t pte_pv, pv_entry_t pt_pv, int sharept,
		     vm_offset_t va, pt_entry_t *ptep, void *arg __unused)
{
	pt_entry_t pte;

	if (pte_pv) {
		/*
		 * This will also drop pt_pv's wire_count. Note that
		 * terminal pages are not wired based on mmu presence.
		 */
		if (info->doinval)
			pmap_remove_pv_pte(pte_pv, pt_pv, &info->inval);
		else
			pmap_remove_pv_pte(pte_pv, pt_pv, NULL);
		pmap_remove_pv_page(pte_pv);
		pv_free(pte_pv);
	} else if (sharept == 0) {
		/*
		 * Unmanaged page
		 *
		 * pt_pv's wire_count is still bumped by unmanaged pages
		 * so we must decrement it manually.
		 */
		if (info->doinval)
			pmap_inval_interlock(&info->inval, pmap, va);
		pte = pte_load_clear(ptep);
		if (info->doinval)
			pmap_inval_deinterlock(&info->inval, pmap);
		if (pte & PG_W)
			atomic_add_long(&pmap->pm_stats.wired_count, -1);
		atomic_add_long(&pmap->pm_stats.resident_count, -1);
		if (vm_page_unwire_quick(pt_pv->pv_m))
			panic("pmap_remove: insufficient wirecount");
	} else {
		/*
		 * Unmanaged page table, pt_pv is actually the pd_pv
		 * for our pmap (not the share object pmap).
		 *
		 * We have to unwire the target page table page and we
		 * have to unwire our page directory page.
		 */
		if (info->doinval)
			pmap_inval_interlock(&info->inval, pmap, va);
		pte = pte_load_clear(ptep);
		if (info->doinval)
			pmap_inval_deinterlock(&info->inval, pmap);
		atomic_add_long(&pmap->pm_stats.resident_count, -1);
		if (vm_page_unwire_quick(PHYS_TO_VM_PAGE(pte & PG_FRAME)))
			panic("pmap_remove: shared pgtable1 bad wirecount");
		if (vm_page_unwire_quick(pt_pv->pv_m))
			panic("pmap_remove: shared pgtable2 bad wirecount");
	}
}

/*
 * Removes this physical page from all physical maps in which it resides.
 * Reflects back modify bits to the pager.
 *
 * This routine may not be called from an interrupt.
 */
static
void
pmap_remove_all(vm_page_t m)
{
	struct pmap_inval_info info;
	pv_entry_t pv;

	if (!pmap_initialized || (m->flags & PG_FICTITIOUS))
		return;

	pmap_inval_init(&info);
	vm_page_spin_lock(m);
	while ((pv = TAILQ_FIRST(&m->md.pv_list)) != NULL) {
		KKASSERT(pv->pv_m == m);
		if (pv_hold_try(pv)) {
			vm_page_spin_unlock(m);
		} else {
			vm_page_spin_unlock(m);
			pv_lock(pv);
			if (pv->pv_m != m) {
				pv_put(pv);
				vm_page_spin_lock(m);
				continue;
			}
		}
		/*
		 * Holding no spinlocks, pv is locked.
		 */
		pmap_remove_pv_pte(pv, NULL, &info);
		pmap_remove_pv_page(pv);
		pv_free(pv);
		vm_page_spin_lock(m);
	}
	KKASSERT((m->flags & (PG_MAPPED|PG_WRITEABLE)) == 0);
	vm_page_spin_unlock(m);
	pmap_inval_done(&info);
}

/*
 * Set the physical protection on the specified range of this map
 * as requested.  This function is typically only used for debug watchpoints
 * and COW pages.
 *
 * This function may not be called from an interrupt if the map is
 * not the kernel_pmap.
 *
 * NOTE!  For shared page table pages we just unmap the page.
 */
void
pmap_protect(pmap_t pmap, vm_offset_t sva, vm_offset_t eva, vm_prot_t prot)
{
	struct pmap_scan_info info;
	/* JG review for NX */

	if (pmap == NULL)
		return;
	if ((prot & VM_PROT_READ) == VM_PROT_NONE) {
		pmap_remove(pmap, sva, eva);
		return;
	}
	if (prot & VM_PROT_WRITE)
		return;
	info.pmap = pmap;
	info.sva = sva;
	info.eva = eva;
	info.func = pmap_protect_callback;
	info.arg = &prot;
	info.doinval = 1;
	pmap_scan(&info);
}

static
void
pmap_protect_callback(pmap_t pmap, struct pmap_scan_info *info,
		      pv_entry_t pte_pv, pv_entry_t pt_pv, int sharept,
		      vm_offset_t va, pt_entry_t *ptep, void *arg __unused)
{
	pt_entry_t pbits;
	pt_entry_t cbits;
	pt_entry_t pte;
	vm_page_t m;

	/*
	 * XXX non-optimal.
	 */
	pmap_inval_interlock(&info->inval, pmap, va);
again:
	pbits = *ptep;
	cbits = pbits;
	if (pte_pv) {
		m = NULL;
		if (pbits & PG_A) {
			m = PHYS_TO_VM_PAGE(pbits & PG_FRAME);
			KKASSERT(m == pte_pv->pv_m);
			vm_page_flag_set(m, PG_REFERENCED);
			cbits &= ~PG_A;
		}
		if (pbits & PG_M) {
			if (pmap_track_modified(pte_pv->pv_pindex)) {
				if (m == NULL)
					m = PHYS_TO_VM_PAGE(pbits & PG_FRAME);
				vm_page_dirty(m);
				cbits &= ~PG_M;
			}
		}
	} else if (sharept) {
		/*
		 * Unmanaged page table, pt_pv is actually the pd_pv
		 * for our pmap (not the share object pmap).
		 *
		 * When asked to protect something in a shared page table
		 * page we just unmap the page table page.  We have to
		 * invalidate the tlb in this situation.
		 */
		pte = pte_load_clear(ptep);
		pmap_inval_invltlb(&info->inval);
		if (vm_page_unwire_quick(PHYS_TO_VM_PAGE(pte & PG_FRAME)))
			panic("pmap_protect: pgtable1 pg bad wirecount");
		if (vm_page_unwire_quick(pt_pv->pv_m))
			panic("pmap_protect: pgtable2 pg bad wirecount");
		ptep = NULL;
	}
	/* else unmanaged page, adjust bits, no wire changes */

	if (ptep) {
		cbits &= ~PG_RW;
		if (pbits != cbits && !atomic_cmpset_long(ptep, pbits, cbits)) {
			goto again;
		}
	}
	pmap_inval_deinterlock(&info->inval, pmap);
	if (pte_pv)
		pv_put(pte_pv);
}

/*
 * Insert the vm_page (m) at the virtual address (va), replacing any prior
 * mapping at that address.  Set protection and wiring as requested.
 *
 * If entry is non-NULL we check to see if the SEG_SIZE optimization is
 * possible.  If it is we enter the page into the appropriate shared pmap
 * hanging off the related VM object instead of the passed pmap, then we
 * share the page table page from the VM object's pmap into the current pmap.
 *
 * NOTE: This routine MUST insert the page into the pmap now, it cannot
 *	 lazy-evaluate.
 */
void
pmap_enter(pmap_t pmap, vm_offset_t va, vm_page_t m, vm_prot_t prot,
	   boolean_t wired, vm_map_entry_t entry)
{
	pmap_inval_info info;
	pv_entry_t pt_pv;	/* page table */
	pv_entry_t pte_pv;	/* page table entry */
	pt_entry_t *ptep;
	vm_paddr_t opa;
	pt_entry_t origpte, newpte;
	vm_paddr_t pa;

	if (pmap == NULL)
		return;
	va = trunc_page(va);
#ifdef PMAP_DIAGNOSTIC
	if (va >= KvaEnd)
		panic("pmap_enter: toobig");
	if ((va >= UPT_MIN_ADDRESS) && (va < UPT_MAX_ADDRESS))
		panic("pmap_enter: invalid to pmap_enter page table "
		      "pages (va: 0x%lx)", va);
#endif
	if (va < UPT_MAX_ADDRESS && pmap == &kernel_pmap) {
		kprintf("Warning: pmap_enter called on UVA with "
			"kernel_pmap\n");
#ifdef DDB
		db_print_backtrace();
#endif
	}
	if (va >= UPT_MAX_ADDRESS && pmap != &kernel_pmap) {
		kprintf("Warning: pmap_enter called on KVA without"
			"kernel_pmap\n");
#ifdef DDB
		db_print_backtrace();
#endif
	}

	/*
	 * Get locked PV entries for our new page table entry (pte_pv)
	 * and for its parent page table (pt_pv).  We need the parent
	 * so we can resolve the location of the ptep.
	 *
	 * Only hardware MMU actions can modify the ptep out from
	 * under us.
	 *
	 * if (m) is fictitious or unmanaged we do not create a managing
	 * pte_pv for it.  Any pre-existing page's management state must
	 * match (avoiding code complexity).
	 *
	 * If the pmap is still being initialized we assume existing
	 * page tables.
	 *
	 * Kernel mapppings do not track page table pages (i.e. pt_pv).
	 * pmap_allocpte() checks the
	 */
	if (pmap_initialized == FALSE) {
		pte_pv = NULL;
		pt_pv = NULL;
		ptep = vtopte(va);
	} else if (m->flags & (PG_FICTITIOUS | PG_UNMANAGED)) { /* XXX */
		pte_pv = NULL;
		if (va >= VM_MAX_USER_ADDRESS) {
			pt_pv = NULL;
			ptep = vtopte(va);
		} else {
			pt_pv = pmap_allocpte_seg(pmap, pmap_pt_pindex(va),
						  NULL, entry, va);
			ptep = pv_pte_lookup(pt_pv, pmap_pte_index(va));
		}
		KKASSERT(*ptep == 0 || (*ptep & PG_MANAGED) == 0);
	} else {
		if (va >= VM_MAX_USER_ADDRESS) {
			/*
			 * Kernel map, pv_entry-tracked.
			 */
			pt_pv = NULL;
			pte_pv = pmap_allocpte(pmap, pmap_pte_pindex(va), NULL);
			ptep = vtopte(va);
		} else {
			/*
			 * User map
			 */
			pte_pv = pmap_allocpte_seg(pmap, pmap_pte_pindex(va),
						   &pt_pv, entry, va);
			ptep = pv_pte_lookup(pt_pv, pmap_pte_index(va));
		}
		KKASSERT(*ptep == 0 || (*ptep & PG_MANAGED));
	}

	pa = VM_PAGE_TO_PHYS(m);
	origpte = *ptep;
	opa = origpte & PG_FRAME;

	newpte = (pt_entry_t)(pa | pte_prot(pmap, prot) | PG_V | PG_A);
	if (wired)
		newpte |= PG_W;
	if (va < VM_MAX_USER_ADDRESS)
		newpte |= PG_U;
	if (pte_pv)
		newpte |= PG_MANAGED;
	if (pmap == &kernel_pmap)
		newpte |= pgeflag;
	newpte |= pat_pte_index[m->pat_mode];

	/*
	 * It is possible for multiple faults to occur in threaded
	 * environments, the existing pte might be correct.
	 */
	if (((origpte ^ newpte) & ~(pt_entry_t)(PG_M|PG_A)) == 0)
		goto done;

	if ((prot & VM_PROT_NOSYNC) == 0)
		pmap_inval_init(&info);

	/*
	 * Ok, either the address changed or the protection or wiring
	 * changed.
	 *
	 * Clear the current entry, interlocking the removal.  For managed
	 * pte's this will also flush the modified state to the vm_page.
	 * Atomic ops are mandatory in order to ensure that PG_M events are
	 * not lost during any transition.
	 */
	if (opa) {
		if (pte_pv) {
			/*
			 * pmap_remove_pv_pte() unwires pt_pv and assumes
			 * we will free pte_pv, but since we are reusing
			 * pte_pv we want to retain the wire count.
			 *
			 * pt_pv won't exist for a kernel page (managed or
			 * otherwise).
			 */
			if (pt_pv)
				vm_page_wire_quick(pt_pv->pv_m);
			if (prot & VM_PROT_NOSYNC)
				pmap_remove_pv_pte(pte_pv, pt_pv, NULL);
			else
				pmap_remove_pv_pte(pte_pv, pt_pv, &info);
			if (pte_pv->pv_m)
				pmap_remove_pv_page(pte_pv);
		} else if (prot & VM_PROT_NOSYNC) {
			/*
			 * Unmanaged page, NOSYNC (no mmu sync) requested.
			 *
			 * Leave wire count on PT page intact.
			 */
			(void)pte_load_clear(ptep);
			cpu_invlpg((void *)va);
			atomic_add_long(&pmap->pm_stats.resident_count, -1);
		} else {
			/*
			 * Unmanaged page, normal enter.
			 *
			 * Leave wire count on PT page intact.
			 */
			pmap_inval_interlock(&info, pmap, va);
			(void)pte_load_clear(ptep);
			pmap_inval_deinterlock(&info, pmap);
			atomic_add_long(&pmap->pm_stats.resident_count, -1);
		}
		KKASSERT(*ptep == 0);
	}

	if (pte_pv) {
		/*
		 * Enter on the PV list if part of our managed memory.
		 * Wiring of the PT page is already handled.
		 */
		KKASSERT(pte_pv->pv_m == NULL);
		vm_page_spin_lock(m);
		pte_pv->pv_m = m;
		TAILQ_INSERT_TAIL(&m->md.pv_list, pte_pv, pv_list);
		/*
		if (m->object)
			atomic_add_int(&m->object->agg_pv_list_count, 1);
		*/
		vm_page_flag_set(m, PG_MAPPED);
		vm_page_spin_unlock(m);
	} else if (pt_pv && opa == 0) {
		/*
		 * We have to adjust the wire count on the PT page ourselves
		 * for unmanaged entries.  If opa was non-zero we retained
		 * the existing wire count from the removal.
		 */
		vm_page_wire_quick(pt_pv->pv_m);
	}

	/*
	 * Kernel VMAs (pt_pv == NULL) require pmap invalidation interlocks.
	 *
	 * User VMAs do not because those will be zero->non-zero, so no
	 * stale entries to worry about at this point.
	 *
	 * For KVM there appear to still be issues.  Theoretically we
	 * should be able to scrap the interlocks entirely but we
	 * get crashes.
	 */
	if ((prot & VM_PROT_NOSYNC) == 0 && pt_pv == NULL)
		pmap_inval_interlock(&info, pmap, va);

	/*
	 * Set the pte
	 */
	*(volatile pt_entry_t *)ptep = newpte;

	if ((prot & VM_PROT_NOSYNC) == 0 && pt_pv == NULL)
		pmap_inval_deinterlock(&info, pmap);
	else if (pt_pv == NULL)
		cpu_invlpg((void *)va);

	if (wired) {
		if (pte_pv) {
			atomic_add_long(&pte_pv->pv_pmap->pm_stats.wired_count,
					1);
		} else {
			atomic_add_long(&pmap->pm_stats.wired_count, 1);
		}
	}
	if (newpte & PG_RW)
		vm_page_flag_set(m, PG_WRITEABLE);

	/*
	 * Unmanaged pages need manual resident_count tracking.
	 */
	if (pte_pv == NULL && pt_pv)
		atomic_add_long(&pt_pv->pv_pmap->pm_stats.resident_count, 1);

	/*
	 * Cleanup
	 */
	if ((prot & VM_PROT_NOSYNC) == 0 || pte_pv == NULL)
		pmap_inval_done(&info);
done:
	KKASSERT((newpte & PG_MANAGED) == 0 || (m->flags & PG_MAPPED));

	/*
	 * Cleanup the pv entry, allowing other accessors.
	 */
	if (pte_pv)
		pv_put(pte_pv);
	if (pt_pv)
		pv_put(pt_pv);
}

/*
 * This code works like pmap_enter() but assumes VM_PROT_READ and not-wired.
 * This code also assumes that the pmap has no pre-existing entry for this
 * VA.
 *
 * This code currently may only be used on user pmaps, not kernel_pmap.
 */
void
pmap_enter_quick(pmap_t pmap, vm_offset_t va, vm_page_t m)
{
	pmap_enter(pmap, va, m, VM_PROT_READ, FALSE, NULL);
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
	pmap_kenter_quick((vm_offset_t)crashdumpmap + (i * PAGE_SIZE), pa);
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

	/*
	 * Misc additional checks
	 */
	psize = x86_64_btop(size);

	if ((object->type != OBJT_VNODE) ||
		((limit & MAP_PREFAULT_PARTIAL) && (psize > MAX_INIT_PT) &&
			(object->resident_page_count > MAX_INIT_PT))) {
		return;
	}

	if (pindex + psize > object->size) {
		if (object->size < pindex)
			return;		  
		psize = object->size - pindex;
	}

	if (psize == 0)
		return;

	/*
	 * If everything is segment-aligned do not pre-init here.  Instead
	 * allow the normal vm_fault path to pass a segment hint to
	 * pmap_enter() which will then use an object-referenced shared
	 * page table page.
	 */
	if ((addr & SEG_MASK) == 0 &&
	    (ctob(psize) & SEG_MASK) == 0 &&
	    (ctob(pindex) & SEG_MASK) == 0) {
		return;
	}

	/*
	 * Use a red-black scan to traverse the requested range and load
	 * any valid pages found into the pmap.
	 *
	 * We cannot safely scan the object's memq without holding the
	 * object token.
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
	lwkt_yield();
	return(0);
}

/*
 * Return TRUE if the pmap is in shape to trivially pre-fault the specified
 * address.
 *
 * Returns FALSE if it would be non-trivial or if a pte is already loaded
 * into the slot.
 *
 * XXX This is safe only because page table pages are not freed.
 */
int
pmap_prefault_ok(pmap_t pmap, vm_offset_t addr)
{
	pt_entry_t *pte;

	/*spin_lock(&pmap->pm_spin);*/
	if ((pte = pmap_pte(pmap, addr)) != NULL) {
		if (*pte & PG_V) {
			/*spin_unlock(&pmap->pm_spin);*/
			return FALSE;
		}
	}
	/*spin_unlock(&pmap->pm_spin);*/
	return TRUE;
}

/*
 * Change the wiring attribute for a pmap/va pair.  The mapping must already
 * exist in the pmap.  The mapping may or may not be managed.
 */
void
pmap_change_wiring(pmap_t pmap, vm_offset_t va, boolean_t wired,
		   vm_map_entry_t entry)
{
	pt_entry_t *ptep;
	pv_entry_t pv;

	if (pmap == NULL)
		return;
	lwkt_gettoken(&pmap->pm_token);
	pv = pmap_allocpte_seg(pmap, pmap_pt_pindex(va), NULL, entry, va);
	ptep = pv_pte_lookup(pv, pmap_pte_index(va));

	if (wired && !pmap_pte_w(ptep))
		atomic_add_long(&pv->pv_pmap->pm_stats.wired_count, 1);
	else if (!wired && pmap_pte_w(ptep))
		atomic_add_long(&pv->pv_pmap->pm_stats.wired_count, -1);

	/*
	 * Wiring is not a hardware characteristic so there is no need to
	 * invalidate TLB.  However, in an SMP environment we must use
	 * a locked bus cycle to update the pte (if we are not using 
	 * the pmap_inval_*() API that is)... it's ok to do this for simple
	 * wiring changes.
	 */
	if (wired)
		atomic_set_long(ptep, PG_W);
	else
		atomic_clear_long(ptep, PG_W);
	pv_put(pv);
	lwkt_reltoken(&pmap->pm_token);
}



/*
 * Copy the range specified by src_addr/len from the source map to
 * the range dst_addr/len in the destination map.
 *
 * This routine is only advisory and need not do anything.
 */
void
pmap_copy(pmap_t dst_pmap, pmap_t src_pmap, vm_offset_t dst_addr, 
	  vm_size_t len, vm_offset_t src_addr)
{
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

	pagezero((void *)va);
}

/*
 * pmap_page_assertzero:
 *
 *	Assert that a page is empty, panic if it isn't.
 */
void
pmap_page_assertzero(vm_paddr_t phys)
{
	vm_offset_t va = PHYS_TO_DMAP(phys);
	size_t i;

	for (i = 0; i < PAGE_SIZE; i += sizeof(long)) {
		if (*(long *)((char *)va + i) != 0) {
			panic("pmap_page_assertzero() @ %p not zero!",
			      (void *)(intptr_t)va);
		}
	}
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
	vm_offset_t virt = PHYS_TO_DMAP(phys);

	bzero((char *)virt + off, size);
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

	src_virt = PHYS_TO_DMAP(src);
	dst_virt = PHYS_TO_DMAP(dst);
	bcopy((void *)src_virt, (void *)dst_virt, PAGE_SIZE);
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

	src_virt = PHYS_TO_DMAP(src);
	dst_virt = PHYS_TO_DMAP(dst);

	bcopy((char *)src_virt + (src & PAGE_MASK),
	      (char *)dst_virt + (dst & PAGE_MASK),
	      bytes);
}

/*
 * Returns true if the pmap's pv is one of the first 16 pvs linked to from
 * this page.  This count may be changed upwards or downwards in the future;
 * it is only necessary that true be returned for a small subset of pmaps
 * for proper page aging.
 */
boolean_t
pmap_page_exists_quick(pmap_t pmap, vm_page_t m)
{
	pv_entry_t pv;
	int loops = 0;

	if (!pmap_initialized || (m->flags & PG_FICTITIOUS))
		return FALSE;

	vm_page_spin_lock(m);
	TAILQ_FOREACH(pv, &m->md.pv_list, pv_list) {
		if (pv->pv_pmap == pmap) {
			vm_page_spin_unlock(m);
			return TRUE;
		}
		loops++;
		if (loops >= 16)
			break;
	}
	vm_page_spin_unlock(m);
	return (FALSE);
}

/*
 * Remove all pages from specified address space this aids process exit
 * speeds.  Also, this code may be special cased for the current process
 * only.
 */
void
pmap_remove_pages(pmap_t pmap, vm_offset_t sva, vm_offset_t eva)
{
	pmap_remove_noinval(pmap, sva, eva);
	cpu_invltlb();
}

/*
 * pmap_testbit tests bits in pte's note that the testbit/clearbit
 * routines are inline, and a lot of things compile-time evaluate.
 */
static
boolean_t
pmap_testbit(vm_page_t m, int bit)
{
	pv_entry_t pv;
	pt_entry_t *pte;

	if (!pmap_initialized || (m->flags & PG_FICTITIOUS))
		return FALSE;

	if (TAILQ_FIRST(&m->md.pv_list) == NULL)
		return FALSE;
	vm_page_spin_lock(m);
	if (TAILQ_FIRST(&m->md.pv_list) == NULL) {
		vm_page_spin_unlock(m);
		return FALSE;
	}

	TAILQ_FOREACH(pv, &m->md.pv_list, pv_list) {
		/*
		 * if the bit being tested is the modified bit, then
		 * mark clean_map and ptes as never
		 * modified.
		 */
		if (bit & (PG_A|PG_M)) {
			if (!pmap_track_modified(pv->pv_pindex))
				continue;
		}

#if defined(PMAP_DIAGNOSTIC)
		if (pv->pv_pmap == NULL) {
			kprintf("Null pmap (tb) at pindex: %"PRIu64"\n",
			    pv->pv_pindex);
			continue;
		}
#endif
		pte = pmap_pte_quick(pv->pv_pmap, pv->pv_pindex << PAGE_SHIFT);
		if (*pte & bit) {
			vm_page_spin_unlock(m);
			return TRUE;
		}
	}
	vm_page_spin_unlock(m);
	return (FALSE);
}

/*
 * This routine is used to modify bits in ptes.  Only one bit should be
 * specified.  PG_RW requires special handling.
 *
 * Caller must NOT hold any spin locks
 */
static __inline
void
pmap_clearbit(vm_page_t m, int bit)
{
	struct pmap_inval_info info;
	pv_entry_t pv;
	pt_entry_t *pte;
	pt_entry_t pbits;
	pmap_t save_pmap;

	if (bit == PG_RW)
		vm_page_flag_clear(m, PG_WRITEABLE);
	if (!pmap_initialized || (m->flags & PG_FICTITIOUS)) {
		return;
	}

	/*
	 * PG_M or PG_A case
	 *
	 * Loop over all current mappings setting/clearing as appropos If
	 * setting RO do we need to clear the VAC?
	 *
	 * NOTE: When clearing PG_M we could also (not implemented) drop
	 *	 through to the PG_RW code and clear PG_RW too, forcing
	 *	 a fault on write to redetect PG_M for virtual kernels, but
	 *	 it isn't necessary since virtual kernels invalidate the
	 *	 pte when they clear the VPTE_M bit in their virtual page
	 *	 tables.
	 *
	 * NOTE: Does not re-dirty the page when clearing only PG_M.
	 */
	if ((bit & PG_RW) == 0) {
		vm_page_spin_lock(m);
		TAILQ_FOREACH(pv, &m->md.pv_list, pv_list) {
	#if defined(PMAP_DIAGNOSTIC)
			if (pv->pv_pmap == NULL) {
				kprintf("Null pmap (cb) at pindex: %"PRIu64"\n",
				    pv->pv_pindex);
				continue;
			}
	#endif
			pte = pmap_pte_quick(pv->pv_pmap,
					     pv->pv_pindex << PAGE_SHIFT);
			pbits = *pte;
			if (pbits & bit)
				atomic_clear_long(pte, bit);
		}
		vm_page_spin_unlock(m);
		return;
	}

	/*
	 * Clear PG_RW.  Also clears PG_M and marks the page dirty if PG_M
	 * was set.
	 */
	pmap_inval_init(&info);

restart:
	vm_page_spin_lock(m);
	TAILQ_FOREACH(pv, &m->md.pv_list, pv_list) {
		/*
		 * don't write protect pager mappings
		 */
		if (!pmap_track_modified(pv->pv_pindex))
			continue;

#if defined(PMAP_DIAGNOSTIC)
		if (pv->pv_pmap == NULL) {
			kprintf("Null pmap (cb) at pindex: %"PRIu64"\n",
			    pv->pv_pindex);
			continue;
		}
#endif
		/*
		 * Skip pages which do not have PG_RW set.
		 */
		pte = pmap_pte_quick(pv->pv_pmap, pv->pv_pindex << PAGE_SHIFT);
		if ((*pte & PG_RW) == 0)
			continue;

		/*
		 * Lock the PV
		 */
		if (pv_hold_try(pv) == 0) {
			vm_page_spin_unlock(m);
			pv_lock(pv);	/* held, now do a blocking lock */
			pv_put(pv);	/* and release */
			goto restart;	/* anything could have happened */
		}

		save_pmap = pv->pv_pmap;
		vm_page_spin_unlock(m);
		pmap_inval_interlock(&info, save_pmap,
				     (vm_offset_t)pv->pv_pindex << PAGE_SHIFT);
		KKASSERT(pv->pv_pmap == save_pmap);
		for (;;) {
			pbits = *pte;
			cpu_ccfence();
			if (atomic_cmpset_long(pte, pbits,
					       pbits & ~(PG_RW|PG_M))) {
				break;
			}
		}
		pmap_inval_deinterlock(&info, save_pmap);
		vm_page_spin_lock(m);

		/*
		 * If PG_M was found to be set while we were clearing PG_RW
		 * we also clear PG_M (done above) and mark the page dirty.
		 * Callers expect this behavior.
		 */
		if (pbits & PG_M)
			vm_page_dirty(m);
		pv_put(pv);
	}
	vm_page_spin_unlock(m);
	pmap_inval_done(&info);
}

/*
 * Lower the permission for all mappings to a given page.
 *
 * Page must be busied by caller.
 */
void
pmap_page_protect(vm_page_t m, vm_prot_t prot)
{
	/* JG NX support? */
	if ((prot & VM_PROT_WRITE) == 0) {
		if (prot & (VM_PROT_READ | VM_PROT_EXECUTE)) {
			/*
			 * NOTE: pmap_clearbit(.. PG_RW) also clears
			 *	 the PG_WRITEABLE flag in (m).
			 */
			pmap_clearbit(m, PG_RW);
		} else {
			pmap_remove_all(m);
		}
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
 * This routine may not block.
 */
int
pmap_ts_referenced(vm_page_t m)
{
	pv_entry_t pv;
	pt_entry_t *pte;
	int rtval = 0;

	if (!pmap_initialized || (m->flags & PG_FICTITIOUS))
		return (rtval);

	vm_page_spin_lock(m);
	TAILQ_FOREACH(pv, &m->md.pv_list, pv_list) {
		if (!pmap_track_modified(pv->pv_pindex))
			continue;
		pte = pmap_pte_quick(pv->pv_pmap, pv->pv_pindex << PAGE_SHIFT);
		if (pte && (*pte & PG_A)) {
			atomic_clear_long(pte, PG_A);
			rtval++;
			if (rtval > 4)
				break;
		}
	}
	vm_page_spin_unlock(m);
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
	boolean_t res;

	res = pmap_testbit(m, PG_M);
	return (res);
}

/*
 *	Clear the modify bits on the specified physical page.
 */
void
pmap_clear_modify(vm_page_t m)
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
{
	pmap_clearbit(m, PG_A);
}

/*
 * Miscellaneous support routines follow
 */

static
void
i386_protection_init(void)
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
 * NOTE: We can't use pgeflag unless we invalidate the pages one at
 *	 a time.
 *
 * NOTE: The PAT attributes {WRITE_BACK, WRITE_THROUGH, UNCACHED, UNCACHEABLE}
 *	 work whether the cpu supports PAT or not.  The remaining PAT
 *	 attributes {WRITE_PROTECTED, WRITE_COMBINING} only work if the cpu
 *	 supports PAT.
 */
void *
pmap_mapdev(vm_paddr_t pa, vm_size_t size)
{
	return(pmap_mapdev_attr(pa, size, PAT_WRITE_BACK));
}

void *
pmap_mapdev_uncacheable(vm_paddr_t pa, vm_size_t size)
{
	return(pmap_mapdev_attr(pa, size, PAT_UNCACHEABLE));
}

/*
 * Map a set of physical memory pages into the kernel virtual
 * address space. Return a pointer to where it is mapped. This
 * routine is intended to be used for mapping device memory,
 * NOT real memory.
 */
void *
pmap_mapdev_attr(vm_paddr_t pa, vm_size_t size, int mode)
{
	vm_offset_t va, tmpva, offset;
	pt_entry_t *pte;
	vm_size_t tmpsize;

	offset = pa & PAGE_MASK;
	size = roundup(offset + size, PAGE_SIZE);

	va = kmem_alloc_nofault(&kernel_map, size, PAGE_SIZE);
	if (va == 0)
		panic("pmap_mapdev: Couldn't alloc kernel virtual memory");

	pa = pa & ~PAGE_MASK;
	for (tmpva = va, tmpsize = size; tmpsize > 0;) {
		pte = vtopte(tmpva);
		*pte = pa | PG_RW | PG_V | /* pgeflag | */
		       pat_pte_index[mode];
		tmpsize -= PAGE_SIZE;
		tmpva += PAGE_SIZE;
		pa += PAGE_SIZE;
	}
	pmap_invalidate_range(&kernel_pmap, va, va + size);
	pmap_invalidate_cache_range(va, va + size);

	return ((void *)(va + offset));
}

void
pmap_unmapdev(vm_offset_t va, vm_size_t size)
{
	vm_offset_t base, offset;

	base = va & ~PAGE_MASK;
	offset = va & PAGE_MASK;
	size = roundup(offset + size, PAGE_SIZE);
	pmap_qremove(va, size >> PAGE_SHIFT);
	kmem_free(&kernel_map, base, size);
}

/*
 * Change the PAT attribute on an existing kernel memory map.  Caller
 * must ensure that the virtual memory in question is not accessed
 * during the adjustment.
 */
void
pmap_change_attr(vm_offset_t va, vm_size_t count, int mode)
{
	pt_entry_t *pte;
	vm_offset_t base;
	int changed = 0;

	if (va == 0)
		panic("pmap_change_attr: va is NULL");
	base = trunc_page(va);

	while (count) {
		pte = vtopte(va);
		*pte = (*pte & ~(pt_entry_t)(PG_PTE_PAT | PG_NC_PCD |
					     PG_NC_PWT)) |
		       pat_pte_index[mode];
		--count;
		va += PAGE_SIZE;
	}

	changed = 1;	/* XXX: not optimal */

	/*
	 * Flush CPU caches if required to make sure any data isn't cached that
	 * shouldn't be, etc.
	 */
	if (changed) {
		pmap_invalidate_range(&kernel_pmap, base, va);
		pmap_invalidate_cache_range(base, va);
	}
}

/*
 * perform the pmap work for mincore
 */
int
pmap_mincore(pmap_t pmap, vm_offset_t addr)
{
	pt_entry_t *ptep, pte;
	vm_page_t m;
	int val = 0;
	
	lwkt_gettoken(&pmap->pm_token);
	ptep = pmap_pte(pmap, addr);

	if (ptep && (pte = *ptep) != 0) {
		vm_offset_t pa;

		val = MINCORE_INCORE;
		if ((pte & PG_MANAGED) == 0)
			goto done;

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
done:
	lwkt_reltoken(&pmap->pm_token);

	return val;
}

/*
 * Replace p->p_vmspace with a new one.  If adjrefs is non-zero the new
 * vmspace will be ref'd and the old one will be deref'd.
 *
 * The vmspace for all lwps associated with the process will be adjusted
 * and cr3 will be reloaded if any lwp is the current lwp.
 *
 * The process must hold the vmspace->vm_map.token for oldvm and newvm
 */
void
pmap_replacevm(struct proc *p, struct vmspace *newvm, int adjrefs)
{
	struct vmspace *oldvm;
	struct lwp *lp;

	oldvm = p->p_vmspace;
	if (oldvm != newvm) {
		if (adjrefs)
			sysref_get(&newvm->vm_sysref);
		p->p_vmspace = newvm;
		KKASSERT(p->p_nthreads == 1);
		lp = RB_ROOT(&p->p_lwp_tree);
		pmap_setlwpvm(lp, newvm);
		if (adjrefs)
			sysref_put(&oldvm->vm_sysref);
	}
}

/*
 * Set the vmspace for a LWP.  The vmspace is almost universally set the
 * same as the process vmspace, but virtual kernels need to swap out contexts
 * on a per-lwp basis.
 *
 * Caller does not necessarily hold any vmspace tokens.  Caller must control
 * the lwp (typically be in the context of the lwp).  We use a critical
 * section to protect against statclock and hardclock (statistics collection).
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
			atomic_set_cpumask(&pmap->pm_active, mycpu->gd_cpumask);
			if (pmap->pm_active & CPUMASK_LOCK)
				pmap_interlock_wait(newvm);
#if defined(SWTCH_OPTIM_STATS)
			tlb_flush_count++;
#endif
			curthread->td_pcb->pcb_cr3 = vtophys(pmap->pm_pml4);
			load_cr3(curthread->td_pcb->pcb_cr3);
			pmap = vmspace_pmap(oldvm);
			atomic_clear_cpumask(&pmap->pm_active, mycpu->gd_cpumask);
		}
		crit_exit();
	}
}

/*
 * Called when switching to a locked pmap, used to interlock against pmaps
 * undergoing modifications to prevent us from activating the MMU for the
 * target pmap until all such modifications have completed.  We have to do
 * this because the thread making the modifications has already set up its
 * SMP synchronization mask.
 *
 * This function cannot sleep!
 *
 * No requirements.
 */
void
pmap_interlock_wait(struct vmspace *vm)
{
	struct pmap *pmap = &vm->vm_pmap;

	if (pmap->pm_active & CPUMASK_LOCK) {
		crit_enter();
		KKASSERT(curthread->td_critcount >= 2);
		DEBUG_PUSH_INFO("pmap_interlock_wait");
		while (pmap->pm_active & CPUMASK_LOCK) {
			cpu_ccfence();
			lwkt_process_ipiq();
		}
		DEBUG_POP_INFO();
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
	return(PHYS_TO_VM_PAGE(*vtopte(va) & PG_FRAME));
}

/*
 * Initialize machine-specific shared page directory support.  This
 * is executed when a VM object is created.
 */
void
pmap_object_init(vm_object_t object)
{
	object->md.pmap_rw = NULL;
	object->md.pmap_ro = NULL;
}

/*
 * Clean up machine-specific shared page directory support.  This
 * is executed when a VM object is destroyed.
 */
void
pmap_object_free(vm_object_t object)
{
	pmap_t pmap;

	if ((pmap = object->md.pmap_rw) != NULL) {
		object->md.pmap_rw = NULL;
		pmap_remove_noinval(pmap,
				  VM_MIN_USER_ADDRESS, VM_MAX_USER_ADDRESS);
		pmap->pm_active = 0;
		pmap_release(pmap);
		pmap_puninit(pmap);
		kfree(pmap, M_OBJPMAP);
	}
	if ((pmap = object->md.pmap_ro) != NULL) {
		object->md.pmap_ro = NULL;
		pmap_remove_noinval(pmap,
				  VM_MIN_USER_ADDRESS, VM_MAX_USER_ADDRESS);
		pmap->pm_active = 0;
		pmap_release(pmap);
		pmap_puninit(pmap);
		kfree(pmap, M_OBJPMAP);
	}
}
