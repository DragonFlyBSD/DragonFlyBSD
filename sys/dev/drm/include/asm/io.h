/*
 * Copyright (c) 2014-2016 François Tigeot
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
#include <vm/pmap.h>
#include <vm/vm.h>

#include <linux/types.h>
#include <asm/page.h>

#undef writeb
static inline void
writeb(u8 value, volatile void __iomem *addr)
{
	*(volatile uint8_t *)addr = value;
}

#undef writew
static inline void
writew(u16 value, volatile void __iomem *addr)
{
	*(volatile uint16_t *)addr = value;
}

#undef writel
static inline void
writel(u32 value, volatile void __iomem *addr)
{
	*(volatile uint32_t *)addr = value;
}

#undef writeq
static inline void
writeq(u64 value, volatile void __iomem *addr)
{
	*(volatile uint64_t *)addr = value;
}

#define ioread8(addr)		*(volatile uint8_t *)((char *)addr)
#define ioread16(addr)		*(volatile uint16_t *)((char *)addr)
#define ioread32(addr)		*(volatile uint32_t *)((char *)addr)

#define iowrite8(data, addr)					\
	do {							\
		*(volatile uint8_t *)((char *)addr) = data;	\
	} while (0)

#define iowrite16(data, addr)					\
	do {							\
		*(volatile uint16_t *)((char *)addr) = data;	\
	} while (0)

#define iowrite32(data, addr)					\
	do {							\
		*(volatile uint32_t *)((char *)addr) = data;	\
	} while (0)

#include <linux/vmalloc.h>

/* ioremap function family: map bus addresses into CPU space */

struct iomap {
	vm_paddr_t paddr;
	int npages;
	void *pmap_addr;
	SLIST_ENTRY(iomap) im_iomaps;
};

void __iomem *
__ioremap_common(unsigned long phys_addr, unsigned long size, int cache_mode);

static inline void __iomem *
ioremap_nocache(resource_size_t phys_addr, unsigned long size)
{
	return __ioremap_common(phys_addr, size, PAT_UNCACHEABLE);
}

static inline void __iomem *
ioremap(resource_size_t offset, unsigned long size)
{
	return ioremap_nocache(offset, size);
}

static inline void __iomem *
ioremap_wc(resource_size_t phys_addr, unsigned long size)
{
	return __ioremap_common(phys_addr, size, PAT_WRITE_COMBINING);
}

static inline void __iomem *
ioremap_wt(resource_size_t phys_addr, unsigned long size)
{
	return __ioremap_common(phys_addr, size, PAT_WRITE_THROUGH);
}

void iounmap(void __iomem *ptr);

/* XXX these should have volatile */
#define	memset_io(a, b, c)	memset((a), (b), (c))
#define	memcpy_fromio(a, b, c)	memcpy((a), (b), (c))
#define	memcpy_toio(a, b, c)	memcpy((a), (b), (c))

#define mmiowb cpu_sfence

#endif	/* _ASM_IO_H_ */
