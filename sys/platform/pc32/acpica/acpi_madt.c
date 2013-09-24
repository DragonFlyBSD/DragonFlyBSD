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

#include "acpi_sdt.h"
#include "acpi_sdt_var.h"
#include "acpi_sci_var.h"

extern int naps;

#define MADT_VPRINTF(fmt, arg...) \
do { \
	if (bootverbose) \
		kprintf("ACPI MADT: " fmt , ##arg); \
} while (0)

/* Multiple APIC Description Table */
struct acpi_madt {
	struct acpi_sdth	madt_hdr;
	uint32_t		madt_lapic_addr;
	uint32_t		madt_flags;
	uint8_t			madt_ents[1];
} __packed;

/* Common parts of MADT APIC structure */
struct acpi_madt_ent {
	uint8_t			me_type;	/* MADT_ENT_ */
	uint8_t			me_len;
} __packed;

#define MADT_ENT_LAPIC		0
#define MADT_ENT_IOAPIC		1
#define MADT_ENT_INTSRC		2
#define MADT_ENT_LAPIC_ADDR	5

/* MADT Processor Local APIC */
struct acpi_madt_lapic {
	struct acpi_madt_ent	ml_hdr;
	uint8_t			ml_cpu_id;
	uint8_t			ml_apic_id;
	uint32_t		ml_flags;	/* MADT_LAPIC_ */
} __packed;

#define MADT_LAPIC_ENABLED	0x1

/* MADT I/O APIC */
struct acpi_madt_ioapic {
	struct acpi_madt_ent	mio_hdr;
	uint8_t			mio_apic_id;
	uint8_t			mio_reserved;
	uint32_t		mio_addr;
	uint32_t		mio_gsi_base;
} __packed;

/* MADT Interrupt Source Override */
struct acpi_madt_intsrc {
	struct acpi_madt_ent	mint_hdr;
	uint8_t			mint_bus;	/* MADT_INT_BUS_ */
	uint8_t			mint_src;
	uint32_t		mint_gsi;
	uint16_t		mint_flags;	/* MADT_INT_ */
} __packed;

#define MADT_INT_BUS_ISA	0

#define MADT_INT_POLA_MASK	0x3
#define MADT_INT_POLA_SHIFT	0
#define MADT_INT_POLA_CONFORM	0
#define MADT_INT_POLA_HIGH	1
#define MADT_INT_POLA_RSVD	2
#define MADT_INT_POLA_LOW	3
#define MADT_INT_TRIG_MASK	0xc
#define MADT_INT_TRIG_SHIFT	2
#define MADT_INT_TRIG_CONFORM	0
#define MADT_INT_TRIG_EDGE	1
#define MADT_INT_TRIG_RSVD	2
#define MADT_INT_TRIG_LEVEL	3

/* MADT Local APIC Address Override */
struct acpi_madt_lapic_addr {
	struct acpi_madt_ent	mla_hdr;
	uint16_t		mla_reserved;
	uint64_t		mla_lapic_addr;
} __packed;

typedef int			(*madt_iter_t)(void *,
				    const struct acpi_madt_ent *);

static int			madt_check(vm_paddr_t);
static int			madt_iterate_entries(struct acpi_madt *,
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

	madt_paddr = sdt_search(ACPI_MADT_SIG);
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
	struct acpi_madt *madt;
	int error = 0;

	KKASSERT(madt_paddr != 0);

	madt = sdt_sdth_map(madt_paddr);
	KKASSERT(madt != NULL);

	/*
	 * MADT in ACPI specification 1.0 - 4.0
	 */
	if (madt->madt_hdr.sdth_rev < 1 || madt->madt_hdr.sdth_rev > 3) {
		kprintf("madt_check: unknown MADT revision %d\n",
			madt->madt_hdr.sdth_rev);
	}

	if (madt->madt_hdr.sdth_len <
	    sizeof(*madt) - sizeof(madt->madt_ents)) {
		kprintf("madt_check: invalid MADT length %u\n",
			madt->madt_hdr.sdth_len);
		error = EINVAL;
		goto back;
	}
back:
	sdt_sdth_unmap(&madt->madt_hdr);
	return error;
}

static int
madt_iterate_entries(struct acpi_madt *madt, madt_iter_t func, void *arg)
{
	int size, cur, error;

	size = madt->madt_hdr.sdth_len -
	       (sizeof(*madt) - sizeof(madt->madt_ents));
	cur = 0;
	error = 0;

	while (size - cur > sizeof(struct acpi_madt_ent)) {
		const struct acpi_madt_ent *ent;

		ent = (const struct acpi_madt_ent *)&madt->madt_ents[cur];
		if (ent->me_len < sizeof(*ent)) {
			kprintf("madt_iterate_entries: invalid MADT "
				"entry len %d\n", ent->me_len);
			error = EINVAL;
			break;
		}
		if (ent->me_len > (size - cur)) {
			kprintf("madt_iterate_entries: invalid MADT "
				"entry len %d, > table length\n", ent->me_len);
			error = EINVAL;
			break;
		}

		cur += ent->me_len;

		/*
		 * Only Local APIC, I/O APIC and Interrupt Source Override
		 * are defined in ACPI specification 1.0 - 4.0
		 */
		switch (ent->me_type) {
		case MADT_ENT_LAPIC:
			if (ent->me_len < sizeof(struct acpi_madt_lapic)) {
				kprintf("madt_iterate_entries: invalid MADT "
					"lapic entry len %d\n", ent->me_len);
				error = EINVAL;
			}
			break;

		case MADT_ENT_IOAPIC:
			if (ent->me_len < sizeof(struct acpi_madt_ioapic)) {
				kprintf("madt_iterate_entries: invalid MADT "
					"ioapic entry len %d\n", ent->me_len);
				error = EINVAL;
			}
			break;

		case MADT_ENT_INTSRC:
			if (ent->me_len < sizeof(struct acpi_madt_intsrc)) {
				kprintf("madt_iterate_entries: invalid MADT "
					"intsrc entry len %d\n",
					ent->me_len);
				error = EINVAL;
			}
			break;
		}
		if (error)
			break;

		error = func(arg, ent);
		if (error)
			break;
	}
	return error;
}

static int
madt_lapic_pass1_callback(void *xarg, const struct acpi_madt_ent *ent)
{
	const struct acpi_madt_lapic_addr *lapic_addr_ent;
	uint64_t *addr64 = xarg;

	if (ent->me_type != MADT_ENT_LAPIC_ADDR)
		return 0;
	if (ent->me_len < sizeof(*lapic_addr_ent)) {
		kprintf("madt_lapic_pass1: "
			"invalid LAPIC address override length\n");
		return 0;
	}
	lapic_addr_ent = (const struct acpi_madt_lapic_addr *)ent;

	*addr64 = lapic_addr_ent->mla_lapic_addr;
	return 0;
}

static vm_paddr_t
madt_lapic_pass1(void)
{
	struct acpi_madt *madt;
	vm_paddr_t lapic_addr;
	uint64_t lapic_addr64;
	int error;

	KKASSERT(madt_phyaddr != 0);

	madt = sdt_sdth_map(madt_phyaddr);
	KKASSERT(madt != NULL);

	MADT_VPRINTF("LAPIC address 0x%x, flags %#x\n",
		     madt->madt_lapic_addr, madt->madt_flags);
	lapic_addr = madt->madt_lapic_addr;

	lapic_addr64 = 0;
	error = madt_iterate_entries(madt, madt_lapic_pass1_callback,
				     &lapic_addr64);
	if (error)
		panic("madt_iterate_entries(pass1) failed");

	if (lapic_addr64 != 0) {
		kprintf("ACPI MADT: warning 64bits lapic address 0x%llx\n",
			lapic_addr64);
		lapic_addr = lapic_addr64;
	}

	sdt_sdth_unmap(&madt->madt_hdr);

	return lapic_addr;
}

struct madt_lapic_pass2_cbarg {
	int	cpu;
	int	bsp_found;
	int	bsp_apic_id;
};

static int
madt_lapic_pass2_callback(void *xarg, const struct acpi_madt_ent *ent)
{
	const struct acpi_madt_lapic *lapic_ent;
	struct madt_lapic_pass2_cbarg *arg = xarg;

	if (ent->me_type != MADT_ENT_LAPIC)
		return 0;

	lapic_ent = (const struct acpi_madt_lapic *)ent;
	if (lapic_ent->ml_flags & MADT_LAPIC_ENABLED) {
		MADT_VPRINTF("cpu id %d, apic id %d\n",
			     lapic_ent->ml_cpu_id, lapic_ent->ml_apic_id);
		if (lapic_ent->ml_apic_id == arg->bsp_apic_id) {
			lapic_set_cpuid(0, lapic_ent->ml_apic_id);
			arg->bsp_found = 1;
		} else {
			lapic_set_cpuid(arg->cpu, lapic_ent->ml_apic_id);
			arg->cpu++;
		}
	}
	return 0;
}

static int
madt_lapic_pass2(int bsp_apic_id)
{
	struct acpi_madt *madt;
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

	sdt_sdth_unmap(&madt->madt_hdr);

	return 0;
}

struct madt_lapic_probe_cbarg {
	int		cpu_count;
	vm_paddr_t	lapic_addr;
};

static int
madt_lapic_probe_callback(void *xarg, const struct acpi_madt_ent *ent)
{
	struct madt_lapic_probe_cbarg *arg = xarg;

	if (ent->me_type == MADT_ENT_LAPIC) {
		const struct acpi_madt_lapic *lapic_ent;

		lapic_ent = (const struct acpi_madt_lapic *)ent;
		if (lapic_ent->ml_flags & MADT_LAPIC_ENABLED) {
			arg->cpu_count++;
			if (lapic_ent->ml_apic_id == APICID_MAX) {
				kprintf("madt_lapic_probe: "
				    "invalid LAPIC apic id %d\n",
				    lapic_ent->ml_apic_id);
				return EINVAL;
			}
		}
	} else if (ent->me_type == MADT_ENT_LAPIC_ADDR) {
		const struct acpi_madt_lapic_addr *lapic_addr_ent;

		if (ent->me_len < sizeof(*lapic_addr_ent)) {
			kprintf("madt_lapic_probe: "
				"invalid LAPIC address override length\n");
			return 0;
		}
		lapic_addr_ent = (const struct acpi_madt_lapic_addr *)ent;

		if (lapic_addr_ent->mla_lapic_addr != 0)
			arg->lapic_addr = lapic_addr_ent->mla_lapic_addr;
	}
	return 0;
}

static int
madt_lapic_probe(struct lapic_enumerator *e)
{
	struct madt_lapic_probe_cbarg arg;
	struct acpi_madt *madt;
	int error;

	if (madt_phyaddr == 0)
		return ENXIO;

	madt = sdt_sdth_map(madt_phyaddr);
	KKASSERT(madt != NULL);

	bzero(&arg, sizeof(arg));
	arg.lapic_addr = madt->madt_lapic_addr;

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

	sdt_sdth_unmap(&madt->madt_hdr);
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
madt_ioapic_probe_callback(void *xarg, const struct acpi_madt_ent *ent)
{
	struct madt_ioapic_probe_cbarg *arg = xarg;

	if (ent->me_type == MADT_ENT_INTSRC) {
		const struct acpi_madt_intsrc *intsrc_ent;
		int trig, pola;

		intsrc_ent = (const struct acpi_madt_intsrc *)ent;

		if (intsrc_ent->mint_src >= ISA_IRQ_CNT) {
			kprintf("madt_ioapic_probe: invalid intsrc irq (%d)\n",
				intsrc_ent->mint_src);
			return EINVAL;
		}

		if (intsrc_ent->mint_bus != MADT_INT_BUS_ISA) {
			kprintf("ACPI MADT: warning intsrc irq %d "
				"bus is not ISA (%d)\n",
				intsrc_ent->mint_src, intsrc_ent->mint_bus);
		}

		trig = (intsrc_ent->mint_flags & MADT_INT_TRIG_MASK) >>
		       MADT_INT_TRIG_SHIFT;
		if (trig == MADT_INT_TRIG_RSVD) {
			kprintf("ACPI MADT: warning invalid intsrc irq %d "
				"trig, reserved\n", intsrc_ent->mint_src);
		} else if (trig == MADT_INT_TRIG_LEVEL) {
			MADT_VPRINTF("warning invalid intsrc irq %d "
			    "trig, level\n", intsrc_ent->mint_src);
		}

		pola = (intsrc_ent->mint_flags & MADT_INT_POLA_MASK) >>
		       MADT_INT_POLA_SHIFT;
		if (pola == MADT_INT_POLA_RSVD) {
			kprintf("ACPI MADT: warning invalid intsrc irq %d "
				"pola, reserved\n", intsrc_ent->mint_src);
		} else if (pola == MADT_INT_POLA_LOW) {
			MADT_VPRINTF("warning invalid intsrc irq %d "
			    "pola, low\n", intsrc_ent->mint_src);
		}
	} else if (ent->me_type == MADT_ENT_IOAPIC) {
		const struct acpi_madt_ioapic *ioapic_ent;

		ioapic_ent = (const struct acpi_madt_ioapic *)ent;
		if (ioapic_ent->mio_addr == 0) {
			kprintf("madt_ioapic_probe: zero IOAPIC address\n");
			return EINVAL;
		}
		if (ioapic_ent->mio_apic_id == APICID_MAX) {
			kprintf("madt_ioapic_probe: "
			    "invalid IOAPIC apic id %d\n",
			    ioapic_ent->mio_apic_id);
			return EINVAL;
		}

		arg->ioapic_cnt++;
		if (ioapic_ent->mio_gsi_base == 0)
			arg->gsi_base0 = 1;
	}
	return 0;
}

static int
madt_ioapic_probe(struct ioapic_enumerator *e)
{
	struct madt_ioapic_probe_cbarg arg;
	struct acpi_madt *madt;
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

	sdt_sdth_unmap(&madt->madt_hdr);
	return error;
}

static int
madt_ioapic_enum_callback(void *xarg, const struct acpi_madt_ent *ent)
{
	if (ent->me_type == MADT_ENT_INTSRC) {
		const struct acpi_madt_intsrc *intsrc_ent;
		enum intr_trigger trig;
		enum intr_polarity pola;
		int ent_trig, ent_pola;

		intsrc_ent = (const struct acpi_madt_intsrc *)ent;

		KKASSERT(intsrc_ent->mint_src < ISA_IRQ_CNT);
		if (intsrc_ent->mint_bus != MADT_INT_BUS_ISA)
			return 0;

		ent_trig = (intsrc_ent->mint_flags & MADT_INT_TRIG_MASK) >>
		    MADT_INT_TRIG_SHIFT;
		if (ent_trig == MADT_INT_TRIG_RSVD)
			return 0;
		else if (ent_trig == MADT_INT_TRIG_LEVEL)
			trig = INTR_TRIGGER_LEVEL;
		else
			trig = INTR_TRIGGER_EDGE;

		ent_pola = (intsrc_ent->mint_flags & MADT_INT_POLA_MASK) >>
		    MADT_INT_POLA_SHIFT;
		if (ent_pola == MADT_INT_POLA_RSVD)
			return 0;
		else if (ent_pola == MADT_INT_POLA_LOW)
			pola = INTR_POLARITY_LOW;
		else
			pola = INTR_POLARITY_HIGH;

		if (intsrc_ent->mint_src == acpi_sci_irqno()) {
			acpi_sci_setmode1(trig, pola);
			MADT_VPRINTF("SCI irq %d, first test %s/%s\n",
			    intsrc_ent->mint_src,
			    intr_str_trigger(trig), intr_str_polarity(pola));
		}

		/*
		 * We ignore the polarity and trigger changes, since
		 * most of them are wrong or useless at best.
		 */
		if (intsrc_ent->mint_src == intsrc_ent->mint_gsi) {
			/* Nothing changed */
			return 0;
		}
		trig = INTR_TRIGGER_EDGE;
		pola = INTR_POLARITY_HIGH;

		MADT_VPRINTF("INTSRC irq %d -> gsi %u %s/%s\n",
			     intsrc_ent->mint_src, intsrc_ent->mint_gsi,
			     intr_str_trigger(trig), intr_str_polarity(pola));
		ioapic_intsrc(intsrc_ent->mint_src, intsrc_ent->mint_gsi,
			      trig, pola);
	} else if (ent->me_type == MADT_ENT_IOAPIC) {
		const struct acpi_madt_ioapic *ioapic_ent;
		uint32_t ver;
		void *addr;
		int npin;

		ioapic_ent = (const struct acpi_madt_ioapic *)ent;
		MADT_VPRINTF("IOAPIC addr 0x%08x, apic id %d, gsi base %u\n",
			     ioapic_ent->mio_addr, ioapic_ent->mio_apic_id,
			     ioapic_ent->mio_gsi_base);

		addr = ioapic_map(ioapic_ent->mio_addr);

		ver = ioapic_read(addr, IOAPIC_VER);
		npin = ((ver & IOART_VER_MAXREDIR) >> MAXREDIRSHIFT) + 1;

		ioapic_add(addr, ioapic_ent->mio_gsi_base, npin);
	}
	return 0;
}

static void
madt_ioapic_enumerate(struct ioapic_enumerator *e)
{
	struct acpi_madt *madt;
	int error;

	KKASSERT(madt_phyaddr != 0);

	madt = sdt_sdth_map(madt_phyaddr);
	KKASSERT(madt != NULL);

	error = madt_iterate_entries(madt, madt_ioapic_enum_callback, NULL);
	if (error)
		panic("madt_ioapic_enumerate failed");

	sdt_sdth_unmap(&madt->madt_hdr);
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
