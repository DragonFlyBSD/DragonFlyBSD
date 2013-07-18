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
 * NOTE: We no longer hardwire NKPT, it is calculated in create_pagetables()
 */
#define NKPML4E		1		/* number of kernel PML4 slots */
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
 * Number of 512G dmap PML4 slots (max ~254 or so but don't go over 64,
 * which gives us 32TB of ram).  Because we cache free, empty pmaps the
 * initialization overhead is minimal.
 *
 * It should be possible to bump this up to 255 (but not 256), which would
 * be able to address a maximum of ~127TB of physical ram.
 */
#define	NDMPML4E	64

/*
 * The *PML4I values control the layout of virtual memory.  Each PML4
 * entry represents 512G.
 */
#define	PML4PML4I	(NPML4EPG/2)	/* Index of recursive pml4 mapping */

#define	KPML4I		(NPML4EPG-1)	/* Top 512GB for KVM */
#define	DMPML4I		(KPML4I-NDMPML4E) /* Next 512GBxN down for dmap */

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
#define	MPPML4I		KPML4I
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
#endif

#ifdef _KERNEL

/*
 * XXX
 */
#define	vtophys(va)	pmap_kextract(((vm_offset_t)(va)))
#define	vtophys_pte(va)	((pt_entry_t)pmap_kextract(((vm_offset_t)(va))))

#endif

#define	pte_load_clear(pte)	atomic_readandclear_long(pte)

static __inline void
pte_store(pt_entry_t *ptep, pt_entry_t pte)
{
	*ptep = pte;
}

#define	pde_store(pdep, pde)	pte_store((pdep), (pde))

/*
 * Pmap stuff
 */
struct pmap;
struct pv_entry;
struct vm_page;
struct vm_object;
struct vmspace;

/*
 * vm_page structures embed a list of related pv_entry's
 */
struct md_page {
	TAILQ_HEAD(,pv_entry)	pv_list;
};

/*
 * vm_object's representing large mappings can contain embedded pmaps
 * to organize sharing at higher page table levels for PROT_READ and
 * PROT_READ|PROT_WRITE maps.
 */
struct md_object {
	struct pmap *pmap_rw;
	struct pmap *pmap_ro;
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

struct pmap {
	pml4_entry_t		*pm_pml4;	/* KVA of level 4 page table */
	struct pv_entry		*pm_pmlpv;	/* PV entry for pml4 */
	TAILQ_ENTRY(pmap)	pm_pmnode;	/* list of pmaps */
	RB_HEAD(pv_entry_rb_tree, pv_entry) pm_pvroot;
	int			pm_count;	/* reference count */
	cpumask_t		pm_active;	/* active on cpus */
	int			pm_flags;
	struct pmap_statistics	pm_stats;	/* pmap statistics */
	struct pv_entry		*pm_pvhint;	/* pv_entry lookup hint */
	int			pm_generation;	/* detect pvlist deletions */
	struct spinlock		pm_spin;
	struct lwkt_token	pm_token;
};

#define CPUMASK_LOCK		CPUMASK(SMP_MAXCPU)
#define CPUMASK_BIT		SMP_MAXCPU	/* for 1LLU << SMP_MAXCPU */

#define PMAP_FLAG_SIMPLE	0x00000001

#define pmap_resident_count(pmap) (pmap)->pm_stats.resident_count

typedef struct pmap	*pmap_t;

#ifdef _KERNEL
extern struct pmap	kernel_pmap;
#endif

/*
 * For each vm_page_t, there is a list of all currently valid virtual
 * mappings of that page.  An entry is a pv_entry_t, the list is pv_table.
 */
typedef struct pv_entry {
	pmap_t		pv_pmap;	/* pmap where mapping lies */
	vm_pindex_t	pv_pindex;	/* PTE, PT, PD, PDP, or PML4 */
	TAILQ_ENTRY(pv_entry)	pv_list;
	RB_ENTRY(pv_entry)	pv_entry;
	struct vm_page	*pv_m;		/* page being mapped */
	u_int		pv_hold;	/* interlock action */
	u_int		pv_flags;
#ifdef PMAP_DEBUG
	const char	*pv_func;
	int		pv_line;
#endif
} *pv_entry_t;

#define PV_HOLD_LOCKED		0x80000000U
#define PV_HOLD_WAITING		0x40000000U
#define PV_HOLD_DELETED		0x20000000U
#define PV_HOLD_MASK		0x1FFFFFFFU

#define PV_FLAG_VMOBJECT	0x00000001U	/* shared pt in VM obj */

#ifdef	_KERNEL

extern caddr_t	CADDR1;
extern pt_entry_t *CMAP1;
extern vm_paddr_t dump_avail[];
extern vm_paddr_t avail_end;
extern vm_paddr_t avail_start;
extern vm_offset_t clean_eva;
extern vm_offset_t clean_sva;
extern char *ptvmmap;		/* poor name! */

void	pmap_release(struct pmap *pmap);
void	pmap_interlock_wait (struct vmspace *);
void	pmap_bootstrap (vm_paddr_t *);
void	*pmap_mapbios(vm_paddr_t, vm_size_t);
void	*pmap_mapdev (vm_paddr_t, vm_size_t);
void	*pmap_mapdev_attr(vm_paddr_t, vm_size_t, int);
void	*pmap_mapdev_uncacheable(vm_paddr_t, vm_size_t);
void	pmap_unmapdev (vm_offset_t, vm_size_t);
struct vm_page *pmap_use_pt (pmap_t, vm_offset_t);
void	pmap_set_opt (void);
void	pmap_init_pat(void);
vm_paddr_t pmap_kextract(vm_offset_t);
void	pmap_invalidate_range(pmap_t, vm_offset_t, vm_offset_t);
typedef struct vm_page *vm_page_t;
void	pmap_invalidate_cache_pages(vm_page_t *pages, int count);
void	pmap_invalidate_cache_range(vm_offset_t sva, vm_offset_t eva);

#endif /* _KERNEL */

#endif /* !LOCORE */

#endif /* !_MACHINE_PMAP_H_ */
