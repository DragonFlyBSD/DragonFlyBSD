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
#include <machine/msi_var.h>

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
	int			im_msi_base;
	uint32_t		im_flags;	/* ICU_IMF_ */
} icu_irqmaps[MAXCPU][IDT_HWI_VECTORS];

static struct lwkt_token icu_irqmap_tok =
	LWKT_TOKEN_INITIALIZER(icu_irqmap_token);

#define ICU_IMT_UNUSED		0	/* KEEP THIS */
#define ICU_IMT_RESERVED	1
#define ICU_IMT_LEGACY		2
#define ICU_IMT_SYSCALL		3
#define ICU_IMT_MSI		4
#define ICU_IMT_MSIX		5

#define ICU_IMT_ISHWI(map)	((map)->im_type != ICU_IMT_RESERVED && \
				 (map)->im_type != ICU_IMT_SYSCALL)

#define ICU_IMF_CONF		0x1

extern void	ICU_INTREN(int);
extern void	ICU_INTRDIS(int);

extern int	imcr_present;

static void	icu_abi_intr_enable(int);
static void	icu_abi_intr_disable(int);
static void	icu_abi_intr_setup(int, int);
static void	icu_abi_intr_teardown(int);

static void	icu_abi_legacy_intr_config(int, enum intr_trigger,
		    enum intr_polarity);
static int	icu_abi_legacy_intr_cpuid(int);
static int	icu_abi_legacy_intr_find(int, enum intr_trigger,
		    enum intr_polarity);
static int	icu_abi_legacy_intr_find_bygsi(int, enum intr_trigger,
		    enum intr_polarity);

static int	icu_abi_msi_alloc(int [], int, int);
static void	icu_abi_msi_release(const int [], int, int);
static void	icu_abi_msi_map(int, uint64_t *, uint32_t *, int);
static int	icu_abi_msix_alloc(int *, int);
static void	icu_abi_msix_release(int, int);

static int	icu_abi_msi_alloc_intern(int, const char *,
		    int [], int, int);
static void	icu_abi_msi_release_intern(int, const char *,
		    const int [], int, int);

static void	icu_abi_finalize(void);
static void	icu_abi_cleanup(void);
static void	icu_abi_setdefault(void);
static void	icu_abi_stabilize(void);
static void	icu_abi_initmap(void);
static void	icu_abi_rman_setup(struct rman *);

struct machintr_abi MachIntrABI_ICU = {
	MACHINTR_ICU,
	.intr_disable	= icu_abi_intr_disable,
	.intr_enable	= icu_abi_intr_enable,
	.intr_setup	= icu_abi_intr_setup,
	.intr_teardown	= icu_abi_intr_teardown,

	.legacy_intr_config = icu_abi_legacy_intr_config,
	.legacy_intr_cpuid = icu_abi_legacy_intr_cpuid,
	.legacy_intr_find = icu_abi_legacy_intr_find,
	.legacy_intr_find_bygsi = icu_abi_legacy_intr_find_bygsi,

	.msi_alloc	= icu_abi_msi_alloc,
	.msi_release	= icu_abi_msi_release,
	.msi_map	= icu_abi_msi_map,
	.msix_alloc	= icu_abi_msix_alloc,
	.msix_release	= icu_abi_msix_release,

	.finalize	= icu_abi_finalize,
	.cleanup	= icu_abi_cleanup,
	.setdefault	= icu_abi_setdefault,
	.stabilize	= icu_abi_stabilize,
	.initmap	= icu_abi_initmap,
	.rman_setup	= icu_abi_rman_setup
};

static int	icu_abi_msi_start;	/* NOTE: for testing only */

/*
 * WARNING!  SMP builds can use the ICU now so this code must be MP safe.
 */

static void
icu_abi_intr_enable(int irq)
{
	const struct icu_irqmap *map;

	KASSERT(irq >= 0 && irq < IDT_HWI_VECTORS,
	    ("icu enable, invalid irq %d", irq));

	map = &icu_irqmaps[mycpuid][irq];
	KASSERT(ICU_IMT_ISHWI(map),
	    ("icu enable, not hwi irq %d, type %d, cpu%d",
	     irq, map->im_type, mycpuid));
	if (map->im_type != ICU_IMT_LEGACY)
		return;

	ICU_INTREN(irq);
}

static void
icu_abi_intr_disable(int irq)
{
	const struct icu_irqmap *map;

	KASSERT(irq >= 0 && irq < IDT_HWI_VECTORS,
	    ("icu disable, invalid irq %d", irq));

	map = &icu_irqmaps[mycpuid][irq];
	KASSERT(ICU_IMT_ISHWI(map),
	    ("icu disable, not hwi irq %d, type %d, cpu%d",
	     irq, map->im_type, mycpuid));
	if (map->im_type != ICU_IMT_LEGACY)
		return;

	ICU_INTRDIS(irq);
}

/*
 * Called before interrupts are physically enabled
 */
static void
icu_abi_stabilize(void)
{
	int intr;

	for (intr = 0; intr < ICU_HWI_VECTORS; ++intr)
		ICU_INTRDIS(intr);
	ICU_INTREN(ICU_IRQ_SLAVE);
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
icu_abi_intr_setup(int intr, int flags)
{
	const struct icu_irqmap *map;
	register_t ef;

	KASSERT(intr >= 0 && intr < IDT_HWI_VECTORS,
	    ("icu setup, invalid irq %d", intr));

	map = &icu_irqmaps[mycpuid][intr];
	KASSERT(ICU_IMT_ISHWI(map),
	    ("icu setup, not hwi irq %d, type %d, cpu%d",
	     intr, map->im_type, mycpuid));
	if (map->im_type != ICU_IMT_LEGACY)
		return;

	ef = read_rflags();
	cpu_disable_intr();

	ICU_INTREN(intr);

	write_rflags(ef);
}

static void
icu_abi_intr_teardown(int intr)
{
	const struct icu_irqmap *map;
	register_t ef;

	KASSERT(intr >= 0 && intr < IDT_HWI_VECTORS,
	    ("icu teardown, invalid irq %d", intr));

	map = &icu_irqmaps[mycpuid][intr];
	KASSERT(ICU_IMT_ISHWI(map),
	    ("icu teardown, not hwi irq %d, type %d, cpu%d",
	     intr, map->im_type, mycpuid));
	if (map->im_type != ICU_IMT_LEGACY)
		return;

	ef = read_rflags();
	cpu_disable_intr();

	ICU_INTRDIS(intr);

	write_rflags(ef);
}

static void
icu_abi_setdefault(void)
{
	int intr;

	for (intr = 0; intr < ICU_HWI_VECTORS; ++intr) {
		if (intr == ICU_IRQ_SLAVE)
			continue;
		setidt_global(IDT_OFFSET + intr, icu_intr[intr],
		    SDT_SYSIGT, SEL_KPL, 0);
	}
}

static void
icu_abi_initmap(void)
{
	int cpu;

	kgetenv_int("hw.icu.msi_start", &icu_abi_msi_start);
	icu_abi_msi_start &= ~0x1f;	/* MUST be 32 aligned */

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
				icu_irqmaps[cpu][i].im_type = ICU_IMT_LEGACY;
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

		for (i = 0; i < IDT_HWI_VECTORS; ++i)
			icu_irqmaps[cpu][i].im_msi_base = -1;

		icu_irqmaps[cpu][IDT_OFFSET_SYSCALL - IDT_OFFSET].im_type =
		    ICU_IMT_SYSCALL;
	}
}

static void
icu_abi_legacy_intr_config(int irq, enum intr_trigger trig,
    enum intr_polarity pola __unused)
{
	struct icu_irqmap *map;

	KKASSERT(trig == INTR_TRIGGER_EDGE || trig == INTR_TRIGGER_LEVEL);

	KKASSERT(irq >= 0 && irq < IDT_HWI_VECTORS);
	map = &icu_irqmaps[0][irq];

	KKASSERT(map->im_type == ICU_IMT_LEGACY);

	/* TODO: Check whether it is configured or not */
	map->im_flags |= ICU_IMF_CONF;

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
icu_abi_legacy_intr_cpuid(int irq __unused)
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

static int
icu_abi_msi_alloc_intern(int type, const char *desc,
    int intrs[], int count, int cpuid)
{
	int i, error;

	KASSERT(cpuid >= 0 && cpuid < ncpus,
	    ("invalid cpuid %d", cpuid));

	KASSERT(count > 0 && count <= 32, ("invalid count %d", count));
	KASSERT((count & (count - 1)) == 0,
	    ("count %d is not power of 2", count));

	lwkt_gettoken(&icu_irqmap_tok);

	/*
	 * NOTE:
	 * Since IDT_OFFSET is 32, which is the maximum valid 'count',
	 * we do not need to find out the first properly aligned
	 * interrupt vector.
	 */

	error = EMSGSIZE;
	for (i = icu_abi_msi_start; i < IDT_HWI_VECTORS; i += count) {
		int j;

		if (icu_irqmaps[cpuid][i].im_type != ICU_IMT_UNUSED)
			continue;

		for (j = 1; j < count; ++j) {
			if (icu_irqmaps[cpuid][i + j].im_type != ICU_IMT_UNUSED)
				break;
		}
		if (j != count)
			continue;

		for (j = 0; j < count; ++j) {
			struct icu_irqmap *map;
			int intr = i + j;

			map = &icu_irqmaps[cpuid][intr];
			KASSERT(map->im_msi_base < 0,
			    ("intr %d, stale %s-base %d",
			     intr, desc, map->im_msi_base));

			map->im_type = type;
			map->im_msi_base = i;

			intrs[j] = intr;
			msi_setup(intr, cpuid);

			if (bootverbose) {
				kprintf("alloc %s intr %d on cpu%d\n",
				    desc, intr, cpuid);
			}
		}
		error = 0;
		break;
	}

	lwkt_reltoken(&icu_irqmap_tok);

	return error;
}

static void
icu_abi_msi_release_intern(int type, const char *desc,
    const int intrs[], int count, int cpuid)
{
	int i, msi_base = -1, intr_next = -1, mask;

	KASSERT(cpuid >= 0 && cpuid < ncpus,
	    ("invalid cpuid %d", cpuid));

	KASSERT(count > 0 && count <= 32, ("invalid count %d", count));

	mask = count - 1;
	KASSERT((count & mask) == 0, ("count %d is not power of 2", count));

	lwkt_gettoken(&icu_irqmap_tok);

	for (i = 0; i < count; ++i) {
		struct icu_irqmap *map;
		int intr = intrs[i];

		KASSERT(intr >= 0 && intr < IDT_HWI_VECTORS,
		    ("invalid intr %d", intr));

		map = &icu_irqmaps[cpuid][intr];
		KASSERT(map->im_type == type,
		    ("trying to release non-%s intr %d, type %d", desc,
		     intr, map->im_type));
		KASSERT(map->im_msi_base >= 0 && map->im_msi_base <= intr,
		    ("intr %d, invalid %s-base %d", intr, desc,
		     map->im_msi_base));
		KASSERT((map->im_msi_base & mask) == 0,
		    ("intr %d, %s-base %d is not properly aligned %d",
		     intr, desc, map->im_msi_base, count));

		if (msi_base < 0) {
			msi_base = map->im_msi_base;
		} else {
			KASSERT(map->im_msi_base == msi_base,
			    ("intr %d, inconsistent %s-base, "
			     "was %d, now %d",
			     intr, desc, msi_base, map->im_msi_base));
		}

		if (intr_next < intr)
			intr_next = intr;

		map->im_type = ICU_IMT_UNUSED;
		map->im_msi_base = -1;

		if (bootverbose) {
			kprintf("release %s intr %d on cpu%d\n",
			    desc, intr, cpuid);
		}
	}

	KKASSERT(intr_next > 0);
	KKASSERT(msi_base >= 0);

	++intr_next;
	if (intr_next < IDT_HWI_VECTORS) {
		const struct icu_irqmap *map = &icu_irqmaps[cpuid][intr_next];

		if (map->im_type == type) {
			KASSERT(map->im_msi_base != msi_base,
			    ("more than %d %s was allocated", count, desc));
		}
	}

	lwkt_reltoken(&icu_irqmap_tok);
}

static int
icu_abi_msi_alloc(int intrs[], int count, int cpuid)
{
	return icu_abi_msi_alloc_intern(ICU_IMT_MSI, "MSI",
	    intrs, count, cpuid);
}

static void
icu_abi_msi_release(const int intrs[], int count, int cpuid)
{
	icu_abi_msi_release_intern(ICU_IMT_MSI, "MSI",
	    intrs, count, cpuid);
}

static int
icu_abi_msix_alloc(int *intr, int cpuid)
{
	return icu_abi_msi_alloc_intern(ICU_IMT_MSIX, "MSI-X",
	    intr, 1, cpuid);
}

static void
icu_abi_msix_release(int intr, int cpuid)
{
	icu_abi_msi_release_intern(ICU_IMT_MSIX, "MXI-X",
	    &intr, 1, cpuid);
}

static void
icu_abi_msi_map(int intr, uint64_t *addr, uint32_t *data, int cpuid)
{
	const struct icu_irqmap *map;

	KASSERT(cpuid >= 0 && cpuid < ncpus,
	    ("invalid cpuid %d", cpuid));

	KASSERT(intr >= 0 && intr < IDT_HWI_VECTORS,
	    ("invalid intr %d", intr));

	lwkt_gettoken(&icu_irqmap_tok);

	map = &icu_irqmaps[cpuid][intr];
	KASSERT(map->im_type == ICU_IMT_MSI ||
	    map->im_type == ICU_IMT_MSIX,
	    ("trying to map non-MSI/MSI-X intr %d, type %d", intr, map->im_type));
	KASSERT(map->im_msi_base >= 0 && map->im_msi_base <= intr,
	    ("intr %d, invalid %s-base %d", intr,
	     map->im_type == ICU_IMT_MSI ? "MSI" : "MSI-X",
	     map->im_msi_base));

	msi_map(map->im_msi_base, addr, data, cpuid);

	if (bootverbose) {
		kprintf("map %s intr %d on cpu%d\n",
		    map->im_type == ICU_IMT_MSI ? "MSI" : "MSI-X",
		    intr, cpuid);
	}

	lwkt_reltoken(&icu_irqmap_tok);
}

static int
icu_abi_legacy_intr_find(int irq, enum intr_trigger trig,
    enum intr_polarity pola __unused)
{
	const struct icu_irqmap *map;

#ifdef INVARIANTS
	if (trig == INTR_TRIGGER_CONFORM) {
		KKASSERT(pola == INTR_POLARITY_CONFORM);
	} else {
		KKASSERT(trig == INTR_TRIGGER_EDGE ||
		    trig == INTR_TRIGGER_LEVEL);
		KKASSERT(pola == INTR_POLARITY_HIGH ||
		    pola == INTR_POLARITY_LOW);
	}
#endif

	if (irq < 0 || irq >= ICU_HWI_VECTORS)
		return -1;

	map = &icu_irqmaps[0][irq];
	if (map->im_type == ICU_IMT_LEGACY) {
		if ((map->im_flags & ICU_IMF_CONF) &&
		    trig != INTR_TRIGGER_CONFORM) {
			if (map->im_trig != trig)
				return -1;
		}
		return irq;
	}
	return -1;
}

static int
icu_abi_legacy_intr_find_bygsi(int gsi, enum intr_trigger trig,
    enum intr_polarity pola)
{
	/* GSI and IRQ has 1:1 mapping */
	return icu_abi_legacy_intr_find(gsi, trig, pola);
}
