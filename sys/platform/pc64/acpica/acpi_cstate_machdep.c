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

/* GAS.BitWidth */
#define ACPI_GAS_INTEL_VENDOR			1

/* GAS.BitOffset */
#define ACPI_GAS_INTEL_CLASS_C1_IO_HALT		1
#define ACPI_GAS_INTEL_CLASS_CX_NATIVE		2

/* GAS.AccessWidth */
#define ACPI_GAS_INTEL_ARG1_HWCOORD		0x1
#define ACPI_GAS_INTEL_ARG1_BM_STS		0x2

/* GAS.Address */
#define ACPI_GAS_INTEL_ARG0_MWAIT_HINTMASK	0xffffffff

static int		acpi_cst_cx_mwait_setup(struct acpi_cst_cx *);
static void		acpi_cst_cx_mwait_enter(const struct acpi_cst_cx *);

int
acpi_cst_md_cx_setup(struct acpi_cst_cx *cx)
{
	int error;

	if (cpu_vendor_id != CPU_VENDOR_INTEL) {
		/*
		 * No optimization for non-Intel CPUs so far.
		 *
		 * Hardware fixed resource is not supported for
		 * C1+ state yet.
		 */
		if (cx->type == ACPI_STATE_C1 &&
		    cx->gas.SpaceId == ACPI_ADR_SPACE_FIXED_HARDWARE)
			return 0;
		if (cx->gas.SpaceId != ACPI_ADR_SPACE_SYSTEM_IO &&
		    cx->gas.SpaceId != ACPI_ADR_SPACE_SYSTEM_MEMORY) {
			kprintf("C%d: invalid SpaceId %d\n", cx->type,
			    cx->gas.SpaceId);
			return EINVAL;
		}
		return 0;
	}

	switch (cx->gas.SpaceId) {
	case ACPI_ADR_SPACE_SYSTEM_IO:
	case ACPI_ADR_SPACE_SYSTEM_MEMORY:
		break;

	case ACPI_ADR_SPACE_FIXED_HARDWARE:
		error = acpi_cst_cx_mwait_setup(cx);
		if (error)
			return error;
		break;

	default:
		kprintf("C%d: invalid SpaceId %d\n", cx->type, cx->gas.SpaceId);
		return EINVAL;
	}

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

static int
acpi_cst_cx_mwait_setup(struct acpi_cst_cx *cx)
{
	uint32_t eax_hint;

	if (bootverbose) {
		kprintf("C%d: BitWidth(vendor) %d, BitOffset(class) %d, "
		    "Address(arg0) 0x%jx, AccessWidth(arg1) 0x%x\n", cx->type,
		    cx->gas.BitWidth, cx->gas.BitOffset,
		    (uintmax_t)cx->gas.Address, cx->gas.AccessWidth);
	}

	if (cx->type == ACPI_STATE_C1) {
		/* XXX mwait */
		/* XXX I/O then halt */
		return 0;
	}

	if (cx->gas.BitOffset != ACPI_GAS_INTEL_CLASS_CX_NATIVE)
		return EINVAL;

	if ((cpu_feature2 & CPUID2_MON) == 0)
		return EOPNOTSUPP;
	if ((cpu_mwait_feature & (CPUID_MWAIT_EXT | CPUID_MWAIT_INTBRK)) !=
	    (CPUID_MWAIT_EXT | CPUID_MWAIT_INTBRK))
		return EOPNOTSUPP;

	eax_hint = cx->gas.Address & ACPI_GAS_INTEL_ARG0_MWAIT_HINTMASK;
	if (bootverbose) {
		kprintf("C%d -> cpu specific C%d sub state %d\n", cx->type,
		    MWAIT_EAX_TO_CX(eax_hint), MWAIT_EAX_TO_CX_SUB(eax_hint));
	}

	if (!cpu_mwait_hint_valid(eax_hint)) {
		kprintf("C%d: invalid mwait hint 0x%08x\n", cx->type, eax_hint);
		return EINVAL;
	}

	cx->md_arg0 = eax_hint;
	cx->enter = acpi_cst_cx_mwait_enter;

	if ((cx->gas.AccessWidth & ACPI_GAS_INTEL_ARG1_BM_STS) == 0) {
		cpu_mwait_cx_no_bmsts();
		if (cx->type >= ACPI_STATE_C3)
			cx->flags &= ~ACPI_CST_CX_FLAG_BM_STS;
	}

	if (cx->type < ACPI_STATE_C3 && MWAIT_EAX_TO_CX(eax_hint) >= 3) {
		/*
		 * If BIOS maps shallow ACPI C-state (<C3) to deep CPU
		 * specific C-state (>=C3), it implies no bus mastering
		 * operations are needed before entering deep CPU specific
		 * C-states.
		 */
		cpu_mwait_cx_no_bmsts();
		cpu_mwait_cx_no_bmarb();
	}

	return 0;
}

static void
acpi_cst_cx_mwait_enter(const struct acpi_cst_cx *cx)
{
	struct globaldata *gd = mycpu;
	int reqflags;

	reqflags = gd->gd_reqflags;
	if ((reqflags & RQF_IDLECHECK_WK_MASK) == 0) {
		cpu_mmw_pause_int(&gd->gd_reqflags, reqflags, cx->md_arg0,
		    MWAIT_ECX_INTBRK);
	}
}
