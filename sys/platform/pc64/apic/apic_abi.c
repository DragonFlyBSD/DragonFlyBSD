/*
 * Copyright (c) 1991 The Regents of the University of California.
 * Copyright (c) 1996, by Steve Passe.  All rights reserved.
 * Copyright (c) 2005,2008 The DragonFly Project.  All rights reserved.
 * All rights reserved.
 * 
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
 *
 * This code is derived from software contributed to Berkeley by
 * William Jolitz.
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
 * $DragonFly: src/sys/platform/pc64/apic/apic_abi.c,v 1.1 2008/08/29 17:07:12 dillon Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/machintr.h>
#include <sys/interrupt.h>
#include <sys/bus.h>

#include <machine/smp.h>
#include <machine/segments.h>
#include <machine/md_var.h>
#include <machine/clock.h>	/* apic_8254_intr */
#include <machine_base/isa/intr_machdep.h>
#include <machine_base/icu/icu.h>
#include <machine/globaldata.h>

#include <sys/thread2.h>

#include "apic_ipl.h"

#ifdef SMP /* APIC-IO */

extern void APIC_INTREN(int);
extern void APIC_INTRDIS(int);

extern inthand_t
	IDTVEC(apic_fastintr0), IDTVEC(apic_fastintr1),
	IDTVEC(apic_fastintr2), IDTVEC(apic_fastintr3),
	IDTVEC(apic_fastintr4), IDTVEC(apic_fastintr5),
	IDTVEC(apic_fastintr6), IDTVEC(apic_fastintr7),
	IDTVEC(apic_fastintr8), IDTVEC(apic_fastintr9),
	IDTVEC(apic_fastintr10), IDTVEC(apic_fastintr11),
	IDTVEC(apic_fastintr12), IDTVEC(apic_fastintr13),
	IDTVEC(apic_fastintr14), IDTVEC(apic_fastintr15),
	IDTVEC(apic_fastintr16), IDTVEC(apic_fastintr17),
	IDTVEC(apic_fastintr18), IDTVEC(apic_fastintr19),
	IDTVEC(apic_fastintr20), IDTVEC(apic_fastintr21),
	IDTVEC(apic_fastintr22), IDTVEC(apic_fastintr23);

static int apic_setvar(int, const void *);
static int apic_getvar(int, void *);
static int apic_vectorctl(int, int, int);
static void apic_finalize(void);
static void apic_cleanup(void);

static inthand_t *apic_fastintr[APIC_HWI_VECTORS] = {
	&IDTVEC(apic_fastintr0), &IDTVEC(apic_fastintr1),
	&IDTVEC(apic_fastintr2), &IDTVEC(apic_fastintr3),
	&IDTVEC(apic_fastintr4), &IDTVEC(apic_fastintr5),
	&IDTVEC(apic_fastintr6), &IDTVEC(apic_fastintr7),
	&IDTVEC(apic_fastintr8), &IDTVEC(apic_fastintr9),
	&IDTVEC(apic_fastintr10), &IDTVEC(apic_fastintr11),
	&IDTVEC(apic_fastintr12), &IDTVEC(apic_fastintr13),
	&IDTVEC(apic_fastintr14), &IDTVEC(apic_fastintr15),
	&IDTVEC(apic_fastintr16), &IDTVEC(apic_fastintr17),
	&IDTVEC(apic_fastintr18), &IDTVEC(apic_fastintr19),
	&IDTVEC(apic_fastintr20), &IDTVEC(apic_fastintr21),
	&IDTVEC(apic_fastintr22), &IDTVEC(apic_fastintr23)
};

static int apic_imcr_present;

struct machintr_abi MachIntrABI_APIC = {
	MACHINTR_APIC,
	.intrdis =	APIC_INTRDIS,
	.intren =	APIC_INTREN,
	.vectorctl =	apic_vectorctl,
	.setvar =	apic_setvar,
	.getvar =	apic_getvar,
	.finalize =	apic_finalize,
	.cleanup =	apic_cleanup
};

static int
apic_setvar(int varid, const void *buf)
{
    int error = 0;

    switch(varid) {
    case MACHINTR_VAR_IMCR_PRESENT:
	apic_imcr_present = *(const int *)buf;
	break;
    default:
	error = ENOENT;
	break;
    }
    return (error);
}

static int
apic_getvar(int varid, void *buf)
{
    int error = 0;

    switch(varid) {
    case MACHINTR_VAR_IMCR_PRESENT:
	*(int *)buf = apic_imcr_present;
	break;
    default:
	error = ENOENT;
	break;
    }
    return (error);
}

/*
 * Called before interrupts are physically enabled, this routine does the
 * final configuration of the BSP's local APIC:
 *
 *  - disable 'pic mode'.
 *  - disable 'virtual wire mode'.
 *  - enable NMI.
 */
static void
apic_finalize(void)
{
    u_int32_t	temp;

    /*
     * If an IMCR is present, program bit 0 to disconnect the 8259
     * from the BSP.  The 8259 may still be connected to LINT0 on
     * the BSP's LAPIC.
     */
    if (apic_imcr_present) {
	outb(0x22, 0x70);	/* select IMCR */
	outb(0x23, 0x01);	/* disconnect 8259 */
    }

    /*
     * Setup lint0 (the 8259 'virtual wire' connection).  We
     * mask the interrupt, completing the disconnection of the
     * 8259.
     */
    temp = lapic->lvt_lint0;
    temp |= APIC_LVT_MASKED;
    lapic->lvt_lint0 = temp;

    /*
     * setup lint1 to handle an NMI 
     */
    temp = lapic->lvt_lint1;
    temp &= ~APIC_LVT_MASKED;
    lapic->lvt_lint1 = temp;

    if (bootverbose)
	apic_dump("bsp_apic_configure()");
}

/*
 * This routine is called after physical interrupts are enabled but before
 * the critical section is released.  We need to clean out any interrupts
 * that had already been posted to the cpu.
 */
static void
apic_cleanup(void)
{
	mdcpu->gd_fpending = 0;
}

static
int
apic_vectorctl(int op, int intr, int flags)
{
    int error;
    int vector;
    int select;
    u_int32_t value;
    u_long ef;

    if (intr < 0 || intr >= APIC_HWI_VECTORS)
	return (EINVAL);

    ef = read_rflags();
    cpu_disable_intr();
    error = 0;

    switch(op) {
    case MACHINTR_VECTOR_SETUP:
	vector = TPR_FAST_INTS + intr;
	setidt(vector, apic_fastintr[intr], SDT_SYSIGT, SEL_KPL, 0);

	/*
	 * Now reprogram the vector in the IO APIC.  In order to avoid
	 * losing an EOI for a level interrupt, which is vector based,
	 * make sure that the IO APIC is programmed for edge-triggering
	 * first, then reprogrammed with the new vector.  This should
	 * clear the IRR bit.
	 */
	if (int_to_apicintpin[intr].ioapic >= 0) {
	    imen_lock();
	    select = int_to_apicintpin[intr].redirindex;
	    value = io_apic_read(int_to_apicintpin[intr].ioapic, select);
	    value |= IOART_INTMSET;
	    io_apic_write(int_to_apicintpin[intr].ioapic,
			  select, (value & ~APIC_TRIGMOD_MASK));
	    io_apic_write(int_to_apicintpin[intr].ioapic,
			  select, (value & ~IOART_INTVEC) | vector);
	    imen_unlock();
	}
	machintr_intren(intr);
	break;
    case MACHINTR_VECTOR_TEARDOWN:
	/*
	 * Teardown an interrupt vector.  The vector should already be
	 * installed in the cpu's IDT, but make sure.
	 */
	machintr_intrdis(intr);
	vector = TPR_FAST_INTS + intr;
	setidt(vector, apic_fastintr[intr], SDT_SYSIGT, SEL_KPL, 0);

	/*
	 * And then reprogram the IO APIC to point to the SLOW vector (it may
	 * have previously been pointed to the FAST version of the vector).
	 * This will allow us to keep track of spurious interrupts.
	 *
	 * In order to avoid losing an EOI for a level interrupt, which is
	 * vector based, make sure that the IO APIC is programmed for 
	 * edge-triggering first, then reprogrammed with the new vector.
	 * This should clear the IRR bit.
	 */
	if (int_to_apicintpin[intr].ioapic >= 0) {
	    imen_lock();
	    select = int_to_apicintpin[intr].redirindex;
	    value = io_apic_read(int_to_apicintpin[intr].ioapic, select);
	    io_apic_write(int_to_apicintpin[intr].ioapic,
			  select, (value & ~APIC_TRIGMOD_MASK));
	    io_apic_write(int_to_apicintpin[intr].ioapic,
			  select, (value & ~IOART_INTVEC) | vector);
	    imen_unlock();
	}
	break;
    case MACHINTR_VECTOR_SETDEFAULT:
	/*
	 * This is a just-in-case an int pin is running through the 8259
	 * when we don't expect it to, or an IO APIC pin somehow wound
	 * up getting enabled without us specifically programming it in
	 * this ABI.  Note that IO APIC pins are by default programmed
	 * to IDT_OFFSET + intr.
	 */
	vector = IDT_OFFSET + intr;
	setidt(vector, apic_fastintr[intr], SDT_SYSIGT, SEL_KPL, 0);
	break;
    default:
	error = EOPNOTSUPP;
	break;
    }

    write_rflags(ef);
    return (error);
}

#endif

