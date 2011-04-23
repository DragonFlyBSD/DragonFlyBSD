/*
 * Copyright (c) 1996, by Steve Passe
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. The name of the developer may NOT be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/i386/include/mpapic.h,v 1.14.2.2 2000/09/30 02:49:34 ps Exp $
 * $DragonFly: src/sys/platform/pc32/apic/mpapic.h,v 1.12 2008/06/07 11:37:23 mneumann Exp $
 */

#ifndef _MACHINE_MPAPIC_H_
#define _MACHINE_MPAPIC_H_

#include "apicreg.h"

#include <machine_base/icu/icu.h>

/*
 * Size of APIC ID list.
 * Also used a MAX size of various other arrays.
 */
#define NAPICID		256

/* these don't really belong in here... */
enum busTypes {
    CBUS = 1,
    CBUSII = 2,
    EISA = 3,
    MCA = 4,
    ISA = 6,
    PCI = 13,
    XPRESS = 18,
    MAX_BUSTYPE = 18,
    UNKNOWN_BUSTYPE = 0xff
};


/*
 * the physical/logical APIC ID management macros
 */
#define CPU_TO_ID(CPU)	(cpu_num_to_apic_id[CPU])
#define ID_TO_CPU(ID)	(apic_id_to_logical[ID])

#ifdef SMP

/*
 * send an IPI INTerrupt containing 'vector' to all CPUs EXCEPT myself
 */
static __inline int
all_but_self_ipi(int vector)
{
	if (smp_active_mask == 1)
		return 0;
	return apic_ipi(APIC_DEST_ALLESELF, vector, APIC_DELMODE_FIXED);
}

#endif

void	lapic_map(vm_offset_t /* XXX should be vm_paddr_t */);

#endif /* _MACHINE_MPAPIC_H */
