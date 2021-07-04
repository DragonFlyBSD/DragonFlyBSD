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

#ifndef _KERNEL
#error "This file should not be included by userland programs."
#endif

#include <sys/param.h>
#include <sys/bitops.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mman.h> /* MADV_RANDOM */
#include <sys/proc.h> /* lwp */
#include <sys/systm.h>
#include <sys/thread2.h>

#include <vm/vm.h>
#include <vm/vm_extern.h>
#include <vm/vm_map.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <vm/vm_pager.h>
#include <vm/vm_param.h> /* KERN_SUCCESS, etc. */

#include <machine/atomic.h>
#include <machine/cpu.h>
#include <machine/cpufunc.h>
#include <machine/md_var.h> /* cpu_high */
#include <machine/npx.h>
#include <machine/specialreg.h>

#ifdef __x86_64__

#undef  __BIT
#define __BIT(__n)		__BIT64(__n)
#undef  __BITS
#define __BITS(__m, __n)	__BITS64(__m, __n)

#endif /* __x86_64__ */

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
 * Constants, functions, etc.
 */

#define DIAGNOSTIC		INVARIANTS
#define MAXCPUS			SMP_MAXCPU
#define curlwp			(curthread->td_lwp)
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

/*
 * CPUID features/level
 */
#define cpuid_level		cpu_high
#define x86_cpuid(eax, regs)	do_cpuid(eax, regs)
#define x86_cpuid2(eax, ecx, regs) \
	cpuid_count(eax, ecx, regs)

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

/*
 * Preemption / critical sections
 *
 * In DragonFly, a normal kernel thread will not migrate to another CPU or be
 * preempted (except by an interrupt thread), so kpreempt_{disable,enable}()
 * are not needed.  However, we can't use critical section as an instead,
 * because that would also prevent interrupt/reschedule flags from being
 * set, which would be a problem for nvmm_return_needed() that's called from
 * vcpu_run() loop.
 */
#define kpreempt_disable()	/* nothing */
#define kpreempt_enable()	/* nothing */
#define kpreempt_disabled()	true

/*
 * FPU
 */
#define x86_xsave_features	npx_xcr0_mask
#define x86_fpu_mxcsr_mask	npx_mxcsr_mask
#define stts()			load_cr0(rcr0() | CR0_TS)
#define clts()			__asm__("clts")

/*
 * Debug registers
 */
static __inline void
x86_dbregs_save(struct lwp *lp)
{
	struct pcb *pcb;

	KKASSERT(lp != NULL && lp->lwp_thread != NULL);
	pcb = lp->lwp_thread->td_pcb;

	if (!(pcb->pcb_flags & PCB_DBREGS))
		return;

	pcb->pcb_dr0 = rdr0();
	pcb->pcb_dr1 = rdr1();
	pcb->pcb_dr2 = rdr2();
	pcb->pcb_dr3 = rdr3();
	pcb->pcb_dr6 = rdr6();
	pcb->pcb_dr7 = rdr7();
}

static __inline void
x86_dbregs_restore(struct lwp *lp)
{
	struct pcb *pcb;

	KKASSERT(lp != NULL && lp->lwp_thread != NULL);
	pcb = lp->lwp_thread->td_pcb;

	if (!(pcb->pcb_flags & PCB_DBREGS))
		return;

	load_dr0(pcb->pcb_dr0);
	load_dr1(pcb->pcb_dr1);
	load_dr2(pcb->pcb_dr2);
	load_dr3(pcb->pcb_dr3);
	load_dr6(pcb->pcb_dr6);
	load_dr7(pcb->pcb_dr7);
}

/*
 * Virtual address space management
 */
typedef vm_offset_t vaddr_t;
typedef vm_offset_t voff_t;
typedef vm_size_t vsize_t;
typedef vm_paddr_t paddr_t;

#define uvm_object		vm_object

static __inline struct vmspace *
uvmspace_alloc(vaddr_t vmin, vaddr_t vmax, bool topdown)
{
	KKASSERT(topdown == false);
	return vmspace_alloc(vmin, vmax);
}

static __inline void
uvmspace_free(struct vmspace *space)
{
	pmap_del_all_cpus(space);
	vmspace_rel(space);
}

static __inline int
uvm_fault(struct vm_map *map, vaddr_t vaddr, vm_prot_t access_type)
{
	int fault_flags;

	if (access_type & VM_PROT_WRITE)
		fault_flags = VM_FAULT_DIRTY;
	else
		fault_flags = VM_FAULT_NORMAL;

	return vm_fault(map, trunc_page(vaddr), access_type, fault_flags);
}

/* NetBSD's UVM functions (e.g., uvm_fault()) return 0 on success,
 * while DragonFly's VM functions return KERN_SUCCESS, which although
 * defined to be 0 as well, but assert it to be future-proof. */
CTASSERT(KERN_SUCCESS == 0);

/* bits 0x07: protection codes */
#define UVM_PROT_MASK		0x07
#define UVM_PROT_RW		VM_PROT_RW
#define UVM_PROT_RWX		VM_PROT_ALL
/* bits 0x30: inherit codes */
#define UVM_INH_MASK		0x30
#define UVM_INH_SHARE		0x00
#define UVM_INH_NONE		0x20
/* bits 0x700: max protection */
/* bits 0x7000: advice codes */
#define UVM_ADV_MASK		0x7
#define UVM_ADV_RANDOM		MADV_RANDOM
/* bits 0xffff0000: mapping flags */
#define UVM_FLAG_FIXED		0x00010000 /* find space */
#define UVM_FLAG_UNMAP		0x08000000 /* unmap existing entries */

/* encoding of flags for uvm_map() */
#define UVM_MAPFLAG(prot, maxprot, inherit, advice, flags) \
	(((advice) << 12) | ((maxprot) << 8) | (inherit) | (prot) | (flags))
/* extract info from flags */
#define UVM_PROTECTION(x)	((x) & UVM_PROT_MASK)
#define UVM_INHERIT(x)		(((x) & UVM_INH_MASK) >> 4)
#define UVM_MAXPROTECTION(x)	(((x) >> 8) & UVM_PROT_MASK)
#define UVM_ADVICE(x)		(((x) >> 12) & UVM_ADV_MASK)

/* Establish a pageable mapping from $obj at $offset in the map $map.
 * The start address of the mapping will be returned in $startp.
 */
static __inline int
uvm_map(struct vm_map *map, vaddr_t *startp /* IN/OUT */, vsize_t size,
    struct vm_object *obj, voff_t offset, vsize_t align, int flags)
{
	vm_offset_t addr = *startp;
	vm_inherit_t inherit = (vm_inherit_t)UVM_INHERIT(flags);
	int advice = UVM_ADVICE(flags);
	int rv = KERN_SUCCESS;
	int count;

	KKASSERT((size & PAGE_MASK) == 0);
	KKASSERT((flags & UVM_FLAG_FIXED) == 0 || align == 0);
	KKASSERT(powerof2(align));
	KKASSERT(inherit == VM_INHERIT_SHARE || inherit == VM_INHERIT_NONE);
	KKASSERT(advice == MADV_RANDOM);
	KKASSERT(obj != NULL);

	if (align == 0)
		align = 1; /* any alignment */

	count = vm_map_entry_reserve(MAP_RESERVE_COUNT);
	vm_map_lock(map);

	if (flags & UVM_FLAG_FIXED) {
		KKASSERT(flags & UVM_FLAG_UNMAP);
		/* Remove any existing entries in the range, so the new
		 * mapping can be created at the requested address. */
		rv = vm_map_delete(map, addr, addr + size, &count);
	} else {
		if (vm_map_findspace(map, addr, size, align, 0, &addr))
			rv = KERN_NO_SPACE;
	}
	if (rv != KERN_SUCCESS) {
		vm_map_unlock(map);
		vm_map_entry_release(count);
		return rv;
	}

	vm_object_hold(obj);
	/* obj reference has been bumped prior to calling uvm_map() */
	rv = vm_map_insert(map, &count, obj, NULL,
	    offset, NULL, addr, addr + size,
	    VM_MAPTYPE_NORMAL, VM_SUBSYS_NVMM,
	    UVM_PROTECTION(flags), UVM_MAXPROTECTION(flags), 0);
	vm_object_drop(obj);
	vm_map_unlock(map);
	vm_map_entry_release(count);
	if (rv != KERN_SUCCESS)
		return rv;

	vm_map_inherit(map, addr, addr + size, inherit);
	vm_map_madvise(map, addr, addr + size, advice, 0);

	*startp = addr;
	return 0;
}

static __inline int
uvm_map_pageable(struct vm_map *map, vaddr_t start, vaddr_t end,
    bool new_pageable, int lockflags)
{
	KKASSERT(lockflags == 0);
	return vm_map_wire(map, start, end, new_pageable ? KM_PAGEABLE : 0);
}

static __inline void
uvm_unmap(struct vm_map *map, vaddr_t start, vaddr_t end)
{
	vm_map_remove(map, start, end);
}

static __inline void
uvm_deallocate(struct vm_map *map, vaddr_t start, vsize_t size)
{
	/* Unwire kernel page before remove, because vm_map_remove() only
	 * handles user wirings.
	 */
	vm_map_wire(map, trunc_page(start), round_page(start + size),
	    KM_PAGEABLE);
	vm_map_remove(map, trunc_page(start), round_page(start + size));
}

/* Kernel memory allocation */

enum {
	UVM_KMF_WIRED = 0x01, /* wired memory */
	UVM_KMF_ZERO  = 0x02, /* want zero-filled memory */
};

/* NOTE: DragonFly's kmem_alloc() may return 0 ! */
static __inline vaddr_t
uvm_km_alloc(struct vm_map *map, vsize_t size, vsize_t align, int flags)
{
	KKASSERT(map == kernel_map);
	KKASSERT(align == 0);
	KKASSERT(flags == (UVM_KMF_WIRED | UVM_KMF_ZERO));

	/* Add parentheses around 'kmem_alloc' to avoid macro expansion */
	return (kmem_alloc)(map, size, VM_SUBSYS_NVMM);
}

static __inline void
uvm_km_free(struct vm_map *map, vaddr_t addr, vsize_t size, int flags __unused)
{
	KKASSERT(map == kernel_map);

	(kmem_free)(map, addr, size);
}

/* Physical page allocation */

struct vm_anon; /* dummy */
enum {
	UVM_PGA_ZERO = VM_ALLOC_ZERO,
};

static __inline struct vm_page *
uvm_pagealloc(struct vm_object *obj, vm_offset_t off, struct vm_anon *anon,
    int flags)
{
	KKASSERT(anon == NULL);
	KKASSERT(flags == UVM_PGA_ZERO);

	return vm_page_alloczwq(OFF_TO_IDX(off),
	    VM_ALLOC_SYSTEM | VM_ALLOC_ZERO | VM_ALLOC_RETRY);
}

static __inline void
uvm_pagefree(struct vm_page *pg)
{
	vm_page_freezwq(pg);
}

/* Anonymous object allocation */

static __inline struct vm_object *
uao_create(size_t size, int flags)
{
	struct vm_object *object;

	KKASSERT(flags == 0);

	/* The object should be pageable (e.g., object of guest physical
	 * memory), so choose default_pager. */
	object = default_pager_alloc(NULL, size, VM_PROT_DEFAULT, 0);
	vm_object_set_flag(object, OBJ_NOSPLIT);

	return object;
}

/* Create an additional reference to the named anonymous memory object. */
static __inline void
uao_reference(struct vm_object *object)
{
	vm_object_reference_quick(object);
}

/* Remove a reference from the named anonymous memory object, destroying it
 * if the last reference is removed. */
static __inline void
uao_detach(struct vm_object *object)
{
	vm_object_deallocate(object);
}

#endif /* _NVMM_COMPAT_H_ */
