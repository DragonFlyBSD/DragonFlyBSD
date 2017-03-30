/*
 * Copyright (c) 2003-2016 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
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
 * $DragonFly: src/sys/platform/vkernel/platform/pmap_inval.c,v 1.4 2007/07/02 02:22:58 dillon Exp $
 */

/*
 * pmap invalidation support code.  Certain hardware requirements must
 * be dealt with when manipulating page table entries and page directory
 * entries within a pmap.  In particular, we cannot safely manipulate
 * page tables which are in active use by another cpu (even if it is
 * running in userland) for two reasons: First, TLB writebacks will
 * race against our own modifications and tests.  Second, even if we
 * were to use bus-locked instruction we can still screw up the
 * target cpu's instruction pipeline due to Intel cpu errata.
 *
 * For our virtual page tables, the real kernel will handle SMP interactions
 * with pmaps that may be active on other cpus.  Even so, we have to be
 * careful about bit setting races particularly when we are trying to clean
 * a page and test the modified bit to avoid races where the modified bit
 * might get set after our poll but before we clear the field.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/vmmeter.h>
#include <sys/thread2.h>
#include <sys/cdefs.h>
#include <sys/mman.h>
#include <sys/vmspace.h>
#include <sys/vmm.h>

#include <vm/vm.h>
#include <vm/pmap.h>
#include <vm/vm_object.h>

#include <machine/cputypes.h>
#include <machine/md_var.h>
#include <machine/specialreg.h>
#include <machine/smp.h>
#include <machine/globaldata.h>
#include <machine/pmap.h>
#include <machine/pmap_inval.h>

#include <unistd.h>
#include <pthread.h>

#include <vm/vm_page2.h>

extern int vmm_enabled;

/*
 * Invalidate the TLB on the current cpu
 *
 * (VMM enabled only)
 */
static __inline
void
vmm_cpu_invltlb(void)
{
#if 0
	/* not directly supported */
	cpu_invltlb();
#else
	/* vmm_guest_sync_addr(NULL, NULL); */
	/* For VMM mode forces vmmexit/resume */
	uint64_t rax = -1;
	__asm __volatile("syscall;"
			:
			: "a" (rax)
			:);
#endif
}

static __inline
void
vmm_cpu_invlpg(void *addr __unused)
{
	vmm_cpu_invltlb();
}

/*
 * Invalidate va in the TLB on the current cpu
 *
 * (VMM disabled only)
 */
static __inline
void
pmap_inval_cpu(struct pmap *pmap, vm_offset_t va, size_t bytes)
{
	if (pmap == &kernel_pmap) {
		madvise((void *)va, bytes, MADV_INVAL);
	} else {
		vmspace_mcontrol(pmap, (void *)va, bytes, MADV_INVAL, 0);
	}
}

/*
 * This is a bit of a mess because we don't know what virtual cpus are
 * mapped to real cpus.  Basically try to optimize the degenerate cases
 * (primarily related to user processes with only one thread or only one
 * running thread), and shunt all the rest to the host cpu.  The host cpu
 * will invalidate all real cpu's the vkernel is running on.
 *
 * This can't optimize situations where a pmap is only mapped to some of
 * the virtual cpus, though shunting to the real host will still be faster
 * if the virtual kernel processes are running on fewer real-host cpus.
 * (And probably will be faster anyway since there's no round-trip signaling
 * overhead).
 *
 * NOTE: The critical section protects against preemption while the pmap
 *	 is locked, which could otherwise result in a deadlock.
 */
static __inline
void
guest_sync_addr(struct pmap *pmap, volatile vpte_t *ptep, vpte_t *srcv)
{
	globaldata_t gd = mycpu;
	cpulock_t olock;
	cpulock_t nlock;

	/*
	 * Lock the pmap
	 */
	crit_enter();
	for (;;) {
		olock = pmap->pm_active_lock;
		cpu_ccfence();
		if ((olock & CPULOCK_EXCL) == 0) {
			nlock = olock | CPULOCK_EXCL;
			if (atomic_cmpset_int(&pmap->pm_active_lock,
					      olock, nlock)) {
				break;
			}
		}
		cpu_pause();
		lwkt_process_ipiq();
		pthread_yield();
	}

	/*
	 * Update the pte and synchronize with other cpus.  If we can update
	 * it trivially, do so.
	 */
	if (CPUMASK_TESTZERO(pmap->pm_active) ||
	    CPUMASK_CMPMASKEQ(pmap->pm_active, gd->gd_cpumask)) {
		if (ptep)
			*srcv = atomic_swap_long(ptep, *srcv);
		vmm_cpu_invltlb();
	} else {
		vmm_guest_sync_addr(__DEVOLATILE(void *, ptep), srcv);
	}

	/*
	 * Unlock the pmap
	 */
	atomic_clear_int(&pmap->pm_active_lock, CPULOCK_EXCL);
	crit_exit();
}

/*
 * Invalidate a pte in a pmap and synchronize with target cpus
 * as required.  Throw away the modified and access bits.  Use
 * pmap_clean_pte() to do the same thing but also get an interlocked
 * modified/access status.
 *
 * Clearing the field first (basically clearing VPTE_V) prevents any
 * new races from occuring while we invalidate the TLB (i.e. the pmap
 * on the real cpu), then clear it again to clean out any race that
 * might have occured before the invalidation completed.
 */
void
pmap_inval_pte(volatile vpte_t *ptep, struct pmap *pmap, vm_offset_t va)
{
	vpte_t pte;

	if (vmm_enabled == 0) {
		atomic_swap_long(ptep, 0);
		pmap_inval_cpu(pmap, va, PAGE_SIZE);
	} else {
		pte = 0;
		guest_sync_addr(pmap, ptep, &pte);
	}
}

/*
 * Same as pmap_inval_pte() but only synchronize with the current
 * cpu.  For the moment its the same as the non-quick version.
 */
void
pmap_inval_pte_quick(volatile vpte_t *ptep, struct pmap *pmap, vm_offset_t va)
{
	atomic_swap_long(ptep, 0);
	if (vmm_enabled == 0)
		pmap_inval_cpu(pmap, va, PAGE_SIZE);
	else
		vmm_cpu_invltlb();
}

/*
 * Invalidate the tlb for a range of virtual addresses across all cpus
 * belonging to the pmap.
 */
void
pmap_invalidate_range(pmap_t pmap, vm_offset_t sva, vm_offset_t eva)
{
	if (vmm_enabled == 0) {
		pmap_inval_cpu(pmap, sva, eva - sva);
	} else {
		guest_sync_addr(pmap, NULL, NULL);
	}
}

/*
 * Invalidating page directory entries requires some additional
 * sophistication.  The cachemask must be cleared so the kernel
 * resynchronizes its temporary page table mappings cache.
 */
void
pmap_inval_pde(volatile vpte_t *ptep, struct pmap *pmap, vm_offset_t va)
{
	vpte_t pte;

	if (vmm_enabled == 0) {
		atomic_swap_long(ptep, 0);
		pmap_inval_cpu(pmap, va, SEG_SIZE);
	} else if (CPUMASK_TESTMASK(pmap->pm_active,
				    mycpu->gd_other_cpus) == 0) {
		atomic_swap_long(ptep, 0);
		vmm_cpu_invltlb();
	} else {
		pte = 0;
		guest_sync_addr(pmap, ptep, &pte);
	}
}

void
pmap_inval_pde_quick(volatile vpte_t *ptep, struct pmap *pmap, vm_offset_t va)
{
	pmap_inval_pde(ptep, pmap, va);
}

/*
 * This is really nasty.
 *
 * (1) The vkernel interlocks pte operations with the related vm_page_t
 *     spin-lock (and doesn't handle unmanaged page races).
 *
 * (2) The vkernel must also issu an invalidation to the real cpu.  It
 *     (nastily) does this while holding the spin-lock too.
 *
 * In addition, atomic ops must be used to properly interlock against
 * other cpus and the real kernel (which could be taking a fault on another
 * cpu and will adjust VPTE_M and VPTE_A appropriately).
 *
 * The atomicc ops do a good job of interlocking against other cpus, but
 * we still need to lock the pte location (which we use the vm_page spin-lock
 * for) to avoid races against PG_WRITEABLE and other tests.
 *
 * Cleaning the pte involves clearing VPTE_M and VPTE_RW, synchronizing with
 * the real host, and updating the vm_page appropriately.
 *
 * If the caller passes a non-NULL (m), the caller holds the spin-lock,
 * otherwise we must acquire and release the spin-lock.  (m) is only
 * applicable to managed pages.
 */
vpte_t
pmap_clean_pte(volatile vpte_t *ptep, struct pmap *pmap, vm_offset_t va,
	       vm_page_t m)
{
	vpte_t pte;
	int spin = 0;

	/*
	 * Acquire (m) and spin-lock it.
	 */
	while (m == NULL) {
		pte = *ptep;
		if ((pte & VPTE_V) == 0)
			return pte;
		if ((pte & VPTE_MANAGED) == 0)
			break;
		m = PHYS_TO_VM_PAGE(pte & VPTE_FRAME);
		vm_page_spin_lock(m);

		pte = *ptep;
		if ((pte & VPTE_V) == 0) {
			vm_page_spin_unlock(m);
			m = NULL;
			continue;
		}
		if ((pte & VPTE_MANAGED) == 0) {
			vm_page_spin_unlock(m);
			m = NULL;
			continue;
		}
		if (m != PHYS_TO_VM_PAGE(pte & VPTE_FRAME)) {
			vm_page_spin_unlock(m);
			m = NULL;
			continue;
		}
		spin = 1;
		break;
	}

	if (vmm_enabled == 0) {
		for (;;) {
			pte = *ptep;
			cpu_ccfence();
			if ((pte & VPTE_RW) == 0)
				break;
			if (atomic_cmpset_long(ptep,
					       pte,
					       pte & ~(VPTE_RW | VPTE_M))) {
				pmap_inval_cpu(pmap, va, PAGE_SIZE);
				break;
			}
		}
	} else {
		pte = *ptep & ~(VPTE_RW | VPTE_M);
		guest_sync_addr(pmap, ptep, &pte);
	}

	if (m) {
		if (pte & VPTE_A) {
			vm_page_flag_set(m, PG_REFERENCED);
			atomic_clear_long(ptep, VPTE_A);
		}
		if (pte & VPTE_M) {
			if (pmap_track_modified(pmap, va))
				vm_page_dirty(m);
		}
		if (spin)
			vm_page_spin_unlock(m);
	}
	return pte;
}

/*
 * This is a combination of pmap_inval_pte() and pmap_clean_pte().
 * Firts prevent races with the 'A' and 'M' bits, then clean out
 * the tlb (the real cpu's pmap), then incorporate any races that
 * may have occured in the mean time, and finally zero out the pte.
 */
vpte_t
pmap_inval_loadandclear(volatile vpte_t *ptep, struct pmap *pmap,
			vm_offset_t va)
{
	vpte_t pte;

	if (vmm_enabled == 0) {
		pte = atomic_swap_long(ptep, 0);
		pmap_inval_cpu(pmap, va, PAGE_SIZE);
	} else {
		pte = 0;
		guest_sync_addr(pmap, ptep, &pte);
	}
	return(pte);
}

void
cpu_invlpg(void *addr)
{
	if (vmm_enabled)
		vmm_cpu_invlpg(addr);
	else
		madvise(addr, PAGE_SIZE, MADV_INVAL);
}

void
cpu_invltlb(void)
{
	if (vmm_enabled)
		vmm_cpu_invltlb(); /* For VMM mode forces vmmexit/resume */
	else
		madvise((void *)KvaStart, KvaEnd - KvaStart, MADV_INVAL);
}

/*
 * Invalidate the TLB on all cpus.  Instead what the vkernel does is
 * ignore VM_PROT_NOSYNC on pmap_enter() calls.
 */
void
smp_invltlb(void)
{
	/* do nothing */
}

void
smp_sniff(void)
{
	/* not implemented */
}

void
cpu_sniff(int dcpu __unused)
{
	/* not implemented */
}
