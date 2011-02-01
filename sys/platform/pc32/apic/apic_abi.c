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
#include <machine/intr_machdep.h>
#include <machine_base/icu/icu.h>
#include <machine/globaldata.h>

#include <sys/thread2.h>

#include "apic_ipl.h"

#ifdef SMP /* APIC-IO */

extern inthand_t
	IDTVEC(apic_intr0),
	IDTVEC(apic_intr1),
	IDTVEC(apic_intr2),
	IDTVEC(apic_intr3),
	IDTVEC(apic_intr4),
	IDTVEC(apic_intr5),
	IDTVEC(apic_intr6),
	IDTVEC(apic_intr7),
	IDTVEC(apic_intr8),
	IDTVEC(apic_intr9),
	IDTVEC(apic_intr10),
	IDTVEC(apic_intr11),
	IDTVEC(apic_intr12),
	IDTVEC(apic_intr13),
	IDTVEC(apic_intr14),
	IDTVEC(apic_intr15),
	IDTVEC(apic_intr16),
	IDTVEC(apic_intr17),
	IDTVEC(apic_intr18),
	IDTVEC(apic_intr19),
	IDTVEC(apic_intr20),
	IDTVEC(apic_intr21),
	IDTVEC(apic_intr22),
	IDTVEC(apic_intr23),
	IDTVEC(apic_intr24),
	IDTVEC(apic_intr25),
	IDTVEC(apic_intr26),
	IDTVEC(apic_intr27),
	IDTVEC(apic_intr28),
	IDTVEC(apic_intr29),
	IDTVEC(apic_intr30),
	IDTVEC(apic_intr31),
	IDTVEC(apic_intr32),
	IDTVEC(apic_intr33),
	IDTVEC(apic_intr34),
	IDTVEC(apic_intr35),
	IDTVEC(apic_intr36),
	IDTVEC(apic_intr37),
	IDTVEC(apic_intr38),
	IDTVEC(apic_intr39),
	IDTVEC(apic_intr40),
	IDTVEC(apic_intr41),
	IDTVEC(apic_intr42),
	IDTVEC(apic_intr43),
	IDTVEC(apic_intr44),
	IDTVEC(apic_intr45),
	IDTVEC(apic_intr46),
	IDTVEC(apic_intr47),
	IDTVEC(apic_intr48),
	IDTVEC(apic_intr49),
	IDTVEC(apic_intr50),
	IDTVEC(apic_intr51),
	IDTVEC(apic_intr52),
	IDTVEC(apic_intr53),
	IDTVEC(apic_intr54),
	IDTVEC(apic_intr55),
	IDTVEC(apic_intr56),
	IDTVEC(apic_intr57),
	IDTVEC(apic_intr58),
	IDTVEC(apic_intr59),
	IDTVEC(apic_intr60),
	IDTVEC(apic_intr61),
	IDTVEC(apic_intr62),
	IDTVEC(apic_intr63),
	IDTVEC(apic_intr64),
	IDTVEC(apic_intr65),
	IDTVEC(apic_intr66),
	IDTVEC(apic_intr67),
	IDTVEC(apic_intr68),
	IDTVEC(apic_intr69),
	IDTVEC(apic_intr70),
	IDTVEC(apic_intr71),
	IDTVEC(apic_intr72),
	IDTVEC(apic_intr73),
	IDTVEC(apic_intr74),
	IDTVEC(apic_intr75),
	IDTVEC(apic_intr76),
	IDTVEC(apic_intr77),
	IDTVEC(apic_intr78),
	IDTVEC(apic_intr79),
	IDTVEC(apic_intr80),
	IDTVEC(apic_intr81),
	IDTVEC(apic_intr82),
	IDTVEC(apic_intr83),
	IDTVEC(apic_intr84),
	IDTVEC(apic_intr85),
	IDTVEC(apic_intr86),
	IDTVEC(apic_intr87),
	IDTVEC(apic_intr88),
	IDTVEC(apic_intr89),
	IDTVEC(apic_intr90),
	IDTVEC(apic_intr91),
	IDTVEC(apic_intr92),
	IDTVEC(apic_intr93),
	IDTVEC(apic_intr94),
	IDTVEC(apic_intr95),
	IDTVEC(apic_intr96),
	IDTVEC(apic_intr97),
	IDTVEC(apic_intr98),
	IDTVEC(apic_intr99),
	IDTVEC(apic_intr100),
	IDTVEC(apic_intr101),
	IDTVEC(apic_intr102),
	IDTVEC(apic_intr103),
	IDTVEC(apic_intr104),
	IDTVEC(apic_intr105),
	IDTVEC(apic_intr106),
	IDTVEC(apic_intr107),
	IDTVEC(apic_intr108),
	IDTVEC(apic_intr109),
	IDTVEC(apic_intr110),
	IDTVEC(apic_intr111),
	IDTVEC(apic_intr112),
	IDTVEC(apic_intr113),
	IDTVEC(apic_intr114),
	IDTVEC(apic_intr115),
	IDTVEC(apic_intr116),
	IDTVEC(apic_intr117),
	IDTVEC(apic_intr118),
	IDTVEC(apic_intr119),
	IDTVEC(apic_intr120),
	IDTVEC(apic_intr121),
	IDTVEC(apic_intr122),
	IDTVEC(apic_intr123),
	IDTVEC(apic_intr124),
	IDTVEC(apic_intr125),
	IDTVEC(apic_intr126),
	IDTVEC(apic_intr127),
	IDTVEC(apic_intr128),
	IDTVEC(apic_intr129),
	IDTVEC(apic_intr130),
	IDTVEC(apic_intr131),
	IDTVEC(apic_intr132),
	IDTVEC(apic_intr133),
	IDTVEC(apic_intr134),
	IDTVEC(apic_intr135),
	IDTVEC(apic_intr136),
	IDTVEC(apic_intr137),
	IDTVEC(apic_intr138),
	IDTVEC(apic_intr139),
	IDTVEC(apic_intr140),
	IDTVEC(apic_intr141),
	IDTVEC(apic_intr142),
	IDTVEC(apic_intr143),
	IDTVEC(apic_intr144),
	IDTVEC(apic_intr145),
	IDTVEC(apic_intr146),
	IDTVEC(apic_intr147),
	IDTVEC(apic_intr148),
	IDTVEC(apic_intr149),
	IDTVEC(apic_intr150),
	IDTVEC(apic_intr151),
	IDTVEC(apic_intr152),
	IDTVEC(apic_intr153),
	IDTVEC(apic_intr154),
	IDTVEC(apic_intr155),
	IDTVEC(apic_intr156),
	IDTVEC(apic_intr157),
	IDTVEC(apic_intr158),
	IDTVEC(apic_intr159),
	IDTVEC(apic_intr160),
	IDTVEC(apic_intr161),
	IDTVEC(apic_intr162),
	IDTVEC(apic_intr163),
	IDTVEC(apic_intr164),
	IDTVEC(apic_intr165),
	IDTVEC(apic_intr166),
	IDTVEC(apic_intr167),
	IDTVEC(apic_intr168),
	IDTVEC(apic_intr169),
	IDTVEC(apic_intr170),
	IDTVEC(apic_intr171),
	IDTVEC(apic_intr172),
	IDTVEC(apic_intr173),
	IDTVEC(apic_intr174),
	IDTVEC(apic_intr175),
	IDTVEC(apic_intr176),
	IDTVEC(apic_intr177),
	IDTVEC(apic_intr178),
	IDTVEC(apic_intr179),
	IDTVEC(apic_intr180),
	IDTVEC(apic_intr181),
	IDTVEC(apic_intr182),
	IDTVEC(apic_intr183),
	IDTVEC(apic_intr184),
	IDTVEC(apic_intr185),
	IDTVEC(apic_intr186),
	IDTVEC(apic_intr187),
	IDTVEC(apic_intr188),
	IDTVEC(apic_intr189),
	IDTVEC(apic_intr190),
	IDTVEC(apic_intr191);

static inthand_t *apic_intr[APIC_HWI_VECTORS] = {
	&IDTVEC(apic_intr0),
	&IDTVEC(apic_intr1),
	&IDTVEC(apic_intr2),
	&IDTVEC(apic_intr3),
	&IDTVEC(apic_intr4),
	&IDTVEC(apic_intr5),
	&IDTVEC(apic_intr6),
	&IDTVEC(apic_intr7),
	&IDTVEC(apic_intr8),
	&IDTVEC(apic_intr9),
	&IDTVEC(apic_intr10),
	&IDTVEC(apic_intr11),
	&IDTVEC(apic_intr12),
	&IDTVEC(apic_intr13),
	&IDTVEC(apic_intr14),
	&IDTVEC(apic_intr15),
	&IDTVEC(apic_intr16),
	&IDTVEC(apic_intr17),
	&IDTVEC(apic_intr18),
	&IDTVEC(apic_intr19),
	&IDTVEC(apic_intr20),
	&IDTVEC(apic_intr21),
	&IDTVEC(apic_intr22),
	&IDTVEC(apic_intr23),
	&IDTVEC(apic_intr24),
	&IDTVEC(apic_intr25),
	&IDTVEC(apic_intr26),
	&IDTVEC(apic_intr27),
	&IDTVEC(apic_intr28),
	&IDTVEC(apic_intr29),
	&IDTVEC(apic_intr30),
	&IDTVEC(apic_intr31),
	&IDTVEC(apic_intr32),
	&IDTVEC(apic_intr33),
	&IDTVEC(apic_intr34),
	&IDTVEC(apic_intr35),
	&IDTVEC(apic_intr36),
	&IDTVEC(apic_intr37),
	&IDTVEC(apic_intr38),
	&IDTVEC(apic_intr39),
	&IDTVEC(apic_intr40),
	&IDTVEC(apic_intr41),
	&IDTVEC(apic_intr42),
	&IDTVEC(apic_intr43),
	&IDTVEC(apic_intr44),
	&IDTVEC(apic_intr45),
	&IDTVEC(apic_intr46),
	&IDTVEC(apic_intr47),
	&IDTVEC(apic_intr48),
	&IDTVEC(apic_intr49),
	&IDTVEC(apic_intr50),
	&IDTVEC(apic_intr51),
	&IDTVEC(apic_intr52),
	&IDTVEC(apic_intr53),
	&IDTVEC(apic_intr54),
	&IDTVEC(apic_intr55),
	&IDTVEC(apic_intr56),
	&IDTVEC(apic_intr57),
	&IDTVEC(apic_intr58),
	&IDTVEC(apic_intr59),
	&IDTVEC(apic_intr60),
	&IDTVEC(apic_intr61),
	&IDTVEC(apic_intr62),
	&IDTVEC(apic_intr63),
	&IDTVEC(apic_intr64),
	&IDTVEC(apic_intr65),
	&IDTVEC(apic_intr66),
	&IDTVEC(apic_intr67),
	&IDTVEC(apic_intr68),
	&IDTVEC(apic_intr69),
	&IDTVEC(apic_intr70),
	&IDTVEC(apic_intr71),
	&IDTVEC(apic_intr72),
	&IDTVEC(apic_intr73),
	&IDTVEC(apic_intr74),
	&IDTVEC(apic_intr75),
	&IDTVEC(apic_intr76),
	&IDTVEC(apic_intr77),
	&IDTVEC(apic_intr78),
	&IDTVEC(apic_intr79),
	&IDTVEC(apic_intr80),
	&IDTVEC(apic_intr81),
	&IDTVEC(apic_intr82),
	&IDTVEC(apic_intr83),
	&IDTVEC(apic_intr84),
	&IDTVEC(apic_intr85),
	&IDTVEC(apic_intr86),
	&IDTVEC(apic_intr87),
	&IDTVEC(apic_intr88),
	&IDTVEC(apic_intr89),
	&IDTVEC(apic_intr90),
	&IDTVEC(apic_intr91),
	&IDTVEC(apic_intr92),
	&IDTVEC(apic_intr93),
	&IDTVEC(apic_intr94),
	&IDTVEC(apic_intr95),
	&IDTVEC(apic_intr96),
	&IDTVEC(apic_intr97),
	&IDTVEC(apic_intr98),
	&IDTVEC(apic_intr99),
	&IDTVEC(apic_intr100),
	&IDTVEC(apic_intr101),
	&IDTVEC(apic_intr102),
	&IDTVEC(apic_intr103),
	&IDTVEC(apic_intr104),
	&IDTVEC(apic_intr105),
	&IDTVEC(apic_intr106),
	&IDTVEC(apic_intr107),
	&IDTVEC(apic_intr108),
	&IDTVEC(apic_intr109),
	&IDTVEC(apic_intr110),
	&IDTVEC(apic_intr111),
	&IDTVEC(apic_intr112),
	&IDTVEC(apic_intr113),
	&IDTVEC(apic_intr114),
	&IDTVEC(apic_intr115),
	&IDTVEC(apic_intr116),
	&IDTVEC(apic_intr117),
	&IDTVEC(apic_intr118),
	&IDTVEC(apic_intr119),
	&IDTVEC(apic_intr120),
	&IDTVEC(apic_intr121),
	&IDTVEC(apic_intr122),
	&IDTVEC(apic_intr123),
	&IDTVEC(apic_intr124),
	&IDTVEC(apic_intr125),
	&IDTVEC(apic_intr126),
	&IDTVEC(apic_intr127),
	&IDTVEC(apic_intr128),
	&IDTVEC(apic_intr129),
	&IDTVEC(apic_intr130),
	&IDTVEC(apic_intr131),
	&IDTVEC(apic_intr132),
	&IDTVEC(apic_intr133),
	&IDTVEC(apic_intr134),
	&IDTVEC(apic_intr135),
	&IDTVEC(apic_intr136),
	&IDTVEC(apic_intr137),
	&IDTVEC(apic_intr138),
	&IDTVEC(apic_intr139),
	&IDTVEC(apic_intr140),
	&IDTVEC(apic_intr141),
	&IDTVEC(apic_intr142),
	&IDTVEC(apic_intr143),
	&IDTVEC(apic_intr144),
	&IDTVEC(apic_intr145),
	&IDTVEC(apic_intr146),
	&IDTVEC(apic_intr147),
	&IDTVEC(apic_intr148),
	&IDTVEC(apic_intr149),
	&IDTVEC(apic_intr150),
	&IDTVEC(apic_intr151),
	&IDTVEC(apic_intr152),
	&IDTVEC(apic_intr153),
	&IDTVEC(apic_intr154),
	&IDTVEC(apic_intr155),
	&IDTVEC(apic_intr156),
	&IDTVEC(apic_intr157),
	&IDTVEC(apic_intr158),
	&IDTVEC(apic_intr159),
	&IDTVEC(apic_intr160),
	&IDTVEC(apic_intr161),
	&IDTVEC(apic_intr162),
	&IDTVEC(apic_intr163),
	&IDTVEC(apic_intr164),
	&IDTVEC(apic_intr165),
	&IDTVEC(apic_intr166),
	&IDTVEC(apic_intr167),
	&IDTVEC(apic_intr168),
	&IDTVEC(apic_intr169),
	&IDTVEC(apic_intr170),
	&IDTVEC(apic_intr171),
	&IDTVEC(apic_intr172),
	&IDTVEC(apic_intr173),
	&IDTVEC(apic_intr174),
	&IDTVEC(apic_intr175),
	&IDTVEC(apic_intr176),
	&IDTVEC(apic_intr177),
	&IDTVEC(apic_intr178),
	&IDTVEC(apic_intr179),
	&IDTVEC(apic_intr180),
	&IDTVEC(apic_intr181),
	&IDTVEC(apic_intr182),
	&IDTVEC(apic_intr183),
	&IDTVEC(apic_intr184),
	&IDTVEC(apic_intr185),
	&IDTVEC(apic_intr186),
	&IDTVEC(apic_intr187),
	&IDTVEC(apic_intr188),
	&IDTVEC(apic_intr189),
	&IDTVEC(apic_intr190),
	&IDTVEC(apic_intr191)
};

extern void	APIC_INTREN(int);
extern void	APIC_INTRDIS(int);

static int	apic_setvar(int, const void *);
static int	apic_getvar(int, void *);
static int	apic_vectorctl(int, int, int);
static void	apic_finalize(void);
static void	apic_cleanup(void);
static void	apic_setdefault(void);

static int	apic_imcr_present;

struct machintr_abi MachIntrABI_APIC = {
	MACHINTR_APIC,
	.intrdis	= APIC_INTRDIS,
	.intren		= APIC_INTREN,
	.vectorctl	= apic_vectorctl,
	.setvar		= apic_setvar,
	.getvar		= apic_getvar,
	.finalize	= apic_finalize,
	.cleanup	= apic_cleanup,
	.setdefault	= apic_setdefault
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

	KKASSERT(MachIntrABI.type == MACHINTR_ICU);
	KKASSERT(apic_io_enable);

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
	 * 8259 is completely disconnected; switch to IOAPIC MachIntrABI
	 * and reconfigure the default IDT entries.
	 */
	MachIntrABI = MachIntrABI_APIC;
	MachIntrABI.setdefault();

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

	if (intr < 0 || intr >= APIC_HWI_VECTORS ||
	    intr == IDT_OFFSET_SYSCALL - IDT_OFFSET)
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

	default:
		error = EOPNOTSUPP;
		break;
	}

	write_eflags(ef);
	return error;
}

static void
apic_setdefault(void)
{
	int intr;

	for (intr = 0; intr < APIC_HWI_VECTORS; ++intr) {
		if (intr == IDT_OFFSET_SYSCALL - IDT_OFFSET)
			continue;
		setidt(IDT_OFFSET + intr, apic_intr[intr], SDT_SYS386IGT,
		       SEL_KPL, GSEL(GCODE_SEL, SEL_KPL));
	}
}

#endif	/* SMP */
