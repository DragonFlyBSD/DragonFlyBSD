/*-
 * Copyright (c) 2016 Microsoft Corp.
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>

#include <dev/virtual/hyperv/include/hyperv_busdma.h>

void
hyperv_dma_map_paddr(void *arg, bus_dma_segment_t *segs, int nseg, int error)
{
	bus_addr_t *paddr = arg;

	if (error)
		return;

	KASSERT(nseg == 1, ("too many segments %d!", nseg));
	*paddr = segs->ds_addr;
}

void *
hyperv_dmamem_alloc(bus_dma_tag_t parent_dtag, bus_size_t alignment,
    bus_addr_t boundary, bus_size_t size, struct hyperv_dma *dma, int flags)
{
	bus_dmamem_t dmem;
	int error;

	error = bus_dmamem_coherent(parent_dtag, alignment, boundary,
	    BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR, size, flags, &dmem);
	if (error)
		return NULL;

	dma->hv_dtag = dmem.dmem_tag;
	dma->hv_dmap = dmem.dmem_map;
	dma->hv_paddr = dmem.dmem_busaddr;
	return dmem.dmem_addr;
}

void
hyperv_dmamem_free(struct hyperv_dma *dma, void *ptr)
{
	bus_dmamap_unload(dma->hv_dtag, dma->hv_dmap);
	bus_dmamem_free(dma->hv_dtag, ptr, dma->hv_dmap);
	bus_dma_tag_destroy(dma->hv_dtag);
}
