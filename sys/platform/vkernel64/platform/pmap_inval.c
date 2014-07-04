/*
 * Copyright (c) 2003,2004 The DragonFly Project.  All rights reserved.
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

extern int vmm_enabled;

static __inline
void
vmm_cpu_invltlb(void)
{
	/* For VMM mode forces vmmexit/resume */
	uint64_t rax = -1;
	__asm __volatile("syscall;"
			:
			: "a" (rax)
			:);
}

/*
 * Invalidate va in the TLB on the current cpu
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
guest_sync_addr(struct pmap *pmap,
		volatile vpte_t *dst_ptep, volatile vpte_t *src_ptep)
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
	if (pmap->pm_active == 0 ||
	    pmap->pm_active == gd->gd_cpumask) {
		*dst_ptep = *src_ptep;
		vmm_cpu_invltlb();
	} else {
		vmm_guest_sync_addr(__DEVOLATILE(void *, dst_ptep),
				    __DEVOLATILE(void *, src_ptep));
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
		*ptep = 0;
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
	*ptep = 0;
	if (vmm_enabled)
		vmm_cpu_invltlb();
	else
		pmap_inval_cpu(pmap, va, PAGE_SIZE);
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
		*ptep = 0;
		pmap_inval_cpu(pmap, va, SEG_SIZE);
	} else if (CPUMASK_TESTMASK(pmap->pm_active,
				    mycpu->gd_other_cpus) == 0) {
		*ptep = 0;
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
 * These carefully handle interactions with other cpus and return
 * the original vpte.  Clearing VPTE_RW prevents us from racing the
 * setting of VPTE_M, allowing us to invalidate the tlb (the real cpu's
 * pmap) and get good status for VPTE_M.
 *
 * When messing with page directory entries we have to clear the cpu
 * mask to force a reload of the kernel's page table mapping cache.
 *
 * clean: clear VPTE_M and VPTE_RW
 * setro: clear VPTE_RW
 * load&clear: clear entire field
 */
#include<stdio.h>
vpte_t
pmap_clean_pte(volatile vpte_t *ptep, struct pmap *pmap, vm_offset_t va)
{
	vpte_t pte;

	pte = *ptep;
	if (pte & VPTE_V) {
		atomic_clear_long(ptep, VPTE_RW);  /* XXX */
		if (vmm_enabled == 0) {
			pmap_inval_cpu(pmap, va, PAGE_SIZE);
			pte = *ptep;
		} else {
			guest_sync_addr(pmap, &pte, ptep);
		}
		atomic_clear_long(ptep, VPTE_RW|VPTE_M);
	}
	return(pte);
}

vpte_t
pmap_clean_pde(volatile vpte_t *ptep, struct pmap *pmap, vm_offset_t va)
{
	vpte_t pte;

	pte = *ptep;
	if (pte & VPTE_V) {
		atomic_clear_long(ptep, VPTE_RW);
		if (vmm_enabled == 0) {
			pmap_inval_cpu(pmap, va, SEG_SIZE);
			pte = *ptep;
		} else {
			guest_sync_addr(pmap, &pte, ptep);
		}
		atomic_clear_long(ptep, VPTE_RW|VPTE_M);
	}
	return(pte);
}

/*
 * This is an odd case and I'm not sure whether it even occurs in normal
 * operation.  Turn off write access to the page, clean out the tlb
 * (the real cpu's pmap), and deal with any VPTE_M race that may have
 * occured.  VPTE_M is not cleared.
 */
vpte_t
pmap_setro_pte(volatile vpte_t *ptep, struct pmap *pmap, vm_offset_t va)
{
	vpte_t pte;
	vpte_t npte;

	pte = *ptep;
	if (pte & VPTE_V) {
		atomic_clear_long(ptep, VPTE_RW);
		if (vmm_enabled == 0) {
			pmap_inval_cpu(pmap, va, PAGE_SIZE);
			pte |= *ptep & VPTE_M;
		} else {
			guest_sync_addr(pmap, &npte, ptep);
			pte |= npte & VPTE_M;
		}
	}
	return(pte);
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
	vpte_t npte;

	pte = *ptep;
	if (pte & VPTE_V) {
		pte = *ptep;
		atomic_clear_long(ptep, VPTE_RW);
		if (vmm_enabled == 0) {
			pmap_inval_cpu(pmap, va, PAGE_SIZE);
			pte |= *ptep & (VPTE_A | VPTE_M);
		} else {
			guest_sync_addr(pmap, &npte, ptep);
			pte |= npte & (VPTE_A | VPTE_M);
		}
	}
	*ptep = 0;
	return(pte);
}

/*
 * Synchronize a kvm mapping originally made for the private use on
 * some other cpu so it can be used on all cpus.
 *
 * XXX add MADV_RESYNC to improve performance.
 *
 * We don't need to do anything because our pmap_inval_pte_quick()
 * synchronizes it immediately.
 */
void
pmap_kenter_sync(vm_offset_t va __unused)
{
}

void
cpu_invlpg(void *addr)
{
	if (vmm_enabled)
		vmm_cpu_invltlb(); /* For VMM mode forces vmmexit/resume */
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

void
smp_invltlb(void)
{
	/* XXX must invalidate the tlb on all cpus */
	/* at the moment pmap_inval_pte_quick */
	/* do nothing */
}
