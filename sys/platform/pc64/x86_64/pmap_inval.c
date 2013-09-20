/*
 * Copyright (c) 2003-2011 The DragonFly Project.  All rights reserved.
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
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/vmmeter.h>
#include <sys/thread2.h>

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

static void pmap_inval_callback(void *arg);

/*
 * Initialize for add or flush
 *
 * The critical section is required to prevent preemption, allowing us to
 * set CPUMASK_LOCK on the pmap.  The critical section is also assumed
 * when lwkt_process_ipiq() is called.
 */
void
pmap_inval_init(pmap_inval_info_t info)
{
    info->pir_flags = 0;
    crit_enter_id("inval");
}

/*
 * Add a (pmap, va) pair to the invalidation list and protect access
 * as appropriate.
 *
 * CPUMASK_LOCK is used to interlock thread switchins, otherwise another
 * cpu can switch in a pmap that we are unaware of and interfere with our
 * pte operation.
 */
void
pmap_inval_interlock(pmap_inval_info_t info, pmap_t pmap, vm_offset_t va)
{
    cpumask_t oactive;
    cpumask_t nactive;

    DEBUG_PUSH_INFO("pmap_inval_interlock");
    for (;;) {
	oactive = pmap->pm_active;
	cpu_ccfence();
	nactive = oactive | CPUMASK_LOCK;
	if ((oactive & CPUMASK_LOCK) == 0 &&
	    atomic_cmpset_cpumask(&pmap->pm_active, oactive, nactive)) {
		break;
	}
	lwkt_process_ipiq();
	cpu_pause();
    }
    DEBUG_POP_INFO();
    KKASSERT((info->pir_flags & PIRF_CPUSYNC) == 0);

    info->pir_va = va;
    info->pir_flags = PIRF_CPUSYNC;
    lwkt_cpusync_init(&info->pir_cpusync, oactive, pmap_inval_callback, info);
    lwkt_cpusync_interlock(&info->pir_cpusync);
    atomic_add_acq_long(&pmap->pm_invgen, 1);
}

void
pmap_inval_invltlb(pmap_inval_info_t info)
{
	info->pir_va = (vm_offset_t)-1;
}

void
pmap_inval_deinterlock(pmap_inval_info_t info, pmap_t pmap)
{
    KKASSERT(info->pir_flags & PIRF_CPUSYNC);
    atomic_clear_cpumask(&pmap->pm_active, CPUMASK_LOCK);
    lwkt_cpusync_deinterlock(&info->pir_cpusync);
    info->pir_flags = 0;
}

static void
pmap_inval_callback(void *arg)
{
    pmap_inval_info_t info = arg;

    if (info->pir_va == (vm_offset_t)-1)
	cpu_invltlb();
    else
	cpu_invlpg((void *)info->pir_va);
}

void
pmap_inval_done(pmap_inval_info_t info)
{
    KKASSERT((info->pir_flags & PIRF_CPUSYNC) == 0);
    crit_exit_id("inval");
}

