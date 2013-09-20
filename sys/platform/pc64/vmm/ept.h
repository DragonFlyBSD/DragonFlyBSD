/*
 * Copyright (c) 2003-2013 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Mihai Carabas <mihai.carabas@gmail.com>
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

#ifndef _VMM_EPT_H_
#define _VMM_EPT_H_

#include <vm/vm.h>

#include <machine/pmap.h>

/* EPT defines */
#define	EPT_PWL4(cap)			((cap) & (1ULL << 6))
#define	EPT_MEMORY_TYPE_WB(cap)		((cap) & (1UL << 14))
#define	EPT_AD_BITS_SUPPORTED(cap)	((cap) & (1ULL << 21))
#define	EPT_PG_READ			(0x1ULL << 0)
#define	EPT_PG_WRITE			(0x1ULL << 1)
#define	EPT_PG_EXECUTE			(0x1ULL << 2)
#define	EPT_IGNORE_PAT			(0x1ULL << 6)
#define	EPT_PG_PS			(0x1ULL << 7)
#define	EPT_PG_A			(0x1ULL << 8)
#define	EPT_PG_M			(0x1ULL << 9)
#define	EPT_PG_AVAIL1			(0x1ULL << 10)
#define	EPT_PG_AVAIL2			(0x1ULL << 11)
#define	EPT_PG_AVAIL3			(0x1ULL << 52)
#define	EPT_PWLEVELS			(4)	/* page walk levels */

#define	EPTP_CACHE(x)			(x)
#define	EPTP_PWLEN(x)			((x) << 3)
#define	EPTP_AD_ENABLE			(0x1ULL << 6)

#define	EPT_MEM_TYPE_SHIFT		(0x3)
#define	EPT_MEM_TYPE_UC			(0x0ULL << EPT_MEM_TYPE_SHIFT)
#define	EPT_MEM_TYPE_WC			(0x1ULL << EPT_MEM_TYPE_SHIFT)
#define	EPT_MEM_TYPE_WT			(0x4ULL << EPT_MEM_TYPE_SHIFT)
#define	EPT_MEM_TYPE_WP			(0x5ULL << EPT_MEM_TYPE_SHIFT)
#define	EPT_MEM_TYPE_WB			(0x6ULL << EPT_MEM_TYPE_SHIFT)
#define	EPT_MEM_TYPE_MASK		(0x7ULL << EPT_MEM_TYPE_SHIFT)

#define	EPT_VIOLATION_READ		(1ULL << 0)
#define	EPT_VIOLATION_WRITE		(1ULL << 1)
#define	EPT_VIOLATION_INST_FETCH	(1ULL << 2)
#define	EPT_VIOLATION_GPA_READABLE	(1ULL << 3)
#define	EPT_VIOLATION_GPA_WRITEABLE	(1ULL << 4)
#define	EPT_VIOLATION_GPA_EXECUTABLE	(1ULL << 5)

#define	INVEPT_TYPE_SINGLE_CONTEXT	1UL
#define	INVEPT_TYPE_ALL_CONTEXTS	2UL

struct invept_desc {
	uint64_t	eptp;
	uint64_t	_res;
};
typedef struct invept_desc invept_desc_t;

CTASSERT(sizeof(struct invept_desc) == 16);

int vmx_ept_init(void);
void vmx_ept_pmap_pinit(pmap_t pmap);
uint64_t vmx_eptp(uint64_t ept_address);

static __inline int
vmx_ept_fault_type(uint64_t qualification)
{
	if (qualification & EPT_VIOLATION_WRITE)
		return VM_PROT_WRITE;
	else if (qualification & EPT_VIOLATION_INST_FETCH)
		return VM_PROT_EXECUTE;
	else
		return VM_PROT_READ;
}

static __inline int
vmx_ept_gpa_prot(uint64_t qualification)
{
	int prot = 0;

	if (qualification & EPT_VIOLATION_GPA_READABLE)
		prot |= VM_PROT_READ;

	if (qualification & EPT_VIOLATION_GPA_WRITEABLE)
		prot |= VM_PROT_WRITE;

	if (qualification & EPT_VIOLATION_GPA_EXECUTABLE)
		prot |= VM_PROT_EXECUTE;

	return prot;
}

#endif
