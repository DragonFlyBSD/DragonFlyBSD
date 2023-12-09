/*
 * Copyright (c) 2009 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Sepherosa Ziehau <sepherosa@gmail.com>
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus_dma.h>
#include <sys/mbuf.h>
#include <sys/malloc.h>

static void
_bus_dmamem_coherent_cb(void *arg, bus_dma_segment_t *segs, int nseg, int error)
{
	bus_addr_t *addr = arg;

	if (error)
		return;

	KASSERT(nseg == 1, ("too many DMA segments, %d should be 1", nseg));
	*addr = segs->ds_addr;
}

int
bus_dmamem_coherent(bus_dma_tag_t parent,
		    bus_size_t alignment, bus_size_t boundary,
		    bus_addr_t lowaddr, bus_addr_t highaddr,
		    bus_size_t maxsize, int flags,
		    bus_dmamem_t *dmem)
{
	int error;

	bzero(dmem, sizeof(*dmem));

	error = bus_dma_tag_create(parent, alignment, boundary,
				   lowaddr, highaddr,
				   maxsize, 1, maxsize, 0,
				   &dmem->dmem_tag);
	if (error)
		return error;

        error = bus_dmamem_alloc(dmem->dmem_tag, &dmem->dmem_addr,
				 flags | BUS_DMA_COHERENT, &dmem->dmem_map);
        if (error) {
		bus_dma_tag_destroy(dmem->dmem_tag);
		bzero(dmem, sizeof(*dmem));
                return error;
	}

	error = bus_dmamap_load(dmem->dmem_tag, dmem->dmem_map,
				dmem->dmem_addr, maxsize,
				_bus_dmamem_coherent_cb, &dmem->dmem_busaddr,
				flags & BUS_DMA_NOWAIT);
	if (error) {
		if (error == EINPROGRESS) {
			panic("DMA coherent memory loading is still "
			      "in progress\n");
		}
		bus_dmamem_free(dmem->dmem_tag, dmem->dmem_addr,
				dmem->dmem_map);
		bus_dma_tag_destroy(dmem->dmem_tag);
		bzero(dmem, sizeof(*dmem));
		return error;
	}
	return 0;
}

void *
bus_dmamem_coherent_any(bus_dma_tag_t parent,
			bus_size_t alignment, bus_size_t size, int flags,
			bus_dma_tag_t *dtag, bus_dmamap_t *dmap,
			bus_addr_t *busaddr)
{
	bus_dmamem_t dmem;
	int error;

	error = bus_dmamem_coherent(parent, alignment, 0,
				    BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR,
				    size, flags, &dmem);
	if (error)
		return NULL;

	*dtag = dmem.dmem_tag;
	*dmap = dmem.dmem_map;
	*busaddr = dmem.dmem_busaddr;

	return dmem.dmem_addr;
}

int
bus_dmamap_load_mbuf_defrag(bus_dma_tag_t dmat, bus_dmamap_t map,
			    struct mbuf **m_head,
			    bus_dma_segment_t *segs, int maxsegs,
			    int *nsegs, int flags)
{
	struct mbuf *m = *m_head;
	int error;

	error = bus_dmamap_load_mbuf_segment(dmat, map, m,
			segs, maxsegs, nsegs, flags);
	if (error == EFBIG) {
		struct mbuf *m_new;

		m_new = m_defrag(m, M_NOWAIT);
		if (m_new == NULL)
			return ENOBUFS;
		else
			*m_head = m = m_new;

		error = bus_dmamap_load_mbuf_segment(dmat, map, m,
				segs, maxsegs, nsegs, flags);
	}
	return error;
}
