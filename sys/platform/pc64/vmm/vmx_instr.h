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

#ifndef _VMM_VMX_INSTR_H_
#define _VMM_VMX_INSTR_H_

#include <vm/pmap.h>

/*
 * Chapter 30 VMX Instruction Reference
 * Section 30.3 "Conventions"
 * from Intel Architecture Manual 3C.
 */
#define	VM_SUCCEED		0
#define	VM_FAIL_INVALID		1
#define	VM_FAIL_VALID		2
#define	VM_EXIT			3

#define	GET_ERROR_CODE				\
	"		jnc 1f;"		\
	"		mov $1, %[err];"	\
	"		jmp 4f;"		\
	"1:		jnz 3f;"		\
	"		mov $2, %[err];"	\
	"		jmp 4f;"		\
	"3:		mov $0, %[err];"	\
	"4:"

static inline int
vmxon(char *vmx_region)
{
	int err;
	uint64_t paddr;

	paddr = vtophys(vmx_region);
	__asm __volatile("vmxon %[paddr];"
			 GET_ERROR_CODE
			 : [err] "=r" (err)
			 : [paddr] "m" (paddr)
			 : "memory");

	return err;
}

static inline void
vmxoff(void)
{

	__asm __volatile("vmxoff");
}

static inline int
vmclear(char *vmcs_region)
{
	int err;
	uint64_t paddr;

	paddr = vtophys(vmcs_region);
	__asm __volatile("vmclear %[paddr];"
			 GET_ERROR_CODE
			 : [err] "=r" (err)
			 : [paddr] "m" (paddr)
			 : "memory");
	return err;
}

static inline void
vmptrst(uint64_t *addr)
{

	__asm __volatile("vmptrst %[addr]"
			:
			: [addr] "m" (*addr)
			: "memory");
}

static inline int
vmptrld(char *vmcs)
{
	int err;
	uint64_t paddr;

	paddr = vtophys(vmcs);
	__asm __volatile("vmptrld %[paddr];"
			 GET_ERROR_CODE
			 : [err] "=r" (err)
			 : [paddr] "m" (paddr)
			 : "memory");
	return err;
}

static inline int
vmwrite(uint64_t reg, uint64_t val)
{
	int err;

	__asm __volatile("vmwrite %[val], %[reg];"
			 GET_ERROR_CODE
			 : [err] "=r" (err)
			 : [val] "r" (val), [reg] "r" (reg)
			 : "memory");

	return err;
}

static inline int
vmread(uint64_t reg, uint64_t *addr)
{
	int err;

	__asm __volatile("vmread %[reg], %[addr];"
			 GET_ERROR_CODE
			 : [err] "=r" (err)
			 : [reg] "r" (reg), [addr] "m" (*addr)
			 : "memory");

	return err;
}

static inline int
invept(uint64_t type, uint64_t *desc_addr)
{
	int err;

	__asm __volatile("invept %[desc_addr], %[type];"
			 GET_ERROR_CODE
			 : [err] "=r" (err)
			 : [desc_addr] "m" (*desc_addr), [type] "r" (type)
			 : "memory");
	return err;
}

#endif
