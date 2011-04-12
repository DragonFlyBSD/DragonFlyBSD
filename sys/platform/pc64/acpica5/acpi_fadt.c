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

#include <machine/pmap.h>
#include <machine/smp.h>
#include <machine/md_var.h>
#include <machine/specialreg.h>
#include <machine_base/apic/mpapic.h>

#include "acpi_sdt.h"
#include "acpi_sdt_var.h"

#define FADT_VPRINTF(fmt, arg...) \
do { \
	if (bootverbose) \
		kprintf("ACPI FADT: " fmt , ##arg); \
} while (0)

/* Fixed ACPI Description Table */
struct acpi_fadt {
	struct acpi_sdth	fadt_hdr;
	uint32_t		fadt_fw_ctrl;
	uint32_t		fadt_dsdt;
	uint8_t			fadt_rsvd1;
	uint8_t			fadt_pm_prof;
	uint16_t		fadt_sci_int;
	uint32_t		fadt_smi_cmd;
	uint8_t			fadt_acpi_en;
	uint8_t			fadt_acpi_dis;
	uint8_t			fadt_s4bios;
	uint8_t			fadt_pstate;
	/* More ... */
} __packed;

static int			acpi_sci_irq = -1;

static void
fadt_probe(void)
{
	struct acpi_fadt *fadt;
	vm_paddr_t fadt_paddr;

	fadt_paddr = sdt_search(ACPI_FADT_SIG);
	if (fadt_paddr == 0) {
		kprintf("fadt_probe: can't locate FADT\n");
		return;
	}

	fadt = sdt_sdth_map(fadt_paddr);
	KKASSERT(fadt != NULL);

	/*
	 * FADT in ACPI specification 1.0 - 4.0
	 */
	if (fadt->fadt_hdr.sdth_rev < 1 || fadt->fadt_hdr.sdth_rev > 4) {
		kprintf("fadt_probe: unsupported FADT revision %d\n",
			fadt->fadt_hdr.sdth_rev);
		goto back;
	}

	if (fadt->fadt_hdr.sdth_len < sizeof(*fadt)) {
		kprintf("fadt_probe: invalid FADT length %u\n",
			fadt->fadt_hdr.sdth_len);
		goto back;
	}

	acpi_sci_irq = fadt->fadt_sci_int;
	FADT_VPRINTF("ACPI FADT: SCI irq %d\n", acpi_sci_irq);

back:
	sdt_sdth_unmap(&fadt->fadt_hdr);
}
SYSINIT(fadt_probe, SI_BOOT2_PRESMP, SI_ORDER_SECOND, fadt_probe, 0);
