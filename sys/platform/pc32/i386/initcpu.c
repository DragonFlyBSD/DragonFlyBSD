/*
 * Copyright (c) KATO Takenori, 1997, 1998.
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
 *
 * $FreeBSD: src/sys/i386/i386/initcpu.c,v 1.19.2.9 2003/04/05 13:47:19 dwmalone Exp $
 */

#include "opt_cpu.h"

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/sysctl.h>

#include <machine/cputypes.h>
#include <machine/md_var.h>
#include <machine/specialreg.h>

void initializecpu(void);
#if defined(I586_CPU) && defined(CPU_WT_ALLOC)
void	enable_K5_wt_alloc(void);
void	enable_K6_wt_alloc(void);
void	enable_K6_2_wt_alloc(void);
#endif

#ifdef I486_CPU
static void init_bluelightning(void);
#ifdef CPU_I486_ON_386
static void init_i486_on_386(void);
#endif
#endif /* I486_CPU */

#ifdef I686_CPU
static void	init_ppro(void);
static void	init_mendocino(void);
#endif

static int	hw_instruction_sse;
SYSCTL_INT(_hw, OID_AUTO, instruction_sse, CTLFLAG_RD,
    &hw_instruction_sse, 0, "SIMD/MMX2 instructions available in CPU");

/* Must *NOT* be BSS or locore will bzero these after setting them */
int	cpu = 0;		/* Are we 386, 386sx, 486, etc? */
u_int	cpu_feature = 0;	/* Feature flags */
u_int	cpu_feature2 = 0;	/* Feature flags */
u_int	amd_feature = 0;	/* AMD feature flags */
u_int	amd_feature2 = 0;	/* AMD feature flags */
u_int	amd_pminfo = 0;		/* AMD advanced power management info */
u_int	via_feature_rng = 0;	/* VIA RNG features */
u_int	via_feature_xcrypt = 0;	/* VIA ACE features */
u_int	cpu_high = 0;		/* Highest arg to CPUID */
u_int	cpu_id = 0;		/* Stepping ID */
u_int	cpu_procinfo = 0;	/* HyperThreading Info / Brand Index / CLFUSH */
u_int	cpu_procinfo2 = 0;	/* Multicore info */
char	cpu_vendor[20] = "";	/* CPU Origin code */
u_int	cpu_vendor_id = 0;	/* CPU vendor ID */
u_int	cpu_clflush_line_size = 32;	/* Default CLFLUSH line size */

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

#ifndef CPU_DISABLE_SSE
u_int	cpu_fxsr;		/* SSE enabled */
#endif

#ifdef I486_CPU
/*
 * IBM Blue Lightning
 */
static void
init_bluelightning(void)
{
	u_long	eflags;

	eflags = read_eflags();
	cpu_disable_intr();

	load_cr0(rcr0() | CR0_CD | CR0_NW);
	invd();

#ifdef CPU_BLUELIGHTNING_FPU_OP_CACHE
	wrmsr(0x1000, 0x9c92LL);	/* FP operand can be cacheable on Cyrix FPU */
#else
	wrmsr(0x1000, 0x1c92LL);	/* Intel FPU */
#endif
	/* Enables 13MB and 0-640KB cache. */
	wrmsr(0x1001, (0xd0LL << 32) | 0x3ff);
#ifdef CPU_BLUELIGHTNING_3X
	wrmsr(0x1002, 0x04000000LL);	/* Enables triple-clock mode. */
#else
	wrmsr(0x1002, 0x03000000LL);	/* Enables double-clock mode. */
#endif

	/* Enable caching in CR0. */
	load_cr0(rcr0() & ~(CR0_CD | CR0_NW));	/* CD = 0 and NW = 0 */
	invd();
	write_eflags(eflags);
}

#ifdef CPU_I486_ON_386
/*
 * There are i486 based upgrade products for i386 machines.
 * In this case, BIOS doesn't enables CPU cache.
 */
void
init_i486_on_386(void)
{
	u_long	eflags;

	eflags = read_eflags();
	cpu_disable_intr();

	load_cr0(rcr0() & ~(CR0_CD | CR0_NW));	/* CD = 0, NW = 0 */

	write_eflags(eflags);
}
#endif

#endif /* I486_CPU */

#ifdef I686_CPU

static void
init_ppro(void)
{
}

/*
 * Initialize BBL_CR_CTL3 (Control register 3: used to configure the
 * L2 cache).
 */
void
init_mendocino(void)
{
#ifdef CPU_PPRO2CELERON
	u_long	eflags;
	u_int64_t	bbl_cr_ctl3;

	eflags = read_eflags();
	cpu_disable_intr();

	load_cr0(rcr0() | CR0_CD | CR0_NW);
	wbinvd();

	bbl_cr_ctl3 = rdmsr(0x11e);

	/* If the L2 cache is configured, do nothing. */
	if (!(bbl_cr_ctl3 & 1)) {
		bbl_cr_ctl3 = 0x134052bLL;

		/* Set L2 Cache Latency (Default: 5). */
#ifdef	CPU_CELERON_L2_LATENCY
#if CPU_L2_LATENCY > 15
#error invalid CPU_L2_LATENCY.
#endif
		bbl_cr_ctl3 |= CPU_L2_LATENCY << 1;
#else
		bbl_cr_ctl3 |= 5 << 1;
#endif
		wrmsr(0x11e, bbl_cr_ctl3);
	}

	load_cr0(rcr0() & ~(CR0_CD | CR0_NW));
	write_eflags(eflags);
#endif /* CPU_PPRO2CELERON */
}

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

#endif /* I686_CPU */

/*
 * Initialize CR4 (Control register 4) to enable SSE instructions.
 */
void
enable_sse(void)
{
#ifndef CPU_DISABLE_SSE
	if ((cpu_feature & CPUID_SSE) && (cpu_feature & CPUID_FXSR)) {
		load_cr4(rcr4() | CR4_FXSR | CR4_XMM);
		cpu_fxsr = hw_instruction_sse = 1;
	}
#endif
}

#ifdef I686_CPU
static
void
init_686_amd(void)
{
#ifdef CPU_ATHLON_SSE_HACK
	/*
	 * Sometimes the BIOS doesn't enable SSE instructions.
	 * According to AMD document 20734, the mobile
	 * Duron, the (mobile) Athlon 4 and the Athlon MP
	 * support SSE. These correspond to cpu_id 0x66X
	 * or 0x67X.
	 */
	if ((cpu_feature & CPUID_XMM) == 0 &&
	    ((cpu_id & ~0xf) == 0x660 ||
	     (cpu_id & ~0xf) == 0x670 ||
	     (cpu_id & ~0xf) == 0x680)) {
		u_int regs[4];
		wrmsr(0xC0010015, rdmsr(0xC0010015) & ~0x08000);
		do_cpuid(1, regs);
		cpu_feature = regs[3];
	}
#endif
}
#endif /* I686_CPU */

void
initializecpu(void)
{
	uint64_t msr;

	switch (cpu) {
#ifdef I486_CPU
	case CPU_BLUE:
		init_bluelightning();
		break;
#ifdef CPU_I486_ON_386
	case CPU_486:
		init_i486_on_386();
		break;
#endif
#endif /* I486_CPU */
#ifdef I686_CPU
	case CPU_686:
		if (cpu_vendor_id == CPU_VENDOR_INTEL) {
			switch (cpu_id & 0xff0) {
			case 0x610:
				init_ppro();
				break;
			case 0x660:
				init_mendocino();
				break;
			}
		} else if (cpu_vendor_id == CPU_VENDOR_AMD) {
			init_686_amd();
		} else if (cpu_vendor_id == CPU_VENDOR_CENTAUR) {
			switch (cpu_id & 0xff0) {
			case 0x690:
				if ((cpu_id & 0xf) < 3)
					break;
				/* fall through. */
			case 0x6a0:
			case 0x6d0:
			case 0x6f0:
				init_via();
				break;
			default:
				break;
			}
		}
		break;
#endif
	default:
		break;
	}
	enable_sse();

	if (cpu_feature2 & CPUID2_VMM)
		vmm_guest = 1;

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
			 * While the errata is not documented as affecting
			 * 32-bit mode, install the workaround out of an
			 * abundance of caution.
			 *
			 * Do not install the workaround when we are running
			 * in a virtual machine.
			 */
			if (vmm_guest)
				break;

			msr = rdmsr(MSR_AMD_DE_CFG);
			if ((msr & 1) == 0) {
				kprintf("Errata 721 workaround installed\n");
				msr |= 1;
				wrmsr(MSR_AMD_DE_CFG, msr);
			}
			break;
		}
	}

	TUNABLE_INT_FETCH("hw.clflush_enable", &hw_clflush_enable);
	if (cpu_feature & CPUID_CLFSH) {
		cpu_clflush_line_size = ((cpu_procinfo >> 8) & 0xff) * 8;

		if (hw_clflush_enable == 0 ||
		    ((hw_clflush_enable == -1) && vmm_guest))
			cpu_feature &= ~CPUID_CLFSH;
	}

}

#if defined(I586_CPU) && defined(CPU_WT_ALLOC)
/*
 * Enable write allocate feature of AMD processors.
 * Following two functions require the Maxmem variable being set.
 */
void
enable_K5_wt_alloc(void)
{
	u_int64_t	msr;

	/*
	 * Write allocate is supported only on models 1, 2, and 3, with
	 * a stepping of 4 or greater.
	 */
	if (((cpu_id & 0xf0) > 0) && ((cpu_id & 0x0f) > 3)) {
		cpu_disable_intr();
		msr = rdmsr(0x83);		/* HWCR */
		wrmsr(0x83, msr & !(0x10));

		/*
		 * We have to tell the chip where the top of memory is,
		 * since video cards could have frame bufferes there,
		 * memory-mapped I/O could be there, etc.
		 */
		if(Maxmem > 0)
		  msr = Maxmem / 16;
		else
		  msr = 0;
		msr |= AMD_WT_ALLOC_TME | AMD_WT_ALLOC_FRE;

		/*
		 * There is no way to know wheter 15-16M hole exists or not. 
		 * Therefore, we disable write allocate for this range.
		 */
		wrmsr(0x86, 0x0ff00f0);
		msr |= AMD_WT_ALLOC_PRE;
		wrmsr(0x85, msr);

		msr=rdmsr(0x83);
		wrmsr(0x83, msr|0x10); /* enable write allocate */

		cpu_enable_intr();
	}
}

void
enable_K6_wt_alloc(void)
{
	quad_t	size;
	u_int64_t	whcr;
	u_long	eflags;

	eflags = read_eflags();
	cpu_disable_intr();
	wbinvd();

#ifdef CPU_DISABLE_CACHE
	/*
	 * Certain K6-2 box becomes unstable when write allocation is
	 * enabled.
	 */
	/*
	 * The AMD-K6 processer provides the 64-bit Test Register 12(TR12),
	 * but only the Cache Inhibit(CI) (bit 3 of TR12) is suppported.
	 * All other bits in TR12 have no effect on the processer's operation.
	 * The I/O Trap Restart function (bit 9 of TR12) is always enabled
	 * on the AMD-K6.
	 */
	wrmsr(0x0000000e, (u_int64_t)0x0008);
#endif
	/* Don't assume that memory size is aligned with 4M. */
	if (Maxmem > 0)
	  size = ((Maxmem >> 8) + 3) >> 2;
	else
	  size = 0;

	/* Limit is 508M bytes. */
	if (size > 0x7f)
		size = 0x7f;
	whcr = (rdmsr(0xc0000082) & ~(0x7fLL << 1)) | (size << 1);

#if defined(NO_MEMORY_HOLE)
	if (whcr & (0x7fLL << 1))
		whcr |=  0x0001LL;
#else
	/*
	 * There is no way to know wheter 15-16M hole exists or not. 
	 * Therefore, we disable write allocate for this range.
	 */
	whcr &= ~0x0001LL;
#endif
	wrmsr(0x0c0000082, whcr);

	write_eflags(eflags);
}

void
enable_K6_2_wt_alloc(void)
{
	quad_t	size;
	u_int64_t	whcr;
	u_long	eflags;

	eflags = read_eflags();
	cpu_disable_intr();
	wbinvd();

#ifdef CPU_DISABLE_CACHE
	/*
	 * Certain K6-2 box becomes unstable when write allocation is
	 * enabled.
	 */
	/*
	 * The AMD-K6 processer provides the 64-bit Test Register 12(TR12),
	 * but only the Cache Inhibit(CI) (bit 3 of TR12) is suppported.
	 * All other bits in TR12 have no effect on the processer's operation.
	 * The I/O Trap Restart function (bit 9 of TR12) is always enabled
	 * on the AMD-K6.
	 */
	wrmsr(0x0000000e, (u_int64_t)0x0008);
#endif
	/* Don't assume that memory size is aligned with 4M. */
	if (Maxmem > 0)
	  size = ((Maxmem >> 8) + 3) >> 2;
	else
	  size = 0;

	/* Limit is 4092M bytes. */
	if (size > 0x3fff)
		size = 0x3ff;
	whcr = (rdmsr(0xc0000082) & ~(0x3ffLL << 22)) | (size << 22);

#if defined(NO_MEMORY_HOLE)
	if (whcr & (0x3ffLL << 22))
		whcr |=  1LL << 16;
#else
	/*
	 * There is no way to know wheter 15-16M hole exists or not. 
	 * Therefore, we disable write allocate for this range.
	 */
	whcr &= ~(1LL << 16);
#endif
	wrmsr(0x0c0000082, whcr);

	write_eflags(eflags);
}
#endif /* I585_CPU && CPU_WT_ALLOC */

#include "opt_ddb.h"
