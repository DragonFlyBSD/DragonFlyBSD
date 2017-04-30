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
#include <vm/vm_page.h>

#include "acpi.h"
#include "acpi_sdt_var.h"

typedef union srat_entry {
	ACPI_SUBTABLE_HEADER	head;
	ACPI_SRAT_CPU_AFFINITY	cpu;
	ACPI_SRAT_MEM_AFFINITY	mem;
	ACPI_SRAT_X2APIC_CPU_AFFINITY	x2apic;
	ACPI_SRAT_GICC_AFFINITY	gicc;
} srat_entry_t;

static void
srat_probe(void)
{
	vm_paddr_t srat_paddr;
	ACPI_TABLE_SRAT *srat;
	srat_entry_t *mem;
	srat_entry_t *cpu;
	int error = 0;
	int enabled = 1;
	int disabled = 0;

	/*
	 * Allow NUMA to be disabled by either setting kern.numa_disable
	 * to 1 or by setting hw.acpi.srat.enabled to 0.
	 */
	kgetenv_int("kern.numa_disable", &disabled);
	kgetenv_int("hw.acpi.srat.enabled", &enabled);
	if (disabled || enabled == 0)
		return;

	/*
	 * Map the SRAT if it exists
	 */
	srat_paddr = sdt_search(ACPI_SIG_SRAT);
	if (srat_paddr == 0) {
		kprintf("srat_probe: can't locate SRAT\n");
		return;
	}

	srat = sdt_sdth_map(srat_paddr);
	KKASSERT(srat != NULL);

	if (srat->Header.Length < sizeof(*srat)) {
		kprintf("acpi: invalid SRAT length %u\n",
			srat->Header.Length);
		error = EINVAL;
		goto done;
	}

	cpu = NULL;

	for (mem = (srat_entry_t *)(srat + 1);
	     (char *)mem < (char *)srat + srat->Header.Length;
	     mem = (srat_entry_t *)((char *)mem + mem->head.Length)) {
		/*
		 * Mem scan memory affinity only
		 */
		if (mem->head.Type != ACPI_SRAT_TYPE_MEMORY_AFFINITY)
			continue;
		if ((mem->mem.Flags & ACPI_SRAT_MEM_ENABLED) == 0)
			continue;

		kprintf("MemAffinity %016jx,%ldMB Prox=%u ",
			mem->mem.BaseAddress,
			mem->mem.Length / (1024 * 1024),
			mem->mem.ProximityDomain);

		/*
		 * Look for associated cpu affinity
		 */
		if (cpu == NULL ||
		    mem->mem.ProximityDomain != cpu->cpu.ProximityDomainLo) {
			for (cpu = (srat_entry_t *)(srat + 1);
			     (char *)cpu < (char *)srat + srat->Header.Length;
			     cpu = (srat_entry_t *)((char *)cpu +
						    cpu->head.Length)) {
				if (cpu->head.Type !=
				    ACPI_SRAT_TYPE_CPU_AFFINITY)
					continue;
				if ((cpu->cpu.Flags &
				     ACPI_SRAT_CPU_USE_AFFINITY) == 0)
					continue;
				if (mem->mem.ProximityDomain ==
				    cpu->cpu.ProximityDomainLo) {
					break;
				}
			}
			if ((char *)cpu >= (char *)srat + srat->Header.Length)
				cpu = NULL;
		}
		if (cpu) {
			kprintf("CpuApicId %02x Socket %d\n",
				cpu->cpu.ApicId,
				get_chip_ID_from_APICID(cpu->cpu.ApicId));
			vm_numa_organize(mem->mem.BaseAddress,
					 mem->mem.Length,
				    get_chip_ID_from_APICID(cpu->cpu.ApicId));
		} else {
			kprintf("(not found)\n");
		}
	}

done:
	sdt_sdth_unmap(&srat->Header);
}

SYSINIT(srat_probe, SI_BOOT2_NUMA, SI_ORDER_FIRST, srat_probe, 0);
