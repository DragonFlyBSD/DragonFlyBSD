/*
 * Copyright (c) 2005 The DragonFly Project.  All rights reserved.
 * Copyright (c) 1991 The Regents of the University of California.
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
 * $DragonFly: src/sys/platform/pc32/icu/icu_abi.c,v 1.8 2005/11/04 19:46:09 dillon Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/machintr.h>
#include <sys/interrupt.h>
#include <sys/bus.h>

#include <machine/segments.h>
#include <machine/md_var.h>
#include <i386/isa/intr_machdep.h>

#include "icu.h"
#include "icu_ipl.h"

#ifndef APIC_IO

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

extern inthand_t
	IDTVEC(icu_slowintr0), IDTVEC(icu_slowintr1),
	IDTVEC(icu_slowintr2), IDTVEC(icu_slowintr3),
	IDTVEC(icu_slowintr4), IDTVEC(icu_slowintr5),
	IDTVEC(icu_slowintr6), IDTVEC(icu_slowintr7),
	IDTVEC(icu_slowintr8), IDTVEC(icu_slowintr9),
	IDTVEC(icu_slowintr10), IDTVEC(icu_slowintr11),
	IDTVEC(icu_slowintr12), IDTVEC(icu_slowintr13),
	IDTVEC(icu_slowintr14), IDTVEC(icu_slowintr15);

static int icu_vectorctl(int, int, int);
static int icu_setvar(int, const void *);
static int icu_getvar(int, void *);
static void icu_finalize(void);

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

static inthand_t *icu_slowintr[ICU_HWI_VECTORS] = {
	&IDTVEC(icu_slowintr0), &IDTVEC(icu_slowintr1),
	&IDTVEC(icu_slowintr2), &IDTVEC(icu_slowintr3),
	&IDTVEC(icu_slowintr4), &IDTVEC(icu_slowintr5),
	&IDTVEC(icu_slowintr6), &IDTVEC(icu_slowintr7),
	&IDTVEC(icu_slowintr8), &IDTVEC(icu_slowintr9),
	&IDTVEC(icu_slowintr10), &IDTVEC(icu_slowintr11),
	&IDTVEC(icu_slowintr12), &IDTVEC(icu_slowintr13),
	&IDTVEC(icu_slowintr14), &IDTVEC(icu_slowintr15)
};

struct machintr_abi MachIntrABI = {
    MACHINTR_ICU,
    ICU_INTRDIS,
    ICU_INTREN,
    icu_vectorctl,
    icu_setvar,
    icu_getvar,
    icu_finalize
};

static int icu_imcr_present;

/*
 * WARNING!  SMP builds can use the ICU now so this code must be MP safe.
 */

static 
int
icu_setvar(int varid __unused, const void *buf __unused)
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
icu_getvar(int varid __unused, void *buf __unused)
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

static void
icu_finalize(void)
{
    machintr_intren(ICU_IRQ_SLAVE);

    /*
     * If an IMCR is present, programming bit 0 disconnects the 8259
     * from the BSP.  The 8259 may still be connected to LINT0 on the BSP's
     * LAPIC.
     *
     * If we are running SMP the LAPIC is active and we want to use virtual
     * wire mode so we can use other interrupt sources within the LAPIC.
     *
     * If we are not running SMP the 8259 must be directly connected to the
     * BSP and we program the IMCR to 0.
     */
    if (icu_imcr_present) {
#ifdef SMP
	outb(0x22, 0x70);
	outb(0x23, 0x01);
#else
	outb(0x22, 0x70);
	outb(0x23, 0x00);
#endif
    }
}

static
int
icu_vectorctl(int op, int intr, int flags)
{
    int error;
    u_long ef;

    if (intr < 0 || intr >= ICU_HWI_VECTORS || intr == ICU_IRQ_SLAVE)
	return (EINVAL);

    ef = read_eflags();
    cpu_disable_intr();
    error = 0;

    switch(op) {
    case MACHINTR_VECTOR_SETUP:
	setidt(IDT_OFFSET + intr,
		flags & INTR_FAST ? icu_fastintr[intr] : icu_slowintr[intr],
		SDT_SYS386IGT, SEL_KPL, GSEL(GCODE_SEL, SEL_KPL));
	machintr_intren(intr);
	break;
    case MACHINTR_VECTOR_TEARDOWN:
    case MACHINTR_VECTOR_SETDEFAULT:
	setidt(IDT_OFFSET + intr, icu_slowintr[intr], SDT_SYS386IGT, SEL_KPL,
		GSEL(GCODE_SEL, SEL_KPL));
	machintr_intrdis(intr);
	break;
    default:
	error = EOPNOTSUPP;
	break;
    }
    write_eflags(ef);
    return (error);
}

#endif
