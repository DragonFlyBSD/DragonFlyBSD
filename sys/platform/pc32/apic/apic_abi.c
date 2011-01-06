/*
 * Copyright (c) 2005 The DragonFly Project.  All rights reserved.
 * Copyright (c) 1996, by Steve Passe.  All rights reserved.
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
 * $DragonFly: src/sys/platform/pc32/apic/apic_abi.c,v 1.12 2007/04/30 16:45:55 dillon Exp $
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
#include <machine_base/isa/intr_machdep.h>
#include <machine_base/icu/icu.h>
#include <machine/globaldata.h>

#include <sys/thread2.h>

#include "apic_ipl.h"

#ifdef SMP /* APIC-IO */

extern void	APIC_INTREN(int);
extern void	APIC_INTRDIS(int);

extern inthand_t
	IDTVEC(apic_intr0),	IDTVEC(apic_intr1),
	IDTVEC(apic_intr2),	IDTVEC(apic_intr3),
	IDTVEC(apic_intr4),	IDTVEC(apic_intr5),
	IDTVEC(apic_intr6),	IDTVEC(apic_intr7),
	IDTVEC(apic_intr8),	IDTVEC(apic_intr9),
	IDTVEC(apic_intr10),	IDTVEC(apic_intr11),
	IDTVEC(apic_intr12),	IDTVEC(apic_intr13),
	IDTVEC(apic_intr14),	IDTVEC(apic_intr15),
	IDTVEC(apic_intr16),	IDTVEC(apic_intr17),
	IDTVEC(apic_intr18),	IDTVEC(apic_intr19),
	IDTVEC(apic_intr20),	IDTVEC(apic_intr21),
	IDTVEC(apic_intr22),	IDTVEC(apic_intr23),
	IDTVEC(apic_intr24),	IDTVEC(apic_intr25),
	IDTVEC(apic_intr26),	IDTVEC(apic_intr27),
	IDTVEC(apic_intr28),	IDTVEC(apic_intr29),
	IDTVEC(apic_intr30),	IDTVEC(apic_intr31);

static int	apic_setvar(int, const void *);
static int	apic_getvar(int, void *);
static int	apic_vectorctl(int, int, int);
static void	apic_finalize(void);
static void	apic_cleanup(void);

static inthand_t *apic_intr[APIC_HWI_VECTORS] = {
	&IDTVEC(apic_intr0),	&IDTVEC(apic_intr1),
	&IDTVEC(apic_intr2),	&IDTVEC(apic_intr3),
	&IDTVEC(apic_intr4),	&IDTVEC(apic_intr5),
	&IDTVEC(apic_intr6),	&IDTVEC(apic_intr7),
	&IDTVEC(apic_intr8),	&IDTVEC(apic_intr9),
	&IDTVEC(apic_intr10),	&IDTVEC(apic_intr11),
	&IDTVEC(apic_intr12),	&IDTVEC(apic_intr13),
	&IDTVEC(apic_intr14),	&IDTVEC(apic_intr15),
	&IDTVEC(apic_intr16),	&IDTVEC(apic_intr17),
	&IDTVEC(apic_intr18),	&IDTVEC(apic_intr19),
	&IDTVEC(apic_intr20),	&IDTVEC(apic_intr21),
	&IDTVEC(apic_intr22),	&IDTVEC(apic_intr23),
	&IDTVEC(apic_intr24),	&IDTVEC(apic_intr25),
	&IDTVEC(apic_intr26),	&IDTVEC(apic_intr27),
	&IDTVEC(apic_intr28),	&IDTVEC(apic_intr29),
	&IDTVEC(apic_intr30),	&IDTVEC(apic_intr31)
};

static int	apic_imcr_present;

struct machintr_abi MachIntrABI_APIC = {
	MACHINTR_APIC,
	.intrdis	= APIC_INTRDIS,
	.intren		= APIC_INTREN,
	.vectorctl	= apic_vectorctl,
	.setvar		= apic_setvar,
	.getvar		= apic_getvar,
	.finalize	= apic_finalize,
	.cleanup	= apic_cleanup
};

static int
apic_setvar(int varid, const void *buf)
{
	int error = 0;

	switch (varid) {
	case MACHINTR_VAR_IMCR_PRESENT:
		apic_imcr_present = *(const int *)buf;
		break;

	default:
		error = ENOENT;
		break;
	}
	return error;
}

static int
apic_getvar(int varid, void *buf)
{
	int error = 0;

	switch (varid) {
	case MACHINTR_VAR_IMCR_PRESENT:
		*(int *)buf = apic_imcr_present;
		break;

	default:
		error = ENOENT;
		break;
	}
	return error;
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
	uint32_t temp;

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
	temp = lapic.lvt_lint0;
	temp |= APIC_LVT_MASKED;
	lapic.lvt_lint0 = temp;

	/*
	 * Setup lint1 to handle NMI
	 */
	temp = lapic.lvt_lint1;
	temp &= ~APIC_LVT_MASKED;
	lapic.lvt_lint1 = temp;

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
	bzero(mdcpu->gd_ipending, sizeof(mdcpu->gd_ipending));
}

static int
apic_vectorctl(int op, int intr, int flags)
{
	int error;
	int vector;
	int select;
	uint32_t value;
	u_long ef;

	if (intr < 0 || intr >= APIC_HWI_VECTORS)
		return EINVAL;

	ef = read_eflags();
	cpu_disable_intr();
	error = 0;

	switch(op) {
	case MACHINTR_VECTOR_SETUP:
		vector = IDT_OFFSET + intr;
		setidt(vector, apic_intr[intr], SDT_SYS386IGT,
		       SEL_KPL, GSEL(GCODE_SEL, SEL_KPL));

		/*
		 * Now reprogram the vector in the IO APIC.  In order to avoid
		 * losing an EOI for a level interrupt, which is vector based,
		 * make sure that the IO APIC is programmed for edge-triggering
		 * first, then reprogrammed with the new vector.  This should
		 * clear the IRR bit.
		 */
		if (int_to_apicintpin[intr].ioapic >= 0) {
			if (bootverbose) {
				kprintf("IOAPIC: try clearing IRR for "
					"irq %d\n", intr);
			}

			imen_lock();

			select = int_to_apicintpin[intr].redirindex;
			value = io_apic_read(int_to_apicintpin[intr].ioapic,
					     select);
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

		vector = IDT_OFFSET + intr;
		setidt(vector, apic_intr[intr], SDT_SYS386IGT, SEL_KPL,
		       GSEL(GCODE_SEL, SEL_KPL));

		/*
		 * In order to avoid losing an EOI for a level interrupt, which
		 * is vector based, make sure that the IO APIC is programmed for
		 * edge-triggering first, then reprogrammed with the new vector.
		 * This should clear the IRR bit.
		 */
		if (int_to_apicintpin[intr].ioapic >= 0) {
			imen_lock();

			select = int_to_apicintpin[intr].redirindex;
			value = io_apic_read(int_to_apicintpin[intr].ioapic,
					     select);

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
		setidt(vector, apic_intr[intr], SDT_SYS386IGT, SEL_KPL,
		       GSEL(GCODE_SEL, SEL_KPL));
		break;

	default:
		error = EOPNOTSUPP;
		break;
	}

	write_eflags(ef);
	return error;
}

#endif	/* SMP */
