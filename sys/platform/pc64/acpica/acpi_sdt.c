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

#include "acpi_sdt.h"
#include "acpi_sdt_var.h"

#define SDT_VPRINTF(fmt, arg...) \
do { \
	if (bootverbose) \
		kprintf("ACPI SDT: " fmt , ##arg); \
} while (0)

#define ACPI_RSDP_EBDA_MAPSZ	1024
#define ACPI_RSDP_BIOS_MAPSZ	0x20000
#define ACPI_RSDP_BIOS_MAPADDR	0xe0000

#define ACPI_RSDP_ALIGN		16

#define ACPI_RSDP_SIGLEN	8
#define ACPI_RSDP_SIG		"RSD PTR "

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

typedef	vm_paddr_t		(*sdt_search_t)(vm_paddr_t, const uint8_t *);

static const struct acpi_rsdp	*sdt_rsdp_search(const uint8_t *, int);
static vm_paddr_t		sdt_search_xsdt(vm_paddr_t, const uint8_t *);
static vm_paddr_t		sdt_search_rsdt(vm_paddr_t, const uint8_t *);

extern u_long			ebda_addr;

static sdt_search_t		sdt_search_func;
static vm_paddr_t		sdt_search_paddr;

static void
sdt_probe(void)
{
	const struct acpi_rsdp *rsdp;
	vm_size_t mapsz;
	uint8_t *ptr;

	if (ebda_addr != 0) {
		mapsz = ACPI_RSDP_EBDA_MAPSZ;
		ptr = pmap_mapdev(ebda_addr, mapsz);

		rsdp = sdt_rsdp_search(ptr, mapsz);
		if (rsdp == NULL) {
			SDT_VPRINTF("RSDP not in EBDA\n");
			pmap_unmapdev((vm_offset_t)ptr, mapsz);

			ptr = NULL;
			mapsz = 0;
		} else {
			SDT_VPRINTF("RSDP in EBDA\n");
			goto found_rsdp;
		}
	}

	mapsz = ACPI_RSDP_BIOS_MAPSZ;
	ptr = pmap_mapdev(ACPI_RSDP_BIOS_MAPADDR, mapsz);

	rsdp = sdt_rsdp_search(ptr, mapsz);
	if (rsdp == NULL) {
		kprintf("sdt_probe: no RSDP\n");
		pmap_unmapdev((vm_offset_t)ptr, mapsz);
		return;
	} else {
		SDT_VPRINTF("RSDP in BIOS mem\n");
	}

found_rsdp:
	if (rsdp->rsdp_rev != 2) {
		sdt_search_func = sdt_search_rsdt;
		sdt_search_paddr = rsdp->rsdp_rsdt;
	} else {
		sdt_search_func = sdt_search_xsdt;
		sdt_search_paddr = rsdp->rsdp_xsdt;
	}
	pmap_unmapdev((vm_offset_t)ptr, mapsz);
}
SYSINIT(sdt_probe, SI_BOOT2_PRESMP, SI_ORDER_FIRST, sdt_probe, 0);

static const struct acpi_rsdp *
sdt_rsdp_search(const uint8_t *target, int size)
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

void *
sdt_sdth_map(vm_paddr_t paddr)
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

void
sdt_sdth_unmap(struct acpi_sdth *sdth)
{
	pmap_unmapdev((vm_offset_t)sdth, sdth->sdth_len);
}

static vm_paddr_t
sdt_search_xsdt(vm_paddr_t xsdt_paddr, const uint8_t *sig)
{
	struct acpi_xsdt *xsdt;
	vm_paddr_t sdt_paddr = 0;
	int i, nent;

	if (xsdt_paddr == 0) {
		kprintf("sdt_search_xsdt: XSDT paddr == 0\n");
		return 0;
	}

	xsdt = sdt_sdth_map(xsdt_paddr);
	if (xsdt == NULL) {
		kprintf("sdt_search_xsdt: can't map XSDT\n");
		return 0;
	}

	if (memcmp(xsdt->xsdt_hdr.sdth_sig, ACPI_XSDT_SIG,
		   ACPI_SDTH_SIGLEN) != 0) {
		kprintf("sdt_search_xsdt: not XSDT\n");
		goto back;
	}

	if (xsdt->xsdt_hdr.sdth_rev != 1) {
		kprintf("sdt_search_xsdt: unknown XSDT revision %d\n",
			xsdt->xsdt_hdr.sdth_rev);
	}

	if (xsdt->xsdt_hdr.sdth_len < sizeof(xsdt->xsdt_hdr)) {
		kprintf("sdt_search_xsdt: invalid XSDT length %u\n",
			xsdt->xsdt_hdr.sdth_len);
		goto back;
	}

	nent = (xsdt->xsdt_hdr.sdth_len - sizeof(xsdt->xsdt_hdr)) /
	       sizeof(xsdt->xsdt_ents[0]);
	for (i = 0; i < nent; ++i) {
		struct acpi_sdth *sdth;

		if (xsdt->xsdt_ents[i] == 0)
			continue;

		sdth = sdt_sdth_map(xsdt->xsdt_ents[i]);
		if (sdth != NULL) {
			int ret;

			ret = memcmp(sdth->sdth_sig, sig, ACPI_SDTH_SIGLEN);
			sdt_sdth_unmap(sdth);

			if (ret == 0) {
				sdt_paddr = xsdt->xsdt_ents[i];
				break;
			}
		}
	}
back:
	sdt_sdth_unmap(&xsdt->xsdt_hdr);
	return sdt_paddr;
}

static vm_paddr_t
sdt_search_rsdt(vm_paddr_t rsdt_paddr, const uint8_t *sig)
{
	struct acpi_rsdt *rsdt;
	vm_paddr_t sdt_paddr = 0;
	int i, nent;

	if (rsdt_paddr == 0) {
		kprintf("sdt_search_rsdt: RSDT paddr == 0\n");
		return 0;
	}

	rsdt = sdt_sdth_map(rsdt_paddr);
	if (rsdt == NULL) {
		kprintf("sdt_search_rsdt: can't map RSDT\n");
		return 0;
	}

	if (memcmp(rsdt->rsdt_hdr.sdth_sig, ACPI_RSDT_SIG,
		   ACPI_SDTH_SIGLEN) != 0) {
		kprintf("sdt_search_rsdt: not RSDT\n");
		goto back;
	}

	if (rsdt->rsdt_hdr.sdth_rev != 1) {
		kprintf("sdt_search_rsdt: unknown RSDT revision %d\n",
			rsdt->rsdt_hdr.sdth_rev);
	}

	if (rsdt->rsdt_hdr.sdth_len < sizeof(rsdt->rsdt_hdr)) {
		kprintf("sdt_search_rsdt: invalid RSDT length %u\n",
			rsdt->rsdt_hdr.sdth_len);
		goto back;
	}

	nent = (rsdt->rsdt_hdr.sdth_len - sizeof(rsdt->rsdt_hdr)) /
	       sizeof(rsdt->rsdt_ents[0]);
	for (i = 0; i < nent; ++i) {
		struct acpi_sdth *sdth;

		if (rsdt->rsdt_ents[i] == 0)
			continue;

		sdth = sdt_sdth_map(rsdt->rsdt_ents[i]);
		if (sdth != NULL) {
			int ret;

			ret = memcmp(sdth->sdth_sig, sig, ACPI_SDTH_SIGLEN);
			sdt_sdth_unmap(sdth);

			if (ret == 0) {
				sdt_paddr = rsdt->rsdt_ents[i];
				break;
			}
		}
	}
back:
	sdt_sdth_unmap(&rsdt->rsdt_hdr);
	return sdt_paddr;
}

vm_paddr_t
sdt_search(const uint8_t *sig)
{
	if (sdt_search_func == NULL)
		return 0;
	return sdt_search_func(sdt_search_paddr, sig);
}
