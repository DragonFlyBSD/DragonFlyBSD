/*-
 * Copyright (c) KATO Takenori, 1997, 1998.
 * Copyright (c) 2008 The DragonFly Project.
 * 
 * All rights reserved.  Unpublished rights reserved under the copyright
 * laws of Japan.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer as
 *    the first lines of this file unmodified.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "opt_cpu.h"

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/sysctl.h>

#include <machine/cputypes.h>
#include <machine/md_var.h>
#include <machine/specialreg.h>
#include <machine/smp.h>

#include <vm/vm.h>
#include <vm/pmap.h>

static int	hw_instruction_sse;
SYSCTL_INT(_hw, OID_AUTO, instruction_sse, CTLFLAG_RD,
    &hw_instruction_sse, 0, "SIMD/MMX2 instructions available in CPU");

int	cpu;			/* Are we 386, 386sx, 486, etc? */
u_int	cpu_feature;		/* Feature flags */
u_int	cpu_feature2;		/* Feature flags */
u_int	amd_feature;		/* AMD feature flags */
u_int	amd_feature2;		/* AMD feature flags */
u_int	via_feature_rng;	/* VIA RNG features */
u_int	via_feature_xcrypt;	/* VIA ACE features */
u_int	cpu_high;		/* Highest arg to CPUID */
u_int	cpu_exthigh;		/* Highest arg to extended CPUID */
u_int	cpu_id;			/* Stepping ID */
u_int	cpu_procinfo;		/* HyperThreading Info / Brand Index / CLFUSH */
u_int	cpu_procinfo2;		/* Multicore info */
char	cpu_vendor[20];		/* CPU Origin code */
u_int	cpu_vendor_id;		/* CPU vendor ID */
u_int	cpu_fxsr;		/* SSE enabled */
u_int	cpu_xsave;		/* AVX enabled by OS*/
u_int	cpu_mxcsr_mask;		/* Valid bits in mxcsr */
u_int	cpu_clflush_line_size = 32;	/* Default CLFLUSH line size */
u_int	cpu_stdext_feature;
u_int	cpu_thermal_feature;
u_int	cpu_mwait_feature;
u_int	cpu_mwait_extemu;

/*
 * -1: automatic (enable on h/w, disable on VMs)
 * 0: disable
 * 1: enable (where available)
 */
static int hw_clflush_enable = -1;

SYSCTL_INT(_hw, OID_AUTO, clflush_enable, CTLFLAG_RD, &hw_clflush_enable, 0,
	   "");

SYSCTL_UINT(_hw, OID_AUTO, via_feature_rng, CTLFLAG_RD,
	&via_feature_rng, 0, "VIA C3/C7 RNG feature available in CPU");
SYSCTL_UINT(_hw, OID_AUTO, via_feature_xcrypt, CTLFLAG_RD,
	&via_feature_xcrypt, 0, "VIA C3/C7 xcrypt feature available in CPU");

/*
 * Initialize special VIA C3/C7 features
 */
static void
init_via(void)
{
	u_int regs[4], val;
	u_int64_t msreg;

	do_cpuid(0xc0000000, regs);
	val = regs[0];
	if (val >= 0xc0000001) {
		do_cpuid(0xc0000001, regs);
		val = regs[3];
	} else
		val = 0;

	/* Enable RNG if present and disabled */
	if (val & VIA_CPUID_HAS_RNG) {
		if (!(val & VIA_CPUID_DO_RNG)) {
			msreg = rdmsr(0x110B);
			msreg |= 0x40;
			wrmsr(0x110B, msreg);
		}
		via_feature_rng = VIA_HAS_RNG;
	}
	/* Enable AES engine if present and disabled */
	if (val & VIA_CPUID_HAS_ACE) {
		if (!(val & VIA_CPUID_DO_ACE)) {
			msreg = rdmsr(0x1107);
			msreg |= (0x01 << 28);
			wrmsr(0x1107, msreg);
		}
		via_feature_xcrypt |= VIA_HAS_AES;
	}
	/* Enable ACE2 engine if present and disabled */
	if (val & VIA_CPUID_HAS_ACE2) {
		if (!(val & VIA_CPUID_DO_ACE2)) {
			msreg = rdmsr(0x1107);
			msreg |= (0x01 << 28);
			wrmsr(0x1107, msreg);
		}
		via_feature_xcrypt |= VIA_HAS_AESCTR;
	}
	/* Enable SHA engine if present and disabled */
	if (val & VIA_CPUID_HAS_PHE) {
		if (!(val & VIA_CPUID_DO_PHE)) {
			msreg = rdmsr(0x1107);
			msreg |= (0x01 << 28/**/);
			wrmsr(0x1107, msreg);
		}
		via_feature_xcrypt |= VIA_HAS_SHA;
	}
	/* Enable MM engine if present and disabled */
	if (val & VIA_CPUID_HAS_PMM) {
		if (!(val & VIA_CPUID_DO_PMM)) {
			msreg = rdmsr(0x1107);
			msreg |= (0x01 << 28/**/);
			wrmsr(0x1107, msreg);
		}
		via_feature_xcrypt |= VIA_HAS_MM;
	}
}

/*
 * Initialize CPU control registers
 */
void
initializecpu(int cpu)
{
	uint64_t msr;

	/*Check for FXSR and SSE support and enable if available.*/
	if ((cpu_feature & CPUID_XMM) && (cpu_feature & CPUID_FXSR)) {
		load_cr4(rcr4() | CR4_FXSR | CR4_XMM);
		cpu_fxsr = hw_instruction_sse = 1;
	}

	if (cpu == 0) {
		/* Check if we are running in a hypervisor. */
		vmm_guest = detect_virtual();
		if (vmm_guest == VMM_GUEST_NONE && (cpu_feature2 & CPUID2_VMM))
			vmm_guest = VMM_GUEST_UNKNOWN;
	}

#if !defined(CPU_DISABLE_AVX)
	/*Check for XSAVE and AVX support and enable if available.*/
	if ((cpu_feature2 & CPUID2_AVX) && (cpu_feature2 & CPUID2_XSAVE)
	     && (cpu_feature & CPUID_SSE)) {
		load_cr4(rcr4() | CR4_XSAVE);

		/* Adjust size of savefpu in npx.h before adding to mask.*/
		xsetbv(0, CPU_XFEATURE_X87 | CPU_XFEATURE_SSE | CPU_XFEATURE_YMM, 0);
		cpu_xsave = 1;
	}
#endif

	if (cpu_vendor_id == CPU_VENDOR_AMD) {
		switch((cpu_id & 0xFF0000)) {
		case 0x100000:
		case 0x120000:
			/*
			 * Errata 721 is the cpu bug found by your's truly
			 * (Matthew Dillon).  It is a bug where a sequence
			 * of 5 or more popq's + a retq, under involved
			 * deep recursion circumstances, can cause the %rsp
			 * to not be properly updated, almost always
			 * resulting in a seg-fault soon after.
			 *
			 * Do not install the workaround when we are running
			 * in a virtual machine.
			 */
			if (vmm_guest)
				break;

			msr = rdmsr(MSR_AMD_DE_CFG);
			if ((msr & 1) == 0) {
				if (cpu == 0)
					kprintf("Errata 721 workaround "
						"installed\n");
				msr |= 1;
				wrmsr(MSR_AMD_DE_CFG, msr);
			}
			break;
		}
	}

	if ((amd_feature & AMDID_NX) != 0) {
		msr = rdmsr(MSR_EFER) | EFER_NXE;
		wrmsr(MSR_EFER, msr);
#if 0 /* JG */
		pg_nx = PG_NX;
#endif
	}
	if (cpu_vendor_id == CPU_VENDOR_CENTAUR &&
	    CPUID_TO_FAMILY(cpu_id) == 0x6 &&
	    CPUID_TO_MODEL(cpu_id) >= 0xf)
		init_via();

	TUNABLE_INT_FETCH("hw.clflush_enable", &hw_clflush_enable);
	if (cpu_feature & CPUID_CLFSH) {
		cpu_clflush_line_size = ((cpu_procinfo >> 8) & 0xff) * 8;

		if (hw_clflush_enable == 0 ||
		    ((hw_clflush_enable == -1) && vmm_guest))
			cpu_feature &= ~CPUID_CLFSH;
	}
}
