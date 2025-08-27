/*-
 * Copyright (c) 1992 Terrence R. Lambert.
 * Copyright (c) 1982, 1987, 1990 The Regents of the University of California.
 * Copyright (c) 1997 KATO Takenori.
 * Copyright (c) 2008 The DragonFly Project.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * William Jolitz.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * from: Id: machdep.c,v 1.193 1996/06/18 01:22:04 bde Exp
 */

#include "opt_cpu.h"

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/eventhandler.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>
#include <sys/power.h>

#include <machine/asmacros.h>
#include <machine/clock.h>
#include <machine/cputypes.h>
#include <machine/frame.h>
#include <machine/segments.h>
#include <machine/specialreg.h>
#include <machine/md_var.h>
#include <machine/npx.h>

/* XXX - should be in header file: */
void printcpuinfo(void);
void identify_cpu(void);
void earlysetcpuclass(void);
void panicifcpuunsupported(void);

static u_int find_cpu_vendor_id(void);
static void print_AMD_info(void);
static void print_AMD_assoc(int i);
static void print_via_padlock_info(void);

int	cpu_class;
char machine[] = "x86_64";
SYSCTL_STRING(_hw, HW_MACHINE, machine, CTLFLAG_RD, 
    machine, 0, "Machine class");

static char cpu_model[128];
SYSCTL_STRING(_hw, HW_MODEL, model, CTLFLAG_RD, 
    cpu_model, 0, "Machine model");

static int hw_clockrate;
SYSCTL_INT(_hw, OID_AUTO, clockrate, CTLFLAG_RD, 
    &hw_clockrate, 0, "CPU instruction clock rate");

static char cpu_brand[48];

static struct {
	char	*cpu_name;
	int	cpu_class;
} x86_64_cpus[] = {
	{ "Clawhammer",		CPUCLASS_K8 },		/* CPU_CLAWHAMMER */
	{ "Sledgehammer",	CPUCLASS_K8 },		/* CPU_SLEDGEHAMMER */
};

static struct {
	char	*vendor;
	u_int	vendor_id;
} cpu_vendors[] = {
	{ INTEL_VENDOR_ID,	CPU_VENDOR_INTEL },	/* GenuineIntel */
	{ AMD_VENDOR_ID,	CPU_VENDOR_AMD },	/* AuthenticAMD */
	{ CENTAUR_VENDOR_ID,	CPU_VENDOR_CENTAUR },	/* CentaurHauls */
};

#ifdef foo
static int cpu_cores;
static int cpu_logical;
#endif

void
printcpuinfo(void)
{
	u_int regs[4], i;
	char *brand;

	cpu_class = x86_64_cpus[cpu_type].cpu_class;
	kprintf("CPU: ");
	strncpy(cpu_model, x86_64_cpus[cpu_type].cpu_name, sizeof (cpu_model));

	/* Check for extended CPUID information and a processor name. */
	if (cpu_exthigh >= 0x80000004) {
		brand = cpu_brand;
		for (i = 0x80000002; i < 0x80000005; i++) {
			do_cpuid(i, regs);
			memcpy(brand, regs, sizeof(regs));
			brand += sizeof(regs);
		}
	}

	switch (cpu_vendor_id) {
	case CPU_VENDOR_INTEL:
		/* Please make up your mind folks! */
		strcat(cpu_model, "EM64T");
		break;
	case CPU_VENDOR_AMD:
		/*
		 * Values taken from AMD Processor Recognition
		 * http://www.amd.com/K6/k6docs/pdf/20734g.pdf
		 * (also describes ``Features'' encodings.
		 */
		strcpy(cpu_model, "AMD ");
		if ((cpu_id & 0xf00) == 0xf00)
			strcat(cpu_model, "AMD64 Processor");
		else
			strcat(cpu_model, "Unknown");
		break;
	case CPU_VENDOR_CENTAUR:
		strcpy(cpu_model, "VIA ");
		if ((cpu_id & 0xff0) == 0x6f0)
			strcat(cpu_model, "Nano Processor");
		else
			strcat(cpu_model, "Unknown");
		break;
	default:
		strcat(cpu_model, "Unknown");
		break;
	}

	/*
	 * Replace cpu_model with cpu_brand minus leading spaces if
	 * we have one.
	 */
	brand = cpu_brand;
	while (*brand == ' ')
		++brand;
	if (*brand != '\0')
		strcpy(cpu_model, brand);

	kprintf("%s (", cpu_model);
	switch(cpu_class) {
	case CPUCLASS_K8:
		hw_clockrate = (tsc_frequency + 5000) / 1000000;
		kprintf("%jd.%02d-MHz ",
		       (intmax_t)(tsc_frequency + 4999) / 1000000,
		       (u_int)((tsc_frequency + 4999) / 10000) % 100);
		kprintf("K8");
		break;
	default:
		kprintf("Unknown");	/* will panic below... */
	}
	kprintf("-class CPU)\n");
	if (*cpu_vendor)
		kprintf("  Origin = \"%s\"", cpu_vendor);
	if (cpu_id)
		kprintf("  Id = 0x%x", cpu_id);

	if (cpu_vendor_id == CPU_VENDOR_INTEL ||
	    cpu_vendor_id == CPU_VENDOR_AMD ||
	    cpu_vendor_id == CPU_VENDOR_CENTAUR) {
		kprintf("  Stepping = %u", cpu_id & 0xf);
		if (cpu_high > 0) {
#if 0
			u_int cmp = 1, htt = 1;
#endif

			/*
			 * Here we should probably set up flags indicating
			 * whether or not various features are available.
			 * The interesting ones are probably VME, PSE, PAE,
			 * and PGE.  The code already assumes without bothering
			 * to check that all CPUs >= Pentium have a TSC and
			 * MSRs.
			 */
			kprintf("\n  Features=0x%pb%i",
			"\020"
			"\001FPU"	/* Integral FPU */
			"\002VME"	/* Extended VM86 mode support */
			"\003DE"	/* Debugging Extensions (CR4.DE) */
			"\004PSE"	/* 4MByte page tables */
			"\005TSC"	/* Timestamp counter */
			"\006MSR"	/* Machine specific registers */
			"\007PAE"	/* Physical address extension */
			"\010MCE"	/* Machine Check support */
			"\011CX8"	/* CMPEXCH8 instruction */
			"\012APIC"	/* SMP local APIC */
			"\013oldMTRR"	/* Previous implementation of MTRR */
			"\014SEP"	/* Fast System Call */
			"\015MTRR"	/* Memory Type Range Registers */
			"\016PGE"	/* PG_G (global bit) support */
			"\017MCA"	/* Machine Check Architecture */
			"\020CMOV"	/* CMOV instruction */
			"\021PAT"	/* Page attributes table */
			"\022PSE36"	/* 36 bit address space support */
			"\023PN"	/* Processor Serial number */
			"\024CLFLUSH"	/* Has the CLFLUSH instruction */
			"\025<b20>"
			"\026DTS"	/* Debug Trace Store */
			"\027ACPI"	/* ACPI support */
			"\030MMX"	/* MMX instructions */
			"\031FXSR"	/* FXSAVE/FXRSTOR */
			"\032SSE"	/* Streaming SIMD Extensions */
			"\033SSE2"	/* Streaming SIMD Extensions #2 */
			"\034SS"	/* Self snoop */
			"\035HTT"	/* Hyperthreading (see EBX bit 16-23) */
			"\036TM"	/* Thermal Monitor clock slowdown */
			"\037IA64"	/* CPU can execute IA64 instructions */
			"\040PBE"	/* Pending Break Enable */
			, cpu_feature);

			if (cpu_feature2 != 0) {
				kprintf("\n  Features2=0x%pb%i",
				"\020"
				"\001SSE3"	/* SSE3 */
				"\002PCLMULQDQ"	/* Carry-Less Mul Quadword */
				"\003DTES64"	/* 64-bit Debug Trace */
				"\004MON"	/* MONITOR/MWAIT Instructions */
				"\005DS_CPL"	/* CPL Qualified Debug Store */
				"\006VMX"	/* Virtual Machine Extensions */
				"\007SMX"	/* Safer Mode Extensions */
				"\010EST"	/* Enhanced SpeedStep */
				"\011TM2"	/* Thermal Monitor 2 */
				"\012SSSE3"	/* SSSE3 */
				"\013CNXT-ID"	/* L1 context ID available */
				"\014SDBG"	/* IA-32 silicon debug */
				"\015FMA"	/* Fused Multiply Add */
				"\016CX16"	/* CMPXCHG16B Instruction */
				"\017xTPR"	/* Send Task Priority Messages */
				"\020PDCM"	/* Perf/Debug Capability MSR */
				"\021<b16>"
				"\022PCID"	/* Process-context Identifiers */
				"\023DCA"	/* Direct Cache Access */
				"\024SSE4.1"	/* SSE 4.1 */
				"\025SSE4.2"	/* SSE 4.2 */
				"\026x2APIC"	/* xAPIC Extensions */
				"\027MOVBE"	/* MOVBE Instruction */
				"\030POPCNT"	/* POPCNT Instruction */
				"\031TSCDLT"	/* TSC-Deadline Timer */
				"\032AESNI"	/* AES Crypto */
				"\033XSAVE"	/* XSAVE/XRSTOR States */
				"\034OSXSAVE"	/* OS-Enabled State Management */
				"\035AVX"	/* Advanced Vector Extensions */
				"\036F16C"	/* Half-precision conversions */
				"\037RDRND"	/* RDRAND RNG function */
				"\040VMM"	/*  Running on a hypervisor */
				, cpu_feature2);
			}

			if (cpu_ia32_arch_caps != 0) {
				kprintf("\n  IA32_ARCH_CAPS=0x%pb%i",
				       "\020"
				       "\001RDCL_NO"
				       "\002IBRS_ALL"
				       "\003RSBA"
				       "\004SKIP_L1DFL_VME"
				       "\005SSB_NO"
				       "\006MDS_NO"
				       "\010TSX_CTRL"
				       "\011TAA_NO",
				       (u_int)cpu_ia32_arch_caps
				);
			}

			/*
			 * AMD64 Architecture Programmer's Manual Volume 3:
			 * General-Purpose and System Instructions
			 * http://www.amd.com/us-en/assets/content_type/white_papers_and_tech_docs/24594.pdf
			 *
			 * IA-32 Intel Architecture Software Developer's Manual,
			 * Volume 2A: Instruction Set Reference, A-M
			 * ftp://download.intel.com/design/Pentium4/manuals/25366617.pdf
			 */
			if (amd_feature != 0) {
				kprintf("\n  AMD Features=0x%pb%i",
				"\020"		/* in hex */
				"\001<s0>"	/* Same */
				"\002<s1>"	/* Same */
				"\003<s2>"	/* Same */
				"\004<s3>"	/* Same */
				"\005<s4>"	/* Same */
				"\006<s5>"	/* Same */
				"\007<s6>"	/* Same */
				"\010<s7>"	/* Same */
				"\011<s8>"	/* Same */
				"\012<s9>"	/* Same */
				"\013<b10>"	/* Undefined */
				"\014SYSCALL"	/* Have SYSCALL/SYSRET */
				"\015<s12>"	/* Same */
				"\016<s13>"	/* Same */
				"\017<s14>"	/* Same */
				"\020<s15>"	/* Same */
				"\021<s16>"	/* Same */
				"\022<s17>"	/* Same */
				"\023<b18>"	/* Reserved, unknown */
				"\024MP"	/* Multiprocessor Capable */
				"\025NX"	/* Has EFER.NXE, NX */
				"\026<b21>"	/* Undefined */
				"\027MMX+"	/* AMD MMX Extensions */
				"\030<s23>"	/* Same */
				"\031<s24>"	/* Same */
				"\032FFXSR"	/* Fast FXSAVE/FXRSTOR */
				"\033Page1GB"	/* 1-GB large page support */
				"\034RDTSCP"	/* RDTSCP */
				"\035<b28>"	/* Undefined */
				"\036LM"	/* 64 bit long mode */
				"\0373DNow!+"	/* AMD 3DNow! Extensions */
				"\0403DNow!"	/* AMD 3DNow! */
				, amd_feature);
			}

			if (amd_feature2 != 0) {
				kprintf("\n  AMD Features2=0x%pb%i",
				"\020"
				"\001LAHF"	/* LAHF/SAHF in long mode */
				"\002CMP"	/* CMP legacy */
				"\003SVM"	/* Secure Virtual Mode */
				"\004ExtAPIC"	/* Extended APIC register */
				"\005CR8"	/* CR8 in legacy mode */
				"\006ABM"	/* LZCNT instruction */
				"\007SSE4A"	/* SSE4A */
				"\010MAS"	/* Misaligned SSE mode */
				"\011Prefetch"	/* 3DNow! Prefetch/PrefetchW */
				"\012OSVW"	/* OS visible workaround */
				"\013IBS"	/* Instruction based sampling */
				"\014XOP"	/* XOP extended instructions */
				"\015SKINIT"	/* SKINIT/STGI */
				"\016WDT"	/* Watchdog timer */
				"\017<b14>"
				"\020LWP"	/* Lightweight Profiling */
				"\021FMA4"	/* 4-operand FMA instructions */
				"\022TCE"       /* Translation Cache Extension */
				"\023<b18>"
				"\024NodeId"	/* NodeId MSR support */
				"\025<b20>"
				"\026TBM"	/* Trailing Bit Manipulation */
				"\027Topology"	/* Topology Extensions */
				"\030PCX_CORE"  /* Core Performance Counter */
				"\031PCX_NB"    /* NB Performance Counter */
				"\032SPM"	/* Streaming Perf Monitor */
				"\033DBE"	/* Data Breakpoint Extension */
				"\034PTSC"	/* Performance TSC */
				"\035PCX_L2I"	/* L2I Performance Counter */
		       	        "\036MWAITX"	/* MONITORX/MWAITX instructions */
				"\037ADMSKX"
				"\040<b31>"
				, amd_feature2);
			}

			if (cpu_stdext_feature != 0) {
				kprintf("\n  Structured Extended "
					"Features=0x%pb%i",
				        "\020"
				        /*RDFSBASE/RDGSBASE/WRFSBASE/WRGSBASE*/
				        "\001GSFSBASE"
				        "\002TSCADJ"
				        /* Bit Manipulation Instructions */
				        "\004BMI1"
				        /* Hardware Lock Elision */
				        "\005HLE"
				        /* Advanced Vector Instructions 2 */
				        "\006AVX2"
				        /* Supervisor Mode Execution Prot. */
				        "\010SMEP"
				        /* Bit Manipulation Instructions */
				        "\011BMI2"
				        "\012ENHMOVSB"
				       /* Invalidate Processor Context ID */
				        "\013INVPCID"
				        /* Restricted Transactional Memory */
				        "\014RTM"
				        "\015PQM"
				        "\016NFPUSG"
				        /* Intel Memory Protection Extensions */
				        "\017MPX"
				        "\020PQE"
				        /* AVX512 Foundation */
				        "\021AVX512F"
				        "\022AVX512DQ"
				        /* Enhanced NRBG */
				        "\023RDSEED"
				        /* ADCX + ADOX */
				        "\024ADX"
				        /* Supervisor Mode Access Prevention */
				        "\025SMAP"
				        "\026AVX512IFMA"
				        /* Formerly PCOMMIT */
				        "\027<b22>"
				        "\030CLFLUSHOPT"
				        "\031CLWB"
				        "\032PROCTRACE"
				        "\033AVX512PF"
				        "\034AVX512ER"
				        "\035AVX512CD"
				        "\036SHA"
				        "\037AVX512BW"
				        "\040AVX512VL",
				        cpu_stdext_feature
				);
			}

			if (cpu_stdext_feature2 != 0) {
				kprintf("\n  Structured Extended "
					"Features2=0x%pb%i",
				        "\020"
				        "\001PREFETCHWT1"
				        "\002AVX512VBMI"
				        "\003UMIP"
				        "\004PKU"
				        "\005OSPKE"
				        "\006WAITPKG"
				        "\007AVX512VBMI2"
				        "\011GFNI"
				        "\012VAES"
				        "\013VPCLMULQDQ"
				        "\014AVX512VNNI"
				        "\015AVX512BITALG"
				        "\016TME"
				        "\017AVX512VPOPCNTDQ"
				        "\021LA57"
				        "\027RDPID"
				        "\032CLDEMOTE"
				        "\034MOVDIRI"
				        "\035MOVDIR64B"
				        "\036ENQCMD"
				        "\037SGXLC",
				        cpu_stdext_feature2
			       );
			}

			if (cpu_stdext_feature3 != 0) {
				kprintf("\n  Structured Extended "
					"Features3=0x%pb%i",
				        "\020"
				        "\003AVX512_4VNNIW"
				        "\004AVX512_4FMAPS"
				        "\005FSRM"
				        "\011AVX512VP2INTERSECT"
				        "\012MCUOPT"
				        "\013MD_CLEAR"
				        "\016TSXFA"
				        "\023PCONFIG"
				        "\025IBT"
				        "\033IBPB"
				        "\034STIBP"
				        "\035L1DFL"
				        "\036ARCH_CAP"
				        "\037CORE_CAP"
				        "\040SSBD",
				        cpu_stdext_feature3
			       );
			}

			if (cpu_thermal_feature != 0) {
				kprintf("\n  Thermal and PM Features=0x%pb%i",
				    "\020"
				    /* Digital temperature sensor */
				    "\001SENSOR"
				    /* Turbo boost */
				    "\002TURBO"
				    /* APIC-Timer-always-running */
				    "\003ARAT"
				    /* Power limit notification controls */
				    "\005PLN"
				    /* Clock modulation duty cycle extension */
				    "\006ECMD"
				    /* Package thermal management */
				    "\007PTM"
				    /* Hardware P-states */
				    "\010HWP"
				    , cpu_thermal_feature);
			}

			if (cpu_mwait_feature != 0) {
				kprintf("\n  MONITOR/MWAIT Features=0x%pb%i",
				    "\020"
				    /* Enumeration of Monitor-Mwait extension */
				    "\001CST"
				    /*  interrupts as break-event for MWAIT */
				    "\002INTBRK"
				    , cpu_mwait_feature);
			}

			if (cpu_vendor_id == CPU_VENDOR_CENTAUR)
				print_via_padlock_info();
			/*
			 * INVALID CPU TOPOLOGY INFORMATION PRINT
			 * DEPRECATED - CPU_TOPOLOGY_DETECTION moved to 
			 * - sys/platform/pc64/x86_64/mp_machdep.c
			 * - sys/kern/subr_cpu_topology
			 */

#if 0
			if ((cpu_feature & CPUID_HTT) &&
			    cpu_vendor_id == CPU_VENDOR_AMD)
				cpu_feature &= ~CPUID_HTT;
#endif

			/*
			 * If this CPU supports HTT or CMP then mention the
			 * number of physical/logical cores it contains.
			 */
#if 0
			if (cpu_feature & CPUID_HTT)
				htt = (cpu_procinfo & CPUID_HTT_CORES) >> 16;
			if (cpu_vendor_id == CPU_VENDOR_AMD &&
			    (amd_feature2 & AMDID2_CMP))
				cmp = (cpu_procinfo2 & AMDID_CMP_CORES) + 1;
			else if (cpu_vendor_id == CPU_VENDOR_INTEL &&
			    (cpu_high >= 4)) {
				cpuid_count(4, 0, regs);
				if ((regs[0] & 0x1f) != 0)
					cmp = ((regs[0] >> 26) & 0x3f) + 1;
			}
#endif
#ifdef foo
			/*
			 * XXX For Intel CPUs, this is max number of cores per
			 * package, not the actual cores per package.
			 */
#if 0
			cpu_cores = cmp;
			cpu_logical = htt / cmp;

			if (cpu_cores > 1)
				kprintf("\n  Cores per package: %d", cpu_cores);
			if (cpu_logical > 1) {
				kprintf("\n  Logical CPUs per core: %d",
				    cpu_logical);
			}
#endif
#endif
		}
	}
	/* Avoid ugly blank lines: only print newline when we have to. */
	if (*cpu_vendor || cpu_id)
		kprintf("\n");

	if (cpu_stdext_feature & (CPUID_STDEXT_SMAP | CPUID_STDEXT_SMEP)) {
		kprintf("CPU Special Features Installed:");
		if (cpu_stdext_feature & CPUID_STDEXT_SMAP)
			kprintf(" SMAP");
		if (cpu_stdext_feature & CPUID_STDEXT_SMEP)
			kprintf(" SMEP");
		kprintf("\n");
	}

	if (bootverbose) {
		if (cpu_vendor_id == CPU_VENDOR_AMD)
			print_AMD_info();
	}

	if (cpu_feature2 & CPUID2_VMM) {
		/*
		 * The vmm_vendor may be empty.  For example, VirtualBox
		 * doesn't advertise the vendor name when using the
		 * "Minimal" paravirtualization interface.
		 */
		if (*vmm_vendor != '\0')
			kprintf("VMM/Hypervisor: Origin=\"%s\"\n", vmm_vendor);
		else
			kprintf("VMM/Hypervisor: Origin=(unknown)\n");
	}
}

void
panicifcpuunsupported(void)
{

#ifndef HAMMER_CPU
#error "You need to specify a cpu type"
#endif
	/*
	 * Now that we have told the user what they have,
	 * let them know if that machine type isn't configured.
	 */
	switch (cpu_class) {
	case CPUCLASS_X86:
#ifndef HAMMER_CPU
	case CPUCLASS_K8:
#endif
		panic("CPU class not configured");
	default:
		break;
	}
}


#if 0 /* JG */
/* Update TSC freq with the value indicated by the caller. */
static void
tsc_freq_changed(void *arg, const struct cf_level *level, int status)
{
	/* If there was an error during the transition, don't do anything. */
	if (status != 0)
		return;

	/* Total setting for this level gives the new frequency in MHz. */
	hw_clockrate = level->total_set.freq;
}

EVENTHANDLER_DEFINE(cpufreq_post_change, tsc_freq_changed, NULL,
    EVENTHANDLER_PRI_ANY);
#endif

/*
 * Final stage of CPU identification.
 */
void
identify_cpu(void)
{
	u_int regs[4];
	u_int cpu_stdext_disable;

	do_cpuid(0, regs);
	cpu_high = regs[0];
	((u_int *)&cpu_vendor)[0] = regs[1];
	((u_int *)&cpu_vendor)[1] = regs[3];
	((u_int *)&cpu_vendor)[2] = regs[2];
	cpu_vendor[12] = '\0';
	cpu_vendor_id = find_cpu_vendor_id();

	do_cpuid(1, regs);
	cpu_id = regs[0];
	cpu_procinfo = regs[1];
	cpu_feature = regs[3];
	cpu_feature2 = regs[2];

	if (cpu_high >= 5) {
		do_cpuid(5, regs);
		cpu_mwait_feature = regs[2];
		if (cpu_mwait_feature & CPUID_MWAIT_EXT) {
			cpu_mwait_extemu = regs[3];
			/* At least one C1 */
			if (CPUID_MWAIT_CX_SUBCNT(cpu_mwait_extemu, 1) == 0) {
				/* No C1 at all, no MWAIT EXT then */
				cpu_mwait_feature &= ~CPUID_MWAIT_EXT;
				cpu_mwait_extemu = 0;
			}
		}
	}
	if (cpu_high >= 6) {
		do_cpuid(6, regs);
		cpu_thermal_feature = regs[0];
	}
	if (cpu_high >= 7) {
		cpuid_count(7, 0, regs);
		cpu_stdext_feature = regs[1];

		/*
		 * Some hypervisors fail to filter out unsupported
		 * extended features.  For now, disable the
		 * extensions, activation of which requires setting a
		 * bit in CR4, and which VM monitors do not support.
		 */
		if (cpu_feature2 & CPUID2_VMM) {
			cpu_stdext_disable = CPUID_STDEXT_FSGSBASE |
					     CPUID_STDEXT_SMEP;
		} else {
			cpu_stdext_disable = 0;
		}
		TUNABLE_INT_FETCH("hw.cpu_stdext_disable", &cpu_stdext_disable);

		/*
		 * Some hypervisors fail to implement
		 * MSR_IA32_ARCH_CAPABILITIES, catch any problems.
		 */
		cpu_stdext_feature &= ~cpu_stdext_disable;
		cpu_stdext_feature2 = regs[2];
		cpu_stdext_feature3 = regs[3];
		if (cpu_stdext_feature3 & CPUID_STDEXT3_ARCH_CAP) {
			if (rdmsr_safe(MSR_IA32_ARCH_CAPABILITIES,
				       &cpu_ia32_arch_caps))
			{
				kprintf("Warning: MSR_IA32_ARCH_CAPABILITIES "
					"cannot be accessed\n");
			}
		}
	}

	if (cpu_vendor_id == CPU_VENDOR_INTEL ||
	    cpu_vendor_id == CPU_VENDOR_AMD ||
	    cpu_vendor_id == CPU_VENDOR_CENTAUR) {
		do_cpuid(0x80000000, regs);
		cpu_exthigh = regs[0];
	}
	if (cpu_exthigh >= 0x80000001) {
		do_cpuid(0x80000001, regs);
		amd_feature = regs[3] & ~(cpu_feature & 0x0183f3ff);
		amd_feature2 = regs[2];
	}
	if (cpu_exthigh >= 0x80000008) {
		do_cpuid(0x80000008, regs);
		cpu_procinfo2 = regs[2];
	}

	/* XXX */
	cpu_type = CPU_CLAWHAMMER;

	if (cpu_feature & CPUID_SSE2)
		cpu_mi_feature |= CPU_MI_BZERONT;

	if (cpu_feature2 & CPUID2_MON)
		cpu_mi_feature |= CPU_MI_MONITOR;

	/* 
	 * We do assume that all CPUs have the same
	 * SSE/FXSR features
	 */
	if ((cpu_feature & CPUID_SSE) && (cpu_feature & CPUID_FXSR))
		npxprobemask();
}

static u_int
find_cpu_vendor_id(void)
{
	int	i;

	for (i = 0; i < NELEM(cpu_vendors); i++)
		if (strcmp(cpu_vendor, cpu_vendors[i].vendor) == 0)
			return (cpu_vendors[i].vendor_id);
	return (0);
}

static void
print_AMD_assoc(int i)
{
	if (i == 255)
		kprintf(", fully associative\n");
	else
		kprintf(", %d-way associative\n", i);
}

static void
print_AMD_l2_assoc(int i)
{
	switch (i & 0x0f) {
	case 0: kprintf(", disabled/not present\n"); break;
	case 1: kprintf(", direct mapped\n"); break;
	case 2: kprintf(", 2-way associative\n"); break;
	case 4: kprintf(", 4-way associative\n"); break;
	case 6: kprintf(", 8-way associative\n"); break;
	case 8: kprintf(", 16-way associative\n"); break;
	case 15: kprintf(", fully associative\n"); break;
	default: kprintf(", reserved configuration\n"); break;
	}
}

static void
print_AMD_info(void)
{
	u_int regs[4];

	if (cpu_exthigh < 0x80000005)
		return;

	do_cpuid(0x80000005, regs);
	kprintf("L1 2MB data TLB: %d entries", (regs[0] >> 16) & 0xff);
	print_AMD_assoc(regs[0] >> 24);

	kprintf("L1 2MB instruction TLB: %d entries", regs[0] & 0xff);
	print_AMD_assoc((regs[0] >> 8) & 0xff);

	kprintf("L1 4KB data TLB: %d entries", (regs[1] >> 16) & 0xff);
	print_AMD_assoc(regs[1] >> 24);

	kprintf("L1 4KB instruction TLB: %d entries", regs[1] & 0xff);
	print_AMD_assoc((regs[1] >> 8) & 0xff);

	kprintf("L1 data cache: %d kbytes", regs[2] >> 24);
	kprintf(", %d bytes/line", regs[2] & 0xff);
	kprintf(", %d lines/tag", (regs[2] >> 8) & 0xff);
	print_AMD_assoc((regs[2] >> 16) & 0xff);

	kprintf("L1 instruction cache: %d kbytes", regs[3] >> 24);
	kprintf(", %d bytes/line", regs[3] & 0xff);
	kprintf(", %d lines/tag", (regs[3] >> 8) & 0xff);
	print_AMD_assoc((regs[3] >> 16) & 0xff);

	if (cpu_exthigh >= 0x80000006) {
		do_cpuid(0x80000006, regs);
		if ((regs[0] >> 16) != 0) {
			kprintf("L2 2MB data TLB: %d entries",
			    (regs[0] >> 16) & 0xfff);
			print_AMD_l2_assoc(regs[0] >> 28);
			kprintf("L2 2MB instruction TLB: %d entries",
			    regs[0] & 0xfff);
			print_AMD_l2_assoc((regs[0] >> 28) & 0xf);
		} else {
			kprintf("L2 2MB unified TLB: %d entries",
			    regs[0] & 0xfff);
			print_AMD_l2_assoc((regs[0] >> 28) & 0xf);
		}
		if ((regs[1] >> 16) != 0) {
			kprintf("L2 4KB data TLB: %d entries",
			    (regs[1] >> 16) & 0xfff);
			print_AMD_l2_assoc(regs[1] >> 28);

			kprintf("L2 4KB instruction TLB: %d entries",
			    (regs[1] >> 16) & 0xfff);
			print_AMD_l2_assoc((regs[1] >> 28) & 0xf);
		} else {
			kprintf("L2 4KB unified TLB: %d entries",
			    (regs[1] >> 16) & 0xfff);
			print_AMD_l2_assoc((regs[1] >> 28) & 0xf);
		}
		kprintf("L2 unified cache: %d kbytes", regs[2] >> 16);
		kprintf(", %d bytes/line", regs[2] & 0xff);
		kprintf(", %d lines/tag", (regs[2] >> 8) & 0x0f);
		print_AMD_l2_assoc((regs[2] >> 12) & 0x0f);	
	}
}

static void
print_via_padlock_info(void)
{
	u_int regs[4];

	/* Check for supported models. */
	switch (cpu_id & 0xff0) {
	case 0x690:
		if ((cpu_id & 0xf) < 3)
			return;
	case 0x6a0:
	case 0x6d0:
	case 0x6f0:
		break;
	default:
		return;
	}

	do_cpuid(0xc0000000, regs);
	if (regs[0] >= 0xc0000001)
		do_cpuid(0xc0000001, regs);
	else
		return;

	kprintf("\n  VIA Padlock Features=0x%pb%i",
	"\020"
	"\003RNG"		/* RNG */
	"\007AES"		/* ACE */
	"\011AES-CTR"		/* ACE2 */
	"\013SHA1,SHA256"	/* PHE */
	"\015RSA"		/* PMM */
	, regs[3]);
}
