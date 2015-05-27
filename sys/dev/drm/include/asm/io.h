/*
 * Copyright (c) 2014-2015 François Tigeot
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

#ifndef _ASM_IO_H_
#define _ASM_IO_H_

#include <machine/pmap.h>
#include <vm/vm.h>

#include <asm/page.h>

#define ioread8(addr)		*(volatile uint8_t *)((char *)addr)
#define ioread16(addr)		*(volatile uint16_t *)((char *)addr)
#define ioread32(addr)		*(volatile uint32_t *)((char *)addr)

#define iowrite8(data, addr)	*(volatile uint8_t *)((char *)addr) = data;
#define iowrite16(data, addr)	*(volatile uint16_t *)((char *)addr) = data;
#define iowrite32(data, addr)	*(volatile uint32_t *)((char *)addr) = data;

/* ioremap: map bus memory into CPU space */
static inline void __iomem *ioremap(resource_size_t offset, unsigned long size)
{
	return pmap_mapdev_uncacheable(offset, size);
}

static inline void __iomem *ioremap_wc(resource_size_t phys_addr, unsigned long size)
{
	return pmap_mapdev_attr(phys_addr, size, VM_MEMATTR_WRITE_COMBINING);
}

static inline void iounmap(void __iomem *ptr, unsigned long size)
{
	pmap_unmapdev((vm_offset_t) ptr, size);
}

#define mmiowb cpu_sfence

#endif	/* _ASM_IO_H_ */
