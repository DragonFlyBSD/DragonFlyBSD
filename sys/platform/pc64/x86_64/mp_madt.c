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
#include <sys/systm.h>

#include <machine/pmap.h>
#include <machine/smp.h>

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

/* MADT Processor Local APIC */
struct acpi_madt_lapic {
	struct acpi_madt_ent	ml_hdr;
	uint8_t			ml_cpu_id;
	uint8_t			ml_apic_id;
	uint32_t		ml_flags;	/* MADT_LAPIC_ */
} __packed;

#define MADT_LAPIC_ENABLED	0x1

typedef	vm_paddr_t		(*madt_search_t)(vm_paddr_t);

static const struct acpi_rsdp	*madt_rsdp_search(const uint8_t *, int);
static void			*madt_sdth_map(vm_paddr_t);
static void			madt_sdth_unmap(struct acpi_sdth *);
static vm_paddr_t		madt_search_xsdt(vm_paddr_t);
static vm_paddr_t		madt_search_rsdt(vm_paddr_t);
static int			madt_parse(vm_paddr_t);

extern u_long	ebda_addr;

int
madt_probe(void)
{
	const struct acpi_rsdp *rsdp;
	madt_search_t search;
	vm_paddr_t search_paddr, madt_paddr;
	vm_size_t mapsz;
	uint8_t *ptr;

	if (ebda_addr != 0) {
		mapsz = ACPI_RSDP_EBDA_MAPSZ;
		ptr = pmap_mapdev(ebda_addr, mapsz);

		rsdp = madt_rsdp_search(ptr, mapsz);
		if (rsdp == NULL) {
			kprintf("madt: RSDP not in EBDA\n");
			pmap_unmapdev((vm_offset_t)ptr, mapsz);

			ptr = NULL;
			mapsz = 0;
		} else {
			kprintf("madt: RSDP in EBDA\n");
			goto found_rsdp;
		}
	}

	mapsz = ACPI_RSDP_BIOS_MAPSZ;
	ptr = pmap_mapdev(ACPI_RSDP_BIOS_MAPADDR, mapsz);

	rsdp = madt_rsdp_search(ptr, mapsz);
	if (rsdp == NULL) {
		kprintf("madt_probe: no RSDP\n");
		pmap_unmapdev((vm_offset_t)ptr, mapsz);
		return ENOENT;
	} else {
		kprintf("madt: RSDP in BIOS mem\n");
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
		return ENOENT;
	}
	return madt_parse(madt_paddr);
}

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
				kprintf("madt: MADT in XSDT\n");
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
				kprintf("madt: MADT in RSDT\n");
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
madt_parse(vm_paddr_t madt_paddr)
{
	struct acpi_madt *madt;
	int size, cur, error;

	KKASSERT(madt_paddr != 0);

	madt = madt_sdth_map(madt_paddr);
	KKASSERT(madt != NULL);

	if (madt->madt_hdr.sdth_rev != 1 && madt->madt_hdr.sdth_rev != 2) {
		kprintf("madt_parse: unsupported MADT revision %d\n",
			madt->madt_hdr.sdth_rev);
		error = EOPNOTSUPP;
		goto back;
	}

	if (madt->madt_hdr.sdth_len <
	    sizeof(*madt) - sizeof(madt->madt_ents)) {
		kprintf("madt_parse: invalid MADT length %u\n",
			madt->madt_hdr.sdth_len);
		error = EINVAL;
		goto back;
	}

	kprintf("madt: LAPIC address 0x%08x, flags %#x\n",
		madt->madt_lapic_addr, madt->madt_flags);

	size = madt->madt_hdr.sdth_len -
	       (sizeof(*madt) - sizeof(madt->madt_ents));
	cur = 0;
	error = 0;

	while (size - cur > sizeof(struct acpi_madt_ent)) {
		const struct acpi_madt_ent *ent;

		ent = (const struct acpi_madt_ent *)&madt->madt_ents[cur];
		if (ent->me_len < sizeof(*ent)) {
			kprintf("madt_parse: invalid MADT entry len %d\n",
				ent->me_len);
			error = EINVAL;
			break;
		}
		if (ent->me_len > (size - cur)) {
			kprintf("madt_parse: invalid MADT entry len %d, "
				"> table length\n", ent->me_len);
			error = EINVAL;
			break;
		}

		cur += ent->me_len;

		if (ent->me_type == MADT_ENT_LAPIC) {
			const struct acpi_madt_lapic *lapic_ent;

			if (ent->me_len < sizeof(*lapic_ent)) {
				kprintf("madt_parse: invalid MADT lapic entry "
					"len %d\n", ent->me_len);
				error = EINVAL;
				break;
			}
			lapic_ent = (const struct acpi_madt_lapic *)ent;
			if (lapic_ent->ml_flags & MADT_LAPIC_ENABLED) {
				kprintf("madt: cpu_id %d, apic_id %d\n",
					lapic_ent->ml_cpu_id,
					lapic_ent->ml_apic_id);
			}
		}
	}
back:
	madt_sdth_unmap(&madt->madt_hdr);
	return error;
}
