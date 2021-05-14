/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2021 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Aaron LI <aly@aaronly.me>
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

/*
 * Compatibility code to adapt NVMM for DragonFly.
 */

#ifndef _NVMM_COMPAT_H_
#define _NVMM_COMPAT_H_

#include <sys/param.h>
#include <sys/bitops.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/systm.h>

#include <machine/atomic.h>
#include <machine/cpufunc.h>
#include <machine/specialreg.h>

#ifdef __x86_64__

#undef  __BIT
#define __BIT(__n)		__BIT64(__n)
#undef  __BITS
#define __BITS(__m, __n)	__BITS64(__m, __n)

#endif /* __x86_64__ */

/*
 * CPUID Fn0000_0001 features
 */
#define CPUID2_MONITOR		CPUID2_MON
#define CPUID2_DEADLINE		CPUID2_TSCDLT
#define CPUID2_RAZ		CPUID2_VMM

/*
 * Intel/AMD Structured Extended Feature
 * CPUID Fn0000_0007
 */
/* %ecx = 0, %ebx */
#define CPUID_SEF_FSGSBASE	CPUID_STDEXT_FSGSBASE
#define CPUID_SEF_TSC_ADJUST	CPUID_STDEXT_TSC_ADJUST
#define CPUID_SEF_SGX		CPUID_STDEXT_SGX
#define CPUID_SEF_BMI1		CPUID_STDEXT_BMI1
#define CPUID_SEF_HLE		CPUID_STDEXT_HLE
#define CPUID_SEF_AVX2		CPUID_STDEXT_AVX2
#define CPUID_SEF_FDPEXONLY	CPUID_STDEXT_FDP_EXC
#define CPUID_SEF_SMEP		CPUID_STDEXT_SMEP
#define CPUID_SEF_BMI2		CPUID_STDEXT_BMI2
#define CPUID_SEF_ERMS		CPUID_STDEXT_ERMS
#define CPUID_SEF_INVPCID	CPUID_STDEXT_INVPCID
#define CPUID_SEF_RTM		CPUID_STDEXT_RTM
#define CPUID_SEF_QM		CPUID_STDEXT_PQM
#define CPUID_SEF_FPUCSDS	CPUID_STDEXT_NFPUSG
#define CPUID_SEF_MPX		CPUID_STDEXT_MPX
#define CPUID_SEF_PQE		CPUID_STDEXT_PQE
#define CPUID_SEF_AVX512F	CPUID_STDEXT_AVX512F
#define CPUID_SEF_AVX512DQ	CPUID_STDEXT_AVX512DQ
#define CPUID_SEF_RDSEED	CPUID_STDEXT_RDSEED
#define CPUID_SEF_ADX		CPUID_STDEXT_ADX
#define CPUID_SEF_SMAP		CPUID_STDEXT_SMAP
#define CPUID_SEF_AVX512_IFMA	CPUID_STDEXT_AVX512IFMA
#define CPUID_SEF_CLFLUSHOPT	CPUID_STDEXT_CLFLUSHOPT
#define CPUID_SEF_CLWB		CPUID_STDEXT_CLWB
#define CPUID_SEF_PT		CPUID_STDEXT_PROCTRACE
#define CPUID_SEF_AVX512PF	CPUID_STDEXT_AVX512PF
#define CPUID_SEF_AVX512ER	CPUID_STDEXT_AVX512ER
#define CPUID_SEF_AVX512CD	CPUID_STDEXT_AVX512CD
#define CPUID_SEF_SHA		CPUID_STDEXT_SHA
#define CPUID_SEF_AVX512BW	CPUID_STDEXT_AVX512BW
#define CPUID_SEF_AVX512VL	CPUID_STDEXT_AVX512VL
/* %ecx = 0, %ecx */
#define CPUID_SEF_PREFETCHWT1	CPUID_STDEXT2_PREFETCHWT1
#define CPUID_SEF_AVX512_VBMI	CPUID_STDEXT2_AVX512VBMI
#define CPUID_SEF_UMIP		CPUID_STDEXT2_UMIP
#define CPUID_SEF_PKU		CPUID_STDEXT2_PKU
#define CPUID_SEF_OSPKE		CPUID_STDEXT2_OSPKE
#define CPUID_SEF_WAITPKG	CPUID_STDEXT2_WAITPKG
#define CPUID_SEF_AVX512_VBMI2	CPUID_STDEXT2_AVX512VBMI2
#define CPUID_SEF_CET_SS	CPUID_STDEXT2_CET_SS
#define CPUID_SEF_GFNI		CPUID_STDEXT2_GFNI
#define CPUID_SEF_VAES		CPUID_STDEXT2_VAES
#define CPUID_SEF_VPCLMULQDQ	CPUID_STDEXT2_VPCLMULQDQ
#define CPUID_SEF_AVX512_VNNI	CPUID_STDEXT2_AVX512VNNI
#define CPUID_SEF_AVX512_BITALG	CPUID_STDEXT2_AVX512BITALG
#define CPUID_SEF_AVX512_VPOPCNTDQ	CPUID_STDEXT2_AVX512VPOPCNTDQ
#define CPUID_SEF_LA57		CPUID_STDEXT2_LA57
#define CPUID_SEF_MAWAU		__BITS(21, 17) /* MAWAU for BNDLDX/BNDSTX */
#define CPUID_SEF_RDPID		CPUID_STDEXT2_RDPID
#define CPUID_SEF_KL		CPUID_STDEXT2_KL
#define CPUID_SEF_CLDEMOTE	CPUID_STDEXT2_CLDEMOTE
#define CPUID_SEF_MOVDIRI	CPUID_STDEXT2_MOVDIRI
#define CPUID_SEF_MOVDIR64B	CPUID_STDEXT2_MOVDIR64B
#define CPUID_SEF_SGXLC		CPUID_STDEXT2_SGXLC
#define CPUID_SEF_PKS		CPUID_STDEXT2_PKS
/* %ecx = 0, %edx */
#define CPUID_SEF_AVX512_4VNNIW	CPUID_STDEXT3_AVX5124VNNIW
#define CPUID_SEF_AVX512_4FMAPS	CPUID_STDEXT3_AVX5124FMAPS
#define CPUID_SEF_FSREP_MOV	CPUID_STDEXT3_FSRM
#define CPUID_SEF_AVX512_VP2INTERSECT	CPUID_STDEXT3_AVX512VP2INTERSECT
#define CPUID_SEF_SRBDS_CTRL	CPUID_STDEXT3_MCUOPT
#define CPUID_SEF_MD_CLEAR	CPUID_STDEXT3_MD_CLEAR
#define CPUID_SEF_TSX_FORCE_ABORT	CPUID_STDEXT3_TSXFA
#define CPUID_SEF_SERIALIZE	CPUID_STDEXT3_SERIALIZE
#define CPUID_SEF_HYBRID	CPUID_STDEXT3_HYBRID
#define CPUID_SEF_TSXLDTRK	CPUID_STDEXT3_TSXLDTRK
#define CPUID_SEF_CET_IBT	CPUID_STDEXT3_CET_IBT
#define CPUID_SEF_IBRS		CPUID_STDEXT3_IBPB
#define CPUID_SEF_STIBP		CPUID_STDEXT3_STIBP
#define CPUID_SEF_L1D_FLUSH	CPUID_STDEXT3_L1D_FLUSH
#define CPUID_SEF_ARCH_CAP	CPUID_STDEXT3_ARCH_CAP
#define CPUID_SEF_CORE_CAP	CPUID_STDEXT3_CORE_CAP
#define CPUID_SEF_SSBD		CPUID_STDEXT3_SSBD

/*
 * Intel CPUID Extended Topology Enumeration
 * CPUID Fn0000_000B
 */
/* %eax */
#define CPUID_TOP_SHIFTNUM	__BITS(4, 0)	/* Topology ID shift value */
/* %ecx */
#define CPUID_TOP_LVLNUM	__BITS(7, 0)	/* Level number */
#define CPUID_TOP_LVLTYPE	__BITS(15, 8)	/* Level type */
#define CPUID_TOP_LVLTYPE_INVAL	0		/* Invalid */
#define CPUID_TOP_LVLTYPE_SMT	1		/* SMT */
#define CPUID_TOP_LVLTYPE_CORE	2		/* Core */

/*
 * Registers
 */

#define XCR0_X87		CPU_XFEATURE_X87	/* 0x00000001 */
#define XCR0_SSE		CPU_XFEATURE_SSE	/* 0x00000002 */

#define MSR_MISC_ENABLE		MSR_IA32_MISC_ENABLE	/* 0x1a0 */
#define MSR_CR_PAT		MSR_PAT			/* 0x277 */
#define MSR_SFMASK		MSR_SF_MASK		/* 0xc0000084 */
#define MSR_KERNELGSBASE	MSR_KGSBASE		/* 0xc0000102 */
#define MSR_NB_CFG		MSR_AMD_NB_CFG		/* 0xc001001f */
#define MSR_IC_CFG		MSR_AMD_IC_CFG		/* 0xc0011021 */
#define MSR_DE_CFG		MSR_AMD_DE_CFG		/* 0xc0011029 */
#define MSR_UCODE_AMD_PATCHLEVEL MSR_AMD_PATCH_LEVEL	/* 0x0000008b */

/* MSR_IA32_ARCH_CAPABILITIES (0x10a) */
#define 	IA32_ARCH_RDCL_NO	IA32_ARCH_CAP_RDCL_NO
#define 	IA32_ARCH_IBRS_ALL	IA32_ARCH_CAP_IBRS_ALL
#define 	IA32_ARCH_RSBA		IA32_ARCH_CAP_RSBA
#define 	IA32_ARCH_SKIP_L1DFL_VMENTRY	IA32_ARCH_CAP_SKIP_L1DFL_VMENTRY
#define 	IA32_ARCH_SSB_NO	IA32_ARCH_CAP_SSB_NO
#define 	IA32_ARCH_MDS_NO	IA32_ARCH_CAP_MDS_NO
#define 	IA32_ARCH_IF_PSCHANGE_MC_NO	IA32_ARCH_CAP_IF_PSCHANGE_MC_NO
#define 	IA32_ARCH_TSX_CTRL	IA32_ARCH_CAP_TSX_CTRL
#define 	IA32_ARCH_TAA_NO	IA32_ARCH_CAP_TAA_NO

/* MSR_IA32_FLUSH_CMD (0x10b) */
#define 	IA32_FLUSH_CMD_L1D_FLUSH	IA32_FLUSH_CMD_L1D

#define MSR_VMCR	MSR_AMD_VM_CR			/* 0xc0010114 */
#define 	VMCR_DPD	VM_CR_DPD
#define 	VMCR_RINIT	VM_CR_R_INIT
#define 	VMCR_DISA20	VM_CR_DIS_A20M
#define 	VMCR_LOCK	VM_CR_LOCK
#define 	VMCR_SVMED	VM_CR_SVMDIS

/*
 * PAT modes
 * (NetBSD: /sys/arch/x86/include/pmap.h)
 */
#define PATENTRY(n, type)	PAT_VALUE(n, type)
#define PAT_UC			PAT_UNCACHEABLE		/* 0x0ULL */
#define PAT_WC			PAT_WRITE_COMBINING	/* 0x1ULL */
#define PAT_WT			PAT_WRITE_THROUGH	/* 0x4ULL */
#define PAT_WP			PAT_WRITE_PROTECTED	/* 0x5ULL */
#define PAT_WB			PAT_WRITE_BACK		/* 0x6ULL */
#define PAT_UCMINUS		PAT_UNCACHED		/* 0x7ULL */

/*
 * Constants, functions, etc.
 */

#define DIAGNOSTIC		INVARIANTS
#define MAXCPUS			SMP_MAXCPU
#define asm			__asm__
#define printf			kprintf
#define __cacheline_aligned	__cachealign
#define __diagused		__debugvar

#define __arraycount(arr)	nitems(arr)
#define __insn_barrier()	cpu_ccfence()
#undef  KASSERT
#define KASSERT(x)		KKASSERT(x)
#define ilog2(n)		((sizeof(n) > 4 ? ffsl(n) : ffs(n)) - 1)
#define uimin(a, b)		MIN(a, b)

#define lcr2(x)			load_cr2(x)
#define lcr4(x)			load_cr4(x)
#define ldr0(x)			load_dr0(x)
#define ldr1(x)			load_dr1(x)
#define ldr2(x)			load_dr2(x)
#define ldr3(x)			load_dr3(x)
#define ldr6(x)			load_dr6(x)
#define ldr7(x)			load_dr7(x)
#define rdxcr(xcr)		rxcr(xcr)
#define wrxcr(xcr, val)		load_xcr(xcr, val)

#define x86_cpuid(eax, regs)		do_cpuid(eax, regs)
#define x86_cpuid2(eax, ecx, regs)	cpuid_count(eax, ecx, regs)

/*
 * Mutex lock
 */
#define kmutex_t		struct lock
#define mutex_init(lock, type, ipl)	lockinit(lock, "nvmmmtx", 0, 0)
#define mutex_destroy(lock)	lockuninit(lock)
#define mutex_enter(lock)	lockmgr(lock, LK_EXCLUSIVE)
#define mutex_exit(lock)	lockmgr(lock, LK_RELEASE)
#define mutex_owned(lock)	(lockstatus(lock, curthread) == LK_EXCLUSIVE)

/*
 * Reader/writer lock
 */
typedef enum krw_t {
	RW_READER = LK_SHARED,
	RW_WRITER = LK_EXCLUSIVE,
} krw_t;
#define krwlock_t		struct lock
#define rw_init(lock)		lockinit(lock, "nvmmrw", 0, 0)
#define rw_destroy(lock)	lockuninit(lock)
#define rw_enter(lock, op)	lockmgr(lock, op)
#define rw_exit(lock)		lockmgr(lock, LK_RELEASE)
#define rw_write_held(lock)	(lockstatus(lock, curthread) == LK_EXCLUSIVE)

/*
 * Memory allocation
 */
MALLOC_DECLARE(M_NVMM);
enum {
	KM_SLEEP   = M_WAITOK,
	KM_NOSLEEP = M_NOWAIT,
};
#define kmem_alloc(size, flags) \
	({								\
		KKASSERT((flags & ~(KM_SLEEP|KM_NOSLEEP)) == 0);	\
		kmalloc(size, M_NVMM, flags);				\
	})
#define kmem_zalloc(size, flags) \
	({								\
		KKASSERT((flags & ~(KM_SLEEP|KM_NOSLEEP)) == 0);	\
		kmalloc(size, M_NVMM, flags|M_ZERO);			\
	})
#define kmem_free(data, size)	kfree(data, M_NVMM)

/*
 * Atomic operations
 */
#define atomic_inc_64(p)	atomic_add_64(p, 1)
#define atomic_inc_uint(p)	atomic_add_int(p, 1)
#define atomic_dec_uint(p)	atomic_subtract_int(p, 1)

#endif /* _NVMM_COMPAT_H_ */
