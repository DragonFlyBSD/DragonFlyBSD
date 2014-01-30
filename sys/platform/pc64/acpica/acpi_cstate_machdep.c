/*
 * Copyright (c) 2014 The DragonFly Project.  All rights reserved.
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
#include <sys/globaldata.h>

#include <machine/md_var.h>
#include <machine/cpufunc.h>
#include <machine/cpufreq.h>
#include <machine/cputypes.h>
#include <machine/specialreg.h>

#include "acpi.h"
#include "acpi_cpu_cstate.h"

int
acpi_cst_md_cx_setup(struct acpi_cst_cx *cx)
{
	if (cpu_vendor_id != CPU_VENDOR_INTEL) {
		/*
		 * No optimization for non-Intel CPUs so far.
		 *
		 * Hardware fixed resouce is not supported for
		 * C1+ state yet.
		 */
		if (cx->type == ACPI_STATE_C1 &&
		    cx->gas.SpaceId == ACPI_ADR_SPACE_FIXED_HARDWARE)
			return 0;
		if (cx->gas.SpaceId != ACPI_ADR_SPACE_SYSTEM_IO &&
		    cx->gas.SpaceId != ACPI_ADR_SPACE_SYSTEM_MEMORY)
			return EINVAL;
		return 0;
	}

	if (cx->type == ACPI_STATE_C1 &&
	    cx->gas.SpaceId == ACPI_ADR_SPACE_FIXED_HARDWARE) {
		/* TODO: filter C1 I/O then halt */
		return 0;
	}

	/* TODO: We don't support fixed hardware yet */
	if (cx->gas.SpaceId != ACPI_ADR_SPACE_SYSTEM_IO &&
	    cx->gas.SpaceId != ACPI_ADR_SPACE_SYSTEM_MEMORY)
		return EINVAL;

	if (cx->type >= ACPI_STATE_C3) {
		if (CPUID_TO_FAMILY(cpu_id) > 0xf ||
		    (CPUID_TO_FAMILY(cpu_id) == 0x6 &&
		     CPUID_TO_MODEL(cpu_id) >= 0xf)) {
			/*
			 * Pentium dual-core, Core 2 and beyond do not
			 * need any additional activities to enter C3(+).
			 */
			cx->preamble = ACPI_CST_CX_PREAMBLE_NONE;
		} else if ((acpi_cst_quirks & ACPI_CST_QUIRK_NO_BM) == 0) {
			/*
			 * Intel CPUs support bus master operation for
			 * entering C3(+) even on MP system.
			 */
			cx->preamble = ACPI_CST_CX_PREAMBLE_BM_ARB;
		}
	}
	return 0;
}
