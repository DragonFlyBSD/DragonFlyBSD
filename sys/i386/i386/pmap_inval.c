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
 * $DragonFly: src/sys/i386/i386/Attic/pmap_inval.c,v 1.3 2004/07/16 05:48:29 dillon Exp $
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
#if defined(SMP) || defined(APIC_IO)
#include <machine/smp.h>
#include <machine/apicreg.h>
#endif /* SMP || APIC_IO */
#include <machine/globaldata.h>
#include <machine/pmap.h>
#include <machine/pmap_inval.h>

#ifdef SMP

static void
_cpu_invltlb(void *dummy)
{
    cpu_invltlb();
}

static void
_cpu_invl1pg(void *data)
{
    cpu_invlpg(data);
}

#endif

/*
 * Initialize for add or flush
 */
void
pmap_inval_init(pmap_inval_info_t info)
{
    info->pir_flags = 0;
}

/*
 * Add a (pmap, va) pair to the invalidation list and protect access
 * as appropriate.
 */
void
pmap_inval_add(pmap_inval_info_t info, pmap_t pmap, vm_offset_t va)
{
#ifdef SMP
    if ((info->pir_flags & PIRF_CPUSYNC) == 0) {
	info->pir_flags |= PIRF_CPUSYNC;
	info->pir_cpusync.cs_run_func = NULL;
	info->pir_cpusync.cs_fin1_func = NULL;
	info->pir_cpusync.cs_fin2_func = NULL;
	lwkt_cpusync_start(pmap->pm_active, &info->pir_cpusync);
    } else if (pmap->pm_active & ~info->pir_cpusync.cs_mask) {
	lwkt_cpusync_add(pmap->pm_active, &info->pir_cpusync);
    }
#else
    if (pmap->pm_active == 0)
	return;
#endif
    if ((info->pir_flags & (PIRF_INVLTLB|PIRF_INVL1PG)) == 0) {
	if (va == (vm_offset_t)-1) {
	    info->pir_flags |= PIRF_INVLTLB;
#ifdef SMP
	    info->pir_cpusync.cs_fin2_func = _cpu_invltlb;
#endif
	} else {
	    info->pir_flags |= PIRF_INVL1PG;
	    info->pir_cpusync.cs_data = (void *)va;
#ifdef SMP
	    info->pir_cpusync.cs_fin2_func = _cpu_invl1pg;
#endif
	}
    } else {
	info->pir_flags |= PIRF_INVLTLB;
#ifdef SMP
	info->pir_cpusync.cs_fin2_func = _cpu_invltlb;
#endif
    }
}

/*
 * Synchronize changes with target cpus.
 */
void
pmap_inval_flush(pmap_inval_info_t info)
{
#ifdef SMP
    if (info->pir_flags & PIRF_CPUSYNC)
	lwkt_cpusync_finish(&info->pir_cpusync);
#else
    if (info->pir_flags & PIRF_INVLTLB)
	cpu_invltlb();
    else if (info->pir_flags & PIRF_INVL1PG)
	cpu_invlpg(info->pir_cpusync.cs_data);
#endif
    info->pir_flags = 0;
}

