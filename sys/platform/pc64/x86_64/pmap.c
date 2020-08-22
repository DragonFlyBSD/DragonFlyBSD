/*
 * Copyright (c) 1991 Regents of the University of California.
 * Copyright (c) 1994 John S. Dyson
 * Copyright (c) 1994 David Greenman
 * Copyright (c) 2003 Peter Wemm
 * Copyright (c) 2005-2008 Alan L. Cox <alc@cs.rice.edu>
 * Copyright (c) 2008, 2009 The DragonFly Project.
 * Copyright (c) 2008, 2009 Jordan Gordeev.
 * Copyright (c) 2011-2019 Matthew Dillon
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
 *
 * Some notes:
 *	- The 'M'odified bit is only applicable to terminal PTEs.
 *
 *	- The 'U'ser access bit can be set for higher-level PTEs as
 *	  long as it isn't set for terminal PTEs for pages we don't
 *	  want user access to.
 */

#if 0 /* JG */
#include "opt_pmap.h"
#endif
#include "opt_msgbuf.h"

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/msgbuf.h>
#include <sys/vmmeter.h>
#include <sys/mman.h>
#include <sys/systm.h>

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

#include <sys/thread2.h>
#include <sys/spinlock2.h>
#include <vm/vm_page2.h>

#include <machine/cputypes.h>
#include <machine/cpu.h>
#include <machine/md_var.h>
#include <machine/specialreg.h>
#include <machine/smp.h>
#include <machine_base/apic/apicreg.h>
#include <machine/globaldata.h>
#include <machine/pmap.h>
#include <machine/pmap_inval.h>

#include <ddb/ddb.h>

#define PMAP_KEEP_PDIRS

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

#define pv_get(pmap, pindex, pmarkp)	_pv_get(pmap, pindex, pmarkp	\
							PMAP_DEBUG_ARGS)
#define pv_lock(pv)			_pv_lock(pv			\
							PMAP_DEBUG_ARGS)
#define pv_hold_try(pv)			_pv_hold_try(pv			\
							PMAP_DEBUG_ARGS)
#define pv_alloc(pmap, pindex, isnewp)	_pv_alloc(pmap, pindex, isnewp	\
							PMAP_DEBUG_ARGS)

#define pv_free(pv, pvp)		_pv_free(pv, pvp PMAP_DEBUG_ARGS)

#else

#define PMAP_DEBUG_DECL
#define PMAP_DEBUG_ARGS
#define PMAP_DEBUG_COPY

#define pv_get(pmap, pindex, pmarkp)	_pv_get(pmap, pindex, pmarkp)
#define pv_lock(pv)			_pv_lock(pv)
#define pv_hold_try(pv)			_pv_hold_try(pv)
#define pv_alloc(pmap, pindex, isnewp)	_pv_alloc(pmap, pindex, isnewp)
#define pv_free(pv, pvp)		_pv_free(pv, pvp)

#endif

/*
 * Get PDEs and PTEs for user/kernel address space
 */
#define pdir_pde(m, v) (m[(vm_offset_t)(v) >> PDRSHIFT])

#define pmap_pde_v(pmap, pte)	\
		((*(pd_entry_t *)pte & pmap->pmap_bits[PG_V_IDX]) != 0)
#define pmap_pte_w(pmap, pte)	\
		((*(pt_entry_t *)pte & pmap->pmap_bits[PG_W_IDX]) != 0)
#define pmap_pte_m(pmap, pte)	\
		((*(pt_entry_t *)pte & pmap->pmap_bits[PG_M_IDX]) != 0)
#define pmap_pte_u(pmap, pte)	\
		((*(pt_entry_t *)pte & pmap->pmap_bits[PG_U_IDX]) != 0)
#define pmap_pte_v(pmap, pte)	\
		((*(pt_entry_t *)pte & pmap->pmap_bits[PG_V_IDX]) != 0)

/*
 * Given a map and a machine independent protection code,
 * convert to a vax protection code.
 */
#define pte_prot(m, p)		\
	(m->protection_codes[p & (VM_PROT_READ|VM_PROT_WRITE|VM_PROT_EXECUTE)])
static uint64_t protection_codes[PROTECTION_CODES_SIZE];

/*
 * Backing scan macros.  Note that in the use case 'ipte' is only a tentitive
 * value and must be validated by a pmap_inval_smp_cmpset*() or equivalent
 * function.
 *
 * NOTE: cpu_ccfence() is required to prevent excessive optmization of
 *	 of the (ipte) variable.
 *
 * NOTE: We don't bother locking the backing object if it isn't mapped
 *	 to anything (backing_list is empty).
 *
 * NOTE: For now guarantee an interlock via iobj->backing_lk if the
 *	 object exists and do not shortcut the lock by checking to see
 *	 if the list is empty first.
 */
#define PMAP_PAGE_BACKING_SCAN(m, match_pmap, ipmap, iptep, ipte, iva)	\
	if (m->object) {						\
		vm_object_t iobj = m->object;				\
		vm_map_backing_t iba, next_ba;				\
		struct pmap *ipmap;					\
		pt_entry_t ipte;					\
		pt_entry_t *iptep;					\
		vm_offset_t iva;					\
		vm_pindex_t ipindex_start;				\
		vm_pindex_t ipindex_end;				\
									\
		lockmgr(&iobj->backing_lk, LK_SHARED);			\
		next_ba = TAILQ_FIRST(&iobj->backing_list);		\
		while ((iba = next_ba) != NULL) {			\
			next_ba = TAILQ_NEXT(iba, entry);		\
			ipmap = iba->pmap;				\
			if (match_pmap && ipmap != match_pmap)		\
				continue;				\
			ipindex_start = iba->offset >> PAGE_SHIFT;	\
			ipindex_end = ipindex_start +			\
				  ((iba->end - iba->start) >> PAGE_SHIFT); \
			if (m->pindex < ipindex_start ||		\
			    m->pindex >= ipindex_end) {			\
				continue;				\
			}						\
			iva = iba->start +				\
			      ((m->pindex - ipindex_start) << PAGE_SHIFT); \
			iptep = pmap_pte(ipmap, iva);			\
			if (iptep == NULL)				\
				continue;				\
			ipte = *iptep;					\
			cpu_ccfence();					\
			if (m->phys_addr != (ipte & PG_FRAME))		\
				continue;				\

#define PMAP_PAGE_BACKING_RETRY						\
			{						\
				next_ba = iba;				\
				continue;				\
			}						\

#define PMAP_PAGE_BACKING_DONE						\
		}							\
		lockmgr(&iobj->backing_lk, LK_RELEASE);			\
	}								\

struct pmap kernel_pmap;
struct pmap iso_pmap;

vm_paddr_t avail_start;		/* PA of first available physical page */
vm_paddr_t avail_end;		/* PA of last available physical page */
vm_offset_t virtual2_start;	/* cutout free area prior to kernel start */
vm_offset_t virtual2_end;
vm_offset_t virtual_start;	/* VA of first avail page (after kernel bss) */
vm_offset_t virtual_end;	/* VA of last avail page (end of kernel AS) */
vm_offset_t KvaStart;		/* VA start of KVA space */
vm_offset_t KvaEnd;		/* VA end of KVA space (non-inclusive) */
vm_offset_t KvaSize;		/* max size of kernel virtual address space */
vm_offset_t DMapMaxAddress;
/* Has pmap_init completed? */
__read_frequently static boolean_t pmap_initialized = FALSE;
//static int pgeflag;		/* PG_G or-in */
uint64_t PatMsr;

static int ndmpdp;
static vm_paddr_t dmaplimit;
vm_offset_t kernel_vm_end = VM_MIN_KERNEL_ADDRESS;

static pt_entry_t pat_pte_index[PAT_INDEX_SIZE];	/* PAT -> PG_ bits */
static pt_entry_t pat_pde_index[PAT_INDEX_SIZE];	/* PAT -> PG_ bits */

static uint64_t KPTbase;
static uint64_t KPTphys;
static uint64_t	KPDphys;	/* phys addr of kernel level 2 */
static uint64_t	KPDbase;	/* phys addr of kernel level 2 @ KERNBASE */
uint64_t KPDPphys;		/* phys addr of kernel level 3 */
uint64_t KPML4phys;		/* phys addr of kernel level 4 */

static uint64_t	DMPDphys;	/* phys addr of direct mapped level 2 */
static uint64_t	DMPDPphys;	/* phys addr of direct mapped level 3 */

/*
 * Data for the pv entry allocation mechanism
 */
__read_mostly static vm_zone_t pvzone;
__read_mostly static int pmap_pagedaemon_waken = 0;
static struct vm_zone pvzone_store;
static struct pv_entry *pvinit;

/*
 * All those kernel PT submaps that BSD is so fond of
 */
pt_entry_t *CMAP1 = NULL, *ptmmap;
caddr_t CADDR1 = NULL, ptvmmap = NULL;
static pt_entry_t *msgbufmap;
struct msgbuf *msgbufp=NULL;

/*
 * PMAP default PG_* bits. Needed to be able to add
 * EPT/NPT pagetable pmap_bits for the VMM module
 */
__read_frequently uint64_t pmap_bits_default[] = {
		REGULAR_PMAP,			/* TYPE_IDX		0 */
		X86_PG_V,			/* PG_V_IDX		1 */
		X86_PG_RW,			/* PG_RW_IDX		2 */
		X86_PG_U,			/* PG_U_IDX		3 */
		X86_PG_A,			/* PG_A_IDX		4 */
		X86_PG_M,			/* PG_M_IDX		5 */
		X86_PG_PS,			/* PG_PS_IDX3		6 */
		X86_PG_G,			/* PG_G_IDX		7 */
		X86_PG_AVAIL1,			/* PG_AVAIL1_IDX	8 */
		X86_PG_AVAIL2,			/* PG_AVAIL2_IDX	9 */
		X86_PG_AVAIL3,			/* PG_AVAIL3_IDX	10 */
		X86_PG_NC_PWT | X86_PG_NC_PCD,	/* PG_N_IDX		11 */
		X86_PG_NX,			/* PG_NX_IDX		12 */
};

/*
 * Crashdump maps.
 */
static pt_entry_t *pt_crashdumpmap;
static caddr_t crashdumpmap;

static int pmap_debug = 0;
SYSCTL_INT(_machdep, OID_AUTO, pmap_debug, CTLFLAG_RW,
    &pmap_debug, 0, "Debug pmap's");
#ifdef PMAP_DEBUG2
static int pmap_enter_debug = 0;
SYSCTL_INT(_machdep, OID_AUTO, pmap_enter_debug, CTLFLAG_RW,
    &pmap_enter_debug, 0, "Debug pmap_enter's");
#endif
static int pmap_yield_count = 64;
SYSCTL_INT(_machdep, OID_AUTO, pmap_yield_count, CTLFLAG_RW,
    &pmap_yield_count, 0, "Yield during init_pt/release");
int pmap_fast_kernel_cpusync = 0;
SYSCTL_INT(_machdep, OID_AUTO, pmap_fast_kernel_cpusync, CTLFLAG_RW,
    &pmap_fast_kernel_cpusync, 0, "Share page table pages when possible");
int pmap_dynamic_delete = 0;
SYSCTL_INT(_machdep, OID_AUTO, pmap_dynamic_delete, CTLFLAG_RW,
    &pmap_dynamic_delete, 0, "Dynamically delete PT/PD/PDPs");
int pmap_lock_delay = 100;
SYSCTL_INT(_machdep, OID_AUTO, pmap_lock_delay, CTLFLAG_RW,
    &pmap_lock_delay, 0, "Spin loops");
static int meltdown_mitigation = -1;
TUNABLE_INT("machdep.meltdown_mitigation", &meltdown_mitigation);
SYSCTL_INT(_machdep, OID_AUTO, meltdown_mitigation, CTLFLAG_RW,
    &meltdown_mitigation, 0, "Userland pmap isolation");

static int pmap_nx_enable = -1;		/* -1 = auto */
/* needs manual TUNABLE in early probe, see below */
SYSCTL_INT(_machdep, OID_AUTO, pmap_nx_enable, CTLFLAG_RD,
    &pmap_nx_enable, 0,
    "no-execute support (0=disabled, 1=w/READ, 2=w/READ & WRITE)");

static int pmap_pv_debug = 50;
SYSCTL_INT(_machdep, OID_AUTO, pmap_pv_debug, CTLFLAG_RW,
    &pmap_pv_debug, 0, "");

static long vm_pmap_pv_entries;
SYSCTL_LONG(_vm, OID_AUTO, pmap_pv_entries, CTLFLAG_RD,
    &vm_pmap_pv_entries, 0, "");

/* Standard user access funtions */
extern int std_copyinstr (const void *udaddr, void *kaddr, size_t len,
    size_t *lencopied);
extern int std_copyin (const void *udaddr, void *kaddr, size_t len);
extern int std_copyout (const void *kaddr, void *udaddr, size_t len);
extern int std_fubyte (const uint8_t *base);
extern int std_subyte (uint8_t *base, uint8_t byte);
extern int32_t std_fuword32 (const uint32_t *base);
extern int64_t std_fuword64 (const uint64_t *base);
extern int std_suword64 (uint64_t *base, uint64_t word);
extern int std_suword32 (uint32_t *base, int word);
extern uint32_t std_swapu32 (volatile uint32_t *base, uint32_t v);
extern uint64_t std_swapu64 (volatile uint64_t *base, uint64_t v);
extern uint32_t std_fuwordadd32 (volatile uint32_t *base, uint32_t v);
extern uint64_t std_fuwordadd64 (volatile uint64_t *base, uint64_t v);

#if 0
static void pv_hold(pv_entry_t pv);
#endif
static int _pv_hold_try(pv_entry_t pv
				PMAP_DEBUG_DECL);
static void pv_drop(pv_entry_t pv);
static void _pv_lock(pv_entry_t pv
				PMAP_DEBUG_DECL);
static void pv_unlock(pv_entry_t pv);
static pv_entry_t _pv_alloc(pmap_t pmap, vm_pindex_t pindex, int *isnew
				PMAP_DEBUG_DECL);
static pv_entry_t _pv_get(pmap_t pmap, vm_pindex_t pindex, vm_pindex_t **pmarkp
				PMAP_DEBUG_DECL);
static void _pv_free(pv_entry_t pv, pv_entry_t pvp PMAP_DEBUG_DECL);
static pv_entry_t pv_get_try(pmap_t pmap, vm_pindex_t pindex,
				vm_pindex_t **pmarkp, int *errorp);
static void pv_put(pv_entry_t pv);
static void *pv_pte_lookup(pv_entry_t pv, vm_pindex_t pindex);
static pv_entry_t pmap_allocpte(pmap_t pmap, vm_pindex_t ptepindex,
		      pv_entry_t *pvpp);
static void pmap_remove_pv_pte(pv_entry_t pv, pv_entry_t pvp,
			pmap_inval_bulk_t *bulk, int destroy);
static vm_page_t pmap_remove_pv_page(pv_entry_t pv, int clrpgbits);
static int pmap_release_pv(pv_entry_t pv, pv_entry_t pvp,
			pmap_inval_bulk_t *bulk);

struct pmap_scan_info;
static void pmap_remove_callback(pmap_t pmap, struct pmap_scan_info *info,
		      vm_pindex_t *pte_placemark, pv_entry_t pt_pv,
		      vm_offset_t va, pt_entry_t *ptep, void *arg __unused);
static void pmap_protect_callback(pmap_t pmap, struct pmap_scan_info *info,
		      vm_pindex_t *pte_placemark, pv_entry_t pt_pv,
		      vm_offset_t va, pt_entry_t *ptep, void *arg __unused);

static void x86_64_protection_init (void);
static void create_pagetables(vm_paddr_t *firstaddr);
static void pmap_remove_all (vm_page_t m);
static boolean_t pmap_testbit (vm_page_t m, int bit);

static pt_entry_t *pmap_pte_quick (pmap_t pmap, vm_offset_t va);
static vm_offset_t pmap_kmem_choose(vm_offset_t addr);

static void pmap_pinit_defaults(struct pmap *pmap);
static void pv_placemarker_wait(pmap_t pmap, vm_pindex_t *pmark);
static void pv_placemarker_wakeup(pmap_t pmap, vm_pindex_t *pmark);

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
 * We have removed a managed pte.  The page might not be hard or soft-busied
 * at this point so we have to be careful.
 *
 * If advanced mode is enabled we can clear PG_MAPPED/WRITEABLE only if
 * MAPPEDMULTI is not set.  This must be done atomically against possible
 * concurrent pmap_enter()s occurring at the same time.  If MULTI is set
 * then the kernel may have to call vm_page_protect() later on to clean
 * the bits up.  This is particularly important for kernel_map/kernel_object
 * mappings due to the expense of scanning the kernel_object's vm_backing's.
 *
 * If advanced mode is not enabled we update our tracking counts and
 * synchronize PG_MAPPED/WRITEABLE later on in pmap_mapped_sync().
 */
static __inline
void
pmap_removed_pte(vm_page_t m, pt_entry_t pte)
{
	int flags;
	int nflags;

	flags = m->flags;
	cpu_ccfence();
	while ((flags & PG_MAPPEDMULTI) == 0) {
		nflags = flags & ~(PG_MAPPED | PG_WRITEABLE);
		if (atomic_fcmpset_int(&m->flags, &flags, nflags))
			break;
	}
}

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

	newaddr = roundup2(addr, NBPDR);
	return newaddr;
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
 * Of all the layers (PTE, PT, PD, PDP, PML4) the best one to cache is
 * the PT layer.  This will speed up core pmap operations considerably.
 * We also cache the PTE layer to (hopefully) improve relative lookup
 * speeds.
 *
 * NOTE: The pmap spinlock does not need to be held but the passed-in pv
 *	 must be in a known associated state (typically by being locked when
 *	 the pmap spinlock isn't held).  We allow the race for that case.
 *
 * NOTE: pm_pvhint* is only accessed (read) with the spin-lock held, using
 *	 cpu_ccfence() to prevent compiler optimizations from reloading the
 *	 field.
 */
static __inline
void
pv_cache(pmap_t pmap, pv_entry_t pv, vm_pindex_t pindex)
{
	if (pindex < pmap_pt_pindex(0)) {
		;
	} else if (pindex < pmap_pd_pindex(0)) {
		pmap->pm_pvhint_pt = pv;
	}
}

/*
 * Locate the requested pt_entry
 */
static __inline
pv_entry_t
pv_entry_lookup(pmap_t pmap, vm_pindex_t pindex)
{
	pv_entry_t pv;

	if (pindex < pmap_pt_pindex(0))
		return NULL;
#if 1
	if (pindex < pmap_pd_pindex(0))
		pv = pmap->pm_pvhint_pt;
	else
		pv = NULL;
	cpu_ccfence();
	if (pv == NULL || pv->pv_pmap != pmap) {
		pv = pv_entry_rb_tree_RB_LOOKUP(&pmap->pm_pvroot, pindex);
		if (pv)
			pv_cache(pmap, pv, pindex);
	} else if (pv->pv_pindex != pindex) {
		pv = pv_entry_rb_tree_RB_LOOKUP_REL(&pmap->pm_pvroot,
						    pindex, pv);
		if (pv)
			pv_cache(pmap, pv, pindex);
	}
#else
	pv = pv_entry_rb_tree_RB_LOOKUP(&pmap->pm_pvroot, pindex);
#endif
	return pv;
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
 * The placemarker hash must be broken up into four zones so lock
 * ordering semantics continue to work (e.g. pte, pt, pd, then pdp).
 *
 * Placemarkers are used to 'lock' page table indices that do not have
 * a pv_entry.  This allows the pmap to support managed and unmanaged
 * pages and shared page tables.
 */
#define PM_PLACE_BASE	(PM_PLACEMARKS >> 2)

static __inline
vm_pindex_t *
pmap_placemarker_hash(pmap_t pmap, vm_pindex_t pindex)
{
	int hi;

	if (pindex < pmap_pt_pindex(0))		/* zone 0 - PTE */
		hi = 0;
	else if (pindex < pmap_pd_pindex(0))	/* zone 1 - PT */
		hi = PM_PLACE_BASE;
	else if (pindex < pmap_pdp_pindex(0))	/* zone 2 - PD */
		hi = PM_PLACE_BASE << 1;
	else					/* zone 3 - PDP (and PML4E) */
		hi = PM_PLACE_BASE | (PM_PLACE_BASE << 1);
	hi += pindex & (PM_PLACE_BASE - 1);

	return (&pmap->pm_placemarks[hi]);
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
	if ((*pdp & pmap->pmap_bits[PG_V_IDX]) == 0)
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
	vm_paddr_t phys;

	if (pmap->pm_flags & PMAP_FLAG_SIMPLE) {
		pd_pindex = pmap_pd_pindex(va);
		spin_lock_shared(&pmap->pm_spin);
		pv = pv_entry_rb_tree_RB_LOOKUP(&pmap->pm_pvroot, pd_pindex);
		if (pv == NULL || pv->pv_m == NULL) {
			spin_unlock_shared(&pmap->pm_spin);
			return NULL;
		}
		phys = VM_PAGE_TO_PHYS(pv->pv_m);
		spin_unlock_shared(&pmap->pm_spin);
		return (pmap_pd_to_pt(phys, va));
	} else {
		pd = pmap_pd(pmap, va);
		if (pd == NULL || (*pd & pmap->pmap_bits[PG_V_IDX]) == 0)
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
	if (pt == NULL || (*pt & pmap->pmap_bits[PG_V_IDX]) == 0)
		 return NULL;
	if ((*pt & pmap->pmap_bits[PG_PS_IDX]) != 0)
		return ((pt_entry_t *)pt);
	return (pmap_pt_to_pte(*pt, va));
}

/*
 * Return address of PT slot in PD (KVM only)
 *
 * Cannot be used for user page tables because it might interfere with
 * the shared page-table-page optimization (pmap_mmu_optimize).
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

/*
 * Returns the physical address translation from va for a user address.
 * (vm_paddr_t)-1 is returned on failure.
 */
vm_paddr_t
uservtophys(vm_offset_t va)
{
	uint64_t mask = ((1ul << (NPTEPGSHIFT + NPDEPGSHIFT +
				  NPDPEPGSHIFT + NPML4EPGSHIFT)) - 1);
	vm_paddr_t pa;
	pt_entry_t pte;
	pmap_t pmap;

	pmap = vmspace_pmap(mycpu->gd_curthread->td_lwp->lwp_vmspace);
	pa = (vm_paddr_t)-1;
	if (va < VM_MAX_USER_ADDRESS) {
		pte = kreadmem64(PTmap + ((va >> PAGE_SHIFT) & mask));
		if (pte & pmap->pmap_bits[PG_V_IDX])
			pa = (pte & PG_FRAME) | (va & PAGE_MASK);
	}
	return pa;
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
	long nkpd_phys;
	int j;

	/*
	 * We are running (mostly) V=P at this point
	 *
	 * Calculate how many 1GB PD entries in our PDP pages are needed
	 * for the DMAP.  This is only allocated if the system does not
	 * support 1GB pages.  Otherwise ndmpdp is simply a count of
	 * the number of 1G terminal entries in our PDP pages are needed.
	 *
	 * NOTE: Maxmem is in pages
	 */
	ndmpdp = (ptoa(Maxmem) + NBPDP - 1) >> PDPSHIFT;
	if (ndmpdp < 4)		/* Minimum 4GB of DMAP */
		ndmpdp = 4;

#if 0
	/*
	 * HACK XXX fix me - Some laptops map the EFI framebuffer in
	 * very high physical addresses and the DMAP winds up being too
	 * small.  The EFI framebuffer has to be mapped for the console
	 * very early and the DMAP is how it does it.
	 */
	if (ndmpdp < 512)	/* Minimum 512GB of DMAP */
		ndmpdp = 512;
#endif

	KKASSERT(ndmpdp <= NDMPML4E * NPML4EPG);
	DMapMaxAddress = DMAP_MIN_ADDRESS +
			 ((ndmpdp * NPDEPG) << PDRSHIFT);

	/*
	 * Starting at KERNBASE - map all 2G worth of page table pages.
	 * KERNBASE is offset -2G from the end of kvm.  This will accomodate
	 * all KVM allocations above KERNBASE, including the SYSMAPs below.
	 *
	 * We do this by allocating 2*512 PT pages.  Each PT page can map
	 * 2MB, for 2GB total.
	 */
	nkpt_base = (NPDPEPG - KPDPI) * NPTEPG;	/* typically 2 x 512 */

	/*
	 * Starting at the beginning of kvm (VM_MIN_KERNEL_ADDRESS),
	 * Calculate how many page table pages we need to preallocate
	 * for early vm_map allocations.
	 *
	 * A few extra won't hurt, they will get used up in the running
	 * system.
	 *
	 * vm_page array
	 * initial pventry's
	 */
	nkpt_phys = (Maxmem * sizeof(struct vm_page) + NBPDR - 1) / NBPDR;
	nkpt_phys += (Maxmem * sizeof(struct pv_entry) + NBPDR - 1) / NBPDR;
	nkpt_phys += 128;	/* a few extra */

	/*
	 * The highest value nkpd_phys can be set to is
	 * NKPDPE - (NPDPEPG - KPDPI) (i.e. NKPDPE - 2).
	 *
	 * Doing so would cause all PD pages to be pre-populated for
	 * a maximal KVM space (approximately 16*512 pages, or 32MB.
	 * We can save memory by not doing this.
	 */
	nkpd_phys = (nkpt_phys + NPDPEPG - 1) / NPDPEPG;

	/*
	 * Allocate pages
	 *
	 * Normally NKPML4E=1-16 (1-16 kernel PDP page)
	 * Normally NKPDPE= NKPML4E*512-1 (511 min kernel PD pages)
	 *
	 * Only allocate enough PD pages
	 * NOTE: We allocate all kernel PD pages up-front, typically
	 *	 ~511G of KVM, requiring 511 PD pages.
	 */
	KPTbase = allocpages(firstaddr, nkpt_base);	/* KERNBASE to end */
	KPTphys = allocpages(firstaddr, nkpt_phys);	/* KVA start */
	KPML4phys = allocpages(firstaddr, 1);		/* recursive PML4 map */
	KPDPphys = allocpages(firstaddr, NKPML4E);	/* kernel PDP pages */
	KPDphys = allocpages(firstaddr, nkpd_phys);	/* kernel PD pages */

	/*
	 * Alloc PD pages for the area starting at KERNBASE.
	 */
	KPDbase = allocpages(firstaddr, NPDPEPG - KPDPI);

	/*
	 * Stuff for our DMAP.  Use 2MB pages even when 1GB pages
	 * are available in order to allow APU code to adjust page
	 * attributes on a fixed grain (see pmap_change_attr()).
	 */
	DMPDPphys = allocpages(firstaddr, NDMPML4E);
#if 1
	DMPDphys = allocpages(firstaddr, ndmpdp);
#else
	if ((amd_feature & AMDID_PAGE1GB) == 0)
		DMPDphys = allocpages(firstaddr, ndmpdp);
#endif
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
		((pt_entry_t *)KPTbase)[i] |=
		    pmap_bits_default[PG_RW_IDX] |
		    pmap_bits_default[PG_V_IDX] |
		    pmap_bits_default[PG_G_IDX];
	}

	/*
	 * Now map the initial kernel page tables.  One block of page
	 * tables is placed at the beginning of kernel virtual memory,
	 * and another block is placed at KERNBASE to map the kernel binary,
	 * data, bss, and initial pre-allocations.
	 */
	for (i = 0; i < nkpt_base; i++) {
		((pd_entry_t *)KPDbase)[i] = KPTbase + (i << PAGE_SHIFT);
		((pd_entry_t *)KPDbase)[i] |=
		    pmap_bits_default[PG_RW_IDX] |
		    pmap_bits_default[PG_V_IDX];
	}
	for (i = 0; i < nkpt_phys; i++) {
		((pd_entry_t *)KPDphys)[i] = KPTphys + (i << PAGE_SHIFT);
		((pd_entry_t *)KPDphys)[i] |=
		    pmap_bits_default[PG_RW_IDX] |
		    pmap_bits_default[PG_V_IDX];
	}

	/*
	 * Map from zero to end of allocations using 2M pages as an
	 * optimization.  This will bypass some of the KPTBase pages
	 * above in the KERNBASE area.
	 */
	for (i = 0; (i << PDRSHIFT) < *firstaddr; i++) {
		((pd_entry_t *)KPDbase)[i] = i << PDRSHIFT;
		((pd_entry_t *)KPDbase)[i] |=
		    pmap_bits_default[PG_RW_IDX] |
		    pmap_bits_default[PG_V_IDX] |
		    pmap_bits_default[PG_PS_IDX] |
		    pmap_bits_default[PG_G_IDX];
	}

	/*
	 * Load PD addresses into the PDP pages for primary KVA space to
	 * cover existing page tables.  PD's for KERNBASE are handled in
	 * the next loop.
	 *
	 * expected to pre-populate all of its PDs.  See NKPDPE in vmparam.h.
	 */
	for (i = 0; i < nkpd_phys; i++) {
		((pdp_entry_t *)KPDPphys)[NKPML4E * NPDPEPG - NKPDPE + i] =
				KPDphys + (i << PAGE_SHIFT);
		((pdp_entry_t *)KPDPphys)[NKPML4E * NPDPEPG - NKPDPE + i] |=
		    pmap_bits_default[PG_RW_IDX] |
		    pmap_bits_default[PG_V_IDX] |
		    pmap_bits_default[PG_A_IDX];
	}

	/*
	 * Load PDs for KERNBASE to the end
	 */
	i = (NKPML4E - 1) * NPDPEPG + KPDPI;
	for (j = 0; j < NPDPEPG - KPDPI; ++j) {
		((pdp_entry_t *)KPDPphys)[i + j] =
				KPDbase + (j << PAGE_SHIFT);
		((pdp_entry_t *)KPDPphys)[i + j] |=
		    pmap_bits_default[PG_RW_IDX] |
		    pmap_bits_default[PG_V_IDX] |
		    pmap_bits_default[PG_A_IDX];
	}

	/*
	 * Now set up the direct map space using either 2MB or 1GB pages
	 * Preset PG_M and PG_A because demotion expects it.
	 *
	 * When filling in entries in the PD pages make sure any excess
	 * entries are set to zero as we allocated enough PD pages
	 *
	 * Stuff for our DMAP.  Use 2MB pages even when 1GB pages
	 * are available in order to allow APU code to adjust page
	 * attributes on a fixed grain (see pmap_change_attr()).
	 */
#if 0
	if ((amd_feature & AMDID_PAGE1GB) == 0)
#endif
	{
		/*
		 * Use 2MB pages
		 */
		for (i = 0; i < NPDEPG * ndmpdp; i++) {
			((pd_entry_t *)DMPDphys)[i] = i << PDRSHIFT;
			((pd_entry_t *)DMPDphys)[i] |=
			    pmap_bits_default[PG_RW_IDX] |
			    pmap_bits_default[PG_V_IDX] |
			    pmap_bits_default[PG_PS_IDX] |
			    pmap_bits_default[PG_G_IDX] |
			    pmap_bits_default[PG_M_IDX] |
			    pmap_bits_default[PG_A_IDX];
		}

		/*
		 * And the direct map space's PDP
		 */
		for (i = 0; i < ndmpdp; i++) {
			((pdp_entry_t *)DMPDPphys)[i] = DMPDphys +
							(i << PAGE_SHIFT);
			((pdp_entry_t *)DMPDPphys)[i] |=
			    pmap_bits_default[PG_RW_IDX] |
			    pmap_bits_default[PG_V_IDX] |
			    pmap_bits_default[PG_A_IDX];
		}
	}
#if 0
	else {
		/*
		 * 1GB pages
		 */
		for (i = 0; i < ndmpdp; i++) {
			((pdp_entry_t *)DMPDPphys)[i] =
						(vm_paddr_t)i << PDPSHIFT;
			((pdp_entry_t *)DMPDPphys)[i] |=
			    pmap_bits_default[PG_RW_IDX] |
			    pmap_bits_default[PG_V_IDX] |
			    pmap_bits_default[PG_PS_IDX] |
			    pmap_bits_default[PG_G_IDX] |
			    pmap_bits_default[PG_M_IDX] |
			    pmap_bits_default[PG_A_IDX];
		}
	}
#endif

	/* And recursively map PML4 to itself in order to get PTmap */
	((pdp_entry_t *)KPML4phys)[PML4PML4I] = KPML4phys;
	((pdp_entry_t *)KPML4phys)[PML4PML4I] |=
	    pmap_bits_default[PG_RW_IDX] |
	    pmap_bits_default[PG_V_IDX] |
	    pmap_bits_default[PG_A_IDX];

	/*
	 * Connect the Direct Map slots up to the PML4
	 */
	for (j = 0; j < NDMPML4E; ++j) {
		((pdp_entry_t *)KPML4phys)[DMPML4I + j] =
		    (DMPDPphys + ((vm_paddr_t)j << PAGE_SHIFT)) |
		    pmap_bits_default[PG_RW_IDX] |
		    pmap_bits_default[PG_V_IDX] |
		    pmap_bits_default[PG_A_IDX];
	}

	/*
	 * Connect the KVA slot up to the PML4
	 */
	for (j = 0; j < NKPML4E; ++j) {
		((pdp_entry_t *)KPML4phys)[KPML4I + j] =
		    KPDPphys + ((vm_paddr_t)j << PAGE_SHIFT);
		((pdp_entry_t *)KPML4phys)[KPML4I + j] |=
		    pmap_bits_default[PG_RW_IDX] |
		    pmap_bits_default[PG_V_IDX] |
		    pmap_bits_default[PG_A_IDX];
	}
	cpu_mfence();
	cpu_invltlb();
}

/*
 *	Bootstrap the system enough to run with virtual memory.
 *
 *	On x86_64 this is called after mapping has already been enabled
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
	int i;

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
	x86_64_protection_init();

	/*
	 * The kernel's pmap is statically allocated so we don't have to use
	 * pmap_create, which is unlikely to work correctly at this part of
	 * the boot sequence (XXX and which no longer exists).
	 */
	kernel_pmap.pm_pml4 = (pdp_entry_t *) (PTOV_OFFSET + KPML4phys);
	kernel_pmap.pm_count = 1;
	CPUMASK_ASSALLONES(kernel_pmap.pm_active);
	RB_INIT(&kernel_pmap.pm_pvroot);
	spin_init(&kernel_pmap.pm_spin, "pmapbootstrap");
	for (i = 0; i < PM_PLACEMARKS; ++i)
		kernel_pmap.pm_placemarks[i] = PM_NOPLACEMARK;

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
	virtual_start = pmap_kmem_choose(virtual_start);

	*CMAP1 = 0;

	/*
	 * PG_G is terribly broken on SMP because we IPI invltlb's in some
	 * cases rather then invl1pg.  Actually, I don't even know why it
	 * works under UP because self-referential page table mappings
	 */
//	pgeflag = 0;

	cpu_invltlb();

	/* Initialize the PAT MSR */
	pmap_init_pat();
	pmap_pinit_defaults(&kernel_pmap);

	TUNABLE_INT_FETCH("machdep.pmap_fast_kernel_cpusync",
			  &pmap_fast_kernel_cpusync);

}

/*
 * Setup the PAT MSR.
 */
void
pmap_init_pat(void)
{
	uint64_t pat_msr;
	u_long cr0, cr4;
	int i;

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
	pat_pte_index[PAT_WRITE_THROUGH]= 0         | X86_PG_NC_PWT;
	pat_pte_index[PAT_UNCACHED]	= X86_PG_NC_PCD;
	pat_pte_index[PAT_UNCACHEABLE]	= X86_PG_NC_PCD | X86_PG_NC_PWT;
	pat_pte_index[PAT_WRITE_PROTECTED] = pat_pte_index[PAT_UNCACHEABLE];
	pat_pte_index[PAT_WRITE_COMBINING] = pat_pte_index[PAT_UNCACHEABLE];

	if (cpu_feature & CPUID_PAT) {
		/*
		 * If we support the PAT then set-up entries for
		 * WRITE_PROTECTED and WRITE_COMBINING using bit patterns
		 * 5 and 6.
		 */
		pat_msr = (pat_msr & ~PAT_MASK(5)) |
			  PAT_VALUE(5, PAT_WRITE_PROTECTED);
		pat_msr = (pat_msr & ~PAT_MASK(6)) |
			  PAT_VALUE(6, PAT_WRITE_COMBINING);
		pat_pte_index[PAT_WRITE_PROTECTED] = X86_PG_PTE_PAT | X86_PG_NC_PWT;
		pat_pte_index[PAT_WRITE_COMBINING] = X86_PG_PTE_PAT | X86_PG_NC_PCD;

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

	for (i = 0; i < 8; ++i) {
		pt_entry_t pte;

		pte = pat_pte_index[i];
		if (pte & X86_PG_PTE_PAT) {
			pte &= ~X86_PG_PTE_PAT;
			pte |= X86_PG_PDE_PAT;
		}
		pat_pde_index[i] = pte;
	}
}

/*
 * Set 4mb pdir for mp startup
 */
void
pmap_set_opt(void)
{
	if (cpu_feature & CPUID_PSE) {
		load_cr4(rcr4() | CR4_PSE);
		if (mycpu->gd_cpuid == 0) 	/* only on BSP */
			cpu_invltlb();
	}

	/*
	 * Check for SMAP support and enable if available.  Must be done
	 * after cr3 is loaded, and on all cores.
	 */
	if (cpu_stdext_feature & CPUID_STDEXT_SMAP) {
		load_cr4(rcr4() | CR4_SMAP);
	}
	if (cpu_stdext_feature & CPUID_STDEXT_SMEP) {
		load_cr4(rcr4() | CR4_SMEP);
	}
}

/*
 * SMAP is just a processor flag, but SMEP can only be enabled
 * and disabled via CR4.  We still use the processor flag to
 * disable SMAP because the page-fault/trap code checks it, in
 * order to allow a page-fault to actually occur.
 */
void
smap_smep_disable(void)
{
	/*
	 * disable SMAP.  This also bypasses a software failsafe check
	 * in the trap() code.
	 */
	smap_open();

	/*
	 * Also needed to bypass a software failsafe check in the trap()
	 * code and allow the userspace address fault from kernel mode
	 * to proceed.
	 *
	 * Note that This will not reload %rip because pcb_onfault_rsp will
	 * not match.  Just setting it to non-NULL is sufficient to bypass
	 * the checks.
	 */
	curthread->td_pcb->pcb_onfault = (void *)1;

	/*
	 * Disable SMEP (requires modifying cr4)
	 */
	if (cpu_stdext_feature & CPUID_STDEXT_SMEP)
		load_cr4(rcr4() & ~CR4_SMEP);
}

void
smap_smep_enable(void)
{
	if (cpu_stdext_feature & CPUID_STDEXT_SMEP)
		load_cr4(rcr4() | CR4_SMEP);
	curthread->td_pcb->pcb_onfault = NULL;
	smap_close();
}

/*
 * Early initialization of the pmap module.
 *
 * Called by vm_init, to initialize any structures that the pmap
 * system needs to map virtual memory.  pmap_init has been enhanced to
 * support in a fairly consistant way, discontiguous physical memory.
 */
void
pmap_init(void)
{
	vm_pindex_t initial_pvs;
	vm_pindex_t i;

	/*
	 * Allocate memory for random pmap data structures.  Includes the
	 * pv_head_table.
	 */
	for (i = 0; i < vm_page_array_size; i++) {
		vm_page_t m;

		m = &vm_page_array[i];
		m->md.interlock_count = 0;
	}

	/*
	 * init the pv free list
	 */
	initial_pvs = vm_page_array_size;
	if (initial_pvs < MINPV)
		initial_pvs = MINPV;
	pvzone = &pvzone_store;
	pvinit = (void *)kmem_alloc(&kernel_map,
				    initial_pvs * sizeof (struct pv_entry),
				    VM_SUBSYS_PVENTRY);
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
 *
 * Also create the kernel page table template for isolated user
 * pmaps.
 */
static void pmap_init_iso_range(vm_offset_t base, size_t bytes);
static void pmap_init2_iso_pmap(void);
#if 0
static void dump_pmap(pmap_t pmap, pt_entry_t pte, int level, vm_offset_t base);
#endif

void
pmap_init2(void)
{
	vm_pindex_t entry_max;

	/*
	 * We can significantly reduce pv_entry_max from historical
	 * levels because pv_entry's are no longer use for PTEs at the
	 * leafs.  This prevents excessive pcpu caching on many-core
	 * boxes (even with the further '/ 16' done in zinitna().
	 *
	 * Remember, however, that processes can share physical pages
	 * with each process still needing the pdp/pd/pt infrstructure
	 * (which still use pv_entry's).  And don't just assume that
	 * every PT will be completely filled up.  So don't make it
	 * too small.
	 */
	entry_max = maxproc * 32 + vm_page_array_size / 16;
	TUNABLE_LONG_FETCH("vm.pmap.pv_entries", &entry_max);
	vm_pmap_pv_entries = entry_max;

	/*
	 * Subtract out pages already installed in the zone (hack)
	 */
	if (entry_max <= MINPV)
		entry_max = MINPV;

	zinitna(pvzone, NULL, 0, entry_max, ZONE_INTERRUPT);

	/*
	 * Enable dynamic deletion of empty higher-level page table pages
	 * by default only if system memory is < 8GB (use 7GB for slop).
	 * This can save a little memory, but imposes significant
	 * performance overhead for things like bulk builds, and for programs
	 * which do a lot of memory mapping and memory unmapping.
	 */
#if 0
	if (pmap_dynamic_delete < 0) {
		if (vmstats.v_page_count < 7LL * 1024 * 1024 * 1024 / PAGE_SIZE)
			pmap_dynamic_delete = 1;
		else
			pmap_dynamic_delete = 0;
	}
#endif
	/*
	 * Disable so vm_map_backing iterations do not race
	 */
	pmap_dynamic_delete = 0;

	/*
	 * Automatic detection of Intel meltdown bug requiring user/kernel
	 * mmap isolation.
	 *
	 * Currently there are so many Intel cpu's impacted that its better
	 * to whitelist future Intel CPUs.  Most? AMD cpus are not impacted
	 * so the default is off for AMD.
	 */
	if (meltdown_mitigation < 0) {
		if (cpu_vendor_id == CPU_VENDOR_INTEL) {
			meltdown_mitigation = 1;
			if (cpu_ia32_arch_caps & IA32_ARCH_CAP_RDCL_NO)
				meltdown_mitigation = 0;
		} else {
			meltdown_mitigation = 0;
		}
	}
	if (meltdown_mitigation) {
		kprintf("machdep.meltdown_mitigation enabled to "
			"protect against (mostly Intel) meltdown bug\n");
		kprintf("system call performance will be impacted\n");
	}

	pmap_init2_iso_pmap();
}

/*
 * Create the isolation pmap template.  Once created, the template
 * is static and its PML4e entries are used to populate the
 * kernel portion of any isolated user pmaps.
 *
 * Our isolation pmap must contain:
 * (1) trampoline area for all cpus
 * (2) common_tss area for all cpus (its part of the trampoline area now)
 * (3) IDT for all cpus
 * (4) GDT for all cpus
 */
static void
pmap_init2_iso_pmap(void)
{
	int n;

	if (bootverbose)
		kprintf("Initialize isolation pmap\n");

	/*
	 * Try to use our normal API calls to make this easier.  We have
	 * to scrap the shadowed kernel PDPs pmap_pinit() creates for our
	 * iso_pmap.
	 */
	pmap_pinit(&iso_pmap);
	bzero(iso_pmap.pm_pml4, PAGE_SIZE);

	/*
	 * Install areas needed by the cpu and trampoline.
	 */
	for (n = 0; n < ncpus; ++n) {
		struct privatespace *ps;

		ps = CPU_prvspace[n];
		pmap_init_iso_range((vm_offset_t)&ps->trampoline,
				    sizeof(ps->trampoline));
		pmap_init_iso_range((vm_offset_t)&ps->dblstack,
				    sizeof(ps->dblstack));
		pmap_init_iso_range((vm_offset_t)&ps->dbgstack,
				    sizeof(ps->dbgstack));
		pmap_init_iso_range((vm_offset_t)&ps->common_tss,
				    sizeof(ps->common_tss));
		pmap_init_iso_range(r_idt_arr[n].rd_base,
				    r_idt_arr[n].rd_limit + 1);
	}
	pmap_init_iso_range((register_t)gdt, sizeof(gdt));
	pmap_init_iso_range((vm_offset_t)(int *)btext,
			    (vm_offset_t)(int *)etext -
			     (vm_offset_t)(int *)btext);

#if 0
	kprintf("Dump iso_pmap:\n");
	dump_pmap(&iso_pmap, vtophys(iso_pmap.pm_pml4), 0, 0);
	kprintf("\nDump kernel_pmap:\n");
	dump_pmap(&kernel_pmap, vtophys(kernel_pmap.pm_pml4), 0, 0);
#endif
}

/*
 * This adds a kernel virtual address range to the isolation pmap.
 */
static void
pmap_init_iso_range(vm_offset_t base, size_t bytes)
{
	pv_entry_t pv;
	pv_entry_t pvp;
	pt_entry_t *ptep;
	pt_entry_t pte;
	vm_offset_t va;

	if (bootverbose) {
		kprintf("isolate %016jx-%016jx (%zd)\n",
			base, base + bytes, bytes);
	}
	va = base & ~(vm_offset_t)PAGE_MASK;
	while (va < base + bytes) {
		if ((va & PDRMASK) == 0 && va + NBPDR <= base + bytes &&
		    (ptep = pmap_pt(&kernel_pmap, va)) != NULL &&
		    (*ptep & kernel_pmap.pmap_bits[PG_V_IDX]) &&
		    (*ptep & kernel_pmap.pmap_bits[PG_PS_IDX])) {
			/*
			 * Use 2MB pages if possible
			 */
			pte = *ptep;
			pv = pmap_allocpte(&iso_pmap, pmap_pd_pindex(va), &pvp);
			ptep = pv_pte_lookup(pv, (va >> PDRSHIFT) & 511);
			*ptep = pte;
			va += NBPDR;
		} else {
			/*
			 * Otherwise use 4KB pages
			 */
			pv = pmap_allocpte(&iso_pmap, pmap_pt_pindex(va), &pvp);
			ptep = pv_pte_lookup(pv, (va >> PAGE_SHIFT) & 511);
			*ptep = vtophys(va) | kernel_pmap.pmap_bits[PG_RW_IDX] |
					      kernel_pmap.pmap_bits[PG_V_IDX] |
					      kernel_pmap.pmap_bits[PG_A_IDX] |
					      kernel_pmap.pmap_bits[PG_M_IDX];

			va += PAGE_SIZE;
		}
		pv_put(pv);
		pv_put(pvp);
	}
}

#if 0
/*
 * Useful debugging pmap dumper, do not remove (#if 0 when not in use)
 */
static
void
dump_pmap(pmap_t pmap, pt_entry_t pte, int level, vm_offset_t base)
{
	pt_entry_t *ptp;
	vm_offset_t incr;
	int i;

	switch(level) {
	case 0:					/* PML4e page, 512G entries */
		incr = (1LL << 48) / 512;
		break;
	case 1:					/* PDP page, 1G entries */
		incr = (1LL << 39) / 512;
		break;
	case 2:					/* PD page, 2MB entries */
		incr = (1LL << 30) / 512;
		break;
	case 3:					/* PT page, 4KB entries */
		incr = (1LL << 21) / 512;
		break;
	default:
		incr = 0;
		break;
	}

	if (level == 0)
		kprintf("cr3 %016jx @ va=%016jx\n", pte, base);
	ptp = (void *)PHYS_TO_DMAP(pte & ~(pt_entry_t)PAGE_MASK);
	for (i = 0; i < 512; ++i) {
		if (level == 0 && i == 128)
			base += 0xFFFF000000000000LLU;
		if (ptp[i]) {
			kprintf("%*.*s ", level * 4, level * 4, "");
			if (level == 1 && (ptp[i] & 0x180) == 0x180) {
				kprintf("va=%016jx %3d term %016jx (1GB)\n",
					base, i, ptp[i]);
			} else if (level == 2 && (ptp[i] & 0x180) == 0x180) {
				kprintf("va=%016jx %3d term %016jx (2MB)\n",
					base, i, ptp[i]);
			} else if (level == 3) {
				kprintf("va=%016jx %3d term %016jx\n",
					base, i, ptp[i]);
			} else {
				kprintf("va=%016jx %3d deep %016jx\n",
					base, i, ptp[i]);
				dump_pmap(pmap, ptp[i], level + 1, base);
			}
		}
		base += incr;
	}
}

#endif

/*
 * Typically used to initialize a fictitious page by vm/device_pager.c
 */
void
pmap_page_init(struct vm_page *m)
{
	vm_page_init(m);
	m->md.interlock_count = 0;
}

/***************************************************
 * Low level helper routines.....
 ***************************************************/

/*
 * Extract the physical page address associated with the map/VA pair.
 * The page must be wired for this to work reliably.
 */
vm_paddr_t 
pmap_extract(pmap_t pmap, vm_offset_t va, void **handlep)
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
		if (pt && (*pt & pmap->pmap_bits[PG_V_IDX])) {
			if (*pt & pmap->pmap_bits[PG_PS_IDX]) {
				rtval = *pt & PG_PS_FRAME;
				rtval |= va & PDRMASK;
			} else {
				ptep = pmap_pt_to_pte(*pt, va);
				if (*pt & pmap->pmap_bits[PG_V_IDX]) {
					rtval = *ptep & PG_FRAME;
					rtval |= va & PAGE_MASK;
				}
			}
		}
		if (handlep)
			*handlep = NULL;
	} else {
		/*
		 * User pages currently do not direct-map the page directory
		 * and some pages might not used managed PVs.  But all PT's
		 * will have a PV.
		 */
		pt_pv = pv_get(pmap, pmap_pt_pindex(va), NULL);
		if (pt_pv) {
			ptep = pv_pte_lookup(pt_pv, pmap_pte_index(va));
			if (*ptep & pmap->pmap_bits[PG_V_IDX]) {
				rtval = *ptep & PG_FRAME;
				rtval |= va & PAGE_MASK;
			}
			if (handlep)
				*handlep = pt_pv;	/* locked until done */
			else
				pv_put (pt_pv);
		} else if (handlep) {
			*handlep = NULL;
		}
	}
	return rtval;
}

void
pmap_extract_done(void *handle)
{
	if (handle)
		pv_put((pv_entry_t)handle);
}

/*
 * Similar to extract but checks protections, SMP-friendly short-cut for
 * vm_fault_page[_quick]().  Can return NULL to cause the caller to
 * fall-through to the real fault code.  Does not work with HVM page
 * tables.
 *
 * if busyp is NULL the returned page, if not NULL, is held (and not busied).
 *
 * If busyp is not NULL and this function sets *busyp non-zero, the returned
 * page is busied (and not held).
 *
 * If busyp is not NULL and this function sets *busyp to zero, the returned
 * page is held (and not busied).
 *
 * If VM_PROT_WRITE is set in prot, and the pte is already writable, the
 * returned page will be dirtied.  If the pte is not already writable NULL
 * is returned.  In otherwords, if the bit is set and a vm_page_t is returned,
 * any COW will already have happened and that page can be written by the
 * caller.
 *
 * WARNING! THE RETURNED PAGE IS ONLY HELD AND NOT SUITABLE FOR READING
 *	    OR WRITING AS-IS.
 */
vm_page_t
pmap_fault_page_quick(pmap_t pmap, vm_offset_t va, vm_prot_t prot, int *busyp)
{
	if (pmap &&
	    va < VM_MAX_USER_ADDRESS &&
	    (pmap->pm_flags & PMAP_HVM) == 0) {
		pv_entry_t pt_pv;
		pv_entry_t pte_pv;
		pt_entry_t *ptep;
		pt_entry_t req;
		vm_page_t m;
		int error;

		req = pmap->pmap_bits[PG_V_IDX] |
		      pmap->pmap_bits[PG_U_IDX];
		if (prot & VM_PROT_WRITE)
			req |= pmap->pmap_bits[PG_RW_IDX];

		pt_pv = pv_get(pmap, pmap_pt_pindex(va), NULL);
		if (pt_pv == NULL)
			return (NULL);
		ptep = pv_pte_lookup(pt_pv, pmap_pte_index(va));
		if ((*ptep & req) != req) {
			pv_put(pt_pv);
			return (NULL);
		}
		pte_pv = pv_get_try(pmap, pmap_pte_pindex(va), NULL, &error);
		if (pte_pv && error == 0) {
			m = pte_pv->pv_m;
			if (prot & VM_PROT_WRITE) {
				/* interlocked by presence of pv_entry */
				vm_page_dirty(m);
			}
			if (busyp) {
				if (prot & VM_PROT_WRITE) {
					if (vm_page_busy_try(m, TRUE))
						m = NULL;
					*busyp = 1;
				} else {
					vm_page_hold(m);
					*busyp = 0;
				}
			} else {
				vm_page_hold(m);
			}
			pv_put(pte_pv);
		} else if (pte_pv) {
			pv_drop(pte_pv);
			m = NULL;
		} else {
			/* error, since we didn't request a placemarker */
			m = NULL;
		}
		pv_put(pt_pv);
		return(m);
	} else {
		return(NULL);
	}
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
		if (pt & kernel_pmap.pmap_bits[PG_PS_IDX]) {
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
	pt_entry_t *ptep;
	pt_entry_t npte;

	npte = pa |
	       kernel_pmap.pmap_bits[PG_RW_IDX] |
	       kernel_pmap.pmap_bits[PG_V_IDX];
//	       pgeflag;
	ptep = vtopte(va);
#if 1
	pmap_inval_smp(&kernel_pmap, va, 1, ptep, npte);
#else
	/* FUTURE */
	if (*ptep)
		pmap_inval_smp(&kernel_pmap, va, ptep, npte);
	else
		*ptep = npte;
#endif
}

/*
 * Similar to pmap_kenter(), except we only invalidate the mapping on the
 * current CPU.  Returns 0 if the previous pte was 0, 1 if it wasn't
 * (caller can conditionalize calling smp_invltlb()).
 */
int
pmap_kenter_quick(vm_offset_t va, vm_paddr_t pa)
{
	pt_entry_t *ptep;
	pt_entry_t npte;
	int res;

	npte = pa | kernel_pmap.pmap_bits[PG_RW_IDX] |
		    kernel_pmap.pmap_bits[PG_V_IDX];
	// npte |= pgeflag;
	ptep = vtopte(va);
#if 1
	res = 1;
#else
	/* FUTURE */
	res = (*ptep != 0);
#endif
	atomic_swap_long(ptep, npte);
	cpu_invlpg((void *)va);

	return res;
}

/*
 * Enter addresses into the kernel pmap but don't bother
 * doing any tlb invalidations.  Caller will do a rollup
 * invalidation via pmap_rollup_inval().
 */
int
pmap_kenter_noinval(vm_offset_t va, vm_paddr_t pa)
{
	pt_entry_t *ptep;
	pt_entry_t npte;
	int res;

	npte = pa |
	    kernel_pmap.pmap_bits[PG_RW_IDX] |
	    kernel_pmap.pmap_bits[PG_V_IDX];
//	    pgeflag;
	ptep = vtopte(va);
#if 1
	res = 1;
#else
	/* FUTURE */
	res = (*ptep != 0);
#endif
	atomic_swap_long(ptep, npte);
	cpu_invlpg((void *)va);

	return res;
}

/*
 * remove a page from the kernel pagetables
 */
void
pmap_kremove(vm_offset_t va)
{
	pt_entry_t *ptep;

	ptep = vtopte(va);
	pmap_inval_smp(&kernel_pmap, va, 1, ptep, 0);
}

void
pmap_kremove_quick(vm_offset_t va)
{
	pt_entry_t *ptep;

	ptep = vtopte(va);
	(void)pte_load_clear(ptep);
	cpu_invlpg((void *)va);
}

/*
 * Remove addresses from the kernel pmap but don't bother
 * doing any tlb invalidations.  Caller will do a rollup
 * invalidation via pmap_rollup_inval().
 */
void
pmap_kremove_noinval(vm_offset_t va)
{
	pt_entry_t *ptep;

	ptep = vtopte(va);
	(void)pte_load_clear(ptep);
}

/*
 * XXX these need to be recoded.  They are not used in any critical path.
 */
void
pmap_kmodify_rw(vm_offset_t va)
{
	atomic_set_long(vtopte(va), kernel_pmap.pmap_bits[PG_RW_IDX]);
	cpu_invlpg((void *)va);
}

/* NOT USED
void
pmap_kmodify_nc(vm_offset_t va)
{
	atomic_set_long(vtopte(va), PG_N);
	cpu_invlpg((void *)va);
}
*/

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

#define PMAP_CLFLUSH_THRESHOLD  (2 * 1024 * 1024)

/*
 * Remove the specified set of pages from the data and instruction caches.
 *
 * In contrast to pmap_invalidate_cache_range(), this function does not
 * rely on the CPU's self-snoop feature, because it is intended for use
 * when moving pages into a different cache domain.
 */
void
pmap_invalidate_cache_pages(vm_page_t *pages, int count)
{
	vm_offset_t daddr, eva;
	int i;

	if (count >= PMAP_CLFLUSH_THRESHOLD / PAGE_SIZE ||
	    (cpu_feature & CPUID_CLFSH) == 0)
		wbinvd();
	else {
		cpu_mfence();
		for (i = 0; i < count; i++) {
			daddr = PHYS_TO_DMAP(VM_PAGE_TO_PHYS(pages[i]));
			eva = daddr + PAGE_SIZE;
			for (; daddr < eva; daddr += cpu_clflush_line_size)
				clflush(daddr);
		}
		cpu_mfence();
	}
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

/*
 * Invalidate the specified range of virtual memory on all cpus associated
 * with the pmap.
 */
void
pmap_invalidate_range(pmap_t pmap, vm_offset_t sva, vm_offset_t eva)
{
	pmap_inval_smp(pmap, sva, (eva - sva) >> PAGE_SHIFT, NULL, 0);
}

/*
 * Add a list of wired pages to the kva.  This routine is used for temporary
 * kernel mappings such as those found in buffer cache buffer.  Page
 * modifications and accesses are not tracked or recorded.
 *
 * NOTE! Old mappings are simply overwritten, and we cannot assume relaxed
 *	 semantics as previous mappings may have been zerod without any
 *	 invalidation.
 *
 * The page *must* be wired.
 */
static __inline void
_pmap_qenter(vm_offset_t beg_va, vm_page_t *m, int count, int doinval)
{
	vm_offset_t end_va;
	vm_offset_t va;

	end_va = beg_va + count * PAGE_SIZE;

	for (va = beg_va; va < end_va; va += PAGE_SIZE) {
		pt_entry_t pte;
		pt_entry_t *ptep;

		ptep = vtopte(va);
		pte = VM_PAGE_TO_PHYS(*m) |
			kernel_pmap.pmap_bits[PG_RW_IDX] |
			kernel_pmap.pmap_bits[PG_V_IDX] |
			kernel_pmap.pmap_cache_bits_pte[(*m)->pat_mode];
//		pgeflag;
		atomic_swap_long(ptep, pte);
		m++;
	}
	if (doinval)
		pmap_invalidate_range(&kernel_pmap, beg_va, end_va);
}

void
pmap_qenter(vm_offset_t beg_va, vm_page_t *m, int count)
{
	_pmap_qenter(beg_va, m, count, 1);
}

void
pmap_qenter_noinval(vm_offset_t beg_va, vm_page_t *m, int count)
{
	_pmap_qenter(beg_va, m, count, 0);
}

/*
 * This routine jerks page mappings from the kernel -- it is meant only
 * for temporary mappings such as those found in buffer cache buffers.
 * No recording modified or access status occurs.
 *
 * MPSAFE, INTERRUPT SAFE (cluster callback)
 */
void
pmap_qremove(vm_offset_t beg_va, int count)
{
	vm_offset_t end_va;
	vm_offset_t va;

	end_va = beg_va + count * PAGE_SIZE;

	for (va = beg_va; va < end_va; va += PAGE_SIZE) {
		pt_entry_t *pte;

		pte = vtopte(va);
		(void)pte_load_clear(pte);
		cpu_invlpg((void *)va);
	}
	pmap_invalidate_range(&kernel_pmap, beg_va, end_va);
}

/*
 * This routine removes temporary kernel mappings, only invalidating them
 * on the current cpu.  It should only be used under carefully controlled
 * conditions.
 */
void
pmap_qremove_quick(vm_offset_t beg_va, int count)
{
	vm_offset_t end_va;
	vm_offset_t va;

	end_va = beg_va + count * PAGE_SIZE;

	for (va = beg_va; va < end_va; va += PAGE_SIZE) {
		pt_entry_t *pte;

		pte = vtopte(va);
		(void)pte_load_clear(pte);
		cpu_invlpg((void *)va);
	}
}

/*
 * This routine removes temporary kernel mappings *without* invalidating
 * the TLB.  It can only be used on permanent kva reservations such as those
 * found in buffer cache buffers, under carefully controlled circumstances.
 *
 * NOTE: Repopulating these KVAs requires unconditional invalidation.
 *	 (pmap_qenter() does unconditional invalidation).
 */
void
pmap_qremove_noinval(vm_offset_t beg_va, int count)
{
	vm_offset_t end_va;
	vm_offset_t va;

	end_va = beg_va + count * PAGE_SIZE;

	for (va = beg_va; va < end_va; va += PAGE_SIZE) {
		pt_entry_t *pte;

		pte = vtopte(va);
		(void)pte_load_clear(pte);
	}
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

static void
pmap_pinit_defaults(struct pmap *pmap)
{
	bcopy(pmap_bits_default, pmap->pmap_bits,
	      sizeof(pmap_bits_default));
	bcopy(protection_codes, pmap->protection_codes,
	      sizeof(protection_codes));
	bcopy(pat_pte_index, pmap->pmap_cache_bits_pte,
	      sizeof(pat_pte_index));
	bcopy(pat_pde_index, pmap->pmap_cache_bits_pde,
	      sizeof(pat_pte_index));
	pmap->pmap_cache_mask_pte = X86_PG_NC_PWT | X86_PG_NC_PCD | X86_PG_PTE_PAT;
	pmap->pmap_cache_mask_pde = X86_PG_NC_PWT | X86_PG_NC_PCD | X86_PG_PDE_PAT;
	pmap->copyinstr = std_copyinstr;
	pmap->copyin = std_copyin;
	pmap->copyout = std_copyout;
	pmap->fubyte = std_fubyte;
	pmap->subyte = std_subyte;
	pmap->fuword32 = std_fuword32;
	pmap->fuword64 = std_fuword64;
	pmap->suword32 = std_suword32;
	pmap->suword64 = std_suword64;
	pmap->swapu32 = std_swapu32;
	pmap->swapu64 = std_swapu64;
	pmap->fuwordadd32 = std_fuwordadd32;
	pmap->fuwordadd64 = std_fuwordadd64;
}
/*
 * Initialize pmap0/vmspace0.
 *
 * On architectures where the kernel pmap is not integrated into the user
 * process pmap, this pmap represents the process pmap, not the kernel pmap.
 * kernel_pmap should be used to directly access the kernel_pmap.
 */
void
pmap_pinit0(struct pmap *pmap)
{
	int i;

	pmap->pm_pml4 = (pml4_entry_t *)(PTOV_OFFSET + KPML4phys);
	pmap->pm_count = 1;
	CPUMASK_ASSZERO(pmap->pm_active);
	pmap->pm_pvhint_pt = NULL;
	pmap->pm_pvhint_unused = NULL;
	RB_INIT(&pmap->pm_pvroot);
	spin_init(&pmap->pm_spin, "pmapinit0");
	for (i = 0; i < PM_PLACEMARKS; ++i)
		pmap->pm_placemarks[i] = PM_NOPLACEMARK;
	bzero(&pmap->pm_stats, sizeof pmap->pm_stats);
	pmap_pinit_defaults(pmap);
}

/*
 * Initialize a preallocated and zeroed pmap structure,
 * such as one in a vmspace structure.
 */
static void
pmap_pinit_simple(struct pmap *pmap)
{
	int i;

	/*
	 * Misc initialization
	 */
	pmap->pm_count = 1;
	CPUMASK_ASSZERO(pmap->pm_active);
	pmap->pm_pvhint_pt = NULL;
	pmap->pm_pvhint_unused = NULL;
	pmap->pm_flags = PMAP_FLAG_SIMPLE;

	pmap_pinit_defaults(pmap);

	/*
	 * Don't blow up locks/tokens on re-use (XXX fix/use drop code
	 * for this).
	 */
	if (pmap->pm_pmlpv == NULL) {
		RB_INIT(&pmap->pm_pvroot);
		bzero(&pmap->pm_stats, sizeof pmap->pm_stats);
		spin_init(&pmap->pm_spin, "pmapinitsimple");
		for (i = 0; i < PM_PLACEMARKS; ++i)
			pmap->pm_placemarks[i] = PM_NOPLACEMARK;
	}
}

void
pmap_pinit(struct pmap *pmap)
{
	pv_entry_t pv;
	int j;

	if (pmap->pm_pmlpv) {
		if (pmap->pmap_bits[TYPE_IDX] != REGULAR_PMAP) {
			pmap_puninit(pmap);
		}
	}

	pmap_pinit_simple(pmap);
	pmap->pm_flags &= ~PMAP_FLAG_SIMPLE;

	/*
	 * No need to allocate page table space yet but we do need a valid
	 * page directory table.
	 */
	if (pmap->pm_pml4 == NULL) {
		pmap->pm_pml4 =
		    (pml4_entry_t *)kmem_alloc_pageable(&kernel_map,
							PAGE_SIZE * 2,
							VM_SUBSYS_PML4);
		pmap->pm_pml4_iso = (void *)((char *)pmap->pm_pml4 + PAGE_SIZE);
	}

	/*
	 * Allocate the PML4e table, which wires it even though it isn't
	 * being entered into some higher level page table (it being the
	 * highest level).  If one is already cached we don't have to do
	 * anything.
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
			    (DMPDPphys + ((vm_paddr_t)j << PAGE_SHIFT)) |
			    pmap->pmap_bits[PG_RW_IDX] |
			    pmap->pmap_bits[PG_V_IDX] |
			    pmap->pmap_bits[PG_A_IDX];
		}
		for (j = 0; j < NKPML4E; ++j) {
			pmap->pm_pml4[KPML4I + j] =
			    (KPDPphys + ((vm_paddr_t)j << PAGE_SHIFT)) |
			    pmap->pmap_bits[PG_RW_IDX] |
			    pmap->pmap_bits[PG_V_IDX] |
			    pmap->pmap_bits[PG_A_IDX];
		}

		/*
		 * install self-referential address mapping entry
		 */
		pmap->pm_pml4[PML4PML4I] = VM_PAGE_TO_PHYS(pv->pv_m) |
		    pmap->pmap_bits[PG_V_IDX] |
		    pmap->pmap_bits[PG_RW_IDX] |
		    pmap->pmap_bits[PG_A_IDX];
	} else {
		KKASSERT(pv->pv_m->flags & PG_MAPPED);
		KKASSERT(pv->pv_m->flags & PG_WRITEABLE);
	}
	KKASSERT(pmap->pm_pml4[255] == 0);

	/*
	 * When implementing an isolated userland pmap, a second PML4e table
	 * is needed.  We use pmap_pml4_pindex() + 1 for convenience, but
	 * note that we do not operate on this table using our API functions
	 * so handling of the + 1 case is mostly just to prevent implosions.
	 *
	 * We install an isolated version of the kernel PDPs into this
	 * second PML4e table.  The pmap code will mirror all user PDPs
	 * between the primary and secondary PML4e table.
	 */
	if ((pv = pmap->pm_pmlpv_iso) == NULL && meltdown_mitigation &&
	    pmap != &iso_pmap) {
		pv = pmap_allocpte(pmap, pmap_pml4_pindex() + 1, NULL);
		pmap->pm_pmlpv_iso = pv;
		pmap_kenter((vm_offset_t)pmap->pm_pml4_iso,
			    VM_PAGE_TO_PHYS(pv->pv_m));
		pv_put(pv);

		/*
		 * Install an isolated version of the kernel pmap for
		 * user consumption, using PDPs constructed in iso_pmap.
		 */
		for (j = 0; j < NKPML4E; ++j) {
			pmap->pm_pml4_iso[KPML4I + j] =
				iso_pmap.pm_pml4[KPML4I + j];
		}
	} else if (pv) {
		KKASSERT(pv->pv_m->flags & PG_MAPPED);
		KKASSERT(pv->pv_m->flags & PG_WRITEABLE);
	}
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

	KKASSERT(CPUMASK_TESTZERO(pmap->pm_active));
	if ((pv = pmap->pm_pmlpv) != NULL) {
		if (pv_hold_try(pv) == 0)
			pv_lock(pv);
		KKASSERT(pv == pmap->pm_pmlpv);
		p = pmap_remove_pv_page(pv, 1);
		pv_free(pv, NULL);
		pv = NULL;	/* safety */
		pmap_kremove((vm_offset_t)pmap->pm_pml4);
		vm_page_busy_wait(p, FALSE, "pgpun");
		KKASSERT(p->flags & PG_UNQUEUED);
		vm_page_unwire(p, 0);
		vm_page_flag_clear(p, PG_MAPPED | PG_WRITEABLE);
		vm_page_free(p);
		pmap->pm_pmlpv = NULL;
	}
	if ((pv = pmap->pm_pmlpv_iso) != NULL) {
		if (pv_hold_try(pv) == 0)
			pv_lock(pv);
		KKASSERT(pv == pmap->pm_pmlpv_iso);
		p = pmap_remove_pv_page(pv, 1);
		pv_free(pv, NULL);
		pv = NULL;	/* safety */
		pmap_kremove((vm_offset_t)pmap->pm_pml4_iso);
		vm_page_busy_wait(p, FALSE, "pgpun");
		KKASSERT(p->flags & PG_UNQUEUED);
		vm_page_unwire(p, 0);
		vm_page_flag_clear(p, PG_MAPPED | PG_WRITEABLE);
		vm_page_free(p);
		pmap->pm_pmlpv_iso = NULL;
	}
	if (pmap->pm_pml4) {
		KKASSERT(pmap->pm_pml4 != (void *)(PTOV_OFFSET + KPML4phys));
		kmem_free(&kernel_map,
			  (vm_offset_t)pmap->pm_pml4, PAGE_SIZE * 2);
		pmap->pm_pml4 = NULL;
		pmap->pm_pml4_iso = NULL;
	}
	KKASSERT(pmap->pm_stats.resident_count == 0);
	KKASSERT(pmap->pm_stats.wired_count == 0);
}

/*
 * This function is now unused (used to add the pmap to the pmap_list)
 */
void
pmap_pinit2(struct pmap *pmap)
{
}

/*
 * This routine is called when various levels in the page table need to
 * be populated.  This routine cannot fail.
 *
 * This function returns two locked pv_entry's, one representing the
 * requested pv and one representing the requested pv's parent pv.  If
 * an intermediate page table does not exist it will be created, mapped,
 * wired, and the parent page table will be given an additional hold
 * count representing the presence of the child pv_entry.
 */
static
pv_entry_t
pmap_allocpte(pmap_t pmap, vm_pindex_t ptepindex, pv_entry_t *pvpp)
{
	pt_entry_t *ptep;
	pt_entry_t *ptep_iso;
	pv_entry_t pv;
	pv_entry_t pvp;
	pt_entry_t v;
	vm_page_t m;
	int isnew;
	int ispt;

	/*
	 * If the pv already exists and we aren't being asked for the
	 * parent page table page we can just return it.  A locked+held pv
	 * is returned.  The pv will also have a second hold related to the
	 * pmap association that we don't have to worry about.
	 */
	ispt = 0;
	pv = pv_alloc(pmap, ptepindex, &isnew);
	if (isnew == 0 && pvpp == NULL)
		return(pv);

	/*
	 * DragonFly doesn't use PV's to represent terminal PTEs any more.
	 * The index range is still used for placemarkers, but not for
	 * actual pv_entry's.
	 */
	KKASSERT(ptepindex >= pmap_pt_pindex(0));

	/*
	 * Note that pt_pv's are only returned for user VAs. We assert that
	 * a pt_pv is not being requested for kernel VAs.  The kernel
	 * pre-wires all higher-level page tables so don't overload managed
	 * higher-level page tables on top of it!
	 *
	 * However, its convenient for us to allow the case when creating
	 * iso_pmap.  This is a bit of a hack but it simplifies iso_pmap
	 * a lot.
	 */

	/*
	 * The kernel never uses managed PT/PD/PDP pages.
	 */
	KKASSERT(pmap != &kernel_pmap);

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
	}

	if (isnew == 0)
		goto notnew;

	/*
	 * (isnew) is TRUE, pv is not terminal.
	 *
	 * (1) Add a wire count to the parent page table (pvp).
	 * (2) Allocate a VM page for the page table.
	 * (3) Enter the VM page into the parent page table.
	 *
	 * page table pages are marked PG_WRITEABLE and PG_MAPPED.
	 */
	if (pvp)
		vm_page_wire_quick(pvp->pv_m);

	for (;;) {
		m = vm_page_alloc(NULL, pv->pv_pindex,
				  VM_ALLOC_NORMAL | VM_ALLOC_SYSTEM |
				  VM_ALLOC_INTERRUPT);
		if (m)
			break;
		vm_wait(0);
	}
	vm_page_wire(m);	/* wire for mapping in parent */
	pmap_zero_page(VM_PAGE_TO_PHYS(m));
	m->valid = VM_PAGE_BITS_ALL;
	vm_page_flag_set(m, PG_MAPPED | PG_WRITEABLE | PG_UNQUEUED);
	KKASSERT(m->queue == PQ_NONE);

	pv->pv_m = m;

	/*
	 * (isnew) is TRUE, pv is not terminal.
	 *
	 * Wire the page into pvp.  Bump the resident_count for the pmap.
	 * There is no pvp for the top level, address the pm_pml4[] array
	 * directly.
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
		v = VM_PAGE_TO_PHYS(m) |
		    (pmap->pmap_bits[PG_RW_IDX] |
		     pmap->pmap_bits[PG_V_IDX] |
		     pmap->pmap_bits[PG_A_IDX]);
		if (ptepindex < NUPTE_USER)
			v |= pmap->pmap_bits[PG_U_IDX];
		if (ptepindex < pmap_pt_pindex(0))
			v |= pmap->pmap_bits[PG_M_IDX];

		ptep = pv_pte_lookup(pvp, ptepindex);
		if (pvp == pmap->pm_pmlpv && pmap->pm_pmlpv_iso)
			ptep_iso = pv_pte_lookup(pmap->pm_pmlpv_iso, ptepindex);
		else
			ptep_iso  = NULL;
		if (*ptep & pmap->pmap_bits[PG_V_IDX]) {
			panic("pmap_allocpte: ptpte present without pv_entry!");
		} else {
			pt_entry_t pte;

			pte = atomic_swap_long(ptep, v);
			if (ptep_iso)
				atomic_swap_long(ptep_iso, v);
			if (pte != 0) {
				kprintf("install pgtbl mixup 0x%016jx "
					"old/new 0x%016jx/0x%016jx\n",
					(intmax_t)ptepindex, pte, v);
			}
		}
	}
	vm_page_wakeup(m);

	/*
	 * (isnew) may be TRUE or FALSE, pv may or may not be terminal.
	 */
notnew:
	if (pvp) {
		KKASSERT(pvp->pv_m != NULL);
		ptep = pv_pte_lookup(pvp, ptepindex);
		v = VM_PAGE_TO_PHYS(pv->pv_m) |
		    (pmap->pmap_bits[PG_RW_IDX] |
		     pmap->pmap_bits[PG_V_IDX] |
		     pmap->pmap_bits[PG_A_IDX]);
		if (ptepindex < NUPTE_USER)
			v |= pmap->pmap_bits[PG_U_IDX];
		if (ptepindex < pmap_pt_pindex(0))
			v |= pmap->pmap_bits[PG_M_IDX];
		if (*ptep != v) {
			kprintf("mismatched upper level pt %016jx/%016jx\n",
				*ptep, v);
		}
	}
	if (pvpp)
		*pvpp = pvp;
	else if (pvp)
		pv_put(pvp);
	return (pv);
}

/*
 * Release any resources held by the given physical map.
 *
 * Called when a pmap initialized by pmap_pinit is being released.  Should
 * only be called if the map contains no valid mappings.
 */
struct pmap_release_info {
	pmap_t	pmap;
	int	retry;
	pv_entry_t pvp;
};

static int pmap_release_callback(pv_entry_t pv, void *data);

void
pmap_release(struct pmap *pmap)
{
	struct pmap_release_info info;

	KASSERT(CPUMASK_TESTZERO(pmap->pm_active),
		("pmap still active! %016jx",
		(uintmax_t)CPUMASK_LOWMASK(pmap->pm_active)));

	/*
	 * There is no longer a pmap_list, if there were we would remove the
	 * pmap from it here.
	 */

	/*
	 * Pull pv's off the RB tree in order from low to high and release
	 * each page.
	 */
	info.pmap = pmap;
	do {
		info.retry = 0;
		info.pvp = NULL;

		spin_lock(&pmap->pm_spin);
		RB_SCAN(pv_entry_rb_tree, &pmap->pm_pvroot, NULL,
			pmap_release_callback, &info);
		spin_unlock(&pmap->pm_spin);

		if (info.pvp)
			pv_put(info.pvp);
	} while (info.retry);


	/*
	 * One resident page (the pml4 page) should remain.  Two if
	 * the pmap has implemented an isolated userland PML4E table.
	 * No wired pages should remain.
	 */
	int expected_res = 0;

	if ((pmap->pm_flags & PMAP_FLAG_SIMPLE) == 0)
		++expected_res;
	if (pmap->pm_pmlpv_iso)
		++expected_res;

#if 1
	if (pmap->pm_stats.resident_count != expected_res ||
	    pmap->pm_stats.wired_count != 0) {
		kprintf("fatal pmap problem - pmap %p flags %08x "
			"rescnt=%jd wirecnt=%jd\n",
			pmap,
			pmap->pm_flags,
			pmap->pm_stats.resident_count,
			pmap->pm_stats.wired_count);
		tsleep(pmap, 0, "DEAD", 0);
	}
#else
	KKASSERT(pmap->pm_stats.resident_count == expected_res);
	KKASSERT(pmap->pm_stats.wired_count == 0);
#endif
}

/*
 * Called from low to high.  We must cache the proper parent pv so we
 * can adjust its wired count.
 */
static int
pmap_release_callback(pv_entry_t pv, void *data)
{
	struct pmap_release_info *info = data;
	pmap_t pmap = info->pmap;
	vm_pindex_t pindex;
	int r;

	/*
	 * Acquire a held and locked pv, check for release race
	 */
	pindex = pv->pv_pindex;
	if (info->pvp == pv) {
		spin_unlock(&pmap->pm_spin);
		info->pvp = NULL;
	} else if (pv_hold_try(pv)) {
		spin_unlock(&pmap->pm_spin);
	} else {
		spin_unlock(&pmap->pm_spin);
		pv_lock(pv);
		pv_put(pv);
		info->retry = 1;
		spin_lock(&pmap->pm_spin);

		return -1;
	}
	KKASSERT(pv->pv_pmap == pmap && pindex == pv->pv_pindex);

	if (pv->pv_pindex < pmap_pt_pindex(0)) {
		/*
		 * I am PTE, parent is PT
		 */
		pindex = pv->pv_pindex >> NPTEPGSHIFT;
		pindex += NUPTE_TOTAL;
	} else if (pv->pv_pindex < pmap_pd_pindex(0)) {
		/*
		 * I am PT, parent is PD
		 */
		pindex = (pv->pv_pindex - NUPTE_TOTAL) >> NPDEPGSHIFT;
		pindex += NUPTE_TOTAL + NUPT_TOTAL;
	} else if (pv->pv_pindex < pmap_pdp_pindex(0)) {
		/*
		 * I am PD, parent is PDP
		 */
		pindex = (pv->pv_pindex - NUPTE_TOTAL - NUPT_TOTAL) >>
			 NPDPEPGSHIFT;
		pindex += NUPTE_TOTAL + NUPT_TOTAL + NUPD_TOTAL;
	} else if (pv->pv_pindex < pmap_pml4_pindex()) {
		/*
		 * I am PDP, parent is PML4.  We always calculate the
		 * normal PML4 here, not the isolated PML4.
		 */
		pindex = pmap_pml4_pindex();
	} else {
		/*
		 * parent is NULL
		 */
		if (info->pvp) {
			pv_put(info->pvp);
			info->pvp = NULL;
		}
		pindex = 0;
	}
	if (pindex) {
		if (info->pvp && info->pvp->pv_pindex != pindex) {
			pv_put(info->pvp);
			info->pvp = NULL;
		}
		if (info->pvp == NULL)
			info->pvp = pv_get(pmap, pindex, NULL);
	} else {
		if (info->pvp) {
			pv_put(info->pvp);
			info->pvp = NULL;
		}
	}
	r = pmap_release_pv(pv, info->pvp, NULL);
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
pmap_release_pv(pv_entry_t pv, pv_entry_t pvp, pmap_inval_bulk_t *bulk)
{
	vm_page_t p;

	/*
	 * The pmap is currently not spinlocked, pv is held+locked.
	 * Remove the pv's page from its parent's page table.  The
	 * parent's page table page's wire_count will be decremented.
	 *
	 * This will clean out the pte at any level of the page table.
	 * If smp != 0 all cpus are affected.
	 *
	 * Do not tear-down recursively, its faster to just let the
	 * release run its course.
	 */
	pmap_remove_pv_pte(pv, pvp, bulk, 0);

	/*
	 * Terminal pvs are unhooked from their vm_pages.  Because
	 * terminal pages aren't page table pages they aren't wired
	 * by us, so we have to be sure not to unwire them either.
	 *
	 * XXX It is unclear if this code ever gets called because we
	 *     no longer use pv's to track terminal pages.
	 */
	if (pv->pv_pindex < pmap_pt_pindex(0)) {
		pmap_remove_pv_page(pv, 0);
		goto skip;
	}

	/*
	 * We leave the top-level page table page cached, wired, and
	 * mapped in the pmap until the dtor function (pmap_puninit())
	 * gets called.
	 *
	 * Since we are leaving the top-level pv intact we need
	 * to break out of what would otherwise be an infinite loop.
	 *
	 * This covers both the normal and the isolated PML4 page.
	 */
	if (pv->pv_pindex >= pmap_pml4_pindex()) {
		pv_put(pv);
		return(-1);
	}

	/*
	 * For page table pages (other than the top-level page),
	 * remove and free the vm_page.  The representitive mapping
	 * removed above by pmap_remove_pv_pte() did not undo the
	 * last wire_count so we have to do that as well.
	 */
	p = pmap_remove_pv_page(pv, 1);
	vm_page_busy_wait(p, FALSE, "pmaprl");
	if (p->wire_count != 1) {
		const char *tstr;

		if (pv->pv_pindex >= pmap_pdp_pindex(0))
			tstr = "PDP";
		else if (pv->pv_pindex >= pmap_pd_pindex(0))
			tstr = "PD";
		else if (pv->pv_pindex >= pmap_pt_pindex(0))
			tstr = "PT";
		else
			tstr = "PTE";

		kprintf("p(%s) p->wire_count was %016lx %d\n",
			tstr, pv->pv_pindex, p->wire_count);
	}
	KKASSERT(p->wire_count == 1);
	KKASSERT(p->flags & PG_UNQUEUED);

	vm_page_unwire(p, 0);
	KKASSERT(p->wire_count == 0);

	vm_page_free(p);
skip:
	pv_free(pv, pvp);

	return 0;
}

/*
 * This function will remove the pte associated with a pv from its parent.
 * Terminal pv's are supported.  All cpus specified by (bulk) are properly
 * invalidated.
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
 * NOTE: The pte being removed might be unmanaged, and the pv supplied might
 *	 be freshly allocated and not imply that the pte is managed.  In this
 *	 case pv->pv_m should be NULL.
 *
 * The pv must be locked.  The pvp, if supplied, must be locked.  All
 * supplied pv's will remain locked on return.
 *
 * XXX must lock parent pv's if they exist to remove pte XXX
 */
static
void
pmap_remove_pv_pte(pv_entry_t pv, pv_entry_t pvp, pmap_inval_bulk_t *bulk,
		   int destroy)
{
	vm_pindex_t ptepindex = pv->pv_pindex;
	pmap_t pmap = pv->pv_pmap;
	vm_page_t p;
	int gotpvp = 0;

	KKASSERT(pmap);

	if (ptepindex >= pmap_pml4_pindex()) {
		/*
		 * We are the top level PML4E table, there is no parent.
		 *
		 * This is either the normal or isolated PML4E table.
		 * Only the normal is used in regular operation, the isolated
		 * is only passed in when breaking down the whole pmap.
		 */
		p = pmap->pm_pmlpv->pv_m;
		KKASSERT(pv->pv_m == p);	/* debugging */
	} else if (ptepindex >= pmap_pdp_pindex(0)) {
		/*
		 * Remove a PDP page from the PML4E.  This can only occur
		 * with user page tables.  We do not have to lock the
		 * pml4 PV so just ignore pvp.
		 */
		vm_pindex_t pml4_pindex;
		vm_pindex_t pdp_index;
		pml4_entry_t *pdp;
		pml4_entry_t *pdp_iso;

		pdp_index = ptepindex - pmap_pdp_pindex(0);
		if (pvp == NULL) {
			pml4_pindex = pmap_pml4_pindex();
			pvp = pv_get(pv->pv_pmap, pml4_pindex, NULL);
			KKASSERT(pvp);
			gotpvp = 1;
		}

		pdp = &pmap->pm_pml4[pdp_index & ((1ul << NPML4EPGSHIFT) - 1)];
		KKASSERT((*pdp & pmap->pmap_bits[PG_V_IDX]) != 0);
		p = PHYS_TO_VM_PAGE(*pdp & PG_FRAME);
		pmap_inval_bulk(bulk, (vm_offset_t)-1, pdp, 0);

		/*
		 * Also remove the PDP from the isolated PML4E if the
		 * process uses one.
		 */
		if (pvp == pmap->pm_pmlpv && pmap->pm_pmlpv_iso) {
			pdp_iso = &pmap->pm_pml4_iso[pdp_index &
						((1ul << NPML4EPGSHIFT) - 1)];
			pmap_inval_bulk(bulk, (vm_offset_t)-1, pdp_iso, 0);
		}
		KKASSERT(pv->pv_m == p);	/* debugging */
	} else if (ptepindex >= pmap_pd_pindex(0)) {
		/*
		 * Remove a PD page from the PDP
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
			pvp = pv_get(pv->pv_pmap, pdp_pindex, NULL);
			gotpvp = 1;
		}

		if (pvp) {
			pd = pv_pte_lookup(pvp, pd_index &
						((1ul << NPDPEPGSHIFT) - 1));
			KKASSERT((*pd & pmap->pmap_bits[PG_V_IDX]) != 0);
			p = PHYS_TO_VM_PAGE(*pd & PG_FRAME);
			pmap_inval_bulk(bulk, (vm_offset_t)-1, pd, 0);
		} else {
			KKASSERT(pmap->pm_flags & PMAP_FLAG_SIMPLE);
			p = pv->pv_m;		/* degenerate test later */
		}
		KKASSERT(pv->pv_m == p);	/* debugging */
	} else if (ptepindex >= pmap_pt_pindex(0)) {
		/*
		 *  Remove a PT page from the PD
		 */
		vm_pindex_t pd_pindex;
		vm_pindex_t pt_index;
		pd_entry_t *pt;

		pt_index = ptepindex - pmap_pt_pindex(0);

		if (pvp == NULL) {
			pd_pindex = NUPTE_TOTAL + NUPT_TOTAL +
				    (pt_index >> NPDPEPGSHIFT);
			pvp = pv_get(pv->pv_pmap, pd_pindex, NULL);
			KKASSERT(pvp);
			gotpvp = 1;
		}

		pt = pv_pte_lookup(pvp, pt_index & ((1ul << NPDPEPGSHIFT) - 1));
#if 0
		KASSERT((*pt & pmap->pmap_bits[PG_V_IDX]) != 0,
			("*pt unexpectedly invalid %016jx "
			 "gotpvp=%d ptepindex=%ld ptindex=%ld pv=%p pvp=%p",
			*pt, gotpvp, ptepindex, pt_index, pv, pvp));
		p = PHYS_TO_VM_PAGE(*pt & PG_FRAME);
#else
		if ((*pt & pmap->pmap_bits[PG_V_IDX]) == 0) {
			kprintf("*pt unexpectedly invalid %016jx "
			        "gotpvp=%d ptepindex=%ld ptindex=%ld "
				"pv=%p pvp=%p\n",
				*pt, gotpvp, ptepindex, pt_index, pv, pvp);
			tsleep(pt, 0, "DEAD", 0);
			p = pv->pv_m;
		} else {
			p = PHYS_TO_VM_PAGE(*pt & PG_FRAME);
		}
#endif
		pmap_inval_bulk(bulk, (vm_offset_t)-1, pt, 0);
		KKASSERT(pv->pv_m == p);	/* debugging */
	} else {
		KKASSERT(0);
	}

	/*
	 * If requested, scrap the underlying pv->pv_m and the underlying
	 * pv.  If this is a page-table-page we must also free the page.
	 *
	 * pvp must be returned locked.
	 */
	if (destroy == 1) {
		/*
		 * page table page (PT, PD, PDP, PML4), caller was responsible
		 * for testing wired_count.
		 */
		KKASSERT(pv->pv_m->wire_count == 1);
		p = pmap_remove_pv_page(pv, 1);
		pv_free(pv, pvp);
		pv = NULL;

		vm_page_busy_wait(p, FALSE, "pgpun");
		vm_page_unwire(p, 0);
		vm_page_flag_clear(p, PG_MAPPED | PG_WRITEABLE);
		vm_page_free(p);
	}

	/*
	 * If we acquired pvp ourselves then we are responsible for
	 * recursively deleting it.
	 */
	if (pvp && gotpvp) {
		/*
		 * Recursively destroy higher-level page tables.
		 *
		 * This is optional.  If we do not, they will still
		 * be destroyed when the process exits.
		 *
		 * NOTE: Do not destroy pv_entry's with extra hold refs,
		 *	 a caller may have unlocked it and intends to
		 *	 continue to use it.
		 */
		if (pmap_dynamic_delete &&
		    pvp->pv_m &&
		    pvp->pv_m->wire_count == 1 &&
		    (pvp->pv_hold & PV_HOLD_MASK) == 2 &&
		    pvp->pv_pindex < pmap_pml4_pindex()) {
			if (pmap != &kernel_pmap) {
				pmap_remove_pv_pte(pvp, NULL, bulk, 1);
				pvp = NULL;	/* safety */
			} else {
				kprintf("Attempt to remove kernel_pmap pindex "
					"%jd\n", pvp->pv_pindex);
				pv_put(pvp);
			}
		} else {
			pv_put(pvp);
		}
	}
}

/*
 * Remove the vm_page association to a pv.  The pv must be locked.
 */
static
vm_page_t
pmap_remove_pv_page(pv_entry_t pv, int clrpgbits)
{
	vm_page_t m;

	m = pv->pv_m;
	pv->pv_m = NULL;
	if (clrpgbits)
		vm_page_flag_clear(m, PG_MAPPED | PG_WRITEABLE);

	return(m);
}

/*
 * Grow the number of kernel page table entries, if needed.
 *
 * This routine is always called to validate any address space
 * beyond KERNBASE (for kldloads).  kernel_vm_end only governs the address
 * space below KERNBASE.
 *
 * kernel_map must be locked exclusively by the caller.
 */
void
pmap_growkernel(vm_offset_t kstart, vm_offset_t kend)
{
	vm_paddr_t paddr;
	vm_offset_t ptppaddr;
	vm_page_t nkpg;
	pd_entry_t *pt, newpt;
	pdp_entry_t *pd, newpd;
	int update_kernel_vm_end;

	/*
	 * bootstrap kernel_vm_end on first real VM use
	 */
	if (kernel_vm_end == 0) {
		kernel_vm_end = VM_MIN_KERNEL_ADDRESS;

		for (;;) {
			pt = pmap_pt(&kernel_pmap, kernel_vm_end);
			if (pt == NULL)
				break;
			if ((*pt & kernel_pmap.pmap_bits[PG_V_IDX]) == 0)
				break;
			kernel_vm_end = (kernel_vm_end + PAGE_SIZE * NPTEPG) &
					~(vm_offset_t)(PAGE_SIZE * NPTEPG - 1);
			if (kernel_vm_end - 1 >= vm_map_max(&kernel_map)) {
				kernel_vm_end = vm_map_max(&kernel_map);
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

	kstart = rounddown2(kstart, (vm_offset_t)(PAGE_SIZE * NPTEPG));
	kend = roundup2(kend, (vm_offset_t)(PAGE_SIZE * NPTEPG));

	if (kend - 1 >= vm_map_max(&kernel_map))
		kend = vm_map_max(&kernel_map);

	while (kstart < kend) {
		pt = pmap_pt(&kernel_pmap, kstart);
		if (pt == NULL) {
			/*
			 * We need a new PD entry
			 */
			nkpg = vm_page_alloc(NULL, mycpu->gd_rand_incr++,
			                     VM_ALLOC_NORMAL |
					     VM_ALLOC_SYSTEM |
					     VM_ALLOC_INTERRUPT);
			if (nkpg == NULL) {
				panic("pmap_growkernel: no memory to grow "
				      "kernel");
			}
			paddr = VM_PAGE_TO_PHYS(nkpg);
			pmap_zero_page(paddr);
			pd = pmap_pd(&kernel_pmap, kstart);

			newpd = (pdp_entry_t)
			    (paddr |
			    kernel_pmap.pmap_bits[PG_V_IDX] |
			    kernel_pmap.pmap_bits[PG_RW_IDX] |
			    kernel_pmap.pmap_bits[PG_A_IDX]);
			atomic_swap_long(pd, newpd);

#if 0
			kprintf("NEWPD pd=%p pde=%016jx phys=%016jx\n",
				pd, newpd, paddr);
#endif

			continue; /* try again */
		}

		if ((*pt & kernel_pmap.pmap_bits[PG_V_IDX]) != 0) {
			kstart = (kstart + PAGE_SIZE * NPTEPG) &
				 ~(vm_offset_t)(PAGE_SIZE * NPTEPG - 1);
			if (kstart - 1 >= vm_map_max(&kernel_map)) {
				kstart = vm_map_max(&kernel_map);
				break;                       
			}
			continue;
		}

		/*
		 * We need a new PT
		 *
		 * This index is bogus, but out of the way
		 */
		nkpg = vm_page_alloc(NULL, mycpu->gd_rand_incr++,
				     VM_ALLOC_NORMAL |
				     VM_ALLOC_SYSTEM |
				     VM_ALLOC_INTERRUPT);
		if (nkpg == NULL)
			panic("pmap_growkernel: no memory to grow kernel");

		vm_page_wire(nkpg);
		ptppaddr = VM_PAGE_TO_PHYS(nkpg);
		pmap_zero_page(ptppaddr);
		newpt = (pd_entry_t)(ptppaddr |
				     kernel_pmap.pmap_bits[PG_V_IDX] |
				     kernel_pmap.pmap_bits[PG_RW_IDX] |
				     kernel_pmap.pmap_bits[PG_A_IDX]);
		atomic_swap_long(pt, newpt);

		kstart = (kstart + PAGE_SIZE * NPTEPG) &
			  ~(vm_offset_t)(PAGE_SIZE * NPTEPG - 1);

		if (kstart - 1 >= vm_map_max(&kernel_map)) {
			kstart = vm_map_max(&kernel_map);
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
	if (pmap != NULL)
		atomic_add_int(&pmap->pm_count, 1);
}

void
pmap_maybethreaded(pmap_t pmap)
{
	atomic_set_int(&pmap->pm_flags, PMAP_MULTI);
}

/*
 * Called while page is hard-busied to clear the PG_MAPPED and PG_WRITEABLE
 * flags if able.  This can happen when the pmap code is unable to clear
 * the bits in prior actions due to not holding the page hard-busied at
 * the time.
 *
 * The clearing of PG_MAPPED/WRITEABLE is an optional optimization done
 * when the pte is removed and only if the pte has not been multiply-mapped.
 * The caller may have to call vm_page_protect() if the bits are still set
 * here.
 *
 * This function is expected to be quick.
 */
int
pmap_mapped_sync(vm_page_t m)
{
	return (m->flags);
}

/***************************************************
 * page management routines.
 ***************************************************/

/*
 * Hold a pv without locking it
 */
#if 0
static void
pv_hold(pv_entry_t pv)
{
	atomic_add_int(&pv->pv_hold, 1);
}
#endif

/*
 * Hold a pv_entry, preventing its destruction.  TRUE is returned if the pv
 * was successfully locked, FALSE if it wasn't.  The caller must dispose of
 * the pv properly.
 *
 * Either the pmap->pm_spin or the related vm_page_spin (if traversing a
 * pv list via its page) must be held by the caller in order to stabilize
 * the pv.
 */
static int
_pv_hold_try(pv_entry_t pv PMAP_DEBUG_DECL)
{
	u_int count;

	/*
	 * Critical path shortcut expects pv to already have one ref
	 * (for the pv->pv_pmap).
	 */
	count = pv->pv_hold;
	cpu_ccfence();
	for (;;) {
		if ((count & PV_HOLD_LOCKED) == 0) {
			if (atomic_fcmpset_int(&pv->pv_hold, &count,
					      (count + 1) | PV_HOLD_LOCKED)) {
#ifdef PMAP_DEBUG
				pv->pv_func = func;
				pv->pv_line = lineno;
#endif
				return TRUE;
			}
		} else {
			if (atomic_fcmpset_int(&pv->pv_hold, &count, count + 1))
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

	for (;;) {
		count = pv->pv_hold;
		cpu_ccfence();
		KKASSERT((count & PV_HOLD_MASK) > 0);
		KKASSERT((count & (PV_HOLD_LOCKED | PV_HOLD_MASK)) !=
			 (PV_HOLD_LOCKED | 1));
		if (atomic_cmpset_int(&pv->pv_hold, count, count - 1)) {
			if ((count & PV_HOLD_MASK) == 1) {
#ifdef PMAP_DEBUG2
				if (pmap_enter_debug > 0) {
					--pmap_enter_debug;
					kprintf("pv_drop: free pv %p\n", pv);
				}
#endif
				KKASSERT(count == 1);
				KKASSERT(pv->pv_pmap == NULL);
				zfree(pvzone, pv);
			}
			return;
		}
		/* retry */
	}
}

/*
 * Find or allocate the requested PV entry, returning a locked, held pv.
 *
 * If (*isnew) is non-zero, the returned pv will have two hold counts, one
 * for the caller and one representing the pmap and vm_page association.
 *
 * If (*isnew) is zero, the returned pv will have only one hold count.
 *
 * Since both associations can only be adjusted while the pv is locked,
 * together they represent just one additional hold.
 */
static
pv_entry_t
_pv_alloc(pmap_t pmap, vm_pindex_t pindex, int *isnew PMAP_DEBUG_DECL)
{
	struct mdglobaldata *md = mdcpu;
	pv_entry_t pv;
	pv_entry_t pnew;
	int pmap_excl = 0;

	pnew = NULL;
	if (md->gd_newpv) {
#if 1
		pnew = atomic_swap_ptr((void *)&md->gd_newpv, NULL);
#else
		crit_enter();
		pnew = md->gd_newpv;	/* might race NULL */
		md->gd_newpv = NULL;
		crit_exit();
#endif
	}
	if (pnew == NULL)
		pnew = zalloc(pvzone);

	spin_lock_shared(&pmap->pm_spin);
	for (;;) {
		/*
		 * Shortcut cache
		 */
		pv = pv_entry_lookup(pmap, pindex);
		if (pv == NULL) {
			vm_pindex_t *pmark;

			/*
			 * Requires exclusive pmap spinlock
			 */
			if (pmap_excl == 0) {
				pmap_excl = 1;
				if (!spin_lock_upgrade_try(&pmap->pm_spin)) {
					spin_unlock_shared(&pmap->pm_spin);
					spin_lock(&pmap->pm_spin);
					continue;
				}
			}

			/*
			 * We need to block if someone is holding our
			 * placemarker.  As long as we determine the
			 * placemarker has not been aquired we do not
			 * need to get it as acquision also requires
			 * the pmap spin lock.
			 *
			 * However, we can race the wakeup.
			 */
			pmark = pmap_placemarker_hash(pmap, pindex);

			if (((*pmark ^ pindex) & ~PM_PLACEMARK_WAKEUP) == 0) {
				tsleep_interlock(pmark, 0);
				atomic_set_long(pmark, PM_PLACEMARK_WAKEUP);
				if (((*pmark ^ pindex) &
				     ~PM_PLACEMARK_WAKEUP) == 0) {
					spin_unlock(&pmap->pm_spin);
					tsleep(pmark, PINTERLOCKED, "pvplc", 0);
					spin_lock(&pmap->pm_spin);
				}
				continue;
			}

			/*
			 * Setup the new entry
			 */
			pnew->pv_pmap = pmap;
			pnew->pv_pindex = pindex;
			pnew->pv_hold = PV_HOLD_LOCKED | 2;
			pnew->pv_flags = 0;
#ifdef PMAP_DEBUG
			pnew->pv_func = func;
			pnew->pv_line = lineno;
			if (pnew->pv_line_lastfree > 0) {
				pnew->pv_line_lastfree =
						-pnew->pv_line_lastfree;
			}
#endif
			pv = pv_entry_rb_tree_RB_INSERT(&pmap->pm_pvroot, pnew);
			atomic_add_long(&pmap->pm_stats.resident_count, 1);
			spin_unlock(&pmap->pm_spin);
			*isnew = 1;

			KASSERT(pv == NULL, ("pv insert failed %p->%p", pnew, pv));
			return(pnew);
		}

		/*
		 * We already have an entry, cleanup the staged pnew if
		 * we can get the lock, otherwise block and retry.
		 */
		if (__predict_true(_pv_hold_try(pv PMAP_DEBUG_COPY))) {
			if (pmap_excl)
				spin_unlock(&pmap->pm_spin);
			else
				spin_unlock_shared(&pmap->pm_spin);
#if 1
			pnew = atomic_swap_ptr((void *)&md->gd_newpv, pnew);
			if (pnew)
				zfree(pvzone, pnew);
#else
			crit_enter();
			if (md->gd_newpv == NULL)
				md->gd_newpv = pnew;
			else
				zfree(pvzone, pnew);
			crit_exit();
#endif
			KKASSERT(pv->pv_pmap == pmap &&
				 pv->pv_pindex == pindex);
			*isnew = 0;
			return(pv);
		}
		if (pmap_excl) {
			spin_unlock(&pmap->pm_spin);
			_pv_lock(pv PMAP_DEBUG_COPY);
			pv_put(pv);
			spin_lock(&pmap->pm_spin);
		} else {
			spin_unlock_shared(&pmap->pm_spin);
			_pv_lock(pv PMAP_DEBUG_COPY);
			pv_put(pv);
			spin_lock_shared(&pmap->pm_spin);
		}
	}
	/* NOT REACHED */
}

/*
 * Find the requested PV entry, returning a locked+held pv or NULL
 */
static
pv_entry_t
_pv_get(pmap_t pmap, vm_pindex_t pindex, vm_pindex_t **pmarkp PMAP_DEBUG_DECL)
{
	pv_entry_t pv;
	int pmap_excl = 0;

	spin_lock_shared(&pmap->pm_spin);
	for (;;) {
		/*
		 * Shortcut cache
		 */
		pv = pv_entry_lookup(pmap, pindex);
		if (pv == NULL) {
			/*
			 * Block if there is ANY placemarker.  If we are to
			 * return it, we must also aquire the spot, so we
			 * have to block even if the placemarker is held on
			 * a different address.
			 *
			 * OPTIMIZATION: If pmarkp is passed as NULL the
			 * caller is just probing (or looking for a real
			 * pv_entry), and in this case we only need to check
			 * to see if the placemarker matches pindex.
			 */
			vm_pindex_t *pmark;

			/*
			 * Requires exclusive pmap spinlock
			 */
			if (pmap_excl == 0) {
				pmap_excl = 1;
				if (!spin_lock_upgrade_try(&pmap->pm_spin)) {
					spin_unlock_shared(&pmap->pm_spin);
					spin_lock(&pmap->pm_spin);
					continue;
				}
			}

			pmark = pmap_placemarker_hash(pmap, pindex);

			if ((pmarkp && *pmark != PM_NOPLACEMARK) ||
			    ((*pmark ^ pindex) & ~PM_PLACEMARK_WAKEUP) == 0) {
				tsleep_interlock(pmark, 0);
				atomic_set_long(pmark, PM_PLACEMARK_WAKEUP);
				if ((pmarkp && *pmark != PM_NOPLACEMARK) ||
				    ((*pmark ^ pindex) &
				     ~PM_PLACEMARK_WAKEUP) == 0) {
					spin_unlock(&pmap->pm_spin);
					tsleep(pmark, PINTERLOCKED, "pvpld", 0);
					spin_lock(&pmap->pm_spin);
				}
				continue;
			}
			if (pmarkp) {
				if (atomic_swap_long(pmark, pindex) !=
				    PM_NOPLACEMARK) {
					panic("_pv_get: pmark race");
				}
				*pmarkp = pmark;
			}
			spin_unlock(&pmap->pm_spin);
			return NULL;
		}
		if (_pv_hold_try(pv PMAP_DEBUG_COPY)) {
			if (pmap_excl)
				spin_unlock(&pmap->pm_spin);
			else
				spin_unlock_shared(&pmap->pm_spin);
			KKASSERT(pv->pv_pmap == pmap &&
				 pv->pv_pindex == pindex);
			return(pv);
		}
		if (pmap_excl) {
			spin_unlock(&pmap->pm_spin);
			_pv_lock(pv PMAP_DEBUG_COPY);
			pv_put(pv);
			spin_lock(&pmap->pm_spin);
		} else {
			spin_unlock_shared(&pmap->pm_spin);
			_pv_lock(pv PMAP_DEBUG_COPY);
			pv_put(pv);
			spin_lock_shared(&pmap->pm_spin);
		}
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
 *
 * If the entry is placemarked by someone else NULL is returned and *errorp
 * is set to 1.
 */
static
pv_entry_t
pv_get_try(pmap_t pmap, vm_pindex_t pindex, vm_pindex_t **pmarkp, int *errorp)
{
	pv_entry_t pv;

	spin_lock_shared(&pmap->pm_spin);

	pv = pv_entry_lookup(pmap, pindex);
	if (pv == NULL) {
		vm_pindex_t *pmark;

		pmark = pmap_placemarker_hash(pmap, pindex);

		if (((*pmark ^ pindex) & ~PM_PLACEMARK_WAKEUP) == 0) {
			*errorp = 1;
		} else if (pmarkp &&
			   atomic_cmpset_long(pmark, PM_NOPLACEMARK, pindex)) {
			*errorp = 0;
		} else {
			/*
			 * Can't set a placemark with a NULL pmarkp, or if
			 * pmarkp is non-NULL but we failed to set our
			 * placemark.
			 */
			*errorp = 1;
		}
		if (pmarkp)
			*pmarkp = pmark;
		spin_unlock_shared(&pmap->pm_spin);

		return NULL;
	}

	/*
	 * XXX This has problems if the lock is shared, why?
	 */
	if (pv_hold_try(pv)) {
		spin_unlock_shared(&pmap->pm_spin);
		*errorp = 0;
		KKASSERT(pv->pv_pmap == pmap && pv->pv_pindex == pindex);
		return(pv);	/* lock succeeded */
	}
	spin_unlock_shared(&pmap->pm_spin);
	*errorp = 1;

	return (pv);		/* lock failed */
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
#ifdef PMAP_DEBUG2
			if (pmap_enter_debug > 0) {
				--pmap_enter_debug;
				kprintf("pv waiting on %s:%d\n",
					pv->pv_func, pv->pv_line);
			}
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

	for (;;) {
		count = pv->pv_hold;
		cpu_ccfence();
		KKASSERT((count & (PV_HOLD_LOCKED | PV_HOLD_MASK)) >=
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
#ifdef PMAP_DEBUG2
	if (pmap_enter_debug > 0) {
		--pmap_enter_debug;
		kprintf("pv_put pv=%p hold=%08x\n", pv, pv->pv_hold);
	}
#endif

	/*
	 * Normal put-aways must have a pv_m associated with the pv,
	 * but allow the case where the pv has been destructed due
	 * to pmap_dynamic_delete.
	 */
	KKASSERT(pv->pv_pmap == NULL || pv->pv_m != NULL);

	/*
	 * Fast - shortcut most common condition
	 */
	if (atomic_cmpset_int(&pv->pv_hold, PV_HOLD_LOCKED | 2, 1))
		return;

	/*
	 * Slow
	 */
	pv_unlock(pv);
	pv_drop(pv);
}

/*
 * Remove the pmap association from a pv, require that pv_m already be removed,
 * then unlock and drop the pv.  Any pte operations must have already been
 * completed.  This call may result in a last-drop which will physically free
 * the pv.
 *
 * Removing the pmap association entails an additional drop.
 *
 * pv must be exclusively locked on call and will be disposed of on return.
 */
static
void
_pv_free(pv_entry_t pv, pv_entry_t pvp PMAP_DEBUG_DECL)
{
	pmap_t pmap;

#ifdef PMAP_DEBUG
	pv->pv_func_lastfree = func;
	pv->pv_line_lastfree = lineno;
#endif
	KKASSERT(pv->pv_m == NULL);
	KKASSERT((pv->pv_hold & (PV_HOLD_LOCKED|PV_HOLD_MASK)) >=
		  (PV_HOLD_LOCKED|1));
	if ((pmap = pv->pv_pmap) != NULL) {
		spin_lock(&pmap->pm_spin);
		KKASSERT(pv->pv_pmap == pmap);
		if (pmap->pm_pvhint_pt == pv)
			pmap->pm_pvhint_pt = NULL;
		if (pmap->pm_pvhint_unused == pv)
			pmap->pm_pvhint_unused = NULL;
		pv_entry_rb_tree_RB_REMOVE(&pmap->pm_pvroot, pv);
		atomic_add_long(&pmap->pm_stats.resident_count, -1);
		pv->pv_pmap = NULL;
		pv->pv_pindex = 0;
		spin_unlock(&pmap->pm_spin);

		/*
		 * Try to shortcut three atomic ops, otherwise fall through
		 * and do it normally.  Drop two refs and the lock all in
		 * one go.
		 */
		if (pvp) {
			if (vm_page_unwire_quick(pvp->pv_m))
				panic("_pv_free: bad wirecount on pvp");
		}
		if (atomic_cmpset_int(&pv->pv_hold, PV_HOLD_LOCKED | 2, 0)) {
#ifdef PMAP_DEBUG2
			if (pmap_enter_debug > 0) {
				--pmap_enter_debug;
				kprintf("pv_free: free pv %p\n", pv);
			}
#endif
			zfree(pvzone, pv);
			return;
		}
		pv_drop(pv);	/* ref for pv_pmap */
	}
	pv_unlock(pv);
	pv_drop(pv);
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
		kprintf("pmap_collect: pv_entries exhausted -- "
			"suggest increasing vm.pmap_pv_entries above %ld\n",
			vm_pmap_pv_entries);
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
		     vm_pindex_t *, pv_entry_t, vm_offset_t,
		     pt_entry_t *, void *);
	void *arg;
	pmap_inval_bulk_t bulk_core;
	pmap_inval_bulk_t *bulk;
	int count;
	int stop;
};

static int pmap_scan_cmp(pv_entry_t pv, void *data);
static int pmap_scan_callback(pv_entry_t pv, void *data);

static void
pmap_scan(struct pmap_scan_info *info, int smp_inval)
{
	struct pmap *pmap = info->pmap;
	pv_entry_t pt_pv;	/* A page table PV */
	pv_entry_t pte_pv;	/* A page table entry PV */
	vm_pindex_t *pte_placemark;
	vm_pindex_t *pt_placemark;
	pt_entry_t *ptep;
	pt_entry_t oldpte;
	struct pv_entry dummy_pv;

	info->stop = 0;
	if (pmap == NULL)
		return;
	if (info->sva == info->eva)
		return;
	if (smp_inval) {
		info->bulk = &info->bulk_core;
		pmap_inval_bulk_init(&info->bulk_core, pmap);
	} else {
		info->bulk = NULL;
	}

	/*
	 * Hold the token for stability; if the pmap is empty we have nothing
	 * to do.
	 */
#if 0
	if (pmap->pm_stats.resident_count == 0) {
		return;
	}
#endif

	info->count = 0;

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
			pte_pv = pv_get(pmap, pmap_pte_pindex(info->sva),
					&pte_placemark);
			KKASSERT(pte_pv == NULL);
			ptep = vtopte(info->sva);
		} else {
			/*
			 * We hold pte_placemark across the operation for
			 * unmanaged pages.
			 *
			 * WARNING!  We must hold pt_placemark across the
			 *	     *ptep test to prevent misintepreting
			 *	     a non-zero *ptep as a shared page
			 *	     table page.  Hold it across the function
			 *	     callback as well for SMP safety.
			 */
			pte_pv = pv_get(pmap, pmap_pte_pindex(info->sva),
					&pte_placemark);
			KKASSERT(pte_pv == NULL);
			pt_pv = pv_get(pmap, pmap_pt_pindex(info->sva),
				       &pt_placemark);
			if (pt_pv == NULL) {
#if 0
				KKASSERT(0);
				pd_pv = pv_get(pmap,
					       pmap_pd_pindex(info->sva),
					       NULL);
				if (pd_pv) {
					ptep = pv_pte_lookup(pd_pv,
						    pmap_pt_index(info->sva));
					if (*ptep) {
						info->func(pmap, info,
						     pt_placemark, pd_pv,
						     info->sva, ptep,
						     info->arg);
					} else {
						pv_placemarker_wakeup(pmap,
								  pt_placemark);
					}
					pv_put(pd_pv);
				} else {
					pv_placemarker_wakeup(pmap,
							      pt_placemark);
				}
#else
				pv_placemarker_wakeup(pmap, pt_placemark);
#endif
				pv_placemarker_wakeup(pmap, pte_placemark);
				goto fast_skip;
			}
			ptep = pv_pte_lookup(pt_pv, pmap_pte_index(info->sva));
		}

		/*
		 * NOTE: *ptep can't be ripped out from under us if we hold
		 *	 pte_pv (or pte_placemark) locked, but bits can
		 *	 change.
		 */
		oldpte = *ptep;
		cpu_ccfence();
		if (oldpte == 0) {
			KKASSERT(pte_pv == NULL);
			pv_placemarker_wakeup(pmap, pte_placemark);
		} else {
			KASSERT((oldpte & pmap->pmap_bits[PG_V_IDX]) ==
				pmap->pmap_bits[PG_V_IDX],
			    ("badB *ptep %016lx/%016lx sva %016lx pte_pv NULL",
			    *ptep, oldpte, info->sva));
			info->func(pmap, info, pte_placemark, pt_pv,
				   info->sva, ptep, info->arg);
		}
		if (pt_pv)
			pv_put(pt_pv);
fast_skip:
		pmap_inval_bulk_flush(info->bulk);
		return;
	}

	/*
	 * Nominal scan case, RB_SCAN() for PD pages and iterate from
	 * there.
	 *
	 * WARNING! eva can overflow our standard ((N + mask) >> bits)
	 *	    bounds, resulting in a pd_pindex of 0.  To solve the
	 *	    problem we use an inclusive range.
	 */
	info->sva_pd_pindex = pmap_pd_pindex(info->sva);
	info->eva_pd_pindex = pmap_pd_pindex(info->eva - PAGE_SIZE);

	if (info->sva >= VM_MAX_USER_ADDRESS) {
		/*
		 * The kernel does not currently maintain any pv_entry's for
		 * higher-level page tables.
		 */
		bzero(&dummy_pv, sizeof(dummy_pv));
		dummy_pv.pv_pindex = info->sva_pd_pindex;
		spin_lock(&pmap->pm_spin);
		while (dummy_pv.pv_pindex <= info->eva_pd_pindex) {
			pmap_scan_callback(&dummy_pv, info);
			++dummy_pv.pv_pindex;
			if (dummy_pv.pv_pindex < info->sva_pd_pindex) /*wrap*/
				break;
		}
		spin_unlock(&pmap->pm_spin);
	} else {
		/*
		 * User page tables maintain local PML4, PDP, PD, and PT
		 * pv_entry's.  pv_entry's are not used for PTEs.
		 */
		spin_lock(&pmap->pm_spin);
		pv_entry_rb_tree_RB_SCAN(&pmap->pm_pvroot, pmap_scan_cmp,
					 pmap_scan_callback, info);
		spin_unlock(&pmap->pm_spin);
	}
	pmap_inval_bulk_flush(info->bulk);
}

/*
 * WARNING! pmap->pm_spin held
 *
 * WARNING! eva can overflow our standard ((N + mask) >> bits)
 *	    bounds, resulting in a pd_pindex of 0.  To solve the
 *	    problem we use an inclusive range.
 */
static int
pmap_scan_cmp(pv_entry_t pv, void *data)
{
	struct pmap_scan_info *info = data;
	if (pv->pv_pindex < info->sva_pd_pindex)
		return(-1);
	if (pv->pv_pindex > info->eva_pd_pindex)
		return(1);
	return(0);
}

/*
 * pmap_scan() by PDs
 *
 * WARNING! pmap->pm_spin held
 */
static int
pmap_scan_callback(pv_entry_t pv, void *data)
{
	struct pmap_scan_info *info = data;
	struct pmap *pmap = info->pmap;
	pv_entry_t pd_pv;	/* A page directory PV */
	pv_entry_t pt_pv;	/* A page table PV */
	vm_pindex_t *pt_placemark;
	pt_entry_t *ptep;
	pt_entry_t oldpte;
	vm_offset_t sva;
	vm_offset_t eva;
	vm_offset_t va_next;
	vm_pindex_t pd_pindex;
	int error;

	/*
	 * Stop if requested
	 */
	if (info->stop)
		return -1;

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
	sva = (pd_pindex - pmap_pd_pindex(0)) << PDPSHIFT;
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
		if (info->stop)
			break;
		if (sva >= VM_MAX_USER_ADDRESS) {
			if (pt_pv) {
				pv_put(pt_pv);
				pt_pv = NULL;
			}
			goto kernel_skip;
		}

		/*
		 * PD cache, scan shortcut if it doesn't exist.
		 */
		if (pd_pv == NULL) {
			pd_pv = pv_get(pmap, pmap_pd_pindex(sva), NULL);
		} else if (pd_pv->pv_pmap != pmap ||
			   pd_pv->pv_pindex != pmap_pd_pindex(sva)) {
			pv_put(pd_pv);
			pd_pv = pv_get(pmap, pmap_pd_pindex(sva), NULL);
		}
		if (pd_pv == NULL) {
			va_next = (sva + NBPDP) & ~PDPMASK;
			if (va_next < sva)
				va_next = eva;
			continue;
		}

		/*
		 * PT cache
		 *
		 * NOTE: The cached pt_pv can be removed from the pmap when
		 *	 pmap_dynamic_delete is enabled.
		 */
		if (pt_pv && (pt_pv->pv_pmap != pmap ||
			      pt_pv->pv_pindex != pmap_pt_pindex(sva))) {
			pv_put(pt_pv);
			pt_pv = NULL;
		}
		if (pt_pv == NULL) {
			pt_pv = pv_get_try(pmap, pmap_pt_pindex(sva),
					   &pt_placemark, &error);
			if (error) {
				pv_put(pd_pv);	/* lock order */
				pd_pv = NULL;
				if (pt_pv) {
					pv_lock(pt_pv);
					pv_put(pt_pv);
					pt_pv = NULL;
				} else {
					pv_placemarker_wait(pmap, pt_placemark);
				}
				va_next = sva;
				continue;
			}
			/* may have to re-check later if pt_pv is NULL here */
		}

		/*
		 * If pt_pv is NULL we either have a shared page table
		 * page (NOT IMPLEMENTED XXX) and must issue a callback
		 * specific to that case, or there is no page table page.
		 *
		 * Either way we can skip the page table page.
		 *
		 * WARNING! pt_pv can also be NULL due to a pv creation
		 *	    race where we find it to be NULL and then
		 *	    later see a pte_pv.  But its possible the pt_pv
		 *	    got created inbetween the two operations, so
		 *	    we must check.
		 *
		 *	    XXX This should no longer be the case because
		 *	    we have pt_placemark.
		 */
		if (pt_pv == NULL) {
#if 0
			/* XXX REMOVED */
			/*
			 * Possible unmanaged (shared from another pmap)
			 * page table page.
			 *
			 * WARNING!  We must hold pt_placemark across the
			 *	     *ptep test to prevent misintepreting
			 *	     a non-zero *ptep as a shared page
			 *	     table page.  Hold it across the function
			 *	     callback as well for SMP safety.
			 */
			KKASSERT(0);
			ptep = pv_pte_lookup(pd_pv, pmap_pt_index(sva));
			if (*ptep & pmap->pmap_bits[PG_V_IDX]) {
				info->func(pmap, info, pt_placemark, pd_pv,
					   sva, ptep, info->arg);
			} else {
				pv_placemarker_wakeup(pmap, pt_placemark);
			}
#else
			pv_placemarker_wakeup(pmap, pt_placemark);
#endif

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
			vm_pindex_t *pte_placemark;
			pv_entry_t pte_pv;

			/*
			 * Yield every 64 pages, stop if requested.
			 */
			if ((++info->count & 63) == 0)
				lwkt_user_yield();
			if (info->stop)
				break;

			/*
			 * We can shortcut our scan if *ptep == 0.  This is
			 * an unlocked check.
			 */
			if (*ptep == 0) {
				sva += PAGE_SIZE;
				++ptep;
				continue;
			}
			cpu_ccfence();

			/*
			 * Acquire the pte_placemark.  pte_pv's won't exist
			 * for leaf pages.
			 *
			 * A multitude of races are possible here so if we
			 * cannot lock definite state we clean out our cache
			 * and break the inner while() loop to force a loop
			 * up to the top of the for().
			 *
			 * XXX unlock/relock pd_pv, pt_pv, and re-test their
			 *     validity instead of looping up?
			 */
			pte_pv = pv_get_try(pmap, pmap_pte_pindex(sva),
					    &pte_placemark, &error);
			KKASSERT(pte_pv == NULL);
			if (error) {
				if (pd_pv) {
					pv_put(pd_pv);	/* lock order */
					pd_pv = NULL;
				}
				if (pt_pv) {
					pv_put(pt_pv);	/* lock order */
					pt_pv = NULL;
				}
				pv_placemarker_wait(pmap, pte_placemark);
				va_next = sva;		/* retry */
				break;
			}

			/*
			 * Reload *ptep after successfully locking the
			 * pindex.
			 */
			cpu_ccfence();
			oldpte = *ptep;
			if (oldpte == 0) {
				pv_placemarker_wakeup(pmap, pte_placemark);
				sva += PAGE_SIZE;
				++ptep;
				continue;
			}

			/*
			 * We can't hold pd_pv across the callback (because
			 * we don't pass it to the callback and the callback
			 * might deadlock)
			 */
			if (pd_pv) {
				vm_page_wire_quick(pd_pv->pv_m);
				pv_unlock(pd_pv);
			}

			/*
			 * Ready for the callback.  The locked placemarker
			 * is consumed by the callback.
			 */
			if (oldpte & pmap->pmap_bits[PG_MANAGED_IDX]) {
				/*
				 * Managed pte
				 */
				KASSERT((oldpte & pmap->pmap_bits[PG_V_IDX]),
				    ("badC *ptep %016lx/%016lx sva %016lx",
				    *ptep, oldpte, sva));
				/*
				 * We must unlock pd_pv across the callback
				 * to avoid deadlocks on any recursive
				 * disposal.  Re-check that it still exists
				 * after re-locking.
				 *
				 * Call target disposes of pte_placemark
				 * and may destroy but will not dispose
				 * of pt_pv.
				 */
				info->func(pmap, info, pte_placemark, pt_pv,
					   sva, ptep, info->arg);
			} else {
				/*
				 * Unmanaged pte
				 *
				 * We must unlock pd_pv across the callback
				 * to avoid deadlocks on any recursive
				 * disposal.  Re-check that it still exists
				 * after re-locking.
				 *
				 * Call target disposes of pte_placemark
				 * and may destroy but will not dispose
				 * of pt_pv.
				 */
				KASSERT((oldpte & pmap->pmap_bits[PG_V_IDX]),
				    ("badD *ptep %016lx/%016lx sva %016lx ",
				     *ptep, oldpte, sva));
				info->func(pmap, info, pte_placemark, pt_pv,
					   sva, ptep, info->arg);
			}
			if (pd_pv) {
				pv_lock(pd_pv);
				if (vm_page_unwire_quick(pd_pv->pv_m)) {
					panic("pmap_scan_callback: "
					      "bad wirecount on pd_pv");
				}
				if (pd_pv->pv_pmap == NULL) {
					va_next = sva;		/* retry */
					break;
				}
			}

			/*
			 * NOTE: The cached pt_pv can be removed from the
			 *	 pmap when pmap_dynamic_delete is enabled,
			 *	 which will cause ptep to become stale.
			 *
			 *	 This also means that no pages remain under
			 *	 the PT, so we can just break out of the inner
			 *	 loop and let the outer loop clean everything
			 *	 up.
			 */
			if (pt_pv && pt_pv->pv_pmap != pmap)
				break;
			sva += PAGE_SIZE;
			++ptep;
		}
	}
	if (pd_pv) {
		pv_put(pd_pv);
		pd_pv = NULL;
	}
	if (pt_pv) {
		pv_put(pt_pv);
		pt_pv = NULL;
	}
	if ((++info->count & 7) == 0)
		lwkt_user_yield();

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
	pmap_scan(&info, 1);
#if 0
	cpu_invltlb();
	if (eva - sva < 1024*1024) {
		while (sva < eva) {
			cpu_invlpg((void *)sva);
			sva += PAGE_SIZE;
		}
	}
#endif
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
	pmap_scan(&info, 0);
}

static void
pmap_remove_callback(pmap_t pmap, struct pmap_scan_info *info,
		     vm_pindex_t *pte_placemark, pv_entry_t pt_pv,
		     vm_offset_t va, pt_entry_t *ptep, void *arg __unused)
{
	pt_entry_t pte;
	vm_page_t oldm;

	/*
	 * Managed or unmanaged pte (pte_placemark is non-NULL)
	 *
	 * pt_pv's wire_count is still bumped by unmanaged pages
	 * so we must decrement it manually.
	 *
	 * We have to unwire the target page table page.
	 */
	pte = *ptep;
	if (pte & pmap->pmap_bits[PG_MANAGED_IDX]) {
		oldm = PHYS_TO_VM_PAGE(pte & PG_FRAME);
		atomic_add_long(&oldm->md.interlock_count, 1);
	} else {
		oldm = NULL;
	}

	pte = pmap_inval_bulk(info->bulk, va, ptep, 0);
	if (pte & pmap->pmap_bits[PG_MANAGED_IDX]) {
		vm_page_t p;

		p = PHYS_TO_VM_PAGE(pte & PG_FRAME);
		KKASSERT(pte & pmap->pmap_bits[PG_V_IDX]);
		if (pte & pmap->pmap_bits[PG_M_IDX])
			vm_page_dirty(p);
		if (pte & pmap->pmap_bits[PG_A_IDX])
			vm_page_flag_set(p, PG_REFERENCED);

		/*
		 * (p) is not hard-busied.
		 *
		 * We can safely clear PG_MAPPED and PG_WRITEABLE only
		 * if PG_MAPPEDMULTI is not set, atomically.
		 */
		pmap_removed_pte(p, pte);
	}
	if (pte & pmap->pmap_bits[PG_V_IDX]) {
		atomic_add_long(&pmap->pm_stats.resident_count, -1);
		if (pt_pv && vm_page_unwire_quick(pt_pv->pv_m))
			panic("pmap_remove: insufficient wirecount");
	}
	if (pte & pmap->pmap_bits[PG_W_IDX])
		atomic_add_long(&pmap->pm_stats.wired_count, -1);
	if (pte & pmap->pmap_bits[PG_G_IDX])
		cpu_invlpg((void *)va);
	pv_placemarker_wakeup(pmap, pte_placemark);
	if (oldm) {
		if ((atomic_fetchadd_long(&oldm->md.interlock_count, -1) &
		     0x7FFFFFFFFFFFFFFFLU) == 0x4000000000000001LU) {
			atomic_clear_long(&oldm->md.interlock_count,
					  0x4000000000000000LU);
			wakeup(&oldm->md.interlock_count);
		}
	}
}

/*
 * Removes this physical page from all physical maps in which it resides.
 * Reflects back modify bits to the pager.
 *
 * This routine may not be called from an interrupt.
 *
 * The page must be busied by its caller, preventing new ptes from being
 * installed.  This allows us to assert that pmap_count is zero and safely
 * clear the MAPPED and WRITEABLE bits upon completion.
 */
static
void
pmap_remove_all(vm_page_t m)
{
	long icount;
	int retry;

	if (__predict_false(!pmap_initialized))
		return;

	/*
	 * pmap_count doesn't cover fictitious pages, but PG_MAPPED does
	 * (albeit without certain race protections).
	 */
#if 0
	if (m->md.pmap_count == 0)
		return;
#endif
	if ((m->flags & PG_MAPPED) == 0)
		return;

	retry = ticks + hz * 60;
again:
	PMAP_PAGE_BACKING_SCAN(m, NULL, ipmap, iptep, ipte, iva) {
		if (!pmap_inval_smp_cmpset(ipmap, iva, iptep, ipte, 0))
			PMAP_PAGE_BACKING_RETRY;
		if (ipte & ipmap->pmap_bits[PG_MANAGED_IDX]) {
			if (ipte & ipmap->pmap_bits[PG_M_IDX])
				vm_page_dirty(m);
			if (ipte & ipmap->pmap_bits[PG_A_IDX])
				vm_page_flag_set(m, PG_REFERENCED);

			/*
			 * NOTE: m is not hard-busied so it is not safe to
			 *	 clear PG_MAPPED and PG_WRITEABLE on the 1->0
			 *	 transition against them being set in
			 *	 pmap_enter().
			 */
			pmap_removed_pte(m, ipte);
		}

		/*
		 * Cleanup various tracking counters.  pt_pv can't go away
		 * due to our wired ref.
		 */
		if (ipmap != &kernel_pmap) {
			pv_entry_t pt_pv;

			spin_lock_shared(&ipmap->pm_spin);
			pt_pv = pv_entry_lookup(ipmap, pmap_pt_pindex(iva));
			spin_unlock_shared(&ipmap->pm_spin);

			if (pt_pv) {
				if (vm_page_unwire_quick(pt_pv->pv_m)) {
					panic("pmap_remove_all: bad "
					      "wire_count on pt_pv");
				}
				atomic_add_long(
					&ipmap->pm_stats.resident_count, -1);
			}
		}
		if (ipte & ipmap->pmap_bits[PG_W_IDX])
			atomic_add_long(&ipmap->pm_stats.wired_count, -1);
		if (ipte & ipmap->pmap_bits[PG_G_IDX])
			cpu_invlpg((void *)iva);
	} PMAP_PAGE_BACKING_DONE;

	/*
	 * If our scan lost a pte swap race oldm->md.interlock_count might
	 * be set from the pmap_enter() code.  If so sleep a little and try
	 * again.
	 */
	icount = atomic_fetchadd_long(&m->md.interlock_count,
				      0x8000000000000000LU) +
		 0x8000000000000000LU;
	cpu_ccfence();
	while (icount & 0x3FFFFFFFFFFFFFFFLU) {
		tsleep_interlock(&m->md.interlock_count, 0);
		if (atomic_fcmpset_long(&m->md.interlock_count, &icount,
					icount | 0x4000000000000000LU)) {
			tsleep(&m->md.interlock_count, PINTERLOCKED,
			       "pgunm", 1);
			icount = m->md.interlock_count;
			if (retry - ticks > 0)
				goto again;
			panic("pmap_remove_all: cannot return interlock_count "
			      "to 0 (%p, %ld)",
			      m, m->md.interlock_count);
		}
	}
	vm_page_flag_clear(m, PG_MAPPED | PG_MAPPEDMULTI | PG_WRITEABLE);
}

/*
 * Removes the page from a particular pmap.
 *
 * The page must be busied by the caller.
 */
void
pmap_remove_specific(pmap_t pmap_match, vm_page_t m)
{
	if (__predict_false(!pmap_initialized))
		return;

	/*
	 * PG_MAPPED test works for both non-fictitious and fictitious pages.
	 */
	if ((m->flags & PG_MAPPED) == 0)
		return;

	PMAP_PAGE_BACKING_SCAN(m, pmap_match, ipmap, iptep, ipte, iva) {
		if (!pmap_inval_smp_cmpset(ipmap, iva, iptep, ipte, 0))
			PMAP_PAGE_BACKING_RETRY;
		if (ipte & ipmap->pmap_bits[PG_MANAGED_IDX]) {
			if (ipte & ipmap->pmap_bits[PG_M_IDX])
				vm_page_dirty(m);
			if (ipte & ipmap->pmap_bits[PG_A_IDX])
				vm_page_flag_set(m, PG_REFERENCED);

			/*
			 * NOTE: m is not hard-busied so it is not safe to
			 *	 clear PG_MAPPED and PG_WRITEABLE on the 1->0
			 *	 transition against them being set in
			 *	 pmap_enter().
			 */
			pmap_removed_pte(m, ipte);
		}

		/*
		 * Cleanup various tracking counters.  pt_pv can't go away
		 * due to our wired ref.
		 */
		if (ipmap != &kernel_pmap) {
			pv_entry_t pt_pv;

			spin_lock_shared(&ipmap->pm_spin);
			pt_pv = pv_entry_lookup(ipmap, pmap_pt_pindex(iva));
			spin_unlock_shared(&ipmap->pm_spin);

			if (pt_pv) {
				atomic_add_long(
					&ipmap->pm_stats.resident_count, -1);
				if (vm_page_unwire_quick(pt_pv->pv_m)) {
					panic("pmap_remove_specific: bad "
					      "wire_count on pt_pv");
				}
			}
		}
		if (ipte & ipmap->pmap_bits[PG_W_IDX])
			atomic_add_long(&ipmap->pm_stats.wired_count, -1);
		if (ipte & ipmap->pmap_bits[PG_G_IDX])
			cpu_invlpg((void *)iva);
	} PMAP_PAGE_BACKING_DONE;
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
	if ((prot & (VM_PROT_READ | VM_PROT_EXECUTE)) == VM_PROT_NONE) {
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
	pmap_scan(&info, 1);
}

static
void
pmap_protect_callback(pmap_t pmap, struct pmap_scan_info *info,
		      vm_pindex_t *pte_placemark,
		      pv_entry_t pt_pv, vm_offset_t va,
		      pt_entry_t *ptep, void *arg __unused)
{
	pt_entry_t pbits;
	pt_entry_t cbits;
	vm_page_t m;

again:
	pbits = *ptep;
	cpu_ccfence();
	cbits = pbits;
	if (pbits & pmap->pmap_bits[PG_MANAGED_IDX]) {
		cbits &= ~pmap->pmap_bits[PG_A_IDX];
		cbits &= ~pmap->pmap_bits[PG_M_IDX];
	}
	/* else unmanaged page, adjust bits, no wire changes */

	if (ptep) {
		cbits &= ~pmap->pmap_bits[PG_RW_IDX];
#ifdef PMAP_DEBUG2
		if (pmap_enter_debug > 0) {
			--pmap_enter_debug;
			kprintf("pmap_protect va=%lx ptep=%p "
				"pt_pv=%p cbits=%08lx\n",
				va, ptep, pt_pv, cbits
			);
		}
#endif
		if (pbits != cbits) {
			if (!pmap_inval_smp_cmpset(pmap, va,
						   ptep, pbits, cbits)) {
				goto again;
			}
		}
		if (pbits & pmap->pmap_bits[PG_MANAGED_IDX]) {
			m = PHYS_TO_VM_PAGE(pbits & PG_FRAME);
			if (pbits & pmap->pmap_bits[PG_A_IDX])
				vm_page_flag_set(m, PG_REFERENCED);
			if (pbits & pmap->pmap_bits[PG_M_IDX])
				vm_page_dirty(m);
		}
	}
	pv_placemarker_wakeup(pmap, pte_placemark);
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
	pv_entry_t pt_pv;	/* page table */
	pv_entry_t pte_pv;	/* page table entry */
	vm_pindex_t *pte_placemark;
	pt_entry_t *ptep;
	pt_entry_t origpte;
	vm_paddr_t opa;
	vm_page_t oldm;
	pt_entry_t newpte;
	vm_paddr_t pa;
	int flags;
	int nflags;

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
	 * Get the locked page table page (pt_pv) for our new page table
	 * entry, allocating it if necessary.
	 *
	 * There is no pte_pv for a terminal pte so the terminal pte will
	 * be locked via pte_placemark.
	 *
	 * Only MMU actions by the CPU itself can modify the ptep out from
	 * under us.
	 *
	 * If the pmap is still being initialized we assume existing
	 * page tables.
	 *
	 * NOTE: Kernel mapppings do not track page table pages
	 *	 (i.e. there is no pt_pv pt_pv structure).
	 *
	 * NOTE: origpte here is 'tentative', used only to check for
	 *	 the degenerate case where the entry already exists and
	 *	 matches.
	 */
	if (__predict_false(pmap_initialized == FALSE)) {
		pte_pv = NULL;
		pt_pv = NULL;
		pte_placemark = NULL;
		ptep = vtopte(va);
		origpte = *ptep;
	} else {
		pte_pv = pv_get(pmap, pmap_pte_pindex(va), &pte_placemark);
		KKASSERT(pte_pv == NULL);
		if (va >= VM_MAX_USER_ADDRESS) {
			pt_pv = NULL;
			ptep = vtopte(va);
		} else {
			pt_pv = pmap_allocpte(pmap, pmap_pt_pindex(va), NULL);
			ptep = pv_pte_lookup(pt_pv, pmap_pte_index(va));
		}
		origpte = *ptep;
		cpu_ccfence();
	}

	pa = VM_PAGE_TO_PHYS(m);

	/*
	 * Calculate the new PTE.
	 */
	newpte = (pt_entry_t)(pa | pte_prot(pmap, prot) |
		 pmap->pmap_bits[PG_V_IDX] | pmap->pmap_bits[PG_A_IDX]);
	if (wired)
		newpte |= pmap->pmap_bits[PG_W_IDX];
	if (va < VM_MAX_USER_ADDRESS)
		newpte |= pmap->pmap_bits[PG_U_IDX];
	if ((m->flags & PG_FICTITIOUS) == 0)
		newpte |= pmap->pmap_bits[PG_MANAGED_IDX];
//	if (pmap == &kernel_pmap)
//		newpte |= pgeflag;
	newpte |= pmap->pmap_cache_bits_pte[m->pat_mode];

	/*
	 * It is possible for multiple faults to occur in threaded
	 * environments, the existing pte might be correct.
	 */
	if (((origpte ^ newpte) &
	    ~(pt_entry_t)(pmap->pmap_bits[PG_M_IDX] |
			  pmap->pmap_bits[PG_A_IDX])) == 0) {
		goto done;
	}

	/*
	 * Adjust page flags.  The page is soft-busied or hard-busied, we
	 * should be able to safely set PG_* flag bits even with the (shared)
	 * soft-busy.
	 *
	 * The pmap_count and writeable_count is only tracked for
	 * non-fictitious pages.  As a bit of a safety, bump pmap_count
	 * and set the PG_* bits before mapping the page.  If another part
	 * of the system does not properly hard-busy the page (against our
	 * soft-busy or hard-busy) in order to remove mappings it might not
	 * see the pte that we are about to add and thus will not be able to
	 * drop pmap_count to 0.
	 *
	 * The PG_MAPPED and PG_WRITEABLE flags are set for any type of page.
	 *
	 * NOTE! PG_MAPPED and PG_WRITEABLE can only be cleared when
	 *	 the page is hard-busied AND pmap_count is 0.  This
	 *	 interlocks our setting of the flags here.
	 */
	/*vm_page_spin_lock(m);*/

	/*
	 * In advanced mode we keep track of single mappings verses
	 * multiple mappings in order to avoid unnecessary vm_page_protect()
	 * calls (particularly on the kernel_map).
	 *
	 * If non-advanced mode we track the mapping count for similar effect.
	 *
	 * Avoid modifying the vm_page as much as possible, conditionalize
	 * updates to reduce cache line ping-ponging.
	 */
	flags = m->flags;
	cpu_ccfence();
	for (;;) {
		nflags = PG_MAPPED;
		if (newpte & pmap->pmap_bits[PG_RW_IDX])
			nflags |= PG_WRITEABLE;
		if (flags & PG_MAPPED)
			nflags |= PG_MAPPEDMULTI;
		if (flags == (flags | nflags))
			break;
		if (atomic_fcmpset_int(&m->flags, &flags, flags | nflags))
			break;
	}
	/*vm_page_spin_unlock(m);*/

	/*
	 * A race can develop when replacing an existing mapping.  The new
	 * page has been busied and the pte is placemark-locked, but the
	 * old page could be ripped out from under us at any time by
	 * a backing scan.
	 *
	 * If we do nothing, a concurrent backing scan may clear
	 * PG_WRITEABLE and PG_MAPPED before we can act on oldm.
	 */
	opa = origpte & PG_FRAME;
	if (opa && (origpte & pmap->pmap_bits[PG_MANAGED_IDX])) {
		oldm = PHYS_TO_VM_PAGE(opa);
		KKASSERT(opa == oldm->phys_addr);
		KKASSERT(entry != NULL);
		atomic_add_long(&oldm->md.interlock_count, 1);
	} else {
		oldm = NULL;
	}

	/*
	 * Swap the new and old PTEs and perform any necessary SMP
	 * synchronization.
	 */
	if ((prot & VM_PROT_NOSYNC) || (opa == 0 && pt_pv != NULL)) {
		/*
		 * Explicitly permitted to avoid pmap cpu mask synchronization
		 * or the prior content of a non-kernel-related pmap was
		 * invalid.
		 */
		origpte = atomic_swap_long(ptep, newpte);
		if (opa)
			cpu_invlpg((void *)va);
	} else {
		/*
		 * Not permitted to avoid pmap cpu mask synchronization
		 * or there prior content being replaced or this is a kernel
		 * related pmap.
		 *
		 * Due to other kernel optimizations, we cannot assume a
		 * 0->non_zero transition of *ptep can be done with a swap.
		 */
		origpte = pmap_inval_smp(pmap, va, 1, ptep, newpte);
	}
	opa = origpte & PG_FRAME;

#ifdef PMAP_DEBUG2
	if (pmap_enter_debug > 0) {
		--pmap_enter_debug;
		kprintf("pmap_enter: va=%lx m=%p origpte=%lx newpte=%lx ptep=%p"
			" pte_pv=%p pt_pv=%p opa=%lx prot=%02x\n",
			va, m,
			origpte, newpte, ptep,
			pte_pv, pt_pv, opa, prot);
	}
#endif

	/*
	 * Account for the changes in the pt_pv and pmap.
	 *
	 * Retain the same wiring count due to replacing an existing page,
	 * or bump the wiring count for a new page.
	 */
	if (pt_pv && opa == 0) {
		vm_page_wire_quick(pt_pv->pv_m);
		atomic_add_long(&pt_pv->pv_pmap->pm_stats.resident_count, 1);
	}
	if (wired && (origpte & pmap->pmap_bits[PG_W_IDX]) == 0)
		atomic_add_long(&pmap->pm_stats.wired_count, 1);

	/*
	 * Account for the removal of the old page.  pmap and pt_pv stats
	 * have already been fully adjusted for both.
	 *
	 * WARNING! oldm is not soft or hard-busied.  The pte at worst can
	 *	    only be removed out from under us since we hold the
	 *	    placemarker.  So if it is still there, it must not have
	 *	    changed.
	 *
	 * WARNING! A backing scan can clear PG_WRITEABLE and/or PG_MAPPED
	 *	    and rip oldm away from us, possibly even freeing or
	 *	    paging it, and not setting our dirtying below.
	 *
	 *	    To deal with this, oldm->md.interlock_count is bumped
	 *	    to indicate that we might (only might) have won the pte
	 *	    swap race, and then released below.
	 */
	if (opa && (origpte & pmap->pmap_bits[PG_MANAGED_IDX])) {
		KKASSERT(oldm == PHYS_TO_VM_PAGE(opa));
		if (origpte & pmap->pmap_bits[PG_M_IDX])
			vm_page_dirty(oldm);
		if (origpte & pmap->pmap_bits[PG_A_IDX])
			vm_page_flag_set(oldm, PG_REFERENCED);

		/*
		 * NOTE: oldm is not hard-busied so it is not safe to
		 *	 clear PG_MAPPED and PG_WRITEABLE on the 1->0
		 *	 transition against them being set in
		 *	 pmap_enter().
		 */
		pmap_removed_pte(oldm, origpte);
	}
	if (oldm) {
		if ((atomic_fetchadd_long(&oldm->md.interlock_count, -1) &
		     0x7FFFFFFFFFFFFFFFLU) == 0x4000000000000001LU) {
			atomic_clear_long(&oldm->md.interlock_count,
					  0x4000000000000000LU);
			wakeup(&oldm->md.interlock_count);
		}
	}

done:
	KKASSERT((newpte & pmap->pmap_bits[PG_MANAGED_IDX]) == 0 ||
		 (m->flags & PG_MAPPED));

	/*
	 * Cleanup the pv entry, allowing other accessors.  If the new page
	 * is not managed but we have a pte_pv (which was locking our
	 * operation), we can free it now.  pte_pv->pv_m should be NULL.
	 */
	if (pte_placemark)
		pv_placemarker_wakeup(pmap, pte_placemark);
	if (pt_pv)
		pv_put(pt_pv);
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

#if 0
#define MAX_INIT_PT (96)

/*
 * This routine preloads the ptes for a given object into the specified pmap.
 * This eliminates the blast of soft faults on process startup and
 * immediately after an mmap.
 */
static int pmap_object_init_pt_callback(vm_page_t p, void *data);
#endif

void
pmap_object_init_pt(pmap_t pmap, vm_map_entry_t entry,
		    vm_offset_t addr, vm_size_t size, int limit)
{
#if 0
	vm_prot_t prot = entry->protection;
	vm_object_t object = entry->ba.object;
	vm_pindex_t pindex = atop(entry->ba.offset + (addr - entry->ba.start));
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
	info.object = object;
	info.entry = entry;

	/*
	 * By using the NOLK scan, the callback function must be sure
	 * to return -1 if the VM page falls out of the object.
	 */
	vm_object_hold_shared(object);
	vm_page_rb_tree_RB_SCAN_NOLK(&object->rb_memq, rb_vm_page_scancmp,
				     pmap_object_init_pt_callback, &info);
	vm_object_drop(object);
#endif
}

#if 0

static
int
pmap_object_init_pt_callback(vm_page_t p, void *data)
{
	struct rb_vm_page_scan_info *info = data;
	vm_pindex_t rel_index;
	int hard_busy;

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
	hard_busy = 0;
again:
	if (hard_busy) {
		if (vm_page_busy_try(p, TRUE))
			return 0;
	} else {
		if (vm_page_sbusy_try(p))
			return 0;
	}
	if (((p->valid & VM_PAGE_BITS_ALL) == VM_PAGE_BITS_ALL) &&
	    (p->flags & PG_FICTITIOUS) == 0) {
		if ((p->queue - p->pc) == PQ_CACHE) {
			if (hard_busy == 0) {
				vm_page_sbusy_drop(p);
				hard_busy = 1;
				goto again;
			}
			vm_page_deactivate(p);
		}
		rel_index = p->pindex - info->start_pindex;
		pmap_enter(info->pmap, info->addr + x86_64_ptob(rel_index), p,
			   VM_PROT_READ, FALSE, info->entry);
	}
	if (hard_busy)
		vm_page_wakeup(p);
	else
		vm_page_sbusy_drop(p);

	/*
	 * We are using an unlocked scan (that is, the scan expects its
	 * current element to remain in the tree on return).  So we have
	 * to check here and abort the scan if it isn't.
	 */
	if (p->object != info->object)
		return -1;
	lwkt_yield();
	return(0);
}

#endif

/*
 * Return TRUE if the pmap is in shape to trivially pre-fault the specified
 * address.
 *
 * Returns FALSE if it would be non-trivial or if a pte is already loaded
 * into the slot.
 *
 * The address must reside within a vm_map mapped range to ensure that the
 * page table doesn't get ripped out from under us.
 *
 * XXX This is safe only because page table pages are not freed.
 */
int
pmap_prefault_ok(pmap_t pmap, vm_offset_t addr)
{
	pt_entry_t *pte;

	/*spin_lock(&pmap->pm_spin);*/
	if ((pte = pmap_pte(pmap, addr)) != NULL) {
		if (*pte & pmap->pmap_bits[PG_V_IDX]) {
			/*spin_unlock(&pmap->pm_spin);*/
			return FALSE;
		}
	}
	/*spin_unlock(&pmap->pm_spin);*/
	return TRUE;
}

/*
 * Change the wiring attribute for a pmap/va pair.  The mapping must already
 * exist in the pmap.  The mapping may or may not be managed.  The wiring in
 * the page is not changed, the page is returned so the caller can adjust
 * its wiring (the page is not locked in any way).
 *
 * Wiring is not a hardware characteristic so there is no need to invalidate
 * TLB.  However, in an SMP environment we must use a locked bus cycle to
 * update the pte (if we are not using the pmap_inval_*() API that is)...
 * it's ok to do this for simple wiring changes.
 */
vm_page_t
pmap_unwire(pmap_t pmap, vm_offset_t va)
{
	pt_entry_t *ptep;
	pv_entry_t pt_pv;
	vm_paddr_t pa;
	vm_page_t m;

	if (pmap == NULL)
		return NULL;

	/*
	 * Assume elements in the kernel pmap are stable
	 */
	if (pmap == &kernel_pmap) {
		if (pmap_pt(pmap, va) == 0)
			return NULL;
		ptep = pmap_pte_quick(pmap, va);
		if (pmap_pte_v(pmap, ptep)) {
			if (pmap_pte_w(pmap, ptep))
				atomic_add_long(&pmap->pm_stats.wired_count,-1);
			atomic_clear_long(ptep, pmap->pmap_bits[PG_W_IDX]);
			pa = *ptep & PG_FRAME;
			m = PHYS_TO_VM_PAGE(pa);
		} else {
			m = NULL;
		}
	} else {
		/*
		 * We can only [un]wire pmap-local pages (we cannot wire
		 * shared pages)
		 */
		pt_pv = pv_get(pmap, pmap_pt_pindex(va), NULL);
		if (pt_pv == NULL)
			return NULL;

		ptep = pv_pte_lookup(pt_pv, pmap_pte_index(va));
		if ((*ptep & pmap->pmap_bits[PG_V_IDX]) == 0) {
			pv_put(pt_pv);
			return NULL;
		}

		if (pmap_pte_w(pmap, ptep)) {
			atomic_add_long(&pt_pv->pv_pmap->pm_stats.wired_count,
					-1);
		}
		/* XXX else return NULL so caller doesn't unwire m ? */

		atomic_clear_long(ptep, pmap->pmap_bits[PG_W_IDX]);

		pa = *ptep & PG_FRAME;
		m = PHYS_TO_VM_PAGE(pa);	/* held by wired count */
		pv_put(pt_pv);
	}
	return m;
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
 *
 * Currently only used to test the 'M'odified bit.  If the page
 * is not PG_WRITEABLE, the 'M'odified bit cannot be set and we
 * return immediately.  Fictitious pages do not track this bit.
 */
static
boolean_t
pmap_testbit(vm_page_t m, int bit)
{
	int res = FALSE;

	if (__predict_false(!pmap_initialized || (m->flags & PG_FICTITIOUS)))
		return FALSE;
	/*
	 * Nothing to do if all the mappings are already read-only.
	 * The page's [M]odify bits have already been synchronized
	 * to the vm_page_t and cleaned out.
	 */
	if (bit == PG_M_IDX && (m->flags & PG_WRITEABLE) == 0)
		return FALSE;

	/*
	 * Iterate the mapping
	 */
	PMAP_PAGE_BACKING_SCAN(m, NULL, ipmap, iptep, ipte, iva) {
		if (ipte & ipmap->pmap_bits[bit]) {
			res = TRUE;
			break;
		}
	} PMAP_PAGE_BACKING_DONE;
	return res;
}

/*
 * This routine is used to modify bits in ptes.  Only one bit should be
 * specified.  PG_RW requires special handling.  This call works with
 * any sort of mapped page.  PG_FICTITIOUS pages might not be optimal.
 *
 * Caller must NOT hold any spin locks
 * Caller must hold (m) hard-busied
 *
 * NOTE: When clearing PG_M we could also (not implemented) drop
 *       through to the PG_RW code and clear PG_RW too, forcing
 *       a fault on write to redetect PG_M for virtual kernels, but
 *       it isn't necessary since virtual kernels invalidate the
 *       pte when they clear the VPTE_M bit in their virtual page
 *       tables.
 *
 * NOTE: Does not re-dirty the page when clearing only PG_M.
 *
 * NOTE: Because we do not lock the pv, *pte can be in a state of
 *       flux.  Despite this the value of *pte is still somewhat
 *       related while we hold the vm_page spin lock.
 *
 *       *pte can be zero due to this race.  Since we are clearing
 *       bits we basically do no harm when this race occurs.
 */
static __inline
void
pmap_clearbit(vm_page_t m, int bit_index)
{
	pt_entry_t npte;
	int retry;
	long icount;

	/*
	 * Too early in the boot
	 */
	if (__predict_false(!pmap_initialized)) {
		if (bit_index == PG_RW_IDX)
			vm_page_flag_clear(m, PG_WRITEABLE);
		return;
	}
	if ((m->flags & (PG_MAPPED | PG_WRITEABLE)) == 0)
		return;

	/*
	 * Being asked to clear other random bits, we don't track them
	 * so we have to iterate.
	 *
	 * pmap_clear_reference() is called (into here) with the page
	 * hard-busied to check whether the page is still mapped and
	 * will clear PG_MAPPED and PG_WRITEABLE if it isn't.
	 */
	if (bit_index != PG_RW_IDX) {
#if 0
		long icount;

		icount = 0;
#endif
		PMAP_PAGE_BACKING_SCAN(m, NULL, ipmap, iptep, ipte, iva) {
#if 0
			++icount;
#endif
			if (ipte & ipmap->pmap_bits[bit_index]) {
				atomic_clear_long(iptep,
						  ipmap->pmap_bits[bit_index]);
			}
		} PMAP_PAGE_BACKING_DONE;
#if 0
		if (icount == 0) {
			icount = atomic_fetchadd_long(&m->md.interlock_count,
						      0x8000000000000000LU);
			if ((icount & 0x3FFFFFFFFFFFFFFFLU) == 0) {
				vm_page_flag_clear(m, PG_MAPPED |
						      PG_MAPPEDMULTI |
						      PG_WRITEABLE);
			}
		}
#endif
		return;
	}

	/*
	 * Being asked to clear the RW bit.
	 *
	 * Nothing to do if all the mappings are already read-only
	 */
	if ((m->flags & PG_WRITEABLE) == 0)
		return;

	/*
	 * Iterate the mappings and check.
	 */
	retry = ticks + hz * 60;
again:
	/*
	 * Clear PG_RW. This also clears PG_M and marks the page dirty if
	 * PG_M was set.
	 *
	 * Since the caller holds the page hard-busied we can safely clear
	 * PG_WRITEABLE, and callers expect us to for the PG_RW_IDX path.
	 */
	PMAP_PAGE_BACKING_SCAN(m, NULL, ipmap, iptep, ipte, iva) {
#if 0
		if ((ipte & ipmap->pmap_bits[PG_MANAGED_IDX]) == 0)
			continue;
#endif
		if ((ipte & ipmap->pmap_bits[PG_RW_IDX]) == 0)
			continue;
		npte = ipte & ~(ipmap->pmap_bits[PG_RW_IDX] |
				ipmap->pmap_bits[PG_M_IDX]);
		if (!pmap_inval_smp_cmpset(ipmap, iva, iptep, ipte, npte))
			PMAP_PAGE_BACKING_RETRY;
		if (ipte & ipmap->pmap_bits[PG_M_IDX])
			vm_page_dirty(m);

		/*
		 * NOTE: m is not hard-busied so it is not safe to
		 *	 clear PG_WRITEABLE on the 1->0 transition
		 *	 against it being set in pmap_enter().
		 *
		 *	 pmap_count and writeable_count are only applicable
		 *	 to non-fictitious pages (PG_MANAGED_IDX from pte)
		 */
	} PMAP_PAGE_BACKING_DONE;

	/*
	 * If our scan lost a pte swap race oldm->md.interlock_count might
	 * be set from the pmap_enter() code.  If so sleep a little and try
	 * again.
	 *
	 * Use an atomic op to access interlock_count to ensure ordering.
	 */
	icount = atomic_fetchadd_long(&m->md.interlock_count,
				      0x8000000000000000LU) +
		 0x8000000000000000LU;
	cpu_ccfence();
	while (icount & 0x3FFFFFFFFFFFFFFFLU) {
		tsleep_interlock(&m->md.interlock_count, 0);
		if (atomic_fcmpset_long(&m->md.interlock_count, &icount,
				        icount | 0x4000000000000000LU)) {
			tsleep(&m->md.interlock_count, PINTERLOCKED,
			       "pgunm", 1);
			icount = m->md.interlock_count;
			if (retry - ticks > 0)
				goto again;
			panic("pmap_clearbit: cannot return interlock_count "
			      "to 0 (%p, %ld)",
			      m, m->md.interlock_count);
		}
	}
	vm_page_flag_clear(m, PG_WRITEABLE);
}

/*
 * Lower the permission for all mappings to a given page.
 *
 * Page must be hard-busied by caller.  Because the page is busied by the
 * caller, this should not be able to race a pmap_enter().
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
			pmap_clearbit(m, PG_RW_IDX);
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
	int rval = 0;
	pt_entry_t npte;

	if (__predict_false(!pmap_initialized || (m->flags & PG_FICTITIOUS)))
		return rval;
	PMAP_PAGE_BACKING_SCAN(m, NULL, ipmap, iptep, ipte, iva) {
		if (ipte & ipmap->pmap_bits[PG_A_IDX]) {
			npte = ipte & ~ipmap->pmap_bits[PG_A_IDX];
			if (!atomic_cmpset_long(iptep, ipte, npte))
				PMAP_PAGE_BACKING_RETRY;
			++rval;
			if (rval > 4)
				break;
		}
	} PMAP_PAGE_BACKING_DONE;
	return rval;
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

	res = pmap_testbit(m, PG_M_IDX);
	return (res);
}

/*
 * Clear the modify bit on the vm_page.
 *
 * The page must be hard-busied.
 */
void
pmap_clear_modify(vm_page_t m)
{
	pmap_clearbit(m, PG_M_IDX);
}

/*
 *	pmap_clear_reference:
 *
 *	Clear the reference bit on the specified physical page.
 */
void
pmap_clear_reference(vm_page_t m)
{
	pmap_clearbit(m, PG_A_IDX);
}

/*
 * Miscellaneous support routines follow
 */

static
void
x86_64_protection_init(void)
{
	uint64_t *kp;
	int prot;

	/*
	 * NX supported? (boot time loader.conf override only)
	 *
	 * -1	Automatic (sets mode 1)
	 *  0	Disabled
	 *  1	NX implemented, differentiates PROT_READ vs PROT_READ|PROT_EXEC
	 *  2	NX implemented for all cases
	 */
	TUNABLE_INT_FETCH("machdep.pmap_nx_enable", &pmap_nx_enable);
	if ((amd_feature & AMDID_NX) == 0) {
		pmap_bits_default[PG_NX_IDX] = 0;
		pmap_nx_enable = 0;
	} else if (pmap_nx_enable < 0) {
		pmap_nx_enable = 1;		/* default to mode 1 (READ) */
	}

	/*
	 * 0 is basically read-only access, but also set the NX (no-execute)
	 * bit when VM_PROT_EXECUTE is not specified.
	 */
	kp = protection_codes;
	for (prot = 0; prot < PROTECTION_CODES_SIZE; prot++) {
		switch (prot) {
		case VM_PROT_NONE | VM_PROT_NONE | VM_PROT_NONE:
			/*
			 * This case handled elsewhere
			 */
			*kp = 0;
			break;
		case VM_PROT_READ | VM_PROT_NONE | VM_PROT_NONE:
			/*
			 * Read-only is 0|NX	(pmap_nx_enable mode >= 1)
			 */
			if (pmap_nx_enable >= 1)
				*kp = pmap_bits_default[PG_NX_IDX];
			break;
		case VM_PROT_READ | VM_PROT_NONE | VM_PROT_EXECUTE:
		case VM_PROT_NONE | VM_PROT_NONE | VM_PROT_EXECUTE:
			/*
			 * Execute requires read access
			 */
			*kp = 0;
			break;
		case VM_PROT_NONE | VM_PROT_WRITE | VM_PROT_NONE:
		case VM_PROT_READ | VM_PROT_WRITE | VM_PROT_NONE:
			/*
			 * Write without execute is RW|NX
			 *			(pmap_nx_enable mode >= 2)
			 */
			*kp = pmap_bits_default[PG_RW_IDX];
			if (pmap_nx_enable >= 2)
				*kp |= pmap_bits_default[PG_NX_IDX];
			break;
		case VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXECUTE:
		case VM_PROT_NONE | VM_PROT_WRITE | VM_PROT_EXECUTE:
			/*
			 * Write with execute is RW
			 */
			*kp = pmap_bits_default[PG_RW_IDX];
			break;
		}
		++kp;
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

void *
pmap_mapbios(vm_paddr_t pa, vm_size_t size)
{
	return (pmap_mapdev_attr(pa, size, PAT_WRITE_BACK));
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

	va = kmem_alloc_nofault(&kernel_map, size, VM_SUBSYS_MAPDEV, PAGE_SIZE);
	if (va == 0)
		panic("pmap_mapdev: Couldn't alloc kernel virtual memory");

	pa = pa & ~PAGE_MASK;
	for (tmpva = va, tmpsize = size; tmpsize > 0;) {
		pte = vtopte(tmpva);
		*pte = pa |
		    kernel_pmap.pmap_bits[PG_RW_IDX] |
		    kernel_pmap.pmap_bits[PG_V_IDX] | /* pgeflag | */
		    kernel_pmap.pmap_cache_bits_pte[mode];
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
 * Sets the memory attribute for the specified page.
 */
void
pmap_page_set_memattr(vm_page_t m, vm_memattr_t ma)
{

    m->pat_mode = ma;

    /*
     * If "m" is a normal page, update its direct mapping.  This update
     * can be relied upon to perform any cache operations that are
     * required for data coherence.
     */
    if ((m->flags & PG_FICTITIOUS) == 0)
        pmap_change_attr(PHYS_TO_DMAP(VM_PAGE_TO_PHYS(m)), 1, m->pat_mode);
}

/*
 * Change the PAT attribute on an existing kernel memory map.  Caller
 * must ensure that the virtual memory in question is not accessed
 * during the adjustment.
 *
 * If the va is within the DMAP we cannot use vtopte() because the DMAP
 * utilizes 2MB or 1GB pages.  2MB is forced atm so calculate the pd_entry
 * pointer based on that.
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

	if (va >= DMAP_MIN_ADDRESS && va < DMAP_MAX_ADDRESS) {
		pd_entry_t *pd;

		KKASSERT(va < DMapMaxAddress);
		pd = (pd_entry_t *)PHYS_TO_DMAP(DMPDphys);
		pd += (va - DMAP_MIN_ADDRESS) >> PDRSHIFT;

		while ((long)count > 0) {
			*pd =
			   (*pd & ~(pd_entry_t)(kernel_pmap.pmap_cache_mask_pde)) |
			   kernel_pmap.pmap_cache_bits_pde[mode];
			count -= NBPDR / PAGE_SIZE;
			va += NBPDR;
			++pd;
		}
	} else {
		while (count) {
			pte = vtopte(va);
			*pte =
			   (*pte & ~(pt_entry_t)(kernel_pmap.pmap_cache_mask_pte)) |
			   kernel_pmap.pmap_cache_bits_pte[mode];
			--count;
			va += PAGE_SIZE;
		}
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
	
	ptep = pmap_pte(pmap, addr);

	if (ptep && (pte = *ptep) != 0) {
		vm_offset_t pa;

		val = MINCORE_INCORE;
		pa = pte & PG_FRAME;
		if (pte & pmap->pmap_bits[PG_MANAGED_IDX])
			m = PHYS_TO_VM_PAGE(pa);
		else
			m = NULL;

		/*
		 * Modified by us
		 */
		if (pte & pmap->pmap_bits[PG_M_IDX])
			val |= MINCORE_MODIFIED|MINCORE_MODIFIED_OTHER;

		/*
		 * Modified by someone
		 */
		else if (m && (m->dirty || pmap_is_modified(m)))
			val |= MINCORE_MODIFIED_OTHER;

		/*
		 * Referenced by us, or someone else.
		 */
		if (pte & pmap->pmap_bits[PG_A_IDX]) {
			val |= MINCORE_REFERENCED|MINCORE_REFERENCED_OTHER;
		} else if (m && ((m->flags & PG_REFERENCED) ||
				 pmap_ts_referenced(m))) {
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
			vmspace_ref(newvm);
		p->p_vmspace = newvm;
		KKASSERT(p->p_nthreads == 1);
		lp = RB_ROOT(&p->p_lwp_tree);
		pmap_setlwpvm(lp, newvm);
		if (adjrefs)
			vmspace_rel(oldvm);
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
	thread_t td;

	oldvm = lp->lwp_vmspace;

	if (oldvm != newvm) {
		crit_enter();
		td = curthread;
		KKASSERT((newvm->vm_refcnt & VM_REF_DELETED) == 0);
		lp->lwp_vmspace = newvm;
		if (td->td_lwp == lp) {
			pmap = vmspace_pmap(newvm);
			ATOMIC_CPUMASK_ORBIT(pmap->pm_active, mycpu->gd_cpuid);
			if (pmap->pm_active_lock & CPULOCK_EXCL)
				pmap_interlock_wait(newvm);
#if defined(SWTCH_OPTIM_STATS)
			tlb_flush_count++;
#endif
			if (pmap->pmap_bits[TYPE_IDX] == REGULAR_PMAP) {
				td->td_pcb->pcb_cr3 = vtophys(pmap->pm_pml4);
				if (meltdown_mitigation && pmap->pm_pmlpv_iso) {
					td->td_pcb->pcb_cr3_iso =
						vtophys(pmap->pm_pml4_iso);
					td->td_pcb->pcb_flags |= PCB_ISOMMU;
				} else {
					td->td_pcb->pcb_cr3_iso = 0;
					td->td_pcb->pcb_flags &= ~PCB_ISOMMU;
				}
			} else if (pmap->pmap_bits[TYPE_IDX] == EPT_PMAP) {
				td->td_pcb->pcb_cr3 = KPML4phys;
				td->td_pcb->pcb_cr3_iso = 0;
				td->td_pcb->pcb_flags &= ~PCB_ISOMMU;
			} else {
				panic("pmap_setlwpvm: unknown pmap type\n");
			}

			/*
			 * The MMU separation fields needs to be updated.
			 * (it can't access the pcb directly from the
			 * restricted user pmap).
			 */
			{
				struct trampframe *tramp;

				tramp = &pscpu->trampoline;
				tramp->tr_pcb_cr3 = td->td_pcb->pcb_cr3;
				tramp->tr_pcb_cr3_iso = td->td_pcb->pcb_cr3_iso;
				tramp->tr_pcb_flags = td->td_pcb->pcb_flags;
				tramp->tr_pcb_rsp = (register_t)td->td_pcb;
				/* tr_pcb_rsp doesn't change */
			}

			/*
			 * In kernel-land we always use the normal PML4E
			 * so the kernel is fully mapped and can also access
			 * user memory.
			 */
			load_cr3(td->td_pcb->pcb_cr3);
			pmap = vmspace_pmap(oldvm);
			ATOMIC_CPUMASK_NANDBIT(pmap->pm_active,
					       mycpu->gd_cpuid);
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

	if (pmap->pm_active_lock & CPULOCK_EXCL) {
		crit_enter();
		KKASSERT(curthread->td_critcount >= 2);
		DEBUG_PUSH_INFO("pmap_interlock_wait");
		while (pmap->pm_active_lock & CPULOCK_EXCL) {
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

	if ((obj == NULL) || (size < NBPDR) ||
	    ((obj->type != OBJT_DEVICE) && (obj->type != OBJT_MGTDEVICE))) {
		return addr;
	}

	addr = roundup2(addr, NBPDR);
	return addr;
}

/*
 * Used by kmalloc/kfree, page already exists at va
 */
vm_page_t
pmap_kvtom(vm_offset_t va)
{
	pt_entry_t *ptep = vtopte(va);

	return(PHYS_TO_VM_PAGE(*ptep & PG_FRAME));
}

/*
 * Initialize machine-specific shared page directory support.  This
 * is executed when a VM object is created.
 */
void
pmap_object_init(vm_object_t object)
{
}

/*
 * Clean up machine-specific shared page directory support.  This
 * is executed when a VM object is destroyed.
 */
void
pmap_object_free(vm_object_t object)
{
}

/*
 * pmap_pgscan_callback - Used by pmap_pgscan to acquire the related
 * VM page and issue a pginfo->callback.
 */
static
void
pmap_pgscan_callback(pmap_t pmap, struct pmap_scan_info *info,
		      vm_pindex_t *pte_placemark,
		      pv_entry_t pt_pv, vm_offset_t va,
		      pt_entry_t *ptep, void *arg)
{
	struct pmap_pgscan_info *pginfo = arg;
	vm_page_t m;
	pt_entry_t pte;

	pte = *ptep;
	cpu_ccfence();

	if (pte & pmap->pmap_bits[PG_MANAGED_IDX]) {
		/*
		 * Try to busy the page while we hold the pte_placemark locked.
		 */
		m = PHYS_TO_VM_PAGE(*ptep & PG_FRAME);
		if (vm_page_busy_try(m, TRUE) == 0) {
			if (m == PHYS_TO_VM_PAGE(*ptep & PG_FRAME)) {
				/*
				 * The callback is issued with the pt_pv
				 * unlocked.
				 */
				pv_placemarker_wakeup(pmap, pte_placemark);
				if (pt_pv) {
					vm_page_wire_quick(pt_pv->pv_m);
					pv_unlock(pt_pv);
				}
				if (pginfo->callback(pginfo, va, m) < 0)
					info->stop = 1;
				if (pt_pv) {
					pv_lock(pt_pv);
					if (vm_page_unwire_quick(pt_pv->pv_m)) {
						panic("pmap_pgscan: bad wire_"
						      "count on pt_pv");
					}
				}
			} else {
				vm_page_wakeup(m);
				pv_placemarker_wakeup(pmap, pte_placemark);
			}
		} else {
			++pginfo->busycount;
			pv_placemarker_wakeup(pmap, pte_placemark);
		}
	} else {
		/*
		 * Shared page table or unmanaged page (sharept or !sharept)
		 */
		pv_placemarker_wakeup(pmap, pte_placemark);
	}
}

void
pmap_pgscan(struct pmap_pgscan_info *pginfo)
{
	struct pmap_scan_info info;

	pginfo->offset = pginfo->beg_addr;
	info.pmap = pginfo->pmap;
	info.sva = pginfo->beg_addr;
	info.eva = pginfo->end_addr;
	info.func = pmap_pgscan_callback;
	info.arg = pginfo;
	pmap_scan(&info, 0);
	if (info.stop == 0)
		pginfo->offset = pginfo->end_addr;
}

/*
 * Wait for a placemarker that we do not own to clear.  The placemarker
 * in question is not necessarily set to the pindex we want, we may have
 * to wait on the element because we want to reserve it ourselves.
 *
 * NOTE: PM_PLACEMARK_WAKEUP sets a bit which is already set in
 *	 PM_NOPLACEMARK, so it does not interfere with placemarks
 *	 which have already been woken up.
 *
 * NOTE: This routine is called without the pmap spin-lock and so can
 *	 race changes to *pmark.  Due to the sensitivity of the routine
 *	 to possible MULTIPLE interactions from other cpus, and the
 *	 overloading of the WAKEUP bit on PM_NOPLACEMARK, we have to
 *	 use a cmpset loop to avoid a race that might cause the WAKEUP
 *	 bit to be lost.
 *
 * Caller is expected to retry its operation upon return.
 */
static
void
pv_placemarker_wait(pmap_t pmap, vm_pindex_t *pmark)
{
	vm_pindex_t mark;

	mark = *pmark;
	cpu_ccfence();
	while (mark != PM_NOPLACEMARK) {
		tsleep_interlock(pmark, 0);
		if (atomic_fcmpset_long(pmark, &mark,
				       mark | PM_PLACEMARK_WAKEUP)) {
			tsleep(pmark, PINTERLOCKED, "pvplw", 0);
			break;
		}
	}
}

/*
 * Wakeup a placemarker that we own.  Replace the entry with
 * PM_NOPLACEMARK and issue a wakeup() if necessary.
 */
static
void
pv_placemarker_wakeup(pmap_t pmap, vm_pindex_t *pmark)
{
	vm_pindex_t pindex;

	pindex = atomic_swap_long(pmark, PM_NOPLACEMARK);
	KKASSERT(pindex != PM_NOPLACEMARK);
	if (pindex & PM_PLACEMARK_WAKEUP)
		wakeup(pmark);
}
