/*
 * Copyright (c) 2018-2021 Maxime Villard, m00nbsd.net
 * All rights reserved.
 *
 * This code is part of the NVMM hypervisor.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _NVMM_X86_INTERNAL_H_
#define _NVMM_X86_INTERNAL_H_

#ifndef _KERNEL
#error "This file should not be included by userland programs."
#endif

#include "nvmm_x86.h"

#define NVMM_X86_MACH_NCONF	0
#define NVMM_X86_VCPU_NCONF	2

struct nvmm_x86_cpuid_mask {
	uint32_t eax;
	uint32_t ebx;
	uint32_t ecx;
	uint32_t edx;
};

/* FPU area + XSAVE header. */
struct nvmm_x86_xsave {
	struct nvmm_x64_state_fpu fpu;
	uint64_t xstate_bv;
	uint64_t xcomp_bv;
	uint8_t rsvd0[8];
	uint8_t rsvd[40];
};
CTASSERT(sizeof(struct nvmm_x86_xsave) == 512 + 64);

extern const struct nvmm_x64_state nvmm_x86_reset_state;
extern const struct nvmm_x86_cpuid_mask nvmm_cpuid_00000001;
extern const struct nvmm_x86_cpuid_mask nvmm_cpuid_00000007;
extern const struct nvmm_x86_cpuid_mask nvmm_cpuid_80000001;
extern const struct nvmm_x86_cpuid_mask nvmm_cpuid_80000007;
extern const struct nvmm_x86_cpuid_mask nvmm_cpuid_80000008;

bool nvmm_x86_pat_validate(uint64_t);
uint32_t nvmm_x86_xsave_size(uint64_t);

/* -------------------------------------------------------------------------- */

/*
 * ASM defines. We mainly rely on the already-existing OS definitions.
 */

#if defined(__NetBSD__)
#include <x86/cpufunc.h>
#include <x86/fpu.h>
#elif defined(__DragonFly__)
#include <machine/cpufunc.h>
#include <machine/npx.h>
#endif

/* CPUID. */
typedef struct {
	uint32_t eax, ebx, ecx, edx;
} cpuid_desc_t;

#if defined(__NetBSD__)
#define x86_get_cpuid(l, d)	x86_cpuid(l, (uint32_t *)d)
#define x86_get_cpuid2(l, c, d)	x86_cpuid2(l, c, (uint32_t *)d)
#elif defined(__DragonFly__)
#define x86_get_cpuid(l, d)	do_cpuid(l, (uint32_t *)d)
#define x86_get_cpuid2(l, c, d)	cpuid_count(l, c, (uint32_t *)d)
#endif

/* Control registers. */
#if defined(__NetBSD__)
#define x86_get_cr0()		rcr0()
#define x86_get_cr2()		rcr2()
#define x86_get_cr3()		rcr3()
#define x86_get_cr4()		rcr4()
#define x86_set_cr0(v)		lcr0(v)
#define x86_set_cr2(v)		lcr2(v)
#define x86_set_cr4(v)		lcr4(v)
#elif defined(__DragonFly__)
#define x86_get_cr0()		rcr0()
#define x86_get_cr2()		rcr2()
#define x86_get_cr3()		rcr3()
#define x86_get_cr4()		rcr4()
#define x86_set_cr0(v)		load_cr0(v)
#define x86_set_cr2(v)		load_cr2(v)
#define x86_set_cr4(v)		load_cr4(v)
#endif

/* Debug registers. */
#if defined(__NetBSD__)
#include <x86/dbregs.h>
static inline void
x86_curthread_save_dbregs(uint64_t *drs __unused)
{
	x86_dbregs_save(curlwp);
}
static inline void
x86_curthread_restore_dbregs(uint64_t *drs __unused)
{
	x86_dbregs_restore(curlwp);
}
#define x86_get_dr0()		rdr0()
#define x86_get_dr1()		rdr1()
#define x86_get_dr2()		rdr2()
#define x86_get_dr3()		rdr3()
#define x86_get_dr6()		rdr6()
#define x86_get_dr7()		rdr7()
#define x86_set_dr0(v)		ldr0(v)
#define x86_set_dr1(v)		ldr1(v)
#define x86_set_dr2(v)		ldr2(v)
#define x86_set_dr3(v)		ldr3(v)
#define x86_set_dr6(v)		ldr6(v)
#define x86_set_dr7(v)		ldr7(v)
#elif defined(__DragonFly__)
#include <sys/proc.h> /* struct lwp */
static inline void
x86_curthread_save_dbregs(uint64_t *drs)
{
	struct pcb *pcb = curthread->td_lwp->lwp_thread->td_pcb;

	if (__predict_true(!(pcb->pcb_flags & PCB_DBREGS)))
		return;

	drs[NVMM_X64_DR_DR0] = rdr0();
	drs[NVMM_X64_DR_DR1] = rdr1();
	drs[NVMM_X64_DR_DR2] = rdr2();
	drs[NVMM_X64_DR_DR3] = rdr3();
	drs[NVMM_X64_DR_DR6] = rdr6();
	drs[NVMM_X64_DR_DR7] = rdr7();
}
static inline void
x86_curthread_restore_dbregs(uint64_t *drs)
{
	struct pcb *pcb = curthread->td_lwp->lwp_thread->td_pcb;

	if (__predict_true(!(pcb->pcb_flags & PCB_DBREGS)))
		return;

	load_dr0(drs[NVMM_X64_DR_DR0]);
	load_dr1(drs[NVMM_X64_DR_DR1]);
	load_dr2(drs[NVMM_X64_DR_DR2]);
	load_dr3(drs[NVMM_X64_DR_DR3]);
	load_dr6(drs[NVMM_X64_DR_DR6]);
	load_dr7(drs[NVMM_X64_DR_DR7]);
}
#define x86_get_dr0()		rdr0()
#define x86_get_dr1()		rdr1()
#define x86_get_dr2()		rdr2()
#define x86_get_dr3()		rdr3()
#define x86_get_dr6()		rdr6()
#define x86_get_dr7()		rdr7()
#define x86_set_dr0(v)		load_dr0(v)
#define x86_set_dr1(v)		load_dr1(v)
#define x86_set_dr2(v)		load_dr2(v)
#define x86_set_dr3(v)		load_dr3(v)
#define x86_set_dr6(v)		load_dr6(v)
#define x86_set_dr7(v)		load_dr7(v)
#endif

/* FPU. */
#if defined(__NetBSD__)
#define x86_curthread_save_fpu()	fpu_kern_enter()
#define x86_curthread_restore_fpu()	fpu_kern_leave()
#define x86_save_fpu(a, m)		fpu_area_save(a, m, true)
#define x86_restore_fpu(a, m)		fpu_area_restore(a, m, true)
#elif defined(__DragonFly__)
#define x86_curthread_save_fpu()	/* TODO */
#define x86_curthread_restore_fpu()	/* TODO */
#define x86_save_fpu(a, m)				\
	({						\
		fpusave((union savefpu *)(a), m);	\
		load_cr0(rcr0() | CR0_TS);		\
	})
#define x86_restore_fpu(a, m)				\
	({						\
		__asm volatile("clts" ::: "memory");	\
		fpurstor((union savefpu *)(a), m);	\
	})
#endif

/* XCRs. */
static inline uint64_t
x86_get_xcr(uint32_t xcr)
{
	uint32_t low, high;

	__asm volatile (
		"xgetbv"
		: "=a" (low), "=d" (high)
		: "c" (xcr)
	);

	return (low | ((uint64_t)high << 32));
}

static inline void
x86_set_xcr(uint32_t xcr, uint64_t val)
{
	uint32_t low, high;

	low = val;
	high = val >> 32;
	__asm volatile (
		"xsetbv"
		:
		: "a" (low), "d" (high), "c" (xcr)
		: "memory"
	);
}

#if defined(__DragonFly__)
#define x86_xsave_features	npx_xcr0_mask
#define x86_fpu_mxcsr_mask	npx_mxcsr_mask
#endif

#endif /* _NVMM_X86_INTERNAL_H_ */
