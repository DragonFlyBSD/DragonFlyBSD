/*
 * Copyright (c) 2009 The DragonFly Project.  All rights reserved.
 * 
 * This code is derived from software contributed to The DragonFly Project
 * by Sepherosa Ziehau <sepherosa@gmail.com>
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
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/systm.h>

#include <machine_base/isa/isa_intr.h>
#include <machine_base/apic/lapic.h>
#include <machine_base/apic/ioapic.h>
#include <machine_base/apic/apicvar.h>

#include <contrib/dev/acpica/source/include/acpi.h>

#include "acpi_sdt_var.h"
#include "acpi_sci_var.h"

extern int naps;

#define MADT_VPRINTF(fmt, arg...) \
do { \
	if (bootverbose) \
		kprintf("ACPI MADT: " fmt , ##arg); \
} while (0)

#define MADT_INT_BUS_ISA	0

typedef int			(*madt_iter_t)(void *,
				    const ACPI_SUBTABLE_HEADER *);

static int			madt_check(vm_paddr_t);
static int			madt_iterate_entries(ACPI_TABLE_MADT *,
				    madt_iter_t, void *);

static vm_paddr_t		madt_lapic_pass1(void);
static int			madt_lapic_pass2(int);

static int			madt_lapic_enumerate(struct lapic_enumerator *);
static int			madt_lapic_probe(struct lapic_enumerator *);

static void			madt_ioapic_enumerate(
				    struct ioapic_enumerator *);
static int			madt_ioapic_probe(struct ioapic_enumerator *);

static vm_paddr_t		madt_phyaddr;

static void
madt_probe(void)
{
	vm_paddr_t madt_paddr;

	KKASSERT(madt_phyaddr == 0);

	madt_paddr = sdt_search(ACPI_SIG_MADT);
	if (madt_paddr == 0) {
		kprintf("madt_probe: can't locate MADT\n");
		return;
	}

	/* Preliminary checks */
	if (madt_check(madt_paddr)) {
		kprintf("madt_probe: madt_check failed\n");
		return;
	}

	madt_phyaddr = madt_paddr;
}
SYSINIT(madt_probe, SI_BOOT2_PRESMP, SI_ORDER_SECOND, madt_probe, 0);

static int
madt_check(vm_paddr_t madt_paddr)
{
	ACPI_TABLE_MADT *madt;
	int error = 0;

	KKASSERT(madt_paddr != 0);

	madt = sdt_sdth_map(madt_paddr);
	KKASSERT(madt != NULL);

	/*
	 * MADT in ACPI specification 1.0 - 5.0
	 */
	if (madt->Header.Revision < 1 || madt->Header.Revision > 3) {
		kprintf("madt_check: unknown MADT revision %d\n",
			madt->Header.Revision);
	}

	if (madt->Header.Length < sizeof(*madt)) {
		kprintf("madt_check: invalid MADT length %u\n",
			madt->Header.Length);
		error = EINVAL;
		goto back;
	}
back:
	sdt_sdth_unmap(&madt->Header);
	return error;
}

static int
madt_iterate_entries(ACPI_TABLE_MADT *madt, madt_iter_t func, void *arg)
{
	int size, cur, error;

	size = madt->Header.Length - sizeof(*madt);
	cur = 0;
	error = 0;

	while (size - cur > sizeof(ACPI_SUBTABLE_HEADER)) {
		const ACPI_SUBTABLE_HEADER *ent;
		
		ent = (const ACPI_SUBTABLE_HEADER *)
		    ((char *)madt + sizeof(*madt) + cur);
		if (ent->Length < sizeof(*ent)) {
			kprintf("madt_iterate_entries: invalid MADT "
				"entry len %d\n", ent->Length);
			error = EINVAL;
			break;
		}
		if (ent->Length > (size - cur)) {
			kprintf("madt_iterate_entries: invalid MADT "
				"entry len %d, > table length\n", ent->Length);
			error = EINVAL;
			break;
		}

		cur += ent->Length;

		/*
		 * Only Local APIC, I/O APIC and Interrupt Source Override
		 * are defined in ACPI specification 1.0 - 5.0
		 */
		switch (ent->Type) {
		case ACPI_MADT_TYPE_LOCAL_APIC:
			if (ent->Length < sizeof(ACPI_MADT_LOCAL_APIC)) {
				kprintf("madt_iterate_entries: invalid MADT "
					"lapic entry len %d\n", ent->Length);
				error = EINVAL;
			}
			break;

		case ACPI_MADT_TYPE_IO_APIC:
			if (ent->Length < sizeof(ACPI_MADT_IO_APIC)) {
				kprintf("madt_iterate_entries: invalid MADT "
					"ioapic entry len %d\n", ent->Length);
				error = EINVAL;
			}
			break;

		case ACPI_MADT_TYPE_INTERRUPT_OVERRIDE:
			if (ent->Length < sizeof(ACPI_MADT_INTERRUPT_OVERRIDE)) {
				kprintf("madt_iterate_entries: invalid MADT "
					"intsrc entry len %d\n",
					ent->Length);
				error = EINVAL;
			}
			break;
		}
		if (error)
			break;

		error = func(arg, ent);
		if (error)
			break;

		ent = ACPI_ADD_PTR(ACPI_SUBTABLE_HEADER, ent, ent->Length);
	}
	return error;
}

static int
madt_lapic_pass1_callback(void *xarg, const ACPI_SUBTABLE_HEADER *ent)
{
	const ACPI_MADT_LOCAL_APIC_OVERRIDE *lapic_addr_ent;
	uint64_t *addr64 = xarg;

	if (ent->Type != ACPI_MADT_TYPE_LOCAL_APIC_OVERRIDE)
		return 0;
	if (ent->Length < sizeof(*lapic_addr_ent)) {
		kprintf("madt_lapic_pass1: "
			"invalid LAPIC address override length\n");
		return 0;
	}
	lapic_addr_ent = (const ACPI_MADT_LOCAL_APIC_OVERRIDE *)ent;

	*addr64 = lapic_addr_ent->Address;
	return 0;
}

static vm_paddr_t
madt_lapic_pass1(void)
{
	ACPI_TABLE_MADT *madt;
	vm_paddr_t lapic_addr;
	uint64_t lapic_addr64;
	int error;

	KKASSERT(madt_phyaddr != 0);

	madt = sdt_sdth_map(madt_phyaddr);
	KKASSERT(madt != NULL);

	MADT_VPRINTF("LAPIC address 0x%x, flags %#x\n",
		     madt->Address, madt->Flags);
	lapic_addr = madt->Address;

	lapic_addr64 = 0;
	error = madt_iterate_entries(madt, madt_lapic_pass1_callback,
				     &lapic_addr64);
	if (error)
		panic("madt_iterate_entries(pass1) failed");

	if (lapic_addr64 != 0) {
		kprintf("ACPI MADT: 64bits lapic address 0x%lx\n",
			lapic_addr64);
		lapic_addr = lapic_addr64;
	}

	sdt_sdth_unmap(&madt->Header);

	return lapic_addr;
}

struct madt_lapic_pass2_cbarg {
	int	cpu;
	int	bsp_found;
	int	bsp_apic_id;
};

static int
madt_lapic_pass2_callback(void *xarg, const ACPI_SUBTABLE_HEADER *ent)
{
	const ACPI_MADT_LOCAL_APIC *lapic_ent;
	struct madt_lapic_pass2_cbarg *arg = xarg;

	if (ent->Type != ACPI_MADT_TYPE_LOCAL_APIC)
		return 0;

	lapic_ent = (const ACPI_MADT_LOCAL_APIC *)ent;
	if (lapic_ent->LapicFlags & ACPI_MADT_ENABLED) {
		MADT_VPRINTF("cpu id %d, apic id %d\n",
			     lapic_ent->ProcessorId, lapic_ent->Id);
		if (lapic_ent->Id == arg->bsp_apic_id) {
			lapic_set_cpuid(0, lapic_ent->Id);
			arg->bsp_found = 1;
		} else {
			lapic_set_cpuid(arg->cpu, lapic_ent->Id);
			arg->cpu++;
		}
	}
	return 0;
}

static int
madt_lapic_pass2(int bsp_apic_id)
{
	ACPI_TABLE_MADT *madt;
	struct madt_lapic_pass2_cbarg arg;
	int error;

	MADT_VPRINTF("BSP apic id %d\n", bsp_apic_id);

	KKASSERT(madt_phyaddr != 0);

	madt = sdt_sdth_map(madt_phyaddr);
	KKASSERT(madt != NULL);

	bzero(&arg, sizeof(arg));
	arg.cpu = 1;
	arg.bsp_apic_id = bsp_apic_id;

	error = madt_iterate_entries(madt, madt_lapic_pass2_callback, &arg);
	if (error)
		panic("madt_iterate_entries(pass2) failed");

	KKASSERT(arg.bsp_found);
	naps = arg.cpu - 1; /* exclude BSP */

	sdt_sdth_unmap(&madt->Header);

	return 0;
}

struct madt_lapic_probe_cbarg {
	int		cpu_count;
	vm_paddr_t	lapic_addr;
};

static int
madt_lapic_probe_callback(void *xarg, const ACPI_SUBTABLE_HEADER *ent)
{
	struct madt_lapic_probe_cbarg *arg = xarg;

	if (ent->Type == ACPI_MADT_TYPE_LOCAL_APIC) {
		const ACPI_MADT_LOCAL_APIC *lapic_ent;

		lapic_ent = (const ACPI_MADT_LOCAL_APIC *)ent;
		if (lapic_ent->LapicFlags & ACPI_MADT_ENABLED) {
			arg->cpu_count++;
			if (lapic_ent->Id == APICID_MAX) {
				kprintf("madt_lapic_probe: "
				    "invalid LAPIC apic id %d\n",
				    lapic_ent->Id);
				return EINVAL;
			}
		}
	} else if (ent->Type == ACPI_MADT_TYPE_LOCAL_APIC_OVERRIDE) {
		const ACPI_MADT_LOCAL_APIC_OVERRIDE *lapic_addr_ent;

		if (ent->Length < sizeof(*lapic_addr_ent)) {
			kprintf("madt_lapic_probe: "
				"invalid LAPIC address override length\n");
			return 0;
		}
		lapic_addr_ent = (const ACPI_MADT_LOCAL_APIC_OVERRIDE *)ent;

		if (lapic_addr_ent->Address != 0)
			arg->lapic_addr = lapic_addr_ent->Address;
	}
	return 0;
}

static int
madt_lapic_probe(struct lapic_enumerator *e)
{
	struct madt_lapic_probe_cbarg arg;
	ACPI_TABLE_MADT *madt;
	int error;

	if (madt_phyaddr == 0)
		return ENXIO;

	madt = sdt_sdth_map(madt_phyaddr);
	KKASSERT(madt != NULL);

	bzero(&arg, sizeof(arg));
	arg.lapic_addr = madt->Address;

	error = madt_iterate_entries(madt, madt_lapic_probe_callback, &arg);
	if (!error) {
		if (arg.cpu_count == 0) {
			kprintf("madt_lapic_probe: no CPU is found\n");
			error = EOPNOTSUPP;
		}
		if (arg.lapic_addr == 0) {
			kprintf("madt_lapic_probe: zero LAPIC address\n");
			error = EOPNOTSUPP;
		}
	}

	sdt_sdth_unmap(&madt->Header);
	return error;
}

static int
madt_lapic_enumerate(struct lapic_enumerator *e)
{
	vm_paddr_t lapic_addr;
	int bsp_apic_id;

	KKASSERT(madt_phyaddr != 0);

	lapic_addr = madt_lapic_pass1();
	if (lapic_addr == 0)
		panic("madt_lapic_enumerate: no local apic");

	lapic_map(lapic_addr);

	bsp_apic_id = APIC_ID(lapic->id);
	if (bsp_apic_id == APICID_MAX) {
		/*
		 * XXX
		 * Some old brain dead BIOS will set BSP's LAPIC apic id
		 * to 255, though all LAPIC entries in MADT are valid.
		 */
		kprintf("%s invalid BSP LAPIC apic id %d\n", __func__,
		    bsp_apic_id);
		return EINVAL;
	}

	if (madt_lapic_pass2(bsp_apic_id))
		panic("madt_lapic_enumerate: madt_lapic_pass2 failed");

	return 0;
}

static struct lapic_enumerator	madt_lapic_enumerator = {
	.lapic_prio = LAPIC_ENUM_PRIO_MADT,
	.lapic_probe = madt_lapic_probe,
	.lapic_enumerate = madt_lapic_enumerate
};

static void
madt_lapic_enum_register(void)
{
	int prio;

	prio = LAPIC_ENUM_PRIO_MADT;
	kgetenv_int("hw.madt_lapic_prio", &prio);
	madt_lapic_enumerator.lapic_prio = prio;

	lapic_enumerator_register(&madt_lapic_enumerator);
}
SYSINIT(madt_lapic, SI_BOOT2_PRESMP, SI_ORDER_ANY, madt_lapic_enum_register, 0);

struct madt_ioapic_probe_cbarg {
	int	ioapic_cnt;
	int	gsi_base0;
};

static int
madt_ioapic_probe_callback(void *xarg, const ACPI_SUBTABLE_HEADER *ent)
{
	struct madt_ioapic_probe_cbarg *arg = xarg;

	if (ent->Type == ACPI_MADT_TYPE_INTERRUPT_OVERRIDE) {
		const ACPI_MADT_INTERRUPT_OVERRIDE *intsrc_ent;
		int trig, pola;

		intsrc_ent = (const ACPI_MADT_INTERRUPT_OVERRIDE *)ent;

		if (intsrc_ent->SourceIrq >= ISA_IRQ_CNT) {
			kprintf("madt_ioapic_probe: invalid intsrc irq (%d)\n",
				intsrc_ent->SourceIrq);
			return EINVAL;
		}

		if (intsrc_ent->Bus != MADT_INT_BUS_ISA) {
			kprintf("ACPI MADT: warning intsrc irq %d "
				"bus is not ISA (%d)\n",
				intsrc_ent->SourceIrq, intsrc_ent->Bus);
		}

		trig = intsrc_ent->IntiFlags & ACPI_MADT_TRIGGER_MASK;
		if (trig == ACPI_MADT_TRIGGER_RESERVED) {
			kprintf("ACPI MADT: warning invalid intsrc irq %d "
				"trig, reserved\n", intsrc_ent->SourceIrq);
		} else if (trig == ACPI_MADT_TRIGGER_LEVEL) {
			MADT_VPRINTF("warning invalid intsrc irq %d "
			    "trig, level\n", intsrc_ent->SourceIrq);
		}

		pola = intsrc_ent->IntiFlags & ACPI_MADT_POLARITY_MASK;
		if (pola == ACPI_MADT_POLARITY_RESERVED) {
			kprintf("ACPI MADT: warning invalid intsrc irq %d "
				"pola, reserved\n", intsrc_ent->SourceIrq);
		} else if (pola == ACPI_MADT_POLARITY_ACTIVE_LOW) {
			MADT_VPRINTF("warning invalid intsrc irq %d "
			    "pola, low\n", intsrc_ent->SourceIrq);
		}
	} else if (ent->Type == ACPI_MADT_TYPE_IO_APIC) {
		const ACPI_MADT_IO_APIC *ioapic_ent;

		ioapic_ent = (const ACPI_MADT_IO_APIC *)ent;
		if (ioapic_ent->Address == 0) {
			kprintf("madt_ioapic_probe: zero IOAPIC address\n");
			return EINVAL;
		}
		if (ioapic_ent->Id == APICID_MAX) {
			kprintf("madt_ioapic_probe: "
			    "invalid IOAPIC apic id %d\n",
			    ioapic_ent->Id);
			return EINVAL;
		}

		arg->ioapic_cnt++;
		if (ioapic_ent->GlobalIrqBase == 0)
			arg->gsi_base0 = 1;
	}
	return 0;
}

static int
madt_ioapic_probe(struct ioapic_enumerator *e)
{
	struct madt_ioapic_probe_cbarg arg;
	ACPI_TABLE_MADT *madt;
	int error;

	if (madt_phyaddr == 0)
		return ENXIO;

	madt = sdt_sdth_map(madt_phyaddr);
	KKASSERT(madt != NULL);

	bzero(&arg, sizeof(arg));

	error = madt_iterate_entries(madt, madt_ioapic_probe_callback, &arg);
	if (!error) {
		if (arg.ioapic_cnt == 0) {
			kprintf("madt_ioapic_probe: no IOAPIC\n");
			error = ENXIO;
		}
		if (!arg.gsi_base0) {
			kprintf("madt_ioapic_probe: no GSI base 0\n");
			error = EINVAL;
		}
	}

	sdt_sdth_unmap(&madt->Header);
	return error;
}

static int
madt_ioapic_enum_callback(void *xarg, const ACPI_SUBTABLE_HEADER *ent)
{
	if (ent->Type == ACPI_MADT_TYPE_INTERRUPT_OVERRIDE) {
		const ACPI_MADT_INTERRUPT_OVERRIDE *intsrc_ent;
		enum intr_trigger trig;
		enum intr_polarity pola;
		int ent_trig, ent_pola;

		intsrc_ent = (const ACPI_MADT_INTERRUPT_OVERRIDE *)ent;

		KKASSERT(intsrc_ent->SourceIrq < ISA_IRQ_CNT);
		if (intsrc_ent->Bus != MADT_INT_BUS_ISA)
			return 0;

		ent_trig = intsrc_ent->IntiFlags & ACPI_MADT_TRIGGER_MASK;
		if (ent_trig == ACPI_MADT_TRIGGER_RESERVED)
			return 0;
		else if (ent_trig == ACPI_MADT_TRIGGER_LEVEL)
			trig = INTR_TRIGGER_LEVEL;
		else
			trig = INTR_TRIGGER_EDGE;

		ent_pola = intsrc_ent->IntiFlags & ACPI_MADT_POLARITY_MASK;
		if (ent_pola == ACPI_MADT_POLARITY_RESERVED)
			return 0;
		else if (ent_pola == ACPI_MADT_POLARITY_ACTIVE_LOW)
			pola = INTR_POLARITY_LOW;
		else
			pola = INTR_POLARITY_HIGH;

		if (intsrc_ent->SourceIrq == acpi_sci_irqno()) {
			acpi_sci_setmode1(trig, pola);
			MADT_VPRINTF("SCI irq %d, first test %s/%s\n",
			    intsrc_ent->SourceIrq,
			    intr_str_trigger(trig), intr_str_polarity(pola));
		}

		/*
		 * We ignore the polarity and trigger changes, since
		 * most of them are wrong or useless at best.
		 */
		if (intsrc_ent->SourceIrq == intsrc_ent->GlobalIrq) {
			/* Nothing changed */
			return 0;
		}
		trig = INTR_TRIGGER_EDGE;
		pola = INTR_POLARITY_HIGH;

		MADT_VPRINTF("INTSRC irq %d -> gsi %u %s/%s\n",
			     intsrc_ent->SourceIrq, intsrc_ent->GlobalIrq,
			     intr_str_trigger(trig), intr_str_polarity(pola));
		ioapic_intsrc(intsrc_ent->SourceIrq, intsrc_ent->GlobalIrq,
			      trig, pola);
	} else if (ent->Type == ACPI_MADT_TYPE_IO_APIC) {
		const ACPI_MADT_IO_APIC *ioapic_ent;
		uint32_t ver;
		void *addr;
		int npin;

		ioapic_ent = (const ACPI_MADT_IO_APIC *)ent;
		MADT_VPRINTF("IOAPIC addr 0x%08x, apic id %d, gsi base %u\n",
			     ioapic_ent->Address, ioapic_ent->Id,
			     ioapic_ent->GlobalIrqBase);

		addr = ioapic_map(ioapic_ent->Address);

		ver = ioapic_read(addr, IOAPIC_VER);
		npin = ((ver & IOART_VER_MAXREDIR) >> MAXREDIRSHIFT) + 1;

		ioapic_add(addr, ioapic_ent->GlobalIrqBase, npin);
	}
	return 0;
}

static void
madt_ioapic_enumerate(struct ioapic_enumerator *e)
{
	ACPI_TABLE_MADT *madt;
	int error;

	KKASSERT(madt_phyaddr != 0);

	madt = sdt_sdth_map(madt_phyaddr);
	KKASSERT(madt != NULL);

	error = madt_iterate_entries(madt, madt_ioapic_enum_callback, NULL);
	if (error)
		panic("madt_ioapic_enumerate failed");

	sdt_sdth_unmap(&madt->Header);
}

static struct ioapic_enumerator	madt_ioapic_enumerator = {
	.ioapic_prio = IOAPIC_ENUM_PRIO_MADT,
	.ioapic_probe = madt_ioapic_probe,
	.ioapic_enumerate = madt_ioapic_enumerate
};

static void
madt_ioapic_enum_register(void)
{
	int prio;

	prio = IOAPIC_ENUM_PRIO_MADT;
	kgetenv_int("hw.madt_ioapic_prio", &prio);
	madt_ioapic_enumerator.ioapic_prio = prio;

	ioapic_enumerator_register(&madt_ioapic_enumerator);
}
SYSINIT(madt_ioapic, SI_BOOT2_PRESMP, SI_ORDER_ANY,
	madt_ioapic_enum_register, 0);
