/*
 * Copyright (c) 2016 Sascha Wildner <saw@online.de>
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

#include <linux/dma-mapping.h>

#include <vm/vm_extern.h>

void *
dma_alloc_coherent(struct device *dev, size_t size, dma_addr_t *dma_handle,
    gfp_t flag)
{
#if 1
	vm_paddr_t high;
	size_t align;
	void *mem;

#if 0 /* XXX swildner */
	if (dev->dma_mask)
		high = *dev->dma_mask;
	else
#endif
		high = BUS_SPACE_MAXADDR_32BIT;
	align = PAGE_SIZE << get_order(size);
	mem = (void *)kmem_alloc_contig(size, 0, high, align);
	if (mem)
		*dma_handle = vtophys(mem);
	else
		*dma_handle = 0;
	return (mem);
#endif

#if 0
	size_t align = PAGE_SIZE << get_order(size);
	int error;
	drm_dma_handle_t *dmah;

	dmah = kmalloc(sizeof(drm_dma_handle_t), M_DRM, M_ZERO | M_NOWAIT);
	if (dmah == NULL)
		return NULL;

	error = bus_dmamem_coherent_any(
		bus_get_dma_tag(dev->dev->bsddev),
		align, size, BUS_DMA_WAITOK | BUS_DMA_ZERO | BUS_DMA_NOCACHE,
		&dmah->tag, &dmah->map, &dmah->vaddr);
	if (error != 0) {
		kfree(dmah);
		return NULL;
	}	
	return dmah;
#endif
}

void
dma_free_coherent(struct device *dev, size_t size, void *cpu_addr,
    dma_addr_t dma_handle)
{

	kmem_free(kernel_map, (vm_offset_t)cpu_addr, size);
}
