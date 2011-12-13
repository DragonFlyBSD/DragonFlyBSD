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
#include <sys/rman.h>
#include <sys/bus.h>

#include <machine/segments.h>
#include <machine/md_var.h>
#include <machine/intr_machdep.h>
#include <machine/globaldata.h>
#include <machine/smp.h>

#include <sys/thread2.h>

#include <machine_base/icu/elcr_var.h>

#include <machine_base/icu/icu.h>
#include <machine_base/icu/icu_ipl.h>
#include <machine_base/apic/ioapic.h>

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
} icu_irqmaps[MAXCPU][IDT_HWI_VECTORS];

#define ICU_IMT_UNUSED		0	/* KEEP THIS */
#define ICU_IMT_RESERVED	1
#define ICU_IMT_LINE		2
#define ICU_IMT_SYSCALL		3

#define ICU_IMT_ISHWI(map)	((map)->im_type != ICU_IMT_RESERVED && \
				 (map)->im_type != ICU_IMT_SYSCALL)

extern void	ICU_INTREN(int);
extern void	ICU_INTRDIS(int);

extern int	imcr_present;

static void	icu_abi_intr_setup(int, int);
static void	icu_abi_intr_teardown(int);
static void	icu_abi_intr_config(int, enum intr_trigger, enum intr_polarity);
static int	icu_abi_intr_cpuid(int);

static void	icu_abi_finalize(void);
static void	icu_abi_cleanup(void);
static void	icu_abi_setdefault(void);
static void	icu_abi_stabilize(void);
static void	icu_abi_initmap(void);
static void	icu_abi_rman_setup(struct rman *);

struct machintr_abi MachIntrABI_ICU = {
	MACHINTR_ICU,

	.intr_disable	= ICU_INTRDIS,
	.intr_enable	= ICU_INTREN,
	.intr_setup	= icu_abi_intr_setup,
	.intr_teardown	= icu_abi_intr_teardown,
	.intr_config	= icu_abi_intr_config,
	.intr_cpuid	= icu_abi_intr_cpuid,

	.finalize	= icu_abi_finalize,
	.cleanup	= icu_abi_cleanup,
	.setdefault	= icu_abi_setdefault,
	.stabilize	= icu_abi_stabilize,
	.initmap	= icu_abi_initmap,
	.rman_setup	= icu_abi_rman_setup
};

/*
 * WARNING!  SMP builds can use the ICU now so this code must be MP safe.
 */

/*
 * Called before interrupts are physically enabled
 */
static void
icu_abi_stabilize(void)
{
	int intr;

	for (intr = 0; intr < ICU_HWI_VECTORS; ++intr)
		machintr_intr_disable(intr);
	machintr_intr_enable(ICU_IRQ_SLAVE);
}

/*
 * Called after interrupts physically enabled but before the
 * critical section is released.
 */
static void
icu_abi_cleanup(void)
{
	bzero(mdcpu->gd_ipending, sizeof(mdcpu->gd_ipending));
}

/*
 * Called after stablize and cleanup; critical section is not
 * held and interrupts are not physically disabled.
 */
static void
icu_abi_finalize(void)
{
	KKASSERT(MachIntrABI.type == MACHINTR_ICU);
	KKASSERT(!ioapic_enable);

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
}

static void
icu_abi_intr_setup(int intr, int flags __unused)
{
	u_long ef;

	KKASSERT(intr >= 0 && intr < ICU_HWI_VECTORS && intr != ICU_IRQ_SLAVE);

	ef = read_eflags();
	cpu_disable_intr();

	machintr_intr_enable(intr);

	write_eflags(ef);
}

static void
icu_abi_intr_teardown(int intr)
{
	u_long ef;

	KKASSERT(intr >= 0 && intr < ICU_HWI_VECTORS && intr != ICU_IRQ_SLAVE);

	ef = read_eflags();
	cpu_disable_intr();

	machintr_intr_disable(intr);

	write_eflags(ef);
}

static void
icu_abi_setdefault(void)
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
icu_abi_initmap(void)
{
	int cpu;

	/*
	 * NOTE: ncpus is not ready yet
	 */
	for (cpu = 0; cpu < MAXCPU; ++cpu) {
		int i;

		if (cpu != 0) {
			for (i = 0; i < ICU_HWI_VECTORS; ++i)
				icu_irqmaps[cpu][i].im_type = ICU_IMT_RESERVED;
		} else {
			for (i = 0; i < ICU_HWI_VECTORS; ++i)
				icu_irqmaps[cpu][i].im_type = ICU_IMT_LINE;
			icu_irqmaps[cpu][ICU_IRQ_SLAVE].im_type =
			    ICU_IMT_RESERVED;

			if (elcr_found) {
				for (i = 0; i < ICU_HWI_VECTORS; ++i) {
					icu_irqmaps[cpu][i].im_trig =
					    elcr_read_trigger(i);
				}
			} else {
				/*
				 * NOTE: Trigger mode does not matter at all
				 */
				for (i = 0; i < ICU_HWI_VECTORS; ++i) {
					icu_irqmaps[cpu][i].im_trig =
					    INTR_TRIGGER_EDGE;
				}
			}
		}
		icu_irqmaps[cpu][IDT_OFFSET_SYSCALL - IDT_OFFSET].im_type =
		    ICU_IMT_SYSCALL;
	}
}

static void
icu_abi_intr_config(int irq, enum intr_trigger trig,
    enum intr_polarity pola __unused)
{
	struct icu_irqmap *map;

	KKASSERT(trig == INTR_TRIGGER_EDGE || trig == INTR_TRIGGER_LEVEL);

	KKASSERT(irq >= 0 && irq < IDT_HWI_VECTORS);
	map = &icu_irqmaps[0][irq];

	KKASSERT(map->im_type == ICU_IMT_LINE);

	/* TODO: Check whether it is configured or not */

	if (trig == map->im_trig)
		return;

	if (bootverbose) {
		kprintf("ICU: irq %d, %s -> %s\n", irq,
			intr_str_trigger(map->im_trig),
			intr_str_trigger(trig));
	}
	map->im_trig = trig;

	if (!elcr_found) {
		if (bootverbose)
			kprintf("ICU: no ELCR, skip irq %d config\n", irq);
		return;
	}
	elcr_write_trigger(irq, map->im_trig);
}

static int
icu_abi_intr_cpuid(int irq __unused)
{
	return 0;
}

static void
icu_abi_rman_setup(struct rman *rm)
{
	int start, end, i;

	KASSERT(rm->rm_cpuid >= 0 && rm->rm_cpuid < MAXCPU,
	    ("invalid rman cpuid %d", rm->rm_cpuid));

	start = end = -1;
	for (i = 0; i < IDT_HWI_VECTORS; ++i) {
		const struct icu_irqmap *map = &icu_irqmaps[rm->rm_cpuid][i];

		if (start < 0) {
			if (ICU_IMT_ISHWI(map))
				start = end = i;
		} else {
			if (ICU_IMT_ISHWI(map)) {
				end = i;
			} else {
				KKASSERT(end >= 0);
				if (bootverbose) {
					kprintf("ICU: rman cpu%d %d - %d\n",
					    rm->rm_cpuid, start, end);
				}
				if (rman_manage_region(rm, start, end)) {
					panic("rman_manage_region"
					    "(cpu%d %d - %d)", rm->rm_cpuid,
					    start, end);
				}
				start = end = -1;
			}
		}
	}
	if (start >= 0) {
		KKASSERT(end >= 0);
		if (bootverbose) {
			kprintf("ICU: rman cpu%d %d - %d\n",
			    rm->rm_cpuid, start, end);
		}
		if (rman_manage_region(rm, start, end)) {
			panic("rman_manage_region(cpu%d %d - %d)",
			    rm->rm_cpuid, start, end);
		}
	}
}
