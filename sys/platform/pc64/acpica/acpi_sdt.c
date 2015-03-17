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

#include <contrib/dev/acpica/source/include/acpi.h>

#include "acpi_sdt_var.h"

#define SDT_VPRINTF(fmt, arg...) \
do { \
	if (bootverbose) \
		kprintf("ACPI SDT: " fmt , ##arg); \
} while (0)

typedef	vm_paddr_t		(*sdt_search_t)(vm_paddr_t, const uint8_t *);

static const ACPI_TABLE_RSDP	*sdt_rsdp_search(const uint8_t *, int);
static vm_paddr_t		sdt_search_xsdt(vm_paddr_t, const uint8_t *);
static vm_paddr_t		sdt_search_rsdt(vm_paddr_t, const uint8_t *);

extern u_long			ebda_addr;

static sdt_search_t		sdt_search_func;
static vm_paddr_t		sdt_search_paddr;

static void
sdt_probe(void)
{
	const ACPI_TABLE_RSDP *rsdp;
	vm_size_t mapsz;
	uint8_t *ptr;

	if (ebda_addr != 0) {
		mapsz = ACPI_EBDA_WINDOW_SIZE;
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

	mapsz = ACPI_HI_RSDP_WINDOW_SIZE;
	ptr = pmap_mapdev(ACPI_HI_RSDP_WINDOW_BASE, mapsz);

	rsdp = sdt_rsdp_search(ptr, mapsz);
	if (rsdp == NULL) {
		kprintf("sdt_probe: no RSDP\n");
		pmap_unmapdev((vm_offset_t)ptr, mapsz);
		return;
	} else {
		SDT_VPRINTF("RSDP in BIOS mem\n");
	}

found_rsdp:
	if (rsdp->Revision != 2 /* || AcpiGbl_DoNotUseXsdt */) {
		sdt_search_func = sdt_search_rsdt;
		sdt_search_paddr = rsdp->RsdtPhysicalAddress;
	} else {
		sdt_search_func = sdt_search_xsdt;
		sdt_search_paddr = rsdp->XsdtPhysicalAddress;
	}
	pmap_unmapdev((vm_offset_t)ptr, mapsz);
}
SYSINIT(sdt_probe, SI_BOOT2_PRESMP, SI_ORDER_FIRST, sdt_probe, 0);

static const ACPI_TABLE_RSDP *
sdt_rsdp_search(const uint8_t *target, int size)
{
	const ACPI_TABLE_RSDP *rsdp;
	int i;

	KKASSERT(size > sizeof(*rsdp));

	for (i = 0; i < size - sizeof(*rsdp); i += ACPI_RSDP_SCAN_STEP) {
		rsdp = (const ACPI_TABLE_RSDP *)&target[i];
		if (memcmp(rsdp->Signature, ACPI_SIG_RSDP,
			   sizeof(rsdp->Signature)) == 0)
			return rsdp;
	}
	return NULL;
}

void *
sdt_sdth_map(vm_paddr_t paddr)
{
	ACPI_TABLE_HEADER *sdth;
	vm_size_t mapsz;

	sdth = pmap_mapdev(paddr, sizeof(*sdth));
	mapsz = sdth->Length;
	pmap_unmapdev((vm_offset_t)sdth, sizeof(*sdth));

	if (mapsz < sizeof(*sdth))
		return NULL;

	return pmap_mapdev(paddr, mapsz);
}

void
sdt_sdth_unmap(ACPI_TABLE_HEADER *sdth)
{
	pmap_unmapdev((vm_offset_t)sdth, sdth->Length);
}

static vm_paddr_t
sdt_search_xsdt(vm_paddr_t xsdt_paddr, const uint8_t *sig)
{
	ACPI_TABLE_XSDT *xsdt;
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

	if (memcmp(xsdt->Header.Signature, ACPI_SIG_XSDT,
		   ACPI_NAME_SIZE) != 0) {
		kprintf("sdt_search_xsdt: not XSDT\n");
		goto back;
	}

	if (xsdt->Header.Revision != 1) {
		kprintf("sdt_search_xsdt: unknown XSDT revision %d\n",
			xsdt->Header.Revision);
	}

	if (xsdt->Header.Length < sizeof(xsdt->Header)) {
		kprintf("sdt_search_xsdt: invalid XSDT length %u\n",
			xsdt->Header.Length);
		goto back;
	}

	nent = (xsdt->Header.Length - sizeof(xsdt->Header)) /
	       sizeof(xsdt->TableOffsetEntry[0]);
	for (i = 0; i < nent; ++i) {
		ACPI_TABLE_HEADER *sdth;

		if (xsdt->TableOffsetEntry[i] == 0)
			continue;

		sdth = sdt_sdth_map(xsdt->TableOffsetEntry[i]);
		if (sdth != NULL) {
			int ret;

			ret = memcmp(sdth->Signature, sig, ACPI_NAME_SIZE);
			sdt_sdth_unmap(sdth);

			if (ret == 0) {
				sdt_paddr = xsdt->TableOffsetEntry[i];
				break;
			}
		}
	}
back:
	sdt_sdth_unmap(&xsdt->Header);
	return sdt_paddr;
}

static vm_paddr_t
sdt_search_rsdt(vm_paddr_t rsdt_paddr, const uint8_t *sig)
{
	ACPI_TABLE_RSDT *rsdt;
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

	if (memcmp(rsdt->Header.Signature, ACPI_SIG_RSDT,
		   ACPI_NAME_SIZE) != 0) {
		kprintf("sdt_search_rsdt: not RSDT\n");
		goto back;
	}

	if (rsdt->Header.Revision != 1) {
		kprintf("sdt_search_rsdt: unknown RSDT revision %d\n",
			rsdt->Header.Revision);
	}

	if (rsdt->Header.Length < sizeof(rsdt->Header)) {
		kprintf("sdt_search_rsdt: invalid RSDT length %u\n",
			rsdt->Header.Length);
		goto back;
	}

	nent = (rsdt->Header.Length - sizeof(rsdt->Header)) /
	       sizeof(rsdt->TableOffsetEntry[0]);
	for (i = 0; i < nent; ++i) {
		ACPI_TABLE_HEADER *sdth;

		if (rsdt->TableOffsetEntry[i] == 0)
			continue;

		sdth = sdt_sdth_map(rsdt->TableOffsetEntry[i]);
		if (sdth != NULL) {
			int ret;

			ret = memcmp(sdth->Signature, sig, ACPI_NAME_SIZE);
			sdt_sdth_unmap(sdth);

			if (ret == 0) {
				sdt_paddr = rsdt->TableOffsetEntry[i];
				break;
			}
		}
	}
back:
	sdt_sdth_unmap(&rsdt->Header);
	return sdt_paddr;
}

vm_paddr_t
sdt_search(const uint8_t *sig)
{
	if (sdt_search_func == NULL)
		return 0;
	return sdt_search_func(sdt_search_paddr, sig);
}
