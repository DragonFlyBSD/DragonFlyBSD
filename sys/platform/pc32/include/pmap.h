/*
 * Copyright (c) 1991 Regents of the University of California.
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
 * (4MB each) to use for the kernel.  256 pages == 1 Gigabyte.
 * This **MUST** be a multiple of 4 (eg: 252, 256, 260, etc).
 *
 * DEFAULT CHANGED 256->384.  32-bit systems have used 256 (1G KVM, 3G UVM)
 * for many years, but all recent and continuing kernel development really
 * assumes that more KVM is available.  In particularly, swapcache can be
 * very effective on a 32-bit system with 64G or even more swap and 1GB of
 * KVM just isn't enough to manage it.  Numerous subsystems such as tmpfs
 * have higher overheads (e.g. when used with pourdriere), vnodes are fatter,
 * mbufs are bigger and we need more of them, and the list goes on.  Even
 * PCI space reservations have gone up due to Video subsystems.  On a
 * freshly booted system well over 512MB of KVM is already reserved before
 * the machine runs its first user process.
 *
 * The new default of 384 gives the kernel 1.5GB of KVM (2.5G UVM) and
 * relieves the pressure on kernel structures.  This almost doubles the
 * amount of KVM available to the kernel post-boot.
 */
#ifndef KVA_PAGES
#define KVA_PAGES	384
#endif

/*
 * PTE related macros
 */
#define VADDR(pdi, pti) ((vm_offset_t)(((pdi)<<PDRSHIFT)|((pti)<<PAGE_SHIFT)))

#ifndef NKPT
#define	NKPT		30			/* starting general kptds */
#endif

#ifndef NKPDE
#define NKPDE	(KVA_PAGES - 2)	/* max general kptds */
#endif
#if NKPDE > KVA_PAGES - 2
#error "Maximum NKPDE is KVA_PAGES - 2"
#endif

/*
 * The *PTDI values control the layout of virtual memory
 *
 * NPEDEPG	- number of pde's in the page directory (1024)
 * NKPDE	- max general kernel page table pages not including
 *		  special PTDs.  Typically KVA_PAGES minus the number
 *		  of special PTDs.
 *
 *	+---------------+ End of kernel memory
 *	|   APTDPTDI	| alt page table map for cpu 0
 *	+---------------+
 *	|    MPPTDI	| globaldata array
 *	+---------------+
 *	|		|
 *	|		|
 *	|		|
 *	|		| general kernel page table pages
 *	|		|
 *	|  KPTDI[NKPDE] |
 *	+---------------+ Start of kernel memory
 *	|    PTDPTDI	| self-mapping of current pmap
 *	+---------------+
 *
 * This typically places PTDPTDI at the index corresponding to VM address
 * (0xc0000000 - 4M) = bfc00000, and that is where PTmap[] is based for
 * the self-mapped page table.  PTD points to the self-mapped page
 * directory itself and any indexes >= KPTDI will correspond to the
 * common kernel page directory pages since all pmaps map the same ones.
 *
 * APTmap / APTDpde are now used by cpu 0 as its alternative page table
 * mapping via gd_GDMAP1 and GD_GDADDR1.  The remaining cpus allocate
 * their own dynamically.
 *
 * Even though the maps are per-cpu the PTD entries are stored in the
 * individual pmaps and obviously not replicated so each process pmap
 * essentially gets its own per-cpu cache (PxN) making for fairly efficient
 * access.
 *
 * UMAXPTDI	- highest inclusive ptd index for user space
 */
#define	APTDPTDI	(NPDEPG-1)	/* alt ptd entry that points to APTD */
#define MPPTDI		(APTDPTDI-1)	/* globaldata array ptd entry */
#define	KPTDI		(MPPTDI-NKPDE)	/* start of kernel virtual pde's */
#define	PTDPTDI		(KPTDI-1)	/* ptd entry that points to ptd! */
#define	UMAXPTDI	(PTDPTDI-1)	/* ptd entry for user space end */

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
extern pt_entry_t PTmap[], APTmap[];
extern pd_entry_t PTD[], APTD[], PTDpde, APTDpde;

extern pd_entry_t IdlePTD;	/* physical address of "Idle" state directory */
#endif

#ifdef _KERNEL
/*
 * virtual address to page table entry and
 * to physical address. Likewise for alternate address space.
 * Note: these work recursively, thus vtopte of a pte will give
 * the corresponding pde that in turn maps it.
 */
#define	vtopte(va)	(PTmap + i386_btop(va))

#define	avtopte(va)	(APTmap + i386_btop(va))

/*
 *	Routine:	pmap_kextract
 *	Function:
 *		Extract the physical page address associated
 *		kernel virtual address.
 */
static __inline vm_paddr_t
pmap_kextract(vm_offset_t va)
{
	vm_paddr_t pa;

	if ((pa = (vm_offset_t) PTD[va >> PDRSHIFT]) & PG_PS) {
		pa = (pa & ~(NBPDR - 1)) | (va & (NBPDR - 1));
	} else {
		pa = *(vm_offset_t *)vtopte(va);
		pa = (pa & PG_FRAME) | (va & PAGE_MASK);
	}
	return pa;
}

/*
 * XXX
 */
#define	vtophys(va)	pmap_kextract(((vm_offset_t)(va)))
#define	vtophys_pte(va)	((pt_entry_t)pmap_kextract(((vm_offset_t)(va))))

#endif

/*
 * Pmap stuff
 */
struct pv_entry;
struct vm_page;
struct vm_object;
struct vmspace;

TAILQ_HEAD(md_page_pv_list, pv_entry);

struct md_page {
	int pv_list_count;
	struct md_page_pv_list	pv_list;
};

struct md_object {
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

struct pmap {
	pd_entry_t		*pm_pdir;	/* KVA of page directory */
	struct vm_page		*pm_pdirm;	/* VM page for pg directory */
	struct vm_object	*pm_pteobj;	/* Container for pte's */
	TAILQ_ENTRY(pmap)	pm_pmnode;	/* list of pmaps */
	TAILQ_HEAD(,pv_entry)	pm_pvlist;	/* list of mappings in pmap */
	TAILQ_HEAD(,pv_entry)	pm_pvlist_free;	/* free mappings */
	int			pm_count;	/* reference count */
	cpumask_t		pm_active;	/* active on cpus */
	cpumask_t		pm_cached;	/* cached on cpus */
	int			pm_filler02;	/* (filler sync w/vkernel) */
	struct pmap_statistics	pm_stats;	/* pmap statistics */
	struct	vm_page		*pm_ptphint;	/* pmap ptp hint */
	int			pm_generation;	/* detect pvlist deletions */
	struct spinlock		pm_spin;
	struct lwkt_token	pm_token;
};

#define pmap_resident_count(pmap) (pmap)->pm_stats.resident_count

#define CPUMASK_LOCK		CPUMASK(SMP_MAXCPU)
#define CPUMASK_BIT		SMP_MAXCPU	/* 1 << SMP_MAXCPU */

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
	vm_offset_t	pv_va;		/* virtual address for mapping */
	TAILQ_ENTRY(pv_entry)	pv_list;
	TAILQ_ENTRY(pv_entry)	pv_plist;
	struct vm_page	*pv_ptem;	/* VM page for pte */
#ifdef PMAP_DEBUG
	struct vm_page	*pv_m;
#else
	void		*pv_dummy;	/* align structure to 32 bytes */
#endif
} *pv_entry_t;

#ifdef	_KERNEL

extern caddr_t	CADDR1;
extern pt_entry_t *CMAP1;
extern vm_paddr_t dump_avail[];
extern vm_paddr_t avail_end;
extern vm_paddr_t avail_start;
extern vm_offset_t clean_eva;
extern vm_offset_t clean_sva;
extern char *ptvmmap;		/* poor name! */

typedef struct vm_page *vm_page_t;
typedef char vm_memattr_t;

void	pmap_release(struct pmap *pmap);
void	pmap_interlock_wait (struct vmspace *);
void	pmap_bootstrap (vm_paddr_t, vm_paddr_t);
void	*pmap_mapbios(vm_paddr_t, vm_size_t);
void	*pmap_mapdev (vm_paddr_t, vm_size_t);
void	*pmap_mapdev_attr(vm_paddr_t, vm_size_t, int);
void	*pmap_mapdev_uncacheable (vm_paddr_t, vm_size_t);
void	pmap_page_set_memattr(vm_page_t m, vm_memattr_t ma);
void	pmap_unmapdev (vm_offset_t, vm_size_t);
unsigned *pmap_kernel_pte (vm_offset_t) __pure2;
struct vm_page *pmap_use_pt (pmap_t, vm_offset_t);
int	pmap_get_pgeflag(void);
void	pmap_set_opt (void);
void	pmap_init_pat(void);
void	pmap_invalidate_range(pmap_t, vm_offset_t, vm_offset_t);
void	pmap_invalidate_cache_pages(vm_page_t *pages, int count);
void	pmap_invalidate_cache_range(vm_offset_t sva, vm_offset_t eva);

static __inline int
pmap_emulate_ad_bits(pmap_t pmap) {
	return 0;
}

#endif /* _KERNEL */

#endif /* !LOCORE */

#endif /* !_MACHINE_PMAP_H_ */
