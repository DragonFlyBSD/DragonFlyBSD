/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * DMA-coherent memory helpers.
 *
 * GSP boot needs system-memory buffers that are simultaneously visible
 * to the host CPU (for staging firmware and inspecting results) and to
 * the GPU (for ucode DMA loads, WPR descriptors, msgq rings).
 *
 * The early GSP boot chain is more restrictive than normal PCI DMA: SEC2 and
 * the r570 booter must be able to fetch these pages before RM has configured
 * the regular runtime path, so allocations are constrained below 4 GiB.
 */

#include "nvgsp_priv.h"

static void
nvgsp_dma_capture_paddr(void *arg, bus_dma_segment_t *segs, int nseg, int error)
{
	bus_addr_t *paddr = arg;

	if (error != 0)
		return;
	KKASSERT(nseg == 1);
	*paddr = segs[0].ds_addr;
}

int
nvgsp_dma_alloc_dmamem(struct nvgsp_state *sc, bus_size_t size,
    bus_size_t alignment, struct nvgsp_dmamem *out)
{
	void *kva;
	bus_dma_tag_t tag = NULL;
	bus_dmamap_t  map = NULL;
	bus_addr_t paddr = 0;

	/*
	 * SEC2/GSP boot firmware consumes these physical addresses before the
	 * normal RM/VMM runtime exists.  Keep them below 4 GiB: the r570 booter
	 * accepts a 64-bit mailbox value, but TU102 fails the handoff when the
	 * WPR metadata and LibOS argument pages are allocated in high sysmem.
	 */
	if (bus_dma_tag_create(bus_get_dma_tag(sc->dev), alignment, 0,
	    BUS_SPACE_MAXADDR_32BIT, BUS_SPACE_MAXADDR, size, 1, size, 0,
	    &tag) != 0)
		return (ENOMEM);

	kva = NULL;
	if (bus_dmamem_alloc(tag, &kva, BUS_DMA_WAITOK | BUS_DMA_ZERO |
	    BUS_DMA_COHERENT, &map) != 0) {
		bus_dma_tag_destroy(tag);
		return (ENOMEM);
	}
	if (bus_dmamap_load(tag, map, kva, size, nvgsp_dma_capture_paddr,
	    &paddr, BUS_DMA_WAITOK) != 0) {
		bus_dmamem_free(tag, kva, map);
		bus_dma_tag_destroy(tag);
		return (ENOMEM);
	}
	if (kva == NULL)
		return (ENOMEM);

	out->kva = kva;
	out->paddr = paddr;
	out->size = size;
	out->tag = tag;
	out->map = map;
	return (0);
}

void
nvgsp_dma_free_dmamem(struct nvgsp_state *sc __unused, struct nvgsp_dmamem *mem)
{
	if (mem->kva == NULL)
		return;
	bus_dmamap_unload(mem->tag, mem->map);
	bus_dmamem_free(mem->tag, mem->kva, mem->map);
	bus_dma_tag_destroy(mem->tag);
	mem->kva = NULL;
	mem->paddr = 0;
	mem->size = 0;
	mem->tag = NULL;
	mem->map = NULL;
}
