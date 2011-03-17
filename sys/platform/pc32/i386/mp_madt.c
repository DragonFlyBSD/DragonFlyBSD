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
#include <sys/kernel.h>
#include <sys/systm.h>

#include <machine/pmap.h>
#include <machine/smp.h>
#include <machine/md_var.h>
#include <machine/specialreg.h>
#include <machine_base/apic/mpapic.h>

#define MADT_VPRINTF(fmt, arg...) \
do { \
	if (bootverbose) \
		kprintf("ACPI MADT: " fmt , ##arg); \
} while (0)

#define ACPI_RSDP_EBDA_MAPSZ	1024
#define ACPI_RSDP_BIOS_MAPSZ	0x20000
#define ACPI_RSDP_BIOS_MAPADDR	0xe0000

#define ACPI_RSDP_ALIGN		16

#define ACPI_RSDP_SIGLEN	8
#define ACPI_RSDP_SIG		"RSD PTR "

#define ACPI_SDTH_SIGLEN	4
#define ACPI_RSDT_SIG		"RSDT"
#define ACPI_XSDT_SIG		"XSDT"
#define ACPI_MADT_SIG		"APIC"

/* Root System Description Pointer */
struct acpi_rsdp {
	uint8_t			rsdp_sig[ACPI_RSDP_SIGLEN];
	uint8_t			rsdp_cksum;
	uint8_t			rsdp_oem_id[6];
	uint8_t			rsdp_rev;
	uint32_t		rsdp_rsdt;
	uint32_t		rsdp_len;
	uint64_t		rsdp_xsdt;
	uint8_t			rsdp_ext_cksum;
	uint8_t			rsdp_rsvd[3];
} __packed;

/* System Description Table Header */
struct acpi_sdth {
	uint8_t			sdth_sig[ACPI_SDTH_SIGLEN];
	uint32_t		sdth_len;
	uint8_t			sdth_rev;
	uint8_t			sdth_cksum;
	uint8_t			sdth_oem_id[6];
	uint8_t			sdth_oem_tbid[8];
	uint32_t		sdth_oem_rev;
	uint32_t		sdth_crt_id;
	uint32_t		sdth_crt_rev;
} __packed;

/* Extended System Description Table */
struct acpi_xsdt {
	struct acpi_sdth	xsdt_hdr;
	uint64_t		xsdt_ents[1];
} __packed;

/* Root System Description Table */
struct acpi_rsdt {
	struct acpi_sdth	rsdt_hdr;
	uint32_t		rsdt_ents[1];
} __packed;

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

typedef	vm_paddr_t		(*madt_search_t)(vm_paddr_t);
typedef int			(*madt_iter_t)(void *,
				    const struct acpi_madt_ent *);

static const struct acpi_rsdp	*madt_rsdp_search(const uint8_t *, int);
static void			*madt_sdth_map(vm_paddr_t);
static void			madt_sdth_unmap(struct acpi_sdth *);
static vm_paddr_t		madt_search_xsdt(vm_paddr_t);
static vm_paddr_t		madt_search_rsdt(vm_paddr_t);
static int			madt_check(vm_paddr_t);
static int			madt_iterate_entries(struct acpi_madt *,
				    madt_iter_t, void *);

static vm_offset_t		madt_lapic_pass1(void);
static int			madt_lapic_pass2(int);

static void			madt_lapic_enumerate(struct lapic_enumerator *);
static int			madt_lapic_probe(struct lapic_enumerator *);

static void			madt_ioapic_enumerate(
				    struct ioapic_enumerator *);
static int			madt_ioapic_probe(struct ioapic_enumerator *);

extern u_long			ebda_addr;

static vm_paddr_t		madt_phyaddr;

static void
madt_probe(void)
{
	const struct acpi_rsdp *rsdp;
	madt_search_t search;
	vm_paddr_t search_paddr, madt_paddr;
	vm_size_t mapsz;
	uint8_t *ptr;

	KKASSERT(madt_phyaddr == 0);

	if (ebda_addr != 0) {
		mapsz = ACPI_RSDP_EBDA_MAPSZ;
		ptr = pmap_mapdev(ebda_addr, mapsz);

		rsdp = madt_rsdp_search(ptr, mapsz);
		if (rsdp == NULL) {
			MADT_VPRINTF("RSDP not in EBDA\n");
			pmap_unmapdev((vm_offset_t)ptr, mapsz);

			ptr = NULL;
			mapsz = 0;
		} else {
			MADT_VPRINTF("RSDP in EBDA\n");
			goto found_rsdp;
		}
	}

	mapsz = ACPI_RSDP_BIOS_MAPSZ;
	ptr = pmap_mapdev(ACPI_RSDP_BIOS_MAPADDR, mapsz);

	rsdp = madt_rsdp_search(ptr, mapsz);
	if (rsdp == NULL) {
		kprintf("madt_probe: no RSDP\n");
		pmap_unmapdev((vm_offset_t)ptr, mapsz);
		return;
	} else {
		MADT_VPRINTF("RSDP in BIOS mem\n");
	}

found_rsdp:
	if (rsdp->rsdp_rev != 2) {
		search_paddr = rsdp->rsdp_rsdt;
		search = madt_search_rsdt;
	} else {
		search_paddr = rsdp->rsdp_xsdt;
		search = madt_search_xsdt;
	}
	pmap_unmapdev((vm_offset_t)ptr, mapsz);

	madt_paddr = search(search_paddr);
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
SYSINIT(madt_probe, SI_BOOT2_PRESMP, SI_ORDER_FIRST, madt_probe, 0);

static const struct acpi_rsdp *
madt_rsdp_search(const uint8_t *target, int size)
{
	const struct acpi_rsdp *rsdp;
	int i;

	KKASSERT(size > sizeof(*rsdp));

	for (i = 0; i < size - sizeof(*rsdp); i += ACPI_RSDP_ALIGN) {
		rsdp = (const struct acpi_rsdp *)&target[i];
		if (memcmp(rsdp->rsdp_sig, ACPI_RSDP_SIG,
			   ACPI_RSDP_SIGLEN) == 0)
			return rsdp;
	}
	return NULL;
}

static void *
madt_sdth_map(vm_paddr_t paddr)
{
	struct acpi_sdth *sdth;
	vm_size_t mapsz;

	sdth = pmap_mapdev(paddr, sizeof(*sdth));
	mapsz = sdth->sdth_len;
	pmap_unmapdev((vm_offset_t)sdth, sizeof(*sdth));

	if (mapsz < sizeof(*sdth))
		return NULL;

	return pmap_mapdev(paddr, mapsz);
}

static void
madt_sdth_unmap(struct acpi_sdth *sdth)
{
	pmap_unmapdev((vm_offset_t)sdth, sdth->sdth_len);
}

static vm_paddr_t
madt_search_xsdt(vm_paddr_t xsdt_paddr)
{
	struct acpi_xsdt *xsdt;
	vm_paddr_t madt_paddr = 0;
	int i, nent;

	if (xsdt_paddr == 0) {
		kprintf("madt_search_xsdt: XSDT paddr == 0\n");
		return 0;
	}

	xsdt = madt_sdth_map(xsdt_paddr);
	if (xsdt == NULL) {
		kprintf("madt_search_xsdt: can't map XSDT\n");
		return 0;
	}

	if (memcmp(xsdt->xsdt_hdr.sdth_sig, ACPI_XSDT_SIG,
		   ACPI_SDTH_SIGLEN) != 0) {
		kprintf("madt_search_xsdt: not XSDT\n");
		goto back;
	}

	if (xsdt->xsdt_hdr.sdth_rev != 1) {
		kprintf("madt_search_xsdt: unsupported XSDT revision %d\n",
			xsdt->xsdt_hdr.sdth_rev);
		goto back;
	}

	nent = (xsdt->xsdt_hdr.sdth_len - sizeof(xsdt->xsdt_hdr)) /
	       sizeof(xsdt->xsdt_ents[0]);
	for (i = 0; i < nent; ++i) {
		struct acpi_sdth *sdth;

		if (xsdt->xsdt_ents[i] == 0)
			continue;

		sdth = madt_sdth_map(xsdt->xsdt_ents[i]);
		if (sdth != NULL) {
			int ret;

			ret = memcmp(sdth->sdth_sig, ACPI_MADT_SIG,
				     ACPI_SDTH_SIGLEN);
			madt_sdth_unmap(sdth);

			if (ret == 0) {
				MADT_VPRINTF("MADT in XSDT\n");
				madt_paddr = xsdt->xsdt_ents[i];
				break;
			}
		}
	}
back:
	madt_sdth_unmap(&xsdt->xsdt_hdr);
	return madt_paddr;
}

static vm_paddr_t
madt_search_rsdt(vm_paddr_t rsdt_paddr)
{
	struct acpi_rsdt *rsdt;
	vm_paddr_t madt_paddr = 0;
	int i, nent;

	if (rsdt_paddr == 0) {
		kprintf("madt_search_rsdt: RSDT paddr == 0\n");
		return 0;
	}

	rsdt = madt_sdth_map(rsdt_paddr);
	if (rsdt == NULL) {
		kprintf("madt_search_rsdt: can't map RSDT\n");
		return 0;
	}

	if (memcmp(rsdt->rsdt_hdr.sdth_sig, ACPI_RSDT_SIG,
		   ACPI_SDTH_SIGLEN) != 0) {
		kprintf("madt_search_rsdt: not RSDT\n");
		goto back;
	}

	if (rsdt->rsdt_hdr.sdth_rev != 1) {
		kprintf("madt_search_rsdt: unsupported RSDT revision %d\n",
			rsdt->rsdt_hdr.sdth_rev);
		goto back;
	}

	nent = (rsdt->rsdt_hdr.sdth_len - sizeof(rsdt->rsdt_hdr)) /
	       sizeof(rsdt->rsdt_ents[0]);
	for (i = 0; i < nent; ++i) {
		struct acpi_sdth *sdth;

		if (rsdt->rsdt_ents[i] == 0)
			continue;

		sdth = madt_sdth_map(rsdt->rsdt_ents[i]);
		if (sdth != NULL) {
			int ret;

			ret = memcmp(sdth->sdth_sig, ACPI_MADT_SIG,
				     ACPI_SDTH_SIGLEN);
			madt_sdth_unmap(sdth);

			if (ret == 0) {
				MADT_VPRINTF("MADT in RSDT\n");
				madt_paddr = rsdt->rsdt_ents[i];
				break;
			}
		}
	}
back:
	madt_sdth_unmap(&rsdt->rsdt_hdr);
	return madt_paddr;
}

static int
madt_check(vm_paddr_t madt_paddr)
{
	struct acpi_madt *madt;
	int error = 0;

	KKASSERT(madt_paddr != 0);

	madt = madt_sdth_map(madt_paddr);
	KKASSERT(madt != NULL);

	/*
	 * MADT in ACPI specification 1.0 - 4.0
	 */
	if (madt->madt_hdr.sdth_rev < 1 || madt->madt_hdr.sdth_rev > 3) {
		kprintf("madt_check: unsupported MADT revision %d\n",
			madt->madt_hdr.sdth_rev);
		error = EOPNOTSUPP;
		goto back;
	}

	if (madt->madt_hdr.sdth_len <
	    sizeof(*madt) - sizeof(madt->madt_ents)) {
		kprintf("madt_check: invalid MADT length %u\n",
			madt->madt_hdr.sdth_len);
		error = EINVAL;
		goto back;
	}
back:
	madt_sdth_unmap(&madt->madt_hdr);
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

static vm_offset_t
madt_lapic_pass1(void)
{
	struct acpi_madt *madt;
	vm_offset_t lapic_addr;
	uint64_t lapic_addr64;
	int error;

	KKASSERT(madt_phyaddr != 0);

	madt = madt_sdth_map(madt_phyaddr);
	KKASSERT(madt != NULL);

	MADT_VPRINTF("LAPIC address 0x%08x, flags %#x\n",
		     madt->madt_lapic_addr, madt->madt_flags);
	lapic_addr = madt->madt_lapic_addr;

	lapic_addr64 = 0;
	error = madt_iterate_entries(madt, madt_lapic_pass1_callback,
				     &lapic_addr64);
	if (error)
		panic("madt_iterate_entries(pass1) failed\n");

	if (lapic_addr64 != 0) {
		kprintf("ACPI MADT: warning 64bits lapic address 0x%llx\n",
			lapic_addr64);
		/* XXX vm_offset_t is 32bits on i386 */
		lapic_addr = lapic_addr64;
	}

	madt_sdth_unmap(&madt->madt_hdr);

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
			mp_set_cpuids(0, lapic_ent->ml_apic_id);
			arg->bsp_found = 1;
		} else {
			mp_set_cpuids(arg->cpu, lapic_ent->ml_apic_id);
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

	madt = madt_sdth_map(madt_phyaddr);
	KKASSERT(madt != NULL);

	bzero(&arg, sizeof(arg));
	arg.cpu = 1;
	arg.bsp_apic_id = bsp_apic_id;

	error = madt_iterate_entries(madt, madt_lapic_pass2_callback, &arg);
	if (error)
		panic("madt_iterate_entries(pass2) failed\n");

	KKASSERT(arg.bsp_found);
	KKASSERT(arg.cpu > 1);
	mp_naps = arg.cpu - 1; /* exclude BSP */

	madt_sdth_unmap(&madt->madt_hdr);

	return 0;
}

struct madt_lapic_probe_cbarg {
	int		cpu_count;
	vm_offset_t	lapic_addr;
};

static int
madt_lapic_probe_callback(void *xarg, const struct acpi_madt_ent *ent)
{
	struct madt_lapic_probe_cbarg *arg = xarg;

	if (ent->me_type == MADT_ENT_LAPIC) {
		const struct acpi_madt_lapic *lapic_ent;

		lapic_ent = (const struct acpi_madt_lapic *)ent;
		if (lapic_ent->ml_flags & MADT_LAPIC_ENABLED)
			arg->cpu_count++;
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

	madt = madt_sdth_map(madt_phyaddr);
	KKASSERT(madt != NULL);

	bzero(&arg, sizeof(arg));
	arg.lapic_addr = madt->madt_lapic_addr;

	error = madt_iterate_entries(madt, madt_lapic_probe_callback, &arg);
	if (!error) {
		if (arg.cpu_count <= 1) {
			kprintf("madt_lapic_probe: "
				"less than 2 CPUs is found\n");
			error = EOPNOTSUPP;
		}
		if (arg.lapic_addr == 0) {
			kprintf("madt_lapic_probe: zero LAPIC address\n");
			error = EOPNOTSUPP;
		}
	}

	madt_sdth_unmap(&madt->madt_hdr);
	return error;
}

static void
madt_lapic_enumerate(struct lapic_enumerator *e)
{
	vm_offset_t lapic_addr;
	int bsp_apic_id;

	KKASSERT(madt_phyaddr != 0);

	lapic_addr = madt_lapic_pass1();
	if (lapic_addr == 0)
		panic("madt_lapic_enumerate: no local apic\n");

	lapic_map(lapic_addr);

	bsp_apic_id = APIC_ID(lapic.id);
	if (madt_lapic_pass2(bsp_apic_id))
		panic("madt_lapic_enumerate: madt_lapic_pass2 failed\n");
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

		/* XXX magic number */
		if (intsrc_ent->mint_src >= 16) {
			kprintf("madt_ioapic_probe: invalid intsrc irq (%d)\n",
				intsrc_ent->mint_src);
			return EINVAL;
		}

		if (intsrc_ent->mint_src == intsrc_ent->mint_gsi) {
			kprintf("ACPI MADT: warning intsrc irq %d "
				"no gsi change\n", intsrc_ent->mint_src);
		}

		if (intsrc_ent->mint_bus != MADT_INT_BUS_ISA) {
			kprintf("ACPI MADT: warning intsrc irq %d "
				"bus is not ISA (%d)\n",
				intsrc_ent->mint_src, intsrc_ent->mint_bus);
		}

		trig = (intsrc_ent->mint_flags & MADT_INT_TRIG_MASK) >>
		       MADT_INT_TRIG_SHIFT;
		if (trig != MADT_INT_TRIG_EDGE &&
		    trig != MADT_INT_TRIG_CONFORM) {
			kprintf("ACPI MADT: warning invalid intsrc irq %d "
				"trig (%d)\n", intsrc_ent->mint_src, trig);
		}

		pola = (intsrc_ent->mint_flags & MADT_INT_POLA_MASK) >>
		       MADT_INT_POLA_SHIFT;
		if (pola != MADT_INT_POLA_HIGH &&
		    pola != MADT_INT_POLA_CONFORM) {
			kprintf("ACPI MADT: warning invalid intsrc irq %d "
				"pola (%d)\n", intsrc_ent->mint_src, pola);
		}
	} else if (ent->me_type == MADT_ENT_IOAPIC) {
		const struct acpi_madt_ioapic *ioapic_ent;

		ioapic_ent = (const struct acpi_madt_ioapic *)ent;
		if (ioapic_ent->mio_addr == 0) {
			kprintf("madt_ioapic_probe: zero IOAPIC address\n");
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

	madt = madt_sdth_map(madt_phyaddr);
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

	madt_sdth_unmap(&madt->madt_hdr);
	return error;
}

static int
madt_ioapic_enum_callback(void *xarg, const struct acpi_madt_ent *ent)
{
	if (ent->me_type == MADT_ENT_INTSRC) {
		const struct acpi_madt_intsrc *intsrc_ent;

		intsrc_ent = (const struct acpi_madt_intsrc *)ent;
		if (intsrc_ent->mint_src == intsrc_ent->mint_gsi ||
		    intsrc_ent->mint_bus != MADT_INT_BUS_ISA)
			return 0;

		MADT_VPRINTF("INTSRC irq %d -> gsi %u\n",
			     intsrc_ent->mint_src, intsrc_ent->mint_gsi);
		ioapic_intsrc(intsrc_ent->mint_src, intsrc_ent->mint_gsi);
	} else if (ent->me_type == MADT_ENT_IOAPIC) {
		const struct acpi_madt_ioapic *ioapic_ent;

		ioapic_ent = (const struct acpi_madt_ioapic *)ent;
		MADT_VPRINTF("IOAPIC addr 0x%08x, apic id %d, gsi base %u\n",
			     ioapic_ent->mio_addr, ioapic_ent->mio_apic_id,
			     ioapic_ent->mio_gsi_base);

		if (!ioapic_use_old) {
			uint32_t ver;
			void *addr;
			int npin;

			addr = ioapic_map(ioapic_ent->mio_addr);

			ver = ioapic_read(addr, IOAPIC_VER);
			npin = ((ver & IOART_VER_MAXREDIR) >>
				MAXREDIRSHIFT) + 1;

			ioapic_add(addr, ioapic_ent->mio_gsi_base, npin);
		}
	}
	return 0;
}

static void
madt_ioapic_enumerate(struct ioapic_enumerator *e)
{
	struct acpi_madt *madt;
	int error;

	KKASSERT(madt_phyaddr != 0);

	madt = madt_sdth_map(madt_phyaddr);
	KKASSERT(madt != NULL);

	error = madt_iterate_entries(madt, madt_ioapic_enum_callback, NULL);
	if (error)
		panic("madt_ioapic_enumerate failed\n");

	madt_sdth_unmap(&madt->madt_hdr);
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
