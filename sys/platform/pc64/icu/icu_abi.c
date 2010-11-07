/*
 * Copyright (c) 1991 The Regents of the University of California.
 * Copyright (c) 2005,2008 The DragonFly Project.
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
 * $DragonFly: src/sys/platform/pc64/icu/icu_abi.c,v 1.1 2008/08/29 17:07:16 dillon Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/machintr.h>
#include <sys/interrupt.h>
#include <sys/bus.h>

#include <machine/segments.h>
#include <machine/md_var.h>
#include <machine_base/isa/intr_machdep.h>
#include <machine/globaldata.h>

#include <sys/thread2.h>

#include "icu.h"
#include "icu_ipl.h"

extern void ICU_INTREN(int);
extern void ICU_INTRDIS(int);

extern inthand_t
	IDTVEC(icu_fastintr0), IDTVEC(icu_fastintr1),
	IDTVEC(icu_fastintr2), IDTVEC(icu_fastintr3),
	IDTVEC(icu_fastintr4), IDTVEC(icu_fastintr5),
	IDTVEC(icu_fastintr6), IDTVEC(icu_fastintr7),
	IDTVEC(icu_fastintr8), IDTVEC(icu_fastintr9),
	IDTVEC(icu_fastintr10), IDTVEC(icu_fastintr11),
	IDTVEC(icu_fastintr12), IDTVEC(icu_fastintr13),
	IDTVEC(icu_fastintr14), IDTVEC(icu_fastintr15);

static int icu_vectorctl(int, int, int);
static int icu_setvar(int, const void *);
static int icu_getvar(int, void *);
static void icu_finalize(void);
static void icu_cleanup(void);

static inthand_t *icu_fastintr[ICU_HWI_VECTORS] = {
	&IDTVEC(icu_fastintr0), &IDTVEC(icu_fastintr1),
	&IDTVEC(icu_fastintr2), &IDTVEC(icu_fastintr3),
	&IDTVEC(icu_fastintr4), &IDTVEC(icu_fastintr5),
	&IDTVEC(icu_fastintr6), &IDTVEC(icu_fastintr7),
	&IDTVEC(icu_fastintr8), &IDTVEC(icu_fastintr9),
	&IDTVEC(icu_fastintr10), &IDTVEC(icu_fastintr11),
	&IDTVEC(icu_fastintr12), &IDTVEC(icu_fastintr13),
	&IDTVEC(icu_fastintr14), &IDTVEC(icu_fastintr15)
};

struct machintr_abi MachIntrABI_ICU = {
    MACHINTR_ICU,
    .intrdis =	ICU_INTRDIS,
    .intren =	ICU_INTREN,
    .vectorctl =icu_vectorctl,
    .setvar =	icu_setvar,
    .getvar =	icu_getvar,
    .finalize =	icu_finalize,
    .cleanup =	icu_cleanup
};

static int icu_imcr_present;

/*
 * WARNING!  SMP builds can use the ICU now so this code must be MP safe.
 */
static 
int
icu_setvar(int varid, const void *buf)
{
    int error = 0;
	
    switch(varid) {
    case MACHINTR_VAR_IMCR_PRESENT:
	icu_imcr_present = *(const int *)buf;
	break;
    default:
	error = ENOENT;
	break;
    }
    return (error);
}

static
int
icu_getvar(int varid, void *buf)
{
    int error = 0;
	
    switch(varid) {
    case MACHINTR_VAR_IMCR_PRESENT:
	*(int *)buf = icu_imcr_present;
	break;
    default:
	error = ENOENT;
	break;
    }
    return (error);
}

/*
 * Called before interrupts are physically enabled
 */
static void
icu_finalize(void)
{
    int intr;

    for (intr = 0; intr < ICU_HWI_VECTORS; ++intr) {
	machintr_intrdis(intr);
    }
    machintr_intren(ICU_IRQ_SLAVE);

    /*
     * If an IMCR is present, programming bit 0 disconnects the 8259
     * from the BSP.  The 8259 may still be connected to LINT0 on the BSP's
     * LAPIC.
     *
     * If we are running SMP the LAPIC is active, try to use virtual wire
     * mode so we can use other interrupt sources within the LAPIC in
     * addition to the 8259.
     */
    if (icu_imcr_present) {
#if defined(SMP)
	outb(0x22, 0x70);
	outb(0x23, 0x01);
#endif
    }
}

/*
 * Called after interrupts physically enabled but before the
 * critical section is released.
 */
static
void
icu_cleanup(void)
{
	mdcpu->gd_fpending = 0;
}


static
int
icu_vectorctl(int op, int intr, int flags)
{
    int error;
    register_t ef;

    if (intr < 0 || intr >= ICU_HWI_VECTORS || intr == ICU_IRQ_SLAVE)
	return (EINVAL);

    ef = read_rflags();
    cpu_disable_intr();
    error = 0;

    switch(op) {
    case MACHINTR_VECTOR_SETUP:
	setidt(IDT_OFFSET + intr, icu_fastintr[intr], SDT_SYSIGT, SEL_KPL, 0);
	machintr_intren(intr);
	break;
    case MACHINTR_VECTOR_TEARDOWN:
    case MACHINTR_VECTOR_SETDEFAULT:
	setidt(IDT_OFFSET + intr, icu_fastintr[intr], SDT_SYSIGT, SEL_KPL, 0);
	machintr_intrdis(intr);
	break;
    default:
	error = EOPNOTSUPP;
	break;
    }
    write_rflags(ef);
    return (error);
}
