/*
 * Copyright (c) 2015-2020 François Tigeot <ftigeot@wolfpond.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
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

#ifndef _ASM_PGTABLE_TYPES_H_
#define _ASM_PGTABLE_TYPES_H_

#include <cpu/pmap.h>

#define _PAGE_PRESENT	X86_PG_V
#define _PAGE_RW	X86_PG_RW
#define _PAGE_PWT	X86_PG_NC_PWT
#define _PAGE_PCD	X86_PG_NC_PCD
#define _PAGE_ACCESSED	X86_PG_A
#define _PAGE_DIRTY	X86_PG_M
#define _PAGE_PAT	X86_PG_PTE_PAT
#define _PAGE_GLOBAL	X86_PG_G
#define _PAGE_NX	X86_PG_NX

#define _PAGE_CACHE_WC		_PAGE_PWT
#define _PAGE_CACHE_UC_MINUS	_PAGE_PCD

#define _PAGE_CACHE_MASK	(_PAGE_PWT | _PAGE_PCD | _PAGE_PAT)

#define __PAGE_KERNEL_EXEC						\
	(_PAGE_PRESENT | _PAGE_RW | _PAGE_DIRTY | _PAGE_ACCESSED | _PAGE_GLOBAL)
#define __PAGE_KERNEL	(__PAGE_KERNEL_EXEC | _PAGE_NX)

#define PAGE_KERNEL	__PAGE_KERNEL
#define PAGE_KERNEL_IO	__PAGE_KERNEL

static inline pgprot_t
pgprot_writecombine(pgprot_t prot)
{
	return (prot | VM_MEMATTR_WRITE_COMBINING);
}

#define __pgprot(value)	((pgprot_t) {(value)})
#define pgprot_val(x)	(x)

#endif	/* _ASM_PGTABLE_TYPES_H_ */
