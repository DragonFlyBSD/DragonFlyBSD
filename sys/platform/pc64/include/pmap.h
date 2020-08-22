/*
 * Copyright (c) 1991 Regents of the University of California.
 * Copyright (c) 2003 Peter Wemm.
 * Copyright (c) 2008 The DragonFly Project.
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
 * Derived from hp300 version by Mike Hibler, this version by William
 * Jolitz uses a recursive map [a pde points to the page directory] to
 * map the page tables using the pagetables themselves. This is done to
 * reduce the impact on kernel virtual memory for lots of sparse address
 * space, and to reduce the cost of memory to each process.
 *
 * from: hp300: @(#)pmap.h	7.2 (Berkeley) 12/16/90
 * from: @(#)pmap.h	7.4 (Berkeley) 5/12/91
 * $FreeBSD: src/sys/i386/include/pmap.h,v 1.65.2.3 2001/10/03 07:15:37 peter Exp $
 */

#ifndef _MACHINE_PMAP_H_
#define	_MACHINE_PMAP_H_

#include <cpu/pmap.h>

/*
 * Size of Kernel address space.  This is the number of page table pages
 * (2GB each) to use for the kernel.  256 pages == 512 Gigabytes.
 * This **MUST** be a multiple of 4 (eg: 252, 256, 260, etc).
 */
#ifndef KVA_PAGES
#define KVA_PAGES	256
#endif

/*
 * Pte related macros.  This is complicated by having to deal with
 * the sign extension of the 48th bit.
 */
#define KVADDR(l4, l3, l2, l1) ( \
	((unsigned long)-1 << 47) | \
	((unsigned long)(l4) << PML4SHIFT) | \
	((unsigned long)(l3) << PDPSHIFT) | \
	((unsigned long)(l2) << PDRSHIFT) | \
	((unsigned long)(l1) << PAGE_SHIFT))

#define UVADDR(l4, l3, l2, l1) ( \
	((unsigned long)(l4) << PML4SHIFT) | \
	((unsigned long)(l3) << PDPSHIFT) | \
	((unsigned long)(l2) << PDRSHIFT) | \
	((unsigned long)(l1) << PAGE_SHIFT))

/*
 * NKPML4E is the number of PML4E slots used for KVM.  Each slot represents
 * 512GB of KVM.  A number between 1 and 128 may be specified.  To support
 * the maximum machine configuration of 64TB we recommend around
 * 16 slots (8TB of KVM).
 *
 * NOTE: We no longer hardwire NKPT, it is calculated in create_pagetables()
 */
#define NKPML4E		16
/* NKPDPE defined in vmparam.h */

/*
 * NUPDPs	512 (256 user)		number of PDPs in user page table
 * NUPDs	512 * 512		number of PDs in user page table
 * NUPTs	512 * 512 * 512		number of PTs in user page table
 * NUPTEs	512 * 512 * 512 * 512	number of PTEs in user page table
 *
 * NUPDP_USER 	number of PDPs reserved for userland
 * NUPTE_USER	number of PTEs reserved for userland (big number)
 */
#define	NUPDP_USER	(NPML4EPG/2)
#define	NUPDP_TOTAL	(NPML4EPG)
#define	NUPD_TOTAL	(NPDPEPG * NUPDP_TOTAL)
#define	NUPT_TOTAL	(NPDEPG * NUPD_TOTAL)
#define NUPTE_TOTAL	((vm_pindex_t)NPTEPG * NUPT_TOTAL)
#define NUPTE_USER	((vm_pindex_t)NPTEPG * NPDEPG * NPDPEPG * NUPDP_USER)

/*
 * Number of 512G dmap PML4 slots.  There are 512 slots of which 256 are
 * used by the kernel.  Of those 256 we allow up to 128 to be used by the
 * DMAP (for 64TB of ram), leaving 128 for the kernel and other incidentals.
 */
#define	NDMPML4E	128

/*
 * The *PML4I values control the layout of virtual memory.  Each PML4
 * entry represents 512G.
 */
#define	PML4PML4I	(NPML4EPG/2)	/* Index of recursive pml4 mapping */

#define	KPML4I		(NPML4EPG-NKPML4E) /* Start of KVM */
#define	DMPML4I		(KPML4I-NDMPML4E) /* Next 512GBxN down for dmap */

/*
 * Make sure the kernel map and DMAP don't overflow the 256 PDP entries
 * we have available.  Minus one for the PML4PML4I.
 */
#if NKPML4E + NDMPML4E >= 255
#error "NKPML4E or NDMPML4E is too large"
#endif

/*
 * The location of KERNBASE in the last PD of the kernel's KVM (KPML4I)
 * space.  Each PD represents 1GB.  The kernel must be placed here
 * for the compile/link options to work properly so absolute 32-bit
 * addressing can be used to access stuff.
 */
#define	KPDPI		(NPDPEPG-2)	/* kernbase at -2GB */

/*
 * per-CPU data assume ~64K x SMP_MAXCPU, say up to 256 cpus
 * in the future or 16MB of space.  Each PD represents 2MB so
 * use NPDEPG-8 to place the per-CPU data.
 */
#define	MPPML4I		(KPML4I + NKPML4E - 1)
#define	MPPDPI		KPDPI
#define	MPPTDI		(NPDEPG-8)

/*
 * XXX doesn't really belong here I guess...
 */
#define ISA_HOLE_START    0xa0000
#define ISA_HOLE_LENGTH (0x100000-ISA_HOLE_START)

#ifndef LOCORE

#ifndef _SYS_TYPES_H_
#include <sys/types.h>
#endif
#ifndef _SYS_CPUMASK_H_
#include <sys/cpumask.h>
#endif
#ifndef _SYS_QUEUE_H_
#include <sys/queue.h>
#endif
#ifndef _SYS_TREE_H_
#include <sys/tree.h>
#endif
#ifndef _SYS_SPINLOCK_H_
#include <sys/spinlock.h>
#endif
#ifndef _SYS_THREAD_H_
#include <sys/thread.h>
#endif
#ifndef _MACHINE_TYPES_H_
#include <machine/types.h>
#endif
#ifndef _MACHINE_PARAM_H_
#include <machine/param.h>
#endif

/*
 * Address of current and alternate address space page table maps
 * and directories.
 */
#ifdef _KERNEL
#define	addr_PTmap	(KVADDR(PML4PML4I, 0, 0, 0))
#define	addr_PDmap	(KVADDR(PML4PML4I, PML4PML4I, 0, 0))
#define	addr_PDPmap	(KVADDR(PML4PML4I, PML4PML4I, PML4PML4I, 0))
#define	addr_PML4map	(KVADDR(PML4PML4I, PML4PML4I, PML4PML4I, PML4PML4I))
#define	addr_PML4pml4e	(addr_PML4map + (PML4PML4I * sizeof(pml4_entry_t)))
#define	PTmap		((pt_entry_t *)(addr_PTmap))
#define	PDmap		((pd_entry_t *)(addr_PDmap))
#define	PDPmap		((pd_entry_t *)(addr_PDPmap))
#define	PML4map		((pd_entry_t *)(addr_PML4map))
#define	PML4pml4e	((pd_entry_t *)(addr_PML4pml4e))

extern u_int64_t KPML4phys;	/* physical address of kernel level 4 */
extern int pmap_fast_kernel_cpusync;
#endif

#ifdef _KERNEL

/*
 * XXX
 */
#define	vtophys(va)	pmap_kextract(((vm_offset_t)(va)))
#define	vtophys_pte(va)	((pt_entry_t)pmap_kextract(((vm_offset_t)(va))))

#endif

#define	pte_load_clear(pte)	atomic_readandclear_long(pte)

/*
 * Pmap stuff
 */
struct pmap;
struct pv_entry;
struct vm_page;
struct vm_object;
struct vmspace;

/*
 * vm_page structure extension for pmap.  Track the number of pmap mappings
 * for a managed page.  Unmanaged pages do not use this field.
 */
struct md_page {
	long interlock_count;
	long writeable_count_unused;
};

#define MD_PAGE_FREEABLE(m)	\
	(((m)->flags & (PG_MAPPED | PG_WRITEABLE)) == 0)

/*
 * vm_object's representing large mappings can contain embedded pmaps
 * to organize sharing at higher page table levels for PROT_READ and
 * PROT_READ|PROT_WRITE maps.
 */
struct md_object {
	void *dummy_unused;
};

/*
 * Each machine dependent implementation is expected to
 * keep certain statistics.  They may do this anyway they
 * so choose, but are expected to return the statistics
 * in the following structure.
 *
 * NOTE: We try to match the size of the pc32 pmap with the vkernel pmap
 * so the same utilities (like 'ps') can be used on both.
 */
struct pmap_statistics {
	long resident_count;    /* # of pages mapped (total) */
	long wired_count;       /* # of pages wired */
};
typedef struct pmap_statistics *pmap_statistics_t;

struct pv_entry_rb_tree;
RB_PROTOTYPE2(pv_entry_rb_tree, pv_entry, pv_entry,
	      pv_entry_compare, vm_pindex_t);

/* Types of PMAP (regular, EPT Intel, NPT Amd) */
#define	REGULAR_PMAP		0
#define	EPT_PMAP		1

/* Bits indexes in pmap_bits */
#define	TYPE_IDX		0
#define	PG_V_IDX		1
#define	PG_RW_IDX		2
#define	PG_U_IDX		3
#define	PG_A_IDX		4
#define	PG_M_IDX		5
#define	PG_PS_IDX		6
#define	PG_G_IDX		7
#define	PG_W_IDX		8
#define	PG_MANAGED_IDX		9
#define	PG_UNUSED10_IDX		10
#define	PG_N_IDX		11
#define	PG_NX_IDX		12
#define	PG_BITS_SIZE		13

#define PROTECTION_CODES_SIZE	8
#define PAT_INDEX_SIZE  	8

#define PM_PLACEMARKS		64		/* 16 @ 4 zones */
#define PM_NOPLACEMARK		((vm_pindex_t)-1)
#define PM_PLACEMARK_WAKEUP	((vm_pindex_t)0x8000000000000000LLU)

struct pmap {
	pml4_entry_t		*pm_pml4;	/* KVA of level 4 page table */
	pml4_entry_t		*pm_pml4_iso;	/* (isolated version) */
	struct pv_entry		*pm_pmlpv;	/* PV entry for pml4 */
	struct pv_entry		*pm_pmlpv_iso;	/* (isolated version) */
	TAILQ_ENTRY(pmap)	pm_pmnode;	/* list of pmaps */
	RB_HEAD(pv_entry_rb_tree, pv_entry) pm_pvroot;
	int			pm_count;	/* reference count */
	cpulock_t		pm_active_lock; /* interlock */
	cpumask_t		pm_active;	/* active on cpus */
	int			pm_flags;
	uint32_t		pm_softhold;
	struct pmap_statistics	pm_stats;	/* pmap statistics */
	struct spinlock		pm_spin;
	struct pv_entry		*pm_pvhint_pt;	/* pv_entry lookup hint */
	struct pv_entry		*pm_pvhint_unused;
	vm_pindex_t		pm_placemarks[PM_PLACEMARKS];
	long			pm_invgen;
	uint64_t		pmap_bits[PG_BITS_SIZE];
	uint64_t		protection_codes[PROTECTION_CODES_SIZE];
	pt_entry_t		pmap_cache_bits_pte[PAT_INDEX_SIZE];
	pt_entry_t		pmap_cache_bits_pde[PAT_INDEX_SIZE];
	pt_entry_t		pmap_cache_mask_pte;
	pt_entry_t		pmap_cache_mask_pde;
	int (*copyinstr)(const void *, void *, size_t, size_t *);
	int (*copyin)(const void *, void *, size_t);
	int (*copyout)(const void *, void *, size_t);
	int (*fubyte)(const uint8_t *);		/* returns int for -1 err */
	int (*subyte)(uint8_t *, uint8_t);
	int32_t (*fuword32)(const uint32_t *);
	int64_t (*fuword64)(const uint64_t *);
	int (*suword64)(uint64_t *, uint64_t);
	int (*suword32)(uint32_t *, int);
	uint32_t (*swapu32)(volatile uint32_t *, uint32_t v);
	uint64_t (*swapu64)(volatile uint64_t *, uint64_t v);
	uint32_t (*fuwordadd32)(volatile uint32_t *, uint32_t v);
	uint64_t (*fuwordadd64)(volatile uint64_t *, uint64_t v);
};

#define PMAP_FLAG_SIMPLE	0x00000001
#define PMAP_EMULATE_AD_BITS	0x00000002
#define PMAP_HVM		0x00000004
#define PMAP_SEGSHARED		0x00000008	/* segment shared opt */
#define PMAP_MULTI		0x00000010	/* multi-threaded use */

#define pmap_resident_count(pmap) ((pmap)->pm_stats.resident_count)
#define pmap_resident_tlnw_count(pmap) ((pmap)->pm_stats.resident_count - \
					(pmap)->pm_stats.wired_count)

typedef struct pmap	*pmap_t;

#ifdef _KERNEL
extern struct pmap	kernel_pmap;
#endif

/*
 * The pv_entry structure is used to track higher levels of the page table.
 * The leaf PTE is no longer tracked with this structure.
 */
typedef struct pv_entry {
	pmap_t		pv_pmap;	/* pmap where mapping lies */
	vm_pindex_t	pv_pindex;	/* PTE, PT, PD, PDP, or PML4 */
	RB_ENTRY(pv_entry) pv_entry;
	struct vm_page	*pv_m;		/* page being mapped */
	u_int		pv_hold;	/* interlock action */
	u_int		pv_flags;
#ifdef PMAP_DEBUG
	const char	*pv_func;
	int		pv_line;
	const char	*pv_func_lastfree;
	int		pv_line_lastfree;
#endif
} *pv_entry_t;

#define PV_HOLD_LOCKED		0x80000000U
#define PV_HOLD_WAITING		0x40000000U
#define PV_HOLD_UNUSED2000	0x20000000U
#define PV_HOLD_MASK		0x1FFFFFFFU

#define PV_FLAG_UNUSED01	0x00000001U
#define PV_FLAG_UNUSED02	0x00000002U

#ifdef	_KERNEL

extern caddr_t	CADDR1;
extern pt_entry_t *CMAP1;
extern vm_paddr_t avail_end;
extern vm_paddr_t avail_start;
extern vm_offset_t clean_eva;
extern vm_offset_t clean_sva;
extern char *ptvmmap;		/* poor name! */

#ifndef __VM_PAGE_T_DEFINED__
#define __VM_PAGE_T_DEFINED__
typedef struct vm_page *vm_page_t;
#endif
#ifndef __VM_MEMATTR_T_DEFINED__
#define __VM_MEMATTR_T_DEFINED__
typedef char vm_memattr_t;
#endif

void	pmap_release(struct pmap *pmap);
void	pmap_interlock_wait (struct vmspace *);
void	pmap_bootstrap (vm_paddr_t *);
void	*pmap_mapbios(vm_paddr_t, vm_size_t);
void	*pmap_mapdev (vm_paddr_t, vm_size_t);
void	*pmap_mapdev_attr(vm_paddr_t, vm_size_t, int);
void	*pmap_mapdev_uncacheable(vm_paddr_t, vm_size_t);
void	pmap_page_set_memattr(vm_page_t m, vm_memattr_t ma);
void	pmap_unmapdev (vm_offset_t, vm_size_t);
struct vm_page *pmap_use_pt (pmap_t, vm_offset_t);
void	pmap_set_opt (void);
void	pmap_init_pat(void);
void	pmap_invalidate_cache_pages(vm_page_t *pages, int count);
void	pmap_invalidate_cache_range(vm_offset_t sva, vm_offset_t eva);

static __inline int
pmap_emulate_ad_bits(pmap_t pmap) {
	return pmap->pm_flags & PMAP_EMULATE_AD_BITS;
}

/* Return various clipped indexes for a given VA */

/*
 * Returns the index of a PTE in a PT, representing a terminal
 * page.
 */
static __inline vm_pindex_t
pmap_pte_index(vm_offset_t va)
{
	return ((va >> PAGE_SHIFT) & ((1ul << NPTEPGSHIFT) - 1));
}

/*
 * Returns the index of a PT in a PD
 */
static __inline vm_pindex_t
pmap_pde_index(vm_offset_t va)
{
	return ((va >> PDRSHIFT) & ((1ul << NPDEPGSHIFT) - 1));
}

/*
 * Returns the index of a PD in a PDP
 */
static __inline vm_pindex_t
pmap_pdpe_index(vm_offset_t va)
{
	return ((va >> PDPSHIFT) & ((1ul << NPDPEPGSHIFT) - 1));
}

/*
 * Returns the index of a PDP in the PML4
 */
static __inline vm_pindex_t
pmap_pml4e_index(vm_offset_t va)
{
	return ((va >> PML4SHIFT) & ((1ul << NPML4EPGSHIFT) - 1));
}

#endif /* _KERNEL */

#endif /* !LOCORE */

#endif /* !_MACHINE_PMAP_H_ */
