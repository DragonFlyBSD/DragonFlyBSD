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
 * $DragonFly: src/sys/platform/pc32/icu/icu_abi.c,v 1.14 2007/07/07 12:13:47 sephe Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/machintr.h>
#include <sys/interrupt.h>
#include <sys/bus.h>

#include <machine/segments.h>
#include <machine/md_var.h>
#include <machine/intr_machdep.h>
#include <machine/globaldata.h>
#include <machine/smp.h>

#include <sys/thread2.h>

#include <machine_base/isa/elcr_var.h>

#include "icu.h"
#include "icu_ipl.h"

extern inthand_t
	IDTVEC(icu_intr0),	IDTVEC(icu_intr1),
	IDTVEC(icu_intr2),	IDTVEC(icu_intr3),
	IDTVEC(icu_intr4),	IDTVEC(icu_intr5),
	IDTVEC(icu_intr6),	IDTVEC(icu_intr7),
	IDTVEC(icu_intr8),	IDTVEC(icu_intr9),
	IDTVEC(icu_intr10),	IDTVEC(icu_intr11),
	IDTVEC(icu_intr12),	IDTVEC(icu_intr13),
	IDTVEC(icu_intr14),	IDTVEC(icu_intr15);

static inthand_t *icu_intr[ICU_HWI_VECTORS] = {
	&IDTVEC(icu_intr0),	&IDTVEC(icu_intr1),
	&IDTVEC(icu_intr2),	&IDTVEC(icu_intr3),
	&IDTVEC(icu_intr4),	&IDTVEC(icu_intr5),
	&IDTVEC(icu_intr6),	&IDTVEC(icu_intr7),
	&IDTVEC(icu_intr8),	&IDTVEC(icu_intr9),
	&IDTVEC(icu_intr10),	&IDTVEC(icu_intr11),
	&IDTVEC(icu_intr12),	&IDTVEC(icu_intr13),
	&IDTVEC(icu_intr14),	&IDTVEC(icu_intr15)
};

static struct icu_irqmap {
	int			im_type;	/* ICU_IMT_ */
	enum intr_trigger	im_trig;
} icu_irqmaps[MAX_HARDINTS];	/* XXX MAX_HARDINTS may not be correct */

#define ICU_IMT_UNUSED		0	/* KEEP THIS */
#define ICU_IMT_RESERVED	1
#define ICU_IMT_LINE		2
#define ICU_IMT_SYSCALL		3

extern void	ICU_INTREN(int);
extern void	ICU_INTRDIS(int);

static int	icu_vectorctl(int, int, int);
static int	icu_setvar(int, const void *);
static int	icu_getvar(int, void *);
static void	icu_finalize(void);
static void	icu_cleanup(void);
static void	icu_setdefault(void);
static void	icu_stabilize(void);
static void	icu_initmap(void);
static void	icu_intr_config(int, enum intr_trigger, enum intr_polarity);

struct machintr_abi MachIntrABI_ICU = {
	MACHINTR_ICU,
	.intrdis	= ICU_INTRDIS,
	.intren		= ICU_INTREN,
	.vectorctl	= icu_vectorctl,
	.setvar		= icu_setvar,
	.getvar		= icu_getvar,
	.finalize	= icu_finalize,
	.cleanup	= icu_cleanup,
	.setdefault	= icu_setdefault,
	.stabilize	= icu_stabilize,
	.initmap	= icu_initmap,
	.intr_config	= icu_intr_config
};

/*
 * WARNING!  SMP builds can use the ICU now so this code must be MP safe.
 */
static int
icu_setvar(int varid, const void *buf)
{
	return ENOENT;
}

static int
icu_getvar(int varid, void *buf)
{
	return ENOENT;
}

/*
 * Called before interrupts are physically enabled
 */
static void
icu_stabilize(void)
{
	int intr;

	for (intr = 0; intr < ICU_HWI_VECTORS; ++intr)
		machintr_intrdis(intr);
	machintr_intren(ICU_IRQ_SLAVE);
}

/*
 * Called after interrupts physically enabled but before the
 * critical section is released.
 */
static void
icu_cleanup(void)
{
	bzero(mdcpu->gd_ipending, sizeof(mdcpu->gd_ipending));
}

/*
 * Called after stablize and cleanup; critical section is not
 * held and interrupts are not physically disabled.
 */
static void
icu_finalize(void)
{
	KKASSERT(MachIntrABI.type == MACHINTR_ICU);

#ifdef SMP
	KKASSERT(!apic_io_enable);

	/*
	 * If an IMCR is present, programming bit 0 disconnects the 8259
	 * from the BSP.  The 8259 may still be connected to LINT0 on the
	 * BSP's LAPIC.
	 *
	 * If we are running SMP the LAPIC is active, try to use virtual
	 * wire mode so we can use other interrupt sources within the LAPIC
	 * in addition to the 8259.
	 */
	if (imcr_present) {
		outb(0x22, 0x70);
		outb(0x23, 0x01);
	}
#endif	/* SMP */
}

static int
icu_vectorctl(int op, int intr, int flags)
{
	int error;
	u_long ef;

	if (intr < 0 || intr >= ICU_HWI_VECTORS || intr == ICU_IRQ_SLAVE)
		return EINVAL;

	ef = read_eflags();
	cpu_disable_intr();
	error = 0;

	switch (op) {
	case MACHINTR_VECTOR_SETUP:
		setidt(IDT_OFFSET + intr, icu_intr[intr], SDT_SYS386IGT,
		       SEL_KPL, GSEL(GCODE_SEL, SEL_KPL));
		machintr_intren(intr);
		break;

	case MACHINTR_VECTOR_TEARDOWN:
		machintr_intrdis(intr);
		setidt(IDT_OFFSET + intr, icu_intr[intr], SDT_SYS386IGT,
		       SEL_KPL, GSEL(GCODE_SEL, SEL_KPL));
		break;

	default:
		error = EOPNOTSUPP;
		break;
	}
	write_eflags(ef);
	return error;
}

static void
icu_setdefault(void)
{
	int intr;

	for (intr = 0; intr < ICU_HWI_VECTORS; ++intr) {
		if (intr == ICU_IRQ_SLAVE)
			continue;
		setidt(IDT_OFFSET + intr, icu_intr[intr], SDT_SYS386IGT,
		       SEL_KPL, GSEL(GCODE_SEL, SEL_KPL));
	}
}

static void
icu_initmap(void)
{
	int i;

	for (i = 0; i < ICU_HWI_VECTORS; ++i)
		icu_irqmaps[i].im_type = ICU_IMT_LINE;
	icu_irqmaps[ICU_IRQ_SLAVE].im_type = ICU_IMT_RESERVED;

	if (elcr_found) {
		for (i = 0; i < ICU_HWI_VECTORS; ++i)
			icu_irqmaps[i].im_trig = elcr_read_trigger(i);
	} else {
		for (i = 0; i < ICU_HWI_VECTORS; ++i) {
			switch (i) {
			case 0:
			case 1:
			case 2:
			case 8:
			case 13:
				icu_irqmaps[i].im_trig = INTR_TRIGGER_EDGE;
				break;

			default:
				icu_irqmaps[i].im_trig = INTR_TRIGGER_LEVEL;
				break;
			}
		}
	}
	icu_irqmaps[IDT_OFFSET_SYSCALL - IDT_OFFSET].im_type = ICU_IMT_SYSCALL;
}

static void
icu_intr_config(int irq __unused, enum intr_trigger trig __unused,
    enum intr_polarity pola __unused)
{
}
