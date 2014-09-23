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

#include <sys/mman.h>
#include <sys/vmspace.h>

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
	*ptep = 0;
	pmap_inval_cpu(pmap, va, PAGE_SIZE);
	*ptep = 0;
}

/*
 * Same as pmap_inval_pte() but only synchronize with the current
 * cpu.  For the moment its the same as the non-quick version.
 */
void
pmap_inval_pte_quick(volatile vpte_t *ptep, struct pmap *pmap, vm_offset_t va)
{
	*ptep = 0;
	pmap_inval_cpu(pmap, va, PAGE_SIZE);
	*ptep = 0;
}

/*
 * Invalidating page directory entries requires some additional
 * sophistication.  The cachemask must be cleared so the kernel
 * resynchronizes its temporary page table mappings cache.
 */
void
pmap_inval_pde(volatile vpte_t *ptep, struct pmap *pmap, vm_offset_t va)
{
	*ptep = 0;
	pmap_inval_cpu(pmap, va, SEG_SIZE);
	*ptep = 0;
	pmap->pm_cpucachemask = 0;
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
vpte_t
pmap_clean_pte(volatile vpte_t *ptep, struct pmap *pmap, vm_offset_t va)
{
	vpte_t pte;

	pte = *ptep;
	if (pte & VPTE_V) {
		atomic_clear_long(ptep, VPTE_RW);
		pmap_inval_cpu(pmap, va, PAGE_SIZE);
		pte = *ptep;
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
		pmap_inval_cpu(pmap, va, SEG_SIZE);
		pte = *ptep;
		atomic_clear_long(ptep, VPTE_RW|VPTE_M);
		pmap->pm_cpucachemask = 0;
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

	pte = *ptep;
	if (pte & VPTE_V) {
		pte = *ptep;
		atomic_clear_long(ptep, VPTE_RW);
		pmap_inval_cpu(pmap, va, PAGE_SIZE);
		pte |= *ptep & VPTE_M;
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

	pte = *ptep;
	if (pte & VPTE_V) {
		pte = *ptep;
		atomic_clear_long(ptep, VPTE_RW);
		pmap_inval_cpu(pmap, va, PAGE_SIZE);
		pte |= *ptep & (VPTE_A | VPTE_M);
	}
	*ptep = 0;
	return(pte);
}
