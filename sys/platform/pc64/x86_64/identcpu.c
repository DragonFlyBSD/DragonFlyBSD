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

static u_int find_cpu_vendor_id(void);
static void print_AMD_info(void);
static void print_INTEL_info(void);
static void print_via_padlock_info(void);
static void print_xsave_info(void);
static void print_svm_info(void);
static void print_vmx_info(void);

int	cpu_type;
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

#if 0
static int cpu_cores;
static int cpu_logical;
#endif

void
printcpuinfo(void)
{
	u_int regs[4], i;
	char *brand;

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
		kprintf("  Origin=\"%s\"", cpu_vendor);
	if (cpu_id)
		kprintf("  Id=0x%x", cpu_id);

	if (cpu_vendor_id == CPU_VENDOR_INTEL ||
	    cpu_vendor_id == CPU_VENDOR_AMD ||
	    cpu_vendor_id == CPU_VENDOR_CENTAUR) {
		kprintf("  Family=0x%x", CPUID_TO_FAMILY(cpu_id));
		kprintf("  Model=0x%x", CPUID_TO_MODEL(cpu_id));
		kprintf("  Stepping=%u", cpu_id & CPUID_STEPPING);
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
				"\037RDRAND"	/* RDRAND Instruction */
				"\040VMM"	/* Running on a hypervisor */
				, cpu_feature2);
			}

			if ((cpu_feature2 & CPUID2_XSAVE) != 0)
				print_xsave_info();

			if (cpu_ia32_arch_caps != 0) {
				kprintf("\n  IA32_ARCH_CAPS=0x%pb%i",
				       "\020"
				       "\001RDCL_NO"
				       "\002IBRS_ALL"
				       "\003RSBA"
				       "\004SKIP_L1DFL_VME"
				       "\005SSB_NO"
				       "\006MDS_NO"
				       "\007IF_PSCHANGE_MC_NO"
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
				"\022TCE"	/* Translation Cache Extension */
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
				"\037ADMSKX"	/* Address Mask Extension */
				"\040<b31>"
				, amd_feature2);
			}

			if (cpu_stdext_feature != 0) {
				kprintf("\n  Structured Extended Features=0x%pb%i",
				"\020"
				"\001GSFSBASE"	/* RDFSBASE/RDGSBASE/WRFSBASE/WRGSBASE */
				"\002TSCADJ"	/* IA32_TSC_ADJUST MSR */
				"\004BMI1"	/* Bit Manipulation Instructions */
				"\005HLE"	/* Hardware Lock Elision */
				"\006AVX2"	/* Advanced Vector Instructions 2 */
				"\007FDPEXC"	/* FDP_EXCPTN_ONLY */
				"\010SMEP"	/* Supervisor Mode Execution Prot. */
				"\011BMI2"	/* Bit Manipulation Instructions 2 */
				"\012ENHMOVSB"	/* Enhanced REP MOVSB/STOSB */
				"\013INVPCID"	/* Invalidate Processor Context ID */
				"\014RTM"	/* Restricted Transactional Memory */
				"\015PQM"	/* Platform QoS Monitoring */
				"\016NFPUSG"	/* Deprecate FPU CS/DS values */
				"\017MPX"	/* Intel Memory Protection Extensions */
				"\020PQE"	/* Platform QoS Enforcement */
				"\021AVX512F"	/* AVX512 Foundation */
				"\022AVX512DQ"	/* AVX512 Double/Quadword */
				"\023RDSEED"	/* Enhanced NRBG */
				"\024ADX"	/* ADCX + ADOX */
				"\025SMAP"	/* Supervisor Mode Access Prevention */
				"\026AVX512IFMA" /* AVX512 Integer Fused Multiply Add */
				"\027<b22>"	/* Formerly PCOMMIT */
				"\030CLFLUSHOPT" /* Cache Line FLUSH OPTimized */
				"\031CLWB"	/* Cache Line Write Back */
				"\032PROCTRACE"	/* Processor Trace */
				"\033AVX512PF"	/* AVX512 Prefetch */
				"\034AVX512ER"	/* AVX512 Exponential and Reciprocal */
				"\035AVX512CD"	/* AVX512 Conflict Detection */
				"\036SHA"	/* SHA Extension */
				"\037AVX512BW"	/* AVX512 Byte and Word */
				"\040AVX512VL"	/* AVX512 Vector Length */
				, cpu_stdext_feature);
			}

			if (cpu_stdext_feature2 != 0) {
				kprintf("\n  Structured Extended Features2=0x%pb%i",
				"\020"
				"\001PREFETCHWT1"	/* PREFETCHWT1 instruction */
				"\002AVX512VBMI"	/* AVX-512 Vector Byte Manipulation */
				"\003UMIP"		/* User-Mode Instruction prevention */
				"\004PKU"		/* Protection Keys for User-mode pages */
				"\005OSPKE"		/* PKU enabled by OS */
				"\006WAITPKG"		/* Timed pause and user-level monitor/wait */
				"\007AVX512VBMI2"	/* AVX-512 Vector Byte Manipulation 2 */
				"\010CET_SS"		/* CET Shadow Stack */
				"\011GFNI"		/* Galois Field instructions */
				"\012VAES"		/* Vector AES instruction set */
				"\013VPCLMULQDQ"	/* CLMUL instruction set */
				"\014AVX512VNNI"	/* Vector Neural Network instructions */
				"\015AVX512BITALG"	/* BITALG instructions */
				"\016TME"		/* Total Memory Encryption */
				"\017AVX512VPOPCNTDQ"	/* Vector Population Count Double/Quadword */
				"\021LA57"		/* 57-bit linear addr & 5-level paging */
				"\027RDPID"		/* RDPID and IA32_TSC_AUX */
				"\030KL"		/* Key Locker */
				"\031BUS_LOCK_DETECT"	/* Bus-Lock Detection */
				"\032CLDEMOTE"		/* Cache line demote */
				"\034MOVDIRI"		/* MOVDIRI instruction */
				"\035MOVDIR64B"		/* MOVDIR64B instruction */
				"\036ENQCMD"		/* Enqueue Stores */
				"\037SGXLC"		/* SGX Launch Configuration */
				"\040PKS"		/* Protection Keys for kern-mode pages */
				, cpu_stdext_feature2);
			}

			if (cpu_stdext_feature3 != 0) {
				kprintf("\n  Structured Extended Features3=0x%pb%i",
				"\020"
				"\003AVX512_4VNNIW"	/* AVX512 4-reg Neural Network instructions */
				"\004AVX512_4FMAPS"	/* AVX512 4-reg Multiply Accumulation Single precision */
				"\005FSRM"		/* Fast Short REP MOVE */
				"\006UINTR"		/* User Interrupts */
				"\011AVX512VP2INTERSECT" /* AVX512 VP2INTERSECT */
				"\012MCUOPT"		/* IA32_MCU_OPT_CTRL */
				"\013MD_CLEAR"		/* VERW clears CPU buffers */
				"\016TSXFA"		/* MSR_TSX_FORCE_ABORT bit 0 */
				"\017SERIALIZE"		/* SERIALIZE instruction */
				"\020HYBRID"		/* Hybrid part */
				"\021TSXLDTRK"		/* TSX suspend load addr tracking */
				"\023PCONFIG"		/* Platform configuration */
				"\025IBT"		/* CET Indirect Branch Tracking */
				"\033IBPB"		/* IBRS / IBPB Speculation Control */
				"\034STIBP"		/* STIBP Speculation Control */
				"\035L1DFL"		/* IA32_FLUSH_CMD MSR */
				"\036ARCH_CAP"		/* IA32_ARCH_CAPABILITIES */
				"\037CORE_CAP"		/* IA32_CORE_CAPABILITIES */
				"\040SSBD"		/* Speculative Store Bypass Disable */
				, cpu_stdext_feature3);
			}

			if (cpu_thermal_feature != 0) {
				kprintf("\n  Thermal and PM Features=0x%pb%i",
				"\020"
				"\001SENSOR"	/* Digital temperature sensor */
				"\002TURBO"	/* Turbo boost */
				"\003ARAT"	/* APIC-Timer-always-running */
				"\005PLN"	/* Power limit notification controls */
				"\006ECMD"	/* Clock modulation duty cycle extension */
				"\007PTM"	/* Package thermal management */
				"\010HWP"	/* Hardware P-states */
				, cpu_thermal_feature);
			}

			if (cpu_mwait_feature != 0) {
				kprintf("\n  MONITOR/MWAIT Features=0x%pb%i",
				"\020"
				"\001CST"	/* Enumeration of Monitor-Mwait extension */
				"\002INTBRK"	/* Interrupts as break-event for MWAIT */
				, cpu_mwait_feature);
			}

			if (cpu_vendor_id == CPU_VENDOR_CENTAUR)
				print_via_padlock_info();

			if (cpu_feature2 & CPUID2_VMX)
				print_vmx_info();

			if (amd_feature2 & AMDID2_SVM)
				print_svm_info();

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
#if 0
			/*
			 * XXX For Intel CPUs, this is max number of cores per
			 * package, not the actual cores per package.
			 */
			cpu_cores = cmp;
			cpu_logical = htt / cmp;

			if (cpu_cores > 1)
				kprintf("\n  Cores per package: %d", cpu_cores);
			if (cpu_logical > 1) {
				kprintf("\n  Logical CPUs per core: %d",
				    cpu_logical);
			}
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
		else if (cpu_vendor_id == CPU_VENDOR_INTEL)
			print_INTEL_info();
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
	cpu_class = x86_64_cpus[cpu_type].cpu_class;

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
print_INTEL_TLB(u_int data)
{
	switch (data) {
	case 0x0:
	case 0x40:
	default:
		break;
	case 0x1:
		kprintf("Instruction TLB: 4 KB pages, 4-way set associative, 32 entries\n");
		break;
	case 0x2:
		kprintf("Instruction TLB: 4 MB pages, fully associative, 2 entries\n");
		break;
	case 0x3:
		kprintf("Data TLB: 4 KB pages, 4-way set associative, 64 entries\n");
		break;
	case 0x4:
		kprintf("Data TLB: 4 MB Pages, 4-way set associative, 8 entries\n");
		break;
	case 0x6:
		kprintf("1st-level instruction cache: 8 KB, 4-way set associative, 32 byte line size\n");
		break;
	case 0x8:
		kprintf("1st-level instruction cache: 16 KB, 4-way set associative, 32 byte line size\n");
		break;
	case 0x9:
		kprintf("1st-level instruction cache: 32 KB, 4-way set associative, 64 byte line size\n");
		break;
	case 0xa:
		kprintf("1st-level data cache: 8 KB, 2-way set associative, 32 byte line size\n");
		break;
	case 0xb:
		kprintf("Instruction TLB: 4 MByte pages, 4-way set associative, 4 entries\n");
		break;
	case 0xc:
		kprintf("1st-level data cache: 16 KB, 4-way set associative, 32 byte line size\n");
		break;
	case 0xd:
		kprintf("1st-level data cache: 16 KBytes, 4-way set associative, 64 byte line size");
		break;
	case 0xe:
		kprintf("1st-level data cache: 24 KBytes, 6-way set associative, 64 byte line size\n");
		break;
	case 0x1d:
		kprintf("2nd-level cache: 128 KBytes, 2-way set associative, 64 byte line size\n");
		break;
	case 0x21:
		kprintf("2nd-level cache: 256 KBytes, 8-way set associative, 64 byte line size\n");
		break;
	case 0x22:
		kprintf("3rd-level cache: 512 KB, 4-way set associative, sectored cache, 64 byte line size\n");
		break;
	case 0x23:
		kprintf("3rd-level cache: 1 MB, 8-way set associative, sectored cache, 64 byte line size\n");
		break;
	case 0x24:
		kprintf("2nd-level cache: 1 MBytes, 16-way set associative, 64 byte line size\n");
		break;
	case 0x25:
		kprintf("3rd-level cache: 2 MB, 8-way set associative, sectored cache, 64 byte line size\n");
		break;
	case 0x29:
		kprintf("3rd-level cache: 4 MB, 8-way set associative, sectored cache, 64 byte line size\n");
		break;
	case 0x2c:
		kprintf("1st-level data cache: 32 KB, 8-way set associative, 64 byte line size\n");
		break;
	case 0x30:
		kprintf("1st-level instruction cache: 32 KB, 8-way set associative, 64 byte line size\n");
		break;
	case 0x39: /* De-listed in SDM rev. 54 */
		kprintf("2nd-level cache: 128 KB, 4-way set associative, sectored cache, 64 byte line size\n");
		break;
	case 0x3b: /* De-listed in SDM rev. 54 */
		kprintf("2nd-level cache: 128 KB, 2-way set associative, sectored cache, 64 byte line size\n");
		break;
	case 0x3c: /* De-listed in SDM rev. 54 */
		kprintf("2nd-level cache: 256 KB, 4-way set associative, sectored cache, 64 byte line size\n");
		break;
	case 0x41:
		kprintf("2nd-level cache: 128 KB, 4-way set associative, 32 byte line size\n");
		break;
	case 0x42:
		kprintf("2nd-level cache: 256 KB, 4-way set associative, 32 byte line size\n");
		break;
	case 0x43:
		kprintf("2nd-level cache: 512 KB, 4-way set associative, 32 byte line size\n");
		break;
	case 0x44:
		kprintf("2nd-level cache: 1 MB, 4-way set associative, 32 byte line size\n");
		break;
	case 0x45:
		kprintf("2nd-level cache: 2 MB, 4-way set associative, 32 byte line size\n");
		break;
	case 0x46:
		kprintf("3rd-level cache: 4 MB, 4-way set associative, 64 byte line size\n");
		break;
	case 0x47:
		kprintf("3rd-level cache: 8 MB, 8-way set associative, 64 byte line size\n");
		break;
	case 0x48:
		kprintf("2nd-level cache: 3MByte, 12-way set associative, 64 byte line size\n");
		break;
	case 0x49:
		if (CPUID_TO_FAMILY(cpu_id) == 0xf &&
		    CPUID_TO_MODEL(cpu_id) == 0x6)
			kprintf("3rd-level cache: 4MB, 16-way set associative, 64-byte line size\n");
		else
			kprintf("2nd-level cache: 4 MByte, 16-way set associative, 64 byte line size\n");
		break;
	case 0x4a:
		kprintf("3rd-level cache: 6MByte, 12-way set associative, 64 byte line size\n");
		break;
	case 0x4b:
		kprintf("3rd-level cache: 8MByte, 16-way set associative, 64 byte line size\n");
		break;
	case 0x4c:
		kprintf("3rd-level cache: 12MByte, 12-way set associative, 64 byte line size\n");
		break;
	case 0x4d:
		kprintf("3rd-level cache: 16MByte, 16-way set associative, 64 byte line size\n");
		break;
	case 0x4e:
		kprintf("2nd-level cache: 6MByte, 24-way set associative, 64 byte line size\n");
		break;
	case 0x4f:
		kprintf("Instruction TLB: 4 KByte pages, 32 entries\n");
		break;
	case 0x50:
		kprintf("Instruction TLB: 4 KB, 2 MB or 4 MB pages, fully associative, 64 entries\n");
		break;
	case 0x51:
		kprintf("Instruction TLB: 4 KB, 2 MB or 4 MB pages, fully associative, 128 entries\n");
		break;
	case 0x52:
		kprintf("Instruction TLB: 4 KB, 2 MB or 4 MB pages, fully associative, 256 entries\n");
		break;
	case 0x55:
		kprintf("Instruction TLB: 2-MByte or 4-MByte pages, fully associative, 7 entries\n");
		break;
	case 0x56:
		kprintf("Data TLB0: 4 MByte pages, 4-way set associative, 16 entries\n");
		break;
	case 0x57:
		kprintf("Data TLB0: 4 KByte pages, 4-way associative, 16 entries\n");
		break;
	case 0x59:
		kprintf("Data TLB0: 4 KByte pages, fully associative, 16 entries\n");
		break;
	case 0x5a:
		kprintf("Data TLB0: 2-MByte or 4 MByte pages, 4-way set associative, 32 entries\n");
		break;
	case 0x5b:
		kprintf("Data TLB: 4 KB or 4 MB pages, fully associative, 64 entries\n");
		break;
	case 0x5c:
		kprintf("Data TLB: 4 KB or 4 MB pages, fully associative, 128 entries\n");
		break;
	case 0x5d:
		kprintf("Data TLB: 4 KB or 4 MB pages, fully associative, 256 entries\n");
		break;
	case 0x60:
		kprintf("1st-level data cache: 16 KB, 8-way set associative, sectored cache, 64 byte line size\n");
		break;
	case 0x61:
		kprintf("Instruction TLB: 4 KByte pages, fully associative, 48 entries\n");
		break;
	case 0x63:
		kprintf("Data TLB: 2 MByte or 4 MByte pages, 4-way set associative, 32 entries and a separate array with 1 GByte pages, 4-way set associative, 4 entries\n");
		break;
	case 0x64:
		kprintf("Data TLB: 4 KBytes pages, 4-way set associative, 512 entries\n");
		break;
	case 0x66:
		kprintf("1st-level data cache: 8 KB, 4-way set associative, sectored cache, 64 byte line size\n");
		break;
	case 0x67:
		kprintf("1st-level data cache: 16 KB, 4-way set associative, sectored cache, 64 byte line size\n");
		break;
	case 0x68:
		kprintf("1st-level data cache: 32 KB, 4 way set associative, sectored cache, 64 byte line size\n");
		break;
	case 0x6a:
		kprintf("uTLB: 4KByte pages, 8-way set associative, 64 entries\n");
		break;
	case 0x6b:
		kprintf("DTLB: 4KByte pages, 8-way set associative, 256 entries\n");
		break;
	case 0x6c:
		kprintf("DTLB: 2M/4M pages, 8-way set associative, 128 entries\n");
		break;
	case 0x6d:
		kprintf("DTLB: 1 GByte pages, fully associative, 16 entries\n");
		break;
	case 0x70:
		kprintf("Trace cache: 12K-uops, 8-way set associative\n");
		break;
	case 0x71:
		kprintf("Trace cache: 16K-uops, 8-way set associative\n");
		break;
	case 0x72:
		kprintf("Trace cache: 32K-uops, 8-way set associative\n");
		break;
	case 0x76:
		kprintf("Instruction TLB: 2M/4M pages, fully associative, 8 entries\n");
		break;
	case 0x78:
		kprintf("2nd-level cache: 1 MB, 4-way set associative, 64-byte line size\n");
		break;
	case 0x79:
		kprintf("2nd-level cache: 128 KB, 8-way set associative, sectored cache, 64 byte line size\n");
		break;
	case 0x7a:
		kprintf("2nd-level cache: 256 KB, 8-way set associative, sectored cache, 64 byte line size\n");
		break;
	case 0x7b:
		kprintf("2nd-level cache: 512 KB, 8-way set associative, sectored cache, 64 byte line size\n");
		break;
	case 0x7c:
		kprintf("2nd-level cache: 1 MB, 8-way set associative, sectored cache, 64 byte line size\n");
		break;
	case 0x7d:
		kprintf("2nd-level cache: 2-MB, 8-way set associative, 64-byte line size\n");
		break;
	case 0x7f:
		kprintf("2nd-level cache: 512-KB, 2-way set associative, 64-byte line size\n");
		break;
	case 0x80:
		kprintf("2nd-level cache: 512 KByte, 8-way set associative, 64-byte line size\n");
		break;
	case 0x82:
		kprintf("2nd-level cache: 256 KB, 8-way set associative, 32 byte line size\n");
		break;
	case 0x83:
		kprintf("2nd-level cache: 512 KB, 8-way set associative, 32 byte line size\n");
		break;
	case 0x84:
		kprintf("2nd-level cache: 1 MB, 8-way set associative, 32 byte line size\n");
		break;
	case 0x85:
		kprintf("2nd-level cache: 2 MB, 8-way set associative, 32 byte line size\n");
		break;
	case 0x86:
		kprintf("2nd-level cache: 512 KB, 4-way set associative, 64 byte line size\n");
		break;
	case 0x87:
		kprintf("2nd-level cache: 1 MB, 8-way set associative, 64 byte line size\n");
		break;
	case 0xa0:
		kprintf("DTLB: 4k pages, fully associative, 32 entries\n");
		break;
	case 0xb0:
		kprintf("Instruction TLB: 4 KB Pages, 4-way set associative, 128 entries\n");
		break;
	case 0xb1:
		kprintf("Instruction TLB: 2M pages, 4-way, 8 entries or 4M pages, 4-way, 4 entries\n");
		break;
	case 0xb2:
		kprintf("Instruction TLB: 4KByte pages, 4-way set associative, 64 entries\n");
		break;
	case 0xb3:
		kprintf("Data TLB: 4 KB Pages, 4-way set associative, 128 entries\n");
		break;
	case 0xb4:
		kprintf("Data TLB1: 4 KByte pages, 4-way associative, 256 entries\n");
		break;
	case 0xb5:
		kprintf("Instruction TLB: 4KByte pages, 8-way set associative, 64 entries\n");
		break;
	case 0xb6:
		kprintf("Instruction TLB: 4KByte pages, 8-way set associative, 128 entries\n");
		break;
	case 0xba:
		kprintf("Data TLB1: 4 KByte pages, 4-way associative, 64 entries\n");
		break;
	case 0xc0:
		kprintf("Data TLB: 4 KByte and 4 MByte pages, 4-way associative, 8 entries\n");
		break;
	case 0xc1:
		kprintf("Shared 2nd-Level TLB: 4 KByte/2MByte pages, 8-way associative, 1024 entries\n");
		break;
	case 0xc2:
		kprintf("DTLB: 4 KByte/2 MByte pages, 4-way associative, 16 entries\n");
		break;
	case 0xc3:
		kprintf("Shared 2nd-Level TLB: 4 KByte /2 MByte pages, 6-way associative, 1536 entries. Also 1GBbyte pages, 4-way, 16 entries\n");
		break;
	case 0xc4:
		kprintf("DTLB: 2M/4M Byte pages, 4-way associative, 32 entries\n");
		break;
	case 0xca:
		kprintf("Shared 2nd-Level TLB: 4 KByte pages, 4-way associative, 512 entries\n");
		break;
	case 0xd0:
		kprintf("3rd-level cache: 512 KByte, 4-way set associative, 64 byte line size\n");
		break;
	case 0xd1:
		kprintf("3rd-level cache: 1 MByte, 4-way set associative, 64 byte line size\n");
		break;
	case 0xd2:
		kprintf("3rd-level cache: 2 MByte, 4-way set associative, 64 byte line size\n");
		break;
	case 0xd6:
		kprintf("3rd-level cache: 1 MByte, 8-way set associative, 64 byte line size\n");
		break;
	case 0xd7:
		kprintf("3rd-level cache: 2 MByte, 8-way set associative, 64 byte line size\n");
		break;
	case 0xd8:
		kprintf("3rd-level cache: 4 MByte, 8-way set associative, 64 byte line size\n");
		break;
	case 0xdc:
		kprintf("3rd-level cache: 1.5 MByte, 12-way set associative, 64 byte line size\n");
		break;
	case 0xdd:
		kprintf("3rd-level cache: 3 MByte, 12-way set associative, 64 byte line size\n");
		break;
	case 0xde:
		kprintf("3rd-level cache: 6 MByte, 12-way set associative, 64 byte line size\n");
		break;
	case 0xe2:
		kprintf("3rd-level cache: 2 MByte, 16-way set associative, 64 byte line size\n");
		break;
	case 0xe3:
		kprintf("3rd-level cache: 4 MByte, 16-way set associative, 64 byte line size\n");
		break;
	case 0xe4:
		kprintf("3rd-level cache: 8 MByte, 16-way set associative, 64 byte line size\n");
		break;
	case 0xea:
		kprintf("3rd-level cache: 12MByte, 24-way set associative, 64 byte line size\n");
		break;
	case 0xeb:
		kprintf("3rd-level cache: 18MByte, 24-way set associative, 64 byte line size\n");
		break;
	case 0xec:
		kprintf("3rd-level cache: 24MByte, 24-way set associative, 64 byte line size\n");
		break;
	case 0xf0:
		kprintf("64-Byte prefetching\n");
		break;
	case 0xf1:
		kprintf("128-Byte prefetching\n");
		break;
	}
}

static void
print_INTEL_info(void)
{
	u_int regs[4];
	u_int rounds, regnum;
	u_int nwaycode, nway;

	if (cpu_high >= 2) {
		rounds = 0;
		do {
			do_cpuid(0x2, regs);
			if (rounds == 0 && (rounds = (regs[0] & 0xff)) == 0)
				break;	/* we have a buggy CPU */

			for (regnum = 0; regnum <= 3; ++regnum) {
				if (regs[regnum] & (1<<31))
					continue;
				if (regnum != 0)
					print_INTEL_TLB(regs[regnum] & 0xff);
				print_INTEL_TLB((regs[regnum] >> 8) & 0xff);
				print_INTEL_TLB((regs[regnum] >> 16) & 0xff);
				print_INTEL_TLB((regs[regnum] >> 24) & 0xff);
			}
		} while (--rounds > 0);
	}

	if (cpu_exthigh >= 0x80000006) {
		do_cpuid(0x80000006, regs);
		nwaycode = (regs[2] >> 12) & 0x0f;
		if (nwaycode >= 0x02 && nwaycode <= 0x08)
			nway = 1 << (nwaycode / 2);
		else
			nway = 0;
		kprintf("L2 cache: %u kbytes, %u-way associative, "
			"%u bytes/line\n",
			(regs[2] >> 16) & 0xffff, nway, regs[2] & 0xff);
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

static void
print_xsave_info(void)
{
	u_int regs[4];

	cpuid_count(0xd, 0x1, regs);
	if (regs[0] == 0)
		return;

	kprintf("\n  XSAVE Features=0x%pb%i",
		"\020"
		"\001XSAVEOPT"
		"\002XSAVEC"
		"\003XGETBV"
		"\004XSAVES"
		, regs[0]);
}

static void
print_svm_info(void)
{
	u_int features, regs[4];
	uint64_t msr;

	kprintf("\n  SVM: ");
	do_cpuid(0x8000000A, regs);
	features = regs[3];

	msr = rdmsr(MSR_AMD_VM_CR);
	if ((msr & VM_CR_SVMDIS) && (msr & VM_CR_LOCK))
		kprintf("(disabled in BIOS) ");

	kprintf("Features=0x%pb%i",
		"\020"
		"\001NP"		/* Nested paging */
		"\002LbrVirt"		/* LBR virtualization */
		"\003SVML"		/* SVM lock */
		"\004NRIPS"		/* NRIP save */
		"\005TscRateMsr"	/* MSR-based TSC rate control */
		"\006VmcbClean"		/* VMCB clean bits */
		"\007FlushByAsid"	/* Flush by ASID */
		"\010DecodeAssist"	/* Decode Assists support */
		"\011<b8>"
		"\012<b9>"
		"\013PauseFilter"	/* PAUSE intercept filter */
		"\014EncryptedMcodePatch"
		"\015PauseFilterThreshold" /* PAUSE filter threshold */
		"\016AVIC"		/* Advanced Virtual Interrupt Controller */
		"\017<b14>"
		"\020V_VMSAVE_VMLOAD"	/* VMSAVE/VMLOAD virtualization */
		"\021vGIF"		/* Global Interrupt Flag virtualization */
		"\022GMET"		/* Guest Mode Execute Trap */
		"\023<b18>"
		"\024<b19>"
		"\025SpecCtrl"		/* SPEC_CTRL virtualization */
		"\026<b21>"
		"\027<b22>"
		"\030<b23>"
		"\031TlbICtrl"		/* TLB Intercept Control */
		"\032<b25>"
		"\033<b26>"
		"\034<b27>"
		"\035<b28>"
		"\036<b29>"
		"\037<b30>"
		"\040<b31>"
		, features);
	kprintf("\n       Revision=%d, ASIDs=%d", regs[0] & 0xff, regs[1]);
}

static u_int
vmx_settable(uint64_t basic, int msr, int true_msr)
{
	uint64_t val;

	if (basic & (1ULL << 55))
		val = rdmsr(true_msr);
	else
		val = rdmsr(msr);

	/* Just report the controls that can be set to 1. */
	return (u_int)(val >> 32);
}

static void
print_vmx_info(void)
{
	uint64_t basic, msr;
	u_int entry_ctls, exit_ctls, pin_ctls, proc_ctls, proc2_ctls;

	kprintf("\n  VT-x: ");
	msr = rdmsr(MSR_IA32_FEATURE_CONTROL);
	if ((msr & IA32_FEATURE_CONTROL_LOCK) != 0 &&
	    (msr & IA32_FEATURE_CONTROL_OUT_SMX) == 0)
		kprintf("(disabled in BIOS) ");

	basic = rdmsr(MSR_VMX_BASIC);
	pin_ctls = vmx_settable(basic, MSR_VMX_PINBASED_CTLS,
				MSR_VMX_TRUE_PINBASED_CTLS);
	proc_ctls = vmx_settable(basic, MSR_VMX_PROCBASED_CTLS,
				 MSR_VMX_TRUE_PROCBASED_CTLS);
	proc2_ctls = 0;
	if (proc_ctls & (1U << 31 /* activate secondary ctls */)) {
		proc2_ctls = vmx_settable(basic, MSR_VMX_PROCBASED_CTLS2,
					  MSR_VMX_PROCBASED_CTLS2);
	}
	exit_ctls = vmx_settable(basic, MSR_VMX_EXIT_CTLS,
				 MSR_VMX_TRUE_EXIT_CTLS);
	entry_ctls = vmx_settable(basic, MSR_VMX_ENTRY_CTLS,
				  MSR_VMX_TRUE_ENTRY_CTLS);

	kprintf("Basic Features=0x%pb%i",
		"\020"
		"\02132PA"		/* 32-bit physical addresses */
		"\022SMM"		/* SMM dual-monitor */
		"\027INS/OUTS"		/* VM-exit info for INS and OUTS */
		"\030TRUE"		/* TRUE_CTLS MSRs */
		, (u_int)(basic >> 32));
	kprintf("\n        Pin-Based Controls=0x%pb%i",
		"\020"
		"\001ExtINT"		/* External-interrupt exiting */
		"\004NMI"		/* NMI exiting */
		"\006VNMI"		/* Virtual NMIs */
		"\007PreTmr"		/* Activate VMX-preemption timer */
		"\010PostIntr"		/* Process posted interrupts */
		, pin_ctls);
	kprintf("\n        Primary Processor Controls=0x%pb%i",
		"\020"
		"\003INTWIN"		/* Interrupt-window exiting */
		"\004TSCOff"		/* Use TSC offsetting */
		"\010HLT"		/* HLT exiting */
		"\012INVLPG"		/* INVLPG exiting */
		"\013MWAIT"		/* MWAIT exiting */
		"\014RDPMC"		/* RDPMC exiting */
		"\015RDTSC"		/* RDTSC exiting */
		"\020CR3-LD"		/* CR3-load exiting */
		"\021CR3-ST"		/* CR3-store exiting */
		"\024CR8-LD"		/* CR8-load exiting */
		"\025CR8-ST"		/* CR8-store exiting */
		"\026TPR"		/* Use TPR shadow */
		"\027NMIWIN"		/* NMI-window exiting */
		"\030MOV-DR"		/* MOV-DR exiting */
		"\031IO"		/* Unconditional I/O exiting */
		"\032IOmap"		/* Use I/O bitmaps */
		"\034MTF"		/* Monitor trap flag */
		"\035MSRmap"		/* Use MSR bitmaps */
		"\036MONITOR"		/* MONITOR exiting */
		"\037PAUSE"		/* PAUSE exiting */
		, proc_ctls);
	if (proc2_ctls != 0) {
		kprintf("\n        Secondary Processor Controls=0x%pb%i",
			"\020"
			"\001APIC"		/* Virtualize APIC accesses */
			"\002EPT"		/* Enable EPT */
			"\003DT"		/* Descriptor-table exiting */
			"\004RDTSCP"		/* Enable RDTSCP */
			"\005x2APIC"		/* Virtualize x2APIC mode */
			"\006VPID"		/* Enable VPID */
			"\007WBINVD"		/* WBINVD exiting */
			"\010UG"		/* Unrestricted guest */
			"\011APIC-reg"		/* APIC-register virtualization */
			"\012VID"		/* Virtual-interrupt delivery */
			"\013PAUSE-loop"	/* PAUSE-loop exiting */
			"\014RDRAND"		/* RDRAND exiting */
			"\015INVPCID"		/* Enable INVPCID */
			"\016VMFUNC"		/* Enable VM functions */
			"\017VMCS"		/* VMCS shadowing */
			"\020EPT#VE"		/* EPT-violation #VE */
			"\021XSAVES"		/* Enable XSAVES/XRSTORS */
			, proc2_ctls);
	}
	kprintf("\n        Exit Controls=0x%pb%i",
		"\020"
		"\003DR"		/* Save debug controls */
		"\015PERF"		/* Load MSR_PERF_GLOBAL_CTRL */
		"\012HostLMA"		/* Host Long Mode */
		"\020AckInt"		/* Acknowledge interrupt on exit */
		"\023PAT-SV"		/* Save MSR_PAT */
		"\024PAT-LD"		/* Load MSR_PAT */
		"\025EFER-SV"		/* Save MSR_EFER */
		"\026EFER-LD"		/* Load MSR_EFER */
		"\027PTMR-SV"		/* Save VMX-preemption timer value */
		, exit_ctls);
	kprintf("\n        Entry Controls=0x%pb%i",
		"\020"
		"\003DR"		/* Save debug controls */
		"\012GuestLMA"		/* Guest Long Mode */
		"\016PERF"		/* Load MSR_PERF_GLOBAL_CTRL */
		"\017PAT"		/* Load MSR_PAT */
		"\020EFER"		/* Load MSR_EFER */
		, entry_ctls);
	if ((proc2_ctls & (1U << 1 /* Enable EPT */)) ||
	    (proc2_ctls & (1U << 5 /* Enable VPID */)))
	{
		msr = rdmsr(MSR_VMX_EPT_VPID_CAP);
		kprintf("\n        EPT Features=0x%pb%i",
			"\020"
			"\001XO"	/* Execute-only translations */
			"\007PW4"	/* Page-walk length of 4 */
			"\011UC"	/* EPT paging-structure mem can be UC */
			"\017WB"	/* EPT paging-structure mem can be WB */
			"\0212M"	/* EPT PDE can map a 2-Mbyte page */
			"\0221G"	/* EPT PDPTE can map a 1-Gbyte page */
			"\025INVEPT"	/* INVEPT is supported */
			"\026AD"	/* Accessed and dirty flags for EPT */
			"\032single"	/* INVEPT single-context type */
			"\033all"	/* INVEPT all-context type */
			, (u_int)msr);
		kprintf("\n        VPID Features=0x%pb%i",
			"\020"
			"\001INVVPID"	/* INVVPID is supported */
			"\011individual" /* INVVPID individual-address type */
			"\012single"	/* INVVPID single-context type */
			"\013all"	/* INVVPID all-context type */
			"\014single-globals" /* INVVPID single-context-retaining-globals type */
			, (u_int)(msr >> 32));
	}
}
